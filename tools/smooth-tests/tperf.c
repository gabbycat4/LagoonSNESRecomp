#include "../../src/lagoon_scene_renderer.c"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
static uint32_t one[W*H], two[2*512*2*H];
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){
  init_tables(); ScenePacket *s=&s_packets[s_complete]; memset(s,0,sizeof *s); srand(5);
  s->valid=1; s->serial=9; s->generation=1;
  for(unsigned i=0;i<32768;i++)s->vram[i]=(uint16_t)rand();
  for(unsigned i=0;i<256;i++)s->pal[i]=(uint16_t)(rand()&0x7fff);
  for(unsigned l=0;l<2;l++){for(unsigned i=0;i<16384;i++)s->maps[l][i]=(uint8_t)rand();for(unsigned i=0;i<1024;i++)s->defs[l][i]=(uint16_t)rand();}
  for(unsigned i=0;i<256;i++)s->oam[i]=(uint16_t)rand();
  for(unsigned i=0;i<128;i++){s->policy.allow[i]=i<40;s->policy.x[i]=(int16_t)(rand()%300-20);}  /* ~40 sprites, like town */
  s->wx=300; s->wy=300; s->native_x=300; s->native_y=300; s->world_width=2048; s->world_height=2048; s->map_fill=1;
  unsigned width=398; LagoonSceneMotion m; s_view_serial=s->serial; s_mode=2; LagoonSceneRenderer_Motion(&m);
  m.half_bg=3; for(int i=0;i<128;i++)m.half_o[i]=(uint8_t)(i&3);
  int N=200; double t0=now(); for(int i=0;i<N;i++)LagoonSceneRenderer_RenderMotion(one,width,&m); double t1=now();
  double tb=0,tr=0; for(int i=0;i<N;i++){ s->serial=1000+i; s_view_serial=s->serial; LagoonSceneRenderer_Motion(&m); m.half_bg=3; for(int j=0;j<128;j++)m.half_o[j]=(uint8_t)(j&3);
    double a=now(); LagoonSceneRenderer_RenderMotion2x(two,width,&m); double b=now(); m.wx+=1; LagoonSceneRenderer_RenderMotion2x(two,width,&m); m.wx+=1; LagoonSceneRenderer_RenderMotion2x(two,width,&m); double c=now(); tb+=b-a; tr+=c-b; }
  double t2=now(); printf("per game frame: first picture (builds the background) %.3f ms, next pictures %.3f ms each\n",tb*1000/N,tr*1000/N/2);
  printf("1x render %.3f ms, 2x render %.3f ms (this machine; ratio %.2f)\n",(t1-t0)*1000/N,(t2-t1)*1000/N,(t2-t1)/(t1-t0));
  return 0;}
