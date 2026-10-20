"""fsclass - two file redirectors attached AT ONCE (SPEC.md 62.9.19).

    python3 tests/fsclass.py [machine]

RAMDISK.DRV and NET.DRV were both DRVC_FILE, and a class is ONE publication
slot (SPEC.md 51.2.1), so the second to attach was refused DRVE_TWICE and the
two were mutually exclusive. SPEC.md 62.9.19 gives the RAM disk a class of its
own, DRVC_FILE2, and makes drv_fs_call dispatch by the MOUNTED volume's class.

The system disk is the shipped 360KB one with a SYSTEM.CFG added that wants
the RAM disk (bit 2, drv_cfgbit's), so it attaches at boot. NET.DRV cannot be
wanted the same way: it refuses DRVE_HW unless something answers at the far
end of the cable, so it is TICKED on the Drivers page with
tests/lptlink/partner.py holding the other end - tests/socktest.py's
sequence, on its machine. Then:

  A  both rows loaded, neither carries an error, and drv_owner names each
     driver's row in its OWN class's slot - 5 the cable, 8 the RAM disk
  M  the RAM disk mounts from its page with the cable still attached
  L  the RAM disk's volume lists and takes a New Folder: the FSV verbs went to
     RAMDISK.DRV. With drv_fs_call still keyed on class 5 they reach NET.DRV,
     the link is down, and the listing fails - which is the defect this row
     exists to catch, and what makes L the assertion rather than A

The kernel before 62.9.19 fails at A: the second attach is DRVE_TWICE.
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
sys.path.insert(0, "tests/lptlink")
import dispcp                                               # noqa: E402
import partner as P                                         # noqa: E402
import os88marty as M                                       # noqa: E402
import os88build                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402
from os88mouse import Mouse                                 # noqa: E402
from os88geom import WIN_SIZE, MAX_WIN                      # noqa: E402

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_lpt"
OUT = os88build.at("build/fsclass")   # the run's tree: launch resolves
                                        # build/ paths there (SOAK-PARALLEL 14.2)
SYS = os.path.join(OUT, "sys360.img")

_EQ = os88sym.equates()
DRVC_FILE, DRVC_FILE2 = _EQ["DRVC_FILE"], _EQ["DRVC_FILE2"]
DRVR_SIZE, DRVR_SEG, DRVR_ERR = 16, _EQ["DRVR_SEG"], _EQ["DRVR_ERR"]
DV_SIZE, DV_KIND, DV_CLASS = _EQ["DV_SIZE"], _EQ["DV_KIND"], _EQ["DV_CLASS"]
DVOL_MAX, DVK_FILE = _EQ["DVOL_MAX"], _EQ["DVK_FILE"]
RD_ROW, NET_ROW = 3, 4              # drv_tab on kern_big (SPEC.md 31.1)
CFG_BITS = 1 << 2                   # drv_cfgbit: the RAM disk's

CP_I0Y, CP_IROWH, CP_IDRV = 6, 14, 2   # tests/rdmount.py's page geometry
CP_RX = 96
CP_LVIS, CP_LAY, CP_LAH, CP_LAX1 = 8, 118, 12, 52    # ctrl.inc, 31.1.5
RP_MNTX, RP_MNTW = 2, 64
RP_B0Y, RP_BH = 52, 16
TITLE_H = 18

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


os.makedirs(OUT, exist_ok=True)
shutil.copyfile(os88build.at("build/os8088-360.img"), SYS)
cfg = os.path.join(OUT, "SYSTEM.CFG")
with open(cfg, "wb") as f:              # the Makefile's system.cfg shape:
    f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW"
            + bytes([1, 2]) + CFG_BITS.to_bytes(2, "little") + b"\0\0")
subprocess.run([sys.executable, "tools/os88fat.py", "add", SYS, cfg,
                "SYSTEM.CFG"], check=True, stdout=subprocess.DEVNULL)

def tree():
    """The far side's file volume: the link mounts one when it comes up."""
    t = P.FileTree()
    t.add(0, "READ.ME", content=b"a link volume\r\n")
    return t


with os88ui.boot(SYS, apps="build/apps360.img", machine=MACHINE) as ui:
    m = ui.m
    mo = Mouse(marty=m)
    print(f"== {MACHINE} : the RAM disk and the cable, attached at once ==")

    # --- tick the cable, the far end answering (tests/socktest.py's way) ----
    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    quiet(m)
    cp = [w for w in wins(m) if w[2] >= 280 and w[3] >= 100]
    if not cp:
        sys.exit("no Control Panel")
    x0, y0 = cp[0][0] + 1, cp[0][1] + TITLE_H
    mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)     # Drivers
    quiet(m)
    p = P.Partner(m)
    vis = dispcp.drv_show(mo, x0, y0, NET_ROW, lambda: quiet(m))
    mo.to(x0 + CP_RX + 40, dispcp.drv_row_y(y0, vis))
    m.pause()
    p.sync()
    p.click_paused_release()
    p.hello(P.NET_VER_SOCK)
    p.allow(60000000)
    p.serve(tree(), limit=24, idle=8000000)
    m.run()
    quiet(m)

    # --- A ------------------------------------------------------------------
    tab = m.sym("drv_tab")
    row = {}
    for name, r in (("RAMDISK", RD_ROW), ("NET", NET_ROW)):
        b = m.read(tab + r * DRVR_SIZE, DRVR_SIZE)
        row[name] = (_u16(b, DRVR_SEG), b[DRVR_ERR])
    check("RAMDISK.DRV loaded, no error", row["RAMDISK"][0] and
          not row["RAMDISK"][1], "(seg %04x err %d)" % row["RAMDISK"])
    check("NET.DRV loaded beside it, no error", row["NET"][0] and
          not row["NET"][1], "(seg %04x err %d)" % row["NET"])
    own = m.read(m.sym("drv_owner"), 2 * DRVC_FILE2)
    o5 = _u16(own, (DRVC_FILE - 1) * 2)
    o8 = _u16(own, (DRVC_FILE2 - 1) * 2)
    k = M.KERNEL_SEG << 4
    want5 = tab + NET_ROW * DRVR_SIZE - k
    want8 = tab + RD_ROW * DRVR_SIZE - k
    check("class 5 is the cable's, class 8 the RAM disk's",
          (o5, o8) == (want5, want8),
          "(owners %04x/%04x, rows %04x/%04x)" % (o5, o8, want5, want8))
    vt = m.read(m.sym("dsk_vtab"), DVOL_MAX * DV_SIZE)
    netv = [i for i in range(DVOL_MAX)
            if vt[i * DV_SIZE + DV_KIND] == DVK_FILE
            and vt[i * DV_SIZE + DV_CLASS] == DRVC_FILE]
    check("the cable's volume is a class-5 redirected one", len(netv) == 1,
          "(rows %r)" % netv)

    # --- M ------------------------------------------------------------------
    # The RAM disk's page is the SECOND driver page now: pages are listed by
    # class and the cable's (5) comes before its (8). Eight static rows and
    # two driver pages is ten, so the list SCROLLS (SPEC.md 31.1.5): eight
    # rows from [cp_ltop] and the arrows in the ninth's place.
    want = m.read(m.sym("cp_nst"), 1)[0] + 1
    for _ in range(4):
        top = m.read(m.sym("cp_ltop"), 1)[0]
        if want < top + CP_LVIS:
            break
        mo.click(x0 + CP_LAX1 + 8, y0 + CP_LAY + CP_LAH // 2)  # down arrow
        quiet(m)
    top = m.read(m.sym("cp_ltop"), 1)[0]
    mo.click(x0 + 40, y0 + CP_I0Y + (want - top) * CP_IROWH + 7)
    quiet(m, 25.0)
    mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
    quiet(m, 25.0)
    vt = m.read(m.sym("dsk_vtab"), DVOL_MAX * DV_SIZE)
    rdv = [i for i in range(DVOL_MAX)
           if vt[i * DV_SIZE + DV_KIND] == DVK_FILE
           and vt[i * DV_SIZE + DV_CLASS] == DRVC_FILE2]
    check("the RAM disk mounted a class-8 volume", len(rdv) == 1,
          "(rows %r)" % rdv)
    if not rdv:
        sys.exit(1)
    m.key("Escape")                             # the panel, closed
    quiet(m)

    # --- L ------------------------------------------------------------------
    letter = chr(ord("A") + rdv[0])
    ui.open_drive(letter)
    before = [r[0] for r in ui.listing()]
    m.key("KeyN")
    quiet(m)
    for ch in "BOTH":
        m.key("Key" + ch)
    m.key("Enter")
    quiet(m)
    after = [r[0] for r in ui.listing()]
    check("%s: lists and takes a New Folder through RAMDISK.DRV" % letter,
          "BOTH" in after and "BOTH" not in before,
          "(%r -> %r)" % (before, after))

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
