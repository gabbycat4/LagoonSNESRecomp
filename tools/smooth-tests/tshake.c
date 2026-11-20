/* Screen shake off: with the real ROM, the boss quake table ($01:DE11, 42 words)
 * is recognised, zeroed when off, restored byte for byte when on, also live on
 * the running copy (g_rom), and refused on any other data.
 * Build: gcc -std=gnu11 -O2 -I src tools/smooth-tests/tshake.c src/lagoon_shake.c -o /tmp/tshake
 * Run from the project folder (reads lagoon.sfc). */
#include "lagoon_shake.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const uint8_t *g_rom;
int main(void){
  FILE *f=fopen("lagoon.sfc","rb");assert(f);
  static uint8_t rom[0x100000],orig[0x100000],live[0x100000];
  assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);memcpy(orig,rom,sizeof rom);
  const int16_t *t=(const int16_t *)(orig+0xDE11);
  assert(t[0]==-4&&t[1]==8&&t[2]==-8&&t[41]==1);
  /* on (default): recognised, image unchanged */
  LagoonShake_Set(1);assert(LagoonShake_OnRom(rom,sizeof rom)&&!memcmp(rom,orig,sizeof rom)&&!LagoonShake_TableZeroed(rom,sizeof rom));
  /* off at load: exactly the 84 table bytes become zero, nothing else */
  LagoonShake_Set(0);memcpy(rom,orig,sizeof rom);assert(LagoonShake_OnRom(rom,sizeof rom)&&LagoonShake_TableZeroed(rom,sizeof rom));
  unsigned changed=0;for(unsigned i=0;i<sizeof rom;i++)if(rom[i]!=orig[i]){changed++;assert(i>=0xDE11&&i<0xDE11+84);}
  printf("off: %u bytes changed, all inside $01:DE11..DE64\n",changed);
  /* live toggles on the running copy */
  memcpy(live,rom,sizeof live);g_rom=live;
  LagoonShake_Set(1);assert(!memcmp(live,orig,sizeof live));
  LagoonShake_Set(0);assert(LagoonShake_TableZeroed(live,sizeof live));
  LagoonShake_Set(1);assert(!memcmp(live,orig,sizeof live));
  /* a different table is never touched */
  g_rom=NULL;memcpy(rom,orig,sizeof rom);rom[0xDE11]^=1;memcpy(live,rom,sizeof rom);LagoonShake_Set(0);
  assert(!LagoonShake_OnRom(rom,sizeof rom)&&!memcmp(rom,live,sizeof rom));
  puts("PASS screen shake: real ROM table recognised, off = 84 zero bytes only, on = original restored, live toggle, other data refused");
  return 0;
}
