#include <string.h>
#include <stdio.h>

#include "globals.h"
#include "registries.h"
#include "tools.h"
#include "crafting.h"

// Checks whether an item can be used for a recipe ingredient
static uint8_t ingredientMatches (uint16_t wanted, uint16_t have) {
  if (wanted == RECIPE_GROUP_COAL) return have == I_coal || have == I_charcoal;
  return wanted == have;
}

// Checks a shaped recipe against the crafting grid. (min_col, min_row) is
// the top left corner of the filled area of the grid.
static uint8_t matchesShaped (const Recipe *recipe, const uint16_t *grid, int min_col, int min_row, uint8_t mirrored) {
  for (int row = 0; row < recipe->h; row ++) {
    for (int col = 0; col < recipe->w; col ++) {
      uint16_t wanted = recipe->input[row * recipe->w + (mirrored ? recipe->w - 1 - col : col)];
      uint16_t have = grid[(min_row + row) * 3 + min_col + col];
      if (wanted == 0) {
        if (have != 0) return false;
      } else if (!ingredientMatches(wanted, have)) {
        return false;
      }
    }
  }
  return true;
}

// Checks a shapeless recipe: the grid has to hold exactly the ingredients
static uint8_t matchesShapeless (const Recipe *recipe, const uint16_t *grid) {
  uint8_t used[9] = {0};
  int ingredients = 0;
  for (int i = 0; i < 9; i ++) if (recipe->input[i]) ingredients ++;
  for (int i = 0; i < 9; i ++) {
    if (grid[i] == 0) continue;
    int found = false;
    for (int j = 0; j < ingredients; j ++) {
      if (used[j] || !ingredientMatches(recipe->input[j], grid[i])) continue;
      used[j] = true;
      found = true;
      break;
    }
    if (!found) return false;
  }
  return true;
}

void getCraftingOutput (PlayerData *player, uint8_t *count, uint16_t *item) {

  *count = 0;
  *item = 0;

  // Exit early if craft_items has been locked
  if (player->flags & 0x80) return;

  // Find the area of the grid that holds items
  int filled = 0, min_col = 3, max_col = -1, min_row = 3, max_row = -1;
  for (int i = 0; i < 9; i ++) {
    if (player->craft_items[i] == 0) continue;
    int col = i % 3, row = i / 3;
    filled ++;
    if (col < min_col) min_col = col;
    if (col > max_col) max_col = col;
    if (row < min_row) min_row = row;
    if (row > max_row) max_row = row;
  }
  if (filled == 0) return;
  int w = max_col - min_col + 1, h = max_row - min_row + 1;

  for (int i = 0; i < recipe_count; i ++) {
    const Recipe *recipe = &recipes[i];
    if (recipe->shapeless) {
      if (!matchesShapeless(recipe, player->craft_items)) continue;
    } else {
      if (recipe->w != w || recipe->h != h) continue;
      if (
        !matchesShaped(recipe, player->craft_items, min_col, min_row, false) &&
        !matchesShaped(recipe, player->craft_items, min_col, min_row, true)
      ) continue;
    }
    *item = recipe->result;
    *count = recipe->count;
    return;
  }

}


// How much smelting a fuel item gives, in half items (0 if it isn't a fuel).
// These match the burn times of the real game.
static uint8_t getFuelUnits (uint16_t fuel) {
  switch (fuel) {
    case I_lava_bucket: return 200;
    case I_coal_block: return 160;
    case I_coal: case I_charcoal: return 16;
    case I_oak_planks: case I_oak_log: case I_oak_wood: case I_crafting_table:
    case I_chest: case I_ladder: case I_oak_fence: case I_oak_fence_gate:
    case I_oak_trapdoor: case I_oak_stairs: case I_oak_pressure_plate: return 3;
    case I_wooden_axe: case I_wooden_pickaxe: case I_wooden_shovel:
    case I_wooden_sword: case I_wooden_hoe: case I_oak_door: return 2;
    case I_stick: case I_oak_sapling: case I_oak_button: return 1;
    default: return 0;
  }
}

void getSmeltingOutput (PlayerData *player) {

  uint8_t *material_count = &player->craft_count[0];
  uint8_t *fuel_count = &player->craft_count[1];

  uint16_t *material = &player->craft_items[0];
  uint16_t *fuel = &player->craft_items[1];

  // Don't process without material, or without fuel (heat left over from
  // earlier fuel still counts)
  if (*material_count == 0 || *material == 0) return;
  if ((*fuel_count == 0 || *fuel == 0) && player->craft_count[3] < 2) return;

  // Furnace output is 3rd crafting table slot
  uint8_t *output_count = &player->craft_count[2];
  uint16_t *output_item = &player->craft_items[2];

  // Work out which smelting recipe applies (without changing anything yet)
  uint16_t result = 0;
  if (*material == I_cobblestone) result = I_stone;
  else if (*material == I_oak_log) result = I_charcoal;
  else if (*material == I_oak_wood) result = I_charcoal;
  else if (*material == I_raw_iron) result = I_iron_ingot;
  else if (*material == I_raw_gold) result = I_gold_ingot;
  else if (*material == I_sand) result = I_glass;
  else if (*material == I_raw_copper) result = I_copper_ingot;
  else if (*material == I_stone) result = I_smooth_stone;
  else if (*material == I_sandstone) result = I_smooth_sandstone;
  else if (*material == I_stone_bricks) result = I_cracked_stone_bricks;
  else if (*material == I_chicken) result = I_cooked_chicken;
  else if (*material == I_beef) result = I_cooked_beef;
  else if (*material == I_porkchop) result = I_cooked_porkchop;
  else if (*material == I_mutton) result = I_cooked_mutton;
  else if (*material == I_potato) result = I_baked_potato;
  else return;
  if (*output_item != 0 && *output_item != result) return;

  // Fuel is measured in half items: smelting one item takes 2 units, and
  // whatever is left over from the last fuel item carries over to the next
  // item, like the burn time of a real furnace. The leftover is kept in the
  // (otherwise unused) fourth crafting slot's count.
  uint8_t *leftover = &player->craft_count[3];

  while (*material_count > 0 && *output_count < 64) {

    if (*leftover < 2) {
      // Burn another fuel item
      if (*fuel == 0 || *fuel_count == 0) break;
      uint8_t fuel_units = getFuelUnits(*fuel);
      if (fuel_units == 0) break;
      *leftover += fuel_units;
      // A lava bucket leaves an empty bucket behind
      if (*fuel == I_lava_bucket) {
        *fuel = I_bucket;
      } else {
        *fuel_count -= 1;
        if (*fuel_count == 0) *fuel = 0;
      }
      continue;
    }

    uint8_t n = *leftover / 2;
    if (n > *material_count) n = *material_count;
    if (n > 64 - *output_count) n = 64 - *output_count;
    *output_item = result;
    *output_count += n;
    *material_count -= n;
    *leftover -= 2 * n;

  }

  if (*material_count == 0) *material = 0;

}
