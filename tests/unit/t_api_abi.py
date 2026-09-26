#!/usr/bin/env python3
"""The API table IS the ABI - so check it against the assembled bytes.

    python3 tests/unit/t_api_abi.py

WHAT BREAKS, AND WHY NOTHING CATCHES IT TODAY.  SPEC.md 20.3's table is 8-byte
cells at 0060:0010 + 8n, and `apps/os88api.inc` turns each address into a
`%define` a package FAR-CALLS.  The two halves live in different files, so
they can disagree, and when they do nothing anywhere says so: the package
assembles, loads, runs, and calls the wrong kernel routine.

CLAUDE.md already names the exact way it happens and asks for it to be done by
hand after every merge - *"sort the %defines by address and look for a
duplicate"* - because it has happened: this branch and `main` both appended to
the same tail at the same time, `main`'s OSAPI_GFX_BLIT1/OSAPI_VOL_SYS landing
on this branch's OSAPI_KEY_DOWN/OSAPI_FSX_SURF.  **The merge did not
conflict.**  Two sides adding different NAMES merge clean, and the result is
two cells pointing at one another's addresses with nothing to say so until a
package calls one and gets the other.  A check performed by remembering to
perform it is not a check, so this is that comparison as a gate.

IT READS THE BINARY, NOT THE SOURCE.  `build/kernel.bin` is a flat image whose
`.text` is loaded at KERNEL_SEG:0000 - BOOT2_PAD bytes into the file since
SPEC.md 2.9 put stage 2 in front of it, which tools/os88layout.py is the one
place that knows - so a slot's address is its file offset plus that, and the cell
can simply be decoded:

    OSAPI_SLOT   1E 0E 1F  E8 lo hi  1F CB     push ds/push cs/pop ds/
                                               call near/pop ds/retf (hot, 8)
    OSAPI_XCELL  55 BD tt tt E9 lo hi          push bp/mov bp,<target>/
                                               jmp near api_x   (hot, 7)
    OSAPI_R*     55 E8 lo hi tt tt             push bp/call api_r<kind>/
                                               dw <target>      (rare, 6)
    OSAPI_JCELL  E9 lo hi                      jmp near <stub>  (3)
    OSAPI_FCELL  9A tt tt ss ss CB             call COLD_SEG:<target>/retf (6)
    OSAPI_ICELL  2E <mov with one memory operand> [98] CB
                                               the INLINE cell: the routine
                                               itself, through CS (5 or 6)

AN INLINE CELL HAS NO TARGET, AND THAT IS WHAT CHECK 6 HAS TO GET ROUND.
Since kernel size pass 4's C3 (SPEC.md 20.3) fifteen cells whose routine was
one `mov` and a `ret` ARE that routine: `mov ax, [cs:ticks] / retf`.  There
is no label to compare the published name against, so the ledger is INLINE
below - one row per cell saying which way the one access goes, in which
register, and at what: a kernel VARIABLE, resolved through os88sym exactly as
a routine is (in `.text` or `.bss`, the two sections KERNEL_SEG:offset can
name), or a WINDOW-RECORD field `[bx+W_*]`, whose displacement must equal the
equate.  So `OSAPI_GET_TICKS` must still reach `ticks` and nothing else, and
a cell that loads AL where its contract says AX, or stores where it should
load, fails as loudly as a SLOT calling the wrong routine.  An inline-shaped
cell with no row fails too, and so does a row whose cell is not inline - the
ledger cannot rot in either direction.

The cells are VARIABLE LENGTH since kernel size pass 4 (SPEC.md 20.3), so
the table is walked by each cell's decoded length and never by a stride.
For a rare cell the `E8` displacement names WHICH body (api_rs, api_rx,
api_rxc, api_rn, api_rsc), and the body decides the section the `dw` target
is resolved in.

THE BP FAMILY'S TARGET IS THE `BD` IMMEDIATE, NOT THE `E9` DISPLACEMENT.
The jump goes to the family's ONE shared body, which is the same address for
every cell of it; decoding it as the target would make check 6 fail on every
one of them and check 4 pass for the wrong reason.  What the `E9` DOES say
is WHICH body, and that decides the segment the target lives in: `api_x`
near-calls a `.text` routine, `api_rxc` and `api_rn` far-call a `.cold` one
through `api_far` (SPEC.md 20.3.2).  An FCELL's offset is `.cold` by
construction, and its segment word must BE COLD_SEG -
a cell that far-calls anywhere else is a cell somebody has mistyped.

So a target is resolved in the section its shape names, never "somewhere in
the kernel": a `.cold` offset is a perfectly good `.text` offset too, and
resolving it against the wrong table gives a plausible wrong name, which is
the trap docs/plans/DISK-CPU-PLAN.md 1 records for the profiler.

The `call`'s displacement is resolved against `tools/os88sym.py`'s map, which
asserts byte-identity with the kernel this tree just built - so a symbol here
can never describe a different binary.  That is the whole point of going to
the bytes: a source-level scan of `kernel.asm` re-derives what NASM already
decided, and the failure being defended against is precisely a disagreement
between what the source says and what got assembled.

SIX THINGS ARE CHECKED, and the last is the one worth the file.

  1. No two published names share an address, and no name is published twice.
  2. Every address is a real cell boundary, walking the table by each
     cell's decoded length (8, 7, 6 or 3 bytes, or an INLINE cell's own
     5 or 6 - SPEC.md 20.3).
  3. Every cell decodes to one of the shapes above - a cell that is none
     of them is a table somebody has written data into.
  4. Every call target lands on a real `.text` symbol.  A displacement into
     the middle of a routine assembles and runs.
  5. Every cell inside the table's extent is accounted for: published here,
     published to DRIVERS (`drivers/os88drv.inc` owns 0x0248), or on the
     COMPAT list below.
  6. THE NAME AGREES WITH THE ROUTINE.  `OSAPI_GFX_LOCK` must reach
     `gfx_lock`.  Most match by the naming convention (`x`, `osapi_x`,
     `api_x` for a stub); the rest are in ALIAS, which makes that list an
     ABI ledger - moving one of those slots onto a different routine now
     costs a deliberate edit here, where a reviewer sees it.

     THE X/N REWORK MADE THIS CHECK WEAKER, and that is worth saying rather
     than pretending otherwise.  Fifteen slots used to be verified by the
     `api_<stem>` convention - a rule nobody can forget - and now name their
     real routine, so they need a ledger row a reviewer has to maintain.
     What was bought is that check 4 lands on the BODY rather than on a stub
     that trivially exists.  A real gain, and a small one against thirteen
     new exemptions.

     A cell that names a COLD body (SPEC.md 20.3.2) names the far ENTRY,
     which carries a module tag and a suffix the resident thunk it replaced
     did not: `dwf_dskw_read` where the thunk was `dskw_read`,
     `osapi_vol_at_x` where it was `osapi_vol_at`.  Those decorations say
     where the routine lives and nothing about what it is, so for a `.cold`
     target the comparison is ALSO made on the stem - the label with one
     leading `<tag>f_`/`<tag>z_` and one trailing `_x` removed - and every
     ALIAS row that named the thunk still holds.  The two whose stem is a
     different word (`ldf_ld_pkg_start`, `fcpf_fcp_door`) are rows below.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88layout                                            # noqa: E402
sys.path.insert(0, HERE)

import os88sym                                            # noqa: E402
from harness import check, eq, done                       # noqa: E402
import os88build                                       # noqa: E402

TABLE_BASE = 0x0010
CELL = 8                # the LONGEST cell: the decode window, not the stride

# Cells that exist in the kernel and are deliberately NOT published by this
# branch's SDK. 0x01B8..0x01C8 are `main`'s three paragraph-counting arena
# slots, kept live so a main-era package still runs and answered through
# converting wrappers (apps/os88api.inc's "KB-counting memory slots" note);
# new code uses the KB slots at 0x0200+. They are not a free list.
COMPAT_OLD = {0x01B8: "main's OSAPI_MEM_ALLOC (paragraphs)",
          0x01C0: "main's OSAPI_MEM_FREE (paragraphs)",
          0x01C8: "main's OSAPI_MEM_AVAIL (paragraphs)",
          # SPEC.md 20.3.1's free list: it was OSAPI_FILE_MOVE, folded into
          # 0x0578's verb byte (22.25). stc/ret, no SDK name.
          0x0580: "RETIRED OSAPI_FILE_MOVE (SPEC.md 22.25)",
          # SPEC.md 50.6.6.1: OSAPI_MEM_CLAIM_LVL, a claim carrying the purge
          # floor as an argument, retired the day the floor became a thing a
          # task SETS (OSAPI_MEM_FLOOR, 0x0560). stc/ret, not published.
          0x0568: "OSAPI_MEM_CLAIM_LVL, retired (SPEC.md 50.6.6.1)"}
COMPAT = {}

# Slots whose published name is not its routine's name. Every entry is a
# deliberate ABI decision; adding one means the SDK and the kernel have
# parted on purpose. Keep the reason short and true.
ALIAS = {
    # SPEC.md 29.6: the slot is wm-shaped because a package names its own
    # WINDOW, and the routine is instance.inc's because minimizing is a fact
    # about the INSTANCE - I_FLAGS bit 0, the dock tile, the zoom.  The two
    # modules were always going to disagree about the name; what matters is
    # that OSAPI_WM_HIDE and this are different cells, since a plain hide
    # leaves a tile that does nothing (the slot's own comment has the account).
    "OSAPI_WM_MINIMIZE":  "inst_minimize",
    # Two cells, one routine, on purpose: the difference is the STUB, not the
    # body - 0x0448 goes through OSAPI_XSTUB and overwrites ES, 0x04A0 through
    # the ordinary SLOT and does not (SPEC.md 20.11.2).
    "OSAPI_DRV_CALL_AT":  "drv_pkg_call_x",
    # SPEC.md 6.6.4: the SDK spelling says what the call COSTS and the kernel
    # routine keeps the name SPEC.md 6 documents it under. The slot numbers did
    # not move - 0x0060 and 0x0068 are what they always were - so this is a
    # rename of a %define and its call sites and nothing about the ABI changed.
    "OSAPI_FONT_CHAR_XPARENT": "font_char",
    "OSAPI_FONT_STR_XPARENT":  "font_str_x",
    "OSAPI_KEY_DOWN":     "kbd_down",
    "OSAPI_FULLSCREEN":   "wm_fullscreen",
    "OSAPI_WM_GROW":      "wm_grow_paint",
    "OSAPI_MENU_SET":     "menu_win_set",
    "OSAPI_FILE_DLG":     "api_fdlg_open",   # still a hand-written JSLOT stub
    # SPEC.md 20.3.2: the cell names the cold far entry, and these two entries
    # are named for the ROUTINE they front rather than for the slot.
    "OSAPI_PKG_START":    "ldf_ld_pkg_start",   # loader.inc's ld_pkg_start_x
    "OSAPI_FILE_COPY":    "fcpf_fcp_door",      # filecp.inc's one door, AL = the verb
    # SPEC.md 20.3's X and N cells name their target directly since the two
    # families became one body each, so a slot whose SDK spelling differs from
    # its routine's needs a row here. Fifteen of them do; these thirteen are
    # the ones the convention used to cover through an `api_<stem>` stub.
    "OSAPI_ICON_DRAW":    "icon_draw_x",
    "OSAPI_FONT_RUN":     "font_run_x",
    "OSAPI_FONT_WIDTH":   "font_width_x",
    "OSAPI_MEM_PARKSAFE": "inst_parksafe_set",
    # ...and its stronger sibling (SPEC.md 66.6.2). Both live in
    # instance.inc because both are a fact about an INSTANCE, and the
    # SDK names them for what the package is declaring rather than for
    # where the kernel keeps it.
    "OSAPI_TASK_RESTARTABLE": "inst_restart_set",
    "OSAPI_SND_FM":       "osapi_snd_fm_x",
    "OSAPI_DRV_CALL":     "drv_pkg_call_x",   # beside DRV_CALL_AT above: two
                                              # cells, one routine, on purpose
    "OSAPI_FILE_WRITE":   "dskw_write",
    "OSAPI_FILE_READ":    "dskw_read",
    "OSAPI_FILE_DELETE":  "dskw_delete",
    "OSAPI_FILE_APPEND":  "dskw_append",
    "OSAPI_FILE_READ_AT": "dskw_read_at",
    "OSAPI_FILE_WRITE_AT": "dskw_write_at",   # ...and its other half (18.4.7)
    "OSAPI_FILE_MKDIR":   "dskw_mkdir",
    "OSAPI_FILE_RMDIR":   "dskw_rmany",
    "OSAPI_TASK_SPAWN":   "inst_pkg_spawn",
    "OSAPI_TASK_ALIVE":   "inst_pkg_alive",
    "OSAPI_ABOUT_SET":    "wm_about_set",
    "OSAPI_FS_PROG":      "fpg_stepb",
    "OSAPI_XMEM_CAPS":    "xm_caps",
    "OSAPI_XMEM_ALLOC":   "xm_alloc",
    "OSAPI_XMEM_FREE":    "xm_free",
    "OSAPI_XMEM_COPY":    "xm_copy",
    "OSAPI_WM_ONDRAG":    "wm_ondrag_c",
    "OSAPI_WM_TIMER":     "wm_timer_c",
    "OSAPI_WM_ONTIMER":   "wm_ontimer_c",
    "OSAPI_TASK_PARK":    "inst_task_park",
    "OSAPI_WM_TITLE":     "wm_title_set",
    "OSAPI_GFX_PEN":      "gfx_pen_cf",
    "OSAPI_TOAST":        "toast_show",
    "OSAPI_BATCH_BEGIN":  "dsk_batch_begin",
    "OSAPI_BATCH_END":    "dsk_batch_end",
    "OSAPI_REBOOT":       "ui_reboot_post",
    # ...and SPEC.md 75.2's, where the divergence IS the contract: the slot
    # does not close the window, it REQUESTS a close that the next UI pass
    # spends - because the caller is standing in the segment the close is
    # about to free. A cell named for the routine would be the honest name
    # for the wrong promise.
    "OSAPI_WM_CLOSE":     "wm_close_req",
}

SDK_FILES = ["apps/os88api.inc", "drivers/os88drv.inc"]
DEFINE = re.compile(r"^%define\s+(OSAPI_\w+)\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)", re.M)


def slots():
    """[(name, address, file)] for every published cell, both SDKs."""
    out = []
    for rel in SDK_FILES:
        with open(os.path.join(ROOT, rel)) as f:
            for name, addr in DEFINE.findall(f.read()):
                out.append((name, int(addr, 16), rel))
    return out


SHAPES = "1E0E1F E8.. 1FCB | 55E8.. tttt | 55BD.. E9.. | E9.. | 9A.. ssss CB | 2E <mov> [98] CB"

# The INLINE cells (SPEC.md 20.3): published name -> (direction, register,
# operand, trailing ops). The operand is a kernel variable's label, or
# "[bx+W_*]" for a window-record word. Every row is a register contract as
# well as a name: evq_pending's is "AL, widened to AX by cbw".
INLINE = {
    "OSAPI_GET_TICKS":     ("load",  "ax", "ticks", ()),
    "OSAPI_SET_COLOR":     ("store", "al", "gfx_color", ()),
    "OSAPI_SRAND":         ("store", "ax", "osapi_seed", ()),
    "OSAPI_CPU_INFO":      ("load",  "ax", "cpu_tier", ()),   # AH = [cpu_feat], the byte after
    "OSAPI_WM_ONSIZE":     ("store", "ax", "[bx+W_ONSIZE]", ()),
    "OSAPI_MENU_OWNER":    ("load",  "bx", "menu_win", ()),
    "OSAPI_BOOT_TICKS":    ("load",  "ax", "boot_ticks", ()),
    "OSAPI_EVQ_PENDING":   ("load",  "al", "evq_count", ("cbw",)),
    "OSAPI_WM_ONRESIZE":   ("store", "ax", "[bx+W_ONSZ]", ()),
    "OSAPI_VOL_SYS":       ("load",  "bl", "dsk_bootvol", ()),
    "OSAPI_WM_ONWAKE":     ("store", "ax", "[bx+W_ONWK]", ()),
    "OSAPI_WM_ONRCLICK":   ("store", "ax", "[bx+W_ONRC]", ()),
    "OSAPI_GFX_BLIT1_PEN": ("store", "ax", "gfx_b1ink", ()),  # AH -> gfx_b1pap, adjacent
    "OSAPI_ICON_PEN":      ("store", "ax", "ico_c1", ()),     # AH -> ico_c2, adjacent
    "OSAPI_WM_ONCLICK":    ("store", "ax", "[bx+W_ONCLICK]", ()),
}


def inline(c):
    """(length, (dir, reg, operand, extra)) for an INLINE cell, else None.

    The operand is ("abs", offset) or ("bx", disp8). Only the encodings the
    table uses are accepted: a CS-prefixed accumulator moffs `mov` (A0-A3),
    a `mov` r/m16 or r/m8 with [disp16] or [bx+disp8], then an optional
    `cbw`, then the `retf` - anything else is not an inline cell.
    """
    if len(c) < 5 or c[0] != 0x2E:
        return None
    op = c[1]
    REG8, REG16 = ("al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"), \
                  ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")
    if op in (0xA0, 0xA1, 0xA2, 0xA3):
        d = "load" if op in (0xA0, 0xA1) else "store"
        reg = "al" if op in (0xA0, 0xA2) else "ax"
        opd, n = ("abs", struct.unpack_from("<H", c, 2)[0]), 4
    elif op in (0x88, 0x89, 0x8A, 0x8B):
        d = "load" if op in (0x8A, 0x8B) else "store"
        mod, r, rm = c[2] >> 6, (c[2] >> 3) & 7, c[2] & 7
        reg = (REG16 if op & 1 else REG8)[r]
        if mod == 0 and rm == 6:
            opd, n = ("abs", struct.unpack_from("<H", c, 3)[0]), 5
        elif mod == 1 and rm == 7:
            opd, n = ("bx", c[3]), 4
        else:
            return None
    else:
        return None
    extra = ()
    if n < len(c) and c[n] == 0x98:
        extra, n = ("cbw",), n + 1
    if n >= len(c) or c[n] != 0xCB:
        return None
    return n + 1, (d, reg, opd, extra)


# The stem of a cold far entry: `dwf_dskw_read` -> `dskw_read`,
# `osapi_vol_at_x` -> `osapi_vol_at`, `lzf_decomp` -> `decomp`.  One tag,
# one suffix, and only for a target the shape says is `.cold`.
COLD_TAG = re.compile(r"^[a-z]{2,4}[fz]_(?=[a-z])")


def stem(name):
    name = COLD_TAG.sub("", name, count=1)
    return name[:-2] if name.endswith("_x") else name


def decode(blob, addr, bodies, cold_seg):
    """(kind, target, section) for the cell at `addr`, or (None, why, None).

    `bodies` maps the shared bodies' names (api_x, api_rs, api_rx, api_rxc, api_rn, api_rsc) to
    their .text offsets; `cold_seg` is COLD_SEG's value.  The section is the
    one the shape says the target lives in - see the header.
    """
    c = blob[addr:addr + CELL]
    if len(c) < CELL:
        return None, None, None

    def rel(at, i):                       # a near displacement at c[i], from `at`
        return (at + struct.unpack_from("<h", c, i)[0]) & 0xFFFF

    def imm(i):
        return struct.unpack_from("<H", c, i)[0]

    if c[0:3] == b"\x1e\x0e\x1f" and c[3] == 0xE8 and c[6:8] == b"\x1f\xcb":
        return "SLOT", rel(addr + 6, 4), ".text"
    if c[0] == 0x55 and c[1] == 0xE8:
        # the 6-byte RARE cell (SPEC.md 20.3): push bp / call <kind> / dw target
        body = rel(addr + 4, 2)
        kind = {bodies.get("api_rs"): ("RSLOT", ".text"),
                bodies.get("api_rx"): ("RXCELL", ".text"),
                bodies.get("api_rxc"): ("RCXCELL", ".cold"),
                bodies.get("api_rn"): ("RNCELL", ".cold"),
                bodies.get("api_rsc"): ("RCSLOT", ".cold")}.get(body)
        if kind is None:
            return None, "rare cell calling 0x%04X, which is no rare body" % body, None
        return kind[0], imm(4), kind[1]
    if c[0] == 0x55 and c[1] == 0xBD and c[4] == 0xE9:
        body = rel(addr + 7, 5)
        if body != bodies.get("api_x"):
            return None, "BP-family shape jumping to 0x%04X, which is not api_x" % body, None
        return "XCELL", imm(2), ".text"
    if c[0] == 0x9A and c[5] == 0xCB:
        seg = imm(3)
        if seg != cold_seg:
            return None, "FARCELL to segment 0x%04X, which is not COLD_SEG (0x%04X)" % (seg, cold_seg), None
        return "FARCELL", imm(1), ".cold"
    if c[0] == 0xE9:
        return "JSLOT", rel(addr + 3, 1), ".text"
    il = inline(c)
    if il is not None:
        return "INLINE", il, None
    return None, c.hex(), None


def check_inline(name, addr, got, label_at, equ):
    """Check 6 for an INLINE cell: the access is the one the ledger names."""
    d, reg, opd, extra = got
    if not check(name in INLINE, "%s (0x%04X) is an INLINE cell with no INLINE row" % (name, addr),
                 "an inline cell has no routine to check the name against, so it needs a "
                 "ledger row saying what it reads or writes", got="%s %s %r" % (d, reg, opd)):
        return
    wd, wreg, wopd, wextra = INLINE[name]
    check((d, reg, extra) == (wd, wreg, wextra),
          "%s (0x%04X) is a %s of %s%s" % (name, addr, d, reg, "".join(" + " + e for e in extra)),
          "the cell's access disagrees with its contract", got="%s %s %s" % (d, reg, extra),
          want="%s %s %s" % (wd, wreg, wextra))
    if wopd.startswith("[bx+"):
        field = wopd[4:-1]
        check(opd[0] == "bx" and opd[1] == equ.get(field),
              "%s (0x%04X) addresses %r, not %s" % (name, addr, opd, wopd),
              "a window-record setter writing the wrong word of the record",
              got=repr(opd), want="bx+%s = %r" % (field, equ.get(field)))
        return
    here = [] if opd[0] != "abs" else \
        label_at.get((".text", opd[1]), []) + label_at.get((".bss", opd[1]), [])
    check(wopd in here,
          "%s (0x%04X) addresses %s" % (name, addr,
                                        "/".join(here) or "0x%04X" % opd[1] if opd[0] == "abs" else repr(opd)),
          "the SDK name and the variable the cell touches have parted",
          got="/".join(here) or repr(opd), want=wopd)


def main():
    blob = open(os.path.join(ROOT, os88build.at("build/kernel.bin")), "rb").read()
    # ...and drop stage 2, so every address below is an offset into `.text`
    # exactly as it was before SPEC.md 2.9 (tools/os88layout.py)
    blob = blob[os88layout.boot2_pad(ROOT):]
    off, sect = os88sym.syms(), os88sym.sections()
    cold_seg = os88sym.equates()["COLD_SEG"]
    # (section, offset) -> the labels there, for the two sections a cell can
    # name. Several labels can share an offset (an entry point and its
    # fallthrough alias), so this is a list.
    label_at = {}
    for n, o in off.items():
        if sect.get(n) in (".text", ".cold", ".bss"):
            label_at.setdefault((sect[n], o), []).append(n)
    equ = os88sym.equates()
    bodies = {b: off[b] for b in ("api_x", "api_rs", "api_rx", "api_rxc", "api_rn", "api_rsc")}
    LEN = {"SLOT": 8, "RSLOT": 6, "RXCELL": 6, "RCXCELL": 6, "RNCELL": 6, "RCSLOT": 6,
           "XCELL": 7, "FARCELL": 6, "JSLOT": 3}
    tend = off["osapi_table_end"]
    starts = []
    a = TABLE_BASE
    while a < tend:
        k, t, w = decode(blob, a, bodies, cold_seg)
        if not check(k is not None, "the table does not decode at 0x%04X" % a, "a cell of no known shape", got=t):
            break
        starts.append(a)
        a += t[0] if k == "INLINE" else LEN[k]
    check(a == tend, "the table walk ends at 0x%04X, not osapi_table_end 0x%04X" % (a, tend), "cell lengths disagree")
    startset = set(starts)

    pub = slots()

    # 1. one name per address, one address per name
    by_addr, by_name = {}, {}
    for name, addr, src in pub:
        if addr in by_addr:
            check(False, "slot 0x%04X is published TWICE" % addr,
                  "two branches appended to the same tail and the merge did not "
                  "conflict - one of these packages calls the other's routine",
                  got="%s (%s) and %s (%s)" % (name, src, by_addr[addr][0], by_addr[addr][1]),
                  want="one name per cell")
        else:
            by_addr[addr] = (name, src)
        if name in by_name:
            check(False, "%s is published twice (0x%04X and 0x%04X)"
                  % (name, by_name[name], addr), "the later %define silently wins")
        else:
            by_name[name] = addr

    # 2. every address is a real cell
    top = max(by_addr)
    for addr, (name, _) in sorted(by_addr.items()):
        check(addr in startset,
              "%s at 0x%04X is not a cell boundary" % (name, addr),
              "cells are walked by length from 0x%04X; a misaligned address lands "
              "mid-cell and far-calls into the middle of a DS switch" % TABLE_BASE)

    # 3/4/6. shape, target, and the name/routine agreement
    for addr, (name, src) in sorted(by_addr.items()):
        kind, tgt, where = decode(blob, addr, bodies, cold_seg)
        if not check(kind is not None,
                     "%s (0x%04X) is not a slot cell" % (name, addr),
                     "the cell is none of the known shapes - the table has "
                     "been overwritten, a cell reaches the wrong body, or "
                     "the address is past the table's end",
                     got=tgt, want=SHAPES):
            continue
        if kind == "INLINE":
            check_inline(name, addr, tgt[1], label_at, equ)
            continue
        if name in INLINE:
            check(False, "%s (0x%04X) is in INLINE but its cell is a %s" % (name, addr, kind),
                  "the ledger names an inline cell that is not one - remove the row, "
                  "or the cell lost its inline body")
        names = label_at.get((where, tgt), [])
        if not check(bool(names),
                     "%s (0x%04X, %s) reaches %s:0x%04X, which is not a label there"
                     % (name, addr, kind, where, tgt),
                     "a displacement into the middle of a routine assembles "
                     "cleanly and runs wrong - and a .cold offset resolved "
                     "against .text is a plausible wrong name"):
            continue
        s = name[len("OSAPI_"):].lower()
        want = {ALIAS[name]} if name in ALIAS else {s, "osapi_" + s, "api_" + s}
        seen = set(names)
        if where == ".cold":
            seen |= {stem(n) for n in names}
        check(bool(seen & want),
              "%s (0x%04X) reaches %s" % (name, addr, "/".join(names)),
              "the SDK name and the kernel routine have parted. If this is "
              "deliberate, add it to ALIAS in this file so a reviewer sees it",
              got="/".join(names), want=" or ".join(sorted(want)))

    # 5. nothing unaccounted for inside the table
    for addr in starts:
        if addr in by_addr or addr in COMPAT:
            continue
        kind, tgt, where = decode(blob, addr, bodies, cold_seg)
        names = label_at.get((where, tgt), []) if kind else []
        check(False, "cell 0x%04X is in the table and published nowhere" % addr,
              "either it is a new slot whose %define was forgotten - packages "
              "cannot reach it - or a retired one that needs a COMPAT entry here",
              got="%s -> %s" % (kind, "/".join(names) or tgt), want="a %define or a COMPAT row")

    # ...and the ledger must not rot: an ALIAS naming a slot nobody publishes
    # any more is a line that will never be read again.
    for name in ALIAS:
        check(name in by_name, "ALIAS names %s, which no SDK publishes" % name,
              "remove the row - a stale exception silently exempts nothing")
    for name in INLINE:
        check(name in by_name, "INLINE names %s, which no SDK publishes" % name,
              "remove the row - a stale exception silently exempts nothing")

    print("t_api_abi: %d published slots (%d aliased, %d inline, %d compat), table 0x%04X..0x%04X"
          % (len(by_addr), len(ALIAS), len(INLINE), len(COMPAT), TABLE_BASE, top))
    done("t_api_abi")


if __name__ == "__main__":
    main()
