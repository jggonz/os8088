#!/usr/bin/env python3
"""Play a Z-machine story on os8088's Frotz, over a wire, unattended.

    make zh                                     # build the harness interpreter
    python3 tools/zharness.py ADVENT.Z3         # play its script, print the log
    python3 tools/zharness.py ADVENT.Z3 --repl  # ...or type at it yourself
    python3 tools/zharness.py --all --compare   # every story, diffed vs dfrotz
    python3 tools/zharness.py --all --graphics  # ...and what is on the SCREEN
    make zcheck / make zgfx                     # the same two, as gates

WHAT THIS IS FOR. `make ztest` asks whether an opcode is right one opcode at a
time, against tests/frotz/zopstest.inf - a story written to be a test. This
asks the other question, which that one cannot: does a REAL story run. The
answers differ. zopstest found @div's rounding; this found a branch that
decoded correctly, executed correctly, and left the program counter in a form
the next instruction's guard rejected.

AND `--graphics` ASKS A THIRD, which neither of the others can: what does the
reader SEE. A transcript is characters, and a story that draws a quote box into
the upper window and then throws it away prints exactly the same characters as
one that keeps it - so `--compare` is structurally blind to every defect that
is about the screen rather than the words. BEAR.Z5 sat with a blank band at the
top of its window matching dfrotz perfectly for as long as anyone had looked.

Three checks, and only the third needs a reference (SPEC.md 59.14):

    model vs pixels   every row the interpreter says holds text is drawn, and
                      every row it says is blank is not. Read by UNIFORMITY, so
                      a reversed status line and a coloured scene do not fool it
    across a repaint  the same, on a window just redrawn from the model, which
                      is where anything on the glass the model does not hold
                      goes missing
    opening screen    against tests/frotz/screens - the real curses Frotz's own
                      screen, photographed by tools/zref.py and committed
    pictures          every rectangle the interpreter said it blitted, read
                      back off the glass and held to the colour that picture
                      is. Only tests/frotz/zpictest.inf has any, because
                      @draw_picture is v6 and nothing that ships is

The first two need no golden and no second interpreter, which is what makes
them worth having: they are the drawing path against its own bookkeeping, and
they say WHICH half is wrong. A grid holding a box against a blank band is a
drawing defect; an empty grid against a blank band is a window-model defect.

EVERY GRAPHICS COMPLAINT LEAVES A PNG in build/zh/, and the raw wire - markers,
both windows' grids, every split, repaint and picture - is in <story>.wire. The
<story>.log beside it is the prose with all of that stripped out, which is the
wrong file to open when the question is graphical.

HOW IT WORKS. build/zh/frotz.o88 is apps/frotz built with -DZHARNESS, which
gives the interpreter a teletype on COM4: every character the story prints
goes out the port, every key it reads comes back in, and four bracketed
markers say where it is - READY, READ, KEY, HALT, QUIT. So a run is a
conversation over a UNIX socket rather than a person reading screendumps, and
a halt arrives with the whole transcript that led to it rather than the last
screenful of it.

READ and KEY are not the same event and the difference matters: READ is the
story asking for a COMMAND and consumes a line of the script, KEY is a "press
any key" prompt and is answered here with a space. Conflated, a title screen
eats the first command and every later one answers the wrong question.

The one part that is still a GUI walk is the launch: os8088 has no way to
start a package from outside, so this double-clicks Disk B and then the story,
at coordinates this file owns. If that ever stops working the symptom is a
timeout waiting for READY.

EVERY RUN THAT DOES NOT FINISH LEAVES A PNG of the screen, in build/zh/. That
is not a convenience: when a story stops mid-sentence the transcript cannot
say whether the machine is hung or waiting at something the harness does not
know how to answer, and those two need opposite fixes.

WHAT A FAILURE LOOKS LIKE, and why the halt line is worth reading closely:

    [Frotz: unknown opcode - the story has stopped.]
      opcode 0x0005 byte 0x0005 at 0x0004F21 es=25B2 story=20C0
    zharness:   HALT after 3 commands -> build/zh/advent.log
    zharness:   byte 0x05 -> 2OP:5 @inc_chk  (long form, small/small)   [pc 0x4F21]

The interpreter reports the opcode NUMBER and the opcode BYTE, and only the
byte says which of the five instructions numbered 5 it was. Naming it is this
file's job because the table belongs on the host, where changing it costs
nothing on the guest.
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
ZHDIR = os.path.join(BUILD, "zh")
STORYDIR = os.path.join(BUILD, "stories")
SCRIPTDIR = os.path.join(ROOT, "tests", "frotz", "scripts")
SCREENDIR = os.path.join(ROOT, "tests", "frotz", "screens")
QMP = os.path.join(BUILD, "qmp.sock")
ZHSOCK = os.path.join(BUILD, "zh.sock")
PIDFILE = os.path.join(BUILD, "qemu.pid")

# The GUI walk, on the 640x480 VGA `make zhboot` builds. Disk B is the second
# desktop icon; the file rows are 16px apart from the first at y=128, and the
# harness image is built HERE with exactly three entries in a fixed order, so
# STORY.DAT is always the third. That determinism is the reason the image is
# built by this file rather than taken as given.
DISK_B = (608, 105)
ROW_X = 170
ROW_Y0 = 128
ROW_H = 16
STORY_ROW = 2                                   # FROTZ.O88, SAVES, STORY.DAT

# A story that will not fit is REFUSED with a reason on screen (SPEC.md 47/
# 59.4) - the whole image is resident, so a 640KB machine plays the v3 stories
# and turns down the big v8 ones. That is a pass, not a hang, and the harness
# has to be able to tell them apart or the gate reports the design as a bug.
REFUSALS = (
    "Story too large for this machine.",
    "Not enough memory for this story.",
    "No room for the scrollback.",
    "That file has no size to read.",
    "Not a Z-machine story file.",
    "Could not read the story file.",
    "That story is not on this disk.",
)

# Every interpreter notice zio.inc and zwin.inc can put on screen. The REFUSALS
# above are the subset that means "this story is not going to run here".
NOTICES = REFUSALS + (
    "No room to save; play continues.",
    "No room for undo; play continues.",
    "Transcript full - it stops here.",
    "Could not write the save file.",
    "Could not read the save file.",
    "This story is too large to save.",
    "Compressed saves cannot be read.",
    "That save is for another story.",
    "Not a Quetzal saved game.",
    "Press RETURN to choose a file.",
)

BOOT_WAIT = 25.0                                # to a usable desktop
READY_WAIT = 40.0                               # ...and to the story's banner
# How long a story may be SILENT before it is called stuck, and how long one
# turn may run however talkative it is. Silence rather than elapsed time,
# because the two failures are different and a fixed per-turn budget confuses
# them: Photopia answers a single `look` with several screenfuls and took more
# than ninety seconds to do it, which was reported as a hang with a screendump
# showing the story mid-sentence and plainly still working. A story that is
# still putting bytes on the wire is not stuck; one that has stopped is, in a
# second or two rather than a minute.
#
# 150 SECONDS OF IT, and the number is large for a reason worth knowing: THE
# WIRE RUNS AHEAD OF THE GLASS. zh_screenc puts a character on the port at the
# moment the stream produces it, and zw_putc draws it afterwards - so a story
# that prints three screenfuls empties them onto the wire in a moment and then
# goes quiet for as long as it takes to paint them. Photopia does exactly that
# and was reported stuck at a prompt it had already reached, with a screendump
# of it mid-paragraph and plainly working.
# 300 of them, because 150 was not enough under load: every story passes on its
# own, and a back-to-back sweep of fifteen would occasionally call a small one
# stuck with a screendump of it plainly mid-conversation. A false STUCK is the
# expensive failure here - it costs a re-run and reads like a defect - and the
# only thing a larger budget costs is how long a genuine hang takes to admit.
TURN_QUIET = 300.0
TURN_CAP = 900.0                                # a livelock still has to end

# The SYNCHRONISING markers - the five the run's control flow turns on. They
# take no arguments, which is what keeps them distinguishable from the window
# and picture events below without a second pass over the stream.
MARK = re.compile(rb"\[\[ZH:(READY|READ|KEY|HALT|QUIT)\]\]")

# ...and everything else the guest says about its windows (apps/frotz/
# zharness.inc): a name and any number of unsigned decimal arguments.
#
# ANCHORED TO A WHOLE LINE, for the reason zh_mark brackets them in the first
# place: every byte of "[[ZH:UPPER 1 2]]" is printable, so a story could print
# one, and the guest's own defence is that a marker occupies a line of its own.
# A dumped row is wrapped in bars and can therefore never be one, whatever the
# story wrote into that window.
EVENT = re.compile(rb"^\[\[ZH:(/?[A-Z]+)((?: \d+)*)\]\]$", re.M)

# A dumped row: the style it is drawn in, then the text between bars. The bars
# are there because a status line's trailing blank is a fact and every
# transport between the guest and here would otherwise eat it; the style is
# there because a row of spaces in REVERSE is a black bar, and its characters
# and its pixels then disagree without either being wrong.
DUMPROW = re.compile(rb"^(\d+)\|(.*)\|$", re.M)

ZST_REVERSE = 1                                 # apps/frotz/zwin.inc

CELL_W = 8                                      # the kernel font (SPEC.md 6)
CELL_H = 8


# ---------------------------------------------------------------------------
# Naming an opcode. Standard 14: the byte's top bits pick the form, the form
# picks how many bits of it are the number, and the number indexes one of five
# tables. A halt prints the byte; this turns it back into a name.
# ---------------------------------------------------------------------------
OP_2OP = [
    None, "je", "jl", "jg", "dec_chk", "inc_chk", "jin", "test",
    "or", "and", "test_attr", "set_attr", "clear_attr", "store",
    "insert_obj", "loadw", "loadb", "get_prop", "get_prop_addr",
    "get_next_prop", "add", "sub", "mul", "div", "mod", "call_2s",
    "call_2n", "set_colour", "throw", None, None, None,
]
OP_1OP = [
    "jz", "get_sibling", "get_child", "get_parent", "get_prop_len",
    "inc", "dec", "print_addr", "call_1s", "remove_obj", "print_obj",
    "ret", "jump", "print_paddr", "load", "not/call_1n",
]
OP_0OP = [
    "rtrue", "rfalse", "print", "print_ret", "nop", "save", "restore",
    "restart", "ret_popped", "pop/catch", "quit", "new_line",
    "show_status", "verify", "extended", "piracy",
]
OP_VAR = [
    "call/call_vs", "storew", "storeb", "put_prop", "sread/aread",
    "print_char", "print_num", "random", "push", "pull", "split_window",
    "set_window", "call_vs2", "erase_window", "erase_line", "set_cursor",
    "get_cursor", "set_text_style", "buffer_mode", "output_stream",
    "input_stream", "sound_effect", "read_char", "scan_table", "not",
    "call_vn", "call_vn2", "tokenise", "encode_text", "copy_table",
    "print_table", "check_arg_count",
]
OP_EXT = [
    "save", "restore", "log_shift", "art_shift", "set_font", "draw_picture",
    "picture_data", "erase_picture", "set_margins", "save_undo",
    "restore_undo", "print_unicode", "check_unicode", "set_true_colour",
    None, None, "move_window", "window_size", "window_style",
    "get_wind_prop", "scroll_window", "pop_stack", "read_mouse",
    "mouse_window", "push_stack", "put_wind_prop", "print_form",
    "make_menu", "picture_table", "buffer_screen",
]


def name_opcode(raw, second=None):
    """raw = the opcode byte; second = the EXT byte when raw is 0xBE."""
    def pick(table, n, form, note):
        nm = table[n] if n < len(table) and table[n] else "<unassigned>"
        return f"{form}:{n} @{nm}  ({note})"

    if raw == 0xBE:
        n = second if second is not None else 0
        return pick(OP_EXT, n, "EXT", "extended form, v5+")
    if raw >= 0xC0:
        n = raw & 0x1F
        if raw & 0x20:
            return pick(OP_VAR, n, "VAR", "variable form")
        return pick(OP_2OP, n, "2OP", "variable form, 2OP number")
    if raw >= 0x80:
        n = raw & 0x0F
        t = (raw >> 4) & 3
        if t == 3:
            return pick(OP_0OP, n, "0OP", "short form, no operand")
        kind = ("large constant", "small constant", "variable")[t]
        return pick(OP_1OP, n, "1OP", f"short form, {kind}")
    n = raw & 0x1F
    a = "variable" if raw & 0x40 else "small"
    b = "variable" if raw & 0x20 else "small"
    return pick(OP_2OP, n, "2OP", f"long form, {a}/{b}")


HALT_RE = re.compile(
    r"opcode 0x([0-9A-F]{4}) byte 0x([0-9A-F]{4}) at 0x([0-9A-F]+)", re.I)


def explain_halt(text):
    m = HALT_RE.search(text)
    if not m:
        return None
    op, raw, pc = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
    # The opcode NUMBER is the second byte when the first is 0xBE: an extended
    # opcode is the one form the byte alone cannot name, and v5+ is where the
    # unimplemented ones live, so this is exactly the case worth getting right.
    return f"byte 0x{raw:02X} -> {name_opcode(raw, op)}   [pc 0x{pc}]"


# ---------------------------------------------------------------------------
# The machine
# ---------------------------------------------------------------------------
def hmp(*cmds):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "qmp.py"),
                    QMP, *cmds], check=True, capture_output=True)


def screendump(path):
    """A PNG of the screen, through tools/shot.py.

    QEMU's own screendump writes a PPM, which nothing on this host opens. Every
    STUCK run leaves one, because the transcript cannot answer the only
    question that matters when a story stops mid-sentence: is the machine hung,
    or is it waiting at something the harness does not know how to answer?
    """
    try:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "shot.py"),
                        QMP, os.path.abspath(path)], check=True,
                       capture_output=True)
        return path
    except Exception:
        return None


def screen_pixels():
    """The screen as (w, h, RGB bytes), through QEMU's own screendump.

    tools/shot.py writes a PNG for a person to look at; this wants the pixels
    themselves, so it borrows that file's P6 reader rather than growing a
    second one. QEMU resolves the path against ITS cwd, so it has to be
    absolute.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import shot
    ppm = os.path.join(ZHDIR, "frame.ppm")
    os.makedirs(ZHDIR, exist_ok=True)
    hmp(f'screendump "{os.path.abspath(ppm)}"')
    try:
        return shot.read_ppm(ppm)
    finally:
        try:
            os.unlink(ppm)
        except OSError:
            pass


def mouse(*args):
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mouse.py"),
                    QMP, *args], check=True, capture_output=True)


def dblclick(x, y):
    """Two presses inside the kernel's 9-tick window (docs/TESTING.md).

    Not two `mouse.py click`s: each of those ends in a settle long enough that
    the guest decodes two single clicks, and a file row then SELECTS instead of
    launching - with nothing anywhere saying so.
    """
    mouse("to", str(x), str(y))
    for _ in range(2):
        hmp("mouse_button 1")
        time.sleep(0.08)
        hmp("mouse_button 0")
        time.sleep(0.10)


def kill_stale():
    subprocess.run(["pkill", "-f", "qemu-system-i386"], capture_output=True)
    time.sleep(0.8)
    for p in (PIDFILE, QMP, ZHSOCK):
        try:
            os.remove(p)
        except FileNotFoundError:
            pass


def build_image(story_path, name):
    """A 1.44MB B: disk: the harness interpreter and the story, as STORY.DAT.

    Renamed because the guest opens a fixed name, which is what lets one
    -DZHARNESS binary play every story without a way to pass it an argument.
    """
    stage = os.path.join(ZHDIR, "stage")
    if os.path.isdir(stage):
        shutil.rmtree(stage)                    # no art left from a past story
    os.makedirs(stage, exist_ok=True)
    shutil.copyfile(story_path, os.path.join(stage, "STORY.DAT"))
    files = [os.path.join(stage, "STORY.DAT")]

    # ART TRAVELS WITH THE STORY, AND UNDER THE STORY'S NAME. zp_mkname derives
    # the archive's name from [zi_story] (apps/frotz/zpic.inc), so a story
    # renamed to STORY.DAT looks for STORY.PIX and would never find BRONZE.PIX
    # sitting beside it. Two places are searched: next to the story, which is
    # where tools/zpicgen.py leaves the fixture's, and build/, which is where
    # the Makefile leaves the one it builds out of Bronze's Blorb.
    stem = os.path.splitext(os.path.basename(story_path))[0]
    for art in (os.path.splitext(story_path)[0] + ".PIX",
                os.path.join(BUILD, stem.upper() + ".PIX")):
        if os.path.exists(art):
            shutil.copyfile(art, os.path.join(stage, "STORY.PIX"))
            files.append(os.path.join(stage, "STORY.PIX"))
            break

    img = os.path.join(ZHDIR, name + ".img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", img, "--size", "1440",
                    os.path.join(ZHDIR, "frotz.o88"), *files,
                    "--folder", "SAVES"],
                   check=True, capture_output=True)
    return img


def boot(img):
    kill_stale()
    subprocess.run(["make", "-s", "zhboot", "ZHIMG=" + img], cwd=ROOT,
                   check=True, capture_output=True)
    deadline = time.time() + BOOT_WAIT
    while time.time() < deadline:
        if os.path.exists(QMP) and os.path.exists(ZHSOCK):
            break
        time.sleep(0.2)
    else:
        raise RuntimeError("zharness: QEMU never opened its sockets")
    time.sleep(6.0)                             # to a drawn desktop


def launch():
    """The GUI walk: Disk B, then the story. os8088 has no other way in."""
    dblclick(*DISK_B)
    time.sleep(2.5)
    dblclick(ROW_X, ROW_Y0 + STORY_ROW * ROW_H)


# ---------------------------------------------------------------------------
# What the guest says is on its screen
# ---------------------------------------------------------------------------
GEOM_FIELDS = ("x0", "y0", "w", "h", "tx", "ty", "cols", "rows", "upper",
               "stat", "lead", "uphold")


class Snapshot:
    """The window model at one prompt: geometry, and both windows as text.

    This is what the interpreter BELIEVES is displayed. It is not evidence
    about the screen until it has been compared with the screen, which is what
    check_pixels does - and the pair is the point. A grid holding a quote box
    against a blank band is a drawing defect; an empty grid against a blank
    band is a window-model defect, and the two need opposite fixes.
    """

    def __init__(self, geom, upper, lower, live=None, pending=0, styles=None):
        self.geom = geom                        # {} until a GEOM has arrived
        self.upper = upper                      # list of str, [cols] wide
        self.lower = lower
        self.live = live                        # index into lower, or None
        self.pending = pending                  # bytes of zw_wbuf not yet drawn
        self.styles = styles or []              # ZST_* per row, upper then lower

    def live_row(self):
        """Which screen row the pending word would land on, or None.

        THE ONE ROW WHOSE GLASS STATE IS GENUINELY AMBIGUOUS, and it is worth
        saying why rather than papering over it. zwin draws one FONT_RUN per
        WORD, so a word with no space after it yet - the '>' of a prompt is
        always one - sits in zw_wbuf undrawn. But a REPAINT composes the live
        row through zw_liveinto, which includes zw_wbuf, so any repaint since
        the word was buffered has put it on the glass. Both states are correct
        and the model does not record which happened, so this row is checked
        against both readings, and strictly - pending word and all - at the
        repaint checkpoint, where there is no ambiguity left.
        """
        if self.live is None or not self.pending:
            return None
        index = len(self.upper) + self.live
        return index if 0 <= index < len(self.rows()) else None

    def rows(self):
        """Every text row on screen, top to bottom.

        The v1-3 status line is NOT here: the interpreter draws it with its own
        OSAPI_FONT_RUN and it belongs to neither window (SPEC.md 59.5), so the
        model has no record of it to report.
        """
        return self.upper + self.lower

    def row_y(self, index):
        """The top pixel row of screen row `index` (0 = the very first)."""
        return self.geom["ty"] + index * self.geom["lead"]

    def screen_rows(self):
        """(screen row index, text) for every row the model claims to own."""
        first = self.geom.get("stat", 0)
        return list(enumerate(self.rows(), start=first))


def parse_snapshot(chunk):
    """The last GEOM/UPPER/LOWER block in `chunk`, as a Snapshot.

    The LAST, because a segment of wire may carry more than one - a story that
    reads a character and then a line snapshots twice - and the one that
    describes the screen the host is about to photograph is the newest.
    """
    geom, upper, lower, where = {}, [], [], None
    styles = {"UPPER": [], "LOWER": []}
    live, pending = None, 0
    for m in re.finditer(
            rb"^\[\[ZH:(/?[A-Z]+)((?: \d+)*)\]\]$|^(\d+)\|(.*)\|$",
            chunk, re.M):
        name = m.group(1)
        if name is None:                        # a dumped row
            row = m.group(4).decode("latin-1")
            if where in ("UPPER", "LOWER"):
                (upper if where == "UPPER" else lower).append(row)
                styles[where].append(int(m.group(3)))
            continue
        name = name.decode()
        args = [int(v) for v in m.group(2).split()]
        if name == "GEOM":
            geom = dict(zip(GEOM_FIELDS, args))
        elif name == "UPPER":
            upper, where, styles["UPPER"] = [], "UPPER", []
        elif name == "LOWER":
            lower, where, styles["LOWER"] = [], "LOWER", []
            live = args[2] if len(args) > 2 and args[2] != 0xFFFF else None
            pending = args[3] if len(args) > 3 else 0
        elif name in ("/UPPER", "/LOWER"):
            where = None
    return Snapshot(geom, upper, lower, live, pending,
                    styles["UPPER"] + styles["LOWER"])


def band_colour(pix, w, h, x, y, cw, ch):
    """The one colour this rectangle is, or None if it is not one colour.

    THE TEST IS UNIFORMITY, NOT INK, and that is what makes it survive
    @set_colour and reverse video. A row of spaces is one flat colour whatever
    the story chose - white, black under a reversed status line, blue in
    Photopia - and a row with so much as one glyph in it is not. Counting dark
    pixels instead would call a reversed blank row "full of ink" and a
    white-on-black letter "empty", and both readings are the wrong way round.
    """
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(w, x + cw), min(h, y + ch)
    if x1 <= x0 or y1 <= y0:
        return b""                              # off-screen: nothing to see
    first = pix[(y0 * w + x0) * 3:(y0 * w + x0) * 3 + 3]
    for yy in range(y0, y1):
        row = pix[(yy * w + x0) * 3:(yy * w + x1) * 3]
        for i in range(0, len(row), 3):
            if row[i:i + 3] != first:
                return None
    return first


def band_is_uniform(pix, w, h, x, y, cw, ch):
    return band_colour(pix, w, h, x, y, cw, ch) is not None


def check_pixels(snap, frame, label, repainted=False):
    """Every row the model describes, against the pixels under it.

    Returns a list of complaints. A row the model says holds text must not be a
    flat band of colour, and a row it says is blank must be exactly that. After
    a repaint every row is read strictly; before one, the live row is allowed
    either reading of its pending word (Snapshot.live_row says why).
    """
    if not snap.geom:
        return []
    w, h, pix = frame
    g = snap.geom
    live = None if repainted else snap.live_row()
    bad = []
    # v1-3: the INTERPRETER owes a status line and draws it with its own
    # FONT_RUN, so it belongs to neither window and the model has no record of
    # it to compare (SPEC.md 59.5). What can still be said is that it is there:
    # the Standard requires the row, and a blank one is a defect whatever it
    # was going to say.
    for index in range(g.get("stat", 0)):
        y = snap.row_y(index)
        if band_is_uniform(pix, w, h, g["tx"], y, g["cols"] * CELL_W, CELL_H):
            bad.append(f"{label}: row {index} is the interpreter's own status "
                       f"line (v1-3) and nothing is drawn in it at y={y}")
    for index, text in snap.screen_rows():
        row = index - g.get("stat", 0)
        style = snap.styles[row] if row < len(snap.styles) else 0
        if not text.strip() and style & ZST_REVERSE:
            # A ROW OF SPACES IN REVERSE VIDEO is a bar of background, and how
            # much of the row it covers is not in the model - only the cells
            # the story actually wrote are painted, and the grid records how
            # many but not where they began. An Inform quote box is framed with
            # three of these. Blank characters and inked pixels are both
            # correct here, so the blank side of the test does not apply; the
            # text side still does, on every other row.
            continue
        y = snap.row_y(index)
        flat = band_is_uniform(pix, w, h, g["tx"], y, g["cols"] * CELL_W,
                               CELL_H)
        want = {bool(text.strip())}             # does the model expect ink?
        if row == live:
            trimmed = text.rstrip()[:-snap.pending]
            want.add(bool(trimmed.strip()))
        if (not flat) in want:
            continue
        if flat:
            bad.append(f"{label}: row {index} holds {text.strip()[:40]!r} in "
                       f"the model and nothing is on screen at y={y}")
        else:
            bad.append(f"{label}: row {index} is blank in the model and has "
                       f"something drawn in it at y={y}")
    return bad


class Wire:
    """The guest's teletype: a byte stream with four markers in it."""

    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(ZHSOCK)
        self.s.settimeout(0.25)
        self.buf = b""
        self.wire = bytearray()                 # exactly what the guest sent
        self.log = bytearray()                  # ...and what a person reads
        self.seg = 0                            # wire consumed by a past mark
        self.last_seg = b""                     # ...and the newest one's bytes
        self.snap = None                        # the newest window snapshot
        self.ready = False                      # READY has arrived
        self.prompted = False                   # ...and a READ or KEY after it

    def refused(self):
        """Has the interpreter said, on screen, that it will not run this story?

        A refusal is a PASS (SPEC.md 59.4/47) and it arrives as a notice on the
        wire, so waiting out the silence budget to infer it is both slow and
        backwards - BRONZE.Z8 spent five minutes proving something it had
        already said in words.

        BETWEEN READY AND THE FIRST PROMPT, and both ends matter. The harness
        build opens B:\\STORY.DAT itself and can refuse it BEFORE it says READY,
        so a short-circuit that ignored the lower bound turned the refusal into
        a failed launch - which is a different thing and is reported
        differently. After the first prompt the same sentences may be the story
        quoting the interpreter back at itself.
        """
        if self.prompted or not self.ready:
            return False
        tail = bytes(self.wire[-4096:])
        return any(r.encode("latin-1") in tail for r in REFUSALS)

    def paints(self):
        """How many full repaints the guest has reported so far.

        The repaint check needs to know a repaint HAPPENED. A scroll that had
        nowhere to go repaints nothing, and a check that silently did not run
        is worse than one that failed - it reads as a pass.
        """
        return len(re.findall(rb"\[\[ZH:PAINT\]\]", self.wire))

    def pump(self):
        try:
            d = self.s.recv(4096)
        except socket.timeout:
            return False
        if not d:
            raise RuntimeError("zharness: the guest closed the wire")
        self.buf += d
        self.wire += d
        self.log += d
        return True

    def await_mark(self, timeout):
        """Read until a marker completes. Returns its name, or None on timeout.

        The wire since the PREVIOUS marker is parsed on the way past, so
        self.snap always describes the moment the caller has just reached -
        which is the moment it is entitled to photograph the screen.
        """
        quiet = time.time() + timeout
        hard = time.time() + TURN_CAP
        while time.time() < quiet and time.time() < hard:
            m = MARK.search(self.buf)
            if m:
                end = len(self.wire) - len(self.buf) + m.end()
                self.last_seg = bytes(self.wire[self.seg:end])
                snap = parse_snapshot(self.last_seg)
                if snap.geom:
                    self.snap = snap
                self.seg = end
                self.buf = self.buf[m.end():]
                name = m.group(1).decode()
                if name == "READY":
                    self.ready = True
                elif name in ("READ", "KEY"):
                    self.prompted = True
                return name
            if self.pump():
                quiet = time.time() + timeout   # bytes are progress
                if self.refused():
                    return None                 # said so; no need to time out
        return None

    def send_key(self, ch):
        """One raw character, no return, no line in the log.

        What a "press any key" prompt gets. It must not consume a line of the
        script: a title screen would then swallow the first command and every
        later one would be answering the wrong question.
        """
        self.s.sendall(ch.encode("latin-1"))

    def send(self, line):
        # The command goes into the LOG here and not onto the wire's own
        # record: the guest does not echo (zwin.inc, zw_typec), so this is the
        # only place that knows a line was typed, and the log is for a person.
        self.log += line.encode("latin-1", "replace") + b"\n"
        self.s.sendall(line.encode("latin-1", "replace") + b"\r")

    def text(self):
        """The guest's own output. What a comparison is entitled to look at."""
        return self.wire.decode("latin-1")

    def transcript(self):
        """...and the same thing with the typed lines put back, for reading."""
        return drain_transcript(bytes(self.log))


def drain_transcript(raw):
    """The wire, minus the markers and the window dumps, for a person.

    The dumps stay in the .wire file next to it: they are forty lines per
    prompt and they would bury the prose, which is what this one is for.
    """
    raw = MARK.sub(b"", raw.replace(b"\r", b""))
    raw = EVENT.sub(b"", raw)
    return DUMPROW.sub(b"", raw).decode("latin-1")


# ---------------------------------------------------------------------------
# The graphics gate
# ---------------------------------------------------------------------------
def park_pointer():
    """The mouse, off the window, before anything photographs it.

    The pointer is drawn INTO the framebuffer, so a pointer resting over the
    text would put pixels in a row the model calls blank - a failure report
    about the harness's own cursor.
    """
    mouse("to", "4", "470")
    time.sleep(0.3)


# THERE IS NO forced repaint here, and there was one for a day. It sent
# PgUp/PgDn - which belong to the interpreter and never reach the story - to
# make the window redraw on demand. It also left DREAMHLD.Z8 with no further
# prompt inside a seven-minute budget, every time, while the same run without
# it finished cleanly: either a repaint of a full 41-row window under lock
# contention with the worker is far slower than anything else here, or paging a
# busy window is a real defect. Either way A HARNESS MAY NOT BE THE THING THAT
# BREAKS THE RUN, and it is worth investigating on its own rather than through
# a gate that fails for two reasons at once.
#
# Nothing is lost by dropping it. Stories provoke repaints themselves - any
# @erase_window -1 does, and so does any @split_window that changes the split -
# and repaint_settled below notices, so the strict comparison still happens. It
# is what caught BEAR.Z5, whose @split_window 2 repaints just before the
# prompt. When a run genuinely never repaints, the report says so rather than
# counting a check that did not run as a pass.


def dac6(rgb):
    """A colour as the screen can actually show it.

    The VGA DAC is SIX bits per channel, so palette entry 1 - 0x0000AA in
    tools/os88pix.py's table - leaves the card as 0x0000A8 and that is what a
    screendump reads back. Comparing the archive's byte against the scanout's
    without this reports every picture on the disk as the wrong colour, which
    is a fact about the hardware being read as a defect in the blitter.
    """
    return bytes(v & 0xFC for v in rgb)


def read_expect(story_path):
    """tools/zpicgen.py's picture table, or {} when the story has no art.

    Keyed by picture number -> (palette index, rgb, width, height).
    """
    base = os.path.splitext(story_path)[0] + ".expect"
    if not os.path.exists(base):
        return {}
    out = {}
    for line in open(base):
        if line.startswith("#") or not line.strip():
            continue
        num, index, r, g, b, w, h = (int(v) for v in line.split())
        out[num] = (index, bytes((r, g, b)), w, h)
    return out


def check_pictures(segment, frame, expect, label):
    """Every rectangle the interpreter said it blitted, read back off the glass.

    The guest reports @draw_picture twice: PIC with the number and the origin
    the STORY asked for, and PICAT with the screen rectangle it actually blitted
    into after clamping to the window. Checking the second against the pixels is
    what makes this a picture test rather than a test of our own arithmetic
    about pictures - the interpreter can be wrong about where a picture went,
    and a check that trusted PICAT's coordinates to find PICAT's pixels would
    agree with it either way.

    Every fixture picture is one flat colour (tools/zpicgen.py), so "solidly the
    right colour" is decidable; a picture drawn at the wrong origin, at the
    wrong size, from the wrong directory entry, or not at all, each fails it
    differently and none of them passes.
    """
    if not expect or b"[[ZH:PIC" not in segment:
        return [], set()                        # this turn drew no pictures
    w, h, pix = frame
    problems, num, seen = [], None, set()
    for m in EVENT.finditer(segment):
        name = m.group(1).decode()
        args = [int(v) for v in m.group(2).split()]
        if name == "PIC":
            num = args[0]
        elif name == "PICNO":
            if args[0] in expect:
                problems.append(f"{label}: picture {args[0]} is in the archive "
                                f"and the interpreter refused to draw it")
        elif name == "PICAT" and len(args) == 4:
            px, py, pw, ph = args
            if num not in expect:
                problems.append(f"{label}: picture {num} is not in the archive "
                                f"and something was blitted at {px},{py}")
                continue
            seen.add(num)
            index, rgb, want_w, want_h = expect[num]
            if pw > want_w or ph > want_h:
                problems.append(f"{label}: picture {num} is {want_w}x{want_h} "
                                f"and {pw}x{ph} was blitted")
            got = band_colour(pix, w, h, px, py, pw, ph)
            if got is None:
                problems.append(f"{label}: picture {num} should be a solid "
                                f"block of palette {index} and the "
                                f"{pw}x{ph} at {px},{py} is not one colour")
            elif dac6(got) != dac6(rgb):
                problems.append(f"{label}: picture {num} should be palette "
                                f"{index} {rgb.hex()} and the screen at "
                                f"{px},{py} is {got.hex()}")
    # Which of them turned up is the CALLER's tally, over the whole run rather
    # than over one turn: a story is free to draw picture 1 now and picture 5
    # three commands later, and a per-turn verdict would call each of them
    # missing while the other was on screen.
    return problems, seen


def repaint_settled(segment):
    """Did this prompt arrive on a freshly repainted window, with nothing since?

    A repaint redraws every row from the model, pending word and all, so the
    screen after one can be compared with the model STRICTLY. That is only true
    if nothing was printed afterwards - a word buffered after the repaint is
    undrawn again - so the test is that the tail of the segment, once the
    markers and the window dumps are taken out of it, has no story text in it.

    Stories provoke repaints themselves: @erase_window -1 does, and so does any
    @split_window that changes the split. Noticing that costs nothing and is
    what the repaint check rides on now that nothing forces one.
    """
    cut = segment.rfind(b"[[ZH:PAINT]]")
    if cut < 0:
        return False
    tail = segment[cut + len(b"[[ZH:PAINT]]"):]
    tail = EVENT.sub(b"", tail)
    tail = DUMPROW.sub(b"", tail)
    return not tail.strip()


def check_opening(snap, story_path, stem):
    """The opening screen, against the reference interpreter's own.

    tests/frotz/screens/<stem>.txt is what the curses Frotz has on display at
    this moment, generated by tools/zref.py. The test is that every word of it
    is on OUR screen too, IN ORDER - a subsequence rather than an equality,
    because our window carries scrollback the terminal has already lost and
    extra content is not a defect. A quote box we drew and threw away is one:
    its words are simply absent.
    """
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import zref
    gold = zref.read_golden(os.path.join(SCREENDIR, stem.lower() + ".txt"))
    if gold is None:
        return None, "no golden screen; `make zscreens` writes one"
    cols, rows, shots = gold
    if snap.geom.get("cols") != cols or snap.geom.get("rows") != rows:
        return None, (f"the golden was taken at {cols}x{rows} and this window "
                      f"is {snap.geom.get('cols')}x{snap.geom.get('rows')} - "
                      f"run `make zscreens`")
    # The reference's first row is the v1-3 status line, and ours is not in
    # either window for the model to report (SPEC.md 59.5). Its PRESENCE is
    # checked against the pixels instead; its wording is the one thing on the
    # screen this comparison cannot reach, and saying so beats a heuristic that
    # tries to guess which words were the score.
    stat = snap.geom.get("stat", 0)
    # The words BOTH takes of the reference agree on. A story may choose its
    # opening with @random - CURSES.Z5 draws a different epigraph every time,
    # and both interpreters seed from the clock (Standard 2.4) - so the words
    # it rolled for cannot be required of anyone. What is left is everything it
    # actually settled on, and that is still the whole of most openings.
    want = zref.words(shots[0][stat:])
    for extra in shots[1:]:
        want = zref.common(want, zref.words(extra[stat:]))
    have = zref.words(snap.rows())
    i = 0
    for word in have:
        if i < len(want) and word == want[i]:
            i += 1
    if i == len(want):
        return True, f"{len(want)} words of the opening screen, all present"
    missing = " ".join(want[i:i + 10])
    return False, (f"the opening screen is missing {len(want) - i} of "
                   f"{len(want)} words the reference shows, from: {missing!r}")


# ---------------------------------------------------------------------------
# Playing
# ---------------------------------------------------------------------------
KEY_LIMIT = 40


def next_prompt(w, at_prompt=None):
    """Await the next marker, answering key-prompts ourselves.

    Returns READ, HALT, QUIT, or None on a timeout. KEY never reaches the
    caller - it is not a turn. `at_prompt` is called at BOTH, before the key
    prompt is answered, because a title screen is only on display until it is:
    the graphics checks have to run while the thing they are about is still up.

    BOUNDED, because "answer every key prompt" is a loop with no natural end.
    A story sitting in a @read_char loop - a menu whose space we are not
    answering the way it wants, or a title screen that re-asks - would be
    answered for ever, and the run would look like a slow story rather than a
    stuck one. Past KEY_LIMIT in a row it is a hang and is reported as one.
    """
    keys = 0
    while True:
        m = w.await_mark(TURN_QUIET)
        if m in ("READ", "KEY") and at_prompt:
            at_prompt(m)
        if m != "KEY":
            return m
        keys += 1
        if keys > KEY_LIMIT:
            return None
        w.send_key(" ")


def default_script():
    """What to type at a story nobody has written a script for.

    Deliberately dull and universally legal: look, inventory, a few moves and
    a couple of the verbs every library implements. The point is to get a few
    hundred thousand instructions executed through the parser, the object tree
    and the text decoder, not to win.
    """
    return ["look", "inventory", "north", "south", "east", "west",
            "look", "wait", "examine me", "inventory", "up", "down", "look"]


def read_script(path):
    """A command file: one line per turn, # for a comment.

    There is deliberately no way to opt a story out of the diff. One existed
    for a day, for the stories whose upper window carries a quote box rather
    than a status line - and then normalise() learned to recognise positioned
    text by its padding and every one of them compared. An opt-out is a place
    for a real divergence to hide, and the two it would have hidden are both
    real.
    """
    return [ln.rstrip("\n") for ln in open(path)
            if ln.strip() and not ln.startswith("#")]


def load_script(stem):
    p = os.path.join(SCRIPTDIR, stem.lower() + ".txt")
    if os.path.exists(p):
        return read_script(p)
    return default_script()


class Graphics:
    """The graphics checks, run at every prompt of one story's run.

    Three questions, and they are deliberately different questions:

      1. does the SCREEN agree with the MODEL - is every row the interpreter
         says holds text actually drawn, and every row it says is blank
         actually blank. This needs no reference interpreter and no golden: it
         is the drawing path against its own bookkeeping.
      2. does the model survive a REPAINT. An uncover or a resize redraws the
         window from that bookkeeping, so anything on screen the model does not
         hold disappears at the first one - which is the defect a person
         describes as "it flashes up and then it is gone".
      3. is the OPENING SCREEN what a real interpreter shows. This is the only
         one that needs a golden, and it is the only one that can catch content
         the model never held in the first place - a quote box that was drawn,
         discarded, and correctly not redrawn.
    """

    def __init__(self, stem, story_path, verbose=True):
        self.stem = stem
        self.story_path = story_path
        self.verbose = verbose
        self.problems = []
        self.prompts = 0
        self.repainted = False                  # a repaint was really observed
        self.opening = None                     # True / False / None = not run
        self.opening_note = "not reached"
        self.parked = False
        self.shots = 0
        self.expect = read_expect(story_path)
        self.drew = set()                       # picture numbers actually seen

    def at_prompt(self, w, kind):
        self.prompts += 1
        if not self.parked:
            park_pointer()
            self.parked = True

        # A repaint the STORY provoked is the only kind taken now, and it
        # arrives free on any story that erases the screen or moves its split.
        strict = repaint_settled(w.last_seg)
        label = f"prompt {self.prompts} ({kind})" + \
                (", on a repainted window" if strict else "")
        frame = screen_pixels()

        # PICTURES FIRST, because they are the one check that does not need the
        # character-window model - and v6, which is the only version that has
        # @draw_picture, is the one version that does not have that model
        # (SPEC.md 59.2: v6 replaces the whole thing with zwin6.inc).
        if self.expect and b"[[ZH:PIC" in w.last_seg:
            found, seen = check_pictures(w.last_seg, frame, self.expect, label)
            self.drew |= seen
            self.note(found, frame, label)

        snap = w.snap
        if snap is None or not snap.geom:
            return
        self.note(check_pixels(snap, frame, label, repainted=strict), frame,
                  label)
        if strict:
            self.repainted = True

        if self.prompts == 1:
            ok, note = check_opening(snap, self.story_path, self.stem)
            self.opening, self.opening_note = ok, note

    def note(self, found, frame, label):
        """Record complaints, and KEEP THE PICTURE that goes with them.

        A row named as blank is not a thing anyone can act on. The frame it was
        blank in is - and by the time the run ends the machine is gone, so it
        has to be written here or not at all.
        """
        if not found:
            return
        self.problems += found
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import shot
        w_, h_, pix = frame
        png = os.path.join(ZHDIR, f"{self.stem.lower()}-gfx{self.shots}.png")
        shot.png(png, w_, h_, pix)
        self.shots += 1
        self.problems.append(f"    ...the screen it happened on -> {png}")

    def report(self):
        for p in self.problems:
            print(f"zharness:     {p}")
        if self.opening is True:
            print(f"zharness:   opening screen: {self.opening_note}")
        elif self.opening is False:
            print(f"zharness:   OPENING SCREEN DIVERGED: {self.opening_note}")
        else:
            print(f"zharness:   opening screen not compared: "
                  f"{self.opening_note}")
        if not self.repainted:
            print("zharness:   the repaint check never ran - this story never "
                  "repainted its own window")
        missed = sorted(set(self.expect) - self.drew)
        if missed:
            print(f"zharness:   PICTURES NEVER DREW: {missed} are in the "
                  f"archive and nothing was ever blitted for them")
        elif self.expect:
            print(f"zharness:   {len(self.expect)} pictures drawn and read "
                  f"back off the screen")
        return bool(self.problems) or self.opening is False or bool(missed)


def play(story_path, script, shot=None, verbose=True, graphics=None):
    """Boot, launch, play the script. Returns (outcome, transcript, turns)."""
    stem = os.path.splitext(os.path.basename(story_path))[0]
    img = build_image(story_path, stem.lower())
    boot(img)
    w = Wire()                                  # BEFORE the launch: the chardev
    launch()                                    # is server=on,wait=off, so
                                                # anything the guest sends with
                                                # nobody attached is DROPPED -
                                                # and READY is the first thing
                                                # it sends. Connecting after
                                                # the double-click reads as the
                                                # story having failed to open,
                                                # with a screendump showing it
                                                # open and waiting.
    if w.await_mark(READY_WAIT) != "READY":
        # ONE RETRY OF THE WALK, because the walk is the fragile part and it
        # fails in a way that is not about the interpreter at all: a
        # double-click that lands before the desktop has finished drawing is
        # decoded as two single clicks, and the run ends with a screendump of a
        # bare desktop with the icon not even selected. Re-doing it costs a few
        # seconds on the rare miss and turns a spurious red gate into a pass.
        # A second failure is reported as it always was.
        print("zharness:   the launch did not land; walking it again",
              flush=True)
        launch()
        if w.await_mark(READY_WAIT) != "READY":
            screendump(shot or os.path.join(ZHDIR,
                                            stem.lower() + "-nolaunch.png"))
            return ("NOLAUNCH", w.text(), w.transcript(), 0)

    hook = (lambda kind: graphics.at_prompt(w, kind)) if graphics else None

    turns = 0
    outcome = "OK"
    for line in script:
        mark = next_prompt(w, hook)
        if mark is None:
            outcome = "STUCK"
            break
        if mark in ("HALT", "QUIT"):
            outcome = mark
            break
        w.send(line)
        turns += 1
        if verbose:
            print(f"  > {line}", flush=True)
    else:
        # The script ran out. One more marker settles whether the last command
        # completed cleanly or took the story down with it.
        mark = next_prompt(w, hook)
        if mark in ("HALT", "QUIT"):
            outcome = mark
        elif mark is None:
            outcome = "STUCK"

    for _ in range(8):                          # let the tail arrive
        if not w.pump():
            break
    # The RAW wire, markers and all. The transcript below is the prose a person
    # reads; this is the evidence - which window each character went to, every
    # split, every repaint, every picture - and a graphics failure is only
    # diagnosable from it.
    with open(os.path.join(ZHDIR, stem.lower() + ".wire"), "w") as f:
        f.write(w.text())
    if outcome == "STUCK":
        shot_at = screendump(os.path.join(ZHDIR, stem.lower() + "-stuck.png"))
        if shot_at:
            print(f"zharness:   the screen at the moment it stopped -> {shot_at}")
    try:
        hmp("quit")
    except Exception:
        pass
    return (outcome, w.text(), w.transcript(), turns)


# ---------------------------------------------------------------------------
# The reference
# ---------------------------------------------------------------------------
def normalise(text):
    """Both transcripts, reduced to what they actually claim.

    Whitespace and line breaks are NOT comparable between the two: dfrotz wraps
    at its -w, os8088 wraps at whatever the window is, and the harness sends
    the pre-wrap stream. Status lines, the [MORE] prompt and the version
    banners differ by design. So this compares the WORDS in order, which is
    what a wrong opcode changes and a different column count does not.
    """
    raw = MARK.sub(b"", text.encode("latin-1"))
    # ...and the window wire with it. The upper/lower dumps are this harness
    # talking to itself about the screen; they are not the story's words, and
    # a story is not diverging because its own status line got counted twice.
    raw = EVENT.sub(b"", raw)
    raw = DUMPROW.sub(b"", raw)
    text = raw.decode("latin-1")
    text = re.sub(r"\[Frotz:.*", "", text)
    # dfrotz's own two lines of preamble, and the right-hand half of the status
    # line. os8088 composes the score and the move count with a separate
    # OSAPI_FONT_RUN and never puts them through the character stream, so they
    # are not text either interpreter is claiming - only the room name is.
    text = re.sub(r"^Using normal formatting\.\s*$", "", text, flags=re.M)
    text = re.sub(r"^Loading .*\.\s*$", "", text, flags=re.M)
    # THE UPPER WINDOW, on the reference side, identified by its padding.
    # dfrotz places upper-window text with spaces - a status line right-aligns
    # its score field, a quote box indents to centre itself - so a run of three
    # or more spaces is the signature of something POSITIONED rather than
    # printed. Its own prose never has one: it wraps at -w and separates words
    # by one space. The harness keeps the upper window off the wire entirely
    # (SPEC.md 59.13), so this is what makes the two sides the same text.
    #
    # It fails SAFE. A story that indents in the LOWER window loses those words
    # from the reference and keeps them here, which reports a divergence - the
    # error is a false alarm to investigate, never a silent pass.
    text = re.sub(r"^.*(?:\S[ ]{3,}\S|^[ ]{3,}\S).*$", "", text, flags=re.M)
    # dfrotz's own diagnostics, which are about the STORY and not about either
    # interpreter's answer - Balances calls @get_child on object 0 and says so
    # every turn. os8088 answers the same 0 and does not editorialise (§59.11:
    # the object layer has no error channel), so this is a difference in
    # commentary. A halt is different and is never stripped: it is the answer.
    # A dfrotz warning is a PARAGRAPH, not a line: it wraps at -w like anything
    # else, so "...(will ignore further\noccurrences)" left the word
    # "occurrences" behind when this matched one line, and every Balances run
    # diverged on it. Take it to the blank line that ends it.
    text = re.sub(r"^[ \t]*Warning:.*?(?:\n[ \t]*\n|\Z)", "", text,
                  flags=re.M | re.S)
    # ...and os8088's own notices, for the mirror-image reason. "No room for
    # undo; play continues." is this interpreter telling the player what it did
    # about a claim it could not take (SPEC.md 47); it is not the story
    # speaking, and dfrotz - which has the whole host to allocate in - has
    # nothing to say. A HALT is not in this list and never will be: a halt is
    # the answer, not commentary about it.
    for notice in NOTICES:
        text = text.replace(notice, "")
    text = re.sub(r"[^A-Za-z0-9'\-]+", " ", text)
    return text.lower().split()


def dfrotz_gold(story_path, script):
    if not shutil.which("dfrotz"):
        return None
    inp = "".join(line + "\n" for line in script)
    try:
        r = subprocess.run(["dfrotz", "-w", "80", "-h", "200", "-p",
                            story_path], input=inp, capture_output=True,
                           text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None
    return r.stdout


def compare(ours, gold):
    """First divergence, as (index, ours-word, gold-word), or None."""
    a, b = normalise(ours), normalise(gold)
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return (i, " ".join(a[max(0, i - 12):i + 8]),
                    " ".join(b[max(0, i - 12):i + 8]))
    if len(a) < len(b) * 0.9:
        return (n, "<ours ended early: %d words>" % len(a),
                "<reference had %d>" % len(b))
    return None


# ---------------------------------------------------------------------------
def resolve(name):
    if os.path.exists(name):
        return name
    p = os.path.join(STORYDIR, name)
    if os.path.exists(p):
        return p
    raise SystemExit(f"zharness: no story {name!r} (try `make stories`)")


def all_stories():
    if not os.path.isdir(STORYDIR):
        raise SystemExit("zharness: no build/stories - run `make stories`")
    return sorted(os.path.join(STORYDIR, f) for f in os.listdir(STORYDIR)
                  if re.search(r"\.(z[1-8]|dat)$", f, re.I))


def repl(story_path):
    """Boot once and hand the keyboard over. The fast development loop."""
    stem = os.path.splitext(os.path.basename(story_path))[0]
    img = build_image(story_path, stem.lower())
    boot(img)
    w = Wire()                                  # before the walk; see play()
    launch()
    if w.await_mark(READY_WAIT) != "READY":
        print(w.text())
        raise SystemExit("zharness: the story never opened")
    print(f"--- {os.path.basename(story_path)} on os8088. Ctrl-D to stop. ---")
    seen = 0
    while True:
        mark = next_prompt(w)
        out = drain_transcript(bytes(w.wire[seen:]))
        seen = len(w.wire)
        sys.stdout.write(out)
        sys.stdout.flush()
        if mark in ("HALT", "QUIT", None):
            print(f"\n--- {mark or 'STUCK'} ---")
            why = explain_halt(w.text())
            if why:
                print("    " + why)
            break
        try:
            line = input()
        except EOFError:
            break
        w.send(line)
    try:
        hmp("quit")
    except Exception:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("story", nargs="?", help="a name in build/stories, or a path")
    ap.add_argument("--all", action="store_true", help="every story there is")
    ap.add_argument("--compare", action="store_true",
                    help="diff the transcript against dfrotz")
    ap.add_argument("--repl", action="store_true",
                    help="boot it and type at it yourself")
    ap.add_argument("--graphics", action="store_true",
                    help="check the windows: model against pixels, across a "
                         "repaint, and the opening screen against the "
                         "reference interpreter's")
    ap.add_argument("--script", help="a command file, one line per turn")
    ap.add_argument("--shot", help="screendump here if the launch never lands")
    args = ap.parse_args()

    if not os.path.exists(os.path.join(ZHDIR, "frotz.o88")):
        raise SystemExit("zharness: no build/zh/frotz.o88 - run `make zh`")

    if args.repl:
        if not args.story:
            raise SystemExit("zharness: --repl needs a story")
        return repl(resolve(args.story))

    targets = all_stories() if args.all else [resolve(args.story or "ADVENT.Z3")]
    failures = []
    for path in targets:
        stem = os.path.splitext(os.path.basename(path))[0]
        if args.script:
            script = read_script(args.script)
        else:
            script = load_script(stem)

        print(f"zharness: {os.path.basename(path)} "
              f"({len(script)} commands)", flush=True)
        gfx = Graphics(stem, path, verbose=not args.all) if args.graphics \
            else None
        outcome, raw, transcript, turns = play(path, script, shot=args.shot,
                                               verbose=not args.all,
                                               graphics=gfx)

        log = os.path.join(ZHDIR, stem.lower() + ".log")
        with open(log, "w") as f:
            f.write(transcript)

        if outcome == "STUCK" and turns == 0:
            said = next((r for r in REFUSALS if r in raw), None)
            if said:
                outcome = "REFUSED"
                print(f"zharness:   refused, with a reason: {said}")

        if outcome == "OK":
            print(f"zharness:   ran {turns} commands, no halt -> {log}")
        elif outcome == "REFUSED":
            pass                                # already said, and it is a pass
        elif outcome == "QUIT":
            print(f"zharness:   the story ended after {turns} commands -> {log}")
        else:
            failures.append(stem)
            print(f"zharness:   {outcome} after {turns} commands -> {log}")
            why = explain_halt(raw)
            if why:
                print(f"zharness:   {why}")
            for line in raw.splitlines():
                if "[Frotz:" in line or "opcode 0x" in line:
                    print("zharness:   " + line.strip())

        if outcome == "REFUSED":
            continue                            # nothing ran; nothing to look at

        if gfx and gfx.report():
            failures.append(stem + " (graphics)")

        if args.compare and outcome in ("OK", "QUIT"):
            gold = dfrotz_gold(path, script[:turns])
            if gold is None:
                print("zharness:   no dfrotz on this host; not compared")
            else:
                d = compare(raw, gold)
                if d is None:
                    print("zharness:   matches dfrotz")
                else:
                    failures.append(stem + " (diverged)")
                    print(f"zharness:   DIVERGED at word {d[0]}")
                    print(f"zharness:     ours: ...{d[1]}...")
                    print(f"zharness:     gold: ...{d[2]}...")

    if failures:
        print(f"zharness: {len(failures)} failed: {', '.join(failures)}")
        return 1
    print("zharness: all clear")
    return 0


if __name__ == "__main__":
    sys.exit(main())
