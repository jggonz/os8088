#!/usr/bin/env python3
"""A KEY PART OF NO BYTES IS REFUSED, NOT RE-READ FOREVER - SPEC.md 98.1.3.8.

    make && python3 tests/vidkpartbad.py

tests/vidplay.py's clip on the Hercules layout with every key cut into
reads of 500 bytes or less (vidkpartpv's), then one hostile edit: the
POSTER key's first part says its length is 0, with KEY_MORE still in its
y0, and the two bytes before it in the file - which a granule read lands
in memory, and which a part's `len` of 0 makes "its last word" - name the
key's own first read. A part's length used to be taken from the part
alone: the walk moved on by 0 bytes, `vp_knrd` read the same record at
the same offset, and found the same answer, forever - inside `vp_kpic`,
on every OPEN of the file, with the gfx lock held. The desktop froze the
moment the file was double-clicked.

1. THE FIXTURE IS WHAT IT CLAIMS: the poster is key 1, in parts, at an
   offset two bytes or more into a sector, and tools/os88vid.py's reader
   refuses it ("a part of 0 in a read of n") while key 2 still reads.
2. THE OPEN RETURNS: the header loads, and a Right is answered - the UI
   task is not inside `vp_kpic` - with no picture made of key 1.
3. THE PLAYER STILL WORKS: a second Right picks key 2, and its picture is
   made ([vp_ploads], [vp_dkey]).

Broken on purpose - `vp_khdr`'s two tests on the part's length taken out -
2 FAILS: the Right is never answered.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
import vidplay                                                # noqa: E402

KPART = 500


class Stop(Exception):
    """a wait that never came true: reported as a FAIL, not a traceback"""


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    if not os.path.exists(pkg):
        sys.exit("vidkpartbad: no build/video.o88 - run `make`")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        good = vidplay.clip(tmp, "herc", kpart=KPART)
        r = vid.Reader(good)
        p = r.poster
        k, off, n = r.keys[p][:3] if p < r.nkeys else (0, 0, 0)
        print("   1: %d keys, poster %d: its first read %d bytes at %d"
              % (r.nkeys, p, n, off))
        if not r.kparts or p != 1 or p + 1 >= r.nkeys or off % 512 < 2 or \
                len(vid.key_reads(r.key(p)[1])) < 2:
            print("   FAIL: 1: the poster is not key 1, in parts with a key "
                  "after it, two bytes into a sector - the row tests nothing")
            return 1
        d = bytearray(open(good, "rb").read())
        if not u16(d, off + 2) & vid.KEY_MORE:
            print("   FAIL: 1: the poster's first part has no KEY_MORE")
            return 1
        struct.pack_into("<H", d, off - 2, n)       # "the next read": itself
        struct.pack_into("<H", d, off, 0)           # a part of NO bytes
        v88 = os.path.join(tmp, "ZERO.V88")
        open(v88, "wb").write(bytes(d))
        os.remove(good)
        rb_ = vid.Reader(v88)
        try:
            rb_.key(p)
            print("   FAIL: 1: os88vid.py's reader takes the part of 0")
            return 1
        except vid.V88Error as e:
            print("   1: the reference refuses it: %s" % e)
        rb_.key(p + 1)                  # (the key after it is sound)
        disk = os.path.join(tmp, "kpbad.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=vidplay.MACHINE["herc"]) as ui:
            m = ui.m

            def wait(cond, what, guest=60.0):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)" % (
                        what, str(e).split(".")[0]))
            try:
                try:
                    w = ui.path("B:/ZERO.V88")
                except Exception as e:      # (a UI task in vp_kpic opens no
                    raise Stop("the file's window never came: %s" % e)
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                base = u16(rec, geom.W_SEG) << 4
                rw = lambda nm: u16(m.read(base + syms[nm], 2))
                rb = lambda nm: m.read(base + syms[nm], 1)[0]
                wait(lambda mm: rb("vp_loaded") == 1, "the header")
                m.key("ArrowRight")
                try:
                    wait(lambda mm: rw("vp_sel") == p, "a Right answered")
                except Stop as e:
                    raise Stop("2: %s - the open never returned (the walk "
                               "re-reads the part of 0)" % e)
                print("   2: the open returned and a Right picked key %d; "
                      "%d pictures made, the box's key %04X"
                      % (rw("vp_sel"), rw("vp_ploads"), rw("vp_dkey")))
                if rw("vp_ploads") or rw("vp_dkey") == p:
                    bad.append("2: a picture was made of the hostile key")
                m.key("ArrowRight")
                wait(lambda mm: rw("vp_ploads") >= 1, "key %d's picture"
                     % (p + 1))
                print("   3: a second Right: key %d picked, the box's key %d"
                      % (rw("vp_sel"), rw("vp_dkey")))
                if rw("vp_sel") != p + 1 or rw("vp_dkey") != p + 1:
                    bad.append("3: key %d picked and %d shown, not %d"
                               % (rw("vp_sel"), rw("vp_dkey"), p + 1))
            except Stop as e:
                bad.append(str(e))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
