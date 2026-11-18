#pragma once
/*
 * lagoon_smooth.h -- in-between pictures for Lagoon (gabbycat4 fork).
 *
 * Lagoon's game logic moves the camera and every character once every two
 * fields (30 Hz), in 2-3 pixel hops. With PicturesPerSecond above 60 the host
 * presents at that rate and this module redraws the latest verified scene
 * packet with the camera and sprites placed part of the way between the last
 * two game steps. Positions are whole pixels at the game's resolution, so the
 * pixel art stays crisp (and GPU Enhanced pixels still work).
 *
 * Safety: drawn only from the scene renderer's frozen packet, only when that
 * packet's own redraw matched the original picture exactly, and only after a
 * second check that the room-map redraw at the real positions matches too.
 * Anything else shows the original picture.
 *
 * Settings: config.ini [Lagoon] PicturesPerSecond = 60 | 120 | 180 | 240
 * (60 = off, the default). LAGOON_PICTURES=<n> overrides for one run.
 * LAGOON_SMOOTH_LOG=file writes a per-picture log for measuring.
 */
#include <stddef.h>
#include <stdint.h>

void LagoonSmooth_AfterConfig(const char *config_path);
void LagoonSmooth_Reset(void);
unsigned LagoonSmooth_PicturesPerSecond(void);
/* after_run_frame: record the game tick, camera and actor table ($0408,
 * $0480/$0482, $0500..) for the field that just ran. */
void LagoonSmooth_AfterFrame(unsigned frame, const uint8_t *ram);
/* SnesDesktopHostGame.presentation_hz */
double LagoonSmooth_PresentationHz(double display_refresh);
/* After the scene renderer replaced dst with its verified picture. May
 * overwrite dst with an in-between picture. */
void LagoonSmooth_Draw(uint8_t *dst, size_t pitch, unsigned width, unsigned height, double alpha);
