/* ============================================================================
 * os8088 - apps/apple2/a2kbd.c       the II+ keyboard - A WAVE-1 STUB
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 6). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * ----------------------------------------------------------------------------
 * THE II+ BYTE MAP (APPLE2-SPEC section 6.1)
 * ----------------------------------------------------------------------------
 * Transcribed from AppleWin's `asciicode[3][10]` ROW 0 - the Apple II row -
 * and the fold code around it (`source/Keyboard.cpp:38-44` and `:121-229`):
 *
 *     { 0x08, 0x00, 0x15, 0x00, 0x00,  0x00,0x00,0x00,0x00,0x00 }   Apple II
 *       LEFT  UP    RIGHT DOWN  SELECT PRINT EXEC SNAP INS  DEL
 *
 * **A ZERO ENTRY MEANS DROP THE KEY, NOT SEND ZERO** (`if (!n) return;`,
 * :226-228). So a II+ has NO up arrow, NO down arrow and NO DEL - the //e row
 * beneath it has $0B, $0A and $7F and this machine is not a //e (section
 * 10.4).
 *
 * a-z fold to UPPERCASE UNCONDITIONALLY, because Caps Lock is always on on a
 * II/II+: AppleWin's non-//e arm is `if (key >= 'a' && key <= 'z') keycode =
 * key - 32;` with no g_bCapsLock test at all (:152-158), where the //e arm
 * above it is gated on the flag.
 *
 * ...and backtick, `{`, `|`, `}`, `~`, DEL and everything above $7F are
 * **REJECTED** rather than translated: `if (key == '`' || key >= '{') return;`
 * (:150-151). A II+ keyboard has no lower case and no braces to send.
 *
 * **LEFT ARROW IS $08**, which is why BkSp, Ctrl+H and Left all deliver $88
 * at the latch with no conflict at all - the three coincide on a real II+ too.
 * That is the whole content of "the Ctrl folds are routed on SCAN, never on
 * ascii": the BIOS has already folded Ctrl+H/I/M into $08/$09/$0D by the time
 * an ascii code arrives, and it does not matter, because $08/$09/$0D are what
 * a II+ wants for all six keys. What the SCAN is genuinely needed for is the
 * ARROWS, which arrive with ascii 0 and a scan code and are the only rows of
 * the table above that an ascii byte cannot express.
 *
 * ----------------------------------------------------------------------------
 * WHAT LANDS HERE LATER
 * ----------------------------------------------------------------------------
 * WAVE 3 brings the rest: F1 and F2 as the game BUTTONS PB0 and PB1 read as a
 * LEVEL through os88_key_down, Ctrl+F3's Open-Apple-Ctrl-Reset chord, and the
 * keyboard-mouse rule.
 *
 * WAVE 4 brings the paste feeder's peek-and-consume handshake and Copy's
 * screen-encoding walk.
 *
 * ALT+ENTER AND CTRL+F ARE **NOT** WAVE 3'S: Machine > Toggle Fullscreen is
 * LIVE from wave 1, a WF_FULL window has no menu bar, and a fullscreen window
 * with no chord is a machine with no way back. They are in apple2.c's
 * os88_onkey, RESIDENT, ahead of everything.
 *
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

/* a2_key - one keystroke, into the keyboard latch at $C000 (section 6.2).
 *
 * The two reset chords are tested FIRST and LATCH rather than run: os88_onkey
 * is dispatched under the desktop's gfx lock, and a power-on is a 48KB fill.
 * The wake spends them with no lock held (apple2.c).
 *
 * CTRL+F2 IS CTRL-RESET AND CTRL+F3 IS ITS OPEN-APPLE FORM (section 6.3),
 * scans $5F and $60 in the classic non-enhanced set an 83-key XT BIOS
 * delivers - and QEMU's SeaBIOS passes enhanced codes a real AT BIOS drops, so
 * the table is CONFIRMED ON IRON IN WAVE 7 and not here. **Ctrl-Reset also has
 * a menu item, which is the guaranteed route**, and it is what this wave's
 * evidence is taken through.
 */
static void a2_key(int ascii, int scan, void *win)
{
    int c;

    (void)win;
    if (ascii == 0 && scan == KSC_CTRL_F2) {
        a2_reset_req = A2_RST_CTRL;
        return;
    }
    if (ascii == 0 && scan == KSC_CTRL_F3) {
        a2_reset_req = A2_RST_OACTRL;
        return;
    }

    if (ascii == 0) {
        /* THE ARROWS, and only the arrows: asciicode[0] has $08 for LEFT and
         * $15 for RIGHT, and a ZERO for UP, DOWN, Select, Print, Execute,
         * Snapshot, Insert and Delete - and a zero means DROP. */
        if (scan == KSC_LEFT)
            c = 0x08;
        else if (scan == KSC_RIGHT)
            c = 0x15;
        else {
#ifdef A2_HOST
            a2_ndrop++;
#endif
            return;                         /* a II+ has no up arrow, no down
                                             * arrow and no DEL */
        }
    } else {
        c = ascii & 0xFF;
        if (c == '`' || c >= '{') {         /* `,{,|,},~,DEL and >$7F */
#ifdef A2_HOST
            a2_ndrop++;
#endif
            return;                         /* REJECTED, not translated */
        }
        if (c >= 'a' && c <= 'z')
            c -= 32;                        /* Caps Lock is always on */
    }
    a2_kb_put(c);
}
