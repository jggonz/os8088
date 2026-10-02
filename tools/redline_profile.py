#!/usr/bin/env python3
"""Run REDLINE on the pinned MartyPC PC reference and retain its evidence.

make redlinedisk build/os8088-360.img marty
python3 tools/redline_profile.py [--machine os8088_redline_pc] [--calibrate]

Default uses bundled GLaBIOS. --machine os8088_redline_pc uses a supplied IBM
ROM. Never claims host time or QEMU throughput as a stock-PC cycle reference.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import statistics
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import os88flush as F
import os88geom as G
import os88marty as M
import os88ui
from os88build import at

NAMES = ("rl_ran", "rl_runs", "bl_saved", "rl_results", "rl_convkb", "rl_cpuname", "rl_early", "rl_video",
         "rl_key", "rl_alu", "rl_facts_start", "rl_facts_end", "bl_nrow", "bl_used", "bl_full",
         "rl_div48by32", "bl_m", "rl_detect", "rl_e820", "rl_mapok", "rl_ramkb",
         "rl_nominal", "rl_tscmhz", "rl_signature", "rl_family", "rl_model", "rl_cpuid")


def symbols():
    """Independent assembler symbol tail, verified against the actual image."""
    src = (ROOT / "apps/redline/redline.asm").read_text()
    with tempfile.TemporaryDirectory() as td:
        asm, binary = Path(td) / "map.asm", Path(td) / "map.bin"
        asm.write_text(src + "\n" + "\n".join("dw " + n for n in NAMES))
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", str(ROOT / "apps") + "/",
                        "-I", str(ROOT / "tests") + "/", "-o", str(binary), str(asm)],
                       cwd=ROOT, check=True)
        data = binary.read_bytes()
        image = Path(at(str(ROOT / "build/redline.bin"))).read_bytes()
        assert data[:-2 * len(NAMES)] == image, "symbol map does not describe build/redline.bin"
        return dict(zip(NAMES, struct.unpack("<%dH" % len(NAMES), data[-2 * len(NAMES):])))


class Probe:
    def __init__(self, ui, sym):
        self.ui, self.m, self.sym = ui, ui.m, sym
        w = ui.window("REDLINE")
        self.base = struct.unpack("<H", self.m.read(self.m.sym("wm_wins") +
                                 w.i * G.WIN_SIZE + G.W_SEG, 2))[0] << 4

    def addr(self, name):
        return self.base + self.sym[name]

    def data(self, name, size=1):
        return self.m.read(self.addr(name), size)

    def word(self, name):
        return struct.unpack("<H", self.data(name, 2))[0]

    def counts(self):
        return struct.unpack("<12I", self.data("rl_results", 48))


def key(ui, name):
    ui.m.pause()
    ui.m.key(name)
    ui.m.advance(frames=4)


def wait(ui, predicate, message, seconds=120):
    # Position by guest frames, bounded by guest time; host load cannot change
    # how far the guest runs.  Never sleep to guess when a benchmark is done.
    for _ in range(seconds * 12):
        if predicate():
            return
        ui.m.advance(frames=5)
    raise AssertionError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(machine, out, repeat=True, calibrate=False):
    sym = symbols()
    out.mkdir(parents=True, exist_ok=True)
    with os88ui.boot(str(ROOT / "build/os8088-360.img"),
                     apps=str(ROOT / "build/redline360.img"), machine=machine, why_ibm=("explicit IBM-ROM reference requested"
                     if machine == "os8088_redline_pc" else None)) as ui:
        runtime_config = Path(ui.m.run_dir) / "martypc.toml"
        assert re.search(r"(?m)^turbo\s*=\s*false\s*(?:#.*)?$",
                         runtime_config.read_text()), "reference requires turbo=false"
        ui.open_drive("B")
        ui.open("REDLINE.O88")
        p = Probe(ui, sym)
        ui.m.pause()
        start = ui.m.status()["cycles"]
        key(ui, "KeyR")
        wait(ui, lambda: p.data("rl_ran") == b"\1", "REDLINE run did not complete")
        counts = p.counts()
        assert all(c > 0 for c in counts), ("zero/overflow timing", counts)
        assert p.word("rl_convkb") == 640, "reference must have 640KB BIOS RAM"
        assert p.word("rl_video") == 0x0102, "reference must have CGA at 1bpp"
        assert p.data("rl_early") == b"\0", "reference must identify an 8088"
        assert p.data("bl_full") == b"\0", "report was truncated"
        end = ui.m.status()["cycles"]
        trials = [list(counts)]
        if repeat:
            for _ in range(2):
                generation = p.word("rl_runs")
                key(ui, "KeyR")
                wait(ui, lambda: p.word("rl_runs") != generation,
                     "repeated REDLINE run did not complete")
                again = p.counts()
                assert all(c > 0 for c in again)
                trials.append(list(again))
            spread = [max(t[i] for t in trials) / min(t[i] for t in trials) - 1
                      for i in range(12)]
            assert max(spread) < .05, ("repeat spread over 5%", trials, spread)
            counts = tuple(int(statistics.median(t[i] for t in trials)) for i in range(12))
        key(ui, "KeyS")
        wait(ui, lambda: p.data("bl_saved") == b"\1", "report was not saved", 30)
        ui.m.advance(frames=60)  # finish the save callback and its visible repaint
        report = F.Flush(marty=ui.m).volume(1).read("REDLINE.TXT")
        (out / "REDLINE.TXT").write_bytes(report)
        w, h, rows = ui.m.vram("cga")
        M.write_png(str(out / "redline-cga.png"), w, h, rows)
        bios = ui.m.read(0xFFFF5, 8).decode("ascii", errors="replace")
        image = Path(at(str(ROOT / "build/redline.bin"))).read_bytes()
        # Assert the source table still describes exactly these measured rows.
        table = (ROOT / "apps/redline/redline.asm").read_text().split("rl_table:")[1]
        entries = re.findall(r"^    dw (rl_label_\w+), (rl_\w+), (\d+), 0", table, re.M)
        assert len(entries) == 12
        workloads = [{"label": label, "body": body, "iterations": int(n)}
                     for label, body, n in entries]
        metadata = {
            "schema": 1, "suite": "REDLINE 1.0", "machine": machine,
            "marty_upstream": (ROOT / "tools/martypc/UPSTREAM").read_text().strip(),
            "clock_hz": 315_000_000 / 22 / 3, "turbo": False,
            "cpu": "Intel8088", "conventional_kb": 640, "wait_states": 0,
            "graphics": "CGA 640x200x1 Dynamic", "bios_date": bios,
            "bios_sha256": hashlib.sha256(ui.m.read(0xFE000, 8192)).hexdigest(),
            "emulator_sha256": sha(ROOT / "build/martypc/run/martypc_headless"),
            "kernel_sha256": sha(ROOT / "build/kernel.bin"),
            "measured_image_sha256": sha(ROOT / "build/redline.bin"),
            "config_sha256": sha(ROOT / "tools/martypc/configs/os8088_machines.toml"),
            "runtime_config_sha256": sha(runtime_config),
            "runtime_machines_sha256": sha(Path(ui.m.run_dir) / "configs/machines/ibm5150.toml"),
            "guest_cycles_including_inventory_ui": end - start,
            "workload_code_sha256": hashlib.sha256(image[sym["rl_alu"]:sym["rl_detect"]]).hexdigest(),
            "workloads": workloads,
            "workload_counts": list(counts), "trials": trials,
            "reference_statistic": "median of three complete runs",
            "units": "net PIT counts at 315000000/22/12 Hz; fixed N per workload",
            "limitations": "Emulator reference; no physical hardware run. GLaBIOS if _gla."
        }
        (out / "reference.json").write_text(json.dumps(metadata, indent=2) + "\n")
        if calibrate:
            ref = ROOT / "apps/redline"
            (ref / "reference.json").write_text(json.dumps(metadata, indent=2) + "\n")
            (ref / "baseline.inc").write_text(
                "; Generated from actual MartyPC measurements; SPEC.md 103.3.\n"
                "; Provenance and trials: apps/redline/reference.json.\n"
                "RL_BASE_ADAPTER equ VID_CGA\nrl_baseline:\n" +
                "".join("    dd %d\n" % c for c in counts))
        print(json.dumps(metadata, indent=2), flush=True)
    return metadata


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--machine", choices=("os8088_redline_pc_gla", "os8088_redline_pc"),
                    default="os8088_redline_pc_gla")
    ap.add_argument("--out", type=Path, default=ROOT / "build/redline-profile")
    ap.add_argument("--calibrate", action="store_true")
    args = ap.parse_args()
    run(args.machine, args.out, calibrate=args.calibrate)


if __name__ == "__main__":
    main()
