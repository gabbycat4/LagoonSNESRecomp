#include "../../src/lagoon_scene_renderer.c"
#include <stdlib.h>
#include <stdio.h>
static uint32_t one[W*H], two[2*512*2*H];
int main(void){
  srand(11); init_tables();
  int fails=0;
  for(int trial=0;trial<40;trial++){
    ScenePacket *s=&s_packets[s_complete]; memset(s,0,sizeof *s);
    s->valid=1; s->serial=100+trial; s->generation=1;
    for(unsigned i=0;i<32768;i++)s->vram[i]=(uint16_t)rand();
    for(unsigned i=0;i<256;i++)s->pal[i]=(uint16_t)(rand()&0x7fff);
    for(unsigned l=0;l<2;l++){for(unsigned i=0;i<16384;i++)s->maps[l][i]=(uint8_t)rand();for(unsigned i=0;i<1024;i++)s->defs[l][i]=(uint16_t)rand();}
    for(unsigned i=0;i<256;i++)s->oam[i]=(uint16_t)rand();
    for(unsigned i=0;i<32;i++)s->high[i]=(uint8_t)rand();
    for(unsigned i=0;i<128;i++){s->policy.allow[i]=rand()%3!=0;s->policy.x[i]=(int16_t)(rand()%300-40);s->policy.clip_left[i]=rand()&1;}
    s->count=rand()%4;
    for(unsigned i=0;i<s->count;i++){SceneObject*a=&s->objects[i];a->x=rand()%300-20;a->y=rand()%200;a->width=8+rand()%24;a->height=8+rand()%24;a->priority=rand()&3;a->slot=rand()&127;a->order=rand();for(int p=0;p<a->width*a->height;p++)a->pixels[p]=(uint16_t)((rand()&1)?0x8000|(rand()&0x7fff):0);}
    s->wx=rand()%600; s->wy=rand()%600; s->native_x=rand()%1024; s->native_y=rand()%1024;
    s->world_width=1024; s->world_height=1024; s->finite_clip=trial%2; s->parallax=trial%5==0; s->bg2_x=rand()%512; s->bg2_y=rand()%512;
    s->map_fill=1;
    unsigned width=256+2*(rand()%72);
    LagoonSceneMotion m; memset(&m,0,sizeof m);
    s_view_serial=s->serial; s_mode=2;
    if(!LagoonSceneRenderer_Motion(&m)){printf("motion refused\n");return 1;}
    LagoonSceneRenderer_RenderMotion(one,width,&m);
    LagoonSceneRenderer_RenderMotion2x(two,width,&m);
    unsigned bad=0;
    for(unsigned y=0;y<2*H;y++)for(unsigned x=0;x<2*width;x++)
      if(two[y*2*width+x]!=one[(y/2)*width+x/2])bad++;
    if(bad){fails++;printf("trial %d width %u: %u pixels differ from the doubled 1x picture\n",trial,width,bad);}
  }
  printf("%s (%d of 40 random scenes differ)\n",fails?"FAIL":"PASS: 2x renderer with no half offsets == doubled 1x renderer",fails);
  return fails!=0;
}
