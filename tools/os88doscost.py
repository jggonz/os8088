#!/usr/bin/env python3
"""What the DOS box is MADE OF, split the way `kern_dos` would split it.

    python3 tools/os88doscost.py              # the image and the bss
    python3 tools/os88doscost.py --procs      # ...every proc, largest first
    python3 tools/os88doscost.py --mouse      # kernel/mouse.inc's own split

docs/plans/KERN-DOS-PLAN.md §4.1.2 splits `apps/dos/dos.asm` into a CORE that
`kern_dos` assembles and a WINDOW half that only the package root pulls in,
and §6's largest row was an estimate of the first: *"~12,000; the box's image
is 30,731 and most of it is window"*.  It is not most of it, and this is the
instrument that says so - wave 1 measured 14,623 of image and 3,031 of bss
against that 12,000 (docs/reports/KERN-DOS-BUDGET-2026-09-13.md).

WHY IT SHIPS RATHER THAN LIVING IN A SESSION'S SCRATCH.  `tools/kernsize.py`'s
own header states the rule: a number nobody can produce in one command is a
number that stops being produced.  W2 performs this split for real and will
want to check its own arithmetic against the same reading, and every later
wave prices itself against the remainder.

HOW.  Symbol SPANS, which is `tools/incsize.py`'s method: assemble with
`[map all]`, sort by address WITHIN each section, and give every label the
distance to the next one.  That is what the assembler emitted - jump
distances, alignment and all - rather than source lines, which CLAUDE.md's
rung banner refuses for exactly this kind of question.

TWO TRAPS, both of which cost a wrong number on the way in:

  - **a local label belongs to the proc above it.**  nasm's map spells `.done`
    as `parent.done`, and nasm's OWN anonymous labels (`..@331.done`, from a
    `%%local` in a macro) have no parent at all - those belong to whatever
    span they fell inside, which in address order is the last NAMED one.
    Bucketing them together instead invents a 439-byte "(anonymous)" proc that
    is really two dozen routines.  It is docs/plans/DISK-CPU-PLAN.md §1's
    second trap, one instrument along.
  - **sections must be spanned separately.**  A flat sort mixes `.text` and
    `.bss` offsets, so a label's "next" can be in another section and the span
    is nonsense.  Against `kernel/mouse.inc` that read 1,629 bytes for a file
    the module table prices at 3,419.

THE CLASSIFICATION IS BY PREFIX AND THE RESIDUAL IS PRINTED, never absorbed:
what the split assumed has to be visible, because the split is a judgement
about where W2 will cut and the measurement underneath it is not.
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- which whole FILES fall on which side ------------------------------------
FILE_CLASS = {
    "apps/dos/dos.asm":     None,        # per-proc, below
    "apps/dos/dosh.inc":    "core",      # COMMAND.COM's built-ins (SPEC.md 96.30)
    "apps/dos/dosc.inc":    "window",    # the in-window prompt (96.33)
    "apps/dos/dosnet.inc":  "drop",      # the cable translation (96.26)
    "apps/os88con.inc":     "window",    # the console screen
    "apps/os88cp437.inc":   "window",    # THE CODE PAGE, AND IT IS THE WINDOW'S
                                        # (SPEC.md 96.44.5). It was "core" here
                                        # on the reasoning that character
                                        # output needs a code page - but
                                        # `con_cp437` has exactly ONE reader,
                                        # os88con.inc:932, and os88con.inc is
                                        # what %includes this file. The core's
                                        # own output is `dos_tty`, which is the
                                        # ROM's teletype and uses the BIOS
                                        # font. 1,280 bytes on the wrong side
    "apps/os88ui.inc":      "window",
    "apps/os88line.inc":    "window",
    "apps/os88sock.inc":    "drop",
    "apps/os88api.inc":     "core",
}

CORE = ("dos_int21", "dos_int33", "dos_int2", "dos_fh_", "dos_psp",
        "dos_build_psp", "dos_mcb_", "dos_exe_", "dos_exec_", "dos_load",
        "dos_find_", "dos_walk", "dos_cd_", "dos_date_", "dos_time_",
        "dos_dow", "dos_hook", "dos_restore", "dos_fcb", "dos_xms_",
        "dos_err", "dos_dta", "dos_name", "dos_83", "dos_up", "dos_drv_let",
        "dos_vol_", "dos_be", "dos_k_", "dos_arg", "dos_env_", "dos_envpath",
        "dos_ivt", "dos_bda", "dos_a20", "dos_clk", "dos_tick",
        "dos_save_machine", "dos_terminate", "dos_wild", "dos_prog_enter",
        "dos_jft", "dos_tty", "dos_ceq", "dos_is_exe", "dos_mou_",
        "dos_movedown", "dos_keeph",
        # ...and these two, which READ like the window's drive list beside
        # them and are called straight from dos_int21 (AH=0Eh).  W2's call
        # graph found it; the prefix rule below had them both wrong, which is
        # why tests/unit/t_dosseam.py walks reachability and does not trust a
        # name (SPEC.md 96.4.2).
        "dos_drv_count", "dos_drv_sel", "dos_drv_let", "dos_drv_bank",
        "dos_drv_recall")
WINDOW = ("dos_paint", "dos_click", "dos_key", "dos_entry", "dos_pref",
          "dos_menu", "dos_l_", "dos_lnk_", "dos_sav_", "dos_swap",
          "dos_fld_", "dos_mem_", "dos_mrad", "dos_mfld", "dos_bar_",
          "dos_furn_", "dos_erow", "dos_senv", "dos_setbox", "dos_line",
          "dos_defocus", "dos_go", "dos_rects", "dos_ico", "dos_about",
          "dos_title", "dos_ttl", "dos_win", "dos_tpl", "dos_drv_",
          "dos_trace_dump", "dos_path_make", "dos_path_take", "dos_con_",
          "dos_wake", "dos_close", "dos_open", "dos_assoc", "dos_stat",
          "dos_run", "dos_fsx_", "dos_repaint", "dos_place", "dos_is_lnk",
          "dos_oncmd", "dos_focused", "dos_btn_", "dos_fmt_", "dos_page_",
          # dos_drv_take / dos_drv_back only; the other dos_drv_* are CORE and
          # are named above, CORE being tested first.
          "dos_hex", "dos_dec", "dos_e_", "dos_wfld")
DROP = ("dos_pkt_", "dos_net", "dn_", "dos_blaster", "dos_cable")

# --- the same question of the BSS table, whose entries are absolute equates --
B_CORE = ("PSP", "MCB", "ARENA", "APARA", "AKB", "IMGSZ", "PRGSP", "SVSS",
          "SVSP", "PIC1", "PIC2", "ISEXE", "IVT", "BDA", "DTA", "FH", "JFT",
          "FCB", "FIND", "WSEG", "WBASE", "WLEN", "WDIRTY", "WFILL", "WOWN",
          "WBYTES", "CBYTES", "WFIL", "GAPN", "TRNOF", "EXIT", "BADFN",
          "DIR", "VOL", "NAME", "PAD", "ENV", "XM", "TICK", "CLK", "DATE",
          "TIME", "INT2", "HOOK", "TTY", "SHELL", "DSH", "CP", "KEEPH",
          "MOU", "TRACE", "PROG", "ARGS", "CWD", "SP", "SS", "BE", "K_",
          "SH", "PBUF", "FPBUF", "DQBUF", "PFBUF", "PFCB", "DVCWD", "FENT",
          "FSI", "FNAME", "DRV", "WILD", "UP", "STK", "W83")
B_WINDOW = ("LN_", "PLN", "MLN", "ELN", "BUF", "PAGE", "MX", "MY", "WIN",
            "STATE", "ERR", "MEMKB", "MRAD", "CON", "ERP", "NCELL", "NKEY",
            "CPW", "DRVOUT", "LEND", "TVOL", "TNAME", "PATH", "EBUF",
            "SBUF", "CNAME", "FBUF", "WNAME", "LBUF", "VSBUF")
B_DROP = ("PKT", "DN", "NET", "SOCK", "BLAST", "CABLE")


def classify(name, core, window, drop):
    for p in drop:
        if name.startswith(p):
            return "drop"
    for p in core:
        if name.startswith(p):
            return "core"
    for p in window:
        if name.startswith(p):
            return "window"
    return None


def nasm_map(src, incs, defines=()):
    """Every label -> (address, section), via `[map all]` on a copy."""
    d = tempfile.mkdtemp(prefix="os88doscost")
    a, mp = os.path.join(d, "x.asm"), os.path.join(d, "x.map")
    open(a, "w").write(open(os.path.join(ROOT, src)).read()
                       + "\n[map all %s]\n" % mp)
    args = ["nasm", "-f", "bin", "-w+error"]
    for i in incs:
        args += ["-I", os.path.join(ROOT, i) + os.sep]
    args += ["-D" + x for x in defines]
    args += ["-o", os.path.join(d, "x.bin"), a]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        sys.exit("os88doscost: %s would not assemble:\n%s" % (src, r.stderr[:600]))
    out, sec = [], None
    for line in open(mp):
        if line.startswith("---- "):
            sec = line.split("Section")[-1].strip() or line.strip()
            continue
        p = line.split()
        if len(p) == 3 and sec and "No Section" not in line:
            try:
                out.append((int(p[0], 16), p[2], sec))
            except ValueError:
                pass
    return out, os.path.getsize(os.path.join(d, "x.bin"))


def spans(rows):
    """(proc, section, bytes), with locals and anonymous labels rolled up."""
    bysec = collections.defaultdict(list)
    for addr, name, sec in rows:
        bysec[sec].append((addr, name))
    out = collections.Counter()
    for sec, lst in bysec.items():
        lst.sort()
        last = "?"
        for i, (addr, name) in enumerate(lst):
            nxt = lst[i + 1][0] if i + 1 < len(lst) else addr
            base = name.split(".")[0]
            if not base or base.startswith("..@"):
                base = last                   # nasm's own, see the header
            else:
                last = base
            out[(base, sec)] += max(0, nxt - addr)
    return out


def owners(files):
    """label -> the source file that defines it at column 0."""
    DEF = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
    out = {}
    for f in files:
        p = os.path.join(ROOT, f)
        if not os.path.exists(p):
            continue
        for l in open(p, encoding="utf-8", errors="replace"):
            m = DEF.match(l)
            if m and m.group(1) not in out:
                out[m.group(1)] = f
    return out


def report(title, buckets, total, residual):
    print("\n%s" % title)
    for k in ("core", "window", "drop", "unclassified"):
        if buckets[k] or k != "unclassified":
            print("  %-14s %6d  %5.1f%%"
                  % (k, buckets[k], 100.0 * buckets[k] / max(1, total)))
    if residual:
        residual.sort(reverse=True)
        print("  unclassified, largest first:")
        for n, name in residual[:12]:
            print("      %5d  %s" % (n, name))


def image_split(show_procs):
    src = "apps/dos/dos.asm"
    rows, size = nasm_map(src, ("apps/", "apps/dos/", "drivers/net/"))
    end = next((a for a, n, _ in rows if n == "os88_image_end"), size)
    rows = [r for r in rows if r[0] <= end]
    sp = spans(rows)
    files = ["apps/dos/dos.asm"]
    files += ["apps/dos/" + f for f in sorted(os.listdir(os.path.join(ROOT, "apps/dos")))
              if f.endswith(".inc")]
    files += ["apps/" + f for f in sorted(os.listdir(os.path.join(ROOT, "apps")))
              if f.endswith(".inc")]
    files += ["drivers/net/netpkg.inc"]
    own = owners(files)

    per_file = collections.Counter()
    buckets, residual, procs = collections.Counter(), [], []
    for (name, _sec), n in sp.items():
        f = own.get(name, "?")
        per_file[f] += n
        c = FILE_CLASS.get(f, "window")
        if c is None:
            c = classify(name, CORE, WINDOW, DROP)
        if c is None:
            residual.append((n, name))
            c = "unclassified"
        buckets[c] += n
        procs.append((n, name, f, c))

    tot = sum(buckets.values())
    print("DOS.O88's image is %d bytes; %d attribute (the rest is the "
          "package header)" % (size, tot))
    print("\nby source file:")
    for f, n in per_file.most_common():
        print("  %7d  %s" % (n, f))
    report("by side (docs/plans/KERN-DOS-PLAN.md 4.1.2):", buckets, tot, residual)
    if show_procs:
        print("\nevery proc, largest first:")
        for n, name, f, c in sorted(procs, reverse=True)[:80]:
            print("  %6d  %-8s %-28s %s" % (n, c, name, f))


def bss_split():
    sys.path.insert(0, os.path.join(ROOT, "tests"))
    import dosmap
    # **THE WHOLE BOX, AS ONE IMAGE** - `shipped=False`, which is the one
    # caller in the tree that wants it. `dosmap.package()`'s default is the
    # PARTED package $(SYSROOT) ships (SPEC.md 96.40.3), and that build has
    # already done this split: `-DDOS_EXTCORE` takes the core's image and bss
    # out, so `DOS_BSS_SIZE` reads 1,676 against the box's ~3,031 and the core
    # bucket comes out at 123.6% of it. A percentage over 100 is the tell.
    dm = dosmap.package(shipped=False)
    total = dm.get("DOS_BSS_SIZE")
    ent = sorted((v, k) for k, v in dm.items() if k.startswith("DOS_B_"))
    buckets, residual = collections.Counter(), []
    for i, (v, k) in enumerate(ent):
        nxt = ent[i + 1][0] if i + 1 < len(ent) else total
        n = max(0, nxt - v)
        short = k[len("DOS_B_"):]
        if short.endswith("RECT") or short in ("LN", "MBUF"):
            c = "window"
        else:
            c = classify(short, B_CORE, B_WINDOW, B_DROP)
        if c is None:
            residual.append((n, k))
            c = "unclassified"
        buckets[c] += n
    print("\nDOS_BSS_SIZE is %d in %d entries; the package's own bss is that "
          "plus CON_BSS %d, the windowed console's"
          % (total, len(ent), dm.get("CON_BSS", 0)))
    report("by side:", buckets, total, residual)


def mouse_split():
    """kernel/mouse.inc's three thirds, on kern_small (KERN-DOS-PLAN 6.1)."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    os.environ.setdefault("OS88_BUILD", "build/smallk")
    import os88sym
    S = os88sym.syms(defines=("KERN_SMALL",), check=False)
    SEC = os88sym.sections(defines=("KERN_SMALL",), check=False)
    rows = [(v, k, SEC.get(k, "?")) for k, v in S.items() if isinstance(v, int)]
    sp = spans(rows)
    own = owners(["kernel/mouse.inc"])
    roll = collections.Counter()
    for (name, sec), n in sp.items():
        if name in own and sec in (".text", ".cold"):
            roll[name] += n
    tot = sum(roll.values())
    cur = sum(v for k, v in roll.items() if k.startswith("cur_"))
    kbd = sum(v for k, v in roll.items()
              if k.startswith("kbm_") or k.startswith("kbd_"))
    print("\nkernel/mouse.inc on kern_small: %d bytes of .text/.cold over %d "
          "procs" % (tot, len(roll)))
    print("  cur_*       the POINTER   %5d  %5.1f%%  (6.1 lever 2)"
          % (cur, 100.0 * cur / max(1, tot)))
    print("  kbm_/kbd_*  the KEYBOARD  %5d  %5.1f%%  (the ROM's own int 16h "
          "serves kern_dos)" % (kbd, 100.0 * kbd / max(1, tot)))
    print("  the rest    the MOUSE     %5d  %5.1f%%"
          % (tot - cur - kbd, 100.0 * (tot - cur - kbd) / max(1, tot)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--procs", action="store_true",
                    help="every proc, largest first")
    ap.add_argument("--mouse", action="store_true",
                    help="kernel/mouse.inc's split instead")
    a = ap.parse_args()
    os.chdir(ROOT)
    if a.mouse:
        mouse_split()
        return 0
    image_split(a.procs)
    bss_split()
    return 0


if __name__ == "__main__":
    sys.exit(main())
