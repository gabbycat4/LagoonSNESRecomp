/* Round 12: a half-pixel picture drawn by 2, 3 or 4 cores (bands) must be
 * identical to the one drawn by one core, for random scenes, random half
 * offsets, with and without the background cache, parallax rooms included.
 * Also times one picture per thread count (rough: this machine, not yours).
 * Build: gcc -std=gnu11 -O2 -w -pthread -I src -I snesrecomp/runner/src -I snesrecomp/runner/src/snes
 *        tools/smooth-tests/tbands.c -o /tmp/tbands -Wl,--unresolved-symbols=ignore-all */
#include "../../src/lagoon_scene_renderer.c"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
static uint32_t ref[2*512*2*H], got[2*512*2*H];
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){
  srand(12); init_tables();
  int fails=0; double cost[BAND_MAX+1]={0}; unsigned timed=0;
  for(int trial=0;trial<60;trial++){
    ScenePacket *s=&s_packets[s_complete]; memset(s,0,sizeof *s);
    s->valid=1; s->serial=300+trial; s->generation=1;
    for(unsigned i=0;i<32768;i++)s->vram[i]=(uint16_t)rand();
    for(unsigned i=0;i<256;i++)s->pal[i]=(uint16_t)(rand()&0x7fff);
    for(unsigned l=0;l<2;l++){for(unsigned i=0;i<16384;i++)s->maps[l][i]=(uint8_t)rand();for(unsigned i=0;i<1024;i++)s->defs[l][i]=(uint16_t)rand();}
    for(unsigned i=0;i<256;i++)s->oam[i]=(uint16_t)rand();
    for(unsigned i=0;i<32;i++)s->high[i]=(uint8_t)rand();
    for(unsigned i=0;i<128;i++){s->policy.allow[i]=rand()%3!=0;s->policy.x[i]=(int16_t)(rand()%300-40);s->policy.clip_left[i]=rand()&1;}
    s->count=rand()%5;
    for(unsigned i=0;i<s->count;i++){SceneObject*a=&s->objects[i];a->x=rand()%300-20;a->y=rand()%200;a->width=8+rand()%24;a->height=8+rand()%24;a->priority=rand()&3;a->slot=rand()&127;a->order=rand();for(int p=0;p<a->width*a->height;p++)a->pixels[p]=(uint16_t)((rand()&1)?0x8000|(rand()&0x7fff):0);}
    s->wx=rand()%600; s->wy=rand()%600; s->native_x=rand()%1024; s->native_y=rand()%1024;
    s->world_width=1024; s->world_height=1024; s->finite_clip=trial%2; s->parallax=trial%5==0; s->bg2_x=rand()%512; s->bg2_y=rand()%512;
    s->map_fill=1;
    unsigned width=256+2*(rand()%72);
    s_motion_cache_on=trial%3!=0;
    LagoonSceneMotion m; memset(&m,0,sizeof m);
    s_view_serial=s->serial; s_mode=2;
    if(!LagoonSceneRenderer_Motion(&m)){printf("motion refused\n");return 1;}
    m.half_bg=(uint8_t)(rand()&3);
    for(unsigned i=0;i<128;i++)m.half_o[i]=(uint8_t)(rand()&3);
    for(unsigned i=0;i<m.count;i++)m.half_obj[i]=(uint8_t)(rand()&3);
    LagoonSceneRenderer_SetDrawThreads(1);
    double t0=now(); if(!LagoonSceneRenderer_RenderMotion2x(ref,width,&m)){printf("render refused\n");return 1;} cost[1]+=now()-t0;
    for(unsigned n=2;n<=BAND_MAX;n++){
      memset(got,0x5a,sizeof got);
      LagoonSceneRenderer_SetDrawThreads(n);
      t0=now(); LagoonSceneRenderer_RenderMotion2x(got,width,&m); cost[n]+=now()-t0;
      unsigned bad=0;
      for(unsigned y=0;y<2*H;y++)for(unsigned x=0;x<2*width;x++)if(got[y*2*width+x]!=ref[y*2*width+x])bad++;
      if(bad){fails++;printf("trial %d width %u threads %u: %u pixels differ\\n",trial,width,n,bad);}
    }
    timed++;
  }
  for(unsigned n=1;n<=BAND_MAX;n++)printf("threads %u: %.2f ms per picture (average of %u random scenes)\n",n,1000*cost[n]/timed,timed);
  printf("%s (%d differing pictures; band pictures drawn %llu)\n",fails?"FAIL":"PASS: band pictures with 2/3/4 cores == one core, pixel for pixel",fails,LagoonSceneRenderer_BandPictures());
  return fails!=0;
}
