#!/usr/bin/env python3
"""Production Boratype engine oracle and negative controls (soak row).

No emulator, asset, filesystem image or optional compiler is required. Builds
the production sources with BT_HOST's uint16_t state; the independent oracle
uses 32-bit host arithmetic solely to detect target-word mistakes.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "apps/boratype"


def build_and_run(tree: Path, exe: Path) -> subprocess.CompletedProcess:
    cc = os.environ.get("CC", "cc")
    subprocess.run([cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-DBT_HOST",
                    str(tree / "hosttest/engine.c"), "-o", str(exe)], check=True)
    return subprocess.run([str(exe)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def main() -> None:
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="boratype-host-") as tmp:
        work = Path(tmp)
        result = build_and_run(SOURCE, work / "engine")
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
        print(result.stdout, end="")
        # Each control builds its own disposable production mutation, never
        # edits the checkout, and must fail an assertion in the same oracle.
        mutations = [
            ("bt_core.c", "bt_muldiv(bt_session.net, 12, bt_session.seconds)",
             "bt_muldiv(bt_session.net, 13, bt_session.seconds)", "WPM credit"),
            ("bt_core.c", "bt_session.errors++; bt_session.key_error[key]++;",
             "bt_session.errors++; bt_session.pos++; bt_session.key_error[key]++;", "strict entry"),
            ("bt_codec.c", "sum = bt_checksum(buf, len);",
             "sum = (BTWord)(buf[18] | (buf[19] << 8));", "integrity fence"),
            ("bt_lessons.c", '"f j f j ff jj fj jf fjj jff fj fj jf jf f j fj jf"',
             '"a j f j ff jj fj jf fjj jff fj fj jf jf f j fj jf"', "lesson declaration"),
        ]
        for index, (filename, needle, replacement, label) in enumerate(mutations):
            tree = work / f"mutation-{index}"
            tree.mkdir()
            (tree / "hosttest").mkdir()
            for name in ("bt.h", "bt_core.c", "bt_codec.c", "bt_lessons.c"):
                shutil.copyfile(SOURCE / name, tree / name)
            shutil.copyfile(SOURCE / "hosttest/engine.c", tree / "hosttest/engine.c")
            path = tree / filename
            text = path.read_text()
            assert text.count(needle) == 1, f"negative control anchor moved: {label}"
            path.write_text(text.replace(needle, replacement))
            result = build_and_run(tree, work / f"bad-{index}")
            assert result.returncode, f"negative control failed to detect {label} mutation"
            print(f"boratype_host: negative control detected {label}")
    print(f"boratype_host: PASS ({time.monotonic() - started:.2f}s)")


if __name__ == "__main__":
    main()
