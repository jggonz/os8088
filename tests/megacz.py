#!/usr/bin/env python3
"""megacz - File > Compress and Uncompress on kern_mega, on a real 286.

    make mega && python3 tests/megacz.py [--keep]

DISKOPS.DRV's compressor walks its buffers as REAL paragraphs - a source and
an output that slide 16KB at a time past 64KB, a 48KB and a 33KB window for a
file too big to hold twice, head[] and prev[] behind them - and on kern_mega
in protected mode a paragraph is not a segment register's value. Since
KERN-MEGA-MEMORY-PLAN MC7.6 the image keeps its arithmetic and loads every
segment register through a scratch selector of its own stream (KSEG, the
MEGA_SEL_K slots), and a slide re-aims that stream's scratch (mega_slidef).
tests/lzbig.py is the same subjects on kern_big under MartyPC; this is the
protected-mode half, on 86Box's 286 (vm/286-mega, the owner's mr286), which
takes the reset path every reflection takes on a real AT.

Three boots of one scratch B: (the verbs WRITE it, and what they wrote is
read off the image between the boots):

  1. Compress BIG1.TXT (100,000 bytes, packs under 64KB: the source slides),
     BIG2.TXT (160,000, packs PAST 64KB: both segments slide) and BIG3.TXT
     (270,000, too big to hold twice beside a kern_mega heap: STREAMED,
     through the windows and the file doors' segment arm). Each verdict is
     `Compressed...` and each file is BYTE FOR BYTE what
     os88lz.lzb_compress_machine writes (lzbig's assertion).
  2. Uncompress all three: each comes back as its original, which for BIG2
     is the decoder reading a packed stream past 64KB into a conventional
     claim held through a K scratch.
  3. No boot charges an exception to anything but the kernel or the BIOS
     (os88box `faults`).
  4. In the second boot, the Control Panel's Floppy page: Reads -> Cylinder,
     and the close that saves it - which runs CTRL.DRV's cpc_fdtest, a 286's
     floppy boot never having crossed a head. Reads stays Cylinder (or comes
     back Auto with the toast saying why, on a ROM that cannot cross).
  5. A third boot clones A: onto B: (Disk > Clone Disk..., cross-drive): the
     cloner's header and staging buffer through K4 and K5, the buffer
     sliding a window of cylinders at a time. B: is then A:, every sector
     but the serial the copy is given (SPEC.md 18.8.2.1).

Broken on purpose: KSEG reduced to a plain `mov` (kernel.asm), so a real
paragraph is loaded as a selector and lands on the identity GDT's entry for
it - right only on the 128-byte grid. Red on 1 and 2 (seen when the row was
written): every packed file is wrong from its first 32KB on (BIG1.TXT 32,791
bytes against 54,082, first differing byte 32,768) and Uncompress then says
`Disk error`. Wrong bytes on the disk and a ratio said over them is the
failure this row exists to catch. cpc_fdtest put back as it was before
MC7.6 is red on 4: an exception charged, and the panel never closes.
clo_span's K5 slide put back to the paragraph add is red on 5: the clone
never finishes and B: differs from A: in 1,330 sectors.

Needs a built 86Box (python3 tools/os88box.py build) and its ROM set.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88box                                                # noqa: E402
import os88geom as G                                          # noqa: E402
import os88lz                                                 # noqa: E402
import os88sym                                                # noqa: E402
import os88ui                                                 # noqa: E402
import hma286                                                 # noqa: E402
import megacdrom as MC                                        # noqa: E402

B = os.path.join(ROOT, "build")
os.environ["OS88_BUILD"] = os.path.join(B, "megak")    # os88sym: kern_mega's
DEFS = ("KERN_MEGA",)                                   # map, every section
EQ = os88sym.equates(DEFS)


def S(name):
    return os88sym.linear(name, DEFS)
FM_ICOMP, FM_IUNCOMP = EQ["FM_ICOMP"], EQ["FM_IUNCOMP"]
TOAST_MAX = 24
QUIET = 240000                  # emulated ms for one verb: a 270KB stream is
                                # two passes of ~1,600 8088 cycles a byte -
                                # ~90 s at 4.77MHz, a fraction of that here
FAILS = []


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def half_text(n, seed):
    sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))
    import t_lzfmt
    return t_lzfmt.half_text(n, seed)


def cz(body, u):
    return b"CZ" + bytes([os88lz.LZB, 0]) + u.to_bytes(4, "little") + body


def fat(*a, **kw):
    return subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                       "os88fat.py")] + list(a),
                          check=True, **kw)


def boot(img, bimg, name, keep):
    b = os88box.launch("286-mega", name=name, hold=True, fresh=True, a=img,
                       b=bimg, temp=not keep, inplace=True)  # (the verbs
                                                # write B:, read back here)
    try:
        hma286.mr_setup(b)
        b.kernel_up(guest_ms=90000, idle=False)
        b.until(lambda: b.byte("mega_on"), "protected mode", 30000, 250)
        b.ui_done(cap_ms=10000)
    except BaseException:
        b.quit(remove=not keep)     # a boot that fails is outside every
        raise                       # caller's try: without this its 86Box
    return b                        # ran on after the row had reported


def rb(b, addr):
    return b.memr(addr, 1)[0]


def toast(b):
    raw = b.memr(S("toast_buf"), TOAST_MAX + 1)
    return raw.split(b"\0")[0].decode("latin-1")


def disk_window(b, letter="B"):
    """Double-click the drive's desktop zone; answer (slot, its state block)"""
    have = set(MC.win_recs(b))
    bx, by = G.drive_pt(MC.Guest(b), letter)
    b.to(bx, by)
    b.click(2)
    b.until(lambda: set(MC.win_recs(b)) - have, "a Disk window for %s:" % letter,
            20000, 100)
    b.ui_done(cap_ms=10000)
    slot = sorted(set(MC.win_recs(b)) - have)[0]
    blk = MC.fs_block(b, slot)
    b.until(lambda: MC.listing(b, blk), "%s:'s listing" % letter, 20000, 100)
    return slot, blk


def select(b, slot, blk, name):
    """MC.pick's walk without its Enter: the entry selected, by FS_SEL"""
    rows = MC.listing(b, blk)
    names = [r[0].upper() for r in rows]
    if name not in names:
        return "%s is not in the folder, which holds %s" % (name,
                                                            " ".join(names))
    k = names.index(name)
    sel = lambda: struct.unpack("<H", b.memr(blk + G.FS_SEL, 2))[0]
    if sel() >= len(rows):
        x, y = MC.win_recs(b)[slot][:2]
        b.to(x + 1 + os88ui.FM_ROW_X,
             y + G.TITLE_H + 1 + G.FM_ROW_Y0 + G.FM_ROW_H // 2)
        b.click()
        b.until(lambda: sel() < len(rows), "a selection", 3000, 20,
                need=False)
        b.ui_done(cap_ms=1000)
    for _ in range(len(rows) + 4):
        s = sel()
        if s == k or s >= len(rows):
            break
        b.tap("down" if s < k else "up")
        b.until(lambda: sel() != s, "the selection to move", 2000, 20,
                need=False)
    b.ui_done(cap_ms=1000)
    return None if sel() == k else "the selection stopped at %d, not %d" % (
        sel(), k)


def verb(b, ui, slot, blk, name, item):
    err = select(b, slot, blk, name)
    if err:
        return "(%s)" % err
    b.cmd("memw 0x%X 00" % S("toast_buf"))
    ui.menu_pick(1, item)                       # cell 0 is the chip, 1 File
    b.until(lambda: toast(b) and not toast(b).startswith("Compressing"),
            "a verdict on %s" % name, QUIET, 250, need=False)
    t = toast(b)
    b.ui_done(cap_ms=20000)
    return t


def faults(b):
    out = []
    for line in open(os.path.join(b.run, "stderr.log"), errors="replace"):
        m = os88box.FAULT.search(line)
        if m and int(m.group(2), 16) < 0xF000 and \
                int(m.group(2), 16) not in (0x0060, 0x0028, MC.MEGA_CODE):
            out.append(line.strip())
    return out


def cylinder(b, ui):
    """4. The Control Panel's Floppy page: Reads -> Cylinder, and the close
    that saves it - which runs cpc_fdtest (CTRL.DRV), because a 286's floppy
    boot never crosses a head (boot_cylrun 0). Its 18KB claim is placed at a
    64KB page by its PARAGRAPH, and both halves of the compare are loaded
    through K scratches. Asked of the A: system disk, two heads."""
    if rb(b, S("boot_cylrun")):
        return "this boot crossed a head, so the save asks nothing"
    have = set(MC.win_recs(b))
    ui.menu_pick(0, "Control Panel")
    b.until(lambda: set(MC.win_recs(b)) - have, "the Control Panel", 15000, 100)
    b.ui_done(cap_ms=5000)
    slot = sorted(set(MC.win_recs(b)) - have)[0]
    wx, wy = MC.win_recs(b)[slot][:2]
    page = EQ["CP_IFDD"]
    hide = rb(b, S("cp_hide"))
    row = sum(1 for r in range(page) if not hide & (1 << r))
    b.to(wx + 1 + 6 + 30, wy + G.TITLE_H + 1 + 6 + row * 14 + 7)
    b.click()
    b.until(lambda: rb(b, S("cp_sel")) == page, "the Floppy page",
            5000, 50)
    b.ui_done(cap_ms=3000)
    fdr = S("drv_cfg") + EQ["CFG_FDR"]
    b.to(wx + 1 + 96 + EQ["CPF_BX1"] + 20,
         wy + G.TITLE_H + 1 + EQ["CPF_R0Y"] + 4 * EQ["CPF_ROWH"] + 7)
    b.mouse(0, 0, 1)
    b.until(lambda: rb(b, S("menu_dropd")), "the Reads drop-down",
            5000, 20)
    mx = b.memr(S("menu_x1"), 2)
    my = b.memr(S("menu_y1"), 2)
    b.to(mx[0] + (mx[1] << 8) + 12, my[0] + (my[1] << 8) + 1 + 2 * 16 + 8,
         buttons=1)
    b.mouse(0, 0, 0)
    b.until(lambda: rb(b, fdr) == 2, "Reads = Cylinder", 5000, 50)
    b.ui_done(cap_ms=3000)
    b.cmd("memw 0x%X 00" % S("toast_buf"))
    b.to(wx + 10, wy + G.TITLE_H // 2)
    b.click()
    b.until(lambda: slot not in MC.win_recs(b), "the panel to close",
            15000, 100)
    b.until(lambda: not rb(b, S("cp_wdirty")), "SYSTEM.CFG written",
            60000, 100)
    b.ui_done(cap_ms=10000)
    got = rb(b, fdr)
    t = toast(b)
    print("   4. Reads after the save: %d, toast %r" % (got, t))
    if got == 2:
        return None
    if got == 0 and "Cylinder" in t:
        return None                  # this ROM cannot cross a head: an answer
    return "Reads came back %d with %r: the save's test went wrong" % (got, t)


def leg(tag, img, bimg, keep, names, item, expect):
    b = boot(img, bimg, "megacz%s%d" % (tag, os.getpid()), keep)
    try:
        ui = hma286.ui_for(b)
        ui.sym = S
        slot, blk = disk_window(b)
        for n in names:
            t = verb(b, ui, slot, blk, n, item)
            want(t.startswith(expect), "%s %s: %r" % (
                "Compress" if item == FM_ICOMP else "Uncompress", n, t))
        if item == FM_IUNCOMP:
            err = cylinder(b, ui)
            want(err is None, "4. Control Panel Reads -> Cylinder saved%s" % (
                "" if err is None else ": " + err))
    except (SystemExit, os88ui.UIError) as e:
        want(False, "%s: the machine stopped answering or a gesture missed: "
             "%s" % (tag, e))
    finally:
        f = faults(b)
        want(not f, "%s: exceptions charged to a loaded image: %d%s" % (
            tag, len(f), "".join("\n        " + x for x in f[:6])))
        b.quit(remove=not keep)


def clone(img, bimg, keep):
    """5. Disk > Clone Disk..., A: onto B: - cross-drive, so no swap and no
    XMS: the cloner's claim header through K4 and its staging buffer through
    K5, sliding a window of cylinders at a time (clo_span). The prompt opens
    offering A: -> B: (clo_nextvol) and one Enter runs it."""
    b = boot(img, bimg, "megaczk%d" % os.getpid(), keep)
    try:
        ui = hma286.ui_for(b)
        ui.sym = S
        disk_window(b, "A")
        ui.menu_pick("Disk", "Clone Disk")
        seg = lambda: struct.unpack("<H", b.memr(S("clo_seg"), 2))[0]
        b.until(lambda: seg(), "the clone's claim", 20000, 100)
        b.ui_done(cap_ms=5000)
        job = b.memr(seg() << 4, 64)            # [clo_seg] is the claim's
        st = (job[EQ["CL_SRC"]], job[EQ["CL_TGT"]])   # REAL paragraph
        want(st == (0, 1), "5. the clone offers A: -> B: (got %r)" % (st,))
        b.tap("return")
        b.until(lambda: not seg(), "the clone to finish", QUIET, 250)
        b.ui_done(cap_ms=20000)
        print("   5. the clone said %r" % toast(b))
    except (SystemExit, os88ui.UIError) as e:
        want(False, "5. the machine stopped answering or a gesture missed: "
             "%s" % e)
    finally:
        f = faults(b)
        want(not f, "5: exceptions charged to a loaded image: %d%s" % (
            len(f), "".join("\n        " + x for x in f[:6])))
        b.quit(remove=not keep)
    src = open(img, "rb").read()
    dst = open(bimg, "rb").read()
    un = lambda d: d[:39] + bytes(4) + d[43:]       # BS_VolID: the copy's
    bad = [i // 512 for i in range(0, len(src), 512)  # own (SPEC.md 18.8.2.1)
           if un(src)[i:i + 512] != un(dst)[i:i + 512]]
    want(len(src) == len(dst) and not bad,
         "5. B: IS A:, every sector bar the serial (%d differ, first %s)"
         % (len(bad), bad[:6]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for f in ("mega.img", "megak/kernel.map"):
        if not os.path.exists(os.path.join(B, f)):
            sys.exit("megacz: build/%s is not built - `make mega`" % f)
    subj = {"BIG1.TXT": half_text(100000, 7),
            "BIG2.TXT": half_text(160000, 11),
            "BIG3.TXT": half_text(270000, 13)}
    packed = {n: cz(os88lz.lzb_compress_machine(d), len(d))
              for n, d in subj.items()}
    if len(packed["BIG1.TXT"]) >= 0x10000 or len(packed["BIG2.TXT"]) <= 0x10000:
        sys.exit("megacz: the fixtures no longer straddle 64KB packed")
    tmp = tempfile.mkdtemp(prefix="megacz")
    img = os.path.join(tmp, "mega.img")         # (named mega*: os88box then
    shutil.copy(os.path.join(B, "mega.img"), img)   # reads build/megak/)
    bimg = os.path.join(tmp, "megab.img")
    staged = []
    for n, v in sorted(subj.items()):
        staged.append(os.path.join(tmp, n))
        open(staged[-1], "wb").write(v)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                    "-o", bimg, "--size", "1440"] + staged,
                   check=True, stdout=subprocess.DEVNULL)
    names = sorted(subj)
    print("megacz: %s" % ", ".join("%s %d -> %d" % (n, len(subj[n]),
                                                   len(packed[n]))
                                    for n in names), flush=True)
    try:
        leg("c", img, bimg, a.keep, names, FM_ICOMP, "Compressed")
        for n in names:
            got = fat("cat", bimg, n, capture_output=True).stdout
            i = next((k for k in range(min(len(got), len(packed[n])))
                      if got[k] != packed[n][k]), None)
            want(got == packed[n], "1. %s packed: %d bytes against %d%s" % (
                n, len(got), len(packed[n]),
                "" if i is None else ", first differing byte %d" % i))
        leg("u", img, bimg, a.keep, names, FM_IUNCOMP, "Uncompressed")
        for n in names:
            got = fat("cat", bimg, n, capture_output=True).stdout
            want(got == subj[n], "2. %s back: %d bytes against %d" % (
                n, len(got), len(subj[n])))
        clone(img, bimg, a.keep)
    finally:
        if not a.keep:
            shutil.rmtree(tmp, ignore_errors=True)
    for f in FAILS:
        print("FAIL megacz: " + f.splitlines()[0])
    print("megacz: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
