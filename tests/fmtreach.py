#!/usr/bin/env python3
"""Does a 720K format PROVE the drive reaches cylinder 79 - and refuse when not?

    python3 tests/fmtreach.py

SPEC.md 18.96.2. A 720K layout puts boot sector, both FATs and the root inside
CYLINDER 0, so on a 40-track drive it formats, mounts and then lies about its
size. dskw_fmt_reach_x is the check: after a row-2 (720K) format it writes a
marker to the volume's LAST sector (LBA 1439, cylinder 79), re-zeroes the
buffer, reads the sector back and compares. A failure re-formats the disk as
360K and says 'Made 360K, not 720K'. No other row reaches that code: fmtlow
formats 360K, and only row 2 is checked.

TWO ARMS, one boot each, on os8088_5150_cga_720b_gla (an 80-track B:):

  A. POSITIVE. The confirmation offers 360K (GLaBIOS answers no AH=08h for a
     floppy, so the probe resolves 9-sector media downward); the row is set to
     2 at the prompt - the user's Space, which the key line offers only on an
     external drive - and Enter. All 160 tracks are laid, the reach test's
     read of cylinder 79 comes back with the marker, the verdict is
     'Formatted B:', the row stays 2, and the host reads B: as an empty 720KB
     volume whose sector 1439 carries the marker (the LBA, three times).
  B. NEGATIVE CONTROL. The same, but the reach test's READ of cylinder 79 is
     sent to cylinder 78 at the int 13h gate - a head that stepped short, the
     case the marker's LBA exists for. The sector that comes back is another
     track's freshly-formatted one, the compare must FAIL, and the disk is
     remade 360K: 80 more tracks, 'Made 360K, not 720K', row 3, and the host
     reads an empty 360KB volume.

VERIFIED RED both ways (2026-10-04): a compare that always passes (the
`jne dskw_fmt_stc` after the `repe scasw` taken out) fails arm B with
'Formatted B:'; a reach test that always fails fails arm A with
'Made 360K, not 720K'.

THE WAITS ARE ON GUEST STATE. The format holds the gfx lock for its whole
run, so the screen is still while it works and `settle` returns early - the
verdict toast, said after the re-list (SPEC.md 22.12.1), is what this waits
for - and the flush waits for the controller to idle and is taken paused
(os88flush), which is what os88marty.Marty.flush asks for anyway.

THE FIXTURE'S BOOT SECTOR IS NOT A BPB, AND THAT IS WHAT MADE THIS ROW
FLAKY. MartyPC saves a raw image through fluxfox, whose raw writer sizes the
file from `closest_format(trust_bpb=true)` - and the BPB it trusts is the one
it PARSED WHEN THE IMAGE WAS MOUNTED (DiskImage::post_load_process), never
refreshed by anything the guest writes since. Random bytes give a media byte
fluxfox recognises about 6 times in 256 (FE FC FD FF F9 F0, bpb.rs's
fallback): FD made it write a 360KB file of the 720KB disk the guest had just
formatted (sector 1439 simply absent - the "(EMPTY)" marker failure), and F0
made it read 18-sector tracks off 9-sector ones and refuse with DataError.
Both reproduce every time with that byte forced, and neither with 00. So the
BPB bytes (11-35) of the fixture are zeroed: still a disk nothing can mount
(BytsPerSec 0), and fluxfox falls back to the track geometry it analysed at
mount, 80 x 2 x 9. The flushed file's SIZE is checked first, so a future
mis-sized capture says so instead of looking like a missing marker.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
import os88marty as M                                     # noqa: E402
import os88flush                                          # noqa: E402
import os88sym                                            # noqa: E402
import os88ui                                             # noqa: E402
from harness import check, done                           # noqa: E402

MACHINE = "os8088_5150_cga_720b_gla"
SYS = "build/os8088-360.img"
LAST = 1439                     # 720K: 2 x 80 x 9 sectors, the last LBA
OK, BAD = "Formatted B:", "Made 360K, not 720K"


# THE ASSEMBLER'S VALUES, not a regex over the source: FS_SIZE is
# `FS_USEDH+2` (61) on kern_big and a literal 24 only in the kern_small arm,
# so a first-digits match read the small build's stride and walked kern_big's
# pool 24 bytes a slot - right for slot 0 alone, and the "every slot clear"
# wait then read bytes out of slot 0's path and slot 1's head.
EQ = os88sym.equates()
FS_EDIT, FS_SIZE, FM_NSLOT = EQ["FS_EDIT"], EQ["FS_SIZE"], EQ["FM_NSLOT"]


def edit(m):
    pool = m.sym("fm_pool")
    for slot in range(FM_NSLOT):
        b = m.read(pool + slot * FS_SIZE, FS_SIZE)
        if b[FS_EDIT]:
            return b[FS_EDIT]
    return 0


def arm(wrong, tmp):
    fix = os.path.join(tmp, "b.img")
    out = os.path.join(tmp, "b.out")
    img = bytearray(os.urandom(737280))         # a foreign format: the
    img[11:36] = bytes(25)                      # probe reads it at 9 spt -
    open(fix, "wb").write(img)                  # and NO BPB (see the top)
    tag = "B (cylinder 79's read sent to 78)" if wrong else "A"
    print("== arm %s ==" % tag)
    with os88ui.boot(SYS, apps=fix, machine=MACHINE) as ui:
        m = ui.m
        try:
            ui.open_drive("B")          # random bytes mount nothing; the
        except os88ui.UIError:          # WINDOW is what counts
            pass
        M.settle(m)
        ui.menu_pick("File", "Format Disk...")
        M.until(m, lambda mm: edit(mm) == 5, "the format confirmation",
                poll=0.2, guest=60.0)
        row = m.read(m.sym("fm_fmtrow"), 1)[0]
        check(row == 3, "arm %s: the probe offers 360K (no AH=08h)" % tag,
              "", got=row, want=3)
        m.write(m.sym("fm_fmtrow"), bytes([2]))   # the user's Space: 720K

        seen = {"tracks": 0, "rd79": 0, "wr79": 0}

        def hit(mm, rec):
            r = rec.get("regs") or mm.regs()
            ah, ch = r["ax"] >> 8, r["cx"] >> 8
            if ah == 0x05:
                seen["tracks"] += 1
            elif ah == 0x03 and ch == 79:
                seen["wr79"] += 1
            elif ah == 0x02 and ch == 79:
                seen["rd79"] += 1
                if wrong:               # the head stepped short
                    mm.setreg("cx", (78 << 8) | (r["cx"] & 0xFF))

        with M.bp_trace(m, {"type": "int", "addr": 0x13}, regs=True,
                        on_hit=hit):
            m.key("Enter")
            M.until(m, lambda mm: (edit(mm) == 0
                                   and ui.toast()[0] in (OK, BAD)),
                    "the format's verdict", poll=0.5, guest=240.0)
        toast = ui.toast()[0]
        row = m.read(m.sym("fm_fmtrow"), 1)[0]
        print("  tracks %(tracks)d, cylinder 79: %(wr79)d write(s), "
              "%(rd79)d read(s)" % seen)
        print("  verdict %r, row %d" % (toast, row))
        check(seen["wr79"] >= 1 and seen["rd79"] >= 1,
              "arm %s: the reach test wrote and read cylinder 79" % tag,
              "dskw_fmt_reach_x's marker goes to LBA 1439 and back",
              got=(seen["wr79"], seen["rd79"]), want=">= 1 each")
        if wrong:
            check(toast == BAD and row == 3,
                  "arm B: a read of the WRONG track fails the compare and "
                  "the disk is remade 360K", "SPEC.md 18.96.2",
                  got=(toast, row), want=(BAD, 3))
            check(seen["tracks"] == 240,
                  "arm B: 160 tracks of 720K, then 80 of 360K", "",
                  got=seen["tracks"], want=240)
        else:
            check(toast == OK and row == 2,
                  "arm A: the reach test PASSES and the 720K volume stands",
                  "SPEC.md 18.96.2", got=(toast, row), want=(OK, 2))
            check(seen["tracks"] == 160,
                  "arm A: 80 cylinders x 2 heads laid", "",
                  got=seen["tracks"], want=160)
        # THE FLUSH WAITS FOR THE DRIVE AND HOLDS THE MACHINE STILL: the
        # verdict is the operation's last word but not the floppy's, and the
        # re-listed window can still be reading.
        M.quiesce(m, lambda: m.disk(), what="the floppy controller to idle")
        os88flush.Flush(marty=m).save(1, out)

    size = os.path.getsize(out)
    check(size == 737280,
          "arm %s: the flushed image is the whole 720KB drive" % tag,
          "fluxfox sizes a raw flush from the BPB it parsed at MOUNT (see the "
          "top); a short file is the capture, not the format",
          got=size, want=737280)
    ls = subprocess.run([sys.executable, "tools/os88fat.py", "ls", out],
                        capture_output=True, text=True).stdout
    want = "354 of 354" if wrong else "713 of 713"
    check(want + " cluster(s) free" in ls,
          "arm %s: B: on the host is an empty %s volume"
          % (tag, "360KB" if wrong else "720KB"), "",
          got=ls.strip()[-120:], want=want + " cluster(s) free")
    if not wrong:
        img = open(out, "rb").read()
        mark = img[LAST * 512:LAST * 512 + 6]
        got = mark.hex() if len(mark) == 6 else \
            "(the file is %d bytes, short of sector 1439)" % len(img)
        check(mark == LAST.to_bytes(2, "little") * 3,
              "arm A: sector 1439 carries the marker - its own LBA, three "
              "times", "dskw_fmt_reach_x writes it and reads it back",
              got=got, want=(LAST.to_bytes(2, "little") * 3).hex())


tmp = tempfile.mkdtemp(prefix="fmtreach-")
try:
    arm(False, tmp)
    arm(True, tmp)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

done("fmtreach")
