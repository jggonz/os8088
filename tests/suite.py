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
import os


class Row:
    """One registered test."""

    __slots__ = ("name", "tier", "cmd", "secs", "needs", "serial", "why",
                 "timeout", "builds", "alone")

    def __init__(self, name, tier, cmd, secs, why, needs=(), serial=False,
                 timeout=None, builds=False, alone=False):
        self.name, self.tier, self.cmd = name, tier, cmd
        self.secs, self.why = secs, why
        self.needs = tuple(needs)
        self.serial = serial
        # BUILDS: this row shells out to `make`, so it writes build/ and
        # cannot share the tree with anything - not with another builder, and
        # not with a row reading what it is halfway through rewriting. It is
        # the one thing that still forces a row to run ALONE now that
        # emulator instances are isolated (tools/os88test.py's --marty-jobs),
        # and `tests/unit/t_registry.py` checks the flag against the script
        # rather than trusting it: a row that gains a `make` and not the flag
        # would be a suite that fails one run in five for no visible reason.
        self.builds = builds
        # ALONE: this row's ANSWER needs the machine to itself, which is a
        # different claim from `builds` and was not sayable until now. A row
        # whose assertion is a RATE - frames a second, milliseconds a redraw -
        # cannot share four cores with two other guests: that is not a flaky
        # row, it is the wrong measurement. Neither can a row whose clicks are
        # paced by a HOST-timed settle, because how much guest time a settle
        # covers is then a property of the box (docs/HANDOFF-SOAK-FINDINGS.md
        # B5).
        #
        # It used to be spelled by EXCLUDING those rows from the wide run and
        # taking them in a second one - `-x saverate -x deskbench ...`, written
        # into two handoffs and remembered by whoever read them. A property of
        # the row is the place for it: the runner keeps an `alone` row out of
        # the shared lane and runs it in the one-at-a-time lane of the SAME
        # run, so there is no second pass to forget.
        #
        # It is not `builds`. A builder cannot share the TREE; one of these
        # can, and only needs the CORES.
        self.alone = alone
        # A generous default: the point of the per-row timeout is to stop a
        # hung emulator eating the tier, not to police a slow machine.
        self.timeout = timeout or max(60, int(secs * 4) + 30)


def py(*a):
    return ["python3"] + list(a)


def _kernel_sources():
    """Every kernel source, ROOT-relative and sorted.

    Asserted non-empty on purpose: a gate reporting 0 findings because its file
    list came out empty is indistinguishable from a clean tree, and the whole
    point of docs/STKBALANCE-KERNEL.md's sensitivity work is that a quiet gate
    has to be quiet for a reason. The runner execs rows with cwd=ROOT, so these
    stay relative.
    """
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = sorted(os.path.relpath(os.path.join(root, "kernel", f), root)
                 for f in os.listdir(os.path.join(root, "kernel"))
                 if f.endswith(".inc"))
    assert len(src) >= 30, "kernel/*.inc came out as %d files" % len(src)
    return src + [os.path.join("kernel", "kernel.asm")]


# --------------------------------------------------------------------------
# fast - host-side, no emulator, no build. Runs on every `make`.
# --------------------------------------------------------------------------
FAST = [
    Row("blobruns", "fast", py("tests/unit/t_blobruns.py"), 0.1,
        "how many int 13h calls stage 1 spends on the blob, per geometry "
        "(SPEC.md 15.3.8.5) - the count is NOT a function of BOOT2_SECS "
        "alone, because a run is bounded by the track and KERNEL.SYS starts "
        "where each BPB puts the data area. 13 is the last sector that fits "
        "two calls on a 720KB disk, and the 14th costs a whole revolution to "
        "move one sector",
        needs=("nasm",)),
    Row("bootfloor", "fast", py("tests/unit/t_bootfloor.py"), 3.5,
        "stage 1's RAM floor against the kernel's own ladder (SPEC.md 2.7.1) "
        "- HEAP_PARA is INJECTED, so the two can disagree, and guard 5c used "
        "to reconcile them until stage 1 started testing the exact condition "
        "and the guard became `x > x + 160`. Also the FLAT_PAYLOAD clamp: a "
        "small diagnostic payload bounds below RELOC_ADJ, where the `sub` "
        "after the compare underflows and relocates the sector to the top of "
        "a 1MB machine that is not there",
        needs=("nasm",)),
    Row("lowwin", "fast", py("tests/unit/t_lowwin.py"), 5.5,
        "the mount-owned window is the BOTTOM of .lowbss (SPEC.md 2.1.2), so "
        "that it and the FAT window under it are one contiguous 8,192-byte "
        "region dead for the whole of kmain. It is bought by one include line "
        "and nothing else would notice it sliding: no RAM moves, no address "
        "any code names changes, and the kernel boots either way - only "
        "stage C would find out, by writing the overlay over vid_rowtab",
        needs=("nasm",)),
    Row("api-abi", "fast", py("tests/unit/t_api_abi.py"), 3.3,
        "the API table decoded from kernel.bin and compared with the SDK - the "
        "silent merge collision CLAUDE.md asks to be checked by hand"),
    Row("stackprose", "full", py("tests/unit/t_stackprose.py"), 5.0,
        "a doc or comment that names the task stack's SIZE names the one the "
        "kernel has. SCH_STACK has been 1,536, 512, 256 and 384; SPEC.md 2.1 "
        "and 20.6 rule 6 followed it every time and the forty-odd places "
        "CITING them did not. That is not a typo class - docs/UPSTREAM.md's "
        "stale 256 had a session report a worker-stack contract difference "
        "between this branch and `main` that had not existed since #112, and "
        "go looking for what to adapt. os88geom guards the copies a SCRIPT "
        "retyped; this guards the ones a HUMAN did. FULL rather than fast: a stale comment misleads a reader, it does not break a build, and the fast tier runs on every `make` against a 30s budget this row is a sixth of",
        needs=()),
    Row("mirror", "fast", py("tests/unit/t_mirror.py"), 3.9,
        "a constant written down in two files must agree in both; there is no "
        "linker here to notice"),
    Row("inktab", "fast", py("tests/unit/t_inktab.py"), 0.2,
        "SPEC.md 42.23.1: Paint's two ink-class masks ARE the kernel's "
        "gfx_inktab. A one-bit canvas stores what a 1bpp SCREEN shows, so the "
        "two have to agree about which of the sixteen are solid and which are "
        "the 50% dither - and the first version of the masks was a GUESS that "
        "put six dither colours in the white class. gfx_inktab is a `db` "
        "table, so `mirror` cannot see it: that is why this is a row of its "
        "own and not one of its names",
        ),
    Row("appsmall", "fast", py("tests/unit/t_appsmall.py"), 0.8,
        "SPEC.md 27.16's two claims: -DAPP_SMALL costs the SHIPPED package "
        "zero bytes (docs/KERN-SPLIT-PLAN.md 6's gate, one level down), and "
        "the small build is really smaller. Both fail silently - a %ifdef one "
        "line too wide changes the shipped package for a feature it still "
        "has, and a define that stops reaching the source leaves "
        "build/smallapps*.img as the ordinary floppy under another name. It "
        "is also the only thing keeping the small arm ASSEMBLING: nothing in "
        "`all` builds it"),
    Row("ktags", "fast", py("tests/unit/t_ktags.py"), 0.1,
        "every owner tag the kernel ships has a TYPE name on the Task "
        "Manager's heap page - SPEC.md 28.4's hex fallback is for a tag this "
        "build has never seen, and three shipped ones had been sitting in it"),
    Row("dirwsize", "fast", py("tests/unit/t_dirwsize.py"), 0.1,
        "The directory cache picks its WIDTH from the machine now (SPEC.md "
        "18.95.5), so three numbers in three places have to agree: the "
        "constants, the gate's divisor, and the shift-add that turns slots "
        "into KB. The row that matters is that the claim COVERS the width at "
        "every n the machine can pick - dsk_rah_fill addresses the last slot "
        "inside the claim, so a claim short by one slot is an int 13h writing "
        "into whatever the heap handed out next. Host-side because a partial "
        "width needs a 36-126KB free run and no emulator here can be put in "
        "that state on demand",
        needs=(), serial=False),
    Row("pgrank", "fast", py("tests/unit/t_pgrank.py"), 0.1,
        "The purgeable caches are ORDERED - WSAVE below FATW below DIRW - "
        "and that ordering IS the machine's eviction policy (SPEC.md 50.6.4). "
        "A rank is one token with no callers and is silent both ways: too low "
        "and the cache is thrown away in front of something cheaper to "
        "rebuild, too high and it survives at a dearer one's expense. "
        "MEM_P_FATW shipped at LOW for one commit on a per-event cost weighed "
        "against a whole-install one (SPEC.md 18.8.4). Also checks each rank "
        "is inside the purgeable range at all, and that dsk_fatw_want asks "
        "mem_avail_lvl at its OWN rank so it may take the caches it outranks",
        needs=(), serial=False),
    Row("kernbudget", "fast", py("tests/unit/t_kernbudget.py"), 0.1,
        "docs/KERNEL-MEMORY.md's blessed baseline carries THIS kernel's "
        "KERN_BUDGET - it went two moves behind because tools/kernsize.py "
        "compared spare and could not see a budget move at all"),
    Row("swallow", "fast", py("tests/unit/t_swallow.py"), 0.1,
        "a statement that ended up inside a block comment: it compiles clean, "
        "runs never, and cost apps/c64 a Paste that outlived a machine reset"),
    Row("drvmem", "fast", py("tests/unit/t_drvmem.py"), 0.1,
        "the Drivers page's memory column (SPEC.md 31.6.2) re-derived: every "
        "image term against the .drv this build made, every claim term against "
        "the constant in the driver that takes it"),
    Row("image", "fast", py("tests/unit/t_image.py"), 0.1,
        "the shipped floppies read by an independent FAT12 walker: contiguity, "
        "the standard BPB, SPEC.md 19.6's attributes"),
    Row("pkg", "fast", py("tests/unit/t_pkg.py"), 0.1,
        "package/driver/module headers, and every file on every image proved "
        "identical to the artifact it was built from"),
    Row("sfx", "fast", py("tests/unit/t_sfx.py"), 0.4,
        "OS88NET.COM's self-extracting stub (SPEC.md 62.12) EXECUTED - the "
        "shipped bytes run in a small 8086 and must rebuild os88net.raw "
        "exactly. The DOS end has shipped broken twice for want of ever "
        "being run (tests/dosstub); a packer checked only by its own "
        "decoder is that shape again"),
    Row("diskverify", "fast", py("tests/unit/t_diskverify.py"), 0.5,
        "the tree's own fsck, pointed at the seven images `make` ships and "
        "never ran on"),
    Row("qemuown", "fast", py("tests/unit/t_qemuown.py"), 0.1,
        "every test that LAUNCHES a QEMU registers a teardown for it. `make "
        "test` daemonises the emulator and returns, so for most of this tree's "
        "life a row that FAILED simply left its running - and the only thing "
        "that ever killed one was the next run's kill-stale, which reaches an "
        "instance in the same checkout with the same pidfile and nothing else. "
        "Two of them survived FIVE HOURS from two worktrees and broke "
        "`ps2mouse` on the pre-merge gate with a write-lock error naming "
        "build/os8088.img: the cost of the leak is paid by an unrelated row, "
        "hours later, wearing a message about the wrong subject "
        "(docs/HANDOFF-SOAK-FINDINGS.md B9)"),
    Row("canary", "fast", py("tests/unit/t_canary.py"), 0.1,
        "SPEC.md 18.93.1's canary offset re-derived from every shipped image's "
        "own BPB: it has to name a sector a transfer run reads AFTER the head "
        "boundary, because the half before it loads correctly on exactly the "
        "machine the canary is for - which is how the first one shipped wrong"),
    Row("mlen", "fast", py("tests/unit/t_mlen.py"), 3.4,
        "twelve month lengths, read back out of build/kernel.bin. clk_mlen "
        "carries the eleven non-February ones as a 16-bit MASK since kernel "
        "size pass 3 - three bytes shorter than the db table it replaced, and "
        "twelve facts collapsed into one hex constant nobody can check by "
        "eye. Nothing else in the tree covers them: tests/dtfield.py row 3 is "
        "the only test that reaches the routine at all and it is '30 Jan + "
        "one month lands on 28/29 Feb', i.e. February - which is the BRANCH "
        "below the mask and the one arm the rewrite did not touch. A wrong "
        "bit surfaces as '31 April accepted in the Date/Time page' and as a "
        "midnight rollover on the wrong day, which no harness here can run "
        "long enough to see"),
    Row("bsssentinel", "fast", py("tests/unit/t_bsssentinel.py"), 3.5,
        "a sentinel byte whose RESTING value is not zero cannot live in .bss "
        "(SPEC.md 12.8.5.1): `-f bin` emits nothing for it and the boot read "
        "lands padding on those bytes, so it comes up 0. fsx_cur shipped that "
        "way the moment fpg_arm started reading it from OUTSIDE an fsx "
        "bracket, and the file-progress widget was refused for every file "
        "operation on the machine - which on an install reads as a lock"),
    Row("invariants", "fast", py("tests/unit/t_invariants.py"), 1.2,
        "three run-time facts that no %if can express, checked by WHO WRITES "
        "the byte: [sch_cur] is never 0xFF (fsx's ownership compares refuse "
        "[fsx_task]'s no-bracket sentinel only because of that, so a second "
        "writer parking one there grants a bracket to nobody); "
        "[vid_mono]/[vid_planes] are one fact written together (SPEC.md 39.26 "
        "deleted four plane loops on it, and a writer that moves one leaves "
        "all four drawing plane 0 alone on every adapter); and "
        "[vid_rseg] has one writer, which is a DIFFERENT fact because "
        "sw_xfer used to end on a segment compare",
        needs=(), serial=False),
    Row("assocpage", "fast", py("tests/unit/t_assocpage.py"), 0.1,
        "the document page is GENERATED now (SPEC.md 54.3), so its 32 words "
        "are replayed on the host against a golden list - the only copy of "
        "them left in the tree. tests/assocglyph.py is the gate on the glass, "
        "but two of its three assertions compare this kernel against ITSELF, "
        "so a generator that composes the same WRONG page every time passes "
        "both of them cleanly and the icon it is wrong about is on every "
        "document in the system. Its third (--ref) closes that and needs a "
        "capture taken BEFORE the change, on a 1bpp adapter, under an "
        "emulator; this row is the same proof for the DATA half in a fifth of "
        "a second, on every make",
        needs=()),
    Row("registry", "fast", py("tests/unit/t_registry.py"), 0.2,
        "every test in tests/ is registered in a tier or says why not - the row "
        "that stops this suite going back to a directory nobody can enumerate"),
    Row("asmrules", "fast", py("tests/unit/t_asmrules.py"), 2.0,
        "unreachable code after an unconditional jump, a prologue restored in "
        "the WRONG ORDER (SPEC.md 1's register discipline: balanced depth, "
        "swapped pair, nothing faults), a `cpu 8086` reachable from every "
        "root, and a kernel LOCAL BLOCK nothing can reach - check 1 stops at "
        "\"is there a label between the jump and this line\" and a label only "
        "helps if something reaches it, which is how the DMA staging arm of "
        "both file pipelines rotted for a year with this row green "
        "(SPEC.md 18.4.2.1)"),
    Row("resident", "fast", py("tests/unit/t_resident.py"), 3.7,
        "nothing the splash's first tick runs may jump to SPEC.md 15.1.2's "
        "epilogue ladder - the ladder is at the far end of .text and the "
        "floppy has not delivered it yet, so the machine dies with a blank "
        "screen and no message. kernel.asm's SPL_RES_SIZE guard measures where "
        "the resident code ENDS, and size is not reach"),
    Row("wakedrain", "fast", py("tests/unit/t_wakedrain.py"), 0.2,
        "every event-queue drain gives a package's wake back - one that eats "
        "it deafens the window for the rest of its life (SPEC.md 74.1.1)"),
    Row("wab", "fast", py("tests/unit/t_wab.py"), 0.1,
        "the demo bundles `all` just packed, read back by an independent "
        "second reader of the .WAB format - weavesim and t_wab are two "
        "implementations written from WEAVE-SPEC that can disagree, and "
        "until the 8086 runtime lands this row is the disagreement's only "
        "audience"),
    Row("lmpack", "fast", py("tests/unit/t_lmpack.py"), 6.5,
        "WEAVE-SPEC 11.1's byte-identity gate, host-side: LOOM's five "
        "SHIPPING compilers built with the host cc, packing every demo, "
        "every template and every case in tests/weave/packerr/, diffed "
        "against tools/weavesim.py bundle for bundle and sentence for "
        "sentence. It is NOT the gate - `weavepack` packs on the MACHINE, "
        "and the difference between the two is one word wide (int is 32 "
        "bits here) - but it is what makes an on-machine compiler writable "
        "at all, and it puts a weavesim change in front of the next `make` "
        "rather than the next soak run. SKIPS with no host compiler, "
        "because a clone with nasm and python3 builds every floppy this "
        "project ships and a red suite there would be reporting on the box",
        needs=()),
    Row("textrules", "fast", py("tests/unit/t_textrules.py"), 0.7,
        "SPEC.md 6.6's ratchet: transparent text (font_char/font_str) draws every "
        "pixel twice and flashes on the target machine, so every call site is "
        "registered in tests/textsites.txt with a reason and the count can only "
        "go down"),
    Row("layout", "fast", py("tests/unit/t_layout.py"), 0.1,
        "SPEC.md 2.9: a GUEST ADDRESS IS NOT A FILE OFFSET. Stage 2 sits in "
        "front of .text in kernel.bin, so a host-side reader that indexes the "
        "image by a symbol, a segment or a return address lands 6,656 bytes "
        "early - on real code, silently. Five readers got it wrong "
        "independently: two rows dead since 2.9, two reporting .cold as "
        "corrupt every run, and stkwater recognising 126 of 3,000 call sites"),
    Row("fixtures", "fast", py("tests/unit/t_fixtures.py"), 0.1,
        "a row's scratch floppy is a BUILD PRODUCT: os88disk.py behind a bare "
        "`not os.path.exists` builds it once and every run after boots "
        "whatever build/ held that minute, which is the stale kernel.bin trap "
        "in other clothes. It read paintsu as 0 pixels wrong against a Paint "
        "without the fix in it, and that number was pushed on"),
    Row("vbrseg", "fast", py("tests/unit/t_vbrseg.py"), 3.4,
        "SPEC.md 52.10.2.1: build/boothd.bin's BLOB_SEG and SPL_FSEG read back "
        "out of the assembled sector and compared with build/kernel.bin's own "
        "map. The volume boot record is told where the heap starts by a host "
        "tool re-running over kernel.asm, and a knob kernel whose ladder the "
        "tool did not know about boots into wild execution with no build "
        "error anywhere.",
        ),
    Row("checkdocs", "fast", py("tools/checkdocs.py"), 1.6,
        "stale SPEC.md citations and slot numbers in prose (already in `make`; "
        "here too so the suite is a complete statement)"),
    Row("docindex", "fast", py("tools/os88index.py", "--check"), 0.2,
        "docs/INDEX.md still matches the tree - an index that has drifted is "
        "worse than none, because it is consulted and believed"),
    Row("checkreadme", "fast", py("tools/checkreadme.py", "readme.txt"), 0.1,
        "README.TXT's width and size rules - Note Pad refuses a file one byte "
        "too long and shows nothing at all"),
    Row("ovlchk", "fast", py("tools/os88ovlchk.py"), 1.4,
        "no near call crosses a section boundary - it assembles cleanly and "
        "runs wrong"),
    Row("dsegaudit", "fast", py("tools/dsegaudit.py"), 0.2,
        "no path holding [dsk_dseg] can reach a claim, and a claim COMPACTS "
        "(SPEC.md 50.6.2). It is a 0/1 gate with no harness around it and "
        "nothing ran it - not `make`, not this file, and not t_registry, "
        "whose walk is over tests/ and cannot see a tool. A static gate that "
        "nobody runs is a comment"),
    Row("stknosave", "fast",
        py("tools/stkdepth.py", "drivers/ether/ether.asm", "--check"), 0.4,
        "every `; STKDEPTH-NOSAVE:` in ETHER.DRV still holds: the routines "
        "that stopped saving a register to fit a 384-byte task slice (SPEC.md "
        "72.16.4) still get it back from every callee. Without this the trade "
        "is a landmine for whoever edits the TCP stack next"),
    Row("stkbalance", "fast",
        py("tools/stkbalance.py", "apps/sheet/sheet.asm", "apps/chart/chart.asm",
           "apps/os88chart.inc", "apps/os88fp.inc", "apps/os88text.inc",
           "apps/os88line.inc", *_kernel_sources()), 0.9,
        "every `ret` in the KERNEL and in SHEET, CHART and the includes they "
        "share is reached at "
        "the depth it started at. `ch_legend` pushed SI and never popped it, so "
        "its `ret` jumped to the saved register: a black canvas and a wedged "
        "app, with no crash and no message (SPEC.md 82.7.3). The walk is "
        "path-aware because a naive push-vs-pop count flags one routine in ten "
        "and would just be ignored. STILL SCOPED to these files, but no longer "
        "because the kernel cannot be walked: the walker follows tail jmps "
        "across files now, and the two `; STKBALANCE-OK:` in sched.inc that "
        "cover the context switch and task_yield's fabricated int 08h frame "
        "have landed, so the kernel measures ZERO and is GATED here from this "
        "commit on (docs/STKBALANCE-KERNEL.md carries the triage of all 24). "
        "Turned on DURING size pass 2 rather than after it, so an imbalance is "
        "caught by the batch that introduces it instead of by a bisect. "
        "One gap "
        "is left and is counted in the tool's own summary line: loop back-edge "
        "conflicts are suppressed, because the count lives in a register"),

    Row("stkapps", "fast", py("tests/unit/t_stkapps.py"), 2.1,
        "every `ret` in EVERY SHIPPED PACKAGE AND DRIVER is reached at the "
        "depth it started at. `ch_legend` pushed SI and never popped it, so its `ret` "
        "jumped to the saved register: a black canvas and a wedged app, with "
        "no crash and no message (SPEC.md 82.7.3). This row walked only SHEET, "
        "CHART and four shared includes - 776 entries - until three blind "
        "spots in the walker were closed; it walks 9,038 now, drivers/ "
        "included - the TCP/IP stack had never been walked either. Each blind spot "
        "hid a whole class: `apps/*/*.inc` was in no file list, so RunCPM's "
        "Z80, the C64's 6510 and Weave's VM had never been walked by anything; "
        "all three dispatch as `jmp [cs:bx+tab]`, which a walker looking for "
        "`jmp [tab+reg]` reads as every opcode handler being a routine entered "
        "at depth 0; and wvm.inc puts its branches inside macros. It found one "
        "real defect - `op_size` in os88parts.inc returned into a saved "
        "register on a malformed part table, in every package via "
        "os88api.inc. The KERNEL is the `stkbalance` row above, not this one: "
        "the two file lists have nothing in common and were arrived at from "
        "opposite ends (docs/STKBALANCE-KERNEL.md 4)",
        ),

    Row("gifdrag", "soak", py("tests/gifdrag.py"), 300.0,
        "THE FIELD'S OWN FREEZE, driven end to end (SPEC.md 8.7.4): the Task "
        "Manager on its HEAP page while PAINT holds MEDIA/OS8088.GIF, then the "
        "window dragged again and again. It asserts the MARGIN and not the "
        "survival, which is the whole reason it is a test rather than a "
        "screenshot: 'it did not freeze' is what every run before the report "
        "also said, because this machine's interrupt floor is 32 bytes where "
        "SPEC.md 8.7 sizes against 64 on iron - so the walk that killed a real "
        "5150 reads 180 of 192 here and passes. It fails when any slice, or "
        "task 0's own 512, goes past 80% full, which is the emulator's honest "
        "question: is there room left for the frame it is not charging? SOAK "
        "and not full - it boots, launches two packages, decodes a GIF and "
        "drags eight times, which is minutes",
        ),

    Row("stkclass", "fast", py("tests/unit/t_stkclass.py"), 12.0,
        "every package's DECLARED stack class (SPEC.md 8.7.2) covers its "
        "worker's deepest chain plus SPEC.md 8.7's 64-byte interrupt floor, at "
        "Frotz's 1.25x - the thinnest margin the tree already carries, so "
        "nothing shipping has to move and only a NEW thinnest can fail. The "
        "row above checks a package's stack arithmetic BALANCES; nothing "
        "checked the slice was big enough to hold it, and `OS88_STACK_192` was "
        "a number a human typed after running a tool once. SPEC.md 8.7.4 is "
        "what that cost: tools/stkdepth.py followed `call` edges and not tail "
        "jumps, so `tm_worker` priced at 56 bytes when the heap page it reaches "
        "by `ja tm_upd_heap` is 96 - the Task Manager took 192 on the strength "
        "of 56, measured 180 of them with its heap page open beside PAINT, and "
        "went through the canary into sch_stkdie's cli/hlt on a real 5150. It "
        "reads the class out of the BUILT .o88's header byte, not out of the "
        "source, so a packer that stops emitting the field fails this too",
        ),

    Row("stkwalker", "fast", py("tests/unit/t_stkbalance.py"), 0.6,
        "the stack walker itself, against eleven idioms it must stay QUIET "
        "about and six defect shapes it must catch. A gate that reports "
        "nothing passes every build and defends nothing; one that reports a "
        "routine in ten gets ignored and defends nothing either, which is why "
        "the kernel went ungated for this tree's whole life. Both halves are "
        "pinned here: the QUIET half is every idiom that was once a finding "
        "(a continuation, a cross-file shared tail, `jmp short $+2`, `pushf` + "
        "`call far`, `push`/`push`/`retf`, a dispatched jump table, a data "
        "table, `owner.local`), and the LOUD half is what a size pass actually "
        "produces - a deleted `pop`, a cross-jumped epilogue that is not a "
        "twin, one overflow handler serving two depths. Nine of the seventeen "
        "fail against the walker as it was, and one of those nine is a LOUD "
        "row: the old walk skipped a routine whose every exit was a tail jmp, "
        "so it could not see that shape at all"),
]

# --------------------------------------------------------------------------
# full - the pre-merge gate. Everything above, plus these.
# --------------------------------------------------------------------------
FULL = [
    Row("buildmatrix", "full", py("tests/unit/t_buildmatrix.py"), 110.0,
        "the knob kernels and kern_small - every configuration `all` "
        "does not build, and so the only thing that keeps them assembling", builds=True),
    Row("bmshare", "full", py("tests/unit/t_bmshare.py"), 16.0,
        "...and that the three variables it builds them WITH change no byte. "
        "ICODIR/NOOVLCHK/NOKERNSIZE each take work out of a knob build - the "
        "shared packages, the source-only overlay gate, the size report's "
        "second assembly - and taking work out of a build is the change that "
        "goes wrong in silence. It builds one knob kernel both ways and "
        "compares the images, and it checks the exclusion the sharing rests "
        "on: SBDRAGOFF/SBRATE reach notepad's own nasm line, t_buildmatrix "
        "derives that pair from $(PKGSBDEF) rather than keeping a copy, and "
        "both ends of that derivation are asserted here", builds=True),
    Row("kernmods", "full", py("tests/unit/t_kernmods.py"), 7.0,
        "tools/kernsize.py's PER-MODULE pass still measures - the byte "
        "compare inside it worked and nothing ran it, so --bless returned 1 "
        "without writing while t_kernbudget went on advising it. Here and "
        "not in fast because it assembles the kernel twice",
        needs=("nasm",), serial=False),
    Row("ctoolchain", "full", py("tests/unit/t_ctoolchain.py"), 8.0,
        "the C toolchain still produces a package - the OTHER thing `all` "
        "does not build, and the one that had a `cc` capability with no row "
        "behind it while no C package assembled for two releases",
        needs=("cc",), serial=True, builds=True),
    Row("martyconc", "full", py("tests/martyconc.py"), 20.0,
        "TWO EMULATORS AT ONCE, and every way that used to go wrong. It is "
        "here rather than in soak because it gates the INSTRUMENT the whole "
        "marty tier runs on, and every failure it catches is SILENT: two "
        "instances sharing a floppy do not error - one boots the other's "
        "disk; two sharing a port do not error - the second attaches to the "
        "first's machine; and a second client on one used to HANG rather "
        "than be refused. It asserts separate ports, directories, disks and "
        "memories, a refusal that arrives in under a second and names the "
        "holder, and that reap() takes an orphan and leaves a live, owned "
        "instance alone. Runs three machines and boots two, so it is also "
        "the one row that would notice the isolation costing more than it "
        "saves",
        needs=("marty",), serial=True),
    Row("bootsmoke", "full", py("tests/bootsmoke.py"), 20.0,
        "does it still reach a desktop on both 1bpp adapters - the widest "
        "reach per second of any test here",
        needs=("marty",), serial=True),
    Row("smallboot", "full", py("tests/smallboot.py"), 110.0,
        "does KERN_SMALL still reach a desktop - buildmatrix assembles that "
        "build and nothing has ever booted it, which is how it has been "
        "DISCOVERED broken three times rather than reported broken. Here "
        "rather than soak because SPEC.md 39's VGA renderer is now gated out "
        "of it, and an %ifdef that takes one body too many assembles "
        "perfectly and dies at the first paint. It builds its own image "
        "(`make small`, into build/smallk/) because there is no capability "
        "to probe for and `all` never builds that kernel",
        needs=("marty",), serial=True, builds=True),
    Row("stk0water", "soak", py("tests/stk0water.py"), 300.0,
        "how deep TASK 0's stack has actually been (SPEC.md 15.1). That "
        "section says `redo the fill probe before lowering either` and the "
        "probe was a hand edit to kmain plus a hand read, so it had been run "
        "once - which is why `STK0_SIZE` sat at 4x a figure nobody had "
        "re-taken. This is it automated: fill everything below task 0's SAVED "
        "SP with 0xCC, drive the machine, read the deepest byte back. It "
        "reads 238 against 15.1's 246 (a heavier drive), and STK0_SIZE is 512 "
        "on both kernels now. Three things it had to get right and each was "
        "wrong first: the LIVE SP is a worker's, because SPEC.md 8.1.2 has "
        "ui_task block and an idle machine is 96.9% halted; the canary at the "
        "bottom must not be filled over, because SPEC.md 8.7 put slot 0 in "
        "sch_stkbase and sch_switch checks it on every switch - filling it "
        "reaches sch_stkdie and the only symptom is a pointer that will not "
        "move; and a menu released inside its pane SELECTS an item, which "
        "launched the About box and left the screen animating for ever. "
        "`soak` because it is a MEASUREMENT rather than an assertion - it "
        "prints the margin at five candidate sizes and fails nothing",
        needs=("marty",), serial=True),
    Row("small128", "full", py("tests/small128.py"), 110.0,
        "...and it reaches that desktop on a machine with 128KB IN IT. Every "
        "other MartyPC profile here is 640KB, so `MIN_RAM_KB` had been an "
        "ARITHMETIC claim since the day it was written - guard 5 compares two "
        "constants at assembly time and nothing had ever asked the result to "
        "run. The row above proves the build boots; this one proves the "
        "MACHINE does, which is a different question, because a purgeable "
        "claim that sizes itself off available heap has a floor of its own "
        "and the directory read-ahead is 64KB on a 640KB box. It is also "
        "docs/KERN-SMALL-CUT-PLAN.md 8.2's `cheapest unexamined lever`: it "
        "walks mem_tab on the machine and fails if ANY pinned claim stands on "
        "a bare desktop, because that is heap the machine never gets back and "
        "no assembler can see it - SPEC.md 54.0's association cache was "
        "holding 3,072 bytes of one and was found by accident. Reads 0 "
        "pinned, 18,432 purgeable, 40.5 KB usable. Builds its own image for "
        "smallboot's reason",
        needs=("marty",), serial=True, builds=True),
    Row("int0sweep", "soak", py("tests/int0sweep.py"), 240.0,
        "Does anything raise a DIVIDE ERROR? (SPEC.md 11.96) On an IBM "
        "5150/5160 ROM the INT 0 vector is a BIOS stub that writes 0FFh to "
        "the 8259 mask and IRETs, so ONE divide overflow anywhere is a dead "
        "machine - IMR=FF, the tick stopped, the CPU parked in "
        "sch_idle_body's hlt with IF=1 and even the ISR-paced pointer "
        "frozen. THE POINT IS THE ROM. Every other MartyPC row in this file "
        "runs GLaBIOS, whose INT 0 handler does not touch the PIC, so the "
        "identical fault there is a wrong clip index and the session "
        "carries on: wm_ttl_rect spending BX under wm_clip_occl locked the "
        "machine hard on an IBM ROM and passed assocopen and every other "
        "row on GLaBIOS. Worse, a machine naming an IBM romset SILENTLY "
        "RESOLVES to glabios_pc when the ROM file is absent, so the handful "
        "of rows that ask for one were not testing it either. Arms INT 0 "
        "across a broad UI session and reports where it fired. The declared "
        "240 is MEASURED (207-209s observed): it said 180, which was the "
        "figure from when the row could not run at all. soak enforces no "
        "budget, so this is a description rather than a limit - but a "
        "description that is wrong is what makes the next person distrust "
        "the column",
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
    Row("weavesmoke", "full", py("tests/weavesmoke.py"), 100.0,
        "WEAVE opens FORM.WAB and draws a window on both 1bpp GLaBIOS twins - "
        "the Weave family's ONE full-tier row, forever (WEAVE-SPEC 12.3), and "
        "the widest reach per second the family has: the .WAB association, "
        "the accept idiom, the bundle reader, the flow walk and the first "
        "paint all fail here. It asserts the drawn window's STRUCTURE and "
        "never a golden screenshot, for bootsmoke's reason. It BUILDS ITS OWN "
        "DISK, which `full` may do and `fast` may not - and that is why it "
        "needs `cc` as well as WEAVE-SPEC 12.3's `marty`: WEAVE is a C "
        "package, so a tree without SmallerC cannot run this row at all and "
        "should say so as a SKIP rather than as a failure. 75s is 45s "
        "MEASURED here - two boots, two Disk-window navigations and two "
        "package launches - taken up by the ~1.6x a boot costs on the "
        "slowest box this suite is written for (7.8s against 5.0s), with a "
        "little room for the package still growing. It is NOT 2x bootsmoke: "
        "the launch after the boot costs as much again as the boot, and it "
        "went 41s -> 45s when wdraw.inc's paint core took weave.o88 from "
        "21,076 bytes to 27,020",
        needs=("marty", "cc"), serial=True, timeout=300, builds=True),
]

# --------------------------------------------------------------------------
# soak - registered, discoverable, not in anybody's budget.
#
# Each row names the subsystem it is about, so `os88test.py soak -k <glob>`
# is how you run the ones your change could have broken. These are the deep
# single-subject gates; several are worth reading before touching their area.
# --------------------------------------------------------------------------
SOAK = [
    Row("weavevm", "soak", py("tests/weavevm.py"), 20.0,
        "WEAVE-SPEC 12.3: the SHIPPING apps/weave/wvm.inc run in a raw-QEMU "
        "BOOT SECTOR with SS != DS and no OS under it at all, diffed case by "
        "case against tools/weavesim.py's end states - the rcz80test / "
        "c64memtest shape, and the gate wave 3's whole interaction half is "
        "built on (13.1 gates it FIRST). It asks docs/TESTING.md's question "
        "differently from every other qemu row here: this is not QEMU instead "
        "of MartyPC for a machine feature, it is a boot sector with one "
        "%included file in it, so what the emulator supplies is an 8086, a "
        "serial port and isa-debug-exit and nothing about the machine is "
        "being asserted. Which is also why it asserts CORRECTNESS and never "
        "a time. Every case runs TWICE, at a 256-op budget and at a budget "
        "of ONE, because a core that kept state in a register across a slice "
        "boundary passes the first and fails the second; and the corpus "
        "carries negative controls the harness must FAIL, without which the "
        "comparison proves nothing. 20s is 1s MEASURED here (the guest runs "
        "in well under a second) plus the corpus generation and the nasm "
        "run, with room for the corpus growing",
        needs=("qemu", "nasm"), serial=True, timeout=300),
    Row("weavecanvas", "soak", py("tests/weavecanvas.py"), 20.0,
        "WEAVE-SPEC 12.1.3: the SHIPPING apps/weave/wspr.inc and "
        "apps/weave/wwork.inc - WEAVE.WSM's composer and frame loop - run in "
        "a raw-QEMU BOOT SECTOR with SS != DS and no OS under them, diffed "
        "case by case against the model's own canvas composer. It is the one "
        "differential in this family whose ORACLE HAD TO BE WRITTEN: every "
        "other row diffs against something that was already there, and "
        "6.10.2's composition had nothing - the model does not draw pixels "
        "and the canvas buffer is on no card, so a sprite composed a byte to "
        "the left or a dirty run a band too short is invisible in every "
        "screenshot this family takes. Four comparisons a case: the sprite "
        "records (the 1/16-px accumulators, the bounce mirrors, the score "
        "latch), the staging ring record for record, the DIRTY-BAND RUNS the "
        "last frame emitted - which is the 2-4 that 14 prices - and the "
        "composed buffer byte for byte. Negative controls the harness must "
        "FAIL, one wrong buffer and one wrong end state. 20s is 1s MEASURED "
        "plus the corpus generation and the nasm run",
        needs=("qemu", "nasm"), serial=True, timeout=300),
    Row("weavesession", "soak", py("tests/weavesession.py"), 150.0,
        "WEAVE-SPEC 12.3, 12.3.1: a scripted session driven through the "
        "SHIPPING package under MartyPC - type in a field, press a button, "
        "toggle a check, take a menu command, dismiss an alert - and every "
        "reading diffed against `weavesim --run` given the same events. It "
        "reads facts that are on the glass or in the kernel's own window "
        "table (a meter's fill in pixels, a check's glyph, whether an alert "
        "window exists) and never a transcript, because a transcript is a "
        "claim the program makes about itself and a -DWVHARNESS build would "
        "be a second implementation of the thing under test (12.3.1 says so "
        "at length). It is the only row that exercises the ring, the slice "
        "and the native surface END TO END - weavevm cannot reach any of "
        "them, having no runtime under it. 90s is 55s MEASURED here for one "
        "boot, one navigation, one launch and eleven gestures per adapter, "
        "MEASURED at 135s over two clean runs and 150s over one that lost a "
        "double-click to host load and spent its three navigation retries. "
        "It is not the 90s this row was declared at before it had ever been "
        "run, and a declared figure nobody has taken is the thing this "
        "registry's budgets exist to stop drifting",
        needs=("marty", "cc"), serial=True, timeout=360, builds=True),
    Row("weavegrid", "soak", py("tests/weavegrid.py"), 200.0,
        "WEAVE-SPEC 13.1's wave-4 gate: the <grid>, against the model and "
        "against itself. Three things no other row in this family can see. "
        "(1) Every visible BAND is read off the glass by 12.3.2's "
        "consistency rule and compared with weavesim's own band() - 6.9.1's "
        "pinned layout, 5.2.1's display conversion, the justification and "
        "the scroll origin, all at once. (2) The set of bands whose PIXELS "
        "changed across an edit must equal the set whose model text changed: "
        "5.5.1's per-row damage said as a fact about the glass, and a "
        "runtime that repaints the whole grid on every edit passes every "
        "value check and fails only this one (a 20-row page is 291 ms "
        "against one row's 14.5). (3) tests/tpdraw.py's identity for the "
        "grid - the pixels after an incremental edit against the pixels "
        "after a full re-compose of the same state, the re-compose forced "
        "with the arrow keys, which is what catches the XOR selection path "
        "and the band composer disagreeing about which cells the selection "
        "covers. It drives BOTH ways into the store, because they share no "
        "code: `Cider +1` is SHEET's own setCell() through the ring, a "
        "slice and CALLM, and then a formula is TYPED into an empty cell "
        "through os88line, 6.9.3's classification and 6.9.2's compiler into "
        "a 5.6 kind-6 pool slot. 200s is 157s MEASURED here over two "
        "adapters - two boots, two navigations, two launches and ~20 "
        "gestures - taken over three consecutive clean runs at 156.8, 157.4 "
        "and 156.7, with room for the demo growing",
        needs=("marty", "cc"), serial=True, timeout=480, builds=True),
    Row("weavegfx", "soak", py("tests/weavegfx.py"), 240.0,
        "WEAVE-SPEC 12.3's pixels-vs-model row, zgfx's shape: every other "
        "gate in this family reads a number or a structure, and none of them "
        "can see a component drawn at the wrong row, a control that draws "
        "nothing at all, or a card whose ink runs outside the content box - "
        "which are precisely a widget library's failure modes (12.4). It is "
        "NOT a golden screenshot, for bootsmoke's reason: it compares the "
        "machine's picture against `weavesim --render`, the oracle 12.1 "
        "makes every differential in this family diff against. Three "
        "assertions per card and two cards - FORM is the widget zoo and "
        "SHEET is the band composer, which draws through GFX_BLIT1 rather "
        "than FONT_RUN - on both 1bpp adapters, because grey rounds to "
        "black there and a drawing change is not done until it has been "
        "looked at on one. The ink-presence half is what makes the text "
        "half honest: an unlearned glyph reads '?' and is skipped, so a "
        "component that drew nothing would otherwise pass a comparison made "
        "entirely of question marks. 240s is 122s MEASURED CLEAN over three "
        "consecutive runs (121, 122) and 190s on the third, which spent "
        "weavesmoke's three navigation retries and then failed - FOUR "
        "sessions is four double-clicks, so this row carries twice "
        "weavesession's exposure to the one thing that flakes in this "
        "family: a double-click whose two presses straddle the kernel's "
        "9-tick window is seen as two FIRST clicks, and on a loaded host "
        "that happens. The retry is weavesmoke's and is not loosened here - "
        "a gate that hid it would hide a host that had really got slower",
        needs=("marty", "cc"), serial=True, timeout=600, builds=True),
    Row("weaveprev", "soak", py("tests/weaveprev.py"), 320.0,
        "WEAVE-SPEC 1.7.1 and 12.3: LOOM's PREVIEW PANE against "
        "`weavesim --render --preview`. Wave 7 draws the pane with WEAVE's "
        "own flow walk and WEAVE's own component painter, compiled a second "
        "time into LOOM.WPV - a second RESIDENT segment (1.2.4) - and "
        "NOTHING ELSE IN THIS FAMILY ENTERS THAT MODULE AT ALL: weavegfx "
        "reads the runtime's window and every assertion it makes would pass "
        "with the pane blank. Because the two images run the same TEXT "
        "(apps/weave/wflow.c and apps/weave/wpaint.c are #included rather "
        "than reimplemented, 1.2's 'never a second copy'), a wrong picture "
        "here is the SEAM or the SEGMENT and never the painter - the pane "
        "rect arriving wrong, the module's .bss not zeroed, the caller's DS "
        "not banked, a stale module believed. Those are exactly the failures "
        "a second segment adds and an overlay does not. weavegfx's three "
        "assertions, aimed at the pane; all THREE demo projects, because "
        "SHEET has a <grid> and PONG a <canvas> and 1.7.1's rule is that a "
        "Preview draws those as their frame - which the model was taught in "
        "one flag rather than the test being taught to ignore two "
        "components. Both 1bpp adapters - six sessions, 180 checks. 260s is "
        "239s MEASURED over three consecutive runs (238.7 inside the tier, "
        "238.5 and 238.6 standalone) with a margin for the one thing that "
        "flakes in this family, a double-click whose two presses straddle "
        "the kernel's 9-tick window; the retry is weavesmoke's and is not "
        "loosened here",
        needs=("marty", "cc"), serial=True, timeout=600, builds=True),
    Row("weaveone", "soak", py("tests/weaveone.py"), 90.0,
        "WEAVE-SPEC 1.4's 256KB machine, ASSERTED: the family's floor board "
        "holds exactly ONE Weave app, and the second launch is refused while "
        "the first goes on running. It is the one row in this family about "
        "MEMORY rather than about a picture or a number, and the arithmetic "
        "it checks is the one WEAVE-SPEC 1.4 states and nothing else "
        "exercised - wave 5 moved it by one claim and wave 7 found the "
        "document naming the wrong refusal: the second launch never reaches "
        "WEAVE, because a package region is claimed by the KERNEL before the "
        "package runs (SPEC.md 20.1, 21) and WEAVE's is 60,320 bytes, so the "
        "loader answers LD_ENOMEM and the Finder says `Out of memory`. What "
        "is asserted is that byte and not the toast drawn from it, which is "
        "a ~3s transient no polling rate worth having catches; plus that the "
        "first app is STILL THERE, which is kernel/loader.inc's own opening "
        "promise and the thing a refusal that took the running app down "
        "with it would break. MartyPC on a GLaBIOS 256KB machine, because a "
        "machine wanting IBM's ROM cannot boot in this tree; `make "
        "xt-weave-256` is the same question on 86Box and is manual evidence "
        "only (docs/TESTING.md). 90s is 46s measured over three consecutive "
        "runs (46.1 inside the tier, 46.0 and 45.9 standalone)",
        needs=("marty", "cc"), serial=True, timeout=300, builds=True),
    Row("weavegame", "soak", py("tests/weavegame.py"), 50.0,
        "WEAVE-SPEC 6.10, 12.3, 14: PONG.WAB under MartyPC, and it asks "
        "wirefps's and wireflick's two questions of a sprite canvas "
        "(SPEC.md 78.9). HOW MANY GFX CALLS A FRAME, read out of WEAVE.WSM's "
        "own frames and blits counters - the only honest way to price a "
        "redraw here (CLAUDE.md: a redraw costs what it CALLS), and 14 "
        "prices a two-sprite frame at 2-4. WHAT THE GLASS SHOWED between the "
        "erase and the draw, sampled once per DISPLAYED frame the way "
        "wireflick does, because m.flicker() waits for a screen to settle "
        "and a running game never does again. AND INPUT OVERRUN, which is "
        "the one of CLAUDE.md's three invisible defects that can be turned "
        "into a number at all: 6.10.6's staging ring counts every record it "
        "could not take, and that counter is asserted at zero. AND THAT "
        "ONTICK FIRED MORE THAN ONCE: PONG's computer paddle is steered from "
        "ontick, and the row reads its y out of the canvas claim before and "
        "after the frames - the module shipped waves 5-7 delivering ONE "
        "ontick per start() (6.10.6) and no counter showed it. No threshold "
        "on TIME - wirefps's rule, that a number which fails a build when a "
        "harness gets slower teaches nobody anything - so the fps is printed "
        "and the FIELD RUN (docs/FIELD-MACHINES.md, WEAVE-PLAN 4.2) is what "
        "turns it into a claim. 50s is 34s MEASURED plus room for the one "
        "navigation retry weavesmoke's own flake can cost",
        needs=("marty", "cc"), serial=True, timeout=300),
    Row("weavepack", "soak", py("tests/weavepack.py"), 1500.0,
        "WEAVE-SPEC 11.1's gate and the one wave 6 closes on: LOOM packs "
        "every demo and every template ON THE MACHINE, the guest's floppy is "
        "flushed to the host, and each .WAB is read back out of it by an "
        "independent FAT12 reader and compared whole. That last part is what "
        "makes the comparison mean anything - without it a scripted session "
        "makes a program save a file and then has to ask the program whether "
        "it worked, which cannot catch the case where the writer and the "
        "reader agree on the same wrong thing. tests/unit/t_lmpack.py packs "
        "the same seven with the HOST cc in four seconds and is the dev "
        "loop; the difference between the two is one word wide (`int` is 32 "
        "bits there and 16 here), so that row proves the logic and this one "
        "proves the arithmetic. Needs `cc` because LOOM is a C package, and "
        "`marty` for the boot. 1,500s MEASURED, and it is eleven LAUNCHES rather "
        "than one session: each project is its own instance (WEAVE-SPEC 1.4) "
        "and they cannot all be open at once, so every one costs a package "
        "load - 55KB of LOOM plus 43KB of LOOM.OVL off an emulated floppy - "
        "and that read is the whole of the time. It is the price of asking "
        "the question on the target rather than on the host",
        needs=("marty", "cc"), serial=True, timeout=3000, builds=True),
    Row("weavefuzz", "soak", py("tests/weavefuzz.py"), 75.0,
        "a thousand DAMAGED projects through both packers, asking the two "
        "questions a fixture cannot: did they agree about whether it is a "
        "program, and when both said yes are the bytes identical "
        "(WEAVE-SPEC 11.1). Fixed seeds, so a find on Tuesday is still there "
        "on Wednesday. Message TEXT is reported and not asserted, and the "
        "row's own header says why - weavesim scans a whole element before "
        "analysing any of it and LOOM analyses as it goes, so a DOUBLY "
        "broken document makes them name different faults; the single-fault "
        "documents an author types are what tests/weave/packerr/ holds them "
        "to. Measured when it was written: 0 verdict disagreements, 0 byte "
        "disagreements, 93 differing messages in 1,000",
        needs=()),
    Row("weavelat", "soak", py("tests/weavelat.py"), 120.0,
        "SPEC.md 7.3's click-to-action bar with a WEAVE FORM as the load "
        "(WEAVE-SPEC 12.4), measured the way tests/uilat.py measures it - "
        "two memory breakpoints and the cycle counter, because os88mouse's "
        "injection path has a ~0.51 s floor and cannot see 40 ms. The "
        "question it asks is the one 4.10's slice design could get wrong: a "
        "handler runs in ONWAKE without the gfx lock, and a runtime that "
        "took the lock for the slice rather than for the flush would hold it "
        "for 51-154 ms against a 37-70 ms bar. That is invisible in every "
        "functional test in this family and it is exactly what this row is "
        "for. 120s is uilat's own figure: the same shape, one more launch",
        needs=("marty", "cc"), serial=True, timeout=480, builds=True),
    Row("assocglyph", "soak", py("tests/assocglyph.py"), 62.3,
        "A DECLARED extension's icon is right from a COLD mount (SPEC.md"
        "54.7.3).",
        needs=("marty",), serial=True),
    Row("assocwake", "soak", py("tests/assocwake.py"), 95.0,
        "SPEC.md 54.10: a document launch draws the PROGRAM'S WINDOW first, "
        "and only then reads the document. The instrument is a breakpoint on "
        "assoc_handover - the guest cannot have read the file yet at that "
        "instruction - and the pixels inside the new window's frame are what "
        "says wm_show already drew it. The 'Decoding GIF' toast (42.14) and "
        "the picture arriving are what stop it passing vacuously.",
        needs=("marty",), serial=True),
    Row("assocopen", "soak", py("tests/assocopen.py"), 60.0,
        "SPEC.md 22.13.2: opening a DOCUMENT draws no pixel of the Disk "
        "window. The instrument is a breakpoint on fm_repaint, and the "
        "FOLDER open beside it is the control that says the breakpoint "
        "fires at all.",
        needs=("marty",), serial=True),
    Row("fmcommit", "soak", py("tests/fmcommit.py"), 62.0,
        "SPEC.md 22.13.3: a committing keystroke redraws the Disk window and "
        "a REFUSED character does not. fm_onkey banks fm_editkey's answer "
        "across the modal-dialog test, because `cmp word [x], 0` clears the "
        "carry and left that `jc` dead - so Delete removed a file and left "
        "its row on the glass. The instrument is a breakpoint on fm_repaint "
        "(assocopen's), and the refused comma is the leg that says the fix "
        "did not buy the repaint back with one nobody owes.",
        needs=("marty",), serial=True),
    Row("multiseg", "soak", py("tests/multiseg.py", "1440"), 60.0,
        "SPEC.md 20.12: a package carries its parts in its OWN FILE and loads "
        "them ITSELF. The kernel parses none of it - all it learns is flags "
        "bit 2, that the file is longer than the image on purpose, and it "
        "hands the entry proc the name of the file it came out of; "
        "apps/os88parts.inc is the rest, and it is package code. THREE "
        "INDEPENDENT CHECKS PER PART, because a segment number proves a claim "
        "was made and not that it was filled: the signature the primary reads "
        "out of the part's own bytes, a far call to <part>:0 answering a "
        "value only that module computes, and the module summing its data "
        "area with a ROTATING add against the figure the assembler computed "
        "over the same generated bytes - a plain sum would pass on a "
        "transposition, which is what a misaligned read actually produces. "
        "SEVEN PARTS: three filed modules, a required 8KB scratch part that "
        "costs no disk at all, a 600KB OPTIONAL one that must be REFUSED, "
        "an OP_XMS one that on every machine here comes back as an "
        "ordinary conventional claim - the FALLBACK, and what makes OP_XMS a "
        "hint rather than a mode (tests/msegxms.py is the other half, on "
        "QEMU, where there is a store) - and an OP_LAZY one that must NOT be "
        "here at all until something fetches it (tests/mseglazy.py drives "
        "that cycle; this row only says it did not happen by itself). The "
        "verdict is the window "
        "TITLE, read out of the package's segment rather than off the glass. "
        "MEASURED with the kernel's bit-2 exception disabled: ld_status 2, "
        "`Bad package`. Needs `make mseg`.",
        needs=("marty",), serial=True),
    Row("mseg360", "soak", py("tests/multiseg.py", "360"), 60.0,
        "...and the same package off a 360KB disk, where it is NOT a "
        "duplicate. A part begins on a 512-byte boundary in the FILE and "
        "OSAPI_FILE_READ_AT will only BEGIN a read on a CLUSTER boundary - "
        "512 bytes at 1.44MB, 1KB here, and up to 32KB on a volume neither "
        "of these is, which is why the file cannot simply be laid out to "
        "suit. So the read starts BELOW the run and op_claim's head slack is "
        "what puts each part's segment where it belongs (SPEC.md 20.12.2). "
        "MSEG's image is padded to an ODD number of sectors on purpose so "
        "the case exists at all - unpadded its first part landed at 1,024, "
        "aligned on both geometries, and this row passed without ever "
        "running the arithmetic it is for; the slack is asserted (512 here, "
        "0 at 1.44MB) so that cannot recur. MEASURED with the slack not "
        "added back: this row answers `MSEG 0/6 BAD` and the 1.44MB row "
        "still answers `MSEG 6/6 OK`, which is why neither alone is the "
        "gate. Needs `make mseg`.",
        needs=("marty",), serial=True),
    Row("msegnomem", "soak", py("tests/msegnomem.py"), 600.0,
        "SPEC.md 20.12.3: a package that cannot fit is refused BEFORE IT "
        "READS ANYTHING, and this row measures that rather than asserting it. "
        "MSEGBIG is MSEG's twin - the same three filed parts and one more, a "
        "REQUIRED 640KB scratch part, which is the whole of the biggest "
        "machine here (512 was the first figure and a 640KB XT GRANTED it, so "
        "the row passed on a mechanism it had never run). The instrument is "
        "dsk_dbg_sec, the kernel's own count of SECTORS transferred, so this "
        "row BUILDS A DISKCNT KERNEL and puts build/ back afterwards. "
        "MEASURED: the refusal moves 9 sectors and the successful launch "
        "21, and the margin is op_read's 13-sector carved run - with the "
        "odds against it, because the refusal goes first on a cold volume "
        "and the success second on a warm one. SECTORS AND NOT CALLS: this "
        "row asserted `at most two int 13h calls` until wave 4 padded both "
        "images to five sectors, which put MSEG's image across a cylinder "
        "boundary, and the driver split one run into two for a launch that "
        "read no extra byte - the refusal then cost the same 3 calls as the "
        "success while moving 12 sectors against 21, and wave 5's seventh "
        "sector moved them again to 1 and 3. The count is a fact about where "
        "the file sits on the disk. The heap is BYTE-FOR-BYTE untouched "
        "across the refusal, which "
        "is stronger than the kernel-side design could manage: it tried the "
        "claim and mem_claim sheds purgeable caches before refusing (SPEC.md "
        "50.6.2), where op_load asks OSAPI_MEM_AVAIL and a question costs "
        "nothing. VERIFIED by A/B - made to try the claim instead of asking, "
        "the table goes 4 claims to 2 and this row names it. Needs `make "
        "mseg`.",
        needs=("marty", "nasm"), serial=True, builds=True),
    Row("c64part", "soak", py("tests/c64part.py"), 120.0,
        "THE FIRST REAL CONSUMER of the parts standard (SPEC.md 20.12, and "
        "C64-SPEC 1.4): C64.ROM - 20,480 bytes of KERNAL, BASIC and "
        "character generator - was a SIDECAR a file copy could separate from "
        "the program it is useless without, and it is part 0 of C64.O88 now. "
        "The port carried a whole halted-machine state to say so when it went "
        "missing: a permanent status row naming the file, a four-line notice "
        "with its own expose repair, three greyed menu items and a SECOND "
        "host-test process. All deleted, because a greying may not outlive "
        "its reason (SPEC.md 47). FIVE ASSERTIONS - C64.ROM is not in the "
        "folder, read out of the guest's own listing; the package declares "
        "parts and its image is smaller than its file; it launched; "
        "os88_part_seg(0) is the segment the C put in c64_m.romseg, which is "
        "the standard's answer and the package's use of it; and five 16-byte "
        "windows of the ROM in the guest equal build/c64-rom/C64.ROM - "
        "including THE LAST SIXTEEN BYTES OF THE PART, because a carve one "
        "sector short reads perfectly at the front. It deliberately does not "
        "assert that the KERNAL BOOTS: measured by A/B against the "
        "unconverted package, the 6510 on this branch runs and never writes a "
        "byte of its own RAM, which is not this wave's to fix and would be a "
        "row failing for a reason it does not name. VERIFIED TO FAIL - a "
        "package truncated by one sector gives ld_status 4 and no window, "
        "because op_read now refuses a run that arrives short. Needs `make "
        "c64disk`, so it needs the C toolchain.",
        needs=("marty", "cc"), serial=True, builds=True),
    Row("mseglazy", "soak", py("tests/mseglazy.py"), 150.0,
        "SPEC.md 20.12.4: an OP_LAZY part is NOT READ AT LOAD and can be "
        "given back. That is the first half of goal 3 - `load only some "
        "minimal amount` - where msegnomem is the second, and MSEG's part 6 "
        "is the biggest of its five modules on purpose, because what lazy "
        "buys is measured in the sectors the launch did not move. A KEY "
        "fetches it and not the entry proc: a fetch inside the entry happens "
        "during the launch, so its sectors would be indistinguishable from "
        "the carve's. FIVE ASSERTIONS - it is absent after the launch and "
        "MSEG's own verdict agrees that is CORRECT (it checks part 6 against "
        "what it asked for, so presence would be the failure); the carve, "
        "read out of the guest as [op_first]/[op_secs], ENDS BEFORE part 6's "
        "first sector, which is the structural half and the one that cannot "
        "be faked; the key moves at least the part's six sectors "
        "(dsk_dbg_sec, so this row builds a DISKCNT kernel) and the three "
        "module checks then pass on it; a second key gives it back with the "
        "claim table BYTE-FOR-BYTE what it was, which is what makes lazy a "
        "saving rather than a postponement; and a third fetches it again. "
        "VERIFIED TO FAIL, and how it failed is the argument for assertion "
        "2: with op_size made to size a lazy row like any other the carve "
        "runs to sector 25 instead of 19 and reads the part at load - and "
        "assertion 1 does NOT notice, because op_seg answers a lazy row out "
        "of the row itself and that is still 0. Presence is what the package "
        "was told; the carve is what the disk did. Needs `make mseg`.",
        needs=("marty", "nasm"), serial=True, builds=True),
    Row("msegxms", "soak", py("tests/msegxms.py"), 150.0,
        "SPEC.md 20.12.4: an OP_XMS part really goes ABOVE 1MB. Every MartyPC "
        "row proves the FALLBACK - an 8088 has nothing up there, so the part "
        "comes back as an ordinary conventional claim, which is what makes "
        "OP_XMS a HINT and not a mode - and this is the other half. WHY QEMU: "
        "docs/TESTING.md's closed list, entry 6's shape - MartyPC cannot host "
        "extended memory at all, so there is no `prefer MartyPC` to weigh; it "
        "borrows tests/xmcheck.py's boot and block-table reader for the same "
        "reason. FOUR ASSERTIONS and the last is what makes the first three "
        "mean anything: op_seg answers ZERO and op_lin a non-zero linear "
        "base; MSEG says `MSEG 6/6 OK`, which means it brought the part back "
        "DOWN through OSAPI_XMEM_COPY and its rotating sum matched; its own "
        "ms_xwhere says 'X' and not 'C', because the bytes are the same "
        "bytes either way and the sum alone cannot tell the two paths apart; "
        "and the block is owned by the INSTANCE rather than XM_OWN_KERN - "
        "xm_alloc attributes through inst_caller, and the loader already "
        "brackets the entry call with the instance's stamp (SPEC.md 41.5.1), "
        "which is what lets a package claim for itself with NO kernel change. "
        "That last one is silent otherwise: a block nobody frees looks "
        "exactly like a block nobody claimed. VERIFIED by A/B - with op_load "
        "made to believe there is no store, the title still says `MSEG 6/6 "
        "OK` and three of the four assertions fire. It CAUGHT TWO DEFECTS on "
        "its first green run: op_xload walked op_xlin as its copy cursor and "
        "put it back by subtracting the BLOCK's size rather than the SPAN's, "
        "so every part came out 512 bytes low and the failure path freed an "
        "address that was never claimed; and MSEG itself passed the copy "
        "direction in DI, which is also its part-loop counter, so the entry "
        "proc never returned. Needs `make mseg`.",
        needs=("qemu",), serial=True, builds=True),
    Row("pkgbig", "soak", py("tests/pkgbig.py"), 60.0,
        "SPEC.md 19.1's package-size rule and the loader half that makes it "
        "safe. APP_MAX_SIZE bounds the primary SEGMENT's image+bss; it "
        "stopped bounding the FILE when a package could carry parts, so the "
        "mount types a *.O88 up to PKG_FILE_HI (1MB) and ld_run_body step 1 "
        "tests the staged size's HIGH word before it trusts the low one. "
        "LIFTING THE MOUNT'S RULE ALONE IS A DEFECT and this is what catches "
        "it: a 70KB file's low word is 4,608, a plausible small package, so "
        "without the guard the loader sizes a region from a wrapped length "
        "and answers `Bad package` about a file whose only fault is its size "
        "(SPEC.md 21.4's hazard). TWO FILES AND THE PAIR IS THE EXPERIMENT - "
        "70,144 bytes must type 1 and be refused LD_EBIG, 1,048,576 must "
        "type 0 and be refused LD_EBAD; BIGPKG alone is also what a rule "
        "that types everything looks like, and HUGE alone is also what the "
        "old `high word == 0` rule looks like. The instrument is [ld_status] "
        "read out of the guest, so it answers for all three adapters out of "
        "one run. Needs `make pkgbig`, which uses os88disk.py --raw: the "
        "fixtures are *.O88 files that are deliberately not packages, and "
        "validate_o88 exists to make those unbuildable. 40s measured.",
        needs=("marty",), serial=True),
    Row("pkgfence", "soak", py("tests/pkgfence.py"), 60.0,
        "SPEC.md 21 steps 4 and 6's WRITE BOUND: ld_check_hdr's `image + bss` "
        "fence. Both operands are separately bounded at APP_MAX_SIZE, so "
        "their sum reaches 0x1E000 - SEVENTEEN BITS - and the compare that "
        "used to stand there read a WRAPPED value, with the comment on the "
        "line stating the defect as its own proof (`img+bss <= 0x1E000: no "
        "wrap`). The repair is `add dx, ax / jc .toobig` and it shipped in "
        "size pass 3 with NO ROW BEHIND IT: nothing else in the tree can see "
        "this, because every other gate loads a well-formed package and "
        "os88pkg.py refuses to build a malformed .O88 at all - which is the "
        "point, since the input this is about comes off a disk (SPEC.md 19). "
        "TWO FILES AND THE PAIR IS THE EXPERIMENT - BSSWRAP.O88 is image = "
        "bss = 0xF000, whose sum wraps to 0xE000, BELOW the bound, for a 56KB "
        "claim and 4KB past it; BSSWORST.O88 is image = 0xF000, bss = 0x1001, "
        "whose sum wraps to 1, for a ONE KILOBYTE claim and 60,416 bytes "
        "written through whatever mem_claim_hi placed under it, which is a "
        "resident package's code because it places top-down. The first alone "
        "under-states the fault fifteenfold and the second alone looks "
        "contrived. Both must answer LD_EBIG. A regression does NOT answer "
        "cleanly - it corrupts the guest's heap and this row times out, which "
        "is inherent: the write the fence bounds has already happened by the "
        "time anything could report it. The instrument is [ld_status] read "
        "out of the guest, so it answers for all three adapters out of one "
        "run. Shares build/pkgbig.img with the pkgbig row - `make pkgbig`.",
        needs=("marty",), serial=True),
    Row("clipkeep", "soak", py("tests/clipkeep.py"), 300.0,
        "SPEC.md 11.96.18: a wholly covered window keeps its raise cache when"
        "it arms a clip, and a partly covered one still loses it.",
        needs=("marty",), serial=True),
    Row("fcpcopy", "soak", py("tests/fcpcopy.py"), 240.0,
        "SPEC.md 22.3-22.5: Cut/Copy/Paste actually moves a file AND a folder "
        "tree. Nothing exercised kernel/filecp.inc at all until this row - a "
        "whole-file pass over the copy engine could be green on assembly, "
        "stkbalance, ovlchk and every size guard while leaving a machine that "
        "cannot copy a file. The load-bearing assertion is the THIRD one: "
        "os88disk --verify walks the volume the engine left behind, because a "
        "stranded cluster or a cross-linked chain looks perfectly fine in the "
        "guest's own listing, which is drawn from the structures that are "
        "wrong. Runs on the 1.44MB disk: the 360KB one is 354 of 354 clusters "
        "in use after one paste, so the folder copy correctly refuses there "
        "with FERR_FULL and the row would be measuring the geometry. "
        "builds=True because the script can shell out to `make small` when it "
        "is pointed at kern_small (the fcpsmall row below), and the runner "
        "gives a building row the tree to itself.",
        needs=("marty",), serial=True, builds=True),
    Row("fcpsmall", "soak",
        ["env", "OS88_DEFINES=KERN_SMALL", "OS88_BUILD=build/smallk",
         "OS88_SYSIMG=build/small.img"] + py("tests/fcpcopy.py"), 300.0,
        "...and the SAME drive against kern_small, where Cut/Copy/Paste is an "
        "on-demand module (SPEC.md 22.3, docs/KERN-SMALL-MODULE-SPLIT.md 9.2) "
        "rather than resident code. It is a different engine to reach: every "
        "call the image makes to the kernel is a far one through an xf_ entry, "
        "the shared register epilogues are copies inside the image because a "
        "`jmp kretc_cx` would return through a near `ret` against a far frame, "
        "and the whole thing is read off the disk by mod_need and given back "
        "at the end of each operation. NONE of that is exercised by the row "
        "above, which runs the resident build - and the first time this one "
        "ran it caught FILECP.DRV missing from the floppy entirely, with every "
        "build step green and the machine booting. It builds its own image "
        "(`make small`) for smallboot's reason.",
        needs=("marty",), serial=True, builds=True),
    Row("cppromise", "soak", py("tests/cppromise.py"), 300.0,
        "SPEC.md 31.12: the Control Panel promises per PAGE, and the clock"
        "page is the one that cannot.",
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
    Row("fishfit", "soak", py("tests/fishfit.py"), 45.0,
        "does the most expensive sea the generator can roll still fit ONE "
        "TICK? (SPEC.md 79.5.8). Sea life is the one saver mode that ever "
        "cost more than the 54.93 ms a task_sleep(1) parks for, and what that "
        "cost is not a slow mode: 18.2 / (floor(work / 54.93) + 1) is 18.2 a "
        "millisecond under and 9.1 a millisecond over, with nothing between. "
        "saverate is the KERNEL half of that - a mode asleep while it is "
        "behind - and cannot catch this half, because a sea that legitimately "
        "costs 70 ms is slow and busy and passes it. The assertion is the "
        "PASS, in guest cycles between two sv_step entries, with all four "
        "swimmers forced to the larger size: one roll in sixteen, so a test "
        "that waited for one would usually measure something cheaper.",
        needs=("marty",), serial=True),
    Row("fishedge", "soak", py("tests/fishedge.py"), 150.0,
        "does sea life leave the reserved strip at the right edge DARK on "
        "Hercules? (SPEC.md 79.5.10). SPEC.md 79.5.9 places the field's "
        "column-0 shimmer in 86Box's plain Hercules renderer rather than in "
        "this kernel - the mark is one row DOWN from the right edge, which no "
        "write here can reach - and 79.5.10 is the product answer to it: an "
        "unlit column reads as the edge of the monitor and a shimmering one "
        "does not, so the mode reserves SV_HEDGE pixels and never lights "
        "them. THE ASSERTION IS AN A/B AND HAS TO BE: a sea whose swimmers "
        "never went near the edge leaves the strip dark too and reads exactly "
        "like a pass, so the same forced sweep runs twice - once as the "
        "driver armed it, once with [sv_hlim] poked to 0, which is the state "
        "every other adapter is in - and the strip must be clean under the "
        "first and dirty under the second. The CGA leg asserts the strip is "
        "NOT armed there, which is what keeps gfx_blit1's own right clip the "
        "only cut on the two adapters with no artifact to hide.",
        needs=("marty",), serial=True),
    Row("saverate", "soak", py("tests/saverate.py"), 260.0,
        "is a saver mode ASLEEP while it is behind? (SPEC.md 79.5.7, 8.1.2.4). "
        "ui_task's task_sleep(1) quantises a deadline polled once a pass to "
        "whole ticks, so a mode whose pass runs a millisecond into the next "
        "one drops to the divisor below rather than to its cost - sea life "
        "measured 12.0 fps swinging 9.2-17.8 with 37.2% of the machine "
        "HALTED. The assertion is NOT a frame rate, which cannot tell an "
        "expensive sea from a quantised one: it is slow AND halted, which no "
        "content can produce. The other three modes are the control and are "
        "counted off [sv_due], which cannot see a re-anchored mode - so they "
        "catch a mode that stopped drawing and not one that was quantised.",
        needs=("marty",), serial=True, alone=True),
    Row("deskbench", "soak", py("tests/deskbench.py"), 330.0,
        "THE STANDARD BUSY DESKTOP, priced: what a full-screen redraw, a "
        "window move and a raise cost with four windows open (PERFORMANCE.md "
        "Part 3). A measurement, not a gate - it asserts its own SCENE and "
        "prints numbers. `--all` runs one per adapter.",
        needs=("marty",), serial=True, alone=True),
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
        needs=("marty", "nasm"), serial=True, builds=True),
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
    Row("splashspin", "soak", py("tests/splashspin.py"), 150.0,
        "Does the logo turn on the WALL CLOCK? (SPEC.md 15.3.6) The composed "
        "angle checked against the guest's own BIOS tick on every frame it "
        "changes, and the rate compared either side of the notch rate "
        "changing - a stopped tick parks the logo at one angle and the "
        "machine still boots, so no screendump in this tree would notice",
        needs=("marty",), serial=True),
    Row("bootfloor-ab", "soak", py("tests/bootfloor.py"), 300.0,
        "SPEC.md 2.7.1's floor, both sides, both kernels: RAMKB at the floor "
        "must reach a DESKTOP - everything the bound is computed from is "
        "downstream of the refusal - and one KB under it must print RAM. "
        "kern_small is the half that matters: guard 5 has asserted it boots "
        "on 128KB since the split and stage 1 refused it at 129 until 2.7.1, "
        "which no host-side row could have noticed",
        needs=("marty",), serial=True, builds=True),
    Row("dljunk", "soak", py("tests/dljunk.py"), 150.0,
        "SPEC.md 2.9.11's DL check, both ways: a BIOS that never set DL left "
        "0x61 in it and every int 13h named a unit that is not there, which "
        "is `Disk error` since the first commit (docs/FIELD-NOTES.md 36). "
        "DLJUNK=0x61 must reach a desktop and DLJUNK=1 - a legal unit the "
        "check must LEAVE ALONE, on a machine whose drive 1 is empty - must "
        "not: without the second half a sector that ignored DL outright "
        "would pass the first",
        needs=("marty", "nasm"), serial=True, timeout=420, builds=True),
    Row("fatwpin", "soak", py("tests/fatwpin.py"), 420.0,
        "Is the kernel's own FAT window somebody's PIN, and only ever one "
        "volume's? (SPEC.md 18.8.3) FAT_SEG used to be a fallback owned by "
        "nobody, so a machine whose volumes all won heap claims reserved "
        "4,608 bytes it never touched and bought the same window again out "
        "of the arena. Asserts the saving (no MEM_K_FATW record at all with "
        "only A: mounted), the safety property (the mounted volume always "
        "has a home - a homeless one is pointed at FAT_SEG and mounts into "
        "it WITHOUT demoting the holder, which 18.8.2's signature cannot "
        "catch because two os8088 floppies of one geometry have identical "
        "boot sectors), FATWNONE=1's ping-pong, where the pin is the "
        "only home there is and must follow the mount, and 18.8.4's SHED - "
        "tests/heapfrag fills the heap at the ordinary rank, which outranks "
        "MEM_P_FATW's MED, and the LIVE window must come back as the pin "
        "with [dsk_fatw0] invalidated, not a pointer into freed memory",
        needs=("marty", "nasm"), serial=True, timeout=900, builds=True),
    Row("vgadirty", "soak", py("tests/vgadirty.py"), 120.0,
        "Does vid_setmode leave the VGA framebuffer black whatever the ROM "
        "did? (SPEC.md 39.23) Builds a VGADIRTY=1 kernel, which fills A0000 "
        "in the one window a machine cannot - after the ROM's mode set and "
        "before ours - and asserts the loading screen comes up on black",
        needs=("qemu", "nasm"), serial=True, timeout=300, builds=True),
    Row("ps2mouse", "full", py("tests/ps2mouse.py"), 40.0,
        "Does the PS/2 mouse reach the pointer, and does the KEYBOARD survive "
        "the handshake? (SPEC.md 9.9) -serial none, so no UART probes present "
        "and the aux port is the only pointing device: mou_p2st 9, port 04, "
        "line FF, ptr 1, and the pointer landing on the EXACT requested pixel, "
        "which is the statement about the sign handling and 9.9.3's Y "
        "inversion that nothing else here makes. Then six keys must advance "
        "the BIOS buffer by twelve bytes, because both halves of the probe are "
        "a chance to take a byte from int 09h. QEMU by name on CLAUDE.md's "
        "closed list - MartyPC is an 8088 and has no 8042 to test",
        needs=("qemu", "nasm"), serial=True, timeout=420, builds=True),
    Row("vmmouse", "full", py("tests/vmmouse.py"), 45.0,
        "The VMware absolute pointer (SPEC.md 9.11), the browser's grabless "
        "mouse - and the one CI gate a browser-only feature gets. QEMU's pc "
        "machine carries a vmport and a vmmouse by default, so VMMOUSE.DRV's "
        "backdoor probe succeeds here exactly as it does under v86. It boots "
        "build/vmmouse.img and NOT os8088.img: the driver is DRVC_OVL with a "
        "drv_tab row and is NOT WANTED BY DEFAULT, so a stock disk never "
        "reads it (SPEC.md 51.3) - `make vmmousetest` builds one whose "
        "SYSTEM.CFG has bit 5 set, ether360.img's shape. vmport ON and "
        "-serial none, so the backdoor is the only pointing device. Asserts "
        "cpu_tier 2 (vmm_boot_x refuses to READ the image below it, the last "
        "CPU gate in the tree), vmm_on 1, mou_port 6 (MOU_VMROW, so "
        "mou_lockon retired the serial rows), then absolute positions "
        "injected through vmmouse landing within a few px - the sign and axis "
        "handling that a boot-state read cannot see - and a drag through a "
        "menu, which is what proves the task_yield service point. QEMU by "
        "name on CLAUDE.md's closed list - MartyPC has no backdoor",
        needs=("qemu", "nasm"), serial=True, timeout=420, builds=True),
    Row("heapmap", "soak", py("tests/heapmap.py"), 120.0,
        "What does the claim heap look like when the boot is over? (SPEC.md "
        "50, 66) Every driver attached at once on a machine WITH memory above "
        "1MB, sampled from instruction zero: the order claims are taken in, "
        "and MC_RLOC for each - which is the machine-readable answer to "
        "'can this be compacted'",
        needs=("qemu", "nasm"), serial=True, timeout=300, builds=True),
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
        needs=("marty", "nasm"), serial=True, builds=True),
    Row("heapcheck", "soak", py("tests/heapcheck.py"), 60.0,
        "Drive tests/heapfrag and read its verdict out of the guest (SPEC.md"
        "66.8).",
        needs=("marty",), serial=True, builds=True),
    Row("xmcheck", "soak", py("tests/xmcheck.py"), 90.0,
        "The extended-memory TEARDOWN gate (SPEC.md 41.5, 29.4). QEMU and "
        "not MartyPC, and the row said `marty` for a year: the machine has "
        "to HAVE memory above 1MB and the target machine never can (SPEC.md "
        "41.9 rule 1), which is one of docs/TESTING.md's five legitimate "
        "uses of QEMU. It also needs nasm for the OVERLAY's map - xm_tab is "
        "in XMEM.DRV now (SPEC.md 41.12), not in the kernel.",
        needs=("qemu", "nasm"), serial=True, timeout=600, builds=True),
    Row("brpromise", "soak", py("tests/brpromise.py"), 120.0,
        "SPEC.md 71.11: the Browser's WF_SAVEU promise follows the FETCH - "
        "withdrawn when br_go starts one, back when it settles. The plain apps "
        "disk, so unlike the other br* rows it needs no `make browsertest`",
        needs=("marty",), serial=True),
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
    Row("dispzoom", "soak", py("tests/dispzoom.py"), 240.0,
        "SPEC.md 11.95.2.1: does a ZOOM land flush on an EXTENDED desktop?"
        "wm_snap_ax refuses to move a window right when it would hang off the"
        "screen, a test written against [vid_w] - which before 39.16 WAS the"
        "screen and on two cards is the SUM. So the refusal that keeps a"
        "maximized window at x=0 on one display stops firing on two, the"
        "window walks 7px, and wm_flush_ck then puts the left border back on"
        "top of the gap. Zooms the same window on one display and on two and"
        "requires the same answer; the single-display half is the CONTROL, so"
        "a failure says the second display broke it rather than that zoom is"
        "broken. Reads the record, because 7px of geometry does not show in a"
        "screenshot of a mostly-white window",
        needs=("marty",), serial=True),
    Row("dispblit", "soak", py("tests/dispblit.py"), 60.0,
        "Does a BLIT reach the second display? (SPEC.md 39.14.7)",
        needs=("marty",), serial=True),
    Row("dispbrow", "soak", py("tests/dispbrow.py"), 60.0,
        "The field's browser report: a drag that does not move it, and a"
        "width cut on a card wide enough to hold it",
        needs=("marty",), serial=True),
    # THE THREE ROWS BELOW DECLARED 60s AND TAKE FOUR TO NINE TIMES THAT.
    # `secs` derives the kill timeout (`max(60, secs*4+30)` = 270s), so all
    # three were being killed MID-RUN and reported as TIMEOUT - which reads
    # like a hung emulator and is not one. Measured on this container, each
    # run directly and to completion, every assertion passing:
    #
    #     dispsize    295 s        dispcalc    406 s        dispprefer  546 s
    #
    # Nothing stalls. dispsize profiled with `settle` instrumented: 61 calls,
    # 153.4 s of a 311.6 s wall inside settle (49.2%), and the LONGEST single
    # settle is 11.4 s against settle's own 120 s limit. Half of one of these
    # rows is the harness's screen-polling floor - `settle(quiet=1.0,
    # stable=2)` cannot return in under ~2 s and dispprefer alone makes 16
    # adapter switches at four settles each - so the wall time is sampling
    # cost, not the guest being slow and not the guest being stuck.
    #
    # The `secs` below is therefore what the row COSTS, and the explicit
    # timeout is ~2x that: enough that container jitter cannot kill a healthy
    # run, small enough that a genuinely hung emulator is still caught in
    # minutes rather than tens of them.
    Row("dispcalc", "soak", py("tests/dispcalc.py"), 420.0,
        "Does the Calculator add up, fold cleanly and redraw nothing spare?",
        needs=("marty",), serial=True, timeout=900),
    Row("dispcalcx", "soak", py("tests/dispcalcx.py"), 60.0,
        "Does the Calculator re-fold cleanly when its box moves under it?",
        needs=("marty",), serial=True),
    Row("dispcheck", "soak", py("tests/dispcheck.py"), 120.0,
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
    Row("dispcold", "soak", py("tests/dispcold.py"), 300.0,
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
    # 546 s measured - the longest of the three, 16 adapter switches at four
    # settles each. See the note above `dispcalc`.
    Row("dispprefer", "soak", py("tests/dispprefer.py"), 560.0,
        "Does a package's PER-ADAPTER preference and floor survive a drag"
        "across the seam, and does a USER outrank it? (SPEC.md 11.100)",
        needs=("marty",), serial=True, timeout=1200),
    Row("disptitle", "soak", py("tests/disptitle.py"), 150.0,
        "Does a title bar STRADDLING the seam have one polarity? (SPEC.md"
        "5.4.2.4) - it builds `make BAND=1` itself, the composer being a knob"
        "again since SPEC.md 5.9.6, and puts the default kernel back",
        needs=("marty",), serial=True, builds=True),
    Row("dispthm", "soak", py("tests/dispthm.py"), 60.0,
        "Does SPEC.md 76's theme meet the extended desktop honestly? Color is"
        "a fact about the PRIMARY and a window can be on the other card",
        needs=("marty",), serial=True),
    # 295 s measured, and the row the settle profile above was taken on.
    # See the note above `dispcalc`.
    Row("dispsize", "soak", py("tests/dispsize.py"), 320.0,
        "What size is a window given when it lands on the other card?"
        "(SPEC.md 11.100.3/11.100.4)",
        needs=("marty",), serial=True, timeout=900),
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
    Row("tank", "soak", py("tests/tank.py"), 150.0,
        "SPEC.md 85: TANK ATTACK draws, ADVANCES, and does not flash - the ink"
        "on the glass per DISPLAYED frame, whose floor against its median is"
        "the whole question a foreign-mode raster is built to answer",
        needs=("marty",), serial=True),
    Row("tankaim", "soak", py("tests/tankaim.py"), 200.0,
        "SPEC.md 85.6.5's aim assist and its reticle: no bearing is unreachable"
        "(by enumeration, not by the algebra), the TURN is untouched at TK_TURN"
        "a tick, the gun corrects inside the closed sight's box and nowhere"
        "else, and the sight closes on exactly the shots that land - read"
        "against tk_aimq and tk_aimz, the code's own measured error and range",
        needs=("marty",), serial=True),
    Row("tankspawn", "soak", py("tests/tankspawn.py"), 200.0,
        "SPEC.md 85.6.6: no round starts inside a piece of scenery - which one"
        "in nineteen did, sealing the player in a box a 26-unit step cannot"
        "leave - and a player who somehow IS inside one can still drive out",
        needs=("marty",), serial=True),
    Row("wireflick", "soak", py("tests/wireflick.py"), 120.0,
        "SPEC.md 78.5's three draw orders, as ink on the glass per displayed"
        "frame - the flicker measured rather than argued about",
        needs=("marty", "wiredisk"), serial=True),
    Row("wirefps", "soak", py("tests/wirefps.py"), 90.0,
        "What SPEC.md 5.6.4.1 is worth to a program that draws lines - apps/wire"
        "reading its own frame rate, with the dispatch poked out and back",
        needs=("marty", "wiredisk"), serial=True, alone=True),
    Row("paintrate", "soak", py("tests/paintrate.py"), 120.0,
        "SPEC.md 42.8.1: is Paint's brush stroke still sampled at the TICK? The"
        "facets in a hand-drawn curve were one 55ms sleep each. On the GLaBIOS"
        "twin, like paintwipe - and unlike paintwipe this row DOES take a"
        "number, so its docstring argues the case: the window is guest cycles"
        "with no int 13h in it, and the assertion is a separation of an order"
        "of magnitude rather than a calibrated figure",
        needs=("marty",), serial=True),
    Row("paintwalk", "soak", py("tests/paintwalk.py"), 120.0,
        "SPEC.md 42.8.3: a brush chord steps each axis exactly |d| times. The"
        "denominator lived in CX, which `loop` decrements, so a wide nib drew"
        "a zig-zag that grew with the hand's speed",
        needs=("marty",), serial=True),
    Row("paintblank", "soak", py("tests/paintblank.py"), 240.0,
        "SPEC.md 42.15: a full-canvas repaint is 980 ms through the pair"
        "decoder and one gfx_fill when every pixel is the same colour, which"
        "is the picture Paint draws most. Counts the DECODER, not the clock,"
        "and checks the stroke is still on the glass afterwards",
        needs=("marty",), serial=True),
    Row("paintsize", "soak", py("tests/paintsize.py"), 240.0,
        "SPEC.md 42.8.6.1: a maximize GROWS Paint's canvas and a restore"
        "shrinks it, so the two clicks walk pt_ucopy over every row at two"
        "strides. A row has AT MOST eight blocks and the walk assumed exactly"
        "eight: 97 seconds and a band of garbage in the saved picture",
        needs=("marty",), serial=True),
    Row("paintundo", "soak", py("tests/paintundo.py"), 150.0,
        "SPEC.md 42.8.6: draw, Ctrl+Z, Ctrl+Z - does the picture come back to"
        "the pixel? Nothing covered undo at all until the copy-on-first-touch"
        "bitmap went from a bit a ROW to a bit a BLOCK",
        needs=("marty",), serial=True),
    Row("spantest", "soak", py("tests/spantest.py"), 180.0,
        "SPEC.md 5.10: gfx_spans against the GFX_FILL a row its own refusal"
        "sends a caller to - nine shapes including an EMPTY row, both clips"
        "and a middle grey's dither, plus the refusal itself. apps/paint only"
        "ever asks for the shapes a brush chord makes",
        needs=("marty",), serial=True, builds=True),
    Row("spantest-vga", "soak",
        py("tests/spantest.py", "--machine", "os8088_xt_vga"), 180.0,
        "...and the same on VGA, which is gfx_spans' other row writer - the"
        "latch-and-bit-mask one, with vga_set_color and vga_gc_reset hoisted"
        "out of the span loop",
        needs=("marty",), serial=True, builds=True),
    Row("paintundo-vga", "soak",
        py("tests/paintundo.py", "--machine", "os8088_xt_vga"), 150.0,
        "...and the same on VGA, which is where SPEC.md 5.10's gfx_spans takes"
        "its OTHER row writer - the latch-and-bit-mask one. The redo hash is"
        "what compares it against the canvas the untouched walk wrote, so this"
        "is the gate on the planar half of the primitive",
        needs=("marty",), serial=True),
    Row("uilat", "soak", py("tests/uilat.py"), 120.0,
        "SPEC.md 7.3: how long a click waits while a worker draws, bracketed"
        "by two memory breakpoints because the mouse harness has a half-second"
        "floor and cannot see it (7.3.1)",
        needs=("marty", "wiredisk"), serial=True, alone=True),
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
    Row("dispreboot", "soak", py("tests/dispreboot.py"), 300.0,
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
    # Two boots a machine and two machines, plus the two `make`s the A/B needs,
    # which is what puts it at four minutes rather than one. It EARNS them: the
    # fixed leg alone cannot tell a conserved run from a run that never crossed
    # a cell, and this file can be null in a way that looks exactly like a pass
    # (SPEC.md 39.14.6). `--no-build` drops to the fixed leg for a hand-built
    # image; `--machine` picks one orientation.
    # THE POSITIVE CONTROL IS THE POINT OF THE ROW. Every assertion in it is
    # "nothing outside its own columns" or "the same bytes as the unclipped
    # draw", and all of them pass on a harness that draws nothing at all -
    # which is what a boot-and-diff version of this would BE, since nothing on
    # a stock desktop puts an icon off the right edge.
    # NOTHING ELSE IN THE TREE REACHES sw_fill_pat. A Disk listing that fits
    # draws no chevrons and the Task Manager has to be open, so a
    # boot-and-look version of this is a null test that reads like a pass -
    # icoclip's problem one primitive along, and the same answer.
    Row("fillpat", "soak", py("tests/fillpat.py"), 600.0,
        "Does the 1bpp PATTERNED fill lay the tile down where it says? "
        "(SPEC.md 5, 32) - gfx_fill_pat on a mono adapter is two masked edge "
        "columns through sw_patcol plus a rep stosw interior, with the tile "
        "row picked by (y & 7). Calls it through the debugger over rows it "
        "zeroed itself, at four rect shapes that run every arm including the "
        "one-byte-wide fold, and checks each byte against the kernel's OWN "
        "staged gfx_patbuf and edge masks rather than a golden image. Both "
        "strides.",
        needs=("marty",), serial=True),
    # THE ENTRY IS NAMED HERE and it is not decoration: ico_disk32 is the
    # INDEXED kind now (SPEC.md 25.7) and `icon_draw` reads that record as
    # 258 bytes of plain art, walking off its 13 into whatever follows. Every
    # assertion in this row is "nothing outside its own columns" or "the same
    # bytes as the unclipped draw", so a mismatched pair DRAWS GARBAGE AND
    # PASSES - measured, on the tree that introduced the kind. The record and
    # the entry have to be named together or this row tests nothing.
    Row("icoclip", "soak", py("tests/icoclip.py", "--entry", "icon_draw_ix"),
        600.0,
        "Does a 32-wide icon HANGING OFF THE RIGHT EDGE still clip byte for "
        "byte? (SPEC.md 25.6) - ico_pass_bb's per-byte column test is the "
        "only thing between an icon at x = w-8 and a write on the NEXT SCAN "
        "LINE, and ico_core does not refuse the shape. Calls icon_draw_ix "
        "through the debugger at all eight shift phases and at every column "
        "that hangs off, on BOTH strides (CGA 80, Hercules 90), over a zeroed "
        "background so two draws are comparable.",
        needs=("marty",), serial=True),
    Row("dispseam", "soak", py("tests/dispseam.py"), 600.0,
        "Does the one cell a display SEAM crosses still reach the glass?"
        "(SPEC.md 39.14.11) - it builds `make NOSEAMCUT=1` itself for the A/B"
        "and puts the default kernel back, both seam orientations",
        needs=("marty",), serial=True, builds=True),
    Row("dskwstage", "soak", py("tests/dskwstage.py"), 120.0,
        "SPEC.md 18.4.2.1: does the DMA STAGING arm run, and does it move the "
        "RIGHT bytes? dskw_runadd's third answer - CF=0 with CX != 0, `not "
        "one sector fits this DMA page` - fell through into a shared "
        "`jmp .ioerr` from 2e8e292 until then, so dskw_wdata.stg and "
        "dskw_rdata.stg had never executed and the fix TURNED ON a routine "
        "nobody had watched. Nothing on a desktop reaches it (18.4.1 keeps "
        "the kernel's own bases 512-aligned), so the row arranges it: a 200KB "
        "mem_claim spans three 64KB physical boundaries, and a buffer 0xF0 "
        "bytes short of one is the only thing that makes dskw_runmax answer "
        "0. Five cases with two page-safe CONTROLS, .stg counted by exec "
        "breakpoint rather than inferred, and the bytes settled OFF the "
        "machine - the floppy is flushed and walked by tests/unit/t_image's "
        "own FAT12 reader, which shares no code with the kernel that wrote "
        "it, so a writer and a reader agreeing on the same wrong thing "
        "cannot pass. `--bug` asserts the PRE-fix refusal instead, which is "
        "what makes the A/B repeatable against an old image",
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
        needs=("marty",), serial=True, builds=True),
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
        needs=("marty",), serial=True, builds=True),
    Row("frpromise", "soak", py("tests/frpromise.py"), 600.0,
        "SPEC.md 40.4: Fractal promises when the frame lands and takes it"
        "back when the view moves.",
        needs=("marty",), serial=True),
    Row("fdlggrey", "soak", py("tests/fdlggrey.py"), 60.0,
        "The file dialog's default button: REDRAWN IN PLACE must equal"
        "FRESHLY PAINTED.",
        needs=("marty",), serial=True, builds=True),
    Row("fdlgsmall", "soak",
        ["env", "OS88_DEFINES=KERN_SMALL", "OS88_BUILD=build/smallk",
         "OS88_SYSIMG=build/small360.img"] + py("tests/fdlggrey.py"), 300.0,
        "...and the SAME drive against kern_small, where the WHOLE dialog is "
        "an on-demand module (SPEC.md 38.0, docs/KERN-SMALL-MODULE-SPLIT.md "
        "9.2.6) rather than resident code. It is `fcpsmall`'s argument one "
        "feature along and a bigger engine: seven entries with two exit "
        "conventions, every call out of the image a far one through an `xd_` "
        "entry, the register epilogues copied inside the image, and mod_need "
        "reading it off the disk on fdlg_open with mod_drop giving it back in "
        "fdlg_reap. NONE of that is exercised by the row above, which runs "
        "the resident build. It builds its own image (`make small`) for "
        "smallboot's reason.",
        needs=("marty",), serial=True, builds=True),
    Row("fdlgdrop", "soak", py("tests/fdlgdrop.py"), 300.0,
        "...and the module comes BACK on every route a dialog ends by "
        "(SPEC.md 38.0.1). The row above drives the dialog and never asks "
        "what happened to its image; three of the four dismissals - Open, "
        "Cancel, Escape - clear [fdlg_win] from inside the image's own "
        "W_ONCLICK, and mod_drop sat behind three separate compares of that "
        "same word, so the pass that should have collected the claim was "
        "turned away by the very store it was meant to notice. A 16KB claim "
        "held for the rest of the session on the machine with 128KB in it, "
        "and the CLOSE BOX - the one route nobody uses - is the one that "
        "worked, which is how it survived the module split's own testing. "
        "The assertion is mod_tab[MOD_FDLG].seg and not a picture, because "
        "the leak is invisible: the dialog really is gone and the next one "
        "reuses the image it never gave back. Each route is a TRANSITION - "
        "held while the dialog is up, zero after - so a build that stopped "
        "LOADING the module fails the first half rather than passing the "
        "second. Against the kernel before it: three fail, the close box "
        "passes. It builds its own image (`make small`) for smallboot's "
        "reason.",
        needs=("marty",), serial=True, builds=True),
    Row("fdlgup", "soak", py("tests/fdlgup.py"), 60.0,
        "SPEC.md 13.8.3: the Standard File dialog's buttons fire on the"
        "RELEASE.",
        needs=("marty",), serial=True, builds=True),
    Row("fmthumb", "soak", py("tests/fmthumb.py"), 60.0,
        "SPEC.md 13.10.5: the Disk window's scroll-bar THUMB is dragged, and"
        "x is never read.",
        needs=("marty",), serial=True),
    Row("fdlgthumb", "soak", py("tests/fdlgthumb.py"), 90.0,
        "SPEC.md 13.10.5: ...and the Standard File dialog's, which is the"
        "second bar one gesture record has to tell apart (13.10.5.10).",
        needs=("marty",), serial=True, builds=True),
    Row("pkgthumb-np", "soak", py("tests/pkgthumb.py", "notepad"), 90.0,
        "SPEC.md 13.10.7: the thumb gesture inside a PACKAGE - Note Pad.",
        needs=("marty",), serial=True, builds=True),
    Row("pkgthumb-br", "soak", py("tests/pkgthumb.py", "browser"), 90.0,
        "SPEC.md 13.10.7: ...the Browser.",
        needs=("marty",), serial=True, builds=True),
    Row("pkgthumb-wd", "soak", py("tests/pkgthumb.py", "word"), 120.0,
        "SPEC.md 13.10.7: ...and Word, which needed 13.10.6.4 settling first -"
        "its menus are a modal poll and the thumb's two edges are disjoint"
        "from them.",
        needs=("marty",), serial=True, builds=True),
    Row("pkgthumb-tp", "soak", py("tests/pkgthumb.py", "texpad"), 90.0,
        "SPEC.md 13.10.7.2: ...and TexPad, whose TWO bars share one gesture"
        "record. --bar=1 drives the preview pane's.",
        needs=("marty",), serial=True, builds=True),
    Row("fmbtn", "soak", py("tests/fmbtn.py"), 60.0,
        "SPEC.md 22.18: the Disk window's two header buttons fire on the"
        "RELEASE.",
        needs=("marty",), serial=True),
    Row("fsxdisp", "soak", py("tests/fsxdisp.py"), 60.0,
        "Does an fsx bracket take ONE display and dark the others? (SPEC.md"
        "39.18)",
        needs=("marty",), serial=True, builds=True),
    Row("knobhd", "soak", py("tests/knobhd.py"), 900.0,
        "SPEC.md 52.10.2.1: a KNOB kernel installed to a hard disk and booted "
        "off it, on BOTH adapters. The build matrix assembles knob kernels and "
        "never boots one; hdboot boots a disk and only the shipped kernel; "
        "every other boot row is a floppy - and the defect needed all three at "
        "once, because the volume boot record is the only loader that has to "
        "be TOLD where the heap starts. REBUILDS build/ and puts it back, and "
        "erases the VHD.",
        needs=("marty",), serial=True, timeout=2400, builds=True),
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
    Row("hibernate", "soak", py("tests/hibernate.py"), 300.0,
        "SPEC.md 87: Hibernate... writes the machine to the hard disk and the "
        "next boot offers to resume it - the About box is the witness, read "
        "out of the restored instance table; then the same again with "
        "Discard. Builds its own VHD under build/",
        needs=("marty",), serial=True, timeout=1500),
    Row("hibernatedrv", "soak", py("tests/hibernate.py", "--driver"), 300.0,
        "SPEC.md 87 through HDD.DRV: a floppy boot whose SYSTEM.CFG wants the "
        "driver, so C: is a DVK_DRV volume and the resume's transport facts "
        "come through DSV_GEOM",
        needs=("marty",), serial=True, timeout=1500),
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
        needs=("qemu", "nasm"), serial=True, timeout=900, builds=True),
    Row("tmload", "soak", py("tests/tmload.py"), 150.0,
        "SPEC.md 28.7: the CPU meter and the process rows read the same "
        "numbers. The page used to show two figures that contradicted each "
        "other in plain sight and were both right - it charged its own "
        "spinning worker 34-38% of CPU TIME while the graph drew 0-2% of "
        "SPIN COUNT. This compares three readings computed three ways: the "
        "kernel's sch_cycles, the page's tm_load, and the page's tm_pct.",
        needs=("marty",), serial=True, timeout=900),
    Row("curdisk", "soak", py("tests/curdisk.py"), 240.0,
        "SPEC.md 7.4: the arrow TRACKS the hand through a disk transfer. It "
        "used to freeze with the machine and then LEAVE THE SCREEN - once an "
        "operation moved FPG_WARM = 3 sectors the widget armed, and the "
        "unclipped menu_draw_bar inside fpg_arm spent gfx_lock's promised "
        "hide. Both claims here are DIFFERENCES, so the row builds "
        "NOCURDISK=1 itself: a cursor move while [gfx_lock_flag] is set is "
        "unreachable on that arm by mou_apply's own first compare, and a "
        "one-armed reading could not tell that from a test that never "
        "reached a freeze at all.",
        needs=("marty",), serial=True, builds=True, timeout=900),
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
        needs=("qemu", "nasm"), serial=True, timeout=900, builds=True),
    Row("mouseup", "soak", py("tests/mouseup.py"), 60.0,
        "SPEC.md 13.7's release, apps/os88ui.inc's arm, and MOUSEUP-PLAN"
        "4.2's guard.",
        needs=("marty",), serial=True, builds=True),
    Row("paintgif", "soak", py("tests/paintgif.py"), 60.0,
        "HOW LONG DOES PAINT TAKE TO OPEN OS8088.GIF? - in GUEST CYCLES",
        needs=("marty",), serial=True),
    Row("paintlzw", "soak", py("tests/paintlzw.py"), 120.0,
        "SPEC.md 42.21: ...and WHICH HALF of it. paintgif times the whole "
        "operation, which is the right shape for a regression that could be "
        "anywhere and cannot say where this one was: the decode was 12,547 ms "
        "and 999 cycles a pixel of it were the LZW loop against 169 in "
        "pt_line_put, because the reader emitted one pixel per near call with "
        "two more around it for the character stack. Breakpoints on Paint's "
        "own labels split pt_gif_in four ways and then split the decode "
        "again, pt_line_put against the loop that feeds it. The ceilings are "
        "loose on purpose - they catch a return to the old SHAPE, not a "
        "picture whose dither packs differently",
        needs=("marty",), serial=True),
    Row("paintanchor", "soak",
        py("tests/paintanchor.py", "--machine", "os8088_5150_herc_gla"), 300.0,
        "SPEC.md 11.90.3: a pure SHRINK owes its content nothing. ui_grow"
        "repaints the union of the old rect and the new, so the window used to"
        "be told to draw all of itself about pixels nothing painted over."
        "Asserts pt_blit is entered with an EMPTY rect, that every surviving"
        "canvas pixel is byte-identical across the drag - which is what makes"
        "skipping it legitimate rather than lucky - and that the vacated"
        "columns went back to the desktop",
        needs=("marty",), serial=True),
    Row("paintshrink", "soak",
        py("tests/paintshrink.py", "--machine", "os8088_5150_herc_gla"), 300.0,
        "SPEC.md 42.17: a shrink that would lose ink gives back what it CAN."
        "Refused used to mean pinned where it started, so one stroke kept the"
        "whole canvas. Inks a stroke at a known place, types a size well past"
        "it into each size box in turn - the GROW BOX cannot reach, the window"
        "has a minimum - and reads what pt_resize was handed, because"
        "pt_szapply resizes the window and pt_track re-fits the width back up"
        "before anything else can look",
        needs=("marty", "nasm"), serial=True),
    Row("paintrz", "soak", py("tests/paintrz.py"), 420.0,
        "SPEC.md 42.19.1: does a resize still have the PICTURE afterwards?"
        "pt_resize used to carry it through the undo image, where every walk"
        "order is safe because the source is a buffer of its own; it moves the"
        "picture where it lies now, and the order IS the correctness argument."
        "The first build had it inverted and every other paint row passed -"
        "they all resize the one way the wrong answer survives - while it"
        "wiped the picture from the middle of the canvas down. Two strokes far"
        "apart, then five resizes: each axis in each direction, and the two"
        "disagreeing (which is what the two passes exist for). The ink is read"
        "out of the CANVAS and compared as a SET, so neither a repaint nor a"
        "smear inside the bounding box can flatter it",
        needs=("marty", "nasm"), serial=True),
    Row("paint1bpp", "soak",
        py("tests/paint1bpp.py"), 180.0,
        "SPEC.md 42.23: is the canvas ONE BIT a pixel, and is the DIB in"
        "front of it a valid 1bpp BMP? The claim is the assertion and not"
        "the pixels - 448x258 is 14.2KB one bit deep against 56.6 packed,"
        "which on the 128KB floor machine is the difference between the full"
        "default picture and 42.6.5's letterbox, and NOT ONE other paint row"
        "would notice a canvas that came out four times bigger than it had"
        "to be. The header is checked field by field against the live"
        "geometry because the canvas IS the file (42): a save is one write"
        "of it, so a header that lies is a file no host can open. A blank"
        "canvas must read all 0xFF, which is 42.23.1's polarity - 1 is"
        "WHITE, both because that is what a 1bpp BMP means and because"
        "gfx_blit1 takes a band that way up at 12.5 clocks a byte instead of"
        "the complementing loop's 17",
        needs=("marty",), serial=True),
    Row("paint1bpp-colour", "soak",
        py("tests/paint1bpp.py", "--machine", "os8088_xt_vga", "--colour"),
        180.0,
        "...and THE NEGATIVE, which is the half that would drift silently: a"
        "COLOUR adapter is untouched by 42.23 - four planes, sixteen"
        "colours, a 118-byte DIB and the 4bpp arithmetic to the byte. A new"
        "canvas opens in colour on a VGA and only a mono screen opens one"
        "bit. Nothing else in tests/ asserts a negative about the format, so"
        "a change that made EVERY canvas one bit deep would pass the whole"
        "suite and quietly cost the VGA fifteen of its colours",
        needs=("marty",), serial=True),
    Row("paint1blit", "soak",
        py("tests/paint1blit.py"), 300.0,
        "SPEC.md 42.23.4: the TWO paths a one-bit canvas reaches the screen"
        "by, compared. kern_big has gfx_blit1 and blits the band straight in;"
        "kern_small carries the SLOT AND NOT THE BODY (5.4.2), so Paint"
        "expands each row for gfx_blit4 instead - and the two must draw the"
        "same picture to the pixel. Neither arm alone would catch a wrong"
        "one: the fast path could draw a plausible picture one row or one"
        "byte out, and the fallback is what every other 1bpp row already"
        "exercises. The fixture is BUILT here, every byte differing from its"
        "neighbours, because a flat picture passes all three of those"
        "mistakes. It is also what makes 42.23.4's negative-stride claim a"
        "checked fact rather than a second piece of reasoning",
        needs=("marty",), serial=True),
    Row("paint1load", "soak",
        py("tests/paint1load.py"), 180.0,
        "SPEC.md 42.23.6: does a 1bpp BMP LOAD? The one path in 42.23 no"
        "other row reaches - pt_line_put's bit arm, pt_fmtpick running before"
        "pt_adopt, and the fixture is BUILT here rather than committed"
        "because what it has to be is a pure function of what the reader is"
        "being asked: a pattern whose every byte differs from its neighbours,"
        "so a row read one byte early or one bit out of phase cannot come"
        "back looking right. The oracle is the CANVAS against the FILE and"
        "not the screen - a one-bit canvas is byte-for-byte a 1bpp BMP's"
        "pixel rows (42.23.2) - and [pt_trunc] must be CLEAR, because"
        "nothing was reduced here and File > Save has to stay allowed",
        needs=("marty",), serial=True),
    Row("paint1load-vga", "soak",
        py("tests/paint1load.py", "--machine", "os8088_xt_vga"), 180.0,
        "...and the same on a COLOUR adapter, which is the interesting leg:"
        "42.23.6 opens a colourless file one bit deep on ANY card, so this is"
        "a one-bit canvas drawn through the planar renderer - pt_blit_1's"
        "expansion into pt_line and gfx_blit4, on the machine whose every"
        "other canvas is four planes",
        needs=("marty",), serial=True),
    Row("paintrz-1bpp", "soak",
        py("tests/paintrz.py", "--machine", "os8088_5150_herc_gla"), 420.0,
        "...and the ONE-BIT canvas, which is a different move: one run of"
        "bits a row instead of four plane-runs whose three inner boundaries"
        "all shift with the width (SPEC.md 42.23, 42.13.2). IT WAS"
        "`paintrz-packed` and the rename is the point: since 42.23 a Hercules"
        "gives Paint a one-bit canvas and not a packed one, so the row that"
        "read `packed` in its name had stopped covering that format. Packed"
        "4bpp is now reached two ways - a COLOUR file opened on a 1bpp"
        "adapter, and gfx_blitp refusing on a colour one - and `paintpack` is"
        "the row that forces the second",
        needs=("marty", "nasm"), serial=True),
    Row("alertbtn", "soak",
        py("tests/alertbtn.py", "--machine", "os8088_5150_herc_gla"), 300.0,
        "SPEC.md 75.3.0: the STANDARD alert's button row - os88ui's and not"
        "Paint's, which is only the alert easiest to raise. One press must"
        "draw ONE button (os88ui_adn redrew the whole row, so a press lettered"
        "three where one was needed) and the row must TRACK: held and dragged"
        "off, the button comes up, which is what says the gesture is cancelled"
        "before the finger commits. Also checks 42.16.1's GIF default",
        needs=("marty",), serial=True),
    Row("alertanim", "soak",
        py("tests/alertanim.py", "--machine", "os8088_5150_herc_gla"), 300.0,
        "SPEC.md 11.99.2.1: the 'Save changes?' alert must NOT zoom open. The"
        "user clicked the close box and got a dialog instead, so a third of a"
        "second of outline in front of it is the machine making a show of"
        "getting in the way. AN A/B AND NEITHER HALF IS A ROW ALONE: a LAUNCH"
        "must still animate, or a theme with the zoom off would pass this"
        "trivially, and the alert must be on the glass at the end, or a dialog"
        "that failed to open reads as one that opened quietly",
        needs=("marty",), serial=True),
    Row("paintdirty", "soak", py("tests/paintdirty.py"), 300.0,
        "SPEC.md 42.16: does Paint ask before it throws a picture away? A"
        "FLAG and not Note Pad's checksum, so the places that set and clear it"
        "are the whole feature - opened 0, MAXIMIZED AND RESTORED 0 (a resize"
        "really does change the document, and firing there would ask about a"
        "blank picture nobody drew on), one stroke 1, and the close box"
        "refuses and puts an alert up",
        needs=("marty",), serial=True),
    Row("paintcull", "soak", py("tests/paintcull.py"), 300.0,
        "SPEC.md 5.4.3.3: does 11.3.3's CULL cost Paint its four planes? An"
        "armed clip region is one of gfx_blitp's refusals and pt_topacked"
        "reads any refusal as a fact about the MACHINE, so one ordinary"
        "damage repaint converted a VGA canvas to nibbles for the session."
        "Open, ONE STROKE - 42.15 answers a blank canvas with a fill and"
        "never blits - maximize, restore; asserts [pt_planar] at each step and"
        "reports which guard fired if it did not hold",
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
    Row("paintwipe", "soak", py("tests/paintwipe.py"), 90.0,
        "SPEC.md 42.13.2.1: is a BLANK canvas blank? Every other paint row"
        "opens a picture and compares it against the file, which is the one"
        "oracle that cannot see pt_wipe - a wrong ground is not a difference"
        "from the file, it is what every comparison starts from. Hercules,"
        "because 42.13 stores the canvas packed only on a 1bpp adapter and"
        "the body that was wrong is the one VGA never runs - the GLaBIOS twin"
        "of it, since this row takes no timing and ibm5150_82_v4 is not in"
        "this tree.",
        needs=("marty",), serial=True),
    Row("paintpack", "soak", py("tests/paintpack.py"), 600.0,
        "SPEC.md 42.13.1: the REFUSAL path. Builds the NOPLANE kernel, where"
        "every gfx_blitp says no in six bytes, so Paint's pt_topacked runs"
        "for real and the nibbles it produced are compared against the file,"
        "then paintbig again over it - the only kernel on which the PACKED"
        "half of pt_copy/pt_paste runs without a second monitor. Rebuilds the"
        "tree, like blitplane",
        needs=("marty", "nasm"), serial=True, builds=True),
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
        needs=("marty", "nasm"), serial=True, timeout=900, builds=True),
    Row("blitcut", "soak", py("tests/blitcut.py"), 420.0,
        "SPEC.md 39.14.7.2: does a STRADDLING gfx_blit4 draw the same pixels "
        "cut at the seam as it does whole-virtual, and is it several times "
        "quicker? blitplane's shape one seam along, and rebuilds the tree for "
        "the same reason - the A/B is two kernels, this one and NOBLITCUT=1. "
        "It needs a two-card machine and a window DRAGGED across the seam: "
        "the drag is what makes gfx_blitp refuse and Paint convert its canvas "
        "to nibbles (SPEC.md 42.13.1), which is what puts the block through "
        "gfx_blit4 at all, and a W_X written by hand would skip it.",
        needs=("marty", "nasm"), serial=True, timeout=1800, builds=True),
    Row("paintmove", "soak", py("tests/paintmove.py"), 60.0,
        "Compact the heap out from under a LIVE Paint canvas (SPEC.md"
        "66.2/42).",
        needs=("marty",), serial=True, builds=True),
    Row("rdmove", "soak", py("tests/rdmove.py"), 60.0,
        "Compact the heap out from under the RAM disk's store (SPEC.md"
        "66.5.10).",
        needs=("marty",), serial=True),
    Row("hdmove", "soak", py("tests/hdmove.py"), 90.0,
        "Compact the heap out from under a DONATED listing claim (SPEC.md "
        "66.5.10.2) - the only claim in the tree with three holders, two of "
        "them the kernel's and on the far side of the ABI from the callback. "
        "A declaration is not a mechanism: check 1 is that the block MOVED, "
        "and check 4b that no word anywhere still holds the old base",
        needs=("marty", "nasm"), serial=True, timeout=900),
    Row("heaphi", "soak", py("tests/heaphi.py"), 90.0,
        "A driver's second image goes at the TOP of the heap (SPEC.md "
        "50.3.2.1). The user's sequence - tick Hard Drive, tick Ram Disk, "
        "select its page - and then the number mem_claim answers a claim "
        "from rather than the one mem_avail prints: a compaction must still "
        "be worth the 63KB read-ahead. RAMPAGE.DRV claimed low first-fits "
        "ABOVE the movable claims and pinned the arena into two pieces, "
        "which moved that number by 64.5KB and the LIVE largest run by "
        "nothing at all",
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
        needs=("marty",), serial=True, builds=True),
    Row("sizesnap", "soak", py("tests/sizesnap.py"), 60.0,
        "the SIZE snap aligns a content width WITHOUT shrinking the zoom "
        "(SPEC.md 11.94.5) - a maximized window must stay x=0, w=[vid_pw]",
        needs=("marty",), serial=True),
    Row("netpromise", "soak", py("tests/netpromise.py"), 240.0,
        "SPEC.md 70.7/77.47: Telnet and the FTP server promise per DEBT, not"
        "per session.",
        needs=("marty",), serial=True),
    Row("monoink", "soak", py("tests/monoink.py"), 120.0,
        "SPEC.md 11.96.17.1's hook reaches font_run and not font_str - it must "
        "not perturb the path it does not serve, and the gap is measured",
        needs=("marty",), serial=True),
    Row("su1bpp", "soak", py("tests/su1bpp.py"), 120.0,
        "SPEC.md 11.96.17: a two-colour window's raise cache is ONE plane, a "
        "quarter the size, and puts back the pixels a full repaint would",
        needs=("marty",), serial=True),
    Row("win1bpp", "soak", py("tests/win1bpp.py"), 90.0,
        "SPEC.md 11.96.17's two-colour declaration reaches W_FLAGS, and clear "
        "of SPEC.md 7.2.1's cursor shape in the same byte",
        needs=("marty",), serial=True),
    Row("tpsaveu", "soak", py("tests/tpsaveu.py"), 120.0,
        "TeXPad - the largest window in the tree, and the one four planes "
        "cannot fund - keeps its pixels one plane deep (SPEC.md 11.96.17)",
        needs=("marty",), serial=True),
    Row("tmrup", "soak", py("tests/tmrup.py"), 60.0,
        "SPEC.md 13.8: the Timer's three buttons fire on the RELEASE.",
        needs=("marty",), serial=True),
    Row("kernresident", "full", py("tests/kernresident.py"), 90.0,
        "kernel.asm rule 3: kern_big fully RESIDES in KERN_RESIDENT_KB at a "
        "bare desktop - the half of the rule an assembler cannot see, which "
        "is a claim made at boot and never given back.",
        needs=("marty",), serial=True),
    Row("zoomsave", "soak", py("tests/zoomsave.py"), 420.0,
        "SPEC.md 11.96.16.2: a window ZOOMED over another banks it - the "
        "precover pass had one caller and a maximize took 0 caches.",
        needs=("marty",), serial=True),
    Row("dmgcull", "soak", py("tests/dmgcull.py"), 420.0,
        "SPEC.md 11.3.3: a marked window does not paint where something above "
        "it is about to - 452 cells under the mover became 26. Counts CELLS "
        "and not calls, because a culled cell is still a call.",
        needs=("marty",), serial=True),
    Row("tmdmg", "soak", py("tests/tmdmg.py"), 420.0,
        "SPEC.md 28.10.2: a partial repaint of the Task Manager draws only "
        "the part - 225 cells put on the glass became 0, against 549 for a "
        "whole repaint. CELLS and not calls (11.3.3).",
        needs=("marty",), serial=True),
    Row("tmrepair", "soak", py("tests/tmrepair.py"), 420.0,
        "SPEC.md 28.11: the Task Manager's quiet pages hold a raise cache by "
        "REPAIRING at the restore - a whole-content band, and tm_update "
        "spends the debt W_PAINT is handed.",
        needs=("marty",), serial=True),
    Row("tmselfsu", "soak", py("tests/tmselfsu.py"), 300.0,
        "SPEC.md 28.8.1: the Task Manager stops repainting for ITS OWN raise "
        "cache and so gets to keep one - and still sees everybody else's, "
        "which is what makes the cut the self-reference and not the range. "
        "Also the row that would notice tm_quiet's key going unrecorded again",
        needs=("marty",), serial=True),
    Row("tmowner", "soak", py("tests/tmowner.py"), 300.0,
        "SPEC.md 28.4.5: a raise cache is listed under the PACKAGE that owns "
        "the window, and a kernel window's stays under System. Reads the rows "
        "the page COMPOSES rather than the pixels, which is the only way to "
        "say which group a row is in",
        needs=("marty",), serial=True),
    Row("tmground", "soak", py("tests/tmground.py"), 300.0,
        "SPEC.md 28.10: the Task Manager paints its own ground, so a repaint"
        "is not a 450ms white hole.",
        needs=("marty",), serial=True),
    Row("trackmove", "soak", py("tests/trackmove.py"), 60.0,
        "Compact the heap out from under a LOADED module (SPEC.md 66.5.2/45).",
        needs=("marty",), serial=True, builds=True),
    Row("tpdraw", "soak", py("tests/tpdraw.py"), 300.0,
        "Does TeXPad's INCREMENTAL source redraw draw what a full repaint"
        "draws? (SPEC.md 69.8)",
        needs=("marty",), serial=True),
    Row("trkrate", "soak", py("tests/trkrate.py"), 60.0,
        "trkrate - XT mode's second rate, and the surface it refuses (SPEC.md"
        "45.9.3)",
        needs=("marty",), serial=True, builds=True),
    Row("trktxsurf", "soak", py("tests/trktxsurf.py"), 90.0,
        "The fullscreen SURFACE is a pick, not XT mode's - text at a 45.10"
        "rate (SPEC.md 45.13.7)",
        needs=("marty",), serial=True, builds=True),
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
