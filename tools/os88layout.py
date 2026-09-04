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


def kernel_text(root=None):
    """kernel.bin as a `.text`-OFFSET-INDEXED image.

    What you want whenever an address that came out of the GUEST - a near
    return address off a stack, a breakpoint, anything KERNEL_SEG-relative -
    is used to index the built binary. The raw file is not that image and has
    not been since SPEC.md 2.9.

    tools/stkwater.py's `is_call_site` is the worked example of getting it
    wrong: given the raw file it recognised 126 of 3,000 real near-call sites
    instead of 3,000, so a stack dump lost almost every "<- call" attribution
    and the handful it kept were bytes that happen to look like a call at the
    wrong offset - which is the exact false confidence that guard exists to
    prevent.
    """
    root = root or ROOT
    with open(os.path.join(root, "build", "kernel.bin"), "rb") as f:
        return f.read()[boot2_pad(root):]


def seg_at(seg, kernel_seg=0x0060, root=None):
    """A runtime SEGMENT -> where that paragraph sits in kernel.bin.

    `text_at`'s answer for a section that is not `.text`. A segment delta is
    NOT a file offset and has not been one since SPEC.md 2.9; getting that
    wrong lands you a whole section early, on plausible-looking bytes, with no
    error - which is why this is here rather than open-coded per caller.
    """
    return boot2_pad(root) + ((seg - kernel_seg) << 4)


def cold_span(root=None):
    """`.cold` as (segment, file offset, length) - all three from the build.

    THREE ROWS GOT THIS WRONG INDEPENDENTLY - tests/dispcold.py,
    tests/dispreboot.py and, in the `.text` form, tests/linefast.py and
    tests/wirefps.py - so it is derived once here. Two traps, and each one
    alone reads as `.cold` being scribbled on from the moment the desktop
    comes up:

      * the segment delta (above): `(COLD_SEG - KERNEL_SEG) << 4` points
        BOOT2_PAD bytes early, into the cold THUNK TABLE - runs of
        `9A offset <cold seg> / C3` that live in `.text` and are what calls
        `.cold`. Comparing those against `.cold` differs in ~98% of bytes.
      * the length: `FAT_SEG - COLD_SEG` is the RUNG the section was rounded
        up into. `.cold` is padded out to fill it and `.ovlw` starts at the
        far end, so the span is `OVLW_START - offset` and not `EOF - offset`.

        **THAT SENTENCE USED TO SAY `.cold` IS THE LAST THING IN kernel.bin
        AND ENDS AT EOF**, which stopped being true when `.ovlw` was
        introduced after it, and the two rows below then failed on every build
        - 37,376 of rung plus 5,215 of `.ovlw` measured as 42,591 of section.
        The failure named the wrong cause too, telling the reader to rebuild a
        build that was current, and it cost a wrong diagnosis before the
        arithmetic was done. A check whose message names the wrong cause is
        worse than one with no message.

    Also: COLD_SEG is asked for, never written down. It has moved twice
    (0x0E00 -> 0x0E20 -> 0x0FC0) and a stale copy reads the wrong RAM.
    """
    import os88sym                        # nasm-backed: not at import time,
                                          # so a fast-tier row keeps boot2_pad
                                          # without paying for a map
    root = root or ROOT
    seg = os88sym.segment_of("fm_layout")            # any .cold symbol will do
    off = seg_at(seg, root=root)
    eq = os88sym.equates()
    rung = (eq["FAT_SEG"] - seg) << 4
    size = eq["OVLW_START"] - off
    have = os.path.getsize(os.path.join(root, "build", "kernel.bin"))
    if not 0 < size <= rung:
        raise RuntimeError(
            ".cold spans %d bytes from file offset %d to OVLW_START, against "
            "a %d-byte rung below FAT_SEG - two equates that derive from the "
            "same section order now disagree, so kernel.asm's layout has "
            "moved and this routine has not" % (size, off, rung))
    if have < off + size:
        raise RuntimeError(
            "build/kernel.bin is %d bytes and .cold's rung ends at %d - the "
            "binary is SHORTER than the map says the section is. A rebuild is "
            "the likely cure; a truncated write is the other one"
            % (have, off + size))
    return seg, off, size
