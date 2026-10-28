#!/usr/bin/env python3
"""The pin ratchet's RUNTIME half: every live claim at a quiet desktop moves,
or tests/pins.txt says why (docs/plans/NO-PINNING-PLAN.md 4).

    make && python3 tests/pinsrt.py
    python3 tests/pinsrt.py --red rloc     # the break-it-on-purpose arm
    python3 tests/pinsrt.py --red tag      # ...and the other one

tests/unit/t_pins.py reads the SOURCE: every claim site declares or has a
line. What it cannot see is a claim made by a path it does not follow - a
register it does not track, a wrapper it does not recognise, a claim made on
somebody else's behalf. So this boots the shipped 360KB pair on MartyPC,
reaches a QUIET desktop (no stream, no transfer, no fsx bracket - plan 5 #11:
a bucket-D or bucket-C pin is legitimately pinned while it is in use, and
nothing is in use here), opens two documents through their associations, and
reads `mem_tab` back. Every record must be one of:

  * MOVABLE - `MC_RLOC` != 0 (SPEC.md 66.2);
  * PURGEABLE - a `MEM_P_*` tag, which the compactor sheds (plan 6.2, 6.4);
  * a kernel tag with a line in tests/pins.txt - and then its BUCKET is held
    to what can be live at a quiet desktop: M, B, X and N can, C and D cannot.
    A C line is a claim that lives for one call; finding it here means the
    call did not give it back. A B line must also be TOP-DOWN (`MC_HI`);
  * a package REGION - t_movable's question (tests/movable.txt), checked here
    as the region record's own `MC_RLOC`;
  * a package's claim that its own directory's lines in tests/pins.txt
    excuse as LONG-LIVED - N, X or B. A package whose only lines are C or D
    has promised that nothing it pins survives the call, so a pinned claim of
    its at a quiet desktop is a finding.

Anything else fails, naming the owner and the tag. A pinned claim whose owner
is neither a kernel tag, an instance nor a known segment is the case the scan
can never see, and that is the one this row exists for.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests"),
                os.path.join(ROOT, "tests", "unit")]
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402

MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
MC_HI = os88geom.MC_DMA_HI
I_RECSZ, I_SPTR = os88geom.I_RECSZ, os88geom.I_SPTR
INST_MAX = os88geom.INST_MAX
I_NAME, I_STATE = 12, 0

# THE TWO DOCUMENTS. Each opens through its association, so the package that
# claims is the one a user would get, and both hold several data claims that
# the scan says are declared: Paint's canvas, undo and scratch (SPEC.md
# 66.5.1), Note Pad's document and undo arena (66.5.7).
OPEN = ("B:/APPS/PAINT.O88", "B:/MEDIA/PAPER.TEX", "BROWSER.HTM")
LIVE_OK = set("MBXN")           # what may still be pinned at a quiet desktop


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def tags():
    """{value: name} for every MEM_K_*/MEM_P_* tag, off kernel/memory.inc -
    derived, so a new tag needs no edit here."""
    src = open(os.path.join(ROOT, "kernel", "memory.inc")).read()
    pg = {m.group(1): int(m.group(2), 16) for m in re.finditer(
        r"^(MEM_PG_\w+)\s+equ\s+0x([0-9A-Fa-f]+)", src, re.M)}
    out = {}
    for m in re.finditer(r"^(MEM_[KP]_\w+)\s+equ\s+([^;\n]+)", src, re.M):
        name, expr = m.group(1), m.group(2).strip()
        if name.endswith("_N"):
            continue
        x = re.match(r"0x([0-9A-Fa-f]+)$", expr)
        if x:
            out[int(x.group(1), 16)] = name
            continue
        x = re.match(r"(MEM_PG_\w+)\s*<<\s*8\s*\|\s*0x([0-9A-Fa-f]+)$", expr)
        if x and x.group(1) in pg:
            out[pg[x.group(1)] << 8 | int(x.group(2), 16)] = name
    return out


def registry():
    """(kernel tag -> bucket, apps dir -> set of buckets), off tests/pins.txt
    through t_pins's own reader, so the two halves cannot read it apart."""
    import t_pins
    reg, _bad = t_pins.registry()
    ktag, pdir = {}, {}
    for (f, key), (bucket, _r) in reg.items():
        if f.startswith("kernel/") and key.startswith("MEM_"):
            ktag.setdefault(key, set()).add(bucket)
        elif f.startswith("apps/"):
            pdir.setdefault(f.split("/")[1], set()).add(bucket)
    return ktag, pdir


def movable_regions():
    """Packages tests/movable.txt exempts from the region declaration."""
    out = set()
    for ln in open(os.path.join(ROOT, "tests", "movable.txt")):
        p = ln.split("#")[0].split()
        if len(p) == 2 and p[0] == "region":
            out.add(p[1].split("/")[1])
    return out


def claims(m, S):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        b = i * MC_SIZE
        seg = u16(raw, b)
        if not seg:
            continue
        out.append({"i": i, "seg": seg, "para": u16(raw, b + 2),
                    "own": u16(raw, b + 4), "hi": bool(u16(raw, b + 6) & MC_HI),
                    "rloc": u16(raw, b + 8)})
    return out


def insts(m, S):
    raw = m.read(S("inst_tab"), INST_MAX * I_RECSZ)
    out = {}
    for i in range(INST_MAX):
        b = i * I_RECSZ
        if raw[b + I_STATE] != 1:
            continue
        name = raw[b + I_NAME:b + I_NAME + 16].split(b"\0")[0]
        out[i] = {"sptr": u16(raw, b + I_SPTR),
                  "name": name.decode("ascii", "replace")}
    return out


def pkgdir(name):
    """The apps/ directory an instance's name belongs to: the name with its
    extension off, lower-cased - and the directory must exist, or None."""
    base = name.split(".")[0].lower()
    return base if os.path.isdir(os.path.join(ROOT, "apps", base)) else None


def audit(m, S, when, fails, quiet=False):
    names = tags()
    ktag, pdir = registry()
    exempt = movable_regions()
    ins = insts(m, S)
    seg2slot = {v["sptr"]: k for k, v in ins.items()}
    n = {"movable": 0, "shed": 0, "kernel": 0, "region": 0, "pkg": 0}
    for c in claims(m, S):
        own, hi = c["own"], c["own"] >> 8
        tag = names.get(own)
        if 0xFB <= hi <= 0xFE:
            # a RANGE tag carries an ordinal (MEM_P_FATW + the volume,
            # MEM_P_WSAVE + the window): name it by the base below it
            base = max((v for v in names if v >> 8 == hi and v <= own),
                       default=None)
            label = "purgeable %s" % (names[base] + ("+%d" % (own - base)
                                                     if own != base else "")
                                      if base is not None else "%04X" % own)
        elif hi == 0xFF:
            label = "kernel %s" % (tag or "tag %04X" % own)
        elif own < INST_MAX:
            label = "slot %d (%s)" % (own, ins.get(own, {}).get("name", "?"))
        elif own in seg2slot:
            label = "%s's claim" % ins[seg2slot[own]]["name"]
        else:
            label = "owner %04X" % own
        if not quiet:
            print("  %s  %04X %5.1fK %-8s %-7s %s" % (
                when[:9], c["seg"], c["para"] / 64.0,
                "movable" if c["rloc"] else "PINNED",
                "top" if c["hi"] else "bottom", label))
        if 0xFB <= hi <= 0xFE:
            n["shed"] += 1
            continue
        if c["rloc"]:
            n["movable"] += 1
            continue
        if hi == 0xFF:
            n["kernel"] += 1
            b = ktag.get(tag or "", set())
            if not b:
                fails.append("%s: a PINNED kernel claim %s at %04X with no line "
                             "in tests/pins.txt" % (when, tag or "%04X" % own,
                                                    c["seg"]))
            elif not (b & LIVE_OK):
                fails.append("%s: kernel claim %s at %04X is PINNED at a quiet "
                             "desktop, and tests/pins.txt says it lives for one "
                             "call (bucket %s) - the call did not give it back"
                             % (when, tag, c["seg"], "/".join(sorted(b))))
            elif b == {"B"} and not c["hi"]:
                fails.append("%s: %s is bucket B (a boot CEILING claim) and sits "
                             "BOTTOM-up at %04X" % (when, tag, c["seg"]))
            continue
        slot = own if own < INST_MAX else seg2slot.get(own)
        if slot is None or slot not in ins:
            fails.append("%s: a PINNED claim at %04X (%.1fK) whose owner %04X "
                         "is no kernel tag, no instance and no package - the "
                         "case the source scan cannot see"
                         % (when, c["seg"], c["para"] / 64.0, own))
            continue
        inst = ins[slot]
        d = pkgdir(inst["name"])
        if own < INST_MAX and c["seg"] <= inst["sptr"] < c["seg"] + c["para"]:
            n["region"] += 1                # the REGION: t_movable's half
            if d not in exempt:
                fails.append("%s: %s's REGION at %04X is pinned and "
                             "tests/movable.txt does not exempt it"
                             % (when, inst["name"], c["seg"]))
            continue
        n["pkg"] += 1
        long_lived = pdir.get(d or "", set()) & {"N", "X", "B"}
        if not long_lived:
            fails.append("%s: %s holds a PINNED claim at %04X (%.1fK) at a quiet "
                         "desktop, and tests/pins.txt has no long-lived (N, X, "
                         "B) line for apps/%s/ - a C or D pin that outlived its "
                         "call, or a claim the scan never saw"
                         % (when, inst["name"], c["seg"], c["para"] / 64.0, d))
    print("pinsrt %s: %d movable, %d shed by design, %d pinned kernel, %d "
          "pinned region(s), %d pinned package claim(s)"
          % (when, n["movable"], n["shed"], n["kernel"], n["region"], n["pkg"]))
    return n


def red(m, S, how):
    """Break it on purpose, so the row is seen to go red (WRITING-TESTS 1)."""
    cs = claims(m, S)
    if how == "rloc":
        # a package's declared claim, its proc taken away: what an
        # OSAPI_MEM_MOVABLE that never ran leaves behind
        ins = insts(m, S)
        seg2slot = {v["sptr"]: k for k, v in ins.items()}
        for c in cs:
            if c["rloc"] and c["own"] in seg2slot:
                m.write(S("mem_tab") + c["i"] * MC_SIZE + 8, b"\0\0")
                print("RED: MC_RLOC of %s's claim at %04X zeroed"
                      % (ins[seg2slot[c["own"]]]["name"], c["seg"]))
                return
    if how == "tag":
        # a kernel claim under a tag tests/pins.txt does not know
        for c in cs:
            if c["own"] >> 8 == 0xFF or 0xFB <= c["own"] >> 8 <= 0xFE:
                b = S("mem_tab") + c["i"] * MC_SIZE
                m.write(b + 4, b"\x0E\xFF")         # MEM_K_ICO_UNUSED
                m.write(b + 8, b"\0\0")
                print("RED: the claim at %04X retagged MEM_K_ICO_UNUSED and "
                      "pinned" % c["seg"])
                return
    raise SystemExit("--red %s: nothing to break" % how)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--red", choices=("rloc", "tag"))
    ap.add_argument("--quiet", action="store_true",
                    help="no per-claim map, only the verdicts")
    a = ap.parse_args()
    fails = []
    with os88ui.boot(a.image, apps=a.apps, machine=a.machine,
                     quick=True) as ui:
        m, S = ui.m, ui.sym
        os88marty.quiesce(m, lambda: sorted(c["seg"] for c in claims(m, S)),
                          guest=1.0, what="the boot's claims")
        audit(m, S, "desktop", fails, a.quiet)
        for spec in OPEN:
            ui.path(spec)
            os88marty.quiesce(m, lambda: sorted(
                (c["seg"], c["rloc"]) for c in claims(m, S)),
                guest=1.0, what="%s's claims" % spec)
        os88marty.ui_done(m)
        if a.red:
            red(m, S, a.red)
        n = audit(m, S, "open", fails, a.quiet)
        if n["pkg"] + n["movable"] < 4:
            fails.append("only %d package/movable claims with two documents "
                         "open - the row is not looking at what it thinks"
                         % (n["pkg"] + n["movable"]))
    for f in fails:
        print("FAIL: " + f)
    print("pinsrt: %s" % ("FAILED (%d)" % len(fails) if fails else "ok"))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
