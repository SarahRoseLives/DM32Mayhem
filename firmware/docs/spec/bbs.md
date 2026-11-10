<!-- SPDX-License-Identifier: MIT -->

# Packet BBS

A small connected-mode AX.25 BBS that runs in the radio: the radio answers a caller over the 1200 baud AFSK modem it
already has for APRS, keeps a mailbox in the data flash, and is configured entirely on its own screen. It is the
`CONFIG_BBS` feature (`core/bbs`, `core/ui/ui_svc_bbs.c`); a build without it is unaffected.

## 1. Layers

| Part | Where |
|---|---|
| Configuration, validation and the defaults | `core/bbs/bbs_config.{h,c}` |
| The configuration in the settings store | `core/bbs/bbs_settings.{h,c}` (`settings_t` `bbs_*` fields) |
| The session app: banner, commands, mailbox | `core/bbs/bbs.c` |
| The AX.25 2.2 connected-mode server (SABM / UA / DISC / I / S) | `core/bbs/ax25_link.{h,c}` |
| The flash mailbox (512-byte records) | `core/bbs/bbs_store.{h,c}` |
| Its backend on the data flash (the board's data-flash native area) | `core/bbs/bbs_flash.c` |
| Radio glue: the APRS modem, the store lock, start/stop | `core/bbs/bbs_radio.c` |
| The UI task's calls, the panel and the mailbox screens | `core/ui/ui_svc_bbs.c`, `ui_menus.c` |

The RF path is the APRS engine's: the AFSK modulator, the HDLC framing, the keying and the receive demodulator
(`core/aprs`, `docs/spec/aprs.md`). The BBS frames go through `aprs_queue_raw_frame()` in the `K_BBS` queue kind, which
skips the APRS carrier-sense wait so a reply follows the caller's frame after a short settle (`aprs_rf_job_t.immediate`,
`radio_sm.c`). The APRS transmit jobs (beacons, messages) are off on the BBS channel.

## 2. Configuration

`bbs_cfg_t` (`bbs_config.h`): the base callsign (1-6 letters or digits), the SSID (0-15), the frequency, the
bandwidth (12.5 or 25 kHz), the BBS name and sysop call shown in the banner, the KISS TXDELAY (10 ms units, 0 = the
radio's default), the CW ID interval (0 off, 1 5 min, 2 10, 3 15, 4 30) and whether to start at power-on. The
defaults are in `bbs_config.h` (`N0CALL`, 145.100 MHz, 25 kHz, 300 ms, CW ID off, and the build's BBS name and
sysop).

`bbs_settings_load()` / `bbs_settings_save()` (`bbs_settings.c`) use the append-only `settings_t` fields
(`bbs_freq_hz`, `bbs_flags`, `bbs_ssid`, `bbs_bw`, `bbs_txdelay`, `bbs_cwid`, `bbs_call`, `bbs_name`, `bbs_sysop`;
[settings-store.md](settings-store.md) 3). A record from a build without them loads the defaults. The fields exist only
in a `CONFIG_BBS` build.

## 3. The radio's screen

With `CONFIG_BBS` the **OK menu is the BBS panel**; the usual Hub rows (Contacts, Messages, Zones, Channels, Tools,
Settings) are not offered (`ui_menus.c`). Home stays as the idle screen. The panel ([docs/ux](../ux/)):

| Row | Action |
|---|---|
| Start BBS / Stop BBS | Tune and enable, or stop |
| Frequency | Type the frequency on the digits (the existing band-checked entry, `ui_freq_entry_*`); OK keeps it and retunes while running |
| Bandwidth | 12.5 or 25 kHz |
| Callsign | Text entry (the existing keypad letters) |
| SSID | Type 0-15 |
| BBS name | Text entry |
| Sysop | Text entry |
| TX delay | Type the preamble in ms (0-1000) |
| Start at power on | On or off |
| CW ID | Off, or every 5, 10, 15 or 30 minutes |
| Messages | The mailbox: a list, then one message |
| Clear messages | Long OK; the deletes run in the radio task |

The status block above the rows shows running or stopped, the call, the frequency and, while a caller is connected, the
peer and the message count.

**Start** tunes the VFO to the configured frequency (FM, the chosen bandwidth) and enables the engine. While it runs the
radio is locked to that channel: `ui_app.c` drops navigation, channel/zone/VFO changes, scan and quick settings, and the
input layer's PTT block keeps the user from transmitting over the BBS (`ui_svc_ptt_block`). The BBS panel and its
message screens still work, so Stop is always reachable. Editing the frequency on the panel is the one retune allowed.
**Stop** releases the lock. Volume, power and the backlight are unaffected.

The message list reads each live record through the locked store; long OK queues a delete. Delete and Clear are flash
work and run in the radio task (`bbs_ui_kill` / `bbs_ui_clear`), one record a poll for Clear; the UI polls
`bbs_ui_poll` and says when it finished.

## 4. The connected-mode server

`ax25_link.c` implements the server side of AX.25 2.2 connected mode with modulo-8 sequence numbers, k = 4, N2 = 10
and a 4 s T1. It answers SABM with UA (a second caller while both links are in use gets DM busy), acknowledges I frames
with an RR carrying N(R) (also for a duplicate or a gap), retransmits unacknowledged I frames on T1, answers polls, and
completes DISC with UA. A disconnect drops the session. Frames are decoded without a PID for U and S frames (a 15-byte
SABM/UA is valid).

The BBS commands (`bbs.c`): `L` list, `R n` read, `S` send (To, Subject, then body lines ending with `/EX`), `K n` kill,
`N` node information, `H` help, `Q` disconnect. The banner and node line use the configured name, call, sysop and
frequency.

## 5. CW identification

While the BBS runs, `radio_sm` keys the configured callsign in Morse on the BBS channel every interval the panel sets
(CW ID: off, 5, 10, 15 or 30 minutes), using the radio's CW keyer (`morse.c`, `radio_cwid.c`, the settings' speed and
tone; `docs/spec/mmdvm-protocol.md` 2.11). It keys only when the BBS is enabled, the radio is idle FM on the BBS
channel, no BBS/APRS frame is waiting, and the channel has been quiet for 2 s, so it never cuts a session or a reply.
The first ID comes shortly after Start; each next one an interval later. Stop or a retune ends the schedule. The first
ID also proves the keyer works without a caller.

## 6. The mailbox

One 512-byte record per message in the native flash area (header magic, type, flags, message number, time, from, to,
subject, body up to 420 bytes, CRC-32). `bbs_flash_backend()` claims the area with a header and never touches a sector
that holds another firmware's bytes; `storage_lock()` serializes the radio task's and the UI task's access. A torn write
(magic, bad CRC) is skipped. The store is scanned at start into a RAM index; a killed record is flagged and dropped from
the index. Clear rewrites one live record a poll until none remain.

## 7. Limits

1200 baud Bell 202 only (the board's audio path; no 9600); two simultaneous callers; no forwarding, NTS or bulletins
beyond the message type; the DMR voice codec is not used or needed in this build.
