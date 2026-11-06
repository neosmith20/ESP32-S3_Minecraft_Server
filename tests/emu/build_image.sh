#!/usr/bin/env bash
# Builds the emulator test firmware with PlatformIO and merges it into one
# 16 MB flash image (bootloader + partition table + app) for esp-emu.
# usage: tests/emu/build_image.sh   (run from the project folder)
set -euo pipefail
pio run -e esp32s3-n16r8-emu
B=.pio/build/esp32s3-n16r8-emu
python3 -m esptool --chip esp32s3 merge-bin -o "$B/merged_emu.bin" \
  --flash-mode dio --flash-size 16MB --fill-flash-size 16MB \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0x10000 "$B/firmware.bin"
echo "Image: $B/merged_emu.bin   ELF: $B/firmware.elf"
