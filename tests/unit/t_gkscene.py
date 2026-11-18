#!/usr/bin/env python3
"""The game kit's scene compiler writes the record SPEC.md 133.6 pins.

    python3 tests/unit/t_gkscene.py

tools/os88gk.py is the only writer of a scene record and apps/cc/gk.inc's
gk_scene the only reader on the guest; a byte that drifts between them draws
the wrong widget, or reads an argument as an op, with nothing to say so. So
this row is a SECOND READER, written from the SPEC table rather than from the
tool's own decode(), and it holds:

  * a GOLDEN record: a three-op scene compiled to exact bytes - magic, size,
    the tag table, each op's length, number, row range, condition and every
    argument encoding (small, negative, word, both slots, a literal with an
    inserted slot, a role);
  * a ROUND TRIP of a larger scene through this reader: every op's routine
    in the order the source names them, its row range covering what it can
    draw, the corners of fill/dither/dots, the conditions;
  * the THREE TABLES that must agree: gk.inc's gk_ops (op number -> routine)
    against the compiler's op numbers, and gk.h's GK_* constants against the
    compiler's names for them;
  * the REFUSALS of SPEC.md 133.7, each by its message.

VERIFIED TO FAIL: swapping two rows of gk.inc's gk_ops, changing GK_SHADOW
in the compiler, or encoding -1 as a word each turn this row red.
"""
import os
import re
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88gk  # noqa: E402

FAILS = []


def check(ok, what):
    if not ok:
        FAILS.append(what)


def compile_text(text, sym):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(text)
    try:
        return os88gk.compile_scenes([f.name], sym)
    finally:
        os.unlink(f.name)


def read(rec):
    """SPEC.md 133.6, independently: -> (w, h, tags, ops)."""
    assert rec[0:1] == b"S", "magic"
    w, h = struct.unpack_from("<HH", rec, 1)
    n = rec[5]
    tags = {rec[6 + 9 * i]: struct.unpack_from("<4h", rec, 7 + 9 * i) for i in range(n)}
    p, ops = 6 + 9 * n, []
    while rec[p] != 0:
        ln, op = rec[p], rec[p + 1]
        top, bot = struct.unpack_from("<2h", rec, p + 2)
        q = p + 6
        cond = None
        if op & 0x80:
            c, k = rec[q], struct.unpack_from("b", rec, q + 1)[0]
            cond = (c & 31, bool(c & 0x20), k)
            q += 2
        args = []
        while q < p + ln:
            b = rec[q]
            q += 1
            if b <= 0x7F:
                args.append(b)
            elif b <= 0x9F:
                args.append(("iv", b - 0x80))
            elif b <= 0xBF:
                args.append(("sv", b - 0xA0))
            elif b == 0xC0:
                args.append(struct.unpack_from("<h", rec, q)[0])
                q += 2
            elif b == 0xC1:
                z = rec.index(0, q)
                args.append(("lit", bytes(rec[q:z])))
                q = z + 1
            elif b == 0xC2:
                args.append(("role", rec[q]))
                q += 1
            elif b >= 0xE0:
                args.append(b - 256)
            else:
                raise AssertionError("code %02Xh" % b)
        assert q == p + ln, "op overruns its length"
        ops.append((op & 0x3F, top, bot, cond, args))
        p += ln
    assert p == len(rec) - 1, "bytes after the end op"
    return w, h, tags, ops


def golden():
    sym = {"SC": 7, "F": 2}
    rec = compile_text(
        "fonth F 10\n"
        "scene SC 100 50\n"
        "  box 0 0 100 50 raised|round 15 #9\n"
        "  if %3=2 text 4 -2 300 \"a$1b\" @TEXT F fit\n"
        "  fill 1 2 3 4 $5\n"
        "end\n".replace("$5", "-1"), sym)[7]
    want = bytearray(b"S" + struct.pack("<HH", 100, 50) + b"\x01")
    want += b"\x09" + struct.pack("<4h", 0, 0, 100, 50)
    # box: x y w h style fill; rows 0..49 (no shadow)
    op = bytes([0]) + struct.pack("<2h", 0, 49) + bytes([0, 0, 100, 50, 0x11, 15])
    want += bytes([len(op) + 1]) + op
    # text, conditional %3 == 2: c = 3 | 20h, k = 2; y -2 -> FEh; w 300 -> C0 word
    op = (bytes([4 | 0x80]) + struct.pack("<2h", -2, 8) + bytes([0x23, 2]) +
          bytes([4, 0xFE, 0xC0]) + struct.pack("<H", 300) +
          b"\xC1a\x02b\x00" + bytes([0xC2, 5, 2, 0x20]))
    want += bytes([len(op) + 1]) + op
    # fill: corners (1, 2, 3, 5), colour -1
    op = bytes([18]) + struct.pack("<2h", 2, 5) + bytes([1, 2, 3, 5, 0xFF])
    want += bytes([len(op) + 1]) + op + b"\x00"
    check(rec == bytes(want), "golden record:\n  got  %s\n  want %s" % (rec.hex(), bytes(want).hex()))


def orins():
    """GK_PLEFT and GK_B_FLAT (DECISIONS D7) reach the record as OR-ins: a
    flat button's constant state carries 10h and its rows lose the shadow's
    three; a GK_PLEFT panel's style carries 40h."""
    rec = compile_text(
        "fonth F 10\n"
        "scene SC 200 100\n"
        "  panel 0 0 200 100 12 \"T\" F raised2|pleft\n"
        "  button 4 70 60 20 \"B\" F pressed|bflat\n"
        "  button 70 70 60 20 \"C\" F GK_B_FOCUS|GK_B_FLAT\n"
        "  button 140 70 50 20 \"D\" F normal\n"
        "end\n", {"SC": 1, "F": 2})[1]
    ops = read(rec)[3]
    check([o[0] for o in ops] == [6, 8, 8, 8], "orins: ops %r" % ([o[0] for o in ops],))
    check(ops[0][-1][7] == 0x42, "orins: panel style %r" % (ops[0][-1][7],))
    check(ops[1][-1][6] == 0x14 and ops[2][-1][6] == 0x11, "orins: button states")
    check(ops[1][1:3] == (70, 89), "orins: flat button rows %r" % (ops[1][1:3],))
    check(ops[3][1:3] == (70, 92), "orins: shadowed button rows %r" % (ops[3][1:3],))


SCENE = r'''
fonth BIG 16
scene 300 400 300
  panel 0 0 396 296 20 "TITLE $0" BIG raised|shadow
  button 10 260 90 24 "OK" BIG %0 #1
  if !%1 {
    radio 120 260 14 1
    check 140 260 14 1 0
  }
  tab 10 30 80 20 "One" BIG %2
  keycap 200 30 30 30 "Q" -1
  dither 0 100 396 8 cyan black 2
  dots 0 120 396 1 4 darkgrey
  spr 300 10 SPRITE
  sprshadow %3 %4 SPRITE 3
  arrow 250 30 16 16 2 red
  drop 10 70 140 22 $1 BIG #2
  banner 10 100 200 18 "50$$ off" BIG -1
  titlebar 3 3 390 18 "X" BIG left
  textbox 10 140 200 30 $2 white BIG centre|emboss
  hline 10 180 200 0
  vline 220 140 40 1
  shadow 300 140 40 40 3
  text 10 200 100 "t" black -1 0
  check 10 220 14 1 SPRITE
end
'''


def roundtrip():
    sym = {"BIG": 4, "SPRITE": 9}
    _, h, tags, ops = read(compile_text(SCENE, sym)[300])
    names = [os88gk.OPNAMES[o[0]] for o in ops]
    check(names == ["panel", "button", "radio", "check", "tab", "keycap",
                    "dither", "dots", "spr", "sprshadow", "arrow", "drop",
                    "banner", "titlebar", "textbox", "hline", "vline", "shadow",
                    "text", "check"], "op order %s" % names)
    check(h == 300 and tags == {1: (10, 260, 90, 24), 2: (10, 70, 140, 22)}, "tags %s" % tags)
    byname = {}
    for o in ops:
        byname.setdefault(os88gk.OPNAMES[o[0]], o)
    panel = byname["panel"]
    check(panel[1] == 0 and panel[2] >= 295 + 3, "panel rows %s" % (panel[1:3],))
    check(panel[4][5] == ("lit", b"TITLE \x01"), "literal slot insertion %r" % (panel[4][5],))
    check(byname["button"][2] >= 260 + 23 + 3, "button rows include the shadow")
    check(byname["tab"][1] <= 28, "tab rows include the 2 px rise")
    check(byname["radio"][3] == (1, True, 0) and byname["check"][3] == (1, True, 0),
          "if !%1 block -> (1, equal, 0)")
    check(byname["button"][4][6] == ("iv", 0), "int slot")
    check(byname["drop"][4][4] == ("sv", 1), "string slot")
    check(byname["keycap"][4][5] == -1 and byname["keycap"][2] >= 30 + 29,
          "kernel font -1 and its rows")
    check(byname["dither"][4][:7] == [0, 100, 395, 107, 3, 0, 2], "dither corners %s" % byname["dither"][4])
    check(byname["dots"][4][:6] == [0, 120, 395, 120, 4, 8], "dots corners %s" % byname["dots"][4])
    check(byname["sprshadow"][1:3] == os88gk.ALLROWS, "a slot y gets every row")
    check(byname["spr"][2] >= 10 + 254, "a sprite's rows are not known: 255")
    check(byname["banner"][4][4] == ("lit", b"50$ off"), "$$ is a dollar")
    check(byname["textbox"][4][7] == 0x11 and byname["textbox"][2] >= 140 + 7 + 16,
          "emboss flags and the textbox's text rows")
    check(byname["shadow"][2] >= 179 + 3, "shadow rows")
    last = ops[-1]
    check(last[1] <= 220 - 100 and last[2] >= 233 + 100, "an art tick may overhang: rows widened")


def tables():
    src = open(os.path.join(ROOT, "apps", "cc", "gk.inc")).read()
    m = re.search(r"^gk_ops:(.*?)^section", src, re.S | re.M)
    routines = re.findall(r"_(?:gk|bx)_\w+", m.group(1)) if m else []
    want = {n: "_bx_" + n if n in ("spr", "fill", "dither", "dots") else "_gk_" + n
            for n in os88gk.OPS}
    for name, (num, _) in os88gk.OPS.items():
        check(num < len(routines) and routines[num] == want[name],
              "gk.inc gk_ops[%d] is %s, the compiler's %s wants %s" %
              (num, routines[num] if num < len(routines) else "missing", name, want[name]))
    check(len(routines) == len(os88gk.OPS), "gk_ops has %d rows" % len(routines))
    hdr = open(os.path.join(ROOT, "apps", "cc", "gk.h")).read()
    for name, v in re.findall(r"^#define (GK_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", hdr, re.M):
        if name in os88gk.KIT:
            check(os88gk.KIT[name] == int(v, 0), "%s: gk.h %s, compiler %d" % (name, v, os88gk.KIT[name]))
        elif not name.startswith(("GK_ROLES", "GKM_")):
            check(False, "gk.h's %s has no name in the compiler" % name)
    m = re.search(r"cmp bl,(\d+)\s*\n\s*ja \.yes", src)
    check(m and int(m.group(1)) == max(v[0] for v in os88gk.OPS.values()),
          "gk_scene's last op number")


REFUSE = {
    'button 0 0 10 10 "x" 1': "takes 7",
    'text 0 0 10 "x" 0 1 0 #3': "no rect",
    'box %1 0 10 10 0 0 #3': "constant",
    'box 0 0 10 10 0 "x"': "string",
    'button 0 0 10 10 %1 1 0': "where a string",
    'box 0 0 10 10 0 %32': "31",
    'zap 0 0': "unknown op",
    'box 0 0 10 10 nope 0': "unknown name",
    'fill 0 %1 10 10 0': "constants",
    'if %1=200 box 0 0 1 1 0 0': "-128..127",
    'text 0 0 10 "' + "x" * 64 + '" 0 1 0': "63 characters",
    'box 0 0 1 1 0 0 #1\n  box 0 0 1 1 0 0 #1': "twice",
    'if %1 {\n  if %2 {': "nest",
    'box 0 0 1 1 0 99999': "16 bits",
}


def refusals():
    for body, want in REFUSE.items():
        try:
            compile_text("scene 1 10 10\n  %s\nend\n" % body, {})
        except os88gk.GkError as e:
            check(want in str(e), "refusal of %r says %r, wanted %r" % (body, str(e), want))
        else:
            check(False, "accepted: %r" % body)
    for text, want in (("scene 1 1 1\n box 0 0 1 1 0 0\n", "no `end`"),
                       ("box 0 0 1 1 0 0\n", "outside a scene"),
                       ("scene 1 1 1\nend\nscene 1 1 1\nend\n", "twice")):
        try:
            compile_text(text, {})
        except os88gk.GkError as e:
            check(want in str(e), "%r says %r" % (text, str(e)))
        else:
            check(False, "accepted: %r" % text)


def main():
    golden()
    orins()
    roundtrip()
    tables()
    refusals()
    for f in FAILS:
        print("t_gkscene: FAIL " + f)
    if FAILS:
        sys.exit(1)
    print("t_gkscene: ok - golden record, D7 OR-ins, round trip, gk_ops/gk.h tables, %d refusals"
          % (len(REFUSE) + 3))


if __name__ == "__main__":
    main()
