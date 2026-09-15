#!/usr/bin/env python3
"""Audit native-lowering coverage for the vendored Speedy BASIC manifest.

This is deliberately an offline frontend check: it needs neither SmallerC nor
an emulator.  Every manifest entry must tokenize, parse, and link.  The report
then distinguishes complete C lowering from a precise CompileError, making the
remaining backend surface visible without treating an unexpected exception as
an ordinary unsupported feature.
"""

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import speedybasic_compile as compiler  # noqa: E402


def read_manifest(path):
    names = []
    for raw in path.read_text(encoding="ascii").splitlines():
        name = raw.strip()
        if name and not name.startswith("#"):
            names.append(name)
    if not names:
        raise ValueError("empty demo manifest: %s" % path)
    if len(names) != len(set(x.upper() for x in names)):
        raise ValueError("duplicate demo name in %s" % path)
    return names


def audit(manifest):
    demo_dir = manifest.parent
    records = []
    for name in read_manifest(manifest):
        path = demo_dir / name
        source = path.read_text(encoding="latin1")
        program = compiler.compile_source(source, str(path))
        record = {
            "name": name,
            "source_bytes": len(source.encode("latin1")),
            "statements": len(program.statements),
            "labels": len(program.labels),
            "procedures": len(program.procedures),
            "data_items": len(program.data),
        }
        try:
            generated = compiler.emit_c(program, path.stem)
            record["native"] = True
            record["generated_c_bytes"] = len(generated.encode("utf-8"))
        except compiler.CompileError as exc:
            record["native"] = False
            record["error_line"] = exc.line
            record["error"] = exc.message
        records.append(record)
    return records


def main(argv=None):
    default = ROOT / "apps" / "speedybasic" / "demos" / "MANIFEST.TXT"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", type=Path, default=default)
    parser.add_argument("--json", action="store_true",
                        help="write the complete machine-readable report")
    parser.add_argument("--require-all", action="store_true",
                        help="fail unless every demo has complete native lowering")
    args = parser.parse_args(argv)
    try:
        records = audit(args.manifest.resolve())
    except (OSError, ValueError, compiler.CompileError) as exc:
        print("speedybasic-manifest: error: %s" % exc, file=sys.stderr)
        return 2

    native = [x for x in records if x["native"]]
    if args.json:
        print(json.dumps({"count": len(records), "native": len(native),
                          "programs": records}, indent=2, sort_keys=True))
    else:
        for item in records:
            if item["native"]:
                status = "native (%d C bytes)" % item["generated_c_bytes"]
            else:
                status = "pending at line %d: %s" % (
                    item["error_line"], item["error"])
            print("%-12s %5d source bytes  %s" %
                  (item["name"], item["source_bytes"], status))
        print("speedybasic-manifest: %d parsed, %d native, %d pending" %
              (len(records), len(native), len(records) - len(native)))
    return 1 if args.require_all and len(native) != len(records) else 0


if __name__ == "__main__":
    raise SystemExit(main())
