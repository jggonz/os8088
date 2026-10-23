/* gbprof - what a Game Boy game asks of an emulator, per frame.
 *
 * A host-side instrument for docs/plans/REALDMG-PLAN.md, built and run by
 * tools/gbprof.py, which fetches Peanut-GB (MIT) at a pinned commit and
 * patches five hook calls into a copy of peanut_gb.h.  Nothing here ships.
 *
 * Per frame it splits the CPU's T-states into HALT (free to an emulator that
 * skips to the next event), spin-poll (a <=12-byte backward loop that re-reads
 * memory and writes nothing), countdown (the same with no data read) and
 * BUSY (everything else - the work an emulator must actually do), and counts
 * the events an emulator design is priced by: MBC writes, VRAM writes, OAM
 * DMA, mid-frame (raster) register writes, data reads/writes by region.
 * Statistics start at frame 800, after the scripted title-screen presses. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
struct gb_s;
static void prof_read(struct gb_s *gb, uint16_t a);
static void prof_write(struct gb_s *gb, uint16_t a, uint8_t v);
static void prof_pre(struct gb_s *gb, uint16_t pc);
static void prof_post(struct gb_s *gb, uint8_t op, uint16_t pc);
static void prof_tick(struct gb_s *gb, unsigned cyc);
#define GBPROF_READ(g,a) prof_read(g,a)
#define GBPROF_WRITE(g,a,v) prof_write(g,a,v)
#define GBPROF_PRE(g,pc) prof_pre(g,pc)
#define GBPROF_POST(g,op,pc) prof_post(g,op,pc)
#define GBPROF_TICK(g,c) prof_tick(g,c)
#define GBPROF_INT(g) ((void)0)
#define GBPROF_CYC(g,c) ((void)0)
#define GBPROF_HALTSTEP(g,c) ((void)0)
#define GBPROF_STOP(g) ((void)0)
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#include "pgb_prof.h"

static uint8_t *rom, cram[0x20000];
static uint8_t fb[144][160];
static uint8_t rr(struct gb_s *g, const uint_fast32_t a){(void)g;return rom[a];}
static uint8_t cr(struct gb_s *g, const uint_fast32_t a){(void)g;return cram[a&0x1FFFF];}
static void cw(struct gb_s *g, const uint_fast32_t a, const uint8_t v){(void)g;cram[a&0x1FFFF]=v;}
static void err(struct gb_s *g, const enum gb_error_e e, const uint16_t a){(void)g;fprintf(stderr,"gb error %d at %04x\n",e,a);exit(1);}
static void line(struct gb_s *g, const uint8_t *px, const uint_fast8_t ly){(void)g;for(int x=0;x<160;x++)fb[ly][x]=px[x]&3;}

/* --- counters --- */
enum {R_ROM0,R_ROMX,R_VRAMT,R_VRAMM,R_SRAM,R_WRAM,R_OAM,R_IO,R_HRAM,R_N};
static const char *rn[R_N]={"ROM0","ROMX","VRAMtile","VRAMmap","SRAM","WRAM","OAM","IO","HRAM"};
static int region(uint16_t a){
  if(a<0x4000)return R_ROM0; if(a<0x8000)return R_ROMX; if(a<0x9800)return R_VRAMT;
  if(a<0xA000)return R_VRAMM; if(a<0xC000)return R_SRAM; if(a<0xFE00)return R_WRAM;
  if(a<0xFF00)return R_OAM; if(a<0xFF80||a==0xFFFF)return R_IO; return R_HRAM;}
static uint16_t cur_pc; static int in_inst;
static unsigned long long rd[R_N], wr[R_N], io_rd[256], io_wr[256];
static unsigned long long fr_mbc, fr_tile, fr_map, fr_oam, fr_raster, fr_dma, fr_ioread;
static unsigned long long tot_mbc, tot_tile, tot_map, tot_raster, tot_dma, tot_inst, tot_cb;
static unsigned long long ophist[256], opcyc[256];
/* per-frame cycle buckets, in T-states */
static unsigned long long f_halt, f_poll, f_count, f_busy;
static unsigned long long t_halt, t_poll, t_count, t_busy;
/* loop detection */
static unsigned long long it_cyc, it_n, f_busy_n, t_busy_n; static int it_wrote, it_dread;
static unsigned last_inst_cyc; static uint8_t last_op; static int raster_lines[154];
static double busy_frac[100000]; static int nfr, nmeas; static int measure_from=800;
/* adjacent pairs in BUSY code, by count: superinstruction candidates (the
 * plan's section 5, lever 1). Held per loop iteration until it is classified,
 * so a spin-wait's pairs - free to an emulator that skips it - never count. */
static unsigned long long pairs[65536]; static uint16_t pbuf[65536]; static int pn, prev_op=-1;
static void pflush(int busy){ if(busy && nfr>=measure_from) for(int i=0;i<pn;i++) pairs[pbuf[i]]++; pn=0; }

static void prof_read(struct gb_s *gb, uint16_t a){(void)gb;
  if(!in_inst) return;
  if(a>=cur_pc && a<=(uint16_t)(cur_pc+2)) return;      /* opcode/operand fetch */
  int r=region(a); rd[r]++; it_dread=1;
  if(r==R_IO){ io_rd[a&0xFF]++; fr_ioread++; }}
/* ROM banks: which the game selects (2000-3FFF, the low bits MBC1/3/5
 * share), and how often a cache of K resident banks - LRU, bank 0 apart -
 * would miss: what a cartridge kept partly in extended memory would pay. */
#define LRU_MAX 16
static unsigned long long bank_used[512], lru_miss[LRU_MAX+1], bank_sel;
static int lru[LRU_MAX+1][LRU_MAX], lru_n[LRU_MAX+1], cur_bank=1;
static void bank_pick(int b){
  if(b==cur_bank) return; cur_bank=b;
  if(nfr<measure_from) return;
  bank_sel++; bank_used[b&511]++;
  for(int k=1;k<=LRU_MAX;k++){ int *l=lru[k], n=lru_n[k], i;
    for(i=0;i<n;i++) if(l[i]==b) break;
    if(i==n){ lru_miss[k]++; if(n<k) n=++lru_n[k]; i=n-1; }
    for(;i>0;i--) l[i]=l[i-1]; l[0]=b; } }
static void prof_write(struct gb_s *gb, uint16_t a, uint8_t v){(void)v;
  int r=region(a); wr[r]++; it_wrote=1;
  if(rom[0x147]>=0x19 && rom[0x147]<=0x1E){          /* MBC5: 0 is bank 0, 3000 the 9th bit */
    if(a>=0x2000 && a<0x3000) bank_pick((cur_bank&0x100)|v);
    else if(a>=0x3000 && a<0x4000) bank_pick((cur_bank&0xFF)|((v&1)<<8)); }
  else if(a>=0x2000 && a<0x4000){ int b=v&0x7F; if(!b) b=1; bank_pick(b); }
  if(a<0x8000) fr_mbc++;
  else if(r==R_VRAMT) fr_tile++;
  else if(r==R_VRAMM) fr_map++;
  else if(r==R_OAM) fr_oam++;
  else if(r==R_IO){ io_wr[a&0xFF]++;
    if(a==0xFF46) fr_dma++;
    uint8_t ly=gb->hram_io[IO_LY];
    if((a==0xFF40||a==0xFF42||a==0xFF43||a==0xFF47||a==0xFF48||a==0xFF49||a==0xFF4A||a==0xFF4B)
       && ly>=1 && ly<144){ fr_raster++; raster_lines[ly]=1; } } }
static void prof_pre(struct gb_s *gb, uint16_t pc){(void)gb; cur_pc=pc; in_inst=1;}
static void prof_post(struct gb_s *gb, uint8_t op, uint16_t pc){
  in_inst=0; last_op=op; if(nfr>=measure_from){tot_inst++; ophist[op]++; if(op==0xCB) tot_cb++;}
  (void)pc; (void)gb; }
static void prof_tick(struct gb_s *gb, unsigned cyc){
  if(gb->gb_halt){ f_halt+=cyc; return; }
  /* one executed instruction: classify via tight backward loops */
  if(nfr>=measure_from) opcyc[last_op]+=cyc;
  it_cyc+=cyc; it_n++;
  if(prev_op>=0 && pn<65536) pbuf[pn++]=(uint16_t)(prev_op<<8|last_op);
  prev_op=last_op;
  uint16_t npc=gb->cpu_reg.pc.reg;
  int taken_back = (npc < cur_pc) && (cur_pc - npc) <= 12;
  int branchy = (last_op==0x18||last_op==0x20||last_op==0x28||last_op==0x30||last_op==0x38||
                 last_op==0xC2||last_op==0xCA||last_op==0xD2||last_op==0xDA||last_op==0xC3);
  if(branchy && taken_back){
    if(!it_wrote && it_dread){ f_poll+=it_cyc; pflush(0); }  /* spin: re-reads memory/IO, writes nothing */
    else if(!it_wrote){ f_count+=it_cyc; pflush(0); }        /* pure register countdown */
    else { f_busy+=it_cyc; f_busy_n+=it_n; pflush(1); }
    it_cyc=0; it_n=0; it_wrote=0; it_dread=0;
  } else if(branchy || last_op==0xCD || last_op==0xC9 || last_op==0xD9 || (last_op&0xC7)==0xC7 || last_op==0xE9){
    f_busy+=it_cyc; f_busy_n+=it_n; it_cyc=0; it_n=0; it_wrote=0; it_dread=0; pflush(1);   /* left the loop shape */
  }
}
static void end_frame(void){
  f_busy+=it_cyc; f_busy_n+=it_n; it_cyc=0; it_n=0; it_wrote=0; it_dread=0; pflush(1);
  unsigned long long tot=f_halt+f_poll+f_count+f_busy;
  (void)tot;
  if(nfr>=measure_from && nmeas<100000) busy_frac[nmeas++]=(double)f_busy/70224.0;
  nfr++;
  if(nfr<=measure_from){ f_halt=f_poll=f_count=f_busy=f_busy_n=0; fr_mbc=fr_tile=fr_map=fr_oam=fr_raster=fr_dma=fr_ioread=0; for(int i=0;i<154;i++)raster_lines[i]=0; return; }
  t_halt+=f_halt; t_poll+=f_poll; t_count+=f_count; t_busy+=f_busy; t_busy_n+=f_busy_n;
  f_halt=f_poll=f_count=f_busy=f_busy_n=0;
  tot_mbc+=fr_mbc; tot_tile+=fr_tile; tot_map+=fr_map; tot_dma+=fr_dma;
  int rl=0; for(int i=0;i<154;i++){rl+=raster_lines[i]; raster_lines[i]=0;}
  tot_raster+=rl;
  fr_mbc=fr_tile=fr_map=fr_oam=fr_raster=fr_dma=fr_ioread=0;
}
static int cmpd(const void*a,const void*b){double x=*(double*)a,y=*(double*)b;return x<y?-1:x>y;}
static void dump(const char *fn){FILE*f=fopen(fn,"wb");fprintf(f,"P5 160 144 255\n");
  for(int y=0;y<144;y++)for(int x=0;x<160;x++)fputc(255-85*fb[y][x],f);fclose(f);}

/* input script: 'S' start taps, then a game-specific pattern */
static uint8_t joy_for(int f, int mode){
  uint8_t j=0xFF; /* active low: A B SELECT START RIGHT LEFT UP DOWN = bits 0..7 */
  if((f>=150&&f<156)||(f>=400&&f<406)||(f>=700&&f<706)) j&=~JOYPAD_START;
  if(f<800) return j;
  if(mode==0){ /* platformer: run right, jump often */
    j&=~JOYPAD_RIGHT; if((f%50)<14) j&=~JOYPAD_A; if((f%400)>370) j&=~JOYPAD_B; }
  else { /* puzzle: wander */
    static const uint8_t d[4]={JOYPAD_RIGHT,JOYPAD_DOWN,JOYPAD_LEFT,JOYPAD_UP};
    if((f%24)<4) j&=~d[(f/24*7)%4]; if((f%90)<4) j&=~JOYPAD_A; }
  return j;
}

int main(int argc,char**argv){
  if(argc<4){fprintf(stderr,"usage: prof rom frames mode [shotprefix]\n");return 2;}
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
  rom=malloc(n); if(fread(rom,1,n,f)!=(size_t)n) return 1; fclose(f);
  static struct gb_s gb;
  if(gb_init(&gb,rr,cr,cw,err,NULL)!=GB_INIT_NO_ERROR){fprintf(stderr,"init fail\n");return 1;}
  gb_init_lcd(&gb,line);
  int frames=atoi(argv[2]), mode=atoi(argv[3]);
  for(int i=0;i<frames;i++){
    gb.direct.joypad=joy_for(i,mode);
    gb_run_frame(&gb); end_frame();
    if(argc>4 && (i==frames/3||i==2*frames/3||i==frames-1)){char b[256];snprintf(b,256,"%s_%05d.pgm",argv[4],i);dump(b);}
  }
  unsigned long long T=t_halt+t_poll+t_count+t_busy;
  int NF=nmeas; printf("rom %s  frames %d, measured %d from frame %d (%.1f s of Game Boy)\n",argv[1],nfr,NF,measure_from,NF/59.7275);
  printf("T-states/frame %.0f (expect 70224)\n",(double)T/NF);
  printf("share: halt %.1f%%  spin-poll %.1f%%  countdown %.1f%%  BUSY %.1f%%\n",
    100.0*t_halt/T,100.0*t_poll/T,100.0*t_count/T,100.0*t_busy/T);
  qsort(busy_frac,NF,sizeof(double),cmpd);
  int m=NF;
  printf("BUSY T/frame %.0f = %.1f%% of a 70224-T frame\n",(double)t_busy/NF,100.0*t_busy/NF/70224);
  printf("busy per frame (of 70224 T): p50 %.1f%%  p90 %.1f%%  p99 %.1f%%  max %.1f%%\n",
    100*busy_frac[m/2],100*busy_frac[m*9/10],100*busy_frac[m*99/100],100*busy_frac[m-1]);
  printf("BUSY instructions/frame %.0f (%.2f M-cycles each)\n",(double)t_busy_n/NF,(double)t_busy/4.0/(t_busy_n?t_busy_n:1));
  printf("instructions/frame %.0f  avg M-cycles/instr (non-halt) %.2f  CB-prefixed %.1f%%\n",
    (double)tot_inst/NF,(double)(T-t_halt)/4.0/tot_inst,100.0*tot_cb/tot_inst);
  printf("per frame: MBC writes %.2f  tile-data writes %.1f  map writes %.1f  OAM DMA %.2f  raster lines %.2f\n",
    (double)tot_mbc/NF,(double)tot_tile/NF,(double)tot_map/NF,(double)tot_dma/NF,(double)tot_raster/NF);
  { int nb=0; for(int i=0;i<512;i++) if(bank_used[i]) nb++;
    printf("ROM banks: %.2f switches/frame among %d banks; LRU misses a second with K resident (bank 0 apart):",
      (double)bank_sel/NF, nb);
    for(int k=1;k<=LRU_MAX;k++) printf(" %d:%.2f", k, lru_miss[k]*59.7275/NF); printf("\n"); }
  printf("data reads/frame by region:"); for(int r=0;r<R_N;r++) printf(" %s %.0f",rn[r],(double)rd[r]/NF); printf("\n");
  printf("writes/frame by region:   "); for(int r=0;r<R_N;r++) printf(" %s %.0f",rn[r],(double)wr[r]/NF); printf("\n");
  printf("top IO reads/frame:"); for(int k=0;k<6;k++){int b=0;for(int i=0;i<256;i++)if(io_rd[i]>io_rd[b])b=i; if(!io_rd[b])break; printf(" FF%02X %.1f",b,(double)io_rd[b]/NF); io_rd[b]=0;} printf("\n");
  printf("top IO writes/frame:"); for(int k=0;k<8;k++){int b=0;for(int i=0;i<256;i++)if(io_wr[i]>io_wr[b])b=i; if(!io_wr[b])break; printf(" FF%02X %.1f",b,(double)io_wr[b]/NF); io_wr[b]=0;} printf("\n");
  { unsigned long long sp=0; for(int i=0;i<65536;i++) sp+=pairs[i];
    printf("top busy pairs (of %.0f a frame):", (double)sp/NF);
    for(int k=0;k<16;k++){int b=0;for(int i=0;i<65536;i++)if(pairs[i]>pairs[b])b=i; if(!pairs[b])break;
      printf(" %02X.%02X:%.1f%%",b>>8,b&255,100.0*pairs[b]/sp); pairs[b]=0;} printf("\n"); }
  printf("top opcodes by cycles:"); unsigned long long sumc=0; for(int i=0;i<256;i++)sumc+=opcyc[i];
  for(int k=0;k<14;k++){int b=0;for(int i=0;i<256;i++)if(opcyc[i]>opcyc[b])b=i; printf(" %02X:%.1f%%",b,100.0*opcyc[b]/sumc); opcyc[b]=0;} printf("\n");
  return 0;
}
