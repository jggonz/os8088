#!/usr/bin/env python3
r"""REDLINE's chipset WRITE path inside the OS, on QEMU (SPEC.md 103.6.3-103.6.5).

QEMU BY NAME, and it is on docs/TESTING.md's list for the reason this needs:
the write path is refused on an 8088 - every button greyed, and on a 5150
port 63h is the 8255's control register, so a write there would reconfigure
the keyboard's PPI. MartyPC is an 8088. QEMU's 386 is an AT, where 63h, 72h
and 73h decode to nothing that matters, so the FE3600 row's writes land
harmlessly and the whole flow - the Tune tab, pick, Target, confirm, Tune,
Save setting, RLAPPLY - can run on the real package. What it cannot show is
a TIMING: no read ever goes wrong in QEMU and no R4 makes anything faster, so
Tune keeps the book's value (or, on a jitter, a step below it). The sweep's
verdicts - wrong reads, no gain, nothing to time - are tests/csprobe.py's,
against a board model that does go wrong and does get faster.

Asserted:
  1. On the Tune tab, picking the FE3600 (which nothing detects here), the
     Target Mfg spec and pressing Tune (T) asks first; Proceed sets R4
     10, the data sheet's, and the state field reads `Mfg spec: 10 waits`.
  2. Target Fastest tested and Tune SETS what it kept - method 3, no alert
     (the question it used to ask is gone, 103.6.3) - and the state field
     names that value: `Mfg spec: 10 waits`, or `Tuned: n waits` below it.
  3. Save setting (K) writes SYSTEM\\APPDATA\\REDLINE.CFG on the BOOT disk,
     and its bytes are 103.6.4's layout with this machine's fingerprint - the
     CRC of the BIOS read out of the guest, its date and its model byte.
  4. Quit (Q) on the Tune tab closes REDLINE.
  5. RLAPPLY.O88 (103.6.5), launched from A:\SYSTEM before the Save, toasts
     `No REDLINE.CFG to apply` and leaves no window; after it, `Chipset
     setting applied`, and again no window - its entry ends in the loader's
     abort, and the toast it raised is the one that stays up.

Break it: drop OD_BOOTVOL from redline.asm and the file lands on B: and
step 3 fails; drop the model byte from cs_cfg_new and step 3's compare
fails.
"""
import os
import shutil
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests"),
                os.path.join(ROOT, "tests", "unit")]
os.chdir(ROOT)

import os88geom as G        # noqa: E402
import os88qemu             # noqa: E402
import os88sym              # noqa: E402
import dispcp               # noqa: E402
import ethernet as eth      # noqa: E402
import redline_profile as R  # noqa: E402

S = os88sym.linear
SOCK = "build/qmp.sock"
SYSIMG = "build/rltune-sys.img"
APPIMG = "build/rltune-apps.img"


def say(*a):
    print(*a, flush=True)


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
            c &= 0xFFFF
    return c


def settle(m, card=None):
    os88qemu.ui_done(m, S, cap=2.0)


def key(m, k):
    m.hmp("sendkey " + k)
    os88qemu.pace(m, 0.1)


def until(what, cond, secs=30.0):
    end = time.time() + secs
    while time.time() < end:
        if cond():
            return
        time.sleep(0.25)
    raise RuntimeError("timed out: " + what)


class Pkg:
    """REDLINE's image, read out of the guest by its window's W_SEG."""

    def __init__(self, m, sym):
        self.m, self.sym = m, sym
        w = next(w for w in G.windows(m, S) if w.title == "REDLINE")
        raw = m.read(S("wm_wins") + w.i * G.WIN_SIZE, G.WIN_SIZE)
        self.seg = struct.unpack_from("<H", raw, G.W_SEG)[0]

    def b(self, name, n=1):
        return self.m.readseg(self.seg, self.sym[name], n)

    def w(self, name):
        return struct.unpack("<H", self.b(name, 2))[0]


def open_redline(m, mo):
    dispcp.open_drive(m, mo, S, settle, "B")
    wins = dispcp.win_list(m, S)
    wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
    for _ in range(4):
        dispcp.open_named(m, mo, S, settle, wx, wy, "REDLINE.O88")
        try:
            until("REDLINE's window", lambda: any(
                w.title == "REDLINE" and w.visible for w in G.windows(m, S)), 20)
            return
        except RuntimeError:
            say("   (the launch did not take - asking again)")
    raise RuntimeError("REDLINE.O88 never opened")


def toast(m):
    return m.read(S("toast_buf"), 32).split(b"\0")[0].decode("latin-1")


def rlapply(m, mo, want, state):
    """Launch A:\\SYSTEM\\RLAPPLY.O88 and wait for its toast. `state` keeps
    the A: Disk window between calls (it is left standing in SYSTEM)."""
    if "a" not in state:
        before = set(dispcp.win_list(m, S))
        dispcp.open_drive(m, mo, S, settle, "A")
        new = [i for i in dispcp.win_list(m, S) if i not in before]
        if not new:
            raise RuntimeError("opening drive A: left no new window")
        state["a"] = new[0]
        wx, wy = dispcp.win_rect(m, S, new[0])[:2]
        dispcp.open_named(m, mo, S, settle, wx, wy, "SYSTEM")
    wx, wy = dispcp.win_rect(m, S, state["a"])[:2]
    mo.click(wx + 60, wy + 5)            # RAISE it: the QEMU path names the
    settle(m)                            # ACTING Disk window's listing
    before = [w.title for w in G.windows(m, S)]
    dispcp.open_named(m, mo, S, settle, wx, wy, "RLAPPLY.O88",
                      expect="refusal")
    until("RLAPPLY's toast %r" % want, lambda: toast(m) == want)
    after = [w.title for w in G.windows(m, S)]
    if after != before:
        raise RuntimeError("RLAPPLY left windows %r (had %r)" % (after, before))


def chipset(m, p):
    key(m, "h")
    until("the Tune tab identifies and measures", lambda: p.b("cs_tab") == b"\1"
          and p.b("cs_measd") == b"\1")


def pick(m, mo, p, rec, item):
    """Open the drop-down `rec` and take its `item`."""
    x1, y1, x2, y2 = struct.unpack("<4H", p.b(rec, 8))
    mo.click((x1 + x2) // 2, (y1 + y2) // 2)
    until("the %s list opens" % rec, lambda: p.b(rec, 17)[16] == 1)
    top = struct.unpack("<H", p.b(rec, 24)[22:24])[0]
    mo.click((x1 + x2) // 2, top + 1 + item * 12 + 6)
    until("the %s list closes" % rec, lambda: p.b(rec, 17)[16] == 0)


def stat(p):
    return p.b("cs_stat", 19).decode("ascii").rstrip()


def proceed(m, mo, p):
    """The alert's Proceed: ADANGER's second button, the last one drawn, so
    os88ui_ar holds its rect once the alert has painted."""
    until("the alert", lambda: p.w("os88ui_awin") != 0)
    settle(m)
    x1, y1, x2, y2 = struct.unpack("<4H", p.b("os88ui_ar", 8))
    mo.click((x1 + x2) // 2, (y1 + y2) // 2)
    until("the alert goes", lambda: p.w("os88ui_awin") == 0)


def main():
    fails = []
    for src, dst in (("build/os8088.img", SYSIMG), ("build/redline.img", APPIMG)):
        if not os.path.exists(src):
            sys.exit("rltune: no %s - run `make` and `make redlinedisk`" % src)
        shutil.copyfile(src, dst)          # SCRATCH: the guest writes these
    sym = R.symbols()
    for n in ("os88ui_awin", "os88ui_ar", "cs_setv", "cs_meth", "cs_saved",
              "cs_tfast", "cs_tab", "cs_measd", "cs_pick", "cs_drop",
              "cs_tgt", "cs_target", "cs_stat"):
        if n not in sym:
            sys.exit("rltune: %s is not in tools/redline_profile.py's NAMES" % n)

    os88qemu.kill()
    os88qemu.own()
    r = subprocess.run(["make", "test", "TESTIMG=" + SYSIMG,
                        "TESTAPPS=" + APPIMG], capture_output=True, text=True)
    if r.returncode:
        sys.exit("rltune: make test failed:\n" + r.stdout + r.stderr)
    m = eth.Qemu(SOCK)
    mo = eth.Mouse()
    try:
        os88qemu.pace(m, 6.0)              # the boot, in the GUEST's seconds
        settle(m)
        state = {}
        rlapply(m, mo, "No REDLINE.CFG to apply", state)
        say("5a. RLAPPLY with nothing saved: No REDLINE.CFG to apply")
        open_redline(m, mo)
        p = Pkg(m, sym)
        chipset(m, p)
        # 1. pick the FE3600 (row 1) and the Target Mfg spec, then T
        pick(m, mo, p, "cs_drop", 1)
        until("the pick is the FE3600", lambda: p.b("cs_pick") == b"\1")
        # Before any write the field is the BIOS's or a MEASUREMENT (the ~,
        # SPEC.md 103.6.3.1) - and QEMU times nothing, its VGA being emulated
        # I/O, so what it measures is the host's. What it may never do is
        # claim, unmarked, a setting nobody wrote
        if not (stat(p) == "Stock: BIOS timing" or "~" in stat(p)):
            fails.append("the state field read %r before any write" % stat(p))
        pick(m, mo, p, "cs_tgt", 1)
        until("the Target is Mfg spec", lambda: p.b("cs_target") == b"\1")
        key(m, "t")
        proceed(m, mo, p)
        until("Mfg spec sets R4 10", lambda: p.b("cs_setv") == b"\x0a")
        if p.b("cs_meth") != b"\1":
            fails.append("Mfg spec's method is %r, want 1" % p.b("cs_meth"))
        if stat(p) != "Mfg spec: 10 waits":
            fails.append("the state field read %r after Mfg spec" % stat(p))
        say("1. FE3600 picked, Target Mfg spec, confirmed: R4 10, 'Mfg spec: 10 waits'")
        # 2. Fastest tested: QEMU reads everything right, so the floor - set,
        # unasked (the confirmation was given once, for the row)
        pick(m, mo, p, "cs_tgt", 0)
        until("the Target is Fastest tested", lambda: p.b("cs_target") == b"\0")
        key(m, "t")
        until("Tune sets what it kept", lambda: p.b("cs_meth") == b"\3")
        # QEMU's R4 writes land on nothing, so no step is FASTER and Tune
        # keeps the book's 10 (SPEC.md 103.6.3's keep rule); its I/O read
        # jitters by a few ns, though, so which step wins is not the
        # assertion - that whatever was kept is SET and NAMED is
        fast = p.b("cs_tfast")[0]
        if fast not in (10, 8, 6, 4) or p.b("cs_setv")[0] != fast:
            fails.append("Tune kept %d, set %d" % (fast, p.b("cs_setv")[0]))
        settle(m)
        if p.w("os88ui_awin") != 0:
            fails.append("Tune raised an alert; it asks nothing now")
        want = "Mfg spec: 10 waits" if fast == 10 else "Tuned: %d waits" % fast
        if stat(p) != want:
            fails.append("the state field read %r after Tune, want %r" % (stat(p), want))
        say("2. Tune kept R4 %d and set it, method 3, no question, %r" % (fast, want))
        # 3. Save setting, then the file on the BOOT image
        key(m, "k")
        until("Save", lambda: p.b("cs_saved") == b"\1", 60)
        settle(m)
        bios = m.read(0xF0000, 0x10000)
        want = (b"RLCFG\0\0\0" + b"F3" + bytes([3, 1])
                + struct.pack("<H", crc16(bios)) + bios[0xFFF5:0xFFFD]
                + bytes([bios[0xFFFE], 0]) + bytes([4, fast]))
        from t_image import Vol
        v = Vol(open(SYSIMG, "rb").read(), SYSIMG)
        got = None
        for folder, name, attr, clus, length in v.walk():
            if name == b"REDLINE CFG":
                chain, _ = v.chain(clus)
                got = b"".join(v.blob[v.cluster_lba(c) * 512:
                                      (v.cluster_lba(c) + v.spc) * 512]
                               for c in chain)[:length]
                where = folder
        if got != want:
            fails.append("REDLINE.CFG on the boot disk is %r, want %r" % (got, want))
        else:
            say("3. Save: %s REDLINE.CFG, fingerprint %04X" % (where, crc16(bios)))
        av = Vol(open(APPIMG, "rb").read(), APPIMG)
        if any(n == b"REDLINE CFG" for _, n, _, _, _ in av.walk()):
            fails.append("REDLINE.CFG landed on B: as well (OD_BOOTVOL)")
        # 4. Quit, on the Tune tab
        key(m, "q")
        until("Quit closes REDLINE", lambda: not any(
            w.title == "REDLINE" for w in G.windows(m, S)))
        say("4. Quit on the Tune tab closes REDLINE")
        settle(m)
        # EVERY Disk window closed and A: opened afresh. Raising the A: window
        # that is left is not enough: with B:'s closed on top of it, [fm_vp]
        # went on naming B:'s listing (seen here, not chased - it is the
        # kernel's, and not this row's subject)
        for i in reversed(dispcp.win_list(m, S)):
            x, y = dispcp.win_rect(m, S, i)[:2]
            mo.click(*G.close_xy(x, y))
            settle(m)
        until("no Disk window", lambda: not dispcp.win_list(m, S))
        state.clear()
        rlapply(m, mo, "Chipset setting applied", state)
        say("5b. RLAPPLY with REDLINE.CFG saved: Chipset setting applied")
    finally:
        try:
            m.quit()
        except Exception:
            pass
    if fails:
        for f in fails:
            say("FAIL " + f)
        sys.exit(1)
    say("rltune: all assertions passed")


if __name__ == "__main__":
    main()
