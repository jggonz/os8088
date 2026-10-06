#!/usr/bin/env python3
"""Does the Control Panel's Floppy page override the drive detection? (SPEC.md 31.14)

    make && python3 tests/fddpage.py

Two boots of one disk. The first drives the page the way a hand does - a
LEFT press on a drop-down box, the popup it opens (menu_popup, SPEC.md 12.4),
a drag onto the item and a release - four times, and reads each answer out of
`drv_cfg`'s 'FD' bytes; then it closes the panel, which is what writes
SYSTEM.CFG (SPEC.md 31.8). The second boots the disk that was written and
reads what `ovl_fdd_apply` did to `dsk_vtab` and the read bound:

  * A: forced to 5.25 - a zone, DVF_525, and NO DVF_GUESS, so the medium
    cannot take it back at the first mount;
  * B: forced to None - its row stays (a letter is not given away), its zone
    goes;
  * the third unit forced to 3.5 on a machine that claims two - a row it did
    not have, at D: because C: is the hard disk's, zone on, not 5.25;
  * the read bound forced to Track AGAINST a canary that found cylinder runs
    on boot 1, so the leg cannot pass by agreeing with the machine.

Then boot 2 picks the read bound's THIRD item, Cylinder, closes the panel
again, and the disk it writes is booted twice more - the two cases Cylinder
exists to tell apart (SPEC.md 31.14):

  * the close writes the boot sector's BS_CYLASK byte (509) to 1, read back
    off the saved image on the host - the loader's half of the setting;
  * on QEMU, whose CPU is a 286-and-up, so 18.93.2's gate keeps the canary
    off it unless that byte asks. Cylinder must turn the run ON: `boot_cylrun`
    non-zero and `[dsk_cylrun]` = 1. That is the machine the setting is for,
    and MartyPC cannot be one;
  * on QEMU again with byte 509 CLEARED - SYSTEM.CFG's Cylinder alone, the
    state of a disk whose boot sector write failed or predates the byte. The
    run must stay OFF: b2_cylok says this floppy boot never looked, and
    Cylinder is never forced blind on a floppy. Which is also what proves the
    leg above came through the CANARY rather than through ovl_fdd_apply;
  * that Cylinder pick is TESTED at the save (cpc_fdtest), because this boot
    did not cross a head - the close makes exactly one cylinder run; and the
    same pick saved again with that run meeting a ROM whose EOT is a sector
    short (its buffer rewritten to what such a ROM returns) comes back as AUTO: the
    record, byte 509 and the toast 'No Cylinder Here: Auto' all say so;
  * on the same 5150, from a copy whose boot sector carries a WRONG `KSIG`, so
    the canary AFFIRMATIVELY FAILS and the loader reloads at the track bound.
    Cylinder must NOT turn the run back on over that: `[dsk_cylrun]` = 0, the
    record untouched. The same patch on the record-less disk reading
    `boot_cylrun` 0 is the witness that the canary did fail - after the boot,
    a broken patch and a broken ovl_fdd_apply look alike on the Cylinder disk.

Take the `call ovl_fdd_apply` out of drv_boot_x and every boot-2 leg fails;
put `[menu_btn]` back to 2 and the first pick fails, because the popup then
closes the instant it opens under a held LEFT button. Take the `and` with
`b2_cylok` out of ovl_fdd_apply and the failed-canary and cleared-byte legs
fail; take the `ss:` byte out of the loader's gate and the QEMU leg does;
take cp_fdbs's call out of the save and the byte-509 check does.
"""
import argparse
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88build                                            # noqa: E402
import os88flush                                            # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import os88fat                                              # noqa: E402
import os88qemu                                             # noqa: E402

S = os88sym.linear
KERNEL_SEG = 0x0060
TITLE_H = 18
CP_RX = 96
MENU_ITEM_H = 16
DV_SIZE, DV_KIND, DV_UNIT, DV_FLAGS = 16, 0, 1, 2
DVK_BIOS, DVK_FREE = 0, 0xFF
DVF_525, DVF_GUESS = 2, 4


def u16(b):
    return b[0] | (b[1] << 8)


def shot(m, path):
    w, h, rows = m.vram(None)
    os88marty.write_png(path, w, h, rows)


def byte(m, addr):
    return m.read(addr, 1)[0]


def close_watching(m, mo, settle, spt, inject):
    """Close the panel - the SYSTEM.CFG save - stopping at every int 13h, and
    answer how many CYLINDER runs it made: AH=02 for 2*spt sectors from
    cylinder 1, head 0, sector 1, which is cpc_fdtest's one question and
    nothing else's.

    With `inject`, that ONE run is made to return what the BIOS the canary was
    written for returns (SPEC.md 18.93.1): a ROM whose EOT is a sector short
    flips head at sector spt - 1, so from that slot on the buffer holds head
    1's sectors, with CF = 0 and the full count. It is written over the run's
    buffer at the NEXT int 13h - the first of the test's track reads, before
    anything has compared it - so every other read of the save is untouched.
    Repointing int 1Eh at such a table was tried first and changes nothing
    here: MartyPC's FDC carries a multi-track read on at the right sector
    whatever EOT says, so the ROM's failure has to be written in by hand."""
    runs, pending = 0, None
    wx, wy = dispcp._cp_win(m, S)
    mo.click(wx + 10, wy + TITLE_H // 2, settle=0)
    # Armed AFTER the click: the save's first read is behind the window's
    # teardown and repaint, and dispcp.close_panel's own waits refuse a
    # machine stopped at a breakpoint - so the close is driven here instead.
    m.breakpoints([{"type": "int", "addr": 0x13}])
    deadline = 600
    while deadline:
        if not m.wait_stop(0.5):
            if dispcp._cp_win(m, S) is None and not byte(m, S("cp_wdirty")):
                break
            deadline -= 1
            continue
        if pending is not None:
            good = m.read(pending, 2 * spt * 512)
            k = (spt - 1) * 512             # the slot the early flip lands in
            bad = good[:k] + good[spt * 512:] + good[-512:]
            m.write(pending, bad)
            pending = None
        r = m.regs()
        if (r["ax"] >> 8 == 0x02 and r["ax"] & 0xFF == 2 * spt
                and r["cx"] == 0x0101 and r["dx"] >> 8 == 0):
            runs += 1
            if inject:
                pending = (r["es"] << 4) + r["bx"]
        m.run()
    m.breakpoints([])
    m.run()
    if not deadline:
        raise SystemExit("fddpage: the panel never finished closing")
    settle(m)
    return runs


def break_canary(src, dst):
    """A copy of `src` whose boot sector expects a KSIG the kernel does not
    carry, so 18.93.1's canary compares UNEQUAL after the first load and the
    loader loads again at the track bound - a failed canary, on a 5150 whose
    FDC crosses heads perfectly well. KSIG is the word at KSIG_OFF +
    BOOT2_PAD of KERNEL.SYS (the Makefile's KSIGDEF2) and boot.asm loads it
    with one `mov di, imm16`, so the patch is that immediate and nothing else."""
    shutil.copyfile(src, dst)
    eq = os88sym.equates()
    k = bytes(os88fat.Fat12(dst).read("KERNEL.SYS"))
    o = eq["KSIG_OFF"] + eq["BOOT2_SECS"] * 512
    ksig = k[o] | (k[o + 1] << 8)
    with open(dst, "r+b") as f:
        bs = bytearray(f.read(512))
        at = [i for i in range(510 - 2)
              if bs[i] == 0xBF and bs[i + 1] | (bs[i + 2] << 8) == ksig]
        if len(at) != 1:
            sys.exit("fddpage: %d `mov di, %04X` in the boot sector, not one"
                     % (len(at), ksig))
        bad = ksig ^ 0x5A5A
        bs[at[0] + 1], bs[at[0] + 2] = bad & 0xFF, bad >> 8
        f.seek(0)
        f.write(bs)


def qemu_boot(img, apps):
    """`make test` on a scratch copy of `img`, until the desktop is idle.
    QEMU because its CPU is a 286 and up: the one machine here on which
    18.93.2's gate leaves the canary unrun."""
    os88qemu.kill()
    os88qemu.own()
    r = subprocess.run(["make", "test", "TESTIMG=" + img, "TESTAPPS=" + apps],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("fddpage: make test failed:\n" + r.stdout + r.stderr)
    from ethernet import Qemu
    q = Qemu()
    entry = S("cold_entry")
    os88qemu.acted(q, lambda: q.read(entry, 1)[0] == 0xE9 and
                   os88qemu.ui_idle(q, S), secs=90, what="the desktop",
                   poll=0.4)
    return q


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--shot", default="build/fddpage.png")
    a = ap.parse_args(argv)
    # The run's tree, not the shared build/ (tools/os88build.at): launch()
    # resolves a `build/...` string itself, but break_canary's copy and
    # `make test`'s TESTIMG/TESTAPPS do not, and the scratch images below are
    # ABSOLUTE so launch() does not re-base them into a tree that never
    # held them (a soak's FileNotFoundError on fddpage-autobad.img)
    a.image = os.path.abspath(os88build.at(a.image))
    a.apps = os.path.abspath(os88build.at(a.apps))

    eq = os88sym.equates()
    FDD = S("drv_cfg") + eq["CFG_FDD"]
    FDR = S("drv_cfg") + eq["CFG_FDR"]
    R0Y, ROWH, BX1 = eq["CPF_R0Y"], eq["CPF_ROWH"], eq["CPF_BX1"]
    settle = os88marty.settle
    fail = []
    written = os.path.abspath(os.path.join("build", "fddpage-written.img"))
    cylimg = os.path.abspath(os.path.join("build", "fddpage-cyl.img"))
    noimg = os.path.abspath(os.path.join("build", "fddpage-cylno.img"))
    spt = 9                     # build/os8088-360.img, the default --image

    def byte(m, addr):
        return m.read(addr, 1)[0]

    def rows(m):
        t = m.read(S("dsk_vtab"), 8 * DV_SIZE)
        return [(t[i * DV_SIZE + DV_KIND], t[i * DV_SIZE + DV_UNIT],
                 t[i * DV_SIZE + DV_FLAGS]) for i in range(8)]

    def row_of(m, unit):
        for i, (k, u, f) in enumerate(rows(m)):
            if k == DVK_BIOS and u == unit:
                return i, f
        return None, None

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        cyl0 = byte(m, S("boot_cylrun"))
        print("boot 1: FD = %02X %02X, boot_cylrun %d, rows %s"
              % (byte(m, FDD), byte(m, FDR), cyl0, rows(m)))
        if byte(m, FDD) or byte(m, FDR):
            sys.exit("fddpage: the disk already carries an 'FD' record")
        if row_of(m, 2)[0] is not None:
            sys.exit("fddpage: this machine already has a third drive, so "
                     "forcing one proves nothing")
        if not cyl0:
            sys.exit("fddpage: the canary found track runs, so forcing Track "
                     "proves nothing")
        # the read bound is forced to the OPPOSITE of what the canary found
        rd, want_cyl = 1, 0

        # the page's RECORD. cp_items is in CTRL.DRV's image now (kernel
        # size pass 8), so it is the kernel's own equate rather than a walk of
        # the table - ctrl.inc asserts CP_IFDD is the Floppy row at build time
        rec = [os88sym.equates()["CP_IFDD"]]
        f = os88flush.Flush(marty=m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle, page=rec[0])
        settle(m)

        def pick(m, mo, row, item):
            wx, wy = dispcp._cp_win(m, S)
            x0 = wx + 1 + CP_RX + BX1 + 20
            y0 = wy + TITLE_H + 1 + R0Y + row * ROWH + 7

            def aim(_mo):
                os88marty.until(m, lambda mm: mm.read(S("menu_dropd"), 1)[0],
                                "the drop-down's list to be up", poll=0.05,
                                guest=10.0)
                x = u16(m.read(S("menu_x1"), 2))
                y = u16(m.read(S("menu_y1"), 2))
                return x + 12, y + 1 + item * MENU_ITEM_H + 8
            mo.menu(x0, y0, 0, 0, aim=aim)

        steps = ((1, 1, lambda: byte(m, FDD) == 0x04, "B: None"),
                 (2, 3, lambda: byte(m, FDD) == 0x34, "unit 2 3.5"),
                 (0, 2, lambda: byte(m, FDD) == 0x36, "A: 5.25"),
                 (4, rd, lambda: byte(m, FDR) == rd, "reads %d" % rd))
        for row, item, ok, what in steps:
            try:
                pick(m, mo, row, item)
                os88marty.until(m, lambda _: ok(), what, poll=0.05,
                                guest=10.0)
                print("boot 1: %-10s ok, FD = %02X %02X"
                      % (what, byte(m, FDD), byte(m, FDR)))
            except os88marty.MartyError as e:
                fail.append("%s: the pick did not reach drv_cfg (%s)"
                            % (what, e))
        settle(m)
        shot(m, a.shot)
        print("boot 1: the page is in %s" % a.shot)
        if not byte(m, S("cp_wdirty")):
            fail.append("no pick marked the settings dirty")
        dispcp.close_panel(m, mo, S, settle)
        if byte(m, S("cp_dsave")):
            fail.append("SYSTEM.CFG was not written: cp_dsave = %d"
                        % byte(m, S("cp_dsave")))
        try:
            cfg = f.volume(0).read("SYSTEM.CFG")
        except os88flush.FlushError:
            cfg = b""
        if b"FD\x01\x02" not in cfg:
            fail.append("SYSTEM.CFG carries no 'FD' ver 1 len 2 record")
        f.save(0, written)

    with os88marty.launch(written, apps=a.apps, machine=a.machine) as m:
        print("boot 2: FD = %02X %02X, boot_cylrun %d, dsk_cylrun %d, rows %s"
              % (byte(m, FDD), byte(m, FDR), byte(m, S("boot_cylrun")),
                 byte(m, S("dsk_cylrun")), rows(m)))
        i, fl = row_of(m, 0)
        if i != 0 or fl & 7 != 1 | DVF_525:
            fail.append("A: is not a 5.25 zone with no guess: row %s flags %s"
                        % (i, fl))
        i, fl = row_of(m, 1)
        if i != 1 or fl & 1:
            fail.append("B: did not keep its row and lose its zone: row %s "
                        "flags %s" % (i, fl))
        i, fl = row_of(m, 2)
        if i != 3 or fl is None or fl & 7 != 1:
            fail.append("the third unit is not a 3.5 zone at D:: row %s "
                        "flags %s" % (i, fl))
        if (byte(m, S("boot_cylrun")) != 0) != bool(want_cyl) or \
                byte(m, S("dsk_cylrun")) != want_cyl:
            fail.append("the read bound was not forced to %d" % want_cyl)
        shot(m, os.path.splitext(a.shot)[0] + "-boot2.png")

        # --- the read bound's third item: Cylinder -------------------------
        f = os88flush.Flush(marty=m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, settle, page=rec[0])
        settle(m)
        try:
            pick(m, mo, 4, 2)
            os88marty.until(m, lambda _: byte(m, FDR) == 2, "reads 2",
                            poll=0.05, guest=10.0)
            print("boot 2: reads 2    ok, FD = %02X %02X"
                  % (byte(m, FDD), byte(m, FDR)))
        except os88marty.MartyError as e:
            fail.append("Cylinder: the pick did not reach drv_cfg (%s)" % e)
        settle(m)
        runs = close_watching(m, mo, settle, spt, inject=False)
        print("boot 2: the close's cylinder runs: %d (the pick's test)" % runs)
        if runs != 1:
            fail.append("the Cylinder pick on a boot that did not cross a "
                        "head made %d cylinder runs, not the test's 1" % runs)
        try:
            cfg = f.volume(0).read("SYSTEM.CFG")
        except os88flush.FlushError:
            cfg = b""
        if b"FD\x01\x02\x36\x02" not in cfg:
            fail.append("SYSTEM.CFG's 'FD' record is not 36 02 after Cylinder")
        f.save(0, cylimg)

        # --- ...and the same pick on a BIOS that cannot cross a head -------
        # Track then Cylinder, so the row changed in THIS session and the
        # pick is tested again; the close's cylinder run is then made to see
        # what the failing ROM does (close_watching).
        dispcp.open_panel(m, mo, S, settle, page=rec[0])
        settle(m)
        try:
            pick(m, mo, 4, 1)
            os88marty.until(m, lambda _: byte(m, FDR) == 1, "reads 1",
                            poll=0.05, guest=10.0)
            pick(m, mo, 4, 2)
            os88marty.until(m, lambda _: byte(m, FDR) == 2, "reads 2",
                            poll=0.05, guest=10.0)
        except os88marty.MartyError as e:
            fail.append("Track then Cylinder did not reach drv_cfg (%s)" % e)
        settle(m)
        runs = close_watching(m, mo, settle, spt, inject=True)
        toast = m.read(S("toast_buf"), 32).split(b"\0")[0]
        print("boot 2: an early flip injected into %d cylinder run(s): FDR %d, "
              "toast %r" % (runs, byte(m, FDR), toast))
        try:
            cfg = f.volume(0).read("SYSTEM.CFG")
        except os88flush.FlushError:
            cfg = b""
        if runs != 1 or byte(m, FDR) != 0:
            fail.append("a Cylinder whose test read the wrong head was not "
                        "set back to Auto: %d run(s), FDR %d"
                        % (runs, byte(m, FDR)))
        if b"FD\x01\x02\x36\x00" not in cfg:
            fail.append("SYSTEM.CFG's 'FD' record is not 36 00 after the "
                        "refused Cylinder")
        if toast != b"No Cylinder Here: Auto":
            fail.append("the refusal was not said: the toast is %r" % toast)
        f.save(0, noimg)

    BS = eq["BS_CYLASK"]
    for img, want in ((written, 0), (cylimg, 1), (noimg, 0)):
        with open(img, "rb") as fh:
            got = fh.read(512)[BS]
        print("disk:   %s boot sector byte %d = %d"
              % (os.path.basename(img), BS, got))
        if got != want:
            fail.append("%s's boot sector byte %d is %d, not %d"
                        % (os.path.basename(img), BS, got, want))

    # --- Cylinder on a 286 and up: the byte opens the gate, the canary rules
    def qemu_leg(name, ask, want):
        qimg = os.path.abspath(os.path.join("build", "fddpage-%s.img" % name))
        shutil.copyfile(cylimg, qimg)
        with open(qimg, "r+b") as fh:
            fh.seek(BS)
            fh.write(bytes([ask]))
        q = qemu_boot(qimg, a.apps)
        try:
            qb = q.read(S("drv_cfg") + eq["CFG_FDR"], 1)[0]
            qr = u16(q.read(S("boot_cylrun"), 2))
            qd = q.read(S("dsk_cylrun"), 1)[0]
        finally:
            q.quit()
            os88qemu.kill()
        print("qemu:   byte %d = %d: FDR %d, boot_cylrun %d, dsk_cylrun %d"
              % (BS, ask, qb, qr, qd))
        if qb != 2 or bool(qr) != bool(want) or qd != want:
            fail.append("QEMU, Cylinder, byte %d = %d: wanted the run %s, got "
                        "boot_cylrun %d dsk_cylrun %d"
                        % (BS, ask, "ON" if want else "OFF", qr, qd))
    qemu_leg("qemu", 1, 1)
    qemu_leg("qemunoask", 0, 0)

    # --- the witness: the same patch on the Auto disk FAILS the canary -----
    # Below, a broken KSIG and a broken ovl_fdd_apply read identically after
    # the boot - Cylinder writes over the loader's 0 - so the loader's half is
    # proved on a disk with no record, where nothing writes over it.
    autobad = os.path.abspath(os.path.join("build", "fddpage-autobad.img"))
    break_canary(a.image, autobad)
    with os88marty.launch(autobad, apps=a.apps, machine=a.machine) as m:
        wr = u16(m.read(S("boot_cylrun"), 2))
        print("boot 3: FD = %02X %02X, boot_cylrun %d (KSIG broken, Auto)"
              % (byte(m, FDD), byte(m, FDR), wr))
    if wr:
        fail.append("the broken KSIG did not fail the canary: boot_cylrun is "
                    "%d on the Auto disk, so the next leg proves nothing" % wr)

    # --- Cylinder over a canary that FAILED this boot -----------------------
    badimg = os.path.abspath(os.path.join("build", "fddpage-cylbad.img"))
    break_canary(cylimg, badimg)
    with os88marty.launch(badimg, apps=a.apps, machine=a.machine) as m:
        br, bd = u16(m.read(S("boot_cylrun"), 2)), byte(m, S("dsk_cylrun"))
        print("boot 4: FD = %02X %02X, boot_cylrun %d, dsk_cylrun %d (KSIG "
              "broken, Cylinder)" % (byte(m, FDD), byte(m, FDR), br, bd))
        if br or bd:
            fail.append("Cylinder turned the run on over a FAILED canary: "
                        "boot_cylrun %d, dsk_cylrun %d" % (br, bd))
        if byte(m, FDR) != 2:
            fail.append("the record changed under a failed canary: FDR %d"
                        % byte(m, FDR))

    for x in fail:
        print("FAIL: %s" % x)
    print("fddpage: %s" % ("FAILED" if fail else
                           "ok - five picks, two files, five reboots"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
