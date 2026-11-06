#!/usr/bin/env python3
"""os88hot - where a user operation's time goes in the KERNEL, on MartyPC.

The kernel hot-paths pass's shared instrument (docs/plans/khp1/BRIEF.md 3).
Every figure is GUEST time off MartyPC's cycle counter, converted at
4.772727 MHz (209.5 ns a cycle), so host load cannot move it.

    python3 tools/os88hot.py scenarios
    python3 tools/os88hot.py time raise [--machine cga|herc|vga|NAME] [--all] [-n 5]
    python3 tools/os88hot.py profile menu --machine herc [--top 30] [--local]
    python3 tools/os88hot.py cycles font_run_cell type --machine vga
    python3 tools/os88hot.py gfxbench --machine cga --json cga.json
    python3 tools/os88hot.py sysbench --machine cga --json sys.json
    python3 tools/os88hot.py diff base.json tip.json
    python3 tools/os88hot.py ab --base a7ee7fab time raise --all
    python3 tools/os88hot.py ab --base a7ee7fab profile drag --machine cga

Every subcommand takes `--json FILE` (the result as data), `--machine`
(a short name - cga, herc, vga - or any MartyPC machine) and `--all` (the
three adapters, one after the other). It works on THIS TREE'S `build/`
(ROOT is derived from this file's path, so a worktree measures its own
kernel), or on `$OS88_BUILD`'s tree when that is set; `ab` sets it.

HOW A SCENARIO IS TIMED - the engine, `Run`, and why it is not settle:

  1. Setup is ordinary tools/os88ui.py verbs on a RUNNING machine (open a
     Disk window, put the pointer on a title bar). None of it is measured.
  2. The guest is run to IRQ0's entry and stopped there, so every window
     starts at the same phase of the 18.2 Hz tick (without that a 35 ms
     operation read +-7% rep to rep on an unchanged kernel: the 3.7 ms tick
     pass lands inside the window or it does not). Then the measurement is
     armed: MartyPC LOGGING
     breakpoints (a hit is stamped with its cycle and resumed inside the
     server, so the guest never stops and no round trip is spent) on the
     idle task's `hlt` (sch_idle_body), on all eight IRQ vectors' entry
     points as the IVT holds them, and on `gfx_unlock`.
  3. The operation's input - mouse packets, key presses - is injected and
     the guest STEPPED with `advance` in batches (one pipelined round trip
     carries dozens). Mouse edges are confirmed off the kernel's published
     pointer before the next packet goes, as os88mouse does, because the
     1200-baud UART drops a packet sent while one is in flight.
  4. The scenario's DONE predicate (by default os88marty.ui_idle: ui_task
     asleep, no event queued, no wake pending, the gfx lock free, the key
     ring empty - and its pass for this tick finished) must hold across a
     whole tick. It is read between batches, on the paused guest.
  5. The logged hits are then exact: the operation ENDS at the last
     `gfx_unlock` before DONE was first seen (the last drawing it did), and
     the CPU was HALTED from each idle `hlt` to the interrupt entry that
     followed it. So:

       ms     injection of the first input -> the end, in guest ms. This is
              the latency a user sees, including the serial flight of the
              mouse packets (~25 ms a packet at 1200 baud) and any disk time.
       busy   the cycles in that window the CPU was NOT halted, in ms - the
              work, which is what a speedup reduces. busy/ms near 100% means
              the CPU was the bottleneck.
       unl    gfx_unlock calls in the window (one per locked drawing pass).

  A sampling PROFILE is the same run with the batch size at ONE MartyPC
  batch (~1,193 cycles, 0.25 ms) and the CS:IP of every advance's end kept:
  a sample every quarter millisecond of guest time, uniform in guest time
  and DETERMINISTIC (the guest only moves when stepped), costing the guest
  nothing. A sample inside a logged halt is `(halted)` and is left out of
  the shares, which are shares of BUSY time. Kernel samples resolve by
  LINEAR address over every code section at its run-time segment (`.text`,
  `.vga` at KERNEL_SEG; `.cold` at COLD_SEG less the mono slide; `.ovl`;
  `.boot2` at [spl_fseg]), a local label rolls up into its routine unless
  --local, and anything else is bucketed: `pkg <title>` (a window's
  package image), `mod <FILE>` (an on-demand module's claim, from mod_tab),
  `BIOS`, `other`.

`cycles SYM` arms logging breakpoints on SYM's entry and on every RET,
RETF and IRET inside its span (the span runs to the next non-local label;
found by disassembling the guest's own bytes with ndisasm), and pairs them
as a stack, so recursion nests and only OUTERMOST activations are summed
into `total`. Time is INCLUSIVE of everything inside a call: callees, any
interrupt that lands, and any task switch a lock wait makes. Calls an
interrupt entry landed inside are counted (`irq`), and `clean` gives the
median of the calls no interrupt touched. A tail JMP out of the span is
reported, because a call leaving that way is timed to the callee's own RET
only if the callee is the routine being measured - otherwise it shows up as
an unmatched entry.

REPEATABILITY. With the alignment, reps of one scenario on one kernel
agree to about 1% (menu open on CGA: 36.46 / 35.80 / 36.46 / 35.80 ms busy)
- a short alternation, so compare MEDIANS over the same -n, which `ab`
does. `raise` and `launch` have warm-up reps (the first raise after the
other drive's window acted re-reads that disk; a relaunch of a closed Calc
reads nothing) and say so in their labels.

WHAT IT IS NOT. MartyPC is cycle-accurate for the CPU, the bus and the
video card, and this tree's build of it gives the FLOPPY a platter
(tools/martypc/patches/04: rotation, data rate, one sector at a time,
seeks; docs/MARTYPC-DEBUG.md *The disk has a platter*), which reproduces
the 5150's raw int 13h rows to the measurement quantum and boots in 0.92x
the field's ticks. So `desktop`, `opendisk` and the cold `launch` include
a disk term that is close to real; what it does NOT model is motor
spin-up (instant here) and anything below the BIOS a real NEC 765 might do
differently, and a disk figure still goes to the 5150 before PERFORMANCE.md
Part 9 quotes it. `fdc` counts the int 13h-level reads and sectors exactly.

ON 86BOX (--emu 86box [--cpu 286|386], an AT through
tools/os88box86api.py) NOTHING IS CYCLE-ACCURATE and every figure is
RELATIVE - an A/B on one machine and nothing else (docs/86BOX.md, *What it
is NOT*). There are no logging breakpoints and `advance` is a whole
emulated millisecond, so every 86Box run is sampled at 1 ms: `busy` is the
share of samples not at the idle hlt, the end is the first millisecond the
DONE state is seen at (1-2 ms resolution), `unl` is not available, and
`cycles` is MartyPC-only. Output names the machine `286/cga` and the JSON
carries `relative: true`.
"""
import argparse
import bisect
import json
import os
import random
import re
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time

# THIS TREE'S root, derived from this file's path - or, for `ab`'s base arm,
# the base worktree's, so that ITS os88sym/os88marty/os88ui are imported and
# its kernel map is checked against its own build/ (os88sym derives its ROOT
# from its own path, which is why the tools directory has to be the base's)
ROOT = os.environ.get("OS88HOT_ROOT") or \
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.chdir(ROOT)

HZ = 4772727.0                  # the 8088's clock, 14.31818 MHz / 3
TICK = 262144                   # IRQ0's period in cycles at the BIOS rate
MACHINES = {"cga": "os8088_5150_cga_gla",
            "herc": "os8088_5150_herc_gla",
            "vga": "os8088_xt_vga"}
ALL = ("cga", "herc", "vga")


def ms(cyc):
    return 1000.0 * cyc / HZ


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def bdir():
    """The build tree whose images and kernel are measured."""
    b = os.environ.get("OS88_BUILD", "")
    if not b:
        return os.path.join(ROOT, "build")
    return b if os.path.isabs(b) else os.path.join(ROOT, b)


def img(name):
    return os.path.join(bdir(), name)


# --emu 86box: the same three adapters on an AT (tools/os88box86api.py's
# names - no `os8088_` prefix, which is MartyPC's namespace). NOT
# cycle-accurate and stepped in 1 ms quanta: every figure is RELATIVE, for an
# A/B on one machine and nothing else (docs/86BOX.md, *What it is NOT*).
EMU = "marty"
CPU = "286"


def machine_name(m):
    if EMU == "86box":
        return {"cga": "at%s_cga", "herc": "at%s_herc",
                "vga": "at%s_vga"}.get(m, m).replace("%s", CPU) \
            if m in ("cga", "herc", "vga") else m
    return MACHINES.get(m, m)


# =============================================================================
# the kernel's map, where it RUNS
# =============================================================================
CODE_SECT = (".text", ".cold", ".vga", ".ovl", ".boot2")


class KMap(object):
    """[(linear, name)] for every kernel CODE label, placed where it runs on
    THIS machine now - so build it after the boot (the `.cold` slide and
    [spl_fseg] are read from the guest)."""

    def __init__(self, m, fseg=None):
        import os88sym
        off = os88sym.syms()
        sect = os88sym.sections()
        eq = os88sym.equates()
        self.slide = m.cold_slide()
        kseg = os88sym.KERNEL_SEG
        cold = eq.get("COLD_SEG")
        if fseg is None:
            fseg = u16(m.read(m.sym("spl_fseg"), 2)) if "spl_fseg" in off \
                else 0
        segs = {".text": kseg, ".bss": kseg, ".vga": kseg,
                ".cold": cold, ".ovl": eq.get("FAT_SEG"),
                ".boot2": fseg or None}
        rows = []
        for n, o in off.items():
            s = sect.get(n)
            if s not in CODE_SECT:
                continue
            if s == ".vga" and self.slide:
                continue                # `.cold` slid over it: not there
            seg = segs.get(s)
            if seg is None:
                continue
            lin = seg * 16 + o - (self.slide if s == ".cold" else 0)
            rows.append((lin & 0xFFFFF, n, s))
        rows.sort()
        self.rows = rows
        self.keys = [r[0] for r in rows]
        self.byname = {}
        for lin, n, s in rows:
            self.byname[n] = lin
        # the extent of each code section where it runs: an IP past the last
        # label of a section is not that section's
        self.extent = {}
        for lin, n, s in rows:
            lo, hi = self.extent.get(s, (lin, lin))
            self.extent[s] = (min(lo, lin), max(hi, lin))

    def find(self, ip):
        """(name, section) of the label at or before `ip`, or None."""
        i = bisect.bisect_right(self.keys, ip) - 1
        if i < 0:
            return None
        lin, n, s = self.rows[i]
        lo, hi = self.extent[s]
        if ip - lin > 0x1000 or ip > hi + 0x400:
            return None
        return n, s

    def span(self, name):
        """(start, end) linear of routine `name`: to the next label that is
        not one of its locals."""
        if name not in self.byname:
            raise SystemExit("os88hot: no kernel code label %r (on this "
                             "machine; a `.vga` label is absent on a mono "
                             "card, where `.cold` slid over it)" % name)
        a = self.byname[name]
        i = bisect.bisect_right(self.keys, a)
        while i < len(self.rows):
            lin, n, s = self.rows[i]
            if lin > a and not n.startswith(name + "."):
                return a, lin
            i += 1
        return a, a + 0x200


def routine(n):
    return n.split(".")[0]


class Buckets(object):
    """Attribute a linear IP to a kernel routine, a package, a module."""

    def __init__(self, m, kmap, local=False):
        import os88geom as geom
        self.k = kmap
        self.local = local
        self.pk = []
        try:
            for w in geom.windows(m):
                raw = m.read(m.sym("wm_wins") + w.i * geom.WIN_SIZE
                             + geom.W_SEG, 2)
                seg = u16(raw)
                if seg and seg != geom.KERNEL_SEG:
                    hdr = m.readseg(seg, 0, 16)
                    if hdr[0:2] == b"O8":
                        size = u16(hdr, 8) or 0x10000
                        t = re.sub(r"\s+\d+$", "", w.title) or "?"
                        self.pk.append((seg * 16, seg * 16 + size, t))
        except Exception:                                   # noqa: BLE001
            pass
        # ON-DEMAND MODULES (SPEC.md 2.8): a mod_tab row's claim, matched to
        # its `.mod*` section by its CODE BYTES against kernel-full.bin (the
        # sections are vstart=0, so a symbol's offset is its offset in the
        # claim), and resolved by routine like the kernel. A row no section
        # matches is bucketed whole, by file.
        self.mods = []          # (lo, hi, file, [(off, name)])
        try:
            a, b = m.sym("mod_tab"), m.sym("mod_tab_end")
            raw = m.read(a, b - a)
            groups = _mod_groups()
            for r in range(0, len(raw) - 3, 4):
                seg, fp = u16(raw, r), u16(raw, r + 2)
                if not seg:
                    continue
                nm = bytes(m.readseg(0x60, fp, 13)).split(b"\0")[0] \
                    .decode("latin-1")
                live = bytes(m.read(seg * 16 + 0x20, 0x40))
                best, tab, size = 0, [], 0x10000
                for g in groups:
                    hit = sum(1 for x, y in zip(live, g["probe"]) if x == y)
                    if hit > best:
                        best, tab, size = hit, g["syms"], g["size"]
                if best < 0x30:
                    tab, size = [], 0x10000
                self.mods.append((seg * 16, seg * 16 + size, nm, tab))
            self.mods.sort()
        except Exception:                                   # noqa: BLE001
            pass
        # LOADED DRIVERS (SPEC.md 51): drv_tab rows, each image at its own
        # segment for DRVR_PARA paragraphs, resolved through the driver's
        # own map (tools/os88drvsym.py, the Makefile's own nasm line)
        self.drv = []
        try:
            import os88sym
            n = os88sym.equates().get("DRV_MAX", 0)
            base = m.sym("drv_tab")
            raw = m.read(base, n * 16)
            for r in range(n):
                seg = u16(raw, r * 16 + 2)
                para = u16(raw, r * 16 + 8)
                fp = u16(raw, r * 16 + 4)
                if not seg:
                    continue
                nm = bytes(m.readseg(0x60, fp, 13)).split(b"\0")[0] \
                    .decode("latin-1")
                self.drv.append((seg * 16, seg * 16 + (para or 0x1000) * 16,
                                 nm, _drv_syms(nm)))
        except Exception:                                   # noqa: BLE001
            pass

    def _sub(self, kind, lo, nm, tab, ip):
        if not tab:
            return "%s %s" % (kind, nm)
        off = ip - lo
        i = bisect.bisect_right(tab, (off, "\xff")) - 1
        if i < 0:
            return "%s %s" % (kind, nm)
        n = tab[i][1]
        return "%s:%s" % (nm.split(".")[0].lower(),
                          n if self.local else routine(n))

    def name(self, ip):
        for lo, hi, t in self.pk:
            if lo <= ip < hi:
                return "pkg " + t
        if ip >= 0xF0000:
            return "BIOS"
        f = self.k.find(ip)
        if f is not None:
            return f[0] if self.local else routine(f[0])
        for lo, hi, nm, tab in self.mods:
            if lo <= ip < hi:
                return self._sub("mod", lo, nm, tab, ip)
        for lo, hi, nm, tab in self.drv:
            if lo <= ip < hi:
                return self._sub("drv", lo, nm, tab, ip)
        return "other %05X" % (ip & 0xFFF00)


_MODG = None


def _mod_groups():
    """[{probe, syms, size}] per on-demand module: its sections out of
    build/kernel.map's segment table (a `.modX` and any section that follows
    it at a nonzero vstart is one module), the bytes at +0x20 out of
    kernel-full.bin, and its labels out of os88sym."""
    global _MODG
    if _MODG is not None:
        return _MODG
    import os88sym
    mp = os.path.join(bdir(), "kernel.map")
    full = open(os.path.join(bdir(), "kernel-full.bin"), "rb").read()
    secs = []
    for line in open(mp):
        f = line.split()
        if len(f) == 6 and f[4] == "progbits" and f[5].startswith(".mod") \
                and f[5] != ".modmap":
            secs.append((int(f[1], 16), int(f[0], 16), int(f[3], 16), f[5]))
        if line.startswith("-- Symbols"):
            break
    off, sect = os88sym.syms(), os88sym.sections()
    groups = []
    for start, vstart, length, name in secs:
        if vstart and groups:           # a follows= half of the one before
            g = groups[-1]
            g["names"].append(name)
            g["size"] = max(g["size"], vstart + length)
            continue
        groups.append(dict(names=[name], size=length,
                           probe=full[start + 0x20:start + 0x60]))
    for g in groups:
        g["syms"] = sorted((off[n], n) for n in off
                           if sect.get(n) in g["names"])
    _MODG = groups
    return groups


_DRVS = {}


def _drv_syms(fname):
    """[(offset, label)] for a loaded driver, or [] when its map cannot be
    had (a knob arm, a build tree without its .bin)."""
    key = fname.upper()
    if key not in _DRVS:
        tab = []
        try:
            import os88drvsym
            d = os88drvsym.symbols(fname.split(".")[0].lower() + ".bin")
            tab = sorted((v, k) for k, v in d.items())
        except BaseException:                               # noqa: BLE001
            tab = []
        _DRVS[key] = tab
    return _DRVS[key]


# =============================================================================
# the engine
# =============================================================================
class Act(object):
    """One input. kind: 'mouse' (dx, dy, l, r) confirmed off the published
    pointer, 'key' (name) spaced by `gap` guest seconds, 'wait' (secs)."""

    def __init__(self, kind, **kw):
        self.kind = kind
        self.kw = kw


def mouse(dx=0, dy=0, l=False):
    return Act("mouse", dx=dx, dy=dy, l=l)


def key(name, gap=0.09):
    return Act("key", name=name, gap=gap)


def wait(secs):
    return Act("wait", secs=secs)


class Run(object):
    """Arm, inject, step, read back. One measured window."""

    def __init__(self, ui, extra_bps=(), sample=False, step=None):
        self.ui = ui
        self.m = ui.m
        self.sample = sample
        # steps: one MartyPC batch (~1,193 cycles) per advance for a profile,
        # coarser when only the logged hits matter
        self.step = step or (1 if sample else 12000)
        self.per = 48 if sample else 6
        self.extra = list(extra_bps)
        self.samples = []           # (end cycle, flat ip, cycles weight)
        m = self.m
        self.hlt = m.sym("sch_idle_body.rearm") - 1
        if m.read(self.hlt, 1) != b"\xF4":
            raise SystemExit("os88hot: sch_idle_body.rearm - 1 is not a hlt "
                             "on this kernel; the idle accounting needs "
                             "updating")
        ivt = m.read(0, 0x40)
        self.isr = sorted(set(
            u16(ivt, n * 4 + 2) * 16 + u16(ivt, n * 4) for n in range(8, 16)))
        self.unl = m.sym("gfx_unlock")
        self._mo = ui.mo
        # 86BOX: no logging breakpoints and no batch, and `advance` is a
        # whole emulated millisecond. So it is always SAMPLED - the IP after
        # every 1 ms step - and both halts and the end come off the samples:
        # a sample at the idle hlt is halted, and the end is the first step
        # the DONE state is seen at (1-2 ms resolution)
        self.box = bool(getattr(m, "box86", False))
        if self.box:
            self.sample = True
            self.step = int(HZ / 1000) + 1
            self.per = 2

    def _bps(self):
        if self.box:
            return []
        a = [self.hlt, self.unl] + self.isr + self.extra
        seen, out = set(), []
        for x in a:
            if x not in seen:
                seen.add(x)
                out.append({"type": "exec", "addr": x, "log": True})
        return out

    def _adv(self, n):
        if self.box:
            c = None
            for _ in range(n):
                r = self.m.cmd(cmd="advance", cycles=self.step)
                c1 = int(r["cycles"])
                prev = self.samples[-1][0] if self.samples else c1 - self.step
                self.samples.append((c1, int(r["flat_ip"]), c1 - prev))
                c = c1
            return c
        cmds = [{"cmd": "advance", "cycles": self.step}] * n
        out = self.m.batch(cmds)
        for r in out:
            if self.sample:
                self.samples.append((int(r["cycles"]), int(r["flat_ip"]),
                                     int(r["advanced_cycles"])))
            if r.get("state") == "breakpoint":
                raise SystemExit("os88hot: a STOPPING breakpoint fired at "
                                 "%04X:%04X inside the window - something "
                                 "else armed one" % (r["cs"], r["ip"]))
        return int(out[-1]["cycles"])

    def where(self):
        return self._mo.where()

    def go(self, acts, done=None, hold=TICK + 20000, limit=60.0,
           min_secs=0.0, end=None):
        """Inject `acts` in order and step until `done` holds across `hold`
        cycles. Returns the result dict. The machine is left RUNNING."""
        m = self.m
        done = done or default_done
        self.end = m.sym(end) if end else None
        if self.end is not None and self.end not in self.extra:
            self.extra.append(self.end)
        m.pause()
        m.breakpoints(self._bps())
        st = m.status()
        c0 = int(st["cycles"])
        t_start = None
        cyc = c0
        lim = c0 + int(limit * HZ)
        fdc0 = _fdc(m)
        for a in acts:
            if a.kind == "wait":
                tgt = cyc + int(a.kw["secs"] * HZ)
                while cyc < tgt:
                    cyc = self._adv(self.per)
                continue
            if t_start is None:
                t_start = cyc
            if a.kind == "key":
                m.key(a.kw["name"])
                tgt = cyc + int(a.kw.get("gap", 0.09) * HZ)
                while cyc < tgt:
                    cyc = self._adv(self.per)
                continue
            # a mouse packet: send, then wait until the pointer says it took
            was = self.where()
            dx, dy, l = a.kw["dx"], a.kw["dy"], a.kw["l"]
            sent = cyc
            m.mouse(dx, dy, l=l)
            tries = 0
            while True:
                cyc = self._adv(max(1, self.per // 3))
                now = self.where()
                if (l and not (now[2] & 1)) or (not l and (now[2] & 1)):
                    took = False
                elif (dx or dy) and now[:2] == was[:2]:
                    took = False
                else:
                    took = True
                if took:
                    break
                if cyc - sent > int(0.25 * HZ):
                    tries += 1
                    if tries > 4:
                        raise SystemExit("os88hot: a mouse packet (%d,%d,%s) "
                                         "was never decoded" % (dx, dy, l))
                    m.mouse(dx, dy, l=l)
                    sent = cyc
                if cyc > lim:
                    raise SystemExit("os88hot: the input did not complete")
            # ...and a packet time between packets, so the UART is free
            tgt = sent + int(0.026 * HZ)
            while cyc < tgt:
                cyc = self._adv(max(1, self.per // 3))
        if t_start is None:
            t_start = c0
        t_min = t_start + int(min_secs * HZ)
        seen = None
        while True:
            cyc = self._adv(self.per)
            if cyc < t_min:
                continue
            ok = done(self)
            if ok:
                if seen is None:
                    seen = cyc
                elif cyc - seen >= hold:
                    break
            else:
                seen = None
            if cyc > lim:
                raise SystemExit("os88hot: the scenario never reached its "
                                 "DONE state inside %.0f guest s" % limit)
        lg = m.cmd(cmd="bplog") if not self.box else {"hits": []}
        m.breakpoints([])
        fdc1 = _fdc(m)
        m.run()
        if lg.get("dropped"):
            print("os88hot: WARNING the server log dropped %d hits"
                  % lg["dropped"])
        hits = [(int(a), int(c)) for a, c in lg["hits"]]
        return self._account(hits, c0, t_start, seen, cyc, fdc0, fdc1,
                             min_secs)

    def _account(self, hits, c0, t0, t_done, c_last, fdc0, fdc1, min_secs):
        if self.box:
            return self._account_box(c0, t0, t_done, fdc0, fdc1)
        isr = set(self.isr)
        # halted intervals: hlt hit -> the next logged hit (which can only be
        # an interrupt entry, the CPU executing nothing in between). The
        # window opened on a paused guest that may have been halted already.
        halts = []
        open_at = None
        if self._was_halted:
            open_at = c0
        for a, c in hits:
            if open_at is not None and c >= open_at:
                halts.append((open_at, c))
                open_at = None
            if a == self.hlt:
                open_at = c
        if open_at is not None:
            halts.append((open_at, c_last))
        unl = [c for a, c in hits if a == self.unl and t0 <= c <= t_done]
        endh = [c for a, c in hits if a == self.end and c >= t0] \
            if self.end is not None else []
        if endh:
            t_end = endh[0]             # the scenario's own END label
            unl = [c for c in unl if c <= t_end]
        elif min_secs:
            t_end = t_done
        else:
            t_end = unl[-1] if unl else None
            if t_end is None:
                hl = [s for s, e in halts if s <= t_done]
                t_end = hl[-1] if hl else t_done
        win = max(1, t_end - t0)
        halted = 0
        for s, e in halts:
            lo, hi = max(s, t0), min(e, t_end)
            if hi > lo:
                halted += hi - lo
        self.halts = halts
        res = dict(ms=ms(win), busy_ms=ms(win - halted),
                   busy_pct=100.0 * (win - halted) / win,
                   unlocks=len(unl),
                   irqs=sum(1 for a, c in hits
                            if a in isr and t0 <= c <= t_end),
                   t0=t0, t_end=t_end, t_done=t_done, cycles=win,
                   fdc=_fdcdelta(fdc0, fdc1))
        self.hits = hits
        return res

    def _account_box(self, c0, t0, t_done, fdc0, fdc1):
        self.halts = []
        self.hits = []
        t_end = t_done
        win = max(1, t_end - t0)
        smp = [x for x in self.samples if t0 < x[0] <= t_end]
        tot = sum(w for c, ip, w in smp) or 1
        hal = sum(w for c, ip, w in smp if ip in (self.hlt, self.hlt + 1))
        return dict(ms=ms(win), busy_ms=ms(win) * (tot - hal) / tot,
                    busy_pct=100.0 * (tot - hal) / tot, unlocks=None,
                    irqs=None, t0=t0, t_end=t_end, t_done=t_done, cycles=win,
                    fdc=_fdcdelta(fdc0, fdc1), relative=True)

    def start(self):
        """Note whether the paused guest is halted right now (the window's
        first interval). Called by go() after the pause."""
        st = self.m.status()
        self._was_halted = int(st["flat_ip"]) in (self.hlt, self.hlt + 1)

    def halted_at(self, c, ip=None):
        if self.box:
            return ip in (self.hlt, self.hlt + 1)
        i = bisect.bisect_right(self._hstarts, c) - 1
        return i >= 0 and self.halts[i][0] <= c < self.halts[i][1]

    def index_halts(self):
        self._hstarts = [s for s, e in self.halts]


def _fdc(m):
    try:
        return m.disk()
    except Exception:                                       # noqa: BLE001
        return None


def _fdcdelta(a, b):
    if not a or not b:
        return None
    return {k: b.get(k, 0) - a.get(k, 0)
            for k in ("reads", "read_sectors", "writes", "write_sectors",
                      "seeks")}


def default_done(run):
    import os88marty
    m = run.m
    if not os88marty.ui_idle(m):
        return False
    return os88marty._ui_lastt(m) == os88marty._ktick(m)


# Run.go's start-of-window halt test lives in start(); wrap go so callers
# cannot forget it
_go = Run.go


def _go_wrapped(self, acts, **kw):
    self.align()
    self.start()
    return _go(self, acts, **kw)


def _align(self):
    """Start every window at the SAME PHASE of the tick: run to IRQ0's entry
    and stop there. Without it a 35 ms operation reads +-10% rep to rep on an
    unchanged kernel - whether the 3.7 ms tick pass lands inside the window
    or not (measured, base against tip with identical code: +6.8% / -6.6%)
    - and an A/B cannot see a 5% change through that. A change that makes
    the operation faster still moves where LATER ticks land, which is real."""
    import os88marty
    m = self.m
    ivt = m.read(0x20, 4)
    irq0 = u16(ivt, 2) * 16 + u16(ivt, 0)
    m.breakpoints([{"type": "exec", "addr": irq0}])
    mark = m.go()
    if m.wait_stop(20.0, since=mark) != "breakpoint":
        raise SystemExit("os88hot: IRQ0 never arrived to align on")


Run.align = _align


Run.go = _go_wrapped


# =============================================================================
# scenarios
# =============================================================================
class Scenario(object):
    """prepare(ui) once; then per rep: pre(ui) [unmeasured], the measured
    `acts` with `done`, post(ui) [unmeasured]. `phases` names several
    measured windows in one rep (menu: open, close)."""
    name = "?"
    what = "?"
    disk = False
    reps = 3
    warm = 1                    # unmeasured reps first: the first raise after
                                # another drive's window acted re-reads its
                                # disk (4 int 13h calls, measured), which is
                                # real but is not what a rep is asking about

    def prepare(self, ui):
        pass

    def phases(self, ui):
        """[(label, acts, done, kwargs)] for one rep, computed after pre."""
        raise NotImplementedError

    def pre(self, ui):
        pass

    def post(self, ui):
        pass


def _dbl(x, y, ui):
    ui.mo.to(x, y)
    return [mouse(l=True), mouse(l=False), mouse(l=True), mouse(l=False)]


def _front_disk(ui, letter):
    """A Disk window on `letter` at its root, opened if need be."""
    return ui.open_drive(letter)


class SOpenDisk(Scenario):
    name = "opendisk"
    what = "double-click drive A:'s desktop zone to its Disk window (disk)"
    disk = True

    def pre(self, ui):
        ui.clear_desktop()

    def phases(self, ui):
        import os88geom as geom
        x, y = geom.drive_pt(ui.m, "A")
        acts = _dbl(x, y, ui)

        def done(run):
            if not default_done(run):
                return False
            f = ui.front()
            return f is not None and ui.fs_of(f) == (0, 0, 1)
        return [("open A:", acts, done, {})]

    def post(self, ui):
        ui.clear_desktop()


class SRaise(Scenario):
    name = "raise"
    warm = 2                    # one each way: both windows' disks warm
    what = "click the title bar of a background Disk window (A: under B:)"

    def prepare(self, ui):
        ui.clear_desktop()
        self.a = ui.open_drive("A")
        self.b = ui.open_drive("B")
        ui.still()

    def phases(self, ui):
        f = ui.front()
        back = [w for w in ui.windows() if w.visible and w.i != f.i
                and ui.fs_of(w) is not None][0]
        (x, y), how = ui._raise_point(back)
        ui.mo.to(x, y)

        def done(run):
            g = ui.front()
            return g is not None and g.i == back.i and default_done(run)
        return [("raise %s" % back.title,
                 [mouse(l=True), mouse(l=False)], done, {})]


class SDrag(Scenario):
    name = "drag"
    what = "drag the front Disk window 64 px right and 24 down, in 8 packets"

    def prepare(self, ui):
        ui.clear_desktop()
        self.w = ui.open_drive("A")
        ui.still()
        self.home = (self.w.x, self.w.y)

    def pre(self, ui):
        w = ui._refresh(self.w)
        if (w.x, w.y) != self.home:
            ui.move_window(w, *self.home)
        self.w = ui._refresh(w)

    def phases(self, ui):
        w = self.w
        (x, y), how = ui._raise_point(w)
        ui.mo.to(x, y)
        acts = [mouse(l=True)] + [mouse(8, 3, l=True)] * 8 + [mouse(l=False)]

        def done(run):
            if not default_done(run):
                return False
            g = ui._refresh(w)
            return (g.x, g.y) != self.home
        return [("drag", acts, done, {})]

    def post(self, ui):
        self.pre(ui)


class SMenu(Scenario):
    name = "menu"
    what = "press the desktop's File menu title (open), release on the bar (close)"

    def pre(self, ui):
        ui.clear_desktop()

    def phases(self, ui):
        cells = ui.menus()
        title, x0, x1, items = cells[1] if len(cells) > 1 else cells[0]
        import os88geom as geom
        x, y = (x0 + x1) // 2, geom.MBAR_H // 2
        ui.mo.to(x, y)

        def dropped(run):
            return ui._word("menu_y1") and ui._byte("menu_dropd")

        def closed(run):
            return not ui._byte("menu_dropd") and default_done(run)
        # a menu that is DOWN keeps ui_task in menu_track's yield loop WITH
        # THE GFX LOCK HELD (the highlights are VRAM-direct), so neither
        # ui_idle nor the last unlock can end the open: menu_drop sets
        # [menu_dropd] the moment the pull-down is drawn and falls into
        # `.poll`, whose first execution is the end
        return [("open %s" % title, [mouse(l=True)], dropped,
                 {"hold": TICK // 2, "end": "menu_drop.poll"}),
                ("close %s" % title, [mouse(l=False)], closed, {})]


class SPointer(Scenario):
    name = "pointer"
    what = "sweep the pointer across the desktop, 24 packets of (+20,+5)"

    def pre(self, ui):
        ui.clear_desktop()
        ui.mo.to(40, 40)

    def phases(self, ui):
        return [("sweep", [mouse(20, 5)] * 24, None, {})]


class SIdle(Scenario):
    name = "idle"
    what = "5 guest seconds of an idle desktop, a Disk window open"
    reps = 1
    warm = 0
    SECS = 5.0

    def prepare(self, ui):
        ui.clear_desktop()
        ui.open_drive("A")
        ui.still()

    def phases(self, ui):
        return [("idle %gs" % self.SECS, [], lambda r: True,
                 {"min_secs": self.SECS, "hold": 0})]


class SType(Scenario):
    name = "type"
    what = "type a 31-key line into Note Pad (A:\\APPS), a key every 90 ms"
    TEXT = "The quick brown fox jumps over."
    reps = 2

    def prepare(self, ui):
        ui.clear_desktop()
        ui.open_drive("A")
        ui.open("APPS")
        self.w = ui.open("NOTEPAD.O88")
        ui.still()

    def pre(self, ui):
        ui.raise_window(ui._refresh(self.w))
        ui.still()

    def phases(self, ui):
        acts = []
        for ch in self.TEXT:
            if ch == " ":
                acts.append(key("Space"))
            elif ch == ".":
                acts.append(key("Period"))
            elif ch.isupper():
                acts.append(key("Key" + ch))     # lower case: no shift
            else:
                acts.append(key("Key" + ch.upper()))
        return [("type %d keys" % len(acts), acts, None, {})]

    def post(self, ui):
        for _ in self.TEXT:
            ui.m.key("Backspace")
            time.sleep(0.01)
        ui.still()


class SLaunch(Scenario):
    name = "launch"
    what = ("double-click B:\\APPS\\CALC.O88 to its first frame: rep 1 "
            "COLD (read off the disk), later reps CACHED (no int 13h)")
    disk = True
    warm = 0                    # the first launch is the cold one, and a
    reps_seen = 0               # relaunch reads nothing (measured: 0 calls)

    def prepare(self, ui):
        ui.clear_desktop()
        ui.open_drive("B")
        self.d = ui.open("APPS")
        ui.still()

    def pre(self, ui):
        for w in ui.windows():
            if w.title.upper().startswith("CALC"):
                ui.close(w)
        ui.raise_window(ui._refresh(self.d))
        ui.still()

    def phases(self, ui):
        d = ui._refresh(self.d)
        i, ty = ui.entry("CALC.O88", d)
        row = ui.scroll_to(i, win=d)
        x, y = ui.row_xy(d, row)
        acts = _dbl(x, y, ui)

        def done(run):
            if not default_done(run):
                return False
            return any(w.title.upper().startswith("CALC") and w.visible
                       for w in ui.windows())
        self.reps_seen += 1
        return [("launch Calc %s" % ("cold" if self.reps_seen == 1
                                     else "cached"),
                 acts, done, {"limit": 120.0})]

    def post(self, ui):
        self.pre(ui)


class SScroll(Scenario):
    name = "scroll"
    what = "PgDn then PgUp in a Disk window on B:\\APPS (12 entries)"

    def prepare(self, ui):
        ui.clear_desktop()
        ui.open_drive("B")
        self.d = ui.open("APPS")
        ui.still()

    def pre(self, ui):
        ui.raise_window(ui._refresh(self.d))
        ui.still()

    def phases(self, ui):
        d = ui._refresh(self.d)
        s0 = ui.scroll(d)

        def moved(run):
            return ui.scroll(d) != s0 and default_done(run)

        def back(run):
            return ui.scroll(d) == s0 and default_done(run)
        return [("PgDn", [key("PageDown", 0.0)], moved, {"limit": 20.0}),
                ("PgUp", [key("PageUp", 0.0)], back, {"limit": 20.0})]


SCEN = [SOpenDisk(), SRaise(), SDrag(), SMenu(), SPointer(), SType(),
        SLaunch(), SScroll(), SIdle()]
BYNAME = dict((s.name, s) for s in SCEN)
BYNAME_ALL = ["desktop"] + [s.name for s in SCEN]


# =============================================================================
# booting
# =============================================================================
def boot(mach, quick=True):
    import os88ui
    return os88ui.boot(img("os8088-360.img"), apps=img("apps360.img"),
                       machine=machine_name(mach), quick=quick,
                       verbose=False, label="os88hot")


def run_desktop(mach, sample=False, local=False):
    """Power-on to the first idle desktop, stepped from cycle 0."""
    import os88marty
    import os88ui
    cm = os88marty.launch(img("os8088-360.img"), apps=img("apps360.img"),
                          machine=os88marty.machine(machine_name(mach)),
                          boot=False, label="os88hot")
    with cm as m:
        ui = os88ui.UI(m, verbose=False)
        samples = []
        c0 = int(m.status()["cycles"])
        stage = [None]

        def up():
            try:
                return (ui._word("desk_rows") and ui._word("menu_nbar")
                        and not ui._byte("spl_live")
                        and not ui._word("spl_fseg")
                        and not ui._byte("mem_cp_busy")
                        and default_done(type("R", (), {"m": m})))
            except Exception:                               # noqa: BLE001
                return False
        seen = None
        fseg = 0                # stage 2's blob is where [spl_fseg] said
        fsa = m.sym("spl_fseg")  # while it lived: kmain zeroes it at the end
        step = 1 if sample else 60000
        n = 48 if sample else 8
        while True:
            out = m.batch([{"cmd": "advance", "cycles": step}] * n)
            if sample:
                samples += [(int(r["cycles"]), int(r["flat_ip"]),
                             int(r.get("advanced_cycles", step)))
                            for r in out]
            cyc = int(out[-1]["cycles"])
            if sample:                  # the LAST value it held before
                v = u16(m.read(fsa, 2))  # kmain cleared it
                fseg = v or fseg
            if up():
                if seen is None:
                    seen = cyc
                elif cyc - seen > TICK:
                    break
            else:
                seen = None
            if cyc - c0 > 200 * HZ:
                raise SystemExit("os88hot: no desktop in 200 guest seconds")
        res = dict(ms=ms(seen - c0), busy_ms=None, fdc=_fdc(m))
        prof = None
        if sample:
            km = KMap(m, fseg=fseg or None)
            bk = Buckets(m, km, local)
            hl = m.sym("sch_idle_body.rearm")
            prof = _profile_table(samples, bk,
                                  lambda c, ip: ip in (hl - 1, hl))
        m.run()
        return res, prof


def _profile_table(samples, bk, halted):
    tab, tot, hal = {}, 0, 0
    for c, ip, w in samples:
        tot += w
        if halted(c, ip):
            hal += w
            continue
        k = bk.name(ip)
        tab[k] = tab.get(k, 0) + w
    return dict(rows=sorted(tab.items(), key=lambda x: -x[1]), total=tot,
                halted=hal)


# =============================================================================
# the subcommands
# =============================================================================
def scen_rows(mach, scen, n, sample=False, local=False, extra=(),
              flicker=False):
    """Boot once, run scenario `scen` n times. -> [(label, [results], prof)]"""
    s = BYNAME[scen]
    out = {}
    profs = {}
    with boot(mach) as ui:
        s.prepare(ui)
        km = KMap(ui.m) if (sample or extra) else None
        for rep in range(-s.warm, n):
            s.pre(ui)
            for label, acts, done, kw in s.phases(ui):
                if rep < 0:
                    Run(ui).go(acts, done=done, **kw)
                    continue
                r = Run(ui, extra_bps=extra(km) if callable(extra) else (),
                        sample=sample)
                res = r.go(acts, done=done, **kw)
                if sample:
                    r.index_halts()
                    bk = Buckets(ui.m, km, local)
                    t0, t1 = res["t0"], res["t_end"]
                    smp = [x for x in r.samples if t0 < x[0] <= t1]
                    p = _profile_table(smp, bk, r.halted_at)
                    acc = profs.setdefault(label, {})
                    for k, v in p["rows"]:
                        acc[k] = acc.get(k, 0) + v
                    acc["(halted)"] = acc.get("(halted)", 0) + p["halted"]
                if extra:
                    res["_hits"] = r.hits
                    res["_isr"] = r.isr
                out.setdefault(label, []).append(res)
                ui.m.run()
            s.post(ui)
    return out, profs


def summarise(rs):
    def med(k):
        v = [r[k] for r in rs if r.get(k) is not None]
        return statistics.median(v) if v else None

    def mn(k):
        v = [r[k] for r in rs if r.get(k) is not None]
        return min(v) if v else None
    return dict(n=len(rs), ms_min=mn("ms"), ms_med=med("ms"),
                busy_min=mn("busy_ms"), busy_med=med("busy_ms"),
                busy_pct=med("busy_pct"), unlocks=med("unlocks"),
                fdc=rs[0].get("fdc"))


def cmd_scenarios(a):
    print("desktop    power-on to the first idle desktop (disk; stepped "
          "from cycle 0)")
    for s in SCEN:
        print("%-10s %s" % (s.name, s.what))
    print("\nmachines: %s, or any MartyPC machine name; --all runs %s"
          % (", ".join("%s=%s" % kv for kv in MACHINES.items()),
             "/".join(ALL)))
    return {}


def machines(a):
    return list(ALL) if a.all else [a.machine]


def tag(mach):
    """How a machine is named in the output: `286/cga` on 86Box, where the
    figures are RELATIVE, so they cannot be read as MartyPC ones."""
    return mach if EMU == "marty" else "%s/%s" % (CPU, mach)


def cmd_time(a):
    res = {}
    for mach in machines(a):
        if a.scenario == "desktop":
            rows = [run_desktop(mach)[0] for _ in range(a.n)]
            out = {"boot": rows}
        else:
            out, _ = scen_rows(mach, a.scenario, a.n)
        res[mach] = {}
        for label, rs in out.items():
            sm = summarise(rs)
            res[mach][label] = dict(summary=sm, runs=rs)
            fdc = sm["fdc"] or {}
            print("%-5s %-8s %-22s ms %8.1f (min %8.1f)  busy %8s  %5s%%  "
                  "unl %4s  fdc %s"
                  % (tag(mach), a.scenario, label, sm["ms_med"], sm["ms_min"],
                     "%.1f" % sm["busy_med"] if sm["busy_med"] is not None
                     else "-",
                     "%.0f" % sm["busy_pct"] if sm["busy_pct"] is not None
                     else "-",
                     sm["unlocks"] if sm["unlocks"] is not None else "-",
                     "%d calls %d sec" % (fdc.get("reads", 0),
                                          fdc.get("read_sectors", 0))
                     if fdc else "-"))
    return res


def cmd_profile(a):
    res = {}
    for mach in machines(a):
        if a.scenario == "desktop":
            r, p = run_desktop(mach, sample=True, local=a.local)
            profs = {"boot": {k: v for k, v in p["rows"]}}
            profs["boot"]["(halted)"] = p["halted"]
            times = {"boot": r}
        else:
            out, profs = scen_rows(mach, a.scenario, a.n, sample=True,
                                   local=a.local)
            times = {k: summarise(v) for k, v in out.items()}
        res[mach] = {}
        for label, tab in profs.items():
            hal = tab.pop("(halted)", 0)
            busy = sum(tab.values())
            rows = sorted(tab.items(), key=lambda x: -x[1])
            tm = times[label]
            print("\n%s  %s / %s: %s ms, busy %s ms  (%d reps, %.0f%% of "
                  "the window halted)" % (
                      tag(mach), a.scenario, label,
                      "%.1f" % tm.get("ms_med", tm.get("ms")),
                      "%.1f" % tm["busy_med"] if tm.get("busy_med") else "-",
                      a.n if a.scenario != "desktop" else 1,
                      100.0 * hal / max(1, hal + busy)))
            print("    %busy  busy ms/rep  routine")
            nrep = a.n if a.scenario != "desktop" else 1
            for k, v in rows[:a.top]:
                print("  %6.2f%%  %10.2f   %s" % (100.0 * v / max(1, busy),
                                               ms(v) / nrep, k))
            res[mach][label] = dict(
                time=tm, busy_cycles=busy, halted_cycles=hal,
                rows=[dict(name=k, cycles=v, pct=100.0 * v / max(1, busy))
                      for k, v in rows])
    return res


def ret_addrs(m, lo, hi):
    """Linear addresses of every RET/RETF/IRET in [lo, hi), and the
    targets of JMPs that leave the span, by disassembling guest bytes."""
    code = m.read(lo, hi - lo)
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(code)
        path = f.name
    try:
        r = subprocess.run(["ndisasm", "-b16", "-o", str(lo & 0xFFFF), path],
                           capture_output=True, text=True, check=True)
    finally:
        os.unlink(path)
    rets, tails = [], []
    base = lo & 0xFFFF
    for line in r.stdout.splitlines():
        p = line.split(None, 2)
        if len(p) < 3:
            continue
        off = int(p[0], 16)
        ins = p[2].strip()
        op = ins.split()[0]
        if op in ("ret", "retf", "iret", "retn"):
            rets.append(lo + (off - base))
        if op == "jmp" and re.match(r"jmp (short |near )?0x[0-9a-f]+$", ins):
            t = int(ins.split()[-1], 16)
            if not (base <= t < base + (hi - lo)):
                tails.append(lo + (t - base))
    return rets, tails


def cmd_cycles(a):
    res = {}
    for mach in machines(a):
        info = {}

        def extra(km, info=info):
            lo, hi = km.span(a.symbol)
            # the span ends where the next routine starts, but .cold's slide
            # moves the routine: re-read through the map we built
            m = km._m
            rets, tails = ret_addrs(m, lo, hi)
            info.update(lo=lo, hi=hi, rets=rets, tails=tails)
            return [lo] + rets
        holder = {}

        def extra_wrap(km):
            km._m = holder["m"]
            return extra(km)
        s = BYNAME[a.scenario]
        out = {}
        with boot(mach) as ui:
            holder["m"] = ui.m
            s.prepare(ui)
            km = KMap(ui.m)
            for rep in range(a.n):
                s.pre(ui)
                for label, acts, done, kw in s.phases(ui):
                    r = Run(ui, extra_bps=extra_wrap(km))
                    rr = r.go(acts, done=done, **kw)
                    rr["_calls"] = pair_calls(r.hits, info["lo"],
                                              set(info["rets"]),
                                              set(r.isr), rr["t0"],
                                              rr["t_end"])
                    out.setdefault(label, []).append(rr)
                s.post(ui)
        res[mach] = {}
        names = ", ".join("%X" % t for t in info["tails"]) or "none"
        print("\n%s  %s over %s: span %05X-%05X, %d RET(s) armed, tail JMPs "
              "out: %s" % (mach, a.symbol, a.scenario, info["lo"], info["hi"],
                           len(info["rets"]), names))
        for label, rs in out.items():
            calls = [c for r in rs for c in r["_calls"]["calls"]]
            unm = sum(r["_calls"]["unmatched"] for r in rs)
            win = sum(r["cycles"] for r in rs)
            busy = sum(r["busy_ms"] for r in rs) * HZ / 1000.0
            tot = sum(c for c, irq, outer in calls if outer)
            if not calls:
                print("  %-22s no call of %s in %d reps" % (label, a.symbol,
                                                           len(rs)))
                res[mach][label] = dict(count=0)
                continue
            cs = sorted(c for c, irq, outer in calls)
            clean = sorted(c for c, irq, outer in calls if not irq)
            row = dict(count=len(calls) / len(rs), min=cs[0],
                       median=statistics.median(cs), max=cs[-1],
                       total=tot / len(rs),
                       clean_median=statistics.median(clean) if clean else None,
                       irq=sum(1 for c, irq, o in calls if irq),
                       unmatched=unm, pct_window=100.0 * tot / win,
                       pct_busy=100.0 * tot / max(1, busy))
            res[mach][label] = row
            print("  %-22s %6.1f calls/rep  cycles min %d  med %d  max %d  "
                  "(clean med %s, %d hit by an IRQ)  total %.2f ms/rep = "
                  "%.1f%% of the window, %.1f%% of busy%s"
                  % (label, row["count"], row["min"], row["median"],
                     row["max"], row["clean_median"], row["irq"],
                     ms(row["total"]), row["pct_window"], row["pct_busy"],
                     "  UNMATCHED %d" % unm if unm else ""))
    return res


def pair_calls(hits, entry, rets, isr, t0, t1):
    """[(cycles, irq_inside, outermost)] for each matched call."""
    # entries are taken inside the window; their RETs wherever they land in
    # the log, which runs on past the window's end to the DONE hold - the
    # window ends AT an entry when the routine is gfx_unlock itself
    stack, calls, unmatched = [], [], 0
    for a, c in hits:
        if c < t0:
            continue
        if a == entry:
            if c > t1:
                continue
            stack.append([c, False])
        elif a in rets and stack:
            s, irq = stack.pop()
            calls.append((c - s, irq, not stack))
        elif a in isr:
            for fr in stack:
                fr[1] = True
    unmatched = len(stack)
    return dict(calls=calls, unmatched=unmatched)


# --- gfxbench / sysbench ------------------------------------------------------
BENCH = {"gfxbench": ("GFXBENCH.O88", {"cga": "GFXCGA.TXT",
                                       "herc": "GFXHERC.TXT",
                                       "vga": "GFXVGA.TXT"}, 180.0),
         "sysbench": ("SYSBENCH.O88", None, 600.0)}


def cmd_bench(a):
    if not os.path.exists(img("bench360.img")):
        print("os88hot: building the bench disk (make bench)")
        subprocess.run(["make", "-j2", "BUILD=%s" % bdir(),
                        os.path.join(bdir(), "bench360.img")],
                       check=True, cwd=ROOT)
    import os88ui
    import os88marty
    import os88flush
    pkg, rep, budget = BENCH[a.cmd]
    res = {}
    for mach in machines(a):
        fname = rep[mach] if rep else "SYSBENCH.TXT"
        t = time.time()
        with os88ui.boot(img("os8088-360.img"), apps=img("bench360.img"),
                         machine=machine_name(mach), quick=True,
                         verbose=False, label="os88hot") as ui:
            ui.open_drive("B")
            w = ui.open(pkg)
            ui.still()
            c0 = int(ui.m.status()["cycles"])
            ui.menu_pick("Bench", "Run")
            # the machine is frozen while a bench runs, so never settle: wait
            # in GUEST time for the UI task to come back to sleep (the run is
            # over), then Save Report through the menu and read the FILE
            os88marty.guest_sleep(ui.m, 2.0)
            os88marty.until(ui.m, lambda mm: os88marty.ui_idle(mm),
                            "%s to finish its run" % a.cmd, poll=0.25,
                            guest=budget)
            ui.still()
            ui.menu_pick("Bench", "Save Report")
            os88marty.guest_sleep(ui.m, 2.0)
            os88marty.until(ui.m, lambda mm: os88marty.ui_idle(mm),
                            "the report to be written", poll=0.25, guest=60.0)
            ui.still()
            fl = os88flush.Flush(marty=ui.m)
            txt = fl.volume(1).read(fname).decode("latin-1")
            c1 = int(ui.m.status()["cycles"])
        if not txt:
            raise SystemExit("os88hot: %s wrote no %s on %s" % (a.cmd, fname,
                                                                mach))
        rows = parse_report(txt)
        res[mach] = dict(file=fname, rows=rows, text=txt,
                         guest_s=(c1 - c0) / HZ)
        print("%s %s: %d rows in %.0f guest s (%.0f host s)"
              % (mach, a.cmd, len(rows), (c1 - c0) / HZ, time.time() - t))
        for r in rows[:a.top]:
            print("   %-44s %s" % (r["name"][:44], "  ".join(r["vals"])))
    return res


NUM = re.compile(r"^-?\d+(\.\d+)?%?$")
UNIT = ("us", "ms", "KB", "s", "!", "t", "w", "cyc", "x")


def parse_report(txt):
    """Every line whose tail is numbers (after a unit and the t/!/w flags):
    {name, vals, flags}. The name carries the `-- section --` it is under,
    so a row that repeats across sections stays apart."""
    rows, sect = [], ""
    for line in txt.splitlines():
        s = line.rstrip()
        if not s.strip():
            continue
        if s.startswith("--"):
            sect = s.strip("- ").split(":")[0]
            continue
        toks = s.split()
        flags = []
        while toks and toks[-1] in UNIT:
            flags.insert(0, toks.pop())
        vals = []
        while len(toks) > 1 and NUM.match(toks[-1]):
            vals.insert(0, toks.pop())
        if vals and toks:
            rows.append(dict(name=(sect + " | " if sect else "")
                             + " ".join(toks), vals=vals,
                             flags=[f for f in flags if f in "!tw"]))
    return rows


def _num(v):
    try:
        return float(v.rstrip("%!"))
    except ValueError:
        return None


def cmd_diff(a):
    A, B = json.load(open(a.a)), json.load(open(a.b))
    # a COMBINED file (the baseline report's HOTPATH-BASE-*.json) holds one
    # result per `<subcommand>-<scenario>`: --key picks one out of either side
    key = getattr(a, "key", None)
    if key:
        A, B = A.get(key, A), B.get(key, B)
    for mach in sorted(set(A) & set(B)):
        ra = A[mach]
        rb = B[mach]
        if "text" in ra and "text" in rb:       # a bench report: re-parse,
            ra = dict(ra, rows=parse_report(ra["text"]))    # so a parser
            rb = dict(rb, rows=parse_report(rb["text"]))    # fix reaches
        if "rows" in ra and isinstance(ra["rows"], list) and ra["rows"] \
                and "vals" in ra["rows"][0]:
            da = dict((r["name"], r["vals"]) for r in ra["rows"])
            print("\n%s: %d rows / %d rows" % (mach, len(ra["rows"]),
                                               len(rb["rows"])))
            for r in rb["rows"]:
                va = da.get(r["name"])
                if not va:
                    continue
                cells = []
                for x, y in zip(va, r["vals"]):
                    fx, fy = _num(x), _num(y)
                    if fx is None or fy is None:
                        continue
                    pct = 100.0 * (fy - fx) / fx if fx else 0.0
                    cells.append("%s -> %s (%+.1f%%)" % (x, y, pct))
                if cells and any(x != y for x, y in zip(va, r["vals"])):
                    print("   %-36s %s" % (r["name"].split(" | ")[-1][:36],
                                         "  ".join(cells)))
            continue
        for label in ra:
            if label not in rb:
                continue
            x, y = ra[label], rb[label]
            _diffrow(mach, label, x, y)


def _diffrow(mach, label, x, y):
    def g(d, *ks):
        for k in ks:
            if isinstance(d, dict) and k in d:
                d = d[k]
            else:
                return None
        return d
    for key_ in (("summary", "ms_med"), ("summary", "busy_med"),
                 ("time", "ms_med"), ("time", "busy_med"), ("median",),
                 ("total",), ("count",)):
        vx, vy = g(x, *key_), g(y, *key_)
        if isinstance(vx, (int, float)) and isinstance(vy, (int, float)):
            pct = 100.0 * (vy - vx) / vx if vx else 0.0
            print("%-5s %-22s %-14s %10.2f -> %10.2f  %+6.1f%%"
                  % (mach, label, "/".join(key_), vx, vy, pct))
    if "rows" in x and "rows" in y:
        dx = dict((r["name"], r) for r in x["rows"])
        dy = dict((r["name"], r) for r in y["rows"])
        nrep = 1
        top = [r["name"] for r in x["rows"][:15]]
        for n in top + [r["name"] for r in y["rows"][:15] if r["name"] not in top]:
            cx = dx.get(n, {}).get("cycles", 0)
            cy = dy.get(n, {}).get("cycles", 0)
            print("      %-30s %9.2f -> %9.2f ms  (%+.2f)"
                  % (n[:30], ms(cx) / nrep, ms(cy) / nrep, ms(cy - cx) / nrep))


# --- ab -------------------------------------------------------------------------
def cmd_ab(a, rest):
    """Build --base in a private worktree+tree, run the same command there and
    here, and diff."""
    scratch = os.environ.get("OS88HOT_SCRATCH") or os.path.join(
        ROOT, "build", "os88hot-ab")
    os.makedirs(scratch, exist_ok=True)
    sha = subprocess.run(["git", "rev-parse", "--short", a.base], cwd=ROOT,
                         capture_output=True, text=True,
                         check=True).stdout.strip()
    wt = os.path.join(scratch, "wt-" + sha)
    if not os.path.isdir(wt):
        subprocess.run(["git", "worktree", "add", "--detach", wt, sha],
                       cwd=ROOT, check=True)
    tree = os.path.join(wt, "build")
    if not os.path.exists(os.path.join(tree, "os8088-360.img")) or \
            not os.path.exists(os.path.join(tree, "apps360.img")):
        os.makedirs(tree, exist_ok=True)
        for link in ("martypc", "86box"):
            src = os.path.join(ROOT, "build", link)
            dst = os.path.join(tree, link)
            if os.path.exists(src) and not os.path.lexists(dst):
                os.symlink(os.path.realpath(src), dst)
        print("os88hot ab: building %s in %s (once; reused after)" % (sha, wt))
        subprocess.run(["make", "-j2", "build/os8088-360.img",
                        "build/apps360.img"], cwd=wt, check=True)
    if rest and rest[0] in ("gfxbench", "sysbench") and not os.path.exists(
            os.path.join(tree, "bench360.img")):
        subprocess.run(["make", "-j2", "build/bench360.img"], cwd=wt,
                       check=True)
    outs = {}
    for arm, root in (("base", wt), ("tip", ROOT)):
        jf = os.path.join(scratch, "ab-%s-%s.json" % (arm, os.getpid()))
        # the BASE's own copy of this tool would not exist at an old commit,
        # so THIS file runs in both arms, pointed at each tree's build/ and
        # sources by its ROOT override
        env = dict(os.environ)
        env["OS88HOT_ROOT"] = root
        env.pop("OS88_BUILD", None)
        print("\n=== %s (%s) ===" % (arm, sha if arm == "base" else "tip"))
        subprocess.run([sys.executable, os.path.abspath(__file__)] + rest
                       + ["--json", jf], env=env, check=True)
        outs[arm] = jf
    print("\n=== delta, base %s -> tip ===" % sha)
    cmd_diff(argparse.Namespace(a=outs["base"], b=outs["tip"]))
    if a.json:
        json.dump(dict(base=json.load(open(outs["base"])),
                       tip=json.load(open(outs["tip"]))),
                  open(a.json, "w"), indent=1)


# =============================================================================
def main():
    argv = sys.argv[1:]
    if argv and argv[0] == "ab":
        p = argparse.ArgumentParser(prog="os88hot ab")
        p.add_argument("--base", required=True)
        p.add_argument("--json")
        a, rest = p.parse_known_args(argv[1:])
        return cmd_ab(a, rest)
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                formatter_class=argparse.RawTextHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(q):
        q.add_argument("--machine", default="cga")
        q.add_argument("--emu", choices=("marty", "86box"), default="marty",
                       help="86box: an AT (RELATIVE figures only)")
        q.add_argument("--cpu", choices=("286", "386"), default="286",
                       help="with --emu 86box: at286_* or at386_*")
        q.add_argument("--all", action="store_true")
        q.add_argument("--json")
        return q
    common(sub.add_parser("scenarios"))
    q = common(sub.add_parser("time"))
    q.add_argument("scenario", choices=BYNAME_ALL)
    q.add_argument("-n", type=int, default=3)
    q = common(sub.add_parser("profile"))
    q.add_argument("scenario", choices=BYNAME_ALL)
    q.add_argument("-n", type=int, default=2)
    q.add_argument("--top", type=int, default=25)
    q.add_argument("--local", action="store_true",
                   help="keep .local labels apart instead of rolling up")
    q = common(sub.add_parser("cycles"))
    q.add_argument("symbol")
    q.add_argument("scenario", choices=list(BYNAME))
    q.add_argument("-n", type=int, default=3)
    for b in ("gfxbench", "sysbench"):
        q = common(sub.add_parser(b))
        q.add_argument("--top", type=int, default=0)
    q = sub.add_parser("diff")
    q.add_argument("a")
    q.add_argument("b")
    q.add_argument("--key", help="one result out of a combined baseline "
                   "file, e.g. time-raise or gfxbench")
    a = p.parse_args(argv)
    global EMU, CPU
    EMU, CPU = getattr(a, "emu", "marty"), getattr(a, "cpu", "286")
    if EMU == "86box":
        os.environ["OS88_EMU"] = "86box"
    fn = {"scenarios": cmd_scenarios, "time": cmd_time,
          "profile": cmd_profile, "cycles": cmd_cycles,
          "gfxbench": cmd_bench, "sysbench": cmd_bench,
          "diff": cmd_diff}[a.cmd]
    res = fn(a)
    if getattr(a, "json", None) and res is not None:
        def clean(o):
            if isinstance(o, dict):
                return {k: clean(v) for k, v in o.items()
                        if not str(k).startswith("_")}
            if isinstance(o, list):
                return [clean(v) for v in o]
            return o
        json.dump(clean(res), open(a.json, "w"), indent=1)
        print("os88hot: wrote %s" % a.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
