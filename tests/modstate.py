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
so a row asks the kernel it booted.

ON kern_mega (SPEC.md 2.8.3.3; `m.bochs`, or $OS88_MEGA for another
backend) a row holds a SELECTOR, and a LIVE slot names the image's CODE
half - MODR_SEG itself for mod_hi's pool pair above 1MB, MODR_SEG - 8 for a
conventional pair in protected mode - so the test there is the KEPT one
mod_need makes: a disarmed slot names mod_gone's segment, COLD_SEG. A kept
image there is a POOL block marked purgeable (MX_FPURGE, its tag in
MX_AUX), not a mem_tab record: kept_owner() reads whichever the kernel has.
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


def mega(m):
    """kern_mega under any backend: Bochs says so, 86Box through $OS88_MEGA"""
    return bool(getattr(m, "bochs", False) or os.environ.get("OS88_MEGA"))


def state(m, mid):
    s = seg(m, mid)
    if not s:
        return "none"
    if mega(m):
        return ("kept" if slot_seg(m, mid) == os88sym.equates()["COLD_SEG"]
                else "live")
    return "live" if slot_seg(m, mid) == s else "kept"


def live(m, mid):
    """The segment while the module is LIVE, 0 otherwise - what every row
    that asked `MODR_SEG != 0` before the cache meant by it."""
    s = seg(m, mid)
    return s if s and state(m, mid) == "live" else 0


MX_BLKSZ, MX_NBLK, MX_FPURGE = 10, 96, 4        # kernel/mega.inc's mx_tab


def pool_block(m, s):
    """kern_mega: the mx_tab entry whose block the selector S is based at,
    as (flags, MX_AUX), or None - the pool's own record of a kept image"""
    lin = m.lin(s, 0)
    xp = os88sym.equates().get("MEGA_XPOOL", 0x100000)
    raw = bytes(m.read(m.tail("mx_tab"), MX_NBLK * MX_BLKSZ))
    for k in range(MX_NBLK):
        e = raw[k * MX_BLKSZ:(k + 1) * MX_BLKSZ]
        off, kb = struct.unpack_from("<HH", e, 0)
        if kb and xp + off * 1024 == lin:
            return e[5], struct.unpack_from("<H", e, 8)[0]
    return None


def kept_owner(m, mid, claims=None):
    """The tag the kept image's memory carries, or None: on kern_mega its
    POOL block's MX_AUX when that block is MX_FPURGE (mega_modkeepf); on
    every other kernel its mem_tab record's owner, out of CLAIMS (a
    heapmap.Map's .claims - the caller has one) when given"""
    s = seg(m, mid)
    if not s:
        return None
    if mega(m):
        b = pool_block(m, s)
        return b[1] if b and b[0] & MX_FPURGE else None
    own = [c.own for c in (claims or ()) if c.seg == s]
    return own[0] if len(own) == 1 else None


def kept_tag(mid):
    return os88sym.equates()["MEM_P_MOD"] + 2 * _id(mid)
