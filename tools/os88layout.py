#!/usr/bin/env python3
"""Where `.text` starts inside kernel.bin (SPEC.md 2.9).

A slot's address USED to be its file offset: the image was `.text` at offset 0,
loaded whole to KERNEL_SEG. Since stage 2 went in front of it (2.9) the file
carries BOOT2_PAD bytes of loader first, so a memory offset from KERNEL_SEG and
a file offset differ by exactly that - and every host-side tool that indexes
the binary by an address has to say which it means.

Read from kernel.asm's own constant rather than assembled, so a fast-tier row
can use it without a nasm run. The Makefile reads the same line with `sed`,
which is the one source of truth SPEC.md 2.9 asks for.
"""
import os
import re

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
_RE = re.compile(r"^BOOT2_SECS\s+equ\s+(\d+)", re.M)


def boot2_pad(root=None):
    """Bytes of stage 2 in front of `.text` in kernel.bin."""
    p = os.path.join(root or ROOT, "kernel", "kernel.asm")
    m = _RE.search(open(p, encoding="utf-8", errors="replace").read())
    if not m:
        raise RuntimeError("kernel.asm has no BOOT2_SECS - see SPEC.md 2.9")
    return int(m.group(1)) * 512


def text_at(addr, root=None):
    """A KERNEL_SEG memory offset -> its file offset in kernel.bin."""
    return addr + boot2_pad(root)
