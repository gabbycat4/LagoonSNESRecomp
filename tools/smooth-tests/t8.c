/* t8: sprites as Lagoon really builds them -- the sprite list of field f uses
 * the PREVIOUS field's actor positions and camera word (measured from
 * oam.log/motion.log: 66 of 67 moving fields); backgrounds use this field's
 * scroll, which may trail the camera word by a field.
 * Three cases: camera still while Nasir walks (church / map edge), camera
 * following with a trailing scroll, camera following without.
 * Expected on every half-pixel picture: Nasir moves exactly one half pixel in
 * the world (3 px per 2 fields = 6 pictures), the guard never moves in the world. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lagoon_scene_renderer.h"
#include "lagoon_smooth.h"
extern LagoonSceneMotion g_pkt;
int LagoonSceneRenderer_RenderMotion(uint32_t*,unsigned,const LagoonSceneMotion*);
static uint32_t base[256*224], pic[512*448]; static uint8_t ram[0x20000];
static void w16(unsigned a,int v){ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static int run(const char *name,int camera_follows,int scroll_trails){
  LagoonSmooth_Reset();
  int cam=1000,nas=1128,guard=1150,tick=0; unsigned serial=0;
  int pcam=cam,pnas=nas;              /* previous field's values (sprite list) */
  int lastn=-99999, bad=0, drawn=0, checked=0;
  static unsigned frame=0;
  for(int i=1;i<=70;i++){
    unsigned f=++frame;
    pcam=cam; pnas=nas;
    int stepped=0;
    if(i%2==0){ tick++; if(i>=6&&i<=60){ nas+=3; if(camera_follows)cam+=3; stepped=1; } }
    int scroll = (stepped && scroll_trails) ? cam-(camera_follows?3:0) : cam;
    ram[0x408]=(uint8_t)tick; w16(0x480,cam); w16(0x482,500);
    ram[0x500]=0xc0; w16(0x502,nas); w16(0x504,600); ram[0x540]=0xc0; w16(0x542,guard); w16(0x544,600);
    LagoonSmooth_AfterFrame(f,ram);
    memset(&g_pkt,0,sizeof g_pkt); g_pkt.generation=1; g_pkt.serial=f; g_pkt.frame=f;
    /* packet origin: camera word - (0,-1) when the scroll is not trailing */
    g_pkt.wx=scroll; g_pkt.wy=501; g_pkt.native_x=scroll; g_pkt.native_y=501;
    g_pkt.visible[0]=g_pkt.visible[1]=1;
    g_pkt.ox[0]=(int16_t)(pnas-pcam-8); g_pkt.oy[0]=84; g_pkt.tile[0]=1;
    g_pkt.ox[1]=(int16_t)(guard-pcam-8); g_pkt.oy[1]=84; g_pkt.tile[1]=2;
    LagoonSceneRenderer_RenderMotion(base,256,&g_pkt);
    LagoonSmooth_BeginPicture(-1); LagoonSmooth_SetCalm(1);
    LagoonSmooth_Draw((uint8_t*)base,1024,256,224,-1);
    for(int k=0;k<3;k++){
      int sx,sy;
      if(!LagoonSmooth_DrawInbetween((uint8_t*)pic,512*4,512,448,k/3.0,&sx,&sy)) continue;
      drawn++;
      int bg=2*(int)pic[0]+(pic[1]!=pic[0]);
      int n=2*(int)(int32_t)pic[4]+(pic[5]!=pic[4]), g=2*(int)(int32_t)pic[6]+(pic[7]!=pic[6]);
      if(i>12&&i<58){
        checked++;
        int nw=n+bg, gw=g+bg;               /* positions in the world, half pixels */
        if(gw!=2*(guard-8)){bad++; if(bad<6)printf("  %s f%d: guard moved in the world (%d)\n",name,i,gw-2*(guard-8));}
        if(lastn!=-99999 && nw-lastn!=1){bad++; if(bad<6)printf("  %s f%d: Nasir moved %d half px\n",name,i,nw-lastn);}
        lastn=nw;
      }
    }
  }
  printf("%-34s pictures %d checked %d: %s\n",name,drawn,checked,bad?"FAIL":"PASS");
  return bad;
}
int main(void){
  setenv("LAGOON_PICTURES","180",1); LagoonSmooth_AfterConfig(NULL);
  int bad=run("camera still (church, map edge)",0,0)
         +run("camera follows, scroll trails",1,1)
         +run("camera follows, scroll on time",1,0);
  printf("%s\n",bad?"FAIL":"PASS: Nasir moves one half pixel per picture, the guard stays put, in all three cases");
  return 0;}
