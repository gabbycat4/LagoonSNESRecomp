Offline tests for the smooth-motion code (no window, no ROM). From the project folder, with the
engine patch applied (the build does that):

  # half-pixel renderer vs their 1x renderer (random scenes)
  for t in t2x t2h t2v; do gcc -std=gnu11 -O1 -w -I src -I snesrecomp/runner/src -I snesrecomp/runner/src/snes tools/smooth-tests/$t.c -o /tmp/$t -Wl,--unresolved-symbols=ignore-all && /tmp/$t | tail -1; done
  # timing/positions (stub renderer): camera moves one half pixel per picture at 180
  gcc -std=c11 -w -I src tools/smooth-tests/t7.c tools/smooth-tests/stub.c src/lagoon_smooth.c -lm -o /tmp/t7 && /tmp/t7 | tail -1

t2x: 2x with no half offsets == 1x doubled.  t2h / t2v: a half-pixel camera move shifts ground,
sprites and objects together by one half-pixel column / row.  t7: second-core flow at 180.
