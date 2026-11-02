#!/usr/bin/env python3
"""What the kern_mega port shims share, whichever emulator answers them
(docs/BOCHS.md, "Rows ported from MartyPC"; docs/86BOX.md, "kern_mega rows
on 86Box").

A row ported to kern_mega runs on a `Marty` that is a SERVER for MartyPC's
debug-server requests, answered by another emulator: tools/os88megabochs.py
(Bochs, the default) or tools/os88mega86box.py (86Box, $OS88_MEGA_EMU=86box).

THE NAMES: `os88mega*.py` are kern_mega's test BACKENDS, one per emulator,
and neither runs MartyPC. Each implements the `Marty` API - the class
tools/os88marty.py's rows are written against, named for MartyPC because
that is the emulator it was first a client of - so the classes are
`MegaBochs` and `Mega86Box`, subclasses of `os88marty.Marty`, and
tests/os88qmpmega.py is QEMU's QMP served by either.
Everything here is the same on both, because it is a fact about KERN_MEGA
and the row's disks rather than about the machine under it:

  * `mega_images` - which floppies a row's (system, apps) pair becomes:
    the shipped system disk is build/mega.img, the shipped apps disk
    build/megaapps.img, anything else goes in as it is;
  * `row_disk`/`hd_geometry`/`_blank` - a row's OWN fixed disk, as MartyPC
    is handed one (`--mount hd:0:PATH` or a staged run tree's VHD);
  * `MegaMixin` - selectors (every kern_mega package region and claim is an
    LDT selector, so `seg << 4` is some other memory), `sym` where kern_mega
    RUNS `.cold` and `.mega` (above 1MB, through the GDT), and the boot
    toast (`Entered protected mode`) retired out of a row's way.
"""
import os
import re
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

import os88marty as om         # noqa: E402

# --- the disks --------------------------------------------------------------

def mega_images(image, apps):
    """(A:, B:) for a row's (system, apps) pair: see the module note. Only
    the SHIPPED images in build/ itself are swapped - a scratch disk a row
    made (`tmp/apps.img`, built out of a shipped one with its own files on
    it) goes in as it is, because what is on it is the row's subject."""
    import os88build
    bld = os88build.mega_build()
    # RESOLVED FIRST, as os88marty.launch does for MartyPC: a scratch disk a
    # row names `build/x.img` was WRITTEN into the run's tree by
    # scratch_disk, and the literal opened as it stands is a FileNotFoundError
    # in a soak (mega-wdcombo). at() is the identity outside one
    image = os88build.at(image) if image else image
    apps = os88build.at(apps) if apps else apps

    # build/ OR THE RUN'S TREE: under a frozen soak a row's
    # `build/os8088-360.img` arrives as the tree's, and testing for build/
    # alone booted kern_BIG on Bochs - "no kern_mega line in 240s", on a
    # machine that was never going to print one (SOAK-PARALLEL.md 17.13)
    def shipped(p):
        return p and os88build.shipped_dir(os.path.dirname(os.path.abspath(p)))
    a = os.environ.get("OS88_MEGA_A") or image
    if not os.environ.get("OS88_MEGA_A") and (
            not image or (shipped(image) and re.match(
                r"(os8088|small)[-\w]*\.img$", os.path.basename(image)))):
        a = os.path.join(bld, "mega.img")
    b = apps
    if apps and shipped(apps) and re.match(r"apps(360|720|120)?\.img$",
                                           os.path.basename(apps)):
        b = os.path.join(bld, "megaapps.img")
    # ...and a shipped SYSTEM floppy in B: is mega.img as well: the rows that
    # boot C: with the system disk in B: install or upgrade FROM it
    # (tests/install.py, instkeep.py), and what they compare the result with
    # is os88marty.system_disk's - kern_mega's
    elif apps and shipped(apps) and re.match(r"(os8088|small)[-\w]*\.img$",
                                             os.path.basename(apps)):
        b = os.path.join(bld, "mega.img")
    return a, b


class MegaMixin(object):
    """What a kern_mega `Marty` answers the same way whatever runs it:
    selectors, symbols where kern_mega runs them, the boot toast. A
    backend supplies `_read`, `read`/`write`, `gdt_base()`/`ldt_base()`
    ((base, limit) of the live tables), `gdt(sel)` and `protected()`."""

    def _clock(self):
        """Seconds of GUEST time, for the waits below. The host's clock is
        only the fallback: Bochs reads its guest's (os88megabochs._clock,
        `ptime` under `clock: sync=slowdown`) and 86Box runs it
        fast-forwarded and overrides this with its own."""
        return time.time()

    # INSIDE A REAL-MODE BRACKET (SPEC.md 53.2.3) the bracket's package runs
    # as a COPY of its region at the real paragraph [fsx_rseg] (a region
    # above 1MB cannot run in real mode), and the region holds what it held
    # at the door until the copy comes back at the exit. A row that took the
    # region's base before the play - every vid* row does - reads and writes
    # the bytes that are NOT running. So while [fsx_rmode] is 1 an access
    # inside the bracket window's region goes to the copy instead
    # (MEGA-VIDEO-REALMODE-PLAN W5). Only an address at or above 1MB can be
    # such a region (one below runs where it is), so nothing else pays a
    # round trip for the question.
    def read(self, addr, length):
        return super().read(self._rmap(addr), length)

    def write(self, addr, data):
        return super().write(self._rmap(addr), data)

    def _rmap(self, addr):
        if addr < 0x100000:
            return addr
        r = self._rcopy()
        if r and r[0] <= addr < r[1]:
            return addr + r[2]
        return addr

    def _rcopy(self):
        """(lo, hi, delta): the bracket window's region [lo, hi) and how far
        its running copy is from it - or None, no real-mode bracket up.
        ONE read a call: fsx_rseg, fsx_rwin, fsx_rent and fsx_rmode are
        adjacent (kernel/fsx.inc), and the region's range is cached for the
        (copy, window) pair, which no bracket changes while it runs."""
        import os88geom
        import os88sym
        try:
            at = os88sym.linear("fsx_rseg")
            if os88sym.linear("fsx_rmode") != at + 6:
                raise KeyError("fsx_rmode")
        except KeyError:
            return None             # (a kernel with no real-mode bracket)
        b = self._read(at, 7)
        if b[6] != 1:
            return None
        rseg, win = b[0] | b[1] << 8, b[2] | b[3] << 8
        if not win:
            return None
        c = getattr(self, "_rcopyc", None)
        if c and c[0] == (rseg, win):
            return c[1]
        kseg = os88sym.equates(()).get("KERNEL_SEG", 0x60)
        w = self._read(kseg * 16 + win + os88geom.W_SEG, 2)
        d = self._desc(w[0] | w[1] << 8)
        if not d:
            return None
        r = (d[0], d[0] + d[1] + 1, (rseg << 4) - d[0])
        self._rcopyc = ((rseg, win), r)
        return r

    def selector(self, sel):
        return self._desc(sel)

    # ...and an EXEC BREAKPOINT a row arms in such a region (vidpreview's on
    # vp_frame.dec) is armed on the copy too, wherever the bracket put it:
    # beside what the row asked, a breakpoint on the kernel's own landing in
    # real mode (fsx_rland, the copy made and [fsx_rmode] 1) re-arms the set
    # for that copy and goes on, so the row never sees that stop. Nothing
    # changes for a set with no address at or above 1MB.
    def breakpoints(self, bps):
        self._rbpw = list(bps)
        return super().breakpoints(self._rbparm())

    def _rbparm(self):
        want = getattr(self, "_rbpw", None) or []
        hi = [int(b["addr"]) for b in want
              if b.get("type") == "exec" and int(b["addr"]) >= 0x100000]
        land = self._rland()
        if not hi or land is None:
            return want
        out = list(want)
        r = self._rcopy()
        if r:
            out += [{"type": "exec", "addr": a + r[2]} for a in hi
                    if r[0] <= a < r[1]]
        out.append({"type": "exec", "addr": land})
        return out

    def _rland(self):
        import os88sym
        try:
            return os88sym.linear("fsx_rland")
        except KeyError:
            return None

    def wait_stop(self, *a, **k):
        while True:
            got = super().wait_stop(*a, **k)
            land = self._rland()
            if not got or land is None or not getattr(self, "_rbpw", None):
                return got
            if self.status().get("flat_ip") != land:
                return got
            super().breakpoints(self._rbparm())
            self.run()

    def _desc(self, sel):
        """(base, limit) of selector SEL, or None when SEL is not a present
        data/code descriptor - a real segment.

        THE GDT AS WELL AS THE LDT. kern_mega's GDT is an IDENTITY map for
        most of it (selector s has base s x 16, which is what lets a real
        segment value mean the same thing), and a few entries are not: the
        kernel's DATA, 0010, is based at KERNEL_SEG x 16, and `.cold`'s
        COLD_SEG names its copy ABOVE 1MB where its real-mode rung is far
        below (tests/tnest.py). For an identity entry this answers what
        `seg << 4` did; past the GDT's limit (B800, A000) it is None."""
        if not sel & 4:
            base, limit = self.gdt_base()
            if sel < 8 or (sel & ~7) + 7 > limit:
                return None
            d = self._read(base + (sel & ~7), 8)
            if not d[5] & 0x80 or not d[5] & 0x10:
                return None
            return (d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24,
                    d[0] | d[1] << 8 | (d[6] & 0x0F) << 16)
        base, limit = self.ldt_base()
        if (sel & ~7) + 7 > limit:
            return None
        d = self._read(base + (sel & ~7), 8)
        if not d[5] & 0x80 or not d[5] & 0x10:     # present, code/data
            return None
        return (d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24,
                d[0] | d[1] << 8 | (d[6] & 0x0F) << 16)

    def sym(self, name, defines=()):
        """Marty.sym, with `.cold` where kern_mega RUNS it: COLD_SEG is a
        GDT selector there, its descriptor based above 1MB (mega_coldup),
        so `COLD_SEG * 16 + offset` - the map's reading - is the low copy's
        leftovers and a breakpoint on it never fires."""
        import os88sym
        eq = os88sym.equates(defines)
        if os88sym.segment_of(name, defines) == eq.get("COLD_SEG") \
                and self.protected():
            return self.gdt(eq["COLD_SEG"])[0] + os88sym.syms(defines)[name]
        # ...and `.mega`, which mega_coldup copies up beside it: one block,
        # `.mega` COLD_PARA paragraphs past `.cold`'s rung (mega_hilin)
        if os88sym.sections(defines).get(name) == ".mega" \
                and self.protected() and self.read(
                    os88sym.linear("mega_codehi", defines), 1)[0]:
            hi = int.from_bytes(self.read(os88sym.linear(
                "mega_coldlin", defines), 4), "little")
            return hi + os88sym.linear(name, defines) - eq["COLD_RAM"] * 16
        # ...and BEFORE mega_enter (a machine handed back paused at reset,
        # or a boot step such as stik_boot, which drv_notice_x runs before
        # protected mode) `.cold` is where the map puts it, COLD_SEG * 16
        return os88sym.linear(name, defines)

    def lin(self, seg, off=0):
        """The linear address of SEG:OFF, SEG a real segment or a selector."""
        s = self.selector(seg)
        return (s[0] if s else seg << 4) + off

    def readseg(self, seg, off, length):
        return self.read(self.lin(seg, off), length)

    def boot_toast(self, limit=10.0):
        """Retire kern_mega's boot toast (`Entered protected mode`,
        mega_mtoast): it is staged at the switch, armed by ui_task's first
        passes for EIGHT seconds, and a row that waits for "a toast" after
        its first act reads it as the answer - while a row comparing the
        menu bar before and after something sees it go. os88marty's
        kernel_up and os88ui's UI.up call this when the machine has it.

        It is waited for (the switch can come after the desktop gate), and
        once it is up it is EXPIRED by its own path - [toast_die] wound
        back, toast_pass's modular compare taking it down, as
        tests/toastbar.py does - rather than waited out."""
        import os88sym
        on, buf = os88sym.linear("toast_on"), os88sym.linear("toast_buf")
        die, ticks = os88sym.linear("toast_die"), os88sym.linear("ticks")
        end = self._clock() + limit
        seen = False
        while self._clock() < end:
            up = self.read(on, 1)[0]
            ours = self.read(buf, 8).startswith(b"Entered")
            if seen and not up:
                return
            if up and not ours:
                return                  # something else is being said
            if up and ours and not seen:
                t = int.from_bytes(self.read(ticks, 2), "little")
                self.write(die, ((t - 2) & 0xFFFF).to_bytes(2, "little"))
                seen = True
            time.sleep(0.1)

    def toast_out(self, limit=20.0):
        """Wait for kern_mega's BOOT TOAST to go (`Entered protected mode`,
        146 ticks from the first idle pass - kernel/mega.inc). No MartyPC
        kernel shows one, so a row that compares the glass across time - a
        damage pass against a whole repaint - would otherwise read the menu
        bar's toast leaving as a difference (deskzoom: 1,014 px in
        (300,6)-(479,13)). os88ui.boot and `launch(boot=True)` call it."""
        # [toast_dirty], [toast_on], [toast_want] - adjacent (toast.inc's
        # %if): the toast is STAGED during the boot and put up on the UI
        # task's first idle pass, so `on` alone reads 0 before it shows
        try:
            self.boot_toast()                   # expire it first, if it can
        except Exception:                       # noqa: BLE001
            pass
        try:
            a = self.sym("toast_dirty")
        except Exception:                       # noqa: BLE001
            return
        end = self._clock() + limit
        while any(self.read(a, 3)) and self._clock() < end:
            time.sleep(0.2)



def hd_geometry(path, chs=None):
    """(C, H, S) of a fixed-disk image a row hands the shim: `chs` when the
    row names one, else a VHD's own footer (`conectix` in the last sector,
    the geometry at +38h - MartyPC's template and every VHD tests/ builds),
    else a raw image's size over 16 heads of 63 sectors, the shape every raw
    disk in tests/ is cut to (hdtake). Bochs is told the same CHS its BIOS
    then reports, so the partition table's CHS reads as it did on MartyPC."""
    if chs:
        return tuple(int(v) for v in re.split(r"[/x,]", chs))
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        f.seek(max(0, size - 512))
        tail = f.read(512)
    if tail[:8] == b"conectix":
        return (int.from_bytes(tail[0x38:0x3A], "big"), tail[0x3A],
                tail[0x3B])
    for h, s in ((16, 63), (4, 17), (4, 26)):
        if size and size % (h * s * 512) == 0:
            return (size // (h * s * 512), h, s)
    raise om.MartyError("os88megabochs: %s is neither a VHD nor a whole "
                        "number of 16x63 cylinders - pass ('--chs', 'C/H/S')"
                        " in `extra`" % path)


def row_disk(machine, extra, run_dir):
    """THE ROW'S OWN FIXED DISK, as MartyPC is handed one, or None:
    `extra=["--mount", "hd:0:PATH"]` (os88marty.launch's own argument), or
    a caller-staged `run_dir` (os88marty.stage_run_dir) on an `_hdd` machine,
    whose `media/hdds/*.vhd` IS the machine's disk on MartyPC. Mounted IN
    PLACE and written through, so an install in one launch is there for the
    boot in the next and the host reads the partition back afterwards.
    `("--chs", "C/H/S")` beside it names a raw image's geometry."""
    hd, chs, it = None, None, iter(extra or ())
    for x in it:
        if x == "--mount":
            v = next(it, "")
            if v.startswith("hd:0:"):
                hd = v[5:]
        elif x == "--chs":
            chs = next(it, None)
    if hd is None and run_dir and "_hdd" in (machine or ""):
        d = os.path.join(run_dir, "media", "hdds")
        vhds = sorted(f for f in os.listdir(d) if f.lower().endswith(".vhd")) \
            if os.path.isdir(d) else []
        if vhds:
            hd = os.path.join(d, vhds[0])
    if hd is None:
        return None, None
    if not os.path.isabs(hd) and run_dir and os.path.exists(
            os.path.join(run_dir, hd)):
        hd = os.path.join(run_dir, hd)      # MartyPC resolves in its run dir
    if not os.path.exists(hd):
        raise om.MartyError("os88megabochs: no fixed disk at %s" % hd)
    return os.path.abspath(hd), hd_geometry(hd, chs)


def _blank(path):
    """An all-zero floppy - a MartyPC row's way of offering the BIOS C:
    (tests/hdboot.py's BLANK). Bochs gets an EMPTY drive instead: a 360KB
    image in its 1.44MB A: is a media mismatch, and empty boots C: the same."""
    with open(path, "rb") as f:
        while True:
            b = f.read(65536)
            if not b:
                return True
            if b.count(0) != len(b):
                return False

