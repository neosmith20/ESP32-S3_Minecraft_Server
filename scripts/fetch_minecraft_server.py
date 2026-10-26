#!/usr/bin/env python3
"""Download a pinned Minecraft Java server JAR via Mojang's official manifest.

Used only for generating registries during CI; the JAR is not committed or
included in firmware artifacts. Verify its official SHA-1 before using it.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from urllib.parse import urlparse
from urllib.request import urlopen

VERSION = "1.21.8"
MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
METADATA_HOSTS = {"piston-meta.mojang.com", "launchermeta.mojang.com"}
DOWNLOAD_HOSTS = {"piston-data.mojang.com", "launcher.mojang.com"}


def fetch(url: str, allowed_hosts: set[str]) -> bytes:
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in allowed_hosts:
        raise RuntimeError(f"Unexpected download location: {url}")
    with urlopen(url, timeout=90) as response:
        return response.read()


def main() -> None:
    manifest = json.loads(fetch(MANIFEST_URL, METADATA_HOSTS))
    version = next((v for v in manifest["versions"] if v["id"] == VERSION), None)
    if version is None:
        raise RuntimeError(f"Version {VERSION} is absent from Mojang's manifest")
    details = json.loads(fetch(version["url"], METADATA_HOSTS))
    server = details["downloads"]["server"]
    payload = fetch(server["url"], DOWNLOAD_HOSTS)
    digest = hashlib.sha1(payload).hexdigest()
    if digest.lower() != server["sha1"].lower() or len(payload) != server["size"]:
        raise RuntimeError("Official Minecraft server JAR checksum/size mismatch")
    path = Path("notchian/server.jar")
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".download")
    temp.write_bytes(payload)
    os.replace(temp, path)
    print(f"Verified Minecraft {VERSION} server JAR ({len(payload)} bytes, SHA-1 {digest})")
    print("The JAR is for registry extraction only and will not be published.")


if __name__ == "__main__":
    main()
