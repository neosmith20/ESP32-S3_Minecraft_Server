# What changed, version by version

## Version 2.1

* Farming: farmland, wheat, carrots, potatoes, hoes, bread, baked potatoes, bone meal on crops.
* Fixed: grass blocks dropped nothing when mined by hand.
* The server now recreates a world file that has the wrong size instead of stopping.

## Version 2

Version 2 is a rewrite of how the server stores blocks, so it can behave much
more like a real Minecraft server. **It starts a new world** (the old world
file is deleted from the board's flash on first start).

## What is new in version 2

* **Real Minecraft block IDs.** Blocks are no longer squeezed into a 256-entry
  list. Every block uses its real 1.21.8 state, so doors, stairs, slabs, fences
  and so on can face any direction, be open or closed, top or bottom, etc.
* **New blocks you can craft and place:** doors (open and close), trapdoors,
  fences and fence gates (they connect to each other and to walls), glass
  panes, iron bars, ladders, wall torches, lanterns, stairs (6 materials),
  slabs (8 materials, two slabs make a full block), walls (3 materials),
  buttons and pressure plates, stone bricks and sandstone variants,
  smooth stone, storage blocks (iron, gold, diamond, coal, copper, redstone),
  iron nuggets, composter, wooden/stone/iron/gold/diamond hoes, shears.
* **Logs keep their direction** (upright or lying down) and chests, furnaces,
  stairs and ladders face the right way.
* **110 crafting recipes**, listed in `recipes_config.js`. Recipes can be
  placed anywhere in the crafting grid and mirrored, like in the real game.
* **Easy to extend.** To add a block or a recipe, add one line to
  `blocks_config.js` or `recipes_config.js` and run `node build_registries.js`.
* **Chunks use per-section palettes**, so they are smaller and the client gets
  exactly what is in the world.
* **Block changes** take 7 bytes each and the limit is 65,000 with PSRAM.
* The "stall logging" debug option (`DEV_LOG_STALLS` in `include/globals.h`)
  is now turned off by default.

## Setup differences from version 1

Use a **new folder** (for example `I:\ESP32\esp32s3-mc-v2`), so your version 1
project stays as it is. Steps 1 to 3 below are otherwise the same. After you
have the `notchian` folder, `./extract_registries.sh` (or `node build_registries.js`)
creates `include/registries.h` and `src/registries.c`.

---

# Version 1 notes (earlier changes, still true for the performance work)

This project started from an open-source minimal Minecraft server (see the credits in README.md) and is tuned for a
board with 16 MB flash and 8 MB PSRAM. Everything below is relative to the
folder you extract this project into (called `<project>` here).

## Before you build

1. **Generate the Minecraft data files** (one time, needs Java 21+ and Node):
   1. Download the Minecraft **1.21.8** `server.jar` (for example from
      https://mcversions.net/download/1.21.8).
   2. Save it as `<project>/notchian/server.jar` (create the `notchian`
      folder if it doesn't exist).
   3. In a terminal, from `<project>`, run `./extract_registries.sh`.
      It creates `<project>/include/registries.h` and `<project>/src/registries.c`.
2. **Enter your WiFi details** in `<project>/include/globals.h`: find the two
   lines `#define WIFI_SSID "your-ssid"` and `#define WIFI_PASS "your-password"`
   near the top and fill them in.
3. **Open `<project>` in VS Code with the PlatformIO extension**, then use
   Build, Upload and Monitor (or `pio run -t upload` and `pio device monitor`
   in a terminal in `<project>`). The serial monitor prints the board's IP
   address once it joins WiFi. Connect with Minecraft Java 1.21.8.

If you change `<project>/sdkconfig.defaults` later, delete the generated
`<project>/sdkconfig.esp32s3-n16r8` file so the new settings are picked up.

## What changed, in plain English

### Chunk sending (the biggest win)
Files: `src/packets.c`, `src/worldgen.c`, `include/worldgen.h`

* **Empty sky is no longer generated or sent in full.** Terrain never goes
  above Y=79, but the original generated and sent 20 full sections for every
  chunk, 15 of them just air. Now only the 5 sections that can contain terrain
  are sent in full; the rest are sent in the same tiny "all air" form the
  original already used for the top of the world. A section above that is
  still sent in full if a player built something in it.
  Setting: `AIR_ABOVE_Y` in `include/globals.h`.
* **Player-made blocks are scanned once per chunk, not 20 times.**
* **Generated terrain is cached in PSRAM** (192 chunk columns, about 3.75 MB).
  Walking back to an area, or a second player joining near the first, reuses
  it. Setting: `CHUNK_CACHE_COLUMNS`. If PSRAM isn't available the cache just
  switches itself off.

### Network
Files: `src/tools.c`, `src/main.c`, `include/tools.h`

* **Small writes are batched.** Most packets were sent a byte or two at a time,
  each as its own network call. They are now gathered in a 2880-byte buffer and
  sent together (sent at the start of every server loop, so nothing waits long).
* **TCP_NODELAY is turned on** for each client, since batching is now done by
  the server itself.

### Block lookups
Files: `src/procedures.c`, `include/procedures.h`

* **Looking up a player-placed block is now instant** (hash table) instead of
  scanning up to the whole list. This runs on every player movement packet.

### Bigger world, saved to flash
Files: `include/globals.h`, `src/serialize.c`, `include/serialize.h`, `src/globals.c`

* **60000 player block changes instead of 20000** when PSRAM is set up as in
  `sdkconfig.defaults` (the arrays live in PSRAM).
* **World saves to flash** (LittleFS, `littlefs` partition in `partitions.csv`).
  Changes are collected and written every 15 seconds, and only the range of
  entries that changed is written. The original wrote every entry separately
  and could read one entry past the end of the array when the world was full;
  that is fixed.
* The save file is about 360 KB. **If you change `MAX_BLOCK_CHANGES` after the
  server has already saved a world, the old save no longer fits and the server
  stops at startup.** Run `pio run -t erase` once to wipe the board, or turn
  saving off.

### Using both CPU cores
File: `src/main.c`

* The server runs pinned to **core 1**; the network stack is pinned to core 0
  next to WiFi (`sdkconfig.defaults`). Before, the server could land on the same
  core as WiFi and fight it for time.
* I did **not** split the server's own work across both cores. The server keeps
  its state in shared global variables and handles one packet at a time, so
  running chunk generation on a second core would need a larger redesign and
  would risk subtle bugs I can't test without your hardware.

### Board settings (new files)
`platformio.ini`, `sdkconfig.defaults`, `partitions.csv`, `CMakeLists.txt`,
`src/idf_component.yml`: 240 MHz, compiler set to optimize for speed, 16 MB
flash, octal PSRAM at 80 MHz, 1 ms task ticks, larger TCP buffers, and the
LittleFS add-on for saving.

### A bug I believe I fixed
Chests store their items in the 14 block-change entries after them. The
original chunk code treated those entries as real blocks when building
chunks, which could draw stray blocks. The new code skips them, as the rest
of the original code already did.

## What was tested, and what wasn't

Tested on a normal PC (Linux) with placeholder Minecraft data:

* The original and modified code produce **identical block data** for several
  hundred chunks, including heavy building, removals, blocks far above the
  ground, and a chunk with more changes than the index holds.
* Every chunk packet's declared length matches the bytes actually sent.
* The block lookup agrees with the original slow method after thousands of
  placements, removals and chests.
* The real server loop was joined by a scripted client, which received and
  validated 25 chunks on joining and 15 more while crossing chunk borders.
* Data per chunk: about 141 KB before, about 75 KB for plain terrain (about
  53%), and more for chunks with player builds in the upper sections. These
  sizes use a placeholder block palette, so real numbers will be a little
  higher, but the ratio should be similar.

**Not tested: anything on a real ESP32-S3.** I couldn't download the ESP32
build tools here, so the board configuration files and the ESP-only code paths
(PSRAM placement, LittleFS saving, core pinning) have **never been compiled or
run**. Expect to possibly fix a small error on the first build. Speed numbers
on the board are unknown; PC timings are only a rough ratio.

## If something goes wrong

* **Build error about `esp_littlefs.h`**: delete `src/idf_component.yml`
  (saving is then switched off automatically), or fix the LittleFS dependency.
* **Boots but crashes or reboots at startup**: set `CHUNK_CACHE_COLUMNS` to 0
  and `MAX_BLOCK_CHANGES` back to 20000 in `include/globals.h`.
* **Players disconnect or stutter**: try `VIEW_DISTANCE 2` (the default), or turn
  off `BROADCAST_ALL_MOVEMENT` as the original README suggests.
* **World doesn't save**: check the serial monitor for LittleFS messages.

## Re-running the PC tests

The `tests/` folder has the test program (`test_chunks.c`), the scripted
client (`e2e_client.py`) and the placeholder-data generator
(`make_stub_registries.py`). Run the generator on this folder to create stub
registry files outside the project, compile everything in `src/` (minus
`main.c` for `test_chunks.c`) against them, and run.
