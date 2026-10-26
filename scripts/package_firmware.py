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

## Flashing (Windows, Linux or macOS)

1. Unzip this archive. Connect your ESP32-S3 N16R8 using a USB DATA cable.
2. Install Python 3 and a supported esptool 5.x version:
   python -m pip install "esptool>=5,<6"
3. Find your device serial port (Windows example: COM5; Linux: /dev/ttyUSB0).
4. In this extracted folder, run:

   python -m esptool --chip esp32s3 --port COM5 write-flash {args}

Replace COM5 with your board's actual port. The offsets above were obtained
from this exact build's ESP-IDF flash metadata, not guessed.

DO NOT erase flash unnecessarily; changing the partition table or firmware can
still affect existing saved worlds. Back up world data before flashing.
Flash mode/size/frequency defaults preserve the values embedded in the images.
No hardware or runtime operation is guaranteed by successful CI compilation.

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
