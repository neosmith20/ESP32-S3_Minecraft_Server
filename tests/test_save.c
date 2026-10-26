// PC test for saving block changes to the world file in pages.
// Build with -DDISK_SYNC_BLOCKS_ON_INTERVAL so it uses the same path as the ESP32.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "worldgen.h"
#include "procedures.h"
#include "serialize.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails ++; } else printf("ok:   %s\n", msg); } while (0)

static int fileMatchesMemory (void) {
  FILE *f = fopen("world2.bin", "rb");
  if (!f) return 0;
  static BlockChange copy[MAX_BLOCK_CHANGES];
  size_t r = fread(copy, 1, sizeof(copy), f);
  fclose(f);
  return r == sizeof(copy) && memcmp(copy, block_changes, sizeof(copy)) == 0;
}

int main (void) {
  remove("world2.bin");
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = BLOCK_NONE;
  block_changes_count = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) { client_states[i * 2] = -1; player_data[i].client_fd = -1; }
  initTerrainCache();
  CHECK(initSerializer() == 0, "world file created");
  rebuildBlockChangeIndex();

  // Make changes spread over the whole list (different pages)
  for (int i = 0; i < 300; i ++) makeBlockChange(i * 3, 70 + (i % 5), i * 7 - 500, B_torch);
  for (int i = 0; i < 60; i ++) makeBlockChange(i, 90, 1000 + i, B_gold_block);
  CHECK(!fileMatchesMemory(), "nothing is written to flash before a save");
  int w = flushDirtyBlockPages(1);
  CHECK(w == 1, "one page written when only one is allowed");
  while (flushDirtyBlockPages(2) > 0);
  CHECK(fileMatchesMemory(), "after saving, the file matches the world exactly");
  CHECK(flushDirtyBlockPages(10) == 0, "nothing left to save");

  // Later changes, including removing a block
  makeBlockChange(3, 71, -497, B_air);
  makeBlockChange(5000, 80, 5000, B_diamond_block);
  CHECK(flushDirtyBlockPages(1000) >= 1, "new changes are written");
  CHECK(fileMatchesMemory(), "file matches again after more changes");

  // Reload as the server would after a restart
  static BlockChange before[MAX_BLOCK_CHANGES];
  memcpy(before, block_changes, sizeof(before));
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = BLOCK_NONE;
  block_changes_count = 0;
  CHECK(initSerializer() == 0, "world file loads");
  CHECK(memcmp(before, block_changes, sizeof(before)) == 0, "world after restart equals the world before");
  rebuildBlockChangeIndex();
  CHECK(getBlockAt(5000, 80, 5000) == B_diamond_block, "a block placed just before the restart is still there");

  printf("\n%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL SAVE TESTS PASSED", fails);
  return fails != 0;
}
