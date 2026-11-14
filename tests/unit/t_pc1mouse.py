#!/usr/bin/env python3
"""PC1MOUSE.DRV must keep its sampled X delta across key-state queries.

    python3 tests/unit/t_pc1mouse.py

`OSAPI_KEY_DOWN` takes the queried scancode in AL. p1_sample has the mouse's
signed X delta in AX at that point, so loading P1_LEFT/P1_RIGHT into AL without
banking AX turns each button poll into a positive mouse movement. The visible
symptom is a pointer that drifts right while the physical mouse is still.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(__file__))
from harness import check, done                                  # noqa: E402


def main():
    path = os.path.join(ROOT, "drivers", "pc1mouse", "pc1mouse.asm")
    with open(path, encoding="latin1") as f:
        src = f.read()
    kpath = os.path.join(ROOT, "kernel", "mouse.inc")
    with open(kpath, encoding="latin1") as f:
        ksrc = f.read()
    m = re.search(r"(?ms)^p1_sample:\s*(.*?)^p1_keyseq:", src)
    check(m is not None, "p1_sample exists", why="the PC1 worker needs a sample path")
    if m is None:
        done("t_pc1mouse")
    body = m.group(1)
    left = body.find("mov al, P1_LEFT")
    right = body.find("mov al, P1_RIGHT")
    calls = [x.start() for x in re.finditer(r"call OSAPI_KEY_DOWN", body)]
    push = body.rfind("push ax", 0, left) if left >= 0 else -1
    restore = body.find("pop ax", calls[-1] + 1) if len(calls) == 2 else -1
    compare = body.find("cmp cl, [p1_buttons]")
    check(left >= 0 and right > left and len(calls) == 2,
          "both PC1 buttons are queried by scancode",
          why="the left/right level must be sampled each worker pass")
    check(push >= 0 and left < calls[0] < right < calls[1] < restore < compare,
          "the X delta is saved across both OSAPI_KEY_DOWN calls",
          got=(push, left, calls, restore, compare),
          want="push AX before the first scancode load; pop AX before report compare",
          why="AL is the key query input and shares AX with the mouse's horizontal delta; "
              "without the bank, the right-button scan 79h becomes a +121 cursor step")
    accel = re.search(r"(?ms)^%macro P1_ACCEL 1\s*(.*?)^%endmacro", src)
    check(accel is not None and "cmp dx, 2" in accel.group(1)
          and "shl %1, 1" in accel.group(1),
          "the DOS threshold-2 acceleration is retained",
          why="the normal driver should speed up larger quadrature deltas as Simone's DOS driver does")
    check(body.count("P1_ACCEL ") == 2,
          "both axes use the DOS acceleration in the normal arm",
          got=body.count("P1_ACCEL "), want=2,
          why="accelerating only one axis makes diagonal movement skew")
    attach = src.split("p1_entry:", 1)[1].split("p1_ready:", 1)[0]
    check("mov al, P1_LEFT" in attach and "call OSAPI_KEY_DOWN" in attach,
          "PC1 attach arms the kernel scancode map before mouse button input",
          why="the PC1's first button press must be visible to the worker")
    scan = re.search(r"(?ms)^%macro KBM_SCAN 1-2\s*(.*?)^%endmacro", ksrc)
    check(scan is not None
          and scan.group(1).find("call kbd_track") < scan.group(1).find("cmp al, KBM_PC1_L")
          < scan.group(1).find("cmp byte [mou_ptr], 0")
          and "%ifnidn %2, mem" not in scan.group(1)
          and "je .pc1eat" in scan.group(1),
          "PC1 filtering runs before KBM_SCAN's chain exits in both scan forms",
          why="the XT uses the shared `mem` expansion in kern_big; neither scan form may bypass the filter")
    kfilter = re.search(r"(?ms)^%ifdef OS88_DRIVERS\s*(.*?)^\.chain:", ksrc)
    check(kfilter is not None
          and "in  al, 0x61" in kfilter.group(1)
          and "or  al, 0x80" in kfilter.group(1)
          and "out 0x61, al" in kfilter.group(1)
          and "out 0x20, al" in kfilter.group(1) and "iret" in kfilter.group(1)
          and "kbm_ivec" not in kfilter.group(1)
          and scan is not None and "[pc1_row+DRVR_SEG]" in scan.group(1)
          and all(x in scan.group(1) for x in ("KBM_PC1_L", "KBM_PC1_M", "KBM_PC1_R")),
          "the kernel consumes PC1 button scans on the XT INT 09h path",
          why="the PC1's 8255 requires a port-61h acknowledge before IRQ1's EOI; its buttons must not reach the BIOS key queue")
    done("t_pc1mouse")


if __name__ == "__main__":
    main()
