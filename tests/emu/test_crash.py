#!/usr/bin/env python3
"""Emulator test: power loss while a player is still online.

Blocks changed more than one journal interval (15 s) before the "power cut"
must still be there after the cut. The flash content at the moment of the cut
is copied out of the emulator, and the firmware is booted again from that copy.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from emulator import Emulator, fresh_image
from mcclient import Client


def grass_columns(c, count):
    found = []
    for (cx, cz) in sorted(c.chunks):
        for dx in range(2, 14, 3):
            for dz in range(2, 14, 3):
                t = c.surface(cx * 16 + dx, cz * 16 + dz)
                if t is not None and c.block_at(cx * 16 + dx, t, cz * 16 + dz) == 9:
                    found.append((cx * 16 + dx, t, cz * 16 + dz))
                    if len(found) == count:
                        return found
    return found


def main(image_src, elf, workdir):
    os.makedirs(workdir, exist_ok=True)
    image = fresh_image(image_src, os.path.join(workdir, "flash.bin"))
    snap = os.path.join(workdir, "snapshot.bin")
    if os.path.exists(snap):
        os.remove(snap)

    emu = Emulator(image, elf, port=25570)
    emu.start()
    assert emu.wait_for("Server listening", 60), "server did not start"
    c = Client("bob", port=25570)
    c.join()
    cols = grass_columns(c, 3)
    assert len(cols) == 3, "not enough grass columns"
    (ax, ay, az), (bx, by, bz), (px, py, pz) = cols

    # mine two grass blocks (dirt goes to the hotbar), then place dirt on a third column
    c.dig(ax, ay, az)
    c.dig(bx, by, bz)
    c.pump(3)
    assert c.block_at(ax, ay, az) == 0 and c.block_at(bx, by, bz) == 0, "digging failed"
    c.place(px, py, pz, face=1)
    c.pump(3)
    placed = c.block_at(px, py + 1, pz)
    print("placed block state above column:", placed)
    assert placed not in (None, 0), "placing a block failed"

    # wait past the journal interval while still connected, then cut the power
    print("waiting for the journal sync ...")
    t0 = emu.mark()
    waited = c.wait_server_seconds(20)
    print('server clock advanced %.1f s in %.1f s of real time' % waited)
    assert emu.alive(), "emulator died\n" + "\n".join(emu.crash_lines())
    emu.read_flash_region(0, 16 * 1024 * 1024, snap)
    emu.crash()
    c.close()
    assert os.path.exists(snap) and os.path.getsize(snap) == 16 * 1024 * 1024, "flash snapshot missing"

    emu2 = Emulator(snap, elf, port=25570)
    emu2.start()
    assert emu2.wait_for("Server listening", 60), "server did not restart after power cut\n" + emu2.text()[-3000:]
    c2 = Client("bob", port=25570)
    c2.join()
    ok_a = c2.block_at(ax, ay, az) == 0
    ok_b = c2.block_at(bx, by, bz) == 0
    # placed dirt next to grass may legitimately have turned into grass meanwhile
    ok_p = c2.block_at(px, py + 1, pz) in (placed, 9)
    print("after power cut: dug A", ok_a, "dug B", ok_b, "placed", ok_p)
    if os.environ.get("EMU_DEBUG"):
        print("--- second boot log ---")
        print("\n".join(l for l in emu2.lines if "[INFO" not in l and not l.startswith(("I (", "W (")))[-3500:])
    emu2.stop()
    assert emu.crash_lines() == [] and emu2.crash_lines() == [], "crash lines: " + str(emu2.crash_lines()[:3])
    assert ok_a and ok_b and ok_p, "block changes made before the journal sync were lost"
    print("CRASH RECOVERY OK")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
