#!/usr/bin/env python3
"""Mounting the RAM disk must not take the machine with it (SPEC.md 22.6.3.1).

    python3 tests/rdmount.py [machine]

WHAT IT GUARDS is a loud mount with NOWHERE TO PUT A LISTING. `disk_mount`
decides twice whether a mount is loud - once on the FAT path and once at
`.fsmount`, where a redirected volume (SPEC.md 62.9.1) goes - and the gate is
`[dsk_quiet]` AND `[dsk_dseg]`. The redirected copy tested only the first
half, so `RAMDISK.DRV`'s Mount button, which reaches `osapi_vol_mount` and is
named in SPEC.md 22.6.3's own table as a caller that supplies no store, ran
`.scan_done` with `ES = 0`: the icon blank wrote `[dsk_nmax]` = 64 bytes of
0xFF over interrupt vectors 0..15, `int 08h` among them.

WHY NOTHING CAUGHT IT is the reason this file exists rather than an extra
check in `tests/rdup.py`. **The mount COMPLETES.** `rd_mount` returns CF=0,
the page repaints `Mounted D: 64K of 64K`, and every pixel of that is
correct; the machine dies at the NEXT TIMER TICK, vectoring to `FFFF:FFFF`.
So a row that ends on a screenshot cannot tell the difference - a frozen
machine and an idle desktop are the same picture - and `rdup` clicks this
exact button and passes either way. The assertion has to be that the guest is
still RUNNING, which is what `ticks` answers.

THE THREE CHECKS, and the first is the positive control:

  M  the volume actually mounted - a kernel that simply refused the mount
     would keep the IVT pristine and pass everything below
  V  the first 64 bytes of the IVT are byte for byte what they were
  T  `[ticks]` is still advancing a second later

V and T are one defect seen twice on purpose: V says WHAT was destroyed and
is exact, T says the machine is dead and would still fire if the next
scribble landed somewhere else.

IT DOES NOT OPEN A DISK WINDOW FIRST, and that is load-bearing rather than
brevity: a Disk window aims `[dsk_dseg]` at its own cache for the mount it
does, so a session that has opened one has a plausible segment lying in that
word and the stray listing lands in a claim instead of on the vectors. The
state this guards is a fresh desktop where the Control Panel is the only
window - which is exactly how the defect was reported.
"""
import sys

sys.path.insert(0, "tools")
import os88marty as M
import os88sym
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"

# ctrl.inc's geometry and page.inc's, mirrored deliberately: a gate that
# derives its coordinates from the kernel is testing the kernel against itself.
CP_I0Y, CP_IROWH = 6, 14
CP_RX = 96
CP_DBY1, CP_DROWH = 20, 26
CP_IDRV = 2
RP_MNTX, RP_MNTW = 2, 64        # page.inc: Mount, first button of the first
RP_B0Y, RP_BH = 52, 16          # row (rp_r_mount)
DRVR_SZ, DRVR_SEG = 16, 2
RD_ROW = 3                      # drv_tab row 3 is the RAM disk (SPEC.md 31.1)
_EQ = os88sym.equates()         # the volume table's shape, asked of the kernel
DV_SIZE, DV_KIND = _EQ["DV_SIZE"], _EQ["DV_KIND"]
DVOL_MAX, DVK_FILE = _EQ["DVOL_MAX"], _EQ["DVK_FILE"]
TITLE_H = 18
IVT_N = 64                      # vectors 0..15: what the icon blank wrote over

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o=0):
    return b[o] | (b[o + 1] << 8)


def wins(m):
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    return [tuple(_u16(b[i * WIN_SIZE:], o) for o in (2, 4, 6, 8))
            for i in range(MAX_WIN) if _u16(b[i * WIN_SIZE:], 0) & 2]


def quiet(m, s=12.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    print(f"== {MACHINE} : Mount must not scribble on the vectors ==")

    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    quiet(m)
    cp = [w for w in wins(m) if w[2] >= 280 and w[3] >= 100]
    if not cp:
        sys.exit("no Control Panel")
    cp = cp[0]
    x0, y0 = cp[0] + 1, cp[1] + TITLE_H

    mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)     # Drivers
    quiet(m)
    mo.click(x0 + CP_RX + 40,                                   # tick Ram Disk
             y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
    quiet(m, 25.0)
    seg = _u16(m.read(m.sym("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2))
    if not seg:
        sys.exit("the RAM disk driver did not load")

    nst = m.read(m.sym("cp_nst"), 1)[0]         # the driver's own page, first
    mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7)
    quiet(m, 25.0)                              # the first paint LOADS
                                                # RAMPAGE.DRV off the floppy

    # --- the snapshot the whole row is against ---------------------------
    ivt = m.read(0, IVT_N)
    print("  IVT before: %s ..." % ivt[:16].hex())

    mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
    quiet(m, 25.0)

    # M: it mounted. THE POSITIVE CONTROL - a kernel that simply refused the
    # mount would leave the vectors alone and pass V and T without ever doing
    # the thing this row is about.
    #
    # **ASKED, NOT TYPED** (rdup's own lesson): the row stride, the kind
    # offset and DVK_FILE all come out of the kernel, and the DRIVER's
    # [rd_vol] is deliberately not read - that would be an offset into an
    # image with no symbol table, and a wrong one decodes as a plausible
    # number rather than an error.
    vt = m.read(m.sym("dsk_vtab"), DVOL_MAX * DV_SIZE)
    kinds = [vt[i * DV_SIZE + DV_KIND] for i in range(DVOL_MAX)]
    check("a redirected volume is in dsk_vtab", DVK_FILE in kinds,
          "(kinds %s, DVK_FILE = %d)" % (kinds, DVK_FILE))

    # V: the vectors
    now = m.read(0, IVT_N)
    bad = [i for i in range(IVT_N) if now[i] != ivt[i]]
    check("the IVT is untouched", not bad,
          "" if not bad else "(%d bytes differ, first %#04x: %02x -> %02x - "
          "vector %d is int %02Xh)"
          % (len(bad), bad[0], ivt[bad[0]], now[bad[0]], bad[0] // 4,
             bad[0] // 4))

    # T: the machine is still running. A dead guest and an idle one are the
    # same screenshot, which is the whole reason this row exists.
    t0 = _u16(m.read(m.sym("ticks"), 2))
    M.pace(m, 0.5)                      # TIME, ~40 ticks: a stopped guest ends it
    t1 = _u16(m.read(m.sym("ticks"), 2))
    check("the guest is still taking ticks", t0 != t1,
          "(%d -> %d)" % (t0, t1))

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
