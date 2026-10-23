#!/usr/bin/env python3
"""A large page grows without spending the Browser's free-heap reserve.

    make browsertest && python3 tests/brmem.py

The Ethernet harness serves deterministic HTML through QEMU's NE2000 path.
At 40 KB it crosses the old source ceiling and forces document growth, then
opens Note Pad while the Browser retains the page. QEMU is used for its NIC;
this asserts behavior and memory bounds, not 8088 timing.
"""
import os
import subprocess
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tests"))
sys.path.insert(0, os.path.join(_ROOT, "tools"))
import brmemory
import ethernet

subprocess.run(["make", "browsertest"], check=True)
os.makedirs("build/brmemory", exist_ok=True)
PAGE = "build/brmemory/mem40k.htm"
brmemory.write_numbered_rows(
    PAGE, 40 * 1024,
    b"<!doctype html><title>Browser 40 KB</title><h1>Browser 40 KB</h1><pre>\n",
    b"row %06d: conventional page data beyond the old source ceiling\n",
    b"\n</pre>")
sys.argv = ["tests/ethernet.py", "--page", PAGE]
raise SystemExit(ethernet.main())
