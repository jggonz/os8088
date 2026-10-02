#!/usr/bin/env python3
"""OS88CZ.COM on a real DOS: the split set's DOS end (SPEC.md 20.17.4).

DOSBox carries its own DOS, runs headless (SDL's dummy drivers) and mounts a
host directory as C:, so the program is exercised as a DOS program - its own
int 21h calls, its own paths - with every assertion made on the host, against
tools/os88cz.py, the reference. One DOSBox session runs every leg from a
batch file, and each leg's output goes to a .LOG the host reads back:

  DOS -> host   S splits text, text-and-noise and a /S store; os88cz.py must
                join each to the original, and EVERY LZ4 block the program
                wrote must need no in-place margin (os88lz.in_place_margin).
                That is the one property of the stream a host decoder cannot
                see and the machine depends on: a cut anywhere but the first
                peak decodes perfectly here and overruns there.
  host -> DOS   J joins sets os88cz.py made - LZ4, LZB, stored and mixed,
                several parts - into OUT\\, byte for byte.
  CZ            U expands a 'CZ' file in each format.
  refusals      a set with a damaged STORED byte (only the check can see it),
                a part from another set, and a missing part on the drive the
                result is going to - which is NOT asked for, because that disk
                cannot go out (SPEC.md 20.17.4), so the Esc on redirected
                stdin is never read: each must say so and leave no result and
                no OS88CZ.$$$ behind.

`--break` is the negative control (docs/WRITING-TESTS.md 1): it hands J the
damaged set as if it were a good one, and the row must go red.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88build                                        # noqa: E402
import os88cz                                           # noqa: E402
import os88lz                                           # noqa: E402
import t_lzfmt                                          # noqa: E402

fails = []


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what, flush=True)
    if not ok:
        fails.append(what)


def noise(n, seed):
    x, out = seed, bytearray()
    while len(out) < n:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        out.append((x >> 16) & 0xFF)
    return bytes(out)


def dosbox(d, batch, limit=300):
    open(os.path.join(d, "RUN.BAT"), "w").write(
        "\r\n".join(batch + ["EXIT"]) + "\r\n")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    try:
        subprocess.run(["dosbox", "-c", "mount c %s" % d, "-c", "c:",
                        "-c", "RUN.BAT", "-c", "exit", "-noconsole"],
                       cwd=d, env=env, timeout=limit,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        fails.append("DOSBox did not finish in %d s - a prompt nobody "
                     "answered?" % limit)


def log(d, name):
    try:
        return open(os.path.join(d, name), "rb").read().decode("latin-1")
    except OSError:
        return ""


def parts_of(d, base):
    return [open(os.path.join(d, "%s.%03d" % (base, k)), "rb").read()
            for k in range(1, 1000)
            if os.path.exists(os.path.join(d, "%s.%03d" % (base, k)))]


def margins(parts, orig):
    """the worst in-place margin over every LZ4 block in `parts`"""
    worst, off = 0, 0
    for p in parts:
        i = os88cz.CS_HDR
        while i < len(p):
            s = int.from_bytes(p[i:i + 2], "little")
            n = int.from_bytes(p[i + 2:i + 4], "little")
            if p[i + 4] == os88cz.M_LZ4:
                pay = p[i + os88cz.REC_HDR:i + os88cz.REC_HDR + s]
                worst = max(worst, os88lz.in_place_margin(
                    orig[off:off + n], os88lz.LZ4, packed=pay))
            off += n
            i += os88cz.REC_HDR + s
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--break", dest="brk", action="store_true")
    a = ap.parse_args()
    if not shutil.which("dosbox"):
        print("czdos: SKIP - no dosbox on this machine (apt-get install -y "
              "dosbox)")
        return 0
    com = os88build.at("build/os88cz.com")
    if not os.path.exists(com):
        sys.exit("czdos: build/os88cz.com is missing - run `make`")

    txt = t_lzfmt.half_text(150000, 7)
    mix = t_lzfmt.half_text(70000, 3) + noise(70000, 5) + b"tail" * 3000
    store = t_lzfmt.half_text(80000, 9)
    hsets = {
        "HLZ4": (t_lzfmt.half_text(120000, 11), os88cz.M_LZ4, 50000),
        "HLZB": (t_lzfmt.half_text(90000, 13), os88cz.M_LZB, "720k"),
        "HMIX": (mix, os88cz.M_LZ4, 60000),
    }
    czs = {"CZ4.CZ": os88lz.LZ4, "CZB.CZ": os88lz.LZB}
    czdata = t_lzfmt.half_text(40000, 17)

    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "OUT"))
        os.makedirs(os.path.join(d, "DMG"))
        os.makedirs(os.path.join(d, "WRG"))
        os.makedirs(os.path.join(d, "MISS"))
        shutil.copy(com, os.path.join(d, "OS88CZ.COM"))
        open(os.path.join(d, "TXT.DAT"), "wb").write(txt)
        open(os.path.join(d, "MIX.DAT"), "wb").write(mix)
        open(os.path.join(d, "STO.DAT"), "wb").write(store)
        open(os.path.join(d, "ESC.TXT"), "wb").write(b"\x1b")
        for base, (data, m, size) in hsets.items():
            for k, p in enumerate(os88cz.split(data, base + ".BIN", size, m,
                                               jobs=1), 1):
                open(os.path.join(d, "%s.%03d" % (base, k)), "wb").write(p)
        for name, fmt in czs.items():
            open(os.path.join(d, name), "wb").write(
                os88lz.cz_wrap(czdata, fmt)[0])
        dmg = os88cz.split(t_lzfmt.half_text(70000, 19), "DMG.BIN", 40000,
                           os88cz.M_STORE, jobs=1)
        dmg[1] = bytearray(dmg[1])
        dmg[1][os88cz.CS_HDR + os88cz.REC_HDR + 7] ^= 0x20
        for k, p in enumerate(dmg, 1):
            open(os.path.join(d, "DMG", "DMG.%03d" % k), "wb").write(p)
        wa = os88cz.split(t_lzfmt.half_text(70000, 21), "WRG.BIN", 40000,
                          os88cz.M_STORE, jobs=1)
        wb = os88cz.split(t_lzfmt.half_text(70000, 23), "WRG.BIN", 40000,
                          os88cz.M_STORE, jobs=1)
        open(os.path.join(d, "WRG", "WRG.001"), "wb").write(wa[0])
        open(os.path.join(d, "WRG", "WRG.002"), "wb").write(wb[1])
        ms = os88cz.split(t_lzfmt.half_text(90000, 25), "MISS.BIN", 40000,
                          os88cz.M_STORE, jobs=1)
        open(os.path.join(d, "MISS", "MISS.001"), "wb").write(ms[0])
        if a.brk:                               # the damaged set, sold as good
            for k, p in enumerate(dmg, 1):
                open(os.path.join(d, "HMIX.%03d" % k), "wb").write(p)

        batch = ["OS88CZ S TXT.DAT 70000 > S1.LOG",
                 "OS88CZ S MIX.DAT 60000 > S2.LOG",
                 "OS88CZ S STO.DAT 720 /S > S3.LOG"]
        for base in hsets:
            batch.append("OS88CZ J %s.001 OUT > J%s.LOG" % (base, base))
        for name in czs:
            batch.append("OS88CZ U %s OUT\\%s.OUT > U%s.LOG"
                         % (name, name[:3], name[:3]))
        batch += ["OS88CZ J DMG\\DMG.002 OUT > RDMG.LOG",
                  "OS88CZ J WRG\\WRG.001 OUT > RWRG.LOG",
                  "OS88CZ J MISS\\MISS.001 OUT < ESC.TXT > RMISS.LOG"]
        dosbox(d, batch)

        print("czdos: OS88CZ.COM under DOSBox, %d bytes" % os.path.getsize(com))
        # --- DOS -> host ---------------------------------------------------
        for base, orig, lg in (("TXT", txt, "S1.LOG"), ("MIX", mix, "S2.LOG"),
                               ("STO", store, "S3.LOG")):
            parts = parts_of(d, base)
            try:
                nm, got = os88cz.join(parts)
            except os88cz.CZError as e:
                nm, got = str(e), None
            check(got == orig and nm == base + ".DAT",
                  "S %s: %d part(s), os88cz.py joins them: %s  [%s]"
                  % (base, len(parts), "identical" if got == orig else nm,
                     log(d, lg).strip().replace("\r\n", " | ")[-80:]))
            if got == orig:
                mth = {p[i + 4] for p in parts
                       for i in _recs(p)}
                check(margins(parts, orig) == 0,
                      "S %s: every LZ4 block needs no in-place margin "
                      "(methods %s)" % (base, sorted(mth)))
        # --- host -> DOS ---------------------------------------------------
        for base, (data, m, size) in hsets.items():
            p = os.path.join(d, "OUT", base + ".BIN")
            got = open(p, "rb").read() if os.path.exists(p) else None
            check(got == data, "J %s (%s): %s  [%s]"
                  % (base, os88cz.MNAMES[m],
                     "identical" if got == data else
                     "MISSING" if got is None else "WRONG",
                     log(d, "J%s.LOG" % base).strip()[-60:]))
        for name in czs:
            p = os.path.join(d, "OUT", name[:3] + ".OUT")
            got = open(p, "rb").read() if os.path.exists(p) else None
            check(got == czdata, "U %s: %s" % (
                name, "identical" if got == czdata else
                log(d, "U%s.LOG" % name[:3]).strip()))
        # --- refusals ------------------------------------------------------
        out = set(os.listdir(os.path.join(d, "OUT")))
        for lg, says, gone in (("RDMG.LOG", "Cannot expand", "DMG.BIN"),
                               ("RWRG.LOG", "Wrong part", "WRG.BIN"),
                               ("RMISS.LOG", "Missing", "MISS.BIN")):
            t = log(d, lg)
            check(says in t and gone not in out and "OS88CZ.$$$" not in out,
                  "%s says %r, and left nothing: %r"
                  % (lg, says, t.strip().replace("\r\n", " | ")[-70:]))
    for f in fails:
        print("  FAIL: " + f)
    print("czdos: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


def _recs(p):
    i = os88cz.CS_HDR
    while i < len(p):
        yield i
        i += os88cz.REC_HDR + int.from_bytes(p[i:i + 2], "little")


if __name__ == "__main__":
    sys.exit(main())
