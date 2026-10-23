#!/usr/bin/env python3
"""Build a bootable floppy that runs RealDMG's core on a bare machine.

    python3 tests/rdgcore/rdgimg.py ROM.GB -o build/realdmg/t.img
        [--geom 360|720|1200|1440] [--mode test|bench|trace|play|idle|render|damage] [--frames N]
        [--patch OFF:HEX]

tests/rdgcore/rdghost.asm is the boot sector and the harness; this assembles
it and the core (apps/realdmg/rdggen.py, then nasm; `--trace` builds the
RDG_TRACE core that reports every instruction; `--patch` breaks it on purpose
for a negative control), patches the geometry and the
lengths into the boot sector, and appends harness, core and ROM. The image
is padded to the geometry's full size so any emulator takes it as a floppy.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GEOM = {360: (9, 2, 40), 720: (9, 2, 80), 1200: (15, 2, 80), 1440: (18, 2, 80)}
MODES = {"test": 0, "bench": 1, "trace": 2, "play": 3, "idle": 4, "render": 5, "damage": 6,
         "renderd": 7, "damage-il": 8, "damage-is": 9, "damage-id": 10,
         "renderv": 12, "state": 13}


def private(out_dir):
    """A directory of this process's own under out_dir, removed at exit. The
    core, its templates and the harness are built in it, because several rows
    build them at once in a soak lane, and one directory between them was a
    nasm reading the generator's half-written source (SIGBUS) and rdgtmpl
    reading another row's half-written core."""
    d = os.path.join(out_dir, "p%d" % os.getpid())
    if not os.path.isdir(d):
        os.makedirs(d)
        import atexit
        import shutil
        atexit.register(shutil.rmtree, d, True)
    return d


def build_core(trace, out_dir):
    if trace and not os.path.exists(os.path.join(private(out_dir), "rdgtmpl.bin")):
        build_core(False, out_dir)      # the harness assembles the recompiler's
                                        #   templates even when a trace core
                                        #   never translates
    out_dir = private(out_dir)
    stem = os.path.join(out_dir, "rdgcore_t" if trace else "rdgcore")
    asm, binf = stem + ".asm", stem + ".bin"
    subprocess.check_call([sys.executable, os.path.join(ROOT, "apps/realdmg/rdggen.py"), asm])
    cmd = ["nasm", "-f", "bin", "-w+error", "-I", os.path.join(ROOT, "apps/realdmg/"),
           "-o", binf, asm]
    if trace:
        cmd.insert(3, "-DRDG_TRACE")
    subprocess.check_call(cmd)
    data = open(binf, "rb").read()
    assert len(data) == 65536, len(data)
    if not trace:                       # the recompiler's templates, from
        subprocess.check_call(          #   this core (REALDMG-SPEC 16)
            [sys.executable, os.path.join(ROOT, "apps/realdmg/rdgtmpl.py"), binf,
             os.path.join(out_dir, "rdgtmpl.bin"), os.path.join(out_dir, "rdgcore.inc")],
            stdout=subprocess.DEVNULL)
    return data


def build_host(out_dir, defines=()):
    out_dir = private(out_dir)
    binf = os.path.join(out_dir, "rdghost.bin")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error"] + ["-D" + d for d in defines] +
                          (["-DRT_DEBUG"] if os.environ.get("RT_DEBUG") else []) +
                          (["-DDMG_DEBUG"] if os.environ.get("DMG_DEBUG") else []) +
                          (["-DSCRL_STAT"] if os.environ.get("SCRL_STAT") else []) + [
                           "-I", os.path.join(ROOT, "apps/realdmg/"),
                           "-I", os.path.join(ROOT, "tests/rdgcore/"),
                           "-I", out_dir + "/",
                           "-o", binf, os.path.join(ROOT, "tests/rdgcore/rdghost.asm")])
    return open(binf, "rb").read()


def patch_core(core, patches):
    """OFF:HEX pairs written over the core - a deliberately broken core is
    how a gate shows it can go red (docs/WRITING-TESTS.md 1)."""
    core = bytearray(core)
    for p in patches or ():
        off, hexb = p.split(":")
        b = bytes.fromhex(hexb)
        off = int(off, 16)
        core[off:off + len(b)] = b
    return bytes(core)


# rdghost.asm's b_jit flags: a second 64KB cache segment (REALDMG-SPEC
# 16.2.1), and both squeezed to 1KB of code so the second fills and both do
JF_SEG2 = 0x04
JF_SMALL = 0x08                     # ...and the block table at 16KB, not 64KB
JF_SQZ = 0x10
JF_NOCORE = 0x20                    # ...and no core tier: the A/B (16.2.2)


def make_image(rom_path, out, geom=1440, mode="test", frames=600, core=None, host=None,
               jit=True, seg2=False, squeeze=False, joy=None, ilace=0, small=False,
               nocore=False):
    out_dir = os.path.join(ROOT, "build", "realdmg")
    os.makedirs(out_dir, exist_ok=True)
    if core is None:
        core = build_core(mode == "trace", out_dir)
    if host is None:
        host = build_host(out_dir)
    boot, body = bytearray(host[:512]), host[512:]
    body += b"\0" * (-len(body) % 512)
    rom = open(rom_path, "rb").read()
    rom += b"\0" * (-len(rom) % 512)
    spt, heads, cyls = GEOM[geom]
    boot[3] = spt
    boot[4] = heads
    boot[5:7] = (len(body) // 512).to_bytes(2, "little")
    boot[7:9] = (128).to_bytes(2, "little")
    boot[9:11] = (len(rom) // 512).to_bytes(2, "little")
    boot[11] = MODES[mode]
    boot[12:14] = frames.to_bytes(2, "little")
    # MODES 3+'s script (joy_script): its third Start pauses a game that has
    # begun by frame 700, which Kirby's Dream Land has - so a bench of it
    # timed a paused game. None: by the ROM's title
    if joy is None:
        joy = 3 if b"KIRBY" in rom[0x134:0x144] else 0     # (and it flies)
    boot[15] = joy
    boot[16] = ilace                        # rdg_ilace (MODE 12's; 8-10 set their own)
    boot[14] = (2 if jit == "static" else 1) if jit and mode != "trace" else 0
    if boot[14]:
        boot[14] |= ((JF_SEG2 if seg2 else 0) | (JF_SQZ if squeeze else 0) |
                     (JF_SMALL if small else 0) | (JF_NOCORE if nocore else 0))
    img = bytes(boot) + body + core + rom
    size = spt * heads * cyls * 512
    if len(img) > size:
        sys.exit("rdgimg: %d bytes do not fit a %dKB floppy" % (len(img), geom))
    img += b"\0" * (size - len(img))
    with open(out, "wb") as f:
        f.write(img)
    return len(body), len(rom)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rom")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--geom", type=int, default=1440, choices=sorted(GEOM))
    ap.add_argument("--mode", default="test", choices=sorted(MODES))
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--nojit", action="store_true", help="interpret only: no recompiler")
    ap.add_argument("--static", action="store_true",
                    help="the recompiler's Static (REALDMG-SPEC 16.8): FRAMES frames recording a "
                         "profile, then the game again from its start, preloaded and fixed, "
                         "FRAMES more (not MODE render)")
    ap.add_argument("--seg2", action="store_true",
                    help="a second 64KB cache segment (REALDMG-SPEC 16.2.1)")
    ap.add_argument("--squeeze", action="store_true",
                    help="each cache segment cut to 1KB of code, so the second fills")
    ap.add_argument("--patch", action="append", metavar="OFF:HEX",
                    help="overwrite core bytes at hex offset OFF (a negative control)")
    a = ap.parse_args()
    out_dir = os.path.join(ROOT, "build", "realdmg")
    os.makedirs(out_dir, exist_ok=True)
    core = patch_core(build_core(a.mode == "trace", out_dir), a.patch)
    nb, nr = make_image(a.rom, a.out, a.geom, a.mode, a.frames, core=core,
                        jit="static" if a.static else not a.nojit, seg2=a.seg2,
                        squeeze=a.squeeze)
    print("%s: harness %d, core 65536, ROM %d bytes, %dKB" % (a.out, nb, nr, a.geom))


if __name__ == "__main__":
    main()
