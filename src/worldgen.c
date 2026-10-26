#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
  #include "esp_heap_caps.h"
#endif

#include "globals.h"
#include "tools.h"
#include "registries.h"
#include "blocks.h"
#include "procedures.h"
#include "worldgen.h"

uint32_t getChunkHash (short x, short z) {

  uint8_t buf[8];
  memcpy(buf, &x, 2);
  memcpy(buf + 2, &z, 2);
  memcpy(buf + 4, &world_seed, 4);

  return splitmix64(*((uint64_t *)buf));

}

uint8_t getChunkBiome (short x, short z) {

  // Center biomes on 0;0
  x += BIOME_RADIUS;
  z += BIOME_RADIUS;

  // Calculate distance from biome center
  int8_t dx = BIOME_RADIUS - mod_abs(x, BIOME_SIZE);
  int8_t dz = BIOME_RADIUS - mod_abs(z, BIOME_SIZE);
  // Each biome is a circular island, with beaches in-between
  // Determine whether the given chunk is within the island
  if (dx * dx + dz * dz > BIOME_RADIUS * BIOME_RADIUS) return W_beach;

  // Calculate "biome coordinates" (one step above chunk coordinates)
  short biome_x = div_floor(x, BIOME_SIZE);
  short biome_z = div_floor(z, BIOME_SIZE);

  // The biome itself is plucked directly from the world seed.
  // The 32-bit seed is treated as a 4x4 biome matrix, with each biome
  // taking up 2 bytes. This is why there are only 4 biomes, excluding
  // beaches. Using the world seed as a repeating pattern avoids
  // having to generate and layer yet another hash.
  uint8_t index = abs((biome_x & 3) + ((biome_z * 4) & 15));
  return (world_seed >> (index * 2)) & 3;

}

uint8_t getCornerHeight (uint32_t hash, uint8_t biome) {

  // When calculating the height, parts of the hash are used as random values.
  // Often, multiple values are stacked to stabilize the distribution while
  // allowing for occasionally larger variances.
  uint8_t height = TERRAIN_BASE_HEIGHT;

  switch (biome) {

    case W_mangrove_swamp: {
      height += (
        (hash % 3) +
        ((hash >> 4) % 3) +
        ((hash >> 8) % 3) +
        ((hash >> 12) % 3)
      );
      // If height dips below sea level, push it down further
      // This ends up creating many large ponds of water
      if (height < 64) height -= (hash >> 24) & 3;
      break;
    }

    case W_plains: {
      height += (
        (hash & 3) +
        (hash >> 4 & 3) +
        (hash >> 8 & 3) +
        (hash >> 12 & 3)
      );
      break;
    }

    case W_desert: {
      height += 4 + (
        (hash & 3) +
        (hash >> 4 & 3)
      );
      break;
    }

    case W_beach: {
      // Start slightly below sea level to ensure it's all water
      height = 62 - (
        (hash & 3) +
        (hash >> 4 & 3) +
        (hash >> 8 & 3)
      );
      break;
    }

    case W_snowy_plains: {
      // Use fewer components with larger ranges to create hills
      height += (
        (hash & 7) +
        (hash >> 4 & 7)
      );
      break;
    }

    default: break;
  }

  return height;

}

uint8_t interpolate (uint8_t a, uint8_t b, uint8_t c, uint8_t d, int x, int z) {
  uint16_t top    = a * (CHUNK_SIZE - x) + b * x;
  uint16_t bottom = c * (CHUNK_SIZE - x) + d * x;
  return (top * (CHUNK_SIZE - z) + bottom * z) / (CHUNK_SIZE * CHUNK_SIZE);
}

// Calculates terrain height using a pointer to an array of anchors
// The pointer should point towards the minichunk containing the desired
// coordinates, with available neighbors on +X and +Z.
uint8_t getHeightAtFromAnchors (int rx, int rz, ChunkAnchor *anchor_ptr) {

  if (rx == 0 && rz == 0) {
    int height = getCornerHeight(anchor_ptr[0].hash, anchor_ptr[0].biome);
    if (height > 67) return height - 1;
  }
  return interpolate(
    getCornerHeight(anchor_ptr[0].hash, anchor_ptr[0].biome),
    getCornerHeight(anchor_ptr[1].hash, anchor_ptr[1].biome),
    getCornerHeight(anchor_ptr[16 / CHUNK_SIZE + 1].hash, anchor_ptr[16 / CHUNK_SIZE + 1].biome),
    getCornerHeight(anchor_ptr[16 / CHUNK_SIZE + 2].hash, anchor_ptr[16 / CHUNK_SIZE + 2].biome),
    rx, rz
  );

}

uint8_t getHeightAtFromHash (int rx, int rz, int _x, int _z, uint32_t chunk_hash, uint8_t biome) {

  if (rx == 0 && rz == 0) {
    int height = getCornerHeight(chunk_hash, biome);
    if (height > 67) return height - 1;
  }
  return interpolate(
    getCornerHeight(chunk_hash, biome),
    getCornerHeight(getChunkHash(_x + 1, _z), getChunkBiome(_x + 1, _z)),
    getCornerHeight(getChunkHash(_x, _z + 1), getChunkBiome(_x, _z + 1)),
    getCornerHeight(getChunkHash(_x + 1, _z + 1), getChunkBiome(_x + 1, _z + 1)),
    rx, rz
  );

}

// Get terrain height at the given coordinates
// Does *not* account for block changes
uint8_t getHeightAt (int x, int z) {

  int _x = div_floor(x, CHUNK_SIZE);
  int _z = div_floor(z, CHUNK_SIZE);
  int rx = mod_abs(x, CHUNK_SIZE);
  int rz = mod_abs(z, CHUNK_SIZE);
  uint32_t chunk_hash = getChunkHash(_x, _z);
  uint8_t biome = getChunkBiome(_x, _z);

  return getHeightAtFromHash(rx, rz, _x, _z, chunk_hash, biome);

}

uint16_t getTerrainAtFromCache (int x, int y, int z, int rx, int rz, ChunkAnchor anchor, ChunkFeature feature, uint8_t height) {

  if (y >= 64 && y >= height && feature.y != 255) switch (anchor.biome) {
    case W_plains: { // Generate trees in the plains biome

      // Don't generate trees underwater
      if (feature.y < 64) break;

      // Handle tree stem and the dirt under it
      if (x == feature.x && z == feature.z) {
        if (y == feature.y - 1) return B_dirt;
        if (y >= feature.y && y < feature.y - feature.variant + 6) return B_oak_log;
      }

      // Get X/Z distance from center of tree
      uint8_t dx = x > feature.x ? x - feature.x : feature.x - x;
      uint8_t dz = z > feature.z ? z - feature.z : feature.z - z;

      // Generate leaf clusters
      if (dx < 3 && dz < 3 && y > feature.y - feature.variant + 2 && y < feature.y - feature.variant + 5) {
        if (y == feature.y - feature.variant + 4 && dx == 2 && dz == 2) break;
        return B_oak_leaves;
      }
      if (dx < 2 && dz < 2 && y >= feature.y - feature.variant + 5 && y <= feature.y - feature.variant + 6) {
        if (y == feature.y - feature.variant + 6 && dx == 1 && dz == 1) break;
        return B_oak_leaves;
      }

      // Since we're sure that we're above sea level and in a plains biome,
      // there's no need to drop down to decide the surrounding blocks.
      if (y == height) return B_grass_block;
      return B_air;
    }

    case W_desert: { // Generate dead bushes and cacti in deserts

      if (x != feature.x || z != feature.z) break;

      if (feature.variant == 0) {
        if (y == height + 1) return B_dead_bush;
      } else if (y > height) {
        // The size of the cactus is determined based on whether the terrain
        // height is even or odd at the target location
        if (height & 1 && y <= height + 3) return B_cactus;
        if (y <= height + 2) return B_cactus;
      }

      break;

    }

    case W_mangrove_swamp: { // Generate lilypads and moss carpets in swamps

      if (x == feature.x && z == feature.z && y == 64 && height < 63) {
        return B_lily_pad;
      }

      if (y == height + 1) {
        uint8_t dx = x > feature.x ? x - feature.x : feature.x - x;
        uint8_t dz = z > feature.z ? z - feature.z : feature.z - z;
        if (dx + dz < 4) return B_moss_carpet;
      }

      break;
    }

    case W_snowy_plains: { // Generate grass stubs in snowy plains

      if (x == feature.x && z == feature.z && y == height + 1 && height >= 64) {
        return B_short_grass;
      }

      break;
    }

    default: break;
  }

  // Handle surface-level terrain (the very topmost blocks)
  if (height >= 63) {
    if (y == height) {
      if (anchor.biome == W_mangrove_swamp) return B_mud;
      if (anchor.biome == W_snowy_plains) return B_snowy_grass_block;
      if (anchor.biome == W_desert) return B_sand;
      if (anchor.biome == W_beach) return B_sand;
      return B_grass_block;
    }
    if (anchor.biome == W_snowy_plains && y == height + 1) {
      return B_snow;
    }
  }
  // Starting at 4 blocks below terrain level, generate minerals and caves
  if (y <= height - 4) {
    // Caves use the same shape as surface terrain, just mirrored
    int8_t gap = height - TERRAIN_BASE_HEIGHT;
    if (y < CAVE_BASE_DEPTH + gap && y > CAVE_BASE_DEPTH - gap) return B_air;

    // The chunk-relative X and Z coordinates are used as the seed for an
    // xorshift RNG/hash function to generate the Y coordinate of the ore
    // in this column. This way, each column is guaranteed to have exactly
    // one ore candidate, as there will always be a Y value to reference.
    uint8_t ore_y = ((rx & 15) << 4) + (rz & 15);
    ore_y ^= ore_y << 4;
    ore_y ^= ore_y >> 5;
    ore_y ^= ore_y << 1;
    ore_y &= 63;

    if (y == ore_y) {
      // Since the ore Y coordinate is effectely a random number in range [0;64),
      // we use it in a bit shift with the chunk's anchor hash to get another
      // pseudo-random number for the ore's rarity.
      uint8_t ore_probability = (anchor.hash >> (ore_y % 24)) & 255;
      // Ore placement is determined by Y level and "probability"
      if (y < 15) {
        if (ore_probability < 10) return B_diamond_ore;
        if (ore_probability < 12) return B_gold_ore;
        if (ore_probability < 15) return B_redstone_ore;
      }
      if (y < 30) {
        if (ore_probability < 3) return B_gold_ore;
        if (ore_probability < 8) return B_redstone_ore;
      }
      if (y < 54) {
        if (ore_probability < 30) return B_iron_ore;
        if (ore_probability < 40) return B_copper_ore;
      }
      if (ore_probability < 60) return B_coal_ore;
      if (y < 5) return B_lava;
      return B_cobblestone;
    }

    // For everything else, fall back to stone
    return B_stone;
  }
  // Handle the space between stone and grass
  if (y <= height) {
    if (anchor.biome == W_desert) return B_sandstone;
    if (anchor.biome == W_mangrove_swamp) return B_mud;
    if (anchor.biome == W_beach && height > 64) return B_sandstone;
    return B_dirt;
  }
  // If all else failed, but we're below sea level, generate water (or ice)
  if (y == 63 && anchor.biome == W_snowy_plains) return B_ice;
  if (y < 64) return B_water;

  // For everything else, fall back to air
  return B_air;

}

ChunkFeature getFeatureFromAnchor (ChunkAnchor anchor) {

  ChunkFeature feature;
  uint8_t feature_position = anchor.hash % (CHUNK_SIZE * CHUNK_SIZE);

  feature.x = feature_position % CHUNK_SIZE;
  feature.z = feature_position / CHUNK_SIZE;
  uint8_t skip_feature = false;

  // The following check does two things:
  //  firstly, it ensures that trees don't cross chunk boundaries;
  //  secondly, it reduces overall feature count. This is favorable
  //  everywhere except for swamps, which are otherwise very boring.
  if (anchor.biome != W_mangrove_swamp) {
    if (feature.x < 3 || feature.x > CHUNK_SIZE - 3) skip_feature = true;
    else if (feature.z < 3 || feature.z > CHUNK_SIZE - 3) skip_feature = true;
  }

  if (skip_feature) {
    // Skipped features are indicated by a Y coordinate of 0xFF (255)
    feature.y = 0xFF;
  } else {
    feature.x += anchor.x * CHUNK_SIZE;
    feature.z += anchor.z * CHUNK_SIZE;
    feature.y = getHeightAtFromHash(
      mod_abs(feature.x, CHUNK_SIZE), mod_abs(feature.z, CHUNK_SIZE),
      anchor.x, anchor.z, anchor.hash, anchor.biome
    ) + 1;
    feature.variant = (anchor.hash >> (feature.x + feature.z)) & 1;
  }

  return feature;

}

uint16_t getTerrainAt (int x, int y, int z, ChunkAnchor anchor) {

  if (y > 80) return B_air;

  int rx = x % CHUNK_SIZE;
  int rz = z % CHUNK_SIZE;
  if (rx < 0) rx += CHUNK_SIZE;
  if (rz < 0) rz += CHUNK_SIZE;

  ChunkFeature feature = getFeatureFromAnchor(anchor);
  uint8_t height = getHeightAtFromHash(rx, rz, anchor.x, anchor.z, anchor.hash, anchor.biome);

  return getTerrainAtFromCache(x, y, z, rx, rz, anchor, feature, height);

}

uint16_t getBlockAt (int x, int y, int z) {

  if (y < 0) return B_bedrock;

  uint16_t block_change = getBlockChange(x, y, z);
  if (block_change != BLOCK_NONE) return block_change;

  short anchor_x = div_floor(x, CHUNK_SIZE);
  short anchor_z = div_floor(z, CHUNK_SIZE);
  ChunkAnchor anchor = {
    .x = anchor_x,
    .z = anchor_z,
    .hash = getChunkHash(anchor_x, anchor_z),
    .biome = getChunkBiome(anchor_x, anchor_z)
  };

  return getTerrainAt(x, y, z, anchor);

}

// Blocks of the chunk section being built, in the order x + z * 16 + y * 256
uint16_t chunk_section[4096];
ChunkAnchor chunk_anchors[(16 / CHUNK_SIZE + 1) * (16 / CHUNK_SIZE + 1)];
ChunkFeature chunk_features[256 / (CHUNK_SIZE * CHUNK_SIZE)];
uint8_t chunk_section_height[16][16];

/**
 * Per-column block change index
 * -----------------------------
 * The original code scanned the *entire* block_changes array once for every
 * 16x16x16 section it built (20 scans per chunk). Here the array is scanned
 * once per chunk column instead. The indices of the block changes that fall
 * inside the column are remembered, along with a bitmask of which vertical
 * sections they touch. Sections without changes skip the overlay entirely.
 *
 * If a single column holds more than MAX_COLUMN_CHANGES changes, the list
 * is marked as overflowed and we fall back to scanning the full array, so
 * the result is always correct.
 */
#define MAX_COLUMN_CHANGES 2048
#if MAX_BLOCK_CHANGES > 65535
  #error "MAX_BLOCK_CHANGES must be at most 65535 (indices are stored as uint16_t)"
#endif

static uint16_t column_changes[MAX_COLUMN_CHANGES];
static int column_change_count = 0;
static uint8_t column_overflow = false;
static uint8_t column_valid = false;
static int column_x = 0, column_z = 0;

// Bit N is set when section N (blocks Y = N*16 .. N*16+15) of the prepared
// column contains block changes that get applied on top of terrain.
uint32_t chunk_column_section_mask = 0;

// Whether this block change is applied as part of the chunk data itself.
// Torches and chests are sent as separate block updates instead.
static inline uint8_t isOverlayBlock (uint16_t block) {
  return !(getBlockFlags(block) & BF_SEPARATE_UPDATE);
}

void prepareChunkColumn (int cx, int cz) {

  column_x = cx;
  column_z = cz;
  column_change_count = 0;
  column_overflow = false;
  chunk_column_section_mask = 0;

  for (int i = 0; i < block_changes_count; i ++) {
    uint16_t block = block_changes[i].block;
    if (block == BLOCK_NONE) continue;
    #ifdef ALLOW_CHESTS
      // A chest is followed by 14 entries of raw item data, not real blocks
      if (IS_BLOCK(block, chest)) {
        i += 14;
        continue;
      }
    #endif
    if (block_changes[i].x < cx || block_changes[i].x >= cx + 16) continue;
    if (block_changes[i].z < cz || block_changes[i].z >= cz + 16) continue;
    if (!isOverlayBlock(block)) continue;

    chunk_column_section_mask |= 1u << (block_changes[i].y >> 4);
    if (column_change_count < MAX_COLUMN_CHANGES) {
      column_changes[column_change_count ++] = i;
    } else {
      column_overflow = true;
    }
  }

  column_valid = true;

}

// Must be called when block changes may have been modified since the
// column was prepared. Cheap, so callers can use it liberally.
void invalidateChunkColumn () {
  column_valid = false;
}

// Writes a single block change into `chunk_section`, if it lies within
// the section whose origin is (cx, cy, cz)
static inline void applyBlockChange (const BlockChange *change, int cx, int cy, int cz) {
  if (change->y < cy || change->y >= cy + 16) return;
  if (change->x < cx || change->x >= cx + 16) return;
  if (change->z < cz || change->z >= cz + 16) return;
  int dx = change->x - cx;
  int dy = change->y - cy;
  int dz = change->z - cz;
  chunk_section[dx + (dz << 4) + (dy << 8)] = change->block;
}

/**
 * Terrain cache
 * -------------
 * Terrain is a pure function of the world seed, so a generated section can
 * be reused as long as it is stored *without* block changes (those are
 * overlaid on every send). That means the cache never has to be invalidated.
 *
 * Only the sections below AIR_ABOVE_Y are cached; everything above is air.
 * On ESP32 boards with PSRAM the cache lives there. If the memory can't be
 * allocated (e.g. PSRAM is off) the cache is simply disabled.
 */
#define TERRAIN_SECTIONS (AIR_ABOVE_Y / 16)

// A cached section stores one byte per block, pointing into a small list of
// the (up to CACHE_PALETTE_SIZE) different blocks the section contains.
// Natural terrain never gets close to that many different blocks per section;
// if it ever does, the section simply isn't cached.
#define CACHE_PALETTE_SIZE 32
#define CACHE_SECTION_BYTES (4096 + CACHE_PALETTE_SIZE * 2)

#if CHUNK_CACHE_COLUMNS > 0

typedef struct {
  short x; // Chunk column coordinates (in 16-block units)
  short z;
  uint8_t mask; // Bit N set when section N is stored in the cache
} TerrainCacheEntry;

static TerrainCacheEntry *terrain_cache_meta = NULL;
static uint8_t *terrain_cache_data = NULL;

static inline int terrainCacheSlot (short x, short z) {
  uint32_t h = ((uint32_t)(uint16_t)x * 73856093u) ^ ((uint32_t)(uint16_t)z * 19349663u);
  return (h >> 4) % CHUNK_CACHE_COLUMNS;
}

void initTerrainCache () {

  size_t data_size = (size_t)CHUNK_CACHE_COLUMNS * TERRAIN_SECTIONS * CACHE_SECTION_BYTES;

  #ifdef ESP_PLATFORM
    terrain_cache_data = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  #else
    terrain_cache_data = malloc(data_size);
  #endif
  terrain_cache_meta = calloc(CHUNK_CACHE_COLUMNS, sizeof(TerrainCacheEntry));

  if (terrain_cache_data == NULL || terrain_cache_meta == NULL) {
    free(terrain_cache_data);
    free(terrain_cache_meta);
    terrain_cache_data = NULL;
    terrain_cache_meta = NULL;
    printf("Terrain cache disabled (could not allocate %u KB)\n", (unsigned)(data_size / 1024));
    return;
  }

  printf("Terrain cache enabled: %d chunk columns, %u KB\n", CHUNK_CACHE_COLUMNS, (unsigned)(data_size / 1024));

}

#else

void initTerrainCache () {}

#endif

#if CHUNK_CACHE_COLUMNS > 0

// Compresses `chunk_section` into a cache slot. Returns false if the
// section has too many different blocks to be cached.
static uint8_t storeCachedSection (uint8_t *slot) {
  uint8_t *indices = slot;
  uint16_t *palette = (uint16_t *)(slot + 4096);
  int count = 0;
  uint16_t last_block = chunk_section[0];
  int last_index = 0;
  palette[count ++] = last_block;
  for (int i = 0; i < 4096; i ++) {
    uint16_t block = chunk_section[i];
    if (block != last_block) {
      int found = -1;
      for (int j = 0; j < count; j ++) {
        if (palette[j] == block) { found = j; break; }
      }
      if (found < 0) {
        if (count == CACHE_PALETTE_SIZE) return false;
        found = count;
        palette[count ++] = block;
      }
      last_block = block;
      last_index = found;
    }
    indices[i] = last_index;
  }
  return true;
}

// Restores a cache slot into `chunk_section`
static void loadCachedSection (const uint8_t *slot) {
  const uint16_t *palette = (const uint16_t *)(slot + 4096);
  for (int i = 0; i < 4096; i ++) chunk_section[i] = palette[slot[i]];
}

#endif

// Generates the terrain (without block changes) for the 16x16x16 section
// at (cx, cy, cz) into `chunk_section`
static void generateTerrainSection (int cx, int cy, int cz) {

  // Precompute hashes, anchors and features for each relevant minichunk
  int anchor_index = 0, feature_index = 0;
  for (int i = cz; i < cz + 16 + CHUNK_SIZE; i += CHUNK_SIZE) {
    for (int j = cx; j < cx + 16 + CHUNK_SIZE; j += CHUNK_SIZE) {

      ChunkAnchor *anchor = chunk_anchors + anchor_index;

      anchor->x = j / CHUNK_SIZE;
      anchor->z = i / CHUNK_SIZE;
      anchor->hash = getChunkHash(anchor->x, anchor->z);
      anchor->biome = getChunkBiome(anchor->x, anchor->z);

      // Compute chunk features for the minichunks within this section
      if (i != cz + 16 && j != cx + 16) {
        chunk_features[feature_index] = getFeatureFromAnchor(*anchor);
        feature_index ++;
      }

      anchor_index ++;
    }
  }

  // Precompute terrain height for entire chunk section
  for (int i = 0; i < 16; i ++) {
    for (int j = 0; j < 16; j ++) {
      anchor_index = (j / CHUNK_SIZE) + (i / CHUNK_SIZE) * (16 / CHUNK_SIZE + 1);
      ChunkAnchor *anchor_ptr = chunk_anchors + anchor_index;
      chunk_section_height[j][i] = getHeightAtFromAnchors(j % CHUNK_SIZE, i % CHUNK_SIZE, anchor_ptr);
    }
  }

  // Generate 4096 blocks in one buffer to reduce overhead
  for (int j = 0; j < 4096; j += 8) {
    // These values don't change in the lower array,
    // since all of the operations are on multiples of 8
    int y = j / 256 + cy;
    int rz = j / 16 % 16;
    int rz_mod = rz % CHUNK_SIZE;
    feature_index = (j % 16) / CHUNK_SIZE + (j / 16 % 16) / CHUNK_SIZE * (16 / CHUNK_SIZE);
    anchor_index = (j % 16) / CHUNK_SIZE + (j / 16 % 16) / CHUNK_SIZE * (16 / CHUNK_SIZE + 1);
    for (int offset = 0; offset < 8; offset ++) {
      int k = j + offset;
      int rx = k % 16;
      // Combine all of the cached data to retrieve the block
      chunk_section[k] = getTerrainAtFromCache(
        rx + cx, y, rz + cz,
        rx % CHUNK_SIZE, rz_mod,
        chunk_anchors[anchor_index],
        chunk_features[feature_index],
        chunk_section_height[rx][rz]
      );
    }
  }

}

// Builds a 16x16x16 chunk of blocks and writes it to `chunk_section`
// Returns the biome at the origin corner of the chunk
uint8_t buildChunkSection (int cx, int cy, int cz) {

  // Make sure the block change index matches this chunk column
  if (!column_valid || column_x != cx || column_z != cz) {
    prepareChunkColumn(cx, cz);
  }

  if (cy >= AIR_ABOVE_Y) {

    // Terrain never reaches this high, so the section is empty air
    memset(chunk_section, 0, sizeof(chunk_section)); // B_air is 0

  } else {

    uint8_t from_cache = false;
    #if CHUNK_CACHE_COLUMNS > 0
      TerrainCacheEntry *entry = NULL;
      uint8_t *slot_data = NULL;
      int section = cy / 16;
      if (terrain_cache_data != NULL) {
        short col_x = cx / 16, col_z = cz / 16;
        int slot = terrainCacheSlot(col_x, col_z);
        entry = &terrain_cache_meta[slot];
        // Replace whatever other column was using this slot
        if (entry->x != col_x || entry->z != col_z) {
          entry->x = col_x;
          entry->z = col_z;
          entry->mask = 0;
        }
        slot_data = terrain_cache_data + ((size_t)slot * TERRAIN_SECTIONS + section) * CACHE_SECTION_BYTES;
        if (entry->mask & (1 << section)) {
          loadCachedSection(slot_data);
          from_cache = true;
        }
      }
    #endif

    if (!from_cache) {
      generateTerrainSection(cx, cy, cz);
      #if CHUNK_CACHE_COLUMNS > 0
        if (entry != NULL && storeCachedSection(slot_data)) {
          entry->mask |= (1 << section);
        }
      #endif
    }

  }

  // Apply block changes on top of terrain
  if (column_overflow) {
    // Too many changes in this column to index, scan everything
    for (int i = 0; i < block_changes_count; i ++) {
      if (block_changes[i].block == BLOCK_NONE) continue;
      #ifdef ALLOW_CHESTS
        if (IS_BLOCK(block_changes[i].block, chest)) {
          i += 14;
          continue;
        }
      #endif
      if (block_changes[i].x < cx || block_changes[i].x >= cx + 16) continue;
      if (block_changes[i].z < cz || block_changes[i].z >= cz + 16) continue;
      if (!isOverlayBlock(block_changes[i].block)) continue;
      applyBlockChange(&block_changes[i], cx, cy, cz);
    }
  } else if (chunk_column_section_mask & (1u << (cy >> 4))) {
    for (int i = 0; i < column_change_count; i ++) {
      applyBlockChange(&block_changes[column_changes[i]], cx, cy, cz);
    }
  }

  return getChunkBiome(cx / CHUNK_SIZE, cz / CHUNK_SIZE);

}
