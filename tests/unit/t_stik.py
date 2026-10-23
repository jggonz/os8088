#!/usr/bin/env python3
"""SPEC.md 110.10: a STICKY APP's shared half, between
MartyPC runs.

`apps/os88stik.inc` is an SDK include (docs/INDEX.md lists it) and
`tools/os88stik.py` is the independent host reader and writer every Sticky App gate's
fixture comes from. Until this row, nothing in `make` touched either: the
include was assembled only by `make stiktest`, the host tool's `--selfcheck`
was run by nobody, and the two agreed about the Sticky App header only because they
were written the same afternoon. So:

  A  `tools/os88stik.py --selfcheck` passes - its own round trips and every
     refusal it mirrors from the boot reader and from stik_load.
  B  `tests/stikprob/stikprob.asm` ASSEMBLES (into a temp file, never
     /dev/null - CLAUDE.md), so os88stik.inc is assembled by every `make`.
  C  the Sticky App header's layout in os88stik.inc (`STIK_H_*`, STIK_HDR_SIZE, the
     magic, the version, the position flag) is where os88stik.py's writer puts
     each field - checked by writing a header with the host tool and reading
     it back at the include's offsets, which is the comparison that matters.
  D  the host tool's copies of kern_big's DERIVED constants - CFG_FBUF (a
     SYSTEM.CFG this size or less carries no trailer the boot reads) and
     DESK_SC0 (the first shortcut's zone) - equal the kernel's, evaluated BY
     NASM from the kernel's own `equ` chain. Both are expressions, which
     os88geom deliberately refuses to evaluate, so t_mirror cannot hold them:
     CFG_FBUF went 131 -> 141 when #248 added two port-pin keys, and the
     second reader then disagreed with the machine about a 132-141-byte file
     with nothing failing - and 141 -> 152 under #252, which this row
     caught at the merge. (DVOL_MAX and SC_MAX are plain integers and are
     os88geom's.)

  E  every shipped SYSTEM image carries STIKAPPS/DESKCAL.O88, byte for
     byte build/deskcal.o88 - and the two small system disks byte for byte
     its SMALL build, build/smallapp/deskcal.o88 (STIK_DESKONLY off: the
     titled fallback and the worker arm, SPEC.md 110.7) - and the same for
     the Desk Clock, build/deskclok.o88 and build/smallapp/deskclok.o88
     (SPEC.md 110.14.8) - read by t_image's
     own FAT12 walker and not os88disk's: the four geometries, both small
     system disks and emu.img (when `make emu` has built it). The live
     media's list is t_livefull's.
  F  and NO data image does: no apps disk, no category disk, no media disk,
     no small apps disk carries STIKAPPS or a DESKCAL.O88 - a widget
     belongs to the BOOT volume, and its row names a path on it (SPEC.md
     110.1, the THEWIRE rule).
  W  the WIDGET's (v2 wave 3, SPEC.md 110.11): STIK_HF_CONT is the host
     tool's; every STIK_WIDGET bss field the soak rows read out of a running
     probe (stik_hv, stik_tagp, stik_wf, stik_li, ...) is where
     os88stik.widget_off() says, EVALUATED BY NASM from the desk probe's own
     build; and STIK_HOVER_T / STIK_LEAVE_T are kern_big's DOCK_HOVER_T /
     DOCK_LEAVE_T, which the widget reuses by value (SPEC.md 30.6).
  N  Sticky Note's numbers are the host tool's (SPEC.md 110.13): the
     payload v2's layout (stiknote.asm's SNP_* / SN_* against os88stik.py's
     STIKNOTE_*), the bss fields tests/stiknote.py reads, where the pad's
     presses land, and its geometry table - each EVALUATED BY NASM - and
     STIKNOTE.O88 rides every system image beside the calendar (E), with
     its NOTES folder - SYSTEM/APPDATA/NOTES with STIKNOTE_SLOTS directory
     slots and WELCOME.TXT, build/notes/WELCOME.TXT byte for byte - and
     neither rides a data image (F).
  G  the Desk Calendar's numbers are the host tool's: the payload's layout
     (deskcal.asm's DCP_* against os88stik.py's DESKCAL_*), its own bss
     fields the soak rows read out of a running guest (DC_BSS_*), and the
     include's stik_st offset - each EVALUATED BY NASM from the package's
     own source, so a moved field fails `make` rather than a soak row.

**BREAK IT ON PURPOSE**: set os88stik.py's CFG_FBUF to 131 and D fails; move
STIK_H_SW to 9 in os88stik.inc and C fails; put a 32-bit register in
os88stik.inc and B fails; drop $(STIKARGS) from $(IMG360)'s recipe and E fails
for os8088-360.img; add $(STIKARGS) to an apps disk and F fails; move
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
import os88stik                                           # noqa: E402

NASM = os.environ.get("NASM", "nasm").split()[0]
EQU = re.compile(r"^\s*([A-Z_][A-Z0-9_]*)\s+equ\s+([^;\n]+)", re.M)
COND = re.compile(r"^\s*%(\w+)\s*(\S*)")
IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")


def plain_equs(path):
    """{NAME: int} for os88stik.inc's plain-integer equs."""
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
SMALL_SYSTEM = ("small.img", "small360.img")    # ...which take the small build
# ON DEMAND, so read when they are there and NEWER than the package: `make`
# builds neither the small pair (`make small`) nor emu.img (`make emu`), and
# a tree that built one before this change would otherwise go red on an
# image nothing rebuilt. `soak -k smallreq` is the small pair's own reader.
OPTIONAL_SYSTEM = ["small.img", "small360.img", "emu.img"]
DATA_IMAGES = ["apps.img", "apps120.img", "apps720.img", "apps360.img",
               "media360.img", "office360.img", "network360.img",
               "games360.img", "emu360.img"]
OPTIONAL_DATA = ["smallapps.img", "smallapps360.img"]   # `make smallapps`
NOTES_DIR = os88stik.STIKNOTE_DIR      # Sticky Note's notes (SPEC.md 110.13.1)
NOTES_SLOTS = os88stik.STIKNOTE_SLOTS  # '.', '..' and 30 notes


def walk(path):
    """{'STIKAPPS/DESKCAL.O88': bytes, ...} for a FAT12 floppy, through
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
            chain, _ = v.chain(clus)        # a folder: its DIRECTORY SLOTS
            out[key + "/"] = len(chain) * v.spc * v.byts // 32
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
             "DC_BSS_ED", "DC_BSS_EDI", "DC_BSS_SELK", "DC_BSS_SELI",
             "DC_BSS_PANE", "DC_BSS_ATOP", "DC_BSS_TTOP", "DC_BSS_ACNT",
             "DC_BSS_EBUF", "DC_BSS_NOW", "DC_BSS_LNOW", "DC_BSS_C3K",
             "DC_BSS_C3A", "DC_BSS_C3T", "DC_BSS_LIT", "DC_BSS_RMSG",
             "DC_BSS_LINE", "DC_BSS_BAND",
             "DCA_FLAGS", "DCA_LEAD", "DCA_TEXT", "DCT_TEXT", "DCP_LEAD",
             "DCP_FLAGS", "DCA_REMIND", "DCA_FIRED", "DCA_SNOOZED",
             "DCA_DUE", "DCP_SNZT", "DCP_SNZD", "DC_SNOOZE", "DCT_DONE",
             "stik_st", "stik_hd", "stik_li", "stik_fw",
             "stik_file - os88_image_end",
             "dc_line - os88_image_end", "dc_band - os88_image_end",
             "GFXE_BAND_W", "GFXE_BAND_H", "STIK_FACE_W",
             "STIK_PAYLEN", "STIK_PAYVER", "dc_geos", "DCG_SZ", "DCG_BELLX",
             "dc_lays", "STIKL_SZ"]
    src = open(os.path.join(ROOT, "apps", "deskcal", "deskcal.asm")).read()
    probe = ("\n    db 'T_STIK_PROBE'\n" +
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
    i = b.index(b"T_STIK_PROBE") + len("T_STIK_PROBE")
    e = dict(zip(names, struct.unpack_from("<%dH" % len(names), b, i)))
    g, n = e["dc_geos"], e["DCG_SZ"]
    e["geos"] = [b[g + k * n:g + (k + 1) * n] for k in range(3)]
    g, n = e["dc_lays"], e["STIKL_SZ"]
    e["lays"] = [b[g + k * n:g + (k + 1) * n] for k in range(3)]
    return e


def stiknote_equs(tmp):
    """stiknote.asm's own numbers, evaluated by NASM - deskcal_equs' shape:
    the payload, the bss fields the soak row reads, and the pad's geometry
    tables."""
    names = ["SNP_LEN", "SNP_VER", "SNP_NAME", "SNP_FLAGS", "SNP_NC",
             "SNP_TAB", "SN_COLSZ", "SN_MAXC", "SN_BOX0", "SN_BOX1",
             "SN_NCOL", "SN_HEAD", "SN_MAXKB", "SN_BSS",
             "sn_n - os88_image_end", "sn_cur - os88_image_end",
             "sn_hlen - os88_image_end", "sn_doc - os88_image_end",
             "sn_ask - os88_image_end", "sn_wslot - os88_image_end",
             "sn_hbuf - os88_image_end",
             "stik_st", "stik_file - os88_image_end",
             "STIK_FACE_W", "STIK_PAYLEN", "STIK_PAYVER", "SNP_S", "SNP_TW",
             "SNP_BTW", "SNP_PLUSX", "SNP_PREVX", "SNP_NEXTX", "SNP_OPENX",
             "SNP_BX", "SNP_BTX", "snp_geos", "SNPG_SZ", "snp_lays",
             "STIKL_SZ"]
    src = open(os.path.join(ROOT, "apps", "stiknote", "stiknote.asm")).read()
    probe = ("\n    db 'T_STIK_PROBE'\n" +
             "".join("    dw %s\n" % n for n in names))
    at = src.rindex("    align 16, db 0")
    path = os.path.join(tmp, "stiknote_probe.asm")
    open(path, "w").write(src[:at] + probe + src[at:])
    out = os.path.join(tmp, "stiknote_probe.bin")
    r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/stiknote/", "-o", out, path],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        raise RuntimeError(r.stderr.strip()[-400:])
    b = open(out, "rb").read()
    i = b.index(b"T_STIK_PROBE") + len("T_STIK_PROBE")
    e = dict(zip(names, struct.unpack_from("<%dH" % len(names), b, i)))
    for key, tab, sz in (("geos", "snp_geos", "SNPG_SZ"),
                         ("lays", "snp_lays", "STIKL_SZ")):
        g, n = e[tab], e[sz]
        e[key] = [b[g + k * n:g + (k + 1) * n] for k in range(3)]
    return e


def deskclok_equs(tmp):
    """deskclok.asm's own numbers, evaluated by NASM - stiknote_equs' shape:
    the payload and the bss fields tests/deskclock.py reads."""
    names = ["DKP_LEN", "DKP_VER", "STIK_PAYLEN", "STIK_PAYVER", "DKP_FACE",
             "DKP_FLAGS", "DKF_ONLY", "DKF_SECS", "DKF_H24", "DKF_ALARM",
             "DKF_INIT", "DKP_SIZE", "DKP_AH", "DKP_AM", "DKP_LABEL",
             "DK_LABMAX", "DKF_N", "DKF_ANALOG", "DK_BSS",
             "dk_win - os88_image_end", "dk_cw - os88_image_end",
             "dk_ch - os88_image_end", "dk_ring - os88_image_end",
             "dk_snz - os88_image_end", "dk_swin - os88_image_end",
             "dk_awin - os88_image_end", "dk_cok - os88_image_end",
             "stik_file - os88_image_end", "stik_st",
             "STIK_FACE_W", "DK_SZW"]
    src = open(os.path.join(ROOT, "apps", "deskclok", "deskclok.asm")).read()
    probe = ("\n    db 'T_STIK_PROBE'\n" +
             "".join("    dw %s\n" % n for n in names))
    at = src.rindex("    align 16, db 0")
    path = os.path.join(tmp, "deskclok_probe.asm")
    open(path, "w").write(src[:at] + probe + src[at:])
    out = os.path.join(tmp, "deskclok_probe.bin")
    r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/deskclok/", "-o", out, path],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        raise RuntimeError(r.stderr.strip()[-400:])
    b = open(out, "rb").read()
    i = b.index(b"T_STIK_PROBE") + len("T_STIK_PROBE")
    e = dict(zip(names, struct.unpack_from("<%dH" % len(names), b, i)))
    e["szw"] = list(b[e["DK_SZW"]:e["DK_SZW"] + 6])
    return e


def probe_equs(tmp, names, defines=("-DDP_DESK",)):
    """tests/stikprob/stikprob.asm's numbers, evaluated by NASM in the build
    `defines` makes - deskcal_equs' marker shape."""
    src = open(os.path.join(ROOT, "tests", "stikprob", "stikprob.asm")).read()
    probe = ("\n    db 'T_STIK_PROBE'\n" +
             "".join("    dw %s\n" % n for n in names))
    at = src.rindex("    align 16, db 0")
    path = os.path.join(tmp, "stikprob_probe.asm")
    open(path, "w").write(src[:at] + probe + src[at:])
    out = os.path.join(tmp, "stikprob_probe.bin")
    r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/"]
                       + list(defines) + ["-o", out, path],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        raise RuntimeError(r.stderr.strip()[-400:])
    b = open(out, "rb").read()
    i = b.index(b"T_STIK_PROBE") + len("T_STIK_PROBE")
    return dict(zip(names, struct.unpack_from("<%dH" % len(names), b, i)))


def main():
    tmp = tempfile.mkdtemp(prefix="t_stik.")
    try:
        # A ---------------------------------------------------------------
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88stik.py"),
                            "--selfcheck"], capture_output=True, text=True,
                           cwd=ROOT)
        check(r.returncode == 0, "tools/os88stik.py --selfcheck passes",
              why="every Sticky App gate's fixture is written by it, and its "
                  "refusals are the boot reader's and stik_load's",
              got=(r.stdout + r.stderr)[-400:], want="exit 0")

        # B ---------------------------------------------------------------
        out = os.path.join(tmp, "stikprob.bin")
        r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                            "-o", out, "tests/stikprob/stikprob.asm"],
                           capture_output=True, text=True, cwd=ROOT)
        check(r.returncode == 0 and os.path.getsize(out) > 64,
              "tests/stikprob/stikprob.asm assembles: os88stik.inc builds",
              why="the include is SDK; without this only `make stiktest` "
                  "assembled it, so an edit that broke it passed `make` and "
                  "`make test-full`",
              got=r.stderr.strip()[-400:], want="a package image")
        # ...and the DESK build of the same source (SPEC.md 11.103, v2 W1):
        # DESKPROB, header flag bit 6 (OS88_F_DESK) set and nothing else new
        out = os.path.join(tmp, "deskprobe.bin")
        r = subprocess.run([NASM, "-f", "bin", "-w+error", "-I", "apps/",
                            "-DDP_DESK", "-o", out,
                            "tests/stikprob/stikprob.asm"],
                           capture_output=True, text=True, cwd=ROOT)
        ok = r.returncode == 0 and os.path.getsize(out) > 64
        check(ok, "the desk probe (-DDP_DESK) assembles",
              why="tests/desk*.py boot it; only `make stiktest` builds it",
              got=r.stderr.strip()[-400:], want="a package image")
        if ok:
            b = open(out, "rb").read()
            check(b[3] & 0x40 and b[16:25] == b"DeskProbe",
                  "the desk probe's header carries OS88_F_DESK (bit 6)",
                  why="the loader's top-down slot is keyed on it (SPEC.md "
                      "11.103.9), and tests/deskdock.py asserts that slot",
                  got=hex(b[3]), want="bit 6 set")

        # C ---------------------------------------------------------------
        inc = plain_equs(os.path.join(ROOT, "apps", "os88stik.inc"))
        check(inc.get("STIK_HDR_SIZE") == os88stik.STIK_HDR_SIZE,
              "STIK_HDR_SIZE agrees", got=inc.get("STIK_HDR_SIZE"),
              want=os88stik.STIK_HDR_SIZE)
        check(struct.pack("<H", inc.get("STIK_HDR_MAGIC", 0))
              == os88stik.STIK_HDR_MAGIC, "the 'DA' magic agrees",
              got=inc.get("STIK_HDR_MAGIC"), want=os88stik.STIK_HDR_MAGIC)
        check(inc.get("STIK_HDR_VER") == os88stik.STIK_HDR_VER,
              "the header version agrees", got=inc.get("STIK_HDR_VER"),
              want=os88stik.STIK_HDR_VER)
        check(inc.get("STIK_HF_POS") == os88stik.STIK_HF_POS,
              "the position flag agrees", got=inc.get("STIK_HF_POS"),
              want=os88stik.STIK_HF_POS)
        pay = b"\xAA" * 7
        h = os88stik.dat(pay, x=0x1234, y=0x2345, screen=(0x3456, 0x4567),
                        payver=0x56)
        fields = [("STIK_H_MAGIC", "<H", struct.unpack("<H",
                                                      os88stik.STIK_HDR_MAGIC)[0]),
                  ("STIK_H_VER", "<B", os88stik.STIK_HDR_VER),
                  ("STIK_H_FLAGS", "<B", os88stik.STIK_HF_POS),
                  ("STIK_H_X", "<H", 0x1234), ("STIK_H_Y", "<H", 0x2345),
                  ("STIK_H_SW", "<H", 0x3456), ("STIK_H_SH", "<H", 0x4567),
                  ("STIK_H_PAYVER", "<B", 0x56), ("STIK_H_LEN", "<H", len(pay))]
        for name, fmt, want in fields:
            off = inc.get(name)
            got = (struct.unpack_from(fmt, h, off)[0]
                   if off is not None and off + struct.calcsize(fmt) <= len(h)
                   else None)
            check(got == want, "os88stik.inc's %s (+%s) is where os88stik.py "
                  "writes that field" % (name, off), got=got, want=want,
                  why="the package and the host tool read one file; a skew "
                      "is a state file every boot calls DAMAGED")

        # W ---------------------------------------------------------------
        check(inc.get("STIK_HF_CONT") == os88stik.STIK_HF_CONT,
              "the face-origin flag agrees", got=inc.get("STIK_HF_CONT"),
              want=os88stik.STIK_HF_CONT,
              why="a widget's state file holds its FACE when +3 bit 1 is "
                  "set (SPEC.md 110.11); a skew converts a face twice")
        names = ["%s - os88_image_end" % n for n, _ in os88stik.WIDGET_FIELDS]
        names += ["STIK_BSS", "STIK_HOVER_T", "STIK_LEAVE_T", "STIK_W_DESK",
                  "STIK_W_FOCUS", "STIK_W_ARMED"]
        try:
            pe = probe_equs(tmp, names)
        except (RuntimeError, ValueError) as ex:
            pe = {}
            check(False, "the desk probe assembles with the marker",
                  got=str(ex))
        wo = os88stik.widget_off(pe.get("STIK_BSS", 0), 0)
        for n, _ in os88stik.WIDGET_FIELDS:
            got = pe.get("%s - os88_image_end" % n)
            check(got == wo[n], "os88stik.inc's %s is where widget_off() "
                  "says (+%s)" % (n, wo[n]), got=got, want=wo[n],
                  why="tests/desklayer.py reads the include's state out of a "
                      "running probe by these offsets")
        for n in ("STIK_W_DESK", "STIK_W_FOCUS", "STIK_W_ARMED"):
            check(pe.get(n) == getattr(os88stik, n), "%s agrees" % n,
                  got=pe.get(n), want=getattr(os88stik, n))
        try:
            kd = nasm_eval(["DOCK_HOVER_T", "DOCK_LEAVE_T"], "big", tmp)
        except (KeyError, RuntimeError) as ex:
            kd = {}
            check(False, "kern_big's DOCK_HOVER_T and DOCK_LEAVE_T evaluate",
                  got=str(ex))
        for mine, theirs in (("STIK_HOVER_T", "DOCK_HOVER_T"),
                             ("STIK_LEAVE_T", "DOCK_LEAVE_T")):
            check(pe.get(mine) == kd.get(theirs) and pe.get(mine),
                  "os88stik.inc's %s is the dock's %s" % (mine, theirs),
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
            check(k.get(name) == getattr(os88stik, name),
                  "tools/os88stik.py's %s is kern_big's" % name,
                  got=getattr(os88stik, name), want=k.get(name),
                  why="the host tool is the second reader of SYSTEM.CFG's "
                      "trailer; a stale copy disagrees with ovl_sc_load "
                      "about which files carry one, and no gate fails")

        # E / F ------------------------------------------------------------
        build = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
        bigpkg = os.path.join(build, "deskcal.o88")
        bigwant = open(bigpkg, "rb").read() if os.path.exists(bigpkg) else None
        check(bigwant is not None, "build/deskcal.o88 exists",
              why="every system disk carries it; run `make`")
        smallpkg = os.path.join(build, "smallapp", "deskcal.o88")
        snpkg = os.path.join(build, "stiknote.o88")
        snwant = open(snpkg, "rb").read() if os.path.exists(snpkg) else None
        dkpkg = os.path.join(build, "deskclok.o88")
        dkwant = open(dkpkg, "rb").read() if os.path.exists(dkpkg) else None
        check(dkwant is not None, "build/deskclok.o88 exists",
              why="every system disk carries it beside the calendar and the "
                  "note (SPEC.md 110.14); run `make`")
        wpath = os.path.join(build, "notes", "WELCOME.TXT")
        welcome = open(wpath, "rb").read() if os.path.exists(wpath) else None
        check(welcome is not None and welcome.startswith(b"Welcome\r\n")
              and b"\n" not in welcome.replace(b"\r\n", b""),
              "build/notes/WELCOME.TXT exists, its line ends CR LF",
              why="Note Pad's own line ends (SPEC.md 27.1); `make` makes "
                  "it from apps/stiknote/welcome.txt")
        check(snwant is not None, "build/stiknote.o88 exists",
              why="every system disk carries it beside the calendar "
                  "(SPEC.md 110.13); run `make`")
        for img in SYSTEM_IMAGES + OPTIONAL_SYSTEM:
            pkg, want = bigpkg, bigwant
            if img in SMALL_SYSTEM:
                pkg = smallpkg
                want = (open(pkg, "rb").read() if os.path.exists(pkg)
                        else None)
            p = os.path.join(build, img)
            if not os.path.exists(p):
                check(img in OPTIONAL_SYSTEM, "%s is missing" % img,
                      why="`make` builds it; the row reads what was built")
                continue
            if img in OPTIONAL_SYSTEM and (want is None or os.path.getmtime(p)
                                           < os.path.getmtime(pkg)):
                print("t_stik: %s predates %s - not read (rebuild it with "
                      "its own target)" % (img, os.path.relpath(pkg, build)))
                continue
            if (img in OPTIONAL_SYSTEM and snwant is not None
                    and os.path.getmtime(p) < os.path.getmtime(snpkg)):
                print("t_stik: %s predates build/stiknote.o88 - not read "
                      "(rebuild it with its own target)" % img)
                continue
            if (img in OPTIONAL_SYSTEM and dkwant is not None
                    and os.path.getmtime(p) < os.path.getmtime(dkpkg)):
                print("t_stik: %s predates build/deskclok.o88 - not read "
                      "(rebuild it with its own target)" % img)
                continue
            files = walk(p)
            got = files.get("STIKAPPS/DESKCAL.O88")
            check(got is not None, "%s carries STIKAPPS/DESKCAL.O88" % img,
                  why="a Sticky App ships in STIKAPPS\\ on the BOOT volume "
                      "(SPEC.md 110.1); $(STIKARGS) / $(SMALLSTIKARGS) thread it "
                      "into each system-disk recipe",
                  got=sorted(k for k in files if k.startswith("SYSTEM/")))
            if got is not None and want is not None:
                check(got == want, "%s's DESKCAL.O88 is %s byte for byte"
                      % (img, os.path.relpath(pkg, build)),
                      got=len(got), want=len(want))
            got = files.get("STIKAPPS/STIKNOTE.O88")
            if img in SMALL_SYSTEM:
                check(got is None, "%s carries no STIKAPPS/STIKNOTE.O88" % img,
                      why="kern_small has no desk layer and no associations: "
                          "the owner took Sticky Note off the 128KB disks "
                          "(SPEC.md 24.5.1)")
                got = None
            else:
                check(got is not None, "%s carries STIKAPPS/STIKNOTE.O88" % img,
                      why="the second Sticky App rides $(STIKAPPS) beside the "
                          "first (SPEC.md 110.13) - on the 360KB disk too, "
                          "which gave Paint up for it (SPEC.md 24.3.1.4)",
                      got=sorted(k for k in files
                                 if k.startswith("STIKAPPS/")))
            if got is not None and snwant is not None:
                check(got == snwant, "%s's STIKNOTE.O88 is "
                      "build/stiknote.o88 byte for byte" % img,
                      got=len(got), want=len(snwant))
            if img not in SMALL_SYSTEM:
                # THE NOTES FOLDER (SPEC.md 110.13.1): beside the package,
                # pre-sized - the kernel never grows a directory (18.5) -
                # and made to exist by WELCOME.TXT
                slots = files.get(NOTES_DIR + "/")
                check(slots is not None and slots >= NOTES_SLOTS,
                      "%s carries %s/ with %d directory slots or more"
                      % (img, NOTES_DIR, NOTES_SLOTS), got=slots,
                      want=NOTES_SLOTS,
                      why="$(STIKNOTESARG) and $(STIKNOTESOPT) ship it in "
                          "each system-disk recipe; a folder the kernel "
                          "made holds 14 notes at 512-byte clusters")
                got = files.get(NOTES_DIR + "/WELCOME.TXT")
                check(got is not None and got == welcome,
                      "%s's %s/WELCOME.TXT is build/notes/WELCOME.TXT "
                      "byte for byte" % (img, NOTES_DIR),
                      got=got and len(got), want=welcome and len(welcome))
            got = files.get("STIKAPPS/DESKCLOK.O88")
            check(got is not None, "%s carries STIKAPPS/DESKCLOK.O88" % img,
                  why="the third Sticky App rides $(STIKAPPS) beside the "
                      "others (SPEC.md 110.14) - on the 360KB disk too, "
                      "which gave its typefaces up for it (SPEC.md 24.3.1.5)",
                  got=sorted(k for k in files if k.startswith("STIKAPPS/")))
            dkp, dkw = dkpkg, dkwant
            if img in SMALL_SYSTEM:             # the Desk Clock's SMALL build
                dkp = os.path.join(build, "smallapp", "deskclok.o88")
                dkw = open(dkp, "rb").read() if os.path.exists(dkp) else None
                check(dkw is not None, "build/smallapp/deskclok.o88 exists",
                      why="the small system disks take the Desk Clock's "
                          "small build (SPEC.md 110.14.8); `make small`")
            if got is not None and dkw is not None:
                check(got == dkw, "%s's DESKCLOK.O88 is %s byte for byte"
                      % (img, os.path.relpath(dkp, build)),
                      got=len(got), want=len(dkw))
        for img in DATA_IMAGES + OPTIONAL_DATA:
            p = os.path.join(build, img)
            if not os.path.exists(p):
                check(img in OPTIONAL_DATA, "%s is missing" % img)
                continue
            bad = [k for k in walk(p) if k.startswith("STIKAPPS")
                   or k.endswith("DESKCAL.O88")
                   or k.endswith("STIKNOTE.O88")
                   or k.endswith("DESKCLOK.O88")]
            check(not bad, "%s carries no Sticky App" % img, got=bad, want=[],
                  why="a widget on an apps disk is a copy that is never the "
                      "one the desktop relaunches - its row names the boot "
                      "volume (SPEC.md 110.1, the THEWIRE rule of 92)")

        # N ----------------------------------------------------------------
        try:
            sn = stiknote_equs(tmp)
        except (RuntimeError, ValueError) as ex:
            sn = {}
            check(False, "stiknote.asm assembles with the probe", got=str(ex))
        for name, want in (
                ("SNP_LEN", os88stik.STIKNOTE_PAYLEN),
                ("STIK_PAYLEN", os88stik.STIKNOTE_PAYLEN),
                ("SNP_VER", os88stik.STIKNOTE_PAYVER),
                ("STIK_PAYVER", os88stik.STIKNOTE_PAYVER),
                ("SNP_NAME", os88stik.STIKNOTE_NAME),
                ("SNP_FLAGS", os88stik.STIKNOTE_FLAGS),
                ("SNP_NC", os88stik.STIKNOTE_NC),
                ("SNP_TAB", os88stik.STIKNOTE_TAB),
                ("SN_COLSZ", os88stik.STIKNOTE_COLSZ),
                ("SN_MAXC", os88stik.STIKNOTE_MAXC),
                ("SN_BOX0", os88stik.STIKNOTE_BOX0),
                ("SN_BOX1", os88stik.STIKNOTE_BOX1),
                ("SN_NCOL", len(os88stik.STIKNOTE_PAPERS)),
                ("SN_HEAD", os88stik.STIKNOTE_HEAD),
                ("SN_MAXKB", os88stik.STIKNOTE_MAXKB),
                ("SN_BSS", os88stik.STIKNOTE_STIK_BSS),
                ("sn_n - os88_image_end", os88stik.STIKNOTE_BSS_N),
                ("sn_cur - os88_image_end", os88stik.STIKNOTE_BSS_CUR),
                ("sn_hlen - os88_image_end", os88stik.STIKNOTE_BSS_HLEN),
                ("sn_doc - os88_image_end", os88stik.STIKNOTE_BSS_DOC),
                ("sn_ask - os88_image_end", os88stik.STIKNOTE_BSS_ASK),
                ("sn_wslot - os88_image_end", os88stik.STIKNOTE_BSS_WSLOT),
                ("sn_hbuf - os88_image_end", os88stik.STIKNOTE_BSS_HBUF),
                ("STIK_FACE_W", os88stik.STIKNOTE_FACE_W),
                ("SNP_S", os88stik.STIKNOTE_S),
                ("SNP_TW", os88stik.STIKNOTE_TW),
                ("SNP_BTW", os88stik.STIKNOTE_BTW),
                ("SNP_PLUSX", os88stik.STIKNOTE_PLUSX),
                ("SNP_PREVX", os88stik.STIKNOTE_PREVX),
                ("SNP_NEXTX", os88stik.STIKNOTE_NEXTX),
                ("SNP_OPENX", os88stik.STIKNOTE_OPENX),
                ("SNP_BX", os88stik.STIKNOTE_BX),
                ("SNP_BTX", os88stik.STIKNOTE_BTX),
                ("stik_file - os88_image_end",
                 os88stik.stik_hdr_off(os88stik.STIKNOTE_STIK_BSS)),
                ("stik_st", os88stik.lib_st_abs())):   # the block
            check(sn.get(name) == want, "stiknote.asm's %s is "
                  "tools/os88stik.py's (%s)" % (name, want),
                  got=sn.get(name), want=want,
                  why="tests/stiknote.py writes the state file, reads the "
                      "guest's bss and presses the pad by these numbers")
        for li, raw in enumerate(sn.get("geos", [])):
            host = os88stik.STIKNOTE_GEO[li]
            want = [host[f] for f in os88stik.STIKNOTE_GEO_FIELDS]
            check(list(raw[:len(want)]) == want, "snp_geos' layout %d is "
                  "os88stik.STIKNOTE_GEO[%d]" % (li, li),
                  got=list(raw[:len(want)]), want=want)
        for li, raw in enumerate(sn.get("lays", [])):
            check(raw[4] == os88stik.STIKNOTE_GEO[li]["H"],
                  "snp_lays' layout %d's card is snp_geos' height" % li,
                  got=raw[4], want=os88stik.STIKNOTE_GEO[li]["H"],
                  why="the composer pads the card snp_geos says; the include "
                      "sizes the window by snp_lays")
        snbin = os.path.join(build, "stiknote.bin")
        if os.path.exists(snbin):
            fl = open(snbin, "rb").read()[3]
            check(fl & 0xC1 == 0xC1, "stiknote.bin's header carries an icon, "
                  "OS88_F_DESK and OS88_F_STIK", got=hex(fl), want="0xc1 set")

        # K: the Desk Clock -------------------------------------------------
        try:
            dk = deskclok_equs(tmp)
        except (RuntimeError, ValueError) as ex:
            dk = {}
            check(False, "deskclok.asm assembles with the probe", got=str(ex))
        for name, want in (
                ("DKP_LEN", os88stik.DESKCLOK_PAYLEN),
                ("STIK_PAYLEN", os88stik.DESKCLOK_PAYLEN),
                ("DKP_VER", os88stik.DESKCLOK_PAYVER),
                ("STIK_PAYVER", os88stik.DESKCLOK_PAYVER),
                ("DKP_FACE", os88stik.DESKCLOK_FACE),
                ("DKP_FLAGS", os88stik.DESKCLOK_FLAGS),
                ("DKF_ONLY", os88stik.DESKCLOK_F_ONLY),
                ("DKF_SECS", os88stik.DESKCLOK_F_SECS),
                ("DKF_H24", os88stik.DESKCLOK_F_H24),
                ("DKF_ALARM", os88stik.DESKCLOK_F_ALARM),
                ("DKF_INIT", os88stik.DESKCLOK_F_INIT),
                ("DKP_SIZE", os88stik.DESKCLOK_SIZE),
                ("DKP_AH", os88stik.DESKCLOK_AH),
                ("DKP_AM", os88stik.DESKCLOK_AM),
                ("DKP_LABEL", os88stik.DESKCLOK_LABEL),
                ("DK_LABMAX", os88stik.DESKCLOK_LABMAX),
                ("DKF_N", len(os88stik.DESKCLOK_FACES)),
                ("DKF_ANALOG", os88stik.DESKCLOK_ANALOG),
                ("DK_BSS", os88stik.DESKCLOK_STIK_BSS),
                ("dk_win - os88_image_end", os88stik.DESKCLOK_BSS_WIN),
                ("dk_cw - os88_image_end", os88stik.DESKCLOK_BSS_CW),
                ("dk_ch - os88_image_end", os88stik.DESKCLOK_BSS_CH),
                ("dk_ring - os88_image_end", os88stik.DESKCLOK_BSS_RING),
                ("dk_snz - os88_image_end", os88stik.DESKCLOK_BSS_SNZ),
                ("dk_swin - os88_image_end", os88stik.DESKCLOK_BSS_SWIN),
                ("dk_awin - os88_image_end", os88stik.DESKCLOK_BSS_AWIN),
                ("dk_cok - os88_image_end", os88stik.DESKCLOK_BSS_COK),
                ("stik_file - os88_image_end",
                 os88stik.stik_hdr_off(os88stik.DESKCLOK_STIK_BSS)),
                ("stik_st", os88stik.lib_st_abs())):   # the block
            check(dk.get(name) == want, "deskclok.asm's %s is "
                  "tools/os88stik.py's (%s)" % (name, want),
                  got=dk.get(name), want=want,
                  why="tests/deskclock.py writes the state file and reads the "
                      "guest's bss by these numbers")
        check(dk.get("szw") == [w for k in os88stik.DESKCLOK_SZW for w in k],
              "DK_SZW is os88stik.DESKCLOK_SZW", got=dk.get("szw"),
              want=[w for k in os88stik.DESKCLOK_SZW for w in k])
        check(all(w % 8 == 0 for k in os88stik.DESKCLOK_SZW for w in k)
              and max(w for k in os88stik.DESKCLOK_SZW for w in k)
              <= dk.get("STIK_FACE_W", 0),
              "every face width is a byte multiple and no wider than "
              "STIK_FACE_W", why="the face is blitted on the byte grid "
              "(BLITP refuses an odd x) and the include's band is "
              "STIK_FACE_W wide")
        dkbin = os.path.join(build, "deskclok.bin")
        if os.path.exists(dkbin):
            fl = open(dkbin, "rb").read()[3]
            check(fl & 0xC1 == 0xC1, "deskclok.bin's header carries an icon, "
                  "OS88_F_DESK and OS88_F_STIK", got=hex(fl), want="0xc1 set")

        # G ----------------------------------------------------------------
        try:
            e = deskcal_equs(tmp)
        except (RuntimeError, ValueError) as ex:
            e = {}
            check(False, "deskcal.asm assembles with the probe", got=str(ex))
        pairs = [("DCP_LEN", os88stik.DESKCAL_PAYLEN),
                 ("STIK_PAYLEN", os88stik.DESKCAL_PAYLEN),
                 ("DCP_VER", os88stik.DESKCAL_PAYVER),
                 ("STIK_PAYVER", os88stik.DESKCAL_PAYVER),
                 ("DCP_MAXAPPT", os88stik.DESKCAL_NAPPT),
                 ("DCP_MAXTASK", os88stik.DESKCAL_NTASK),
                 ("DCP_APPT0", os88stik.DESKCAL_APPT0),
                 ("DCP_APPTSZ", os88stik.DESKCAL_APPT),
                 ("DCP_TASK0", os88stik.DESKCAL_TASK0),
                 ("DCP_TASKSZ", os88stik.DESKCAL_TASK),
                 ("DCP_NAPPT", 0), ("DCP_NTASK", 1),
                 ("DC_BSS", os88stik.DESKCAL_STIK_BSS),
                 ("DC_BSS_GRID", os88stik.DESKCAL_BSS_GRID),
                 ("DC_BSS_Y", os88stik.DESKCAL_BSS_Y),
                 ("DC_BSS_M", os88stik.DESKCAL_BSS_M),
                 ("DC_BSS_SEL", os88stik.DESKCAL_BSS_SEL),
                 ("DC_BSS_TY", os88stik.DESKCAL_BSS_TY),
                 ("DC_BSS_TM", os88stik.DESKCAL_BSS_TM),
                 ("DC_BSS_TD", os88stik.DESKCAL_BSS_TD),
                 ("DC_BSS_ED", os88stik.DESKCAL_BSS_ED),
                 ("DC_BSS_EDI", os88stik.DESKCAL_BSS_EDI),
                 ("DC_BSS_SELK", os88stik.DESKCAL_BSS_SELK),
                 ("DC_BSS_SELI", os88stik.DESKCAL_BSS_SELI),
                 ("DC_BSS_PANE", os88stik.DESKCAL_BSS_PANE),
                 ("DC_BSS_ATOP", os88stik.DESKCAL_BSS_ATOP),
                 ("DC_BSS_TTOP", os88stik.DESKCAL_BSS_TTOP),
                 ("DC_BSS_ACNT", os88stik.DESKCAL_BSS_ACNT),
                 ("DC_BSS_EBUF", os88stik.DESKCAL_BSS_EBUF),
                 ("DC_BSS_NOW", os88stik.DESKCAL_BSS_NOW),
                 ("DC_BSS_LNOW", os88stik.DESKCAL_BSS_LNOW),
                 ("DC_BSS_C3K", os88stik.DESKCAL_BSS_C3K),
                 ("DC_BSS_C3A", os88stik.DESKCAL_BSS_C3A),
                 ("DC_BSS_C3T", os88stik.DESKCAL_BSS_C3T),
                 ("DC_BSS_LIT", os88stik.DESKCAL_BSS_LIT),
                 ("DC_BSS_RMSG", os88stik.DESKCAL_BSS_RMSG),
                 ("DC_BSS_LINE", os88stik.DESKCAL_BSS_LINE),
                 ("DC_BSS_BAND", os88stik.DESKCAL_BSS_BAND),
                 ("DCA_FLAGS", 4), ("DCA_LEAD", 5), ("DCA_TEXT", 6),
                 ("DCT_TEXT", 4), ("DCP_LEAD", 2), ("DCP_FLAGS", 3),
                 ("DCA_REMIND", os88stik.APPT_REMIND),
                 ("DCA_FIRED", os88stik.APPT_FIRED),
                 ("DCA_SNOOZED", os88stik.APPT_SNOOZED),
                 ("DCA_DUE", os88stik.APPT_DUE),
                 ("DCP_SNZT", os88stik.DESKCAL_SNZT),
                 ("DCP_SNZD", os88stik.DESKCAL_SNZD),
                 ("DC_SNOOZE", os88stik.DESKCAL_SNOOZE),
                 ("DCT_DONE", os88stik.TASK_DONE),
                 ("dc_line - os88_image_end", os88stik.DESKCAL_BSS_LINE),
                 ("dc_band - os88_image_end", os88stik.DESKCAL_BSS_BAND),
                 ("GFXE_BAND_W", 176), ("STIK_FACE_W", 176),
                 ("GFXE_BAND_H", 32),
                 ("stik_file - os88_image_end",
                  os88stik.stik_hdr_off(os88stik.DESKCAL_STIK_BSS)),
                 # the shared library's block (SPEC.md 110.15): the state
                 # is at fixed SEGMENT offsets, not past the image
                 ("stik_st", os88stik.lib_st_abs()),
                 ("stik_hd", os88stik.lib_hdr_abs()),
                 ("stik_li", os88stik.state_off(True, 0, 0, 0)["stik_li"]),
                 ("stik_fw", os88stik.state_off(True, 0, 0, 0)["stik_fw"])]
        # v2: the calendar is a DESK widget - header flag bit 6, OS88_F_DESK
        # (SPEC.md 20.2, 110.3) - read off the BUILT package's header
        dbin = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"),
                            "deskcal.bin")
        if os.path.exists(dbin):
            fl = open(dbin, "rb").read()[3]
            check(fl & 0x40, "deskcal.bin's header carries OS88_F_DESK "
                  "(bit 6)", got=hex(fl), want="bit 6 set",
                  why="without it the loader gives the widget an ordinary "
                      "slot and the dock a tile (SPEC.md 11.103, 30)")
        # the three layouts' geometry (dc_geos) against the host mirror
        # every soak row computes its coordinates from
        for li, raw in enumerate(e.get("geos", [])):
            host = os88stik.DESKCAL_GEO[li]
            want = [host[f] for f in os88stik.DESKCAL_GEO_FIELDS]
            nf = len(os88stik.DESKCAL_GEO_FIELDS)
            check(list(raw[:nf]) == want and raw[e["DCG_BELLX"]]
                  == host["BELLX"], "dc_geos' layout %d is "
                  "os88stik.DESKCAL_GEO[%d]" % (li, li),
                  got=list(raw[:nf]) + [raw[e["DCG_BELLX"]]],
                  want=want + [host["BELLX"]],
                  why="the rows press and probe the calendar by the "
                      "host's copy of these numbers")
        # ...and the cards, the tag, the face and its default y (dc_lays,
        # os88stik.inc's STIK_LAYOUT: db cards, db tag, dw fy, then a height
        # and a gap per card), which every soak row's card_top comes from
        for li, raw in enumerate(e.get("lays", [])):
            host = os88stik.DESKCAL_GEO[li]
            nc, tag, fy = raw[0], raw[1], struct.unpack_from("<H", raw, 2)[0]
            cards = tuple((raw[4 + 2 * k], raw[5 + 2 * k]) for k in range(nc))
            face = (os88stik.DESKCAL_FACE_W,
                    sum(h + gp for h, gp in cards) + tag)
            check((cards, tag, face, fy) == (host["cards"], host["tag"],
                                             host["face"], host["fy"]),
                  "dc_lays' layout %d is os88stik.DESKCAL_GEO[%d]'s cards, "
                  "tag, face and fy" % (li, li),
                  got=(cards, tag, face, fy),
                  want=(host["cards"], host["tag"], host["face"], host["fy"]),
                  why="deskcal_card_top and every row's probe rows come "
                      "from the host's copy of these numbers")
        for name, host in pairs:
            check(e.get(name) == host, "deskcal.asm's %s is tools/os88stik.py's"
                  % name, got=e.get(name), want=host,
                  why="the package and the host tool read one file and the "
                      "soak rows read the package's bss by these offsets")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    done("t_stik")


if __name__ == "__main__":
    main()
