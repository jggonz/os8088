"""Shared by the Zip rows (tests/zip*.py, SPEC.md 138.8, docs/plans/ZIP-PLAN.md).

Two halves, and the rows use one or the other:

  ZipHost    a HOST for the Zip drive, in Python, through MartyPC's `inb` and
             `outb` verbs - Linux's ppa.c and imm.c register sequences, written
             as the driver's behaviour (what it writes, what it expects back).
             It is how `tests/zipmodel.py` proves the INSTRUMENT (the port
             wiring, the config keys, the verbs, the host-image file) with no
             guest and no ZIP.DRV in the way; and it is NOT the driver, which is
             8086 assembly and is what the other rows exercise.

  the guest side   the helpers the end-to-end rows share: launching a Zip
             machine on a fresh copy of a QA disk, ticking `Zip Drive` on the
             Drivers page, finding the volume the driver registers, and
             reading the driver's own state. They go through tools/os88ui.py
             wherever it has a verb, and through the kernel's own tables where
             it has none - never through a remembered coordinate (docs/WRITING-
             TESTS.md section 6): every position here is read off the guest.

Nothing in this file runs an emulator at import.
"""
import os
import re
import shutil
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]

import os88marty as M                                      # noqa: E402
import os88zip                                             # noqa: E402

LPT = 0x378
BUILD = os.environ.get("OS88_BUILD") or os.path.join(ROOT, "build")
ZIPDIR = os.path.join(BUILD, "zip")

# the SCSI status bytes and sense keys the model answers with (devices/zip.rs)
GOOD, CHECK = 0x00, 0x02
SK_NOT_READY, SK_ILLEGAL, SK_UNIT_ATTN, SK_DATA_PROT = 2, 5, 6, 7


def qa_image(name):
    """The path of one of tools/os88zip.py's QA disks (`make zipimg`)."""
    return os.path.join(ZIPDIR, name)


def scratch_copy(name, into):
    """A private, still-sparse copy of a QA disk: the guest writes in place, so
    a row never lets it near the original."""
    dst = os.path.join(into, name)
    os88zip.clone(qa_image(name), dst)
    return dst


def read10(lba, n):
    return bytes([0x28, 0]) + struct.pack(">I", lba) + bytes([0]) + \
        struct.pack(">H", n) + bytes([0])


def write10(lba, n):
    return bytes([0x2A]) + read10(lba, n)[1:]


class ZipHost(object):
    """Linux's ppa.c / imm.c, as a host, through `inb`/`outb`."""

    def __init__(self, m, vpi2, ps2=False, base=LPT):
        self.m, self.vpi2, self.ps2, self.base = m, vpi2, ps2, base

    # -- ports, batched ------------------------------------------------------
    def run(self, ops):
        """ops: ('o', reg, value) | ('i', reg); reg 0 = data, 1 = status,
        2 = control. One round trip; the values read, in order."""
        cmds = []
        for op in ops:
            if op[0] == "o":
                cmds.append({"cmd": "outb", "port": self.base + op[1], "value": op[2]})
            else:
                cmds.append({"cmd": "inb", "port": self.base + op[1]})
        out = self.m.batch(cmds)
        return [r["value"] for r, op in zip(out, ops) if op[0] == "i"]

    def dtr(self, v):
        self.run([("o", 0, v)])

    def ctr(self, *vs):
        self.run([("o", 2, v) for v in vs])

    def str_(self):
        return self.run([("i", 1)])[0]

    def rdtr(self):
        return self.run([("i", 0)])[0]

    # ---------------- VPI0 (ppa.c) ----------------
    def ppa_pulse(self, b, kind):
        if kind == "c":
            self.run([("o", 0, b), ("o", 2, 4), ("o", 2, 6), ("o", 2, 4), ("o", 2, 0xC)])
        else:
            self.run([("o", 0, b), ("o", 2, 0xC), ("o", 2, 0xE), ("o", 2, 0xC),
                      ("o", 2, 4), ("o", 2, 0xC)])

    def ppa_connect(self):
        for b in (0, 0x3C, 0x20, 0x8F):
            self.ppa_pulse(b, "c")

    def ppa_disconnect(self):
        for b in (0, 0x3C, 0x20, 0x0F):
            self.ppa_pulse(b, "d")

    def ppa_cable_ok(self):
        a = self.run([("o", 2, 0xE), ("i", 1), ("o", 2, 0xC), ("i", 1)])
        return a[0] & 8 == 8 and a[1] & 8 == 0

    def ppa_select(self, tid):
        if self.str_() & 0x40:
            return False
        r = self.run([("o", 0, 1 << tid), ("o", 2, 0xE), ("o", 2, 0xC), ("o", 0, 0x80),
                      ("o", 2, 8), ("i", 1)])
        return bool(r[0] & 0x40)

    def ppa_wait(self):
        r = self.str_()
        return (r & 0xF0) if r & 0xC0 == 0xC0 else 0

    def ppa_out(self, data):
        if self.ppa_wait() & 0x50 != 0x40:
            return False
        ops = []
        for b in data:
            ops += [("o", 0, b), ("o", 2, 0xE), ("o", 2, 0xC)]
        self.run(ops)
        return True

    def ppa_in(self, n):
        if self.ppa_wait() & 0x50 != 0x50:
            return None
        if self.ps2:
            ops = [("o", 2, 0x25)]
            for _ in range(n):
                ops += [("i", 0), ("o", 2, 0x27), ("o", 2, 0x25)]
            ops += [("o", 2, 4), ("o", 2, 0xC)]
            return bytes(self.run(ops))
        ops = []
        for _ in range(n):
            ops += [("o", 2, 4), ("i", 1), ("o", 2, 6), ("i", 1)]
        ops += [("o", 2, 0xC)]
        r = self.run(ops)
        return bytes((r[2 * i] & 0xF0) | ((r[2 * i + 1] & 0xF0) >> 4) for i in range(n))

    # ---------------- VPI2 (imm.c) ----------------
    def imm_cpp(self, b):
        r = self.run([("o", 2, 0xC), ("o", 0, 0xAA), ("o", 0, 0x55), ("o", 0, 0),
                      ("o", 0, 0xFF), ("i", 1), ("o", 0, 0x87), ("i", 1), ("o", 0, 0x78),
                      ("i", 1), ("o", 0, b), ("o", 2, 0xC), ("o", 2, 0xD), ("o", 2, 0xC),
                      ("o", 0, 0xFF)])
        s1, s2, s3 = r[0] & 0xB8, r[1] & 0xB8, r[2] & 0x38
        if (s1, s2, s3) == (0xB8, 0x18, 0x30):
            return 1
        if (s1, s2, s3) == (0xB8, 0x18, 0x38):
            return 0
        return -1

    def imm_connect(self):
        self.imm_cpp(0xE0)
        self.imm_cpp(0x30)
        return self.imm_cpp(0xE0)

    def imm_disconnect(self):
        self.imm_cpp(0x30)

    def imm_select(self, tid):
        r = self.run([("o", 2, 0xC), ("i", 1)])
        if r[0] & 8:
            return False
        r = self.run([("o", 2, 4), ("o", 0, 0x80 | (1 << tid)), ("o", 2, 0xC),
                      ("o", 2, 0xD), ("i", 1), ("o", 2, 0xC)])
        return bool(r[0] & 8)

    def imm_wait(self):
        r = self.run([("o", 2, 0xC), ("i", 1), ("o", 2, 4)])[0]
        return (r & 0xB8) if r & 0x80 else 0

    def imm_negotiate(self):
        r = self.run([("o", 2, 4), ("o", 0, 1 if self.ps2 else 0), ("o", 2, 6),
                      ("i", 1), ("o", 2, 7), ("o", 2, 6)])
        return bool(r[0] & 0x20)

    def imm_out(self, data):
        if self.imm_wait() & 0x18 != 0x08:
            return False
        ops = [("o", 2, 4)]
        for i in range(0, len(data), 2):
            ops += [("o", 0, data[i]), ("o", 2, 5), ("o", 0, data[i + 1]), ("o", 2, 0)]
        ops += [("o", 2, 4)]
        self.run(ops)
        return True

    def imm_in(self, n):
        if self.imm_wait() & 0x18 != 0x18:
            return None
        ops = [("o", 2, 4)]
        for _ in range(n):
            if self.ps2:
                ops += [("o", 2, 0x26), ("i", 0), ("o", 2, 0x25)]
            else:
                ops += [("o", 2, 6), ("i", 1), ("o", 2, 5), ("i", 1), ("o", 2, 4)]
        ops += [("o", 2, 0xC)]
        r = self.run(ops)
        if self.ps2:
            return bytes(r)
        return bytes((r[2 * i + 1] & 0xF0) | ((r[2 * i] & 0xF0) >> 4) for i in range(n))

    # ---------------- one whole command, either protocol ----------------
    def command(self, cdb, din=0, dout=None, tid=6):
        """Run one SCSI command the way the Linux engine orders it. Returns
        (status, data). Raises on a transport failure (the model's refusal to
        be driven wrongly is the finding)."""
        cdb = bytes(cdb)
        if self.vpi2:
            return self._imm_command(cdb, din, dout, tid)
        return self._ppa_command(cdb, din, dout, tid)

    def _ppa_command(self, cdb, din, dout, tid):
        self.ppa_connect()
        assert self.ppa_cable_ok(), "VPI0: the cable check failed"
        assert self.ppa_select(tid), "VPI0: select timed out"
        self.ctr(0xC)
        for b in cdb:
            assert self.ppa_out(bytes([b])), "VPI0: the command phase refused a byte"
        data = b""
        if dout is not None:
            for i in range(0, len(dout), 512):
                self.ctr(0xC)
                if self.str_() & 0xF0 == 0xF0:      # the drive ended it early
                    break                           # (DATA PROTECT, ...)
                assert self.ppa_out(dout[i:i + 512]), "VPI0: the data-out phase refused"
        else:
            left = din
            while True:
                self.ctr(0xC)
                if self.str_() & 0xF0 == 0xF0:
                    break
                assert left > 0, "VPI0: the drive has more data than the host asked for"
                burst = 512 if left >= 512 else 1
                got = self.ppa_in(burst)
                assert got is not None, "VPI0: data-in refused"
                data += got
                left -= burst
        assert self.ppa_wait() == 0xF0, "VPI0: no status phase"
        st = self.ppa_in(1)[0]
        assert self.ppa_wait() == 0xF0, "VPI0: no message phase"
        msg = self.ppa_in(1)[0]
        assert msg == 0, "VPI0: message byte %02x" % msg
        self.ppa_disconnect()
        return st, data

    def _imm_command(self, cdb, din, dout, tid):
        assert self.imm_connect() == 1, "VPI2: the CPP connect was not answered"
        assert self.imm_select(tid), "VPI2: select timed out"
        self.ctr(0xC)
        for i in range(0, len(cdb), 2):
            assert self.imm_out(cdb[i:i + 2]), "VPI2: the command phase refused"
        self.ctr(0xC)
        x = self.str_() & 0xB8
        rd, dp = bool(x & 0x10), not x & 0x20
        if dp and rd:
            assert self.imm_negotiate(), "VPI2: no data available at negotiation"
        data = b""
        if dp:
            if dout is not None:
                for i in range(0, len(dout), 512):
                    self.ctr(0xC)
                    if self.str_() & 0xB8 == 0xB8:  # the drive ended it early
                        break
                    assert self.imm_out(dout[i:i + 512]), "VPI2: data-out refused"
                    self.ctr(0xC)
            else:
                left = din
                while True:
                    self.ctr(0xC)
                    r = self.str_() & 0xB8
                    if r == 0xB8:
                        break
                    assert r & 0x88 == 0x88 and left > 0, "VPI2: data phase %02x" % r
                    burst = 512 if left >= 512 else 1
                    got = self.imm_in(burst)
                    assert got is not None, "VPI2: data-in refused"
                    data += got
                    left -= burst
        if dp and rd:
            self.ctr(4, 0xC, 0xE, 4)
        assert self.imm_wait() == 0xB8, "VPI2: no status phase"
        assert self.imm_negotiate(), "VPI2: no status at negotiation"
        st = self.imm_in(1)[0]
        assert self.imm_wait() == 0xB8, "VPI2: no message phase"
        self.imm_in(1)
        self.ctr(4, 0xC, 0xE, 4)
        self.imm_disconnect()
        return st, data

    # ---------------- convenience ----------------
    def sense(self):
        st, d = self.command([3, 0, 0, 0, 18, 0], 18)
        assert st == GOOD
        return d[2], d[12], d[13]

    def read(self, lba, n):
        st, d = self.command(read10(lba, n), 512 * n)
        return st, d

    def write(self, lba, data):
        return self.command(write10(lba, len(data) // 512), 0, data)[0]


# =============================================================================
# the guest side
# =============================================================================

DRVR_SIZE, DRVR_SEG, DRVR_FILE, DRVR_ERR = 16, 2, 4, 14      # kernel/driver.inc
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96                          # kernel/ctrl.inc
CP_LALL, CP_LVIS, CP_LAY, CP_LAH, CP_LAX1 = 9, 8, 118, 12, 52   # ...the list's scroll
CP_DBY1, CP_DROWH, CP_DVIS = 20, 26, 4
CP_DSX1, CP_DSW, CP_DSAH = 200, 14, 14
CP_IDRV = 2


def drv_row(m, filename="ZIP.DRV"):
    """The index of a driver's row in the kernel's drv_tab, found BY NAME: the
    file name each row carries is a NUL-terminated string in the kernel image,
    so the row's position is read off the guest rather than remembered (a
    constant like cdrom.py's CD_ROW is one reorder from being wrong)."""
    tab = m.sym("drv_tab")
    kseg = M.KERNEL_SEG << 4
    for i in range(24):
        row = m.read(tab + i * DRVR_SIZE, DRVR_SIZE)
        ptr = row[DRVR_FILE] | row[DRVR_FILE + 1] << 8
        if not ptr:
            continue
        name = m.read(kseg + ptr, 16).split(b"\0")[0].decode("ascii", "replace")
        if name.upper() == filename.upper():
            return i
    raise M.MartyError("no %s row in drv_tab" % filename)


def drv_state(m, row):
    """(segment, last error) of a driver row: segment 0 = not loaded."""
    r = m.read(m.sym("drv_tab") + row * DRVR_SIZE, DRVR_SIZE)
    return r[DRVR_SEG] | r[DRVR_SEG + 1] << 8, r[DRVR_ERR]


# ---- reading the guest ---------------------------------------------------------

import contextlib                                          # noqa: E402
import os88geom as geom                                    # noqa: E402


@contextlib.contextmanager
def paused(m):
    """The machine stopped for the length of the block (a host-side read of
    the Zip image must not land in the middle of a multi-sector commit,
    docs/MARTYPC-DEBUG.md's flush)."""
    m.pause()
    try:
        yield
    finally:
        m.run()


def find_text(m, text, top=0xA0000, chunk=0x10000):
    """Linear addresses of `text` in conventional memory. The Zip page, an
    editor's buffer and a toast all hold their words as plain bytes
    somewhere, and where is not a thing a row should have to know."""
    needle = text.encode("latin-1")
    hits, pos = [], 0
    while pos < top:
        n = min(chunk + len(needle), top - pos)
        blk = bytes(m.read(pos, n))
        at = blk.find(needle)
        while at >= 0:
            hits.append(pos + at)
            at = blk.find(needle, at + 1)
        pos += chunk
    return hits


def zones(m):
    """The drive letters that have a desktop zone, off desk_zslot."""
    return [chr(65 + i) for i in range(geom.DVOL_MAX)
            if geom.drive_ordinal(m, i) is not None]


def disk_windows_on(ui, letter):
    """The Disk windows showing drive `letter`."""
    d = ord(letter.upper()) - 65
    out = []
    for w in ui.windows():
        f = ui.fs_of(w)
        if f is not None and f[0] == d:
            out.append(w)
    return out


# ---- the Control Panel's Drivers page ----------------------------------------------

def control_panel(ui):
    """Open the Control Panel and return (window, cx, cy) with the content
    origin."""
    import cpface
    cpface.compact(ui.m, ui.m.sym)        # SPEC.md 135.13: the compact face's geometry
    ui.menu_pick("Apple", "Control Panel")
    w = ui.wait_window("Control Panel")
    return w, w.content[0], w.content[1]


def cp_item(ui, w, cx, cy, want):
    """Click list items until `cp_sel` is `want` (the page list scrolls, and
    a driver's page is appended after the static ones, SPEC.md 31.9)."""
    m = ui.m
    # Nine rows show while the list fits (CP_LALL); past that it scrolls, eight
    # show (CP_LVIS) and two arrows sit under them (kernel/ctrl.inc, SPEC.md
    # 31.1.5). A VGA machine has Display as well as the CGA statics, so the Zip
    # page is the TENTH row there and needs the down arrow (QA, the VGA look).
    for ordinal in range(CP_LALL):
        ui.mo.click(cx + 40, cy + CP_I0Y + ordinal * CP_IROWH + 7, settle=0)
        M.ui_done(m, "a Control Panel list click")
        if m.read(m.sym("cp_sel"), 1)[0] == want:
            return
    for _ in range(4):
        ui.mo.click(cx + CP_LAX1 + 6, cy + CP_LAY + CP_LAH // 2, settle=0)
        M.ui_done(m, "the Control Panel list's down arrow")
        ui.mo.click(cx + 40, cy + CP_I0Y + (CP_LVIS - 1) * CP_IROWH + 7, settle=0)
        M.ui_done(m, "a Control Panel list click")
        if m.read(m.sym("cp_sel"), 1)[0] == want:
            return
    raise M.MartyError("no row of the Control Panel list selects page %d" % want)


def tick_zip(ui, limit=120.0):
    """THE TICK (docs/plans/ZIP-PLAN.md claim 1): Control Panel > Drivers,
    scroll the Zip row into the four that show, click its box, wait for the
    driver to load or refuse. Returns (row, segment, error, new_letters):
    segment 0 with an error is a refusal (`DRVE_HW` = 5 is "No drive")."""
    m = ui.m
    row = drv_row(m)
    before = set(zones(m))
    w, cx, cy = control_panel(ui)
    cp_item(ui, w, cx, cy, CP_IDRV)
    dtop = m.sym("cp_dtop")
    ax = cx + CP_RX + CP_DSX1
    for _ in range(24):
        t = m.read(dtop, 1)[0]
        if t <= row < t + CP_DVIS:
            break
        ui.mo.click(ax + CP_DSW // 2, cy + CP_DBY1 + CP_DROWH * CP_DVIS - CP_DSAH // 2,
                    settle=0)
        M.ui_done(m, "the Drivers page's scroll arrow")
        if m.read(dtop, 1)[0] == t:
            raise M.MartyError("the Drivers list will not scroll to row %d (cp_dtop %d)" % (row, t))
    t = m.read(dtop, 1)[0]
    ui.mo.click(cx + CP_RX + 40, cy + CP_DBY1 + (row - t) * CP_DROWH + CP_DROWH // 2,
                settle=0)
    M.until(m, lambda _: any(drv_state(m, row)), "ZIP.DRV to load or refuse",
            limit=limit, guest=60.0)
    M.ui_done(m, "the driver's attach")
    seg, err = drv_state(m, row)
    return row, seg, err, sorted(set(zones(m)) - before)


def zip_page(ui):
    """Open the Zip Drive's own page in the Control Panel (the first page
    after the static ones). Returns (window, cx, cy)."""
    m = ui.m
    w, cx, cy = control_panel(ui)
    # [cp_sel] is a RECORD, not a list ordinal (kernel/ctrl.inc cp_v2r): the
    # statics are records 0..CP_ITEMS-1, some hidden on this adapter, and a
    # driver's page is CP_ITEMS + its ordinal among the driver pages. The Zip
    # page is the only driver page these machines have, so it is CP_ITEMS.
    # ([cp_nst], the count of statics SHOWN, is its list ordinal - and on a
    # CGA machine, where Display is hidden, it is Keyboard's record number.)
    cp_item(ui, w, cx, cy, cp_items(m))
    ui.still()
    return w, cx, cy


def cp_items(m):
    """CP_ITEMS, the kernel's own equate (kernel/ctrl.inc). Read off the map
    rather than off cp_items' labels, which live in CTRL.DRV's .modu and have
    no fixed segment."""
    import os88sym
    return os88sym.equates()["CP_ITEMS"]


# THE EJECT BUTTON'S GEOMETRY IS SPEC.md 138.6's AND IS NOT PINNED YET: the page
# is the core's (drivers/zip/zipcp.inc) and was being written in parallel with
# this file. The QA run (docs/plans/ZIP-PLAN.md Z6) pins it from that file's
# own layout constants - (x, y) of the button's middle, relative to the pane's
# content origin (cx + CP_RX, cy) - and until then the page-eject step FAILS
# rather than skips: a skip that reports as a pass is docs/WRITING-TESTS.md
# incident 38.
PAGE_EJECT = (34, 82)            # the middle of SPEC.md 138.6's pane-relative
                                 # rect (2, 74)-(65, 89) - drivers/zip/zipcp.inc


def page_eject(ui):
    """Press the page's Eject button and wait for the drive to be empty."""
    if PAGE_EJECT is None:
        raise M.MartyError("tests/zipq.py PAGE_EJECT is not pinned: take the Eject "
                           "button's position from drivers/zip/zipcp.inc (SPEC.md 138.6)")
    w, cx, cy = zip_page(ui)
    ui.mo.click(cx + CP_RX + PAGE_EJECT[0], cy + PAGE_EJECT[1], settle=0)
    M.ui_done(ui.m, "the Eject button")
    return w


def listing_ref(path, folder=""):
    """What tools/os88zip.py says a folder of a QA disk holds: {NAME: is_dir}."""
    v, _ = os88zip.open_image(path)
    e = v.lookup(os88zip._split(folder))
    out = {x["name"]: bool(x["attr"] & os88zip.A_DIR) for x in v.entries(e["clus"])
           if not x["attr"] & os88zip.A_LABEL and x["name"] not in (".", "..")}
    v.img.close()
    return out
