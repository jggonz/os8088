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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ethernet

subprocess.run(["make", "browsertest"], check=True)
sys.argv = ["tests/ethernet.py", "--large-kb", "40"]
raise SystemExit(ethernet.main())
