; =============================================================================
; os8088 1.0 - kernel
;
; A Macintosh-style GUI for the 8086: 640x480x16 (VGA mode 12h), pre-emptive
; round-robin multitasking off the PIT, serial Microsoft mouse on COM1.
;
; Runs in real mode, near model: CS = DS = KERNEL_SEG for all kernel code and
; every task, SS = LOW_SEG, ES scratch. EVERY inter-module call inside the
; kernel is near - there is no far code and no second code segment. The
; module contracts (register use, data layouts, concurrency rules) live in
; SPEC.md; that document is binding.
;
; Three fixed entry points, at KERNEL_SEG (0x0060 - see the ladder below):
;   0060:0000  cold entry (the boot sector jumps here)
;   0060:0008  boot splash tick (SPEC.md 15): far-called by the boot sector
;              after every sector it reads, while the rest of this image is
;              still coming off the floppy
;   0060:0010  os8088 API jump table (SPEC.md 20.3): 8-byte DS-switching cells
;              at pinned offsets, far-called by loaded packages
; =============================================================================

cpu 8086
bits 16
org 0x0000

; --- global constants (SPEC.md 3) -------------------------------------------
KERNEL_SEG  equ 0x0060          ; linear 0x00600 - the first paragraph above
                                ; the BIOS data area, and the base of the ONE
                                ; 64KB region the whole kernel lives in
                                ; (docs/KERNEL-MEMORY.md). The boot sector no
                                ; longer floors it: boot/boot.asm relocates
                                ; itself out of the landing zone before it
                                ; reads a single sector
VGA_SEG     equ 0xA000          ; mode 12h planar framebuffer
SCREEN_W    equ 640
SCREEN_H    equ 480
ROW_BYTES   equ 80
MBAR_H      equ 20              ; menu bar height, px
TITLE_H     equ 18              ; window title bar height, px

CBLACK      equ 0
CWHITE      equ 15
CLGRAY      equ 7
CDGRAY      equ 8
CBLUE       equ 1               ; 0,0,170 - the Color theme's ACTIVE title bar
CTEAL       equ 3               ; 0,170,170 - ...and its desktop (SPEC.md
                                ; 76.12). Both are VGA-only by construction:
                                ; SPEC.md 39.4 reduces every index outside
                                ; {0, 15} to black, white or the 50% dither,
                                ; so a Color theme on a 1bpp adapter would be
                                ; Bright with extra steps - which is why the
                                ; theme is refused there rather than reduced

; --- the clip region (SPEC.md 11.3) ------------------------------------------
; wm.inc builds it; vga12.inc, font.inc and icons.inc consume it. The layout
; is therefore a cross-module contract and lives here rather than in wm.inc,
; which NASM only reaches five includes later.
WM_CLIP_MAX equ 16              ; rects; more than this and the frame is
                                ; skipped instead, which is exactly what the
                                ; wm_obscured veto used to do
WCR_X1  equ 0                   ; one clip rect, 8 bytes, inclusive corners
WCR_Y1  equ 2
WCR_X2  equ 4
WCR_Y2  equ 6
WCR_SZ  equ 8

GLS_X   equ 0                   ; SPEC.md 5.6.7's resumable walk state, owned
GLS_Y   equ 2                   ; by the CALLER: current point, the Bresenham
GLS_ERR equ 4                   ; error, and the four constants of the line
GLS_DX  equ 6
GLS_DY  equ 8
GLS_SX  equ 10                  ; +1 / -1
GLS_SY  equ 12
GLS_SZ  equ 14

; loadable programs (SPEC.md 20) - a package's region is a HEAP CLAIM
APP_MAX_SIZE equ 0xF000         ; the biggest single package: 60KB, and the
                                ; ceiling is now the SEGMENT rather than a
                                ; pool - a package links at org 0 and
                                ; addresses itself with 16-bit offsets, so
                                ; image + bss cannot reach 64KB whatever the
                                ; heap has free. Mirrored in apps/os88api.inc
                                ; and tools/os88pkg.py
PKG_FILE_HI  equ 16             ; the HIGH word of the biggest FILE the mount
                                ; will type as a package (SPEC.md 19.1): the
                                ; file must be under 1MB. It is deliberately
                                ; NOT APP_MAX_SIZE - that bounds the primary
                                ; segment's image+bss, which is all it ever
                                ; meant, and it stopped bounding the FILE the
                                ; moment a package could carry parts beyond
                                ; its own segment. A decision rather than a
                                ; derivation: far above any package this tree
                                ; has (the largest is ~98KB) and far below
                                ; anything that could overflow the loader's
                                ; KB arithmetic
PKG_DISP     equ 12             ; the dispatcher's fixed offset INSIDE the
                                ; .o88 header (SPEC.md 20.2): three bytes,
                                ; `call bp / retf`. Every kernel-to-package
                                ; call lands here with BP = the real target,
                                ; which is what lets every package callback
                                ; stay a near proc with a near `ret`

; =============================================================================
; The memory map (SPEC.md 2). ONE ladder, bottom to top, every rung derived
; from the one below it, and NO growth room anywhere in it: each rung is the
; measured size of what it holds, so the heap starts wherever this build's
; kernel happens to end and moves when the kernel does.
;
;   0x00000  IVT + BIOS data area                    (theirs, 1,536 B)
;   0x00600  KERNEL_SEG  .text + .bss                KIMG_PARA (derived)
;            FAT_SEG     mount-time FAT snapshot     FAT_PARA
;            LOW_SEG     .lowbss + task 0's stack    LOW_PARA
;            HEAP_SEG    the claim heap              up to int 12h's top
;            (the top)   the boot sector + its stack 2,560 B, until handoff
;
; That last rung is not the kernel's and is not reserved: boot/boot.asm
; relocates itself to the last 512 bytes the machine has and runs there while
; the kernel lands (SPEC.md 2.7), so those bytes are live during the boot and
; ordinary heap the instant kmain sets its own stack. The heap hands regions
; out from the top down, so the first package loaded sits exactly where the
; sector was.
;
; **There is no package pool.** It was a fixed 60KB reservation between the
; kernel and the heap - unavailable to anything else whether or not a package
; was loaded - and a package's region is an ordinary heap claim now (SPEC.md
; 20.1/50), taken from the TOP of the heap downward while data claims grow
; up from the bottom. That returned 60KB to every machine: 510KB -> 570KB of
; heap on a 640KB one, and it is the whole reason a 128KB machine can run
; this at all (the pool's own top used to sit above 128KB, so such a machine
; had no heap and could load nothing).
;
; **Everything from KERNEL_SEG to the end of task 0's stack is the kernel**,
; and the KERN_BUDGET guard holds that whole span to a budget just above the
; BIOS data area (100KB of machine on kern_big, 64KB on kern_small). Code, data, scratch, the FAT snapshot, the disk buffers and
; every task stack are inside it. The one deliberate exception is the menu
; save-under, which is a heap claim (SPEC.md 12.4/50) because it is 20KB that
; only exists while a menu is down.
;
; The two guards are NAMED, not numbered, and the names are the whole point:
; KERN_BUDGET is the FOOTPRINT (this whole span, RAM taken from the machine)
; and KERN_CODE_MAX is the SEGMENT (.text + .bss inside one 64KB window,
; because offsets are 16 bits). They bind different things and are relieved
; by different mechanisms - the boot overlay and the cold segment buy
; KERN_CODE_MAX room and buy KERN_BUDGET nothing at all - and the numbering
; they used to carry said none of that.
;
; The sizes are measured, not guessed. With a 0xCC fill in every byte of the
; stack region and the machine driven hard - Timer, two Bounces, About, the
; Control Panel on both its pages, the Task Manager with a window drag, a Disk
; window, the Fractal with its worker task, and Paint saving a GIF into a
; folder it created from the file dialog - the deepest mark left was 246 bytes
; on task 0's stack and 150 on a background task's.
; =============================================================================
; --- the split (docs/history/KERN-SPLIT-PLAN.md) -------------------------------------
; KERN_BIG and KERN_SMALL are two builds off this one tree, and the knob is
; `make KERN_BIG=1`. The reason is written out in docs/KERNEL-MEMORY.md: a
; 128KB machine and a 640KB machine stop wanting the same feature set long
; before they stop fitting the same image, so each build has a budget of its
; own (KERN_BUDGET below).
;
; EXACTLY ONE of KERN_BIG / KERN_SMALL is in force. The Makefile always sends
; one explicitly, the default being KERN_BIG; both are positive so that no site
; has to read `%ifndef` to mean "the other build", which on the one conditional
; whose whole job is "which build is this" is where a reader gets it backwards.
;
; BOTH is an error, because it is genuinely ambiguous. NEITHER is not: it
; defaults to KERN_BIG, the same answer the Makefile gives, and that is a fix
; rather than laxity. It was an error for about an hour, and what that broke
; was tools/os88sym.py - which assembles a TEMPORARY COPY of this file to read
; the symbol map out of it and has no business knowing which product is being
; measured. The failure surfaced in a mouse script, three layers from the
; cause. Anything that assembles this file to look at it gets the shipped
; kernel, which is the only answer it can have wanted.
%ifdef KERN_BIG
 %ifdef KERN_SMALL
%error "KERN_BIG and KERN_SMALL are both defined - pick one"
 %endif
%elifndef KERN_SMALL
%define KERN_BIG
%endif

; --- the THIRD build: kern_emu (SPEC.md 9.11.7) ------------------------------
; KERN_EMU is not a fourth tier beside those two - it is KERN_BIG WITH ONE
; FEATURE ADDED, and it is written as an addition rather than as a tier for a
; reason that is structural rather than stylistic: a third exclusive name would
; fall outside every `%ifdef KERN_BIG` already in this tree, so kern_emu would
; lose the RAM disk, the extended store, the loadable drivers and the whole
; feature set it is meant to be a superset of. It is therefore ADDITIVE, and
; kernel.asm defines KERN_BIG for it here so that no site below has to spell
; `%ifdef KERN_BIG` or `%ifdef KERN_EMU` to mean "the big feature set".
;
; WHAT IT BUYS is SPEC.md 9.11's absolute pointer and nothing else. The
; backdoor protocol is 32-bit and the target machine is an 8088, so the whole
; feature is dead weight on every machine this project is calibrated against -
; which is the same argument SPEC.md 41.12 made for XMEM.DRV, one level up:
; XMEM moved the BODY out and kept a resident sniff because an 8086 can ask
; whether there is memory above 1MB. Here even the PROBE is 386 code, so there
; is nothing an XT can usefully keep, and the honest resident cost on that
; machine is ZERO rather than a small one.
;
; SO kern_big AND kern_small PAY NOTHING - not a byte of image, not a cluster
; of floppy. VMMOUSE.DRV is off their disks too ($(DRIVERS) in the Makefile),
; because a driver no resident code can name is 521 bytes of a 360KB floppy
; that had 17 clusters left.
;
; NEITHER IS AN ERROR: KERN_EMU with KERN_SMALL would be a 128KB machine
; running a 386 protocol, which is not a product, so it is refused here rather
; than left to fail somewhere below as a missing symbol.
%ifdef KERN_EMU
 %ifdef KERN_SMALL
%error "KERN_EMU and KERN_SMALL are both defined - the emulator build is big"
 %endif
 %ifndef KERN_BIG
  %define KERN_BIG
 %endif
%endif

; BANDCOMP - the 1bpp band composer (SPEC.md 5.9), and since SPEC.md 5.9.6 it
; is a KNOB again rather than kern_big's default. `make BAND=1` compiles it in
; and puts wm_draw_title's interior on ONE composed blit; the default build
; draws the bar with the fifteen primitive calls it always did.
;
; NOTHING ABOUT THE MEASUREMENTS MOVED WITH THE DEFAULT. The composed bar is
; still the faster bar on both 1bpp adapters and it still writes no pixel
; twice where the fifteen calls write the caption's own rows four times
; (SPEC.md 5.9.1, PERFORMANCE.md Sets 88-93). What sent it back to a knob is
; the byte price - 1,634 of them, .text +322, .bss +34, .cold +1,278, on a
; kernel that has to be efficient with size everywhere (CLAUDE.md's rung
; rule). KERN_SIZE follows the sum: 121,344 -> 120,320, 1,024 bytes of every
; machine's RAM handed back.
;
; The composer IS gfx_blit1 and nothing else, and gfx_blit1_x is KERN_BIG only
; (SPEC.md 5.4.2), so on kern_small this assembles to NOTHING rather than to
; two undefined symbols - kern_small keeps the fifteen calls and always did.
; `make KERN_SMALL=1 BAND=1` is therefore a small kernel with no composer in
; it, which is the answer that build can have, and the KERN_BIG test here is
; what makes it that rather than a link error.
;
; IT IS UP HERE, above kmain, because nasm's preprocessor is one pass: the
; same three lines sitting beside `%include "band.inc"` defined the symbol
; 500 lines AFTER kmain's `call band_init` had already been skipped by a
; %ifdef that could not see it. The kernel came out three bytes smaller, every
; pixel was still correct because the fallback drew them, and the composer
; simply never ran - which reads exactly like a knob that does nothing. That
; trap is SHARPER now that the composer is the opt-in half: a `BAND=1` build
; whose symbol went undefined is a plain kernel, and the picture cannot tell.
%ifdef BAND
 %ifdef KERN_BIG
%define BANDCOMP
 %endif
%endif

; SPEC.md 11.99's zoom outline is kern_big's and can be compiled out of it
; for the A/B (`make ANIMOFF=1`). ONE symbol decides it, so a call site cannot
; disagree with the body about whether the feature is in this build.
%ifdef KERN_BIG
  %ifndef ANIMOFF
    %define WM_ANIM 1
  %endif
%endif

; SPEC.md 76's THEME is kern_big's too, and for 11.99's reason one level up:
; the small build is the 128KB machine's and stands at one 512-byte step of
; KERN_BUDGET, which the palette's ~155 bytes were enough to spend outright.
; ONE symbol decides it, so a call site cannot disagree with the body - and
; the chrome pens keep their NAMES on both builds, becoming constants there
; (SPEC.md 76.13), so the ~40 sites that call them need no %ifdef at all.
%ifdef KERN_BIG
  %define OS88_THEME 1
%endif

; SPEC.md 54's file ASSOCIATIONS are kern_big's, on OS88_THEME's terms one
; block up. This takes assoc.inc
; and the associco.inc glyph table it pulls in out whole - .text, .cold and
; .bss, 2,520 bytes of them, and 2,560 of KERN_SIZE once the call sites go
; with them. SPEC.md 54.0 is the contract and says what the machine loses.
;
; IT USED TO BE "FOR A BIGGER REASON THAN THE FOOTPRINT", and that reason has
; been RETIRED AT THE SOURCE rather than by this gate. asc_use_x claimed
; ASC_KB under MEM_K_ASC - a kernel TAG and not one of the MEM_P_* purgeable
; classes - and nothing freed it; disk_mount_x calls asc_use_x and the boot
; mount is a mount, so on a machine whose whole heap is ~31KB the association
; cache stood on a bare desktop holding a tenth of it before the user had done
; anything. SPEC.md 54.7.4 made it a FILE BUFFER: the bodies are absorbed into
; 25.9's machine-wide store and asc_drop frees the claim before asc_use
; returns, so kern_big does not pay it either. The footprint is what is left,
; and it is now the whole of the argument.
;
; ONE symbol decides it, so a call site cannot disagree with the body - and
; it is resolved HERE, above every %include, because nasm's preprocessor is
; one pass and a %define beside the include it guards is made too late to
; guard anything (the trap SBDRAG's block below records paying for).
%ifdef KERN_BIG
  %define OS88_ASSOC 1
%endif

; SPEC.md 26.8's desktop SHORTCUTS - an entry dragged out of a Disk window
; onto the desktop, kept in SYSTEM.CFG across a reboot - are kern_big's alone.
; kern_small reads no SYSTEM.CFG at all (SPEC.md 51.5), so there is nothing to
; keep one in, and every site the feature touches is `%ifdef` over the new code
; so the 128KB floor assembles byte for byte what it did before. It leans on
; OS88_ASSOC (a document opens through the association route) and on DOCK_OPT
; (the grid starts at the band's left edge), both kern_big's already.
%ifdef KERN_BIG
  %define OS88_SHORTCUTS 1
%endif

; SPEC.md 51's LOADABLE DRIVERS are kern_big's too (SPEC.md 51.0). It is the
; largest single item in docs/plans/KERN-SMALL-CUT-PLAN.md's hardware group and the
; only one there that is not a device - it is the ABILITY to load one - so it
; is the one that needed a product decision rather than a measurement. The
; decision is the owner's, in their words: *"remove the drivers from the OS
; disk builds for small; we don't need to take up disk space with files we'll
; never use."* A 128KB machine has no heap to host a driver in, so the
; mechanism was closer to unusable there than merely unused, and the disk and
; the kernel now agree: $(SMALLDRIVERS) is the on-demand kernel MODULES and
; nothing else.
;
; drvvol.inc is what could not go with it - four routines that live in
; driver.inc for historical reasons and are not driver code. Resolved HERE
; for OS88_ASSOC's reason.
%ifdef KERN_BIG
  %define OS88_DRIVERS 1
%endif

; ...and with them go the sound layer's CARD tiers (SPEC.md 34.0). This is
; the half of docs/plans/KERN-SMALL-CUT-PLAN.md's A1 that OS88_DRIVERS has already
; made unreachable rather than merely unwanted: every route to an OPL2 or a
; Sound Blaster is a `[drv_svc + DSV_*]` read, and on a build that can load no
; driver that table is zero for the life of the machine. The PC SPEAKER is
; untouched and stays on both kernels - tones and beeps still play - because
; it is resident code that needs no driver at all. (PCM CLIPS are kern_big's
; since kernel size pass 8: their one caller reads its clip out of a driver's
; grant, SPEC.md 34.4.1.)
;
; A SEPARATE symbol from OS88_DRIVERS, and not `%ifdef OS88_DRIVERS` reused,
; because the two are different claims: this one says "there is no card to
; send a tone to", and a fork that wanted a driverless kernel WITH a built-in
; OPL2 would turn exactly one of them on. Resolved here for OS88_ASSOC's
; reason.
%ifdef KERN_BIG
  %define OS88_SNDCARD 1
%endif

; ...and SPEC.md 62.9's REDIRECTOR - a volume whose files come from a DRIVER
; rather than from sectors. This one is not a trade at all, which is why it
; is resolved here with the others rather than argued about at the sites: on
; a kernel with no loadable drivers such a volume CANNOT EXIST.
;
; The chain is short and every link is already in the tree. A row is stamped
; DVK_FILE only by `dsk_vol_add`; the only caller that can pass that kind is
; `osapi_vol_add`, behind `osapi_vol_fence`; and that fence walks the
; PUBLISHED CLASSES with `drv_cls_fp`, whose whole body on this build is
; `xor di, di / stc / ret` - no class is published, ever, because nothing can
; attach. So `[dsk_vkind]` is DVK_BIOS for the life of the machine and every
; redirected arm in `disk.inc`, `diskw.inc` and `loader.inc` is code the
; instruction pointer cannot reach.
;
; A SEPARATE symbol from OS88_DRIVERS for OS88_SNDCARD's reason, and the two
; are not the same claim: that one says "no .DRV can be loaded", this one says
; "no volume can be a redirected one". A fork that gave kern_small a BUILT-IN
; redirector - the parallel cable soldered in, the RAM disk resident - would
; turn exactly one of them on.
%ifdef KERN_BIG
  %define OS88_REDIR 1
%endif

; ...and SPEC.md 18.101's MOUNTED IMAGES - a volume whose sectors are a FILE's
; sectors on another volume. kern_big alone, and that one is a decision rather
; than an impossibility: the arm is 164-odd resident bytes and kern_small is
; on a diet (SPEC.md 39.27.4), so the owner kept it off the 128KB machine
; ENTIRELY - not even the dispatch test. It rides OS88_DRIVERS's `.drv` arm in
; dsk_xfer, which kern_small does not assemble at all.
%ifdef KERN_BIG
  %define OS88_IMG 1
%endif

; ...and SPEC.md 37.90's RTC LADDER, for a reason that is about the HARDWARE
; and not about the bytes (SPEC.md 37.0.1). All four rungs are chips a 128KB
; machine cannot have:
;
;   rung 1  MC146818 - arrived WITH THE AT, so not in an XT at all
;   rung 2  MM58167  - an add-on card, and the card everyone means is the AST
;                      SixPakPlus, which IS A RAM EXPANSION: a machine with
;                      one in it does not have 128KB any more
;   rung 3  RP5C01   - the first machine that shipped with one shipped with
;                      256KB
;   rung 4  int 1Ah AH=02h - "an XT BIOS implements AH=00h/01h and nothing
;                      else" (SPEC.md 37.90), so it answers nothing here
;
; So the ladder is not merely unlikely on this build, it is unreachable, and
; tier 0 - the PIT tick and a clock the user sets for the session - is what a
; 128KB XT actually has. Resolved here for OS88_ASSOC's reason.
%ifdef KERN_BIG
  %define OS88_RTC 1
%endif

; SPEC.md 66's HEAP COMPACTOR is kern_big's (SPEC.md 66.0). The 128KB machine
; keeps PURGING and gives up MOVING, which is a trade this tree measured both
; halves of rather than assumed:
;
;   what it costs there is small and getting smaller. Its two biggest
;   customers - a driver IMAGE and a driver's DONATED claim (SPEC.md
;   66.6.3/66.5.10.2) - cannot exist on a kernel OS88_DRIVERS gates out, the
;   association cache went with SPEC.md 54.0, and hibernate is kern_big's; so
;   of the kernel's own claims only the menu save-under is left movable there,
;   and it is 3.0KB alive for one menu;
;
;   and what made the case for keeping it - two 2KB Disk-window listing caches
;   stranding 21.5KB of a 48.5KB arena between them - was answered at the
;   CLAIM instead (SPEC.md 50.6.5): a cache that can be shed does not need to
;   be moved, and shedding reaches 48.5KB where compacting reached 44.5.
;
; What goes with it is the whole feature and not the call: mem_can_move and
; its five predicates, the seventeen mem_cp_* routines, mem_reloc_call,
; mem_region_reloc's table walk, OSAPI_MEM_MOVABLE's body, the WORKER PARK
; (SPEC.md 66.5) and sch_wk_restart. THE API SLOTS STAY - a small-built
; package calls the same table at the same offsets (SPEC.md 24.5) - and
; refuse, which is gfx_blit1's precedent on this kernel (SPEC.md 5.4.2.5).
;
; Resolved here for OS88_ASSOC's reason.
%ifdef KERN_BIG
  %define OS88_COMPACT 1
%endif

; SPEC.md 22.3-22.5's Cut/Copy/Paste is an ON-DEMAND MODULE on kern_small
; (SPEC.md 2.8, docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md 9.2 wave 1) and stays resident
; `.cold` on kern_big, which keeps its speed and its one contiguous boot read:
; a module is CUT OUT of kernel.bin by tools/os88mod.py and MODC_START is
; exactly where the image ends, so nothing about big's read changes.
;
; It is the FIRST feature through the seam because it needs nothing new:
; five entry points, and every caller is a user
; gesture in files.inc. ONE symbol decides it, resolved here above every
; %include for OS88_ASSOC's reason.
%ifdef KERN_SMALL
  %define FCP_MOD 1
%endif

; SPEC.md 38's Standard File dialog, on FCP_MOD's terms one block up and
; through the same seam (SPEC.md 38.0, docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md 9.2
; wave 2). SEVEN entries, because SPEC.md 13.10.5's thumb drag is kern_big's
; already and takes fdlg_onup and fdlg_ondrag with it.
%ifdef KERN_SMALL
  %define FDLG_MOD 1
%endif

; MOU_IN_BLOB - WHICH HALF of the boot overlay mouse_init's serial probe is
; assembled into (SPEC.md 2.5.3.2, 2.5.3.3): its three blocks in mouse.inc and
; OVBCALL's arm for it below both read this ONE symbol, so the section a body
; is in and the entry that reaches it cannot disagree.
;
; kern_big puts the probe in the WINDOW half and kern_small in the BLOB half,
; like the other OVBCALL bodies - EXCEPT on a kern_small KNOB build, which
; takes kern_big's arrangement for the probe alone. That is how a diagnostic
; arm gets room without a shipped kernel paying for it. The blob is
; BOOT2_SECS sectors on every build, because the boot canary rests on there
; being ONE blob length (SPEC.md 18.93.1), and a shipped kern_small fills it
; to within ~40 bytes; BOOTMARK=1 alone adds ~220 of MARKW sites to kmain_o.
; A knob build already has 1,024 bytes of window no shipped build has
; (DSK_OVLPAD, dskwin.inc: "the room a knob build needs is room a knob build
; can pay for"), and the probe - 648 bytes, the largest OVBCALL body - is
; what moves into it. It is the probe and not all six bodies because all six
; are ~1,340 bytes and overflow even the padded window; the probe alone
; clears the worst knob combination in both halves (SPEC.md 2.5.3.3 has the
; figures).
%ifdef KERN_SMALL
%ifndef KERN_KNOB
  %define MOU_IN_BLOB 1
%endif
%endif

; SPEC.md 30.5-30.6's Dock PLACEMENT and AUTO-HIDE - the left and right
; edges, the hidden strip, the Control Panel's Dock page, SYSTEM.CFG's 'DK'
; key and the DOCK.DRV module that carries the advanced half - are kern_big's
; alone. kern_small keeps the bottom Dock exactly as it was before the
; feature: every site the feature touched is `%ifdef DOCK_OPT` over the new
; code with the old code in its `%else`, so the 128KB floor pays nothing.
%ifdef KERN_BIG
  %define DOCK_OPT 1
%endif

; SPEC.md 13.10.5's thumb DRAG is kern_big's and SHIPS - `make SBDRAGOFF=1`
; compiles it out, which is WM_ANIM's shape one section up and exists to be
; diffed against rather than because anybody should build it.
;
; The symbol is resolved HERE rather than beside the %include that pulls
; os88ui.inc in - which is fdlg.inc, and fdlg.inc is included AFTER files.inc.
; A %ifdef is a PREPROCESSOR test and is answered in file order, so a define
; made down there is not made yet up here: the element assembled, the Disk
; window's whole wiring vanished, and the only tell was that adding 17 bytes
; to files.inc moved .cold by zero. It cost nothing to build and would have
; cost a session to explain from the screen.
%ifdef KERN_BIG
  %ifndef SBDRAGOFF
    %define OS88UI_SBDRAG 1
  %endif
%endif

; SPEC.md 22.27's MULTI-SELECT in the Disk window is kern_big's and SHIPS -
; `make MSELOFF=1` compiles it out, SBDRAGOFF's shape one block up and for the
; same file-order reason: files.inc and filecp.inc both test it, and the symbol
; is made here, above every %include. kern_small has none of it: there the
; copy engine is FILECP.DRV, so every operation on a set would ask for the
; system disk anyway (SPEC.md 22.26 drew the same line for the arrows).
%ifdef KERN_BIG
  %ifndef MSELOFF
    %define FM_MSEL 1
  %endif
%endif

; GFX_VGA - does this build drive a VGA at all? (docs/plans/MONO-RECLAIM-PLAN.md 2)
;
; kern_big does. kern_small DOES NOT, and that is a product decision rather
; than a size trim that happened to fit: a 128KB machine has no business with
; a VGA card in it, so the small build carries no planar renderer, no mode 12h
; and none of the Graphics Controller code that serves them. Where the other
; kern_big-only symbols here buy a FEATURE, this one removes an ADAPTER.
;
; WHAT A kern_small MACHINE WITH A VGA CARD DOES, because that is the first
; question anybody asks: it runs the card as a CGA at 640x200, and gracefully.
; vid_detect's VGA and EGA probes are gated out with everything else, so the
; walk falls through to int 11h's equipment word - which a VGA in colour mode
; answers 10b, VID_CGA - and the CGA arm sets BIOS mode 6, which every VGA
; BIOS serves because a VGA is a CGA superset. Measured on MartyPC's
; os8088_xt_vga: mode 'Mode6HiResGraphics', a structural desktop at 640x200,
; 59.6% of the screen lit. Nothing refuses and nothing goes black.
;
; IT IS NOT A KNOB. There is no `make NOVGA=1` for kern_big, because the
; bodies this gates are the ones kern_big exists to run; the A/B that matters
; is `make small`, which test-full's buildmatrix row already builds.
%ifdef KERN_BIG
  %define GFX_VGA 1
%endif

; SPEC.md 5.4.1.3's planar row decoder, on the same terms as 39.3.1's row
; table: kern_big gets it and kern_small does not, because KERN_SMALL_BUDGET
; is the figure that has to be defended and this is 1,536 bytes of it. A
; 128-256KB machine keeps vga_blit_span for every row, which is what every
; machine had before this and is still the right writer for flat art.
; NOPLANE=1 takes it out of kern_big too - that is the A/B, not a tier.
%ifdef KERN_BIG
  %ifndef NOPLANE
    %define GFX_PLANE 1
  %endif
%endif

; SPEC.md 39.25's whole-column store in sw_col, on 5.4.1.3's terms one block
; up: kern_big gets it, kern_small does not, because that build is under a cut
; (39.27.4) and this is 24 bytes of speed rather than of correctness. NOCOLFAST=1
; takes it out of kern_big too - that is the A/B, and it is the only thing that
; exercises the general body on an ordinary build, because a chrome fill is
; byte-aligned (11.94) and never reaches it.
%ifdef KERN_BIG
  %ifndef NOCOLFAST
    %define GFX_COLFAST 1
  %endif
%endif

; ...AND THE DECODER IS A VGA THING, so it may not outlive the card. Its three
; bodies (vga_p4build, vga_blit_prow, vga_prow_emit) sit inside `%ifdef
; GFX_PLANE` alone rather than inside both gates, which is correct only while
; GFX_PLANE implies GFX_VGA. Stated rather than assumed: the two are set by
; separate blocks and nothing else would notice them parting.
%ifdef GFX_PLANE
 %ifndef GFX_VGA
%error "GFX_PLANE without GFX_VGA - the planar row decoder is VGA-only code (SPEC.md 5.4.1.3); see docs/plans/MONO-RECLAIM-PLAN.md 2"
 %endif
%endif

; THE BUDGETS. Three rules, each a different quantity (docs/KERNEL-MEMORY.md):
;
;   1. kern_big fully RESIDES in KERN_RESIDENT_KB (100KB) once it is at the
;      desktop - every non-purgeable byte, no drivers, no XMS, on VGA. Guard 1
;      below is the static half, this span; tests/kernresident.py is the half
;      an assembler cannot see, a claim made at boot and never given back.
;   2. kern_small fully RESIDES in KERN_SMALL_RESIDENT_KB (64KB). Guard 1 is
;      all of it: tests/small128.py asserts that no pinned claim stands on its
;      bare desktop, so its span IS what it holds.
;   3. BOTH BOOT on 128KB - guard 5, MIN_RAM_KB below. Booting and residing
;      are separate questions: overlays and the boot order are what a 128KB
;      boot exercises, and they have broken it at a resident size well under
;      the ceiling.
;
; A BUDGET IS MET EXACTLY AT AN UPSTREAM MERGE OR A RELEASE. Branch and
; integration work may run up to KERN_GRACE over it, because features land
; whole and are size-passed after; past the grace this file refuses to
; assemble. Over a budget at a merge or a release is a conversation with the
; owner and a rework of the budgets - never a quiet raise, and never a build
; fix. `make budget-check` is that gate (tools/kernsize.py --release).
;
; Both figures are DERIVED: the span starts at KERNEL_SEG and the rule says
; where it ends, so there is nothing to choose but the KB. How the previous
; figures (129,536 and 107,520) moved, thirty-five times, is
; docs/history/KERN-BUDGET-LEDGER.md.
KERN_RESIDENT_KB       equ 100  ; rule 1
KERN_SMALL_RESIDENT_KB equ 64   ; rule 2
KERN_GRACE             equ 2048 ; branch and integration headroom over either
%ifdef KERN_BIG
KERN_BUDGET equ KERN_RESIDENT_KB*1024 - KERNEL_SEG*16
                                ; = 100,864
%else
KERN_BUDGET equ KERN_SMALL_RESIDENT_KB*1024 - KERNEL_SEG*16
                                ; = 64,000
%endif
KERN_SMALL_BUDGET equ KERN_SMALL_RESIDENT_KB*1024 - KERNEL_SEG*16
                                ; named on BOTH builds, so a kern_big report
                                ; can quote the figure the small one lives
                                ; under without assembling it

KERN_CODE_MAX equ 65536         ; the kernel's own SEGMENT: .text + .bss are
                                ; both addressed through KERNEL_SEG, so they
                                ; must fit one 64KB window. Unlike KERN_BUDGET
                                ; this is NOT a policy figure and cannot be
                                ; raised by anybody - a 16-bit offset reaches
                                ; 65,535 and that is the end of it. The boot
                                ; overlay (SPEC.md 2.5) and the cold segment
                                ; (SPEC.md 2.6) are the two ways to buy room
                                ; against it, and neither buys a single byte
                                ; against KERN_BUDGET: overlay code is still
                                ; read off the disk into the FAT window, cold
                                ; code is still resident. Confusing the two is
                                ; why they are named rather than numbered

; The relocated boot sector (boot/boot.asm). The kernel lands at 0x00600 and
; runs up through 0x7C00, where the BIOS put the sector that is reading it -
; so the sector copies ITSELF out of the way first, keeping its own offset so
; every label in it still resolves at org 0x7C00, and its stack grows down
; from its new base.
;
; **Where it goes is computed, not fixed** (SPEC.md 2.7): int 12h, the top of
; conventional RAM, the last 512 bytes the machine has. There is no
; BOOT_RELOC here any more and nothing to keep in step - KERNEL_SEG is the
; only constant the two files still share. What that changes is guard 5. A
; fixed low address made the kernel's footprint a hostage to where the sector
; happened to sit, and moves 6 through 9 of KERN_BUDGET below ate the whole
; of the gap it left; at the ceiling the two can only meet on a machine too
; small to run the OS, so what the guard asserts now is which machines those
; are. The sector refuses to relocate at all when the kernel's read would
; reach it, which is the same question asked of the machine actually in front
; of it rather than of the smallest one we support.
BOOT_SECT   equ 512             ; the sector itself, sitting at the very top
BOOT_STACK  equ 2048            ; ...and its stack, growing down from there
; MIN_RAM_KB - THE MACHINE BOTH KERNELS BOOT ON (rule 3 at KERN_BUDGET).
; Guard 5 below asks whether stage 1 can put its sector and its stack at the
; top of that machine and still read the kernel AND its stage-2 blob in
; underneath. It is a different question from residing - KERN_BUDGET's - and
; kernsize prints both lines so that neither is steered by alone.
; tests/small128.py and tests/kernresident.py boot each kernel on the 128KB
; machine, which is the half an assembler cannot answer.
MIN_RAM_KB  equ 128

%ifdef KERN_SMALL
DSK_FAT_SECS equ 2              ; TWO on kern_small: 1,024 bytes of FAT_SEG
                                ; instead of 4,608, and the largest volume it
                                ; will mount is a 360KB floppy
                                ; (docs/plans/KERN-SMALL-CUT-PLAN.md D2).
                                ;
                                ; It is an ACCEPTANCE threshold like the value
                                ; below, so this is not a buffer that might
                                ; pinch - a volume declaring more FAT sectors
                                ; is refused at mount rule 10 before a byte of
                                ; it is read. tools/os88disk.py --fatcap is
                                ; what keeps every GEOMETRY available anyway:
                                ; a 1.44MB floppy formatted with 4KB clusters
                                ; declares a 2-sector FAT, so the disk is
                                ; still 1.44MB and still a standard FAT12
                                ; volume any host mounts.
                                ;
                                ; IT COULD NOT HAVE BEEN TAKEN BEFORE SPEC.md
                                ; 37.0.1. docs/plans/KERN-SMALL-CUT-PLAN.md 7 is the
                                ; floor: `.ovl` is loaded ONTO this window and
                                ; spills through the mount buffers, so it has
                                ; to fit FAT_PARA*16 + DSK_WIN_BYTES. At 2
                                ; sectors that region is 4,352, and `.ovl`
                                ; rounded to whole sectors was 4,608 - short by
                                ; 256. Gating the RTC ladder took `.ovl` from
                                ; 4,328 to 2,789, which rounds to 3,072, and
                                ; the cap fits with 1,280 to spare. The clock
                                ; was 40% of that overlay and this is what it
                                ; was really worth.
                                ;
                                ; **AND THIS NUMBER IS THE ONE HALF OF THE
                                ; REGION THAT CANNOT MOVE.** The other half is
                                ; DSK_WIN_BYTES, which SPEC.md 22.6.3 has
                                ; since cut all the way to 512 - the listing
                                ; left `.lowbss` altogether and what is left
                                ; is the sector buffer; this one is at its
                                ; floor already, because the SMALLEST geometry
                                ; this OS boots - a 360KB floppy - declares a
                                ; 2-sector FAT, and rule 10 is an ACCEPTANCE
                                ; threshold rather than a buffer, so 1 would
                                ; refuse every volume rather than merely list
                                ; less of one. Region 1,024 + 512 = 1,536,
                                ; and the `.ovl` guard is what holds it -
                                ; `.ovl` being 1,412 here (pass 4), so this
                                ; kernel has 124 bytes of slack where kern_big
                                ; has none.
%else
DSK_FAT_SECS equ 9              ; resident FAT cap, sectors (4,608 bytes).
                                ; Exactly what the largest geometry this OS
                                ; boots or builds declares: 1.44MB = 9, 1.2MB
                                ; = 7, 720KB = 3, 360KB = 2. It is an
                                ; ACCEPTANCE threshold (SPEC.md 18.2 rule 10),
                                ; not a buffer with slack: a volume claiming
                                ; more is refused before a byte of it is read,
                                ; and every FAT16 volume there can be is
                                ; refused by this number alone (a FAT is only
                                ; FAT16 with >= 4,085 clusters, i.e. >= 16 FAT
                                ; sectors)
%endif
FAT_PARA    equ DSK_FAT_SECS * 32     ; 512 bytes = 32 paragraphs

STK0_SIZE   equ 512             ; task 0's stack - the UI task's, and so the
                                ; one every window callback, every menu track
                                ; and every file-dialog interaction runs on.
                                ; 2.08x a measured high-water mark, and the
                                ; measurement is `tests/stk0water.py` now
                                ; rather than a hand edit run once: it fills
                                ; everything below task 0's SAVED SP with
                                ; 0xCC, drives the machine, and reads the
                                ; deepest byte back. It reads 238 against
                                ; SPEC.md 15.1's 246, whose drive is heavier,
                                ; so 246 is the number to divide by.
                                ;
                                ; 2.08x SPEC.md 15.1's 246, and that is
                                ; GENEROUS by the standard this tree actually
                                ; holds: docs/plans/completed/STACK-SLOTS-PLAN.md 12 has Frotz
                                ; at 1.26x - a 22-level chain reading 240 of a
                                ; 384 slice - as the thinnest margin in the
                                ; tree, and it is accepted because a slice's
                                ; depth is now DETERMINISTIC. SPEC.md 9.10 put
                                ; both mouse ISRs on a private stack and 8.5
                                ; the ROM's int 08h chain, so what lands on a
                                ; task stack is that task's own call chain and
                                ; nothing arriving from outside it. The margin
                                ; used to be covering variance that no longer
                                ; exists.
                                ;
                                ; It is also GUARDED, which two earlier
                                ; readings of this got wrong in both
                                ; directions: SPEC.md 8.7's sch_stkbase holds
                                ; STK0_BOT at slot 0, sched_init seeds it with
                                ; SCH_MAGIC, and sch_switch compares it on
                                ; EVERY switch like every other slot's - so an
                                ; overrun here reaches sch_stkdie rather than
                                ; going quiet. tests/stk0water.py found that by
                                ; dying the moment its fill reached the magic
                                ; word.
                                ;
                                ; Guard 3 below refuses anything under 512, so
                                ; this is the floor the design allows and the
                                ; next 256 would need that guard revisited
                                ; rather than just a probe.
                                ; It is a CONSTANT now: it used to be "whatever
                                ; is left between .lowbss and the kernel", so
                                ; every byte saved anywhere below simply made
                                ; this bigger and freed nothing at all

; the file manager's per-window view cache (SPEC.md 2.3/22.1) - a heap claim
; per open Disk window now, not four pinned 4KB slots reserved from boot.
; What it buys is unchanged: a background file-manager window paints from
; memory, so wm_paint_all (no clip rect, on every window move) costs no
; floppy I/O. What changed is that a machine with no Disk window open pays
; nothing for it, and the Task Manager can bill the 3KB to the window.
VIEW_SLOTS  equ 4               ; max Disk windows = the kind's KD_CAP
VIEW_KB     equ 2               ; each cache: 1,536 bytes of entries and 64 of
                                ; reference bytes - 1,600 of 2,048, for the
                                ; SIXTY-FOUR entries SPEC.md 19 lists now. It
                                ; was 832 of 1,024 at 32 entries, and 3KB
                                ; of entries and ICONS, and SPEC.md 25.9 put
                                ; the bodies in one machine-wide store: the
                                ; window carries a BYTE an entry where it
                                ; carried 64, so three windows open now cost
                                ; less heap than one did
; --- the derived ladder -------------------------------------------------------
; Every base below is the one before it plus the MEASURED size of what it
; holds. KIMG_PARA and LOW_PARA forward-reference the section sizes at the end
; of this file, which is legal because a segment VALUE never changes an
; instruction's length - `mov ax, imm16` is `mov ax, imm16` whatever the
; immediate turns out to be, so NASM converges on the second pass.
; The image rounds up to a whole 512 BYTES, not to a paragraph, and that is
; not tidiness - it is what keeps every rung above it 512-aligned. int 13h
; moves one sector per call, which bounds a transfer to 512 bytes but does
; NOT stop one from straddling a 64KB physical boundary: only starting on a
; 512-byte boundary does that, and the DMA controller answers a straddle with
; error 09h. Every base below is an int 13h target - the FAT snapshot, the
; disk buffers, a package image, a package's file buffer out of the heap -
; and FAT_PARA (288) and LOW_PARA are both multiples of 32
; paragraphs, so aligning this one rung aligns the whole ladder. Guard 6
; proves it. It used to hold by luck: every base in the map was a round
; constant like 0x0300 or 0x2A00, and nothing said why that mattered.
KIMG_PARA   equ ((KTEXT_SIZE + KBSS_SIZE + 511) / 512) * 32   ; image + scratch
COLD_SEG    equ KERNEL_SEG + KIMG_PARA   ; cold code (SPEC.md 2.6): resident
                                ; for the whole session, but in a segment of
                                ; its own, so none of it counts against the
                                ; kernel's 64KB window. Same contract as the
                                ; boot overlay - CS here, DS still KERNEL_SEG -
                                ; and it rides the same contiguous boot read,
                                ; which is why it sits between the image and
                                ; the FAT window rather than above the stacks:
                                ; anywhere else would need the loader to skip
                                ; over .lowbss, which is nobits and not in the
                                ; file at all
COLD_PARA   equ ((COLD_SIZE + 511) / 512) * 32
FAT_SEG     equ COLD_SEG + COLD_PARA   ; mount-time FAT snapshot
                                ; (SPEC.md 2.1/18), reached via ES ONLY,
                                ; never DS; dsk_next_clus is the one reader
LOW_SEG     equ FAT_SEG + FAT_PARA    ; .lowbss (task stacks + disk buffers)
                                ; and, on top of it, task 0's own stack
LOW_PARA    equ ((KLOW_SIZE + STK0_SIZE + 511) / 512) * 32
STK0_TOP    equ KLOW_SIZE + STK0_SIZE - 2   ; task 0's stack top, growing down
STK0_BOT    equ KLOW_SIZE       ; ...and the floor it grows down ONTO, which is
                                ; the top of .lowbss - the other tasks' slices,
                                ; the glyph table and the disk buffers. Slot 0
                                ; of sch_stkbase holds this address, sched_init
                                ; seeds SCH_MAGIC here on every build, and
                                ; sch_switch checks it on every switch like
                                ; any other slot's (SPEC.md 8.6.1), so an
                                ; overrun reaches sch_stkdie rather than
                                ; going quiet; guard 3 proves the two cannot
                                ; meet
VGABUF_SEG  equ LOW_SEG + LOW_PARA   ; THE PLANAR DECODER'S BUFFERS (SPEC.md
                                ; 5.4.1.3), and a rung of their own because
                                ; they are the one part of the kernel a
                                ; machine with no VGA has no use for
                                ; (SPEC.md 39.22).
                                ;
                                ; THE TOPMOST RUNG, and that is the whole
                                ; design: `.lowbss` is the last thing under
                                ; the heap, so a byte that leaves it lowers
                                ; the heap FLOOR directly - there is nothing
                                ; above it to relocate. mem_init seeds
                                ; [mem_base] under this rung when
                                ; [vid_avail] & VID_A_VGA is clear, which is
                                ; a conditional and not a ladder that slides:
                                ; the floor was ALREADY a runtime word and
                                ; HEAP_SEG is only what it is seeded with.
                                ;
                                ; NOT A HEAP CLAIM, which was the first shape
                                ; proposed and the wrong one. These bytes ARE
                                ; system memory; a claim would make SPEC.md
                                ; 28's RAM view call them a program's, spend
                                ; one of MEM_MAX's 32 records, and buy a
                                ; refusal path that cannot fire - the claim
                                ; would be made at boot, so the only machine
                                ; that could miss is one with under 1KB of
                                ; heap, which is a machine that does not run.
%ifdef GFX_PLANE
VGABUF_PARA equ 32              ; 512 bytes, and A MULTIPLE OF 32
                                ; PARAGRAPHS on purpose: the mono floor is
                                ; HEAP_SEG - VGABUF_PARA, so 32-alignment
                                ; holds in BOTH cases by construction and
                                ; guard 6 keeps proving it. Guard 2d checks
                                ; the multiple rather than trusting this
                                ; comment, and checks the buffers fit.
%else
VGABUF_PARA equ 0               ; NO PLANAR DECODER, NO RUNG. GFX_PLANE is
                                ; kern_big's (SPEC.md 5.4.1.3), so kern_small
                                ; and every NOPLANE build declare nothing here
                                ; and must not pay 1,024 bytes of footprint
                                ; for an empty reservation. Zero is a multiple
                                ; of 32, so guard 2d still passes and
                                ; mem_init's conditional folds to nothing.
%endif
KERN_END    equ VGABUF_SEG + VGABUF_PARA   ; ...and there the kernel stops
KERN_SIZE   equ (KERN_END - KERNEL_SEG) * 16   ; what KERN_BUDGET measures

HEAP_SEG    equ KERN_END        ; the claim heap (SPEC.md 50) starts where
                                ; the kernel ACTUALLY ends, not where a
                                ; budget said it might, and runs to whatever
                                ; int 12h reports. Package regions and data
                                ; claims share it from opposite ends
                                ; (SPEC.md 50.3); nothing up here has a fixed
                                ; address any more

; --- CPU tiers and memory above 1MB (SPEC.md 41) -----------------------------
; None of this exists on tier 0, which is the target machine: an 8088 has no
; A20 line, nothing above linear 0x0FFFFF, and every routine keyed off these
; constants returns having touched no port. The tier is INFORMATION, not
; permission - kernel code branches on the verified feature bits and packages
; branch on the KB figure from osapi_xmem_caps (SPEC.md 41.1/41.8).
CPU_8086    equ 0               ; tier 0: 8086/8088. No A20, no HMA, no store.
                                ; The default [cpu_tier] and the fallback.
CPU_286     equ 1               ; tier 1: A20 gate + HMA, int 15h AH=88h
                                ; sizing and AH=87h block move (SPEC.md 41.5)
CPU_386     equ 2               ; tier 2: all of tier 1, plus unreal mode -
                                ; a 4GB data limit on FS/GS (SPEC.md 41.4)
XM_MAX_BLKS equ 8               ; the pool's fixed block table, entries: a
                                ; bulk store for a handful of large claims,
                                ; not a malloc (SPEC.md 41.5). It is here and
                                ; not only in the overlay because BOTH builds'
                                ; xm_caps report it as the free-entry count -
                                ; a table with nothing in it, above a pool
                                ; with nothing in it
;
; HMA_SEG, HMA_MIN_OFF, HMA_BYTES and XM_HMA_KB LIVED HERE AND ARE THE
; OVERLAY'S NOW (SPEC.md 41.12, drivers/xmem/xmem.asm). The kernel does not
; open the A20 gate, claim the HMA or size the pool any more, so it has no use
; for the addresses of a window it cannot reach - and a constant mirrored in
; two files that only one of them reads is the shape that drifts. What the
; kernel keeps is the TIER above, which is a fact about the CPU that ten
; readers across the tree branch on and which has nothing to do with the
; store.

; =============================================================================
; Section layout (SPEC.md 2.1) - declared here, once, with attributes; every
; module afterwards switches with a bare `section .text` / `.bss` / `.lowbss`.
; NASM's -f bin resolves the attributes at layout time, so a forward reference
; to .text below is fine.
;
;   .text     the kernel image, org 0, KERNEL_SEG. ALL of it: there is no
;             .fartext any more. Cold modules used to be copied down to a
;             second segment below the kernel to buy window space, and the
;             reserve that mechanism needed was 10,752 bytes of low memory for
;             a 5,455-byte blob - so it cost more RAM than it saved the moment
;             the kernel stopped being the thing that was short (SPEC.md 33).
;   .bss      kernel scratch, KERNEL_SEG, vfollows=.text.
;   .lowbss   task stacks and the disk buffers, in LOW_SEG (SPEC.md 2.1) -
;             above the kernel image now, not below it. vstart=0, addressed
;             through SS or ES, never DS.
; =============================================================================
; --- STAGE 2, THE LOADER (SPEC.md 2.9) ---------------------------------------
; The first BOOT2_SECS sectors of KERNEL.SYS are not the kernel at all: they
; are the rest of the boot loader, which the 512-byte sector reads to a scratch
; segment and jumps to. Everything the sector cannot afford lives here - the
; int 1Eh parameter table, read_run's four bounds, the 286 head-cross gate,
; SPEC.md 18.93.1's canary and its shorten-and-reload.
;
; It is at file offset 0 because it must be aboard FIRST and the sector reads
; forward from the file's first sector; `.text` therefore starts at BOOT2_PAD
; and every file offset below it is shifted by the same amount. THE MEMORY
; LADDER IS NOT: KIMG_PARA and everything under it are computed from section
; SIZES, and stage 2 reads the kernel's remainder to KERNEL_SEG:0000, so
; `cold_entry` lands where it always did.
;
; ITS SIZE IS A RUNG, not a measurement, for SPL_RESIDENT's reason: the boot
; sector has to know how many sectors to read before anything has told it, so
; the number is a constant both ends share and an assertion at the bottom of
; this file is what keeps them honest.
; THE BLOB IS ONE SECTION (SPEC.md 2.9.14). `.boot2` is the loader, the loading
; screen AND the boot overlay's blob half - every boot-only body that has to
; outlive drv_boot's first mount. The overlay used to be a section of its own
; at a hand-set `OVL_AT` inside the blob, with the same segment, the same
; lifetime, the same read and the same checksum as the loader: the only thing
; the split did was hold 359 bytes idle below it while the half above it ran
; out. One section is one pool, measured against one guard at the foot of this
; file, and stage 2's entry stays at offset 0 because the jump to boot2_entry
; that splash.inc already needed is emitted where the section is declared.
;
; ONE RUNG, hand-set and asserted at the foot of this file: BOOT2_SECS is how
; long the blob is. Stage 1 has to know it before anything has told it -
; SPL_RESIDENT's reason - and the Makefile reads it out of this file with one
; `sed` so the two cannot disagree.
; THERE IS ONE BLOB LENGTH NOW, AND THAT IS WHAT KSIG_OFF RESTS ON.
; `BOOT2_SECS_STARS equ 9` stood here, with a second `sed` in the Makefile to
; find it, because SPLSTARS' twinkle wanted 4,193 bytes of a 4,096-byte blob -
; over by 97, WHEREVER OVL_AT fell, so moving the split alone could not pay for
; it (SPEC.md 15.3.8.5). The splash's own size pass took that arm's `.boot2`
; from 2,768 to 2,568 and the blob to 3,993, and the sector went back.
;
; WHAT IT WAS COSTING WAS NOT THE SECTOR. The boot canary (SPEC.md 18.93.1) is
; a word at a fixed MEMORY offset whose FILE sector has to cross a head on
; every shipped geometry, and the file sector is this constant further in - so
; an offset legal for a 4,096-byte blob and an offset legal for a 4,608-byte
; one had to be the SAME offset. That intersection is four sectors wide and
; all four sit at the top of `.text`; the single-length band is seven wide and
; reaches memory 6,656. A second blob length pinned the canary to the part of
; `.text` a size pass eats first, which is how it came to have thirty bytes of
; headroom left. The Makefile's KSIG_OFF block is the other end of this.
; The ninth sector holds the boot copy of Dock layout setup (SPEC.md 30.5).
; The whole blob is freed before the desktop; KERN_BUDGET is unchanged. It is
; nine on kern_small too, which has no such copy: KSIG_OFF (the Makefile) is
; one constant for every kernel, and it names file sector 21 only at nine.
; TEN since SPEC.md 6.0.1: the tenth sector is `.boot2`'s, for the timed font
; pick (font_init in the blob on both kernels), and KSIG_OFF moved one memory
; sector down with it (6144 -> 5632) so the FILE sector is still 21. It sits
; inside the same int 13h run on all four geometries (tests/unit/t_blobruns.py).
; ELEVEN ON A KNOB BUILD (SPEC.md 2.5.3.3): a knob kernel ships nowhere, so its
; blob may grow where a shipped one may not - the extra sector is overlay room
; for diagnostics (BOOTMARK=1's MARKW sites) that SPEC.md 39.28's 'VC' key left
; 9 bytes short. The literal stays FIRST on the line: the Makefile, os88layout,
; t_blobruns and kzboot all read the shipped figure off it, and the Makefile
; adds the knob's sector in the same block that defines KERN_KNOB, so the two
; cannot disagree. KSIG_OFF is derived from the total there, so the canary
; stays on file sector 21 at either length.
; ...AND ELEVEN ON kern_emu (SPEC.md 9.11.2.1), for the knob's reason one step
; along: it is a shipped kernel, but only an emulator boots it - its probe is
; 386 code - so its stage 1 reading one sector more costs no period machine
; anything. What it buys is vmm_boot_x in the blob instead of resident `.cold`.
; The Makefile adds the sector for KERN_EMU in the same block as for a knob.
%ifdef KERN_KNOB
  %define BOOT2_KNOBSEC 1
%elifdef KERN_EMU
  %define BOOT2_KNOBSEC 1
%else
  %define BOOT2_KNOBSEC 0
%endif
BOOT2_SECS  equ 10 + BOOT2_KNOBSEC ; sectors stage 1 reads before it jumps - the
                                ; loader, its screen and the blob half of
                                ; the boot overlay, one section to BOOT2_PAD.
                                ; THE TOTAL IS THIS, so
                                ; read those two rather than a number written
                                ; out in prose: this comment used to say
                                ; "NINETEEN: five for the loader, FOURTEEN for
                                ; the overlay. It was THIRTEEN" against a
                                ; constant that is 8, and SPEC.md 2.9.12 and
                                ; 15.3.8.5 still carry the same stale count
                                ; (2.5.3's table is the one that is right).
                                ; Before either, it was 4 with the overlay in
                                ; the image; the disk is unchanged every time,
                                ; the sectors having left there.
                                ;
                                ; The six that took it from 13 are SPEC.md
                                ; 2.9.12's, and they are not for a feature:
                                ; they are the room to move twenty-five
                                ; BOOT-ONLY bodies out of `.text` and `.cold`
                                ; into `.boot2`, where mem_unblob gives them back
                                ; to the heap at the end of kmain.
                                ; docs/plans/LAST-DROP-BYTES.md is the register of
                                ; them and names the caller that makes each one
                                ; boot-only. 18 is 36 bytes short of the whole
                                ; register; 19 takes it with room over.
                                ;
                                ; WHAT IT COST, measured off the images by
                                ; tests/unit/t_blobruns.py and not reasoned
                                ; from one geometry: ONE extra int 13h on 360KB
                                ; and one on 720KB, none on 1.44MB, plus the
                                ; six in-run sectors - which land on EVERY
                                ; geometry, ~144 ms at PERFORMANCE.md's 24 ms a
                                ; sector, and are the half a call table hides.
                                ; All of it before the first splash pixel.
                                ;
                                ; 20 and 21 change no call on any geometry and
                                ; are deliberately NOT taken - they are there
                                ; when something needs them. 22 buys 1.44MB a
                                ; third call and 23 buys 720KB a fourth, and
                                ; that is where the next conversation is

BOOT2_PAD   equ BOOT2_SECS * 512

section .boot2   start=0 vstart=0
; --- `.boot2` OPENS HERE, and that is not a formality (SPEC.md 2.9) ---------
; Stage 1 jumps to STAGE2_SEG:0000, so offset 0 of this section has to be the
; loader's entry. `-f bin` fills a section in the order the assembler MEETS its
; blocks, and the boot overlay's blob half (the `section .boot2` blocks in the
; modules) and splash.inc are both met long before boot2.asm, which is
; %included at the FOOT - so without these three bytes offset 0 is whatever
; label the first of them starts with, and stage 1 lands in the middle of it.
; It did once, with splash.inc: the guest ran off into its own segment with SP
; intact and CS:IP wandering, which is what executing the wrong offset looks
; like from outside. They are at the declaration and not beside the splash's
; %include because since SPEC.md 2.9.14 the overlay's blocks come first.
    jmp boot2_entry
section .vgabuf  nobits vstart=0
section .lowbss  nobits vstart=0
section .bss     nobits vfollows=.text valign=1
section .text    start=BOOT2_PAD vstart=0
section .cold    start=COLD_START vstart=0
; --- .ovl: THE BOOT OVERLAY'S OTHER HALF (SPEC.md 2.5.3) --------------------
; `.boot2` is dead at spl_finish, when mem_unblob gives the blob back. `.ovl` is
; dead EARLIER - at drv_boot's first mount, which is what takes the FAT window
; and the mount-owned buffers above it (SPEC.md 2.1.2). So a body whose every
; caller runs before the mount costs no blob byte at all: it rides the kernel's
; own contiguous read, lands on FAT_SEG, and is forfeit where it lies.
;
; `start=` is a FILE offset - the FAT window's, which is the image rung plus
; the cold rung - so NASM emits the cold rung's padding as zeros and stage 2's
; single read puts this exactly where the ladder says FAT_SEG is. That is
; SPEC.md 2.5's original trick, which 2.5.1 gave up for LIFETIME and not for
; room; the split is what buys the lifetime back for the half that needs it.
OVL_START equ BOOT2_PAD + (FAT_SEG - KERNEL_SEG) * 16
section .ovl    start=OVL_START vstart=0
; --- on-demand kernel modules (SPEC.md 2.8, docs/plans/completed/ONDEMAND-PLAN.md) -----------
; Code that is NOT in KERNEL.SYS at all. Each of these is assembled here, with
; this kernel's data addresses, and then SPLIT OUT of the binary by
; tools/os88mod.py into a file of its own; the kernel loads one into a heap
; claim when the feature is asked for and frees it when the feature is done.
;
; They are `.cold` in every respect that matters - vstart=0, CS of their own,
; DS still KERNEL_SEG, out through the cw_* shims - which is exactly why they
; can leave: cold code was already position-independent at paragraph
; granularity and nothing but the constant in its inbound thunk tied it to
; COLD_SEG. Being part of THIS assembly is the whole trick, and it is what
; keeps a module's view of kernel data from being a second opinion.
;
; They are LAST in the file and unpadded, so truncating at MODC_START yields
; byte for byte the kernel.bin that would have been emitted without them.
section .modc    start=MODC_START vstart=0
section .modu    follows=.modc vfollows=.modc align=1 valign=1
                                ; ...CTRL.DRV's SECOND half, the panel's UI,
                                ; straight after the first in the file and in
                                ; the address space (SPEC.md 2.8.7). `.modc`
                                ; is the header and the SETTINGS CORE - what a
                                ; desktop gesture needs to write SYSTEM.CFG -
                                ; so MOD_SETS reads the file's first MODS_SIZE
                                ; bytes and stops. `.modu` may name anything
                                ; in `.modc`; `.modc` may name NOTHING in
                                ; `.modu` (tools/os88ovlchk.py), because on a
                                ; core-only load it is not there
%ifdef OS88_DRIVERS
section .modcb   nobits vfollows=.modu ; CTRL.DRV's own scratch: the SYSTEM.CFG
                                ; writer's file buffer (driver.inc, CFG_DATA
                                ; cpc). NOT zeroed - MOD_BSS stays kern_small's
                                ; FCP_MOD alone - because its one user fills it
                                ; before it reads it (kernel size pass 8)
%endif
section .modf    start=MODF_START vstart=0
section .modl    start=MODL_START vstart=0
%ifdef KERN_BIG
section .modh    start=MODH_START vstart=0
section .modhb   nobits vfollows=.modh ; HIBER.DRV's own scratch: the DOS
                                ; handoff's record copy and the pointer file's
                                ; buffer (hiber.inc). NOT zeroed, `.modcb`'s
                                ; terms: every cell is written before it is read
%endif
%ifdef FCP_MOD
  %define MOD_BSS 1             ; ...and THIS is what mod_need's zeroing hangs
                                ; off: a build with no module bss has nothing
                                ; to zero and must not carry the code. Widen it
                                ; when a second module declares one.
section .modp    start=MODP_START vstart=0
; ...AND ITS OWN BSS (docs/plans/MODULE-SELFCONTAIN-PLAN.md 3.1). A module runs
; from a heap claim and mod_need sizes that claim in whole KB
; (mem_bytes_kb_x), so every image already owns memory past its own end for
; exactly as long as it is loaded - and a module's scratch can live THERE
; instead of in the kernel's `.bss`, where it is resident whether the feature
; has ever been opened or not.
;
; `nobits`, so not one byte of it reaches the floppy; `vfollows`, so its
; labels are offsets from the same claim base the image's are and `[cs:x]`
; reaches them. What may live here is SCRATCH and not PENDING state
; (MODULE-SELFCONTAIN-PLAN 3.4): anything that has to outlive mod_drop - the
; clipboard's own selection - stays in the kernel.
section .modpb   nobits vfollows=.modp
%endif
%ifdef FDLG_MOD
section .modd    start=MODD_START vstart=0
%endif
%ifdef DOCK_OPT
section .modk    start=MODK_START vstart=0
%endif
%ifdef KERN_BIG
section .modx    start=MODX_START vstart=0
section .modw    start=MODW_START vstart=0
%endif
section .modmap  start=MODMAP_START vstart=0
section .text

; =============================================================================
; The boot overlay (SPEC.md 2.5): code that runs ONCE, from kmain, and is
; never reachable again.
;
; Its blob half is assembled into `.boot2` - INSIDE THE BLOB (SPEC.md 2.9.6,
; 2.9.14) - in the same section as the loader and the splash, because the blob
; is one segment and every label in it is an offset from [spl_fseg]. Stage 1
; reads the whole blob before it jumps and stage 2 copies it to HEAP_SEG, so
; the overlay is resident from the first instruction of the boot and stays
; until kmain gives the blob back after spl_finish. It costs no RAM after that
; at all, and - this is the point - none of it counts against KERN_CODE_MAX.
; Its OTHER half, `.ovl`, is the bodies dead at the first mount (below).
;
; **It used to ride the kernel's own read into the FAT window** and be copied
; out of it into a high heap claim (`ovl_relocate`, `MEM_K_OVL`) before the
; first `disk_mount` could overwrite it. That claim was a 4KB transient at the
; TOP of the heap, which is the one place a transient strands what it leaves
; behind - the freed run sat above drivers that can never move and
; mem_compact packs downward, so it rejoined nothing. Riding the blob instead
; costs the same disk, the same lifetime, and no claim.
;
; NASM emits the gap between the blob's end and BOOT2_PAD as zeros, which is
; why stage 1 can read a flat rung.
;
; The contract, and all of it matters:
;   CS = the blob's segment ([spl_fseg]), DS = KERNEL_SEG, SS = LOW_SEG.
; DS is the kernel's, so every reference to kernel data - [cpu_tier], [xm_kb],
; the eighteen snd_* words - assembles and runs exactly as it did in .text.
; That is what makes this nearly free: nothing to marshal, no es: prefixes,
; nothing rewritten. The price is the mirror image: the overlay may NOT reach
; its own labels through DS. It has no data of its own today; anything added
; needs a cs: override, and NASM will not warn about it.
;
; Calls out of the overlay into resident code must be FAR, so a resident
; routine an overlay entry needs gets a four-byte call/retf shim (ovw_*).
; Calls the other way come through the stubs below, which is what lets every
; routine that moved keep its near `ret` and change in no other way.
; =============================================================================
section .boot2
; THE FIVE SHIMS THAT USED TO BE HERE ARE GONE (SPEC.md 2.6.1, 2.5.2). Each was
; `call <body> / retf` for a body that is ALSO in the blob, in cpudet.inc,
; xmem.inc, desk.inc, snd.inc and clock.inc - four bytes apiece to own a far
; return the body can own itself, which is the deletion 2.6.1 made eighty-four
; times over on the `.cold` side and had never been made here. kmain names each
; body directly now and nothing near-calls any of them, which is what
; os88ovlchk's return-kind and blob-entry rules check.
;
; `ovl_base` went with them; nothing in the tree ever named it.

section .text

; The debug registry's tags (SPEC.md 57). Two ASCII characters, and each one
; is ALSO the first word of the block it names, so a reader can check that the
; offset it followed landed where it meant to.
DBG_TAG_MOUSE equ 0x4F4D          ; 'MO' - SPEC.md 9.4.2
DBG_TAG_DISK  equ 0x4444          ; 'DD' - SPEC.md 18.94
DBG_TAG_CLOCK equ 0x4B43          ; 'CK' - SPEC.md 37.92
DBG_TAG_VIDEO equ 0x4456          ; 'VD' - SPEC.md 57.4
DBG_TAG_FDD   equ 0x4446          ; 'FD' - SPEC.md 57.5
DBG_TAG_BUILD equ 0x4449          ; 'ID' - SPEC.md 57.6
DBG_TAG_BPROF equ 0x5042          ; 'BP' - SPEC.md 15.5, BOOTPROF=1
DBG_TAG_STKD  equ 0x4453          ; 'SD' - docs/plans/completed/STACK-SLOTS-PLAN.md, STKDIAG=1

; =============================================================================
; Fixed entry points
; =============================================================================
cold_entry:
    jmp kmain

    times 0x04 - ($ - $$) db 0
boot_cylrun:                    ; 0060:0004 - THE BOOT SECTOR'S FINDING about
    dw 0                        ; this machine's FDC (SPEC.md 18.93.1): NON-ZERO
                                ; means a transfer run crossed a head and came
                                ; back RIGHT, so dsk_xfer may do the same. The
                                ; loader writes it on that one path and on no
                                ; other, so ZERO - the image's own value - is
                                ; every other case at once: it fell back, it was
                                ; too small to have crossed, it was built
                                ; TRACKRUN=1, the machine is not an XT, or the
                                ; boot sector predates the canary. All of them
                                ; want the cautious answer and all of them get
                                ; it. **Do not turn this back into a compare
                                ; against a volume's spt** - the loader loaded
                                ; from ONE volume and the kernel mounts others.
                                ;
                                ; 0x06..0x07 stay free: the mouse instrument
                                ; used to have a fixed word at 0x0006 and is
                                ; now an entry in the registry at 0x000E (57)
    times 0x08 - ($ - $$) db 0
                                ; 0x08..0x0B ARE FREE AGAIN. `jmp near
                                ; spl_tick` answered here for the boot sector,
                                ; which had to name a kernel symbol and had no
                                ; other way to. Stage 2 draws the splash and
                                ; calls it NEAR, in its own segment, so the
                                ; pinned entry has no caller left (SPEC.md 2.9.4)

    times 0x0C - ($ - $$) db 0
boot_ticks:                     ; 0060:000C - the boot timer (SPEC.md 15.4).
    dw 0xFFFF                   ; The BOOT SECTOR writes the BIOS tick it read
                                ; before it loaded anything; kmain replaces it
                                ; IN PLACE with the elapsed count once the
                                ; first desktop frame is on the glass, and
                                ; nothing can read it in between. 0xFFFF is
                                ; "never stamped" - an image whose boot sector
                                ; predates the timer, which must report
                                ; unknown rather than a plausible wrong
                                ; number. The offset is fixed because the boot
                                ; sector has no other way to name it, exactly
                                ; like the two jumps above and the table below.

    times 0x0E - ($ - $$) db 0
dbg_reg_at:                     ; 0060:000E - THE DEBUG REGISTRY (SPEC.md 57)
    dw dbg_reg                  ; A fixed word for boot_ticks' reason: a TEST
                                ; package has to find kernel state without an
                                ; API slot, and a slot that exists in one
                                ; build and not another is an ABI that depends
                                ; on a knob (SPEC.md 20.8). There was one word
                                ; per instrument until the first paragraph
                                ; filled up at two; this is the indirection
                                ; that stops the third having nowhere to go.

    times 0x10 - ($ - $$) db 0  ; the table must land exactly at 0x0010

; =============================================================================
; os8088 API jump table (SPEC.md 20.3)
;
; Loaded programs FAR-call these absolute offsets, starting at 0x0010. Since
; kernel size pass 4 there are TWO cell sizes (SPEC.md 20.3): a HOT cell -
; one some package calls per frame, per draw or per event - keeps the 8-byte
; OSAPI_SLOT (or the 7-byte OSAPI_XCELL), the fastest segment switch there
; is; every other cell is the 6-byte rare form, `push bp / call api_r<kind> /
; dw target`, which trades ~18 us a call for two bytes. A third shape, the
; INLINE cell (OSAPI_ICELL, below the macros), is a routine that was one
; `mov` and a `ret` with no other caller, and IS that routine - shorter and
; faster than either size. The offsets are therefore NOT 0x0010 + 8n:
; apps/os88api.inc is the address map, and tests/unit/t_api_abi.py decodes
; this table by cell length and checks every published name against it. The order below IS the ABI for as long as it
; stands; renumbering it is a flag day that rebuilds every package in the
; tree (SPEC.md 20.8 rule 4).
;
; Why 8 bytes and a DS switch. Since SPEC.md 20.1 a package lives in its OWN
; segment, so CS and DS are the package's on the way in and must be the
; KERNEL's while kernel code runs. `push cs / pop ds` is the two-byte way to
; say that (the table is .text, so CS is KERNEL_SEG here), and it clobbers no
; register - which matters, because every register in this ABI is an argument
; to something. The cell is exactly:
;
;       push ds / push cs / pop ds / call near target / pop ds / retf
;
; POP and RETF touch no flags, so a routine's CF answer survives the return
; (menu_win_set and inst_pkg_alive contractually preserve the FLAGS word).
;
; Three cells need more than that and jump to a stub below instead:
;   X  the caller's DS must reach the kernel routine (a package pointer it
;      has to dereference) - the stub puts it in ES
;   N  the caller passes a NUL name at DS:SI while ES:BX is already spoken
;      for by a data buffer - the stub STAGES the name into kernel scratch,
;      the dsk_get_dir idiom of SPEC.md 2.1
; =============================================================================
%macro OSAPI_SLOT 1                 ; 8 bytes exactly (a hot cell)
    push ds
    push cs
    pop ds
    call %1
    pop ds
    retf
%endmacro

%macro OSAPI_XCELL 1                ; 7 bytes: %1 is a .text routine (hot)
    push bp                         ; 55        the CALLER's, under the frame
    mov bp, %1                      ; BD lo hi
    jmp strict near api_x           ; E9 lo hi
%endmacro

; **A CELL WHOSE ROUTINE IS `.cold` NAMES THE COLD BODY** (SPEC.md 20.3.2).
; It used to name a resident thunk - `foo: call COLD_SEG:foo_x / ret`, six
; bytes of .text - and forty of those thunks were reached by NOTHING but their
; own cell. The rare cold shapes below (RCXCELL, RNCELL, RCSLOT) reach the
; cold body through `api_far` instead, one trampoline that builds COLD_SEG:BP
; on the caller's stack the way drv_pkg_disp builds a driver's far call: the
; body's own `retf` returns through the far frame the `call KERNEL_SEG:
; api_far` pushed. `push` and `retf` touch no flags, so a CF answer still
; survives. No hot cell's routine is `.cold`, so there is no 8-byte cold
; shape any more. OSAPI_FCELL is the one cell with nothing to do but cross:
; lz_decomp_x takes the caller's own DS:SI and ES:DI, so the far call IS the
; whole cell. tests/unit/t_api_abi.py decodes every one of these shapes out
; of kernel.bin.

; **`apic_*` NAMES A CELL, for a caller inside the kernel**
; (docs/plans/MODULE-SELFCONTAIN-PLAN.md 5). An on-demand module runs from a
; heap claim with a CS of its own, so it reaches kernel code through a far
; call - and where a cell ALREADY publishes the routine it wants, that cell is
; the door and the private `cw_*` shim below is a second one. A new cell is at
; least six bytes (the rare form) and a shim is four, so this is only ever
; worth doing where the cell exists already: publishing a routine in order to
; delete its shim spends 6 to save 4 and commits the SDK for ever.
;
; The label is what makes it safe to say. A `%define` of the slot NUMBER would
; be that number written down twice - tests/unit/t_mirror.py's whole subject -
; where a label is DERIVED and follows the cell if the table is ever renumbered.
; It emits nothing.
;
; `OSAPI_SLOT` is `push ds / push cs / pop ds / call / pop ds / retf`, which
; for a caller whose DS is already KERNEL_SEG is the shim's semantics exactly,
; plus four instructions; `pop` and `retf` touch no flags, so a CF answer still
; survives. `OSAPI_XCELL` additionally sets ES = the caller's DS and restores
; it, which for a module is ES = KERNEL_SEG - what a kernel-owned template
; wants, and what the one XCELL caller here used to do by hand.
; ---- the RARE cells (SPEC.md 20.3): six bytes, the target behind the call ----
%macro OSAPI_RCELL 2                ; 6 bytes: push bp / call <kind> / dw target
    push bp
    call %1
    dw %2
%endmacro
%macro OSAPI_RSLOT 1
    OSAPI_RCELL api_rs, %1
%endmacro
%macro OSAPI_RXCELL 1
    OSAPI_RCELL api_rx, %1
%endmacro
%macro OSAPI_RCXCELL 1
    OSAPI_RCELL api_rxc, %1
%endmacro
%macro OSAPI_RNCELL 1
    OSAPI_RCELL api_rn, %1
%endmacro
%macro OSAPI_RCSLOT 1
    OSAPI_RCELL api_rsc, %1
%endmacro
%macro OSAPI_JCELL 1
    jmp strict near %1
%endmacro
%macro OSAPI_FCELL 1
    call COLD_SEG:%1
    retf
%endmacro

; ---- the INLINE cells (SPEC.md 20.3): THE CELL IS THE ROUTINE ---------------
; Fifteen routines were ONE memory access and a `ret` - a getter, a setter or
; a window-record word - with no caller anywhere but their own cell. The cell
; runs with CS = KERNEL_SEG (the table is .text and every caller far-calls
; KERNEL_SEG:cell), so `mov ax, [cs:ticks] / retf` IS the whole call: no DS
; switch, no near call, no body. It honours the SLOT contract exactly - DS,
; ES, BP and every register but the documented output come back untouched,
; because none of them is touched, and a `mov` and a `retf` write no flags -
; and it is SHORTER and FASTER than either sized cell: OSAPI_SET_COLOR
; measures 52-57 cycles inline on MartyPC's 4.77 MHz 8088 against 159-162
; as the hot SLOT it was, and a rare cell is ~85 dearer than a hot one.
;
; THE LENGTH IS ABI. An inline cell is as long as its instructions, and one
; SDK serves every build (kern_big, kern_small, kern_emu and every knob), so
; a cell whose body is %ifdef'd must come out the SAME LENGTH on each or every
; slot after it answers at a different address on one kernel. OSAPI_ICELL
; declares the length and OSAPI_IEND refuses to assemble a cell that is not
; exactly that long - the whole table's addresses are the sum of these.
; tests/unit/t_api_abi.py decodes the one memory operand and checks it names
; the variable (or record field) the published name promises.
%macro OSAPI_ICELL 1                ; %1 = the cell's length, retf included
    %push osapi_icell
    %assign %$len %1
%$start:
%endmacro
%macro OSAPI_IEND 0
    retf
    ; THE LENGTH CHECK. NASM's %if cannot read a context-local label, so the
    ; two TIMES below are the assertion: each is zero exactly when the cell is
    ; its declared length, and one of them goes NEGATIVE - "TIMES value -n is
    ; negative", an error - when it is not, on whichever build it is not.
    times ($ - %$start) - %$len db 0
    times %$len - ($ - %$start) db 0
    %pop
%endmacro

; ---- CLC_OR_STC .label: THE CF-RETURN TAIL IN TWO BYTES (size pass 9) -------
; The tree's commonest exit is
;       clc / jmp short .out / .no: stc / .out:      4 bytes, 2 + 15 cycles
; and this is the same exit as
;       CLC_OR_STC .no / .out:                       2 bytes, 4 cycles
; It emits `db 0xA8` and then `.no: stc`: falling in executes `test al, 0xF9`
; (A8 F9 - the stc is its immediate), which CLEARS CF and touches no
; register, while a jump to .no executes the `stc`. One macro rather than a
; bare `db` so the swallowed byte is always the stc it was written for.
; THE ONE DIFFERENCE FROM `clc`: `test` also writes ZF, SF, PF and OF, where
; `clc` writes CF alone - so this goes only where the routine answers in CF
; and nothing reads another flag at the return (each site was checked).
%macro CLC_OR_STC 1
    db 0xA8                     ; test al, imm8 - the imm8 is the stc below
%1: stc
%endmacro

osapi_table:
    OSAPI_SLOT gfx_lock           ; 0x0010
    OSAPI_SLOT gfx_unlock         ; 0x0018
    OSAPI_SLOT gfx_pixel          ; 0x0020
apic_gfx_hline:
    OSAPI_SLOT gfx_hline          ; 0x0028
apic_gfx_vline:
    OSAPI_SLOT gfx_vline          ; 0x0030
apic_gfx_fill:
    OSAPI_SLOT gfx_fill           ; 0x0038
    OSAPI_SLOT gfx_frame          ; 0x0040
apic_gfx_fill_gray:
    OSAPI_SLOT gfx_fill_gray      ; 0x0048
    OSAPI_SLOT gfx_xor_rect       ; 0x0050
apic_gfx_xor_fill:
    OSAPI_SLOT gfx_xor_fill       ; 0x0058
    OSAPI_SLOT font_char          ; 0x0060
apic_font_str:
    OSAPI_XCELL font_str_x      ; 0x0068  X: the string is package data
    OSAPI_XCELL font_width_x    ; 0x006F  X
apic_wm_create:
    OSAPI_RXCELL wm_create     ; 0x0076  X: so is the template
apic_wm_show:
    OSAPI_RSLOT wm_show            ; 0x007C
    OSAPI_RSLOT wm_hide            ; 0x0082
apic_wm_front:                    ; DOCK.DRV's door (SPEC.md 30.4)
    OSAPI_RSLOT wm_front           ; 0x0088

    OSAPI_SLOT wm_content         ; 0x008E
    OSAPI_SLOT wm_obscured        ; 0x0096
    OSAPI_SLOT task_yield         ; 0x009E
    OSAPI_SLOT task_sleep         ; 0x00A6
    OSAPI_ICELL 5                 ; 0x00AE
    mov ax, [cs:ticks]
    OSAPI_IEND
apic_osapi_set_color:
    OSAPI_ICELL 5                 ; 0x00B3
    mov [cs:gfx_color], al
    OSAPI_IEND
    OSAPI_SLOT osapi_mouse        ; 0x00B8
    OSAPI_ICELL 5                 ; 0x00C0
    mov [cs:osapi_seed], ax
    OSAPI_IEND
    OSAPI_SLOT osapi_rand         ; 0x00C5
    OSAPI_RSLOT osapi_snd_caps     ; 0x00CD - sound (SPEC.md 34): what the PC
apic_osapi_snd_tone:
    OSAPI_SLOT osapi_snd_tone     ; 0x00D3   speaker can do, a tone, and a
    OSAPI_SLOT osapi_snd_play     ; 0x00DB   clip out of the caller's buffer
    OSAPI_XCELL osapi_snd_fm_x        ; 0x00E3 - FM verbs (SPEC.md 34.2). X: a
                                  ;          patch-load's 11 bytes are the
                                  ;          caller's, and only live while a
                                  ;          sound DRIVER is loaded (51.4)
    OSAPI_XCELL osapi_snd_stream    ; 0x00EA - PCM_BG streams (SPEC.md 34.5),
                                  ;          likewise the driver's. Both
                                  ;          answer CF=1 with no driver, which
                                  ;          is the same thing the held cells
                                  ;          they replaced did (SPEC.md 34.5/
                                  ;          34.6). The two numbers are held
                                  ;          rather than reused, which is what
                                  ;          fixes every slot below them
    OSAPI_RSLOT wm_sizable         ; 0x00F1 - window features (SPEC.md 11.1)
    OSAPI_RSLOT wm_fullscreen      ; 0x00F7 - fullscreen (SPEC.md 11.2)
apic_wm_grow_paint:
    OSAPI_RSLOT wm_grow_paint      ; 0x00FD - grow-box restore (SPEC.md 11.1)
    OSAPI_RNCELL dwf_dskw_write ; 0x0103 - files (SPEC.md 18.4/20.3): N,
    OSAPI_RNCELL dwf_dskw_read ; 0x0109   because ES:BX is the data buffer
                                  ;          - and DX:CX its 32-bit count, so
                                  ;          these two are the WHOLE read/write
                                  ;          surface (SPEC.md 18.4.1). DX is
                                  ;          an argument to both and an output
                                  ;          of the read, and the N stub keeps
                                  ;          its hands off it
    OSAPI_RNCELL dwf_dskw_delete ; 0x010F   and the name still has to cross
    OSAPI_JCELL api_file_rename   ; 0x0115   (two names, this one)
    OSAPI_RSLOT osapi_file_dfree   ; 0x0118 - free space on the CALLING
                                  ;          INSTANCE's volume (SPEC.md
                                  ;          19.2.1), which is the only
                                  ;          volume its writes can reach
    OSAPI_RSLOT menu_win_set       ; 0x011E - app menus (SPEC.md 12.2): the
                                  ;          set's segment comes from the
                                  ;          window, so no stub is needed
    OSAPI_JCELL api_fdlg_open     ; 0x0124 - the Standard File dialog
                                  ;          (SPEC.md 38.6): N, for the
                                  ;          default name
    OSAPI_SLOT osapi_video        ; 0x0127 - runtime screen geometry (39.2)
    OSAPI_RXCELL inst_pkg_spawn     ; 0x012F - worker tasks (SPEC.md 20.6): X,
                                  ;          the ownership fence needs to
                                  ;          know which segment is calling
    OSAPI_SLOT inst_pkg_alive     ; 0x0135
    OSAPI_SLOT wm_clip_set        ; 0x013D - the clip region (SPEC.md 11.3)
apic_wm_clip_clear:
    OSAPI_SLOT wm_clip_clear      ; 0x0145
apic_wm_clip_test:
    OSAPI_SLOT wm_clip_test       ; 0x014D
    OSAPI_ICELL 5                 ; 0x0155 - CPU tiers and memory above 1MB
    mov ax, [cs:cpu_tier]
    OSAPI_IEND
apic_xm_caps:
    OSAPI_RSLOT xm_caps            ; 0x015A   (SPEC.md 41): each body already
apic_xm_alloc:
    OSAPI_RSLOT xm_alloc           ; 0x0160   answers its SPEC.md 20.3 contract
apic_xm_free:
    OSAPI_RSLOT xm_free            ; 0x0166   exactly, so the slots call
apic_xm_copy:
    OSAPI_SLOT xm_copy            ; 0x016C   straight at them - and xm_copy's
                                  ;          ES:SI is the caller's own choice,
                                  ;          so no X stub is involved either
    OSAPI_SLOT wm_geom            ; 0x0174 - content size + visibility
                                  ;          (SPEC.md 11): content size
                                  ;          without touching the record
; --- every published slot keeps its NUMBER (SPEC.md 20.8) -------------------
;     This block was once moved down three cells when the paragraph-counting
;     arena was retired, and a package built against the older SDK then
;     called wm_resize where it meant cm_alloc. The numbers are the ABI:
;     the three arena slots stay at 0x01B8..0x01C8 as wrappers over the
;     claim heap (osapi_cm_*, kernel/memory.inc), the six slots after them
;     keep their published numbers, and everything ADDED since starts at
;     0x0200 - the first free number above them.
    ; (dropped) OSAPI_CXCELL osapi_cm_alloc_x   ; 0x01B8 - the v3 arena (SPEC.md 20.8):
                                  ;          AX = PARAGRAPHS -> AX = segment.
                                  ;          X - the owner fence needs the
                                  ;          caller's segment
    ; (dropped) OSAPI_CXCELL osapi_cm_free_x    ; 0x01C0 - AX = a base segment you own; X
    ; (dropped) OSAPI_CSLOT osapi_cm_caps_x   ; 0x01C8 - AX/DX = largest/total free
                                  ;          PARAGRAPHS, BL = free records
    OSAPI_RSLOT osapi_wm_resize    ; 0x017C - resize a window (SPEC.md 11.1):
                                  ;          BX = win, CX = w, DX = h; lock
                                  ;          held or not (11.1.2). Retires the last liberty
                                  ;          in docs/plans/completed/PAINT-NOTES.md - an app
                                  ;          writing W_W/W_H itself
    OSAPI_SLOT gfx_blit4          ; 0x0182 - packed 4bpp block (SPEC.md 5.4):
                                  ;          ES:SI = source, BP = stride,
                                  ;          AX/BX = dest, CX/DX = w/h. ES is
                                  ;          the caller's own choice here, so
                                  ;          no stub is needed
    OSAPI_RSLOT wm_about_set       ; 0x018A - the app-name pull-down (12.2):
                                  ;          BX = win, SI = your About handler
    OSAPI_RCSLOT osapi_vol_kind_x  ; 0x0190 - AL = a volume index. CF=1 = there
                                  ;          is no such volume; CF=0 with AL =
                                  ;          VK_REMOVABLE / VK_FIXED and AH =
                                  ;          VT_BIOS / VT_DRIVER (SPEC.md
                                  ;          18.7.2). The MEDIUM and the
                                  ;          TRANSPORT are two questions, and
                                  ;          a package asking "is this a hard
                                  ;          disk" wants the first.
                                  ;          WAS readbig (SPEC.md 18.4.1),
                                  ;          retired when dskw_read lost its
                                  ;          64KB ceiling - SPEC.md 20.3.1's
                                  ;          free list, and taking it cost the
                                  ;          table nothing where an append
                                  ;          would have been eight bytes
apic_wm_onmouseup:
    OSAPI_RSLOT wm_onmouseup       ; 0x0196 - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it): the
                                  ;          release half of a content click
                                  ;          (SPEC.md 13.7). Call it after
                                  ;          OSAPI_WM_CREATE, like
                                  ;          OSAPI_WM_ONSIZE - W_ONMOUSEUP is
                                  ;          not a template word.
                                  ;
                                  ;          THIS CELL IS REUSED, and it is
                                  ;          the worked example of SPEC.md
                                  ;          20.3.1's free list: it held the
                                  ;          RETIRED gfx_dbuf_gone (32), whose
                                  ;          contract was withdrawn, whose SDK
                                  ;          name was removed, and which
                                  ;          therefore had no caller that
                                  ;          could exist. Reuse cost the table
                                  ;          nothing where an append would
                                  ;          have grown it
    OSAPI_SLOT gfx_scroll         ; 0x019C - vertical scroll blit (SPEC.md
                                  ;          5.5): AX/BX/CX/DX = the rect,
                                  ;          SI = signed dy. The vacated rows
                                  ;          are the caller's to repaint
; --- and from here on, the slots added since ----------------------------------
    OSAPI_RCXCELL mmf_osapi_mem_claim ; 0x01A4 - the claim heap (SPEC.md 50.3):
    OSAPI_RCXCELL mmf_osapi_mem_free ; 0x01AA   X, same fence as the spawn
apic_osapi_mem_avail:
    OSAPI_RCSLOT mmf_osapi_mem_avail ; 0x01B0
    OSAPI_RSLOT osapi_font_glyphs  ; 0x01B6 - the kernel's 8x8 glyph table
                                  ;          (SPEC.md 6): out DX:SI = the
                                  ;          table (it lives in LOW_SEG now -
                                  ;          the one-time amendment at the
                                  ;          body below), AL = first code,
                                  ;          AH = last, CX = bytes per glyph
    OSAPI_ICELL 5                 ; 0x01BC - install the resize negotiator
                                  ;          (SPEC.md 11.1): BX = win, AX =
                                  ;          near proc. The other half of
                                  ;          docs/plans/completed/PAINT-NOTES.md's resize
                                  ;          complaint - wm_resize is the app
                                  ;          asking, this is the app answering
    mov [byte cs:bx+W_ONSIZE], ax
    OSAPI_IEND
    OSAPI_RSLOT osapi_file_here    ; 0x01C1 - where the file API's names
                                  ;          resolve (SPEC.md 18.4/19.2)
    OSAPI_RSLOT osapi_file_goto    ; 0x01C7 - ...and how to put it back
    OSAPI_RCXCELL osapi_mem_regrow_x ; 0x01CD - resize a claim you already hold
                                  ;          (SPEC.md 50.3): X, same owner
                                  ;          fence as the claim itself. In
                                  ;          place when the paragraphs above
                                  ;          are free, which is what stops a
                                  ;          grow needing old + new at once
apic_wm_title_set:
    OSAPI_RSLOT wm_title_set       ; 0x01D3 - retitle a window and redraw ONLY
                                  ;          its caption (SPEC.md 11.92): BX =
                                  ;          win, AX = the new string (0 = the
                                  ;          bytes W_TITLE names changed in
                                  ;          place). Not an X cell: the string
                                  ;          is read through W_SEG, which is
                                  ;          already the caller's segment
    OSAPI_RCXCELL drv_task_x   ; 0x01D9 - a DRIVER's worker task (SPEC.md
                                  ;          51.7): AX = a near entry in its
                                  ;          own segment, or 0 = "this IS the
                                  ;          worker, and it is exiting". X,
                                  ;          because the fence is an identity
                                  ;          test on the caller's segment
    OSAPI_RCXCELL mmf_osapi_mem_claim_dma ; 0x01DF - a claim an ISA DMA controller can
                                  ;          reach (SPEC.md 50.3): AX = KB,
                                  ;          CX = KB of the HEAD that must not
                                  ;          cross a 64KB physical boundary.
                                  ;          X, the claim's own owner fence.
                                  ;          A separate cell and not a CX on
                                  ;          mem_claim, because every existing
                                  ;          caller passes garbage there and
                                  ;          the failure would be silent
    OSAPI_XCELL font_run_x      ; 0x01E5 - one OPAQUE text run (SPEC.md 6.1):
                                  ;          CX = x, DX = y, SI = ASCIIZ,
                                  ;          AL = ink, AH = background. Draws
                                  ;          the cells' background AND their
                                  ;          glyphs in one pass, so the two
                                  ;          cannot disagree about the clip
                                  ;          (11.3's granularity rule) and, on
                                  ;          a 1bpp adapter at a byte-aligned
                                  ;          x, a cell row is one store. X:
                                  ;          the string is package data.
                                  ;          APPENDED after the cm_* trio
                                  ;          rather than kept at 0x0240 - the
                                  ;          three arena cells that had
                                  ;          been held empty are filled now
                                  ;          (SPEC.md 20.8), and everything
                                  ;          above them moved 24 bytes up
apic_wm_top:                       ; EXTD.DRV's and DOCK.DRV's door (SPEC.md 39.19.6.2, 30.5)
    OSAPI_RSLOT wm_top             ; 0x01EC - out BX = the frontmost VISIBLE
                                  ;          window, 0 if none. The one thing
                                  ;          a package could not find out: it
                                  ;          learns it HAS focus (W_ONCLICK)
                                  ;          and never that it LOST it, so a
                                  ;          real-time app had no way to pause
                                  ;          when another window came forward
                                  ;          (SPEC.md 44.8). Compare against
                                  ;          your own window ptr; W_FLAGS bit1
                                  ;          only says VISIBLE, which a wholly
                                  ;          covered window still is
apic_wm_snap:
    OSAPI_RSLOT wm_snap            ; 0x01F2 - BX = window, AL = 0 clear / non-0
                                  ;          set: keep this window's CONTENT
                                  ;          ORIGIN on a multiple of 8, so its
                                  ;          text can take font_run's
                                  ;          single-store path (SPEC.md
                                  ;          11.94/6.1). Content, not frame:
                                  ;          the border makes them differ by
                                  ;          one and the kernel owns that
                                  ;          pixel. Mono only - it is a no-op
                                  ;          on VGA, so an app may set it
                                  ;          unconditionally
    OSAPI_RCXCELL osapi_vol_add_x    ; 0x01F8 - X: a DRVC_DISK driver registers
                                  ;          one mounted volume (SPEC.md
                                  ;          18.7/51.8). in AL = its own
                                  ;          volume handle, CX = the volume's
                                  ;          sector count, DX = a listing
                                  ;          claim's segment (0 = the .lowbss
                                  ;          floor and a 32-entry listing),
                                  ;          SI = a NUL desktop label in the
                                  ;          CALLER's segment (0 = derive
                                  ;          'Disk X'). out CF=0 and AL = the
                                  ;          volume index, CF=1 = no free row.
                                  ;          The desktop zone and the drive
                                  ;          letter both fall out of the index
    OSAPI_RCXCELL osapi_vol_del_x    ; 0x01FE - X: in AL = a volume index this
                                  ;          driver registered. Drops the
                                  ;          zone, and if that volume was the
                                  ;          mounted one falls back to A: with
                                  ;          the write gate shut. Cannot fail
    OSAPI_RCXCELL osapi_vol_mount_x  ; 0x0204 - X: in AL = a volume index; mount it
                                  ;          and list it (SPEC.md 18.3), which
                                  ;          is what a driver's Mount button
                                  ;          does after osapi_vol_add. out
                                  ;          CF=1 = it is not a readable
                                  ;          FAT12/16 volume. UI-task context
                                  ;          only, like every other file slot
    ; (dropped) OSAPI_CSLOT osapi_vol_paint_x ; 0x0288 - repaint the desktop's drive zones
                                  ;          (SPEC.md 26). A driver that has
                                  ;          just added or dropped a volume
                                  ;          owes the screen this; it takes
                                  ;          the gfx lock itself, so it must
                                  ;          NOT be called from a callback
                                  ;          that already holds it - post it
                                  ;          the way a page click posts a
                                  ;          repaint
    OSAPI_RCXCELL osapi_drv_cfg_x    ; 0x020A - X: the driver's own settings blob
                                  ;          inside SYSTEM.CFG (SPEC.md 51.9).
                                  ;          in AL = 0 read / 1 write / 2 write
                                  ;          and flush now, ES:SI = the driver's
                                  ;          buffer, CX = bytes. out CF=1 = not
                                  ;          a published driver, or a write past
                                  ;          DRV_BLOB_SZ. The kernel carries the
                                  ;          bytes and never reads them, so a
                                  ;          never-written blob reads back as
                                  ;          zeroes and the driver's own version
                                  ;          byte is what recognises it
    OSAPI_RCSLOT osapi_sys_snapshot_x ; 0x0210 - the scheduler AND the instance
                                  ;          table, in ONE cli window
                                  ;          (SPEC.md 28.2). in ES:DI = a
                                  ;          SYS_SNAPSHOT_SIZE buffer; out AX =
                                  ;          MAX_TASKS, BX = INST_MAX. ES:DI
                                  ;          is the caller's own choice, so
                                  ;          no X stub is involved. It is one
                                  ;          call and not two because
                                  ;          task_exit frees an instance
                                  ;          record atomically with its task
                                  ;          slot (SPEC.md 8): read in two
                                  ;          windows, a slot can be gone from
                                  ;          one half and live in the other,
                                  ;          and the cycle diffs that CPU% is
                                  ;          built from go quietly wrong
    OSAPI_RCSLOT mmf_osapi_claim_snapshot ; 0x0216 - the claim table (SPEC.md 50.5),
                                  ;          all MEM_MAX records into ES:DI
                                  ;          as CLS_RECSZ triples; out AX =
                                  ;          MEM_MAX. WHOLE-TABLE rather than
                                  ;          per record, and it steps mem_tab
                                  ;          itself: the memory map walks
                                  ;          every record to draw it and
                                  ;          hashes every record to decide
                                  ;          whether to
    OSAPI_RCSLOT osapi_sys_kb_x    ; 0x021C - what the KERNEL occupies and what
                                  ;          the heap holds, in KB, into ES:DI
                                  ;          (SK_* below). Every term used to
                                  ;          be an assembly-time constant of
                                  ;          the kernel's own, which is
                                  ;          exactly what a package cannot
                                  ;          have: the kernel's footprint
                                  ;          moves with every build
    OSAPI_XCELL osapi_gfx_fill_pat  ; 0x0222 - a patterned fill (SPEC.md 5):
                                  ;          AX/BX/CX/DX = the rect, SI = 8
                                  ;          row bytes. X, because those eight
                                  ;          bytes are the caller's and
                                  ;          gfx_pat_stage reads them through
                                  ;          DS
    OSAPI_ICELL 6                 ; 0x0229 - out BX = the window owning the
                                  ;          menu bar, 0 = Locator. "Am I the
                                  ;          ACTIVE APPLICATION?" - which
                                  ;          wm_top above cannot answer,
                                  ;          because clicking the bare desktop
                                  ;          hands the bar to Locator and
                                  ;          moves nothing in wm_zord. Takes
                                  ;          no lock: a worker may ask
    mov bx, [cs:menu_win]
    OSAPI_IEND
    OSAPI_RSLOT fsx_caps           ; 0x022F - fullscreen exclusive (SPEC.md
                                  ;          53): AX = the FSXM bitmask this
                                  ;          adapter can set, DL = vid_kind.
                                  ;          Any context - it is how an app
                                  ;          greys its mode menu (SPEC.md 47)
    OSAPI_RXCELL fsx_run       ; 0x0235 - the bracket (SPEC.md 53.1): AX =
                                  ;          near entry, BX = own window, CX =
                                  ;          flags. X - the ownership fence is
                                  ;          inst_pkg_spawn's identity test on
                                  ;          the caller's DS. Does NOT return
                                  ;          until the app's proc does
    OSAPI_RSLOT fsx_mode           ; 0x023B - a foreign mode + its FSI info
                                  ;          block (SPEC.md 53.4): AL = FSXM
                                  ;          id, ES:DI = the caller's buffer
    OSAPI_SLOT fsx_wait           ; 0x0241 - frame clock / present (SPEC.md
                                  ;          53.5): AL = 0 tick / 1 retrace
    ; (dropped) OSAPI_SLOT gfx_line           ; 0x02E0 - RETIRED (SPEC.md 5.12.7). The
                                  ;          cell stays - a slot number is a
                                  ;          published constant (20.3) - and
                                  ;          the body answers CF = 1. The line
                                  ;          is apps/os88gfx.inc's GFXE_LINE
                                  ;          now, committed through gfx_blit1
    OSAPI_RCSLOT osapi_arg_file_x  ; 0x0249 - the document this instance was
                                  ;          launched to open (SPEC.md 54.5):
                                  ;          out CF=1 none; CF=0 with SI = its
                                  ;          NUL 8.3 name in KERNEL_SEG (read
                                  ;          it through ES), DX = its directory
                                  ;          cluster and BL = its volume, the
                                  ;          pair OSAPI_FILE_GOTO takes.
                                  ;          READ-AND-CLEAR: a second instance
                                  ;          cannot inherit it
    OSAPI_RCXCELL osapi_assoc_set_x  ; 0x024F - X: claim an extension for a
                                  ;          program (SPEC.md 54.5). ES:SI =
                                  ;          3 extension bytes then 8 stem
                                  ;          bytes, both space-padded; out
                                  ;          CF=1 = the tables are full.
                                  ;          Repoints an extension somebody
                                  ;          else claimed - that is the ask,
                                  ;          not an oversight - and marks it
                                  ;          sticky so a header declaration
                                  ;          cannot take it back
    OSAPI_ICELL 5                 ; 0x0255 - how long this machine took to
                                  ;          boot, in system ticks (SPEC.md
                                  ;          15.4): the boot sector's first
                                  ;          instruction to the first desktop
                                  ;          frame. 0xFFFF = unknown
    mov ax, [cs:boot_ticks]
    OSAPI_IEND
    OSAPI_RCSLOT osapi_dsk_cache_x ; 0x025A - COMMAND THE READ-AHEAD CACHE'S
                                  ;          WIDTH (SPEC.md 18.95.8), for a
                                  ;          program about to take the arena.
                                  ;          AL = 0 hold nothing / 1..7 hold
                                  ;          at most that many 4.5KB chunks /
                                  ;          DSK_RAH_AUTO (0xFF) no ceiling of
                                  ;          mine. Out CF=0 with AX = the
                                  ;          width held now, 0 = none; CF=1 =
                                  ;          AL was neither.
                                  ;          **A REUSED CELL** - this was
                                  ;          OSAPI_GFX_LINIT, retired by
                                  ;          5.12.7 with the rest of the walk,
                                  ;          and 0x0580 went the same way this
                                  ;          cycle. The table does not grow,
                                  ;          and the SDK name that is gone was
                                  ;          gone already: a retired cell
                                  ;          answers CF=1 and every caller of
                                  ;          one has to test CF, so there is
                                  ;          no program that reaches this
                                  ;          number and would notice
    ; (dropped) OSAPI_XCELL gfx_lstep     ; 0x0308  RETIRED (SPEC.md 5.12.7): stc/ret.
                                  ;          gfxe_wstep draws the walk's next
                                  ;          CX pixels into the package's band
apic_gfx_pen_cf:
    OSAPI_SLOT gfx_pen_cf         ; 0x0260 - CF = 0 live / 1 disabled, and it
                                  ;          sets [gfx_color] AND [gfx_dis]
                                  ;          together (SPEC.md 47 rule 3), so
                                  ;          a package's disabled TEXT
                                  ;          dithers on mono like the
                                  ;          kernel's. The cell is
                                  ;          push/pop/call/retf and touches
                                  ;          no flag, so CF crosses it
                                  ;          unchanged - which is why this
                                  ;          needs no stub and no AL
                                  ;          argument
    ; (dropped) OSAPI_XCELL gfx_lstepv    ; 0x0318  RETIRED (SPEC.md 5.12.7): stc/ret.
                                  ;          gfxe_wstepv is the batch form and
                                  ;          gfx_points (0x0538) is what a
                                  ;          package commits a point set with
    OSAPI_RSLOT clip_put           ; 0x0268 - the system clipboard (SPEC.md
                                  ;          55): ES:SI = text, CX = bytes
                                  ;          (0 = empty it); out CF=1 refused.
                                  ;          ES:SI and not the caller's DS
                                  ;          because a document is usually a
                                  ;          heap claim of its own, not the
                                  ;          package's image
    OSAPI_RSLOT clip_get           ; 0x026E - ES:DI = the caller's buffer, CX =
                                  ;          its capacity; out CF=1 empty,
                                  ;          else AX = the whole length and
                                  ;          CX = the bytes copied
    OSAPI_RSLOT clip_size          ; 0x0274 - out CF=1 and AX=0 when empty,
                                  ;          else AX = the length. What a
                                  ;          paste asks BEFORE it makes room
    OSAPI_ICELL 6                 ; 0x027A - out AX = events still queued.
                                  ;          "Is there another one of these
                                  ;          right behind me?", so a handler
                                  ;          can drop a redraw it is about to
                                  ;          be asked to do again (SPEC.md
                                  ;          13.4)
    mov al, [cs:evq_count]
    cbw
    OSAPI_IEND
    OSAPI_JCELL api_file_write_sys ; 0x0280 - N, and the ONE cell in this table
                                  ;          a package may not call: dskw_write
                                  ;          for a file that belongs to the
                                  ;          KERNEL (SPEC.md 19.6.1). Fenced on
                                  ;          the caller being a loaded DRIVER,
                                  ;          so 19.6's rule - a package cannot
                                  ;          make a file the user can neither
                                  ;          see nor delete - stands unchanged
    OSAPI_JCELL api_file_find     ; 0x0283  X: ES:DI is the caller's buffer.
                                  ;         List the current directory by
                                  ;         ORDINAL (SPEC.md 19.7.1) - the
                                  ;         one file operation the API had no
                                  ;         way to express, so a package could
                                  ;         read a file by name and never find
                                  ;         out what was there
    OSAPI_RNCELL dwf_dskw_append ; 0x0286  N: SI = name, ES:BX = bytes, CX =
                                  ;         count. Add to the END of a file
                                  ;         (SPEC.md 18.4.4). Its precondition
                                  ;         is the file's current size being a
                                  ;         whole number of clusters, which is
                                  ;         what a chunked write already is -
                                  ;         and it is dskw_write_at's body with
                                  ;         the size for an offset (18.4.7.3)
    OSAPI_RNCELL dwf_dskw_read_at ; 0x028C  N: ...and the read half. DX:AX =
                                  ;         the byte offset, CX = capacity;
                                  ;         out DX:AX = bytes delivered, 0 at
                                  ;         the end. Stateless, so a copy loop
                                  ;         may write between two reads
    OSAPI_RNCELL dwf_dskw_mkdir ; 0x0292  N: SI = name. Create a folder in
                                  ;         the current directory (SPEC.md
                                  ;         18.5). The routine is the file
                                  ;         manager's own - its three callers
                                  ;         were all cold-segment, so it had
                                  ;         never needed a .text thunk, which
                                  ;         is the whole reason it looked
                                  ;         unpublished
    OSAPI_RSLOT ui_reboot_post     ; 0x0298  AL = 0 flush the Control Panel's
                                  ;         settings on the way out / non-0 do
                                  ;         not go near a disk at all. No
                                  ;         answer: POST a restart, which
                                  ;         ui_task spends with no lock held
                                  ;         (SPEC.md 20.10). The System menu's
                                  ;         Restart, reachable from a callback
                                  ;         - which cannot do it inline,
                                  ;         because that path takes the gfx
                                  ;         lock the caller is holding and
                                  ;         waits on workers that need to be
                                  ;         scheduled. AL non-0 is for a caller
                                  ;         that has just asked the user to
                                  ;         take the floppy OUT
    OSAPI_RSLOT osapi_file_goto_q  ; 0x029E  DX = folder cluster, BL = volume.
                                  ;         GOTO's quiet twin (SPEC.md 19.2.2):
                                  ;         same volume = a word, another one =
                                  ;         a quiet mount. For a caller about to
                                  ;         read or write BY NAME rather than to
                                  ;         list - which is every copy loop
apic_wm_saveu:
    OSAPI_RSLOT wm_saveu           ; 0x02A4 - BX = window, AL = 0 clear / non-0
                                  ;          set. "My content does not change
                                  ;          while I am not drawing", which
                                  ;          lets the raise cache put its old
                                  ;          pixels back instead of calling
                                  ;          W_PAINT (SPEC.md 11.96.1)
apic_toast_show:
    OSAPI_RSLOT toast_show         ; 0x02AA - ES:SI = a NUL line, CX = ticks to
                                  ;          live (0 = ~3s). Says it in the
                                  ;          menu bar and takes it down on its
                                  ;          own (SPEC.md 59). An EMPTY string
                                  ;          retires whatever is up. ES:SI for
                                  ;          clip_put's reason: the text is
                                  ;          often not in the caller's image
    OSAPI_RCSLOT dkf_dsk_batch_begin ; 0x02B0  no arguments, no answer. "The
                                  ;         interface is frozen and the disk is
                                  ;         the same disk" (SPEC.md 18.9.3), so
                                  ;         a floppy may reuse its banked BPB
                                  ;         instead of re-reading LBA 0 at every
                                  ;         volume switch. Nests
    OSAPI_RCSLOT dsk_batch_end_x   ; 0x02B6  ...and the other end. Optional: any
                                  ;         gfx_unlock ends the batch anyway,
                                  ;         which is what makes an unclosed one
                                  ;         impossible rather than merely rare
apic_wm_destroy:
    OSAPI_RSLOT wm_destroy         ; 0x02BC  BX = a window of YOURS; the gfx lock
                                  ;         is held, exactly as OSAPI_WM_HIDE
                                  ;         wants it. Frees the RECORD, where
                                  ;         hide only takes the pixels down.
                                  ;
                                  ;         For the unowned species (SPEC.md
                                  ;         38.1) - a driver's windows, and a
                                  ;         package's second one - because
                                  ;         those have no instance teardown to
                                  ;         free the slot for them. Closing one
                                  ;         is a hide, so the record survives
                                  ;         holding a W_SEG, and an image that
                                  ;         is then unloaded leaves that record
                                  ;         naming memory the next claim takes
                                  ;         (SPEC.md 52.11.3)
    OSAPI_JCELL api_file_append_sys ; 0x02C2 - N, and the SECOND fenced cell
                                  ;         (SPEC.md 19.6.1): dskw_append for a
                                  ;         file that is already hidden +
                                  ;         system. The same arguments as
                                  ;         OSAPI_FILE_APPEND and the same
                                  ;         precondition; the only thing it
                                  ;         forgives is the two bits
                                  ;         OSAPI_FILE_WRITE_SYS stamped. It
                                  ;         cannot CREATE one - an append needs
                                  ;         an entry that is already there - so
                                  ;         19.6's rule is unmoved, and this is
                                  ;         what lets a system file too big for
                                  ;         the caller's buffer be finished at
                                  ;         all (SPEC.md 18.4.4)
    OSAPI_RSLOT wm_ownbg           ; 0x02C5 - BX = window, AL = 0 clear / non-0
                                  ;          set. "I paint every pixel of my
                                  ;          content myself", which skips
                                  ;          wm_draw_win's white fill for it
                                  ;          (SPEC.md 11.90.1). A window that
                                  ;          sets it and leaves a pixel unwritten
                                  ;          shows whatever was there before -
                                  ;          after a move, another window's
    OSAPI_SLOT wm_damage          ; 0x02CB - BX = your window, inside your own
                                  ;          W_PAINT. CF=1 = draw the whole
                                  ;          content (AX/BX/CX/DX = it); CF=0 =
                                  ;          draw AX/BX/CX/DX only, absolute and
                                  ;          inclusive, possibly EMPTY meaning
                                  ;          draw nothing (SPEC.md 11.90.2).
                                  ;          Answers "whole" unless WF_OWNBG is
                                  ;          set, because without it the kernel
                                  ;          has already whitened the content
    OSAPI_RSLOT wm_band            ; 0x02D3 - BX = window, AL = edge (0 left,
                                  ;          1 right, 2 top, 3 bottom), CX = the
                                  ;          band's extent in pixels 0..255, 0
                                  ;          retires that edge. One call per
                                  ;          edge and all four may be named.
                                  ;          "Cache THESE of my content and
                                  ;          tell me about the rest" (SPEC.md
                                  ;          11.96.11). CF = 0 taken, CF = 1
                                  ;          refused - and an app MUST read it,
                                  ;          because the reason to name a band
                                  ;          is that a cache over the whole
                                  ;          content is unaffordable, so a
                                  ;          caller that promises WF_SAVEU
                                  ;          anyway has promised the thing it
                                  ;          was trying to avoid
    OSAPI_RCXCELL osapi_fs_ent_x     ; 0x02D9 - X: a DRVC_FILE driver appends ONE
                                  ;          entry to the listing being built
                                  ;          (SPEC.md 62.9.1). ES:SI -> a
                                  ;          DSK_DE_SIZE-byte staged SPEC.md
                                  ;          19.1 entry in YOUR segment: name
                                  ;          at 0 (NUL-terminated 8.3), type at
                                  ;          16 (0 file / 1 package / 2 folder,
                                  ;          never 3 - the parent link is the
                                  ;          kernel's), YOUR OPAQUE HANDLE at
                                  ;          18, size at 20. out CF=0 and AX =
                                  ;          the index it landed at; CF=1 = the
                                  ;          listing is full.
                                  ;          LEGAL ONLY FROM INSIDE FSV_LIST -
                                  ;          the kernel raises the gate around
                                  ;          that call and lowers it after, and
                                  ;          it still SORTS (19.4) and still
                                  ;          synthesizes '..' (19.5), so do
                                  ;          neither
apic_fpg_stepb:
    OSAPI_RSLOT fpg_stepb          ; 0x02DF - AX = bytes moved SINCE YOUR LAST
                                  ;          REPORT, and a DRVC_FILE driver's
                                  ;          (SPEC.md 12.8.1/62.9.1): step
                                  ;          SPEC.md 12.8's file-activity bar
                                  ;          from inside FSV_READ/FSV_WRITE.
                                  ;          The kernel armed it from
                                  ;          FSV_STAT's size and is one far
                                  ;          call deep and blind until the verb
                                  ;          returns, so only you can move it.
                                  ;          Unarmed it is a compare and a
                                  ;          return, so call it
                                  ;          unconditionally; it DRAWS, so the
                                  ;          gfx lock must already be held -
                                  ;          which it is, inside a file
                                  ;          operation (SPEC.md 18)
    OSAPI_RSLOT wm_cursor          ; 0x02E5 - BX = window, AL = OSAPI_CUR_*.
                                  ;          The picture the pointer wears over
                                  ;          THIS window's content, and nowhere
                                  ;          else (SPEC.md 7.2). Takes effect on
                                  ;          the next UI pass rather than now:
                                  ;          the pointer may not be over you,
                                  ;          and only the kernel knows. Chrome -
                                  ;          the title bar and its boxes - stays
                                  ;          the arrow, because it is the
                                  ;          kernel's to draw and to click
    OSAPI_ICELL 5                 ; 0x02EB - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it). Your
                                  ;          content box CHANGED and you did
                                  ;          not ask - the machine changed
                                  ;          adapter under you (SPEC.md
                                  ;          39.11.2), or your window crossed
                                  ;          the seam onto a shorter display
                                  ;          (39.16.3). Called SI = window,
                                  ;          CX/DX = the NEW content size, gfx
                                  ;          lock HELD, and MUST NOT DRAW: a
                                  ;          full repaint follows and this is
                                  ;          your chance to be laid out
                                  ;          correctly before it. The other
                                  ;          half of OSAPI_WM_ONSIZE, which
                                  ;          asks BEFORE and takes an answer
                                  ;          (SPEC.md 11.98)
    mov [byte cs:bx+W_ONSZ], ax
    OSAPI_IEND
    OSAPI_RSLOT wm_keeph           ; 0x02F0 - BX = window, AL = 0 clear / non-0
                                  ;          set. "My layout is FIXED: if I
                                  ;          will not fit the desktop band,
                                  ;          hang me over the dock rather than
                                  ;          shorten me" (SPEC.md 11.93). Only
                                  ;          you know whether a row can be
                                  ;          given up - shortened, your content
                                  ;          is still drawn and simply outside
                                  ;          the frame, so it takes no clicks.
                                  ;          Call it from your entry proc,
                                  ;          right after OSAPI_WM_CREATE: it
                                  ;          re-fits from the size you asked
                                  ;          for, and preserves the flags so
                                  ;          the CF you owe the loader survives
    OSAPI_RCSLOT osapi_vol_at_x    ; 0x02F6 - DL = an int 13h drive number,
                                  ;          BX:CX = a partition's 32-bit base
                                  ;          LBA. out CF=0 and AL = the volume
                                  ;          index already covering it, CF=1 =
                                  ;          none is (SPEC.md 52.10.3). What a
                                  ;          DRVC_DISK driver asks BEFORE
                                  ;          osapi_vol_add, because on an
                                  ;          installed machine the kernel has
                                  ;          already adopted the partition it
                                  ;          booted from and the driver cannot
                                  ;          see that from its own table.
                                  ;          Answers about BIOS-transport rows
                                  ;          alone: a driver's own rows are
                                  ;          the driver's to recognise
    OSAPI_SLOT kbd_down           ; 0x02FC - AL = a make scancode. out CF = 1
                                  ;          that key is DOWN right now, CF = 0
                                  ;          it is up; every register kept.
                                  ;          int 16h answers what was TYPED,
                                  ;          which has no key-up in it and
                                  ;          cannot tell a typematic repeat
                                  ;          from a press, so a real-time app
                                  ;          inferring a hold from the interval
                                  ;          goes blind for the whole typematic
                                  ;          DELAY and loses the key entirely
                                  ;          when a second one is pressed
                                  ;          (SPEC.md 9.7). No lock and no
                                  ;          port: legal from a worker.
                                  ;          ASKING ARMS IT - a machine whose
                                  ;          software never asks never reads
                                  ;          port 60h - so the first answer
                                  ;          after the first call is always
                                  ;          "up", and a key already held when
                                  ;          it arms is not seen until it is
                                  ;          pressed again
    OSAPI_SLOT fsx_surf           ; 0x0304 - no arguments. Bracket-only. out
                                  ;          CF=0 with AX = x, BX = y, CX = w,
                                  ;          DX = h: THE RECT THIS BRACKET
                                  ;          OWNS, in the coordinates the
                                  ;          drawing slots take (SPEC.md 53.7.1).
                                  ;          CF=1 outside a bracket. (0,0,w,h)
                                  ;          on every one-display machine and
                                  ;          after any fsx_mode call, so it is
                                  ;          what a same-mode bracket used to
                                  ;          hard-code - and the second
                                  ;          display's own origin when the
                                  ;          bracket is over there
    OSAPI_RCXCELL osapi_mem_movable_x ; 0x030C - X: DX = a claim of yours, AX = a
                                  ;          near proc in YOUR segment (0 pins
                                  ;          it again). out CF = 1 = no such
                                  ;          claim, or not yours.
                                  ;          DECLARES THE CLAIM RELOCATABLE
                                  ;          (SPEC.md 66.2): the heap compactor
                                  ;          may then slide it down to close a
                                  ;          hole, and calls your proc through
                                  ;          your own dispatcher afterwards
                                  ;          with BX = the old base segment and
                                  ;          DX = the new one, the bytes having
                                  ;          already moved. An ordinary near
                                  ;          proc with a near `ret`, like
                                  ;          W_PAINT.
                                  ;          THE DEFAULT IS PINNED, so a claim
                                  ;          nobody declares behaves exactly as
                                  ;          every claim did before this
                                  ;          existed - which is what lets a
                                  ;          package adopt it one buffer at a
                                  ;          time. Your proc must put right
                                  ;          EVERY pointer you derived from the
                                  ;          base, not just the word you keep
                                  ;          it in, and must not claim, free or
                                  ;          resize anything (SPEC.md 66.3)
    OSAPI_RXCELL inst_parksafe_set  ; 0x0312 - X: AL = 1 declare / 0 withdraw.
                                  ;          out CF = 1 = you are not a live
                                  ;          package instance.
                                  ;          "THE KERNEL MAY PARK ME WHILE I
                                  ;          AM BLOCKED IN IT" (SPEC.md
                                  ;          66.5.4), which today means while
                                  ;          a worker of yours is waiting on
                                  ;          the gfx lock. Without it a worker
                                  ;          parks only at OSAPI_TASK_ALIVE -
                                  ;          and a worker that DRAWS is
                                  ;          usually blocked on the lock
                                  ;          exactly when a compaction wants
                                  ;          it, so its claims never move.
                                  ;          WHAT YOU ARE ASSERTING is that no
                                  ;          register or stack slot of yours
                                  ;          holds a pointer DERIVED from one
                                  ;          of your movable claims across a
                                  ;          call that can yield. Re-read your
                                  ;          base after any such call and this
                                  ;          is true by construction.
                                  ;          The default is NOT declared, and
                                  ;          forgetting costs a missed
                                  ;          compaction and never memory
    OSAPI_SLOT inst_task_park     ; 0x0318 - a DRIVER's worker parks here for
                                  ;          a heap compaction (SPEC.md
                                  ;          66.5.5), the way a package's
                                  ;          parks at OSAPI_TASK_ALIVE. Call
                                  ;          it once per outer loop, at a
                                  ;          point where you hold no pointer
                                  ;          derived from a claim of the
                                  ;          driver's. Every register and the
                                  ;          flags are preserved, and it costs
                                  ;          one compare when nothing is
                                  ;          asking. Without it NO claim your
                                  ;          driver owns can ever be
                                  ;          compacted, because the kernel
                                  ;          cannot tell a running service
                                  ;          task from an idle one
    OSAPI_SLOT gfx_blit1          ; 0x0320 - ES:SI = a 1bpp band in the
                                  ;          framebuffer's own bit order,
                                  ;          BP = its stride, AX = x and
                                  ;          CX = width, BOTH multiples of 8,
                                  ;          BX = y, DX = rows 1..255. Puts a
                                  ;          composed run of proportional text
                                  ;          down in ONE drawing call
                                  ;          (SPEC.md 5.4.2). out CF=1 =
                                  ;          refused and nothing drawn, which
                                  ;          is also every kern_small
    OSAPI_ICELL 6                 ; 0x0328 - out BL = the volume the machine
                                  ;          BOOTED from, which is where its
                                  ;          system resources live (SPEC.md
                                  ;          19.7). No disk I/O; every other
                                  ;          register preserved. What it is
                                  ;          FOR: OSAPI_FILE_GOTO takes a
                                  ;          VOLUME and a cluster, and until
                                  ;          this cell a package had no way to
                                  ;          name the system disk at all - it
                                  ;          could stand in its own folder, or
                                  ;          in one a dialog had given it, and
                                  ;          nowhere else
    mov bl, [cs:dsk_bootvol]
    OSAPI_IEND
    OSAPI_RCXCELL osapi_drv_dlg_x    ; 0x032E - X: the Standard File dialog, for a
                                  ;          DRIVER's Control Panel page
                                  ;          (SPEC.md 51.10). AL = FDLG_OPEN /
                                  ;          FDLG_SAVE, ES:SI = a default name
                                  ;          or 0, DI = your own near
                                  ;          completion proc. out CF=1 refused.
                                  ;          fdlg_open fences on a live
                                  ;          INSTANCE owning the window it is
                                  ;          handed (SPEC.md 38.1) and a driver
                                  ;          has neither, so this borrows the
                                  ;          panel's - which is the window the
                                  ;          page is drawn in anyway
    OSAPI_RSLOT wm_ondrag_c        ; 0x0334 - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it): the
                                  ;          pointer MOVED while your press
                                  ;          was armed (SPEC.md 13.8.2).
                                  ;          CX = x, DX = y, SI = window, the
                                  ;          same environment W_ONCLICK and
                                  ;          W_ONMOUSEUP are dispatched in.
                                  ;          Between the two edges of one
                                  ;          gesture, only when the point
                                  ;          CHANGED, and outside the window
                                  ;          as freely as the release - which
                                  ;          is the whole point, because a
                                  ;          control cannot un-draw itself as
                                  ;          the pointer leaves if nothing
                                  ;          tells it. Call it AFTER wm_create
    OSAPI_SLOT wm_timer_c         ; 0x033A - BX = window, AX = TICKS from now
                                  ;          (0 cancels). Arms the one-shot
                                  ;          timer (SPEC.md 13.9); W_ONTIMER
                                  ;          runs when it expires and the
                                  ;          timer disarms itself FIRST, so a
                                  ;          handler wanting a repeat re-arms
                                  ;          inside itself. 18.2 ticks a second
    OSAPI_RSLOT wm_ontimer_c       ; 0x0342 - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it): what
                                  ;          the timer above calls. CX = DX = 0
                                  ;          (a timer has no point), SI = the
                                  ;          window, under the gfx lock and
                                  ;          billed to you - W_ONCLICK's
                                  ;          environment. Call it AFTER
                                  ;          wm_create
    OSAPI_XCELL drv_pkg_call_x      ; 0x0348 - a PACKAGE calls a DRIVER (SPEC.md
                                  ;          20.11). BH = the DRVC_* class, BL
                                  ;          = a verb THAT DRIVER defines, and
                                  ;          the kernel knows nothing about
                                  ;          either. X, because the buffer a
                                  ;          verb is handed is the package's:
                                  ;          this is the one driver entry point
                                  ;          where ES is not KERNEL_SEG.
                                  ;          CF=1 with AX=0 when no driver of
                                  ;          that class is published or it
                                  ;          published no DSV_PKGCALL - which
                                  ;          is THE FENCE, and is why an .O88
                                  ;          cannot name DRVC_DISK and arrive
                                  ;          at a raw sector write.
                                  ;
                                  ;          **IT WAS 0x0428 AND MOVED HERE IN
                                  ;          A MERGE**, because the integration
                                  ;          branch had appended OSAPI_DRV_DLG
                                  ;          at that number at the same time.
                                  ;          The two sides added different
                                  ;          NAMES, so apps/os88api.inc merged
                                  ;          CLEAN with both cells pointing at
                                  ;          one address and nothing would have
                                  ;          said so until a package called one
                                  ;          and got the other (docs/UPSTREAM.md
                                  ;          Rule 0's sibling: sort the
                                  ;          %defines by address after every
                                  ;          merge and look for a duplicate)
apic_wm_wake:                     ; mem_cpq_run_x's door to the wake (SPEC.md
                                  ; 66.4.3): the cell IS the shim, so no cw_
    OSAPI_SLOT wm_wake            ; 0x034F - BX = a window of yours: post an
                                  ;          EVT_WAKE for it (SPEC.md 74.1).
                                  ;          Any context - ISR- and worker-
                                  ;          safe. out CF=0 a wake is queued
                                  ;          (now, or one was already waiting:
                                  ;          one per window at a time), CF=1
                                  ;          the ring was full and nothing was
                                  ;          posted. The UI task pops it and
                                  ;          calls the handler below WITHOUT
                                  ;          the gfx lock
    OSAPI_ICELL 5                 ; 0x0357 - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it): the
                                  ;          WAKE handler. Called SI = your
                                  ;          window, on the UI task, lock NOT
                                  ;          held, billed to your instance -
                                  ;          it may call the file slots and
                                  ;          may take the lock for a burst it
                                  ;          can state. Install it after
                                  ;          OSAPI_WM_CREATE like
                                  ;          OSAPI_WM_ONRESIZE - a side table,
                                  ;          not a template word
    mov [byte cs:bx+W_ONWK], ax
    OSAPI_IEND
    OSAPI_RSLOT osapi_file_goto_qm ; 0x035C - DX = folder cluster, BL = volume:
                                  ;          OSAPI_FILE_GOTO_Q's quiet stand
                                  ;          AND THEN inst_vol_mark, so the
                                  ;          calling instance now believes it
                                  ;          lives there and the next file
                                  ;          cell's inst_vol_enter does not
                                  ;          undo the move (SPEC.md 74.1,
                                  ;          19.2.2). Same answers as GOTO_Q
                                  ;
                                  ;          **THESE THREE WERE 0x0428/0x0430/
                                  ;          0x0438 AND MOVED HERE IN A MERGE**,
                                  ;          the same way api_drv_call moved
                                  ;          above and for the same reason: both
                                  ;          trees appended to one tail in one
                                  ;          round, so main's wm_wake met this
                                  ;          branch's OSAPI_DRV_DLG, wm_onwake
                                  ;          met WM_ONDRAG and file_goto_qm met
                                  ;          WM_TIMER. TWO OF THE THREE MERGED
                                  ;          WITH NO CONFLICT MARKER AT ALL -
                                  ;          different names, one address - and
                                  ;          nothing would have said so until a
                                  ;          package called one and got the
                                  ;          other. Sort the %defines by address
                                  ;          after every merge from main
    OSAPI_RSLOT wm_onclose         ; 0x0362 - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it): the
                                  ;          CLOSE NEGOTIATOR (SPEC.md 75.1).
                                  ;          Called SI = your window, on the UI
                                  ;          task, gfx lock HELD - W_ONCLICK's
                                  ;          environment - when anything at all
                                  ;          tries to close you: the close box,
                                  ;          the app-name menu's Close, the
                                  ;          dock tile's context menu. Answer
                                  ;          CF = 0 to let the close happen and
                                  ;          CF = 1 to REFUSE it, in which case
                                  ;          nothing happens at all and you owe
                                  ;          the user a way out. Install it
                                  ;          after OSAPI_WM_CREATE like
                                  ;          OSAPI_WM_ONWAKE - a side table,
                                  ;          not a template word
    OSAPI_RSLOT wm_close_req       ; 0x0368 - BX = a window of yours: close it
                                  ;          (SPEC.md 75.2). DEFERRED to the
                                  ;          next UI pass, because the caller
                                  ;          is standing in the segment the
                                  ;          close is about to free - so this
                                  ;          RETURNS, and your window is gone a
                                  ;          moment later. It does NOT ask your
                                  ;          negotiator again: this is the
                                  ;          answer to the question it asked
    OSAPI_RSLOT wm_prefer          ; 0x036E - BX = window, SI = the offset IN
                                  ;          THIS WINDOW'S OWN SEGMENT of a
                                  ;          12-byte table: three (w, h) frame
                                  ;          sizes in VID_VGA / VID_HERC /
                                  ;          VID_CGA order, so the adapter kind
                                  ;          indexes it. SI = 0 withdraws it, a
                                  ;          (0,0) pair means "nothing to say
                                  ;          about that adapter". Registers AND
                                  ;          applies, FLAGS PRESERVED
                                  ;          (SPEC.md 11.100.1)
apic_wm_minsize:
    OSAPI_RSLOT wm_minsize         ; 0x0374 - BX = window, CX = minimum outer
                                  ;          width, DX = minimum outer height;
                                  ;          0/0 withdraws. The floor every
                                  ;          path that REDUCES a size honours,
                                  ;          itself capped at the display's own
                                  ;          extent so it can never cost a
                                  ;          window its title bar. Registers AND
                                  ;          applies, FLAGS PRESERVED
                                  ;          (SPEC.md 11.100.2)
    OSAPI_RSLOT wm_display         ; 0x037A - BX = window; out AX = width, BX =
                                  ;   height, CX = the first row the DOCK owns,
                                  ;   SI = the first usable row, DL = kind, DH =
                                  ;   bpp - OSAPI_VIDEO for the display THIS
                                  ;   WINDOW is on (SPEC.md 39.16.4)
    OSAPI_ICELL 5                 ; 0x0380 - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it): the
                                  ;          RIGHT-CLICK handler (SPEC.md
                                  ;          13.11). Called CX = x, DX = y,
                                  ;          SI = your window, on the UI task,
                                  ;          gfx lock HELD - W_ONCLICK's
                                  ;          environment exactly - when the
                                  ;          RIGHT button is pressed in the
                                  ;          content of your FRONTMOST window.
                                  ;          Press edge only; there is no
                                  ;          release half. Install it after
                                  ;          OSAPI_WM_CREATE like
                                  ;          OSAPI_WM_ONWAKE - a side table,
                                  ;          not a template word
    mov [byte cs:bx+W_ONRC], ax
    OSAPI_IEND
    OSAPI_RNCELL dwf_dskw_rmany ; 0x0385  N: SI = a NUL 8.3 name, AL = 0 an
                                  ;         EMPTY folder only / non-zero the
                                  ;         whole tree under it (SPEC.md 18.6).
                                  ;         The AL survives the N stub because
                                  ;         api_copyname banks AX
    OSAPI_SLOT drv_pkg_call_x     ; 0x038B - OSAPI_DRV_CALL WITH ES LEFT ALONE
                                  ;          (SPEC.md 20.11.2). The same
                                  ;          routine behind the same fence: the
                                  ;          only difference is the stub. The
                                  ;          ordinary SLOT does not touch ES,
                                  ;          where the X stub overwrites it
                                  ;          with the caller's DS - so a buffer
                                  ;          in a heap claim is reachable and a
                                  ;          package stops having to grow its
                                  ;          own segment to feed a driver
    OSAPI_ICELL 5                 ; 0x0393 - AL = ink (what a SET bit in the
                                  ;          next gfx_blit1's band becomes),
                                  ;          AH = paper (a CLEAR one). SPEC.md
                                  ;          5.4.2.2. Dies with the gfx lock,
                                  ;          exactly as [gfx_dis] does, so a
                                  ;          site that forgets to reset it
                                  ;          cannot tint what draws next. NOT
                                  ;          READ on a 1bpp adapter: a band on
                                  ;          one plane already means lit and
                                  ;          unlit.
                                  ;
                                  ;          FIVE BYTES ON EVERY BUILD, which
                                  ;          is the one inline cell whose body
                                  ;          differs by build: the pen is
                                  ;          VGA's and so kern_big's alone
                                  ;          (vga12.inc), the one word store
                                  ;          works because the pair is
                                  ;          adjacent (the %error there), and
                                  ;          BAND=1's polarity byte makes a
                                  ;          third store that does not fit -
                                  ;          so the knob build jumps to a
                                  ;          body that does it all, and
                                  ;          kern_small, which has no pen,
                                  ;          spends the length as NOPs
%ifdef KERN_BIG
 %ifdef BANDCOMP
    jmp strict near gfx_blit1_pen   ; 3 bytes; the body is retf-ended
    nop                             ; ...and these two are never reached
 %else
    mov [cs:gfx_b1ink], ax          ; AL -> ink, AH -> paper
 %endif
%else
    nop                             ; kern_small: no pen to set, and the
    nop                             ; cell is five bytes on every kernel
    nop
    nop
%endif
    OSAPI_IEND
    OSAPI_ICELL 5                 ; 0x0398 - AL = the colour the NEXT
                                  ;          OSAPI_ICON_DRAW's MASK rows lay
                                  ;          down, AH = the colour its DATA
                                  ;          rows draw over them. ONE-SHOT:
                                  ;          the draw puts CWHITE/CBLACK back,
                                  ;          so a site that forgets cannot
                                  ;          tint the next icon (SPEC.md 25.6)
    mov [cs:ico_c1], ax
    OSAPI_IEND
    OSAPI_XCELL icon_draw_x     ; 0x039D - CX = x, DX = y, SI -> a masked
                                  ;          1bpp record in YOUR segment: a
                                  ;          two-byte header (words a row,
                                  ;          rows), then that many mask words
                                  ;          and that many data words, bit 15
                                  ;          leftmost. The mask says which
                                  ;          pixels are yours; everything
                                  ;          outside it is left alone, and x
                                  ;          may be ANYTHING - which is what
                                  ;          OSAPI_GFX_BLIT1 cannot do
                                  ;          (SPEC.md 5.4.2 refuses an x off
                                  ;          the byte grid). out CF = 1
                                  ;          refused: it stages one word a row
                                  ;          and sixteen rows at the outside
                                  ;          (SPEC.md 25.6.1)
api_gfx_blitp:                    ; (named: SPEC.md 5.4.3.6's walk far-calls it)
    OSAPI_SLOT gfx_blitp          ; 0x03A4 - ES:SI = plane 0's first row of a
                                  ;          block that is ALREADY FRAMEBUFFER
                                  ;          BYTES, DI = the step to the next
                                  ;          plane, BP = the row stride inside
                                  ;          one, AX/BX = x (A MULTIPLE OF 8)
                                  ;          and y, CX/DX = width in pixels and
                                  ;          rows. out CF = 1 REFUSED and a
                                  ;          caller must read it: a 1bpp
                                  ;          adapter, an x off the byte grid, a
                                  ;          block off the screen sideways, an
                                  ;          armed clip region, two displays
                                  ;          straddled, or a kernel built
                                  ;          without the planes at all - and
                                  ;          the answer to every one of them is
                                  ;          OSAPI_GFX_BLIT4, which still works
                                  ;          (SPEC.md 5.4.3)
    OSAPI_RCXCELL mmf_osapi_mem_claim_hi ; 0x03AC - X: AX = KB, BX unused. THE SAME
                                  ;          CLAIM FROM THE OTHER END (SPEC.md
                                  ;          50.3.2), for a buffer the HARDWARE
                                  ;          holds the address of and which can
                                  ;          therefore never move. A driver's
                                  ;          ring in the data arena is 50.3's
                                  ;          own warning - "one long-lived data
                                  ;          claim landing mid-heap permanently
                                  ;          splits the space a package has to
                                  ;          load into" - and this is the door
                                  ;          it lacked
    OSAPI_RCXCELL mmf_osapi_mem_claim_dma_hi ; 0x03B2 - ...and with CX = the 64KB
                                  ;          page-safe HEAD, which is what a
                                  ;          sound card's DMA ring wants
    OSAPI_XCELL gfx_spans     ; 0x03B8 - X: AX = the first row, ES:SI = CX
                                  ;          records of {x1, x2} - ONE INTERVAL
                                  ;          A ROW, on CONSECUTIVE rows, x1 > x2
                                  ;          being an empty one. Fills them all
                                  ;          in [gfx_color] with ONE row base
                                  ;          computed and one clip (SPEC.md
                                  ;          5.10). out CF = 1 REFUSED and a
                                  ;          caller must read it: an armed clip
                                  ;          region, two displays, or
                                  ;          kern_small - and the answer to all
                                  ;          three is a GFX_FILL a row, which
                                  ;          draws the identical pixels
    OSAPI_RSLOT wm_ownseg          ; 0x03BF - AL = a window SLOT; out CF = 1 no
                                  ;          live window there, else DX = the
                                  ;          segment that owns it (KERNEL_SEG
                                  ;          for a window the kernel made).
                                  ;          SPEC.md 11.96.3.1: the only way to
                                  ;          read back the slot a raise cache's
                                  ;          owner tag carries. IT ASKED FOR
                                  ;          0x04D8 AND SO DID gfx_spans, on
                                  ;          another branch, in the same cycle
                                  ;          - the collision CLAUDE.md asks to
                                  ;          be checked by hand, caught by the
                                  ;          merge and then by t_api_abi
    OSAPI_SLOT fsx_page           ; 0x03C5 - AL = a page index; shows it, and
                                  ;          waits for the retrace that latches
                                  ;          it. Bracket-only, exclusive task
                                  ;          only, graphics modes with more
                                  ;          than one page only (SPEC.md 53.10).
                                  ;          FSI_PAGES has been published since
                                  ;          53.4 with no supported way to act
                                  ;          on it: Mode X moves the CRTC start
                                  ;          address, a Hercules flips a bit in
                                  ;          3B8h that does nothing until 3BFh
                                  ;          allows it, and neither is
                                  ;          derivable from the info block
    OSAPI_RSLOT wm_noanim          ; 0x03CD - BX = a window of yours, between
                                  ;          OSAPI_WM_CREATE and
                                  ;          OSAPI_WM_SHOW: it does NOT zoom
                                  ;          open (SPEC.md 11.99.2.1). No AL
                                  ;          and no clear - the five sibling
                                  ;          flag slots (wm_sizable, wm_snap,
                                  ;          wm_keeph, wm_ownbg, wm_saveu) all
                                  ;          take AL = 0 clear / non-zero set
                                  ;          and this one deliberately does
                                  ;          not, the bit
                                  ;          being read in one place and only
                                  ;          on the wm_show of a window that
                                  ;          is not yet visible. THE CELL IS
                                  ;          IN BOTH KERNELS and the body is
                                  ;          not %ifdef'd: a slot that exists
                                  ;          in one build and not another is
                                  ;          an ABI that depends on a knob
                                  ;          (SPEC.md 20.8 rule 4), and on
                                  ;          kern_small the bit is simply set
                                  ;          and never read
    OSAPI_FCELL lzf_decomp      ; 0x03D3 - expand a compressed block
                                  ;          (docs/plans/O88-COMPRESSION-PLAN.md
                                  ;          12.1). A JSLOT and not an X or N
                                  ;          cell: BOTH segment registers are
                                  ;          the caller's here, because the
                                  ;          decompressor reads no kernel data
                                  ;          at all and so needs no stub to
                                  ;          reach the caller's segment - it
                                  ;          needs the OPPOSITE, to be left
                                  ;          alone. OSAPI_GFX_BLIT4 is the
                                  ;          precedent for a cell taking a
                                  ;          caller far pointer this way.
                                  ;          IN BOTH KERNELS and never
                                  ;          %ifdef'd (SPEC.md 20.8 rule 4):
                                  ;          COMPRESS= decides which FORMATS
                                  ;          answer, not whether the cell is
                                  ;          there, and a format this build
                                  ;          lacks is refused with CF
    OSAPI_JCELL api_file_find_raw ; 0x03D9 - OSAPI_FILE_FIND, answering what
                                  ;          the file OCCUPIES rather than
                                  ;          what it expands to (SPEC.md
                                  ;          20.14.3). The cell a COPIER wants:
                                  ;          OSAPI_FILE_READ_AT is already raw,
                                  ;          and this is the size to go with it
    OSAPI_SLOT api_gfx_save       ; 0x03DC - AX/BX/CX/DX = an inclusive rect,
                                  ;          ES:DI = your buffer. Bank the
                                  ;          pixels under a thing you are about
                                  ;          to draw over (SPEC.md 5.3), so
                                  ;          taking it down is a write-back
                                  ;          instead of a repaint. CF = 1
                                  ;          REFUSED - the rect straddles two
                                  ;          displays - and nothing is written
    OSAPI_SLOT api_gfx_rest       ; 0x03E4 - ...and put it back: same rect,
                                  ;          ES:SI = the buffer the save filled
    OSAPI_RXCELL inst_restart_set  ; 0x03EC - X: AX = a near offset in YOUR own
                                  ;          image, 0 to withdraw. out CF = 1 =
                                  ;          you are not a live package
                                  ;          instance.
                                  ;          "IF YOU HAVE TO MOVE MY REGION,
                                  ;          THROW MY WORKER'S STACK AWAY AND
                                  ;          RE-ENTER IT HERE" (SPEC.md
                                  ;          66.6.2). The one way past 66.6.1's
                                  ;          limit - a worker's stack carries
                                  ;          its own segment at depths nothing
                                  ;          can find, so the answer is not to
                                  ;          find them but to arrange for the
                                  ;          stack not to matter.
                                  ;          IT IS A WINDOW, NOT A PROPERTY,
                                  ;          and the asymmetry against
                                  ;          OSAPI_MEM_PARKSAFE is the thing to
                                  ;          weigh: parksafe declared wrongly
                                  ;          costs a missed optimisation, this
                                  ;          costs a lost loop iteration - and
                                  ;          if the worker was holding
                                  ;          something, correctness
    OSAPI_RNCELL ldf_ld_pkg_start  ; 0x03F2  N: START THE PACKAGE CALLED NAME
                                  ;         (SPEC.md 21.5). SI = a NUL 8.3
                                  ;         name in YOUR segment, resolved in
                                  ;         the folder you are standing in.
                                  ;         DX:CX = the length of an image you
                                  ;         are HOLDING at ES:DI, or ZERO
                                  ;         meaning you hold none and the
                                  ;         kernel is to READ THE FILE. Out
                                  ;         CF=0 AL=0 running; CF=1 AL=LD_*.
                                  ;         ONE DOOR: it was two for a cycle -
                                  ;         this taking an image and 0x05A0 a
                                  ;         name - which is the successor slot
                                  ;         beside a no-consumer path that
                                  ;         SPEC.md 20.8 rule 4 names as the
                                  ;         worse spec. 0x05A0 is WITHDRAWN.
                                  ;         N, and it is the N stub that makes
                                  ;         the merge cheap: it stages the
                                  ;         name AND hands the body the
                                  ;         caller's ES and DI untouched, so
                                  ;         one contract carries both forms.
                                  ;         The PARTS refusal lives on the
                                  ;         image arm, where it is true: with
                                  ;         no file there is nothing for
                                  ;         op_load to read a part out of
    OSAPI_RCXCELL osapi_desk_item_x  ; 0x03F8 - X: put an ITEM on the
                                  ;          desktop, or take one off
                                  ;          (SPEC.md 26.9). in AL = 1 add /
                                  ;          0 remove, ES:SI = a record in
                                  ;          the caller's own segment: a
                                  ;          DRIVER's is the 40-byte service
                                  ;          record (26.7), a PACKAGE's a
                                  ;          128-byte link (26.8.1), and each
                                  ;          names the cell it would like.
                                  ;          out CF=0 AL = the zone. It was
                                  ;          OSAPI_DESK_SVC, a driver's door
                                  ;          only. kern_small's body refuses
                                  ;          (SPEC.md 20.8 rule 4: the cell
                                  ;          is in both kernels)
    OSAPI_RCXCELL osapi_pkg_rehome_x ; 0x03FE - X: a LOADER hands its identity to
                                  ;          one of its own parts (SPEC.md
                                  ;          20.12.10). in DX = the segment the
                                  ;          program's image starts at, AX =
                                  ;          the bytes available there.
                                  ;          out CF=1 refused - not the entry
                                  ;          proc of the launch in flight, a
                                  ;          second call, or DX = 0.
                                  ;          IT ONLY RECORDS: the loader is
                                  ;          still running in the region this
                                  ;          frees, so the work is ld_start's
                                  ;          step 8a, one instruction after
                                  ;          this returns
    OSAPI_XCELL gfx_points        ; 0x0404 - X: draw a set of pixels the CALLER
                                  ;          computed (SPEC.md 5.6.9). ES:SI =
                                  ;          CX records of two words each, x
                                  ;          then y; CX = how many, 0 legal.
                                  ;          [gfx_color] is the ink and the
                                  ;          lock is held. Preserves every
                                  ;          register; a point outside every
                                  ;          clip rect is SKIPPED, not refused
    OSAPI_SLOT cur_busy        ; 0x040B - I AM ABOUT TO GO QUIET FOR A WHILE
                                  ;          (SPEC.md 7.5). No argument. The
                                  ;          pointer becomes a CLOCK for
                                  ;          the rest of the gfx-lock hold the
                                  ;          caller is inside, and gfx_unlock
                                  ;          puts the old one back - so there
                                  ;          is no "off" to forget and no way
                                  ;          to leave one on the screen.
                                  ;          out NOTHING, flags included: it
                                  ;          refuses on its own - no hold of
                                  ;          the caller's own, a clip region
                                  ;          armed, an fsx bracket, a pointer
                                  ;          something else is holding down -
                                  ;          and a refusal costs the caller
                                  ;          nothing but the picture, so there
                                  ;          is nothing for it to act on
apic_inst_minimize:               ; DOCK.DRV's door (SPEC.md 30.4)
    OSAPI_RSLOT inst_minimize      ; 0x0413 - SEND MY OWN WINDOW TO THE DOCK
                                  ;          (SPEC.md 29.6). BX = a window of
                                  ;          yours, the gfx lock held - the
                                  ;          minimize box's own environment,
                                  ;          which is W_ONCLICK's. The tile
                                  ;          inverts, the window hides, and a
                                  ;          click on the tile brings it back.
                                  ;          OSAPI_WM_HIDE IS NOT THIS and is
                                  ;          a trap in its place: a plain hide
                                  ;          leaves I_FLAGS bit0 clear, so the
                                  ;          tile is drawn LIVE, dock_click
                                  ;          takes its wm_front arm, and
                                  ;          wm_front never sets the visible
                                  ;          bit on an invisible window - a
                                  ;          tile that does nothing and an
                                  ;          instance with no way back.
                                  ;          A FULLSCREEN window may call it:
                                  ;          wm_hide drops the latch and
                                  ;          restores the windowed geometry
                                  ;          (SPEC.md 11.2), so the app must
                                  ;          fix up its OWN idea of being
                                  ;          fullscreen first, and neither zoom
                                  ;          is drawn (SPEC.md 11.99.5).
                                  ;          Preserves every register
    OSAPI_RCXCELL drv_suspend_x  ; 0x0419 - THE MACHINE, OUT OF MY WAY
                                  ;          (SPEC.md 51.11): AL = 1 suspend
                                  ;          the hardware drivers, ES:DI = a
                                  ;          DQ_SIZE-record buffer or DI = 0,
                                  ;          out CX = records written; AL = 0
                                  ;          put them back; AL = 2 hand the
                                  ;          WHOLE machine to kern_dos, ES:SI
                                  ;          = a KDH_* record (96.40). X
                                  ;          because the buffer and the record
                                  ;          are the CALLER's. The bodies are
                                  ;          HIBER.DRV's - the same sweep the
                                  ;          hibernate detaches with - and the
                                  ;          resident part is the thunk
    OSAPI_RXCELL api_file_path   ; 0x041F - WHERE AM I STANDING? (SPEC.md
                                  ;          19.2.4). ES:DI = your buffer,
                                  ;          CX = its size; out CF=0 with a
                                  ;          NUL `\DIR\DIR` in it and CX its
                                  ;          length, else AX = FERR_*. X
                                  ;          because the buffer is the
                                  ;          CALLER's - and a slot at all
                                  ;          because dsk_find drops the dot
                                  ;          links, so no package can walk up
    OSAPI_RCSLOT mmf_osapi_mem_floor ; 0x0425 - "COMPACT THE DISK CACHE, DO NOT
                                  ;          DESTROY IT" (SPEC.md 50.6.6). AL =
                                  ;          a purge level, and it is THIS
                                  ;          TASK's floor from here on: every
                                  ;          claim it makes sheds and drops only
                                  ;          caches strictly BELOW it, and
                                  ;          OSAPI_MEM_AVAIL answers net of the
                                  ;          same rule. MEM_LVL_TOP lifts it,
                                  ;          and so does the instance's own
                                  ;          teardown. Preserves every register
                                  ;          AND the flags, so it can sit
                                  ;          between a claim and its `jnc`
    ; (dropped) OSAPI_SLOT gfx_line           ; 0x0568 - RETIRED (SPEC.md 50.6.6.1): it was
                                  ;          OSAPI_MEM_CLAIM_LVL, a claim with
                                  ;          the floor as an argument, and the
                                  ;          floor above made it one cell and
                                  ;          sixty bytes of body for nothing.
                                  ;          stc/ret, and the ONE package that
                                  ;          ever called it is in this tree
    OSAPI_RSLOT osapi_vol_stat     ; 0x042B - THE VOLUME YOU ARE STANDING ON,
                                  ;          in four registers (SPEC.md
                                  ;          18.4.6): out CF=0 with AX =
                                  ;          sectors per cluster, BX = free
                                  ;          clusters, CX = bytes per sector,
                                  ;          DX = total clusters; CF=1 with
                                  ;          AX = FERR_NODISK. DOS AH=36h's own
                                  ;          shape, the DOS box being its one
                                  ;          caller - it was a 12-byte record
                                  ;          until the size pass found no
                                  ;          caller read more than these four
    OSAPI_RNCELL fcpf_fcp_door     ; 0x0431 - COPY OR MOVE ONE ENTRY, source
                                  ;          folder to destination (SPEC.md
                                  ;          22.24). SI = its 8.3 name, AL =
                                  ;          the verb (OSAPI_FCP_COPY or
                                  ;          OSAPI_FCP_MOVE), BL/DX the source
                                  ;          drive and folder cluster, BH/CX
                                  ;          the destination's - the pair
                                  ;          OSAPI_FILE_HERE answers. Out CF=0
                                  ;          AX=0, else AX = FERR_*. N, because
                                  ;          the name is package data and the
                                  ;          stage is what api_n does. It is
                                  ;          the file manager's OWN engine
                                  ;          entered where a Paste enters it:
                                  ;          a move is a Cut - re-linked on
                                  ;          one volume, copied and deleted
                                  ;          where the re-link declines - and
                                  ;          a copy streams through a buffer
                                  ;          the engine claims and undoes a
                                  ;          partial destination on failure
    OSAPI_RCSLOT osapi_drv_classk_x ; 0x0437 - WHAT ONE DRIVER CLASS HOLDS
                                  ;          (SPEC.md 51.12). AL = a DRVC_*.
                                  ;          Out CF=0 with AX = the KB its
                                  ;          LOADED rows hold - bit 15 =
                                  ;          DRVM_PLUS, "and a store the user
                                  ;          sizes" - and CX = how many
                                  ;          answered; CF=1 with AX=0 CX=0,
                                  ;          nothing of that class is loaded.
                                  ;          One walk answers both halves a
                                  ;          caller needs: CF greys the
                                  ;          checkbox and AX is the figure
                                  ;          beside it (SPEC.md 47 rule 5).
                                  ;          A SLOT - no caller segment is
                                  ;          involved - and it took the
                                  ;          RETIRED cell that stood here,
                                  ;          OSAPI_FILE_MOVE's, so the table
                                  ;          gains no byte and 20.3.1's free
                                  ;          list is empty again
    OSAPI_RNCELL dwf_dskw_write_at ; 0x043D  N: SI = name, ES:BX = bytes, CX =
    OSAPI_RCXCELL osapi_mem_compact_x ; 0x0443 - X: MY OWN REGION IN THE PASS
                                  ;          (SPEC.md 66.4.3): one door, the
                                  ;          verb in AH. AH=0 THE WHAT-IF:
                                  ;          OSAPI_MEM_AVAIL_LVL's answer (AL =
                                  ;          the level, out AX/BX) planned as
                                  ;          if YOUR OWN REGION moved too. A
                                  ;          MEASUREMENT AND NOT A PROMISE:
                                  ;          claiming it refuses, because you
                                  ;          really are standing in your
                                  ;          region. AH=1 THE POST: "compact
                                  ;          everything you can, INCLUDING MY
                                  ;          REGION, then wake me" - BX = a
                                  ;          window of yours, AL = the shed
                                  ;          rank the pass is to respect. Out
                                  ;          CF=0 posted - return from your
                                  ;          callback and claim in your wake
                                  ;          handler, where plain OSAPI_MEM_AVAIL
                                  ;          has become the what-if; CF=1 a
                                  ;          post is already standing.
                                  ;          OSAPI_PKG_REHOME's shape: it only
                                  ;          RECORDS, because you are executing
                                  ;          in the region it is going to move.
                                  ;          X because the what-if excuses the
                                  ;          caller's segment and the stub
                                  ;          already puts it in ES. On
                                  ;          kern_small the what-if IS the
                                  ;          plain answer and the post refuses
                                  ;          (SPEC.md 66.0)
    OSAPI_XCELL osapi_mouse_feed  ; 0x0449 - X: ONE RELATIVE REPORT from a
                                  ;          pointing device the kernel does
                                  ;          not drive itself (SPEC.md 9.12):
                                  ;          AX = dx, BX = dy (positive is
                                  ;          down), CL = buttons. DRIVERS ONLY
                                  ;          - the fence is ES == the segment
                                  ;          published in DRVC_POINT, which is
                                  ;          why it is an X cell. out CF=1
                                  ;          refused. THE CELL IS IN BOTH
                                  ;          KERNELS (SPEC.md 20.8 rule 4); on
                                  ;          kern_small the body refuses.
                                  ;          CLOBBERS AX, BX, CX, DX, SI and
                                  ;          DI: mou_apply spends all six and
                                  ;          the six pushes that hid that were
                                  ;          twelve resident bytes bought for
                                  ;          a caller that wanted none of them
                                  ;          (SPEC.md 9.12.5). It came
                                  ;          from main at 0x0550, which this
                                  ;          tree had already spent, and took
                                  ;          the tail cell the size pass freed
                                  ;          (OSAPI_MEM_COMPACT_WAKE's)
    OSAPI_ICELL 5                 ; 0x0450 - BX = window, AX = a near proc in
                                  ;          YOUR segment (0 clears it): the
                                  ;          PRESS half of a content click.
                                  ;          W_ONCLICK is a template word and
                                  ;          this is the only way to write it
                                  ;          late, which os88ui_btninit needs
                                  ;          so the button control can see a
                                  ;          press before the package does
                                  ;          (SPEC.md 20.5.1.3.3)
    mov [byte cs:bx+W_ONCLICK], ax
    OSAPI_IEND
    OSAPI_RNCELL dwf_dskw_read_seq ; 0x0455  N: A STREAMING READ (SPEC.md
                                  ;          18.4.8): SI = name, DX:BX = the
                                  ;          buffer, CX = capacity (clusters),
                                  ;          ES:DI = the caller's 16-byte
                                  ;          cursor - zero it and set +12 to
                                  ;          start or seek. READ_AT without
                                  ;          the per-call walk: the cursor is
                                  ;          trusted under the mount
                                  ;          generation it was seeded in and
                                  ;          re-seeded from the name when
                                  ;          anything remounted or wrote.
                                  ;          kern_big; the small door refuses
    OSAPI_RSLOT osapi_fsx_spk     ; 0x045B - THE SPEAKER AT A SAMPLE RATE
                                  ;          (SPEC.md 34.11), inside your own
                                  ;          FSXF_RATE bracket: AL = 0 open
                                  ;          (DX = N, SI = your sample ISR),
                                  ;          out AX = K, DX:BX = the chain;
                                  ;          AL = 1 close. kern_big; the
                                  ;          small body refuses AX = 1
    OSAPI_RNCELL dwsf_dskw_write_seq ; 0x0461 N: A STREAMING APPEND (SPEC.md
                                  ;          18.4.9): SI = name, DX:BX = the
                                  ;          bytes, CX = the count (0 closes),
                                  ;          ES:DI = the caller's 16-byte
                                  ;          cursor, +3 bit 0 = HELD. APPEND
                                  ;          without the lookup and the walk;
                                  ;          HELD commits the FAT and the
                                  ;          size once, at the close or when
                                  ;          the UI unlocks. kern_big; the
                                  ;          small door refuses
    OSAPI_RSLOT osapi_ferr_text   ; 0x0467 - AX = a FERR_*, out ES:SI = the
                                  ;          kernel's own NUL text for it -
                                  ;          the Disk window's status-line
                                  ;          table, so a package toasts a
                                  ;          refusal without carrying the
                                  ;          words (SPEC.md 18.4.11). 0x0467
                                  ;          was OSAPI_VOL_TAKE for one cycle
                                  ;          and never shipped (52.1.1)
    OSAPI_ICELL 8                  ; 0x046D - AL = THM_BRIGHT/DARK/COLOR/WIN
                                   ; (SPEC.md 76, 76.14): adopts it as the
                                   ; theme and repaints the chrome. An
                                   ; unknown value, or COLOR on an adapter
                                   ; with no colour, falls back to Bright -
                                   ; the same refusal a settings file gets
                                   ; (76.12.1). OSAPI_SLOT's exact 8-byte
                                   ; shape, by hand: kern_small has no theme
                                   ; to set at all (76.13) and spends the
                                   ; length as NOPs, gfx_blit1_pen's own
                                   ; precedent (0x0393 above)
%ifdef KERN_BIG
    push ds
    push cs
    pop ds
    call api_thm_set
    pop ds
%else
    nop
    nop
    nop
    nop
    nop
    nop
    nop
%endif
    OSAPI_IEND
    OSAPI_ICELL 8                  ; 0x0475 - AL = 0..7, one of Color's eight
                                   ; presets (SPEC.md 76.14). Only reaches
                                   ; the screen while Color is already the
                                   ; live theme; otherwise a store for
                                   ; whenever the machine switches to Color.
                                   ; An out-of-range value falls back to 0.
                                   ; Same shape and reason as the cell above
%ifdef KERN_BIG
    push ds
    push cs
    pop ds
    call thm_set_preset
    pop ds
%else
    nop
    nop
    nop
    nop
    nop
    nop
    nop
%endif
    OSAPI_IEND
    OSAPI_ICELL 8                  ; 0x047D - AL bit 0 stripes, bit 1 95 buttons, bit 2 Windows title style
%ifdef KERN_BIG
    push ds
    push cs
    pop ds
    call thm_set_stripes
    pop ds
%else
    times 7 nop
%endif
    OSAPI_IEND
osapi_table_end:                  ; 0x0485 today (0x05A8 before pass 4's
                                  ; renumber). TWO cells came off the tail in
                                  ; the size pass: OSAPI_MEM_COMPACT_WAKE
                                  ; (0x0598) is 0x0590's MEMC_POST verb
                                  ; now (SPEC.md 66.4.3), and the DOS
                                  ; handoff (0x05A0, one cycle) is verb 2
                                  ; of 0x0550 (51.11, 96.40). The table
                                  ; shrank rather than holing, so the
                                  ; free list (20.3.1) stays empty - and
                                  ; 0x0598 went straight back out to
                                  ; OSAPI_MOUSE_FEED above

; build-time assertions: the table's start and span are ABI, prove them here
OSAPI_TABLE_OFF equ osapi_table - $$
OSAPI_TABLE_LEN equ osapi_table_end - osapi_table
%if OSAPI_TABLE_OFF != 0x0010
%error "os8088 API jump table must start at offset 0x0010"
%endif
%if OSAPI_TABLE_LEN != 43*8 + 11*7 + 99*6 + 6*3 + 6 + 12*5 + 3*6 + 3*8
%error "os8088 API jump table length mismatch: includes FERR_TEXT and Color theme cells"
%endif

; =============================================================================
; The debug registry (SPEC.md 57)
;
; How a TEST package reads kernel state that is not an API slot: one word at
; the fixed offset 0060:000E names this list, and each entry is a (tag,
; offset) pair naming a published block in KERNEL_SEG. Tag 0 ends it.
;
; It exists because the fixed-word mechanism does not scale. boot_ticks took
; 0x000C, the mouse instrument took 0x0006 and the disk instrument 0x000E, and
; at that point the first paragraph was full and the fourth had nowhere to go
; - while the alternative, an API slot, is worse: half of these are knob-built
; (`make DISKCNT=1`), and a slot that exists in one build and not another is
; an ABI that depends on a knob (SPEC.md 20.8 rule 4).
;
; The TAG IS THE BLOCK'S OWN FIRST WORD, two ASCII characters, so a reader can
; check that the offset it followed lands on what it asked for - and so a
; human reading `xp` output over QMP can see 'MO' and 'DK' rather than count
; words. Everything past that first word belongs to the section that owns the
; block; this list says only where to look.
;
; A kernel that publishes nothing still has the word, reading 0. Anything a
; reader cannot find, it reports and skips (tests/sysbench does both).
; =============================================================================
dbg_reg:
    dw DBG_TAG_MOUSE, mou_dbg_blk   ; SPEC.md 9.4.2 - unconditional: the port
                                    ; contest is a question about a REAL card,
                                    ; so it has to be in the build the field
                                    ; machine is actually sent
%ifdef STK_DIAG
    dw DBG_TAG_STKD, sd_dbg_blk     ; docs/plans/completed/STACK-SLOTS-PLAN.md - `make
                                    ; STKDIAG=1`, knob-built under 57.2's rule
                                    ; for bprof's reason: it COUNTS
%endif
%ifdef BOOT_PROFILE
    dw DBG_TAG_BPROF, bprof_dbg_blk  ; SPEC.md 15.5 - `make BOOTPROF=1`, and
                                    ; knob-built under SPEC.md 57.2's rule: it
                                    ; COUNTS, which is work a kernel has no
                                    ; other reason to do
%endif
    dw DBG_TAG_CLOCK, clk_dbg_blk   ; SPEC.md 37.92 - unconditional for the
                                    ; same reason: an RTC ladder is a question
                                    ; about silicon nobody here has, and the
                                    ; one machine that has it is sent a
                                    ; knob-free kernel by handover rule
    dw DBG_TAG_VIDEO, vid_dbg_blk   ; SPEC.md 57.4 - and unconditional for the
                                    ; THIRD time for the same reason: whether
                                    ; a second monitor is plugged into a
                                    ; second card is the one question in
                                    ; SPEC.md 39 no emulator can be asked
    dw DBG_TAG_BUILD, kbld_dbg_blk  ; SPEC.md 57.6 - WHICH KERNEL IS THIS. A
                                    ; report that cannot name its own build is
                                    ; a report somebody has to take on trust,
                                    ; and this session lost a day to exactly
                                    ; that: three field disks whose KERNEL.SYS
                                    ; is 88,134 bytes apiece, because the image
                                    ; rounds to a 512-byte rung, so not one row
                                    ; in the report could tell them apart
    dw DBG_TAG_FDD, fdd_dbg_blk     ; SPEC.md 57.5 - and the FOURTH, more
                                    ; plainly than any of them: this block
                                    ; exists BECAUSE no emulator here can be
                                    ; trusted about what a 765 reports for a
                                    ; drive that is not there, so its two raw
                                    ; status bytes have to be in the build the
                                    ; field machine is actually sent
%ifdef DISK_COUNTERS
    dw DBG_TAG_DISK, dsk_dbg_blk    ; SPEC.md 18.94 - `make DISKCNT=1` only
%endif
    dw 0                            ; end of list

; The three snapshot cells above (0x0210..0x021C) each fill a buffer the
; CALLER owns, and their layouts are ABI like the slot numbers themselves.
; They are declared with the tables they copy - SS_*/SSI_* in instance.inc,
; CLS_* and SK_* in memory.inc - because every one of those layouts is
; derived from MAX_TASKS, INST_MAX or MEM_MAX, and a constant belongs beside
; the table it measures. All of them are mirrored in apps/os88api.inc.
;
; =============================================================================
; The stubs the X, N and cold-SLOT cells reach (SPEC.md 20.3). Each ends in
; retf and restores every segment register it borrowed.
; =============================================================================

; X: ES = the caller's DS, so the kernel routine can reach package data.
; ONE body for every X cell whose routine is .text, entered with BP = the
; routine to call...
api_rx:  ; STKBALANCE-OK: pops the rare cell's return address - it IS the target word's address, and the push bp the cell made is the frame
    pop bp                      ; the cell's return address = its target word
    mov bp, [cs:bp]
api_x:
    push ds                     ; the caller's DS...
    push es                     ; ...and its ES
    push ds
    pop es                      ; ES = the caller's DS
    push cs
    pop ds                      ; DS = KERNEL_SEG
    call bp
.done:
    pop es
    pop ds
    pop bp                      ; ...and the BP the cell pushed for us. POP
    retf                        ; and RETF touch no flags, so the routine's
                                ; CF answer still survives the return

; ...and ONE for every X cell whose routine is .cold (OSAPI_CXCELL), the same
; frame with the near `call bp` replaced by api_far's far one. The prologue
; is six bytes and is written twice rather than shared, because it PUSHES for
; the body that follows it and a subroutine cannot push on its caller's
; behalf; the epilogue is api_x's own.
api_rxc:  ; STKBALANCE-OK: pops the rare cell's return address - it IS the target word's address, and the push bp the cell made is the frame
    pop bp
    mov bp, [cs:bp]
                                ; (was api_xc: the cold X body, reached only by falling through from api_rxc)
    push ds
    push es
    push ds
    pop es                      ; ES = the caller's DS
    push cs
    pop ds                      ; DS = KERNEL_SEG (api_far reads its word by it)
    call KERNEL_SEG:api_far     ; COLD_SEG:BP, and the body's retf lands here
    jmp short api_x.done

; The rare SLOT bodies (OSAPI_RCSLOT for a .cold routine, OSAPI_RSLOT for a
; .text one): DS = KERNEL and NOTHING else changed, which is the plain SLOT's
; contract. The cell is `push bp / call api_rs[c] / dw target`, so the return
; address the call pushed IS the address of the target word: pop it, read
; the word through CS, and the BP the cell pushed is what the `pop bp` before
; the retf gives back. BP is an INPUT to no cell of this shape.
api_rsc:  ; STKBALANCE-OK: pops the rare cell's return address - it IS the target word's address, and the push bp the cell made is the frame
    pop bp
    mov bp, [cs:bp]
    push ds
    push cs
    pop ds
    call KERNEL_SEG:api_far     ; COLD_SEG:BP
    pop ds
    pop bp
    retf
api_rs:  ; STKBALANCE-OK: pops the rare cell's return address - it IS the target word's address, and the push bp the cell made is the frame
    pop bp
    mov bp, [cs:bp]
    push ds
    push cs
    pop ds
    call bp
    pop ds
    pop bp
    retf

; api_far - the far call to COLD_SEG:BP, built on the CALLER'S stack the way
; driver.inc's drv_pkg_disp builds a driver's (SPEC.md 20.3.2). The 8086 has
; no `call far reg`, so the caller far-calls THIS, which pushes the target
; under that frame and retf's into it; the cold body's own retf then returns
; through the caller's frame, to the instruction after its call. Nothing here
; touches the flags. A TOP-LEVEL label reached by a real far call, for
; drv_pkg_disp's reason: os88ovlchk judges a routine by the returns in its
; extent, and this one's is {retf} - far-called, so consistent.
;
; It wants DS = KERNEL_SEG, which all three callers set first; the `dw` is
; .text data because COLD_SEG is an equate and the 8086 cannot push an
; immediate. Peak stack is four bytes deeper than a plain `call far` for the
; length of the two pushes, and two bytes SHALLOWER than the thunk it
; replaces from the cold body onwards - the thunk's own near return is gone.
api_far:
    push word [api_coldseg]
    push bp
    retf
api_coldseg: dw COLD_SEG

; N: the name at the caller's DS:SI is staged into kernel scratch first,
; because ES:BX belongs to the caller's data buffer and cannot carry it.
; ONE body for every N cell, entered with BP = the routine to call - and
; every N target is a file door in a cold module, so this body far-calls it
; through api_far and there is no near N body at all.
;
; inst_vol_enter - "resolve this in the CALLING INSTANCE's directory"
; (SPEC.md 19.2.1) - used to be a per-cell argument, and all seven cells
; passed 1. It is what an N cell IS, so it is unconditional here.
; api_fdlg_open is NOT one of these cells - see the stub below it, and the
; reason there is why this body cannot serve it: every one of these names is
; MANDATORY, so the stage is unconditional too.
; The caller's DS and SI stay banked across the far call, IN THAT ORDER:
; dwsf_dskw_write_seq reads the DS there for WSEQF_SYS's fence (18.4.9).
api_rn:  ; STKBALANCE-OK: pops the rare cell's return address - it IS the target word's address, and the push bp the cell made is the frame
    pop bp
    mov bp, [cs:bp]
                                ; (was api_n: the N body, reached only by falling through from api_rn)
    push ds
    push si
    push di
    push es
    call api_cpname             ; caller DS:SI -> KERNEL:api_name, <= 13 bytes
    pop es                      ; the caller's ES back: it is the buffer
    pop di                      ; and its DI, which the callee may need
    push cs
    pop ds                      ; DS = KERNEL
    call inst_vol_enter         ; this instance's own folder (SPEC.md 19.2.1);
    mov si, api_name            ; preserves every register and the flags
    call KERNEL_SEG:api_far     ; COLD_SEG:BP, the cold file door
    pop si
    pop ds
    pop bp
    retf

; -----------------------------------------------------------------------------
; api_fdlg_open - slot 0x0124. The N stub's shape with ONE difference, and it
; is why this cannot be an OSAPI_NSTUB: every other N cell's name is an
; argument the operation cannot run without, and THIS one's is optional -
; SPEC.md 38.6 publishes SI as "a default name, or 0", and fdlg_open_x tests
; it (`or si, si / jnz .setname`) to choose between the caller's default and
; SPEC.md 38.10's "the name this application last picked".
;
; Staged unconditionally, that 0 became `api_name` - a kernel address, never
; zero - holding whatever the copy found at the CALLER's offset 0, which for a
; package is its own 32-byte header (SPEC.md 20.2). So an app that asked for
; no default got `O8` and the version bytes in the name box, and fdlg_home_name
; was unreachable from any package: the branch that calls it cannot be taken
; through a stub that never leaves SI zero. Four of the six packages that use
; the dialog ask for no default - Tracker, Frotz (twice) and ModPlug (twice,
; Open and PlayList > Add...) - so this was every one of them.
;
; It takes no V for the reason the macro's header gives: fdlg_seed decides
; where a DIALOG opens, and a volume switch underneath it would pre-empt that.
; -----------------------------------------------------------------------------
api_fdlg_open:
    push ds
    push si
    push di
    push es
    or si, si
    jz .nodef                   ; "no default": the ZERO is the argument, so it
    call api_cpname             ; must reach fdlg_open unstaged and unchanged
    mov si, api_name            ; a kernel offset, so it survives the DS switch
    jmp short .go
.nodef:
    xor si, si
.go:
    pop es                      ; the caller's ES back: it is the buffer
    pop di                      ; and its DI, which fdlg_open needs as an
                                ; input (the completion proc's offset)
    push cs
    pop ds                      ; DS = KERNEL
    call COLD_SEG:fdf_fdlg_open
    pop si
    pop ds
    retf

; -----------------------------------------------------------------------------
; api_gfx_save / api_gfx_rest - slots 0x0508 / 0x0510 (SPEC.md 5.3)
;
; gfx_save and gfx_restore have been in this kernel since the menu save-under
; and are on BOTH builds - unlike gfx_blit1, which is kern_big only - so what a
; package was missing was the CELL and not the code. The one thing it was also
; missing is the test the kernel's own callers owe them: GFXDENTERR puts the
; whole shape on the display holding the top-left (SPEC.md 39.14.6), so a
; straddling rect banks one display's half and, on the way back, leaves the
; other display's half STALE. wm_su_take asks vid_span_one for exactly this
; reason; a package has no published way to ask, so the cell asks for it.
;
; Refusing is the whole answer rather than a shortcoming: a caller that gets
; CF = 1 repaints, which is what it did before it had this cell at all
; (SPEC.md 12.4's [menu_sseg] = 0 rule, one layer out).
;
; vid_span_one takes AX/BX/CX/DX inclusive - the same four registers these two
; take - so the guard needs no frame, and gfx_save/gfx_restore preserve all
; four, so the caller's rect survives for the restore.
; -----------------------------------------------------------------------------
api_gfx_save:
%ifdef KERN_BIG
    call vid_span_one           ; ...and only kern_big HAS a second display to
    jc .no                      ; straddle: vid_span_one is inside that build's
%endif                          ; guard because the extended desktop is, so on
    call gfx_save               ; the small kernel the question cannot arise
    clc                         ; and the cell is five bytes lighter
.no:                            ; (vid_span_one's CF = 1 is the refusal as it
    ret                         ; stands: no `stc` of its own)

api_gfx_rest:
%ifdef KERN_BIG
    call vid_span_one           ; symmetric with the save on purpose: a
    jc .no                      ; contract that refuses on one side and not
%endif                          ; the other is the kind that costs somebody a
    call gfx_restore            ; day
    clc
.no:
    ret

; -----------------------------------------------------------------------------
; api_file_find - slot 0x0283 (X). in CX = ordinal, ES:DI = a DSK_FIND_SZ
; buffer; out CF=0 with it filled and CX = the next ordinal (SPEC.md 19.7.1)
;
; An X stub because the buffer is the caller's, and it is the same fence as
; api_file_write_sys below in its OTHER direction: a driver may SEE hidden and
; system entries, a package may not. The two together are one boundary rather
; than two rules - a package can neither find a system file here nor create
; one there - which is what keeps SPEC.md 19.6 true as a sentence and not
; merely as a list of blocked entry points.
;
; It resolves in the CALLING INSTANCE's directory (SPEC.md 19.2.1), like every
; other name-taking cell: "list the current directory" has to mean the same
; directory that OSAPI_FILE_READ would resolve a name in, or a package would
; enumerate one folder and open files from another.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; api_file_find_raw - slot 0x03D9, and api_file_find's twin in one respect
;
; **THE SIZE, AND NOTHING ELSE.** A compressed file has two of them (SPEC.md
; 20.14.3): what it occupies and what it expands to. `api_file_find` answers
; the second, because an application sizes its claim from +18 and then reads
; the file with OSAPI_FILE_READ, which expands it. A COPIER wants the first -
; OSAPI_FILE_READ_AT delivers the packed bytes, so the packed length is the
; number that describes what it is about to move.
;
; It exists because the record has nowhere to put both. `OSAPI_FIND_SZ` is 24
; and published, so a caller's buffer is 24 bytes wide and growing it would
; write past one; and asking per file with a second call would put a directory
; walk on every entry of every listing. A second CELL over the same walk costs
; a slot and nothing per entry.
;
; **Bit 0 of +22 still means COMPRESSED**, on both cells, because that is a
; fact about the FILE rather than about which number was reported - and a
; caller knows which it got from which cell it called.
;
; The fence is unchanged: hidden and system entries go to a DRIVER and not to
; a package, on this cell exactly as on the other one (SPEC.md 19.6.1).
; -----------------------------------------------------------------------------
api_file_find_raw:
    mov al, 1                   ; AL is not an input here (CX and ES:DI are)
    jmp short api_ff_go         ; and the fence below spends it anyway, so it
                                ; carries the raw flag across the one prologue

api_file_find:
    xor al, al
api_ff_go:
    push ds
    push si
    push bx
    mov bx, ds                  ; the caller's segment, for the fence
    push cs
    pop ds                      ; DS = KERNEL
    mov [dsk_fdraw], al
    call inst_vol_enter         ; this instance's own folder; preserves
                                ; everything including the flags
%ifndef OS88_DRIVERS
    xor al, al                  ; kern_small: no segment is a driver's, so no
%else                           ; caller may see them (kernel size pass 11)
    call COLD_SEG:dvf_drv_owns_seg ; CF = 0: a loaded driver, so it may see the
    sbb al, al                  ; system files it is going to have to copy.
    inc al                      ; CF straight into AL without a branch: `sbb
                                ; al,al` is 0 for CF=0 and 0xFF for CF=1
                                ; whatever AL held, and `inc al` makes that 1
                                ; and 0 - the same answer, two bytes shorter
                                ; and with no jump. AL is not an input here
                                ; (CX and ES:DI are), and CF SURVIVES: on the
                                ; 8086 AL-AL-CF borrows exactly when CF was
                                ; set, so `sbb al,al` gives CF back unchanged
                                ; and `inc` does not touch it - which matters
                                ; only in that it is dsk_find_x's OUTPUT below
                                ; and never its input.
%endif
    pop bx
    call COLD_SEG:dsk_find_x
    pop si
    pop ds
    retf

; -----------------------------------------------------------------------------
; api_file_path - slot 0x041F (X). in ES:DI = the caller's buffer, CX = its
; size; out CF=0 with the path written and CX its length, else AX = FERR_*
;
; An X cell (not a JSLOT like api_file_find, which does its own segment work):
; api_x has already put the caller's DS in ES and KERNEL in DS, and reaches
; here with a NEAR call - so this stub is inst_vol_enter, the walk, and a near
; `ret`. It begins with inst_vol_enter so the walk starts from where THIS
; instance believes it is standing rather than from wherever the volume was
; last dragged (SPEC.md 19.2.1). **That call is the ONE "is this the same
; disk?" in the whole operation**: dsk_path walks with dsk_dirw_start/get,
; which take a cluster and stand nowhere, so no level of the walk mounts and
; none re-reads LBA 0 (SPEC.md 19.2.4).
;
; There is no driver fence on this cell. The other two file cells have one
; because they can NAME a hidden or system file; a path names directories the
; caller is already standing inside, and a package that could not see its own
; folder's name could not have been launched from it.
; -----------------------------------------------------------------------------
api_file_path:
%ifndef KERN_SMALL
    call inst_vol_enter         ; preserves everything, including the flags
    call COLD_SEG:dsk_path_x    ; ...and this banks every register it does
                                ; not answer in, so the stub banks none
    ret                         ; NEAR. api_x reaches every X cell with
                                ; `call bp` and does the segment work itself -
                                ; ES = the caller's DS, DS = KERNEL - so a
                                ; stub here neither loads a segment nor
                                ; returns far. A `retf` popped one word too
                                ; many and returned into nothing, which
                                ; presented as a second window whose title
                                ; read as machine code
%else
    mov ax, FERR_NODISK         ; kern_small carries the cell and no walker
    stc                         ; (SPEC.md 19.2.4.3): no package on its disks
    ret                         ; calls this, and every caller tests CF
%endif

; -----------------------------------------------------------------------------
; api_file_write_sys - slot 0x0280, and the ONE fenced cell (SPEC.md 19.6.1)
;
; dskw_write for a file that belongs to the KERNEL: hidden, system and (bar
; SYSTEM.CFG) read-only, which is what makes an installed volume a SYSTEM
; volume rather than a folder with the same bytes in it.
;
; SPEC.md 19.6 says this entry point must never get an API slot, and the
; reason it gives is the test to apply: "a package cannot make a file the
; user can neither see nor delete". That is a statement about PACKAGES, and
; it still holds - the fence below refuses one. A DRIVER is the other species
; (SPEC.md 51): drv_tab is a fixed kernel-side table of known files, a .DRV
; carries header version DRV_VER which ld_check_hdr refuses for an application, and
; disk_mount types only *.O88 as launchable - so the set of things that can
; ever be a driver is decided when this kernel is built and a user cannot add
; to it. That is a real boundary rather than an honour system, which is why
; 19.6's rule is narrowed here rather than dropped.
;
; The fence is drv_owns_seg, and the precedent is SPEC.md 51.7's spawn fence:
; ES is the caller's DS stamped by the stub convention (SPEC.md 20.3), and a
; package's DS is its own segment, which is never a drv_tab row. It is an
; IDENTITY test, not a containment one.
;
; BX is banked across it because BX is the caller's DATA BUFFER offset here -
; the fence needs a register and that one is live.
;
; api_file_append_sys - slot 0x02C2 - is the SAME cell with a different tail,
; and it shares this body rather than copying it because the fence is the
; whole of the interesting part and two copies of a fence is one that can be
; got wrong. [api_sysfp] IS which dskw_ entry point runs - the offset half
; of a COLD_SEG far pointer, so the tail is one `call far` and not a
; compare between two (size pass 10: it was a byte flag, [api_sysap]); it is
; written through CS because DS is still the CALLER's here, and it is set on
; both paths rather than only the append one - a word left set by an append
; would silently turn the next driver's write into an append.
; -----------------------------------------------------------------------------
%ifndef OS88_DRIVERS
api_file_append_sys:            ; kern_small: the fence refuses EVERY caller,
api_file_write_sys:             ; no segment being a driver's - so the two
    mov ax, FERR_PROT           ; cells are .refuse's answer and nothing else,
    stc                         ; every register .refuse gave back untouched
    retf                        ; (kernel size pass 11)
%else
api_file_append_sys:
    mov word [cs:api_sysfp], dwf_dskw_append_sys    ; ...the same fence, the
    jmp short api_file_sysc                         ; other verb (18.4.4)
api_file_write_sys:
    mov word [cs:api_sysfp], dwf_dskw_write_sys
api_file_sysc:
    push ds
    push si
    push di
    push es
    push bx                     ; the caller's buffer offset: live, and
    mov bx, ds                  ; drv_owns_seg wants a register
    push ds                     ; ...AND THE CALLER'S DS IS KEPT, because
    push cs                     ; api_copyname below reads the name through
    pop ds                      ; it. The fence needs DS = KERNEL to reach
    call COLD_SEG:dvf_drv_owns_seg ; drv_tab, and the first version of this stub
    pop ds                      ; switched and never switched back - so it
                                ; staged 13 bytes of KERNEL image as the file
                                ; name and handed that to dskw_write_sys.
                                ; OSAPI_NSTUB avoids it by copying BEFORE it
                                ; touches DS; this one has to put it back
    pop bx
    jc .refuse
    call api_cpname             ; caller DS:SI -> KERNEL:api_name, <= 13 bytes
    pop es                      ; the caller's ES back: it is the buffer
    pop di
    push cs
    pop ds                      ; DS = KERNEL
                                ; NO V (SPEC.md 19.2.1): the installer names
                                ; the volume it is building and must not have
                                ; its own instance's folder put underneath it
    mov si, api_name
    call far [api_sysfp]        ; COLD_SEG:the verb the entry chose
    pop si
    pop ds
    retf
.refuse:                        ; DS is already KERNEL; unwind what is left
    pop es
    pop di
    pop si
    pop ds
    mov ax, FERR_PROT           ; the same answer DSKW_PROT gives a package
    stc                         ; that names a system file: it is protected,
    retf                        ; and from out there that is the whole truth
%endif                          ; OS88_DRIVERS

; ...and the two-name case, which needs DI as well and so is written out
api_file_rename:
    push ds
    push si
    push di
    push es
    push di                     ; bank the new-name pointer across the first
    call api_cpname             ; copy, which needs DI itself - the old name,
                                ; and ES = KERNEL for the second from here
    pop si                      ; SI = the caller's DI = the new name
    mov di, api_name2
    call api_copyname           ; new name
    pop es
    push cs
    pop ds                      ; DS = KERNEL
    call inst_vol_enter         ; V, as the three above (SPEC.md 19.2.1):
                                ; BOTH names resolve in the calling
                                ; instance's directory, which is the only
                                ; reading of a rename that makes sense
    mov si, api_name
    mov di, api_name2
    call COLD_SEG:dwf_dskw_rename
    pop di
    pop si
    pop ds
    retf

; -----------------------------------------------------------------------------
; api_copyname - stage a NUL 8.3 name across the segment boundary
; in:  DS:SI = the caller's name, ES:DI = kernel scratch (13 bytes)
; out: nothing; the copy is NUL-terminated even if the source was not - a
;      package cannot make this run on
; clobbers: SI and DI, which the copy spends and does NOT put back. AX and CX
;      come back the caller's, because both are still ARGUMENTS at every one
;      of the four call sites; SI and DI are dead at all four - each
;      overwrites both on the instruction after the call, and each banked the
;      caller's pair in its own prologue. A FIFTH call site has to read this.

; --- resident shims the overlay far-calls (see the contract above) ----------
; Four bytes each. A routine gets one only because an overlay entry needs it
; and it has to stay resident for its own reasons. (It used to name
; dsk_vol_slot here, "because every zone painter calls it on every repaint" -
; but the painters near-call dsk_vol_slot_x in `.cold`, and the `.text` thunk
; had desk_init as its only caller; it is gone.) (This sentence used to open with
; `xm_arm`, which moved into XMEM.DRV with SPEC.md 41.12 and has had no shim
; below - and no existence in kernel/ - since.)
%ifdef BOOT_MARK
ovw_mark_stamp:     call mark_stamp     ; SPEC.md 15.5's marker, reached from
                    retf                ; the OVERLAY (37.95). The MARK macro
                                        ; itself cannot go there: it puts the
                                        ; index byte in the instruction stream
                                        ; and mark_here reads it `cs:`, which
                                        ; is the KERNEL's segment inside
                                        ; mark_here and the BLOB's at the call
%endif
                                ; (ovw_desk_rowcalc, `call desk_rowcalc /
                                ; retf`, stood here: desk.inc's overlay
                                ; caller far-calls desk_rowcalc_x itself now,
                                ; as kmain_o does - that body already ends in
                                ; a far return. Size pass 9, -4 resident)
%ifdef BOOT_PROFILE
ovw_bprof_mark:     call bprof_mark
                    retf
%endif
%ifdef SCH_QUANTUM
ovw_sch_fast_on:    call sch_fast_on
                    retf
%endif
%ifdef BANDCOMP
ovw_band_init:      call band_init
                    retf
%endif
%ifdef DOCK_OPT
ovw_dock_geom:      call dock_geom
                    retf
%endif
; ...and the MOUSE's (SPEC.md 9.4.7). mouse_init's boot half is in the
; overlay and everything mou_hotplug or mouse_unhook can reach stayed resident,
; so the port writers the probe uses and the round counter it resets are edges
; that cross. They had five shims here; they go through viddet.inc's spw_near
; now, from overlay-side stubs in mouse.inc (kernel size pass 8), and what is
; left is the two knob builds' own.
%ifdef MOU_DIAG
ovw_mdb_rxb:        call mdb_rxb        ; SPEC.md 9.4.6.5's wire counter, which
                    retf                ; mou_idbyte reaches from the overlay.
                                        ; Knob-only, so it costs a shipped
                                        ; kernel nothing at all
%endif
%ifdef KERN_BIG                 ; the PS/2 half is kern_small's absent one
%ifdef KERN_EMU
ovw_mou_lockon:     call mou_lockon     ; ...and one for SPEC.md 9.11:
                    retf                ; vmm_boot_x settles the contest on the
                                        ; backdoor and has to retire the UARTs
                                        ; from `.boot2`, where it and mouse_init
                                        ; both are. The five that stood above were
                                        ; mouse_init's; THIS ONE IS
                                        ; kern_emu's, because vmm_boot_x is its
                                        ; only caller and that routine does not
                                        ; exist on the other builds (SPEC.md
                                        ; 9.11.7). A wrapper whose callee is
                                        ; compiled out is 4 bytes of overlay
                                        ; nothing can reach
%endif
%endif
ovw_font_run_x:     call font_run_x     ; SPEC.md 15.6's status line composes
                    retf                ; into the OVERLAY, so its string is
                                        ; reached ES:CS and not through DS -
                                        ; which is font_run_x's entry and not
                                        ; font_run's

; ...AND THE CLOCK'S FIVE ARE DOWN TO TWO (SPEC.md 37.94). The five were port
; helpers that the READ path (overlay) and the WRITE path shared, and the
; write path was resident. It is not any more - it is in CTRL.DRV - so three
; of the five lost their last near caller in `.text` along with it, and their
; bodies simply end in `retf` now and are far-called by name (SPEC.md 2.6.1).
; That is not a saving of twelve bytes so much as a statement the BUILD makes:
; os88ovlchk refuses a near call to a retf body, so "resident only because two
; other address spaces need me" is checked rather than commented.
;
; What is left is the two that clk_ns_stamp - itself one of the survivors -
; NEAR-calls, so neither can say it that way. They are `cw_` and not `ovw_`
; because `ovw_` names the overlay and the overlay is no longer the caller
; that matters: CTRL.DRV reaches these all session, the overlay for one boot.
; (cw_clk_ns_put and cw_clk_tobcd went with them in kernel size pass 9: the
; port helpers are a copy in each image now, near-called - SPEC.md 37.94.)
; -----------------------------------------------------------------------------
api_cpname:                     ; ...the same into KERNEL:api_name, which is
    push cs                     ; what four of the five callers want, and the
    pop es                      ; three instructions they each wrote out ahead
    mov di, api_name            ; of it (ES = KERNEL on the way out)
api_copyname:
    push ax                     ; AX and CX ONLY, and both are arguments the
    push cx                     ; caller is still carrying: AX is DX:AX's low
    cld                         ; half for READ_AT and AL for RMDIR, CX the
                                ; byte count for WRITE/READ/APPEND. SI and DI
                                ; are SPENT - all four call sites overwrite
                                ; both on the instruction after the call, and
                                ; all four already banked the caller's pair in
                                ; their own prologue
    mov cx, 13
.c:
    lodsb
    stosb
    or al, al
    jz .done
    loop .c
    mov byte [es:di-1], 0
.done:
    pop cx
    pop ax
    ret

api_name:   times 13 db 0       ; staged names (.text, not .bss: the file
api_name2:  times 13 db 0       ; slots are reachable before anything clears
                                ; .bss, and -f bin clears nothing)
; THE BLOB'S FAR POINTER, and there is exactly one of it (SPEC.md 2.9.6). The
; loading screen and the boot overlay are two sections of one image at one
; address - stage 2 copies the whole rung to HEAP_SEG and publishes the segment
; here - so `SPLCALL splf_step` and `SPLCALL ovl_clk_init` are the same
; instruction pair through the same dword, and the two macros differ only in
; which half of the blob they read well at. The overlay used to have a pair of
; its own seeded FAT_SEG, because it lived in the FAT window until MARK 12.
;
; ONE DWORD AND A MACRO rather than a dword per entry: the segment half is
; written once, by stage 2, and the offset half per call - four bytes of .text
; for the pointer instead of four per site. It is .text and not .bss for the
; reason api_name is: `-f bin` zeroes nothing.
;
; COLD_SEG:mod_gone is the seed rather than zero, which is SPEC.md 2.8.1's rule
; that an unreached slot REFUSES (`stc` / `retf`) instead of naming the
; divide-by-zero vector - and it is what makes a kernel started some other way
; survive a spl_step.
spl_fp:     dw mod_gone
spl_fseg:   dw COLD_SEG

; ...AND AN INTERRUPT MAY NOT BORROW THAT WORD (SPEC.md 15.3.8.3). SPLCALL
; stages its target in [spl_fp] and does the `call far` two instructions later,
; so IRQ0 landing in that window with a SPLCALL of its own sends the MAIN
; LINE's call to the interrupt's target instead. The window is a couple of
; microseconds of every SPLCALL and a boot makes about two hundred of them, so
; it is roughly one boot in fifty - which lands dsk_xfer's per-sector notch on
; the loading screen's animation, silently, and never twice in the same place.
;
; sch_isr therefore stages its own, and copies the segment at call time rather
; than mirroring it: [spl_fseg] is written once when stage 2 publishes the blob
; and once when kmain gives it back, and the copy is two instructions.
spl_ifp:    dw mod_gone
spl_ifseg:  dw COLD_SEG

%ifdef OS88_DRIVERS
api_sysfp:  dw dwf_dskw_write_sys  ; which verb the shared fenced cell runs:
            dw COLD_SEG         ; dskw_write_sys or dskw_append_sys, as the
                                ; far pointer it calls. .text for api_name's
                                ; reason, and written through CS because the
                                ; stub still has the caller's DS when it lands
%endif

; =============================================================================
; Boot (SPEC.md 15)
; =============================================================================

; BPMARK - one phase boundary of the boot profile (SPEC.md 15.5). Nothing at
; all without `make BOOTPROF=1`, which is what lets the marks sit in kmain's
; own line-by-line order instead of in a table somewhere else that has to be
; kept in step with it. bprof_mark preserves the flags as well as every
; register, so a mark may go between any two calls here without changing what
; either of them did.

; BLOBMARK - MARK's twin for a body that lives in the BLOB (SPEC.md 9.4.7)
;
; MARK puts the index BYTE IN THE INSTRUCTION STREAM and mark_here reads it
; `cs:` - which is KERNEL_SEG inside mark_here and the BLOB's segment at an
; overlay call site, so the plain macro reads the wrong memory from `.boot2` and
; then returns to the wrong place. The index goes in AL here and mark_stamp is
; reached through ovw_mark_stamp, which is a far shim.
;
; It lived in clock.inc while clk_init was its only user. mouse_init's eight
; sites joined it, and mouse.inc is %included first - so it is here, beside the
; macro it is the twin of, which is where it should have been anyway.
;
; NOTHING WITHOUT `make BOOTMARK=1`, like every other MARK - and it carries
; BOOTHALT's arm too, which it did not while it had one user: markers 40-47 are
; mouse_init's, and a machine that hangs in the mouse probe is exactly what
; BOOTHALT= is reached for.
%ifdef BOOT_MARK
%macro BLOBMARK 1
    push ax
%if ((%1) % 5) == 0
    mov al, (%1) | 0x80         ; every fifth block is the tall one
%else
    mov al, (%1)
%endif
    call KERNEL_SEG:ovw_mark_stamp
    pop ax
%ifdef BOOT_HALT
%if (%1) == BOOT_HALT
    cli                         ; `make BOOTHALT=n`, exactly as MARK below
%%hlt:
    hlt
    jmp short %%hlt
%endif
%endif
%endmacro
%else
%macro BLOBMARK 1
%endmacro
%endif

%macro MARK 1
%ifdef BOOT_MARK
    call mark_here              ; **FOUR BYTES A SITE, NOT FIVE.** The index
%if ((%1) % 5) == 0             ; used to ride in AL - `mov al, imm8` and a
    db (%1) | 0x80              ; `call`, five bytes forty-two times over - and
%else                           ; it rides in the instruction stream instead:
    db (%1)                     ; mark_here reads the byte its own return
%endif                          ; address points at and steps past it. 42 bytes
                                ; back for 20 spent, and it was worth doing
                                ; because THIS build has the smallest ceiling
                                ; in the tree (SPEC.md 15.1.1) - so it is the
                                ; one that decides whether a feature fits
%ifdef BOOT_HALT
%if (%1) == BOOT_HALT
    cli                         ; `make BOOTHALT=n`: STOP here, so a machine
%%hlt:                          ; that would otherwise reset or loop cannot -
    hlt                         ; the screen stays up to be read, and a boot
    jmp short %%hlt             ; that still goes round proves this was never
%endif                          ; reached
%endif
%endif
%endmacro


%macro BPMARK 1
%ifdef BOOT_PROFILE
    mov al, %1
    call bprof_mark
%endif
%endmacro

; SPLCALL - one entry into the blob, wherever stage 2 put it (SPEC.md 2.9.6).
; It reaches the loading screen and the boot overlay's blob half alike: since
; SPEC.md 2.9.14 those are one section, and the blob's old OVLCALL/OVLGATE/
; OVLGATE1 names - the same instructions under a second name - went with the
; split (OVLCALL is the WINDOW half's entry now, below). There is no ordering
; rule left to get wrong - the overlay used to be at FAT_SEG until MARK 12 and
; reached by an immediate `call FAT_SEG:` before it, which was silent in both
; directions.
;
; **THE GUARD IS THE KERNEL'S, AND IT HAS TO BE** (SPEC.md 2.9.5.1). Every entry
; in the blob already tests [spl_live] as its first instruction - but that test
; is inside the blob, and since kmain gives the blob back to the heap after
; spl_finish those instructions are somebody else's memory. So the test moves to
; this side of the far call.
;
; Retiring the POINTER instead does not work, and the way it fails is the reason
; this comment is long. `mov word [spl_fp], %1` writes the OFFSET half only, so
; a pair aimed at a resident `retf` survives exactly until the next call site
; overwrites the offset with a blob one - and the call then lands at
; KERNEL_SEG:3, which is `cold_entry`'s padding. disk.inc's per-sector
; `SPLCALL splf_step` runs for the life of the machine, so that was every disk
; access after the first desktop frame.
;
; **TWENTY bytes a site** (pushf 1 + cmp word 6 + jbe 2 + mov word 6 + call far
; 4 + popf 1), and the flags are banked across them because the
; busiest caller is inside dsk_xfer's run loop with its own CF in flight. That
; count is why SPLGATE below exists, and why this form still does too: nothing
; in here is a CODE reference - only DS-relative data - so it drops into
; `.text`, `.cold` and `.boot2` unchanged, and those three are three
; address spaces (SPEC.md 2.6). It is the portable form and the expensive one.
;
; SPLGATE - the same call through `.text`'s shared gate, 3 bytes a
; site. **`.text` ONLY**, and that is not a convention: a near call out of
; `.cold` into `.text` is a jump into another segment, which is exactly what
; tools/os88ovlchk.py exists to refuse - so the misuse fails the build rather
; than the machine. kmain's eleven sites are all of `.text`'s, and every one of
; them runs ONCE per boot, so the near call+ret this adds is ~99us on a
; nine-second boot and the per-sector site in dsk_xfer keeps the inline form.
;
; A site names its target and so does its stub, and the NAME is the link: a
; SPLGATE with no matching SPLSTUB is an undefined symbol, not a wrong address.
; That is the whole reason the stub is generated from the target rather than
; written out - SPEC.md 2.9.5.1 is what a second copy of a target looks like
; when it goes wrong.
%macro SPLCALL 1
    pushf
    cmp word [spl_fseg], COLD_SEG
    jbe %%dead
    mov word [spl_fp], %1
    call far [spl_fp]
%%dead:
    popf
%endmacro

; SPLCALLC - the CF-RETURNING form, and the one entry that needs it (SPEC.md
; 2.9.5.4). The macro above BANKS THE FLAGS, so a blob entry that
; answers in CF has its answer thrown away at the `popf` and every `jc` after
; one reads the caller's own carry instead. `drv_boot`'s `ovl_cfg_load` is the
; only such entry in the tree.
;
; It is a second name rather than a change to SPLCALL because the banking is
; load-bearing where it is: the busiest caller is `dsk_xfer`'s per-sector
; `SPLCALL splf_step`, inside a run loop with its own CF in flight. **This form
; must never go there.**
%macro SPLCALLC 1
    cmp word [spl_fseg], COLD_SEG
    jbe %%dead
    mov word [spl_fp], %1
    call far [spl_fp]
    jmp short %%done
%%dead:
    stc                         ; a retired blob means "the defaults stand",
%%done:                         ; never "a record was read"
%endmacro

%macro SPLGATE 1
    call splg_%1
%endmacro

; ...and the SINGLE-SITE form, which is the one most targets want. A stub only
; pays when it is SHARED: 3 at the site + 8 for the stub is 11 against 9
; written out here, so the two forms cross over at two sites. splf_step (three
; sites, all in kmain) and splf_stepq (two, both in mouse.inc) keep their
; stubs; the other ten targets use these. The NAME is still the link - it is
; written at the site and nowhere else, so a target this configuration does not
; assemble is an undefined symbol exactly as a stub for one would be, and the
; per-stub %ifdefs are gone because the site already carries the one that
; matters. Using the shared form without a SPLSTUB is a build error; using this
; form where a stub exists costs 2 bytes and nothing else.
%macro SPLGATE1 1
    mov word [spl_fp], %1
    call spl_gate
%endmacro

; OVLCALL - into the WINDOW half, `.ovl` (SPEC.md 2.5.3; it was OVWCALL and
; `.ovlw` until SPEC.md 2.9.14 gave the name back). A plain far call, where the
; blob half needs a pointer pair: `.ovl` lands on FAT_SEG, which is a CONSTANT
; this assembly knows, so there is nothing to publish and nothing to retire.
; There is no liveness guard either, and that is the trade - the blob half can
; refuse a late call through [spl_fseg] (SPEC.md 2.9.5.1) and this half cannot,
; because the bytes are simply forfeit. tests/ovlrefs.txt is what stands in for
; it: every reference into either half is registered with the reason it runs in
; time, and os88ovlchk asks the two halves different questions.
%macro OVLCALL 1
    call FAT_SEG:%1
%endmacro

; OVBCALL - into whichever half THIS BUILD put the body in (SPEC.md 2.5.3.2).
; A boot-only body that finishes before the first mount may live in EITHER
; half, and the two kernels answer differently: kern_big puts the six OVBCALL
; bodies in `.ovl`, kern_small in the blob, because the region `.ovl` lands
; on is what stops kern_small's mount buffers shedding (the `.ovl` guard at
; the foot of this file, and DSK_NENT in dskwin.inc).
;
; **IT IS NOT A THIRD KIND OF ENTRY** - it expands to OVLCALL or to BLOBCALL
; and nothing else, so there is nothing new at the site to get wrong. What it
; buys is that the arms are written ONCE, here, rather than at every call
; site. Every site is in kmain_o, which is in the blob, so the blob arm is
; BLOBCALL and costs no resident byte (it was SPLGATE1 from `.text`, four
; resident bytes a site, until SPEC.md 2.5.3.3). mouse_init's arm reads
; MOU_IN_BLOB rather than KERN_SMALL, for the knob builds' reason given where
; that symbol is defined.
;
; tools/os88ovlchk.py knows this name (MACHALF) and holds it to the `.boot2`
; arm, which is the kern_small model that every build-conditional `section`
; in the tree is written to - so an OVBCALL aimed at a body that did NOT move
; fails rule 2d exactly as a mismatched SPLGATE1 would, and an OVLCALL aimed
; at one that DID fails it the other way.
%macro OVBCALL 1
%ifidn %1, mouse_init           ; ...the probe follows MOU_IN_BLOB (above
%ifdef MOU_IN_BLOB              ; the %includes), which is KERN_SMALL minus
    BLOBCALL %1                 ; the knob builds
%else
    OVLCALL %1
%endif
%elifdef KERN_SMALL
    BLOBCALL %1                 ; every OVBCALL site is in kmain_o, which is
%else                           ; itself in the blob (SPEC.md 2.5.3.3)
    OVLCALL %1
%endif
%endmacro


; BLOBCALL / MARKW / BPMARKW - the entries, spelled for a caller that
; is itself IN THE BLOB (kmain_o). The blob is one segment, so a
; blob entry is `push cs` + a near call - the far frame its `retf` wants -
; with no pointer and no liveness guard: the caller is executing in the blob,
; so the blob is aboard by construction.
%macro BLOBCALL 1
    push cs
    call %1
%endmacro
%macro MARKW 1
%ifdef BOOT_MARK
    call kmo_mark               ; MARK's own four-byte shape: the index rides
%if ((%1) % 5) == 0             ; in the instruction stream, read `cs:` by a
    db (%1) | 0x80              ; stub that is itself in the blob
%else
    db (%1)
%endif
%ifdef BOOT_HALT
%if (%1) == BOOT_HALT
    cli
%%hlt:
    hlt
    jmp short %%hlt
%endif
%endif
%endif
%endmacro
%macro BPMARKW 1
%ifdef BOOT_PROFILE
    mov al, %1
    call KERNEL_SEG:ovw_bprof_mark
%endif
%endmacro

; SPLSTUB - one three-instruction landing per SHARED target, emitted at the gate
%macro SPLSTUB 1
splg_%1:
    mov word [spl_fp], %1
    jmp short spl_gate
%endmacro


; =============================================================================
; kmain_o - kmain's BOOT HALF, in the blob (SPEC.md 2.5.3.3)
;
; Everything kmain does from its first far call to spl_finish: it runs once,
; and it is given back with the rest of the blob by the mem_unblob_x kmain
; makes on its return. It ENDS by calling spl_finish, so the blob is live for
; the whole body by construction - no argument about call order is needed.
;
; A caller in the blob reaches another blob entry with BLOBCALL (`push cs` /
; near call, 4 bytes): the blob is one segment, and it is
; aboard because this code is running in it. Everything resident is reached
; FAR - OVLCALL for the window half until drv_boot_x's mount (os88ovlchk rule
; 2e scans THIS body for that line now), `call KERNEL_SEG:`/`COLD_SEG:` shims
; for `.text` and `.cold`. MARK and BPMARK are MARKW and BPMARKW here, the
; same numbers spelled for a blob caller.
; =============================================================================
section .boot2
kmain_o:
    OVLCALL  dsk_boot_from_x    ; WHICH VOLUME DID WE COME OFF? (SPEC.md
                                ; 52.10.3) DL and BX:CX are the boot sector's
                                ; handoff and nothing above touches them - the
                                ; segment loads spend AX alone - so this is
                                ; the first instruction that may. On a floppy
                                ; boot BX:CX are 0 and DL is 0 or 1, and all
                                ; this does is store the byte drv_mounted used
                                ; to hardcode. On a hard disk it claims the
                                ; boot partition as a DVK_BIOS row, which is
                                ; what lets the kernel read SYSTEM.CFG and
                                ; load HDD.DRV off the volume that driver
                                ; would otherwise have been needed to reach
    MARKW 1

%ifdef KERN_BIG                 ; the store above 1MB is kern_big's alone
                                ; (SPEC.md 41.11). kern_small is the
                                ; 128KB-floor product and neither sniffs nor
                                ; loads. The tier is still detected in both
                                ; builds: it is a fact about the CPU that
                                ; packages read
                                ;
                                ; ONE CROSSING, NOT TWO. xm_sniff's first
                                ; instruction reads [cpu_tier], so it runs
                                ; cpu_detect itself - two adjacent OVLCALLs
                                ; were 5 resident bytes each and one of them
                                ; buys nothing that a far call from inside the
                                ; overlay, into the segment it is already
                                ; running in, does not. The ORDER is unchanged:
                                ; the tier is published before the sniff reads
                                ; it, and both still run before sched_init
    OVLCALL  xm_sniff        ; ...and CPU_DETECT FIRST. IS there memory
                                ; above 1MB? ONE int 15h
                                ; AH=88h, and on tier 0 one compare (SPEC.md
                                ; 41.12.1). The gate, the sizing, the
                                ; allocator and both transports are XMEM.DRV
                                ; now, read off the disk by xm_boot below and
                                ; only on a machine that answered yes.
                                ;
                                ; The probe is EXACT rather than heuristic -
                                ; it is the same call the overlay sizes the
                                ; store with, so it has no false negatives by
                                ; construction - and asking it BEFORE the A20
                                ; gate is a strict improvement on what stood
                                ; here: a 286 with exactly 1MB used to poke
                                ; port 0x92 and race the keyboard controller
                                ; for D1h/DFh to be told there was nothing up
                                ; there. AH=88h reads CMOS and needs no gate
    MARKW 2
%else
    OVLCALL  cpu_detect         ; kern_small has no sniff to hang it off, so
                                ; the tier probe is its own crossing here
    MARKW 2
%endif
    MARKW 3

    OVLCALL  dsk_dpt_init_x     ; int 1Eh becomes ours (SPEC.md 18.92) before
                                ; any transfer: the ROM's EOT is 8, and every
                                ; multi-sector read past it silently returns
                                ; the OTHER HEAD's sectors
    MARKW 4
    OVBCALL   sched_init         ; pre-emption live from here on. IN THE BLOB
                                ; (docs/plans/LAST-DROP-BYTES.md row 3) - one
                                ; caller, and this is it
    MARKW 5
%ifdef SCH_QUANTUM
    mov al, SCH_QUANTUM         ; `make QUANTUM=` - SPEC.md 53.2.1's sub-tick,
    call KERNEL_SEG:ovw_sch_fast_on            ; armed system-wide instead of per bracket
%endif                          ; (docs/FIELD-NOTES.md 27.4). OFF by default
    BPMARKW 0                    ; SPEC.md 15.5: the PIT clock is ours from
                                ; here, and this also closes the ticks-only
                                ; era - the boot sector's load and the three
                                ; calls above it
    OVLCALL  sch_idle_start     ; PROBE (docs/plans/completed/SCHED-IDLE-PLAN.md 6.2): the idle
                                ; task. Inert while ui_task never sleeps - the
                                ; scan skips its slot, so it is reached only
                                ; where sch_switch used to resume the outgoing
                                ; task. AFTER sched_init, which sets
                                ; sch_idleslot to 0xFF, and after the task
                                ; table is cleared
    ; (no evq_init: the ring's three .bss indices arrive zeroed - events.inc)
    MARKW 6
    OVLCALL  clk_init        ; system clock (SPEC.md 37): probe the RTC,
                                ; or fall back to the fixed date - before the
                                ; mode set, so the very first menu bar paint
                                ; already carries a valid clock
    MARKW 7
    BLOBCALL vid_init               ; video adapter (SPEC.md 39): probe, publish
                                ; the runtime geometry. Re-runs what the
                                ; splash already did, EXCEPT the mode set -
                                ; the loading screen stays up and keeps
                                ; ticking until spl_finish below (15.3)
    MARKW 8
%ifdef KERN_BIG
    OVLCALL  vid_ctx_init       ; ...and bank that geometry as display 0's
                                ; (SPEC.md 39.12). AFTER vid_apply and never
                                ; FROM it: vid_apply runs from the splash while
                                ; the rest of the kernel is still coming off
                                ; the floppy, and a call from there into
                                ; vidsel.inc executes what has not loaded yet
%endif
    MARKW 9
    OVBCALL   vid_probe_avail    ; ...and which OTHER adapters this machine has
                                ; (SPEC.md 39.11.1). AFTER the mode is set, and
                                ; that is the whole correctness argument: a VGA
                                ; in mode 12h decodes A000 only, so B000 and
                                ; B800 are free for a second card to answer at.
                                ; Probed before the mode set, a VGA whose BIOS
                                ; came up in mono text answers at B000 as
                                ; ITSELF and reports a Hercules that is not
                                ; there
    MARKW 10
%ifdef KERN_BIG
    call KERNEL_SEG:cw_vid_disp_init          ; ...and if it has BOTH mono cards, programme
                                ; the second one too (SPEC.md 39.13). Here
                                ; because [vid_avail] is what decides, so this
                                ; is the earliest it can run; it claims nothing
                                ; and draws nothing, so the second monitor comes
                                ; up scanning our raster and black
%endif
    MARKW 11
    OVBCALL   mem_init_x         ; the claim heap (SPEC.md 50): int 12h, the
                                ; empty map. FIRST of the memory users -
                                ; every claim below goes through it
    MARKW 12
    BPMARKW 1                    ; ...the clock, the adapter and the heap
%ifdef DIRTYRAM
    ; --- DIAGNOSTIC ONLY (make DIRTYRAM=1): fill the heap with a pattern -----
    ; QEMU hands the guest zeroed RAM and a real machine does not, so a read
    ; of a claim nothing has written yet is INVISIBLE here and is whatever
    ; the last thing to use that memory left behind out in the field. This
    ; makes that difference reproducible: every heap byte starts 0xAA, so a
    ; claim used before it is filled draws, counts or points somewhere
    ; obviously wrong instead of somewhere accidentally right.
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, [mem_base]
    mov dx, [mem_top]
.dirty:
    cmp bx, dx
    jae .dirtied
    mov es, bx
    xor di, di
    mov cx, 512                 ; 1KB per pass, as words
    mov ax, 0xAAAA
    cld
    rep stosw
    add bx, 64
    jmp short .dirty
.dirtied:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
%endif
    call COLD_SEG:mod_init_x    ; on-demand modules (SPEC.md 2.8): point every
                                ; entry slot at mod_gone. `-f bin` zeroes no
                                ; .bss, so until this runs those slots hold
                                ; whatever was in memory - and they are
                                ; far-call targets. It is here rather than
                                ; lower down because it needs nothing but the
                                ; table itself, and the rule for a thing that
                                ; makes a jump target safe is that it cannot
                                ; be after anything that could jump
    MARKW 13
    BPMARKW 2                    ; ...the claim heap and the module table
%ifdef BAKED_FONT
    OVBCALL  ovl_font_init  ; the typeface this BUILD carries (SPEC.md
                                ; 6.2), out of the overlay - so it needs no
                                ; int 10h and no F000:FA6E, and the machine's
                                ; own ROM font is not consulted at all
%else
    BLOBCALL  font_init          ; needs int 10h, so after the mode is set;
                                ; in the blob on BOTH kernels (SPEC.md 6.0.1)
%endif
    MARKW 14
    BPMARKW 3                    ; ...the typeface
    OVLCALL  wm_init
    MARKW 15
%ifdef BANDCOMP
    call KERNEL_SEG:ovw_band_init              ; SPEC.md 5.9.2: the composer's 2KB, before the
                                ; first title bar and after the heap exists.
                                ; A refusal is survivable - wm_draw_title's
                                ; fifteen-call path is what runs then
%endif
    OVLCALL  menu_init          ; menu bar owner (SPEC.md 12): Locator, so
                                ; the first wm_paint_all already has a bar
    MARKW 16
    OVLCALL  inst_init          ; instance table (SPEC.md 29) - clean boot:
                                ; no app instances exist until launched
    MARKW 17
    BPMARKW 4                    ; ...the window manager, the bar, the table
    BLOBCALL splf_step           ; a notch: the mode set and the font are done.
                                ; ON A HARD DISK THIS IS THE FRAME THAT PUTS
                                ; THE SCREEN UP, at 0 of SPL_POST (SPEC.md
                                ; 2.9.9.1) - so the line below is the first
                                ; thing written under a bar that is about to
                                ; sit still for over a second
%ifdef MOU_DIAG
    mov ax, [ticks]             ; SPEC.md 9.4.6.1: the bar's own 0%% frame
    mov [mdg_n0], ax
%endif
    MARKW 18
    BLOBCALL ovl_spl_msg_mouse   ; ...and SAY SO (SPEC.md 15.6.4). AFTER the
                                ; notch, which is what raises [spl_live]:
                                ; composed before it, the line is never drawn
    OVBCALL   mouse_init         ; IRQ4 live; cursor stays hidden until shown.
                                ; OVBCALL, not OVLCALL: the serial probe is in
                                ; `.boot2` on kern_small (SPEC.md 2.5.3.2)
    MARKW 19
    BPMARKW 5                    ; ...and SPEC.md 9.4.1's two waits, which are
                                ; the largest phase of a boot that is not
                                ; the disk
    BLOBCALL splf_step           ; ...and another: the serial reset holds
                                ; DTR/RTS low for MOU_RSTLOW ticks (~165ms),
                                ; which is the only non-I/O phase up here
                                ; long enough to see (SPEC.md 15.3)
%ifdef MOU_DIAG
    mov ax, [ticks]             ; SPEC.md 9.4.6.1: the bar's own 6%% frame
    mov [mdg_n1], ax
%endif
    MARKW 20
    BLOBCALL ovl_spl_msg_fdd     ; ...and the SECOND stall the hard disk exposed
                                ; (SPEC.md 15.6.4): desk_init asks SPEC.md
                                ; 18.97's TRACK 0 question about unit 1, and a
                                ; drive heading for the absent verdict costs
                                ; FDD_MOTORW + FDD_SEEKW - 32 ticks, 1.76 s,
                                ; the longest single wait in the whole boot
    OVLCALL  desk_init  ; volume zones for the desktop (SPEC.md 26.1)
    MARKW 21
    OVLCALL  dock_init          ; dock strip scratch (SPEC.md 30)
    MARKW 22
    OVLCALL  files_init_x       ; Disk module state (no window at boot)
    MARKW 23
                                ; NO loader_init: all four of the loader's
                                ; resting values ARE zero (LD_OK is 0) and
                                ; .bss is zero at boot (SPEC.md 2.5), so the
                                ; routine wrote zero over zero. The MARK stays
                                ; where it is - they are counted, not named,
                                ; so renumbering would move every BOOTHALT
                                ; number after it for nothing
    MARKW 24
    OVLCALL  drv_init_x         ; the driver table (SPEC.md 51) - BEFORE
                                ; snd_init, whose tone route reads the
                                ; published service table on its first tick
    MARKW 25
    OVLCALL  drv_sniff          ; is there an FM chip at 388h, or a hard
                                ; disk the BIOS knows? (SPEC.md 51.3.1,
                                ; 51.3.2) If so, that row becomes WANTED by
                                ; DEFAULT - the machine's choice, which a
                                ; SYSTEM.CFG overrides only for a row the
                                ; user has answered (51.5.4). HERE and not inside drv_boot,
                                ; because the overlay this lives in is dead by
                                ; then: drv_boot's own mount writes over it
    MARKW 26
    OVBCALL   snd_init   ; sound layer (SPEC.md 34.7): saves the 61h
                                ; boot bits, stores its .bss state, publishes
                                ; snd_live LAST - snd_tick has been running
                                ; gated since sched_init hooked int 08h
    MARKW 27
    BPMARKW 6                    ; ...the desktop, the dock, the driver table

    BLOBCALL splf_step           ; a notch, and the last one kmain spends by
                                ; hand: everything below is sectors, and
                                ; dsk_xfer ticks the bar itself (SPEC 15.3)
%ifdef MOU_DIAG
    mov ax, [ticks]             ; SPEC.md 9.4.6.1: the bar's own 12%% frame
    mov [mdg_n2], ax
%endif
    MARKW 28

    BLOBCALL drv_boot_x         ; ...and load what SYSTEM.CFG asks for
%ifdef KERN_EMU
    BLOBCALL vmm_boot_x         ; ...one of which may be SPEC.md 9.11's
                                ; absolute pointer, which is DRVC_OVL and so
                                ; stops at drv_check with nothing called: the
                                ; kernel is its owner and this is where it
                                ; attaches it. AFTER drv_boot_x because that
                                ; is what read it, and after mouse_init above
                                ; because settling the contest a second time
                                ; is what winning it looks like from here
%endif
%ifndef KERN_SMALL
    mov byte [desk_ready], 1    ; from here on the desktop places things...
%endif
    call COLD_SEG:desk_rowcalc_x    ; every volume the boot added and every
                                ; shortcut and placed drive SYSTEM.CFG put
                                ; back, into their cells in one pass - and
                                ; rowcalc, not the bare reflow, because the
                                ; file is HOSTILE: a cell this screen does not
                                ; have is dropped before the reflow places it
                                ; anew (SPEC.md 26.9)
                                ; (SPEC.md 51.3). Before the first paint, so
                                ; a machine whose sound driver loads has
                                ; sound from the first frame; nothing here
                                ; can stop the boot. NOTHING loads that the
                                ; settings file did not ask for - a driver is
                                ; several seconds of floppy on this machine
    MARKW 29
%ifdef KERN_BIG
    BLOBCALL hb_probe_x         ; ...and is there a hibernation to resume?
                                ; (SPEC.md 87.5) After drv_boot, so a driver
                                ; volume is mounted to be looked at; before
                                ; the first paint, because the answer is a
                                ; window ui_task's first pass puts up
%endif

    ; --- the WINDOW half is dead from here (SPEC.md 2.5.3) -------------------
    ; drv_boot's mount has taken the FAT window `.ovl` was read onto, so
    ; nothing below may OVLCALL (os88ovlchk rule 2e scans this body). The BLOB
    ; half - this code included - lives on until kmain's mem_unblob_x, after
    ; spl_finish below, so there is no claim to free and no pointer to retire
    ; here. What used to be here was a 4KB mem_free at the TOP of the heap and
    ; a second far pointer to neutralise.
    BPMARKW 7                    ; ...SYSTEM.CFG and whatever it asked for

%ifdef KERN_BIG
    BLOBCALL xm_boot_x          ; ...and the store above 1MB, if xm_sniff
                                ; found any (SPEC.md 41.12). Here rather than
                                ; inside drv_boot because XMEM.DRV is an
                                ; OVERLAY and not a driver: no drv_tab row, no
                                ; Drivers-page tick, no SYSTEM.CFG bit and
                                ; nothing for a user to decide. Still on the
                                ; splash bar - dsk_xfer ticks per sector - and
                                ; still before the first paint. A failure is
                                ; silent by design (SPEC.md 41.12.4)
%endif
    MARKW 30

%ifdef OS88_THEME
    mov al, [thm_kind]          ; RESOLVE THE PALETTE FROM THE KIND, once, before
    call KERNEL_SEG:cw_thm_set ; anything is drawn (SPEC.md 76.12.1). Two
                                ; things need it and only one of them has run:
                                ; drv_cfg_unpack calls thm_set when SYSTEM.CFG
                                ; carried a theme, and a machine with no
                                ; settings file at all - a fresh image, or the
                                ; THEMEDARK knob - would otherwise boot with
                                ; [thm_kind] and the eight live bytes
                                ; disagreeing. It is also where the ADAPTER
                                ; refusal lands for a settings disk carried
                                ; from a VGA machine to this one, [vid_kind]
                                ; being long since probed
%endif
    MARKW 31

    BLOBCALL spl_finish         ; the bar to 100% and the screen handed back:
                                ; the paint kmain makes on return covers every
                                ; pixel of it, so the loading screen needs no
                                ; erase. The LAST call of the body: kmain
                                ; releases the blob as soon as this returns

    retf
%ifdef BOOT_MARK
kmo_mark:                       ; MARKW's landing - mark_here's shape, one
    pushf                       ; segment along: the byte is read through the
    push bp                     ; BLOB's CS, which is where it is
    mov bp, sp
    push bx
    mov bx, [bp+4]              ; the return address (flags and bp above it)
    mov al, [cs:bx]
    inc word [bp+4]             ; ...and the `ret` below steps past it
    pop bx
    pop bp
    popf
    call KERNEL_SEG:ovw_mark_stamp
    ret
%endif

section .text

kmain:
    cli
%ifdef BOOT_MARK
    call mark_beacon            ; BEFORE anything: no state of ours is needed
%endif
    mov ax, KERNEL_SEG          ; the boot sector jumped here with its own
    mov ds, ax                  ; segments; setting ours up is our job
    mov es, ax
%ifdef BOOT_MARK
    MARK 50                     ; ...and the first one down the ORDINARY path,
                                ; while SS is still the boot sector's. A corner
                                ; with no 50 beside it means the drawing helpers
                                ; are what is broken, not the boot
%endif
    mov ax, LOW_SEG             ; SS is NOT KERNEL_SEG (SPEC.md 2.1): the task
    mov ss, ax                  ; stacks sit in their own segment just above
    mov sp, STK0_TOP            ; the image, so a stack offset stays small and
    sti                         ; the kernel's own 64KB window stays for code
    cld

%ifdef BOOT_MARK
    call mark_prev              ; what the PREVIOUS boot reached, if this machine
                                ; went round rather than stopping...
    call mark_hook              ; ...and take the fault vectors, so the next one
                                ; stops with its number up instead of resetting
%endif
    MARK 0
    SPLGATE1 kmain_o            ; ...the whole boot, from the blob, up to and
                                ; including spl_finish (SPEC.md 2.5.3.3). MARK
                                ; 1-31 are in there, as MARKW

    ; --- ...AND STAGE 2 GOES BACK TO THE HEAP (SPEC.md 2.9.5, 50.6.3) -------
    ; The line above is the last thing that ever wants the loading screen, so
    ; this is where its BOOT2_PAD bytes stop being reserved. They are not a
    ; claim and there is nothing to free: the loader is at the heap's floor
    ; and mem_init simply started the arena above it. mem_unblob puts
    ; [mem_base] back on the floor and compacts, which is what turns the
    ; release into part of the long run instead of a hole under kern:FATW.
    ;
    ; ONE WORD RETIRES IT, and it is the one the macro's guard asks (SPEC.md
    ; 2.9.5.3). Pointing `spl_fp` at a resident `retf` was tried and lasted
    ; exactly until the next SPLCALL, which writes the OFFSET half alone
    ; (2.9.5.1); the guard then asked [spl_live], which is a different fact
    ; and cost a hard-disk boot its whole boot overlay (2.9.9). COLD_SEG is
    ; the image's own default for this word - mod_gone - so putting it back is
    ; putting it back, not inventing a sentinel.
    mov word [spl_fseg], COLD_SEG
    call COLD_SEG:mem_unblob_x
    MARK 32
    BPMARK 8                    ; ...the store above 1MB, the palette, the bar

    mov bp, wm_paint_all
    call ui_lcall
    BPMARK 9                    ; ...and the first desktop frame

    ; --- stop the boot timer (SPEC.md 15.4) ----------------------------------
    ; HERE, not after cursor_show: the question is when the first desktop
    ; FRAME is finished, and gfx_unlock is what puts it on the glass. The
    ; cursor is not the desktop.
    cmp word [boot_ticks], 0xFFFF   ; unstamped: leave it saying unknown
    je .nobt
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, [0x046C]            ; the same BIOS tick the boot sector read. Our
    pop ds                      ; own int 08h hook chains it, so it never
    sub ax, [boot_ticks]        ; stopped ticking. One word: 65,536 ticks is
    mov [boot_ticks], ax        ; an hour, and a boot is not
.nobt:

    call cursor_show

    mov ax, [ticks]             ; TIME THE HOT-PLUG POLLER FROM THE DESKTOP
    mov [mou_hpt], ax           ; (SPEC.md 9.4.8). [mou_hpt] is the poll
                                ; interval's base and starts at 0, and [ticks]
                                ; started at sched_init, so the poller's first
                                ; DTR drop fired the instant [ticks] passed
                                ; MOU_REPOLL - which on a boot that reaches the
                                ; desktop past tick 55 (a hard disk, or any
                                ; driver) is the FIRST UI pass, dropping the
                                ; mouse's power under the user's hand. Based
                                ; here, the ~3s interval is measured from when
                                ; the pointer first exists: a user who moves
                                ; inside it settles the port and the poller
                                ; never drops. "The poller fired" is read off
                                ; [mou_hpst] >= 1 instead (SPEC.md 9.4.2). AX
                                ; is dead - the boot timer above stored its
                                ; count

    call COLD_SEG:drv_notice_x  ; ...and only NOW say what did not load: a
                                ; window needs a screen that has been painted
    BPMARK 10                   ; ...the cursor and the driver notice

%ifdef MOU_DIAG
    call mdg_show               ; SPEC.md 9.4.6: what mouse_init's identify
%endif                          ; window saw. NEAR - moudiag is `.text`
%ifdef BOOT_PROFILE
    call COLD_SEG:bpf_bprof_show ; SPEC.md 15.5: the table on the glass, LAST,
                                ; so nothing above paints over it. It draws on
                                ; the DESKTOP rather than into a window - a
                                ; window is wm state the user then has to
                                ; close, and this should go on the first repaint
%endif

%ifdef STK_DIAG
    call sd_start               ; docs/plans/completed/STACK-SLOTS-PLAN.md: the diagnostic task
                                ; and its panel. AFTER the desktop, so the
                                ; first paint has somewhere to land, and after
                                ; BOOT_PROFILE's table so the two knobs do not
                                ; fight over the same pixels
%endif

    jmp ui_task                 ; task 0 becomes the UI task; never returns

; =============================================================================
; osapi helpers (SPEC.md 20.4) - tiny accessors for loaded programs, reached
; only through the jump table. Each preserves all registers except its
; documented outputs.
; =============================================================================

; OSAPI_BOOT_TICKS, OSAPI_GET_TICKS, OSAPI_SET_COLOR, OSAPI_SRAND and
; OSAPI_VOL_SYS had bodies here that were one `mov` and a `ret`: they are
; INLINE cells now (SPEC.md 20.3), and the table is where each one lives.
; The boot timer is system ticks, 18.2065 Hz, from the boot sector's first
; instruction to the first desktop frame, 0xFFFF = never stamped (15.4).

; osapi_mouse lives in kernel/mouse.inc beside the IRQ it re-arms (SPEC.md 9.13).

; ---- osapi_rand - seed = seed*25173 + 13849; out: AX = new seed --------------
osapi_rand:
    push dx                     ; mul clobbers DX; only AX is an output
    mov ax, [osapi_seed]
    mov dx, 25173
    mul dx                      ; DX:AX = seed * 25173; keep the low word
    add ax, 13849
    mov [osapi_seed], ax
    pop dx
    ret

; ---- osapi_font_glyphs - the kernel's own 8x8 font (SPEC.md 6/20.3) ---------
; out: DX:SI = the glyph table, AL = FONT_FIRST, AH = FONT_LAST, CX = 8 bytes
;      per glyph, row 0 first, bit 7 leftmost.
;
; DX is an AMENDMENT to a shipped slot, which SPEC.md 20.8 rule 4 otherwise
; forbids: the cell used to answer with SI alone and the table was an offset
; in KERNEL_SEG. It moved to LOW_SEG to give KERN_CODE_MAX back 760 bytes - the
; largest single object the kernel's own segment was carrying - and an offset
; without a segment cannot say where it went. Recorded as a one-time exception
; on the same terms as slots 0x0120/0x0128: exactly ONE package reads this
; cell (apps/paint's text tool), it is in this tree, and `make` rebuilds it.
; A package that ignores DX now reads the wrong segment, so this invalidates
; every .o88 the same way a renumbering would.
;
; For an app that draws text into its OWN pixels rather than onto the screen
; (apps/paint's text tool). Before this it re-probed the ROM font through
; int 10h and carried the kernel's F000:FA6E fallback - 40 lines to arrive
; at a table the kernel already had.
osapi_font_glyphs:
    mov si, [font_base]         ; wherever font_init found it: the ROM's own
    mov dx, [font_seg]          ; table, or a baked face's copy in LOW_SEG
    mov ax, FONT_FIRST | (FONT_LAST << 8)   ; AL and AH in one instruction
    mov cx, 8
    ret

; ---- osapi_file_dfree - free space where this app's writes will land -------
; The same V as the three name cells (SPEC.md 19.2.1), and for a sharper
; reason than symmetry: an app asks this to find out whether its save will
; fit, so an answer about a volume other than the one the save is going to
; is not merely stale, it is about the wrong disk.
osapi_file_dfree:
    call inst_vol_enter
    call COLD_SEG:dwf_dskw_dfree    ; its outputs and CF are ours. DIRECTLY,
    ret                             ; not through a dskw_dfree shim: this was
                                    ; the one cold shim in the kernel whose
                                    ; only reference was a call - every other
                                    ; one is an API cell or a kind template,
                                    ; where the near entry is the contract

; ---- osapi_vol_stat - the volume this app stands on, in four registers ------
; osapi_file_dfree's V and for its reason (SPEC.md 19.2.1): an app asks this
; about the disk its writes are going to, so an answer about the machine's
; idea of "current" would be about the wrong one.
%ifndef KERN_SMALL
osapi_vol_stat:
    call inst_vol_enter
    call COLD_SEG:dwf_dskw_vstat    ; AX/BX/CX/DX and CF are ours
    ret
%else
osapi_vol_stat equ osapi_snd_fm     ; DOS-only, and the DOS box is not on the
                                    ; small disks (SPEC.md 18.4.7.4): the cell
                                    ; refuses with snd.inc's kern_small FM
                                    ; body, the same `stc` / `ret`
%endif

; ---- osapi_file_here / osapi_file_goto - the volume's location (SPEC.md 19.2)
;
; **These two answer for the CALLING INSTANCE now** (SPEC.md 19.2.1), not for
; the machine, and the change is worth reading because it turns this pair
; from a requirement into a convenience.
;
; What it used to say here was: the file API resolves every name in the
; volume's current directory, that is one global word shared by every window,
; so an app that means "where I saved last time" has to bank the pair and put
; it back - and four packages duly did (Note Pad, Tracker, Paint, ArtfulType),
; each with its own copy of the same six lines, each of them a bug waiting for
; the fifth package not to write them. The instance owns its directory now, so
; the kernel does that banking underneath every file cell (inst_vol_enter) and
; no package has to do it at all.
;
;   osapi_file_here   out DX = the calling instance's current directory (0 =
;                     the root), BL = its drive (0 = A:, 1 = B:). No disk I/O.
;   osapi_file_goto   in  DX = a cluster from osapi_file_here, BL = its drive;
;                     MOVES that instance there, and the machine with it.
;                     out CF=1 the volume could not be listed there and is
;                     back at the root with the write gate shut. This is a
;                     REMOUNT (SPEC.md 19.2) - real floppy I/O, UI-task
;                     context only, exactly like the other file slots.
;
; The four existing callers keep working unchanged: `here` still answers the
; folder the dialog just committed to (fdlg_home_save records it into the
; instance before the completion callback runs), and their `goto` back to it
; is now a compare that finds the volume already there. Redundant, not wrong -
; which is the only way to retire a duty a published slot handed to apps.
;
; A caller with no instance behind it - the kernel, a driver - gets the
; machine's own position, exactly as before.
;
; **A DRIVER only gets that because drv_call and drv_cp_call arrange it**
; (SPEC.md 51.2.3), and this sentence was a statement of intent until they
; did. A driver is not an instance, but inst_caller answers with the
; DISPATCHED CALLBACK's stamp, and the kernel enters a driver from inside
; one - ticking its row on the Drivers page is a Control Panel click. So this
; cell reported where the PANEL was standing, the hard-disk driver banked that
; as the system volume at DRVV_READY (SPEC.md 52.11), and Install then said
; "Need the system disk" with the system disk in the drive. The two entry
; points clear the stamp for the length of the call; the other two,
; drv_svc_call and drv_blk_call, deliberately do not and cannot reach here.
; -----------------------------------------------------------------------------
; DX and BL are the outputs; SPEC.md 1 makes everything else this routine's
; to preserve, BH and AX included.
osapi_file_here:
    push ax                     ; the reading is instance.inc's inst_where (DL
    call inst_where             ; = drive, AX = directory, the machine's own for
    mov bl, dl                  ; no instance), which banks BX - so BH is still
    mov dx, ax                  ; the caller's here - and spends only DH, an
    pop ax                      ; output
    ret

; OSAPI_VOL_SYS (out BL = [dsk_bootvol], nothing else touched, flags
; included) is an INLINE cell (SPEC.md 20.3). [dsk_bootvol] is A: on a floppy
; machine and the installed PARTITION on one that boots from its hard disk
; (SPEC.md 52.10.3); the cell hands that fact OUT, so a package can reach the
; system disk's own folders - SYSTEM/FONTS is the first (SPEC.md 19.8) -
; without guessing a drive letter or walking every volume looking for one.

osapi_file_goto:
    push ax
    mov ax, dx
    mov dl, bl
    call COLD_SEG:dkf_dsk_chdir ; CF = the mount failed; it has already put
    jc .out                     ; the volume back at the root
    call inst_vol_mark          ; ...and the instance follows the machine:
                                ; this is a deliberate move, so it is where
                                ; that app now believes it is standing
.out:
    pop ax
    ret

; ---- osapi_file_goto_q - stand here to READ OR WRITE, not to list ------------
; in:  DX = the folder's first cluster (0 = the root), BL = the volume
; out: CF=0 moved; CF=1 and AX = FERR_*
;
; SPEC.md 19.2.2. The same two quiet paths fcp_goto has had since SPEC.md
; 22.5, published because a COPY ENGINE OUTSIDE THE KERNEL needs them just as
; much as the one inside it - the hard-disk installer was paying a full
; dsk_chdir per file and per chunk, twice, and a full chdir is the BPB, the
; FAT window, the directory scan, the sort and one icon harvest per file.
;
;   * A move INSIDE the current volume is a word. Nothing a caller can do
;     between two of these reads the LISTING: dskw_* resolve names by walking
;     [dsk_cwd]'s raw directory sectors, and so does dsk_find (SPEC.md
;     19.7.1). The FAT window and every derived geometry belong to the
;     VOLUME, which has not changed.
;   * Crossing to another volume is dsk_chdir_q (SPEC.md 18.9), which keeps
;     the BPB and the FAT window and skips the scan, the sort and the
;     harvest - and SPEC.md 18.8.2's banked window means the FAT usually is
;     not re-read at all.
;
; The cluster is range-checked HERE because the quiet path skips
; disk_mount's own .cwd_lost validation - fcp_goto's reason, and the same
; two compares.
;
; NOT a replacement for OSAPI_FILE_GOTO: this leaves the global listing empty
; and [dsk_lstale] raised, so a caller that is going to SHOW a folder wants
; the other one. The instance's folder is deliberately NOT moved either
; (no inst_vol_mark): this is where the caller is standing to do a job, not
; where the application now believes it lives.
; **THIS WAS A SECOND TRANSCRIPTION OF `fcp_goto`**, and SPEC.md 19.2.2 says
; so in as many words: the slot is "fcp_goto's two paths, which the kernel's
; own copy engine has had since 22.5, published because a copy engine outside
; the kernel needs them just as much". Two bodies, in the scarce section, and
; they had DRIFTED APART in both directions - this one had the [dsk_mntok]
; gate and fcp_goto did not; fcp_goto had the DVK_FILE / FSV_CHDIR arm and
; this one did not. The second of those was a BUG here (SPEC.md 62.9.1): on a
; redirected volume this wrote [dsk_cwd] behind the driver's back, and every
; name the caller then asked about was looked up in the folder it came FROM.
; Merging fixes it and gives fcp_goto the gate; the argument shuffle is four
; instructions and `pop` does not disturb CF, so .out returns fcp_goto's flag.
osapi_file_goto_q:
    push bx
    push dx
    mov al, bl                  ; fcp_goto takes AL = drive, BX = cluster
    mov bx, dx
    call COLD_SEG:fcpf_fcp_goto ; ...already published for DISKOPS.DRV (18.99.8)
    jc .out
    xor ax, ax                  ; the documented AX = 0, and it clears CF with
.out:                           ; it; on the CF=1 arm AX is fcp_goto's FERR_*
    pop dx
    pop bx
    ret

; ---- osapi_file_goto_qm - the quiet stand that MOVES THE INSTANCE (74.1) -----
; in:  DX = the folder's first cluster (0 = the root), BL = the volume
; out: CF=0 moved and marked; CF=1 and AX = FERR_* (nothing marked)
;
; osapi_file_goto_q and then inst_vol_mark, which is the one line the two
; differ by. GOTO_Q deliberately leaves the instance's own folder alone, and
; every package file cell first re-stands the machine there (inst_vol_enter,
; SPEC.md 19.2.1) - so a package that WORKS in a folder that is not its own
; (RunCPM's CP/M drives are folders, SPEC.md 74.3, and every BDOS call may
; change one) was undone by its very next read. This is the quiet move that
; sticks: no listing, no sort, no icon harvest, and the instance follows.
; It is what OSAPI_FILE_GOTO does minus the dsk_chdir, and inst_vol_mark
; preserves the flags, so GOTO_Q's CF answer is the answer.
osapi_file_goto_qm:
    call osapi_file_goto_q
    jc .out
    jmp inst_vol_mark
.out:
    ret

; ---- osapi_video - the screen the program actually got (SPEC.md 39.2) --------
; out: AX = width, BX = height, CX = first row the dock owns (so the usable
;      desktop is rows MBAR_H..CX-1), DL = 0 VGA / 1 Hercules / 2 CGA,
;      DH = bits per pixel, 4 or 1
; -----------------------------------------------------------------------------
; osapi_vol_kind_x - what KIND of volume is this? (SPEC.md 18.7.2, slot 0x0190)
; in:  AL = a volume index (0 = A:, 1 = B:, ...)
; out: CF=1 = no such volume; CF=0 with AL = VK_* and AH = VT_*
; clobbers: AX (the output), flags
;
; The first thing a package can ask about a volume other than by using it, and
; it exists because sysbench could not: its hard-disk block probes volume
; index 2 with a FILE_GOTO and assumed a hard disk, which SPEC.md 18.98's
; external floppies made false. OSAPI_VOL_* are the DRIVER's and refuse a
; package (SPEC.md 51), so there was no way to ask at all.
;
; TWO answers because they are two questions: a package wanting "is this a
; hard disk" wants the MEDIUM, and one wanting "will this go through a driver"
; wants the TRANSPORT. Conflating them is the bug this section exists for.
; -----------------------------------------------------------------------------
;
; COLD, and the cell names it (SPEC.md 20.3.2): a package asks this once, and
; the body's far return replaces a resident body plus the `.text` thunk it
; called into the cold half through (size pass 9, -6 resident). The body is
; with the ct_ trampolines below the %includes, because `.cold`'s first byte
; is vga12.inc's and this file's top half comes before it.

osapi_video:
    mov ax, [vid_pwm1]          ; THE PRIMARY, NOT THE DESKTOP UNION (39.2.1).
    inc ax                      ; [vid_w]/[vid_h] are the whole virtual
    mov bx, [vid_phm1]          ; desktop once a second display is extended,
    inc bx                      ; and a package has no use for that number:
                                ; windows open on the primary, wm_fit confines
                                ; a frame to one display, and everything a
                                ; package draws is window-relative. Answering
                                ; the union made Arkanoid lay itself out for a
                                ; 1360px screen and then draw 115px of that
                                ; layout onto the OTHER MONITOR.
                                ;
                                ; The slot was already answering the primary
                                ; for CX below - the dock is chrome and chrome
                                ; is the primary's - so it was mixing the two
                                ; and could not have been right either way.
                                ; Identical on every one-display machine, where
                                ; [vid_pwm1]+1 IS [vid_w].
    mov cx, [vid_dock_y0]
    mov dl, [vid_kind]
    mov dh, 4
    cmp byte [vid_mono], 0
    je .r
    mov dh, 1
.r:
    ret

osapi_seed:  dw 0                ; PRNG state (inline data: .bss takes no init)

; =============================================================================
; KFZ - the kernel breadcrumb (make KFZ=1; ships nowhere)
;
; For a reported hard freeze on ANY key ModPlug takes, on a machine with a
; hard disk mounted, that no emulator in this container reproduces. The
; package-side instrument (MPPDBG, apps/modplug) came back reading ZERO, so
; W_ONKEY is never reaching the package and the stall is in ui_task's own
; `.keys` branch - between int 16h and wm_pkgcall.
;
; ONE BYTE STORE, and it draws nothing. It painted raw framebuffer cells at
; x = 0 once, and lost that ground to the heartbeat, which needs the width;
; what it does now is set the byte the heartbeat paints AS ITS THIRD CELL, so
; a photograph of a stopped machine carries the mark as well as everything
; else. That also removes what made the first version delicate - three of the
; sites it marks run before gfx_lock is taken and one of them IS gfx_lock, so
; anything that drew through the gfx_* slots could deadlock on exactly the
; routine under suspicion.
;
; A later mark overwrites an earlier one, so the value is the HIGHEST step the
; last keystroke reached. It preserves every register and the flags - CF is
; live across blk_wake and kbm_key - which `mov` gives for free, and CS = DS
; in the kernel's near model so the store needs no segment setup.
; =============================================================================
; DDG - DRVDIAG=1's step mark (Makefile, sched.inc's ddg_paint): which step
; of loading a driver the boot is in. A plain store, flags untouched.
%macro DDG 1
%ifdef DRV_DIAG
    mov byte [ddg_step], %1
%endif
%endmacro
%ifdef DRV_DIAG                 ; ...and the knob is kern_big's alone: what it
 %ifndef OS88_DRIVERS           ; watches is drv_boot, which kern_small has not
  %error "DRVDIAG=1 is kern_big only: kern_small has no drv_boot and no drv_tab"
 %endif
%endif

%macro KFZ 1
%ifdef KFZTRACE
    mov byte [khb_kfz], %1
%endif
%endmacro

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; CURUNLAZY - spend gfx_lock's deferred cursor hide (SPEC.md 7.1.4), with the
;             test that answers "already spent" INLINE.
;
; [cur_lazy] is one-way within a lock hold: the first drawing call clears it
; and every call after it finds nothing to do. cur_unlazy's body is 67 cycles
; measured and the call frame around it is most of what a NOT-owed hide cost -
; and NINETEEN sites in the drawing layer pay it, on every fill, line, glyph
; run, blit and scroll in the machine.
;
; Seven bytes a site to stop paying a near call and a return to be told no.
; The routine stays: this is its guard hoisted out, not a second copy of it,
; and the one .cold caller still reaches it through KERNEL_SEG:cw_cur_unlazy.
; -----------------------------------------------------------------------------
%macro CURUNLAZY 0
    cmp byte [cur_lazy], 0
    je %%spent
    call cur_unlazy
%%spent:
%endmacro

%include "dskwin.inc"           ; THE FIRST INCLUDE, and that is the whole of
                                ; the mechanism (SPEC.md 2.1.2): `.lowbss` is
                                ; laid out in the order its contributors
                                ; appear, so the mount-owned buffers are its
                                ; first bytes only while this line is first.
                                ; tests/unit/t_lowwin.py is what notices if a
                                ; new include ever lands above it

%include "viddet.inc"           ; video adapters (SPEC.md 39): the splash
                                ; probes and sets the mode on its first tick,
                                ; so this must be resident with it

%include "splash.inc"           ; SPEC.md 2.9.4: the loading screen is STAGE
                                ; 2's now - it is drawn from the loader that
                                ; draws it, and its 967 bytes have left .text
                                ; entirely. The module opens `.boot2` and
                                ; hands .text back ITSELF, which is the shape
                                ; every other module out of .text has and the
                                ; one tools/kernsize.py can read: its
                                ; per-module markers give each %include the
                                ; section they end in, so a `section` wrapped
                                ; round this one here put splash back in the
                                ; image and the whole per-module pass refused
                                ; itself. What stays behind is [spl_live]
                                ; and four shims. This used to say "must be
                                ; resident within the image's opening
                                ; SPL_RESIDENT sectors (SPEC.md 15)
%ifdef KERN_BIG
; EXTD.DRV's entries (SPEC.md 39.19.6, kernel/extmod.inc). EXTCALL is the
; whole dispatch: a far call straight at the slot mod_need armed, with no
; thunk, because a slot at rest is mod_gone (CF = 1, registers kept) and
; every caller either sits behind a [vid_ndisp] > 1 test - which cannot be
; true without the image - or takes that refusal as the one-display answer.
; Here and not in extmod.inc because vidsel.inc, wm.inc, ui.inc and fsx.inc
; all call it and every one of them is %included first.
EXT_EXTEND  equ 0               ; vid_disp_init's Extend arm; CF = 1 Single
EXT_LAND    equ 1               ; ui_drag_size, CF INVERTED (1 = not moved)
EXT_STRADS  equ 2               ; wm_strads: CF = 1 not straddling
EXT_APPLY   equ 3               ; wm_strads + wm_strad_fit; CF = 1 not
EXT_FITBOX  equ 4               ; wm_fit_box: registers kept = the primary
EXT_KINDNOW equ 5               ; wm_kind_now's arm: AL kept = the primary's
EXT_FSX     equ 6               ; fsx.inc's second-display arms, by AH:
EXTF_ENTER  equ 0               ;   vid_fsx_enter (AL = the display)
EXTF_LEAVE  equ 1               ;   vid_fsx_leave
EXTF_UNBLANK equ 2              ;   vid_fsx_unblank
EXTF_KIND   equ 3               ;   fsx_mode: BL = [fsx_wdisp]'s kind
EXTF_CAPS   equ 4               ;   fsx_caps: DL = window BX's display's kind
EXT_DISPLAY equ 7               ; wm_display's secondary arm, behind its gate
EXT_SPAN    equ 8               ; wm_disp_span: registers kept = the primary
EXT_FSRECT  equ 9               ; wm_fs_setrect's arm: CF = 1 one display
EXT_DESK    equ 10              ; vid_disp_desk: CF = 1 nothing painted
EXT_YLOW    equ 11              ; ui_ylow's arm, behind its caller's gate
%macro EXTCALL 1
    call far [EXFP + %1*4]
%endmacro
%endif
%include "vidsel.inc"           ; which adapters the machine HAS, and moving
                                ; between them at run time (SPEC.md 39.11).
                                ; AFTER splash.inc and not beside viddet.inc,
                                ; because nothing here is reachable from the
                                ; splash and everything above it eats that
                                ; module's residency budget
%include "cpudet.inc"           ; CPU tiers + the A20 line (SPEC.md 41.1-41.3)
%include "xmem.inc"             ; the kernel's HALF of the store above 1MB
                                ; (SPEC.md 41.12): four cells, a boot sniff
                                ; and the dispatch. The gate itself, the
                                ; sizing, the allocator and both transports
                                ; are drivers/xmem/xmem.asm.
                                ;
                                ; It reads driver.inc's drv_load_at, drv_call,
                                ; drv_release and DRVR_/DRVC_ vocabulary, all
                                ; of which are BELOW it - NASM resolves the
                                ; labels and the equs across passes, and the
                                ; position is kept where it was so that
                                ; kern_small comes out byte-identical rather
                                ; than merely the same size (docs/
                                ; KERN-SPLIT-PLAN.md 1.1: a divergence that
                                ; hides in the rung's padding is still real)
%include "vga12.inc"
%include "softgfx.inc"

; --- SPEC.md 39.27.5: A NO-VGA ARM THAT IS FALLEN INTO MUST EMIT CODE --------
; Here and not at the foot of vga12.inc, which is where it belongs and where
; the sw_* names are not defined yet - softgfx.inc is the include above.
;
; Four entries in vga12.inc are reached by FALL-THROUGH, from a GFXDISP that
; expands to nothing on kern_small or from a plain label. Writing the no-VGA
; arm as `equ sw_x` is the obvious thing and is wrong: an `equ` emits no code,
; so the entry falls past it into the next routine. It assembles, it boots, and
; gfx_fill_gray runs osapi_gfx_fill_pat's body - which is how it shipped, and
; PERFORMANCE.md Set 113c is what found it, a bench row rather than a gate.
;
; vga12.inc's NOVGA_ARM macro carries an assertion of its own and it CANNOT
; catch this: writing `equ` instead of the macro does not invoke the macro, so
; the assertion goes with it. This is the check that survives that, and putting
; the `equ` back is what tests it.
%ifndef GFX_VGA
 %if gfx_fill_gray_raw == sw_fill_gray
  %error "gfx_fill_gray_raw is an `equ` - the entry above FALLS THROUGH to it and an equ emits no code (SPEC.md 39.27.5)"
 %endif
 %if gfx_fill_pat_raw == sw_fill_pat
  %error "gfx_fill_pat_raw is an `equ` - see SPEC.md 39.27.5"
 %endif
 %if vga_save_vram == sw_save
  %error "vga_save_vram is an `equ` - see SPEC.md 39.27.5"
 %endif
 %if vga_restore_vram == sw_restore
  %error "vga_restore_vram is an `equ` - see SPEC.md 39.27.5"
 %endif
%endif
%include "font.inc"
%include "band.inc"             ; the 1bpp band composer (SPEC.md 5.9), which
                                ; is a KNOB again since 5.9.6 (`make BAND=1`)
                                ; and assembles to nothing without it: after
                                ; font.inc for font_glyphs and FONT_FIRST, and
                                ; after vga12.inc for gfx_blit1_x - it calls
                                ; both and defines neither
%include "mouproto.inc"        ; the serial packet's arithmetic, shared as SOURCE
                              ; with kerndos/kdmouse.inc (SPEC.md 96.45)
%include "mouse.inc"
%include "vmmouse.inc"          ; the VMware absolute pointer (SPEC.md 9.11) -
                                ; a row, a gate and a pump; the protocol and
                                ; its 386 island are VMMOUSE.DRV. AFTER
                                ; mouse.inc, which defines mou_apply_abs and
                                ; MOUPRIV_ENTER, and BEFORE sched.inc and
                                ; ui.inc, which use the VMM_POLL macro
%include "bootprof.inc"       ; the boot phase table (SPEC.md 15.5), BOOTPROF=1
%include "stkdiag.inc"        ; what an interrupt costs a task stack
                              ; (docs/plans/completed/STACK-SLOTS-PLAN.md), STKDIAG=1
%include "kbddiag.inc"        ; what the keyboard sent (SPEC.md 9.8.1),
                              ; KBDDIAG=1
%include "moudiag.inc"        ; ...and what the identify window saw (SPEC.md
                                ; 9.4.6), MOUDIAG=1. Both are knob-only and
                                ; both draw on the finished desktop, because
                                ; the machines they answer questions about are
                                ; the ones with no debugger
                              ; only: after sched.inc, whose PIT programming its
                              ; clock depends on, and before nothing - kmain
                              ; forward-references bprof_mark through the macro
%include "sched.inc"
%include "events.inc"
%include "clock.inc"            ; the system clock (SPEC.md 37): after
                                ; sched.inc, whose [ticks] it advances from
%include "blank.inc"            ; the idle screen blanker (SPEC.md 64): after
                                ; sched.inc for [ticks], events.inc for the
                                ; evq_pop/wm_wake_eaten drain that spends the
                                ; wake press (SPEC.md 74.1.1 - it was an
                                ; evq_init, a reset that ate the wakes, and
                                ; that routine no longer exists at all), and
                                ; vidsel.inc for the vid_blank_kind pair -
                                ; which is every module it reads, so this is
                                ; the first place it can go with none of its
                                ; dependencies still ahead of it
%include "wm.inc"
%include "memory.inc"           ; the claim heap (SPEC.md 50): after
                                ; instance.inc, whose records own the claims
%include "clip.inc"             ; the system clipboard (SPEC.md 55): after
                                ; memory.inc, whose heap the text lives in
%include "instance.inc"
%include "menu.inc"
%include "fprog.inc"          ; the file-operation progress widget (SPEC.md
                              ; 12.8): after menu.inc, whose bar geometry it
                              ; sits beside and whose menu_draw_bar gives the
                              ; borrowed pixels back; before disk.inc, which
                              ; steps it per sector
%include "toast.inc"          ; the transient one-line message (SPEC.md 59):
                              ; the bar's other tenant, beside fprog.inc for
                              ; the same reason. It sits in the CLOCK's field
                              ; (SPEC.md 59.8), so it no longer defers to that
                              ; widget and the order here is subject, not need
%include "ui.inc"
%include "apps.inc"
%include "assoc.inc"          ; file type associations (SPEC.md 54): the
                              ; tables and the icon composition. BEFORE
                              ; disk.inc, whose harvest calls into it.
                              ;
                              ; INCLUDED UNCONDITIONALLY AND GATED INSIDE
                              ; (SPEC.md 54.0), which is band.inc's idiom and
                              ; not a style choice: tools/kernsize.py brackets
                              ; every module with markers it inserts AT the
                              ; %include line, in Python, over kernel.asm's
                              ; raw text - so a %ifdef around this line puts
                              ; the OPENING marker inside the gate and the
                              ; closing one outside it, and the per-module
                              ; pass stops assembling with an undefined
                              ; KSMn_boot2. On a kern_small build the file
                              ; contributes zero bytes and reports as a zero
                              ; row, which is what band.inc already does
%include "mod.inc"            ; on-demand kernel modules (SPEC.md 2.8): the
                              ; loader and the far-pointer table. BEFORE
                              ; every module it serves, because a module's
                              ; own section carries the header that names
                              ; MOD_*
%include "lz.inc"             ; decompression (docs/plans/O88-COMPRESSION-PLAN.md):
                                ; BEFORE every loader that calls it, and it
                                ; calls nothing itself - no kernel data, no
                                ; kernel routine, just the caller's two
                                ; segments and a four-byte stack frame
%include "disk.inc"
%include "diskw.inc"          ; the FAT write path (SPEC.md 18.4): after
                                ; disk.inc, whose constants and layout it uses
%include "diskops.inc"        ; DISKOPS.DRV, the disk operations - the
                                ; cloner (SPEC.md 18.99) first: after
                                ; diskw.inc, whose FERR_* neighbours its own
                                ; CERR_*, and before files.inc, which arms it
%include "compress.inc"       ; ...and the one thing that COMPRESSES (SPEC.md
                                ; 20.15), which rides in the CLONER'S image:
                                ; this file emits `.modl` after diskops.inc's
                                ; header and not one byte of `.text`, and
                                ; modl_end below both is where the image ends
%include "loader.inc"
%include "files.inc"
%include "filecp.inc"        ; Cut/Copy/Paste and the recursive paste
                                ; engine (SPEC.md 22.3) - after files.inc,
                                ; whose fm_* it reads
%include "fdlg.inc"             ; the Standard File dialog (SPEC.md 38)
%include "icons.inc"
%include "desk.inc"
%include "dock.inc"
%include "dockmod.inc"            ; empty unless DOCK_OPT (kern_big)
%include "extmod.inc"             ; EXTD.DRV, the extended desktop
%include "winmod.inc"             ; WIN95.DRV, the Windows theme (SPEC.md 76.14)
                                  ; (SPEC.md 39.19.6) - kern_big only
%include "ctrl.inc"
%include "desksc.inc"           ; desktop shortcuts (SPEC.md 26.8): empty
                                ; unless OS88_SHORTCUTS (kern_big). AFTER
                                ; ctrl.inc, because it emits into CTRL.DRV's
                                ; .modc and that image's HEADER must be first
%include "hiber.inc"            ; hibernate and resume (SPEC.md 87): the
                                ; resident thunks, the probe, and HIBER.DRV.
                                ; After mod.inc for MOD_*, a size here

; --- the modules' far-pointer slots (SPEC.md 2.8.1): ONE BLOCK PER MODULE,
; --- EXACTLY AS LONG AS THAT MODULE'S OWN X_NENT. Here, below every module's
; --- %include, because RESB wants a constant it has already seen. MODFP emits
; --- the .bss block AND its mod_fpt word together, and refuses an id out of
; --- MOD_* order or a count below one - mod_fpr's CX of 0 would make
; --- mod_disarm's `loop` run 65,536 times. The blocks are contiguous, so the
; --- sentinel word is the end of the last one and mod_init_x clears the whole
; --- run in one pass.
%assign MODFP_I 0
%macro MODFP 3                  ; label, MOD_ id, entry count
  %if %2 != MODFP_I
    %error "MODFP: %1 is module id %2 but is block MODFP_I - mod_fpt is indexed by id, so the blocks must be declared in MOD_* order"
  %endif
  %if %3 < 1
    %error "MODFP: %1 declares no entries; mod_disarm would loop 65,536 times"
  %endif
section .bss
%1: resb %3*4
section .text
    dw %1
  %assign MODFP_I MODFP_I+1
%endmacro
section .bss
mod_fp:
section .text
mod_fpt:
    MODFP CPFP, MOD_CTRL, CP_NENT
    MODFP FMFP, MOD_FMT, FM_NENT
    MODFP CLFP, MOD_DISKOPS, CLO_NENT
%ifdef KERN_BIG
    MODFP HBFP, MOD_HIBER, HB_NENT
%endif
%ifdef FCP_MOD
    MODFP FCPFP, MOD_FCP, FCP_NENT
    MODFP FDFP, MOD_FDLG, FD_NENT
%endif
%ifdef DOCK_OPT
    MODFP DKFP, MOD_DOCK, DK_NENT
%endif
%ifdef KERN_BIG
    MODFP EXFP, MOD_EXT, EXT_NENT
    MODFP WNFP, MOD_WIN, WN_NENT
%endif
%ifdef OS88_SHORTCUTS
    MODFP SETFP, MOD_SETS, 1    ; the settings core's ONE slot, CPE_SC (2.8.7)
%endif
%if MODFP_I != MOD_MAX
  %error "MODFP: MODFP_I blocks against MOD_MAX rows"
%endif
    dw mod_fp_end
section .bss
mod_fp_end:
section .text
MOD_NSLOT equ (mod_fp_end - mod_fp) / 4
%include "driver.inc"           ; loadable drivers (SPEC.md 51): after
                                ; diskw (it reads and writes the system disk)
                                ; and memory (a driver image is a claim)
%include "snd.inc"              ; the sound layer (SPEC.md 34): PC speaker
%include "fsx.inc"              ; fullscreen exclusive (SPEC.md 53): after
                                ; sched.inc (the freeze bytes), instance.inc
                                ; (the fence), snd.inc (the release walk)
                                ; and viddet.inc (the mode leaves)

; =============================================================================
; SPLGATE's landing pads (SPEC.md 2.9.5.2)
;
; DOWN HERE for the same reason the BOOTMARK block below is: this is `.text`,
; and `.text` ABOVE the %includes pushes splash.inc out of the image's first
; SPL_RESIDENT sectors.
;
; One stub a TARGET, not a site: kmain calls splf_step three times and pays
; three bytes for each, against one stub of eight shared between them. The
; stubs are generated from the target name (SPLSTUB) so the name is written
; once and a mismatch is an undefined symbol.
;
; The gate itself is the guard the inline macro carries at every site, kept
; word for word - SPEC.md 2.9.5.1 is what happens when this test is not on
; this side of the far call - plus the `ret` that makes it shareable. It
; preserves the flags, so a caller with CF in flight is unchanged, and it
; clobbers NO register: the target arrives as the stub's own immediate rather
; than in AX, which is what keeps this a size change and not a register-
; discipline change.
; =============================================================================
; ONE STUB, because one target has more than one site: splf_step, three times
; in kmain. Every other target is called once and writes its own immediate at
; that one site (SPLGATE1 / SPLGATE1 above) - a stub for it would be 11 bytes
; where the site alone is 9, and it would carry a %ifdef the site already has.
; That took ovl_font_init's BAKED_FONT arm and ovl_xm_sniff's KERN_BIG arm out
; of this block, and with them the one snag worth remembering: a stub names a
; target, so a stub for a target this CONFIGURATION does not assemble is an
; undefined symbol - which is the property that makes a missing pairing a build
; error and not a wrong address, and it is not theoretical. Both were found by
; the assembler within a minute of each other, one by `make` and one by
; test-full's kern_small row.
;
; **splf_stepq's stub is gone with its sites** (SPEC.md 9.4.7). Both were in
; mouse_init's identify window, which is in the blob now: a SPLGATE is a NEAR
; call to a `.text` stub and `.boot2` cannot make one, so both became the inline
; SPLCALL form and the 8-byte landing pad had nothing left to land.
spl_gate:
    pushf
    cmp word [spl_fseg], COLD_SEG
    jbe .dead
    call far [spl_fp]
.dead:
    popf
    ret

; =============================================================================
; BOOTMARK=1's blocks (see the MARK macro above)
;
; DOWN HERE for the shims' reason, which the block below spells out: this is
; .text, and .text above the %includes pushes splash.inc out of the image's
; first SPL_RESIDENT sectors. Putting it here cost one build to learn and the
; error names splash rather than this.
; =============================================================================
%ifdef BOOT_MARK
; -----------------------------------------------------------------------------
; MARK / mark_stamp - `make BOOTMARK=1`: stamp one block into the bottom of the
;                     screen for a call in kmain that RETURNED
;
; in:  AL = the block's index, bit 7 set for a tall one
; out: nothing - every register and the flags preserved, for BPMARK's reason:
;      a mark may go between any two calls below without changing what either
;      of them did
;
; It is for ONE question, and it is the question no other instrument here can
; answer: a machine that reaches the loading screen and then STOPS. The splash
; bar cannot say where - it is one number, and kmain only moves it three times
; - BOOTPROF's phase table needs a boot that reaches a desktop to print it on,
; and 86Box has no debugger to attach to the machine that shows the fault.
;
; Nothing in here reads state the boot could already have broken: the geometry
; is the SPLASH's, published before the handoff and known good because the bar
; is on screen, and the fill is spl_bar's own routine. A marker that needed a
; working kernel would not survive the boot it exists to diagnose.
;
; The blocks are COUNTED, not read: N blocks means N calls returned, so the
; freeze is in call N+1 of the table in the commit message.
; -----------------------------------------------------------------------------
MARK_X0     equ 4               ; the first block's byte column - 32 pixels in,
                                ; which clears the dialog on every adapter
MARK_UP     equ 14              ; rows up from the bottom of WHATEVER screen we
                                ; got: 466 on VGA, 334 on Hercules, and the
                                ; loading screen draws on neither
MARK_H      equ 6               ; a block...
MARK_H5     equ 12              ; ...and every fifth, so thirty of them are
                                ; countable without a ruler
MARK_PREVUP equ MARK_UP + 10     ; ...and ten rows above that, the mark the
                                ; PREVIOUS boot reached - the row that answers a
                                ; machine which RESETS instead of stopping
MARK_MAXIDX equ 67              ; the largest index any MARK uses
MARK_SLOT   equ 0x0590          ; 0000:0590 - beside the boot sector's own DPT
                                ; copy at 0x0580 and inside the same page it
                                ; already argues is free (boot/boot.asm), which
                                ; is below the kernel and above the BIOS data
                                ; area, so nothing here or in the BIOS owns it
MARK_MAGIC  equ 0x8088          ; ...and a signature, because a BIOS that
                                ; cleared RAM on the way round must read as NO
                                ; answer rather than as marker zero

; -----------------------------------------------------------------------------
; mark_here - stamp the index that FOLLOWS the call (SPEC.md 15.1.1)
; in:  the byte at the return address
; out: AL = that byte - which is what `mov al, N` left there before, so it is
;      what callers already tolerate. Every other register and the flags are
;      preserved.
;
; `[bp+2]` is the return address and BP addresses SS, which is where a stack
; is: the one place in this kernel where SPEC.md 2.1's SS != DS is being USED
; rather than worked around. The byte is read `cs:` because CS is the segment
; that is EXECUTING and DS is not KERNEL_SEG at every MARK site - the first of
; them runs before the kernel has a data segment at all.
;
; AX is deliberately not saved. `MARK n` has always ended with AL = n (the old
; macro wrote it and mark_stamp handed back what it was given), so nothing can
; be relying on AL across a mark, and two bytes here are two bytes.
; -----------------------------------------------------------------------------
mark_here:
    push bp
    mov bp, sp
    pushf
    push bx
    mov bx, [bp+2]              ; ...the return address
    mov al, [cs:bx]             ; ...the index byte sitting at it
    inc word [bp+2]             ; ...and the `ret` below steps past it
    call mark_stamp
    pop bx
    popf
    pop bp
    ret

mark_stamp:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bx, MARK_H
    test al, 0x80               ; bit 7: one of the tall every-fifth ones
    jz .plain
    mov bx, MARK_H5
.plain:
    and ax, 0x007F              ; ...and the rest of it is the index
    mov si, ax                  ; banked: gfx_rowbase spends AX, CX and DX
    mov es, [vid_seg]
    mov ax, [vid_h]             ; the LIVE height (SPEC.md 39), never a
    sub ax, MARK_UP             ; reference constant - this runs on all three
    call gfx_rowbase            ; adapters and two of them are not 640x480
    add ax, si
    add ax, MARK_X0
    mov di, ax
    mov cx, 1                   ; one byte = eight pixels
    mov al, 0xFF
    cld
    call mark_fill
    call mark_note              ; SI is still the index: nothing above spends it
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; mark_note - bank the index in SI where a RESET cannot reach it
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; mark_fill - one block, ES:DI, CX bytes wide, BX rows tall, AL = the ink
;
; THE MARKER MAY NOT BORROW THE SPLASH'S FILL, and that is what it used to do:
; `SPLCALL splf_fill` into `.boot2`. The blob is given back to the heap after
; spl_finish (SPEC.md 2.9.5), so every mark from 32 on drew through a pointer
; the macro's own guard now refuses - which is a marker that goes quiet exactly
; where kmain stops being about the boot and starts being about the desktop,
; the half a boot diagnostic is least able to spare. FIELD-REPORTED as a run of
; blocks ending at 31 on a machine that was frozen somewhere past it.
;
; It is nine instructions and it owes nothing to anything: gfx_nextrow inlined,
; the same three words spl_fill reads, through DS - which is KERNEL_SEG at
; every mark site by the time this can run.
; -----------------------------------------------------------------------------
mark_fill:
    push bx
    push cx
    push di
.row:
    push cx
    push di
    rep stosb
    pop di
    pop cx
    add di, [vid_rowadd]
    test di, [vid_wrapbit]
    jz .next
    add di, [vid_wrapfix]
.next:
    dec bx
    jnz .row
    pop di
    pop cx
    pop bx
    ret

mark_note:
    push ax
    push es
    xor ax, ax
    mov es, ax
    mov word [es:MARK_SLOT], MARK_MAGIC
    mov [es:MARK_SLOT+2], si
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; mark_prev - stamp what the previous boot reached, one row above this one
;
; A machine that RESETS rather than stops erases the whole band on its way
; round, so the run of blocks says nothing: every cycle starts from none. This
; is the one block that survives it, and in a boot LOOP it is the answer -
; it settles on the same column every cycle, and that column is the call.
; -----------------------------------------------------------------------------
mark_prev:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor ax, ax
    mov es, ax
    cmp word [es:MARK_SLOT], MARK_MAGIC
    jne .out                    ; never written, or a POST that cleared it
    mov ax, [es:MARK_SLOT+2]
    cmp ax, MARK_MAXIDX
    ja .out
    mov si, ax
    mov es, [vid_seg]
    mov ax, [vid_h]
    sub ax, MARK_PREVUP
    call gfx_rowbase
    add ax, si
    add ax, MARK_X0
    mov di, ax
    mov cx, 1
    mov bx, MARK_H5             ; always tall: there is only ever one of these
    mov al, 0xFF
    cld
    call mark_fill
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; mark_hook / mark_faulted - take the CPU's own exception vectors, so a fault
;                            STOPS with its number on screen instead of
;                            cascading to a shutdown and resetting the machine
;
; A 286 answers a fault it cannot deliver with a shutdown cycle, and the board
; wires that to RESET - so the defect erases its own evidence. These stubs stop
; instead, and the block they leave in the 60..67 band names the vector.
;
; NOT vector 2: that is NMI and a parity report is not ours to swallow. Not 8
; upwards either - on a PC those are the remapped IRQs, so the machine's timer
; and its sound card own them, not the CPU.
; -----------------------------------------------------------------------------
%macro MARKFSTUB 1
    mov al, (%1) | 0x80
    jmp short mark_faulted
%endmacro

mark_fstubs:                    ; FOUR BYTES each, indexed by vector number -
    MARKFSTUB 60                ; which is the IVT's stride too, so the same
    MARKFSTUB 61                ; multiply lands in both tables
    MARKFSTUB 62
    MARKFSTUB 63
    MARKFSTUB 64
    MARKFSTUB 65
    MARKFSTUB 66
    MARKFSTUB 67

; -----------------------------------------------------------------------------
; mark_beacon - the crudest "we got here" this machine can be asked for
;
; It depends on NOTHING: not DS, not [vid_seg], not [vid_h], not gfx_rowbase,
; not spl_fill, not a mode set. Four bytes at offset 0 of all three framebuffer
; segments, so whichever one this adapter answers at gets a short white run in
; the TOP-LEFT CORNER and the other two write to RAM nobody reads.
;
; It exists because the band at the bottom said nothing, and a marker that says
; nothing has two readings - the code never ran, or the marker never drew. This
; separates them: corner AND band = both work; corner ALONE = kmain is running
; and the drawing path is what is broken; NEITHER = kmain was never entered and
; the fault is above it, in the loader's tail or the far jump itself.
;
; Needs a stack for the call, and at the top of kmain that is still the BOOT
; SECTOR's - which the loader has been running on all along, so it is known
; good at exactly the moment this is asked.
; -----------------------------------------------------------------------------
mark_beacon:
    push ax
    push cx
    push di
    push es
    mov ax, 0xA000              ; VGA planar FIRST, and the order is the point:
    call .one                   ; a VGA in mode 12h decodes A000 alone, so the
    mov ax, 0xB000              ; other two are writes onto a bus with nothing
    call .one                   ; on it. Harmless on every machine seen here -
    mov ax, 0xB800              ; but if one of them ever is not, the corner is
    call .one                   ; already drawn by the time we find out
    pop es
    pop di
    pop cx
    pop ax
    ret
.one:
    mov es, ax
    xor di, di
    mov cx, 4
    mov al, 0xFF
    cld
    rep stosb
    ret

mark_faulted:
    cli
    push cs
    pop ds
    call mark_stamp
.h:
    hlt                         ; and STAY here: the screen is the report
    jmp short .h

mark_hook:
    pushf
    cli
    push ax
    push bx
    push cx
    push si
    push di
    push es
    xor ax, ax
    mov es, ax
    mov si, mark_fvec
    mov cx, MARK_FNVEC
.v:
    lodsb                       ; AL = the vector to take
    xor ah, ah
    mov di, ax
    shl di, 1
    shl di, 1                   ; DI = vector * 4: the IVT slot...
    mov bx, di
    add bx, mark_fstubs         ; ...and its stub, at the same stride
    mov [es:di], bx
    mov [es:di+2], cs
    loop .v
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

mark_fvec   db 0, 1, 3, 4, 5, 6, 7
MARK_FNVEC  equ $ - mark_fvec
%endif

; =============================================================================
; The cold segment's shims (SPEC.md 2.6)
;
; This block is BELOW every %include on purpose, and it is the one thing about
; it that is not obvious. It used to sit up with the API stubs, which is above
; splash.inc - and splash.inc has to end inside the image's first SPL_RESIDENT
; sectors (SPEC.md 15), because the boot sector ticks the bar while the rest of
; the kernel is still arriving. The shims were 140 bytes then and fitted; the
; file modules (below) took them past 500 and pushed the splash out of its
; sectors, which fails the build with an error naming splash and nothing else.
; Anywhere in .text is correct for a shim. Here is the only place that stays
; correct as the list grows.
;
; Two directions, four and six bytes each:
;
;   cw_*  what COLD code calls OUT to. Cold code runs with CS = COLD_SEG, so a
;         near call to resident code would be a displacement computed between
;         two address spaces - it assembles clean and runs wrong, which is what
;         tools/os88ovlchk.py exists to refuse.
;
;   the named thunks, what the kernel calls IN. A cold routine keeps its
;         ordinary near `ret` and the thunk owns the far call, so the PUBLIC
;         name is the thunk and the body is the same name with _x. Every
;         caller outside is unchanged, including the OSAPI_SLOT/OSAPI_NSTUB
;         cells - which matters, because a macro ARGUMENT is a call site that
;         os88ovlchk.py cannot see (the `call` is in the macro body, as
;         `call %1`), so nothing would have reported those.
;
; A SHIM IS NOT OWED (SPEC.md 2.6.1). When the body has no near caller of its
; own it can simply end in `retf` and the far call can land on it - 84 of them
; went that way, 340 bytes, and `.text`'s rung slack went 88 bytes to 162.
; FOUR MORE WENT IN SIZE PASS 2 and the census above had drifted: font_width,
; inst_park_req and sched_mode_get each had exactly ONE reference in the whole
; tree - their own shim - and wm_su_stale is the "one that tail-jumps" this
; paragraph named as impossible.  It is merely dearer: wm_su_drop has three
; other NEAR callers and must keep its near `ret`, so the jump became
; `call wm_su_drop / retf` and that row is worth 2 bytes rather than 4.
; What is left here is what still could not: a body with a near caller, one
; that near-calls a LOCAL helper (that helper's `ret` is inside the same
; extent and has to stay near), and one whose thunk is named by a dispatch
; table rather than by a call. os88ovlchk.py gates the
; distinction in both directions, because `ret` pops two bytes and `retf` pops
; four and the wrong one does not fault - shown to fire here by putting
; sched_mode_get's near `ret` back, which named all four of its far call
; sites and refused.
;
; **AND WHERE A CELL ALREADY PUBLISHES THE ROUTINE, THE CELL IS THE DOOR**
; (size pass 9; MODULE-SELFCONTAIN-PLAN 5's apic_ rule taken the whole way).
; Thirteen shims below are `cw_X equ apic_X`: the name stays, so not one far
; call site changed, and it lands on the routine's own OSAPI cell, which for a
; caller whose DS is already KERNEL_SEG IS the shim's semantics - the cell's
; push/pop of DS and its `retf` touch no flag and no other register. Four
; bytes each. What it costs is the cell's DS switch, ~50 cycles a call on an
; 8088 for a SLOT cell and ~130 for a rare (RSLOT) one, and two or four bytes
; more of stack - so it is taken only where every caller is a dialog, a
; window being built, the Control Panel or a hibernate step, and NOT for the
; drawing shims a repaint calls by the dozen (gfx_fill/lock/unlock/frame,
; font_run), nor for anything a built-in WORKER calls from its 128-byte slice
; (task_sleep, task_yield, wm_clip_set, wm_content - the Bounce task's).
; =============================================================================
cw_app_launch:          call app_launch
                    retf
cw_app_close_win:       call app_close_win  ; the Standard File chooser's
                    retf                ; close (SPEC.md 38.7)
cw_clk_snapshot:        call clk_snapshot
                    retf

; --- ...and the six the Date/Time page's editor needs (SPEC.md 37.93) -------
; clk_fld_str and clk_fld_adj moved into CTRL.DRV, and these are everything
; they reach on the way. Not one of them could go with them: clk_fmt draws
; the menu bar from five of these and clk_inc_sec's day carry reaches the
; sixth, so all six keep a RESIDENT near caller and none can become a retf
; body (SPEC.md 2.6.1). 329 bytes of .text left and 24 came back, so the move
; is 305; duplicating the six into the image instead would have saved those
; 24 and cost a second copy of clk_mlen, which is the leap-year rule.
cw_clk_mlen:            call clk_mlen
                    retf
cw_clk_put_mon:         call clk_put_mon
                    retf
cw_clk_put2:            call clk_put2
                    retf
cw_clk_put4:            call clk_put4
                    retf
cw_clk_h12h:            call clk_h12h
                    retf
cw_clk_ampm:            call clk_ampm
                    retf
cw_cur_unlazy:          call cur_unlazy     ; gfx_blit1_x's (SPEC.md 5.4.2), and
                    retf                    ; on BOTH builds since 5.4.2.5's
                                            ; decision was reversed
%ifdef KERN_BIG
cw_cur_lazyrect:        call cur_lazyrect   ; ...and SPEC.md 7.1.4.5's rect
                    retf                    ; form, which is the one a BAND asks
; ...and the REFCOUNTED pair (SPEC.md 7.1), which the screen saver's cold half
; brackets a session with: the arrow has to be off the glass for as long as
; something is drawing over it, and every gfx_lock/gfx_unlock inside a frame
; then nests underneath and draws nothing (SPEC.md 79.6). kern_big's, like the
; saver itself.
cw_cursor_hide:         call cursor_hide
                    retf
cw_cursor_show:         call cursor_show
                    retf
%endif                                      ; so kern_small pays for none of
cw_evq_mup:             call evq_mup        ; them out of its 75-byte rung.
                    retf                    ; THE WHOLE LOOP and not its two
                                            ; halves: cw_evq_pop and
                                            ; cw_wm_wake_eaten were reached
                                            ; only from fm_drag's two `.cold`
                                            ; track loops, which now call this
                                            ; instead - so two cells died here
                                            ; and one was born (SPEC.md 74.1.1)
cw_font_run:            call font_run
                    retf
%ifdef KERN_BIG                 ; ...and gfx_disp_enter is dual display's
cw_gfx_disp_enter:      call gfx_disp_enter
                    retf
%endif
cw_gfx_fill:            call gfx_fill
                    retf
cw_gfx_fill_gray equ apic_gfx_fill_gray ; THE CELL IS THE DOOR (size pass 9)
cw_gfx_fill_pat:        call gfx_fill_pat
                    retf
%ifdef OS88_THEME              ; (kern_small: vga12.inc aliases it)
cw_thm_desk:            call thm_desk
                    retf
cw_thm_set:             call thm_set
                    retf
%ifdef KERN_BIG
cw_thmv_open:           call COLD_SEG:thmv_open_x  ; thmv_* is .cold (it
                    retf                           ; calls drv_load_at/
                                                    ; drv_release, SPEC.md
                                                    ; 76.14) - THMPAL.DRV's
                                                    ; own far door is
                                                    ; OSAPI_THM_SET/
                                                    ; _SET_PRESET; this pair
                                                    ; is ctrl.inc's: the
                                                    ; button that opens the
                                                    ; window and the
                                                    ; teardown that frees it
cw_thmv_reap:           call COLD_SEG:thmv_reap_f
                    retf
%endif
%endif
%ifdef DOCK_OPT
cw_dock_band:           call dock_band
                       retf
cw_kretf:           retf                    ; DOCK.DRV's dkk_* stubs return
                                            ; through this. (cw_dock_apply
                                            ; fell into it until size pass 9:
                                            ; the Dock page and the settings
                                            ; reader far-call dkf_apply now,
                                            ; whose own return is far)
cw_gfx_clip_query:      call gfx_clip_query ; CLIPQF: shared region query
                    retf                    ; from .cold (SPEC.md 30.6.1)
%endif
; THE SCREEN SAVER'S WAY BACK (SPEC.md 79.6), and it is THREE calls behind one
; shim rather than three shims, because the image rung it comes out of has
; single-figure bytes left in it (docs/KERNEL-MEMORY.md). wm_paint_all deliberately forces NEITHER
; the bar nor the strip - the dither it lays starts at MBAR_H and stops above
; the dock, so nothing in it has ever been able to touch either (SPEC.md 12.9,
; 30.3.2) - and a saver session has drawn over both. fsx_restore is the other
; caller that has to say so, and for the identical reason: an app that owned
; the whole screen really did overdraw them.
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_blk_relit:           call blk_relit
                    retf
%endif
cw_gfx_frame:           call gfx_frame
                    retf
cw_gfx_hline equ apic_gfx_hline ; THE CELL IS THE DOOR (size pass 9)
cw_gfx_lock:            call gfx_lock
                    retf
cw_gfx_pen_cf equ apic_gfx_pen_cf ; THE CELL IS THE DOOR (size pass 9)
cw_gfx_pen_live:        call gfx_pen_live
                    retf
cw_gfx_rowbase:         call gfx_rowbase
                    retf
cw_gfx_unlock:          call gfx_unlock
                    retf
cw_gfx_xor_fill equ apic_gfx_xor_fill ; THE CELL IS THE DOOR (size pass 9)
cw_icon_draw:           call icon_draw
                    retf
cw_icon_draw_ix:        call icon_draw_ix   ; the INDEXED kind (SPEC.md 25.7),
                    retf                    ; desk.inc's volume icons and no
                                            ; other caller in the tree
cw_icon_draw16:         call icon_draw16
                    retf
cw_inst_find_kind:      call inst_find_kind
                    retf
cw_inst_fhome_idx:      call inst_fhome_idx
                    retf
cw_inst_win_owner:      call inst_win_owner
                    retf
; ...and apps.inc's three (SPEC.md 14): the three built-in kinds are cold now,
; so every reach back into the instance store crosses. inst_task_die NEVER
; RETURNS - both Timer and Bounce end their loop on it - and it is still a
; `call` rather than a `jmp`, for drv_task's reason: a jmp to a shim pops the
; jumping routine's near frame as CS:IP the day the target does come back.
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_inst_of_win:         call inst_of_win
                    retf
%endif
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_inst_ptr:            call inst_ptr
                    retf
%endif
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_inst_task_die equ inst_task_die  ; NEVER RETURNS (it ends in task_exit's
                                ; jmp sch_switch), so the shim's `retf` was
                                ; unreachable and the far call may land on the
                                ; body: the frame it leaves dies with the
                                ; task's stack either way (size pass 9)
%endif
cw_mem_disp equ spw_near        ; `call bp` / `retf` - viddet.inc's splash door
                                ; is the same three bytes, and it stays where
                                ; SPL_RESIDENT needs it (size pass 10)
cw_menu_activate:       call menu_activate
                    retf
; ...and menu_kbnav's two, which went cold with it (SPEC.md 12.10)
cw_menu_item_dis:       call menu_item_dis
                    retf
cw_mou_clamp:           call mou_clamp
                    retf
; ...and the BOOT OVERLAY's (docs/plans/LAST-DROP-BYTES.md rows 15 and 17):
; dock_init and menu_init moved into `.boot2`; menu_init has exactly one
; outbound call left, and dock_init now has NONE - what it asked cw_dock_force
; for was "the whole strip is owed", which is two words it writes itself, so
; the shim went with the call.
cw_menu_relayout:       call menu_relayout
                    retf
cw_menu_draw_bar:       call menu_draw_bar
                    retf
cw_menu_popup:          call menu_popup
                    retf
cw_snd_beep:            call snd_beep
                    retf
cw_snd_disp_set:        call snd_disp_set
                    retf
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_task_exit equ task_exit      ; NEVER RETURNS - cw_inst_task_die's reason
%endif
cw_task_spawn:           call task_spawn
                     retf
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_task_sleep:           call task_sleep     ; Bounce's 2 ticks (SPEC.md 14),
                     retf                    ; from .cold
%endif
cw_task_yield:          call task_yield
                    retf
cw_toast_show equ apic_toast_show ; THE CELL IS THE DOOR (size pass 9)
%ifdef KERN_BIG
cw_ui_svc_open:         call ui_svc_open    ; the service zone's double-click
                    retf                    ; (SPEC.md 26.7): desk.inc is cold
                                            ; and ui_sys_open is not
cw_drv_pkg_call:        call drv_pkg_call_x ; ...and its PAINT, which is the
                    retf                    ; same door a package reaches a
                                            ; driver by (SPEC.md 20.11)
%endif
cw_vga_xor_rect_vram:   call vga_xor_rect_vram
                    retf
cw_vid_avail_test:      call vid_avail_test
                    retf
%ifdef KERN_BIG                 ; vid_disp_init is dual display's and so is
                                ; its shim - cw_vid_dual_ok below already had
                                ; the guard and this one was missed, so
                                ; kern_small stopped assembling entirely
cw_vid_disp_init:       call vid_disp_init
                    retf
%endif
%ifdef KERN_BIG
; vidsel.inc's whole dual-display half is inside one %ifdef KERN_BIG, so these
; bodies do not exist in the small build.
cw_vid_dual_ok:         call vid_dual_ok
                    retf
; ...and these two are the BOOT OVERLAY's, not cold code's: vid_ctx_init went
; into `.boot2` (docs/plans/LAST-DROP-BYTES.md row 13) and calls both. They are `cw_`
; rather than `ovw_` for the reason the clock's pair is - `ovw_` names the
; overlay, and what these actually name is "resident, reached from another
; address space", which is the same shim whichever side asks.
cw_vid_ctx_ptr:         call vid_ctx_ptr
                    retf
cw_vid_ctx_capture:     call vid_ctx_capture
                    retf
; ...and EXTD.DRV's one hop that is on OSAPI_WM_DISPLAY's path (wm_disp_now's
; one-display arm, SPEC.md 39.19.6.2). Four bytes here where the image carried
; a twelve-byte stub of its own, and a near call + RETF where the stub built
; the frame by hand and returned through cw_kretf
cw_vid_disp_of:         call vid_disp_of
                    retf
%endif
cw_wm_clip_clear equ apic_wm_clip_clear ; THE CELL IS THE DOOR (size pass 9)
cw_wm_clip_rows:        call wm_clip_rows
                    retf
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_wm_clip_set:         call wm_clip_set
                    retf
%endif
cw_wm_clip_test equ apic_wm_clip_test ; THE CELL IS THE DOOR (size pass 9)
cw_wm_content:          call wm_content ; NOT the cell: app_ball_wipe and
                    retf                ; app_ball_fill reach it on the Bounce
                                        ; WORKER's 128-byte slice
cw_wm_minsize equ apic_wm_minsize ; THE CELL IS THE DOOR (size pass 9)
%ifdef KERN_BIG                 ; its callers are kern_big only
cw_wm_snap equ apic_wm_snap ; THE CELL IS THE DOOR (size pass 9)
                                ; app_tmr_kinit asks for the snap
                                ; (SPEC.md 11.94), and Timer is
%endif                                  ; kern_big's alone (SPEC.md 14.6)
%ifdef KERN_BIG
cw_wm_onmouseup equ apic_wm_onmouseup ; THE CELL IS THE DOOR (size pass 9)
cw_wm_ondrag:           call wm_ondrag
                    retf
cw_wm_timer:            call wm_timer       ; SPEC.md 13.10.5.4.2's PAUSE
                    retf                    ; commit and the Timer's clock
                                            ; (14.7): the kernel ARMS the same
                                            ; one-shot a package does. Only the
                                            ; arm needs a wrapper - it sets
                                            ; [wm_tarm] and [ui_post] - where
                                            ; the install (wm_ontimer) and the
                                            ; cancel are each one store, made
                                            ; in place. KERN_BIG and not
                                            ; OS88UI_SBDRAG: the Timer needs it
                                            ; under SBDRAGOFF=1 as well
; ...and HIBER.DRV's seven needs go through cw_mem_disp (`call bp / retf`)
%endif
cw_wm_dmg_zadd:          push si             ; desk_dmg_zones grows the
                         mov si, wm_dmg_zb   ; ZONES' box, not the damage
                         call wm_runion      ; (SPEC.md 11.91)
                         pop si
                         retf
cw_wm_dmg_hit:           call wm_dmg_hit
                     retf
%ifdef KERN_SMALL
cw_wm_zone_r:            call wm_zone_r      ; is any of a zone revealed?
                     retf                    ; (SPEC.md 11.91.6)
%endif
cw_wm_dmg_wins:         call wm_dmg_wins
                    retf
cw_wm_grow_paint equ apic_wm_grow_paint ; THE CELL IS THE DOOR (size pass 9)
cw_wm_hit:              call wm_hit
                    retf
cw_wm_idx2ptr:          call wm_idx2ptr
                    retf
cw_wm_paint_all:        call wm_paint_all
                    retf
cw_wm_paint_dmg:         call wm_paint_dmg
                     retf
cw_wm_pkgcall:          call wm_pkgcall
                    retf
cw_wm_show equ apic_wm_show ; THE CELL IS THE DOOR (size pass 9)
%ifdef WM_ANIM
cw_inst_unmin:          call inst_unmin
                    retf
%endif
cw_xm_release_rec:      call xm_release_rec
                    retf
cw_wm_title_set equ apic_wm_title_set ; THE CELL IS THE DOOR (size pass 9)
cw_wm_win_rect:         call wm_win_rect
                    retf

; --- COLD-SIDE TRAMPOLINES (SPEC.md 2.6.2) -----------------------------------
; A call out of `.cold` into `.text` is five bytes (`call KERNEL_SEG:x`) where
; a near call is three, and the targets below have enough `.cold` call sites
; that one six-byte `call KERNEL_SEG:x / ret` here, near-called from each of
; them, is smaller than the far calls it replaces: 2N - 6 bytes for N sites.
; Nothing a caller can see changes - a near call and its `ret` touch no
; register and no flag, and BP (cw_mem_disp's input) passes straight through -
; except two bytes of stack, for the near return address, during the call.
;
; `.cold` CALLERS ONLY. An on-demand module (`.modc`/`.modd`/...) and the boot
; overlay run at another CS and keep the far call; fdlg.inc, which is `.cold`
; on kern_big and FDLG.DRV on kern_small, reaches these through its FDK macro.
; The sites-per-target figures are kern_big's / kern_small's, measured when
; this block was written; a target whose count falls to 3 or below is paying
; for its trampoline, and goes back to the plain far call.
section .cold
osapi_vol_kind_x:               ; (see osapi_vol_kind's header above)
    push bx
    mov bl, al
    call dsk_vol_fixed_x        ; ...which answers BOTH, so this cell is the
    pop bx                      ; index-to-BL and nothing else (a pop leaves
    retf                        ; CF alone)

ct_cw_gfx_fill:     call KERNEL_SEG:cw_gfx_fill
                    ret                     ; 23 / 18 sites
ct_cw_gfx_unlock:   call KERNEL_SEG:cw_gfx_unlock
                    ret                     ; 15 / 10 sites
ct_cw_gfx_lock:     call KERNEL_SEG:cw_gfx_lock
                    ret                     ; 14 / 10 sites
ct_cw_font_run:     call KERNEL_SEG:cw_font_run
                    ret                     ; 10 / 10 sites
ct_cw_wm_content:   call KERNEL_SEG:cw_wm_content
                    ret                     ; 9 / 3 sites
ct_cw_mem_disp:     call KERNEL_SEG:cw_mem_disp
                    ret                     ; 8 / 6 sites
ct_toast_say:       call KERNEL_SEG:toast_say
                    ret                     ; 8 / 8 sites
ct_cw_gfx_frame:    call KERNEL_SEG:cw_gfx_frame
                    ret                     ; 8 / 7 sites
ct_font_width:      call KERNEL_SEG:font_width
                    ret                     ; 7 / 5 sites
ct_fpg_begin:       call KERNEL_SEG:fpg_begin
                    ret                     ; 6 / 5 sites
ct_cw_inst_win_owner: call KERNEL_SEG:cw_inst_win_owner
                    ret                     ; 6 / 5 sites
ct_cw_gfx_xor_fill: call KERNEL_SEG:cw_gfx_xor_fill
                    ret                     ; 5 / 3 sites
ct_cw_wm_clip_test: call KERNEL_SEG:cw_wm_clip_test
                    ret                     ; 5 / 3 sites
ct_cw_snd_beep:     call KERNEL_SEG:cw_snd_beep
                    ret                     ; 5 / 2 sites
ct_fpg_end:         call KERNEL_SEG:fpg_end
                    ret                     ; 4 / 4 sites
ct_cw_task_yield:   call KERNEL_SEG:cw_task_yield
                    ret                     ; 4 / 3 sites
ct_cw_gfx_pen_cf:   call KERNEL_SEG:cw_gfx_pen_cf
                    ret                     ; 4 / 4 sites
ct_cw_gfx_hline:    call KERNEL_SEG:cw_gfx_hline
                    ret                     ; 4 / 4 sites
section .text

; ...and the other direction: what the kernel calls IN the Control Panel.
; NOTHING, any more, for its window: cp_tpl and cp_kinit name the cold
; cpf_ entries themselves, because a kernel window's callbacks are dispatched
; into `.cold` by wm_pkgcall (SPEC.md 2.6.3) and need no resident thunk.
; ...and NOT cp_flush either. It has no thunk on purpose: with no way into it
; from .text, "the panel's teardown is the only thing that writes SYSTEM.CFG"
; (SPEC.md 31.8) is something the build enforces rather than something every
; new page has to be told

%ifdef KERN_BIG
; --- ...and wm.inc's two chrome-box helpers (SPEC.md 13.8.1), which are cold
; for the ordinary reason - .text's rung had 204 bytes left and the down state
; wanted 232 - and whose thunks are not quite the plain shape above.
;
; wm_chrome_relit's GUARD IS HERE rather than in the body, and that is the
; point of writing it out: wm_draw_title calls it on every title draw in the
; system and the answer is almost always "nothing is held down". Resident, that
; costs a compare and a near ret; in the body it would cost a far call, a
; compare, and a far ret, on a path that runs whenever any window is raised.
wm_chrome_relit:      cmp byte [ui_armlit], 0
                  jne .go
                  ret
.go:              call COLD_SEG:wmz_wm_chrome_relit
                  ret
%endif                          ; KERN_BIG - kern_small draws no down state

; --- ...and vga12.inc's band blit (SPEC.md 5.4.2). The PUBLIC name is the
; thunk and the body is the same name with _x, so the OSAPI_SLOT cell below
; names gfx_blit1 and knows nothing about which segment it lives in.
gfx_blit1:            call COLD_SEG:gbz_gfx_blit1
                  ret                   ; a near ret over a far one, neither
                                        ; of which touches the flags - and CF
                                        ; is this routine's whole answer.
                                        ; ON BOTH BUILDS since SPEC.md
                                        ; 5.4.2.5's refusal was reversed:
                                        ; kern_small carried the SLOT and not
                                        ; the body, so Paint expanded a row at
                                        ; a time there at 24x the cost
                                        ; (SPEC.md 42.23.4)

; --- ...and SPEC.md 13.8.2/13.9's three, which are kern_big's ALONE. The CELL
; is in both tables - a slot number that exists in one build and not another
; is an ABI that depends on a knob (SPEC.md 20.8 rule 4) and would shift every
; number above it - and the small build answers CF = 1, gfx_blit1's precedent
; exactly. A package tests CF and does without: no tracking, no timer, and its
; down state is the static one 13.8.2 describes. The big build STATES CF = 0
; rather than letting it fall out of the body, or the test is not a test on the
; kernel that has the feature.
;
; They are named _c rather than being the routines themselves so that the
; kern_big side is a plain jump to code that knows nothing about the split,
; and so that the refusal is one place to read rather than three %ifdefs
; inside three routines.
%ifdef KERN_BIG
wm_ondrag_c:          call wm_ondrag
                  clc             ; CF = 0 is the ANSWER, not the caller's own
                  ret             ; carry coming back: the bodies write no flag
wm_timer_c:           call wm_timer
                  clc             ; the arm path's is worse - it is the carry
                  ret             ; out of [ticks] + delay
wm_ontimer_c:         call wm_ontimer
                  clc
                  ret
%else
wm_ondrag_c:                    ; THREE LABELS, ONE BODY - the cells are what
wm_timer_c:                     ; cost, and a refusal has nothing to say about
wm_ontimer_c:         stc       ; which of the three it is refusing
                  ret
%endif

; --- ...and the file modules': loader.inc, diskw.inc, files.inc (SPEC.md 2.6).
; filecp.inc needs none - every caller of an fcp_ routine is files.inc, which
; is cold too, so those calls stayed near. diskw.inc's eight file doors and
; loader.inc's package start need none EITHER, since SPEC.md 20.3.2: each was
; reached by nothing but its N cell, and the cell names the dwf_/ldf_ far
; entry itself now. What is left here is what a NEAR caller in .text names
; and a window's dispatch does not reach - the Disk window's KD_INIT. Its
; template, its menu handler, its two edges and its image dialog's completion
; name the cold bodies, and so does the file dialog's template: a kernel
; window's callbacks are dispatched into `.cold` by wm_pkgcall (SPEC.md 2.6.3).
fm_kinit:             call COLD_SEG:fm_kinit_x
                    ret
; SPEC.md 38.1.1 - THE GUARD IS ON THE CALLER'S SIDE, and the caller is
; ui.inc's ladder, which far-calls fdlg_reap_x itself. There used to be a
; resident `fdlg_reap` here that asked the same question a second time between
; the ladder's compare and the body's.


; --- apps.inc's (SPEC.md 14): the three built-in kinds. They joined the cold
; set on CADENCE, which is SPEC.md 2.6's test and not size: a launch, a click
; and a repaint are human-scale, and the two background tasks wake every 9 and
; every 2 ticks - so a far call is microseconds against a frame whose two
; gfx_fills alone are ~1.5ms on the target machine (PERFORMANCE.md Part 2).
; The DATA stayed resident: rule 1, and every reader of the templates, titles
; and strings addresses DS.
;
; FOUR ENTRIES, and they are the two KD_INIT words and the two KD_TASK words
; in inst_kinds, which inst_launch and task_spawn reach NEAR in KERNEL_SEG.
; The three templates' paint and click words and the two edges app_tmr_kinit
; installs name the cold bodies: they land on wm_pkgcall, which dispatches a
; kernel window into `.cold` (SPEC.md 2.6.3). So did app_about_center, whose
; one caller - the notice's paint - is cold now too.
;
; The two TASKS never return: task_spawn seeds the frame with the entry as a
; near address in KERNEL_SEG, so the thunk has to be resident, and the `ret`
; below it is unreachable. It is written anyway rather than as a far `jmp`,
; because a thunk that is not shaped like every other thunk here is one a
; future reader has to stop and check.
%ifdef KERN_BIG                 ; SPEC.md 14.6: Timer and Bounce are kern_big's
app_tmr_kinit:        call COLD_SEG:app_tmr_kinit_x
                    ret
%endif                          ; KERN_BIG - no Timer, no Bounce (SPEC.md 14.6)
%ifdef KERN_BIG                 ; SPEC.md 14.6: Timer and Bounce are kern_big's
app_bounce_kinit:     call COLD_SEG:app_bounce_kinit_x
                    ret
%endif                          ; KERN_BIG - no Timer, no Bounce (SPEC.md 14.6)
%ifdef KERN_BIG                 ; SPEC.md 14.6: Timer and Bounce are kern_big's
app_bounce_task:      call COLD_SEG:app_bounce_task_x
                    ret                     ; never reached (inst_task_die)
%endif                          ; KERN_BIG - no Timer, no Bounce (SPEC.md 14.6)

; --- ...and assoc.inc's (SPEC.md 54). It joined the cold set because nothing
; in it runs faster than a double-click, and because its heaviest callees were
; already there: the calls to ld_run_name, files_refresh and
; dskw_stat were a DOUBLE crossing (out through a cw_ shim, in through a
; resident thunk) and are near calls now - SPEC.md 2.6's "growing the set makes
; the ones already in it cheaper". Its two slots name the cold bodies from
; their cells (SPEC.md 20.3.2), so nothing of it is resident on kern_big...
%ifndef OS88_ASSOC
; ...and kern_small has no associations (SPEC.md 54.0), so both slots keep
; their CELLS - the ABI is parity, one .o88 serves both kernels - and share
; ONE refusing body, which is SPEC.md 13.9's idiom for exactly this. It is
; COLD because the cells reach a cold body: the same two bytes, one segment
; along.
;
; NO PACKAGE NEEDS CHANGING, and that is a property of the two contracts
; rather than luck. OSAPI_ARG_FILE's CF=1 is documented as "launched empty,
; the ordinary case" - the answer it already gives on every launch that was
; not a document open - and OSAPI_ASSOC_SET's is "the tables are full and
; nothing was stored". Both slots already require the caller to test CF.
section .cold
osapi_arg_file_x:
osapi_assoc_set_x:    stc
                    retf
section .text
%endif

; --- ...and disk.inc's (SPEC.md 18-19). The FAT read path, mount, and the
; volume table: everything here is bounded by a floppy, where SPEC.md 2.6's
; far call is ~6us against a sector's ~24ms. dsk_xfer's per-sector spl_step
; goes out through a cw_ shim and stays flag-transparent, which it must be
; (SPEC.md 15.3) - call and retf touch no flags.
dsk_chdir_q:      call COLD_SEG:dkf_dsk_chdir_q
              ret

                                            ; The batch bracket, the five
                                            ; volume slots, fs_ent and
                                            ; desk_svc have NO thunk: their
                                            ; cells name the cold body
                                            ; (SPEC.md 20.3.2). Nor do
                                            ; dsk_flop_add and dsk_vol_slot
                                            ; any more: desk_init was their
                                            ; one caller and it is `.ovl`,
                                            ; so it far-calls the window and
                                            ; cold bodies itself (kernel size
                                            ; pass 5, 12 resident bytes)

; --- ...and driver.inc's (SPEC.md 51). Boot-time loading, the Control Panel
; pages and the class dispatch. One entry is reached from an ISR:
; drv_svc_call is called by snd_tick inside IRQ0, so it pays a far call 18.2
; times a second (54.6 under 53.2.1 FSXF_FASTTICK) - ~6us on a tick that has
; a whole 55ms, and it does nothing at all when no driver publishes a stream.
; drv_boot's thunk is GONE (SPEC.md 2.5.2): the body is in the overlay and
; kmain's one call site reaches it through SPLGATE1, which is the crossing the
; thunk used to be.
%ifdef OS88_DRIVERS                     ; (kern_small: no caller - snd.inc's
drv_svc_call:  call COLD_SEG:drv_svc_call_x ; are all OS88_DRIVERS's now)
           ret                          ; drv_task, drv_cfg and drv_dlg are
%endif                                  ; their cells' own (SPEC.md 20.3.2)
; ...and the other end of that round trip (SPEC.md 51.10) needs NO thunk:
; fdlg_commit dispatches a kernel window's completion proc through wm_pkgcall,
; which takes a W_SEG 0 window into `.cold` (SPEC.md 2.6.3), so the offset
; handed to fdlg_open is the cold body - drv_dlg_done_x here, and fm_img_done_x
; for the Disk window's image commands (SPEC.md 18.99.8).

; --- ...and memory.inc's (SPEC.md 50). The claim heap: every claim and free
; in the machine, none of them on a drawing path. The busiest is the menu
; save-under (12.4), one claim and one free per pull-down, where the far call
; rides a gesture that already costs a heap scan and a screen save.
; Its ONE outbound call, drv_owns_seg, is near now - driver.inc is cold too.
mem_claim:            call COLD_SEG:mmf_mem_claim
                  ret
mem_free:             call COLD_SEG:mmf_mem_free
                  ret
mem_free_rec:         call COLD_SEG:mem_free_rec_x
                  ret
; ...and that is ALL of memory.inc's resident thunks. The nineteen published
; memory and snapshot slots name their cold bodies from the cell (SPEC.md
; 20.3.2) - the last of them, osapi_sys_snapshot, had been a bespoke JSLOT
; stub, then "the ordinary thunk", and is now neither. What remains below is
; kern_small's two refusing bodies, cold because the cells reach a cold body.
%ifndef OS88_COMPACT            ; kern_small MOVES NOTHING (SPEC.md 66.0), so
section .cold                   ; the door is a stub and none of its
osapi_mem_compact_x:            ; machinery is assembled: the what-if IS the
                  or ah, ah     ; plain level door's answer, and a post is
                  jnz .no       ; refused - which is the documented
                  jmp mmf_osapi_mem_avail   ; outcome a caller already carries
.no:              stc           ; on from
                  retf
section .text
%endif
%ifndef KERN_BIG                ; **kern_small HAS NO DRIVER LAYER AND NO
section .cold                   ; HIBERNATE** (SPEC.md 51.11.3), so 0x0550's
drv_suspend_x:    xor cx, cx    ; three verbs answer here. The suspend and the
                  cmp al, 2     ; resume ANSWER SUCCESS with no records -
                  cmc           ; nothing had to get out of the way - because
                  retf          ; a caller does not test CF for a condition
section .text                   ; that is not an error
                                ; (docs/plans/KERN-SMALL-CUT-PLAN.md 10); the
                                ; handoff REFUSES, the published meaning of a
                                ; slot that cannot do what was asked (20.8) -
                                ; dos_mem_whole greys the arm on the part
                                ; table rather than on this, so a kern_small
                                ; machine carrying a parted DOS.O88 would have
                                ; offered the arm and is refused here. `cmp`
                                ; sets CF for AL below 2 and `cmc` turns that
                                ; into the two answers in one byte
%endif

; --- ...and desk.inc's (SPEC.md 26.1). The desktop dither and the drive
; zones. Drawn on a mount, on a volume going away and on a click on bare
; desktop - never in a loop, and the module carries no data at all, so it
; moved whole with no section toggle in it.
desk_rowcalc:     call COLD_SEG:desk_rowcalc_x
              ret

; =============================================================================
; THE SHARED-EPILOGUE LADDER (SPEC.md 15.1.2)
;
; A routine that banks the canonical prologue - `push ax / bx / cx / dx / si /
; di / bp / es`, or any SUFFIX of it - restores it with the mirror-image pop
; run and returns.  That run is the same bytes in every routine that takes it,
; so 150 copies of it are 150 copies of the same six or seven bytes.  Here it
; is written ONCE, entered by `jmp` at the rung that pops first, and every
; converted routine spends 3 bytes on the jump instead of N on the run and its
; `ret`.  Nothing is pushed to get here and nothing is popped that the routine
; did not push: a rung is a suffix of a suffix.
;
; IT COSTS THE CONVERTED ROUTINE A TAKEN NEAR `jmp` (~18-22 clocks) AND SAVES
; IT (N+1)-3 BYTES.  So the eleven exits entered per RUN or per CELL keep their
; own pop runs - `gfx_clip_run`, `gfx_disp_run`, `gfx_blit_run`, `gfx_lstep`,
; `gfx_lstepv`, `gfx_lstep_slow`, `gfx_bank_ok`, `sw_pairbuild`,
; `font_run_scell`, `cur_lazyck`, `cur_shape_pass` - and that spare is what
; separates this from the arm that takes every site for 33 bytes more.
;
; PLACEMENT IS LOAD-BEARING, AND IT IS THE PART THAT WENT WRONG FIRST.  Each
; ladder is inside the section its callers are in and ABOVE that section's own
; `*_end` label: a ladder below the label is real image bytes that KTEXT_SIZE /
; COLD_SIZE do not count, and everything derived from them - KIMG_PARA,
; COLD_START, the KERN_CODE_MAX guard, SPEC.md 57.6's kbld_* - then
; under-reports the image.  The twelve `%if`s at the foot of this file are what
; makes that a build error rather than a silent one.
;
; HOW IT FAILS, AND IT FAILS CLOSED.  Delete the last `jmp` to a rung and that
; rung's label becomes a global nothing jumps to; `tools/stkbalance.py` then
; walks it as an ENTRY, from depth 0, and the build goes red with
; `kret_cx: ret at depth -3`.  That is the right answer - the cure is to delete
; the dead rung label, never to exempt it.  While a rung IS jumped to,
; stkbalance classes it a CONTINUATION: it inherits the arriving depth, so a
; routine that leaks a word still reaches the ladder's `ret` at +1 and is
; reported.  NO RUNG CARRIES stkbalance's skip annotation and no rung may: it
; makes `walk()` stop on arrival, which is the one thing that would genuinely
; blind the gate to all 141 converted routines at once.  (Its name is not
; spelled here either - that pragma is matched as a SUBSTRING of any comment,
; so writing it out in this block would exempt whatever routine owns the line
; and the gate would go from 2 exemptions to 3.  Found by doing it.)
;
; The ORDER half is `tests/unit/t_asmrules.py`'s `crossed_pops`, which reads the
; rung -> pop map out of the ladder below rather than assuming it - so a rung
; label sitting over the wrong `pop` is caught there, and a converted routine
; keeps exactly the coverage its own pop run had.
;
; One known imprecision, left deliberately: stkbalance dedups on
; (path, line, why), so two routines leaking the same amount into the same rung
; report as one line rather than two.  Both are still reported as findings; it
; is the count that collapses, and making it exact costs duplicate lines
; wherever one shared tail is reached from several entries.
; =============================================================================
; --- ...AND ITS PROLOGUE HALF, for `.text` (kernel size pass 10) ----------
; `call kent_bp` IS `push ax / bx / cx / dx / si / di / bp`, and `call kent_di`
; the same without BP: `.cold`'s kentc_bp / kentc_di, written again here
; because a `.text` caller cannot reach those near. The words land exactly
; where the pushes would put them, so the routine's own pops or a rung of the
; ladder below end it unchanged. Stack only, so re-entrant; no flag moves. The
; return address is moved out from under the banked registers by pushing a
; copy and storing AX over the original: every write is at or above SP, so an
; interrupt can land anywhere in here. It peaks one word above the pushes,
; which is where the routine's own next `call` puts its return address anyway.
; (kent_bp was wm.inc's wm_kent_bp, which the wm agent wrote for nine of its
; own prologues; it moved here when it stopped being wm.inc's alone.)
;
; 3 bytes a site against 7 (kent_bp) or 6 (kent_di), for ~95 cycles (~20 us)
; a call - so it is for routines entered per OPERATION, per window, per menu
; or per icon, never per glyph, run, span or pixel, never in the mouse ISR,
; the cursor, the scheduler or the API cell. wm_clip_rows (per glyph cell
; under a clip) and the save-under cache (docs/plans/LAST-DROP-BYTES.md 7.10)
; keep their pushes.
;
; AND NOTHING THE SPLASH CALLS MAY USE THEM: these sit at the END of `.text`,
; and stage 2's loading screen calls vid_apply, vid_setmode and gfx_rowbase
; (viddet.inc's spw_near) while the image is still arriving - SPL_RESIDENT's
; first sectors are aboard and this one is not.
kent_bp:
    ; STKBALANCE-NET: +7 - banks AX..BP on the CALLER's stack; its epilogue pops them
    push bx
    push cx
    push dx
    push si
    push di
    push bp                     ; [bp][di][si][dx][cx][bx][ret]
    mov bp, sp
    push word [bp+12]           ; [ret][bp]...[bx][ret]
    mov [bp+12], ax             ; ...[bx][ax]
    mov bp, [bp]                ; BP back
    ret

kent_di:
    ; STKBALANCE-NET: +6 - banks AX..DI on the CALLER's stack; its epilogue pops them
    push bx
    push cx
    push dx
    push si
    push di                     ; [di][si][dx][cx][bx][ret]
    mov di, sp
    push word [ss:di+10]        ; [ret][di][si][dx][cx][bx][ret]
    mov [ss:di+10], ax          ; ...[bx][ax]
    mov di, [ss:di]             ; DI back
    ret

kret_es:          pop es
kret_bp:          pop bp
kret_di:          pop di
kret_si:          pop si
kret_dx:          pop dx
kret_cx:          pop cx
                  pop bx
                  pop ax
kret_ret:         ret         ; NAMED so a test can breakpoint the RETURN.
                              ; A label is zero bytes and it retires a whole
                              ; class of stale address: tests/blitcut.py
                              ; breakpointed `gfx_blit4.pops + 7` - seven bytes
                              ; of `pop` and then the `ret`, counted by hand -
                              ; and this ladder replaced that run with a
                              ; three-byte `jmp` here. The offset then landed
                              ; on unrelated code, the breakpoint never fired,
                              ; and the row waited out 240s and reported that
                              ; no canvas blit had arrived: a sentence about
                              ; the kernel for a fault in one host-side line.
                              ; Every routine that leaves through this ladder
                              ; returns HERE, with sp back at its own entry
                              ; value, which is what a caller matching on sp
                              ; needs.

section .cold
; --- THE SHARED-PROLOGUE HELPERS (size pass 4 prototype) -------------------
; `call kentc_di` IS `push ax / bx / cx / dx / si / di`, and `call kentc_bp`
; the same and `push bp`: the words land exactly where the pushes would put
; them, so the routine's own pops and the ladder below end it unchanged. Stack
; only - re-entrant under the pre-emptive scheduler - and no flag moves. The
; return address is moved out from under the banked registers by pushing a copy
; and storing AX over the original: every write is at or above SP, so an
; interrupt can land anywhere in here. apps/word/word.asm's wd_sv6 is the same
; idea, and was first.
kentc_di:
    ; STKBALANCE-NET: +6 - banks AX..DI on the CALLER's stack; its epilogue pops them
    push bx
    push cx
    push dx
    push si
    push di                     ; [di][si][dx][cx][bx][ret]
    mov di, sp
    push word [ss:di+10]        ; [ret][di][si][dx][cx][bx][ret]
    mov [ss:di+10], ax          ; ...[bx][ax]
    mov di, [ss:di]             ; DI back
    ret

kentc_bp:
    ; STKBALANCE-NET: +7 - banks AX..BP on the CALLER's stack; its epilogue pops them
    push bx
    push cx
    push dx
    push si
    push di
    push bp                     ; [bp][di][si][dx][cx][bx][ret]
    mov bp, sp
    push word [bp+12]           ; [ret][bp]...[bx][ret]
    mov [bp+12], ax             ; ...[bx][ax]
    mov bp, [bp]                ; BP back
    ret

kretc_es:         pop es
kretc_bp:         pop bp
kretc_di:         pop di
kretc_si:         pop si
kretc_dx:         pop dx
kretc_cx:         pop cx
                  pop bx
                  pop ax
                  ret

kretfc_es:        pop es
kretfc_bp:        pop bp
kretfc_di:        pop di
                  pop si          ; unlabelled for the same reason as `cx`
kretfc_dx:        pop dx          ; below: app_about_paint_x was its last
                                  ; caller, and it returns near now (SPEC.md
                                  ; 2.6.3)
                  pop cx          ; UNLABELLED, and it is the `dx` rung above
                  pop bx          ; that took its last caller: `kretfc_cx` had
                  pop ax          ; exactly one jump in the tree and desk.inc's
cold_kretf:       retf            ; two paint entries merged onto `dx` instead.
                                  ; A rung nothing jumps to is walked as an
                                  ; ENTRY from depth 0 and goes red - so the
                                  ; label goes, per the rule above.
                                  ; cold_kretf is NOT a rung and nothing
                                  ; jumps to it: it is a COLD_SEG `retf` a
                                  ; module's hand-built far frame names as
                                  ; the near return of a cold body
                                  ; (filecp.inc's fcpx_go, kern_small's
                                  ; FILECP.DRV) - cw_kretf's job, one
                                  ; segment along, for no byte
section .text

; --- WHICH KERNEL IS THIS? (SPEC.md 57.6) ------------------------------------
; Three words that change whenever any section's length does, so a field
; report can name the build that produced it. They are SECTION-END LABELS, so
; they cost the machine no cycles at all, and they are STABLE in a way a
; checksum of the running image could not be - .text here carries
; mutable data on purpose (dsk_vtab, vid_w, fdd_dbg_*, fm_fchk), so a sum of
; it would depend on the adapter the machine booted on and on when it was
; taken.
;
; What it is NOT is a content hash: a byte-neutral edit - two instructions
; swapped - leaves every length alone and every term equal. That is the price
; of costing nothing, and it is the right trade, because the question this
; answers in practice is "is the disk in the drive the one I think it is",
; and an ordinary change moves at least one of these.
;
; Forward references: each label is an offset in a `vstart 0` section, so each
; IS that section's length, and a `dw` is not a critical expression - nasm
; resolves them in a later pass.
kbld_dbg_blk:
    dw DBG_TAG_BUILD                ; 'ID' - the magic, and the block's own
    dw kbld_dbg_span                ; first word (SPEC.md 57)
kbld_dbg_span:
kbld_text: dw kernel_text_end       ; THREE PLAIN WORDS, not one hashed one:
kbld_cold: dw cold_end              ; nasm refuses arithmetic between
kbld_blob: dw boot2_entry           ; relocatable symbols ("expression is not
                                    ; simple or relocatable"), and the `equ`s
                                    ; that hold these lengths are defined
                                    ; BELOW - after `kernel_text_end`, which
                                    ; must stay the last thing in .text. Three
                                    ; words is the better answer anyway: a
                                    ; reader gets the sections themselves and
                                    ; can see WHICH one moved, where a hash
                                    ; only says that something did.
                                    ; THE THIRD IS THE BLOB UP TO THE LOADER -
                                    ; the overlay's blob half and the splash -
                                    ; and NOT boot2_end (SPEC.md 2.9.14): the
                                    ; loader is the last thing in `.boot2` and
                                    ; its size moves with the -DKZ_* numbers
                                    ; pass 2 hands it, so a word in `.text`
                                    ; naming the blob's END would make the two
                                    ; packings differ (SPEC.md 2.9.13.4)
KBLD_DBG_SPAN equ ($ - kbld_dbg_span)
%if KBLD_DBG_SPAN != 6
  %error "kernel: SPEC.md 57.6 block - the published span is not 6 bytes"
%endif

; =============================================================================
; Size guards (SPEC.md 15.1). Same-section label differences bound via equ -
; a bare label in %if is a non-scalar and will not assemble, and a difference
; across two sections is not a constant at all, which is why each section
; measures itself against its own $$.
;
; kernel_text_end MUST be the last thing in .text: it is simultaneously the
; size of the image and the base of .bss (see the section layout at the top of
; this file), and through KIMG_PARA it is where the FAT snapshot begins.
; =============================================================================
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

; ...AND THE ONE SPEC.md 15 SAYS IS "on spw_resident_end", WHICH HAD NO CODE.
; The label was defined in viddet.inc and referenced by nothing in the tree -
; grep answered one line - so a constant three sections and two documents call
; binding was measuring nothing at all. It is here rather than beside the label
; because SPL_RESIDENT is splash.inc's and splash.inc is included after
; viddet.inc; by the foot of the file both are settled.
;
; WHAT IT GUARDS. Stage 2 ticks the loading screen once the image's first
; SPL_RESIDENT sectors are aboard, and the first tick PROBES THE ADAPTER -
; vid_detect, vid_apply, vid_setmode, gfx_rowbase - the first in the blob
; itself since SPEC.md 2.5.3.3, the other three through the far shims that end
; at this label. Everything the first tick can reach has to be
; below the line, and a call that leaves it is a call into sectors the floppy
; has not delivered yet: no fault, no message, whatever the machine left in
; that memory.
;
; It is also what refused the tempting shape here for a while: routing
; spw_vid_detect into the boot overlay through spl_gate. `.boot2` is aboard
; before stage 1 jumps and vid_detect would be too, which reads as free - but
; spl_gate is at the far end of `.text`, so the PATH is not aboard even though
; the destination is. docs/plans/LAST-DROP-BYTES.md rows 10 and 22 were refused
; on this. SPEC.md 2.5.3.3 took them anyway by changing the PATH: spl_chrome
; is `.boot2`, the same segment as `.boot2`, so it reaches vid_detect with a
; BLOBCALL (`push cs` / near call) and no `.text` byte in between.
SPL_RES_SIZE equ spw_resident_end - $$
%if SPL_RES_SIZE > SPL_RESIDENT * 512
%error "the splash's first tick reaches code outside the sectors stage 2 waits for - raise SPL_RESIDENT in BOTH kernel/splash.inc and boot/boot2.asm, or move what grew (SPEC.md 15)"
%endif

section .lowbss
kernel_low_end:
KLOW_SIZE equ kernel_low_end - $$

section .vgabuf
kernel_vgabuf_end:
KVGABUF_SIZE equ kernel_vgabuf_end - $$

section .bss
; modules declared their own .bss blocks; NASM accumulates them in order,
; so this lands last
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$

; `.cold`'s file position IS the image rung, which is what makes stage 2's
; single contiguous read land it on COLD_SEG. It cannot be expressed as padding
; inside .text - that would grow KTEXT_SIZE, which grows KIMG_PARA, which grows
; the padding, and there is no fixed point - but as a SECTION start it is not
; circular at all.
;
; The boot overlay used to follow it at the next rung, which is what put it
; in the FAT window. Its blob half is part of `.boot2` now (SPEC.md 2.9.6,
; 2.9.14), so it is not on this ladder at all and the image ends where `.cold`
; does.
COLD_START equ BOOT2_PAD + ((KTEXT_SIZE + KBSS_SIZE + 511) / 512) * 512

section .boot2
%include "../boot/boot2.asm"    ; STAGE 2 (SPEC.md 2.9), here at the FOOT of
                                ; the file because B2_KSECS is derived from
                                ; MODC_START and every other size constant is
                                ; settled by now. `-f bin` places the section
                                ; at file offset 0 whatever the source order
boot2_end:
BOOT2_SIZE equ boot2_end - $$   ; THE WHOLE BLOB: the loader, the splash and
                                ; the boot overlay's blob half, one section
                                ; (SPEC.md 2.9.14) and so one ceiling. There
                                ; was a second guard here, the loader against
                                ; a hand-set OVL_AT, and a third for the
                                ; overlay above it - two budgets cut out of one
                                ; pool by a line nothing needed
%if BOOT2_SIZE > BOOT2_PAD
%error "the boot blob (loader, splash and the overlay's blob half) does not fit BOOT2_SECS sectors - move a body that is dead before the first mount to `.ovl`, or raise BOOT2_SECS in this file after tests/unit/t_blobruns.py says what it costs (SPEC.md 2.9.6, 2.9.14)"
%endif

section .cold
cold_end:
COLD_SIZE equ cold_end - $$

section .ovl
ovl_end:
OVL_SIZE equ ovl_end - $$     ; `$$` is 0 here: `.ovl` has a vstart of its
                                ; own and is addressed with CS = FAT_SEG

; ...AND THE WINDOW HALF HAS A CEILING OF ITS OWN (SPEC.md 2.5.3), which is the
; whole of what is dead at the first mount: the FAT window, and the mount-owned
; buffers stage B put at the bottom of `.lowbss` immediately above it (SPEC.md
; 2.1.2). One contiguous region, and `.ovl` may fill it and no more - a byte
; past it is `vid_rowtab`, which vid_init wrote at MARK 7 and the overlay is
; still using.
; **The overlay ARRIVES BY SECTOR**, so the ceiling is the region rounded DOWN
; to a whole sector and the payload rounded UP to one. That was the same
; number while the region was 8,192 - the mount window being 7 x 512 exactly -
; and it stopped being when SPEC.md 19.1's staged listing narrowed to
; DSK_DE_STRIDE and took `disk_dir` from 1,024 to 768. Comparing against the
; raw region would pass an overlay whose LAST sector lands on vid_rowtab.
;
; Since LISTING-HOME-PLAN 13 took the listing out of the region it is the
; FAT window plus dsk_secbuf, and both builds are measured (kernel size pass 4):
;
;   kern_big    region 5,120 (4,608 + 512), `.ovl` 4,968 -> 5,120 rounded.
;               152 bytes of payload spare, NO whole sector (kernel size
;               pass 8's close, after font_init moved into the blob).
;   kern_small  region 1,536 (1,024 + 512), `.ovl` 1,497 -> 1,536 rounded.
;               39 bytes of payload spare.
;
; So KERN_BIG binds this guard. The blob is the other home for a boot body:
; since SPEC.md 2.5.3.3 put kmain's boot half in it, `.boot2` leaves 308 bytes
; of it on kern_big and 415 on kern_small at ten sectors (kernel size pass 8
; grew it from nine for SPEC.md 6.0.1's timed font pick), so the window half
; binds kern_big again. A KNOB
; build has DSK_OVLPAD's 1,024 more here, and 2.5.3.3.1 is what spends it.
%if ((OVL_SIZE + 511) / 512) * 512 > FAT_PARA * 16 + DSK_WIN_BYTES
%error "the boot overlay's window half has outgrown the FAT window plus dsk_secbuf - see SPEC.md 2.1.2 and 2.5.3. Move a body to the blob (`.ovl`, SPEC.md 2.5.3.2) or out of the boot path"
%endif

; --- the on-demand modules' file positions (SPEC.md 2.8) ---------------------
; UNPADDED, unlike COLD_START, and that is the point: a module is extracted
; into a file of its own rather than read at an address, so nothing needs it
; aligned - and MODC_START is then exactly where the kernel image ends, so
; `truncate to MODC_START` reproduces the pre-module kernel.bin byte for byte.
; tools/os88mod.py proves that rather than trusting it.
; The image ends where `.ovl` does, not where `.cold` does: the window half of
; the boot overlay is the last thing stage 2 reads (SPEC.md 2.5.3).
MODC_START   equ OVL_START + OVL_SIZE
MODF_START   equ MODC_START + MODC_SIZE
MODL_START   equ MODF_START + MODF_SIZE
%ifdef KERN_BIG
MODH_START   equ MODL_START + MODL_SIZE   ; hibernate, kern_big's alone
MODK_START   equ MODH_START + MODH_SIZE
%else
MODP_START   equ MODL_START + MODL_SIZE   ; Cut/Copy/Paste, kern_small's alone
MODD_START   equ MODP_START + MODP_SIZE   ; ...and the file dialog after it
MODMAP_START equ MODD_START + MODD_SIZE   ; no Dock module (SPEC.md 30.5)
%endif                                    ; The
                                          ; compressor has no image of its
                                          ; own: it rides in the cloner's
                                          ; (SPEC.md 20.15.3)

%ifdef DOCK_OPT                           ; DOCK_OPT is KERN_BIG, so
MODX_START   equ MODK_START + MODK_SIZE   ; EXTD.DRV (SPEC.md 39.19.6) and
MODW_START   equ MODX_START + MODX_SIZE   ; WIN95.DRV (SPEC.md 76.14) are
MODMAP_START equ MODW_START + MODW_SIZE   ; always the last two images there
%endif

%ifdef KERN_BIG
section .modx
modx_end:
MODX_SIZE equ modx_end - $$
section .modw
modw_end:
MODW_SIZE equ modw_end - $$
%if MODW_SIZE > MOD_MAX_KB*1024
  %error "WIN95.DRV exceeds its maximum claim"
%endif
%endif
%ifdef DOCK_OPT
section .modk
modk_end:
MODK_SIZE equ modk_end - $$
%if MODK_SIZE > MOD_MAX_KB*1024
  %error "Dock module exceeds its maximum claim"
%endif
%endif

section .modc
mods_end:
MODS_SIZE equ mods_end - $$     ; the settings core: CTRL.DRV's first bytes
section .modu
%ifdef ANIMOFF
; ANIMOFF=1 takes the zoom outline's callers out of the panel, and kernel size
; pass 10 had already taken CTRL.DRV to 11,301 bytes: the knob's image came to
; 11,177, so its claim rounded to 11KB and the 131 bytes of .modcb below did
; not fit it (the MODC_BSS assertion). DOSRMARK=1's answer for HIBER.DRV (the
; .modh block above): pad the knob's image to ONE byte past a KB boundary, so
; the rounding leaves 1,023 bytes whatever either side grows to - a disk-only
; cost, on a build that ships nowhere. `.modu` does not start KB-aligned, so
; the pad is counted from the IMAGE's start (MODS_SIZE in), not `align`'s
    times 1024 - ((MODS_SIZE + ($ - $$)) % 1024) db 0
    db 0
%endif
modc_end:
MODU_SIZE equ modc_end - $$     ; ...the panel behind it...
MODC_SIZE equ MODS_SIZE + MODU_SIZE ; ...and the WHOLE image: align=1 on both
                                ; ends of `.modu`, so there is no pad between

section .modf
modf_end:
MODF_SIZE equ modf_end - $$

section .modl
modl_end:
MODL_SIZE equ modl_end - $$

%ifdef KERN_BIG
section .modh
%ifdef DOSR_MARK
; DOSRMARK=1's trace (kernel/hbmark.inc, staged through hbstub.inc) is ~290
; bytes of the IMAGE, and the bss below has to fit the KB rounding of the
; claim the image sizes (MODULE-SELFCONTAIN-PLAN 3.2): the shipped image
; leaves it 63 bytes of slack, so the knob overran it by 227 and the kernel
; refused to assemble. The knob pads its own image to ONE byte past a KB
; boundary instead, which leaves 1,023 bytes of rounding whatever either
; side grows to - a disk-only cost, and only on a build that ships nowhere.
    align 1024, db 0
    db 0
%endif
modh_end:
MODH_SIZE equ modh_end - $$
section .modhb
modhb_end:
MODH_BSS equ modhb_end - $$
%endif

%ifdef OS88_DRIVERS
section .modcb
modcb_end:
MODC_BSS equ modcb_end - $$
%endif

%ifdef FCP_MOD
section .modp
modp_end:
MODP_SIZE equ modp_end - $$
section .modpb
modpb_end:
MODP_BSS equ modpb_end - $$
%endif

%ifdef FDLG_MOD
section .modd
modd_end:
MODD_SIZE equ modd_end - $$
%endif

; ...and each of them has to fit the claim mod_need makes for it. MOD_MAX_KB
; (mod.inc) is a RUN-TIME test - a module over it is refused cleanly, the
; feature simply does not open - so nothing would say a word at build time
; about an image that has grown past the only claim there is to put it in.
; This is where that gets said, while the number is still a constant.
%if MODC_SIZE > MOD_MAX_KB*1024
%error "the ctrl module is over MOD_MAX_KB - mod_need would refuse it at run time"
%endif
%ifdef OS88_DRIVERS
 %if MODC_SIZE + MODC_BSS > ((MODC_SIZE + 1023) / 1024) * 1024
%error "CTRL.DRV's bss (the SYSTEM.CFG writer's buffer) does not fit its claim's KB rounding - see MODULE-SELFCONTAIN-PLAN 3.2"
 %endif
%endif
%if MODF_SIZE > MOD_MAX_KB*1024
%error "the format module is over MOD_MAX_KB - mod_need would refuse it at run time"
%endif
%if MODL_SIZE > MOD_MAX_KB*1024
%error "the clone module is over MOD_MAX_KB - mod_need would refuse it at run time"
%endif
%ifdef KERN_BIG
 %if MODH_SIZE > MOD_MAX_KB*1024
%error "the hibernate module is over MOD_MAX_KB - mod_need would refuse it at run time"
 %endif
 %if MODH_SIZE + MODH_BSS > ((MODH_SIZE + 1023) / 1024) * 1024
%error "HIBER.DRV's bss (the DOS record and the pointer buffer) does not fit its claim's KB rounding - see MODULE-SELFCONTAIN-PLAN 3.2"
 %endif
%endif
%ifdef FCP_MOD
 %if MODP_SIZE > MOD_MAX_KB*1024
%error "the Cut/Copy/Paste module is over MOD_MAX_KB - mod_need would refuse it at run time"
 %endif
; ...and its bss has to fit inside the KB ROUNDING of that same claim, which
; is what makes it cost no heap at all (MODULE-SELFCONTAIN-PLAN 3.2). The risk
; is a CLIFF and not a slope - an image that grows past a KB boundary loses
; the room all at once - so it is asserted here, in the file that grew, rather
; than discovered as a module scribbling on whatever follows its claim.
; If this ever fires, MODULE-SELFCONTAIN-PLAN 3.2 names the fallback: a word
; per module in mod_tab, added to AX before mem_bytes_kb_x, for 12 resident
; bytes on kern_small. Do that WHEN it fires and not before.
 %if MODP_SIZE + MODP_BSS > ((MODP_SIZE + 1023) / 1024) * 1024
%error "FILECP.DRV's bss does not fit its claim's KB rounding - see MODULE-SELFCONTAIN-PLAN 3.2"
 %endif
%endif
%ifdef FDLG_MOD
 %if MODD_SIZE > MOD_MAX_KB*1024
%error "the file dialog module is over MOD_MAX_KB - mod_need would refuse it at run time"
 %endif
%endif

; --- the split table, which is HOST-SIDE ONLY --------------------------------
; os88mod.py has to know where each module starts and how long it is, and the
; only thing that can answer without a second opinion is this assembly. So it
; says so, in a trailer: the file's last two bytes are the magic and the two
; before them point back at the table. The whole of it - modules and trailer -
; is stripped from kernel.bin, so not one byte of this reaches a machine.
;
; The alternative was assembling the kernel a second time under -DKERNSIZE and
; parsing the `ks:` line, which is what tools/kernsize.py does. It works and it
; costs an assembly; this costs 18 bytes of a file that is about to be cut up.
section .modmap
mod_map:
    db 'O8MM'
    db MOD_NIMG                 ; rows below - IMAGES, so MOD_SETS (CTRL.DRV's
                                ; own first bytes, SPEC.md 2.8.7) is not one
    db 0
    dd MODC_START, MODC_SIZE
    dw CP_NENT                  ; ...and each row the KERNEL's entry count
                                ; DWORDS: a file offset past 64KB is the
                                ; ordinary case here, the kernel being ~100KB
                                ; before a module is added, and a `dw` of it
                                ; assembles as a silent truncation under any
                                ; flags but this tree's -w+error
    dd MODF_START, MODF_SIZE
    dw FM_NENT
    dd MODL_START, MODL_SIZE
    dw CLO_NENT
%ifdef KERN_BIG
    dd MODH_START, MODH_SIZE    ; ...and kern_big's fourth: hibernate (87)
    dw HB_NENT
%else
    dd MODP_START, MODP_SIZE    ; ...or kern_small's fourth (SPEC.md 22.3)
    dw FCP_NENT
    dd MODD_START, MODD_SIZE    ; ...and its fifth (SPEC.md 38.0)
    dw FD_NENT
%endif
%ifdef DOCK_OPT
    dd MODK_START, MODK_SIZE    ; optional advanced Dock (kern_big)
    dw DK_NENT
%endif
%ifdef KERN_BIG
    dd MODX_START, MODX_SIZE    ; the extended desktop (SPEC.md 39.19.6)
    dw EXT_NENT
    dd MODW_START, MODW_SIZE    ; the Windows theme's chrome (SPEC.md 76.14)
    dw WN_NENT
%endif
    dd MODMAP_START             ; ...where the table began, and
    dw 0x384F                   ; the last two bytes of the file
modmap_end:
MODMAP_SIZE equ modmap_end - $$

; What the Task Manager's RAM view reports (SPEC.md 28), in KB rounded up:
; the whole kernel, buffers and stacks included, because since SPEC.md 2 that
; is one contiguous span and there is nothing of the kernel outside it.
KERN_KB    equ (KERN_SIZE + 1023) / 1024

; --- osapi_sys_kb's five footprint terms, in BYTES (SPEC.md 20.9) ------------
; One per row of the memory view, and every one of them a span this ladder
; actually declares. They are here rather than in memory.inc because they are
; ladder arithmetic and KLOW_SIZE is not known until the foot of this file;
; the block's FIELD OFFSETS stay with the routine that fills it (SPEC.md 4).
;
; SKB_IMG IS THE ONLY ONE THAT IS "THE REST", and unlike the residual it
; replaces it is the honest name for what it holds: the image, `.bss`, the
; cold segment (SPEC.md 2.6), the planar decoder's rung (39.22) and the
; kernel's own TABLES in `.lowbss` - the glyph table, the row table, the pair
; tables, the claim map, the event ring, the menu bar, the built-in state
; pools and the rung's own padding. Every one of those is `.bss`-class data
; that sits in LOW_SEG to buy back KERN_CODE_MAX and for no other reason
; (docs/KERNEL-MEMORY.md, "Moving data out of the segment"), so `Code+data`
; was always its label - it just used to be billed to `Disk bufs`, which read
; 6 KB for 3.5 KB of buffer.
;
; ...AND THE MOUNT'S OWN WINDOW IS INSIDE IT NOW, WHICH IS WHY THERE ARE FOUR
; TERMS AND NOT FIVE. SKB_DSK was DSK_WIN_BYTES - the three mount-owned
; buffers of SPEC.md 2.1.2 - and two changes emptied it: 25.9 took the icon
; bodies to one machine-wide store, and docs/plans/LISTING-HOME-PLAN.md 13
; took the entries and their reference index into the store the CALLER
; supplies. What is left is `dsk_secbuf`, 512 bytes of int 13h scratch, and
; half a kilobyte cannot be a row here: under the cumulative rounding below
; it read `-` on kern_big and 1 KB on kern_small - THE SAME BUFFER, reported
; as two different sizes, because a half-kilobyte part is decided by where
; the running boundary happens to fall. A row that reports the arithmetic
; rather than the machine is not a row, so the 512 bytes accumulate into
; SKB_IMG with the rest of `.lowbss`, which is the class they were always in.
; The guard below still checks the window is only that sector buffer - what
; went away is the ROW, not the fact.
SKB_FAT    equ FAT_PARA * 16              ; the mount-time FAT snapshot (18.8)
SKB_STK    equ SCH_STK_TOTAL              ; the background slices
SKB_STK0   equ STK0_SIZE                  ; ...and task 0's
SKB_IMG    equ KERN_SIZE - SKB_FAT - SKB_STK - SKB_STK0

; --- ...and in KB, rounded CUMULATIVELY so that they still sum --------------
; Every rung is a whole number of 512-byte sectors, so half of them are an odd
; half-kilobyte: SKB_FAT is 4.5 KB on kern_big and SKB_STK 2.75, and parts
; rounded one at a time gain kilobytes against a total that rounds once. The
; old answer was a residual to dump the error in, which is how a row came to
; be 2,944 bytes out with nothing bounding it.
;
; So nothing is rounded per part. Each running BOUNDARY is rounded to the
; nearest kilobyte and each part is the DIFFERENCE of two rounded boundaries,
; which telescopes: the parts sum to SK_R of the last boundary whatever they
; are individually. That boundary is KERN_SIZE, a multiple of 512, where
; nearest and up agree - so the sum is KERN_KB exactly, and no part is a whole
; kilobyte from its true size because each boundary moves by at most half of
; one. Guard 7 proves both.
;
; SKB_IMG IS ACCUMULATED LAST, AND THAT IS THE WHOLE OF THE ORDERING RULE.
; The other three are fixed facts about the build - SCH_PARTITION's slices,
; STK0_SIZE, DSK_FAT_SECS sectors - each a constant of its own configuration
; and none of them a remainder of anything. Put the image first and they
; become one: its half-kilobyte lands in the running total, so every boundary
; after it moves with the build and a buffer that never changed size rounds
; one way on one configuration and the other way on the next. Last, the image
; takes the build's own remainder, which is the one row it belongs to -
; SKB_IMG is the only term here that moves at all.
;
; THAT LAST SENTENCE IS NOT HYPOTHETICAL AND IT IS WHAT RETIRED SKB_DSK: 512
; bytes of sector buffer read `-` on kern_big and 1 KB on kern_small, from
; the same ladder and the same buffer. A part this small is the rounding's
; answer and not the machine's, which is the whole argument for folding it
; into the term that is a remainder anyway.
;
; What is left is the tie the ladder genuinely cannot break: on kern_big the
; FAT window is 4.5 KB and the stacks 3.25, and which way each falls is
; decided by the running boundary rather than by a rule about buffers.
%define SK_R(b)  (((b) + 512) / 1024)     ; bytes -> KB, nearest, ties up
SK_CUM1    equ SKB_STK
SK_CUM2    equ SK_CUM1 + SKB_STK0
SK_CUM3    equ SK_CUM2 + SKB_FAT
SK_CUM4    equ SK_CUM3 + SKB_IMG          ; == KERN_SIZE, by construction
SK_STK_KB  equ SK_R(SK_CUM1)
SK_STK0_KB equ SK_R(SK_CUM2) - SK_R(SK_CUM1)
SK_FAT_KB  equ SK_R(SK_CUM3) - SK_R(SK_CUM2)
SK_IMG_KB  equ SK_R(SK_CUM4) - SK_R(SK_CUM3)

; SK_BUF, the part of the kernel that is scratch rather than program - and the
; BAND SPEC.md 28's map draws over the kernel's own gray. It is the three
; terms above telescoping to their own last boundary, so it is the two buffer
; ROWS added up and cannot disagree with them: 8 KB on kern_big and 2 on
; kern_small, and no knob moves either.
;
; It follows the ROWS and not the FAT+LOW rungs, which is a change: the rungs
; are bigger, and what is in them and not in a row is the tables the list
; bills to Code+data - and, since SKB_DSK was retired, `dsk_secbuf` with
; them. Drawn the old way the band would claim territory no row under it
; admits to, and a legend square keying a row to a band it is not in is worse
; than no square at all (SPEC.md 28).
KBUF_KB    equ SK_R(SK_CUM3)
; ...and what a machine with NO VGA gives back, taken off SK_KERN and SK_IMG
; together so the parts go on summing (SPEC.md 39.22). Derived from the same
; rounding as the rows rather than written out as a KB constant of its own:
; VGABUF_PARA is only guaranteed to be a multiple of 32 PARAGRAPHS, so a rung
; of 1.5 KB would move the boundary by one kilobyte and a hand-rounded 2 would
; make the column stop totalling on mono alone. It comes off the LAST boundary
; because that is the one the rung is inside - it is part of SKB_IMG.
SK_VGAB_KB equ SK_R(SK_CUM4) - SK_R(SK_CUM4 - VGABUF_PARA * 16)

; --- the size report, for tools/kernsize.py (docs/KERNEL-MEMORY.md) ----------
; Every figure in the ladder, published in one line, so that measuring the
; kernel is a command rather than a bisect. It is a knob and not an
; unconditional %warning because -w+error is deliberately strict here: relax
; the `user` class for every build and a %warning somebody adds as a real
; alarm stops failing. `make` runs it with -DKERNSIZE -w-error=user, which
; changes not one byte of the code being measured.
;
; %assign is what makes this work: it defines a single-line macro that
; expands to the EVALUATED number, and %warning macro-expands its argument.
; The line is tagged `ks:` and not `KERNSIZE` for the same reason: -D defines
; that name as the empty string, so %warning expanded the tag to nothing.
%ifdef KERNSIZE
  %assign KS_TEXT   KTEXT_SIZE
  %assign KS_BSS    KBSS_SIZE
  %assign KS_COLD   COLD_SIZE
  %assign KS_LOW    KLOW_SIZE
  %assign KS_VGAB   KVGABUF_SIZE
  %assign KS_OVL   OVL_SIZE    ; the window half (SPEC.md 2.5.3)
  %assign KS_BOOT2  BOOT2_SIZE   ; THE WHOLE BLOB (SPEC.md 2.9.14): the
                                 ; loader, splash.inc and the overlay's blob
                                 ; half. Not a rung anybody spends into - it
                                 ; is padded to BOOT2_PAD and given back at
                                 ; spl_finish - but it has a ceiling, and the
                                 ; per-module table reads splash.inc's bytes
                                 ; out of it
  %assign KS_STK0   STK0_SIZE
  %assign KS_IMGP   KIMG_PARA
  %assign KS_COLDP  COLD_PARA
  %assign KS_FATP   FAT_PARA
  %assign KS_LOWP   LOW_PARA
  %assign KS_VGABP  VGABUF_PARA
  %assign KS_SIZE   KERN_SIZE
  %assign KS_BUDGET KERN_BUDGET
  %assign KS_GRACE  KERN_GRACE
  %assign KS_CODEM  KERN_CODE_MAX
  %assign KS_END    KERN_END
  %assign KS_KSEG   KERNEL_SEG
  %assign KS_MINRAM MIN_RAM_KB   ; ...and guard 5's ceiling, because a report
  %assign KS_BOOTMX MIN_RAM_KB*1024 - BOOT_SECT - BOOT_STACK - KERNEL_SEG*16 - BOOT2_PAD
                                 ; that names one guard teaches everybody to
                                 ; steer by it, and for a while the one it
                                 ; named was the looser of the two
  %warning ks: text=KS_TEXT bss=KS_BSS cold=KS_COLD lowbss=KS_LOW vgabuf=KS_VGAB ovl=KS_OVL boot2=KS_BOOT2 stk0=KS_STK0 imgpara=KS_IMGP coldpara=KS_COLDP fatpara=KS_FATP lowpara=KS_LOWP vgabufpara=KS_VGABP ksize=KS_SIZE budget=KS_BUDGET codemax=KS_CODEM kend=KS_END kseg=KS_KSEG minramkb=KS_MINRAM bootmax=KS_BOOTMX grace=KS_GRACE
%endif

; 1. KERN_BUDGET - the FOOTPRINT. The whole kernel - image, scratch, FAT
;    snapshot, disk buffers and every task stack - is one span starting at
;    KERNEL_SEG, and it fits KERN_BUDGET (100,864 on kern_big, 64,000 on
;    kern_small) just above the BIOS data area, with KERN_GRACE over it
;    allowed in branch and integration work. The exact figure is checked by
;    `make budget-check` at an upstream merge or a release; raising a budget
;    is a decision with the owner, not a build fix (docs/KERNEL-MEMORY.md).
;
;    **A KNOB KERNEL IS NOT THE SHIPPED KERNEL, AND KERN_BUDGET DOES NOT
;    BIND IT.** Guard 2 below does, and nothing else does: a build with any
;    knob on it exists to answer a question about a machine, it is not what
;    anybody boots, and the RAM it takes is a fact about that session rather
;    than about the product. `-DKERN_KNOB` is how it knows - the Makefile
;    passes it whenever $(KNOBS) holds anything but KERN_SMALL, which is a
;    SHIPPED configuration with a budget of its own and stays bound by it.
;    docs/history/KERN-BUDGET-LEDGER.md has the five knobs that taught this.
;
;    What bounds a knob kernel is guard 2 (one 64KB segment, which nobody can raise) and
;    guard 5 (MIN_RAM_KB, which is about a machine rather than a policy) -
;    and, for a FIELD kernel specifically, tools/fieldsize.py, which reports
;    whether it shares a KIMG_PARA rung with the shipped one so that "bigger"
;    stays known about rather than silent.
%ifndef KERN_KNOB
%if KERN_SIZE > KERN_BUDGET + KERN_GRACE
%error "kernel too big: past KERN_BUDGET's branch grace - see docs/KERNEL-MEMORY.md"
%endif
%endif
; 2. KERN_CODE_MAX - the SEGMENT. The kernel's own segment is 64KB like any
;    other, and .text + .bss are both addressed through it, so they have to
;    fit one whether or not the budget above is ever raised. Nobody can raise
;    this one: it is what a 16-bit offset reaches.
%if KTEXT_SIZE + KBSS_SIZE > KERN_CODE_MAX
%error "kernel image + bss overflows KERN_CODE_MAX - one 64KB segment"
%endif
; 2d. THE DECODER'S BUFFER RUNG (SPEC.md 39.22). Two things, and the first is
;    what keeps guard 6 a proof rather than a hope: the mono heap floor is
;    HEAP_SEG - VGABUF_PARA, so unless the rung is a whole number of 32-
;    paragraph steps a mono machine's heap starts unaligned and int 13h
;    answers a straddling transfer with error 09h - "Disk error" on a large
;    save, on mono only. The second is the ordinary one: what is declared has
;    to fit what was reserved.
%if VGABUF_PARA % 32
%error "VGABUF_PARA must be a multiple of 32 paragraphs or the mono heap floor loses its 512-byte alignment - see guard 6"
%endif
%if KVGABUF_SIZE > VGABUF_PARA * 16
%error "the planar decoder's buffers overflow VGABUF_PARA - raise it by 32 paragraphs at a time"
%endif
; 3. task 0's stack grows DOWN from STK0_TOP onto the top of .lowbss, and
;    both live in LOW_SEG. STK0_SIZE is the whole of the gap between them,
;    so this proves the constant is actually a stack and not a rounding
;    error - and that a LOW_SEG offset still fits a 16-bit register.
%if STK0_SIZE < 512
%error "STK0_SIZE is too small to be a stack"
%endif
%if KLOW_SIZE + STK0_SIZE > 65536
%error "lowbss + task 0's stack overflows one 64KB segment"
%endif
; 3b. menu_bar is a LITERAL byte count (.bss may not forward-reference), so
;    nothing makes it follow MENU_BARMAX. It gained a cell the day the app
;    name became a pull-down (SPEC.md 12.2); this is what catches the next one.
%if MENU_BARMAX * MB_ENTSZ > 84
%error "menu_bar is too small for MENU_BARMAX cells - raise the resb in menu.inc"
%endif
; 4. the menu save-under (SPEC.md 2.2/12.4) must fit MENU_SAVE_KB. gfx_save
;    costs planes x rows x (byte span + 1); the two clamps in menu.inc bound
;    both factors, and this is where they are checked. The claim itself is
;    menu_save_kb, sized from the RECT ACTUALLY DROPPED - so this is now the
;    ceiling that arithmetic can never exceed rather than the figure claimed,
;    and what it really proves is that menu_save_kb's multiplies stay inside
;    16 bits. The save-under is the ONE kernel buffer outside the budget
;    above, deliberately: it exists only while a menu is down (SPEC.md 50).
%if 4 * (MENU_POPMAX*MENU_ITEM_H + 2) * (MENU_MAXW/8 + 2) > MENU_SAVE_KB*1024
%error "menu save-under can overflow its claim - lower MENU_POPMAX/MENU_MAXW"
%endif
; 6. every base an int 13h transfer can land on is 512-byte aligned, or a
;    single-sector read can still straddle a 64KB DMA boundary and fail with
;    error 09h (see KIMG_PARA above). A segment is 512-aligned when it is a
;    multiple of 32 paragraphs.
%if (KERNEL_SEG % 32) || (FAT_SEG % 32) || (LOW_SEG % 32)
%error "a disk-buffer segment is not 512-byte aligned - see KIMG_PARA"
%endif
%if HEAP_SEG % 32
%error "the heap base is not 512-byte aligned - see KIMG_PARA"
%endif
; 7. osapi_sys_kb's parts (SPEC.md 20.9) SUM TO THE WHOLE, and none of them
;    is more than a kilobyte from what it really measures. Those are two
;    different properties and a residual only ever had the first: the row
;    labelled `Disk bufs` totalled correctly at 6 KB while the buffers it
;    named were 3,584 bytes, because everything the other rows did not claim
;    was landing in it. The bound is the half of this that could not have
;    been asserted before. That same row is what proved the bound is not the
;    whole story either: 512 bytes is WITHIN a kilobyte of what it measures
;    on both shipped kernels and still reads `-` on one and 1 KB on the
;    other, which is why it was folded into SKB_IMG rather than left here
;    reporting the rounding (SPEC.md 20.9).
;
;    The sum is telescoping and holds by construction, so what this really
;    checks is that KERN_SIZE is still a whole number of kilobytes' worth of
;    512-byte rungs - i.e. that SK_R's round-to-nearest and KERN_KB's round-up
;    still agree on the last boundary. Break the ladder's 512-alignment and
;    the column stops totalling; guard 6 would catch it first, and this says
;    what the second symptom would have been.
%if SK_IMG_KB + SK_STK_KB + SK_STK0_KB + SK_FAT_KB != KERN_KB
%error "osapi_sys_kb's parts no longer sum to KERN_KB - the memory view's column will not total (SPEC.md 20.9)"
%endif
%if SK_IMG_KB*1024 - SKB_IMG >= 1024 || SKB_IMG - SK_IMG_KB*1024 >= 1024
%error "sys_kb: the Code+data row is a kilobyte or more from what it measures (SPEC.md 20.9)"
%endif
%if SK_STK_KB*1024 - SKB_STK >= 1024 || SKB_STK - SK_STK_KB*1024 >= 1024
%error "sys_kb: the background-stack term is a kilobyte or more from what it measures (SPEC.md 20.9)"
%endif
%if SK_STK0_KB*1024 - SKB_STK0 >= 1024 || SKB_STK0 - SK_STK0_KB*1024 >= 1024
%error "sys_kb: task 0's stack term is a kilobyte or more from what it measures (SPEC.md 20.9)"
%endif
%if SK_FAT_KB*1024 - SKB_FAT >= 1024 || SKB_FAT - SK_FAT_KB*1024 >= 1024
%error "sys_kb: the FAT snap row is a kilobyte or more from what it measures (SPEC.md 20.9)"
%endif
;    ...and the same for the machine with no VGA, where SK_KERN and SK_IMG are
;    both taken down by SK_VGAB_KB. The subtractions cancel in the sum for any
;    value at all, so what is worth checking is that the one they use is the
;    right rounding of the rung - a hand-written constant here is how a mono
;    column would come to be a kilobyte out with every VGA machine fine.
%if VGABUF_PARA && (SK_VGAB_KB*1024 - VGABUF_PARA*16 >= 1024 || VGABUF_PARA*16 - SK_VGAB_KB*1024 >= 1024)
%error "sys_kb: SK_VGAB_KB is a kilobyte or more from the planar rung it gives back (SPEC.md 39.22)"
%endif
;    THE MOUNT-OWNED WINDOW HAS NO ROW OF ITS OWN ANY MORE, WHICH MAKES THIS
;    GUARD MATTER MORE RATHER THAN LESS. SKB_DSK was a term and `Disk bufs`
;    was a row, and the row's job was to name the window so that it could not
;    drift back into meaning "the rest of .lowbss". Both are gone: what is
;    left of the window is `dsk_secbuf` alone, 512 bytes, and it accumulates
;    into SKB_IMG with the rest of `.lowbss` (see the SKB_ block above).
;    Folding is honest for 512 bytes and dishonest for 3,584, and the ONLY
;    thing standing between the two is this line - with the row gone there is
;    nothing on the screen that would show a fourth buffer arriving, so a
;    window that grew would be billed to `Code+data` in silence.
;
;    IT IS COMPARED AGAINST AN INDEPENDENT RECOMPUTATION, and not against a
;    symbol derived from the same declaration. This read `%if SKB_DSK !=
;    DSK_WIN_BYTES` for the life of the tree while the SKB_ block defined
;    SKB_DSK as `equ DSK_WIN_BYTES` - so it was `X != X` and no edit could
;    make it true. The exact drift it was written for went straight through
;    it: a fourth `resb` between dsk_win_base and dsk_win_end grows
;    DSK_WIN_BYTES, SKB_DSK followed it, and both sides moved together. A
;    guard that cannot fail is not a weak guard, it is an absent one that
;    reads as present. DSK_WIN_BYTES is the DECLARED span (dsk_win_end -
;    dsk_win_base) and `DSK_OVLPAD + 512` is what the window is supposed to
;    hold, written out from the other end.
;    THE 512 IS A LITERAL ON PURPOSE: spelling it as a symbol that also sizes
;    dsk_secbuf puts the same tautology back one level down.
;    WHAT IT CATCHES (each broken on purpose): a fourth buffer inside the
;    window, a resized dsk_secbuf, a buffer moved out of it, and padding
;    inserted between two of them.
;    WHAT IT STILL MISSES, said rather than assumed: a SAME-SIZE substitution
;    - one buffer swapped for another of the same length - which used to
;    leave the row's arithmetic right and its LABEL wrong, and now leaves
;    `Code+data` right and this comment wrong.
;
;    HOW IT GOT TO ONE SECTOR, because the history is what makes the fold
;    defensible: SPEC.md 25.8 pooled the icon bodies on kern_small, 25.9 took
;    them out of the listing on BOTH kernels into one machine-wide store, and
;    docs/plans/LISTING-HOME-PLAN.md 13 took the ENTRIES and their reference
;    index out too - a listing is written into the store its CALLER keeps, a
;    Disk window's own claim or the Standard File dialog's. So the only thing
;    left in `.lowbss` that belongs to the mount is the SECTOR BUFFER.
;    `DSK_OVLPAD` is the one term here that was never about listing anything:
;    it is the boot overlay's landing ground, 0 on every SHIPPED kernel -
;    kern_big, kern_small and kern_emu alike - and non-zero only under
;    `KERN_KNOB`, where a diagnostic has joined `.ovl` and kern_big's
;    region, which the abolished floor listing left fitting to the byte,
;    needs a rung to spill into (kernel/dskwin.inc). Spelling kern_big's arm
;    as a bare 512 made THIS guard fire on a build whose overlay guard had
;    just been satisfied, which reads as the fix not working rather than as a
;    second place holding the same number.
%if DSK_WIN_BYTES != DSK_OVLPAD + 512
%error "sys_kb: the mount's own scratch is no longer one sector (SPEC.md 2.1.2, 20.9, docs/plans/LISTING-HOME-PLAN.md 13). It is dsk_secbuf, plus dsk_ovlpad where the boot overlay needs a floor - the entries and their reference index live in the caller's store and the icon bodies in the machine-wide one. There is no `Disk bufs` row any more: these bytes are billed to `Code+data`, which is honest for one sector and not for a window that has grown, so give it a term and a row again rather than raising this number"
%endif
;
; 6b. ...and so is every claim in it, which is what a package region rides
;     on now that it is a claim rather than a pool slot: mem_claim rounds to
;     whole KB, so every base it hands out is HEAP_SEG + n*64 paragraphs.
;     MEM_PARA_KB is that 64; if it ever stopped being a multiple of 32 an
;     int 13h read of a package image could straddle a 64KB DMA boundary and
;     the symptom would be "Disk error" on the LARGE packages only.
%if MEM_PARA_KB % 32
%error "a heap claim is not 512-byte aligned - a package image is read into one"
%endif
; 4b. (retired, SPEC.md 2.9.14) the boot overlay had to EXIST, because an
;    empty `.ovl` sent every blob call OVL_AT bytes into the padding between
;    two sections. The overlay's blob half is part of `.boot2` now, so there is
;    no second section to be empty and no split to land beside.
; 5. the boot sector relocates itself to the TOP OF CONVENTIONAL RAM before
;    it reads a sector, and its stack grows down from there (SPEC.md 2.7), so
;    on any given machine the kernel has to end below both. That address is
;    computed from int 12h and is therefore not a constant this file can
;    check - what it CAN check is the machine we claim to support: at
;    MIN_RAM_KB the sector and its stack occupy the top 2,560 bytes, and
;    everything below that is the kernel's to spend.
;
;    This is a POLICY guard, like guard 1 and unlike guard 2 - which is the
;    whole reason the constant it reads is named for the machine rather than
;    for an address. Both kernels answer to 128KB here, and separately from
;    guard 1, because BOOTING on a machine and RESIDING in one are different
;    questions.
;
;    **A KNOB KERNEL IS EXEMPT, for guard 1's reason and not a new one.** It
;    is not what anybody boots; it exists to answer a question about a machine,
;    and every machine this project tests on has 640KB or could have. That does
;    not make a RAM-constrained test impossible - it makes the test state its
;    own constraint instead of borrowing the shipped kernel's.
%ifndef KERN_KNOB
%if KERNEL_SEG*16 + KERN_SIZE + BOOT2_PAD > MIN_RAM_KB*1024 - BOOT_SECT - BOOT_STACK
%error "the kernel cannot BOOT on MIN_RAM_KB - see docs/KERNEL-MEMORY.md"
%endif
%endif
; 5b. ...and BOOT2_PAD is in that sum because STAGE 2 SITS ON THE HEAP'S FLOOR
;    (SPEC.md 2.9.5), which is the kernel's own last paragraph. It is given
;    back at spl_finish and the arena is compacted onto it, so it is not
;    footprint in KERN_BUDGET's sense - but it IS occupied at the same moment
;    the boot sector and its stack are, which is exactly what guard 5 asks
;    about. docs/plans/completed/SETTINGS-COST.md 7.2 is the same shape: a transient the
;    minimum machine still has to be able to hold.
;
; 5c. RETIRED, BECAUSE STAGE 1 NOW ASKS THE QUESTION ITSELF (SPEC.md 2.7.1).
;    It asserted that the blob fits under stage 1's refusal - that a machine
;    stage 1 ACCEPTED has room for stage 2 at HEAP_SEG, or the copy runs off
;    the end of RAM and the symptom is a boot that stops with a half-drawn
;    splash. Stage 1's bound was
;
;      int12h_paragraphs >= KERNEL_SEG + KERNEL_SECTORS*32 + BOOT_STACK/16
;                                      + RELOC_ADJ
;
;    which mentions neither HEAP_SEG nor the blob, so the two had to be
;    reconciled here. That is what made this a real check, and it is also what
;    made it 31KB looser than it needed to be: RELOC_ADJ is stage 1's own
;    addressing offset, not a reservation, and carrying it in the bound
;    refused machines the kernel fits on.
;
;    Stage 1 is now told HEAP_PARA and tests
;
;      int12h_paragraphs >= HEAP_PARA
;                            + ceil((2*BOOT2_PAD + BOOT_SECT + BOOT_STACK
;                                    - B2_TAIL) / 16)
;
;    whose first two terms ARE the left-hand side of this guard, so the guard
;    can only ever pass. An assertion that cannot fail is worse than no
;    assertion: it reads like cover. (The second BOOT2_PAD is stage 2's copy
;    surviving itself - SPEC.md 2.7.1 - which is the part this guard never
;    asked about and the old bound met by accident.)
;
;    THE WORRY IN ITS LAST PARAGRAPH IS THE ONE THING GENUINELY FIXED HERE.
;    It said the slack could be spent by `.lowbss` growing, "the one rung the
;    file does not carry and stage 1 therefore cannot see" - and stage 1 could
;    not, because KERNEL_SECTORS is a FILE size and `.lowbss` is nobits.
;    HEAP_PARA is the ladder's top, so that rung is now in the number stage 1
;    compares against, exactly and by construction.
;
;    What replaces it is a HOST-SIDE row, because what can still go wrong is
;    not arithmetic this file can do: the injected HEAP_PARA going stale
;    against the kernel actually on the disk, or being measured with the wrong
;    knobs (the bug $(BOOTHEAP_DEFS) in the Makefile carries $(VIDDEF) for).
;    Nothing inside this assembly can see the sector's immediate.
;    tests/unit/t_bootfloor.py reads it back out of the built sector and
;    compares it against this ladder.

; =============================================================================
; NOTHING MAY LAND BELOW A SECTION'S OWN END LABEL (SPEC.md 15.1.2).
;
; Every *_SIZE above is `<section>_end - $$`, so a byte emitted into that
; section AFTER its end label is a real byte of the image that the constant
; does not count. That is not a build error anywhere else in this tree:
; `-f bin` places it, `.bss` is `nobits vfollows=.text` so nothing is aliased,
; and os88ovlchk walks SECTIONS and cannot see a same-section, wrong-side-of-
; the-label byte. What under-reports instead is the arithmetic - KTEXT_SIZE,
; KIMG_PARA (the FAT snapshot base), COLD_START, KERN_SIZE, the KERN_CODE_MAX
; guard and SPEC.md 57.6's published kbld_text/kbld_cold - and for `.modc`,
; `.modf` and `.modl` the consequence is worse than arithmetic: os88mod.py cuts
; the module file at <sec>_end, so a byte below it is CUT OFF THE MODULE and a
; call into it lands in unclaimed heap.
;
; Found during kernel size pass 2, where a transform appended a shared epilogue
; ladder to the foot of this file: 9 bytes in `.text` and 9 in `.cold` that
; every size constant missed, with 434 bytes of slack standing between that and
; `.cold` being laid over the tail of `.bss`. Costs nothing: each %if is
; assembly-time and emits no code.
;
; THIS BLOCK MUST STAY THE LAST THING IN THE FILE, and that is a real limit
; rather than tidiness: a `%if` measures the section where it SITS, so anything
; emitted BELOW here is past the check as well as past the label and is not
; seen at all. Measured, batch 7: the same ladder appended after this block
; assembles clean, `stkbalance` stays green at 0 findings, and `.text` and
; `.cold` under-report by 9 and 18 bytes with nothing said. Appended ABOVE it,
; both errors below fire by name. So: add a new section's rung to the list -
; never a line of anything else after the last %endif.
;
; TWELVE LABELS AND NOT TWO. The `.text`/`.cold` pair is where the defect was
; actually found, and it is the cheap half; `.modc` is the one that decides
; the shape of this block, because the same mis-placement there is machine-
; breaking rather than merely under-reported - and a `.modc` ladder is one
; command line away, the section prefix already being in the transform's list.
; =============================================================================
section .text
%if ($ - $$) != KTEXT_SIZE
  %error "something landed in .text below kernel_text_end - KTEXT_SIZE, KIMG_PARA, COLD_START, the KERN_CODE_MAX guard and kbld_text all under-report it"
%endif
section .lowbss
%if ($ - $$) != KLOW_SIZE
  %error "something landed in .lowbss below kernel_low_end - KLOW_SIZE and LOW_PARA under-report it"
%endif
section .vgabuf
%if ($ - $$) != KVGABUF_SIZE
  %error "something landed in .vgabuf below kernel_vgabuf_end - KVGABUF_SIZE under-reports it"
%endif
section .bss
%if ($ - $$) != KBSS_SIZE
  %error "something landed in .bss below kernel_bss_end - KBSS_SIZE and COLD_START under-report it"
%endif
section .boot2
%if ($ - $$) != BOOT2_SIZE
  %error "something landed in .boot2 below boot2_end - BOOT2_SIZE under-reports it and the blob guard is blind to it"
%endif
section .cold
%if ($ - $$) != COLD_SIZE
  %error "something landed in .cold below cold_end - COLD_SIZE, COLD_PARA and kbld_cold under-report it"
%endif
section .ovl
%if ($ - $$) != OVL_SIZE
  %error "something landed in .ovl below ovl_end - OVL_SIZE under-reports it and MODC_START with it"
%endif
section .modc
%if ($ - $$) != MODS_SIZE
  %error "something landed in .modc below mods_end - MOD_SETS would read the settings core short of it and a call into it lands in unclaimed heap"
%endif
section .modu
%if ($ - $$) != MODU_SIZE
  %error "something landed in .modu below modc_end - os88mod.py would CUT CTRL.DRV short of it and a call into it lands in unclaimed heap"
%endif
section .modf
%if ($ - $$) != MODF_SIZE
  %error "something landed in .modf below modf_end - os88mod.py would CUT the format module short of it"
%endif
%ifdef FCP_MOD
section .modp
%if ($ - $$) != MODP_SIZE
  %error "something landed in .modp below modp_end - os88mod.py would CUT the Cut/Copy/Paste module short of it"
%endif
%endif
%ifdef FDLG_MOD
section .modd
%if ($ - $$) != MODD_SIZE
  %error "something landed in .modd below modd_end - os88mod.py would CUT the file dialog module short of it"
%endif
%endif
section .modl
%if ($ - $$) != MODL_SIZE
  %error "something landed in .modl below modl_end - os88mod.py would CUT the clone module short of it"
%endif
%ifdef KERN_BIG
section .modh
%if ($ - $$) != MODH_SIZE
  %error "something landed in .modh below modh_end - os88mod.py would CUT the hibernate module short of it"
%endif
%endif
%ifdef DOCK_OPT
section .modk
%if $ != modk_end
  %error "something landed in .modk after modk_end"
%endif
section .modw
%if $ != modw_end
  %error "something landed in .modw after modw_end"
%endif
%endif
section .modmap
%if ($ - $$) != MODMAP_SIZE
  %error "something landed in .modmap below modmap_end - the split trailer is no longer the last two bytes of the file"
%endif
section .text
