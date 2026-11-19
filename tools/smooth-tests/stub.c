#include <string.h>
#include <stdio.h>
#include "lagoon_scene_renderer.h"
LagoonSceneMotion g_pkt; int g_ok=1;
int LagoonSceneRenderer_Motion(LagoonSceneMotion *m){if(!g_ok)return 0;*m=g_pkt;return 1;}
/* "Picture" = the positions written as pixels: [0]=wx [1]=wy [2+i]=ox[i] [130+i]=oy[i] */
int LagoonSceneRenderer_RenderMotion(uint32_t *out,unsigned width,const LagoonSceneMotion *p){
  if(p->serial!=g_pkt.serial)return 0;
  memset(out,0,width*224*4);out[0]=(uint32_t)p->wx;out[1]=(uint32_t)p->wy;
  for(int i=0;i<128;i++){out[2+i]=(uint32_t)(int32_t)p->ox[i];out[130+i]=p->oy[i];}
  return 1;}
/* 2x stub, consistent with the 1x stub doubled when no half offsets are set:
 * value v at 1x index j appears at 2x indices 2j and 2j+1 of rows 0 and 1;
 * a half offset marks the odd copy with 0x40000000. */
int LagoonSceneRenderer_RenderMotion2x(uint32_t *out,unsigned width,const LagoonSceneMotion *p){
  if(p->serial!=g_pkt.serial)return 0;
  memset(out,0,width*2*448*4);
  for(int r=0;r<2;r++){uint32_t *row=out+r*2*width;
    row[0]=row[1]=(uint32_t)p->wx; if(p->half_bg&1)row[1]|=0x40000000u;
    row[2]=row[3]=(uint32_t)p->wy;
    for(int i=0;i<128;i++){row[4+2*i]=row[5+2*i]=(uint32_t)(int32_t)p->ox[i]; if(p->half_o[i]&1)row[5+2*i]^=0x40000000u;
      row[260+2*i]=row[261+2*i]=p->oy[i];}
  }
  return 1;}
void LagoonSceneRenderer_SetMotionCache(int on){(void)on;}
