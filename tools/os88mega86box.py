#!/usr/bin/env python3
"""kern_mega under 86Box, behind the os88marty test API - the second backend
of kern_mega's port mechanism (docs/86BOX.md, "kern_mega rows on 86Box"; the
first is tools/os88megabochs.py, docs/BOCHS.md "Rows ported from MartyPC").

    OS88_MEGA=1 OS88_MEGA_EMU=86box python3 tests/<row>.py

With both set, `os88marty.launch()` answers a `Mega86Box`. Everything that
is a fact about 86Box rather than about kern_mega - the server for `Marty`'s
requests (`Marty` is the TEST API's class, named for the emulator it was
first a client of; nothing here runs MartyPC), the machine a row's name
describes, the CMOS priming, the GDB stub, the poker - is
tools/os88box86api.py's, which serves kern_big and kern_small rows too
($OS88_EMU=86box). What is here is what kern_mega adds on top:

  * THE DISKS: os88megacommon's mega_images - a shipped system floppy is
    build/mega.img, a shipped apps floppy build/megaapps.img, the default B:
    is megaapps.img - and the map is build/megak/kernel.map
    ($OS88_MEGA_BUILD's);
  * THE MACHINE: always vm/at286-port, the owner's mr286 (MR BIOS 286,
    16 MHz, 4MB, ISA IDE) with its CMOS seeded ($OS88_MEGA_VM names another
    profile), whatever the row's MartyPC name says added to it - the table
    is os88box86api's module note. $OS88_MEGA_SND, $OS88_MEGA_MEGS and
    $OS88_MEGA_POKE are its knobs, as they were;
  * PROTECTED MODE: `selector`/`lin`/`readseg` through kern_mega's own
    tables ([mega_gdtr], the LDT's descriptor at GDT 18h - automation has no
    sreg), `sym` where kern_mega RUNS `.cold` and `.mega`, the IDT's gate
    for an `int` breakpoint, and the boot toast (`Entered protected mode`)
    retired out of a row's way - os88megacommon's MegaMixin, shared with
    Bochs. `inb`/`outb` refuse in protected mode (the stub is real mode's).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import os88box86api as api     # noqa: E402
import os88marty as om         # noqa: E402
from os88megacommon import (MegaMixin, mega_images,  # noqa: E402
                            row_disk, _blank)
# the names this module has always answered, for whoever imports them here
from os88box86api import (GUEST_HZ, Gdb, GDB_REGS, BIOS_PROMPTS,  # noqa: E402,F401
                          scancode, machine_config as _machine_config,
                          cmos_seed, cmos_primed, _with_cfg)

PROFILE = api.MEGA_PROFILE


def machine_config(machine):
    """kern_mega's machine for a MartyPC name: ([(section, key, value)],
    [card, ...], {"display": ..., "snd": ...}) - os88box86api's table on
    vm/at286-port (see its module note)."""
    _profile, sets, cards, info = _machine_config(machine, mega=True)
    return sets, cards, info


class Mega86Box(MegaMixin, api.Api86Box):
    """Api86Box with kern_mega's tables on top: see the module note. `m.b`
    is the os88box.Box underneath."""

    box86 = True
    mega = True
    # MegaMixin's waits are host seconds (Bochs runs at real time); 86Box
    # runs fast-forwarded, so its clock is the GUEST's
    _clock = api.Api86Box._clock

    def _entered(self):
        # an AT's POST visits protected mode and leaves patterns in low
        # memory, and kern_mega's desktop is only ever up in protected mode
        return self.protected()

    @property
    def cmos(self):
        # the port stub is real mode's, and kern_mega's desktop is not
        raise AttributeError("cmos: not under kern_mega (protected mode)")

    def gdt_base(self):
        import os88sym
        g = self._read(os88sym.linear("mega_gdtr"), 6)
        return (g[2] | g[3] << 8 | g[4] << 16 | g[5] << 24,
                g[0] | g[1] << 8)

    def ldt_base(self):
        base, limit = self.gdt_base()
        d = self._read(base + 0x18, 8)          # MEGA_SEL_LDT's descriptor
        return (d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24,
                d[0] | d[1] << 8 | (d[6] & 0x0F) << 16)

    def gdt(self, sel):
        base, limit = self.gdt_base()
        if (sel & ~7) + 7 > limit:
            raise om.MartyError("os88mega86box: GDT selector %#x is past the "
                                "table's limit %#x" % (sel, limit))
        d = self._read(base + (sel & ~7), 8)
        return (d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24,
                d[0] | d[1] << 8 | (d[6] & 0x0F) << 16)

    def _vector(self, v):
        """Vector V's handler, linear: the IDT's gate in protected mode
        ([mega_idtr]), the IVT's in real mode."""
        if self.protected():
            import os88sym
            t = self._read(os88sym.linear("mega_idtr"), 6)
            base = t[2] | t[3] << 8 | t[4] << 16 | t[5] << 24
            g = self._read(base + v * 8, 8)
            off = g[0] | g[1] << 8
            if g[5] & 0x08:                     # a 386 gate
                off |= (g[6] | g[7] << 8) << 16
            return self.lin(g[2] | g[3] << 8, off)
        return api.Api86Box._vector(self, v)


def launch(image, apps=None, machine="", boot=True, label=None, extra=(),
           run_dir=None, cfg=None, megs=None, cimg=None, ne2k_io=None, **_):
    """os88marty.launch's shape, on 86Box and kern_mega (see the module
    note). `boot` True runs the machine to the desktop (os88box's kernel
    gate, on the emulated clock) and hands it back RUNNING; a number runs
    for that many guest seconds; False hands it back PAUSED at power-on.

    The rest are os88megabochs.launch's, for tests/os88qmpmega.py's QMP
    server: `cfg` a SYSTEM.CFG driver mask written onto A:'s copy ("keep":
    A:'s own stands; an `_ne2k` machine with neither wants ETHER.DRV, 10h),
    `megs` the RAM, `cimg` the row's own fixed disk (mounted IN PLACE, as
    `--mount hd:0:` is), `ne2k_io` the card's port."""
    if cimg:
        extra = list(extra or ()) + ["--mount", "hd:0:" + cimg]
    cimg, _cgeom = row_disk(machine, extra, run_dir)
    noa = bool(cimg) and (not image or _blank(image))
    a, b = mega_images(image, apps)
    import os88build
    bld = os88build.mega_build()        # the run's tree under a soak
    if not b:
        b = os.path.join(bld, "megaapps.img")
    if noa:
        a = None
    # THE MACHINE'S RAM in MB, as the Bochs shim takes it ($OS88_BOCHS_MEGS,
    # megaport.txt's `@OS88_BOCHS_MEGS=1`: a 1MB kern_mega has no pool, so
    # every claim is conventional and selector-named); the profile's 4MB
    # otherwise. ($OS88_MEGA_MEGS says the same without naming Bochs)
    megs = megs or os.environ.get("OS88_MEGA_MEGS") or os.environ.get(
        "OS88_BOCHS_MEGS")
    ne2k = "_ne2k" in (machine or "") or bool(os.environ.get(
        "OS88_BOCHS_NE2K"))
    if cfg == "keep":
        cfg = None
    elif cfg is None and ne2k:
        cfg = 0x10              # A:'s SYSTEM.CFG wants ETHER.DRV, as Bochs's
    return api.boot_machine(Mega86Box, a, b, machine, boot=boot, label=label,
                            cimg=cimg, cfg=cfg, megs=megs, ne2k_io=ne2k_io,
                            kmap=os.path.join(bld, "megak", "kernel.map"),
                            mega=True)


def main(argv):
    """`os88mega86box.py IMAGE [APPS] [MACHINE]`: boot one and print what the
    shim sees - a smoke test of the backend."""
    import json
    m = launch(argv[0] if argv else None, apps=argv[1] if len(argv) > 1
               else None, machine=argv[2] if len(argv) > 2 else "")
    with m:
        print(json.dumps(m.status()))
        print(json.dumps(m.cards()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
