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

#define registerSmeltingRecipe(a, b) \
  if (*material == a && (*output_item == b || *output_item == 0)) *output_item = b

void getSmeltingOutput (PlayerData *player) {

  uint8_t *material_count = &player->craft_count[0];
  uint8_t *fuel_count = &player->craft_count[1];

  // Don't process if we're missing material or fuel
  if (*material_count == 0 || *fuel_count == 0) return;

  uint16_t *material = &player->craft_items[0];
  uint16_t *fuel = &player->craft_items[1];

  // Don't process if we're missing material or fuel
  if (*material == 0 || *fuel == 0) return;

  // Furnace output is 3rd crafting table slot
  uint8_t *output_count = &player->craft_count[2];
  uint16_t *output_item = &player->craft_items[2];

  // Determine fuel efficiency based on the type of item
  // Since we can't represent fractions, some items use a random component
  // to represent the fractional part. In some cases (e.g. sticks), this
  // can lead to a fuel_value of 0, which means that the fuel gets consumed
  // without processing any materials.
  uint8_t fuel_value = 0;
  if (*fuel == I_coal) fuel_value = 8;
  else if (*fuel == I_charcoal) fuel_value = 8;
  else if (*fuel == I_coal_block) fuel_value = 80;
  else if (*fuel == I_oak_planks) fuel_value = 1 + (fast_rand() & 1);
  else if (*fuel == I_oak_log) fuel_value = 1 + (fast_rand() & 1);
  else if (*fuel == I_crafting_table) fuel_value = 1 + (fast_rand() & 1);
  else if (*fuel == I_stick) fuel_value = (fast_rand() & 1);
  else if (*fuel == I_oak_sapling) fuel_value = (fast_rand() & 1);
  else if (*fuel == I_wooden_axe) fuel_value = 1;
  else if (*fuel == I_wooden_pickaxe) fuel_value = 1;
  else if (*fuel == I_wooden_shovel) fuel_value = 1;
  else if (*fuel == I_wooden_sword) fuel_value = 1;
  else if (*fuel == I_wooden_hoe) fuel_value = 1;
  else return;

  uint8_t exchange = *material_count > fuel_value ? fuel_value : *material_count;

  registerSmeltingRecipe(I_cobblestone, I_stone);
  else registerSmeltingRecipe(I_oak_log, I_charcoal);
  else registerSmeltingRecipe(I_oak_wood, I_charcoal);
  else registerSmeltingRecipe(I_raw_iron, I_iron_ingot);
  else registerSmeltingRecipe(I_raw_gold, I_gold_ingot);
  else registerSmeltingRecipe(I_sand, I_glass);
  else registerSmeltingRecipe(I_raw_copper, I_copper_ingot);
  else registerSmeltingRecipe(I_stone, I_smooth_stone);
  else registerSmeltingRecipe(I_sandstone, I_smooth_sandstone);
  else registerSmeltingRecipe(I_stone_bricks, I_cracked_stone_bricks);
  else registerSmeltingRecipe(I_chicken, I_cooked_chicken);
  else registerSmeltingRecipe(I_beef, I_cooked_beef);
  else registerSmeltingRecipe(I_porkchop, I_cooked_porkchop);
  else registerSmeltingRecipe(I_mutton, I_cooked_mutton);
  else registerSmeltingRecipe(I_potato, I_baked_potato);
  else return;

  *output_count += exchange;
  *material_count -= exchange;

  *fuel_count -= 1;
  if (*fuel_count == 0) *fuel = 0;

  if (*material_count <= 0) {
    *material_count = 0;
    *material = 0;
  } else return getSmeltingOutput(player);

  return;

}
