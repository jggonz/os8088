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

/* --- 7.6's fact, and the observable that answers it ----------------------
 * SPEC.md 9.6: on a machine with NO MOUSE the arrow keys ARE the pointer, and
 * ScrollLock hands them back. That matters here more than anywhere, because
 * those four keys are also this port's joystick (8): with no mouse the stick
 * works - kbd_track runs in the int 09h ISR and fills os88_key_down's map
 * whatever the UI task then does with the key (kernel/mouse.inc:1438) - but
 * every deflection ALSO drags the desktop pointer about, which is what the
 * message exists to explain.
 *
 * THE SDK CANNOT BE ASKED "HAS A MOUSE SPOKEN". C64-SPEC §7.6 said to read it
 * out of os88_mouse(), and os88_mouse answers x, y and the button and nothing
 * else - kernel/kernel.asm:3263's osapi_mouse tests [mou_seen] and does not
 * report it. Adding a slot for it spends kernel headroom (SPEC.md 73.9's
 * KERN_BUDGET is a decision and not a build fix), so this asks a question the
 * package CAN answer and that has the same answer:
 *
 *   kbm_key (kernel/mouse.inc:934-941) intercepts a cursor key when, and only
 *   when, no mouse has spoken AND ScrollLock is off - and an intercepted key
 *   never reaches os88_onkey. So "the down-map says an arrow is held, and
 *   os88_onkey has never once delivered an arrow" IS "the kernel is eating
 *   them", observed rather than inferred.
 *
 * Three consecutive polls are required because a wake posted BEFORE the press
 * is dispatched before the key event behind it, so one poll can legitimately
 * see the ISR's bit with the W_ONKEY still queued. The latch is one way and
 * the message is said once a session (SPEC.md 47). */
static int c64_arrow_typed;                 /* os88_onkey delivered a cursor
                                             * key: the kernel is not taking
                                             * them, and it never will be */
static int c64_arrow_held;                  /* consecutive polls with the
                                             * stick deflected and no arrow
                                             * ever typed */
static int c64_slock_said;

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
    c64_arrow_typed = 0;
    c64_arrow_held = 0;
    c64_slock_said = 0;
}

static void c64_key_push(int ascii, int scan)
{
    int n = (c64_ring_t + 1) & (C64K_RING - 1);
    if (scan == KSC_UP || scan == KSC_DOWN
        || scan == KSC_LEFT || scan == KSC_RIGHT)
        c64_arrow_typed = 1;                /* the kernel's keyboard mouse is
                                             * not taking these (7.6) - and it
                                             * cannot start to, so this latch
                                             * is one way */
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

    /* ...and 7.6's message, at the one moment it is worth saying: the stick
     * is deflected and the four keys doing it have never once arrived as a
     * keystroke, which is kbm_key eating them. It costs one compare a poll
     * after it has been said. */
    if ((j & 0x0F) == 0) {
        c64_arrow_held = 0;
    } else if (!c64_slock_said && !c64_arrow_typed) {
        c64_arrow_held++;
        if (c64_arrow_held >= 3) {
            c64_slock_said = 1;
            c64_say("ScrollLock for joystick");
        }
    }

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

/* ==========================================================================
 * COPY, PASTE AND THE KEYBOARD-BUFFER FEEDER (11.1's Edit menu, 7.7)
 *
 * The two buffers and the feeder are HERE, with the keyboard, because that is
 * what a paste is on this machine: VICE does not type into a window, it puts
 * PETSCII into the KERNAL's OWN ten-byte keyboard buffer at $0277 with the
 * count at $C6 (kbdbuf_init's three numbers, src/c64/c64.c:1124) and lets the
 * machine's own scanner take it from there.
 *
 * THE PACE IS THE KERNAL'S AND NOT OURS. kbdbuf_flush (src/kbdbuf.c:370-410)
 * refuses to put anything in while the buffer is NOT EMPTY - `kbdbuf_is_empty`
 * is `mem_read($C6) == 0` (:233-236) - and never puts in more than the buffer
 * holds. So a paste is ten characters, then nothing at all until BASIC's
 * GETIN has drained them, then ten more; a program that stops reading stops
 * the paste, which is exactly what happens when you type at a real one.
 *
 * AND IT IS BOUNDED PER WAKE, which is this OS's half of the rule: VICE calls
 * kbdbuf_flush once a frame from the vsync handler, and the equivalent here is
 * once per wake from the slice driver - at most ten c64_wr calls and one
 * c64_rd, whatever the clipboard holds.
 *
 * The two buffers are SEPARATE on purpose. c64_clip is COPY's output and
 * c64_pastebuf is PASTE's queue, which outlives the command by however long
 * the machine takes to drink it. One buffer would have made a Copy taken
 * during a long paste silently rewrite what was still being typed - and each
 * is now sized from ITS OWN direction, which the first draft was not: it
 * staged the host's clipboard in c64_clip and so bounded a paste by what a
 * COPY can produce, which is not a bound on anything a paste can be handed.
 *
 * ----------------------------------------------------------------------------
 * AND EVERY PER-BYTE STEP OF BOTH IS RESIDENT AND TABLE-DRIVEN. THAT IS THE
 * WHOLE REASON THIS BLOCK IS SHAPED THE WAY IT IS.
 * ----------------------------------------------------------------------------
 * The first draft put the 40x25 walk and the conversion loop in c64cmd.c,
 * which is C64.OVL - so the inner loop crossed the segment boundary TWICE a
 * cell: `call far [cc_ovv_c64_rd]` into the resident shim (crt0.asm's
 * cc_ovthunk: pop/pop, the three-word LIFO stash, `call bx`, unstash, two
 * pushes, retf) and `call far [cc_ovm_ovl_sc_ascii]` back into the module,
 * ~500 clocks of pure bridge per cell on top of the work. 2,000 crossings for
 * one Edit > Copy, ~110 ms, ALL OF IT under the gfx lock - and the harness's
 * cost row charged one NEAR call for the lot. SPEC.md 73.14 splits by
 * FREQUENCY: what runs once per command goes out, what runs per BYTE stays
 * in, and docs/C-TOOLCHAIN.md says it in one line - "the inner loop is
 * assembly: anything that touches bytes per iteration is a hand-written proc
 * that C calls once".
 *
 * So the command is a LATCH in the module and the work is resident. A Copy is
 * ONE bridge crossing now - os88_oncmd -> ovl_cmd, the runtime's far entry,
 * and back - and a Paste is one, because c64_clip_service and the feeder are
 * both this side of the boundary and the clipboard calls are theirs. The
 * enumeration matters because the next ovl_* that loops gets priced from it:
 * the previous shape crossed FOUR times a command (os88_oncmd -> ovl_cmd,
 * ovl_cmd -> ovl_copy, ovl_copy -> c64_copy_screen, ovl_copy ->
 * os88_clip_put) and the first draft of all crossed 2,000 times, two a cell.
 * Inside the resident half:
 *
 *  - THE ROW ITSELF IS COMPOSED IN ASSEMBLY. c64_copy_row (c64mem.inc) does
 *    the table index, the reverse-video mask, last_non_whitespace, the trim
 *    and the store into the clipboard claim - 25 calls for the screen, ~20 us
 *    a cell against the ~95 the same loop cost as SmallerC emitted it with an
 *    os88_poke per byte. docs/C-TOOLCHAIN.md's rule, not a preference.
 *  - THE MATRIX COMES OUT ONE ROW PER CALL, not one cell per call.
 *    c64_zcopy_out is c64mem.inc's `rep movsb` out of the RAM claim (the same
 *    claim c64_rd's fast path reads, so this is the same bytes by a cheaper
 *    route and not a different memory), 25 near calls for the screen.
 *  - THE TWO CONVERSIONS ARE 128- AND 256-BYTE TABLES, built once at launch
 *    from the transcribed functions below, so the loop body is an index and
 *    not a call. The functions stay as VICE wrote them - they are the
 *    provenance, and building the table from them is what keeps one copy of
 *    the arithmetic.
 * ========================================================================*/
#define C64_CLIPMAX (C64_CELLS + C64_ROWS + 1)  /* 1,026: COPY's OWN bound -
                                                 * the 40x25 screen, one line
                                                 * ending a row and the NUL,
                                                 * which is the largest thing
                                                 * clipboard_read_screen_output
                                                 * can produce */
#define C64_PASTEMAX 2048                   /* ...and PASTE's, which is a
                                             * different question: it is what
                                             * ONE os88_clip_get can be given,
                                             * because OSAPI_CLIP_GET has no
                                             * offset (kernel/clip.inc:145-157)
                                             * and so cannot be read in chunks.
                                             * 2,048 is ~50 lines of a BASIC
                                             * listing; a clipboard longer than
                                             * it is pasted as far as it fits
                                             * and the truncation is SAID
                                             * (SPEC.md 47). The message names
                                             * this number and c64uitest.c
                                             * checks that it still does */
#define C64_KB_BUF   0x0277                 /* kbdbuf_init(631, 198, 10, ...) */
#define C64_KB_NDX   0x00C6                 /* src/c64/c64.c:1124 */
#define C64_KB_SIZE  10

/* NEITHER BUFFER IS bss ANY MORE, AND THAT IS THE POINT (C64-SPEC §13.0.1).
 * 1,026 + 2,048 = 3,074 bytes held for the life of the app so that two menu
 * commands nobody may ever pick would have somewhere to put their bytes -
 * and this port's resident figure is what wave 4 (the bitmap modes, the
 * sprites, the PRG loader) has to fit under. CWORD's lesson, in one line: the
 * next byte to save is a buffer moving to a claim. So each is a TRANSIENT
 * HEAP CLAIM, 2KB, taken when the command runs and freed the moment it is
 * spent - Copy's inside one wake, Paste's when the queue drains or the next
 * reset empties it. A claim that cannot be had is a refusal that is SAID
 * (SPEC.md 47), and the machine is untouched.
 *
 * The two are still SEPARATE claims for the reason they were separate arrays:
 * c64_clip is COPY's output and the paste queue outlives its command by
 * however long the machine takes to drink it, so a Copy during a long Paste
 * must not rewrite what is still being typed.
 *
 * The bytes get into and out of the claims WITHOUT a per-byte C loop, which
 * is the toolchain's rule and not a preference (docs/C-TOOLCHAIN.md). Copy
 * composes a whole row inside c64_copy_row (c64mem.inc) - table index, trim
 * and store, 25 calls for the screen; the feeder peeks its 21-byte window out
 * of the paste claim with os88_peek, which is ~0.4 ms once a wake and is not
 * an inner loop by any reading. c64uitest.c prices both. */
#define C64_CLIPKB   2                      /* ceil(1,026 / 1024) */
#define C64_PASTEKB  2                      /* ...and 2,048 exactly */
#define C64_PFWIN    (C64_KB_SIZE * 2 + 1)  /* the feeder's peek window: ten
                                             * delivered bytes consume at most
                                             * twenty (every one a CRLF pair),
                                             * and the +1 is the LOOKAHEAD, so
                                             * the pair test at the end of a
                                             * window always has the byte it
                                             * needs and can never leave a
                                             * stray LF to become a second
                                             * RETURN on the next wake */

static unsigned c64_paste_seg;              /* Paste's claim, 0 = none */
static unsigned char c64_pbuf[C64_PFWIN];   /* ...and the window peeked out of
                                             * it once per wake */
static int c64_paste_n;                     /* host bytes in the queue */
static int c64_paste_i;                     /* ...and how many are spent */
static unsigned char c64_scrow[C64_COLS];   /* one matrix row, one call */
static unsigned char c64_sctab[128];        /* screen code -> host ASCII */
static unsigned char c64_pettab[256];       /* host byte -> PETSCII */

/* THE TWO TABLES ARE BUILT IN THE OVERLAY, ONCE, AND THAT IS WHY THEY ARE
 * DECLARED HERE AND FILLED THERE. `ovl_conv_init` (c64cmd.c) carries VICE's
 * three transcribed conversions - charset_screencode_to_petscii,
 * petcii_fix_dupes, charset_p_toascii and charset_p_topetscii - and runs them
 * 128 and 256 times on the FIRST WAKE. The arrays above are bss, so they stay
 * RESIDENT and DS-relative, which is the whole of what makes the move legal
 * (SPEC.md 73.14: only code moves).
 *
 * IT IS THE FREQUENCY SPLIT APPLIED TO THIS PORT'S OWN LAUNCH PATH. ~275
 * emitted instructions that run EXACTLY ONCE were sitting in the resident
 * half beside the loops that run a thousand times a Copy; once-per-launch is
 * the `goes out` side of that line, and the recovery is ~700 bytes of image
 * against §13.0.1's headroom. Every per-byte path in this file indexes the
 * tables and calls nothing.
 *
 * A disk with no C64.OVL is unaffected: c64_sctab is read only by ovl_copy
 * and c64_pettab only by the feeder, which cannot have a queue until
 * ovl_paste has run - so an unbuilt table is unreachable on exactly the disks
 * where it is unbuilt. */

/* c64_copy_screen - clipboard_read_screen_output (src/clipboard.c:41-100),
 * RESIDENT and called ONCE per Edit > Copy. `base` is the caller's c64_mbase,
 * which is mem_get_screen_parameter's answer ($D018 and CIA2's bank,
 * clipboard.c:55) - passed in rather than read here because c64_mbase is
 * declared in c64scr.c, which this file precedes.
 *
 * The answer is the byte count in c64_clip; the line ending is `\n`, which is
 * what every non-Windows build uses (actions-clipboard.c:54-58).
 *
 * COST, AND IT IS NO LONGER HELD-LOCK TIME: 50 calls for the screen - one
 * c64_zcopy_out and one c64_copy_row a row - and it runs from os88_onwake
 * with NO lock held (c64_clip_service below). Nothing is drawn. The C here
 * is a 25-iteration loop and NOT a per-cell one: the table index, the
 * last_non_whitespace test, the trim and the store into the claim are all
 * inside c64_copy_row (c64mem.inc), which is the toolchain's own rule -
 * anything that touches bytes per iteration is assembly that C calls once. */
static int c64_copy_screen(unsigned seg, unsigned base)
{
    int row, n;

    n = 0;
    for (row = 0; row < C64_ROWS; row++) {
        c64_zcopy_out(c64_scrow, base, C64_COLS);
        base += C64_COLS;
        n += c64_copy_row(seg, (unsigned)n, C64_COLS);
    }
    return n;
}

/* c64_clip_service - EDIT > COPY AND EDIT > PASTE, RUN FROM THE WAKE.
 *
 * WHY NEITHER RUNS IN ITS MENU COMMAND. os88_oncmd is dispatched under the
 * kernel's gfx lock (os88.h) - "you may draw, you must not take the lock" -
 * so every instruction a command executes is the WHOLE DESKTOP stopped: every
 * other task's drawing, the mouse cursor, the dock. Copy's body is 25
 * c64_zcopy_out calls, 1,000 table-driven cells at ~75 us each and 1,026
 * pokes: ~103 ms, nearly two host ticks, for a command that draws nothing.
 * On top of that both clipboard calls and the claims reach kernel/clip.inc's
 * mem_claim, which may compact an arena this app has a pinned 64KB and a
 * pinned 20KB sitting in - "a memcpy in tenths of a second", memory.inc's own
 * words, and a term nobody can bound from here. LESSONS.md 6 forbids exactly
 * that: "no C between gfx_lock and gfx_unlock that is not bounded by a count
 * you can state."
 *
 * Paste's per-byte work was moved out to the wake for this reason in the same
 * wave the two commands landed. This is the same argument applied to the
 * other half of the pair, and to the claims both now take.
 *
 * NOTHING ABOUT THE RESULT CHANGES. The 6510 advances only inside
 * os88_onwake, and this runs at the TOP of one, before the slice: between the
 * command's dispatch and here not one emulated cycle has run, so the matrix
 * and c64_mbase are bit-identical to what the user was looking at when they
 * picked the item. `base` is the caller's c64_mbase - $D018 and CIA2's bank,
 * mem_get_screen_parameter's answer (clipboard.c:55) - passed in because
 * c64_mbase is declared in c64scr.c, which this file precedes.
 *
 * Both latches are spent whatever the machine's state, so a paused or jammed
 * machine still services the Copy that is on the glass in front of it. */
static void c64_clip_service(unsigned base)
{
    unsigned seg;
    int n, got;

    if (c64_copy_req) {
        c64_copy_req = 0;
        seg = os88_mem_claim(C64_CLIPKB);
        if (seg == 0) {
            c64_say("No memory for the copy.");
        } else {
            n = c64_copy_screen(seg, base);
            if (os88_clip_put_seg(seg, 0, (unsigned)n) != 0)
                c64_say("The clipboard refused the screen.");
            os88_mem_free(seg);             /* ...and it is gone before the
                                             * slice runs: the claim lives for
                                             * one wake and no longer */
        }
    }

    if (c64_paste_req) {
        c64_paste_req = 0;
        n = os88_clip_size();
        if (n == -1) {                      /* the ONE answer that means empty
                                             * - a full 32,768-byte clipboard
                                             * arrives as 0x8000, which a
                                             * 16-bit int reads as -32,768 */
            c64_say("The clipboard is empty.");
            return;
        }
        c64_paste_stop();                   /* a second Paste over the first:
                                             * the old claim goes back before
                                             * the new one is asked for */
        seg = os88_mem_claim(C64_PASTEKB);
        if (seg == 0) {
            c64_say("No memory for the paste.");
            return;
        }
        c64_paste_seg = seg;
        got = os88_clip_get_seg(seg, 0, (unsigned)C64_PASTEMAX);
        if (got <= 0) {
            c64_say("The clipboard refused to be read.");
            c64_paste_stop();               /* which frees the claim */
            return;
        }
        c64_paste_i = 0;
        c64_paste_n = got;                  /* WHAT FITS, which is CX and not
                                             * the clipboard's whole length */
        if ((unsigned)n > (unsigned)C64_PASTEMAX)
            c64_say("Pasting the first 2048 bytes.");  /* C64_PASTEMAX, spelled
                                                        * out; hosttest checks
                                                        * the two still agree */
    }
}

/* c64_paste_stop - a reset empties the queue, as VICE's kbdbuf_abort does
 * (src/kbdbuf.c:312-320, called from machine_reset at src/machine.c:262 -
 * kbdbuf_reset at :297 is a DIFFERENT function that re-initialises the
 * buffer's location and size and does not clear num_pending). Without it a
 * Power cycle left the previous machine's paste typing itself into the new
 * one. VICE's abort is conditional on kbd_buf_cmdline, which has no
 * equivalent here.
 *
 * ...AND IT IS ALSO WHERE THE CLAIM GOES BACK. It is called on every route
 * out of a queue - drained, reset, or a second Paste over the top of the
 * first - so the 2KB is held for exactly as long as there are bytes to type
 * and never a wake longer. */
static void c64_paste_stop(void)
{
    c64_paste_n = 0;
    c64_paste_i = 0;
    if (c64_paste_seg != 0) {
        os88_mem_free(c64_paste_seg);
        c64_paste_seg = 0;
    }
}

/* c64_paste_feed - kbdbuf_flush, on this machine's clock. Called once per
 * wake from the slice driver, BEFORE the slice, so the machine consumes what
 * it is given inside the same wake.
 *
 * AND THE CONVERSION IS HERE, TEN BYTES AT A TIME, NOT IN THE COMMAND.
 * charset_petconvstring converts VICE's whole clipboard up front and can
 * afford to; here that loop is ~145 us a byte of SmallerC's own code
 * (C64COST_PSBYTE, C64-SPEC §9.7) and os88_oncmd is dispatched UNDER THE GFX
 * LOCK (os88.h:566), so a full 2,048-byte queue converted in the command
 * would be ~300 ms of desktop
 * stopped dead - LESSONS.md 6's "no C between gfx_lock and gfx_unlock that is
 * not bounded by a count you can state". os88_onwake holds NO lock, and this
 * is already bounded by a count that is stated: the ten bytes the KERNAL's
 * buffer holds. Nothing about it is visible from the machine's side - the
 * same PETSCII arrives in the same order at the same pace - and what the
 * command now holds the lock for is one os88_clip_get.
 *
 * THE LINE ENDINGS ARE STILL TESTED BEFORE THE MAP (charset.c:72-74): the
 * PAIR test is here and the single-byte answer is in c64_pettab. */
static void c64_paste_feed(void)
{
    int n, c, k, m;

    if (c64_paste_i >= c64_paste_n)
        return;
    if (c64_rd(C64_KB_NDX) != 0)
        return;                             /* kbdbuf_is_empty said no: the
                                             * KERNAL has not drained the last
                                             * ten yet (kbdbuf.c:383) */
    /* THE WINDOW COMES OUT OF THE CLAIM ONCE, not a peek per byte tested:
     * at most 21 os88_peeks a wake (C64_PFWIN), which is ~0.4 ms. */
    m = c64_paste_n - c64_paste_i;
    if (m > C64_PFWIN)
        m = C64_PFWIN;
    for (k = 0; k < m; k++)
        c64_pbuf[k] = (unsigned char)os88_peek(c64_paste_seg,
                                               (unsigned)(c64_paste_i + k));
    n = 0;
    k = 0;
    while (n < C64_KB_SIZE && k < m) {
        c = (int)c64_pbuf[k];
        k++;
        /* A NUL ENDS THE PASTE, because it ends the STRING VICE converts:
         * charset_petconvstring is `while (*s)` (src/charset.c:71) and
         * kbdbuf_feed takes a C string, so a clipboard with a NUL in it is
         * fed as far as the NUL and no further. The kernel's clipboard is
         * bytes and not a string (SPEC.md 55), so this port can be handed
         * one; without this test the map turns it into PETSCII $3F and the
         * machine types a screenful of `?` for whatever binary followed. What
         * has already been produced stands - the KERNAL drinks it - and the
         * claim goes back with the queue. */
        if (c == 0) {
            c64_paste_n = c64_paste_i + k;  /* nothing beyond it is typed */
            break;
        }
        /* ...AND THE PAIR TEST IS NESTED RATHER THAN ONE `&&` CHAIN, which is
         * a code-size decision and not a style one: SmallerC lowers every
         * relational into a setcc sequence and an `&&` evaluates the whole
         * chain, so three of them rode on EVERY byte. Nested, an ordinary
         * character pays one compare and jumps over the rest - ~20 bytes of
         * 8088 fetch a byte, which is what this loop is priced in
         * (C64COST_PSBYTE, c64scr.c). */
        if (c == 0x0D) {                    /* test_lineend (charset.c:49-63):
                                             * CRLF is TWO bytes and ONE line
                                             * end. Without the pair test a
                                             * document written on a DOS
                                             * machine types two RETURNs a
                                             * line, which in BASIC is a
                                             * listing with a blank line
                                             * between every statement */
            if (k < m) {                /* the +1 of C64_PFWIN is what makes
                                         * this lookahead always available */
                if (c64_pbuf[k] == 0x0A)
                    k++;
            }
        }
        c64_wr(C64_KB_BUF + n, (int)c64_pettab[c]);
        n++;
    }
    c64_paste_i += k;                       /* what the window ACTUALLY spent */
    c64_wr(C64_KB_NDX, n);
    if (c64_paste_i >= c64_paste_n)
        c64_paste_stop();                   /* ...and the claim goes with it */
}
