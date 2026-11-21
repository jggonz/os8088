#!/usr/bin/env python3
"""Measure installed Boratype on a 4.77 MHz XT, using actual keyboard input.

The 100 ms assertion is queued key -> first completed prompt blit / wrong-key
badge, not whole callback or CRT scanout. Complete callback and periodic work
are reported separately. Rapid input is queued at each preceding callback
return, without UI drain; the report separates input consumption from eventual
UI quiescence. No guest state is written by this row.

--negative intentionally asks a correct F for a wrong-key ! badge and must
fail: it proves that the observer attributes the actual output, not any call.
--relocation adds the public HEAPFRAG compaction scenario (requires HEAPFRAG
and CHELLO fixtures). No build or kernel modification is performed here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[1]
SRC = Path(os.environ.get("OS88_SRC", ROOT)).resolve()
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]
import boratype_target as T
import os88build
import os88marty as M
import os88sym
import os88ui
from os88fat import Fat12

HZ = M.GUEST_HZ
OUTPUTS = ("_bx_out", "_os88_gfx_fill", "_os88_gfx_frame", "_os88_gfx_hline")
SOURCES = tuple(sorted((SRC / "apps/boratype").rglob("*"))) + tuple(
    SRC / p for p in ("apps/breadxt/draw.inc", "apps/cc/gk.inc", "apps/cc/gkmus.inc",
                     "apps/cc/crt0.asm", "apps/cc/os88thunk.asm", "apps/os88api.inc"))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def ms(cycles):
    return round(cycles * 1000 / HZ, 3)


def source_manifest():
    return {str(p.relative_to(SRC)): digest(p) for p in SOURCES if p.is_file()}


def preflight(build, image, apps):
    files = {"kernel_bin": build / "kernel.bin", "kernel_sys": build / "kernel.sys",
             "native_bin": build / "boratype.bin", "package": build / "boratype.o88",
             "system_image": image, "software_image": apps,
             "generated_app_assembly": build / "boratype.gen.asm"}
    if (build / "kernel.kz.json").is_file(): files["kernel_pack_metadata"] = build / "kernel.kz.json"
    hashes = {name: digest(path) for name, path in files.items()}
    assert Fat12(image).read("KERNEL.SYS") == files["kernel_sys"].read_bytes(), "mismatched system disk"
    assert Fat12(apps).read("BORATYPE.O88") == files["package"].read_bytes(), "stale installed package"
    symbols = T.app_symbols(build)  # NASM output must equal this exact BIN.
    os88sym.syms()                 # Same proof for the kernel map.
    assert hashes == {name: digest(path) for name, path in files.items()}, "artifacts changed during preflight"
    for name in ("_bt_key", "_bt_face_paint", "_bt_pulse", "_os88_onkey") + OUTPUTS:
        assert name in symbols, "missing measured production entry " + name
    for name in ("_bt_vplay", "_bt_vtick", "_bt_audio_reload"):
        assert name not in symbols, "removed PCM clip path is present: " + name
    return symbols, files, hashes


class Guard:
    """Read shipping UI canary and actual sampled SP; never fill its stack.

    Task0 has no shipping sentinel fill (stk0water.py explains why). These
    samples are an observed lower bound on maximum use, not global high-water.
    """
    def __init__(self, ui):
        self.ui = ui
        self.eq = os88sym.equates()
        self.bottom = self.eq["STK0_BOT"]
        self.top = self.eq["STK0_TOP"] + 2
        self.low = self.eq["LOW_SEG"]
        self.samples = []

    def sample(self, where):
        m = self.ui.m
        regs = m.regs()
        guard = int.from_bytes(m.read(self.low * 16 + self.bottom, 2), "little")
        assert guard == self.eq["SCH_MAGIC"], ("UI stack guard overwritten", where, guard)
        cur = m.read(self.ui._S("sch_cur"), 1)[0]
        assert cur == 0 and regs["ss"] == self.low, ("probe is not on UI task", where, cur, regs)
        assert self.bottom + 2 <= regs["sp"] <= self.top, ("UI SP outside guarded slice", regs)
        self.samples.append({"where": where, "cycles": m.status()["cycles"], "sp": regs["sp"],
                             "used_bytes": self.top - regs["sp"], "cs": regs["cs"], "ip": regs["ip"]})

    def report(self):
        assert self.samples, "empty stack sample set"
        return {"guard": hex(self.eq["SCH_MAGIC"]), "guard_intact": True,
                "stack_bytes": self.eq["STK0_SIZE"], "sample_count": len(self.samples),
                "maximum_observed_bytes": max(s["used_bytes"] for s in self.samples),
                "minimum_observed_sp": min(s["sp"] for s in self.samples),
                "global_high_water": "NOT MEASURED: task0 is not sentinel-filled; no guest writes",
                "deepest_samples": sorted(self.samples, key=lambda s: -s["used_bytes"])[:12]}


class Probe:
    def __init__(self, app, guard):
        self.app, self.ui, self.m, self.S, self.guard = app, app.ui, app.ui.m, app.sym, guard
        self.cs = app.p.seg
        self.observations = []
        self.periodic_outputs = OUTPUTS + tuple(n for n in ("_bt_anim_planar",) if n in self.S)

    def arm(self, *offsets):
        self.m.bp_exec(*(self.cs * 16 + o for o in offsets))

    def arm_queue(self, extra=None):
        points = [self.cs * 16 + self.S[n] for n in ("_os88_onkey", "_bt_pulse")]
        points += [self.ui._S(n) for n in ("kbd_track", "ui_task.typed")]
        if extra is not None: points.append(self.cs * 16 + extra)
        self.m.bp_exec(*points)

    def stop(self, what, sample=True):
        assert self.m.wait_stop(20, guest=60), "no production breakpoint: " + what
        if sample: self.guard.sample(what)
        return self.m.regs()

    def return_offset(self, regs):
        return int.from_bytes(self.m.read(regs["ss"] * 16 + regs["sp"], 2), "little")

    def sound(self):
        assert self.app.store_byte("music") and self.app.store_byte("effects")
        assert self.app.global_word("bt_master_sound")
        on = self.app.p.byte(self.S["gkm_on"])
        route = self.app.p.byte(self.S["gkm_rt"])
        song = self.app.p.word(self.S["gkm_cur"])
        assert on & 3 == 3 and route in (1, 2) and song > 0, ("music/effects not playing", on, route, song)
        return {"enabled_bits": on, "route": route, "song_plus_one": song,
                "audible_capture": "not claimed by this cycle probe"}

    def queued_context(self):
        return {"registers": self.m.regs(),
                "fields": {name: self.app.global_word(name) for name in
                           ("tf_statpend", "tf_hintpending", "tf_footpending", "tf_cosneeded", "tf_capneeded",
                            "tf_sx", "tf_sy", "tf_sw", "tf_sh")},
                "band": {name: self.app.p.word(self.S[name]) for name in
                         ("bx_x0", "bx_y0", "bx_w", "bx_h")},
                "mirror": struct.unpack("<3h", self.app.p.read(self.S["_tf_dirtyfirst"], 6))}

    def observe_callback(self, character, correct, origin, limit_ms, negative=False, context=None):
        queued_work = []
        pulse_start, pulse_ret = None, None
        for _ in range(32):
            regs = self.stop("queued input dispatch", sample=False)
            flat = regs["cs"] * 16 + regs["ip"]
            here = self.m.status()["cycles"]
            if flat == self.cs * 16 + self.S["_os88_onkey"]:
                self.guard.sample("os88_onkey entry")
                break
            if flat == self.cs * 16 + self.S["_bt_pulse"]:
                self.guard.sample("bt_pulse before input")
                assert pulse_start is None, "nested periodic call"
                pulse_start, pulse_ret = here, self.return_offset(regs)
                queued_work.append({"kind": "bt_pulse entry", "cycles": here})
            elif pulse_ret is not None and flat == self.cs * 16 + pulse_ret:
                self.guard.sample("bt_pulse return before input")
                elapsed = here - pulse_start
                queued_work.append({"kind": "bt_pulse return", "cycles": here,
                                    "cost_cycles": elapsed, "cost_ms": ms(elapsed)})
                pulse_start, pulse_ret = None, None
            elif flat == self.ui._S("kbd_track"):
                queued_work.append({"kind": "IRQ1 scancode", "cycles": here, "scan": regs["ax"] & 255})
            else:
                assert flat == self.ui._S("ui_task.typed"), ("unexpected queue stop", regs)
                queued_work.append({"kind": "BIOS key pending", "cycles": here, "key_word": regs["ax"]})
            self.arm_queue(pulse_ret); self.m.run()
        else:
            raise AssertionError("queued input starved behind32 dispatch markers")
        callback = self.m.status()["cycles"]
        callback_ret = self.return_offset(regs)
        ascii_word = int.from_bytes(self.m.read(regs["ss"] * 16 + regs["sp"] + 2, 2), "little")
        assert regs["ip"] == self.S["_os88_onkey"], ("wrong callback breakpoint", regs)
        assert ascii_word == ord(character), ("keyboard order/ASCII mismatch", character, ascii_word, regs, self.app.state())
        before = self.app.state()
        self.arm(self.S["_bt_key"], callback_ret)
        self.m.run()
        regs = self.stop("bt_key entry")
        assert regs["ip"] == self.S["_bt_key"], "printable callback returned without scoring"
        entered = self.m.status()["cycles"]
        self.arm(self.S["_bt_face_paint"], callback_ret)
        self.m.run()
        regs = self.stop("face entry")
        assert regs["ip"] == self.S["_bt_face_paint"], "scored callback omitted synchronous feedback"
        returned = self.return_offset(regs)
        face = self.m.status()["cycles"]
        first, first_kind = None, None
        expected = "mistake badge" if not correct or negative else "prompt band"
        for _ in range(80):
            self.arm(self.S["_bx_out"], self.S["_os88_gfx_fill"], returned)
            self.m.run()
            regs = self.stop("output or face return")
            if regs["ip"] == returned:
                break
            is_band = regs["ip"] == self.S["_bx_out"]
            matches = is_band and expected == "prompt band"
            if not is_band:
                # The first stroke is a four-pixel stem. The final, separate
                # dot commits the complete exclamation mark on the wrong key.
                rect = struct.unpack("<4H", self.m.read(regs["ss"]*16+regs["sp"]+2,8))
                dot_x = self.app.global_word("tf_kx") + self.app.global_word("tf_kww") - 7
                dot_y = self.app.global_word("tf_ky0") + 10
                matches = expected == "mistake badge" and rect == (dot_x,dot_y,dot_x,dot_y)
            ret = self.return_offset(regs)
            self.arm(ret)
            self.m.run()
            self.stop("completed output")
            if matches and first is None:
                first, first_kind = self.m.status()["cycles"], expected
        else:
            raise AssertionError("face never returned after80 output entries")
        face_done = self.m.status()["cycles"]
        assert first is not None, "expected feedback was never emitted: " + expected
        self.arm(callback_ret)
        self.m.run()
        self.stop("complete os88_onkey callback")
        end = self.m.status()["cycles"]
        after = self.app.state()
        assert after["attempts"] == before["attempts"] + 1, (before, after)
        assert after["correct"] == before["correct"] + int(correct), (before, after)
        assert after["errors"] == before["errors"] + int(not correct), (before, after)
        assert after["pos"] == before["pos"] + int(correct), (before, after)
        base = origin
        result = {"character": character, "correct": correct, "feedback": first_kind,
                  "feedback_origin": "queued hardware key",
                  "feedback_cycles": first - base, "feedback_ms": ms(first - base),
                  "queued_feedback_cycles": first - origin, "queued_feedback_ms": ms(first - origin),
                  "callback_cycles": end - callback, "callback_ms": ms(end - callback),
                  "queue_to_callback_cycles": callback - origin, "queue_to_callback_ms": ms(callback - origin),
                  "queued_callback_return_cycles": end - origin, "queued_callback_return_ms": ms(end - origin),
                  "engine_to_face_return_cycles": face_done - entered, "face_cycles": face_done - face,
                  "callback_entry_cycle": callback, "first_output_cycle": first, "callback_return_cycle": end,
                  "dispatch_before_input": queued_work,
                  "queued_context": context,
                  "state": after}
        self.observations.append(result)
        print("feedback " + json.dumps({k: v for k, v in result.items() if k != "state"}), flush=True)
        assert first - base <= int(HZ * limit_ms / 1000), result
        return result

    def key(self, character, correct, limit_ms, negative=False):
        self.m.pause()
        self.sound()
        context = self.queued_context()
        self.arm_queue()
        start = self.m.status()["cycles"]
        self.m.type_text(character)
        self.m.run()
        result = self.observe_callback(character, correct, start, limit_ms, negative=negative, context=context)
        self.arm()
        self.m.run()
        M.ui_done(self.m, "measured Boratype key complete", limit=60)
        return result

    def periodic(self, count, limit_ms):
        records = []
        self.app.evidence["periodic_observations"] = records
        directory = (Path(self.app.evidence["app_build"]) / "BTAN0.BIN").read_bytes()
        complete = {}
        for frame in range(30):
            signatures = tuple(struct.unpack_from("<4H", directory, 16+8*(frame*9+i))[3] for i in range(5))
            complete.setdefault(signatures, []).append(frame)
        displayed = set()
        self.m.pause()
        self.arm(self.S["_bt_pulse"])
        self.m.run()
        for i in range(count):
            regs = self.stop("periodic entry")
            start = self.m.status()["cycles"]
            pending = {name: self.app.global_word(name) for name in
                       ("tf_statpend", "tf_hintpending", "tf_footpending", "tf_cosneeded", "tf_capneeded", "tf_capline")}
            pending["mirror_cell"] = self.app.p.word(self.S["_tf_dirtyfirst"] + 4)
            ret = self.return_offset(regs)
            paints = 0
            rectangles = []
            for _ in range(50):
                self.arm(ret, *(self.S[name] for name in self.periodic_outputs))
                self.m.run()
                regs = self.stop("periodic output/return")
                if regs["ip"] == ret:
                    break
                paints += 1
                if regs["ip"] == self.S.get("_bt_anim_planar"):
                    seg, off, x, y, height = struct.unpack("<5H", self.m.read(regs["ss"]*16+regs["sp"]+2, 10))
                    rectangles.append({"output": "native planar pose", "segment": seg, "offset": off,
                                       "bx_x0": x, "bx_y0": y, "bx_w": 96, "bx_h": height})
                else:
                    rectangles.append({name: self.app.p.word(self.S[name]) for name in
                                       ("bx_x0", "bx_y0", "bx_w", "bx_h")})
            else:
                raise AssertionError("periodic callback did not finish")
            cycles = self.m.status()["cycles"] - start
            assert self.app.global_word("tf_aready"), "XT animation banks refused or failed during painting"
            sig = struct.unpack("<5H", self.app.p.read(self.S["_tf_asig"], 10))
            displayed.update(complete.get(sig, ()))
            records.append({"index": i, "cycles": cycles, "ms": ms(cycles), "output_entries": paints,
                            "output_bands": rectangles,
                            "complete_portrait_frames": complete.get(sig, []), "pending_at_entry": pending})
            assert cycles <= int(HZ * limit_ms / 1000), records[-1]
            if i + 1 < count:
                self.arm(self.S["_bt_pulse"])
                self.m.run()
        # The last sample is already stopped at its caller return. Do not
        # resume into another armed pulse and race pause against that stop.
        self.arm()
        self.m.go()
        quiet = [r["cycles"] for r in records if not r["output_entries"]]
        assert quiet and any(r["output_entries"] for r in records), "must see quiet AND actual periodic drawing"
        assert max(quiet) < 65536 * 4, "quiet work still consumes an entire OS tick"
        assert any(frame in displayed for frame in (1,2,3)), ("an actual closed/closing eye frame must reach the display", displayed)
        return {"displayed_frames": sorted(displayed), "scope": "bt_pulse entry -> caller return; timer rearm excluded",
                "count": len(records), "quiet_min_cycles": min(quiet), "quiet_max_cycles": max(quiet),
                "quiet_max_ms": ms(max(quiet)), "max_cycles": max(r["cycles"] for r in records),
                "max_ms": max(r["ms"] for r in records), "records": records}


def burst(probe, count, limit_ms, sink):
    """Continuous physical input: next key immediately at previous callback return.

    Do not enqueue all make/break pairs into the controller at once. This is
    typing latency, not a claim about the emulator controller FIFO capacity.
    No ui_done/paint drain occurs between these keys.
    """
    assert 1 <= count <= 8
    app, m = probe.app, probe.m
    before = app.state()
    passage, position = before["passage"], before["pos"]
    text, accepted, errors, corrects = "", 0, 0, []
    for i in range(count):
        expected = passage[position]
        good = i % 3 != 0
        corrects.append(good)
        if not good:
            text += "x" if expected != "x" else "z"; errors += 1
        else:
            text += expected; position += 1; accepted += 1
    m.pause(); probe.sound()
    start = m.status()["cycles"]
    callbacks = []
    record = {"queued_text": text, "attempts": count, "accepted": accepted, "errors": errors,
              "callbacks": callbacks, "status": "RUNNING",
              "input_policy": "next real key at preceding callback return; no per-key UI drain"}
    sink.append(record)
    for character, correct in zip(text, corrects):
        context = probe.queued_context()
        probe.arm_queue()
        queued = m.status()["cycles"]
        m.type_text(character); m.run()
        callbacks.append(probe.observe_callback(character, correct, queued, limit_ms, context=context))
    consumed = m.status()["cycles"]
    after = app.state()
    assert after["attempts"] == before["attempts"] + count, (text, before, after)
    assert after["correct"] == before["correct"] + accepted and after["errors"] == before["errors"] + errors
    assert after["pos"] == position, (text, after)
    probe.arm(); m.run(); M.ui_done(m, "rapid Boratype input drawn", limit=60)
    elapsed = m.status()["cycles"] - start
    record.update({"status": "PASS", "consumed_cycles": consumed - start,
                   "consumed_ms": ms(consumed - start), "quiescent_cycles": elapsed,
                   "quiescent_ms": ms(elapsed), "state": after})
    return record


def claims(app):
    eq = os88sym.equates()
    def read(m):
        owner = app.p.seg
        instance = m.read(app.ui._S("wm_owner") + app.win.i, 1)[0]
        raw = bytes(m.read(app.ui._S("mem_tab"), eq["MEM_MAX"] * eq["MC_SIZE"]))
        found = {}
        for n in range(eq["MEM_MAX"]):
            at = n * eq["MC_SIZE"]
            seg, paras, own, _, rloc = struct.unpack_from("<5H", raw, at)
            if seg and own in (owner, instance):
                found[seg] = {"segment": seg, "bytes": paras * 16, "rloc": rloc}
        roles = {"region": owner, "art": app.global_word("bt_art"),
                 "sound": app.global_word("bt_soundpart"), "band": app.global_word("bt_band"),
                 "overlay": app.p.word(app.sym["cc_ovcs"])}
        lengths = {}
        header = app.p.read(app.sym["op_table"], 10)
        assert header[:8] == b"O88PARTP" and header[8] == 8, header
        table = app.p.read(app.sym["op_table"] + 10, header[8] * 8)
        for i, name in enumerate(("art", "sound", "overlay", "animation0", "animation1", "animation2", "animation3", "animation4")):
            _, flags, _, length, fetched_segment = struct.unpack_from("<BBHHH", table, 8*i)
            if i >= 3:
                assert flags & 8, ("animation bank should use the package's lazy loading ABI", name)
                assert 16 < length <= 65000, ("invalid optional animation bank length", name, length)
                if i in (4, 6, 7):
                    assert length == 57616, ("raw bank must hold ten whole planar frames", name, length)
                if not flags & 32:
                    continue
                roles[name] = fetched_segment
            lengths[name] = length
        fetched = [name for name in roles if name.startswith("animation")]
        assert len(fetched) in (0,5), ("partially loaded animation set", fetched)
        result = {}
        for name, seg in roles.items():
            held = [v for base, v in found.items() if base <= seg < base + v["bytes"] // 16]
            assert len(held) == 1 and held[0]["rloc"], ("claim not movable", name, seg, found)
            claim = held[0]
            result[name] = {"segment": seg, "claim_segment": claim["segment"],
                            "claim_bytes": claim["bytes"], "rloc": claim["rloc"]}
            if name in lengths:
                result[name]["part_bytes"] = lengths[name]
                assert (seg - claim["segment"]) * 16 + lengths[name] <= claim["bytes"]
            if name in ("art", "sound") or name.startswith("animation"):
                result[name]["sha256"] = hashlib.sha256(m.read(seg * 16, lengths[name])).hexdigest()
        assert app.global_word("tf_art") == roles["art"], "face art holder stale"
        assert app.global_word("tf_band") == roles["band"], "face band holder stale"
        assert app.p.word(app.sym["bx_art2"]) == roles["sound"], "second art holder stale"
        assert app.global_word("bx_bseg") == roles["band"], "shared band holder stale"
        result["code"] = {name: app.p.read(app.sym[name], 48).hex() for name in ("_bt_key", "_bt_tick")}
        return result
    return M.heap_read(app.ui.m, read, "Boratype movable claim snapshot")


def relocation(args, build, symbols, layout, evidence):
    """Existing CHELLO ceiling/floor holder + HEAPFRAG's public compaction."""
    import cmemmove
    fixture = args.evidence / "relocation-apps.img"
    fixtures = args.fixture_build or build
    for name in ("chello.o88", "heapfrag.o88"):
        assert (fixtures / name).is_file(), "relocation fixture missing: " + str(fixtures / name)
    evidence["fixture_sha256"] = {name: digest(fixtures / name) for name in ("chello.o88", "heapfrag.o88")}
    subprocess.run([sys.executable, str(SRC / "tools/os88disk.py"), "-o", str(fixture), "--size", "360",
                    str(build / "boratype.o88"), str(fixtures / "chello.o88"), str(fixtures / "heapfrag.o88"),
                    "--folder", "SYSTEM/APPDATA"], check=True)
    assert Fat12(fixture).read("BORATYPE.O88") == (build / "boratype.o88").read_bytes()
    with os88ui.boot(str(args.image), apps=str(fixture), machine=args.machine, quick=True) as ui:
        disk = ui.open_drive("B")
        holder = ui.open("CHELLO.O88", win=disk, limit=90)
        app = T.App(ui, ui.open("BORATYPE.O88", win=disk, limit=90), symbols, layout, evidence)
        app.press("Enter").wait_screen("PLAY")
        app.key(app.state()["passage"][0], True)
        app.press("F4").wait_screen("PAUSE")
        helper = ui.open("HEAPFRAG.O88", win=disk, limit=90)
        ui._wait(lambda: cmemmove.heapfrag_ran(ui.m, ui._S), "initial HEAPFRAG public suite", 120)
        before = claims(app)
        score = app.state()
        ui.close(holder, limit=90)  # opens a hole above the regions AND below the bottom-up claims
        ui.raise_window(helper)
        ui.press("Space", limit=120)  # HEAPFRAG posts OSAPI_MEM_COMPACT after returning
        def posted_done():
            seg = M.pkg_seg(ui.m, helper, ui.sym)
            image = int.from_bytes(ui.m.read(seg * 16 + 8, 2), "little")
            bss = bytes(ui.m.read(seg * 16 + image, 188))
            done = int.from_bytes(bss[152:154], "little") == 4
            if done:
                assert bss[148:152] == bytes(4), ("HEAPFRAG region checks failed", bss[148:152])
            return done
        ui._wait(posted_done, "HEAPFRAG public compaction wake completed", 120)
        after = claims(app)
        moved = [name for name in ("region", "art", "sound", "band", "overlay")
                 if before[name]["segment"] != after[name]["segment"]]
        assert "region" in moved and "band" in moved and any(n in moved for n in ("art", "sound", "overlay")), \
            ("relocation did not exercise all ownership families", moved, before, after)
        assert before.keys() == after.keys(), "claim set changed during compaction"
        for name in (n for n in before if "sha256" in before[n]):
            assert before[name]["sha256"] == after[name]["sha256"], "immutable part corrupted: " + name
        assert before["code"] == after["code"], "resident engine code corrupted by move"
        for field in ("attempts", "correct", "errors", "pos", "net"):
            assert app.state()[field] == score[field], ("compaction changed score", field)
        ui.raise_window(app.win)
        app.menu("View", "Settings", "SETTINGS")  # cold overlay, fonts and return path after movement
        app.press("Escape").wait_screen("PAUSE")
        app.press("F4").wait_screen("PLAY")         # sprite/art, band and music/font part after movement
        resumed = app.state()
        app.key(resumed["passage"][resumed["pos"]], True)
        evidence["relocation"] = {"status": "PASS", "moved_roles": moved, "before": before, "after": after,
                                  "mechanism": "CHELLO close, existing HEAPFRAG key -> public MEM_COMPACT post/wake",
                                  "guest_state_writes": False}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", type=Path)
    ap.add_argument("--apps", type=Path)
    ap.add_argument("--evidence", type=Path, default=Path(os88build.at("build/boratype-performance")))
    ap.add_argument("--feedback-ms", type=float, default=100)
    ap.add_argument("--periodic-ms", type=float, default=100)
    ap.add_argument("--periodic-samples", type=int, default=220)
    ap.add_argument("--bursts", type=int, default=3)
    ap.add_argument("--negative", action="store_true")
    ap.add_argument("--relocation", action="store_true")
    ap.add_argument("--relocation-only", action="store_true")
    ap.add_argument("--fixture-build", type=Path)
    args = ap.parse_args()
    assert M.emulator() == "marty", "cycle performance requires MartyPC, not QEMU/86Box"
    build = Path(os88sym.kernel_image()).resolve().parent
    args.image = (args.image or build / "os8088-360.img").resolve()
    args.apps = (args.apps or build / "boratype360.img").resolve()
    args.evidence = args.evidence.resolve(); args.evidence.mkdir(parents=True, exist_ok=True)
    manifest = source_manifest()
    layout = T.target_layout()
    result = {"status": "RUNNING", "machine": args.machine, "hz": HZ, "source": str(SRC),
              "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=SRC, text=True).strip(),
              "build": str(build), "app_build": str(build), "source_sha256": manifest,
              "limits_ms": {"first_feedback": args.feedback_ms, "periodic": args.periodic_ms},
              "guest_state_writes": False, "pcm_clips": "absent",
              "probe_sha256": digest(Path(__file__)), "target_helper_sha256": digest(Path(T.__file__)),
              "feedback_definition": "queued hardware key -> completed first prompt blit or final wrong-key badge stroke",
              "crt_scanout": "not included; output completion is framebuffer completion"}
    started = time.monotonic()
    guard = None
    try:
        symbols, files, hashes = preflight(build, args.image, args.apps)
        result["artifact_sha256"] = hashes
        if not args.relocation_only:
            with os88ui.boot(str(args.image), apps=str(args.apps), machine=args.machine, quick=True) as ui:
                app = T.App(ui, ui.path("B:/BORATYPE.O88", limit=90), symbols, layout, result)
                app.press("F2").wait_screen("LESSONS")
                app.click_action(layout["BT_A_LESSON_BASE"])
                app.press("Enter").wait_screen("PLAY")
                guard = Guard(ui); probe = Probe(app, guard)
                result["observed_keys"] = probe.observations
                result["sound"] = probe.sound()
                reads = ui.m.disk().get("reads")
                result["keys"] = []
                for i, (ch, correct) in enumerate((("f", True), ("z", False), ("x", False), ("c", False), (" ", True), ("j", True))):
                    result["keys"].append(probe.key(ch, correct, args.feedback_ms, negative=args.negative and i == 0))
                assert reads == ui.m.disk().get("reads"), "floppy read occurred in ordinary typing path"
                result["bursts"] = []
                for _ in range(args.bursts):
                    burst(probe, 8, args.feedback_ms, result["bursts"])
                result["periodic"] = probe.periodic(args.periodic_samples, args.periodic_ms)
                result["stack"] = guard.report()
                app.shot(args.evidence, "performance-play")
                capture = args.evidence / "performance-play.png"
                result["capture"] = str(capture)
                result["sound_after"] = probe.sound()
        if args.relocation or args.relocation_only:
            relocation(args, build, symbols, layout, result)
        assert manifest == source_manifest(), "measured source changed during the run"
        assert hashes == {name: digest(path) for name, path in files.items()}, "measured artifacts changed during the run"
        result["status"] = "PASS"
    except BaseException as error:
        result["status"] = "FAIL"; result["error"] = repr(error); result["traceback"] = traceback.format_exc()
        raise
    finally:
        if guard and guard.samples: result["stack"] = guard.report()
        result["host_seconds"] = round(time.monotonic() - started, 3)
        (args.evidence / "report.json").write_text(json.dumps(result, indent=2) + "\n")
        print("boratype_performance: " + result["status"] + ", report " + str(args.evidence / "report.json"), flush=True)


if __name__ == "__main__":
    main()
