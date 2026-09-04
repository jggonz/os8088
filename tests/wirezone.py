#!/usr/bin/env python3
"""The desktop SERVICE zone appears with its driver and leaves with it (SPEC.md 26.7).

    make && python3 tests/wirezone.py

The kernel's half of the Wire is a GENERIC zone a driver registers: no glyph,
no caption, no launch name in the kernel at all, so a machine with no network
card pays one compare and nothing else. That claim has two halves and this row
is both of them.

  SCENARIO 1, with the driver.  `make ethertest` builds a disk whose
    SYSTEM.CFG already asks for ETHER.DRV, and QEMU is given an ne2k_isa - so
    the card is up and the driver has registered before the first paint, and
    nothing here has to drive the Control Panel to make the zone exist.
    Asserts `[desk_svc_seg]` is non-zero and that the zone's drawn rect HAS A
    PICTURE IN IT.  Then unticks Ethernet on the Control Panel's Drivers page,
    which is the one user route to a detach, and asserts `[desk_svc_seg]` is
    back to 0 and the rect is bare desktop again WITH NO STALE PIXELS.
  SCENARIO 2, without it.  The shipped `os8088.img`, no NIC: `[desk_svc_seg]`
    is 0 and the same rect is bare desktop.

**HOW "HAS A PICTURE IN IT" IS MEASURED, because a pixel count is not it.**
The desktop is a 50% dither - a perfect checkerboard, so no two horizontally
adjacent pixels are ever the same colour and the longest horizontal run of one
colour in any row is exactly 1.  Anything drawn over it is solid somewhere:
the caption's white rect is a run of ~30, the icon's flanges are runs of 6.
So the discriminator is the LONGEST HORIZONTAL RUN in the rect, and it
separates the two states by more than an order of magnitude rather than by a
threshold somebody has to tune.  It is also adapter- and art-independent: it
says "something opaque is drawn here", which is the actual claim, and it
cannot be satisfied by a stale fragment being the right total number of
pixels.

The withdraw half is the reason this row exists.  `wz_withdraw`,
`desk_zmark`'s delete edge and the extra `inc byte [desk_zhw]` that covers the
ordinal past the last volume are reached by no other test in the tree, and the
failure they guard against - the icon staying on the glass after its driver
has gone - is invisible to every assertion about state.

QEMU by name, and not by preference: MartyPC has no network card of any kind,
so the driver cannot be hosted on it at all (tests/ethernet.py's note).
Nothing here is a time.
"""
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
# tests/ FIRST and tools/ SECOND, so tools/ ends up at index 0: there is a
# tests/heapmap.py as well as a tools/heapmap.py and the wrong order shadows
# the one with Qmp in it (tests/vmmouse.py's note).
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88sym                                              # noqa: E402
import os88qemu                                             # noqa: E402
import shot                                                 # noqa: E402

SOCK = os.path.join(ROOT, "build", "wirezone.sock")
PIDFILE = os.path.join(ROOT, "build", "wirezone.pid")
PPM = os.path.join(ROOT, "build", "wirezone.ppm")

# kernel/desk.inc and kernel/disk.inc, mirrored - the geometry is derived from
# the kernel's own published words below and only these constants are copied.
DESK_ZY0, DESK_ZW, DESK_ZOVER, DESK_COLW = 32, 32, 2, 44
DV_FLAGS, DV_SIZE, DVOL_MAX = 2, 16, 8

RUN_DRAWN = 8                   # the longest horizontal run that says "drawn"
RUN_DITHER = 2                  # ...and the one that says "bare desktop"


def say(*a):
    print(*a)
    sys.stdout.flush()


def kill_stale():
    if os.path.exists(PIDFILE):
        try:
            os.kill(int(open(PIDFILE).read().strip()), signal.SIGTERM)
            time.sleep(1)
        except Exception:
            pass
    for f in (SOCK, PIDFILE, PPM):
        if os.path.exists(f):
            os.unlink(f)


def build():
    """The kernel under test, the shipped disk and the gate's own. The row is
    builds=True (tests/suite.py) precisely so this may write build/, and a
    reader running the script by hand should not have to know the target."""
    subprocess.run(["make", "-s", "build/os8088.img", "ethertest"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)


def launch(img, nic):
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    card = (" -netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=3"
            if nic else "")
    subprocess.run(
        em +
        " -drive file=%s,format=raw,if=floppy -boot a"
        " -drive file=build/apps.img,format=raw,if=floppy,index=1"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s%s"
        % (img, SOCK, PIDFILE, card), cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)
    q = heapmap.Qmp(SOCK)
    for _ in range(300):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.1)
    return q


def word(q, sym):
    b = q.read(os88sym.linear(sym), 2)
    return b[0] | (b[1] << 8)


def wait_desktop(q):
    t0 = time.time()
    while time.time() - t0 < 120:
        try:
            if word(q, "vid_w"):
                break
        except Exception:
            pass
        time.sleep(0.25)
    time.sleep(8)               # ...and the first paint, plus drv_boot's read


def zone_rect(q):
    """The service zone's DRAWN rect, out of the kernel's own words.

    desk_zone_rect's arithmetic and desk_ord_xy's wrap, in Python: the zone's
    ordinal is the number of volumes that have one, columns fill downwards and
    then wrap LEFT, and the drawn rect is the hit zone plus the caption's 2px
    overhang each side. Derived rather than written down, so the row still
    means something on a machine that mounts a different number of volumes.
    """
    zx = word(q, "vid_desk_zx")
    step = word(q, "desk_zstep")
    zh1 = word(q, "desk_zh1")
    rows = word(q, "desk_rows")
    vtab = q.read(os88sym.linear("dsk_vtab"), DV_SIZE * DVOL_MAX)
    shown = sum(1 for i in range(DVOL_MAX)
                if vtab[i * DV_SIZE + DV_FLAGS] & 1)
    col, row = divmod(shown, rows)          # the service zone's own ordinal
    x = zx - DESK_COLW * col
    y = DESK_ZY0 + step * row
    return (x - DESK_ZOVER, y,
            x + DESK_ZW + DESK_ZOVER - 1, y + zh1), shown


def longest_run(q, rect, cga):
    """The longest horizontal run of one colour inside rect (see the header).

    The dump is the HOST scanout, so on the CGA path it comes back 640x400 for
    a 640x200 mode and every Y doubles; X matches (tools/shot.py's note).
    """
    q.hmp('screendump "%s"' % PPM)
    w, h, pix = shot.read_ppm(PPM)
    os.unlink(PPM)
    x1, y1, x2, y2 = rect
    if cga:
        y1, y2 = y1 * 2, y2 * 2 + 1
    best = 0
    for y in range(max(0, y1), min(h, y2 + 1)):
        base = y * w * 3
        run, prev = 0, None
        for x in range(max(0, x1), min(w, x2 + 1)):
            px = pix[base + x * 3:base + x * 3 + 3]
            run = run + 1 if px == prev else 1
            prev = px
            if run > best:
                best = run
    return best


def open_drivers_page(q):
    """Chip menu -> Control Panel, then the Drivers page.

    A press on the chip, a move down the pull-down and a release on the item -
    the menu idiom, not two clicks (SPEC.md 12). The item positions are the
    kernel's own fixed layout; a miss shows up as [desk_svc_seg] never
    reaching 0 below, which is what the failure says.
    """
    sys.argv = ["mouse.py", SOCK]
    import mouse                                            # noqa: E402
    mouse.SOCK = SOCK
    mouse.goto(8, 8)
    mouse.hmp("mouse_button 1")
    time.sleep(0.4)
    mouse.goto(60, 45)                  # 'Control Panel', the second item
    time.sleep(0.3)
    mouse.hmp("mouse_button 0")
    time.sleep(5)
    mouse.goto(195, 186)                # the page list's 'Drivers'
    mouse.hmp("mouse_button 1")
    time.sleep(0.2)
    mouse.hmp("mouse_button 0")
    time.sleep(3)
    return mouse


def untick_ethernet(mouse):
    """...and the Ethernet checkbox, which is the one user route to a detach.

    Row 3 of the Drivers page - Sound, Hard Drive, Ethernet, Ram Disk - at the
    panel's fixed origin. Nothing else on this machine can make a loaded
    driver let go.
    """
    mouse.goto(267, 229)
    mouse.hmp("mouse_button 1")
    time.sleep(0.2)
    mouse.hmp("mouse_button 0")


def main():
    os.chdir(ROOT)              # mouse.hmp shells out to tools/qmp.py by a
                                # RELATIVE path, so the cwd is part of its
                                # contract
    kill_stale()
    build()
    fails = []

    # --- 1: the driver is loaded, so the zone is there ----------------------
    try:
        q = launch("build/ether360.img", nic=True)
        wait_desktop(q)
        cga = word(q, "vid_h") < 300
        rect, shown = zone_rect(q)
        seg = word(q, "desk_svc_seg")
        say("with ETHER.DRV: desk_svc_seg %04X, %d volume zone(s), "
            "service rect %s" % (seg, shown, rect))
        if not seg:
            fails.append("desk_svc_seg is 0 with ETHER.DRV loaded - the "
                         "driver did not register the zone (SPEC.md 26.7). "
                         "If the Drivers page says 'No hardware found', QEMU "
                         "got no ne2k_isa and this run proves nothing")
        run = longest_run(q, rect, cga)
        say("  drawn:     longest horizontal run %d px (want >= %d)"
            % (run, RUN_DRAWN))
        if run < RUN_DRAWN:
            fails.append("the zone's rect is bare desktop with the driver "
                         "loaded: longest run %d, want >= %d. The kernel "
                         "believes the zone exists, so this is the painter - "
                         "desk_draw_zone's call out to the driver's verb, or "
                         "the caption" % (run, RUN_DRAWN))

        # --- ...and it leaves with its driver ------------------------------
        m = open_drivers_page(q)
        untick_ethernet(m)
        t0 = time.time()
        while time.time() - t0 < 40:
            if not word(q, "desk_svc_seg"):
                break
            time.sleep(1)
        time.sleep(4)                   # ui_task's pass spends the repaint
        seg2 = word(q, "desk_svc_seg")
        say("after untick:  desk_svc_seg %04X" % seg2)
        if seg2:
            fails.append("desk_svc_seg is still %04X after unticking "
                         "Ethernet - either the checkbox was missed (the "
                         "coordinates in untick_ethernet are the panel's "
                         "fixed layout) or wz_withdraw did not run" % seg2)
        else:
            q.hmp("stop")
            run2 = longest_run(q, rect, cga)
            say("  withdrawn: longest horizontal run %d px (want <= %d)"
                % (run2, RUN_DITHER))
            if run2 > RUN_DITHER:
                fails.append("STALE PIXELS: the zone's rect still has a "
                             "run of %d after the driver detached, want <= "
                             "%d. The zone is gone from the kernel's state "
                             "and still on the glass - desk_zmark's delete "
                             "edge or the `inc byte [desk_zhw]` that covers "
                             "the ordinal past the last volume (SPEC.md 26.7)"
                             % (run2, RUN_DITHER))
    finally:
        kill_stale()

    # --- 2: no such driver, so no zone and nothing to pay for --------------
    try:
        q = launch("build/os8088.img", nic=False)
        wait_desktop(q)
        cga = word(q, "vid_h") < 300
        rect, shown = zone_rect(q)
        seg = word(q, "desk_svc_seg")
        q.hmp("stop")
        run = longest_run(q, rect, cga)
        say("plain os8088.img: desk_svc_seg %04X, %d volume zone(s), "
            "longest run %d px (want <= %d)" % (seg, shown, run, RUN_DITHER))
        if seg:
            fails.append("desk_svc_seg is %04X on a disk whose SYSTEM.CFG "
                         "asks for no network driver" % seg)
        if run > RUN_DITHER:
            fails.append("the shipped disk draws something where the service "
                         "zone would be: longest run %d, want <= %d"
                         % (run, RUN_DITHER))
    finally:
        kill_stale()

    for f in fails:
        say("FAIL " + f)
    say("wirezone: %d assertion(s) failed" % len(fails) if fails
        else "wirezone: OK - the zone arrives with its driver, leaves with "
             "it, and a machine without one draws nothing")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
