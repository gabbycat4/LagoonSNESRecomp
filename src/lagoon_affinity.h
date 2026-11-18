#pragma once
/*
 * lagoon_affinity.h -- give the second core's guest thread a physical core of
 * its own and keep the main thread on the other cores sharing its L3 cache.
 * Call from the thread itself (SnesDesktopHostGame.pin_thread): the main
 * thread before the worker starts, the worker at its top. Linux only.
 * LAGOON_PIN=0 turns it off.
 */
enum { LAGOON_PIN_MAIN = 0, LAGOON_PIN_GUEST = 1 };
void LagoonAffinity_Pin(int role);
