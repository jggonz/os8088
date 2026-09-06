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
                                               call near/pop ds/retf
    OSAPI_JSLOT  E9 lo hi  00 00 00 00 00      jmp near <stub>
    OSAPI_X/NCELL 55 BD lo hi  E9 lo hi  00    push bp/mov bp,<target>/
                                               jmp near api_x|api_n

THE THIRD SHAPE'S TARGET IS THE `BD` IMMEDIATE, NOT THE `E9` DISPLACEMENT.
The jump goes to the family's ONE shared body, which is the same address for
all forty cells; decoding it instead would make check 6 fail on every one of
them and check 4 pass for the wrong reason.

The `call`'s displacement is resolved against `tools/os88sym.py`'s map, which
asserts byte-identity with the kernel this tree just built - so a symbol here
can never describe a different binary.  That is the whole point of going to
the bytes: a source-level scan of `kernel.asm` re-derives what NASM already
decided, and the failure being defended against is precisely a disagreement
between what the source says and what got assembled.

SIX THINGS ARE CHECKED, and the last is the one worth the file.

  1. No two published names share an address, and no name is published twice.
  2. Every address is a real cell: 0x0010 + 8n, inside the table.
  3. Every cell decodes to one of the two shapes above - a cell that is
     neither is a table somebody has written data into.
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

TABLE_BASE = 0x0010
CELL = 8

# Cells that exist in the kernel and are deliberately NOT published by this
# branch's SDK. 0x01B8..0x01C8 are `main`'s three paragraph-counting arena
# slots, kept live so a main-era package still runs and answered through
# converting wrappers (apps/os88api.inc's "KB-counting memory slots" note);
# new code uses the KB slots at 0x0200+. They are not a free list.
COMPAT = {0x01B8: "main's OSAPI_MEM_ALLOC (paragraphs)",
          0x01C0: "main's OSAPI_MEM_FREE (paragraphs)",
          0x01C8: "main's OSAPI_MEM_AVAIL (paragraphs)"}

# Slots whose published name is not its routine's name. Every entry is a
# deliberate ABI decision; adding one means the SDK and the kernel have
# parted on purpose. Keep the reason short and true.
ALIAS = {
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
    # SPEC.md 20.3's X and N cells name their target directly since the two
    # families became one body each, so a slot whose SDK spelling differs from
    # its routine's needs a row here. Fifteen of them do; these thirteen are
    # the ones the convention used to cover through an `api_<stem>` stub.
    "OSAPI_ICON_DRAW":    "icon_draw_x",
    "OSAPI_FONT_RUN":     "font_run_x",
    "OSAPI_FONT_WIDTH":   "font_width_x",
    "OSAPI_MEM_PARKSAFE": "inst_parksafe_set",
    "OSAPI_SND_FM":       "osapi_snd_fm_x",
    "OSAPI_DRV_CALL":     "drv_pkg_call_x",   # beside DRV_CALL_AT above: two
                                              # cells, one routine, on purpose
    "OSAPI_FILE_WRITE":   "dskw_write",
    "OSAPI_FILE_READ":    "dskw_read",
    "OSAPI_FILE_DELETE":  "dskw_delete",
    "OSAPI_FILE_APPEND":  "dskw_append",
    "OSAPI_FILE_READ_AT": "dskw_read_at",
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


def decode(blob, addr):
    """(kind, target) for the cell at `addr`, or (None, None) if it is neither."""
    c = blob[addr:addr + CELL]
    if len(c) < CELL:
        return None, None
    if c[0:3] == b"\x1e\x0e\x1f" and c[3] == 0xE8 and c[6:8] == b"\x1f\xcb":
        return "SLOT", (addr + 6 + struct.unpack_from("<h", c, 4)[0]) & 0xFFFF
    if c[0] == 0x55 and c[1] == 0xBD and c[4] == 0xE9 and c[7] == 0x00:
        # X/N cell: the target rides in BP, so it is the immediate at 2..3.
        return "XCELL", struct.unpack_from("<H", c, 2)[0]
    if c[0] == 0xE9 and c[3:8] == b"\x00" * 5:
        # ...and the JSLOT shape stays: five cells still reach a hand-written
        # stub (rename, the file dialog, the two fenced SYS writes, file_find).
        return "JSLOT", (addr + 3 + struct.unpack_from("<h", c, 1)[0]) & 0xFFFF
    return None, c.hex()


def main():
    blob = open(os.path.join(ROOT, "build/kernel.bin"), "rb").read()
    # ...and drop stage 2, so every address below is an offset into `.text`
    # exactly as it was before SPEC.md 2.9 (tools/os88layout.py)
    blob = blob[os88layout.boot2_pad(ROOT):]
    off, sect = os88sym.syms(), os88sym.sections()
    # offset -> the .text labels there. Several labels can share an offset
    # (an entry point and its fallthrough alias), so this is a list.
    text_at = {}
    for n, o in off.items():
        if sect.get(n) == ".text":
            text_at.setdefault(o, []).append(n)

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
        check(addr >= TABLE_BASE and (addr - TABLE_BASE) % CELL == 0,
              "%s at 0x%04X is not a cell boundary" % (name, addr),
              "cells are 8 bytes from 0x%04X; a misaligned address lands "
              "mid-cell and far-calls into the middle of a DS switch" % TABLE_BASE)

    # 3/4/6. shape, target, and the name/routine agreement
    for addr, (name, src) in sorted(by_addr.items()):
        kind, tgt = decode(blob, addr)
        if not check(kind is not None,
                     "%s (0x%04X) is not a slot cell" % (name, addr),
                     "the cell is neither the SLOT nor the JSLOT shape - the "
                     "table has been overwritten or the address is past its end",
                     got=tgt,
                     want="1E0E1F E8.. 1FCB | E9.. 0000000000 | 55BD.. E9.. 00"):
            continue
        names = text_at.get(tgt, [])
        if not check(bool(names),
                     "%s (0x%04X) calls 0x%04X, which is not a .text label"
                     % (name, addr, tgt),
                     "a displacement into the middle of a routine assembles "
                     "cleanly and runs wrong"):
            continue
        stem = name[len("OSAPI_"):].lower()
        want = {ALIAS[name]} if name in ALIAS else {stem, "osapi_" + stem, "api_" + stem}
        check(any(n in want for n in names),
              "%s (0x%04X) reaches %s" % (name, addr, "/".join(names)),
              "the SDK name and the kernel routine have parted. If this is "
              "deliberate, add it to ALIAS in this file so a reviewer sees it",
              got="/".join(names), want=" or ".join(sorted(want)))

    # 5. nothing unaccounted for inside the table
    for addr in range(TABLE_BASE, top + 1, CELL):
        if addr in by_addr or addr in COMPAT:
            continue
        kind, tgt = decode(blob, addr)
        names = text_at.get(tgt, []) if kind else []
        check(False, "cell 0x%04X is in the table and published nowhere" % addr,
              "either it is a new slot whose %define was forgotten - packages "
              "cannot reach it - or a retired one that needs a COMPAT entry here",
              got="%s -> %s" % (kind, "/".join(names) or tgt), want="a %define or a COMPAT row")

    # ...and the ledger must not rot: an ALIAS naming a slot nobody publishes
    # any more is a line that will never be read again.
    for name in ALIAS:
        check(name in by_name, "ALIAS names %s, which no SDK publishes" % name,
              "remove the row - a stale exception silently exempts nothing")

    print("t_api_abi: %d published slots (%d aliased, %d compat), table 0x%04X..0x%04X"
          % (len(by_addr), len(ALIAS), len(COMPAT), TABLE_BASE, top))
    done("t_api_abi")


if __name__ == "__main__":
    main()
