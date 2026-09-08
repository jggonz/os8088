#!/usr/bin/env python3
"""NO MODEL DECLARES A RADIUS SMALLER THAN ITS OWN VERTICES (SPEC.md 88.5.11).

`CSM_RAD` is an upper bound on the model's EUCLIDEAN radius: every consumer
but one adds it to a depth or subtracts it from one, which is sphere
arithmetic. `cs_sizepx`'s comment has said since the routine was written
that "it must never be under the true radius", and that was false of the
data the whole time - every model macro computed `wx + wz + h / 2`, which is
the bound for a model whose origin is its CENTRE, and the origin here is the
BASE. 36 of 122 models were under their own radius: the Shard 209 against
306, the Empire State 285 against 389.

The one consumer that is not sphere arithmetic is `cs_inwater`, which
rejects a water ribbon on `|dx| + |dz|` - a MANHATTAN distance, which runs
to sqrt(2) times the radius. It compares against 3/2 of `CSM_RAD` for that
reason, so this checks that too: a flat model's Manhattan radius must fit
inside 3/2 of what it declares. Both halves are one number's contract, so
they are one gate.

That is not a rounding error, because `cs_projall` reads it to decide
`cs_pwhole` - "no vertex of this object is behind the near plane" - and
`cs_edge1` then draws every edge out of `cs_sxv` WITHOUT LOOKING AT `cs_fv`,
while `cs_pinview` turns `cs_seg`'s clip off. A vertex that was never
projected this frame contributed whatever the last object left in its slot,
unclipped: a line straight across the cockpit, intermittently, depending on
what was there before.

So the claim gets a reader. Host-side, no emulator, a fraction of a second:
every model in `build/skies.bin` decoded at the offsets the package's own
equates give, and its declared radius held against the vertices it points
at. It walks EVERY `cs_m_*` in the map and not only the ones a world reaches
today, because a model nothing places yet is one a world places tomorrow.

Break it on purpose: put the `/ 2` back on any of `csworld.inc`'s four
macros, or lower one hand-written `dw`, and this goes red naming the model
and both numbers. Take the `shr`/`add` out of `cs_inwater` and the ribbons
go red instead - and that half needs a source read, because the 3/2 is in
`csflight.inc` and the number it protects is here.
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import dispapps                                             # noqa: E402


def main():
    img = open(os.path.join(ROOT, "build", "skies.bin"), "rb").read()
    mp = dispapps._map("skies")
    E = {}
    for line in open(os.path.join(ROOT, "apps", "skies", "skies.asm")):
        m = re.match(r"^(CS[A-Z]*_[A-Z0-9_]+)\s+equ\s+"
                     r"(-?(?:0[xX][0-9A-Fa-f]+|\d+))\s*(?:;|$)", line)
        if m:
            E[m.group(1)] = int(m.group(2), 0)
    need = ["CSM_TYPE", "CSM_NV", "CSM_RAD", "CSM_VERTS", "CSM_FLAT", "CSM_STACK"]
    miss = [n for n in need if n not in E]
    if miss:
        sys.exit("t_csrad: skies.asm no longer defines %s" % ", ".join(miss))

    def w(off):
        v = int.from_bytes(img[off:off + 2], "little")
        return v - 65536 if v >= 32768 else v

    # A MODEL LABEL, NOT ITS INSIDES: `.v` and `.e` are NASM local labels
    # under the model they belong to, and the map spells them `cs_m_x.e` -
    # decoding an edge list as a model header reads a radius of 3 against a
    # need of 10,067 and looks exactly like the defect this gate is for.
    models = sorted((n, a) for n, a in mp.items()
                    if n.startswith("cs_m_") and "." not in n)
    if len(models) < 60:
        sys.exit("t_csrad: %d models in the map - it does not describe this "
                 "package" % len(models))

    bad, walked = [], 0
    for nm, at in models:
        t = img[at + E["CSM_TYPE"]]
        if t not in (E["CSM_STACK"], E["CSM_FLAT"]):
            continue                    # a model shape this does not know
        nv, rad, vp = img[at + E["CSM_NV"]], w(at + E["CSM_RAD"]), w(at + E["CSM_VERTS"])
        if nv == 0 or not (0 < vp < len(img)):
            continue
        walked += 1
        man = euc = 0.0
        # A STACK is `dw wx, y, wz` a LEVEL, and the level's four corners are
        # (+-wx, y, +-wz); a FLAT is `dw x, z` a vertex, on the ground.
        if t == E["CSM_STACK"]:
            for k in range(nv):
                x, y, z = w(vp + 6 * k), w(vp + 6 * k + 2), w(vp + 6 * k + 4)
                man = max(man, abs(x) + abs(y) + abs(z))
                euc = max(euc, math.sqrt(x * x + y * y + z * z))
        else:
            for k in range(nv):
                x, z = w(vp + 4 * k), w(vp + 4 * k + 2)
                man = max(man, abs(x) + abs(z))
                euc = max(euc, math.hypot(x, z))
        why = ""
        if rad < euc:
            why = "under its EUCLIDEAN radius"
        elif t == E["CSM_FLAT"] and rad + rad // 2 < man:
            why = "cs_inwater's 3/2 does not cover its MANHATTAN radius"
        if why:
            bad.append((nm, rad, man, euc, nv,
                        "stack" if t == E["CSM_STACK"] else "flat", why))

    if bad:
        print("t_csrad: %d model(s) declare LESS than their own vertices need "
              "(SPEC.md 88.5.11)" % len(bad))
        print("  %-22s %-5s %3s %7s %9s %8s  %s"
              % ("model", "type", "nv", "CSM_RAD", "euclidean", "manhattan", "why"))
        for nm, rad, man, euc, nv, t, why in sorted(bad, key=lambda r: r[1] - r[3]):
            print("  %-22s %-5s %3d %7d %9.1f %8d  %s"
                  % (nm, t, nv, rad, euc, man, why))
        print("  cs_projall trusts this bound to say an object is wholly in "
              "front of the near plane, and cs_edge1 then draws every edge "
              "from cs_sxv without testing cs_fv.")
        return 1
    print("t_csrad: %d models, every CSM_RAD over its own Euclidean radius "
          "(and every ribbon inside cs_inwater's 3/2)" % walked)
    return 0


if __name__ == "__main__":
    sys.exit(main())
