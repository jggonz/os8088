#!/usr/bin/env python3
"""win16write - Write 3.0 keeps its document while its code comes and goes
(SPEC.md 124.26, docs/plans/WIN16-ANY-PLAN.md S20).

    python3 tests/win16write.py            (row win16write)
    python3 tests/win16write.py --keep     leave the QEMU for a look

*As a user, Write opens a document on a 640 KB machine.*

WRITE.EXE is Microsoft's, in build/win16/ only where the win16-reference
skill's `samples` put it; elsewhere the row SKIPS. WIN16 is built into a
private tree with `W16DEFS=-DW16_CODEKB=40` (tools/os88build.py): the
program's code may hold 40 KB at once, a quarter of Write's 163 KB, so
using it discards segments and loads them again all the time (124.26.6).
On VGA under QEMU:

  1. a sentence is typed, and File > Save As (Write's own dialog) saves it
     as ROW.WRI;
  2. File > New: the window is Untitled again;
  3. File > Open, ROW.WRI: the window is named for it again.

Then ROW.WRI is read off B: on the host: a Write file (31BEh, the text from
offset 128 to fcMac) holding the sentence. WIN16's two counters,
`w16_cnload` and `w16_cndrop`, must both have moved: segments were
discarded and loaded again while Write ran. Write must still be running.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
from win16mines import b                                      # noqa: E402
import win16fpu as F                                          # noqa: E402
from win16sol import SDrive, window_of, W16S_RUNNING          # noqa: E402
from win16pbrush import disk_file                             # noqa: E402

NAME = "WRITE.EXE"
CODEKB = 40
SENTENCE = "win16 row says hi"
KEYS = {" ": "spc", ".": "dot"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16write: SKIP - no build/win16/%s (the win16-reference "
              "skill's `samples`)" % NAME)
        return 77
    tree = os88build.tree("W16DEFS=-DW16_CODEKB=%d" % CODEKB,
                          targets=("win16.o88",))
    os.environ["W16_DEFINES"] = "W16_CODEKB=%d" % CODEKB
    import os88ui
    import xmcheck
    from win16cpu import raise_
    bad = []
    work = tempfile.mkdtemp(prefix="w16w-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                        "--size", "1440", tree.img("win16.o88"),
                        b("win16/DOS.O88"), b(os.path.join("win16", NAME)),
                        "--folder", "SYSTEM/APPDATA",
                        "--folder", "SYSTEM/APPDATA/WIN16"],
                       check=True, capture_output=True)
        d = SDrive(work, b("os8088.img"), bimg, a.keep)
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        raise_(d, ui, dw)
        p = F.Prog(d, ui, xmcheck.sym, S, NAME)
        p.launch(dw)
        d.wait(lambda: p.text("w16_l2") == "Running", 300, "Write to run")
        d.idle(8.0)

        def key(k, pause=0.3):
            subprocess.run([sys.executable, "tools/qmp.py", d.sock,
                            "sendkey " + k], check=True, capture_output=True)
            time.sleep(pause)

        def typed(s):
            for c in s:
                key(KEYS.get(c, c))

        def title():
            w = window_of(ui, p)
            return w.title if w else ""

        def running(when):
            if p.pb("w16_state") != W16S_RUNNING:
                bad.append("Write is not running %s: %s / %s"
                           % (when, p.text("w16_l1"), p.text("w16_l2")))
                raise StopIteration

        running("at its start")
        load0, drop0 = p.pw("w16_cnload"), p.pw("w16_cndrop")
        typed(SENTENCE)                 # 1: typed, then Save As ROW.WRI
        time.sleep(2.0)
        key("alt-f", 2.0)
        key("a", 8.0)
        typed("row")
        key("ret", 12.0)
        running("after File > Save As")
        if "ROW.WRI" not in title().upper():
            bad.append("after Save As the window is %r, not named ROW.WRI"
                       % title())
        key("alt-f", 2.0)               # 2: File > New
        key("n", 8.0)
        running("after File > New")
        if "UNTITLED" not in title().upper():
            bad.append("after File > New the window is %r, not Untitled"
                       % title())
        key("alt-f", 2.0)               # 3: File > Open ROW.WRI
        key("o", 8.0)
        typed("row.wri")
        key("ret", 15.0)
        running("after File > Open")
        if "ROW.WRI" not in title().upper():
            bad.append("after File > Open the window is %r, not named ROW.WRI"
                       % title())
        load, drop = p.pw("w16_cnload") - load0, p.pw("w16_cndrop") - drop0
        print("Write: %r; while it ran, %d segments loaded and %d discarded "
              "under a %d KB code budget" % (title(), load, drop, CODEKB))
        if load < 1 or drop < 1:
            bad.append("no segment was discarded and loaded again (%d loaded, "
                       "%d discarded): the budget did not bite" % (load, drop))
        data = disk_file(d.p.b, "ROW.WRI")    # QEMU's copy, not src.img
        if data is None:
            bad.append("no ROW.WRI on B:")
        else:
            ident = struct.unpack_from("<H", data, 0)[0] if len(data) > 2 else 0
            mac = struct.unpack_from("<I", data, 14)[0] if len(data) > 18 else 0
            text = data[128:mac].decode("latin-1") if 128 <= mac <= len(data) \
                else ""
            print("ROW.WRI: %d bytes, ident %04Xh, text %r"
                  % (len(data), ident, text))
            if ident != 0xBE31 or text != SENTENCE:
                bad.append("ROW.WRI is not a Write file holding %r (ident "
                           "%04Xh, text %r)" % (SENTENCE, ident, text))
        if not a.keep:
            d.p.close()
    except StopIteration:
        pass
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16write: typed, saved, new, opened again under a %d KB code "
              "budget, with segments discarded and reloaded, and ROW.WRI "
              "whole on B:" % CODEKB)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
