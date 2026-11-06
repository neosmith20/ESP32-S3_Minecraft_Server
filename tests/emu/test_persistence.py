#!/usr/bin/env python3
"""Emulator test: blocks mined and placed by a client survive a restart."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from emulator import Emulator, fresh_image
from mcclient import Client


def main(image_src, elf, workdir):
    os.makedirs(workdir, exist_ok=True)
    image = fresh_image(image_src, os.path.join(workdir, "flash.bin"))

    # ---- first boot ----
    emu = Emulator(image, elf)
    emu.start()
    assert emu.wait_for("Server listening", 60), "server did not start\n" + emu.text()[-2000:]
    print("boot ok")
    c = Client("alice")
    c.join()
    print("joined, chunks:", len(c.chunks))
    # pick a column whose top block is plain grass (not a tree or water)
    x = z = top = None
    for (cx, cz) in sorted(c.chunks):
        for dx in range(2, 14):
            for dz in range(2, 14):
                t = c.surface(cx * 16 + dx, cz * 16 + dz)
                if t is not None and c.block_at(cx * 16 + dx, t, cz * 16 + dz) == 9:
                    x, z, top = cx * 16 + dx, cz * 16 + dz, t
                    break
            if top is not None:
                break
        if top is not None:
            break
    assert top is not None, "no grass column found"
    before_top = c.block_at(x, top, z)
    print("column", x, z, "surface y", top, "block", before_top)
    n0 = len(c.packets)
    c.dig(x, top, z)
    got = c.pump(4)
    after = c.block_at(x, top, z)
    assert after == 0, f"dig did not remove the block (got {after})"
    print("dig ok")
    time.sleep(1)
    c.close()
    time.sleep(3)  # disconnect flushes the journal
    assert emu.alive(), "emulator died\n" + emu.text()[-2000:]
    emu.stop()

    # ---- second boot, same flash ----
    emu2 = Emulator(image, elf)
    emu2.start()
    assert emu2.wait_for("Server listening", 60), "server did not restart\n" + emu2.text()[-2000:]
    log = emu2.text()
    c2 = Client("alice")
    c2.join()
    got = c2.block_at(x, top, z)
    print("after restart block at dug position:", got)
    assert got == 0, f"dug block came back after restart (state {got})"
    emu2.stop()
    bad = emu.crash_lines() + emu2.crash_lines()
    assert not bad, "crash lines in log: " + str(bad[:3])
    print("PERSISTENCE OK")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
