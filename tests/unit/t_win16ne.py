#!/usr/bin/env python3
"""WIN16's census reads a Windows program the way the layer will, and its CPU
verdict comes from code it REACHED (SPEC.md 124.3, 124.4; docs/plans/
WIN16-PLAN.md S1).

    python3 tests/unit/t_win16ne.py

THE VERDICT IS A REFUSAL. S2's loader refuses a 186 program on an 8088 with a
sentence, and an 8088 that runs one anyway does not fault: 0x60-0x6F alias
the conditional jumps and 0x0F is `pop cs`, so it mis-executes silently. A
census that says 8086 for a 186 program is a machine that crashes later for
no stated reason; one that says 186 for an 8086 program refuses a program
the machine could run. Both directions are checked here, on a fixture whose
answer is known because it was written to have it.

FOUR PARTS, the first two always run:

  A  tests/win16/tiny16.asm, assembled here in five variants. The base build
     hides five 186-looking bytes behind a jump, dispatches through a switch
     table and carries a procedure nothing calls; the variants add a reached
     186 instruction, an unreached-but-prologued one, an x87 pair with OS
     fixups, and Open Watcom's guarded 286 probe. Each has one right verdict.
     THE NEGATIVE CONTROL is the base build's dark bytes: a linear sweep
     over the same segment finds 186 instructions in them, so the census's
     8086 is a decision and not an accident.
  B  hostile input: every prefix of the fixture and some noise. The census
     reads files the user hands it; a malformed one is a one-line refusal,
     never a traceback.
  C  the Open Watcom fixtures in build/win16/, when `make win16fixtures` has
     built them. Every one is 8086 code (-0), FPU.EXE is 8086 + x87, and the
     resources say what their .rc files say.
  D  the reference programs in win16ref/samples/, when the win16-reference
     skill has fetched them: the plan's table (WinMine 2.9 and both 3.1
     programs 186, CALC 3.00 8086 + x87) and D4's VGA System font. They are
     Microsoft's and Duff Software's files, never committed, so C and D are
     skipped with a note rather than failed when absent.
"""
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, eq, done                       # noqa: E402
import os88ne                                              # noqa: E402

SPECS = os88ne.load_specs()
FIXTURE = os.path.join(ROOT, "tests", "win16", "tiny16.asm")
BUILT = os.path.join(ROOT, "build", "win16")
SAMPLES = os.path.join(ROOT, "win16ref", "samples")


def imports(r):
    return sorted("%s.%s %s" % (i["module"], i["ordinal"], i["name"]) for i in r["imports"])


# --- E: the tables ---------------------------------------------------------
for mod, least in (("KERNEL", 150), ("USER", 300), ("GDI", 150)):
    check(len(SPECS.get(mod, {})) >= least,
          "tools/win16spec names %d %s ordinals, want at least %d"
          % (len(SPECS.get(mod, {})), mod, least),
          why="the census and S2's refusals name imports from these tables")
eq(SPECS.get("USER", {}).get(1, ("?",))[0], "MessageBox", "USER.1's name")
eq(SPECS.get("KERNEL", {}).get(91, ("?",))[0], "InitTask", "KERNEL.91's name")

# --- A: the NASM fixture ---------------------------------------------------
TINY_IMPORTS = ["KERNEL.30 WaitEvent", "KERNEL.91 InitTask",
                "USER.1 MessageBox", "USER.5 InitApp"]
tmp = tempfile.mkdtemp(prefix="win16ne")
built = {}
for v in ("", "T16_186", "T16_LONELY186", "T16_FPU", "T16_PROBE286"):
    out = os.path.join(tmp, (v or "BASE") + ".EXE")
    cmd = ["nasm", "-f", "bin", "-w+error", "-o", out, FIXTURE] + (["-D" + v] if v else [])
    p = subprocess.run(cmd, capture_output=True, text=True)
    if check(p.returncode == 0, "tiny16.asm assembles with %s" % (v or "no define"),
             got=p.stderr.strip()):
        built[v] = out

if "" in built:
    r = os88ne.analyse(built[""], SPECS)
    eq(r.get("verdict"), "8086", "tiny16 base: verdict",
       why="its only 186-looking bytes are data behind a jmp")
    eq(imports(r), TINY_IMPORTS, "tiny16 base: imports, named from the .spec tables")
    s1 = r["segments"][0]
    eq(s1["switch_tables"], 1, "tiny16 base: the jmp cs:[bx+table] switch was read")
    eq(s1["switch_entries"], 3, "tiny16 base: all three cases of the switch")
    eq(s1["prologue_seeds"], 1, "tiny16 base: t16_lonely found by its prologue")
    check(s1["dark_186"] >= 1,
          "tiny16 base: a linear sweep over the dark bytes finds 186 opcodes",
          why="THE NEGATIVE CONTROL - without it an 8086 verdict proves nothing "
              "about the descent; the trap bytes must still look like 186 code",
          got=s1["dark_186"], want=">= 1")
    check(s1["coverage"] < 100.0, "tiny16 base: the trap bytes stay unreached",
          got=s1["coverage"], want="< 100")
    eq((r["library"], r["selfload"], r["pmode_only"], r["dgroup"]),
       (False, False, False, "multiple"), "tiny16 base: the loader's flags")
    eq(r["resources"], [], "tiny16 base: no resource table (rsrctab == restab)")
    eq(r["description"], "tiny16 - os8088 WIN16 fixture (SPEC.md 124)",
       "tiny16 base: the non-resident description")

if "T16_186" in built:
    r = os88ne.analyse(built["T16_186"], SPECS)
    eq(r.get("verdict"), "186", "tiny16 -DT16_186: a reached push imm8")
    check("186 push imm8" in r.get("beyond_8086", {}), "tiny16 -DT16_186: names push imm8",
          got=r.get("beyond_8086"))

if "T16_LONELY186" in built:
    r = os88ne.analyse(built["T16_LONELY186"], SPECS)
    eq(r.get("verdict"), "186", "tiny16 -DT16_LONELY186: the prologue heuristic's code counts",
       why="a procedure reached only through a pointer is still code an 8088 "
           "would run - the heuristic errs toward refusing")

if "T16_FPU" in built:
    r = os88ne.analyse(built["T16_FPU"], SPECS)
    eq(r.get("verdict"), "8086 + x87", "tiny16 -DT16_FPU: verdict")
    eq(r.get("x87"), {"fld m64": 1, "fstp m64": 1}, "tiny16 -DT16_FPU: the x87 subset (D5)")
    eq(r.get("osfixups"), {"5": 2}, "tiny16 -DT16_FPU: two FIDRQQ OS fixups")

if "T16_PROBE286" in built:
    r = os88ne.analyse(built["T16_PROBE286"], SPECS)
    eq(r.get("verdict"), "8086", "tiny16 -DT16_PROBE286: a guarded smsw does not count",
       why="Open Watcom's start-up probes for a 286 this way in every program "
           "it builds; counting it would refuse all of them on an 8088")
    check(any(k.startswith("guarded 286") for k in r.get("beyond_8086", {})),
          "tiny16 -DT16_PROBE286: the guarded smsw is still REPORTED",
          got=r.get("beyond_8086"))

# --- B: hostile input ------------------------------------------------------
if "" in built:
    data = open(built[""], "rb").read()
    rng = random.Random(124)
    cases = [data[:n] for n in range(0, len(data), 7)]
    cases += [bytes(rng.randrange(256) for _ in range(600)) for _ in range(20)]
    cases += [b"MZ" + bytes(rng.randrange(256) for _ in range(900)) for _ in range(20)]
    flipped = []
    for k in range(40):
        b = bytearray(data)
        for _ in range(4):
            b[rng.randrange(0x80, len(b))] = rng.randrange(256)
        flipped.append(bytes(b))
    raised = []
    for c in cases + flipped:
        try:
            os88ne.analyse(c, SPECS, "hostile")
        except Exception as e:                  # noqa: BLE001 - that IS the check
            raised.append("%s: %s" % (e.__class__.__name__, e))
    eq(raised[:3], [], "%d truncated, random and bit-flipped files: none raises"
       % len(cases + flipped),
       why="the census reads whatever the user's Windows directory holds")
    dos = os88ne.analyse(b"MZ" + bytes(0x3E) + b"\x00" * 64, SPECS, "dos")
    check("DOS program" in dos.get("error", ""), "a DOS MZ is refused as a DOS program",
          got=dos)

# --- C: the Open Watcom fixtures ------------------------------------------
WATCOM = {"MSGBOX": "8086", "WINDOW": "8086", "MENU": "8086", "DIALOG": "8086",
          "PROFILE": "8086", "BITMAP": "8086", "FPU": "8086 + x87"}
have = [n for n in WATCOM if os.path.exists(os.path.join(BUILT, n + ".EXE"))]
if not have:
    print("  note: build/win16/ has no Open Watcom fixtures - `make win16fixtures` "
          "builds them; part C skipped")
for n in have:
    r = os88ne.analyse(os.path.join(BUILT, n + ".EXE"), SPECS)
    eq(r.get("verdict"), WATCOM[n], "%s.EXE (Open Watcom -0): verdict" % n)
    check(any(i["name"] is None for i in r.get("imports", [])) is False,
          "%s.EXE: every import has a name in tools/win16spec" % n,
          got=[i for i in r.get("imports", []) if i["name"] is None])
    res = {x["type"]: x for x in r.get("resources", [])}
    if n == "MENU":
        items = res.get("MENU", {}).get("detail", {}).get("items", [])
        flat = [(i["text"], i["flags"] & 0x0F) for p in items for i in p.get("items", [])]
        check(("&Red", 0x08) in flat and ("&Unavailable", 0x01) in flat,
              "MENU.EXE: Red is CHECKED and Unavailable GRAYED", got=flat)
    if n == "DIALOG":
        ctl = [c["class"] for c in res.get("DIALOG", {}).get("detail", {}).get("controls", [])]
        eq(ctl, ["STATIC", "EDIT", "BUTTON", "BUTTON", "BUTTON", "BUTTON", "LISTBOX",
                 "BUTTON", "BUTTON"], "DIALOG.EXE: control classes in order")
    if n == "BITMAP":
        bm = sorted((x["detail"]["width"], x["detail"]["bpp"])
                    for x in r.get("resources", []) if x["type"] == "BITMAP")
        eq(bm, [(16, 1), (16, 4)], "BITMAP.EXE: a 4bpp bitmap and its 1bpp twin")

# --- D: the reference programs --------------------------------------------
REF = [("winmine-2.9/WINMINE.EXE", "186"), ("w3-archive/WINDOWS/CALC.EXE", "8086 + x87"),
       ("w31/WINMINE.EXE", "186"), ("w31/CALC.EXE", "186 + x87")]
if not os.path.isdir(SAMPLES):
    print("  note: win16ref/samples/ is absent - the win16-reference skill fetches "
          "it; part D skipped")
else:
    for rel, want in REF:
        p = os.path.join(SAMPLES, rel)
        if os.path.exists(p):
            eq(os88ne.analyse(p, SPECS).get("verdict"), want, "%s: the plan's verdict" % rel)
    p = os.path.join(SAMPLES, "w3-archive/WINDOWS/SYSTEM/VGASYS.FON")
    if os.path.exists(p):
        f = [x["detail"] for x in os88ne.analyse(p, SPECS)["resources"] if x["type"] == "FONT"]
        eq([(d["face"], d["pixheight"], d["ascent"], d["avgwidth"], d["maxwidth"])
            for d in f], [("System", 16, 13, 7, 14)], "VGASYS.FON: D4's VGA System font")

# --- E: the machine's opcode table is the host's (SPEC.md 124.4.1) --------
# apps/win16/w16cens.inc decodes with w16optab.inc, which os88ne.py WRITES:
# a committed file that differs is a machine census that has drifted from the
# reference one. Regenerate with `python3 tools/os88ne.py --optab >
# apps/win16/w16optab.inc`.
OPTAB = os.path.join(ROOT, "apps", "win16", "w16optab.inc")
with open(OPTAB) as f:
    eq(f.read(), os88ne.optab_inc(), "apps/win16/w16optab.inc is os88ne.py --optab's")
tab, names = os88ne.optab()
eq((names[0x6A], names[0x0F], names[0x75], names[0xFF], names[0x2E]),
   ("T186", "OP0F", "BR8", "GRPFF", "PREFIX"), "the table's classes, spot-checked")
eq((tab[0x9A] & 7, tab[0xC8] & 7, (tab[0x80] >> 3) & 1), (4, 3, 1),
   "the table's lengths: call far 4, enter 3, the 80 group's modrm")

done("win16ne")
