#!/usr/bin/env python3
"""The Video Battle builder's zip, used where there is no os8088 tree
(`make vbtbundle`, tools/os88vbtbundle.py).

    make vbtbundle && python3 tests/vbtbundle.py

Unpacked in a temporary folder and run from THERE, with no path back to
the tree: `os88vbt.py demo-project`, `build` and `verify` must work and
give the tree's own demo bundle byte for byte, and `disk` a hard disk
that boots, from the zip's own boot/; the window's module must
import (no Tk needed to import it); `vbtana.py --help` must run; the zip
must hold VBATTLE.O88 as built, a README, and the user's guide - GUIDE.HTML
and every guide/ picture it names; and two zips from the same tree must be
the same bytes.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ZIP = os.path.join(ROOT, "build", "os8088-battle-builder.zip")
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


if not os.path.exists(ZIP):
    sys.exit("no %s: make vbtbundle" % ZIP)
with tempfile.TemporaryDirectory() as t:
    with zipfile.ZipFile(ZIP) as z:
        z.extractall(t)
        names = z.namelist()
    d = os.path.join(t, "os8088-battle-builder")
    ok(os.path.isdir(d), "one folder, os8088-battle-builder/")
    eng = open(os.path.join(d, "VBATTLE.O88"), "rb").read()
    ok(eng == open(os.path.join(ROOT, "build", "vbattle.o88"), "rb").read(),
       "VBATTLE.O88 is the one built")
    ok(os.path.exists(os.path.join(d, "README.TXT")), "a README")
    gp = os.path.join(d, "GUIDE.HTML")
    page = open(gp, encoding="utf-8").read() if os.path.exists(gp) else ""
    pics = sorted(set(re.findall(r'(?:src|href)="(guide/[^"#?]+)"', page)))
    gone = [n for n in pics if not os.path.exists(os.path.join(d, n))]
    ok(page and pics and not gone, "the guide, GUIDE.HTML, and the %d "
       "pictures it names (missing: %s)" % (len(pics), gone or "none"))
    for tool in ("os88vbtgui.py", "vbtana.py", "os88vbt.py", "vbtproj.py",
                 "os88venc.py"):
        ok(os.path.exists(os.path.join(d, tool)),
           "%s, which the builder runs, is in it" % tool)
    env = {k: v for k, v in os.environ.items() if k != "PYTHONPATH"}
    work = os.path.join(t, "work")
    os.makedirs(work)

    def run(*a):
        return subprocess.run([sys.executable, "-I"] + list(a), cwd=work,
                              env=env, capture_output=True, text=True)
    r = run(os.path.join(d, "os88vbt.py"), "demo-project", "demo.vbp")
    ok(r.returncode == 0, "demo-project runs from the zip" + r.stderr[-300:])
    r = run(os.path.join(d, "os88vbt.py"), "build", "demo.vbp", "-o",
            "BATTLE.VBT")
    ok(r.returncode == 0, "build runs from the zip" + r.stderr[-300:])
    ref = os.path.join(t, "ref.VBT")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88vbt.py"),
                    "demo", ref], check=True, stdout=subprocess.DEVNULL)
    built = os.path.join(work, "BATTLE.VBT")
    ok(os.path.exists(built) and open(built, "rb").read() ==
       open(ref, "rb").read(), "...and gives the tree's demo bundle byte "
       "for byte")
    r = run(os.path.join(d, "os88vbt.py"), "verify", "BATTLE.VBT")
    ok(r.returncode == 0 and "joined exactly" in r.stdout, "verify runs")
    # ...and a DISK of it, from the zip alone: the ST11M hard disk BOOTS,
    # its boot files out of the zip's boot/, the engine out of the zip
    r = run(os.path.join(d, "os88vbt.py"), "disk", "demo.vbp", "--bundle",
            "BATTLE.VBT", "--disk", "4", "--json")
    vhd = os.path.join(work, "BATTLE-ST11M.VHD")
    ok(r.returncode == 0 and os.path.exists(vhd), "a hard disk of it runs "
       "from the zip" + r.stderr[-300:])
    if os.path.exists(vhd):
        v = run(os.path.join(d, "os88disk.py"), "--verify-hdd", vhd)
        raw = open(vhd, "rb").read()
        have = [n for n in (b"KERNEL  SYS", b"HDD     DRV", b"SOUND   DRV",
                            b"VBATTLE O88", b"BATTLE  VBT") if n in raw]
        ok(v.returncode == 0 and len(have) == 5, "...verified, carrying what "
           "it boots from: "
           "the kernel, HDD.DRV and SOUND.DRV, beside VBATTLE.O88 and "
           "BATTLE.VBT (%s)" % b" ".join(have).decode())
    r = run("-c", "import sys; sys.path.insert(0, %r); import os88vbtgui; "
            "print(os88vbtgui.TITLE)" % d)
    ok(r.returncode == 0, "the window's module imports" + r.stderr[-300:])
    r = run(os.path.join(d, "vbtana.py"), "--help")
    ok(r.returncode == 0, "vbtana runs")
    again = os.path.join(t, "again.zip")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                 "os88vbtbundle.py"), again,
                    "--engine", os.path.join(ROOT, "build", "vbattle.o88"),
                    "--diag", os.path.join(ROOT, "build", "vbattled.o88"),
                    "--boot", os.path.join(ROOT, "build")],
                   check=True, stdout=subprocess.DEVNULL)
    ok(open(again, "rb").read() == open(ZIP, "rb").read(),
       "the zip is the same bytes made twice")
    import zipfile
    names = zipfile.ZipFile(ZIP).namelist()
    ok(any(x.endswith("/VBATTLED.O88") for x in names),
       "VBATTLED.O88, the profiling engine (SPEC.md 126.8), is in it")

if fails:
    print("%d FAILED" % len(fails))
    sys.exit(1)
print("all passed")
