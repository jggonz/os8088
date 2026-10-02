#!/usr/bin/env python3
"""What a split-set join costs its TARGET, transfer by transfer
(docs/plans/STREAM-WRITER-PLAN.md 9 and 11).

A 640 KB set in three parts in B:/PARTS is joined by File > Uncompress
To... onto A:, the system disk - two floppies, so every 32 KB block is a
hop: B: to read, A: to write, B: again. Every `int 13h` the kernel makes is
caught just after it returns (dsk_xfer.attempt, as tests/czto.py injects its
errors) and filed by drive, direction and REGION of the volume - the FAT,
the root directory or the data - off the LBA the transfer started at.

What it prints is the measurement the plan asked for before anything else
is built: the target's FAT and directory writes per block, the reads, and
the guest seconds from Enter to `Uncompressed`. It asserts only that the
join worked and the result is the original byte for byte, and that A:
checks clean, so it can be run against any build as an A/B:

    python3 tests/czseq.py                   # today's build/
    OS88_TREE=<dir> python3 tests/czseq.py   # a private tree

`--verb` picks the WRITER: `to` is the above, `same` the join beside its
parts, and `copy`/`copyb` the file manager's Copy and Ctrl+V of a plain file
of the same size, to A: (C: with `--hdd`) or within B:. That is how
docs/reports/STREAM-WRITER-AB-2026-09-29.md was taken, the script copied into
both trees.

`--nobp` runs the same join with no breakpoint at all, which is the time
to quote: a stop per transfer costs the host, never the guest, but the
guest seconds are read off its own cycle counter either way and the two
must agree.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88build                                       # noqa: E402
import os88cz                                          # noqa: E402
import os88flush                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88ui                                          # noqa: E402

MACHINE = "os8088_xt_vga_144"
SIZE = 640 * 1024


class _Hold(object):
    """a launched machine and its UI, closed on the way out like boot()'s"""
    def __init__(self, m, ui):
        self.m, self.ui = m, ui

    def __enter__(self):
        return self.ui

    def __exit__(self, *exc):
        try:
            self.m.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nobp", action="store_true")
    ap.add_argument("--verb", choices=("to", "same", "copy", "copyb"),
                    default="to",
                    help="to: File > Uncompress To... onto A: (C: with "
                    "--hdd). same: File > Uncompress, the result beside the "
                    "parts on B:, one volume and no hops. copy: Edit > Copy "
                    "of a plain file of the same size in B:/FILES, then "
                    "Ctrl+V in A: (C:). copyb: the same copy into B:'s root "
                    "- the file manager's two writers, over the join's data")
    ap.add_argument("--hdd", action="store_true",
                    help="the owner's case: the parts on a 360KB B:, the "
                    "result on C:, a fixed disk the machine booted from - "
                    "no motor to wait for on the target")
    ap.add_argument("--lose", action="store_true",
                    help="SPEC.md 18.8.5's lost hold: once A:'s dirt is "
                    "banked and the machine stands on B:, A:'s banked disk "
                    "signature is zeroed, so the next hop re-reads its "
                    "window. The join must say `Disk error` and leave A: "
                    "with no result, no CMPRESS~.TMP and a clean FAT")
    ap.add_argument("--fatwnone", action="store_true",
                    help="on a FATWNONE=1 kernel, built into a private tree: "
                    "every heap window refused, so A: and B: take the pin "
                    "from each other at every hop and the held volume's "
                    "dirt must be FLUSHED at the park, never banked - a "
                    "bank there is lost at the first hop (SPEC.md 18.8.5)")
    a = ap.parse_args()
    if a.fatwnone:
        os88build.tree("FATWNONE=1", targets=("os8088.img",)).apply()
    global SIZE
    if a.hdd:
        SIZE = 300 * 1024
    sys.path.insert(0, os.path.join(HERE, "unit"))
    import t_lzfmt
    data = t_lzfmt.half_text(SIZE, 21)
    parts = os88cz.split(data, "SET.DAT", 110000 if a.hdd else 230000,
                         os88cz.M_STORE, jobs=1)
    work = os.path.abspath(os.path.join(os88build.at("build"),
                                        "czseq-%d" % os.getpid()))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, "PARTS"))
    args = []
    if a.verb in ("copy", "copyb"):
        os.makedirs(os.path.join(work, "FILES"))
        f = os.path.join(work, "FILES", "SET.DAT")
        open(f, "wb").write(data)
        args.append("FILES:" + f)
        parts = []
    for i, p in enumerate(parts):
        f = os.path.join(work, "PARTS", "SET.%03d" % (i + 1))
        open(f, "wb").write(p)
        args.append("PARTS:" + f)
    img = os.path.join(work, "b.img")
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "tools",
                                                     "os88disk.py"),
                        "-o", img, "--size", "360" if a.hdd else "1440"]
                       + args,
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("czseq: os88disk: " + r.stderr)
    bad = []
    vhd = os.path.join(work, "c.vhd")
    B = os88build.at("build")
    if a.hdd:
        subprocess.run(
            ["python3", "tools/os88hdd.py", "--template",
             "build/martypc/run/media/hdds/default_xtide.vhd", "--out", vhd,
             "--kernel", os.path.join(B, "kernel.sys"),
             "--vbr", os.path.join(B, "boothd.bin"),
             "--mbr", os.path.join(B, "mbr.bin"),
             "--file", "HDD.DRV=" + os.path.join(B, "hdd.drv"),
             "--file", "CLONE.DRV=" + os.path.join(B, "clone.drv")],
            check=True, capture_output=True)
        m0 = os88marty.launch(None, apps=img,
                              machine="os8088_5150_herc_hdd_sb_gla",
                              extra=["--mount", "hd:0:" + vhd])
        ui0 = os88ui.UI(m0, verbose=False)
        ui0.ready(limit=240)
        ctx = _Hold(m0, ui0)
        tgt = 0x80
        vt = os88flush.vhd_volume(vhd)
    else:
        ctx = os88ui.boot(os88build.at("build/os8088.img"), apps=img,
                          machine=MACHINE, verbose=False)
        tgt = 0
        vt = None
    with ctx as ui:
        m = ui.m
        S = ui._S
        fl = os88flush.Flush(marty=m)
        if a.verb in ("same", "copyb"):
            tgt, vt = 1, fl.volume(1)
        vt = vt or fl.volume(0)
        root0, data0 = vt.root_lba, vt.data_lba
        ui.open_drive("B")
        ui.open("FILES" if a.verb in ("copy", "copyb") else "PARTS")
        win = ui.raise_window(ui.disk_window())
        idx, _ = ui.entry("SET.DAT" if a.verb in ("copy", "copyb")
                          else "SET.001", win)
        x, y = ui.row_xy(win, ui.scroll_to(idx, win=win))
        ui.mo.click(x, y)
        ui.settle()
        m.write(S("toast_buf"), bytes(25))
        if a.verb == "to":
            ui.menu_pick("File", "Uncompress To...")
            os88marty.until(m, lambda mm: int.from_bytes(
                m.read(S("fdlg_win"), 2), "little") != 0,
                "the Save box to open", poll=0.1, guest=30.0)
            ui.settle()
        elif a.verb in ("copy", "copyb"):
            ui.menu_pick("Edit", "Copy")
            if a.verb == "copy":
                ui.open_drive("C" if a.hdd else "A")
            else:
                ui.open("..")
            ui.settle()
        tally = {}
        seq = []
        at = S("dsk_xfer.attempt")
        i13 = at + m.read(at, 200).index(b"\xCD\x13") + 2
        pre = i13 - 2
        op, unit, run = S("dsk_op"), S("dsk_unit"), S("dsk_run")
        busy = S("fcp_busy")
        seen = {}
        m.disk(reset=True)
        c0 = int(m.status()["cycles"])
        if a.verb == "to":
            m.key("Enter")
        elif a.verb == "same":
            ui.menu_pick("File", "Uncompress")
        else:
            m.key("ControlLeft", up=False)
            m.key("KeyV")
            m.key("ControlLeft", down=False)

        def done():
            """a VERDICT: the join's own, or any error it ended on - or, for
            a copy, [fcp_busy] up and then down again"""
            if a.verb in ("copy", "copyb"):
                b = m.read(busy, 1)[0]
                if b:
                    seen["b"] = 1
                return bool(seen and not b)
            t, on = ui.toast()
            return bool(on and t and ("compress" in t.lower()
                                      or "error" in t.lower()))

        if a.lose:
            hd0, drv = S("dws_hd0"), S("disk_drive")
            sig = S("dsk_fatwsig")

            def banked(mm):
                return (int.from_bytes(m.read(hd0, 2), "little") != 0xFFFF
                        and m.read(drv, 1)[0] == 1)
            os88marty.until(m, banked, "A:'s dirt to be banked", poll=0.05,
                            guest=300.0)
            m.pause()
            if not banked(m):
                bad.append("the poke missed the banked moment")
            m.write(sig, b"\0\0")      # A: is volume 0
            m.run()
            os88marty.until(m, lambda mm: done(), "the join to end",
                            poll=0.5, guest=900.0)
        elif a.nobp:
            os88marty.until(m, lambda mm: done(), "the join to finish",
                            poll=0.5, guest=900.0)
        else:
            m.bp_exec(pre, i13)
            t_in = None
            try:
                while not done():
                    if m.wait_stop(limit=0.5) != "breakpoint":
                        continue
                    rg = m.regs()
                    cyc = int(m.status()["cycles"])
                    if (rg["cs"] << 4) + rg["ip"] == pre:
                        t_in = cyc
                        m.run()
                        continue
                    dt = cyc - t_in if t_in is not None else 0
                    t_in = None
                    u = m.read(unit, 1)[0]
                    w = m.read(op, 1)[0] == 0x03
                    lba = rg["di"]
                    n = m.read(run, 1)[0]
                    where = ("fat" if lba < root0 else "dir"
                             if lba < data0 else "data")
                    k = ("tgt" if u == tgt else "B:" if u == 1 else hex(u),
                         "write" if w else "read", where)
                    if os.environ.get("CZSEQ_LOG"):
                        seq.append((k[0], k[1][0], lba, n, dt // 4773,
                                    m.read(0x43F, 1)[0], m.read(0x440, 1)[0]))
                    c, s, ct = tally.get(k, (0, 0, 0))
                    tally[k] = (c + 1, s + n, ct + dt)
                    m.run()
            finally:
                m.bp_exec()
                m.run()
        secs = (int(m.status()["cycles"]) - c0) / os88marty.GUEST_HZ
        fdc = m.disk()
        t = os88marty.quiesce(m, lambda: ui.toast()[0],
                              what="the toast's text to be whole")
        if a.verb in ("copy", "copyb"):
            t = "Uncompressed" if m.read(S("fcp_err"), 1)[0] == 0 else \
                "fcp_err %d" % m.read(S("fcp_err"), 1)[0]
        ui.settle()
        if a.hdd:
            m.close()
            tv = os88flush.vhd_volume(vhd)
        else:
            tv = fl.volume(tgt)
        # BY NAME where the result is the only SET.DAT on its volume - the
        # Save box puts Uncompress To...'s wherever A:'s window stands, MEDIA
        # as often as the root - and by PATH on B:, where the source shares
        # its name
        if a.verb in ("same", "copyb"):
            af = {e.path.upper().lstrip("/"): e for e in tv.walk()}
            want = "PARTS/SET.DAT" if a.verb == "same" else "SET.DAT"
        else:
            af = {e.name.upper(): e for e in tv.walk()}
            want = "SET.DAT"
        got = tv.read(af[want].path) if want in af else None
        if a.lose:
            if t != "Disk error" or got is not None or "CMPRESS~.TMP" in af:
                bad.append("a LOST hold: the join said %r, the target holds "
                           "%s" % (t, sorted(n for n in af
                                             if n.startswith(("SET", "CMP")))
                                   or "nothing of it"))
        elif t != "Uncompressed" or got != data:
            bad.append("the join said %r and the target's SET.DAT is %s"
                       % (t, "identical" if got == data else "MISSING"
                          if got is None else "WRONG"))
        dump = os.path.join(work, "a.img")
        if not a.hdd:
            fl.save(tgt, dump)
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "tools",
                                                     "os88disk.py")]
                       + (["--verify-hdd", vhd] if a.hdd
                          else ["--verify", dump]),
                       capture_output=True, text=True)
    if r.returncode:
        bad.append("fsck: " + (r.stdout + r.stderr).strip())
    if os.environ.get("CZSEQ_LOG"):
        with open(os.environ["CZSEQ_LOG"], "w") as f:
            for r in seq:
                f.write("%s %s lba %5d n %3d %5d ms  motor %02x cnt %d\n" % r)
    blocks = (SIZE + 32767) // 32768
    print("czseq: %s%s, %d KB in %d parts, %d blocks, %.1f guest s to %r"
          % (a.verb, " --hdd" if a.hdd else "", SIZE // 1024, len(parts),
             blocks, secs, t))
    print("  the floppy controller: %s" % ", ".join(
        "%s %s" % (k, fdc[k]) for k in sorted(fdc) if isinstance(fdc[k], int)))
    tot = 0.0
    for k in sorted(tally):
        c, s, ct = tally[k]
        sec = ct / os88marty.GUEST_HZ
        tot += sec
        print("  %-3s %-5s %-4s  %5d calls  %6d sectors  %5.2f calls a "
              "block  %6.1f s in the ROM (%.0f ms a call)"
              % (k + (c, s, c / blocks, sec, 1000 * sec / c)))
    if tally:
        print("  int 13h %.1f s of %.1f; the rest is the machine's own"
              % (tot, secs))
    fw = tally.get(("tgt", "write", "fat"), (0, 0, 0))[0]
    if tally and not (a.lose or a.fatwnone) and fw > blocks // 2:
        bad.append("the target took %d FAT writes for %d blocks: its dirt "
                   "was flushed at the hops, not banked (SPEC.md 18.8.5)"
                   % (fw, blocks))
    for b in bad:
        print("  BAD " + b)
    shutil.rmtree(work, ignore_errors=True)
    print("czseq: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
