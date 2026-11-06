# Audit: this server versus vanilla Minecraft Java 1.21.8

Status labels: **Works** (covered by tests), **Partial**, **Missing**. Anything about real speed, flash timing or WiFi is
marked **needs the physical board** because the emulator and PC tests cannot measure it.

## Works
- Login, configuration, chunks (real 1.21.8 block states), movement, chat, multiplayer.
- Mining/placing, drops, tool wear, crafting, doors/trapdoors/gates, stairs/slabs, beds (respawn + night skip), chests.
- Water/lava flow, buckets, crop and sapling growth, grass spread and die-back, short grass, trees, leaves dropping saplings.
- Falling sand/gravel (new): unsupported sand lands on the next solid block instantly (no falling animation).
- Mobs: passive animals (follow wheat, breed, grow), zombies with line-of-reach checks for doors/walls.
- Saving: paged world file, 15 s journal, player snapshot (players.bin) and bed file (beds.bin), crash/power-cut tests.

## Partial
- Mobs: only a few types; no skeletons or creepers.
- Fluids: simplified flow rules.
- Falling blocks have no animation or damage.

## Missing (not feasible or not worth the RAM/CPU on an ESP32-S3)
Redstone, fire, TNT, XP/enchanting, villagers/trading, nether/end, potions, maps, elytra, advancements.

## Measured (emulator, NOT real hardware)
Internal RAM min free about 176 KB, PSRAM free about 3.59 MB with two players and farming; no watchdog or crash markers.
The emulator runs about 0.3x real time, so its timing says nothing about the board.

## Needs the physical board
Save latency and any lag while saving, real flash timing, WiFi throughput, player capacity, tick timing under load.

## Risks
- Saving lag is the main regression risk: periodic saves must stay small (journal and players.bin only; no full player write).
- DEBUG_MOB_HITS chat diagnostic is still enabled until the zombie-through-door report is resolved.
