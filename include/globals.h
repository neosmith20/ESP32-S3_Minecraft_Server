#ifndef H_GLOBALS
#define H_GLOBALS

#include <stdint.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
  #include "sdkconfig.h"
  #include "esp_attr.h"
  #define WIFI_SSID "your-ssid"
  #define WIFI_PASS "your-password"
  void task_yield ();
#else
  #define task_yield();
#endif

#define true 1
#define false 0

// TCP port, Minecraft's default is 25565
#define PORT 25565

// How many players to keep in memory, NOT the amount of concurrent players
// Even when offline, players who have logged on before take up a slot
#define MAX_PLAYERS 16

// How many mobs to allocate memory for
#define MAX_MOBS (MAX_PLAYERS)

// Manhattan distance at which mobs despawn
#define MOB_DESPAWN_DISTANCE 256

// Server game mode: 0 - survival; 1 - creative; 2 - adventure; 3 - spectator
#define GAMEMODE 0

// Max render distance, determines how many chunks to send
#define VIEW_DISTANCE 2

// Time between server ticks in microseconds (default = 1s)
#define TIME_BETWEEN_TICKS 1000000

// Calculated from TIME_BETWEEN_TICKS
#define TICKS_PER_SECOND ((float)1000000 / TIME_BETWEEN_TICKS)

// Initial world generation seed, will be hashed on startup
// Used in generating terrain and biomes
#define INITIAL_WORLD_SEED 0xA103DE6C

// Initial general RNG seed, will be hashed on startup
// Used in random game events like item drops and mob behavior
#define INITIAL_RNG_SEED 0xE2B9419

// Size of each bilinearly interpolated area ("minichunk")
// For best performance, CHUNK_SIZE should be a power of 2
#define CHUNK_SIZE 8

// Terrain low point - should start a bit below sea level for rivers/lakes
#define TERRAIN_BASE_HEIGHT 60

// Cave generation Y level
#define CAVE_BASE_DEPTH 24

// Size of every major biome in multiples of CHUNK_SIZE
// For best performance, should also be a power of 2
#define BIOME_SIZE (CHUNK_SIZE * 8)

// Calculated from BIOME_SIZE
#define BIOME_RADIUS (BIOME_SIZE / 2)

// How many visited chunk coordinates to "remember"
// The server will not re-send chunks that the player has recently been in
// Must be at least 1, otherwise chunks will be sent on each position update
#define VISITED_HISTORY 4

// How many player-made block changes to allow
// Determines the fixed amount of memory allocated to blocks.
// Each entry is 7 bytes (plus 2 bytes of lookup index). Must not exceed 65535.
// When the project is built with PSRAM and "bss in external memory" turned
// on (see sdkconfig.defaults), these big arrays live in the 8 MB PSRAM, which
// allows a much larger world. Otherwise they take up regular RAM, where
// 20000 entries is already a lot.
#if defined(ESP_PLATFORM) && defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY)
  #define MAX_BLOCK_CHANGES 65000
  #define BIG_ARRAY_ATTR EXT_RAM_BSS_ATTR
#else
  #define MAX_BLOCK_CHANGES 20000
  #define BIG_ARRAY_ATTR
#endif

// Terrain never generates at or above this Y level (trees top out at 79).
// Chunk sections that start at or above it are sent as compact "all air"
// sections unless a player has built something in them. This saves most of
// the terrain generation work and over half of the data sent per chunk.
// Must be a multiple of 16.
#define AIR_ABOVE_Y 80

// How many chunk columns of generated terrain to keep cached.
// Each column takes (AIR_ABOVE_Y / 16) * 4096 bytes (20 KB at the default).
// On ESP32 the cache is placed in PSRAM; if PSRAM isn't available the cache
// turns itself off. Set to 0 to disable caching entirely.
#ifdef ESP_PLATFORM
  #define CHUNK_CACHE_COLUMNS 192
#else
  #define CHUNK_CACHE_COLUMNS 64
#endif

// If defined, writes and reads world data to/from disk (or flash).
// This is a synchronous operation, and can cause performance issues if
// frequent random disk access is slow. Data is still stored in and
// accessed from memory - reading from disk is only done on startup.
// When targeting ESP-IDF, LittleFS is used to manage flash reads and
// writes. Flash is typically *very* slow and unreliable, so on ESP-IDF
// block changes are written in batches (see DISK_SYNC_BLOCKS_ON_INTERVAL).
// This is switched on automatically when the LittleFS component is part of
// the build (src/idf_component.yml adds it). Remove that file or define
// DISABLE_FLASH_SAVE to build without saving.
#ifdef ESP_PLATFORM
  #if !defined(DISABLE_FLASH_SAVE) && __has_include("esp_littlefs.h")
    #define SYNC_WORLD_TO_DISK
  #endif
#else
  #define SYNC_WORLD_TO_DISK
#endif

// The minimum interval (in microseconds) at which certain data is written
// to disk/flash. Bounded on the low end by TIME_BETWEEN_TICKS. By default,
// applies only to player data. Block changes are written as soon as they
// are made, but in much smaller portions. Set DISK_SYNC_BLOCKS_ON_INTERVAL
// to make this apply to block changes as well.
#ifdef ESP_PLATFORM
  // Writing to flash freezes the server for several seconds (measured: about
  // 4 s for the player data on an ESP32-S3). So on ESP-IDF the periodic save
  // is a rare backup (every 30 minutes), and the real save happens when the
  // last player leaves (see DISK_SYNC_WHEN_EMPTY below).
  #define DISK_SYNC_INTERVAL 1800000000
  // If defined, all data is also written to flash whenever the last player
  // who was actually playing disconnects, when nobody can notice the pause.
  #define DISK_SYNC_WHEN_EMPTY
#else
  #define DISK_SYNC_INTERVAL 15000000
#endif

// When block changes are synced on an interval, building and mining are saved
// every 5 seconds, up to 2 pages of 4 KB at a time. A page write briefly
// pauses the server.
#define BLOCK_SYNC_INTERVAL 5000000
#define BLOCK_SYNC_PAGES 2

// Whether to sync block changes to disk on an interval, instead of syncing
// on each change. On systems with fast random disk access, this shouldn't
// be necessary. Enabled by default on ESP-IDF, where every flash write
// stalls the server. Only the range of entries that changed is written.
#ifdef ESP_PLATFORM
  #define DISK_SYNC_BLOCKS_ON_INTERVAL
#endif

// Size of the outgoing data buffer (bytes). Small writes are gathered here
// and sent in one piece. About two TCP segments works well on ESP-IDF.
#define TX_BUFFER_SIZE 2880

// Stack size (bytes) of the server task on ESP-IDF
#define SERVER_TASK_STACK_SIZE 8192

// If defined, the server prints a message whenever something stalls (a
// main loop pass, a network send, or a network receive that takes longer
// than STALL_LOG_THRESHOLD microseconds), whenever WiFi disconnects (with
// the reason code), and a memory status line every 10 seconds on ESP32.
// #define DEV_LOG_STALLS
#define STALL_LOG_THRESHOLD 250000

// Time in microseconds to spend waiting for data transmission before
// timing out. Default is 15s, which leaves 5s to prevent starving other
// clients from Keep Alive packets.
#define NETWORK_TIMEOUT_TIME 15000000

// Size of the receive buffer for incoming string data
#define MAX_RECV_BUF_LEN 256

// If defined, sends the server brand to clients. Doesn't do much, but will
// show up in the top-left of the F3/debug menu, in the Minecraft client.
// You can change the brand string in the "brand" variable in src/globals.c
#define SEND_BRAND

// If defined, rebroadcasts ALL incoming movement updates, disconnecting
// movement from the server's tickrate. This makes movement much smoother
// on very low tickrates, at the cost of potential network instability when
// hosting more than just a couple of players. When disabling this on low
// tickrates, consider disabling SCALE_MOVEMENT_UPDATES_TO_PLAYER_COUNT too.
#define BROADCAST_ALL_MOVEMENT

// If defined, scales the frequency at which player movement updates are
// broadcast based on the amount of players, reducing overhead for higher
// player counts. For very many players, makes movement look jittery.
// It is not recommended to use this if BROADCAST_ALL_MOVEMENT is disabled
// on low tickrates, as that might drastically decrease the update rate.
#define SCALE_MOVEMENT_UPDATES_TO_PLAYER_COUNT

// If defined, calculates fluid flow when blocks are updated near fluids
// Somewhat computationally expensive and potentially unstable
#define DO_FLUID_FLOW

// If defined, allows players to craft and use chests.
// Chests take up 15 block change slots each, require additional checks,
// and use some terrible memory hacks to function. On some platforms, this
// could cause bad performance or even crashes during gameplay.
#define ALLOW_CHESTS

// If defined, enables flight for all players. As a side-effect, allows
// players to sprint when starving.
// #define ENABLE_PLAYER_FLIGHT

// If defined, enables the item pickup animation when mining a block/
// Does not affect how item pickups work! Items from broken blocks still
// get placed directly in the inventory, this is just an animation.
// Relatively inexpensive, though requires sending a few more packets
// every time a block is broken.
// Testing only: players who join with an empty inventory get a starter kit
// (hoe, seeds, carrots, potatoes, bone meal, pickaxe, planks, cobblestone,
// coal, logs, iron). Remove the two slashes in front of the line to turn it on.
// #define GIVE_TEST_KIT

#define ENABLE_PICKUP_ANIMATION

// If defined, players are able to receive damage from nearby cacti.
#define ENABLE_CACTUS_DAMAGE

// If defined, logs unrecognized packet IDs
// #define DEV_LOG_UNKNOWN_PACKETS

// If defined, logs cases when packet length doesn't match parsed byte count
#define DEV_LOG_LENGTH_DISCREPANCY

// If defined, log chunk generation events
// #define DEV_LOG_CHUNK_GENERATION

// If defined, allows dumping world data by sending 0xBEEF (big-endian),
// and uploading world data by sending 0xFEED, followed by the data buffer.
// Doesn't implement authentication, hence disabled by default.
// #define DEV_ENABLE_BEEF_DUMPS

#define STATE_NONE 0
#define STATE_STATUS 1
#define STATE_LOGIN 2
#define STATE_TRANSFER 3
#define STATE_CONFIGURATION 4
#define STATE_PLAY 5

extern ssize_t recv_count;
extern uint8_t recv_buffer[MAX_RECV_BUF_LEN];

extern uint32_t world_seed;
extern uint32_t rng_seed;

extern uint16_t world_time;
extern uint32_t server_ticks;

extern char motd[];
extern uint8_t motd_len;

#ifdef SEND_BRAND
  extern char brand[];
  extern uint8_t brand_len;
#endif

extern uint16_t client_count;

#pragma pack(push, 1)

// A block placed or removed by a player. `block` is a Minecraft block state
// ID (see registries.h). BLOCK_NONE marks an unused entry.
typedef struct {
  short x;
  short z;
  uint8_t y;
  uint16_t block;
} BlockChange;

#define BLOCK_NONE 0xFFFF

typedef struct {
  uint8_t uuid[16];
  char name[16];
  int client_fd;
  short x;
  uint8_t y;
  short z;
  short visited_x[VISITED_HISTORY];
  short visited_z[VISITED_HISTORY];
  #ifdef SCALE_MOVEMENT_UPDATES_TO_PLAYER_COUNT
    uint16_t packets_since_update;
  #endif
  int8_t yaw;
  int8_t pitch;
  uint8_t grounded_y;
  uint8_t health;
  uint8_t hunger;
  uint16_t saturation;
  uint8_t hotbar;
  uint16_t inventory_items[41];
  uint16_t craft_items[9];
  uint8_t inventory_count[41];
  uint8_t craft_count[9];
  // Usage depends on player's flags, see below
  // When no flags are set, acts as cursor item ID
  uint16_t flagval_16;
  // Usage depends on player's flags, see below
  // When no flags are set, acts as cursor item count
  uint8_t flagval_8;
  // 0x01 - attack cooldown, uses flagval_8 as the timer
  // 0x02 - has not spawned yet
  // 0x04 - sneaking
  // 0x08 - sprinting
  // 0x10 - eating, makes flagval_16 act as eating timer
  // 0x20 - client loading, uses flagval_16 as fallback timer
  // 0x40 - movement update cooldown
  // 0x80 - craft_items lock (for storing pointers)
  uint8_t flags;
} PlayerData;

typedef struct {
  uint8_t type;
  short x;
  // When the mob is dead (health is 0), the Y coordinate acts
  // as a timer for deleting and deallocating the mob
  uint8_t y;
  short z;
  // Lower 5 bits: health
  // Middle 1 bit: sheep sheared, unused for other mobs
  // Upper 2 bits: panic timer
  uint8_t data;
} MobData;

#pragma pack(pop)

union EntityDataValue {
  uint8_t byte;
  int pose;
};

typedef struct {
  uint8_t index;
  // 0 - Byte
  // 21 - Pose
  int type;
  union EntityDataValue value;
} EntityData;

extern BlockChange block_changes[MAX_BLOCK_CHANGES];
extern int block_changes_count;

extern PlayerData player_data[MAX_PLAYERS];
// Exact position of each player (player_data only keeps block coordinates)
extern double player_exact[MAX_PLAYERS][3];
extern int player_data_count;

extern MobData mob_data[MAX_MOBS];

#endif
