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
    Row("mirror", "fast", py("tests/unit/t_mirror.py"), 0.2,
        "a constant written down in two files must agree in both; there is no "
        "linker here to notice"),
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
    Row("asmrules", "fast", py("tests/unit/t_asmrules.py"), 0.5,
        "unreachable code after an unconditional jump, and a `cpu 8086` "
        "reachable from every root"),
    Row("wakedrain", "fast", py("tests/unit/t_wakedrain.py"), 0.3,
        "every event-queue drain gives a package's wake back - one that eats "
        "it deafens the window for the rest of its life (SPEC.md 74.1.1)"),
    Row("checkdocs", "fast", py("tools/checkdocs.py"), 1.0,
        "stale SPEC.md citations and slot numbers in prose (already in `make`; "
        "here too so the suite is a complete statement)"),
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
    Row("cpup", "soak", py("tests/cpup.py"), 41.3,
        "SPEC.md 13.8.3: the Control Panel acts on the RELEASE, not the"
        "press.",
        needs=("marty",), serial=True),
    Row("saver", "soak", py("tests/saver.py"), 95.0,
        "the animated screen saver end to end (SPEC.md 79): every mode draws, "
        "the overlay is loaded and freed, the wake puts the whole desktop back "
        "including the bar and the dock, no block is left in the menu bar, and "
        "all three fallbacks reach the blanker with the framebuffer untouched",
        needs=("marty",), serial=True),
    Row("cycweb", "soak", py("tests/cycweb.py"), 51.4,
        "Does the claw eat the web it slides over? (SPEC.md 67.5.3.1)",
        needs=("marty",), serial=True),
    Row("dockmark", "soak", py("tests/dockmark.py"), 60.0,
        "Does the dock strip mark windows it did not draw under? (SPEC.md"
        "30.3.3)",
        needs=("marty",), serial=True),
    Row("dualcheck", "soak", py("tests/dualcheck.py"), 60.0,
        "Can this MartyPC drive TWO video cards at once?"
        "(docs/DUAL-DISPLAY-PLAN.md 9)",
        needs=("marty",), serial=True),
    Row("heapcheck", "soak", py("tests/heapcheck.py"), 60.0,
        "Drive tests/heapfrag and read its verdict out of the guest (SPEC.md"
        "66.8).",
        needs=("marty",), serial=True),
    Row("xmcheck", "soak", py("tests/xmcheck.py"), 60.0,
        "The extended-memory TEARDOWN gate (SPEC.md 41.5, 29.4).",
        needs=("marty",), serial=True),
    Row("calcflick", "soak", py("tests/calcflick.py"), 60.0,
        "Does the Calculator FLASH? (PERFORMANCE.md Part 3.1, SPEC.md 65.4)",
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
    Row("dispreboot", "soak", py("tests/dispreboot.py"), 60.0,
        "WHO WRITES ui_rebootq? (docs/DUAL-DISPLAY-VGA.md 8(11))",
        needs=("marty",), serial=True),
    Row("dispsave", "soak", py("tests/dispsave.py"), 60.0,
        "Does the raise cache work on the SECOND display? (SPEC.md 39.14.8)",
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
    Row("instdeep", "soak", py("tests/instdeep.py"), 240.0,
        "SPEC.md 52.10.13: an install reproduces the source disk's WHOLE "
        "tree - the empty SYSTEM/APPDATA and SYSTEM/DOS/OS88NET.COM included, "
        "which one folder level could not reach. It ERASES the VHD.",
        needs=("marty",), serial=True, timeout=1200),
    Row("hddcp", "soak",
        py("tests/hddcp.py", "build/os8088-360.img", "build/hddcp-out.bin"),
        60.0,
        "The hard-disk driver's Control Panel page, and the two windows"
        "behind it.",
        needs=("marty",), serial=True),
    Row("mediadisk", "soak", py("tests/mediadisk.py"), 60.0,
        "The 360KB MEDIA DISK mounts, and the apps disk keeps MEDIA (SPEC.md"
        "24.4).",
        needs=("marty",), serial=True),
    Row("mkclick", "soak", py("tests/mkclick.py"), 60.0,
        "mkclick - generate CLICK.MOD, a metronome for judging A/V sync by"
        "eye and ear.",
        needs=(), serial=False),
    Row("minesrc", "soak", py("tests/minesrc.py"), 180.0,
        "SPEC.md 13.11's right button: it flags a Minesweeper cell, and it "
        "does nothing on the strip, on an open cell or on a window that was "
        "not already frontmost.",
        needs=("qemu", "nasm"), serial=True, timeout=900),
    Row("mouseup", "soak", py("tests/mouseup.py"), 60.0,
        "SPEC.md 13.7's release, apps/os88ui.inc's arm, and MOUSEUP-PLAN"
        "4.2's guard.",
        needs=("marty",), serial=True),
    Row("paintgif", "soak", py("tests/paintgif.py"), 60.0,
        "HOW LONG DOES PAINT TAKE TO OPEN OS8088.GIF? - in GUEST CYCLES",
        needs=("marty",), serial=True),
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
