# ESP32-S3 Minecraft Server

**An experimental Minecraft: Java Edition 1.21.8 server for an ESP32-S3 microcontroller.**

Run a lightweight Minecraft server on an ESP32-S3 **N16R8** board (16 MB flash, 8 MB PSRAM). This project adapts [p2r3/bareiron](https://github.com/p2r3/bareiron) for ESP-IDF / PlatformIO and extends its world, block, and crafting systems.

> [!IMPORTANT]
> **Active development — hardware operation is not yet verified in the checked-in documentation.** PC-based logic tests have been performed, but the documented ESP32-S3 firmware paths have not yet been confirmed by a successful physical-device build and test. See [S3-CHANGES.md](S3-CHANGES.md) for the current test status and known limitations.

## At a glance

| | |
|---|---|
| **Target** | ESP32-S3 N16R8 (16 MB flash / 8 MB PSRAM) |
| **Framework** | Espressif ESP-IDF via PlatformIO |
| **Language** | C (with JavaScript-based registry generation tools) |
| **Minecraft** | Java Edition 1.21.8 (protocol 772) |
| **Connection** | TCP port 25565 by default |
| **Upstream** | [p2r3/bareiron](https://github.com/p2r3/bareiron) |
| **License** | GPL-3.0 — see [LICENSE](LICENSE) |

This is a resource-constrained, experimental implementation, **not a full vanilla Minecraft server**. Gameplay and protocol behavior can differ from the official server.

## What this ESP32-S3 adaptation adds

According to the current [development notes](S3-CHANGES.md), the project includes:

- **Minecraft block states:** Support for more block types and orientation/state behavior, including doors, trapdoors, stairs, slabs, fences, ladders, lanterns, and related variants.
- **Crafting:** A configurable set of **110 recipes**.
- **Chunk optimization:** Per-section palettes and compact all-air sections to reduce traffic.
- **PSRAM-aware caching:** Cached terrain and larger block-change storage when external RAM is available.
- **Network efficiency:** Batched socket writes and TCP_NODELAY.
- **World persistence:** LittleFS integration intended to save world changes to onboard flash.
- **ESP32-S3 tuning:** CPU, flash, and PSRAM settings for the target board.

These describe implemented source changes, **not a claim that all features have been validated on real hardware**.

## Hardware and software prerequisites

- ESP32-S3 N16R8 development board and USB cable
- [Visual Studio Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension, or PlatformIO CLI
- **Java 21 or newer**, to generate Minecraft registry data
- **Node.js**, Bun, or Deno, for registry conversion
- A matching **Minecraft Java Edition 1.21.8 server JAR** for generating the registry definitions

## Getting started

### 1. Clone the project

```bash
git clone https://github.com/neosmith20/ESP32-S3_Minecraft_Server.git
cd ESP32-S3_Minecraft_Server
```

### 2. Generate the Minecraft registries

The generated `include/registries.h` and `src/registries.c` are intentionally excluded from source control. They must exist before a normal build.

Download the **Minecraft Java Edition 1.21.8 server JAR** and save it to `notchian/server.jar`:

```bash
mkdir -p notchian
# Place the matching 1.21.8 server.jar in notchian/
./extract_registries.sh
```

The script runs Minecraft's data generator and then converts its output with a JavaScript runtime. It requires Java 21+ and either Node.js, Bun, or Deno.

### 3. Configure Wi-Fi

Edit the ESP32-specific credentials near the start of `include/globals.h`:

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
```

> [!CAUTION]
> **Do not commit real Wi-Fi credentials.** This header is tracked in a public repository. Keep personal values local and check your Git diff before every push. The project should eventually move secrets into a local, untracked configuration mechanism.

### 4. Build and upload

Open the repository in PlatformIO, select the `esp32s3-n16r8` environment and use **Build**, **Upload**, and **Monitor**. Or run:

```bash
pio run -e esp32s3-n16r8
pio run -e esp32s3-n16r8 -t upload
pio device monitor -b 115200
```

When the board joins Wi-Fi, check the serial monitor for its IP address. On the same trusted network, connect from **Minecraft Java Edition 1.21.8** using that IP address; the default server port is **25565**.

> [!WARNING]
> Moving from version 1 to version 2 changes the world-storage representation. The existing notes say the old world is deleted on first start. **Back up any valuable world data and read [S3-CHANGES.md](S3-CHANGES.md) before upgrading.**

## Customizing blocks and recipes

- Modify `blocks_config.js` to configure additional blocks.
- Modify `recipes_config.js` to configure crafting recipes.
- Regenerate the registry-related source after modifying the configuration:

```bash
node build_registries.js
```

The generated data must first exist from the Minecraft registry extraction step. If you use Bun or Deno instead of Node.js, use the equivalent runtime invocation.

## Testing and development status

The repository contains host-side testing utilities:

- `tests/test_chunks.c` — chunk data and packet validation
- `tests/test_game.c` — gameplay behavior checks
- `tests/e2e_client.py` — scripted protocol client
- `tests/make_stub_registries.py` — placeholder registry data for host-side tests

The placeholder registry data is **not suitable for production game content**. PC tests do not validate ESP-IDF compilation, Wi-Fi behavior, PSRAM allocation, flash persistence, or performance on the physical board.

See [S3-CHANGES.md](S3-CHANGES.md) for detailed test history, troubleshooting, and differences from upstream. The original Bareiron guide is preserved in [BAREIRON-README.md](BAREIRON-README.md).

## Security and networking

- The default Minecraft TCP port is **25565**.
- Treat the server as an experimental network service. Test it on a **trusted LAN** rather than exposing it directly to the public internet.
- The upstream development/debug world-data upload mechanism documented in [BAREIRON-README.md](BAREIRON-README.md) is unauthenticated when enabled; do **not** enable it on an untrusted network.
- Never put passwords or private keys into committed source files.

## Origins, license, and acknowledgments

This project is derived from [Bareiron by p2r3](https://github.com/p2r3/bareiron), whose design prioritizes memory usage, performance, and lightweight operation on constrained hardware. The original project documentation remains available in [BAREIRON-README.md](BAREIRON-README.md).

Distributed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE) for the applicable terms.

Minecraft is a trademark of Mojang Studios / Microsoft. This project is an independent community effort and is not affiliated with or endorsed by them.
