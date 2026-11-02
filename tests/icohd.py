#!/usr/bin/env python3
"""A HARD-DISK INSTALL, A SECOND FLOPPY, AND NOTHING ON THE GENERIC ICON
(SPEC.md 25.9.6).

Reported from the field: on a hard disk installed from the standard pair of
1.44MB disks, icons went missing - `.IMG` files first, and INSTALL.O88 - and
came back after a restart. The machine-wide icon store was a FIXED 60 rows,
and a row is taken by every distinct (name, size) package AND every composed
document body. A hard disk carries ~50 packages; a floppy whose packages are a
DIFFERENT BUILD of the same programs carries 50 more identities, because the
size is half the key; and a `*.IMG` is composed in the DOCUMENT pass, the last
thing a listing harvests, so it is the first to find the store full.

This is that machine, end to end:

  1  `tests/instdeep.py`'s installer puts the 720KB system + apps pair on
     MartyPC's XT-IDE disk (the one HDD machine here with 80-cylinder drives);
  2  the machine REBOOTS FROM C: with a scratch floppy in B: built here - ten
     shipped packages each padded by 16 bytes (another build of the same
     programs: same names, different sizes), XTMP3 (the one package on the
     1.44MB pair the 720KB set lacks), and a `BACKUP.IMG`;
  3  every folder of C: is browsed, then B:, then C: again, and every LIVE
     entry (FS_N, not the claim's width) is asked whether it references
     ICO_R_NONE, SPEC.md 25's generic icon.

The tree before SPEC.md 25.9.6 fails it: the store stops at 60 rows and
MINES, SOLITAIR, TELNET and BACKUP.IMG draw the generic icon (measured, base
`origin/elendilon` at 2026-10-30). With the grow the store takes a sixth KB
and nothing is generic.

    python3 tests/icohd.py

REQUIRES A DISK: os8088_xt_hdd_720, and it ERASES it - the run directory is
this row's own.
"""
import os
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88build                                           # noqa: E402
import os88marty as M                                      # noqa: E402
import os88sym                                             # noqa: E402
import os88ui                                              # noqa: E402
import instdeep as ID                                      # noqa: E402

MACHINE = "os8088_xt_hdd_720"
SYS, APPS = "build/os8088-720.img", "build/apps720.img"
PADDED = ("calc", "notepad", "paint", "texpad", "browser", "telnet", "ftpd",
          "fractal", "mines", "solitair")
WALK = ("C:/", "C:/APPS", "C:/GAMES", "C:/MEDIA", "C:/SYSTEM", "C:/STIKAPPS",
        "B:/", "B:/APPS", "B:/MEDIA", "C:/MEDIA", "C:/APPS", "C:/")

_SY = os88sym.syms()
_EQ = os88sym.equates()


def V(name):
    return _SY[name] if name in _SY else _EQ[name]


def fail(msg):
    print("icohd: FAIL: %s" % msg)
    sys.exit(1)


def scratch_disk(run_dir):
    """B:: the padded packages, XTMP3 and a .IMG, through the shipping tool."""
    src = os.path.join(run_dir, "icohd-src")
    os.makedirs(src, exist_ok=True)
    args = []
    for p in PADDED + ("xtmp3",):
        data = open(os88build.at("build/%s.o88" % p), "rb").read()
        if p != "xtmp3":
            data += bytes(16)           # same name, another SIZE: another key
        out = os.path.join(src, p.upper() + ".O88")
        open(out, "wb").write(data)
        args.append("APPS:" + out)
    img = os.path.join(src, "BACKUP.IMG")
    open(img, "wb").write(bytes(range(256)) * 16)
    args.append("MEDIA:" + img)
    disk = os.path.join(run_dir, "icohd720.img")
    rc = os.system("python3 tools/os88disk.py --size 720 -o %s %s >/dev/null"
                   % (disk, " ".join(args)))
    if rc:
        fail("tools/os88disk.py could not build the scratch floppy")
    return disk


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in (SYS, APPS) + tuple(os88build.at("build/%s.o88" % p)
                                   for p in PADDED + ("xtmp3",)):
        if not os.path.exists(img):
            fail("%s is missing - `make` builds it" % img)

    run_dir = M.stage_run_dir("icohd")
    print("icohd: installing %s + %s onto the XT-IDE disk" % (SYS, APPS))
    with M.launch(SYS, apps=APPS, machine=MACHINE, boot=False,
                  run_dir=run_dir) as m:
        m.run()
        M.kernel_up(m)
        M.no_saver(m)
        ID.install(m)
    bdisk = scratch_disk(run_dir)
    blank = os.path.join(run_dir, "blank720.img")
    open(blank, "wb").write(bytes(737280))   # no boot signature: GLaBIOS
                                             # offers its menu, C is the disk

    with M.launch(blank, apps=bdisk, machine=MACHINE, boot=0,
                  run_dir=run_dir) as m:
        m.advance(frames=300)
        m.key("KeyC")
        m.advance(frames=60)
        m.run()
        M.kernel_up(m)
        M.no_saver(m)
        ui = os88ui.UI(m, verbose=False)
        k = V("KERNEL_SEG")
        lin = getattr(m, "lin", lambda s, o=0: (s << 4) + o)

        def rd(a, n=1):
            return int.from_bytes(m.read((k << 4) + a, n), "little")

        generic, seen = [], 0
        for p in WALK:
            ui.path(p)
            M.settle(m)
            vp = rd(V("fm_vp"), 2)
            vs = rd(vp + V("FS_VSEG"), 2)
            if not vs:
                fail("%s: the acting Disk window holds no listing" % p)
            n, live = rd(V("dsk_nmax"), 2), rd(vp + V("FS_N"), 2)
            st = V("DSK_DE_STRIDE")
            raw = m.read(lin(vs), n * st)
            ix = m.read(lin(vs, n * st), n)
            names = [raw[i * st:i * st + 16].split(b"\0")[0].decode("latin-1")
                     for i in range(min(live, n))]
            gen = [nm for nm, r in zip(names, ix) if r == 0xFF and nm != ".."]
            seen += len(names)
            generic += [p.rstrip("/") + "/" + g for g in gen]
            print("icohd: %-11s %2d entries  store rows %3d  generic: %s"
                  % (p, len(names), rd(V("ico_n")), " ".join(gen) or "-"))
        print("icohd: store %d rows, %dKB holding %d"
              % (rd(V("ico_n")), rd(V("ico_kb")), rd(V("ico_max"))))
        if seen < 60:
            fail("only %d entries listed - the install or the scratch disk "
                 "is not what this row was written against" % seen)
        if generic:
            fail("%d entr%s on the generic icon: %s. A FULL store answers "
                 "ICO_R_NONE (SPEC.md 25.9.6), and the document pass is "
                 "harvested last - so a *.IMG goes first"
                 % (len(generic), "y" if len(generic) == 1 else "ies",
                    ", ".join(generic)))
        if rd(V("ico_kb")) <= V("ICO_KB"):
            fail("nothing is generic but the store never GREW (%dKB) - the "
                 "scenario no longer overflows a first claim, so this row "
                 "has stopped testing the grow" % rd(V("ico_kb")))
    print("icohd: ok")


if __name__ == "__main__":
    main()
