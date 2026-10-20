#!/usr/bin/env python3
"""Tokenise an Applesoft listing into the bytes an Apple II Plus holds.

    python3 tools/a2bas.py apps/apple2/welcome.a2b -o build/WELCOME.BAS
    python3 tools/a2bas.py apps/apple2/welcome.a2b --selfcheck
    python3 tools/a2bas.py --list build/WELCOME.BAS

WHY THIS EXISTS.  `APPLE2` (docs/APPLE2-SPEC.md section 12) loads a program by
WALKING it - a chain of `next-line pointer (2), line number (2), tokens, $00`
ending in a next of `$0000` - so a plain ASCII listing on the disk is refused
by name, correctly, and the welcome program has to ship in the machine's own
form.  `apps/apple2/welcome.a2b` is the SOURCE and `build/WELCOME.BAS` is the
artefact, on `tools/os88rtf.py`'s shape one package along.

**THE TOKEN TABLE IS READ OUT OF THE PINNED ROM AND IS NOT TYPED HERE.**
Applesoft's own TOKEN.NAME.TABLE lives at `$D0D0`
(`AppleWin/bin/A2_BASIC.SYM:805`), which is offset `0xD0` of
`build/apple2-rom/APPLE2.ROM` (APPLE2-SPEC section 1.4's layout: `$D000` is
offset 0).  Each name is stored with bit 7 set on its LAST character, and the
107 of them run from `$80` (`END`) to `$EA` (`MID$`).  A hand-typed copy of
that table would be a second transcription of something the build already has
in front of it, and the one thing this project does not do is type a table
from memory (`.claude/skills/port-to-os8088/LESSONS.md` 1).  It is also what
makes the output DETERMINISTIC in the sense the repo means: the ROM is pinned
by SHA-256, so the shipped `WELCOME.BAS` rebuilds byte for byte.

WHAT IT IMPLEMENTS, and it is Applesoft's PARSE (`$D559`) rather than a
plausible tokeniser:

  - **a line is `<number> <text>`**, and a source line that does not start
    with a digit is a comment in the SOURCE file and is not emitted at all -
    which is what lets `welcome.a2b` carry a header;
  - the text is **upper-cased**, because a II Plus keyboard has no lower case
    and its character generator no lower-case glyphs;
  - a **quoted string is copied verbatim**, quotes included;
  - after `DATA` the rest of the STATEMENT is copied verbatim and after `REM`
    the rest of the LINE is, `:` included, which is the difference the ROM
    makes - so a keyword in a comment stays text.  All three token bytes
    (`REM`, `DATA`, `PRINT` for `?`) are DERIVED from the table read above,
    never typed;
  - `?` is `PRINT`, as it is on the machine;
  - otherwise, at each position, the token names are tried **in table order**
    and the first that matches wins, **skipping spaces in the input** while
    comparing - which is why `PR INT` is `PRINT` on a real Apple and is why
    the operators (`+` `-` `*` `/` `^` `>` `=` `<`) are tokens here and not
    characters: they are names in that same table.

THE SELF-CHECK IS THE GATE.  `--selfcheck` tokenises the listing, LISTS the
result back with the same table, and requires the two to agree once spaces
outside strings are normalised away.  A tokeniser is exactly the kind of tool
whose output looks fine and runs wrong - one byte off and the machine shows a
`]` prompt whose `LIST` is empty or full of `SYNTAX ERROR`, which no
screendump of a booted machine reveals (LESSONS.md 9).  `apps/apple2/build.sh`
does not run it; the Makefile rule that writes `build/WELCOME.BAS` does, so
the artefact cannot be written without it passing.

**AND HERE IS WHAT IT CANNOT SEE**, because a gate whose reach is not stated
gets read as proof of more than it checks: the LIST half walks the SAME table
this half tokenised with, so any mis-tokenisation that is length-preserving
round-trips to an identical squashed string.  It catches a byte LOST, a byte
ADDED and a name matched at the wrong place; it does NOT catch a token whose
VALUE is wrong, nor literal mode failing to engage - both of which look
correct on the way back out and wrong only on the machine.  That is the whole
reason the three special-cased token bytes below are derived from the ROM's
table rather than typed: the derivation, not the self-check, is what makes
them right.

WHAT IT DOES NOT DO: it does not evaluate, renumber, or check that a line
number ascends - the PACKAGE's own walk does that (`ovl_a2_walk`,
`apps/apple2/a2prog.c`) and a second copy of a rule is a second dialect.  It
does refuse a line number outside 0..63999, because that one is Applesoft's
and the walk would refuse the file with nothing saying why.
"""
import argparse
import os
import sys

ROM_DEFAULT = os.path.join("build", "apple2-rom", "APPLE2.ROM")

TOK_BASE = 0x80                     # END
TOK_LAST = 0xEA                     # MID$
TOK_TABLE = 0x00D0                  # $D0D0 - $D000, the ROM's own offset
TOK_COUNT = TOK_LAST - TOK_BASE + 1

PROG = 0x0801                       # where program text begins on a II+
LINE_MAX = 63999                    # Applesoft's own line-number cap
MEMSIZ = 0xC000                     # 48K, no Language Card


def read_tokens(rompath):
    """The token NAME table, out of the pinned ROM itself."""
    with open(rompath, "rb") as f:
        rom = f.read()
    if len(rom) < TOK_TABLE + 1024:
        sys.exit("a2bas: %s is %d bytes and is not the APPLE2.ROM this tool "
                 "reads (APPLE2-SPEC section 1.4)" % (rompath, len(rom)))
    names = []
    p = TOK_TABLE
    cur = ""
    while len(names) < TOK_COUNT and p < TOK_TABLE + 1024:
        c = rom[p]
        p += 1
        cur += chr(c & 0x7F)
        if c & 0x80:
            names.append(cur)
            cur = ""
    # THE SHAPE IS ASSERTED, not assumed: a ROM that is not this one gives a
    # plausible-looking table of the wrong thing, and every byte downstream
    # would then be wrong in a way only the machine could show.
    if len(names) != TOK_COUNT or names[0] != "END" or names[-1] != "MID$":
        sys.exit("a2bas: the token table at $D0D0 of %s reads %d name(s) "
                 "%r..%r and Applesoft's is %d from 'END' to 'MID$'"
                 % (rompath, len(names), names[:1], names[-1:], TOK_COUNT))
    return names


def _tok(names, name):
    """The token byte for a keyword, DERIVED from the table read out of the
    ROM.  Three of them are special-cased in the parse below - REM and DATA
    open literal mode, `?` is PRINT - and typing 0xB2/0x83/0xBA here would be
    exactly the hand-copied table this file's header refuses.  A wrong value
    would be SILENT: literal mode would never engage and a keyword inside a
    REM would quietly become a token, which --selfcheck cannot see (below)."""
    if name not in names:
        sys.exit("a2bas: the ROM's token table has no %r in it" % name)
    return TOK_BASE + names.index(name)


def tokenise_line(text, names):
    """One line's statement text -> its bytes.  Applesoft PARSE, $D559."""
    t_rem = _tok(names, "REM")
    t_data = _tok(names, "DATA")
    t_print = _tok(names, "PRINT")
    out = bytearray()
    src = text.upper()
    i = 0
    n = len(src)
    literal = None                  # None, 'stmt' (DATA: to the next `:`) or
                                    # 'line' (REM: to the end of the line)
    while i < n:
        c = src[i]
        if c == '"':                # a string is copied whole, quotes and all
            out.append(ord('"'))
            i += 1
            while i < n and src[i] != '"':
                out.append(ord(src[i]))
                i += 1
            if i < n:
                out.append(ord('"'))
                i += 1
            continue
        if literal:
            if c == ':' and literal == 'stmt':
                literal = None      # DATA ends at the statement separator...
                continue            # ...and REM does NOT: it runs to the end
                                    # of the line, which is ENFORCED here
                                    # rather than asserted in a comment
            out.append(ord(c))
            i += 1
            continue
        if c == '?':                # the machine's own shorthand
            out.append(t_print)
            i += 1
            continue
        hit = None
        for k, name in enumerate(names):
            j = i
            ok = True
            for ch in name:
                while j < n and src[j] == ' ':
                    j += 1          # spaces INSIDE a keyword, as the ROM does
                if j >= n or src[j] != ch:
                    ok = False
                    break
                j += 1
            if ok:
                hit = (TOK_BASE + k, j)
                break
        if hit is not None:
            out.append(hit[0])
            i = hit[1]
            if hit[0] == t_rem:
                literal = 'line'
            elif hit[0] == t_data:
                literal = 'stmt'
            continue
        out.append(ord(c))
        i += 1
    return bytes(out)


def tokenise(srclines, names):
    """The whole listing -> (image bytes at $0801, [(lineno, bytes)])."""
    lines = []
    for raw in srclines:
        s = raw.rstrip("\n").rstrip("\r")
        t = s.strip()
        if not t or not t[0].isdigit():
            continue                # a comment in the SOURCE, not a program
                                    # line (this file's header)
        k = 0
        while k < len(t) and t[k].isdigit():
            k += 1
        num = int(t[:k])
        if num > LINE_MAX:
            sys.exit("a2bas: line number %d is above Applesoft's own %d"
                     % (num, LINE_MAX))
        lines.append((num, tokenise_line(t[k:].lstrip(), names)))

    if not lines:
        sys.exit("a2bas: no numbered lines - a listing of nothing is not a "
                 "program")

    # THE CHAIN, WITH THE LINKS COMPUTED FROM WHERE EACH LINE LANDS. The
    # package repairs them anyway (Applesoft's own FIX.LINKS, a2prog.c), so
    # this is what a real machine holds rather than what the loader needs.
    img = bytearray()
    addr = PROG
    for num, body in lines:
        nxt = addr + 4 + len(body) + 1
        if nxt >= MEMSIZ:
            sys.exit("a2bas: the program passes $%04X, which is not RAM on a "
                     "48K II Plus" % MEMSIZ)
        img += bytes((nxt & 0xFF, nxt >> 8, num & 0xFF, num >> 8))
        img += body
        img.append(0)
        addr = nxt
    img += b"\x00\x00"              # the terminating link - and VARTAB points
    return bytes(img), lines        # PAST it (a2prog.c)


def list_image(img, names):
    """LIST, the way the machine does it - the self-check's other half."""
    out = []
    p = 0
    addr = PROG
    while p + 2 <= len(img):
        nxt = img[p] | (img[p + 1] << 8)
        if nxt == 0:
            break
        num = img[p + 2] | (img[p + 3] << 8)
        p += 4
        text = ""
        while p < len(img) and img[p] != 0:
            b = img[p]
            if b >= TOK_BASE and b <= TOK_LAST:
                text += " " + names[b - TOK_BASE] + " "
            else:
                text += chr(b)
            p += 1
        p += 1
        addr = nxt
        out.append("%d %s" % (num, text))
    return out


def squash(s):
    """A line with every space outside a string taken out, upper-cased."""
    r = ""
    q = False
    for ch in s.upper():
        if ch == '"':
            q = not q
        if ch == ' ' and not q:
            continue
        r += ch
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("source", nargs="?", help="the .a2b listing")
    ap.add_argument("-o", "--output", metavar="OUT.BAS",
                    help="the tokenised program to write")
    ap.add_argument("--rom", default=ROM_DEFAULT,
                    help="the pinned APPLE2.ROM the token table is read from")
    ap.add_argument("--selfcheck", action="store_true",
                    help="tokenise, LIST it back, and require the two to "
                         "agree (the gate; the Makefile rule runs it)")
    ap.add_argument("--list", metavar="IN.BAS",
                    help="LIST a tokenised program and exit")
    args = ap.parse_args()

    names = read_tokens(args.rom)

    if args.list:
        with open(args.list, "rb") as f:
            for line in list_image(f.read(), names):
                print(line)
        return 0

    if not args.source:
        ap.error("a listing is required (or --list)")
    with open(args.source, "r") as f:
        src = f.readlines()
    img, lines = tokenise(src, names)

    if args.selfcheck:
        want = ["%d %s" % (num, "") for num, _ in lines]
        got = list_image(img, names)
        if len(got) != len(lines):
            sys.exit("a2bas: --selfcheck LISTed %d line(s) out of %d"
                     % (len(got), len(lines)))
        bad = 0
        for (num, _), back, raw in zip(lines, got,
                                       [l for l in src
                                        if l.strip() and l.strip()[0].isdigit()]):
            if squash(back) != squash(raw.strip()):
                print("a2bas: --selfcheck line %d\n  source %s\n  LIST   %s"
                      % (num, squash(raw.strip()), squash(back)))
                bad += 1
        if bad:
            sys.exit("a2bas: --selfcheck FAILED on %d line(s) - the "
                     "tokenised bytes are not this listing" % bad)
        print("a2bas: --selfcheck OK - %d line(s), %d byte(s), every one "
              "LISTs back to its source" % (len(lines), len(img)))
        _ = want

    if args.output:
        with open(args.output, "wb") as f:
            f.write(img)
        print("a2bas: %s -> %s (%d line(s), %d bytes at $%04X, %d token "
              "name(s) from %s)"
              % (args.source, args.output, len(lines), len(img), PROG,
                 len(names), args.rom))
    return 0


if __name__ == "__main__":
    sys.exit(main())
