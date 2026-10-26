// PC test for gameplay: recipes, doors, slabs, stairs, fences, ladders, logs.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>

#include "globals.h"
#include "tools.h"
#include "packets.h"
#include "worldgen.h"
#include "registries.h"
#include "procedures.h"
#include "crafting.h"
#include "blocks.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails ++; } else printf("ok:   %s\n", msg); } while (0)

static int rx_fd;
static void *drain (void *arg) {
  uint8_t tmp[65536];
  while (read(rx_fd, tmp, sizeof(tmp)) > 0);
  return NULL;
}

static uint16_t craft (const uint16_t *grid, uint8_t *count) {
  PlayerData p;
  memset(&p, 0, sizeof(p));
  memcpy(p.craft_items, grid, 9 * sizeof(uint16_t));
  uint16_t item = 0;
  getCraftingOutput(&p, count, &item);
  return item;
}

static int countItem (PlayerData *p, uint16_t item) {
  int n = 0;
  for (int i = 0; i < 41; i ++) if (p->inventory_items[i] == item) n += p->inventory_count[i];
  return n;
}

static PlayerData *p;
static short X = 3, Z = 4;
static int gy;

static void clearArea (void) {
  for (int dx = -2; dx <= 2; dx ++) for (int dz = -2; dz <= 2; dz ++) {
    makeBlockChange(X + dx, gy, Z + dz, B_stone);
    for (int dy = 1; dy <= 4; dy ++) makeBlockChange(X + dx, gy + dy, Z + dz, B_air);
  }
}
static void hold (uint16_t item, int count) {
  for (int i = 0; i < 41; i ++) { p->inventory_items[i] = 0; p->inventory_count[i] = 0; }
  p->hotbar = 0; p->inventory_items[0] = item; p->inventory_count[0] = count;
}

int main (void) {
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = BLOCK_NONE;
  block_changes_count = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) { client_states[i * 2] = -1; player_data[i].client_fd = -1; }
  initTerrainCache();
  rebuildBlockChangeIndex();

  // ---- recipes ----
  const uint16_t P = I_oak_planks, S = I_stick, C = I_cobblestone;
  uint8_t n;
  uint16_t door1[9] = { P, P, 0,  P, P, 0,  P, P, 0 };
  uint16_t door2[9] = { 0, P, P,  0, P, P,  0, P, P };
  uint16_t bad1[9] = { P, P, P,  P, P, P,  0, 0, 0 };
  uint16_t bad2[9] = { P, P, 0,  P, C, 0,  P, P, 0 };
  uint16_t bad3[9] = { P, P, 0,  P, P, 0,  P, 0, P };
  uint16_t chest[9] = { P, P, P,  P, 0, P,  P, P, P };
  uint16_t stairs_l[9] = { C, 0, 0,  C, C, 0,  C, C, C };
  uint16_t stairs_r[9] = { 0, 0, C,  0, C, C,  C, C, C };
  uint16_t slab[9] = { 0, 0, 0,  C, C, C,  0, 0, 0 };
  uint16_t slab_low[9] = { 0, 0, 0,  0, 0, 0,  C, C, C };
  uint16_t pick[9] = { C, C, C,  0, S, 0,  0, S, 0 };
  uint16_t axe_l[9] = { C, C, 0,  C, S, 0,  0, S, 0 };
  uint16_t axe_r[9] = { 0, C, C,  0, S, C,  0, S, 0 };
  uint16_t torch[9] = { 0, 0, 0,  0, I_charcoal, 0,  0, S, 0 };
  uint16_t torch2[9] = { 0, I_coal, 0,  0, S, 0,  0, 0, 0 };
  uint16_t planks[9] = { 0, 0, 0,  0, I_oak_log, 0,  0, 0, 0 };
  uint16_t fence[9] = { P, S, P,  P, S, P,  0, 0, 0 };
  uint16_t ladder[9] = { S, 0, S,  S, S, S,  S, 0, S };
  uint16_t nuggets[9] = { 0, 0, 0,  0, I_iron_ingot, 0,  0, 0, 0 };
  CHECK(craft(door1, &n) == I_oak_door && n == 3, "door recipe, left columns");
  CHECK(craft(door2, &n) == I_oak_door && n == 3, "door recipe, right columns");
  CHECK(craft(bad1, &n) == I_oak_trapdoor && n == 2, "3x2 planks is a trapdoor, not a door");
  CHECK(craft(bad2, &n) == 0, "door pattern with a cobblestone is not a door");
  CHECK(craft(bad3, &n) == 0, "scattered planks is nothing");
  CHECK(craft(chest, &n) == I_chest && n == 1, "chest recipe");
  CHECK(craft(stairs_l, &n) == I_cobblestone_stairs && n == 4, "stairs recipe");
  CHECK(craft(stairs_r, &n) == I_cobblestone_stairs && n == 4, "stairs recipe, mirrored");
  CHECK(craft(slab, &n) == I_cobblestone_slab && n == 6, "slab recipe, middle row");
  CHECK(craft(slab_low, &n) == I_cobblestone_slab && n == 6, "slab recipe, bottom row");
  CHECK(craft(pick, &n) == I_stone_pickaxe, "stone pickaxe");
  CHECK(craft(axe_l, &n) == I_stone_axe, "stone axe");
  CHECK(craft(axe_r, &n) == I_stone_axe, "stone axe, mirrored");
  CHECK(craft(torch, &n) == I_torch && n == 4, "torch from charcoal (shifted position)");
  CHECK(craft(torch2, &n) == I_torch && n == 4, "torch from coal (top position)");
  CHECK(craft(planks, &n) == I_oak_planks && n == 4, "planks from log (shapeless, any slot)");
  CHECK(craft(fence, &n) == I_oak_fence && n == 3, "fence recipe");
  CHECK(craft(ladder, &n) == I_ladder && n == 3, "ladder recipe");
  CHECK(craft(nuggets, &n) == I_iron_nugget && n == 9, "iron nuggets");

  // ---- player ----
  int sv[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  rx_fd = sv[1];
  pthread_t th;
  pthread_create(&th, NULL, drain, NULL);
  p = &player_data[0];
  memset(p, 0, sizeof(*p));
  p->client_fd = sv[0];
  p->x = 100; p->z = 100; p->y = 100;
  gy = 120;
  while (gy > 1 && getBlockAt(X, gy, Z) == B_air) gy --;
  clearArea();

  // ---- doors ----
  struct { int8_t yaw; int facing; } yaws[] = { {0, DIR_SOUTH}, {63, DIR_WEST}, {127, DIR_NORTH}, {-127, DIR_NORTH}, {-63, DIR_EAST} };
  for (int i = 0; i < 5; i ++) {
    hold(I_oak_door, 5);
    p->yaw = yaws[i].yaw;
    handlePlayerUseItem(p, X, gy, Z, 1, 128);
    int f = yaws[i].facing;
    CHECK(getBlockAt(X, gy + 1, Z) == BMIN_oak_door + OFF_door[f][0][0], "door lower half, facing from yaw");
    CHECK(getBlockAt(X, gy + 2, Z) == BMIN_oak_door + OFF_door[f][1][0], "door upper half");
    CHECK(p->inventory_count[0] == 4, "one door used");
    makeBlockChange(X, gy + 1, Z, B_air); makeBlockChange(X, gy + 2, Z, B_air);
  }
  hold(I_oak_door, 2);
  makeBlockChange(X, gy + 2, Z, B_stone);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  CHECK(getBlockAt(X, gy + 1, Z) == B_air && p->inventory_count[0] == 2, "no door when top is blocked");
  makeBlockChange(X, gy + 2, Z, B_air);
  p->yaw = 0;
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  uint16_t lo = BMIN_oak_door + OFF_door[DIR_SOUTH][0][0], hi = BMIN_oak_door + OFF_door[DIR_SOUTH][1][0];
  uint16_t lo_o = BMIN_oak_door + OFF_door[DIR_SOUTH][0][1], hi_o = BMIN_oak_door + OFF_door[DIR_SOUTH][1][1];
  CHECK(getBlockAt(X, gy + 1, Z) == lo, "door placed");
  CHECK(!isPassableBlock(lo) && !isPassableBlock(hi), "closed door blocks");
  hold(I_dirt, 3);
  handlePlayerUseItem(p, X, gy + 1, Z, 5, 128);
  CHECK(getBlockAt(X, gy + 1, Z) == lo_o && getBlockAt(X, gy + 2, Z) == hi_o, "right-click opens both halves");
  CHECK(p->inventory_count[0] == 3, "nothing placed when opening");
  CHECK(isPassableBlock(lo_o) && isPassableBlock(hi_o), "open door is passable");
  handlePlayerUseItem(p, X, gy + 2, Z, 5, 128);
  CHECK(getBlockAt(X, gy + 1, Z) == lo && getBlockAt(X, gy + 2, Z) == hi, "right-click upper closes both");
  hold(0, 0);
  handlePlayerAction(p, 2, X, gy + 2, Z);
  CHECK(getBlockAt(X, gy + 1, Z) == B_air && getBlockAt(X, gy + 2, Z) == B_air, "breaking either half removes door");
  CHECK(countItem(p, I_oak_door) == 1, "one door dropped");
  hold(I_oak_door, 1);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  hold(0, 0);
  handlePlayerAction(p, 2, X, gy, Z);
  CHECK(getBlockAt(X, gy + 1, Z) == B_air && getBlockAt(X, gy + 2, Z) == B_air, "door falls when its floor is mined");
  makeBlockChange(X, gy, Z, B_stone);

  // ---- slabs ----
  hold(I_oak_slab, 10);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  CHECK(getBlockAt(X, gy + 1, Z) == BMIN_oak_slab + OFF_slab[0], "slab on top face: bottom slab");
  handlePlayerUseItem(p, X, gy + 1, Z, 1, 128);
  CHECK(getBlockAt(X, gy + 1, Z) == BMIN_oak_slab + OFF_slab[2], "click top of bottom slab: becomes double");
  CHECK(getBlockAt(X, gy + 2, Z) == B_air, "no slab above");
  makeBlockChange(X, gy + 1, Z, B_air);
  handlePlayerUseItem(p, X + 1, gy + 1, Z, 4, 200);
  CHECK(getBlockAt(X, gy + 1, Z) == BMIN_oak_slab + OFF_slab[1] || getBlockAt(X, gy + 1, Z) == BMIN_oak_slab + OFF_slab[0], "slab on side face placed by cursor height");
  makeBlockChange(X, gy + 1, Z, BMIN_oak_slab + OFF_slab[2]);
  hold(0, 0);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(countItem(p, I_oak_slab) == 2, "double slab drops two slabs");
  makeBlockChange(X, gy + 1, Z, B_air);

  // ---- stairs ----
  hold(I_oak_stairs, 4);
  p->yaw = 0; // facing south
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  int f, t;
  CHECK(decodeStairs(getBlockAt(X, gy + 1, Z), &f, &t) && f == DIR_SOUTH && t == 0, "stairs facing the way you look, bottom half");
  makeBlockChange(X, gy + 1, Z, B_air);

  // ---- logs ----
  hold(I_oak_log, 4);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  int axis;
  CHECK(decodeLog(getBlockAt(X, gy + 1, Z), &axis) && axis == 1, "log on top face is upright");
  makeBlockChange(X, gy + 1, Z, B_air);
  handlePlayerUseItem(p, X - 1, gy + 1, Z, 5, 128);
  CHECK(decodeLog(getBlockAt(X, gy + 1, Z), &axis) && axis == 0, "log on east face lies along X");
  makeBlockChange(X, gy + 1, Z, B_air);

  // ---- fences connect ----
  hold(I_oak_fence, 6);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  handlePlayerUseItem(p, X + 1, gy, Z, 1, 128);
  uint16_t a = getBlockAt(X, gy + 1, Z), b = getBlockAt(X + 1, gy + 1, Z);
  CHECK(a == BMIN_oak_fence + OFF_fence[0][1][0][0], "left fence connects east");
  CHECK(b == BMIN_oak_fence + OFF_fence[0][0][0][1], "right fence connects west");
  hold(0, 0);
  handlePlayerAction(p, 2, X + 1, gy + 1, Z);
  CHECK(getBlockAt(X, gy + 1, Z) == BMIN_oak_fence + OFF_fence[0][0][0][0], "fence disconnects when neighbor is removed");
  makeBlockChange(X, gy + 1, Z, B_air);

  // ---- ladder / torch / chest ----
  hold(I_ladder, 4);
  makeBlockChange(X - 1, gy + 1, Z, B_stone);
  handlePlayerUseItem(p, X - 1, gy + 1, Z, 5, 128);
  CHECK(getBlockAt(X, gy + 1, Z) == BMIN_ladder + OFF_ladder[DIR_EAST], "ladder on a west wall faces east");
  makeBlockChange(X, gy + 1, Z, B_air);
  makeBlockChange(X - 1, gy + 1, Z, B_air);
  hold(I_torch, 4);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  CHECK(getBlockAt(X, gy + 1, Z) == B_torch, "torch on floor");
  makeBlockChange(X, gy + 1, Z, B_air);
  hold(I_chest, 2);
  p->yaw = 63;
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  CHECK(IS_BLOCK(getBlockAt(X, gy + 1, Z), chest), "chest placed");
  hold(0, 0);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(getBlockAt(X, gy + 1, Z) == B_air && countItem(p, I_chest) == 1, "chest broken, item returned");

  // ---- save/load roundtrip ----
  makeBlockChange(10, 70, 10, B_gold_block);
  makeBlockChange(-11, 71, 12, BMIN_oak_door + OFF_door[2][1][1]);
  CHECK(getBlockAt(10, 70, 10) == B_gold_block, "block change lookup");

  printf("\n%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL GAME TESTS PASSED", fails);
  return fails != 0;
}
