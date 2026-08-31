/* ============================================================================
 * os8088 - apps/loom/loom.h
 *
 * LOOM's own header: the IDE's state, the scratch workspace's layout, and the
 * prototypes every part of the one translation unit shares. A C package is one
 * compilation (SPEC.md 73.1), so this file is #included by apps/loom/loom.c
 * before the parts and nothing here is ever compiled twice.
 *
 * IT IS NOT THE CONTRACT. docs/WEAVE-SPEC.md is, and every constant below
 * carries the section it is a copy of. Where this file and that one disagree,
 * that one is right (the os88.h/os88api.inc rule, said again).
 *
 * LOOM IS A SEPARATE PACKAGE FROM WEAVE (WEAVE-SPEC 1.2, the WORD/CWORD
 * precedent: two things may not answer to one name). What the two SHARE they
 * share as source - apps/weave/weave.h's format constants, apps/weave/
 * wblob.inc's claim accessors, apps/weave/wdraw.inc's paint cores,
 * apps/weave/wflow.c's flow walk and apps/weave/wfxc.c's FX compiler are all
 * %included or #included by both images, never copied (SPEC.md 20.5.1, this
 * platform's only code-sharing mechanism).
 * ==========================================================================*/

#ifndef LOOM_H
#define LOOM_H

/* ============================================================================
 * THE PROJECT (WEAVE-SPEC 11.2)
 *
 * A folder: MAIN.WML (required), MAIN.WJS (iff a <script> names it),
 * SPRITES.WSP (iff sprites), SHEET.WFX (iff a grid has initial cells). Loom
 * opens the .WML; the companions are found beside it, by the .WML's own stem
 * first and then by 11.2's project spellings - which is exactly what
 * tools/weavesim.py's pack_project() does, and the two must agree about which
 * file they read or the byte-identity gate (11.1) compares two different
 * projects.
 * ==========================================================================*/

#define LM_SLOT_WML   0             /* the four slots, in sidebar order */
#define LM_SLOT_WJS   1
#define LM_SLOT_WFX   2
#define LM_SLOT_WSP   3
#define LM_NSLOT      4

/* One source file's ceiling, and it is a REFUSAL rather than a truncation.
 * The three committed demo projects are 350..918 bytes a file (apps/weave/
 * demos/), so this is seven times the largest thing the family has ever
 * packed. A file over it opens read-only with the sentence in the status row:
 * a truncated source would pack a bundle nobody wrote. */
#define LM_TEXTMAX  6144            /* ...and four of them is 24,576 bytes.
                                     * The claim that holds them is bigger:
                                     * apps/loom/loom.c's LM_CLAIMKB adds the
                                     * editor's glass shadow behind the four
                                     * slots, because 4,576 bytes of bss would
                                     * be 7.5% of SPEC.md 20.1's whole
                                     * allowance for image AND bss */

#define LM_MAXLINE   400            /* the line index, one word a line. A
                                     * source longer than this many lines is
                                     * refused with the same voice as
                                     * LM_TEXTMAX - 400 lines of WML is four
                                     * times the largest demo */

/* ============================================================================
 * THE SCRATCH WORKSPACE - one heap claim, LM_WORKKB, taken by Pack and freed
 * when Pack is done (WEAVE-SPEC 11.4).
 *
 * EVERY COMPILER TABLE LIVES HERE AND NOT IN BSS, and the reason is
 * SPEC.md 73.14 rather than taste: the compilers are overlay tenants, so their
 * CODE is free - it ships in LOOM.OVL - while "every global, literal and bss
 * byte it names stays resident and DS-relative". A 4,000-byte component table
 * declared as a C array would be 4,000 bytes of LOOM.O88's resident image for
 * a body that runs once per Pack. So the tables are byte offsets into this
 * claim, reached through lm_wb()/lm_wpb()/lm_ww()/lm_wpw(), and the resident
 * cost of the whole pack step is one segment word.
 *
 * The regions are laid out end to end below; each is sized from the largest
 * thing the format can legally carry OR from a stated bound with a refusal
 * sentence of its own (WEAVE-SPEC 10.5's voice: what was written, the bound,
 * the fact).
 * ==========================================================================*/

#define LMW_ATOFF     0x0000        /*   384: 2.7's count word + 188 offsets */
#define LMW_ATTXT     0x0180        /*  6144: the interned strings, packed */
#define LMW_TOKS      0x1980        /* 10240: 1,280 WJS tokens of 8 bytes */
#define LMW_CODE      0x4180        /*  6144: 2.8's bytecode, all functions */
#define LMW_FUNCS     0x5980        /*  2048: 128 function rows of 16 */
#define LMW_GLOBS     0x6180        /*   768: 128 global rows of 6 */
#define LMW_COMPS     0x6480        /*  5008: 250 component rows of 20 */
#define LMW_PROPS     0x7810        /*  4096: 1,024 property rows of 4 */
#define LMW_NAMES     0x8810        /*  2048: the handler, sprite-image and
                                     *        list-item NAMES and blobs the
                                     *        WML pass has to keep past the
                                     *        element that wrote them */
#define LMW_FX        0x9010        /*  4096: 5.3's RPN streams, packed */
#define LMW_FXOFF     0xA010        /*   512: 256 formula offsets */
#define LMW_CELLS     0xA210        /*  3072: 384 CELLS rows of 8 */
#define LMW_SPRD      0xAE10        /*   256: 16 sprite rows of 16 */
#define LMW_SPRB      0xAF10        /*  6144: 2.11's image + mask bytes */
#define LMW_END       0xC710        /* = 50,960 */

/* There is NO UISTREAM region, and its absence is worth a line: the writer
 * emits 2.5's records straight into the OUTPUT image at their final offset,
 * because the section count - and therefore where the first section starts -
 * is known before any body is built (2.3). A staging area for it would have
 * been 2,560 bytes of claim that nothing ever read. */

#define LM_WORKKB     50            /* ...and the claim that holds it */

/* The per-region capacities, each with the sentence that refuses past it. */
#define LM_MAXATOM   187            /* 2.7: ids 64..250 */
#define LM_ATTXTMAX 6144
#define LM_MAXTOK   1280
#define LM_CODEMAX  6144
#define LM_MAXFUNC   128            /* 2.8 */
#define LM_MAXGLOB   128            /* 2.8 */
#define LM_MAXCOMP   250            /* 2.5: comp_id is 1..250 */
#define LM_MAXPROP  1024
#define LM_NAMEMAX  2048
#define LM_FXMAX    4096
#define LM_MAXFORM   255
#define LM_MAXCELL   384
#define LM_MAXSPR     16            /* 2.11 */
#define LM_SPRBMAX  6144

/* The row strides, in bytes. */
#define LM_TOKSZ       8
#define LM_FUNCSZ     16
#define LM_GLOBSZ      6
#define LM_COMPSZ     20
#define LM_PROPSZ      4
#define LM_SPRDSZ     16

/* --- the component row (LMW_COMPS + LM_COMPSZ * i) ----------------------- */
#define LMC_CTYPE      0            /* b: 2.5.1 */
#define LMC_ID         1            /* b: comp_id, 1..250, document order */
#define LMC_W          2            /* b */
#define LMC_H          3            /* b */
#define LMC_STYLE      4            /* b: 2.5.2 */
#define LMC_CFLAGS     5            /* b: 2.5.3 */
#define LMC_CARD       6            /* b: 1-based card index */
#define LMC_NPROP      7            /* b: how many rows at LMC_PROP */
#define LMC_PROP       8            /* w: first row index in LMW_PROPS */
#define LMC_LINE      10            /* w: the WML line, for 10.5's messages */
#define LMC_IDOFF     12            /* w: the `id` attribute's SPAN in the
                                     *    WML text, 0 = none. An id is never
                                     *    interned: a string in the pool that
                                     *    weavesim's pool does not have would
                                     *    fail the byte compare */
#define LMC_IDLEN     14            /* b */
#define LMC_AUX       15            /* b: a <sprite>'s img name length */
#define LMC_AUXOFF    16            /* w: ...and its offset in LMW_NAMES */
#define LMC_SPARE     18            /* w */

/* --- the property row (LMW_PROPS + 4 * i), 2.6's own shape --------------- */
#define LMP_ATOM       0            /* b */
#define LMP_KIND       1            /* b: PK_* */
#define LMP_VAL        2            /* w */

/* --- the function row (LMW_FUNCS + 8 * i) -------------------------------- */
#define LMF_NAMEOFF    0            /* w: offset of the name in the WJS text */
#define LMF_NAMELEN    2            /* b */
#define LMF_NARGS      3            /* b */
#define LMF_NLOCALS    4            /* b */
#define LMF_FLAGS      5            /* b: LMFF_* */
#define LMF_CODEOFF    6            /* w: offset in LMW_CODE */
#define LMF_CODELEN    8            /* w */
#define LMF_BODY0     10            /* w: token index of the body's '{' */
#define LMF_BODY1     12            /* w: ...and of its matching '}' */
#define LMF_NOPS      14            /* w: 4.11.1's op count */
#define LMFF_BACKJMP 0x01           /* 4.11.1: a backward jump was patched */
#define LMFF_CALL    0x02           /* ...and a CALL or a BUILT was emitted */

/* --- the global row (LMW_GLOBS + 6 * i) ---------------------------------- */
#define LMG_NAMEOFF    0            /* w */
#define LMG_NAMELEN    2            /* b */
#define LMG_KIND       3            /* b: LMGK_* - the initializer's shape */
#define LMG_VAL        4            /* w: the int, the atom, or the size */
#define LMGK_ZERO      0            /* int 0 - no module-init store needed */
#define LMGK_INT       1
#define LMGK_STR       2
#define LMGK_BOOL      3
#define LMGK_NULL      4
#define LMGK_ARRAY     5

/* --- the WJS token row (LMW_TOKS + 8 * i) -------------------------------- */
#define LMT_KIND       0            /* b: LMTK_* */
#define LMT_AUX        1            /* b: the punctuator id, LMPU_* */
#define LMT_LINE       2            /* w */
#define LMT_VAL        4            /* w: number, atom id, or name length */
#define LMT_OFF        6            /* w: offset of the text in the source */
#define LMTK_EOF       0
#define LMTK_NUM       1
#define LMTK_STR       2            /* LMT_VAL is the interned atom id */
#define LMTK_ID        3
#define LMTK_KW        4            /* LMT_VAL is the LMKW_* index */
#define LMTK_PUNCT     5            /* LMT_AUX is the LMPU_* index */

/* --- the sprite descriptor row (LMW_SPRD + 16 * i) ----------------------- */
#define LMS_NAMEOFF    0            /* w: into the .WSP text */
#define LMS_NAMELEN    2            /* b */
#define LMS_WB         3            /* b: width in bytes, 1..8 */
#define LMS_HPX        4            /* b: 1..64 */
#define LMS_FRAMES     5            /* b: 1..8 */
#define LMS_PAD        6            /* b */
#define LMS_DATA       8            /* w: offset in LMW_SPRB */
#define LMS_LEN       10            /* w: bytes at LMS_DATA */

/* ============================================================================
 * THE OUTPUT IMAGE - a second claim, 62KB, holding the finished .WAB
 * (WEAVE-SPEC 2.1's cap and 11.4's "stages the image in a transient claim").
 * ==========================================================================*/

#define LM_OUTKB      62            /* 63,488 = 2.1's 0xF800 exactly */

/* ============================================================================
 * THE WORKSPACE ACCESSORS
 *
 * On the machine these wrap apps/weave/wblob.inc's w_b/w_w/w_pb/w_pw over two
 * claim segments; in apps/loom/hosttest/ they are two plain arrays, which is
 * what lets the compilers be diffed against tools/weavesim.py in a second
 * rather than in a boot. Data in a claim is reached through a (segment,
 * offset) PAIR and never as a C pointer - a C pointer here is a package-DS
 * offset and nothing else (C64-SPEC 3.6's rule).
 * ==========================================================================*/

unsigned lm_wb(unsigned off);               /* scratch: one byte, unsigned */
void     lm_wpb(unsigned off, unsigned v);
unsigned lm_ww(unsigned off);               /* ...and the little-endian word */
void     lm_wpw(unsigned off, unsigned v);
void     lm_wfill(unsigned off, unsigned v, unsigned n);

unsigned lm_ob(unsigned off);               /* the output image */
void     lm_opb(unsigned off, unsigned v);
void     lm_opw(unsigned off, unsigned v);
void     lm_ofill(unsigned off, unsigned v, unsigned n);

/* The four source texts, as the compilers see them. A source is a NUL-free
 * byte run the editor owns; lm_src() answers its base and lm_srclen() its
 * length, and both answer 0/0 for a slot the project does not have. On the
 * machine the text lives in the source claim, so these are (segment, offset)
 * readers too - lm_sb() is the one byte-at-a-time door the parsers use. */
unsigned lm_srclen(int slot);
unsigned lm_sb(int slot, unsigned off);     /* one byte, 0 past the end */

/* ============================================================================
 * PACK ERRORS (WEAVE-SPEC 10.5)
 *
 * Format: `<file>:<line>: <message>`, and the sentences are the SAME TEXT
 * tools/weavesim.py prints - that is what tests/weave/packerr/ diffs. A
 * compiler raises one by calling lm_perr() and returning; every caller up the
 * chain tests lm_failed() and returns without emitting. There is no setjmp in
 * this toolchain and no exception in this language, so the discipline is
 * "raise and unwind by hand", which is also why every parse routine answers
 * an int rather than a value.
 * ==========================================================================*/

#define LM_ERRMAX    160            /* one message, NUL included */

void        lm_perr(int slot, int line, const char *msg);
void        lm_perrn(int slot, int line, const char *a, int n,
                     const char *b);
int         lm_failed(void);
void        lm_clearerr(void);
const char *lm_errtext(void);       /* the whole `<file>:<line>: <message>` */
int         lm_errline(void);
int         lm_errslot(void);

/* lm_num - append a decimal to a NUL-terminated buffer of LM_ERRMAX. The
 * messages carry counts and bounds ("188 app atoms; the cap is 187"), and
 * os88_itoa needs a caller-owned buffer, which a static one is. */
void        lm_cat(char *dst, const char *s);
void        lm_catn(char *dst, int v);
void        lm_catu(char *dst, unsigned v);
const char *lm_fname(int slot);
void        lm_setfname(int slot, const char *s);

/* The scanners' whole character vocabulary. The C library is not on this
 * floppy and locale is not a thing here. */
int lm_lower(int c);
int lm_isdigit(int c);
int lm_isalpha(int c);
int lm_isalnum(int c);
int lm_isspace(int c);
int lm_srceq(int sslot, unsigned off, unsigned len, const char *lit);
int lm_srceqi(int sslot, unsigned off, unsigned len, const char *lit);

/* ============================================================================
 * THE ATOM INTERNER (WEAVE-SPEC 2.7, 2.14 rule 3)
 *
 * First appearance wins, in one pinned traversal: (a) the WML document in
 * document order - element by element, attributes in 3.3's table order for
 * that element, then text content; (b) the WJS source in token order; (c) FX
 * formulas in CELLS order. Duplicate strings intern once.
 * ==========================================================================*/

/* THE STRING BUILDER is how a string reaches the interner: every parser
 * assembles the FOLDED, entity-expanded, whitespace-collapsed bytes into
 * lm_sbuf and then interns it, which is exactly the order tools/weavesim.py
 * builds a Python string and calls Interner.intern() in. 256 bytes, because
 * 2.7 caps an atom's length byte at 255. */
#define LM_SBUF  256
extern char lm_sbuf[LM_SBUF];
extern int  lm_sbn;
extern int  lm_sbover;
extern int  lm_sbwant;              /* the builder overflowed - the caller
                                     * raises 2.7's length refusal */
void     lm_sbclear(void);
void     lm_sbputc(int c);          /* RAW - already folded */
void     lm_sbfold(int c);          /* ...through lm_fold(), dropping -1 */
void     lm_sbtrim(void);           /* 3.1: drop leading/trailing spaces */

int      ovl_intern(int slot, int line);   /* lm_sbuf -> 64..250; 0 on a
                                            * pack error, which is set */
int      lm_natom(void);
unsigned lm_atom_off(int aid);             /* into LMW_ATTXT */
unsigned lm_atom_len(int aid);
void     lm_atoms_reset(void);

/* WEAVE-SPEC 3.1's Latin-1 fold to ASCII 0x20..0x7E, the browser's own table.
 * Every attribute value, every text run and every WJS string literal goes
 * through it; the cell font has 95 glyphs (SPEC.md 6.1) and nothing outside
 * them survives to a bundle. */
int lm_fold(int c);                        /* -1 = drop the byte */

/* ============================================================================
 * THE MODEL, filled by lmwml.c and read by lmpack.c
 * ==========================================================================*/

#define LM_MAXCARD     8            /* 3.2: 1..8 cards */
#define LM_MAXMENU     5            /* MENU_APPMAX, SPEC.md 12.2 */
#define LM_MAXITEM     8

/* Everything the app-level analysis produced. Small enough to be bss and it
 * has to be: lmpack.c's writer reads it after the parsers have gone. */
extern char     lm_appname[16];     /* 2.2's name field, <= 15 chars */
extern int      lm_vmkb;            /* 3.3: app vm=, 16..32 */
extern int      lm_entrycard;       /* 1-based */
extern int      lm_ncard;
extern int      lm_ncomp;
extern int      lm_nprop;
extern int      lm_nblob;
extern int      lm_nrec;            /* UISTREAM records emitted */
extern int      lm_nfunc;
extern int      lm_nglob;
extern int      lm_ncell;
extern int      lm_nform;
extern int      lm_nspr;            /* <sprite> ELEMENTS in the WML */
extern int      lm_nspr_art;        /* ...and sprite entries in the .WSP */
extern int      lm_nmenu;
extern int      lm_startfn;         /* 2.6.2's module-init index, -1 = none */
extern int      lm_hasgrid, lm_hascanvas, lm_hasscript;
extern int      lm_gridcols, lm_gridrows;
extern int      lm_canvw, lm_canvh, lm_canvspr;
extern int      lm_flags;           /* 2.2.1, computed by the writer */
extern int      lm_used;            /* the builtin bitmap, for the flags */
extern char     lm_scriptsrc[13];   /* the <script src=""> 8.3 name */
extern int      lm_scriptline;

/* The menu model: 5 menus of 8 items, each item a label atom and either a
 * function index or 0xFF (2.6.2). The oncommand NAMES are resolved after the
 * script has been collected, so the row keeps the name's source span. */
extern unsigned char lm_mtitle[LM_MAXMENU];
extern unsigned char lm_mcount[LM_MAXMENU];
extern unsigned char lm_mlabel[LM_MAXMENU * LM_MAXITEM];
extern unsigned      lm_mfnoff[LM_MAXMENU * LM_MAXITEM];   /* into the WML */
extern unsigned char lm_mfnlen[LM_MAXMENU * LM_MAXITEM];
extern int           lm_mline[LM_MAXMENU * LM_MAXITEM];

/* The event bindings a component declared, kept as SOURCE SPANS until the
 * script exists to resolve them against (pack_project's own order). */
#define LM_MAXEV   128
extern unsigned char lm_evcomp[LM_MAXEV];   /* the component's INDEX */
extern unsigned char lm_evatom[LM_MAXEV];
extern unsigned      lm_evoff[LM_MAXEV];
extern unsigned char lm_evlen[LM_MAXEV];
extern int           lm_evline[LM_MAXEV];
extern int           lm_nev;

/* ============================================================================
 * THE COMPILERS - the overlay's tenants (WEAVE-SPEC 1.2's LOOM.OVL list, in
 * its order: the WML compiler, the WJS compiler, the FX pre-compiler, the
 * atom interner, the bundle writer).
 *
 * EVERY ONE ANSWERS "IT RAN" SEPARATELY FROM WHAT IT DECIDED, because a
 * refused overlay returns 0 (apps/cc/crt0.asm). So each returns 1 on success,
 * 0 on failure - and lm_failed() says whether the failure was a pack error or
 * the module never loading, which is the difference a refusal message has to
 * be able to make.
 * ==========================================================================*/

int ovl_wml(void);                  /* parse + analyse LM_SLOT_WML */
int ovl_wjs(void);                  /* tokenize + collect (2.14 rule 3b) */
int ovl_wjs_gen(void);              /* ...and 4.6, AFTER the sheet and art */
int ovl_sheet(void);                /* .WFX -> CELLS + FXCODE (via wfxc.c) */
int ovl_sprites(void);              /* .WSP -> SPRITES */
int ovl_resolve(void);              /* events and menu items -> fn indices */
int ovl_write(void);                /* -> the output claim; lm_outlen() */
unsigned lm_outlen(void);

/* ovl_pack_rest - everything after the WML, in one door: the script, the
 * sheet, the sprites, the resolve and the write. It is a SECOND door rather
 * than one call because the WML is what names the .WJS (3.3's <script src>),
 * and the two halves of the gate find that file differently - the machine has
 * it in a project slot already (lmproj.c read it at open), the host harness
 * opens it here. Both then run this. */
int ovl_pack_rest(void);

/* ovl_pack - the whole of File > Pack Bundle, in one door. 1 = a bundle is in
 * the output claim and lm_outlen() is its size; 0 = it did not happen, and
 * lm_failed() distinguishes a pack error (lm_errtext() has the sentence) from
 * an overlay that would not load. */
int ovl_pack(void);

/* ============================================================================
 * THE SEAM TO LOOM.WPV (WEAVE-SPEC 1.2.4)
 *
 * apps/loom/lmpv.inc's five routines. The module is a SECOND RESIDENT SEGMENT
 * holding WEAVE's flow walk and WEAVE's component painter, read once when
 * Preview is first opened; apps/weave/wpvabi.inc is the contract and
 * apps/weave/wpvabi.h its C side, guarded in both assemblies.
 *
 * lpv_call() answers 0 with no module bound, which is the same "it did not
 * happen" a refused overlay gives (apps/cc/crt0.asm) - so a caller asks "did
 * it run" separately from "what did it say".
 * ==========================================================================*/
unsigned lpv_call(unsigned verb, unsigned a, unsigned b, unsigned c);
void     lpv_bindmod(unsigned seg);     /* 0 = forget it */
int      lpv_kb(void);                  /* the claim, in KB: image + bss */
unsigned lpv_bytes(void);               /* WPV_SIZE, to check the read */
int      lpv_stamp(unsigned seg);       /* 0 ok, 1 not ours/truncated,
                                         * 2 stale (ABI, size or bss) */

/* ============================================================================
 * THE IDE'S OWN STATE
 * ==========================================================================*/

#define LM_ST_EMPTY   0             /* no project open */
#define LM_ST_EDIT    1             /* editing a source */
#define LM_ST_PREVIEW 2             /* 1.7's Preview pane is up */

/* The sidebar (WEAVE-SPEC 13.1's "project folder + file switcher"). */
#define LM_SIDE_CELLS 12            /* its width, in 8px cells */
#define LM_ROWH        8

/* WEAVE-SPEC 1.7's shortcuts, read in os88_onkey() as control characters -
 * `apps/os88line.inc` hands a control byte back to its caller rather than
 * inserting it, so a shortcut and a focused editor cannot fight over one. The
 * menu item's label says so, which is Note Pad's convention verbatim. */
#define LM_K_PACK   0x10            /* ^P - Pack Bundle */
#define LM_K_SAVE   0x13            /* ^S - Save */
#define LM_K_NEXT   0x0E            /* ^N - the next file in the project */

#endif /* LOOM_H */
