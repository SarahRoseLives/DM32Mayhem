# DM32Mayhem TODO

## Planned

- [ ] **Spectrum analyzer / waterfall** (RX): sweep 136–174 / 400–480 MHz (and 108–136 AM) using the FD6818 RSSI, draw a waterfall on the 240×320 display, add a peak list, tap-to-tune, and signal ID (DMR / CTCSS-DCS / APRS / ACARS / analog). Built on the existing scan/RSSI core; main work is sweep latency and RSSI calibration.

## Ideas considered

- SSTV TX/RX (slow-scan test pattern over FM) — the radio-native "TV test pattern".
- RF coverage/site-survey logger with GPS (RSSI vs position, GPX/CSV export, heatmap).
- Off-grid comms hub: Winlink packet / APRS iGate / digipeater / BBS / DTN.
- Sub-GHz OOK capture/replay (315/433/900 MHz) — needs an RX sensitivity check first.
- GPS-disciplined time/frequency reference over the serial link.
