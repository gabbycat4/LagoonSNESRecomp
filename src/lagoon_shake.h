#pragma once
/* gabbycat4 fork: optional "screen shake off" (Comfort tab, [Lagoon] ScreenShake).
 * The game's boss quake ($01:DDAD) copies 42 words from the ROM table at
 * $01:DE11 (-4,+8,-8,+8 ... +1) into the vertical scroll/sprite offsets $90/$92.
 * Off = that table reads as zeros: the quake still starts, plays its sound and
 * runs its 42 steps; only the picture stays still. No game logic changes. */
#include <stddef.h>
#include <stdint.h>
/* Before the machine copies the image: remember the authentic table, zero it if off. */
int LagoonShake_OnRom(uint8_t *rom, size_t size);
/* Any time (main thread): on = original quake, off = still picture. Applies to
 * the running ROM copy at once when it is known. */
void LagoonShake_Set(int on);
int LagoonShake_On(void);
/* 1 when the quake table in this image is the zeroed one (shake off). */
int LagoonShake_TableZeroed(const uint8_t *rom, size_t size);
