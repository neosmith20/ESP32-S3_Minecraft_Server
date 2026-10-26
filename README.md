# ESP32-S3 Minecraft Server

**An experimental Minecraft: Java Edition 1.21.8 server for an ESP32-S3 microcontroller.**

Run a lightweight Minecraft server on an ESP32-S3 **N16R8** board (16 MB flash, 8 MB PSRAM). This project adapts [p2r3/bareiron](https://github.com/p2r3/bareiron) for ESP-IDF / PlatformIO and extends its world, block, and crafting systems.

> [!IMPORTANT]
> **Active development — CI compiles real ESP32-S3 firmware and runs native tests, but booting and gameplay on a physical board remain unverified in the checked-in documentation.** See [S3-CHANGES.md](S3-CHANGES.md) for the current test status and known limitations.

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

## Minecraft client compatibility

- **Edition:** Minecraft **Java Edition**, not Bedrock Edition or console Minecraft.
- **Version:** **1.21.8 only** (protocol 772); other versions are not expected to connect.
- **Game mode:** Survival by default.
- **Authentication:** The implementation currently uses offline mode, so player-account authentication is not performed. **Only allow trusted players onto a trusted network.**

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

### Current gameplay scope

The source and development notes describe terrain and biome generation, caves and ores, mining and tools, crafting and furnaces, health and hunger, chests, day/night, animals and some hostile mobs, and multiplayer support. Treat these as **implemented or intended behaviors pending hardware validation**, not as a promise of full vanilla parity.

**Not supported as a complete vanilla feature set:** Nether and End dimensions, villages, advanced redstone, enchanting, brewing, and numerous other gameplay systems. This project intentionally prioritizes efficient operation over complete vanilla compatibility.

## Using this project in Visual Studio Code

You have **two different downloads**, and the instructions are **not interchangeable**. In either case, the Minecraft server runs **on the ESP32-S3 board**, not inside VS Code. VS Code is used to flash the board, build source when needed, and view serial output.

| Download | Best for | VS Code workflow | Need Java/Node/registry extraction? |
|---|---|---|---|
| **Release firmware ZIP** (versioned `ESP32-S3-N16R8-*.zip`) | Flash the exact precompiled CI build | VS Code terminal + `esptool` | **No** |
| **Main-branch source ZIP** (GitHub **Code → Download ZIP**) | Configure Wi-Fi or change the game code | VS Code + PlatformIO **Build → Upload → Monitor** | **Yes** |

Both options require an **ESP32-S3 N16R8** (16 MB flash, 8 MB octal PSRAM), a USB **data** cable, and a computer with VS Code. The ESP32-S3 uses **2.4 GHz Wi-Fi**, not 5 GHz.

> [!IMPORTANT]
> **Current prebuilt firmware limitation:** Wi-Fi credentials are defined at compile time in `include/globals.h`. The public CI build contains the placeholder values `your-ssid` and `your-password`, so **flashing the Release ZIP alone does not produce a usable Wi-Fi Minecraft server on your network**. Until Wi-Fi provisioning is implemented, choose **Option B** to configure your own network and build/upload locally. Release ZIPs are useful for firmware flashing and version testing, but do not solve the network setup.

### Option A — Flash a GitHub Release ZIP using VS Code (no compilation)

1. Open **[GitHub Releases](https://github.com/neosmith20/ESP32-S3_Minecraft_Server/releases)**, expand **Assets** under a development release, and download the **`ESP32-S3-N16R8-MC1.21.8-<version>.zip`** asset. **Do not choose GitHub's automatic "Source code (zip)"** for this option.
2. Extract the ZIP to a normal folder. In **VS Code → File → Open Folder**, choose the folder containing `FLASHING.md`, `firmware.bin`, `bootloader.bin`, and `partitions.bin`. This is **not** a PlatformIO source project (there is no `platformio.ini`); the PlatformIO **Build/Upload** buttons do not flash this ZIP.
3. Install [Python 3](https://www.python.org/downloads/) if needed. In **VS Code → Terminal → New Terminal**, run:

   ```bash
   python -m pip install "esptool>=5,<6"
   python -m esptool version
   ```

   On Windows, use `py -m pip ...` and `py -m esptool ...` if `python` is not recognized.
4. Connect the ESP32-S3 with a USB **data** cable. Find its serial port (for example **COM5** in Windows Device Manager, **/dev/ttyUSB0** on Linux, or **/dev/cu.usbmodem...** on macOS). Close other serial monitors before flashing.
5. **Open the included `FLASHING.md` in VS Code and copy its exact `python -m esptool ... write-flash` command into the terminal.** Replace the example `COM5` with your actual port. The command uses the correct offsets for this particular release's bootloader, partition table, and firmware—**do not guess offsets or flash only `firmware.bin` on a blank device**.
6. After flashing, reset the board if needed. To read serial output from the VS Code terminal, you can run `python -m serial.tools.miniterm COM5 115200` (replace `COM5` with your port). Install `pyserial` if required. Close the monitor before the next flash.

**Result:** The prebuilt binaries are flashed, but the current firmware cannot join your Wi-Fi unless it was separately compiled with credentials matching your network. See the limitation above. See [RELEASING.md](RELEASING.md) for release/version details.

### Option B — Download the source ZIP and build/upload with VS Code + PlatformIO

1. On the [repository's main page](https://github.com/neosmith20/ESP32-S3_Minecraft_Server), click **Code → Download ZIP** (or clone the repository). Extract it; the folder should contain `platformio.ini`, `src/`, `include/`, `build_registries.js`, and `extract_registries.sh`. A Release firmware ZIP is **not** a substitute for this source folder.
2. Install [VS Code](https://code.visualstudio.com/) and its **PlatformIO IDE** extension. Use **File → Open Folder** and select the extracted folder **containing `platformio.ini`**, not its parent. Let PlatformIO initialize the project.
3. Install **Java JDK 21 or newer** and **Node.js** (or Bun/Deno) before generating registries. On Windows, install [Eclipse Temurin JDK 21](https://adoptium.net/temurin/releases/?version=21) or run the following in a **PowerShell** terminal:

   ```powershell
   winget install --id EclipseAdoptium.Temurin.21.JDK --exact
   ```

   **Close and reopen VS Code** after installing Java so new terminals inherit the updated PATH. Then verify it from **VS Code → Terminal → New Terminal**:

   ```powershell
   java -version
   node --version
   ```

   `java -version` should report **21 or newer**. For Windows, we recommend **Git Bash** (installed with [Git for Windows](https://git-scm.com/downloads/win)) to execute `extract_registries.sh`. **WSL is a separate Linux environment** and will not necessarily see your Windows Java installation: using `bash` from PowerShell may launch `C:\Windows\System32\bash.exe` (WSL) instead of Git Bash.
4. Download the official **Minecraft Java Edition 1.21.8** `server.jar` (used only for extracting registry data). Create a `notchian` folder in the repository root and save the JAR as **`notchian/server.jar`**. It is not shipped in this repository or the firmware ZIP.
5. From the **repository root** in the VS Code **Git Bash** terminal on Windows (or a normal Bash shell on Linux/macOS), run:

   ```bash
   bash ./extract_registries.sh
   ```

   **Windows troubleshooting — `Java not found in PATH.` even though `java -version` works in PowerShell?** PowerShell's bare `bash` command may start WSL instead of Git Bash. From the **PowerShell terminal in VS Code**, run Git Bash explicitly using this working Windows command:

   ```powershell
   & "C:\Program Files\Git\bin\bash.exe" -c 'bash ./extract_registries.sh'
   ```

   This uses **Git Bash**, which can access your installed Windows JDK. The path shown is the standard Git for Windows install location; adjust it if Git was installed elsewhere. You can check whether Git Bash sees Java first:

   ```powershell
   & "C:\Program Files\Git\bin\bash.exe" -lc 'java -version'
   ```

   If instead you intentionally use **WSL**, install Java 21+ inside that Linux distribution and run the script from there. Do not assume Windows PowerShell's Java PATH is shared with WSL.

   Wait for the script to generate `include/registries.h` and `src/registries.c`. It runs Minecraft's data generator and `build_registries.js`. **This step is required for building from source, but not for Option A.**
6. Open `include/globals.h` in VS Code and set your **local 2.4 GHz Wi-Fi credentials** in `WIFI_SSID` and `WIFI_PASS`. **Never publish real credentials** in this public repository or commit that edit. They are currently compiled into the binary.
7. In VS Code, open the **PlatformIO** sidebar → **PROJECT TASKS → esp32s3-n16r8 → General**. Run **Build**, then **Upload**, then **Monitor** (115200 baud). The equivalent commands in a **PlatformIO Core CLI terminal** are:

   ```bash
   pio run -e esp32s3-n16r8
   pio run -e esp32s3-n16r8 -t upload
   pio device monitor -b 115200
   ```

   If `pio` is not recognized in the regular VS Code terminal, use the **PlatformIO Core CLI** terminal or the sidebar buttons. PlatformIO may need to download the ESP-IDF toolchain on the first build.
8. When the board connects to Wi-Fi, the **serial monitor** should show its IP address. In the Minecraft launcher, select **Java Edition 1.21.8**, then choose **Multiplayer → Add Server** and enter that IP address. TCP port **25565** is the default.

> [!WARNING]
> Changing from an older world-storage version or flashing an incompatible partition layout may destroy saved worlds. Back up world data before upgrading and review [S3-CHANGES.md](S3-CHANGES.md). The server runs in offline/authentication-disabled mode and should remain on a trusted local network.

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

The placeholder registry data is **not suitable for production game content**. GitHub Actions now generates real registries and compiles ESP-IDF firmware as well as running host tests, but CI cannot validate physical Wi-Fi connectivity, PSRAM behavior, flash persistence, or on-device performance.

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
