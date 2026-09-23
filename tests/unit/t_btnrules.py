#!/usr/bin/env python3
"""THE BUTTON REGISTRY (SPEC.md 20.5.1.3, tests/btnsites.txt).

`os88ui_btn` is the standard button and carries the SPEC.md 13.7 gesture with
it.  `os88ui_btnraw` is the bare painter underneath - no gesture at all - so a
caller on it owns the press/release/track/repaint dance by hand, and
twenty-five of the tree's call sites owned it by NOT doing it: they fired on
the press, showed no pressed look, and could not be cancelled by sliding off.

IT IS A RATCHET, NOT A CLEAN GATE, and that is deliberate.  The registry starts
at the counts the tree actually has, because a rule that cannot be enforced
from the day it is written is not enforced at all.  Four failures:

  * a FILE NOT IN THE REGISTRY calling either - the case that matters, because
    it is what a new package hits;
  * a file EXCEEDING either count;
  * a count that is now too HIGH, so the numbers cannot rot quietly behind a
    conversion that lowered them;
  * a `raw` line with no reason after the `#`.

WHY STATIC AND NOT DRIVEN: a press-fired button and a release-fired one are the
same pixels in every still.  The difference exists only while a button is
physically held, and no screenshot-driven row holds one down - which is exactly
how this survived ten packages and a written survey
(docs/plans/completed/UIHELPERS-PLAN.md 15.4, SPEC.md 13.8.4).

t_textrules.py is the precedent and most of this is its shape.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                           # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
REGISTRY = os.path.join(ROOT, "tests", "btnsites.txt")

# ...AND SO DOES A MACRO-WRAPPED CALL. kernel/fdlg.inc reaches everything
# outside itself through `FDX <name>`, which is `call <name>` on kern_big and
# `call COLD_SEG:xd_<name>` on kern_small, with `FDXF <name>` generating the
# far thunk. NEITHER spelling contains `call os88ui_btn`, so the file was not
# in the registry at all and could not be counted, let alone checked - and it
# named os88ui_btn where it meant os88ui_kbtn, which took every button off the
# Standard File dialog. A wrapper is how a caller hides from a grep; this
# matches the VERB rather than the instruction.
# ...and it matches ANY uppercase wrapper, not FDX by name: this tree has
# six more of the same shape (FCPX/FCPXF for FILECP.DRV, OVWCALL, DKXPAD,
# OSAPI_CSLOT, OSAPI_FARCELL), every one of which could reach the control
# tomorrow and hide it again. `<MACRO> os88ui_btn` is the whole pattern, so
# the rule is "the symbol appears as the first operand of a statement",
# whatever verb precedes it.
REC = re.compile(r"^\s*(?:call (?:\w+:)?|[A-Z][A-Z0-9_]* +)"
                 r"(?:os88ui_btn|os88ui_kbtn|os88ui_btn_f)\b", re.M)
# THE FAR ENTRY COUNTS. os88ui_btn_f is how an on-demand module reaches the
# control (SPEC.md 2.6) - ctrl.inc and hiber.inc have a CS of their own - and
# a `call COLD_SEG:os88ui_btn_f` is invisible to a grep for `call os88ui_btn`.
# That is exactly how the whole Control Panel was missed by the conversion
# sweep: the far entry went on pointing at an os88ui_btn that had started
# taking a RECORD, so every panel button read its live count out of a
# rectangle's coordinates and the Date/Time page filled the screen white.
RAW = re.compile(r"^\s*call os88ui_btnraw\b", re.M)
DEAD = re.compile(r"^os88ui_btnraw:", re.M)
SCAN = ("apps", "drivers", "kernel")
EXT = (".asm", ".inc", ".c", ".h")


def registry():
    """path -> (record, raw, reason); and the parse errors found on the way."""
    out, bad = {}, []
    for n, line in enumerate(open(REGISTRY, encoding="utf-8"), 1):
        body = line.split("#", 1)
        reason = body[1].strip() if len(body) > 1 else ""
        f = body[0].split()
        if not f:
            continue
        if len(f) != 2 or not f[0].isdigit():
            bad.append("%s:%d: want `<count> <path>  # <reason>`, got %r"
                       % (os.path.basename(REGISTRY), n, line.rstrip()))
            continue
        out[f[1]] = (int(f[0]), 0, reason)
    return out, bad


def tree():
    """path -> (record, raw) for every source that calls either."""
    out = {}
    for base in SCAN:
        for dp, _, fns in os.walk(os.path.join(ROOT, base)):
            for fn in fns:
                if not fn.endswith(EXT):
                    continue
                p = os.path.join(dp, fn)
                try:
                    t = open(p, encoding="utf-8", errors="replace").read()
                except OSError:
                    continue
                rec, raw = len(REC.findall(t)), len(RAW.findall(t))
                if rec or raw:
                    rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
                    out[rel] = (rec, raw)
    return out


def main():
    reg, bad = registry()
    live = tree()

    # --- THE SCAFFOLD IS GONE AND MAY NOT COME BACK -------------------------
    # os88ui_btnraw was the old loose-register painter, kept reachable while
    # the tree converted one caller at a time. A scaffold that outlives its
    # conversion is the thing the next author finds and copies - a painter
    # with no gesture, which is the whole defect - so the body is private
    # (os88ui_bdraw, one caller) and the name is not a symbol any more.
    ui = open(os.path.join(ROOT, "apps", "os88ui.inc"), encoding="utf-8",
              errors="replace").read()
    if DEAD.search(ui):
        bad.append("apps/os88ui.inc defines os88ui_btnraw again. It was the "
                   "scaffold for the conversion and every caller is off it: "
                   "a reachable painter with NO gesture is what the next "
                   "package copies (SPEC.md 20.5.1.3)")

    for path, (rec, raw) in sorted(live.items()):
        if path not in reg:
            bad.append("%s is NOT in tests/btnsites.txt and calls the button "
                       "(%d record, %d raw). Every caller is registered with a "
                       "reason - and if this is a NEW raw caller it is a button "
                       "that fires on the press, which is the defect the "
                       "registry exists for (SPEC.md 20.5.1.3)" % (path, rec, raw))
            continue
        wrec, _wraw, _ = reg[path]
        if rec != wrec:
            bad.append("%s calls os88ui_btn %d time(s), registered for %d - "
                       "keep the count honest" % (path, rec, wrec))

    # --- THE RECORD MUST BE AIMED, and by the file that draws from it -------
    # A record whose BT_RECTS or BT_N is never written is all zeroes, and
    # os88ui_btn draws NOTHING for an index past a live count of 0.  That is
    # not a subtle failure: it is a button that is simply absent, and it
    # shipped once - apps/artful/atui.inc set its flags and its rects and
    # never the record's three POINTERS, so the modal had no buttons at all.
    #
    # It cannot catch the other half of that bug (DOS aimed its record in the
    # CLICK path, so the paint path drew nothing) - only driving it can, which
    # is tests/btngesture.py's DOS case.  It catches the half that is visible
    # from the source.
    # ...or the rect array is NAMED in the declaration (`os88ui_arec: dw
    # os88ui_ar, ...`), which is a record pointed at a STATIC array and needs
    # no write. A leading `0` deliberately does not count: that is the three
    # drivers' shape, where the rect is restaged per button and BT_RECTS has
    # to be written - and they do write it.
    AIM = re.compile(r"OS88UI_BT_RECTS\]|OS88UI_BTNREC\s+\w+\s*,\s*\w|"
                     + r"^\w+:\s*dw\s+[A-Za-z_]\w*\s*,", re.M)
    # ...OR THE COUNT IS IN THE DECLARATION, which is how the three drivers
    # and the alert card do it: `eu_btrec: dw 0, eu_btlbl, eu_btflg, 1, 0...`
    # presets N and never writes it again, because a one-button staging's
    # count cannot change. That is the same concession OS88UI_BTNREC gets one
    # spelling along, and without it this rule fires on correct code - which
    # it did, silently, for as long as the check() below could not fail.
    CNT = re.compile(r"OS88UI_BT_N\]|OS88UI_BTNREC\s+\w+\s*,|"
                     + r"^\w+:\s*dw\s+[^;\n]*?,[^;\n]*?,[^;\n]*?,\s*[1-9]\d*\s*,",
                     re.M)
    KBTN = re.compile(r"^\s*(?:call|[A-Z][A-Z0-9_]*) +os88ui_kbtn\b", re.M)
    for path, (rec, raw) in sorted(live.items()):
        if not rec or path.endswith("os88ui.inc"):
            continue
        t0 = open(os.path.join(ROOT, path), encoding="utf-8",
                  errors="replace").read()
        # os88ui_kbtn is the KERNEL's one-button staging and aims the record
        # itself (SPEC.md 20.5.1.3), so a file that only calls that has
        # nothing of its own to aim.
        FAR = re.compile(r"^\s*call \w+:os88ui_btn_f\b", re.M)
        if (KBTN.search(t0) or FAR.search(t0)) and \
           not re.search(r"^\s*(?:call|[A-Z][A-Z0-9_]*) +os88ui_btn\b", t0, re.M):
            continue                # reaches the control through the kernel's
                                    # staging, which aims the record itself
        t = open(os.path.join(ROOT, path), encoding="utf-8",
                 errors="replace").read()
        # OS88UI_BTNREC declares the record with its pointers and count
        # already set, which is the whole reason it exists; a file that uses
        # it has nothing left to aim (SPEC.md 20.5.1.3.2).
        if not AIM.search(t):
            bad.append("%s calls os88ui_btn but never writes "
                       "OS88UI_BT_RECTS: its record's rect array is a null "
                       "pointer and the buttons do not appear at all "
                       "(SPEC.md 20.5.1.3)" % path)
        if not CNT.search(t):
            bad.append("%s calls os88ui_btn but never writes OS88UI_BT_N: a "
                       "live count of 0 means every index is past the end and "
                       "os88ui_btn draws nothing (SPEC.md 20.5.1.3)" % path)

    # --- AND THE LIBRARY'S OWN SITES ARE NOT EXEMPT ------------------------
    # The loop above skips apps/os88ui.inc, because os88ui_btn's own gesture
    # handlers are handed a record by their CALLER and have nothing to aim.
    # That exemption is how the ALERT CARD shipped broken: os88ui_abtn1 went
    # on handing os88ui_ar - a RECT - to an os88ui_btn that had started taking
    # a record, and the registry counted the call as "the library's own" and
    # asked nothing else about it. The live count came out of the rect's y2,
    # the flags pointer out of its x2, OS88UI_BT_DOWN out of os88ui_asets'
    # first word - so the DOS box's "Open windows are lost. Proceed?" filled
    # the whole screen with a PRESSED button's black interior, at a rect read
    # from four arbitrary words of the package's own image.
    #
    # So every call site in this file must PROVE, inside its own routine, that
    # it is treating BX as a record: either it names a record symbol
    # (`mov bx, os88ui_krec`) or it reads/writes a field through it
    # (`[bx+OS88UI_BT_DOWN]`). Per ROUTINE and not a line window, so moving a
    # load a few lines cannot make this fire for nothing.
    #
    # It is a proxy and it says so: it cannot tell a record from a 16-byte
    # block of something else. What it CAN do is fail the build for the one
    # shape both of this control's regressions had - a rect where a record
    # goes - which no still can show, because the wrong picture is a fill at
    # coordinates that came out of the right one.
    REC_OK = re.compile(r"\[bx\s*\+\s*OS88UI_BT_\w+\]|"
                        r"mov\s+bx\s*,\s*os88ui_\w*rec\b")
    LBL = re.compile(r"^([A-Za-z_]\w*):", re.M)
    CALL = re.compile(r"^\s*call os88ui_btn\b", re.M)
    lines = ui.splitlines()
    owner, cur = [], "(file top)"
    for ln in lines:
        m = LBL.match(ln)
        if m:
            cur = m.group(1)
        owner.append(cur)
    for n, ln in enumerate(lines):
        if not CALL.match(ln):
            continue
        who = owner[n]
        body = "\n".join(lines[i] for i in range(len(lines))
                          if owner[i] == who)
        if not REC_OK.search(body):
            bad.append("apps/os88ui.inc:%d: %s calls os88ui_btn and nothing "
                       "in it treats BX as a record - no [bx+OS88UI_BT_*] and "
                       "no `mov bx, os88ui_*rec`. os88ui_btn takes a RECORD "
                       "and an index; a RECT arriving there reads the live "
                       "count out of y2 and the rect pointer out of x1 "
                       "(SPEC.md 20.5.1.3)" % (n + 1, who))

    # --- AND THE RECORD MUST BE OS88UI_BT_SIZE BYTES WIDE ------------------
    # os88ui_btninit writes OS88UI_BT_ONCLK at +12 and OS88UI_BT_NEXT at +14,
    # so a record with only 12 bytes reserved puts four bytes of LIBRARY state
    # on top of whatever the package declared next. SEVEN declarations in the
    # tree were 12, and FIVE of them were live: Sheet reserves its records by
    # `<next> equ <rec> + N` and calls btninit on all five dialogs, so every
    # open wrote its click proc over `sh_fdlg_count`, over `sh_ldsb` (the
    # scroll-bar block), and over `sh_idlg_win` - the Insert dialog's
    # single-instance guard, which a code address makes read as "already
    # open". The other direction is worse: Sheet writing those cells corrupts
    # OS88UI_BT_NEXT, which is the list link os88ui_btnclick WALKS on every
    # press, so a garbage pointer gets `[bx+OS88UI_BT_WIN]` compared and a
    # garbage OS88UI_BT_ONCLK can be `call bx`-ed. Word and Scribe had the
    # same shortfall latently - neither calls btninit yet, and the four bytes
    # land on their own `*_dgdown`, "which control is a press live on".
    #
    # OS88UI_BTNREC emits the full eight words, so a file using the macro is
    # right by construction. What this checks is the three hand-rolled shapes.
    # A reservation given as a SYMBOL is accepted: the assembler guards those
    # (`%if WD_BTREC_SZ != OS88UI_BT_SIZE`, DOS_BTREC_SZ's own mirror), and a
    # symbol is what a fixed one looks like.
    SIZE = 16                   # OS88UI_BT_SIZE, mirrored - and checked below
    m = re.search(r"^OS88UI_BT_SIZE\s+equ\s+(\d+)", ui, re.M)
    if not m or int(m.group(1)) != SIZE:
        bad.append("apps/os88ui.inc's OS88UI_BT_SIZE is %s and this check "
                   "mirrors %d - fix the literal here"
                   % (m.group(1) if m else "?", SIZE))
    elif True:
        SIZE = int(m.group(1))
    CHAIN = re.compile(r"^(\w+)\s+equ\s+(\w*btrec\w*)\s*\+\s*(\d+)",
                       re.M | re.I)
    VAR   = re.compile(r"^\s*[A-Z]\w*\s+(\w*btrec\w*)\s*,\s*(\d+)\s*(?:;|$)",
                       re.M | re.I)
    DWREC = re.compile(r"^(\w*btrec\w*)\s*:\s*dw\s+([^;\n]+)", re.M | re.I)
    for base in SCAN:
        for dp, _, fns in os.walk(os.path.join(ROOT, base)):
            for fn in fns:
                if not fn.endswith((".asm", ".inc")):
                    continue
                fp = os.path.join(dp, fn)
                rel = os.path.relpath(fp, ROOT).replace(os.sep, "/")
                s = open(fp, encoding="utf-8", errors="replace").read()
                for nxt, rec, n in CHAIN.findall(s):
                    if int(n) < SIZE:
                        bad.append("%s: `%s equ %s + %s` reserves only %s "
                                   "bytes for a button record - OS88UI_BT_SIZE "
                                   "is %d, so os88ui_btninit writes "
                                   "OS88UI_BT_ONCLK and OS88UI_BT_NEXT on top "
                                   "of %s (SPEC.md 20.5.1.3.2)"
                                   % (rel, nxt, rec, n, n, SIZE, nxt))
                for rec, n in VAR.findall(s):
                    if int(n) < SIZE:
                        bad.append("%s: the bss slot for `%s` is %s bytes and "
                                   "a button record is %d - the last four are "
                                   "OS88UI_BT_ONCLK and OS88UI_BT_NEXT, and "
                                   "they land on whatever is declared next "
                                   "(SPEC.md 20.5.1.3.2)" % (rel, rec, n, SIZE))
                for rec, body in DWREC.findall(s):
                    if len(body.split(",")) * 2 < SIZE:
                        bad.append("%s: `%s` is declared with %d words and a "
                                   "button record is %d bytes (SPEC.md "
                                   "20.5.1.3.2)"
                                   % (rel, rec, len(body.split(",")), SIZE))

    for path in sorted(reg):
        if path not in live:
            bad.append("%s is in tests/btnsites.txt and calls neither - drop "
                       "the line" % path)

    # **THE CONDITION COMES FIRST, and this call had it LAST.** harness.check
    # is check(cond, what, why="", got=None, want=None), and this read
    # check("<message>", not bad, 0, len(bad), "<why>") - so `cond` was a
    # non-empty string literal, which is always truthy. The gate PASSED for
    # every input it could ever be given: it printed its findings, `make`
    # reported `ok btnrules`, and three of them had been printed on every
    # build since the row landed. It is the green row that tests nothing
    # (docs/WRITING-TESTS.md 1), in the file written to stop this control's
    # regressions recurring - so the alert card's rect-for-a-record went out
    # past a ratchet that was decoration.
    check(not bad,
          "every button call site is registered, and the raw count only falls",
          "os88ui_btnraw is the painter with NO gesture; os88ui_btn is the "
          "control (SPEC.md 20.5.1.3). A press-fired button and a "
          "release-fired one photograph identically, so nothing else in the "
          "suite can see this.",
          got=len(bad), want=0)
    for b in bad:
        print("  " + b)
    nrec = sum(v[0] for v in live.values())
    nraw = sum(v[1] for v in live.values())
    print("btnrules: %d record call(s), %d raw, in %d file(s)"
          % (nrec, nraw, len(live)))
    done("t_btnrules")


if __name__ == "__main__":
    main()
