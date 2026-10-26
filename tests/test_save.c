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
  remove("world2.bin"); remove("journal.bin");
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
  CHECK(flushDirtyBlockPages(1000) >= 1, "full save writes new changes");
  CHECK(fileMatchesMemory(), "file matches again after more changes");

  // Journal: quick saves of recent changes, no rewrite of the world file
  makeBlockChange(7000, 66, 7000, B_gold_block);
  makeBlockChange(7001, 66, 7000, B_torch);
  makeBlockChange(7000, 66, 7000, B_air);
  flushBlockJournal();
  CHECK(!fileMatchesMemory(), "journal save does not touch the world file");
  FILE *jf = fopen("journal.bin", "rb");
  long jsize = 0;
  if (jf) { fseek(jf, 0, SEEK_END); jsize = ftell(jf); fclose(jf); }
  CHECK(jsize > 0 && jsize < 200, "journal is tiny (only a few bytes per change)");
  makeBlockChange(7002, 66, 7000, B_stone);
  flushBlockJournal();

  // Simulate a restart without a full save: the journal must restore everything
  static BlockChange before[MAX_BLOCK_CHANGES];
  memcpy(before, block_changes, sizeof(before));
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = BLOCK_NONE;
  block_changes_count = 0;
  CHECK(initSerializer() == 0, "world file loads with a journal");
  CHECK(memcmp(before, block_changes, sizeof(before)) == 0, "journal restores the world exactly");
  rebuildBlockChangeIndex();
  CHECK(getBlockAt(7001, 66, 7000) == B_torch, "block saved only in the journal is back");
  CHECK(getBlockAt(7000, 66, 7000) != B_gold_block, "a block removed after being placed stays removed");
  CHECK(getBlockAt(5000, 80, 5000) == B_diamond_block, "block from the full save is still there");

  // Full save merges the journal and removes it
  CHECK(flushDirtyBlockPages(1000) >= 1, "journal contents are merged into the world file");
  CHECK(fileMatchesMemory(), "world file now holds everything");
  CHECK(fopen("journal.bin", "rb") == NULL, "journal removed after merge");

  // Torn last record (power loss while writing) is ignored
  makeBlockChange(8000, 66, 8000, B_gold_block);
  flushBlockJournal();
  jf = fopen("journal.bin", "ab"); fwrite("abc", 1, 3, jf); fclose(jf);
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = BLOCK_NONE;
  block_changes_count = 0;
  CHECK(initSerializer() == 0, "loads with a damaged journal end");
  rebuildBlockChangeIndex();
  CHECK(getBlockAt(8000, 66, 8000) == B_gold_block, "complete records before the damage are kept");

  // Many changes at once fall back to a full save instead of losing anything
  for (int i = 0; i < 1500; i ++) makeBlockChange(i, 100, 3000, B_stone);
  flushBlockJournal();
  CHECK(fileMatchesMemory(), "overflow triggers a full save");

  printf("\n%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL SAVE TESTS PASSED", fails);
  return fails != 0;
}
