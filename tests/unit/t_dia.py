#!/usr/bin/env python3
"""SPEC.md 107.10: a DESKTOP INTEGRATED APPLICATION's shared half, between
MartyPC runs.

`apps/os88dia.inc` is an SDK include (docs/INDEX.md lists it) and
`tools/os88dia.py` is the independent host reader and writer every DIA gate's
fixture comes from. Until this row, nothing in `make` touched either: the
include was assembled only by `make diatest`, the host tool's `--selfcheck`
was run by nobody, and the two agreed about the DIA header only because they
were written the same afternoon. So:

  A  `tools/os88dia.py --selfcheck` passes - its own round trips and every
     refusal it mirrors from the boot reader and from dia_load.
  B  `tests/diaprobe/diaprobe.asm` ASSEMBLES (into a temp file, never
     /dev/null - CLAUDE.md), so os88dia.inc is assembled by every `make`.
  C  the DIA header's layout in os88dia.inc (`DIA_H_*`, DIA_HDR_SIZE, the
     magic, the version, the position flag) is where os88dia.py's writer puts
     each field - checked by writing a header with the host tool and reading
     it back at the include's offsets, which is the comparison that matters.
  D  the host tool's copies of kern_big's DERIVED constants - CFG_FBUF (a
     SYSTEM.CFG this size or less carries no trailer the boot reads) and
     DESK_SC0 (the first shortcut's zone) - equal the kernel's, evaluated BY
     NASM from the kernel's own `equ` chain. Both are expressions, which
     os88geom deliberately refuses to evaluate, so t_mirror cannot hold them:
     CFG_FBUF went 131 -> 141 when #248 added two port-pin keys, and the
     second reader then disagreed with the machine about a 132-141-byte file
     with nothing failing. (DVOL_MAX and SC_MAX are plain integers and are
     os88geom's.)

  E  every shipped SYSTEM image carries SYSTEM/DIA/DESKCAL.O88, byte for
     byte build/deskcal.o88, read by t_image's own FAT12 walker and not
     os88disk's: the four geometries, both small system disks and emu.img
     (when `make emu` has built it). The live media's list is t_livefull's.
  F  and NO data image does: no apps disk, no category disk, no media disk,
     no small apps disk carries SYSTEM/DIA or a DESKCAL.O88 - a widget
     belongs to the BOOT volume, and its row names a path on it (SPEC.md
     107.1, the THEWIRE rule).
  W  the WIDGET's (v2 wave 3, SPEC.md 107.11): DIA_HF_CONT is the host
     tool's; every DIA_WIDGET bss field the soak rows read out of a running
     probe (dia_hv, dia_tagp, dia_wf, dia_li, ...) is where
     os88dia.widget_off() says, EVALUATED BY NASM from the desk probe's own
     build; and DIA_HOVER_T / DIA_LEAVE_T are kern_big's DOCK_HOVER_T /
     DOCK_LEAVE_T, which the widget reuses by value (SPEC.md 30.6).
  G  the Desk Calendar's numbers are the host tool's: the payload's layout
     (deskcal.asm's DCP_* against os88dia.py's DESKCAL_*), its own bss
     fields the soak rows read out of a running guest (DC_BSS_*), and the
     include's dia_st offset - each EVALUATED BY NASM from the package's
     own source, so a moved field fails `make` rather than a soak row.

**BREAK IT ON PURPOSE**: set os88dia.py's CFG_FBUF to 131 and D fails; move
DIA_H_SW to 9 in os88dia.inc and C fails; put a 32-bit register in
os88dia.inc and B fails; drop $(DIAARGS) from $(IMG360)'s recipe and E fails
for os8088-360.img; add $(DIAARGS) to an apps disk and F fails; move
DC_BSS_SEL in deskcal.asm and G fails.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import os88dia                                            # noqa: E402

NASM = os.environ.get("NASM", "nasm").split()[0]
EQU = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s+equ\s+([^;\n]+)", re.M)
COND = re.compile(r"^\s*%(\w+)\s*(\S*)")
IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")


def plain_equs(path):
    """{NAME: int} for os88dia.inc's plain-integer equs."""
    out = {}
    for name, val in EQU.findall(open(path, errors="replace").read()):
        try:
            out[name] = int(val.strip(), 0)
        except ValueError:
            pass
    return out


def kernel_equs(arm):
    """{NAME: expression} over every kernel source, the OTHER arm's
    `%ifdef KERN_BIG` / `%ifdef KERN_SMALL` blocks folded out (os88geom's
    rule, and its feature-symbol map), every other conditional opaque and
    first-wins."""
    try:
        import os88geom
        karm = os88geom._KARM
    except Exception:
        karm = {"KERN_BIG": "big", "KERN_SMALL": "small"}
    out = {}
    kdir = os.path.join(ROOT, "kernel")
    files = sorted(os.path.join(kdir, f) for f in os.listdir(kdir)
                   if f.endswith((".inc", ".asm")))
    for path in files:
        stack = []
        for line in open(path, errors="replace"):
            mo = COND.match(line)
            if mo:
                d, a = mo.group(1).lower(), mo.group(2)
                if d in ("ifdef", "ifndef") and a in karm:
                    stack.append(["k", (karm[a] == arm) == (d == "ifdef")])
                elif d.startswith("if"):
                    stack.append(["?", True])
                elif d == "else" and stack and stack[-1][0] == "k":
                    stack[-1][1] = not stack[-1][1]
                elif d.startswith("eli") and stack:
                    stack[-1] = ["?", True]
                elif d == "endif" and stack:
                    stack.pop()
                continue
            if not all(e[1] for e in stack):
                continue
            mo = EQU.match(line)
            if mo:
                out.setdefault(mo.group(1), mo.group(2).strip())
    return out


def nasm_eval(names, arm, tmp):
    """The kernel's own value of each of `names`, as nasm evaluates the
    kernel's own expressions. Raises with the reason when a name in the chain
    is not an `equ` (a label, a %define) - a loud refusal, not a guess."""
    eq = kernel_equs(arm)
    order, seen = [], set()

    def need(n):
        if n in seen:
            return
        seen.add(n)
        if n not in eq:
            raise KeyError("%s is not a kernel `equ` under kern_%s" % (n, arm))
        for dep in IDENT.findall(eq[n]):
            if dep in eq or dep.isupper():
                need(dep)
        order.append(n)
    for n in names:
        need(n)
    src = os.path.join(tmp, "eval_%s.asm" % arm)
    out = os.path.join(tmp, "eval_%s.bin" % arm)
    with open(src, "w") as f:
        f.write("cpu 8086\n")
        for n in order:
            f.write("%s equ %s\n" % (n, eq[n]))
        for n in names:
            f.write("dw %s\n" % n)
    r = subprocess.run([NASM, "-f", "bin", "-o", out, src],
                       capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("nasm refused the chain: %s" % r.stderr.strip())
    data = open(out, "rb").read()
    return dict(zip(names, struct.unpack("<%dH" % len(names), data)))


SYSTEM_IMAGES = ["os8088.img", "os8088-120.img", "os8088-720.img",
                 "os8088-360.img"]
# ON DEMAND, so read when they are there and NEWER than the package: `make`
# builds neither the small pair (`make small`) nor emu.img (`make emu`), and
# a tree that built one before this change would otherwise go red on an
# image nothing rebuilt. `soak -k smallreq` is the small pair's own reader.
OPTIONAL_SYSTEM = ["small.img", "small360.img", "emu.img"]
DATA_IMAGES = ["apps.img", "apps120.img", "apps720.img", "apps360.img",
               "media360.img", "office360.img", "network360.img",
               "games360.img"]
OPTIONAL_DATA = ["smallapps.img", "smallapps360.img"]   # `make smallapps`


def walk(path):
    """{'SYSTEM/DIA/DESKCAL.O88': bytes, ...} for a FAT12 floppy, through
    t_image's walker (deliberately not os88disk's)."""
    import t_image
    blob = open(path, "rb").read()
    v = t_image.Vol(blob, os.path.basename(path))
    out = {}
    for p, name11, attr, clus, size in v.walk():
        nm = name11.decode("ascii", "replace")
        name = nm[:8].strip() + ("." + nm[8:].strip() if nm[8:].strip()
                                 else "")
        key = p + name
        if attr & 0x10:
            out[key + "/"] = None
            continue
        data = bytearray()
        chain, _ = v.chain(clus)
        for c in chain:
            lba = v.cluster_lba(c)
            data += blob[lba * v.byts:(lba + v.spc) * v.byts]
        out[key] = bytes(data[:size])
    return out


def deskcal_equs(tmp):
    """deskcal.asm's own numbers, evaluated by NASM: the package is
    assembled with a probe appended that emits each as a word after a
    marker, so a value is what the BUILD computes, not what a regex reads."""
    names = ["DCP_LEN", "DCP_VER", "DCP_NAPPT", "DCP_NTASK", "DCP_MAXAPPT",
             "DCP_MAXTASK", "DCP_APPT0", "DCP_APPTSZ", "DCP_TASK0",
             "DCP_TASKSZ", "DC_BSS", "DC_BSS_GRID", "DC_BSS_Y", "DC_BSS_M",
             "DC_BSS_SEL", "DC_BSS_TY", "DC_BSS_TM", "DC_BSS_TD",
             "DC_BSS_OX", "DC_BSS_OY", "DC_BSS_ED", "DC_BSS_EDI", "DC_BSS_SELK", "DC_BSS_SELI", "DC_BSS_EBUF",
             "DC_BSS_TTOP", "DC_BSS_TSHOW", "DC_BSS_ALERT", "DC_BSS_NOW",
             "DC_BSS_LNOW", "DC_BSS_RMSG",
             "DCA_FLAGS", "DCA_LEAD", "DCA_TEXT", "DCT_TEXT", "DCP_LEAD",
             "DCP_FLAGS", "DCA_REMIND", "DCA_FIRED", "DCT_DONE",
             "dia_st - os88_image_end", "dia_file - os88_image_end",
             "dc_line - os88_image_end",
             "DIA_PAYLEN", "DIA_PAYVER"]
    src = open(os.path.join(ROOT, "apps", "deskcal", "deskcal.asm")).read()
    probe = ("\n    db 'T_DIA_PROBE'\n" +
             "".join("    dw %s\n" % n for n in names))
    at = src.rindex("    align 16, db 0")
    path = os.path.join(tmp, "deskcal_probe.asm")
    open(path, "w").write(src[:at] + probe + src[at:])
    out = os.path.join(tmp, "deskcal_probe.bin")
    r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/deskcal/", "-o", out, path],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        raise RuntimeError(r.stderr.strip()[-400:])
    b = open(out, "rb").read()
    i = b.index(b"T_DIA_PROBE") + len("T_DIA_PROBE")
    return dict(zip(names, struct.unpack_from("<%dH" % len(names), b, i)))


def probe_equs(tmp, names, defines=("-DDP_DESK",)):
    """tests/diaprobe/diaprobe.asm's numbers, evaluated by NASM in the build
    `defines` makes - deskcal_equs' marker shape."""
    src = open(os.path.join(ROOT, "tests", "diaprobe", "diaprobe.asm")).read()
    probe = ("\n    db 'T_DIA_PROBE'\n" +
             "".join("    dw %s\n" % n for n in names))
    at = src.rindex("    align 16, db 0")
    path = os.path.join(tmp, "diaprobe_probe.asm")
    open(path, "w").write(src[:at] + probe + src[at:])
    out = os.path.join(tmp, "diaprobe_probe.bin")
    r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/"]
                       + list(defines) + ["-o", out, path],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        raise RuntimeError(r.stderr.strip()[-400:])
    b = open(out, "rb").read()
    i = b.index(b"T_DIA_PROBE") + len("T_DIA_PROBE")
    return dict(zip(names, struct.unpack_from("<%dH" % len(names), b, i)))


def main():
    tmp = tempfile.mkdtemp(prefix="t_dia.")
    try:
        # A ---------------------------------------------------------------
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88dia.py"),
                            "--selfcheck"], capture_output=True, text=True,
                           cwd=ROOT)
        check(r.returncode == 0, "tools/os88dia.py --selfcheck passes",
              why="every DIA gate's fixture is written by it, and its "
                  "refusals are the boot reader's and dia_load's",
              got=(r.stdout + r.stderr)[-400:], want="exit 0")

        # B ---------------------------------------------------------------
        out = os.path.join(tmp, "diaprobe.bin")
        r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                            "-o", out, "tests/diaprobe/diaprobe.asm"],
                           capture_output=True, text=True, cwd=ROOT)
        check(r.returncode == 0 and os.path.getsize(out) > 64,
              "tests/diaprobe/diaprobe.asm assembles: os88dia.inc builds",
              why="the include is SDK; without this only `make diatest` "
                  "assembled it, so an edit that broke it passed `make` and "
                  "`make test-full`",
              got=r.stderr.strip()[-400:], want="a package image")
        # ...and the DESK build of the same source (SPEC.md 11.103, v2 W1):
        # DESKPROB, header flag bit 6 (OS88_F_DESK) set and nothing else new
        out = os.path.join(tmp, "deskprobe.bin")
        r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                            "-DDP_DESK", "-o", out,
                            "tests/diaprobe/diaprobe.asm"],
                           capture_output=True, text=True, cwd=ROOT)
        ok = r.returncode == 0 and os.path.getsize(out) > 64
        check(ok, "the desk probe (-DDP_DESK) assembles",
              why="tests/desk*.py boot it; only `make diatest` builds it",
              got=r.stderr.strip()[-400:], want="a package image")
        if ok:
            b = open(out, "rb").read()
            check(b[3] & 0x40 and b[16:25] == b"DeskProbe",
                  "the desk probe's header carries OS88_F_DESK (bit 6)",
                  why="the loader's top-down slot is keyed on it (SPEC.md "
                      "11.103.9), and tests/deskdock.py asserts that slot",
                  got=hex(b[3]), want="bit 6 set")

        # C ---------------------------------------------------------------
        inc = plain_equs(os.path.join(ROOT, "apps", "os88dia.inc"))
        check(inc.get("DIA_HDR_SIZE") == os88dia.DIA_HDR_SIZE,
              "DIA_HDR_SIZE agrees", got=inc.get("DIA_HDR_SIZE"),
              want=os88dia.DIA_HDR_SIZE)
        check(struct.pack("<H", inc.get("DIA_HDR_MAGIC", 0))
              == os88dia.DIA_HDR_MAGIC, "the 'DA' magic agrees",
              got=inc.get("DIA_HDR_MAGIC"), want=os88dia.DIA_HDR_MAGIC)
        check(inc.get("DIA_HDR_VER") == os88dia.DIA_HDR_VER,
              "the header version agrees", got=inc.get("DIA_HDR_VER"),
              want=os88dia.DIA_HDR_VER)
        check(inc.get("DIA_HF_POS") == os88dia.DIA_HF_POS,
              "the position flag agrees", got=inc.get("DIA_HF_POS"),
              want=os88dia.DIA_HF_POS)
        pay = b"\xAA" * 7
        h = os88dia.dat(pay, x=0x1234, y=0x2345, screen=(0x3456, 0x4567),
                        payver=0x56)
        fields = [("DIA_H_MAGIC", "<H", struct.unpack("<H",
                                                      os88dia.DIA_HDR_MAGIC)[0]),
                  ("DIA_H_VER", "<B", os88dia.DIA_HDR_VER),
                  ("DIA_H_FLAGS", "<B", os88dia.DIA_HF_POS),
                  ("DIA_H_X", "<H", 0x1234), ("DIA_H_Y", "<H", 0x2345),
                  ("DIA_H_SW", "<H", 0x3456), ("DIA_H_SH", "<H", 0x4567),
                  ("DIA_H_PAYVER", "<B", 0x56), ("DIA_H_LEN", "<H", len(pay))]
        for name, fmt, want in fields:
            off = inc.get(name)
            got = (struct.unpack_from(fmt, h, off)[0]
                   if off is not None and off + struct.calcsize(fmt) <= len(h)
                   else None)
            check(got == want, "os88dia.inc's %s (+%s) is where os88dia.py "
                  "writes that field" % (name, off), got=got, want=want,
                  why="the package and the host tool read one file; a skew "
                      "is a state file every boot calls DAMAGED")

        # W ---------------------------------------------------------------
        check(inc.get("DIA_HF_CONT") == os88dia.DIA_HF_CONT,
              "the face-origin flag agrees", got=inc.get("DIA_HF_CONT"),
              want=os88dia.DIA_HF_CONT,
              why="a widget's state file holds its FACE when +3 bit 1 is "
                  "set (SPEC.md 107.11); a skew converts a face twice")
        names = ["%s - os88_image_end" % n for n, _ in os88dia.WIDGET_FIELDS]
        names += ["DIA_BSS", "DIA_HOVER_T", "DIA_LEAVE_T", "DIA_W_DESK",
                  "DIA_W_FOCUS", "DIA_W_ARMED"]
        try:
            pe = probe_equs(tmp, names)
        except (RuntimeError, ValueError) as ex:
            pe = {}
            check(False, "the desk probe assembles with the marker",
                  got=str(ex))
        wo = os88dia.widget_off(pe.get("DIA_BSS", 0), 0)
        for n, _ in os88dia.WIDGET_FIELDS:
            got = pe.get("%s - os88_image_end" % n)
            check(got == wo[n], "os88dia.inc's %s is where widget_off() "
                  "says (+%s)" % (n, wo[n]), got=got, want=wo[n],
                  why="tests/desklayer.py reads the include's state out of a "
                      "running probe by these offsets")
        for n in ("DIA_W_DESK", "DIA_W_FOCUS", "DIA_W_ARMED"):
            check(pe.get(n) == getattr(os88dia, n), "%s agrees" % n,
                  got=pe.get(n), want=getattr(os88dia, n))
        try:
            kd = nasm_eval(["DOCK_HOVER_T", "DOCK_LEAVE_T"], "big", tmp)
        except (KeyError, RuntimeError) as ex:
            kd = {}
            check(False, "kern_big's DOCK_HOVER_T and DOCK_LEAVE_T evaluate",
                  got=str(ex))
        for mine, theirs in (("DIA_HOVER_T", "DOCK_HOVER_T"),
                             ("DIA_LEAVE_T", "DOCK_LEAVE_T")):
            check(pe.get(mine) == kd.get(theirs) and pe.get(mine),
                  "os88dia.inc's %s is the dock's %s" % (mine, theirs),
                  got=pe.get(mine), want=kd.get(theirs),
                  why="a widget's handle comes and goes on the strip's own "
                      "timings, by name in SPEC.md 30.6")

        # D ---------------------------------------------------------------
        try:
            k = nasm_eval(["CFG_FBUF", "DESK_SC0"], "big", tmp)
        except (KeyError, RuntimeError) as e:
            k = {}
            check(False, "the kernel's CFG_FBUF and DESK_SC0 evaluate",
                  got=str(e), want="two words")
        for name in ("CFG_FBUF", "DESK_SC0"):
            check(k.get(name) == getattr(os88dia, name),
                  "tools/os88dia.py's %s is kern_big's" % name,
                  got=getattr(os88dia, name), want=k.get(name),
                  why="the host tool is the second reader of SYSTEM.CFG's "
                      "trailer; a stale copy disagrees with ovl_sc_load "
                      "about which files carry one, and no gate fails")

        # E / F ------------------------------------------------------------
        build = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
        pkg = os.path.join(build, "deskcal.o88")
        want = open(pkg, "rb").read() if os.path.exists(pkg) else None
        check(want is not None, "build/deskcal.o88 exists",
              why="every system disk carries it; run `make`")
        for img in SYSTEM_IMAGES + OPTIONAL_SYSTEM:
            p = os.path.join(build, img)
            if not os.path.exists(p):
                check(img in OPTIONAL_SYSTEM, "%s is missing" % img,
                      why="`make` builds it; the row reads what was built")
                continue
            if (img in OPTIONAL_SYSTEM and want is not None
                    and os.path.getmtime(p) < os.path.getmtime(pkg)):
                print("t_dia: %s predates build/deskcal.o88 - not read "
                      "(rebuild it with its own target)" % img)
                continue
            files = walk(p)
            got = files.get("SYSTEM/DIA/DESKCAL.O88")
            check(got is not None, "%s carries SYSTEM/DIA/DESKCAL.O88" % img,
                  why="a DIA ships in SYSTEM\\DIA\\ on the BOOT volume "
                      "(SPEC.md 107.1); $(DIAARGS) / $(SMALLDIAARGS) thread it "
                      "into each system-disk recipe",
                  got=sorted(k for k in files if k.startswith("SYSTEM/")))
            if got is not None and want is not None:
                check(got == want, "%s's DESKCAL.O88 is build/deskcal.o88 "
                      "byte for byte" % img, got=len(got), want=len(want))
        for img in DATA_IMAGES + OPTIONAL_DATA:
            p = os.path.join(build, img)
            if not os.path.exists(p):
                check(img in OPTIONAL_DATA, "%s is missing" % img)
                continue
            bad = [k for k in walk(p) if k.startswith("SYSTEM/DIA")
                   or k.endswith("DESKCAL.O88")]
            check(not bad, "%s carries no DIA" % img, got=bad, want=[],
                  why="a widget on an apps disk is a copy that is never the "
                      "one the desktop relaunches - its row names the boot "
                      "volume (SPEC.md 107.1, the THEWIRE rule of 92)")

        # G ----------------------------------------------------------------
        try:
            e = deskcal_equs(tmp)
        except (RuntimeError, ValueError) as ex:
            e = {}
            check(False, "deskcal.asm assembles with the probe", got=str(ex))
        pairs = [("DCP_LEN", os88dia.DESKCAL_PAYLEN),
                 ("DIA_PAYLEN", os88dia.DESKCAL_PAYLEN),
                 ("DCP_VER", os88dia.DESKCAL_PAYVER),
                 ("DIA_PAYVER", os88dia.DESKCAL_PAYVER),
                 ("DCP_MAXAPPT", os88dia.DESKCAL_NAPPT),
                 ("DCP_MAXTASK", os88dia.DESKCAL_NTASK),
                 ("DCP_APPT0", os88dia.DESKCAL_APPT0),
                 ("DCP_APPTSZ", os88dia.DESKCAL_APPT),
                 ("DCP_TASK0", os88dia.DESKCAL_TASK0),
                 ("DCP_TASKSZ", os88dia.DESKCAL_TASK),
                 ("DCP_NAPPT", 0), ("DCP_NTASK", 1),
                 ("DC_BSS", os88dia.DESKCAL_DIA_BSS),
                 ("DC_BSS_GRID", os88dia.DESKCAL_BSS_GRID),
                 ("DC_BSS_Y", os88dia.DESKCAL_BSS_Y),
                 ("DC_BSS_M", os88dia.DESKCAL_BSS_M),
                 ("DC_BSS_SEL", os88dia.DESKCAL_BSS_SEL),
                 ("DC_BSS_TY", os88dia.DESKCAL_BSS_TY),
                 ("DC_BSS_TM", os88dia.DESKCAL_BSS_TM),
                 ("DC_BSS_TD", os88dia.DESKCAL_BSS_TD),
                 ("DC_BSS_OX", os88dia.DESKCAL_BSS_OX),
                 ("DC_BSS_OY", os88dia.DESKCAL_BSS_OY),
                 ("DC_BSS_ED", os88dia.DESKCAL_BSS_ED),
                 ("DC_BSS_EDI", os88dia.DESKCAL_BSS_EDI),
                 ("DC_BSS_SELK", os88dia.DESKCAL_BSS_SELK),
                 ("DC_BSS_SELI", os88dia.DESKCAL_BSS_SELI),
                 ("DC_BSS_EBUF", os88dia.DESKCAL_BSS_EBUF),
                 ("DC_BSS_TTOP", os88dia.DESKCAL_BSS_TTOP),
                 ("DC_BSS_TSHOW", os88dia.DESKCAL_BSS_TSHOW),
                 ("DC_BSS_ALERT", os88dia.DESKCAL_BSS_ALERT),
                 ("DC_BSS_NOW", os88dia.DESKCAL_BSS_NOW),
                 ("DC_BSS_LNOW", os88dia.DESKCAL_BSS_LNOW),
                 ("DC_BSS_RMSG", os88dia.DESKCAL_BSS_RMSG),
                 ("DCA_FLAGS", 4), ("DCA_LEAD", 5), ("DCA_TEXT", 6),
                 ("DCT_TEXT", 4), ("DCP_LEAD", 2), ("DCP_FLAGS", 3),
                 ("DCA_REMIND", os88dia.APPT_REMIND),
                 ("DCA_FIRED", os88dia.APPT_FIRED),
                 ("DCT_DONE", os88dia.TASK_DONE),
                 ("dc_line - os88_image_end", os88dia.DESKCAL_BSS_LINE),
                 ("dia_file - os88_image_end",
                  os88dia.dia_hdr_off(os88dia.DESKCAL_DIA_BSS)),
                 ("dia_st - os88_image_end",
                  os88dia.dia_st_off(os88dia.DESKCAL_DIA_BSS,
                                     os88dia.DESKCAL_PAYLEN))]
        for name, host in pairs:
            check(e.get(name) == host, "deskcal.asm's %s is tools/os88dia.py's"
                  % name, got=e.get(name), want=host,
                  why="the package and the host tool read one file and the "
                      "soak rows read the package's bss by these offsets")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    done("t_dia")


if __name__ == "__main__":
    main()
