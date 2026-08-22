/* ============================================================================
 * os8088 - apps/c64/c64kbd.c        the keyboard map and the level model
 *
 * Part of C64 (C64-SPEC §7). Derived from VICE 3.10, Copyright (C)
 * 1996-2025 the VICE team, GPL-2-or-later - see apps/c64/COPYING. The map is
 * data/C64/gtk3_sym.vkm transcribed; the level model is src/keyboard.c's
 * (keyboard_key_pressed / keyboard_key_released feeding the matrix that
 * src/c64/c64cia1.c reads). Nothing of VICE's source is vendored.
 *
 * #included into apps/c64/c64.c - ONE translation unit (SPEC.md 73.1).
 *
 * ----------------------------------------------------------------------------
 * A LEVEL MODEL, POLLED ONCE PER WAKE (7.2)
 * ----------------------------------------------------------------------------
 * VICE's keyboard.c is a LEVEL model - a key is in the matrix while it is
 * down and leaves it when it comes up - and so is this one. os88_onkey
 * delivers PRESSES only, so the state comes from OSAPI_KEY_DOWN, and four
 * rules exist because getting each of them wrong is silent:
 *
 *   1. the map is armed ONCE, in os88_main (its first call clears it);
 *   2. host key state is polled ONCE PER WAKE and cached, so every emulated
 *      CIA1 read in that wake reads the same matrix and the cost does not
 *      multiply by the number of slices;
 *   3. the matrix is REBUILT from the whole down-list, never cleared
 *      incrementally - several mappings share the synthetic SHIFT, CTRL and
 *      C= bits, so clearing one key's bits can clear another key's;
 *   4. a fresh press is guaranteed C64K_FRESH_CYC = 20,000 EMULATED CYCLES
 *      in the matrix - one CIA1 timer A period, so a KERNAL scan cannot miss
 *      it - before the release poll can take it out. It is counted in
 *      emulated cycles and NOT in wakes (§7.2): a wall slice is 256..16,384
 *      cycles, which at the floor is 1/64 of a scan.
 *
 * There is NO INVENTED HOLD TIME, which is what makes RUN/STOP+RESTORE work,
 * makes a game reading $DC01 work, and leaves the repeat rate to the KERNAL.
 *
 * ----------------------------------------------------------------------------
 * THE FLAGS, RE-ENCODED INTO ONE BYTE
 * ----------------------------------------------------------------------------
 * gtk3_sym.vkm's shiftflag is a 16-bit mask (its own header lists the
 * sixteen bits). The five that a C64 map actually uses are re-encoded here
 * into one byte, so the table is four bytes an entry and not six
 * (C64-SPEC §13.2 budgets 608 bytes for it):
 *
 *   .vkm                            here
 *   0x0001 combined with shift      C64K_SH
 *   0x0008 can be shifted or not    C64K_OPT
 *   0x0010 deshift                  C64K_DESH
 *   0x0800 combined with cbm        C64K_CBM
 *   0x0002 / 0x0004 IS a shift key  C64K_ISSH
 *   0x0040 IS shift-lock            C64K_ISLOCK
 *   0x2000 IS the (left) cbm key    C64K_ISCBM
 *   0x4000 IS the (left) ctrl key   C64K_ISCTRL
 *
 * The re-encoding is stated because it is the one place this port does not
 * carry a VICE table byte for byte, and the reason is a byte of image per
 * entry.
 * ==========================================================================*/

#define C64K_SH     0x01
#define C64K_OPT    0x02
#define C64K_DESH   0x04
#define C64K_CBM    0x08
#define C64K_ISSH   0x10
#define C64K_ISLOCK 0x20
#define C64K_ISCBM  0x40
#define C64K_ISCTRL 0x80
#define C64K_NONE   0xFF                    /* no mapping for this key */

/* The four positions the .vkm names with !LSHIFT / !RSHIFT / !LCBM / !LCTRL.
 * Several mappings share these synthetic bits, which is exactly why 7.2's
 * rule 3 says the matrix is REBUILT from the whole down-list every wake and
 * never cleared incrementally. */
#define C64K_LSHIFT_R 1
#define C64K_LSHIFT_C 7
#define C64K_RSHIFT_R 6
#define C64K_RSHIFT_C 4
#define C64K_LCBM_R   7
#define C64K_LCBM_C   5
#define C64K_LCTRL_R  7
#define C64K_LCTRL_C  2

/* --- the PC scan codes this port names (set 1 make codes) ---------------- */
#define KSC_ESC    0x01
#define KSC_BKSP   0x0E
#define KSC_TAB    0x0F
#define KSC_ENTER  0x1C
#define KSC_CTRL   0x1D
#define KSC_LSHIFT 0x2A
#define KSC_RSHIFT 0x36
#define KSC_ALT    0x38
#define KSC_SPACE  0x39
#define KSC_CAPS   0x3A
#define KSC_F1     0x3B
#define KSC_F2     0x3C
#define KSC_F3     0x3D
#define KSC_F4     0x3E
#define KSC_F5     0x3F
#define KSC_F6     0x40
#define KSC_F7     0x41
#define KSC_F8     0x42
#define KSC_SCROLL 0x46
#define KSC_HOME   0x47
#define KSC_UP     0x48
#define KSC_PGUP   0x49
#define KSC_LEFT   0x4B
#define KSC_KP5    0x4C
#define KSC_RIGHT  0x4D
#define KSC_END    0x4F
#define KSC_DOWN   0x50
#define KSC_PGDN   0x51
#define KSC_INS    0x52
#define KSC_DEL    0x53
#define KSC_CTRLH  0x23                     /* 7.3: the BIOS folds these three */
#define KSC_CTRLI  0x17                     /*      onto BS, Tab and CR, so */
#define KSC_CTRLM  0x32                     /*      they are routed on SCAN */

/* ==========================================================================
 * THE MAP, keyed by ASCII 32..126 (7.1)
 *
 * Four bytes an entry - row, column, flags, and one byte of padding that
 * makes the stride a power of two, so an index is a shift and never an
 * `imul` the gate would have to find a scratch register for (LESSONS.md 3).
 * A row of C64K_NONE is a key this map does not carry, which is what the
 * .vkm's silence about '{' and '}' means.
 * ========================================================================*/
#define C64K_ENT(r, c, f) (r), (c), (f), 0
#define C64K_NOENT        C64K_NONE, 0, 0, 0

static const unsigned char c64_kasc[95 * 4] = {
    C64K_ENT(7, 4, C64K_OPT),               /* 32 space    7 4 8   */
    C64K_ENT(7, 0, C64K_SH),                /* 33 !        7 0 1   */
    C64K_ENT(7, 3, C64K_SH),                /* 34 "        7 3 1   */
    C64K_ENT(1, 0, C64K_SH),                /* 35 #        1 0 1   */
    C64K_ENT(1, 3, C64K_SH),                /* 36 $        1 3 1   */
    C64K_ENT(2, 0, C64K_SH),                /* 37 %        2 0 1   */
    C64K_ENT(2, 3, C64K_SH),                /* 38 &        2 3 1   */
    C64K_ENT(3, 0, C64K_SH),                /* 39 '        3 0 1   */
    C64K_ENT(3, 3, C64K_SH),                /* 40 (        3 3 1   */
    C64K_ENT(4, 0, C64K_SH),                /* 41 )        4 0 1   */
    C64K_ENT(6, 1, C64K_DESH),              /* 42 *        6 1 16  */
    C64K_ENT(5, 0, C64K_DESH),              /* 43 +        5 0 16  */
    C64K_ENT(5, 7, 0),                      /* 44 ,        5 7 0   */
    C64K_ENT(5, 3, 0),                      /* 45 -        5 3 0   */
    C64K_ENT(5, 4, 0),                      /* 46 .        5 4 0   */
    C64K_ENT(6, 7, 0),                      /* 47 /        6 7 0   */
    C64K_ENT(4, 3, 0),                      /* 48 0        4 3 0   */
    C64K_ENT(7, 0, 0),                      /* 49 1        7 0 0   */
    C64K_ENT(7, 3, 0),                      /* 50 2        7 3 0   */
    C64K_ENT(1, 0, 0),                      /* 51 3        1 0 0   */
    C64K_ENT(1, 3, 0),                      /* 52 4        1 3 0   */
    C64K_ENT(2, 0, 0),                      /* 53 5        2 0 0   */
    C64K_ENT(2, 3, 0),                      /* 54 6        2 3 0   */
    C64K_ENT(3, 0, 0),                      /* 55 7        3 0 0   */
    C64K_ENT(3, 3, 0),                      /* 56 8        3 3 0   */
    C64K_ENT(4, 0, 0),                      /* 57 9        4 0 0   */
    C64K_ENT(5, 5, C64K_DESH),              /* 58 :        5 5 16  */
    C64K_ENT(6, 2, 0),                      /* 59 ;        6 2 0   */
    C64K_ENT(5, 7, C64K_SH),                /* 60 <        5 7 1   */
    C64K_ENT(6, 5, 0),                      /* 61 =        6 5 0   */
    C64K_ENT(5, 4, C64K_SH),                /* 62 >        5 4 1   */
    C64K_ENT(6, 7, C64K_SH),                /* 63 ?        6 7 1   */
    C64K_ENT(5, 6, C64K_DESH),              /* 64 @        5 6 16  */
    C64K_ENT(1, 2, C64K_OPT),               /* 65 A        1 2 8   */
    C64K_ENT(3, 4, C64K_OPT),               /* 66 B        3 4 8   */
    C64K_ENT(2, 4, C64K_OPT),               /* 67 C        2 4 8   */
    C64K_ENT(2, 2, C64K_OPT),               /* 68 D        2 2 8   */
    C64K_ENT(1, 6, C64K_OPT),               /* 69 E        1 6 8   */
    C64K_ENT(2, 5, C64K_OPT),               /* 70 F        2 5 8   */
    C64K_ENT(3, 2, C64K_OPT),               /* 71 G        3 2 8   */
    C64K_ENT(3, 5, C64K_OPT),               /* 72 H        3 5 8   */
    C64K_ENT(4, 1, C64K_OPT),               /* 73 I        4 1 8   */
    C64K_ENT(4, 2, C64K_OPT),               /* 74 J        4 2 8   */
    C64K_ENT(4, 5, C64K_OPT),               /* 75 K        4 5 8   */
    C64K_ENT(5, 2, C64K_OPT),               /* 76 L        5 2 8   */
    C64K_ENT(4, 4, C64K_OPT),               /* 77 M        4 4 8   */
    C64K_ENT(4, 7, C64K_OPT),               /* 78 N        4 7 8   */
    C64K_ENT(4, 6, C64K_OPT),               /* 79 O        4 6 8   */
    C64K_ENT(5, 1, C64K_OPT),               /* 80 P        5 1 8   */
    C64K_ENT(7, 6, C64K_OPT),               /* 81 Q        7 6 8   */
    C64K_ENT(2, 1, C64K_OPT),               /* 82 R        2 1 8   */
    C64K_ENT(1, 5, C64K_OPT),               /* 83 S        1 5 8   */
    C64K_ENT(2, 6, C64K_OPT),               /* 84 T        2 6 8   */
    C64K_ENT(3, 6, C64K_OPT),               /* 85 U        3 6 8   */
    C64K_ENT(3, 7, C64K_OPT),               /* 86 V        3 7 8   */
    C64K_ENT(1, 1, C64K_OPT),               /* 87 W        1 1 8   */
    C64K_ENT(2, 7, C64K_OPT),               /* 88 X        2 7 8   */
    C64K_ENT(3, 1, C64K_OPT),               /* 89 Y        3 1 8   */
    C64K_ENT(1, 4, C64K_OPT),               /* 90 Z        1 4 8   */
    C64K_ENT(5, 5, C64K_SH),                /* 91 [        5 5 1   */
    C64K_ENT(6, 0, 0),                      /* 92 \        6 0 0   - Pound */
    C64K_ENT(6, 2, C64K_SH),                /* 93 ]        6 2 1   */
    C64K_ENT(6, 6, C64K_DESH),              /* 94 ^        6 6 16  */
    C64K_ENT(5, 6, C64K_CBM | C64K_DESH),   /* 95 _        5 6 0x810 */
    C64K_ENT(3, 0, C64K_SH),                /* 96 `        3 0 1   */
    C64K_ENT(1, 2, C64K_OPT),               /* 97 a */
    C64K_ENT(3, 4, C64K_OPT),               /* 98 b */
    C64K_ENT(2, 4, C64K_OPT),               /* 99 c */
    C64K_ENT(2, 2, C64K_OPT),               /* 100 d */
    C64K_ENT(1, 6, C64K_OPT),               /* 101 e */
    C64K_ENT(2, 5, C64K_OPT),               /* 102 f */
    C64K_ENT(3, 2, C64K_OPT),               /* 103 g */
    C64K_ENT(3, 5, C64K_OPT),               /* 104 h */
    C64K_ENT(4, 1, C64K_OPT),               /* 105 i */
    C64K_ENT(4, 2, C64K_OPT),               /* 106 j */
    C64K_ENT(4, 5, C64K_OPT),               /* 107 k */
    C64K_ENT(5, 2, C64K_OPT),               /* 108 l */
    C64K_ENT(4, 4, C64K_OPT),               /* 109 m */
    C64K_ENT(4, 7, C64K_OPT),               /* 110 n */
    C64K_ENT(4, 6, C64K_OPT),               /* 111 o */
    C64K_ENT(5, 1, C64K_OPT),               /* 112 p */
    C64K_ENT(7, 6, C64K_OPT),               /* 113 q */
    C64K_ENT(2, 1, C64K_OPT),               /* 114 r */
    C64K_ENT(1, 5, C64K_OPT),               /* 115 s */
    C64K_ENT(2, 6, C64K_OPT),               /* 116 t */
    C64K_ENT(3, 6, C64K_OPT),               /* 117 u */
    C64K_ENT(3, 7, C64K_OPT),               /* 118 v */
    C64K_ENT(1, 1, C64K_OPT),               /* 119 w */
    C64K_ENT(2, 7, C64K_OPT),               /* 120 x */
    C64K_ENT(3, 1, C64K_OPT),               /* 121 y */
    C64K_ENT(1, 4, C64K_OPT),               /* 122 z */
    C64K_NOENT,                             /* 123 {  - not in the .vkm */
    C64K_ENT(6, 0, C64K_OPT),               /* 124 |       6 0 8   */
    C64K_NOENT,                             /* 125 }  - not in the .vkm */
    C64K_ENT(7, 1, C64K_DESH)                /* 126 ~       7 1 16  */
};

/* ==========================================================================
 * ...and by SCAN CODE for everything else (7.1, 7.3)
 *
 * Four bytes an entry - scan, row, col, flags - terminated by a zero scan.
 * Tab is C= (7 5 8200) and Escape is RUN/STOP (7 7 8) STRAIGHT OUT OF THE
 * .vkm, and the three the BIOS folds are routed here on their scan codes so
 * Ctrl+H, Ctrl+I and Ctrl+M are not lost to BS, Tab and CR (7.3).
 *
 * Page_Up is `-3 0` in the .vkm - the first RESTORE key - and is not a matrix
 * position at all: it is the NMI of 7.4, and it is marked with row 0xFE so
 * the wave-2 poll can tell it apart from a mapping.
 * ========================================================================*/
#define C64K_RESTORE 0xFE

static const unsigned char c64_kscan[] = {
    KSC_BKSP,   0, 0, C64K_OPT,             /* BackSpace  0 0 8  INST/DEL */
    KSC_DEL,    0, 0, C64K_OPT,             /* Delete     0 0 8 */
    KSC_INS,    0, 0, C64K_SH,              /* Insert     0 0 1 */
    KSC_ENTER,  0, 1, C64K_OPT,             /* Return     0 1 8 */
    KSC_RIGHT,  0, 2, C64K_OPT,             /* Right      0 2 8 */
    KSC_LEFT,   0, 2, C64K_SH,              /* Left       0 2 1 */
    KSC_DOWN,   0, 7, C64K_OPT,             /* Down       0 7 8 */
    KSC_UP,     0, 7, C64K_SH,              /* Up         0 7 1 */
    KSC_F1,     0, 4, C64K_OPT,             /* F1         0 4 8 */
    KSC_F2,     0, 4, C64K_SH,              /* F2         0 4 1 */
    KSC_F3,     0, 5, C64K_OPT,             /* F3         0 5 8 */
    KSC_F4,     0, 5, C64K_SH,              /* F4         0 5 1 */
    KSC_F5,     0, 6, C64K_OPT,             /* F5         0 6 8 */
    KSC_F6,     0, 6, C64K_SH,              /* F6         0 6 1 */
    KSC_F7,     0, 3, C64K_OPT,             /* F7         0 3 8 */
    KSC_F8,     0, 3, C64K_SH,              /* F8         0 3 1 */
    KSC_HOME,   6, 3, C64K_OPT,             /* Home       6 3 8  CLR/HOME */
    KSC_PGDN,   6, 6, C64K_OPT,             /* Page_Down  6 6 8  up-arrow */
    KSC_END,    7, 1, 0,                    /* End        7 1 0  left-arrow */
    KSC_ESC,    7, 7, C64K_OPT,             /* Escape     7 7 8  RUN/STOP */
    KSC_TAB,    C64K_LCBM_R, C64K_LCBM_C, C64K_ISCBM,   /* Tab 7 5 8200 C= */
    KSC_LSHIFT, C64K_LSHIFT_R, C64K_LSHIFT_C, C64K_ISSH,/* Shift_L 1 7 2 */
    KSC_RSHIFT, C64K_RSHIFT_R, C64K_RSHIFT_C, C64K_ISSH,/* Shift_R 6 4 4 */
    KSC_CAPS,   C64K_LSHIFT_R, C64K_LSHIFT_C, C64K_ISLOCK, /* Caps_Lock 1 7 64 */
    KSC_CTRL,   C64K_LCTRL_R, C64K_LCTRL_C, C64K_ISCTRL,/* Control_L 7 2 16392 */
    KSC_PGUP,   C64K_RESTORE, 0, 0,         /* Page_Up   -3 0    RESTORE */
    0, 0, 0, 0
};

/* ==========================================================================
 * THE CACHED MATRIX AND THE DOWN-LIST (7.2)
 *
 * The matrix is what an emulated CIA1 read sees, and it is REBUILT from the
 * whole down-list once per wake - never cleared incrementally, because
 * several mappings share the synthetic SHIFT, CTRL and C= bits and clearing
 * one key's bits can clear another key's. The down-list is 16 entries and its
 * overflow path is bounded: the 17th simultaneous key is DROPPED, not written
 * past the end.
 * ========================================================================*/
#define C64K_DOWNMAX 16

static unsigned char c64_matrix[8];         /* row -> the eight column bits */
static unsigned char c64_down[C64K_DOWNMAX];/* the scan codes still held */
static unsigned char c64_dasc[C64K_DOWNMAX];/* ...and the ascii they arrived
                                             * with, because the map is keyed
                                             * by ascii for 32..126 (7.1) */
/* RULE 4, AND ITS UNIT IS EMULATED CYCLES - NOT WAKES.
 *
 * A fresh press is held in the matrix until the emulated machine has had time
 * to scan the keyboard once, and one wake is NOT that time. A wake is one
 * wall slice: 256..16,384 cycles (C64_SLICE_MIN/MAX), floor 256, against the
 * KERNAL's own matrix scan every CIA1 timer-A period, ~16,421 cycles. So "one
 * wake" was between 1/64 and 1 of a single scan, and a press the host
 * released before the next poll had at best a 1-in-64 chance of ever being
 * SEEN. Under QEMU the core runs at some thousands of per cent, so an
 * emulated jiffy is well under a millisecond and every press was scanned many
 * times over; on the 4.77 MHz target the core runs at a few per cent, a
 * 200 ms keypress buys a few thousand emulated cycles, and typing loses
 * characters at random. PERFORMANCE.md's third emulator-invisible defect -
 * input overrun - with the emulator-vs-target speed ratio as its cause.
 *
 * So the age is a THREE-state thing and the middle state is timed by the
 * emulated clock: 0 pressed this poll, 1 fresh (the stamp in c64_dcyc says
 * when), 2 aged and releasable. It is checked every poll and a poll never
 * runs more than C64_SLICE_MAX cycles, so the 16-bit stamp cannot lap. It is
 * BOUNDED on purpose: a program that never reads the matrix cannot make a key
 * stick, because the entry ages on the clock and not on being read. */
#define C64K_AGE_FRESH 1
#define C64K_AGE_OLD   2
#define C64K_FRESH_CYC 20000                /* one CIA1 TA period (16,421 at
                                             * the KERNAL's own $4025) plus
                                             * margin, in 6510 cycles */
static unsigned char c64_dage[C64K_DOWNMAX];
static unsigned c64_dcyc[C64K_DOWNMAX];     /* c64_cyc_lo when it was pressed */
static int c64_ndown;
#ifdef C64_HOST
static int c64_ndrop;                       /* presses the 16-entry list had
                                             * to drop - the bounded overflow
                                             * path, counted so the harness
                                             * can see it happen, and NOT in
                                             * the shipping image (c64scr.c's
                                             * note beside the cost counters) */
#endif
static int c64_joyswap;                     /* Alt+J, Swap joysticks (8) */
/* the two control ports' direction masks, in src/joyport/joystick.c's bit
 * order: 0 up, 1 down, 2 left, 3 right, 4 fire. The status row's two five-dot
 * indicators are these, and they are what c64_st_joy1/c64_st_joy2 delta
 * against. Port 1 is empty on this machine (C64-SPEC §8). */
static int c64_joy1, c64_joy2;
static int c64_shift_down;                  /* the HOST's shift keys, polled */
static int c64_ctrl_down;                   /* ...and its Ctrl */

/* the key ring: os88_onkey pushes and the slice driver's poll drains it into
 * the down-list, so a key pressed between two wakes is not eaten. */
#define C64K_RING 32
static unsigned char c64_ring_a[C64K_RING];
static unsigned char c64_ring_s[C64K_RING];
static int c64_ring_h, c64_ring_t;

static void c64_kbd_init(void)
{
    int i;
    for (i = 0; i < 8; i++)
        c64_matrix[i] = 0;
    c64_ndown = 0;
#ifdef C64_HOST
    c64_ndrop = 0;
#endif
    c64_ring_h = 0;
    c64_ring_t = 0;
    c64_joyswap = 0;
    c64_joy1 = 0;
    c64_joy2 = 0;
    c64_shift_down = 0;
    c64_ctrl_down = 0;
}

static void c64_key_push(int ascii, int scan)
{
    int n = (c64_ring_t + 1) & (C64K_RING - 1);
    if (n == c64_ring_h)
        return;                             /* the ring is full: the key is
                                             * dropped, not written past it */
    c64_ring_a[c64_ring_t] = (unsigned char)ascii;
    c64_ring_s[c64_ring_t] = (unsigned char)scan;
    c64_ring_t = n;
}

/* c64_kmap_asc / c64_kmap_scan - the two lookups, so the poll has one place
 * to ask and this file's table has one reader. They answer the entry's index
 * into its table, or -1. */
static int c64_kmap_asc(int ch)
{
    if (ch < 32 || ch > 126)
        return -1;
    if (c64_kasc[(ch - 32) * 4] == C64K_NONE)
        return -1;
    return (ch - 32) * 4;
}

static int c64_kmap_scan(int scan)
{
    int i = 0;
    while (c64_kscan[i] != 0) {
        if (c64_kscan[i] == (unsigned char)scan)
            return i;
        i += 4;
    }
    return -1;
}

/* ==========================================================================
 * THE FOLDS THE BIOS IMPOSES, RESOLVED ON SCAN (7.3)
 *
 * An AT BIOS int 16h AH=0 folds Ctrl+H, Ctrl+I and Ctrl+M onto BS, Tab and
 * CR. Three of the six pairs below are the C64 keys and three are the CTRL
 * combinations, and the SCAN CODE is the only thing that tells them apart.
 * CTRL+digit - the colour and reverse codes - does not come through here at
 * all: it comes from the once-per-wake poll while Ctrl is held (7.3), which
 * is what the level model buys and what keeps VICE's Alt+digit captions.
 * ========================================================================*/
static int c64_scan_is_ctrl(int ascii, int scan)
{
    if (ascii == 8 && scan == KSC_CTRLH)
        return 'H';
    if (ascii == 9 && scan == KSC_CTRLI)
        return 'I';
    if (ascii == 13 && scan == KSC_CTRLM)
        return 'M';
    if (ascii >= 1 && ascii <= 26 && ascii != 8 && ascii != 9 && ascii != 13)
        return ascii + 64;
    return 0;
}

/* CTRL+digit's ten scancodes, and the joystick keyset's five. Port 2 is the
 * cursor keys with Ctrl as fire - THIS PORT'S CHOICE, stated (C64-SPEC
 * §8), because a keyset is the only joystick source this machine has. */
static unsigned char c64_kdig_down[10];
static const unsigned char c64_kdigsc[10] = {
    0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A
};
static const unsigned char c64_joysc[5] = {
    KSC_UP, KSC_DOWN, KSC_LEFT, KSC_RIGHT, KSC_CTRL
};

/* THE KEYSET IS ON, AND ITS KEYS ARE CONSUMED. `KeySetEnable=1` is the
 * shipped state (joystick.c:553) because a keyset is the only joystick source
 * this machine has, and Preferences > Allow keyset joysticks is shown checked
 * and disabled (C64-SPEC §8). This is that resource, so the harness can turn
 * it off and show the keys reaching the matrix again - the negative control
 * for the rule below, and `joykeys_enable` is exactly the flag VICE tests
 * first in joystick_check_set (joystick.c:598-601). */
static int c64_keyset = 1;

/* c64_is_joykey - a key the keyset has taken, which therefore does NOT reach
 * the keyboard matrix.
 *
 * VICE'S keyboard_key_pressed (src/keyboard.c:788-798) walks the ports mapped
 * to NUMPAD/KEYSET1/KEYSET2, calls joystick_check_set for each, and RETURNS
 * before kbd_queue_pushkey when one takes the key: a keyset key drives the
 * stick and is not typed. This port shipped the joystick without that rule,
 * so the four cursor keys drove port 2 AND entered the matrix at the same
 * time - a game polling $DC00 for the stick also saw phantom cursor presses
 * out of $DC01, and moving the stick in BASIC walked the cursor.
 *
 * CTRL IS THE ONE DEPARTURE AND §8 ALREADY STATES IT: Ctrl is both fire and
 * the CTRL key, so it is not in the loop below. It cannot be in the down-list
 * anyway - a bare modifier raises no int 16h event (7.2) - and the CTRL+digit
 * and CTRL+letter paths need it in the matrix.
 *
 * It is applied when the matrix is BUILT rather than when the key arrives, so
 * the entry stays in the down-list and its release is still tracked by the
 * ordinary poll; the matrix - which is all CIA1 can see - is what VICE's
 * early return actually protects. */
static int c64_is_joykey(int scan)
{
    int i;
    if (!c64_keyset)
        return 0;                           /* joykeys_enable == 0: the key
                                             * falls through to the matrix,
                                             * exactly as joystick.c:598 does */
    for (i = 0; i < 4; i++) {               /* the four directions only */
        if (scan == (int)c64_joysc[i])
            return 1;
    }
    return 0;
}

/* c64_kbd_set - one matrix cell, plus the synthetic modifier the .vkm entry
 * asks for. `deshift` counts the entries that want SHIFT actively RELEASED,
 * which is what the .vkm's 0x10 means and why the rebuild is a whole pass and
 * not a set of independent writes. */
static int c64_kbd_deshift;

static void c64_kbd_set(int row, int col, int flags)
{
    if (row == C64K_RESTORE)
        return;                             /* not a matrix position (7.4) */
    if (row < 0 || row > 7)
        return;
    c64_matrix[row] |= (unsigned char)(1 << col);
    if ((flags & C64K_SH) != 0)
        c64_matrix[C64K_LSHIFT_R] |= (unsigned char)(1 << C64K_LSHIFT_C);
    if ((flags & C64K_CBM) != 0)
        c64_matrix[C64K_LCBM_R] |= (unsigned char)(1 << C64K_LCBM_C);
    if ((flags & C64K_DESH) != 0)
        c64_kbd_deshift++;
}

/* c64_kbd_build - RULE 3: the matrix is rebuilt from the WHOLE down-list,
 * never cleared incrementally. */
static void c64_kbd_build(void)
{
    int i, e, ch, wantsh;

    for (i = 0; i < 8; i++)
        c64_matrix[i] = 0;
    c64_kbd_deshift = 0;
    wantsh = 0;

    for (i = 0; i < c64_ndown; i++) {
        if (c64_is_joykey((int)c64_down[i]))
            continue;                       /* the keyset took it: it drives
                                             * port 2 and is not typed
                                             * (keyboard.c:788-798) */
        ch = c64_dasc[i];
        e = c64_scan_is_ctrl(ch, c64_down[i]);
        if (e != 0) {
            /* CTRL+letter: the letter's own position, and the CTRL key */
            e = c64_kmap_asc(e + 32);       /* the lowercase entry */
            if (e >= 0)
                c64_kbd_set(c64_kasc[e], c64_kasc[e + 1], 0);
            c64_matrix[C64K_LCTRL_R] |= (unsigned char)(1 << C64K_LCTRL_C);
            continue;
        }
        e = c64_kmap_scan(c64_down[i]);
        if (e >= 0) {
            c64_kbd_set(c64_kscan[e + 1], c64_kscan[e + 2], c64_kscan[e + 3]);
            continue;
        }
        e = c64_kmap_asc(ch);
        if (e < 0)
            continue;
        c64_kbd_set(c64_kasc[e], c64_kasc[e + 1], c64_kasc[e + 2]);
        if ((c64_kasc[e + 2] & C64K_OPT) != 0 && c64_shift_down)
            wantsh = 1;                     /* an OPTionally shifted key takes
                                             * the HOST's shift, which is what
                                             * makes SHIFT+letter the graphic
                                             * character it is on the machine */
    }

    /* CTRL+digit, from the poll and not from a chord (7.3) */
    if (c64_ctrl_down) {
        for (i = 0; i < 10; i++) {
            if (c64_kdig_down[i]) {
                e = c64_kmap_asc('0' + i);
                if (e >= 0)
                    c64_kbd_set(c64_kasc[e], c64_kasc[e + 1], 0);
                c64_matrix[C64K_LCTRL_R] |=
                    (unsigned char)(1 << C64K_LCTRL_C);
            }
        }
    }

    /* A BARE SHIFT AND A BARE CTRL ARE MATRIX KEYS IN THEIR OWN RIGHT.
     * keyboard_latch_modifier_states (src/keyboard.c:474-482, :505-512) puts
     * the left-shift and left-ctrl positions in the matrix whenever the
     * PHYSICAL key is down - `left_shift_down > 0 && !virtual_deshift`, and
     * `left_ctrl_down > 0` - with no other key needed. This port set SHIFT
     * only when some OTHER key's mapping asked for it and CTRL only for a
     * recognised Ctrl chord or digit, so a game that polls $DC01 for the
     * shift key (the second fire button of a thousand of them) saw nothing at
     * all, and holding Shift before pressing a key put nothing in the matrix.
     * The DESHIFT suppression below still wins, which is VICE's
     * `!virtual_deshift`. */
    if ((wantsh || c64_shift_down) && c64_kbd_deshift == 0)
        c64_matrix[C64K_LSHIFT_R] |= (unsigned char)(1 << C64K_LSHIFT_C);
    if (c64_kbd_deshift != 0)
        c64_matrix[C64K_LSHIFT_R] &= (unsigned char)~(1 << C64K_LSHIFT_C);
    if (c64_ctrl_down)
        c64_matrix[C64K_LCTRL_R] |= (unsigned char)(1 << C64K_LCTRL_C);
}

/* ==========================================================================
 * THE ONCE-PER-WAKE POLL (7.2's rules 2, 3 and 4)
 *
 * Every emulated CIA1 read in this wake reads what this leaves behind. The
 * bound is the down-list, the five joystick scancodes, Ctrl, the two shifts
 * and - only while Ctrl is held - the ten digits: about twenty far calls at
 * 46.7 us, under a millisecond, AND IT DOES NOT MULTIPLY BY THE NUMBER OF
 * SLICES IN THE WAKE.
 * ========================================================================*/
static void c64_kbd_add(int ascii, int scan)
{
    int i;
    for (i = 0; i < c64_ndown; i++) {
        if (c64_down[i] == (unsigned char)scan) {
            c64_dasc[i] = (unsigned char)ascii;
            c64_dage[i] = 0;                /* a typematic repeat is the key
                                             * still being held, and it starts
                                             * the freshness window again */
            c64_dcyc[i] = c64_cyc_lo;
            return;
        }
    }
    if (c64_ndown >= C64K_DOWNMAX) {
#ifdef C64_HOST
        c64_ndrop++;                    /* the 17th key is DROPPED, not
                                         * written past the end (7.2) */
#endif
        return;
    }
    c64_down[c64_ndown] = (unsigned char)scan;
    c64_dasc[c64_ndown] = (unsigned char)ascii;
    c64_dage[c64_ndown] = 0;
    c64_dcyc[c64_ndown] = c64_cyc_lo;       /* the EMULATED clock, which is
                                             * the one the machine scans on */
    c64_ndown++;
}

static void c64_kbd_poll(void)
{
    int i, j, n;

    /* --- drain the ring: every press since the last wake goes in first, so
     * a key pressed and released between two wakes is still seen once */
    while (c64_ring_h != c64_ring_t) {
        i = c64_ring_a[c64_ring_h];
        j = c64_ring_s[c64_ring_h];
        c64_ring_h = (c64_ring_h + 1) & (C64K_RING - 1);
        if (j == KSC_PGUP) {                /* RESTORE is not a matrix key */
            c64_nmi_raise();                /* 7.4: the NMI, and RUN/STOP is
                                             * read from the matrix the way a
                                             * real machine reads it */
            continue;
        }
        c64_kbd_add(i, j);
    }

    /* --- the release poll. An entry that has not yet had one emulated
     * keyboard-scan interval in the matrix is not tested at all (rule 4, and
     * its unit is C64K_FRESH_CYC EMULATED cycles); one that is still down
     * stays; one that is up and has been seeable leaves. */
    n = 0;
    for (i = 0; i < c64_ndown; i++) {
        if (c64_dage[i] == 0) {
            c64_dage[i] = C64K_AGE_FRESH;
        } else if (c64_dage[i] == C64K_AGE_FRESH) {
            if ((unsigned)(c64_cyc_lo - c64_dcyc[i]) >=
                (unsigned)C64K_FRESH_CYC)
                c64_dage[i] = C64K_AGE_OLD; /* it has been in the matrix long
                                             * enough for the machine to have
                                             * scanned it; from here the host
                                             * key state decides */
        }
        if (c64_dage[i] == C64K_AGE_OLD && !os88_key_down(c64_down[i]))
            continue;
        c64_down[n] = c64_down[i];
        c64_dasc[n] = c64_dasc[i];
        c64_dage[n] = c64_dage[i];
        c64_dcyc[n] = c64_dcyc[i];
        n++;
    }
    c64_ndown = n;

    /* --- the modifiers and the joystick, from the same pass */
    c64_shift_down = (os88_key_down(KSC_LSHIFT) || os88_key_down(KSC_RSHIFT))
                     ? 1 : 0;
    c64_ctrl_down = os88_key_down(KSC_CTRL) ? 1 : 0;
    for (i = 0; i < 10; i++)
        c64_kdig_down[i] = 0;
    if (c64_ctrl_down) {
        for (i = 0; i < 10; i++)
            c64_kdig_down[i] = (unsigned char)(os88_key_down(c64_kdigsc[i])
                                               ? 1 : 0);
    }
    j = 0;
    if (c64_keyset) {                       /* joykeys_enable: with the keyset
                                             * off there is no stick and the
                                             * keys are typed instead
                                             * (joystick.c:598-601) */
        for (i = 0; i < 5; i++) {
            if (os88_key_down(c64_joysc[i]))
                j |= (1 << i);
        }
    }
    c64_joy2 = j;
    c64_joy1 = 0;                           /* port 1 is empty (8) */

    c64_kbd_build();
}

/* ==========================================================================
 * WHAT CIA1 SEES (6.1, src/c64/c64cia1.c's read_ciapa / read_ciapb)
 *
 * PRA drives the matrix's row lines and PRB reads its column lines; a bit the
 * DDR makes an input floats HIGH, and a closed key pulls the line it is on
 * LOW - which is why every value below is built by clearing bits out of $FF.
 * Control port 2 is on PRA bits 0-4 and control port 1 on PRB bits 0-4, both
 * active low, exactly as the hardware has them.
 * ========================================================================*/
static int c64_port_out(int k, int r)
{
    int d = c64_cia[k][r + 2];
    return ((c64_cia[k][r] & d) | (~d & 0xFF)) & 0xFF;
}

static int c64_joy_bits(int port)           /* 1 = the line is PULLED LOW */
{
    int m = c64_joyswap ? ((port == 2) ? c64_joy1 : c64_joy2)
                        : ((port == 2) ? c64_joy2 : c64_joy1);
    return m & 0x1F;
}

static int c64_kbd_pa(void)
{
    int v, pb, r;
    v = c64_port_out(0, 0);
    pb = c64_port_out(0, 1);
    for (r = 0; r < 8; r++) {
        if ((c64_matrix[r] & ~pb & 0xFF) != 0)
            v &= ~(1 << r);
    }
    v &= ~c64_joy_bits(2);
    return v & 0xFF;
}

static int c64_kbd_pb(void)
{
    int v, pa, r;
    v = c64_port_out(0, 1);
    pa = c64_port_out(0, 0);
    for (r = 0; r < 8; r++) {
        if ((pa & (1 << r)) == 0)
            v &= ~c64_matrix[r];
    }
    v &= ~c64_joy_bits(1);
    return v & 0xFF;
}
