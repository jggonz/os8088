#!/usr/bin/env python3
"""THE ENCODER BUNDLE WORKS WITH NO os8088 TREE - SPEC.md 98.2.13.

    make && python3 tests/vencbundle.py

`make vencbundle` packs the encoder window for people who have never built
os8088: tools/os88vbundle.py computes what it imports and runs, and zips
that with VIDEO.O88 and a README. This row makes the bundle the same way,
unpacks it OUTSIDE the tree, and uses it from there - its own folder the
only place Python may find an os88 module.

1. IT IS WHOLE: the zip holds the window, the four tools it runs or imports
   and their own imports, VIDEO.O88 and README.TXT, all in os8088-encoder/;
   and made twice it is the same bytes.
2. IT ENCODES: os88venc.py, from the unpacked folder, turns a second of
   ffmpeg's test pattern into a .V88 that os88vid verifies.
3. IT MAKES A FLOPPY: the window's disk_argv makes a 360 KB floppy holding
   the video under its 8.3 name and the VIDEO.O88 from beside the tool.
4. IT MAKES A HARD DISK: with no tree to boot from, the ST11M disk is
   formatted and not bootable (--noboot), passes os88disk --verify-hdd, and
   holds the video and VIDEO.O88.
5. NOTHING CAME FROM THE TREE: every os88 module the steps loaded was the
   bundle's.

Broken on purpose - os88pkg.py taken out of the unpacked folder - 3 and 4
FAIL: os88disk imports it, and both disks are made by processes the window
starts (which is why 5 counts only the window's own process).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                              # noqa: E402

NEED = ("os88vencgui.py", "os88venc.py", "os88vid.py", "os88disk.py",
        "os88hdd.py", "VIDEO.O88", "README.TXT")

# run INSIDE the unpacked folder: make a floppy and a hard disk the way the
# window does, and say where every os88 module came from
PROBE = r'''
import os, sys, json, subprocess, tempfile
import os88vencgui as G
v88 = sys.argv[1]
out = {}
for i in (0, 4):
    with tempfile.TemporaryDirectory() as stage:
        cmd, img = G.disk_argv(v88, i, stage=stage)
        p = subprocess.run(cmd, capture_output=True, text=True)
    out[str(i)] = [img, "--noboot" in cmd, p.returncode, p.stderr.strip()]
import os88venc, os88vid, os88lz, os88cgacomp
out["mods"] = sorted(os.path.dirname(os.path.abspath(m.__file__))
                     for n, m in sys.modules.items()
                     if n.startswith("os88") and getattr(m, "__file__", None))
print(json.dumps(out))
'''


def names83(img, st11, spt, heads):
    """the root's names on a hard disk (tests/vencguitest.py's reader)"""
    d = open(img, "rb").read()
    o = spt * heads * 512 if st11 else 0
    lba = struct.unpack_from("<I", d, o + 446 + 8)[0]
    b = o + lba * 512
    rsvd, nfats = struct.unpack_from("<H", d, b + 14)[0], d[b + 16]
    ents, fatsz = (struct.unpack_from("<H", d, b + 17)[0],
                   struct.unpack_from("<H", d, b + 22)[0])
    r = b + (rsvd + nfats * fatsz) * 512
    out = []
    for i in range(ents):
        e = d[r + 32 * i:r + 32 * i + 32]
        if e[0] in (0, 0xE5):
            continue
        n, x = e[:8].decode().strip(), e[8:11].decode().strip()
        out.append(n + ("." + x if x else ""))
    return out


def main():
    os.chdir(ROOT)
    bad = []
    if not shutil.which("ffmpeg"):
        print("   SKIP: no ffmpeg")
        return 0
    with tempfile.TemporaryDirectory() as tmp:     # OUTSIDE the tree
        player = os88build.at("build/video.o88")
        z1, z2 = os.path.join(tmp, "a.zip"), os.path.join(tmp, "b.zip")
        for z in (z1, z2):
            subprocess.run([sys.executable, "tools/os88vbundle.py", z,
                            "--player", player], check=True,
                           capture_output=True)
        names = zipfile.ZipFile(z1).namelist()
        miss = [n for n in NEED if "os8088-encoder/" + n not in names]
        same = open(z1, "rb").read() == open(z2, "rb").read()
        print("   1: %d files, all under os8088-encoder/: %s; made twice: %s"
              % (len(names), all(n.startswith("os8088-encoder/")
                                 for n in names),
                 "the same bytes" if same else "DIFFERENT"))
        if miss or not same:
            bad.append("1: missing %s, deterministic %s" % (miss, same))
        zipfile.ZipFile(z1).extractall(os.path.join(tmp, "user"))
        home = os.path.join(tmp, "user", "os8088-encoder")
        env = {k: v for k, v in os.environ.items()
               if not k.startswith("PYTHON")}
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        src = os.path.join(tmp, "user", "My Holiday Film.mp4")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi",
                        "-i", "testsrc2=duration=1:size=320x200:rate=15",
                        src], check=True)
        v88 = os.path.join(tmp, "user", "My Holiday Film.V88")
        p = subprocess.run([sys.executable, "os88venc.py", src, v88,
                            "--preset", "herc", "--quiet"], cwd=home,
                           env=env, capture_output=True, text=True)
        ok = p.returncode == 0 and subprocess.run(
            [sys.executable, "tools/os88vid.py", "verify", v88],
            capture_output=True).returncode == 0
        print("   2: encoded from the bundle: %s" % (
            "a .V88 that verifies" if ok else p.stderr.strip()[-200:]))
        if not ok:
            bad.append("2: the bundle did not encode")
            return report(bad)
        p = subprocess.run([sys.executable, "-c", PROBE, v88], cwd=home,
                           env=env, capture_output=True, text=True)
        if p.returncode:
            bad.append("3/4: the window's disk code failed: %s"
                       % p.stderr.strip()[-300:])
            return report(bad)
        import json
        out = json.loads(p.stdout.strip().splitlines()[-1])
        img, noboot, rc, err = out["0"]
        ls = subprocess.run([sys.executable, "tools/os88fat.py", "ls", img],
                            capture_output=True, text=True).stdout.upper()
        fl = [n for n in ("MYHOLIDA.V88", "VIDEO.O88") if n not in ls]
        print("   3: the floppy: %s" % ("made, with MYHOLIDA.V88 and "
                                         "VIDEO.O88" if not (rc or fl)
                                         else err or "without %s" % fl))
        if rc or fl:
            bad.append("3: the floppy: %s" % (err or "without %s" % fl))
        img, noboot, rc, err = out["4"]
        v = subprocess.run([sys.executable, "tools/os88disk.py",
                            "--verify-hdd", img], capture_output=True,
                           text=True)
        hd = names83(img, True, 17, 4) if not rc else []
        hm = [n for n in ("MYHOLIDA.V88", "VIDEO.O88") if n not in hd]
        print("   4: the ST11M disk: %s, %s, root %s" % (
            "not bootable" if noboot else "BOOTABLE",
            "verified" if not (rc or v.returncode) else "FAILED", hd))
        if rc or v.returncode or hm or not noboot or "KERNEL.SYS" in hd:
            bad.append("4: the hard disk: %s" % (err or v.stderr.strip() or
                                                 "root %s" % hd))
        mods = out["mods"]
        foreign = [m for m in mods if os.path.realpath(m) !=
                   os.path.realpath(home)]
        print("   5: %d os88 modules loaded, %d from outside the bundle"
              % (len(mods), len(foreign)))
        if foreign or len(mods) < 5:
            bad.append("5: modules from %s" % sorted(set(foreign)))
    return report(bad)


def report(bad):
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
