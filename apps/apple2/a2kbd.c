/* ============================================================================
 * os8088 - apps/apple2/a2kbd.c       the II+ keyboard - A WAVE-1 STUB
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 6). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * ----------------------------------------------------------------------------
 * WHAT LANDS HERE, AND WHEN
 * ----------------------------------------------------------------------------
 * WAVE 2 brings AppleWin's II+ byte map whole (`source/Keyboard.cpp:38-44`
 * and `:121-195`): `asciicode[3][10]` row 0, in which a II+ has NO up arrow,
 * NO down arrow and NO DEL - a zero entry means DROP the key, not send zero -
 * a-z fold to uppercase unconditionally because Caps Lock is always on on a
 * II/II+, and backtick, `{ | } ~`, DEL and everything above $7F are REJECTED
 * rather than translated. Left arrow IS $08, which is why BkSp, Ctrl+H and
 * Left all deliver $88 with no conflict. The Ctrl folds are routed ON SCAN
 * and never on ascii, because the BIOS has already folded Ctrl+H/I/M by the
 * time an ascii code arrives. Ctrl+F2 is Ctrl-Reset.
 *
 * WAVE 3 brings the rest: F1 and F2 as the Open-Apple and Solid-Apple BUTTONS
 * read as a LEVEL through os88_key_down, Ctrl+F3, and the keyboard-mouse
 * rule.
 *
 * ALT+ENTER AND CTRL+F ARE **NOT** WAVE 3'S, and that was a scheduling
 * mistake this wave had to correct: Machine > Toggle Fullscreen is LIVE from
 * wave 1, a WF_FULL window has no menu bar, and a fullscreen window with no
 * chord is a machine with no way back. They are in apple2.c's os88_onkey,
 * RESIDENT, ahead of everything - which is where the section-6.3 comment
 * always said they had to be.
 *
 * WAVE 4 brings the paste feeder's peek-and-consume handshake.
 *
 * ----------------------------------------------------------------------------
 * WHAT IS HERE IN WAVE 1
 * ----------------------------------------------------------------------------
 * The scan codes the rest of the package names, and a a2_key() that DROPS
 * every key - which is what an Apple II with no 6502 in it does with one.
 * There is no keyboard latch to put a byte in until wave 2 writes $C000's,
 * and a stub that pretended to accept keys would be a machine that looked
 * like it was typing and was not.
 * ==========================================================================*/

/* The BIOS scan codes this package names. They are the XT set's, which is
 * what OSAPI_KEY_DOWN and os88_onkey both speak (SPEC.md 9.7). */
#define KSC_SPACE  0x39
#define KSC_LSHIFT 0x2A
#define KSC_RSHIFT 0x36
#define KSC_ENTER  0x1C
#define KSC_BKSP   0x0E
#define KSC_TAB    0x0F
#define KSC_LEFT   0x4B
#define KSC_RIGHT  0x4D
#define KSC_UP     0x48
#define KSC_DOWN   0x50
#define KSC_DEL    0x53
#define KSC_F1     0x3B
#define KSC_F2     0x3C
/* THE TWO RESET CHORDS ARE A TARGET-CLASS QUESTION (APPLE2-SPEC section 6.3).
 * Ctrl+F2 is scan 0x5F and Ctrl+F3 is 0x60 in the classic non-enhanced set an
 * 83-key XT BIOS delivers, and QEMU's SeaBIOS passes codes a real AT BIOS
 * drops - so the table is CONFIRMED ON IRON in wave 7 and not here. Ctrl-Reset
 * also has a menu item, which is the guaranteed route. */
#define KSC_CTRL_F2 0x5F
#define KSC_CTRL_F3 0x60
/* ALT+ENTER, THE FULLSCREEN CHORD (APPLE2-SPEC section 6.3), which arrives
 * with ascii 0 and the ENTER scan in the classic 83-key set an XT BIOS
 * delivers and as 0xA6 from an enhanced one. Both are accepted rather than
 * one being guessed - the table proper is confirmed on iron in wave 7, and
 * this chord could not wait for it: os88_onkey's own comment says why. */
#define KSC_ALT_ENTER 0xA6

#ifdef A2_HOST
static int a2_ndrop;                        /* keys dropped, for the harness's
                                             * cost table. -DA2_HOST keeps the
                                             * counter out of the shipping
                                             * image (SPEC.md 73.9) */
#endif

/* a2_key - one keystroke.
 *
 * WAVE 1 DROPS IT, and says so here rather than in a toast: an Apple II with
 * no CPU has nothing to read the keyboard latch, and a stub that stored a
 * byte in it would be a machine that looked like it was accepting typing.
 */
static void a2_key(int ascii, int scan, void *win)
{
    (void)ascii;
    (void)scan;
    (void)win;
#ifdef A2_HOST
    a2_ndrop++;
#endif
}
