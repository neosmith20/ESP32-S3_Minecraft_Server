# ESP32-S3 Minecraft Server

A Minecraft Java 1.21.8 server that runs on an ESP32-S3 (N16R8: 16 MB flash, 8 MB PSRAM),
based on [p2r3/bareiron](https://github.com/p2r3/bareiron) (GPL-3.0).

Version 2 uses real Minecraft block IDs, so it has doors, trapdoors, stairs, slabs, fences,
walls, ladders, lanterns and more, with 110 crafting recipes. Add your own blocks and recipes
in `blocks_config.js` and `recipes_config.js`.

**Start with `S3-CHANGES.md`** for setup steps and what changed. The original bareiron readme is
in `BAREIRON-README.md`.

Tests (run on a PC): `tests/test_chunks.c`, `tests/test_game.c`, `tests/e2e_client.py`.
