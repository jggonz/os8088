#!/usr/bin/env python3
"""Build gbclock, the Game Boy timing ROM, in each of its load variants.

    python3 tools/gbclock.py                 # build/gbclock/CLOCK{0,5,...}.GB
    python3 tools/gbclock.py --busy 0 25     # just those
    python3 tools/gbclock.py --rgbds         # print where rgbds is, and stop

gbclock (tools/gbprof/gbclock.asm) counts EMULATED frames on screen and keeps
the Game Boy's CPU busy for BUSY percent of each frame with a game-shaped
loop, then HALTs. An emulator's speed on it is the counter over wall
seconds x 59.7275, so the same ROM times no$gmb, RealDMG and anything else on
the same footing (docs/plans/REALDMG-PLAN.md section 7).

rgbds (MIT) is the assembler. It is FETCHED and built at a pinned commit
into build/rgbds/, never committed - tools/setup-cc.sh's shape for SmallerC.
It needs bison 3.0 or later, libpng-dev and pkg-config; as root with apt they
are installed the way tools/martypc/build.sh installs its own, anywhere else
the missing ones are named and the build stops. A NAME IS NOT A VERSION
(tools/setup-nasm3.sh's rule): macOS answers `bison` with Apple's 2.3, which
rgbds refuses deep inside its make, so the bison is asked its version, and
Homebrew's - keg-only, never on PATH - is looked for when PATH's is too old.

`python3 tools/gbprof.py build/gbclock/CLOCK25.GB --mode puzzle` is the
check that a variant is the load it says: 25.7% busy (the extra 0.7% is the
VBlank handler drawing the counter).
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RGBDS_DIR = os.path.join(ROOT, "build", "rgbds")
RGBDS_URL = "https://github.com/gbdev/rgbds"
RGBDS_REV = "d766efe95d82f0d64dd95dece60b7681c7a14de9"   # 2026-10-17
SRC = os.path.join(ROOT, "tools", "gbprof", "gbclock.asm")
OUT = os.path.join(ROOT, "build", "gbclock")
VARIANTS = (0, 5, 10, 25, 50, 90)


BISON_MIN = 3


def bison_ok(path):
    """Is PATH a bison of BISON_MIN or later?"""
    try:
        out = subprocess.run([path, "--version"], capture_output=True,
                             text=True).stdout
    except OSError:
        return False
    words = out.split("\n", 1)[0].split()
    try:
        return int(words[-1].split(".")[0]) >= BISON_MIN
    except (IndexError, ValueError):
        return False


def find_bison():
    """The directory of a usable bison: PATH's, else Homebrew's keg-only one
    (`brew install bison`, which is not linked onto PATH). None if neither."""
    cands = [shutil.which("bison")]
    if shutil.which("brew"):
        pre = subprocess.run(["brew", "--prefix", "bison"],
                             capture_output=True, text=True).stdout.strip()
        if pre:
            cands.append(os.path.join(pre, "bin", "bison"))
    cands += ["/opt/homebrew/opt/bison/bin/bison", "/usr/local/opt/bison/bin/bison"]
    for c in cands:
        if c and os.access(c, os.X_OK) and bison_ok(c):
            return os.path.dirname(c)
    return None


def preflight():
    need = []
    if not find_bison():
        need.append("bison")
    if subprocess.call(["pkg-config", "--exists", "libpng"],
                       stderr=subprocess.DEVNULL) != 0:
        need += ["libpng-dev", "pkg-config"]
    if not need:
        return
    if os.geteuid() == 0 and shutil.which("apt-get"):
        print("==> installing " + " ".join(need))
        if subprocess.call(["apt-get", "install", "-y", "-q"] + need) != 0:
            subprocess.call(["apt-get", "update", "-q"])
            subprocess.check_call(["apt-get", "install", "-y", "-q"] + need)
        return
    if sys.platform == "darwin":
        brew = ["bison" if n == "bison" else "libpng" if n == "libpng-dev"
                else "pkgconf" for n in need]
        sys.exit("gbclock: rgbds needs " + " ".join(need) + " (bison %d or "
                 "later; macOS's own is 2.3)\n  brew install " % BISON_MIN +
                 " ".join(brew))
    sys.exit("gbclock: rgbds needs " + " ".join(need) + " (bison %d or "
             "later)\n  apt-get install -y " % BISON_MIN + " ".join(need))


def rgbds():
    tools = [os.path.join(RGBDS_DIR, t) for t in ("rgbasm", "rgblink", "rgbfix")]
    if all(os.path.exists(t) for t in tools):
        head = subprocess.check_output(["git", "-C", RGBDS_DIR, "rev-parse",
                                        "HEAD"], text=True).strip()
        if head == RGBDS_REV:
            return tools
    preflight()
    if not os.path.isdir(os.path.join(RGBDS_DIR, ".git")):
        os.makedirs(os.path.dirname(RGBDS_DIR), exist_ok=True)
        subprocess.check_call(["git", "clone", "-q", RGBDS_URL, RGBDS_DIR])
    subprocess.check_call(["git", "-C", RGBDS_DIR, "fetch", "-q", "origin"])
    subprocess.check_call(["git", "-C", RGBDS_DIR, "checkout", "-q", RGBDS_REV])
    env = dict(os.environ)          # PATH's bison first if it will do, else
    env["PATH"] = find_bison() + os.pathsep + env.get("PATH", "")   # brew's
    subprocess.check_call(["make", "-C", RGBDS_DIR, "-j4", "-s"], env=env)
    return tools


def build(busy, tools):
    asm, link, fix = tools
    os.makedirs(OUT, exist_ok=True)
    obj = os.path.join(OUT, "clock%d.o" % busy)
    rom = os.path.join(OUT, "CLOCK%d.GB" % busy)
    subprocess.check_call([asm, "-DBUSY=%d" % busy, "-o", obj, SRC])
    subprocess.check_call([link, "-o", rom, obj])
    subprocess.check_call([fix, "-v", "-p", "0xFF", "-t", "GBCLOCK%d" % busy,
                           rom])
    os.remove(obj)
    return rom


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--busy", type=int, nargs="*", default=list(VARIANTS),
                    help="the load variants, percent of a frame (0..90)")
    ap.add_argument("--rgbds", action="store_true",
                    help="build rgbds if needed, print its directory, stop")
    a = ap.parse_args()
    tools = rgbds()
    if a.rgbds:
        print(RGBDS_DIR)
        return
    for b in a.busy:
        if not 0 <= b <= 90:
            sys.exit("gbclock: BUSY %d is outside 0..90" % b)
        print(build(b, tools))


if __name__ == "__main__":
    main()
