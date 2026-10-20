#!/usr/bin/env python3
"""AN UNKNOWN EXTENSION IS LOOKED FOR, NOT REFUSED (SPEC.md 54.4.3).

    make && python3 tests/assocfind.py

The 720KB system disk with one extra file in its root, `HELLO.TEX`, and
apps720.img in B:. `.TEX` is TEXPAD's DECLARATION (SPEC.md 54.6): the kernel
has no default for it and the system disk's ASSOC.DAT does not carry it, so
the only thing on this machine that knows what opens it is B:'s own
ASSOC.DAT - and nothing has read that, because B: has never been opened.

A double-click on A:\\HELLO.TEX must open TeXPad. Before 54.4.3 an extension
the tables did not already hold went the package route and toasted `Load
failed` (LD_EBAD), on a machine whose other disk said exactly which program
it wanted.

The second leg is the search coming back EMPTY: `HELLO.ZZZ`, which nothing on
either disk declares, must ask the disks (the floppy controller moves) and
then give the old verdict - `Load failed`, no window - and leave the Disk
window it was opened from still working.

Broken on purpose - the kernel before 54.4.3 - it FAILS with that toast.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build                          # noqa: E402
from os88fat import Fat12                                     # noqa: E402

MACHINE = "os8088_5150_herc_720_gla"
# THE TITLE WITH THE DOCUMENT IN IT, and not just "TeXPad": the package's
# window comes up first and takes the document in a second phase (SPEC.md
# 54.10), retitling itself when it has. Waiting on the bare name let the
# second leg snapshot the window list between the two and then read the
# retitle as something HELLO.ZZZ had opened.
WANT = "TeXPad - HELLO.TEX"
DOC = b"\\title{Hello}\n\nFound by its extension.\n"


def main():
    os.chdir(ROOT)
    bad = []
    tag = os.getpid()
    img = os.path.join(ROOT, "build", "assocfind-%d.img" % tag)
    src = os.path.join(ROOT, "build", "assocfind-%d.tex" % tag)
    nobody = "HELLO.ZZZ"
    with open(os88build.at("build/os8088-720.img"), "rb") as f:
        data = f.read()
    with open(img, "wb") as f:
        f.write(data)
    with open(src, "wb") as f:
        f.write(DOC)
    try:
        fat = Fat12(img)
        fat.add(src, "HELLO.TEX")
        fat.add(src, nobody)
        fat.save()
        with os88ui.boot(img, apps=os88build.at("build/apps720.img"),
                         machine=MACHINE) as ui:
            ui.path("A:")
            ui.open("HELLO.TEX", expect=None)

            def done():
                txt, on = ui.toast()
                return WANT in ui.titles() or \
                    (on and txt)
            try:
                os88marty.until(ui.m, lambda mm: done(), "the open",
                                poll=0.3, limit=300.0, guest=120.0)
            except os88marty.MartyError as e:
                bad.append("neither a window nor a toast: %s" % e)
            txt, on = ui.toast()
            print("   windows %s; toast %r (%s)"
                  % (ui.titles(), txt, "up" if on else "down"))
            if WANT not in ui.titles():
                bad.append("A:\\HELLO.TEX did not open in TeXPad: the toast "
                           "says %r" % txt)

            # --- the search finds nothing ---------------------------------
            wins = sorted(ui.titles())
            reads = ui.m.disk().get("reads")
            ui.open(nobody, expect=None)     # a REFUSAL, asserted below by
            try:                             # its toast rather than by a
                ui.wait_toast(says="Load failed")   # window never arriving
            except os88ui.UIError as e:
                bad.append("%s: no 'Load failed' toast: %s" % (nobody, e))
            now = ui.m.disk().get("reads")
            print("   %s: toast %r, %d reads, windows %s"
                  % (nobody, ui.toast()[0], now - reads, ui.titles()))
            if sorted(ui.titles()) != wins:
                bad.append("%s opened something: %s" % (nobody, ui.titles()))
            if now == reads:
                bad.append("%s was refused without asking a disk - the "
                           "search did not run" % nobody)
            ui.open("MEDIA")                 # ...and the Disk window it was
                                             # asked from still navigates
    finally:
        for p in (img, src):
            if os.path.exists(p):
                os.remove(p)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
