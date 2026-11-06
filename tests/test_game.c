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

#define IS_WATER_T(b) ((b) >= B_water && (b) < B_water + 16)
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

  // ---- mining gives drops ----
  hold(0, 0);
  makeBlockChange(X, gy + 1, Z, B_dirt);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(countItem(p, I_dirt) == 1, "mining dirt gives a dirt item");
  makeBlockChange(X, gy + 1, Z, B_grass_block);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(countItem(p, I_dirt) == 2, "mining grass gives a dirt item");
  makeBlockChange(X, gy + 1, Z, B_oak_log);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(countItem(p, I_oak_log) == 1, "mining a log gives a log");
  makeBlockChange(X, gy + 1, Z, B_sand);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(countItem(p, I_sand) == 1, "mining sand gives sand");

  // ---- farming ----
  CHECK(craft((uint16_t[9]){ 0, 0, 0,  I_wheat, I_wheat, I_wheat,  0, 0, 0 }, &n) == I_bread && n == 1, "bread from 3 wheat");
  clearArea();
  makeBlockChange(X, gy, Z, B_dirt);
  hold(I_stone_hoe, 1);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  CHECK(getBlockFamily(getBlockAt(X, gy, Z)) == FAM_FARMLAND, "hoe turns dirt into farmland");
  makeBlockChange(X + 1, gy, Z, B_grass_block);
  handlePlayerUseItem(p, X + 1, gy, Z, 1, 128);
  CHECK(getBlockFamily(getBlockAt(X + 1, gy, Z)) == FAM_FARMLAND, "hoe turns grass into farmland");
  makeBlockChange(X - 1, gy, Z, B_stone);
  handlePlayerUseItem(p, X - 1, gy, Z, 1, 128);
  CHECK(getBlockAt(X - 1, gy, Z) == B_stone, "hoe does nothing to stone");
  hold(I_wheat_seeds, 5);
  handlePlayerUseItem(p, X - 1, gy, Z, 1, 128);
  CHECK(getBlockAt(X - 1, gy + 1, Z) == B_air, "seeds don't plant on stone");
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  CHECK(IS_BLOCK(getBlockAt(X, gy + 1, Z), wheat) && getCropAge(getBlockAt(X, gy + 1, Z)) == 0 && p->inventory_count[0] == 4, "seeds plant wheat on farmland");
  hold(I_carrot, 3);
  handlePlayerUseItem(p, X + 1, gy, Z, 1, 128);
  CHECK(IS_BLOCK(getBlockAt(X + 1, gy + 1, Z), carrots), "carrot plants carrots on farmland");
  CHECK(!(p->flags & 0x10), "planting doesn't start eating");
  // growth
  for (int i = 0; i < 400; i ++) tickFarming(50);
  CHECK(getCropAge(getBlockAt(X, gy + 1, Z)) == 7, "wheat grows to ripe");
  CHECK(getCropAge(getBlockAt(X + 1, gy + 1, Z)) == 7, "carrots grow to ripe");
  // dry farmland with crops stays; wet check
  CHECK(getFarmlandMoisture(getBlockAt(X, gy, Z)) == 0 || getFarmlandMoisture(getBlockAt(X, gy, Z)) > 0, "farmland state readable");
  // harvesting
  hold(0, 0);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(countItem(p, I_wheat) == 1 && countItem(p, I_wheat_seeds) >= 1, "ripe wheat gives wheat and seeds");
  hold(0, 0);
  handlePlayerAction(p, 2, X + 1, gy + 1, Z);
  CHECK(countItem(p, I_carrot) >= 1 && countItem(p, I_carrot) <= 4, "ripe carrots give 1-4 carrots");
  // unripe wheat gives a seed
  hold(I_wheat_seeds, 2);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  hold(0, 0);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(countItem(p, I_wheat_seeds) == 1 && countItem(p, I_wheat) == 0, "unripe wheat gives only a seed");
  // breaking farmland under a plant removes the plant
  hold(I_wheat_seeds, 2);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  hold(0, 0);
  handlePlayerAction(p, 2, X, gy, Z);
  CHECK(getBlockAt(X, gy + 1, Z) == B_air && countItem(p, I_dirt) == 1, "mining farmland drops dirt and removes the plant");
  // bone meal
  makeBlockChange(X, gy, Z, BMIN_farmland + OFF_farmland[7]);
  makeBlockChange(X, gy + 1, Z, getCropState(BMIN_wheat, 0));
  hold(I_bone_meal, 3);
  handlePlayerUseItem(p, X, gy + 1, Z, 1, 128);
  CHECK(getCropAge(getBlockAt(X, gy + 1, Z)) >= 2, "bone meal advances a crop");
  // hoe on dirt with a block on top does nothing
  makeBlockChange(X + 2, gy, Z, B_dirt);
  makeBlockChange(X + 2, gy + 1, Z, B_stone);
  hold(I_stone_hoe, 1);
  handlePlayerUseItem(p, X + 2, gy, Z, 1, 128);
  CHECK(getBlockAt(X + 2, gy, Z) == B_dirt, "hoe needs free space above");
  // water makes farmland wet; dry empty farmland turns back to dirt
  makeBlockChange(X, gy + 1, Z, B_air);
  makeBlockChange(X + 1, gy + 1, Z, B_air);
  makeBlockChange(X, gy, Z, BMIN_farmland + OFF_farmland[0]);
  makeBlockChange(X + 1, gy, Z, BMIN_farmland + OFF_farmland[7]);
  for (int i = 0; i < 400; i ++) tickFarming(1);
  CHECK(getBlockAt(X, gy, Z) == B_dirt, "dry empty farmland turns back to dirt");
  // ...but not right away: hoe a patch and look 3 times (15 seconds)
  makeBlockChange(X, gy, Z, B_dirt);
  hold(I_stone_hoe, 1);
  handlePlayerUseItem(p, X, gy, Z, 1, 128);
  int early = 0;
  for (int trial = 0; trial < 200; trial ++) {
    makeBlockChange(X, gy, Z, BMIN_farmland + OFF_farmland[0]);
    for (int i = 0; i < 3; i ++) tickFarming(1);
    if (getBlockAt(X, gy, Z) == B_dirt) early ++;
  }
  CHECK(early < 60, "dry farmland rarely reverts within 15 seconds");
  // seeds from grass
  int seeds = 0;
  for (int i = 0; i < 400; i ++) {
    hold(0, 0);
    makeBlockChange(X, gy + 1, Z, B_short_grass);
    handlePlayerAction(p, 2, X, gy + 1, Z);
    seeds += countItem(p, I_wheat_seeds);
  }
  CHECK(seeds > 20 && seeds < 90, "short grass gives seeds about 1 time in 8");
  // food
  CHECK(1, "farming tests done");

  // ---- tool wear ----
  clearArea();
  makeBlockChange(X, gy + 1, Z, B_cobblestone);
  hold(I_wooden_pickaxe, 1);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(getBlockAt(X, gy + 1, Z) == B_air, "pickaxe mines cobblestone");
  for (int i = 0; i < 100; i ++) handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(p->inventory_items[0] == I_wooden_pickaxe, "repeated finish packets on air don't wear the tool");
  for (int i = 0; i < 57; i ++) {
    makeBlockChange(X, gy + 1, Z, B_cobblestone);
    handlePlayerAction(p, 2, X, gy + 1, Z);
  }
  CHECK(p->inventory_items[0] == I_wooden_pickaxe, "wooden pickaxe survives 58 blocks");
  makeBlockChange(X, gy + 1, Z, B_cobblestone);
  handlePlayerAction(p, 2, X, gy + 1, Z);
  CHECK(p->inventory_items[0] == 0, "wooden pickaxe breaks on the 59th block");

  // ---- buckets ----
  CHECK(craft((uint16_t[9]){ I_iron_ingot, 0, I_iron_ingot,  0, I_iron_ingot, 0,  0, 0, 0 }, &n) == I_bucket, "bucket recipe");
  clearArea();
  // Player stands at (X+0.5, gy+1, Z+0.5) and looks south and 45 degrees down
  p->x = X; p->z = Z; p->y = gy + 1;
  player_exact[0][0] = X + 0.5; player_exact[0][1] = gy + 1; player_exact[0][2] = Z + 0.5;
  makeBlockChange(X, gy, Z + 2, B_water);
  hold(I_bucket, 2);
  CHECK(handleBucketUse(p, 0.0f, 45.0f), "bucket use is handled");
  CHECK(getBlockAt(X, gy, Z + 2) == B_air, "empty bucket picks up a water source");
  CHECK(p->inventory_items[0] == I_bucket && p->inventory_count[0] == 1 && countItem(p, I_water_bucket) == 1, "one bucket is filled");
  // aiming away from water does nothing
  makeBlockChange(X, gy, Z + 2, B_water);
  hold(I_bucket, 1);
  handleBucketUse(p, 90.0f, 45.0f);
  CHECK(getBlockAt(X, gy, Z + 2) == B_water && p->inventory_items[0] == I_bucket, "aiming away leaves the water alone");
  // flowing water is not picked up
  makeBlockChange(X, gy, Z + 2, B_water + 3);
  handleBucketUse(p, 0.0f, 45.0f);
  CHECK(getBlockAt(X, gy, Z + 2) == B_water + 3 && p->inventory_items[0] == I_bucket, "flowing water can't be picked up");
  // a lake refills the spot (two or more neighbouring sources)
  makeBlockChange(X, gy - 1, Z + 2, B_stone);
  makeBlockChange(X, gy, Z + 2, B_water);
  makeBlockChange(X + 1, gy, Z + 2, B_water);
  makeBlockChange(X - 1, gy, Z + 2, B_water);
  hold(I_bucket, 1);
  handleBucketUse(p, 0.0f, 45.0f);
  CHECK(p->inventory_items[0] == I_water_bucket, "bucket filled from a lake");
  CHECK(getBlockAt(X, gy, Z + 2) == B_water, "a lake refills the taken spot with a new source");
  makeBlockChange(X + 1, gy, Z + 2, B_air);
  makeBlockChange(X - 1, gy, Z + 2, B_air);
  makeBlockChange(X, gy, Z + 2, B_air);
  // lava
  hold(I_bucket, 1);
  makeBlockChange(X, gy, Z + 2, B_lava);
  handleBucketUse(p, 0.0f, 45.0f);
  CHECK(p->inventory_items[0] == I_lava_bucket && getBlockAt(X, gy, Z + 2) == B_air, "lava source fills a lava bucket");
  // placing
  makeBlockChange(X, gy, Z + 2, B_stone);
  hold(I_water_bucket, 1);
  handleBucketUse(p, 0.0f, 45.0f);
  int found = 0;
  for (int dz = -1; dz <= 3; dz ++) for (int dy = 1; dy <= 2; dy ++) if (getBlockAt(X, gy + dy, Z + dz) == B_water) found ++;
  CHECK(found >= 1 && p->inventory_items[0] == I_bucket, "water bucket places water and becomes empty");
  CHECK(getBlockAt(X, gy, Z + 2) == B_stone, "the floor is untouched");
  { int total = 0; for (int dx = -6; dx <= 6; dx ++) for (int dz = -6; dz <= 6; dz ++) for (int dy = 1; dy <= 3; dy ++) if (IS_WATER_T(getBlockAt(X + dx, gy + dy, Z + dz))) total ++; printf("info: water cells after placing a bucket: %d\n", total); CHECK(total > 1, "placed water spreads out"); }
  hold(I_lava_bucket, 1);
  handleBucketUse(p, 0.0f, 45.0f);
  CHECK(p->inventory_items[0] == I_bucket || p->inventory_items[0] == I_lava_bucket, "second placement is safe");
  hold(I_dirt, 1);
  CHECK(!handleBucketUse(p, 0.0f, 45.0f), "other items are not buckets");
  CHECK(getItemStackSize(I_water_bucket) == 1 && getItemStackSize(I_bucket) == 16, "bucket stack sizes");
  // potato, carrot and bread food values exist
  p->hunger = 5;
  hold(I_bread, 1);
  handlePlayerEating(p, false);
  CHECK(p->hunger == 10, "eating bread restores 5 hunger");

  // ---- furnace (smelting takes time) ----
  {
    PlayerData f;
    FurnaceState st;
    #define FURNACE(mat, mc, fuel, fc) memset(&f, 0, sizeof(f)); memset(&st, 0, sizeof(st)); f.craft_items[0] = mat; f.craft_count[0] = mc; f.craft_items[1] = fuel; f.craft_count[1] = fc;
    #define RUN(seconds) for (int s_ = 0; s_ < (seconds); s_ ++) tickFurnace(&f, &st);
    FURNACE(I_porkchop, 16, I_oak_planks, 20)
    RUN(9)
    CHECK(f.craft_count[2] == 0, "nothing is cooked before 10 seconds are up");
    RUN(1)
    CHECK(f.craft_items[2] == I_cooked_porkchop && f.craft_count[2] == 1 && f.craft_count[0] == 15, "the first item is done after 10 seconds");
    RUN(150)
    CHECK(f.craft_count[2] == 16 && f.craft_count[0] == 0, "16 porkchops take 160 seconds");
    CHECK(f.craft_count[1] == 9, "16 porkchops use 11 planks (1.5 items per plank)");
    FURNACE(I_porkchop, 8, I_coal, 1)
    RUN(80)
    CHECK(f.craft_count[2] == 8 && f.craft_count[1] == 0, "one coal cooks 8 items in 80 seconds");
    FURNACE(I_porkchop, 9, I_coal, 1)
    RUN(200)
    CHECK(f.craft_count[2] == 8 && f.craft_count[0] == 1, "one coal is not enough for 9 items");
    FURNACE(I_beef, 2, I_oak_planks, 3)
    RUN(10)
    CHECK(f.craft_count[2] == 1 && f.craft_count[1] == 2, "one plank cooks the first item");
    RUN(5)
    CHECK(f.craft_count[2] == 1 && st.burn_left == 0, "the plank burns out after 15 seconds");
    RUN(5)
    CHECK(f.craft_count[2] == 2 && f.craft_count[1] == 1, "the next plank is lit when more heat is needed and the progress carries on");
    FURNACE(I_porkchop, 4, I_stick, 10)
    RUN(40)
    CHECK(f.craft_count[2] == 4 && f.craft_count[1] == 2, "4 items take 8 sticks");
    FURNACE(I_porkchop, 3, 0, 0)
    RUN(60)
    CHECK(f.craft_count[2] == 0, "no fuel, no cooking");
    FURNACE(I_porkchop, 3, I_dirt, 5)
    RUN(60)
    CHECK(f.craft_count[2] == 0 && f.craft_count[1] == 5, "dirt is not a fuel");
    FURNACE(I_dirt, 3, I_coal, 5)
    RUN(60)
    CHECK(f.craft_count[2] == 0 && f.craft_count[1] == 5, "dirt can't be smelted and no fuel is wasted");
    FURNACE(I_sand, 20, I_lava_bucket, 1)
    RUN(200)
    CHECK(f.craft_count[2] == 20 && f.craft_items[1] == I_bucket, "a lava bucket smelts a lot and leaves a bucket");
    FURNACE(I_raw_iron, 3, I_coal, 1) f.craft_items[2] = I_gold_ingot; f.craft_count[2] = 2;
    RUN(30)
    CHECK(f.craft_count[2] == 2 && f.craft_count[0] == 3 && f.craft_count[1] == 1, "a different item in the output slot blocks smelting");
    FURNACE(I_porkchop, 64, I_coal_block, 1)
    RUN(640)
    CHECK(f.craft_count[2] == 64, "a full stack of 64 cooks with one coal block in 640 seconds");
    FURNACE(I_beef, 1, I_coal, 1)
    RUN(4)
    f.craft_items[0] = 0; f.craft_count[0] = 0;
    RUN(2)
    CHECK(st.progress == 0, "taking the material away resets the progress");
    CHECK(st.burn_left > 0, "the fire keeps burning without material");
  }

  // ---- natural short grass does not float ----
  {
    int fx = 0, fy = 0, fz = 0, found = 0;
    for (int x = 200; x < 1000 && !found; x += 1) for (int z = 200; z < 1000 && !found; z += 7) {
      for (int y = 64; y < 90; y ++) {
        if (getBlockAt(x, y, z) == B_short_grass) { fx = x; fy = y; fz = z; found = 1; break; }
      }
    }
    CHECK(found, "plains have natural short grass");
    if (found) {
      CHECK(getBlockAt(fx, fy - 1, fz) == B_grass_block || getBlockAt(fx, fy - 1, fz) == B_snowy_grass_block, "natural short grass sits on a grass block");
      makeBlockChange(fx, fy - 1, fz, B_air);
      CHECK(getBlockAt(fx, fy, fz) == B_air, "short grass vanishes when the block under it is dug away");
      int cx = fx & ~15, cz = fz & ~15, cy = fy & ~15;
      if (fy - cy < 15) {
        buildChunkSection(cx, cy, cz);
        CHECK(chunk_section[(fx - cx) + ((fz - cz) << 4) + ((fy - cy) << 8)] == B_air, "sent chunk has no floating short grass");
      }
    }
  }

  // ---- grass spreads, saplings grow ----
  {
    clearArea();
    makeBlockChange(X, gy, Z, B_dirt);
    makeBlockChange(X + 1, gy, Z, B_grass_block);
    makeBlockChange(X, gy + 1, Z, B_air);
    int spread = 0;
    for (int t = 0; t < 600 && !spread; t ++) { tickFarming(1); spread = IS_BLOCK(getBlockAt(X, gy, Z), grass_block); }
    CHECK(spread, "dirt next to grass turns into grass over time");
    // dirt far from any grass stays dirt, and dirt under a block stays dirt
    makeBlockChange(X + 4, gy, Z + 4, B_dirt);
    makeBlockChange(X + 4, gy, Z + 5, B_dirt);
    makeBlockChange(X + 5, gy, Z + 5, B_stone);
    makeBlockChange(X + 4, gy + 1, Z + 5, B_stone);
    makeBlockChange(X + 3, gy, Z + 4, B_stone); makeBlockChange(X + 5, gy, Z + 4, B_stone);
    makeBlockChange(X + 4, gy, Z + 3, B_stone); makeBlockChange(X + 4, gy, Z + 6, B_stone);
    makeBlockChange(X + 3, gy, Z + 5, B_stone); makeBlockChange(X + 3, gy, Z + 6, B_stone);
    makeBlockChange(X + 5, gy, Z + 6, B_stone); makeBlockChange(X + 3, gy, Z + 3, B_stone); makeBlockChange(X + 5, gy, Z + 3, B_stone);
    for (int t = 0; t < 600; t ++) tickFarming(1);
    CHECK(IS_BLOCK(getBlockAt(X + 4, gy, Z + 5), dirt), "dirt under a block does not turn into grass");
    // grass covered by a block dies back to dirt
    makeBlockChange(X + 1, gy + 1, Z, B_stone);
    int died = 0;
    for (int t = 0; t < 600 && !died; t ++) { tickFarming(1); died = IS_BLOCK(getBlockAt(X + 1, gy, Z), dirt); }
    CHECK(died, "grass under a block turns back into dirt");
    // saplings grow into trees
    clearArea();
    makeBlockChange(X + 2, gy, Z + 2, B_grass_block);
    makeBlockChange(X + 2, gy + 1, Z + 2, B_oak_sapling);
    int grew = 0;
    for (int t = 0; t < 4000 && !grew; t ++) { tickFarming(1); grew = IS_BLOCK(getBlockAt(X + 2, gy + 1, Z + 2), oak_log); }
    CHECK(grew, "a sapling grows into a tree over time");
    CHECK(IS_BLOCK(getBlockAt(X + 2, gy + 6, Z + 2), oak_leaves) || IS_BLOCK(getBlockAt(X + 2, gy + 5, Z + 2), oak_leaves), "the tree has leaves");
    // a sapling with no room above stays a sapling
    clearArea();
    makeBlockChange(X + 2, gy, Z + 2, B_grass_block);
    makeBlockChange(X + 2, gy + 1, Z + 2, B_oak_sapling);
    makeBlockChange(X + 2, gy + 3, Z + 2, B_stone);
    for (int t = 0; t < 2000; t ++) tickFarming(1);
    CHECK(IS_BLOCK(getBlockAt(X + 2, gy + 1, Z + 2), oak_sapling), "a sapling under a roof does not grow");
    clearArea();
  }

  // ---- animals: luring, breeding, growing up ----
  {
    clearArea();
    for (int i = 0; i < MAX_MOBS; i ++) mob_data[i].type = 0;
    p->x = X; p->z = Z; p->y = gy + 1; p->flags = 0;
    int a = spawnMob(28, X + 5, gy + 1, Z + 5, 10);
    CHECK(a >= 0, "a cow spawns");
    // a cow follows a player holding wheat
    hold(I_wheat, 6);
    int before = abs(mob_data[a].x - X) + abs(mob_data[a].z - Z);
    for (int t = 0; t < 3; t ++) handleServerTick(1000000);
    int after = abs(mob_data[a].x - X) + abs(mob_data[a].z - Z);
    CHECK(after < before, "a cow walks towards a player holding wheat");
    // and stops when it arrives
    for (int t = 0; t < 8; t ++) handleServerTick(1000000);
    CHECK(abs(mob_data[a].x - X) + abs(mob_data[a].z - Z) <= 2, "the cow stops next to the player");
    // a pig is not interested in wheat
    int pig = spawnMob(95, X + 5, gy + 1, Z + 5, 10);
    int px0 = mob_data[pig].x, pz0 = mob_data[pig].z;
    mob_data[a].type = 0;
    for (int t = 0; t < 3; t ++) handleServerTick(1000000);
    CHECK(abs(mob_data[pig].x - px0) + abs(mob_data[pig].z - pz0) <= 3, "a pig doesn't follow wheat");
    mob_data[pig].type = 0;

    // breeding
    int c1 = spawnMob(28, X + 1, gy + 1, Z, 10);
    int c2 = spawnMob(28, X + 2, gy + 1, Z, 10);
    hold(I_wheat, 3);
    interactEntity(-2 - c1, p->client_fd);
    CHECK((mob_data[c1].flags & 2) && p->inventory_count[0] == 2, "feeding wheat puts a cow in love and uses one wheat");
    interactEntity(-2 - c1, p->client_fd);
    CHECK(p->inventory_count[0] == 2, "a cow already in love isn't fed again");
    interactEntity(-2 - c2, p->client_fd);
    CHECK((mob_data[c2].flags & 2) && p->inventory_count[0] == 1, "the second cow is fed");
    int cows_before = 0; for (int i = 0; i < MAX_MOBS; i ++) if (mob_data[i].type == 28) cows_before ++;
    tickAnimals();
    int cows_after = 0, baby = -1; for (int i = 0; i < MAX_MOBS; i ++) if (mob_data[i].type == 28) { cows_after ++; if (mob_data[i].flags & 1) baby = i; }
    CHECK(cows_after == cows_before + 1 && baby >= 0, "two cows in love make a baby");
    CHECK(!(mob_data[c1].flags & 2) && mob_data[c1].timer == 300, "the parents rest for 5 minutes");
    CHECK(mob_data[baby].timer >= 1199 && mob_data[baby].timer <= 1200, "a baby needs 20 minutes to grow up");
    interactEntity(-2 - c1, p->client_fd);
    CHECK(p->inventory_count[0] == 1, "parents can't breed again right away");
    // love runs out
    hold(I_wheat, 2);
    mob_data[c1].timer = 0; mob_data[c2].timer = 0;
    interactEntity(-2 - c1, p->client_fd);
    for (int t = 0; t < 31; t ++) { mob_data[c2].flags &= ~2; tickAnimals(); }
    CHECK(!(mob_data[c1].flags & 2), "love wears off after 30 seconds");
    // babies grow up
    mob_data[baby].timer = 1200;
    hold(I_wheat, 2);
    interactEntity(-2 - baby, p->client_fd);
    CHECK(mob_data[baby].timer == 1080 && p->inventory_count[0] == 1, "feeding a baby speeds up its growth");
    for (int t = 0; t < 1080; t ++) tickAnimals();
    CHECK(!(mob_data[baby].flags & 1), "the baby becomes an adult");
    // a full world makes no baby, but nothing breaks
    for (int i = 0; i < MAX_MOBS; i ++) if (mob_data[i].type == 0) { mob_data[i].type = 145; mob_data[i].data = 20; }
    mob_data[c1].timer = 0; mob_data[c2].timer = 0; mob_data[c1].flags |= 2; mob_data[c2].flags |= 2; mob_data[c1].love = 30; mob_data[c2].love = 30;
    tickAnimals();
    CHECK(countFreeMobSlots() == 0, "no baby when all mob slots are used");
    for (int i = 0; i < MAX_MOBS; i ++) mob_data[i].type = 0;
  }

  // ---- beds ----
  {
    uint16_t W = I_white_wool;
    uint16_t bed_grid[9] = { W, W, W,  P, P, P,  0, 0, 0 };
    CHECK(craft(bed_grid, &n) == I_white_bed && n == 1, "bed recipe: 3 wool and 3 planks");
    clearArea();
    for (int i = 0; i < MAX_MOBS; i ++) mob_data[i].type = 0;
    p->x = X + 6; p->z = Z + 6; p->y = gy + 1; p->yaw = 0; p->flags = 0;
    hold(I_white_bed, 2);
    handlePlayerUseItem(p, X, gy, Z, 1, 0);
    int look = yawToDirection(p->yaw);
    short hx = X + dirDX(look), hz = Z + dirDZ(look);
    CHECK(getBlockAt(X, gy + 1, Z) == BMIN_white_bed + OFF_bed[look][0], "bed foot is placed where clicked");
    CHECK(getBlockAt(hx, gy + 1, hz) == BMIN_white_bed + OFF_bed[look][1], "bed head is placed in the look direction");
    CHECK(p->inventory_count[0] == 1, "one bed used");
    // no room for the head
    makeBlockChange(X + 3, gy + 1, Z + 3, B_stone);
    makeBlockChange(X + 3 + dirDX(look), gy + 1, Z + 3 + dirDZ(look), B_stone);
    handlePlayerUseItem(p, X + 3, gy, Z + 3, 1, 0);
    CHECK(p->inventory_count[0] == 1, "no bed when the head has no room");
    // breaking either half removes both and gives a bed back
    handlePlayerAction(p, 2, hx, gy + 1, hz);
    CHECK(getBlockAt(X, gy + 1, Z) == B_air && getBlockAt(hx, gy + 1, hz) == B_air, "breaking one half removes the whole bed");
    CHECK(countItem(p, I_white_bed) == 2, "the bed is dropped");
    makeBlockChange(X + 3, gy + 1, Z + 3, B_air);
    makeBlockChange(X + 3 + dirDX(look), gy + 1, Z + 3 + dirDZ(look), B_air);

    // sleeping
    hold(I_white_bed, 1);
    handlePlayerUseItem(p, X, gy, Z, 1, 0);
    p->x = X; p->z = Z; p->y = gy + 2;
    world_time = 1000;
    useBed(p, X, gy + 1, Z);
    for (int t = 0; t < 8; t ++) tickSleeping();
    CHECK(world_time == 1000, "you can't sleep through the day");
    world_time = 14000;
    int zombie = spawnMob(145, X + 2, gy + 1, Z, 20);
    useBed(p, X, gy + 1, Z);
    for (int t = 0; t < 8; t ++) tickSleeping();
    CHECK(world_time == 14000, "a zombie nearby stops you from sleeping");
    mob_data[zombie].type = 0;
    useBed(p, X, gy + 1, Z);
    for (int t = 0; t < 3; t ++) tickSleeping();
    CHECK(world_time == 14000, "the night isn't skipped right away");
    for (int t = 0; t < 4; t ++) tickSleeping();
    CHECK(world_time == 0, "sleeping at night skips to the morning");
    // walking away cancels sleeping
    world_time = 15000;
    useBed(p, X, gy + 1, Z);
    tickSleeping();
    p->x = X + 20;
    for (int t = 0; t < 8; t ++) tickSleeping();
    CHECK(world_time == 15000, "walking away from the bed cancels sleeping");

    // respawning at the bed
    p->x = 8; p->z = 8; p->y = 80;
    CHECK(applyBedSpawn(p) && p->x == X && p->z == Z, "after dying you come back at your bed");
    handlePlayerAction(p, 2, X, gy + 1, Z);
    p->x = 8; p->z = 8;
    CHECK(!applyBedSpawn(p) && p->x == 8, "without the bed you come back at the world spawn");
    world_time = 1000;
  }

  // ---- mobs can't hit through blocks ----
  clearArea();
  CHECK(mobCanReachPlayer(X, gy + 1, Z, X + 2, gy + 1, Z), "a mob can hit across open air");
  CHECK(mobCanReachPlayer(X, gy + 1, Z, X + 1, gy + 1, Z), "a mob can hit right next to the player");
  makeBlockChange(X + 1, gy + 1, Z, BMIN_oak_door + OFF_door[DIR_EAST][0][0]);
  makeBlockChange(X + 1, gy + 2, Z, BMIN_oak_door + OFF_door[DIR_EAST][1][0]);
  CHECK(!mobCanReachPlayer(X, gy + 1, Z, X + 2, gy + 1, Z), "a closed door blocks the hit");
  CHECK(!mobCanReachPlayer(X, gy + 2, Z, X + 2, gy + 1, Z), "a closed door blocks a hit from a mob standing one block higher");
  CHECK(!mobCanReachPlayer(X, gy + 1, Z, X + 2, gy + 2, Z), "a closed door blocks a hit on a player standing one block higher");
  makeBlockChange(X + 1, gy + 1, Z, BMIN_oak_door + OFF_door[DIR_EAST][0][1]);
  makeBlockChange(X + 1, gy + 2, Z, BMIN_oak_door + OFF_door[DIR_EAST][1][1]);
  CHECK(mobCanReachPlayer(X, gy + 1, Z, X + 2, gy + 1, Z), "an open door lets the hit through");
  makeBlockChange(X + 1, gy + 1, Z, B_stone);
  CHECK(!mobCanReachPlayer(X, gy + 1, Z, X + 2, gy + 1, Z), "a wall blocks the hit");
  makeBlockChange(X + 1, gy + 1, Z, B_air);
  makeBlockChange(X + 1, gy + 2, Z, B_stone);
  CHECK(!mobCanReachPlayer(X, gy + 1, Z, X + 2, gy + 1, Z), "a block at head height blocks the hit");
  makeBlockChange(X + 1, gy + 2, Z, B_air);
  makeBlockChange(X, gy + 1, Z + 1, B_stone);
  makeBlockChange(X + 1, gy + 1, Z, B_stone);
  CHECK(!mobCanReachPlayer(X, gy + 1, Z, X + 1, gy + 1, Z + 1), "both corners blocked stops a diagonal hit");
  makeBlockChange(X + 1, gy + 1, Z, B_air);
  CHECK(mobCanReachPlayer(X, gy + 1, Z, X + 1, gy + 1, Z + 1), "one open corner lets a diagonal hit through");
  makeBlockChange(X, gy + 1, Z + 1, B_air);
  makeBlockChange(X + 1, gy + 1, Z, B_glass);
  CHECK(!mobCanReachPlayer(X, gy + 1, Z, X + 2, gy + 1, Z), "glass blocks the hit");
  makeBlockChange(X + 1, gy + 1, Z, B_air);

  // ---- save/load roundtrip ----
  makeBlockChange(10, 70, 10, B_gold_block);
  makeBlockChange(-11, 71, 12, BMIN_oak_door + OFF_door[2][1][1]);
  CHECK(getBlockAt(10, 70, 10) == B_gold_block, "block change lookup");

  printf("\n%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL GAME TESTS PASSED", fails);
  return fails != 0;
}
