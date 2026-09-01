/* ============================================================================
 * os8088 - apps/weave/weave.h
 *
 * WEAVE's own header: the .WAB format's constants (WEAVE-SPEC 2), the
 * runtime's state, and the prototypes of the hand-written cores. Part of one
 * translation unit - `nasm -f bin` has no notion of an external symbol, so a C
 * package is one compilation (SPEC.md 73.1) and this file is #included by
 * apps/weave/weave.c before the parts.
 *
 * IT IS NOT THE CONTRACT. docs/WEAVE-SPEC.md is, and every constant below
 * carries the section it is a copy of. Where this file and that one disagree,
 * that one is right (the os88.h/os88api.inc rule, said again).
 * ==========================================================================*/

#ifndef WEAVE_H
#define WEAVE_H

/* --- the bundle (WEAVE-SPEC 2) ------------------------------------------ */

#define W_CAP        0xF800    /* 2.1: total size cap, 63,488 bytes - so every
                                * internal offset is a 16-bit word within one
                                * segment */
#define W_HDR_SIZE   32        /* 2.2: the header ends at +32 exactly */
#define W_ROW_SIZE   8         /* 2.3: one section-table row */

/* 2.2 - the header's fields, by offset. */
#define W_H_MAGIC     0        /* 4: 'W','A','B',0x1A */
#define W_H_VERSION   4        /* 2: 1, and any other value refuses */
#define W_H_TOTAL     6        /* 2: the file size in bytes */
#define W_H_FLAGS     8        /* 2: 2.2.1 */
#define W_H_VMKB     10        /* 1: 16..32 */
#define W_H_GRIDKB   11        /* 1: 0 or 8..26 */
#define W_H_NSEC     12        /* 1: 5..9 */
#define W_H_ENTRY    13        /* 1: entry card, 1-based */
#define W_H_CANVASKB 14        /* 1: 0 or 2..8 */
#define W_H_RSVD     15        /* 1: 0 */
#define W_H_NAME     16        /* 16: 15 chars + NUL, 0x00 after */

/* 2.2.1 - the flags word.  Bits 5..15 set refuse the bundle. */
#define WABF_GRID    0x0001
#define WABF_CANVAS  0x0002
#define WABF_TIMER   0x0004
#define WABF_STATE   0x0008
#define WABF_SOURCE  0x0010
#define WABF_KNOWN   0x001F

/* 2.4 - the nine section types.  Indexed straight into w_soff[]/w_slen[]. */
#define W_UISTREAM   1
#define W_PROPS      2
#define W_CODE       3
#define W_ATOMS      4
#define W_FXCODE     5
#define W_CELLS      6
#define W_SPRITES    7
#define W_ICON       8
#define W_SOURCE     9
#define W_NSECTYPE  10         /* one past the last, for the arrays */

/* 2.5 - UISTREAM record kinds and layout. */
#define W_REC_SIZE   10
#define W_REC_END    0x00
#define W_REC_CARD   0x01
#define W_REC_COMP   0x02
#define W_R_ID        1        /* comp_id, or a card index */
#define W_R_CTYPE     2
#define W_R_W         3        /* cells, 0 = natural (7.3) */
#define W_R_H         4        /* rows of 8px, 0 = natural */
#define W_R_STYLE     5        /* 2.5.2 */
#define W_R_CFLAGS    6        /* 2.5.3 */
#define W_R_PROPS     8        /* word: PROPS offset, 0xFFFF = none */
#define W_NOPROPS    0xFFFF

/* 2.5.1 - ctype.  0x0F+ is unassigned and a reader refuses it. */
#define WC_LABEL     0x01
#define WC_TEXT      0x02
#define WC_RULE      0x03
#define WC_BOX       0x04
#define WC_SPACER    0x05
#define WC_METER     0x06
#define WC_BUTTON    0x07
#define WC_CHECK     0x08
#define WC_RADIO     0x09
#define WC_INPUT     0x0A
#define WC_LIST      0x0B
#define WC_GRID      0x0C
#define WC_CANVAS    0x0D
#define WC_SPRITE    0x0E
#define WC_MAX       0x0E

/* 2.5.2 - the style byte: the WHOLE style vocabulary.  No colours: two of
 * three adapters are 1bpp and half-honoured colour pairs made invisible text
 * twice in this tree (WEAVE-SPEC 9.2, SPEC.md 39.4). */
#define WS_BOLD      0x01
#define WS_INVERT    0x02
#define WS_ALIGN     0x0C      /* 0 left, 1 center, 2 right; 3 refuses */
#define WS_ALIGNSH   2
#define WS_KNOWN     0x0F

/* 2.5.3 - cflags. */
#define CF_BREAK     0x01
#define CF_HIDDEN    0x02
#define CF_DISABLED  0x04
#define CF_KNOWN     0x07

/* 2.6 - property record kinds. */
#define PK_INT       0
#define PK_ATOM      1
#define PK_BLOB      2
#define PK_FUNC      3
#define PK_SPRITE    4
#define PK_MAX       4

/* 2.7.1 - the well-known atoms this wave needs by name.  Ids 1..63 are
 * pinned and are NOT in the pool; 64..250 are the bundle's own. */
#define WA_TEXT       1
#define WA_VALUE      2
#define WA_LABEL      3
#define WA_ENABLED    4
#define WA_CHECKED    5
#define WA_HIDDEN     6
#define WA_X          7
#define WA_Y          8
#define WA_VX         9
#define WA_VY        10
#define WA_FRAME     11
#define WA_SHOWN     12
#define WA_MIN       13
#define WA_MAX       14
#define WA_ROWS      15
#define WA_COLS      16
#define WA_GROUP     19
#define WA_WALLS     23
#define WA_TICK      24
#define WA_COLOR     25             /* 6.10.7's palette, 2.7.1's newest three */
#define WA_INK       26
#define WA_PAPER     27
#define WA_CARD      20
#define WA_START     40
#define WA_SEL       17
#define WA_SELROW    21
#define WA_SELCOL    22
#define WA_CELL      32
#define WA_SETCELL   33
#define WA_RECALC    34
#define WA_SELECT    35
#define WA_GO        37
#define WA_SET       38
#define WA_GET       39
#define WA_STOP      36
#define WA_CLEAR     41
#define WA_ONCLICK   48
#define WA_ONCHANGE  49
#define WA_ONKEY     50
#define WA_ONSELECT  51
#define WA_ONEDIT    52
#define WA_ONCALC    53
#define WA_ONCOLLIDE 54
#define WA_ONWALL    55
#define WA_ONSCORE   56
#define WA_ONTICK    57
#define WA_ONCOMMAND 58
#define WA_ONTIMER   59
#define WA_ONALERT   60
#define WA_ITEMS     61
#define WA_MENUS     62
#define WA_APP_FIRST 64
#define WA_APP_LAST 250

/* --- the runtime's own limits ------------------------------------------- */

/* 2.5: comp_id is 1..250 and a card may hold every one of them, so the walk's
 * table is sized for the format rather than for a guess about a card. Ten
 * bytes each in bss, which SPEC.md 73.9 prices as the cheap half. */
#define W_MAXLAY    250
#define W_NCOMP     251        /* ...and a table keyed by comp_id: 1..250,
                                * so index 250 has to exist (2.5) */
#define W_MAXCARD     8        /* 2.5: card index <= 8 */

/* The header probe (WEAVE-SPEC 10.1, SPEC.md 18.4.4): os88_file_read_at()'s
 * capacity must be a whole number of clusters, so this is two 512-byte
 * sectors - every floppy geometry this OS builds has a cluster of one or two
 * sectors. A volume whose cluster is LARGER degrades honestly; see w_probe(). */
#define W_PROBE   1024

#define W_MSG       88         /* one refusal sentence, NUL included */

/* --- the runtime's states (1.5's trap 3: load, THEN branch) -------------- */
#define W_ST_DECK    0         /* launched empty: 1.6's Deck lands here */
#define W_ST_RUN     1         /* a bundle is loaded and validated */
#define W_ST_ERR     2         /* refused: w_msg holds section 10's sentence */

/* ============================================================================
 * THE HAND-WRITTEN CORES (SPEC.md 73.11: the inner loop is assembly)
 *
 * apps/weave/wblob.inc and apps/weave/wdraw.inc, %included by the shim after
 * the compiled C.  Data in the bundle claim is passed as an explicit
 * (segment, offset) PAIR and never as a C pointer: a C pointer here is a
 * package-DS offset and nothing else (C64-SPEC 3.6's rule).
 * ==========================================================================*/

unsigned w_b(unsigned seg, unsigned off);      /* one byte, UNSIGNED - `char`
                                                * is signed in this toolchain
                                                * and 0x80 would read as -128 */
unsigned w_w(unsigned seg, unsigned off);      /* the little-endian word */
void     w_copy(unsigned seg, unsigned off, char *dst, unsigned n);
void     w_pb(unsigned seg, unsigned off, unsigned v);     /* INTO a claim */
void     w_pw(unsigned seg, unsigned off, unsigned v);
void     w_pcopy(char *src, unsigned seg, unsigned off, unsigned n);
int      w_print(unsigned seg, unsigned off, unsigned n);  /* all 0x20..0x7E? */
int      w_zero(unsigned seg, unsigned off, unsigned n);   /* all 0x00? */

/* --- wdraw.inc: the paint and hit-test cores (WEAVE-SPEC 1.2's seam) ------
 *
 * Every one takes SCREEN PIXELS and arguments already resolved. Which
 * property carries which and where the atom pool is are wpaint.c's; these
 * draw, and know nothing about the bundle's shape - which is the line LOOM's
 * Preview has to be able to cross.
 *
 * `disabled` reaches all of them through SPEC.md 47's PEN, which wpaint.c
 * sets once per component with os88_gfx_pen(). Only wd_button and wd_glyph
 * take the flag as well, because os88ui_btn and os88ui_glyph put the pen back
 * themselves and would otherwise draw a greyed control live. */

/* the two text flags (WEAVE-SPEC 6.2), mirrored in wdraw.inc */
#define WD_BOLD  1                     /* the double strike, one px right */
#define WD_PAD   2                     /* pad to `cells`: the padding IS the
                                        * erase - no fill-then-letter pair */

/* os88ui_sbhit's answers, in WEAVE's own names because a C file may not name
 * an nasm equ. wdraw.inc carries a %if that fails the build if os88ui.inc
 * ever renumbers these - naming this file as the one that follows. */
#define WD_SB_NONE  0
#define WD_SB_UP    1
#define WD_SB_DOWN  2
#define WD_SB_PGUP  3
#define WD_SB_PGDN  4
#define WD_SB_THUMB 5

void w_draw_run(int x, int y, unsigned seg, unsigned off, unsigned len,
                int cells, int ink, int paper, int flags);
void w_draw_text(int x, int y, const char *s, int cells,
                 int ink, int paper, int flags);

void wd_rule(int x1, int x2, int y);
void wd_box(int x1, int y1, int x2, int y2);
void wd_meter(int x1, int y1, int x2, int y2, int value, int max);
void wd_mdelta(int x1, int y1, int x2, int y2, int oldv, int newv,
               int max);      /* 6.4's delta: ONE call, or none */
void wd_xor(int x1, int y1, int x2, int y2);
void wd_input(int x1, int y1, int x2, int y2, const char *s);

void wd_button(int *rect, const char *label, int dis, int down, int fill);
void wd_glyph(int x, int y, int radio, int on, int dis);

void wd_sbar(int *blk);                /* os88ui.inc's 7-word scroll block */
void wd_sbmove(int *blk, int oldpos);
int  wd_sbhit(int *blk, int x, int y); /* WD_SB_* */

/* ============================================================================
 * THE FX FORMULA VM (WEAVE-SPEC 5), apps/weave/wfx.inc
 *
 * A SECOND CORE, and the two share only WEAVE-SPEC 4.10's slice: WJS is
 * 16-bit signed over the VM claim and FX is 32-bit 16.16 over the GRID claim.
 * Every value below is a PAIR of ints, low word then high, because this
 * toolchain has no long at all (SPEC.md 73.7) - which is the whole reason the
 * arithmetic is not here.
 * ==========================================================================*/

void wfx_bind(unsigned gseg, unsigned cols, unsigned rows);
int  wfx_cell(int r, int c, int *out2);      /* 0 empty/label, 1 num, 2 err */
int  wfx_eval(unsigned seg, unsigned off, unsigned len, int *out3);
int  wfx_mag(int lo, int hi, int *out2);     /* |v|; 1 when it was negative */
unsigned wfx_cents(unsigned lo);             /* 5.2.1's two truncated cents */
unsigned wfx_frac(unsigned num, unsigned den);   /* 5.1's decimal -> 16.16 */

/* --- apps/weave/wband.inc: the grid's band composer (WEAVE-SPEC 6.9.1) ----
 * WG_STRIDE is that file's own constant and weave.asm carries a %if that
 * fails the build if it stops covering the widest content grid. */
#define WG_STRIDE 90

void wg_band(unsigned char *dst, const char *chars, int n, int inv0, int inv1);

/* ============================================================================
 * THE WJS VM (WEAVE-SPEC 4), apps/weave/wvm.inc
 *
 * Everything below is a copy of that file's own constants and IT IS NOT THE
 * CONTRACT either: docs/WEAVE-SPEC.md is, and wvm.inc is written from it. A
 * disagreement between the two %if-fails the build - see the guard at the
 * foot of wvm.inc's include in weave.asm.
 * ==========================================================================*/

/* 4.3 - the tags. */
#define WT_INT    0
#define WT_STR    1
#define WT_ARR    2
#define WT_COMP   3
#define WT_NULL   4
#define WT_BOOL   5

/* wvm_slice's answers. */
#define WR_IDLE   0
#define WR_DONE   1
#define WR_MORE   2
#define WR_GC     3
#define WR_ERR    4

/* 10.6.1's sentences, as codes. 0..6 are the core's own and are raised inside
 * wvm.inc; 7 and up are the runtime's and travel out through WN_ERRC. The
 * TEXT of all of them is in wevent.c, which is the only place that has a
 * status row to put one in. */
#define WE_TYPE   0
#define WE_DIV0   1
#define WE_STRSP  2
#define WE_DEEP   3
#define WE_AIDX   4
#define WE_OPCODE 5
#define WE_BUILT  6
#define WE_NOCOMP 7             /* no component %d. */
#define WE_NOPROP 8             /* no property "%s" on a %s. */
#define WE_NOMETH 9             /* no method "%s" on a %s. */
#define WE_NOIMPL 10            /* no method. */
#define WE_LIDX   11            /* list index %d of %d. */
#define WE_GCELL  12            /* grid cell %d,%d of %dx%d. */
#define WE_CARD   13            /* card %s of %d. */
#define WE_RAND   14            /* rand of %s. */
#define WE_GDIV0  15            /* cell is #DIV0. */
#define WE_GRANGE 16            /* cell %s is out of int range. */
#define WE_GPOOL  17            /* grid pool full. */
#define WE_FRAME  18            /* frame %d of %d. */
#define WE_FPS    19            /* start(%s): fps is 1..18. */
#define WE_NERR   20

/* The native block (wvm.inc's WN_*), in WORDS - the C side indexes an int
 * array and the assembly side a byte offset, so every name here is the
 * assembly's halved. NB() is the one accessor; there is deliberately no
 * struct, because a struct assignment is one of the four C refusals
 * (SPEC.md 73.5) and a struct here would invite one. */
#define WN_SIZE   54
#define WNW_KIND   0
#define WNW_COMP   1
#define WNW_ATOM   2
#define WNW_ARGC   3
#define WNW_REST   4
#define WNW_RESV   5
#define WNW_ERRC   6
#define WNW_ERRA   7
#define WNW_ERRB   8
#define WNW_ARG0   9            /* 9 cells of 2 words each */

#define WNK_GETP   0
#define WNK_SETP   1
#define WNK_CALLM  2
#define WNK_BUILT  3

/* 8.1's builtin indices. */
#define WB_ALERT   0
#define WB_TIMER   1
#define WB_SAVE    2
#define WB_LOAD    3
#define WB_PLAY    4
#define WB_TONE    5

/* --- the core's C surface (apps/weave/wvm.inc) --------------------------- */
void wvm_bind(unsigned vmseg, unsigned vmbytes, unsigned bseg,
              unsigned codeoff, unsigned codelen, unsigned nfunc,
              unsigned atomoff, unsigned natoms, void *nblk, unsigned seed);
void wvm_unbind(void);
void wvm_argi(int v);
int  wvm_begin(int fn);
int  wvm_busy(void);
void wvm_abort(void);
int  wvm_slice(int budget);
void wvm_gc(void);
int  wvm_mark(int tag, int handle);
int  wvm_gcreq(void);
int  wvm_errcode(void);
int  wvm_errfn(void);
int  wvm_erra(void);
int  wvm_errb(void);
int  wvm_enq(int comp, int atom, int d1, int d2);
int  wvm_deq(int *rec4);
int  wvm_rcount(void);
int  wvm_str_read(int handle, char *dst, unsigned cap);
int  wvm_str_make(const char *s);
int  wvm_atom_handle(int atom);
int  wvm_gtag(int i);
int  wvm_gval(int i);
void wvm_gset(int i, int tag, int val);
unsigned wvm_save(unsigned seg, unsigned off, unsigned cap);
int  wvm_load(unsigned seg, unsigned off, unsigned len);

/* --- apps/weave/wui.inc: the shared alert and the arm/fire word ---------- */
/* os88ui.inc is assembly and a C file may not name an nasm label, so these
 * four are the whole of what WEAVE needs out of it that wdraw.inc did not
 * already wrap. wu_ask's completion is a near proc in wui.inc that calls
 * w_alertdone() below - which is why the runtime never writes a `retf`
 * (SPEC.md 20's package boundary, solved once). */
int  wd_ask(const char *msg, void *win, int set);   /* 0 up, -1 refused */
void wd_arm(int i);
int  wd_fire(void);
int  wd_armed(void);

/* ...and apps/os88line.inc's field, which declares no storage of its own -
 * every routine takes a block the CALLER owns (6.7). */
void wd_ldraw(int *blk);
int  wd_lkey(int *blk, int ascii, int scan);   /* 0 used it, -1 the caller's */
int  wd_lclick(int *blk, int x, int y);        /* 0 inside, -1 outside */
void wd_lset(int *blk, const char *s);
void wd_lcaron(int *blk);
void wd_lcaroff(int *blk);

#define WD_AOK      0           /* os88ui.inc's button sets */
#define WD_AYESNO   1
#define WD_AMAX    34           /* the message, in characters (8.2) */
#define WD_ACANCEL (-1)         /* ...and the answer for a DISMISSED alert */

/* ============================================================================
 * WEAVE.WSM - THE CANVAS CORE (WEAVE-SPEC 1.2.2, 6.10), apps/weave/wcanvas.asm
 *
 * A SECOND, RESIDENT SEGMENT and not an overlay, because every byte of it
 * runs on a WORKER task per frame and SPEC.md 73.14's cc_ovneed refuses a
 * worker at its first instruction. WEAVE-PLAN 2.9 prices the alternatives.
 *
 * Everything below is a C COPY of apps/weave/wsmabi.inc, which is the file
 * both assemblies share, and IT IS NOT THE CONTRACT: docs/WEAVE-SPEC.md
 * 6.10.3 and 6.10.4 are. weave.asm carries %ifs that fail the build if any of
 * these drift - a stale copy here would read a frame counter out of the
 * middle of the staging ring, assemble cleanly and run wrong.
 * ==========================================================================*/

#define WSMV_BIND   0               /* 6.10.3's eight verbs */
#define WSMV_SPRITE 1
#define WSMV_START  2
#define WSMV_STOP   3
#define WSMV_PAINT  4
#define WSMV_DRAIN  5
#define WSMV_UNBIND 6
#define WSMV_PLACE  8

#define WSMP_W      0               /* WSMV_BIND's parameter block */
#define WSMP_H      1
#define WSMP_WALLS  2
#define WSMP_TICK   3
#define WSMP_NSPR   4
#define WSMP_SPOFF  5
#define WSMP_CID    6
#define WSMP_COLOR  7               /* 6.10.7: paper colour in the low byte */
#define WSMP_NW     8               /* ...in WORDS: the C side hands over an
                                     * int array and the module reads bytes */

#define WSMF_X      0               /* WSMV_SPRITE's fields */
#define WSMF_Y      1
#define WSMF_VX     2
#define WSMF_VY     3
#define WSMF_FRAME  4
#define WSMF_SHOWN  5
#define WSMF_NFRAME 6
#define WSMF_DESC   7
#define WSMF_COLOR  8               /* 6.10.7, load path only */

#define WSS_RUN     0               /* the state block, at WSM_H_STATE */
#define WSS_ACK     1
#define WSS_SLEEP   2
#define WSS_FRAME   6
#define WSS_CVSEG   8
#define WSS_BLITS  18
#define WSS_FRAMES 20
#define WSS_OVF    22

#define WSM_MAXSPR 16               /* 2.11's own cap */

/* --- the seam (apps/weave/wcv.inc) ---------------------------------------
 * The magic, the ABI number and the module's size are NOT copied here: they
 * are nasm equs out of the two files the two assemblies share, and a C copy
 * would be a fourth place for the contract to drift - which is the exact
 * failure the ABI word exists to prevent. wcv_stamp() checks all three where
 * they live. */
unsigned wcv_call(unsigned verb, unsigned a, unsigned b, unsigned c);
void     wcv_bindmod(unsigned seg);
void     wcv_run(void *win);        /* the worker's body; it does not return */
int      wcv_kb(void);              /* the module's claim, in KB */
unsigned wcv_bytes(void);           /* ...and in bytes, to check the read */
int      wcv_stamp(unsigned seg);   /* 0 ok, 1 not ours/truncated, 2 stale */
unsigned wcv_stateoff(unsigned seg);
void     wcv_ops(int spent);        /* 4.12's banner: one exhausted slice */
unsigned wcv_opsps(void);           /* ...and the last window's answer */

/* --- 4.10's slice model -------------------------------------------------- */
#define W_SLICE0  256           /* the start budget, 256 << cpu tier */
#define W_SLICEMIN 128
#define W_SLICEMAX 1536
#define W_RUNAWAY  90           /* ticks before 4.11's alert (~4.9 s) */

/* 4.8.1's list-item override pool: 64 is one full <list> (2.6.1's own cap). */
#define W_NLSET    64

/* 6.7's field bound. apps/os88line.inc declares no storage - every routine
 * takes a block the CALLER owns - so WEAVE's fields come out of a fixed pool,
 * assigned in UISTREAM order at load. Eight blocks over 512 bytes of text is
 * eight full-width fields (cols caps at 60) or twenty typical ones; past it a
 * field is painted and refuses focus, with 6.7's sentence in the toast. */
#define W_MAXIN     8
#define W_IPOOL   512

/* 6.7's caret, in ticks between blinks. 4 is ~220 ms at 18.2 Hz - slow enough
 * that the blink costs two ~756 us primitives a fifth of a second on the
 * target, and fast enough to read as a caret. */
#define W_BLINK     4

#endif /* WEAVE_H */
