# Emulator tests (ESP32-S3 N16R8)

These tests run the **real firmware image** (bootloader, partition table, app,
16 MB flash, 8 MB octal PSRAM, LittleFS save partition) inside Espressif's
[esp-emulator](https://github.com/espressif/esp-emulator) and talk to it over
TCP with a scripted Minecraft 1.21.8 client (`mcclient.py`).

**Important:** the emulator does not model a physical board's speed or flash
latency. It finds crashes, hangs, lost saves and bad memory use. It cannot tell
you how fast saving is on a real chip, so the old flash-saving lag must still
be checked on hardware.

## What is tested
| Test | What it checks |
|---|---|
| `test_persistence.py` | mined blocks stay gone after a normal restart (journal merged on disconnect) |
| `test_crash.py` | power cut while a player is online: blocks mined/placed more than one journal interval earlier survive (flash is copied out of the running emulator, firmware booted from the copy) |
| `test_gameplay.py` | two players at once (block sync), hoe + seeds + bone meal, free internal RAM and PSRAM, crash/watchdog markers in the log |

Time inside the emulator runs slower than wall time (about 0.3x here), so
tests wait on the **server's own clock** (Update Time packets), never on `sleep`.

## Running
1. Build and merge the image (needs PlatformIO and `pip install esptool`):
   `tests/emu/build_image.sh`
2. Install the emulator once:
   `curl -fsSL https://raw.githubusercontent.com/espressif/esp-emulator/main/install.sh | sh -s -- --version 0.51.0`
3. Run: `python3 tests/emu/run_emu_tests.py .pio/build/esp32s3-n16r8-emu/merged_emu.bin .pio/build/esp32s3-n16r8-emu/firmware.elf`

The `env:esp32s3-n16r8-emu` build adds `-DEMU_TEST_BUILD`: every new player gets
the test kit and the server prints `MEM ...` lines every 10 seconds. Do not
flash that build on a server you play on.
