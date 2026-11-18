#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lagoon_scene_renderer.h"
#include "lagoon_smooth.h"
extern LagoonSceneMotion g_pkt;
int LagoonSceneRenderer_RenderMotion(uint32_t*,unsigned,const LagoonSceneMotion*);
static uint32_t base[256*224], pic[512*448]; static uint8_t ram[0x20000];
static void w16(unsigned a,int v){ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
int main(void){
  setenv("LAGOON_PICTURES","180",1); LagoonSmooth_AfterConfig(NULL);
  int cam=1000,nas=1128,guard=1150,tick=0,scroll=1000; unsigned serial=0;
  int last=-1, moves[4]={0}, bad=0, drawn=0;
  for(unsigned f=1; f<=70; f++){
    if(f%2==0){ tick++; if(f>=6&&f<=60){cam+=3;nas+=3;} }
    scroll = (f%2==0 && f>=6 && f<=60) ? cam-3 : cam;
    ram[0x408]=(uint8_t)tick; w16(0x480,cam); w16(0x482,500);
    ram[0x500]=0xc0; w16(0x502,nas); w16(0x504,600); ram[0x540]=0xc0; w16(0x542,guard); w16(0x544,600);
    LagoonSmooth_AfterFrame(f,ram);
    memset(&g_pkt,0,sizeof g_pkt); g_pkt.generation=1; g_pkt.serial=++serial; g_pkt.frame=f;
    g_pkt.wx=scroll; g_pkt.wy=500; g_pkt.native_x=scroll; g_pkt.native_y=500;
    g_pkt.visible[0]=g_pkt.visible[1]=1;
    g_pkt.ox[0]=(int16_t)(nas-scroll-8); g_pkt.oy[0]=84; g_pkt.tile[0]=1;
    g_pkt.ox[1]=(int16_t)(guard-scroll-8); g_pkt.oy[1]=84; g_pkt.tile[1]=2;
    /* base picture (second core): composed, verified, not shown */
    LagoonSceneRenderer_RenderMotion(base,256,&g_pkt);
    LagoonSmooth_BeginPicture(-1); LagoonSmooth_SetCalm(1);
    LagoonSmooth_Draw((uint8_t*)base,1024,256,224,-1);
    for(int k=0;k<3;k++){
      int sx,sy;
      if(!LagoonSmooth_DrawInbetween((uint8_t*)pic,512*4,512,448,k/3.0,&sx,&sy)) continue;
      drawn++;
      int bg=2*(int)pic[0]+(pic[1]!=pic[0]); int n=2*(int)(int32_t)pic[4]+(pic[5]!=pic[4]); int g=2*(int)(int32_t)pic[6]+(pic[7]!=pic[6]);
      if(f>10&&f<58){
        if(n != 2*(nas-cam-8)) {bad++; printf("f%u: Nasir off his spot (%d half px)\n",f,n-2*(nas-cam-8));}
        if(g+bg != 2*(guard-8)) {bad++; printf("f%u: guard off the ground (%d)\n",f,g+bg-2*(guard-8));}
        if(last>=0){int m=bg-last; if(m>=0&&m<4)moves[m]++; else {bad++;printf("f%u: camera moved %d half px\n",f,m);} }
        last=bg;
      }
    }
  }
  printf("pictures drawn %d; camera move per picture in half pixels: 0 x%d, 1 x%d, 2 x%d\n",drawn,moves[0],moves[1],moves[2]);
  printf("%s\n", bad||moves[0]||moves[2]?"FAIL":"PASS: the camera moves exactly one half pixel on every picture, Nasir fixed, guard on the ground");
  return 0;}
