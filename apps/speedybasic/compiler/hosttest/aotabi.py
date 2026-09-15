#!/usr/bin/env python3
"""Prove that the in-OS emitter's fixed offsets describe SPEEDYCC.RT."""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
from os88map import Syms  # noqa: E402


def defines(path):
    result = {}
    pattern = re.compile(r"^#define\s+(SBAOT_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|[0-9]+)u?$")
    for line in path.read_text().splitlines():
        match = pattern.match(line)
        if match:
            result[match.group(1)] = int(match.group(2), 0)
    return result


def main():
    header = defines(ROOT / "apps/speedybasic/compiler/aot.h")
    emitter = defines(ROOT / "apps/speedybasic/compiler/aot.c")
    values = dict(header)
    values.update(emitter)
    syms = Syms("apps/speedybasic/compiler/template.asm",
                "build/speedycc.bin", ["apps", "build"])
    expected = {
        "SBAOT_CODE": syms.sym("_sbaot_code"),
        "SBAOT_CODE_SIZE": (syms.sym("_sbaot_code_end")
                            - syms.sym("_sbaot_code")),
        "SBAOT_PC": syms.sym("_sbaot_pc"),
        "SBAOT_TITLE": syms.sym("_sbr_title"),
        "SBAOT_RT_PRINT": syms.sym("_sbr_print_str"),
        "SBAOT_RT_PRINT_NL": syms.sym("_sbr_print_nl"),
        "SBAOT_RT_SCREEN": syms.sym("_sbr_screen"),
        "SBAOT_RT_CLS": syms.sym("_sbr_cls"),
        "SBAOT_RT_COLOR": syms.sym("_sbr_color"),
        "SBAOT_RT_LOCATE": syms.sym("_sbr_locate"),
        "SBAOT_RT_PSET": syms.sym("_sbr_pset"),
        "SBAOT_RT_LINE": syms.sym("_sbr_line"),
    }
    failures = []
    for name, wanted in expected.items():
        if values.get(name) != wanted:
            failures.append("%s is %r, template requires %#x"
                            % (name, values.get(name), wanted))

    package = (ROOT / "build/SPEEDYCC.RT").read_bytes()
    fields = {
        "SBAOT_TEMPLATE_ENTRY": int.from_bytes(package[6:8], "little"),
        "SBAOT_TEMPLATE_SIZE": len(package),
        "SBAOT_TEMPLATE_BSS": int.from_bytes(package[10:12], "little"),
    }
    if package[:4] != b"O8\x03\x01":
        failures.append("SPEEDYCC.RT is not an icon-bearing O88 v3 image")
    if int.from_bytes(package[8:10], "little") != len(package):
        failures.append("SPEEDYCC.RT header image size does not match file")
    for name, wanted in fields.items():
        if values.get(name) != wanted:
            failures.append("%s is %r, package requires %#x"
                            % (name, values.get(name), wanted))
    if failures:
        raise SystemExit("aot ABI: FAIL\n" + "\n".join(failures))
    print("aot ABI: %d offsets/header fields match SPEEDYCC.RT"
          % (len(expected) + len(fields)))


if __name__ == "__main__":
    main()
