#!/usr/bin/env python3
"""A PROJECT'S FILES BY RELATIVE PATH (SPEC.md 126.2): the owner's "store
paths to clips/sound/images relatively if they are alongside, or below the
.vbp ... so I can copy projects around without needing to relink".

    python3 tests/vbtpaths.py

Host side, the model alone (tools/vbtproj.py):

  1. saved: a video, a sound and a band picture in the project's folder or
     below it are written RELATIVE (forward slashes); a file elsewhere stays
     absolute;
  2. the folder copied somewhere else: the copy finds every file - the
     check names none missing;
  3. SAVE AS into another folder: every file still found - the moved
     project's paths rebased, the ones it can no longer reach downward
     absolute;
  4. a path absolute on another machine (D:\\...), the file beside the
     project by that name: found there.

Broken on purpose (save() writing paths as they were given) 1 and 2 fail.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import vbtproj as P         # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def touch(p):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "wb").write(b"x")


def missing(p):
    return [x for x in p.check() if "missing" in x.text]


def main():
    with tempfile.TemporaryDirectory() as t:
        a = os.path.join(t, "A")
        touch(os.path.join(a, "media", "fight.mp4"))
        touch(os.path.join(a, "beep.wav"))
        touch(os.path.join(a, "art", "band.png"))
        touch(os.path.join(t, "elsewhere", "far.wav"))
        p = P.Project()
        p.save(os.path.join(a, "b.vbp"))
        p.add_source("fight", path=os.path.join(a, "media", "fight.mp4"))
        p.add_sound("beep", path=os.path.join(a, "beep.wav"))
        p.add_sound("far", path=os.path.join(t, "elsewhere", "far.wav"))
        p.set(("images",), [{"name": "band", "path": os.path.join(
            a, "art", "band.png"), "col": 0, "row": 0, "dither": True}])
        p.save()
        d = P.Project.load(os.path.join(a, "b.vbp")).d
        got = (d["sources"]["fight"]["path"], d["sounds"]["beep"]["path"],
               d["images"][0]["path"], d["sounds"]["far"]["path"])
        ok(got[:3] == ("media/fight.mp4", "beep.wav", "art/band.png")
           and os.path.isabs(got[3]),
           "1. saved: inside the folder relative, outside absolute (%s)"
           % (got,))
        b = os.path.join(t, "B")
        shutil.copytree(a, b)
        shutil.rmtree(a)
        q = P.Project.load(os.path.join(b, "b.vbp"))
        ok(not missing(q), "2. the folder copied and the original gone: "
           "every file found (%s)" % missing(q))
        c = os.path.join(t, "C")
        os.makedirs(c)
        q.save(os.path.join(c, "b.vbp"))
        r = P.Project.load(os.path.join(c, "b.vbp"))
        ok(not missing(r) and os.path.isabs(r.d["sources"]["fight"]["path"]),
           "3. SAVE AS into another folder: every file still found, the "
           "ones beside the old folder absolute now (%s)"
           % r.d["sources"]["fight"]["path"])
        r.d["sounds"]["beep"]["path"] = "D:\\DosDisks\\os8088\\beep.wav"
        touch(os.path.join(c, "beep.wav"))
        ok(r.here(r.d["sounds"]["beep"]["path"]) ==
           os.path.join(c, "beep.wav") and not r.sound_missing("beep"),
           "4. a Windows path from another machine: the file beside the "
           "project by its name (%s)" % r.here(r.d["sounds"]["beep"]["path"]))
    if fails:
        print("%d FAILED" % len(fails))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
