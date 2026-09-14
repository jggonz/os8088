#!/usr/bin/env python3
"""The Speedy BASIC web samples and the vendored disk corpus agree."""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
from harness import check, eq, done                       # noqa: E402

TOOL = ROOT / "tools" / "speedybasic_samples.py"
VENDOR = ROOT / "apps" / "speedybasic" / "demos"
EXPECTED_DIGEST = "c2e11e2db05f4a4a6edecaf9631ce2e0d35ef8517b446d5df10134a9b62af3b0"


def load_tool():
    spec = importlib.util.spec_from_file_location("speedybasic_samples", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    tool = load_tool()
    rows = tool.read_vendored(VENDOR)
    eq(len(rows), 29, "the vendored corpus contains all 29 web demos",
       "a missing sample would make the port pass its own examples while the "
       "web version still supports one more")
    eq(tool.digest(rows), EXPECTED_DIGEST,
       "the ordered demo corpus has the reviewed digest",
       "names, order and source bytes all reach the disk; pin them together")

    for name, data in rows:
        check(len(name) <= 12 and len(name.rsplit(".", 1)[0]) <= 8,
              "%s is an 8.3 disk name" % name)
        try:
            data.decode("ascii")
            ascii_ok = True
        except UnicodeDecodeError:
            ascii_ok = False
        check(ascii_ok, "%s is ASCII BASIC source" % name)

    sibling = ROOT.parent / "speedybasic" / "src" / "features" / "samples" / "samples.ts"
    if sibling.exists():
        eq(tool.parse_typescript(sibling), rows,
           "the vendored corpus matches the sibling web source byte for byte",
           "the disk demos and the interpreter used to define compatibility "
           "must be the same programs")

    with tempfile.TemporaryDirectory(prefix="os88-speedybasic-") as tmp:
        missing = Path(tmp) / "no-sibling.ts"
        out = Path(tmp) / "out"
        cmd = [sys.executable, str(TOOL), "--vendor", str(VENDOR),
               "--source-ts", str(missing), "-o", str(out)]
        made = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        check(made.returncode == 0, "the extractor works without a sibling checkout",
              "a standalone os8088 clone must build every application disk",
              got=(made.stdout + made.stderr).strip(), want="exit 0")
        actual = [(name, (out / name).read_bytes()) for name, _ in rows]
        eq(actual, rows, "materialized demos are byte-identical to the vendor")
        checked = subprocess.run(cmd + ["--check"], cwd=ROOT,
                                 capture_output=True, text=True)
        check(checked.returncode == 0, "--check accepts a fresh materialization")

    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    check("$(call CC_PACKAGE,speedybasic,speedybasic,SPEEDYBA.OVL)" in makefile,
          "the C package is wired through the standard toolchain",
          "bypassing CC_PACKAGE skips the 8086 instruction gate and package validation")
    check("$(BUILD)/speedyd/SPEEDYBA.O88" in makefile,
          "the descriptive build artifact has an 8.3 disk-facing name",
          "speedybasic.o88 cannot be represented in a FAT short-name directory")
    for image, size in (("speedybasic.img", "1440"),
                        ("speedybasic720.img", "720"),
                        ("speedybasic120.img", "1200"),
                        ("speedybasic360.img", "360")):
        rule = re.search(r"^\$\(BUILD\)/%s:.*?\n((?:\t.*\n)+)" %
                         re.escape(image), makefile, re.M)
        check(rule is not None and ("--size %s" % size) in rule.group(1) and
              "tools/os88disk.py --verify $@" in rule.group(1),
              "%s is wired at the %sKB geometry" % (image, size),
              "application disks ship in all four standard geometries")
    check("SPEEDY/DEMOS=64" in makefile and
          "SPEEDY:apps/speedybasic/README.TXT" in makefile,
          "the disk reserves a writable demo directory and carries its guide")
    check(makefile.count("SPEEDY:$(BUILD)/SPEEDYBA.OVL") >= 2,
          "dedicated and everything disks ship the required overlay",
          "SPEEDYBA.O88 preloads SPEEDYBA.OVL before its first paint")

    done("t_speedybasic_samples")


if __name__ == "__main__":
    main()
