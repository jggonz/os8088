/* gbcover - what a STATIC recompiler of a Game Boy game would have to cover.
 *
 * A host-side instrument for docs/reports/REALDMG-STATIC-2026-10-20.md, built
 * and run by tools/gbstatic.py against the same pinned, patched Peanut-GB as
 * tools/gbprof.py, with gbprof's own input script.  Nothing here ships.
 *
 * It writes one text file of per-SITE records, a site being an instruction's
 * (bank, PC) - bank 0 below 4000h, the mapped ROM bank in 4000-7FFF, 1FFh for
 * code run from RAM - so tools/gbstatic.py can put what RAN beside what a
 * recursive-descent disassembler FINDS:
 *
 *   I bank pc op cb all busy cyc rdF rdS wrF wrS fl0 flZ flC flZC flX
 *       an executed site: executions from frame 800 (all, and busy = less the
 *       spin-waits and countdowns RealDMG itself skips - REALDMG-SPEC section
 *       8's rules, which are NARROWER than gbprof's classifier: a loop such
 *       as ADD HL,DE / DEC B / JR NZ is skipped by gbprof's and run by
 *       RealDMG, so it is busy here), its T-states, its data accesses by
 *       whether the interpreter's FAST path takes them (F) or they leave for
 *       rd_slow/wr_slow (S), and how often the flags it wrote were never read
 *       (fl0), read for Z only, C only, Z and C, or for N/H (DAA, PUSH AF)
 *   E sbank spc dbank dpc n
 *       a control transfer (JR/JP/CALL/RET/RETI/RST/JP HL), not-taken
 *       conditionals included, and the interrupt dispatch (sbank FFFFh)
 *   X bank pc op cb        a site executed only before frame 800 (boot,
 *                          title): code a static translator also has to find
 *
 * The per-site access, flag and edge counts are over ALL executions;
 * gbstatic.py weights them by busy/all.
 *
 * Flag liveness ignores an interrupt handler: its PUSH AF/POP AF hands the
 * interrupted code its flags back whatever they were, so a flag the
 * interrupted code never reads is dead however the handler saves it. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
struct gb_s;
static void cv_read(struct gb_s *gb, uint16_t a);
static void cv_write(struct gb_s *gb, uint16_t a, uint8_t v);
static void cv_pre(struct gb_s *gb, uint16_t pc);
static void cv_post(struct gb_s *gb, uint8_t op, uint16_t pc);
static void cv_tick(struct gb_s *gb, unsigned cyc);
static void cv_int(struct gb_s *gb);
#define GBPROF_READ(g,a) cv_read(g,a)
#define GBPROF_WRITE(g,a,v) cv_write(g,a,v)
#define GBPROF_PRE(g,pc) cv_pre(g,pc)
#define GBPROF_POST(g,op,pc) cv_post(g,op,pc)
#define GBPROF_TICK(g,c) cv_tick(g,c)
#define GBPROF_INT(g) cv_int(g)
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
static void line(struct gb_s *g, const uint8_t *px, const uint_fast8_t ly){(void)g;(void)px;(void)ly;}

typedef struct {
  uint32_t key; uint8_t op, cb;
  unsigned long long all, busy, cyc, rdF, rdS, wrF, wrS, fl[5]; int pre;
} rec_t;
#define NREC (1<<18)
static rec_t recs[NREC]; static int nrec;
static int htab[1<<20];
static int nfr, measure_from=800;

static uint32_t keyof(struct gb_s *gb, uint16_t pc){
  unsigned b = pc<0x4000 ? 0 : pc<0x8000 ? (gb->selected_rom_bank & gb->num_rom_banks_mask) : 0x1FF;
  return (b<<16)|pc; }
static int recof(uint32_t key){
  unsigned h=(key*2654435761u)>>12;
  for(;;h=(h+1)&((1<<20)-1)){
    int i=htab[h]; if(!i){ if(nrec+1>=NREC){fprintf(stderr,"too many sites\n");exit(1);}
      i=++nrec; recs[i].key=key; htab[h]=i; return i; }
    if(recs[i].key==key) return i; } }

/* edges */
typedef struct { uint32_t s,d; unsigned long long n; } edge_t;
#define NEDGE (1<<20)
static edge_t edges[NEDGE]; static int nedge; static int etab[1<<21];
static void edge(uint32_t s, uint32_t d){
  if(nfr<measure_from) return;
  unsigned h=((s*2654435761u)^(d*40503u))>>11;
  for(;;h=(h+1)&((1<<21)-1)){
    int i=etab[h]; if(!i){ if(nedge+1>=NEDGE){fprintf(stderr,"too many edges\n");exit(1);}
      i=++nedge; edges[i].s=s; edges[i].d=d; etab[h]=i; edges[i].n=1; return; }
    if(edges[i].s==s && edges[i].d==d){ edges[i].n++; return; } } }

/* flag liveness: bit 0 Z, 1 N, 2 H, 3 C */
typedef struct { int rec, bits, live; } pend_t;
static pend_t pend[8]; static int owner[4]={-1,-1,-1,-1};
static void fin(int k){
  if(nfr>=measure_from){
    int L=pend[k].live; rec_t *r=&recs[pend[k].rec]; int c;
    if(!L) c=0; else if(L&6) c=4; else if(L==1) c=1; else if(L==8) c=2; else c=3;
    r->fl[c]++; }
  pend[k].rec=0; }
static void fwrite_bits(int rec, int W){
  for(int b=0;b<4;b++) if(W>>b&1){ int o=owner[b]; if(o>=0){ pend[o].bits&=~(1<<b); if(!pend[o].bits) fin(o);} owner[b]=-1; }
  int k; for(k=0;k<8;k++) if(!pend[k].rec) break;
  pend[k].rec=rec; pend[k].bits=W; pend[k].live=0;
  for(int b=0;b<4;b++) if(W>>b&1) owner[b]=k; }
static void fread_bits(int R){
  for(int b=0;b<4;b++) if(R>>b&1){ int o=owner[b]; if(o>=0) pend[o].live|=1<<b; } }
/* an interrupt: the handler's flags are its own */
#define ISTK 8
static pend_t ipend[ISTK][8]; static int iown[ISTK][4], isp[ISTK], idep;
static void cv_int(struct gb_s *gb){
  if(idep<ISTK){ memcpy(ipend[idep],pend,sizeof pend); memcpy(iown[idep],owner,sizeof owner);
    isp[idep]=gb->cpu_reg.sp.reg; idep++; }
  memset(pend,0,sizeof pend); for(int b=0;b<4;b++) owner[b]=-1;
  edge(0xFFFFFFFFu, 0); }
static void iret_check(struct gb_s *gb){
  while(idep>0 && gb->cpu_reg.sp.reg >= isp[idep-1]){        /* returned past the frame */
    idep--; for(int k=0;k<8;k++) if(pend[k].rec) fin(k);
    memcpy(pend,ipend[idep],sizeof pend); memcpy(owner,iown[idep],sizeof owner); } }

/* (W, R) flag bits by opcode: Z=1 N=2 H=4 C=8 */
static void flags_of(uint8_t op, uint8_t cb, int *W, int *R){
  *W=0; *R=0;
  if(op==0xCB){ if(cb<0x40){ *W=15; if((cb&0xF0)==0x10) *R=8; } else if(cb<0x80) *W=7; return; }
  if(op>=0x80 && op<0xC0){ *W=15; if((op&0xF0)==0x80 && (op&8)) *R=8; if((op&0xF0)==0x90 && (op&8)) *R=8; return; }
  switch(op){
  case 0xC6: case 0xD6: case 0xE6: case 0xEE: case 0xF6: case 0xFE: *W=15; return;
  case 0xCE: case 0xDE: *W=15; *R=8; return;
  case 0x07: case 0x0F: *W=15; return;
  case 0x17: case 0x1F: *W=15; *R=8; return;
  case 0x09: case 0x19: case 0x29: case 0x39: *W=14; return;
  case 0xE8: case 0xF8: *W=15; return;
  case 0x27: *W=13; *R=14; return;
  case 0x2F: *W=6; return;
  case 0x37: *W=14; return;
  case 0x3F: *W=14; *R=8; return;
  case 0xF1: *W=15; return;
  case 0xF5: *R=15; return;
  case 0x20: case 0x28: case 0xC2: case 0xCA: case 0xC4: case 0xCC: case 0xC0: case 0xC8: *R=1; return;
  case 0x30: case 0x38: case 0xD2: case 0xDA: case 0xD4: case 0xDC: case 0xD0: case 0xD8: *R=8; return; }
  if((op&0xC7)==0x04 || (op&0xC7)==0x05) *W=7;          /* INC r, DEC r */
}

/* the busy classifier: one loop iteration buffered until it is judged */
static int ibuf[65536], in_n; static unsigned long long it_cyc; static int it_wrote, it_dread;
static void iflush(int busy){
  if(nfr>=measure_from) for(int i=0;i<in_n;i++){ recs[ibuf[i]].all++; if(busy) recs[ibuf[i]].busy++; }
  in_n=0; }

static uint16_t cur_pc; static int cur_rec, in_inst; static uint32_t cur_key;
static void cv_read(struct gb_s *gb, uint16_t a){(void)gb;
  if(!in_inst) return;
  if(a>=cur_pc && a<=(uint16_t)(cur_pc+2)) return;      /* opcode/operand fetch */
  int io = (a>=0xFF00 && a<0xFF80) || a==0xFFFF;
  int slow = (a>=0x4000 && a<0x8000) || io;
  if(!io) it_dread=1;                                   /* a register is never a spin's subject */
  if(nfr>=measure_from){ if(slow) recs[cur_rec].rdS++; else recs[cur_rec].rdF++; } }
static void cv_write(struct gb_s *gb, uint16_t a, uint8_t v){(void)gb;(void)v;
  it_wrote=1;
  if(!in_inst) return;
  int slow = a<0x8000 || (a>=0xFF00 && a<0xFF80) || a==0xFFFF;
  if(nfr>=measure_from){ if(slow) recs[cur_rec].wrS++; else recs[cur_rec].wrF++; } }
static void cv_pre(struct gb_s *gb, uint16_t pc){
  cur_pc=pc; in_inst=1; cur_key=keyof(gb,pc); cur_rec=recof(cur_key);
  if(nfr<measure_from) recs[cur_rec].pre=1;
  if(!recs[cur_rec].all && !recs[cur_rec].op){ recs[cur_rec].op=__gb_read(gb,pc); recs[cur_rec].cb=__gb_read(gb,pc+1); } }
static int is_ct(uint8_t op){
  return op==0x18||op==0x20||op==0x28||op==0x30||op==0x38||op==0xC3||op==0xC2||op==0xCA||op==0xD2||op==0xDA||
         op==0xCD||op==0xC4||op==0xCC||op==0xD4||op==0xDC||op==0xC9||op==0xC0||op==0xC8||op==0xD0||op==0xD8||
         op==0xD9||op==0xE9||(op&0xC7)==0xC7; }
static uint8_t last_op; static int prev_op2=-1;
static void cv_post(struct gb_s *gb, uint8_t op, uint16_t pc){(void)pc;
  in_inst=0; prev_op2=last_op; last_op=op;
  int W,R; flags_of(op, recs[cur_rec].cb, &W, &R);
  fread_bits(R); if(W) fwrite_bits(cur_rec, W);
  if(is_ct(op)){ edge(cur_key, keyof(gb, gb->cpu_reg.pc.reg)); }
  if(op==0xC9||op==0xD9||op==0xC0||op==0xC8||op==0xD0||op==0xD8) iret_check(gb); }
static void cv_tick(struct gb_s *gb, unsigned cyc){
  if(gb->gb_halt) return;
  if(in_n<65536) ibuf[in_n++]=cur_rec;
  if(nfr>=measure_from) recs[cur_rec].cyc+=cyc;
  it_cyc+=cyc;
  uint16_t npc=gb->cpu_reg.pc.reg;
  int back = cur_pc - npc;                 /* a taken JR back: npc = pc + 2 + e */
  int jr = last_op==0x18||last_op==0x20||last_op==0x28||last_op==0x30||last_op==0x38;
  if(jr && npc < cur_pc && back >= 2 && back <= 14){
    /* RealDMG's own spin-wait (REALDMG-SPEC section 8): a loop of -4 to -16
     * that read memory, not a register, and wrote nothing. Its countdown,
     * DEC A/B/C / JR NZ,-3, is the arm below. Every other loop is run. */
    iflush(it_wrote || !it_dread);
    it_cyc=0; it_wrote=0; it_dread=0;
  } else if(last_op==0x20 && npc==cur_pc-1 && (prev_op2==0x05||prev_op2==0x0D||prev_op2==0x3D)){
    iflush(0); it_cyc=0; it_wrote=0; it_dread=0;
  } else if(jr || last_op==0xC3 || (last_op&0xE7)==0xC2 || last_op==0xCD || last_op==0xC9 ||
            last_op==0xD9 || (last_op&0xC7)==0xC7 || last_op==0xE9 || (last_op&0xE7)==0xC0 ||
            (last_op&0xE7)==0xC4){
    iflush(1); it_cyc=0; it_wrote=0; it_dread=0; } }

static uint8_t joy_for(int f){            /* gbprof's platformer script */
  uint8_t j=0xFF;
  if((f>=150&&f<156)||(f>=400&&f<406)||(f>=700&&f<706)) j&=~JOYPAD_START;
  if(f<800) return j;
  j&=~JOYPAD_RIGHT; if((f%50)<14) j&=~JOYPAD_A; if((f%400)>370) j&=~JOYPAD_B;
  return j; }

int main(int argc,char**argv){
  if(argc<4){fprintf(stderr,"usage: gbcover rom frames out\n");return 2;}
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
  rom=malloc(n); if(fread(rom,1,n,f)!=(size_t)n) return 1; fclose(f);
  static struct gb_s gb;
  if(gb_init(&gb,rr,cr,cw,err,NULL)!=GB_INIT_NO_ERROR){fprintf(stderr,"init fail\n");return 1;}
  gb_init_lcd(&gb,line);
  int frames=atoi(argv[2]);
  for(int i=0;i<frames;i++){ gb.direct.joypad=joy_for(i); gb_run_frame(&gb); iflush(1); nfr++; }
  FILE*o=fopen(argv[3],"w");
  fprintf(o,"F %d %d\n", nfr-measure_from, measure_from);
  for(int i=1;i<=nrec;i++){ rec_t*r=&recs[i];
    if(r->all) fprintf(o,"I %x %x %x %x %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
      r->key>>16, r->key&0xFFFF, r->op, r->cb, r->all, r->busy, r->cyc, r->rdF, r->rdS, r->wrF, r->wrS,
      r->fl[0], r->fl[1], r->fl[2], r->fl[3], r->fl[4]);
    else fprintf(o,"X %x %x %x %x\n", r->key>>16, r->key&0xFFFF, r->op, r->cb); }
  for(int i=1;i<=nedge;i++) fprintf(o,"E %x %x %x %x %llu\n", edges[i].s>>16, edges[i].s&0xFFFF,
      edges[i].d>>16, edges[i].d&0xFFFF, edges[i].n);
  fclose(o); return 0;
}
