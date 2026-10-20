#!/usr/bin/env python3
"""THE WATCHDOG NAMES A FREEZE (SPEC.md 88.14), on MartyPC.

    make skiesdiag && python3 tests/skiesdiag.py

An instrument that has never been tested is not an instrument. This one is
for a machine that has hard-frozen, so the only way to try it is to freeze a
machine on purpose: the row patches a `jmp $` over an instruction in the
package's own render path, at an address it chose, and then asks the glass
where the machine is. The watchdog must say that address.

  1. the hook is up - int 08h points into the package;
  2. running normally, the COUNTER block advances every tick and every banked
     sample is PLACED - inside the package when the CS banked with it is the
     package's, and in the kernel or the ROM when it is not;
  3. frozen on purpose, the picture stops - and the counter keeps going,
     which is what says IRQ0 is alive and the freeze is ours;
  4. ...and the three IP blocks all name the patched address, within the
     length of the instruction it replaced.

Check 4 is the whole row: 1 to 3 can pass on an instrument that samples the
wrong word.

WHAT THE RUNNING CHECK STILL CATCHES, broken on purpose in the guest's own
copy of `cs_diag_isr` (docs/WRITING-TESTS.md 1), on `os8088_5150_herc_gla`:

    arm                                       running check   check 4
    control                                   green           green
    banks the interrupted AX ([bp+8*2])       RED             RED
    banks the interrupted BX ([bp+7*2])       RED             RED
    `mov ax, 0x4242` - every sample the same  RED             RED
    ring store NOPped and the ring zeroed     RED             RED

The two wrong-word arms are the defect this row's own docstring says checks
1 to 3 can pass on, and they go red here only because a register sampled a
dozen times over a flight reaches zero; **check 4 is what catches them for
certain**, and it did - `1c20 1c20 1c20` against the `5fff` the machine is
stuck at. So the ordering above is still the right one to believe.

CHECK 2 USED TO SAY "the ring holds package addresses" AND THAT IS NOT TRUE
OF THIS MACHINE. The ring banks whatever `int 08h` interrupted, which while
the flight is running is regularly somewhere else: `cs_input` polls the
keyboard with the ROM's own `int 16h` (SPEC.md 53.1), so a tick landing in
`F000:E82E`'s handful of instructions banked `e832`, `e83c`, `e84b`. The
check read the IP alone, had no CS to place it by, and went red on a sample
that was correct - about four runs in five. SPEC.md 88.14.4 banks the CS per
slot and this asks the question that can be answered.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSD_SLOTS, CSD_ROWS, CSD_BLKS = 3, 4, 9
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def diagmap():
    """Assemble the CSDIAG package and take the four addresses off it.

    The listing rather than a map: nasm -f bin has no map, and these are
    absolute offsets in the package's one segment either way.

    THE SCRATCH IS THIS PROCESS'S. Two invocations at once - this row and
    `skieskfz`, which imports this function, or one of either run by hand
    beside the suite - wrote the same `skies.lst` and each read what the
    other had half-written. The symptom is NOT an error: a regex simply
    misses and the row exits `no cs_render in the listing`, which reads as
    the package having changed. Measured at 1 run in 24 three abreast."""
    scratch = os.path.join(ROOT, "build", "skiesdiag", "map%d" % os.getpid())
    lst = os.path.join(scratch, "skies.lst")
    binp = os.path.join(scratch, "skies.map.bin")
    os.makedirs(scratch, exist_ok=True)
    # -I THE PRIVATE TREE, and it is the tree's own and not build/'s: the
    # world index is GENERATED (SPEC.md 88.10.5), `make skiesdiag` writes a
    # copy of it beside the package it builds, and the two are only the same
    # file while nothing under apps/skies/ has moved. Reaching for build/'s
    # would assemble the diag package against the shipped tree's addresses,
    # which is the stale-tree failure the check below exists to catch,
    # arriving through the include path instead.
    subprocess.check_call(
        ["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/skies/",
         "-I", os.path.join(ROOT, "build", "skiesdiag") + os.sep,
         "-DCSDIAG", "-l", lst, "-o", binp, "apps/skies/skies.asm"],
        cwd=ROOT)
    # ...AND THE PRIVATE TREE HELD TO IT, os88sym's rule one level down: the
    # disk is built by `make skiesdiag` and nothing re-runs it, so a source
    # change leaves a tree whose addresses are plausible and wrong - which
    # reads as the watchdog being broken rather than as a stale build
    built = os.path.join(ROOT, "build", "skiesdiag", "skies.bin")
    if not os.path.exists(built) or open(built, "rb").read() != \
            open(binp, "rb").read():
        sys.exit("skiesdiag: build/skiesdiag/ is BEHIND apps/skies/ - every "
                 "address below would describe a package the guest has not "
                 "got. Run `make skiesdiag`.")
    out, text = {}, open(lst, errors="replace").read()
    # ...and the package's own EXTENT, which is what places a sample banked
    # with our CS. `0 < ip < 0xE000` was the old stand-in for this and it is
    # 45KB of slack: the image is 0xcdc0
    out["image"] = os.path.getsize(binp)
    for name, pat in (("cs_spguard2", r"mov si, cs_spguard2"),
                      ("cs_dtick", r"mov word \[cs_dtick\], 0"),
                      ("cs_dring", r"mov \[cs_dring \+ bx\], ax"),
                      ("cs_dcsr", r"mov \[cs_dcsr \+ bx\], cx"),
                      ("cs_dold", r"mov \[cs_dold\], ax"),
                      ("cs_doff", r"mov si, \[cs_doff \+ si\]"),
                      ("cs_diag_isr", r"mov word \[es:8\*4\], cs_diag_isr")):
        m = re.search(r"^\s*\d+ [0-9A-F]{8} ([0-9A-F]+)\[([0-9A-F]{4})\].*"
                      + pat, text, re.M)
        if not m:
            sys.exit("skiesdiag: no `%s` in the listing" % pat)
        out[name] = int(m.group(2)[2:4] + m.group(2)[0:2], 16)
    # a bare label emits nothing, so the address is the next line that does
    lines = text.splitlines()
    out["cs_render"] = None
    for i, ln in enumerate(lines):
        if ln.rstrip().endswith("cs_render:"):
            for nxt in lines[i + 1:i + 8]:
                m = re.match(r"\s*\d+ ([0-9A-F]{8}) [0-9A-F]", nxt)
                if m:
                    out["cs_render"] = int(m.group(1), 16)
                    break
            break
    if out["cs_render"] is not None:    # a scratch only this run can see -
        shutil.rmtree(scratch, True)    # kept when a regex missed, so the
    return out                          # listing is there to be looked at


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/skiesdiag/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    if not os.path.exists(a.apps):
        # **NOT 0.** The suite gates this row on the `skiesdiag` capability
        # (tools/os88test.py), so it is never REACHED without the tree - and
        # this branch returning 0 is how the row spent its life being scored
        # `ok` in 0.1s against 20s declared before that gate existed. A run
        # that could not answer says so with its exit status; the line is
        # here to be friendlier than a traceback to somebody running it by
        # hand, not to turn an absence into a result.
        print("  SKIP: %s - run `make skiesdiag` first" % a.apps)
        return 2
    sym = diagmap()
    print("    cs_diag_isr %04x  cs_dtick %04x  cs_doff %04x"
          % (sym["cs_diag_isr"], sym["cs_dtick"], sym["cs_doff"]))

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        m.advance(frames=40)
        m.run()
        m.type_text("f")
        m.advance(frames=150)
        m.run()

        v = m.read(8 * 4, 4)
        vseg = int.from_bytes(v[2:4], "little")
        voff = int.from_bytes(v[0:2], "little")
        check(vseg == seg and voff == sym["cs_diag_isr"],
              "int 08h points into the package (%04x:%04x)" % (vseg, voff))

        def devoff(row):
            # cs_doff and NOT cs_devoff since SPEC.md 88.14.3: the strip is
            # painted ABOVE the view where the backend leaves room, so a
            # frame's blit cannot overwrite the reading
            return int.from_bytes(
                m.readseg(seg, sym["cs_doff"] + 2 * row, 2), "little")

        def blocks():
            """The nine painted words, read off the GLASS and not off bss.

            PAUSED: SPEC.md 88.14.3 is the incident. A sampler that reads
            these with the guest running has the ISR repaint between two of
            its round trips and invents a mixture - which is where the "59
            mixed readings" that section retracts came from. One stop, every
            block, then go."""
            m.pause()
            out = []
            for s in range(CSD_BLKS):
                b = m.read(0xB0000 + devoff(s * CSD_ROWS), 2)
                out.append((b[0] << 8) | b[1])
            m.run()
            return out

        def ring():
            """Every slot's IP with the CS BANKED WITH IT (SPEC.md 88.14.4),
            and the counter, out of BSS.

            PAUSED, AND IN ONE CALL, for blocks()' reason one level in. Slot
            at a time is worse than a mixture: the ISR fires between two round
            trips and the IP of one tick comes back paired with the CS of
            another, which is the single thing this reading must not do.

            While the flight is RUNNING the frame overwrites the painted
            blocks between one read and the next - which is the design (88.14)
            and not a fault, so the running checks read the source and the
            frozen one reads the glass, which is the claim that matters."""
            lo = min(sym["cs_dring"], sym["cs_dcsr"], sym["cs_dtick"])
            hi = max(sym["cs_dring"] + CSD_SLOTS * 2,
                     sym["cs_dcsr"] + CSD_SLOTS * 2, sym["cs_dtick"] + 2)
            m.pause()
            blob = m.readseg(seg, lo, hi - lo)
            m.run()

            def w(at):
                return int.from_bytes(blob[at - lo:at - lo + 2], "little")
            return ([(w(sym["cs_dcsr"] + 2 * k), w(sym["cs_dring"] + 2 * k))
                     for k in range(CSD_SLOTS)], w(sym["cs_dtick"]))

        # the kernel's own int 08h CS, out of the vector cs_diag_on displaced
        # - so "the kernel" is a fact off the guest rather than a constant
        # this file mirrors. `cs_dold` is only written when the bracket goes
        # up, which the check above has just established; read before that it
        # is ZERO, and a zero here would fail every kernel sample for a reason
        # that has nothing to do with the watchdog
        kseg = int.from_bytes(m.readseg(seg, sym["cs_dold"] + 2, 2), "little")
        if not kseg or kseg == seg:
            sys.exit("skiesdiag: cs_dold holds %04x:%04x - the bracket was not"
                     " up when it was read, so nothing below can be placed"
                     % (kseg, int.from_bytes(
                         m.readseg(seg, sym["cs_dold"], 2), "little")))

        seen = []
        for _ in range(4):
            seen.append(ring())
            m.advance(frames=6)
            m.run()
        ticks = [t for _pairs, t in seen]
        print("      running: ticks %s" % ticks)
        check(all(ticks[i + 1] > ticks[i] for i in range(len(ticks) - 1)),
              "the counter advances every tick (%s)" % ticks)
        banked = set()
        for pr, _t in seen:
            banked |= set(pr)
        mine = sorted(ip for cs, ip in banked if cs == seg)
        away = sorted(set((cs, ip) for cs, ip in banked if cs != seg))
        print("      sampled outside the package: %s"
              % (" ".join("%04x:%04x" % x for x in away) or "none"))
        # A SAMPLE THAT IS OURS IS AN OFFSET INSIDE OUR IMAGE, and that
        # is what catches an instrument reading the wrong word off the frame:
        # junk is spread over a 64KB range and the package is 0xcab0 of it.
        # 7ac6ea14 reached the same diagnosis and could only say "at least
        # one of them is in 0..0xE000", on the ground that "cs_dcseg is a
        # single word and names only the LAST sample, so no per-slot filter
        # is available to do better". That was true of the instrument and it
        # cost FOUR BYTES of a CSDIAG-only ISR to stop being true (88.14.4).
        # Its own `own` filter is worth keeping in mind: `0 < v < 0xE000`
        # admits a KERNEL offset - 0060:3a41, 0060:00b8 and 0060:bebc were
        # all banked here - so a ring holding nothing but kernel samples
        # reads as "the watchdog is watching the flight" when it is not
        check(len(set(ip for _c, ip in banked)) > 2 and mine
              and all(0 < ip < sym["image"] for ip in mine),
              "every sample banked with OUR cs is an offset inside the"
              " package (%s, image %04x)"
              % (" ".join("%04x" % v for v in mine), sym["image"]))
        # ...AND A SAMPLE THAT IS NOT OURS NAMES SOMEWHERE THE FLIGHT GOES.
        # There are two and only two: a kernel slot - or fsx_wait's own hlt,
        # which is stage 10 - is KERNEL_SEG, and cs_input's `int 16h`
        # keyboard poll (SPEC.md 53.1) is the ROM. Anything else is a real
        # finding about where this package spends a tick, so it goes red
        # naming itself rather than being swallowed by a range
        strayed = [(cs, ip) for cs, ip in away
                   if cs != kseg and cs < 0xF000]
        if strayed:                 # only NOW is it worth assembling the
            import os88sym          # kernel to place a `.cold` sample
            cold = os88sym.equates().get("COLD_SEG")
            strayed = [x for x in strayed if x[0] != cold]
        check(not strayed,
              "...and every sample banked with another names the kernel"
              " (%04x) or the ROM (%s)"
              % (kseg, " ".join("%04x:%04x" % x for x in strayed)
                 or "none strayed"))

        # --- 2b: break a GUARD on purpose (SPEC.md 88.14.1) ------------------
        # The latch is the half that says whether memory went wrong BEFORE
        # the machine died, so it needs its own deliberate break: one byte
        # into cs_spguard2, which nothing in the package addresses at all.
        pre = blocks()
        check(pre[6] == 0 and pre[7] == 0,
              "no guard has gone while the flight is healthy (%04x %04x)"
              % (pre[6], pre[7]))
        m.pause()
        m.write(lin + sym["cs_spguard2"], b"\x01")
        m.run()
        m.advance(frames=12)
        m.run()
        post = blocks()
        print("      guard broken on purpose: which %d, stage %d, tick %d"
              % (post[6] >> 8, post[6] & 0xFF, post[7]))
        check((post[6] >> 8) == 3,
              "the LATCH names the guard that went (region %d, wanted 3)"
              % (post[6] >> 8))
        check(1 <= (post[6] & 0xFF) <= 11 and post[7] > 0,
              "...with the stage and the tick it went on (%d, %d)"
              % (post[6] & 0xFF, post[7]))
        again = blocks()
        m.advance(frames=12)
        m.run()
        check(blocks()[7] == again[7],
              "...and it latches ONCE, so the reading is of the moment (%d)"
              % again[7])

        # --- 3/4: freeze it on purpose ---------------------------------------
        # `jmp $` over the FIRST instruction of cs_diag_paint's caller is no
        # use - it must be code the flight runs and the ISR does not, or the
        # watchdog freezes with it. cs_render is exactly that.
        at = sym["cs_render"]
        if at is None:
            sys.exit("skiesdiag: no cs_render in the listing")
        m.pause()
        m.write(lin + at, b"\xEB\xFE")          # jmp $
        m.run()
        m.advance(frames=20)
        m.run()
        before = m.read(0xB0000, 4 * 0x2000)
        m.advance(frames=40)
        m.run()
        after = m.read(0xB0000, 4 * 0x2000)
        b1 = blocks()
        m.advance(frames=20)
        m.run()
        b2 = blocks()
        print("      frozen at %04x: ticks %d -> %d, age %d -> %d, "
              "stage %02x, blocks %s"
              % (at, b1[3], b2[3], b1[4], b2[4], b2[5] & 0xFF,
                 " ".join("%04x" % v for v in b2[:CSD_SLOTS])))
        check(b2[3] > b1[3],
              "the counter keeps going when the flight has stopped (%d -> %d)"
              % (b1[3], b2[3]))
        check(b2[4] > b1[4] and b2[4] > 10,
              "and TICKS SINCE THE LAST FRAME grows, which is the reading a"
              " single photograph can be taken of (%d -> %d)" % (b1[4], b2[4]))
        # 9 and not 1: the `jmp $` lands on cs_render's FIRST instruction,
        # which is before its own CSSTAGE, so the last stage the machine got
        # PAST is the simulation loop's - which is exactly what the block
        # means and worth having the gate state
        check((b2[5] & 0xFF) == 9,
              "and the STAGE block says the last stage it got past (%d)"
              % (b2[5] & 0xFF))
        # the picture is a `jmp $`, so every sample lands on it or one byte in
        check(all(at <= v <= at + 1 for v in b2[:CSD_SLOTS]),
              "and all %d IP blocks NAME the address it is stuck at (%04x: %s)"
              % (CSD_SLOTS, at, " ".join("%04x" % v for v in b2[:CSD_SLOTS])))
        changed = sum(1 for i in range(len(before)) if before[i] != after[i])
        print("      VRAM bytes moving while frozen: %d (the watchdog's own)"
              % changed)

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
