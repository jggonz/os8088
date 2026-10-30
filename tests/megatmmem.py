#!/usr/bin/env python3
"""megatmmem - the Task Manager's memory page on kern_mega (SPEC.md 28.4.15).

    make mega && python3 tests/megatmmem.py [--keep] [--shots DIR]

Boots kern_mega under Bochs at 4MB with SOUND.DRV, HDD.DRV and NET.DRV
wanted, opens A:'s and B:'s Disk windows (whose caches go above 1MB), opens
the Task Manager from the system menu, turns to the memory page and opens
every group with +ALL. Then, off the package's own state (its bss, read
through the map of the arm `make mega` put on the system disk, the guest
quiet across two reads) and off the glass (the page's text, read cell by
cell against the kernel's own glyph table):

  1. GROUPING: every driver's block above 1MB (owner FDh, C0h | row) is
     classified with TMG_HMAF - System>1MB's - and [tm_hifix], the span's
     figure, is [tm_hisys], everything the system holds above 1MB; on the
     glass every DrvImg row sits between `System>1MB` and `Programs`;
  2. THE SUMS: IVT+BIOS + System + System>1MB + every program, as the page
     computes its collapsed rows, is RAM's used half exactly, with at least
     one kernel CACHE above 1MB present (so the sum is not vacuous) and in
     none of them - OSAPI_XMEM_CAPS counts it free since KERN-MEGA-PLAN
     14.122, so the used half is `sized - caps` with nothing taken off, and
     a cache counted twice reads the used half SHORT; and System>1MB's
     heading is the sum of the rows it opens into;
  3. THE ADDRESS: every DrvImg row's ADDR is five hex digits - the block's
     paragraph, checked against the walk's own record - and no row has
     `HIGH` in the ADDR column;
  4. THE MAP: [tm_mconw] is the left part's width (TM_GW x conventional /
     the machine, never under a third), and the map's frame row on the glass
     is TWO black runs with two pixels of ground between them;
  6. THE CACHES BY NAME (KERN-MEGA-PLAN 14.92): the arm asks OSAPI_XMEM_WALK
     for LONG records (BX = XMW_LONG), so every kernel cache above 1MB in
     tm_xw carries its whole tag, high byte its level; and with the page
     collapsed, turned to SYSTEM and Purgeable alone opened, every one of
     them is a row at its five-digit paragraph NAMED by that tag as a
     conventional cache is - WinSave for a raise cache (the two Disk
     windows'), FATwin, Icons - never the bare `Cache`.

Broken on purpose (docs/WRITING-TESTS.md 1): with the pre-28.4.15 source
built into the image 10 legs of 12 are red - the drivers file under System
(1), the caches leave the rows 70K short of RAM (2), ADDR reads HIGH (3)
and the map is one frame (4). Each was also put back on its own: the
[tm_pm] flag in tm_xgidof (1 and the sum red), the cache exclusion in
tm_hikb (System>1MB's heading 70K over its rows, red). And 14.122: the
arm with the old `sub [tm_hiused], bx` kept over the new mx_caps reads the
used half short by every cache (2, red)
and the paragraph in tm_xrow (3 red). Leg 6 is red on the arm before
14.92 - every tag 0000 and all four pool caches lettered `Cache`.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "megak")
import os88bochs  # noqa: E402
import os88sym  # noqa: E402
from os88fixture import need  # noqa: E402

FAILS = []
ARM = os.path.join(ROOT, "build", "megak", "apps", "taskmgr.bin")
CFG = 0x07                      # SOUND.DRV, HDD.DRV, NET.DRV (SPEC.md 51.3)


def want(cond, what):
    print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        FAILS.append(what)


def u16(b, i):
    return b[i] | b[i + 1] << 8


def armmap(tmp):
    """The arm's symbols: the same source assembled the way the Makefile's
    $(MEGAAPPDIR)/taskmgr.bin rule does, plus a map - and refused unless it
    is byte-identical to the build on the disk."""
    out, mp = os.path.join(tmp, "tm.bin"), os.path.join(tmp, "tm.map")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-DPKG_MEGA", "--before", "[map all %s]" % mp, "-o", out,
                    "apps/taskmgr/taskmgr.asm"], cwd=ROOT, check=True)
    if open(out, "rb").read() != open(ARM, "rb").read():
        sys.exit("megatmmem: build/megak/apps/taskmgr.bin is not this tree's "
                 "arm - `make mega` first")
    off, _, equ = os88sym._parse_map(mp)
    equ.update(off)                 # (a bss name is an equ OF a label)
    return equ


def menu(box, dy):
    box.goto(10, 5)
    box.xd("mousedown", 1)
    left = dy * 2
    while left > 30:
        box.xd("mousemove_relative", "--", 0, 30)
        time.sleep(0.04)
        left -= 30
    box.xd("mousemove_relative", "--", 0, left)
    time.sleep(0.5)
    box.xd("mouseup", 1)
    box.ui_done("the system menu", secs=120)


class TM(object):
    """The Task Manager's bss, read in ONE stop of the guest."""

    LABELS = ("tm_view", "tm_exp", "tm_cx", "tm_cy", "tm_xoff", "tm_usedk",
              "tm_totkb", "tm_kb", "tm_gkb", "tm_ist", "tm_iknd", "tm_lowkb",
              "tm_pm", "tm_hion", "tm_hiused", "tm_hitot", "tm_hisys",
              "tm_xwn", "tm_xw", "tm_hifix", "tm_hibase",
              "tm_mconw", "tm_xgid", "tm_detail")

    def __init__(self, box, eq):
        self.box, self.eq = box, eq
        # A label the build does not have reads 0, so the row runs - and
        # goes red where it should - against a build from before 28.4.15
        self.gone = [l for l in self.LABELS if l not in eq]
        if self.gone:
            print("megatmmem: this build has no %s" % " ".join(self.gone))
        offs = [eq[l] for l in self.LABELS if l in eq]
        self.lo = min(offs)
        self.n = max(offs) + 0x200 - self.lo

    def read(self):
        base = self.box.pkg_base("TaskMgr")
        if base is None:
            raise RuntimeError("no TaskMgr instance")
        self.raw = self.box.read(base + self.lo, self.n)
        return self.raw

    def b(self, label, i=0):
        if label not in self.eq:
            return 0
        return self.raw[self.eq[label] - self.lo + i]

    def w(self, label, i=0):
        if label not in self.eq:
            return 0
        return u16(self.raw, self.eq[label] - self.lo + i)


def quiet(tm, tries=12):
    """Two reads a second apart whose MEMORY fields agree: the page is not
    mid-paint (the CPU figures move every sample and are not asked)."""
    def key():
        tm.read()
        e = tm.eq
        return (tm.raw[e["tm_pm"] - tm.lo:e["tm_xw"] - tm.lo +
                       64 * e.get("TM_XWR", 6)] +
                tm.raw[e["tm_hifix"] - tm.lo:e["tm_hifix"] - tm.lo + 8] +
                tm.raw[e["tm_gkb"] - tm.lo:e["tm_gkb"] - tm.lo + 64] +
                tm.raw[e["tm_xgid"] - tm.lo:e["tm_xgid"] - tm.lo + 64] +
                bytes([tm.b("tm_view")]) +
                tm.raw[e["tm_usedk"] - tm.lo:e["tm_usedk"] - tm.lo + 2])
    last = key()
    for _ in range(tries):
        time.sleep(1.0)
        now = key()
        if now == last:
            return now
        last = now
    raise RuntimeError("the Task Manager's memory state never held still")


def glyphs(box):
    """{8 row bytes: char} off the kernel's own table ([font_seg]:
    [font_base] is glyph 32), so the glass is read with the face it was
    drawn in."""
    b = box.reads([("font_seg", 4)])[0]
    seg, off = u16(b, 0), u16(b, 2)
    lin = (box.sel_base(seg) if seg & 4 else seg * 16) + off
    raw = box.read(lin, 95 * 8)
    out = {}
    for c in range(95):
        out.setdefault(bytes(raw[c * 8:c * 8 + 8]), chr(32 + c))
    return out


def ocr(png, table, x0, x1, y0, y1):
    """[(y, x, text)] for every pixel row where a run of cells reads as
    glyphs - at whichever of the eight cell phases reads the most, since
    the grab's origin need not be the guest's to the pixel."""
    from PIL import Image
    im = Image.open(png).convert("L")
    px = im.load()
    best = []
    for ph in range(-4, 4):
        lines = []
        for y in range(max(0, y0), min(y1, im.height - 8)):
            s = []
            for x in range(max(0, x0 + ph), min(x1, im.width) - 7, 8):
                k = bytes(sum(0x80 >> i for i in range(8)
                              if px[x + i, y + r] < 128) for r in range(8))
                s.append(table.get(k, "\x00"))
            t = "".join(s)
            if sum(1 for ch in t if ch not in " \x00") >= 3 and \
                    t.count("\x00") <= len(t) // 3:
                lines.append((y, x0 + ph, t.replace("\x00", "?")))
        if sum(len(t.strip()) for _, _, t in lines) > \
                sum(len(t.strip()) for _, _, t in best):
            best = lines
    return best


def runs(png, y, x0, x1):
    """The black runs along pixel row Y from X0 to X1, as (start, end)."""
    from PIL import Image
    im = Image.open(png).convert("L")
    px = im.load()
    out, start = [], None
    for x in range(x0, x1 + 1):
        ink = px[x, y] < 128
        if ink and start is None:
            start = x
        if not ink and start is not None:
            out.append((start, x - 1))
            start = None
    if start is not None:
        out.append((start, x1))
    return out


def main():
    keep = "--keep" in sys.argv
    shots = None
    if "--shots" in sys.argv:
        shots = sys.argv[sys.argv.index("--shots") + 1]
        os.makedirs(shots, exist_ok=True)
    for p in ("build/mega.img", "build/megaapps.img", "build/megarun.o88",
              "build/mbr.bin", "build/megak/apps/taskmgr.bin"):
        need(p)
    tmp = tempfile.mkdtemp(prefix="megatmmem-")
    eq = armmap(tmp)
    t0 = time.time()
    box = os88bochs.Box(tmp, [], sb=True, cfg=CFG,
                        bimg=os.path.join(ROOT, "build", "megaapps.img"),
                        btype="1_44", megs=4)
    try:
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window", lock=False)
        box.dbl(591, 50, "A:'s Disk window", lock=False)
        menu(box, 51)                       # Task Manager (megatbl's)
        tm = TM(box, eq)
        box.until(lambda: box.pkg_base("TaskMgr") is not None,
                  "the Task Manager", 60)
        quiet(tm)
        cx, cy = tm.w("tm_cx"), tm.w("tm_cy")
        box.click(cx + 112, cy + 40, "the page turn", lock=False)
        box.until(lambda: tm.read() and tm.b("tm_view") == 1,
                  "the memory page", 30)
        xoff = tm.w("tm_xoff")
        box.click(cx + eq["TM_PEN"] + 25 * 8 - 12,
                  cy + eq["TMM_HDR_Y"] - xoff + 4, "+ALL", lock=False)
        box.until(lambda: tm.read() and u16(tm.raw, eq["tm_exp"] - tm.lo),
                  "+ALL", 30)
        time.sleep(4.5)                     # two of the page's paints
        quiet(tm)
        png = os.path.join(tmp, "page.png")
        box.shot(png)
        if shots:
            shutil.copy(png, os.path.join(shots, "megatmmem.png"))

        # --- the state ---------------------------------------------------
        want(tm.b("tm_pm") == 1 and tm.b("tm_hion") == 1,
             "0. protected mode and a pool the walk names")
        hmaf, n = eq["TMG_HMAF"], tm.w("tm_xwn")
        recs = []
        for i in range(n):
            o = eq["tm_xw"] - tm.lo + i * eq.get("TM_XWR", eq["XMW_SIZE"])
            r = tm.raw[o:o + 8]
            recs.append((u16(r, 0), u16(r, 2), r[4], r[5],
                         tm.raw[eq["tm_xgid"] - tm.lo + i],
                         u16(r, 6) if eq.get("TM_XWR", 6) == 8 else 0))
        drv = [r for r in recs if r[2] == 0xFD or r[2] & 0xE0 == 0xC0]
        caches = [r for r in recs if r[2] == 0xFE and r[3] >= eq["MEM_PG_MIN"]]
        want(len(drv) >= 3 and all(r[4] & hmaf for r in drv),
             "1. %d driver block(s) above 1MB, each filed under System>1MB "
             "(xgid %s)" % (len(drv), " ".join("%02X" % r[4] for r in drv)))
        want(tm.w("tm_hifix") == tm.w("tm_hisys"),
             "1. System>1MB's figure %dK is everything the system holds "
             "above 1MB (%dK)" % (tm.w("tm_hifix"), tm.w("tm_hisys")))
        # the collapsed rows, as tm_rows_mem and tm_systot compute them
        ninst, kind_pkg = eq["INST_MAX"], eq["KIND_PKG"]
        gkb = [tm.w("tm_gkb", 2 * g) for g in range(eq["TMG_N"])]
        sysk = tm.w("tm_totkb") - tm.w("tm_kb", eq["SK_HEAP"]) - \
            tm.w("tm_lowkb")
        live = [s for s in range(ninst) if tm.b("tm_ist", s)]
        pkgs = [s for s in live if tm.b("tm_iknd", s) & kind_pkg]
        system = sysk + gkb[eq["TMG_KERN"]] + \
            sum(gkb[eq["TMG_DRV"]:eq["TMG_N"]]) + \
            sum(gkb[s] for s in live if s not in pkgs)
        progs = sum(gkb[s] for s in pkgs)
        rows = tm.w("tm_lowkb") + system + tm.w("tm_hifix") + progs
        used = tm.w("tm_usedk") + tm.w("tm_hiused")
        want(caches and rows == used,
             "2. IVT+BIOS %d + System %d + System>1MB %d + Programs %d = %dK,"
             " RAM's used %dK, with %d cache(s) above 1MB (%dK) in neither"
             % (tm.w("tm_lowkb"), system, tm.w("tm_hifix"), progs, rows, used,
                len(caches), sum(r[1] for r in caches)))
        span = sum(r[1] for r in recs if 0xF0 <= r[2] <= 0xF7) + \
            sum(r[1] for r in recs if hmaf <= r[4] < hmaf + eq["TMG_N"])
        want(span == tm.w("tm_hifix"),
             "2. System>1MB's rows - the fixed areas and every block filed "
             "under it - are %dK, its heading %dK" % (span, tm.w("tm_hifix")))
        gw = eq["TM_GW"]
        conv, hit = tm.w("tm_totkb"), tm.w("tm_hitot")
        cw = max(gw * conv // (conv + hit), gw // 3)
        want(tm.w("tm_mconw") == cw,
             "4. the map's left part is %dpx (want %d: %dK of %dK, never "
             "under a third)" % (tm.w("tm_mconw"), cw, conv, conv + hit))

        # --- the glass ---------------------------------------------------
        table = glyphs(box)
        lines = ocr(png, table, cx, cx + 232, cy, cy + 300)
        text = [t for _, _, t in lines]
        for t in text:
            print("      | " + t.rstrip())
        ix = {k: next((i for i, t in enumerate(text) if t.strip()
                       .startswith(k)), None)
              for k in ("-System>1MB", "Programs")}
        dimg = [i for i, t in enumerate(text) if "DrvImg" in t]
        want(dimg and None not in ix.values() and
             all(ix["-System>1MB"] < i < ix["Programs"] for i in dimg),
             "1. on the glass every DrvImg row (%d) is inside System>1MB "
             "(rows %s, between %s and %s)" % (len(dimg), dimg,
                                                ix["-System>1MB"],
                                                ix["Programs"]))
        paras = {(0x400 + tm.w("tm_hibase") + r[0]) * 64 for r in drv
                 if r[2] == 0xFD}
        seen = set()
        for i in dimg:
            m = re.search(r"DrvImg\s+([0-9A-F]{5})\s+\d+K", text[i])
            if m:
                seen.add(int(m.group(1), 16))
        want(seen and seen == paras,
             "3. each DrvImg's ADDR is its five-digit paragraph: %s (the walk "
             "says %s)" % (sorted("%05X" % p for p in seen),
                           sorted("%05X" % p for p in paras)))
        want(not any(re.search(r"\sHIGH\s+\d+K", t) for t in text),
             "3. no row has HIGH in its ADDR column")
        # KERN-MEGA-PLAN 14.122: AVAIL is everything a claim could get -
        # the caches included once, by OSAPI_XMEM_CAPS - so it and RAM's
        # used half are the machine to the KB. The arm's old add-back of
        # the pool caches over the new cell reads every cache twice here
        av = next((re.search(r"AVAIL (\d+)K", t) for t in text
                   if re.search(r"AVAIL (\d+)K", t)), None)
        tot = tm.w("tm_totkb") + tm.w("tm_hitot")
        want(av and int(av.group(1)) + rows == tot,
             "2. AVAIL %sK + the rows' %dK is the machine's %dK: no cache "
             "counted twice" % (av and av.group(1), rows, tot))
        ram = next(((y, x) for y, x, t in lines
                    if t.lstrip().startswith("Ram ")), None)
        if ram is None:
            want(False, "4. found the Ram caption on the glass")
        else:               # (the caption letters from the PEN, TM_PEN in)
            fy = ram[0] - eq["TMM_RAM_Y"] + eq["TMM_M1_Y1"]
            gx = ram[1] - cx    # the grab's x less the guest's
            rr = runs(png, fy, cx + gx + 6, cx + gx + eq["TM_RW"])
            want(len(rr) == 2 and rr[1][0] - rr[0][1] == 3,
                 "4. the map's frame row (y %d) is two frames with two "
                 "pixels between: %s" % (fy, rr))

        # --- 6. THE POOL'S CACHES BY NAME (KERN-MEGA-PLAN 14.92) ----------
        # The arm asks for the walk's LONG records, so every cache above 1MB
        # carries its whole tag (XMW_TAG), and its row is named by it as a
        # conventional cache's is - WinSave for a raise cache - never the
        # bare `Cache` a six-byte record could only say. Purgeable is the
        # LAST group, listed in the SYSTEM view (tm_detail, the header's
        # left end) and below the rows the glass shows with every group
        # open, so the page is collapsed (-ALL), turned to SYSTEM and
        # Purgeable alone opened
        ws0, wsn = eq["MEM_P_WSAVE"], eq["MEM_P_WSAVE_N"]
        want(caches and all(r[5] and r[5] >> 8 == r[3] for r in caches),
             "6. every cache above 1MB carries its whole tag in the walk's "
             "LONG record, the high byte its level: %s"
             % " ".join("%04X/%02X" % (r[5], r[3]) for r in caches))
        para = {(0x400 + tm.w("tm_hibase") + r[0]) * 64: r[5] for r in caches}
        box.click(cx + eq["TM_PEN"] + 25 * 8 - 12,
                  cy + eq["TMM_HDR_Y"] - xoff + 4, "-ALL", lock=False)
        box.until(lambda: tm.read() and
                  not u16(tm.raw, eq["tm_exp"] - tm.lo), "-ALL", 30)
        box.click(cx + eq["TM_PEN"] + 4 * 8,
                  cy + eq["TMM_HDR_Y"] - xoff + 4, "SYSTEM", lock=False)
        box.until(lambda: tm.read() and tm.b("tm_detail") == 1,
                  "the SYSTEM view", 30)
        time.sleep(4.5)
        quiet(tm)
        box.shot(png)
        lines = ocr(png, table, cx, cx + 232, cy, cy + 300)
        pl = next(((y, x) for y, x, t in lines
                   if t.strip().startswith("+Purgeable")), None)
        if pl is None:
            want(False, "6. a collapsed Purgeable row on the glass: %s"
                 % [t.strip() for _, _, t in lines])
        else:
            bit = 1 << eq["TMG_PURGE"]
            box.click(pl[1] + 24, pl[0] + 3, "Purgeable", lock=False)
            box.until(lambda: tm.read() and
                      (u16(tm.raw, eq["tm_exp"] - tm.lo) |
                       u16(tm.raw, eq["tm_exp"] - tm.lo + 2) << 16) & bit,
                      "Purgeable open", 30)
            time.sleep(4.5)
            quiet(tm)
            box.shot(png)
            if shots:
                shutil.copy(png, os.path.join(shots, "megatmmem-purge.png"))
            lines = ocr(png, table, cx, cx + 232, cy, cy + 300)
            seen = {}
            for _, _, t in lines:
                print("      | " + t.rstrip())
                mm = re.search(r"^\s+(\S+)\s+([0-9A-F]{5})\s+\d+K", t)
                if mm:
                    seen[int(mm.group(2), 16)] = mm.group(1)
            want(para and all(p in seen for p in para),
                 "6. every cache above 1MB is a Purgeable row at its "
                 "paragraph: %s (the walk's %s)"
                 % (sorted("%05X" % p for p in seen),
                    sorted("%05X" % p for p in para)))
            wrong = ["%05X %s (tag %04X)" % (p, seen.get(p), t)
                     for p, t in para.items() if p in seen and
                     (seen[p] == "Cache" or
                      (ws0 <= t < ws0 + wsn) != (seen[p] == "WinSave"))]
            want(para and not wrong,
                 "6. each is named by its tag - WinSave for a raise cache, "
                 "never the bare Cache: %s"
                 % (", ".join(wrong) or
                    ", ".join("%05X %s" % (p, seen.get(p)) for p in para)))
        bad = [l for l in box.lines() if "kern_mega FAULT" in l or
               "kern_mega CLOSED" in l]
        want(not bad, "5. no kern_mega fault%s" %
             "".join(" | " + l for l in bad[:3]))
    except Exception as e:      # noqa: BLE001 - the machine stopped
        want(False, "the run: %s" % e)
    finally:
        box.stop()
        if keep:
            print("megatmmem: the boot is in " + tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    print("megatmmem: %.0fs under Bochs" % (time.time() - t0))
    for f in FAILS:
        print("FAIL megatmmem: " + f)
    print("megatmmem: %s" % ("FAILED" if FAILS else "PASS"))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
