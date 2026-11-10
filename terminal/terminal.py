#!/usr/bin/env python3
"""KISS TCP TNC terminal client.

Connects to a KISS-over-TCP TNC, runs an AX.25 connected-mode session to a
BBS/node, and gives you an interactive terminal.

DM32BBS defaults: the client station is N0CALL-1, the onboard BBS is N0CALL on
145.100 MHz (25 kHz).  The radios own the frequency; this client only speaks
KISS/AX.25 to a TNC over TCP (the VR-N76 at 192.168.1.85:8001).

Examples
--------
Transmit a valid KISS/AX.25 UI frame and watch for replies (link test):
    python3 terminal.py --test

Open an interactive connected session with the DM32BBS node:
    python3 terminal.py

Quit an interactive session with Ctrl-] (sends DISC).
"""

from __future__ import annotations

import argparse
import os
import queue
import select
import sys
import termios
import threading
import time
import tty

import ax25
import kiss

T1_DEFAULT = 4.0   # retransmit timeout (seconds)
N2_DEFAULT = 10    # max retries
K_DEFAULT = 4      # window size (3-bit sequence numbers -> max 7)

DISCONNECTED = "disconnected"
AWAIT_CONN = "awaiting-connection"
CONNECTED = "connected"
AWAIT_RELEASE = "awaiting-release"


class Link:
    """Owns the TCP socket and the AX.25 connected-mode state machine."""

    def __init__(self, host: str, port: int, mycall: str, node: str,
                 *, t1: float = T1_DEFAULT, n2: int = N2_DEFAULT,
                 k: int = K_DEFAULT, events: queue.Queue | None = None):
        self.host = host
        self.port = port
        self.mycall = mycall.upper()
        self.node = node.upper()
        self.t1 = t1
        self.n2 = n2
        self.k = k
        self.events = events or queue.Queue()

        self.sock: kiss.socket.socket | None = None
        self.decoder = kiss.KissDecoder()
        self._out: queue.Queue[bytes] = queue.Queue()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

        self.state = DISCONNECTED
        self.vs = self.va = self.vr = 0
        self.unacked: list[tuple[int, bytes]] = []
        self.retries = 0
        self.t1_deadline: float | None = None
        self._sabm_frame: bytes | None = None
        self._disc_frame: bytes | None = None

    # -- lifecycle ------------------------------------------------------
    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="ax25-link", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2)

    def _run(self) -> None:
        self._emit("status", f"connecting to {self.host}:{self.port} ...")
        try:
            self.sock = kiss.socket.create_connection((self.host, self.port), timeout=5)
        except OSError as exc:
            self._emit("status", f"connect failed: {exc}")
            self._emit("fatal", "")
            return

        self._emit("status", f"TNC connected - connecting to {self.node}")
        self.sock.settimeout(0.2)
        # A short UA needs a long preamble for the far TNC's demodulator to lock; ask the TNC for 600 ms
        self._out.put(kiss.encode_command(kiss.CMD_TXDELAY, 60))
        self._out.put(kiss.encode_command(kiss.CMD_PERSISTENCE, 63))
        self._out.put(kiss.encode_command(kiss.CMD_SLOTTIME, 10))
        self._begin_connect()

        try:
            while not self._stop.is_set():
                self._flush_out()
                frames = self._read_frames()
                for command, payload in frames:
                    if command == kiss.CMD_DATA:
                        self._on_ax25(payload)
                    else:
                        self._emit("status", f"KISS cmd 0x{command:02x}: {payload.hex()}")
                self._check_timer()
        except (ConnectionError, OSError) as exc:
            self._emit("status", f"link error: {exc}")
        finally:
            self._set_state(DISCONNECTED)
            try:
                self.sock.close()
            except OSError:
                pass
            self._emit("closed", "")

    # -- outgoing -------------------------------------------------------
    def send_info(self, data: bytes) -> bool:
        if self.state != CONNECTED:
            self._emit("status", "not connected - line not sent")
            return False
        if (self.vs - self.va) & 0x07 >= self.k:
            self._emit("status", "window full - line dropped")
            return False
        control = ax25.i_control(self.vs, self.vr)
        frame = ax25.build_frame(self.node, self.mycall, control,
                                 pid=ax25.PID_NO_L3, info=data, command=True)
        self.unacked.append((self.vs, frame))
        self._out.put(kiss.encode_frame(frame))
        self.vs = (self.vs + 1) & 0x07
        self._arm_t1()
        return True

    def disconnect(self) -> None:
        if self.state in (CONNECTED, AWAIT_CONN):
            self._start_disconnect()

    def _raw_send(self, frame: bytes) -> None:
        self._out.put(kiss.encode_frame(frame))

    def _flush_out(self) -> None:
        if self.sock is None:
            return
        while True:
            try:
                raw = self._out.get_nowait()
            except queue.Empty:
                return
            try:
                self.sock.sendall(raw)
            except OSError as exc:
                self._emit("status", f"send failed: {exc}")
                return

    def _read_frames(self) -> list[tuple[int, bytes]]:
        try:
            data = self.sock.recv(4096)
        except TimeoutError:
            return []
        if not data:
            raise ConnectionError("TNC closed the connection")
        return self.decoder.feed(data)

    # -- connection setup ----------------------------------------------
    def _begin_connect(self) -> None:
        self._sabm_frame = ax25.build_frame(
            self.node, self.mycall, ax25.SABM | ax25.PF, command=True)
        self._set_state(AWAIT_CONN)
        self.retries = 0
        self._send_and_arm(self._sabm_frame)

    def _start_disconnect(self) -> None:
        self._disc_frame = ax25.build_frame(
            self.node, self.mycall, ax25.DISC | ax25.PF, command=True)
        self._set_state(AWAIT_RELEASE)
        self.retries = 0
        self._send_and_arm(self._disc_frame)

    def _send_and_arm(self, frame: bytes) -> None:
        self._raw_send(frame)
        self.t1_deadline = time.monotonic() + self.t1

    def _arm_t1(self) -> None:
        if self.t1_deadline is None:
            self.t1_deadline = time.monotonic() + self.t1

    def _disarm_t1(self) -> None:
        self.t1_deadline = None

    def _check_timer(self) -> None:
        if self.t1_deadline is None or time.monotonic() < self.t1_deadline:
            return
        if self.state == AWAIT_CONN and self._sabm_frame:
            self.retries += 1
            if self.retries > self.n2:
                self._emit("status", f"no answer from {self.node} (timeout)")
                self._set_state(DISCONNECTED)
                return
            self._emit("status", f"retransmitting SABM ({self.retries}/{self.n2})")
            self._send_and_arm(self._sabm_frame)
        elif self.state == AWAIT_RELEASE and self._disc_frame:
            self.retries += 1
            if self.retries > self.n2:
                self._set_state(DISCONNECTED)
                return
            self._send_and_arm(self._disc_frame)
        elif self.state == CONNECTED:
            if not self.unacked:
                self._disarm_t1()
                return
            self.retries += 1
            if self.retries > self.n2:
                self._emit("status", "too many retries - link reset")
                self._set_state(DISCONNECTED)
                return
            for _, frame in self.unacked:
                self._raw_send(frame)
            self.t1_deadline = time.monotonic() + self.t1
        else:
            self._disarm_t1()

    # -- incoming -------------------------------------------------------
    def _on_ax25(self, data: bytes) -> None:
        try:
            frame = ax25.parse_frame(data)
        except ValueError as exc:
            self._emit("status", f"dropped bad AX.25 frame: {exc}")
            return

        if ax25.is_i_frame(frame.control):
            self._on_i(frame)
        elif ax25.is_s_frame(frame.control):
            self._on_s(frame)
        elif ax25.is_u_frame(frame.control):
            self._on_u(frame)
        else:
            self._emit("status", f"unknown frame ctrl=0x{frame.control:02x}")

    def _ack(self, nr: int) -> None:
        while self.va != nr and self.unacked:
            self.unacked.pop(0)
            self.va = (self.va + 1) & 0x07
        if not self.unacked:
            self._disarm_t1()

    def _on_i(self, frame: ax25.Frame) -> None:
        # Data proves the peer accepted the connection. A TNC can lose the small UA frame while the following
        # I frames get through, so adopt the link from the first I frame instead of giving up until N2
        if self.state == AWAIT_CONN:
            self.vs = self.va = self.vr = 0
            self.unacked.clear()
            self._disarm_t1()
            self._set_state(CONNECTED)
            self._emit("status", "connected (UA not seen; data arrived)")
        ns = ax25.i_ns(frame.control)
        nr = ax25.i_nr(frame.control)
        poll = ax25.pf(frame.control)
        self._ack(nr)
        if ns == self.vr:
            if frame.info:
                self._emit("rx", frame.info)
            self.vr = (self.vr + 1) & 0x07
            self._raw_send(ax25.build_frame(
                self.node, self.mycall, ax25.s_control(ax25.RR, self.vr, poll),
                command=False))
        else:
            self._raw_send(ax25.build_frame(
                self.node, self.mycall, ax25.s_control(ax25.REJ, self.vr, poll),
                command=False))

    def _on_s(self, frame: ax25.Frame) -> None:
        nr = ax25.s_nr(frame.control)
        poll = ax25.pf(frame.control)
        kind = frame.control & 0x0F
        self._ack(nr)
        if kind == ax25.REJ:
            # Selective reject: resend from the sequence the peer asks for
            self.va = nr
            for _, f in list(self.unacked):
                self._raw_send(f)
            if self.unacked:
                self._arm_t1()
        if poll:
            self._raw_send(ax25.build_frame(
                self.node, self.mycall, ax25.s_control(ax25.RR, self.vr, 1),
                command=False))

    def _on_u(self, frame: ax25.Frame) -> None:
        kind = ax25.u_type(frame.control)
        if kind == ax25.SABM:
            self._raw_send(ax25.build_frame(
                self.node, self.mycall, ax25.UA | ax25.PF, command=False))
            self.vs = self.va = self.vr = 0
            self.unacked.clear()
            self._set_state(CONNECTED)
        elif kind == ax25.DISC:
            self._raw_send(ax25.build_frame(
                self.node, self.mycall, ax25.UA | ax25.PF, command=False))
            self._set_state(DISCONNECTED)
        elif kind == ax25.UA:
            if self.state == AWAIT_CONN:
                self.vs = self.va = self.vr = 0
                self.unacked.clear()
                self._set_state(CONNECTED)
            elif self.state == AWAIT_RELEASE:
                self._set_state(DISCONNECTED)
            self._disarm_t1()
        elif kind == ax25.DM:
            if self.state == AWAIT_CONN:
                self._emit("status", f"{self.node} refused the connection")
            self._set_state(DISCONNECTED)
        elif kind == ax25.UI:
            if frame.info:
                self._emit("rx", frame.info)
        else:
            self._emit("status", f"U frame 0x{frame.control:02x}")

    # -- helpers --------------------------------------------------------
    def _set_state(self, state: str) -> None:
        if state == self.state:
            return
        self.state = state
        self._emit("state", state)

    def _emit(self, kind: str, payload) -> None:
        self.events.put((kind, payload))


# --- interactive terminal ---------------------------------------------------

def run_terminal(args: argparse.Namespace) -> int:
    events: queue.Queue = queue.Queue()
    link = Link(args.host, args.port, args.mycall, args.node,
                t1=args.t1, n2=args.n2, events=events)
    print(f"DM32BBS  {args.mycall} -> {args.node}  {args.freq} MHz  via {args.host}:{args.port}")
    print("Ctrl-] disconnects and exits")
    link.start()

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    line = bytearray()
    prompt = b""
    try:
        tty.setraw(fd)
        while True:
            r, _, _ = select.select([fd], [], [], 0.1)
            if r:
                data = os.read(fd, 1024)
                if not data:
                    break
                for byte in data:
                    if byte == 0x1D:  # Ctrl-]
                        link.disconnect()
                        time.sleep(0.5)
                        return 0
                    if byte in (0x0D, 0x0A):
                        if line:
                            if link.send_info(bytes(line) + b"\r"):
                                pass
                            line.clear()
                        sys.stdout.write("\r\n")
                        sys.stdout.flush()
                    elif byte in (0x7F, 0x08):
                        if line:
                            line.pop()
                            sys.stdout.write(b"\b \b".decode())
                            sys.stdout.flush()
                    elif byte == 0x15:  # Ctrl-U
                        while line:
                            line.pop()
                            sys.stdout.write(b"\b \b".decode())
                        sys.stdout.flush()
                    elif 0x20 <= byte < 0x7F:
                        line.append(byte)
                        sys.stdout.write(chr(byte))
                        sys.stdout.flush()

            _drain_events(events, line)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        link.stop()
    return 0


def _drain_events(events: queue.Queue, line: bytearray) -> None:
    redraw = False
    while True:
        try:
            kind, payload = events.get_nowait()
        except queue.Empty:
            break
        if kind == "rx":
            text = bytes(payload).decode("latin-1").replace("\r\n", "\n").replace("\r", "\n")
            sys.stdout.write("\r\033[K")
            sys.stdout.write(text)
            if not text.endswith("\n"):
                sys.stdout.write("\n")
            redraw = True
        elif kind == "state":
            sys.stdout.write(f"\r\033[K[link: {payload}]\n")
            redraw = True
        elif kind == "status":
            sys.stdout.write(f"\r\033[K[{payload}]\n")
            redraw = True
        elif kind == "fatal":
            sys.stdout.write("\r\033[K")
            sys.stdout.flush()
            raise SystemExit(1)
    if redraw:
        sys.stdout.write("\r\033[K" + bytes(line).decode("latin-1"))
        sys.stdout.flush()


# --- link test --------------------------------------------------------------

def run_test(args: argparse.Namespace) -> int:
    info = args.text.encode("latin-1")
    frame = ax25.build_frame(
        args.node, args.mycall, ax25.UI, pid=ax25.PID_NO_L3, info=info, command=True)

    print(f"AX.25 frame  ({len(frame)} bytes): {frame.hex()}")
    print(f"KISS  frame  ({len(kiss.encode_frame(frame))} bytes): {kiss.encode_frame(frame).hex()}")

    client = kiss.KissClient(args.host, args.port, timeout=args.timeout)
    for attempt in range(1, 4):
        try:
            client.connect()
            break
        except OSError as exc:
            print(f"connect attempt {attempt} failed: {exc}")
            time.sleep(1)
    else:
        print("could not reach the TNC")
        return 1

    print(f"connected to {args.host}:{args.port}, transmitting ...")
    client.send_frame(frame)

    deadline = time.monotonic() + args.timeout
    got = False
    while time.monotonic() < deadline:
        for command, payload in client.recv_frames(timeout=min(0.5, args.timeout)):
            got = True
            if command == kiss.CMD_DATA:
                try:
                    rx = ax25.parse_frame(payload)
                    print(f"RX AX.25: {rx}")
                except ValueError:
                    print(f"RX raw:   {payload.hex()}")
            else:
                print(f"RX KISS cmd 0x{command:02x}: {payload.hex()}")
    client.close()
    print("done (no reply frames)" if not got else "done")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="KISS TCP TNC terminal client for the DM32BBS node")
    parser.add_argument("--host", default="192.168.1.85", help="TNC host (default 192.168.1.85, the VR-N76)")
    parser.add_argument("--port", type=int, default=8001, help="KISS TCP port (default 8001)")
    parser.add_argument("--mycall", default="N0CALL-1", help="your AX.25 callsign (default N0CALL-1)")
    parser.add_argument("--node", "--bbs", dest="node", default="N0CALL",
                        help="destination BBS/node callsign (default N0CALL)")
    parser.add_argument("--freq", default="145.100", help="BBS frequency in MHz, informational (default 145.100)")
    parser.add_argument("--t1", type=float, default=T1_DEFAULT, help="retransmit timeout seconds")
    parser.add_argument("--n2", type=int, default=N2_DEFAULT, help="max retransmit attempts")
    parser.add_argument("--timeout", type=float, default=6.0, help="test-mode wait seconds")
    parser.add_argument("--text", default="KISS TCP terminal link test", help="test-mode payload")
    parser.add_argument("--test", action="store_true",
                        help="transmit one valid KISS/AX.25 UI frame and exit")
    args = parser.parse_args(argv)

    for label, call in (("--mycall", args.mycall), ("--node", args.node)):
        base = call.split("-", 1)[0]
        if not base or len(base) > ax25.AX25_CALL_LEN:
            parser.error(f"{label} {call!r}: AX.25 callsigns are at most "
                         f"{ax25.AX25_CALL_LEN} characters plus a numeric SSID")

    try:
        if args.test:
            return run_test(args)
        return run_terminal(args)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
