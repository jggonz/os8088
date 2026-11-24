#!/usr/bin/env python3
"""zipmodel - SPEC.md 138.8, 138.10: MartyPC's Zip drive, with no guest at all.

    python3 tests/zipmodel.py            (make zipimg builds the QA disks)

The instrument is a patch (tools/martypc/patches/18-zip-lpt.patch) and the
driver it will be used to test does not exist yet when the instrument is
finished, so this row drives the INSTRUMENT ALONE: the machine is launched
PAUSED, no guest instruction runs, and a Python host (tests/zipq.py's ZipHost -
Linux's ppa.c and imm.c register sequences through the debug server's `inb` and
`outb`) speaks to the drive through the real LPT card at 378h. What it proves:

  wiring   the `[machine.zip]` key makes a drive, `bidir` makes the card
           tri-state, OS88_ZIPIMG is the medium the drive reads and writes IN
           PLACE (what the host wrote is what the guest reads, and what the
           guest writes is in the host file, byte for byte, afterwards)
  VPI0 and VPI2, each in the mode its machine has: nibble on an output-only
           card (the XT's), PS/2 byte on a bidirectional one - and `zip_stats`
           says which ran (`nibble_bytes` against `ps2_bytes`), because a
           driver that silently fell back to nibble on a PS/2 port would pass
           every data check and be eight times slower
  SCSI     TEST UNIT READY, REQUEST SENSE, INQUIRY (`IOMEGA`, `ZIP 100` or
           `ZIP 250`), READ CAPACITY, MODE SENSE, READ(10), WRITE(10), the UNIT
           ATTENTION a reset and an insert owe, NOT READY with no medium (the
           eject verb AND START STOP UNIT), DATA PROTECT under `zip_wp`
  the card an output-only card gives back what was written however the
           direction bit is set; a bidirectional one tri-states (the core's
           "is this port bidirectional" test, SPEC.md 138.2)

It is the model's own gate: if this is red the Zip rows are meaningless. The
Rust half is `cargo test -p marty_core zip::` in build/martypc/src (nine host
replays, no emulator); this is the same story through a running MartyPC and
the whole configuration path.

Break on purpose: swap the nibble order in ppa_status() (zip.rs) and every
VPI0 nibble read fails its compare; drop the write-back in ZipScsi::
finish_write and the host-file compare after the WRITE fails.
"""
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.chdir(ROOT)
import os88marty as M                                      # noqa: E402
import os88zip                                             # noqa: E402
import zipq                                                # noqa: E402

BOOT = "build/os8088-360.img"
ARMS = (
    # machine,                     vpi2,  ps2,   disk,         product
    ("os8088_5150_cga_zip0",        False, False, "zip100.img", b"ZIP 100"),
    ("os8088_5150_cga_zip2",        True,  False, "zip100.img", b"ZIP 100"),
    ("os8088_5150_cga_zip250",      True,  False, "zip250.img", b"ZIP 250"),
    ("os8088_5150_cga_zip0_bidir",  False, True,  "zip100.img", b"ZIP 100"),
    ("os8088_5150_cga_zip2_bidir",  True,  True,  "zip100.img", b"ZIP 100"),
)

fails = []


def check(cond, what, got=None):
    print("  [%s] %s%s" % ("PASS" if cond else "FAIL", what,
                           "" if got is None else "  (%r)" % (got,)))
    if not cond:
        fails.append(what)


def arm(machine, vpi2, ps2, disk, product, tmp):
    print("\n== %s  (%s, %s)" % (machine, "VPI2" if vpi2 else "VPI0",
                                 "PS/2" if ps2 else "nibble"))
    img = zipq.scratch_copy(disk, tmp)
    ref = os88zip.open_image(zipq.qa_image(disk))[0]     # what the host wrote
    with M.launch(BOOT, machine=machine, boot=False, zip=img) as m:
        h = zipq.ZipHost(m, vpi2, ps2)
        s0 = m.zip_stats()
        check(s0["medium"] and s0["path"] == os.path.abspath(img),
              "the drive has the host image in it", s0["path"])
        sectors = zipq.os88zip.KINDS["zip250" if disk.startswith("zip250") else "ibm"]["total"]
        check(s0["sectors"] == sectors, "...%d sectors" % sectors, s0["sectors"])

        # power-on and a reset each owe one UNIT ATTENTION, reported once
        st, _ = h.command([0, 0, 0, 0, 0, 0])
        check(st == zipq.CHECK, "TEST UNIT READY after power-on is CHECK CONDITION", st)
        check(h.sense()[:2] == (zipq.SK_UNIT_ATTN, 0x29), "...a UNIT ATTENTION, power-on", h.sense())
        st, _ = h.command([0, 0, 0, 0, 0, 0])
        check(st == zipq.GOOD, "...and the next one is GOOD", st)

        st, d = h.command([0x12, 0, 0, 0, 36, 0], 36)
        check(st == zipq.GOOD and d[8:16] == b"IOMEGA  " and d[16:16 + len(product)] == product,
              "INQUIRY says IOMEGA %s" % product.decode(), d[8:32])
        st, d = h.command([0x25] + [0] * 9, 8)
        check(int.from_bytes(d[:4], "big") == sectors - 1 and int.from_bytes(d[4:], "big") == 512,
              "READ CAPACITY is %d sectors of 512" % sectors, d.hex())

        # READ(10): the partition's boot sector, a few clusters of file data
        st, d = h.read(0, 1)
        raw0 = open(img, "rb").read(512)
        check(st == zipq.GOOD and d == raw0, "READ(10) sector 0 is the host file's (the MBR)")
        lba = ref.base + ref.lay.data_lba
        st, d = h.read(lba, 3)
        with open(img, "rb") as f:
            f.seek(lba * 512)
            want = f.read(1536)
        check(st == zipq.GOOD and d == want, "READ(10) 3 sectors of the data area, byte for byte")
        st, d = h.read(ref.base + ref.lay.fat_lba, 4)
        with open(img, "rb") as f:
            f.seek((ref.base + ref.lay.fat_lba) * 512)
            want = f.read(2048)
        check(d == want, "...and 4 sectors of the FAT")

        # a READ past the end is ILLEGAL REQUEST, then READ(10) is not poisoned
        st, _ = h.command(zipq.read10(sectors - 1, 2))
        check(st == zipq.CHECK and h.sense()[:2] == (zipq.SK_ILLEGAL, 0x21),
              "READ(10) past the end is ILLEGAL REQUEST, LBA out of range")

        # WRITE(10): 4 sectors in the free area, read back through the drive
        # and out of the host file
        free_lba = sectors - 2000
        payload = bytes((i * 31 + 7) & 0xFF for i in range(4 * 512))
        st = h.write(free_lba, payload)
        check(st == zipq.GOOD, "WRITE(10) 4 sectors is GOOD", st)
        check(h.read(free_lba, 4)[1] == payload, "...and reads back through the drive")
        with open(img, "rb") as f:
            f.seek(free_lba * 512)
            check(f.read(2048) == payload, "...and is in the HOST FILE, byte for byte")

        # write protect: reads work, a write is DATA PROTECT and writes nothing
        m.zip_wp(True)
        st, d = h.command([0x1A, 0, 0x3F, 0, 12, 0], 12)
        check(d[2] & 0x80, "MODE SENSE reports write-protected", d.hex())
        st = h.write(free_lba, bytes(512))
        check(st == zipq.CHECK and h.sense()[:2] == (zipq.SK_DATA_PROT, 0x27),
              "a WRITE on a protected disk is DATA PROTECT")
        with open(img, "rb") as f:
            f.seek(free_lba * 512)
            check(f.read(512) == payload[:512], "...and wrote nothing")
        check(h.read(0, 1)[1] == raw0, "...while reads still work")
        m.zip_wp(False)

        # eject by the verb, by START STOP UNIT; insert another disk
        m.zip_eject()
        st, _ = h.command([0, 0, 0, 0, 0, 0])
        check(st == zipq.CHECK and h.sense()[:2] == (zipq.SK_NOT_READY, 0x3A),
              "after zip_eject the drive is NOT READY, medium not present")
        st, d = h.command([0x12, 0, 0, 0, 36, 0], 36)
        check(st == zipq.GOOD and d[8:14] == b"IOMEGA", "...INQUIRY still answers")
        other = zipq.scratch_copy("zip100b.img", tmp)
        m.zip_insert(other)
        st, _ = h.command(zipq.read10(0, 1))
        check(st == zipq.CHECK and h.sense()[:2] == (zipq.SK_UNIT_ATTN, 0x28),
              "a new disk is a UNIT ATTENTION, medium changed")
        st, d = h.read(0, 1)
        check(st == zipq.GOOD and d == open(other, "rb").read(512), "...and then reads ITS sector 0")
        st, _ = h.command([0x1B, 0, 0, 0, 2, 0])
        check(st == zipq.GOOD, "START STOP UNIT (LoEj) is GOOD")
        st, _ = h.command([0, 0, 0, 0, 0, 0])
        check(st == zipq.CHECK and h.sense()[:2] == (zipq.SK_NOT_READY, 0x3A),
              "...and ejected the disk")

        s = m.zip_stats()
        check(s["violations"] == 0, "the host did nothing the drive has no answer for", s["violations"])
        check(s["sectors_written"] == 4 and s["sectors_read"] > 0, "counters: %d read, %d written"
              % (s["sectors_read"], s["sectors_written"]))
        if ps2:
            check(s["ps2_bytes"] > 0 and s["nibble_bytes"] == 0,
                  "every byte came in through the DATA lines (PS/2)", (s["ps2_bytes"], s["nibble_bytes"]))
        else:
            check(s["nibble_bytes"] > 0 and s["ps2_bytes"] == 0,
                  "every byte came in through the STATUS lines (nibble)", (s["nibble_bytes"], s["ps2_bytes"]))
        # (paused machine: no guest time passes, so read_us/write_us stay 0 here -
        # zipspeed is the row that reads them, on a running guest)
        check(s["read_cmds"] >= 4 and s["write_cmds"] >= 2,
              "READ and WRITE commands are counted per command",
              (s["read_cmds"], s["write_cmds"]))
        check(s["by_opcode"].get("28", 0) >= 4 and s["by_opcode"].get("2a", 0) >= 2,
              "READ(10) and WRITE(10) are in the opcode counts", s["by_opcode"])

        # the card itself: the core's bidirectional test (SPEC.md 138.2)
        h.dtr(0)
        h.run([("o", 2, 0x20), ("o", 0, 0x55)])
        a = h.run([("i", 0)])[0]
        h.run([("o", 0, 0xAA)])
        b = h.run([("i", 0)])[0]
        h.run([("o", 2, 0x0C)])
        if ps2:
            check((a, b) != (0x55, 0xAA), "a bidirectional card does NOT read back its latch", (a, b))
        else:
            check((a, b) == (0x55, 0xAA), "an output-only card reads back its latch", (a, b))


def noimage():
    print("\n== a drive with no disk")
    with M.launch(BOOT, machine="os8088_5150_cga_zip0", boot=False) as m:
        h = zipq.ZipHost(m, False)
        s = m.zip_stats()
        check(not s["medium"], "no OS88_ZIPIMG: the drive is empty")
        h.command([0, 0, 0, 0, 0, 0])               # eats the power-on attention
        st, _ = h.command([0, 0, 0, 0, 0, 0])
        check(st == zipq.CHECK and h.sense()[:2] == (zipq.SK_NOT_READY, 0x3A),
              "an empty drive is NOT READY, medium not present")
        r = m.zip_insert(zipq.qa_image("zip100super.img"))
        check(r["sectors"] == 196608, "zip_insert puts a disk in", r)
    print("\n== no drive on the port: the model is not in the way")
    with M.launch(BOOT, machine="os8088_5150_cga_lpt", boot=False) as m:
        try:
            m.zip_stats()
            check(False, "a machine with no [machine.zip] answers zip_stats")
        except M.MartyError:
            check(True, "a machine with no [machine.zip] has no drive to ask about")


def main():
    for p in ("zip100.img", "zip100b.img", "zip250.img", "zip100super.img"):
        if not os.path.exists(zipq.qa_image(p)):
            sys.exit("zipmodel: %s is missing - `make zipimg` builds it" % zipq.qa_image(p))
    tmp = tempfile.mkdtemp(prefix="zipmodel")
    try:
        for a in ARMS:
            arm(*a, tmp=tmp)
        noimage()
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        sys.exit("zipmodel FAILED: %d check(s): %s" % (len(fails), "; ".join(fails[:5])))
    print("\nzipmodel: all checks passed")


if __name__ == "__main__":
    main()
