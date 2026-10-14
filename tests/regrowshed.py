#!/usr/bin/env python3
"""A GROW is not refused over a cache, and "Too big" is not said about memory.

    make small && python3 tests/regrowshed.py [machine]

SPEC.md 50.6.2.1 and 27.6.1, on the 128KB floor machine, through the one path
that put both defects on the glass at once: **Note Pad opening the manual.**

The field report was *"kern_small says Too big opening README.TXT"*, and the
arithmetic said it should work - Note Pad's small region is 13,475 bytes, the
manual is 14,722, and the machine has 52.5KB of heap. Two things were wrong
and each hid the other:

  `mem_regrow` had no SHED.  `mem_claim` has had shed-and-retry since
                             SPEC.md 50.6.2 and this routine never did, so a
                             grow was refused over three purgeable caches
                             holding 31,744 bytes - memory the kernel keeps
                             on the explicit understanding that it can give
                             it away.
  `np_load` said the wrong    The claim stayed at NP_KB0, the read compared
  thing about it.             14,722 against 1,024, and the file API answered
                              the only thing it can: FERR_BIG. So a MEMORY
                              refusal reached the user as a sentence about
                              the FILE, and the field went looking for a size
                              limit that was not the cause.

Five verdicts, and the fourth is the positive control WRITING-TESTS 1 asks
for - a Note Pad that never said "Too large" at all would pass the other four:

  loaded    README.TXT opens with np_len at the CRLF file FOLDED, and the
            claim where np_fitclaim leaves it: the note plus NP_GROWKB,
            DERIVED from Note Pad's own equs (FIT_KB below)
  shed      ...AND THE SHED IS WHAT PAID FOR IT, which is the leg's whole
            subject and was not asserted for two cycles. It is measured as
            the CACHES FALLING across the load (19,456 -> 11,264 here) and
            not from the free run, because the free run is read before File >
            Open and the dialog is FDLG.DRV on this kernel: mod_need claims
            its image out of that very run and holds it for the whole of
            np_load. The row used to print a NOTE saying "a 16KB run was
            already free, so this leg did NOT exercise the shed" and carry
            on, and that note was WRONG - with mem_shed_one taken out of
            mem_regrow.shed the load fails against a measured 18,944-byte run
  intact    ...and a REFUSED load leaves the note alone (SPEC.md 27.6), which
            is checked on the way through the next leg rather than costing
            one of its own
  toobig    a file genuinely too big for this application must still be
            refused as one - off the size probe, with nothing claimed. The
            subject is TOOBIG.TXT, a 24KB plain-text FIXTURE this row adds to
            its own copy of the boot disk, and its size is checked as the
            VOLUME'S DIRECTORY reports it, because that is what the probe
            reads. It used to be APPS/PAINT.O88 guarded on `image_unwrap`'s
            UNPACKED size, and both halves were wrong: a PKGZ package is not a
            'CZ' file, so the transparent read does not expand it and the
            probe sees the FILE (SPEC.md 20.13.5) - and Paint's APP_SMALL size
            pass took that file from 16,784 bytes to 15,143, under the
            16,384-byte ceiling, so Note Pad simply LOADED it. A subject that
            is another package's size goes vacuous the next time that package
            is made smaller; a fixture nothing else owns does not
  nomem     ...and a SECOND Note Pad cannot fund a second 16KB document on a
            128KB machine, so its load is refused for real - and the toast
            must be "No memory" and never "Too large" (FERR_BIG in the
            kernel's own words since SPEC.md 18.4.11; Note Pad's own table
            said "Too big", which is what the field report quotes)

The toast is read out of `toast_buf` and not off the glass: it expires on a
tick count (SPEC.md 59), so a settle long enough to be sure a load finished
is long enough to lose it.

BOTH NOTE PADS ARE OPENED BEFORE ANYTHING IS LOADED, and that is what makes
the first two legs and the last one about the mechanism rather than about
this kernel's free-heap figure. A Note Pad INSTANCE is 14,336 bytes of
region, the manual's claim is 16,384, FDLG.DRV takes 4KB for as long as the
dialog is up (SPEC.md 38.0), and the 128KB machine's heap is 60.5KB: one
instance and the dialog leave a 22KB run, so a lone Note Pad's grow FITS and
sheds nothing - the row read `caches 24576 -> 24576` that way, its nomem leg
funded the second document and printed a NOTE, and both legs had lost their
subject the cycle kern_small's heap grew past the 53,760 bytes they were
staged on. With the second instance standing the largest run is ~1KB, so the
first grow can be funded only by a shed, and once it has been the second
instance's grow cannot be funded at all - two regions, one 16KB document and
the dialog are 49KB of 60.5 before the 16KB the second read wants. The old
sequence emptied the first note with File > New to make room to LAUNCH the
second; standing both up first needs no such step.

**How to make it go red** (WRITING-TESTS 1, both re-run at the re-derivation):
take the `call mem_shed_one` out of `mem_regrow.shed` and `loaded`, `shed`,
`toobig` and `intact` all fail - np_len 0 and the caches untouched at
19,456 - while `nomem` still passes, which is why `shed` is a leg of its own.
Take np_load's `jae .say` out and `nomem` alone fails reading "Too large".
"""
import atexit
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
# kern_small, and set BEFORE the imports: os88sym checks the map against the
# binary the moment it is asked.
os.environ.setdefault("OS88_DEFINES", "KERN_SMALL")
import os88build                                            # noqa: E402
os88build.use_build("build/smallk")
import os88marty as M                                       # noqa: E402
import os88geom                                             # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402
import os88flush                                             # noqa: E402

ARM = ("KERN_SMALL",)
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_128k"

# THE MANUAL'S FOLDED LENGTH, derived and not carried: the file ships with
# CRLF and np_load folds each pair to one 13, so what the note holds is the
# repository copy's own byte count. Deriving it means an edit to readme.txt
# never makes this row red.
README = os.path.join(os.path.dirname(__file__), "..", "readme.txt")
WANT_LEN = len(open(README, "rb").read().replace(b"\r\n", b"\n"))

# WHERE THE CLAIM ENDS A LOAD, derived for the same reason. np_load sizes the
# claim off the size PROBE (SPEC.md 27.6) and np_fitclaim leaves the note's
# fit on BOTH its paths, a refused load claiming nothing - so after any load
# the claim is the NOTE's whole kilobytes plus NP_GROWKB, clamped to
# NP_KB0..NP_MAXKB (np_resize). This row asserted 16 here, which was that
# arithmetic for a 14,427-byte manual: the README rework made it 12,904, the
# fit became 14, and both legs reading the claim went red while Note Pad did
# exactly what it says. The constants are READ out of the package's source,
# so a retuned Note Pad moves this with it.
_NP = open(os.path.join(os.path.dirname(__file__), "..", "apps", "notepad",
                        "notepad.asm")).read()


def _np_equ(name):
    import re
    hit = re.search(r"^%s\s+equ\s+(\d+)" % name, _NP, re.M)
    if not hit:
        sys.exit("regrowshed: no `%s equ` in apps/notepad/notepad.asm - the "
                 "claim this row expects cannot be derived" % name)
    return int(hit.group(1))


NP_KB0, NP_GROWKB, NP_MAXKB = (_np_equ(n) for n in
                               ("NP_KB0", "NP_GROWKB", "NP_MAXKB"))
FIT_KB = min(NP_MAXKB, max(NP_KB0, (WANT_LEN + 1023) // 1024 + NP_GROWKB))

fails = []


def check(name, cond, note=""):
    print("  [%s] %-8s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


# A PRIVATE TREE (WRITING-TESTS 5.2): `make small` writes build/*.drv beside
# build/smallk/, so out of tree is what lets this row run beside anything
# else. `small` is the only target it needs - the small SYSTEM disk carries
# README.TXT in its root AND the APP_SMALL Note Pad in APPS/ (§24.3), so
# there is no B: floppy in this scenario at all.
_T = os88build.tree(targets=("small",))

# ...AND BOTH READERS FOLLOW IT, which takes `apply()` AND an override.
#
# `apply()` is what makes `os88build.at` resolve `build/...` into this tree -
# and dispapps.sym needs that, because its staleness guard compares the
# APP_SMALL package against `build/smallapp/notepad.o88`. Without it the
# offsets come from the shared directory while the GUEST runs the tree's copy,
# which agree only while `build/` happens to be current: the guard then fails
# saying "build/ is BEHIND THE TREE" about a tree that is fine.
#
# But `apply()` alone points the KERNEL reader wrong, which is
# tests/small128.py's note: `defines_for` asks how the tree's DEFAULT kernel
# target is compiled and answers KERN_BIG even for a tree built for `small`.
# So name the tree's own smallk/ afterwards and put KERN_SMALL back.
_T.apply()
os.environ["OS88_BUILD"] = os.path.join(_T.dir, "smallk")
os.environ["OS88_DEFINES"] = "KERN_SMALL"
os88sym.default_defines(*ARM)

# THE SMALL BUILD'S OWN LAYOUT (dispapps.sym's note): the disk carries the
# APP_SMALL Note Pad, whose np_* offsets are not the shipped build's.
NP = dict((k, dispapps.sym("notepad", k, small=True))
          for k in ("np_len", "np_cap", "np_capkb", "np_dseg"))

# THE TOOBIG LEG'S SUBJECT, on a scratch copy of the boot disk (see the
# docstring's `toobig`). Plain text and not 'CZ', so the size on disk IS the
# size the probe answers - and 24KB, half as much again as NP_MAXKB, so no
# change to Note Pad's ceiling short of a redesign makes it fit.
BIG_NAME = "TOOBIG.TXT"
BIG_BYTES = 24 * 1024
NP_CEIL = NP_MAXKB * 1024
_BOOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build",
                     "regrowshed-%d.img" % os.getpid())
_FIX = _BOOT[:-4] + ".txt"
atexit.register(lambda: [os.unlink(p) for p in (_BOOT, _FIX)
                         if os.path.exists(p)])
shutil.copyfile(_T.img("small360.img"), _BOOT)
_LINE = b"regrowshed: a file Note Pad must refuse as TOO BIG, not load.\r\n"
with open(_FIX, "wb") as f:
    f.write((_LINE * (BIG_BYTES // len(_LINE) + 1))[:BIG_BYTES])
subprocess.check_call([sys.executable, os.path.join(os.path.dirname(
    os.path.abspath(__file__)), "..", "tools", "os88fat.py"), "add", _BOOT,
    _FIX, BIG_NAME], stdout=subprocess.DEVNULL)
_ON_DISK = [e.size for e in
            os88flush.Volume(open(_BOOT, "rb").read()).find(BIG_NAME)]
if len(_ON_DISK) != 1 or _ON_DISK[0] <= NP_CEIL:
    sys.exit("regrowshed: %s is %r bytes in the boot disk's directory, which "
             "is not one file over Note Pad's %d-byte ceiling - the toobig leg "
             "has lost its subject (WRITING-TESTS 1)"
             % (BIG_NAME, _ON_DISK, NP_CEIL))

with os88ui.boot(_BOOT, machine=MACHINE, limit=180) as ui:
    m = ui.m
    S = lambda n: m.sym(n, ARM)                             # noqa: E731
    eq = os88sym.equates(ARM)
    ram = u16(m.read(0x413, 2)) * 1024
    HEAP, top = eq["HEAP_SEG"], u16(m.read(S("mem_top"), 2))
    print("== %s : %d bytes RAM, heap %.1f KB, the manual folds to %d =="
          % (MACHINE, ram, (top - HEAP) * 16 / 1024.0, WANT_LEN))

    def heap(label):
        """The map, and the largest FREE RUN - which is the quantity the whole
        row is about: `mem_regrow` wants one run the whole new size."""
        sz = os88geom.MC_SIZE
        tab = m.read(S("mem_tab"), eq["MEM_MAX"] * sz)
        rows = []
        for i in range(eq["MEM_MAX"]):
            r = tab[i * sz:(i + 1) * sz]
            seg = u16(r, os88geom.MC_SEG)
            if seg:
                rows.append((seg, u16(r, os88geom.MC_PARA),
                             u16(r, os88geom.MC_OWN)))
        rows.sort()
        cur, free, run = HEAP, 0, 0
        purge = 0
        for seg, para, own in rows:
            if seg > cur:
                free += (seg - cur) * 16
                run = max(run, (seg - cur) * 16)
            if eq["MEM_PG_MIN"] <= (own >> 8) <= eq["MEM_PG_MAX"]:
                purge += para * 16
            cur = max(cur, seg + para)
        if top > cur:
            free += (top - cur) * 16
            run = max(run, (top - cur) * 16)
        print("    %-26s free %6d  largest run %6d  in caches %6d"
              % (label, free, run, purge))
        heap.free = free
        return run, purge

    def npseg(win):
        rec = m.read(S("wm_wins") + win.i * os88geom.WIN_SIZE,
                     os88geom.WIN_SIZE)
        return u16(rec, os88geom.W_SEG)

    def npstate(win):
        p = npseg(win)
        return dict((k, u16(m.readseg(p, NP[k], 2)))
                    for k in ("np_len", "np_cap", "np_capkb", "np_dseg"))

    def open_file(win, path, tag):
        """File > Open in `win`, then walk to `path` and pick it.

        `path` is folder-relative to the VOLUME ROOT ("README.TXT",
        "APPS/PAINT.O88"), and the walk starts by going UP until there is no
        `..` row - which it has to, because SPEC.md 38.10 opens the chooser
        on MEDIA the first time and on whatever folder this instance last
        chose after that, so a row that assumed either would pick the wrong
        file on one of the two.

        KEYBOARD, not a click: in the chooser the arrows MOVE THE SELECTION
        and Enter acts on it (SPEC.md 38.4 - a folder dives, a file answers),
        so no listing geometry is needed and nothing can land on the wrong
        row. The rows are the chooser's OWN listing cache (`ui.listing` with
        the chooser named), which is what it paints and what FS_SEL indexes.
        """
        ui.raise_window(win)
        ui.menu_pick("File", "Open")
        try:
            ch = ui.chooser(limit=120)
        except os88ui.UIError as e:
            sys.exit("%s: File > Open put no chooser up - %s" % (tag, e))

        def rows():
            return [r[0] for r in ui.listing(ch)]

        def dive():
            """Enter on the selected FOLDER, waited for by the listing it
            replaces - a dive re-lists the chooser in place."""
            was = rows()
            m.key("Enter")
            M.until(m, lambda _m: rows() != was, "the chooser to re-list",
                    poll=0.05, guest=60.0)
            M.settle(m, limit=120)

        def pick(name, last):
            """Select `name` and press Enter - a dive for a folder, the
            answer for a file. Asserts the SELECTION before committing, so a
            miss is reported here rather than as the feature under test."""
            rs = rows()
            if name not in rs:
                sys.exit("%s: %s is not listed here - %r" % (tag, name, rs))
            ui.chooser_select(name, ch) # a click; the arrows scroll a
                                        # chooser (SPEC.md 38.4)
            if not last:
                dive()
                return
            m.key("Enter")
            ui.chooser_gone(limit=60)   # the answer is POSTED (SPEC.md 38.6)
            M.settle(m, limit=180)      # ...and np_load runs after the reap

        # The chooser's window is up before its LISTING is: the read behind
        # it sheds caches first on a full heap, which is the state the nomem
        # leg stages, so the rows are waited for and not read on arrival.
        try:
            M.until(m, lambda _m: rows(), "the chooser's listing",
                    poll=0.05, guest=60.0)
        except M.MartyError as e:
            sys.exit("%s: the chooser came up and never listed anything - %s"
                     % (tag, e))
        while rows()[:1] == [".."]:     # up to the volume root
            ui.chooser_select("..", ch)
            dive()
        parts = path.split("/")
        for k, part in enumerate(parts):
            pick(part, k == len(parts) - 1)
        return m.read(S("toast_buf"), 26).split(b"\0")[0].decode(
            "latin-1", "replace")

    # --- the caches have to EXIST, so open a Disk window before anything ----
    # A bare desktop holds no claims at all (tests/small128.py asserts it), so
    # a Note Pad launched from nowhere would find the heap empty and grow with
    # no shed to make. Navigating A: is what claims MEM_P_VIEW and the
    # directory read-ahead, and it is also how a person gets to Note Pad.
    first = ui.path("A:/APPS/NOTEPAD.O88")
    second = ui.path("A:/APPS/NOTEPAD.O88")     # see the docstring: BOTH, first
    if second.i == first.i:
        sys.exit("regrowshed: the second launch fronted the first Note Pad "
                 "instead of starting another - two regions is the staging")
    run, purge = heap("two Note Pads, caches warm")
    st = npstate(first)
    print("    claim starts at %d bytes (np_capkb=%d)"
          % (st["np_cap"], st["np_capkb"]))

    # 1. THE SHED. The claim needs 16,384 and the largest run is smaller
    #    than that with tens of KB sitting in caches - which is the whole
    #    defect, so the row says so rather than leaving it implied.
    print("  -- README.TXT, %d bytes into a %d-byte claim --"
          % (WANT_LEN, st["np_cap"]))
    t = open_file(first, "README.TXT", "loaded")
    st = npstate(first)
    check("loaded", st["np_len"] == WANT_LEN and st["np_capkb"] == FIT_KB,
          "(np_len=%d want %d, np_capkb=%d want %d, toast %r; the run was %d "
          "with %d in caches)"
          % (st["np_len"], WANT_LEN, st["np_capkb"], FIT_KB, t, run, purge))

    # ...AND THE SHED IS WHAT FUNDED IT, asserted rather than assumed. This
    # leg used to print a NOTE when the largest free run it had measured was
    # already 16KB, saying the shed had not been exercised and the row wanted
    # re-deriving - and the note was WRONG, which is worse than either thing
    # it could have been. It reads `run` from BEFORE File > Open, and on
    # kern_small the dialog is FDLG.DRV (SPEC.md 38.0): mod_need claims its
    # image out of that very run and holds it for the whole of np_load, so
    # what mem_regrow sees is always smaller - measured here at 19,456 bytes
    # in caches before the load and 11,264 after, which is MEM_P_ICO and the
    # window's raise cache both given away to fund one grow. So the run
    # measured above is an upper bound on what the grow faces and can never
    # say the shed did not happen; the caches falling is the fact that can.
    run2, purge2 = heap("...the manual loaded")
    check("shed", purge2 < purge,
          "(caches %d -> %d: the 16,384-byte claim was funded out of memory "
          "the kernel was holding on the understanding it could give it away, "
          "SPEC.md 50.6.2.1)" % (purge, purge2))

    # 2. THE POSITIVE CONTROL. There is no claim this application may grow to
    #    that holds TOOBIG.TXT, so FERR_BIG is the true answer - said as
    #    "Too large", the KERNEL's words (OSAPI_FERR_TEXT, SPEC.md 18.4.11),
    #    where Note Pad's own table said "Too big" - and since
    #    np_load sizes by the PROBE (SPEC.md 27.6) it is said off the file's
    #    own KB with NOTHING claimed, so np_capkb stays the manual's fit. It
    #    used to be opened to the ceiling first and read 16. Without this leg a
    #    Note Pad that had simply stopped saying "Too large" would pass every
    #    other one.
    #
    #    THE SIZE IS CHECKED BEFORE THE BOOT (above, `_ON_DISK`), off the
    #    boot disk's own directory entry, because this is the leg that goes
    #    vacuous the moment its subject fits - and it did, once, when its
    #    subject was a package somebody else's size pass shrank.
    t = open_file(first, BIG_NAME, "toobig")
    st = npstate(first)
    check("toobig", t == "Too large" and st["np_capkb"] == FIT_KB,
          "(toast %r, np_capkb=%d want %d, nothing claimed - %s is %d "
          "bytes on disk against a %d-byte ceiling)"
          % (t, st["np_capkb"], FIT_KB, BIG_NAME, _ON_DISK[0], NP_CEIL))
    # ...and SPEC.md 27.6's other promise, for free: a refused load leaves
    # the note exactly as it was.
    check("intact", st["np_len"] == WANT_LEN,
          "(np_len=%d after the refusal, want %d unchanged)"
          % (st["np_len"], WANT_LEN))

    # 3. A REAL refusal, and what it is allowed to say. Two Note Pads cannot
    #    fund two 16KB documents on a 128KB machine, so the second's grow is
    #    refused with every cache already shed - which is why both instances
    #    were opened before the first load (the docstring), and why there is
    #    nothing to stage here.
    run, purge = heap("before the second's load")
    # ...UNLESS the machine has grown past the staging. The arithmetic in the
    # docstring was struck against a 60.5KB heap and a File > Open that took
    # FDLG.DRV's 4KB for the length of np_load; kern_small since has 63.0KB
    # and a chooser that is a Disk window, and the second grow was simply
    # FUNDED out of 14,336 free and 4,096 shed. So the leg stands up what it
    # needs rather than hoping: while free + shed-able could still fund the
    # second document, the next SMALL package on the disk takes some of it.
    # Small, because the chooser File > Open puts up has to list a folder
    # out of what is left: a third Note Pad's 14KB region was tried and left
    # 3KB, and the chooser came up empty. And each launch's Disk window is
    # shut behind it, because kern_small has six window slots (SPEC.md
    # 11.102) and the chooser is a Disk window too; a listing holds only a
    # purgeable cache, so closing one moves no figure the loop reads. If the
    # list runs out the check below FAILS - it never passes vacuously.
    for filler in ("CALC", "PIANO", "FRACTAL"):
        if heap.free + purge < 16 * 1024:
            break
        print("    free + caches %d could fund a second 16KB document - "
              "%s takes some of it" % (heap.free + purge, filler))
        for w in [w for w in ui.windows() if w.title == "APPS"]:
            ui.close(w)
        ui.path("A:/APPS/%s.O88" % filler)
        for w in [w for w in ui.windows() if w.title == "APPS"]:
            ui.close(w)
        run, purge = heap("%s standing" % filler)
    t = open_file(second, "README.TXT", "nomem")
    st = npstate(second)
    if st["np_capkb"] >= 16:
        # A LEG THAT HAS LOST ITS SUBJECT FAILS (WRITING-TESTS 1), as `toobig`
        # does. This was a NOTE and a pass, and it stood green for the whole
        # time the refusal was not happening.
        check("nomem", False,
              "(the second document was FUNDED - toast %r, largest run %d "
              "before it - so no refusal happened for this leg to read; the "
              "staging in the docstring no longer fills this machine)"
              % (t, run))
    else:
        check("nomem", t == "No memory",
              "(toast %r with np_capkb=%d short of %d - a refused CLAIM must "
              "not be reported as a file that is too big, SPEC.md 27.6.1)"
              % (t, st["np_capkb"], 16))

print()
if fails:
    sys.exit("regrowshed: FAILED: %s" % ", ".join(fails))
print("regrowshed: a grow sheds, and the toast tells memory from size")
