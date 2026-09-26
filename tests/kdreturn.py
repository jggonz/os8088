#!/usr/bin/env python3
"""The DOS handoff comes BACK (SPEC.md 96.41, docs/plans/KERN-DOS-PLAN.md 8).

    python3 tests/kdreturn.py                 # booted off the fixed disk
    python3 tests/kdreturn.py --boot floppy   # ...and off a FLOPPY, SPEC.md 96.46.1

W5 gave a DOS program the whole machine and restarted when it exited, because
there was nothing to return to.  On a machine with a fixed disk there is: the
kernel writes a hibernation image before the handoff, `kern_dos` restarts as it
always did, and the fresh boot finds the pointer and resumes it **without
asking** - because this was not the user leaving the machine.

WHAT IT ASSERTS, in the order the machine does it:

  1 the program ran with the whole machine, which is the KB figure W5 gates
  2 the desktop came back BY ITSELF - no Resume window, no question. A
    hibernation the user did not ask for and then has to answer for is worse
    than no return at all
  3 the DOS window is up with the program's own exit code in it, which is the
    whole point: the session is the one that left
  4 the mailbox at 0040:00F0 is CLEARED, so the next resume cannot pick up a
    code from this one
  5 THE CLOCK IS STILL A CLOCK, and it has moved FORWARD (SPEC.md 96.49.5).
    `kd_resume` staged the six time fields out of `clk_sn_*`, filled by a
    `cw_clk_snapshot` that is a bare `retf` - so the session came home at
    00:00:00 on 00/00/0000 and a month of zero indexes `clk_mnames` three
    bytes before the table. It is asserted as a RANGE and a DIRECTION rather
    than a value: the guest's own clock is what the row compares against,
    which needs no wall clock and no RTC on the machine

**THREE ARMS SWAP THE PROGRAM FOR ONE THAT LEAVES THE MACHINE AS A REAL DOS
PROGRAM LEAVES IT**, and each is a different question.  All three are DOSHELLO
built with one define, because what they change is one instruction and a
copied source is a source that drifts:

  `--mode`   `-DMODESET=2`, the colour TEXT mode `DIGIRAIN.COM` sets.  The
             BDA's video mode byte is then 2 rather than 7, so `kd_stageseg`
             answers B800 where `hbm_wake` will read `[vid_kind]` (SPEC.md
             96.49.6).  **Only a question on a machine whose BIOS will accept
             it** - GLaBIOS on a mono-only 5150 forces 7 back, so the arm is
             carried for the config that does not, and the segment is carried
             rather than derived either way.
  `--gfx`    `-DMODESET=0x13`, and this is the one with teeth: in mode 13h a
             VGA decodes A000 ALONE, so B800 is open bus - the stub is
             `rep movsb`'d into nothing and the far jump into it is the end of
             the machine.  Run it on `os8088_xt_vga_hdd`.
  `--pit`    the program takes PIT channel 0 for its own timer and exits
             without giving it back, which is what a DOS game does.  What puts
             it back is `dos_restore_machine`, in the SHARED core, and nothing
             asserted that line (SPEC.md 96.5).

IT NEEDS A HARD DISK: `hb_pick` is the predicate on both sides, so a
floppy-only machine takes W5's arm and this row would be asserting nothing.
tests/hibernate.py's fixture, with the parted DOS.O88 and a DOS program in the
volume's root.

**`--boot floppy` IS THE SAME MACHINE BOOTED THE OTHER WAY, and it is a
different question** (SPEC.md 96.46.1).  Which volume `HIBERNAT.IMG` lands on
is `hb_pick`'s: the one the machine booted from when that is fixed, else the
FIRST FIXED VOLUME THERE IS.  Boot off a floppy and that second arm runs, and
the volume it names is driver-backed - `dsk_boot_from_x` adds a `DVK_BIOS`
partition row only on its hard-disk arm.  The launch-block gather wrote
`DVK_FREE` for a `DVK_DRV` row, so `kd_resume` mounted an index that named no
volume, refused, and `kd_leave` fell back to `int 19h`: a whole POST, a whole
boot and a restore at the desktop.  The field reported exactly that and this
row is what would have caught it - the LIVE_MAX cycle bound below goes red on
the fallback, which is the whole reason it is a bound and not a screen read.

The fixture is the SHIPPED 360KB system disk with one file added: a SYSTEM.CFG
asking for the hard-disk driver.  Nothing loads unless SYSTEM.CFG asks (SPEC.md
51.3), so without it the machine has no C:, `hb_pick` refuses, and the row would
be asserting §96.42's arm instead.  Bit 1 is the hard disk's and is not its row
number (`drv_cfgbit`) - it is only coincidentally both.
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88geom                                                # noqa: E402
import os88sym                                                 # noqa: E402
import os88marty as M                                          # noqa: E402
import os88mouse                                               # noqa: E402
import os88build                                               # noqa: E402
import os88ui                                                  # noqa: E402

# --tree "<make args>" builds a PRIVATE tree and drives that instead, so a knob
# kernel never lands in build/ (tools/os88build.py). The one it exists for is
# DOSRMARK=1, whose trace is the only way to watch a resume from outside.
# The targets are this row's own `wants=`: a tree is cut to what it is asked
# for, so a missing goal here is a FileNotFoundError naming a private tree.
_TREE_TARGETS = ("kdos/DOS.O88", "DOSHELLO.COM", "DOSMODE.COM", "DOSGFX.COM",
                 "DOSPIT.COM", "kernel.sys",
                 "boothd.bin", "mbr.bin", "hiber.drv", "ctrl.drv", "hdd.drv",
                 "os8088-360.img", "apps360.img")
if "--tree" in sys.argv:
    os88build.tree(*sys.argv[sys.argv.index("--tree") + 1].split(),
                   targets=_TREE_TARGETS).apply()

# THE TEMPLATE IS THE BUILT TREE'S, not tools/martypc/ - that directory holds
# the SOURCE patches and the ROMs, and `make marty` stages the run tree under
# build/. tests/hibernate.py names the same file.
TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd"
KERNEL = os88build.at("build/kernel.sys")
# **ABSOLUTE, BECAUSE MARTYPC RESOLVES A PATH AGAINST ITS OWN RUN TREE**
# (tools/os88marty.py's _private_run_dir): a relative one names a file that is
# not there and the machine boots to a loading screen and stays on it.
VHD = os.path.abspath("build/kdreturn-%d.vhd" % os.getpid())
FLOPPY = os.path.abspath("build/kdreturn-%d.img" % os.getpid())
# ...and the BOOT floppy, which only the --boot floppy arm builds
SYSIMG = os.path.abspath("build/kdreturn-sys-%d.img" % os.getpid())
SHIPSYS = os88build.at("build/os8088-360.img")
HDD_CFGBIT = 1                  # kernel/driver.inc's drv_cfgbit, row 1

KDB = 0x4F0                     # 0040:00F0, the exit code's mailbox


def fail(msg):
    print("kdreturn: FAIL: %s" % msg)
    sys.exit(1)


def rows(m):
    return [r.rstrip() for r in (m.screen() or [])]


def fixture(prog="DOSHELLO.COM"):
    """...and the DEFAULT is the plain program, because this is IMPORTED.

    `tests/mouresume.py` builds this row's fixture and drives its own session
    on top of it, so the argument the arms below need must not become a
    required one - making it so failed that row with a TypeError, in 0.1s,
    naming a line in a file its author never touched.
    """
    for p in (TEMPLATE, KERNEL, os88build.at("build/kdos/DOS.O88"),
              os88build.at("build/" + prog)):
        if not os.path.exists(p):
            fail("%s is missing - `make kdostest` builds the DOS pieces and "
                 "`make marty` the template" % p)
    subprocess.check_call(
        ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out", VHD,
         "--kernel", KERNEL, "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HIBER.DRV=" + os88build.at("build/hiber.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "DOS.O88=" + os88build.at("build/kdos/DOS.O88")])
    # ...AND THE PROGRAM ON A FLOPPY, which is still not an accident of the
    # fixture though the reason has changed. It USED to be that `kern_dos`
    # had no volume table and a fixed disk was a geometry it had not got -
    # SPEC.md 96.46 carries the kernel's table over now and `tests/kdhdd.py`
    # is that row. What W6 is about is the RETURN, and keeping the program on
    # a floppy keeps this row about the return rather than about the mount.
    subprocess.check_call(
        ["python3", "tools/os88disk.py", "-o", FLOPPY, "--size", "360",
         os88build.at("build/" + prog)])


def sysfloppy():
    """The shipped 360KB system disk plus a SYSTEM.CFG that wants the disk driver.

    Copied rather than rebuilt: what this arm is about is a machine booting the
    disk the field boots, and a system floppy assembled here out of the same
    parts would be a different artefact with the same contents - so a change to
    what `all` writes would stop being tested exactly when it mattered.
    """
    if not os.path.exists(SHIPSYS):
        fail("%s is missing - `make` builds the shipped system disks" % SHIPSYS)
    with open(SHIPSYS, "rb") as f:
        raw = f.read()
    with open(SYSIMG, "wb") as f:
        f.write(raw)
    # SPEC.md 51.5's container in eighteen bytes: the signature, the
    # generation, one DW record and the terminator. Every other key ABSENT, so
    # the reader answers each with its default (rule 3) - writing zeros for the
    # video mode would be selecting a setting this row has no opinion about.
    cfg = os.path.abspath("build/kdreturn-cfg-%d.bin" % os.getpid())
    with open(cfg, "wb") as f:
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                + b"DW" + bytes([1, 2])
                + (1 << HDD_CFGBIT).to_bytes(2, "little") + b"\0\0")
    subprocess.check_call(["python3", "tools/os88fat.py", "add",
                           SYSIMG, cfg, "SYSTEM.CFG"])
    os.unlink(cfg)


def claims(m):
    """Every live `mem_tab` record as (base, paragraphs, owner, dma, rloc).

    The table is in `.lowbss`, so it is LOW_SEG-relative and `os88sym.linear`
    is what resolves it - the same read `tools/heapmap.py` makes, done here
    rather than imported because this row wants one question answered and not
    a timeline.
    """
    raw = bytes(m.read(os88sym.linear("mem_tab"), 32 * os88geom.MC_SIZE))
    out = []
    for i in range(32):
        rec = struct.unpack_from("<HHHHH", raw, i * os88geom.MC_SIZE)
        if rec[0]:
            out.append(rec)
    return out


def wait_text(m, want, secs, what):
    got = []

    def seen(mm):
        rs = rows(mm)
        if any(want in r for r in rs):
            got.append(rs)
            return True
        return False
    try:                        # `secs` is budgeted on the GUEST's clock
        M.until(m, seen, "%s: %r" % (what, want), poll=0.25, limit=secs)
        return got[-1]
    except M.MartyError:
        pass
    fail("%s: %r never reached the text screen; the last one held %r"
         % (what, want, [r for r in rows(m) if r.strip()][:10]))


def topmem(rs):
    for r in rs:
        if "Memory to top of block:" in r:
            return int(r.split(":")[1].strip().split()[0])
    fail("the program printed no top-of-memory figure: %r"
         % [r for r in rs if r.strip()][:8])


def wait_desktop(m, ui, secs=300, stamp=None):
    """Wait for the RESTARTED machine to reach a graphics desktop.

    **`ui.up()` IS NOT THE WAIT HERE**, and neither is poking its two words
    first. A warm boot clears no bss (kernel/hiber.inc's `hb_probe` says so
    about its own three), so `desk_rows` and `menu_nbar` hold whatever is at
    those offsets - which between the handoff and the boot is `kern_dos`'s
    own image, so `up()` returns on the BIOS banner and everything after it
    reads a machine that has not booted. Zeroing them first is worse: at that
    moment those addresses ARE kern_dos's running code.

    The mode is the honest question. os8088's desktop is GRAPHICS on every
    adapter it has (SPEC.md 39) and everything between - the ROM's banner,
    `kern_dos`, the loading screen's own text - is not.

    **ASK IT WITH `video_is_text` AND NOT WITH THE `mode` STRING**, because on
    the MDA/Hercules that string is a DEAD FIELD: os8088 puts the card into HGC
    graphics through 3BF/3B8 rather than through int 10h, so `display_mode()`
    still reads `Mode0TextBw40` on a desktop while `graphics` correctly says
    true (tools/os88marty.py `video_is_text`, which knows which field is live
    per card). A `"Graphics" in mode` test therefore can NEVER pass on a mono
    machine, whatever the kernel did - so this row would have gone red on
    os8088_5150_herc_hdd_gla for a reason that is not about the resume at all,
    and the screen it printed would have been a graphics desktop decoded as
    text, which reads exactly like a crash.
    """
    def graphics(mm):
        try:
            return not M.video_is_text(mm.video() or {})
        except Exception:
            return False
    try:                        # `secs` is budgeted on the GUEST's clock
        M.until(m, graphics, "a graphics desktop", poll=0.2, limit=secs)
        if stamp is not None:
            stamp.append(m.status()["cycles"])
        return ui.ready(limit=secs)
    except Exception:           # ...as the host loop swallowed them
        pass
    # EVERY non-blank row, NUMBERED. A failure here is always "the machine
    # stopped with something on the glass", so which ROW a thing is on is half
    # the evidence - the resume stub is staged at OFFSET 0 of the text page, so
    # rows 0-2 being stub bytes says the copy landed, and row 7 is DOSRMARK=1's
    # trace (SPEC.md 96.49.3). The old message kept the last four and threw the
    # rest away, which lost both.
    raise RuntimeError(
        "no graphics desktop in %ds; the text screen holds:\n%s"
        % (secs, "\n".join("  %2d| %s" % (i, r)
                            for i, r in enumerate(rows(m)) if r.strip())))


def clock(m):
    """The live `clk_sec`..`clk_year` block, as (h, m, s, day, mon, year).

    Nine bytes and not ten: `clk_h12` is the tenth and is a display toggle.
    Read off the guest rather than compared with the host's wall clock - the
    machine has no RTC on most of these profiles (SPEC.md 37.90), so its own
    clock is the only authority for what its own clock should say.
    """
    raw = bytes(m.read(os88sym.linear("clk_sec"), 9))
    return (raw[2], raw[1], raw[0], raw[3], raw[4], raw[7] | (raw[8] << 8))


def secs(c):
    return c[0] * 3600 + c[1] * 60 + c[2]


def guest_ticks(m):
    return int.from_bytes(bytes(m.read(os88sym.linear("ticks"), 2)), "little")


def main():
    from_floppy = "--boot" in sys.argv and "floppy" in sys.argv
    # --mode swaps the program for DOSHELLO built -DMODESET, which sets BIOS
    # mode 2 first. See the header: it is only a question on a MONO machine.
    prog = "DOSHELLO.COM"
    if "--mode" in sys.argv:
        prog = "DOSMODE.COM"
    elif "--gfx" in sys.argv:
        prog = "DOSGFX.COM"
    elif "--pit" in sys.argv:
        prog = "DOSPIT.COM"
    fixture(prog)
    boot = None
    if from_floppy:
        sysfloppy()
        boot = SYSIMG
    # --machine points this at another 8088 with a fixed disk on it. The one
    # that matters is os8088_5150_herc_hdd_gla, whose staging area is at B000
    # rather than B800 (SPEC.md 87.5) - the mono half of the resume, which no
    # machine in this tree could host until that config existed.
    machine = MACHINE
    if "--machine" in sys.argv:
        machine = sys.argv[sys.argv.index("--machine") + 1]
    m = M.launch(boot, apps=FLOPPY, machine=machine,
                 extra=["--mount", "hd:0:" + VHD])
    try:
        ui = os88ui.UI(m)
        ui.ready(limit=240)
        print("kdreturn: booted off %s"
              % ("a 360KB floppy, with the fixed disk on a DRIVER"
                 if from_floppy else "the fixed disk"))
        if from_floppy:
            # **THE HARD DISK HAS TO BE THERE, or this row quietly becomes
            # §96.42's** - the one where there is nothing to come back to, the
            # box asks first, and every assertion below about the return is
            # about a machine that never left. The driver publishes its volume
            # at attach, so a live row past B: is the fact to read.
            G = os88geom
            raw = bytes(m.read(os88sym.linear("dsk_vtab"),
                               G.DVOL_MAX * G.DV_SIZE))
            kinds = [raw[v * G.DV_SIZE + G.DV_KIND] for v in range(G.DVOL_MAX)]
            if all(k == G.DVK_FREE for k in kinds[2:]):
                fail("no volume past B: - HDD.DRV did not attach, so there is "
                     "no fixed disk for hb_pick to choose and this row would "
                     "assert nothing. Kinds %r" % (kinds,))
            if G.DVK_DRV not in kinds:
                fail("the fixed disk is not DRIVER-backed (kinds %r), so this "
                     "arm is testing the same thing the fixed-disk one does - "
                     "SPEC.md 96.46.1 is about a DVK_DRV row crossing into the "
                     "launch block" % (kinds,))
            print("kdreturn: volume kinds %r - the fixed disk is on a driver"
                  % (kinds,))

        before = clock(m)
        print("kdreturn: the guest's clock reads %02d:%02d:%02d %02d/%02d/%d"
              % (before[0], before[1], before[2], before[3], before[4],
                 before[5]))
        win = ui.path("B:/" + prog)
        if not win:
            fail("double-clicking %s on the floppy opened no window" % prog)
        rs = wait_text(m, "READY", 150, "the windowed run")
        win_kb = topmem(rs)
        m.type_text("x")
        M.settle(m)
        print("kdreturn: windowed, %d KB above the PSP" % win_kb)

        dm = dosmap.package()
        pseg = dosmap.instance(m)
        mo = os88mouse.Mouse(marty=m)
        # **THE WINDOWED LAUNCH ABOVE LEFT ITS ANSWER IN THESE THREE CELLS**,
        # and they are the ones the return is asserted on. DOSHELLO exits 42
        # either way and DST_RAN is DST_RAN, so a return that poked nothing at
        # all passed every check below for as long as this row has existed -
        # which is how SPEC.md 96.41.3 stayed invisible on the very adapter
        # this row boots. Zero them, and anything found afterwards is the
        # return's (docs/WRITING-TESTS.md 1).
        m.write((pseg << 4) + dm["dos_exit"], bytes([0]))
        m.write((pseg << 4) + dm["dos_akb"], bytes([0, 0]))
        m.write((pseg << 4) + dm["dos_state"], bytes([0]))
        m.write((pseg << 4) + dm["dos_keepc"], bytes([1, 0]))
        mo.click(*dosmap.centre(m, pseg, dm, "dos_rrect"))

        rs = wait_text(m, "READY", 300, "the run under kern_dos")
        kd_kb = topmem(rs)
        if kd_kb <= win_kb:
            fail("%d KB under kern_dos against %d in the window - the handoff "
                 "gave the program no more memory" % (kd_kb, win_kb))
        print("kdreturn: under kern_dos, %d KB - the handoff is worth %d"
              % (kd_kb, kd_kb - win_kb))

        # --- ...AND IT COMES BACK WITHOUT A BOOT (SPEC.md 96.49) -------------
        # W6's route printed `the program has exited, code 42` and waited for
        # a key before `int 19h`. There is no reboot now: `kd_leave` puts the
        # session back itself, and the line below is the POSITIVE signal that
        # it did - asserting the ABSENCE of the reboot's own line would be
        # absence of evidence, and both routes end at the same desktop.
        # **THE ROUTE IS MEASURED IN GUEST CYCLES, not read off the screen.**
        # `kd_resume` does print `putting the session back` - a screen that
        # changes by itself is a crash and one that says why is not - but it
        # prints INTO THE STAGING AREA, which is the visible text page: the
        # stub and the extent list overwrite it microseconds later, so no
        # poll can be relied on to catch it. Cycles are exact at any
        # emulator speed (CLAUDE.md, Testing) and they measure the thing the
        # feature is FOR: W6's route is a POST plus a whole boot plus the
        # restore, and this one is the restore alone.
        c0 = m.status()["cycles"]
        m.type_text("x")                        # exit with code 42

        # --- 2. IT COMES BACK BY ITSELF ---------------------------------
        # **AND THE STAMP IS THE FIRST GRAPHICS FRAME**, not the settled
        # desktop: `wait_desktop` polls at a whole second and then waits for
        # the desktop to be READY, and timing that measures the harness. The
        # mode is the honest edge - everything between here and it is text
        # (that routine's own reason).
        c1 = []
        try:
            wait_desktop(m, ui, stamp=c1)
        except Exception as e:
            fail("the machine never came back after the return: %s" % e)
        spent = c1[0] - c0
        # 4.77 MHz, so a second is 4,772,727 cycles. A boot alone is ~2,087 ms
        # (docs/plans/completed/BOOT-PERF-PLAN.md) and the POST in front of it
        # is seconds more, against a restore of ~2 s - so this bound is wide
        # enough to be about the ROUTE and not about either route's speed.
        LIVE_MAX = 15 * 4772727
        print("kdreturn: back in %.2f guest seconds (%d cycles)"
              % (spent / 4772727.0, spent))
        if spent > LIVE_MAX:
            fail("the return took %.1f guest seconds, which is a POST and a "
                 "BOOT: the live resume refused and kd_leave fell back to "
                 "int 19h (SPEC.md 96.49). That fallback is deliberate and "
                 "works, so this is about the fast path not being taken - "
                 "the mount, the image, the geometry or the extents"
                 % (spent / 4772727.0))
        titles = ui.titles()
        print("kdreturn: back on a desktop, titles %r" % (titles,))
        if "Hibernate" in titles:
            fail("the machine asked. An image written for a DOS handoff is "
                 "not the user leaving the machine, so HBP_DOS should have "
                 "turned UI_RBQ_ASK into UI_RBQ_RESUME "
                 "(docs/plans/KERN-DOS-PLAN.md 8)")
        if "DOS" not in titles:
            fail("the DOS window is not up: the session that came back is not "
                 "the one that left - titles %r" % (titles,))

        # --- 3. ...with the program's own exit code ---------------------
        pseg = dosmap.instance(m)
        st = m.read((pseg << 4) + dm["dos_state"], 1)[0]
        code = m.read((pseg << 4) + dm["dos_exit"], 1)[0]
        if st != 2:                             # DST_RAN
            fail("[dos_state] is %d and not DST_RAN: the box was woken and "
                 "did not read KDH_CODE (SPEC.md 96.41)" % st)
        if code != 42:
            fail("[dos_exit] is %d and DOSHELLO exits with 42 - the code came "
                 "back wrong, which is worse than not coming back" % code)
        print("kdreturn: the box shows DST_RAN, exit code %d" % code)

        # --- 4. and the mailbox is spent --------------------------------
        box = m.read(KDB, 6)
        if box[:4] == b"KDX1":
            fail("0040:00F0 still holds a live record %r - the next resume "
                 "would attach this program's code to another run" % box)
        print("kdreturn: the mailbox is cleared (%s)" % box[:4].hex())

        # --- 5. ...AND THE PICTURE'S OWN EXTENT LIST IS GONE (87.6.1) ----
        # The handoff claims the list at step 3 and writes the image at step
        # 3b, so HIBERNAT.IMG is a picture of a machine with a live MEM_K_HIB
        # claim in its `mem_tab`. Restore that and the claim comes back over
        # an extent list nothing will read again: a HELD block, PINNED because
        # nothing declared it, sitting wherever the writing machine's heap put
        # it - reported from the field as `Resume 33C0 4K HELD` on the Task
        # Manager's page, and worth 4KB of every DOS arena for the rest of the
        # session.
        #
        # It is asserted HERE because this row already pays for the round
        # trip, and there is no cheaper way to reach the state: it takes a
        # whole-machine run and a return to create one claim.
        #
        # VERIFIED TO FAIL: with `hbm_wake`'s free taken out, this reads
        # `2KB at 26C0` on this fixture (the list is sized for the volume, so
        # the field's 128MB disk gives 4KB) and the largest free run after the
        # return is 393KB against 400.
        hib = [r for r in claims(m) if r[2] == 0xFF0C]
        if hib:
            fail("MEM_K_HIB is still claimed after the return - %dKB at "
                 "%04X, rloc=%04X. That is the resume's EXTENT LIST, which "
                 "the image was written over the top of (SPEC.md 87.6.1): it "
                 "describes the machine that took the picture and is dead in "
                 "the one that reads it, and nothing declared it movable so "
                 "it is a wall as well as a leak"
                 % (hib[0][1] // 64, hib[0][0], hib[0][4]))
        print("kdreturn: no MEM_K_HIB claim survived the restore")

        # --- 6. ...AND THE CONSOLE SAYS IT ONCE, WITH THE RIGHT ARENA -------
        # Two defects met on this one line and both were reported from the
        # field (SPEC.md 96.41.1, 96.35.1):
        #
        #   the LINE. `dos_run`'s arm-3 handoff fell into `.out`, which runs
        #   `dos_con_ended` - so a SUCCESSFUL post logged `ended, exit code
        #   000` before the machine had been handed over, about a program that
        #   had not started. It reads as the box loading something in order to
        #   exit. The post leaves by `.outq` now and the line is written HERE,
        #   on the wake that says the run finished;
        #
        #   the ARENA. `[dos_akb]` is `dos_run`'s banked figure and on this arm
        #   `dos_run` never claimed, so what stood there was the WINDOWED
        #   launch's number - 422KB against the 579 the program really had.
        #   That is a wrong answer rather than a missing one, which is why the
        #   assertion is `>= kd_kb` and not `!= 0`.
        #
        # The last such line is the one this run wrote; the windowed launch
        # above left one too, and that one is still correct.
        scr = bytes(m.read((pseg << 4) + dm["con_scr"], dm["CON_SCRSZ"]))
        text = [bytes(scr[i:i + 2 * dm["CON_COLS"]:2]).decode("latin-1").rstrip()
                for i in range(0, dm["CON_SCRSZ"], 2 * dm["CON_COLS"])]
        ended = [r for r in text if "ended, exit code" in r]
        if not ended:
            fail("the console logged nothing about the run that just came "
                 "back (SPEC.md 96.41.1): %r" % ([r for r in text if r][-6:],))
        last = ended[-1]
        if "042" not in last:
            fail("the console's last exit line is %r - DOSHELLO exits with 42, "
                 "so a 000 there is dos_run's arm-3 post logging an END before "
                 "the machine was handed over (SPEC.md 96.35.1)" % last)
        if "(Arena: " not in last:
            fail("the console's exit line carries no arena (SPEC.md 96.33.19): "
                 "%r" % last)
        akb = int(last.split("(Arena: ")[1].split("KB")[0])
        if akb < kd_kb:
            fail("the console says the arena was %dKB and the program itself "
                 "reported %dKB above its PSP: [dos_akb] did not come home "
                 "(SPEC.md 96.41.1) and what is on the glass is the WINDOWED "
                 "launch's figure - %r" % (akb, kd_kb, last))
        print("kdreturn: the console logged %r" % last)

        # --- 7. ...AND THE CLOCK IS STILL A CLOCK (SPEC.md 96.49.5) ---------
        # `kd_resume` staged the six TIME fields out of `clk_sn_*`, which
        # `cw_clk_snapshot` fills - and that routine was a bare `retf` under a
        # comment saying it was *"the one stub that is NOT a stub"*. So the
        # stores copied cleared `.bss` and the session came home at 00:00:00 on
        # 00/00/0000, reported from the field as a corrupted clock on EVERY DOS
        # program. Two of those fields are not merely wrong but OUT OF RANGE:
        # `clk_mon` = 0 indexes `clk_mnames` three bytes before the table.
        #
        # Asserted against the GUEST's own clock and never the host's: most of
        # these profiles are a 5150, which has no RTC at all (SPEC.md 37.90),
        # so what the machine said before the handoff is the only authority for
        # what it should say after it.
        #
        # VERIFIED RED on the shipped tree: `00:00:18 00/00/0000` against
        # `00:01:18 04/07/2026`.
        after = clock(m)
        print("kdreturn: the clock came back %02d:%02d:%02d %02d/%02d/%d"
              % (after[0], after[1], after[2], after[3], after[4], after[5]))
        if not (1 <= after[4] <= 12) or not (1 <= after[3] <= 31):
            fail("the clock came back on %02d/%02d/%d, which is not a date "
                 "(SPEC.md 96.49.5): `cw_clk_snapshot` is a stub, so the six "
                 "time fields kd_resume staged over KDL_CLK were cleared bss. "
                 "A month of 0 indexes clk_mnames BEFORE the table"
                 % (after[3], after[4], after[5]))
        if after[5] != before[5] or after[4] != before[4]:
            fail("the clock went from %r to %r - the DATE changed across a "
                 "return that took seconds (SPEC.md 96.49.5)"
                 % (before, after))
        moved = secs(after) - secs(before)
        if moved < 0:
            moved += 24 * 3600
        # The round trip is a handoff, a whole DOS program and an image read:
        # seconds, never zero and never minutes. The lower bound is what goes
        # red on a resume that staged the LAUNCH's clock and never advanced it;
        # the upper is what goes red on one that advanced it by garbage.
        if not (1 <= moved <= 600):
            fail("the clock moved %d seconds across the return, from %r to "
                 "%r. It should move by about how long the DOS program had "
                 "the machine: 0 is a resume that put the handoff's clock "
                 "back untouched, and a large number is HS_TICK0 being "
                 "subtracted from the wrong thing (SPEC.md 96.49.5)"
                 % (moved, before, after))
        print("kdreturn: ...and it moved forward %d seconds" % moved)

        # --- 8. ...AND IRQ0 IS STILL 18.2 Hz (SPEC.md 96.5, 96.5.3) --------
        # A DOS program may take PIT channel 0 for its own timer, and a great
        # many do; `dos_restore_machine` is what puts the divisor back, and it
        # is in the SHARED CORE so it runs on the windowed host and inside
        # `kern_dos` alike. Nothing asserted that line - it sits in a routine
        # that also restores the IVT, the BDA and the 8259 masks, and a
        # resumed machine counting ticks at a rate of the program's choosing
        # has EVERYTHING it measures in ticks wrong together, the clock
        # included.
        #
        # TWO INDEPENDENT RESTORES hold this - the core's teardown and
        # `hb_wake`'s own - so what is asserted is the OUTCOME, and removing
        # either one alone leaves it green. VERIFIED RED with BOTH gone: 72.8
        # Hz on the resumed desktop, and the clock reported 309 seconds of a
        # 60-second round trip, which is the compound damage in one line.
        #
        # It does NOT cover the MODE, which is not observable from here: that
        # same routine wrote 0x36 where `sched_init` writes 0x34, so channel 0
        # came back in the ROM's mode 3 after every DOS program ever run in a
        # window (SPEC.md 96.5.3). Said out loud so a green row is not taken
        # for cover it has not got.
        #
        # Measured against the GUEST's cycle counter, which is exact at any
        # emulator speed (CLAUDE.md, Testing) and is the one clock on the box
        # that the guest cannot influence.
        s0, t0 = m.status()["cycles"], guest_ticks(m)
        M.pace(m, 2.0)          # TIME: the rate's window, ~164 ticks
        s1, t1 = m.status()["cycles"], guest_ticks(m)
        hz = ((t1 - t0) & 0xFFFF) * 4772727.0 / (s1 - s0)
        print("kdreturn: IRQ0 is running at %.1f Hz" % hz)
        if not (16.0 <= hz <= 21.0):
            fail("IRQ0 is running at %.1f Hz after the return and the kernel "
                 "owns channel 0 at 18.2065 (SPEC.md 8.1). The DOS program "
                 "took the channel and `dos_restore_machine` did not put the "
                 "divisor back (SPEC.md 96.5) - so every tick-measured thing "
                 "on the machine, the clock included, is wrong together" % hz)
    finally:
        m.close()
        for p in (VHD, FLOPPY, SYSIMG):
            try:
                os.unlink(p)
            except OSError:
                pass

    print("kdreturn: ok - the machine went away, ran %s, and came back" % prog)


if __name__ == "__main__":
    main()
