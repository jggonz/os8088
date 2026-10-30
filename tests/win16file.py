#!/usr/bin/env python3
"""win16file - files, through WIN16's DOS (SPEC.md 124.21,
docs/plans/WIN16-ANY-PLAN.md S16).

    python3 tests/win16file.py              the QEMU leg (row win16file)
    python3 tests/win16file.py --keep       leave QEMU and its disks for a look

FILES.EXE (tests/win16/files.c, `make win16fixtures`) reaches files through
both doors a Windows program has: KERNEL's _lopen family, OpenFile and
GetTempFileName, and the C runtime's own int 21h - mkdir, chdir, getcwd,
rename, remove, _dos_findfirst and stdio. Every one of them is answered by
DOS.O88's core, which WIN16 hosts (124.21.1), so a check that fails is named
in its MessageBox and the box must say none did.

Then it writes W16FILE.TXT - 250 numbered lines of 40 bytes, more than one
8 KB window of the DOS - and reads it back. The row reads that file off the
floppy image on the host after the program has ended and checks every byte:
the DOS's window flushed, the FAT and the directory entry written, which is
what no message box can say. The fixture not built is a SKIP.

THE NOTEPAD LEG (SPEC.md 124.22.5) runs when build/win16/NOTEPAD.EXE is
there - Microsoft's, from `make win16disk WIN3X=<dir>`, never in this tree -
and is skipped with a note when it is not. Notepad 3.0 is started, opens
NP.TXT (80 numbered lines, two screens and more) through its own File Open,
and the leg scrolls it by the main window's bar and by Page Down - Notepad's
EDIT has no bars of its own, its window has them and asks EM_GETTHUMB, so
both the EDIT's first line and the window's thumb are checked - then
Ctrl+End, types a line, saves with File Save, and reads NP.TXT off the image
on the host: the original bytes and the line after them, CR LF and all.
"""
import argparse
import os
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
from win16mines import QDrive, b                              # noqa: E402
import win16fpu as F                                          # noqa: E402

NAME = "FILES.EXE"
NOTEPAD = "NOTEPAD.EXE"
TYPED = "WIN16 was here"

# WIN16's window record (apps/win16/w16user.inc, w16dlg.inc, w16edml.inc)
W16W_SIZE = 102
W16W_FLAGS, W16W_KIND, W16W_SBV = 0, 68, 94
W16WF_USED = 1
W16Q_EDIT, W16Q_DIALOG = 2, 5
EC_CARET, ME_TOP, ME_LEN, ME_LINES = 70, 74, 84, 86


def lines():
    """files.c's line(): exactly 40 bytes each, CR LF included"""
    return b"".join(("W16FILE line %03d of 250, %04X ........\r\n"
                     % (i, (i * 97) & 0xFFFF)).encode() for i in range(1, 251))


def nptext():
    """NP.TXT: 80 lines, tabs in them, one in ten past the window's width"""
    out = []
    for i in range(1, 81):
        t = "Line %02d\tof the Notepad file, %s" % (i, "word " * (i % 7))
        if i % 10 == 0:
            t += " and a long tail" * 8
        out.append(t)
    return ("\r\n".join(out) + "\r\n").encode()


def notepad_leg(d, ui, S, dw, bad):
    """SPEC.md 124.22.5: open, scroll by bar and key, type, save"""
    import xmcheck
    from win16fpu import Prog
    p = Prog(d, ui, xmcheck.sym, S, NOTEPAD)
    p.launch(dw)

    def recs():
        raw = d.read((p.seg << 4) + S["w16_wnd"], W16W_SIZE * 48)
        return [raw[i * W16W_SIZE:(i + 1) * W16W_SIZE] for i in range(48)]

    def w(r, off):
        return struct.unpack_from("<H", r, off)[0]

    def edit():
        for r in recs():
            if r[W16W_FLAGS] & W16WF_USED and r[W16W_KIND] == W16Q_EDIT \
                    and w(r, 8) & 4:                    # ES_MULTILINE
                return r
        return None

    def focus():
        return p.pw("w16_focus")

    def dialog():
        return any(r[W16W_FLAGS] & W16WF_USED and r[W16W_KIND] == W16Q_DIALOG
                   for r in recs())

    def keys(*ks):
        for k in ks:
            d.hmp("sendkey %s" % k)
            d.idle(0.3)

    def typed(text):
        keys(*[{" ": "spc", ".": "dot"}.get(c, ("shift-" + c.lower())
               if c.isupper() else c) for c in text])

    d.wait(lambda: edit() is not None and focus() != 0, 120,
           "Notepad's EDIT, with the focus")
    d.idle(3.0)
    keys("alt-f")
    keys("o")
    d.wait(lambda: dialog() and focus() != 0, 60, "File Open, with the focus")
    d.idle(3.0)
    typed("np.txt")
    keys("ret")
    want = nptext()
    d.wait(lambda: not dialog() and w(edit(), ME_LEN) == len(want), 120,
           "NP.TXT in Notepad's EDIT")
    d.idle(4.0)
    e = edit()
    print("Notepad: NP.TXT open, %d bytes in %d lines" % (w(e, ME_LEN), w(e, ME_LINES)))
    if w(e, ME_LINES) != 81:
        bad.append("Notepad's EDIT counts %d lines, 81 wanted" % w(e, ME_LINES))

    # the main window's bar: its down arrow, above the corner box
    main = next(r for r in recs() if r[W16W_FLAGS] & W16WF_USED and r[W16W_KIND] == 0)
    p.w = next(x for x in ui.windows() if x.i == p.w.i)
    _, _, r_, b_ = p.w.content()
    top0, thumb0 = w(e, ME_TOP), w(main, W16W_SBV)
    d.click(r_ - 8, b_ - 17 - 9)
    d.idle(3.0)
    e = edit()
    main = next(r for r in recs() if r[W16W_FLAGS] & W16WF_USED and r[W16W_KIND] == 0)
    print("   the bar's down arrow: first line %d -> %d, thumb %d -> %d"
          % (top0, w(e, ME_TOP), thumb0, w(main, W16W_SBV)))
    if w(e, ME_TOP) <= top0 or w(main, W16W_SBV) <= thumb0:
        bad.append("Notepad: the bar's down arrow did not scroll (first line "
                   "%d -> %d, thumb %d -> %d)" % (top0, w(e, ME_TOP), thumb0,
                                                  w(main, W16W_SBV)))
    top1 = w(e, ME_TOP)
    keys("pgdn")
    d.idle(3.0)
    e = edit()
    print("   Page Down: first line %d -> %d" % (top1, w(e, ME_TOP)))
    if w(e, ME_TOP) <= top1 + 5:
        bad.append("Notepad: Page Down moved the first line %d -> %d"
                   % (top1, w(e, ME_TOP)))

    d.hmp("sendkey ctrl-end 300")
    d.idle(3.0)
    if w(edit(), EC_CARET) != len(want):
        bad.append("Notepad: Ctrl+End left the caret at %d, not %d"
                   % (w(edit(), EC_CARET), len(want)))
    typed(TYPED)
    keys("ret")
    d.idle(3.0)
    keys("alt-f")
    keys("s")
    d.wait(lambda: w(edit(), ME_LEN) == len(want) + len(TYPED) + 2 and
           not dialog(), 60, "Notepad's save")
    d.idle(6.0)
    return want + TYPED.encode() + b"\r\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16file: SKIP - FILES.EXE is not built (make win16fixtures)")
        return 77
    import os88ui
    import xmcheck
    from os88fat import Fat12
    bad = []
    work = tempfile.mkdtemp(prefix="w16f-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        np = os.path.exists(b(os.path.join("win16", NOTEPAD)))
        extra = []
        if np:
            extra = [os.path.join(work, "NP.TXT")]
            with open(extra[0], "wb") as f:
                f.write(nptext())
        F.bdisk(bimg, 1440, [NAME] + ([NOTEPAD] if np else []), extra)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: "files: 0 failed"})
        npwant = None
        if np:
            npwant = notepad_leg(d, ui, S, dw, bad)
        else:
            print("win16file: the Notepad leg is skipped - NOTEPAD.EXE is "
                  "not built (make win16disk WIN3X=<dir>)")
        d.hmp("stop")                   # the image, as the guest left it
        fat = Fat12(d.p.b)
        names = [Fat12.pretty(e[:11]) for _, _, e in fat.entries()]
        try:
            got = fat.read("W16FILE.TXT")
        except Exception as e:          # noqa: BLE001 - absent is the finding
            got = None
            bad.append("W16FILE.TXT is not on B: (%s)" % e)
        if got is not None:
            want = lines()
            if got != want:
                at = next((i for i in range(min(len(got), len(want)))
                           if got[i] != want[i]), min(len(got), len(want)))
                bad.append("W16FILE.TXT is %d bytes, %d wanted, first "
                           "difference at %d" % (len(got), len(want), at))
            else:
                print("B:\\W16FILE.TXT: %d bytes, every one as written" % len(got))
        if "W16DIR" in names:
            bad.append("W16DIR is still on B: - rmdir did not reach the disk")
        if npwant is not None:
            try:
                got = fat.read("NP.TXT")
            except Exception as e:      # noqa: BLE001 - absent is the finding
                got = None
                bad.append("NP.TXT is not on B: (%s)" % e)
            if got is not None and got != npwant:
                bad.append("NP.TXT as Notepad saved it is %d bytes, %d wanted; "
                           "it ends %r" % (len(got), len(npwant), got[-40:]))
            elif got is not None:
                print("B:\\NP.TXT: %d bytes, the file and the typed line" % len(got))
        if not a.keep:
            d.p.close()
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16file: both doors to the DOS, and 10,000 bytes on the disk")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
