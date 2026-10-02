#!/usr/bin/env python3
"""EXCITEBIKE wave 7: the four floppy geometries, and a machine with too little memory
(SPEC.md 102.8).

    make excitebikedisk && python3 tests/excitebike_geom.py [--host] [--boot 360|720|1440] [--lowmem]

--host      No emulator.  Every one of build/excitebike{,720,120,360}.img is read by
            tests/unit/t_image.py's Vol - a FAT12 walker that is deliberately not
            os88disk's - and must carry EXACTLY the eight files of the package in one
            root, every chain whole and the right length for its size, the same bytes
            in all four geometries, and be the geometry its name says.  Negative
            control: a copy with one FAT entry damaged must fail the same walk.
--boot G    Boot-and-launch with that geometry's own system floppy and game floppy: 360 on
            the VGA XT, 720 on the Hercules XT with 720KB drives (the only machine here
            with them), 1440 on the VGA XT with 1.44MB drives (os8088_xt_vga_144).  The
            1.2MB floppy needs a 5.25" HD drive, which no MartyPC machine has: it is
            walked by --host and booted by hand on 86Box's 286-525 (SPEC.md 102.8.2).  Open the disk, open the package, the splash
            art loads, Enter runs the race loop, the frame counter moves, Esc leaves
            and closing the window returns the heap to what the desktop had.
--lowmem    A 256KB XT (os8088_5150_cga_gla_256k: the floor machine).  ONE Excitebike
            plays there; a SECOND window opens (the code is shared, ~10KB of splash
            and bss) and leaves too little for the game's own claims, so Enter on it
            must be REFUSED with `NOT ENOUGH MEMORY` on the glass - not the
            `8BB?.GFX MISSING` an unrelated refusal used to print - with `xb_error` =
            XB_ERR_MEM and the heap exactly what it was before Enter (every claim the
            attempt made is freed).  This row also stands guard over a bug it found:
            the small (17KB) sprite claim, taken when the big one is refused, was
            reported as a failed load because `cmp`'s borrow was read as an error.

Nothing external is read: no ROM, no disassembly (SPEC.md 102).
"""
import argparse
import os
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))

IMAGES = (("1440", "build/excitebike.img", 1440 * 1024, 18),
          ("720", "build/excitebike720.img", 720 * 1024, 9),
          ("1200", "build/excitebike120.img", 1200 * 1024, 15),
          ("360", "build/excitebike360.img", 360 * 1024, 9))
FILES = ("8BITBIKE.O88", "README.MD", "8BBV.GFX", "8BBC.GFX", "8BBH.GFX",
         "8BITBIKE.VGA", "8BITBIKE.CGA", "8BITBIKE.HRC")


def at(p):
    return os.path.join(ROOT, p)


# ---------------------------------------------------------------------------
# host: the four images, walked by an independent reader
# ---------------------------------------------------------------------------
def volume_files(v):
    """{NAME.EXT: (bytes, chain)} for the root of a t_image.Vol."""
    out = {}
    for path, name11, attr, clus, size in v.walk():
        nm = name11.decode("ascii").strip()
        base, ext = name11[:8].decode().strip(), name11[8:].decode().strip()
        nm = base + ("." + ext if ext else "")
        chain, end = v.chain(clus)
        assert end >= 0xFF8, (nm, "chain does not end in EOC", hex(end))
        assert not path, ("a file outside the root", path, nm)
        need = -(-size // (v.spc * v.byts))
        assert len(chain) == need, (nm, "chain of %d clusters for %d bytes" % (len(chain), size))
        data = b"".join(v.blob[v.cluster_lba(c) * v.byts:(v.cluster_lba(c) + v.spc) * v.byts]
                        for c in chain)[:size]
        out[nm] = data
    return out


def host():
    import t_image
    seen = {}
    for tag, img, size, spt in IMAGES:
        r = subprocess.run([sys.executable, at("tools/os88disk.py"), "--verify", at(img)],
                           capture_output=True, text=True)
        assert r.returncode == 0, (img, r.stdout, r.stderr)
        blob = t_image.read(at(img))
        assert len(blob) == size, (img, len(blob), size)
        v = t_image.Vol(blob, img)
        assert v.spt == spt, (img, "sectors per track", v.spt)
        files = volume_files(v)
        names = tuple(sorted(n for n in files if n != "ASSOC.DAT"))
        assert names == tuple(sorted(FILES)), (img, names)
        assert all(files[n] for n in FILES), (img, "an empty file")
        seen[tag] = {n: files[n] for n in FILES}
        print("geometry %-4s %s: %d clusters of %d bytes, %d files, verify OK, chains whole" %
              (tag, img, v.clusters, v.spc * v.byts, len(names)), flush=True)
    ref = seen["1440"]
    for tag, files in seen.items():
        for n in FILES:
            assert files[n] == ref[n], ("the same file differs between geometries", tag, n)
    print("all four geometries carry the same %d files, byte for byte" % len(FILES), flush=True)
    # the executable is the built package, whole (packing changes nothing here: it is the file)
    built = open(at("build/8bitbike.o88"), "rb").read()
    assert ref["8BITBIKE.O88"] == built, "8BITBIKE.O88 on the floppy is not build/8bitbike.o88"
    # negative control: one broken FAT entry must fail the walk
    blob = bytearray(t_image.read(at("build/excitebike360.img")))
    v = t_image.Vol(bytes(blob), "damaged")
    first = None
    for path, name11, attr, clus, size in v.walk():
        if name11.startswith(b"8BITBIKE"):
            first = clus
    chain, _ = v.chain(first)
    n = chain[0]
    off = v.fat0 + n + (n >> 1)
    word = struct.unpack_from("<H", blob, off)[0]
    word = (word & 0x000F) | (0xFF7 << 4) if n & 1 else (word & 0xF000) | 0xFF7    # a bad-cluster mark
    struct.pack_into("<H", blob, off, word)
    try:
        volume_files(t_image.Vol(bytes(blob), "damaged"))
    except AssertionError:
        print("negative control: a FAT entry damaged in 8BITBIKE.O88's chain fails the walk", flush=True)
    else:
        raise AssertionError("the damaged image passed the independent walk")


# ---------------------------------------------------------------------------
# emulator: boot-and-launch on a geometry
# ---------------------------------------------------------------------------
def emu_imports():
    global M, os88ui, G, F
    import os88marty as M          # noqa: F401
    import os88ui                  # noqa: F401
    import os88geom as G           # noqa: F401
    import excitebike_front as F   # noqa: F401


def boot_geometry(g):
    emu_imports()
    sym = F.symbols()
    system, disk, machine = {
        "360": ("build/os8088-360.img", "build/excitebike360.img", "os8088_xt_vga"),
        "720": ("build/os8088-720.img", "build/excitebike720.img", "os8088_5150_herc_sb_720_gla"),
        "1440": ("build/os8088.img", "build/excitebike.img", "os8088_xt_vga_144")}[g]
    system, disk = at(system), at(disk)
    with os88ui.boot(system, apps=disk, machine=machine) as ui:
        m = ui.m
        ui.open_drive("B")
        ui.settle()
        before = F.paras(F.claims(m))
        ui.open("8BITBIKE.O88")
        ui.settle()
        p = F.Probe(ui, sym)
        F.revealed(ui, p)
        assert p.w("artseg"), "the splash art did not load from the %s floppy" % g
        assert p.b("frontplay") and not p.b("error")
        c_pre = F.claims(m)
        p.put("noflow", 1)                       # the raw race loop: this row is about the DISK
        m.pause()
        m.bp_exec(p.addr("race"))
        F.key(ui, "Enter", False)
        m.run()
        assert m.wait_stop(120) == "breakpoint", "the race loop was never entered from the %s floppy" % g
        m.bp_exec()
        m.run()
        M.until(m, lambda _: p.w("xb_frames") > 4, "the race loop renders frames", guest=30)
        f0 = p.w("xb_frames")
        M.pace(m, 1)
        assert p.w("xb_frames") > f0, "the frame counter stopped"
        assert p.b("fs") and not p.b("error")
        F.key(ui, "Escape", False)
        M.until(m, lambda _: p.b("fs") == 0, "Esc leaves the race", guest=30)
        ui.settle()
        assert not p.b("error") and F.claims(m) == c_pre, "the claim map after Esc differs"
        ui.close(ui.window("8BitBike"))
        ui.settle()
        assert F.paras(F.claims(m)) == before, "closing the window left the heap changed"
        print("boot %s: disk opens, splash art loads, the race runs (%d frames), Esc and close restore the heap: PASS"
              % (g, f0), flush=True)


# ---------------------------------------------------------------------------
# emulator: too little memory
# ---------------------------------------------------------------------------
def solid(claims):
    """The claims that are the program's, not the kernel's CACHES: a purgeable record
    (owner high byte 0xFB..0xFE, kernel/memory.inc MEM_PG_*) is a window's raise cache and
    the like, which the desktop makes and sheds as it pleases; and a movable claim may
    have moved (its base is not its identity), so a claim is (size, owner, the rest)."""
    return sorted((c[1],) + tuple(c[2:]) for c in claims if not 0xFB <= (c[2] >> 8) <= 0xFE)


def lowmem():
    emu_imports()
    sym = F.symbols()
    with os88ui.boot(at("build/os8088-360.img"), apps=at("build/excitebike360.img"),
                     machine="os8088_5150_cga_gla_256k") as ui:
        m = ui.m
        # the first window: it plays (the floor machine holds ONE)
        ui.open_drive("B")
        ui.settle()
        ui.open("8BITBIKE.O88")
        ui.settle()
        first = F.Probe(ui, sym)
        F.revealed(ui, first)
        ui.open_drive("B")
        ui.settle()
        ui.open("8BITBIKE.O88")                  # the second window: the code is shared, the bss and art are not
        ui.settle()

        class Front(F.Probe):
            def __init__(self, ui, sym):
                self.ui, self.m, self.sym = ui, ui.m, sym
                w = ui.front()
                raw = self.m.read(self.m.sym("wm_wins"), G.MAX_WIN * G.WIN_SIZE)
                self.base = struct.unpack_from("<H", raw, w.i * G.WIN_SIZE + G.W_SEG)[0] << 4
        p = Front(ui, sym)
        F.revealed(ui, p)
        c_pre = F.claims(m)
        assert p.b("frontplay") and not p.b("error")
        F.key(ui, "Enter", False)
        M.until(m, lambda _: p.b("error") != 0, "the refusal", guest=90)
        M.until(m, lambda _: p.b("fs") == 0, "the bracket ends", guest=30)
        ui.settle()
        err, nomem = p.b("error"), p.b("nomem")
        assert err == 3 and nomem == 1, ("the refusal is not XB_ERR_MEM", err, nomem)
        assert p.w("gfxseg") == 0 and p.w("sprseg") == 0 and p.w("xc_dseg") == 0, "a claim survived the refusal"
        # the sentence, in the package's own image, and on the glass
        s = m.read(p.base + sym["xb_errmem"], 18)
        assert s == b"NOT ENOUGH MEMORY\x00", s
        s3 = F.shot(ui, "cga", "lowmem")
        fx, fy = p.w("frontx"), p.w("fronty")
        y0, x0 = fy + 96, fx + 156
        lit = sum(1 for y in range(y0, y0 + 8) for x in range(x0, x0 + 144) if s3[2][y][x] != 0)
        assert lit > 60, ("the refusal sentence is not on the glass", lit)
        assert solid(F.claims(m)) == solid(c_pre), (
            "the heap after the refusal is not the heap before it", solid(c_pre), solid(F.claims(m)))
        # a refused visit leaves a working window: help still opens
        F.key(ui, "KeyH")
        F.revealed(ui, p)
        assert p.b("help"), "the window is dead after the refusal"
        print("lowmem: the second window on a 256KB XT is refused with NOT ENOUGH MEMORY (xb_error 3), "
              "every claim freed, the window still answers: PASS", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", action="store_true")
    ap.add_argument("--boot", choices=("360", "720", "1440"), action="append")
    ap.add_argument("--lowmem", action="store_true")
    a = ap.parse_args()
    everything = not (a.host or a.boot or a.lowmem)
    if a.host or everything:
        host()
    for g in (a.boot or (("360", "720", "1440") if everything else ())):
        boot_geometry(g)
    if a.lowmem or everything:
        lowmem()
    print("excitebike_geom: PASS")


if __name__ == "__main__":
    main()
