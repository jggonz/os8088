# =============================================================================
# pkg.py - find a running package, and resolve its labels
#
# The core of tools/notepad: everything else here builds on these two answers.
#
# WHY IT LOOKS LIKE THIS. docs/NOTEPAD-NOTES.md 6.3 is a probe that computed
# its field addresses from the size of the wrong binary and reported plausible
# garbage - len=59587, vrows=10546 - rather than an error. Both answers below
# are therefore SELF-VALIDATING rather than derived:
#
#   the segment   is found by scanning RAM for the package's own 32-byte
#                 header (SPEC.md 20.2) and matching the name field, so
#                 nothing is inferred from where the heap "should" have put it
#   the labels    come from a NASM listing, and `verify()` checks that listing's
#                 binary against guest memory BYTE FOR BYTE
#
# Neither is expensive and both caught a wrong answer in one session.
# =============================================================================
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import os88marty as M                                       # noqa: E402

CPU_HZ = 4772727.0          # 4.77MHz 8088 - the machine every figure is in


def ms(cycles):
    """Guest cycles as milliseconds on the target machine.

    docs/NOTEPAD-NOTES.md 6.4: MartyPC here runs the guest at whatever
    multiple of real time the host manages, so WALL time means nothing and
    the cycle count means everything. Convert, never measure.
    """
    return cycles * 1000.0 / CPU_HZ


# --- labels ------------------------------------------------------------------

class Labels:
    """`name` and `name.local` -> image offset, out of a `-f bin` listing.

    A label line emits no bytes of its own, so its address is the address of
    the next line that does. Locals are qualified with the global above them,
    so `np_redraw.band` is the name to ask for.
    """

    _ADDR = re.compile(r"^\s*(\d+)\s+([0-9A-F]{8})\s")
    _LAB = re.compile(r"^\s*\d+\s+(?:[0-9A-F]{8}\s+(?:[0-9A-F<>rep\-]+\s+)?)?"
                      r"(\.?[A-Za-z_][A-Za-z0-9_.]*):")

    def __init__(self, lst_path):
        self.addr = {}
        pending, cur = [], None
        for line in open(lst_path, encoding="utf-8", errors="replace"):
            m = self._LAB.match(line)
            if m:
                nm = m.group(1)
                if nm.startswith("."):
                    nm = (cur or "") + nm
                else:
                    cur = nm
                pending.append(nm)
            a = self._ADDR.match(line)
            if a:
                off = int(a.group(2), 16)
                for nm in pending:
                    self.addr.setdefault(nm, off)
                pending = []

    def __getitem__(self, name):
        if name not in self.addr:
            raise KeyError("no such label in the listing: %s" % name)
        return self.addr[name]

    def __contains__(self, name):
        return name in self.addr


# --- the machine -------------------------------------------------------------

class Lab:
    """A headless MartyPC with one os8088 package under the microscope."""

    def __init__(self, addr="127.0.0.1:9001", timeout=120):
        self.m = M.Marty(addr, timeout=timeout)
        self.seg = None

    # -- finding the package, by its own header ------------------------------
    def find_pkg(self, name=b"NOTEPAD", lo=0x1000, hi=0xA000):
        """Scan paragraphs for a v3 package header carrying `name`.

        A package is loaded on a paragraph boundary off the top of the heap
        (SPEC.md 20.1/50.3), so the scan is over paragraphs and the match is
        'O8' + version 3 + the 16-byte name field. Returns the LAST hit: the
        heap hands regions out downward, so a second instance of the same
        package sits below the first and is the one most recently launched.
        """
        hits = []
        step = 0x400                                    # 16KB a read
        for base in range(lo, hi, step):
            n = min(step, hi - base) * 16
            try:
                data = self.m.read(base * 16, n)
            except Exception:
                continue
            for i in range(0, len(data) - 32, 16):
                if data[i] == 0x4F and data[i + 1] == 0x38 and data[i + 2] == 3:
                    if bytes(data[i + 16:i + 32]).split(b"\0")[0] == name:
                        hits.append(base + i // 16)
        if not hits:
            raise SystemExit("notepad/lab: no %s package header in RAM - is it "
                             "open?" % name.decode())
        self.seg = hits[-1]
        return self.seg, hits

    # -- reading -------------------------------------------------------------
    def rd(self, off, n):
        return self.m.readseg(self.seg, off, n)

    def w(self, off):
        b = self.rd(off, 2)
        return b[0] | (b[1] << 8)

    def b(self, off):
        return self.rd(off, 1)[0]

    def cycles(self):
        return self.m.status()["cycles"]

    # -- the check that closes docs/NOTEPAD-NOTES.md 6.3 ---------------------
    def verify(self, bin_path):
        """Guest memory against the built image, byte for byte.

        os88marty.py's `verify` for the kernel, for a package. Run it after
        every rebuild: a stale disk, a stale listing and a stale emulator all
        look exactly like a change that did nothing.
        """
        img = open(bin_path, "rb").read()
        guest = self.rd(0, len(img))
        return len(img), sum(1 for a, b in zip(img, guest) if a != b)
