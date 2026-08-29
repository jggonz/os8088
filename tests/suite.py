"""The test registry - what runs, in which tier, and why.

`tools/os88test.py` reads this and nothing else.  Adding a test is adding a
row here; a test that is not in this file does not run in any tier, which is
the state all ninety of them were in before it existed.

THE THREE TIERS.

  fast   Budget 30s. Host-side only - it reads what `make` just built and
         checks the invariants that break SILENTLY. Hangs off the default
         build, so it cannot be skipped.

  full   Budget 10 minutes. THE PRE-MERGE GATE. fast, plus the build matrix
         `all` never builds, plus a CURATED set of emulator tests.

  soak   No budget. Everything else - the rest of `tests/`, which is a great
         deal and is where the deep single-subject gates live.

WHY `full` IS CURATED AND NOT "ALL OF THEM", which is the thing to understand
before adding a row to it.  Measured on a cycle-accurate 5150 in a container:
a MartyPC boot to a settled desktop is **7.8 seconds**, and the emulator tests
in `tests/` run **40-75 seconds each** because each one boots its own machine
and then drives a session through it.  They also cannot run in parallel -
every one of them drives the debug server on 127.0.0.1:9001, one port, one
connection, and a second client does not error, it HANGS (docs/MARTYPC-DEBUG.md).

So 10 minutes is about **eight** emulator tests, not fifty.  That is not a
limitation to be engineered away - it is what the machine costs - and the
honest response is to say which eight and put the rest in `soak` where they
are still one command away (`os88test.py soak -k disp*`).  The runner FAILS
the tier when it overruns, so this stays true as rows are added rather than
drifting until the suite is too slow to run.

WHAT EARNS A `full` ROW.  Breadth per second, and independence.  `bootsmoke`
is the model: eight seconds, and it exercises the boot sector, FAT12, the
`int 13h` splitter, adapter detection, the heap ladder, `drv_boot` and the
first paint - so it fails for almost any serious regression, wherever it was.
A row that can only fail for one narrowly-scoped reason belongs in `soak`,
next to the change that would break it.
"""


class Row:
    """One registered test."""

    __slots__ = ("name", "tier", "cmd", "secs", "needs", "serial", "why", "timeout")

    def __init__(self, name, tier, cmd, secs, why, needs=(), serial=False,
                 timeout=None):
        self.name, self.tier, self.cmd = name, tier, cmd
        self.secs, self.why = secs, why
        self.needs = tuple(needs)
        self.serial = serial
        # A generous default: the point of the per-row timeout is to stop a
        # hung emulator eating the tier, not to police a slow machine.
        self.timeout = timeout or max(60, int(secs * 4) + 30)


def py(*a):
    return ["python3"] + list(a)


# --------------------------------------------------------------------------
# fast - host-side, no emulator, no build. Runs on every `make`.
# --------------------------------------------------------------------------
FAST = [
    Row("api-abi", "fast", py("tests/unit/t_api_abi.py"), 2.0,
        "the API table decoded from kernel.bin and compared with the SDK - the "
        "silent merge collision CLAUDE.md asks to be checked by hand"),
    Row("mirror", "fast", py("tests/unit/t_mirror.py"), 1.6,
        "a constant written down in two files must agree in both; there is no "
        "linker here to notice"),
    Row("ktags", "fast", py("tests/unit/t_ktags.py"), 0.2,
        "every owner tag the kernel ships has a TYPE name on the Task "
        "Manager's heap page - SPEC.md 28.4's hex fallback is for a tag this "
        "build has never seen, and three shipped ones had been sitting in it"),
    Row("kernbudget", "fast", py("tests/unit/t_kernbudget.py"), 0.2,
        "docs/KERNEL-MEMORY.md's blessed baseline carries THIS kernel's "
        "KERN_BUDGET - it went two moves behind because tools/kernsize.py "
        "compared spare and could not see a budget move at all"),
    Row("drvmem", "fast", py("tests/unit/t_drvmem.py"), 0.2,
        "the Drivers page's memory column (SPEC.md 31.6.2) re-derived: every "
        "image term against the .drv this build made, every claim term against "
        "the constant in the driver that takes it"),
    Row("image", "fast", py("tests/unit/t_image.py"), 0.2,
        "the shipped floppies read by an independent FAT12 walker: contiguity, "
        "the standard BPB, SPEC.md 19.6's attributes"),
    Row("pkg", "fast", py("tests/unit/t_pkg.py"), 0.5,
        "package/driver/module headers, and every file on every image proved "
        "identical to the artifact it was built from"),
    Row("diskverify", "fast", py("tests/unit/t_diskverify.py"), 0.6,
        "the tree's own fsck, pointed at the seven images `make` ships and "
        "never ran on"),
    Row("canary", "fast", py("tests/unit/t_canary.py"), 0.2,
        "SPEC.md 18.93.1's canary offset re-derived from every shipped image's "
        "own BPB: it has to name a sector a transfer run reads AFTER the head "
        "boundary, because the half before it loads correctly on exactly the "
        "machine the canary is for - which is how the first one shipped wrong"),
    Row("registry", "fast", py("tests/unit/t_registry.py"), 0.2,
        "every test in tests/ is registered in a tier or says why not - the row "
        "that stops this suite going back to a directory nobody can enumerate"),
    Row("asmrules", "fast", py("tests/unit/t_asmrules.py"), 1.0,
        "unreachable code after an unconditional jump, a prologue restored in "
        "the WRONG ORDER (SPEC.md 1's register discipline: balanced depth, "
        "swapped pair, nothing faults), and a `cpu 8086` reachable from every "
        "root"),
    Row("wakedrain", "fast", py("tests/unit/t_wakedrain.py"), 0.3,
        "every event-queue drain gives a package's wake back - one that eats "
        "it deafens the window for the rest of its life (SPEC.md 74.1.1)"),
    Row("wab", "fast", py("tests/unit/t_wab.py"), 0.3,
        "the demo bundles `all` just packed, read back by an independent "
        "second reader of the .WAB format - weavesim and t_wab are two "
        "implementations written from WEAVE-SPEC that can disagree, and "
        "until the 8086 runtime lands this row is the disagreement's only "
        "audience"),
    Row("textrules", "fast", py("tests/unit/t_textrules.py"), 0.4,
        "SPEC.md 6.6's ratchet: transparent text (font_char/font_str) draws every "
        "pixel twice and flashes on the target machine, so every call site is "
        "registered in tests/textsites.txt with a reason and the count can only "
        "go down"),
    Row("checkdocs", "fast", py("tools/checkdocs.py"), 1.0,
        "stale SPEC.md citations and slot numbers in prose (already in `make`; "
        "here too so the suite is a complete statement)"),
    Row("docindex", "fast", py("tools/os88index.py", "--check"), 0.5,
        "docs/INDEX.md still matches the tree - an index that has drifted is "
        "worse than none, because it is consulted and believed"),
    Row("checkreadme", "fast", py("tools/checkreadme.py", "readme.txt"), 0.3,
        "README.TXT's width and size rules - Note Pad refuses a file one byte "
        "too long and shows nothing at all"),
    Row("ovlchk", "fast", py("tools/os88ovlchk.py"), 1.0,
        "no near call crosses a section boundary - it assembles cleanly and "
        "runs wrong"),
    Row("stknosave", "fast",
        py("tools/stkdepth.py", "drivers/ether/ether.asm", "--check"), 1.5,
        "every `; STKDEPTH-NOSAVE:` in ETHER.DRV still holds: the routines "
        "that stopped saving a register to fit a 256-byte task slice (SPEC.md "
        "72.16.4) still get it back from every callee. Without this the trade "
        "is a landmine for whoever edits the TCP stack next"),
    Row("stkbalance", "fast",
        py("tools/stkbalance.py", "apps/sheet/sheet.asm", "apps/chart/chart.asm",
           "apps/os88chart.inc", "apps/os88fp.inc", "apps/os88text.inc",
           "apps/os88line.inc"), 1.5,
        "every `ret` in SHEET, CHART and the includes they share is reached at "
        "the depth it started at. `ch_legend` pushed SI and never popped it, so "
        "its `ret` jumped to the saved register: a black canvas and a wedged "
        "app, with no crash and no message (SPEC.md 82.7.3). The walk is "
        "path-aware because a naive push-vs-pop count flags one routine in ten "
        "and would just be ignored. SCOPED to these files on purpose - the "
        "kernel's ISR tails push in one global label and pop in another, which "
        "this cannot follow, so pointing it there would report noise. Two "
        "stated gaps, both counted in the tool's own summary line: a routine "
        "whose every exit is a tail jmp is not walked, and loop back-edge "
        "conflicts are suppressed"),
]

# --------------------------------------------------------------------------
# full - the pre-merge gate. Everything above, plus these.
# --------------------------------------------------------------------------
FULL = [
    Row("buildmatrix", "full", py("tests/unit/t_buildmatrix.py"), 45.0,
        "the knob kernels and kern_small - every configuration `all` "
        "does not build, and so the only thing that keeps them assembling"),
    Row("ctoolchain", "full", py("tests/unit/t_ctoolchain.py"), 8.0,
        "the C toolchain still produces a package - the OTHER thing `all` "
        "does not build, and the one that had a `cc` capability with no row "
        "behind it while no C package assembled for two releases",
        needs=("cc",), serial=True),
    Row("bootsmoke", "full", py("tests/bootsmoke.py"), 20.0,
        "does it still reach a desktop on both 1bpp adapters - the widest "
        "reach per second of any test here",
        needs=("marty",), serial=True),
    Row("vgadrop", "soak", py("tests/vgadrop.py"), 150.0,
        "SPEC.md 39.22: the heap floor starts UNDER .vgabuf on a machine with "
        "no VGA and AT KERN_END on one that has it. Reads [mem_base] as a "
        "WORD on three adapters rather than a KB total, because a KB rounds "
        "and rounding is where an off-by-a-rung hides - and it is the only "
        "thing that would notice the gate being on [vid_mono] instead of "
        "[vid_avail], which reads identically until somebody switches a VGA "
        "machine to mono",
        needs=("marty",), serial=True),
]

# --------------------------------------------------------------------------
# soak - registered, discoverable, not in anybody's budget.
#
# Each row names the subsystem it is about, so `os88test.py soak -k <glob>`
# is how you run the ones your change could have broken. These are the deep
# single-subject gates; several are worth reading before touching their area.
# --------------------------------------------------------------------------
SOAK = [
    Row("assocglyph", "soak", py("tests/assocglyph.py"), 62.3,
        "A DECLARED extension's icon is right from a COLD mount (SPEC.md"
        "54.7.3).",
        needs=("marty",), serial=True),
    Row("assocopen", "soak", py("tests/assocopen.py"), 60.0,
        "SPEC.md 22.13.2: opening a DOCUMENT draws no pixel of the Disk "
        "window. The instrument is a breakpoint on fm_repaint, and the "
        "FOLDER open beside it is the control that says the breakpoint "
        "fires at all.",
        needs=("marty",), serial=True),
    Row("cpup", "soak", py("tests/cpup.py"), 41.3,
        "SPEC.md 13.8.3: the Control Panel acts on the RELEASE, not the"
        "press.",
        needs=("marty",), serial=True),
    Row("dtfield", "soak", py("tests/dtfield.py"), 50.0,
        "SPEC.md 37.93: the Date/Time field editor still edits now that it "
        "runs from inside CTRL.DRV. The day clamp is the load-bearing leg - "
        "cw_clk_mlen is the only call clk_fld_adj makes out of the image, so "
        "a bad thunk shows up there and nowhere else on the page.",
        needs=("marty",), serial=True),
    Row("dtwrite", "soak", py("tests/dtwrite.py"), 45.0,
        "SPEC.md 37.94: the hardware clock is written by the Control Panel's "
        "CLOSE and no longer drained off the system tick. [clk_dirty] "
        "SURVIVING ~54 ticks with the panel open is the leg that matters - "
        "on the old kernel ui_task spent it inside 55 ms. No rung is reached "
        "here: a 5150 has no RTC and MartyPC models no clock card, so the "
        "writers themselves are a QEMU session (see the docstring).",
        needs=("marty",), serial=True),
    Row("saver", "soak", py("tests/saver.py"), 95.0,
        "the animated screen saver end to end (SPEC.md 79): every mode draws, "
        "the overlay is loaded and freed, the wake puts the whole desktop back "
        "including the bar and the dock, no block is left in the menu bar, and "
        "all three fallbacks reach the blanker with the framebuffer untouched",
        needs=("marty",), serial=True),
    Row("deskbench", "soak", py("tests/deskbench.py"), 330.0,
        "THE STANDARD BUSY DESKTOP, priced: what a full-screen redraw, a "
        "window move and a raise cost with four windows open (PERFORMANCE.md "
        "Part 3). A measurement, not a gate - it asserts its own SCENE and "
        "prints numbers. `--all` runs one per adapter.",
        needs=("marty",), serial=True),
    Row("arkpuwipe", "soak", py("tests/arkpuwipe.py"), 300.0,
        "Does a capsule the blit REFUSED leave a streak behind it? (SPEC.md "
        "44.10.6.2). VGA on purpose - on CGA ARK_PUFALL floors to 1 and the "
        "one vacated row is the capsule's BLACK top edge on a BLACK playfield, "
        "so the broken build scores zero. `--small --img build/small360.img` "
        "is trigger A, and wants `make small` first.",
        needs=("marty",), serial=True),
    Row("cycweb", "soak", py("tests/cycweb.py"), 51.4,
        "Does the claw eat the web it slides over? (SPEC.md 67.5.3.1)",
        needs=("marty",), serial=True),
    Row("cycfire", "soak", py("tests/cycfire.py"), 180.0,
        "Does holding the mouse button repeat the gun, and does a press on "
        "somebody else's window leave it alone? (SPEC.md 67.11.3)",
        needs=("marty",), serial=True),
    Row("bootstatus", "soak", py("tests/bootstatus.py"), 240.0,
        "Does the boot say WHAT it is doing, not only how far? (SPEC.md 15.6)"
        " Builds a disk whose SYSTEM.CFG wants two drivers, then reads the "
        "composed line out of the overlay AND hashes the pixel band under the"
        " bar, on both 1bpp adapters",
        needs=("marty", "nasm"), serial=True),
    Row("blobsum", "soak", py("tests/blobsum.py"), 90.0,
        "Does a SHORT READ of stage 2's blob halt instead of executing what "
        "landed? (SPEC.md 2.9.7) Blanks one sector in the middle of it - the "
        "failure that is not a disk error, because stage 2 and the loading "
        "screen are in the sectors that DID arrive",
        needs=("marty",), serial=True),
    Row("postboot", "soak", py("tests/postboot.py"), 180.0,
        "Does the machine survive its FIRST DISK ACCESS AFTER THE DESKTOP? "
        "(SPEC.md 2.9.5.1) Every other boot row in this file stops at the "
        "first frame, which is how a kernel whose next int 13h jumped into "
        "cold_entry passed all of them",
        needs=("marty",), serial=True),
    Row("cylrun", "soak", py("tests/cylrun.py"), 150.0,
        "Did the kernel load actually CROSS A HEAD? (SPEC.md 18.93.3) "
        "boot_cylrun at 0060:0004 is written on the one path where the "
        "cylinder bound, the 8088 gate and the canary all held - and 18.93's "
        "reload is what makes losing all three look like a normal boot",
        needs=("marty",), serial=True),
    Row("splashbar", "soak", py("tests/splashbar.py"), 150.0,
        "Does the progress bar ADVANCE during the kernel load? (SPEC.md 15, "
        "15.3.1) The counter AND the lit width of the trough, sampled a frame "
        "at a time - a bar that parked at 44% for the whole load passed every "
        "other row in this file and was found by somebody watching it",
        needs=("marty",), serial=True),
    Row("vgadirty", "soak", py("tests/vgadirty.py"), 120.0,
        "Does vid_setmode leave the VGA framebuffer black whatever the ROM "
        "did? (SPEC.md 39.23) Builds a VGADIRTY=1 kernel, which fills A0000 "
        "in the one window a machine cannot - after the ROM's mode set and "
        "before ours - and asserts the loading screen comes up on black",
        needs=("qemu", "nasm"), serial=True, timeout=300),
    Row("heapmap", "soak", py("tests/heapmap.py"), 120.0,
        "What does the claim heap look like when the boot is over? (SPEC.md "
        "50, 66) Every driver attached at once on a machine WITH memory above "
        "1MB, sampled from instruction zero: the order claims are taken in, "
        "and MC_RLOC for each - which is the machine-readable answer to "
        "'can this be compacted'",
        needs=("qemu", "nasm"), serial=True, timeout=300),
    Row("dockmark", "soak", py("tests/dockmark.py"), 60.0,
        "Does the dock strip mark windows it did not draw under? (SPEC.md"
        "30.3.3)",
        needs=("marty",), serial=True),
    Row("dualcheck", "soak", py("tests/dualcheck.py"), 60.0,
        "Can this MartyPC drive TWO video cards at once?"
        "(docs/DUAL-DISPLAY-PLAN.md 9)",
        needs=("marty",), serial=True),
    Row("gfxlk", "soak", py("tests/gfxlk.py"), 150.0,
        "Does ANYTHING draw with the gfx lock free - which is the one state "
        "the mouse ISR draws in? (SPEC.md 7/12.8.4, docs/FIELD-NOTES.md 34) "
        "Rebuilds the tree, because the counters are a knob kernel",
        needs=("marty", "nasm"), serial=True),
    Row("heapcheck", "soak", py("tests/heapcheck.py"), 60.0,
        "Drive tests/heapfrag and read its verdict out of the guest (SPEC.md"
        "66.8).",
        needs=("marty",), serial=True),
    Row("xmcheck", "soak", py("tests/xmcheck.py"), 90.0,
        "The extended-memory TEARDOWN gate (SPEC.md 41.5, 29.4). QEMU and "
        "not MartyPC, and the row said `marty` for a year: the machine has "
        "to HAVE memory above 1MB and the target machine never can (SPEC.md "
        "41.9 rule 1), which is one of docs/TESTING.md's five legitimate "
        "uses of QEMU. It also needs nasm for the OVERLAY's map - xm_tab is "
        "in XMEM.DRV now (SPEC.md 41.12), not in the kernel.",
        needs=("qemu", "nasm"), serial=True, timeout=600),
    Row("calcflick", "soak", py("tests/calcflick.py"), 60.0,
        "Does the Calculator FLASH? (PERFORMANCE.md Part 3.1, SPEC.md 65.4)",
        needs=("marty",), serial=True),
    Row("ftpdflick", "soak", py("tests/ftpdflick.py"), 90.0,
        "What does clicking into an FTPD Setup field cost, and is it still "
        "two cells rather than the page? (SPEC.md 77.45)",
        needs=("marty",), serial=True),
    Row("ftpdfocus", "soak", py("tests/ftpdfocus.py"), 90.0,
        "Does FTPD's Setup page keep a caret it cannot type into? "
        "(SPEC.md 77.45.4)",
        needs=("marty",), serial=True),
    Row("dispapp", "soak", py("tests/dispapp.py"), 60.0,
        "Does a PACKAGE stay on its own display? (SPEC.md 39.2.1)",
        needs=("marty",), serial=True),
    Row("dispapps", "soak", py("tests/dispapps.py"), 60.0,
        "Do the apps that lay out ONCE re-derive when the adapter changes?",
        needs=("marty",), serial=True),
    Row("dispband", "soak", py("tests/dispband.py"), 54.1,
        "Can a window use the SECOND display's top rows? (SPEC.md 39.16.2)",
        needs=("marty",), serial=True),
    Row("dispblit", "soak", py("tests/dispblit.py"), 60.0,
        "Does a BLIT reach the second display? (SPEC.md 39.14.7)",
        needs=("marty",), serial=True),
    Row("dispbrow", "soak", py("tests/dispbrow.py"), 60.0,
        "The field's browser report: a drag that does not move it, and a"
        "width cut on a card wide enough to hold it",
        needs=("marty",), serial=True),
    Row("dispcalc", "soak", py("tests/dispcalc.py"), 60.0,
        "Does the Calculator add up, fold cleanly and redraw nothing spare?",
        needs=("marty",), serial=True),
    Row("dispcalcx", "soak", py("tests/dispcalcx.py"), 60.0,
        "Does the Calculator re-fold cleanly when its box moves under it?",
        needs=("marty",), serial=True),
    Row("dispcheck", "soak", py("tests/dispcheck.py"), 60.0,
        "Did os8088 bring the SECOND card up, can it DRAW on it, and does the",
        needs=("marty",), serial=True),
    Row("dispclose", "soak", py("tests/dispclose.py"), 150.0,
        "SPEC.md 75: closing ASKS - W_ONCLOSE, the deferred OSAPI_WM_CLOSE, "
        "and os88ui_ask's alert, driven through every branch and finished by "
        "reading the saved file off the floppy with os88flush",
        needs=("marty",), serial=True, timeout=900),
    Row("dispclose-small", "soak", py("tests/dispclose.py", "--small"), 150.0,
        "...and the same suite on kern_small, which since SPEC.md 75.3.2 has "
        "the identical behaviour rather than a fallback. It needs `make "
        "small` first, and it is the ONE gate here that drives that build",
        needs=("marty",), serial=True, timeout=900),
    Row("dispcold", "soak", py("tests/dispcold.py"), 60.0,
        "WHO DRAWS INTO .cold? (docs/DUAL-DISPLAY-VGA.md 8(11))",
        needs=("marty",), serial=True),
    Row("dispcorner", "soak", py("tests/dispcorner.py"), 60.0,
        "REPORTED ARTIFACTS, LOCALISED (a corner pixel, and two drags across"
        "a seam)",
        needs=("marty",), serial=True),
    Row("dispcp", "soak", py("tests/dispcp.py"), 60.0,
        "Driving the Control Panel's Display page from a scripted session.",
        needs=("marty",), serial=True),
    Row("dispdepth", "soak", py("tests/dispdepth.py"), 60.0,
        "Does a window dragged BACK from a different-depth display arrive"
        "intact?",
        needs=("marty",), serial=True),
    Row("dispdrag", "soak", py("tests/dispdrag.py"), 60.0,
        "Does a window DRAGGED across the seam arrive on both displays?",
        needs=("marty",), serial=True),
    Row("dispfit", "soak", py("tests/dispfit.py"), 60.0,
        "Is changing adapter and changing back the IDENTITY on every window"
        "rect?",
        needs=("marty",), serial=True),
    Row("dispprefer", "soak", py("tests/dispprefer.py"), 60.0,
        "Does a package's PER-ADAPTER preference and floor survive a drag"
        "across the seam, and does a USER outrank it? (SPEC.md 11.100)",
        needs=("marty",), serial=True),
    Row("disptitle", "soak", py("tests/disptitle.py"), 150.0,
        "Does a title bar STRADDLING the seam have one polarity? (SPEC.md"
        "5.4.2.4) - it builds `make BAND=1` itself, the composer being a knob"
        "again since SPEC.md 5.9.6, and puts the default kernel back",
        needs=("marty",), serial=True),
    Row("dispthm", "soak", py("tests/dispthm.py"), 60.0,
        "Does SPEC.md 76's theme meet the extended desktop honestly? Color is"
        "a fact about the PRIMARY and a window can be on the other card",
        needs=("marty",), serial=True),
    Row("dispsize", "soak", py("tests/dispsize.py"), 60.0,
        "What size is a window given when it lands on the other card?"
        "(SPEC.md 11.100.3/11.100.4)",
        needs=("marty",), serial=True),
    Row("dispfrac", "soak", py("tests/dispfrac.py"), 60.0,
        "Does apps/fractal's restore cache survive an adapter change?"
        "(SPEC.md 40.1)",
        needs=("marty",), serial=True),
    Row("dispfreeze", "soak", py("tests/dispfreeze.py"), 60.0,
        "The field's freeze: a straddling window over a Disk window, then a"
        "click",
        needs=("marty",), serial=True),
    Row("dispfsx", "soak", py("tests/dispfsx.py"), 60.0,
        "WHICH MONITOR DOES A FULLSCREEN BRACKET LAND ON? (SPEC.md 53.7.1)",
        needs=("marty",), serial=True),
    Row("dispherc1", "soak", py("tests/dispherc1.py"), 60.0,
        "HERCULES PRIMARY, VGA SECOND (SPEC.md 39.19.2's other arrangement)",
        needs=("marty",), serial=True),
    Row("dispmcfs", "soak", py("tests/dispmcfs.py"), 60.0,
        "SPEC.md 11.2 fullscreen with the window's CENTRE on the second"
        "display",
        needs=("marty",), serial=True),
    Row("wireflick", "soak", py("tests/wireflick.py"), 120.0,
        "SPEC.md 78.5's three draw orders, as ink on the glass per displayed"
        "frame - the flicker measured rather than argued about",
        needs=("marty",), serial=True),
    Row("wirefps", "soak", py("tests/wirefps.py"), 90.0,
        "What SPEC.md 5.6.4.1 is worth to a program that draws lines - apps/wire"
        "reading its own frame rate, with the dispatch poked out and back",
        needs=("marty",), serial=True),
    Row("paintrate", "soak", py("tests/paintrate.py"), 120.0,
        "SPEC.md 42.8.1: is Paint's brush stroke still sampled at the TICK? The"
        "facets in a hand-drawn curve were one 55ms sleep each",
        needs=("marty",), serial=True),
    Row("uilat", "soak", py("tests/uilat.py"), 120.0,
        "SPEC.md 7.3: how long a click waits while a worker draws, bracketed"
        "by two memory breakpoints because the mouse harness has a half-second"
        "floor and cannot see it (7.3.1)",
        needs=("marty",), serial=True),
    Row("evqfull", "soak", py("tests/evqfull.py"), 60.0,
        "SPEC.md 10.1: a full event ring discards its OLDEST input, and never"
        "a coalesced WAKE - asked of evq_push directly, with the CPU parked",
        needs=("marty",), serial=True),
    Row("linefast", "soak", py("tests/linefast.py"), 60.0,
        "Does SPEC.md 5.6.4.1's fast walk lay 5.6.4's pixels? Both inks, all"
        "eight octants, clipped and not - against the same kernel with the"
        "dispatch poked out",
        needs=("marty",), serial=True),
    Row("dispmine", "soak", py("tests/dispmine.py"), 60.0,
        "Can Minesweeper's bottom row be PLAYED on a CGA? (SPEC.md 11.93)",
        needs=("marty",), serial=True),
    Row("curshape", "soak", py("tests/curshape.py"), 60.0,
        "Does the pointer change SHAPE over a window that asks for one? "
        "(SPEC.md 7.2) - nothing covered it when 7.2.1.1 rewrote the test",
        needs=("marty",), serial=True),
    Row("dispmode", "soak", py("tests/dispmode.py"), 60.0,
        "Single or Extend, where the second display sits, and does it survive"
        "a",
        needs=("marty",), serial=True),
    Row("dispmodex", "soak", py("tests/dispmodex.py"), 60.0,
        "Which display does Missile Command ask about Mode X? (SPEC.md"
        "39.18.1)",
        needs=("marty",), serial=True),
    Row("dispnp", "soak", py("tests/dispnp.py"), 60.0,
        "Does a WIDE straddling Note Pad letter its whole row? (SPEC.md"
        "27.2.1)",
        needs=("marty",), serial=True),
    Row("cfgtrip", "soak", py("tests/cfgtrip.py"), 150.0,
        "SPEC.md 51.5.3: does a setting still survive the panel and a reboot?"
        "The parser is two copies now - the reader in the boot overlay and the"
        "writer inside CTRL.DRV - sharing no segment, no table and no buffer,"
        "and every way of getting that wrong assembles cleanly and boots. So"
        "the assertion is the round trip: poke three settings, close the panel,"
        "flush the disk the guest wrote and boot IT. Two boots, which is why it"
        "is here and not in the gate",
        needs=("marty",), serial=True),
    Row("dispreboot", "soak", py("tests/dispreboot.py"), 60.0,
        "WHO WRITES ui_rebootq? (docs/DUAL-DISPLAY-VGA.md 8(11))",
        needs=("marty",), serial=True),
    Row("dispsave", "soak", py("tests/dispsave.py"), 60.0,
        "Does the raise cache work on the SECOND display? (SPEC.md 39.14.8)",
        needs=("marty",), serial=True),
    Row("dispblitp", "soak", py("tests/dispblitp.py"), 180.0,
        "SPEC.md 5.4.3: does gfx_blitp's REFUSAL survive its own teardown?"
        "Its whole output is CF and the teardown opened with a `cmp`, so every"
        "refusal came back as drawn - invisible until an extended desktop,"
        "where a straddle is one. Two legs, because a DIRECT move onto the"
        "mono display refuses on a different guard and leaked a display nest."
        "Needs the VGA+mono machine",
        needs=("marty",), serial=True),
    Row("dispstrad", "soak", py("tests/dispstrad.py"), 60.0,
        "Does a window dragged across the seam give back the rows only ONE"
        "display",
        needs=("marty",), serial=True),
    Row("disptext", "soak", py("tests/disptext.py"), 60.0,
        "Does going back to text name the CARD? (SPEC.md 39.20)",
        needs=("marty",), serial=True),
    Row("dispvy", "soak", py("tests/dispvy.py"), 60.0,
        "How many rows of the SECOND monitor can a straddling window use?",
        needs=("marty",), serial=True),
    Row("drvcall", "soak", py("tests/drvcall.py"), 60.0,
        "Can a PACKAGE reach a DRIVER? (SPEC.md 20.11, docs/NET-STACK-PLAN.md"
        "stage A)",
        needs=("marty",), serial=True),
    Row("drvscroll", "soak", py("tests/drvscroll.py"), 60.0,
        "SPEC.md 31.1.2: scrolling the Drivers list draws the LIST once, not"
        "thrice.",
        needs=("marty",), serial=True),
    Row("drvup", "soak", py("tests/drvup.py"), 60.0,
        "SPEC.md 13.8.4: a DRIVER's Control Panel page acts on the RELEASE.",
        needs=("marty",), serial=True),
    Row("editmove", "soak", py("tests/editmove.py", "--app", "notepad"), 60.0,
        "Compact the heap out from under a live app that is holding a big"
        "claim",
        needs=("marty",), serial=True),
    Row("fdlggrey", "soak", py("tests/fdlggrey.py"), 60.0,
        "The file dialog's default button: REDRAWN IN PLACE must equal"
        "FRESHLY PAINTED.",
        needs=("marty",), serial=True),
    Row("fdlgup", "soak", py("tests/fdlgup.py"), 60.0,
        "SPEC.md 13.8.3: the Standard File dialog's buttons fire on the"
        "RELEASE.",
        needs=("marty",), serial=True),
    Row("fmthumb", "soak", py("tests/fmthumb.py"), 60.0,
        "SPEC.md 13.10.5: the Disk window's scroll-bar THUMB is dragged, and"
        "x is never read.",
        needs=("marty",), serial=True),
    Row("fdlgthumb", "soak", py("tests/fdlgthumb.py"), 90.0,
        "SPEC.md 13.10.5: ...and the Standard File dialog's, which is the"
        "second bar one gesture record has to tell apart (13.10.5.10).",
        needs=("marty",), serial=True),
    Row("pkgthumb-np", "soak", py("tests/pkgthumb.py", "notepad"), 90.0,
        "SPEC.md 13.10.7: the thumb gesture inside a PACKAGE - Note Pad.",
        needs=("marty",), serial=True),
    Row("pkgthumb-br", "soak", py("tests/pkgthumb.py", "browser"), 90.0,
        "SPEC.md 13.10.7: ...the Browser.",
        needs=("marty",), serial=True),
    Row("pkgthumb-wd", "soak", py("tests/pkgthumb.py", "word"), 120.0,
        "SPEC.md 13.10.7: ...and Word, which needed 13.10.6.4 settling first -"
        "its menus are a modal poll and the thumb's two edges are disjoint"
        "from them.",
        needs=("marty",), serial=True),
    Row("pkgthumb-tp", "soak", py("tests/pkgthumb.py", "texpad"), 90.0,
        "SPEC.md 13.10.7.2: ...and TexPad, whose TWO bars share one gesture"
        "record. --bar=1 drives the preview pane's.",
        needs=("marty",), serial=True),
    Row("fmbtn", "soak", py("tests/fmbtn.py"), 60.0,
        "SPEC.md 22.18: the Disk window's two header buttons fire on the"
        "RELEASE.",
        needs=("marty",), serial=True),
    Row("fsxdisp", "soak", py("tests/fsxdisp.py"), 60.0,
        "Does an fsx bracket take ONE display and dark the others? (SPEC.md"
        "39.18)",
        needs=("marty",), serial=True),
    Row("hdboot", "soak", py("tests/hdboot.py"), 420.0,
        "Does os8088 BOOT from the hard disk it was installed to? (SPEC.md "
        "2.9.9) instdeep proves the bytes ARRIVE and every other boot row "
        "boots a floppy, so the volume boot record - a different 512 bytes "
        "with a different loader - was unexercised, and 2.9 broke it",
        needs=("marty",), serial=True),
    Row("instdeep", "soak", py("tests/instdeep.py"), 240.0,
        "SPEC.md 52.10.13: an install reproduces the source disk's WHOLE "
        "tree - the empty SYSTEM/APPDATA and SYSTEM/DOS/OS88NET.COM included, "
        "which one folder level could not reach. It ERASES the VHD.",
        needs=("marty",), serial=True, timeout=1200),
    Row("instrest", "soak", py("tests/instrest.py"), 240.0,
        "SPEC.md 52.10.6.1: the installer's ACTION BUTTON reads Install and "
        "then Restart, there is no third button, and clicking it at the end "
        "restarts the machine. The caption is read out of the framebuffer "
        "against the kernel's own glyph table. It ERASES the VHD.",
        needs=("marty",), serial=True, timeout=1200),
    Row("hddcp", "soak",
        py("tests/hddcp.py", "build/os8088-360.img", "build/hddcp-out.bin"),
        90.0,
        "The hard-disk driver's Control Panel page, the two windows behind"
        "it, and SPEC.md 52.6.1's tick-mounts-the-disk. It takes an image"
        "and an output path and DEFAULTS both: registered with neither,"
        "every run died on sys.argv[1] before the emulator started.",
        needs=("marty",), serial=True),
    Row("mediadisk", "soak", py("tests/mediadisk.py"), 60.0,
        "The 360KB MEDIA DISK mounts, and the apps disk keeps MEDIA (SPEC.md"
        "24.4).",
        needs=("marty",), serial=True),
    Row("mkclick", "soak", py("tests/mkclick.py"), 60.0,
        "mkclick - generate CLICK.MOD, a metronome for judging A/V sync by"
        "eye and ear.",
        needs=(), serial=False),
    Row("minexflag", "soak", py("tests/minexflag.py"), 180.0,
        "A wrong flag must not be drawn pixel-identical to a mine (SPEC.md "
        "23): the X over it is light red because a black one lands entirely "
        "inside the black glyph beneath and cannot be seen.",
        needs=("marty",), serial=True, timeout=900),
    Row("minesrc", "soak", py("tests/minesrc.py"), 180.0,
        "SPEC.md 13.11's right button: it flags a Minesweeper cell, and it "
        "does nothing on the strip, on an open cell or on a window that was "
        "not already frontmost.",
        needs=("qemu", "nasm"), serial=True, timeout=900),
    Row("tmload", "soak", py("tests/tmload.py"), 150.0,
        "SPEC.md 28.7: the CPU meter and the process rows read the same "
        "numbers. The page used to show two figures that contradicted each "
        "other in plain sight and were both right - it charged its own "
        "spinning worker 34-38% of CPU TIME while the graph drew 0-2% of "
        "SPIN COUNT. This compares three readings computed three ways: the "
        "kernel's sch_cycles, the page's tm_load, and the page's tm_pct.",
        needs=("marty",), serial=True, timeout=900),
    Row("uiblock", "soak", py("tests/uiblock.py"), 150.0,
        "SPEC.md 8.1.2: ui_task blocks instead of spinning, so an idle "
        "desktop is 97% HALTED and the loop runs 18 times a second instead "
        "of 1,134 - which nothing on screen can show, so the rate is the "
        "only witness. Its last row is the one that matters to a person: the "
        "LOST WAKEUP (8.1.2.3) is a TAIL, not a median, and before the guard "
        "existed it was one mouse event in fourteen waiting a whole tick.",
        needs=("marty",), serial=True, timeout=900),
    Row("schacct", "soak", py("tests/schacct.py"), 90.0,
        "SPEC.md 8.1.1: the scheduler charges a slice only when the task "
        "CHANGES, so an idle desktop - where every switch resumes the task "
        "that was already running - pays the TICK rate and not the switch "
        "rate. The only thing in this tree that can see it: the rule is "
        "exact, so putting the unconditional call back changes no counter, "
        "no screen and no snapshot, and costs 10.9% of a 4.77 MHz 8088. The "
        "books are checked beside it against MartyPC's own cycle counter, "
        "which is an authority outside the kernel's arithmetic.",
        needs=("marty",), serial=True, timeout=600),
    Row("heapscrl", "soak", py("tests/heapscrl.py"), 260.0,
        "SPEC.md 28.4.4: the Task Manager's heap page scrolls, its bar "
        "survives six refreshes of the list beside it (tm_rowr), and a scroll "
        "down and back leaves the rows byte-identical - which is what says "
        "SPEC.md 28.2's per-chunk cache is indexed by the SCREEN row and not "
        "the table row. On a 5150 with a CGA, because the TWO-COLUMN layout "
        "is the one that can put a tm_mrow_nolast blank between the table and "
        "its own end stop and no one-column machine can show it.",
        needs=("marty",), serial=True, timeout=900),
    Row("trkscrl", "soak", py("tests/trkscrl.py"), 180.0,
        "SPEC.md 45.12.2: a jump of n rows in the pattern view costs ONE "
        "gfx_scroll and no full repaint, and what it leaves on the screen is "
        "byte-identical to a repaint of the same view. QEMU, because the "
        "graphics fullscreen is not what a tier-0 machine draws.",
        needs=("qemu", "nasm"), serial=True, timeout=900),
    Row("mouseup", "soak", py("tests/mouseup.py"), 60.0,
        "SPEC.md 13.7's release, apps/os88ui.inc's arm, and MOUSEUP-PLAN"
        "4.2's guard.",
        needs=("marty",), serial=True),
    Row("paintgif", "soak", py("tests/paintgif.py"), 60.0,
        "HOW LONG DOES PAINT TAKE TO OPEN OS8088.GIF? - in GUEST CYCLES",
        needs=("marty",), serial=True),
    Row("paintplan", "soak", py("tests/paintplan.py"), 150.0,
        "SPEC.md 42.13: is Paint's PLANAR canvas the picture? Opens"
        "OS8088.GIF and compares the screen against the FILE, so the GIF"
        "decoder, pt_line_put's packing into four planes and gfx_blitp are"
        "all inside one answer",
        needs=("marty",), serial=True),
    Row("blitp", "soak", py("tests/blitp.py"), 120.0,
        "SPEC.md 5.4.3: does gfx_blitp put the bytes where it was given them?"
        "Reads the four PLANES rather than the rendered frame - which below"
        "the raster is last frame's, and reads exactly like a blit that"
        "stopped halfway. Needs `make bench`",
        needs=("marty", "nasm"), serial=True),
    Row("blitpair", "soak", py("tests/blitpair.py"), 90.0,
        "SPEC.md 5.4.1.1: is the 1bpp canvas the PICTURE? OS8088.GIF is two"
        "colours, so 39.4 sends every pixel to a solid class and the"
        "framebuffer can be compared against the FILE - which is the only"
        "thing that can see sw_blit_row's tables read through the wrong"
        "segment (5.4.1.3 moved them to .lowbss)",
        needs=("marty",), serial=True),
    Row("paintdraw", "soak", py("tests/paintdraw.py"), 180.0,
        "SPEC.md 42.13: does DRAWING on the planar canvas touch only what it"
        "drew? paintplan covers the routines that write a whole row; this one"
        "covers pt_rect, which is the pencil's dab and builds a left mask, a"
        "right mask and a byte count the packed path gets from one shift",
        needs=("marty",), serial=True),
    Row("paintsu", "soak", py("tests/paintsu.py"), 240.0,
        "SPEC.md 11.96.11: on a 1bpp adapter Paint banks its WHOLE content"
        "rather than the tool column, because there the cache is ~9KB and the"
        "canvas it saves redrawing is 399 ms. Asserts the size asked for, that"
        "no canvas blit crosses an uncover, and that what came back is right",
        needs=("marty",), serial=True),
    Row("paintfill", "soak", py("tests/paintfill.py"), 180.0,
        "SPEC.md 42.13.2: does the FLOOD FILL find the same edges the picture"
        "has? pt_fpix gathers one bit per plane, so a plane addressed wrongly"
        "does not corrupt anything - it makes the fill see a picture that is"
        "not there. The oracle is a flood fill on the host over the same file",
        needs=("marty",), serial=True),
    Row("paintbig", "soak", py("tests/paintbig.py"), 240.0,
        "SPEC.md 42.13.2: GROW the canvas, which is the only thing that"
        "changes [pt_bpr] - the one number the two storage formats do not"
        "share - then copy a block past the clipboard's 4KB floor and paste"
        "it back at a DIFFERENT bit phase (SPEC.md 42.13.3), which is what"
        "makes both shifts and both edge masks run. Nothing else resizes",
        needs=("marty",), serial=True),
    Row("paintback", "soak", py("tests/paintback.py"), 600.0,
        "SPEC.md 11.96.11.4 and 42.13.1.3: a window dragged clear onto the"
        "other card and home again - the PICTURE first, because the stale BX"
        "this caught had the kernel writing zeros into its own .text and the"
        "damage lands wherever the layout puts it; then [pt_planar], because"
        "the canvas has to come home as four planes. The herc leg is the only"
        "row here on a machine whose colour card is not the primary",
        needs=("marty",), serial=True),
    Row("paintrow", "soak", py("tests/paintrow.py"), 240.0,
        "SPEC.md 42.13.1.2: pt_line_get's FOUR-PLANE row reader, whose only"
        "caller is the GIF writer - so nothing that draws can fail on it and"
        "no screenshot here can see it. Calls the routine directly, through"
        "five bytes written over pt_blit's entry, and compares the colour"
        "classes it returns against the file's",
        needs=("marty",), serial=True),
    Row("paintpack", "soak", py("tests/paintpack.py"), 600.0,
        "SPEC.md 42.13.1: the REFUSAL path. Builds the NOPLANE kernel, where"
        "every gfx_blitp says no in six bytes, so Paint's pt_topacked runs"
        "for real and the nibbles it produced are compared against the file,"
        "then paintbig again over it - the only kernel on which the PACKED"
        "half of pt_copy/pt_paste runs without a second monitor. Rebuilds the"
        "tree, like blitplane",
        needs=("marty", "nasm"), serial=True),
    Row("bouncecost", "soak", py("tests/bouncecost.py"), 120.0,
        "SPEC.md 14/2.6: what one Bounce frame costs the machine in 8088 "
        "cycles. Its PERIOD is task_sleep's, so nothing here can move its "
        "frame rate - what it measures is how much of a 4.77MHz machine one "
        "live Bounce takes away from the UI, which is the quantity SPEC.md "
        "2.6's cadence test is an argument about. Frames are PAIRED on the "
        "ball's (x,y,vx,vy) and the trajectory is seeded, so two kernels are "
        "compared frame for frame and the per-frame variation - which is real "
        "work, not noise - cancels instead of being averaged over.",
        needs=("marty",), serial=True, timeout=600),
    Row("blitplane", "soak", py("tests/blitplane.py"), 180.0,
        "SPEC.md 5.4.1.3: does gfx_blit4's PLANAR DECODER draw the same "
        "pixels as the run writer, on both destination phases, and is it "
        "still several times quicker? Rebuilds the tree - one of two rows "
        "that do, with gfxlk - because the A/B is two kernels. It drives "
        "Paint OFF THE BYTE GRID on purpose: since SPEC.md 42.13 a canvas on "
        "the grid is four planes and repaints through gfx_blitp, so a window "
        "left where it opens does not reach this primitive at all.",
        needs=("marty", "nasm"), serial=True, timeout=900),
    Row("paintmove", "soak", py("tests/paintmove.py"), 60.0,
        "Compact the heap out from under a LIVE Paint canvas (SPEC.md"
        "66.2/42).",
        needs=("marty",), serial=True),
    Row("rdmove", "soak", py("tests/rdmove.py"), 60.0,
        "Compact the heap out from under the RAM disk's store (SPEC.md"
        "66.5.10).",
        needs=("marty",), serial=True),
    Row("modstr", "soak", py("tests/modstr.py"), 90.0,
        "modstr - a module's own strings letter correctly (SPEC.md 2.8.6). "
        "The bytes, out of fm_hdrbuf and toast_buf, because a string read "
        "through DS instead of CS lands in kernel code and letters plausible "
        "rubbish rather than faulting",
        needs=("marty",), serial=True),
    Row("diskclone", "soak", py("tests/diskclone.py"), 90.0,
        "diskclone - Clone Disk... (SPEC.md 18.99/22.21) driven end to end, "
        "with the assertion that cannot pass for the wrong reason: the two "
        "floppies read back off the guest and diffed byte for byte. Cross "
        "drive, same drive, the un-swapped-disk guard and Esc",
        needs=("marty",), serial=True),
    Row("rdup", "soak", py("tests/rdup.py"), 60.0,
        "SPEC.md 62.9.11.3: the Ram Disk page acts on the RELEASE.",
        needs=("marty",), serial=True),
    Row("sbar", "soak", py("tests/sbar.py"), 60.0,
        "SPEC.md 13.10: the shared scroll bar, and the two kernel bars are"
        "one now.",
        needs=("marty",), serial=True),
    Row("tmrup", "soak", py("tests/tmrup.py"), 60.0,
        "SPEC.md 13.8: the Timer's three buttons fire on the RELEASE.",
        needs=("marty",), serial=True),
    Row("trackmove", "soak", py("tests/trackmove.py"), 60.0,
        "Compact the heap out from under a LOADED module (SPEC.md 66.5.2/45).",
        needs=("marty",), serial=True),
    Row("tpdraw", "soak", py("tests/tpdraw.py"), 300.0,
        "Does TeXPad's INCREMENTAL source redraw draw what a full repaint"
        "draws? (SPEC.md 69.8)",
        needs=("marty",), serial=True),
    Row("trkrate", "soak", py("tests/trkrate.py"), 60.0,
        "trkrate - XT mode's second rate, and the surface it refuses (SPEC.md"
        "45.9.3)",
        needs=("marty",), serial=True),
    Row("trktxsurf", "soak", py("tests/trktxsurf.py"), 90.0,
        "The fullscreen SURFACE is a pick, not XT mode's - text at a 45.10"
        "rate (SPEC.md 45.13.7)",
        needs=("marty",), serial=True),
    Row("wmartifact", "soak", py("tests/wmartifact.py"), 60.0,
        "Two window-manager artifacts, reproduced with NO package of ours"
        "involved.",
        needs=("marty",), serial=True),
    Row("xorrect", "soak", py("tests/xorrect.py"), 60.0,
        "gfx_xor_rect draws the same pixels it always did (SPEC.md 39.14.10)",
        needs=("marty",), serial=True),
]


def rows():
    """Every registered row, cheap ones first so failures report early."""
    return FAST + FULL + SOAK
