#!/usr/bin/env python3
"""The sound debug disk, booted (SPEC.md 127.2-127.4).

    python3 tests/snddiag.py [--keep]

Boots build/debug-sound-720.img on MartyPC's 5150 with a Sound Blaster
(os8088_5150_herc_sb_720_gla), touches nothing, and reads what SNDDIAG.O88
put on the screen - not off the pixels but out of the package's own text
buffer, sd_txt, which is exactly what its rows draw (sd_refresh copies it to
the glass row by row). The buffer is found by its header row, so the row needs
no map of the package.

What it asserts is the whole promise of the disk with nobody at the machine:
\\AUTOSTRT opened the program, the -DSNDDIAG driver answered SNDV_DIAG, the
play test started by itself, ran ten seconds and named nothing as stopped,
and pages 1 and 3 show the probe record (the DSP at 220h, a BLASTER= line,
the version command in the DSP write log). After the tone finishes, an
injected reported DMA 3 and a refused route exercise the actual displayed
text; this is not a claim that the emulator played through DMA 3. Optional
--image/--apps/--machine select an adapter, and --evidence saves its report.
"""
import argparse
import os
import re
import sys
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.chdir(ROOT)
import os88marty as M          # noqa: E402
import os88drvsym
import os88geom as G           # noqa: E402

IMG = "build/debug-sound-720.img"
APPS = "build/apps720.img"
MACHINE = "os8088_5150_herc_sb_720_gla"
HDR = re.compile(rb" os8088 Sound Debug  build \d")
ROWS, LW = 17, 79
RATE = 22050


def page(m):
    wins = [w for w in G.windows(m) if str(w.title).startswith("Sound Debug")]
    if not wins:
        raise M.MartyError("no Sound Debug window: \\AUTOSTRT did not open it")
    data = M.pkg_read(m, wins[0], 0, 0x6000)
    hit = HDR.search(data)
    if not hit:
        raise M.MartyError("Sound Debug's text buffer has no header row")
    o = hit.start()
    return [data[o + i * LW:o + i * LW + LW - 1].decode("latin-1").rstrip()
            for i in range(ROWS)]


def show(rows, what):
    print("---- %s" % what)
    for r in rows:
        print("  |" + r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--image", default=IMG)
    ap.add_argument("--apps", default=APPS)
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--evidence")
    a = ap.parse_args()
    fails = []

    def check(cond, what):
        print("  %s %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)

    for p in (a.image, a.apps):
        if not os.path.exists(p):
            print("snddiag: %s missing - make" % p)
            return 1
    with M.launch(a.image, apps=a.apps, machine=a.machine) as m:
        rows = None
        for _ in range(60):                 # ~a minute of guest time at most
            m.advance(frames=50)
            try:
                rows = page(m)
            except M.MartyError:
                continue
            if "Done." in rows[1] or "REFUSED" in rows[1]:
                break
        if rows is None:
            print("snddiag: FAIL - the program never appeared")
            return 1
        show(rows, "page 2, as the play test left it")
        check("Done." in rows[1], "the play test started by itself and "
              "finished (row 1 says Done.)")
        table = {}
        for r in rows[4:14]:
            f = r.split()
            if len(f) >= 3 and f[0].isdigit():
                table[int(f[0])] = (int(f[1]), f[2])
        check(sorted(table) == list(range(1, 11)), "ten one-second rows")
        for s in range(2, 10):
            b, irq = table.get(s, (0, "0"))
            check(abs(b - RATE) <= RATE // 10,
                  "second %d played %d bytes, within 10%% of %d" % (s, b, RATE))
            check(irq.isdigit() and int(irq) > 0,
                  "second %d took block IRQs (%s)" % (s, irq))
        check("watchdog 0" in rows[14], "no watchdog stop")
        check("steady" in rows[15], "the verdict names nothing as stopped")

        m.key("Digit1")
        m.advance(frames=25)
        p1 = page(m)
        show(p1, "page 1")
        check("debug build" in p1[2], "the -DSNDDIAG driver answered "
              "SNDV_DIAG")
        check("base 220h" in p1[4], "the DSP at 220h")
        check(p1[11].startswith("BLASTER=A220"), "a BLASTER= line")
        check("tier attached" in p1[13], "the verdict says the tier is up")

        # The real tone has finished. Change only the driver's reported DMA
        # byte to exercise the diagnostic on a card the emulator cannot model;
        # never start playback with this injection in place.
        dsym = os88drvsym.symbols("snddiag/sound.bin")
        row = int.from_bytes(m.read(m.sym("drv_owner"), 2), "little")
        seg = int.from_bytes(m.readseg(M.KERNEL_SEG, row + 2, 2), "little")
        dma_addr = (seg << 4) + dsym["sbl_dma"]
        original = m.read(dma_addr, 1)
        try:
            m.write(dma_addr, bytes([3]))
            m.key("Digit1")
            m.go()                         # advance() above left the guest paused
            M.until(m, lambda mm: " D3" in page(mm)[11],
                    "the diagnostic's selected DMA 3", limit=30, guest=5)
            selected = page(m)
            check("DMA 3 HDMA" in selected[4], "DSP row reports selected DMA 3")
            check(" D3" in selected[11], "BLASTER reports D3 (injected state, no DMA-3 audio)")
            if a.evidence:
                M.ui_done(m)
                M.guest_sleep(m, 0.06)
                path = Path(a.evidence)
                path.parent.mkdir(parents=True, exist_ok=True)
                if "vga" in a.machine:
                    M.write_png_rgb(str(path), *m.fbuf())
                else:
                    M.write_png(str(path), *m.vram())
                print("DMA-3 report injection screenshot: " + str(path))
            m.write(dma_addr, bytes([255]))
            m.key("Digit1")
            m.go()                         # advance() above left the guest paused
            M.until(m, lambda mm: "DMA none" in page(mm)[4],
                    "a refused DMA route", limit=30, guest=5)
            refused = page(m)
            check(not refused[11].startswith("BLASTER="), "refused DMA omits BLASTER")
        finally:
            m.write(dma_addr, original)

        m.key("Digit3")
        m.advance(frames=25)
        p3 = page(m)
        show(p3, "page 3")
        check("DSP writes" in p3[10], "the DSP write log is shown")
        check("E1" in (p3[11] + p3[12]).split(),
              "the version command E1h is in the log")
    if fails:
        print("snddiag: %d FAILED" % len(fails))
        return 1
    print("snddiag: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
