#!/usr/bin/env python3
"""MOUSECHK reads the kernel's 'MO' block by offset - hold those offsets to it.

SPEC.md 126.4, owner decision O1 (2026-11-05): SPEC.md 57.3 forbids a shipped
package to read the debug registry, and MOUSECHK is its one NAMED exception.
The price is this row. `apps/mousechk/moblk.inc` is a copy of the layout that
kernel/mouse.inc publishes (SPEC.md 9.4.2-9.4.4), and nothing in NASM notices
when the two drift: a kernel change that moves `mou_hpst` would republish it in
MOUSECHK as some other quantity, on a disk in the field, on a machine with no
debugger. So this reads the kernel's own map and the kernel image the build
just made and fails `make` until MOUSECHK is changed in the same commit.

What it holds, all against build/kernel.map (and, when `make mega` has built
it, build/megak/kernel.map - the same mouse.inc, a different kernel):

  1. every MOS_*, MOP_*, MOC_* constant equals the distance between the real
     labels (mou_run, mou_p2, mouse_x) and its member's label;
  2. MOS_SIZE and MOP_SIZE are one past the last member moblk.inc names (the
     kernel may APPEND to either span - SPEC.md 9.4.2's rule - and the package
     reads only what it names; the kernel's own %errors hold the members);
  3. the descriptor words MOB_* are what mou_dbg_blk's words hold - read from
     the image, since a label cannot say what a `dw` points at;
  4. the 'ID' block (SPEC.md 57.6) MOUSECHK checks itself against: the three
     words in build/mousechk_kid.inc equal the lengths worked out from the map
     by hand (kernel_text_end, cold_end - COLD_VSTART, boot2_entry), so the
     generator and the kernel cannot disagree about what 'ID' is.

Every check has a negative control: break one constant in moblk.inc and the row
goes red, naming the member (tests/unit/t_mousechk.py is run with
OS88_MOBLK=<path> to point it at a different copy).
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(
    __file__))))
MOBLK = os.environ.get("OS88_MOBLK") or os.path.join(
    ROOT, "apps", "mousechk", "moblk.inc")


def moblk():
    """{name: value} for every `NAME equ number` in moblk.inc."""
    out = {}
    for line in open(MOBLK):
        m = re.match(r"^([A-Z0-9_]+)\s+equ\s+(0x[0-9A-Fa-f]+|\d+)", line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def parse_map(path):
    """-> ({name: (real, virtual)}, {equate: value}). nasm's [map all]."""
    syms, equs = {}, {}
    insyms = False
    for line in open(path):
        s = line.strip()
        if s.startswith("-- Symbols"):
            insyms = True
            continue
        if not insyms:
            continue
        p = s.split()
        if len(p) == 3 and p[2][:1].isalpha():
            try:
                syms[p[2]] = (int(p[0], 16), int(p[1], 16))
            except ValueError:
                pass
        elif len(p) == 2 and p[1][:1].isalpha():
            try:
                equs[p[1]] = int(p[0], 16)
            except ValueError:
                pass
    return syms, equs


FAILS = []


def fail(msg):
    FAILS.append(msg)
    print("  FAIL: " + msg)


def want(tag, name, got, expect):
    if got != expect:
        fail("%s: moblk.inc says %s = %d, the kernel's layout is %d"
             % (tag, name, got, expect))


def check_layout(tag, syms, equs, image, mb):
    v = {n: syms[n][1] for n in syms}

    def need(*names):
        for n in names:
            if n not in v:
                fail("%s: %s is not in the map - renamed? change moblk.inc "
                     "and MOUSECHK with it" % (tag, n))
                return False
        return True

    # 1 and 2: the spans
    span = [("MOS_RUN", "mou_run"), ("MOS_PORT", "mou_port"),
            ("MOS_NEED", "mou_need"), ("MOS_IDN", "mou_idn"),
            ("MOS_IDB0", "mou_idb0"), ("MOS_IDLAST", "mou_idlast"),
            ("MOS_IDENT", "mou_ident"), ("MOS_IDANY", "mou_idany"),
            ("MOS_SEEN", "mou_seen"), ("MOS_HPST", "mou_hpst"),
            ("MOS_HPT", "mou_hpt"), ("MOS_DRAIN", "mou_drain"),
            ("MOS_DSTAMP", "mou_dstamp"), ("MOS_LINE", "mou_line")]
    if need("mou_run", *[s for _, s in span]):
        for const, sym in span:
            if const not in mb:
                fail("%s: moblk.inc has no %s" % (tag, const))
                continue
            want(tag, const, mb[const], v[sym] - v["mou_run"])
        want(tag, "MOS_SIZE", mb.get("MOS_SIZE"), mb["MOS_LINE"] + 1)
    p2 = [("MOP_P2", "mou_p2"), ("MOP_ST", "mou_p2st"), ("MOP_ID", "mou_p2id"),
          ("MOP_PH", "mou_p2ph"), ("MOP_B0", "mou_p2b0"),
          ("MOP_B1", "mou_p2b1"), ("MOP_CMD0", "mou_p2cmd0")]
    if need("mou_p2", *[s for _, s in p2]):
        for const, sym in p2:
            if const not in mb:
                fail("%s: moblk.inc has no %s" % (tag, const))
                continue
            want(tag, const, mb[const], v[sym] - v["mou_p2"])
        want(tag, "MOP_SIZE", mb.get("MOP_SIZE"), mb["MOP_CMD0"] + 1)
    if need("mouse_x", "mouse_y", "mouse_btn"):
        want(tag, "MOC_X", mb.get("MOC_X"), 0)
        want(tag, "MOC_Y", mb.get("MOC_Y"), v["mouse_y"] - v["mouse_x"])
        want(tag, "MOC_BTN", mb.get("MOC_BTN"), v["mouse_btn"] - v["mouse_x"])

    # 3: the descriptor, as the image holds it
    if need("mou_dbg_blk", "mou_bases", "mou_run", "mouse_x", "mou_p2"):
        real = syms["mou_dbg_blk"][0]
        words = struct.unpack_from("<5H", image, real)
        tag_w = 0x4F4D
        if words[0] != tag_w:
            fail("%s: mou_dbg_blk does not lead with 'MO'" % tag)
        for const, i, sym in (("MOB_BASES", 1, "mou_bases"),
                              ("MOB_SPAN", 2, "mou_run"),
                              ("MOB_CUR", 3, "mouse_x"),
                              ("MOB_P2", 4, "mou_p2")):
            if const not in mb:
                fail("%s: moblk.inc has no %s" % (tag, const))
                continue
            if words[i] != v[sym]:
                fail("%s: the descriptor word at +%d is %04X, not &%s (%04X) "
                     "- the block was reordered" % (tag, 2 * i, words[i],
                                                    sym, v[sym]))
            want(tag, const, mb[const], 2 * i)
        want(tag, "MOB_TAG", mb.get("MOB_TAG"), 0)


def check_kid(tag, syms, equs, kidpath):
    """4: the generated fingerprint against the map worked by hand."""
    if not os.path.isfile(kidpath):
        print("  not checked: %s (no %s)" % (tag, os.path.relpath(kidpath,
                                                                  ROOT)))
        return
    kid = {}
    for line in open(kidpath):
        m = re.match(r"^MC_KID_([ABC]) equ 0x([0-9A-Fa-f]+)", line)
        if m:
            kid[m.group(1)] = int(m.group(2), 16)
    try:
        exp = {"A": syms["kernel_text_end"][1],
               "B": syms["cold_end"][1] - equs["COLD_VSTART"],
               "C": syms["boot2_entry"][1]}
    except KeyError as e:
        fail("%s: %s is not in the map - SPEC.md 57.6's 'ID' changed" %
             (tag, e))
        return
    for k in "ABC":
        if kid.get(k) != exp[k]:
            fail("%s: MC_KID_%s is %s in %s, the map works it out as %04X"
                 % (tag, k, "%04X" % kid[k] if k in kid else "missing",
                    os.path.relpath(kidpath, ROOT), exp[k]))


def one(tag, bdir, kidpath, mb):
    mapf = os.path.join(bdir, "kernel.map")
    full = os.path.join(bdir, "kernel-full.bin")
    if not (os.path.isfile(mapf) and os.path.isfile(full)):
        return None
    syms, equs = parse_map(mapf)
    image = open(full, "rb").read()
    before = len(FAILS)
    check_layout(tag, syms, equs, image, mb)
    check_kid(tag, syms, equs, kidpath)
    if len(FAILS) == before:
        print("  ok: %s - moblk.inc matches the kernel's 'MO' layout and the "
              "'ID' words are the map's" % tag)
    return True


def main():
    if os.environ.get("OS88_DEFINES"):
        print("t_mousechk: a knob build (OS88_DEFINES) - not the shipped "
              "layout, nothing to pin")
        return 0
    mb = moblk()
    if not mb:
        print("t_mousechk: FAIL: no constants in %s" % MOBLK)
        return 1
    big = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
    if one("kern_big", big, os.path.join(big, "mousechk_kid.inc"), mb) is None:
        print("t_mousechk: FAIL: no kernel.map and kernel-full.bin in %s - "
              "run make" % big)
        return 1
    mega = os.path.join(big, "megak")
    if one("kern_mega", mega, os.path.join(mega, "apps", "mousechk_kid.inc"),
           mb) is None:
        print("  not checked: kern_mega (no %s - `make mega`)"
              % os.path.relpath(mega, ROOT))
    if FAILS:
        print("t_mousechk: kernel/mouse.inc and apps/mousechk/moblk.inc "
              "disagree. MOUSECHK reads the 'MO' block by offset on a disk "
              "in the field (SPEC.md 57.3's named exception, 126.4), so a "
              "kernel change to the block is a MOUSECHK change in the SAME "
              "commit: update moblk.inc, the sections that decode it, and "
              "SPEC.md 126.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
