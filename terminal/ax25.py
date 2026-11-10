"""Minimal AX.25 v2.0 frame codec used by the KISS TCP terminal client.

Only what is needed to run a connected-mode session (I/S/U frames) over a
KISS TNC.  No digipeater routing, no layer-3 routing, 3-bit sequence numbers.
"""

from __future__ import annotations

from dataclasses import dataclass, field

AX25_CALL_LEN = 6  # callsign characters; the SSID is separate (0..15)

# Control field values (P/F bit cleared).
SABM = 0x2F
DISC = 0x43
DM = 0x0F
UA = 0x63
FRMR = 0x87
UI = 0x03
RR = 0x01
RNR = 0x05
REJ = 0x09

PF = 0x10  # Poll/Final bit in the control field.
PID_NO_L3 = 0xF0


def crc16_x25(data: bytes) -> int:
    """CRC-16/X.25 (poly 0x1021 reflected -> 0x8408, init/final 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
    return crc ^ 0xFFFF


def _split_call(call: str) -> tuple[str, int]:
    call = call.strip().upper()
    if "-" in call:
        base, _, ssid = call.partition("-")
        return base[:AX25_CALL_LEN], int(ssid or 0) & 0x0F
    return call[:AX25_CALL_LEN], 0


def encode_address(call: str, ssid: int, *, last: bool, command: bool) -> bytes:
    base, call_ssid = _split_call(call)
    if ssid is not None:
        call_ssid = ssid & 0x0F
    base = base.ljust(6)
    out = bytearray(ord(ch) << 1 for ch in base)
    ssid_byte = 0x60 | ((call_ssid & 0x0F) << 1)
    if command:
        ssid_byte |= 0x80
    if last:
        ssid_byte |= 0x01
    out.append(ssid_byte)
    return bytes(out)


def decode_address(block: bytes) -> tuple[str, int]:
    call = "".join(chr(b >> 1) for b in block[:6]).strip()
    ssid = (block[6] >> 1) & 0x0F
    return call, ssid


@dataclass
class Frame:
    dest: str
    src: str
    control: int
    pid: int | None = None
    info: bytes = b""
    digis: list[str] = field(default_factory=list)

    def __str__(self) -> str:
        src = self.src
        dst = self.dest
        if self.pid is not None:
            return f"{src}>{dst} I pid=0x{self.pid:02x} {self.info!r}"
        return f"{src}>{dst} ctrl=0x{self.control:02x}"


def build_frame(
    dest: str,
    src: str,
    control: int,
    *,
    pid: int | None = None,
    info: bytes = b"",
    digis: list[str] | None = None,
    command: bool = True,
    fcs: bool = False,
) -> bytes:
    """Assemble an AX.25 frame. The default has no FCS: that is what a KISS TNC wants (it adds the FCS on the air)."""
    digis = digis or []
    addresses = bytearray()
    addresses += encode_address(dest, None, last=False, command=command)
    addresses += encode_address(src, None, last=not digis, command=not command)
    for i, digi in enumerate(digis):
        addresses += encode_address(digi, None, last=(i == len(digis) - 1), command=False)

    body = bytearray(addresses)
    body.append(control)
    if pid is not None:
        body.append(pid)
    body += info

    if fcs:
        crc = crc16_x25(bytes(body))
        body += bytes((crc & 0xFF, (crc >> 8) & 0xFF))
    return bytes(body)


def parse_frame(data: bytes, fcs: bool = False) -> Frame:
    """Parse an AX.25 frame from KISS (no FCS by default). With fcs=True, check and drop the trailing FCS."""
    if fcs:
        if len(data) < 2 + 7 + 7 + 1:
            raise ValueError("frame too short")
        body = data[:-2]
        got = data[-2] | (data[-1] << 8)
        if crc16_x25(body) != got:
            raise ValueError("bad FCS")
    else:
        if len(data) < 7 + 7 + 1:
            raise ValueError("frame too short")
        body = data

    addresses = []
    i = 0
    while True:
        if i + 7 > len(body):
            raise ValueError("truncated address field")
        addresses.append(decode_address(body[i : i + 7]))
        last = body[i + 7 - 1] & 0x01
        i += 7
        if last:
            break

    if len(addresses) < 2 or i >= len(body):
        raise ValueError("missing source address or control field")

    dest_call, dest_ssid = addresses[0]
    src_call, src_ssid = addresses[1]
    digis = [f"{c}-{s}" if s else c for c, s in addresses[2:]]
    dest = f"{dest_call}-{dest_ssid}" if dest_ssid else dest_call
    src = f"{src_call}-{src_ssid}" if src_ssid else src_call

    control = body[i]
    i += 1

    # I-frames (bit 0 == 0) and UI frames carry a PID + info field.
    pid = None
    info = body[i:]
    if control & 0x01 == 0 or (control & 0xEF) == UI:
        if i < len(body):
            pid = body[i]
            info = body[i + 1 :]

    return Frame(dest=dest, src=src, control=control, pid=pid, info=info, digis=digis)


# --- Control field helpers -------------------------------------------------

def is_i_frame(control: int) -> bool:
    return control & 0x01 == 0


def is_s_frame(control: int) -> bool:
    return control & 0x03 == 0x01


def is_u_frame(control: int) -> bool:
    return control & 0x03 == 0x03


def u_type(control: int) -> int:
    return control & 0xEF  # ignore P/F bit


def i_ns(control: int) -> int:
    return (control >> 1) & 0x07


def i_nr(control: int) -> int:
    return (control >> 5) & 0x07


def s_nr(control: int) -> int:
    return (control >> 5) & 0x07


def pf(control: int) -> int:
    return (control >> 4) & 0x01


def i_control(ns: int, nr: int, p: int = 0) -> int:
    return ((nr & 0x07) << 5) | ((p & 0x01) << 4) | ((ns & 0x07) << 1)


def s_control(kind: int, nr: int, p: int = 0) -> int:
    return ((nr & 0x07) << 5) | ((p & 0x01) << 4) | (kind & 0x0F)
