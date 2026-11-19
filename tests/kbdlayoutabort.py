#!/usr/bin/env python3
"""A valid package HOLDs glyphs, aborts its entry, and gives the hold back.

The stop at ld_unreserve proves the failed entry acquired its own slot's
hold before cleanup. LD_EABORT, the unloaded module row and a scan of live
heap claims prove the unwind. A successful package must then reuse that
slot, see QUERY=0 before its own HOLD, repeat HOLD idempotently and release.

Prerequisite (runner wants=): build/kbdlayoutabort.img and build/kbdhold.bin.
This row never builds or changes shared artifacts. --negative-cleanup NOPs
the uniquely anchored kl_release_inst call inside inst_rel_rec on its own
guest, after proving the hold exists. Its post-abort checks must then fail.
"""
import argparse
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                                # noqa: E402
import os88marty                                                # noqa: E402
import os88sym                                                  # noqa: E402
import os88ui                                                   # noqa: E402
from kbdsettings import require, set_file                        # noqa: E402


def u16(data, at=0):
    return int.from_bytes(data[at:at + 2], "little")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--fixture", default="build/kbdlayoutabort.img")
    ap.add_argument("--live-bin", default="build/kbdhold.bin")
    ap.add_argument("--negative-cleanup", action="store_true",
                    help="disable failed-entry keyboard release; this run must fail")
    a = ap.parse_args(argv)
    image = Path(os88build.at(a.image)).resolve()
    fixture = Path(os88build.at(a.fixture)).resolve()
    binary = Path(os88build.at(a.live_bin)).resolve()
    require(fixture.is_file() and binary.is_file(),
            "build the declared kbdlayoutabort fixture before running this row")
    eq, syms = os88sym.equates(), os88sym.syms()
    build = Path(os88build.at("build")).resolve()
    build.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kbdlayoutabort-", dir=build) as tmp:
        scratch = Path(tmp)
        fresh = scratch / "system.img"
        shutil.copyfile(image, fresh)
        set_file(fresh, "SYSTEM.CFG", None, scratch)
        with os88ui.boot(str(fresh), apps=str(fixture), machine=a.machine,
                         quick=True) as ui:
            m, S = ui.m, ui.sym

            def word(name):
                return u16(m.read(S(name), 2))

            def clients():
                seg = word("mod_r_keyboard")
                return u16(m.readseg(seg, syms["klm_clients"], 2)) if seg else 0

            def keyboard_claims():
                def read(mm):
                    raw = mm.read(S("mem_tab"), eq["MEM_MAX"] * eq["MC_SIZE"])
                    found = []
                    for at in range(0, len(raw), eq["MC_SIZE"]):
                        seg = u16(raw, at + eq["MC_SEG"])
                        if not seg or u16(raw, at + eq["MC_OWN"]) != eq["MEM_K_MOD"]:
                            continue
                        hdr = bytes(mm.readseg(seg, 0, 4))
                        if hdr == b"O8" + bytes([eq["MOD_VER"], eq["MOD_KEYBOARD"]]):
                            found.append(seg)
                    return found
                return os88marty.heap_read(m, read, "optional keyboard claim census")

            def empty(why):
                require(not word("mod_r_keyboard") and not clients(),
                        why + ": optional module or glyph client remains")
                require(not keyboard_claims(), why + ": KEYBOARD.DRV heap claim remains")

            empty("default boot")
            disk = ui.open_drive("B")
            before = {w.i for w in ui.windows()}
            m.bp_exec(S("ld_unreserve"))
            try:
                # The click is resolved by filename. Completion is the
                # cleanup breakpoint, rather than a new-window wait.
                ui.open("KBDABORT.O88", expect=None, win=disk)
                require(m.wait_stop(60), "failed entry never reached ld_unreserve")
                rec = word("ld_rec")
                offset = rec - syms["inst_tab"]
                require(offset >= 0 and offset % eq["I_RECSZ"] == 0,
                        "ld_rec does not identify a reserved instance record")
                slot = offset // eq["I_RECSZ"]
                require(slot < eq["INST_MAX"], "failed-entry slot is out of range")
                require(word("mod_r_keyboard") and clients() == 1 << slot,
                        "failed entry did not acquire exactly its own glyph hold")
                require(len(keyboard_claims()) == 1,
                        "failed entry did not load exactly one optional module claim")
                print("before unwind: reserved slot", slot, "has its glyph hold")

                if a.negative_cleanup:
                    at = S("inst_rel_rec")
                    target = S("kl_release_inst")
                    body = bytes(m.read(at, 28))
                    sites = []
                    for i in range(len(body) - 2):
                        rel = int.from_bytes(body[i + 1:i + 3], "little", signed=True)
                        if body[i] == 0xe8 and at + i + 3 + rel == target:
                            sites.append(at + i)
                    require(len(sites) == 1,
                            "failed-entry release negative-control CALL anchor moved")
                    m.write(sites[0], b"\x90" * 3)
                    print("negative control armed: instance sweep omits keyboard release")
            finally:
                m.bp_exec()
                m.run()
            os88marty.ui_done(m, "failed-entry cleanup to complete")
            require(m.read(S("ld_status"), 1)[0] == eq["LD_EABORT"],
                    "fixture did not report LD_EABORT")
            require({w.i for w in ui.windows()} == before,
                    "an aborting entry left a window alive")
            require(m.read(S("inst_tab") + slot * eq["I_RECSZ"] + eq["I_STATE"], 1) == b"\0",
                    "failed instance record remains published")
            empty("entry abort")

            win = ui.open("KBDHOLD.O88", win=disk)
            os88marty.ui_done(m, "successful entry to finish")
            pkg = os88marty.Pkg(m, win, image=binary.stat().st_size, sym=S)
            pre, after, error, serial = pkg.reads([(0, 2), (2, 2), (4, 1), (5, 1)])
            owner = m.read(S("wm_owner") + win.i, 1)[0]
            require(owner == slot, "next successful entry did not reuse the failed slot")
            require(u16(pre) == 0, "reused slot inherited a glyph hold before HOLD")
            require(error == b"\0" and u16(after) == 0x0100 and serial == b"\x01",
                    "successful entry did not obtain a fresh BIOS-layout glyph hold")
            require(clients() == 1 << slot and len(keyboard_claims()) == 1,
                    "fresh HOLD has the wrong instance ownership/claim count")

            def key(name, expected, result):
                old = pkg.byte(5)
                ui.press(name, need=True)
                ui._wait(lambda: pkg.byte(5) == (old + 1) & 0xff,
                         "fixture keyboard control to finish", os88ui.T_NAV)
                require(pkg.byte(4) == 0 and pkg.word(2) == result,
                        "fixture keyboard control refused or returned wrong status")
                require(clients() == expected, "fixture keyboard client mask is wrong")

            key("KeyH", 1 << slot, 0x0100)     # idempotent: never a count of two
            key("KeyR", 0, 0)
            empty("explicit release in reused slot")
            key("KeyH", 1 << slot, 0x0100)
            ui.close(win)
            empty("closing reused slot")
            print("PASS failed-entry HOLD cleanup, empty reused slot and fresh HOLD/RELEASE")


if __name__ == "__main__":
    main()
