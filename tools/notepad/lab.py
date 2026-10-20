#!/usr/bin/env python3
# =============================================================================
# lab.py - Note Pad on the bench: price a keystroke, and say where it went
#
# The instrument docs/plans/completed/NOTEPAD-NOTES.md 5.1 was found with, and the one its
# remaining open reports should be picked up with. It drives a headless
# MartyPC through tools/os88marty.py, so every figure is a GUEST CYCLE COUNT
# on a cycle-accurate 4.77MHz 8088 and nothing here reads a wall clock.
#
# It adds NO code to the guest. tests/npbench.inc is the other instrument and
# they answer different questions: npbench times a routine from INSIDE, in
# guest ticks, and runs on the field machine; this times a real keystroke from
# OUTSIDE and can say which branch it took, which no in-guest timer can.
#
#   make npbench                                  # the disk this drives
#   python3 tools/notepad/lab.py boot             # cold boot, open README.TXT
#   python3 tools/notepad/lab.py verify           # guest memory == the build?
#   python3 tools/notepad/lab.py state
#   python3 tools/notepad/lab.py press ArrowDown 20
#   python3 tools/notepad/lab.py trace ArrowDown 3
#
# `press` is the summary - one line a keystroke, with the redraw branch it
# took. `trace` is the breakdown, and the one to reach for when `press` says
# a number you cannot place.
#
# THE LISTING MUST MATCH THE DISK. Labels come from a NASM listing, so after
# every rebuild:
#
#   nasm -f bin -w+error -DNPBENCH -I apps/ -I tests/ \
#        -l build/npbench.lst -o /tmp/x.bin apps/notepad/notepad.asm
#   cmp /tmp/x.bin build/npbench.bin
#
# ...and then `lab.py verify`, which checks the guest against the same binary.
# Skipping that is docs/plans/completed/NOTEPAD-NOTES.md 6.3, and it has been skipped twice.
# =============================================================================
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import drive                                            # noqa: E402
import trace as tr                                      # noqa: E402
from pkg import Lab, Labels, ms                         # noqa: E402
from state import State                                 # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BIN = os.path.join(ROOT, "build", "npbench.bin")
LST = os.path.join(ROOT, "build", "npbench.lst")

# README.TXT on the system disk, after Note Pad's CR/LF fold: 15,889 on disk,
# 461 newlines. The guard value for State.check - pass --len for another note.
README_LEN = 15428

# The redraw tails, in the order the report should prefer to name them.
PATHS = ["np_redraw.fullpaint", "np_redraw.scrolled", "np_redraw.scrolled0",
         "np_redraw.band"]

PRESS_LABELS = ["np_vmove", "np_move", "np_measure", "np_redraw",
                "np_redraw.normal", "np_redraw.haveit", "np_redraw.done",
                "np_redraw.out"] + PATHS

TRACE_LABELS = PRESS_LABELS + ["np_seedck", "np_seedrow", "np_seedtail",
                               "np_scrollto", "np_scrollpaint", "np_walk",
                               # the edit path, for a typing question
                               "np_ins", "np_room", "np_urec_ins", "np_hmark",
                               "np_delspan", "np_reconcile", "np_brkdraw",
                               # the row index (SPEC.md 27.13)
                               "np_xseed", "np_xseedi", "np_xnote", "np_xhalve"]


# SPEC.md 20.2's dispatcher: `call bp` at PKG_DISP=12, `retf` at 14. EVERY
# callback the kernel makes returns through that one instruction, so it is the
# terminator for "the package has finished" - which no label inside the module
# can be, because W_PAINT and W_ONCLICK end in different places and a raise
# may call back zero times. Synthesised into the label map because it is a
# property of the format rather than of this build.
PKG_RETF = 14


def labels_for(args):
    L = Labels(args.lst)
    L.addr["pkg_retf"] = PKG_RETF
    return L


def attach(args, want_len=None):
    lab = Lab(args.addr)
    lab.find_pkg(name=args.pkg.encode())
    st = State(lab, args.bin)
    if want_len is not None:
        st.check(want_len)
    return lab, st


def cmd_boot(args):
    # THE ADDRESS COMES FROM start(), not from args: `--addr` defaulted before
    # this boot existed, and the port is the one the OS handed the emulator a
    # moment ago. Every LATER command picks it up from drive.ADDRFILE.
    args.addr = drive.start(image=args.image, machine=args.machine)
    print("bench on %s" % args.addr)
    lab = Lab(args.addr)
    drive.open_readme(lab.m)
    out = os.path.join(ROOT, "build", "notepad-lab-opened.png")
    print("opened; %s" % (out if drive.shot(out) else "(no screenshot)"))
    lab.find_pkg(name=args.pkg.encode())
    n, diff = lab.verify(args.bin)
    print("package at 0x%04x, %d bytes, %d differ from %s"
          % (lab.seg, n, diff, os.path.relpath(args.bin, ROOT)))


def cmd_verify(args):
    lab = Lab(args.addr)
    lab.find_pkg(name=args.pkg.encode())
    n, diff = lab.verify(args.bin)
    print("package at 0x%04x, %d bytes, differing: %d" % (lab.seg, n, diff))
    print("VERDICT:", "guest IS this binary" if diff == 0
          else "MISMATCH - the guest is running something else")
    return 0 if diff == 0 else 1


def cmd_state(args):
    lab, st = attach(args)
    print("package at 0x%04x" % lab.seg)
    for k, v in st.dump().items():
        print("  %-12s %d" % (k, v))
    if args.rows:
        n = min(st.w("np_rowsn"), 60)
        print("  np_rows[0:%d] %s" % (n, st.arr("np_rows", n)))


def cmd_press(args):
    lab, st = attach(args, args.len)
    labels = labels_for(args)
    t = tr.Tracer(lab, labels, [n for n in PRESS_LABELS if n in labels])
    print("seg 0x%04x  len %d  vrows %d  rcols %d  drows %d"
          % (lab.seg, st.w("np_len"), st.w("np_vrows"), st.w("np_rcols"),
             st.w("np_drows")))
    print("%3s %5s %6s %-10s %9s %9s %9s  %s"
          % ("#", "top", "cur", "path", "total_ms", "pass1_ms", "draw_ms", "scr"))
    for i in range(args.n):
        top0, cur0 = st.w("np_top"), st.w("np_cur")
        t0, hits = t.press(args.key, settle=args.settle)
        p1 = tr.leg(hits, "np_redraw.normal", "np_redraw.haveit")
        dr = (tr.leg(hits, "np_redraw.band", "np_redraw.done")
              or tr.leg(hits, "np_redraw.scrolled", "np_redraw.done"))
        f = lambda c: ("%9.1f" % ms(c)) if c is not None else "        -"
        print("%3d %5d %6d %-10s %9.1f %s %s  %s"
              % (i, top0, cur0, tr.which(hits, PATHS).split(".")[-1],
                 tr.total_ms(t0, hits), f(p1), f(dr),
                 "YES" if st.w("np_top") != top0 else ""))


def cmd_click(args):
    """Price a click - a raise, a scroll-bar hit, a caret placement.

    docs/plans/completed/NOTEPAD-NOTES.md 5.3: bringing Note Pad to the front was reported as
    pausing about half a second, and SPEC.md 11.90 says an UNOBSCURED raise
    should cost a title bar and nothing else - so the first thing to establish
    is whether the window was covered at all.
    """
    lab, st = attach(args, args.len)
    labels = labels_for(args)
    names = [n for n in TRACE_LABELS + ["np_paint", "pkg_retf"] if n in labels]
    t = tr.Tracer(lab, labels, names)
    for i in range(args.n):
        t0, hits = t.click(args.x, args.y, settle=args.settle)
        tr.report(t0, hits, "click (%d,%d) #%d" % (args.x, args.y, i))
        print("    top=%d cur=%d" % (st.w("np_top"), st.w("np_cur")))


def cmd_trace(args):
    lab, st = attach(args, args.len)
    labels = labels_for(args)
    t = tr.Tracer(lab, labels, [n for n in TRACE_LABELS if n in labels])
    for i in range(args.n):
        before = st.dump()
        t0, hits = t.press(args.key, settle=args.settle)
        tr.report(t0, hits, "%s #%d  (top=%d cur=%d)"
                  % (args.key, i, before["np_top"], before["np_cur"]))
        print("    after: top=%d cur=%d" % (st.w("np_top"), st.w("np_cur")))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    # The bench's address, not a constant: `boot` starts a detached instance
    # on a port the OS picked and records it (drive.ADDRFILE), so every
    # command after it finds the machine without anybody typing a number.
    ap.add_argument("--addr", default=drive.addr())
    ap.add_argument("--bin", default=BIN)
    ap.add_argument("--lst", default=LST)
    ap.add_argument("--pkg", default="NOTEPAD")
    ap.add_argument("--len", type=int, default=README_LEN,
                    help="np_len to pin the offsets against (0 = no check)")
    ap.add_argument("--settle", type=int, default=60,
                    help="frames of guest time to let a press finish in")
    sub = ap.add_subparsers(dest="op", required=True)

    p = sub.add_parser("boot"); p.set_defaults(fn=cmd_boot)
    p.add_argument("--image", default="build/npbench360.img")
    # The period IBM ROM is not in this tree (it has never been licensed for
    # redistribution), so a container that has not been given one can only
    # boot the GLaBIOS machines - os8088_5150_cga_gla. The CPU is the same
    # cycle-accurate 8088 either way, which is what every figure here is in.
    p.add_argument("--machine", default="os8088_5150_cga")
    p = sub.add_parser("verify"); p.set_defaults(fn=cmd_verify)
    p = sub.add_parser("state"); p.set_defaults(fn=cmd_state)
    p.add_argument("--rows", action="store_true")
    p = sub.add_parser("press"); p.set_defaults(fn=cmd_press)
    p.add_argument("key"); p.add_argument("n", type=int, nargs="?", default=10)
    p = sub.add_parser("trace"); p.set_defaults(fn=cmd_trace)
    p.add_argument("key"); p.add_argument("n", type=int, nargs="?", default=1)
    p = sub.add_parser("click"); p.set_defaults(fn=cmd_click)
    p.add_argument("x", type=int); p.add_argument("y", type=int)
    p.add_argument("n", type=int, nargs="?", default=1)

    args = ap.parse_args()
    if args.len == 0:
        args.len = None
    sys.exit(args.fn(args) or 0)


main()
