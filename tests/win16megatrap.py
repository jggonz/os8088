#!/usr/bin/env python3
"""WIN16 on kern_mega with no completed #GP in its hot paths
(docs/plans/KERN-MEGA-PLAN.md 14.164, 14.165, SPEC.md 124.31.5, 124.31.7).

    python3 tests/win16megatrap.py              the row: every leg
    python3 tests/win16megatrap.py --leg mdi    one leg (lz, pm, sol, mdi
                                                or winfile; repeatable)
    python3 tests/win16megatrap.py --pkg F.O88 --map F.MAP
                                                census another kern_mega
                                                WIN16.O88 (the one before a
                                                change) with its own map
    python3 tests/win16megatrap.py --keep       ...and leave the E9 logs

kern_mega COMPLETES a store through a code selector (mega_gpw, ~884 clocks a
286) instead of faulting, so a WIN16 part that keeps its scratch in its own
bytes and writes it `mov [cs:x], reg` is a SLOW program that looks like a
working one. The census of 14.164 found three such paths a user meets:
the int 21h gate of the DOS core WIN16 hosts (fifteen an int 21h: 7,187 for
Write's open of a 20 KB file), part 10's MDI paint (~180 a key in Program
Manager's group, ~850 a Tile) and part 9's VER and LZ copies (a trap a
byte). This boots `make megatrap`'s kernel (a private tree; one E9 line per
completed #GP) under Bochs, the one emulator here that enforces a 286's
segment rules (docs/BOCHS.md), with C: holding WIN16 (the tree's kern_mega
arm), its DOS.O88 and a fixture with an \\AUTOSTRT link to it, in two legs:

  lz    LZT.EXE (`make win16fixtures`, tests/win16/lzt.c) and the three SZDD
        files tests/win16lz.py packs: LZRead forward and back, LZCopy,
        VerFindFile and VerInstallFile - a few hundred int 21h calls. It
        must leave SAMPLE.TXT and the system directory's SECOND.DAT,
        expanded byte for byte, and the traps charged to the DOS block's
        code selector ([w16_dhent + 2]) must be none, and to part 9's
        ([w16_sseg + 4]) at most LZ_SYS - the per-call cells left cold
        (tests/cswrites.txt's w16lzver.inc line)
  pm    Windows 3.0's Program Manager (PROGMAN.EXE, Microsoft's: in
        win16ref/samples/ only where the win16-reference skill put it, so
        the leg is a SKIP without it) with its three groups, a PROGMAN.INI
        naming them in WIN16's Windows directory: Window > Tile, Window >
        Cascade, Ctrl+Tab four times and eight arrows in the Main group -
        part 10's MDI frames, captions, icons, tile and cascade. It must
        stay up with no fault, and the traps charged to part 10's code
        selector ([w16_tseg + 4]) must be none
  sol   Windows 3.0's Solitaire (SOL.EXE, win16ref's too): its window has
        WS_CLIPCHILDREN, so its first paint walks w16_clipkids, whose
        w16_cutkid lost the walk's CX - the walk ran past the window table
        and read past WIN16's data limit, and kern_mega CLOSED Solitaire
        (and Cardfile, and MDI.EXE) at 10xx:9E26 before 14.164. It must be
        up, its instance alive, with no fault, ten guest seconds after
  mdi   MDI.EXE (`make win16fixtures`, tests/win16/mdi.c): an MDICLIENT and
        three children by WM_MDICREATE, tiled, cascaded, minimized,
        maximized and destroyed by WM_MDIDESTROY, all checked by the
        program - its MessageBox, read off its own stack while WIN16
        serves it, must say "MDI OK"
  winfile Windows 3.0's File Manager (WINFILE.EXE, win16ref's; a SKIP
        without it): its drive window, Window > Tile and > Cascade, up

EVERY leg must also take no far-transfer fixup (an `xfer` line: a #GP
whose error code is a selector) at WIN16's own code selector - the
instance's data selector - 8. Part 10's MDICLIENT made each WM_MDICREATE
and WM_MDIDESTROY a call back into WIN16's CreateWindow and DestroyWindow
stubs, and named the stubs' segment `ds`, the image's DATA half in the arm,
so w16_cbenter's retf loaded a data selector into CS (14.165): kern_mega's
mega_gpfix completed it for Program Manager (three) and CLOSED MDI.EXE and
File Manager there, because its pair check read the LDT's end through SS -
the program's stack - and took whatever the program kept at that offset.
The selectors are read off WIN16's own data (its instance's I_SPTR is its
data selector; the offsets are the tree's megak/apps/win16.map), so a trap
is charged by the kernel's own bookkeeping, not by an address that moves
with every build. Each leg must also raise no kern_mega FAULT or CLOSED.

THE FAILING CASE (docs/WRITING-TESTS.md 1): --pkg/--map with the WIN16.O88
from before 14.164 (and its map) - the lz leg charged 1,959 traps to the DOS
block (12 at the gate, 2 in the host, 1 in the handler, an int 21h) and 534
to part 9, and the pm leg 4,838 to part 10, and the sol leg was CLOSED: measured red
when this was written, against 0, 123, 0 and up after. And with the
WIN16.O88 from before 14.165: pm 3, mdi 6 and winfile 1 xfer fixups at
w16_cbenter's retf (10E4:0B2F) on 14.165's kernel, and on the kernel before
it MDI.EXE CLOSED there ("MDI never started"); none and "MDI OK" after.

It writes only under its private tree's win16megatrap/ directory.
"""
import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88bochs  # noqa: E402
import os88build  # noqa: E402

LZ_SYS = 140                    # part 9's cold residue in the lz leg: 123
                                #   measured, 49 sites of VER's and LZ's
                                #   per-call cells at 1-5 apiece (before
                                #   14.164 the per-byte copy alone was 279)
LIMIT = 300                     # guest seconds a leg's program may take
SAMPLES = os.path.join(ROOT, "win16ref", "samples", "w3-archive", "WINDOWS")
GROUPS = ("ACCESSOR.GRP", "GAMES.GRP", "MAIN.GRP")
TRAP = re.compile(r"^T ([0-9A-F]{4}) ([0-9A-F]{4}):([0-9A-F]{4}) ")
W16F_ARGS = 12                  # a waiting call's arguments, above its frame


def mt_env():
    return dict(os.environ, MTOOLS_SKIP_CHECK="1")


def win16_syms(path):
    """{name: offset} for the WIN16 data words the row reads, off PATH (a
    win16.bin's nasm map: the package's own, since the words move with it)"""
    import win16fpu
    global MSGBOX
    MSGBOX = win16fpu.row_handler(win16fpu.msgrow())   # MessageBox's handler
    want = {"w16_dhent", "w16_sseg", "w16_tseg", "w16_inprog", "w16_idx",
            "w16_bind", "w16_pss", "w16_psp_", MSGBOX}
    out = {}
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[2] in want:
                out[p[2]] = int(p[1], 16)
    missing = want - set(out)
    if missing:
        sys.exit("win16megatrap: %s not in the tree's win16.map" %
                 ", ".join(sorted(missing)))
    return out


def hd(t, out, win16, exe, extra):
    """C: - the tree's mega.img's files, WIN16 with its DOS.O88 and the
    fixture in C:\\WIN16, and the \\AUTOSTRT link that opens it"""
    tree = os.path.join(out, "tree")
    os.makedirs(tree)
    subprocess.run(["mcopy", "-s", "-n", "-i", t.img("mega.img"), "::*",
                    tree], env=mt_env(), check=True, capture_output=True)
    args = []
    for dp, _, fns in os.walk(tree):
        rel = os.path.relpath(dp, tree)
        for fn in sorted(fns):
            if rel == "." and fn.upper() in ("KERNEL.SYS", "ASSOC.DAT"):
                continue
            p = os.path.join(dp, fn)
            args.append(p if rel == "." else
                        rel.replace(os.sep, "/").upper() + ":" + p)
    lnk = os.path.join(out, "FIX.LNK")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lnk.py"),
                    lnk, "WIN16\\" + os.path.basename(exe), "--vol", "C",
                    "--kind", "document"], check=True, capture_output=True)
    w16 = os.path.join(out, "WIN16.O88")
    shutil.copy(win16, w16)
    # Program Manager's INI and groups go in WIN16's Windows directory
    # (SPEC.md 124.20.5), the rest beside the program
    home = [p for p in extra if p.upper().endswith((".INI", ".GRP"))]
    args += ["WIN16:" + w16,
             "WIN16:" + os.path.join(t.dir, "megak", "kdos", "DOS.O88"),
             "WIN16:" + exe] + ["WIN16:" + p for p in extra if p not in home] \
        + ["SYSTEM/APPDATA/WIN16:" + p for p in home] + ["AUTOSTRT:" + lnk]
    c = os.path.join(out, "c.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", c, "--hdd", "--mbr", t.img("mbr.bin"),
                    "--boot", os.path.join(t.dir, "megak", "boothd.bin"),
                    "--kernel", os.path.join(t.dir, "megak", "kernel.sys"),
                    "--geometry", "%d/%d/%d" % os88bochs.GEOM,
                    "--folder", "SYSTEM/APPDATA/WIN16/SYSTEM"] + args,
                   check=True, capture_output=True)
    return c


def mread(c, path):
    """PATH's bytes off C:'s first partition, or None"""
    with open(c, "rb") as f:
        mbr = f.read(512)
    lba = int.from_bytes(mbr[0x1BE + 8:0x1BE + 12], "little")
    r = subprocess.run(["mtype", "-i", "%s@@%d" % (c, lba * 512), "::" + path],
                       env=mt_env(), capture_output=True)
    return r.stdout if r.returncode == 0 else None


def leg(t, syms, name, win16, keep, display, res):
    out = os.path.join(t.dir, "win16megatrap", name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    fails, said = [], []

    def say(m):
        said.append("win16megatrap %s: %s" % (name, m))

    if name == "lz":
        import win16lz
        exe = os.path.join(ROOT, "build", "win16", "LZT.EXE")
        extra = win16lz.files(out)
    elif name == "sol":
        exe = os.path.join(SAMPLES, "SOL.EXE")
        extra = []
    elif name == "mdi":
        exe = os.path.join(ROOT, "build", "win16", "MDI.EXE")
        extra = []
    elif name == "winfile":
        exe = os.path.join(SAMPLES, "WINFILE.EXE")
        extra = []
    else:
        exe = os.path.join(SAMPLES, "PROGMAN.EXE")
        ini = os.path.join(out, "PROGMAN.INI")
        with open(ini, "wb") as f:
            f.write(b"[Settings]\r\nWindow=64 48 576 384 1\r\n"
                    b"SaveSettings=0\r\nMinOnRun=0\r\nAutoArrange=0\r\n\r\n"
                    b"[Groups]\r\n" + b"".join(
                        b"Group%d=C:\\SYSTEM\\APPDATA\\WIN16\\%s\r\n"
                        % (i + 1, g.encode()) for i, g in enumerate(GROUPS)))
        extra = [ini] + [os.path.join(SAMPLES, g) for g in GROUPS]
    c = hd(t, out, win16, exe, extra)
    box = os88bochs.Box(out, [], cimg=c, cgeom=os88bochs.GEOM, noa=True,
                        bootc=True, build=t.dir, display=display)
    prog = os.path.splitext(os.path.basename(exe))[0]
    sel, csel = {}, None
    try:
        box.up()
        last = [0.0]

        def started():
            if any(n.upper() == prog for _, n, _, _ in box.instances()):
                return True
            if time.time() - last[0] > 2.0:   # a CLOSE ends the wait: the
                last[0] = time.time()         #   instance never comes
                if box.faults():
                    raise RuntimeError("%s never started" % prog)
            return False
        box.until(started, prog + "'s instance", LIMIT)
        dsel = [p for _, n, p, _ in box.instances() if n.upper() == prog][0]
        csel = dsel - 8                 # its code half (W16ICS, 124.31.4)
        base = box.sel_base(dsel)
        if name == "lz":
            import win16lz
            box.until(lambda: mread(c, "/WIN16/SAMPLE.TXT") and mread(
                c, "/SYSTEM/APPDATA/WIN16/SYSTEM/SECOND.DAT"),
                "LZT's SAMPLE.TXT and SECOND.DAT", LIMIT)
            box.guest_sleep(3)
            if mread(c, "/WIN16/SAMPLE.TXT") != win16lz.sample():
                fails.append("SAMPLE.TXT is not SAMPLE.TX_ expanded")
            if mread(c, "/SYSTEM/APPDATA/WIN16/SYSTEM/SECOND.DAT") != \
                    win16lz.second():
                fails.append("SECOND.DAT is not SECOND.DA_ expanded")
            words = {"dos": syms["w16_dhent"] + 2, "sys": syms["w16_sseg"] + 4}
        elif name == "mdi":
            def u16(a):
                b = box.read(a, 2)
                return b[0] | b[1] << 8

            def at_box():               # WIN16 serving MessageBox, the
                if not any(n.upper() == prog for _, n, _, _ in
                           box.instances()):
                    raise RuntimeError("MDI's WIN16 is gone")
                if box.read(base + syms["w16_inprog"], 1)[0]:
                    return False        #   program waiting in it
                slot = u16(base + syms["w16_idx"])
                return u16(base + syms["w16_bind"] + 4 * slot + 2) == \
                    syms[MSGBOX]
            box.until(at_box, "MDI.EXE's MessageBox", LIMIT, poll=1.0)
            # MessageBox(hwnd, text, caption, type), pascal: the text's far
            # pointer is at +6 of the waiting call's arguments (W16F_ARGS)
            a = box.sel_base(u16(base + syms["w16_pss"])) + \
                u16(base + syms["w16_psp_"]) + W16F_ARGS + 6
            off, tsel = u16(a), u16(a + 2)
            got = box.read(box.sel_base(tsel) + off, 120).split(b"\0")[0]
            say("its MessageBox: %r" % got.decode("latin-1"))
            if got != b"MDI OK":
                fails.append("MDI.EXE's MessageBox says %r, not 'MDI OK'"
                             % got.decode("latin-1"))
            words = {}
        elif name == "winfile":
            box.capture()
            box.guest_sleep(10)
            box.ui_done("File Manager's drive window", secs=120)
            for keys in (("alt+w",), ("t",), ("alt+w",), ("c",)):
                box.key(*keys, delay=120)
                box.guest_sleep(1)
                box.ui_done("File Manager after %s" % keys[0], secs=120)
            if not any(n.upper() == prog for _, n, _, _ in box.instances()):
                fails.append("File Manager's WIN16 is gone")
            words = {}
        elif name == "sol":
            box.guest_sleep(10)
            box.ui_done("Solitaire's first paint", secs=120)
            if not any(n.upper() == prog for _, n, _, _ in box.instances()):
                fails.append("Solitaire's WIN16 is gone")
            words = {}
        else:
            box.capture()
            box.guest_sleep(10)
            box.ui_done("Program Manager's groups", secs=120)
            for keys in (("alt+w",), ("t",), ("alt+w",), ("c",),
                         ("ctrl+Tab",) * 4, ("Right",) * 4 + ("Left",) * 4):
                box.key(*keys, delay=120)
                box.guest_sleep(1)
                box.ui_done("Program Manager after %s" % keys[0], secs=120)
            if not any(n.upper() == prog for _, n, _, _ in box.instances()):
                fails.append("Program Manager's WIN16 is gone")
            words = {"tsk": syms["w16_tseg"] + 4}
        for k, off in words.items():
            b = box.read(base + off, 2)
            sel[k] = b[0] | b[1] << 8
        fl = box.faults()
        if fl:
            fails.append("a fault: %s" % "; ".join(fl))
        box.shot(os.path.join(out, "end.png"))
    except RuntimeError as e:
        fl = box.faults()
        fails.append("%s%s" % (e, (" - " + "; ".join(fl)) if fl else ""))
    finally:
        box.stop()
    with open(box.e9, "rb") as f:
        text = os88bochs._unsplit(f.read().decode("latin-1"))
    per, xfer = collections.Counter(), collections.Counter()
    for line in text.splitlines():
        m = TRAP.match(line)
        if m:
            per[int(m.group(2), 16)] += 1
            if int(m.group(1), 16) and int(m.group(2), 16) == csel:
                xfer["%s:%s" % (m.group(2), m.group(3))] += 1
    # a far transfer out of WIN16's own code to a DATA selector - a callback
    # frame naming one of WIN16's stubs by its data half (KERN-MEGA-PLAN
    # 14.165): kern_mega's xfer fixup completes it at best, and closes the
    # program when the fixup refuses
    if csel is not None:
        say("%d far transfers fixed up out of WIN16's code %04X%s" % (
            sum(xfer.values()), csel, (" (" + ", ".join(
                "%s x%d" % kv for kv in sorted(xfer.items())) + ")")
            if xfer else ""))
        if xfer:
            fails.append("%d #GP xfer fixups out of WIN16's code selector "
                         "%04X at %s: a far return to a DATA selector (none "
                         "allowed)" % (sum(xfer.values()), csel,
                                       ", ".join(sorted(xfer))))
    for k, s in sorted(sel.items()):
        n = per.get(s, 0)
        allow = LZ_SYS if k == "sys" else 0
        say("%-3s (selector %04X) %5d traps (%d allowed)" % (k, s, n, allow))
        if n > allow:
            fails.append("%d #GP fixups charged to WIN16's %s selector %04X "
                         "(%d allowed; tools/megatraps.py %s)"
                         % (n, k, s, allow, box.e9))
    say("%d trap lines in all" % sum(per.values()))
    if not keep and not fails:
        shutil.rmtree(out, ignore_errors=True)
    res[name] = (fails, said)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", choices=("lz", "pm", "sol", "mdi", "winfile"), action="append")
    ap.add_argument("--pkg", help="the kern_mega WIN16.O88 to census "
                    "(default: the tree's megak/apps/win16.o88)")
    ap.add_argument("--map", help="--pkg's own win16.map (nasm's, of the "
                    "same win16.bin): the words the row reads move with it")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    legs = a.leg or ["lz", "pm", "sol", "mdi", "winfile"]
    for fx in ("LZT.EXE", "MDI.EXE"):
        if (fx == "LZT.EXE" and "lz" in legs or fx == "MDI.EXE" and "mdi" in
                legs) and not os.path.exists(os.path.join(
                    ROOT, "build", "win16", fx)):
            print("win16megatrap: SKIP - %s is not built (make "
                  "win16fixtures)" % fx)
            return 77
    for lg, need in (("pm", ("PROGMAN.EXE",) + GROUPS), ("sol", ("SOL.EXE",)),
                     ("winfile", ("WINFILE.EXE",))):
        if lg in legs and not all(os.path.exists(os.path.join(SAMPLES, f))
                                  for f in need):
            print("win16megatrap: the %s leg is a SKIP - no %s in %s (the "
                  "win16-reference skill's samples)" % (lg, need[0], SAMPLES))
            legs.remove(lg)
    if not legs:
        return 77
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "mbr.bin", "megak/apps/win16.o88"))
    if a.pkg and not a.map:
        sys.exit("win16megatrap: --pkg needs --map, that package's win16.map")
    syms = win16_syms(a.map or os.path.join(t.dir, "megak", "apps",
                                            "win16.map"))
    win16 = a.pkg or os.path.join(t.dir, "megak", "apps", "win16.o88")
    t0, res, th = time.time(), {}, []
    for name in legs:
        th.append(threading.Thread(target=leg, args=(
            t, syms, name, win16, a.keep, os88bochs.take_display(), res)))
        th[-1].start()
    for x in th:
        x.join()
    fails = []
    for name in legs:
        f, said = res.get(name, (["the leg did not finish"], []))
        for m in said:
            print(m)
        fails += ["%s: %s" % (name, m) for m in f]
    print("win16megatrap: %d legs, %.0fs under Bochs" % (len(legs),
                                                        time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
