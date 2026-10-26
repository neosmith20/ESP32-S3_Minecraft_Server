#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <winsock2.h>
#endif

#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "packets.h"
#include "registries.h"
#include "blocks.h"
#include "worldgen.h"
#include "structures.h"
#include "serialize.h"
#include "procedures.h"

int client_states[MAX_PLAYERS * 2];

void setClientState (int client_fd, int new_state) {
  // Look for a client state with a matching file descriptor
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    client_states[i + 1] = new_state;
    return;
  }
  // If the above failed, look for an unused client state slot
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != -1) continue;
    client_states[i] = client_fd;
    client_states[i + 1] = new_state;
    return;
  }
}

int getClientState (int client_fd) {
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    return client_states[i + 1];
  }
  return STATE_NONE;
}

int getClientIndex (int client_fd) {
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] != client_fd) continue;
    return i;
  }
  return -1;
}

// Restores player data to initial state (fresh spawn)
void resetPlayerData (PlayerData *player) {
  player->health = 20;
  player->hunger = 20;
  player->saturation = 2500;
  player->x = 8;
  player->z = 8;
  player->y = 80;
  player->flags |= 0x02;
  player->grounded_y = 0;
  for (int i = 0; i < 41; i ++) {
    player->inventory_items[i] = 0;
    player->inventory_count[i] = 0;
  }
  for (int i = 0; i < 9; i ++) {
    player->craft_items[i] = 0;
    player->craft_count[i] = 0;
  }
  player->flags &= ~0x80;
}

// Assigns the given data to a player_data entry
int reservePlayerData (int client_fd, uint8_t *uuid, char *name) {

  for (int i = 0; i < MAX_PLAYERS; i ++) {
    // Found existing player entry (UUID match)
    if (memcmp(player_data[i].uuid, uuid, 16) == 0) {
      // Set network file descriptor and username
      player_data[i].client_fd = client_fd;
      memcpy(player_data[i].name, name, 16);
      // Flag player as loading
      player_data[i].flags |= 0x20;
      player_data[i].flagval_16 = 0;
      // Reset their recently visited chunk list
      for (int j = 0; j < VISITED_HISTORY; j ++) {
        player_data[i].visited_x[j] = 32767;
        player_data[i].visited_z[j] = 32767;
      }
      return 0;
    }
    // Search for unallocated player slots
    uint8_t empty = true;
    for (uint8_t j = 0; j < 16; j ++) {
      if (player_data[i].uuid[j] != 0) {
        empty = false;
        break;
      }
    }
    // Found free space for a player, initialize default parameters
    if (empty) {
      if (player_data_count >= MAX_PLAYERS) return 1;
      player_data[i].client_fd = client_fd;
      player_data[i].flags |= 0x20;
      player_data[i].flagval_16 = 0;
      memcpy(player_data[i].uuid, uuid, 16);
      memcpy(player_data[i].name, name, 16);
      resetPlayerData(&player_data[i]);
      player_data_count ++;
      return 0;
    }
  }

  return 1;

}

int getPlayerData (int client_fd, PlayerData **output) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == client_fd) {
      *output = &player_data[i];
      return 0;
    }
  }
  return 1;
}

// Returns the player with the given name, or NULL if not found
PlayerData *getPlayerByName (int start_offset, int end_offset, uint8_t *buffer) {
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1) continue;
    int j;
    for (j = start_offset; j < end_offset && j < 256 && buffer[j] != ' '; j++) {
      if (player_data[i].name[j - start_offset] != buffer[j]) break;
    }
    if ((j == end_offset || buffer[j] == ' ') && j < 256) {
      return &player_data[i];
    }
  }
  return NULL;
}


// Marks a client as disconnected and cleans up player data
void handlePlayerDisconnect (int client_fd) {
  // Search for a corresponding player in the player data array
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd != client_fd) continue;
    // Mark the player as being offline
    player_data[i].client_fd = -1;
    // Prepare leave message for broadcast
    uint8_t player_name_len = strlen(player_data[i].name);
    strcpy((char *)recv_buffer, player_data[i].name);
    strcpy((char *)recv_buffer + player_name_len, " left the game");
    // Broadcast this player's leave to all other connected clients
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (player_data[j].client_fd == client_fd) continue;
      if (player_data[j].flags & 0x20) continue;
      // Send chat message
      sc_systemChat(player_data[j].client_fd, (char *)recv_buffer, 14 + player_name_len);
      // Remove leaving player's entity
      sc_removeEntity(player_data[j].client_fd, client_fd);
    }
    break;
  }
  // Find the client state entry and reset it
  for (int i = 0; i < MAX_PLAYERS * 2; i += 2) {
    if (client_states[i] == client_fd) {
      client_states[i] = -1;
      return;
    }
  }
}

// Marks a client as connected and broadcasts their data to other players
void handlePlayerJoin (PlayerData* player) {

  // Prepare join message for broadcast
  uint8_t player_name_len = strlen(player->name);
  strcpy((char *)recv_buffer, player->name);
  strcpy((char *)recv_buffer + player_name_len, " joined the game");

  // Inform other clients (and the joining client) of the player's name and entity
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    sc_systemChat(player_data[i].client_fd, (char *)recv_buffer, 16 + player_name_len);
    sc_playerInfoUpdateAddPlayer(player_data[i].client_fd, *player);
    if (player_data[i].client_fd != player->client_fd) {
      sc_spawnEntityPlayer(player_data[i].client_fd, *player);
    }
  }

  // Clear "client loading" flag and fallback timer
  player->flags &= ~0x20;
  player->flagval_16 = 0;

}

void disconnectClient (int *client_fd, int cause) {
  if (*client_fd == -1) return;
  // Remember whether this was a player in the game (and not, for example, a
  // server list ping), to decide about saving at the end
  uint8_t was_playing = getClientState(*client_fd) == STATE_PLAY;
  // Hand over any batched outgoing data (without waiting on the network)
  flush_tx_for_disconnect(*client_fd);
  client_count --;
  setClientState(*client_fd, STATE_NONE);
  handlePlayerDisconnect(*client_fd);
  #ifdef _WIN32
  closesocket(*client_fd);
  printf("Disconnected client %d, cause: %d, errno: %d\n", *client_fd, cause, WSAGetLastError());
  #else
  close(*client_fd);
  printf("Disconnected client %d, cause: %d, errno: %d\n\n", *client_fd, cause, errno);
  #endif
  *client_fd = -1;
  #ifdef DISK_SYNC_WHEN_EMPTY
    // The last player left: nobody can notice a slow flash write now
    if (was_playing && client_count == 0) writeAllDataToDisk();
  #endif
}

uint8_t serverSlotToClientSlot (int window_id, uint8_t slot) {

  if (window_id == 0) { // player inventory

    if (slot < 9) return slot + 36;
    if (slot >= 9 && slot <= 35) return slot;
    if (slot == 40) return 45;
    if (slot >= 36 && slot <= 39) return 44 - slot;
    if (slot >= 41 && slot <= 44) return slot - 40;

  } else if (window_id == 12) { // crafting table

    if (slot >= 41 && slot <= 49) return slot - 40;
    return serverSlotToClientSlot(0, slot - 1);

  } else if (window_id == 14) { // furnace

    if (slot >= 41 && slot <= 43) return slot - 41;
    return serverSlotToClientSlot(0, slot + 6);

  }

  return 255;
}

uint8_t clientSlotToServerSlot (int window_id, uint8_t slot) {

  if (window_id == 0) { // player inventory

    if (slot >= 36 && slot <= 44) return slot - 36;
    if (slot >= 9 && slot <= 35) return slot;
    if (slot == 45) return 40;
    if (slot >= 5 && slot <= 8) return 44 - slot;

    // map inventory crafting slots to player data crafting grid (semi-hack)
    // this abuses the fact that the buffers are adjacent in player data
    if (slot == 1) return 41;
    if (slot == 2) return 42;
    if (slot == 3) return 44;
    if (slot == 4) return 45;

  } else if (window_id == 12) { // crafting table

    // same crafting offset overflow hack as above
    if (slot >= 1 && slot <= 9) return 40 + slot;
    // the rest of the slots are identical, just shifted by one
    if (slot >= 10 && slot <= 45) return clientSlotToServerSlot(0, slot - 1);

  } else if (window_id == 14) { // furnace

    // move furnace items to the player's crafting grid
    // this lets us put them back in the inventory once the window closes
    if (slot <= 2) return 41 + slot;
    // the rest of the slots are identical, just shifted by 6
    if (slot >= 3 && slot <= 38) return clientSlotToServerSlot(0, slot + 6);

  }
  #ifdef ALLOW_CHESTS
  else if (window_id == 2) { // chest

    // overflow chest slots into crafting grid
    // technically invalid, expected to be handled on a per-case basis
    if (slot <= 26) return 41 + slot;
    // the rest of the slots are identical, just shifted by 18
    if (slot >= 27 && slot <= 62) return clientSlotToServerSlot(0, slot - 18);

  }
  #endif

  return 255;
}

int givePlayerItem (PlayerData *player, uint16_t item, uint8_t count) {

  if (item == 0 || count == 0) return 0;

  uint8_t slot = 255;
  uint8_t stack_size = getItemStackSize(item);

  for (int i = 0; i < 41; i ++) {
    if (player->inventory_items[i] == item && player->inventory_count[i] <= stack_size - count) {
      slot = i;
      break;
    }
  }

  if (slot == 255) {
    for (int i = 0; i < 41; i ++) {
      if (player->inventory_count[i] == 0) {
        slot = i;
        break;
      }
    }
  }

  // Fail to assign item if slot is outside of main inventory
  if (slot >= 36) return 1;

  player->inventory_items[slot] = item;
  player->inventory_count[slot] += count;
  sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, slot), player->inventory_count[slot], item);

  return 0;

}

// Sends the full sequence for spawning the player to the client
void spawnPlayer (PlayerData *player) {

  // Player spawn coordinates, initialized to placeholders
  float spawn_x = 8.5f, spawn_y = 80.0f, spawn_z = 8.5f;
  float spawn_yaw = 0.0f, spawn_pitch = 0.0f;

  if (player->flags & 0x02) { // Is this a new player?
    // Determine spawning Y coordinate based on terrain height
    spawn_y = getHeightAt(8, 8) + 1;
    player->y = spawn_y;
    player->flags &= ~0x02;
  } else { // Not a new player
    // Calculate spawn position from player data
    spawn_x = (float)player->x + 0.5;
    spawn_y = player->y;
    spawn_z = (float)player->z + 0.5;
    spawn_yaw = player->yaw * 180 / 127;
    spawn_pitch = player->pitch * 90 / 127;
  }

  // Teleport player to spawn coordinates (first pass)
  sc_synchronizePlayerPosition(player->client_fd, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch);

  task_yield(); // Check task timer between packets

  // Clear crafting grid residue, unlock craft_items
  for (int i = 0; i < 9; i++) {
    player->craft_items[i] = 0;
    player->craft_count[i] = 0;
  }
  player->flags &= ~0x80;

  // Sync client inventory and hotbar
  for (uint8_t i = 0; i < 41; i ++) {
    sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, i), player->inventory_count[i], player->inventory_items[i]);
  }
  sc_setHeldItem(player->client_fd, player->hotbar);
  // Sync client health and hunger
  sc_setHealth(player->client_fd, player->health, player->hunger, player->saturation);
  // Sync client clock time
  sc_updateTime(player->client_fd, world_time);

  #ifdef ENABLE_PLAYER_FLIGHT
  if (GAMEMODE != 1 && GAMEMODE != 3) {
    // Give the player flight (for testing)
    sc_playerAbilities(player->client_fd, 0x04);
  }
  #endif

  // Calculate player's chunk coordinates
  short _x = div_floor(player->x, 16), _z = div_floor(player->z, 16);

  // Indicate that we're about to send chunk data
  sc_setDefaultSpawnPosition(player->client_fd, 8, 80, 8);
  sc_startWaitingForChunks(player->client_fd);
  sc_setCenterChunk(player->client_fd, _x, _z);

  task_yield(); // Check task timer between packets

  // Send spawn chunk first
  sc_chunkDataAndUpdateLight(player->client_fd, _x, _z);
  for (int i = -VIEW_DISTANCE; i <= VIEW_DISTANCE; i ++) {
    for (int j = -VIEW_DISTANCE; j <= VIEW_DISTANCE; j ++) {
      if (i == 0 && j == 0) continue;
      sc_chunkDataAndUpdateLight(player->client_fd, _x + i, _z + j);
    }
  }
  // Re-teleport player after all chunks have been sent
  sc_synchronizePlayerPosition(player->client_fd, spawn_x, spawn_y, spawn_z, spawn_yaw, spawn_pitch);

  task_yield(); // Check task timer between packets

}

// Broadcasts a player's entity metadata (sneak/sprint state) to other players
void broadcastPlayerMetadata (PlayerData *player) {
  uint8_t sneaking = (player->flags & 0x04) != 0;
  uint8_t sprinting = (player->flags & 0x08) != 0;

  uint8_t entity_bit_mask = 0;
  if (sneaking) entity_bit_mask |= 0x02;
  if (sprinting) entity_bit_mask |= 0x08;

  int pose = 0;
  if (sneaking) pose = 5;

  EntityData metadata[] = {
    {
      0,                   // Index (Entity Bit Mask)
      0,                   // Type (Byte)
      { entity_bit_mask }, // Value
    },
    {
      6,        // Index (Pose),
      21,       // Type (Pose),
      { pose }, // Value (Standing)
    }
  };

  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData* other_player = &player_data[i];
    int client_fd = other_player->client_fd;

    if (client_fd == -1) continue;
    if (client_fd == player->client_fd) continue;
    if (other_player->flags & 0x20) continue;

    sc_setEntityMetadata(client_fd, player->client_fd, metadata, 2);
  }
}

// Sends a mob's entity metadata to the given player.
// If client_fd is -1, broadcasts to all player
void broadcastMobMetadata (int client_fd, int entity_id) {

  int mob_index = -entity_id - 2;
  if (mob_index < 0 || mob_index >= MAX_MOBS) return;
  MobData *mob = &mob_data[mob_index];

  EntityData *metadata;
  size_t length;

  switch (mob->type) {
    case 106: // Sheep
      if (!((mob->data >> 5) & 1)) // Don't send metadata if sheep isn't sheared
        return;

      metadata = malloc(sizeof *metadata);
      metadata[0] = (EntityData){
        17,                // Index (Sheep Bit Mask),
        0,                 // Type (Byte),
        { (uint8_t)0x10 }, // Value
      };
      length = 1;

      break;

    default: return;
  }

  if (client_fd == -1) {
    for (int i = 0; i < MAX_PLAYERS; i ++) {
      PlayerData* player = &player_data[i];
      client_fd = player->client_fd;

      if (client_fd == -1) continue;
      if (player->flags & 0x20) continue;

      sc_setEntityMetadata(client_fd, entity_id, metadata, length);
    }
  } else {
    sc_setEntityMetadata(client_fd, entity_id, metadata, length);
  }

  free(metadata);
}

/**
 * Block change lookup index
 * -------------------------
 * Looking up a block change used to scan the whole block_changes array,
 * which happens for every block the server looks at (including on every
 * player movement packet). It now uses a hash table with chaining:
 *  - `bc_head[bucket]` is the first block change index in that bucket,
 *  - `bc_next[i]` is the next index in the same bucket.
 * Every block change entry that holds a real block is in the table. The
 * chest item data entries and unused entries (block == BLOCK_NONE) are not.
 *
 * Whoever changes the coordinates or occupancy of an entry has to keep the
 * index up to date with bcIndexInsert / bcIndexRemove (only makeBlockChange
 * does this), or call rebuildBlockChangeIndex after bulk changes (loading
 * the world from disk, uploading world data).
 */
#define BC_BUCKETS 16384 // Must be a power of 2
#define BC_NIL 0xFFFF

static uint16_t bc_head[BC_BUCKETS];
static BIG_ARRAY_ATTR uint16_t bc_next[MAX_BLOCK_CHANGES];

// All entries below this index are in use. Speeds up finding free entries.
static int bc_gap_hint = 0;

static inline uint32_t bcHash (short x, uint8_t y, short z) {
  uint32_t h = ((uint32_t)(uint16_t)x * 73856093u) ^
               ((uint32_t)y * 19349663u) ^
               ((uint32_t)(uint16_t)z * 83492791u);
  return (h ^ (h >> 15)) & (BC_BUCKETS - 1);
}

static void bcIndexInsert (int i) {
  uint32_t bucket = bcHash(block_changes[i].x, block_changes[i].y, block_changes[i].z);
  bc_next[i] = bc_head[bucket];
  bc_head[bucket] = i;
}

// Must be called while the entry still holds its old coordinates
static void bcIndexRemove (int i) {
  uint32_t bucket = bcHash(block_changes[i].x, block_changes[i].y, block_changes[i].z);
  uint16_t *link = &bc_head[bucket];
  while (*link != BC_NIL) {
    if (*link == i) {
      *link = bc_next[i];
      return;
    }
    link = &bc_next[*link];
  }
}

// Returns the entry index holding the block at the given position, or -1
static int bcFind (short x, uint8_t y, short z) {
  uint16_t i = bc_head[bcHash(x, y, z)];
  while (i != BC_NIL) {
    if (
      block_changes[i].x == x &&
      block_changes[i].y == y &&
      block_changes[i].z == z
    ) return i;
    i = bc_next[i];
  }
  return -1;
}

// Returns the first unused entry (block_changes_count if there is none)
static int bcFirstGap () {
  while (bc_gap_hint < block_changes_count && block_changes[bc_gap_hint].block != BLOCK_NONE) {
    bc_gap_hint ++;
  }
  return bc_gap_hint;
}

static inline void bcNoteFreed (int i) {
  if (i < bc_gap_hint) bc_gap_hint = i;
}

void rebuildBlockChangeIndex () {
  for (int i = 0; i < BC_BUCKETS; i ++) bc_head[i] = BC_NIL;
  bc_gap_hint = 0;
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block == BLOCK_NONE) continue;
    bcIndexInsert(i);
    #ifdef ALLOW_CHESTS
      // Skip chest contents
      if (IS_BLOCK(block_changes[i].block, chest)) i += 14;
    #endif
  }
}

uint16_t getBlockChange (short x, uint8_t y, short z) {
  int i = bcFind(x, y, z);
  return i < 0 ? BLOCK_NONE : block_changes[i].block;
}

// Handle running out of memory for new block changes
void failBlockChange (short x, uint8_t y, short z, uint16_t block) {

  // Get previous block at this location
  uint16_t before = getBlockAt(x, y, z);

  // Broadcast a new update to all players
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    // Reset the block they tried to change
    sc_blockUpdate(player_data[i].client_fd, x, y, z, before);
    // Broadcast a chat message warning about the limit
    sc_systemChat(player_data[i].client_fd, "Block changes limit exceeded. Restore original terrain to continue.", 67);
  }

}

// Remembers that block change entries need to be written to disk
static inline void saveBlockChanges (int from, int to) {
  #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
  writeBlockChangesToDisk(from, to);
  #else
  markBlockChangesDirty(from, to);
  #endif
}

uint8_t makeBlockChange (short x, uint8_t y, short z, uint16_t block) {

  // Transmit block update to all in-game clients
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    sc_blockUpdate(player_data[i].client_fd, x, y, z, block);
  }

  // Calculate terrain at these coordinates and compare it to the input block.
  // Since block changes get overlayed on top of terrain, we don't want to
  // store blocks that don't differ from the base terrain.
  ChunkAnchor anchor = {
    .x = x / CHUNK_SIZE,
    .z = z / CHUNK_SIZE,
    .hash = 0,
    .biome = 0
  };
  if (x % CHUNK_SIZE < 0) anchor.x --;
  if (z % CHUNK_SIZE < 0) anchor.z --;
  anchor.hash = getChunkHash(anchor.x, anchor.z);
  anchor.biome = getChunkBiome(anchor.x, anchor.z);

  uint8_t is_base_block = block == getTerrainAt(x, y, z, anchor);

  #ifdef ALLOW_CHESTS
  uint8_t placing_chest = IS_BLOCK(block, chest);
  #endif

  // Prioritize replacing the entry with matching coordinates.
  // This prevents having conflicting entries for one set of coordinates.
  int existing = bcFind(x, y, z);
  if (existing >= 0) {
    int i = existing;
    int last_changed = i;
    #ifdef ALLOW_CHESTS
    // When replacing chests, clear following 14 entries too (item data)
    if (IS_BLOCK(block_changes[i].block, chest)) {
      for (int j = 1; j < 15; j ++) block_changes[i + j].block = BLOCK_NONE;
      bcNoteFreed(i + 1);
      last_changed = i + 14;
    }
    #endif
    if (is_base_block) {
      bcIndexRemove(i);
      block_changes[i].block = BLOCK_NONE;
      bcNoteFreed(i);
      saveBlockChanges(i, last_changed);
      return 0;
    }
    #ifdef ALLOW_CHESTS
    // When placing chests, just unallocate the target block and fall
    // through to the chest-specific routine below.
    if (placing_chest) {
      bcIndexRemove(i);
      block_changes[i].block = BLOCK_NONE;
      bcNoteFreed(i);
      saveBlockChanges(i, last_changed);
    } else
    #endif
    {
      // Same coordinates, so the entry stays in the lookup index
      block_changes[i].block = block;
      saveBlockChanges(i, last_changed);
      return 0;
    }
  }

  // Don't create a new entry if it contains the base terrain block
  if (is_base_block) return 0;

  // The first unused entry. If there is none, this is block_changes_count.
  int first_gap = bcFirstGap();

  #ifdef ALLOW_CHESTS
  if (placing_chest) {
    // Chests require 15 entries total, so for maximum space-efficiency,
    // we have to find a continuous gap that's at least 15 slots wide.
    // By design, this loop also continues past the current search range,
    // which naturally appends the chest to the end if a gap isn't found.
    int last_real_entry = first_gap - 1;
    for (int i = first_gap; i <= block_changes_count + 15; i ++) {
      if (i >= MAX_BLOCK_CHANGES) break; // No more space, trigger failBlockChange

      if (block_changes[i].block != BLOCK_NONE) {
        last_real_entry = i;
        continue;
      }
      if (i - last_real_entry != 15) continue;
      // A wide enough gap has been found, assign the chest
      block_changes[last_real_entry + 1].x = x;
      block_changes[last_real_entry + 1].y = y;
      block_changes[last_real_entry + 1].z = z;
      block_changes[last_real_entry + 1].block = block;
      bcIndexInsert(last_real_entry + 1);
      // Zero out the following 14 entries for item data
      for (int j = 2; j <= 15; j ++) {
        block_changes[last_real_entry + j].x = 0;
        block_changes[last_real_entry + j].y = 0;
        block_changes[last_real_entry + j].z = 0;
        block_changes[last_real_entry + j].block = 0;
      }
      // Extend future search range if necessary
      if (i >= block_changes_count) {
        block_changes_count = i + 1;
      }
      // Write changes to disk (if applicable)
      saveBlockChanges(last_real_entry + 1, last_real_entry + 15);
      return 0;
    }
    // If we're here, no changes were made
    failBlockChange(x, y, z, block);
    return 1;
  }
  #endif

  // Handle running out of memory for new block changes
  if (first_gap == MAX_BLOCK_CHANGES) {
    failBlockChange(x, y, z, block);
    return 1;
  }

  // Fall back to storing the change at the first possible gap
  block_changes[first_gap].x = x;
  block_changes[first_gap].y = y;
  block_changes[first_gap].z = z;
  block_changes[first_gap].block = block;
  bcIndexInsert(first_gap);
  // Write change to disk (if applicable)
  saveBlockChanges(first_gap, first_gap);
  // Extend future search range if we've appended to the end
  if (first_gap == block_changes_count) {
    block_changes_count ++;
  }

  return 0;
}

// Water and lava come in several "levels", the source being level 0
#define IS_WATER(b) ((b) >= B_water && (b) < B_water + 8)
#define IS_LAVA(b) ((b) >= B_lava && (b) < B_lava + 4)

static uint8_t isAnyPickaxe (uint16_t item) {
  return (
    item == I_wooden_pickaxe || item == I_stone_pickaxe || item == I_iron_pickaxe ||
    item == I_golden_pickaxe || item == I_diamond_pickaxe || item == I_netherite_pickaxe
  );
}

static uint8_t isAnyShovel (uint16_t item) {
  return (
    item == I_wooden_shovel || item == I_stone_shovel || item == I_iron_shovel ||
    item == I_golden_shovel || item == I_diamond_shovel || item == I_netherite_shovel
  );
}

// Returns the result of mining a block, taking into account the block type and tools
// Probability numbers obtained with this formula: N = floor(P * (2 ^ 32))
uint16_t getMiningResult (uint16_t held_item, uint16_t block) {

  const BlockInfo *info = getBlockInfo(block);
  if (info == NULL) return 0;

  if (IS_BLOCK(block, oak_leaves)) {
    if (held_item == I_shears) return I_oak_leaves;
    uint32_t r = fast_rand();
    if (r < 21474836) return I_apple; // 0.5%
    if (r < 85899345) return I_stick; // 2%
    if (r < 214748364) return I_oak_sapling; // 5%
    return 0;
  }

  // Short grass gives seeds, or itself when cut with shears
  if (IS_BLOCK(block, short_grass)) {
    if (held_item == I_shears) return I_short_grass;
    return (fast_rand() & 7) == 0 ? I_wheat_seeds : 0;
  }

  // Ripe wheat gives wheat, anything else gives its seed or vegetable
  if (IS_BLOCK(block, wheat) && getCropAge(block) == 7) return I_wheat;

  // Check for the tools needed to get a drop
  if (info->flags & BF_IRON_PICKAXE) {
    if (
      held_item != I_iron_pickaxe && held_item != I_golden_pickaxe &&
      held_item != I_diamond_pickaxe && held_item != I_netherite_pickaxe
    ) return 0;
  }
  if ((info->flags & BF_PICKAXE) && !isAnyPickaxe(held_item)) return 0;
  if ((info->flags & BF_SHOVEL) && !isAnyShovel(held_item)) return 0;

  return info->item;

}

// How many items mining a block gives
uint8_t getMiningCount (uint16_t block) {
  int type;
  // Ripe carrots and potatoes give 1 to 4
  if ((IS_BLOCK(block, carrots) || IS_BLOCK(block, potatoes)) && getCropAge(block) == 7) {
    uint32_t r = fast_rand();
    return 1 + ((r & 3) != 0) + ((r & 12) != 0) + ((r & 48) != 0);
  }
  // Double slabs are two slabs
  if (getBlockFamily(block) == FAM_SLAB && decodeSlab(block, &type) && type == 2) return 2;
  return 1;
}

// Rolls a random number to determine whether the player's tool should break
void bumpToolDurability (PlayerData *player) {

  uint16_t held_item = player->inventory_items[player->hotbar];

  // In order to avoid storing durability data, items break randomly with
  // the probability weighted based on vanilla durability.
  uint32_t r = fast_rand();
  if (
    ((held_item == I_wooden_pickaxe || held_item == I_wooden_axe || held_item == I_wooden_shovel || held_item == I_wooden_hoe) && r < 72796055) ||
    ((held_item == I_stone_pickaxe || held_item == I_stone_axe || held_item == I_stone_shovel || held_item == I_stone_hoe) && r < 32786009) ||
    ((held_item == I_iron_pickaxe || held_item == I_iron_axe || held_item == I_iron_shovel || held_item == I_iron_hoe) && r < 17179869) ||
    ((held_item == I_golden_pickaxe || held_item == I_golden_axe || held_item == I_golden_shovel || held_item == I_golden_hoe) && r < 134217728) ||
    ((held_item == I_diamond_pickaxe || held_item == I_diamond_axe || held_item == I_diamond_shovel || held_item == I_diamond_hoe) && r < 2751420) ||
    ((held_item == I_netherite_pickaxe || held_item == I_netherite_axe || held_item == I_netherite_shovel || held_item == I_netherite_hoe) && r < 2114705) ||
    (held_item == I_shears && r < 18046081)
  ) {
    player->inventory_items[player->hotbar] = 0;
    player->inventory_count[player->hotbar] = 0;
    sc_entityEvent(player->client_fd, player->client_fd, 47);
    sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), 0, 0);
  }

}

// Checks whether the given block would be mined instantly with the held tool
uint8_t isInstantlyMined (PlayerData *player, uint16_t block) {

  uint16_t held_item = player->inventory_items[player->hotbar];

  if (IS_BLOCK(block, snow) || IS_BLOCK(block, snow_block)) return (
    held_item == I_stone_shovel ||
    held_item == I_iron_shovel ||
    held_item == I_diamond_shovel ||
    held_item == I_netherite_shovel ||
    held_item == I_golden_shovel
  );

  if (IS_BLOCK(block, oak_leaves))
    return held_item == I_shears;

  return (getBlockFlags(block) & BF_INSTAMINE) != 0;

}

// Checks whether the given block has to have something beneath it
uint8_t isColumnBlock (uint16_t block) {
  return (getBlockFlags(block) & BF_COLUMN) != 0;
}

// Checks whether the given block is a door, trapdoor or gate that is open
static uint8_t isOpenPassage (uint8_t family, uint16_t block) {
  int a, b, open;
  switch (family) {
    case FAM_DOOR: return decodeDoor(block, &a, &b, &open) && open;
    case FAM_TRAPDOOR: return decodeTrapdoor(block, &a, &b, &open) && open;
    case FAM_GATE: return decodeGate(block, &a, &open) && open;
    default: return 0;
  }
}

// Checks whether the given block is non-solid
uint8_t isPassableBlock (uint16_t block) {
  if (block == B_air || IS_WATER(block) || IS_LAVA(block)) return 1;
  const BlockInfo *info = getBlockInfo(block);
  if (info == NULL) return 0;
  return (info->flags & BF_PASSABLE) || isOpenPassage(info->family, block);
}

// Checks whether the given block is non-solid and spawnable
uint8_t isPassableSpawnBlock (uint16_t block) {
  if (IS_WATER(block) || IS_LAVA(block)) return 0;
  if (getBlockInfo(block) != NULL && getBlockInfo(block)->family != FAM_NONE) return 0;
  return isPassableBlock(block);
}

// Checks whether the given block can be replaced by another block
uint8_t isReplaceableBlock (uint16_t block) {
  if (block == B_air || IS_WATER(block) || IS_LAVA(block)) return 1;
  return (getBlockFlags(block) & BF_REPLACEABLE) != 0;
}

uint8_t isReplaceableFluid (uint16_t block, uint8_t level, uint16_t fluid) {
  if (block >= fluid && block - fluid < 8) {
    return block - fluid > level;
  }
  return isReplaceableBlock(block);
}

// Whether a water block is close enough to keep farmland wet
// (within 4 blocks sideways, on the farmland's level or one above)
static uint8_t hasWaterNearby (short x, uint8_t y, short z) {
  for (int dx = -4; dx <= 4; dx ++) {
    for (int dz = -4; dz <= 4; dz ++) {
      for (int dy = 0; dy <= 1; dy ++) {
        if (IS_WATER(getBlockAt(x + dx, y + dy, z + dz))) return true;
      }
    }
  }
  return false;
}

static uint8_t isHoeItem (uint16_t item) {
  return
    item == I_wooden_hoe || item == I_stone_hoe || item == I_iron_hoe ||
    item == I_golden_hoe || item == I_diamond_hoe || item == I_netherite_hoe;
}

// Checks whether the given item can be used in a composter
// Returns the probability (out of 2^32) to return bone meal
uint32_t isCompostItem (uint16_t item) {

  // Output values calculated using the following formula:
  // P = 2^32 / (7 / compost_chance)

  if ( // Compost chance: 30%
    item == I_oak_leaves ||
    item == I_short_grass ||
    item == I_wheat_seeds ||
    item == I_oak_sapling ||
    item == I_moss_carpet
  ) return 184070026;

  if ( // Compost chance: 50%
    item == I_cactus ||
    item == I_sugar_cane
  ) return 306783378;

  if ( // Compost chance: 65%
    item == I_apple ||
    item == I_lily_pad ||
    item == I_wheat ||
    item == I_carrot ||
    item == I_potato
  ) return 398818392;

  if ( // Compost chance: 85%
    item == I_bread ||
    item == I_baked_potato
  ) return 521531742;

  return 0;
}

// Returns the maximum stack size of an item
uint8_t getItemStackSize (uint16_t item) {

  if (
    // Pickaxes
    item == I_wooden_pickaxe ||
    item == I_stone_pickaxe ||
    item == I_iron_pickaxe ||
    item == I_golden_pickaxe ||
    item == I_diamond_pickaxe ||
    item == I_netherite_pickaxe ||
    // Axes
    item == I_wooden_axe ||
    item == I_stone_axe ||
    item == I_iron_axe ||
    item == I_golden_axe ||
    item == I_diamond_axe ||
    item == I_netherite_axe ||
    // Shovels
    item == I_wooden_shovel ||
    item == I_stone_shovel ||
    item == I_iron_shovel ||
    item == I_golden_shovel ||
    item == I_diamond_shovel ||
    item == I_netherite_shovel ||
    // Swords
    item == I_wooden_sword ||
    item == I_stone_sword ||
    item == I_iron_sword ||
    item == I_golden_sword ||
    item == I_diamond_sword ||
    item == I_netherite_sword ||
    // Hoes
    item == I_wooden_hoe ||
    item == I_stone_hoe ||
    item == I_iron_hoe ||
    item == I_golden_hoe ||
    item == I_diamond_hoe ||
    item == I_netherite_hoe ||
    // Shears
    item == I_shears
  ) return 1;

  if (
    item == I_snowball
  ) return 16;

  return 64;
}

// Returns defense points for the given piece of armor
// If the input item is not armor, returns 0
uint8_t getItemDefensePoints (uint16_t item) {

  switch (item) {
    case I_leather_helmet: return 1;
    case I_golden_helmet: return 2;
    case I_iron_helmet: return 2;
    case I_diamond_helmet: // Same as netherite
    case I_netherite_helmet: return 3;
    case I_leather_chestplate: return 3;
    case I_golden_chestplate: return 5;
    case I_iron_chestplate: return 6;
    case I_diamond_chestplate: // Same as netherite
    case I_netherite_chestplate: return 8;
    case I_leather_leggings: return 2;
    case I_golden_leggings: return 3;
    case I_iron_leggings: return 5;
    case I_diamond_leggings: // Same as netherite
    case I_netherite_leggings: return 6;
    case I_leather_boots: return 1;
    case I_golden_boots: return 1;
    case I_iron_boots: return 2;
    case I_diamond_boots: // Same as netherite
    case I_netherite_boots: return 3;
    default: break;
  }

  return 0;
}

// Calculates total defense points for the player's equipped armor
uint8_t getPlayerDefensePoints (PlayerData *player) {
  return (
    // Helmet
    getItemDefensePoints(player->inventory_items[39]) +
    // Chestplate
    getItemDefensePoints(player->inventory_items[38]) +
    // Leggings
    getItemDefensePoints(player->inventory_items[37]) +
    // Boots
    getItemDefensePoints(player->inventory_items[36])
  );
}

// Returns the designated server slot for the given piece of armor
// If input item is not armor, returns 255
uint8_t getArmorItemSlot (uint16_t item) {

    switch (item) {
    case I_leather_helmet:
    case I_golden_helmet:
    case I_iron_helmet:
    case I_diamond_helmet:
    case I_netherite_helmet:
      return 39;
    case I_leather_chestplate:
    case I_golden_chestplate:
    case I_iron_chestplate:
    case I_diamond_chestplate:
    case I_netherite_chestplate:
      return 38;
    case I_leather_leggings:
    case I_golden_leggings:
    case I_iron_leggings:
    case I_diamond_leggings:
    case I_netherite_leggings:
      return 37;
    case I_leather_boots:
    case I_golden_boots:
    case I_iron_boots:
    case I_diamond_boots:
    case I_netherite_boots:
      return 36;
    default: break;
  }

  return 255;
}

// Handles the player eating their currently held item
// Returns whether the operation was succesful (item was consumed)
// If `just_check` is set to true, the item doesn't get consumed
uint8_t handlePlayerEating (PlayerData *player, uint8_t just_check) {

  // Exit early if player is unable to eat
  if (player->hunger >= 20) return false;

  uint16_t *held_item = &player->inventory_items[player->hotbar];
  uint8_t *held_count = &player->inventory_count[player->hotbar];

  // Exit early if player isn't holding anything
  if (*held_item == 0 || *held_count == 0) return false;

  uint8_t food = 0;
  uint16_t saturation = 0;

  // The saturation ratio from vanilla to here is about 1:500
  switch (*held_item) {
    case I_chicken: food = 2; saturation = 600; break;
    case I_beef: food = 3; saturation = 900; break;
    case I_porkchop: food = 3; saturation = 300; break;
    case I_mutton: food = 2; saturation = 600; break;
    case I_cooked_chicken: food = 6; saturation = 3600; break;
    case I_cooked_beef: food = 8; saturation = 6400; break;
    case I_cooked_porkchop: food = 8; saturation = 6400; break;
    case I_cooked_mutton: food = 6; saturation = 4800; break;
    case I_rotten_flesh: food = 4; saturation = 0; break;
    case I_apple: food = 4; saturation = 1200; break;
    case I_bread: food = 5; saturation = 3000; break;
    case I_carrot: food = 3; saturation = 1800; break;
    case I_potato: food = 1; saturation = 300; break;
    case I_baked_potato: food = 5; saturation = 3000; break;
    default: break;
  }

  // If just checking the item, return before making any changes
  if (just_check) return food != 0;

  // Apply saturation and food boost
  player->saturation += saturation;
  player->hunger += food;
  if (player->hunger > 20) player->hunger = 20;

  // Consume held item
  *held_count -= 1;
  if (*held_count == 0) *held_item = 0;

  // Update the client of these changes
  sc_entityEvent(player->client_fd, player->client_fd, 9);
  sc_setHealth(player->client_fd, player->health, player->hunger, player->saturation);
  sc_setContainerSlot(
    player->client_fd, 0,
    serverSlotToClientSlot(0, player->hotbar),
    *held_count, *held_item
  );

  return true;
}

void handleFluidMovement (short x, uint8_t y, short z, uint16_t fluid, uint16_t block) {

  // Get fluid level (0-7)
  // The terminology here is a bit different from vanilla:
  // a higher fluid "level" means the fluid has traveled farther
  uint8_t level = block - fluid;

  // Query blocks adjacent to this fluid stream
  uint8_t adjacent[4] = {
    getBlockAt(x + 1, y, z),
    getBlockAt(x - 1, y, z),
    getBlockAt(x, y, z + 1),
    getBlockAt(x, y, z - 1)
  };

  // Handle maintaining connections to a fluid source
  if (level != 0) {
    // Check if this fluid is connected to a block exactly one level lower
    uint8_t connected = false;
    for (int i = 0; i < 4; i ++) {
      if (adjacent[i] == block - 1) {
        connected = true;
        break;
      }
    }
    // If not connected, clear this block and recalculate surrounding flow
    if (!connected) {
      makeBlockChange(x, y, z, B_air);
      checkFluidUpdate(x + 1, y, z, adjacent[0]);
      checkFluidUpdate(x - 1, y, z, adjacent[1]);
      checkFluidUpdate(x, y, z + 1, adjacent[2]);
      checkFluidUpdate(x, y, z - 1, adjacent[3]);
      return;
    }
  }

  // Check if water should flow down, prioritize that over lateral flow
  uint16_t block_below = getBlockAt(x, y - 1, z);
  if (isReplaceableBlock(block_below)) {
    makeBlockChange(x, y - 1, z, fluid);
    return handleFluidMovement(x, y - 1, z, fluid, fluid);
  }

  // Stop flowing laterally at the maximum level
  if (level == 3 && fluid == B_lava) return;
  if (level == 7) return;

  // Handle lateral water flow, increasing level by 1
  if (isReplaceableFluid(adjacent[0], level, fluid)) {
    makeBlockChange(x + 1, y, z, block + 1);
    handleFluidMovement(x + 1, y, z, fluid, block + 1);
  }
  if (isReplaceableFluid(adjacent[1], level, fluid)) {
    makeBlockChange(x - 1, y, z, block + 1);
    handleFluidMovement(x - 1, y, z, fluid, block + 1);
  }
  if (isReplaceableFluid(adjacent[2], level, fluid)) {
    makeBlockChange(x, y, z + 1, block + 1);
    handleFluidMovement(x, y, z + 1, fluid, block + 1);
  }
  if (isReplaceableFluid(adjacent[3], level, fluid)) {
    makeBlockChange(x, y, z - 1, block + 1);
    handleFluidMovement(x, y, z - 1, fluid, block + 1);
  }

}

void checkFluidUpdate (short x, uint8_t y, short z, uint16_t block) {

  uint16_t fluid;
  if (block >= B_water && block < B_water + 8) fluid = B_water;
  else if (block >= B_lava && block < B_lava + 4) fluid = B_lava;
  else return;

  handleFluidMovement(x, y, z, fluid, block);

}

#ifdef ENABLE_PICKUP_ANIMATION
// Plays the item pickup animation with the given item at the given coordinates
void playPickupAnimation (PlayerData *player, uint16_t item, double x, double y, double z) {

  // Spawn a new item entity at the input coordinates
  // ID -1 is safe, as elsewhere it's reserved as a placeholder
  // The player's name is used as the UUID as it's cheap and unique enough
  sc_spawnEntity(player->client_fd, -1, (uint8_t *)player->name, 69, x + 0.5, y + 0.5, z + 0.5, 0, 0);

  // Write a Set Entity Metadata packet for the item
  // There's no packets.c entry for this, as it's not cheaply generalizable
  writeVarInt(player->client_fd, 12 + sizeVarInt(item));
  writeByte(player->client_fd, 0x5C);
  writeVarInt(player->client_fd, -1);

  // Describe slot data array entry
  writeByte(player->client_fd, 8);
  writeByte(player->client_fd, 7);
  // Send slot data
  writeByte(player->client_fd, 1);
  writeVarInt(player->client_fd, item);
  writeByte(player->client_fd, 0);
  writeByte(player->client_fd, 0);
  // Terminate entity metadata array
  writeByte(player->client_fd, 0xFF);

  // Send the Pickup Item packet targeting this entity
  sc_pickupItem(player->client_fd, -1, player->client_fd, 1);

  // Remove the item entity from the client right away
  sc_removeEntity(player->client_fd, -1);

}
#endif

// ---------------------------------------------------------------------------
// Block shapes: blocks that connect to their neighbors (fences, panes, walls),
// things that are attached to other blocks, and things that open and close.
// ---------------------------------------------------------------------------

// Whether a fence, pane or wall (of the given family) connects to a block
static uint8_t connectsTo (uint8_t family, uint16_t block) {
  const BlockInfo *info = getBlockInfo(block);
  if (info == NULL) return 0;
  if (info->flags & BF_SOLID) return 1;
  switch (family) {
    case FAM_FENCE: return info->family == FAM_FENCE || info->family == FAM_GATE;
    case FAM_PANE: return info->family == FAM_PANE || (info->flags & BF_GLASS);
    case FAM_WALL: return info->family == FAM_WALL || info->family == FAM_PANE || info->family == FAM_GATE;
    default: return 0;
  }
}

// Works out the state a fence, pane or wall has at the given position
static uint16_t getConnectedState (const BlockInfo *info, short x, short y, short z) {
  // In the order of the offset tables: north, east, south, west
  uint8_t n = connectsTo(info->family, getBlockAt(x, y, z - 1));
  uint8_t e = connectsTo(info->family, getBlockAt(x + 1, y, z));
  uint8_t sd = connectsTo(info->family, getBlockAt(x, y, z + 1));
  uint8_t w = connectsTo(info->family, getBlockAt(x - 1, y, z));
  switch (info->family) {
    case FAM_FENCE: return info->min + OFF_fence[n][e][sd][w];
    case FAM_PANE: return info->min + OFF_pane[n][e][sd][w];
    case FAM_WALL: {
      // Walls get a post, except where they run in a straight line
      uint8_t up = !((n && sd && !e && !w) || (e && w && !n && !sd));
      return info->min + OFF_wall[up][n][e][sd][w];
    }
    default: return 0;
  }
}

// Updates the fence, pane or wall at the given position, if there is one
static void refreshConnections (short x, short y, short z) {
  uint16_t block = getBlockAt(x, y, z);
  const BlockInfo *info = getBlockInfo(block);
  if (info == NULL) return;
  if (info->family != FAM_FENCE && info->family != FAM_PANE && info->family != FAM_WALL) return;
  uint16_t state = getConnectedState(info, x, y, z);
  if (state != block) makeBlockChange(x, y, z, state);
}

// Updates the connections of everything next to the given position
static void refreshNeighborConnections (short x, short y, short z) {
  refreshConnections(x, y, z - 1);
  refreshConnections(x + 1, y, z);
  refreshConnections(x, y, z + 1);
  refreshConnections(x - 1, y, z);
}

// For things attached to the side of a block (wall torches, ladders):
// returns the direction they face, which points away from the block they
// hang on. Returns -1 for anything else.
static int getAttachedFacing (uint16_t block) {
  const BlockInfo *info = getBlockInfo(block);
  if (info == NULL) return -1;
  for (int f = 0; f < 4; f ++) {
    if (info->family == FAM_WALL_TORCH && OFF_wall_torch[f] == block - info->min) return f;
    if (info->family == FAM_LADDER && OFF_ladder[f] == block - info->min) return f;
  }
  return -1;
}

// Opens or closes the door, trapdoor or fence gate at the given position.
// Returns false if the block is none of those.
static uint8_t toggleOpenable (short x, short y, short z, uint16_t block) {
  const BlockInfo *info = getBlockInfo(block);
  if (info == NULL) return false;
  int facing, half, open;
  switch (info->family) {
    case FAM_DOOR: {
      if (!decodeDoor(block, &facing, &half, &open)) return false;
      // Doors are two blocks tall, both halves swing together
      short other_y = half ? y - 1 : y + 1;
      uint16_t other = getBlockAt(x, other_y, z);
      if (makeBlockChange(x, y, z, info->min + OFF_door[facing][half][!open])) return true;
      int of, oh, oo;
      if (decodeDoor(other, &of, &oh, &oo) && getBlockInfo(other) == info) {
        makeBlockChange(x, other_y, z, info->min + OFF_door[of][oh][!oo]);
      }
      return true;
    }
    case FAM_TRAPDOOR:
      if (!decodeTrapdoor(block, &facing, &half, &open)) return false;
      makeBlockChange(x, y, z, info->min + OFF_trapdoor[facing][half][!open]);
      return true;
    case FAM_GATE:
      if (!decodeGate(block, &facing, &open)) return false;
      makeBlockChange(x, y, z, info->min + OFF_gate[facing][!open]);
      return true;
    default:
      return false;
  }
}

// Removes a door completely, given any one of its halves
static void removeDoor (short x, short y, short z, uint16_t block) {
  int facing, upper, open;
  if (!decodeDoor(block, &facing, &upper, &open)) return;
  short other_y = upper ? y - 1 : y + 1;
  if (getBlockInfo(getBlockAt(x, other_y, z)) == getBlockInfo(block)) {
    makeBlockChange(x, other_y, z, B_air);
  }
}

// Called after a block was removed: removes blocks that depended on it
// (doors standing on it, torches and ladders hanging on it) and updates
// fences, panes and walls that were connected to it.
static void updateAfterRemoval (PlayerData *player, short x, short y, short z) {

  // Hanging on the side of the removed block
  for (int d = 0; d < 4; d ++) {
    short nx = x + dirDX(d), nz = z + dirDZ(d);
    uint16_t neighbor = getBlockAt(nx, y, nz);
    if (getAttachedFacing(neighbor) != d) continue;
    uint16_t drop = getBlockInfo(neighbor)->item;
    makeBlockChange(nx, y, nz, B_air);
    if (drop) givePlayerItem(player, drop, 1);
  }

  // A door standing on the removed block
  uint16_t above = getBlockAt(x, y + 1, z);
  int facing, upper, open;
  if (getBlockFamily(above) == FAM_DOOR && decodeDoor(above, &facing, &upper, &open) && !upper) {
    uint16_t drop = getBlockInfo(above)->item;
    makeBlockChange(x, y + 1, z, B_air);
    makeBlockChange(x, y + 2, z, B_air);
    if (drop) givePlayerItem(player, drop, 1);
  }

  refreshNeighborConnections(x, y, z);

}

void handlePlayerAction (PlayerData *player, int action, short x, short y, short z) {

  // Re-sync slot when player drops an item
  if (action == 3 || action == 4) {
    sc_setContainerSlot(
      player->client_fd, 0,
      serverSlotToClientSlot(0, player->hotbar),
      player->inventory_count[player->hotbar],
      player->inventory_items[player->hotbar]
    );
    return;
  }

  // "Finish eating" action, called any time eating stops
  if (action == 5) {
    // Reset eating timer and clear eating flag
    player->flagval_16 = 0;
    player->flags &= ~0x10;
  }

  // Ignore further actions not pertaining to mining blocks
  if (action != 0 && action != 2) return;

  uint16_t block = getBlockAt(x, y, z);

  // In creative, only the "start mining" action is sent
  // No additional verification is performed, the block is simply removed
  if (action == 0 && GAMEMODE == 1) {
    makeBlockChange(x, y, z, B_air);
    if (getBlockFamily(block) == FAM_DOOR) removeDoor(x, y, z, block);
    updateAfterRemoval(player, x, y, z);
    return;
  }

  // If this is a "start mining" packet, the block must be instamine
  if (action == 0 && !isInstantlyMined(player, block)) return;

  // Don't continue if the block change failed
  if (makeBlockChange(x, y, z, B_air)) return;

  // Doors are two blocks tall, remove the other half too
  if (getBlockFamily(block) == FAM_DOOR) removeDoor(x, y, z, block);

  uint16_t held_item = player->inventory_items[player->hotbar];
  uint16_t item = getMiningResult(held_item, block);
  uint8_t item_count = getMiningCount(block);
  bumpToolDurability(player);

  if (item) {
    #ifdef ENABLE_PICKUP_ANIMATION
    playPickupAnimation(player, item, x, y, z);
    #endif
    givePlayerItem(player, item, item_count);
  }

  // Ripe wheat also gives 1 to 3 seeds
  if (IS_BLOCK(block, wheat) && getCropAge(block) == 7) {
    givePlayerItem(player, I_wheat_seeds, 1 + (fast_rand() % 3));
  }

  // Update nearby fluids
  uint16_t block_above = getBlockAt(x, y + 1, z);
  #ifdef DO_FLUID_FLOW
    checkFluidUpdate(x, y + 1, z, block_above);
    checkFluidUpdate(x - 1, y, z, getBlockAt(x - 1, y, z));
    checkFluidUpdate(x + 1, y, z, getBlockAt(x + 1, y, z));
    checkFluidUpdate(x, y, z - 1, getBlockAt(x, y, z - 1));
    checkFluidUpdate(x, y, z + 1, getBlockAt(x, y, z + 1));
  #endif

  // Check if any blocks above this should break, and if so,
  // iterate upward over all blocks in the column and break them
  uint8_t y_offset = 1;
  while (isColumnBlock(block_above)) {
    // Destroy the next block
    makeBlockChange(x, y + y_offset, z, B_air);
    // Check for item drops *without a tool*
    uint16_t item = getMiningResult(0, block_above);
    if (item) givePlayerItem(player, item, 1);
    // Select the next block in the column
    y_offset ++;
    block_above = getBlockAt(x, y + y_offset, z);
  }

  // Remove things that depended on this block
  updateAfterRemoval(player, x, y, z);

}

// Works out which block state results from placing the given block at the
// given position, based on where the player is looking and what they clicked.
// Returns BLOCK_NONE if the block can't be placed like this.
static uint16_t getPlacedState (
  PlayerData *player, const BlockInfo *info, uint16_t block,
  short x, short y, short z, uint8_t face, uint8_t cursor_y, uint16_t target
) {

  int look = yawToDirection(player->yaw);
  int face_dir = faceToDirection(face);
  // Whether the player clicked the upper half of the side of a block
  uint8_t upper_click = cursor_y > 127;

  switch (info->family) {

    case FAM_DOOR:
      return info->min + OFF_door[look][0][0];

    case FAM_TRAPDOOR: {
      int facing, top;
      if (face_dir >= 0) {
        facing = face_dir;
        top = upper_click;
      } else {
        facing = dirOpposite(look);
        top = face == 0;
      }
      return info->min + OFF_trapdoor[facing][top][0];
    }

    case FAM_GATE:
      return info->min + OFF_gate[look][0];

    case FAM_STAIRS:
      return info->min + OFF_stairs[look][face == 0 || (face_dir >= 0 && upper_click)];

    case FAM_SLAB:
      return info->min + OFF_slab[face == 0 || (face_dir >= 0 && upper_click)];

    case FAM_LOG: {
      int axis = face_dir < 0 ? 1 : (face_dir == DIR_NORTH || face_dir == DIR_SOUTH) ? 2 : 0;
      return info->min + OFF_log[axis];
    }

    case FAM_LADDER:
      // Ladders hang on the side of a solid block
      if (face_dir < 0 || !(getBlockFlags(target) & BF_SOLID)) return BLOCK_NONE;
      return info->min + OFF_ladder[face_dir];

    case FAM_TORCH:
      // Torches stick to the side of solid blocks, or stand on the ground
      if (face == 0) return BLOCK_NONE;
      if (face_dir >= 0 && (getBlockFlags(target) & BF_SOLID)) {
        const BlockInfo *wall_torch = getBlockInfo(B_wall_torch);
        return wall_torch->min + OFF_wall_torch[face_dir];
      }
      return block;

    case FAM_CHEST:
      return info->min + OFF_chest[dirOpposite(look)];

    case FAM_FURNACE:
      return info->min + OFF_furnace[dirOpposite(look)];

    case FAM_LANTERN:
      return info->min + OFF_lantern[face == 0];

    case FAM_FENCE:
    case FAM_PANE:
    case FAM_WALL:
      return getConnectedState(info, x, y, z);

    default:
      return block;

  }

}

void handlePlayerUseItem (PlayerData *player, short x, short y, short z, uint8_t face, uint8_t cursor_y) {

  // Get targeted block (if coordinates are provided)
  uint16_t target = face == 255 ? B_air : getBlockAt(x, y, z);
  // Get held item properties
  uint8_t *count = &player->inventory_count[player->hotbar];
  uint16_t *item = &player->inventory_items[player->hotbar];

  // Check interaction with containers when not sneaking
  if (!(player->flags & 0x04) && face != 255) {
    if (IS_BLOCK(target, crafting_table)) {
      sc_openScreen(player->client_fd, 12, "Crafting", 8);
      return;
    } else if (IS_BLOCK(target, furnace)) {
      sc_openScreen(player->client_fd, 14, "Furnace", 7);
      return;
    } else if (toggleOpenable(x, y, z, target)) {
      return;
    } else if (IS_BLOCK(target, composter)) {
      // Check if the player is holding anything
      if (*count == 0) return;
      // Check if the item is a valid compost item
      uint32_t compost_chance = isCompostItem(*item);
      if (compost_chance != 0) {
        // Take away composted item
        if ((*count -= 1) == 0) *item = 0;
        sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
        // Test compost chance and give bone meal on success
        if (fast_rand() < compost_chance) {
          givePlayerItem(player, I_bone_meal, 1);
        }
        return;
      }
    }
    #ifdef ALLOW_CHESTS
    else if (IS_BLOCK(target, chest)) {
      // Get a pointer to the entry following this chest in block_changes
      uint8_t *storage_ptr = NULL;
      for (int i = 0; i < block_changes_count; i ++) {
        if (!IS_BLOCK(block_changes[i].block, chest)) continue;
        if (block_changes[i].x != x || block_changes[i].y != y || block_changes[i].z != z) continue;
        storage_ptr = (uint8_t *)(&block_changes[i + 1]);
        break;
      }
      if (storage_ptr == NULL) return;
      // Terrible memory hack!!
      // Copy the pointer into the player's crafting table item array.
      // This allows us to save some memory by repurposing a feature that
      // is mutually exclusive with chests, though it is otherwise a
      // terrible idea for obvious reasons.
      memcpy(player->craft_items, &storage_ptr, sizeof(storage_ptr));
      // Flag craft_items as locked due to holding a pointer
      player->flags |= 0x80;
      // Show the player the chest UI
      sc_openScreen(player->client_fd, 2, "Chest", 5);
      // Load the slots of the chest from the block_changes array.
      // This is a similarly dubious memcpy hack, but at least we're not
      // mixing data types? Kind of?
      for (int i = 0; i < 27; i ++) {
        uint16_t item;
        uint8_t count;
        memcpy(&item, storage_ptr + i * 3, 2);
        memcpy(&count, storage_ptr + i * 3 + 2, 1);
        sc_setContainerSlot(player->client_fd, 2, i, count, item);
      }
      return;
    }
    #endif
  }

  // If the selected slot doesn't hold any items, exit
  if (*count == 0) return;

  // Check special item handling
  if (*item == I_bone_meal) {
    uint16_t target_below = getBlockAt(x, y - 1, z);
    if (IS_BLOCK(target, oak_sapling)) {
      // Consume the bone meal (yes, even before checks)
      // Wasting bone meal on misplanted saplings is vanilla behavior
      if ((*count -= 1) == 0) *item = 0;
      sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
      if ( // Saplings can only grow when placed on these blocks
        target_below == B_dirt ||
        target_below == B_grass_block ||
        target_below == B_snowy_grass_block ||
        target_below == B_mud
      ) {
        // Bone meal has a 25% chance of growing a tree from a sapling
        if ((fast_rand() & 3) == 0) placeTreeStructure(x, y, z);
      }
    } else if (getCropAge(target) >= 0 && getCropAge(target) < 7) {
      // Bone meal advances crops by 2 to 5 growth stages
      if ((*count -= 1) == 0) *item = 0;
      sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
      makeBlockChange(x, y, z, getCropState(target, getCropAge(target) + 2 + (fast_rand() & 3)));
    }
  } else if (isHoeItem(*item)) {
    // Hoes turn dirt and grass into farmland, if nothing is on top of it
    if (face == 255 || face == 0) return;
    if (
      (IS_BLOCK(target, dirt) || IS_BLOCK(target, grass_block)) &&
      getBlockAt(x, y + 1, z) == B_air
    ) {
      uint8_t wet = hasWaterNearby(x, y, z);
      if (!makeBlockChange(x, y, z, BMIN_farmland + OFF_farmland[wet ? 7 : 0])) {
        bumpToolDurability(player);
      }
    }
    return;
  } else if (!(face == 1 && getBlockFamily(target) == FAM_FARMLAND) && handlePlayerEating(player, true)) {
    // Reset eating timer and set eating flag
    player->flagval_16 = 0;
    player->flags |= 0x10;
  } else if (getItemDefensePoints(*item) != 0) {
    // For some reason, this action is sent twice when looking at a block
    // Ignore the variant that has coordinates
    if (face != 255) return;
    // Swap to held piece of armor
    uint8_t slot = getArmorItemSlot(*item);
    uint16_t prev_item = player->inventory_items[slot];
    player->inventory_items[slot] = *item;
    player->inventory_count[slot] = 1;
    player->inventory_items[player->hotbar] = prev_item;
    player->inventory_count[player->hotbar] = 1;
    // Update client inventory
    sc_setContainerSlot(player->client_fd, -2, serverSlotToClientSlot(0, slot), 1, *item);
    sc_setContainerSlot(player->client_fd, -2, serverSlotToClientSlot(0, player->hotbar), 1, prev_item);
    return;
  }

  // Don't proceed with block placement if no coordinates were provided
  if (face == 255) return;

  // If the selected item doesn't correspond to a block, exit
  uint16_t block = I_to_B(*item);
  const BlockInfo *info = getBlockInfo(block);
  if (block == 0 || info == NULL) return;

  // Two slabs of the same kind combine into a double slab
  int slab_type;
  if (
    info->family == FAM_SLAB && getBlockInfo(target) == info &&
    decodeSlab(target, &slab_type) &&
    ((slab_type == 0 && face == 1) || (slab_type == 1 && face == 0))
  ) {
    if (!makeBlockChange(x, y, z, info->min + OFF_slab[2])) {
      if ((*count -= 1) == 0) *item = 0;
    }
    sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);
    return;
  }

  // The block ends up next to the one that was clicked
  short px = x, pz = z, py = y;
  switch (face) {
    case 0: py -= 1; break;
    case 1: py += 1; break;
    case 2: pz -= 1; break;
    case 3: pz += 1; break;
    case 4: px -= 1; break;
    case 5: px += 1; break;
    default: break;
  }

  uint16_t state = getPlacedState(player, info, block, px, py, pz, face, cursor_y, target);
  uint8_t is_door = info->family == FAM_DOOR;

  if (
    state != BLOCK_NONE &&
    py >= 1 && py <= 250 &&
    isReplaceableBlock(getBlockAt(px, py, pz)) &&
    (!is_door || (
      isReplaceableBlock(getBlockAt(px, py + 1, pz)) &&
      !isPassableBlock(getBlockAt(px, py - 1, pz))
    )) &&
    !( // Is player in the way?
      !isPassableBlock(state) &&
      px == player->x && pz == player->z &&
      (py == player->y || py == player->y + 1 || (is_door && py + 1 == player->y))
    ) &&
    (!isColumnBlock(state) || getBlockAt(px, py - 1, pz) != B_air) &&
    (info->family != FAM_CROP || (face == 1 && getBlockFamily(target) == FAM_FARMLAND)) &&
    (info->family != FAM_LANTERN || getBlockAt(px, py + (face == 0 ? 1 : -1), pz) != B_air)
  ) {
    // Apply server-side block change
    if (makeBlockChange(px, py, pz, state)) return;
    if (is_door) {
      int facing, upper, open;
      decodeDoor(state, &facing, &upper, &open);
      if (makeBlockChange(px, py + 1, pz, info->min + OFF_door[facing][1][0])) {
        makeBlockChange(px, py, pz, B_air);
        return;
      }
    }
    // Planting a carrot or potato must not also start eating it
    if (info->family == FAM_CROP) {
      player->flags &= ~0x10;
      player->flagval_16 = 0;
    }
    // Decrease item amount in selected slot
    *count -= 1;
    // Clear item id in slot if amount is zero
    if (*count == 0) *item = 0;
    // Fences, panes and walls next to the new block need to connect to it
    refreshNeighborConnections(px, py, pz);
    // Calculate fluid flow
    #ifdef DO_FLUID_FLOW
      checkFluidUpdate(px, py + 1, pz, getBlockAt(px, py + 1, pz));
      checkFluidUpdate(px - 1, py, pz, getBlockAt(px - 1, py, pz));
      checkFluidUpdate(px + 1, py, pz, getBlockAt(px + 1, py, pz));
      checkFluidUpdate(px, py, pz - 1, getBlockAt(px, py, pz - 1));
      checkFluidUpdate(px, py, pz + 1, getBlockAt(px, py, pz + 1));
    #endif
  }

  // Sync hotbar contents to player
  sc_setContainerSlot(player->client_fd, 0, serverSlotToClientSlot(0, player->hotbar), *count, *item);

}

void spawnMob (uint8_t type, short x, uint8_t y, short z, uint8_t health) {

  for (int i = 0; i < MAX_MOBS; i ++) {
    // Look for type 0 (unallocated)
    if (mob_data[i].type != 0) continue;

    // Assign it the input parameters
    mob_data[i].type = type;
    mob_data[i].x = x;
    mob_data[i].y = y;
    mob_data[i].z = z;
    mob_data[i].data = health & 31;

    // Forge a UUID from a random number and the mob's index
    uint8_t uuid[16];
    uint32_t r = fast_rand();
    memcpy(uuid, &r, 4);
    memcpy(uuid + 4, &i, 4);

    // Broadcast entity creation to all players
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (player_data[j].client_fd == -1) continue;
      sc_spawnEntity(
        player_data[j].client_fd,
        -2 - i, // Use negative IDs to avoid conflicts with player IDs
        uuid, // Use the UUID generated above
        type, (double)x + 0.5f, y, (double)z + 0.5f,
        // Face opposite of the player, as if looking at them when spawning
        (player_data[j].yaw + 127) & 255, 0
      );
    }

    // Freshly spawned mobs currently don't need metadata updates.
    // If this changes, uncomment this line.
    // broadcastMobMetadata(-1, i);

    break;
  }

}

void interactEntity (int entity_id, int interactor_id) {

  PlayerData *player;
  if (getPlayerData(interactor_id, &player)) return;

  int mob_index = -entity_id - 2;
  if (mob_index < 0 || mob_index >= MAX_MOBS) return;
  MobData *mob = &mob_data[mob_index];

  switch (mob->type) {
    case 106: // Sheep
      if (player->inventory_items[player->hotbar] != I_shears)
        return;

      if ((mob->data >> 5) & 1) // Check if sheep has already been sheared
        return;

      mob->data |= 1 << 5; // Set sheared to true

      bumpToolDurability(player);

      #ifdef ENABLE_PICKUP_ANIMATION
      playPickupAnimation(player, I_white_wool, mob->x, mob->y, mob->z);
      #endif

      uint8_t item_count = 1 + (fast_rand() & 1); // 1-2
      givePlayerItem(player, I_white_wool, item_count);

      for (int i = 0; i < MAX_PLAYERS; i ++) {
        PlayerData* player = &player_data[i];
        int client_fd = player->client_fd;

        if (client_fd == -1) continue;
        if (player->flags & 0x20) continue;

        sc_entityAnimation(client_fd, interactor_id, 0);
      }

      broadcastMobMetadata(-1, entity_id);

      break;
  }
}

void hurtEntity (int entity_id, int attacker_id, uint8_t damage_type, uint8_t damage) {

  if (attacker_id > 0) { // Attacker is a player

    PlayerData *player;
    if (getPlayerData(attacker_id, &player)) return;

    // Check if attack cooldown flag is set
    if (player->flags & 0x01) return;

    // Scale damage based on held item
    uint16_t held_item = player->inventory_items[player->hotbar];
    if (held_item == I_wooden_sword) damage *= 4;
    else if (held_item == I_golden_sword) damage *= 4;
    else if (held_item == I_stone_sword) damage *= 5;
    else if (held_item == I_iron_sword) damage *= 6;
    else if (held_item == I_diamond_sword) damage *= 7;
    else if (held_item == I_netherite_sword) damage *= 8;

    // Enable attack cooldown
    player->flags |= 0x01;
    player->flagval_8 = 0;

  }

  // Whether this attack caused the target entity to die
  uint8_t entity_died = false;

  if (entity_id > 0) { // The attacked entity is a player

    PlayerData *player;
    if (getPlayerData(entity_id, &player)) return;

    // Don't continue if the player is already dead
    if (player->health == 0) return;

    // Calculate damage reduction from player's armor
    uint8_t defense = getPlayerDefensePoints(player);
    // This uses the old (pre-1.9) protection calculation. Factors are
    // scaled up 256 times to avoid floating point math. Due to lost
    // precision, the 4% reduction factor drops to ~3.9%, although the
    // the resulting effective damage is then also rounded down.
    uint8_t effective_damage = damage * (256 - defense * 10) / 256;

    // Process health change on the server
    if (player->health <= effective_damage) {

      player->health = 0;
      entity_died = true;

      // Prepare death message in recv_buffer
      uint8_t player_name_len = strlen(player->name);
      strcpy((char *)recv_buffer, player->name);

      if (damage_type == D_fall && damage > 8) {
        // Killed by a greater than 5 block fall
        strcpy((char *)recv_buffer + player_name_len, " fell from a high place");
        recv_buffer[player_name_len + 23] = '\0';
      } else if (damage_type == D_fall) {
        // Killed by a less than 5 block fall
        strcpy((char *)recv_buffer + player_name_len, " hit the ground too hard");
        recv_buffer[player_name_len + 24] = '\0';
      } else if (damage_type == D_lava) {
        // Killed by being in lava
        strcpy((char *)recv_buffer + player_name_len, " tried to swim in lava");
        recv_buffer[player_name_len + 22] = '\0';
      } else if (attacker_id < -1) {
        // Killed by a mob
        strcpy((char *)recv_buffer + player_name_len, " was slain by a mob");
        recv_buffer[player_name_len + 19] = '\0';
      } else if (attacker_id > 0) {
        // Killed by a player
        PlayerData *attacker;
        if (getPlayerData(attacker_id, &attacker)) return;
        strcpy((char *)recv_buffer + player_name_len, " was slain by ");
        strcpy((char *)recv_buffer + player_name_len + 14, attacker->name);
        recv_buffer[player_name_len + 14 + strlen(attacker->name)] = '\0';
      } else if (damage_type == D_cactus) {
        // Killed by being near a cactus
        strcpy((char *)recv_buffer + player_name_len, " was pricked to death");
        recv_buffer[player_name_len + 21] = '\0';
      } else {
        // Unknown death reason
        strcpy((char *)recv_buffer + player_name_len, " died");
        recv_buffer[player_name_len + 5] = '\0';
      }

    } else player->health -= effective_damage;

    // Update health on the client
    sc_setHealth(entity_id, player->health, player->hunger, player->saturation);

  } else { // The attacked entity is a mob

    int mob_index = -entity_id - 2;
    if (mob_index < 0 || mob_index >= MAX_MOBS) return;
    MobData *mob = &mob_data[mob_index];

    uint8_t mob_health = mob->data & 31;

    // Don't continue if the mob is already dead
    if (mob_health == 0) return;

    // Set the mob's panic timer
    mob->data |= (3 << 6);

    // Process health change on the server
    if (mob_health <= damage) {

      mob->data -= mob_health;
      mob->y = 0;
      entity_died = true;

      // Handle mob drops
      if (attacker_id > 0) {
        PlayerData *player;
        if (getPlayerData(attacker_id, &player)) return;
        switch (mob->type) {
          case 25: givePlayerItem(player, I_chicken, 1); break;
          case 28: givePlayerItem(player, I_beef, 1 + (fast_rand() % 3)); break;
          case 95: givePlayerItem(player, I_porkchop, 1 + (fast_rand() % 3)); break;
          case 106: givePlayerItem(player, I_mutton, 1 + (fast_rand() & 1)); break;
          case 145:
            givePlayerItem(player, I_rotten_flesh, (fast_rand() % 3));
            // Rare vegetable drops (there are no villages to find them in)
            if (fast_rand() < 107374182) givePlayerItem(player, I_carrot, 1);
            else if (fast_rand() < 107374182) givePlayerItem(player, I_potato, 1);
            break;
          default: break;
        }
      }

    } else mob->data -= damage;

  }

  // Broadcast damage event to all players
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    int client_fd = player_data[i].client_fd;
    if (client_fd == -1) continue;
    sc_damageEvent(client_fd, entity_id, damage_type);
    // Below this, handle death events
    if (!entity_died) continue;
    sc_entityEvent(client_fd, entity_id, 3);
    if (entity_id >= 0) {
      // If a player died, broadcast their death message
      sc_systemChat(client_fd, (char *)recv_buffer, strlen((char *)recv_buffer));
    }
  }

}

// Grows crops and keeps farmland wet or dries it out. Called every few
// seconds. Only a limited number of plants is looked at per call, so big farms
// grow a little slower rather than stalling the server. `speed` multiplies
// the growth chances (1 for normal play).
#define FARM_BUDGET 48
void tickFarming (uint32_t speed) {

  struct { short x; uint8_t y; short z; uint16_t state; } pending[FARM_BUDGET];
  int n_pending = 0, looked_at = 0;

  static int scan_pos = 0;
  if (scan_pos >= block_changes_count) scan_pos = 0;

  int i = scan_pos;
  for (; i < block_changes_count && looked_at < FARM_BUDGET; i ++) {
    uint16_t block = block_changes[i].block;
    if (block == BLOCK_NONE) continue;
    #ifdef ALLOW_CHESTS
      if (IS_BLOCK(block, chest)) { i += 14; continue; }
    #endif
    uint8_t family = getBlockFamily(block);
    if (family != FAM_CROP && family != FAM_FARMLAND) continue;
    looked_at ++;

    short x = block_changes[i].x, z = block_changes[i].z;
    uint8_t y = block_changes[i].y;

    if (family == FAM_FARMLAND) {
      int moisture = getFarmlandMoisture(block);
      uint16_t above = getBlockAt(x, y + 1, z);
      uint16_t next = block;
      if (hasWaterNearby(x, y, z)) {
        if (moisture != 7) next = BMIN_farmland + OFF_farmland[7];
      } else if (moisture > 0) {
        if (fast_rand() % 12 == 0) next = BMIN_farmland + OFF_farmland[moisture - 1];
      } else if (getBlockFamily(above) != FAM_CROP) {
        // Dried out with nothing growing on it
        next = B_dirt;
      }
      if (next != block && n_pending < FARM_BUDGET) {
        pending[n_pending].x = x; pending[n_pending].y = y; pending[n_pending].z = z;
        pending[n_pending].state = next;
        n_pending ++;
      }
    } else {
      int age = getCropAge(block);
      if (age >= 7) continue;
      uint16_t soil = getBlockAt(x, y - 1, z);
      if (getBlockFamily(soil) != FAM_FARMLAND) continue;
      // Wet farmland: a stage about every 8 minutes. Dry farmland: half as fast.
      uint32_t chance = getFarmlandMoisture(soil) > 0 ? 44739243 : 22369621;
      uint64_t scaled = (uint64_t)chance * speed;
      if (fast_rand() < (scaled > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)scaled)) {
        if (n_pending < FARM_BUDGET) {
          pending[n_pending].x = x; pending[n_pending].y = y; pending[n_pending].z = z;
          pending[n_pending].state = getCropState(block, age + 1);
          n_pending ++;
        }
      }
    }
  }
  scan_pos = i;

  // Apply the changes after scanning, as they can reshuffle block_changes
  for (int j = 0; j < n_pending; j ++) {
    makeBlockChange(pending[j].x, pending[j].y, pending[j].z, pending[j].state);
  }

}

// Simulates events scheduled for regular intervals
// Takes the time since the last tick in microseconds as the only arguemnt
void handleServerTick (int64_t time_since_last_tick) {

  #ifdef DEV_LOG_STALLS
    int64_t tick_t0 = get_program_time(), tick_t1 = 0, tick_t2 = 0;
  #endif


  // Update world time
  world_time = (world_time + time_since_last_tick / 50000) % 24000;
  // Increment server tick counter
  server_ticks ++;

  // Grow crops every 5 seconds
  if (server_ticks % (uint32_t)(5 * TICKS_PER_SECOND) == 0) tickFarming(1);

  // Update player events
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    PlayerData *player = &player_data[i];
    if (player->client_fd == -1) continue; // Skip offline players
    if (player->flags & 0x20) { // Check "client loading" flag
      // If 3 seconds (60 vanilla ticks) have passed, assume player has loaded
      player->flagval_16 ++;
      if (player->flagval_16 > (uint16_t)(3 * TICKS_PER_SECOND)) {
        handlePlayerJoin(player);
      } else continue;
    }
    // Reset player attack cooldown
    if (player->flags & 0x01) {
      if (player->flagval_8 >= (uint8_t)(0.6f * TICKS_PER_SECOND)) {
        player->flags &= ~0x01;
        player->flagval_8 = 0;
      } else player->flagval_8 ++;
    }
    // Handle eating animation
    if (player->flags & 0x10) {
      if (player->flagval_16 >= (uint16_t)(1.6f * TICKS_PER_SECOND)) {
        handlePlayerEating(&player_data[i], false);
        player->flags &= ~0x10;
        player->flagval_16 = 0;
      } else player->flagval_16 ++;
    }
    // Reset movement update cooldown if not broadcasting every update
    // Effectively ties player movement updates to the tickrate
    #ifndef BROADCAST_ALL_MOVEMENT
      player->flags &= ~0x40;
    #endif
    // Below this, process events that happen once per second
    if (server_ticks % (uint32_t)TICKS_PER_SECOND != 0) continue;
    // Send Keep Alive and Update Time packets
    sc_keepAlive(player->client_fd);
    sc_updateTime(player->client_fd, world_time);
    // Tick damage from lava
    uint16_t block = getBlockAt(player->x, player->y, player->z);
    if (block >= B_lava && block < B_lava + 4) {
      hurtEntity(player->client_fd, -1, D_lava, 8);
    }
    #ifdef ENABLE_CACTUS_DAMAGE
    // Tick damage from a cactus block if one is under/inside or around the player.
    if (block == B_cactus ||
      getBlockAt(player->x + 1, player->y, player->z) == B_cactus ||
      getBlockAt(player->x - 1, player->y, player->z) == B_cactus ||
      getBlockAt(player->x, player->y, player->z + 1) == B_cactus ||
      getBlockAt(player->x, player->y, player->z - 1) == B_cactus
    ) hurtEntity(player->client_fd, -1, D_cactus, 4);
    #endif
    // Heal from saturation if player is able and has enough food
    if (player->health >= 20 || player->health == 0) continue;
    if (player->hunger < 18) continue;
    if (player->saturation >= 600) {
      player->saturation -= 600;
      player->health ++;
    } else {
      player->hunger --;
      player->health ++;
    }
    sc_setHealth(player->client_fd, player->health, player->hunger, player->saturation);
  }

  // Perform regular checks for if it's time to write to disk
  #ifdef DEV_LOG_STALLS
    tick_t1 = get_program_time();
  #endif
  writeDataToDiskOnInterval();
  #ifdef DEV_LOG_STALLS
    tick_t2 = get_program_time();
  #endif

  /**
   * If the RNG seed ever hits 0, it'll never generate anything
   * else. This is because the fast_rand function uses a simple
   * XORshift. This isn't a common concern, so we only check for
   * this periodically. If it does become zero, we reset it to
   * the world seed as a good-enough fallback.
   */
  if (rng_seed == 0) rng_seed = world_seed;

  // Tick mob behavior
  for (int i = 0; i < MAX_MOBS; i ++) {
    if (mob_data[i].type == 0) continue;
    int entity_id = -2 - i;

    // Handle deallocation on mob death
    if ((mob_data[i].data & 31) == 0) {
      if (mob_data[i].y < (unsigned int)TICKS_PER_SECOND) {
        mob_data[i].y ++;
        continue;
      }
      mob_data[i].type = 0;
      for (int j = 0; j < MAX_PLAYERS; j ++) {
        if (player_data[j].client_fd == -1) continue;
        // Spawn death smoke particles
        sc_entityEvent(player_data[j].client_fd, entity_id, 60);
        // Remove the entity from the client
        sc_removeEntity(player_data[j].client_fd, entity_id);
      }
      continue;
    }

    uint8_t passive = (
      mob_data[i].type == 25 || // Chicken
      mob_data[i].type == 28 || // Cow
      mob_data[i].type == 95 || // Pig
      mob_data[i].type == 106 // Sheep
    );
    // Mob "panic" timer, set to 3 after being hit
    // Currently has no effect on hostile mobs
    uint8_t panic = (mob_data[i].data >> 6) & 3;

    // Burn hostile mobs if above ground during sunlight
    if (!passive && (world_time < 13000 || world_time > 23460) && mob_data[i].y > 48) {
      hurtEntity(entity_id, -1, D_on_fire, 2);
    }

    uint32_t r = fast_rand();

    if (passive) {
      if (panic) {
        // If panicking, move randomly at up to 4 times per second
        if (TICKS_PER_SECOND >= 4) {
          uint32_t ticks_per_panic = (uint32_t)(TICKS_PER_SECOND / 4);
          if (server_ticks % ticks_per_panic != 0) continue;
        }
        // Reset panic state after timer runs out
        // Each panic timer tick takes one second
        if (server_ticks % (uint32_t)TICKS_PER_SECOND == 0) {
          mob_data[i].data -= (1 << 6);
        }
      } else {
        // When not panicking, move idly once per 4 seconds on average
        if (r % (4 * (unsigned int)TICKS_PER_SECOND) != 0) continue;
      }
    } else {
      // Update hostile mobs once per second
      if (server_ticks % (uint32_t)TICKS_PER_SECOND != 0) continue;
    }

    // Find the player closest to this mob
    PlayerData* closest_player = &player_data[0];
    uint32_t closest_dist = 2147483647;
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (player_data[j].client_fd == -1) continue;
      uint16_t curr_dist = (
        abs(mob_data[i].x - player_data[j].x) +
        abs(mob_data[i].z - player_data[j].z)
      );
      if (curr_dist < closest_dist) {
        closest_dist = curr_dist;
        closest_player = &player_data[j];
      }
    }

    // Despawn mobs past a certain distance from nearest player
    if (closest_dist > MOB_DESPAWN_DISTANCE) {
      mob_data[i].type = 0;
      continue;
    }

    short old_x = mob_data[i].x, old_z = mob_data[i].z;
    uint8_t old_y = mob_data[i].y;

    short new_x = old_x, new_z = old_z;
    uint8_t new_y = old_y, yaw = 0;

    if (passive) { // Passive mob movement handling

      // Move by one block on the X or Z axis
      // Yaw is set to face in the direction of motion
      if ((r >> 2) & 1) {
        if ((r >> 1) & 1) { new_x += 1; yaw = 192; }
        else { new_x -= 1; yaw = 64; }
      } else {
        if ((r >> 1) & 1) { new_z += 1; yaw = 0; }
        else { new_z -= 1; yaw = 128; }
      }

    } else { // Hostile mob movement handling

      // If we're already next to the player, hurt them and skip movement
      if (closest_dist < 3 && abs(old_y - closest_player->y) < 2) {
        hurtEntity(closest_player->client_fd, entity_id, D_generic, 6);
        continue;
      }

      // Move towards the closest player on 8 axis
      // The condition nesting ensures a correct yaw at 45 degree turns
      if (closest_player->x < old_x) {
        new_x -= 1; yaw = 64;
        if (closest_player->z < old_z) { new_z -= 1; yaw += 32; }
        else if (closest_player->z > old_z) { new_z += 1; yaw -= 32; }
      }
      else if (closest_player->x > old_x) {
        new_x += 1; yaw = 192;
        if (closest_player->z < old_z) { new_z -= 1; yaw -= 32; }
        else if (closest_player->z > old_z) { new_z += 1; yaw += 32; }
      } else {
        if (closest_player->z < old_z) { new_z -= 1; yaw = 128; }
        else if (closest_player->z > old_z) { new_z += 1; yaw = 0; }
      }

    }

    // Holds the block that the mob is moving into
    uint16_t block = getBlockAt(new_x, new_y, new_z);
    // Holds the block above the target block, i.e. the "head" block
    uint16_t block_above = getBlockAt(new_x, new_y + 1, new_z);

    // Validate movement on X axis
    if (new_x != old_x && (
      !isPassableBlock(getBlockAt(new_x, new_y + 1, old_z)) ||
      (
        !isPassableBlock(getBlockAt(new_x, new_y, old_z)) &&
        !isPassableBlock(getBlockAt(new_x, new_y + 2, old_z))
      )
    )) {
      new_x = old_x;
      block = getBlockAt(old_x, new_y, new_z);
      block_above = getBlockAt(old_x, new_y + 1, new_z);
    }
    // Validate movement on Z axis
    if (new_z != old_z && (
      !isPassableBlock(getBlockAt(old_x, new_y + 1, new_z)) ||
      (
        !isPassableBlock(getBlockAt(old_x, new_y, new_z)) &&
        !isPassableBlock(getBlockAt(old_x, new_y + 2, new_z))
      )
    )) {
      new_z = old_z;
      block = getBlockAt(new_x, new_y, old_z);
      block_above = getBlockAt(new_x, new_y + 1, old_z);
    }
    // Validate diagonal movement
    if (new_x != old_x && new_z != old_z && (
      !isPassableBlock(block_above) ||
      (
        !isPassableBlock(block) &&
        !isPassableBlock(getBlockAt(new_x, new_y + 2, new_z))
      )
    )) {
      // We know that movement along just one axis is fine thanks to the
      // checks above, pick one based on proximity.
      int dist_x = abs(old_x - closest_player->x);
      int dist_z = abs(old_z - closest_player->z);
      if (dist_x < dist_z) new_z = old_z;
      else new_x = old_x;
      block = getBlockAt(new_x, new_y, new_z);
    }

    // Check if we're supposed to climb/drop one block
    // The checks above already ensure that there's enough space to climb
    if (!isPassableBlock(block)) new_y += 1;
    else if (isPassableBlock(getBlockAt(new_x, new_y - 1, new_z))) new_y -= 1;

    // Exit early if all movement was cancelled
    if (new_x == mob_data[i].x && new_z == old_z && new_y == old_y) continue;

    // Prevent collisions with other mobs
    uint8_t colliding = false;
    for (int j = 0; j < MAX_MOBS; j ++) {
      if (j == i) continue;
      if (mob_data[j].type == 0) continue;
      if (
        mob_data[j].x == new_x &&
        mob_data[j].z == new_z &&
        abs((int)mob_data[j].y - (int)new_y) < 2
      ) {
        colliding = true;
        break;
      }
    }
    if (colliding) continue;

    if ( // Hurt mobs that stumble into lava
      (block >= B_lava && block < B_lava + 4) ||
      (block_above >= B_lava && block_above < B_lava + 4)
    ) hurtEntity(entity_id, -1, D_lava, 8);

    // Store new mob position
    mob_data[i].x = new_x;
    mob_data[i].y = new_y;
    mob_data[i].z = new_z;

    // Vary the yaw angle to look just a little less robotic
    yaw += ((r >> 7) & 31) - 16;

    // Broadcast relevant entity movement packets
    for (int j = 0; j < MAX_PLAYERS; j ++) {
      if (player_data[j].client_fd == -1) continue;
      sc_teleportEntity (
        player_data[j].client_fd, entity_id,
        (double)new_x + 0.5, new_y, (double)new_z + 0.5,
        yaw * 360 / 256, 0
      );
      sc_setHeadRotation(player_data[j].client_fd, entity_id, yaw);
    }

  }


  #ifdef DEV_LOG_STALLS
    {
      int64_t tick_t3 = get_program_time();
      if (tick_t3 - tick_t0 > STALL_LOG_THRESHOLD) {
        printf(
          "STALL in tick: players %d ms, disk save %d ms, mobs %d ms\n",
          (int)((tick_t1 - tick_t0) / 1000), (int)((tick_t2 - tick_t1) / 1000), (int)((tick_t3 - tick_t2) / 1000)
        );
      }
    }
  #endif
}

#ifdef ALLOW_CHESTS
// Broadcasts a chest slot update to all clients who have that chest open,
// except for the client who initiated the update.
void broadcastChestUpdate (int origin_fd, uint8_t *storage_ptr, uint16_t item, uint8_t count, uint8_t slot) {

  for (int i = 0; i < MAX_PLAYERS; i ++) {
    if (player_data[i].client_fd == -1) continue;
    if (player_data[i].flags & 0x20) continue;
    // Filter for players that have this chest open
    if (memcmp(player_data[i].craft_items, &storage_ptr, sizeof(storage_ptr)) != 0) continue;
    // Send slot update packet
    sc_setContainerSlot(player_data[i].client_fd, 2, slot, count, item);
  }

  #ifndef DISK_SYNC_BLOCKS_ON_INTERVAL
  writeChestChangesToDisk(storage_ptr, slot);
  #endif

}
#endif

ssize_t writeEntityData (int client_fd, EntityData *data) {
  writeByte(client_fd, data->index);
  writeVarInt(client_fd, data->type);

  switch (data->type) {
    case 0: // Byte
      return writeByte(client_fd, data->value.byte);
    case 21: // Pose
      writeVarInt(client_fd, data->value.pose);
      return 0;

    default: return -1;
  }
}

// Returns the networked size of an EntityData entry
int sizeEntityData (EntityData *data) {
  int value_size;

  switch (data->type) {
    case 0: // Byte
      value_size = 1;
      break;
    case 21: // Pose
      value_size = sizeVarInt(data->value.pose);
      break;

    default: return -1;
  }

  return 1 + sizeVarInt(data->type) + value_size;
}

// Returns the networked size of an array of EntityData entries
int sizeEntityMetadata (EntityData *metadata, size_t length) {
  int total_size = 0;
  for (size_t i = 0; i < length; i ++) {
    int size = sizeEntityData(&metadata[i]);
    if (size == -1) return -1;
    total_size += size;
  }
  return total_size;
}
