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
 * WAVE 4 BROUGHT the paste feeder's peek-and-consume handshake and Copy's
 * screen-encoding walk; they are at the foot of this file.
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
static unsigned a2_key_t0;                  /* ...and the tick the run of them
                                             * started on */
static int a2_slock_said;

/* THE WINDOW IS TICKS AND NOT POLLS, and three polls was NOT ENOUGH. A wake
 * runs about 1,400 times a second on a fast host, so three consecutive polls
 * span about two MILLISECONDS - far inside the gap between the ISR setting a
 * key's down-bit and the W_ONKEY carrying that key being dispatched. So the
 * first Space of `PRINT 6*7` latched the message on QEMU, which HAS a mouse
 * and was never eating anything (build/port-shots/wave4-cga-print.png caught
 * it in the status row). The poll count is kept as the cheap half and this is
 * the half that means something: four ticks is ~220 ms, which is longer than
 * any keystroke takes to come back round as an event and shorter than a press
 * somebody is holding to move a pointer with. */
#define A2_KBM_TICKS 4

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
            if (a2_key_held == 0)
                a2_key_t0 = os88_ticks();
            a2_key_held++;
            if (a2_key_held >= 3
                && (unsigned)(os88_ticks() - a2_key_t0) >= A2_KBM_TICKS) {
                a2_slock_said = 1;
                a2_say("ScrollLock: arrows, Space.");
            }
        }
    }
}

/* ==========================================================================
 * EDIT > COPY AND EDIT > PASTE (APPLE2-SPEC section 6.5)
 *
 * THE COMMANDS ARE LATCHES AND THE WORK IS SPLIT BY FREQUENCY, which is
 * SPEC.md 73.14's rule applied to a pair of commands that would otherwise
 * cross the segment boundary a thousand times:
 *
 *  - ONCE PER PICK - the two clipboard calls, the claims, the six refusals
 *    and the row walk - is `ovl_a2_clip_service` / `ovl_a2_copy_screen`, in
 *    APPLE2.OVL. This paragraph used to claim the split and the build did not
 *    have it: both bodies sat in `section .text`, about 170 x86 instructions
 *    of resident image for code that runs once per menu pick, and a2cmd.c
 *    held only the two latch assignments. They are reachable as `ovl_`
 *    because the wake is a UI-task context holding no lock and the latch can
 *    only have been set through ovl_a2_cmd, so the module is already resolved.
 *  - PER BYTE AND PER $C000 READ - `a2_paste_peek`, `_take`, `_live` and
 *    `_stop` - IS RESIDENT AND MUST BE: those are on the emulator's hottest
 *    I/O path, and `a2_paste_stop` is called by `a2_reset_service` too.
 *  - PER ROW is a hand-written proc (`a2_copy_row`, a2mem.inc), called once a
 *    row. A Copy is ONE bridge crossing - os88_oncmd -> ovl_a2_cmd and back -
 *    plus the service's own, and the C64's header records what the first
 *    draft of the same pair cost before any of this: 2,000 far-call round
 *    trips, ~110 ms, all of it under the gfx lock.
 *
 * AND NEITHER RUNS IN ITS COMMAND HANDLER. os88_oncmd is dispatched under the
 * DESKTOP's gfx lock (os88.h), so every instruction a command executes is the
 * whole desktop stopped; both clipboard calls and both claims reach
 * kernel/clip.inc's mem_claim, which may COMPACT an arena this app has a
 * pinned 64KB in - a term nobody can bound from a command handler
 * (LESSONS.md 6). ovl_a2_clip_service runs from the TOP of the wake with no lock
 * held and BEFORE the slice, so between the pick and the work not one
 * emulated cycle has run and the page copied is the page on the glass.
 * ========================================================================*/
#define A2_CLIPMAX  (A2_ROWS * (A2_COLS + 1))   /* 984: COPY'S OWN BOUND - 24
                                                 * rows of at most 40 folded
                                                 * cells and one CR, which is
                                                 * the largest thing a 40x24
                                                 * text page can produce */
#define A2_CLIPKB   1                       /* ceil(984 / 1024) */
#define A2_PASTEMAX 2048                    /* ...and PASTE's, which is a
                                             * DIFFERENT question: it is what
                                             * ONE os88_clip_get can be given,
                                             * because OSAPI_CLIP_GET has no
                                             * offset (kernel/clip.inc) and so
                                             * cannot be read in chunks. 2,048
                                             * is ~50 lines of an Applesoft
                                             * listing; a longer clipboard is
                                             * pasted as far as it fits and the
                                             * truncation is SAID (SPEC.md 47),
                                             * with the number spelled out */
#define A2_PASTEKB  2

/* NEITHER BUFFER IS bss, AND THAT IS SECTION 15'S OWN ARITHMETIC. The C64
 * gave back 3,074 bytes doing exactly this: two staging arrays held for the
 * life of the app so that two commands nobody may ever pick would have
 * somewhere to put their bytes. Each is a TRANSIENT HEAP CLAIM instead -
 * Copy's taken and freed inside one wake, Paste's held for exactly as long as
 * there are bytes left to type. A claim that cannot be had is a refusal that
 * is SAID, and the machine is untouched. */
/* a2_paste_seg IS DECLARED IN a2io.c, beside a2_kb_put and a2_paste_up, and
 * the note there says why: the soft-switch arms test it directly rather than
 * calling a2_paste_live, so that a machine with no paste pays one compare on
 * its hottest path instead of a near call. The queue's other three words are
 * this file's. */
static int a2_paste_n;                      /* bytes in the queue... */
static int a2_paste_i;                      /* ...and how many are spent */
static int a2_paste_cr;                     /* the last byte PRESENTED was a
                                             * CR, so an LF behind it is the
                                             * second half of a CR LF pair and
                                             * not a second RETURN */

/* a2_paste_stop - every route out of a queue: drained, reset, or a second
 * Paste over the top of the first. It is also where the claim goes back, so
 * the 2KB is held for exactly as long as there are bytes to type and never a
 * wake longer. */
static void a2_paste_stop(void)
{
    a2_paste_n = 0;
    a2_paste_i = 0;
    a2_paste_cr = 0;
    a2_paste_up = 0;
    if (a2_paste_seg != 0) {
        os88_mem_free(a2_paste_seg);
        a2_paste_seg = 0;
    }
}

/* a2_paste_peek - THE PEEK HALF OF apple2emu's HANDSHAKE
 * (src/keyboard.cpp:176-195, `keyboard_read`), called from the $C000 latch
 * read and from nowhere else.
 *
 * THE EMULATED PROGRAM SETS THE RATE AND THERE IS NO PACING STATE AT ALL.
 * apple2emu's keyboard_read looks at `*Clipboard_ptr` and answers it with bit
 * 7 set; keyboard_clear ($C010) is what ADVANCES the pointer. So a byte is
 * presented exactly as long as the machine has not taken it, and the next one
 * appears the instant it has - which is the one shape that cannot overrun the
 * machine's input (PERFORMANCE.md's third emulator-invisible defect). A
 * feeder that pushed n bytes a wake would be a guess about how fast Applesoft
 * drinks; this is not a guess.
 *
 * THE THREE FOLDS ARE THE DECISION'S, and each is one line:
 *   `\n` -> `\r`   apple2emu's own (keyboard.cpp:181-183)
 *   CR LF -> CR    the LF behind a presented CR is skipped, so a listing
 *                  copied on a DOS-line-ending machine types one RETURN a
 *                  line and not two
 *   a-z -> A-Z     as a2_key folds it, because the II+ keyboard has no lower
 *                  case at all (section 6.1) and Applesoft would answer
 *                  `?SYNTAX ERROR` to every line of a lower-case listing
 * and `\0` ENDS THE PASTE, because it ends the string apple2emu converts.
 *
 * **IT PRESENTS ONLY INTO A FREE LATCH, AND THAT ONE COMPARE IS TWO FIXES.**
 * This is called on EVERY $C000 read while a paste is in flight, not once per
 * byte the machine takes, and it used to rewrite the latch unconditionally.
 * So (1) a key the USER typed during a paste was DESTROYED - a2_kb_put set
 * the latch and the machine's next read overwrote it before returning, which
 * put Ctrl-C, the only in-machine way to stop a runaway paste, permanently
 * out of reach; and (2) Applesoft's ISCNTC polls $C000 between statements and
 * does NOT strobe $C010 unless the byte is Ctrl-C, so a RUN with a queue
 * still in it paid the guard + a2_paste_peek + os88_peek - ~33 us
 * (CLAUDE.md's 11 us a near call) - per emulated poll, for a queue that
 * could not advance. `if (a2_kb_ready) return;` closes both: a byte the
 * machine has not taken is still presented and a2_paste_i has not moved, so
 * nothing is lost; a key the user typed is delivered and the paste resumes
 * behind it; and a poll loop that is not consuming stops paying for the look.
 *
 * IT TAKES ONE MORE BYTE OF STATE AND IT HAS TO. Once the latch can hold a
 * byte that is not the queue's, the strobe at $C010 can no longer assume it
 * is consuming a pasted character: without `a2_paste_up` (declared in a2io.c
 * beside a2_kb_put, which is what clears it) every key the user typed during
 * a paste would swallow one queued byte on the strobe that followed it.
 *
 * ONE os88_peek PER CHARACTER THE MACHINE ACTUALLY TAKES, which is what this
 * costs: ~47 us once per byte the machine takes, because the latch is only
 * written when it is empty - the KEYIN loop that reads $C000 a thousand times
 * a second is reading a byte this has already presented and latched, and the
 * compare above is all it pays. */
static void a2_paste_peek(void)
{
    int k;

    if (a2_kb_ready)                        /* the machine has not taken the
                                             * last one - present nothing, and
                                             * do not tread on a key the user
                                             * typed */
        return;
    while (a2_paste_i < a2_paste_n) {
        k = os88_peek(a2_paste_seg, (unsigned)a2_paste_i) & 0xFF;
        if (k == 0) {
            a2_paste_stop();
            return;
        }
        if (k == '\n') {
            if (a2_paste_cr) {              /* the LF of a CR LF pair */
                a2_paste_i++;
                continue;
            }
            k = '\r';
        }
        if (k >= 'a' && k <= 'z')
            k -= 32;
        a2_paste_cr = (k == '\r');
        a2_kb_code = k & 0x7F;
        a2_kb_ready = 1;
        a2_paste_up = 1;                    /* ...and the strobe may consume
                                             * it (a2io.c beside a2_kb_put) */
        return;
    }
    a2_paste_stop();                        /* drained: the claim goes back */
}

/* a2_paste_take - THE CONSUME HALF (apple2emu's `keyboard_clear`,
 * src/keyboard.cpp:198-215): the strobe advances the pointer and clears the
 * latch, and the NEXT $C000 read presents the next byte. */
static void a2_paste_take(void)
{
    if (!a2_paste_up)                       /* the byte the machine just took
                                             * was the USER's - the queue has
                                             * presented nothing to consume
                                             * (a2io.c beside a2_kb_put) */
        return;
    a2_paste_up = 0;
    a2_paste_i++;
    if (a2_paste_i >= a2_paste_n)
        a2_paste_stop();
}

/* ovl_a2_copy_screen - the 40x24 text page onto the clipboard, ONE CALL A
 * ROW, and `ovl_` for this file's header's reason: it runs once per pick.
 *
 * 48 calls for the whole screen - one a2_zcopy_out and one a2_copy_row a row
 * - against 960 table indexes and 984 os88_pokes if the loop were written
 * here in C, which is docs/C-TOOLCHAIN.md's rule rather than a preference:
 * "anything that touches bytes per iteration is a hand-written proc that C
 * calls once".
 *
 * IT READS THE ROW BASES THE DISPLAY READS (a2_row_base), so a Copy on PAGE2
 * copies page 2 and a Copy of a MIXED screen copies the four text rows and
 * the twenty rows of graphics bytes folded as if they were text - which is
 * what "the text page" means when the machine is showing something else, and
 * is stated rather than hidden. The answer is the byte count. */
static int ovl_a2_copy_screen(unsigned seg)
{
    int row, n;

    n = 0;
    for (row = 0; row < A2_ROWS; row++) {
        a2_zcopy_out(a2_scrow, a2_row_base(row), A2_COLS);
        n += a2_copy_row(seg, (unsigned)n, A2_COLS);
    }
    return n;
}

/* ovl_a2_clip_service - both commands, run from the wake (this file's
 * header). It answers 1 because it ran: the runtime's own refusal path
 * answers 0 without running, which is how the caller learns the module could
 * not be resolved (a2cmd.c's header rule). */
static int ovl_a2_clip_service(void)
{
    unsigned seg;
    int n, got;

    if (a2_copy_req) {
        a2_copy_req = 0;
        seg = os88_mem_claim(A2_CLIPKB);
        if (seg == 0) {
            a2_say("No memory for the copy.");
        } else {
            n = ovl_a2_copy_screen(seg);
            if (os88_clip_put_seg(seg, 0, (unsigned)n) != 0)
                a2_say("The clipboard refused it.");
            os88_mem_free(seg);             /* gone before the slice runs: the
                                             * claim lives for one wake */
        }
    }

    if (a2_paste_req) {
        a2_paste_req = 0;
        n = os88_clip_size();
        if (n == -1) {                      /* the ONE answer that means empty
                                             * - a full 32,768-byte clipboard
                                             * arrives as 0x8000, which a
                                             * 16-bit int reads as -32,768 */
            a2_say("The clipboard is empty.");
            return 1;
        }
        a2_paste_stop();                    /* a second Paste over the first:
                                             * the old claim goes back before
                                             * the new one is asked for */
        seg = os88_mem_claim(A2_PASTEKB);
        if (seg == 0) {
            a2_say("No memory for the paste.");
            return 1;
        }
        a2_paste_seg = seg;
        got = os88_clip_get_seg(seg, 0, (unsigned)A2_PASTEMAX);
        if (got <= 0) {
            a2_say("Cannot read the clipboard.");
            a2_paste_stop();                /* ...which frees the claim */
            return 1;
        }
        a2_paste_i = 0;
        a2_paste_n = got;                   /* WHAT FITS, which is the read's
                                             * answer and not the clipboard's
                                             * whole length */
        a2_paste_cr = 0;
        if ((unsigned)n > (unsigned)A2_PASTEMAX)
            a2_say("Pasting 2048 bytes only.");     /* A2_PASTEMAX, spelled
                                                     * out; a2uitest checks
                                                     * that the two agree */
    }
    return 1;
}
