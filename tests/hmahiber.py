#!/usr/bin/env python3
"""hmahiber - hibernate carries the HMA's driver pool (SPEC.md 87.4 step 3b,
87.6 step 0b, 2.10.8; docs/plans/HMA-PLAN.md 5.4).

    make && python3 tests/hmahiber.py

On a 286 or better with `.cold` in the HMA, HDD.DRV and NET.DRV run from the
pool under it (tests/hmadrv.py). The image is conventional memory, so a
driver up there used to be swept like a chip owner whatever its class - and
on a FLOPPY boot whose only C: is HDD.DRV's, that took down the volume the
image was about to be written to, and the hibernate refused. Now the module
copies the tail's block table and every live block into a MEM_K_HIB claim the
image carries, and the wake copies them back to the same bases before the
`sti`.

WHY QEMU: docs/TESTING.md's closed list, entry 1 - above 1MB is a 286-and-up
place and every MartyPC machine is an 8088. Nothing here is timed.

hmadrv's machine and disks: the shipped 1.44MB system disk plus a SYSTEM.CFG
wanting SOUND.DRV, HDD.DRV and NET.DRV, an SB16, and an IDE disk holding a
FAT16 partition and HELLO.TXT, booted from the FLOPPY - so the hibernation
file is on HDD.DRV's own volume.

  A. -m 128. Hibernate... from the System menu, Enter: the machine reaches
     text mode and both files reach C: (read on the HOST). QEMU is killed -
     the power-off - and a fresh QEMU boots the same disks and answers the
     question with Enter. Then, out of the guest:
     1. [hb_resumes] is 1, [sch_lock] is 0;
     2. HDD.DRV's and NET.DRV's rows name the SAME HMA segments as before
        the hibernate, and the tail's hb_tab holds the same records for them
        (base, bytes, owner);
     3. each of the two is still its org-DRV_HMA_ORG assembly where nothing
        writes (tests/hmadrv.py's comparison) - the copy came back up;
     4. SOUND.DRV, swept and reloaded, is in the pool again;
     5. no MEM_K_HIB claim is left - the copy's and the extent list's both
        given back;
     6. a Disk window on C: lists HELLO.TXT - HDD.DRV serves from the
        restored pool;
     7. both files are gone from C:, read on the host.
  B. -m 1, the NEGATIVE CONTROL (docs/WRITING-TESTS.md 1): copies of the
     same disks taken while the image was on them, booted with no HMA, so
     `.cold` stays in RAM - the pointer's pad byte refuses it before anything
     is torn down: `Hibernation: other build`, [hb_resumes] 0, and the
     pointer deleted.

Without the fix the row goes red at A's first step: the sweep unloads
HDD.DRV and the write has no volume (`Hibernate failed`, no text mode). With
step 0b compiled out and the old HRESET in its place, the image's HDD.DRV
row names an empty segment and the wake's own first disk call (step 5's
delete) runs into it: [hb_resumes] never reaches 1.
"""
import os
import shutil
import struct
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88geom    # noqa: E402
import os88qemu    # noqa: E402
import os88sym     # noqa: E402
import hmacold     # noqa: E402
import hmadrv      # noqa: E402
import xmcheck     # noqa: E402

S = os88sym.linear
EQ = os88sym.equates(())
B = os.path.join(ROOT, "build")
ORG = EQ["DRV_HMA_ORG"]
MEM_K_HIB = EQ["MEM_K_HIB"]
HB_M_RESUME, HB_M_GONE = 2, 3           # kernel/hiber.inc
KEPT = ("HDD", "NET")                   # the kept classes the pool holds
u16 = hmadrv.u16


def byte(q, name):
    return q.read(S(name), 1)[0]


def word(q, name):
    return u16(q.read(S(name), 2))


def on_c(hd):
    """The root of C:, read on the HOST by a FAT reader that is not the
    kernel's. instdeep prints the slot it found; that line is noise here."""
    import contextlib
    import io
    import instdeep
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            return set(instdeep.partition(hd).tree())
    except SystemExit:
        return set()


def host_wait(cond, secs=20.0):
    """A HOST wait for a HOST thing: the guest's int 13h has returned and the
    sectors are in QEMU's hands, which is not when the file has them."""
    end = time.time() + secs
    while not cond():
        if time.time() >= end:
            return cond()
        time.sleep(0.3)
    return True


def claims(q):
    """Every live mem_tab record as (base, paragraphs, owner)."""
    n = 32
    raw = q.read(S("mem_tab"), n * os88geom.MC_SIZE)
    out = []
    for i in range(n):
        rec = struct.unpack_from("<HHH", raw, i * os88geom.MC_SIZE)
        if rec[0]:
            out.append(rec)
    return out


def main():
    import argparse
    argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter).parse_args()
    os.chdir(ROOT)
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
        if not cond:
            fails.append(what)

    for need in ("os8088.img", "coldhma.sys", "recorder.o88", "mbr.bin",
                 "boothd.bin", "kernel.sys", "hdd.hma.bin", "net.hma.bin"):
        if not os.path.exists(os.path.join(B, need)):
            sys.exit("hmahiber: no build/%s - run `make`" % need)
    with tempfile.TemporaryDirectory(dir=B) as tmp:
        sysimg, apps, hd = hmadrv.disks(tmp)

        print("A: -m 128, SOUND + HDD + NET wanted, a floppy boot, C: is "
              "HDD.DRV's")
        q = hmadrv.boot(tmp, 128, sysimg, apps, hd)
        if u16(q.read(S("api_coldseg"), 2)) != 0xFFFF:
            sys.exit("hmahiber: `.cold` is not in the HMA - tests/hmacold.py's "
                     "premise, and nothing below means anything without it")
        r0 = hmadrv.rows(q)
        b0 = hmadrv.blocks(q)
        for name in ("SOUND",) + KEPT:
            want(0xF200 <= r0[name]["seg"] < 0xF800 and r0[name]["kb"] == 0,
                 "before: %s.DRV runs from the pool at %04X"
                 % (name, r0[name]["seg"]))
        print("   the pool before: %s" % ["%04X+%d/%04X" % b for b in b0])
        ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
        ui.menu_pick("Apple", "Hibernate...")
        ui.wait_window("Hibernate")             # HB_M_ASK, the module loaded
        os88qemu.pace(q, 1)
        xmcheck.qmp(q.sock, "sendkey ret")
        text = os88qemu.acted(q, lambda: q.read(0x449, 1)[0] in (3, 7),
                              secs=120, what="the hibernated sentence")
        if not text:
            want(False, "the hibernate reached text mode - toast %r; the "
                 "sweep taking HDD.DRV down is `Hibernate failed`, the pool's "
                 "claim refused is `Not enough memory`" % (ui.toast(),))
            q.close()
            sys.exit("hmahiber: %d failed" % len(fails))
        both = {"HIBERNAT.PTR", "HIBERNAT.IMG"}
        want(host_wait(lambda: both <= on_c(hd)),
             "the hibernate reached text mode and both files are on C: (the "
             "positive control for B and for A7)")
        q.close()                                   # the power-off
        sys2 = os.path.join(tmp, "sys-neg.img")
        hd2 = os.path.join(tmp, "hd-neg.img")
        shutil.copy(sysimg, sys2)
        shutil.copy(hd, hd2)

        q = hmadrv.boot(tmp, 128, sysimg, apps, hd)
        if not os88qemu.acted(q, lambda: byte(q, "hb_mode") == HB_M_RESUME,
                              secs=60, what="the resume question"):
            want(False, "the fresh boot asked the question (hb_mode %d, "
                 "toast %r)" % (byte(q, "hb_mode"),
                                hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S,
                                            verbose=False).toast()))
        else:
            os88qemu.pace(q, 1)
            xmcheck.qmp(q.sock, "sendkey ret")      # Resume
            ok = os88qemu.acted(q, lambda: word(q, "hb_resumes") == 1,
                                secs=120, what="[hb_resumes] to reach 1")
            want(ok, "A1. the machine resumed: [hb_resumes] %d"
                 % word(q, "hb_resumes"))
            if ok:
                os88qemu.acted(q, lambda: byte(q, "sch_lock") == 0, secs=20,
                               what="the wake's last step")
                os88qemu.pace(q, 1)
                want(byte(q, "sch_lock") == 0, "    ...and [sch_lock] is down")
                r1 = hmadrv.rows(q)
                b1 = hmadrv.blocks(q)
                print("   the pool after:  %s"
                      % ["%04X+%d/%04X" % b for b in b1])
                for name in KEPT:
                    seg = r0[name]["seg"]
                    rec0 = [b for b in b0 if b[2] == seg]
                    rec1 = [b for b in b1 if b[2] == seg]
                    want(r1[name]["seg"] == seg and r1[name]["kb"] == 0
                         and rec0 and rec1 == rec0,
                         "A2. %s.DRV is the image's again: row %04X (was "
                         "%04X), its records %s (were %s)"
                         % (name, r1[name]["seg"], seg,
                            ["%04X+%d" % b[:2] for b in rec1],
                            ["%04X+%d" % b[:2] for b in rec0]))
                    img = hmadrv.hma_image(name.lower())
                    got = q.read((seg << 4) + ORG, len(img))
                    ent = r1[name]["ent"] - ORG
                    want(got[:32] == img[:32]
                         and got[ent:ent + 64] == img[ent:ent + 64],
                         "A3. ...and the pool at %04X:%04X holds its "
                         "org-%04X code" % (seg, ORG, ORG))
                snd = r1["SOUND"]
                want(0xF200 <= snd["seg"] < 0xF800 and snd["kb"] == 0
                     and any(b[2] == snd["seg"] for b in b1),
                     "A4. SOUND.DRV, swept and reloaded, is in the pool at "
                     "%04X" % snd["seg"])
                hib = [c for c in claims(q) if c[2] == MEM_K_HIB]
                want(not hib, "A5. no MEM_K_HIB claim is left: %s"
                     % ["%04X+%dp" % c[:2] for c in hib])
                ok, names, _ui = hmadrv.c_lists(q, "HELLO.TXT")
                want(ok, "A6. C: lists HELLO.TXT through the restored "
                     "HDD.DRV: %s" % names)
        q.close()
        want(host_wait(lambda: not (both & on_c(hd))),
             "A7. both files are gone from C: after the resume: %s"
             % sorted(both & on_c(hd)))

        print("B: -m 1, copies of the disks with the image on them: no HMA")
        q = hmadrv.boot(tmp, 1, sys2, apps, hd2)
        want(u16(q.read(S("api_coldseg"), 2)) != 0xFFFF,
             "`.cold` stays in RAM (the control's premise)")
        ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
        said = os88qemu.acted(q, lambda: "other build" in ui.toast()[0],
                              secs=60, what="the refusal")
        want(said and byte(q, "hb_mode") == HB_M_GONE
             and word(q, "hb_resumes") == 0,
             "the image is refused before the teardown: toast %r, hb_mode "
             "%d, [hb_resumes] %d" % (ui.toast()[0], byte(q, "hb_mode"),
                                     word(q, "hb_resumes")))
        q.close()
        want(host_wait(lambda: "HIBERNAT.PTR" not in on_c(hd2)),
             "...and the pointer is deleted")

    if fails:
        sys.exit("hmahiber: %d failed" % len(fails))
    print("hmahiber: ok")


if __name__ == "__main__":
    main()
