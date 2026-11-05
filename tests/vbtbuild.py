#!/usr/bin/env python3
"""The Video Battle builder's model and build, host-side
(docs/plans/VBATTLE-BUILDER-PLAN.md wave 1, SPEC.md 125.2).

    python3 tests/vbtbuild.py

What it asserts:
  1. THE DEMO AS A PROJECT BUILDS THE DEMO: `os88vbt.py demo-project` then
     `build` gives `os88vbt.py demo`'s bundle BYTE FOR BYTE, full and
     floppy. Each runs in its own process, so a seed that differs between
     processes (str's hash(), which vbtscene once used) fails here.
  2. THE BUNDLE IS REPRODUCIBLE: `demo` twice, in two processes, is the
     same file.
  3. THE MODEL'S CHECKS BITE: a clip with no anchor, an action a menu names
     that does not exist, a fifth character - each an ERROR (or refused);
     and an untouched demo project has none.
  4. UNDO IS AN EDIT'S INVERSE, and a save and a load lose nothing.
  5. THE ANALYSIS FINDS WHAT IS THERE: two laps of the demo's idle loop
     recommend its own period (24 frames, seam 0); a clip made to start on
     idle frame 10 and end on idle frame 5 recommends exactly those at a
     score of 0; and the longest wait follows the anchors' spacing.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import vbtproj as P     # noqa: E402
import vbtana as A      # noqa: E402
import os88vbt as B     # noqa: E402

VBT = os.path.join(ROOT, "tools", "os88vbt.py")
fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def run(*a):
    subprocess.run([sys.executable, VBT] + list(a), check=True,
                   stdout=subprocess.DEVNULL)


def read(p):
    with open(p, "rb") as f:
        return f.read()


with tempfile.TemporaryDirectory() as t:
    j = lambda n: os.path.join(t, n)
    # 1 and 2
    for tag, extra in (("full", []), ("floppy", ["--floppy"])):
        run("demo-project", j(tag + ".vbp"), *extra)
        run("build", j(tag + ".vbp"), "-o", j(tag + ".VBT"))
        run("demo", j(tag + "-ref.VBT"), *extra)
        ok(read(j(tag + ".VBT")) == read(j(tag + "-ref.VBT")),
           "the %s demo as a project builds demo's bundle byte for byte" % tag)
    run("demo", j("again.VBT"))
    ok(read(j("again.VBT")) == read(j("full-ref.VBT")),
       "the demo bundle is the same from two processes")

    # 3
    pr = P.Project.load(j("full.vbp"))
    ok(not pr.errors(), "the demo project has no errors")
    pr.set_anchors("ari_bolt", [])
    ok(any(x.where == "clips.ari_bolt" and "anchor" in x.text
           for x in pr.errors()), "a clip with no anchor is an ERROR")
    pr.undo()
    ok(not pr.errors(), "undo puts the anchors back")
    pr.menu_add(0, action="nonesuch")
    ok(any("nonesuch" in x.text for x in pr.errors()),
       "a menu naming no action is an ERROR")
    pr.undo()
    for n in ("C", "D"):
        pr.add_character(n)
        pr.menu_add(len(pr.d["characters"]) - 1, action="attack")
    try:
        pr.add_character("E")
        ok(False, "a fifth character is refused")
    except ValueError:
        ok(len(pr.d["characters"]) == 4, "a fifth character is refused")
    ok(not pr.errors(), "4 characters are a battle the engine plays")

    # 4
    before = P.Project.load(j("full.vbp")).d
    pr2 = P.Project.load(j("full.vbp"))
    pr2.set(("enemy", "hp"), 7)
    ok(pr2.d["enemy"]["hp"] == 7, "an edit is applied")
    pr2.undo()
    ok(pr2.d == before, "undo is the edit's inverse")
    ok(pr2.redo() and pr2.d["enemy"]["hp"] == 7, "redo re-applies it")
    pr2.save(j("saved.vbp"))
    ok(P.Project.load(j("saved.vbp")).d == pr2.d,
       "a save and a load lose nothing")
    ok(list(P.Project.load(j("saved.vbp")).d["clips"]) ==
       list(pr2.d["clips"]), "a save keeps the clips' order")

    # 5
    pr = P.Project.load(j("full.vbp"))
    idle = A.screens(pr, "idle")
    cand = A.loop_candidates(idle + idle, min_len=12)
    ok(cand[0][0] == 0 and cand[0][2] - cand[0][1] == len(idle),
       "two laps of the idle loop recommend its own period (%d)" % len(idle))
    mid = A.screens(pr, "ari_attack")[5:9]
    clip = [idle[10]] + mid + [idle[5]]
    r = A.attach(clip, idle, search=1)
    ok(r["first"] == 0 and r["anchors"][:1] == [10] and
       r["entry_scores"][10] == 0,
       "a clip that starts on idle frame 10 recommends anchor 10 at 0 "
       "(got %s)" % r["anchors"])
    ok(r["exit"] == 5 and r["exit_score"] == 0 and r["last"] == len(clip) - 1,
       "...and one that ends on idle frame 5 exits onto 5 at 0 (got %d)"
       % r["exit"])
    ok(A.wait_frames([0], 30) == 30 and A.wait_frames([0, 15], 30) == 15,
       "one anchor waits a whole loop, two opposite ones half")

    # 6. THE LAST OPENING'S SEAM SOUNDS the music's frame before its join:
    # it was a frame of silence between the opening's own sound and the
    # music (the owner's "small but noticeable pause")
    import struct
    pr = P.Project.load(j("full.vbp"))
    pr.d["music"]["join"] = 4 * pr.d["target"]["rate"]
    data = B.build_project(pr)
    bd = B.Bundle(data)
    seam = bd.stream(bd.clips[bd.openings[-1]])[-1]
    n, alen = struct.unpack_from("<HH", seam)
    want = bd.mus[bd.music_at - bd.spf:bd.music_at]
    ok(alen == bd.spf and seam[n - alen:n] == want,
       "the last opening's seam carries the music's frame before its join "
       "(%d bytes of %d)" % (alen, bd.spf))

if fails:
    print("%d FAILED" % len(fails))
    sys.exit(1)
print("all passed")
