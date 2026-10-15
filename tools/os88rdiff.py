#!/usr/bin/env python3
"""os88rdiff - what a size pass did to every ROUTINE, and whether hot code moved.

    python3 tools/os88rdiff.py apps/video/video.asm e8f9e5a1          # base -> working tree
    python3 tools/os88rdiff.py apps/video/video.asm e8f9e5a1 HEAD     # two commits
    python3 tools/os88rdiff.py apps/video/video.asm e8f9e5a1 -D VP_DIAG
    python3 tools/os88rdiff.py kernel HEAD~3                          # the kernel too
    python3 tools/os88rdiff.py --lst old/video.lst build/video.lst    # two listings you have
    python3 tools/os88rdiff.py apps/video/video.asm e8f9e5a1 \\
            --hot 'vd_.*' --hot 'vp_(rd|ring|hook|adue).*'            # ASSERT the hot path

For every top-level label of a package (or the kernel) it reports, base
against new:

    same      every instruction identical, relocations resolved SYMBOLICALLY
    equ       identical except an `equ` the layout derives (a segment base,
              a size) whose value moved with the layout - the names are shown
    relaxed   identical except for BRANCH FORM: nasm chose short where it
              chose near before (or the other way), so the routine changed
              size without a single source line changing
    changed   anything else
    new/gone  only on one side

...with its address on each side, its SIZE delta, and its address SHIFT and
that shift's PARITY. `--hot` names routines that must come out `same` and
shifted by an EVEN amount (`--align N` for another multiple), and the run
exits 1 naming each one that did not.

WHY IT EXISTS. A size pass on a package whose speed matters is supposed to
edit COLD code only - the UI, open/parse, setup - and leave the play path
alone. Proving that took the Video agent of the first apps size pass
(docs/reports/APPS-SIZE-P1-SET6-2026-10-12.md) two scratch scripts and a
finding nobody had written down: **a cold-only edit is not hot-only-safe,
because NASM re-relaxes branches**. Every `jz label` is assembled short when
the target is in range and as `jnz +3 / jmp near` when it is not (nasm's
own synthesis for a CPU below the 386 - no source line says so), and every
`jmp label` as EB or E9; deleting forty bytes of cold code brings some far
branch IN range, the branch shrinks by one or three bytes, and EVERYTHING
after it - the hot routines included - moves by an odd amount. The routines
are instruction-identical and still not where they were. On a 16-bit bus
(the 8086, a V30, every 286 in vm/) a word fetch at an odd address is two bus
cycles, so an odd shift re-prices every word operand and every jump target
in the loop; Video wrote five branches back by hand to put its play path at
its base parity. A byte-compare of the binaries cannot see any of this - it
reports "everything after the first edit differs" - and a source diff
cannot see the relaxation at all, because no source line changed.

HOW. Each side is assembled with `nasm -Le -Lf -l` and a `[map all]` map (a
commit is exported with `git archive` - its .asm/.inc/.mac files only, never
the 58MB of demo video beside apps/video - and assembled there). Every
listing line that emitted bytes is attributed to the top-level label it falls
under, by SECTION and address. Then each line is normalised:

  * a relocated field - nasm marks one `[....]` (an absolute address) or
    `(....)` (a cross-section one) - is resolved to `label+offset` against
    that build's own map, and two sides agree when any reading agrees. On a
    package there is one section and one reading; on the kernel `.text`,
    `.cold`, `.lowbss` and the modules all start at offset 0, so a value is
    read in every section whose range holds it and the two sets must meet;
  * a RELATIVE branch (E8/E9/EB, Jcc, LOOP/JCXZ, and nasm's synthesised
    `Jncc +3 / JMP near`) is decoded to its target and resolved the same way,
    so a call into code that moved is not a difference and a call that now
    lands somewhere else is.

That is a STRONGER test than masking the displacement, which the scratch
scripts did: masking cannot tell a moved target from a retargeted branch.

THE SECTION A LINE IS IN is the hard part on the kernel, because nasm lists
every SOURCE line, the ones a false `%ifdef` threw away included - so a
`section .modd` line in the listing may never have been assembled. `-Le`
adds a `;;; ` line for exactly what WAS assembled, fully expanded
(`[section .cold vstart=0]`, `zz equ e + 2` out of a macro), and that is what
is read. As a check on all of it, every routine's listed bytes must add up
to its size in the map; a build where they do not is named on stderr. A
plain listing (the Makefile's `STKLST` ones, `build/<pkg>.lst`, for `--lst`)
is read off its source text: right for a one-section package, warned about
on anything else.

FLAGS. The include path and literal `-D`s are read out of the Makefile's own
recipe for the source (each side's Makefile, from its own commit), so
`apps/browser/browser.asm`'s four `-I`s need not be typed. A Makefile
VARIABLE is not expanded (`$(VPDEF)` is a knob, and which arm to compare is
the caller's question): it is named on stderr and `-D` passes it. The kernel
is assembled the way `tools/os88sym.py` assembles it, with the BUILD NUMBER
shadowed to one value on both sides - otherwise every commit is three bytes
of `.text` that differ (SPEC.md 14.2) - and without the packed kernel's KZ_*
defines unless `-D` passes them, so it is the NOKZIP arm's layout of both.
Generated includes (`-I build/`) come from the CURRENT build for both sides.

Scratch goes under build/rdiff/ and is removed afterwards (`--keep` keeps
it). No file outside it is written.
"""
import argparse
import bisect
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

NASM = os.environ.get("NASM", "nasm")


class Fail(Exception):
    pass


# ---------------------------------------------------------------- the map

def parse_map(path):
    """-> (sections, syms, equs).

    sections: name -> dict(vstart, length, nobits)
    syms:     name -> (section, virtual address)   every label, locals too
    equs:     name -> value                        the 'No Section' table
    """
    try:
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        raise Fail("no map at %s (a listing needs its map beside it)" % path)
    sections, syms, equs = {}, {}, {}
    i, n = 0, len(lines)
    while i < n and not lines[i].startswith("-- Sections (summary)"):
        i += 1
    while i < n and not lines[i].startswith("Vstart"):
        i += 1
    i += 1
    while i < n and lines[i].strip():
        f = lines[i].split()
        if len(f) == 6:
            sections[f[5]] = dict(vstart=int(f[0], 16), length=int(f[3], 16),
                                  nobits=(f[4] == "nobits"))
        i += 1
    while i < n and not lines[i].startswith("-- Symbols"):
        i += 1
    cur = None
    for l in lines[i:]:
        m = re.match(r"^---- (No Section|Section (\S+)) -", l)
        if m:
            cur = m.group(2) or ""
            continue
        f = l.split()
        if cur is None or not f:
            continue
        if cur == "" and len(f) == 2 and re.match(r"^[0-9A-F]+$", f[0]):
            equs[f[1]] = int(f[0], 16)
        elif cur and len(f) == 3 and re.match(r"^[0-9A-F]+$", f[1]):
            syms[f[2]] = (cur, int(f[1], 16))
    if not sections:
        raise Fail("%s is not a nasm map" % path)
    return sections, syms, equs


class Build(object):
    """One side: its sections, its labels, and its code per routine."""

    def __init__(self, lst, mapf, label):
        self.label = label
        self.sections, self.syms, self.equs = parse_map(mapf)
        # every label, per section, sorted - for resolving an address
        per = {}
        for name, (sec, a) in self.syms.items():
            if name.startswith("..@"):      # macro-local: renumbered by any
                continue                    # change in macro use, so useless
            per.setdefault(sec, []).append((a, name))
        self.byaddr = {}
        # how far an address can be read as this section's: its length, or
        # - when its labels run PAST its length, which is a package's bss
        # (`OS88_BSS` names it with equs above the image) - all of the 64KB
        self.hi = {}
        for sec, d in self.sections.items():
            self.hi[sec] = d["vstart"] + d["length"]
        for sec, v in per.items():
            v.sort()
            self.byaddr[sec] = ([a for a, _ in v], v)
            if sec in self.hi and v and v[-1][0] > self.hi[sec]:
                self.hi[sec] = 0xFFFFFFFF
        self.multi = sum(1 for s in self.sections.values()
                         if not s["nobits"]) > 1
        lines = self._scan(lst)
        # the ROUTINES: top-level labels in progbits sections - but not a
        # label laid out under `absolute` (a union over some other storage:
        # the kernel's gfx_u members, compress.inc's cmz_ovl) nor an `equ`
        # alias into the middle of something (diskops.inc's clo_s_yn), either
        # of which would cut a real routine's extent in two
        self.routines = {}
        rper = {}
        for name, (sec, a) in self.syms.items():
            if "." in name or name.startswith("..@") or name in self.absl:
                continue
            s = self.sections.get(sec)
            if s is None or s["nobits"] or a >= s["vstart"] + s["length"]:
                continue                # (above the image: a package's bss)
            rper.setdefault(sec, []).append((a, name))
        self.rindex = {}
        for sec, v in rper.items():
            v.sort()
            self.rindex[sec] = ([a for a, _ in v], [nm for _, nm in v])
            end = self.sections[sec]["vstart"] + self.sections[sec]["length"]
            for k, (a, nm) in enumerate(v):
                nxt = v[k + 1][0] if k + 1 < len(v) else end
                self.routines[nm] = dict(sec=sec, addr=a, size=nxt - a,
                                         code=[], nbytes=0)
        self._attribute(lines)

    def misattributed(self):
        """Routines whose listed bytes do not add up to their size in the
        map - the sign that a line was put in the wrong section (or the
        listing is not of this map). Empty is the only right answer."""
        return [n for n, r in self.routines.items()
                if r["nbytes"] != r["size"]]

    # -- resolution
    def resolve(self, v, sec=None):
        """Every reading of `v` as (section, label, offset).

        With `sec`, v is a VIRTUAL address in that section - a relative
        branch's target, which nasm has already resolved. Without, v is a
        relocated field out of the listing, and nasm prints that as the
        target's OFFSET INTO ITS OWN SECTION, not its final value: the
        kernel's `mov al, [sch_fast]` lists as A0[DD01] with sch_fast at
        AB45, .bss starting at A968. So it is read in every section long
        enough to hold it, at that section's vstart."""
        if sec:
            cands = [(sec, v)]
        else:
            cands = [(s, d["vstart"] + v) for s, d in self.sections.items()
                     if d["vstart"] + v <= self.hi[s]]
        out = set()
        for s, va in cands:
            if s not in self.byaddr:
                out.add((s, "", va - self.sections[s]["vstart"]))
                continue
            addrs, items = self.byaddr[s]
            k = bisect.bisect_right(addrs, va) - 1
            if k < 0:
                out.add((s, "", va - self.sections[s]["vstart"]))
                continue
            a = addrs[k]
            while k >= 0 and addrs[k] == a:     # every name at that address
                out.add((s, items[k][1], va - a))
                k -= 1
        if not out:
            out.add(("?", "", v))
        return frozenset(out)

    # -- the listing
    LINE = re.compile(r"^\s*\d+\s+(?:([0-9A-F]{8})\s+(\S+?)(<rep [0-9A-F]+h>)?"
                      r"(?=\s|$)\s*)?(?:<\d+>\s?)?(.*)$")
    SECT = re.compile(r"^\s*(section|segment|absolute)\s+(\S+)", re.I)

    LABEL = re.compile(r"^([A-Za-z_][\w?@$#~]*)(?::|\s+(?:res[bwdqt]|times|"
                       r"d[bwdqt])\b)")

    EQU = re.compile(r"^([A-Za-z_][\w?@$#~]*)\s*:?\s+equ\b", re.I)

    ASM = re.compile(r"^\[(section|segment|absolute)\s+([^\s\]]+)", re.I)

    def _scan(self, lst):
        """Pass 1: the listing's emitted lines as (section, address, field,
        rep, text, line number), with the section each was REALLY in, and
        the names that are not routines (self.absl): an `equ` alias, and a
        label laid out under `absolute`.

        **A LISTING MADE WITH `-Le` IS THE ONE TO TRUST** and this tool
        always makes one. nasm lists every SOURCE line, the ones a false
        `%if` threw away included - so a `section` line read off the source
        text may never have been assembled, and a label a macro made reads
        `%2_sh:` - while `-Le` adds a `;;; ` line for exactly what was
        assembled, fully expanded: `[section .cold vstart=0]`, `zz equ e+2`.
        (`-Lf` with it, or the built-in `section` macro's expansion, being
        `.nolist`, is never shown.) A plain listing - the Makefile's
        `STKLST` ones - falls back to the source text, which is right for a
        one-section package and is warned about on anything else."""
        try:
            text = open(lst, encoding="utf-8", errors="replace").read()
        except OSError:
            raise Fail("no listing at %s" % lst)
        le = re.search(r"^\s*\d+ .*;;; ", text, re.M) is not None
        self.warn = None
        if self.multi and not le:
            self.warn = ("%s was listed without -Le, so a `section` line in a "
                         "FALSE %%if is taken as a switch - section tracking "
                         "may be wrong" % lst)
        self.absl = set()
        out = []
        cur = ".text"
        if ".text" not in self.sections:
            cur = next(s for s, d in self.sections.items() if not d["nobits"])
        lastno = None                   # the line an emitted item is on
        for l in text.splitlines():
            m = self.LINE.match(l)
            if not m:
                continue
            txt = m.group(4)
            lineno = int(l.split(None, 1)[0])
            if le:
                k = txt.find(";;;")
                if k < 0:
                    # an item's continuation chunk (address, no text), or a
                    # source line that is only the listing's echo
                    if not (m.group(1) or m.group(2)) or lineno != lastno:
                        continue
                    src = ""
                else:
                    src = txt[k + 3:].strip()
                    ms = self.ASM.match(src)
                    if ms:
                        cur = None if ms.group(1).lower() == "absolute" \
                            else ms.group(2)
                        continue
            else:
                src = txt.strip()
                ms = self.SECT.match(src)
                if ms:
                    if self.multi:
                        cur = None if ms.group(1).lower() == "absolute" \
                            else ms.group(2)
                    continue
            if src:
                me = self.EQU.match(src)
                if me:                  # `x equ y + 11`: an ALIAS, which
                    self.absl.add(me.group(1))  # the map lists as a label
                    continue
            if cur is None:
                ml = self.LABEL.match(src) if src else None
                if ml:
                    self.absl.add(ml.group(1))
                continue
            field = (m.group(2) or "").rstrip("-")
            if not m.group(1) and not field.startswith("<rep"):
                continue
            if field.startswith("<rep"):         # `<rep N>` on its own line
                out.append((cur, None, "", field + l.split(field, 1)[1]
                            .split(">")[0] + ">", src, lineno))
                continue
            if "?" in field or field.startswith("<"):
                continue                     # nobits: no code
            lastno = lineno
            out.append((cur, int(m.group(1), 16), field, m.group(3) or "",
                        src, lineno))
        return out

    def _attribute(self, lines):
        """Pass 2: each emitted line into its routine, normalised."""
        last = None                 # [routine, line number, item bytes]
        for cur, off, field, rep, src, lineno in lines:
            d = self.sections.get(cur)
            if d is None or d["nobits"]:
                continue
            nrep = int(re.match(r"<rep ([0-9A-F]+)h>", rep).group(1), 16) \
                if rep else 1
            if off is None:             # `<rep N>` alone: the item repeats
                if last and last[1] == lineno:
                    last[0]["nbytes"] += (nrep - 1) * last[2]
                    last[0]["code"].append((((rep,), (), None, None, None),
                                            src))
                continue
            va = d["vstart"] + off
            tok = self._token(cur, va, field, rep)
            if tok is None:
                continue
            ra, rn = self.rindex.get(cur, ([], []))
            k = bisect.bisect_right(ra, va) - 1
            if k < 0:
                continue                     # before any label
            rt = self.routines[rn[k]]
            nb = len(re.sub(r"[\[\]()]", "", field)) // 2
            rt["nbytes"] += nb
            if last and last[0] is rt and last[1] == lineno:
                last[2] += nb
            else:
                last = [rt, lineno, nb]
            # nasm prints `times N` against the ITEM's last chunk, and the
            # item is every chunk on this line: 12 bytes over two chunks,
            # <rep 3h>, is 36 (a one-chunk `00<rep 4h>` is the same rule)
            rt["nbytes"] += (nrep - 1) * last[2]
            code = rt["code"]
            # `jnz .nz / jmp near .done / .nz:` written by hand is the SAME
            # five bytes nasm synthesises for an out-of-range `jz .done` -
            # and writing it back by hand is exactly how a size pass pins a
            # relaxed branch - so the pair is merged into nasm's one line
            if code and tok[2] == "JMP" and tok[0][0] == 0xE9 and \
                    code[-1][0][3] == 3 and len(code[-1][0][0]) == 2 and \
                    0x70 <= code[-1][0][0][0] <= 0x7F and \
                    code[-1][0][4] + 2 == va:
                jc = code[-1][0][0][0]
                tok = ((jc, "J", 0xE9, "R0"), tok[1], "J%02X" % (jc ^ 1),
                       None, va - 2)
                code[-1] = (tok, code[-1][1] + " / " + src)
                continue
            code.append((tok, src))

    def _token(self, sec, va, field, rep):
        """(raw, relocs, canon): raw is a tuple of ints with 'R<n>' standing
        in for each relocated field; relocs the candidate set of each; canon
        the branch-form-blind spelling of a relative branch, or None."""
        raw, relocs = [], []
        i = 0
        while i < len(field):
            c = field[i]
            if c in "[(":
                j = field.index("]" if c == "[" else ")", i)
                h = field[i + 1:j]
                v = int.from_bytes(bytes.fromhex(h), "little")
                raw.append("R%d" % len(relocs))
                relocs.append(self.resolve(v))
                i = j + 1
            else:
                try:
                    raw.append(int(field[i:i + 2], 16))
                except ValueError:
                    return None
                i += 2
        if rep:
            raw.append(rep)
        canon, disp = None, None
        if not relocs and not rep:
            b, t, kind = raw, None, None
            if len(b) == 3 and b[0] in (0xE8, 0xE9):
                t = va + 3 + int.from_bytes(bytes(b[1:3]), "little", signed=True)
                kind = "CALL" if b[0] == 0xE8 else "JMP"
            elif len(b) == 2 and (b[0] == 0xEB or 0x70 <= b[0] <= 0x7F
                                   or 0xE0 <= b[0] <= 0xE3):
                t = va + 2 + int.from_bytes(bytes(b[1:2]), "little", signed=True)
                kind = "JMP" if b[0] == 0xEB else "J%02X" % b[0]
            elif len(b) == 5 and 0x70 <= b[0] <= 0x7F and b[1] == 3 \
                    and b[2] == 0xE9:
                t = va + 5 + int.from_bytes(bytes(b[3:5]), "little", signed=True)
                kind = "J%02X" % (b[0] ^ 1)  # the condition it STANDS for
            if t is not None:
                disp = t - va - len(b)
                # an IP is 16 bits and a near branch WRAPS: `call` from
                # .cold offset 0x466 to 0x8C9D is spelt E8 37 88, -30,665
                t &= 0xFFFF
                raw = [b[0]] + (["J", b[2]] if len(b) == 5 else []) + ["R0"]
                relocs = [self.resolve(t, sec)]
                canon = kind
        # (raw, relocs, canon, displacement, address) - the last two only
        # serve the merge above and take no part in a comparison
        return (tuple(raw), tuple(relocs), canon, disp, va)


# ---------------------------------------------------------------- comparing

def _meet(a, b):
    return all(x & y for x, y in zip(a, b))


def line_eq(t1, t2, equ_old, equ_new, names, relaxed=False):
    """Do two normalised lines say the same thing? `names` collects the
    layout equs it had to excuse."""
    r1, c1, k1 = t1[:3]
    r2, c2, k2 = t2[:3]
    if relaxed and k1 and k2:
        return k1 == k2 and _meet(c1, c2)
    if len(c1) != len(c2) or not _meet(c1, c2):
        return False
    if r1 == r2:
        return True
    if len(r1) != len(r2):
        return False
    diff = [i for i in range(len(r1)) if r1[i] != r2[i]]
    if any(not isinstance(r1[i], int) or not isinstance(r2[i], int)
           for i in diff):
        return False
    got, covered = set(), set()
    for i in diff:
        if i in covered:
            continue
        ok = False
        for s in (i - 1, i):
            if s < 0 or s + 1 >= len(r1):
                continue
            w = (r1[s], r1[s + 1], r2[s], r2[s + 1])
            if not all(isinstance(x, int) for x in w):
                continue
            ow, nw = w[0] | w[1] << 8, w[2] | w[3] << 8
            hit = equ_old.get(ow, set()) & equ_new.get(nw, set())
            if hit:
                got |= hit
                covered |= {s, s + 1}
                ok = True
                break
        if not ok:
            return False
    names |= got
    return True


def compare(a, b):
    """-> rows, in base address order, then the new routines."""
    moved = {n for n in a.equs if n in b.equs and a.equs[n] != b.equs[n]}
    eo, en = {}, {}
    for n in moved:
        eo.setdefault(a.equs[n] & 0xFFFF, set()).add(n)
        en.setdefault(b.equs[n] & 0xFFFF, set()).add(n)
    rows = []
    order = sorted(a.routines, key=lambda r: (a.routines[r]["sec"],
                                              a.routines[r]["addr"]))
    order += sorted((r for r in b.routines if r not in a.routines),
                    key=lambda r: (b.routines[r]["sec"], b.routines[r]["addr"]))
    for name in order:
        ra, rb = a.routines.get(name), b.routines.get(name)
        row = dict(name=name, a=ra, b=rb, equs=set())
        if ra is None or rb is None:
            row["status"] = "new" if ra is None else "gone"
            rows.append(row)
            continue
        ca = [t for t, _ in ra["code"]]
        cb = [t for t, _ in rb["code"]]
        row["shift"] = rb["addr"] - ra["addr"]
        if len(ca) == len(cb) and all(
                line_eq(x, y, {}, {}, set()) for x, y in zip(ca, cb)):
            row["status"] = "same"
        elif len(ca) == len(cb) and all(
                line_eq(x, y, eo, en, row["equs"]) for x, y in zip(ca, cb)):
            row["status"] = "equ"
        else:
            row["equs"] = set()
            if len(ca) == len(cb) and all(
                    line_eq(x, y, eo, en, row["equs"], relaxed=True)
                    for x, y in zip(ca, cb)):
                row["status"] = "relaxed"
            else:
                row["equs"] = set()
                row["status"] = "changed"
        rows.append(row)
    return rows, moved


def render(code):
    out = []
    for (raw, relocs, canon, _d, _a), src in code:
        parts = []
        for x in raw:
            if isinstance(x, int):
                parts.append("%02X" % x)
            elif x.startswith("R"):
                c = sorted(relocs[int(x[1:])],
                           key=lambda t: (t[1] == "", t[2] != 0, t[2], t))[0]
                parts.append("<%s%s>" % (c[1] or c[0],
                                         "+%d" % c[2] if c[2] else ""))
            else:
                parts.append(x)
        out.append("%-36s ; %s" % (" ".join(parts), src[:60]))
    return out


# ---------------------------------------------------------------- building

def git(*args, **kw):
    return subprocess.run(["git", "-C", ROOT] + list(args),
                          capture_output=True, **kw)


def makefile_flags(mk_text, src):
    """The -I/-D a Makefile recipe assembles `src` with, and the variables
    it uses that this cannot expand. The recipe either names the source
    (`... -o $@ apps/video/video.asm`) or is a rule whose FIRST
    prerequisite it is and assembles `$<` (ETHER.DRV's); a rule for
    `$(BUILD)/<stem>.bin` wins over a variant arm of the same source."""
    text = mk_text.replace("\\\n", " ")
    stem = os.path.splitext(os.path.basename(src))[0]
    named = re.compile(r"(^|\s)" + re.escape(src) + r"(\s|$)")
    best, head, first = None, "", None
    for l in text.splitlines():
        if not l.startswith("\t"):
            if ":" in l and not re.match(r"^\s*[\w.$()-]+\s*[:+?]?=", l):
                head = l
                pre = l.split(":", 1)[1].split()
                first = pre[0] if pre else None
            continue
        if "$(NASM)" not in l:
            continue
        if not (named.search(l) or ("$<" in l and first == src)):
            continue
        if best is None or ("$(BUILD)/%s.bin" % stem) in head:
            best = l
    if best is None:
        return [], []
    toks = best.split()
    flags, unk, k = [], [], 0
    while k < len(toks):
        t = toks[k]
        if t in ("-I", "-D") and k + 1 < len(toks):
            flags += [t, toks[k + 1]]
            k += 2
            continue
        if t.startswith("-I") or t.startswith("-D"):
            flags.append(t)
        elif t.startswith("$(") and t not in ("$(NASM)", "$(STKLST)", "$@",
                                              "$<"):
            unk.append(t)
        k += 1
    return flags, unk


class Side(object):
    """Assemble one side (a commit, or the working tree when rev is None)."""

    def __init__(self, rev, src, args, scratch, tag):
        self.rev, self.src, self.args = rev, src, args
        self.dir = os.path.join(scratch, tag)
        os.makedirs(self.dir)
        self.cwd = ROOT if rev is None else os.path.join(self.dir, "tree")
        self.lst = os.path.join(self.dir, "out.lst")
        self.map = os.path.join(self.dir, "out.map")
        self.label = rev or "working tree"

    def flags(self):
        bdir = os.path.join(ROOT, self.args.build) if self.rev is None \
            else os.path.join(self.cwd, "build")
        if self.src == "kernel/kernel.asm":
            f = ["-I", "kernel/", "-I", "apps/"]
            f += ["-I", self.args.shadow + os.sep] if self.args.shadow else []
            f += ["-I", bdir + os.sep]
        else:
            if self.args.no_makefile:
                mk = ""
            elif self.rev is None:
                mk = open(os.path.join(ROOT, "Makefile")).read()
            else:
                r = git("show", "%s:Makefile" % self.rev, text=True)
                mk = r.stdout if r.returncode == 0 else ""
            f, unk = makefile_flags(mk, self.src)
            f = [x.replace("$(BUILD)", bdir) for x in f]
            if unk and not self.args.quiet:
                print("os88rdiff: %s: the Makefile's recipe also uses %s - "
                      "not expanded; pass -D for the arm you mean"
                      % (self.label, " ".join(sorted(set(unk)))),
                      file=sys.stderr)
            if not any(x == "-I" or x.startswith("-I") for x in f):
                f = ["-I", os.path.dirname(self.src) + "/", "-I", "apps/"]
        for i in self.args.I or []:
            f += ["-I", i]
        for d in self.args.D or []:
            f += ["-D" + d]
        return f

    def assemble(self):
        if self.rev is not None:
            os.makedirs(self.cwd, exist_ok=True)
            self._export_sources()
            self._link_build()
        fl = self.flags()
        self.flaglist = fl
        for _ in range(64):
            cmd = [NASM, "-f", "bin", "-w+error", "-Le", "-Lf"] + fl + [
                "-l", self.lst, "--before", "[map all %s]" % self.map,
                "-o", os.path.join(self.dir, "out.bin"), self.src]
            r = subprocess.run(cmd, cwd=self.cwd, capture_output=True,
                               text=True)
            if r.returncode == 0:
                return
            m = re.search(r"unable to open (?:include )?file [`'](.+?)'",
                          r.stderr)
            if self.rev is None or not m or not self._fetch(m.group(1), fl):
                raise Fail("%s: nasm failed:\n%s" % (self.label,
                                                     r.stderr[-3000:]))
        raise Fail("%s: gave up fetching includes" % self.label)

    def _export_sources(self):
        """Every .asm/.inc/.mac under the top directories the assembly can
        reach - a few MB at most, never the media beside a package. (nasm's
        -M cannot be asked: preprocess-only mode refuses this tree's
        `%if ($ - $$)` guards, and -MD records the main file alone.) An
        incbin'd asset is fetched on demand when nasm names it missing."""
        roots = {self.src.split("/")[0]}
        for f in self._iflags():
            if not os.path.isabs(f):
                roots.add(os.path.normpath(f).split(os.sep)[0])
        if self.src == "kernel/kernel.asm":
            roots |= {"kernel", "boot", "apps"}
        roots.discard("build")
        roots.discard("..")
        have = git("ls-tree", "-r", "--name-only", self.rev, "--",
                   *sorted(roots), text=True)
        if have.returncode:
            raise Fail("git ls-tree %s: %s" % (self.rev, have.stderr.strip()))
        self._export([n for n in have.stdout.splitlines()
                      if n.endswith((".asm", ".inc", ".mac"))])

    def _iflags(self):
        out = list(self.args.I or [])
        if self.src != "kernel/kernel.asm" and not self.args.no_makefile:
            r = git("show", "%s:Makefile" % self.rev, text=True)
            f, _ = makefile_flags(r.stdout if r.returncode == 0 else "",
                                  self.src)
            out += [f[k + 1] if f[k] == "-I" else f[k][2:]
                    for k in range(len(f)) if f[k] == "-I"
                    or (f[k].startswith("-I") and len(f[k]) > 2)]
        return [x for x in out if "$(" not in x]

    def _link_build(self):
        """build/ for a commit is a REAL directory of links to the current
        build's top-level files: generated includes resolve, and an
        `%include "../boot/x"` reached through it lands in the EXPORT -
        a plain `-I <checkout>/build/` would resolve it in the checkout and
        quietly assemble the working tree's file into the base."""
        src = os.path.join(ROOT, self.args.build)
        dst = os.path.join(self.cwd, "build")
        os.makedirs(dst, exist_ok=True)
        if os.path.isdir(src):
            for n in os.listdir(src):
                p = os.path.join(src, n)
                if os.path.isfile(p):
                    os.symlink(p, os.path.join(dst, n))

    def _export(self, paths):
        paths = sorted({p for p in paths if p})
        have = git("ls-tree", "-r", "--name-only", self.rev, "--",
                   *paths, text=True)
        if have.returncode:
            raise Fail("git ls-tree %s: %s" % (self.rev, have.stderr.strip()))
        names = have.stdout.split()
        if not names:
            return False
        a = subprocess.Popen(["git", "-C", ROOT, "archive", self.rev, "--"]
                             + names, stdout=subprocess.PIPE)
        t = subprocess.run(["tar", "-x", "-C", self.cwd], stdin=a.stdout)
        a.stdout.close()
        if a.wait() or t.returncode:
            raise Fail("git archive %s failed" % self.rev)
        return True

    def _fetch(self, name, fl):
        """A missing include: from the commit when it has it, else from the
        working tree (a generated file under a source directory)."""
        dirs = [os.path.dirname(self.src)] + [
            fl[k + 1] if fl[k] == "-I" else fl[k][2:]
            for k in range(len(fl)) if fl[k] == "-I" or
            (fl[k].startswith("-I") and len(fl[k]) > 2)]
        for d in dirs:
            if os.path.isabs(d):
                continue
            p = os.path.normpath(os.path.join(d, name))
            if os.path.exists(os.path.join(self.cwd, p)):
                continue
            if git("cat-file", "-e", "%s:%s" % (self.rev, p)).returncode == 0:
                return self._export([p])
            w = os.path.join(ROOT, p)
            if os.path.isfile(w):
                os.makedirs(os.path.dirname(os.path.join(self.cwd, p)),
                            exist_ok=True)
                shutil.copy(w, os.path.join(self.cwd, p))
                return True
        return False


def shadow_buildnum(scratch, bdir):
    """One build number on both sides (tools/os88sym.py's shadow, for the
    same reason: a commit is otherwise a three-byte difference)."""
    import os88sym
    bn = os88sym.image_build_num(os.path.join(bdir, "kernel.bin")) or 0
    d = os.path.join(scratch, "bn")
    os.makedirs(d)
    with open(os.path.join(d, "buildnum.inc"), "w") as f:
        f.write("%%define BUILD_NUM %d\n%%define BUILD_STR '%d'\n" % (bn, bn))
    return d


# ---------------------------------------------------------------- report

def main(argv=None):
    p = argparse.ArgumentParser(
        prog="os88rdiff",
        description="Per-routine diff of two builds of a package or the "
                    "kernel, with relocations resolved, branch relaxation "
                    "named, and the PARITY of every shift.")
    p.add_argument("src", nargs="?",
                   help="the package's .asm, or `kernel`")
    p.add_argument("base", nargs="?", default="HEAD",
                   help="a commit (default HEAD)")
    p.add_argument("new", nargs="?", default=".",
                   help="a commit, or `.` for the working tree (default)")
    p.add_argument("--lst", nargs=2, metavar=("BASE.lst", "NEW.lst"),
                   help="compare two listings you already have (each with "
                        "its .map beside it) instead of building")
    p.add_argument("-D", action="append", metavar="DEF",
                   help="a define for BOTH sides (a knob's arm)")
    p.add_argument("-I", action="append", metavar="DIR",
                   help="an extra include directory for both sides")
    p.add_argument("--no-makefile", action="store_true",
                   help="do not read the Makefile recipe's -I/-D")
    p.add_argument("--build", default=os.environ.get("OS88_BUILD") or "build",
                   help="where generated includes come from (default build)")
    p.add_argument("--hot", action="append", metavar="REGEX",
                   help="routines (full-match on the name) that must be "
                        "`same` and at an aligned shift; repeatable")
    p.add_argument("--hot-file", metavar="FILE",
                   help="one --hot regex per line, # comments")
    p.add_argument("--align", type=int, default=2,
                   help="a hot routine's shift must be a multiple of this "
                        "(default 2: parity; 1 turns the check off)")
    p.add_argument("--equ-ok", default=r".*_SEG", metavar="REGEX",
                   help="layout equs a hot routine may differ by and still "
                        "pass (default '.*_SEG' - a segment base is pure "
                        "relocation; a SIZE is not)")
    p.add_argument("--all", action="store_true",
                   help="list every routine, not only the interesting ones")
    p.add_argument("--quiet", action="store_true",
                   help="summary and the hot verdict only")
    p.add_argument("--show", metavar="REGEX",
                   help="print an instruction diff of the matching routines")
    p.add_argument("--keep", action="store_true",
                   help="keep the scratch trees under build/rdiff/")
    p.add_argument("--selfcheck", action="store_true",
                   help="assemble a synthetic package in eight arms and "
                        "check every verdict, each against a planted defect")
    args = p.parse_args(argv)
    if args.selfcheck:
        return selfcheck()

    hot = list(args.hot or [])
    if args.hot_file:
        for l in open(args.hot_file):
            l = l.split("#", 1)[0].strip()
            if l:
                hot.append(l)

    scratch = None
    try:
        if args.lst:
            ma = os.path.splitext(args.lst[0])[0] + ".map"
            mb = os.path.splitext(args.lst[1])[0] + ".map"
            a = Build(args.lst[0], ma, args.lst[0])
            b = Build(args.lst[1], mb, args.lst[1])
            title = "%s -> %s" % (args.lst[0], args.lst[1])
        else:
            if not args.src:
                p.error("name a source (or `kernel`), or pass --lst")
            src = args.src
            if src == "kernel":
                src = "kernel/kernel.asm"
            src = os.path.relpath(os.path.abspath(src), ROOT) \
                if os.path.isabs(src) or os.path.exists(src) else src
            if not os.path.exists(os.path.join(ROOT, src)):
                p.error("no source %s" % src)
            base = args.base
            new = None if args.new in (".", "WORK", "worktree") else args.new
            for r in [base] + ([new] if new else []):
                if git("rev-parse", "--verify", "-q",
                       r + "^{commit}").returncode:
                    raise Fail("%s is not a commit here (a shallow clone? "
                               "`git fetch --unshallow`)" % r)
            os.makedirs(os.path.join(ROOT, "build", "rdiff"), exist_ok=True)
            scratch = tempfile.mkdtemp(prefix="rd-",
                                       dir=os.path.join(ROOT, "build", "rdiff"))
            args.shadow = shadow_buildnum(
                scratch, os.path.join(ROOT, args.build)) \
                if src == "kernel/kernel.asm" else None
            sides = []
            for tag, rev in (("base", base), ("new", new)):
                s_ = Side(rev, src, args, scratch, tag)
                s_.assemble()
                sides.append(s_)
            a = Build(sides[0].lst, sides[0].map, sides[0].label)
            b = Build(sides[1].lst, sides[1].map, sides[1].label)
            title = "%s  %s -> %s   (%s)" % (
                src, a.label, b.label, " ".join(
                    os.path.relpath(f, ROOT) if os.path.isabs(f) else f
                    for f in sides[1].flaglist))
        for x in (a, b):
            if x.warn:
                print("os88rdiff: WARNING: " + x.warn, file=sys.stderr)
            bad = x.misattributed()
            if bad:
                print("os88rdiff: WARNING: %s: %d routine(s) whose listed "
                      "bytes do not add up to their size in the map (%s) - "
                      "their verdicts are not to be trusted"
                      % (x.label, len(bad), ", ".join(sorted(bad)[:6])),
                      file=sys.stderr)
        return report(a, b, title, args, hot)
    except Fail as e:
        print("os88rdiff: %s" % e, file=sys.stderr)
        return 2
    finally:
        if scratch and not args.keep:
            shutil.rmtree(scratch, ignore_errors=True)
        elif scratch:
            print("os88rdiff: kept %s" % os.path.relpath(scratch, ROOT),
                  file=sys.stderr)


def report(a, b, title, args, hot):
    rows, moved = compare(a, b)
    out = print
    out("os88rdiff: " + title)
    for sec in sorted(set(a.sections) | set(b.sections)):
        da, db = a.sections.get(sec), b.sections.get(sec)
        la = da["length"] if da else 0
        lb = db["length"] if db else 0
        if la != lb or args.all:
            out("  %-8s %6d -> %6d  (%+d)%s" % (sec, la, lb, lb - la,
                                               "  nobits" if (da or db)
                                               ["nobits"] else ""))
    cnt = {}
    for r in rows:
        cnt[r["status"]] = cnt.get(r["status"], 0) + 1
    odd = [r for r in rows if r["status"] in ("same", "equ")
           and r["shift"] % 2]
    out("  routines: " + ", ".join("%d %s" % (cnt[k], k) for k in
                                   ("same", "equ", "relaxed", "changed",
                                    "new", "gone") if cnt.get(k))
        + " - %d identical ones at an ODD shift" % len(odd))
    if moved and not args.quiet:
        out("  layout equs that moved: " + ", ".join(sorted(moved)[:12])
            + (" ..." if len(moved) > 12 else ""))

    if not args.quiet:
        # where the layout moved: runs of one shift, in base order
        out("\n  shift runs (base order; a run boundary is where bytes "
            "were added or removed):")
        for sec in sorted({r["a"]["sec"] for r in rows if r["a"]}):
            seq = sorted((r for r in rows if r["a"] and r["b"]
                          and r["a"]["sec"] == sec),
                         key=lambda r: r["a"]["addr"])
            runs = []
            for r in seq:
                if runs and runs[-1][2] == r["shift"]:
                    runs[-1][1] = r
                    runs[-1][3] += 1
                else:
                    runs.append([r, r, r["shift"], 1])
            if len(runs) == 1 and runs[0][2] == 0 and not args.all:
                continue
            for f, l, s, n in runs:
                out("    %-6s %s %+6d  %s .. %s  (%d)" % (
                    sec, "ODD" if s % 2 else "   ", s, f["name"], l["name"],
                    n))
        out("")
        out("  %-8s %-28s %6s %6s %6s  %s" % ("status", "routine", "base",
                                              "new", "shift", "size"))
        for r in rows:
            interesting = r["status"] != "same" or r["shift"] % 2
            if not (interesting or args.all):
                continue
            ra, rb = r["a"], r["b"]
            sh = r.get("shift")
            out("  %-8s %-28s %6s %6s %6s  %s%s" % (
                r["status"], r["name"],
                "%04X" % ra["addr"] if ra else "-",
                "%04X" % rb["addr"] if rb else "-",
                ("%+d%s" % (sh, " odd" if sh % 2 else "")) if sh is not None
                else "",
                "%d -> %d" % (ra["size"], rb["size"]) if ra and rb and
                ra["size"] != rb["size"] else "%d" % (ra or rb)["size"],
                ("  [%s]" % ", ".join(sorted(r["equs"]))) if r["equs"] else ""))

    if args.show:
        import difflib
        for r in rows:
            if not re.fullmatch(args.show, r["name"]) or not r["a"] \
                    or not r["b"]:
                continue
            out("\n  --- %s (%s)" % (r["name"], r["status"]))
            for d in difflib.unified_diff(render(r["a"]["code"]),
                                          render(r["b"]["code"]),
                                          lineterm="", n=1):
                out("  " + d)

    if not hot:
        return 0
    bad, seen, mis = [], {}, {}
    eqok = re.compile(args.equ_ok)
    for pat in hot:
        rx = re.compile(pat)
        hits = [r for r in rows if rx.fullmatch(r["name"])]
        if not hits:
            bad.append("%s: matches NO routine (a typo is a green that "
                       "tests nothing)" % pat)
        for r in hits:
            seen[r["name"]] = r
    for r in rows:                      # base order, each routine once
        if r["name"] not in seen:
            continue
        if r["status"] in ("new", "gone"):
            bad.append("%s: %s" % (r["name"], r["status"]))
            continue
        st = r["status"]
        if st == "equ" and all(eqok.fullmatch(n) for n in r["equs"]):
            st = "same"
        if st != "same":
            bad.append("%s: %s%s" % (r["name"], r["status"],
                                     " [%s]" % ", ".join(sorted(r["equs"]))
                                     if r["equs"] else ""))
        elif args.align > 1 and r["shift"] % args.align:
            mis.setdefault((r["a"]["sec"], r["shift"]), []).append(r["name"])
    out("")
    if not bad and not mis:
        out("  HOT: all %d hot routine(s) identical, at a shift that is a "
            "multiple of %d" % (len(seen), args.align))
        return 0
    nmis = sum(len(v) for v in mis.values())
    out("  HOT: FAILED - %d of %d hot routine(s):" % (len(bad) + nmis,
                                                      len(seen)))
    for x in bad:
        out("    " + x)
    for (sec, sh), names in sorted(mis.items(), key=lambda kv: -len(kv[1])):
        out("    %d identical but moved %+d, not a multiple of %d (%s): %s"
            % (len(names), sh, args.align, sec,
               ", ".join(names[:4]) + (" ..." if len(names) > 4 else "")))
    if mis:
        # WHERE the alignment was lost: walking the base in address order,
        # each place the shift goes from aligned to not is a routine (or a
        # new/gone one beside it) whose size moved by an unaligned amount -
        # usually one relaxed branch, and the thing to write back
        turns = []
        for sec in sorted({k[0] for k in mis}):
            seq = sorted((r for r in rows if r["a"] and r["b"]
                          and r["a"]["sec"] == sec),
                         key=lambda r: r["a"]["addr"])
            turn = None                 # the turn into the current run
            for p_, n_ in zip([None] + seq, seq):
                if n_["shift"] % args.align == 0:
                    turn = None
                    continue
                if p_ is not None and p_["shift"] % args.align == 0:
                    between = [r for r in rows if r["b"] and not r["a"]
                               and r["b"]["sec"] == sec and p_["b"]["addr"]
                               < r["b"]["addr"] < n_["b"]["addr"]]
                    turn = ["%s (%s, %+d)%s, before %s" % (
                        p_["name"], p_["status"],
                        p_["b"]["size"] - p_["a"]["size"],
                        "".join(" + new %s (%d)" % (r["name"], r["b"]["size"])
                                for r in between),
                        n_["name"]), False]
                elif p_ is None:
                    turn = ["the start of %s, before %s" % (sec, n_["name"]),
                            False]
                if turn and not turn[1] and n_["name"] in seen:
                    turns.append(turn[0])  # this run holds hot code
                    turn[1] = True
        out("    ...alignment lost at: " + "; ".join(turns[:8])
            + (" ..." if len(turns) > 8 else ""))
    return 1


# ---------------------------------------------------------------- selfcheck

SELF_SRC = r"""
cpu 8086
org 0
%ifdef NOPE
section .cold                   ; NOT taken: section tracking must ignore it
%endif
start:
    jmp hot
cold:                           ; the routine a size pass edits
    cmp ax, 1
%ifdef PIN
    jnz .x                      ; nasm's synthesised near jz, written by hand
    jmp near .far
.x:
%else
    jz .far                     ; near or short: nasm decides by distance
%endif
    times PAD nop
.far:
%ifdef NEARJMP
    jmp near .end               ; a pinned near jmp...
%else
    jmp .end                    ; ...or nasm's short one: RELAXED, -1 byte
%endif
.end:
    ret
hot:                            ; the routine it must not move
    mov si, table
    mov al, [var]
    mov bx, [bssvar]
    mov cx, 3
.lp:
    lodsb
%ifdef HOTEDIT
    dec al
%else
    inc al
%endif
    loop .lp
    call cold
    call far_cold
    ret
table: db 1, 2, 3
var:   db 0
msg:   db 'Spc=drive  Enter=yes', 0
alias  equ msg + 11             ; an alias INTO a string: not a routine
%macro ZB 1
%1 equ image_end + 8            ; a macro-made equ: only -Le shows its name
%endmacro
ZB zvar
rows:  times 3 db 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12    ; two chunks, x3
pad:   times 4 db 0
union:
absolute union                  ; a union laid over `pad`: not routines
u_a:   resb 1
u_b    resw 1
section .text
tail:  ret
section .cold vstart=0 follows=.text
far_cold:                       ; another section at offset 0, kernel-shaped
    mov al, [var]
    mov [bvar], al
    ret
section .bss nobits vfollows=.cold
bvar: resb 2
section .text
image_end:
bssvar equ image_end + 4        ; a package's bss: equs above the image
"""


def selfcheck():
    """Every verdict against a planted case - WRITING-TESTS.md 1's rule
    applied to a tool: a check is only worth having if it goes red."""
    import contextlib
    import io
    os.makedirs(os.path.join(ROOT, "build", "rdiff"), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="rdself-",
                           dir=os.path.join(ROOT, "build", "rdiff"))
    src = os.path.join(tmp, "self.asm")
    with open(src, "w") as f:
        f.write(SELF_SRC)
    built = {}

    def asm(defs, lflags=("-Le", "-Lf")):
        key = tuple(sorted(defs)) + tuple(lflags)
        if key not in built:
            n = os.path.join(tmp, "v%d" % len(built))
            r = subprocess.run([NASM, "-f", "bin", "-w+error"] + list(lflags)
                               + ["-D" + d for d in defs]
                               + ["-l", n + ".lst", "--before",
                                  "[map all %s.map]" % n, "-o", n + ".bin",
                                  src], capture_output=True, text=True)
            if r.returncode:
                raise Fail("selfcheck source failed: " + r.stderr)
            built[key] = Build(n + ".lst", n + ".map", n)
        return built[key]

    class A(object):
        align, equ_ok, all, quiet, show = 2, r".*_SEG", False, True, None

    base = ["PAD=140"]          # jz .far is out of short range: 5 bytes
    cases = [
        # (what, base defs, new defs, {routine: status}, hot, exit, shift)
        ("an identical rebuild", base, base,
         {"cold": "same", "hot": "same", "far_cold": "same"}, ["hot"], 0, 0),
        ("an EVEN cold shrink (2 nops)", base, ["PAD=138"],
         {"cold": "changed", "hot": "same", "far_cold": "same"},
         ["hot"], 0, -2),
        ("a cold shrink that RE-RELAXES jz (-20 nops, -3 jz): an ODD shift",
         base, ["PAD=120"], {"cold": "changed", "hot": "same"},
         ["hot"], 1, -23),
        ("the relaxed jz PINNED back by hand (jnz/jmp near): identical",
         base, ["PAD=140", "PIN"], {"cold": "same", "hot": "same"},
         ["hot"], 0, 0),
        ("a jmp near -> short and nothing else: RELAXED, hot moves -1",
         ["PAD=140", "NEARJMP"], base, {"cold": "relaxed", "hot": "same"},
         ["hot"], 1, -1),
        ("an edited HOT instruction", base, ["PAD=140", "HOTEDIT"],
         {"cold": "same", "hot": "changed"}, ["hot"], 1, 0),
        ("a hot regex that matches nothing is a FAILURE, not a pass",
         base, base, {"hot": "same"}, ["hott"], 1, 0),
    ]
    bad = 0
    try:
        for what, da, db, want, hot, code, shift in cases:
            a, b = asm(da), asm(db)
            rows, _ = compare(a, b)
            got = {r["name"]: r["status"] for r in rows}
            sh = {r["name"]: r.get("shift") for r in rows}
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = report(a, b, what, A, hot)
            errs = ["%s is %s, wanted %s" % (k, got.get(k), v)
                    for k, v in want.items() if got.get(k) != v]
            if rc != code:
                errs.append("exit %d, wanted %d" % (rc, code))
            if sh.get("hot") != shift:
                errs.append("hot shifted %s, wanted %+d" % (sh.get("hot"),
                                                            shift))
            print("  %s  %s" % ("ok  " if not errs else "FAIL", what))
            for e in errs:
                print("        " + e)
            bad += bool(errs)
        # ATTRIBUTION: every routine's listed bytes add up to its size in the
        # map - through a FALSE `%ifdef`'d section line, a two-chunk
        # `times`, an equ alias into a string, a macro-made equ and an
        # `absolute` union - and the check goes red on a listing made
        # without -Le, where that false section line is taken as a switch
        for what, lf, want_bad in (
                ("every byte attributed, with -Le", ("-Le", "-Lf"), False),
                ("...and a plain listing is CAUGHT misattributing", (),
                 True)):
            x = asm(base, lf)
            mis = x.misattributed()
            ok = bool(mis) == want_bad and (want_bad or (
                "alias" not in x.routines and "zvar" not in x.routines
                and "u_a" not in x.routines and "rows" in x.routines))
            print("  %s  %s%s" % ("ok  " if ok else "FAIL", what,
                                  "" if ok else "  (misattributed: %s)"
                                  % ", ".join(sorted(mis)) or "none"))
            bad += not ok
            cases.append(what)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("os88rdiff selfcheck: %d of %d cases %s" % (
        len(cases) - bad, len(cases), "ok" if not bad else "FAILED"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
