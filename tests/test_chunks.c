// PC test for chunk sending. Sends real "Chunk Data and Update Light"
// packets through a socket pair, decodes them like a client would and
// compares every block with what the server says is there.
//
// Also prints, for the plain terrain chunks, a hash of the decoded blocks in
// the same order the original (pre-rewrite) test used, so the output can be
// compared with the original code's output.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>

#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "packets.h"
#include "worldgen.h"
#include "registries.h"
#include "blocks.h"
#include "procedures.h"

static uint8_t *rx;
static size_t rx_len, rx_cap;
static int rx_fd;

static void *reader (void *arg) {
  uint8_t tmp[65536];
  for (;;) {
    ssize_t n = read(rx_fd, tmp, sizeof(tmp));
    if (n <= 0) break;
    if (rx_len + n > rx_cap) {
      rx_cap = (rx_len + n) * 2;
      rx = realloc(rx, rx_cap);
    }
    memcpy(rx + rx_len, tmp, n);
    rx_len += n;
  }
  return NULL;
}

static size_t pos;
static const uint8_t *buf;
static size_t buf_len;
static int bad;

static uint8_t rd8 (void) {
  if (pos >= buf_len) { bad = 1; return 0; }
  return buf[pos++];
}
static uint32_t rd32 (void) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i ++) v = (v << 8) | rd8();
  return v;
}
static uint16_t rd16 (void) { return (rd8() << 8) | rd8(); }
static int rdVar (void) {
  int v = 0, shift = 0;
  for (;;) {
    uint8_t b = rd8();
    v |= (b & 0x7F) << shift;
    if (!(b & 0x80)) return v;
    shift += 7;
    if (shift > 28) { bad = 1; return 0; }
  }
}

// Decoded blocks of the 20 terrain sections (Y = 0..319), x + z*16 + y*256
static uint16_t states[20][4096];
static int bits_seen[16];

// Returns bytes beyond the declared packet length (extra block updates),
// or a negative number when the packet is malformed.
static long decode (const uint8_t *data, size_t n) {
  buf = data; buf_len = n; pos = 0; bad = 0;

  int total = rdVar();
  size_t body = pos;
  if (rdVar() != 0x27) return -1;
  rd32(); rd32();
  if (rdVar() != 0) return -1; // heightmaps
  int cds = rdVar();
  size_t chunk_start = pos;

  for (int s = 0; s < 32; s ++) {
    int non_air = rd16();
    int bits = rd8();
    uint16_t sec[4096];
    if (bits < 16) bits_seen[bits] ++;
    if (bits == 0) {
      int v = rdVar();
      for (int i = 0; i < 4096; i ++) sec[i] = v;
    } else {
      uint16_t pal[256];
      int plen = 0;
      if (bits >= 4 && bits <= 8) {
        plen = rdVar();
        if (plen < 2 || plen > 256 || plen > (1 << bits)) return -4;
        for (int i = 0; i < plen; i ++) pal[i] = rdVar();
      } else if (bits != 15) return -5;
      int per_long = 64 / bits;
      int n_entry = 0;
      while (n_entry < 4096) {
        uint64_t v = 0;
        for (int b = 0; b < 8; b ++) v = (v << 8) | rd8();
        for (int j = 0; j < per_long && n_entry < 4096; j ++, n_entry ++) {
          unsigned e = (v >> (j * bits)) & ((1u << bits) - 1);
          if (bits == 15) sec[n_entry] = e;
          else {
            if ((int)e >= plen) return -6;
            sec[n_entry] = pal[e];
          }
        }
      }
    }
    int counted = 0;
    for (int i = 0; i < 4096; i ++) if (sec[i] != 0) counted ++;
    if (counted != non_air && !(s < 4 && non_air == 4096)) return -7;
    if (rd8() != 0) return -1; // biome bits
    rdVar(); // biome palette
    if (s >= 4 && s < 24) memcpy(states[s - 4], sec, sizeof(sec));
    if (bad) return -1;
  }
  if (pos - chunk_start != (size_t)cds) return -2; // chunk data size wrong
  if (rdVar() != 0) return -1; // block entities
  if (rdVar() != 1) return -1;
  rd32(); rd32();
  if (rdVar() != 0 || rdVar() != 0 || rdVar() != 0) return -1;
  if (rdVar() != 26) return -1;
  for (int i = 0; i < 26; i ++) {
    if (rdVar() != 2048) return -1;
    pos += 2048;
  }
  if (rdVar() != 0) return -1;
  if (bad || pos > n) return -1;
  if (pos != body + (size_t)total) return -3; // packet length wrong
  return (long)(n - pos);
}

// Hash in the order of the original test: groups of 8 reversed
static uint64_t hashStatesOriginalOrder (void) {
  uint64_t h = 1469598103934665603ULL;
  for (int s = 0; s < 20; s ++) {
    for (int i = 0; i < 4096; i ++) {
      int j = (i & ~7) | (7 - (i & 7));
      h ^= states[s][j];
      h *= 1099511628211ULL;
    }
  }
  return h;
}

static uint32_t test_rng = 12345;
static uint32_t rnd (void) {
  test_rng ^= test_rng << 13;
  test_rng ^= test_rng >> 17;
  test_rng ^= test_rng << 5;
  return test_rng;
}

static int errors = 0;
static int verbose_hash = 0;

// Sends and decodes one chunk, checks every block
static void doChunk (int cx, int cz, const char *tag) {
  int sv[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  rx = NULL; rx_len = rx_cap = 0; rx_fd = sv[1];
  pthread_t th;
  pthread_create(&th, NULL, reader, NULL);

  sc_chunkDataAndUpdateLight(sv[0], cx, cz);
  flush_tx();
  close(sv[0]);
  pthread_join(th, NULL);
  close(sv[1]);

  long extra = decode(rx, rx_len);
  int wrong = 0;
  if (extra >= 0) {
    for (int sec = 0; sec < 20; sec ++) {
      for (int i = 0; i < 4096; i ++) {
        int x = cx * 16 + (i & 15), z = cz * 16 + ((i >> 4) & 15), y = sec * 16 + (i >> 8);
        // The server only stores block changes up to Y=255
        uint16_t want = y > 255 ? B_air : getBlockAt(x, y, z);
        // Light emitters and chests are sent as block updates after the chunk
        if (getBlockFlags(want) & BF_SEPARATE_UPDATE) {
          ChunkAnchor a = { .x = div_floor(x, CHUNK_SIZE), .z = div_floor(z, CHUNK_SIZE) };
          a.hash = getChunkHash(a.x, a.z);
          a.biome = getChunkBiome(a.x, a.z);
          want = getTerrainAt(x, y, z, a);
        }
        if (states[sec][i] != want) {
          if (wrong < 3 && getenv("DBG")) printf("  mismatch at %d,%d,%d: got %d want %d (flags %x)\n", x, y, z, states[sec][i], want, getBlockFlags(want));
          wrong ++;
        }
      }
    }
  }
  if (extra < 0 || wrong) errors ++;
  if (verbose_hash) {
    printf("%s %d %d len=%zu hash=%016llx\n", tag, cx, cz, rx_len, (unsigned long long)hashStatesOriginalOrder());
  }
  if (extra < 0 || wrong) printf("FAIL %s %d %d: decode=%ld wrong blocks=%d\n", tag, cx, cz, extra, wrong);
  free(rx);
}

int main (int argc, char **argv) {

  world_seed = splitmix64(world_seed);
  rng_seed = splitmix64(rng_seed);
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = BLOCK_NONE;
  block_changes_count = 0;
  for (int i = 0; i < MAX_PLAYERS; i ++) {
    client_states[i * 2] = -1;
    player_data[i].client_fd = -1;
  }
  initTerrainCache();
  rebuildBlockChangeIndex();

  // A. plain terrain
  verbose_hash = 1;
  for (int cz = -6; cz <= 6; cz ++) for (int cx = -6; cx <= 6; cx ++) doChunk(cx, cz, "A");
  verbose_hash = 0;
  printf("terrain chunks done, errors=%d\n", errors);

  // B. lots of random player changes using many different blocks
  static const uint16_t pool[] = {
    B_stone, B_dirt, B_cobblestone, B_air, B_oak_log, B_sand, B_torch, B_oak_planks, B_glass,
    B_oak_door, B_oak_stairs, B_oak_slab, B_oak_fence, B_ladder, B_lantern, B_cobblestone_wall
  };
  for (int i = 0; i < 4000; i ++) {
    short x = (short)(rnd() % 160) - 80, z = (short)(rnd() % 160) - 80;
    uint8_t y;
    uint32_t r = rnd() % 4;
    if (r < 2) y = 40 + rnd() % 50;
    else if (r == 2) y = 90 + rnd() % 166;
    else y = rnd() % 256;
    makeBlockChange(x, y, z, pool[rnd() % (sizeof(pool) / sizeof(pool[0]))]);
  }
  for (int cz = -5; cz <= 5; cz ++) for (int cx = -5; cx <= 5; cx ++) doChunk(cx, cz, "B");
  for (int cz = -5; cz <= 5; cz ++) for (int cx = -5; cx <= 5; cx ++) doChunk(cx, cz, "B2");
  printf("changed chunks done, errors=%d\n", errors);

  // C. one section with more than 256 different blocks (direct palette)
  {
    int n = 0;
    for (int i = 0; i < block_info_count && n < 4096; i ++) {
      for (int st = block_info[i].min; st <= block_info[i].max && n < 4096; st ++, n ++) {
        makeBlockChange(200 + (n & 15), 100 + (n >> 8), 200 + ((n >> 4) & 15), st);
      }
    }
    printf("placed %d distinct states in one section\n", n);
    doChunk(12, 12, "C");
    doChunk(12, 12, "C2");
  }
  printf("big-palette chunk done, errors=%d\n", errors);

  // D. a uniform section and a nearly-uniform one
  for (int z = 0; z < 16; z ++) for (int x = 0; x < 16; x ++) for (int y = 100; y < 116; y ++) {
    makeBlockChange(400 + x, y, 400 + z, B_stone);
  }
  doChunk(25, 25, "D");
  makeBlockChange(405, 105, 405, B_glass);
  doChunk(25, 25, "D2");

  printf("bits per entry seen:");
  for (int b = 0; b < 16; b ++) if (bits_seen[b]) printf(" %d(x%d)", b, bits_seen[b]);
  printf("\nerrors=%d\n", errors);
  return errors != 0;
}
