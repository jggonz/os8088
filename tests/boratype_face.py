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


def run(tree, executable):
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I", str(ROOT / "apps/cc"),
                    str(tree / "hosttest/face_test.c"), "-o", str(executable)], check=True)
    return subprocess.run([str(executable)], capture_output=True, text=True)


def main():
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="boratype-face-") as tmp:
        work = Path(tmp)
        positive = run(SOURCE, work / "face")
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
        broken = run(tree, work / "broken-face")
        assert broken.returncode != 0, "face oracle accepted reversed Shift hands"
    print(f"boratype_face: PASS, reversed Shift production mutation rejected ({time.monotonic()-started:.2f}s)")


if __name__ == "__main__":
    main()
