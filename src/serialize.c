#include "globals.h"

#ifdef SYNC_WORLD_TO_DISK

// The world file format changed with the move to Minecraft block state IDs,
// so it has its own name. A world.bin from an older version is ignored.
#ifdef ESP_PLATFORM
  #include "esp_littlefs.h"
  #define FILE_PATH "/littlefs/world2.bin"
  #define OLD_FILE_PATH "/littlefs/world.bin"
  #define JOURNAL_PATH "/littlefs/journal.bin"
#else
  #include <stdio.h>
  #define FILE_PATH "world2.bin"
  #define JOURNAL_PATH "journal.bin"
#endif

#include "tools.h"
#include "registries.h"
#include "serialize.h"

int64_t last_disk_sync_time = 0;

#ifdef DISK_SYNC_BLOCKS_ON_INTERVAL
static void markPagesDirty (int from, int to);
static int replayBlockJournal (void);
#endif

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
      remove(JOURNAL_PATH);
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
    #ifdef DISK_SYNC_BLOCKS_ON_INTERVAL
    // Apply the changes saved in the journal after the world file was last written
    int replayed = replayBlockJournal();
    if (replayed > 0) printf("Restored %d recent block changes from the journal.\n", replayed);
    #endif
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

// Saving block changes without freezing the server
//
// Rewriting a page in the middle of the world file makes flash erase and
// rewrite a lot, which freezes the server. So building is saved the cheap
// way: changes are collected in memory and, every few seconds, added to the
// end of a small "journal" file (a few bytes per change, quick). The journal
// is merged into the big world file only at quiet times: when the last
// player leaves, or on the long backup interval. When the server starts, the
// journal is applied on top of the world file, so nothing is lost.
#define SAVE_PAGE_BYTES 4096
#define SAVE_PAGES ((sizeof(block_changes) + SAVE_PAGE_BYTES - 1) / SAVE_PAGE_BYTES)
static uint8_t dirty_pages[(SAVE_PAGES + 7) / 8];
static int dirty_count = 0;

#define JOURNAL_PENDING_MAX 1024
#define JOURNAL_MAX_RECORDS 20000
typedef struct __attribute__((packed)) {
  uint32_t index;
  BlockChange change;
} JournalRecord;

static uint32_t journal_pending[JOURNAL_PENDING_MAX];
static int journal_pending_count = 0;
static uint8_t journal_overflow = 0;
static uint32_t journal_records_on_disk = 0;

static void markPagesDirty (int from, int to) {
  size_t first = ((size_t)from * sizeof(BlockChange)) / SAVE_PAGE_BYTES;
  size_t last = (((size_t)to + 1) * sizeof(BlockChange) - 1) / SAVE_PAGE_BYTES;
  for (size_t pg = first; pg <= last && pg < SAVE_PAGES; pg ++) {
    if (dirty_pages[pg >> 3] & (1 << (pg & 7))) continue;
    dirty_pages[pg >> 3] |= 1 << (pg & 7);
    dirty_count ++;
  }
}

void markBlockChangesDirty (int from, int to) {
  if (from < 0) from = 0;
  if (to >= MAX_BLOCK_CHANGES) to = MAX_BLOCK_CHANGES - 1;
  if (to < from) return;
  markPagesDirty(from, to);
  for (int i = from; i <= to; i ++) {
    // Same entry changed again right away: one record is enough
    if (journal_pending_count > 0 && journal_pending[journal_pending_count - 1] == (uint32_t)i) continue;
    if (journal_pending_count >= JOURNAL_PENDING_MAX) { journal_overflow = 1; return; }
    journal_pending[journal_pending_count ++] = i;
  }
}

// Forgets everything saved in the journal (used once its content is in the world file)
static void clearBlockJournal (void) {
  remove(JOURNAL_PATH);
  journal_records_on_disk = 0;
  journal_pending_count = 0;
  journal_overflow = 0;
}

void resetBlockJournal (void) {
  clearBlockJournal();
  memset(dirty_pages, 0, sizeof(dirty_pages));
  dirty_count = 0;
}

// Writes up to `max_pages` changed pages of the block change list to the world
// file (the slow, full save). Returns how many pages were written. Once every
// page is saved, the journal is no longer needed and is removed.
int flushDirtyBlockPages (int max_pages) {

  if (dirty_count == 0 || max_pages <= 0) return 0;

  FILE *file = fopen(FILE_PATH, "r+b");
  if (!file) {
    perror("Failed to open \"world2.bin\" for saving block changes");
    return 0;
  }

  static size_t next_page = 0;
  size_t start_page = next_page;
  int written_pages = 0;
  for (size_t n = 0; n < SAVE_PAGES && written_pages < max_pages; n ++) {
    size_t pg = (start_page + n) % SAVE_PAGES;
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
  if (dirty_count == 0) clearBlockJournal();
  return written_pages;

}

// Adds the changes made since the last call to the end of the journal file.
// Quick, because only a few bytes are written. Called every few seconds.
void flushBlockJournal (void) {

  if (journal_pending_count == 0 && !journal_overflow) return;

  // Too many changes at once, or a journal that has grown big: do the full save
  if (journal_overflow || journal_records_on_disk + journal_pending_count > JOURNAL_MAX_RECORDS) {
    flushDirtyBlockPages(1 << 30);
    return;
  }

  FILE *file = fopen(JOURNAL_PATH, "ab");
  if (!file) {
    perror("Failed to open the journal file. Changes will be saved with the next full save");
    journal_pending_count = 0;
    return;
  }
  // Write all records in one go
  static JournalRecord batch[JOURNAL_PENDING_MAX];
  for (int i = 0; i < journal_pending_count; i ++) {
    batch[i].index = journal_pending[i];
    batch[i].change = block_changes[journal_pending[i]];
  }
  size_t count = journal_pending_count;
  if (fwrite(batch, sizeof(JournalRecord), count, file) == count) {
    journal_records_on_disk += count;
  } else {
    perror("Failed to write to the journal file");
  }
  fclose(file);
  journal_pending_count = 0;

}

// Applies the journal on top of the freshly loaded world file.
// Returns the number of changes restored.
static int replayBlockJournal (void) {

  FILE *file = fopen(JOURNAL_PATH, "rb");
  if (!file) return 0;
  JournalRecord rec;
  int restored = 0;
  // A record cut short by a power loss is simply ignored
  while (fread(&rec, sizeof(rec), 1, file) == 1) {
    if (rec.index >= MAX_BLOCK_CHANGES) continue;
    block_changes[rec.index] = rec.change;
    markPagesDirty(rec.index, rec.index);
    restored ++;
  }
  fclose(file);
  journal_records_on_disk = restored;
  return restored;

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
  // Building is added to the small journal file every few seconds, so a
  // restart loses at most that much work. This only writes a few bytes.
  static int64_t last_block_sync_time = 0;
  if (now - last_block_sync_time >= BLOCK_SYNC_INTERVAL) {
    last_block_sync_time = now;
    flushBlockJournal();
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
  #ifdef DISK_SYNC_BLOCKS_ON_INTERVAL
  markBlockChangesDirty(index, index);
  #else
  writeBlockChangesToDisk(index, index);
  #endif
}
#endif

#endif
