/* gbtrace - Peanut-GB's instruction trace, in RealDMG's trace format.
 *
 * The reference half of tools/gbtrace.py, built against the same patched
 * copy of peanut_gb.h that tools/gbprof.py makes.  One line per instruction,
 * before it runs - exactly the 14-byte record apps/realdmg/rdgsys.inc's
 * rdg_trline sends on COM1 from an RDG_TRACE core: PC, A, F, BC, DE, HL, SP and
 * the M-cycles so far, words little-endian.
 * Nothing here ships. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
struct gb_s;
static void tr_pre(struct gb_s *gb, uint16_t pc);
static void tr_write(uint16_t a, uint8_t v);
static int tr_io(uint16_t a, uint8_t *v);
/* Where Peanut-GB is not the hardware, the reference is corrected to it, so
 * that a difference is RealDMG's:
 *  - LY. Each visible line zeroes Peanut's line counter and drops the cycles
 *    that overran it, so its LY falls behind time. A read answers the exact
 *    line, (M-cycles since the LCD was switched on / 114) mod 154.
 *  - DIV and TIMA. On the hardware both run off one free-running counter
 *    that a DIV write zeroes: DIV is its top byte, and TIMA ticks when the
 *    TAC-selected bit of it falls. Peanut keeps a TIMA counter of its own,
 *    phased to nothing in particular. A DIV read answers the counter, and
 *    before every instruction Peanut's TIMA counter is set to its phase.
 *  - FF4C-FF7F, where a DMG has nothing and reads FF.
 *  - An interrupt's dispatch, HALT's granularity and STOP's length: below. */
#define GBPROF_READ(g,a) do { uint8_t tr_v; if(tr_io(a, &tr_v)) return tr_v; } while(0)
#define GBPROF_WRITE(g,a,v) tr_write(a,v)
#define GBPROF_PRE(g,pc) tr_pre(g,pc)
#define GBPROF_POST(g,op,pc) ((void)0)
static unsigned long long tcyc;
static int tr_int; static unsigned tr_op;
/* The M-cycle of an instruction its memory access falls in, counted from 0:
 * the hardware reads and writes at a fixed point inside the instruction, and
 * Peanut-GB at its start. LY, DIV and TIMA are answered, and LCDC and DIV
 * writes take effect, as of that M-cycle - mem_timing's rule, and RealDMG's
 * (REALDMG-SPEC section 6.2). */
static unsigned acc_rd(unsigned op){
  switch(op){
  case 0x0A: case 0x1A: case 0x2A: case 0x3A: case 0x34: case 0x35: case 0xF2: return 1;
  case 0xF0: case 0xCB: return 2;
  case 0xFA: return 3;
  }
  if((op>=0x46&&op<=0x7E&&(op&7)==6) || (op>=0x86&&op<=0xBE&&(op&7)==6)) return 1;
  return 0;
}
static unsigned acc_wr(unsigned op){
  switch(op){
  case 0x02: case 0x12: case 0x22: case 0x32: case 0xE2: return 1;
  case 0x34: case 0x35: case 0x36: case 0xE0: return 2;
  case 0xEA: case 0xCB: case 0x08: return 3;
  }
  if(op>=0x70&&op<=0x77&&op!=0x76) return 1;
  return 0;
}
#define GBPROF_TICK(g,c) (tcyc += (c))
/* An interrupt's dispatch is 5 M-cycles on the hardware and none in
 * Peanut-GB: charge them with the handler's first instruction, so Peanut's
 * own timers see them, and show them in that instruction's time. */
#define GBPROF_INT(g) (tr_int = 1)
#define GBPROF_CYC(g,c) do { tr_op = opcode; if(tr_int){ (c) += 20; tr_int = 0; } } while(0)
/* HALT: Peanut advances a halted CPU by whole LCD modes, so it can wake well
 * after the interrupt. One M-cycle at a time wakes it on the M-cycle. */
#define GBPROF_HALTSTEP(g,c) do { if((g)->gb_halt) (c) = 4; } while(0)
/* STOP is two bytes, 10 00: Peanut takes one and runs the 00 as a NOP. */
#define GBPROF_STOP(g) ((g)->cpu_reg.pc.reg++)
#define ENABLE_SOUND 0
#define ENABLE_LCD 0
#include "pgb_prof.h"

static uint8_t *rom, cram[0x20000];
static unsigned long long left, t0, lcd_on_at; static int lcd_on=1;
static long long tdiv = -0xABCC;      /* T-state the counter was zero: ABCCh at 0100 on a DMG */
static uint8_t rr(struct gb_s *g, const uint_fast32_t a){(void)g;return rom[a];}
static uint8_t cr(struct gb_s *g, const uint_fast32_t a){(void)g;return cram[a&0x1FFFF];}
static void cw(struct gb_s *g, const uint_fast32_t a, const uint8_t v){(void)g;cram[a&0x1FFFF]=v;}
static void err(struct gb_s *g, const enum gb_error_e e, const uint16_t a){(void)g;fprintf(stderr,"gb error %d at %04x\n",e,a);exit(1);}
static void tr_write(uint16_t a, uint8_t v){
  unsigned long long t = t0 + acc_wr(tr_op);
  if(a==0xFF04) tdiv=(long long)t*4;
  if(a!=0xFF40) return;
  if((v&0x80) && !lcd_on) lcd_on_at=t;
  lcd_on=(v&0x80)!=0;
}
static struct gb_s *tr_gb;
static int tr_io(uint16_t a, uint8_t *v){
  unsigned long long t = t0 + (a>=0xFF00 ? acc_rd(tr_op) : 0);
  if(a==0xFF44){ *v = lcd_on ? (uint8_t)((t-lcd_on_at)/114%154) : 0; return 1; }
  if(a==0xFF04){ *v = (uint8_t)(((long long)t*4-tdiv)>>8); return 1; }
  if(a==0xFF05 && t!=t0 && (tr_gb->hram_io[IO_TAC]&4)){   /* ticks since t0 */
    static const unsigned per[4]={1024,16,64,256};
    unsigned p=per[tr_gb->hram_io[IO_TAC]&3];
    long long n=((long long)t*4-tdiv)/p - ((long long)t0*4-tdiv)/p;
    uint8_t x=tr_gb->hram_io[IO_TIMA];
    while(n-- > 0){ x++; if(!x) x=tr_gb->hram_io[IO_TMA]; }
    *v=x; return 1;
  }
  if(a>=0xFF4C && a<0xFF80){ *v = 0xFF; return 1; }
  return 0;
}
static void tr_pre(struct gb_s *gb, uint16_t pc){
  tr_gb=gb;
  t0=tcyc/4 + (tr_int ? 5 : 0);
  { static const unsigned per[4]={1024,16,64,256};
    gb->counter.tima_count = (unsigned)(((long long)t0*4 - tdiv) % per[gb->hram_io[IO_TAC]&3]); }
  struct cpu_registers_s *r=&gb->cpu_reg;
  unsigned w[]={pc,r->bc.reg,r->de.reg,r->hl.reg,r->sp.reg,(unsigned)t0};
  uint8_t b[14]={w[0],w[0]>>8,r->a,r->f.reg&0xF0,w[1],w[1]>>8,w[2],w[2]>>8,w[3],w[3]>>8,w[4],w[4]>>8,w[5],w[5]>>8};
  fwrite(b,1,14,stdout);
  if(--left==0){fflush(stdout);exit(0);}
}
int main(int argc,char**argv){
  static struct gb_s gb;
  if(argc<3){fprintf(stderr,"usage: gbtrace rom instructions\n");return 2;}
  FILE*f=fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
  fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
  rom=malloc(n); if(fread(rom,1,n,f)!=(size_t)n)return 1; fclose(f);
  left=strtoull(argv[2],0,10);
  static char buf[1<<20]; setvbuf(stdout,buf,_IOFBF,sizeof buf);
  if(gb_init(&gb,rr,cr,cw,err,NULL)!=GB_INIT_NO_ERROR){fprintf(stderr,"init fail\n");return 1;}
  for(;;) gb_run_frame(&gb);
}
