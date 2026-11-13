#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Generate the per-app pages (web/site/apps/*.html) and their index from one catalog. The landing page links here.
# The screenshots live in web/site/img/screens and are captured from a radio running the build.
#   python3 web/tools/gen_apps.py
import html
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SITE = os.path.join(HERE, "..", "site")
OUT = os.path.join(SITE, "apps")

GROUPS = [
    ("trx", "Transceiver", "Two-way apps: the ones where the radio keys on your command."),
    ("tx", "Transmit", "One-shot transmitters. Each queues a job and the radio keys, tunes and unkeys itself."),
    ("rx", "Receive", "Listeners that turn the FM audio into something you can read."),
]

# Each app: id, name, group, menu, summary, what (paragraphs), steps, fields ([label, text]), shots ([file, caption]),
# note (optional), available (the page exists).
APPS = [
    {
        "id": "fm",
        "name": "FM transceiver",
        "group": "trx",
        "menu": "TRX → FM",
        "summary": "A real FM transceiver with live PTT, repeater offset and CTCSS.",
        "what": [
            "The one screen where the handheld behaves like a normal FM radio. Set the transmit and receive frequencies "
            "independently, pick a repeater offset, and give RX and TX their own CTCSS tone.",
            "Hold PTT to talk and release to listen. The header turns red and shows TX while keyed, and the Signal row "
            "shows the live level.",
        ],
        "steps": [
            "Open <b>TRX → FM</b>.",
            "Set <b>TX</b> and <b>RX</b>; TX is the frequency the radio transmits on.",
            "Set <b>Offset</b> for a repeater, and <b>CTCSS RX</b> / <b>CTCSS TX</b> if the repeater uses a tone.",
            "Hold <b>PTT</b> to talk; release to listen.",
        ],
        "fields": [
            ["TX", "Transmit frequency."],
            ["RX", "Receive frequency."],
            ["Offset", "Off, up/down shift, or a split frequency."],
            ["CTCSS RX / TX", "Per-direction tone."],
            ["Signal", "Live receive level in dBm."],
        ],
        "shots": [["fm-trx.png", "The FM transceiver, idle."], ["fm-tx.png", "Keyed: the header shows TX."]],
        "note": "Manual PTT is only in this screen. Everywhere else the apps key the radio themselves.",
    },
    {
        "id": "bbs",
        "name": "Packet BBS",
        "group": "trx",
        "menu": "TRX → BBS",
        "summary": "A small connected-mode packet mailbox that runs in the radio.",
        "what": [
            "A packet BBS on the radio's 1200-baud AFSK modem. It answers a caller, shows a banner, runs the usual "
            "connected-mode commands and keeps a mailbox in the data flash.",
            "It is configured entirely on its own screen, and can start automatically at power-on.",
        ],
        "steps": [
            "Open <b>TRX → BBS</b>.",
            "Set <b>Frequency</b>, <b>Bandwidth</b>, <b>Callsign</b> and <b>SSID</b>, plus the <b>BBS name</b> and <b>Sysop</b> shown in the banner.",
            "Set <b>TX delay</b> and the <b>CW ID</b> interval if you want an identifier.",
            "Select <b>Start BBS</b>; the header shows Running with the callsign and frequency.",
        ],
        "fields": [
            ["Start BBS", "Start or stop the server."],
            ["Frequency / Bandwidth", "The RF channel, 12.5 or 25 kHz."],
            ["Callsign / SSID", "The BBS's own AX.25 address."],
            ["BBS name / Sysop", "Shown in the banner."],
            ["TX delay", "KISS TXDELAY in 10 ms units."],
            ["Start at power on", "Run the BBS as soon as the radio boots."],
            ["CW ID", "Morse identifier interval: off, 5, 10, 15 or 30 minutes."],
        ],
        "shots": [["bbs.png", "The BBS screen, stopped and ready to start."]],
        "note": "The RF path is the APRS engine's; APRS beacons are off while the BBS runs.",
    },
    {
        "id": "kiss-tnc",
        "name": "KISS TNC",
        "group": "trx",
        "menu": "TRX → KISS TNC",
        "summary": "A KISS packet TNC over USB for APRS programs on a computer or phone.",
        "what": [
            "Turns the radio into a KISS packet TNC on the USB serial port, so APRS software can send and receive "
            "through it. The screen shows the channel, callsign and packet counters.",
        ],
        "steps": [
            "Open <b>TRX → KISS TNC</b>.",
            "Point your APRS program at the radio's serial port in KISS mode.",
            "Watch <b>Heard</b>, <b>KISS in</b> and <b>KISS out</b> to confirm traffic.",
        ],
        "fields": [
            ["Status", "The TNC state."],
            ["Frequency / Callsign", "The APRS channel and station address."],
            ["Heard", "Frames heard on the channel."],
            ["KISS in / out", "Frames from and to the computer."],
            ["Serial", "The USB serial setting: KISS 115200 8N1."],
        ],
        "shots": [["kiss-tnc.png", "The KISS TNC, reporting the serial port."]],
        "note": "Needs a build with KISS and hostlink; the callsign is the APRS callsign.",
    },
    {
        "id": "same",
        "name": "SAME",
        "group": "tx",
        "menu": "TX → SAME",
        "summary": "NOAA/EAS SAME weather headers over AFSK.",
        "what": [
            "Sends a SAME header — the 520.83-baud AFSK burst used by NOAA Weather Radio and the Emergency Alert "
            "System — on a weather frequency. The header is assembled from the originator, event, FIPS location and "
            "duration.",
        ],
        "steps": [
            "Open <b>TX → SAME</b>.",
            "Set <b>Originator</b>, <b>Event</b>, the <b>FIPS</b> state and county, and the <b>Duration</b>.",
            "Set the <b>Frequency</b> and <b>Station ID</b>; the header preview updates at the top.",
            "Select <b>Transmit SAME</b>.",
        ],
        "fields": [
            ["Originator", "WXR, EAS, CIV or PEP."],
            ["Event", "The three-letter event code, e.g. RWT."],
            ["FIPS state / county", "The location code."],
            ["Duration h / min", "How long the alert runs."],
            ["Frequency / Station ID", "The channel and sender ID."],
            ["Transmit SAME", "Send the header."],
        ],
        "shots": [["same-tx.png", "SAME TX with a weather header ready."]],
        "note": "Only transmit on frequencies you are licensed for; SAME is a real alerting signal.",
    },
    {
        "id": "ook",
        "name": "OOK",
        "group": "tx",
        "menu": "TX → OOK",
        "summary": "On-off keying for fixed-code remotes and 433.92 MHz devices.",
        "what": [
            "Keys the carrier on and off to send fixed-code remote signals. Use a hex code for a known device, or a "
            "de Bruijn sequence to sweep every code of a given order.",
        ],
        "steps": [
            "Open <b>TX → OOK</b>.",
            "Set <b>Frequency</b> and <b>Bit length</b>.",
            "Pick <b>Mode</b> (De Bruijn or Hex) and <b>Data</b> (an order, or a code).",
            "Set <b>Repeat</b> and select <b>Transmit OOK</b>.",
        ],
        "fields": [
            ["Frequency", "The carrier, e.g. 433.920 MHz."],
            ["Bit length", "Pulse width per bit in microseconds."],
            ["Mode", "De Bruijn sweep or a hex code."],
            ["Data", "The sequence order or the code."],
            ["Repeat", "How many times to send it."],
        ],
        "shots": [["ook-tx.png", "OOK TX sending a de Bruijn sequence."]],
        "note": "Timing is limited by the PA settling time — about 1 ms per bit.",
    },
    {
        "id": "tv",
        "name": "TV",
        "group": "tx",
        "menu": "TX → TV",
        "summary": "An analog TV test-pattern carrier on cable and UHF channels.",
        "what": [
            "Transmits a TV test pattern as an AM video carrier on a cable or UHF channel, with a selectable pattern "
            "and bit timing. Useful for testing old TVs and video gear.",
        ],
        "steps": [
            "Open <b>TX → TV</b>.",
            "Pick a <b>Channel</b>; the <b>Frequency</b> follows.",
            "Choose a <b>Pattern</b> and set the <b>Bit</b> timing.",
            "Select <b>Send pattern</b> to start, and again to stop.",
        ],
        "fields": [
            ["Channel", "Cable 54–66 and UHF 14/15."],
            ["Frequency", "Derived from the channel."],
            ["Pattern", "Bars, Stripes, Checker, Split or Field."],
            ["Bit", "Video bit timing in microseconds."],
            ["Send pattern", "Start or stop the carrier."],
        ],
        "shots": [["tv-tx.png", "TV TX on Cable 54."]],
        "note": "Check your licence before transmitting; this keys a video carrier.",
    },
    {
        "id": "mdc",
        "name": "MDC-1200",
        "group": "tx",
        "menu": "TX → MDC",
        "summary": "MDC-1200 ANI and PTT ID bursts.",
        "what": [
            "Sends the MDC-1200 signalling burst that many commercial radios use for unit identification (ANI) and PTT "
            "ID.",
        ],
        "steps": [
            "Open <b>TX → MDC</b>.",
            "Set <b>Frequency</b> and the <b>Opcode</b>.",
            "Set the <b>Unit ID</b> and <b>Source</b> bytes.",
            "Select <b>Transmit MDC</b>.",
        ],
        "fields": [
            ["Frequency", "The channel to key."],
            ["Opcode / Argument", "The MDC message type."],
            ["Unit ID hi / lo", "The sending unit's number."],
            ["Source hi / lo", "The source fields."],
            ["Transmit MDC", "Send the burst."],
        ],
        "shots": [["mdc-tx.png", "MDC TX with a PTT ID opcode."]],
    },
    {
        "id": "p25",
        "name": "P25 control channel",
        "group": "tx",
        "menu": "TX → P25",
        "summary": "A P25 Phase 1 control channel transmitter.",
        "what": [
            "Builds and transmits a P25 Phase 1 control channel: the frame sync, BCH Network ID, TSBK, trellis coding, "
            "interleaving and outbound status symbols, rotating IDEN_UP, NET_STS, RFSS_STS and a group voice grant.",
            "The modem sends it as continuous 4FSK.",
        ],
        "steps": [
            "Open <b>TX → P25</b>.",
            "Set <b>Frequency</b>, <b>NAC</b> and <b>WACN</b>.",
            "Set the <b>System ID</b>, <b>RFSS</b>, <b>Site</b> and <b>Talkgroup</b>, plus the <b>Voice channel</b>.",
            "Select <b>Transmit P25 CC</b>; abort from the app.",
        ],
        "fields": [
            ["Frequency", "The control channel."],
            ["NAC / WACN / System ID", "The network identity."],
            ["RFSS / Site", "The site numbers."],
            ["Talkgroup / Voice channel", "What the grant points at."],
            ["Transmit P25 CC", "Start or abort."],
        ],
        "shots": [["p25cc-tx.png", "A P25 control channel ready to send."]],
        "note": "This is a test transmitter for P25 receivers, not a repeater.",
    },
    {
        "id": "aprs",
        "name": "APRS",
        "group": "tx",
        "menu": "TX → APRS",
        "summary": "Status, messages and position beacons.",
        "what": [
            "The APRS transmit app: send a status, a message to another station, or your current position, keying and "
            "unkeying the radio itself.",
            "Network builds add KISS and iGate paths to the APRS-IS.",
        ],
        "steps": [
            "Open <b>TX → APRS</b>.",
            "Set your <b>Callsign</b> and <b>Frequency</b>.",
            "Fill in a <b>Status</b>, or the <b>Message to</b> and <b>Message</b> rows.",
            "Select <b>Send status</b>, <b>Send message</b> or <b>Send position now</b>.",
        ],
        "fields": [
            ["Callsign", "Your station, with an SSID if you use one."],
            ["Frequency", "The APRS channel."],
            ["State / Status", "The status text to beacon."],
            ["Message to / Message", "A directed APRS message."],
            ["Send status / message / position", "Queue the transmission."],
        ],
        "shots": [["aprs-tx.png", "APRS TX with a callsign and channel set."]],
        "note": "Position needs a GPS fix; the network paths need the KISS/iGate build.",
    },
    {
        "id": "rtty",
        "name": "RTTY",
        "group": "tx",
        "menu": "TX → RTTY  ·  RX → RTTY",
        "summary": "ITA2 RTTY transmit and a real receive decoder.",
        "what": [
            "A two-tone FSK RTTY app. Transmit ITA2 text at 45.45 or 50 baud with a settable mark and shift, or listen "
            "and decode RTTY from the FM discriminator.",
            "On receive the text scrolls in a log and opens full screen from the Messages row.",
        ],
        "steps": [
            "<b>Transmit:</b> open TX → RTTY, set Frequency, Message, Baud, Mark, Shift and Repeat, then select Transmit RTTY.",
            "<b>Receive:</b> open RX → RTTY, set Frequency, Baud, Mark, Shift and Invert; it starts listening.",
            "Select <b>Stop listening</b> to stop, or <b>Messages</b> to read the log full screen.",
        ],
        "fields": [
            ["Frequency", "The channel."],
            ["Message", "The ITA2 text to send."],
            ["Baud", "45.45 or 50 baud."],
            ["Mark / Shift", "The tone and its shift, e.g. 2125 / 170 Hz."],
            ["Repeat", "Times to send the message."],
            ["Invert", "Flip the tones on receive."],
            ["Messages / Status", "The log, and the sync/error counts."],
        ],
        "shots": [
            ["rtty-tx.png", "RTTY TX with a message and 45.45 baud."],
            ["rtty-rx.png", "RTTY RX decoding a test line."],
            ["rtty-messages.png", "The receive log, full screen."],
        ],
    },
    {
        "id": "gps",
        "name": "GPS",
        "group": "rx",
        "menu": "RX → GPS",
        "summary": "Live position, satellites, and raw NMEA out of the USB serial.",
        "what": [
            "Shows the GPS fix, a sky plot of the satellites, and a Maidenhead locator, and streams raw NMEA "
            "($GPRMC and $GPGGA) out of the USB serial at 115200 8N1 while the screen is open.",
        ],
        "steps": [
            "Open <b>RX → GPS</b>.",
            "Press <b>Up</b> / <b>Down</b> to page between Status, Sky and Grid.",
            "Point a serial program at the radio's port to read the NMEA feed.",
        ],
        "fields": [
            ["Status", "Sats used/seen, latitude, longitude, altitude, speed, HDOP and UTC."],
            ["Sky", "A sky plot with GPS and BeiDou counts."],
            ["Grid", "The Maidenhead locator, e.g. EN91."],
            ["Serial", "NMEA 115200 8N1 while the screen is open."],
        ],
        "shots": [["gps-status.png", "GPS Status with a 3D fix."], ["gps-sky.png", "The satellite sky plot."]],
        "note": "NMEA is emitted only while the GPS screen is open; with no fix the status is V and the fields are zero.",
    },
]

GROUP_LABEL = {g[0]: g[1] for g in GROUPS}

HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} — DM32Mayhem</title>
<meta name="description" content="{desc}">
<link rel="icon" href="/img/dm32.svg" type="image/svg+xml">
<link rel="stylesheet" href="/css/tokens.css">
<link rel="stylesheet" href="/css/landing.css">
<link rel="stylesheet" href="/css/apps.css">
</head>
<body>
<header class="site-head">
	<div class="wrap head-row">
		<a class="brand" href="/">DM32<span>Mayhem</span></a>
		<nav class="site-nav">
			<a href="/#features">Features</a>
			<a href="/apps/" aria-current="page">Apps</a>
			<a href="/#limits">Limits</a>
			<a href="/flasher/">Flasher</a>
			<a href="https://github.com/SarahRoseLives/DM32Mayhem">Source</a>
			<a class="nav-donate" href="https://sarahsforge.dev/products/donation">Donate</a>
		</nav>
	</div>
</header>
<main>
"""

FOOT = """</main>
<footer class="site-foot">
	<div class="wrap foot-row">
		<p>DM32Mayhem is MIT-licensed, inspired by HackRF PortaPack Mayhem. <a href="https://github.com/SarahRoseLives/DM32Mayhem">Source on GitHub</a> · <a href="https://sarahsforge.dev/products/donation">Donate</a>.</p>
	</div>
</footer>
</body>
</html>
"""


def esc(s):
    return html.escape(s, quote=False)


def shots_html(shots):
    out = []
    for f, cap in shots:
        out.append(
            '<figure class="shot"><img src="/img/screens/%s" alt="%s" width="240" height="320" loading="lazy">'
            '<figcaption>%s</figcaption></figure>' % (f, esc(cap), esc(cap))
        )
    return "\n".join(out)


def page(app):
    what = "\n".join("<p>%s</p>" % p for p in app["what"])
    steps = "\n".join("<li>%s</li>" % s for s in app["steps"])
    fields = "\n".join('<div><dt>%s</dt><dd>%s</dd></div>' % (esc(k), esc(v)) for k, v in app["fields"])
    note = ""
    if app.get("note"):
        note = '<p class="notice info">%s</p>' % app["note"]
    body = """<section class="wrap app-head">
	<p class="eyebrow">%s · %s</p>
	<h1>%s</h1>
	<p class="lead">%s</p>
	<div class="actions">
		<a class="btn" href="/apps/">← All apps</a>
		<a class="btn btn-primary" href="/flasher/">Flash your radio →</a>
	</div>
</section>
<section class="wrap app-body">
	<div class="app-text">
		<h2>What it does</h2>
		%s
		<h2>How to use it</h2>
		<ol class="steps">
%s
		</ol>
		<h2>Screen fields</h2>
		<dl class="fields">
%s
		</dl>
		%s
	</div>
	<aside class="app-shots">
%s
	</aside>
</section>
""" % (GROUP_LABEL[app["group"]], esc(app["menu"]), esc(app["name"]), esc(app["summary"]), what, steps, fields,
       note, shots_html(app["shots"]))
    title = app["name"]
    h = HEAD.format(title=esc(title), desc=esc(app["summary"]))
    return h + body + FOOT


def index():
    sections = []
    for gid, gname, glead in GROUPS:
        cards = []
        for app in APPS:
            if app["group"] != gid:
                continue
            cards.append(
                '<a class="feature-card app-card" href="/apps/%s.html">'
                '<p class="kicker">%s</p><h3>%s</h3><p>%s</p><p class="more">How to use it →</p></a>'
                % (app["id"], esc(app["menu"]), esc(app["name"]), esc(app["summary"]))
            )
        extra = ""
        if gid == "rx":
            extra = '<p class="lead">RTTY listens on this menu too; its page covers both transmit and receive.</p>'
        sections.append(
            '<section class="wrap section"><div class="section-head"><h2>%s</h2></div>'
            '<p class="lead">%s</p>%s<div class="feature-grid">\n%s\n</div></section>'
            % (gname, glead, extra, "\n".join(cards))
        )
    exp = """<section class="wrap section">
	<div class="section-head"><h2>Experimental (EXP)</h2></div>
	<div class="notice info"><p><strong>The EXP menu holds experimental apps</strong> — ACARS, POCSAG and SSTV today.
	They are in the tree and they may change, or not decode yet. They are not documented here on purpose.</p></div>
	<div class="shot-grid" style="margin-top: var(--space-4)">
		<figure class="shot"><img src="/img/screens/exp-menu.png" alt="The EXP menu" width="240" height="320" loading="lazy"><figcaption>The EXP menu, where the experimental apps live.</figcaption></figure>
	</div>
</section>"""
    body = """<section class="wrap app-head">
	<p class="eyebrow">Apps</p>
	<h1>What's in the radio</h1>
	<p class="lead">DM32Mayhem puts a set of small, single-purpose apps in one tile grid. Pick one, set its rows, and it keys, tunes and unkeys the radio for you. Here is each one, what it does and how to use it.</p>
	<div class="actions"><a class="btn btn-primary" href="/flasher/">Flash your radio →</a><a class="btn" href="/#features">What it does</a></div>
</section>
%s
%s
""" % ("\n".join(sections), exp)
    h = HEAD.format(title="Apps", desc="Every app in DM32Mayhem: what it does and how to use it.")
    return h + body + FOOT


def main():
    os.makedirs(OUT, exist_ok=True)
    for app in APPS:
        with open(os.path.join(OUT, app["id"] + ".html"), "w") as f:
            f.write(page(app))
    with open(os.path.join(OUT, "index.html"), "w") as f:
        f.write(index())
    print("wrote %d app pages + index to %s" % (len(APPS), OUT))


if __name__ == "__main__":
    main()
