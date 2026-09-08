#!/usr/bin/env python3
"""The Drivers page's memory column still says what a driver actually costs.

    python3 tests/unit/t_drvmem.py

SPEC.md 31.6.2 puts a figure beside every driver on the Control Panel's
Drivers page - roughly the memory that driver holds while it is doing its job
- and the figure is a COMPILE-TIME CONSTANT, `drv_memk` in kernel/driver.inc.
That is the right shape for it (three of the five rows cannot answer the
question before they attach, and none of them can answer it about a machine
whose card is absent), and it is also exactly the shape that goes quietly
wrong.  Both of its terms drift on their own:

  * THE IMAGE.  A driver's image is a heap claim like any other (`DRVR_KB` is
    `ceil(file size / 1024)`), and it is the biggest single term on the row a
    256KB machine most needs to be right about - ETHER.DRV is 16KB of the 52
    its row claims.  A commit that adds a feature to a driver moves that
    number and touches nothing in the kernel.  There is no linker here
    (SPEC.md 1), so nothing notices.

  * THE CLAIMS.  `SBL_POOLKB`, `SK_RXMAX`, `HDD_LISTKB` and the rest live in
    the drivers' own sources, which the kernel cannot `%include` - they are
    assembled into separate flat binaries.  So the kernel's arithmetic is a
    COPY of theirs, which is t_mirror's failure mode with the two halves too
    far apart for t_mirror to see them.

Both halves are re-derived here from the primary source: the image from the
`.drv` the build just produced, each claim term from the constant in the
driver that takes it.  A wrong figure is not a crash - it is a number on
screen that stops being true, which is the kind of defect that survives for
releases because nobody can tell by looking.

WHAT IS NOT CHECKED, and cannot be: whether the SET of claims named below is
the complete set a driver takes.  A driver that grows a SIXTH claim and does
not declare it here passes.  That is a real hole and the mitigation is where
the hole is - `OSAPI_MEM_CLAIM` in a driver is rare enough that adding one is
a deliberate act, and SPEC.md 31.6.2 is what it is supposed to send you to.
This row narrows the drift to that one case rather than closing it.
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, eq, done                        # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88drv                                            # noqa: E402

BUILD = os.path.join(ROOT, "build")

# Where the constants live. Every file that spells out a term of any figure.
SOURCES = [
    "kernel/driver.inc",
    "drivers/os88drv.inc",
    "drivers/sound/sb.inc",
    "drivers/hdd/hddabi.inc",
    "drivers/ether/tcp.inc",
    "drivers/net/netpkg.inc",
    "drivers/ramdisk/rdabi.inc",
]

# row -> (the DRVM_ constant, the DRVM_IMG_ term, the built image). The ORDER
# is drv_tab's, and it is checked against drv_tab below rather than trusted.
ROWS = [
    ("Sound",      "DRVM_SND", "DRVM_IMG_SND", "sound.drv"),
    ("Hard Drive", "DRVM_HDD", "DRVM_IMG_HDD", "hdd.drv"),
    ("Ethernet",   "DRVM_ETH", "DRVM_IMG_ETH", "ether.drv"),
    ("Ram Disk",   "DRVM_RAM", "DRVM_IMG_RAM", "ramdisk.drv"),
    ("os88net",    "DRVM_NET", "DRVM_IMG_NET", "net.drv"),
    # SPEC.md 9.11's absolute pointer. THE ONE DRVC_OVL ROW IN drv_tab, so
    # unlike the five above it publishes nothing - but it is a row, it has a
    # SYSTEM.CFG bit and the Drivers page prices it, so it is in drv_memk and
    # therefore here
    ("Absolute mouse", "DRVM_VMM", "DRVM_IMG_VMM", "vmmouse.drv"),
]

EQU = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\s+equ\s+(.+?)\s*(?:;.*)?$", re.M)


def symbols():
    """Every `NAME equ EXPR` in SOURCES, resolved to an int where it is one.

    NASM's expressions here are arithmetic over other equs, so they are
    evaluated rather than pattern-matched: RD_TABMAXKB is `RD_MAXEXT * 2 /
    1024` and reading it as a literal would read it as nothing.
    """
    raw = {}
    for rel in SOURCES:
        with open(os.path.join(ROOT, rel), errors="replace") as f:
            for name, expr in EQU.findall(f.read()):
                raw.setdefault(name, expr)          # first spelling wins
    out, pending = {}, True
    while pending:
        pending = False
        for name, expr in raw.items():
            if name in out:
                continue
            v = evaluate(expr, out)
            if v is not None:
                out[name] = v
                pending = True
    return out


def evaluate(expr, known):
    """One NASM expression as an int, or None while a name is still unknown."""
    e = re.sub(r"\b([0-9A-Fa-f]+)h\b", lambda m: "0x" + m.group(1), expr)
    e = e.replace("/", "//")                        # NASM's / is integer
    names = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\b", e))
    for n in names:
        if n.lower().startswith("0x"):
            continue
        if n not in known:
            return None
        e = re.sub(r"\b%s\b" % re.escape(n), str(known[n]), e)
    try:
        v = eval(e, {"__builtins__": {}}, {})       # arithmetic over ints only
    except Exception:
        return None
    return v if isinstance(v, int) else None


def kb(n):
    """Bytes -> whole KB, the way drv_load rounds a driver image up."""
    return int(math.ceil(n / 1024.0))


def main():
    s = symbols()
    for n in ("DRVM_PLUS", "DRV_MAX"):
        if n not in s:
            check(False, "%s is defined in the sources this reads" % n,
                  "the figure's own machinery moved - point SOURCES at its new "
                  "home rather than letting this row pass on an empty table")
            done("t_drvmem")

    plus = s["DRVM_PLUS"]

    # --- 1. drv_memk is drv_tab's order, row for row ------------------------
    # nasm's own %if checks the LENGTH. Nothing checks that row 2's figure is
    # row 2's driver, and a table read by index is exactly the kind that gets
    # re-ordered correctly in one place and not the other.
    src = open(os.path.join(ROOT, "kernel/driver.inc"), errors="replace").read()
    tab = re.findall(r"^\s*dw\s+drv_f_(\w+),\s*drv_t_(\w+)", src, re.M)
    memk = re.search(r"^drv_memk:(.*?)^%if \(\$ - drv_memk\)", src,
                     re.M | re.S)          # ...and NOT the first ^%if:
                                           # the %ifdef KERN_BIG is INSIDE
                                           # the table and stopping there
                                           # reads three rows of five
    order = re.findall(r"^\s*dw\s+(DRVM_\w+)", memk.group(1), re.M) if memk else []
    eq(len(order), len(tab), "drv_memk has one word per drv_tab row",
       "nasm's %if catches a SHORT table on the build it is assembling; this "
       "catches one that lost a row under an %ifdef the build did not take")
    eq(order, [r[1] for r in ROWS][:len(order)],
       "drv_memk's rows are drv_tab's, in order",
       "the table is read by INDEX (cp_drv_mem1 shifts the row number), so a "
       "figure in the wrong slot prices the wrong driver and nothing refuses it")

    # --- 2. every image term is the image the build just made ---------------
    seen = 0
    for title, total, img, drv in ROWS:
        p = os.path.join(BUILD, drv)
        if not os.path.exists(p):
            continue                                # not built: t_pkg's job
        seen += 1
        # THE RESIDENT SIZE, WHICH IS NO LONGER THE FILE SIZE. A driver's
        # trailing zeros are stripped by os88drv.py and re-made by drv_bss, so
        # what the loader claims is file + bss (byte +31, in paragraphs) -
        # docs/plans/O88-COMPRESSION-PLAN.md 12.6. ether.drv is 13,602 bytes on the
        # floppy and 17,666 in memory, and the column is the second number.
        # ...AND THE FILE IS NO LONGER THE IMAGE EITHER (SPEC.md 20.13.2).
        # Every shipped driver is LZ4 on the disk now, and every term below -
        # the bss arithmetic, the KB column, DRV_MAX_KB - is about what
        # drv_load CLAIMS, which is the unpacked size. Reading the file here
        # under-reports all three at once, which is a column that is wrong in
        # the safe direction and therefore the kind nobody notices.
        blob = os88drv.image_unwrap(open(p, "rb").read())
        # ...and the stripping is LOSSLESS. os88drv.py takes whole paragraphs
        # of TRAILING ZEROS off the file and drv_bss puts them back at load, so
        # the two together must reproduce the assembler's own output byte for
        # byte. Rounding that count UP instead of down is what this catches:
        # the first version did, and took seven bytes of real code off
        # sound.drv with nine trailing zeros.
        src = os.path.join(ROOT, "build", drv.replace(".drv", ".bin"))
        if os.path.exists(src):
            orig = open(src, "rb").read()
            rebuilt = blob[:8] + orig[8:10] + blob[10:31] + b"\0" \
                      + blob[32:] + b"\0" * (blob[31] * 16)
            eq(rebuilt, orig, "%s: stripped + bss is the assembled image"
               % title, "os88drv.py takes only whole paragraphs of trailing "
               "zeros; anything else and drv_bss restores the wrong bytes")
        want = kb(len(blob) + blob[31] * 16)
        eq(s.get(img), want, "%s: %s is ceil((%s + its bss) / 1024)"
           % (title, img, drv),
           "a driver's image IS a heap claim (drv_load rounds the file up to KB "
           "and claims that), so the column is wrong by the difference the "
           "moment the driver grows - and nothing else in the tree compares "
           "these two numbers")
        check(want <= s.get("DRV_MAX_KB", 40),
              "%s: %s fits DRV_MAX_KB" % (title, drv),
              "drv_load refuses an image over DRV_MAX_KB, so this driver no "
              "longer loads at all - the column is the least of it",
              got=want, want="<= %s" % s.get("DRV_MAX_KB"))
    check(seen > 0, "the drivers are built",
          "run `make` first - this row reads build/*.drv, and with none of them "
          "there it would pass by checking nothing")

    # --- 3. ...and every claim term is the driver's own constant ------------
    # Each expression is re-derived from the source that TAKES the claim, so a
    # pool that doubles or a ring that grows fails here rather than on screen.
    ceil_kb = lambda n: (n + 1023) // 1024
    want = {
        "DRVM_SND": s["DRVM_IMG_SND"]                       # its image...
                    + s["SBL_DMASZ"] // 1024                # sbl_dma_map
                    + s["SBL_POOLKB"],                      # sbl_pool_get, top rung
        "DRVM_HDD": s["DRVM_IMG_HDD"]
                    + s["HD_MAXVOL"] * s["HDD_LISTKB"],     # one per mounted volume
        # **THE POOL, NOT ONE RING A SLOT** (SPEC.md 72.21). This was
        # NET_SOCKS * (rx + tx), which is exactly the arithmetic the ring pool
        # exists to break: a slot is 128 bytes of bss and a ring pair is 9,216
        # of the claim, and multiplying them together is what made two more
        # handles cost 18KB. NET_SOCKS must NOT appear here - if it comes back,
        # the weld is back.
        "DRVM_ETH": s["DRVM_IMG_ETH"]
                    + ceil_kb(s["SK_RXMAX"] + s["SK_TXMAX"]          # the bulk pair
                              + s["SK_NLEAN"] * (s["SK_LEANRX"]      # ...and the lean
                                                 + s["SK_LEANTX"])), # ones beside it
        "DRVM_RAM": (s["DRVM_IMG_RAM"]
                     + s["RD_TABMAXKB"]                     # the chain table
                     + s["RD_EXTMAXKB"]) | plus,            # ...and the bounce
        "DRVM_NET": s["DRVM_IMG_NET"],                      # no heap claim at all
        # ...and nor does the absolute mouse: it hooks no vector, owns no port
        # and keeps no buffer, so the image IS the whole footprint
        "DRVM_VMM": s["DRVM_IMG_VMM"],
    }
    for title, total, _img, _drv in ROWS:
        eq(s.get(total), want[total],
           "%s: %s is its image plus the claims it works with" % (title, total),
           "the kernel cannot %include a driver's source - they are separate "
           "flat binaries - so this arithmetic is a COPY of the driver's. "
           "SPEC.md 31.6.2 says which claims count and which do not")

    # --- 4. the flag is a flag ----------------------------------------------
    for title, total, _img, _drv in ROWS:
        v = s.get(total, 0)
        check(0 < (v & ~plus) < 10000,
              "%s: %s is a plausible number of KB" % (title, total),
              "the figure is drawn as decimal digits with the top bit masked "
              "off (cp_drv_memstr); a five-digit one runs the column into the "
              "scroll arrows and a zero one draws nothing at all",
              got=v & ~plus, want="1..9999")
    check(s["DRVM_RAM"] & plus,
          "the RAM disk's figure carries DRVM_PLUS",
          "its arena is [rd_kb] - the USER's number, not the driver's - so its "
          "row prices the driver and says '+' for the store (SPEC.md 31.6.2)")
    for title, total, _img, _drv in ROWS:
        if total == "DRVM_RAM":
            continue
        check(not (s.get(total, 0) & plus),
              "%s: no '+' - its ceiling is a real one" % title,
              "'+' means the user sizes the rest on another page. Every other "
              "row's largest claim is bounded by the driver or the machine, so "
              "a '+' here would be telling the user the number is open-ended "
              "when it is not")

    print("t_drvmem: %d rows, %d images re-measured, %d constants resolved"
          % (len(ROWS), seen, len(s)))
    done("t_drvmem")


if __name__ == "__main__":
    main()
