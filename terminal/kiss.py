"""KISS protocol framing and a small synchronous TCP transport."""

from __future__ import annotations

import socket
from dataclasses import dataclass

FEND = 0xC0
FESC = 0xDB
TFEND = 0xDC
TFESC = 0xDD

CMD_DATA = 0x00
CMD_TXDELAY = 0x01
CMD_PERSISTENCE = 0x02
CMD_SLOTTIME = 0x03
CMD_TXTAIL = 0x04
CMD_FULLDUPLEX = 0x05
CMD_SETHARDWARE = 0x06
CMD_RETURN = 0xFF


def _escape(payload: bytes) -> bytes:
    out = bytearray()
    for byte in payload:
        if byte == FEND:
            out += bytes((FESC, TFEND))
        elif byte == FESC:
            out += bytes((FESC, TFESC))
        else:
            out.append(byte)
    return bytes(out)


def encode_frame(data: bytes = b"", *, port: int = 0, command: int = CMD_DATA) -> bytes:
    """Wrap AX.25 bytes (or a control command payload) in a KISS frame."""
    return bytes((FEND,)) + bytes(((port & 0x0F) << 4 | (command & 0x0F),)) + _escape(data) + bytes((FEND,))


def encode_command(command: int, value: int = 0, *, port: int = 0) -> bytes:
    """Encode a KISS parameter command (e.g. TXDELAY) with an optional value."""
    payload = bytes((value,)) if value else b""
    return encode_frame(payload, port=port, command=command)


class KissDecoder:
    """Incrementally turn a raw byte stream into (command, payload) frames."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, bytes]]:
        frames: list[tuple[int, bytes]] = []
        for byte in data:
            if byte == FEND:
                if self._buf:
                    frames.extend(self._finish(self._buf))
                    self._buf.clear()
                continue
            self._buf.append(byte)
        return frames

    @staticmethod
    def _finish(buf: bytearray) -> list[tuple[int, bytes]]:
        raw = bytearray()
        i = 0
        while i < len(buf):
            byte = buf[i]
            if byte == FESC and i + 1 < len(buf):
                i += 1
                nxt = buf[i]
                if nxt == TFEND:
                    raw.append(FEND)
                elif nxt == TFESC:
                    raw.append(FESC)
                else:
                    raw.append(nxt)
            else:
                raw.append(byte)
            i += 1
        if not raw:
            return []
        command = raw[0]
        return [(command, bytes(raw[1:]))]


@dataclass
class KissClient:
    host: str
    port: int
    timeout: float = 2.0

    _sock: socket.socket | None = None
    _decoder: KissDecoder | None = None

    def connect(self) -> None:
        self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._decoder = KissDecoder()

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    def send_frame(self, data: bytes, *, port: int = 0) -> None:
        self.send_raw(encode_frame(data, port=port))

    def send_command(self, command: int, value: int = 0, *, port: int = 0) -> None:
        self.send_raw(encode_command(command, value, port=port))

    def send_raw(self, raw: bytes) -> None:
        if self._sock is None:
            raise RuntimeError("not connected")
        self._sock.sendall(raw)

    def recv_frames(self, timeout: float | None = None) -> list[tuple[int, bytes]]:
        """Return frames decoded from whatever bytes arrive before the timeout."""
        if self._sock is None or self._decoder is None:
            raise RuntimeError("not connected")
        self._sock.settimeout(self.timeout if timeout is None else timeout)
        try:
            data = self._sock.recv(4096)
        except socket.timeout:
            return []
        if not data:
            raise ConnectionError("TNC closed the connection")
        return self._decoder.feed(data)
