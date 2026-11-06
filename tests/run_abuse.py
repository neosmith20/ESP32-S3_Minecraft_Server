#!/usr/bin/env python3
"""Abuse test against the native server: stalled/hostile connections must not
freeze or crash it. usage: run_abuse.py /path/to/mcserver_native"""
import socket, subprocess, sys, time
from pathlib import Path

def varint(v):
    out = b""
    while True:
        if v & ~0x7F:
            out += bytes([(v & 0x7F) | 0x80]); v >>= 7
        else:
            return out + bytes([v])

def status_ping(timeout=3.0):
    """Handshake + status request; returns True if the server answers in time."""
    s = socket.create_connection(("127.0.0.1", 25565), timeout=timeout)
    try:
        hs = varint(0) + varint(772) + varint(9) + b"localhost" + b"\x63\xdd" + varint(1)
        s.sendall(varint(len(hs)) + hs)
        s.sendall(varint(1) + varint(0))
        data = s.recv(4096)
        return len(data) > 0
    except Exception:
        return False
    finally:
        s.close()

def main():
    binary = Path(sys.argv[1]).resolve()
    server = subprocess.Popen([str(binary)], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    fails = 0
    try:
        time.sleep(3)
        def check(cond, msg):
            nonlocal fails
            print(("ok:   " if cond else "FAIL: ") + msg)
            if not cond: fails += 1
        check(status_ping(), "server answers a status request")
        # half a packet: says 100 bytes follow, sends only 3, then goes quiet
        stall = socket.create_connection(("127.0.0.1", 25565))
        stall.sendall(varint(100) + b"\x00\x01")
        time.sleep(0.5)
        t = time.time()
        ok = status_ping(timeout=3.0)
        check(ok and time.time() - t < 2.5, "a stalled half-packet does not block other clients")
        # a second kind: only the first byte of a two-byte length
        stall2 = socket.create_connection(("127.0.0.1", 25565))
        stall2.sendall(b"\xff")
        time.sleep(0.3)
        check(status_ping(timeout=3.0), "an unfinished length prefix does not block other clients")
        # garbage / hostile lengths
        for payload in (b"\xff\xff\xff\xff\xff\xff\x01", varint(2000000) + b"\x00", varint(0), b"\xfe\x01\xfa" + b"\x00" * 20):
            g = socket.create_connection(("127.0.0.1", 25565))
            g.sendall(payload); time.sleep(0.2); g.close()
        check(status_ping(), "server still answers after garbage packets")
        # handshake claiming play state
        h = socket.create_connection(("127.0.0.1", 25565))
        hs = varint(0) + varint(772) + varint(9) + b"localhost" + b"\x63\xdd" + varint(5)
        h.sendall(varint(len(hs)) + hs)
        h.sendall(varint(2) + varint(0x08) + b"\x00")
        time.sleep(0.3); h.close()
        check(server.poll() is None and status_ping(), "server survives a fake play-state handshake")
        # after the partial-packet timeout the stalled connection is dropped
        time.sleep(6)
        stall.settimeout(2)
        try:
            dropped = stall.recv(10) == b""
        except socket.timeout:
            dropped = False
        except Exception:
            dropped = True
        check(dropped, "the stalled connection is dropped after the timeout")
        check(server.poll() is None, "server is still running")
    finally:
        if server.poll() is None: server.terminate()
        try: server.wait(5)
        except Exception: server.kill()
    print("\n" + ("SOME TESTS FAILED" if fails else "ALL ABUSE TESTS PASSED"))
    return 1 if fails else 0

sys.exit(main())
