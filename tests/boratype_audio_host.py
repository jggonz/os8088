#!/usr/bin/env python3
"""Production reaction/music policy plus actual8086 PCM transport checks.

The OS is mocked here; real guest audio belongs to boratype_audio_target.py.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="boratype-audio-host-") as tmp:
        executable = Path(tmp) / "director"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O1", "-g",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", str(ROOT / "tests/boratype_audio_host.c"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        subprocess.run([sys.executable, str(ROOT / "tests/boratype_audio_check.py")], check=True)
    print(f"boratype_audio_host: PASS ({time.monotonic()-started:.2f}s)")


if __name__ == "__main__":
    main()
