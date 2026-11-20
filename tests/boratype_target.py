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
import os88marty as M
import os88sym
import os88ui
from os88fat import Fat12

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
             "BT_S_PROFILE BT_A_START BT_A_LESSON_BASE BT_A_RETRY BT_A_MUSIC BT_A_EFFECTS "
             "BT_A_VOICE BT_A_KEYCLICK BT_A_ROAST BT_A_TEST_MUSIC BT_A_TEST_VOICE "
             "BT_A_DURATION_BASE BT_A_DIFFICULTY_BASE").split()


def target_layout():
    """Ask the actual uint16_t production header; do not hand-copy offsets.

    These three types contain no pointers/int members. Their native layout is
    exactly the host's 1-byte BTByte / 2-byte BTWord layout (alignment <=2).
    Plain C UI globals are read explicitly as two-byte native ints separately.
    """
    types = {"BTSession": SESSION_BYTES + SESSION_WORDS + ["key_attempt", "key_error", "passage"],
             "BTProfile": PROFILE_FIELDS, "BTStore": STORE_FIELDS}
    lines = ['#include <stddef.h>', '#include <stdio.h>', '#define BT_HOST',
             '#include "bt.h"', '#include "bt_face.h"', 'int main(void) {',
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
        native = (Path(os88sym.kernel_image()).parent / "boratype.bin").read_bytes()
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

    def key(self, character, expected_correct):
        before = self.state()
        cycles = self.ui.m.status()["cycles"]
        self.ui.type(character, limit=90)
        self.ui._wait(lambda: self.state()["attempts"] == before["attempts"] + 1,
                      "one printable attempt", 30, snapshot=self.state)
        after = self.state()
        assert after["correct"] == before["correct"] + int(expected_correct), after
        assert after["errors"] == before["errors"] + int(not expected_correct), after
        assert after["pos"] == before["pos"] + int(expected_correct), after
        self.evidence.setdefault("key_to_ui_done_cycles", []).append(self.ui.m.status()["cycles"] - cycles)
        return after

    def shot(self, directory, name):
        self.ui.still()
        w, h, rgb = self.ui.m.fbuf()
        path = directory / (name + ".png")
        M.write_png_rgb(str(path), w, h, rgb)
        self.evidence.setdefault("captures", []).append({"path": str(path), "w": w, "h": h,
                                                        "inspection": "required"})
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


def smoke(app, directory, negative):
    L, ui, ev = app.L, app.ui, app.evidence
    app.shot(directory, "01-home")
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
    ui.type(passage[1:], limit=90)
    app.wait_screen("RESULTS")
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
    ui.type(mixed, limit=90)
    app.wait_screen("RESULTS")
    mixed_result = app.state()
    assert (mixed_result["attempts"], mixed_result["correct"], mixed_result["errors"], mixed_result["net"]) == (len(mixed), len(mixed), 0, len(mixed))
    assert mixed_result["accuracy"] == 100 and mixed_result["committed"]
    app.shot(directory, "03b-mixed-shift-punctuation")
    ev["mixed_lesson_oracle"] = mixed_result
    # Return to the beginner lesson before testing Retry and transitions.
    app.press("F2").wait_screen("LESSONS")
    app.click_action(L["BT_A_LESSON_BASE"])
    app.press("Enter").wait_screen("PLAY")
    ui.type(app.state()["passage"], limit=90)
    app.wait_screen("RESULTS")
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
    app.evidence["audio_limit"] = "Reaction/mute/input probe only; no listening or PCM/FM/speaker capture claim."


def persistence_and_readonly(ui, app, args, symbols, layout, evidence):
    expected, music = app.profile(), app.store_byte("music")
    ui.close(app.win, limit=90)
    reopened = App(ui, ui.path(args.path, limit=90), symbols, layout, evidence)
    assert reopened.profile() == expected, "save/close/reopen lost profile fields"
    assert reopened.store_byte("music") == music, "saved music preference lost"
    reopened.shot(args.evidence, "09-reopened")
    generation = reopened.generation()
    second = App(ui, ui.path(args.path, limit=90), symbols, layout, evidence)
    assert second.global_word("bt_readonly") == 1, "second instance acquired writer ownership"
    assert second.profile() == expected
    second.menu("File", "Save progress")
    ui._wait(lambda: second.text("bt_notice").startswith("Read-only:"), "second instance save refused", 30)
    assert second.generation() == generation
    second.shot(args.evidence, "10-readonly-instance")
    ui.close(second.win, limit=90)
    ui.raise_window(reopened.win)
    assert reopened.generation() == generation and reopened.profile() == expected
    ui.close(reopened.win, limit=90)
    evidence["persistence"] = "PASS save-close-reopen and second-instance writer exclusion; reboot not exercised"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--machine", default="os8088_xt_vga")
    parser.add_argument("--evidence", type=Path, default=ROOT / "build/boratype-target")
    parser.add_argument("--scenario", choices=("smoke", "challenges", "audio"), default="smoke")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--apps", type=Path)
    parser.add_argument("--path", default="B:/BORATYPE.O88", help="installed guest path to open with os88ui")
    parser.add_argument("--negative", action="store_true", help="intentional wrong-count assertion; smoke must fail")
    parser.add_argument("--host-check", action="store_true", help="derive/check offsets only; never boot a guest")
    args = parser.parse_args()
    layout = target_layout()
    if args.host_check:
        print(json.dumps(layout, indent=2, sort_keys=True))
        print("boratype_target: host offsets checked; guest NOT RUN")
        return
    build = Path(os88sym.kernel_image()).parent
    args.image = (args.image or build / "os8088-360.img").resolve()
    args.apps = (args.apps or build / "boratype360.img").resolve()
    args.evidence = args.evidence.resolve()
    args.evidence.mkdir(parents=True, exist_ok=True)
    evidence = {"status": "RUNNING", "scenario": args.scenario, "machine": args.machine,
                "layout": layout, "source": str(SRC), "build": str(build),
                "image_sha256": digest(args.image), "apps_sha256": digest(args.apps),
                "visual_inspection": "required; screenshots alone do not pass appearance claims"}
    started = time.monotonic()
    ui = app = None
    try:
        symbols = app_symbols(build)
        # This disk's kernel must be the exact file os88sym reassembles/maps.
        assert Fat12(args.image).read("KERNEL.SYS") == Path(os88sym.kernel_image()).read_bytes(), \
            "boot disk kernel differs from the verified symbol map"
        if args.path.upper() == "B:/BORATYPE.O88":
            assert Fat12(args.apps).read("BORATYPE.O88") == (build / "boratype.o88").read_bytes(), \
                "software disk contains a stale package"
        os88sym.syms()  # byte-identical source/kernel check before launch
        with os88ui.boot(str(args.image), apps=str(args.apps), machine=args.machine, quick=True) as ui:
            try:
                ui.open_drive("B")
                app = App(ui, ui.path(args.path, limit=90), symbols, layout, evidence)
                evidence["start_cycles"] = ui.m.status()["cycles"]
                if args.scenario == "smoke":
                    smoke(app, args.evidence, args.negative)
                    persistence_and_readonly(ui, app, args, symbols, layout, evidence)
                    app = None
                elif args.scenario == "challenges":
                    challenges(app, args.evidence)
                else:
                    audio_probe(app, args.evidence)
                evidence["end_cycles"] = ui.m.status()["cycles"]
                evidence["status"] = "PASS"
            except BaseException:
                evidence["status"] = "FAIL"
                if app is not None:
                    try:
                        evidence["failure_state"] = app.state()
                        app.shot(args.evidence, "FAIL-current-guest")
                    except Exception as capture_error:
                        evidence["capture_error"] = str(capture_error)
                raise
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
