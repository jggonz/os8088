#!/usr/bin/env python3
"""One place for the kernel geometry the harness mirrors - and a guard that
fails LOUDLY when the kernel moves under it.

    python3 tools/os88geom.py          # check every mirrored constant, print

WHY THIS EXISTS. Nine scripts had their own copy of the window record's
stride, the desktop's zone pitch and the Disk window's row height, and the
kernel moved four times: WIN_SIZE went 26 -> 28 when SPEC.md 13.7 added
W_ONMOUSEUP, 28 -> 30 for 13.8.2's W_ONDRAG and 30 -> 34 for 13.9's timer
pair - this guard caught the last two on the next run, which is exactly what
it is for - and SPEC.md 26.4's square CGA icon took the zone pitch 60 -> 34
and its width 48 -> 32. Every copy that did not follow went on returning
NUMBERS - a stride that decodes window 1 as garbage and reads it as unused, a
double-click 22px below a 14-row zone that lands on bare desktop - and both
failures look exactly like the FEATURE UNDER TEST being broken:

  * `tools/sucheck.py` and `tools/callfront.py` saw one window where there
    were two, so every "the newest window" pick was the older one;
  * `tests/dispcp.py`'s own `_cp_win` walked the table at 26 while the rest
    of that file used 28, so finding the Control Panel was luck;
  * a session lost an afternoon to "the second Disk window never opens",
    which was this and not the window manager.

The lesson is the tree's own (SPEC.md 22's `fm_hit` discipline): the drawn
thing and the tested thing must come from ONE place. So this module is that
place, and it does two things no per-file copy can.

**It reads the guest wherever the kernel does.** `desk_rows`, `desk_zstep`,
`desk_zh1` and `vid_desk_zx` are live words - the adapter decides them at
boot and SPEC.md 39.11.2's Display page can change them while the machine
runs - so nothing here writes them down. Only assembly-time constants with
no published copy are mirrored.

**And every mirrored constant is CHECKED against the kernel source at
import**, by parsing the `equ`s out of the .inc files. A constant that moves
is then an error with the old and new values in it, on the next run of any
script, rather than a wrong coordinate nobody attributes for a day. That is
the whole point: this file cannot go stale quietly, which is the property the
nine copies lacked.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class GeomError(Exception):
    pass


# --- the mirrored constants -------------------------------------------------
#
# name -> (kernel file, value). The value is what this module uses; the file
# is where `verify` goes to find out whether it is still true. Anything the
# kernel computes at run time is NOT here - see the module docstring.

_MIRROR = {
    # kernel/wm.inc - the window record (SPEC.md 11)
    "WIN_SIZE": ("kernel/wm.inc", 34),
    "MAX_WIN": ("kernel/wm.inc", 12),
    "W_FLAGS": ("kernel/wm.inc", 0),
    "W_X": ("kernel/wm.inc", 2),
    "W_Y": ("kernel/wm.inc", 4),
    "W_W": ("kernel/wm.inc", 6),
    "W_H": ("kernel/wm.inc", 8),
    "W_TITLE": ("kernel/wm.inc", 10),
    "W_MENUS": ("kernel/wm.inc", 18),
    "W_SEG": ("kernel/wm.inc", 22),
    "WF_SIZABLE": ("kernel/wm.inc", 4),
    "WF_FULL": ("kernel/wm.inc", 8),
    "WF_NOSNAP": ("kernel/wm.inc", 16),
    "WF_SAVEU": ("kernel/wm.inc", 32),
    "WF_OWNBG": ("kernel/wm.inc", 64),
    # kernel/instance.inc - the instance record (SPEC.md 29)
    "I_STATE": ("kernel/instance.inc", 0),
    "I_FLAGS": ("kernel/instance.inc", 1),
    "I_KIND": ("kernel/instance.inc", 2),
    "I_TASK": ("kernel/instance.inc", 3),
    "I_WIN": ("kernel/instance.inc", 4),
    "I_SPTR": ("kernel/instance.inc", 6),
    "I_SIZE": ("kernel/instance.inc", 8),
    "I_ICON": ("kernel/instance.inc", 10),
    "I_NAME": ("kernel/instance.inc", 12),
    "I_CYC": ("kernel/instance.inc", 28),
    "I_RECSZ": ("kernel/instance.inc", 32),
    "INST_MAX": ("kernel/instance.inc", 12),
    # kernel/dock.inc - the strip (SPEC.md 30)
    "DOCK_H": ("kernel/dock.inc", 24),
    "DOCK_X0": ("kernel/dock.inc", 8),
    "DOCK_STEP": ("kernel/dock.inc", 28),
    "DOCK_TILE_W": ("kernel/dock.inc", 24),
    "DOCK_TILE_H": ("kernel/dock.inc", 20),
    # kernel/desk.inc - the drive column (SPEC.md 26.1/26.4)
    "DESK_ZY0": ("kernel/desk.inc", 32),
    "DESK_ZW": ("kernel/desk.inc", 32),
    "DESK_COLW": ("kernel/desk.inc", 44),
    "DESK_ZOVER": ("kernel/desk.inc", 2),
    # kernel/disk.inc - the volume table (SPEC.md 18.7)
    "DVOL_MAX": ("kernel/disk.inc", 8),
    "DV_KIND": ("kernel/disk.inc", 0),
    "DV_FLAGS": ("kernel/disk.inc", 2),
    "DV_SIZE": ("kernel/disk.inc", 16),
    "DVK_BIOS": ("kernel/disk.inc", 0),
    "DVK_DRV": ("kernel/disk.inc", 1),
    "DVK_FREE": ("kernel/disk.inc", 0xFF),
    # kernel/files.inc - a Disk window's list (SPEC.md 22)
    "FM_ROW_Y0": ("kernel/files.inc", 22),
    "FM_ROW_H": ("kernel/files.inc", 16),
    # ...and two fields of its per-window state block, which is what lets a
    # harness ask WHERE THE LIST IS SCROLLED TO instead of assuming row 0 is
    # entry 0. That assumption is the one dispcp.open_named exists to end.
    "FS_SCRL": ("kernel/files.inc", 2),
    "FS_N": ("kernel/files.inc", 6),
    "FS_VIEW": ("kernel/files.inc", 12),
    "FS_VSEG": ("kernel/files.inc", 16),
    # kernel/kernel.asm - the chrome
    "MBAR_H": ("kernel/kernel.asm", 20),
    "TITLE_H": ("kernel/kernel.asm", 18),
    "MENU_ITEM_H": ("kernel/menu.inc", 16),
    # kernel/mouse.inc - the pointer's CELL and the worst hot spot in it
    # (SPEC.md 7.1/7.2.2). A shape's cell starts at (pointer - hot), so a
    # harness masking the arrow's 8x12 at the published position misses the
    # part of a hot-spotted shape that sits ABOVE and LEFT of it.
    "CUR_GW": ("kernel/mouse.inc", 8),
    "CUR_GH": ("kernel/mouse.inc", 12),
    "CUR_XHX": ("kernel/mouse.inc", 3),
    "CUR_XHY": ("kernel/mouse.inc", 5),
    "KERNEL_SEG": ("kernel/kernel.asm", 0x0060),
    # kernel/memory.inc - the claim table (SPEC.md 50)
    "MEM_MAX": ("kernel/memory.inc", 32),
    "MC_SEG": ("kernel/memory.inc", 0),
    "MC_PARA": ("kernel/memory.inc", 2),
    "MC_OWN": ("kernel/memory.inc", 4),
    "MC_DMA": ("kernel/memory.inc", 6),
    "MC_RLOC": ("kernel/memory.inc", 8),
    "MC_SIZE": ("kernel/memory.inc", 10),
}

globals().update({k: v for k, (_, v) in _MIRROR.items()})

# Derived, and not mirrored: W_FLAGS bits 0 and 1 have no names in wm.inc.
WF_USED, WF_VIS = 1, 2

# The purgeable ranks (SPEC.md 50.6.4), by the high byte of the owner word.
RANK = {0xFB: "trivial", 0xFC: "low", 0xFD: "medium", 0xFE: "high"}


# --- the guard --------------------------------------------------------------

_EQU = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s+equ\s+"
                  r"(0[xX][0-9a-fA-F]+|[0-9]+)\s*(?:;.*)?$")


def _equs(path):
    """{NAME: int} for every `NAME equ <plain integer>` in a kernel source.

    Deliberately only PLAIN integers: several constants here are expressions
    over others (desk_zh1 is 32 + DESK_ZGAP + DESK_LBLH - 1), and a half
    evaluator that silently got one wrong would be worse than not checking it
    - so an expression is reported as unverifiable rather than guessed at.
    """
    out = {}
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            mo = _EQU.match(line)
            if mo:
                out.setdefault(mo.group(1), int(mo.group(2), 0))
    return out


def verify(root=None):
    """Every mirrored constant against the kernel. Returns a list of strings,
    empty when they all still agree."""
    root = root or ROOT
    problems, cache = [], {}
    for name, (rel, mine) in sorted(_MIRROR.items()):
        path = os.path.join(root, rel)
        if rel not in cache:
            if not os.path.exists(path):
                problems.append("%s: %s is missing" % (name, rel))
                cache[rel] = {}
                continue
            cache[rel] = _equs(path)
        theirs = cache[rel].get(name)
        if theirs is None:
            problems.append(
                "%s: no plain `%s equ <int>` in %s - it was renamed, removed, "
                "or turned into an expression. Check it by hand and update "
                "tools/os88geom.py." % (name, name, rel))
        elif theirs != mine:
            problems.append(
                "%s: this module says %d and %s says %d - the kernel moved. "
                "Fix the value here; every script reads it from this one "
                "place." % (name, mine, rel, theirs))
    return problems


def _check_at_import():
    # A source tree is not always there (a wheel, a copied script), and a
    # missing kernel is not a mismatch - only an ANSWERED disagreement is.
    if not os.path.isdir(os.path.join(ROOT, "kernel")):
        return
    problems = verify()
    if problems:
        raise GeomError(
            "tools/os88geom.py has gone stale against the kernel:\n  "
            + "\n  ".join(problems)
            + "\n\nEvery scripted coordinate in the harness comes from here, "
              "so this is stopping now rather than aiming clicks at the wrong "
              "pixels and reporting the feature under test as broken.")


_check_at_import()


# --- reading the guest ------------------------------------------------------
#
# `sym` is the symbol resolver. It defaults to the Marty's own `m.sym`; the
# display tests pass their cached `S` because they resolve against a kernel
# built with -D knobs (os88sym.py --define).

def _sym(m, sym):
    return sym if sym is not None else m.sym


def word(m, name, sym=None):
    """One live kernel word, by symbol."""
    b = m.read(_sym(m, sym)(name), 2)
    return b[0] | (b[1] << 8)


class Win(object):
    """One window record, decoded at the CURRENT stride."""

    def __init__(self, m, i, raw, sym=None):
        b = i * WIN_SIZE
        self.i = i
        self.flags = struct.unpack_from("<H", raw, b + W_FLAGS)[0]
        self.x, self.y, self.w, self.h = struct.unpack_from("<HHHH", raw,
                                                            b + W_X)
        tp = struct.unpack_from("<H", raw, b + W_TITLE)[0]
        seg = struct.unpack_from("<H", raw, b + W_SEG)[0]
        self.title = bytes(m.readseg(seg or KERNEL_SEG, tp, 24)) \
            .split(b"\0")[0].decode("latin-1")

    @property
    def used(self):
        return bool(self.flags & WF_USED)

    @property
    def visible(self):
        return bool(self.flags & WF_VIS)

    @property
    def promises(self):
        return bool(self.flags & WF_SAVEU)

    @property
    def content(self):
        """The content rect - what wm_su_rect answers and the cache holds."""
        return (self.x + 1, self.y + TITLE_H,
                self.x + self.w - 2, self.y + self.h - 2)

    def covers(self, x, y):
        return (self.x <= x < self.x + self.w
                and self.y <= y < self.y + self.h)

    def __repr__(self):
        return "%s@%d(%d,%d,%d,%d)%s%s" % (
            self.title, self.i, self.x, self.y, self.w, self.h,
            "" if self.visible else " HIDDEN",
            "+saveu" if self.promises else "")


def windows(m, sym=None):
    """Every USED window record."""
    raw = m.read(_sym(m, sym)("wm_wins"), MAX_WIN * WIN_SIZE)
    return [Win(m, i, raw, sym) for i in range(MAX_WIN)
            if struct.unpack_from("<H", raw, i * WIN_SIZE)[0] & WF_USED]


def winptr(m, win, sym=None):
    """The kernel address of a window record - what wm_top answers with."""
    i = win.i if hasattr(win, "i") else win
    return _sym(m, sym)("wm_wins") + i * WIN_SIZE


def win_rect(m, slot, sym=None):
    """(x, y, w, h) of one window slot, by index."""
    r = m.read(_sym(m, sym)("wm_wins") + slot * WIN_SIZE, WIN_SIZE)
    return struct.unpack_from("<HHHH", r, W_X)


def win_list(m, sym=None, check=True):
    """The slots that are used - and visible too, unless check is False."""
    want = (WF_USED | WF_VIS) if check else WF_USED
    raw = m.read(_sym(m, sym)("wm_wins"), MAX_WIN * WIN_SIZE)
    return [i for i in range(MAX_WIN)
            if struct.unpack_from("<H", raw, i * WIN_SIZE)[0] & want == want]


def top(m, sym=None):
    """wm_top's answer - the address of the frontmost VISIBLE window, or 0.

    Taken the way wm_top takes it, off the back of wm_zord, so a z-order bug
    cannot hide behind a re-derivation from the window table.
    """
    S = _sym(m, sym)
    n = m.read(S("wm_zn"), 1)[0]
    if not n:
        return 0
    zord = m.read(S("wm_zord"), n)
    raw = m.read(S("wm_wins"), MAX_WIN * WIN_SIZE)
    for k in range(n - 1, -1, -1):
        i = zord[k]
        if struct.unpack_from("<H", raw, i * WIN_SIZE)[0] & WF_VIS:
            return S("wm_wins") + i * WIN_SIZE
    return 0


def instances(m, sym=None):
    """{slot: {state, flags, win, minimized}} for every LIVE instance."""
    raw = m.read(_sym(m, sym)("inst_tab"), INST_MAX * I_RECSZ)
    out = {}
    for i in range(INST_MAX):
        b = i * I_RECSZ
        if raw[b + I_STATE] != 1:
            continue
        out[i] = {"state": raw[b + I_STATE], "flags": raw[b + I_FLAGS],
                  "win": struct.unpack_from("<H", raw, b + I_WIN)[0],
                  "minimized": bool(raw[b + I_FLAGS] & 1)}
    return out


def tile_xy(m, win, sym=None):
    """The centre of the dock tile of the instance owning a window.

    Through wm_owner (window slot -> instance slot) rather than counted off by
    eye: tile position i belongs to instance-table record i, holes included,
    so the Nth tile on screen is not the Nth instance (SPEC.md 30).
    """
    S = _sym(m, sym)
    i = win.i if hasattr(win, "i") else win
    inst = m.read(S("wm_owner"), MAX_WIN)[i]
    if inst == 0xFF:
        raise GeomError("window slot %d has no instance, so no dock tile" % i)
    return (DOCK_X0 + inst * DOCK_STEP + DOCK_TILE_W // 2,
            word(m, "vid_dock_y0", sym) + DOCK_TILE_H // 2)


def drive_ordinal(m, letter="B", sym=None):
    """Which desktop ZONE does drive `letter` own? None if it has none.

    NOT the drive number. A zone exists per volume with DV_FLAGS bit 0 set and
    the ordinal is that volume's POSITION among the shown ones - so a machine
    whose B: was retired by SPEC.md 18.97's probe, or which mounts a hard
    disk, numbers them differently. Walking dsk_vtab is the only way to be
    right, and it turns "no window opened" into "B: has no zone", which is the
    difference between a test that fails and a test that says why.
    """
    want = ord(letter.upper()) - ord("A") if isinstance(letter, str) else letter
    t = m.read(_sym(m, sym)("dsk_vtab"), DVOL_MAX * DV_SIZE)
    n = 0
    for v in range(DVOL_MAX):
        r = t[v * DV_SIZE:(v + 1) * DV_SIZE]
        if r[DV_KIND] == DVK_FREE or not (r[DV_FLAGS] & 1):
            continue
        if v == want:
            return n
        n += 1
    return None


def drive_xy(m, ordinal, sym=None):
    """The centre of zone `ordinal`, in virtual screen coordinates.

    desk_ord_xy's arithmetic (SPEC.md 26.1): zones fill a column downwards and
    wrap to a NEW COLUMN ON THE LEFT, and how many fit is [desk_rows] - 2 on a
    CGA with the tall icon, 4 with SPEC.md 26.4's square one, 4 on Hercules,
    7 on VGA. The pitch and the zone's height are live words for that reason.
    """
    rows = word(m, "desk_rows", sym)
    step = word(m, "desk_zstep", sym)
    zh1 = word(m, "desk_zh1", sym)
    zx = word(m, "vid_desk_zx", sym)
    col, row = divmod(ordinal, rows)
    return (zx - col * DESK_COLW + DESK_ZW // 2,
            DESK_ZY0 + row * step + zh1 // 2)


def drive_pt(m, letter="B", sym=None):
    """drive_xy for a LETTER, refusing a drive with no zone by name."""
    ordinal = drive_ordinal(m, letter, sym)
    if ordinal is None:
        raise GeomError("drive %s: has no desktop zone on this machine "
                        "(dsk_vtab says it is free or hidden)" % letter)
    return drive_xy(m, ordinal, sym)


def row_xy(win, row=0, x=60):
    """fm_layout's list geometry (SPEC.md 22): row `row`'s middle, in the
    content of a Disk window. Takes a Win, or an (x, y) frame origin."""
    wx, wy = (win.x, win.y) if hasattr(win, "x") else win
    return (wx + x, wy + TITLE_H + FM_ROW_Y0 + row * FM_ROW_H + FM_ROW_H // 2)


# --- and the same question asked of the harness itself ----------------------

def scan(root=None):
    """Every LOCAL copy of a mirrored constant in tools/ and tests/.

    Returns [(path, line, name, value, kernel_value)], stale ones first. This
    module fixes the copies that had already gone wrong; `scan` is what stops
    the next one going wrong quietly, because a copy that is correct today is
    only the seed of the next occurrence - and several are left deliberately,
    where converting a working test would be more churn than the copy is
    worth. One command names them all and says which have drifted.
    """
    import ast

    root = root or ROOT
    out = []
    for sub in ("tools", "tests"):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".py") or fn == "os88geom.py":
                continue
            path = os.path.join(d, fn)
            try:
                tree = ast.parse(open(path, "r", errors="replace").read())
            except SyntaxError:
                continue
            for node in ast.walk(tree):
                if not isinstance(node, ast.Assign):
                    continue
                for tgt in node.targets:
                    # NAME = 18, and NAME, OTHER = 18, 20 - the two spellings
                    # the harness actually uses.
                    pairs = []
                    if isinstance(tgt, ast.Name):
                        pairs = [(tgt, node.value)]
                    elif (isinstance(tgt, ast.Tuple)
                          and isinstance(node.value, ast.Tuple)
                          and len(tgt.elts) == len(node.value.elts)):
                        pairs = list(zip(tgt.elts, node.value.elts))
                    for nm, val in pairs:
                        if not isinstance(nm, ast.Name) or nm.id not in _MIRROR:
                            continue
                        if not isinstance(val, ast.Constant) \
                                or not isinstance(val.value, int):
                            continue
                        out.append((os.path.join(sub, fn), node.lineno, nm.id,
                                    val.value, _MIRROR[nm.id][1]))
    return sorted(out, key=lambda r: (r[3] == r[4], r[0], r[1]))


def _main():
    problems = verify()
    for p in problems:
        print("os88geom: %s" % p)

    copies = scan()
    stale = [c for c in copies if c[3] != c[4]]
    for path, line, name, mine, theirs in copies:
        print("os88geom: %-22s %s:%d = %d%s"
              % (name, path, line, mine,
                 "" if mine == theirs else "  ** STALE, kernel says %d **"
                 % theirs))
    print("os88geom: %d constants, %d problem(s); %d local copies, %d stale"
          % (len(_MIRROR), len(problems), len(copies), len(stale)))
    return 1 if problems or stale else 0


if __name__ == "__main__":
    sys.exit(_main())
