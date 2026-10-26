#include <stddef.h>
#include "blocks.h"

const BlockInfo *getBlockInfo (uint16_t state) {
  int lo = 0, hi = (int)block_info_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    const BlockInfo *info = &block_info[mid];
    if (state < info->min) hi = mid - 1;
    else if (state > info->max) lo = mid + 1;
    else return info;
  }
  return NULL;
}

int yawToDirection (int8_t yaw) {
  int angle = yaw * 180 / 127;
  if (angle >= -45 && angle < 45) return DIR_SOUTH;
  if (angle >= 45 && angle < 135) return DIR_WEST;
  if (angle >= -135 && angle < -45) return DIR_EAST;
  return DIR_NORTH;
}

// Offset of a state from its block's first state
static int stateOffsetOf (uint16_t state) {
  const BlockInfo *info = getBlockInfo(state);
  return info ? state - info->min : -1;
}

int decodeDoor (uint16_t state, int *facing, int *upper, int *open) {
  int off = stateOffsetOf(state);
  for (int f = 0; f < 4; f ++) for (int u = 0; u < 2; u ++) for (int o = 0; o < 2; o ++) {
    if (OFF_door[f][u][o] == off) { *facing = f; *upper = u; *open = o; return 1; }
  }
  return 0;
}

int decodeTrapdoor (uint16_t state, int *facing, int *top, int *open) {
  int off = stateOffsetOf(state);
  for (int f = 0; f < 4; f ++) for (int t = 0; t < 2; t ++) for (int o = 0; o < 2; o ++) {
    if (OFF_trapdoor[f][t][o] == off) { *facing = f; *top = t; *open = o; return 1; }
  }
  return 0;
}

int decodeGate (uint16_t state, int *facing, int *open) {
  int off = stateOffsetOf(state);
  for (int f = 0; f < 4; f ++) for (int o = 0; o < 2; o ++) {
    if (OFF_gate[f][o] == off) { *facing = f; *open = o; return 1; }
  }
  return 0;
}

int decodeStairs (uint16_t state, int *facing, int *top) {
  int off = stateOffsetOf(state);
  for (int f = 0; f < 4; f ++) for (int t = 0; t < 2; t ++) {
    if (OFF_stairs[f][t] == off) { *facing = f; *top = t; return 1; }
  }
  return 0;
}

int decodeSlab (uint16_t state, int *type) {
  int off = stateOffsetOf(state);
  for (int t = 0; t < 3; t ++) {
    if (OFF_slab[t] == off) { *type = t; return 1; }
  }
  return 0;
}

int decodeLog (uint16_t state, int *axis) {
  int off = stateOffsetOf(state);
  for (int a = 0; a < 3; a ++) {
    if (OFF_log[a] == off) { *axis = a; return 1; }
  }
  return 0;
}
