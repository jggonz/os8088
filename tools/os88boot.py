#!/usr/bin/env python3
"""os88boot - where a boot actually goes, measured on the 4.77MHz 8088.

    python3 tools/os88boot.py                         # the 360KB image, CGA 5150
    python3 tools/os88boot.py --json build/boot.json  # ...and keep the numbers
    python3 tools/os88boot.py --build build/smallk --define KERN_SMALL \
                              --image build/small360.img   # kern_small's boot

**This is a stopwatch, not a sampler.** `tools/os88prof.py` samples `flat_ip`
to find where a package spends its time; a boot is a straight line of named
phases that each run ONCE, so the right instrument is a breakpoint on the
return address of every `call` in `kmain` and the guest's own cycle counter
either side of it. That costs the guest nothing and is exact rather than
statistical - two runs agree to the cycle.

**Cycles are the answer, not wall clock.** MartyPC is cycle-accurate on an
8088 at 4.772727 MHz and models the drive's mechanics (tools/martypc/patches
/04-floppy-disk-timing.patch), so `cycles / 4772727` is SECONDS ON THE FIELD
MACHINE - and the host it ran on does not enter into it. PERFORMANCE.md Part 9
Set 37 is where that model was checked against the real 5150.

The FDC counters (`cmd: disk`, SPEC.md 18.94's block from outside the guest)
are read at every boundary too, so each phase carries the sectors, the seeks
and the modelled mechanical milliseconds it spent - which is what separates a
phase that is slow from a phase that is merely waiting for a floppy.

Milestones, in order:

  * `post`        - the machine's own ROM, from reset to the boot sector
  * `bootsector`  - 0000:7C00 to the far jump into the kernel: SPEC.md 18.93's
                    multi-sector load of KERNEL.SYS, with the splash on top
  * one row per `call` in `kmain`, named for what it calls
  * `paint`       - the last of them, ending where SPEC.md 15.4's boot timer
                    stops: `wm_paint_all` done and the frame on the glass

The kernel's own `boot_ticks` is read at the end and printed beside the total,
which is the cross-check that the milestones did not miss anything: the two
are the same measurement taken by different clocks and disagree by less than
a tick when the walk is complete.
"""

import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty                                    # noqa: E402
import os88sym                                      # noqa: E402

HZ = 4772727.0                                      # the 5150's dot clock / 3
KERNEL_SEG = 0x0060


def callsites(defines=("KERN_BIG",), build="build"):
    """[(return address, what it called)] for every `call` in `kmain`.

    The RETURN address and not the call's own, because that is the address
    reached exactly once, after the phase has finished - a breakpoint on the
    callee would fire on every other caller of it too, and `gfx_lock`,
    `spl_step` and `vid_init` all have them.

    `.text` is the one section whose listing addresses need no fixing up
    (os88sym.py's header has the story for the other four), and `kmain` is in
    it - so the listing is read directly here, and asserted byte-identical to
    the kernel this tree just built so it cannot describe a different one.

    `build` is WHICH kernel that is: `make small` and every knob build assemble
    into a directory of their own, and a listing of kern_small compared
    against build/kernel-full.bin is a refusal about the wrong kernel.
    """
    src = os.path.join(ROOT, "kernel", "kernel.asm")
    built = os.path.join(ROOT, build, "kernel-full.bin")
    with tempfile.TemporaryDirectory() as tmp:
        lst = os.path.join(tmp, "k.lst")
        binf = os.path.join(tmp, "k.bin")
        cmd = ["nasm", "-f", "bin", "-w+error",
               "-I", os.path.join(ROOT, "kernel") + os.sep,
               "-I", os.path.join(ROOT, "apps") + os.sep,
               "-I", os.path.join(ROOT, build) + os.sep]
        # The generated includes a sub-make did not remake are in build/
        # (the Makefile's ICODIR, os88sym's $OS88_ICODIR): searched AFTER the
        # named build, so its own copy wins wherever it has one.
        if os.path.normpath(os.path.join(ROOT, build)) != \
                os.path.normpath(os.path.join(ROOT, "build")):
            cmd += ["-I", os.path.join(ROOT, "build") + os.sep]
        for d in defines:
            cmd += ["-D" + d]
        cmd += ["-l", lst, "-o", binf, src]
        subprocess.run(cmd, check=True, cwd=ROOT)
        if open(binf, "rb").read() != open(built, "rb").read():
            raise SystemExit(
                "os88boot: the listing is a DIFFERENT kernel from %s - "
                "`make` and try again (or name the build: --build and "
                "--define together, e.g. --build build/smallk --define "
                "KERN_SMALL)" % os.path.relpath(built, ROOT))
        lines = open(lst, errors="replace").read().splitlines()

    # Every line that emitted a byte, in address order, with its source text.
    #
    # MOST OF kmain IS MACROS, and a nasm listing prints the macro's BODY
    # rather than the argument it was given: `OVWCALL clk_init` lists as
    # `<1>  call FAT_SEG:%1`, tagged with the expansion depth. So a row is
    # named from the INVOCATION line - which carries the name and emits no
    # bytes - and the expansion only says where the call landed.
    #
    # WITHOUT THAT, TWO THIRDS OF kmain IS INVISIBLE. Every OVWCALL, OVLGATE
    # and SPLGATE site is dropped, the phases either side of a run of them
    # merge, and the whole run is charged to whichever plain `call` happens to
    # close it. Measured on the tree this was fixed against: eleven rows where
    # there are thirty, with drv_boot's 1,463 ms and nine sectors of floppy
    # booked to `thm_set` - two instructions of palette resolve that does no
    # I/O at all. It reads as a phase list, not as a broken one, which is why
    # it survived: nothing about the output says a row is missing.
    emit = []
    inmain = False
    row = re.compile(r"^\s*\d+\s+([0-9A-F]{8})\s+([0-9A-F]{2}[0-9A-F <>\[\]-]*?)\s{2,}(\S.*)$")
    quiet = re.compile(r"^\s*\d+\s{2,}(\S.*)$")     # a line that emitted nothing
    deep = re.compile(r"^<\d+>\s*")                  # nasm's expansion marker
    macro = None                                      # the invocation in force
    for L in lines:
        if re.match(r"^\s*\d+\s+kmain:", L):
            inmain = True
            continue
        if not inmain:
            continue
        m = row.match(L)
        if not m:
            q = quiet.match(L)
            if q:
                t = q.group(1).split(";")[0].strip()
                if t and not t.startswith("<"):
                    # A source-level line. An invocation is the ALL-CAPS kind;
                    # anything else (a label, a %if) retires the last one, so a
                    # plain `call` can never inherit a name it has no claim to.
                    w = t.split(None, 1)
                    macro = (w[1].split(",")[0].strip()
                             if w[0].isupper() and len(w) > 1 else None)
            continue
        text = m.group(3)
        expanded = deep.match(text) is not None
        text = deep.sub("", text).split(";")[0].strip()
        if expanded and macro:
            text = text.replace("%1", macro)
        emit.append((int(m.group(1), 16), text, macro if expanded else None))
        if text.startswith("jmp ui_task"):
            break

    out = []
    for i, (addr, text, arg) in enumerate(emit):
        if not text.startswith("call "):
            continue
        if i + 1 >= len(emit):
            break
        # THE MACRO'S OWN ARGUMENT BEATS THE BODY'S TEXT. SPLGATE1 and
        # OVLGATE1 both end in a bare `call spl_gate`, so the body cannot tell
        # drv_boot_x from xm_boot_x and the invocation can. A numeric argument
        # is not a name - MARK and BPMARK take an index - so those fall back.
        name = arg if (arg and not arg.isdigit()) else None
        if name is None:
            name = text[5:].strip().split(":")[-1]  # `FAT_SEG:ovl_clk_init`
        out.append((emit[i + 1][0], name))
    if not out:
        raise SystemExit("os88boot: no calls found in kmain - listing format?")
    return out


def collapse(sites):
    """Merge the runs of `call spl_step` and friends into one row each.

    Three of `kmain`'s calls are a notch on the loading bar and two are the
    lock around the paint; naming each of them separately in a chart that has
    twenty rows already buys nothing. A run of the SAME callee collapses; two
    different ones never do.
    """
    out = []
    for addr, name in sites:
        if out and out[-1][1] == name:
            out[-1] = (addr, name, out[-1][2] + 1)
        else:
            out.append((addr, name, 1))
    return out


def walk(m, marks, limit=180.0):
    """Run the guest from where it is, stopping at each mark in turn.

    Yields (name, cycles, disk) at every one. The marks are armed ONE AT A
    TIME rather than all at once, so a callee that is also reached from
    somewhere else cannot land the walk on the wrong row - and a mark that is
    never reached is a timeout naming itself rather than a silent skip.
    """
    for name, addr in marks:
        m.bp_exec(addr)
        m.run()
        if m.wait_stop(limit) is None:
            raise SystemExit("os88boot: never reached %s (%05X) - the boot "
                             "either hung or does not go through it" % (name, addr))
        yield name, m.status()["cycles"], m.disk()


def bracket(m, back, limit):
    """Run to `back` and answer the cycle count there."""
    m.bp_exec(back)
    m.run()
    if m.wait_stop(limit) is None:
        raise SystemExit("os88boot: never came back to %05X" % back)
    return m.status()["cycles"]


def splash_entry(defines=("KERN_BIG",)):
    """Where `spl_tick` is once the blob has landed: the flat address the
    boot sector's `call spl_tick` reaches.

    NOT a fixed kernel offset. The pinned far entry at KERNEL_SEG:0008 that
    stage 1 once called has had no caller since SPEC.md 2.9.4 - stage 2 draws
    the splash from inside the blob and calls it NEAR - so a bracket on that
    offset is a bracket on an address the boot never reaches, and reads as a
    splash that costs nothing while the sector loop swallows its ~225 ms.
    `.boot2` has no segment of its own in the map (os88sym refuses to place
    it), and it does not need one: stage 1 reads the blob to the heap's floor,
    which is `HEAP_SEG`.
    """
    return (os88sym.equates(defines)["HEAP_SEG"] * 16
            + os88sym.syms(defines)["spl_tick"])


def bootsector_walk(m, kmain, splash, limit=180.0):
    """Run the boot sector to `kmain`, charging its three jobs separately.

    The sector is over half of a boot and is not one thing:

      * `int 13h`   - SPEC.md 18.93's multi-sector reads. Everything the drive
                      costs is in here, and so is everything the machine's ROM
                      costs to ask for it, which is the pair worth separating
                      from our own code
      * the splash  - one NEAR `call spl_tick` a run, from stage 2 into the
                      blob it shares with the screen (SPEC.md 15.3, 2.9.4)
      * the loop    - what is left: the BPB arithmetic, the run bounds and the
                      destination segment walk

    Both brackets take the return address off the guest's OWN STACK rather
    than computing it. The sector relocates itself to the top of whatever
    int 12h reported, so its addresses are a property of the machine's memory
    size; and the ROM's int 13h is wherever that ROM put it. THE TWO FRAMES
    DIFFER IN SHAPE: an `int` leaves CS:IP as the bottom two words and the
    splash's near call leaves IP alone, with CS still the caller's - and
    reading a near frame as a far one brackets to an address the boot never
    reaches, which is a hang at the timeout rather than an error.
    """
    vec = m.read(0x13 * 4, 4)                  # the live int 13h vector
    rom13 = (int.from_bytes(vec[2:4], "little") << 4) \
        + int.from_bytes(vec[0:2], "little")
    spent = {"splash": 0, "int13": 0}
    calls = {"splash": 0, "int13": 0}
    while True:
        m.breakpoints([{"type": "exec", "addr": splash},
                       {"type": "exec", "addr": rom13},
                       {"type": "exec", "addr": kmain}])
        m.run()
        if m.wait_stop(limit) is None:
            raise SystemExit("os88boot: the boot sector never reached kmain")
        st = m.status()
        if st["flat_ip"] == kmain:
            return spent, calls
        r = m.regs()
        if st["flat_ip"] == splash:
            kind = "splash"
            frame = m.read((r["ss"] << 4) + r["sp"], 2)
            back = (r["cs"] << 4) + int.from_bytes(frame, "little")
        else:
            kind = "int13"
            frame = m.read((r["ss"] << 4) + r["sp"], 4)
            back = (int.from_bytes(frame[2:4], "little") << 4) \
                + int.from_bytes(frame[0:2], "little")
        spent[kind] += bracket(m, back, limit) - st["cycles"]
        calls[kind] += 1


def profile(image, apps=None, machine="os8088_5150_cga", defines=("KERN_BIG",),
            build="build"):
    sites = collapse(callsites(defines, build))
    kmain = KERNEL_SEG * 16 + os88sym.syms(defines)["kmain"]
    splash = splash_entry(defines)
    tail = [(name if n == 1 else "%s x%d" % (name, n), KERNEL_SEG * 16 + addr)
            for addr, name, n in sites]

    rows, prev, pdisk = [], 0, None

    def note(name, cyc, disk, kind):
        """Close a phase AT cumulative cycle `cyc` with cumulative `disk`."""
        nonlocal prev, pdisk
        row = {"phase": name, "kind": kind, "cycles": cyc - prev,
               "ms": (cyc - prev) * 1000.0 / HZ, "at_ms": cyc * 1000.0 / HZ}
        for k in ("reads", "read_sectors", "seeks", "seek_cylinders",
                  "transfer_ms", "seek_ms"):
            row[k] = disk[k] - (pdisk[k] if pdisk else 0)
        rows.append(row)
        prev, pdisk = cyc, disk

    with os88marty.launch(image, apps=apps, machine=machine, boot=False) as m:
        # --- the machine's own ROM, which os8088 does not write --------------
        m.bp_exec(0x7C00)
        m.run()
        if m.wait_stop(180.0) is None:
            raise SystemExit("os88boot: the machine never reached 0000:7C00")
        note("post", m.status()["cycles"], m.disk(), "rom")

        # --- the boot sector, split three ways ------------------------------
        spent, calls = bootsector_walk(m, kmain, splash)
        at, disk = m.status()["cycles"], m.disk()
        # The int 13h row is closed FIRST and takes the whole sector's disk
        # counters with it, which is where they belong: the other two rows do
        # no I/O, and closing them against the same cumulative reading is what
        # makes their deltas zero rather than negative.
        note("boot: int 13h x%d" % calls["int13"], prev + spent["int13"], disk, "disk")
        note("boot: splash x%d" % calls["splash"], prev + spent["splash"], disk, "draw")
        note("boot: sector loop", at, disk, "kernel")

        for name, cyc, d in walk(m, tail):
            note(name, cyc, d, "kernel")

        total = prev
        longest = pdisk["longest_run"]
        # SPEC.md 15.4's own timer, in 18.2065Hz ticks: the cross-check. It is
        # stamped by the boot sector, so it measures everything below `post`.
        m.bp_exec()
        ticks = int.from_bytes(m.read(KERNEL_SEG * 16 + 0x000C, 2), "little")
    return {"machine": machine, "image": os.path.basename(image),
            "total_cycles": total, "total_ms": total * 1000.0 / HZ,
            "longest_run": longest,
            "boot_ticks": ticks, "boot_ticks_ms": ticks * 1000.0 / 18.2065,
            "phases": rows}


def main(argv):
    image, apps, machine, out = "build/os8088-360.img", None, "os8088_5150_cga", None
    defines, build = [], None
    it = iter(argv)
    for a in it:
        if a == "--define":
            defines.append(next(it))   # a knob kernel's symbols are its own
        elif a == "--image":
            image = next(it)
        elif a == "--apps":
            apps = next(it)
        elif a == "--machine":
            machine = next(it)
        elif a == "--json":
            out = next(it)
        elif a == "--build":
            build = next(it)           # ...and a sub-make's kernel is in its own directory
        else:
            raise SystemExit(__doc__)
    # KERN_BIG is the default and not a seed: `--define KERN_SMALL` on top of
    # a seeded KERN_BIG assembles a kernel that is both, which is neither.
    if not any(d.split("=")[0] in ("KERN_BIG", "KERN_SMALL") for d in defines):
        defines.append("KERN_BIG")
    # os88sym reads $OS88_BUILD for which kernel.bin to check its map against;
    # --build names the same directory, so the two cannot disagree.
    build = build or os.environ.get("OS88_BUILD") or "build"
    os.environ["OS88_BUILD"] = build
    r = profile(image, apps, machine, tuple(defines), build)
    print("%-28s %10s %9s %7s  %s" % ("phase", "cycles", "ms", "%", "disk"))
    for p in r["phases"]:
        d = ""
        if p["read_sectors"] or p["seek_cylinders"]:
            d = "%d sec / %d read(s), %d cyl, %.0fms mechanical" % (
                p["read_sectors"], p["reads"], p["seek_cylinders"],
                p["transfer_ms"] + p["seek_ms"])
        print("%-28s %10d %9.1f %6.1f%%  %s"
              % (p["phase"], p["cycles"], p["ms"],
                 100.0 * p["cycles"] / r["total_cycles"], d))
    print("%-28s %10d %9.1f" % ("TOTAL (from reset)", r["total_cycles"], r["total_ms"]))
    print("longest single run asked of the FDC: %d sectors" % r["longest_run"])
    below = r["total_ms"] - r["phases"][0]["ms"]
    print("cross-check: the kernel's own boot_ticks is %d = %.0f ms against "
          "%.0f ms of milestones below `post` (%+.0f ms, one tick is 54.9)"
          % (r["boot_ticks"], r["boot_ticks_ms"], below,
             r["boot_ticks_ms"] - below))
    if out:
        json.dump(r, open(out, "w"), indent=1)
        print("-> %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
