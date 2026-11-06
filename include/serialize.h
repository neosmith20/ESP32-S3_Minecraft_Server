#ifndef SERIALIZE_H
#define SERIALIZE_H

#include "globals.h"

#ifdef SYNC_WORLD_TO_DISK
  int initSerializer ();
  void writeBlockChangesToDisk (int from, int to);
  void markBlockChangesDirty (int from, int to);
  int flushDirtyBlockPages (int max_pages);
  void flushBlockJournal (void);
  void resetBlockJournal (void);
  void writeChestChangesToDisk (uint8_t *storage_ptr, uint8_t slot);
  void writePlayerDataToDisk ();
  void writeDataToDiskOnInterval ();
  void writeAllDataToDisk ();
  void writePlayerSnapshot (void);
  int saveExtraData (const char *name, const void *data, size_t size);
  int loadExtraData (const char *name, void *data, size_t size);
#else
  // Define no-op placeholders for when disk syncing isn't enabled
  #define writeBlockChangesToDisk(a, b)
  #define markBlockChangesDirty(a, b)
  #define flushDirtyBlockPages(a) 0
  #define flushBlockJournal()
  #define resetBlockJournal()
  #define writeChestChangesToDisk(a, b)
  #define writePlayerDataToDisk()
  #define writeDataToDiskOnInterval()
  #define writeAllDataToDisk()
  #define writePlayerSnapshot()
  #define saveExtraData(a, b, c) 0
  #define loadExtraData(a, b, c) 0
  #define initSerializer() 0
#endif

#endif
