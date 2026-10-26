# ESP32-S3 Minecraft Server

A small **Minecraft Java Edition server that runs on an ESP32-S3 microcontroller**. It uses
about the power of a phone charger, fits on a board that costs a few dollars, and you can
join it with a normal Minecraft client.

It is written in C, built with PlatformIO / ESP-IDF, and made for the **ESP32-S3 N16R8**
(16 MB flash, 8 MB octal PSRAM).

## Which Minecraft do I use?

| | |
|---|---|
| **Game** | Minecraft: **Java Edition** (not Bedrock / Windows 10 Edition / console) |
| **Version** | **1.21.8** exactly (protocol 772). Other versions cannot join. |
| **Address** | The board's IP address, for example `192.168.1.50` (port 25565 is the default) |
| **Mode** | Survival. Online-mode (account checks) is off, so it is meant for a home network or friends you trust. |

In the Minecraft launcher, create an installation for **1.21.8**, start it, choose
*Multiplayer → Add Server*, and enter the board's IP address.

## What you need

* An **ESP32-S3 board with 16 MB flash and 8 MB octal PSRAM** (N16R8). Other ESP32-S3
  boards will need changes to `platformio.ini`, `sdkconfig.defaults` and `partitions.csv`.
* A USB cable and a 2.4 GHz WiFi network.
* On your computer: [Visual Studio Code](https://code.visualstudio.com/) with the
  [PlatformIO extension](https://platformio.org/install/ide?install=vscode),
  [Node.js](https://nodejs.org/) and [Java 21 or newer](https://adoptium.net/).
* Git Bash (comes with [Git for Windows](https://git-scm.com/download/win)) or any
  Linux/macOS terminal, to run one setup script.

## Setup

1. **Get the files.** Download this repository (green *Code* button → *Download ZIP*) and
   extract it, or run `git clone https://github.com/neosmith20/ESP32-S3_Minecraft_Server`.
   The folder is called `<project>` below.
2. **Download the Minecraft server file.** Get the official **1.21.8** `server.jar` from
   Mojang (for example from <https://mcversions.net/download/1.21.8>). Create a folder named
   `notchian` inside `<project>` and save the file as `<project>/notchian/server.jar`.
   The jar is only used to read Minecraft's block and item data. It is not run on the board
   and is not included here, because it belongs to Mojang.
3. **Generate the data files.** Open a terminal (Git Bash on Windows) in `<project>` and run
   `./extract_registries.sh`. This creates `<project>/include/registries.h` and
   `<project>/src/registries.c`. If you later edit `blocks_config.js` or `recipes_config.js`,
   run `node build_registries.js` to regenerate them.
4. **Enter your WiFi.** In `<project>/include/globals.h`, change
   `#define WIFI_SSID "your-ssid"` and `#define WIFI_PASS "your-password"`.
5. **Build and upload.** Open `<project>` in VS Code, plug in the board, and use the
   PlatformIO *Upload* and *Monitor* buttons (or `pio run -t upload` and `pio device monitor`).
   The serial monitor shows the board's IP address once it joins WiFi.
6. **Join** with Minecraft 1.21.8 using that IP address.

The first start creates a fresh world and saves it to the board's flash. If you change
`sdkconfig.defaults` later, delete `<project>/sdkconfig.esp32s3-n16r8` so it is picked up.

## What works

* Survival terrain with biomes, trees, caves, ores, water and lava.
* Mining with proper tool requirements, drops, tool durability, hunger and health.
* **110 crafting recipes** (tools, armor, doors, fences, stairs, slabs, walls, ladders, chests,
  furnaces, lanterns, storage blocks and more). Patterns can go anywhere in the grid and
  can be mirrored, like the real game. Furnace smelting works too.
* Blocks with shapes: **doors** (open/close), trapdoors, fence gates, stairs, slabs (two make a
  full block), fences, walls and glass panes that connect to their neighbors, ladders, wall
  torches, and logs placed upright or sideways.
* Chests with storage, animals and some hostile mobs, day/night, saving your world.
* Several players at once (the server allows up to 16 connections, but a microcontroller
  is happiest with 4 to 8).

## What does not work (yet)

No Nether or End, villages, redstone circuits, enchanting, brewing, farming or furnace timers
yet. It is a small server, not a replacement for the real thing.

## Adding blocks and recipes

Blocks are listed in `blocks_config.js` and recipes in `recipes_config.js`. Add a line, run
`node build_registries.js`, rebuild, and upload.

## Tests

The tests run on a PC (not on the board) and need `gcc`:
`tests/test_chunks.c` checks that chunk data matches the world, `tests/test_game.c` checks
crafting and block behavior, and `tests/e2e_client.py` joins a PC build of the server like a
real client. `S3-CHANGES.md` lists what changed in each version.

## Credits and license

This project started from a minimal open-source Minecraft server written in C by
[p2r3](https://github.com/p2r3), whose work made this possible. It is licensed under the
**GNU General Public License v3.0** (see `LICENSE`), the same license as the original.

Minecraft is a trademark of Mojang AB / Microsoft. This project is not affiliated with or
endorsed by Mojang, Microsoft or Anthropic.
