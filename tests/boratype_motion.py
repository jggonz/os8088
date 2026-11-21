#!/usr/bin/env python3
"""Time a real three-error reaction on an XT and recognize each frame from the VGA pixels."""
import argparse
import hashlib
import json
from pathlib import Path

import boratype_target as target
import os88bbart
import os88btanim
import os88marty as marty
import os88ui
from os88fat import Fat12


def reference_frames():
    palette = [bytes(color) for color in os88bbart.EGA]
    frames = {}
    for name, _, _, pixels in os88btanim.read():
        if name.startswith("big"):
            frame = int(name[3:])
            if 18 <= frame <= 23:
                frames[hashlib.sha256(b"".join(palette[p] for p in pixels)).digest()] = frame
    assert len(frames) == 6, "each roast pose needs its own silhouette"
    return frames


def measure(build, output):
    expected = reference_frames()
    software = build / "boratype360.img"
    assert Fat12(software).read("BORATYPE.O88") == (build / "boratype.o88").read_bytes()
    symbols, layout = target.app_symbols(build), target.target_layout()
    report = {"app_build": str(build), "package_sha256": target.digest(build / "boratype.o88"),
              "guest_hz": marty.GUEST_HZ, "displayed": []}
    with os88ui.boot(str(build / "os8088-360.img"), apps=str(software),
                     machine="os8088_xt_vga", quick=True) as ui:
        app = target.App(ui, ui.path("B:/BORATYPE.O88", limit=120), symbols, layout, report)
        app.press("F2").wait_screen("LESSONS")
        app.click_action(layout["BT_A_LESSON_BASE"])
        app.press("Enter").wait_screen("PLAY")
        ui._wait(lambda: (app.global_word("bta_now") - app.global_word("bta_spoke")) % 65536 > 230,
                 "welcome caption cooldown", 60)
        for key in ("z", "x"):
            app.key(key, False)
        machine = ui.m
        machine.pause()
        x, y = app.global_word("tf_ax"), app.global_word("tf_ay")
        assert (app.global_word("tf_aw"), app.global_word("tf_ah")) == (96, 120)
        start = machine.status()["cycles"]
        machine.type_text("c")
        # Sample at 30 Hz of *guest cycles*. A signature comes only from the
        # completed scanout, independent of the engine's selected pose/cache.
        cycles = start
        budget = 10 * marty.GUEST_HZ
        while cycles - start < budget and len(report["displayed"]) < 6:
            cycles = machine.advance(cycles=round(marty.GUEST_HZ / 30))["cycles"]
            width, height, rgb = machine.fbuf()
            assert 0 <= x <= width-96 and 0 <= y <= height-120
            crop = b"".join(rgb[(row*width+x)*3:(row*width+x+96)*3] for row in range(y, y+120))
            frame = expected.get(hashlib.sha256(crop).digest())
            if frame is not None and (not report["displayed"] or frame != report["displayed"][-1]["frame"]):
                elapsed = round((cycles-start)*1000/marty.GUEST_HZ, 1)
                report["displayed"].append({"frame": frame, "elapsed_ms": elapsed})
                marty.write_png_rgb(str(output / f"roast-{frame}.png"), 96, 120, crop)
        assert app.state()["errors"] == 3 and app.global_word("bta_lasttag") == 2
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app-build", type=Path, default=Path("build"))
    parser.add_argument("--evidence", type=Path, default=Path("build/boratype-final/motion"))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    report = measure(args.app_build.resolve(), args.evidence)
    seen = [p["frame"] for p in report["displayed"]]
    report["pass"] = seen == list(range(18, 24))
    (args.evidence / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    assert report["pass"], f"Incomplete or out-of-order reaction on the VGA display: {report['displayed']}"
    print("boratype_motion: six complete XT poses from actual scanout:", report["displayed"])


if __name__ == "__main__":
    main()
