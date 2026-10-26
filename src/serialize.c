#include "globals.h"

#ifdef SYNC_WORLD_TO_DISK

// The world file format changed with the move to Minecraft block state IDs,
// so it has its own name. A world.bin from an older version is ignored.
#ifdef ESP_PLATFORM
  #include "esp_littlefs.h"
  #define FILE_PATH "/littlefs/world2.bin"
  #define OLD_FILE_PATH "/littlefs/world.bin"
#else
  #include <stdio.h>
  #define FILE_PATH "world2.bin"
#endif

#include "tools.h"
#include "registries.h"
#include "serialize.h"

int64_t last_disk_sync_time = 0;

// Restores world data from disk, or writes world file if it doesn't exist
int initSerializer () {

  last_disk_sync_time = get_program_time();

  #ifdef ESP_PLATFORM
    esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs",
      .partition_label = "littlefs",
      .format_if_mount_failed = true,
      .dont_mount = false
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
      printf("LittleFS error %d\n", ret);
      perror("Failed to mount LittleFS. Aborting.");
      return 1;
    }
  #endif

  #ifdef ESP_PLATFORM
    // Free up the flash space used by a world file of the old format
    if (remove(OLD_FILE_PATH) == 0) {
      printf("Removed the old-format world file (this version starts a new world).\n");
    }
  #endif

  // Attempt to open existing world file. A file of the wrong size is left
  // over from an interrupted write or an older version, so start fresh.
  FILE *file = fopen(FILE_PATH, "rb");
  if (file) {
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size != (long)(sizeof(block_changes) + sizeof(player_data))) {
      printf("The world file has the wrong size (%ld bytes, expected %u). Starting a new world.\n",
        file_size, (unsigned)(sizeof(block_changes) + sizeof(player_data)));
      fclose(file);
      remove(FILE_PATH);
      file = NULL;
    }
  }
  if (file) {

    // Read block changes from the start of the file directly into memory
    size_t read = fread(block_changes, 1, sizeof(block_changes), file);
    if (read != sizeof(block_changes)) {
      printf("Read %u bytes from \"world2.bin\", expected %u (block changes). Aborting.\n", read, sizeof(block_changes));
      fclose(file);
      return 1;
    }
    // Find the index of the last occupied entry to recover block_changes_count
    for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) {
      if (block_changes[i].block == BLOCK_NONE) continue;
      if (IS_BLOCK(block_changes[i].block, chest)) i += 14;
      if (i >= block_changes_count) block_changes_count = i + 1;
    }
    // Seek past block changes to start reading player data
    if (fseek(file, sizeof(block_changes), SEEK_SET) != 0) {
      perror("Failed to seek to player data in \"world2.bin\". Aborting.");
      fclose(file);
      return 1;
    }
    // Read player data directly into memory
    read = fread(player_data, 1, sizeof(player_data), file);
    fclose(file);
    if (read != sizeof(player_data)) {
      printf("Read %u bytes from \"world2.bin\", expected %u (player data). Aborting.\n", read, sizeof(player_data));
      return 1;
    }

  } else { // World file doesn't exist or failed to open
    printf("No \"world2.bin\" file found, creating one...\n\n");

    // Try to create the file in binary write mode
    file = fopen(FILE_PATH, "wb");
    if (!file) {
      perror(
        "Failed to open \"world2.bin\" for writing.\n"
        "Consider checking permissions or disabling SYNC_WORLD_TO_DISK in \"globals.h\"."
      );
      return 1;
    }
    // Write initial block changes array
    // This should be done after all entries have had `block` set to 0xFF
    size_t written = fwrite(block_changes, 1, sizeof(block_changes), file);
    if (written != sizeof(block_changes)) {
      perror(
        "Failed to write initial block data to \"world2.bin\".\n"
        "Consider checking permissions or disabling SYNC_WORLD_TO_DISK in \"globals.h\"."
      );
      fclose(file);
      return 1;
    }
    // Seek past written block changes to start writing player data
    if (fseek(file, sizeof(block_changes), SEEK_SET) != 0) {
      perror(
        "Failed to seek past block changes in \"world2.bin\"."
        "Consider checking permissions or disabling SYNC_WORLD_TO_DISK in \"globals.h\"."
      );
      fclose(file);
      return 1;
    }
    // Write initial player data to disk (should be just nulls?)
    written = fwrite(player_data, 1, sizeof(player_data), file);
    fclose(file);
    if (written != sizeof(player_data)) {
      perror(
        "Failed to write initial player data to \"world2.bin\".\n"
        "Consider checking permissions or disabling SYNC_WORLD_TO_DISK in \"globals.h\"."
      );
      return 1;
    }

  }

  return 0;
}

// Writes a range of block change entries (inclusive) to disk
void writeBlockChangesToDisk (int from, int to) {

  // Stay inside the array, no matter what the caller asks for
  if (from < 0) from = 0;
  if (to >= MAX_BLOCK_CHANGES) to = MAX_BLOCK_CHANGES - 1;
  if (to < from) return;

  // Try to open the file in rw (without overwriting)
  FILE *file = fopen(FILE_PATH, "r+b");
  if (!file) {
    perror("Failed to open \"world2.bin\". Block updates have been dropped.");
    return;
  }

  // Entries are stored back to back, so the whole range is a single write
  if (fseek(file, (long)from * sizeof(BlockChange), SEEK_SET) != 0) {
    fclose(file);
    perror("Failed to seek in \"world2.bin\". Block updates have been dropped.");
    return;
  }
  size_t count = to - from + 1;
  if (fwrite(&block_changes[from], sizeof(BlockChange), count, file) != count) {
    fclose(file);
    perror("Failed to write to \"world2.bin\". Block updates have been dropped.");
    return;
  }

  fclose(file);
}

// Remembers which parts of the block change list need to be written to flash
// (see DISK_SYNC_BLOCKS_ON_INTERVAL). The list is split into 4 KB pages, the
// size of a flash sector, and only pages with changes get written.
#define SAVE_PAGE_BYTES 4096
#define SAVE_PAGES ((sizeof(block_changes) + SAVE_PAGE_BYTES - 1) / SAVE_PAGE_BYTES)
static uint8_t dirty_pages[(SAVE_PAGES + 7) / 8];
static int dirty_count = 0;

void markBlockChangesDirty (int from, int to) {
  if (from < 0) from = 0;
  if (to >= MAX_BLOCK_CHANGES) to = MAX_BLOCK_CHANGES - 1;
  if (to < from) return;
  size_t first = ((size_t)from * sizeof(BlockChange)) / SAVE_PAGE_BYTES;
  size_t last = (((size_t)to + 1) * sizeof(BlockChange) - 1) / SAVE_PAGE_BYTES;
  for (size_t pg = first; pg <= last && pg < SAVE_PAGES; pg ++) {
    if (dirty_pages[pg >> 3] & (1 << (pg & 7))) continue;
    dirty_pages[pg >> 3] |= 1 << (pg & 7);
    dirty_count ++;
  }
}

// Writes up to `max_pages` changed pages of the block change list to flash.
// Returns how many pages were written.
int flushDirtyBlockPages (int max_pages) {

  if (dirty_count == 0 || max_pages <= 0) return 0;

  FILE *file = fopen(FILE_PATH, "r+b");
  if (!file) {
    perror("Failed to open \"world2.bin\" for saving block changes");
    return 0;
  }

  static size_t next_page = 0;
  int written_pages = 0;
  for (size_t n = 0; n < SAVE_PAGES && written_pages < max_pages; n ++) {
    size_t pg = (next_page + n) % SAVE_PAGES;
    if (!(dirty_pages[pg >> 3] & (1 << (pg & 7)))) continue;
    size_t offset = pg * SAVE_PAGE_BYTES;
    size_t length = sizeof(block_changes) - offset;
    if (length > SAVE_PAGE_BYTES) length = SAVE_PAGE_BYTES;
    if (fseek(file, (long)offset, SEEK_SET) != 0) break;
    if (fwrite((uint8_t *)block_changes + offset, 1, length, file) != length) {
      perror("Failed to write block changes to \"world2.bin\"");
      break;
    }
    dirty_pages[pg >> 3] &= ~(1 << (pg & 7));
    dirty_count --;
    written_pages ++;
    next_page = pg + 1;
  }

  fclose(file);
  return written_pages;

}

// Writes all player data to disk
void writePlayerDataToDisk () {

  // Try to open the file in rw (without overwriting)
  FILE *file = fopen(FILE_PATH, "r+b");
  if (!file) {
    perror("Failed to open \"world2.bin\". Player updates have been dropped.");
    return;
  }
  // Seek past block changes in file
  if (fseek(file, sizeof(block_changes), SEEK_SET) != 0) {
    fclose(file);
    perror("Failed to seek in \"world2.bin\". Player updates have been dropped.");
    return;
  }
  // Write full player data array to file
  // Since this is a bigger write, it should ideally be done infrequently
  if (fwrite(&player_data, 1, sizeof(player_data), file) != sizeof(player_data)) {
    fclose(file);
    perror("Failed to write to \"world2.bin\". Player updates have been dropped.");
    return;
  }

  fclose(file);
}

// Writes all player data and all pending block changes right away.
// Slow on flash (seconds), so only call this when nobody is playing.
void writeAllDataToDisk () {

  last_disk_sync_time = get_program_time();

  #ifdef DEV_LOG_STALLS
    int64_t save_start = get_program_time();
  #endif

  writePlayerDataToDisk();
  #ifdef DISK_SYNC_BLOCKS_ON_INTERVAL
  flushDirtyBlockPages(1 << 30);
  #endif

  #ifdef DEV_LOG_STALLS
    printf("World saved in %d ms\n", (int)((get_program_time() - save_start) / 1000));
  #endif

}

// Writes data queued for interval writes, but only if enough time has passed
void writeDataToDiskOnInterval () {

  int64_t now = get_program_time();

  #ifdef DISK_SYNC_BLOCKS_ON_INTERVAL
  // Block changes are saved a couple of pages at a time every few seconds,
  // so a restart loses at most a few seconds of building
  static int64_t last_block_sync_time = 0;
  if (dirty_count != 0 && now - last_block_sync_time >= BLOCK_SYNC_INTERVAL) {
    last_block_sync_time = now;
    flushDirtyBlockPages(BLOCK_SYNC_PAGES);
  }
  #endif

  // Skip the full write if enough time hasn't passed since the last one
  if (now - last_disk_sync_time < DISK_SYNC_INTERVAL) return;
  last_disk_sync_time = now;

  // Write full player data and all remaining block changes
  writePlayerDataToDisk();
  #ifdef DISK_SYNC_BLOCKS_ON_INTERVAL
  flushDirtyBlockPages(1 << 30);
  #endif

}

#ifdef ALLOW_CHESTS
// Writes a chest slot change to disk
void writeChestChangesToDisk (uint8_t *storage_ptr, uint8_t slot) {
  /**
   * More chest-related memory hacks!!
   *
   * Since chests are implemented in the block_changes array, any
   * changes to the contents of a chest have to be synced to the block
   * changes part of the world file. The index of the "blocks" is
   * determined as such:
   *
   * The storage pointer points to the block entry directly following
   * the chest itself. To get the index of this entry, we can subtract
   * the pointer to the block changes array (cast to uint8_t*) from the
   * storage pointer. This gets us the amount of bytes between the start
   * of the block changes array and the chest's item data, as a pointer.
   * To get the actual block index, we cast this weird pointer to an
   * integer, and divide it by the byte size of the BlockChange struct.
   * Finally, the chest slot divided by 2 is added to this index to get
   * the block entry pertaining to the relevant chest slot, as each
   * entry encodes exactly 2 slots.
   */
  int index = (int)(storage_ptr - (uint8_t *)block_changes) / sizeof(BlockChange) + slot / 2;
  writeBlockChangesToDisk(index, index);
}
#endif

#endif
