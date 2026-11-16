#!/usr/bin/env python3
"""isoTron on `kern_small`'s 128KB floor machine REFUSES IN WORDS (SPEC.md 108.15, 24.5).

    make small smallapps && python3 tests/isotronsmall.py [--shot FILE.png]

SPEC.md 24.5's line is *a package that merely wants heap can REFUSE ITSELF in
its own words* - so it stays on the small floppies - and *one that cannot*
does not go on the disk at all (DrMarco's ground: the LOADER cannot place it,
`Load failed` before a byte of the package runs). isoTron is on
`smallapps360.img` (it is in `APPS_GAMES` and not in `SMALLOMIT_GAMES`), so
this row is the MEASUREMENT that decision rests on. Five things, each a
different way for the floor machine to fail:

  A  THE LOADER PLACES IT.  The image (~21 KB) and its bss (~3.7 KB) are one
     claim off a 52.5 KB arena; a refusal there is a window that never opens,
     and the package would have to go in $(SMALLOMIT_GAMES).
  B  IT REFUSES ITSELF BEFORE ANY CLAIM.  `it_nomem` = 1 and neither the
     canvas (`it_cseg`, 63 KB) nor the work claim (`it_seg`, 30 KB) is held -
     and, traced at the door across the launch, the package called
     `OSAPI_MEM_P` with MEMP_AVAIL and NEVER a claim verb: it asked before claiming,
     so the disk caches were not shed for nothing. (Holding no claim alone
     would be just as true of a package that claimed, was refused and shed
     them.)
  C  THE SENTENCE IS THE ARITHMETIC.  `it_msgbuf` reads `isoTron needs 63 KB
     and 30 KB of free memory; this machine has N KB.`, and N is checked
     against the kernel's own claim map, walked here: no smaller than the
     largest hole as it stands (a run any claim gets untouched) and no larger
     than every byte not held by an ordinary claim (what compaction and
     dropping every cache could make of it) - so 0, a paragraph count or
     int 12h's 128 fail.
  D  THE SENTENCE IS ON THE GLASS.  The window's content carries the
     sentence's black ink over white (one `OSAPI_FONT_RUN`), not a black
     picture: the CGA's video memory under the line's cells has ink in it.
  E  NOTHING STARTS.  No worker (`it_spawned` = 0), and `1` - a new game at
     the title - changes nothing: the key REACHES the package (traced at
     `it_onkey`, which runs before the `it_nomem` guard) and posts nothing
     (`it_qstart`, `it_qesc`, `it_qpause` all still 0). Asserting the state
     alone would pass with the guard deleted: `it_key` only POSTS a start,
     and the worker that would act on it never runs here.

BREAK IT ON PURPOSE: delete the entry proc's first `cmp ax, IT_CANVKB` /
`jb .nomem` (the ASK) and `make small smallapps` - the package then claims
63 KB, is refused, and leg B fails on the traced `OSAPI_MEM_CLAIM` while
`it_nomem`, `it_cseg` and `it_seg` read exactly as they do now (run: "2
OSAPI_MEM_AVAIL, 1 OSAPI_MEM_CLAIM", and the largest hole read 32 KB against
14 - the caches it shed). Delete `it_onkey`'s `cmp byte [it_nomem], 0` /
`jne .out` and leg E fails on `it_qstart` = 129 with `it_state` still the
title, which is the case the old state-only check passed. Or add
`$(BUILD)/isotron.o88` to `$(SMALLOMIT_GAMES)` (and delete the tree's
smallapps360.img - ddsmall.py says why) and leg A fails naming what the folder
holds.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))

# BEFORE os88geom IMPORTS (tests/ddsmall.py says why): a kern_small guest
# decoded at kern_big's stride returns plausible numbers, not an error
os.environ["OS88_DEFINES"] = "KERN_SMALL"
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88build                                             # noqa: E402
import os88sym                                               # noqa: E402

os88build.use_build("build/smallk")
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "smallk")
DEFS = ("KERN_SMALL",)
os88sym.default_defines(*DEFS)

import os88geom                                              # noqa: E402
import os88marty as M                                        # noqa: E402
import os88ui                                                # noqa: E402
import isotronlib as L                                       # noqa: E402

PKG = "B:/GAMES/ISOTRON.O88"
MACHINE = "os8088_5150_cga_128k"
CANVKB, CLAIMKB = 63, 30           # SPEC.md 108.8's two claims, in KB


# ONE cell since the flag day (kernel size pass 19): the KB doors this row
# traced were withdrawn for OSAPI_MEM_P (SPEC.md 50.3), which isoTron reaches
# in paragraphs since kernel size pass 19 (kbpara) - so a hit is a CLAIM or an
# ASK by its verb, BL (MEMP_CONV, 80h, masked off: a verb BIT that keeps a
# claim conventional, not a verb). And OSAPI_MEM_P is a DOOR cell since T5
# (SPEC.md 20.3.4): a call is `int 81h / db <index>`, no cell address to
# break on - so the trace is at the door's entry, where the stack holds the
# int frame (IP, CS, FLAGS), and a hit is MEM_P's when the byte at the
# caller's CS:IP is the SDK's index, the kernel's rdp_MEM_P. BL is the verb
# there: the door banks BX before it reads its index.
def memp_door():
    """(the door's linear entry, OSAPI_MEM_P's published index)."""
    return (os88sym.linear("os88_door", DEFS),
            os88sym.equates(DEFS)["rdp_MEM_P"])


MEMP_ASK = 5                       # MEMP_AVAIL; 0..3 are the claim verbs
MEMP_CONV = 0x80                   # a verb bit (SPEC.md 50.3.7), not the verb
MEM_MAX_SMALL = 16                 # kernel/memory.inc's MEM_MAX under KERN_SMALL
MEM_PG_MIN, MEM_PG_MAX = 0xFB, 0xFE   # a cache's owner high byte (memory.inc)


def _u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def arena(m):
    """(largest hole, every paragraph not held by an ordinary claim), in
    paragraphs, off the kernel's claim map (tests/heapcheck.py's walk)."""
    S = lambda n: os88sym.linear(n, DEFS)                       # noqa: E731
    base, top = _u16(m.read(S("mem_base"), 2)), _u16(m.read(S("mem_top"), 2))
    size = os88geom.MC_SIZE
    raw = m.read(S("mem_tab"), MEM_MAX_SMALL * size)
    cl = []
    for i in range(MEM_MAX_SMALL):
        r = raw[i * size:(i + 1) * size]
        if _u16(r, 0):
            cl.append((_u16(r, 0), _u16(r, 2), _u16(r, 4)))
    cl.sort()
    hole, fill = 0, base
    for b, p, _ in cl:
        if b > fill:
            hole = max(hole, b - fill)
        fill = max(fill, b + p)
    hole = max(hole, top - fill)
    held = sum(p for _, p, o in cl if not (MEM_PG_MIN <= (o >> 8) <= MEM_PG_MAX))
    return hole, (top - base) - held

fails = []
SHOT = sys.argv[sys.argv.index("--shot") + 1] if "--shot" in sys.argv else None


def check(ok, name, note=""):
    print("  [%s] %s %s" % ("PASS" if ok else "FAIL", name, note), flush=True)
    if not ok:
        fails.append(name)


def main():
    t = os88build.tree(targets=("small", "smallapps"))
    image, apps = t.img("small360.img"), t.img("smallapps360.img")
    print("== %s: kern_small, the 128KB floor machine ==" % MACHINE, flush=True)
    with os88ui.boot(image, apps=apps, machine=MACHINE,
                     quick=True) as ui:
        ui.still(0)     # booted on STATE; the first paint done (SOAK-SPEED 4)
        m = ui.m
        # --- A: the loader places it ---------------------------------------
        # the door traced across the launch, each hit with the CS of its
        # int frame and the index byte after the `int`: the package's own
        # MEM_P calls are the ones from its segment, whoever else asks
        door_at, memp_ix = memp_door()

        def caller(mm, rec):
            r = rec.get("regs") or {}
            f = mm.read(((r.get("ss", 0) << 4) + r.get("sp", 0)) & 0xFFFFF, 4)
            ip, cs = _u16(f, 0), _u16(f, 2)
            return cs, mm.read(((cs << 4) + ip) & 0xFFFFF, 1)[0]
        try:
            with M.bp_trace(m, door_at, regs=True, on_hit=caller,
                            cap=400) as tr:
                ui.path(PKG)
                g = L.Game(ui)
                M.quiesce(m, lambda: (g.b("it_nomem"), g.b("it_spawned"), g.w("it_cseg")),
                          guest=1.0, what="isoTron's entry and first paint")
        except Exception as e:
            check(False, "A: the loader places the package",
                  "- %s" % str(e).splitlines()[0][:160])
            return
        check(True, "A: the loader places the package", "(segment 0x%04X)" % g.seg)
        # --- B: refused before any claim -----------------------------------
        mine = [h for h in tr.hits if h.get("hit") == (g.seg, memp_ix)]
        verb = [((h.get("regs") or {}).get("bx", 0) & 0xFF & ~MEMP_CONV)
                for h in mine]
        asks = sum(1 for v in verb if v == MEMP_ASK)
        claims = sum(1 for v in verb if v < 4)     # CLAIM, _HI, | MEMP_CPU
        check(g.b("it_nomem") == 1 and g.w("it_cseg") == 0 and g.w("it_seg") == 0
              and asks >= 1 and claims == 0 and not tr.overflowed,
              "B: it ASKS, refuses itself and never claims",
              "(it_nomem %d, it_cseg %04X, it_seg %04X; from its segment %d "
              "MEMP_AVAIL, %d MEMP_CLAIM)"
              % (g.b("it_nomem"), g.w("it_cseg"), g.w("it_seg"), asks, claims))
        # --- C: the sentence ------------------------------------------------
        raw = m.read(g.a("it_msgbuf"), 96).split(b"\0")[0].decode("latin-1")
        head = "isoTron needs %d KB and %d KB of free memory; this machine has " \
            % (CANVKB, CLAIMKB)
        n = None
        if raw.startswith(head) and raw.endswith(" KB."):
            try:
                n = int(raw[len(head):-4])
            except ValueError:
                n = None
        hole, free = arena(m)
        lo, hi = hole // 64, free // 64
        check(n is not None and n < CANVKB and lo - 1 <= n <= hi + 1,
              "C: the sentence is the arithmetic",
              "(%r; the claim map: largest hole %d KB, %d KB not held by an "
              "ordinary claim)" % (raw, lo, hi))
        # --- D: on the glass -----------------------------------------------
        # the CGA's 640x200 1bpp desktop: bank 0 holds even rows, 80 bytes a
        # row. The sentence is one font_run at the content's (x + 8, y + 8),
        # black ink over white: a row through its cells holds BOTH 00 bits
        # (ink) and 1 bits (ground) where an unpainted white content or a
        # black picture would hold one of them.
        w = ui.wait_window(L.TITLE)
        cx, cy = w.content[0], w.content[1]
        vram = m.read(0xB8000, 16384)
        b0 = (cx + 8) // 8
        cells = len(raw)

        def rowbytes(y):
            base = (y & 1) * 8192 + (y >> 1) * 80
            return vram[base + b0: base + b0 + cells]
        rows = range(cy + 8, cy + 16)       # the glyphs' eight rows
        ink = sum(bin(0xFF ^ v).count("1") for y in rows for v in rowbytes(y))
        ground = sum(bin(v).count("1") for y in rows for v in rowbytes(y))
        check(ink > cells and ground > cells, "D: the sentence is on the glass",
              "(%d ink and %d ground pixels in the 8 rows of its %d cells at (%d, %d))"
              % (ink, ground, cells, cx + 8, cy + 8))
        # --- E: nothing starts ----------------------------------------------
        # the key must ARRIVE (it_onkey, before the guard) and post nothing:
        # it_key only posts it_qstart, and with no worker nothing would ever
        # act on it, so the state alone cannot tell a guard from none
        M.guest_sleep(m, 0.5)
        ONKEY = g.base + g.code["it_onkey"]
        with M.bp_trace(m, ONKEY, cap=50) as tk:
            for _ in range(3):              # (a key sent while the pump has
                m.key("Digit1", down=True, up=False)    # the guest stopped can
                M.guest_sleep(m, 0.04)                  # be lost: isotronlive's
                m.key("Digit1", down=False, up=True)    # tap and press)
                if tk.wait(1, limit=3.0):
                    break
            M.guest_sleep(m, 1.0)
        q = (g.b("it_qstart"), g.b("it_qesc"), g.b("it_qpause"))
        check(tk.n >= 1 and q == (0, 0, 0) and g.b("it_spawned") == 0
              and g.w("it_cseg") == 0 and g.w("it_seg") == 0
              and g.b("it_state") == L.ST_TITLE,
              "E: no worker, and `1` reaches it and starts nothing",
              "(it_onkey %d hits; it_qstart/qesc/qpause %d/%d/%d; it_spawned %d, "
              "it_state %d)" % ((tk.n,) + q + (g.b("it_spawned"), g.b("it_state"))))
        if SHOT:                            # the refusal, for whoever reads the log
            wd, ht, rows = m.vram("cga")
            M.write_png(SHOT, wd, ht, rows)
            print("       the glass: %s" % SHOT, flush=True)
        avail = struct.unpack("<H", bytes(m.read(0x413, 2)))[0]
        print("       int 12h %d KB; the kernel's largest free run as the package "
              "read it: %s KB (the claim map now: hole %d KB, %d KB unheld)"
              % (avail, n, lo, hi), flush=True)


if __name__ == "__main__":
    main()
    if fails:
        print("\nisotronsmall: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        sys.exit(1)
    print("\nisotronsmall: ok")
    sys.exit(0)
