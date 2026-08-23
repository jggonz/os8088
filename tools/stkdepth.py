#!/usr/bin/env python3
"""stkdepth: the deepest call chain in a driver or a package, statically.

    python3 tools/stkdepth.py drivers/ether/ether.asm --from eth_pump
    python3 tools/stkdepth.py drivers/ether/ether.asm            # every root

`tools/stkwater.py` says HOW DEEP a task slice went; this says WHERE, and it
says it without an emulator, a network or a lucky sample. `SCH_STACK` is 256
bytes for every background task (SPEC.md 8) and `ETHER.DRV`'s service worker
has been measured at 208 of them, so "which chain do we cut" is a question with
a numeric answer and this is what answers it.

--- WHAT IT COUNTS ---------------------------------------------------------
The NASM listing, not the source: `%if` is already resolved and every byte has
an address, so a routine that is not assembled cannot be counted. Per global
label, the instruction stream is walked LINEARLY - `push`/`pushf` +2,
`pop`/`popf` -2, `sub sp, n` +n, `call` +2 for the return address (+4 for a far
call, which is how the API is reached) - and every call site is recorded with
the depth it happens at. The routine's cost is then the deepest of them.

--- WHAT THAT APPROXIMATION GETS WRONG -------------------------------------
**Branches.** A linear walk cannot know that `.err` popped three registers and
jumped to the exit; where a routine's paths differ, the number it prints is the
one for the fall-through path. This code style - push the set at entry, pop it
at every exit - is what makes that close, and the answer is checked against
`stkwater.py`'s measured high water rather than trusted.

**Recursion** is reported and then cut, because a cycle has no static maximum.
**Indirect calls** (`call [bx]`) cannot be resolved and are counted as their 2
bytes and nothing below them - so a dispatch through a table UNDERSTATES, and
the report names every one it found.

--- CHECKED AGAINST THE MEASUREMENT ----------------------------------------
`eth_pump` reads 140 bytes here; `tools/stkwater.py` measured the slice that
runs it at 208 after a whole FTP session, and the difference is the kernel's:
`sch_isr`'s 24, `khb_paint`'s 16 under `KFZ=1`, the BIOS `int 08h` chain, and
the far entry the driver was called through. Two instruments agreeing within
what the third one explains is what makes either of them usable.

--- AND WHAT IT IS FOR -----------------------------------------------------
`--leaf NAME` prices a change before anybody writes it: it counts NAME as
costing nothing below its call, which is the "what if this stopped nesting
here" question. `--leaf ip_start --leaf ip_finish` is "what if a receive
handler STAGED its reply instead of sending it inline" - and the answer, 140
down to 126, is why that change was not worth making on its own.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

LBL = re.compile(r"^([A-Za-z_][\w]*)$")
CALL = re.compile(r"\bcall\s+(?:(far|near)\s+)?([A-Za-z_.][\w.]*)\s*$")
CALLIND = re.compile(r"\bcall\s+(?:far\s+|near\s+)?[\[A-Z]")
PUSH = re.compile(r"^\s*(push|pushf)\b")
POP = re.compile(r"^\s*(pop|popf)\b")
SUBSP = re.compile(r"^\s*sub\s+sp\s*,\s*(\w+)")
ADDSP = re.compile(r"^\s*add\s+sp\s*,\s*(\w+)")
INT = re.compile(r"^\s*int\s+")
RET = re.compile(r"^\s*ret(f|n)?\b")


def assemble(asm, includes):
    d = tempfile.mkdtemp()
    src = os.path.join(d, "a.asm")
    mp = os.path.join(d, "a.map")
    lst = os.path.join(d, "a.lst")
    out = os.path.join(d, "a.bin")
    shutil.copy(asm, src)
    with open(src, "a") as f:
        f.write("\n[map all %s]\n" % mp)
    cmd = ["nasm", "-f", "bin", "-w+error"]
    for i in includes:
        cmd += ["-I", i]
    cmd += ["-l", lst, "-o", out, src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("stkdepth: nasm failed:\n" + r.stderr)
    return lst, mp


def symbols(mp):
    """{offset: name} for every GLOBAL label - locals belong to their parent."""
    out = {}
    for line in open(mp):
        m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if m and LBL.match(m.group(3)):
            out[int(m.group(1), 16)] = m.group(3)
    return out


def routines(lst, syms):
    """(routines, indirect sites, entry pushes, self-written regs)."""
    starts = sorted(syms)

    def owner(addr):
        lo, hi = 0, len(starts) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            if starts[mid] <= addr:
                best = starts[mid]
                lo = mid + 1
            else:
                hi = mid - 1
        return syms.get(best)

    rout, cur, d, mx = {}, None, 0, 0
    pushes, pops, owns = {}, {}, {}          # BALANCED pushes = "saved"; a
    indirect = []                            # register written any other way
                                             # is one the routine owns
    for line in open(lst, errors="replace"):
        m = re.match(r"^\s*\d+\s+([0-9A-F]{8})\s+(.*)$", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        rest = m.group(2)
        hexcol = re.match(r"^([0-9A-F]+)", rest)
        hexcol = hexcol.group(1) if hexcol else ""
        # THE BYTE COLUMN IS NOT ALWAYS PLAIN HEX. An instruction with a
        # relocatable memory operand lists as `[9434]01`, and a parser that
        # only eats [0-9A-F]+ leaves that in the source text - so `mov al,
        # [dhcp_try]` stops looking like a write to AX and every routine that
        # loads from memory reads as clobbering nothing.
        inc = re.search(r"<\d+>", rest)
        if inc:
            text = rest[inc.end():]
        else:
            text = re.sub(r"^[0-9A-F\[\]<>()-]+\s*", "", rest)
        text = text.split(";")[0].rstrip()
        if not text.strip():
            continue
        who = owner(addr)
        if who is None:
            continue                    # bytes before the first global label
        if who != cur:
            if cur is not None:
                rout[cur] = (mx, rout.get(cur, (0, []))[1])
            cur, d, mx = who, 0, 0
            rout.setdefault(cur, (0, []))
            pushes.setdefault(cur, {})
            pops.setdefault(cur, {})
            owns.setdefault(cur, set())
        if PUSH.match(text):
            d += 2
            r = _reg(text.split()[-1])
            if r:
                pushes[cur][r] = pushes[cur].get(r, 0) + 1
        elif POP.match(text):
            d -= 2
            r = _reg(text.split()[-1])
            if r:
                pops[cur][r] = pops[cur].get(r, 0) + 1
        elif SUBSP.match(text):
            try:
                d += int(SUBSP.match(text).group(1), 0)
            except ValueError:
                pass
        elif ADDSP.match(text):
            try:
                d -= int(ADDSP.match(text).group(1), 0)
            except ValueError:
                pass
        if not PUSH.match(text) and not POP.match(text):
            for r in writes(text):
                owns[cur].add(r)   # ...and a `pop` is NEVER a write here: it
                                   # is the other half of a push, and counting
                                   # the epilogue as "the routine writes AX"
                                   # made every save look necessary and the
                                   # whole report read as a clean bill
        cm = CALL.search(text)
        if cm:
            far = cm.group(1) == "far"
            # **RESOLVE BY ADDRESS, not by the name in the source.** A
            # `%define prof_now pit_now` leaves `call prof_now` in the listing
            # text and no such label in the map, so a name-based edge reads as
            # "not in this image" and the callee preserves NOTHING - which
            # silently forbade every caller above it from dropping a save. The
            # encoded target cannot lie: E8 is call rel16, three bytes.
            target = cm.group(2)
            if len(hexcol) >= 6 and hexcol[:2] == "E8":
                rel = int(hexcol[4:6] + hexcol[2:4], 16)
                if rel > 0x7FFF:
                    rel -= 0x10000
                dest = (addr + 3 + rel) & 0xFFFF
                if dest in syms:
                    target = syms[dest]
            rout[cur][1].append((d + (4 if far else 2), target,
                                 "far" if far else "near"))
        elif CALLIND.search(text):
            indirect.append((cur, addr))
            rout[cur][1].append((d + 4, None, "indirect"))
        elif INT.match(text):
            mx = max(mx, d + 6)
        mx = max(mx, d)
        rout[cur] = (mx, rout[cur][1])
    if cur is not None:
        rout[cur] = (mx, rout[cur][1])
    # A register is SAVED where its pushes and pops balance - which is this
    # tree's idiom (push the set at entry, pop it at every exit) and survives
    # a routine that saves something in the middle of itself.
    saves = {}
    for n in rout:
        # `pops >= pushes`, not `==`: this tree's routines have SEVERAL exits
        # and each one pops the whole set, so dhcp_timer pushes AX once and
        # pops it twice. Demanding equality called that "not saved", which
        # poisoned every caller of it in the fixpoint.
        saves[n] = [r for r in R16
                    if pushes.get(n, {}).get(r, 0) > 0
                    and pops.get(n, {}).get(r, 0) >= pushes[n][r]]
        # `owns` stays RAW - a register the routine both saves and scribbles on
        # is still one it scribbles on. Subtracting the saves here made the
        # "did it need to push this" test vacuous for every saved register,
        # which is every register the test is about, and the report claimed 50
        # bytes of waste it could not have known about.
    return rout, indirect, saves, owns


R16 = ("ax", "bx", "cx", "dx", "si", "di", "bp", "es", "ds")
R8 = {"al": "ax", "ah": "ax", "bl": "bx", "bh": "bx", "cl": "cx", "ch": "cx",
      "dl": "dx", "dh": "dx"}
# mnemonics whose FIRST operand is written
W1 = ("mov", "xor", "add", "sub", "and", "or", "adc", "sbb", "inc", "dec",
      "neg", "not", "shl", "shr", "sal", "sar", "rol", "ror", "rcl", "rcr",
      "lea", "pop", "xchg", "les", "lds", "in", "movzx", "movsx")
# ...and the ones that write registers named nowhere in the operands
IMPLICIT = {"mul": ("ax", "dx"), "imul": ("ax", "dx"), "div": ("ax", "dx"),
            "idiv": ("ax", "dx"), "cbw": ("ax",), "cwd": ("ax", "dx"),
            "xlat": ("ax",), "lodsb": ("ax", "si"), "lodsw": ("ax", "si"),
            "stosb": ("di",), "stosw": ("di",), "movsb": ("si", "di"),
            "movsw": ("si", "di"), "scasb": ("di",), "scasw": ("di",),
            "cmpsb": ("si", "di"), "cmpsw": ("si", "di"), "loop": ("cx",),
            "loope": ("cx",), "loopne": ("cx",)}


def _reg(tok):
    tok = tok.strip().lower()
    if tok in R16:
        return tok
    return R8.get(tok)


def writes(text):
    """The 16-bit registers this one instruction writes. Conservative."""
    t = text.strip().lower()
    if not t:
        return ()
    if t.startswith("rep"):
        parts = t.split(None, 1)
        out = {"cx"}
        if len(parts) > 1:
            out |= set(writes(parts[1]))
        return tuple(out)
    mn = t.split()[0]
    out = set(IMPLICIT.get(mn, ()))
    if mn in W1:
        ops = t[len(mn):].split(",")
        if ops:
            r = _reg(ops[0])
            if r:
                out.add(r)
        if mn == "xchg" and len(ops) > 1:
            r = _reg(ops[1])
            if r:
                out.add(r)
        if mn in ("les", "lds"):
            out.add("es" if mn == "les" else "ds")
    return tuple(out)


def redundant(rout, saves, owns, name, memo=None):
    """Which of `name`'s entry pushes it never needed.

    A push is redundant when the routine does not write the register ITSELF and
    every routine it calls gives it back. `preserves` is a fixpoint over the
    call graph, seeded pessimistically - an unresolved or indirect callee
    preserves nothing, and a cycle preserves nothing - so the answer is a
    LOWER bound on the waste and never an upper one.

    **It is a report and not a rewrite.** A push removed on a wrong answer here
    is silent corruption in a driver, so every line it prints is a candidate to
    read, not a patch to apply.
    """
    pres = {}
    for n in rout:
        pres[n] = set(saves.get(n, ()))
    for _ in range(12):                       # fixpoint; the graph is shallow
        changed = False
        for n, (own, calls) in rout.items():
            keep = set()
            for r in R16:
                if r in saves.get(n, ()):
                    keep.add(r)
                    continue
                if r in owns.get(n, ()):
                    continue
                ok = True
                for _at, callee, kind in calls:
                    if callee is None or callee.startswith("."):
                        ok = ok and callee is not None
                        continue
                    if callee not in rout or r not in pres.get(callee, ()):
                        ok = False
                        break
                if ok:
                    keep.add(r)
            if keep != pres[n]:
                pres[n] = keep
                changed = True
        if not changed:
            break
    out = []
    for r in saves.get(name, ()):
        if r in owns.get(name, ()):
            continue
        if all(c is None or c.startswith(".") or r in pres.get(c, ())
               for _a, c, _k in rout[name][1]):
            out.append(r)
    return out, pres


def deepest(rout, name, seen=(), leaves=()):
    """(bytes, [chain]) below and including `name`'s own frame.

    `leaves` are routines to treat as costing nothing below their call - the
    "what if this stopped nesting here" question, which is how a chain that
    calls the transmit half from inside the receive half gets priced against
    one that defers it.
    """
    if name in leaves:
        return 0, ["%s (CUT: counted as a leaf)" % name]
    if name in seen:
        return 0, ["%s (RECURSION - cut here, no static maximum)" % name]
    own, calls = rout.get(name, (0, []))
    best, chain = own, []
    for at, callee, kind in calls:
        if callee is None:
            sub, sc = 0, ["<indirect - UNRESOLVED, nothing counted below it>"]
        elif callee.startswith("."):
            continue                               # intra-routine
        elif callee not in rout:
            sub, sc = 0, ["%s (not in this image - API/kernel)" % callee]
        else:
            sub, sc = deepest(rout, callee, seen + (name,), leaves)
        if at + sub > best:
            best, chain = at + sub, ["%s -> %s (+%d here)" % (name, callee, at)] + sc
    return best, chain


MARK = re.compile(r";\s*STKDEPTH-NOSAVE:\s*([a-z ]+?)\s*(?:;|$)")


def marks(asm, includes):
    """{routine: [regs]} declared by `; STKDEPTH-NOSAVE:` in the SOURCE.

    **A routine that stops saving a register stops being locally correct.**
    SPEC.md 1 has every public routine preserve everything precisely so that a
    change inside a callee cannot break a caller three levels up - and dropping
    a push trades that for bytes. The trade is worth making in exactly one
    place, the network stack's deepest chain, and only if it is CHECKED: the
    marker says which registers a routine is relying on its callees for, and
    `--check` fails the build the day one of them stops preserving one.

    The marker sits on the routine's own label line, so it is impossible to
    read the code without reading the claim.
    """
    out = {}
    for inc in includes + [os.path.dirname(asm) + "/"]:
        if not os.path.isdir(inc):
            continue
        for f in sorted(os.listdir(inc)):
            if not f.endswith((".asm", ".inc")):
                continue
            for line in open(os.path.join(inc, f), errors="replace"):
                m = MARK.search(line)
                if not m:
                    continue
                lbl = re.match(r"^([A-Za-z_][\w]*):", line)
                if not lbl:
                    sys.exit("stkdepth: a STKDEPTH-NOSAVE marker must sit on "
                             "the routine's own label line - %s" % line.strip())
                out[lbl.group(1)] = m.group(1).split()
    return out


def check(rout, saves, owns, declared):
    """Every declared drop still holds. Returns a list of failures."""
    bad = []
    for name, regs in sorted(declared.items()):
        if name not in rout:
            bad.append("%s: no such routine in this image" % name)
            continue
        ok, pres = redundant(rout, saves, owns, name)
        for r in regs:
            if r in owns.get(name, ()):
                bad.append("%s must save %s: it WRITES it itself now" % (name, r))
                continue
            for _at, callee, kind in rout[name][1]:
                if callee is None:
                    bad.append("%s must save %s: it now makes an INDIRECT call"
                               % (name, r))
                    break
                if callee.startswith("."):
                    continue
                if callee not in rout:
                    bad.append("%s must save %s: callee %s is not in this image"
                               % (name, r, callee))
                    break
                if r not in pres.get(callee, ()):
                    bad.append("%s must save %s: %s no longer preserves it"
                               % (name, r, callee))
                    break
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("asm")
    ap.add_argument("--from", dest="roots", action="append", default=[],
                    help="a root to walk from (repeatable); default: the "
                         "deepest routine in the image")
    ap.add_argument("-I", dest="inc", action="append", default=[])
    ap.add_argument("--leaf", action="append", default=[],
                    help="treat this routine as costing nothing below it - "
                         "the 'what if we stopped nesting here' question")
    ap.add_argument("--check", action="store_true",
                    help="verify every `; STKDEPTH-NOSAVE:` marker in the "
                         "source and exit - the build gate")
    ap.add_argument("--top", type=int, default=12,
                    help="how many roots to list when none is named")
    a = ap.parse_args()
    inc = a.inc or [os.path.dirname(a.asm) + "/", "drivers/net/", "drivers/",
                    "apps/"]
    lst, mp = assemble(a.asm, inc)
    syms = symbols(mp)
    rout, indirect, saves, owns = routines(lst, syms)
    if a.check:
        declared = marks(a.asm, inc)
        bad = check(rout, saves, owns, declared)
        for b in bad:
            print("stkdepth: FAIL %s" % b)
        n = sum(len(v) for v in declared.values())
        print("stkdepth: %d dropped save(s) across %d routine(s) in %s - %s"
              % (n, len(declared), a.asm, "ALL STILL HOLD" if not bad
                 else "%d BROKEN" % len(bad)))
        return 1 if bad else 0
    print("stkdepth: %s - %d routines" % (a.asm, len(rout)))
    if indirect:
        print("stkdepth: %d INDIRECT call site(s); nothing below them is "
              "counted:" % len(indirect))
        for who, addr in indirect[:8]:
            print("          %s at 0x%04X" % (who, addr))
    leaves = tuple(a.leaf)
    roots = a.roots or sorted(
        rout, key=lambda n: -deepest(rout, n, (), leaves)[0])[:a.top]
    for r in roots:
        n, chain = deepest(rout, r, (), leaves)
        print("\n== %s: %d bytes ==" % (r, n))
        for step in chain:
            print("   " + step)
        # ...and what each level on that chain saved without needing to
        print("   --- entry pushes this chain did not need ---")
        total = 0
        for step in chain:
            who = step.split(" ->")[0].strip()
            if who not in rout:
                continue
            red, _ = redundant(rout, saves, owns, who)
            if red:
                total += 2 * len(red)
                print("   %-16s %s  (%d bytes)"
                      % (who, " ".join(red), 2 * len(red)))
        print("   %d bytes of %d are pushes the routine never needed to make"
              % (total, n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
