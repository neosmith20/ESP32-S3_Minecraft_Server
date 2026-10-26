#!/usr/bin/env python3
"""Scripted Minecraft-protocol client for an end-to-end test of the PC server.

Joins like a real client (handshake, login, configuration, play), then reads
everything the server sends, checks that every Chunk Data packet is
well-formed with a correct declared length, and walks across a chunk border
to trigger more chunk sending.
"""
import socket
import struct
import sys
import time

HOST, PORT = "127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 25565


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def string(s):
    b = s.encode()
    return varint(len(b)) + b


def packet(pid, body=b""):
    inner = varint(pid) + body
    return varint(len(inner)) + inner


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def var(self):
        v = shift = 0
        while True:
            b = self.u8()
            v |= (b & 0x7F) << shift
            if not b & 0x80:
                return v
            shift += 7

    def take(self, n):
        v = self.d[self.p:self.p + n]
        assert len(v) == n, "truncated"
        self.p += n
        return v


def check_chunk(body):
    """body = packet payload after the packet id. Raises on a malformed chunk."""
    r = Reader(body)
    cx, cz = struct.unpack(">ii", r.take(8))
    assert r.var() == 0, "heightmaps"
    cds = r.var()
    start = r.p
    full = 0
    for s in range(32):
        r.take(2)
        bits = r.u8()
        if bits == 0:
            r.var()
        elif 4 <= bits <= 8 or bits == 15:
            if bits != 15:
                plen = r.var()
                assert 2 <= plen <= (1 << bits)
                for _ in range(plen):
                    r.var()
            r.take(8 * (-(-4096 // (64 // bits))))
            full += 1
        else:
            raise AssertionError(f"bad bits {bits}")
        assert r.u8() == 0
        r.var()
    assert r.p - start == cds, f"chunk data size {cds} vs {r.p - start}"
    assert r.var() == 0
    assert r.var() == 1
    r.take(8)
    assert (r.var(), r.var(), r.var()) == (0, 0, 0)
    assert r.var() == 26
    for _ in range(26):
        assert r.var() == 2048
        r.take(2048)
    assert r.var() == 0
    assert r.p == len(body), f"{len(body) - r.p} bytes left over inside packet"
    return cx, cz, full


def split_packets(buf):
    """Yield (id, body) for complete packets, return leftover bytes."""
    out = []
    p = 0
    while True:
        r = Reader(buf[p:])
        try:
            length = r.var()
        except IndexError:
            break
        hdr = r.p
        if len(buf) - p - hdr < length:
            break
        body = buf[p + hdr:p + hdr + length]
        rr = Reader(body)
        pid = rr.var()
        out.append((pid, body[rr.p:]))
        p += hdr + length
    return out, buf[p:]


def main():
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    buf = b""
    packets = []

    def pump(seconds):
        nonlocal buf
        end = time.time() + seconds
        last = time.time()
        s.settimeout(0.2)
        while time.time() < end:
            try:
                data = s.recv(65536)
            except socket.timeout:
                if time.time() - last > 1.5:
                    break
                continue
            if not data:
                break
            last = time.time()
            buf += data
        got, buf = split_packets(buf)
        packets.extend(got)
        return got

    # handshake -> login
    s.sendall(packet(0x00, varint(772) + string("localhost") + struct.pack(">H", PORT) + varint(2)))
    s.sendall(packet(0x00, string("tester") + bytes(range(16))))
    got = pump(3)
    assert any(pid == 0x02 for pid, _ in got), "no login success"
    print("login success received")

    # login acknowledged -> configuration
    s.sendall(packet(0x03))
    s.sendall(packet(0x00, string("en_US") + bytes([8]) + varint(0) + bytes([1]) + bytes([127]) + varint(1) + bytes([0]) + bytes([1]) + varint(0)))
    got = pump(3)
    print("configuration packets:", [hex(pid) for pid, _ in got])
    s.sendall(packet(0x07, varint(0)))  # known packs
    got = pump(3)
    assert any(pid == 0x03 for pid, _ in got), "no finish configuration"
    s.sendall(packet(0x03))  # acknowledge configuration -> play
    got = pump(8)

    chunks = [(pid, body) for pid, body in packets if pid == 0x27]
    print(f"after join: {len(packets)} packets total, {len(chunks)} chunk packets")
    ok = 0
    full_counts = []
    for pid, body in chunks:
        cx, cz, full = check_chunk(body)
        full_counts.append(full)
        ok += 1
    print(f"join chunks valid: {ok}/{len(chunks)}, full sections per chunk: {sorted(set(full_counts))}")
    assert chunks, "no chunks sent on join"

    # walk east across several chunk borders: position packet (0x1D)
    before = len(chunks)
    for step in range(1, 4):
        x = 8.5 + 16 * step
        y = 90.0
        z = 8.5
        s.sendall(packet(0x1D, struct.pack(">ddd", x, y, z) + bytes([0])))
        pump(2.5)
    chunks2 = [(pid, body) for pid, body in packets if pid == 0x27][before:]
    ok2 = 0
    for pid, body in chunks2:
        check_chunk(body)
        ok2 += 1
    print(f"walking chunks valid: {ok2}/{len(chunks2)}")
    assert chunks2, "no chunks sent while walking"

    # server still alive and answering?
    s.sendall(packet(0x1B, struct.pack(">q", 0)))
    s.close()
    print("E2E OK")


main()
