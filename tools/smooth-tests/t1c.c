#include "../../src/lagoon_scene_renderer.c"
#include <stdlib.h>
#include <stdio.h>
static uint32_t ref[W*H];
int main(void){
  init_tables(); int fails=0;
  for(int trial=0;trial<40;trial++){
    ScenePacket *s=&s_packets[s_complete]; memset(s,0,sizeof *s); srand(300+trial);
    s->valid=1; s->serial=500+trial; s->generation=1;
    for(unsigned i=0;i<32768;i++)s->vram[i]=(uint16_t)rand();
    for(unsigned i=0;i<256;i++)s->pal[i]=(uint16_t)(rand()&0x7fff);
    for(unsigned l=0;l<2;l++){for(unsigned i=0;i<16384;i++)s->maps[l][i]=(uint8_t)rand();for(unsigned i=0;i<1024;i++)s->defs[l][i]=(uint16_t)rand();}
    for(unsigned i=0;i<256;i++)s->oam[i]=(uint16_t)rand();
    for(unsigned i=0;i<128;i++){s->policy.allow[i]=rand()%3!=0;s->policy.x[i]=(int16_t)(rand()%300-40);}
    s->wx=rand()%600; s->wy=rand()%600; s->native_x=rand()%1024; s->native_y=rand()%1024;
    s->world_width=1024; s->world_height=1024; s->finite_clip=trial%2; s->parallax=trial%5==0; s->map_fill=trial%3!=0;
    unsigned width=256+2*(rand()%100);
    s_motion_cache_on=0; s_bgc.valid=0; memset(&s_background,0,sizeof s_background); render_into(s,width,s_pixels,0); memcpy(ref,s_pixels,(size_t)width*H*4);
    s_motion_cache_on=1; s_bgc.valid=0; memset(&s_background,0,sizeof s_background); render_into(s,width,s_pixels,0);
    unsigned bad=0; for(unsigned i=0;i<width*H;i++) if(ref[i]!=s_pixels[i]) bad++;
    if(bad){fails++; printf("trial %d (width %u): %u pixels differ\n",trial,width,bad);}
  }
  printf("%s\n",fails?"FAIL":"PASS: their verified render with the shared cache == without it (40 random scenes)");
  return fails;}
