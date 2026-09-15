#!/usr/bin/env python3
"""Build a Turbo BASIC source file as a standalone os8088 .O88 package.

    python3 tools/speedybasic_build.py INPUT.BAS -o OUTPUT.O88 --name NAME

The frontend emits C. This driver then applies the repository's pinned
SmallerC compiler, the mandatory 8086 safety gate, the fixed package shim,
NASM, and os88pkg. Intermediates live in a temporary directory beside the
output and a previous output is replaced only after every stage succeeds.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
APPS = ROOT / "apps"
CC_ROOT = ROOT / "build" / "cc" / "SmallerC"
SMLRCC = CC_ROOT / "smlrcc"
CC_INCLUDE = CC_ROOT / "v0100" / "include"
SHIM = APPS / "speedybasic" / "compiler" / "compiled.asm"
NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9 _.-]{0,14}\Z")


class BuildError(Exception):
    pass


def package_name(value):
    """Validate the O88 header name and keep it safe as a NASM define."""
    if not NAME_RE.fullmatch(value):
        raise argparse.ArgumentTypeError(
            "NAME must be 1-15 ASCII letters, digits, spaces, '.', '_' or '-'"
        )
    return value


def run(stage, argv, env=None):
    print("speedybasic-build: " + stage, flush=True)
    try:
        subprocess.run([str(x) for x in argv], cwd=ROOT, env=env, check=True)
    except FileNotFoundError as exc:
        raise BuildError("required command is unavailable: %s" % exc.filename)
    except subprocess.CalledProcessError as exc:
        raise BuildError("%s failed (exit %d)" % (stage, exc.returncode))


def build(source, output, name):
    source = source.resolve()
    output = output.resolve()
    if not source.is_file():
        raise BuildError("input is not a file: %s" % source)
    if output.suffix.lower() != ".o88":
        raise BuildError("output must end in .O88")
    output.parent.mkdir(parents=True, exist_ok=True)

    # CC_PACKAGE obtains exactly this compiler through the same target. Do not
    # silently accept a different SmallerC found on PATH.
    run("prepare pinned SmallerC", ["make", "--no-print-directory",
                                    "cc-toolchain"])
    if not SMLRCC.is_file():
        raise BuildError("cc-toolchain did not produce %s" % SMLRCC)

    with tempfile.TemporaryDirectory(prefix=".speedybasic-build-",
                                     dir=output.parent) as work_name:
        work = Path(work_name)
        generated_c = work / "program.c"
        raw_asm = work / "program.raw.asm"
        gen_asm = work / "program.gen.asm"
        image = work / "program.bin"
        package = work / "program.o88"

        run("compile BASIC to C",
            [sys.executable, TOOLS / "speedybasic_compile.py", source,
             "-o", generated_c, "--name", name])

        env = os.environ.copy()
        env["PATH"] = str(CC_ROOT) + os.pathsep + env.get("PATH", "")
        run("compile C to NASM",
            [SMLRCC, "-tiny", "-S", "-SI", CC_INCLUDE,
             "-I", CC_INCLUDE, "-I", APPS / "cc",
             "-I", APPS / "speedybasic" / "compiler",
             "-I", APPS / "speedybasic", "-I", APPS,
             generated_c, "-o", raw_asm], env=env)

        run("gate generated code for the 8086",
            [sys.executable, TOOLS / "cc8086.py", raw_asm,
             "-o", gen_asm, "--max-frame", "96"])

        nasm = shutil.which("nasm")
        if not nasm:
            raise BuildError("required command is unavailable: nasm")
        run("assemble package image",
            [nasm, "-f", "bin", "-w+error",
             "-I", str(APPS) + os.sep, "-I", str(work) + os.sep,
             "-DSB_COMPILED_NAME='%s'" % name,
             '-DSB_COMPILED_GEN="program.gen.asm"',
             "-o", image, SHIM])

        run("validate and package O88",
            [sys.executable, TOOLS / "os88pkg.py", "--compress-if=lz4",
             image, "-o", package])

        # The temporary directory is beside output so this final replacement
        # is atomic even when the system's /tmp is on another filesystem.
        os.replace(package, output)

    print("speedybasic-build: wrote %s" % output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, metavar="INPUT.BAS")
    parser.add_argument("-o", "--output", required=True, type=Path,
                        metavar="OUTPUT.O88")
    parser.add_argument("--name", required=True, type=package_name,
                        metavar="NAME",
                        help="package/window name (1-15 safe ASCII characters)")
    args = parser.parse_args(argv)
    try:
        build(args.input, args.output, args.name)
    except (BuildError, OSError) as exc:
        print("speedybasic-build: error: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
