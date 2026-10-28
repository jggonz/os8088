/* gblossy - what a Game Boy picture is made of, and what a LOSSY renderer
 * would lose: the instrument behind docs/reports/REALDMG-LOSSY-2026-10-22.md.
 *
 * Built and run by tools/gblossy.py on tools/gbprof.py's patched Peanut-GB
 * (the same pinned commit and hooks; the hooks are no-ops here).  Peanut-GB
 * runs the game; at every line it has drawn, this composes the same line
 * AGAIN from VRAM, OAM and the registers as they stand at that line - its
 * own renderer, checked against Peanut-GB's pixels on every line it draws -
 * once faithfully and once per lossy variant, and counts:
 *   - which layer each pixel came from (background colour 0 / other, window
 *     colour 0 / other, sprite), and its shade;
 *   - how many pixels each variant gets WRONG against the faithful line:
 *       nobg    background and window off (sprites over white)
 *       nobgw   background off, window kept (a status bar survives)
 *       nowin   window off
 *       nospr   sprites off
 *       opaque  a sprite's colour 0 drawn, not transparent
 *       noprio  a sprite's behind-the-background bit ignored
 *       bw      two shades: shade 0-1 white, 2-3 black
 *       bw1     two shades: only shade 0 white
 *   - how many frames each variant leaves EXACTLY right.
 * --shots writes one PPM per chosen frame with the variants side by side.
 * Statistics start at frame 800, after the scripted title-screen presses -
 * gbprof's script, so the frames are the ones its reports measured. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define GBPROF_READ(g,a) ((void)0)
#define GBPROF_WRITE(g,a,v) ((void)0)
#define GBPROF_PRE(g,pc) ((void)0)
#define GBPROF_POST(g,op,pc) ((void)0)
#define GBPROF_TICK(g,c) ((void)0)
#define GBPROF_INT(g) ((void)0)
#define GBPROF_CYC(g,c) ((void)0)
#define GBPROF_HALTSTEP(g,c) ((void)0)
#define GBPROF_STOP(g) ((void)0)
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#include "pgb_prof.h"

static uint8_t *rom, cram[0x20000];
static uint8_t rr(struct gb_s *g, const uint_fast32_t a){(void)g;return rom[a];}
static uint8_t cr(struct gb_s *g, const uint_fast32_t a){(void)g;return cram[a&0x1FFFF];}
static void cw(struct gb_s *g, const uint_fast32_t a, const uint8_t v){(void)g;cram[a&0x1FFFF]=v;}
static void err(struct gb_s *g, const enum gb_error_e e, const uint16_t a){(void)g;fprintf(stderr,"gb error %d at %04x\n",e,a);exit(1);}

enum {V_FULL, V_NOBG, V_NOBGW, V_NOWIN, V_NOSPR, V_OPAQUE, V_NOPRIO, V_BW, V_BW1, V_N};
static const char *vn[V_N]={"full","nobg","nobgw","nowin","nospr","opaque","noprio","bw","bw1"};
enum {L_BG0, L_BGN, L_WIN0, L_WINN, L_SPR, L_OFF, L_N};
static const char *ln_[L_N]={"bg col0","bg other","win col0","win other","sprite","lcd off"};

static uint8_t fb[V_N][144][160];               /* shades */
static int nfr, measure_from=800, wline;
static unsigned long long layer[L_N], shade[4], wrong[V_N], exact[V_N], frames, mism;
static unsigned long long spr_lines, spr_n, uni_lines, lines;
static int fr_wrong[V_N];
/* the playfield's scroll (line 100's), frame to frame: how often a frame
 * scrolls, and how often it would with the camera snapped to 8 pixels */
static int sx, sy, psx=-1, psy=-1; static unsigned long long scx_ch, scy_ch, scx8_ch, scy8_ch, any_ch, any8_ch;

/* one line, variant v, into out[160]; lay[] gets the faithful layer */
static void compose(struct gb_s *gb, int v, uint8_t ly, int wl, uint8_t *out, uint8_t *lay){
  uint8_t lcdc=gb->hram_io[IO_LCDC], bgp=gb->hram_io[IO_BGP];
  uint8_t col[160], src[160];           /* colour index, source layer */
  if(!(lcdc&0x80)){ memset(out,0,160); if(lay) memset(lay,L_OFF,160); return; }
  int bg=(lcdc&1) && v!=V_NOBG && v!=V_NOBGW;
  int win=(lcdc&1) && (lcdc&0x20) && ly>=gb->display.WY && gb->hram_io[IO_WX]<=166 && v!=V_NOBG && v!=V_NOWIN;
  for(int x=0;x<160;x++){ col[x]=0; src[x]=L_BG0; }
  if(bg){
    uint8_t y=ly+gb->hram_io[IO_SCY]; uint16_t map=(lcdc&8)?0x1C00:0x1800;
    for(int x=0;x<160;x++){ uint8_t bx=x+gb->hram_io[IO_SCX];
      uint8_t idx=gb->vram[map+(y>>3)*32+(bx>>3)];
      uint16_t t=(lcdc&0x10)? idx*16 : 0x1000+(int8_t)idx*16;
      t+=(y&7)*2; int px=7-(bx&7);
      col[x]=((gb->vram[t]>>px)&1)|(((gb->vram[t+1]>>px)&1)<<1); src[x]=col[x]?L_BGN:L_BG0; } }
  if(win){
    int wx=gb->hram_io[IO_WX]-7; uint16_t map=(lcdc&0x40)?0x1C00:0x1800;
    for(int x=wx<0?0:wx;x<160;x++){ int wxx=x-wx;
      uint8_t idx=gb->vram[map+(wl>>3)*32+(wxx>>3)];
      uint16_t t=(lcdc&0x10)? idx*16 : 0x1000+(int8_t)idx*16;
      t+=(wl&7)*2; int px=7-(wxx&7);
      col[x]=((gb->vram[t]>>px)&1)|(((gb->vram[t+1]>>px)&1)<<1); src[x]=col[x]?L_WINN:L_WIN0; } }
  for(int x=0;x<160;x++) out[x]=(bgp>>(2*col[x]))&3;
  if((lcdc&2) && v!=V_NOSPR){
    int h=(lcdc&4)?16:8, n=0, list[10];
    for(int i=0;i<40&&n<10;i++){ int sy=gb->oam[i*4]-16; if(ly>=sy && ly<sy+h) list[n++]=i; }
    /* draw lowest priority first: higher X, then later OAM index, loses */
    int ord[10]; for(int i=0;i<n;i++) ord[i]=list[i];
    for(int i=1;i<n;i++){ int k=ord[i], j=i;
      while(j>0 && gb->oam[ord[j-1]*4+1] > gb->oam[k*4+1]){ ord[j]=ord[j-1]; j--; } ord[j]=k; }
    for(int i=n-1;i>=0;i--){ uint8_t *o=&gb->oam[ord[i]*4]; int sx=o[1]-8, row=ly-(o[0]-16);
      uint8_t at=o[3], tl=o[2]; if(h==16) tl&=0xFE; if(at&0x40) row=h-1-row;
      uint16_t t=tl*16+row*2; uint8_t pal=gb->hram_io[(at&0x10)?IO_OBP1:IO_OBP0];
      for(int p=0;p<8;p++){ int x=sx+p; if(x<0||x>=160) continue;
        int bit=(at&0x20)?p:7-p; uint8_t c=((gb->vram[t]>>bit)&1)|(((gb->vram[t+1]>>bit)&1)<<1);
        if(!c && v!=V_OPAQUE) continue;
        if((at&0x80) && v!=V_NOPRIO && col[x]) continue;
        out[x]=(pal>>(2*c))&3; src[x]=L_SPR; } } }
  if(v==V_BW) for(int x=0;x<160;x++) out[x]=out[x]>=2?3:0;
  if(v==V_BW1) for(int x=0;x<160;x++) out[x]=out[x]>=1?3:0;
  if(lay) memcpy(lay,src,160);
}

static void line(struct gb_s *g, const uint8_t *px, const uint_fast8_t ly){
  if(ly==0) wline=0;
  int wl=wline, wdrawn=(g->hram_io[IO_LCDC]&0x21)==0x21 && ly>=g->display.WY && g->hram_io[IO_WX]<=166;
  uint8_t lay[160];
  for(int v=0;v<V_N;v++) compose(g,v,ly,wl,fb[v][ly],v==V_FULL?lay:NULL);
  if(wdrawn) wline++;
  if(ly==100){ sx=g->hram_io[IO_SCX]; sy=g->hram_io[IO_SCY]; }
  if(nfr<measure_from) return;
  lines++;
  for(int x=0;x<160;x++){ if((px[x]&3)!=fb[V_FULL][ly][x]) mism++; layer[lay[x]]++; shade[fb[V_FULL][ly][x]]++; }
  for(int v=1;v<V_N;v++){ int w=0; for(int x=0;x<160;x++) w+=fb[v][ly][x]!=fb[V_FULL][ly][x]; wrong[v]+=w; fr_wrong[v]+=w; }
  int s=0; for(int x=0;x<160;x++) s+=lay[x]==L_SPR; if(s) spr_lines++; spr_n+=s;
  int u=1; for(int x=1;x<160;x++) if(lay[x]==L_SPR||lay[x]>=L_WIN0||fb[V_FULL][ly][x]!=fb[V_FULL][ly][0]) {u=0;break;}
  uni_lines+=u;
}

static int flap;                        /* the bare harness's JOY_FLAP|JOY_NO3RD */
static uint8_t joy_for(int f){             /* gbprof's platformer script, or */
  uint8_t j=0xFF;                          /*   with --flap the bare harness's */
  if(flap){                                /*   Kirby script (rdghost.asm) */
    j&=~JOYPAD_RIGHT;
    if((f>=150&&f<156)||(f>=400&&f<406)) j&=~JOYPAD_START;
    if((f%12)<6) j&=~JOYPAD_A;
    j&=~JOYPAD_UP;
    return j; }
  if((f>=150&&f<156)||(f>=400&&f<406)||(f>=700&&f<706)) j&=~JOYPAD_START;
  if(f<800) return j;
  j&=~JOYPAD_RIGHT; if((f%50)<14) j&=~JOYPAD_A; if((f%400)>370) j&=~JOYPAD_B;
  return j;
}

static void shot(const char *pre, int f){
  static const uint8_t g[4]={0xE0,0xA8,0x60,0x18};
  static const int show[]={V_FULL,V_OPAQUE,V_BW,V_NOBGW,V_NOBG};
  int nv=sizeof show/sizeof *show, W=160*nv+4*(nv-1);
  char b[512]; snprintf(b,sizeof b,"%s_%05d.ppm",pre,f); FILE*o=fopen(b,"wb");
  fprintf(o,"P6 %d 144 255\n",W);
  for(int y=0;y<144;y++) for(int i=0;i<nv;i++){
    for(int x=0;x<160;x++){ uint8_t c=g[fb[show[i]][y][x]]; fputc(c,o); fputc(c,o); fputc(c+8,o); }
    if(i<nv-1) for(int x=0;x<4;x++){ fputc(200,o); fputc(40,o); fputc(40,o); } }
  fclose(o);
}

int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"usage: gblossy rom frames [shotprefix f1 f2 ...]\n");return 2;}
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
  rom=malloc(n); if(fread(rom,1,n,f)!=(size_t)n) return 1; fclose(f);
  static struct gb_s gb;
  if(gb_init(&gb,rr,cr,cw,err,NULL)!=GB_INIT_NO_ERROR){fprintf(stderr,"init fail\n");return 1;}
  gb_init_lcd(&gb,line);
  int nrun=atoi(argv[2]);
  if(getenv("GBLOSSY_FLAP")) flap=1;
  for(int i=0;i<nrun;i++){
    gb.direct.joypad=joy_for(i);
    memset(fr_wrong,0,sizeof fr_wrong);
    gb_run_frame(&gb);
    if(nfr>=measure_from){ frames++; for(int v=1;v<V_N;v++) exact[v]+=fr_wrong[v]==0;
      if(psx>=0){ int a=sx!=psx, b=sy!=psy, a8=(sx&~7)!=(psx&~7), b8=(sy&~7)!=(psy&~7);
        scx_ch+=a; scy_ch+=b; scx8_ch+=a8; scy8_ch+=b8; any_ch+=a||b; any8_ch+=a8||b8; } }
    psx=sx; psy=sy;
    for(int k=4;k<argc;k++) if(atoi(argv[k])==i) shot(argv[3],i);
    nfr++;
  }
  unsigned long long P=lines*160;
  printf("rom %s: %llu frames measured from frame %d, %llu lines\n",argv[1],frames,measure_from,lines);
  printf("check: %llu of %llu pixels differ from Peanut-GB's (%.4f%%)\n",mism,P,100.0*mism/P);
  printf("layers:"); for(int l=0;l<L_N;l++) printf(" %s %.1f%%",ln_[l],100.0*layer[l]/P); printf("\n");
  printf("shades: 0 %.1f%%  1 %.1f%%  2 %.1f%%  3 %.1f%%\n",100.0*shade[0]/P,100.0*shade[1]/P,100.0*shade[2]/P,100.0*shade[3]/P);
  printf("lines with a sprite pixel %.1f%%; sprite pixels a line %.1f; lines one shade of background %.1f%%\n",
    100.0*spr_lines/lines,(double)spr_n/lines,100.0*uni_lines/lines);
  printf("frames scrolled: SCX %.1f%%  SCY %.1f%%  either %.1f%%; snapped to 8: SCX %.1f%%  SCY %.1f%%  either %.1f%%\n",
    100.0*scx_ch/frames,100.0*scy_ch/frames,100.0*any_ch/frames,100.0*scx8_ch/frames,100.0*scy8_ch/frames,100.0*any8_ch/frames);
  printf("%-7s %10s %12s\n","variant","px wrong","frames exact");
  for(int v=1;v<V_N;v++) printf("%-7s %9.2f%% %11.1f%%\n",vn[v],100.0*wrong[v]/P,100.0*exact[v]/frames);
  return 0;
}
