#!/usr/bin/env python3
"""Three run-time invariants that no `%if` can express, checked by WHO WRITES.

    python3 tests/unit/t_invariants.py

A `%if`/`%error` pins a CONSTANT.  These three are facts about the VALUES a
byte may hold while the machine is running, and the only thing the host can
ask about that is which lines in the tree store into it.  Each row is "this
byte has exactly these writers, in this file" - a new writer fails the build,
and a new writer that is fine adds itself here, in front of a reviewer.

They are gathered rather than left beside their routines because all three
have the same shape: a size change somewhere else removed a test, a loop or a
compare on the strength of one of them, and nothing anywhere says so.

1. [sch_cur] IS ALWAYS A LIVE SLOT, 0..MAX_TASKS-1.
   kernel/fsx.inc's ownership compares are `cmp ah, [sch_cur]` and nothing
   else, and 0xFF is [fsx_task]'s "no bracket".  They refuse the no-bracket
   case only because sch_cur can never BE 0xFF.  Park a "nothing running"
   sentinel there - which is this very corner of the tree's house idiom,
   sch_idleslot, fsx_task, fsx_worker and T_INST all use 0xFF for it - and the
   compare FAILS OPEN: both bytes read 0xFF, compare equal, and a bracket is
   granted to nobody.

2. [vid_mono], [vid_planes] AND [vid_planes_w] ARE ONE FACT, WRITTEN TOGETHER,
   AND ONLY BY vid_depth_set.  SPEC.md 39.26 deleted the software renderer's
   plane loop on that fact, and four bodies are written on it - sw_rect_pl,
   sw_fill_pat, ico_pass_bb and font_char_bb.  A second writer that moves
   [vid_mono] without [vid_planes] leaves all four drawing plane 0 alone, on
   every adapter, with no error anywhere.  viddet.inc's own header records the
   half of this that has already happened once: activating a VGA display with
   [vid_mono] still 1 pointed the software renderer at rseg 0 and splattered
   0xFF across kernel .text.

   **THE PAIR IS NOW ONE WORD STORE.**  vid_depth_set writes [vid_mono] and
   [vid_planes] with a single `mov [vid_mono], ax`, so between them there is
   ONE named writer and [vid_planes] has NONE - which is why the counts below
   are 1, 0 and 1 rather than three 2s.  That is strictly stronger than what
   this row used to check: an ISR cannot now observe mono = 1 beside
   planes = 4 between two byte writes.  It rests on the two bytes staying
   adjacent, mono first, and viddet.inc's own `%if` is what pins that.  The
   extra row below is the other half: the writer has to stay a WORD store,
   because a regression to `mov byte [vid_mono], 1` keeps the count at one,
   passes the row above, and silently stops setting the plane count.

3. [vid_rseg] IS WRITTEN ONLY BY vid_apply.
   This was checked separately from 2 because sw_xfer's pass loop terminated
   on a SEGMENT COMPARE - `add bx,[vid_rpara] / cmp bx,[vid_rend]` - and not
   on the plane count, so it was a DIFFERENT fact that broke the same bodies.
   That loop is gone (39.26's sweep reached it), which leaves [vid_rseg] the
   fact worth guarding here: it is the software renderer's TARGET SEGMENT, and
   a second writer that sets it while [vid_mono] disagrees is the 0xFF
   splatter in 2.
   [vid_rpara] and [vid_rend] WERE in this row, and they are not any more:
   with the loop that read them gone they were two cells written every
   vid_apply and read by nothing, so they left viddet.inc entirely rather
   than being guarded for ever.  A row naming a symbol that no longer exists
   reads 0 writers and fails, which is exactly how this file is meant to
   notice - and it is how it noticed this.

WHAT IT CANNOT SEE, and it matters that this is written down rather than
assumed: a write through a POINTER (`mov [bx], al` where BX happens to hold
the address), a `stos`/`movs`/`rep` that covers the byte as part of a run, a
WORD store through the symbol BELOW it (which is exactly how [vid_planes] is
written today - see 2), and a write inside a macro body that names the symbol
through a parameter.  It is a lint over `mov [sym], ...`, not a proof.  What it defends against is the
ordinary way a second writer arrives - somebody adding a line.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                            # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The whole tree, not just kernel/: a package or a driver cannot reach these
# bytes through DS today, but `apps/` and `drivers/` are where a future one
# would be written, and a grep that stops at kernel/ is REGISTER.md's own
# recurring failure (a claim of "module-private" made after grepping four
# directories and not the fifth).
SUBDIRS = ("kernel", "boot", "apps", "drivers")

# `mov [sym], x` / `mov byte [sym], x` / `mov word [es:sym], x`
WRITE = r"^\s*mov\s+(?:byte|word)?\s*\[\s*(?:[a-z]{2}:)?%s\s*\]\s*,\s*(.+?)\s*$"


def sources():
    for sub in SUBDIRS:
        for dirpath, _, files in os.walk(os.path.join(ROOT, sub)):
            for f in sorted(files):
                if f.endswith((".asm", ".inc")):
                    yield os.path.join(dirpath, f)


def writers(sym):
    """[(file, line, rhs)] - every `mov [sym], <rhs>` in the tree."""
    pat = re.compile(WRITE % re.escape(sym), re.I)
    out = []
    for path in sources():
        with open(path, encoding="utf-8", errors="replace") as f:
            for n, raw in enumerate(f.read().split("\n"), 1):
                m = pat.match(raw.split(";")[0])
                if m:
                    out.append((os.path.relpath(path, ROOT), n, m.group(1)))
    return out


def shown(w):
    return ", ".join("%s:%d <- %s" % r for r in w) or "(none)"


def one_owner(sym, nwrit, owner, what, why):
    w = writers(sym)
    check(len(w) == nwrit and all(p == owner for p, _, _ in w),
          what, why, got=shown(w),
          want="%d writer(s), all in %s" % (nwrit, owner))
    return w


def main():
    # --- 1. the running slot ------------------------------------------------
    w = one_owner(
        "sch_cur", 2, "kernel/sched.inc",
        "[sch_cur] has exactly two writers, both in sched.inc",
        "kernel/fsx.inc's ownership compares are `cmp ah, [sch_cur]` and "
        "nothing else. They refuse [fsx_task]'s 0xFF 'no bracket' ONLY "
        "because sch_cur can never be 0xFF. A third writer, or either of "
        "these two parking a sentinel there, grants a bracket to nobody - "
        "silently, and only when no bracket is up")
    bad = [r for r in w if "0xff" in r[2].lower() or r[2].strip() == "255"]
    check(not bad, "no writer parks a sentinel in [sch_cur]",
          "0xFF is this corner of the tree's own 'none' idiom (sch_idleslot, "
          "fsx_task, fsx_worker, T_INST) and is the one value the fsx "
          "ownership compares cannot survive",
          got=shown(bad), want="a slot index, always")

    # --- 2. the renderer's depth --------------------------------------------
    # 1, 0 and 1: vid_depth_set sets the first two with ONE word store through
    # [vid_mono], so [vid_planes] has no writer of its own name at all.
    why2 = ("SPEC.md 39.26 removed the software renderer's plane loop from "
            "sw_rect_pl, sw_fill_pat, ico_pass_bb and font_char_bb on the "
            "fact that [vid_mono] and [vid_planes] are set TOGETHER - mono "
            "1 and 1, VGA 0 and 4. A writer that moves one without the "
            "other leaves every one of those bodies drawing plane 0 alone, "
            "on every adapter, with nothing erroring. vid_depth_set now "
            "writes the pair as one word through [vid_mono], so the counts "
            "are 1, 0 and 1")
    for sym, n_w in (("vid_mono", 1), ("vid_planes", 0), ("vid_planes_w", 1)):
        one_owner(sym, n_w, "kernel/viddet.inc",
                  "[%s] is written only by vid_depth_set" % sym, why2)

    # ...and that one writer must stay a WORD store.  `mov byte [vid_mono], 1`
    # would keep the count at one, pass the row above, and stop setting
    # [vid_planes] - which is the precise failure this section exists for, so
    # it gets a row of its own rather than a comment.
    bytew = []
    for path in sources():
        with open(path, encoding="utf-8", errors="replace") as f:
            for ln, raw in enumerate(f.read().split("\n"), 1):
                if re.match(r"\s*mov\s+byte\s*\[\s*(?:[a-z]{2}:)?vid_mono\s*\]",
                            raw.split(";")[0], re.I):
                    bytew.append((os.path.relpath(path, ROOT), ln, raw.strip()))
    check(not bytew, "[vid_mono]'s writer is a WORD store, carrying [vid_planes]",
          "vid_depth_set sets the pair with one `mov [vid_mono], ax`. A BYTE "
          "store there writes mono and leaves the plane count behind, which "
          "the writer-count row above cannot see - it would still be one "
          "writer in the right file",
          got=shown(bytew), want="no `mov byte [vid_mono], ...` anywhere")

    # --- 3. the renderer's target segment -----------------------------------
    # [vid_rpara]/[vid_rend] were checked here too until they were deleted:
    # sw_xfer's loop was what read them and SPEC.md 39.26 took it, leaving
    # two cells that vid_apply wrote and nothing anywhere read.
    one_owner(
        "vid_rseg", 1, "kernel/viddet.inc",
        "[vid_rseg] is written only by vid_apply",
        "it is the software renderer's TARGET SEGMENT, and a second writer "
        "that sets it while [vid_mono] disagrees is the 0xFF splatter row 2 "
        "is about. vid_apply decides it from the depth vid_depth_set has "
        "just published, in one place")

    done("invariants")


if __name__ == "__main__":
    main()
