#!/usr/bin/env python3
"""Play a Z-machine story on os8088's Frotz, over a wire, unattended.

    make zh                                     # build the harness interpreter
    python3 tools/zharness.py ADVENT.Z3         # play its script, print the log
    python3 tools/zharness.py ADVENT.Z3 --repl  # ...or type at it yourself
    python3 tools/zharness.py --all --compare   # every story, diffed vs dfrotz
    make zcheck                                 # the same thing, as a gate

WHAT THIS IS FOR. `make ztest` asks whether an opcode is right one opcode at a
time, against tests/frotz/zopstest.inf - a story written to be a test. This
asks the other question, which that one cannot: does a REAL story run. The
answers differ. zopstest found @div's rounding; this found a branch that
decoded correctly, executed correctly, and left the program counter in a form
the next instruction's guard rejected.

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
TURN_WAIT = 90.0                                # one command; a v8 game is slow
MARK = re.compile(rb"\[\[ZH:(READY|READ|KEY|HALT|QUIT)\]\]")


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
    os.makedirs(stage, exist_ok=True)
    shutil.copyfile(story_path, os.path.join(stage, "STORY.DAT"))
    img = os.path.join(ZHDIR, name + ".img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", img, "--size", "1440",
                    os.path.join(ZHDIR, "frotz.o88"),
                    os.path.join(stage, "STORY.DAT"), "--folder", "SAVES"],
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


class Wire:
    """The guest's teletype: a byte stream with four markers in it."""

    def __init__(self):
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(ZHSOCK)
        self.s.settimeout(0.25)
        self.buf = b""
        self.wire = bytearray()                 # exactly what the guest sent
        self.log = bytearray()                  # ...and what a person reads

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
        """Read until a marker completes. Returns its name, or None on timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            m = MARK.search(self.buf)
            if m:
                self.buf = self.buf[m.end():]
                return m.group(1).decode()
            self.pump()
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
    """The wire, minus the markers, as something a person reads."""
    return MARK.sub(b"", raw.replace(b"\r", b"")).decode("latin-1")


# ---------------------------------------------------------------------------
# Playing
# ---------------------------------------------------------------------------
KEY_LIMIT = 40


def next_prompt(w):
    """Await the next marker, answering key-prompts ourselves.

    Returns READ, HALT, QUIT, or None on a timeout. KEY never reaches the
    caller - it is not a turn.

    BOUNDED, because "answer every key prompt" is a loop with no natural end.
    A story sitting in a @read_char loop - a menu whose space we are not
    answering the way it wants, or a title screen that re-asks - would be
    answered for ever, and the run would look like a slow story rather than a
    stuck one. Past KEY_LIMIT in a row it is a hang and is reported as one.
    """
    keys = 0
    while True:
        m = w.await_mark(TURN_WAIT)
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


def play(story_path, script, shot=None, verbose=True):
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
        screendump(shot or os.path.join(ZHDIR, stem.lower() + "-nolaunch.png"))
        return ("NOLAUNCH", w.text(), w.transcript(), 0)

    turns = 0
    outcome = "OK"
    for line in script:
        mark = next_prompt(w)
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
        mark = next_prompt(w)
        if mark in ("HALT", "QUIT"):
            outcome = mark
        elif mark is None:
            outcome = "STUCK"

    for _ in range(8):                          # let the tail arrive
        if not w.pump():
            break
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
    text = MARK.sub(b"", text.encode("latin-1")).decode("latin-1")
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
        outcome, raw, transcript, turns = play(path, script, shot=args.shot,
                                               verbose=not args.all)

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
            continue

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
