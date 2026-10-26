# Releases and development build history

This public repository publishes a **new, immutable version tag and GitHub pre-release for each successfully completed `main`-branch CI run**. Only releases with a complete firmware build and passing native regression and packet tests are published.

## Automated development releases

- Workflow: `.github/workflows/esp32-ci.yml`
- Version convention: `v0.1.0-dev.<run-number>` (the series comes from `release-series.txt`).
- A release is generated **only for pushes to `main`**, never from unmerged pull requests.
- Rapid successive pushes can cancel superseded runs; those cancelled runs do not get releases.
- `github.run_number` is monotonically increasing per workflow. Skipped numbers are normal (for PR builds or failed builds).
- Release names include the Minecraft compatibility version separately (currently Java **1.21.8**).
- Every development version is a **pre-release**. No automation promotes a dev build to stable.

Every release contains a downloadable ZIP with:
- `bootloader.bin`, `partitions.bin` and `firmware.bin`
- `flasher_args.json` with portable paths and **offsets from the actual ESP-IDF build**
- `FLASHING.md`, source revision / hardware `manifest.json`, GPL license and `SHA256SUMS.txt`
- A separate `.zip.sha256` for verifying the ZIP itself

GitHub separately offers its standard source ZIP/tar.gz files for each tag. The Minecraft server JAR and Wi-Fi credentials are **not** included in release assets.

## How to download

Open [Releases](https://github.com/neosmith20/ESP32-S3_Minecraft_Server/releases), choose the pre-release for the commit you want, and expand **Assets**. Download the **versioned ESP32-S3 firmware ZIP**, not GitHub's automatic **Source code (zip)**. Extract it, open that folder in **Visual Studio Code**, and follow the bundled `FLASHING.md` using the **VS Code integrated terminal** and Python esptool. PlatformIO's Build/Upload buttons **cannot** be used directly on the prebuilt release ZIP, because it has no `platformio.ini`.

**Current limitation:** the prebuilt firmware has hardcoded placeholder Wi-Fi credentials and will not normally connect to your network. To run your own Wi-Fi Minecraft server, download the **main-branch source ZIP** and follow the complete **[VS Code + PlatformIO walkthrough in the README](README.md#option-b--download-the-source-zip-and-buildupload-with-vs-code--platformio)**; generate registries, configure Wi-Fi locally, build, and upload. Never push Wi-Fi secrets to GitHub.

Firmware is experimental. Building is not the same as successfully booting and operating on physical ESP32-S3 hardware. Target board is N16R8 (16 MB flash, 8 MB PSRAM), not other configurations.

## Milestones

Update `release-series.txt` when starting a new development milestone, such as `0.2.0`; commits then become `v0.2.0-dev.N`. A future Minecraft protocol update may warrant `2.0.0` or `3.0.0` depending on compatibility changes. Update all relevant Minecraft registry generation and documentation alongside a version change.

**Version 1.0.0 is a deliberately chosen hardware-tested milestone.** Publish `v1.0.0` manually only after testing on a real board. The automated workflow **never labels a build stable**.

## Notes for maintainers

- Full CI must pass before publication: real registry generation, host unit/E2E tests, firmware compilation, and packaging.
- Clang-Tidy findings are currently *advisory* until the inherited-code baseline has been triaged; do not interpret successful CI as a clean security audit.
- The legacy Cosmopolitan desktop workflow is manual-only and cannot publish the `latest` release. Desktop executables, if reinstated, should use separate named assets and a working pipeline.
- Publishing uses the built-in `GITHUB_TOKEN` with `contents: write` only in the release job, not in PR builds.
- For public repository downloads and vulnerability review, see GitHub's Security tab and development notes. Production deployment on an untrusted public network is not recommended.
