#!/usr/bin/env python3
"""The C SDK's way to big memory (docs/plans/MEGA-HUGE-PLAN.md F3, F6, F7).

    make chuge mega && python3 tests/chuge.py          both legs
    python3 tests/chuge.py --leg mega                  kern_mega under Bochs
    python3 tests/chuge.py --leg big                   kern_big on MartyPC
    python3 tests/chuge.py --keep                      keep the boots' files

tests/chuge/chuge.c is a C package that does everything in os88_main and
returns 0: each step is a line, on port E9 and in CHUGE.TXT beside it
(tests/chuge/chuge.c lists them). This row runs it twice:

  MEGA  kern_mega at 4MB under Bochs, CHUGE.O88 on C:'s RUN folder (MEGARUN
        starts it, as tests/megahuge.py's probe), lines off E9. Asserts
          F6  os88_mem_largest_kb() >= 64 and os88_mem_total_kb() >= 704 -
              the KB doors' answer with the POOL counted - so the
              Weave-shaped pre-read check ACCEPTS eleven 64KB claims, and
              all eleven os88_mem_claim(64) are then really given;
          F3  os88_mem_huge_ask(OS88_HUGE_W32), and os88_mem_huge_w() of
              exactly that is GRANTED, step 8;
          F7  os88_mem_huge(136) step 8; 272 blocks of 512 through
              os88_seg_put by window and back by os88_seg_get; the claim
              written whole by os88_file_write_huge (139,264 bytes, past 16
              bits) and read whole by os88_file_read_huge into a second
              claim, os88_file_hi() = 2, every block equal; and
              os88_file_read_at_seg of the cluster at 128KB into the first
              claim's window 1, which then reads as that block.
  BIG   kern_big on MartyPC (the GLaBIOS XT), CHUGE.O88 opened off B: and
        CHUGE.TXT read back off the live floppy. The same F7 lines with
        step 1000h (a conventional claim), F3's ask answered (OSAPI_MEM_AVAIL
        there) and granted, and F6 REFUSING - 640KB holds no 704KB - which is
        the fallback path of every thunk this changed.

THE FAILING CASE (docs/WRITING-TESTS.md 1). The package is new, so the tree
before has no CHUGE; what makes each assertion bite was seen RED by breaking
one thing at a time:
  - the kernel before (mega-release 626e0ebb's mega.img and megak/,
    `--megabuild DIR`): `total 0001` and `f6 refuse` (MEMH_AVAIL was an ask
    answering the bridge's figure, BX and DX as they went), `w32 got 0000`
    (the 32KB-window ask answered the bridge's 3454K, a claim that cannot
    bridge), and `read ferr 0002` - a plain file past 64KB into a claim above
    1MB was refused FERR_IO (kernel/diskw.inc's dskw_rbody, fixed with this);
  - os88_mem_largest_kb/total_kb on OSAPI_MEM_AVAIL alone (cc_avail skipping
    the ask): `total 023E`, `f6 refuse`;
  - os88_mem_huge's BX at 8000h (what BX = SP asked whenever SP was 8000h):
    `fill bad 0040`, block 64 overwritten by window 1's first block, the
    windows 32KB apart under a walk by 64KB.
"""
import argparse
import os
import re
import shutil
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

PKG = os.path.join(ROOT, "build", "chuge.o88")


def parse(lines):
    """{'what': value or None} for every `CHUGE: what [hex]` line."""
    got = {}
    for l in lines:
        m = re.match(r"CHUGE: (.*?)(?: ([0-9A-F]{4}))?\s*$", l.strip())
        if m:
            got[m.group(1)] = int(m.group(2), 16) if m.group(2) else None
    return got


def common(got, step, fails):
    """The F3 and F7 lines both kernels owe."""
    if "done" not in got:
        fails.append("no `done`: %s" % sorted(got))
    if not got.get("w32 ask"):
        fails.append("F3: os88_mem_huge_ask(W32) answered %s"
                     % got.get("w32 ask"))
    if not got.get("w32 got"):
        fails.append("F3: os88_mem_huge_w() of the ask's %s KB was refused"
                     % got.get("w32 ask"))
    if got.get("w32 step") != step:
        fails.append("F3: the 32KB-window claim's step %s, want %X"
                     % (got.get("w32 step"), step))
    if got.get("step") != step or not got.get("huge"):
        fails.append("F7: os88_mem_huge(136) %s step %s, want step %X" % (
            got.get("huge"), got.get("step"), step))
    for k in ("fill ok", "write ok", "read ok", "readat ok"):
        if k not in got:
            fails.append("F7: no `%s` - %s" % (k, [
                x for x in got if x.split()[0] == k.split()[0]]))
    if got.get("read lo") != 0x2000 or got.get("read hi") != 2:
        fails.append("F7: os88_file_read_huge answered %s:%s, want "
                     "0002:2000" % (got.get("read hi"), got.get("read lo")))


def leg_mega(keep, megabuild):
    import os88bochs
    from os88fixture import need
    for p in ("build/megarun.o88", "build/mbr.bin"):
        need(p)
    out = tempfile.mkdtemp(prefix="chuge-mega-")
    os88bochs.MEGS = 4
    box = os88bochs.Box(out, [PKG], build=megabuild)
    fails, lines = [], []
    t0 = time.time()
    try:
        if box.wait_pm(240):
            box.up()
            box.megarun()
            end = time.time() + 300
            while time.time() < end:
                lines = box.said()
                if any(l.startswith(("kern_mega FAULT", "CHUGE: done"))
                       for l in lines):
                    break
                time.sleep(0.5)
            box.guest_sleep(1)
            lines = box.said()
        else:
            fails.append("kern_mega never switched to protected mode")
    finally:
        box.stop()
    for l in lines:
        if l.startswith(("kern_mega FAULT", "kern_mega CLOSED")):
            fails.append(l)
        if l.startswith("CHUGE"):
            print("chuge mega: " + l)
    got = parse(lines)
    if (got.get("total") or 0) < 704 or (got.get("avail") or 0) < 64:
        fails.append("F6: largest %s total %s KB - the pool is not counted"
                     % (got.get("avail"), got.get("total")))
    if "f6 accept" not in got or got.get("f6 claims") != 11:
        fails.append("F6: the Weave-shaped check %s, %s of 11 claims given"
                     % ("accepted" if "f6 accept" in got else "REFUSED",
                        got.get("f6 claims")))
    common(got, 8, fails)
    print("chuge mega: %.0fs under Bochs" % (time.time() - t0))
    if keep or fails:
        print("chuge mega: the boot is in " + out)
    else:
        shutil.rmtree(out, ignore_errors=True)
    return fails


def leg_big(keep):
    import os88ui
    import os88flush
    img = os.path.join(ROOT, "build", "chuge-big.img")
    shutil.copy(os.path.join(ROOT, "build", "chuge360.img"), img)
    fails, lines = [], []
    t0 = time.time()
    with os88ui.boot("build/os8088-360.img", apps=img) as ui:
        ui.open_drive("B")
        ui.open("CHUGE.O88", expect="refusal")   # os88_main answers 0
        fl = os88flush.Flush(marty=ui.m)
        end = time.time() + 600
        while time.time() < end:
            try:
                txt = fl.volume(1).read("CHUGE.TXT")
            except Exception:
                txt = None
            if txt:
                lines = txt.decode("latin-1").split("\r\n")
                break
            ui.m.run_for(2.0) if hasattr(ui.m, "run_for") else \
                time.sleep(2.0)
    for l in lines:
        if l:
            print("chuge big: " + l)
    if not lines:
        fails.append("no CHUGE.TXT on B: in 600s")
    got = parse(lines)
    if "f6 refuse" not in got:
        fails.append("F6 on 640KB: %s - no conventional heap holds 704KB"
                     % [k for k in got if k.startswith("f6")])
    common(got, 0x1000, fails)
    print("chuge big: %.0fs on MartyPC" % (time.time() - t0))
    if not keep:
        try:
            os.remove(img)
        except OSError:
            pass
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--leg", choices=("mega", "big", "both"), default="both")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--megabuild", help="another tree's mega.img, megak/, "
                    "megarun.o88 and mbr.bin (the red)")
    a = ap.parse_args()
    if not os.path.exists(PKG):
        print("chuge: no %s - `make chuge`" % PKG)
        return 1
    fails = []
    if a.leg in ("mega", "both"):
        fails += ["mega: " + f for f in leg_mega(a.keep, a.megabuild)]
    if a.leg in ("big", "both"):
        fails += ["big: " + f for f in leg_big(a.keep)]
    for f in fails:
        print("FAIL: " + f)
    if not fails:
        print("chuge: ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
