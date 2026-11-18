/*
 * lagoon_smooth.c -- in-between pictures for Lagoon. See lagoon_smooth.h.
 * gabbycat4 fork, Step 4.1 (round 3: character-based motion).
 *
 * Timeline. The game's main tick ($0408) advances every second field; that is
 * a "step": the camera ($0480/$0482) and the actors (table at $0500) move.
 * Let `to` be the latest step and `from` the one before. A picture presented
 * tau fields after the step field (whole fields + the host's alpha) shows
 *     value = to - (1 - t) * (to - from),   t = tau / period (normally 2)
 * for the camera and for every actor. Cost: up to two fields (33 ms) of delay.
 *
 * Why the actor table and not the sprites: a character's sprite pieces change
 * when its pose changes (walk cycle, sword swing), and following the pieces
 * slid them from the old pose to the new one ("shaking"). Each piece is tied
 * to the nearest character in the to-frame and moves exactly as that character
 * moves; its pose appears at once. Pieces near no character follow their own
 * motion when they keep their picture, else stay fixed in the world.
 *
 * Camera: timing from the game's camera words; placement from the scroll.
 * The game draws the background AND the sprites with its scroll, which can
 * follow the camera word one field late. The whole packet is moved from its
 * scroll to the interpolated camera, so ground and sprites always agree.
 * Outside the area the original field showed, the background comes from the
 * room map (lagoon_scene_renderer.c), never from not-yet-streamed tiles.
 *
 * Rounding: camera and actors are rounded with the same fraction, so a thing
 * standing still stays on the ground and Nasir (who the camera follows) stays
 * exactly where the game puts him on screen.
 */
#include "lagoon_smooth.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lagoon_scene_renderer.h"

enum { kMaxWidth = 1280, kHeight = 224, kMaxStep = 8, kActors = 32, kRing = 16 };

/* Game state after one simulated field (read from WRAM in after_run_frame). */
typedef struct Record {
  unsigned frame, valid;
  uint8_t tick;
  int cx, cy;
  uint8_t live[kActors];
  int ax[kActors], ay[kActors];
} Record;

/* Interpolation end points: camera and actors (doubles so a step that arrives
 * early can start from the in-between values actually on screen). */
typedef struct State {
  double cx, cy;
  uint8_t live[kActors];
  double ax[kActors], ay[kActors];
} State;

static unsigned s_pps = 60;
static int s_second_core = 1;           /* [Lagoon] SecondCore, LAGOON_SECOND_CORE */
static int s_base_ok, s_calm, s_last_shift_x, s_last_shift_y;
static unsigned s_base_width;
static Record s_ring[kRing];
static Record s_rec_to;
static State s_from, s_to;
static LagoonSceneMotion s_pkt, s_pkt_from, s_pos, s_rendered, s_cur;
static int s_have_to, s_have_from, s_have_pkt_from, s_checked, s_rendered_valid;
static unsigned s_step_frame, s_period = 2, s_rendered_width, s_picture_in_field;
static int8_t s_owner[128];
static int16_t s_obj_owner[LAGOON_SCENE_MOTION_OBJECTS];
static uint32_t s_buf[kMaxWidth * kHeight];
static FILE *s_log;
static unsigned long long s_steps, s_checks_ok, s_checks_failed, s_drawn, s_renders, s_owned, s_free;

static unsigned valid_pps(long v) {
  return v == 120 || v == 180 || v == 240 ? (unsigned)v : 60;
}

static void read_ini(const char *path) {
  FILE *f = path ? fopen(path, "r") : NULL;
  if (!f) return;
  char line[512], section[64] = "";
  while (fgets(line, sizeof line, f)) {
    char name[64], value[64];
    const char *in = line;
    if ((unsigned char)in[0] == 0xef && (unsigned char)in[1] == 0xbb && (unsigned char)in[2] == 0xbf) in += 3;
    if (sscanf(in, " [%63[^]]]", name) == 1) { snprintf(section, sizeof section, "%s", name); continue; }
    if (strcmp(section, "Lagoon") || sscanf(in, " %63[^=] = %63s", name, value) != 2) continue;
    size_t n = strlen(name);
    while (n && isspace((unsigned char)name[n - 1])) name[--n] = 0;
    if (!strcmp(name, "PicturesPerSecond")) s_pps = valid_pps(strtol(value, NULL, 10));
    if (!strcmp(name, "SecondCore")) s_second_core = strtol(value, NULL, 10) != 0;
  }
  fclose(f);
}

static void report(void) {
  fprintf(stderr, "[lagoon-smooth] pictures_per_second=%u steps=%llu checks_ok=%llu checks_failed=%llu "
          "inbetween_pictures=%llu renders=%llu pieces_with_character=%llu pieces_alone=%llu\n",
          s_pps, s_steps, s_checks_ok, s_checks_failed, s_drawn, s_renders, s_owned, s_free);
  if (s_log) fclose(s_log);
}

void LagoonSmooth_AfterConfig(const char *config_path) {
  static int once;
  s_pps = 60;
  s_second_core = 1;
  read_ini(config_path);
  read_ini("config.local.ini");
  const char *env = getenv("LAGOON_PICTURES");
  if (env && *env) s_pps = valid_pps(strtol(env, NULL, 10));
  const char *core = getenv("LAGOON_SECOND_CORE");
  if (core && *core) s_second_core = strtol(core, NULL, 10) != 0;
  if (!once) {
    once = 1;
    const char *path = getenv("LAGOON_SMOOTH_LOG");
    if (path && *path) {
      s_log = fopen(path, "w");
      if (s_log) fprintf(s_log, "# frame alpha t drawn reason (one line per presented picture)\n");
    }
    atexit(report);
  }
  LagoonSmooth_Reset();
}

void LagoonSmooth_Reset(void) {
  s_have_to = s_have_from = s_have_pkt_from = s_checked = s_rendered_valid = 0;
  s_base_ok = 0;
  s_period = 2;
  memset(s_ring, 0, sizeof s_ring);
}

unsigned LagoonSmooth_PicturesPerSecond(void) { return s_pps; }
int LagoonSmooth_SecondCore(void) { return s_pps > 60 && s_second_core; }
void LagoonSmooth_BeginPicture(double alpha) {
  s_last_shift_x = s_last_shift_y = 0;
  if (alpha < 0) s_base_ok = 0;
}
void LagoonSmooth_SetCalm(int calm) { s_calm = calm != 0; }
void LagoonSmooth_LastShift(int *x, int *y) {
  if (x) *x = s_last_shift_x;
  if (y) *y = s_last_shift_y;
}

double LagoonSmooth_PresentationHz(double display_refresh) {
  (void)display_refresh;
  /* An exact multiple of the SNES field rate (180 -> 180.30), so pictures and
   * fields keep the same phase instead of drifting through each other. */
  return s_pps > 60 ? (double)(s_pps / 60) * 60.098811862 : 0;
}

static unsigned word(const uint8_t *ram, unsigned a) {
  return (unsigned)ram[a] | ((unsigned)ram[a + 1] << 8);
}

void LagoonSmooth_AfterFrame(unsigned frame, const uint8_t *ram) {
  if (s_pps <= 60 || !ram) return;
  Record *r = &s_ring[frame % kRing];
  r->frame = frame; r->valid = 1;
  r->tick = ram[0x408];
  r->cx = (int)word(ram, 0x480); r->cy = (int)word(ram, 0x482);
  for (unsigned k = 0; k < kActors; ++k) {
    unsigned a = 0x500 + k * 0x40;
    r->live[k] = k == 0 || (ram[a] & 0xc0) == 0xc0;
    r->ax[k] = (int)word(ram, a + 2); r->ay[k] = (int)word(ram, a + 4);
  }
}

static const Record *record_for(unsigned frame) {
  const Record *r = &s_ring[frame % kRing];
  return r->valid && r->frame == frame ? r : NULL;
}

static void state_from_record(State *s, const Record *r) {
  s->cx = r->cx; s->cy = r->cy;
  for (unsigned k = 0; k < kActors; ++k) { s->live[k] = r->live[k]; s->ax[k] = r->ax[k]; s->ay[k] = r->ay[k]; }
}

static int small(double d) { return d >= -kMaxStep && d <= kMaxStep; }
static int sy(uint8_t y) { return y >= 240 ? (int)y - 256 : (int)y; }
static double lerp(double a, double b, double t) { return b - (1.0 - t) * (b - a); }

/* Interpolated camera/actors at t into *out. Actors that teleported keep `to`. */
static void state_at(double t, State *out) {
  out->cx = lerp(s_from.cx, s_to.cx, t);
  out->cy = lerp(s_from.cy, s_to.cy, t);
  for (unsigned k = 0; k < kActors; ++k) {
    out->live[k] = s_to.live[k];
    int ok = s_to.live[k] && s_from.live[k] &&
        small(s_to.ax[k] - s_from.ax[k]) && small(s_to.ay[k] - s_from.ay[k]);
    out->ax[k] = ok ? lerp(s_from.ax[k], s_to.ax[k], t) : s_to.ax[k];
    out->ay[k] = ok ? lerp(s_from.ay[k], s_to.ay[k], t) : s_to.ay[k];
  }
}

/* Tie each sprite piece of the to-packet to its character. Candidates are the
 * characters whose box holds the piece (measured from the logs: a character's
 * pieces lie within about 24 px left/right of its position and from 40 px
 * above it to a few px below). When the piece kept its picture since the last
 * step, the candidate whose movement explains the piece's own movement wins:
 * next to a standing guard, Nasir's pieces moved with Nasir. Otherwise the
 * nearest candidate, or the owner of the neighbouring list slot. */
enum { kOwnerNone = -1, kOwnerScreen = -2, kHudTop = 180 };

static int owner_of(int x, int y, int have_motion, int odx, int ody, int *by_motion) {
  int best = kOwnerNone, best_score = 1 << 30;
  *by_motion = 0;
  for (unsigned k = 0; k < kActors; ++k) {
    if (!s_rec_to.live[k]) continue;
    int sx = s_rec_to.ax[k] - s_pkt.wx, sy0 = s_rec_to.ay[k] - s_pkt.wy;
    int dx = x - sx, dy = y - sy0;
    if (dx < -28 || dx > 28 || dy < -44 || dy > 12) continue;
    int score = abs(dx + 4) + abs(dy + 12);
    if (have_motion) {
      double pdx = (s_to.ax[k] - s_from.ax[k]) - (s_pkt.wx - s_pkt_from.wx);
      double pdy = (s_to.ay[k] - s_from.ay[k]) - (s_pkt.wy - s_pkt_from.wy);
      score += 1000 * (int)lround(fabs(odx - pdx) + fabs(ody - pdy));
    }
    if (score < best_score) { best_score = score; best = (int)k; }
  }
  if (have_motion && best >= 0 && best_score < 1000) *by_motion = 1;
  return best;
}

static int in_box(int k, int x, int y) {
  if (k < 0 || !s_rec_to.live[k]) return 0;
  int dx = x - (s_rec_to.ax[k] - s_pkt.wx), dy = y - (s_rec_to.ay[k] - s_pkt.wy);
  return dx >= -28 && dx <= 28 && dy >= -44 && dy <= 12;
}

static void assign_owners(void) {
  uint8_t sure[128] = {0};
  int motion = s_have_from && s_have_pkt_from;
  for (unsigned i = 0; i < 128; ++i) {
    s_owner[i] = kOwnerNone;
    if (!s_pkt.visible[i]) continue;
    int x = s_pkt.ox[i], y = sy(s_pkt.oy[i]);
    int same = motion && s_pkt_from.visible[i] && s_pkt_from.tile[i] == s_pkt.tile[i];
    /* HUD sprites (the item icon): bottom band, same picture, same screen
     * position: fixed on screen, never moved. */
    if (same && y >= kHudTop && s_pkt_from.ox[i] == s_pkt.ox[i] && s_pkt_from.oy[i] == s_pkt.oy[i]) {
      s_owner[i] = kOwnerScreen; sure[i] = 1; continue;
    }
    int by_motion;
    s_owner[i] = (int8_t)owner_of(x, y, same, same ? x - s_pkt_from.ox[i] : 0,
                                  same ? y - sy(s_pkt_from.oy[i]) : 0, &by_motion);
    sure[i] = (uint8_t)by_motion;
  }
  /* A piece that changed its picture takes the owner of a neighbouring list
   * slot that was decided by movement, when that character's box holds it
   * (a character's pieces sit next to each other in the sprite list). */
  for (unsigned i = 0; i < 128; ++i) {
    if (!s_pkt.visible[i] || sure[i] || s_owner[i] == kOwnerScreen) continue;
    int x = s_pkt.ox[i], y = sy(s_pkt.oy[i]);
    for (int d = 1; d <= 3; ++d) {
      int a = (int)i - d, b = (int)i + d, k = kOwnerNone;
      if (a >= 0 && sure[a] && s_owner[a] >= 0 && in_box(s_owner[a], x, y)) k = s_owner[a];
      else if (b < 128 && sure[b] && s_owner[b] >= 0 && in_box(s_owner[b], x, y)) k = s_owner[b];
      if (k != kOwnerNone) { s_owner[i] = (int8_t)k; break; }
    }
  }
  int unused;
  for (unsigned i = 0; i < s_pkt.count; ++i)
    s_obj_owner[i] = (int16_t)owner_of(s_pkt.objx[i], s_pkt.objy[i], 0, 0, 0, &unused);
}

/* The to-packet redrawn at fraction t. Returns 0 when the camera jumped. */
static int positions_at(double t, LagoonSceneMotion *out) {
  if (!small(s_to.cx - s_from.cx) || !small(s_to.cy - s_from.cy)) return 0;
  State now;
  state_at(t, &now);
  *out = s_pkt;
  int cx = (int)lround(now.cx), cy = (int)lround(now.cy);
  /* The whole packet (background AND sprites, both placed by the game with
   * its scroll) moves to the interpolated camera. The scroll can trail the
   * camera word by a field; moving both together keeps them in agreement. */
  int bx = cx - s_pkt.wx, by = cy - s_pkt.wy;
  if (!small(bx / 2.0) || !small(by / 2.0)) return 0;
  out->wx = cx; out->wy = cy;
  out->native_x = s_pkt.native_x + bx; out->native_y = s_pkt.native_y + by;
  if (s_have_pkt_from && s_pkt.parallax && s_pkt_from.parallax) {
    int d = s_pkt.bg2_x - s_pkt_from.bg2_x, e = s_pkt.bg2_y - s_pkt_from.bg2_y;
    if (small(d)) out->bg2_x = s_pkt.bg2_x - (int)lround((1.0 - t) * d);
    if (small(e)) out->bg2_y = s_pkt.bg2_y - (int)lround((1.0 - t) * e);
  }
  /* Screen position of a piece = its offset from its character in the
   * to-frame + where that character is now relative to the camera now. */
  for (unsigned i = 0; i < 128; ++i) {
    if (!s_pkt.visible[i]) continue;
    int k = s_owner[i], mx, my;
    if (k == kOwnerScreen) continue;  /* HUD: exactly where the game put it */
    if (k >= 0) {
      mx = (int)lround(now.ax[k]) - s_rec_to.ax[k];
      my = (int)lround(now.ay[k]) - s_rec_to.ay[k];
    } else if (s_have_pkt_from && s_pkt_from.visible[i] && s_pkt_from.tile[i] == s_pkt.tile[i]) {
      /* No character nearby, same picture: its own motion in the world. */
      double wx1 = s_pkt.ox[i] + s_pkt.wx, wx0 = s_pkt_from.ox[i] + s_pkt_from.wx;
      double wy1 = sy(s_pkt.oy[i]) + s_pkt.wy, wy0 = sy(s_pkt_from.oy[i]) + s_pkt_from.wy;
      if (small(wx1 - wx0) && small(wy1 - wy0)) {
        mx = (int)lround(lerp(wx0, wx1, t)) - (int)wx1;
        my = (int)lround(lerp(wy0, wy1, t)) - (int)wy1;
      } else mx = my = 0;
    } else mx = my = 0;  /* fixed in the world */
    out->ox[i] = (int16_t)(s_pkt.ox[i] + mx - bx);
    out->oy[i] = (uint8_t)((sy(s_pkt.oy[i]) + my - by) & 255);
  }
  for (unsigned i = 0; i < s_pkt.count; ++i) {
    int k = s_obj_owner[i], mx = 0, my = 0;
    if (k >= 0) {
      mx = (int)lround(now.ax[k]) - s_rec_to.ax[k];
      my = (int)lround(now.ay[k]) - s_rec_to.ay[k];
    }
    out->objx[i] = s_pkt.objx[i] + mx - bx;
    out->objy[i] = s_pkt.objy[i] + my - by;
  }
  return 1;
}

/* Where this picture sits within its field, by COUNT, not by clock: the host
 * shows pps/60 pictures per field, the last one just before the next field
 * runs (alpha 1). Timing jitter of a fraction of a millisecond used to flip
 * the rounding of half-pixel positions back and forth (ragged 0/1 pixel
 * camera moves = stutter). Counting gives exactly k/n. */
static double picture_phase(double alpha) {
  unsigned n = s_pps / 60;
  unsigned k = alpha >= 0.999 ? n : s_picture_in_field;
  if (k > n) k = n;
  return n ? (double)k / (double)n : alpha;
}

static double current_t(double alpha) {
  double tau = (double)(s_pkt.frame - s_step_frame) + picture_phase(alpha);
  double t = tau / (double)s_period;
  return t < 0 ? 0 : t > 1 ? 1 : t;
}

/* Compare a redraw with the picture in dst: differing pixels + bounding box. */
static unsigned compare(const uint8_t *dst, size_t pitch, unsigned width, unsigned box[4]) {
  unsigned n = 0;
  box[0] = width; box[1] = kHeight; box[2] = box[3] = 0;
  for (unsigned y = 0; y < kHeight; ++y) {
    const uint32_t *a = (const uint32_t *)(dst + (size_t)y * pitch), *b = s_buf + (size_t)y * width;
    if (!memcmp(a, b, (size_t)width * 4)) continue;
    for (unsigned x = 0; x < width; ++x)
      if (a[x] != b[x]) {
        ++n;
        if (x < box[0]) box[0] = x;
        if (x > box[2]) box[2] = x;
        if (y < box[1]) box[1] = y;
        if (y > box[3]) box[3] = y;
      }
  }
  return n;
}

/* Self-check: our redraw of the packet at its own positions must equal the
 * verified picture already in dst, or no in-between picture is drawn. */
static int check(const uint8_t *dst, size_t pitch, unsigned width) {
  unsigned box[4], n;
  s_rendered_valid = 0;
  ++s_renders;
  if (!LagoonSceneRenderer_RenderMotion(s_buf, width, &s_pkt)) return -1;
  if ((n = compare(dst, pitch, width, box)) != 0) {
    if (s_log)
      fprintf(s_log, "%u check-diff room=%x n=%u x=%u-%u y=%u-%u\n", s_pkt.frame, s_pkt.room,
              n, box[0], box[2], box[1], box[3]);
    return -1;
  }
  return 1;
}

static void log_line(unsigned frame, double alpha, double t, int drawn, const char *why) {
  if (s_log) fprintf(s_log, "%u %.3f %.3f %d %s\n", frame, alpha, t, drawn, why);
}

static void new_packet(const uint8_t *dst, size_t pitch, unsigned width) {
  const Record *rec = record_for(s_cur.frame);
  int same_scene = s_have_to && rec && s_cur.generation == s_pkt.generation &&
      s_cur.room == s_pkt.room && s_cur.parallax == s_pkt.parallax && s_cur.frame > s_pkt.frame;
  if (!rec) {
    s_have_to = s_have_from = 0;
    return;
  }
  if (!same_scene) {
    s_pkt = s_cur; s_rec_to = *rec; state_from_record(&s_to, rec);
    s_have_to = 1; s_have_from = s_have_pkt_from = 0; s_step_frame = s_cur.frame;
  } else if (rec->tick == s_rec_to.tick) {
    /* Same game step (the in-between field): new pose/graphics, same motion.
     * The camera word does not change here; the scroll may catch up. */
    s_pkt = s_cur; s_rec_to = *rec;
  } else {
    unsigned elapsed = s_cur.frame - s_step_frame;
    double t_now = (double)elapsed / (double)s_period;
    State start;
    if (s_have_from && t_now < 1) state_at(t_now, &start); else start = s_to;
    s_from = start;
    s_pkt_from = s_pkt; s_have_pkt_from = 1;
    s_pkt = s_cur; s_rec_to = *rec; state_from_record(&s_to, rec);
    s_period = elapsed >= 1 && elapsed <= 4 ? elapsed : 2;
    s_step_frame = s_cur.frame;
    s_have_from = 1;
    ++s_steps;
  }
  assign_owners();
  for (unsigned i = 0; i < 128; ++i)
    if (s_pkt.visible[i]) { if (s_owner[i] >= 0) ++s_owned; else ++s_free; }
  /* The redraw of the packet at its own positions is the same drawing from
   * the same data as the verified picture; checking it every field cost a
   * whole extra redraw on the busiest picture. Check once a second (and at
   * every new scene), keep the last verdict in between. */
  static unsigned packets_since_check;
  if (!s_have_from || s_checked <= 0 || ++packets_since_check >= 60) {
    packets_since_check = 0;
    s_checked = check(dst, pitch, width);
    if (s_checked > 0) ++s_checks_ok; else ++s_checks_failed;
  }
  if (s_log && s_have_from)
    fprintf(s_log, "%u step-info room=%x tick=%x cam=%d,%d scroll-lag=%d,%d period=%u\n", s_pkt.frame,
            s_pkt.room, s_rec_to.tick, s_rec_to.cx, s_rec_to.cy, s_rec_to.cx - s_pkt.wx,
            s_rec_to.cy - s_pkt.wy, s_period);
}

static int same_draw(const LagoonSceneMotion *a, const LagoonSceneMotion *b) {
  return a->serial == b->serial && a->wx == b->wx && a->wy == b->wy && a->bg2_x == b->bg2_x &&
      a->bg2_y == b->bg2_y && a->native_x == b->native_x && a->native_y == b->native_y &&
      !memcmp(a->ox, b->ox, sizeof a->ox) && !memcmp(a->oy, b->oy, sizeof a->oy) &&
      a->count == b->count &&
      !memcmp(a->objx, b->objx, sizeof(a->objx[0]) * a->count) &&
      !memcmp(a->objy, b->objy, sizeof(a->objy[0]) * a->count);
}

void LagoonSmooth_Draw(uint8_t *dst, size_t pitch, unsigned width, unsigned height, double alpha) {
  if (s_pps <= 60 || !dst || height != kHeight || width > kMaxWidth || width < 256) return;
  if (!LagoonSceneRenderer_Motion(&s_cur)) {
    s_have_to = s_have_from = 0;
    log_line(0, alpha, 1, 0, "no-packet");
    return;
  }
  if (!s_have_to || s_cur.serial != s_pkt.serial) { new_packet(dst, pitch, width); s_picture_in_field = 0; }
  if (alpha < 0) {
    /* Second core: the field's base picture (composed, verified, not shown).
     * Its pictures come later through LagoonSmooth_DrawInbetween. */
    s_base_ok = s_calm && s_have_to && s_have_from && s_checked > 0;
    s_base_width = width;
    return;
  }
  ++s_picture_in_field;
  if (!s_have_to) { log_line(s_cur.frame, alpha, 1, 0, "no-record"); return; }
  if (!s_have_from) { log_line(s_pkt.frame, alpha, 1, 0, "still"); return; }
  if (s_checked <= 0) { log_line(s_pkt.frame, alpha, 1, 0, "check-failed"); return; }
  double t = current_t(alpha);
  if (!positions_at(t, &s_pos)) { log_line(s_pkt.frame, alpha, t, 0, "jump"); return; }
  /* t = 1 is drawn too: there the scroll's one-field lag behind the camera
   * is corrected. Positions identical to the packet reuse dst as it is. */
  if (same_draw(&s_pos, &s_pkt)) { log_line(s_pkt.frame, alpha, t, 0, "same-pixels"); return; }
  if (!s_rendered_valid || s_rendered_width != width || !same_draw(&s_rendered, &s_pos)) {
    ++s_renders;
    if (!LagoonSceneRenderer_RenderMotion(s_buf, width, &s_pos)) {
      s_rendered_valid = 0;
      log_line(s_pkt.frame, alpha, t, 0, "render-refused");
      return;
    }
    s_rendered = s_pos; s_rendered_width = width; s_rendered_valid = 1;
  }
  for (unsigned y = 0; y < kHeight; ++y)
    memcpy(dst + (size_t)y * pitch, s_buf + (size_t)y * width, (size_t)width * 4);
  ++s_drawn;
  s_last_shift_x = s_pos.wx - s_pkt.wx; s_last_shift_y = s_pos.wy - s_pkt.wy;
  log_line(s_pkt.frame, alpha, t, 1, "inbetween");
}

int LagoonSmooth_DrawInbetween(uint8_t *dst, size_t pitch, unsigned width, unsigned height,
                               double alpha, int *shift_x, int *shift_y) {
  /* Second core: called while the guest may be running. Reads only this
   * module's state and the scene renderer's completed packet, both written
   * on the main thread while the guest was idle. */
  *shift_x = *shift_y = 0;
  if (s_pps <= 60 || !dst || !s_base_ok || height != kHeight || width != s_base_width) {
    log_line(s_pkt.frame, alpha, 1, 0, "hold");
    return 0;
  }
  ++s_picture_in_field;
  double t = current_t(alpha);
  if (!positions_at(t, &s_pos)) { log_line(s_pkt.frame, alpha, t, 0, "jump"); return 0; }
  if (!s_rendered_valid || s_rendered_width != width || !same_draw(&s_rendered, &s_pos)) {
    ++s_renders;
    if (!LagoonSceneRenderer_RenderMotion(s_buf, width, &s_pos)) {
      s_rendered_valid = 0;
      log_line(s_pkt.frame, alpha, t, 0, "render-refused");
      return 0;
    }
    s_rendered = s_pos; s_rendered_width = width; s_rendered_valid = 1;
  }
  for (unsigned y = 0; y < kHeight; ++y)
    memcpy(dst + (size_t)y * pitch, s_buf + (size_t)y * width, (size_t)width * 4);
  ++s_drawn;
  *shift_x = s_pos.wx - s_pkt.wx; *shift_y = s_pos.wy - s_pkt.wy;
  log_line(s_pkt.frame, alpha, t, 1, "inbetween-2nd-core");
  return 1;
}
