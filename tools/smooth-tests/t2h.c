#include "../../src/lagoon_scene_renderer.c"
#include <stdlib.h>
#include <stdio.h>
static uint32_t one[W*H], two[2*512*2*H];
int main(void){
  init_tables(); int fails=0;
  for(int trial=0;trial<30;trial++){
    ScenePacket *s=&s_packets[s_complete]; memset(s,0,sizeof *s); srand(100+trial);
    s->valid=1; s->serial=200+trial; s->generation=1;
    for(unsigned i=0;i<32768;i++)s->vram[i]=(uint16_t)rand();
    for(unsigned i=0x3c00;i<0x3c00+1024;i++)s->vram[i]=0; for(unsigned i=0x6000;i<0x6000+8;i++)s->vram[i]=0;  /* no HUD: it never moves */
    for(unsigned i=0;i<256;i++)s->pal[i]=(uint16_t)(rand()&0x7fff);
    for(unsigned l=0;l<2;l++){for(unsigned i=0;i<16384;i++)s->maps[l][i]=(uint8_t)rand();for(unsigned i=0;i<1024;i++)s->defs[l][i]=(uint16_t)rand();}
    for(unsigned i=0;i<256;i++)s->oam[i]=(uint16_t)rand();
    for(unsigned i=0;i<32;i++)s->high[i]=(uint8_t)rand();
    for(unsigned i=0;i<128;i++){s->policy.allow[i]=rand()%3!=0;s->policy.x[i]=(int16_t)(rand()%240+8);}
    s->count=2; for(unsigned i=0;i<2;i++){SceneObject*a=&s->objects[i];a->x=60+90*i;a->y=40;a->width=20;a->height=20;a->slot=(uint8_t)(i+1);a->order=i;for(int p=0;p<400;p++)a->pixels[p]=(uint16_t)((rand()&1)?0x8000|(rand()&0x7fff):0);}
    s->wx=300; s->wy=300; s->native_x=300; s->native_y=300; s->world_width=2048; s->world_height=2048; s->map_fill=1;
    unsigned width=320; LagoonSceneMotion m; s_view_serial=s->serial; s_mode=2;
    LagoonSceneRenderer_Motion(&m); LagoonSceneRenderer_RenderMotion(one,width,&m);
    LagoonSceneMotion h=m; h.half_bg=1;                     /* camera +0.5 px in x */
    for(unsigned i=0;i<128;i++){h.ox[i]=(int16_t)(m.ox[i]-1);h.half_o[i]=1;}   /* world-still: screen -0.5 px */
    for(unsigned i=0;i<2;i++){h.objx[i]=m.objx[i]-1;h.half_obj[i]=1;}
    LagoonSceneRenderer_RenderMotion2x(two,width,&h);
    unsigned bad=0;
    for(unsigned y=0;y<2*H;y++)for(unsigned x=0;x+1<2*width;x++) if(two[y*2*width+x]!=one[(y/2)*width+(x+1)/2])bad++;
    if(bad){fails++;unsigned cnt=0,minx=9999,maxx=0;for(unsigned y=0;y<2*H;y++)for(unsigned x=0;x+1<2*width;x++) if(two[y*2*width+x]!=one[(y/2)*width+(x+1)/2]){if(x<minx)minx=x;if(x>maxx)maxx=x;if(cnt++<3)printf("  at 2x x=%u y=%u\n",x,y);} printf("trial %d: %u pixels differ, x range %u-%u (1x cols %u-%u), left=%d\n",trial,bad,minx,maxx,minx/2,maxx/2,(int)(width-256)/2);}
  }
  printf("%s\n",fails?"FAIL":"PASS: a half-pixel camera move shifts ground, sprites and objects together by exactly one half-pixel column (30 random scenes)");
  return fails;
}
