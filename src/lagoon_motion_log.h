#pragma once
/*
 * lagoon_motion_log.h -- per-frame measuring logs for the smooth-motion work
 * (gabbycat4 fork). Off unless an environment variable names a file:
 *
 *   LAGOON_MOTION_LOG=motion.log  one line per simulated field: game tick,
 *                                 room, brightness, BG mode, HDMA, camera,
 *                                 layer scrolls (and per-line bands when they
 *                                 differ down the screen), player, actors,
 *                                 palette entries that changed.
 *   LAGOON_OAM_LOG=oam.log        one line per field: every on-screen sprite.
 *
 * Read-only: nothing here writes guest state, timing or the picture. Pair
 * with the engine's SNESRECOMP_FRAME_TIMING=timing.csv for pacing.
 */
#include <stdint.h>

struct Ppu;

/* Every raster line (from LagoonWide_ObserveRaster). */
void LagoonMotionLog_ObserveLine(const struct Ppu *ppu, unsigned line);
/* Once per simulated field, after it completed (after_run_frame). */
void LagoonMotionLog_EndFrame(unsigned frame, const struct Ppu *ppu,
                              const uint8_t *ram, uint8_t hdmaen);
