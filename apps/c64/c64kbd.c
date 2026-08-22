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
 * WAVE 1 IS THE TABLES
 * ----------------------------------------------------------------------------
 * The map, the shift/CBM/CTRL matrix positions, the cached matrix and the
 * 16-entry down-list are here now. The LEVEL model that fills them - the
 * once-per-wake OSAPI_KEY_DOWN poll, the rebuild from the whole down-list,
 * the Ctrl-held digit poll, RESTORE's NMI - is wave 2, with the CIA1 that
 * reads the matrix (docs/C64-PORT-PLAN.md wave 2). Until then a key goes into
 * a small ring and kicks the slice driver, and nothing pretends to type.
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
static int c64_ndown;
static int c64_joyswap;                     /* Alt+J, Swap joysticks (8) */
/* the two control ports' direction masks, in src/joyport/joystick.c's bit
 * order: 0 up, 1 down, 2 left, 3 right, 4 fire. The status row's two five-dot
 * indicators are these, and they are what c64_st_joy1/c64_st_joy2 delta
 * against. Wave 1 has no CIA1 to answer them and no keyset poll, so both are
 * honestly 0 and the row shows ten unlit dots (C64-SPEC §8). */
static int c64_joy1, c64_joy2;

/* the key ring: os88_onkey pushes and wave 2's slice pops. Wave 1 keeps it so
 * a key pressed before the core exists is not silently eaten. */
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
    c64_ring_h = 0;
    c64_ring_t = 0;
    c64_joyswap = 0;
    c64_joy1 = 0;
    c64_joy2 = 0;
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

/* c64_kmap_asc / c64_kmap_scan - the two lookups, so wave 2's poll has one
 * place to ask and this file's table has one reader. They answer the entry's
 * index into its table, or -1. */
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
