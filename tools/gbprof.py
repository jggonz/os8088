#!/usr/bin/env python3
"""What a Game Boy game asks of an emulator, per frame - on the HOST.

    python3 tools/gbprof.py GAME.GB                 # platformer input script
    python3 tools/gbprof.py GAME.GB --mode puzzle   # wandering d-pad instead
    python3 tools/gbprof.py GAME.GB --frames 7200 --shots build/gbprof/shot

The instrument behind docs/plans/REALDMG-PLAN.md and
docs/reports/GAMEBOY-WORKLOAD-2026-10-19.md. It answers the question every
speed figure in that plan hangs off - how much of each frame does the game's
CPU spend doing work an emulator cannot skip? - without an 8088 anywhere:
HALT and tight polling loops cost an emulator nothing if it skips to the next
event, so the BUSY share is the CPU load an emulator has to carry.

It fetches Peanut-GB (MIT, Mahyar Koshkouei) at a PINNED commit into
build/gbprof/, patches five hook calls into a copy of peanut_gb.h, builds
tools/gbprof/gbprof.c against it with the host C compiler and runs the ROM.
`make clean` takes build/gbprof/ with it; the next run fetches again.

NO ROM IS EVER COMMITTED OR SHIPPED. Commercial cartridges are the user's to
supply; the homebrew used in the report is built from its own source.

--shots writes PGM screenshots at a third, two thirds and the end of the run,
which is how to check the input script reached gameplay rather than measuring
a title screen for two minutes.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "build", "gbprof")
PEANUT_URL = "https://github.com/deltabeard/Peanut-GB"
PEANUT_REV = "d0bcca771c83a2638c93a9ae61f3d51f226dc905"   # 2026-09-21

# (anchor in peanut_gb.h, text inserted after it). Each anchor must occur
# exactly once; a moved upstream fails loudly here rather than measuring
# something else.
HOOKS = [
    ("uint8_t __gb_read(struct gb_s *gb, uint16_t addr)\n{",
     "\n\tGBPROF_READ(gb, addr);"),
    ("void __gb_write(struct gb_s *gb, uint_fast16_t addr, uint8_t val)\n{",
     "\n\tGBPROF_WRITE(gb, addr, val);"),
    ("\t/* Obtain opcode */\n",
     "\tuint16_t gbprof_pc0 = gb->cpu_reg.pc.reg; GBPROF_PRE(gb, gbprof_pc0);\n"),
    ("\t\tPGB_UNREACHABLE();\n\t}\n\n",
     "\tGBPROF_POST(gb, opcode, gbprof_pc0);\n"),
    ("\tdo\n\t{\n\t\t/* DIV register timing */\n",
     "\t\tGBPROF_TICK(gb, inst_cycles);\n"),
    # Peanut-GB charges an interrupt's dispatch NOTHING; the hardware takes
    # 5 M-cycles. gbprof leaves both no-ops, so its numbers are what they
    # were; tools/gbtrace.py charges the 5, so its clock is the hardware's.
    ("\t\t/* Push Program Counter */\n",
     "\t\tGBPROF_INT(gb);\n"),
    ("\tinst_cycles = op_cycles[opcode];\n",
     "\tGBPROF_CYC(gb, inst_cycles);\n"),
    # And HALT advances in chunks as long as an LCD mode, so it can wake
    # late. gbtrace steps it one M-cycle at a time; gbprof leaves it alone.
    ("\t\tinst_cycles = (uint_fast16_t)halt_cycles;\n",
     "\t\tGBPROF_HALTSTEP(gb, inst_cycles);\n"),
    ("\t\t\t\tinst_cycles = LCD_MODE3_LCD_DRAW_MIN_DURATION - gb->counter.lcd_count;\n\t\t}\n",
     "\t\tGBPROF_HALTSTEP(gb, inst_cycles);\n"),
    # STOP is two bytes, 10 00; Peanut-GB takes one and runs the 00 as a NOP.
    ("\tcase 0x10: /* STOP */\n",
     "\t\tGBPROF_STOP(gb);\n"),
]


def fetch():
    src = os.path.join(WORK, "peanut")
    if not os.path.isdir(os.path.join(src, ".git")):
        os.makedirs(WORK, exist_ok=True)
        subprocess.check_call(["git", "clone", "-q", PEANUT_URL, src])
    head = subprocess.check_output(["git", "-C", src, "rev-parse", "HEAD"],
                                   text=True).strip()
    if head != PEANUT_REV:
        subprocess.check_call(["git", "-C", src, "fetch", "-q", "origin"])
        subprocess.check_call(["git", "-C", src, "checkout", "-q", PEANUT_REV])
    return os.path.join(src, "peanut_gb.h")


def patch(header):
    text = open(header).read()
    for anchor, add in HOOKS:
        n = text.count(anchor)
        if n != 1:
            sys.exit("gbprof: anchor occurs %d times in peanut_gb.h, wanted 1:"
                     "\n%r" % (n, anchor))
        text = text.replace(anchor, anchor + add)
    out = os.path.join(WORK, "pgb_prof.h")
    open(out, "w").write(text)
    return out


def build():
    patch(fetch())
    exe = os.path.join(WORK, "gbprof")
    cc = os.environ.get("CC", "cc")
    subprocess.check_call([cc, "-O2", "-w", "-I", WORK, "-o", exe,
                           os.path.join(ROOT, "tools", "gbprof", "gbprof.c")])
    return exe


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--frames", type=int, default=7200,
                    help="frames to run (default 7200, two minutes)")
    ap.add_argument("--mode", choices=("platformer", "puzzle"),
                    default="platformer", help="the input script")
    ap.add_argument("--shots", metavar="PREFIX",
                    help="write PREFIX_NNNNN.pgm screenshots")
    a = ap.parse_args()
    exe = build()
    cmd = [exe, a.rom, str(a.frames), "0" if a.mode == "platformer" else "1"]
    if a.shots:
        cmd.append(a.shots)
    sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
