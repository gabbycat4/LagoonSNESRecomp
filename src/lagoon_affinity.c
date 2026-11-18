/*
 * lagoon_affinity.c -- keep the second core's guest thread and the main
 * thread on separate physical cores that share one L3 cache. Ported from
 * gabbycat4/nesrecomp runner/cyc/cyc_affinity.c (proven on Contra / NG1 / MM2).
 *
 * The plan, made once from the CPUs this process may use (so `taskset` still
 * narrows it): group the CPUs by L3 cache (a Ryzen CCX), and by physical core
 * inside a group (SMT twins share a core). In the group with the most cores
 * (at least 2), the guest thread gets the first CPU of the second core; the
 * main thread gets every other CPU of the group except that core's twin.
 * Other threads (audio, the GPU driver) are left alone.
 *
 * Why: left to the scheduler, the two threads shared a core or sat in
 * different CCXs; on Lagoon's first second-core run the guest took 7.3 ms a
 * field instead of ~3.5 and the raster capture 2.1 ms instead of ~1.
 * Linux only; elsewhere it does nothing. LAGOON_PIN=0 turns it off.
 */
#define _GNU_SOURCE
#include "lagoon_affinity.h"

#if defined(__linux__)
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

enum { MAX_CPUS = 256 };

static bool s_planned, s_ok;
static cpu_set_t s_main_set, s_guest_set;

static int read_int(const char *fmt, int cpu) {
  char path[160];
  snprintf(path, sizeof path, fmt, cpu);
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  int v = -1;
  if (fscanf(f, "%d", &v) != 1) v = -1;
  fclose(f);
  return v;
}

static void list_set(const cpu_set_t *set, char *out, size_t cap) {
  size_t n = 0;
  out[0] = 0;
  for (int c = 0; c < MAX_CPUS && n + 8 < cap; ++c)
    if (CPU_ISSET(c, set)) n += (size_t)snprintf(out + n, cap - n, "%s%d", n ? "," : "", c);
}

static void plan(void) {
  s_planned = true;
  const char *env = getenv("LAGOON_PIN");
  if (env && env[0] == '0') {
    fprintf(stderr, "[lagoon threads] LAGOON_PIN=0: threads not pinned\n");
    return;
  }
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  if (sched_getaffinity(0, sizeof allowed, &allowed) != 0) return;

  int cpu[MAX_CPUS], l3[MAX_CPUS], core[MAX_CPUS], n = 0;
  for (int c = 0; c < MAX_CPUS; ++c) {
    if (!CPU_ISSET(c, &allowed)) continue;
    int pkg = read_int("/sys/devices/system/cpu/cpu%d/topology/physical_package_id", c);
    int cid = read_int("/sys/devices/system/cpu/cpu%d/topology/core_id", c);
    int cache = read_int("/sys/devices/system/cpu/cpu%d/cache/index3/id", c);
    cpu[n] = c;
    core[n] = cid < 0 ? c : (pkg < 0 ? 0 : pkg) * 65536 + cid;
    l3[n] = cache < 0 ? (pkg < 0 ? 0 : pkg) : (pkg < 0 ? 0 : pkg) * 65536 + cache;
    ++n;
  }

  /* the L3 group with the most physical cores */
  int best_l3 = -1, best_cores = 0;
  for (int i = 0; i < n; ++i) {
    bool seen = false;
    for (int j = 0; j < i; ++j) seen |= l3[j] == l3[i];
    if (seen) continue;
    int cores = 0;
    for (int j = 0; j < n; ++j) {
      if (l3[j] != l3[i]) continue;
      bool dup = false;
      for (int k = 0; k < j; ++k) dup |= l3[k] == l3[i] && core[k] == core[j];
      cores += !dup;
    }
    if (cores > best_cores) { best_cores = cores; best_l3 = l3[i]; }
  }
  if (best_cores < 2) {
    fprintf(stderr, "[lagoon threads] fewer than 2 cores available: threads not pinned\n");
    return;
  }

  /* the guest thread: the lowest-numbered CPU of the group's second core */
  int first_core = -1, guest_core = -1, guest_cpu = -1;
  for (int i = 0; i < n; ++i) {
    if (l3[i] != best_l3) continue;
    if (first_core < 0) { first_core = core[i]; continue; }
    if (core[i] != first_core) { guest_core = core[i]; guest_cpu = cpu[i]; break; }
  }
  CPU_ZERO(&s_guest_set);
  CPU_ZERO(&s_main_set);
  CPU_SET(guest_cpu, &s_guest_set);
  for (int i = 0; i < n; ++i)
    if (l3[i] == best_l3 && core[i] != guest_core) CPU_SET(cpu[i], &s_main_set);
  s_ok = true;

  char a[512], b[512];
  list_set(&s_main_set, a, sizeof a);
  list_set(&s_guest_set, b, sizeof b);
  fprintf(stderr, "[lagoon threads] guest on CPU %s, main on CPUs %s (same L3, %d cores; LAGOON_PIN=0 turns this off)\n",
          b, a, best_cores);
}

void LagoonAffinity_Pin(int role) {
  if (!s_planned) plan();
  if (!s_ok) return;
  const cpu_set_t *set = role == LAGOON_PIN_GUEST ? &s_guest_set : &s_main_set;
  if (sched_setaffinity(0, sizeof *set, set) != 0)   /* 0: the calling thread */
    fprintf(stderr, "[lagoon threads] could not pin the %s thread\n",
            role == LAGOON_PIN_GUEST ? "guest" : "main");
}

#else
void LagoonAffinity_Pin(int role) { (void)role; }
#endif
