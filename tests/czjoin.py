#!/usr/bin/env python3
"""File > Uncompress on a PART of a split set: the join (SPEC.md 20.17, 22.23.5).

A set is cut on the host by tools/os88cz.py - the reference - and staged on a
scratch 1.44MB B:, and the machine joins it with the file manager's own verb.
Every assertion is about the bytes the machine WROTE, read back off the live
floppy, and about what its toast SAID:

  BIG.???    a 340KB original of text, noise and text, cut at 100,000 bytes a
             part, so it is SEVERAL parts, blocks both stored and LZ4, and a
             result far past 64KB. Uncompress on the MIDDLE part must write
             BIG.DAT byte for byte, leave every part alone and leave no
             CMPRESS~.TMP. The join is timed in guest seconds.
  LZB.001    a one-part set in LZB: the third method, and a set of one.
  NOTP.123   a plain file named like a part: `Not compressed`, and nothing
             written.
  MISS/      parts 1 and 3 of 3: `Missing MISS.002`, no MISS.DAT.
  DMG/       one byte flipped in part 2's STORED payload, which only the
             block's two sums can see (SPEC.md 20.17.2): `Cannot expand this
             one`, and the half-written result deleted.
  WRG/       part 2 from ANOTHER set of the same name: `Wrong part WRG.002`.
  EX/        a set whose result's name is already taken: `Name exists`, and
             the file that was there is untouched.

Then the volume is handed to os88disk's fsck, because a result deleted
half-way that leaked a cluster reads back perfectly everywhere else.

`--break` is the negative control (docs/WRITING-TESTS.md 1): it stages BIG
with a part MISSING and a byte damaged, and the row must go red.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88build                                       # noqa: E402
import os88cz                                          # noqa: E402
import os88flush                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88ui                                          # noqa: E402

MACHINE = "os8088_xt_vga_144"   # 1.44MB: see tests/lzbig.py - every 720KB
                                # profile here is 40-cylinder
QUIET = 400.0                   # guest seconds for the big join


def say(*a):
    print(*a, flush=True)


def half_text(n, seed):
    sys.path.insert(0, os.path.join(HERE, "unit"))
    import t_lzfmt
    return t_lzfmt.half_text(n, seed)


def noise(n, seed):
    x, out = seed, bytearray()
    while len(out) < n:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def stage(d, name, data):
    path = os.path.join(d, name)
    try:
        same = open(path, "rb").read() == data
    except OSError:
        same = False
    if not same:
        open(path, "wb").write(data)
    return path


def methods(parts):
    got = set()
    for p in parts:
        i = os88cz.CS_HDR
        while i < len(p):
            got.add(p[i + 4])
            i += os88cz.REC_HDR + int.from_bytes(p[i:i + 2], "little")
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--break", dest="brk", action="store_true",
                    help="stage BIG damaged: the row must FAIL")
    a = ap.parse_args()
    if not os.path.exists(os88build.at("build/os8088.img")):
        sys.exit("czjoin: build/os8088.img is missing - run `make` first")

    big = half_text(200000, 7) + noise(100000, 5) + half_text(40000, 3)
    bigp = os88cz.split(big, "BIG.DAT", 100000, os88cz.M_LZ4, jobs=1)
    if len(bigp) < 3 or methods(bigp) != {os88cz.M_STORE, os88cz.M_LZ4}:
        sys.exit("czjoin: BIG is %d part(s) with methods %r - the fixture "
                 "must be several parts, stored AND LZ4"
                 % (len(bigp), methods(bigp)))
    lzb = half_text(70000, 11)
    lzbp = os88cz.split(lzb, "LZB.DAT", "720k", os88cz.M_LZB, jobs=1)
    if len(lzbp) != 1 or os88cz.M_LZB not in methods(lzbp):
        sys.exit("czjoin: LZB is not a one-part LZB set")
    miss = os88cz.split(half_text(90000, 13), "MISS.DAT", 40000,
                        os88cz.M_STORE, jobs=1)
    dmg = os88cz.split(half_text(70000, 17), "DMG.DAT", 40000,
                       os88cz.M_STORE, jobs=1)   # STORED, so nothing but the
                                                # block's check can see it
    dmg[1] = bytearray(dmg[1])
    dmg[1][os88cz.CS_HDR + os88cz.REC_HDR + 5] ^= 0x10
    dmg[1] = bytes(dmg[1])
    wa = os88cz.split(half_text(70000, 19), "WRG.DAT", 40000,
                      os88cz.M_STORE, jobs=1)
    wb = os88cz.split(half_text(70000, 23), "WRG.DAT", 40000,
                      os88cz.M_STORE, jobs=1)
    exs = os88cz.split(b"new contents " * 100, "EX.DAT", "720k", jobs=1)
    exold = b"the file that was already here\r\n"
    for want, got in ((3, len(miss)), (2, len(dmg)), (2, len(wa))):
        if got < want:
            sys.exit("czjoin: a refusal fixture has too few parts")
    if a.brk:                   # the negative control: a set that must not
        bigp = bigp[:1] + bigp[2:]      # join - and would, if the machine
                                        # did not look at the order

    d = os.path.join(os88build.at("build"), "czjoin-%d" % os.getpid())
    for sub in ("", "MISS", "DMG", "WRG", "EX"):
        os.makedirs(os.path.join(d, sub), exist_ok=True)
    files = []
    for k, p in enumerate(bigp, 1):
        files.append(stage(d, "BIG.%03d" % k, p))
    files.append(stage(d, "LZB.001", lzbp[0]))
    files.append(stage(d, "NOTP.123", b"not a part at all\r\n" * 20))
    files.append("MISS:" + stage(os.path.join(d, "MISS"), "MISS.001", miss[0]))
    files.append("MISS:" + stage(os.path.join(d, "MISS"), "MISS.003", miss[2]))
    for k, p in enumerate(dmg, 1):
        files.append("DMG:" + stage(os.path.join(d, "DMG"), "DMG.%03d" % k, p))
    files.append("WRG:" + stage(os.path.join(d, "WRG"), "WRG.001", wa[0]))
    files.append("WRG:" + stage(os.path.join(d, "WRG"), "WRG.002", wb[1]))
    files.append("EX:" + stage(os.path.join(d, "EX"), "EX.001", exs[0]))
    files.append("EX:" + stage(os.path.join(d, "EX"), "EX.DAT", exold))
    img = "/tmp/czjoin-%d%s.img" % (os.getpid(), "b" if a.brk else "")
    disk = os88marty.scratch_disk(img, *files, size=1440)
    say("czjoin: BIG %d bytes in %d parts (%s), LZB %d in 1"
        % (len(big), len(bigp), "/".join(str(len(p)) for p in bigp),
           len(lzb)))
    fails = []

    with os88ui.boot(os88build.at("build/os8088.img"), apps=disk,
                     machine=MACHINE, verbose=False) as ui:
        m = ui.m
        fl = os88flush.Flush(marty=m)
        ui.open_drive("B")

        def uncompress(name, limit=60.0):
            """select `name` in the acting Disk window, File > Uncompress,
            and answer what the machine said and how long it took"""
            win = ui.raise_window(ui.disk_window())
            idx, _ = ui.entry(name, win)
            row = ui.scroll_to(idx, win=win)
            x, y = ui.row_xy(win, row)
            ui.mo.click(x, y)
            ui.settle()
            # the WHOLE buffer, and the answer once it stops changing: with
            # only byte 0 cleared, a read landing inside the kernel's copy of
            # "Missing MISS.002" came back "Mit compressed" - its first two
            # letters over the last leg's "Not compressed"
            m.write(ui._S("toast_buf"), bytes(25))
            c0 = int(m.status()["cycles"])
            ui.menu_pick("File", "Uncompress")
            ui.wait_toast(limit=limit)
            secs = (int(m.status()["cycles"]) - c0) / os88marty.GUEST_HZ
            t = os88marty.quiesce(m, lambda: ui.toast()[0],
                                  what="the toast's text to be whole")
            ui.settle()
            return t, secs

        def leg(tag, ok, msg):
            say("  %-9s %s  %s" % (tag, "ok " if ok else "BAD", msg))
            if not ok:
                fails.append("%s: %s" % (tag, msg))

        def vol():
            return fl.volume(1)

        def names(path=""):
            return {e.name.upper() for e in vol().listdir(path)}

        # --- the set that must join -------------------------------------
        t, s = uncompress("BIG.%03d" % min(2, len(bigp)), limit=QUIET)
        try:
            got = vol().read("BIG.DAT")
        except os88flush.FlushError:
            got = None
        rest = names()
        leg("big", t == "Uncompressed" and got == big
            and all("BIG.%03d" % k in rest for k in range(1, len(bigp) + 1))
            and "CMPRESS~.TMP" not in rest,
            "%r in %.1f guest s, BIG.DAT %s" % (
                t, s, "identical" if got == big else
                "MISSING" if got is None else "%d bytes, WRONG" % len(got)))
        t, s = uncompress("LZB.001")
        try:
            got = vol().read("LZB.DAT")
        except os88flush.FlushError:
            got = None
        leg("lzb", t == "Uncompressed" and got == lzb,
            "%r in %.1f guest s" % (t, s))
        t, s = uncompress("NOTP.123")
        leg("plain", t == "Not compressed" and "NOTP.DAT" not in names(),
            repr(t))

        # --- the refusals, a folder each ---------------------------------
        for folder, pick, expect, gone in (
                ("MISS", "MISS.003", "Missing MISS.002", "MISS.DAT"),
                ("DMG", "DMG.001", "Cannot expand this one", "DMG.DAT"),
                ("WRG", "WRG.001", "Wrong part WRG.002", "WRG.DAT")):
            ui.open(folder)
            t, s = uncompress(pick)
            here = names(folder)
            leg(folder.lower(), t == expect and gone not in here
                and "CMPRESS~.TMP" not in here,
                "%r (wanted %r), %s" % (t, expect, sorted(here)))
            ui.open("..")
        ui.open("EX")
        t, s = uncompress("EX.001")
        leg("exists", t == "Name exists" and vol().read("EX/EX.DAT") == exold
            and "CMPRESS~.TMP" not in names("EX"), repr(t))
        ui.open("..")

        chk = "/tmp/czjoin-fsck-%d.img" % os.getpid()
        fl.save(1, chk)
    r = subprocess.run([sys.executable,
                        os.path.join(HERE, "..", "tools", "os88disk.py"),
                        "--verify", chk], capture_output=True, text=True)
    leg("fsck", r.returncode == 0,
        (r.stdout + r.stderr).strip().splitlines()[-1:] or "")
    for f in (img, chk):
        try:
            os.remove(f)
        except OSError:
            pass
    shutil.rmtree(d, ignore_errors=True)
    for f in fails:
        say("  FAIL: " + f)
    say("czjoin: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
