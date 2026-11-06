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

## Security review of packet handling (done)
Fixed: a failed string read could write one byte before the buffer; broken string lengths could make the server loop for
billions of steps; inventory clicks accepted invalid slots (writing outside the player data, and through the chest pointer
when no chest was open), any item count, and an unbounded number of changes; the whisper command could overflow the
receive buffer; the handshake accepted "play" as the next state (skipping login); an all-zero UUID skipped player setup;
respawn worked while alive (free full heal); "player loaded" could be repeated; player names could hold control characters;
block, use-item and attack packets had no reach limit; impossible movement numbers (NaN, huge) were converted to integers;
huge teleports made the server send hundreds of chunks; a client sending half a packet froze the whole server for 15 s at a
time (now: packets are only read once complete, half packets are dropped after 5 s); packet length was unchecked.
Tests: tests/test_security.c (run with address/undefined-behavior sanitizers), tests/run_abuse.py (live hostile connections).
Known and left as is: the server has no online login, so a UUID/name can be claimed by anyone (same as any offline-mode
server); inventory contents are still client-trusted apart from slot/count limits (full survival-inventory validation does not
fit this design); no speed/fly detection; sends to a client that stops reading can still stall for up to 15 s.
Pre-existing quirks noted but not changed: the chest storage pointer trick reads 16-bit values at odd addresses (works on the
board per earlier testing); one world-generation shift uses a negative count on some inputs (changing it would alter existing worlds).
