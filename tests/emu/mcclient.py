"""Small Minecraft 1.21.8 (protocol 772) client used by the emulator tests.

It joins the server like a real client, keeps every packet it receives, can
dig and place blocks, and decodes Chunk Data packets so a test can read the
block at any position the server has sent.
"""
import socket
import struct
import time


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def mcstring(s):
    b = s.encode()
    return varint(len(b)) + b


def packet(pid, body=b""):
    inner = varint(pid) + body
    return varint(len(inner)) + inner


def pack_pos(x, y, z):
    return struct.pack(">Q", ((x & 0x3FFFFFF) << 38) | ((z & 0x3FFFFFF) << 12) | (y & 0xFFF))


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
        if len(v) != n:
            raise ValueError("truncated")
        self.p += n
        return v


def decode_chunk(body):
    """Returns (cx, cz, {section_index: list of 4096 state ids})."""
    r = Reader(body)
    cx, cz = struct.unpack(">ii", r.take(8))
    r.var()  # heightmaps
    r.var()  # chunk data size
    sections = {}
    for s in range(32):
        r.take(2)
        bits = r.u8()
        if bits == 0:
            sections[s] = [r.var()] * 4096
        else:
            palette = None
            if bits != 15:
                palette = [r.var() for _ in range(r.var())]
            per = 64 // bits
            nlongs = -(-4096 // per)
            raw = r.take(8 * nlongs)
            longs = struct.unpack(">%dQ" % nlongs, raw)
            mask = (1 << bits) - 1
            states = []
            for i in range(4096):
                v = (longs[i // per] >> ((i % per) * bits)) & mask
                states.append(palette[v] if palette is not None else v)
            sections[s] = states
        r.u8()   # biome bits
        r.var()  # biome value
    return cx, cz, sections


class Client:
    def __init__(self, name="tester", host="127.0.0.1", port=25565, uuid=None):
        self.name = name
        self.host, self.port = host, port
        self.uuid = uuid or (name.encode() + bytes(16))[:16]
        self.sock = None
        self.buf = b""
        self.packets = []      # every (id, body) received, in order
        self.chunks = {}       # (cx, cz) -> {section: states}
        self.block_updates = {}  # (x, y, z) -> state (latest Block Update)
        self.sequence = 0
        self.world_age = None   # server uptime in game ticks (20 per second), from Update Time

    # --- connection ---
    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=10)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def send(self, pid, body=b""):
        self.sock.sendall(packet(pid, body))

    def pump(self, seconds, quiet=1.5):
        """Read packets for up to `seconds`, stopping early after `quiet` s of silence."""
        end = time.time() + seconds
        last = time.time()
        self.sock.settimeout(0.2)
        got = []
        while time.time() < end:
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                if time.time() - last > quiet:
                    break
                continue
            if not data:
                raise ConnectionError("server closed the connection")
            last = time.time()
            self.buf += data
        pkts, self.buf = self._split(self.buf)
        for pid, body in pkts:
            self._note(pid, body)
        got.extend(pkts)
        self.packets.extend(pkts)
        return pkts

    @staticmethod
    def _split(buf):
        out, p = [], 0
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

    def _note(self, pid, body):
        if pid == 0x27:  # Chunk Data and Update Light
            cx, cz, sections = decode_chunk(body)
            self.chunks[(cx, cz)] = sections
        elif pid == 0x6A:  # Update Time: world age is the server's own clock
            self.world_age = struct.unpack(">Q", body[:8])[0]
        elif pid == 0x08:  # Block Update
            r = Reader(body)
            v = struct.unpack(">q", r.take(8))[0]
            x = v >> 38
            z = (v >> 12) & 0x3FFFFFF
            z -= (1 << 26) if z >= (1 << 25) else 0
            y = v & 0xFFF
            y -= (1 << 12) if y >= (1 << 11) else 0
            self.block_updates[(x, y, z)] = r.var()

    def join(self):
        self.connect()
        self.send(0x00, varint(772) + mcstring("localhost") + struct.pack(">H", self.port) + varint(2))
        self.send(0x00, mcstring(self.name) + self.uuid)
        got = self.pump(10)
        assert any(pid == 0x02 for pid, _ in got), "no login success"
        self.send(0x03)
        self.send(0x00, mcstring("en_US") + bytes([8]) + varint(0) + bytes([1]) + bytes([127]) + varint(1) + bytes([0]) + bytes([1]) + varint(0))
        self.pump(5)
        self.send(0x07, varint(0))
        got = self.pump(10)
        assert any(pid == 0x03 for pid, _ in got), "no finish configuration"
        self.send(0x03)
        self.pump(20)
        assert self.chunks, "no chunks after join"
        self.send(0x2B)  # Player Loaded: the server now treats us as in game
        self.pump(5)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

    # --- world access ---
    def block_at(self, x, y, z):
        if (x, y, z) in self.block_updates:
            return self.block_updates[(x, y, z)]
        ch = self.chunks.get((x >> 4, z >> 4))
        if ch is None:
            return None
        # the world starts at y=-64, so section 0 covers y=-64..-49
        sec = ch.get((y + 64) >> 4)
        if sec is None:
            return None
        return sec[((y & 15) << 8) | ((z & 15) << 4) | (x & 15)]

    def surface(self, x, z):
        """Highest block that is not air at (x, z), from received chunks."""
        for y in range(190, -1, -1):
            b = self.block_at(x, y, z)
            if b is None:
                return None
            if b != 0:
                return y
        return None

    # --- actions ---
    def move(self, x, y, z):
        self.send(0x1D, struct.pack(">ddd", x, y, z) + bytes([1]))

    def dig(self, x, y, z):
        """Start and finish digging a block (the server does not time digging)."""
        self.sequence += 1
        self.send(0x28, bytes([0]) + pack_pos(x, y, z) + bytes([1]) + varint(self.sequence))
        self.sequence += 1
        self.send(0x28, bytes([2]) + pack_pos(x, y, z) + bytes([1]) + varint(self.sequence))

    def place(self, x, y, z, face=1):
        """Right-click the given block face with the held item."""
        self.sequence += 1
        self.send(0x3F, bytes([0]) + pack_pos(x, y, z) + bytes([face]) + struct.pack(">fff", 0.5, 1.0, 0.5) + bytes([0, 0]) + varint(self.sequence))

    def select_slot(self, slot):
        """Select a hotbar slot (0-8)."""
        self.send(0x34, struct.pack(">h", slot))
        self.pump(1, quiet=0.5)

    def chat(self, text):
        # Chat Message: message, timestamp, salt, no signature, message count, acknowledged
        self.send(0x08, mcstring(text) + struct.pack(">q", int(time.time() * 1000)) + struct.pack(">q", 0) + bytes([0]) + varint(0) + bytes([0, 0, 0]))

    def wait_server_seconds(self, seconds, wall_timeout=600):
        """Waits until the SERVER's own clock has advanced `seconds` (the emulator may run
        slower than real time, so wall-clock sleeps would be misleading)."""
        start_wall = time.time()
        while self.world_age is None and time.time() - start_wall < 30:
            self.pump(2, quiet=5)
        start_age = self.world_age
        while time.time() - start_wall < wall_timeout:
            self.pump(2, quiet=5)
            if self.world_age - start_age >= seconds * 20:
                return (self.world_age - start_age) / 20.0, time.time() - start_wall
        raise TimeoutError("server clock did not advance %d s within %d s of wall time" % (seconds, wall_timeout))
