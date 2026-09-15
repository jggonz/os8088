#!/usr/bin/env python3
"""Prove that the complete-language fallback constants match SPEEDYVM.RT."""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
from os88map import Syms  # noqa: E402


def main():
    values = {}
    pattern = re.compile(
        r"^#define\s+(SBVM_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|[0-9]+)u?$")
    for line in (ROOT / "apps/speedybasic/compiler/vm.h").read_text().splitlines():
        match = pattern.match(line)
        if match:
            values[match.group(1)] = int(match.group(2), 0)

    image = (ROOT / "build/SPEEDYVM.RT").read_bytes()
    overlay = (ROOT / "build/SPEEDYVM.OVL").read_bytes()
    syms = Syms("apps/speedybasic/speedybasic.asm",
                "build/speedybasicvm.bin", ["apps", "build"],
                defines=("SB_VM_ASM",))
    expected = {
        "SBVM_TEMPLATE_SIZE": len(image),
        "SBVM_TEMPLATE_BSS": int.from_bytes(image[10:12], "little"),
        "SBVM_OVERLAY_SIZE": len(overlay),
        "SBVM_PARTS": image.find(b"O88PARTS"),
        "SBVM_MARKER": syms.sym("_sb_welcome"),
        "SBVM_TITLE": syms.sym("_sb_window_title"),
    }
    failures = ["%s is %r, VM requires %#x" %
                (name, values.get(name), wanted)
                for name, wanted in expected.items()
                if values.get(name) != wanted]
    if len(image) + expected["SBVM_TEMPLATE_BSS"] > 0xF000:
        failures.append("VM image+BSS exceeds the 60K loader limit")
    if (image[:3] != b"O8\x03" or not image[3] & 4
            or image[expected["SBVM_PARTS"] + 8] != 2):
        failures.append("VM is not a two-part O88 v3 image")
    if failures:
        raise SystemExit("VM ABI: FAIL\n" + "\n".join(failures))
    print("VM ABI: %d offsets/header fields match SPEEDYVM.RT; %d-byte runway"
          % (len(expected), 0xF000 - len(image)
             - expected["SBVM_TEMPLATE_BSS"]))


if __name__ == "__main__":
    main()
