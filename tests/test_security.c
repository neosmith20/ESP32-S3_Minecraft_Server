// Security regression tests: feeds hostile packets to the real packet handlers
// through a socket pair. Build with -fsanitize=address,undefined to also catch
// memory errors (CI does this).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include "globals.h"
#include "tools.h"
#include "varnum.h"
#include "registries.h"
#include "worldgen.h"
#include "procedures.h"
#include "packets.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails ++; } else printf("ok:   %s\n", msg); } while (0)

static int mk (int *peer) {
  int sv[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  int fl = fcntl(sv[0], F_GETFL, 0); fcntl(sv[0], F_SETFL, fl | O_NONBLOCK);
  *peer = sv[1];
  return sv[0];
}
static uint8_t out[4096]; static int outn;
static void b (uint8_t v) { out[outn ++] = v; }
static void vi (uint32_t v) { while (v & ~0x7F) { b((v & 0x7F) | 0x80); v >>= 7; } b(v); }
static void flushOut (int peer) { if (outn) write(peer, out, outn); outn = 0; }
static void drain (int peer) {
  int fl = fcntl(peer, F_GETFL, 0); fcntl(peer, F_SETFL, fl | O_NONBLOCK);
  uint8_t tmp[4096]; while (read(peer, tmp, sizeof(tmp)) > 0);
}

int main (void) {
  for (int i = 0; i < MAX_PLAYERS; i ++) { client_states[i * 2] = -1; player_data[i].client_fd = -1; }
  for (int i = 0; i < MAX_BLOCK_CHANGES; i ++) block_changes[i].block = BLOCK_NONE;
  initTerrainCache();

  int peerA, peerB;
  int fdA = mk(&peerA), fdB = mk(&peerB);
  memset(&player_data[0], 0, sizeof(PlayerData));
  memset(&player_data[1], 0, sizeof(PlayerData));
  player_data[0].client_fd = fdA; strcpy(player_data[0].name, "AAAAAAAAAAAAAAA");
  player_data[1].client_fd = fdB; strcpy(player_data[1].name, "Bob");
  player_data[0].x = 100; player_data[0].z = 100; player_data[0].y = 70; player_data[0].health = 20;
  player_data[1].x = 101; player_data[1].z = 100; player_data[1].y = 70; player_data[1].health = 20;

  // 1. Reach checks
  CHECK(isBlockInReach(&player_data[0], 103, 70, 100), "nearby block is in reach");
  CHECK(!isBlockInReach(&player_data[0], 300, 70, 100), "far block is out of reach");
  CHECK(!isBlockInReach(&player_data[0], 100, 300, 100), "block above the world is out of reach");
  CHECK(!isBlockInReach(&player_data[0], 100, -5, 100), "block below the world is out of reach");
  CHECK(isEntityInReach(&player_data[0], fdB), "nearby player is in reach");
  CHECK(!isEntityInReach(&player_data[0], 9999), "unknown entity is not in reach");
  CHECK(!isEntityInReach(&player_data[0], -2 - 1000), "mob index outside the table is not in reach");

  // 2. A string with an impossible length is refused, never looped over
  vi(0xFFFFFFFFu); flushOut(peerA);
  readStringN(fdA, 100);
  CHECK(recv_count < 0, "negative string length is a protocol error");
  drain(peerA);

  fdA = mk(&peerA); player_data[0].client_fd = fdA;
  vi(200); for (int i = 0; i < 200; i ++) b('x'); flushOut(peerA);
  readStringN(fdA, 16);
  CHECK(recv_count == 16 && strlen((char *)recv_buffer) == 16, "long strings are cut to the maximum and the rest skipped");

  // 3. Inventory clicks: slot 255 and absurd slots must not write out of bounds
  fdA = mk(&peerA); player_data[0].client_fd = fdA;
  uint16_t before_items[41]; memcpy(before_items, player_data[0].inventory_items, sizeof(before_items));
  outn = 0;
  vi(0);        // window 0
  vi(0);        // state id
  b(0); b(0);   // clicked slot (int16)
  b(0);         // button
  vi(0);        // mode
  vi(2);        // two changed slots
  b(0x00); b(0xFF); b(1); vi(5); vi(64); vi(0); vi(0);   // slot 255 with an item
  b(0x7F); b(0xFF); b(1); vi(5); vi(64); vi(0); vi(0);   // slot 32767 with an item
  b(0);         // no cursor item
  flushOut(peerA);
  cs_clickContainer(fdA);
  CHECK(memcmp(before_items, player_data[0].inventory_items, sizeof(before_items)) == 0, "clicks on invalid slots change nothing");

  // an item count above a stack is not accepted
  fdA = mk(&peerA); player_data[0].client_fd = fdA;
  outn = 0;
  vi(0); vi(0); b(0); b(0); b(0); vi(0); vi(1);
  b(0); b(36); b(1); vi(5); vi(120); vi(0); vi(0);       // hotbar slot 0, count 120
  b(0);
  flushOut(peerA);
  cs_clickContainer(fdA);
  CHECK(player_data[0].inventory_count[0] <= 64, "item counts above 64 are refused");

  // too many changes disconnects instead of reading forever
  fdA = mk(&peerA); player_data[0].client_fd = fdA;
  outn = 0; vi(0); vi(0); b(0); b(0); b(0); vi(0); vi(2000000000); flushOut(peerA);
  CHECK(cs_clickContainer(fdA) == 1, "absurd change count is refused");

  // 4. Chest window without an open chest must not touch memory
  fdA = mk(&peerA); player_data[0].client_fd = fdA;
  player_data[0].flags &= ~0x80;
  outn = 0; vi(2); vi(0); b(0); b(0); b(0); vi(0); vi(1);
  b(0); b(5); b(1); vi(5); vi(10); vi(0); vi(0);
  b(0); flushOut(peerA);
  cs_clickContainer(fdA);
  CHECK(1, "chest slot with no open chest is ignored (ASan checks memory)");

  // 5. Whisper that is too long cannot overflow the buffer
  fdA = mk(&peerA); player_data[0].client_fd = fdA;
  fdB = mk(&peerB); player_data[1].client_fd = fdB;
  outn = 0;
  vi(4 + 1 + 3 + 1 + 219); b('!'); b('m'); b('s'); b('g'); b(' '); b('B'); b('o'); b('b'); b(' ');
  for (int i = 0; i < 219; i ++) b('Z');
  for (int i = 0; i < 8 + 8; i ++) b(0); b(0); vi(0); b(0); b(0); b(0); b(0);
  flushOut(peerA);
  cs_chat(fdA);
  CHECK(1, "oversized whisper handled (ASan checks memory)");
  drain(peerB);

  // 6. Handshake cannot request the game state directly
  fdA = mk(&peerA); player_data[0].client_fd = fdA;
  outn = 0; vi(772); vi(9); for (int i = 0; i < 9; i ++) b("localhost"[i]); b(0x63); b(0xDD); vi(5); flushOut(peerA);
  CHECK(cs_handshake(fdA) == 1, "handshake with intent 5 is refused");

  // 7. Zero UUID is refused
  uint8_t zero[16] = {0}; char nm[16] = "Zero";
  CHECK(reservePlayerData(77, zero, nm) == 1, "all-zero UUID is refused");

  printf("\n%s (%d failures)\n", fails ? "SOME TESTS FAILED" : "ALL SECURITY TESTS PASSED", fails);
  return fails != 0;
}
