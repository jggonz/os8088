#!/usr/bin/env python3
"""EXCITEBIKE: launch-to-title load time on MartyPC's 4.77 MHz XT (PERFORMANCE.md Set 153).

    make excitebikedisk
    python3 tests/excitebike_load.py [--adapter vga|cga|herc|both]

From the loader's entry for the double-clicked 8BITBIKE.O88 (the kernel's ld_run_body_x) to the splash
reveal finishing (the title is up and takes keys), in emulated cycles / 4,772,727 = seconds on the field machine.
The package's own sprite-blob build and art reads are inside that span; the double-click's own timing is not.
"""
import argparse, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools")); sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import excitebike_video as V   # noqa: E402
import exbsim                  # noqa: E402

HZ = M.GUEST_HZ


def one(tag):
    ref = exbsim.Ref()
    sym = V.symbols()
    with os88ui.boot(V.at("build/os8088-360.img"), apps=V.at("build/excitebike360.img"),
                     machine=V.MACHINE[tag]) as ui:
        m = ui.m
        ui.open_drive("B")
        ui.settle()
        # the loader's entry, then (with the package's segment known) the package's own milestones,
        # each a breakpoint that costs the guest no cycle
        m.bp_exec("ld_run_body_x")
        ui.open("8BITBIKE.O88", expect=None)
        assert m.wait_stop(60) == "breakpoint", "the loader was never entered"
        c0 = int(m.status()["cycles"])
        m.disk(reset=True)
        m.breakpoints([])
        m.bp_exec("ld_start")
        m.run()
        assert m.wait_stop(60) == "breakpoint", "ld_start was never reached"
        c_ld = int(m.status()["cycles"])
        rg = m.regs()
        base = rg["es"] << 4                # the region the image was expanded into starts at offset 0
        d_ld = m.disk()
        flat = {base + sym["xb_entry"]: "xb_entry"}
        m.breakpoints([])
        m.bp_exec(*flat.keys())
        tl = [("loader entry (double-click taken)", c0), ("image read from the floppy + expanded, bss zeroed (ld_start)", c_ld)]
        m.run()
        assert m.wait_stop(120) == "breakpoint"
        tl.append(("package entry: window created", int(m.status()["cycles"])))
        m.breakpoints([])
        m.run()
        g = V.Game(ui, tag, sym, ref)
        M.until(m, lambda _: g.w("xb_reveal") >= g.w("xb_frontheight") > 0, "the splash reveal finishes", poll=0.05, guest=30)
        m.pause()
        tl.append(("splash revealed, taking keys", int(m.status()["cycles"])))
        d1 = m.disk()
        m.disk(reset=True)
        # phase 2: the Enter that starts the game -> full screen, the assets, the sprites, the title
        marks = [n for n in ("xb_loading", "xb_gfx_load", "xb_race_open", "xs_load_vga", "xs_load_cga",
                             "xs_compile_v", "xs_compile", "xu_start", "xb_game") if n in sym]
        flat = {base + sym[n]: n for n in marks}
        m.bp_exec(*flat.keys())
        m.run()
        m.key("Enter", up=False)
        M.pace(m, .04)
        m.key("Enter", down=False)
        tl.append(("-- Enter pressed --", None))
        while True:
            assert m.wait_stop(120) == "breakpoint", tl
            cyc = int(m.status()["cycles"])
            ip = (m.regs()["cs"] << 4) + m.regs()["ip"]
            tl.append((flat.get(ip, hex(ip)), cyc))
            if flat.get(ip) == "xb_game":
                break
            m.run()
        m.breakpoints([])
        m.run()
        M.until(m, lambda _: g.b("xb_fs") == 1 and g.b("xb_state") == 0 and g.w("xb_idle") > 2,
                "the title screen is up", poll=0.05, guest=90)
        m.pause()
        tl.append(("title screen up (first idle frames)", int(m.status()["cycles"])))
        d2 = m.disk()
        m.run()
        d = (d1, d2)
        return tl, d_ld, d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="both", choices=["vga", "cga", "herc", "both"])
    a = ap.parse_args()
    for tag in (["vga", "cga", "herc"] if a.adapter == "both" else [a.adapter]):
        tl, d_ld, (d1, d2) = one(tag)
        print("%s: launch timeline (clk since the loader's entry)" % tag)
        prev = None
        for name, c in tl:
            if c is None:
                print("   " + name)
                prev = None
                continue
            print("   %-62s +%9d clk = %6.2f s%s" % (name, c - tl[0][1], (c - tl[0][1]) / HZ,
                  "" if prev is None else "   (step %6.2f s)" % ((c - prev) / HZ)))
            prev = c
        for n, dd in (("loader entry -> ld_start", d_ld), ("loader entry -> splash", d1), ("Enter -> title", d2)):
            print("   floppy, %-26s %3d sectors in %2d reads, transfer %5.0f ms, seek %4.0f ms" %
                  (n, dd["read_sectors"], dd["reads"], dd["transfer_ms"], dd["seek_ms"]), flush=True)


main()
