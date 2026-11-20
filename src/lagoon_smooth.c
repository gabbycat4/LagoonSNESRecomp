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
/* Engine (runner/src/beam_frame_driver.h, gabbycat4 patch): optional faster CPU. */
void snes_beam_frame_driver_set_cpu_speed(unsigned percent);
unsigned snes_beam_frame_driver_cpu_speed(void);
void snes_beam_frame_driver_cpu_speed_stats(uint64_t *fields, uint64_t *fields_helped,
                                            uint64_t *clocks_returned);

enum { kMaxWidth = 1280, kHeight = 224, kMaxStep = 8, kActors = 32, kRing = 16 };

/* Game state after one simulated field (read from WRAM in after_run_frame). */
typedef struct Record {
  unsigned frame, valid;
  uint8_t tick;
  int cx, cy;
  uint8_t live[kActors], type[kActors];
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
static int s_half_pixels = 1;           /* [Lagoon] HalfPixels, LAGOON_HALF_PIXELS */
static int s_smooth_camera = 1;         /* [Lagoon] SmoothCamera, LAGOON_SMOOTH_CAMERA */
static unsigned s_cpu_speed = 100;      /* [Lagoon] CpuSpeed, LAGOON_CPU_SPEED (100 = original) */
static volatile unsigned s_cpu_speed_wanted = 100;   /* Comfort menu, applied on the game thread */
enum { kCamAvg = 16 };
static double s_camq[kCamAvg][2];
static unsigned s_camq_n, s_camq_pos, s_camq_frame, s_camq_pic;
static double s_camq_t = -1, s_cam_out[2];
static uint32_t s_buf2[1024 * 2 * kHeight];
static LagoonSceneMotion s_rendered2;
static int s_rendered2_valid, s_half_ok;
static unsigned s_rendered2_width;
static int s_base_ok, s_calm, s_last_shift_x, s_last_shift_y;
static unsigned s_base_width;
static Record s_ring[kRing];
static Record s_rec_to;
/* Sprite reference: the game builds its sprite list one field LATE, from the
 * previous field's actor positions AND the previous field's camera word
 * (measured: 66 of 67 moving fields; backgrounds use the current scroll).
 * Pieces are placed relative to that record and that camera, not to the
 * packet's scroll. With the camera standing still (church, map edges) the old
 * reference put Nasir one step off on every other field: a double image. */
enum { kCamBaseX = 0, kCamBaseY = -1 };  /* camera word - packet origin, scroll not trailing */
static Record s_spr, s_spr_from;
static int s_spr_ox, s_spr_oy, s_spr_from_ox, s_spr_from_oy;
/* The game rebuilds its sprite list only on step fields (measured Nov 19:
 * the list is unchanged on 3635 of 3683 in-between fields in town, 675 of
 * 677 in the cave). An unchanged list keeps the reference of the field that
 * built it: Nasir's record/camera from the field BEFORE that one, and for
 * every other piece that field's record and scroll. */
static Record s_ref_rec;
static int s_ref_wx, s_ref_wy, s_ref_from_wx, s_ref_from_wy;
static LagoonSceneMotion s_last;   /* the packet before this one (owner ties) */
static int8_t s_last_owner[128];
static int s_have_last;
static State s_from, s_to;
static LagoonSceneMotion s_pkt, s_pkt_from, s_pos, s_rendered, s_cur;
static int s_have_to, s_have_from, s_have_pkt_from, s_checked, s_rendered_valid;
static unsigned s_step_frame, s_rendered_width, s_picture_in_field;
static double s_period = 2;   /* fields per game step: average of the last 8 */
static uint8_t s_period_hist[8];
static unsigned s_period_pos;
static int s_step_real;   /* s_step_frame is a real step (not a scene start) */
static int8_t s_owner[128];
/* Palettes each character's pieces use (round 7c), learned from pieces whose
 * owner was decided by movement. Nasir uses palettes 0/1, the guards 3, town
 * people 2...; when two characters overlap, a piece goes to the one whose
 * palette it has. */
static uint32_t s_pal_n[kActors][8];
static uint8_t s_pal_type[kActors];
static uint8_t s_late[kActors];           /* round 10: sprites built one field late */
static uint16_t s_late_n[kActors][2];     /* [0] on time, [1] one field late */
static int16_t s_obj_owner[LAGOON_SCENE_MOTION_OBJECTS];
static uint32_t s_buf[kMaxWidth * kHeight];
static FILE *s_log;
static unsigned long long s_steps, s_checks_ok, s_checks_failed, s_drawn, s_renders, s_owned, s_free;

static unsigned valid_pps(long v) {
  return v == 120 || v == 180 || v == 240 ? (unsigned)v : 60;
}

static unsigned valid_speed(long v) { return v < 100 ? 100 : v > 400 ? 400 : (unsigned)v; }

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
    if (!strcmp(name, "HalfPixels")) s_half_pixels = strtol(value, NULL, 10) != 0;
    if (!strcmp(name, "SmoothCamera")) s_smooth_camera = strtol(value, NULL, 10) != 0;
    if (!strcmp(name, "CpuSpeed")) s_cpu_speed = valid_speed(strtol(value, NULL, 10));
  }
  fclose(f);
}

static void report(void) {
  uint64_t fields = 0, helped = 0, returned = 0;
  snes_beam_frame_driver_cpu_speed_stats(&fields, &helped, &returned);
  fprintf(stderr, "[lagoon-cpu-speed] cpu_speed=%u fields=%llu fields_given_time=%llu "
          "clocks_given_per_such_field=%.0f\n", snes_beam_frame_driver_cpu_speed(),
          (unsigned long long)fields, (unsigned long long)helped,
          helped ? (double)returned / (double)helped : 0.0);
  fprintf(stderr, "[lagoon-smooth] pictures_per_second=%u second_core=%d half_pixels=%d smooth_camera=%d "
          "steps=%llu checks_ok=%llu checks_failed=%llu "
          "inbetween_pictures=%llu renders=%llu pieces_with_character=%llu pieces_alone=%llu\n",
          s_pps, s_second_core, s_half_pixels, s_smooth_camera, s_steps, s_checks_ok, s_checks_failed, s_drawn, s_renders, s_owned, s_free);
  if (s_log) fclose(s_log);
}

void LagoonSmooth_AfterConfig(const char *config_path) {
  static int once;
  s_pps = 60;
  s_second_core = 1;
  s_half_pixels = 1;
  s_cpu_speed = 100;
  read_ini(config_path);
  read_ini("config.local.ini");
  const char *env = getenv("LAGOON_PICTURES");
  if (env && *env) s_pps = valid_pps(strtol(env, NULL, 10));
  const char *core = getenv("LAGOON_SECOND_CORE");
  if (core && *core) s_second_core = strtol(core, NULL, 10) != 0;
  const char *scam = getenv("LAGOON_SMOOTH_CAMERA");
  if (scam && *scam) s_smooth_camera = strtol(scam, NULL, 10) != 0;
  const char *speed = getenv("LAGOON_CPU_SPEED");
  if (speed && *speed) s_cpu_speed = valid_speed(strtol(speed, NULL, 10));
  snes_beam_frame_driver_set_cpu_speed(s_cpu_speed);
  s_cpu_speed_wanted = s_cpu_speed;
  const char *half = getenv("LAGOON_HALF_PIXELS");
  if (half && *half) s_half_pixels = strtol(half, NULL, 10) != 0;
  if (!once) {
    once = 1;
    const char *path = getenv("LAGOON_SMOOTH_LOG");
    if (path && *path) {
      s_log = fopen(path, "w");
      if (s_log) {
        fprintf(s_log, "# settings pictures_per_second=%u second_core=%d half_pixels=%d smooth_camera=%d cpu_speed=%u\n",
                s_pps, s_second_core, s_half_pixels, s_smooth_camera, s_cpu_speed);
        fprintf(s_log, "# frame alpha t drawn reason (one line per presented picture)\n");
      }
    }
    atexit(report);
  }
  LagoonSmooth_Reset();
}

void LagoonSmooth_Reset(void) {
  s_have_to = s_have_from = s_have_pkt_from = s_checked = s_rendered_valid = 0;
  s_have_last = 0;
  s_camq_n = 0;
  s_base_ok = 0;
  s_rendered2_valid = 0;
  s_period = 2;
  memset(s_period_hist, 0, sizeof s_period_hist);
  memset(s_ring, 0, sizeof s_ring);
  memset(s_pal_n, 0, sizeof s_pal_n);
  memset(s_late, 0, sizeof s_late);
  memset(s_late_n, 0, sizeof s_late_n);
  memset(s_pal_type, 0, sizeof s_pal_type);
}

unsigned LagoonSmooth_PicturesPerSecond(void) { return s_pps; }
int LagoonSmooth_SecondCore(void) { return s_pps > 60 && s_second_core; }
int LagoonSmooth_HalfPixels(int source_w, int source_h) {
  return s_pps > 60 && s_second_core && s_half_pixels && source_h == kHeight &&
         source_w >= 256 && source_w * 2 <= 1024;
}
void LagoonSmooth_BeginPicture(double alpha) {
  s_last_shift_x = s_last_shift_y = 0;
  LagoonSceneRenderer_SetMotionCache(s_pps > 60 && s_second_core && s_half_pixels);
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

void LagoonSmooth_SetSmoothCamera(int on) { s_smooth_camera = on != 0; s_camq_n = 0; }
void LagoonSmooth_SetCpuSpeed(unsigned percent) { s_cpu_speed_wanted = valid_speed(percent); }

void LagoonSmooth_AfterFrame(unsigned frame, const uint8_t *ram) {
  /* Between game frames on the game's thread: the next field starts with a fresh mark. */
  if (s_cpu_speed_wanted != s_cpu_speed) {
    s_cpu_speed = s_cpu_speed_wanted;
    snes_beam_frame_driver_set_cpu_speed(s_cpu_speed);
  }
  if (s_pps <= 60 || !ram) return;
  Record *r = &s_ring[frame % kRing];
  r->frame = frame; r->valid = 1;
  r->tick = ram[0x408];
  r->cx = (int)word(ram, 0x480); r->cy = (int)word(ram, 0x482);
  for (unsigned k = 0; k < kActors; ++k) {
    unsigned a = 0x500 + k * 0x40;
    r->live[k] = k == 0 || (ram[a] & 0xc0) == 0xc0;
    r->type[k] = ram[a + 24];
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

/* Where a character's 8x8 sprite pieces sit (top-left corner relative to its
 * position), measured from the Nov 19 oam.log/motion.log over 270,000 pieces:
 * Nasir (slot 0) x -27..13, y -35..0; type 5 (guards: 16x16 pieces) x -19..3,
 * y -49..-11; type 1 x -13..-4, y -32..-2; others a wide box. The old single
 * box (x -28..28, y -44..12) missed a guard's head and reached far below
 * characters, so Nasir walking under a guard could take its head, or the
 * guard Nasir's. Score = pixels outside the shape (x200), then distance to
 * its middle; a piece more than kSlack px outside belongs to nobody. */
enum { kSlack = 16 };
/* Which field each sprite piece was built from (measured Nov 19, both sessions):
 * Nasir's pieces come from the PREVIOUS field (his position and camera word,
 * best fit in every room); every other piece in the sprite list -- doors,
 * effects, the few characters drawn there -- follows THIS field's scroll.
 * Round 6 used Nasir's rule for everything: town and cave pieces reversed
 * direction 5-10x more often (shaking doors, NPCs, priest, enemies). */
/* Round 10 (Nov 20): not only Nasir. The cave monsters' sprites are ALSO
 * built from the previous field's position (measured: type 2 pieces fit the
 * previous field's actor 66% vs 21-34% for this field's). Treated as on time,
 * each step looked like "the pieces stood still while the monster moved", so
 * they went to nobody for that field: freeze a field, catch up, freeze...
 * (blurry, stop-go monsters). Each character now LEARNS its timing: when it
 * moves on one field and not the other, do its pieces move with this field's
 * motion or the previous field's? Nasir is always late. */
static int late(unsigned k) { return k == 0 || s_late[k]; }
static int ref_ax(unsigned k) { return late(k) ? s_spr.ax[k] : s_ref_rec.ax[k]; }
static int ref_ay(unsigned k) { return late(k) ? s_spr.ay[k] : s_ref_rec.ay[k]; }
static int ref_ox(unsigned k) { return late(k) ? s_spr_ox : s_ref_wx; }
static int ref_oy(unsigned k) { return late(k) ? s_spr_oy : s_ref_wy; }
static int ref_live(unsigned k) { return s_rec_to.live[k] && (late(k) ? s_spr.live[k] : s_ref_rec.live[k]); }
static void extent(unsigned k, int *x0, int *x1, int *y0, int *y1) {
  uint8_t t = s_spr.type[k];
  if (k == 0) { *x0 = -27; *x1 = 13; *y0 = -35; *y1 = 0; }
  else if (t == 5) { *x0 = -19; *x1 = 3; *y0 = -49; *y1 = -11; }
  else if (t == 1) { *x0 = -13; *x1 = -4; *y0 = -32; *y1 = -2; }
  else { *x0 = -28; *x1 = 12; *y0 = -49; *y1 = 4; }
}
static int outside(unsigned k, int dx, int dy) {
  int x0, x1, y0, y1;
  extent(k, &x0, &x1, &y0, &y1);
  return (dx < x0 ? x0 - dx : dx > x1 ? dx - x1 : 0) + (dy < y0 ? y0 - dy : dy > y1 ? dy - y1 : 0);
}

/* prefer: the character this list slot belonged to in the previous packet
 * (same picture). It wins ties: in the field before a step nobody has moved
 * yet, so movement cannot tell a walking Nasir from the guard he passes.
 * Movement that disagrees by a pixel (1000 points) still overrides it. */
/* A character that has shown at least 24 pieces and used this palette for
 * fewer than 1 in 20 of them is the wrong owner: +5000 (more than five pixels
 * of disagreeing movement). Only a penalty, so a character that is the only
 * candidate (Nasir flashing after a hit) keeps its pieces. */
static int palette_penalty(unsigned k, int pal) {
  uint32_t total = 0;
  for (int p = 0; p < 8; ++p) total += s_pal_n[k][p];
  return total >= 24 && s_pal_n[k][pal & 7] * 20 < total ? 5000 : 0;
}

static void learn_palettes(const uint8_t *sure) {
  for (unsigned k = 0; k < kActors; ++k) {
    uint8_t want = ref_live(k) ? (uint8_t)(s_spr.type[k] + 1) : 0;
    if (k == 0 && want) want = 1;   /* Nasir is Nasir whatever his slot type says */
    if (want != s_pal_type[k]) {
      memset(s_pal_n[k], 0, sizeof s_pal_n[k]); s_pal_type[k] = want;
      s_late_n[k][0] = s_late_n[k][1] = 0; s_late[k] = 0;
    }
  }
  for (unsigned i = 0; i < 128; ++i) {
    int k = s_owner[i];
    if (!s_pkt.visible[i] || !sure[i] || k < 0) continue;
    uint32_t *n = s_pal_n[k];
    ++n[(s_pkt.tile[i] >> 9) & 7];
    uint32_t total = 0;
    for (int p = 0; p < 8; ++p) total += n[p];
    if (total > 4096) for (int p = 0; p < 8; ++p) n[p] /= 2;
  }
}

static int owner_of(int x, int y, int pal, int have_motion, int odx, int ody, int *by_motion, int prefer) {
  int best = kOwnerNone, best_score = 1 << 30;
  *by_motion = 0;
  for (unsigned k = 0; k < kActors; ++k) {
    if (!ref_live(k)) continue;
    int sx = ref_ax(k) - ref_ox(k), sy0 = ref_ay(k) - ref_oy(k);
    int out = outside(k, x - sx, y - sy0);
    if (out > kSlack) continue;
    int x0, x1, y0, y1;
    extent(k, &x0, &x1, &y0, &y1);
    int score = 200 * out + abs(2 * (x - sx) - x0 - x1) / 2 + abs(2 * (y - sy0) - y0 - y1) / 2;
    if (have_motion) {
      double pdx = late(k) ? (s_spr.ax[k] - s_spr_from.ax[k]) - (s_spr_ox - s_spr_from_ox)
                           : (s_to.ax[k] - s_from.ax[k]) - (s_ref_wx - s_ref_from_wx);
      double pdy = late(k) ? (s_spr.ay[k] - s_spr_from.ay[k]) - (s_spr_oy - s_spr_from_oy)
                           : (s_to.ay[k] - s_from.ay[k]) - (s_ref_wy - s_ref_from_wy);
      score += 1000 * (int)lround(fabs(odx - pdx) + fabs(ody - pdy));
    }
    if ((int)k == prefer) score -= 500;
    score += palette_penalty(k, pal);
    if (score < best_score) { best_score = score; best = (int)k; }
  }
  /* Scenery: a piece that kept its picture and stayed put in the world (a
   * door's sprite parts, a sign) while the character whose shape holds it
   * moved belongs to nobody. Without this choice, a door next to Nasir rode
   * along with him. It only wins on movement evidence (a character that
   * moved the same way scores lower: 300 points vs any character's 1000/px). */
  if (have_motion && best >= 0) {
    int still = 300 + 1000 * (int)lround(fabs(odx + (s_ref_wx - s_ref_from_wx)) + fabs(ody + (s_ref_wy - s_ref_from_wy)));
    if (still < best_score) { *by_motion = 1; return kOwnerNone; }
  }
  if (have_motion && best >= 0 && best_score < 1000) *by_motion = 1;
  return best;
}

static int claimed(int x, int y) {
  for (unsigned k = 0; k < kActors; ++k)
    if (ref_live(k) && !outside(k, x - (ref_ax(k) - ref_ox(k)), y - (ref_ay(k) - ref_oy(k)))) return 1;
  return 0;
}

/* Owner of a wide-view object: the test used before round 6 (this field's
 * record and the packet's scroll; box x -28..28, y -44..12). */
static int obj_owner_of(int x, int y) {
  int best = kOwnerNone, best_score = 1 << 30;
  for (unsigned k = 0; k < kActors; ++k) {
    if (!s_rec_to.live[k]) continue;
    int dx = x - (s_rec_to.ax[k] - s_pkt.wx), dy = y - (s_rec_to.ay[k] - s_pkt.wy);
    if (dx < -28 || dx > 28 || dy < -44 || dy > 12) continue;
    int score = abs(dx + 4) + abs(dy + 12);
    if (score < best_score) { best_score = score; best = (int)k; }
  }
  return best;
}

static int in_box(int k, int x, int y) {
  if (k < 0 || !ref_live((unsigned)k)) return 0;
  return outside((unsigned)k, x - (ref_ax(k) - ref_ox(k)), y - (ref_ay(k) - ref_oy(k))) <= 4;
}

/* Timing evidence from pieces that belonged to character k in the previous
 * packet (same list slot, same picture). Only fields where k moved on exactly
 * one of the last two fields can tell the rules apart. */
static void learn_timing(void) {
  if (!s_have_last || s_last.frame + 1 != s_pkt.frame) return;
  const Record *r0 = record_for(s_pkt.frame), *r1 = record_for(s_pkt.frame - 1), *r2 = record_for(s_pkt.frame - 2);
  if (!r0 || !r1 || !r2) return;
  uint8_t seen[kActors] = {0};
  for (unsigned i = 0; i < 128; ++i) {
    int k = s_last_owner[i];
    if (k <= 0 || seen[k] || !s_pkt.visible[i] || !s_last.visible[i] || s_last.tile[i] != s_pkt.tile[i]) continue;
    if (!r0->live[k] || !r1->live[k] || !r2->live[k]) continue;
    int a0x = r0->ax[k] - r1->ax[k], a0y = r0->ay[k] - r1->ay[k];   /* moved this field */
    int a1x = r1->ax[k] - r2->ax[k], a1y = r1->ay[k] - r2->ay[k];   /* moved last field */
    if ((a0x == a1x && a0y == a1y) || abs(a0x) > kMaxStep || abs(a0y) > kMaxStep ||
        abs(a1x) > kMaxStep || abs(a1y) > kMaxStep) continue;
    int px = (s_pkt.ox[i] + s_pkt.wx) - (s_last.ox[i] + s_last.wx);
    int py = (sy(s_pkt.oy[i]) + s_pkt.wy) - (sy(s_last.oy[i]) + s_last.wy);
    int which = px == a0x && py == a0y ? 0 : px == a1x && py == a1y ? 1 : -1;
    if (which < 0) continue;
    seen[k] = 1;
    uint16_t *n = s_late_n[k];
    if (++n[which] > 64) { n[0] /= 2; n[1] /= 2; }
    /* Switch only on clear evidence (3+ and twice the other), so a stray
     * field never flips a character. */
    if (!s_late[k] && n[1] >= 3 && n[1] >= 2 * n[0]) s_late[k] = 1;
    else if (s_late[k] && n[0] >= 3 && n[0] >= 2 * n[1]) s_late[k] = 0;
  }
}

static void assign_owners(void) {
  learn_timing();
  uint8_t sure[128] = {0};
  int8_t prev[128];
  memcpy(prev, s_last_owner, sizeof prev);
  int motion = s_have_from && s_have_pkt_from;
  for (unsigned i = 0; i < 128; ++i) {
    s_owner[i] = kOwnerNone;
    if (!s_pkt.visible[i]) continue;
    int x = s_pkt.ox[i], y = sy(s_pkt.oy[i]);
    int same = motion && s_pkt_from.visible[i] && s_pkt_from.tile[i] == s_pkt.tile[i];
    /* HUD sprites (the item icon): bottom band, same picture, same screen
     * position, and inside no character's shape: fixed on screen, never
     * moved. (Without the shape test, the feet of Nasir or a guard standing
     * at the bottom of the screen -- the water's edge -- were pinned while
     * the rest of the body moved: a double image.) */
    if (same && y >= kHudTop && s_pkt_from.ox[i] == s_pkt.ox[i] && s_pkt_from.oy[i] == s_pkt.oy[i] &&
        !claimed(x, y)) {
      s_owner[i] = kOwnerScreen; sure[i] = 1; continue;
    }
    int by_motion;
    int prefer = s_have_last && s_last.visible[i] && s_last.tile[i] == s_pkt.tile[i] ? prev[i] : kOwnerNone;
    s_owner[i] = (int8_t)owner_of(x, y, (s_pkt.tile[i] >> 9) & 7, same, same ? x - s_pkt_from.ox[i] : 0,
                                  same ? y - sy(s_pkt_from.oy[i]) : 0, &by_motion, prefer);
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
      if (k != kOwnerNone && palette_penalty((unsigned)k, (s_pkt.tile[i] >> 9) & 7)) k = kOwnerNone;
      if (k != kOwnerNone) { s_owner[i] = (int8_t)k; break; }
    }
  }
  learn_palettes(sure);
  for (unsigned i = 0; i < s_pkt.count; ++i)
    s_obj_owner[i] = (int16_t)obj_owner_of(s_pkt.objx[i], s_pkt.objy[i]);
  s_last = s_pkt; memcpy(s_last_owner, s_owner, sizeof s_last_owner); s_have_last = 1;
}

/* Steady camera (Nov 19). The game's camera follows Nasir's PREVIOUS step,
 * so on a diagonal walk (right, down, right, down) it moves 3 px, stops a step,
 * moves 3 px: at 30 Hz invisible, smoothed it made the whole scene glide-stop-
 * glide -- the guards by the gate and the house doors "jumped" (28 start/stops
 * near the guards in one walk, 1 elsewhere). The stop-go cycle is two steps
 * long, so the VIEW is averaged over the last four fields (12 pictures at
 * 180/s); the world (Nasir, everyone) is not touched. At most 4 px from the
 * game's camera; restarts after a jump. */

static void steady_camera(double t, State *now) {
  if (!s_smooth_camera) return;
  unsigned want = s_pps / 15;   /* pictures in four fields = two game steps */
  if (want > kCamAvg) want = kCamAvg;
  if (want < 2) return;
  if (s_camq_frame == s_pkt.frame && s_camq_pic == s_picture_in_field && s_camq_t == t) {
    now->cx = s_cam_out[0]; now->cy = s_cam_out[1]; return;   /* same picture again */
  }
  if (s_camq_n) {
    const double *last = s_camq[(s_camq_pos + kCamAvg - 1) % kCamAvg];
    if (fabs(now->cx - last[0]) > kMaxStep || fabs(now->cy - last[1]) > kMaxStep) s_camq_n = 0;
  }
  s_camq[s_camq_pos][0] = now->cx; s_camq[s_camq_pos][1] = now->cy;
  s_camq_pos = (s_camq_pos + 1) % kCamAvg;
  if (s_camq_n < want) ++s_camq_n;
  double ax = 0, ay = 0;
  for (unsigned i = 0; i < s_camq_n; ++i) {
    const double *q = s_camq[(s_camq_pos + kCamAvg - 1 - i) % kCamAvg];
    ax += q[0]; ay += q[1];
  }
  ax /= s_camq_n; ay /= s_camq_n;
  if (ax > now->cx + 4) ax = now->cx + 4;
  if (ax < now->cx - 4) ax = now->cx - 4;
  if (ay > now->cy + 4) ay = now->cy + 4;
  if (ay < now->cy - 4) ay = now->cy - 4;
  now->cx = ax; now->cy = ay;
  s_camq_frame = s_pkt.frame; s_camq_pic = s_picture_in_field; s_camq_t = t;
  s_cam_out[0] = ax; s_cam_out[1] = ay;
}

static int floordiv(int a, int b) { int q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }

/* The to-packet redrawn at fraction t, positions in 1/scale pixel (scale 1:
 * whole pixels; scale 2: half pixels, the half recorded in the half_* bits).
 * Returns 0 when the camera jumped. */
static int positions_at_scaled(double t, LagoonSceneMotion *out, int scale) {
  if (!small(s_to.cx - s_from.cx) || !small(s_to.cy - s_from.cy)) return 0;
  State now;
  state_at(t, &now);
  steady_camera(t, &now);
  *out = s_pkt;
  /* The camera in units, and its whole-pixel part for the backgrounds. */
  int ucx = (int)lround(scale * now.cx), ucy = (int)lround(scale * now.cy);
  int cx = floordiv(ucx, scale), cy = floordiv(ucy, scale);
  out->half_bg = (uint8_t)((ucx - scale * cx) | ((ucy - scale * cy) << 1));
  /* The whole packet (background AND sprites, both placed by the game with
   * its scroll) moves to the interpolated camera. The scroll can trail the
   * camera word by a field; moving both together keeps them in agreement. */
  int bx = cx - s_pkt.wx, by = cy - s_pkt.wy;
  if (!small(bx / 2.0) || !small(by / 2.0)) return 0;
  int ubx = ucx - scale * s_pkt.wx, uby = ucy - scale * s_pkt.wy;   /* camera shift in units */
  int usx = ucx - scale * s_spr_ox, usy = ucy - scale * s_spr_oy;   /* Nasir's sprite pieces */
  int urx = ucx - scale * s_ref_wx, ury = ucy - scale * s_ref_wy;   /* the other sprite pieces */
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
    out->half_o[i] = 0;
    if (!s_pkt.visible[i]) continue;
    int k = s_owner[i], mx, my;   /* motion in units */
    if (k == kOwnerScreen) continue;  /* HUD: exactly where the game put it */
    int nasir = k >= 0 && late((unsigned)k);   /* built from the previous field (Nasir, monsters) */
    if (k >= 0) {
      mx = (int)lround(scale * now.ax[k]) - scale * ref_ax(k);
      my = (int)lround(scale * now.ay[k]) - scale * ref_ay(k);
    } else if (s_have_pkt_from && s_pkt_from.visible[i] && s_pkt_from.tile[i] == s_pkt.tile[i]) {
      /* No character nearby, same picture: its own motion in the world. */
      double wx1 = s_pkt.ox[i] + s_ref_wx, wx0 = s_pkt_from.ox[i] + s_ref_from_wx;
      double wy1 = sy(s_pkt.oy[i]) + s_ref_wy, wy0 = sy(s_pkt_from.oy[i]) + s_ref_from_wy;
      if (small(wx1 - wx0) && small(wy1 - wy0)) {
        mx = (int)lround(scale * lerp(wx0, wx1, t)) - scale * (int)wx1;
        my = (int)lround(scale * lerp(wy0, wy1, t)) - scale * (int)wy1;
      } else mx = my = 0;
    } else mx = my = 0;  /* fixed in the world */
    int ux = scale * s_pkt.ox[i] + mx - (nasir ? usx : urx), uy = scale * sy(s_pkt.oy[i]) + my - (nasir ? usy : ury);
    int px = floordiv(ux, scale), py = floordiv(uy, scale);
    out->ox[i] = (int16_t)px;
    out->oy[i] = (uint8_t)(py & 255);
    out->half_o[i] = (uint8_t)((ux - scale * px) | ((uy - scale * py) << 1));
  }
  for (unsigned i = 0; i < s_pkt.count; ++i) {
    /* The wide view's extra characters, doors and props (drawn by the port's
     * own code, not the game's sprite list) are placed from THIS field's
     * record and the packet's scroll, as before round 6. (Round 6b moved them
     * to the sprite list's one-field-late reference: NPCs, cave enemies and
     * doors shook.) */
    int k = s_obj_owner[i], mx = 0, my = 0;
    if (k >= 0) {
      mx = (int)lround(scale * now.ax[k]) - scale * s_rec_to.ax[k];
      my = (int)lround(scale * now.ay[k]) - scale * s_rec_to.ay[k];
    }
    int ux = scale * s_pkt.objx[i] + mx - ubx, uy = scale * s_pkt.objy[i] + my - uby;
    int px = floordiv(ux, scale), py = floordiv(uy, scale);
    out->objx[i] = px; out->objy[i] = py;
    out->half_obj[i] = (uint8_t)((ux - scale * px) | ((uy - scale * py) << 1));
  }
  return 1;
}

static int positions_at(double t, LagoonSceneMotion *out) { return positions_at_scaled(t, out, 1); }

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

/* A late step: in busy places the game itself sometimes needs 3 fields for a
 * step instead of 2 (48 times in a 3.5-minute town walk). Freezing at t = 1
 * for that field was a visible hitch (three pictures at one spot, then a
 * rush). Keep moving at the same speed for at most one extra field instead;
 * the next step starts from wherever the picture really is. */
static double t_max(void) { return 1.0 + 1.0 / (double)s_period; }

static double current_t(double alpha) {
  double tau = (double)(s_pkt.frame - s_step_frame) + picture_phase(alpha);
  double t = tau / (double)s_period, hi = t_max();
  return t < 0 ? 0 : t > hi ? hi : t;
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

/* Self-check of the half-pixel renderer: at the packet's own positions (no
 * half offsets) its double-size picture must equal the verified 1x picture
 * in dst with every pixel doubled. */
static int check2x(const uint8_t *dst, size_t pitch, unsigned width) {
  s_rendered2_valid = 0;
  if (width * 2 > 1024) return 0;
  ++s_renders;
  if (!LagoonSceneRenderer_RenderMotion2x(s_buf2, width, &s_pkt)) return 0;
  for (unsigned y = 0; y < 2 * kHeight; ++y) {
    const uint32_t *a = (const uint32_t *)(dst + (size_t)(y / 2) * pitch), *b = s_buf2 + (size_t)y * 2 * width;
    for (unsigned x = 0; x < 2 * width; ++x)
      if (a[x / 2] != b[x]) {
        if (s_log) fprintf(s_log, "%u check2x-diff room=%x at x=%u y=%u\n", s_pkt.frame, s_pkt.room, x, y);
        return 0;
      }
  }
  return 1;
}

static void log_line(unsigned frame, double alpha, double t, int drawn, const char *why) {
  if (s_log) fprintf(s_log, "%u %.3f %.3f %d %s\n", frame, alpha, t, drawn, why);
}

/* The record and camera the game used for this packet's sprite list. */
static int same_sprite_list(const LagoonSceneMotion *a, const LagoonSceneMotion *b) {
  for (unsigned i = 0; i < 128; ++i) {
    if (a->visible[i] != b->visible[i]) return 0;
    if (a->visible[i] && (a->ox[i] != b->ox[i] || a->oy[i] != b->oy[i] || a->tile[i] != b->tile[i])) return 0;
  }
  return 1;
}

static void set_sprite_ref(const Record *rec, int new_scene) {
  if (!new_scene && s_have_last && same_sprite_list(&s_cur, &s_last)) return;  /* not rebuilt */
  const Record *p = record_for(s_cur.frame - 1);
  if (p) { s_spr = *p; s_spr_ox = p->cx - kCamBaseX; s_spr_oy = p->cy - kCamBaseY; }
  else { s_spr = *rec; s_spr_ox = s_cur.wx; s_spr_oy = s_cur.wy; }
  s_ref_rec = *rec; s_ref_wx = s_cur.wx; s_ref_wy = s_cur.wy;
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
    s_pkt = s_cur; s_rec_to = *rec; state_from_record(&s_to, rec); set_sprite_ref(rec, 1);
    s_have_last = 0;   /* new scene: no owners to keep */
    s_camq_n = 0;      /* and no camera history */
    s_have_to = 1; s_have_from = s_have_pkt_from = 0; s_step_frame = s_cur.frame; s_step_real = 0;
  } else if (rec->tick == s_rec_to.tick) {
    /* Same game step (the in-between field): new pose/graphics, same motion.
     * The camera word does not change here; the scroll may catch up. */
    s_pkt = s_cur; s_rec_to = *rec; set_sprite_ref(rec, 0);
  } else {
    unsigned elapsed = s_cur.frame - s_step_frame;
    double t_now = (double)elapsed / (double)s_period;
    if (t_now > t_max()) t_now = t_max();
    State start;
    if (s_have_from && t_now != 1) state_at(t_now, &start); else start = s_to;
    s_from = start;
    s_pkt_from = s_pkt; s_have_pkt_from = 1;
    s_spr_from = s_spr; s_spr_from_ox = s_spr_ox; s_spr_from_oy = s_spr_oy;
    s_ref_from_wx = s_ref_wx; s_ref_from_wy = s_ref_wy;
    s_pkt = s_cur; s_rec_to = *rec; state_from_record(&s_to, rec); set_sprite_ref(rec, 0);
    /* Fields per step = the AVERAGE of the last 8 steps. Normally exactly 2.
     * When the town slows the game down (steps of 2 and 3 fields mixed, up to
     * 16 slow steps in 2 s), the average gives one steady, slower speed; the
     * most common value flipped between 2 and 3 and the speed jumped. */
    if (s_step_real && elapsed >= 1 && elapsed <= 4) s_period_hist[s_period_pos++ % 8] = (uint8_t)elapsed;
    s_step_real = 1;
    {
      unsigned sum = 0, n = 0;
      for (unsigned i = 0; i < 8; ++i) if (s_period_hist[i]) { sum += s_period_hist[i]; ++n; }
      s_period = n ? (double)sum / (double)n : 2;
    }
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
    if (s_checked > 0 && LagoonSmooth_HalfPixels((int)width, kHeight)) {
      s_half_ok = check2x(dst, pitch, width);
      if (!s_half_ok) ++s_checks_failed;
    }
  }
  /* Measurement for the wide view's own objects (NPCs, enemies, doors drawn by
   * the port): their positions per field, compared offline with motion.log. */
  if (s_log && s_pkt.count) {
    fprintf(s_log, "%u objs wx=%d wy=%d n=%u", s_pkt.frame, s_pkt.wx, s_pkt.wy, s_pkt.count);
    for (unsigned i = 0; i < s_pkt.count && i < 48; ++i)
      fprintf(s_log, " %d,%d", (int)s_pkt.objx[i], (int)s_pkt.objy[i]);
    fputc('\n', s_log);
  }
  if (s_log && s_have_from)
    fprintf(s_log, "%u step-info room=%x tick=%x cam=%d,%d scroll-lag=%d,%d period=%u\n", s_pkt.frame,
            s_pkt.room, s_rec_to.tick, s_rec_to.cx, s_rec_to.cy, s_rec_to.cx - s_pkt.wx,
            s_rec_to.cy - s_pkt.wy, (unsigned)lround(s_period));
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
  int doubled = width == 2 * s_base_width && height == 2 * kHeight;
  if (s_pps <= 60 || !dst || !s_base_ok || (!doubled && (height != kHeight || width != s_base_width))) {
    log_line(s_pkt.frame, alpha, 1, 0, "hold");
    return 0;
  }
  ++s_picture_in_field;
  double t = current_t(alpha);
  if (doubled && !s_half_ok) {
    /* The half-pixel self-check failed: whole-pixel picture, doubled. */
    if (!positions_at(t, &s_pos)) { log_line(s_pkt.frame, alpha, t, 0, "jump"); return 0; }
    ++s_renders;
    if (!LagoonSceneRenderer_RenderMotion(s_buf, s_base_width, &s_pos)) return 0;
    s_rendered_valid = 0;
    for (unsigned y = 0; y < height; ++y) {
      uint32_t *row = (uint32_t *)(dst + (size_t)y * pitch);
      const uint32_t *src = s_buf + (size_t)(y / 2) * s_base_width;
      for (unsigned x = 0; x < width; ++x) row[x] = src[x / 2];
    }
    ++s_drawn;
    *shift_x = s_pos.wx - s_pkt.wx; *shift_y = s_pos.wy - s_pkt.wy;
    log_line(s_pkt.frame, alpha, t, 1, "inbetween-doubled");
    return 1;
  }
  if (doubled) {
    /* Half-pixel picture: every 3 px step is six 0.5 px moves at 180. */
    if (!positions_at_scaled(t, &s_pos, 2)) { log_line(s_pkt.frame, alpha, t, 0, "jump"); return 0; }
    if (!s_rendered2_valid || s_rendered2_width != s_base_width || !same_draw(&s_rendered2, &s_pos) ||
        s_rendered2.half_bg != s_pos.half_bg || memcmp(s_rendered2.half_o, s_pos.half_o, sizeof s_pos.half_o) ||
        memcmp(s_rendered2.half_obj, s_pos.half_obj, s_pos.count)) {
      ++s_renders;
      if (!LagoonSceneRenderer_RenderMotion2x(s_buf2, s_base_width, &s_pos)) {
        s_rendered2_valid = 0;
        log_line(s_pkt.frame, alpha, t, 0, "render2x-refused");
        return 0;
      }
      s_rendered2 = s_pos; s_rendered2_width = s_base_width; s_rendered2_valid = 1;
    }
    for (unsigned y = 0; y < height; ++y)
      memcpy(dst + (size_t)y * pitch, s_buf2 + (size_t)y * width, (size_t)width * 4);
    ++s_drawn;
    *shift_x = s_pos.wx - s_pkt.wx; *shift_y = s_pos.wy - s_pkt.wy;
    log_line(s_pkt.frame, alpha, t, 1, "inbetween-half-pixel");
    return 1;
  }
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
