#ifndef H_BLOCKS
#define H_BLOCKS

#include <stdint.h>
#include "registries.h"

// The block data files are generated from blocks_config.js and recipes_config.js.
// If this stops the build, run `node build_registries.js` in the project folder.
#if !defined(FAM_FARMLAND) || !defined(FAM_CROP)
  #error "include/registries.h is out of date. Run: node build_registries.js (in the project folder), then delete the .pio folder and build again."
#endif

// Block state helpers. Blocks are identified by their Minecraft block state
// ID, which also tells us facing, open/closed, and so on. The generated
// registries.h describes every supported block (see blocks_config.js).

// Looks up the block a state belongs to (NULL if the state isn't supported)
const BlockInfo *getBlockInfo (uint16_t state);

// Flags (BF_*) of a block state, 0 for unsupported states
static inline uint16_t getBlockFlags (uint16_t state) {
  const BlockInfo *info = getBlockInfo(state);
  return info ? info->flags : 0;
}

// Block family (FAM_*) of a block state
static inline uint8_t getBlockFamily (uint16_t state) {
  const BlockInfo *info = getBlockInfo(state);
  return info ? info->family : FAM_NONE;
}

// Horizontal directions, in the order used by the OFF_* tables
#define DIR_NORTH 0
#define DIR_SOUTH 1
#define DIR_WEST 2
#define DIR_EAST 3

// Where a direction points (north is -Z, south +Z, west -X, east +X)
static inline int dirDX (int dir) { return dir == DIR_WEST ? -1 : dir == DIR_EAST ? 1 : 0; }
static inline int dirDZ (int dir) { return dir == DIR_NORTH ? -1 : dir == DIR_SOUTH ? 1 : 0; }
// The opposite direction
static inline int dirOpposite (int dir) { return dir ^ 1; }

// Horizontal direction a player is facing, from the server's stored yaw
// (-127..127 for -180..180 degrees, 0 being south)
int yawToDirection (int8_t yaw);

// Face numbers used by the protocol (0 down, 1 up, 2 north, 3 south, 4 west, 5 east)
// converted to our horizontal direction (-1 for up/down faces)
static inline int faceToDirection (int face) {
  return face >= 2 && face <= 5 ? face - 2 : -1;
}

// ---- State decoding. Each of these finds the position of `state` in the
// family's offset table and returns it through the pointers.
// They return 0 if the state isn't in the table.
int decodeDoor (uint16_t state, int *facing, int *upper, int *open);
int decodeTrapdoor (uint16_t state, int *facing, int *top, int *open);
int decodeGate (uint16_t state, int *facing, int *open);
int decodeStairs (uint16_t state, int *facing, int *top);
int decodeSlab (uint16_t state, int *type);
int decodeLog (uint16_t state, int *axis);

// Crops: growth stage 0 to 7 (7 is ripe). Returns -1 if the state isn't a crop.
int getCropAge (uint16_t state);
// The state of the same crop at another growth stage
uint16_t getCropState (uint16_t state, int age);
// Farmland: 7 is wet, 0 is dry. Returns -1 if the state isn't farmland.
int getFarmlandMoisture (uint16_t state);

#endif
