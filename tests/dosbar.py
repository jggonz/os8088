#!/usr/bin/env python3
"""dosbar - launching a DOS program is TWO file operations: one for DOS.O88
and one for the program's load module (SPEC.md 12.8.10, 96.44.5.5, 96.8.2.1).

    make && make doscom && python3 tests/dosbar.py

The row writes BIGLOAD.EXE - build/DOSHELLO.EXE with its load module padded
to ~136 KB, so dos_load reads it in four 32 KB chunks and a tail, the shape
WordPerfect 4.1's 134,512-byte module has - onto a scratch 360KB floppy, then
double-clicks it there with DOS.O88 coming off the 360KB LZB system disk, and
traces the kernel's file widget until the program's READY prompt:

  1. EXACTLY TWO operations open (`fpg_opbegin`): dsl_entry's launch, and
     dos_load's module;
  2. every by-name read of DOS.O88 lands inside one, and so does the LOADER's
     probe of the document (dsl_w16's first cluster, SPEC.md 124.3.1) - which
     sat in front of the launch's operation and filled the trough on its own;
  3. every read of BIGLOAD.EXE but dos_load's one-sector probe is inside one -
     the four chunks and the tail each filled the trough again before
     96.8.2.1;
  4. each operation fills its trough: `[fpg_done]` reaches 90% of `[fpg_opt]`;
  5. at most PHASES_MAX phases OUTSIDE an operation last 100 ms or more - the
     ones a user can see as a bar of their own. They are the two e7bd353b
     had, where the one-bar operation landed: the kernel's read of the
     loader's header off the floppy before any entry proc runs (~390 ms),
     and HIBER.DRV read as the run's bracket opens (~1.1 s, the flash as the
     window comes up);
  6. the widget comes UP at most BARS_MAX times, the count measured on this
     disk with this program.

VERIFIED TO FAIL on f4c112c6, the tree it was written against: leg 1 counts
1 operation (dos_load opened none), leg 2 finds the loader's probe outside the
launch's operation, leg 3 the module's five chunks outside any, and leg 5
eight phases of 100 ms or more where the fix leaves two.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui                                               # noqa: E402
import os88marty as M                                       # noqa: E402

SYS = "build/os8088-360.img"
HELLO = "build/DOSHELLO.EXE"
NAME = "BIGLOAD.EXE"
PAGES = 272                     # 139,264 bytes of header + module: 4 x 32 KB
FPG_SHOWN = 4                   # chunks and a tail after the 512-byte probe
FPG_OP = 8
BARS_MAX = 4
PHASES_MAX = 2
LONG = 0.100 * 4772727          # 100 ms of a 4.77 MHz 8088, in cycles


def fail(msg):
    print("dosbar: FAIL: %s" % msg, flush=True)
    sys.exit(1)


def bigload(path):
    """DOSHELLO.EXE with zeros appended INSIDE its load module.

    hello.asm's last page is full (e_cblp 0) and its stack sits past the
    image in the minalloc area, so whole pages of zeros after the code move
    nothing the program reads: it runs exactly as before, from a module
    dos_load has to fetch in five READ_ATs."""
    with open(HELLO, "rb") as f:
        exe = bytearray(f.read())
    cblp, cp = struct.unpack_from("<2H", exe, 2)
    if cblp != 0 or len(exe) != cp * 512:
        fail("%s is no longer whole pages (e_cblp %d, e_cp %d, %d bytes) - "
             "bigload() pads by pages" % (HELLO, cblp, cp, len(exe)))
    exe += bytes((PAGES - cp) * 512)
    struct.pack_into("<H", exe, 4, PAGES)
    with open(path, "wb") as f:
        f.write(exe)


def main():
    os.chdir(ROOT)
    for p in (SYS, HELLO):
        if not os.path.exists(p):
            fail("%s is missing - `make && make doscom` builds it" % p)
    tmp = tempfile.mkdtemp(prefix="dosbar")
    try:
        exe = os.path.join(tmp, NAME)
        bigload(exe)
        disk = os.path.join(tmp, "dosbar360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", exe], check=True,
                       stdout=subprocess.DEVNULL)
        hits = trace(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return judge(hits)


def trace(disk):
    def on(m, rec):
        f = m.read(m.sym("fpg_on"), 1)[0]
        cyc = int(m.status().get("cycles", 0))
        name, off, cap = "", 0, 0
        if rec["name"] == "dskw_read_at_x":
            r = m.regs()
            raw = m.read(r["ds"] * 16 + r["si"], 16)
            name = raw.split(b"\0")[0].decode("latin1").upper()
            off, cap = (r["dx"] << 16) | r["ax"], r["cx"]
        done = opt = 0
        if rec["name"] == "fpg_show" and f & FPG_OP:
            done = int.from_bytes(m.read(m.sym("fpg_done"), 2), "little")
            opt = int.from_bytes(m.read(m.sym("fpg_opt"), 2), "little")
        return (f, cyc, name, off, cap, done, opt)

    with os88ui.boot(SYS, apps=disk, quick=True) as ui:
        m = ui.m
        ui.open_drive("B")
        M.settle(m)
        with M.bp_trace(m, "fpg_show", "fpg_hide", "fpg_opbegin",
                        "fpg_begin", "fpg_end", "dskw_read_at_x",
                        on_hit=on, cap=12000) as tr:
            ui.path("B:/" + NAME)
            M.until(m, lambda mm: any("READY" in r
                                      for r in (mm.screen() or [])),
                    "the program's READY prompt", guest=150.0, poll=0.2)
        m.type_text("x")
    return tr.hits


def judge(hits):
    ups, shown, ops, best = 0, False, 0, []
    dos_in, dos_out, prog_out, probe_in = 0, 0, [], None
    phases, begun = [], None
    for h in hits:
        n, (f, cyc, name, off, cap, done, opt) = h["name"], h["hit"]
        if n == "fpg_show" and (f & FPG_OP) and opt and best:
            best[-1] = max(best[-1], done * 100 // opt)
        if n == "fpg_show" and (f & FPG_SHOWN) and not shown:
            shown = True
            ups += 1
        elif n == "fpg_hide":
            shown = False
        elif n == "fpg_opbegin":
            ops += 1
            best.append(0)
        elif n == "fpg_begin":
            begun = None if f & FPG_OP else cyc
        elif n == "fpg_end" and begun is not None:
            phases.append(cyc - begun)
            begun = None
        elif n == "dskw_read_at_x" and name == "DOS.O88":
            if f & FPG_OP:
                dos_in += 1
            else:
                dos_out += 1
        elif n == "dskw_read_at_x" and name == NAME:
            if off == 0 and cap > 512 and probe_in is None:
                probe_in = bool(f & FPG_OP)     # the loader's, a cluster
            elif off == 0 and cap == 512:
                pass                            # dos_load's one sector
            elif not f & FPG_OP:
                prog_out.append((off, cap))
    long_ = [c for c in phases if c >= LONG]
    print("dosbar: %d operation(s) filled to %s%%; DOS.O88 read %d time(s) "
          "inside one and %d outside; the loader's probe %s; %d module "
          "read(s) outside one; %d phase(s) outside an operation, %d of them "
          "100 ms or more (%s); the widget came up %d time(s) (at most %d)"
          % (ops, "/".join(map(str, best)) or "-", dos_in, dos_out,
             {None: "not seen", True: "inside", False: "OUTSIDE"}[probe_in],
             len(prog_out), len(phases), len(long_),
             " ".join("%.0fms" % (c / 4772.727) for c in long_) or "-",
             ups, BARS_MAX), flush=True)
    if ops != 2:
        fail("%d fpg_opbegin calls, not two - dsl_entry's launch and "
             "dos_load's module are an operation each (SPEC.md 96.44.5.5, "
             "96.8.2.1)" % ops)
    if dos_out or not dos_in:
        fail("DOS.O88 was read %d time(s) outside the launch's operation "
             "(and %d inside): each such read fills the bar again"
             % (dos_out, dos_in))
    if probe_in is not True:
        fail("the loader's probe of the document was %s the launch's "
             "operation (SPEC.md 96.44.5.5)"
             % ("never seen in" if probe_in is None else "outside"))
    if prog_out:
        fail("%d read(s) of the load module outside an operation (%s): one "
             "bar a 32 KB chunk (SPEC.md 96.8.2.1)" % (
                 len(prog_out), " ".join("%x+%x" % r for r in prog_out)))
    low = [b for b in best if b < 90]
    if low:
        fail("an operation ended with the bar at %d%% of its scale" % low[0])
    if len(long_) > PHASES_MAX:
        fail("%d phases outside an operation lasted 100 ms or more, each a "
             "bar of its own, against the %d measured" % (len(long_),
                                                           PHASES_MAX))
    if ups > BARS_MAX:
        fail("the widget came up %d times between the double-click and READY "
             "against the %d measured: something the launch reads is a bar of "
             "its own again" % (ups, BARS_MAX))
    print("dosbar: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
