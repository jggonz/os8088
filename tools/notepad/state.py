# =============================================================================
# state.py - Note Pad's bss, read out of a running guest
#
# The offsets are parsed from apps/notepad/notepad.asm, which declares its bss
# two ways - `NPVAR name, size` and `name equ os88_image_end + <expr>` - and
# both are relative to os88_image_end, which is the IMAGE SIZE. So the whole
# map hangs off one number, and that number is the size of the binary the
# guest is actually running.
#
# WHICH IS THE TRAP (docs/plans/completed/NOTEPAD-NOTES.md 6.3). Point this at a machine
# running a different build - the shipped NOTEPAD.O88 against npbench's, which
# differ by a kilobyte - and every field reads plausible garbage rather than
# erroring. `check()` is the guard and it REFUSES rather than warning: confirm
# np_len against the file you actually opened before believing anything else.
# =============================================================================
import os
import re

ASM = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "..", "apps", "notepad", "notepad.asm"))


def _consts(src):
    d = {}
    for m in re.finditer(r"^%define\s+(NP_[A-Z0-9_]+)\s+(\d+)", src, re.M):
        d[m.group(1)] = int(m.group(2))
    for m in re.finditer(r"^(NP_[A-Z0-9_]+)\s+equ\s+(\d+)", src, re.M):
        d[m.group(1)] = int(m.group(2))
    return d


def offsets(bin_path, asm=ASM):
    """{name: offset in the package segment} for every np_* bss word."""
    src = open(asm, encoding="utf-8", errors="replace").read()
    defs = _consts(src)
    base = os.path.getsize(bin_path)                    # os88_image_end
    out = {}

    def val(expr):
        e = expr
        for k, v in defs.items():
            e = re.sub(r"\b%s\b" % k, str(v), e)
        if not re.fullmatch(r"[0-9+\-*/() ]+", e):
            return None
        return eval(e)                                  # digits and operators only

    for m in re.finditer(r"^(np_[a-z0-9_]+)\s+equ\s+os88_image_end\s*\+\s*([^;\n]+)",
                         src, re.M):
        v = val(m.group(2).strip())
        if v is not None:
            out[m.group(1)] = base + v

    # NPVAR runs sequentially from whatever NPB was last %assign'd - and that
    # start is an EXPRESSION, `508 + NP_MAXROWS*2`. Matching only its leading
    # digits put every NPVAR-declared field 120 bytes out, which read as the
    # row index holding impossible values and sent a whole diagnosis at the
    # code instead of at this line (docs/plans/completed/NOTEPAD-NOTES.md 6.3, in the probe).
    npb = None
    for line in src.splitlines():
        m = re.match(r"^\s*%assign\s+NPB\s+([^;\n]+)", line)
        if m and "NPB +" not in m.group(1):     # not the macro's own bump
            v = val(m.group(1).strip())
            if v is not None:
                npb = v
            continue
        m = re.match(r"^\s*NPVAR\s+(np[a-z0-9_]*)\s*,\s*([0-9A-Za-z_*+ ]+)", line)
        if m and npb is not None:
            sz = val(m.group(2).strip())
            if sz is None:
                continue
            out[m.group(1)] = base + npb
            npb += sz
    return out


class State:
    """Read Note Pad's bss. Call check() before believing a single field."""

    # The fields worth printing for any scrolling or caret question.
    WORDS = ["np_len", "np_cur", "np_top", "np_ptop", "np_vrows", "np_rcols",
             "np_drows", "np_rowsn", "np_lastrow", "np_ckpr", "np_ckpi"]
    BYTES = ["np_ckok", "np_rowsok", "np_resume", "np_sigok", "np_curseen",
             "np_follow", "np_ekind", "np_hdirty"]

    def __init__(self, lab, bin_path):
        self.lab = lab
        self.bin = bin_path
        self.off = offsets(bin_path)

    def w(self, name):
        return self.lab.w(self.off[name])

    def b(self, name):
        return self.lab.b(self.off[name])

    def arr(self, name, n):
        base = self.off[name]
        return [self.lab.w(base + 2 * i) for i in range(n)]

    def check(self, want_len):
        """REFUSE unless np_len is the note that was opened.

        want_len is the file size AFTER the CR/LF fold, so README.TXT's 15,889
        bytes read back as 15,428. A mismatch means the offsets do not
        describe this guest - it does not mean "close enough".
        """
        got = self.w("np_len")
        if got != want_len:
            raise SystemExit(
                "notepad/lab: REFUSING - np_len reads %d, expected %d. The "
                "offsets come from %s; if that is not the binary the guest is "
                "running, every field here is garbage "
                "(docs/plans/completed/NOTEPAD-NOTES.md 6.3)." % (got, want_len, self.bin))
        return got

    def dump(self):
        d = {}
        for n in self.WORDS:
            if n in self.off:
                d[n] = self.w(n)
        for n in self.BYTES:
            if n in self.off:
                d[n] = self.b(n)
        return d
