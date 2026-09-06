#!/usr/bin/env python3
"""The BOOT LADDER page: every discrete move of memory from reset to the first
desktop frame, walkable, with the timeline each one costs on a 4.77MHz 8088.

    python3 tools/os88ladder.py                     # -> build/bootladder.html
    python3 tools/os88ladder.py --selfcheck         # ...does this still WORK?
    python3 tools/os88ladder.py --no-measure        # structure only, no emulator

**ON DEMAND. NOTHING IN `make` RUNS THIS AND NOTHING SHOULD.** It boots the
tree under MartyPC and single-steps a whole boot through forty-odd
breakpoints, which is minutes - `make test-fast`'s whole budget is 30 seconds
and a row that wanted 4 would already be the most expensive thing in it.

**SO THE PAGE GOES STALE, AND THAT IS THE DESIGN.** Nothing rebuilds it when
`kmain` gains a call, when a section moves in the ladder, when a constant is
retuned or when somebody makes the disk faster. The page it wrote last time
will still open, still animate, and still be confidently wrong.

**THE FIRST THING TO DO WHEN SOMEONE ASKS FOR THIS PAGE IS TO RUN
`--selfcheck` AND FIX WHAT IT SAYS - before regenerating, and before
believing a number on the page you already have.** The check is not a
formality: this tool reads fifteen constants out of five source files by
name, resolves the seven symbols the probe reads out of the guest, and maps
a measured phase list onto a fixed model of the boot. Every one of those is a thing the tree is allowed to
change without telling anybody, and each has its own refusal below naming
the file to go and look at. A rename fails the check; it does not quietly
produce a page with a hole in it.

WHAT IS MEASURED AND WHAT IS DERIVED, because the page says so per number
and this is where the rule comes from:

  * MEASURED - every millisecond on the timeline, off MartyPC's own cycle
    counter at 4.772727MHz with the floppy's mechanics modelled
    (docs/MARTYPC-DEBUG.md). Also the loading bar's real numerator and
    denominator, read out of `[spl_done]`/`[spl_total]` in the blob at each
    stop, and the heap's arena and claim table, read out of `mem_base`,
    `mem_top` and `mem_tab`.
  * DERIVED - the memory addresses, which come from the ladder
    `tools/kernsize.py` measures out of the built kernel, and the handful of
    sub-millisecond costs inside the boot sector that are arithmetic on the
    8088's own instruction timings rather than a bracket of their own. The
    page marks these.

**A DISK FIGURE OFF A GLaBIOS MACHINE IS NOT A FIELD FIGURE**
(docs/MARTYPC-DEBUG.md). The IBM 5150 ROM is not in this tree and cannot be,
so `--machine` defaults to the IBM-ROM 5150 and FALLS BACK to its GLaBIOS
twin when the ROM is absent - and the page is stamped with which one ran and
what that costs. What does NOT move with the ROM is the mechanical column,
which is the FDC model PERFORMANCE.md Set 37 calibrated against the real
5150; what does is the ROM's own code, which is two of the four things a
boot spends time on.
"""

import base64
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

HZ = 4772727.0                  # the 5150's dot clock / 3, MartyPC's own rate
TICK_HZ = 18.2065               # the BIOS tick SPEC.md 15.4's timer counts
KERNEL_SEG = 0x0060


class Stale(SystemExit):
    """The tree moved under the tool. Always names the file to go and look at."""

    def __init__(self, what, where, fix):
        SystemExit.__init__(
            self, "os88ladder: %s\n"
                  "  it lived in: %s\n"
                  "  what to do : %s\n"
                  "  (this is the staleness this tool's header warns about - "
                  "the page is NOT rebuilt by `make`)" % (what, where, fix))


# -----------------------------------------------------------------------------
# 1. What the TREE says - constants, the ladder, the volume. No emulator.
# -----------------------------------------------------------------------------

def grab(path, pattern, what, fix):
    """One constant out of one source file, or a refusal that names both.

    Every scrape in this file goes through here on purpose. A `%s` that no
    longer matches is the single commonest way a generator like this rots,
    and the difference between a tool that says which line of which file to
    open and one that says `NoneType has no group` is most of what makes it
    worth running a year later.
    """
    src = open(os.path.join(ROOT, path), errors="replace").read()
    m = re.search(pattern, src, re.M)
    if not m:
        raise Stale("%s is gone" % what, "%s  /%s/" % (path, pattern), fix)
    return int(m.group(1), 0)


def constants():
    """The dozen numbers the boot is shaped by, read where they are defined."""
    c = {}
    c["BOOT2_SECS"] = grab("kernel/kernel.asm", r"^BOOT2_SECS\s+equ\s+(\d+)",
                           "BOOT2_SECS - the blob's length in sectors",
                           "find its new name in kernel/kernel.asm and update "
                           "constants(); the Makefile scrapes the same line")
    c["SPL_RESIDENT"] = grab("kernel/splash.inc", r"^SPL_RESIDENT\s+equ\s+(\d+)",
                             "SPL_RESIDENT - sectors before the bar can draw",
                             "kernel/splash.inc; it decides which stage the "
                             "loading screen first appears in")
    c["SPL_POST"] = grab("kernel/splash.inc", r"^SPL_POST\s+equ\s+(\d+)",
                         "SPL_POST - notches added to the bar's DENOMINATOR",
                         "kernel/splash.inc; the bar's arithmetic on the page "
                         "is done/(sectors+SPL_POST)")
    c["SPL_BAR_PX"] = grab("kernel/splash.inc", r"^SPL_BAR_PX\s+equ\s+(\d+)",
                           "SPL_BAR_PX - the bar's interior width",
                           "kernel/splash.inc; the page draws the bar to scale")
    c["SPL_BAR_H"] = grab("kernel/splash.inc", r"^SPL_BAR_H\s+equ\s+(\d+)",
                          "SPL_BAR_H", "kernel/splash.inc")
    c["SPL_TTLC"] = grab("kernel/splash.inc", r"^SPL_TTLC\s+equ\s+(\d+)",
                         "SPL_TTLC - the caption field, in cells",
                         "kernel/splash.inc")
    c["RELOC_ADJ"] = grab("boot/boot.asm", r"^RELOC_ADJ\s+equ\s+(0x[0-9A-Fa-f]+)",
                          "RELOC_ADJ - where stage 1 relocates to",
                          "boot/boot.asm; the page places the sector at "
                          "int12h*64 - RELOC_ADJ")
    c["BOOT_SECT"] = grab("boot/boot.asm", r"^BOOT_SECT\s+equ\s+(\d+)",
                          "BOOT_SECT", "boot/boot.asm")
    c["BOOT_STACK"] = grab("boot/boot.asm", r"^BOOT_STACK\s+equ\s+(\d+)",
                           "BOOT_STACK - stage 1's stack, under its own body",
                           "boot/boot.asm")
    c["DPT_AT"] = grab("boot/boot.asm", r"^DPT_AT\s+equ\s+(0x[0-9A-Fa-f]+)",
                       "DPT_AT - where the diskette parameter table is copied",
                       "boot/boot.asm and boot/boot2.asm, which must agree")
    c["KSIG_OFF"] = grab("boot/boot2.asm", r"^KSIG_OFF\s+equ\s+(\d+)",
                         "KSIG_OFF - SPEC.md 18.93.1's canary probe",
                         "boot/boot2.asm; the Makefile types the same number")
    c["STK0_SIZE"] = grab("kernel/kernel.asm", r"^STK0_SIZE\s+equ\s+(\d+)",
                          "STK0_SIZE - task 0's stack",
                          "kernel/kernel.asm's ladder")
    c["MEM_MAX"] = grab("kernel/memory.inc", r"^MEM_MAX\s+equ\s+(\d+)",
                        "MEM_MAX - heap claim records",
                        "kernel/memory.inc; the page walks mem_tab")
    c["MC_SIZE"] = grab("kernel/memory.inc", r"^MC_SIZE\s+equ\s+(\d+)",
                        "MC_SIZE - one claim record",
                        "kernel/memory.inc; if the record grew, the walk in "
                        "heap_now() below reads the wrong words")
    # Scraped HERE and not in regions(), which is where it used to be: a
    # scrape the self-check does not reach is one that fails after the walk,
    # with the minutes already spent - which is how this one was found, its
    # pattern binding on a comment the tree had since rewritten.
    c["OVL_AT"] = grab("kernel/kernel.asm", r"^OVL_AT\s+equ\s+(\d+)",
                       "OVL_AT - where `.ovl` sits inside the blob",
                       "kernel/kernel.asm; `section .ovl start=OVL_AT` is the "
                       "line that uses it")
    # MIN_RAM_KB is NOT scraped: kernel.asm defines it twice, once per build,
    # and a regex picks whichever it happens to reach. tools/kernsize.py is
    # told which build it is measuring and reports the one that applies.
    return c


def ladder(build="build", defines=()):
    """The segment ladder, out of the tool that MEASURES the built kernel.

    Never re-derived here. `tools/kernsize.py --json` assembles the kernel and
    reports the sizes the ladder falls out of, so the page cannot describe a
    layout the tree does not have - which is exactly what a hand-kept copy of
    these numbers does, and docs/BOOT-LADDER-PLAN.md's own tables are the
    worked example: they quote a ladder three changes old.
    """
    cmd = ["python3", os.path.join(ROOT, "tools", "kernsize.py"),
           "--json", "--build", build] + ["-D" + d for d in defines]
    try:
        out = subprocess.check_output(cmd, cwd=ROOT)
    except (subprocess.CalledProcessError, OSError) as e:
        raise Stale("tools/kernsize.py would not answer (%s)" % e,
                    "tools/kernsize.py --json",
                    "run `make` first - kernsize measures build/kernel.bin")
    k = json.loads(out)
    for want in ("kseg", "imgpara", "coldpara", "fatpara", "lowpara",
                 "vgabufpara", "kend", "ksize", "text", "bss", "cold",
                 "lowbss", "ovlw", "ovl", "boot2", "stk0"):
        if want not in k:
            raise Stale("tools/kernsize.py no longer reports `%s`" % want,
                        "tools/kernsize.py --json",
                        "its JSON keys changed; re-map them in ladder()")
    p = k["kseg"]
    k["cold_seg"] = p = p + k["imgpara"]
    k["fat_seg"] = p = p + k["coldpara"]
    k["low_seg"] = p = p + k["fatpara"]
    k["vgabuf_seg"] = p = p + k["lowpara"]
    if p + k["vgabufpara"] != k["kend"]:
        raise Stale("the ladder does not add up: %#x + %d != kend %#x"
                    % (p, k["vgabufpara"], k["kend"]),
                    "kernel/kernel.asm's ladder / tools/kernsize.py",
                    "a rung was inserted or removed - teach ladder() the new "
                    "one, in kernel.asm's own order")
    return k


def volume(path):
    """The BPB and the root directory of a built image - the boot's own view.

    Where KERNEL.SYS starts is not a constant anywhere: boot/boot.asm derives
    it from the four BPB fields below, exactly as done here, so that a change
    to the disk layout moves the page and the sector together.
    """
    if not os.path.exists(path):
        raise Stale("no image at %s" % path, path,
                    "run `make` - the page describes a disk that exists")
    d = open(path, "rb").read()
    v = {}
    v["bps"], v["spc"] = struct.unpack_from("<HB", d, 11)
    v["rsvd"], = struct.unpack_from("<H", d, 14)
    v["nfat"] = d[16]
    v["rootent"], = struct.unpack_from("<H", d, 17)
    v["tot16"], = struct.unpack_from("<H", d, 19)
    v["media"] = d[21]
    v["fatsz"], = struct.unpack_from("<H", d, 22)
    v["spt"], = struct.unpack_from("<H", d, 24)
    v["heads"], = struct.unpack_from("<H", d, 26)
    if v["bps"] != 512 or not v["spt"] or not v["heads"]:
        raise Stale("the BPB in %s is not one this page understands" % path,
                    "tools/os88disk.py writes it", "check the image built")
    v["data_lba"] = (v["rsvd"] + v["nfat"] * v["fatsz"]
                     + (v["rootent"] * 32 + v["bps"] - 1) // v["bps"])
    # KERNEL.SYS is allocated first and contiguously, which is the whole
    # reason a 512-byte sector can read it with flat arithmetic.
    off = (v["rsvd"] + v["nfat"] * v["fatsz"]) * v["bps"]
    v["kernel"] = None
    for i in range(v["rootent"]):
        e = d[off + i * 32:off + i * 32 + 32]
        if not e or e[0] in (0, 0xE5) or e[11] & 0x08:
            continue
        if e[:11] == b"KERNEL  SYS":
            clus, = struct.unpack_from("<H", e, 26)
            size, = struct.unpack_from("<I", e, 28)
            v["kernel"] = {"lba": v["data_lba"] + (clus - 2) * v["spc"],
                           "size": size,
                           "sectors": (size + v["bps"] - 1) // v["bps"]}
            break
    if not v["kernel"]:
        raise Stale("KERNEL.SYS is not in %s's root directory" % path, path,
                    "the boot sector looks for the FIRST file in the data "
                    "area; if the image layout changed, so has the boot")
    v["md5"] = hashlib.md5(d).hexdigest()
    return v


# -----------------------------------------------------------------------------
# 2. What the MACHINE says - one boot, walked. Needs MartyPC.
# -----------------------------------------------------------------------------

# THE MACHINE IS A HERCULES 5150, and the card is the reason. All three
# adapters run the same kernel, so the choice costs the page nothing in
# accuracy - but the picture of the finished desktop is a real screen, and
# 720x348 is a shape a reader recognises where CGA's 640x200 is a letterbox
# that has to be stretched two and a half times vertically to look like the
# monitor it was on.
#
# The IBM 5150 ROM is IBM's and is not in this tree; the GLaBIOS twin of the
# same machine boots without it and is FASTER than any 5150 ever was, so the
# page is stamped with which one ran.
FIELD_MACHINE = "os8088_5150_herc"
TWIN_MACHINE = "os8088_5150_herc_gla"


def machine_available(name):
    """Is this MartyPC config's ROM set actually present?"""
    run = os.path.join(ROOT, "build", "martypc", "run")
    cfg = os.path.join(ROOT, "tools", "martypc", "configs", "os8088_machines.toml")
    if not (os.path.isdir(run) and os.path.exists(cfg)):
        return False
    src = open(cfg, errors="replace").read()
    m = re.search(r'name\s*=\s*"%s"(.*?)(?=\n\[\[machine\]\]|\Z)' % re.escape(name),
                  src, re.S)
    if not m:
        return False
    r = re.search(r'rom_set\s*=\s*"([^"]+)"', m.group(1))
    if not r:
        return False
    if not r.group(1).startswith("ibm"):
        return True                     # GLaBIOS ships with the emulator
    roms = os.path.join(run, "media", "roms")
    return os.path.isdir(roms) and any(f.upper().endswith(".BIN")
                                       for f in os.listdir(roms))


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


PROBE_BLOB = ("spl_done", "spl_total", "spl_tick")   # offsets inside the blob
PROBE_KERN = ("kmain",)                               # KERNEL_SEG offsets
PROBE_LIN = ("mem_base", "mem_top", "mem_tab")        # linear, wherever they are


def probe_symbols(defines):
    """The seven symbols the walk reads out of the guest, resolved - or a
    refusal naming the one that is gone. Called by the Probe, and by
    `--selfcheck` BEFORE any emulator is started, so a rename is a second's
    failure and not a walk's."""
    import os88sym
    sym = os88sym.syms(defines)
    out = {}
    for want in PROBE_BLOB + PROBE_KERN:
        if want not in sym:
            raise Stale("the kernel has no symbol `%s`" % want,
                        "kernel/splash.inc, kernel/kernel.asm",
                        "the page reads it to draw the loading bar; find "
                        "what replaced it")
        out[want] = sym[want]
    for want in PROBE_LIN:
        try:
            out[want] = os88sym.linear(want, defines)
        except KeyError:
            raise Stale("the kernel has no symbol `%s`" % want,
                        "kernel/memory.inc",
                        "the page walks the heap's arena and claim table "
                        "through it; find what replaced it")
    return out


class Probe(object):
    """One live guest, with the four questions this page asks of it."""

    def __init__(self, m, lad, defines):
        self.m, self.lad = m, lad
        self.blob = lad["kend"] * 16
        self.sym = probe_symbols(defines)
        self.mem_base = self.sym["mem_base"]
        self.mem_top = self.sym["mem_top"]
        self.mem_tab = self.sym["mem_tab"]

    # `.boot2` has no fixed segment (os88sym refuses to place it), and it does
    # not need one here: stage 1 reads the blob to the heap's floor, so the
    # segment is the ladder's own `kend`.
    def blobaddr(self, name):
        return self.blob + self.sym[name]

    def bar(self):
        """The loading bar's REAL numerator and denominator, from the blob."""
        done = u16(self.m.read(self.blobaddr("spl_done"), 2))
        total = u16(self.m.read(self.blobaddr("spl_total"), 2))
        return {"done": done, "total": total}

    def heap(self, mc_size, mem_max):
        """The arena's ends and every live claim, out of the kernel's own table.

        Before mem_init these words are whatever was in RAM, so the caller
        decides when to start believing them - `base` below zero-lengths the
        arena and the model treats that as "the heap does not exist yet".
        """
        base = u16(self.m.read(self.mem_base, 2))
        top = u16(self.m.read(self.mem_top, 2))
        claims = []
        raw = self.m.read(self.mem_tab, mc_size * mem_max)
        for i in range(mem_max):
            r = raw[i * mc_size:(i + 1) * mc_size]
            seg, para = u16(r, 0), u16(r, 2)
            if seg and para:
                claims.append({"seg": seg, "para": para, "own": u16(r, 4)})
        return {"base": base, "top": top, "claims": claims}


def png1(w, h, rows, lit=(255, 255, 255), dark=(0, 0, 0)):
    """A 1-bit PNG from the rows of bits `Marty.vram()` hands back.

    Written out here rather than pulled in, because the whole page is one
    file and a dependency for two hundred lines of PNG would be the only
    thing in this tool that is not either measured or read out of the tree.

    A SET BIT IS A LIT PIXEL, which is the one thing to get right and the one
    thing that does not announce itself: the wrong way round produces a
    perfectly good picture of the desktop with black and white swapped, and
    only somebody who has seen the real screen would notice.
    """
    import struct
    import zlib
    raw = bytearray()
    for r in rows:
        raw.append(0)                       # filter: none
        b, n = 0, 0
        for px in r:
            b = (b << 1) | (1 if px else 0)
            n += 1
            if n == 8:
                raw.append(b); b, n = 0, 0
        if n:
            raw.append(b << (8 - n))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 1, 3, 0, 0, 0))
            + chunk(b"PLTE", bytes(dark) + bytes(lit))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def walk(image, machine, defines, lad, cons, limit=240.0, verbose=True,
         build="build"):
    """Boot once, stopping wherever the page has something to say.

    THE BOUNDARIES ARE THE PAGE'S OWN, which is why this does not simply call
    tools/os88boot.py: that tool charges the whole boot sector as three rows,
    and this page's stages cut it at the two places a discrete thing MOVES -
    the far jump into stage 2, and the sector at which the loading screen
    first has enough of itself in RAM to draw. Both are addresses, so both are
    breakpoints; neither is a phase os88boot has a name for.

    `kmain`'s rows ARE os88boot's, imported rather than re-derived, so the two
    instruments cannot drift apart about what a phase is called.
    """
    import os88marty
    import os88boot

    sites = os88boot.collapse(os88boot.callsites(defines, build))
    if len(sites) < 20:
        raise Stale("os88boot.callsites() found only %d calls in kmain"
                    % len(sites),
                    "tools/os88boot.py callsites()",
                    "kmain has ~30; the listing parser has lost the macro "
                    "expansions again - OVWCALL and friends list as `%1`")

    ev = []                             # the timeline, in cycle order
    t = {"prev": 0, "pdisk": None}

    def close(kind, name, cyc, disk, extra=None):
        row = {"kind": kind, "name": name,
               "t0": t["prev"] * 1000.0 / HZ, "t1": cyc * 1000.0 / HZ,
               "ms": (cyc - t["prev"]) * 1000.0 / HZ}
        for k in ("reads", "read_sectors", "seeks", "seek_cylinders",
                  "transfer_ms", "seek_ms"):
            row[k] = disk.get(k, 0) - (t["pdisk"] or {}).get(k, 0)
        if extra:
            row.update(extra)
        ev.append(row)
        t["prev"], t["pdisk"] = cyc, disk
        return row

    with os88marty.launch(image, machine=machine, boot=False) as m:
        p = Probe(m, lad, defines)
        kmain = KERNEL_SEG * 16 + p.sym["kmain"]
        spl_tick = p.blobaddr("spl_tick")
        stage2 = p.blob                 # `.boot2` offset 0 is `jmp boot2_entry`

        # --- the machine's own ROM, which os8088 does not write --------------
        m.bp_exec(0x7C00)
        m.run()
        if m.wait_stop(limit) is None:
            raise Stale("the machine never reached 0000:7C00", machine,
                        "the image is not bootable on this config, or the "
                        "emulator is not the one this tool expects")
        close("rom", "post", m.status()["cycles"], m.disk())
        ramkb = u16(m.read(0x413, 2))   # the BDA's own answer to int 12h

        vec = m.read(0x13 * 4, 4)
        rom13 = (u16(vec, 2) << 4) + u16(vec, 0)

        # --- stage 1 and stage 2, bracketed at every call that costs ---------
        # int 13h is entered through the vector and returns through an IRET
        # frame; spl_tick is a NEAR call from inside the blob and returns
        # through two bytes. GETTING THAT WRONG IS A HANG, NOT AN ERROR: the
        # bracket runs to an address the boot never reaches and the walk sits
        # there until the timeout. tools/os88boot.py brackets the same call
        # at the same address (its splash_entry()), so the two instruments
        # agree about what the splash costs.
        #
        # THE ONE `mem` BREAKPOINT IS WHAT SEPARATES TWO STAGES: relocating
        # the sector and taking over the diskette parameter table are one
        # unbroken run of code with no call between them, so the only edge to
        # stop on is the first touch of DPT_AT itself. It is DISARMED the
        # moment it fires - the vector points there afterwards, so the BIOS
        # reads those eleven bytes on every floppy operation for the rest of
        # the boot and the walk would spend the whole load stopping on them.
        seen2 = False
        dptbp = {"type": "mem", "addr": cons["DPT_AT"]}
        while True:
            bps = [{"type": "exec", "addr": a}
                   for a in (rom13, spl_tick, stage2, kmain)]
            if dptbp:
                bps.append(dptbp)
            m.breakpoints(bps)
            m.run()
            if m.wait_stop(limit) is None:
                raise Stale("the boot never reached kmain", machine,
                            "a breakpoint address is wrong, or the boot "
                            "stopped - run the image by hand and look")
            st = m.status()
            ip, cyc = st["flat_ip"], st["cycles"]
            if ip == kmain:
                close("kernel", "stage 2: loop", cyc, m.disk())
                break
            if ip == stage2:
                close("kernel", "stage 1: sector code", cyc, m.disk())
                seen2 = True
                continue
            if dptbp and ip not in (rom13, spl_tick):
                # Not one of ours: the DPT write, which is the only other
                # thing armed. Close the relocation here and stop watching.
                close("kernel", "relocate", cyc, m.disk())
                dptbp = None
                continue
            r = m.regs()
            near = (ip == spl_tick)
            name = "splash tick" if near else "int 13h"
            close("kernel", "stage %d: %s code" % (2 if seen2 else 1,
                                                  "loader" if seen2 else "sector"),
                  cyc, m.disk())
            frame = m.read((r["ss"] << 4) + r["sp"], 2 if near else 4)
            back = ((r["cs"] << 4) + u16(frame) if near
                    else (u16(frame, 2) << 4) + u16(frame, 0))
            if near:
                # AX = sectors loaded, DX = total; the bar's own arguments.
                extra = {"arg_done": r["ax"], "arg_total": r["dx"]}
            else:
                # AH is the function and AL the sectors asked for, so the page
                # can say "reset the controller" where it means that rather
                # than calling every int 13h a read.
                extra = {"fn": (r["ax"] >> 8) & 0xFF, "want": r["ax"] & 0xFF,
                         "cyl": ((r["cx"] >> 8) & 0xFF)
                                | ((r["cx"] & 0xC0) << 2),
                         "sec": r["cx"] & 0x3F, "head": (r["dx"] >> 8) & 0xFF,
                         "drive": r["dx"] & 0xFF, "dest": r["es"]}
                name = ("int 13h reset" if extra["fn"] == 0
                        else "int 13h read %d" % extra["want"])
            m.bp_exec(back)
            m.run()
            if m.wait_stop(limit) is None:
                raise Stale("a %s never returned to %05X" % (name, back),
                            "the bracket in walk()",
                            "spl_tick is a NEAR call and int 13h an IRET - if "
                            "either changed shape, so must this")
            close("disk" if not near else "draw", name,
                  m.status()["cycles"], m.disk(), extra)
            if verbose:
                sys.stderr.write("  %-16s %8.1f ms\n" % (name, ev[-1]["ms"]))

        # --- kmain, one row per call, os88boot's own list -------------------
        for addr, name, n in sites:
            m.bp_exec(KERNEL_SEG * 16 + addr)
            m.run()
            if m.wait_stop(limit) is None:
                raise Stale("kmain never returned from %s" % name,
                            "kernel/kernel.asm's kmain",
                            "the call list and the boot disagree - regenerate "
                            "after `make`, and see os88boot.callsites()")
            row = close("kernel", name if n == 1 else "%s x%d" % (name, n),
                        m.status()["cycles"], m.disk())
            row["bar"] = p.bar()
            row["heap"] = p.heap(cons["MC_SIZE"], cons["MEM_MAX"])
            # WATCH THE FAT WINDOW. `.ovlw` is boot-overlay CODE that rides
            # the kernel's own read onto FAT_SEG and is forfeit at the first
            # mount (SPEC.md 2.5.3) - so somewhere in this walk those bytes
            # stop being the code that is running and become a FAT snapshot.
            # Digesting them at every stop finds the phase it happened in
            # WITHOUT the page having to assert which one, and proves the
            # claim rather than repeating it.
            row["fatw"] = hashlib.md5(
                m.read(lad["fat_seg"] * 16, 256)).hexdigest()[:8]
            if verbose:
                sys.stderr.write("  %-22s %8.1f ms  bar %d/%d\n"
                                 % (row["name"], row["ms"],
                                    row["bar"]["done"], row["bar"]["total"]))

        total = t["prev"]
        longest = (t["pdisk"] or {}).get("longest_run", 0)
        m.bp_exec()
        ticks = u16(m.read(KERNEL_SEG * 16 + 0x000C, 2))

        # --- and what all of that was FOR --------------------------------
        # The walk stops on the step that draws the desktop, so the frame is
        # already on the glass; letting it settle first is only so the pointer
        # and the driver notice have landed. It is read out of the card's own
        # memory rather than screenshotted, so the picture is the machine's
        # pixels and not a rendering of them.
        shot = None
        try:
            m.run()
            os88marty.settle(m, quiet=0.6, stable=2, limit=40.0)
            w, h, rows = m.vram()
            shot = png1(w, h, rows)
            if verbose:
                sys.stderr.write("  desktop %dx%d, %s bytes of PNG\n"
                                 % (w, h, "{:,}".format(len(shot))))
        except Exception as e:                          # noqa: BLE001
            # A missing picture is a page with one illustration fewer, not a
            # failed run - and on an adapter this cannot read (a colour card
            # in a planar mode) that is the right outcome.
            sys.stderr.write("os88ladder: no desktop picture (%s)\n" % e)

    return {"machine": machine, "image": os.path.basename(image),
            "ram_kb": ramkb, "events": ev,
            "desktop": base64.b64encode(shot).decode() if shot else "",
            "total_ms": total * 1000.0 / HZ, "longest_run": longest,
            "boot_ticks": ticks, "boot_ticks_ms": ticks * 1000.0 / TICK_HZ,
            "taken": time.strftime("%Y-%m-%d %H:%M:%S")}


# -----------------------------------------------------------------------------
# 3. The MODEL - which measured phase belongs to which stage, and what memory
#    looks like once that stage has happened.
# -----------------------------------------------------------------------------
#
# A STAGE IS ONE DISCRETE MOVE OF MEMORY, which is the whole organising idea of
# the page: the boot is not a list of routines, it is a sequence of things
# arriving at, and leaving, addresses. `phases` is the measured work that
# happens between one arrival and the next, named exactly as the instruments
# name it so that a phase which vanishes or appears cannot be absorbed
# silently - assign() refuses on either.
STAGES = [
    dict(id="post", short="firmware", title="The firmware starts, and reads one sector",
         moved="512 bytes: the first sector of the floppy",
         take=dict(until="post")),
    dict(id="reloc", short="relocate", title="The boot sector moves itself out of the way",
         moved="those same 512 bytes, now at the machine's very top",
         take=dict(until="relocate")),
    dict(id="dpt", short="drive settings", title="The floppy's settings table is replaced",
         moved="eleven bytes, low in memory",
         take=dict(count=1)),
    dict(id="blob", short="loader", title="The rest of the loader arrives",
         moved="the loader, the loading screen and the start-up code",
         take=dict(before="stage 2: loader code")),
    dict(id="splash", short="loading screen", title="The loading screen appears",
         moved="the first slice of the operating system - and the screen itself",
         take=dict(until="splash tick")),
    dict(id="kernel", short="system loads", title="The rest of the operating system arrives",
         moved="everything from its code to its start-up code",
         take=dict(until="stage 2: loop")),
    dict(id="kmain", short="hand over", title="The operating system takes over",
         moved="the working stack, into an area of its own", focus=["lowbss"],
         take=dict(phases=["dsk_boot_from_x", "cpu_detect", "xm_sniff",
                           "dsk_dpt_init_x", "sched_init", "sch_idle_start",
                           "clk_init", "vid_init", "vid_ctx_init",
                           "vid_probe_avail", "vid_disp_init"])),
    dict(id="heap", short="memory pool", title="The memory pool opens",
         moved="everything above the loader becomes available to ask for",
         focus=["free*"],
         take=dict(phases=["mem_init_x", "mod_init_x"])),
    dict(id="ui", short="typeface + windows", title="Typeface, windows and the menu bar",
         moved="the letterforms, and the first blocks handed out",
         focus=["lowbss"],
         take=dict(phases=["font_init", "ovl_font_init", "wm_init",
                           "band_init", "menu_init", "inst_init",
                           "splf_step"])),
    dict(id="mouse", short="mouse", title="Looking for a mouse",
         moved="nothing - the longest wait in the boot that is not the disk",
         take=dict(phases=["ovl_spl_msg_mouse", "mouse_init", "splf_step"])),
    dict(id="desk", short="drives + dock", title="Drives, the dock and the driver table",
         moved="desktop state, in the operating system's own storage",
         focus=["ktext"],
         take=dict(phases=["ovl_spl_msg_fdd", "desk_init", "dock_init",
                           "files_init_x", "drv_init_x",
                           "drv_snd_sniff", "snd_init", "splf_step"])),
    dict(id="drvboot", short="mount + drivers", title="The floppy is mounted - over the start-up code",
         moved="the disk's index, written across 5KB of code that was running",
         take=dict(phases=["drv_boot_x", "hb_probe_x", "xm_boot_x",
                           "thm_set"])),
    dict(id="unblob", short="space back", title="The loader's space is given back",
         moved="4KB returned to the pool, and the gap closed up",
         focus=["free*"],
         take=dict(phases=["spl_finish", "mem_unblob_x"])),
    dict(id="paint", short="first frame", title="The first desktop frame",
         moved="nothing new - the screen, at last", focus=[],
         take=dict(phases=["gfx_lock", "wm_paint_all", "gfx_unlock",
                           "cursor_show", "drv_notice_x"])),
]


def basename(name):
    """A measured phase's name with the walk's own decorations taken off."""
    n = re.sub(r" x\d+$", "", name)
    return re.sub(r"^(int 13h read) \d+$", r"\1", n)


def defined_in_tree(name):
    """Is there a label `name:` anywhere under kernel/? The cheapest question
    that separates a phase a KNOB left out of this build from one that has
    left the tree altogether."""
    kdir = os.path.join(ROOT, "kernel")
    pat = re.compile(r"^\s*%s:" % re.escape(name), re.M)
    for f in sorted(os.listdir(kdir)):
        if f.endswith((".inc", ".asm")):
            if pat.search(open(os.path.join(kdir, f), errors="replace").read()):
                return True
    return False


def check_coverage(defines, build="build"):
    """Does the STAGES table still describe the kmain that is in the tree?

    THE ONE CHECK THAT MATTERS, and the reason it is a check rather than a
    comment: the boot-sector stages are cut at addresses this tool chooses, so
    they cannot drift - but kmain's is a list of calls that somebody edits, and
    a call added between two of them would otherwise be absorbed into whichever
    stage happened to be adjacent, with its milliseconds and its story silently
    charged to the wrong picture.
    """
    import os88boot
    live = [n for _, n, _ in os88boot.collapse(os88boot.callsites(defines, build))]
    named = set()
    for st in STAGES:
        named |= set(st["take"].get("phases", []))
    missing = [n for n in live if n not in named]
    if missing:
        raise Stale("kmain calls nothing on the page knows about: %s"
                    % ", ".join(sorted(set(missing))),
                    "kernel/kernel.asm's kmain, against STAGES here",
                    "add each to the stage whose STORY it belongs to - and if "
                    "it is a new discrete move of memory, it wants a stage of "
                    "its own, which is what this page is a ladder OF")
    # The other direction is TWO cases, and only one of them is a warning. A
    # phase this table names that THIS build's kmain does not call costs the
    # page nothing (the stage simply gets fewer rows): `xm_sniff`, `thm_set`
    # and `band_init` are all legitimately absent depending on how the kernel
    # is built. A phase that is defined NOWHERE in kernel/ is not gated, it is
    # gone - and a table that keeps naming it, with a note and a title
    # written for it, is describing a boot the tree no longer has. That one
    # is a refusal, because it read as the first case for two releases.
    unused = [n for n in sorted(named) if n not in live]
    gone = [n for n in unused if not defined_in_tree(n)]
    if gone:
        raise Stale("STAGES names a phase no build has: %s" % ", ".join(gone),
                    "STAGES, NOTES and TITLES in tools/os88ladder.py, against "
                    "every label under kernel/",
                    "it is gone from the tree, not gated out of this build - "
                    "remove it from all three tables")
    return unused


def assign(events, defines=("KERN_BIG",), build="build"):
    """Hand every measured event to the stage that wants it, in order.

    Two kinds of boundary, because the boot has two kinds. Down in the boot
    sector a stage ends at an ADDRESS the walk stopped on, so the rule is
    positional - `until` this event, or `before` that one. In kmain a stage
    ends where a NAMED call does, so the rule is the phase list, and an
    unnamed phase is a refusal.
    """
    check_coverage(defines, build)
    out = [dict(st, events=[]) for st in STAGES]
    i = 0
    for s, st in enumerate(out):
        t = st["take"]
        last = (s == len(out) - 1)
        while i < len(events):
            e = events[i]
            n = basename(e["name"])
            if "phases" in t:
                if n not in t["phases"]:
                    break
                st["events"].append(e); i += 1
            elif "before" in t:
                if n == t["before"]:
                    break
                st["events"].append(e); i += 1
            elif "count" in t:
                st["events"].append(e); i += 1
                if len(st["events"]) >= t["count"]:
                    break
            else:                                   # `until`, inclusive
                st["events"].append(e); i += 1
                if n == t["until"]:
                    break
        if not st["events"] and not last:
            raise Stale("stage `%s` got no measured phase at all" % st["id"],
                        "STAGES in tools/os88ladder.py",
                        "the work it names has moved or gone; either the "
                        "stage should go too, or its `take` is out of date")
    if i < len(events):
        raise Stale("%d measured phases fell off the end, starting at `%s`"
                    % (len(events) - i, events[i]["name"]),
                    "STAGES in tools/os88ladder.py",
                    "the last stage stopped taking events before the boot "
                    "did - check what kmain does after wm_paint_all")
    return out


def owner_tags():
    """{value: name} for every kernel/purgeable claim tag, out of memory.inc.

    Scraped so a claim on the map is named by the constant that made it. A tag
    this does not know still renders - as its number - which is the right
    failure for a page: a claim nobody can name is more interesting than none.
    """
    src = open(os.path.join(ROOT, "kernel", "memory.inc"), errors="replace").read()
    out = {}
    for m in re.finditer(r"^(MEM_K_\w+)\s+equ\s+(0x[0-9A-Fa-f]+)", src, re.M):
        out[int(m.group(2), 0)] = m.group(1)
    for m in re.finditer(r"^(MEM_P_\w+)\s+equ\s+MEM_PG_(\w+)\s*<<\s*8\s*\|\s*(0x[0-9A-Fa-f]+)",
                         src, re.M):
        # The purge LEVEL is written in hex (MEM_PG_HIGH equ 0xFE), so the
        # value has to be parsed base-agnostically - reading it as decimal
        # matches the leading `0` and files every purgeable claim under 0x00.
        lvl = re.search(r"^MEM_PG_%s\s+equ\s+(0x[0-9A-Fa-f]+|\d+)"
                        % m.group(2), src, re.M)
        if lvl:
            out[(int(lvl.group(1), 0) << 8) | int(m.group(3), 0)] = m.group(1)
    if not out:
        raise Stale("no MEM_K_* claim tags in kernel/memory.inc",
                    "kernel/memory.inc",
                    "the page names heap claims by their tag; find the new "
                    "spelling")
    return out


def _claim_name(tag):
    """MEM_K_SAVE -> "Menu save-under". A tag is an internal name; the map is
    not the place for one, and the reader only needs to know what the block is
    for."""
    if not tag:
        return "In use"
    nice = {"SAVE": "Menu backing store", "DRV": "Loaded driver",
            "COPY": "Copy buffer", "ASC": "File-association cache",
            "CLIP": "Clipboard", "MOD": "Loaded module",
            "CLONE": "Disk copier buffer", "BAND": "Title-bar composer",
            "WSAVE": "Window backing store", "FATW": "Disk index",
            "DIRW": "Directory read-ahead"}
    key = tag.split("_")[-1]
    return nice.get(key, "In use")


def regions(stage_id, lad, cons, vol, ram_kb, heap, loaded_sectors, spl_first):
    """The memory map AFTER this stage - a list of spans over 640KB.

    Everything here is an address the tree computes for itself: the ladder
    from tools/kernsize.py, the blob from BOOT2_SECS, the relocation from
    int 12h and RELOC_ADJ. The one thing that is not is the heap, which is
    READ OUT OF THE RUNNING MACHINE at each stop - so the arena and its claims
    are what the kernel actually had, not what it ought to have had.
    """
    top = ram_kb * 1024
    ovl_at = cons["OVL_AT"]
    order = [st["id"] for st in STAGES]
    at = order.index(stage_id)

    def since(i):
        return at >= order.index(i)

    R = []

    # THE FLOOR IS READ, NOT ASSUMED. SPEC.md 39.22 gives the heap the
    # `.vgabuf` rung back on a machine with no VGA - this 5150 is a CGA - so
    # the kernel's last rung stops existing partway through the boot. Clipping
    # every ladder region at the live floor is what shows that happening
    # instead of drawing a decoder buffer the machine gave away.
    floor = heap["base"] * 16 if (heap and heap.get("base")) else None

    # The magnified strip writes INSIDE a block, and a block there can be
    # sixty pixels wide - so every region carries a short form as well. A
    # label that does not fit is dropped rather than clipped (a clipped one
    # reads as a different, shorter name), which is why the long one alone is
    # not enough.
    # A SHORT FORM IS NOT A SECOND NAME, it is the same name with the prose
    # taken off - and it is what both the callouts and the magnified strip
    # use, because thirteen labels averaging forty characters is what puts
    # thirteen rungs on a stack that every stage then has to make room for.
    # The full label is on the block itself, and in the panel below.
    SHORT = {"ivt": "Interrupt table", "bda": "Firmware scratch",
             "dpt": "Drive settings", "vbr": "Boot sector",
             "vbrtop": "Boot sector (moved)", "vbrstk": "Boot stack",
             "ktext": "System code", "kcold": "System code 2",
             "ovlw": "Start-up code", "ovl": "Start-up code",
             "fatwin": "Room for the index", "fatw": "Disk index",
             "lowbss": "Stacks + buffers", "vgabuf": "Graphics scratch",
             "blob": "Loader", "pending": "Still on the floppy",
             "unclaimed": "Not spoken for"}

    def add(a, b, rid, label, cls, note="", layer=0, sl=None):
        if floor is not None and rid not in ("heap", "claim", "free"):
            if not rid.startswith(("free", "claim")):
                b = min(b, floor)
        if b > a:
            R.append({"a": a, "b": b, "id": rid, "label": label,
                      "sl": sl or SHORT.get(rid)
                      or ("Free" if rid.startswith("free") else label),
                      "cls": cls, "note": note, "layer": layer})

    add(0, 0x400, "ivt", "Interrupt jump table", "bios",
        "1,024 bytes: 256 addresses, one per [[interrupt]], saying which "
        "routine handles it. The boot sector changes one of them, so that the "
        "disk service reads a [[floppy settings table]] of its own.")
    add(0x400, 0x500, "bda", "Firmware scratch", "bios",
        "Where the machine's built-in software keeps its running state. The "
        "count of timer [[tick|ticks]] since power-on lives here, and the "
        "boot's own stopwatch is that count read twice.")
    if since("dpt"):
        add(cons["DPT_AT"], cons["DPT_AT"] + 11, "dpt",
            "Floppy drive settings", "ours",
            "Eleven bytes copied out of the machine's own table with one "
            "changed: the highest [[sector]] number the [[controller]] may "
            "touch on a [[track]]. This disk has %d per track and the original "
            "IBM firmware says eight." % vol["spt"])
    # The BIOS's copy survives only until the load reaches it - which is a
    # thing to WATCH rather than assert, so it is computed from how many
    # sectors have actually landed.
    kload_end = lad["kseg"] * 16 + loaded_sectors * vol["bps"]
    if not (since("splash") and kload_end > 0x7C00):
        add(0x7C00, 0x7E00, "vbr", "Boot sector, where the firmware put it",
            "dead" if since("reloc") else "ours",
            "The firmware reads exactly one [[sector]] to this address and "
            "jumps into it. That is the whole of what the machine knows how "
            "to do on its own; everything else on this page follows from these "
            "512 bytes. Once they have moved, the copy here is only waiting to "
            "be overwritten - the operating system's own code runs straight "
            "through this address.")

    # ...and stage 1's sector and stack are live until kmain takes SS away,
    # then simply part of the arena. They are DROPPED rather than drawn dead
    # once the heap exists, because a claim can land on them.
    if since("reloc") and not since("heap"):
        seg = ram_kb * 64 - cons["RELOC_ADJ"]
        base = seg * 16 + 0x7C00
        add(base, base + cons["BOOT_SECT"], "vbrtop",
            "Boot sector, moved", "dead" if since("kmain") else "ours",
            "The same 512 bytes, now ending on the machine's very last byte. "
            "The [[block copy]] keeps the same offset and changes only the "
            "[[segment]], so every address inside the sector still resolves "
            "and no instruction had to be rewritten.")
        add(base - cons["BOOT_STACK"], base, "vbrstk",
            "The boot sector's stack", "dead" if since("kmain") else "ours",
            "%s bytes of [[stack]] immediately under the sector itself, so "
            "the two travel together and neither can be landed on by the "
            "operating system arriving below."
            % "{:,}".format(cons["BOOT_STACK"]))

    # --- the kernel image, as much of it as has arrived ----------------------
    kstart = lad["kseg"] * 16
    if since("splash"):
        img_end = min(kstart + loaded_sectors * vol["bps"], lad["kend"] * 16)
        parts = [
            (kstart, lad["cold_seg"] * 16, "ktext",
             "Operating system code and working storage",
             "kern", "%s bytes of code and %s of scratch. They share one "
             "[[segment]], and a segment reaches 64KB - so this block is the "
             "one hard ceiling in the whole system, and code is moved out of "
             "it rather than allowed to grow past it."
             % ("{:,}".format(lad["text"]), "{:,}".format(lad["bss"]))),
            (lad["cold_seg"] * 16, lad["fat_seg"] * 16, "kcold",
             "Operating system code, second block",
             "kern", "%s bytes of code that lives in a [[segment]] of its own "
             "and is reached the long way round. It is always resident; what "
             "it buys is that none of it counts against the 64KB ceiling next "
             "door." % "{:,}".format(lad["cold"])),
        ]
        if since("drvboot"):
            parts.append((lad["fat_seg"] * 16, lad["low_seg"] * 16, "fatw",
                          "Disk index", "data",
                          "The [[file allocation table]] of the floppy that "
                          "has just been [[mount|mounted]] - which is what "
                          "makes its files openable. THESE ARE THE BYTES THE "
                          "START-UP CODE WAS IN."))
        else:
            parts.append((lad["fat_seg"] * 16, lad["low_seg"] * 16, "fatwin",
                          "Room for the disk index", "free",
                          "%s bytes set aside for the [[file allocation table]]"
                          " of whatever disk gets [[mount|mounted]] first. "
                          "Nothing has been mounted yet, so what is actually "
                          "sitting in it is [[overlay|start-up code]]."
                          % "{:,}".format((lad["low_seg"] - lad["fat_seg"]) * 16)))
        parts.append((lad["low_seg"] * 16, lad["vgabuf_seg"] * 16, "lowbss",
                      "Stacks and disk buffers", "kern",
                      "Every running task's [[stack]], and the buffers a "
                      "[[mount]] fills. Those buffers sit at the BOTTOM of "
                      "this block on purpose, so that the start-up code above "
                      "can run on into them and be thrown away by the same "
                      "mount that needs the room."))
        parts.append((lad["vgabuf_seg"] * 16, lad["kend"] * 16, "vgabuf",
                      "Graphics scratch", "kern",
                      "%s bytes the picture-drawing code assembles rows in "
                      "before pushing them at the [[framebuffer]]. A machine "
                      "with no colour card never needs it, and gets it back."
                      % "{:,}".format(lad["vgabuf"])))
        for a, b, rid, lab, cls, note in parts:
            if a >= img_end and not since("kernel"):
                continue
            add(a, min(b, img_end) if not since("kernel") else b,
                rid, lab, cls, note)
        # ...AND THE OVERLAY, ON A LAYER OF ITS OWN. `.ovlw` is 5,215 bytes
        # against the FAT window's 4,608, so it does not fit the block it is
        # drawn over: it runs on into the bottom of `.lowbss`, where SPEC.md
        # 2.1.2 deliberately put the three MOUNT-OWNED buffers so that it
        # could. Drawing it as a raised band that spans both is the only
        # honest picture - it is not a region of the map, it is code lying
        # ACROSS two of them until the mount takes the ground back.
        if not since("drvboot") and lad["ovlw"]:
            ov_a = lad["fat_seg"] * 16
            ov_b = ov_a + lad["ovlw"]
            if since("kernel") or img_end > ov_a:
                add(ov_a, min(ov_b, img_end) if not since("kernel") else ov_b,
                    "ovlw", "Start-up code - borrowed space", "ovl",
                    "%s bytes of [[overlay|start-up code]]. It rides in on the "
                    "same continuous read as everything else and lands on the "
                    "space reserved for the disk index - so it costs nothing "
                    "permanent, and it is FORFEIT the moment a disk is "
                    "[[mount|mounted]]. It is also LONGER than that space, so "
                    "it lies across the buffers below as well. Most of the "
                    "start-up work you can click on runs from here; the one "
                    "step that does not is the mount itself, because that is "
                    "the step that overwrites it."
                    % "{:,}".format(lad["ovlw"]), layer=1)
        # `.ovl`, the other half, is a band inside the blob for the same
        # reason: it is a passenger, not a region.
        if since("blob") and not since("unblob") and lad["ovl"]:
            ova = lad["kend"] * 16 + ovl_at
            add(ova, ova + lad["ovl"], "ovl",
                "Start-up code that outlives the mount", "ovl",
                "%s bytes riding inside the loader's %s, and the half that has "
                "to survive the [[mount]]: the mount itself is here, and so "
                "are the two captions the loading screen shows while it waits. "
                "It goes back to the [[memory pool]] with the rest of the "
                "loader once the desktop is ready to draw."
                % ("{:,}".format(lad["ovl"]),
                   "{:,}".format(cons["BOOT2_SECS"] * vol["bps"])), layer=1)
        if not since("kernel") and img_end < lad["kend"] * 16:
            add(img_end, lad["kend"] * 16, "pending",
                "Still on the floppy", "free",
                "%d of %d [[sector|sectors]] have landed. The number on the "
                "loading bar is this one."
                % (loaded_sectors, vol["kernel"]["sectors"]))

    # --- stage 2's blob, on the heap's floor ---------------------------------
    blob_a = lad["kend"] * 16
    blob_b = blob_a + cons["BOOT2_SECS"] * vol["bps"]
    if since("blob") and not since("unblob"):
        add(blob_a, blob_b, "blob",
            "Loader, loading screen and start-up code", "ovl",
            "%d [[sector|sectors]] read straight to what will become the FLOOR "
            "of the [[memory pool]]. That address is chosen: when these bytes "
            "are given back they rejoin one long free run, where the same "
            "block at the other end of the pool would strand everything above "
            "it." % cons["BOOT2_SECS"])

    # --- the heap, as the machine reported it --------------------------------
    if since("heap") and heap and heap.get("base") and heap.get("top"):
        base, htop = heap["base"] * 16, heap["top"] * 16
        tags = owner_tags()
        # The free space is the arena MINUS the claims, emitted as the runs
        # between them - so the map shows fragmentation rather than drawing a
        # single "free" band with claims sitting on top of it.
        cl = sorted(((c["seg"] * 16, (c["seg"] + c["para"]) * 16, c)
                     for c in heap["claims"] if c["seg"] * 16 >= base),
                    key=lambda x: x[0])
        at = base
        for ca, cb, c in cl:
            add(at, min(ca, htop), "free%06x" % at, "Memory pool - free",
                "free",
                "Nobody has asked for this yet. The pool's two ends are read "
                "live out of the running machine at this exact point, not "
                "worked out from the layout.")
            add(ca, cb, "claim%04x" % c["seg"],
                _claim_name(tags.get(c["own"])), "claim",
                "%s bytes handed out as a single [[claim]], starting at "
                "[[segment|segment]] %04X. Read out of the operating system's "
                "own table of who owns what, at this exact point."
                % ("{:,}".format(cb - ca), c["seg"]))
            at = max(at, cb)
        add(at, htop, "free%06x" % at, "Memory pool - free", "free",
            "%s bytes nobody has asked for, at the top of the pool."
            % "{:,}".format(max(0, htop - at)))
    else:
        # Before mem_init there is no arena, only what nobody has taken. It
        # stops UNDER stage 1's stack, which is real memory in use.
        lo = blob_b if since("blob") else 0x7E00
        hi = (ram_kb * 64 - cons["RELOC_ADJ"]) * 16 + 0x7C00 - cons["BOOT_STACK"] \
            if since("reloc") else top
        add(lo, hi, "unclaimed", "Not spoken for", "free",
            "There is no [[memory pool]] yet - nothing has been set up to hand "
            "memory out - so this is not free space so much as memory nobody "
            "has touched.")

    R.sort(key=lambda r: (r["a"], r["b"]))
    return R


# -----------------------------------------------------------------------------
# 4. The PAGE's words.
# -----------------------------------------------------------------------------

def zooms(regs, ram, want=2):
    """Which slice(s) of the 640KB are worth magnifying for this stage.

    THE ZOOM FOLLOWS THE CONTENT. Early on everything of ours is 512 bytes at
    the bottom and 2.5KB at the very top - two specks 630KB apart, and one
    window over both of them magnifies nothing. So the occupied regions are
    clustered by the gaps between them and each cluster gets a window of its
    own, up to two; beyond that the smallest gaps are closed until two are
    left, because three strips stop reading as "detail" and start reading as
    a second map.
    """
    used = sorted((r["a"], r["b"]) for r in regs
                  if r["cls"] not in ("free",) and r["id"] != "unclaimed")
    if not used:
        return []
    runs = [list(used[0])]
    for a, b in used[1:]:
        if a - runs[-1][1] > ram // 20:         # 32KB at 640KB
            runs.append([a, b])
        else:
            runs[-1][1] = max(runs[-1][1], b)
    while len(runs) > want:
        i = min(range(len(runs) - 1),
                key=lambda j: runs[j + 1][0] - runs[j][1])
        runs[i][1] = runs[i + 1][1]
        del runs[i + 1]
    out = []
    for a, b in runs:
        pad = max((b - a) // 12, 1024)
        a = max(0, (a - pad) & ~0x7FF)
        b = min(ram, (b + pad + 0x7FF) & ~0x7FF)
        if b - a < 8192:                        # never magnify past legibility
            mid = (a + b) // 2
            a, b = max(0, mid - 4096), min(ram, mid + 4096)
        # Padding can bring two windows together; two strips a hair apart
        # magnify the same thing twice and read as a mistake.
        if out and a <= out[-1]["b"] + 2048:
            out[-1]["b"] = max(out[-1]["b"], b)
        else:
            out.append({"a": a, "b": b})
    return out


def strings():
    """The loading screen's own text, out of the kernel that draws it.

    Scraped rather than typed, so the mimic at the top right of the page says
    what the machine says. If one of these is renamed the page refuses instead
    of quietly showing last year's caption.
    """
    def s(path, label, what):
        src = open(os.path.join(ROOT, path), errors="replace").read()
        # The colon is optional: splash.inc writes `spl_s_welcome db '..'`
        # and vidsel.inc writes `spl_s_mouse: db '..'`, and both are labels.
        m = re.search(r"^%s:?\s+db\s+'([^']*)'" % re.escape(label), src, re.M)
        if not m:
            raise Stale("the loading screen's %s (`%s`) is gone"
                        % (what, label), path,
                        "the page draws a mimic of the real bar; find the "
                        "string's new name")
        return m.group(1)
    return {
        "welcome": s("kernel/splash.inc", "spl_s_welcome", "caption"),
        "kern": s("kernel/splash.inc", "spl_s_kern", "load message"),
        "mouse": s("kernel/vidsel.inc", "spl_s_mouse", "mouse message"),
        "fdd": s("kernel/vidsel.inc", "spl_s_fdd", "drives message"),
        "boot": s("kernel/vidsel.inc", "spl_s_boot", "hand-over message"),
    }


# -----------------------------------------------------------------------------
# THE GLOSSARY, and the rule the page's prose is written to.
#
# The reader is assumed to be technically literate and to know NOTHING about
# this machine or this operating system. So the prose says what happens in
# plain words, and a term survives only where it IS the subject - in which
# case it is marked `[[term]]` and gets a definition here that a reader can
# take in without looking anything else up.
#
# TWO RULES, BOTH CHECKED RATHER THAN INTENDED. A definition may not lean on
# another marked term (`--selfcheck` fails on one that does), because a
# glossary that needs a glossary has not explained anything. And no page text
# may cite a specification section, a source file or a comment: the reader has
# none of those, and what the page describes is what the machine does NOW -
# when a thing came to be done that way is not on it.
# -----------------------------------------------------------------------------

GLOSS = {
    '64 KB boundary':
        'The chip that moves disk data into memory cannot carry a '
        'transfer across an address that is a multiple of 65,536. A run '
        'that would straddle one has to be split, which is why one read '
        'here is a single sector.',
    'BIOS disk call':
        "The built-in software's disk service. A program asks it for a "
        'run of sectors and it drives the controller, so nobody has to '
        'know how the controller works.',
    'BIOS memory call':
        'The built-in software\'s answer to "how much memory is fitted", '
        'in kilobytes. Everything on this machine, including this '
        'operating system, sizes itself from that one number.',
    'BIOS screen call':
        "The built-in software's screen service - setting a display mode, "
        'drawing a character. It is correct and it is slow: one character '
        'in graphics mode costs about 40 milliseconds here.',
    'block copy':
        'A single instruction that copies a run of memory and repeats '
        'until a counter runs out. One instruction moves the whole '
        '512-byte boot sector.',
    'boot sector':
        'The first sector of a disk. The firmware reads exactly this one, '
        'checks it ends with a known pair of bytes, and jumps into it - '
        'which is all the machine knows how to do on its own.',
    'checksum':
        'Adding every value in a block together and comparing the total '
        'against one worked out in advance. It catches a block that '
        'arrived incomplete, which a disk can do without reporting any '
        'error at all.',
    'claim':
        'A block of memory handed out to something that asked for it. The '
        'pool tracks who owns each one, and whether it may be shuffled to '
        'close up gaps.',
    'controller':
        'The chip between the processor and the drive. It is told a track '
        'and a sector and it delivers the bytes; almost every disk figure '
        'on this page is really the controller waiting for the disk to '
        'turn.',
    'cylinder':
        'Both surfaces of the disk at the same head position, taken '
        'together. A floppy has two sides, so a cylinder is two tracks - '
        'and the drive can switch sides electrically, without moving '
        'anything.',
    'driver':
        'A separate piece of software, loaded from disk only if asked '
        'for, that teaches the operating system about one piece of '
        'hardware.',
    'file allocation table':
        'The index at the front of a disk that says which chunks belong '
        'to which file. It has to be read into memory before any file can '
        'be opened, and it is why mounting a disk needs space.',
    'firmware scratch':
        "A small area just above the interrupt table where the machine's "
        'built-in software keeps its running state - including the count '
        'of timer ticks since power-on.',
    'floppy settings table':
        'A small table of drive timings the disk service re-reads before '
        'every single operation, found through an entry in the interrupt '
        'table. One byte of it is the highest sector number the '
        'controller may touch on a track.',
    'framebuffer':
        'The block of memory the display hardware reads to decide what is '
        'on the screen. Writing to it is drawing.',
    'head':
        'The part that reads the surface. There are two, one per side, '
        'and only one is ever active.',
    'interrupt':
        'A signal that makes the processor stop what it is doing, run a '
        'small routine, and carry on. Hardware raises them (a timer, a '
        'key, the mouse) and programs can raise them deliberately to ask '
        'the firmware for a service.',
    'interrupt table':
        'A list of 256 addresses at the very bottom of memory, one per '
        'interrupt, saying which routine handles it. Changing an entry '
        'redirects that service to your own code.',
    'kernel':
        'The operating system itself, as opposed to the small loader that '
        'fetches it or the programs that run on top of it.',
    'memory pool':
        'The run of memory left over once the operating system has placed '
        'itself, out of which every later request is satisfied.',
    'mount':
        "Reading a disk's index into memory so its files can be opened. "
        'Until a disk is mounted the machine knows nothing about what is '
        'on it.',
    'multi-sector read':
        'Asking for several sectors in one request instead of one at a '
        'time. The cost is dominated by getting the head to the right '
        'place, so eighteen sectors in one call costs barely more than '
        'six.',
    'overlay':
        'Code that is only needed for a while and is deliberately put '
        'somewhere that will be reused. It costs nothing permanent, but '
        'whatever runs from it must be finished before the space is '
        'taken.',
    'sector':
        'The smallest chunk a disk will read or write in one go. On these '
        'floppies a sector is 512 bytes, and the drive can only ever hand '
        'over whole ones.',
    'segment':
        'On this processor an address is written as two numbers - a base '
        'and an offset - and the base counts in 16-byte steps. So '
        '0060:0000 is base 0x60 times 16, which is byte 1,536. It is why '
        'the addresses here jump in units of 16.',
    'stack':
        'The scratch area a program uses for return addresses and '
        'temporary values. It grows downwards from a fixed top, so where '
        'it is placed decides what it can safely overwrite.',
    'task switching':
        'Running several programs by giving each a slice of time and '
        'swapping between them on the timer interrupt.',
    'tick':
        'The timer interrupt, 18.2 times a second. It is the clock '
        "everything on this machine is measured in, and the boot's own "
        'stopwatch counts them.',
    'track':
        'One ring of sectors at a fixed distance from the middle of the '
        'disk. Reading within a track is nearly free; moving to a '
        'different one means physically stepping the head.',
}


# Marks are matched case-insensitively, so a sentence may open with one.
_GLOSS_LC = dict((k.lower(), v) for k, v in GLOSS.items())

TERM = re.compile(r"\[\[([^\]|]+)(?:\|([^\]]+))?\]\]")


def mark(text):
    """`[[sector]]` and `[[mount|Mounting]]` -> a span the page can define.

    Returns HTML, so everything else in the string is escaped here and the
    page sets it without escaping again. A marked term with no definition is
    a refusal rather than a silent plain word - the whole point of the mark
    is that the reader can hover it.
    """
    def one(m):
        show = m.group(2) or m.group(1)
        key = m.group(1).lower()
        if key not in _GLOSS_LC:
            raise Stale("the prose marks `%s`, which has no definition" % key,
                        "GLOSS in tools/os88ladder.py",
                        "add one - in words that do not themselves need "
                        "looking up - or take the marks off and say it plainly")
        return '<b class="gl" tabindex="0" data-g="%s">%s</b>' % (
            esc(key), esc(show))
    return TERM.sub(one, esc(text))


# What each measured phase is DOING, in the page's own register: plain
# words, marked terms where a term is the subject, and nothing about
# where any of it is written down. Keyed by the phase's own name, so a
# note cannot outlive the thing it describes.
NOTES = {
    'post':
        "The machine's own built-in software, from power-on: it tests "
        'every byte of memory, fills in the [[interrupt table]], works '
        'out what hardware is attached, and finally reads the [[boot '
        'sector]] of the floppy into memory and jumps into it. None of '
        'os8088 has run yet. The mechanical figure on this row is the '
        "drive's own - the head stepping out and one sector passing "
        'under it.',
    'relocate':
        'Interrupts off, and then the first thing worth recording: the '
        "current [[tick]] count, which is where the boot's own "
        'stopwatch starts. It is read here because everything expensive '
        'is below it. Then the [[BIOS memory call]], a refusal if the '
        'machine is too small to finish the job, and a [[block copy]] '
        'of all 512 bytes to the very top of memory. THE COPY KEEPS THE '
        'SAME OFFSET, so every address inside the sector still resolves '
        'and only its [[segment]] changes.',
    'stage 1: sector code':
        "Out of harm's way, the sector replaces the [[floppy settings "
        "table]]: it copies the machine's own - those timings belong to "
        'these drives and are not ours to invent - and changes one '
        'byte, the highest sector number the [[controller]] may touch '
        'on a track. The original IBM PC and XT say eight. This disk '
        'has nine, and a read that ran past the eighth would come back '
        "with the other side's sectors and no error at all.",
    'int 13h reset':
        'Step the head back to track zero before trusting the drive. '
        'The one disk call here that moves no data.',
    'int 13h read':
        'A [[multi-sector read]] through the [[BIOS disk call]]. COST A '
        'DISK CALL IN CALLS, NOT SECTORS: the head is where the head '
        'is, so one call moving eighteen [[sector|sectors]] costs about '
        'what one moving six does. The firmware reporting success is '
        'what says the whole request completed - the count it hands '
        'back alongside is not reliable, and believing it costs several '
        'times the traffic.',
    'stage 2: loader code':
        "The loader's own work between disk calls: a plain text screen "
        'while the sectors land, the [[floppy settings table]] again, '
        'and then how far one disk call may run. On an 8086 or 8088 it '
        'widens that from a [[track]] to a whole [[cylinder]] - the '
        '[[controller]] will carry a read onto the second [[head]] by '
        'itself - which halves the number of calls the rest of the load '
        'takes.',
    'stage 2: loop':
        "The last of the load, and the handover. The stopwatch's "
        'starting value is written where the [[kernel]] will look for '
        'it; the loader records where it has ended up, so the kernel '
        'can still reach the loading screen; the whole load is verified '
        'against a value planted in the middle of the file, and re-read '
        'more cautiously if it fails; and the timer [[interrupt]] is '
        'handed back.',
    'splash tick':
        'One notch of the loading bar. THE FIRST ONE IS THE EXPENSIVE '
        'ONE - it switches the display into graphics, draws the frame, '
        'the trough and the caption. The rest redraw a few pixels of '
        'fill and four digits. Both are drawn as pixels rather than '
        'asked of the [[BIOS screen call]], which charges about 40 '
        'milliseconds a character on this machine.',
    'dsk_boot_from_x':
        'Which disk did we come off? On a floppy this stores a single '
        'byte.',
    'cpu_detect':
        'Work out which processor this is. It happens here because this '
        "is the last moment at which none of the [[kernel]]'s own "
        '[[interrupt]] handlers are installed yet.',
    'xm_sniff':
        'One firmware call: is there any memory above the first '
        'megabyte? An exact answer rather than a guess, and asking it '
        'this way needs none of the hardware gymnastics that reaching '
        'such memory does.',
    'dsk_dpt_init_x':
        "The [[floppy settings table]] becomes the [[kernel]]'s, at the "
        'same address and for the same reason the loader took it.',
    'sched_init':
        '[[task switching|Task switching]] is live from here: the '
        'kernel takes the timer [[interrupt]] and clears its table of '
        'tasks.',
    'sch_idle_start':
        'The task that runs when nothing else wants to. It does nothing '
        'until the desktop starts sleeping between events - which is '
        'what lets a finished desktop spend most of its time halted '
        'instead of spinning.',
    'clk_init':
        'Read the real-time clock, or fall back to a fixed date. Before '
        'the display mode is set, so the very first menu bar already '
        'carries the right time.',
    'vid_init':
        'Work out which display adapter is fitted and publish its real '
        'dimensions - three are supported and one binary drives all of '
        'them, so almost nothing in the kernel may assume a size. The '
        'loading screen stays up and keeps ticking.',
    'vid_ctx_init':
        "Record that geometry as the first display's.",
    'vid_probe_avail':
        'Which OTHER adapters are fitted. It runs AFTER the mode is '
        'set, and that is the whole correctness argument: a colour card '
        'in graphics mode stops answering at the addresses a monochrome '
        'card uses, so the two stop being mistakable for one another.',
    'vid_disp_init':
        'If the machine has both monochrome cards, program the second '
        'one as well. It claims nothing and draws nothing, so the '
        'second monitor comes up scanning and black.',
    'mem_init_x':
        "The [[memory pool]]. The loader is sitting on the pool's "
        'floor, so the pool starts just above it - which is why the '
        'loader was read to that address in the first place. When its '
        'space is given back it rejoins one long free run, instead of '
        'leaving a hole with everything else stranded above it.',
    'mod_init_x':
        'Point every optional-module slot at a safe stub. Nothing '
        'clears this memory, so until this runs those slots hold '
        'whatever happened to be there - and they are jump targets.',
    'font_init':
        "The typeface, into the kernel's own storage.",
    'ovl_font_init':
        'The typeface this build carries, read out of the '
        '[[overlay|start-up code]] - so it needs nothing from the '
        "firmware and the machine's own built-in font is never "
        'consulted.',
    'wm_init':
        "The window manager's state: no windows, one clipping region.",
    'band_init':
        '2 KB for the title-bar composer, which draws a whole bar in '
        'one pass instead of fifteen. A refusal is survivable - the '
        'fifteen-pass version still exists.',
    'menu_init':
        "The menu bar's owner, so the first repaint already has a bar "
        'to draw.',
    'inst_init':
        'The table of running programs. Nothing is running yet.',
    'splf_step':
        'One notch of the loading bar spent by hand, where the kernel '
        "knows something has finished. There are three, and the bar's "
        'denominator has room reserved for them.',
    'ovl_spl_msg_mouse':
        'Write the caption for the wait that is about to happen. It is '
        'composed AFTER the notch that precedes it, never before: the '
        'loading screen refuses to draw while it does not own the '
        'screen, so a line composed too early is composed and never '
        'seen.',
    'mouse_init':
        'Look for a serial mouse. The port is reset - which holds two '
        'of its control lines low for about a sixth of a second - and '
        'then listened to for the single letter a mouse sends to '
        'introduce itself. THE LONGEST STRETCH OF THE BOOT THAT IS NOT '
        'THE DISK: about 0.6 s with a mouse on the other end and about '
        'twice that without one. It charges the bar a notch per '
        '[[tick]] rather than sitting still.',
    'ovl_spl_msg_fdd':
        '...and the caption for the second wait: asking each floppy '
        'drive whether it is there at all.',
    'desk_init':
        'Work out which drives belong on the desktop, which means '
        'asking each floppy unit whether it exists. A drive heading for '
        "the answer 'no' takes nearly two seconds to say so, which on a "
        'machine with an empty second bay is the longest single wait in '
        'the whole boot; here it is one drive answering quickly.',
    'dock_init':
        "The dock strip's working storage.",
    'files_init_x':
        "The file browser's state. No window is open at boot.",
    'drv_init_x':
        'The [[driver]] table - before the sound layer, whose tone '
        'routine reads it on its very first timer tick.',
    'drv_snd_sniff':
        'Is there a music chip at the usual address? If so its '
        '[[driver]] becomes wanted by default - which the settings file '
        'then overrides if it says otherwise, so this is only ever the '
        'answer on a machine nobody has told.',
    'snd_init':
        'The sound layer publishes itself last. Its timer routine has '
        'been running, and deliberately doing nothing, since task '
        'switching started.',
    'drv_boot_x':
        'MOUNT THE FLOPPY, READ THE SETTINGS FILE, AND LOAD WHAT IT '
        'ASKS FOR. [[mount|Mounting]] is what claims the space the '
        "disk's [[file allocation table|index]] goes in - and that "
        'space is currently holding [[overlay|start-up code]] that has '
        'been running the last dozen steps. So this is the call that '
        'destroys it, and that is exactly why THIS routine lives '
        'somewhere else: a routine cannot be the one that overwrites '
        'itself. Nothing loaded here can stop the boot.',
    'hb_probe_x':
        'Is there a hibernation to resume? A look for the pointer file '
        'in the root of the one volume a hibernate writes to - after the '
        'mount, so a hard disk is there to be looked at, and before the '
        'first frame, because the answer is a question the desktop puts '
        'up. A floppy-only machine pays eight compares and no disk work.',
    'xm_boot_x':
        'Set up the memory above the first megabyte, if any was found. '
        'A failure is silent by design.',
    'thm_set':
        'Resolve the colour scheme from the setting, once, before '
        'anything is drawn.',
    'spl_finish':
        'The bar to 100% and the screen handed back. The desktop below '
        'covers every pixel of it, so the loading screen needs no '
        'erasing.',
    'mem_unblob_x':
        "...AND THE LOADER'S SPACE GOES BACK. There is nothing to free: "
        "the loader is at the [[memory pool]]'s floor, so putting the "
        'floor back where it started and closing up turns the release '
        'into part of the one long free run rather than a hole.',
    'gfx_lock':
        'Take the drawing lock, so what follows is one frame and not a '
        'race with the timer.',
    'wm_paint_all':
        'THE FIRST DESKTOP FRAME - the background pattern, the menu '
        'bar, the disk icons, the dock. From here on nothing redraws '
        'more of the screen than it changed; on a machine this slow a '
        'full repaint is visible as a sweep.',
    'gfx_unlock':
        "The frame is on the glass, and the boot's stopwatch stops HERE "
        '- not after the pointer. The question is when the first '
        'desktop frame is finished, and the pointer is not the desktop.',
    'cursor_show':
        "The arrow. From here the mouse's own [[interrupt]] handler "
        'draws it, which is why it keeps moving smoothly however busy '
        'the machine is.',
    'drv_notice_x':
        '...and only NOW say what did not load. A window needs a screen '
        'that has been painted.',
}



# A phase's own name is an internal identifier - `drv_boot_x` means nothing to
# a reader who has not seen the source, and this page is written for one who
# has not. So every phase gets a title in the page's own words. The self-check
# refuses a phase with no title, for the same reason it refuses one with no
# note: the alternative is a raw symbol on the screen.
TITLES = {
    "post": "Firmware start-up, and the first sector",
    "relocate": "Move the boot sector to the top of memory",
    "stage 1: sector code": "Take over the floppy settings table",
    "int 13h reset": "Disk: step the head back to track zero",
    "int 13h read": "Disk read",
    "stage 2: loader code": "Loader housekeeping",
    "stage 2: loop": "Finish the load, and hand over",
    "splash tick": "Advance the loading bar",
    "dsk_boot_from_x": "Which disk did we start from?",
    "cpu_detect": "Identify the processor",
    "xm_sniff": "Is there memory above the first megabyte?",
    "dsk_dpt_init_x": "Take over the floppy settings again",
    "sched_init": "Start task switching",
    "sch_idle_start": "Create the idle task",
    "clk_init": "Read the clock",
    "vid_init": "Identify the display",
    "vid_ctx_init": "Record the display's geometry",
    "vid_probe_avail": "Look for other display cards",
    "vid_disp_init": "Programme the second monitor",
    "mem_init_x": "Open the memory pool",
    "mod_init_x": "Make the optional-module slots safe",
    "font_init": "Load the typeface",
    "ovl_font_init": "Load the typeface",
    "wm_init": "Start the window manager",
    "band_init": "Claim the title-bar composer",
    "menu_init": "Create the menu bar",
    "inst_init": "Clear the table of running programs",
    "splf_step": "Advance the loading bar",
    "ovl_spl_msg_mouse": "Caption: Looking for Mouse",
    "mouse_init": "Look for a mouse",
    "ovl_spl_msg_fdd": "Caption: Looking for Drives",
    "desk_init": "Find the drives",
    "dock_init": "Set up the dock",
    "files_init_x": "Set up the file browser",
    "drv_init_x": "Create the driver table",
    "drv_snd_sniff": "Look for a music chip",
    "snd_init": "Start the sound layer",
    "drv_boot_x": "Mount the floppy, and load what it asks for",
    "hb_probe_x": "Is there a hibernation to resume?",
    "xm_boot_x": "Set up the memory above the first megabyte",
    "thm_set": "Resolve the colour scheme",
    "spl_finish": "Finish the loading screen",
    "mem_unblob_x": "Give the loader's space back",
    "gfx_lock": "Take the drawing lock",
    "wm_paint_all": "Draw the desktop",
    "gfx_unlock": "Release the drawing lock",
    "cursor_show": "Show the pointer",
    "drv_notice_x": "Report anything that did not load",
}


SECTION_REGION = {
    ".text": "ktext", ".bss": "ktext", ".cold": "kcold",
    ".ovlw": "ovlw", ".ovl": "blob", ".boot2": "blob",
    ".lowbss": "lowbss", ".vgabuf": "vgabuf",
}


def region_at(regs, seg):
    """Which drawn region a segment falls in - for a read's DESTINATION."""
    a = seg * 16
    for r in regs:
        if r["a"] <= a < r["b"]:
            return r["id"]
    return None


def check_fatw(stages):
    """PROVE the band goes away where the page says it does.

    walk() digests the first 256 bytes of FAT_SEG at every kmain stop: while
    `.ovlw` is the code that is running they do not change, and the mount is
    what overwrites them with the disk's index. The page draws that band as
    forfeit at `drvboot`. Reading the digests back is what turns that from a
    thing the page asserts into a thing the walk saw - and a walk in which
    the FIRST change lands in any other stage, or never lands at all, is one
    the map would have drawn wrong, so it is refused rather than rendered.
    A walk with no digests (one taken before they were recorded) proves
    nothing either way and is let through.
    """
    seen, first = None, None
    for st in stages:
        for e in st["events"]:
            f = e.get("fatw")
            if f is None:
                continue
            if seen is not None and f != seen and first is None:
                first = (st["id"], e["name"])
            seen = f
    if seen is None:
        return "no FAT-window digests in this walk"
    if first is None:
        raise Stale("the FAT window never changed across kmain, so the walk "
                    "never saw the mount overwrite `.ovlw`",
                    "walk()'s `fatw` digest, against STAGES' `drvboot`",
                    "either the mount no longer lands on FAT_SEG (SPEC.md "
                    "2.5.3) or the digest is read from the wrong place")
    if first[0] != "drvboot":
        raise Stale("the FAT window first changed in stage `%s` (at `%s`), "
                    "not at the mount the page charges it to" % first,
                    "STAGES in tools/os88ladder.py, `drvboot`'s phase list",
                    "whatever first writes FAT_SEG belongs to the stage that "
                    "says the band goes away; move the phase or the claim")
    return "first changed at `%s` in stage `%s`" % (first[1], first[0])


def build_page(walkdata, lad, cons, vol, defines, strs, notes=NOTES,
               build="build"):
    """Everything the page needs, as one JSON-able object."""
    import os88sym
    sect = os88sym.sections(defines)
    stages = assign(walkdata["events"], defines, build)
    check_fatw(stages)
    ram = walkdata["ram_kb"] * 1024
    ksecs = vol["kernel"]["sectors"] - cons["BOOT2_SECS"]

    # The bar's message, per stage. Every string is scraped, and the point at
    # which each is written is a call in kmain that the stage OWNS - so this
    # says which stage, not which line, and cannot drift into a lie about
    # wording. The last stage opens with the bar full and the hand-over
    # caption still on the glass - `spl_finish` fills the bar and nothing
    # rewrites the caption until the desktop paints over it.
    MSG = {"splash": "kern", "kernel": "kern", "kmain": "kern", "heap": "kern",
           "ui": "kern", "mouse": "mouse", "desk": "fdd", "drvboot": "boot",
           "unblob": "boot", "paint": "boot"}

    heap, loaded, out, t0, running = None, 0, [], 0.0, None
    for st in stages:
        ev = st["events"]
        # --- how much of the kernel has landed, and where the heap stands ---
        for e in ev:
            if "arg_done" in e:
                loaded = e["arg_done"]
            if e.get("heap", {}).get("base"):
                heap = e["heap"]
        if st["id"] not in ("post", "reloc", "dpt", "blob", "splash"):
            loaded = ksecs
        # THE BAR AS IT STANDS WHEN THE STAGE OPENS, not when it closes: the
        # screen beside the ladder is what the machine looked like at the
        # moment you arrived, and a stage's own work is what moves it on. So
        # the reading is carried through the whole walk and sampled here
        # BEFORE this stage's events are applied.
        bar = dict(running) if running else None
        # ...with one exception, and it is about what the reader recognises.
        # The stage where the loading screen APPEARS opens, strictly, on a
        # blinking cursor: the screen is drawn part-way through it, by the
        # first notch. Opening it at 0% is what anyone who has watched this
        # machine boot will expect to see, and the stage's own timeline still
        # says exactly when the thing was really drawn.
        if bar is None:
            for e in ev:
                if "bar" in e and e["bar"]["total"]:
                    bar = {"done": 0, "total": e["bar"]["total"]}
                    break
                if "arg_done" in e:
                    bar = {"done": 0,
                           "total": e["arg_total"] + cons["SPL_POST"]}
                    break
        regs = regions(st["id"], lad, cons, vol, walkdata["ram_kb"], heap,
                       loaded, None)
        for r in regs:
            r["note"] = mark(r["note"])

        steps = []
        for e in ev:
            base = basename(e["name"])
            mem = []
            s = sect.get(base)
            if s in SECTION_REGION:
                rid = SECTION_REGION[s]
                # A section's home can be OFF this stage's map by the time a
                # step in it runs: `.ovlw` is gone once the mount has been
                # through it, and the blob is gone once `mem_unblob_x` has
                # given it back - yet `spl_finish` runs from the blob in
                # that same stage. Light whatever the map draws where the
                # section's bytes were, which is the disk index for one and
                # the free run for the other, rather than a name the map
                # does not have.
                if not any(r["id"] == rid for r in regs):
                    home = {"ovlw": lad["fat_seg"], "blob": lad["kend"]}.get(rid)
                    rid = region_at(regs, home) if home else None
                mem.append(rid)
            elif e["kind"] == "rom":
                mem.append("vbr")
            elif st["id"] in ("reloc", "dpt"):
                mem.append("vbrtop" if any(r["id"] == "vbrtop" for r in regs)
                           else "vbr")
                if st["id"] == "dpt":
                    mem.append("dpt")
            elif base.startswith("stage 1"):
                mem.append("vbrtop")
            elif base.startswith("stage 2") or base == "splash tick":
                mem.append("blob")
            if e["kind"] == "disk":
                mem.append("vbrtop" if st["id"] == "blob" else "blob")
                if e.get("dest"):
                    d = region_at(regs, e["dest"])
                    if d:
                        mem.append(d)
            label = TITLES.get(base, base)
            if e.get("fn") == 2:
                label = "%d sector%s \u2192 %04X:0000" % (
                    e["want"], "" if e["want"] == 1 else "s", e["dest"])
            elif "arg_done" in e:
                label = "bar: %d of %d sectors" % (e["arg_done"],
                                                   e["arg_total"])
            note = notes.get(base, "")
            if base == "int 13h read" and e["want"] == 1:
                note += (" THIS ONE IS A SINGLE SECTOR because the place it "
                         "was going was about to cross a [[64 KB boundary]]. "
                         "The run had to stop there and start again.")
            if "bar" in e and e["bar"]["total"]:
                running = dict(e["bar"])
            elif "arg_done" in e:
                running = {"done": e["arg_done"],
                           "total": e["arg_total"] + cons["SPL_POST"]}
            elif running is None and bar is not None:
                running = dict(bar)
            steps.append({
                "label": label, "phase": base, "kind": e["kind"],
                "ms": e["ms"], "t0": e["t0"], "note": mark(note),
                "bar": (dict(running,
                             pct=100.0 * running["done"]
                             / max(1, running["total"])) if running else None),
                "mem": sorted(set(x for x in mem if x)),
                "sectors": e["read_sectors"], "reads": e["reads"],
                "cyl": e["seek_cylinders"],
                "mech": e["transfer_ms"] + e["seek_ms"],
            })

        # WHAT MOVED, as blocks rather than as a sentence. The diff against the
        # previous stage's map is the honest answer for most of them - a block
        # that is new, or that changed its extent - and the firmware's own
        # areas are excluded because nothing os8088 does moves those. Where
        # the diff is empty and the stage still HAS a subject (the working
        # stack does not change any block's extent, it changes what a register
        # points at) the stage names it.
        prev = out[-1]["regions"] if out else []
        was = dict((r["id"], (r["a"], r["b"])) for r in prev)
        skip = ("unclaimed", "pending")     # ground shrinking is not a move
        moved = [r["id"] for r in regs
                 if r["cls"] != "bios" and not r["id"].startswith("free")
                 and r["id"] not in skip
                 and was.get(r["id"]) != (r["a"], r["b"])]
        if "focus" in st:
            moved = []
            for f in st["focus"]:
                moved += ([r["id"] for r in regs if r["id"].startswith("free")]
                          if f == "free*" else [f])

        # A NOTCH DRAWS WHAT THE READ BEFORE IT FETCHED. The bar's numerator is
        # sectors that have landed, and the read is what lands them - the
        # notch a moment later is only when the machine gets round to drawing
        # it. So a notch's reading is carried back over the steps between it
        # and the read that earned it, which is what makes clicking along a
        # stage's timeline walk the bar up instead of holding it at the value
        # it had when the stage opened.
        for i in range(len(steps) - 1, -1, -1):
            if steps[i]["phase"] != "splash tick" or not steps[i]["bar"]:
                continue
            v = steps[i]["bar"]
            for j in range(i - 1, -1, -1):
                steps[j]["bar"] = dict(v)
                if steps[j]["sectors"]:
                    break
                if steps[j]["phase"] == "splash tick":
                    break

        ms = sum(e["ms"] for e in ev)
        out.append({
            "moved_ids": moved,
            "zooms": zooms(regs, ram),
            "id": st["id"], "short": st.get("short", st["id"]),
            "title": st["title"], "moved": st["moved"],
            "ms": ms, "t0": t0, "regions": regs, "steps": steps,
            "bar": None if bar is None else {
                "done": bar["done"], "total": bar["total"],
                "pct": 100.0 * bar["done"] / max(1, bar["total"]),
                "msg": strs[MSG.get(st["id"], "kern")]},
        })
        t0 += ms

    return {
        "meta": {
            "machine": walkdata["machine"],
            "field": walkdata["machine"] == FIELD_MACHINE,
            "image": walkdata["image"], "ram_kb": walkdata["ram_kb"],
            "taken": walkdata["taken"], "total_ms": walkdata["total_ms"],
            "boot_ticks": walkdata["boot_ticks"],
            "boot_ticks_ms": walkdata["boot_ticks_ms"],
            "longest_run": walkdata["longest_run"],
            "desktop": walkdata.get("desktop", ""),
            # Where os8088 starts. Everything before this is the machine's own
            # firmware, and on a 5150 that is most of the wall clock - so the
            # page counts from here and says so, rather than burying nine
            # seconds of operating system inside a minute of memory test.
            "os0_ms": out[0]["ms"] if out else 0.0,
            "os_ms": sum(x["ms"] for x in out[1:]),
            "kernel_md5": hashlib.md5(
                open(os.path.join(ROOT, "build", "kernel.bin"), "rb").read()
            ).hexdigest() if os.path.exists(
                os.path.join(ROOT, "build", "kernel.bin")) else "",
            "image_md5": vol["md5"],
            "commit": subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                                     cwd=ROOT, capture_output=True,
                                     text=True).stdout.strip(),
            "defines": list(defines),
        },
        # THE WALK RIDES IN THE MODEL. Re-rendering is seconds and re-booting
        # is minutes, so the expensive half has to be something you can keep -
        # and one file that `--measure` will take back is a better answer than
        # two that have to be kept in step.
        "walk": walkdata,
        "gloss": _GLOSS_LC,
        "ram": ram,
        # The magnified strip's span: the top of the loader's blob, rounded up
        # to a whole 4KB so the frame is a round number and does not move when
        # the kernel changes size by a rung.
        "zoom": ((lad["kend"] * 16 + cons["BOOT2_SECS"] * vol["bps"]
                  + 4095) // 4096) * 4096,
        "cons": cons, "vol": vol, "strings": strs,
        "ladder": {k: lad[k] for k in
                   ("kseg", "cold_seg", "fat_seg", "low_seg", "vgabuf_seg",
                    "kend", "text", "bss", "cold", "lowbss", "vgabuf", "ovlw",
                    "ovl", "boot2", "ksize", "minramkb")},
        "ksecs": ksecs,
        "stages": out,
    }


# -----------------------------------------------------------------------------
# 5. The PAGE. One stylesheet, one script, and the whole model as
#    JSON - so the file WORKS with no network at all, which a page about a
#    machine that boots from a floppy ought to manage. The single exception
#    is the webfont link, and every rule that uses it names a real fallback:
#    offline, the page renders correctly in the system's own faces.
# -----------------------------------------------------------------------------

CSS = r''':root{
  --bg:#e9ecef; --panel:#fbfcfd; --ink:#12161a; --dim:#5d656e; --rule:#c8cfd6;
  --rule2:#e0e5ea; --accent:#15497f; --sel:#b8560a;
  --c-bios:#8b939c; --c-ours:#15497f; --c-kern:#1f7a58; --c-ovl:#b8560a;
  --c-data:#6a37bd; --c-claim:#0c6f86; --c-free:#dde2e7; --c-dead:#bcc4cc;
  --on-free:#5d656e;
}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){
  --bg:#0f1216; --panel:#181c21; --ink:#e4e9ee; --dim:#8a929c; --rule:#2b313a;
  --rule2:#20252b; --accent:#79aef2; --sel:#f2a552;
  --c-bios:#767e88; --c-ours:#5a9bef; --c-kern:#42bf95; --c-ovl:#f2a552;
  --c-data:#ab8cf7; --c-claim:#2fbdd4; --c-free:#232830; --c-dead:#3c434c;
  --on-free:#8a929c;
}}
:root[data-theme="dark"]{
  --bg:#0f1216; --panel:#181c21; --ink:#e4e9ee; --dim:#8a929c; --rule:#2b313a;
  --rule2:#20252b; --accent:#79aef2; --sel:#f2a552;
  --c-bios:#767e88; --c-ours:#5a9bef; --c-kern:#42bf95; --c-ovl:#f2a552;
  --c-data:#ab8cf7; --c-claim:#2fbdd4; --c-free:#232830; --c-dead:#3c434c;
  --on-free:#8a929c;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);
  font:14px/1.55 "IBM Plex Sans",ui-sans-serif,-apple-system,"Segoe UI",Roboto,
       Helvetica,Arial,sans-serif;
  -webkit-font-smoothing:antialiased;font-variant-numeric:tabular-nums}
.mono,code{font-family:"IBM Plex Mono",ui-monospace,"SF Mono",Menlo,Consolas,
  "DejaVu Sans Mono",monospace}
.wrap{max-width:1580px;margin:0 auto;padding:15px 20px 48px}

/* ---- the board: a rail of rungs, and the work beside it ----------------- */
.board{display:grid;grid-template-columns:252px minmax(0,1fr);gap:22px;
  align-items:start}
.rail{position:sticky;top:15px;min-width:0}
.work{min-width:0}
@media (max-width:900px){
  .board{grid-template-columns:1fr}
  .rail{position:static}
  .stages{flex-direction:row!important;flex-wrap:wrap}
  .st{flex:0 0 auto}
  .st .ms{margin-left:6px!important}
}

/* ---- header ---------------------------------------------------------- */

h1{font-family:"IBM Plex Sans Condensed","IBM Plex Sans",ui-sans-serif,
     Helvetica,Arial,sans-serif;
  font-size:23px;margin:0 0 5px;letter-spacing:-.005em;font-weight:700;
  text-wrap:balance}
h1 .sub{color:var(--dim);font-weight:400}
.lede{margin:5px 0 11px;color:var(--dim);font-size:11.5px;line-height:1.5}
footer .stamp{font-size:11.5px;color:var(--dim);margin:0 0 10px}
.stamp b{font-weight:600;color:var(--ink)}

/* ---- the loading-screen mimic ---------------------------------------- */
.splash{width:100%;aspect-ratio:4/3;flex:0 0 auto;background:#000;color:#fff;
  padding:8px 11px;border:1px solid var(--rule);border-radius:2px;position:relative;
  display:flex;flex-direction:column;justify-content:center;overflow:hidden}
.splash .logo{text-align:center;font-weight:800;letter-spacing:.22em;font-size:16px;
  padding:0 0 9px;perspective:420px}
.splash .logo span{display:inline-block;animation:flip8088 3.52s steps(16,end) infinite}
@keyframes flip8088{to{transform:rotateY(360deg)}}
.splash .dlg{border:1px solid #fff;padding:7px}
.splash .dlg2{border:1px solid #fff;padding:11px 10px 9px;text-align:center}
.splash .cap{font-size:12px;letter-spacing:.04em;margin-bottom:9px;
  font-family:"IBM Plex Sans",ui-sans-serif,Helvetica,sans-serif}
.splash .trough{border:1px solid #fff;height:14px;padding:1px}
.splash .fill{height:100%;background:#fff;width:0;transition:width .5s cubic-bezier(.4,0,.2,1)}
.splash .pct{font-size:12px;margin-top:7px}
.splash .msg{font-size:11.5px;margin-top:9px;color:#fff;min-height:1.3em;opacity:.92}
.splash.off{color:#3a3a3a}
.splash.off .dlg,.splash.off .dlg2{border-color:#000}
.splash.off .logo,.splash.off .cap,.splash.off .trough,.splash.off .pct{visibility:hidden}
.splash.off .dlg{border:none;padding:0}
.splash.off .msg{visibility:hidden}
/* Before os8088 owns the screen, the 5150 shows exactly this: a blinking
   block in the top-left corner and nothing else. */
.splash .desk{display:none;position:absolute;left:0;top:0;width:100%;height:100%;
  object-fit:fill;background:#000;image-rendering:pixelated}
.splash.done .desk{display:block}
.splash.done .curs,.splash.done .dlg,.splash.done .logo{display:none}
.splash .curs{display:none;position:absolute;left:12px;top:22px;width:9px;
  height:2px;background:#d2d2d2;animation:blink5150 1.07s steps(1,end) infinite}
.splash.off:not(.done) .curs{display:block}
@keyframes blink5150{0%,49%{opacity:1}50%,100%{opacity:0}}
.splash .note{font-size:10.5px;color:var(--dim);margin-top:8px;text-align:center}
.splash-outer{flex:0 0 auto}
.splash-outer{width:100%}
.splash-outer .cabin{font-size:10.5px;color:var(--dim);margin:6px 0 12px;
  text-align:center;line-height:1.4}
.grp{display:flex;justify-content:space-between;gap:8px;font-size:9.5px;
  letter-spacing:.11em;text-transform:uppercase;color:var(--dim);
  font-weight:600;margin:7px 2px 2px;padding-bottom:2px;
  border-bottom:1px solid var(--rule2)}
.grp:first-child{margin-top:0}
.grp span:last-child{font-family:"IBM Plex Mono",ui-monospace,monospace;
  letter-spacing:0;text-transform:none;opacity:.85}

/* ---- stage strip ------------------------------------------------------ */
.stages{display:flex;flex-direction:column;align-items:stretch;gap:2px;margin:0}
.st{appearance:none;border:1px solid var(--rule);background:var(--panel);color:var(--dim);
  font:inherit;font-size:11.5px;padding:3px 9px;border-radius:2px;cursor:pointer;
  display:flex;gap:7px;align-items:baseline;transition:.13s;text-align:left}
.st .n{min-width:1.2em;text-align:right}
.st .ms{margin-left:auto}
.st:hover{border-color:var(--accent);color:var(--ink)}
.st .n{font-weight:700;font-variant-numeric:tabular-nums}
.st .ms{font-size:10.5px;opacity:.75;font-family:ui-monospace,monospace}
.st[aria-current="true"]{background:var(--accent);border-color:var(--accent);color:#fff}
.st[aria-current="true"] .ms{opacity:.85}
.nav{display:flex;gap:5px;margin-top:6px}
.nav button{flex:1;width:auto}
.nav button{appearance:none;border:1px solid var(--rule);background:var(--panel);
  color:var(--ink);width:32px;height:29px;border-radius:2px;cursor:pointer;font-size:14px;
  line-height:1}
.nav button:hover{border-color:var(--accent)}
.nav button:disabled{opacity:.35;cursor:default}
.shead{display:flex;align-items:baseline;gap:14px;flex-wrap:wrap;
  min-height:26px}
.stitle{font-family:"IBM Plex Sans Condensed","IBM Plex Sans",ui-sans-serif,
     Helvetica,Arial,sans-serif;
  margin:0;font-size:20px;font-weight:700;letter-spacing:-.005em}
.smoved{color:var(--dim);font-size:12.5px;margin:0}
.smoved b{color:var(--sel);font-weight:600}

/* ---- section frames --------------------------------------------------- */
.panel{background:var(--panel);border:1px solid var(--rule);border-radius:3px;
  padding:9px 13px 8px;margin-top:9px}
.ph{font-size:10.5px;letter-spacing:.11em;text-transform:uppercase;color:var(--dim);
  font-weight:600;
  margin:0 0 2px;display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap}
.ph .hint{text-transform:none;letter-spacing:0;font-size:11px}

/* ---- memory map ------------------------------------------------------- */
.mlab{position:relative;transition:height .35s}
.mlab .lb{position:absolute;font-size:10.5px;line-height:1.25;white-space:nowrap;
  padding:1px 4px;border:1px solid transparent;border-radius:2px;
  cursor:pointer;transition:left .45s cubic-bezier(.4,0,.2,1),top .3s,opacity .3s,
  background .13s,border-color .13s;background:var(--panel)}
.mlab .lb .sz{color:var(--dim);font-family:ui-monospace,monospace;font-size:9.5px}
.mlab .lb:hover{border-color:var(--rule)}
.mlab .lb.hot{border-color:var(--sel);background:var(--sel);color:#fff}
.mlab .lb.hot .sz{color:rgba(255,255,255,.82)}
.mlab.soft .lb.hot{background:var(--panel);color:var(--sel);font-weight:600}
.mlab.soft .lb.hot .sz{color:var(--sel);opacity:.7}
.mlab .rz{position:absolute;width:1px;background:var(--rule);
  transition:left .45s cubic-bezier(.4,0,.2,1),top .3s,height .3s,opacity .3s}
/* The callouts recede with the blocks they point at - gently, because a label
   is words and a block is a colour, and .28 of a word is not readable. */
.mlab .lb.dim, .mlab .rz.dim, .mlab .jg.dim, .mlab .dt.dim{opacity:.42}
.mlab .rz.hot{background:var(--sel);width:1.5px;z-index:2}
.mlab .jg{position:absolute;height:1px;background:var(--rule);
  transition:left .45s cubic-bezier(.4,0,.2,1),top .3s,width .3s,opacity .3s}
.mlab .jg.hot{background:var(--sel);height:1.5px;z-index:2}
.mlab .dt{position:absolute;width:5px;height:5px;border-radius:50%;
  background:var(--rule);transition:left .45s cubic-bezier(.4,0,.2,1),top .3s,opacity .3s}
.mlab .dt.hot{background:var(--sel);box-shadow:0 0 0 2px var(--panel);z-index:3}
.mbar{position:relative;height:38px;margin-top:2px;border:1px solid var(--rule);
  background:var(--c-free);border-radius:2px;overflow:hidden}
.mbar .rg{position:absolute;top:0;height:100%;
  transition:left .45s cubic-bezier(.4,0,.2,1),width .45s cubic-bezier(.4,0,.2,1),
  opacity .3s,background .25s;cursor:pointer}
.mbar .rg.dim{opacity:.28}
/* A HAIRLINE OF PAGE BETWEEN THE RING AND THE BLOCK, because `--sel` IS the
   start-up code's own colour - so an orange ring drawn straight onto an orange
   block was the one selection on the map you could not see. */
.mbar .rg.hot{box-shadow:inset 0 0 0 2px var(--sel), inset 0 0 0 3.5px var(--panel)}
.mbar.soft .rg.hot{box-shadow:inset 0 0 0 1.5px var(--sel), inset 0 0 0 3px var(--panel)}
.movl{position:relative;height:15px;margin-top:-15px;pointer-events:none;z-index:3}
.movl .ov{position:absolute;height:15px;top:0;border:1.5px solid var(--c-ovl);
  background:repeating-linear-gradient(135deg,var(--c-ovl) 0 3px,transparent 3px 7px);
  border-radius:2px;pointer-events:auto;cursor:pointer;
  transition:left .45s cubic-bezier(.4,0,.2,1),width .45s cubic-bezier(.4,0,.2,1),opacity .3s}
.movl .ov.hot{box-shadow:0 0 0 1.5px var(--panel), 0 0 0 3.5px var(--sel);
  background:var(--c-ovl)}
.mrule{position:relative;height:15px;margin-top:3px}
.mrule span{position:absolute;font-size:10px;color:var(--dim);transform:translateX(-50%);
  font-family:ui-monospace,monospace}
.mrule span.e0{transform:translateX(0)}
.mrule span.e1{transform:translateX(-100%)}

/* ---- timeline --------------------------------------------------------- */
.tbar{position:relative;height:30px;border:1px solid var(--rule);border-radius:2px;
  overflow:hidden;background:var(--c-free)}
.tbar .sg{position:absolute;top:0;height:100%;cursor:pointer;
  border-right:1px solid var(--panel);
  transition:left .4s cubic-bezier(.4,0,.2,1),width .4s cubic-bezier(.4,0,.2,1),
  opacity .25s,filter .13s}
.tbar .sg:hover{filter:brightness(1.18)}
.tbar .sg.sel{box-shadow:inset 0 0 0 2px var(--ink)}
.tlab{position:relative;transition:height .3s}
.tlab .rz{position:absolute;width:1px;background:var(--rule)}
.tlab .rz.sel{background:var(--ink);width:1.5px;z-index:2}
.tlab .jg{position:absolute;height:1px;background:var(--rule)}
.tlab .jg.sel{background:var(--ink);height:1.5px;z-index:2}
.tlab .dt{position:absolute;width:5px;height:5px;border-radius:50%;background:var(--rule)}
.tlab .dt.sel{background:var(--ink);box-shadow:0 0 0 2px var(--panel);z-index:3}
.tlab .lb{position:absolute;font-size:10.5px;white-space:nowrap;
  padding:1px 4px;cursor:pointer;border:1px solid transparent;border-radius:2px;
  background:var(--panel)}
.tlab .lb:hover{border-color:var(--rule)}
.tlab .lb.sel{border-color:var(--ink);font-weight:600}
.tlab .lb .ms{font-family:ui-monospace,monospace;color:var(--dim);font-size:9.5px}
.tlab .lb.sel .ms{color:var(--ink)}

/* ---- detail ----------------------------------------------------------- */
.detail{display:grid;grid-template-columns:minmax(0,2.2fr) minmax(0,1fr);gap:22px}
@media(max-width:820px){.detail{grid-template-columns:1fr}}
.detail h3{margin:0 0 6px;font-size:15px;font-weight:620}
.detail p{margin:0 0 9px;font-size:13.5px;max-width:78ch}
.detail p.lit{margin:0;font-size:12px;color:var(--dim);white-space:nowrap;
  overflow:hidden;text-overflow:ellipsis;max-width:100%}
.detail p.lit b{color:var(--ink)}
.facts{border-top:1px solid var(--rule2);font-size:12px}
.facts.two{display:grid;grid-template-columns:1fr 1fr;column-gap:20px;
  border-top:none}
.facts.two > div:nth-child(-n+2){border-top:1px solid var(--rule2)}
.facts div{display:flex;justify-content:space-between;gap:14px;padding:4px 0;
  border-bottom:1px solid var(--rule2)}
.facts .k{color:var(--dim)}
.facts .v{font-family:ui-monospace,monospace;text-align:right}
.tag{display:inline-block;font-size:10px;letter-spacing:.06em;text-transform:uppercase;
  padding:1px 6px;border-radius:2px;border:1px solid var(--rule);color:var(--dim);
  margin-right:6px;vertical-align:1px}
.tag.meas{border-color:var(--c-kern);color:var(--c-kern)}
.tag.deriv{border-color:var(--c-ovl);color:var(--c-ovl)}
/* The memory state's own stamp. `measured` says a figure came off the running
   machine; this one says the opposite kind of thing - not a time at all, but
   what is sitting at an address at this point in the boot. */
.tag.hold{border-color:var(--c-claim);color:var(--c-claim)}
.legend{display:flex;gap:12px;flex-wrap:wrap;font-size:11px;color:var(--dim);margin-top:9px}
.legend i{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:4px;
  vertical-align:-1px}
.kbd{font-family:ui-monospace,monospace;font-size:10.5px;border:1px solid var(--rule);
  border-bottom-width:2px;border-radius:3px;padding:0 4px;color:var(--dim)}
footer{margin-top:24px;padding-top:14px;border-top:1px solid var(--rule);
  font-size:11.5px;color:var(--dim);max-width:88ch}
footer code{font-size:11px}

/* ---- the magnified detail ----------------------------------------------- */
/* A dimension bracket under the full map, connectors down to the strip, and a
   strip deliberately NARROWER than the map: the width difference is what says
   "this is that, enlarged" rather than "here is a second map". */
.zdim{position:relative;height:19px;margin-top:5px}
.zdim .br{position:absolute;top:7px;height:1px;background:var(--sel);
  transition:left .45s cubic-bezier(.4,0,.2,1),width .45s cubic-bezier(.4,0,.2,1)}
.zdim .tk{position:absolute;top:1px;width:1px;height:7px;background:var(--sel);
  transition:left .45s cubic-bezier(.4,0,.2,1)}
.zdim .lb{position:absolute;top:0;transform:translateX(-50%);font-size:10px;
  color:var(--sel);background:var(--panel);padding:0 5px;white-space:nowrap;
  font-family:"IBM Plex Mono",ui-monospace,monospace;
  transition:left .45s cubic-bezier(.4,0,.2,1)}
.zlink{display:block;width:100%;height:22px;overflow:visible}
.zlink polygon{fill:var(--sel);opacity:.10}
.zlink line{stroke:var(--sel);stroke-width:1}
.zrow{position:relative;height:30px}
.zwin{position:absolute;top:0;height:30px;border:1px solid var(--sel);
  border-radius:2px;overflow:hidden;background:var(--c-free)}
.zwin .rg{position:absolute;top:0;height:100%;cursor:pointer;overflow:hidden;
  transition:left .4s cubic-bezier(.4,0,.2,1),width .4s cubic-bezier(.4,0,.2,1),
  opacity .3s,background .25s}
.zwin .rg.dim{opacity:.28}
.zwin .rg.hot{box-shadow:inset 0 0 0 2px var(--sel), inset 0 0 0 3.5px var(--panel)}
.zrow.soft .zwin .rg.hot{box-shadow:inset 0 0 0 1.5px var(--sel),
  inset 0 0 0 3px var(--panel)}
.zwin .rg span{position:absolute;left:4px;top:50%;transform:translateY(-50%);
  font-size:10px;white-space:nowrap;color:#fff;text-shadow:0 1px 2px rgba(0,0,0,.45);
  pointer-events:none}
.zwin .rg.pale span{color:var(--on-free);text-shadow:none}
.zovl{position:absolute;height:16px;z-index:4;pointer-events:none}
.zovl .ov{position:absolute;height:16px;top:0;border:1.5px solid var(--c-ovl);
  border-radius:2px;pointer-events:auto;cursor:pointer;
  background:repeating-linear-gradient(135deg,var(--c-ovl) 0 4px,transparent 4px 9px);
  transition:left .4s cubic-bezier(.4,0,.2,1),width .4s cubic-bezier(.4,0,.2,1),opacity .3s}
.zovl .ov span{position:absolute;left:5px;top:50%;transform:translateY(-50%);
  font-size:9.5px;white-space:nowrap;color:var(--ink);font-weight:600;
  background:var(--panel);padding:0 3px;border-radius:2px}
.zovl .ov.hot{box-shadow:0 0 0 1.5px var(--panel), 0 0 0 3.5px var(--sel)}
/* GONE: THE BLOCK IS NOT IN THIS STAGE AT ALL, on any of the four hosts a
   block can live on. One rule listing all four, because it was three rules
   next to the things they hid and the fourth was simply never written - so
   the hatched band on the full map went on being drawn over stages that had
   destroyed it, while the same band on the magnified strip disappeared
   correctly. It has to sit after every `.dim` and `.hot` rule above to beat
   them, and being one rule is what keeps that true. */
.mbar .rg.gone, .movl .ov.gone, .zwin .rg.gone, .zovl .ov.gone{opacity:0}

.zcap{position:relative;height:15px;margin-top:5px}
.zcap span{position:absolute;top:0;transform:translateX(-50%);font-size:10.5px;
  color:var(--dim);white-space:nowrap;
  transition:left .45s cubic-bezier(.4,0,.2,1)}

/* ---- glossary ----------------------------------------------------------- */
.gl{font-weight:500;color:inherit;border-bottom:1px dotted var(--accent);
  cursor:help;outline:none}
.gl:hover,.gl:focus{background:var(--rule2)}
#gtip{position:fixed;z-index:99;max-width:330px;background:var(--ink);
  color:var(--bg);font-size:12px;line-height:1.45;padding:8px 11px;
  border-radius:3px;pointer-events:none;opacity:0;transition:opacity .12s;
  box-shadow:0 6px 22px rgba(0,0,0,.28)}
#gtip.on{opacity:1}
#gtip b{display:block;font-size:10px;letter-spacing:.08em;text-transform:uppercase;
  opacity:.65;margin-bottom:3px;font-weight:600}

/* ---- the freshly-booted desktop ---------------------------------------- */
.shot{margin:2px 0 12px}
.shot img{display:block;width:100%;max-width:520px;border:1px solid var(--rule);
  border-radius:2px;background:#000;image-rendering:pixelated}
.shot figcaption{font-size:11px;color:var(--dim);margin-top:5px;max-width:520px}

a:focus-visible,button:focus-visible,.st:focus-visible{outline:2px solid var(--accent);
  outline-offset:2px}
@media (prefers-reduced-motion:reduce){
  *,*::before,*::after{transition-duration:.001ms!important;animation-duration:.001ms!important;
    animation-iteration-count:1!important}
}
'''

JS = r'''(function(){
"use strict";
var D = window.LADDER, S = D.stages;
/* THREE STATES PER STAGE, and only ever one of them at a time.

   `start`    - you have just arrived, or clicked the stage again. The panel
                describes the stage, the screen shows the bar as the stage
                OPENED, and the map outlines what this stage moved.
   `timeline` - you clicked a step. The panel describes the step, the screen
                moves to where the bar stood when it finished, and the memory
                the step touched is filled in on the map.
   `memory`   - you clicked a block or its label, on the full map or the
                magnified strip. The panel describes what that area HOLDS at
                this point in the boot; the block is filled in.

   A region is not a moment, so the memory state leaves the screen on the
   stage's opening reading rather than inventing a time for it. Selecting in
   one place clears the other, which is what keeps the panel and the two
   highlights describing the same thing. */
var stage = 0, step = -1, pick = null;
var CLS = {bios:"--c-bios", ours:"--c-ours", kern:"--c-kern", ovl:"--c-ovl",
           data:"--c-data", claim:"--c-claim", free:"--c-free", dead:"--c-dead"};
var KIND = {rom:"bios", disk:"ours", kernel:"kern", draw:"data"};
var $ = function(id){ return document.getElementById(id); };
function col(c){ return "var(" + (CLS[c] || "--c-free") + ")"; }
function bytes(n){
  if (n >= 1024*1024) return (n/1048576).toFixed(1) + " MB";
  if (n >= 1024) return (n/1024 >= 100 ? Math.round(n/1024) : (n/1024).toFixed(1)) + " KB";
  return n + " B";
}
function hex(n, w){ var s = n.toString(16).toUpperCase(); while (s.length < (w||5)) s = "0"+s; return s; }
function ms(v){
  if (v >= 1000) return (v/1000).toFixed(2) + " s";
  if (v >= 10) return v.toFixed(0) + " ms";
  if (v >= 1) return v.toFixed(1) + " ms";
  return v.toFixed(2) + " ms";
}

/* --------------------------------------------------------------------------
   Labels with risers. Two passes, because a label's width is not knowable
   until it is in the document: place them all on one row, measure, then drop
   each into the lowest row where it does not touch its neighbour. The riser
   is drawn from the bar's edge to whatever row the label landed on, which is
   what makes a narrow region legible without widening it and lying about it.
   -------------------------------------------------------------------------- */
function layout(host, items, W, up){
  var ROW = 17, PAD = 5, GAP = 9, FAN = 260;
  /* THE WHOLE CONSTRUCTION IS MIRRORED FOR LABELS BELOW THE BAR, and doing it
     by flipping x into W - x, running the identical packer and flipping back
     is what keeps this one piece of code with one argument behind it.

     Above the bar a label extends RIGHT of its anchor and the leftmost sits
     farthest away; below it a label extends LEFT and the leftmost sits
     NEAREST - which is both what the no-crossing argument needs and what
     reads correctly, step one's label being the one closest to the timeline.

     The argument itself, in three lines. A riser only passes rows between its
     own and the bar. Rows are ordered so those rows all belong to anchors on
     the side the labels grow AWAY from, so none of their labels can cover it.
     And every jog that has any length at all gets a row to itself, so no two
     horizontals share a y. Two labels share a row only when the second needs
     no jog and clears the first. */
  var flip = !up;
  var seq = items.map(function(it){
    return {it: it, u: flip ? (W - it.x) : it.x};
  }).sort(function(a, b){ return a.u - b.u; });

  var row = -1, cur = -1e9, prev = -1e9;
  seq.forEach(function(s){
    var w = s.it.el.getBoundingClientRect().width;
    /* Three floors, and the label takes the highest. `prev + GAP` (the
       previous ANCHOR) is the one the argument above rests on; clearing the
       previous LABEL as well is what fans a crowd out, and FAN stops that
       running off the end of a 640KB bar whose first eighth holds nine
       regions. */
    var u = Math.max(s.u, prev + GAP, Math.min(cur + GAP, s.u + FAN));
    if (u + w > W) u = Math.max(0, W - w);
    if (u < s.u) u = Math.min(s.u, Math.max(0, W - w));
    var jog = u > s.u + 0.5;
    if (row < 0 || jog || u < cur + GAP) row++;
    s.row = row; s.u0 = u; s.w = w;
    cur = u + w; prev = s.u;
  });

  var n = row + 1, H = PAD + n * ROW + 4;
  /* THE RESERVE IS PART OF THE GEOMETRY, not just of the box. Reserving the
     tallest stack stretches this container, and everything below was still
     being placed against the height the stack NEEDED - so on any stage with
     fewer rungs than the tallest, the risers and their dots stopped short of
     the bar by the difference. The spare goes ABOVE the labels, which keeps
     them against the bar they point at. */
  var HE = Math.max(H, +(host.dataset.reserve || 0));
  var off = up ? (HE - H) : 0;
  seq.forEach(function(s){
    var it = s.it;
    var top = PAD + (up ? s.row : (n - 1 - s.row)) * ROW + off;
    it.el.style.left = (flip ? (W - s.u0 - s.w) : s.u0) + "px";
    it.el.style.top = top + "px";
    /* The jog sits just clear of the label, on the side the bar is on, and
       meets the label edge NEAREST the anchor. */
    var hy = up ? (top + ROW - 4) : (top - 4);
    var lx = flip ? (W - s.u0) : s.u0;
    var l0 = flip ? (W - s.u0 - s.w) : s.u0;
    var a = Math.min(it.x, lx), b = Math.max(it.x, lx);
    it.jg.style.left = a + "px";
    it.jg.style.width = Math.max(0, b - a) + "px";
    it.jg.style.top = hy + "px";
    /* No jog where the label already SITS over its anchor. That happens at
       the container edge, where a label wider than the room to its side has
       to overhang - and a horizontal drawn under its own label reads as a
       leader pointing somewhere else. */
    var covers = it.x >= l0 - 2 && it.x <= l0 + s.w + 2;
    it.jg.style.opacity = (!covers && (b - a) > 1) ? "1" : "0";
    it.rz.style.left = it.x + "px";
    it.rz.style.top = up ? hy + "px" : "0px";
    it.rz.style.height = Math.max(2, up ? (HE - hy) : hy) + "px";
    if (it.dt){
      it.dt.style.left = (it.x - 2.5) + "px";
      it.dt.style.top = (up ? HE - 5 : 0) + "px";
    }
  });
  /* The NATURAL height is what a reserve is worked out from; the height the
     block actually takes is that or the reserve, whichever is larger. */
  host.dataset.nat = H;
  host.style.height = HE + "px";
}

/* --------------------------------------------------------------------------
   RESERVE THE TALLEST, ONCE. The callout stack is as tall as the stage needs -
   three rungs on the first stage and thirteen on the twelfth - so the panels
   below it moved every time you stepped, and the thing you were about to
   click moved with them. Every stage is laid out once at startup, the tallest
   of each block is remembered, and every stage is drawn at that height from
   then on. The cost is whitespace on the quiet stages; what it buys is a page
   that sits still while you arrow through it.

   It runs again when the webfont arrives, because everything measured here was
   measured in the fallback face.
   -------------------------------------------------------------------------- */
function reserve(){
  var m = $("mlab"), t = $("tlab"), d = document.querySelector(".panel.detail");
  var cab = $("scab");
  var keepStage = stage, keepStep = step, keepPick = pick;
  pick = null;
  m.dataset.reserve = 0; t.dataset.reserve = 0;
  d.style.minHeight = ""; cab.style.minHeight = "";
  var mx = 0, tx = 0, dx = 0, cx = 0;
  function sample(){
    drawMap(); drawTime(); drawDetail(); drawSplash();
    mx = Math.max(mx, +m.dataset.nat || 0);
    tx = Math.max(tx, +t.dataset.nat || 0);
    dx = Math.max(dx, d.getBoundingClientRect().height);
    /* The line under the screen is three lines while nothing can draw and two
       once it can, and the rungs below it moved by the difference. */
    cx = Math.max(cx, cab.getBoundingClientRect().height);
  }
  for (var i = 0; i < S.length; i++){
    stage = i; step = -1; sample();
    /* The two steps that make the panel tallest: the wordiest, and the first
       one with disk figures, which carries four more rows of them. */
    var wordy = -1, best = -1, disky = -1, j;
    for (j = 0; j < S[i].steps.length; j++){
      var L = (S[i].steps[j].note || "").length;
      if (L > best){ best = L; wordy = j; }
      if (disky < 0 && S[i].steps[j].sectors) disky = j;
    }
    if (wordy >= 0){ step = wordy; sample(); }
    if (disky >= 0){ step = disky; sample(); }
    /* AND THE MEMORY STATE, which is a third panel with its own height: six
       fact rows rather than four or five, and on a block this stage moved an
       extra line naming what moved. Both extremes are sampled, because the
       wordiest block is not usually one of the ones that moved. */
    step = -1;
    var rw = -1, rb = -1, rm = -1, k;
    for (k = 0; k < S[i].regions.length; k++){
      var rr = S[i].regions[k], RL = (rr.note || "").length;
      if (RL > rb){ rb = RL; rw = k; }
      if (rm < 0 && (S[i].moved_ids || []).indexOf(rr.id) >= 0) rm = k;
    }
    if (rw >= 0){ pick = S[i].regions[rw].id; sample(); }
    if (rm >= 0){ pick = S[i].regions[rm].id; sample(); }
    pick = null;
  }
  /* One row of slack on the timeline: selecting a step that is too small to
     label anyway gives it one, which can add a rung to that stack. */
  m.dataset.reserve = Math.ceil(mx);
  t.dataset.reserve = Math.ceil(tx) + 17;
  /* A little slack on the panel below everything else: the three samples per
     stage do not cover every step's exact facts, and being a few pixels short
     there is the one place it costs nothing to be generous. */
  d.style.minHeight = (Math.ceil(dx) + 14) + "px";
  cab.style.minHeight = Math.ceil(cx) + "px";
  stage = keepStage; step = keepStep; pick = keepPick;
}

/* --------------------------------------------------------------------------
   The memory map. Blocks are keyed by region id and reused across stages, so
   a block that survives a stage change SLIDES to its new place instead of
   being torn down and rebuilt - which is the whole reason the page animates:
   the thing you are watching is memory moving, and a cut hides the move.
   -------------------------------------------------------------------------- */
var mblocks = {}, mlabels = {};
function drawMap(){
  var st = S[stage], bar = $("mbar"), lab = $("mlab"), ovh = $("movl");
  var W = bar.clientWidth || 1, ram = D.ram;
  /* WITH NOTHING SELECTED, THE HIGHLIGHT IS WHAT THIS STAGE MOVED - the same
     blocks the line under the title names, so the sentence and the picture
     agree without the reader having to find the correspondence. Picking a
     step on the timeline hands the highlight over to that step's own memory. */
  var hot = {};
  (pick ? [pick]
        : ((step >= 0 ? st.steps[step].mem : st.moved_ids) || []))
    .forEach(function(id){ hot[id] = 1; });
  /* Two highlights, told apart on purpose: what a STAGE moved is outlined,
     because it can be six blocks at once and a filled six is a shout; what a
     STEP uses - or the one block you clicked - is filled, because it is an
     answer to something the reader just did. */
  var soft = step < 0 && !pick;
  $("mbar").classList.toggle("soft", soft);
  $("zrow").classList.toggle("soft", soft);
  $("mlab").classList.toggle("soft", soft);
  /* The size threshold below exists to stop thirteen crowded regions putting
     thirteen rungs on the stack. Where a stage HAS no crowd it should not
     apply at all - the first stage owns three regions, and hiding all three
     leaves a map with nothing named on it. */
  var named = st.regions.filter(function(r){
    return r.id.indexOf("free") !== 0 && r.id !== "unclaimed";
  }).length;
  var crowded = named > 7;
  var seen = {}, items = [];
  st.regions.forEach(function(r){
    seen[r.id] = 1;
    var host = r.layer ? ovh : bar;
    var b = mblocks[r.id];
    if (!b){
      b = document.createElement("div");
      b.className = (r.layer ? "ov" : "rg") + " gone";
      b.style.left = (100 * r.a / ram) + "%";
      b.style.width = (100 * Math.max(r.b - r.a, ram/900) / ram) + "%";
      host.appendChild(b);
      mblocks[r.id] = b;
      b.addEventListener("click", function(){ pickRegion(r.id); });
      requestAnimationFrame(function(){ b.classList.remove("gone"); });
    }
    if (b.parentNode !== host) host.appendChild(b);
    /* VISIBILITY IS A CLASS, NOT AN INLINE STYLE. It was inline, set to 1 on
       every draw - which beat `.dim` on specificity, so the fading of what you
       did NOT select had never once happened on either map. */
    b.classList.remove("gone");
    b.style.pointerEvents = "";
    b.style.left = (100 * r.a / ram) + "%";
    b.style.width = (100 * Math.max(r.b - r.a, ram/900) / ram) + "%";
    if (!r.layer) b.style.background = col(r.cls);
    b.title = r.label + "  " + hex(r.a) + "-" + hex(r.b) + "  " + bytes(r.b - r.a);
    var anyHot = (step >= 0 || pick) && Object.keys(hot).length;
    b.classList.toggle("dim", !!(anyHot && !hot[r.id] && r.cls !== "free"));
    b.classList.toggle("hot", !!hot[r.id]);

    /* label it if it is worth a label: everything but the anonymous free runs */
    /* THE FULL MAP LABELS WHAT YOU CAN SEE ON IT. Nine of a stage's thirteen
       regions are inside the first fifth of memory and several are a few
       hundred bytes, so labelling every one puts thirteen rungs on the stack
       - and the rungs come from the ANCHORS being crowded, not from the words
       being long, so no amount of shortening fixes it. The small ones are
       named on the magnified strip directly below, which is what the strip is
       for. Anonymous runs of free pool are unlabelled either way. */
    var anon = r.id.indexOf("free") === 0 || r.id === "unclaimed";
    var wantLabel = !crowded || (r.b - r.a) >= ram * 0.007;
    if (anon) wantLabel = (r.b - r.a) > ram * 0.10;
    var L = mlabels[r.id];
    if (wantLabel){
      if (!L){
        L = document.createElement("div"); L.className = "lb";
        var rz = document.createElement("div"); rz.className = "rz";
        var jg = document.createElement("div"); jg.className = "jg";
        var dt = document.createElement("div"); dt.className = "dt";
        lab.appendChild(rz); lab.appendChild(jg); lab.appendChild(dt);
        lab.appendChild(L);
        mlabels[r.id] = L; L._rz = rz; L._jg = jg; L._dt = dt;
        L.addEventListener("click", function(){ pickRegion(r.id); });
      }
      L.innerHTML = "";
      L.appendChild(document.createTextNode((r.sl || r.label) + " "));
      var sz = document.createElement("span"); sz.className = "sz";
      sz.textContent = bytes(r.b - r.a);
      L.appendChild(sz);
      L.style.opacity = "1"; L.style.pointerEvents = "";
      L._rz.style.opacity = "1";
      var lo = !!(anyHot && !hot[r.id]);
      L.classList.toggle("hot", !!hot[r.id]);
      L._rz.classList.toggle("hot", !!hot[r.id]);
      L._jg.classList.toggle("hot", !!hot[r.id]);
      L._dt.classList.toggle("hot", !!hot[r.id]);
      L.classList.toggle("dim", lo);
      L._rz.classList.toggle("dim", lo);
      L._jg.classList.toggle("dim", lo);
      L._dt.classList.toggle("dim", lo);
      L._dt.style.opacity = "1";
      items.push({el: L, rz: L._rz, jg: L._jg, dt: L._dt,
                  x: W * (r.a + r.b) / 2 / ram, id: r.id});
    } else if (L){
      /* All four, or the dot outlives the label it belonged to and sits on
         the bar pointing at nothing. */
      L.style.opacity = "0"; L.style.pointerEvents = "none";
      L._rz.style.opacity = "0";
      L._jg.style.opacity = "0"; L._dt.style.opacity = "0";
    }
  });
  /* A BLOCK FADED OUT IS STILL IN THE DOCUMENT AND STILL TAKES CLICKS, sitting
     over the bar at the extent it had when it last existed. That was harmless
     while a click did nothing; now that it selects, a block from the previous
     stage would answer for one under it. Hidden means unclickable here. */
  Object.keys(mblocks).forEach(function(id){
    if (!seen[id]){
      mblocks[id].classList.add("gone");
      mblocks[id].style.pointerEvents = "none";
      if (mlabels[id]){
        mlabels[id].style.opacity = "0";
        mlabels[id].style.pointerEvents = "none";
        mlabels[id]._rz.style.opacity = "0";
        mlabels[id]._jg.style.opacity = "0";
        mlabels[id]._dt.style.opacity = "0";
      }
    }
  });
  layout(lab, items, W, true);

  drawZoom(hot, soft);

  var ru = $("mrule");
  if (!ru.childNodes.length){
    for (var k = 0; k <= 640; k += 128){
      var s = document.createElement("span");
      s.textContent = k ? k + "K" : "0";
      s.style.left = (100 * k / 640) + "%";
      if (k === 0) s.className = "e0";
      if (k === 640) s.className = "e1";
      ru.appendChild(s);
    }
  }
}
/* This used to hunt for a timeline step that touched the block and select
   THAT, which answered a different question from the one being asked: a block
   is a thing that exists, and several steps can have written to it. It selects
   the block itself now, and the panel describes what is in it. */
function pickRegion(id){ setRegion(id); }


/* --------------------------------------------------------------------------
   THE MAGNIFIED DETAIL. At 640KB to scale the start-up code is 5,215 bytes -
   four fifths of one per cent, a sliver you cannot see and certainly cannot
   see LYING ACROSS two blocks. What it is saying is a shape, so it needs a
   scale that shows the shape.

   The window FOLLOWS THE STAGE rather than sitting at a fixed address, and
   there can be two of them: early on everything of ours is 512 bytes at the
   bottom and 2.5KB at the very top, and one window over both magnifies
   nothing. A bracket under the full map measures the slice, connectors run
   down to the strip's own edges, and the strip is deliberately narrower than
   the map - the width difference is what makes it read as an enlargement
   rather than as a second, different map.
   -------------------------------------------------------------------------- */
var zblocks = {};
function drawZoom(hot, soft){
  var st = S[stage], wins = st.zooms || [], W = $("mbar").clientWidth || 1;
  var dim = $("zdim"), row = $("zrow"), cap = $("zcap"), link = $("zlink");
  dim.innerHTML = ""; cap.innerHTML = "";
  var anyHot = (step >= 0 || pick) && hot && Object.keys(hot).length;

  /* Widths: proportional to the square root of the span, so a 6KB window and
     a 124KB one are both legible, with a floor and a gap between. */
  var GAP = 5, n = wins.length, tot = 0, sh = [], i;
  for (i = 0; i < n; i++){ sh[i] = Math.sqrt(wins[i].b - wins[i].a); tot += sh[i]; }
  var avail = (n ? 100 - GAP * (n - 1) : 0) * (n === 1 ? 0.74 : 0.92);
  var w = [], at = [];
  for (i = 0; i < n; i++) w[i] = Math.max(avail / n * 0.62, avail * sh[i] / tot);
  var sum = 0; for (i = 0; i < n; i++) sum += w[i];
  for (i = 0; i < n; i++) w[i] *= avail / sum;
  /* One window is centred UNDER ITS OWN BRACKET, not in the middle of the
     panel: a symmetric pair of connectors reads as an enlargement, and a
     lopsided one reads as an arrow pointing somewhere. */
  var cur;
  if (n === 1){
    var mid = 100 * (wins[0].a + wins[0].b) / 2 / D.ram;
    cur = Math.min(Math.max(0, mid - w[0] / 2), 100 - w[0]);
  } else {
    cur = (100 - (avail + GAP * (n - 1))) / 2;
  }
  for (i = 0; i < n; i++){ at[i] = cur; cur += w[i] + GAP; }

  /* SHOW THE WINDOWS BEFORE PUTTING ANYTHING IN THEM. This ran at the END,
     after the blocks had been laid out and their names measured against the
     room available - so on a stage that opens a SECOND window, that window
     was still `display:none` while its own blocks asked how wide it was, got
     nought, and dropped every label. Which window a stage has depends on the
     stage you came from, so the names came and went with the route. */
  for (i = 0; i < n; i++){ zwin(i); zovl(i); }
  for (i = zwinN; i > n; i--){ zwin(i - 1).style.display = "none";
                               zovl(i - 1).style.display = "none"; }
  for (i = 0; i < n; i++){ zwin(i).style.display = ""; zovl(i).style.display = ""; }

  var seen = {}, poly = [];
  for (i = 0; i < n; i++){
    var win = wins[i], span = win.b - win.a;
    var host = zwin(i), ovh = zovl(i);
    host.style.left = at[i] + "%"; host.style.width = w[i] + "%";
    ovh.style.left = at[i] + "%"; ovh.style.width = w[i] + "%";
    ovh.style.top = "-8px";

    /* the bracket under the full map, measuring what this window covers */
    var x0 = W * win.a / D.ram, x1 = W * win.b / D.ram;
    dim.appendChild(el("div", "br", {left: x0 + "px", width: (x1 - x0) + "px"}));
    dim.appendChild(el("div", "tk", {left: x0 + "px"}));
    dim.appendChild(el("div", "tk", {left: (x1 - 1) + "px"}));
    var lb = el("div", "lb", {left: ((x0 + x1) / 2) + "px"});
    lb.textContent = hex(win.a) + "–" + hex(win.b);
    dim.appendChild(lb);
    var lw = lb.offsetWidth / 2 + 4;
    lb.style.left = Math.min(Math.max(lw, (x0 + x1) / 2), W - lw) + "px";
    poly.push([x0, x1, W * at[i] / 100, W * (at[i] + w[i]) / 100]);

    var c = el("span", "", {left: (at[i] + w[i] / 2) + "%"});
    c.textContent = bytes(span) + " at " + Math.round(D.ram / span) + "× actual size";
    cap.appendChild(c);

    st.regions.forEach(function(r){
      if (r.b <= win.a || r.a >= win.b) return;
      var id = i + "/" + r.id;
      seen[id] = 1;
      var into = r.layer ? ovh : host;
      var el2 = zblocks[id];
      if (!el2){
        el2 = document.createElement("div");
        el2.className = (r.layer ? "ov" : "rg") + " gone";
        el2.appendChild(document.createElement("span"));
        into.appendChild(el2);
        zblocks[id] = el2;
        el2.addEventListener("click", function(){ pickRegion(r.id); });
      }
      if (el2.parentNode !== into) into.appendChild(el2);
      el2.style.pointerEvents = "";
      var ra = Math.max(r.a, win.a), rb = Math.min(r.b, win.b);
      var pw = 100 * (rb - ra) / span;
      el2.classList.remove("gone");
      el2.style.left = (100 * (ra - win.a) / span) + "%";
      el2.style.width = Math.max(pw, 0.4) + "%";
      if (!r.layer){
        el2.style.background = col(r.cls);
        el2.classList.toggle("pale", r.cls === "free" || r.cls === "dead");
      }
      el2.title = r.label + "  " + hex(r.a) + "-" + hex(r.b) + "  " + bytes(r.b - r.a);
      el2.classList.toggle("dim", !!(anyHot && !hot[r.id] && r.cls !== "free"));
      el2.classList.toggle("hot", !!hot[r.id]);
      /* MEASURE THE WIDTH THE BLOCK IS GOING TO BE, not the one it is at.
         A block's width is animated, so `clientWidth` during a draw is
         wherever the transition has got to - which on a stage change is the
         width it had in the PREVIOUS stage, or zero on the frame it is born.
         The label was dropped against that and nothing re-measured when the
         animation finished, so whether a name appeared depended on which
         stage you arrived from. The hosts are not animated, so the target is
         exactly the share of one this block was just given. */
      var want = Math.max(pw, 0.4) / 100 * into.clientWidth;
      var t = el2.firstChild;
      t.textContent = r.sl || r.label;
      t.style.visibility = "hidden";
      if (t.scrollWidth + 10 > want) t.textContent = "";
      t.style.visibility = "";
    });
  }
  Object.keys(zblocks).forEach(function(k){
    if (!seen[k]){ zblocks[k].classList.add("gone");
                   zblocks[k].style.pointerEvents = "none"; }
  });
  /* the connectors: a faint web from the bracket's ends to the strip's */
  var H = 22;
  link.setAttribute("viewBox", "0 0 " + W + " " + H);
  link.setAttribute("preserveAspectRatio", "none");
  link.innerHTML = poly.map(function(q){
    return '<polygon points="' + q[0] + ',0 ' + q[1] + ',0 ' + q[3] + ',' + H +
           ' ' + q[2] + ',' + H + '"/>' +
           '<line x1="' + q[0] + '" y1="0" x2="' + q[2] + '" y2="' + H + '"/>' +
           '<line x1="' + q[1] + '" y1="0" x2="' + q[3] + '" y2="' + H + '"/>';
  }).join("");
}
var zwinN = 0;
function zwin(i){
  var id = "zwin" + i, e = $(id);
  if (!e){
    e = document.createElement("div"); e.className = "zwin"; e.id = id;
    $("zrow").appendChild(e); zwinN = Math.max(zwinN, i + 1);
  }
  return e;
}
function zovl(i){
  var id = "zovl" + i, e = $(id);
  if (!e){
    e = document.createElement("div"); e.className = "zovl"; e.id = id;
    $("zrow").appendChild(e);
  }
  return e;
}
function el(tag, cls, css){
  var e = document.createElement(tag);
  if (cls) e.className = cls;
  for (var k in css) e.style[k] = css[k];
  return e;
}

/* -------------------------------------------------------------------------- */
var tblocks = {}, tlabels = {};
function drawTime(){
  var st = S[stage], bar = $("tbar"), lab = $("tlab");
  var W = bar.clientWidth || 1;
  var n = st.steps.length, tot = 0, i;
  for (i = 0; i < n; i++) tot += st.steps[i].ms;
  /* A SOFT FLOOR, not a hard one: every segment gets the same small constant
     added before normalising, so ordering and ratios survive and a 0.03 ms
     call is still something you can hit with a mouse. The label carries the
     exact figure, which is the number to read. */
  var pad = 0.35 / n, sum = 0, w = [];
  for (i = 0; i < n; i++){ w[i] = (tot ? st.steps[i].ms / tot : 1/n) + pad; sum += w[i]; }
  /* WHICH STEPS GET A LABEL. All of them where there are few; otherwise the
     longest LABEL_MAX by time, which on the stage that loads the kernel means
     the reads and not the housekeeping between them. The rest keep their
     segment, their hover and their click. */
  var LABEL_MAX = 7, keep = {};
  if (n <= LABEL_MAX){
    for (i = 0; i < n; i++) keep[i] = 1;
  } else {
    var order = [];
    for (i = 0; i < n; i++) order.push(i);
    order.sort(function(a, b){ return st.steps[b].ms - st.steps[a].ms; });
    for (i = 0; i < LABEL_MAX; i++) keep[order[i]] = 1;
  }
  bar.innerHTML = ""; lab.innerHTML = "";
  var at = 0, items = [];
  for (i = 0; i < n; i++){
    var sp = st.steps[i], fw = w[i] / sum;
    var el = document.createElement("div");
    el.className = "sg" + (i === step ? " sel" : "");
    el.style.left = (100 * at) + "%";
    el.style.width = (100 * fw) + "%";
    el.style.background = col(KIND[sp.kind] || "kern");
    el.title = sp.label + " - " + ms(sp.ms);
    (function(j){ el.addEventListener("click", function(){ setStep(j); }); })(i);
    bar.appendChild(el);
    var big = keep[i] || i === step;
    if (big){
      var L = document.createElement("div");
      L.className = "lb" + (i === step ? " sel" : "");
      L.appendChild(document.createTextNode(short(sp.label) + " "));
      var m = document.createElement("span"); m.className = "ms"; m.textContent = ms(sp.ms);
      L.appendChild(m);
      (function(j){ L.addEventListener("click", function(){ setStep(j); }); })(i);
      var rz = document.createElement("div"); rz.className = "rz" + (i === step ? " sel" : "");
      var jg = document.createElement("div"); jg.className = "jg" + (i === step ? " sel" : "");
      var dt = document.createElement("div"); dt.className = "dt" + (i === step ? " sel" : "");
      lab.appendChild(rz); lab.appendChild(jg); lab.appendChild(dt); lab.appendChild(L);
      items.push({el: L, rz: rz, jg: jg, dt: dt, x: W * (at + fw/2)});
    }
    at += fw;
  }
  layout(lab, items, W, false);
}
function short(s){ return s.length > 44 ? s.slice(0, 43) + "…" : s; }

/* --------------------------------------------------------------------------
   THE MEMORY STATE. A block is not a moment in the boot, so this panel answers
   "what is in here, now" and not "what happened" - the address, how much of
   the machine it is, and whether this stage is the one that put it there.
   -------------------------------------------------------------------------- */
/* THE LEGEND'S OWN WORDS, so the row names the colour the reader can see under
   the block rather than a synonym of it. `dead` has no swatch - it is the
   living colour, faded - so it is the one entry with wording of its own. */
var KINDNAME = {bios: "firmware", ours: "boot sector / disk",
                kern: "operating system", ovl: "start-up code",
                data: "disk index", claim: "handed out",
                free: "free", dead: "finished with"};
function showRegion(r){
  var st = S[stage];
  $("dtitle").textContent = r.label;
  $("dbody").innerHTML = "";
  var p = document.createElement("p");
  p.innerHTML = "<span class='tag hold'>holds</span>" + (r.note || "");
  $("dbody").appendChild(p);
  /* WHERE IT CAME FROM. Two different facts, and they were one line saying the
     stage's own `what moves` sentence - which describes the STAGE, so on a
     block the stage merely resized it named a different block entirely. */
  var fresh = born(r.id) === "this stage";
  var mine = (st.moved_ids || []).indexOf(r.id) >= 0;
  if (fresh || mine){
    var q = document.createElement("p");
    q.className = "lit";
    q.innerHTML = fresh
      ? "<b>New in this stage</b> — this block is on no earlier rung of the "
        + "ladder."
      : "<b>Changed in this stage</b> — it was already on the map, and this "
        + "stage moved it or altered its extent.";
    $("dbody").appendChild(q);
  }
  var span = r.b - r.a;
  facts([["address", hex(r.a) + " – " + hex(r.b)],
         ["size", bytes(span) + " / " + span.toLocaleString() + " B"],
         ["segment", hex(r.a >> 4, 4) + ":0000"],
         ["what it is", KINDNAME[r.cls] || r.cls],
         ["share of 640K", (100 * span / D.ram).toFixed(2) + "%"],
         ["first appears", born(r.id)]]);
}
/* Every stage's map is on the page, so the stage a block ARRIVED in is a fact
   this can work out rather than approximate: it is the first one whose map has
   it. "By this stage at the latest" was true and useless. */
function born(id){
  for (var i = 0; i < S.length; i++)
    for (var j = 0; j < S[i].regions.length; j++)
      if (S[i].regions[j].id === id)
        return i === stage ? "this stage" : (i + 1) + ". " + S[i].short;
  return "\u2014";
}
function facts(rows){
  var f = $("dfacts"); f.innerHTML = "";
  f.classList.toggle("two", rows.filter(function(r){
    return r[1] !== null && r[1] !== undefined && r[1] !== "";
  }).length > 5);
  rows.forEach(function(kv){
    if (kv[1] === null || kv[1] === undefined || kv[1] === "") return;
    var d = document.createElement("div");
    var a = document.createElement("span"); a.className = "k"; a.textContent = kv[0];
    var b = document.createElement("span"); b.className = "v"; b.textContent = kv[1];
    d.appendChild(a); d.appendChild(b); f.appendChild(d);
  });
}
function drawDetail(){
  var st = S[stage];
  if (pick){
    var r = st.regions.filter(function(x){ return x.id === pick; })[0];
    /* A block can stop existing when you arrow into the next stage; the
       selection is cleared with the stage, so this only guards a resize
       landing between the two. */
    if (r){ showRegion(r); return; }
    pick = null;
  }
  if (step < 0){
    $("dtitle").textContent = st.title;
    $("dbody").innerHTML = "";
    var p = document.createElement("p");
    p.innerHTML = "<span class='tag meas'>measured</span>" +
      "This stage is " + ms(st.ms) + " of the boot. Click a segment of the " +
      "timeline above to see what that step does — the memory it touches " +
      "lights up on the map. Click a block on the map, or the label rising " +
      "from it, to read what that area holds.";
    $("dbody").appendChild(p);
    /* `what moves` is beside the title already; a second copy in a column
       half this wide only wraps to five lines and unbalances the pair. */
    facts([["stage", (stage + 1) + " of " + S.length],
           ["time in this stage", ms(st.ms)],
           (stage === 0
             ? ["os8088 starts at", ms(D.meta.os0_ms)]
             : ["since os start", ms(st.t0 - D.meta.os0_ms) + " → "
                                  + ms(st.t0 + st.ms - D.meta.os0_ms)]),
           ["steps measured", String(st.steps.length)],
           ["loading bar", st.bar
              ? (st.bar.done + " / " + st.bar.total + "  ("
                 + (100 * st.bar.done / st.bar.total).toFixed(1) + "%)")
              : (stage >= S.length - 1 ? "finished, screen handed back"
                                       : "not up yet")],
           /* The bar counts THINGS, not pixels: the sectors of the operating
              system, then a fixed set of later steps. Saying so here is what
              stops "222" reading as a width. */
           ["the bar counts",
            D.ksecs + " sectors + " + D.cons.SPL_POST + " steps"]]);
    return;
  }
  var sp = st.steps[step];
  $("dtitle").textContent = sp.label;
  $("dbody").innerHTML = "";
  var p = document.createElement("p");
  p.innerHTML = "<span class='tag meas'>measured</span>" + (sp.note || "");
  $("dbody").appendChild(p);
  if (sp.mem && sp.mem.length){
    var names = sp.mem.map(function(id){
      var r = st.regions.filter(function(x){ return x.id === id; })[0];
      return r ? r.label : id;
    });
    var q = document.createElement("p");
    q.className = "lit";
    q.innerHTML = "<b>Lit up on the map:</b> " + esc(names.join(" · "));
    $("dbody").appendChild(q);
  }
  var rows = [["time", ms(sp.ms)],
              (stage === 0
                ? ["os8088 starts at", ms(D.meta.os0_ms)]
                : ["since os start", ms(sp.t0 - D.meta.os0_ms)]),
              ["share of stage", (100 * sp.ms / Math.max(st.ms, 1e-9)).toFixed(1) + "%"]];
  if (sp.sectors) rows.push(["sectors moved", String(sp.sectors)]);
  if (sp.reads) rows.push(["disk requests", String(sp.reads)]);
  if (sp.cyl) rows.push(["cylinders stepped", String(sp.cyl)]);
  if (sp.mech) rows.push(["of which waiting for the disk", ms(sp.mech)]);
  facts(rows);
}
function esc(s){ var d = document.createElement("div"); d.textContent = s; return d.innerHTML; }

/* -------------------------------------------------------------------------- */
function drawSplash(){
  var st = S[stage], box = $("splash");
  /* The stage's own reading is the one it OPENED with; picking a step on the
     timeline moves the screen to where the bar stood when that step finished. */
  var b = (step >= 0 && st.steps[step]) ? st.steps[step].bar : st.bar;
  var on = !!b;
  /* The panel is the MACHINE'S SCREEN all the way down: a bare cursor while
     nothing of ours can draw, then the loading screen, then the desktop it
     was all for. */
  var done = stage >= S.length - 1 && !!D.meta.desktop;
  if (done && !$("sdesk").src)
    $("sdesk").src = "data:image/png;base64," + D.meta.desktop;
  box.classList.toggle("done", done);
  box.classList.toggle("off", !on);
  if (on){
    var pct = b.pct !== undefined ? b.pct : 100 * b.done / Math.max(1, b.total);
    $("sfill").style.width = pct.toFixed(2) + "%";
    $("spct").textContent = Math.round(pct) + "%";
    $("smsg").textContent = st.bar ? st.bar.msg : (b.msg || "Loading Kernel");
  } else {
    $("sfill").style.width = "0%";
  }
  /* The desktop wins: by the last stage the bar has reached 222 of 222 and
     been handed back, so a caption about it would be describing a screen that
     is no longer on the machine. */
  $("scab").textContent = done
    ? "The desktop, read out of the display card's own memory at the end of "
      + "this boot."
    : (on
        ? "The bar " + (step >= 0 ? "after this step" : "as this stage opens")
          + " \u2014 " + b.done + " of " + b.total
          + ", read out of the running machine rather than worked out here."
        : "What a 5150 shows until os8088 takes the screen \u2014 a cursor, "
          + "and nothing more.");
}
function drawStages(){
  var s = $("stages"); 
  if (!s.dataset.built){
    S.forEach(function(st, i){
      /* TWO GROUPS, AND THE SPLIT IS THE POINT. On this machine the firmware's
         own memory test is most of the time between the switch and a desktop,
         and leaving it as rung one of fourteen buries nine seconds of
         operating system inside a minute of something else. */
      if (i === 0 || i === 1){
        var g = document.createElement("div");
        g.className = "grp";
        var a = document.createElement("span");
        a.textContent = i ? "os8088" : "The machine's firmware";
        var c = document.createElement("span");
        c.textContent = ms(i ? D.meta.os_ms : D.meta.os0_ms);
        g.appendChild(a); g.appendChild(c);
        s.insertBefore(g, $("navbtns"));
      }
      var b = document.createElement("button");
      b.className = "st"; b.type = "button";
      var n = document.createElement("span"); n.className = "n"; n.textContent = String(i+1);
      var t = document.createElement("span"); t.textContent = st.short || st.id;
      var m = document.createElement("span"); m.className = "ms"; m.textContent = ms(st.ms);
      b.appendChild(n); b.appendChild(t); b.appendChild(m);
      b.addEventListener("click", function(){ setStage(i); });
      s.insertBefore(b, $("navbtns"));
    });
    s.dataset.built = "1";
  }
  Array.prototype.forEach.call(s.querySelectorAll(".st"), function(b, i){
    b.setAttribute("aria-current", i === stage ? "true" : "false");
  });
  /* The arrows walk STEPS across stage boundaries, so they run out only at
     the two ends of the whole boot - not at the ends of a stage. */
  $("prev").disabled = stage === 0 && step < 0;
  $("next").disabled = stage === S.length - 1
                       && step >= S[stage].steps.length - 1;
  $("stitle").textContent = (stage + 1) + ". " + S[stage].title;
  $("smovedt").innerHTML = "What moves: <b>" + esc(S[stage].moved) + "</b>";
}
function setStage(i){
  if (i < 0 || i >= S.length) return;
  stage = i; step = -1; pick = null;
  drawStages(); drawSplash(); drawMap(); drawTime(); drawDetail();
}
function setStep(i){
  step = (step === i ? -1 : i); pick = null;
  drawStages();
  /* THE SCREEN IS PART OF THE SELECTION. It was redrawn on a stage change and
     not on a step one, so the bar held whatever the stage opened with however
     far along the timeline you clicked. */
  drawMap(); drawTime(); drawDetail(); drawSplash();
}
/* Clicking a block is a selection like any other, so clicking it again puts
   the stage back the way you found it. */
function setRegion(id){
  pick = (pick === id ? null : id); step = -1;
  drawStages();
  drawMap(); drawTime(); drawDetail(); drawSplash();
}
function clearSel(){ if (step >= 0 || pick){ step = -1; pick = null;
  drawStages(); drawMap(); drawTime(); drawDetail(); drawSplash(); } }
/* --------------------------------------------------------------------------
   ONE AXIS THROUGH THE WHOLE BOOT. Left and right walk STEPS, and run off the
   end of a stage into the next one rather than stopping there; up and down
   stay on the ladder and move a whole rung at a time. So the arrows alone
   will take a reader from the firmware reading one sector to the first
   desktop frame, through all 99 measured steps, without them having to know
   the page has two levels.

   A stage's own overview is part of the run rather than something the walk
   skips past: position 0 in a stage is the stage itself and 1..n are its
   steps, so arriving at a stage shows what it moves before its first step
   opens. Going back off the front of a stage lands on the LAST step of the
   one before, which is the same rule read the other way.
   -------------------------------------------------------------------------- */
function seek(d){
  var p = step + 1 + d, to = stage;
  if (p < 0){
    if (stage === 0) return;
    to = stage - 1; p = S[to].steps.length;
  } else if (p > S[stage].steps.length){
    if (stage === S.length - 1) return;
    to = stage + 1; p = 0;
  }
  stage = to; step = p - 1; pick = null;
  drawStages(); drawSplash(); drawMap(); drawTime(); drawDetail();
}
$("prev").addEventListener("click", function(){ seek(-1); });
$("next").addEventListener("click", function(){ seek(1); });
document.addEventListener("keydown", function(e){
  if (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName)) return;
  if (e.key === "ArrowRight"){ seek(1); e.preventDefault(); }
  else if (e.key === "ArrowLeft"){ seek(-1); e.preventDefault(); }
  else if (e.key === "ArrowDown"){ setStage(stage + 1); e.preventDefault(); }
  else if (e.key === "ArrowUp"){ setStage(stage - 1); e.preventDefault(); }
  else if (e.key === "Escape"){ clearSel(); }
  else if (e.key === "Home"){ setStage(0); e.preventDefault(); }
  else if (e.key === "End"){ setStage(S.length - 1); e.preventDefault(); }
});
/* One shared popup rather than one per term: the page marks a few dozen and
   they repeat. It is positioned rather than CSS-only so it cannot be clipped
   by the panel it sits in. */
(function(){
  var tip = document.createElement("div");
  tip.id = "gtip"; document.body.appendChild(tip);
  function show(e){
    var k = e.target && e.target.dataset && e.target.dataset.g;
    if (!k || !D.gloss[k]) return;
    tip.innerHTML = "";
    var h = document.createElement("b"); h.textContent = k;
    tip.appendChild(h);
    tip.appendChild(document.createTextNode(D.gloss[k]));
    tip.classList.add("on");
    var r = e.target.getBoundingClientRect();
    tip.style.left = "0px"; tip.style.top = "0px";
    var w = tip.offsetWidth, hh = tip.offsetHeight;
    var x = Math.min(Math.max(8, r.left + r.width / 2 - w / 2),
                     window.innerWidth - w - 8);
    var y = r.top - hh - 8;
    if (y < 8) y = r.bottom + 8;
    tip.style.left = x + "px"; tip.style.top = y + "px";
  }
  function hide(){ tip.classList.remove("on"); }
  document.addEventListener("mouseover", function(e){
    if (e.target.classList && e.target.classList.contains("gl")) show(e);
  });
  document.addEventListener("mouseout", function(e){
    if (e.target.classList && e.target.classList.contains("gl")) hide();
  });
  document.addEventListener("focusin", function(e){
    if (e.target.classList && e.target.classList.contains("gl")) show(e);
  });
  document.addEventListener("focusout", hide);
  window.addEventListener("scroll", hide, true);
})();

var rt;
window.addEventListener("resize", function(){
  clearTimeout(rt);
  rt = setTimeout(function(){ reserve(); setStage(stage); }, 110);
});
setStage(0);
reserve();
setStage(stage);
if (document.fonts && document.fonts.ready){
  document.fonts.ready.then(function(){ reserve(); setStage(stage); });
}
})();
'''


def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


# IBM's own typeface, on a page about an IBM 5150 - and the ONE thing here
# that is not in the file. Every rule names a real fallback stack, so a
# machine with no network renders the page correctly in system faces; nothing
# but the lettering depends on it.
FONTS = ('<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?'
         'family=IBM+Plex+Mono:wght@400;500&'
         'family=IBM+Plex+Sans:wght@400;500;600&'
         'family=IBM+Plex+Sans+Condensed:wght@600;700&display=swap">')


def render(p, fragment=False):
    """The whole page as one string.

    `fragment` drops <!doctype>, <html>, <head> and <body> for a host that
    supplies its own skeleton. Everything else is identical, so there is one
    page and not two.
    """
    m, lad, cons, vol = p["meta"], p["ladder"], p["cons"], p["vol"]
    title = "os8088 Boot Ladder"
    rom = ("the machine's original 1982 firmware"
           if m["field"] else
           "an open replacement firmware, because the original is not "
           "redistributable \u2014 it boots faster than any real one did, so "
           "the firmware's own share of the time here is not a field figure")
    prov = " \u00b7 ".join([
        "Measured on a cycle-accurate 4.77\u202fMHz 8088 running %s" % rom,
        "%d\u202fKB of memory" % m["ram_kb"],
        "a 360\u202fKB floppy",
        "build <b>%s</b>" % esc(m["commit"] or m["kernel_md5"][:8]),
        esc(m["taken"][:10]),
    ])

    legend = "".join(
        "<span><i style='background:var(--c-%s)'></i>%s</span>" % (k, v)
        for k, v in (("bios", "firmware"), ("ours", "boot sector / disk"),
                     ("kern", "operating system"), ("ovl", "start-up code"),
                     ("data", "disk index"), ("claim", "handed out"),
                     ("free", "free")))

    body = """
<div class="wrap">
 <div class="board">
 <aside class="rail">
   <h1>os8088 <span class="sub">Boot Ladder</span></h1>
   <p class="lede">What an IBM PC does between the power switch and a usable
    desktop \u2014 %(nst)d <b>discrete moves of memory</b>, on a 4.77&nbsp;MHz
    8088 reading a 360&nbsp;KB floppy.
    <span class="kbd">&larr;</span><span class="kbd">&rarr;</span> walk it a
    step at a time, end to end;
    <span class="kbd">&uarr;</span><span class="kbd">&darr;</span> move a whole
    rung. Underlined words define on hover.</p>
   <div class="splash-outer">
    <div class="splash off" id="splash">
     <div class="curs"></div>
     <img class="desk" id="sdesk" alt="The os8088 desktop as this boot left it">
     <div class="logo" title="The loading screen's own logo, turning once every 3.5 seconds. It reads 8808 halfway round because the real one is a coin flip of the whole word: at 180 degrees the first 8 is on the right."><span>8088</span></div>
     <div class="dlg"><div class="dlg2">
       <div class="cap">%(welcome)s</div>
       <div class="trough"><div class="fill" id="sfill"></div></div>
       <div class="pct" id="spct">0%%</div>
       <div class="msg" id="smsg"></div>
     </div></div>
    </div>
    <div class="cabin" id="scab"></div>
   </div>
   <div class="stages" id="stages"><span class="nav" id="navbtns">
     <button id="prev" type="button" title="back one step (left arrow) — into the rung before, at its end">&larr;</button>
     <button id="next" type="button" title="on one step (right arrow) — into the next rung when this one runs out">&rarr;</button>
   </span></div>
 </aside>
 <main class="work">
 <div class="shead"><div class="stitle" id="stitle"></div>
  <div class="smoved" id="smovedt"></div></div>

 <div class="panel">
  <p class="ph"><span>Memory \u2014 all 640&nbsp;KB of it, drawn to scale</span>
   <span class="hint">labels rise from the blocks big enough to see at this
   scale; the rest are named on the magnified strip below \u00b7 the hatched
   band is code lying <i>across</i> the map rather than a region of it</span></p>
  <div class="mlab" id="mlab"></div>
  <div class="mbar" id="mbar"></div>
  <div class="movl" id="movl"></div>
  <div class="mrule" id="mrule"></div>
  <div class="zdim" id="zdim"></div>
  <svg class="zlink" id="zlink" aria-hidden="true"></svg>
  <div class="zrow" id="zrow"></div>
  <div class="zcap" id="zcap"></div>
  <div class="legend">%(legend)s</div>
 </div>

 <div class="panel">
  <p class="ph"><span>What this stage spends its time on</span>
   <span class="hint">widths are proportional, with a small constant added to
   every segment so that a hundredth of a millisecond is still something you
   can click \u2014 read the figure, not the width</span></p>
  <div class="tbar" id="tbar"></div>
  <div class="tlab" id="tlab"></div>
 </div>

 <div class="panel detail">
  <div><h3 id="dtitle"></h3><div id="dbody"></div></div>
  <div class="facts" id="dfacts"></div>
 </div>
 </main>
 </div>

 <footer>
  <p><b>Every figure here was measured, not estimated.</b> The boot was run on
  a cycle-accurate emulation of a 4.77&nbsp;MHz 8088 with the floppy drive's
  mechanics modelled \u2014 the head stepping, the disk turning \u2014 and the
  milliseconds are that machine's own cycle count. The loading bar is not a
  reconstruction of the arithmetic either: its two numbers were read out of the
  running system at each point on this page, and so was every figure about the
  memory pool. The addresses are the ones this build actually places.</p>
  <p><b>It describes one boot, of one build.</b> A different floppy, a
  different display card or a second drive would move several of these numbers
  \u2014 the drive probe especially, which is nearly two seconds on a machine
  with an empty second bay. The page is generated from a measured run rather
  than maintained by hand, so the build and the date above are the ones it
  speaks for.</p>
  <p class="stamp">%(prov)s</p>
  <p class="mono" style="opacity:.75">%(sums)s</p>
 </footer>
</div>
<script>window.LADDER = %(data)s;</script>
<script>%(js)s</script>
""" % {
        "nst": len(p["stages"]),
        "prov": prov, "legend": legend,
        "welcome": esc(p["strings"]["welcome"]),
        "data": json.dumps(p, separators=(",", ":")),
        "js": JS,
        "sums": esc(
            "The operating system is one file of %s bytes \u2014 %d sectors, "
            "fetched in %d passes of the drive; the first %d of those sectors "
            "are the loader and the loading screen. It settles into %s bytes "
            "of memory, and %s of that is start-up code sitting in space that "
            "is about to be reused for something else. Switch-on to desktop: "
            "%.1f seconds, of which %.1f is the firmware's own self-test "
            "before any of this begins."
            % ("{:,}".format(vol["kernel"]["size"]),
               vol["kernel"]["sectors"],
               sum(1 for st in p["stages"] for x in st["steps"]
                   if x.get("sectors")),
               cons["BOOT2_SECS"], "{:,}".format(lad["ksize"]),
               "{:,}".format(lad["ovlw"] + lad["ovl"]),
               m["total_ms"] / 1000.0,
               p["stages"][0]["ms"] / 1000.0)),
    }
    if fragment:
        return ("<title>%s</title>\n%s\n<style>%s</style>\n%s"
                % (title, FONTS, CSS, body))
    return ("<!doctype html>\n<html lang=\"en\"><head>"
            "<meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,"
            "initial-scale=1\">"
            "<title>%s</title>%s\n<style>%s</style></head><body>%s</body></html>"
            % (title, FONTS, CSS, body))


# -----------------------------------------------------------------------------
# 6. --selfcheck: DOES THIS STILL DESCRIBE THE TREE?
# -----------------------------------------------------------------------------

def selfcheck(image, defines, build="build"):
    """Every assumption this tool makes, tried against the tree, out loud.

    Run this FIRST, always. It needs no emulator and takes about a second, so
    there is no reason not to - and it is the difference between finding out
    that the page is wrong and publishing a page that is wrong.
    """
    ok, bad = [], []

    def try_(what, fn):
        try:
            v = fn()
            ok.append((what, v if isinstance(v, str) else "ok"))
        except SystemExit as e:
            bad.append((what, str(e).splitlines()[0]))
        except Exception as e:                       # noqa: BLE001
            bad.append((what, "%s: %s" % (type(e).__name__, e)))

    try_("constants scraped from source",
         lambda: "%d found" % len(constants()))
    try_("the ladder, out of tools/kernsize.py",
         lambda: "KERNEL %04X  COLD %04X  FAT %04X  LOW %04X  VGABUF %04X  "
                 "HEAP %04X" % tuple(ladder(build, defines)[k] for k in
                                     ("kseg", "cold_seg", "fat_seg", "low_seg",
                                      "vgabuf_seg", "kend")))
    try_("the image's BPB and KERNEL.SYS",
         lambda: "%d sectors at LBA %d, %d SPT x %d heads"
                 % (volume(image)["kernel"]["sectors"],
                    volume(image)["kernel"]["lba"], volume(image)["spt"],
                    volume(image)["heads"]))
    try_("the loading screen's own strings",
         lambda: " / ".join(strings().values()))
    try_("heap claim tags in kernel/memory.inc",
         lambda: "%d tags" % len(owner_tags()))

    try_("the seven symbols the walk reads out of the guest",
         lambda: "  ".join("%s=%05X" % (k, v) for k, v in
                           sorted(probe_symbols(defines).items())))

    def cover():
        unused = check_coverage(defines, build)
        out = "every kmain call is in a stage"
        if unused:
            out += ("; gated out of this build (defined under kernel/, not "
                    "called by this kmain): " + ", ".join(unused))
        return out
    try_("kmain's calls, against the STAGES table", cover)

    def phases_have_notes():
        import os88boot
        live = [n for _, n, _ in os88boot.collapse(os88boot.callsites(defines, build))]
        gap = [n for n in live if n not in NOTES]
        if gap:
            raise SystemExit("os88ladder: no note written for: %s"
                             % ", ".join(sorted(set(gap))))
        return "%d phases, all with a note" % len(set(live))
    try_("a note for every phase the page will show", phases_have_notes)

    def titled():
        import os88boot
        live = [n for _, n, _ in os88boot.collapse(os88boot.callsites(defines, build))]
        gap = [n for n in live if n not in TITLES]
        if gap:
            raise SystemExit("os88ladder: no plain-words title for: %s"
                             % ", ".join(sorted(set(gap))))
        return "%d phases, all with a title a reader can use" % len(set(live))
    try_("a title for every phase the page will show", titled)

    def plain():
        """The two rules the page's prose is written to, checked.

        A glossary that needs a glossary explains nothing, and a reader who
        has none of this project's documents cannot follow a citation of it -
        so neither is left to good intentions.
        """
        leaky = [k for k, v in GLOSS.items() if TERM.search(v)]
        if leaky:
            raise SystemExit(
                "os88ladder: these definitions lean on another marked term, "
                "which a reader cannot follow: " + ", ".join(sorted(leaky)))
        cite = re.compile(r"SPEC\.md|PERFORMANCE\.md|docs/|\u00a7|"
                          r"\b\w+\.(?:inc|asm)\b|\bcomment\b", re.I)
        prose = dict(NOTES)
        prose.update(dict(("title:" + k, v) for k, v in TITLES.items()))
        prose.update(dict(("gloss:" + k, v) for k, v in GLOSS.items()))
        for st in STAGES:
            prose["stage:" + st["id"]] = st["title"] + " / " + st["moved"]
        bad = sorted(k for k, v in prose.items() if cite.search(v))
        if bad:
            raise SystemExit(
                "os88ladder: page text cites something the reader does not "
                "have: " + ", ".join(bad))
        return ("%d strings, no citations, %d definitions that stand alone"
                % (len(prose), len(GLOSS)))
    try_("the page's prose, against its own two rules", plain)

    def states():
        """THREE STATES PER STAGE, and only ever one of them at a time.

        The page's whole readability rests on the panel at the bottom, the
        highlight on the map and the screen at the left describing the SAME
        thing. Three selectors keep that true by clearing each other, and one
        of them going quietly out of step is not something a screenshot shows -
        it looks like a panel that did not update.
        """
        need = [
            ("stage = i; step = -1; pick = null;",
             "setStage clears both selections"),
            ("step = (step === i ? -1 : i); pick = null;",
             "picking a step clears the block"),
            ("pick = (pick === id ? null : id); step = -1;",
             "picking a block clears the step"),
            ("if (pick){", "the panel answers to a picked block first"),
            ('if (e.key === "ArrowRight"){ seek(1);',
             "right walks a step, not a rung"),
            ('else if (e.key === "ArrowDown"){ setStage(stage + 1);',
             "down still walks a rung"),
            ("to = stage + 1; p = 0;",
             "a step off the end of a stage enters the next one"),
            ("to = stage - 1; p = S[to].steps.length;",
             "and off the front, the end of the one before"),
            ("var soft = step < 0 && !pick;",
             "a picked block is filled in, not outlined"),
        ]
        gap = [why for frag, why in need if frag not in JS]
        if gap:
            raise SystemExit("os88ladder: the page's three states have come "
                             "apart - " + "; ".join(gap))
        # every surface a reader can click on the map has to reach the same
        # selector, or one of them silently does nothing.
        hits = JS.count("pickRegion(r.id)")
        if hits < 3:
            raise SystemExit("os88ladder: only %d of the map's click surfaces "
                             "select a block; the full map, its labels and the "
                             "magnified strip all must" % hits)
        return "start / timeline / memory, %d click surfaces into the last" % hits
    try_("the three states a stage can be in", states)

    def hosts():
        """Every host a block can be drawn on can also HIDE one.

        A block that leaves the map is faded out by a class, and the rule that
        does it lists the hosts by name. One host missing from that list is a
        block that goes on being drawn over stages that destroyed it - which
        is what happened to the hatched band on the full map, and looked like
        a modelling mistake rather than a stylesheet with three of four
        selectors in it.
        """
        want = [".mbar .rg", ".movl .ov", ".zwin .rg", ".zovl .ov"]
        rule = [ln for ln in CSS.splitlines() if ".gone{" in ln]
        gap = [w for w in want if not any(w + ".gone" in ln for ln in rule)]
        if gap:
            raise SystemExit("os88ladder: a block on %s can never be hidden - "
                             "no `.gone` rule for it" % ", ".join(gap))
        if len(rule) != 1:
            raise SystemExit("os88ladder: `.gone` is %d rules; it has to be one, "
                             "after every `.dim` and `.hot` above it, or it "
                             "loses to them on some hosts and not others"
                             % len(rule))
        return "%d hosts, one rule, after the rules it must beat" % len(want)
    try_("hiding a block, on every map it can appear on", hosts)

    try_("MartyPC, and a machine to run it on",
         lambda: ("IBM-ROM %s - field figures" % FIELD_MACHINE)
                 if machine_available(FIELD_MACHINE)
                 else (("GLaBIOS %s only - the IBM 5150 ROM is not in "
                        "tools/martypc/roms/, so the ROM's own time will not "
                        "be a field figure" % TWIN_MACHINE)
                       if machine_available(TWIN_MACHINE)
                       else _no_marty()))

    w = max(len(k) for k, _ in ok + bad) if (ok or bad) else 0
    for k, v in ok:
        sys.stdout.write("  ok   %-*s  %s\n" % (w, k, v))
    for k, v in bad:
        sys.stdout.write("  FAIL %-*s  %s\n" % (w, k, v))
    if bad:
        sys.stdout.write(
            "\nos88ladder: %d check(s) failed. THE PAGE IS OUT OF DATE WITH "
            "THE TREE.\nFix these before regenerating - each line above names "
            "the file to open.\n" % len(bad))
        return 1
    sys.stdout.write("\nos88ladder: the model still describes this tree.\n")
    return 0


def _no_marty():
    raise SystemExit("os88ladder: no MartyPC to measure with - `make marty` "
                     "(needs cargo, libudev-dev and pkg-config)")


# -----------------------------------------------------------------------------
# 7. The command line.
# -----------------------------------------------------------------------------

USAGE = """os88ladder - the interactive Boot Ladder page (ON DEMAND, never in `make`)

  python3 tools/os88ladder.py [options]

  --selfcheck        does the model still describe this tree? RUN THIS FIRST.
                     No emulator, about a second, and it names the file to open
                     for anything that has moved.
  -o, --out PATH     where to write (default build/bootladder.html)
  --fragment         emit without <html>/<head>/<body>, for a host that has its
                     own skeleton (the Artifact wrapper)
  --image PATH       the floppy to boot (default build/os8088-360.img)
  --machine NAME     a MartyPC machine (default: the IBM-ROM 5150, falling back
                     to its GLaBIOS twin when the ROM is not in the tree)
  --define SYM       an extra -D for the kernel this describes (repeatable).
                     KERN_BIG unless one of KERN_BIG/KERN_SMALL is given
  --build DIR        where the built kernel is (default build, or $OS88_BUILD).
                     kern_small: --build build/smallk --define KERN_SMALL
                     --image build/small360.img, after `make small`
  --json PATH        where to keep the model (default: beside the page). It
                     carries the WALK it was built from, so `--measure` on it
                     re-renders in seconds without booting anything
  --measure PATH     re-use a walk taken earlier instead of booting again
  --no-measure       refuse to boot anything; only legal with --measure
"""


def main(argv):
    image = os.path.join(ROOT, "build", "os8088-360.img")
    out = os.path.join(ROOT, "build", "bootladder.html")
    machine, defines, build = None, [], None
    jsonout, reuse, frag, check, nomeas = None, None, False, False, False
    it = iter(argv)
    for a in it:
        if a == "--selfcheck":
            check = True
        elif a in ("-o", "--out"):
            out = next(it)
        elif a == "--fragment":
            frag = True
        elif a == "--image":
            image = next(it)
        elif a == "--machine":
            machine = next(it)
        elif a == "--define":
            defines.append(next(it))
        elif a == "--build":
            build = next(it)
        elif a == "--json":
            jsonout = next(it)
        elif a == "--measure":
            reuse = next(it)
        elif a == "--no-measure":
            nomeas = True
        else:
            raise SystemExit(USAGE)
    # KERN_BIG is the DEFAULT, not a seed: seeded, `--define KERN_SMALL` on
    # top of it assembled a kernel that was both, and every row failed.
    if not any(d.split("=")[0] in ("KERN_BIG", "KERN_SMALL") for d in defines):
        defines.append("KERN_BIG")
    defines = tuple(defines)
    # ONE build directory, told to everything that reads it. os88sym checks
    # its map against $OS88_BUILD/kernel.bin, os88boot its listing against
    # <build>/kernel-full.bin and kernsize measures <build>/kernel.bin - so
    # --build and the environment are reconciled here, before any of them is
    # imported, and cannot name different kernels.
    build = build or os.environ.get("OS88_BUILD") or "build"
    os.environ["OS88_BUILD"] = build

    if check:
        return selfcheck(image, defines, build)

    # THE SELF-CHECK RUNS ANYWAY, because a page generated off a stale model is
    # worse than no page: it looks exactly like a good one.
    sys.stderr.write("os88ladder: checking the model against the tree...\n")
    if selfcheck(image, defines, build):
        sys.stderr.write("os88ladder: refusing to generate a page from a model "
                         "the tree has moved out from under.\n")
        return 1

    lad, cons, vol, strs = ladder(build, defines), constants(), volume(image), strings()
    if reuse:
        w = json.load(open(reuse))
        if "events" not in w and isinstance(w.get("walk"), dict):
            w = w["walk"]                   # a MODEL: take the walk out of it
        if "events" not in w or "machine" not in w:
            raise SystemExit(
                "os88ladder: %s is not a walk and has no walk in it.\n"
                "  --measure takes what a previous run MEASURED - either the "
                "file --json wrote, or a bare walk.\n"
                "  If this is neither, drop --measure and let it boot." % reuse)
        sys.stderr.write("os88ladder: re-using the walk in %s (%s, %s)\n"
                         % (reuse, w["machine"], w["taken"]))
    elif nomeas:
        raise SystemExit("os88ladder: --no-measure needs --measure PATH - "
                         "there is nothing to draw a timeline from")
    else:
        if machine is None:
            machine = (FIELD_MACHINE if machine_available(FIELD_MACHINE)
                       else TWIN_MACHINE)
            if machine == TWIN_MACHINE:
                sys.stderr.write(
                    "os88ladder: the IBM 5150 ROM is not in tools/martypc/"
                    "roms/, so this measures the GLaBIOS twin. The page will "
                    "say so.\n")
        sys.stderr.write("os88ladder: booting %s on %s - this is minutes, "
                         "not seconds\n" % (os.path.basename(image), machine))
        t0 = time.time()
        w = walk(image, machine, defines, lad, cons, build=build)
        sys.stderr.write("os88ladder: walked %d phases in %.0f s of host time\n"
                         % (len(w["events"]), time.time() - t0))

    d = os.path.dirname(os.path.abspath(out))
    if d and not os.path.isdir(d):
        os.makedirs(d)
    jpath = jsonout or (os.path.splitext(out)[0] + ".json")
    if not reuse:
        # THE WALK IS KEPT THE MOMENT IT EXISTS, before the model is built
        # from it. A refusal in build_page() is a second's fix and a re-run
        # of `--measure` on this file; without this line it was also the
        # loss of the minutes the walk took, which is how a scrape that had
        # gone stale cost every page three boots.
        json.dump(w, open(jpath, "w"), indent=1)

    p = build_page(w, lad, cons, vol, defines, strs, build=build)
    # The walk rides in the JSON so a re-render needs no boot; it does NOT ride
    # in the page, which would put 26KB of raw cycle counts into every reader's
    # download to say nothing the page does not already draw.
    html = render(dict((k, v) for k, v in p.items() if k != "walk"),
                  fragment=frag)
    open(out, "w", encoding="utf-8").write(html)
    json.dump(p, open(jpath, "w"), indent=1)
    sys.stderr.write(
        "os88ladder: %s  (%s, %d stages, %d measured phases)\n"
        % (out, "{:,}".format(len(html)), len(p["stages"]),
           sum(len(s["steps"]) for s in p["stages"])))
    # The one line worth reading back: the two clocks that should agree.
    below = w["total_ms"] - w["events"][0]["ms"]
    sys.stderr.write(
        "os88ladder: cross-check - the kernel's own boot_ticks is %d = %.0f ms "
        "against %.0f ms of measured phases below `post` (%+.0f ms, one tick "
        "is 54.9)\n" % (w["boot_ticks"], w["boot_ticks_ms"], below,
                        w["boot_ticks_ms"] - below))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
