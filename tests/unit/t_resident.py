#!/usr/bin/env python3
"""Does anything the splash's first tick runs JUMP into sectors not yet aboard?
(SPEC.md 15's `SPL_RES_SIZE` guard, SPEC.md 15.1.2's ladder)

    python3 tests/unit/t_resident.py [-v]

Stage 2 ticks the loading screen once the image's first `SPL_RESIDENT` sectors
have landed, and that first tick PROBES THE ADAPTER - `vid_detect`,
`vid_apply`, `vid_setmode`, `gfx_rowbase`, through four far shims that end at
`spw_resident_end`. kernel.asm already asserts that those shims END inside the
window:

    SPL_RES_SIZE equ spw_resident_end - $$
    %if SPL_RES_SIZE > SPL_RESIDENT * 512

**That is a SIZE check, and size is not reach.** A routine can sit wholly
inside the window and still jump to a helper at the far end of `.text`, and
the guard says nothing. kernel.asm's own comment names the failure exactly -
*"a call that leaves it is a call into sectors the floppy has not delivered
yet: no fault, no message, whatever the machine left in that memory"* - and
refuses one such shape on that ground (routing `spw_vid_detect` through
`spl_gate`, sector 104 of 119: *"the PATH is not aboard even though the
destination is"*). Nothing enforced it.

IT HAS BEEN PAID FOR ONCE. SPEC.md 15.1.2's shared epilogue ladder converted
`vid_pop8` - the eight-register epilogue `vid_setmode`, `vid_detect` and
`vid_text` share - from its own pop run to `jmp kret_es`. The ladder is at
`.text` 0xC59C; the window ends at 0x0C10. (This paragraph said 0xC628 and
0x0BCD for a year and two readers quoted them as measurements - they were the
tree the bug was found on. THE RUN PRINTS BOTH: take them from its output, not
from here.) Every adapter came up dead with no
message, and `make`, the fast tier, `stkbalance`, `os88ovlchk`, `t_asmrules`
and `checkdocs` were all green - none of them boots a machine. The first gate
that could see it was `make test-full`.

WHAT IT CHECKS, and it is deliberately narrow: **no `jmp kret*` may sit in
`.text` below `spw_resident_end`.** The three ladders are always at the far end
of their sections, so a rung jump from inside the window is a jump into
unloaded memory, always - there is no benign case to argue about. Measured on
this tree: 62 rung jumps in `.text`, 0 below the line; with `vid_pop8`
converted, exactly 1, at 0x0AF5.

WHAT WAS TRIED FIRST AND REFUSED, because the numbers are the argument. Two
wider rules were written before this one and both were wrong in the loud
direction:

  * *"nothing below the line may transfer above it"* - **218 findings** on a
    tree that boots. The window is not a wall: code is laid out below it that
    has nothing to do with the splash, and a routine that merely STRADDLES the
    boundary jumps across it all day.
  * *a reachability walk from the four shims, following every near
    call/jmp/jcc and fall-through* - **18 findings**, still on a tree that
    boots, because a conditional arm the boot never takes is still an edge in
    the graph and an indirect call cannot be followed at all.

Both would have caught this bug and neither could ever be green, and a gate
that is never green is a gate somebody switches off. The narrow rule is exact.
It does not catch a resident routine reaching a non-resident helper by some
OTHER route - `test-full` is what catches that, as it did here.

THE SECOND CHECK IS ABOUT DATA, and it is what made SPL_RESIDENT safe to
DERIVE rather than argue (the big review's boot-4, BR-153). kernel.asm's foot
computes SPL_RESIDENT from spw_resident_end now - 5 sectors where the hand rung
said 9 - and a label measures where the CODE ends, not what the code NAMES.
The ticks read and write kernel words further up `.text` (vid_setmode's
[vid_cgamode], its VESA arm's [vx_ep]) and in `.bss` ([ui_post]), and a store
there on the first tick lands in a sector stage 2 has not read - or, under
KZIP, on the packed tail lying where `.text` and `.bss` will be: SPEC.md
2.9.4's `ui_post` incident, which turned an 01 into menu_track's first compare.

So this walks the ticks' code - everything reachable from spl_tick and spl_isr
through near calls, jumps, BLOBCALL and the spw_near door (whose target is the
BP loaded before it), plus fall-through - and collects every DS memory operand
naming a kernel label. A `cs:` override counts as DS in every proc outside
`.boot2`/`.boot3`: behind spw_near's far door CS = KERNEL_SEG = DS, and the
geometry words gfx_rowbase reads through CS on every tick ([cs:vid_stride],
kern_mega's [cs:k_vsa]) are exactly the words a moved label would strand. One that lands in `.text` at or past SPL_RESIDENT * 512,
or in `.bss`/`.lowbss`, FAILS unless tests/tickrefs.txt names it with the
reason no tick reaches it on a partial kernel. The walk OVER-approximates (an
arm the boot never takes is still an edge), which is the safe direction for a
refusal and the reason the registry exists. It runs on every kernel tree that
is built - kern_big always, kern_small, kern_mega and kern_emu when present -
against each tree's own map, because each has its own window.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# A listing carries every section and their offsets OVERLAP - `.cold`, `.boot2`,
# `.modc` and `.text` all start at 0 - so an address alone says nothing about
# which section it is in. The `<N>` is nasm's include depth, and leaving it out
# of this pattern is what made the first draft read three `.cold` sites as
# `.text` ones.
LST = re.compile(r"^\s*\d+\s+([0-9A-F]{8})\s+([0-9A-F]+)")
SEC = re.compile(r"^\s*\d+\s+(?:<\d+>\s+)?(?:;;;\s+)?section\s+(\.\S+)")
RUNG = re.compile(r"\bjmp (kretf?c?_[a-z]{2})\b")


# (tree under build/, the defines that make it that kernel). kern_big always;
# the others when their tree has been built (`make` builds smallk; megak and
# emuk are `make mega` / `make emu`'s)
TREES = [("", []), ("smallk", ["-DKERN_SMALL"]), ("megak", ["-DKERN_MEGA"]),
         ("emuk", ["-DKERN_EMU"])]


def start_listing(build, defines):
    """-Le: the PREPROCESSED text of every macro expansion, so a `BLOBCALL x`
    or VID_APPLY's `%1 %+ _apply:` is listed as what it assembles to."""
    out = os.path.join(build, "t_resident.lst")
    cmd = ["nasm", "-f", "bin", "-w+error", "-Le"] + defines + [
           "-I", os.path.join(ROOT, "kernel") + os.sep,
           "-I", os.path.join(ROOT, "apps") + os.sep,
           "-I", build + os.sep,
           "-l", out, "-o", out + ".bin",
           os.path.join(ROOT, "kernel", "kernel.asm")]
    return out, subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True, cwd=ROOT)


def finish_listing(out, proc):
    _o, err = proc.communicate()
    if proc.returncode != 0:
        tail = (err.strip().splitlines() or ["(no stderr)"])[-1]
        raise SystemExit("t_resident: the kernel does not assemble, so there "
                         "is no listing to read: %s" % tail[:200])
    try:
        os.remove(out + ".bin")
    except OSError:
        pass
    return open(out, errors="replace").read().split("\n")


def readmap(path):
    """{label: (section, virtual address)} and {equ: value} off a NASM map."""
    sym, absv, sec = {}, {}, None
    for ln in open(path, errors="replace"):
        m = re.match(r"^---- (?:Section (\S+)|No Section)", ln)
        if m:
            sec = m.group(1)
            continue
        if sec is None:
            m = re.match(r"^([0-9A-F]{8})\s+(\S+)\s*$", ln)
            if m:
                absv[m.group(2)] = int(m.group(1), 16)
            continue
        m = re.match(r"^\s*[0-9A-F]+\s+([0-9A-F]+)\s+(\S+)\s*$", ln)
        if m:
            sym[m.group(2)] = (sec, int(m.group(1), 16))
    return sym, absv


def window(lines):
    """spw_resident_end's .text offset, out of the listing.

    The label carries no bytes of its own, so it may be listed with the
    address of whatever follows it - either reading is the same boundary.
    """
    for i, ln in enumerate(lines):
        if not re.search(r"\bspw_resident_end(:|\s+equ\s+\$)", ln) or \
                ln.lstrip().startswith(";") or ";;;" in ln:
            continue
        m = LST.match(ln)
        if m:
            return int(m.group(1), 16)
        for nxt in lines[i + 1:]:
            m = LST.match(nxt)
            if m:
                return int(m.group(1), 16)
        break
    raise SystemExit("t_resident: spw_resident_end is not in the listing - the "
                     "label this check hangs on has moved or gone (SPEC.md 15)")


# The fewest ladder jumps the walk may find and still be believed (see
# rungs()), and the fewest data references the tick walk may find: a pattern
# that stopped reading the listing found nothing and passed, once
RUNG_FLOOR = 40
REF_FLOOR = 15

# --- the tick walk (the data check) -----------------------------------------
TLINE = re.compile(r"^\s*\d+\s+(?:([0-9A-F]{8})\s+([0-9A-F\[\]()]+)-?\s+)?"
                   r"(?:<\d+>\s?)?(.*)$")
TSEC = re.compile(r"^section\s+(\.\S+)", re.I)
TLAB = re.compile(r"^([A-Za-z_]\w*)(:?)")
UNCOND = re.compile(r"^(?:jmp|ret|retf|iret)\b", re.I)
XFER = re.compile(r"^(call|jmp|j[a-z]{1,4}|loop\w*|BLOBCALL)\s+"
                  r"(?:short\s+|near\s+)?([A-Za-z_]\w*)\b", re.I)
MEM = re.compile(r"\[([^\]]+)\]")
IDENT = re.compile(r"[A-Za-z_]\w*")
STORE = re.compile(r"^(mov|inc|dec|add|sub|and|or|xor|not|neg|shl|shr|sal|"
                   r"sar|rol|ror|rcl|rcr|xchg|pop|adc|sbb)\b", re.I)
ROOTS = ("spl_tick", "spl_isr")
# ...and what the walk must reach, or it walked nothing real: the first
# tick's own probe, both halves of the far door, and the IRQ0 frame
MUST = ("spl_chrome", "vid_detect", "bvs_apply", "vid_setmode",
        "gfx_rowbase", "spl_anim")


def procs(lines, sym):
    """{proc: [(text, emitted)]} and the order procs appear in, per section.
    A proc is a column-0 label (a `.local` belongs to the one above); with
    -Le a macro's expansion is the `;;;` line, which carries its bytes."""
    body, order, cur, csec, inmac = {}, [], None, ".text", False
    for raw in lines:
        m = TLINE.match(raw)
        if not m:
            continue
        byts, text = m.group(2), m.group(3)
        # a %macro DEFINITION is listed verbatim and assembles nothing: its
        # body belongs to whatever expands it, never to the proc above it
        head = text.lstrip().lower()
        if head.startswith(("%macro", "%imacro")):
            inmac = True
            continue
        if head.startswith("%endmacro") or head.startswith("%endm"):
            inmac = False
            continue
        if inmac:
            continue
        if text.lstrip().startswith(";;;"):
            text = text.lstrip()[4:]
        elif byts is None and "%" in text:
            continue                    # the unexpanded macro body line
        t = text.split(";", 1)[0].rstrip()
        st = t.strip()
        d = TSEC.match(st)
        if d:
            csec = d.group(1)
            continue
        if not st or st.startswith("%"):
            continue
        lm = TLAB.match(t)
        if lm and (lm.group(2) or sym.get(lm.group(1), ("",))[0] == csec):
            cur = lm.group(1)
            order.append((cur, csec))
            body.setdefault(cur, [])
            rest = t[lm.end():].strip()
            if rest:
                body[cur].append((rest, bool(byts)))
            continue
        if cur:
            body[cur].append((st, bool(byts)))
    return body, order


def tick_refs(lines, sym):
    """Every DS data reference the ticks can make: [(proc, label, store?,
    text)], and the set of procs walked."""
    body, order = procs(lines, sym)
    # a `cs:` override is the BLOB's own data only in the blob's sections
    # (CS = the blob's segment there). Everywhere else the ticks reach - the
    # `.text` procs behind spw_near's far door - CS = KERNEL_SEG = DS, so
    # `[cs:vid_stride]` is a KERNEL_SEG reference like any other and is
    # checked like one (gfx_rowbase reads four such words on every tick)
    psec = dict(order)
    nxt = {}
    for i, (p, sc) in enumerate(order[:-1]):
        if order[i + 1][1] == sc:
            nxt.setdefault(p, order[i + 1][0])
    seen, todo, refs = set(), list(ROOTS), []
    while todo:
        p = todo.pop()
        if p in seen or p not in body:
            continue
        seen.add(p)
        bp, last = None, False
        for st, emitted in body[p]:
            mm = re.match(r"^mov\s+bp\s*,\s*([A-Za-z_]\w*)$", st, re.I)
            if mm:
                bp = mm.group(1)
            x = XFER.match(st)
            if x and (emitted or x.group(1).upper() == "BLOBCALL"):
                todo.append(x.group(2))
            if "spw_near" in st and bp:
                todo.append(bp)         # the far door runs what BP names
            if emitted:
                last = bool(UNCOND.match(st))
            # ...and an ADDRESS loaded for a string op or a pointer walk:
            # `mov di, mouse_x / stosw` stores through ES:DI = DS:DI and has
            # no bracket to find (BP is the door's target, a code edge above)
            mm = re.match(r"^mov\s+(si|di|bx)\s*,\s*([A-Za-z_]\w*)$", st, re.I)
            if mm and mm.group(2) in sym and emitted:
                refs.append((p, mm.group(2), True, st))
            for mo in MEM.finditer(st):
                op = mo.group(1).replace(" ", "").lower()
                if op.startswith("cs:") and \
                        psec.get(p) in (".boot2", ".boot3"):
                    continue            # the blob's own segment
                if op.startswith(("es:", "ss:")) or \
                        (op.startswith("bp") and "ds:" not in op):
                    continue
                ops = st.split(None, 1)
                first = ops[1].split(",")[0] if len(ops) > 1 else ""
                store = bool(STORE.match(st)) and mo.group(0) in first
                for w in IDENT.findall(mo.group(1)):
                    if w in sym:
                        refs.append((p, w, store, st))
        if not last and p in nxt:
            todo.append(nxt[p])
    return refs, seen


def registry():
    out = {}
    path = os.path.join(ROOT, "tests", "tickrefs.txt")
    for ln in open(path):
        ln = ln.split("#", 1)[0].strip()
        if ln:
            name, _, why = ln.partition(" ")
            out[name] = why.strip()
    return out


def rungs(lines, end, tag, verbose):
    sec, below, above = ".text", [], 0
    for ln in lines:
        d = SEC.match(ln)
        if d:
            sec = d.group(1)
            continue
        if sec != ".text" or ln.lstrip().startswith(";"):
            continue
        if not RUNG.search(ln):
            continue
        m = LST.match(ln)
        if not m:
            continue                        # the macro line, not an emission
        addr = int(m.group(1), 16)
        if addr < end:
            below.append((addr, ln.strip()[:110]))
        else:
            above += 1

    print("t_resident[%s]: the resident window is %d bytes of .text "
          "(0x0000..0x%04X, spw_resident_end); %d ladder jump(s) in .text, "
          "%d of them inside it" % (tag, end, end, above + len(below),
                                    len(below)))
    if verbose and not below:
        print("  (a rung jump above the line is fine: by then the whole image "
              "is aboard)")

    # A FLOOR on what was found. The listing's `jmp kret_xx` lines are what
    # this row reads, and a pattern that stopped matching them - a renamed
    # ladder, a listing format change - found 0 jumps and 0 inside the window
    # and PASSED. There are ~62 on kern_big; 40 leaves room for a size pass
    # and none for a blind one.
    total = above + len(below)
    if total < RUNG_FLOOR:
        print("  FAIL: only %d ladder jump(s) found in .text; the shipped "
              "kernel has ~62, so the pattern is no longer reading the "
              "listing (RUNG/LST/SEC in this file) and the check above "
              "passed over nothing" % total)
        return 1
    if below:
        for addr, txt in below:
            print("  FAIL: 0x%04X is inside the resident window and jumps to a "
                  "ladder rung" % addr)
            print("        line: %s" % txt)
        print("        why:  the ladders are at the far end of their sections, "
              "so this jumps into memory the floppy has not delivered when the "
              "splash first ticks. There is no fault and no message - the "
              "machine executes what was already there and dies with a blank "
              "screen on every adapter. Give the routine back its own pop run "
              "(SPEC.md 15, 15.1.2).")
        return 1
    return 0


def data(lines, sym, absv, reg, tag, verbose):
    spl = absv.get("SPL_RESIDENT")
    if spl is None:
        print("  FAIL[%s]: the map lists no SPL_RESIDENT - kernel.asm's foot "
              "derives it, so this map is not this source's" % tag)
        return 1, set()
    win = spl * 512
    refs, seen = tick_refs(lines, sym)
    miss = [p for p in MUST if p not in seen]
    if miss or len(refs) < REF_FLOOR:
        print("  FAIL[%s]: the tick walk reached %d proc(s) and %d data "
              "reference(s), and not %s - the walk is no longer reading the "
              "listing, so the check below would pass over nothing"
              % (tag, len(seen), len(refs), ", ".join(miss) or "enough"))
        return 1, set()
    out, used = {}, set()
    for p, w, store, st in refs:
        sec, va = sym[w]
        if sec == ".text" and va < win:
            continue                    # aboard before the first tick
        if sec in (".boot2", ".boot3"):
            continue                    # the blob's own (DS = CS there)
        if sec != ".text" and not sec.endswith("bss"):
            continue                    # another segment's: not DS's
        out.setdefault(w, [sec, va, set(), set()])
        out[w][2].add(p)
        out[w][3].add("W" if store else "R")
    bad = 0
    for w, (sec, va, ps, rw) in sorted(out.items(), key=lambda a: a[1][1]):
        if w in reg:
            used.add(w)
            if verbose:
                print("  ok[%s]: %s %s 0x%04X (%s) by %s - %s"
                      % (tag, "".join(sorted(rw)), sec, va, w,
                         ", ".join(sorted(ps)), reg[w]))
            continue
        bad += 1
        print("  FAIL[%s]: the ticks %s [%s], %s 0x%04X - past the %d-sector "
              "resident window (0x%04X) - in %s"
              % (tag, "WRITE" if "W" in rw else "read", w, sec, va, spl, win,
                 ", ".join(sorted(ps))))
    if bad:
        print("        why:  stage 2 ticks the splash once SPL_RESIDENT sectors "
              "are aboard, and under KZIP everything past them is the packed "
              "tail lying where `.text` and `.bss` will be (SPEC.md 2.9.4, "
              "2.9.13): a read there is garbage and a store is lost at "
              "kz_expand or lands on a packed byte. Move the word below "
              "spw_resident_end, or if no tick can reach it on a partial "
              "kernel, say why in tests/tickrefs.txt.")
    print("t_resident[%s]: the ticks walk %d procs and make %d data "
          "reference(s); %d past the %d-sector window, %d of them explained "
          "in tests/tickrefs.txt" % (tag, len(seen), len(refs), len(out), spl,
                                     len(out) - bad))
    return (1 if bad else 0), used


def main():
    verbose = "-v" in sys.argv
    reg = registry()
    jobs = []
    for sub, defs in TREES:
        build = os.path.join(ROOT, "build", sub)
        mapf = os.path.join(build, "kernel.map")
        if sub and not os.path.exists(mapf):
            print("t_resident[%s]: not built - skipped" % sub)
            continue
        jobs.append((sub or "kern_big", mapf) + start_listing(build, defs))
    rc, used = 0, set()
    for tag, mapf, out, proc in jobs:
        lines = finish_listing(out, proc)
        sym, absv = readmap(mapf)
        rc |= rungs(lines, window(lines), tag, verbose)
        r, u = data(lines, sym, absv, reg, tag, verbose)
        rc |= r
        used |= u
    stale = sorted(set(reg) - used)
    if stale and not rc:
        # the registry only turns one way: a line nothing needs is a line
        # that would excuse the NEXT reference of that name unread
        print("  FAIL: tests/tickrefs.txt explains %s, which no tick reaches "
              "past the window on any tree built here - take the line out"
              % ", ".join(stale))
        rc = 1
    print("t_resident: %s" % ("FAILED" if rc else "2 checks passed on %d "
                              "kernel(s)" % len(jobs)))
    return rc


if __name__ == "__main__":
    sys.exit(main())
