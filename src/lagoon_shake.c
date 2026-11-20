/* gabbycat4 fork: optional screen shake off. See lagoon_shake.h. */
#include "lagoon_shake.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t *g_rom;   /* the running machine's ROM copy (common_rtl.h) */
enum { kRomSize = 0x100000, kTable = 0xDE11, kTableBytes = 84 };   /* LoROM $01:DE11, 42 words */
static uint8_t s_original[kTableBytes];
static int s_have, s_on = 1;

static uint64_t fnv(const uint8_t *p, unsigned n) {
  uint64_t h = UINT64_C(14695981039346656037);
  for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * UINT64_C(1099511628211);
  return h;
}
static void apply(uint8_t *rom) {
  if (!rom || !s_have) return;
  if (s_on) memcpy(rom + kTable, s_original, kTableBytes);
  else memset(rom + kTable, 0, kTableBytes);
}
int LagoonShake_OnRom(uint8_t *rom, size_t size) {
  s_have = 0;
  if (!rom || size != kRomSize || fnv(rom + kTable, kTableBytes) != UINT64_C(0x9ee85f4097be1a2c)) return 0;
  memcpy(s_original, rom + kTable, kTableBytes);
  s_have = 1;
  apply(rom);
  return 1;
}
void LagoonShake_Set(int on) {
  s_on = on != 0;
  /* The machine's ROM is a malloc'd copy (cart.c); a 16-bit table read racing
   * this write sees either the old or the new word, both valid offsets. */
  if (g_rom) apply((uint8_t *)(uintptr_t)g_rom);
  fprintf(stderr, "[lagoon-shake] screen shake %s (%s)\n", s_on ? "ON" : "OFF",
          !s_have ? "waiting for the ROM" : g_rom ? "running game updated" : "applied at load");
}
int LagoonShake_On(void) { return s_on; }
int LagoonShake_TableZeroed(const uint8_t *rom, size_t size) {
  if (!rom || size != kRomSize) return 0;
  for (unsigned i = 0; i < kTableBytes; ++i) if (rom[kTable + i]) return 0;
  return 1;
}
