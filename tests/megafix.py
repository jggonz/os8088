#!/usr/bin/env python3
"""A row's built SYSTEM-DISK FIXTURE, on kern_mega (tests/megaport.txt).

    import megafix
    img = megafix.system_disk(img)      # PATH itself off OS88_MEGA

Not a row: a fixture for the rows ported to kern_mega under Bochs
(tools/os88megabochs.py, docs/BOCHS.md "Rows ported from MartyPC"). Several
rows boot a system disk the Makefile builds out of the SHIPPED one plus a
few files - `make stiktest`'s stik*360.img most of all - and that disk
carries kern_big: os88megabochs swaps only the shipped images in build/,
so the row would boot the wrong kernel under Bochs. This builds the SAME
fixture on build/mega.img, kern_mega's one system disk (1.44MB, the same
tree), by putting the same files where the Makefile's recipe puts them.
A private copy, removed at exit; with OS88_MEGA unset it is the identity.
"""
import atexit
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402

_tmp = []


def _extra(name, bld):
    """[(file, destination)] the Makefile adds to the shipped system disk
    for fixture NAME, or None when NAME is not one this file knows."""
    fix = os.path.join(bld, "stikfix")
    if name == "stikdesk360.img":
        return [(os.path.join(bld, "deskprob.o88"), "::/STIKAPPS/"),
                (os.path.join(fix, "DESKPROB.DAT"), "::/SYSTEM/APPDATA/"),
                (os.path.join(fix, "desk", "system.cfg"), "::/SYSTEM.CFG")]
    m = re.match(r"stik(\w+)360\.img$", name)
    if m:
        return [(os.path.join(bld, "stikprob.o88"), "::/STIKAPPS/"),
                (os.path.join(fix, "STIKPROB.DAT"), "::/SYSTEM/APPDATA/"),
                (os.path.join(fix, m.group(1), "system.cfg"),
                 "::/SYSTEM.CFG")]
    return None


def system_disk(path):
    """PATH, or under $OS88_MEGA the same fixture built on build/mega.img"""
    if not os.environ.get("OS88_MEGA"):
        return path
    bld = os.path.join(ROOT, "build")
    extra = _extra(os.path.basename(path), bld)
    if extra is None:
        return os88marty.system_disk(path)
    if not _tmp:
        _tmp.append(tempfile.mkdtemp(prefix="megafix-"))
        atexit.register(shutil.rmtree, _tmp[0], True)
    out = os.path.join(_tmp[0], os.path.basename(path))
    shutil.copyfile(os88marty.system_disk(path), out)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    for src, dst in extra:
        subprocess.run(["mcopy", "-o", "-i", out, src, dst], check=True,
                       env=env, stdout=subprocess.DEVNULL)
    return out


def arm_disk(path):
    """PATH, or under $OS88_MEGA a private copy of it with every package that
    has a kern_mega ARM (build/megak/apps/*.o88, $(MEGAPKGS)) replaced by the
    arm, wherever on the volume it sits. An APPS disk a row builds out of the
    SHIPPED packages (covoxlisten720.img's Tracker and Audio, mrwt720.img's
    MIDIRack) would otherwise hand kern_mega a build that refuses its speaker
    or PCM tier in protected mode (KERN-MEGA-PLAN 14.66) - and
    tests/dispapps._map already reads the ARM's bss there"""
    if not os.environ.get("OS88_MEGA"):
        return path
    arms = os.path.join(ROOT, "build", "megak", "apps")
    have = {f.upper(): os.path.join(arms, f) for f in os.listdir(arms)
            if f.endswith(".o88")}
    if not _tmp:
        _tmp.append(tempfile.mkdtemp(prefix="megafix-"))
        atexit.register(shutil.rmtree, _tmp[0], True)
    out = os.path.join(_tmp[0], "arm-" + os.path.basename(path))
    shutil.copyfile(os.path.join(ROOT, path) if not os.path.isabs(path)
                    else path, out)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    ls = subprocess.run(["mdir", "-/", "-b", "-i", out, "::/"], env=env,
                        capture_output=True, text=True, check=True).stdout
    for f in ls.split():
        name = f.rsplit("/", 1)[-1].upper()
        if name in have:
            subprocess.run(["mcopy", "-o", "-i", out, have[name], f],
                           check=True, env=env, stdout=subprocess.DEVNULL)
    return out


if __name__ == "__main__":
    print(system_disk(*sys.argv[1:]))
