#!/usr/bin/env python3
"""PageDesk's assembly reads a STACK argument through SS, never DS (QA F32).

    python3 tests/unit/t_pdssarg.py

CLAUDE.md's near model: SS is LOW_SEG and DS is the package, so after
`mov bx, sp` (or si, di) a stack slot is `[ss:bx+2]`. Without the override
the instruction assembles cleanly and reads the package's own image at SP's
offset. pduslot.inc's `_pdu_claimhi` did exactly that: the overlay's edit
store asked for `[bx+2]` paragraphs, got whatever DS held there (0.5 KiB on
TTSIMPLE.PDF), and the 16 KiB store was laid over claim A - a typed note's
text was overwritten by the page's content stream and Save As wrote those
bytes into the annotation's /Contents (QA F32).

Every .inc/.asm under apps/pagedesk/: from a `mov R, sp` (R = bx, si, di) to
the end of that routine (the next global label, a ret/retf or a jmp, or R
written again), a memory operand based on R must carry `ss:`. A negative
control runs the same scan over pduslot.inc with the override taken out of
`_pdu_claimhi`, and must find it. Host-side, ~0 s.
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PD = os.path.join(ROOT, "apps", "pagedesk")

LABEL = re.compile(r"^([A-Za-z_][\w.]*):")          # a global label (no dot first)
MOVSP = re.compile(r"^\s*mov\s+(bx|si|di)\s*,\s*sp\b", re.I)
END = re.compile(r"^\s*(retf?|iret|jmp)\b", re.I)


def scan(lines, name):
    """[(file:line, text)] of R-based operands without ss: after mov R, sp"""
    bad = []
    reg = None
    for n, raw in enumerate(lines, 1):
        ln = raw.split(";", 1)[0]
        if LABEL.match(ln):
            reg = None
        m = MOVSP.match(ln)
        if m:
            reg = m.group(1).lower()
            continue
        if not reg:
            continue
        for op in re.findall(r"\[([^\]]*)\]", ln):
            base = re.split(r"[+\-*\s]", op.strip().lower(), maxsplit=1)[0]
            if base.endswith(":"):
                continue
            if base == reg or re.match(r"^%s\b" % reg, op.strip().lower()):
                if not op.strip().lower().startswith("ss:"):
                    bad.append(("%s:%d" % (name, n), raw.rstrip()))
        # R written again (as a destination) ends the window
        if re.match(r"^\s*(mov|pop|xchg|lea|add|sub|xor|inc|dec|and|or)\s+%s\b" % reg, ln, re.I):
            reg = None
        if END.match(ln):
            reg = None
    return bad


def main():
    files = sorted(glob.glob(os.path.join(PD, "*.inc")) + glob.glob(os.path.join(PD, "*.asm")))
    check(len(files) >= 20, "PageDesk's assembly sources found (%d)" % len(files))
    for f in files:
        name = os.path.basename(f)
        for where, text in scan(open(f, encoding="latin-1").read().splitlines(), name):
            check(False, "%s reads a stack slot through DS: %s" % (where, text.strip()))
    # negative control: pd_claimhi's C entry without the override must be caught
    src = open(os.path.join(PD, "pduslot.inc"), encoding="latin-1").read()
    check("mov ax, [ss:bx+2]" in src, "_pdu_claimhi reads its argument as [ss:bx+2]")
    broken = src.replace("mov ax, [ss:bx+2]", "mov ax, [bx+2]", 1)
    hits = scan(broken.splitlines(), "pduslot.inc(control)")
    check(any("[bx+2]" in t for _, t in hits),
          "negative control: [bx+2] after mov bx, sp is flagged (%d)" % len(hits))
    return done("t_pdssarg")


if __name__ == "__main__":
    sys.exit(main())
