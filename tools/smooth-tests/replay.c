#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lagoon_scene_renderer.h"
#include "lagoon_smooth.h"
LagoonSceneMotion g_pkt; static LagoonSceneMotion g_got; static int g_have;
int LagoonSceneRenderer_Motion(LagoonSceneMotion *m){*m=g_pkt;return 1;}
int LagoonSceneRenderer_RenderMotion(uint32_t *out,unsigned width,const LagoonSceneMotion *p){
  if(p->serial!=g_pkt.serial)return 0;
  memset(out,0,width*224*4);out[0]=(uint32_t)p->wx;out[1]=(uint32_t)p->wy;
  for(int i=0;i<128;i++){out[2+i]=(uint32_t)(int32_t)p->ox[i];out[130+i]=p->oy[i];}
  return 1;}
int LagoonSceneRenderer_RenderMotion2x(uint32_t *out,unsigned width,const LagoonSceneMotion *p){
  static uint32_t one[512*224];
  if(p->serial!=g_pkt.serial)return 0;
  LagoonSceneRenderer_RenderMotion(one,width,p);
  for(unsigned y=0;y<448;y++)for(unsigned x=0;x<2*width;x++)out[y*2*width+x]=one[(y/2)*width+x/2];
  g_got=*p; g_have=1; return 1;}
void LagoonSceneRenderer_SetMotionCache(int on){(void)on;}
static uint32_t base[256*224], pic[512*448]; static uint8_t ram[0x20000];
static void w16(unsigned a,int v){ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static int sy8(int y){return y>=240?y-256:y;}
#define NA 32
int main(int argc,char**argv){
  setenv("LAGOON_PICTURES","180",1); LagoonSmooth_AfterConfig(NULL);
  FILE *in=fopen(argv[1],"r"); char *line=NULL; size_t cap=0;
  int pcx=0,pcy=0,pax[NA]={0},pay[NA]={0},plive[NA]={0}; static int ptype[NA],pptype[NA];
  /* per actor: last world position (half px) per picture */
  int lastw[NA][2]; int haslast[NA]={0}; unsigned lastf[NA]={0};
  long split[4]={0},jerk[4]={0},pics[4]={0},still[4]={0}; static long camh[4][8]; static int sl_w[128][2], sl_d[128][2], sl_ok[128]; static uint16_t sl_t[128]; static long rev[4], revn[4], revw[3][2]; static long stat_n[4], stat_bad[4]; static int pv_wx,pv_wy; static LagoonSceneMotion pv; static int pv_ok; static int lbx=-1,lby=-1,lmov=0; static long freeze=0;
  int prevroom=-1;
  while(getline(&line,&cap,in)>0){
    char *p=line+2; int n;
    unsigned f; int tick,cx,cy,wx,wy,room,na;
    sscanf(p,"%u %d %d %d %d %d %d %d%n",&f,&tick,&cx,&cy,&wx,&wy,&room,&na,&n); p+=n;
    memset(ram+0x500,0,0x800);
    int ax[NA]={0},ay[NA]={0},live[NA]={0};
    for(int i=0;i<na;i++){int s,x,y,ty;sscanf(p,"%d %d %d %d%n",&s,&x,&y,&ty,&n);p+=n; if(s<NA){ram[0x500+s*0x40]=0xc0;ram[0x518+s*0x40]=(uint8_t)ty;ptype[s]=ty;w16(0x502+s*0x40,x);w16(0x504+s*0x40,y);ax[s]=x;ay[s]=y;live[s]=1;}}
    ram[0x408]=(uint8_t)tick; w16(0x480,cx); w16(0x482,cy);
    LagoonSmooth_AfterFrame(f,ram);
    int np; sscanf(p,"%d%n",&np,&n); p+=n;
    memset(&g_pkt,0,sizeof g_pkt); g_pkt.generation=1; g_pkt.serial=f; g_pkt.frame=f; g_pkt.room=(unsigned)room;
    g_pkt.wx=wx; g_pkt.wy=wy; g_pkt.native_x=wx; g_pkt.native_y=wy;
    for(int i=0;i<np;i++){int s,x,y,t;sscanf(p,"%d %d %d %d%n",&s,&x,&y,&t,&n);p+=n;
      if(s<128){g_pkt.visible[s]=1;g_pkt.ox[s]=(int16_t)x;g_pkt.oy[s]=(uint8_t)y;g_pkt.tile[s]=(uint16_t)t;}}
    /* truth: which actor built each piece (previous field's actor - camera) */
    int own[128]; for(int i=0;i<128;i++){own[i]=-1;
      if(!g_pkt.visible[i])continue; int best=1<<30;
      for(int k=0;k<NA;k++){ if(!plive[k])continue; int dx=g_pkt.ox[i]-(pax[k]-pcx), dy=sy8(g_pkt.oy[i])-(pay[k]-pcy);
        int x0,x1,y0,y1; if(k==0){x0=-27;x1=13;y0=-35;y1=0;} else if(pptype[k]==5){x0=-19;x1=3;y0=-49;y1=-11;} else if(pptype[k]==1){x0=-13;x1=-4;y0=-32;y1=-2;} else {x0=-28;x1=12;y0=-49;y1=4;}
        int out=(dx<x0?x0-dx:dx>x1?dx-x1:0)+(dy<y0?y0-dy:dy>y1?dy-y1:0); if(out>0)continue;
        int sc=abs(2*dx-x0-x1)+abs(2*dy-y0-y1); if(sc<best){best=sc;own[i]=k;}}}
    if(room!=prevroom){memset(haslast,0,sizeof haslast);prevroom=room;}
    LagoonSceneRenderer_RenderMotion(base,256,&g_pkt);
    LagoonSmooth_BeginPicture(-1); LagoonSmooth_SetCalm(1);
    LagoonSmooth_Draw((uint8_t*)base,1024,256,224,-1);
    for(int k=0;k<3;k++){
      int sx,sy; g_have=0;
      if(!LagoonSmooth_DrawInbetween((uint8_t*)pic,512*4,512,448,k/3.0,&sx,&sy)){memset(haslast,0,sizeof haslast);continue;}
      
      LagoonSceneMotion *q=&g_got; int r=room&3; pics[r]++;
      int bgx=2*q->wx+(q->half_bg&1), bgy=2*q->wy+((q->half_bg>>1)&1);
      if(lbx!=-1){int m=abs(bgx-lbx)+abs(bgy-lby); if(m<8)camh[r][m]++; else camh[r][7]++; if(m==0&&lmov)freeze++; lmov=m!=0;} lbx=bgx;lby=bgy;
      for(int i=0;i<128;i++){
        if(!pv_ok||!g_pkt.visible[i]||!pv.visible[i]||pv.tile[i]!=g_pkt.tile[i])continue;
        if(g_pkt.ox[i]+g_pkt.wx!=pv.ox[i]+pv.wx||sy8(g_pkt.oy[i])+g_pkt.wy!=sy8(pv.oy[i])+pv.wy)continue;  /* game keeps it still in the world */
        if(g_pkt.wx==pv.wx&&g_pkt.wy==pv.wy)continue;  /* only while the view moves */
        int ux=2*q->ox[i]+(q->half_o[i]&1)+bgx, uy=2*sy8(q->oy[i])+((q->half_o[i]>>1)&1)+bgy;
        stat_n[r]++; if(ux!=2*(g_pkt.ox[i]+g_pkt.wx)||uy!=2*(sy8(g_pkt.oy[i])+g_pkt.wy)) stat_bad[r]++;
      }
      for(int i=0;i<128;i++){
        if(!g_pkt.visible[i]){sl_ok[i]=0;continue;}
        int ux=2*q->ox[i]+(q->half_o[i]&1)+bgx, uy=2*sy8(q->oy[i])+((q->half_o[i]>>1)&1)+bgy;
        if(sl_ok[i]&&sl_t[i]==g_pkt.tile[i]){
          int dx=ux-sl_w[i][0], dy=uy-sl_w[i][1];
          if(abs(dx)<16&&abs(dy)<16){ revn[r]++;
            int wh=own[i]==0?0:own[i]>0?1:2; revw[wh][1]++;
            if((dx&&sl_d[i][0]&&(dx>0)!=(sl_d[i][0]>0))||(dy&&sl_d[i][1]&&(dy>0)!=(sl_d[i][1]>0))) {rev[r]++;revw[wh][0]++;}
            if(dx)sl_d[i][0]=dx; if(dy)sl_d[i][1]=dy; }
        } else {sl_d[i][0]=sl_d[i][1]=0;}
        sl_w[i][0]=ux;sl_w[i][1]=uy;sl_t[i]=g_pkt.tile[i];sl_ok[i]=1;
      }
      for(int a=0;a<NA;a++){
        int have=0,wxs=0,wys=0,bad=0;
        for(int i=0;i<128;i++){ if(own[i]!=a)continue;
          int ux=2*q->ox[i]+(q->half_o[i]&1), uy=2*sy8(q->oy[i])+((q->half_o[i]>>1)&1);
          int dx=g_pkt.ox[i]-(pax[a]-pcx), dy=sy8(g_pkt.oy[i])-(pay[a]-pcy);
          int Wx=ux+bgx-2*dx, Wy=uy+bgy-2*dy;
          if(!have){wxs=Wx;wys=Wy;have=1;} else if(Wx!=wxs||Wy!=wys) bad=1; }
        if(!have)continue;
        if(bad){split[r]++; if(split[r]<=5)printf("split  room %d f%u pic%d actor %d\n",room,f,k,a);}
        if(haslast[a]){ int mx=wxs-lastw[a][0], my=wys-lastw[a][1];
          if(mx<-1||mx>1||my<-1||my>1){jerk[r]++; if(jerk[r]<=8)printf("jerk   room %d f%u pic%d actor %d moved %d,%d half px\n",room,f,k,a,mx,my);} }
        lastw[a][0]=wxs; lastw[a][1]=wys; haslast[a]=1; lastf[a]=f;
      }
    }
    pv=g_pkt; pv_ok=1;
    pcx=cx;pcy=cy+0; memcpy(pax,ax,sizeof ax);memcpy(pay,ay,sizeof ay);memcpy(plive,live,sizeof live); memcpy(pptype,ptype,sizeof ptype);
  }
  for(int r=0;r<3;r++)printf("room %d camera moves per picture (half px) 0:%ld 1:%ld 2:%ld 3:%ld 4+:%ld\n",r,camh[r][0],camh[r][1],camh[r][2],camh[r][3],camh[r][4]+camh[r][5]+camh[r][6]+camh[r][7]);
  printf("camera stops while moving: %ld\n",freeze);
  for(int r=0;r<4;r++)printf("room %d: scenery pieces drawn off their spot %ld of %ld\n",r,stat_bad[r],stat_n[r]);
  printf("reversals: Nasir %ld/%ld  other characters %ld/%ld  no character %ld/%ld\n",revw[0][0],revw[0][1],revw[1][0],revw[1][1],revw[2][0],revw[2][1]);
  for(int r=0;r<4;r++)printf("room %d: sprite pieces reversing direction %ld of %ld moves\n",r,rev[r],revn[r]);
  for(int r=0;r<4;r++)printf("room %d: pictures %ld, pieces split from their character %ld, jumps > 0.5 px %ld\n",r,pics[r],split[r],jerk[r]);
  return 0;}

/* Engine CPU-speed hooks (beam_frame_driver.c) for offline tests. */
#include <stdint.h>
static unsigned s_test_cpu_speed = 100;
void snes_beam_frame_driver_set_cpu_speed(unsigned p) { s_test_cpu_speed = p; }
unsigned snes_beam_frame_driver_cpu_speed(void) { return s_test_cpu_speed; }
void snes_beam_frame_driver_cpu_speed_stats(uint64_t *a, uint64_t *b, uint64_t *c) {
  if (a) *a = 0; if (b) *b = 0; if (c) *c = 0;
}
