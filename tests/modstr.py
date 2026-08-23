#!/usr/bin/env python3
"""Do the on-demand modules' own strings letter correctly? (SPEC.md 2.8.6)

    python3 tests/modstr.py

`CLONE.DRV` and `FORMAT.DRV` carry their prompts, their key lines and their
verdicts in the IMAGE and read them through CS. The failure that change can
produce is silent and specific: a string read through `DS` instead lands at
that offset in `KERNEL_SEG`, which is kernel code - so the line letters
plausible-looking rubbish rather than faulting, and a screenshot of it is not
obviously wrong at a glance.

So this asserts the BYTES. Two per image, chosen because both are readable
from the host without rendering anything:

  * the **key line**, out of `fm_hdrbuf` after a paint - the status line is
    the last thing a Disk window's painter stages there;
  * the **verdict**, out of `toast_buf` - which is also the test of §2.8.6's
    one trick, composing a message while the image is still loaded so that it
    outlives the module without the module's bytes outliving it.

It is deliberately NOT a screenshot: a golden image fails on every legitimate
pixel change, and what is being defended here is the text.
"""
import os
import re
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
sys.path.insert(0, "tests/unit")
import os88marty as M                                     # noqa: E402
from os88mouse import Mouse                               # noqa: E402
import dispcp                                             # noqa: E402
from harness import check, done                           # noqa: E402

MACHINE = "os8088_5150_cga_gla"
FILE_X, BAR_Y = 120, 8
ITEM0_Y, ITEM_H = 28, 16


def equ(path, name):
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, open(path).read(), re.M)
    if not m:
        sys.exit("%s: no `%s equ`" % (path, name))
    return int(m.group(1))


FM_IFMT = equ("kernel/files.inc", "FM_IFMT")
FM_ICLONE = equ("kernel/files.inc", "FM_ICLONE")


def text(m, addr, n=40):
    """The NUL string at `addr`, trailing spaces off.

    `fm_stat_line` pads its line to the window's width, because the padding IS
    the erase (SPEC.md 22.13.1) - so what the composer staged and what sits in
    the buffer differ by however much white space the window happened to have.
    """
    return m.read(addr, n).split(b"\x00")[0].decode("ascii", "replace").rstrip()


def watch_toast(m, addr, limit=8.0, unless=None):
    """The first toast to appear that is not `unless`, or ''.

    Polled rather than slept for: the emulator runs the guest at over four
    times real time, so a message with a fixed tick life can be up and gone
    inside one `sleep`. `unless` is the message ALREADY in the buffer - the
    staging area is not cleared when a toast expires, so without it the
    previous verdict answers instantly for the next one.
    """
    t0 = time.time()
    while time.time() - t0 < limit:
        t = text(m, addr, 24)
        if t and t != unless:
            return t
        time.sleep(0.03)
    return ""


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    hdr, toast = m.sym("fm_hdrbuf"), m.sym("toast_buf")
    print("== %s : a module's own strings (SPEC.md 2.8.6) ==" % MACHINE)

    # --- CLONE.DRV, on A: ----------------------------------------------------
    dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="A")
    mo.menu(FILE_X, BAR_Y, FILE_X + 30, ITEM0_Y + FM_ICLONE * ITEM_H)
    M.settle(m)
    got = text(m, hdr)
    check(got == "Spc=drive  Enter=yes  Esc=no",
          "CLONE.DRV letters its own key line",
          "the string is in the image and read through CS (SPEC.md 2.8.6); a "
          "DS read lands at that offset in KERNEL_SEG, which is kernel code, "
          "and letters rubbish that does not fault",
          got=got, want="Spc=drive  Enter=yes  Esc=no")

    m.key("Enter")                          # A: -> B:, the whole cross-drive
    said = watch_toast(m, toast, limit=90)  # clone, then the verdict
    check(said == "Cloned A: to B:", "...and its verdict, composed pre-drop",
          "SPEC.md 2.8.6: the image stages this into fm_hdrbuf while it is "
          "still loaded and toast_show copies it, so the message outlives the "
          "module. An empty string here means the compose happened after "
          "fm_edit_end",
          got=said, want="Cloned A: to B:")
    M.settle(m, limit=180)

    # --- FORMAT.DRV, on B: (which the clone above has just overwritten) ------
    dispcp.open_drive(m, mo, lambda n: m.sym(n), M.settle, letter="B")
    mo.menu(FILE_X, BAR_Y, FILE_X + 30, ITEM0_Y + FM_IFMT * ITEM_H)
    M.settle(m)
    got = text(m, hdr)
    check(got == "Enter=yes  Esc=no", "FORMAT.DRV letters its own key line",
          "'Spc=size' is named only on an external drive (SPEC.md 18.96.2), "
          "and B: is not one - so the SHORT line is the right answer here and "
          "the predicate that picks it is in the image too",
          got=got, want="Enter=yes  Esc=no")

    m.key("Enter")
    said = watch_toast(m, toast, limit=60, unless="Cloned A: to B:")
    check(said == "Formatted B:", "...and its verdict",
          "fm_edit_commit composes this before its own fm_edit_end, which is "
          "what drops the image (SPEC.md 2.8.6)",
          got=said, want="Formatted B:")
    M.settle(m, limit=120)

done("modstr")
