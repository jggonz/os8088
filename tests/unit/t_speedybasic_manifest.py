#!/usr/bin/env python3
"""The offline manifest audit covers every demo and diagnoses backend gaps."""

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "speedybasic_manifest.py"


def check(value, message, output=""):
    if not value:
        raise AssertionError(message + (("\n" + output) if output else ""))


def run(*args):
    return subprocess.run([sys.executable, str(TOOL), *args], cwd=ROOT,
                          text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)


def main():
    report_run = run("--json")
    check(report_run.returncode == 0, "manifest audit failed",
          report_run.stdout)
    report = json.loads(report_run.stdout)
    programs = report["programs"]
    check(report["count"] == 29 and len(programs) == 29,
          "manifest does not contain all 29 demos")
    check(len({x["name"].upper() for x in programs}) == 29,
          "manifest contains duplicate names")
    check(sum(x["source_bytes"] for x in programs) > 180000,
          "audit did not read the complete sources")
    by_name = {x["name"]: x for x in programs}
    for name in ("HELLO.BAS", "PATTERN.BAS", "CODEDIFF.BAS", "STARS3D.BAS"):
        check(by_name[name]["native"], name + " lost native lowering")
        check(by_name[name]["generated_c_bytes"] > 1000,
              name + " generated an implausibly small translation")
    pending = [x for x in programs if not x["native"]]
    check(pending, "coverage gate should be updated when all demos lower")
    check(all(x.get("error_line", 0) > 0 and x.get("error") for x in pending),
          "pending demo lacks a source diagnostic")

    strict = run("--require-all")
    check(strict.returncode == 1,
          "--require-all accepted incomplete backend coverage", strict.stdout)
    check("29 parsed" in strict.stdout and "pending" in strict.stdout,
          "human report omitted its coverage totals", strict.stdout)
    print("speedybasic manifest: 29 parsed, native set and diagnostics - PASS")


if __name__ == "__main__":
    main()
