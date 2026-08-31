/* ============================================================================
 * os8088 - apps/cc/os88.h
 *
 * The C-callable surface of the os8088 API (SPEC.md 73). One header, included
 * by exactly one .c file, which becomes one .o88 package.
 *
 * WHAT THIS FILE IS. apps/os88api.inc is the SDK for an assembly package: it
 * publishes 130-odd jump-table slots, each with a REGISTER contract. C does
 * not have register contracts - it has a cdecl stack frame - so every slot a
 * C package can reach has a hand-written bridge in apps/cc/os88thunk.asm and
 * a prototype here. os88api.inc stays the authority on what each slot DOES,
 * on what it costs and on when it may be called; this file is the shape it
 * takes in C, and every prototype below carries the section that owns it so
 * the two are one document.
 *
 * The build is `.c -> smlrcc -> tools/cc8086.py -> nasm -f bin -> os88pkg.py`
 * and it is written out in apps/cc/Makefile.inc. A C package is a .c file
 * plus a ten-line .asm shim; apps/cc/ccsmoke.c and apps/cc/ccsmoke.asm are
 * the worked example of both, and are built by `make cc-smoke`.
 *
 * ---------------------------------------------------------------------------
 * THE FOUR RULES A C AUTHOR CANNOT SEE (SPEC.md 73.5, 67.5.1, 67.7, 67.8)
 * ---------------------------------------------------------------------------
 *
 * 1. TAKING THE ADDRESS OF AN AUTOMATIC IS A BUILD FAILURE, and it has to be,
 *    because it is silently wrong. SS is LOW_SEG and DS is your package's
 *    segment, always and on every task (SPEC.md 1, 2.1): `[bp+N]` reads the
 *    stack correctly, but the ADDRESS `&x` is a bare 16-bit offset that every
 *    later dereference resolves through DS. The program writes to the stack
 *    and reads out of its own image. tools/cc8086.py refuses every
 *    bp-relative `lea` by name and there is no flag that permits it.
 *
 *        static char line[80];       ok - DS-relative, &line is a real address
 *        void f(void) { char t[80]; g(t); }      REFUSED at build time
 *        void f(void) { char t[8]; t[0] = 1; }   ok - never addressed
 *        void f(void) { char t[8]; t[i] = 1; }   REFUSED - a variable index
 *                                                is an address
 *
 *    It also catches a local struct passed or assigned BY VALUE, which the
 *    compiler implements as an address and a copy, with nothing in the C that
 *    looks like a pointer. Pass and return struct POINTERS to statics, which
 *    is what every prototype below that takes a struct does.
 *
 *    AND IT CATCHES AN OUT-PARAMETER, which is the one that surprises people
 *    and is worth reading twice, because half the calls below are out-
 *    parameters. This is refused - the struct is an automatic, so its address
 *    is a stack offset:
 *
 *        void paint(void *w) { struct os88_pt org;
 *                              os88_wm_content(w, &org); }
 *
 *    and this is the fix, which reads the same and is what apps/cc/ccsmoke.c
 *    does:
 *
 *        void paint(void *w) { static struct os88_pt org;
 *                              os88_wm_content(w, &org); }
 *
 *    A `static` declared inside a function reads correctly and is the idiom.
 *    Two things it costs you, because nothing will remind you: it is not
 *    re-entrant (your worker and your callbacks share it, and they pre-empt
 *    each other), and it is spent against the 60KB ceiling rather than
 *    against the stack.
 *
 * 2. ES BELONGS TO THE KERNEL. Every callback is entered with ES = KERNEL_SEG
 *    (SPEC.md 20.1) because the two things the kernel hands you - the window
 *    record and the file dialog's name - live there. Compiled C never loads
 *    ES, so a `rep movsb` the compiler emitted would write into the KERNEL,
 *    not into your buffer: tools/cc8086.py refuses movs/stos/scas/cmps
 *    outright. You never need them - os88_memcpy() and friends below are
 *    hand-written DS-only loops - and you never read the window record
 *    directly either: os88_win_w() and its siblings do the `es:` override for
 *    you and are correct in any context, because they load KERNEL_SEG
 *    themselves rather than trusting the ES they were entered with.
 *
 * 3. THE STACK IS TINY AND IS NOT GROWABLE. The UI task has 1,024 bytes total
 *    with a measured 246 already spent under load, and a worker has 384 with
 *    a measured 150 (SPEC.md 2.1, 67.8). tools/cc8086.py prints every
 *    function's frame size on every build and fails on any frame over 96
 *    bytes (CC_FRAME_MAX). Rule 1 is most of what keeps this true: a buffer
 *    that cannot be addressed on the stack was never going to be on it.
 *
 * 4. THERE IS NO 32-BIT INTEGER, AND `float` IS POISONED. `long` does not
 *    exist in 16-bit mode. `float` is worse than absent - SmallerC compiles
 *    it and it is silently TWO BYTES - so all three keywords are #defined
 *    below to an identifier that cannot parse, and the compiler names it:
 *
 *      Unexpected token OS88_ERROR_float_is_only_2_bytes_here__use_scaled_int
 *
 *    Where the kernel answers with 32 bits (a file size, free space) this
 *    header either splits it into two words you are given both of, or does
 *    the arithmetic in assembly and answers in KB. See os88_file_read() and
 *    os88_disk_free_kb().
 *
 * ---------------------------------------------------------------------------
 * PERFORMANCE (PERFORMANCE.md; SPEC.md 73.11)
 * ---------------------------------------------------------------------------
 * The numbers do not change because the source language did. Any gfx_* call
 * is ~756 us on the target 4.77 MHz 8088 whoever makes it, one of these
 * thunks adds an OSAPI far call at 46.7 us plus a near call at 11, one 8x8
 * glyph cell is ~900 us and a 78-cell row of text is ~71 ms. A redraw is
 * priced by how many primitive calls it makes, not by how many pixels it
 * covers - so the C is free to be three to five times slower than assembly
 * INSIDE the loop and must not add a single call to it. Anything that
 * touches pixels or cells per iteration belongs in hand-written assembly
 * that C calls once (SPEC.md 6.3/6.5/65.13: a whole row of proportional
 * glyphs is composed into a 1bpp band in your own RAM and emitted with ONE
 * os88_gfx_blit1, because lettering it a glyph at a time is 79 ms of pure
 * call floor).
 *
 * ---------------------------------------------------------------------------
 * WHAT IS NOT WRAPPED, AND WHY (the full list, so nobody has to grep)
 * ---------------------------------------------------------------------------
 *   OSAPI_SND_FM / SND_STREAM / SND_PLAY  a sound DRIVER's verb interface and
 *     a clip player that freezes the desktop for its length; multi-verb
 *     register protocols that would read as a soup of ints in C. Write a
 *     sound app in assembly (apps/tracker is the reference).
 *   OSAPI_XMEM_*                          every argument and answer is a
 *     32-bit linear base, and there is no 32-bit type here (rule 4). The one
 *     part of the API that C genuinely cannot hold.
 *   OSAPI_FSX_*                           the exclusive bracket (SPEC.md 53)
 *     hands control to a near proc of yours and then forbids every drawing
 *     slot until it returns. The rules are the whole feature and none of them
 *     is checkable from C. (OSAPI_FULLSCREEN, the WINDOW latch of SPEC.md
 *     11.2, is a different thing and IS wrapped: os88_fullscreen() below.)
 *   OSAPI_GFX_LINIT / LSTEP / LSTEPV      a resumable Bresenham whose state
 *     block is explicitly not yours to read (SPEC.md 5.6.7).
 *   OSAPI_SYS_SNAPSHOT / CLAIM_SNAPSHOT / SYS_KB   buffer layouts that the
 *     kernel renumbers; for the Task Manager, not for applications.
 *   OSAPI_VOL_* / OSAPI_FS_* / OSAPI_DRV_CFG / OSAPI_FILE_*_SYS   fenced on
 *     the caller being a loaded DRIVER (SPEC.md 51.7); a package gets CF=1.
 *   OSAPI_WM_ONSIZE                       it must ANSWER in CX/DX with a size
 *     it will accept, and a C function has one return value. Write the
 *     negotiator in the shim's assembly if you need one.
 *   OSAPI_REBOOT, OSAPI_FILE_GOTO_Q, OSAPI_VOL_AT, OSAPI_WM_PREFER,
 *   OSAPI_FONT_GLYPHS, OSAPI_WM_BAND      omitted for surface, not for
 *     danger. Adding one is a thunk of a dozen lines in os88thunk.asm.
 *     (OSAPI_FILE_GOTO_QM, GOTO_Q's instance-marking twin, is wrapped as
 *     os88_file_goto_q_mark(): SPEC.md 74.1 says why the plain one was not
 *     enough for a program whose working folder changes on every call.
 *     OSAPI_WM_PREFER's argument is a 12-byte TABLE that has to outlive the
 *     call, which is a thing to get wrong from C for no gain here;
 *     os88_wm_minsize() is the half of SPEC.md 11.100 that is two ints and
 *     is wrapped. OSAPI_FILE_READ_AT was on this list until a package wanted
 *     to read a file's HEADER and decide from it before reading the rest,
 *     which os88_file_read() cannot express - it is os88_file_read_at().)
 *
 * The count: 96 of the 149 slots apps/os88api.inc publishes, plus six
 * window-record accessors and six runtime helpers that are not slots at all -
 * 117 C entry points, and every one of them is in apps/cc/os88thunk.asm.
 *
 * How those three are counted, so the next person does not have to guess:
 *   149  `%define OSAPI_<NAME> KERNEL_SEG:0x...` lines in apps/os88api.inc.
 *        OSAPI_FIND_SZ is an `equ`, not a slot, and is not one of them.
 *    96  those names that appear in apps/cc/os88thunk.asm, in code rather
 *        than in a comment - a macro invocation names its slot as an
 *        argument, so a plain grep finds them all.
 *   117  distinct `_os88_*` entry points defined in that same file, whether
 *        written out or generated by one of the CC_T_* macros. It is not
 *        96 + 12: some slots have two entry points (the _seg forms), and
 *        the callbacks a package DEFINES - os88_main, os88_paint and the
 *        rest - are not entry points into the API and are not counted.
 * ==========================================================================*/

#ifndef OS88_H
#define OS88_H

/* --- the poison (SPEC.md 73.7) -------------------------------------------
 * These three are the only #defines here that are not a constant. They are
 * spelled as an error message because the compiler prints the token it could
 * not parse, so the diagnostic explains itself at the line that caused it.
 *
 * `float` is the dangerous one: it PARSES under SmallerC and is two bytes
 * wide, so a program that declares one, assigns to it and compares it builds
 * and runs and is wrong about every value it ever held. Arithmetic on it
 * calls ___addsf3, which tools/cc8086.py refuses - but the declare-and-
 * compare program never reaches the arithmetic, so the refusal has to be
 * here, at the keyword.
 *
 * Consequence: do not #include SmallerC's own headers. This one is the whole
 * library a package gets, and it needs none of theirs. */
#define float  OS88_ERROR_float_is_only_2_bytes_here__use_scaled_int
#define double OS88_ERROR_double_does_not_exist_here__use_scaled_int
#define long   OS88_ERROR_no_32_bit_int_in_16_bit_mode__use_two_words

/* --- the shapes the API answers in ---------------------------------------
 * Every one of these is passed and returned BY POINTER, never by value: a
 * struct passed by value is copied through an address of an automatic, which
 * is rule 1 (SPEC.md 73.5). Point them at statics. */

struct os88_pt   { int x, y; };
struct os88_size { int w, h; };
struct os88_rect { int x1, y1, x2, y2; };        /* inclusive, like the API */

struct os88_mouse {                              /* OSAPI_MOUSE, SPEC.md 9 */
    int x, y;
    int btn;                                     /* bit 0 = left button down */
};

struct os88_video {                              /* OSAPI_VIDEO, SPEC.md 39.2 */
    int w, h;                                    /* the screen you ACTUALLY got */
    int dock_top;                                /* first row the dock owns:
                                                  * the desktop is OS88_MBAR_H
                                                  * .. dock_top-1 */
    int kind;                                    /* OS88_VID_VGA / HERC / CGA */
    int bpp;                                     /* 4 or 1 - see os88_gfx_pen */
};

struct os88_find {                               /* OSAPI_FILE_FIND, 19.7.1 */
    char name[13];                               /* NUL-terminated 8.3 */
    unsigned char attr;                          /* the raw FAT attribute */
    unsigned type;                               /* OS88_FT_* - read the note */
    unsigned clus;                               /* first cluster (a folder's
                                                  * is what os88_file_goto
                                                  * takes) */
    unsigned size_lo, size_hi;                   /* 32 bits, as two words */
    unsigned rsvd;
};

struct os88_place {                              /* where your instance stands */
    unsigned clus;                               /* 0 = the volume root */
    int vol;                                     /* 0 = A: */
};

/* An app menu set (SPEC.md 12.2 - the AM_ and AMENU_ offsets in
 * apps/os88api.inc). The
 * layout is the kernel's, field for field, which is why `oncmd` is here at
 * all: LEAVE IT ZERO. os88_menu_set() writes the address of the runtime's
 * command trampoline into it, because a C program cannot name an assembly
 * label. That also means the set must be WRITABLE - declare it without
 * `const`, or it lands in .rodata and the patch is silent nonsense.
 *
 * The array is a fixed OS88_MENU_MAX because C has no flexible member here;
 * the unused entries cost six bytes each in .data (SPEC.md 73.9: bss is
 * cheap, .data is paid for twice). Declare fewer menus by declaring your own
 * struct with the same first three fields and a shorter array, and cast. */
#define OS88_MENU_MAX  5                         /* = MENU_APPMAX */
struct os88_menu {
    const char *title;                           /* the bar cell's text */
    const char **items;                          /* NUL item strings; an item
                                                  * beginning with OS88_MENU_DIS
                                                  * is drawn disabled (47) */
    int nitems;
};
struct os88_menuset {
    const char *name;                            /* your name in the bar */
    int oncmd;                                   /* LEAVE 0 - the runtime's */
    int nmenus;
    struct os88_menu menu[OS88_MENU_MAX];
};

/* The layouts above are the kernel's, not this header's opinion of them, so
 * the header checks itself: a mismatch is an "Array dimension less than 1"
 * at the line below rather than a package that reads a menu set at the wrong
 * stride. Costs one byte of bss each. */
static char os88__sz_menu[sizeof(struct os88_menu)    ==  6 ? 1 : -1];
static char os88__sz_mset[sizeof(struct os88_menuset) == 36 ? 1 : -1];
static char os88__sz_find[sizeof(struct os88_find)    == 24 ? 1 : -1];

/* --- constants, mirrored from apps/os88api.inc ---------------------------
 * Mirrored, which means they are two copies of one fact: if a value here ever
 * disagrees with os88api.inc, os88api.inc is right. */

#define OS88_MBAR_H   20                         /* menu bar height, px */
#define OS88_TITLE_H  18                         /* window title bar height */
#define OS88_WMIN_W   96                         /* smallest frame a resize */
#define OS88_WMIN_H   64                         /* can leave */

#define OS88_BLACK     0                         /* the 16 EGA indices */
#define OS88_BLUE      1
#define OS88_GREEN     2
#define OS88_CYAN      3
#define OS88_RED       4
#define OS88_MAGENTA   5
#define OS88_BROWN     6
#define OS88_LGRAY     7                         /* DECORATION grey */
#define OS88_DGRAY     8                         /* the disabled pen - but set
                                                  * it with os88_gfx_pen(), not
                                                  * os88_set_color(): a greyed
                                                  * control is this colour AND
                                                  * a flag, and on the two 1bpp
                                                  * adapters the flag is the
                                                  * whole difference (47) */
#define OS88_LBLUE     9
#define OS88_LGREEN   10
#define OS88_LCYAN    11
#define OS88_LRED     12
#define OS88_LMAGENTA 13
#define OS88_YELLOW   14
#define OS88_WHITE    15

#define OS88_VID_VGA   0                         /* struct os88_video.kind */
#define OS88_VID_HERC  1
#define OS88_VID_CGA   2

#define OS88_CPU_8086  0                         /* os88_cpu() - the target */
#define OS88_CPU_286   1
#define OS88_CPU_386   2

#define OS88_CUR_ARROW 0                         /* os88_wm_cursor() */
#define OS88_CUR_CROSS 1

/* os88_file_dlg()'s mode, and os88_onfile()'s first argument.
 *
 * These are two separate one-line comments and not one two-line comment: as a
 * two-line one the `/*` opened after OS88_FDLG_OPEN's value and did not close
 * until the end of the next line, which ate the OS88_FDLG_SAVE line whole. The
 * preprocessor is happy, the header still compiles, and the only symptom is
 * that OS88_FDLG_SAVE does not exist - so the first package to open a SAVE
 * dialog fails to build for a reason nothing in it explains. */
#define OS88_FDLG_OPEN 0
#define OS88_FDLG_SAVE 1

#define OS88_MENU_DIS  1                         /* first byte of an item
                                                  * string = drawn disabled */

/* OSAPI_FILE_FIND's type word. TYPE 1 IS NOT "A FILE": it is a file the
 * LOADER would accept (*.O88), so a walk that copies type 1 copies the
 * programs and skips the documents. Ask `type < OS88_FT_DIR` for a file. */
#define OS88_FT_FILE   0
#define OS88_FT_PKG    1
#define OS88_FT_DIR    2
#define OS88_FT_UP     3

#define OS88_FERR_OK      0                      /* os88_ferr(), SPEC.md 18.4 */
#define OS88_FERR_NODISK  1
#define OS88_FERR_IO      2
#define OS88_FERR_NAME    3
#define OS88_FERR_NOENT   4
#define OS88_FERR_EXIST   5
#define OS88_FERR_FULL    6
#define OS88_FERR_DIRFULL 7
#define OS88_FERR_PROT    8
#define OS88_FERR_WPROT   9
#define OS88_FERR_BIG    10

/* ==========================================================================
 * THE FUNCTIONS YOUR PACKAGE DEFINES (SPEC.md 73.4)
 *
 * The kernel reaches a package by far-calling its dispatcher with BP = a near
 * offset, and the callback contracts pass their arguments in REGISTERS and
 * expect registers back. So none of these is ever the dispatch target: each
 * has a hand-written trampoline in apps/cc/crt0.asm that saves the registers
 * the kernel wants preserved, clears DF, pushes the arguments right to left,
 * calls you, and cleans up.
 *
 * A trampoline is assembled ONLY if the package's .asm shim asks for it, and
 * that is how "I have no key handler" is said. The two halves cannot drift:
 * a %define with no C function is an nasm external-reference error naming the
 * function, and a C function with no %define is code the kernel never calls.
 *
 *   always            void *os88_main(void);
 *                     void  os88_paint(void *win);
 *   CC_HAS_ONKEY      void  os88_onkey(int ascii, int scan, void *win);
 *   CC_HAS_ONCLICK    void  os88_onclick(int x, int y, void *win);
 *   CC_HAS_ONMOUSEUP  void  os88_onmouseup(int x, int y, void *win);
 *   CC_HAS_ONRESIZE   void  os88_onresize(int w, int h, void *win);
 *   CC_HAS_MENUS      void  os88_oncmd(int item, int menu, void *win);
 *   CC_HAS_ABOUT      void  os88_about(void *win);
 *   CC_HAS_FDLG       void  os88_onfile(int mode, const char *name,
 *                                       unsigned size_lo, unsigned size_hi,
 *                                       void *win);
 *   CC_HAS_WORKER     void  os88_worker(void *win);
 *   CC_HAS_ONWAKE     void  os88_onwake(void *win);       (74.1 - see below)
 *   CC_HAS_ONCLOSE    int   os88_onclose(void *win);      (75.1 - see below)
 * ========================================================================*/

/* os88_main - your entry point (SPEC.md 20.2, 21 step 8).
 * Runs on the UI task with the gfx lock NOT held, before your window exists
 * and before your instance is published. Create your window with
 * os88_wm_create() and return it; return 0 to abort the launch, which the
 * runtime turns into the CF the loader is owed.
 *
 * You MAY: os88_wm_create, os88_menu_set, os88_about_set, the wm_* flag
 * setters, os88_mem_claim (this is where an app sizes itself), the file
 * slots, os88_arg_file. You may NOT draw, show your window or spawn your
 * worker - the loader shows the window for you, and os88_task_spawn refuses
 * because your instance is not published yet. */
void *os88_main(void);

/* os88_paint - W_PAINT (SPEC.md 11). The gfx lock is ALREADY HELD: draw
 * directly and never take it. Content only; the kernel drew the frame. The
 * window moves, so take the origin from os88_wm_content() every call. */
void os88_paint(void *win);

void os88_onkey(int ascii, int scan, void *win);
void os88_onclick(int x, int y, void *win);
void os88_onmouseup(int x, int y, void *win);
void os88_onresize(int w, int h, void *win);
void os88_oncmd(int item, int menu, void *win);
void os88_about(void *win);
void os88_onfile(int mode, const char *name,
                 unsigned size_lo, unsigned size_hi, void *win);

/* os88_ontimer - W_ONTIMER (SPEC.md 13.9), armed by os88_wm_timer() and
 * installed by os88_wm_ontimer(). ONE-SHOT: it disarms itself before this
 * runs, so re-arm inside it for a repeat (a blinking caret) and do nothing
 * for a single shot. W_ONCLICK's environment exactly - UI task, gfx lock
 * HELD, billed to you - so it may draw. It fires while you are MINIMIZED
 * too; ask os88_wm_geom() if that matters. Needs `%define CC_HAS_ONTIMER`. */
void os88_ontimer(void *win);

/* os88_onwake - W_ONWAKE (SPEC.md 74.1), installed by os88_wm_onwake() and
 * posted by os88_wm_wake(). THE ONE CALLBACK THAT RUNS WITHOUT THE GFX LOCK:
 * on the UI task, billed to your instance, lock free. So it may call the file
 * slots (they are UI-task-only, and this IS the UI task), and it may take the
 * lock itself - os88_gfx_lock() ... os88_gfx_unlock() around a burst of
 * drawing you can put a number on - and it must not draw before it has.
 * Nothing arrives with it: it is a kick, at most one queued per window at a
 * time, and a stale one after your window's slot was reused is possible, so
 * be indifferent to being called with nothing to do. It is what lets a long
 * computation with I/O live on the UI task in slices, each re-posting the
 * next (RunCPM's Z80, SPEC.md 74). Needs CC_HAS_ONWAKE. */
void os88_onwake(void *win);

/* os88_onclose - THE CLOSE NEGOTIATOR (SPEC.md 75.1). The kernel asks before
 * it closes your window, through every door there is: the close box, the
 * app-name menu's Close, the dock tile's context menu. There is one door and
 * this is in front of it.
 *
 * ANSWER 1 to let the close happen - which is what a package with no unsaved
 * work says, and what a window with no negotiator says by default. ANSWER 0
 * to REFUSE it: nothing at all happens, your window is not even hidden, and
 * YOU NOW OWE THE USER A WAY OUT. Put up an alert (os88ui.inc's os88ui_ask,
 * OS88UI_ASAVE) and call os88_wm_close() when it is answered. A refusal with
 * nothing behind it is a window that cannot be closed, and real mode has no
 * way to take that back.
 *
 * It is os88_onclick()'s environment: the UI task, the gfx lock HELD, so it
 * may draw, may call the file slots, may raise an alert - and must not take
 * the lock and must not take long. It is never asked about itself while it is
 * running, so raising a dialog from inside one cannot recurse.
 *
 * Needs CC_HAS_ONCLOSE, and os88_wm_onclose() to install it. */
int os88_onclose(void *win);

/* os88_worker - your one background task (SPEC.md 20.6), started by
 * os88_task_spawn() from a callback. IT MUST NEVER RETURN: call
 * os88_task_alive(win) at least once per outer iteration - it never returns
 * once your close box is clicked - and os88_task_sleep() when idle. If you
 * return anyway the runtime parks you in an alive/sleep loop rather than let
 * a near `ret` jump into a random kernel offset, but that is a net, not a
 * contract. Your stack here is 384 bytes TOTAL, with the tick and mouse ISR
 * frames landing on it while you run. */
void os88_worker(void *win);

/* ==========================================================================
 * THE API
 * ========================================================================*/

/* --- drawing (SPEC.md 5) -------------------------------------------------
 * Coordinates are ABSOLUTE screen pixels and rectangles are INCLUSIVE. Every
 * one of these needs the gfx lock held; a window callback already holds it,
 * and a worker takes it with os88_gfx_lock() for a short burst and releases
 * it (20.6 rule 3 - a worker that computes under the lock wedges the machine
 * and no watchdog can break it). Each costs ~756 us on the target machine
 * whatever it draws, so the number of CALLS is the cost. */
void os88_gfx_lock(void);
void os88_gfx_unlock(void);
void os88_set_color(int colour);                 /* ONE global pen for the
                                                  * whole machine: set it in
                                                  * the same lock hold as the
                                                  * drawing it colours */
void os88_gfx_pixel(int x, int y);
void os88_gfx_hline(int x1, int x2, int y);
void os88_gfx_vline(int x, int y1, int y2);
void os88_gfx_fill(int x1, int y1, int x2, int y2);
void os88_gfx_frame(int x1, int y1, int x2, int y2);
void os88_gfx_fill_gray(int x1, int y1, int x2, int y2);   /* 50% dither */
void os88_gfx_xor_rect(int x1, int y1, int x2, int y2);
void os88_gfx_xor_fill(int x1, int y1, int x2, int y2);
void os88_gfx_line(int x1, int y1, int x2, int y2, int dilate);  /* 5.6 */

/* The 8 pattern bytes are yours and are SCREEN-aligned, so two rects that
 * abut tile seamlessly. A set bit is WHITE, bit 7 is leftmost. */
void os88_gfx_fill_pat(int x1, int y1, int x2, int y2, const void *pat8);

/* os88_gfx_pen - the DISABLED pen (SPEC.md 47). disabled != 0 gives CDGRAY
 * AND the [gfx_dis] flag, and the flag is the entire difference on the two
 * 1bpp adapters, where the colour alone rounds to solid black. Grey the whole
 * control - frame, box, icon and label - never just the caption. It lasts one
 * lock hold; put it back with os88_gfx_pen(0) when the control is drawn. */
void os88_gfx_pen(int disabled);

/* os88_gfx_scroll - move a rect's contents instead of redrawing them (5.5).
 * x1 and x2+1 must be multiples of 8. Returns 0 and leaves |dy| vacated rows
 * for you to repaint; returns -1 refused and nothing moved, so fall back to a
 * full repaint. A clip region that does not wholly contain the rect refuses. */
int os88_gfx_scroll(int x1, int y1, int x2, int y2, int dy);

/* os88_gfx_blit1 - ONE drawing call for a whole band you composed yourself
 * (SPEC.md 5.4.2, 6.3, 65.13). `bits` is a 1bpp band in your own segment in
 * the framebuffer's bit order (row-major, bit 7 leftmost, 1 = lit), `stride`
 * its bytes per row; x and w MUST both be multiples of 8; rows is 1..255; x
 * and y are signed. Returns -1 REFUSED and nothing drawn - a broken argument,
 * or a kern_small machine that carries the slot without the body - so TEST IT
 * and have a second path through os88_font_run().
 *
 * This is the call that makes proportional text affordable: the band arrives
 * in final screen polarity, no pen is read, and one of these replaces the
 * ~104 per-glyph calls a row of text would otherwise cost at 756 us each. */
int os88_gfx_blit1(const void *bits, int stride, int x, int y,
                   int w, int rows);

/* os88_gfx_blit4 - the same idea for a 4bpp image: two pixels per byte, high
 * nibble leftmost. Coalesces equal-coloured runs into horizontal lines. */
void os88_gfx_blit4(const void *pix, int stride, int x, int y, int w, int h);

/* --- text (SPEC.md 6) ----------------------------------------------------
 * The kernel's 8x8 face. One glyph cell is ~900 us and a 78-cell row is
 * ~71 ms, so a row of text is not a free thing to repaint.
 *
 * os88_font_run() below is the one you want. The two _xparent calls are
 * TRANSPARENT - they leave whatever is underneath - so a caller that fills a
 * rect and then letters it has written every one of those pixels twice, with
 * a visible gap between the two passes on the machine this OS is for
 * (SPEC.md 6.6; PERFORMANCE.md Part 1's double-draw flash). The name is the
 * point: choosing transparent should be something you typed. */
void os88_font_char_xparent(int x, int y, int ch);
void os88_font_str_xparent(int x, int y, const char *s);
int  os88_font_width(const char *s);             /* pixels */

/* os88_font_run - the cells' background AND their glyphs in one pass (6.1),
 * which is the erase-then-letter pair every text element writes by hand.
 * Prefer it: it cannot produce the clip-granularity failure that a wide erase
 * followed by glyphs can (11.3), and on a 1bpp adapter with x a multiple of 8
 * a cell row becomes a single store. */
void os88_font_run(int x, int y, const char *s, int ink, int paper);

/* --- your window (SPEC.md 11) --------------------------------------------- */

/* os88_wm_create - call it from os88_main() and return what it gives you.
 * The frame is authored against the 640x480 reference screen and clamped onto
 * the live one, so an oversized window is placed and clipped rather than
 * lost. The title string must OUTLIVE the window: point it at a literal or a
 * static, never at a buffer you reuse. Returns 0 if the window table is full,
 * which os88_main() should return straight on as its abort. */
void *os88_wm_create(int x, int y, int w, int h, const char *title);

void os88_wm_show(void *win);
void os88_wm_hide(void *win);
void os88_wm_front(void *win);
void os88_wm_destroy(void *win);                 /* the RECORD, not the pixels
                                                  * - for a SECOND window you
                                                  * own; your own closes with
                                                  * the instance */

/* os88_wm_close - CLOSE YOUR OWN WINDOW, the way your close box does (SPEC.md
 * 75.2). This is what a File > Exit / File > Quit item wants, and it is not
 * os88_wm_destroy: destroy frees the RECORD and nobody repaints the dock, so
 * a package that destroys its own window and then parks leaves a dead tile on
 * the strip that answers no click. This goes through the kernel's own close
 * path - the negotiator retired, app_close_win, the dock repainted, the
 * instance and every claim freed.
 *
 * IT RETURNS, AND YOUR WINDOW GOES A MOMENT LATER: the close is deferred to
 * the next UI pass, because the kernel is about to free the very segment you
 * are standing in. So CALL IT AND RETURN - do not draw afterwards, do not
 * call it twice, do not assume when it lands. The gfx lock may be held or
 * not, which means a menu command may call it directly; the tidiest place is
 * still a callback that draws nothing after it, and os88_onwake() is that.
 * Your close negotiator is NOT asked again - this call IS the answer. */
void os88_wm_close(void *win);

void os88_wm_content(void *win, struct os88_pt *origin);   /* content origin */
int  os88_wm_geom(void *win, struct os88_size *sz);        /* content size;
                                                  * -1 = not visible. Prefer
                                                  * this to arithmetic on the
                                                  * frame - it is the same
                                                  * subtraction done once */
int  os88_wm_obscured(void *win);                /* 1 = covered or hidden */
void os88_wm_title(void *win, const char *s);    /* redraws the caption strip
                                                  * and NOTHING else: 17 rows.
                                                  * The bytes must outlive it */
void os88_wm_sizable(void *win, int on);         /* then lay out from the LIVE
                                                  * geometry every paint */
void os88_wm_snap(void *win, int on);            /* keep my content origin on
                                                  * a multiple of 8 (11.94) */
void os88_wm_keeph(void *win, int on);           /* my layout is FIXED: hang
                                                  * me over the dock rather
                                                  * than shorten me (11.93) */
void os88_wm_ownbg(void *win, int on);           /* I paint every pixel, skip
                                                  * the kernel's white fill */
void os88_wm_saveu(void *win, int on);           /* a PROMISE: my content does
                                                  * not change while I am not
                                                  * drawing (11.96.1) */
void os88_wm_grow(void *win);                    /* a resizable window's own
                                                  * repaint must END with this
                                                  * - the white fill erased
                                                  * the grow box */
void os88_wm_resize(void *win, int w, int h);    /* do NOT hold the lock */

/* os88_wm_minsize - "below this my layout stops working; do not cut me past
 * it, even to fit a screen" (11.100.2). w and h are the OUTER window, frame
 * included; 0, 0 withdraws it. It registers AND applies, so the place to call
 * it is right after os88_wm_create() in os88_main(); without one you get the
 * system floor. Every path that REDUCES a size honours it - the create-time
 * fit, an adapter change, a drag onto a smaller display, the grow box and
 * os88_wm_resize(). It is capped at the display's own extent, so it can never
 * cost you your title bar; what it CAN do is leave you hanging over the dock,
 * and content below the display's last row is drawn nowhere and takes no
 * clicks. Ask for what your layout needs, not for what you would like - and
 * read os88_wm_geom() after an os88_onresize() rather than assuming the box
 * you asked for is the box you got. */
void os88_wm_minsize(void *win, int w, int h);
void os88_wm_cursor(void *win, int shape);       /* OS88_CUR_*, over your
                                                  * content only (7.2) */

/* os88_wm_damage - called from inside your own os88_paint(). Returns 1 and
 * fills r with your whole content, or 0 and fills r with the part that
 * actually needs drawing - which may be EMPTY (x1 > x2), meaning draw
 * nothing. An empty rect is safe to hand any primitive. Requires
 * os88_wm_ownbg(win, 1): without it the kernel has already whitened your
 * content and the only correct answer is "whole" (11.90.2). */
int os88_wm_damage(void *win, struct os88_rect *r);

/* The clip region (SPEC.md 11.3) - what a background painter uses instead of
 * a visibility veto. Arm it under the lock and every drawing call you make is
 * cut to the part of YOUR content nothing is covering. It dies at your next
 * os88_gfx_unlock(), so it is one call per lock hold and there is nothing to
 * undo. clip_set returns -1 when not one pixel shows: draw nothing. */
int  os88_wm_clip_set(void *win);
void os88_wm_clip_clear(void);
int  os88_wm_clip_test(int x1, int y1, int x2, int y2);   /* 0 = drawable */

void *os88_wm_top(void);                         /* frontmost VISIBLE window:
                                                  * "do I have focus?" */
void *os88_menu_owner(void);                     /* who owns the bar, 0 =
                                                  * Locator: "am I the active
                                                  * APPLICATION?" - different
                                                  * from wm_top exactly when
                                                  * the user clicked the bare
                                                  * desktop (44.8) */

/* --- reading the window record (SPEC.md 11, 20.5) ------------------------
 * The record lives in KERNEL_SEG and C cannot write a segment override, so
 * these six do it. They load KERNEL_SEG themselves rather than trusting the
 * ES a callback was entered with, which makes them correct from a worker and
 * after any other call. Read-only by contract: change geometry with
 * os88_wm_resize(), never by writing the record. */
int os88_win_flags(void *win);
int os88_win_visible(void *win);                 /* flags bit 1 */
int os88_win_x(void *win);
int os88_win_y(void *win);
int os88_win_w(void *win);                       /* FRAME width, not content */
int os88_win_h(void *win);

/* --- menus and the app's own name (SPEC.md 12.2) -------------------------- */

/* os88_menu_set - register your set, from os88_main(), with the window
 * os88_wm_create() just returned. It WRITES set->oncmd (see the struct) and
 * needs CC_HAS_MENUS in the shim. Pass 0 to detach. Your commands arrive at
 * os88_oncmd() on the UI task under the gfx lock, exactly like a click: you
 * may draw and call the file slots, you must not take the lock, and the
 * kernel does not repaint for you afterwards. */
void os88_menu_set(void *win, struct os88_menuset *set);

/* os88_about_set - add 'About <Name>' above the 'Close' the kernel already
 * puts in your name's pull-down. Needs CC_HAS_ABOUT. */
void os88_about_set(void *win);

/* Install the two optional window callbacks. Call from os88_main() after
 * os88_wm_create(); they are not template words. Each needs its %define. */
void os88_wm_onmouseup(void *win);               /* 13.7 - the RELEASE half of
                                                  * a content click. It
                                                  * arrives even outside your
                                                  * window, so range-test it */
void os88_wm_onresize(void *win);                /* 11.98 - your content box
                                                  * CHANGED and you did not
                                                  * ask (an adapter change).
                                                  * You must NOT draw in it */
void os88_wm_ontimer(void *win);                 /* install the handler
                                                  * above, after wm_create */
int  os88_wm_timer(void *win, int ticks);        /* ...and arm it: ticks from
                                                  * now at 18.2 Hz, 0 cancels.
                                                  * 0 = armed, -1 = REFUSED
                                                  * (kern_small carries the
                                                  * slot and not the body) -
                                                  * TEST IT and have a second
                                                  * path (SPEC.md 13.8.2) */
/* os88_wm_onclose - install the close negotiator above (SPEC.md 75.1). Call
 * it once from os88_main(), after os88_wm_create(), the way os88_wm_onwake()
 * is called. A side table, not a template word: the kernel clears it when the
 * window dies. Needs CC_HAS_ONCLOSE. */
void os88_wm_onclose(void *win);

void os88_wm_onwake(void *win);                  /* 74.1 - install
                                                  * os88_onwake(); needs
                                                  * CC_HAS_ONWAKE */

/* os88_wm_wake - post the kick (SPEC.md 74.1): os88_onwake() runs on the UI
 * task, lock free, a few event-loop passes later. Any context - a callback,
 * the handler itself, a worker; no lock needed. Returns 0 when a wake is
 * queued for this window (posted now, or one was ALREADY waiting - the kernel
 * keeps at most one per window, so kicking from every callback is free and
 * cannot fill the 16-record event ring), -1 when the ring was full of other
 * events and nothing was posted: kick again from your next callback.
 * Needs CC_HAS_ONWAKE, like the installer: a wake with no handler dispatches
 * to nothing, so a package without one does not carry the thunk. */
int os88_wm_wake(void *win);

/* os88_fullscreen - SPEC.md 11.2's latch: your window's frame becomes the
 * whole screen, menu bar and dock included, until you let go. Call it with
 * the gfx lock held (a callback holds it). enter != 0 takes the screen and
 * returns -1 if another window already has it; 0 gives it back. The kernel
 * repaints you whole either way. Exiting is YOUR job - the bar is unreachable
 * meanwhile - and 11.2.1's F/Esc is the convention; a terminal that owns both
 * keys states its own chord (74.2). NOT the exclusive bracket of SPEC.md 53. */
int os88_fullscreen(void *win, int enter);

/* --- tasks and time (SPEC.md 8, 20.6) ------------------------------------- */
void os88_task_yield(void);
void os88_task_sleep(int ticks);                 /* 18 ticks ~ 1 second */
unsigned os88_ticks(void);
unsigned os88_boot_ticks(void);                  /* 0xFFFF = never stamped */

/* os88_task_spawn - start os88_worker(). Call it from a callback (the gfx
 * lock HELD - that is required, not allowed), never from os88_main(). Returns
 * 0, or -1 refused: you already have a worker, or the 12-slot task table is
 * full. A refusal is NORMAL and transient - degrade and retry later, do not
 * abort. Needs CC_HAS_WORKER. */
int os88_task_spawn(void *win);

/* os88_task_alive - from YOUR WORKER only, with the gfx lock NOT held (it is
 * not reentrant: calling this while holding it deadlocks the machine). It
 * NEVER RETURNS if your close box was clicked - the kernel tears the instance
 * down inside this call. */
void os88_task_alive(void *win);

/* --- input, video, the machine ------------------------------------------- */
void os88_mouse(struct os88_mouse *m);
/* os88_key_down - is that make scancode down RIGHT NOW (SPEC.md 9.7)? 1 down,
 * 0 up. os88_onkey delivers PRESSES only, so this is the only way to model a
 * key as a LEVEL - a held cursor key, a joystick keyset, Ctrl+digit.
 * ASKING IS WHAT ARMS IT: the first call clears and arms the kernel's map and
 * always answers 0. Ask it ONCE from os88_main() and ignore that answer;
 * arming it from a callback erases the make os88_onkey has already seen.
 * It is ADVICE, NOT AN ORACLE: a break code the ISR missed leaves that key
 * reading down until it is pressed again. Bound what a "yes" makes you do. */
int  os88_key_down(int scan);

int  os88_evq_pending(void);                     /* events queued BEHIND the
                                                  * one being dispatched (13.4)
                                                  * - "am I about to be asked
                                                  * to do this again?" */
void os88_video(struct os88_video *v);           /* ASK THIS, do not assume
                                                  * 640x480: two adapters of
                                                  * three are something else */
int  os88_cpu(void);                             /* OS88_CPU_* - a fact to
                                                  * test, not a guess (73.11) */
void os88_srand(int seed);
int  os88_rand(void);

/* --- sound, the resident half only (SPEC.md 34) --------------------------- */
int os88_snd_caps(void);                         /* SND_CAP_* bits */
int os88_snd_tone(int hz, int ticks, int prio);  /* hz 0 = off, ticks 0 = until
                                                  * off, prio 0x40 is the
                                                  * default. -1 = refused.
                                                  * Worker-safe; ask for a
                                                  * DURATION and it silences
                                                  * itself */

/* --- files (SPEC.md 18.4, 19) --------------------------------------------
 * UI-TASK CONTEXT ONLY: os88_main(), a window callback, a menu command or
 * os88_onfile(). NEVER from your worker - the kernel cannot enforce it and a
 * second concurrent caller corrupts the FAT. A write stalls every painter for
 * its duration, so never from os88_paint().
 *
 * Names resolve in YOUR INSTANCE's current directory (19.2.1) - the folder
 * you were launched from until a dialog moves you - so a Save that means
 * "where my Save As went" is just a Save.
 *
 * The 32-bit count the slots take is capped at 16 bits here, which costs
 * nothing a package can reach: your image and bss together cap at 60KB, and
 * anything bigger belongs in a claim, which the _seg forms read into.
 * A _seg base MUST BE 512-BYTE ALIGNED (SPEC.md 2.1.1: every disk-visible
 * base is): a claim's own base is (the kernel's guard 6b - KB-granular
 * blocks from a 512-aligned heap), and so is any offset into it that is a
 * multiple of 0x200 (seg + 0x20, + 0x40, ...) - but seg + 0x10 is NOT, and
 * int 13h answers a run that then straddles a 64KB physical page with
 * error 09h on real hardware, which QEMU never shows. Read to an aligned
 * offset and move the bytes yourself if they must sit elsewhere (SPEC.md
 * 71's loader is the worked example). A DS-relative buffer (the non-_seg
 * forms) is copied through the kernel and has no such rule.
 * Every one of these sets os88_ferr(). */
unsigned os88_file_read(const char *name, void *buf, unsigned cap);
unsigned os88_file_read_seg(const char *name, unsigned seg, unsigned cap);
int os88_file_write(const char *name, const void *buf, unsigned count);
int os88_file_write_seg(const char *name, unsigned seg, unsigned count);
int os88_file_append(const char *name, const void *buf, unsigned count);

/* os88_file_read_at - ONE CHUNK of a file, by byte offset (18.4.4), and the
 * read half of the pair whose write half is os88_file_append(). This is how a
 * package looks at a file it could never hold: os88_file_read() refuses a
 * short buffer with FERR_BIG and reads nothing, so "read the header, then
 * decide whether to read the rest" cannot be said with it.
 *
 * The offset is TWO WORDS because it is genuinely 32 bits and there is no
 * such type here (rule 4) - the same shape struct os88_find's size_lo /
 * size_hi and os88_onfile()'s size arguments already use. Reading a header is
 * os88_file_read_at(name, buf, cluster_bytes, 0, 0).
 *
 * The ANSWER is one word and exact: it is bounded by cap, which is an
 * unsigned. 0 means either "at or past the end" - how the loop terminates -
 * or a refusal, and os88_ferr() is what tells them apart.
 *
 * THE OFFSET AND THE CAPACITY MUST EACH BE A WHOLE NUMBER OF CLUSTERS
 * (os88_disk_cluster_sectors() * 512) or you get FERR_NAME - refused, never
 * mis-read. The final chunk is the exception the arithmetic makes for itself:
 * a tail shorter than a cluster is delivered whole and the next call answers
 * 0. It is STATELESS - the offset is the whole argument - which is what lets
 * you write between two reads. */
unsigned os88_file_read_at(const char *name, void *buf, unsigned cap,
                           unsigned off_lo, unsigned off_hi);
int os88_file_delete(const char *name);
int os88_file_rename(const char *oldname, const char *newname);
int os88_file_mkdir(const char *name);

/* os88_ferr - the FERR_* of the last file call, 0 if it succeeded. It is one
 * word of the runtime's bss and every file thunk writes it, so read it before
 * the next call. os88_file_read() answers 0 bytes both for an empty file and
 * for a failure; this is what tells them apart. */
int os88_ferr(void);

/* os88_file_find - by ORDINAL, no cursor anywhere (19.7.1): ask for 0, then
 * for what it returns. -1 = the end. Every step between two of these - a
 * read, a write - walks directories itself, which is exactly why there is no
 * find-first/find-next state to be destroyed. */
int os88_file_find(int ordinal, struct os88_find *f);

unsigned os88_disk_free_kb(void);                /* the 32-bit answer, shifted
                                                  * into KB in assembly */
int os88_disk_cluster_sectors(void);             /* chunk sizes are multiples
                                                  * of this - at BOTH ends of
                                                  * a copy (18.4.4) */

void os88_file_here(struct os88_place *p);       /* bank where you stand... */
int  os88_file_goto(struct os88_place *p);       /* ...and come back. A
                                                  * REMOUNT: real floppy I/O */

/* os88_file_goto_q_mark - stand in another folder QUIETLY, and STAY there
 * (SPEC.md 74.1, 19.2.2). Inside the volume you are on it is a word - no disk
 * I/O - and across volumes a quiet mount that skips the listing, the sort and
 * the icon harvest; and unlike OSAPI_FILE_GOTO_Q (deliberately not wrapped)
 * your instance's own folder moves with it, so the very next os88_file_*
 * resolves THERE rather than being re-stood where you were launched. A CP/M
 * drive is a folder, and this is how a drive switch costs one word. 0 moved,
 * -1 refused (os88_ferr() says why). It does not fill any Disk window's
 * listing, so it is not the one to call before showing a folder. */
int  os88_file_goto_q_mark(unsigned clus, int vol);
int  os88_vol_sys(void);                         /* the volume this machine
                                                  * BOOTED from */

/* os88_file_dlg - the Standard File dialog (SPEC.md 38). It does NOT block:
 * it returns with the dialog on screen and calls os88_onfile() much later.
 * Cancel calls nothing at all. Call it from a menu command or a key handler,
 * never from os88_paint(). Returns -1 refused (one is already up).
 * defname may be 0. Needs CC_HAS_FDLG.
 *
 * USE THE SIZE os88_onfile() IS GIVEN TO REFUSE BEFORE YOU TOUCH THE DISK.
 * It is free - the dialog read it out of the mount snapshot - and refusing
 * after a read is not: 116KB off a floppy is about ten seconds of motor, and
 * the user cannot tell that from a load that works. */
int os88_file_dlg(int mode, void *win, const char *defname);

/* os88_arg_file - the document this instance was launched to open (54.5).
 * Copies the name into your 13-byte buffer and fills p with the folder it
 * lives in, which is exactly what os88_file_goto() takes. Returns 0, or -1 if
 * this instance was launched empty (the ordinary case). READ-AND-CLEAR: the
 * first caller gets it. */
int os88_arg_file(char *name13, struct os88_place *p);

/* os88_assoc_set - claim an extension for this program (54.5), from
 * os88_main(). ext is up to 3 characters and stem up to 8, both uppercase;
 * this pads them. Your icon becomes the mark its documents carry. */
int os88_assoc_set(const char *ext, const char *stem);

/* The batch bracket (18.9.3): between these two a floppy may reuse its banked
 * BPB instead of re-reading LBA 0 at every volume switch - one call and one
 * revolution per switch, on a copy or an install. It nests, and any
 * os88_gfx_unlock() ends it, so it cannot be left open across a moment the
 * user could reach the drive. */
void os88_batch_begin(void);
void os88_batch_end(void);

/* --- memory beyond your region (SPEC.md 50.3) ----------------------------
 * Your image and bss together cap at 60KB (APP_MAX_SIZE), and a C package
 * meets that two to four times sooner than an assembly one. Anything bigger
 * is a claim: os88_mem_claim() answers a SEGMENT, which the _seg file calls
 * take and os88_peek()/os88_poke() reach a byte at a time. The kernel frees
 * your claims when your instance dies, so there is no teardown to write.
 * Size yourself from os88_mem_largest_kb(), never from int 12h. */
unsigned os88_mem_claim(int kb);                 /* 0 = refused */
int      os88_mem_free(unsigned seg);
unsigned os88_mem_regrow(unsigned seg, int kb);  /* 0 = refused and the old
                                                  * claim is untouched;
                                                  * otherwise the base NOW,
                                                  * which may have MOVED */
unsigned os88_mem_largest_kb(void);
unsigned os88_mem_total_kb(void);

/* Reach into a claim. Two far-segment accessors, because C cannot say `es:`.
 * A byte at a time is ~11 us of near call each: fine for a header, wrong for
 * a copy. Move bulk with os88_file_read_seg() or by claiming, then blitting
 * out of it with os88_gfx_blit4(), which takes a segment of its own. */
int  os88_peek(unsigned seg, unsigned off);
void os88_poke(unsigned seg, unsigned off, int value);

/* --- the system clipboard (SPEC.md 55) -----------------------------------
 * One text buffer for the whole machine, outliving the app that filled it.
 * All three are legal from a worker as well as a callback. */
int os88_clip_put(const void *text, unsigned len);   /* len 0 EMPTIES it */
int os88_clip_get(void *buf, unsigned cap);          /* bytes copied; ask
                                                      * os88_clip_size() first
                                                      * and grow your buffer */
/* ...and the _seg forms, which os88_file_read_seg is the model for: the text
 * lives in a HEAP CLAIM rather than in your own segment, so a 1KB or 2KB
 * clipboard staging area costs you a claim while the command runs instead of
 * bss for the life of the app. Claim, fill (os88_poke or a mover of your
 * own), put, free. A _seg base needs no alignment - this is memory, not a
 * disk transfer. */
int os88_clip_put_seg(unsigned seg, unsigned off, unsigned len);
int os88_clip_get_seg(unsigned seg, unsigned off, unsigned cap);

int os88_clip_size(void);                            /* -1 = empty */

/* --- a message that costs your window no pixels (SPEC.md 59) -------------
 * An inverse-video strip at the right end of the menu bar, taken down on its
 * own. ticks 0 = about 3 seconds; an EMPTY string retires whatever is up. The
 * text is COPIED, so it may be scratch. Any context, worker included. */
int os88_toast(const char *text, int ticks);

/* --- the runtime's own helpers -------------------------------------------
 * There is no C library here (SPEC.md 73.9: no printf family - it is
 * thousands of bytes for the one conversion a program wanted). These six are
 * hand-written in apps/cc/os88thunk.asm, are DS-only - no string instruction
 * and therefore no ES - and are all a package has needed so far. */
void os88_memset(void *p, int c, unsigned n);
void os88_memcpy(void *dst, const void *src, unsigned n);
unsigned os88_strlen(const char *s);
void os88_strcpy(char *dst, const char *src, unsigned cap);  /* always
                                                  * NUL-terminates; cap counts
                                                  * the NUL */
char *os88_utoa(unsigned v, char *dst6);         /* returns dst */
char *os88_itoa(int v, char *dst7);              /* returns dst. Note the
                                                  * compiler's own edge: the
                                                  * literal -32768 does not
                                                  * parse ("Constant too big
                                                  * for 16-bit signed type"),
                                                  * because it is read as a
                                                  * negation of 32768. Write
                                                  * -32767 - 1. The RUNTIME
                                                  * value is fine and prints
                                                  * correctly */

#endif /* OS88_H */
