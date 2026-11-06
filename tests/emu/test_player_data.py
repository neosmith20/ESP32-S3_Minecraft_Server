#!/usr/bin/env python3
"""Emulator test: a player's inventory and position survive a power cut.

Player data is copied to its own small file every PLAYER_SNAPSHOT_INTERVAL
(20 s in the emulator test build). The test mines a block (so the inventory
changes), moves, waits past the interval, takes a flash copy as the power is
cut, boots from the copy and compares what the server sends on rejoin.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from emulator import Emulator, fresh_image
from mcclient import Client


def main(image_src, elf, workdir):
    os.makedirs(workdir, exist_ok=True)
    image = fresh_image(image_src, os.path.join(workdir, "flash.bin"))
    snap = os.path.join(workdir, "snapshot.bin")
    emu = Emulator(image, elf, port=25590)
    emu.start()
    assert emu.wait_for("Server listening", 60), "server did not start"
    c = Client("carol", port=25590)
    c.join()
    kit = dict(c.slots)
    assert kit, "no inventory received on join (EMU_TEST_BUILD image needed)"

    # mine a grass block: the dirt item changes the inventory
    x = z = top = None
    for (cx, cz) in sorted(c.chunks):
        for dx in range(3, 13):
            for dz in range(3, 13):
                t = c.surface(cx * 16 + dx, cz * 16 + dz)
                if t is not None and c.block_at(cx * 16 + dx, t, cz * 16 + dz) == 9:
                    x, z, top = cx * 16 + dx, cz * 16 + dz, t
                    break
            if top is not None:
                break
        if top is not None:
            break
    c.dig(x, top, z)
    c.pump(3)
    assert c.slots != kit, "mining did not change the inventory"
    want_slots = dict(c.slots)
    # walk a few blocks away on the surface
    wx, wy, wz = x + 3.5, top + 1.0, z + 3.5
    c.move(wx, wy, wz)
    c.pump(2)

    waited = c.wait_server_seconds(25)
    print("server clock advanced %.1f s in %.1f s of real time" % waited)
    emu.read_flash_region(0, 16 * 1024 * 1024, snap)
    emu.crash()
    c.close()

    emu2 = Emulator(snap, elf, port=25590)
    emu2.start()
    assert emu2.wait_for("Server listening", 60), "server did not restart"
    log = emu2.text()
    assert "Restored player data from the latest player snapshot." in log, "snapshot was not used on restart"
    c2 = Client("carol", port=25590)
    c2.join()
    print("inventory before cut:", len(want_slots), "slots; after restart:", len(c2.slots), "slots")
    assert c2.slots == want_slots, "inventory differs after the power cut\nbefore: %s\nafter: %s" % (want_slots, c2.slots)
    assert c2.sync_pos is not None, "no position sent on rejoin"
    print("rejoin position:", c2.sync_pos, "moved to:", (wx, wy, wz))
    assert abs(c2.sync_pos[0] - wx) < 1.5 and abs(c2.sync_pos[2] - wz) < 1.5, "position was not restored"
    emu2.stop()
    assert emu.crash_lines() == [] and emu2.crash_lines() == [], "crash markers in the log"
    print("PLAYER DATA OK")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
