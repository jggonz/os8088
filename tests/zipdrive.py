#!/usr/bin/env python3
"""zipdrive - SPEC.md 138, docs/plans/ZIP-PLAN.md claims 1-5, 7 and 8: ZIP.DRV
end to end, on MartyPC with an emulated Zip drive.

    make zipimg && python3 tests/zipdrive.py --arm vpi0|vpi2|z250|ps2-0|ps2-2|super

WRITTEN BEFORE THE DRIVER EXISTED and not yet run: the instrument (patch 18) and
this row were built in parallel with ZIP.DRV, so the QA run (ZIP-PLAN Z6) is the
first time it meets the real thing. Everything it reads is the guest's own: the
driver row is found by NAME in drv_tab, the Zip zone by diffing `desk_zslot`
before and after the tick, a window by what `fs_of` says it shows. Two things
are NOT pinned and fail loudly rather than guess: the Eject button's geometry
(tests/zipq.py PAGE_EJECT, SPEC.md 138.6) and the toast's exact words for a
write-protected disk (matched on `protect`).

One arm is one machine and one disk (tests/suite.py has a row for each):

  vpi0   os8088_5150_cga_zip0         VPI0, nibble, output-only card  zip100.img  (200 KB off, 120 KB on)
  vpi2   os8088_5150_cga_zip2         VPI2, nibble                    zip100.img
  z250   os8088_5150_cga_zip250       VPI2, nibble, a Zip 250         zip250.img
  ps2-0  os8088_5150_cga_zip0_bidir   VPI0, PS/2 byte                 zip100.img
  ps2-2  os8088_5150_cga_zip2_bidir   VPI2, PS/2 byte                 zip100.img  (200 KB off, 120 KB on)
  super  os8088_5150_cga_zip0         a SUPERFLOPPY (no partition)    zip100super.img

and each does, in order (the numbers are ZIP-PLAN's claims):

  1  tick `Zip Drive` on the Drivers page: the row loads with no error, ONE new
     desktop zone appears (D: or later), the driver's page says the protocol
     and mode (`VPI0 nibble`, `VPI2 PS/2`...) and the size (`96 MB`)
  2  the zone opens and the root lists what tools/os88zip.py says the host
     wrote - a folder too, and into it; README.TXT opens in an editor and its
     sentence is in the guest's memory
  3  a file copied OFF the Zip onto B: is the host's bytes (read back off B:
     through tools/os88flush.py), and a file copied from B: ONTO the Zip is in
     the host file afterwards, byte for byte, with the volume still passing
     os88zip's fsck - 200 KB (BIG200.BIN) on the vpi0 and ps2-2 arms
  4  Eject from the PAGE: the window closes, the zone stays (an empty drive),
     START STOP UNIT was sent, and the next disk inserted by the debug verb
     mounts with ITS files and not the first disk's FAT; the disk pulled by
     the verb (a user's hand) and put back is seen the same way
  5  a write-protected disk: it still opens and lists, a write is refused with
     a toast that says `protect`, and the host file is the same bytes after
  8  (the z250 and super arms are claim 8's: a Zip 250 and a superfloppy mount)
  7  (the vpi2 and ps2 arms are claim 7's)

`zip_stats` closes each arm: `violations` 0, and the mode that ran (`nibble_bytes`
against `ps2_bytes`) is the one the machine has - a driver that quietly used
nibble on a PS/2 port passes every byte compare and is eight times slower.

Break on purpose: have the driver answer 04h for every transfer past the first
512 KB (the length fence, 138.4) and step 3's 200 KB copy fails; leave
START STOP UNIT out of ZIPV_EJECT and step 4's stats check does.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.chdir(ROOT)
import os88flush                                           # noqa: E402
import os88marty as M                                      # noqa: E402
import os88ui                                              # noqa: E402
import os88zip                                             # noqa: E402
import zipq                                                # noqa: E402

BOOT = "build/os8088-360.img"
BIG = 200000
SMALL = 4001

#          machine,                     disk,              mode text,    size,    big, ps2
ARMS = {
    "vpi0":  ("os8088_5150_cga_zip0",       "zip100.img",      "VPI0 nibble", "96 MB",  True,  False),
    "vpi2":  ("os8088_5150_cga_zip2",       "zip100.img",      "VPI2 nibble", "96 MB",  False, False),
    "z250":  ("os8088_5150_cga_zip250",     "zip250.img",      "VPI2 nibble", "239 MB", False, False),
    "ps2-0": ("os8088_5150_cga_zip0_bidir", "zip100.img",      "VPI0 PS/2",   "96 MB",  False, True),
    "ps2-2": ("os8088_5150_cga_zip2_bidir", "zip100.img",      "VPI2 PS/2",   "96 MB",  True,  True),
    "super": ("os8088_5150_cga_zip0",       "zip100super.img", "VPI0 nibble", "96 MB",  False, False),
}

bad = []


def ok(cond, what, got=None):
    print("  [%s] %s%s" % ("PASS" if cond else "FAIL", what,
                           "" if got is None else "  (%r)" % (got,)))
    if not cond:
        bad.append(what)
    return cond


def sha(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def listing_of(ui, w):
    """{NAME: is_dir} of what the guest's Disk window shows."""
    return {n.upper(): ty == 2 for n, ty in ui.listing(w) if ty != 3}


def select(ui, w, name):
    """One click on a row, confirmed."""
    idx, _ = ui.entry(name, w)
    row = ui.scroll_to(idx, win=w)
    x, y = ui.row_xy(w, row)
    ui.mo.click(x, y, settle=0)
    M.ui_done(ui.m, "the click on %s" % name)


def copy(ui, src, name, dst):
    """Edit > Copy of `name` in drive `src`'s window, Edit > Paste into
    `dst`'s, and wait for the copy engine to finish."""
    m = ui.m
    w = ui.open_drive(src)
    select(ui, w, name)
    ui.menu_pick("Edit", "Copy")
    ui.open_drive(dst)
    ui.menu_pick("Edit", "Paste")
    M.until(m, lambda _: m.read(m.sym("fcp_busy"), 1)[0] == 0,
            "the copy of %s from %s: to %s:" % (name, src, dst), guest=600.0)
    M.ui_done(m, "the copy's last repaint")
    return m.read(m.sym("fcp_err"), 1)[0]


def mkfloppy(path, files):
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", path, "--size", "360"],
                   check=True, capture_output=True)
    for name, data in files.items():
        src = path + "." + name
        with open(src, "wb") as f:
            f.write(data)
        subprocess.run([sys.executable, "tools/os88fat.py", "add", path, src, name],
                       check=True, capture_output=True)
        os.remove(src)


def run(arm):
    machine, disk, mode, size, big, ps2 = ARMS[arm]
    tmp = tempfile.mkdtemp(prefix="zipdrive-")
    img = zipq.scratch_copy(disk, tmp)
    ref0 = os88zip.open_image(zipq.qa_image(disk))[0]
    bname, bsize = ("BIG200.BIN", BIG) if big else ("README.TXT", None)
    want_b = ref0.read_file(bname)                       # the host's bytes
    fromb = os88zip.pattern(120001 if big else SMALL, 0xB0B)
    second = os88zip.pattern(777, 0x5EC)
    bimg = os.path.join(tmp, "b.img")
    mkfloppy(bimg, {"FROMB.BIN": fromb, "SECOND.TXT": second})
    print("== %s: %s on %s, %s, %s" % (arm, disk, machine, mode, "200 KB copies" if big else "small copies"))

    with os88ui.boot(BOOT, apps=bimg, machine=machine, zip=img, verbose=False) as ui:
        m = ui.m
        # ---- 1: the tick -------------------------------------------------
        row, seg, err, new = zipq.tick_zip(ui)
        if not ok(seg and not err, "1: ZIP.DRV loaded (segment %#06x, error %d)" % (seg, err)):
            return
        if not ok(len(new) == 1 and new[0] >= "D",
                  "1: exactly one new desktop zone, at D: or later", new):
            return
        L = new[0]
        zipq.zip_page(ui)
        # the page draws "Mode", the protocol and the port mode as separate
        # runs (drivers/zip/zipcp.inc), so each word is looked for on its own;
        # which mode RAN is the stats check at the end, off the drive itself
        ok(all(zipq.find_text(m, t) for t in mode.split()), "1: the page says %r" % mode)
        ok(zipq.find_text(m, size), "1: ...and %r" % size)

        # ---- 2: the root, a folder, a file ----------------------------------
        w = ui.open_drive(L)
        ok(listing_of(ui, w) == zipq.listing_ref(zipq.qa_image(disk)),
           "2: the root lists what os88zip says the host wrote", listing_of(ui, w))
        if "DOCS" in zipq.listing_ref(zipq.qa_image(disk)):
            w = ui.open("DOCS")
            ok(listing_of(ui, w) == zipq.listing_ref(zipq.qa_image(disk), "DOCS"),
               "2: ...and DOCS does", listing_of(ui, w))
            ui.open("..")
        ed = ui.open("README.TXT")
        ok(zipq.find_text(m, "If you can read this in os8088, the Zip drive works."),
           "2: README.TXT opens and its sentence is in the guest's memory")
        ui.close(ed)

        # ---- 3: off the Zip, onto it -----------------------------------------------
        fe = copy(ui, L, bname, "B")
        f = os88flush.Flush(marty=m)
        got = f.volume(1).read(bname)
        ok(got == want_b, "3: %s copied OFF the Zip is the host's %d bytes (fcp_err %d)"
           % (bname, len(want_b), fe), None if got == want_b else (len(got or b""), len(want_b)))
        fe = copy(ui, "B", "FROMB.BIN", L)
        ok(fe == 0, "3: FROMB.BIN copied ONTO the Zip (fcp_err %d)" % fe)

        # ---- 4: eject from the page, insert another ---------------------------------
        zipq.page_eject(ui)
        s = m.zip_stats()
        ok(not s["medium"] and s["by_opcode"].get("1b", 0) >= 1,
           "4: Eject sent START STOP UNIT and the drive is empty", (s["medium"], s["by_opcode"]))
        ok(L in zipq.zones(m), "4: ...the zone is still there (an empty drive)")
        ok(not zipq.disk_windows_on(ui, L), "4: ...and no Disk window still shows it")
        with zipq.paused(m):
            v, _ = os88zip.open_image(img)
            ok(v.read_file("FROMB.BIN") == fromb,
               "3: FROMB.BIN is in the HOST FILE, byte for byte, after the eject wrote back")
            ok(v.check() == [], "3: ...and the volume passes os88zip's fsck", v.check())
            ok(v.read_file(bname) == want_b, "3: ...and the file that was there is intact")
            v.img.close()
        other = zipq.scratch_copy("zip100b.img", tmp)
        m.zip_insert(other)
        w = ui.open_drive(L)
        ok(listing_of(ui, w) == zipq.listing_ref(other),
           "4: the NEXT disk mounts with ITS files, not the first disk's", listing_of(ui, w))
        ui.close(w)
        m.zip_eject()                                   # ...and a user's hand
        M.guest_sleep(m, 0.5)
        ok(L in zipq.zones(m), "4: a disk pulled by hand leaves the zone")
        m.zip_insert(img)
        w = ui.open_drive(L)
        ok(listing_of(ui, w) == zipq.listing_ref(img),
           "4: ...and put back, it mounts again with its own files (FROMB.BIN among them)",
           listing_of(ui, w))

        # ---- 5: write-protected --------------------------------------------------
        with zipq.paused(m):
            before = sha(img)
        m.zip_wp(True)
        ui.close(w)
        w = ui.open_drive(L)
        ok(listing_of(ui, w) == zipq.listing_ref(img), "5: a protected disk still opens and lists")
        fe = copy(ui, "B", "SECOND.TXT", L)
        try:
            t = ui.wait_toast(says="protect", limit=60.0)
            ok(True, "5: a write says it is protected", t)
        except Exception as e:                          # noqa: BLE001
            ok(False, "5: a write on a protected disk says nothing about protection", str(e)[:120])
        with zipq.paused(m):
            ok(sha(img) == before, "5: ...and the host file is the same bytes")
        m.zip_wp(False)

        # ---- the whole arm ------------------------------------------------------
        s = m.zip_stats()
        ok(s["violations"] == 0, "the host did nothing the drive has no answer for", s["violations"])
        if ps2:
            ok(s["ps2_bytes"] > 0 and s["nibble_bytes"] == 0,
               "every byte the driver read came through the DATA lines (PS/2)",
               (s["ps2_bytes"], s["nibble_bytes"]))
        else:
            ok(s["nibble_bytes"] > 0 and s["ps2_bytes"] == 0,
               "every byte the driver read came through the STATUS lines (nibble)",
               (s["nibble_bytes"], s["ps2_bytes"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True, choices=sorted(ARMS))
    a = ap.parse_args()
    for need in ("zip100.img", "zip100b.img", "zip250.img", "zip100super.img"):
        if not os.path.exists(zipq.qa_image(need)):
            sys.exit("zipdrive: %s is missing - `make zipimg`" % zipq.qa_image(need))
    run(a.arm)
    if bad:
        sys.exit("zipdrive[%s] FAILED: %d check(s): %s" % (a.arm, len(bad), "; ".join(bad[:4])))
    print("zipdrive[%s]: all checks passed" % a.arm)


if __name__ == "__main__":
    main()
