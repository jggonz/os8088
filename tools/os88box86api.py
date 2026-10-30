#!/usr/bin/env python3
"""86Box answering the os88marty TEST API - for every kernel: kern_big and
kern_small here, kern_mega through tools/os88mega86box.py (docs/86BOX.md,
"kern_big rows on 86Box").

THE NAME. "Marty" below is `os88marty.Marty`, the class every MartyPC row is
written against - named for MartyPC because that emulator was the first one
it was a client of. Nothing in this module runs MartyPC: `Api86Box` is a
SERVER for the debug-server requests `Marty` is built on (`cmd(**kw)`),
answered out of an 86Box (tools/os88box.py: its automation socket and its
GDB stub). So os88ui, os88mouse, `until`/`settle`/`ui_done` and every row
above them run on it unchanged.

    OS88_EMU=86box python3 tests/<row>.py               # kern_big
    OS88_EMU=86box OS88_BUILD=build/smallk \\
        OS88_DEFINES=KERN_SMALL python3 tests/<row>.py   # kern_small
    OS88_MEGA=1 OS88_MEGA_EMU=86box python3 tests/<row>.py   # kern_mega

With $OS88_EMU=86box (and no $OS88_MEGA), `os88marty.launch()` answers an
`Api86Box` booting THE ROW'S OWN IMAGES and THE ROW'S OWN KERNEL: no disk is
swapped, and symbols come through os88sym's normal path (it re-assembles
kernel.asm and checks build/kernel.bin, or $OS88_BUILD's). The kern_mega
backend is the same class with kern_mega's tables on top (selectors, `.cold`
above 1MB, the boot toast) and the shipped floppies swapped for mega.img.

WHY 86Box FOR A REAL-MODE KERNEL: it models the hardware MartyPC has not got,
which is CLAUDE.md's QEMU closed list - a 286 and a 386 (HDD.DRV's rung 1),
an AT's MC146818 (the RTC ladder's write half), an 8042 with an aux port
(the PS/2 mouse), an NE1000/NE2000 on slirp - on a period BIOS rather than
SeaBIOS, and it reads back through the same API as a MartyPC row. It is NOT
cycle-accurate (docs/86BOX.md, *What it is NOT*): a row whose assertion is
an 8088 cycle count or a timing stays MartyPC's.

THE MACHINE a row's MartyPC name describes (`machine_config`):

  kern_big / kern_small - an IBM XT (vm/xt640: ibmxt86, 8088 at 4.77 MHz,
  640KB, a serial mouse on COM1) when everything fits one, else an AT:
    _ps2          vm/386-ps2: a Packard Bell Legend 300SX, the one machine
                  whose 8042 has an aux port, with its PS/2 mouse
    at386 / _386  the same 386SX with the serial mouse
    at286 / _286  vm/at286-port: the owner's mr286 (MR BIOS, 16 MHz, 4MB,
                  ISA IDE) - also taken for a 1.2MB or 1.44MB floppy (an XT
                  BIOS drives neither) and for `_ne2k` (a 16-bit card)
    _NNNk         the XT's RAM (128k, 256k, 512k ...)
  kern_mega - always vm/at286-port, as before.
  $OS88_86BOX_VM (kern_mega: $OS88_MEGA_VM) names any other vm/ profile.
  A name for a machine only 86Box has carries NO `os8088_` prefix - that is
  MartyPC's namespace, which tests/unit/t_machines.py holds to its machine
  list - so a row asking for an AT says `at286_vga`, `at286_vga_hdd` or
  `at386_vga_ps2`; an `os8088_*` name is the row's MartyPC machine, mapped.

and on any of them, what the name says is added:

    both          a CGA and a Hercules (os8088_5150_both*: the extended
                  desktop's pair), monitor 0 and 1; `_mono`: Hercules first
    vga_herc      a VGA (OTI-067) and a Hercules; vga_mda: and an MDA
    herc / cga    that card alone; xt_vga / vga: the VGA; ega: an EGA
    (no adapter)  a CGA - MartyPC's default machine is os8088_5150_cga
    _sb           a Sound Blaster 2.0 at 220h on IRQ 7, DMA 1, as MartyPC's
                  (SB DSP 2.1 on IRQ 7 + an AdLib, which the SB2.0's OPL2 is);
                  $OS88_86BOX_SND (kern_mega: $OS88_MEGA_SND) =sb16[:IRQ]
                  for an SB16 instead (an AT only)
    _ne2k         an NE2000 at 300h on IRQ 10 on an AT, an NE1000 at 300h on
                  IRQ 3 on an XT, on slirp ($OS88_BOCHS_NE2K too)
    _hdd          the row's own fixed disk: on the AT's IDE controller, on an
                  XT behind an XT-IDE (row_disk)

and a floppy drive's TYPE follows its image: on an AT a 360KB or 1.2MB image
gets a 5.25" 1.2MB drive and anything else a 3.5" 1.44MB one; on an XT a
360KB image a 5.25" 360KB drive and a 720KB one a 3.5" 720KB drive. An AT's
CMOS is SEEDED or PRIMED once per configuration (`cmos_primed`), so no boot
stops in BIOS setup; an XT has no CMOS.

THE CLOCK IS THE GUEST'S, as on MartyPC. Running is 86Box released with
fast-forward on (unpaced); paused is `hold`, and `advance` is an exact
`step` of emulated milliseconds. `status()["cycles"]` is emulated time at
4.77 MHz (GUEST_HZ), so every guest budget in os88marty means the same
seconds of machine time it means there - and the 1 ms quantum is its
resolution, not a measurement.

WHAT IT MAPS
  * memory: `read`/`write` are PHYSICAL (memr/memw - paging is off);
    `readseg`/`lin` are `seg << 4` in real mode (kern_mega's selectors are
    os88megacommon's MegaMixin).
  * registers: `regs` is automation's; `setreg`/`park` go through the GDB
    stub's `P` (IF and every segment register included).
  * breakpoints: `exec`/`execseg`/`int` are the stub's HARDWARE breakpoints
    (Z1), linear and unlimited in number; `int` is the vector's handler (the
    IVT's in real mode, the IDT's gate under kern_mega). A hit stops the CPU
    before the instruction runs; resuming executes it. `mem`/`io` are
    refused.
  * ports: `inb`/`outb` are one instruction (`in al, dx` / `out dx, al`)
    run through the stub at 0000:0500 with IF clear, the bytes and every
    register put back after - REAL MODE ONLY. `cmos()` is the MC146818's
    128 bytes read that way, the chip as the hardware holds it.
  * keys: set-1 scancodes through automation; the mouse: relative packets
    into whichever mouse the machine has (serial or PS/2), which os88mouse
    closes the loop on.
  * the glass: `fbuf(card)` is the card's last frame (monitor 0 is the
    config's first card, 1 its second - tools/86box/patches/05), `vram()`
    reads B8000/B0000 directly, `screen()` automation's text rows.
  * sound: with $MARTYPC_WAV set at launch the Sound Blaster's DAC output is
    written to <it>.sound_blaster.wav (tools/86box/patches/04), MartyPC's
    per-card capture, sample-and-held at 48 kHz on the emulated clock.
  * disks: `disks()` names the image AS MOUNTED (copied at launch); `flush`
    copies the live one (86Box writes a floppy through, a track at a time);
    `mount` loads another image into the drive (`fdd load`); `reset` is a
    hard reset; `disk()` is the FDC's READ/WRITE DATA counters (patches/06).
  * refused by name: flicker, pace, snapshot/restore, planes, wheel,
    history.

SEEING A ROW GO RED without a knob build: $OS88_86BOX_POKE (kern_mega:
$OS88_MEGA_POKE too) takes `SYM[+OFF]=HEX;...`, written over the kernel once
its desktop is up (`_poker`).
"""
import atexit
import json
import os
import queue
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import os88box as ob           # noqa: E402
import os88marty as om         # noqa: E402
from os88megacommon import row_disk, _blank  # noqa: E402,F401

GUEST_HZ = om.GUEST_HZ
TAG = "os88box86api"

# THE PROFILES this module builds machines from (vm/<name>/86box.cfg). The AT
# is the owner's mr286 as `at286-port`: a plain MR BIOS 286 the port
# mechanisms (kern_mega's and kern_big's) share, with nothing kernel-specific
# in it - its seeded CMOS (nvr-seed/) is why it is the AT of choice
XT_PROFILE = "xt640"
AT_PROFILE = "at286-port"
PS2_PROFILE = "386-ps2"
MEGA_PROFILE = "at286-port"


def _env(name, mega=False):
    """$OS88_86BOX_<NAME>, and under kern_mega $OS88_MEGA_<NAME> first (the
    names the kern_mega ports were registered with)"""
    if mega and os.environ.get("OS88_MEGA_" + name):
        return os.environ["OS88_MEGA_" + name]
    return os.environ.get("OS88_86BOX_" + name)


# W3C KeyboardEvent.code (MartyKey's names) -> set-1 scancode (0x100 = E0)
_SCAN = {
    "Enter": 0x1C, "Escape": 0x01, "Backspace": 0x0E, "Tab": 0x0F,
    "Space": 0x39, "ArrowUp": 0x148, "ArrowDown": 0x150,
    "ArrowLeft": 0x14B, "ArrowRight": 0x14D, "Home": 0x147, "End": 0x14F,
    "PageUp": 0x149, "PageDown": 0x151, "Insert": 0x152, "Delete": 0x153,
    "ShiftLeft": 0x2A, "ShiftRight": 0x36, "ControlLeft": 0x1D,
    "ControlRight": 0x11D, "AltLeft": 0x38, "AltRight": 0x138,
    "CapsLock": 0x3A, "NumLock": 0x45, "ScrollLock": 0x46,
    "Minus": 0x0C, "Equal": 0x0D, "BracketLeft": 0x1A, "BracketRight": 0x1B,
    "Backslash": 0x2B, "Semicolon": 0x27, "Quote": 0x28, "Backquote": 0x29,
    "Comma": 0x33, "Period": 0x34, "Slash": 0x35,
    "NumpadEnter": 0x11C, "NumpadAdd": 0x4E, "NumpadSubtract": 0x4A,
    "NumpadMultiply": 0x37, "NumpadDivide": 0x135, "NumpadDecimal": 0x53,
}
for _i, _n in enumerate("789", 0x47):
    _SCAN["Numpad" + _n] = _i
for _i, _n in enumerate("456", 0x4B):
    _SCAN["Numpad" + _n] = _i
for _i, _n in enumerate("123", 0x4F):
    _SCAN["Numpad" + _n] = _i
_SCAN["Numpad0"] = 0x52
for _i in range(10):
    _SCAN["F%d" % (_i + 1)] = 0x3B + _i
_SCAN["F11"], _SCAN["F12"] = 0x57, 0x58


def scancode(name):
    if name in _SCAN:
        return _SCAN[name]
    if name.startswith("Key") and len(name) == 4:
        return ob.KEYS[name[3].lower()]
    if name.startswith("Digit") and len(name) == 6:
        return ob.KEYS[name[5]]
    raise om.MartyError("os88box86api: no scancode for MartyKey %r" % name)


# --- the machine a row's MartyPC name describes --------------------------------

# 86Box's card -> (MartyPC's `type`, the 1bpp kind `vram` takes or None)
_CARDS = {"cga": ("cga", "cga"), "hercules": ("mda", "herc"), "vga": ("vga", None),
          "mda": ("mda", "herc"), "oti067": ("vga", None),
          "ega": ("ega", None)}

# the CPUs whose machines have no CMOS and an XT's floppy controller
_XT_CPUS = ("8088", "8086", "v20", "v30", "8088_europc")


def profile_cfg(profile):
    """{key: value} of vm/<profile>/86box.cfg's [Machine] section"""
    order, secs = ob.read_cfg(ob.find_cfg(profile))
    return dict(secs.get("Machine", []))


def is_at(profile):
    """True for a profile whose CPU is a 286 or better: an AT, with a CMOS
    and a 500 kbps floppy controller. An XT has neither."""
    return profile_cfg(profile).get("cpu_family", "8088") not in _XT_CPUS


def _cards_of(n):
    if "both" in n:                     # `_mono`: the Hercules is primary
        return ["hercules", "cga"] if "mono" in n else ["cga", "hercules"]
    if "vga_herc" in n:
        return ["vga", "hercules"]
    if "vga_mda" in n:
        return ["vga", "mda"]
    if "herc" in n:
        return ["hercules"]
    if "ega" in n:
        return ["ega"]
    if "vga" in n:
        return ["oti067"]
    return ["cga"]


def pick_profile(machine, images=()):
    """THE vm/ PROFILE a kern_big or kern_small row's machine name is run on
    (see the module note): an XT unless the name or a floppy needs an AT."""
    n = (machine or "").lower()
    if "_ps2" in n or "at386" in n or "_386" in n:
        return PS2_PROFILE
    if "at286" in n or "_286" in n or "_ne2k" in n:
        return AT_PROFILE
    for img in images:
        if img and os.path.exists(img) and \
                os.path.getsize(img) not in _XT_FD:
            return AT_PROFILE           # a 1.2MB or 1.44MB floppy
    return XT_PROFILE


def machine_config(machine, mega=False, images=()):
    """What MartyPC machine NAME has, as 86Box config lines: (profile,
    [(section, key, value)], [card, ...], {"display": 0x14 bits, "snd": name
    or None, "at": bool}). See the module note for the table. MEGA keeps
    the kern_mega backend's lines exactly as they were (its CMOS cache is
    keyed on them)."""
    n = (machine or "").lower()
    profile = (_env("VM", True) or MEGA_PROFILE) if mega else \
        (_env("VM") or pick_profile(machine, images))
    at = is_at(profile)
    cards = _cards_of(n)
    sets = [("Video", "gfxcard", cards[0]),
            ("Video", "gfxcard_2", cards[1] if len(cards) > 1 else None)]
    snd = None
    if "_sb" in n or _env("SND", mega):
        snd = _env("SND", mega) or "sb2.0"
        kind, _, irq = snd.partition(":")
        sec = {"sb2.0": "Sound Blaster v2.0 #1",
               "sb16": "Sound Blaster 16 #1"}.get(kind)
        if sec is None or (kind == "sb16" and not at):
            raise om.MartyError("%s: the sound card %r is neither sb2.0 nor "
                                "(on an AT) sb16" % (TAG, snd))
        sets += [("Sound", "sndcard", kind), (sec, "base", "0220"),
                 (sec, "irq", irq or ("7" if kind == "sb2.0" else "5")),
                 (sec, "dma", "1")]
        if kind == "sb16":
            sets.append((sec, "dma16", "5"))
    if "_ne2k" in n or os.environ.get("OS88_BOCHS_NE2K"):
        if at:
            sets += [("Network", "net_01_card", "ne2k"),
                     ("Network", "net_01_net_type", "slirp"),
                     ("Network", "net_01_link", "0"),
                     ("NE2000 #1", "base", "0300"), ("NE2000 #1", "irq", "10")]
        else:                           # an 8-bit card in an XT's slot
            sets += [("Network", "net_01_card", "ne1k"),
                     ("Network", "net_01_net_type", "slirp"),
                     ("Network", "net_01_link", "0"),
                     ("Novell NE1000 #1", "base", "0300"),
                     ("Novell NE1000 #1", "irq", "3")]
    if not mega:
        # THE MOUSE the name asks for (the 386's profile carries a PS/2 one)
        # and the XT's RAM; kern_mega's machine keeps its profile's own
        sets.append(("Input devices", "mouse_type",
                     "ps2" if "_ps2" in n else "msserial"))
        if "_ps2" in n:         # ...and NO UART, as QEMU's `-serial none`:
            sets += [("Ports (COM & LPT)", "serial1_enabled", "0"),  # the
                     ("Ports (COM & LPT)", "serial2_enabled", "0")]  # aux
            # port is then the only pointing device the probe can find
        k = re.search(r"_(\d+)k(?:_|$)", n)
        if k and not at:
            sets.append(("Machine", "mem_size", k.group(1)))
    # the CMOS equipment byte's display bits: EGA/VGA 00, CGA 80 col 10,
    # monochrome 11 - what the PRIMARY is, or an AT BIOS stops at POST
    disp = {"cga": 0x20, "hercules": 0x30, "mda": 0x30}.get(cards[0], 0x00)
    return profile, sets, cards, {"display": disp, "snd": snd, "at": at}


# an AT's drive for an image (86Box's type, the CMOS's type) and an XT's
_FDTYPE = {368640: ("525_2hd", 2), 1228800: ("525_2hd", 2),
           737280: ("35_2hd", 4), 1474560: ("35_2hd", 4)}
_XT_FD = {368640: "525_2dd", 737280: "35_2dd"}


def fd_type(img, at):
    """86Box's fdd_0N_type for IMG in a drive of this machine's"""
    size = os.path.getsize(img)
    if at:
        return _FDTYPE.get(size, ("35_2hd",))[0]
    if size not in _XT_FD:
        raise om.MartyError("%s: an XT drives a 360KB or a 720KB floppy, and "
                            "%s is %d bytes - name an AT (at286)"
                            % (TAG, img, size))
    return _XT_FD[size]


def _cmos_types(a, b):
    # (a drive with NO image is still a drive - MartyPC's `_hdd`
    # machines keep theirs, empty - so it is a 1.44MB one)
    ta = _FDTYPE.get(os.path.getsize(a), (None, 4))[1] if a else 4
    tb = _FDTYPE.get(os.path.getsize(b), (None, 4))[1] if b else 4
    return ta, tb


def _cmos_sum(c):
    s = sum(c[0x10:0x2E])
    c[0x2E], c[0x2F] = s >> 8 & 0xFF, s & 0xFF


def cmos_seed(profile, a, b, display):
    """The profile's seeded CMOS (vm/<profile>/nvr-seed/), with the floppy
    types (0x10), the equipment byte's display bits (0x14) and the checksum
    (0x2E-0x2F, over 0x10-0x2D) made to agree with THIS machine. {} when the
    profile carries no seed."""
    d = os.path.join(ROOT, "vm", profile, "nvr-seed")
    out = {}
    for f in sorted(os.listdir(d)) if os.path.isdir(d) else ():
        c = bytearray(open(os.path.join(d, f), "rb").read())
        if len(c) == 128 and f == "mr286.nvr":
            ta, tb = _cmos_types(a, b)
            c[0x10] = ta << 4 | tb
            c[0x14] = (c[0x14] & ~0x30) | display
            _cmos_sum(c)
        out[f] = bytes(c)
    return out


def _cmos_fix(c, a, b, display):
    """A STANDARD AT CMOS made to agree with this machine, for a BIOS whose
    own defaults were saved (Phoenix's): both floppy types, the equipment
    byte's drive present/count and display bits, the checksum."""
    ta, tb = _cmos_types(a, b)
    c[0x10] = ta << 4 | tb
    c[0x14] = (c[0x14] & 0x0E) | 0x01 | 0x40 | display
    _cmos_sum(c)




def _setup(bx, mono, hdd=False, guest_ms=20000):
    """The priming boot's way through MR BIOS: POST's *"Press Any Key"*
    takes it into SETUP, and F10 there records what it found. A machine
    whose PRIMARY is a monochrome card (`both_gla_mono`: the Hercules first)
    also has its Video page's *Primary Video* stepped to *Monochrome* -
    detection prefers the colour card, and a BIOS that initialised the CGA
    hands os8088 an equipment word saying so (kernel/viddet.inc's .equip),
    so the kernel would take the CGA for its primary. A machine with a
    FIXED DISK has C: made type 47 and its geometry IDENTIFIED off the drive
    (the Fixed-Disk page's `?`): no listed type is a VHD's 615/4/26, and a
    BIOS translating at a type's 17 sectors would read the wrong sectors.
    Each page is found by pressing Right until its text is on the screen."""
    def text():
        return bx.screen_text() or ""

    def page(word):
        for _ in range(12):
            if word in text():
                return
            bx.tap("right")
            bx.cmd("step 300")
        raise SystemExit("os88box86api: MR BIOS setup has no %r page" % word)

    def keys(*ks):
        for k in ks:
            bx.tap(k)
            bx.cmd("step 300")
    t0 = bx.ms()
    while bx.ms() - t0 < guest_ms:
        t = text()
        if "Summary   Energy" in t:
            break
        if "Press Any Key" in t:
            bx.tap("esc")
        bx.cmd("step 250")
    else:
        return                          # no setup at all: nothing to record
    if hdd:
        page("Fixed Disk 80")
        keys("down", "return", "4", "7", "return", "down", "shift+/")
        if "Type .......... 47" not in text():
            raise SystemExit("os88box86api: MR BIOS would not take C: as an "
                             "identified type 47")
    if mono:
        page("Primary Video")
        bx.tap("down")
        bx.cmd("step 300")
        for _ in range(4):
            if "Monochrome" in text():
                break
            bx.tap("space")
            bx.cmd("step 300")
        if "Monochrome" not in text():
            raise SystemExit("os88box86api: MR BIOS's Video page offers no "
                             "Monochrome primary")
    for _ in range(4):                  # out of the fields: F10 is the MENU's
        if "Summary   Energy" in text():
            break
        keys("esc")
    bx.tap("f10")


def _setup_phoenix(bx, guest_ms=40000):
    """The PRIMING boot's way through a fresh Phoenix 80386 ROM BIOS PLUS
    1.10 (vm/386-ps2's Packard Bell): *"Invalid configuration ... F1 to
    continue, F2 to run the setup utility"*, F2, any key past its list of
    what it corrected, Esc to the exit menu, F4 - save and reboot. What it
    saves is its own defaults (one 1.2MB A:, no B:, CGA80), which
    cmos_primed then makes agree with this machine byte by byte (_cmos_fix);
    every page of a Phoenix setup is +/- over fields, which is not worth
    driving for three bytes the standard layout pins."""
    def until(word, key, booted=False):
        t0 = bx.ms()
        while bx.ms() - t0 < guest_ms:
            t = bx.screen_text()
            if word in (t or ""):
                bx.tap(key)
                bx.cmd("step 500")
                return True
            if booted and t is None and bx.ms() - t0 > 3000:
                return False                # a graphics mode: os8088's splash
            bx.cmd("step 250")
        return False
    if not until("F2 to run the setup", "f2", booted=True):
        return False                        # no complaint: nothing to save
    for word, key in (("Hit any Key", "space"),
                      ("Standard System Parameters", "esc"),
                      ("Exiting SETUP", "f4")):
        if not until(word, key):
            raise SystemExit("%s: Phoenix setup never showed %r"
                             % (TAG, word))
    return True


def cmos_primed(profile, a, b, sets, nvr, mono=False, c=None, kmap=None,
                display=0):
    """THE CMOS THIS MACHINE'S BIOS ACCEPTS WITHOUT A WORD. An XT has none
    ({}). The mr286's seed is the VGA machine's; a CGA, a Hercules (whose
    card carries an LPT port), an SB, a fixed disk each make MR BIOS stop at
    POST on *"... Configuration Change - Press Any Key"* and then record the
    new configuration. Phoenix (vm/386-ps2) has no seed at all and stops on
    an invalid one. So the first boot of each configuration is a PRIMING
    boot, held, answering those prompts (BIOS_PROMPTS) up to os8088's
    desktop, and what 86Box saves at the power-off is cached under
    build/86box/nvrcache/ by a hash of the configuration and the seed: every
    later launch of the same machine POSTs straight through, as MartyPC's
    does, so a row launched paused at power-on (`boot=False`) runs the BIOS
    with nobody to answer it. The shutdown byte (0Fh, which kern_mega's 286
    reset path writes) is cleared, so a power-on is never taken for a warm
    reset. KMAP is the booted kernel's map, for the desktop gate."""
    import hashlib
    bios = profile_cfg(profile).get("machine")
    phoenix = bios == "pbl300sx"
    if not nvr and not phoenix:
        return nvr
    h = hashlib.sha1(json.dumps([profile, sets, mono], sort_keys=True).encode())
    for f in sorted(nvr):
        h.update(f.encode() + nvr[f])
    if phoenix:                         # what _cmos_fix writes depends on
        h.update(json.dumps(_cmos_types(a, b) + (display,)).encode())
    d = os.path.join(ROOT, "build", "86box", "nvrcache", h.hexdigest()[:16])
    names = list(nvr) or ["%s.nvr" % bios]
    if os.path.isdir(d) and all(os.path.exists(os.path.join(d, f))
                                for f in names):
        return {f: open(os.path.join(d, f), "rb").read() for f in names}
    import os88build
    kmap = kmap or os.path.join(os88build.mega_build(), "megak",
                                "kernel.map")
    # PHOENIX TAKES ROUNDS: the first saves its defaults, which _cmos_fix
    # then makes agree with the drives and the card - and the next POST may
    # still find the memory it counts (shadowing moves it) unlike the
    # defaults' figure, which only its own setup rewrites. So a round that
    # met the complaint saves again, until one boots without it
    cur = nvr
    for rnd in range(4 if phoenix else 1):
        name = "prime-%s-%d-%d" % (h.hexdigest()[:8], os.getpid(), rnd)
        bx = ob.launch(profile, name=name, hold=True, fresh=True, a=a, b=b,
                       c=c, cfgs=sets, temp=True, nvr=cur, eject_a=not a,
                       kmap=kmap)
        try:
            if phoenix:
                complained = _setup_phoenix(bx)
            else:
                _setup(bx, mono, hdd=bool(c))
            bx.kernel_up(guest_ms=240000, prompts=BIOS_PROMPTS, idle=False)
        except SystemExit as e:
            bx.quit(remove=True)
            raise om.MartyError("%s: the priming boot of this machine never "
                                "reached the desktop: %s" % (TAG, e))
        bx.quit()
        out = {}
        for f in os.listdir(os.path.join(bx.run, "nvr")) if phoenix else nvr:
            cm = bytearray(open(os.path.join(bx.run, "nvr", f), "rb").read())
            if len(cm) >= 128:
                cm[0x0F] = 0
                if phoenix:
                    _cmos_fix(cm, a, b, display)
            out[f] = bytes(cm)
        shutil.rmtree(bx.run, ignore_errors=True)
        cur = out
        if not phoenix or (rnd and not complained):
            break
    else:
        raise om.MartyError("%s: %s's BIOS still complained about its CMOS "
                            "after %d setup rounds" % (TAG, profile, rnd + 1))
    # WRITTEN ATOMICALLY, because two rows priming one machine is the
    # ordinary case now that 86Box rows share the emulator lane
    # (docs/plans/SOAK-PARALLEL.md 18): the reader above takes the cache the
    # moment every NAME exists, and a plain `open(..., "wb")` makes the name
    # before the bytes - so the second row could boot a truncated CMOS. Each
    # writer stages under its own pid and renames; both write the same bytes,
    # so whichever rename lands last is as good as the first
    os.makedirs(d, exist_ok=True)
    for f, cm in out.items():
        tmp = os.path.join(d, ".%s.%d" % (f, os.getpid()))
        with open(tmp, "wb") as fh:
            fh.write(cm)
        os.replace(tmp, os.path.join(d, f))
    return out


# --- the GDB stub ------------------------------------------------------------

class Gdb(object):
    """The smallest remote-protocol client that does what this needs: Z1/z1,
    P, c, s. Every packet the stub sends is acked by the reader thread (the
    stub writes nothing more until it is), stop replies (T../S..) go to
    `on_stop`, everything else to the request waiting for it."""

    def __init__(self, port, on_stop):
        self.s = socket.create_connection(("127.0.0.1", port), timeout=10)
        # NO NAGLE: every packet here is a few bytes followed by a one-byte
        # `+`, and the stub sends nothing more until it is acked - so with
        # Nagle's coalescing against the host's delayed ACK each round trip
        # stalled ~40 ms (a port read was 6.6 s of them, 0.6 s without)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.s.settimeout(None)
        self.on_stop = on_stop
        self.replies = queue.Queue()
        self.wlock = threading.Lock()
        self.dead = False
        threading.Thread(target=self._reader, daemon=True).start()
        self.s.sendall(b"+")

    def _reader(self):
        buf = b""
        try:
            while True:
                d = self.s.recv(65536)
                if not d:
                    break
                buf += d
                while buf:
                    c = buf[:1]
                    if c != b"$":
                        buf = buf[1:]
                        continue
                    i = buf.find(b"#")
                    if i < 0 or len(buf) < i + 3:
                        break
                    body, buf = buf[1:i].decode("latin-1"), buf[i + 3:]
                    with self.wlock:
                        self.s.sendall(b"+")
                    if body[:1] in ("T", "S"):
                        self.on_stop(body)
                    else:
                        self.replies.put(body)
        except OSError:
            pass
        self.dead = True

    def send(self, body):
        pkt = "$%s#%02x" % (body, sum(body.encode("latin-1")) & 0xFF)
        with self.wlock:
            self.s.sendall(pkt.encode("latin-1"))

    def req(self, body, timeout=15.0):
        self.send(body)
        try:
            r = self.replies.get(timeout=timeout)
        except queue.Empty:
            raise om.MartyError("os88box86api: the GDB stub never answered %r"
                                % body)
        if r.startswith("E"):
            raise om.MartyError("os88box86api: the GDB stub refused %r: %s"
                                % (body, r))
        return r

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


GDB_REGS = {"ax": 0, "cx": 1, "dx": 2, "bx": 3, "sp": 4, "bp": 5, "si": 6,
            "di": 7, "ip": 8, "flags": 9, "cs": 10, "ss": 11, "ds": 12,
            "es": 13, "fs": 14, "gs": 15}


# --- the machine ---------------------------------------------------------------

class Api86Box(om.Marty):
    """`os88marty.Marty` - the TEST API, not the emulator - with the debug
    server replaced by 86Box: see the module note. `m.b` is the os88box.Box
    underneath. Real mode throughout; tools/os88mega86box.py's Mega86Box is
    this with kern_mega's tables on top."""

    box86 = True
    mega = False

    def __init__(self, b, run_dir, cards, label=None):
        self.b = b
        self.run_dir = run_dir
        self.cards_ = cards
        self.addr = "86box:%s" % b.name
        self.port = None
        st = ob.state(b.name) or {}
        self.pid = st.get("pid")
        self._gdbport = st.get("gdb")
        self._proc = None
        self._rec = None
        self._lock = threading.RLock()      # the automation socket
        self._log = []
        self._go = None
        self._pumping = 0
        self._planes_verb = False
        self._pipeline = False
        self.label = label
        self._state = "paused"
        self._stops = 0
        self._ms0 = None
        self._ms = 0
        self._at = (0, 0, 0)
        self._gdb = None
        self._bps = set()
        self._stepq = None
        self._closed = False
        self._src = {}
        self._wav = None

    # ---- the automation socket ---------------------------------------------
    def _a(self, line):
        with self._lock:
            r = self.b.cmd(line)
        if "ms" in r:
            self._ms = r["ms"]
            if self._ms0 is None:
                self._ms0 = self._ms
        if r.get("stopped") == "debugger" and self._stepq is None:
            self._hit()
        return r

    def _cycles(self):
        return int((self._ms - (self._ms0 or 0)) * GUEST_HZ / 1000.0)

    def _clock(self):
        """MegaMixin's waits (the boot toast) in GUEST seconds: the machine
        runs fast-forwarded, so ten host seconds are minutes of it"""
        if self._state == "running":
            self._a("status")
        return self._ms / 1000.0

    # ---- running and stopping ----------------------------------------------
    def _release(self):
        self._a("release")
        self._state = "running"

    def _hold(self):
        self._a("hold")

    def _hit(self, body=None):
        """A stop the GDB stub made: a breakpoint. The machine is HELD so a
        resume decides when it runs again."""
        with self._lock:
            if self._state != "running":
                return
            self.b.cmd("hold")
            self._state = "breakpoint"
            self._stops += 1
            self._regs_at()

    def _on_stop(self, body):
        if self._stepq is not None:
            self._stepq.put(body)
            return
        threading.Thread(target=self._hit, args=(body,), daemon=True).start()

    def _regs_at(self):
        r = self.b.cmd("regs")
        self._at = (r["cs"], r["eip"] & 0xFFFF, r["cs_base"] + r["eip"])
        return r

    def gdb(self):
        """The stub, connected on first use. Connecting STOPS the CPU (the
        stub's own rule) and the `c` sent straight after lets it go - with
        the machine held, so nothing ran in between."""
        if self._gdb is None:
            with self._lock:
                was = self._state
                if was == "running":
                    self.b.cmd("hold")
                self._gdb = Gdb(self._gdbport, self._on_stop)
                self._gdb.send("c")
                if was == "running":
                    self.b.cmd("release")
        return self._gdb

    # ---- memory ---------------------------------------------------------------
    def _read(self, addr, n):
        out = bytearray()
        while n:
            k = min(n, 65536)
            out += bytes.fromhex(self._a("memr 0x%X %d" % (addr, k))["data"])
            addr += k
            n -= k
        return bytes(out)

    def _write(self, addr, data):
        for i in range(0, len(data), 1024):
            self._a("memw 0x%X %s" % (addr + i, data[i:i + 1024].hex()))
        return len(data)

    # ---- the server -----------------------------------------------------------
    def cmd(self, **kw):
        c = kw.pop("cmd")
        f = getattr(self, "_c_" + c, None)
        if f is None:
            raise om.MartyError(
                "`%s` is a MartyPC instrument the 86Box backend does not have "
                "(tools/os88box86api.py) - this row is not portable as it "
                "stands" % c)
        r = dict(f(**kw) or {})
        r["ok"] = True
        return r

    def _status(self):
        if self._state == "running":
            self._a("status")
        cs, ip, flat = self._at
        return {"state": self._state, "cycles": self._cycles(),
                "stops": self._stops, "cs": cs, "ip": ip, "flat_ip": flat}

    def _c_ping(self, **_):
        return {"pipeline": False}

    def _c_status(self, **_):
        return self._status()

    def _c_read(self, addr, len, **_):          # noqa: A002
        return {"data": self._read(addr, len).hex()}

    def _c_write(self, addr, data, **_):
        return {"written": self._write(addr, bytes.fromhex(data))}

    def _c_run(self, **_):
        frm = self._stops
        if self._state == "breakpoint":
            self.gdb().send("c")        # the stop just left is not re-hit
        if self._state != "running":
            self._release()
        st = self._status()
        st["resumed_from"] = frm
        return st

    def _c_pause(self, **_):
        with self._lock:
            if self._state == "running":
                self.b.cmd("hold")
                self._state = "paused"
                self._stops += 1
                self._regs_at()
        return self._status()

    def _gdb_stop(self, what, max_ms=200):
        """Run the HELD machine a millisecond at a time until the stub
        reports the stop a `s` or a breakpoint asked for; answer its packet.
        A stop is not always in the first millisecond (the packet is taken
        between two of the CPU's chunks), so it is polled for, on the
        emulated clock."""
        for _ in range(max_ms):
            r = self.b.cmd("step 1")
            try:
                return self._stepq.get(
                    timeout=5 if r.get("stopped") == "debugger" else 0.004)
            except queue.Empty:
                continue
        raise om.MartyError("%s: %s never stopped in %d emulated ms"
                            % (TAG, what, max_ms))

    def _where(self):
        r = self.b.cmd("regs")
        return r["cs"], r["eip"]

    def _c_step(self, n=1, over=False, **_):
        """N INSTRUCTIONS, through the stub's single step. A `s` can stop
        with nothing executed, so each instruction is a `s` until CS:IP
        moves - four at most. AT A HLT IT NEVER MOVES: 86Box's timers run on
        the CPU's cycles, so a CPU stepped a cycle at a time never reaches
        the interrupt that ends the HLT (`_stub` is how this module gets
        past one: a breakpoint on IRQ0's handler)."""
        self._c_pause()
        g = self.gdb()
        with self._lock:
            self._stepq = queue.Queue()
            try:
                for _ in range(n):
                    was = self._where()
                    for _try in range(4):
                        g.send("s")
                        self._gdb_stop("a single step")
                        if self._where() != was:
                            break
                g.send("c")             # ...and the stub back to running,
            finally:                    # the machine still held
                self._stepq = None
            self._state = "paused"
            self._stops += 1
            self._regs_at()
        return self._status()

    def _c_advance(self, frames=None, cycles=None, card=None, **_):
        ms = (cycles * 1000.0 / GUEST_HZ if cycles is not None else
              (frames or 1) * 1000.0 / 60.0)
        if self._state == "breakpoint":
            self.gdb().send("c")
        with self._lock:
            self.b.cmd("hold")
            self._state = "running"
            r = self._a("step %d" % max(1, int(round(ms))))
            if r.get("stopped") != "debugger":
                self._state = "paused"
                self._stops += 1
                self._regs_at()
        return self._status()

    def _c_bp(self, list, **_):                 # noqa: A002
        want = set()
        for b in list:
            t = b.get("type")
            if t == "exec":
                want.add(int(b["addr"]))
            elif t == "execseg":
                want.add(self.lin(int(b["seg"]), int(b["off"])))
            elif t == "int":
                want.add(self._vector(int(b["addr"])))
            else:
                raise om.MartyError("os88box86api: a %r breakpoint is not "
                                    "available on 86Box" % t)
        if want or self._bps:
            g = self.gdb()
            for a in sorted(self._bps - want):
                g.req("z1,%x,1" % a)
            for a in sorted(want - self._bps):
                g.req("Z1,%x,1" % a)
            self._bps = want
        return {"count": len(want)}

    def _c_regs(self, **_):
        r = self._a("regs")
        out = {n: r["e" + n] & 0xFFFF for n in ("ax", "bx", "cx", "dx", "sp",
                                                "bp", "si", "di")}
        out["ip"] = r["eip"] & 0xFFFF
        out["flags"] = r["eflags"] & 0xFFFF
        for s in ("cs", "ds", "es", "ss", "fs", "gs"):
            out[s] = r[s]
        return out

    def _setregs(self, vals):
        """GDB `P` writes, the 16-bit general registers merged into their
        32-bit ones and IP made linear (the stub's EIP is CS's base + IP)."""
        g = self.gdb()
        r = self._a("regs")
        for reg, v in vals:
            if reg in ("ax", "bx", "cx", "dx", "sp", "bp", "si", "di"):
                v = (r["e" + reg] & 0xFFFF0000) | (v & 0xFFFF)
            elif reg == "flags":
                v = (r["eflags"] & 0xFFFF0000) | (v & 0xFFFF)
            elif reg == "ip":
                v = r["cs_base"] + (v & 0xFFFF)
            g.req("P%x=%s" % (GDB_REGS[reg], (v & 0xFFFFFFFF).to_bytes(
                4, "little").hex()))
            if reg == "cs":
                r = self._a("regs")
        self._regs_at()

    def _c_setreg(self, reg, value, **_):
        self._setregs([(reg, value)])
        return {}

    def _c_park(self, cs, ip, **_):
        self._c_pause()
        self._setregs([("cs", cs), ("ip", ip)])
        return {"if_cleared": False}

    def _c_key(self, key, down=True, up=True, **_):
        k = scancode(key)
        if down:
            self._a("key down 0x%X" % k)
        if up:
            self._a("key up 0x%X" % k)
        self._log.append({"cycles": self._cycles(), "kind": "key",
                          "args": {"key": key, "down": down, "up": up}})
        return {"cycles": self._cycles()}

    def _c_mouse(self, dx=0, dy=0, l=False, r=False, wz=0, **_):
        if wz:
            raise om.MartyError("os88box86api: no wheel on the serial mouse")
        self._a("mouse %d %d %d" % (dx, dy, (1 if l else 0) | (2 if r else 0)))
        return {"cycles": self._cycles()}

    # ---- the glass --------------------------------------------------------------
    def _card(self, card):
        i = 0 if card in (None, "pri") else int(card)
        if not 0 <= i < len(self.cards_):
            raise om.MartyError("os88box86api: no video card %r on this "
                                "machine (%s)" % (card, self.cards_))
        return i

    def _video(self, card=None):
        i = self._card(card)
        typ, kind = _CARDS[self.cards_[i]]
        if kind is None:                        # VGA/EGA: the BIOS's mode
            mode = self._read(0x449, 1)[0]
            text = mode in (0, 1, 2, 3, 7)
            fw, fh = (800, 524) if not text else (720, 400)
        else:
            text = False
            mode = 0x06 if kind == "cga" else 0x07
            fw, fh = (640, 200) if kind == "cga" else (720, 348)
        return {"idx": i, "type": typ, "primary": i == 0,
                "mode": "Mode%02X" % mode, "graphics": not text,
                "field_w": fw, "field_h": fh, "frames": 1}

    def _c_video(self, card=None, **_):
        return self._video(card)

    def _c_cards(self, **_):
        return {"cards": [self._video(i) for i in range(len(self.cards_))]}

    def video(self, card=None):
        return self._video(card)

    def _c_screen(self, card=None, **_):
        try:
            t = self._a("text")
        except SystemExit:
            return {"rows": []}
        return {"rows": [bytes(b if b >= 32 else 32 for b in bytes.fromhex(
            x)).decode("cp437").rstrip() for x in t.get("chars", [])]}

    def _c_fbuf(self, aperture=0, card=None, **_):
        from PIL import Image
        i = self._card(card)
        p = os.path.join(self.run_dir, "fbuf%d.png" % i)
        self._a("%s %s" % ("screenshot" if i == 0 else "screenshot1", p))
        im = Image.open(p).convert("RGB")
        return {"w": im.width, "h": im.height, "data": im.tobytes().hex()}

    def _c_disk(self, reset=False, **_):
        """MartyPC's FDC counters: READ DATA and WRITE DATA commands the
        controller completed (tools/86box/patches/06), since launch or the
        last `reset=True`"""
        r = self._a("fdc")
        base = getattr(self, "_fdc0", (0, 0))
        out = {"reads": r["reads"] - base[0], "writes": r["writes"] - base[1]}
        if reset:
            self._fdc0 = (r["reads"], r["writes"])
        return out

    def _c_mount(self, drive, path, **_):
        """A floppy swap under a running machine: a COPY of PATH is loaded
        into the drive, as MartyPC mounts a private copy; `disks` names PATH
        as mounted from then on, and `flush` writes the copy back"""
        n = len([f for f in os.listdir(self.b.run) if f.startswith("swap")])
        dst = os.path.join(self.b.run, "swap%d-%s" % (n, os.path.basename(path)))
        shutil.copyfile(path, dst)
        self._a("fdd load %d %s" % (drive, dst))
        self._live[drive] = dst
        self._src[drive] = os.path.join(self.run_dir, "%s-mounted%d.img"
                                        % ("ab"[drive], n))
        shutil.copy(path, self._src[drive])
        return {}

    def _c_reset(self, **_):
        self._a("reset hard")
        self._release()
        return self._status()

    def _c_quit(self, **_):
        self.close()
        return {}

    def vram(self, kind=None):
        """Marty.vram, and on a VGA (no flat framebuffer) the glass instead -
        lit where not black, as tools/os88megabochs.py answers it."""
        if kind is None and _CARDS[self.cards_[0]][1] is None:
            w, h, px = self.fbuf()
            return w, h, [[1 if (px[(y * w + x) * 3] | px[(y * w + x) * 3 + 1]
                                 | px[(y * w + x) * 3 + 2]) else 0
                           for x in range(w)] for y in range(h)]
        return om.Marty.vram(self, kind)

    def disks(self):
        return [{"drive": d, "path": self._src.get(d, self._live.get(d))}
                for d in (0, 1)]

    def flush(self, drive=0, path=None, fmt="raw"):
        src = self._live[drive]
        dst = path or self._src.get(drive)
        if dst:
            shutil.copy(src, dst)
        return {"path": dst or src}

    def faults(self):
        """The CPU's exceptions and resets so far (tools/86box/patches/01),
        as `os88box.py NAME faults` prints them - the BIOS's own hidden."""
        import contextlib
        import io
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ob.faults(self.b.name, False, self.b.kmap())
        return out.getvalue().splitlines()

    def close(self):
        if self._closed:
            return
        self._closed = True
        try:
            if self._gdb:
                self._gdb.close()
        except Exception:                       # noqa: BLE001
            pass
        try:
            self.b.quit()
        except BaseException:                   # noqa: BLE001
            pass
        if self.pid and ob.alive(self.pid):     # a quit it ignored: ours,
            try:                                # so it goes
                os.kill(self.pid, 9)
            except OSError:
                pass
        keep = os.environ.get("OS88_86BOX_KEEP") or os.environ.get(
            "OS88_MEGA_KEEP")
        if keep:
            print("os88box86api: kept %s and %s" % (self.b.run, self.run_dir),
                  file=sys.stderr)
        else:
            shutil.rmtree(self.b.run, ignore_errors=True)
            shutil.rmtree(self.run_dir, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ---- real mode ----------------------------------------------------------
    def protected(self):
        return bool(self._a("regs")["cr0"] & 1)

    def lin(self, seg, off=0):
        """SEG:OFF's linear address - `seg << 4` in real mode (MegaMixin's
        selector lookup replaces this under kern_mega)"""
        return ((seg & 0xFFFF) << 4) + off

    def _vector(self, v):
        """Vector V's handler, linear: the IVT's (Mega86Box: the IDT's in
        protected mode)."""
        g = self._read(v * 4, 4)
        return ((g[2] | g[3] << 8) << 4) + (g[0] | g[1] << 8)

    def _kmap(self):
        return self.b.kmap()

    def _entered(self):
        """THE KERNEL IS IN MEMORY - the poker's first gate, before any of
        the kernel's own words is believed: an AT's POST and a memory test
        leave patterns in low memory that can read as a desktop. Here: 32
        bytes of `font_run`'s code where the booted kernel.bin has them
        (`.text` is the file from BOOT2_PAD on, at KERNEL_SEG:0) - code, so
        nothing the kernel writes at run time (Mega86Box: protected mode)."""
        if getattr(self, "_khead", None) is None:
            import os88sym
            self._khead = (0, b"")
            try:
                off = os88sym.syms()["font_run"]
                at = os88sym.equates()["BOOT2_PAD"] + off
                kb = os.path.join(os.path.dirname(self._kmap() or ""),
                                  "kernel.bin")
                with open(kb, "rb") as f:
                    f.seek(at)
                    self._khead = ((om.KERNEL_SEG << 4) + off, f.read(32))
            except (OSError, KeyError):
                pass
        at, head = self._khead
        return bool(head) and self.read(at, len(head)) == head

    def _poker(self, spec):
        """$OS88_86BOX_POKE (kern_mega: $OS88_MEGA_POKE) - a port's way to
        BREAK the thing it checks (box86port.txt's and megaport.txt's "seen
        to go red"): `SYM[+OFF]=HEX;...` written over the kernel once its
        desktop is up (`_entered`, desk_rows and menu_nbar written, [spl_live]
        0, [ticks] moving), so a row needs no knob build to be shown failing.
        86Box's interpreter keeps no decoded copy of code, so a byte written
        is the byte executed; `sym` resolves `.cold` where the kernel runs
        it (a slid one on a mono machine; `.cold`/`.mega` above 1MB under
        kern_mega). Each poke is printed, with the bytes it replaced."""
        import os88sym
        pokes = []
        for p in filter(None, spec.split(";")):
            where, hexb = p.split("=")
            mm = re.match(r"^(.*?)([+-](?:0x[0-9a-fA-F]+|\d+))?$", where)
            pokes.append((mm.group(1), int(mm.group(2) or "0", 0),
                          bytes.fromhex(hexb)))
        tk = None
        while not self._closed:
            try:
                up = (self._entered()
                      and self.read(os88sym.linear("desk_rows"), 2) != b"\0\0"
                      and self.read(os88sym.linear("menu_nbar"), 2) != b"\0\0"
                      and not self.read(os88sym.linear("spl_live"), 1)[0])
                t = self.read(os88sym.linear("ticks"), 2)   # [ticks] MOVING
                up, tk = up and tk is not None and t != tk, t
            except Exception:                   # noqa: BLE001
                up = False
            if up:
                break
            time.sleep(0.05)
        for name, off, data in pokes:
            try:
                a = self.sym(name) + off
            except Exception as e:              # noqa: BLE001
                print("%s: POKE %s: %s" % (TAG, name, e), flush=True)
                continue
            old = self.read(a, len(data))
            self.write(a, data)
            print("%s: POKE %s+%#x (%#x): %s -> %s"
                  % (TAG, name, off, a, old.hex(), data.hex()), flush=True)

    # ---- ports: a stub run between two breakpoints --------------------------
    _STUB_AT = 0x500            # 0050:0000, the BIOS's data area past 0040:00FF

    def _run_to(self, addr, what, max_ms=1000):
        """Run the held machine until its CPU reaches linear ADDR (a hardware
        breakpoint armed for the run and lifted after)."""
        g = self.gdb()
        g.req("Z1,%x,1" % addr)
        try:
            t0 = self.b.ms()
            while True:
                g.send("c")
                left = max_ms - (self.b.ms() - t0)
                if left <= 0:
                    raise om.MartyError("%s: %s was not reached in %d "
                                        "emulated ms" % (TAG, what, max_ms))
                self._gdb_stop(what, left)
                r = self.b.cmd("regs")
                if r["cs_base"] + r["eip"] == addr:
                    return r
                # ...a STALE stop - a step's that arrived after its wait
                # was over - is not this one: go on
        finally:
            g.req("z1,%x,1" % addr)

    def _stub(self, code, regs=(), save=("ax", "bx", "cx", "dx"), out=0):
        """Run CODE at 0050:0000 to its end with REGS ((name, value), ...)
        set and IF clear, and answer (the registers after it, OUT bytes from
        0050:0040). REAL MODE ONLY. The machine is left as it was found: the
        256 bytes under the stub, SAVE's registers, CS:IP and FLAGS put
        back, the row's breakpoints lifted for the run and re-armed, the run
        state restored.

        A SINGLE STEP CANNOT DO THIS, which is why it is two breakpoints: a
        held machine is usually in the idle task's HLT, a `s` there executes
        nothing - 86Box's timers run on the CPU's cycles, so a CPU stepped a
        cycle at a time never reaches the interrupt that would end it - and
        the 8088 core's halt survives a CS:IP write. So unless it is already
        stopped at a breakpoint, the CPU is first run to IRQ0's handler (at
        most a tick, 55 ms of guest time): an instruction boundary inside an
        interrupt gate, with IF clear, where the stub then runs and from
        which the handler carries on as if nothing had happened."""
        end = self._STUB_AT + len(code)
        with self._lock:
            was = self._state
            if was == "running":
                self.b.cmd("hold")
            r0 = self._a("regs")
            if r0["cr0"] & 1:
                if was == "running":
                    self.b.cmd("release")
                raise om.MartyError("%s: a port stub runs in REAL mode, and "
                                    "the CPU is in protected mode" % TAG)
            g = self.gdb()
            rows = sorted(self._bps)
            for a in rows:
                g.req("z1,%x,1" % a)
            self._stepq = queue.Queue()
            keep, back = None, None
            try:
                if was != "breakpoint":
                    self._run_to(self._vector(8), "IRQ0's handler", 2000)
                r = self._a("regs")
                back = [("cs", r["cs"]), ("ip", r["eip"] & 0xFFFF),
                        ("flags", r["eflags"] & 0xFFFF)] + \
                    [(n, r["e" + n] & 0xFFFF) for n in save]
                keep = self._read(self._STUB_AT, 256)
                self._write(self._STUB_AT, code)
                self._setregs([("cs", self._STUB_AT >> 4), ("ip", 0),
                               ("flags", r["eflags"] & 0xFDFF)] + list(regs))
                self._run_to(end, "the port stub")
                after = self._a("regs")
                data = self._read(self._STUB_AT + 0x40, out) if out else b""
            finally:
                if keep is not None:
                    self._write(self._STUB_AT, keep)
                if back is not None:
                    self._setregs(back)
                for a in rows:
                    g.req("Z1,%x,1" % a)
                g.send("c")             # the stub running, the machine held
                self._stepq = None
                self._regs_at()
                if was == "running":
                    self.b.cmd("release")
        return after, data

    def _c_inb(self, port, **_):
        r, _d = self._stub(b"\xEC", [("dx", port & 0xFFFF)])     # in al, dx
        return {"value": r["eax"] & 0xFF}

    def _c_outb(self, port, value, **_):
        self._stub(b"\xEE", [("dx", port & 0xFFFF),               # out dx, al
                             ("ax", value & 0xFF)])
        return {}

    # xor bx, bx / L: mov al, bl / out 70h, al / in al, 71h /
    # mov [cs:bx+40h], al / inc bx / cmp bl, 80h / jne L
    _CMOS_STUB = bytes.fromhex("31db" "88d8" "e670" "e471" "2e884740"
                               "43" "80fb80" "75f0")

    @property
    def cmos(self):
        """`m.cmos()`: the MC146818's 128 bytes, read through ports 70h/71h by
        one stub run - the chip as the HARDWARE holds it, which no guest read
        can tell from the kernel's own copy (os88megabochs's `cmos` reads
        Bochs's device). AN AT ONLY: on an XT there is no chip, and the
        attribute is ABSENT - `getattr(m, "cmos", None)` is how a row asks
        (tests/dtwrite.py), as it is absent on MartyPC."""
        if not getattr(self, "at", False):
            raise AttributeError("cmos: this machine (%s) has no clock chip"
                                 % getattr(self, "profile", "?"))
        return self._cmos

    def _cmos(self):
        # index bit 7 (NMI) left clear, as the BIOS leaves it
        _r, data = self._stub(self._CMOS_STUB, out=128)
        return data

    def __repr__(self):
        return "<%s %s>" % (type(self).__name__, self.addr)


def _vhd_params(path):
    """86Box's `hdd_01_parameters` (spt, heads, cylinders) for a row's disk:
    a VHD's footer, else os88megacommon's geometry by size"""
    import os88megacommon
    c, h, s = os88megacommon.hd_geometry(path)
    return s, h, c


def kernel_map():
    """The map of the kernel a kern_big or kern_small row boots: $OS88_BUILD's
    (build/smallk for kern_small), as os88sym resolves it, else build/'s"""
    import os88sym
    bdir = os88sym._in_tree(os.environ.get("OS88_BUILD", "")) \
        or os.path.join(ROOT, "build")
    if not os.path.isabs(bdir):
        bdir = os.path.join(ROOT, bdir)
    return os.path.join(bdir, "kernel.map")


def boot_machine(cls, a, b, machine, boot=True, label=None, cimg=None,
                 cfg=None, megs=None, ne2k_io=None, kmap=None, mega=False,
                 images=(), turbo=False):
    """THE LAUNCH BOTH BACKENDS SHARE: A: and B: as given (None leaves a
    drive empty), CIMG the row's own fixed disk mounted in place, CFG a
    SYSTEM.CFG driver mask written onto A:'s copy, MEGS the RAM in MB, KMAP
    the booted kernel's map, MEGA the kern_mega machine (machine_config's).
    BOOT True runs to the desktop and hands the machine back RUNNING; a
    number runs that many guest seconds blind; False hands it back PAUSED at
    power-on. TURBO is os88box's per-drive floppy turbo - a sector costs no
    rotation or seek - for a row written against QEMU, whose floppy is
    instant and whose subject is not the floppy's timing."""
    profile, sets, cards, info = machine_config(
        machine, mega=mega, images=images or (a, b))
    if ne2k_io:
        net = "NE2000 #1" if info["at"] else "Novell NE1000 #1"
        sets.append((net, "base", "%04X" % ne2k_io))
    if cfg is not None and a:
        a = _with_cfg(a, cfg)
    if megs:
        sets.append(("Machine", "mem_size", str(int(float(megs) * 1024))))
    for d, img in (("01", a), ("02", b)):
        if img:
            sets.append(("Floppy and CD-ROM drives", "fdd_%s_type" % d,
                         fd_type(img, info["at"])))
    if cimg:
        s_, h_, c_ = _vhd_params(cimg)
        if not info["at"]:              # an XT: the XT-IDE card's BIOS
            sets.append(("Storage controllers", "hdc_1", "xtide"))
        sets += [("Hard disks", "hdd_01_parameters",
                  "%d, %d, %d, 0, ide" % (s_, h_, c_)),
                 ("Hard disks", "hdd_01_ide_channel", "0:0")]
    tag = re.sub(r"\W+", "", label or os.path.splitext(
        os.path.basename(sys.argv[0] or "row"))[0])[:20]
    base = os.path.join(ROOT, "build", "86box", "inst")
    os.makedirs(base, exist_ok=True)
    rdir = tempfile.mkdtemp(prefix="%s-" % tag, dir=base)
    name = "mp-" + os.path.basename(rdir)
    env = {}
    wav = os.environ.get("MARTYPC_WAV")
    if wav and info["snd"]:
        env["OS88_SBWAV"] = wav + ".sound_blaster.wav"
    nvr = cmos_seed(profile, a, b, info["display"])
    if info["at"]:
        nvr = cmos_primed(profile, a, b, sets, nvr,
                          mono=cards[0] in ("hercules", "mda")
                          and len(cards) > 1, c=cimg, kmap=kmap,
                          display=info["display"])
    try:
        box = ob.launch(profile, name=name, hold=True, fresh=True, a=a, b=b,
                        c=cimg, inplace_c=bool(cimg), cfgs=sets, temp=True,
                        kmap=kmap, nvr=nvr, env=env, eject_a=not a,
                        turbo=turbo)
    except SystemExit as e:
        shutil.rmtree(rdir, ignore_errors=True)
        raise om.MartyError("%s: 86Box did not start: %s" % (TAG, e))
    m = cls(box, rdir, cards, label=label)
    m.profile, m.at = profile, info["at"]
    poke = _env("POKE", mega)
    if poke:
        threading.Thread(target=m._poker, args=(poke,), daemon=True).start()
    m._live = {0: os.path.join(box.run, "fdd1" + os.path.splitext(a)[1])
               if a else None,
               1: os.path.join(box.run, "fdd2" + os.path.splitext(b)[1])
               if b else None}
    for d in (0, 1):                # THE MOUNTED-FROM IMAGES: 86Box writes
        if m._live[d] and os.path.exists(m._live[d]):   # through, so the
            m._src[d] = os.path.join(rdir, "%s-mounted.img" % "ab"[d])  # live
            shutil.copy(m._live[d], m._src[d])          # file is no reference
    try:
        try:                            # THIS TREE'S 86Box: patches 04-06
            m._a("fdc")                 # (the SB capture, `screenshot1`,
        except SystemExit:              # `fdc`) - an older build answers
            raise om.MartyError(        # "unknown command"
                "%s: %s predates tools/86box/patches/04-06 - rebuild it: "
                "python3 tools/os88box.py build" % (TAG, ob.BIN))
        m._a("ff on")
        if cimg and a and not info["at"]:
            _xub_floppy(box)
        m._a("status")
        if boot is False or boot == 0:
            return m
        if boot is True:
            box.kernel_up(guest_ms=240000, prompts=BIOS_PROMPTS)
            m._a("status")
            m._release()
            if getattr(m, "boot_toast", None):
                m.boot_toast()
            return m
        m._release()                    # BLIND for that many GUEST seconds
        end = m._cycles() + float(boot) * GUEST_HZ
        while m.status()["cycles"] < end:
            time.sleep(0.02)
        return m
    except BaseException:
        m.close()
        raise


def _xub_floppy(box, guest_ms=90000):
    """AN XT WITH A FIXED DISK BOOTS A: - which 86Box's XT-IDE card does not:
    its XTIDE Universal BIOS (r631, roms/hdd/xtide/ide_xt.bin) defaults to
    C:, so a row's data disk was booted and refused (*"Not a bootable disk"*).
    MartyPC's `_hdd` machines carry a build that boots the floppy. So the
    held machine is run to the hotkey bar XUB draws while it detects the
    drives (*"A>>FDD [A] C>>HDD [C]"*) and `a` is pressed there, as a user
    would; a row launched paused (`boot=False`) gets it back at that point,
    the boot sector not yet read. With A: empty there is nothing to choose
    and this is not called."""
    t0 = box.ms()
    while box.ms() - t0 < guest_ms:
        if "FDD [A]" in (box.screen_text() or ""):
            box.tap("a")
            box.cmd("step 100")
            return
        box.cmd("step 100")
    raise om.MartyError("%s: the XT-IDE BIOS's boot hotkeys never showed in "
                        "%d emulated s" % (TAG, guest_ms // 1000))


def launch(image, apps=None, machine="", boot=True, label=None, extra=(),
           run_dir=None, cfg=None, megs=None, cimg=None, ne2k_io=None,
           turbo=False, **_):
    """os88marty.launch's shape, on 86Box, for a kern_big or kern_small row
    ($OS88_EMU=86box): THE ROW'S OWN IMAGES - nothing swapped - and its own
    kernel's map (`kernel_map`). `machine` is the row's MartyPC name, mapped
    by machine_config. The rest are os88mega86box.launch's, for
    tests/os88qmpmega.py's QMP server: `cfg` a SYSTEM.CFG driver mask written
    onto A:'s copy (None or "keep": A:'s own stands), `megs` the RAM, `cimg`
    the row's own fixed disk (mounted IN PLACE, as `--mount hd:0:` is),
    `ne2k_io` the card's port, `turbo` the floppies' (boot_machine)."""
    if cimg:
        extra = list(extra or ()) + ["--mount", "hd:0:" + cimg]
    cimg, _cgeom = row_disk(machine, extra, run_dir)
    a = image
    if cimg and image and _blank(image):
        a = None                        # a BLANK A:: the BIOS boots C:
    if cfg == "keep":
        cfg = None
    return boot_machine(Api86Box, a, apps, machine, boot=boot, label=label,
                        cimg=cimg, cfg=cfg, megs=megs, ne2k_io=ne2k_io,
                        kmap=kernel_map(), images=(image, apps),
                        turbo=turbo)


def _with_cfg(a, bits):
    """A copy of A: whose SYSTEM.CFG wants the drivers in BITS - the file
    tools/os88bochs.Box writes (`--cfg`), so the two backends boot the same
    disk. The copy lives until the process exits (86Box copies it again)."""
    d = tempfile.mkdtemp(prefix="o88cfg-", dir=os.path.join(
        ROOT, "build", "86box", "inst"))
    atexit.register(shutil.rmtree, d, True)
    out = os.path.join(d, os.path.basename(a))
    shutil.copyfile(a, out)
    f = os.path.join(d, "SYSTEM.CFG")
    with open(f, "wb") as fh:
        fh.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
                 bytes([1, 2]) + bits.to_bytes(2, "little") + b"\0\0")
    fat = os.path.join(ROOT, "tools", "os88fat.py")
    subprocess.run([sys.executable, fat, "del", out, "SYSTEM.CFG"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([sys.executable, fat, "add", out, f], check=True,
                   stdout=subprocess.DEVNULL)
    return out


# An AT BIOS on the way up: a CMOS that does not match the machine stops at
# POST (tests/rdgcore/rdgwin.py's) - MR BIOS 286, the AMI the 286-525
# profiles carry, and Phoenix's (vm/386-ps2) F1
BIOS_PROMPTS = (("bypass MEMORY test", "esc"), ("to RESUME", "f1"),
                ("Press Any Key", "esc"), ("EXIT FOR BOOT", "return"),
                # ...which takes MR BIOS into SETUP, where F10 records the
                # configuration it found and boots
                ("Summary   Energy", "f10"),
                ("Strike the F1 key to continue", "f1"))


def main(argv):
    """`os88box86api.py IMAGE [APPS] [MACHINE]`: boot one (the row's images,
    kern_big's map) and print what the backend sees - a smoke test."""
    m = launch(argv[0] if argv else None, apps=argv[1] if len(argv) > 1
               else None, machine=argv[2] if len(argv) > 2 else "")
    with m:
        print(m.profile, json.dumps(m.status()))
        print(json.dumps(m.cards()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
