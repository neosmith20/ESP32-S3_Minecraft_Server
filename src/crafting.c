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

// What a smelting material turns into, or 0 if it can't be smelted
uint16_t getSmeltingResult (uint16_t material) {
  if (material == I_cobblestone) return I_stone;
  if (material == I_oak_log) return I_charcoal;
  if (material == I_oak_wood) return I_charcoal;
  if (material == I_raw_iron) return I_iron_ingot;
  if (material == I_raw_gold) return I_gold_ingot;
  if (material == I_sand) return I_glass;
  if (material == I_raw_copper) return I_copper_ingot;
  if (material == I_stone) return I_smooth_stone;
  if (material == I_sandstone) return I_smooth_sandstone;
  if (material == I_stone_bricks) return I_cracked_stone_bricks;
  if (material == I_chicken) return I_cooked_chicken;
  if (material == I_beef) return I_cooked_beef;
  if (material == I_porkchop) return I_cooked_porkchop;
  if (material == I_mutton) return I_cooked_mutton;
  if (material == I_potato) return I_baked_potato;
  return 0;
}

// Runs a furnace for one second. Smelting one item takes FURNACE_SMELT_SECONDS
// seconds of heat; a fuel item burns for getFuelUnits() * 5 seconds, so a plank
// (3 units) lasts 15 seconds and smelts 1.5 items, like in the real game.
// A fuel item is only used up when there is something to smelt, and the fire
// keeps burning for the rest of its time even if the material runs out.
// The furnace slots are the first three crafting slots of the player:
// 0 = material, 1 = fuel, 2 = output. Returns true if any slot changed.
uint8_t tickFurnace (PlayerData *player, FurnaceState *state) {

  uint8_t changed = false;

  uint16_t *material = &player->craft_items[0];
  uint8_t *material_count = &player->craft_count[0];
  uint16_t *fuel = &player->craft_items[1];
  uint8_t *fuel_count = &player->craft_count[1];
  uint16_t *output = &player->craft_items[2];
  uint8_t *output_count = &player->craft_count[2];

  uint16_t result = (*material_count > 0 && *material != 0) ? getSmeltingResult(*material) : 0;
  uint8_t can_smelt = result != 0 && (*output_count == 0 || (*output == result && *output_count < 64));

  // Light the furnace with a new fuel item when there is work to do
  if (can_smelt && state->burn_left == 0 && *fuel_count > 0 && *fuel != 0) {
    uint8_t units = getFuelUnits(*fuel);
    if (units != 0) {
      state->burn_left = units * 5;
      state->burn_total = state->burn_left;
      if (*fuel == I_lava_bucket) {
        *fuel = I_bucket; // a lava bucket leaves an empty bucket behind
      } else {
        *fuel_count -= 1;
        if (*fuel_count == 0) *fuel = 0;
      }
      changed = true;
    }
  }

  uint8_t lit = state->burn_left > 0;
  if (lit) state->burn_left --;

  if (can_smelt && lit) {
    state->progress ++;
    if (state->progress >= FURNACE_SMELT_SECONDS) {
      state->progress = 0;
      *output = result;
      *output_count += 1;
      *material_count -= 1;
      if (*material_count == 0) *material = 0;
      changed = true;
    }
  } else if (state->progress > 0) {
    // Without heat or material the progress arrow runs backwards
    state->progress = can_smelt ? (state->progress >= 2 ? state->progress - 2 : 0) : 0;
  }

  return changed;

}
