Offline tests for the smooth-motion code (no window, no ROM). From the project folder, with the
engine patch applied (the build does that):

  # half-pixel renderer vs their 1x renderer (random scenes)
  for t in t2x t2h t2v t1c; do gcc -std=gnu11 -O1 -w -I src -I snesrecomp/runner/src -I snesrecomp/runner/src/snes tools/smooth-tests/$t.c -o /tmp/$t -Wl,--unresolved-symbols=ignore-all && /tmp/$t | tail -1; done
  # timing/positions (stub renderer): camera moves one half pixel per picture at 180
  gcc -std=c11 -w -I src tools/smooth-tests/t7.c tools/smooth-tests/stub.c src/lagoon_smooth.c -lm -o /tmp/t7 && /tmp/t7 | tail -1

t2x: 2x with no half offsets == 1x doubled.  t2h / t2v: a half-pixel camera move shifts ground,
sprites and objects together by one half-pixel column / row.  t7: second-core flow at 180.

  # speed of the half-pixel drawing (per game frame)
  gcc -std=gnu11 -O3 -w -I src -I snesrecomp/runner/src -I snesrecomp/runner/src/snes tools/smooth-tests/tperf.c -o /tmp/tperf -Wl,--unresolved-symbols=ignore-all && /tmp/tperf | head -1
t1c: their verified render with the shared background cache == without it (40 random scenes).

t8 (Nov 19): sprites as Lagoon really builds them (sprite list = PREVIOUS field's actors and
camera word). Camera still / following with trailing scroll / following on time: Nasir moves
one half pixel per picture, the guard stays put. Build:
  gcc -std=gnu11 -w -I src tools/smooth-tests/t8.c tools/smooth-tests/stub.c src/lagoon_smooth.c -lm -o /tmp/t8 && /tmp/t8
t7 is RETIRED: it modelled sprites placed with the current scroll, which the game does not do.

replay (Nov 19): replays a REAL play session through lagoon_smooth.c. Record with
LAGOON_MOTION_LOG=motion.log LAGOON_OAM_LOG=oam.log, then see make_replay.py's header.
Nov 19 session (13,203 fields), jumps > 0.5 px / pieces split, old -> now:
  town 7411/3275 -> 303/1122, church 751/194 -> 37/144, guards' area 1331/570 -> 92/422.
