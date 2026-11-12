"""What a module's mod_tab row says RIGHT NOW (SPEC.md 2.8.3) - one reader
for every row that asks, so none of them reads "MODR_SEG non-zero" as
"loaded" on a kernel that KEEPS a dropped image.

    import modstate
    modstate.state(m, "MOD_CTRL")   # 'none', 'live' or 'kept'
    modstate.live(m, "MOD_CTRL")    # the segment while LIVE, else 0

LIVE is mod_need's own test (kernel/mod.inc's .held, and mod_live): the
row holds a segment AND its first slot names it. KEPT is a held segment
whose slots rest on mod_gone - the drop disarmed them and re-tagged the
claim MEM_P_MOD + 2 x the id, purgeable. NONE is an empty row: never
loaded, freed (kern_small, kern_mega, an image that does not opt in), or
a kept image the heap has since shed.

`m` is an os88marty.Marty; symbols come from m.sym and os88sym's equates,
so a row asks the kernel it booted. Real mode only: on kern_mega a row may
hold a selector, and nothing there is kept.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88sym  # noqa: E402

KERNEL_SEG = 0x0060


def _u16(m, addr):
    return struct.unpack("<H", bytes(m.read(addr, 2)))[0]


def _id(mid):
    return os88sym.equates()[mid] if isinstance(mid, str) else mid


def seg(m, mid):
    """The row's MODR_SEG: 0, or the segment it holds - live or kept."""
    eq = os88sym.equates()
    return _u16(m, m.sym("mod_tab") + _id(mid) * eq["MODR_SIZE"]
                + eq["MODR_SEG"])


def slot_seg(m, mid):
    """The segment the row's FIRST slot names (mod_fpt -> its block)."""
    ks = os88sym.equates().get("KERNEL_SEG", KERNEL_SEG)
    blk = _u16(m, m.sym("mod_fpt") + 2 * _id(mid))
    return _u16(m, (ks << 4) + blk + 2)


def state(m, mid):
    s = seg(m, mid)
    if not s:
        return "none"
    return "live" if slot_seg(m, mid) == s else "kept"


def live(m, mid):
    """The segment while the module is LIVE, 0 otherwise - what every row
    that asked `MODR_SEG != 0` before the cache meant by it."""
    s = seg(m, mid)
    return s if s and slot_seg(m, mid) == s else 0


def kept_tag(mid):
    return os88sym.equates()["MEM_P_MOD"] + 2 * _id(mid)
