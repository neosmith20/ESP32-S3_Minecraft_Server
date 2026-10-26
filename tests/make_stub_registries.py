#!/usr/bin/env python3
"""Generates placeholder registries.h / registries.c for compile and logic tests.

The real files come from a vanilla Minecraft server jar. These stubs only
provide the same names so the server code builds, plus a 256-entry block
palette. Block 'air' is index 0 with global state id 0 (as in vanilla).
"""
import re
import sys
from pathlib import Path

project = Path(sys.argv[1])
out_dir = Path(sys.argv[2])
out_dir.mkdir(parents=True, exist_ok=True)

text = ""
for p in list(project.glob("src/*.c")) + list(project.glob("include/*.h")):
    if p.name.startswith("registries"):
        continue
    text += p.read_text(errors="ignore")

def names(prefix, skip=()):
    found = sorted(set(re.findall(r"\b" + prefix + r"_[A-Za-z0-9_]+\b", text)))
    return [n for n in found if n not in skip]

blocks = [n for n in names("B", ("B_to_I",)) if n != "B_air"]
blocks = ["B_air"] + blocks
items = names("I", ("I_to_B",))
damage = names("D")
biomes = ["W_plains", "W_mangrove_swamp", "W_desert", "W_snowy_plains", "W_beach"]

assert len(blocks) <= 256, len(blocks)

def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)

# Global state ids: air is 0, bedrock must be 85 in the real data but is
# only referenced by number in packets.c, so any distinct ids work here.
palette = [0] + [100 + 7 * i for i in range(1, len(blocks))]
palette += [0] * (256 - len(palette))
net_palette = b"".join(varint(v) for v in palette)

def carray(data):
    return ", ".join(str(b) for b in data)

header = ["#ifndef H_REGISTRIES", "#define H_REGISTRIES", "#include <stdint.h>"]
header += ["extern uint8_t registries_bin[2];", "extern uint8_t tags_bin[2];"]
header += ["extern uint16_t block_palette[256];"]
header += [f"extern uint8_t network_block_palette[{len(net_palette)}];"]
header += ["extern uint16_t B_to_I[256];", "uint8_t I_to_B (uint16_t item);"]
header += [f"#define {n} {i}" for i, n in enumerate(blocks)]
header += [f"#define {n} {i + 1}" for i, n in enumerate(items)]
header += [f"#define {n} {i}" for i, n in enumerate(biomes)]
header += [f"#define {n} {i}" for i, n in enumerate(damage)]
header += ["#endif", ""]
(out_dir / "registries.h").write_text("\n".join(header))

src = f"""#include <stdint.h>
#include "registries.h"
uint8_t registries_bin[2] = {{1, 0x7F}};
uint8_t tags_bin[2] = {{1, 0x7E}};
uint16_t block_palette[256] = {{ {", ".join(str(v) for v in palette)} }};
uint8_t network_block_palette[] = {{ {carray(net_palette)} }};
uint16_t B_to_I[256] = {{0}};
uint8_t I_to_B (uint16_t item) {{ return 0; }}
"""
(out_dir / "registries.c").write_text(src)
print(f"{len(blocks)} blocks, {len(items)} items, {len(damage)} damage types, palette bytes {len(net_palette)}")
