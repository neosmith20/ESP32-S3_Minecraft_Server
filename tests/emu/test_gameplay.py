#!/usr/bin/env python3
"""Emulator test: farming, two players at once, memory use and crash markers.

Needs the emulator test build (EMU_TEST_BUILD), which gives every new player
a hoe, seeds, carrots, potatoes and bone meal and prints free memory.
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from emulator import Emulator, fresh_image
from mcclient import Client

# hotbar slots of the test kit (see GIVE_TEST_KIT in src/procedures.c)
SLOT_HOE, SLOT_SEEDS, SLOT_BONE_MEAL = 0, 1, 4


def select(c, slot):
    c.select_slot(slot)


def grass_column(c, skip=()):
    for (cx, cz) in sorted(c.chunks):
        for dx in range(3, 13):
            for dz in range(3, 13):
                x, z = cx * 16 + dx, cz * 16 + dz
                if (x, z) in skip:
                    continue
                t = c.surface(x, z)
                if t is not None and c.block_at(x, t, z) == 9:
                    return x, t, z
    raise AssertionError("no grass column found")


def main(image_src, elf, workdir):
    os.makedirs(workdir, exist_ok=True)
    image = fresh_image(image_src, os.path.join(workdir, "flash.bin"))
    emu = Emulator(image, elf, port=25580)
    emu.start()
    assert emu.wait_for("Server listening", 60), "server did not start"

    alice = Client("alice", port=25580)
    alice.join()
    bob = Client("bob", port=25580)
    bob.join()
    print("two players joined")

    # ---- multiplayer: what alice mines, bob sees ----
    mx, my, mz = grass_column(alice)
    alice.dig(mx, my, mz)
    alice.pump(3)
    bob.pump(3)
    assert alice.block_at(mx, my, mz) == 0, "alice's dig failed"
    assert bob.block_at(mx, my, mz) == 0, "bob did not see alice's dig"
    print("multiplayer block sync ok")

    # ---- farming: hoe grass, plant seeds, grow with bone meal ----
    fx, fy, fz = grass_column(alice, skip={(mx, mz)})
    select(alice, SLOT_HOE)
    alice.place(fx, fy, fz, face=1)
    alice.pump(3)
    farmland = alice.block_at(fx, fy, fz)
    print("block after hoe:", farmland)
    assert farmland not in (None, 0, 9, 10), "hoe did not make farmland"
    select(alice, SLOT_SEEDS)
    alice.place(fx, fy, fz, face=1)
    alice.pump(3)
    seed = alice.block_at(fx, fy + 1, fz)
    print("block above farmland after planting:", seed)
    assert seed not in (None, 0), "seeds were not planted"
    select(alice, SLOT_BONE_MEAL)
    for _ in range(4):
        alice.place(fx, fy, fz + 0, face=1)  # aimed at the farmland top, crop is above
        alice.place(fx, fy + 1, fz, face=1)
        alice.pump(2)
    grown = alice.block_at(fx, fy + 1, fz)
    print("crop state after bone meal:", grown)
    assert grown not in (None, 0), "crop vanished"
    assert grown != seed, "bone meal did not advance the crop"
    print("farming ok")

    # ---- stay online a while, watch memory ----
    waited = alice.wait_server_seconds(25)
    bob.pump(1)
    mem = [l for l in emu.lines if l.startswith("MEM ")]
    assert mem, "no MEM lines (is this the EMU_TEST_BUILD firmware?)"
    last = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", mem[-1])}
    print("last memory report:", last)
    assert last["internal_min"] > 20 * 1024, "internal RAM nearly exhausted: %d bytes at its lowest" % last["internal_min"]
    assert last["psram_min"] > 1024 * 1024, "PSRAM nearly exhausted: %d bytes at its lowest" % last["psram_min"]
    assert emu.alive(), "emulator died"
    assert emu.crash_lines() == [], "crash markers in the log: " + str(emu.crash_lines()[:3])
    alice.close()
    bob.close()
    time.sleep(2)
    emu.stop()
    print("GAMEPLAY OK")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
