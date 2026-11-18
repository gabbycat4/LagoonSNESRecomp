/*
 * lagoon_motion_log.c -- see lagoon_motion_log.h. gabbycat4 fork, Step 2.
 *
 * motion.log line format (one per simulated field):
 *   F <frame> t=<tick $0408> r=<room $048E> i=<INIDISP> m=<BGMODE> h=<HDMAEN>
 *     c=<$0480>,<$0482>,<$0484>,<$0486>          game camera words
 *     s=<h1>,<v1>,<h2>,<v2>,<h3>,<v3>             layer scrolls, last drawn line
 *     [b=<first>-<last>:<h1>,<v1>,<h2>,<v2>,<h3>,<v3>|...]  only when the
 *                                                 scrolls change down the screen
 *     p=<x>,<y>                                   player ($0502,$0504)
 *     a=<slot>:<x>,<y>,<type>;...                 other active actors
 *     [pal=<index>:<bgr555 hex>,...]              palette entries that changed
 * Values are hex except frame and slot. A header line starts with '#'.
 */
#include "lagoon_motion_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes/ppu.h"

enum { kLines = 225, kLayers = 3 };

static int s_init;
static FILE *s_motion, *s_oam;
static uint16_t s_line_scroll[kLines][kLayers * 2];
static uint8_t s_line_seen[kLines];
static uint16_t s_last_cgram[256];
static int s_have_cgram;

static FILE *open_log(const char *env) {
  const char *path = getenv(env);
  if (!path || !*path) return NULL;
  FILE *f = fopen(path, "w");
  if (!f) fprintf(stderr, "[lagoon-motion-log] cannot write %s=%s\n", env, path);
  return f;
}

static void init_once(void) {
  if (s_init) return;
  s_init = 1;
  s_motion = open_log("LAGOON_MOTION_LOG");
  s_oam = open_log("LAGOON_OAM_LOG");
  if (s_motion)
    fprintf(s_motion, "# lagoon motion log v1: see src/lagoon_motion_log.c for the format\n");
  if (s_oam)
    fprintf(s_oam, "# lagoon oam log v1: F frame obsel=<OBSEL> n=<count> "
                   "slot:x,y,tile,attr,big ... (x signed decimal, y decimal, rest hex)\n");
}

static unsigned word(const uint8_t *ram, unsigned a) {
  return (unsigned)ram[a] | ((unsigned)ram[a + 1] << 8);
}

void LagoonMotionLog_ObserveLine(const Ppu *ppu, unsigned line) {
  init_once();
  if (!s_motion || !ppu || line >= kLines) return;
  for (int l = 0; l < kLayers; ++l) {
    s_line_scroll[line][l * 2] = ppu->hScroll[l];
    s_line_scroll[line][l * 2 + 1] = ppu->vScroll[l];
  }
  s_line_seen[line] = 1;
}

static void write_bands(void) {
  /* Runs of lines whose six scroll values are equal. Written only when there
   * is more than one run: a split or a per-line effect (HDMA). */
  int first = -1, runs = 0;
  for (unsigned y = 1; y < kLines; ++y) {
    if (!s_line_seen[y]) continue;
    if (first < 0) { first = (int)y; runs = 1; continue; }
    if (memcmp(s_line_scroll[y], s_line_scroll[y - 1], sizeof s_line_scroll[y]) ||
        !s_line_seen[y - 1])
      ++runs;
  }
  if (runs < 2) return;
  fputs(" b=", s_motion);
  int start = first, sep = 0;
  for (unsigned y = (unsigned)first + 1; y <= kLines; ++y) {
    int end_run = y == kLines || !s_line_seen[y] ||
        memcmp(s_line_scroll[y], s_line_scroll[y - 1], sizeof s_line_scroll[y]);
    if (!end_run) continue;
    const uint16_t *v = s_line_scroll[start];
    fprintf(s_motion, "%s%d-%u:%x,%x,%x,%x,%x,%x", sep ? "|" : "", start, y - 1,
            v[0], v[1], v[2], v[3], v[4], v[5]);
    sep = 1;
    while (y < kLines && !s_line_seen[y]) ++y;
    start = (int)y;
  }
}

static void write_oam(unsigned frame, const Ppu *ppu) {
  unsigned count = 0;
  char buf[128 * 32];
  buf[0] = 0;
  size_t len = 0;
  for (unsigned slot = 0; slot < 128; ++slot) {
    unsigned index = slot * 2;
    int x = ppu->oam[index] & 0xff;
    x |= ((ppu->highOam[index >> 3] >> (index & 7)) & 1) << 8;
    if (x >= 256) x -= 512;
    unsigned y = ppu->oam[index] >> 8;
    unsigned big = (ppu->highOam[index >> 3] >> ((index & 7) + 1)) & 1;
    unsigned tile = ppu->oam[index + 1] & 0xff;
    unsigned attr = ppu->oam[index + 1] >> 8;
    /* Hidden: parked below the picture (224..239) or fully off the sides.
     * Sprites with y >= 240 wrap to the top and can be partly visible. */
    if ((y >= 224 && y < 240) || x <= -64) continue;
    int n = snprintf(buf + len, sizeof buf - len, " %u:%d,%u,%x,%x,%u",
                     slot, x, y, tile, attr, big);
    if (n < 0 || (size_t)n >= sizeof buf - len) break;
    len += (size_t)n;
    ++count;
  }
  fprintf(s_oam, "F %u obsel=%x n=%u%s\n", frame, ppu->obsel, count, buf);
}

void LagoonMotionLog_EndFrame(unsigned frame, const Ppu *ppu,
                              const uint8_t *ram, uint8_t hdmaen) {
  init_once();
  if (!ppu || !ram) return;
  if (s_oam) write_oam(frame, ppu);
  if (!s_motion) return;

  fprintf(s_motion, "F %u t=%x r=%x i=%x m=%x h=%x c=%x,%x,%x,%x s=%x,%x,%x,%x,%x,%x",
          frame, ram[0x408], word(ram, 0x48e), ppu->inidisp, ppu->bgmode, hdmaen,
          word(ram, 0x480), word(ram, 0x482), word(ram, 0x484), word(ram, 0x486),
          ppu->hScroll[0], ppu->vScroll[0], ppu->hScroll[1], ppu->vScroll[1],
          ppu->hScroll[2], ppu->vScroll[2]);
  write_bands();

  /* Actor table: 32 slots of $40 bytes from $0500; slot 0 is Nasir.
   * Bits $C0 of the first byte mark a live slot (as lagoon_qol.c tests). */
  fprintf(s_motion, " p=%x,%x a=", word(ram, 0x502), word(ram, 0x504));
  int any = 0;
  for (unsigned slot = 1; slot < 32; ++slot) {
    unsigned a = 0x500 + slot * 0x40;
    if ((ram[a] & 0xc0) != 0xc0) continue;
    fprintf(s_motion, "%s%u:%x,%x,%x", any ? ";" : "", slot,
            word(ram, a + 2), word(ram, a + 4), ram[a + 24]);
    any = 1;
  }

  if (s_have_cgram && memcmp(s_last_cgram, ppu->cgram, sizeof s_last_cgram)) {
    fputs(" pal=", s_motion);
    int sep = 0;
    for (unsigned i = 0; i < 256; ++i)
      if (s_last_cgram[i] != ppu->cgram[i]) {
        fprintf(s_motion, "%s%x:%x", sep ? "," : "", i, ppu->cgram[i]);
        sep = 1;
      }
  }
  memcpy(s_last_cgram, ppu->cgram, sizeof s_last_cgram);
  s_have_cgram = 1;
  fputc('\n', s_motion);
  memset(s_line_seen, 0, sizeof s_line_seen);
  if (frame % 60 == 0) {
    fflush(s_motion);
    if (s_oam) fflush(s_oam);
  }
}
