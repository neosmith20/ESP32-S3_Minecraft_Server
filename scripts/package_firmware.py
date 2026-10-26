#!/usr/bin/env python3
"""Create a portable, checksummed ESP32-S3 firmware ZIP from PlatformIO output.

Never guesses flash offsets: they come from ESP-IDF's flasher_args.json.
A missing or unrecognized flash image is a packaging error.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path

EXPECTED_ROLES = {"bootloader.bin", "partitions.bin", "firmware.bin"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for data in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def get_role(original: str) -> str:
    name = original.replace("\\", "/").lower()
    if "bootloader" in name:
        return "bootloader.bin"
    if "partition" in name:
        return "partitions.bin"
    if name.endswith(".bin"):
        return "firmware.bin"
    raise ValueError(f"Unrecognized flash image in ESP-IDF metadata: {original}")


def package(build_dir: Path, output: Path, version: str, sha: str, minecraft: str) -> Path:
    if not re.fullmatch(r"v\d+\.\d+\.\d+-dev\.[1-9]\d*", version):
        raise ValueError("Development tag must be like v0.1.0-dev.7")
    if not re.fullmatch(r"[a-f0-9]{40}", sha):
        raise ValueError("Source commit must be a full lowercase 40-character SHA")
    if not re.fullmatch(r"\d+\.\d+\.\d+", minecraft):
        raise ValueError("Invalid Minecraft version")

    flash_path = build_dir / "flasher_args.json"
    if not flash_path.is_file():
        raise FileNotFoundError(f"Missing ESP-IDF flash metadata: {flash_path}")
    flash = json.loads(flash_path.read_text(encoding="utf-8"))
    files = flash.get("flash_files")
    if not isinstance(files, dict) or len(files) != 3:
        raise ValueError("Expected bootloader, partition and application images in flash_files")

    output.mkdir(parents=True, exist_ok=True)
    payload = output / "payload"
    if payload.exists():
        shutil.rmtree(payload)
    payload.mkdir(parents=True)

    portable = {}
    used_roles = set()
    for original_offset, original_image in sorted(files.items(), key=lambda kv: int(kv[0], 0)):
        offset = int(original_offset, 0)
        if offset < 0 or offset >= 16 * 1024 * 1024 or offset % 4096:
            raise ValueError(f"Unexpected flash offset: {original_offset}")
        if not isinstance(original_image, str):
            raise ValueError("Invalid flash image path")
        role = get_role(original_image)
        if role in used_roles:
            raise ValueError(f"Duplicate flash image role: {role}")
        used_roles.add(role)
        source = build_dir / role  # PlatformIO flattens ESP-IDF image paths.
        if not source.is_file() or not source.stat().st_size:
            raise FileNotFoundError(f"Required flash image missing: {source}")
        shutil.copyfile(source, payload / role)
        portable[f"0x{offset:x}"] = role

    if used_roles != EXPECTED_ROLES:
        raise ValueError(f"Missing flash images: {sorted(EXPECTED_ROLES - used_roles)}")

    (payload / "flasher_args.json").write_text(json.dumps({
        "flash_settings": flash.get("flash_settings", {}),
        "flash_files": portable,
    }, indent=2) + "\n", encoding="utf-8")
    info = {
        "project": "ESP32-S3 Minecraft Server",
        "board": "ESP32-S3 N16R8",
        "flash_bytes": 16 * 1024 * 1024,
        "psram_bytes": 8 * 1024 * 1024,
        "minecraft_java_version": minecraft,
        "version": version,
        "source_commit": sha,
        "source_url": f"https://github.com/neosmith20/ESP32-S3_Minecraft_Server/tree/{sha}",
        "flash_files": portable,
        "status": "Experimental; hardware functionality has not been verified by CI",
    }
    (payload / "manifest.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    source_license = Path("LICENSE")
    if not source_license.is_file():
        raise FileNotFoundError("GPL-3.0 LICENSE file must accompany distributed firmware")
    shutil.copyfile(source_license, payload / "LICENSE")

    args = " ".join(f"{offset} {name}" for offset, name in portable.items())
    guide = f"""# ESP32-S3 Minecraft Server — {version}

**Experimental development firmware. Target: ESP32-S3 N16R8 ONLY (16 MB flash, 8 MB PSRAM).**
Minecraft Java {minecraft}; source commit {sha}.

## Flashing this prebuilt release using Visual Studio Code

This ZIP is ready-to-flash firmware, NOT a PlatformIO source project.
It does not contain platformio.ini, so do NOT use PlatformIO's Build/Upload
buttons with this folder. You also do NOT need extract_registries.sh, Node.js,
Java, or the Minecraft server JAR to flash it.

1. Unzip the release and open the extracted folder using
   **Visual Studio Code > File > Open Folder**.
2. Connect an ESP32-S3 N16R8 with a USB DATA cable.
3. Open **Terminal > New Terminal** and install Python 3 and esptool 5.x:

   python -m pip install "esptool>=5,<6"

   On Windows, use "py -m pip" and "py -m esptool" if "python" is not found.
4. Find the serial port. Windows: Device Manager, e.g. COM5.
   Linux: e.g. /dev/ttyUSB0. macOS: e.g. /dev/cu.usbmodem...
5. From the extracted folder, run this exact release-specific command:

   python -m esptool --chip esp32s3 --port COM5 write-flash {args}

   Replace COM5 with your port; use "py -m esptool" on Windows if required.
   The addresses above are from THIS firmware build's ESP-IDF metadata.
   They are not generic values: do not guess offsets or flash firmware.bin
   alone to a blank board.
6. To view logs, use a VS Code terminal:
   python -m serial.tools.miniterm COM5 115200

   Replace COM5 with your port, and install "pyserial" if needed.
   Close serial monitors before flashing again.

## Current Wi-Fi limitation — important

The publicly built firmware currently embeds WIFI_SSID="your-ssid" and
WIFI_PASS="your-password" from include/globals.h. You CAN flash this ZIP,
but it normally CANNOT join your home Wi-Fi or host a reachable Minecraft
server using those placeholders. Until Wi-Fi provisioning is added, build
from the main-branch SOURCE ZIP using VS Code + PlatformIO, set your local
Wi-Fi credentials, generate registry files, and then upload that firmware.
The full source-workflow instructions are in the project README.

The server runs on the ESP32-S3, not within VS Code. It is experimental:
hardware operation is not validated by CI. Keep it on a trusted local LAN
because the Minecraft server currently runs without online authentication.

Do not erase flash unnecessarily; changes in partition layout or world data
format can affect previously saved worlds. Back up world data before upgrading.
Flash mode/frequency settings are taken from the compiled images.

## Checking the archive
The outer .zip.sha256 published next to this ZIP checks the entire download.
This ZIP also includes SHA256SUMS.txt for its individual contents.

Project & source: https://github.com/neosmith20/ESP32-S3_Minecraft_Server
License: GPL-3.0 (see LICENSE). Minecraft is owned by Mojang/Microsoft.
The Minecraft server JAR is NOT included in the release.
"""
    (payload / "FLASHING.md").write_text(guide, encoding="utf-8")
    checksum_targets = sorted(p for p in payload.iterdir() if p.is_file())
    (payload / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256(p)}  {p.name}\n" for p in checksum_targets), encoding="utf-8"
    )

    name = f"ESP32-S3-N16R8-MC{minecraft}-{version}.zip"
    archive = output / name
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for source in sorted(payload.iterdir()):
            bundle.write(source, arcname=source.name)
    (output / f"{name}.sha256").write_text(f"{sha256(archive)}  {name}\n", encoding="utf-8")
    shutil.rmtree(payload)
    print(f"Verified {len(portable)} flash images; packaged {archive.name} ({archive.stat().st_size} bytes)")
    print(f"Flash layout: {portable}")
    return archive


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--minecraft", default="1.21.8")
    args = parser.parse_args()
    package(args.build_dir, args.output, args.version, args.sha, args.minecraft)


if __name__ == "__main__":
    main()
