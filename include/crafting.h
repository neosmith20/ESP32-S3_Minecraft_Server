#ifndef CRAFTING_H
#define CRAFTING_H

#include "globals.h"

void getCraftingOutput (PlayerData *player, uint8_t *count, uint16_t *item);
// Seconds of heat needed to smelt one item
#define FURNACE_SMELT_SECONDS 10

// What a furnace needs to remember between seconds (kept in memory only)
typedef struct {
  uint16_t burn_left;   // seconds of heat left from the current fuel item
  uint16_t burn_total;  // length of the current fuel item's burn, for the flame display
  uint8_t progress;     // seconds spent on the current item
} FurnaceState;

uint16_t getSmeltingResult (uint16_t material);
uint8_t tickFurnace (PlayerData *player, FurnaceState *state);

#endif
