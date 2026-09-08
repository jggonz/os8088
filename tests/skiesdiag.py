#!/usr/bin/env python3
"""THE WATCHDOG NAMES A FREEZE (SPEC.md 88.14), on MartyPC.

    make skiesdiag && python3 tests/skiesdiag.py

An instrument that has never been tested is not an instrument. This one is
for a machine that has hard-frozen, so the only way to try it is to freeze a
machine on purpose: the row patches a `jmp $` over an instruction in the
package's own render path, at an address it chose, and then asks the glass
where the machine is. The watchdog must say that address.

  1. the hook is up - int 08h points into the package;
  2. running normally, the COUNTER block advances every tick and the IP
     blocks hold package addresses;
  3. frozen on purpose, the picture stops - and the counter keeps going,
     which is what says IRQ0 is alive and the freeze is ours;
  4. ...and the three IP blocks all name the patched address, within the
     length of the instruction it replaced.

Check 4 is the whole row: 1 to 3 can pass on an instrument that samples the
wrong word.
"""
import argparse
import os
import re
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
    absolute offsets in the package's one segment either way."""
    lst = os.path.join(ROOT, "build", "skiesdiag", "skies.lst")
    binp = os.path.join(ROOT, "build", "skiesdiag", "skies.map.bin")
    os.makedirs(os.path.dirname(lst), exist_ok=True)
    subprocess.check_call(
        ["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I", "apps/skies/",
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
    for name, pat in (("cs_spguard2", r"mov si, cs_spguard2"),
                      ("cs_dtick", r"mov word \[cs_dtick\], 0"),
                      ("cs_dring", r"mov \[cs_dring \+ bx\], ax"),
                      ("cs_devoff", r"mov si, \[cs_devoff \+ si\]"),
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
    return out


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
    print("    cs_diag_isr %04x  cs_dtick %04x  cs_devoff %04x"
          % (sym["cs_diag_isr"], sym["cs_dtick"], sym["cs_devoff"]))

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
            return int.from_bytes(
                m.readseg(seg, sym["cs_devoff"] + 2 * row, 2), "little")

        def blocks():
            """The four painted words, read off the GLASS and not off bss."""
            out = []
            for s in range(CSD_BLKS):
                b = m.read(0xB0000 + devoff(s * CSD_ROWS), 2)
                out.append((b[0] << 8) | b[1])
            return out

        def ring():
            """The ring and the counter out of BSS. While the flight is
            RUNNING the frame overwrites the painted blocks between one read
            and the next - which is the design (88.14) and not a fault, so
            the running checks read the source and the frozen one reads the
            glass, which is the claim that matters."""
            r = [int.from_bytes(m.readseg(seg, sym["cs_dring"] + 2 * k, 2),
                                "little") for k in range(CSD_SLOTS)]
            return r + [int.from_bytes(m.readseg(seg, sym["cs_dtick"], 2),
                                       "little")]

        seen = []
        for _ in range(4):
            seen.append(ring())
            m.advance(frames=6)
            m.run()
        ticks = [s[CSD_SLOTS] for s in seen]
        print("      running: ticks %s" % ticks)
        check(all(ticks[i + 1] > ticks[i] for i in range(len(ticks) - 1)),
              "the counter advances every tick (%s)" % ticks)
        ips = set()
        for s in seen:
            ips |= set(s[:CSD_SLOTS])
        check(len(ips) > 2 and all(0 < v < 0xE000 for v in ips),
              "the ring holds package addresses (%s)"
              % " ".join("%04x" % v for v in sorted(ips)))

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
