/* ============================================================================
 * os8088 - apps/apple2/a2kbd.c              the II+ keyboard byte map
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
 * THE REST OF IT, WHICH IS A POLL AND NOT A KEYSTROKE (section 6.3, 6.6)
 * ----------------------------------------------------------------------------
 * F1 and F2 are the II+'s GAME BUTTONS PB0 and PB1 at $C061/$C062, and a
 * button is a LEVEL rather than an event: a game reads the address in a loop
 * and asks whether the button is down NOW. So they are polled once per wake
 * through os88_key_down (SPEC.md 9.7) and cached in a2_btn[], where the soft
 * switch reads them - never one bridge crossing per emulated read, which is
 * what a game polling $C061 would cost.
 *
 * They are a DEPARTURE FROM AppleWin'S Left-Alt / Right-Alt, with its reason:
 * Alt+Enter is this port's fullscreen chord (section 6.3). And they are host
 * conveniences rather than keys of the machine - `Open-Apple` and
 * `Solid-Apple` are //e KEYBOARD keys and a II+ has none; what a II+ has is
 * three digital inputs at $C061-$C063, of which PB0 and PB1 are the two a
 * game reads.
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

/* ==========================================================================
 * THE KEYBOARD-MOUSE RULE (APPLE2-SPEC section 6.6, C64-SPEC 7.6)
 * ========================================================================*/
/* SPEC.md 9.6: on a machine with NO MOUSE the kernel eats the arrows, Space
 * and Del as its pointer, and ScrollLock hands them back. On this machine
 * that is worse than a nuisance and better than a mystery: LEFT and RIGHT are
 * keys of the Apple II+ (asciicode row 0 gives $08 and $15) and SPACE is the
 * key a person typing BASIC presses most often after the letters.
 *
 * THE SDK CANNOT BE ASKED `HAS A MOUSE SPOKEN`. os88_mouse() answers x, y and
 * the button and nothing else, and adding a slot for it would spend kernel
 * headroom, which is a decision and not a build fix. So this asks a question
 * the package CAN answer and that has the same answer:
 *
 *   kbm_key (kernel/mouse.inc) intercepts one of those keys when, and only
 *   when, no mouse has spoken AND ScrollLock is off - and an intercepted key
 *   never reaches os88_onkey. So `the down-map says one is held, and
 *   os88_onkey has never once delivered one` IS `the kernel is eating them`,
 *   observed rather than inferred.
 *
 * NOTHING HERE READS SCROLLLOCK'S OWN SCAN CODE (0x46), and that is the whole
 * design rather than an omission: the rule is INFERRED from three polls and a
 * one-way latch, so a scan-code test for it would be a second, weaker answer
 * to a question already answered. apps/c64/c64kbd.c, the precedent, names no
 * such constant for the same reason; a `#define KSC_SCRLK` sat here unread
 * for a wave and was deleted, because a dead constant beside a rule that
 * deliberately reads no scan code invites the next reader to add one.
 *
 * THREE CONSECUTIVE POLLS, because a wake posted BEFORE a press is dispatched
 * ahead of the key event behind it, so one poll can legitimately see the
 * ISR's bit with the W_ONKEY still queued. The latch is ONE WAY - a kernel
 * that has delivered one of these keys is not going to start eating them -
 * and the message is said once a session (SPEC.md 47). */
static int a2_key_typed;                    /* os88_onkey delivered one of the
                                             * kernel's pointer keys: the
                                             * keyboard mouse is not taking
                                             * them, and it cannot start to */
static int a2_key_held;                     /* consecutive polls with one held
                                             * and none ever typed */
static int a2_slock_said;

static int a2_ptr_key(int scan)
{
    return (scan == KSC_LEFT || scan == KSC_RIGHT || scan == KSC_UP
            || scan == KSC_DOWN || scan == KSC_SPACE || scan == KSC_DEL)
           ? 1 : 0;
}

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
    /* SECTION 6.6'S OBSERVABLE, AND IT IS ONE WAY. A key the kernel's
     * pointer would have eaten has ARRIVED, so it is not eating them: either
     * a mouse has spoken or ScrollLock is already on, and neither un-happens.
     * It is tested before every drop below, because a dropped key is still a
     * delivered one. */
    if (a2_ptr_key(scan))
        a2_key_typed = 1;
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

/* a2_kbd_poll - the LEVEL half of the keyboard, once per wake.
 *
 * The two game buttons and section 6.6's message, from one pass of the
 * down-map. It runs at the top of the wake with no lock held, AFTER the reset
 * latches are spent - so Ctrl+F3's `PB0 held across the reset` survives this
 * poll rather than being overwritten by it (a2_reset_service). */
static void a2_kbd_poll(void)
{
    int held;

    /* THE GAME BUTTONS, AS LEVELS. F1 and F2 are PB0 and PB1 (section 6.3). */
    a2_btn[0] = os88_key_down(KSC_F1) ? 1 : 0;
    a2_btn[1] = os88_key_down(KSC_F2) ? 1 : 0;

    /* ...AND PB2 IS THE SHIFT-KEY MOD, ACTIVE WHEN SHIFT IS UP (AppleWin
     * Joystick.cpp:640-651, citing Sather UTAII p7-36; a2io.c has the whole
     * of it). BEHIND ITS OWN READER: two more os88_key_down calls are 93 us
     * a wake on the target, and almost no program asks, so the poll starts
     * only once $C063 has actually been read. Until then a2_btn[2] holds its
     * initial 1, which is Shift's own resting state. */
    if (a2_btn2_want)
        a2_btn[2] = (os88_key_down(KSC_LSHIFT) || os88_key_down(KSC_RSHIFT))
                  ? 0 : 1;

    /* ...AND THE KEYBOARD-MOUSE MESSAGE'S FIVE READS ARE BEHIND ITS OWN
     * ANSWER (section 6.6). The message is a ONE-SHOT: once a2_slock_said or
     * a2_key_typed is set, nothing can read `held` again - and a2_key_typed
     * latches on the first Space or arrow the user types into BASIC, which on
     * a machine that HAS a mouse is within seconds of launch. Asking anyway
     * was five OSAPI far calls at 46.7 us - 234 us a wake, for ever, on the
     * target - producing a value nothing reads. The `||` does not
     * short-circuit when no key is down, which is the common case. The two
     * button reads above stay unconditional: those ARE the level $C061
     * answers. */
    if (!a2_slock_said && !a2_key_typed) {
        held = (os88_key_down(KSC_LEFT) || os88_key_down(KSC_RIGHT)
                || os88_key_down(KSC_UP) || os88_key_down(KSC_DOWN)
                || os88_key_down(KSC_SPACE)) ? 1 : 0;
        if (!held) {
            a2_key_held = 0;
        } else {
            a2_key_held++;
            if (a2_key_held >= 3) {
                a2_slock_said = 1;
                a2_say("ScrollLock: arrows, Space.");
            }
        }
    }
}
