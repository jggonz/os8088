#!/usr/bin/env python3
"""Native Boratype QA through real installed-package input (MartyPC).

Build boratypedisk first. No guest writes, frame advances, host sleeps or
guessed OS coordinates drive this test. --host-check needs only a C compiler;
it does not boot or claim native verification. See docs/boratype/target-test.md.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
SRC = Path(os.environ.get("OS88_SRC", ROOT)).resolve()
sys.path.insert(0, str(ROOT / "tools"))
import os88geom as geom
import os88build
import os88marty as M
import os88sym
import os88ui
from os88fat import Fat12
from os88flush import Flush, Volume

SESSION_BYTES = ("mode status lesson difficulty duration ranked started committed "
                 "last_wrong error_run countdown new_record profile paused_ready").split()
SESSION_WORDS = ("attempts correct errors net pos length completed streak best_streak "
                 "backspaces elapsed last_tick ready_ticks wpm accuracy seconds remaining "
                 "seed events achievement_new").split()
PROFILE_FIELDS = ("name lesson_best lesson_done key_attempt key_error record_wpm record_accuracy "
                  "achievements best_streak practice_minutes baseline_wpm best_wpm next_lesson "
                  "recent_wpm recent_accuracy recent_seconds recent_mode recent_next").split()
STORE_FIELDS = "profile selected music effects voice keyclick roast display generation".split()
CONSTANTS = ("BT_LESSONS BT_PROFILES BT_KEYS BT_PASSAGE BT_MASTERY BT_LESSON BT_PRACTICE "
             "BT_CHALLENGE BT_IDLE BT_READY BT_ACTIVE BT_PAUSED BT_FINISHED BT_ACH_FIRST "
             "BT_ACH_MASTER BT_ACH_25 BT_ACH_50 BT_S_HOME BT_S_LESSONS BT_S_PLAY BT_S_RESULTS "
             "BT_S_PRACTICE BT_S_CHALLENGE BT_S_RECORDS BT_S_SETTINGS BT_S_HELP BT_S_PAUSE "
             "BT_S_PROFILE BT_A_START BT_A_LESSON_BASE BT_A_PROFILE_BASE BT_A_RETRY BT_A_MUSIC BT_A_EFFECTS "
             "BT_A_KEYCLICK BT_A_ROAST BT_A_TEST_MUSIC "
             "BT_A_DURATION_BASE BT_A_DIFFICULTY_BASE BT_A_PAUSE BT_R_EMPTY").split()


def target_layout():
    """Ask the actual uint16_t production header; do not hand-copy offsets.

    These three types contain no pointers/int members. Their native layout is
    exactly the host's 1-byte BTByte / 2-byte BTWord layout (alignment <=2).
    Plain C UI globals are read explicitly as two-byte native ints separately.
    """
    types = {"BTSession": SESSION_BYTES + SESSION_WORDS + ["key_attempt", "key_error", "passage"],
             "BTProfile": PROFILE_FIELDS, "BTStore": STORE_FIELDS}
    lines = ['#include <stddef.h>', '#include <stdio.h>', '#define BT_HOST',
             '#include "bt.h"', '#include "bt_face.h"', '#include "bt_audio.h"', 'int main(void) {',
             'printf("word %u\\n", (unsigned)sizeof(BTWord));']
    for typ, names in types.items():
        lines += [f'printf("{typ}.size %u\\n", (unsigned)sizeof({typ}));',
                  f'printf("{typ}.align %u\\n", (unsigned)offsetof(struct {{ char pad; {typ} v; }}, v));']
        for name in names:
            lines.append(f'printf("{typ}.{name} %u\\n", (unsigned)offsetof({typ}, {name}));')
    for name in CONSTANTS:
        lines.append(f'printf("{name} %u\\n", (unsigned){name});')
    lines += ['return 0;', '}']
    with tempfile.TemporaryDirectory(prefix="boratype-layout-") as tmp:
        source, exe = Path(tmp) / "layout.c", Path(tmp) / "layout"
        source.write_text("\n".join(lines) + "\n")
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-I", str(SRC / "apps/boratype"), str(source), "-o", str(exe)], check=True)
        data = dict((key, int(value)) for key, value in
                    (line.split() for line in subprocess.check_output([str(exe)], text=True).splitlines()))
    assert data["word"] == 2
    for typ in types:
        assert data[typ + ".align"] <= 2, (typ, data[typ + ".align"])
        offsets = [data[typ + "." + name] for name in types[typ]]
        assert len(set(offsets)) == len(offsets) and all(0 <= n < data[typ + ".size"] for n in offsets)
    assert data["BTSession.passage"] + data["BT_PASSAGE"] + 1 <= data["BTSession.size"]
    assert data["BTStore.selected"] == data["BTProfile.size"] * data["BT_PROFILES"]
    return data


def app_symbols(build):
    """NASM's linked map, proved against the exact full native image."""
    with tempfile.TemporaryDirectory(prefix="boratype-map-") as tmp:
        mp, binary = Path(tmp) / "map", Path(tmp) / "native.bin"
        subprocess.run([os.environ.get("NASM", "nasm"), "-f", "bin", "-w+error",
                        "-I", str(SRC / "apps") + "/", "-I", str(build) + "/",
                        "--before", f"[map all {mp}]", "-o", str(binary),
                        str(SRC / "apps/boratype/boratype.asm")], cwd=SRC, check=True)
        assert binary.read_bytes() == (build / "boratype.bin").read_bytes(), \
            "NASM map describes a different Boratype binary; rebuild the complete package"
        result = {}
        for line in mp.read_text().splitlines():
            match = re.fullmatch(r"\s*([0-9A-F]+)\s+([0-9A-F]+)\s+(\w+)\s*", line)
            if match:
                result[match[3]] = int(match[1], 16)
    for name in ("_bt_store", "_bt_session", "_bt_screen", "_tf_nhit", "_tf_hit"):
        assert name in result, "missing production symbol " + name
    return result


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def drive_root(ui, letter):
    """Raise an existing Disk by its live volume, even behind a full app.

    A desktop drive icon can be covered. Clicking through the application is
    not navigation; os88ui's dock/title raise is the valid route to its Disk.
    """
    drive = ord(letter.upper()) - ord("A")
    candidates = [w for w in ui.windows() if (lambda fs: fs is not None and fs[0] == drive)(ui.fs_of(w))]
    if not candidates:
        return ui.open_drive(letter)
    win = ui.raise_window(candidates[-1])
    for _ in range(32):
        state = ui.fs_of(win)
        assert state is not None and state[0] == drive
        if state[1] == 0:
            return win
        win = ui.open("..", expect="nav", win=win)
    raise AssertionError("Disk did not reach its volume root")


def open_package(ui, path):
    parts = [p for p in path.replace("\\", "/").split("/") if p]
    if parts and len(parts[0]) == 2 and parts[0][1] == ":":
        drive_root(ui, parts.pop(0)[0])
    assert parts
    return ui.path("/".join(parts), limit=90)


class App:
    def __init__(self, ui, win, symbols, layout, evidence):
        self.ui, self.win, self.sym, self.L, self.evidence = ui, win, symbols, layout, evidence
        self.p = M.Pkg(ui.m, win, sym=ui.sym)
        self.ui._wait(lambda: self.global_word("bt_screen") == self.L["BT_S_HOME"],
                      "Boratype home state", 60, snapshot=self.state)
        self.ui._wait(lambda: any(menu[0] == "Lesson" for menu in ui.menus()),
                      "Boratype installed native menus", 30)
        # Prove this running image matches the map's resident instructions as
        # well as its header sizes. These hot entries have no bound far cells.
        native = (Path(evidence["app_build"]) / "boratype.bin").read_bytes()
        live = self.p.read(0, 26)
        assert live[:2] == b"O8" and live[16:25] == b"BORATYPE\0", live
        assert live[8:12] == native[8:12], "loaded package image/BSS differs from mapped build"
        for name in ("_bt_key", "_bt_tick", "_bt_seconds"):
            at = self.sym[name]
            assert self.p.read(at, 48) == native[at:at + 48], "stale installed code at " + name

    def global_word(self, name):
        return self.p.word(self.sym["_" + name])  # native int is two bytes

    def text(self, name, cap=64):
        return self.p.read(self.sym["_" + name], cap).split(b"\0")[0].decode("ascii", "replace")

    def state(self):
        raw = self.p.read(self.sym["_bt_session"], self.L["BTSession.size"])
        out = {name: raw[self.L["BTSession." + name]] for name in SESSION_BYTES}
        out.update({name: struct.unpack_from("<H", raw, self.L["BTSession." + name])[0]
                    for name in SESSION_WORDS})
        out["passage"] = raw[self.L["BTSession.passage"]:].split(b"\0")[0].decode("ascii")
        assert out["attempts"] == out["correct"] + out["errors"], out
        assert out["net"] == out["completed"] + out["pos"], out
        assert 0 <= out["pos"] <= out["length"] <= self.L["BT_PASSAGE"], out
        out["screen"] = self.global_word("bt_screen")
        return out

    def store_byte(self, name):
        return self.p.byte(self.sym["_bt_store"] + self.L["BTStore." + name])

    def generation(self):
        return self.p.word(self.sym["_bt_store"] + self.L["BTStore.generation"])

    def profile(self, number=None):
        if number is None:
            number = self.store_byte("selected")
        return self.p.read(self.sym["_bt_store"] + self.L["BTStore.profile"] +
                           number * self.L["BTProfile.size"], self.L["BTProfile.size"])

    def pword(self, field, offset=0):
        return struct.unpack_from("<H", self.profile(), self.L["BTProfile." + field] + offset)[0]

    def wait_screen(self, screen):
        wanted = self.L["BT_S_" + screen]
        self.ui._wait(lambda: self.global_word("bt_screen") == wanted,
                      "Boratype " + screen, 90, snapshot=self.state)
        return self

    def press(self, key):
        print("      Boratype key %s from screen=%d" % (key, self.global_word("bt_screen")), flush=True)
        self.ui.press(key, limit=90)
        return self

    def menu(self, menu, item, screen=None):
        self.ui.raise_window(self.win)
        self.ui.menu_pick(menu, item, limit=40)
        if screen:
            self.wait_screen(screen)
        return self

    def click_action(self, action):
        """Application hit rects come directly from the production face cache."""
        found = {}

        def locate():
            n = self.global_word("tf_nhit")
            assert 0 <= n <= 32, n
            if not n:  # A sliced scene may be between cache clear and first button.
                return False
            raw = self.p.read(self.sym["_tf_hit"], n * 10)
            hits = [struct.unpack_from("<5h", raw, i * 10) for i in range(n)]
            found["hits"] = hits
            found["hit"] = next((h for h in hits if h[4] == action), None)
            return found["hit"] is not None

        self.ui._wait(locate, "visible Boratype action " + str(action), 45, snapshot=lambda: found)
        x, y, w, h, _ = found["hit"]
        assert w > 0 and h > 0
        self.ui.mo.click(x + w // 2, y + h // 2, settle=0)
        M.ui_done(self.ui.m, "Boratype application button", limit=90)
        return self

    def key(self, character, expected_correct, foreign=False):
        before = self.state()
        cycles = self.ui.m.status()["cycles"]
        # Native animation slices and FSX need a scoring acknowledgement,
        # not generic UI idle or a BIOS-ring fallback. Keep just one real
        # printable key unacknowledged, then witness production counters.
        self.ui.m.type_text(character)
        self.ui._wait(lambda: self.state()["attempts"] == before["attempts"] + 1,
                      "one printable attempt", 30, snapshot=self.state)
        after = self.state()
        assert after["correct"] == before["correct"] + int(expected_correct), after
        assert after["errors"] == before["errors"] + int(not expected_correct), after
        assert after["pos"] == before["pos"] + int(expected_correct), after
        self.evidence.setdefault("score_witness_cycles", []).append(
            self.ui.m.status()["cycles"] - cycles)
        return after

    def finish_passage(self, foreign=False):
        """Ordinary tutoring input: acknowledge each accepted character.

        Queuing an entire passage at once tests BIOS-ring stress, not an
        ordinary completed lesson. Keep that separate from this oracle.
        """
        count = 0
        while self.state()["status"] == self.L["BT_ACTIVE"]:
            state = self.state()
            assert state["mode"] != self.L["BT_CHALLENGE"]
            assert count < self.L["BT_PASSAGE"], "passage failed to make progress"
            self.key(state["passage"][state["pos"]], True, foreign=foreign)
            count += 1
        self.wait_screen("RESULTS")
        return self.state()

    def shot(self, directory, name, foreign=False, settled=True):
        if settled and foreign:
            # FSX owns the UI callback until it exits; ui_done cannot hold
            # there. Wait for its actual VRAM to stop changing across several
            # refreshes; a tick stamp written before paint is not a witness.
            base = self.global_word("bt_cga_seg") * 16
            M.quiesce(self.ui.m, lambda: self.ui.m.read(base, 16384), guest=0.15,
                      budget=15, what="CGA painted VRAM stable")
        elif settled:
            if self.global_word("bt_screen") == self.L["BT_S_PLAY"]:
                # Idle callbacks drain the deliberately sliced hint, stats
                # and accepted-input rows after urgent prompt feedback.
                # ui_done alone may be true between those callbacks.
                def primary_pending():
                    return (self.global_word("tf_hintpending"), self.global_word("tf_statpend"),
                            self.global_word("tf_footpending"),
                            self.p.word(self.sym["_tf_dirtyfirst"] + 4))
                self.ui._wait(lambda: primary_pending() == (0, 0, 0, 65535),
                              "native primary field paint queue drained", 30, snapshot=primary_pending)
            self.ui.still()
        w, h, rgb = self.ui.m.fbuf()
        path = directory / (name + ".png")
        M.write_png_rgb(str(path), w, h, rgb)
        self.evidence.setdefault("captures", []).append({"path": str(path), "w": w, "h": h,
                                                        "inspection": "required", "settled": settled})
        self.evidence.setdefault("states", {})[name] = self.state()

    def resource_snapshot(self):
        """One heap-consistent read; package claims can move between inputs."""
        eq = os88sym.equates()

        def read(mm):
            seg = M.pkg_seg(mm, self.win, self.ui.sym)
            owner = mm.read(self.ui._S("wm_owner") + self.win.i, 1)[0]
            raw = mm.read(self.ui._S("mem_tab"), eq["MEM_MAX"] * eq["MC_SIZE"])
            claims = []
            for i in range(eq["MEM_MAX"]):
                at = i * eq["MC_SIZE"]
                base = struct.unpack_from("<H", raw, at + eq["MC_SEG"])[0]
                own = struct.unpack_from("<H", raw, at + eq["MC_OWN"])[0]
                if base and own in (seg, owner):
                    paragraphs = struct.unpack_from("<H", raw, at + eq["MC_PARA"])[0]
                    claims.append({"segment": base, "owner": own, "bytes": paragraphs * 16})
            return {"package_segment": seg, "instance": owner, "claims": claims,
                    "total_claim_bytes": sum(c["bytes"] for c in claims)}

        return M.heap_read(self.ui.m, read, "Boratype memory ledger")


def wait_ticks(ui, ticks):
    start = ui._word("ticks")
    ui._wait(lambda: ((ui._word("ticks") - start) & 65535) >= ticks,
             "guest tick counter to advance", ticks * 65536 / 1193182 + 5)


def display_state(ui):
    return {"kind": ui._byte("vid_kind"), "w": ui._word("vid_cw"),
            "h": ui._word("vid_ch"), "bpp": ui._byte("vid_planes")}


def config_records(image):
    """Inspect the real SYSTEM.CFG shipped on the selected driver disk."""
    try:
        raw = Fat12(image).read("SYSTEM.CFG")
    except KeyError:
        return {}
    assert raw[:8] == b"O88CFG\0\0" and len(raw) >= 12, "invalid driver-disk SYSTEM.CFG"
    records, at = {}, 10
    while at + 2 <= len(raw) and raw[at:at + 2] != b"\0\0":
        assert at + 4 <= len(raw), "truncated SYSTEM.CFG record"
        key, version, length = raw[at:at + 2].decode("ascii"), raw[at + 2], raw[at + 3]
        assert at + 4 + length <= len(raw), "truncated SYSTEM.CFG payload"
        records[key] = {"version": version, "data": list(raw[at + 4:at + 4 + length])}
        at += 4 + length
    return records


def prepare_driver_disk(args, build):
    """Private shipped-system copy with the established VESA CFG records.

    Never alter a source disk or introduce a display implementation. The
    real driver must attach and the live mode must match afterward.
    """
    if args.image is not None or not args.adapter.startswith("vesa"):
        return args.image or build / "os8088-360.img"
    fixture = build / ("vesa8.img" if args.adapter == "vesa800" else "vesa.img")
    if fixture.exists():
        return fixture
    base = build / "os8088.img"
    assert base.is_file(), "prepare the shipped system image first: make " + str(base)
    dst = args.evidence / (args.adapter + "-system.img")
    shutil.copyfile(base, dst)
    cfg = b"O88CFG\0\0" + struct.pack("<H", 3) + b"DW\x01\x02" + struct.pack("<H", 1 << 9)
    if args.adapter == "vesa800":
        cfg += b"VM\x03\x04\x00\x00\x00\x8a"
    cfg += b"\0\0"
    cfgfile = args.evidence / (args.adapter + "-SYSTEM.CFG")
    cfgfile.write_bytes(cfg)
    volume = Fat12(dst)
    if volume.find("SYSTEM.CFG")[2] is not None:
        volume.delete("SYSTEM.CFG")
    volume.add(cfgfile, "SYSTEM.CFG")
    if volume.find("VESA.DRV")[2] is None:
        driver = build / "vesa.drv"
        assert driver.is_file(), "build the shipped VESA.DRV before this scenario"
        volume.add(driver, "VESA.DRV")
    volume.save()
    return dst


def verify_adapter(ui, requested, evidence):
    actual = display_state(ui)
    expected = {"vga": (0, 640, 480, 4), "cga": (2, 640, 200, 1),
                "herc": (1, 720, 348, 1), "ega": (3, 640, 350, 4),
                "vesa800": (4, 800, 600, 8), "vesa1024": (4, 1024, 768, 16)}
    if requested != "auto":
        assert tuple(actual[k] for k in ("kind", "w", "h", "bpp")) == expected[requested], \
            ("requested adapter was not actually attached", requested, actual)
    evidence["actual_display"] = actual
    evidence["timing_instrument"] = ("guest cycles at 4.77MHz" if not getattr(ui.m, "qemu", False)
                                     and not getattr(ui.m, "box86", False)
                                     else "emulated clock only; no XT cycle-performance claim")


def grow_window(ui, win, width, height):
    """Real grow gesture, from the kernel's source-defined grow rectangle.

    os88ui has no resize verb. Use its mouse/geometry and prove both the
    press and tracked outline, without clearing or writing kernel statics.
    """
    body = (SRC / "kernel/wm.inc").read_text().split("\nwm_grow_rect:", 1)[1].split("\n    ret", 1)[0]
    inset = re.search(r"sub ax,\s*(\d+)", body)
    span = re.search(r"add cx,\s*(\d+)", body)
    assert inset and span, "kernel grow-box geometry changed; derive its current center"
    offset = int(inset[1]) - int(span[1]) // 2
    live = ui._refresh(win)
    ui.raise_window(live)
    gx, gy = live.x + live.w - offset, live.y + live.h - offset
    tx, ty = live.x + width - offset, live.y + height - offset
    record = ui._wrec(live)
    ui.mo.to(gx, gy)
    ui._edge_until(lambda: ui._word("ui_dragwin") == record and
                   ui._word("ui_origx") == live.w and ui._word("ui_origy") == live.h,
                   "grow-box press accepted for this geometry", 20)
    ui.mo.to(tx, ty, l=True)
    tracked = M.quiesce(ui.m, lambda: (ui._word("ui_dragw"), ui._word("ui_dragh")),
                        guest=0.2, budget=15, what="grow outline settled")
    assert tracked is not None
    ui.mo._edge(False)
    ui._wait(lambda: ui.m.read(ui._S("gfx_lock_flag"), 1)[0] == 0 and
             (ui._refresh(win).w, ui._refresh(win).h) != (live.w, live.h),
             "resize commit and drawing complete", 90)
    return ui._refresh(win)


def window_checks(app, directory):
    L, ui = app.L, app.ui
    if app.state()["status"] != L["BT_ACTIVE"]:
        app.press("F2").wait_screen("LESSONS")
        app.click_action(L["BT_A_LESSON_BASE"])
        app.press("Enter").wait_screen("PLAY")
        app.key(app.state()["passage"][0], True)
    before = app.state()
    app.press("F1").wait_screen("HELP")
    assert app.state()["status"] == L["BT_PAUSED"] and app.state()["pos"] == before["pos"]
    app.shot(directory, "window-live-lesson-help")
    app.press("Escape").wait_screen("PAUSE")
    assert app.state()["pos"] == before["pos"]
    app.press("F4").wait_screen("PLAY")
    live = ui._refresh(app.win)
    original = [live.x, live.y, live.w, live.h]
    disk = drive_root(ui, "B")
    ui._wait(lambda: app.state()["status"] == L["BT_PAUSED"], "focus loss pauses guided lesson", 30)
    covered = ui._refresh(disk)
    assert max(live.x, covered.x) < min(live.x + live.w, covered.x + covered.w)
    assert max(live.y, covered.y) < min(live.y + live.h, covered.y + covered.h)
    app.shot(directory, "window-covered-focus-lost")
    paused = app.state()
    wait_ticks(ui, 8)
    assert app.state()["elapsed"] == paused["elapsed"] and app.state()["pos"] == before["pos"]
    ui.raise_window(app.win)
    assert app.state()["status"] == L["BT_PAUSED"], "focus return resumed without intent"
    app.shot(directory, "window-uncovered-still-paused")
    app.press("F4").wait_screen("PLAY")
    minimum = struct.unpack("<HH", ui.m.read(os88sym.wfield(app.win.i, "W_MINW"), 4))
    assert minimum[0] and minimum[1], "app did not publish a usable minimum"
    shrunk = grow_window(ui, app.win, minimum[0] // 2, minimum[1] // 2)
    app.wait_screen("PAUSE")
    assert shrunk.w >= minimum[0] and shrunk.h >= minimum[1], (minimum, shrunk)
    assert app.global_word("tf_w") >= 480 and app.global_word("tf_h") >= 128
    assert app.state()["pos"] == before["pos"]
    app.shot(directory, "window-minimum")
    # Essential controls must remain hit-testable at the actual clamped size.
    app.click_action(L["BT_A_PAUSE"])
    app.wait_screen("PLAY")
    app.key(app.state()["passage"][app.state()["pos"]], True)
    app.press("F4").wait_screen("PAUSE")
    enlarged = grow_window(ui, app.win, original[2], original[3])
    assert [enlarged.x, enlarged.y, enlarged.w, enlarged.h] == original
    app.shot(directory, "window-size-restored")
    app.evidence["window_checks"] = {"minimum_frame": list(minimum),
                                    "actual_minimum": [shrunk.w, shrunk.h],
                                    "restored": original, "focus_requires_resume": True}


def cga_key_cycles(app, character):
    """Physical-key route to first completed CGA VRAM flush, actual XT cycles.

    No pacing cap, no guest patch and no synthetic scoring. FSX pins its
    running region. The card's later raster scanout remains a separate cost.
    """
    m = app.ui.m
    before = app.state()
    base = app.p.seg * 16
    route = base + app.sym["_bt_key_route"]
    flush = base + app.sym["_bt_cga_flush"]
    cell = base + app.sym["bt_cga_cell"]
    stamps = []
    primary = {}

    def hit(mm, record):
        regs = record["regs"]
        if not stamps:
            ascii_code = int.from_bytes(mm.read(regs["ss"] * 16 + regs["sp"] + 2, 2), "little")
            assert ascii_code == ord(character), (ascii_code, character)
            stamps.append(record["cycles"])
            mm.breakpoints([{"type": "exec", "addr": flush}])
        elif len(stamps) == 1:
            stamps.append(record["cycles"])
            returned = int.from_bytes(mm.read(regs["ss"] * 16 + regs["sp"], 2), "little")
            primary["flush_return"] = regs["cs"] * 16 + returned
            mm.breakpoints([{"type": "exec", "addr": cell},
                            {"type": "exec", "addr": primary["flush_return"]}])
        elif record["addr"] == primary["flush_return"]:
            assert "return_cycles" in primary, "flush completed without a changed prompt cell"
            stamps.append(record["cycles"])
            mm.breakpoints([])
        elif record["addr"] == cell:
            row, col = divmod(regs["di"] // 2, 40)
            if row in (5, 6) and 6 <= col < 39:
                primary.update(row=row, col=col, character=chr(regs["ax"] & 255),
                               attribute=regs["ax"] >> 8, entry_cycles=record["cycles"])
                returned = int.from_bytes(mm.read(regs["ss"] * 16 + regs["sp"], 2), "little")
                primary["cell_return"] = regs["cs"] * 16 + returned
                mm.breakpoints([{"type": "exec", "addr": primary["cell_return"]},
                                {"type": "exec", "addr": primary["flush_return"]}])
        else:
            assert record["addr"] == primary["cell_return"]
            primary["return_cycles"] = record["cycles"]
            mm.breakpoints([{"type": "exec", "addr": primary["flush_return"]}])

    with M.bp_trace(m, route, regs=True, on_hit=hit) as trace:
        m.type_text(character)
        trace.until(lambda: len(stamps) == 3, "correct-key route and CGA flush return", limit=20)
    after = app.state()
    assert after["attempts"] == before["attempts"] + 1 and after["correct"] == before["correct"] + 1
    assert after["net"] == before["net"] + 1
    assert primary["character"] == character and primary["attribute"] & 3 == 1, primary
    assert stamps[2] > stamps[1] >= stamps[0]
    assert stamps[1] < primary["return_cycles"] < stamps[2]
    cycles = stamps[2] - stamps[0]
    feedback_cycles = primary["return_cycles"] - stamps[0]
    app.evidence["cga_key_commit"] = {
        "route_to_flush_return_cycles": cycles,
        "route_to_flush_return_ms": round(cycles * 1000 / M.GUEST_HZ, 3),
        "flush_cycles": stamps[2] - stamps[1],
        "route_to_first_prompt_cell_return_cycles": feedback_cycles,
        "route_to_first_prompt_cell_return_ms": round(feedback_cycles * 1000 / M.GUEST_HZ, 3),
        "first_prompt_cell": {k: primary[k] for k in ("row", "col", "character", "attribute")},
        "commit_within_100ms": cycles <= M.GUEST_HZ / 10,
        "scope": "production key-route entry to VRAM; hardware input polling and monitor scanout are separate",
        "scanout": "VRAM committed; raster presentation may add up to one refresh"}


def adapter_probe(app, directory, requested):
    """Short real-key flow on the ACTUAL attached adapter and native latch."""
    L, ui = app.L, app.ui
    app.shot(directory, "adapter-native-home")
    cga = display_state(ui)["kind"] == 2
    if cga and app.store_byte("display"):
        app.press("KeyS").wait_screen("SETTINGS").press("KeyD")
        app.press("Escape").wait_screen("HOME")
    if not cga:
        app.press("F2").wait_screen("LESSONS")
        app.click_action(L["BT_A_LESSON_BASE"])
        app.press("Enter").wait_screen("PLAY")
        first = app.state()["passage"][0]
        app.key("x" if first != "x" else "q", False)
        app.key(first, True)
        app.shot(directory, "adapter-native-input")
    app.press("F6").wait_screen("HOME" if cga else "PAUSE")
    assert app.global_word("bt_full") and ui._word("wm_fs")
    app.shot(directory, "adapter-native-fullscreen")
    if not cga:
        paused = app.state()
        app.press("F4").wait_screen("PLAY")
        assert app.state()["pos"] == paused["pos"] and app.state()["seed"] == paused["seed"]
        app.shot(directory, "adapter-native-fullscreen-play")
        app.press("F4").wait_screen("PAUSE")
    app.press("F6")
    ui._wait(lambda: not app.global_word("bt_full") and not ui._word("wm_fs"), "native mode restored", 45)
    if not cga:
        return
    app.press("KeyS").wait_screen("SETTINGS")
    if not app.store_byte("display"):
        app.press("KeyD")
        ui._wait(lambda: app.store_byte("display") == 1, "CGA color preference selected", 20)
    app.press("Escape").wait_screen("HOME")
    # FSX runs inside the original callback: only its own state/frame can be
    # a witness. Never ask ui_done while that callback owns the bracket.
    ui.press("F6", done=False)
    ui._wait(lambda: app.global_word("bt_in_cga") == 1 and app.global_word("bt_cga_seg") != 0,
             "CGA320 FSX bracket and framebuffer accepted", 30)
    fsi = app.p.read(app.sym["_bt_cga_fsi"], 16)
    assert struct.unpack_from("<HH", fsi, 2) == (320, 200) and fsi[9] == 2, fsi.hex()
    app.shot(directory, "adapter-cga320-home", foreign=True)
    ui.press("F2", done=False)
    app.wait_screen("LESSONS")
    for _ in range(L["BT_LESSONS"]):
        if not app.global_word("bt_selection"):
            break
        ui.press("ArrowUp", done=False)
    assert app.global_word("bt_selection") == 0
    ui.press("Enter", done=False)
    app.wait_screen("PLAY")
    assert app.state()["status"] == L["BT_ACTIVE"] and not app.state()["attempts"]
    target = app.state()["passage"][app.state()["pos"]]
    app.key("x" if target != "x" else "q", False, foreign=True)
    app.shot(directory, "adapter-cga320-error", foreign=True)
    cga_key_cycles(app, target)
    app.finish_passage(foreign=True)
    result = app.state()
    assert result["committed"] and result["errors"] == 1 and result["net"] == result["length"]
    app.shot(directory, "adapter-cga320-result", foreign=True)
    ui.press("F6", done=False)
    ui._wait(lambda: not app.global_word("bt_in_cga"), "CGA FSX returned through OS restoration", 30)
    assert display_state(ui) == {"kind": 2, "w": 640, "h": 200, "bpp": 1}
    assert app.state()["net"] == result["net"]
    app.shot(directory, "adapter-cga640-restored")
    app.evidence["cga320_result"] = result
    app.evidence["cga_claims"] = {"lesson_and_restoration": "PASS", "key_commit_100ms":
        "PASS" if app.evidence["cga_key_commit"]["commit_within_100ms"] else "FAIL"}
    assert app.evidence["cga_key_commit"]["commit_within_100ms"], \
        "CGA event-to-VRAM commit exceeds100ms: " + str(app.evidence["cga_key_commit"])


def smoke(app, directory, negative):
    L, ui, ev = app.L, app.ui, app.evidence
    app.shot(directory, "01-home")
    app.press("KeyN").wait_screen("PROFILE")
    for number in range(4):
        app.click_action(L["BT_A_PROFILE_BASE"] + number)
        name = "QA" + str(number + 1)
        ui.type(name)
        assert app.profile(number).split(b"\0")[0].decode("ascii") == name
    app.click_action(L["BT_A_PROFILE_BASE"])
    app.shot(directory, "01b-four-profiles")
    app.press("Enter").wait_screen("HOME")
    app.press("F2").wait_screen("LESSONS")
    # Select lesson1 deliberately even on a reused store with later Continue.
    app.click_action(L["BT_A_LESSON_BASE"])
    assert app.global_word("bt_selection") == 0
    app.press("Enter").wait_screen("PLAY")
    initial = app.state()
    assert initial["mode"] == L["BT_LESSON"] and initial["lesson"] == 0
    assert not initial["attempts"] and not initial["started"] and not initial["elapsed"]
    passage = initial["passage"]
    assert passage and len(passage) == initial["length"]
    wrong = "x" if passage[0] != "x" else "q"
    after = app.key(wrong, False)
    assert after["errors"] == (0 if negative else 1), "negative control: real wrong input must count once"
    app.shot(directory, "02-wrong-key")
    app.key(passage[0], True)
    app.press("Backspace")
    backed = app.state()
    assert (backed["attempts"], backed["correct"], backed["errors"], backed["net"], backed["streak"]) == (2, 1, 1, 0, 0)
    assert backed["backspaces"] == 1
    app.key(passage[0], True)
    app.finish_passage()
    ui._wait(lambda: app.state()["committed"] == 1, "frozen result committed once", 30)
    result = app.state()
    n = len(passage)
    assert (result["attempts"], result["correct"], result["errors"], result["net"]) == (n + 2, n + 1, 1, n), result
    assert result["accuracy"] == (100 * (n + 1) + (n + 2) // 2) // (n + 2), result
    assert result["seconds"] == result["elapsed"] * 65536 // 1193182
    assert result["wpm"] == (12 * n // result["seconds"] if result["seconds"] else 0)
    assert result["best_streak"] == n and result["status"] == L["BT_FINISHED"]
    profile = app.profile(result["profile"])
    assert profile[L["BTProfile.lesson_done"]] == 1
    assert profile[L["BTProfile.lesson_best"]] >= L["BT_MASTERY"]
    achievements = struct.unpack_from("<H", profile, L["BTProfile.achievements"])[0]
    assert achievements & L["BT_ACH_FIRST"] and achievements & L["BT_ACH_MASTER"]
    if n >= 25:
        assert achievements & L["BT_ACH_25"]
    app.shot(directory, "03-results")
    ev["first_lesson_oracle"] = result
    # A separate authored mixed sentence exercises actual shifted letters and
    # punctuation through the same OS-delivered strict engine.
    app.press("F2").wait_screen("LESSONS")
    app.click_action(L["BT_A_LESSON_BASE"] + 22)
    app.press("Enter").wait_screen("PLAY")
    mixed = app.state()["passage"]
    assert any(c.isupper() for c in mixed) and any(c in ".,!;" for c in mixed)
    for character in mixed[:8]:
        app.key(character, True)
    app.shot(directory, "03c-mixed-native-input")
    app.press("F6").wait_screen("PAUSE")
    app.press("F4").wait_screen("PLAY")
    app.shot(directory, "03d-mixed-fullscreen-input")
    app.press("F6").wait_screen("PAUSE")
    app.press("F4").wait_screen("PLAY")
    app.finish_passage()
    mixed_result = app.state()
    assert (mixed_result["attempts"], mixed_result["correct"], mixed_result["errors"], mixed_result["net"]) == (len(mixed), len(mixed), 0, len(mixed))
    assert mixed_result["accuracy"] == 100 and mixed_result["committed"]
    app.shot(directory, "03b-mixed-shift-punctuation")
    ev["mixed_lesson_oracle"] = mixed_result
    # Return to the beginner lesson before testing Retry and transitions.
    app.press("F2").wait_screen("LESSONS")
    app.click_action(L["BT_A_LESSON_BASE"])
    app.press("Enter").wait_screen("PLAY")
    app.finish_passage()
    app.press("KeyR").wait_screen("PLAY")
    assert app.state()["lesson"] == 0 and not app.state()["attempts"]
    app.key(app.state()["passage"][0], True)
    wait_ticks(ui, 5)
    app.press("F4").wait_screen("PAUSE")
    paused = app.state()
    assert paused["started"] and paused["elapsed"] > 0
    wait_ticks(ui, 8)
    assert app.state()["elapsed"] == paused["elapsed"]
    app.shot(directory, "04-pause")
    app.press("F4").wait_screen("PLAY")
    assert app.state()["status"] == L["BT_ACTIVE"] and app.state()["net"] == paused["net"]
    before = app.state()
    old_rect = next(w for w in ui.windows() if w.i == app.win.i)
    old_geometry = [old_rect.x, old_rect.y, old_rect.w, old_rect.h]
    app.press("F6").wait_screen("PAUSE")
    assert app.global_word("bt_full") == 1
    assert ui._word("wm_fs") != 0
    assert app.state()["pos"] == before["pos"] and app.state()["seed"] == before["seed"]
    app.shot(directory, "05-native-fullscreen")
    app.press("F6")
    ui._wait(lambda: not app.global_word("bt_full") and not ui._word("wm_fs"), "native fullscreen restored", 45)
    restored = next(w for w in ui.windows() if w.i == app.win.i)
    assert [restored.x, restored.y, restored.w, restored.h] == old_geometry
    window_checks(app, directory)
    app.press("KeyX").wait_screen("HOME")
    app.press("F1").wait_screen("HELP").shot(directory, "06-help")
    app.press("Escape").wait_screen("HOME")
    app.press("KeyS").wait_screen("SETTINGS")
    music = app.store_byte("music")
    app.click_action(L["BT_A_MUSIC"])
    ui._wait(lambda: app.store_byte("music") == (music ^ 1), "music setting toggled", 15)
    app.shot(directory, "07-settings")
    app.press("Escape").wait_screen("HOME")
    app.press("KeyR").wait_screen("RECORDS").shot(directory, "08-records")
    app.press("Escape").wait_screen("HOME")
    previous_generation = app.generation()
    app.menu("File", "Save progress")
    ui._wait(lambda: app.generation() != previous_generation and not app.global_word("bt_save_pending")
             and app.text("bt_notice").startswith("Progress saved"),
             "validated installed-path save", 90, snapshot=lambda: app.text("bt_notice"))
    ev["saved_profile_hex"] = app.profile().hex()
    ev["saved_music"] = app.store_byte("music")
    ev["saved_generation"] = app.generation()
    ev["resources"] = app.resource_snapshot()


def challenges(app, directory):
    L, ui = app.L, app.ui
    records = []
    for index, seconds in enumerate((60, 180, 300)):
        app.menu("Lesson", "Challenge", "CHALLENGE")
        app.click_action(L["BT_A_DURATION_BASE"] + index)
        app.click_action(L["BT_A_DIFFICULTY_BASE"])
        assert app.global_word("bt_ch_duration") == index and app.global_word("bt_ch_diff") == 0
        old = app.profile()
        app.press("Enter").wait_screen("PLAY")
        ui._wait(lambda: app.state()["status"] == L["BT_ACTIVE"], "challenge countdown elapsed", 30, snapshot=app.state)
        app.shot(directory, f"challenge-{seconds}-active")
        ui._wait(lambda: app.state()["status"] == L["BT_FINISHED"],
                 f"{seconds}-second challenge expires without input", seconds + 45, snapshot=app.state)
        app.wait_screen("RESULTS")
        result = app.state()
        ticks = (seconds * 1193182 + 65535) // 65536
        assert result["elapsed"] == ticks and result["seconds"] == seconds and result["remaining"] == 0, result
        assert result["ranked"] and result["started"] and result["committed"]
        assert result["attempts"] == result["errors"] == result["correct"] == result["net"] == 0
        assert not result["new_record"]
        assert app.global_word("bta_lasttag") == L["BT_R_EMPTY"], "empty challenge received success praise"
        off = L["BTProfile.record_wpm"]
        end = L["BTProfile.record_accuracy"] + 24
        assert app.profile()[off:end] == old[off:end], "no-input test replaced a record"
        app.shot(directory, f"challenge-{seconds}-result")
        records.append(result)
    # Compare the same1-minute category with one scored key, then a paused run.
    for ranked in (True, False):
        app.menu("Lesson", "Challenge", "CHALLENGE")
        app.click_action(L["BT_A_DURATION_BASE"])
        before = app.profile()
        app.press("Enter").wait_screen("PLAY")
        ui._wait(lambda: app.state()["status"] == L["BT_ACTIVE"], "countdown", 30)
        app.key(app.state()["passage"][0], True)
        if not ranked:
            app.press("F4").wait_screen("PAUSE")
            paused = app.state()
            wait_ticks(ui, 8)
            assert app.state()["elapsed"] == paused["elapsed"] and not paused["ranked"]
            app.press("F4").wait_screen("PLAY")
        ui._wait(lambda: app.state()["status"] == L["BT_FINISHED"], "one-minute comparison test", 105, snapshot=app.state)
        app.wait_screen("RESULTS")
        result = app.state()
        assert bool(result["ranked"]) == ranked and result["accuracy"] == 100
        off = L["BTProfile.record_wpm"]
        end = L["BTProfile.record_accuracy"] + 24
        if not ranked:
            assert not result["new_record"] and app.profile()[off:end] == before[off:end]
        else:
            assert app.pword("record_accuracy") == 100
        app.shot(directory, "challenge-ranked" if ranked else "challenge-unranked")
        records.append(result)
    app.evidence["challenge_results"] = records


def audio_probe(app, directory):
    """Reaction/input probe only; captures do not claim audible verification."""
    app.press("F2").wait_screen("LESSONS")
    app.click_action(app.L["BT_A_LESSON_BASE"])
    app.press("Enter").wait_screen("PLAY")
    if "_bta_spoke" in app.sym:
        app.ui._wait(lambda: ((app.ui._word("ticks") - app.global_word("bta_spoke")) & 65535) >= 219,
                     "reaction cooldown elapsed before error probe", 30)
    for _ in range(3):
        app.key("x", False)
    assert app.state()["error_run"] == 3
    if "_bta_lasttag" in app.sym:
        app.ui._wait(lambda: app.global_word("bta_lasttag") == 2, "three-error roast selected", 30)
    app.shot(directory, "audio-three-errors")
    app.press("F3")
    assert app.global_word("bt_master_sound") == 0
    app.shot(directory, "audio-muted")
    app.press("F3")
    assert app.global_word("bt_master_sound") == 1
    app.press("F4").wait_screen("PAUSE")
    app.evidence["audio_limit"] = "Visual reaction/mute/input probe only; music/effects require separate listening or capture evidence."


def persistence_and_readonly(ui, app, args, symbols, layout, evidence):
    expected, music = app.profile(), app.store_byte("music")
    profiles = [app.profile(i).hex() for i in range(4)]
    ui.close(app.win, limit=90)
    reopened = App(ui, open_package(ui, args.path), symbols, layout, evidence)
    assert [reopened.profile(i).hex() for i in range(4)] == profiles, "save/close/reopen lost profile fields"
    assert reopened.store_byte("music") == music, "saved music preference lost"
    reopened.shot(args.evidence, "09-reopened")
    generation = reopened.generation()
    second = App(ui, open_package(ui, args.path), symbols, layout, evidence)
    assert second.global_word("bt_readonly") == 1, "second instance acquired writer ownership"
    assert second.profile() == expected
    second.menu("File", "Save progress")
    ui._wait(lambda: second.text("bt_notice").startswith("Read-only:"), "second instance save refused", 30)
    assert second.generation() == generation
    second.shot(args.evidence, "10-readonly-instance")
    second.press("F6")
    ui._wait(lambda: second.global_word("bt_full") == 1, "readonly instance fullscreen", 30)
    # Native fullscreen intentionally hides the title/close box. Use the
    # application's advertised Ctrl+Q, then witness actual record teardown.
    ui.m.key("ControlLeft", down=True, up=False)
    try:
        ui.press("KeyQ", limit=90)
    finally:
        ui.m.key("ControlLeft", down=False, up=True)
    ui._wait(lambda: all(w.i != second.win.i for w in ui.windows()),
             "fullscreen Ctrl+Q closes the readonly instance", 90)
    assert not ui._word("wm_fs"), "closing fullscreen instance retained the desktop latch"
    ui.raise_window(reopened.win)
    assert reopened.generation() == generation and reopened.profile() == expected
    ui.close(reopened.win, limit=90)
    # MartyPC holds guest writes only in RAM. Export the live, closed-save
    # disks before terminating this machine, then actually cold-boot them.
    exported = []
    for drive, label in ((0, "system"), (1, "apps")):
        path = args.evidence / ("reboot-" + label + ".img")
        Flush(marty=ui.m).save(drive, str(path))
        exported.append(path)
    drive = ord(args.path[0].upper()) - ord("A") if len(args.path) > 1 and args.path[1] == ":" else 1
    assert drive in (0, 1), "reboot export currently covers the two floppy volumes"
    volume = Volume(exported[drive].read_bytes(), "exported package disk")
    copies = []
    for slot in ("BORATA.BTT", "BORATB.BTT"):
        path = "SYSTEM/APPDATA/" + slot
        if path not in (e.path for e in volume.walk()):
            continue
        raw = volume.read(path)
        assert len(raw) == 2136 and raw[:4] == b"BTT1", (path, len(raw))
        assert struct.unpack_from("<HH", raw, 4) == (1, len(raw))
        a = b = 0
        for i, byte in enumerate(raw):
            a = (a + (0 if i in (18, 19) else byte)) % 255
            b = (b + a) % 255
        assert struct.unpack_from("<H", raw, 18)[0] == a | (b << 8), path
        copies.append({"path": path, "generation": struct.unpack_from("<H", raw, 8)[0],
                       "sha256": hashlib.sha256(raw).hexdigest()})
    assert copies, "no independent host-readable save on exported package volume"
    newest = copies[0]
    for copy in copies[1:]:
        delta = (copy["generation"] - newest["generation"]) & 65535
        if 0 < delta < 32768:
            newest = copy
    evidence["persistence"] = {"close_reopen": "PASS", "single_writer": "PASS",
                               "exported_save_copies": copies, "reboot": "PENDING"}
    return {"profile": expected.hex(), "profiles": profiles, "music": music, "generation": newest["generation"],
            "system": str(exported[0]), "apps": str(exported[1])}


def reboot_verify(args, symbols, layout, evidence, saved):
    previous = os.environ.get("OS88_BOOTCACHE")
    os.environ["OS88_BOOTCACHE"] = "0"  # a fresh machine, not a fork snapshot
    try:
        with os88ui.boot(saved["system"], apps=saved["apps"], machine=args.machine,
                         quick=True, label="boratype-reboot") as ui:
            verify_adapter(ui, args.adapter, evidence)
            app = App(ui, open_package(ui, args.path), symbols, layout, evidence)
            assert [app.profile(i).hex() for i in range(4)] == saved["profiles"], "cold reboot lost progress/statistics"
            assert app.store_byte("music") == saved["music"], "cold reboot lost preference"
            assert app.generation() == saved["generation"], "cold reboot loaded wrong save generation"
            assert not app.global_word("bt_readonly"), "fresh machine retained obsolete writer ownership"
            app.shot(args.evidence, "11-reboot-restored")
            evidence["persistence"]["reboot"] = "PASS"
            ui.close(app.win, limit=90)
    finally:
        if previous is None:
            os.environ.pop("OS88_BOOTCACHE", None)
        else:
            os.environ["OS88_BOOTCACHE"] = previous


def reboot_probe(app, directory):
    """Bounded cold-reboot diagnosis: one real lesson and a saved preference."""
    L = app.L
    app.press("F2").wait_screen("LESSONS")
    app.click_action(L["BT_A_LESSON_BASE"])
    app.press("Enter").wait_screen("PLAY")
    result = app.finish_passage()
    assert result["correct"] == result["net"] == result["length"]
    assert result["attempts"] == result["correct"] and not result["errors"] and result["accuracy"] == 100
    app.shot(directory, "reboot-lesson-complete")
    app.press("Escape").wait_screen("HOME")
    app.press("KeyS").wait_screen("SETTINGS")
    old = app.store_byte("music")
    app.press("KeyM")
    assert app.store_byte("music") == (old ^ 1)
    app.press("Escape").wait_screen("HOME")
    app.evidence["reboot_lesson_result"] = result


def select_instrument(args):
    if args.backend:
        os.environ["OS88_EMU"] = "" if args.backend == "marty" else args.backend
    elif args.adapter.startswith("vesa"):
        os.environ["OS88_EMU"] = "qemu"
    elif args.adapter == "ega":
        os.environ["OS88_EMU"] = "86box"
    backend = M.emulator()
    if args.adapter in ("cga", "herc") and backend == "qemu":
        raise ValueError("QEMU has no actual CGA/Hercules; use the MartyPC card model")
    if args.adapter == "cga" and args.scenario == "adapters" and backend != "marty":
        raise ValueError("the CGA adapter arm measures raw XT cycles and requires MartyPC")
    if args.adapter == "ega" and backend != "86box":
        raise ValueError("actual EGA350 requires the automation-capable 86Box card model")
    if args.adapter.startswith("vesa") and backend != "qemu":
        raise ValueError("VESA display QA uses QEMU's actual VBE and VESA.DRV")
    if not args.machine:
        args.machine = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla",
                        "ega": "os8088_xt_ega_640k"}.get(args.adapter, "os8088_xt_vga")
    return backend


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--machine", help="defaults to the actual requested adapter's machine")
    parser.add_argument("--backend", choices=("marty", "qemu", "86box"))
    parser.add_argument("--adapter", choices=("auto", "vga", "cga", "herc", "ega", "vesa800", "vesa1024"), default="auto")
    parser.add_argument("--evidence", type=Path, default=ROOT / "build/boratype-target")
    parser.add_argument("--scenario", choices=("smoke", "challenges", "audio", "adapters", "windows", "reboot"), default="smoke")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--apps", type=Path)
    parser.add_argument("--app-build", type=Path, help="mapped Boratype artifacts; defaults to the selected build root")
    parser.add_argument("--path", default="B:/BORATYPE.O88", help="installed guest path to open with os88ui")
    parser.add_argument("--negative", action="store_true", help="intentional wrong-count assertion; smoke must fail")
    parser.add_argument("--host-check", action="store_true", help="derive/check offsets only; never boot a guest")
    args = parser.parse_args()
    backend = select_instrument(args)
    layout = target_layout()
    if args.host_check:
        print(json.dumps(layout, indent=2, sort_keys=True))
        print("boratype_target: host offsets checked; guest NOT RUN")
        return
    mega = bool(os.environ.get("OS88_MEGA"))
    kernel_build = Path(os88build.mega_build()) / "megak" if mega else Path(os88sym.kernel_image()).parent
    build = (args.app_build or (Path(os88build.mega_build()) if mega else kernel_build)).resolve()
    args.evidence = args.evidence.resolve()
    args.evidence.mkdir(parents=True, exist_ok=True)
    args.apps = (args.apps or build / "boratype360.img").resolve()
    if mega and args.image is None:
        args.image = Path(os88build.mega_build()) / "mega.img"
    evidence = {"status": "RUNNING", "scenario": args.scenario, "machine": args.machine,
                "adapter": args.adapter, "backend": backend,
                "layout": layout, "source": str(SRC), "build": str(kernel_build), "app_build": str(build),
                "kernel": "mega" if mega else "real-mode",
                "visual_inspection": "required; screenshots alone do not pass appearance claims"}
    started = time.monotonic()
    # A killed emulator must leave RUNNING, never a previous run's PASS.
    (args.evidence / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
    ui = app = saved = None
    try:
        args.image = Path(prepare_driver_disk(args, build)).resolve()
        evidence["image_sha256"] = digest(args.image)
        evidence["apps_sha256"] = digest(args.apps)
        symbols = app_symbols(build)
        evidence["app_bin_sha256"] = digest(build / "boratype.bin")
        evidence["app_package_sha256"] = digest(build / "boratype.o88")
        evidence["app_source_tree"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD:apps/boratype"], cwd=SRC, text=True).strip()
        # This disk's kernel must be the exact file os88sym reassembles/maps.
        # The disk carries the KZ-wrapped shipped SYS; os88sym verifies the
        # assembled BIN separately. Comparing those two formats is invalid.
        assert Fat12(args.image).read("KERNEL.SYS") == (kernel_build / "kernel.sys").read_bytes(), \
            "boot disk kernel differs from the verified symbol map"
        if args.path.upper() == "B:/BORATYPE.O88":
            assert Fat12(args.apps).read("BORATYPE.O88") == (build / "boratype.o88").read_bytes(), \
                "software disk contains a stale package"
        os88sym.syms()  # byte-identical source/kernel check before launch
        evidence["kernel_bin_sha256"] = digest(kernel_build / "kernel.bin")
        if mega:
            evidence["kernel_map_sha256"] = digest(kernel_build / "kernel.map")
        evidence["system_cfg"] = config_records(args.image)
        if args.adapter.startswith("vesa"):
            records = evidence["system_cfg"]
            assert "DW" in records and int.from_bytes(bytes(records["DW"]["data"]), "little") & (1 << 9), \
                "real driver disk did not request VESA.DRV"
            assert Fat12(args.image).read("VESA.DRV"), "real driver disk lacks VESA.DRV"
            if args.adapter == "vesa800":
                assert records.get("VM", {}).get("data", [])[-1:] == [0x8A], \
                    "800x600x8 SYSTEM.CFG class was not requested"
        with os88ui.boot(str(args.image), apps=str(args.apps), machine=args.machine, quick=True) as ui:
            try:
                if mega:
                    evidence["protected_mode"] = bool(ui.m.protected())
                    assert evidence["protected_mode"], "Mega probe did not enter protected mode"
                verify_adapter(ui, args.adapter, evidence)
                ui.open_drive("B")
                app = App(ui, open_package(ui, args.path), symbols, layout, evidence)
                evidence["start_cycles"] = ui.m.status()["cycles"]
                evidence["resources_launch"] = app.resource_snapshot()
                if args.scenario == "smoke":
                    smoke(app, args.evidence, args.negative)
                    saved = persistence_and_readonly(ui, app, args, symbols, layout, evidence)
                    app = None
                elif args.scenario == "challenges":
                    challenges(app, args.evidence)
                elif args.scenario == "adapters":
                    adapter_probe(app, args.evidence, args.adapter)
                elif args.scenario == "windows":
                    window_checks(app, args.evidence)
                elif args.scenario == "reboot":
                    reboot_probe(app, args.evidence)
                    saved = persistence_and_readonly(ui, app, args, symbols, layout, evidence)
                    app = None
                else:
                    audio_probe(app, args.evidence)
                evidence["end_cycles"] = ui.m.status()["cycles"]
                evidence["status"] = "PASS"
            except BaseException:
                evidence["status"] = "FAIL"
                if app is not None:
                    try:
                        app.shot(args.evidence, "FAIL-current-guest", settled=False)
                    except Exception as capture_error:
                        evidence["capture_error"] = str(capture_error)
                    try:
                        evidence["failure_state"] = app.state()
                    except Exception as state_error:
                        evidence["state_read_error"] = str(state_error)
                raise
        if saved is not None:
            reboot_verify(args, symbols, layout, evidence, saved)
    except BaseException as error:
        evidence["status"] = "FAIL"
        evidence["error"] = repr(error)
        evidence["traceback"] = traceback.format_exc()
        raise
    finally:
        evidence["host_seconds"] = round(time.monotonic() - started, 3)
        (args.evidence / "report.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence, indent=2), flush=True)
    print("boratype_target: " + args.scenario + " actual guest assertions PASS; inspect captured pictures")


if __name__ == "__main__":
    main()
