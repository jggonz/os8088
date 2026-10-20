#!/usr/bin/env python3
"""PIXELSTEIN 3D's compiled scalers, modelled on the host (SPEC.md 97.3, 97.12):
the byte image of the generated half of part 2 - the scratch, in the indices
os88pkg prints - for a backend.

    python3 tools/pxsgen.py [--backend cga4|herc|win1|c160|modex] [--sizes]
                            [--dump OUT.bin]

WHAT THE PACKAGE GENERATES on the first Textured frame and on a Mode change
(pxgen.inc's px_gen_build, into the scratch part the loader carved - 97.9's
part 2) is modelled here instruction for instruction, so that tests/pxsscale.py can read
the part back off MartyPC and diff it, and tests/unit/t_pxsscale.py can hold
the image to the size the loader claims for it. Two scaler SETS, one per
resolution, then a col2tex table per scaler per set:

  a SCALER for height h draws one texture column of 32 texels over h view
  rows centred on the horizon - top = (80 - h) >> 1 - into the 80-byte-stride
  destination at DS:DI, reading texel v at ES:[SI + v]. Texel v covers the
  rows [(v*h) >> 5, ((v+1)*h) >> 5) relative to the top (WL_SCALE.C's
  fix/step walk, read for technique), and a texel that covers no row ON THE
  VIEW emits nothing - rows off the view emit no store, so a wall never
  patches and any h over 120 clamps to the 120-row scaler (graft 3 of the
  plan's 13). Per texel run:

      mov al, [es:si + v]          26 8A 44 vv
      mov [di + r*80], al          88 85 lo hi     the EVEN rows first
      <the odd-row phase>          the pixel format's (97.3): `ror al, 1`
                                   twice on CGA4 (D0 C8 D0 C8), one `ror
                                   al, cl` with CL = 3 on Hercules and WIN1
                                   (D2 C8), nothing on C160 and Mode X
      mov [di + r*80], al          ...then the odd rows
      ret                          C3 - NEAR: the driver is in the part

  the LOW-RES set stores a WORD a row - the column is two shadow bytes -
  through `mov ah, al` (88 C4) before the even stores and again after the
  phase before the odd ones (89 85 lo hi), so both bytes carry one phase.

  Heights: 2..80 by 2 (40) then 86..116 by 6 and 120 (7): 47 scalers a set,
  every height 1..120 aliased DOWN to the nearest generated one by the
  directory (px_sctab, part 0's bss) and by the cast's px_hq, which
  quantises a column's h the same way when the rung is Textured so that
  top/bot and the drawn rows agree.

  EVERY SCALER IS PRECEDED BY ITS codeofs TABLE (wave 3, 97.3, 97.6): 33
  words at px_sctab[h] - 66, codeofs[v] = the part offset of the load of
  the first EMITTED texel at or after v, or 0 when none from v on emits.
  The post walk enters a scaler at codeofs[v0] and the driver patches a
  near ret over codeofs[v1]'s load (the one patch site); a 0 entry draws
  nothing / patches nothing.

  col2tex[w(h)] (graft 2): for the sprite walk, per scaler height and set,
  the source column each of the w(h) screen columns a 1-tile-wide object of
  that height covers takes - w = (h*71 + 128) >> 8 at Full (a tile 2.5 tiles
  off spans 22.2 of 64 columns; 71/256 = 0.2773) and (h*71 + 256) >> 9 at
  Low res, and at least 1 - as [w][col_0 .. col_(w-1)], col_j = (j*32) // w.

The 4-byte store is the mod=10 disp16 form even for row 0, so every store is
the instruction tests/pxsbench.py priced.
"""
import argparse
import sys

ROWS = 80
TEX = 32
HEIGHTS = list(range(2, ROWS + 2, 2)) + [86, 92, 98, 104, 110, 116, 120]
HMAX = HEIGHTS[-1]
NSC = len(HEIGHTS)
PHASE = {"cga4": b"\xD0\xC8\xD0\xC8", "herc": b"\xD2\xC8", "win1": b"\xD2\xC8",
         "c160": b"", "modex": b""}
LOAD = b"\x26\x8A\x44"          # es: mov al, [si + disp8]
STORE_B = b"\x88\x85"           # mov [di + disp16], al
STORE_W = b"\x89\x85"           # mov [di + disp16], ax
AHAL = b"\x88\xC4"              # mov ah, al
RET = b"\xC3"
SETS = ("full", "low")


def quantise(h):
    if h < HEIGHTS[0]:
        return HEIGHTS[0]
    if h > HMAX:
        return HMAX
    q = HEIGHTS[0]
    for s in HEIGHTS:
        if s <= h:
            q = s
    return q


def hq_table():
    """px_hq[0..120]: the quantised height of every h (the cast's table)."""
    return [quantise(h) for h in range(HMAX + 1)]


def texel_rows(h):
    """Per texel v the absolute view rows it covers, clipped to the view."""
    top = (ROWS - h) >> 1
    out = []
    for v in range(TEX):
        r0 = top + ((v * h) >> 5)
        r1 = top + (((v + 1) * h) >> 5)
        r0, r1 = max(r0, 0), min(r1, ROWS)
        out.append(list(range(r0, r1)))
    return out


COTSZ = (TEX + 1) * 2           # a codeofs table: 33 words


def scaler(h, backend, word, loads=None):
    """The scaler's bytes; `loads`, when given, collects per texel the
    offset of its load WITHIN the scaler (None for a texel that emits
    nothing)."""
    code = bytearray()
    st = STORE_W if word else STORE_B
    for v, rows in enumerate(texel_rows(h)):
        if not rows:
            if loads is not None:
                loads.append(None)
            continue
        if loads is not None:
            loads.append(len(code))
        code += LOAD + bytes([v])
        evens = [r for r in rows if r & 1 == 0]
        odds = [r for r in rows if r & 1]
        if evens:
            if word:
                code += AHAL
            for r in evens:
                code += st + (r * ROWS).to_bytes(2, "little")
        if odds:
            code += PHASE[backend]
            if word:
                code += AHAL
            for r in odds:
                code += st + (r * ROWS).to_bytes(2, "little")
    code += RET
    return bytes(code)


def codeofs(h, backend, word, base):
    """The 33-word table of the scaler at part offset `base`: codeofs[v] =
    base + the load of the first emitted texel >= v, or 0."""
    loads = []
    scaler(h, backend, word, loads)
    out = [0] * (TEX + 1)
    nxt = 0
    for v in range(TEX - 1, -1, -1):
        if loads[v] is not None:
            nxt = base + loads[v]
        out[v] = nxt
    return b"".join(o.to_bytes(2, "little") for o in out)


def width(h, word):
    """w(h), at least 1: the 2-row scaler at Low res rounds to 0 columns and
    a zero-wide table would divide by it (the generator hung on it once)."""
    w = ((h * 71 + 256) >> 9) if word else ((h * 71 + 128) >> 8)
    return max(1, w)


def col2tex(h, word):
    w = width(h, word)
    return bytes([w] + [(j * TEX) // w for j in range(w)])


def image(backend, base=0):
    """(bytes, sctab, c2t): the generated region from `base` on - the Full
    set, the Low-res set, then the two col2tex blocks - and the two
    directories as the generator writes them: sctab[set][h] the scaler's
    offset in the part for every h 0..120 (aliased down), c2t[set][h] the
    col2tex block's."""
    out = bytearray()
    sctab = {s: [0] * (HMAX + 1) for s in SETS}
    c2t = {s: [0] * (HMAX + 1) for s in SETS}
    for s in SETS:
        ent = {}
        for h in HEIGHTS:
            here = base + len(out) + COTSZ          # the scaler, after its table
            out += codeofs(h, backend, s == "low", here)
            ent[h] = here
            out += scaler(h, backend, s == "low")
        for h in range(HMAX + 1):
            sctab[s][h] = ent[quantise(h)]
    for s in SETS:
        ent = {}
        for h in HEIGHTS:
            ent[h] = base + len(out)
            out += col2tex(h, s == "low")
        for h in range(HMAX + 1):
            c2t[s][h] = ent[quantise(h)]
    return bytes(out), sctab, c2t


def sizes():
    return {be: len(image(be)[0]) for be in PHASE}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--backend", default="cga4", choices=sorted(PHASE))
    ap.add_argument("--base", type=lambda v: int(v, 0), default=0)
    ap.add_argument("--dump")
    ap.add_argument("--sizes", action="store_true")
    a = ap.parse_args()
    if a.sizes:
        for be, n in sorted(sizes().items()):
            print("pxsgen: %-5s %6d bytes (%d scalers a set, both sets, col2tex)"
                  % (be, n, NSC))
        return 0
    img, sctab, c2t = image(a.backend, a.base)
    print("pxsgen: %s: %d bytes from base 0x%04X; %d scalers a set; the 120-row "
          "scaler at 0x%04X (Full) / 0x%04X (Low res)"
          % (a.backend, len(img), a.base, NSC, sctab["full"][120], sctab["low"][120]))
    if a.dump:
        open(a.dump, "wb").write(img)
        print("pxsgen: wrote %s" % a.dump)
    return 0


if __name__ == "__main__":
    sys.exit(main())
