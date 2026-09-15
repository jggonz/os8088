#!/usr/bin/env python3
"""The public Speedy BASIC driver builds a transactional standalone O88."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "tools" / "speedybasic_build.py"
HELLO = ROOT / "apps" / "speedybasic" / "demos" / "HELLO.BAS"


def run(*args, cwd=None):
    return subprocess.run([sys.executable, str(DRIVER), *map(str, args)],
                          cwd=cwd or ROOT, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def check(value, message, output=""):
    if not value:
        raise AssertionError(message + ("\n" + output if output else ""))


def main():
    help_run = run("--help")
    check(help_run.returncode == 0, "--help failed", help_run.stdout)
    for word in ("INPUT.BAS", "OUTPUT.O88", "--name"):
        check(word in help_run.stdout, "--help omitted " + word,
              help_run.stdout)

    with tempfile.TemporaryDirectory(prefix="os88-sbasic-build-") as tmp:
        td = Path(tmp)
        out = td / "HELLO.O88"

        bad = run(HELLO, "-o", out, "--name", "BAD'NAME", cwd=td)
        check(bad.returncode != 0 and not out.exists(),
              "unsafe package name was accepted", bad.stdout)

        out.write_bytes(b"previous-good-output")
        unsupported = run(ROOT / "apps" / "speedybasic" / "demos" /
                          "MINICALC.BAS", "-o", out, "--name", "MINICALC",
                          cwd=td)
        check(unsupported.returncode != 0,
              "unsupported native lowering unexpectedly passed",
              unsupported.stdout)
        check(out.read_bytes() == b"previous-good-output",
              "failed compile replaced the previous output",
              unsupported.stdout)

        built = run(HELLO, "-o", out, "--name", "SBHELLO", cwd=td)
        check(built.returncode == 0, "HELLO build failed", built.stdout)
        data = out.read_bytes()
        check(len(data) >= 96, "package is shorter than header+icon")
        magic, version, flags, link, entry, image, bss = \
            struct.unpack_from("<HBBHHHH", data, 0)
        check(magic == 0x384F and version == 3 and link == 0,
              "invalid O88 identity/header")
        check(data[12:15] == bytes((0xFF, 0xD5, 0xCB)),
              "missing package dispatcher")
        check(data[16:32].split(b"\0", 1)[0] == b"SBHELLO",
              "package name did not reach the O88 header")
        check(entry >= 96 and image + bss <= 0xF000,
              "entry or package budget is invalid")
        check(flags & 1, "compiled package lost its icon")
        check("speedybasic-build: wrote" in built.stdout,
              "driver did not report its result", built.stdout)
        check(not list(td.glob(".speedybasic-build-*")),
              "temporary build directory was retained")

    print("speedybasic build: help, refusal, transaction and HELLO O88 - PASS")


if __name__ == "__main__":
    main()
