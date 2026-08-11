#!/usr/bin/env python3
"""Photograph a reference interpreter's SCREEN, and write it out as a golden.

    make zscreens                             # regenerate every golden
    python3 tools/zref.py BEAR.Z5             # ...or one, to stdout
    python3 tools/zref.py --all -o tests/frotz/screens

WHY A SCREEN AND NOT A TRANSCRIPT. `dfrotz -p` answers what the story PRINTED,
which is what tools/zharness.py already diffs, and it is structurally unable to
answer what the reader can SEE. The two differ exactly where this project's
defects live: a quote box, a title screen or a status line is text positioned
in a window, and a story that draws one and then loses it prints precisely the
same characters as one that keeps it. BEAR.Z5 was blank at the top of its
window for a fortnight with a transcript that matched dfrotz word for word.

So this runs the REAL Frotz - the curses one, the one whose screen is a screen
- inside a pseudo-terminal, models that terminal, and writes down which words
are on display at each point in the script. tools/zharness.py then requires
every one of them to be on os8088's screen too (as a subsequence: our window
carries scrollback the terminal has lost, and extra content is not an error).

THE GOLDEN IS COMMITTED, AND THAT IS THE POINT. This tool needs `frotz` and
`pyte`, neither of which the rest of the toolchain does, and CLAUDE.md's
toolchain is deliberately nasm + qemu + python3. Generating at gate time would
make the gate depend on both; generating HERE, into tests/frotz/screens, means
the gate depends on a text file. The gold is still generated rather than
hand-written, which is the rule `make ztest` already follows.

    brew install frotz && pip3 install pyte      # to REGENERATE, never to gate

GEOMETRY IS PART OF THE GOLDEN and is written into its header. Word wrap
decides how many rows a paragraph takes, which decides what has scrolled off
the top, so a screen taken at 80 columns is not a fact about a 67-column
window. zharness.py compares the header against the live window and refuses,
with the reason, rather than reporting the mismatch as a story failure.

ONE SCREEN PER STORY: THE OPENING, and the restraint is deliberate. Driving
the reference deeper means replaying the guest's input, and the guest's input
is not known here - a "press any key" prompt is answered by the harness itself
and consumes no line of the script, so a story that asks for two leaves the
reference a turn behind and every checkpoint after it compares two different
moments in the story. That failure would look exactly like the defect this is
for. The opening needs no input at all, is where every title screen, quote box
and cover picture lives, and is therefore the one checkpoint that is free of
the problem. Everything after it is covered by the model-against-pixels checks
in zharness.py, which need no reference interpreter at all.
"""
import argparse
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STORYDIR = os.path.join(ROOT, "build", "stories")
SCREENDIR = os.path.join(ROOT, "tests", "frotz", "screens")

# The os8088 window the harness boots, in characters. zharness.py checks the
# live [zw_cols]/[zw_rows] against these and says so if they have moved.
REF_COLS = 67
REF_ROWS = 41

SETTLE = 0.6                                    # quiet before a screen is read
SETTLE_MAX = 20.0                               # ...and how long to wait at all
MORE_LIMIT = 12                                 # pages of opening to walk past


def capture(story, inputs, cols=REF_COLS, rows=REF_ROWS):
    """Run frotz on a pty and return one screen per checkpoint.

    `inputs` is the sequence the guest was given, in order, each either a bare
    character (a "press any key" prompt) or a whole command line. Replaying the
    SAME sequence is what keeps the two interpreters at the same point in the
    story; anything cleverer - watching for a prompt, guessing at a delay -
    puts the reference one turn away from us and reports it as a divergence.

    Returns [screen_before_any_input, screen_after_inputs[0], ...], each a list
    of `rows` strings.
    """
    import fcntl
    import pty
    import struct
    import termios

    import pyte

    screen = pyte.Screen(cols, rows)
    stream = pyte.ByteStream(screen)

    pid, fd = pty.fork()
    if pid == 0:                                # the child IS the terminal
        os.environ["TERM"] = "xterm"
        os.environ["LINES"], os.environ["COLUMNS"] = str(rows), str(cols)
        try:
            os.execvp("frotz", ["frotz", story])
        finally:
            os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def quiesce():
        """Feed output until it stops arriving. The screen is what is left."""
        import select
        quiet_since = None
        deadline = time.time() + SETTLE_MAX
        while time.time() < deadline:
            r, _, _ = select.select([fd], [], [], 0.15)
            if r:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    break
                if not data:
                    break
                stream.feed(data)
                quiet_since = None
            else:
                quiet_since = quiet_since or time.time()
                if time.time() - quiet_since >= SETTLE:
                    break
        return [line.rstrip() for line in screen.display]

    def settle():
        """...and page through [MORE], because the guest does not.

        THE HARNESS BUILD DOES NOT PAGE (SPEC.md 61.13): `[MORE]` waits on a
        key and the harness's "person" is a script that types only when the
        story asks for a command, so leaving it in deadlocks the run. The
        consequence here is that a story whose opening is longer than one
        screen leaves the two interpreters on DIFFERENT PAGES of it - `ZTUU.Z5`
        opens with a letter about a screen and a half long, and comparing our
        last page with the reference's first found nothing in common and called
        it a divergence.

        So the reference is paged through to the same place. It is not an
        allowance: both interpreters end up showing the end of the opening,
        which is what a reader who pressed space would be looking at.
        """
        shot = quiesce()
        for _ in range(MORE_LIMIT):
            if not any(line.rstrip().endswith("[MORE]") for line in shot):
                return shot
            os.write(fd, b" ")
            shot = quiesce()
        return shot

    shots = [settle()]
    for item in inputs:
        payload = item.encode("latin-1", "replace")
        if len(item) != 1:
            payload += b"\n"
        try:
            os.write(fd, payload)
        except OSError:
            break
        shots.append(settle())
    try:
        os.write(fd, b"\x03")
        os.close(fd)
    except OSError:
        pass
    try:
        os.waitpid(pid, os.WNOHANG)
    except OSError:
        pass
    return shots


# ---------------------------------------------------------------------------
# The golden file
# ---------------------------------------------------------------------------
HDR = re.compile(r"^#\s*geometry\s+cols=(\d+)\s+rows=(\d+)\s*$")
CHECKPOINT = "=== checkpoint "


def write_golden(path, story_name, cols, rows, shots):
    with open(path, "w") as f:
        f.write(f"# {story_name} - the OPENING screen of the reference\n")
        f.write("# interpreter, word for word. Generated by tools/zref.py;\n")
        f.write("# do not hand-edit. `make zscreens` takes it again.\n")
        f.write("#\n")
        f.write("# TWO takes of the same opening. A story is free to choose\n")
        f.write("# its opening with @random - CURSES.Z5 draws a different\n")
        f.write("# epigraph every time - and both interpreters seed from the\n")
        f.write("# clock (Standard 2.4), so no single screen can be the\n")
        f.write("# answer. tools/zharness.py requires the words COMMON to\n")
        f.write("# both takes, in order: everything the story settled on\n")
        f.write("# stays in the gate, and everything it rolled for drops out\n")
        f.write("# without anyone having to declare which was which.\n")
        f.write(f"# geometry cols={cols} rows={rows}\n")
        for i, shot in enumerate(shots):
            f.write(f"{CHECKPOINT}{i} (the opening, before any input)\n")
            for line in shot:
                f.write(line + "\n")


def read_golden(path):
    """-> (cols, rows, [screen, ...]) or None when there is no golden."""
    if not os.path.exists(path):
        return None
    cols = rows = None
    shots, cur = [], None
    for line in open(path):
        line = line.rstrip("\n")
        m = HDR.match(line)
        if m:
            cols, rows = int(m.group(1)), int(m.group(2))
            continue
        if line.startswith("#"):
            continue
        if line.startswith(CHECKPOINT):
            cur = []
            shots.append(cur)
            continue
        if cur is not None:
            cur.append(line)
    if cols is None or not shots:
        return None
    return cols, rows, shots


def common(a, b):
    """The words both takes have, in order - a longest common subsequence.

    Not a set intersection: order is half of what the comparison is checking,
    and a story that prints the same words in a different arrangement has not
    printed the same screen.
    """
    if a == b:
        return list(a)
    n, m = len(a), len(b)
    table = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        row, nxt = table[i], table[i + 1]
        for j in range(m - 1, -1, -1):
            row[j] = nxt[j + 1] + 1 if a[i] == b[j] else max(nxt[j], row[j + 1])
    out, i, j = [], 0, 0
    while i < n and j < m:
        if a[i] == b[j]:
            out.append(a[i])
            i, j = i + 1, j + 1
        elif table[i + 1][j] >= table[i][j + 1]:
            i += 1
        else:
            j += 1
    return out


def words(lines):
    """A screen reduced to the words on it, in reading order.

    Punctuation goes, because the two interpreters disagree about it in ways
    that are not defects - a box border, an ellipsis, a wrap split - and case
    goes with it. What is left is what the reader can read.

    The reference's own COMMENTARY goes too, for the reason tools/zharness.py
    drops dfrotz's: "Warning: get_child called with object 0" is frotz talking
    about Balances, not Balances talking to the player, and os8088 answers the
    same 0 without editorialising (SPEC.md 61.11 - the object layer has no
    error channel). It is a PARAGRAPH and not a line, because on a screen it
    wraps like anything else, so it is taken to the blank line that ends it.
    A halt is never commentary and is never dropped here or there.
    """
    text = "\n".join(lines)
    text = re.sub(r"^[ \t]*Warning:.*?(?:\n[ \t]*\n|\Z)", "", text,
                  flags=re.M | re.S)
    # THE HYPHEN IS A SEPARATOR HERE, and it has to be: the two interpreters
    # break a long hyphenated token differently at the margin. Lost Pig's
    # banner carries "ZCODE-1-070917-994E", which the reference splits after
    # "ZCODE-1-" and this one moves down whole - so as one word they never
    # match, and as four they always do. Both wraps are legal and neither is
    # about anything the reader would call a difference.
    return re.sub(r"[^A-Za-z0-9']+", " ", text).lower().split()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("story", nargs="?")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("-o", "--out", default=SCREENDIR)
    ap.add_argument("--cols", type=int, default=REF_COLS)
    ap.add_argument("--rows", type=int, default=REF_ROWS)
    args = ap.parse_args()

    try:
        import pyte                             # noqa: F401
    except ImportError:
        raise SystemExit("zref: needs pyte - `pip3 install pyte`. The GATE "
                         "does not; only regenerating the goldens does.")
    import shutil
    if not shutil.which("frotz"):
        raise SystemExit("zref: needs the curses frotz - `brew install frotz`")

    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import zharness

    if args.all:
        targets = zharness.all_stories()
    else:
        targets = [zharness.resolve(args.story or "ADVENT.Z3")]

    os.makedirs(args.out, exist_ok=True)
    for path in targets:
        stem = os.path.splitext(os.path.basename(path))[0]
        print(f"zref: {os.path.basename(path)}", flush=True)
        shots = [capture(path, [], args.cols, args.rows)[0] for _ in range(2)]
        out = os.path.join(args.out, stem.lower() + ".txt")
        write_golden(out, os.path.basename(path), args.cols, args.rows, shots)
        stable = common(words(shots[0]), words(shots[1]))
        note = "" if len(stable) == len(words(shots[0])) else \
            f" ({len(words(shots[0])) - len(stable)} of them randomised)"
        print(f"zref:   {len(stable)} stable words{note} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
