#!/usr/bin/env python3
"""The Speedy BASIC AOT frontend accepts every shipped demo without gaps."""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import speedybasic_compile as sbc  # noqa: E402


def check(value, message):
    if not value:
        raise AssertionError(message)


def main():
    demo_dir = os.path.join(ROOT, "apps", "speedybasic", "demos")
    manifest = os.path.join(demo_dir, "MANIFEST.TXT")
    names = [x.strip() for x in open(manifest, encoding="ascii")
             if x.strip() and not x.startswith("#")]
    check(len(names) == 29, "expected the complete 29-demo manifest")
    for name in names:
        path = os.path.join(demo_dir, name)
        program = sbc.compile_source(open(path, encoding="latin1").read(), path)
        check(program.statements, name + " produced no IR")
        check(all(s.op != "UNKNOWN" for s in program.statements),
              name + " retained an unknown statement")

    toks = sbc.tokenize("A=1.25E2+&H10\\\\3\n")
    check(any(t.kind == "NUM" and t.value == 125 for t in toks),
          "decimal exponent token")
    check(sum(t.text == "\\" for t in toks) == 1,
          "vendored doubled-backslash token")
    expr = sbc.parse_expr([t for t in sbc.tokenize("2+3*4")
                           if t.kind not in ("NL", "EOF")])
    check(expr.op == "binary" and expr.value == "+" and
          expr.args[1].value == "*", "expression precedence AST")

    linked = sbc.compile_source(
        "FOR I=1 TO 2\nIF I THEN\nGOSUB X\nEND IF\nNEXT I\nEND\nX:\nRETURN\n")
    check(linked.statements[0].jump > 0, "FOR/NEXT link")
    check(any(s.op == "GOSUB" and s.jump >= 0 for s in linked.statements),
          "GOSUB label link")

    for name in ("HELLO", "PATTERN", "STARS3D"):
        src = os.path.join(demo_dir, name + ".BAS")
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, name + ".c")
            subprocess.run([sys.executable,
                            os.path.join(ROOT, "tools", "speedybasic_compile.py"),
                            src, "-o", out, "--name", name], check=True)
            generated = open(out, encoding="utf-8").read()
            check("int sbp_program(int budget)" in generated,
                  name + " missing runtime entry")
            check('compiler/runtime.c"' in generated,
                  name + " missing shared runtime")

            if name == "PATTERN":
                check("sbr_line(sbp_tint[0]" in generated,
                      "PATTERN LINE lowering")
            if name == "STARS3D":
                for needle in ("switch(sbp_pc)", "sbm_make(",
                               "SBR_NUM_IDIV", "SBR_NUM_MOD",
                               "SBR_NUM_AND", "sbr_poke("):
                    check(needle in generated,
                          "STARS3D missing native lowering " + needle)

    flow = sbc.compile_source(
        "DIM A(9), S$\nA(1)=3\nX=A(1)+2\n"
        "IF X=5 THEN\nX=X AND 7\nELSE\nX=0\nEND IF\n"
        "FOR I=1 TO 2\nNEXT I\nWHILE X\nX=X-1\nWEND\n"
        "DO WHILE X<2\nX=X+1\nLOOP\n"
        "DO\nX=X-1\nLOOP UNTIL X=0\n"
        "GOSUB SUBR\nGOTO DONE\nSUBR:\nRETURN\nDONE:\nEND\n")
    lowered = sbc.emit_c(flow, "FLOW")
    for needle in ("sbr_num_array_store", "SBR_NUM_AND", "sbp_gosub",
                   "case 1", "while(budget-->0)"):
        check(needle in lowered, "control lowering missing " + needle)
    # A false structured IF must enter the ELSE body, not execute ELSE's
    # true-branch escape case.
    check("if(!sbr_num_truth())sbp_pc=8;else sbp_pc=6" in lowered,
          "structured ELSE false target")
    print("speedybasic compiler: 29 demos parsed; HELLO/PATTERN/STARS3D lowered")


if __name__ == "__main__":
    main()
