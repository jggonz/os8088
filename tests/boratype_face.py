#!/usr/bin/env python3
"""Production Boratype geometry/cache host checks and a broken Shift map.

All compiles and the deliberate production mutation live in a private tree.
This row does not boot and cannot prove guest pixels or XT latency.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "apps/boratype"


def run(tree, executable, banks):
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I", str(ROOT / "apps/cc"),
                    str(tree / "hosttest/face_test.c"), "-o", str(executable)], check=True)
    return subprocess.run([str(executable)], capture_output=True, text=True,
                          env=dict(os.environ, BT_ANIM_DIR=str(banks)))


def main():
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="boratype-face-") as tmp:
        work = Path(tmp)
        subprocess.run([os.environ.get("PYTHON", "python3"), str(ROOT / "tools/os88btanim.py"), "build",
                        "--out0", str(work / "BTAN0.BIN"), "--out1", str(work / "BTAN1.BIN"),
                        "--out2", str(work / "BTAN2.BIN"), "--out3", str(work / "BTAN3.BIN"),
                        "--out4", str(work / "BTAN4.BIN")], check=True)
        positive = run(SOURCE, work / "face", work)
        assert positive.returncode == 0, positive.stdout + positive.stderr
        print(positive.stdout, end="")
        tree = work / "broken"
        (tree / "hosttest").mkdir(parents=True)
        for name in ("bt_face.c", "bt_menus.c", "bt_face.h", "bt.h", "art.h"):
            shutil.copyfile(SOURCE / name, tree / name)
        shutil.copyfile(SOURCE / "hosttest/face_test.c", tree / "hosttest/face_test.c")
        source = tree / "bt_face.c"
        body = source.read_text()
        needle = "return tf_keys[i][5]<=4?52:51;"
        assert body.count(needle) == 1, "opposite-Shift negative control anchor moved"
        source.write_text(body.replace(needle, "return tf_keys[i][5]<=4?51:52;"))
        broken = run(tree, work / "broken-face", work)
        assert broken.returncode != 0, "face oracle accepted reversed Shift hands"
        assert body.count("tf_aframe=frame;") == 1, "portrait clock mutation anchor moved"
        source.write_text(body.replace("tf_aframe=frame;", "tf_aframe=0;"))
        stopped = run(tree, work / "stopped-face", work)
        assert stopped.returncode != 0, "portrait oracle accepted a frozen animation clock"
    print(f"boratype_face: PASS, reversed Shift and frozen animation mutations rejected ({time.monotonic()-started:.2f}s)")


if __name__ == "__main__":
    main()
