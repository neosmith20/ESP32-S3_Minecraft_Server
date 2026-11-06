#!/usr/bin/env python3
"""Runs every emulator test against one merged flash image.

usage: run_emu_tests.py MERGED_FLASH.bin FIRMWARE.elf [WORKDIR]

The image must come from the emulator test build (PlatformIO env
esp32s3-n16r8-emu, see tests/emu/README.md). Each test starts its own
emulator, so they run one after another.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = ["test_persistence.py", "test_crash.py", "test_player_data.py", "test_gameplay.py"]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    image, elf = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    work = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else os.path.join(HERE, "_work")
    results = []
    for name in TESTS:
        start = time.time()
        print("=== %s ===" % name, flush=True)
        proc = subprocess.run(
            [sys.executable, os.path.join(HERE, name), image, elf, os.path.join(work, name[:-3])],
            timeout=1500,
        )
        results.append((name, proc.returncode == 0, time.time() - start))
    print("\nEmulator test summary (the emulator's speed is not the board's speed):")
    for name, ok, secs in results:
        print("  %-24s %s  (%.0f s)" % (name, "PASS" if ok else "FAIL", secs))
    return 0 if all(ok for _, ok, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
