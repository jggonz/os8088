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
; and the KERN_BUDGET guard holds that whole span to 72.5KB just above the
; BIOS data area. Code, data, scratch, the FAT snapshot, the disk buffers and
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
; --- the split (docs/KERN-SPLIT-PLAN.md) -------------------------------------
; KERN_BIG and KERN_SMALL are two builds off this one tree, and the knob is
; `make KERN_BIG=1`. The reason is three budget moves old and is written out
; below and in docs/KERNEL-MEMORY.md: a 128KB machine and a 640KB machine stop
; wanting the same feature set long before they stop fitting the same image,
; so the answer at guard 5's ceiling is a second build rather than a raise.
;
; THE TWO FOOTPRINT GUARDS ARE SEPARATE CONSTANTS AS OF THIS COMMIT AND HOLD
; THE SAME VALUE. That is deliberate and it is not an oversight: separating
; them is the mechanism, and MOVING one is a decision to take with whoever
; asked for the feature that needs it (the fifth move's rule, and every move
; since). So the split costs kern_small nothing today - not a byte, not a
; step - and kern_big's guard can move on its own the first time something
; actually needs it to.
;
; When they do diverge, the one that has to be defended byte by byte is
; KERN_SMALL_BUDGET. kern_big's is headroom for a machine that has RAM.
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

%ifdef KERN_BIG
KERN_BUDGET equ 98304           ; kern_big's FOOTPRINT guard, and the SHIPPED
                                ; one: big is the default build. Free to move
                                ; on its own terms - it has a machine with RAM
                                ; behind it - where KERN_SMALL_BUDGET below is
                                ; the one that has to be defended.
                                ;
                                ; THE SIXTEENTH MOVE, 96,256 -> 98,304, ASKED
                                ; FOR AND GRANTED, and the second that is
                                ; kern_big's alone. 2KB again, for the rest of
                                ; SPEC.md 39's dual display - 39.16's union
                                ; and what follows it - on the fifteenth
                                ; move's terms.
                                ;
                                ; WHAT SPENT THE FIFTEENTH IS WORTH RECORDING,
                                ; because the two rounds landed in the same
                                ; week and the arithmetic reads as one. Dual
                                ; display took 39.12's context, 39.13's second
                                ; card, 39.14's split, 39.15's cursor and
                                ; 39.16's union; the spare it left went to
                                ; SPEC.md 18.96/22.12's floppy FORMAT and
                                ; 11.96.3's per-window raise cache, which are
                                ; other work and are what took the guard from
                                ; two steps to one. A raise is granted for a
                                ; feature, so which feature spent the last one
                                ; is the question the next request has to
                                ; answer, and it is not always the one asking.
                                ;
                                ; THE FIFTEENTH MOVE, 94,208 -> 96,256, AND
                                ; THE FIRST THAT IS kern_big's ALONE. 2KB,
                                ; asked for and granted, for SPEC.md 39's
                                ; dual display (docs/DUAL-DISPLAY-PLAN.md):
                                ; the estimate is 1,400-1,900 bytes and the
                                ; spare had fallen to 1,024 (two steps) after
                                ; the disk round, so the feature no longer
                                ; fitted. Granted at 2KB on the thirteenth
                                ; move's terms - headroom, half a step - and
                                ; NOT at the 4KB that would pre-authorise
                                ; another feature's worth.
                                ;
                                ; What is new is which guard moved. Every one
                                ; of the previous fourteen moved the figure
                                ; the 128KB machine lives under, because there
                                ; was only one. This one does not: KERN_SMALL
                                ; stays at 94,208 and the raise buys room on
                                ; the machine that has RAM, which is the whole
                                ; reason the split exists. The fifth move's
                                ; rule still stands for both - headroom for
                                ; ordinary growth, not an invitation to spend
                                ; it - and kern_small's is now the tighter of
                                ; the two by 2KB, which is the direction it
                                ; should drift from here.
%else
KERN_BUDGET equ 94208           ; the whole kernel's FOOTPRINT. Growing past
                                ; this is not a build detail - see
                                ; docs/KERNEL-MEMORY.md before raising it.
                                ; It has moved fourteen times, every raise asked
                                ; for and granted: 65,536 -> 71,680 for the
                                ; SPEC.md 41 store and the two API surfaces
                                ; that came with it (wm_geom, wm_about_set);
                                ; 71,680 -> 72,704 for the driver subsystem
                                ; (SPEC.md 51) and the Control Panel pages
                                ; that drive it - which BUYS more than it
                                ; spends, the OPL2 and Sound Blaster code it
                                ; makes loadable being thousands of lines that
                                ; would otherwise be resident on a machine
                                ; with neither card; 72,704 -> 76,800 for
                                ; SPEC.md 51.5's keyed SYSTEM.CFG, granted in
                                ; ADVANCE of further work with an optimisation
                                ; pass to follow, so the slack under that one
                                ; was temporary rather than an invitation; and
                                ; 76,800 -> here for the file manager's
                                ; Cut/Copy/Paste, its recursive paste engine
                                ; and the drag (SPEC.md 22.3/22.4), which
                                ; overran the previous figure by 512 bytes
                                ; with the drag still to come. Asked for and
                                ; granted with the 4KB it costs the claim heap
                                ; on every machine named up front - Paint
                                ; gives up one canvas tier for it, and the
                                ; 128KB RAM floor is untouched.
                                ;
                                ; The fifth move is the first DOWNWARD one:
                                ; 80,896 -> 74,240. Every raise above was
                                ; spent and then some of it handed back by the
                                ; optimisation passes that followed - the Task
                                ; Manager leaving for the system disk, the
                                ; Control Panel into a cold segment, the clock
                                ; ladder and the glyph table out of the
                                ; segment - until the kernel sat at 72,192
                                ; with 8,704 bytes of budget above it. Slack
                                ; that large is not headroom, it is the guard
                                ; switched off: anything short of an 8KB
                                ; addition passed without a conversation,
                                ; which is exactly the conversation this
                                ; constant exists to force. 74,240 leaves
                                ; 2,048 bytes - enough that an ordinary bug
                                ; fix does not trip it, small enough that a
                                ; FEATURE does. It returns no
                                ; RAM and is not meant to: HEAP_SEG is
                                ; KERN_END, so the heap has always started
                                ; where the kernel ACTUALLY ends and never
                                ; where the budget said it might - which is
                                ; also why the SIXTH move below costs the
                                ; machine nothing at all. The slack
                                ; was never costing memory - it was costing
                                ; scrutiny, and scrutiny is the only thing
                                ; this constant has ever bought.
                                ;
                                ; The sixth move, 74,240 -> 76,288, is that
                                ; scrutiny working. Two features landed in
                                ; parallel and met at the guard: SPEC.md
                                ; 5.6's gfx_line, the arbitrary-angle line
                                ; primitive (512 bytes, one KIMG_PARA step),
                                ; and the file dialog handing an app the SIZE
                                ; so a load can be refused for free (SPEC.md
                                ; 38.6). Either alone fitted; together they
                                ; overran by 512. Asked for and granted at
                                ; 2KB rather than the 512 the merge needed,
                                ; because the fifth move's stated intent was
                                ; 2,048 bytes of headroom and by the time
                                ; anyone measured it there were 512 - three
                                ; features had spent it without the constant
                                ; being revisited, which is the failure mode
                                ; a stale number in docs/KERNEL-MEMORY.md let
                                ; through. That doc now carries the bisect
                                ; recipe instead of a figure, so the next
                                ; author measures rather than trusts.
                                ;
                                ; The seventh move, 76,288 -> 78,336, was
                                ; asked for and granted in ADVANCE, for two
                                ; plans costed together before either was
                                ; written: SPEC.md 54's file type
                                ; associations (docs/ASSOC-PLAN.md, ~1,600)
                                ; and the disk path (docs/DISK-PERF-PLAN.md,
                                ; ~200), against the 1,536 that were spare,
                                ; which the two do not fit. Granted in
                                ; advance is the fifth move's warning, so the
                                ; terms were stated with it: the raise lands
                                ; with the commit that FIRST needs it and not
                                ; with the documents, and it landed here -
                                ; SPEC.md 54.4's open path had taken the
                                ; footprint to exactly 76,288, which passes
                                ; the guard by a byte and leaves the next
                                ; change nowhere to go. Two phases of the
                                ; association work and the whole of the disk
                                ; work were already spent under the OLD
                                ; figure before this line moved.
                                ;
                                ; Part of what pays for it is elsewhere:
                                ; SPEC.md 18.91's batched transfer is 117
                                ; bytes that took a directory change from 12
                                ; int 13h calls to 5, and mechanism D
                                ; (docs/DISK-PERF-PLAN.md 5.5) removes 8 of
                                ; the 13 an APPS/ open still costs.
                                ;
                                ; The eighth move, 78,336 -> 80,384, is the
                                ; seventh's tail: the association work's own
                                ; bug reports. SPEC.md 54.4.1's notice - a
                                ; document whose program is not on the disk
                                ; now says WHICH program, instead of
                                ; reporting a Task Manager the user never
                                ; asked for - needs a name built at run time,
                                ; and the branding half of that fix costs
                                ; nothing at all: measured, it lands at
                                ; 67,016, byte for byte the size before it.
                                ; The naming half is ~115 bytes and crosses a
                                ; KIMG_PARA step, which is the whole 512, and
                                ; the seventh move's 2,048 had 16 bytes left.
                                ; Asked for and granted with the buffer work
                                ; that came with it and pays part of it back
                                ; in TIME rather than bytes: SPEC.md 18.92's
                                ; diskette parameter table (the batching was
                                ; returning the wrong head's sectors on real
                                ; hardware), 18.93's batched boot read (131
                                ; int 13h calls to 16, ~31 s to 4-5 s on the
                                ; target) and 18.4.2's read-side run
                                ; coalescer (a 116KB load, 244 calls to 34).
                                ; None of those three cost the footprint a
                                ; byte - they all landed inside the image's
                                ; existing 512-byte rounding.
                                ;
                                ; The ninth move, 80,384 -> 82,432, is the
                                ; first one bought for the OTHER guard. The
                                ; file load, file save, file manager and
                                ; file dialog modules went into .cold
                                ; (SPEC.md 2.6), and the two rungs that swap
                                ; do not round the same way: the image loses
                                ; 15.3KB and the cold segment gains it,
                                ; which lands two 512 steps wide. What it
                                ; buys is KERN_CODE_MAX, which had 3,519
                                ; bytes spare and now has 18,811 - both
                                ; measured by bisecting the guards, not
                                ; modelled - against the work that is
                                ; coming. The footprint ends at 80,384 with
                                ; 2,048 spare, which is twice the 1,024 it
                                ; started with: the five modules cost two
                                ; steps between them and the raise gave
                                ; four. Asked for and granted
                                ; on those terms. Note what it does NOT buy:
                                ; a byte of RAM back for the machine, and not
                                ; a byte of footprint either. Cold code is
                                ; resident and counts here exactly like
                                ; .text's - moving a module cold to fix an
                                ; overrun of THIS constant is a no-op that
                                ; looks like a fix (docs/KERNEL-MEMORY.md).
                                ;
                                ; The ninth move landed exactly on guard 5's
                                ; ceiling, and for a while that made this
                                ; constant unraisable: the kernel had to end
                                ; below a boot sector nailed to linear
                                ; 0x15000, which capped the footprint at
                                ; 82,432 whatever this line said. The sector
                                ; is at the top of RAM now (SPEC.md 2.7) and
                                ; guard 5 is a statement about the smallest
                                ; supported machine instead, 126,976 - so a
                                ; tenth move is possible again, and is the
                                ; same decision the nine above were. What is
                                ; NOT available is a tenth move that quietly
                                ; takes the slack: this figure buys scrutiny
                                ; and nothing else, and 44.5KB of it would be
                                ; the fifth move's mistake at five times the
                                ; size.
                                ;
                                ; The tenth move, 82,432 -> 86,528, is the
                                ; first one taken against that room, and it
                                ; is 4KB granted IN ADVANCE for incoming
                                ; quality-of-life work - the same shape as
                                ; moves 3 and 7, which is why the same terms
                                ; come with it. The fifth move settled that
                                ; 2,048 bytes is the right amount of slack:
                                ; enough that an ordinary bug fix does not
                                ; trip the guard, small enough that a FEATURE
                                ; does. This lands at 4,608 - nine 512-byte
                                ; steps - so until the work it was asked for
                                ; arrives the guard is looser than the
                                ; project's own standard, and what the fifth
                                ; move is on record for is that slack of that
                                ; size stops being scrutiny. So: spend it on
                                ; what it was granted for, and if the work
                                ; lands under it, hand the remainder back the
                                ; way move 5 did rather than leaving it for
                                ; the next author to find.
                                ;
                                ; The eleventh move, 86,528 -> 86,016, is the
                                ; second DOWNWARD one and it is that last
                                ; sentence being honoured rather than an
                                ; optimisation pass: SPEC.md 53.6.1's XMS
                                ; desktop stash was REMOVED, and the step it
                                ; cost goes back with it. The feature saved
                                ; the desktop's four planes at the first
                                ; fsx_mode call and wrote them back at exit
                                ; instead of repainting - correct for the
                                ; PIXELS and wrong about the DESKTOP, because
                                ; a bracket takes real time and what is on
                                ; screen behind it is not a constant: the
                                ; menu bar's clock (which the stash path
                                ; already had to redraw, the one admission
                                ; that a snapshot goes stale), the Timer's
                                ; window, a Bounce, any background task that
                                ; paints - and the exclusive app's OWN window,
                                ; whose content is usually what the bracket
                                ; just spent the whole session changing. So
                                ; the stash restored a photograph of a desktop
                                ; that had moved on, and wm_paint_all - which
                                ; asks every window what it looks like NOW -
                                ; was the correct answer all along. Measured
                                ; on the removal alone: 84 bytes off .text +
                                ; .bss (the image rung unmoved) and 190 off
                                ; .cold, whose rung falls 40 -> 39, so it is
                                ; worth 84,480 -> 83,968. SPEC.md 22.9's
                                ; status-line work landed in the same round
                                ; and spent a step, so the tree stands at
                                ; 84,480 with 1,536 spare - one step UNDER
                                ; the fifth move's standard, which is the
                                ; guard doing its job rather than a fault in
                                ; either change. The next feature asks.
                                ; What it does NOT touch is SPEC.md 41: the
                                ; xm_* slots are a published package ABI
                                ; (20.8 rule 4) with the Task Manager and
                                ; sysbench reading them, and this was one
                                ; kernel-side consumer of that store, not the
                                ; store itself.
                                ;
                                ; The twelfth move, 86,016 -> 90,112, is 4KB
                                ; asked for and granted in ADVANCE, on the
                                ; seventh move's terms and the fifth move's
                                ; warning: the project is in a growth phase
                                ; and the raise lands with the commit that
                                ; first needs it. That commit is SPEC.md
                                ; 39.11's adapter switching, which took the
                                ; spare to EXACTLY ZERO - 6 bytes left in the
                                ; image rung and 155 in the cold one, so the
                                ; next byte added to .text anywhere failed the
                                ; build. What it buys immediately is 39.11.4
                                ; (blanking the card the machine has just
                                ; left, so a two-monitor 5150 does not sit
                                ; with a frozen desktop on the tube nobody is
                                ; using) and 31.10's hiding of a Display page
                                ; that has nothing to choose between.
                                ;
                                ; It is granted WITHOUT the usual "and hand
                                ; back what the optimisation pass saves",
                                ; because the plan for the small machine has
                                ; changed shape: the 128KB floor is to be met
                                ; by a SECOND BUILD of this kernel rather than
                                ; by keeping one build inside a figure both
                                ; machines can live with. Once that exists the
                                ; guard here is kern_big's, and the one that
                                ; has to be defended byte by byte is
                                ; kern_small's. Until it exists this is still
                                ; the only guard there is, so the fifth move's
                                ; rule stands unchanged: this is headroom for
                                ; ordinary growth, not an invitation to spend
                                ; 4KB without a conversation.
                                ;
                                ; The thirteenth move, 90,112 -> 92,160, is
                                ; 2KB and the twelfth's story again: the
                                ; spare hit EXACTLY ZERO, and this time from
                                ; two directions at once - SPEC.md 52's
                                ; hard-disk installer arriving on the
                                ; integration branch, and 11.95.1's
                                ; "a window that grew reveals nothing", which
                                ; is 193 bytes of .text and 8 of .bss. Asked
                                ; for and granted at 2KB rather than 4: the
                                ; project is still in active development, so
                                ; the answer is headroom, but half a step of
                                ; it, which puts the guard back within reach
                                ; of ordinary growth (four steps) without
                                ; pre-authorising another feature's worth.
                                ; The fifth move's rule stands, and so does
                                ; the twelfth's note about kern_small: the
                                ; day that second build exists, this figure
                                ; is kern_big's and stops being the one that
                                ; has to be defended.
                                ;
                                ; The fourteenth move, 92,160 -> 94,208, is
                                ; 2KB granted in ADVANCE for SPEC.md 18.94.2's
                                ; finding: a file operation spends over half
                                ; its disk TIME on work the progress widget
                                ; never shows, and the reason is that the
                                ; kernel optimised for SECTORS where the media
                                ; charges for REVOLUTIONS. Measured over one
                                ; install, the payload streams at 5.78 sectors
                                ; per int 13h call and every other phase - the
                                ; BPB, the FAT window, the root scan, the
                                ; subdirectory walks - runs at exactly 1.00,
                                ; because dsk_dirw_next hands out one LBA at a
                                ; time and every caller reads it with cx = 1
                                ; into a single 512-byte buffer. The fixes are
                                ; a per-volume banked BPB (so a fixed disk,
                                ; which cannot be swapped, revalidates once
                                ; ever) and coalescing the directory walks
                                ; into runs, which needs somewhere bigger than
                                ; dsk_secbuf to read into. Granted at 2KB on
                                ; the thirteenth move's terms - headroom, half
                                ; a step - with the batch bracket and its
                                ; sector cache still to come; that one is a
                                ; REFUSABLE heap claim by explicit decision,
                                ; so it costs this figure nothing and a 128KB
                                ; machine can still install, just slowly.
%endif                          ; KERN_BIG

KERN_SMALL_BUDGET equ 94208     ; ...and kern_small's, named separately so it
                                ; can be REPORTED on a big build rather than
                                ; only enforced on a small one. tools/
                                ; kernsplit.py reads both out of the map and
                                ; says what the big build costs over the
                                ; small; without a name for the small figure
                                ; the only way to ask that question is to
                                ; build twice and remember, which is how a
                                ; number goes stale.
                                ;
                                ; It is the figure that has to be DEFENDED.
                                ; kern_big has a machine with RAM behind it;
                                ; this one is the 128KB floor the project was
                                ; written for, and nothing may be added to
                                ; kern_small without the conversation every
                                ; budget move so far has had.

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
MIN_RAM_KB  equ 128             ; the smallest machine os8088 claims to run.
                                ; A POLICY figure exactly like KERN_BUDGET,
                                ; and the same kind of decision: it is not
                                ; the smallest machine that CAN boot (that is
                                ; roughly the kernel's own size plus this
                                ; sector, and it lands you a desktop with a
                                ; heap too small to open anything), it is the
                                ; one the shipped system is claimed to work
                                ; on. Guard 5 turns it into the footprint
                                ; ceiling: 126,976 bytes, against KERN_BUDGET
                                ; 86,528. When the kernel approaches THAT the
                                ; answer is not another raise - it is two
                                ; kernels, a big one and a minimum one, off
                                ; the same tree (docs/KERNEL-MEMORY.md)

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
FAT_PARA    equ DSK_FAT_SECS * 32     ; 512 bytes = 32 paragraphs

STK0_SIZE   equ 1024            ; task 0's stack - the UI task's, and so the
                                ; one every window callback, every menu track
                                ; and every file-dialog interaction runs on.
                                ; 4x its measured 246-byte high-water mark.
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
VIEW_KB     equ 3               ; each cache: 1KB of entries + 2KB of icons

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
                                ; onto the top of .lowbss; guard 3 proves the
                                ; two cannot meet
KERN_END    equ LOW_SEG + LOW_PARA    ; ...and there the kernel stops
KERN_SIZE   equ (KERN_END - KERNEL_SEG) * 16   ; what KERN_BUDGET measures

HEAP_SEG    equ KERN_END        ; the claim heap (SPEC.md 50) starts where
                                ; the kernel ACTUALLY ends, not where a
                                ; budget said it might, and runs to whatever
                                ; int 12h reports. Package regions and data
                                ; claims share it from opposite ends
                                ; (SPEC.md 50.3); nothing up here has a fixed
                                ; address any more

; double buffering (SPEC.md 32) - the back buffer is a heap CLAIM now, so
; there is no BB_SEG constant: bb_init asks for BB_KB and remembers what it
; got, and on a machine that cannot fund it the Control Panel says so.
BB_PLANE_PARA equ 0x960         ; paragraphs per plane (0x9600 = 480 rows x 80)
BB_KB         equ 150           ; 4 planes x 0x9600 bytes, in KB

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
HMA_SEG     equ 0xFFFF          ; the one segment above 1MB: HMA_SEG:0010 is
HMA_MIN_OFF equ 0x0010          ; linear 0x100000 (0xFFFF0 + 0x10) and
HMA_BYTES   equ 0xFFF0          ; HMA_SEG:FFFF is linear 0x10FFEF - the
                                ; highest byte real mode can name at all.
                                ; 65,520 bytes, DATA ONLY: the near model
                                ; pins CS = DS = KERNEL_SEG, so no code ever
                                ; lives up there (SPEC.md 41.3/41.9 rule 3)
XM_HMA_KB   equ 64              ; what a successful cpu_hma_claim takes off
                                ; the xm pool - the HMA is the first 64KB of
                                ; exactly the RAM AH=88h sizes (SPEC.md 2.4)
XM_MAX_BLKS equ 8               ; xm_alloc's fixed block table, entries: a
                                ; bulk store for a handful of large claims,
                                ; not a malloc (SPEC.md 41.5)

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
section .lowbss  nobits vstart=0
section .bss     nobits vfollows=.text valign=1
section .cold    start=COLD_START vstart=0
section .ovl     start=OVL_START vstart=0
section .text

; =============================================================================
; The boot overlay (SPEC.md 2.5): code that runs ONCE, from kmain, and is
; never reachable again.
;
; It is assembled into `.ovl`, which -f bin places at file offset OVL_START -
; the image rung - with its own addresses starting at zero. The boot sector's
; ONE contiguous read therefore lands it exactly at FAT_SEG, the 4,608-byte
; FAT window, which nothing touches until drv_boot calls disk_mount: the LAST
; thing kmain does before the first paint. Every entry below runs before that,
; so the overlay is alive for exactly as long as it is needed and is then
; written over by the volume's FAT. It costs no RAM at all, and - this is the
; point - none of it counts against KERN_CODE_MAX.
;
; NASM emits the gap between .text and OVL_START as zeros, which is why the
; image needs no padding from outside AND why .bss now arrives zeroed.
;
; The contract, and all of it matters:
;   CS = FAT_SEG, DS = KERNEL_SEG, SS = LOW_SEG.
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
section .ovl
ovl_base:
ovl_cpu_detect:     call cpu_detect
                    retf
ovl_cpu_a20:        call cpu_a20_enable
                    retf
ovl_xm_init:        call xm_init
                    retf
ovl_desk_init:      call desk_init
                    retf
ovl_snd_init:       call snd_init
                    retf
ovl_clk_init:       call clk_init
                    retf
section .text

; The debug registry's tags (SPEC.md 57). Two ASCII characters, and each one
; is ALSO the first word of the block it names, so a reader can check that the
; offset it followed landed where it meant to.
DBG_TAG_MOUSE equ 0x4F4D          ; 'MO' - SPEC.md 9.4.2
DBG_TAG_DISK  equ 0x4444          ; 'DD' - SPEC.md 18.94
DBG_TAG_CLOCK equ 0x4B43          ; 'CK' - SPEC.md 37.92
DBG_TAG_VIDEO equ 0x4456          ; 'VD' - SPEC.md 57.4

; =============================================================================
; Fixed entry points
; =============================================================================
cold_entry:
    jmp kmain

    times 0x08 - ($ - $$) db 0   ; 0x03..0x07 are free again: the mouse
                                ; instrument used to have a fixed word of its
                                ; own at 0x0006 and is now an entry in the
                                ; registry at 0x000E (SPEC.md 57)
    jmp near spl_tick           ; 0800:0008 - boot splash tick (SPEC.md 15)

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
; Loaded programs FAR-call these pinned absolute offsets: 8-byte cells at
; 0x0010 + 8n, each one a complete DS switch around a near call into the
; kernel routine named in the comment. The slot order below IS the ABI -
; never reorder.
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
%macro OSAPI_SLOT 1                 ; 8 bytes exactly
    push ds
    push cs
    pop ds
    call %1
    pop ds
    retf
%endmacro

%macro OSAPI_JSLOT 1                ; a cell that defers to a longer stub
    jmp near %1                     ; E9 rel16 = 3 bytes
    times 5 db 0
%endmacro

osapi_table:
    OSAPI_SLOT gfx_lock           ; 0x0010
    OSAPI_SLOT gfx_unlock         ; 0x0018
    OSAPI_SLOT gfx_pixel          ; 0x0020
    OSAPI_SLOT gfx_hline          ; 0x0028
    OSAPI_SLOT gfx_vline          ; 0x0030
    OSAPI_SLOT gfx_fill           ; 0x0038
    OSAPI_SLOT gfx_frame          ; 0x0040
    OSAPI_SLOT gfx_fill_gray      ; 0x0048
    OSAPI_SLOT gfx_xor_rect       ; 0x0050
    OSAPI_SLOT gfx_xor_fill       ; 0x0058
    OSAPI_SLOT font_char          ; 0x0060
    OSAPI_JSLOT api_font_str      ; 0x0068  X: the string is package data
    OSAPI_JSLOT api_font_width    ; 0x0070  X
    OSAPI_JSLOT api_wm_create     ; 0x0078  X: so is the template
    OSAPI_SLOT wm_show            ; 0x0080
    OSAPI_SLOT wm_hide            ; 0x0088
    OSAPI_SLOT wm_front           ; 0x0090
    OSAPI_SLOT wm_content         ; 0x0098
    OSAPI_SLOT wm_obscured        ; 0x00A0
    OSAPI_SLOT task_yield         ; 0x00A8
    OSAPI_SLOT task_sleep         ; 0x00B0
    OSAPI_SLOT osapi_get_ticks    ; 0x00B8
    OSAPI_SLOT osapi_set_color    ; 0x00C0
    OSAPI_SLOT osapi_mouse        ; 0x00C8
    OSAPI_SLOT osapi_srand        ; 0x00D0
    OSAPI_SLOT osapi_rand         ; 0x00D8
    OSAPI_SLOT osapi_snd_caps     ; 0x00E0 - sound (SPEC.md 34): what the PC
    OSAPI_SLOT osapi_snd_tone     ; 0x00E8   speaker can do, a tone, and a
    OSAPI_SLOT osapi_snd_play     ; 0x00F0   clip out of the caller's buffer
    OSAPI_JSLOT api_snd_fm        ; 0x00F8 - FM verbs (SPEC.md 34.2). X: a
                                  ;          patch-load's 11 bytes are the
                                  ;          caller's, and only live while a
                                  ;          sound DRIVER is loaded (51.4)
    OSAPI_JSLOT api_snd_stream    ; 0x0100 - PCM_BG streams (SPEC.md 34.5),
                                  ;          likewise the driver's. Both
                                  ;          answer CF=1 with no driver, which
                                  ;          is the same thing the held cells
                                  ;          they replaced did (SPEC.md 34.5/
                                  ;          34.6). The two numbers are held
                                  ;          rather than reused, which is what
                                  ;          fixes every slot below them
    OSAPI_SLOT wm_sizable         ; 0x0108 - window features (SPEC.md 11.1)
    OSAPI_SLOT wm_fullscreen      ; 0x0110 - fullscreen (SPEC.md 11.2)
    OSAPI_SLOT wm_grow_paint      ; 0x0118 - grow-box restore (SPEC.md 11.1)
    OSAPI_JSLOT api_file_write    ; 0x0120 - files (SPEC.md 18.4/20.3): N,
    OSAPI_JSLOT api_file_read     ; 0x0128   because ES:BX is the data buffer
                                  ;          - and DX:CX its 32-bit count, so
                                  ;          these two are the WHOLE read/write
                                  ;          surface (SPEC.md 18.4.1). DX is
                                  ;          an argument to both and an output
                                  ;          of the read, and the N stub keeps
                                  ;          its hands off it
    OSAPI_JSLOT api_file_delete   ; 0x0130   and the name still has to cross
    OSAPI_JSLOT api_file_rename   ; 0x0138   (two names, this one)
    OSAPI_SLOT osapi_file_dfree   ; 0x0140 - free space on the CALLING
                                  ;          INSTANCE's volume (SPEC.md
                                  ;          19.2.1), which is the only
                                  ;          volume its writes can reach
    OSAPI_SLOT menu_win_set       ; 0x0148 - app menus (SPEC.md 12.2): the
                                  ;          set's segment comes from the
                                  ;          window, so no stub is needed
    OSAPI_JSLOT api_fdlg_open     ; 0x0150 - the Standard File dialog
                                  ;          (SPEC.md 38.6): N, for the
                                  ;          default name
    OSAPI_SLOT osapi_video        ; 0x0158 - runtime screen geometry (39.2)
    OSAPI_JSLOT api_pkg_spawn     ; 0x0160 - worker tasks (SPEC.md 20.6): X,
                                  ;          the ownership fence needs to
                                  ;          know which segment is calling
    OSAPI_SLOT inst_pkg_alive     ; 0x0168
    OSAPI_SLOT wm_clip_set        ; 0x0170 - the clip region (SPEC.md 11.3)
    OSAPI_SLOT wm_clip_clear      ; 0x0178
    OSAPI_SLOT wm_clip_test       ; 0x0180
    OSAPI_SLOT cpu_info           ; 0x0188 - CPU tiers and memory above 1MB
    OSAPI_SLOT xm_caps            ; 0x0190   (SPEC.md 41): each body already
    OSAPI_SLOT xm_alloc           ; 0x0198   answers its SPEC.md 20.3 contract
    OSAPI_SLOT xm_free            ; 0x01A0   exactly, so the slots call
    OSAPI_SLOT xm_copy            ; 0x01A8   straight at them - and xm_copy's
                                  ;          ES:SI is the caller's own choice,
                                  ;          so no X stub is involved either
    OSAPI_SLOT wm_geom            ; 0x01B0 - content size + visibility
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
    OSAPI_JSLOT api_cm_alloc      ; 0x01B8 - the v3 arena (SPEC.md 20.8):
                                  ;          AX = PARAGRAPHS -> AX = segment.
                                  ;          X - the owner fence needs the
                                  ;          caller's segment
    OSAPI_JSLOT api_cm_free       ; 0x01C0 - AX = a base segment you own; X
    OSAPI_SLOT osapi_cm_caps      ; 0x01C8 - AX/DX = largest/total free
                                  ;          PARAGRAPHS, BL = free records
    OSAPI_SLOT wm_resize          ; 0x01D0 - resize a window (SPEC.md 11.1):
                                  ;          BX = win, CX = w, DX = h; lock
                                  ;          held. Retires the last liberty
                                  ;          in docs/PAINT-NOTES.md - an app
                                  ;          writing W_W/W_H itself
    OSAPI_SLOT gfx_blit4          ; 0x01D8 - packed 4bpp block (SPEC.md 5.4):
                                  ;          ES:SI = source, BP = stride,
                                  ;          AX/BX = dest, CX/DX = w/h. ES is
                                  ;          the caller's own choice here, so
                                  ;          no stub is needed
    OSAPI_SLOT wm_about_set       ; 0x01E0 - the app-name pull-down (12.2):
                                  ;          BX = win, SI = your About handler
    OSAPI_SLOT dskw_gone          ; 0x01E8 - RETIRED (SPEC.md 18.4.1/20.8):
                                  ;          this was readbig, the one file op
                                  ;          with no 64KB ceiling. dskw_read
                                  ;          has none either now, so the cell
                                  ;          answers CF=1 / AX = FERR_NAME
                                  ;          rather than being reused - a
                                  ;          shipped slot keeps its contract
    OSAPI_SLOT osapi_gfx_dbuf     ; 0x01F0 - a package's own bb_set (SPEC.md
                                  ;          32): AL = 1 arm / 0 disarm, out
                                  ;          AL = the state before, to hand
                                  ;          back. CF=1 on the wrong adapter
                                  ;          or a heap that cannot fund it
    OSAPI_SLOT gfx_scroll         ; 0x01F8 - vertical scroll blit (SPEC.md
                                  ;          5.5): AX/BX/CX/DX = the rect,
                                  ;          SI = signed dy. The vacated rows
                                  ;          are the caller's to repaint
; --- and from here on, the slots added since ----------------------------------
    OSAPI_JSLOT api_mem_claim     ; 0x0200 - the claim heap (SPEC.md 50.3):
    OSAPI_JSLOT api_mem_free      ; 0x0208   X, same fence as the spawn
    OSAPI_SLOT osapi_mem_avail    ; 0x0210
    OSAPI_SLOT osapi_font_glyphs  ; 0x0218 - the kernel's 8x8 glyph table
                                  ;          (SPEC.md 6): out DX:SI = the
                                  ;          table (it lives in LOW_SEG now -
                                  ;          the one-time amendment at the
                                  ;          body below), AL = first code,
                                  ;          AH = last, CX = bytes per glyph
    OSAPI_SLOT wm_onsize          ; 0x0220 - install the resize negotiator
                                  ;          (SPEC.md 11.1): BX = win, AX =
                                  ;          near proc. The other half of
                                  ;          docs/PAINT-NOTES.md's resize
                                  ;          complaint - wm_resize is the app
                                  ;          asking, this is the app answering
    OSAPI_SLOT osapi_file_here    ; 0x0228 - where the file API's names
                                  ;          resolve (SPEC.md 18.4/19.2)
    OSAPI_SLOT osapi_file_goto    ; 0x0230 - ...and how to put it back
    OSAPI_JSLOT api_mem_regrow    ; 0x0238 - resize a claim you already hold
                                  ;          (SPEC.md 50.3): X, same owner
                                  ;          fence as the claim itself. In
                                  ;          place when the paragraphs above
                                  ;          are free, which is what stops a
                                  ;          grow needing old + new at once
    OSAPI_SLOT wm_title_set       ; 0x0240 - retitle a window and redraw ONLY
                                  ;          its caption (SPEC.md 11.92): BX =
                                  ;          win, AX = the new string (0 = the
                                  ;          bytes W_TITLE names changed in
                                  ;          place). Not an X cell: the string
                                  ;          is read through W_SEG, which is
                                  ;          already the caller's segment
    OSAPI_JSLOT api_drv_task      ; 0x0248 - a DRIVER's worker task (SPEC.md
                                  ;          51.7): AX = a near entry in its
                                  ;          own segment, or 0 = "this IS the
                                  ;          worker, and it is exiting". X,
                                  ;          because the fence is an identity
                                  ;          test on the caller's segment
    OSAPI_JSLOT api_mem_claim_dma ; 0x0250 - a claim an ISA DMA controller can
                                  ;          reach (SPEC.md 50.3): AX = KB,
                                  ;          CX = KB of the HEAD that must not
                                  ;          cross a 64KB physical boundary.
                                  ;          X, the claim's own owner fence.
                                  ;          A separate cell and not a CX on
                                  ;          mem_claim, because every existing
                                  ;          caller passes garbage there and
                                  ;          the failure would be silent
    OSAPI_JSLOT api_font_run      ; 0x0258 - one OPAQUE text run (SPEC.md 6.1):
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
    OSAPI_SLOT wm_top             ; 0x0260 - out BX = the frontmost VISIBLE
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
    OSAPI_SLOT wm_snap            ; 0x0268 - BX = window, AL = 0 clear / non-0
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
    OSAPI_JSLOT api_vol_add       ; 0x0270 - X: a DRVC_DISK driver registers
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
    OSAPI_JSLOT api_vol_del       ; 0x0278 - X: in AL = a volume index this
                                  ;          driver registered. Drops the
                                  ;          zone, and if that volume was the
                                  ;          mounted one falls back to A: with
                                  ;          the write gate shut. Cannot fail
    OSAPI_JSLOT api_vol_mount     ; 0x0280 - X: in AL = a volume index; mount it
                                  ;          and list it (SPEC.md 18.3), which
                                  ;          is what a driver's Mount button
                                  ;          does after osapi_vol_add. out
                                  ;          CF=1 = it is not a readable
                                  ;          FAT12/16 volume. UI-task context
                                  ;          only, like every other file slot
    OSAPI_SLOT osapi_vol_paint    ; 0x0288 - repaint the desktop's drive zones
                                  ;          (SPEC.md 26). A driver that has
                                  ;          just added or dropped a volume
                                  ;          owes the screen this; it takes
                                  ;          the gfx lock itself, so it must
                                  ;          NOT be called from a callback
                                  ;          that already holds it - post it
                                  ;          the way a page click posts a
                                  ;          repaint
    OSAPI_JSLOT api_drv_cfg       ; 0x0290 - X: the driver's own settings blob
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
    OSAPI_SLOT osapi_sys_snapshot     ; 0x0298 - the scheduler AND the instance
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
    OSAPI_SLOT osapi_claim_snapshot   ; 0x02A0 - the claim table (SPEC.md 50.5),
                                  ;          all MEM_MAX records into ES:DI
                                  ;          as CLS_RECSZ triples; out AX =
                                  ;          MEM_MAX. A cell over
                                  ;          mem_claim_get, whole rather than
                                  ;          per record: the memory map walks
                                  ;          every record to draw it and
                                  ;          hashes every record to decide
                                  ;          whether to
    OSAPI_SLOT osapi_sys_kb       ; 0x02A8 - what the KERNEL occupies and what
                                  ;          the heap holds, in KB, into ES:DI
                                  ;          (SK_* below). Every term used to
                                  ;          be an assembly-time constant of
                                  ;          the kernel's own, which is
                                  ;          exactly what a package cannot
                                  ;          have: the kernel's footprint
                                  ;          moves with every build
    OSAPI_JSLOT api_gfx_fill_pat  ; 0x02B0 - a patterned fill (SPEC.md 5):
                                  ;          AX/BX/CX/DX = the rect, SI = 8
                                  ;          row bytes. X, because those eight
                                  ;          bytes are the caller's and
                                  ;          vga_pat_stage reads them through
                                  ;          DS
    OSAPI_SLOT menu_owner         ; 0x02B8 - out BX = the window owning the
                                  ;          menu bar, 0 = Locator. "Am I the
                                  ;          ACTIVE APPLICATION?" - which
                                  ;          wm_top above cannot answer,
                                  ;          because clicking the bare desktop
                                  ;          hands the bar to Locator and
                                  ;          moves nothing in wm_zord. Takes
                                  ;          no lock: a worker may ask
    OSAPI_SLOT fsx_caps           ; 0x02C0 - fullscreen exclusive (SPEC.md
                                  ;          53): AX = the FSXM bitmask this
                                  ;          adapter can set, DL = vid_kind.
                                  ;          Any context - it is how an app
                                  ;          greys its mode menu (SPEC.md 47)
    OSAPI_JSLOT api_fsx_run       ; 0x02C8 - the bracket (SPEC.md 53.1): AX =
                                  ;          near entry, BX = own window, CX =
                                  ;          flags. X - the ownership fence is
                                  ;          inst_pkg_spawn's identity test on
                                  ;          the caller's DS. Does NOT return
                                  ;          until the app's proc does
    OSAPI_SLOT fsx_mode           ; 0x02D0 - a foreign mode + its FSI info
                                  ;          block (SPEC.md 53.4): AL = FSXM
                                  ;          id, ES:DI = the caller's buffer
    OSAPI_SLOT fsx_wait           ; 0x02D8 - frame clock / present (SPEC.md
                                  ;          53.5): AL = 0 tick / 1 retrace;
                                  ;          flushes an armed back buffer
                                  ;          first while the mode is unswitched
    OSAPI_SLOT gfx_line           ; 0x02E0 - an arbitrary-angle line (SPEC.md
                                  ;          5.6): AX/BX = x1/y1, CX/DX =
                                  ;          x2/y2 inclusive, pen in
                                  ;          [gfx_color], lock held. An
                                  ;          axis-aligned pair defers to
                                  ;          gfx_hline / gfx_vline, which stay
                                  ;          the right answer for a long run
    OSAPI_SLOT osapi_arg_file     ; 0x02E8 - the document this instance was
                                  ;          launched to open (SPEC.md 54.5):
                                  ;          out CF=1 none; CF=0 with SI = its
                                  ;          NUL 8.3 name in KERNEL_SEG (read
                                  ;          it through ES), DX = its directory
                                  ;          cluster and BL = its volume, the
                                  ;          pair OSAPI_FILE_GOTO takes.
                                  ;          READ-AND-CLEAR: a second instance
                                  ;          cannot inherit it
    OSAPI_JSLOT api_assoc_set     ; 0x02F0 - X: claim an extension for a
                                  ;          program (SPEC.md 54.5). ES:SI =
                                  ;          3 extension bytes then 8 stem
                                  ;          bytes, both space-padded; out
                                  ;          CF=1 = the tables are full.
                                  ;          Repoints an extension somebody
                                  ;          else claimed - that is the ask,
                                  ;          not an oversight - and marks it
                                  ;          sticky so a header declaration
                                  ;          cannot take it back
    OSAPI_SLOT osapi_boot_ticks   ; 0x02F8 - how long this machine took to
                                  ;          boot, in system ticks (SPEC.md
                                  ;          15.4): the boot sector's first
                                  ;          instruction to the first desktop
                                  ;          frame. 0xFFFF = unknown
    OSAPI_JSLOT api_gfx_linit     ; 0x0300  X: the walk state is package data
                                  ;          (SPEC.md 5.6.7). AX/BX = x1/y1,
                                  ;          CX/DX = x2/y2, ES:DI = a GLS_SZ
                                  ;          block. The walk runs in the
                                  ;          CALLER'S direction - order
                                  ;          matters here, unlike gfx_line
    OSAPI_JSLOT api_gfx_lstep     ; 0x0308  X: draw the walk's next CX pixels
                                  ;          in [gfx_color] and advance it.
                                  ;          N then M is exactly the N+M one
                                  ;          call would have drawn, which is
                                  ;          what lets an erase replay a draw
    OSAPI_SLOT gfx_pen_cf         ; 0x0310 - CF = 0 live / 1 disabled, and it
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
    OSAPI_JSLOT api_gfx_lstepv    ; 0x0318  X: gfx_lstep for CX walks at once
                                  ;          (SPEC.md 5.6.8). ES:DI = an array
                                  ;          of `dw block, pixels` pairs. Same
                                  ;          pixels as CX separate calls, one
                                  ;          arriving instead of CX of them
    OSAPI_SLOT clip_put           ; 0x0320 - the system clipboard (SPEC.md
                                  ;          55): ES:SI = text, CX = bytes
                                  ;          (0 = empty it); out CF=1 refused.
                                  ;          ES:SI and not the caller's DS
                                  ;          because a document is usually a
                                  ;          heap claim of its own, not the
                                  ;          package's image
    OSAPI_SLOT clip_get           ; 0x0328 - ES:DI = the caller's buffer, CX =
                                  ;          its capacity; out CF=1 empty,
                                  ;          else AX = the whole length and
                                  ;          CX = the bytes copied
    OSAPI_SLOT clip_size          ; 0x0330 - out CF=1 and AX=0 when empty,
                                  ;          else AX = the length. What a
                                  ;          paste asks BEFORE it makes room
    OSAPI_SLOT evq_pending        ; 0x0338 - out AX = events still queued.
                                  ;          "Is there another one of these
                                  ;          right behind me?", so a handler
                                  ;          can drop a redraw it is about to
                                  ;          be asked to do again (SPEC.md
                                  ;          13.4)
    OSAPI_JSLOT api_file_write_sys ; 0x0340 - N, and the ONE cell in this table
                                  ;          a package may not call: dskw_write
                                  ;          for a file that belongs to the
                                  ;          KERNEL (SPEC.md 19.6.1). Fenced on
                                  ;          the caller being a loaded DRIVER,
                                  ;          so 19.6's rule - a package cannot
                                  ;          make a file the user can neither
                                  ;          see nor delete - stands unchanged
    OSAPI_JSLOT api_file_find     ; 0x0348  X: ES:DI is the caller's buffer.
                                  ;         List the current directory by
                                  ;         ORDINAL (SPEC.md 19.7.1) - the
                                  ;         one file operation the API had no
                                  ;         way to express, so a package could
                                  ;         read a file by name and never find
                                  ;         out what was there
    OSAPI_JSLOT api_file_append   ; 0x0350  N: SI = name, ES:BX = bytes, CX =
                                  ;         count. Add to the END of a file
                                  ;         (SPEC.md 18.4.4). Its precondition
                                  ;         is the file's current size being a
                                  ;         whole number of clusters, which is
                                  ;         what a chunked write already is
    OSAPI_JSLOT api_file_read_at  ; 0x0358  N: ...and the read half. DX:AX =
                                  ;         the byte offset, CX = capacity;
                                  ;         out DX:AX = bytes delivered, 0 at
                                  ;         the end. Stateless, so a copy loop
                                  ;         may write between two reads
    OSAPI_JSLOT api_file_mkdir    ; 0x0360  N: SI = name. Create a folder in
                                  ;         the current directory (SPEC.md
                                  ;         18.5). The routine is the file
                                  ;         manager's own - its three callers
                                  ;         were all cold-segment, so it had
                                  ;         never needed a .text thunk, which
                                  ;         is the whole reason it looked
                                  ;         unpublished
    OSAPI_SLOT ui_reboot_post     ; 0x0368  no arguments, no answer: POST a
                                  ;         restart, which ui_task spends with
                                  ;         no lock held (SPEC.md 20.10). The
                                  ;         System menu's Restart, reachable
                                  ;         from a callback - which cannot do
                                  ;         it inline, because that path takes
                                  ;         the gfx lock the caller is holding
                                  ;         and waits on workers that need to
                                  ;         be scheduled
    OSAPI_SLOT osapi_file_goto_q  ; 0x0370  DX = folder cluster, BL = volume.
                                  ;         GOTO's quiet twin (SPEC.md 19.2.2):
                                  ;         same volume = a word, another one =
                                  ;         a quiet mount. For a caller about to
                                  ;         read or write BY NAME rather than to
                                  ;         list - which is every copy loop
    OSAPI_SLOT wm_saveu           ; 0x0378 - BX = window, AL = 0 clear / non-0
                                  ;          set. "My content does not change
                                  ;          while I am not drawing", which
                                  ;          lets the raise cache put its old
                                  ;          pixels back instead of calling
                                  ;          W_PAINT (SPEC.md 11.96.1)
    OSAPI_SLOT toast_show         ; 0x0380 - ES:SI = a NUL line, CX = ticks to
                                  ;          live (0 = ~3s). Says it in the
                                  ;          menu bar and takes it down on its
                                  ;          own (SPEC.md 60). An EMPTY string
                                  ;          retires whatever is up. ES:SI for
                                  ;          clip_put's reason: the text is
                                  ;          often not in the caller's image
    OSAPI_SLOT dsk_batch_begin    ; 0x0388  no arguments, no answer. "The
                                  ;         interface is frozen and the disk is
                                  ;         the same disk" (SPEC.md 18.9.3), so
                                  ;         a floppy may reuse its banked BPB
                                  ;         instead of re-reading LBA 0 at every
                                  ;         volume switch. Nests
    OSAPI_SLOT dsk_batch_end      ; 0x0390  ...and the other end. Optional: any
                                  ;         gfx_unlock ends the batch anyway,
                                  ;         which is what makes an unclosed one
                                  ;         impossible rather than merely rare
    OSAPI_SLOT wm_destroy         ; 0x0398  BX = a window of YOURS; the gfx lock
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
osapi_table_end:                  ; 0x03A0

; build-time assertions: the table's start and span are ABI, prove them here
OSAPI_TABLE_OFF equ osapi_table - $$
OSAPI_TABLE_LEN equ osapi_table_end - osapi_table
%if OSAPI_TABLE_OFF != 0x0010
%error "os8088 API jump table must start at offset 0x0010"
%endif
%if OSAPI_TABLE_LEN != 114 * 8
%error "os8088 API jump table must be exactly 114 8-byte slots"
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
%ifdef DISK_COUNTERS
    dw DBG_TAG_DISK, dsk_dbg_blk    ; SPEC.md 18.94 - `make DISKCNT=1` only
%endif
    dw 0                            ; end of list

; The three snapshot cells above (0x0298..0x02A8) each fill a buffer the
; CALLER owns, and their layouts are ABI like the slot numbers themselves.
; They are declared with the tables they copy - SS_*/SSI_* in instance.inc,
; CLS_* and SK_* in memory.inc - because every one of those layouts is
; derived from MAX_TASKS, INST_MAX or MEM_MAX, and a constant belongs beside
; the table it measures. All of them are mirrored in apps/os88api.inc.
;
; =============================================================================
; The stubs the X and N cells jump to (SPEC.md 20.3). Each ends in retf and
; restores every segment register it borrowed.
; =============================================================================

; X: ES = the caller's DS, so the kernel routine can reach package data
%macro OSAPI_XSTUB 2
%1:
    push ds                     ; the caller's DS...
    push es                     ; ...and its ES
    push ds
    pop es                      ; ES = the caller's DS
    push cs
    pop ds                      ; DS = KERNEL_SEG
    call %2
    pop es
    pop ds
    retf
%endmacro

    OSAPI_XSTUB api_font_str,   font_str_x
    OSAPI_XSTUB api_font_run,   font_run_x
    OSAPI_XSTUB api_font_width, font_width_x
    OSAPI_XSTUB api_wm_create,  wm_create
    OSAPI_XSTUB api_pkg_spawn,  inst_pkg_spawn
    OSAPI_XSTUB api_mem_claim,  osapi_mem_claim
    OSAPI_XSTUB api_mem_claim_dma, osapi_mem_claim_dma
    OSAPI_XSTUB api_mem_free,   osapi_mem_free
    OSAPI_XSTUB api_cm_alloc,   osapi_cm_alloc
    OSAPI_XSTUB api_cm_free,    osapi_cm_free
    OSAPI_XSTUB api_mem_regrow, osapi_mem_regrow
    OSAPI_XSTUB api_snd_fm,     osapi_snd_fm_x
    OSAPI_XSTUB api_drv_task,   drv_task
    OSAPI_XSTUB api_snd_stream, osapi_snd_stream
    OSAPI_XSTUB api_gfx_linit,  gfx_linit
    OSAPI_XSTUB api_gfx_lstep,  gfx_lstep
    OSAPI_XSTUB api_gfx_lstepv, gfx_lstepv
    OSAPI_XSTUB api_vol_add,    osapi_vol_add
    OSAPI_XSTUB api_vol_del,    osapi_vol_del
    OSAPI_XSTUB api_vol_mount,  osapi_vol_mount
    OSAPI_XSTUB api_drv_cfg,    osapi_drv_cfg
    OSAPI_XSTUB api_gfx_fill_pat, osapi_gfx_fill_pat
    OSAPI_XSTUB api_fsx_run,    fsx_run
    OSAPI_XSTUB api_assoc_set,  osapi_assoc_set

; N: the name at the caller's DS:SI is staged into kernel scratch first,
; because ES:BX belongs to the caller's data buffer and cannot carry it.
;
; The optional third argument is V - "resolve this in the CALLING INSTANCE's
; directory" (SPEC.md 19.2.1). It goes on every cell that resolves a file
; name and on nothing else: api_fdlg_open uses this macro too and must NOT
; have it, because fdlg_home_go is the routine that decides where a DIALOG
; opens and a volume switch underneath it would pre-empt that decision.
%macro OSAPI_NSTUB 2-3 0
%1:
    push ds
    push si
    push di
    push es
    push cs
    pop es                      ; ES = KERNEL for the copy destination
    mov di, api_name
    call api_copyname           ; caller DS:SI -> ES:DI, at most 13 bytes
    pop es                      ; the caller's ES back: it is the buffer
    pop di                      ; and its DI, which fdlg_open needs as an
                                ; input (the completion proc's offset)
    push cs
    pop ds                      ; DS = KERNEL
%if %3
    call inst_vol_enter         ; this instance's own folder (SPEC.md 19.2.1);
%endif                          ; preserves every register and the flags
    mov si, api_name
    call %2
    pop si
    pop ds
    retf
%endmacro

    OSAPI_NSTUB api_file_write,  dskw_write,  1
    OSAPI_NSTUB api_file_read,   dskw_read,   1
    OSAPI_NSTUB api_file_delete, dskw_delete, 1
    OSAPI_NSTUB api_fdlg_open,   fdlg_open       ; NO V - see the macro
    OSAPI_NSTUB api_file_append, dskw_append, 1
    OSAPI_NSTUB api_file_read_at, dskw_read_at, 1
    OSAPI_NSTUB api_file_mkdir,  dskw_mkdir,  1

; -----------------------------------------------------------------------------
; api_file_find - slot 0x0348 (X). in CX = ordinal, ES:DI = a DSK_FIND_SZ
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
api_file_find:
    push ds
    push si
    push bx
    mov bx, ds                  ; the caller's segment, for the fence
    push cs
    pop ds                      ; DS = KERNEL
    call inst_vol_enter         ; this instance's own folder; preserves
                                ; everything including the flags
    call drv_owns_seg           ; CF = 0: a loaded driver, so it may see the
    mov al, 0                   ; system files it is going to have to copy
    jc .nothid
    mov al, 1
.nothid:
    pop bx
    call dsk_find
    pop si
    pop ds
    retf

; -----------------------------------------------------------------------------
; api_file_write_sys - slot 0x0340, and the ONE fenced cell (SPEC.md 19.6.1)
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
; carries header version 4 which ld_check_hdr refuses for an application, and
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
; -----------------------------------------------------------------------------
api_file_write_sys:
    push ds
    push si
    push di
    push es
    push bx                     ; the caller's buffer offset: live, and
    mov bx, ds                  ; drv_owns_seg wants a register
    push ds                     ; ...AND THE CALLER'S DS IS KEPT, because
    push cs                     ; api_copyname below reads the name through
    pop ds                      ; it. The fence needs DS = KERNEL to reach
    call drv_owns_seg           ; drv_tab, and the first version of this stub
    pop ds                      ; switched and never switched back - so it
                                ; staged 13 bytes of KERNEL image as the file
                                ; name and handed that to dskw_write_sys.
                                ; OSAPI_NSTUB avoids it by copying BEFORE it
                                ; touches DS; this one has to put it back
    pop bx
    jc .refuse
    push cs
    pop es                      ; ES = KERNEL for the copy destination
    mov di, api_name
    call api_copyname           ; caller DS:SI -> ES:DI, at most 13 bytes
    pop es                      ; the caller's ES back: it is the buffer
    pop di
    push cs
    pop ds                      ; DS = KERNEL
                                ; NO V (SPEC.md 19.2.1): the installer names
                                ; the volume it is building and must not have
                                ; its own instance's folder put underneath it
    mov si, api_name
    call dskw_write_sys
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

; ...and the two-name case, which needs DI as well and so is written out
api_file_rename:
    push ds
    push si
    push di
    push es
    push cs
    pop es                      ; ES = KERNEL
    push di                     ; bank the new-name pointer across the first
    mov di, api_name            ; copy, which needs DI itself
    call api_copyname           ; old name
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
    call dskw_rename
    pop di
    pop si
    pop ds
    retf

; -----------------------------------------------------------------------------
; api_copyname - stage a NUL 8.3 name across the segment boundary
; in:  DS:SI = the caller's name, ES:DI = kernel scratch (13 bytes)
; out: nothing (all registers preserved); the copy is NUL-terminated even if
;      the source was not - a package cannot make this run on

; --- resident shims the overlay far-calls (see the contract above) ----------
; Four bytes each. A routine gets one only because an overlay entry needs it
; and it has to stay resident for its own reasons: xm_arm because xm_copy
; re-arms unreal mode inside the window that uses it, dsk_vol_slot because
; every zone painter calls it on every repaint.
ovw_xm_arm:         call xm_arm
                    retf
ovw_dsk_vol_slot:   call dsk_vol_slot
                    retf
ovw_desk_rowcalc:   call desk_rowcalc
                    retf

; ...and the clock's five. Each is a port helper the READ path (overlay) and
; the WRITE path (resident, because the Control Panel can set the clock all
; session) both use, so it cannot move and cannot be duplicated without the
; two halves drifting apart.
ovw_clk_at_get:     call clk_at_get
                    retf
ovw_clk_at_done:    call clk_at_done
                    retf
ovw_clk_ns_put:     call clk_ns_put
                    retf
ovw_clk_ns_stamp:   call clk_ns_stamp
                    retf
ovw_clk_rp_get:     call clk_rp_get
                    retf
; -----------------------------------------------------------------------------
api_copyname:
    push ax
    push cx
    push si
    push di
    cld
    mov cx, 13
.c:
    lodsb
    stosb
    or al, al
    jz .done
    loop .c
    mov byte [es:di-1], 0
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

api_name:   times 13 db 0       ; staged names (.text, not .bss: the file
api_name2:  times 13 db 0       ; slots are reachable before anything clears
                                ; .bss, and -f bin clears nothing)

; =============================================================================
; Boot (SPEC.md 15)
; =============================================================================
kmain:
    cli
    mov ax, KERNEL_SEG          ; the boot sector jumped here with its own
    mov ds, ax                  ; segments; setting ours up is our job
    mov es, ax
    mov ax, LOW_SEG             ; SS is NOT KERNEL_SEG (SPEC.md 2.1): the task
    mov ss, ax                  ; stacks sit in their own segment just above
    mov sp, STK0_TOP            ; the image, so a stack offset stays small and
    sti                         ; the kernel's own 64KB window stays for code
    cld

    call dsk_boot_from          ; WHICH VOLUME DID WE COME OFF? (SPEC.md
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

    call FAT_SEG:ovl_cpu_detect ; CPU tier + memory above 1MB (SPEC.md 41),
                                ; here and nowhere else: BEFORE sched_init,
                                ; because this is the last moment at which no
                                ; kernel ISR is installed - the unreal-mode
                                ; window inside xm_init runs with CR0.PE set
                                ; and a real-mode IVT, so the only handlers
                                ; that may fire in it are the BIOS's own, and
                                ; a tick lost here costs nothing ([ticks] is
                                ; zeroed by sched_init anyway)
    call FAT_SEG:ovl_cpu_a20    ; ...and VERIFY it: the feature bit is set by
                                ; the wraparound probe, never by the poke
                                ; (SPEC.md 41.2). A no-op on tier 0 - an 8088
                                ; has no gate and port 0x92 belongs to
                                ; something else there
    call FAT_SEG:ovl_xm_init    ; size the store (int 15h AH=88h, on task 0
                                ; per SPEC.md 7), claim the HMA, arm unreal
                                ; mode on tier 2, publish [xm_kb] LAST

    call dsk_dpt_init           ; int 1Eh becomes ours (SPEC.md 18.92) before
                                ; any transfer: the ROM's EOT is 8, and every
                                ; multi-sector read past it silently returns
                                ; the OTHER HEAD's sectors
    call sched_init             ; pre-emption live from here on
    call evq_init
    call FAT_SEG:ovl_clk_init   ; system clock (SPEC.md 37): probe the RTC,
                                ; or fall back to the fixed date - before the
                                ; mode set, so the very first menu bar paint
                                ; already carries a valid clock
    call vid_init               ; video adapter (SPEC.md 39): probe, publish
                                ; the runtime geometry. Re-runs what the
                                ; splash already did, EXCEPT the mode set -
                                ; the loading screen stays up and keeps
                                ; ticking until spl_finish below (15.3)
%ifdef KERN_BIG
    call vid_ctx_init           ; ...and bank that geometry as display 0's
                                ; (SPEC.md 39.12). AFTER vid_apply and never
                                ; FROM it: vid_apply runs from the splash while
                                ; the rest of the kernel is still coming off
                                ; the floppy, and a call from there into
                                ; vidsel.inc executes what has not loaded yet
%endif
    call vid_probe_avail        ; ...and which OTHER adapters this machine has
                                ; (SPEC.md 39.11.1). AFTER the mode is set, and
                                ; that is the whole correctness argument: a VGA
                                ; in mode 12h decodes A000 only, so B000 and
                                ; B800 are free for a second card to answer at.
                                ; Probed before the mode set, a VGA whose BIOS
                                ; came up in mono text answers at B000 as
                                ; ITSELF and reports a Hercules that is not
                                ; there
%ifdef KERN_BIG
    call vid_disp_init          ; ...and if it has BOTH mono cards, programme
                                ; the second one too (SPEC.md 39.13). Here
                                ; because [vid_avail] is what decides, so this
                                ; is the earliest it can run; it claims nothing
                                ; and draws nothing, so the second monitor comes
                                ; up scanning our raster and black
%endif
    call mem_init               ; the claim heap (SPEC.md 50): int 12h, the
                                ; empty map. FIRST of the memory users -
                                ; every claim below goes through it
    call bb_init                ; back buffer (SPEC.md 32): can this ADAPTER
                                ; double-buffer? The memory question is asked
                                ; of the heap when the buffer is armed
%ifdef BAKED_FONT
    call FAT_SEG:ovl_font_init  ; the typeface this BUILD carries (SPEC.md
                                ; 6.2), out of the overlay - so it needs no
                                ; int 10h and no F000:FA6E, and the machine's
                                ; own ROM font is not consulted at all
%else
    call font_init              ; needs int 10h, so after the mode is set
%endif
    call wm_init
    call menu_init              ; menu bar owner (SPEC.md 12): Locator, so
                                ; the first wm_paint_all already has a bar
    call inst_init              ; instance table (SPEC.md 29) - clean boot:
                                ; no app instances exist until launched
    call spl_step               ; a notch: the mode set and the font are done
    call mouse_init             ; IRQ4 live; cursor stays hidden until shown
    call spl_step               ; ...and another: the serial reset holds
                                ; DTR/RTS low for MOU_RSTLOW ticks (~165ms),
                                ; which is the only non-I/O phase up here
                                ; long enough to see (SPEC.md 15.3)
    call FAT_SEG:ovl_desk_init  ; volume zones for the desktop (SPEC.md 26.1)
    call dock_init              ; dock strip scratch (SPEC.md 30)
    call files_init             ; Disk module state (no window at boot)
    call loader_init            ; package loader state
    call drv_init               ; the driver table (SPEC.md 51) - BEFORE
                                ; snd_init, whose tone route reads the
                                ; published service table on its first tick
    call FAT_SEG:drv_snd_sniff  ; is there an FM chip at 388h? (SPEC.md
                                ; 51.3.1) If so, row 0 becomes WANTED by
                                ; DEFAULT - which a SYSTEM.CFG that says
                                ; otherwise then overwrites, so this is only
                                ; ever the answer on a machine that has never
                                ; been asked. HERE and not inside drv_boot,
                                ; because the overlay this lives in is dead by
                                ; then: drv_boot's own mount writes over it
    call FAT_SEG:ovl_snd_init   ; sound layer (SPEC.md 34.7): saves the 61h
                                ; boot bits, stores its .bss state, publishes
                                ; snd_live LAST - snd_tick has been running
                                ; gated since sched_init hooked int 08h

    call spl_step               ; a notch, and the last one kmain spends by
                                ; hand: everything below is sectors, and
                                ; dsk_xfer ticks the bar itself (SPEC 15.3)

    call drv_boot               ; ...and load what SYSTEM.CFG asks for
                                ; (SPEC.md 51.3). Before the first paint, so
                                ; a machine whose sound driver loads has
                                ; sound from the first frame; nothing here
                                ; can stop the boot. NOTHING loads that the
                                ; settings file did not ask for - a driver is
                                ; several seconds of floppy on this machine

    call spl_finish             ; the bar to 100% and the screen handed back:
                                ; the paint below covers every pixel of it,
                                ; so the loading screen needs no erase

    call gfx_lock
    call wm_paint_all
    call gfx_unlock

    ; --- stop the boot timer (SPEC.md 15.4) ----------------------------------
    ; HERE, not after cursor_show: the question is when the first desktop
    ; FRAME is finished, and gfx_unlock is what puts it on the glass - it
    ; flushes the back buffer where there is one, so this is the same instant
    ; on all three adapters. The cursor is not the desktop.
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

    call drv_notice             ; ...and only NOW say what did not load: a
                                ; window needs a screen that has been painted

    jmp ui_task                 ; task 0 becomes the UI task; never returns

; =============================================================================
; osapi helpers (SPEC.md 20.4) - tiny accessors for loaded programs, reached
; only through the jump table. Each preserves all registers except its
; documented outputs.
; =============================================================================

; ---- osapi_boot_ticks - out: AX = the boot timer (SPEC.md 15.4) --------------
; System-tick units, 18.2065 Hz, from the boot sector's first instruction to
; the first desktop frame being on the glass. 0xFFFF = the boot sector never
; stamped it, which is an image built before the timer existed.
osapi_boot_ticks:
    mov ax, [boot_ticks]
    ret

; ---- osapi_get_ticks - out: AX = [ticks] -------------------------------------
osapi_get_ticks:
    mov ax, [ticks]
    ret

; ---- osapi_set_color - in: AL -> [gfx_color] ---------------------------------
osapi_set_color:
    mov [gfx_color], al
    ret

; ---- osapi_mouse - out: CX = [mouse_x], DX = [mouse_y], AL = [mouse_btn] -----
; A package's tracking loop spins on this and does not return until the button
; comes up, exactly as menu_track / ui_drag / ui_grow do - so on a machine with
; no mouse it is a loop the keyboard has to be serviced from, or the button the
; keyboard latched can never be released and the UI task never comes back
; (SPEC.md 9.6.1). One compare on every machine that has a mouse.
osapi_mouse:
    cmp byte [mou_seen], 0
    jne .live
    call kbm_poll
.live:
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    mov al, [mouse_btn]
    ret

; ---- osapi_srand - in: AX -> [osapi_seed] -------------------------------------
osapi_srand:
    mov [osapi_seed], ax
    ret

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
    mov si, font_glyphs
    mov dx, LOW_SEG
    mov al, FONT_FIRST
    mov ah, FONT_LAST
    mov cx, 8
    ret

; ---- osapi_file_dfree - free space where this app's writes will land -------
; The same V as the three name cells (SPEC.md 19.2.1), and for a sharper
; reason than symmetry: an app asks this to find out whether its save will
; fit, so an answer about a volume other than the one the save is going to
; is not merely stale, it is about the wrong disk.
osapi_file_dfree:
    call inst_vol_enter
    jmp dskw_dfree              ; a tail call: its outputs and CF are ours

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
; -----------------------------------------------------------------------------
; DX and BL are the outputs; SPEC.md 1 makes everything else this routine's
; to preserve, BH and AX included - so the slot walks the side table through
; SI rather than through the BX it is about to answer in.
osapi_file_here:
    push cx
    push si
    call inst_caller            ; DH = the calling instance; DX is an output,
    mov si, dx                  ; so both halves of it are ours to spend
    mov cl, 8
    shr si, cl                  ; SI = the slot (8086: shifts go through CL)
    cmp si, INST_MAX
    jae .global
    mov bl, [inst_fdrv+si]      ; the instance's drive...
    shl si, 1
    mov dx, [inst_fcwd+si]      ; ...and its directory
    pop si
    pop cx
    ret
.global:
    mov dx, [dsk_cwd]           ; no instance behind this call: the machine's
    mov bl, [disk_drive]        ; own position, exactly as before
    pop si
    pop cx
    ret

osapi_file_goto:
    push ax
    mov ax, dx
    mov dl, bl
    call dsk_chdir              ; CF = the mount failed; it has already put
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
osapi_file_goto_q:
    push dx
    cmp bl, [disk_drive]
    jne .full
    or dx, dx
    jz .quiet                   ; 0 is the root, always legal
    cmp dx, 2
    jb .bad
    cmp dx, [dsk_maxclus]
    ja .bad
.quiet:
    mov [dsk_cwd], dx
    pop dx
    xor ax, ax
    clc
    ret
.bad:
    mov ax, FERR_IO
    pop dx
    stc
    ret
.full:
    push dx
    mov dl, bl
    pop ax                      ; AX = the cluster, DL = the volume
    call dsk_chdir_q
    jc .ferr
    pop dx
    xor ax, ax
    clc
    ret
.ferr:
    mov ax, FERR_NODISK
    pop dx
    stc
    ret

; ---- osapi_video - the screen the program actually got (SPEC.md 39.2) --------
; out: AX = width, BX = height, CX = first row the dock owns (so the usable
;      desktop is rows MBAR_H..CX-1), DL = 0 VGA / 1 Hercules / 2 CGA,
;      DH = bits per pixel, 4 or 1
osapi_video:
    mov ax, [vid_w]
    mov bx, [vid_h]
    mov cx, [vid_dock_y0]
    mov dl, [vid_kind]
    mov dh, 4
    cmp byte [vid_mono], 0
    je .r
    mov dh, 1
.r:
    ret

osapi_seed:  dw 0                ; PRNG state (inline data: .bss takes no init)

; -----------------------------------------------------------------------------
%include "viddet.inc"           ; video adapters (SPEC.md 39): the splash
                                ; probes and sets the mode on its first tick,
                                ; so this must be resident with it
%include "splash.inc"           ; must be resident within the image's opening
                                ; SPL_RESIDENT sectors (SPEC.md 15)
%include "vidsel.inc"           ; which adapters the machine HAS, and moving
                                ; between them at run time (SPEC.md 39.11).
                                ; AFTER splash.inc and not beside viddet.inc,
                                ; because nothing here is reachable from the
                                ; splash and everything above it eats that
                                ; module's residency budget
%include "cpudet.inc"           ; CPU tiers + the A20 line (SPEC.md 41.1-41.3)
%include "xmem.inc"             ; memory above 1MB (SPEC.md 41.4/41.5): after
                                ; cpudet.inc, whose tier and feature bits it
                                ; branches on and whose cpu_hma_claim it calls
%include "vga12.inc"
%include "vgabb.inc"
%include "font.inc"
%include "mouse.inc"
%include "sched.inc"
%include "events.inc"
%include "clock.inc"            ; the system clock (SPEC.md 37): after
                                ; sched.inc, whose [ticks] it advances from
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
%include "toast.inc"          ; the transient one-line message (SPEC.md 60):
                              ; the bar's other tenant, beside fprog.inc for
                              ; the same reason and after it, because
                              ; toast_show refuses while that widget is armed
%include "ui.inc"
%include "apps.inc"
%include "assoc.inc"          ; file type associations (SPEC.md 54): the
                              ; tables and the icon composition. BEFORE
                              ; disk.inc, whose harvest calls into it
%include "disk.inc"
%include "diskw.inc"          ; the FAT write path (SPEC.md 18.4): after
                                ; disk.inc, whose constants and layout it uses
%include "loader.inc"
%include "files.inc"
%include "filecp.inc"        ; Cut/Copy/Paste and the recursive paste
                                ; engine (SPEC.md 22.3) - after files.inc,
                                ; whose fm_* it reads
%include "fdlg.inc"             ; the Standard File dialog (SPEC.md 38)
%include "icons.inc"
%include "desk.inc"
%include "dock.inc"
%include "ctrl.inc"
%include "driver.inc"           ; loadable drivers (SPEC.md 51): after
                                ; diskw (it reads and writes the system disk)
                                ; and memory (a driver image is a claim)
%include "snd.inc"              ; the sound layer (SPEC.md 34): PC speaker
%include "fsx.inc"              ; fullscreen exclusive (SPEC.md 53): after
                                ; sched.inc (the freeze bytes), instance.inc
                                ; (the fence), snd.inc (the release walk)
                                ; and viddet.inc (the mode leaves)

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
; =============================================================================
cw_app_launch:          call app_launch
                    retf
cw_assoc_post:          call assoc_post
                    retf
cw_bb_set:              call bb_set
                    retf
cw_clk_fld_adj:         call clk_fld_adj
                    retf
cw_clk_fld_str:         call clk_fld_str
                    retf
cw_clk_snapshot:        call clk_snapshot
                    retf
cw_disk_mount:          call disk_mount
                    retf
cw_disk_read:           call disk_read
                    retf
cw_disk_write:          call disk_write
                    retf
cw_drv_cfg_save:        call drv_cfg_save
                    retf
cw_drv_cls_svc:         call drv_cls_svc
                    retf
cw_drv_cp_call:         call drv_cp_call
                    retf
cw_drv_cp_class:        call drv_cp_class
                    retf
cw_drv_cp_count:        call drv_cp_count
                    retf
cw_drv_cp_name:         call drv_cp_name
                    retf
cw_drv_load:            call drv_load
                    retf
cw_drv_row:             call drv_row
                    retf
cw_drv_status:          call drv_status
                    retf
cw_drv_tier:            call drv_tier
                    retf
cw_drv_unload:          call drv_unload
                    retf
cw_dsk_batch_begin:     call dsk_batch_begin
                    retf
cw_dsk_chdir:           call dsk_chdir
                    retf
cw_dsk_chdir_q:         call dsk_chdir_q
                    retf
cw_dsk_clus2lba:        call dsk_clus2lba
                    retf
cw_dsk_copy_in:         call dsk_copy_in
                    retf
cw_dsk_copy_seg:        call dsk_copy_seg
                    retf
cw_dsk_dirw_get:        call dsk_dirw_get
                    retf
cw_dsk_dirw_next:       call dsk_dirw_next
                    retf
cw_dsk_dirw_start:      call dsk_dirw_start
                    retf
cw_dsk_dotdot:          call dsk_dotdot
                    retf
cw_dsk_fat_ofs:         call dsk_fat_ofs
                    retf
cw_dsk_find_name:       call dsk_find_name
                    retf
cw_dsk_free_clus:       call dsk_free_clus
                    retf
cw_dsk_get_dir:         call dsk_get_dir
                    retf
cw_dsk_get_icon:        call dsk_get_icon
                    retf
cw_dsk_next_clus:       call dsk_next_clus
                    retf
cw_dsk_read_chain:      call dsk_read_chain
                    retf
cw_dsk_relist:          call dsk_relist
                    retf
cw_dsk_synth:           call dsk_synth
                    retf
cw_dsk_vol_row:         call dsk_vol_row
                    retf
cw_evq_pop:             call evq_pop
                    retf
cw_font_str:            call font_str
                    retf
cw_font_width:          call font_width
                    retf
cw_fpg_begin:           call fpg_begin
                    retf
cw_fpg_end:             call fpg_end
                    retf
cw_gfx_fill:            call gfx_fill
                    retf
cw_gfx_fill_gray:       call gfx_fill_gray
                    retf
cw_gfx_frame:           call gfx_frame
                    retf
cw_gfx_hline:           call gfx_hline
                    retf
cw_gfx_lock:            call gfx_lock
                    retf
cw_gfx_pen_cf:          call gfx_pen_cf
                    retf
cw_gfx_pen_dis:         call gfx_pen_dis
                    retf
cw_gfx_pen_live:        call gfx_pen_live
                    retf
cw_gfx_pixel:           call gfx_pixel
                    retf
cw_gfx_scroll:          call gfx_scroll
                    retf                    ; retf leaves the flags alone, so
                                            ; gfx_scroll's CF is still its
                                            ; answer at the cold caller
cw_gfx_unlock:          call gfx_unlock
                    retf
cw_gfx_vline:           call gfx_vline
                    retf
cw_gfx_xor_fill:        call gfx_xor_fill
                    retf
cw_icon_draw:           call icon_draw
                    retf
cw_icon_draw16:         call icon_draw16
                    retf
cw_inst_alloc:          call inst_alloc
                    retf
cw_inst_bind_win:       call inst_bind_win
                    retf
cw_inst_find_kind:      call inst_find_kind
                    retf
cw_inst_launch_post:    call inst_launch_post
                    retf
cw_inst_set_name_x:     call inst_set_name_x
                    retf
cw_inst_fhome_idx:      call inst_fhome_idx
                    retf
cw_inst_win_owner:      call inst_win_owner
                    retf
cw_mem_avail:           call mem_avail
                    retf
cw_mem_claim:           call mem_claim
                    retf
cw_mem_claim_dma:       call mem_claim_dma
                    retf
cw_mem_claim_hi:        call mem_claim_hi
                    retf
cw_mem_free:            call mem_free
                    retf
cw_mem_free_owner:      call mem_free_owner
                    retf
cw_menu_activate:       call menu_activate
                    retf
cw_menu_draw_bar:       call menu_draw_bar
                    retf
cw_menu_popup:          call menu_popup
                    retf
cw_osapi_snd_tone:      call osapi_snd_tone
                    retf
cw_sched_mode_get:      call sched_mode_get
                    retf
cw_sched_mode_set:      call sched_mode_set
                    retf
cw_snd_beep:            call snd_beep
                    retf
cw_snd_disp_set:        call snd_disp_set
                    retf
cw_task_yield:          call task_yield
                    retf
cw_toast_show:          call toast_show
                    retf
cw_toast_say:           call toast_say
                    retf
cw_ui_post_cmd:         call ui_post_cmd
                    retf
cw_vga_xor_rect_vram:   call vga_xor_rect_vram
                    retf
cw_vid_avail_test:      call vid_avail_test
                    retf
cw_vid_switch:          call vid_switch
                    retf
%ifdef KERN_BIG
cw_vid_dual_ok:         call vid_dual_ok
                    retf
cw_vid_disp_relay:      call vid_disp_relayout
                    retf
%endif
cw_wm_clip_set:         call wm_clip_set
                    retf
cw_wm_clip_test:        call wm_clip_test
                    retf
cw_wm_content:          call wm_content
                    retf
cw_wm_create:           call wm_create
                    retf
cw_wm_destroy:          call wm_destroy
                    retf
cw_wm_destroy_seg:      call wm_destroy_seg
                    retf
cw_wm_dmg_wins:         call wm_dmg_wins
                    retf
cw_wm_grow_paint:       call wm_grow_paint
                    retf
cw_wm_hit:              call wm_hit
                    retf
cw_wm_idx2ptr:          call wm_idx2ptr
                    retf
cw_wm_obscured:         call wm_obscured
                    retf
cw_wm_paint_all:        call wm_paint_all
                    retf
cw_wm_pkgcall:          call wm_pkgcall
                    retf
cw_wm_show:             call wm_show
                    retf
cw_wm_title_set:        call wm_title_set
                    retf
cw_wm_win_rect:         call wm_win_rect
                    retf

; ...and the other direction: what the kernel calls IN the Control Panel.
; cp_tpl and the driver/UI call sites still name these, so nothing outside
; ctrl.inc changed at all.
cp_paint:             call COLD_SEG:cpf_cp_paint
                    ret
cp_onclick:           call COLD_SEG:cpf_cp_onclick
                    ret
                      ; ...but NOT cp_flush. It has no thunk on purpose: with
                      ; no way into it from .text, "the panel's teardown is the
                      ; only thing that writes SYSTEM.CFG" (SPEC.md 31.8) is
                      ; something the build enforces rather than something
                      ; every new page has to be told
cp_flush_close:       call COLD_SEG:cpf_cp_flush_close
                    ret
cp_drv_gone:          call COLD_SEG:cpf_cp_drv_gone
                    ret
cp_tick_due:          call COLD_SEG:cpf_cp_tick_due
                    ret
cp_tick:              call COLD_SEG:cpf_cp_tick
                    ret

; --- ...and the file modules': loader.inc, diskw.inc, files.inc (SPEC.md 2.6).
; filecp.inc needs none - every caller of an fcp_ routine is files.inc, which
; is cold too, so those calls stayed near.
dskw_delete:          call COLD_SEG:dwf_dskw_delete
                    ret
dskw_dfree:           call COLD_SEG:dwf_dskw_dfree
                    ret
dskw_flush:           call COLD_SEG:dwf_dskw_flush
                    ret
dskw_gone:            call COLD_SEG:dwf_dskw_gone
                    ret
dskw_read:            call COLD_SEG:dwf_dskw_read
                    ret
dskw_remount:         call COLD_SEG:dwf_dskw_remount
                    ret
dskw_rename:          call COLD_SEG:dwf_dskw_rename
                    ret
dskw_stat:            call COLD_SEG:dwf_dskw_stat
                    ret
dskw_sync:            call COLD_SEG:dwf_dskw_sync
                    ret
dskw_write:           call COLD_SEG:dwf_dskw_write
                    ret
dskw_write_sys:       call COLD_SEG:dwf_dskw_write_sys
                    ret
dskw_mkdir:           call COLD_SEG:dwf_dskw_mkdir
                    ret
dskw_read_at:         call COLD_SEG:dwf_dskw_read_at
                    ret
dskw_append:          call COLD_SEG:dwf_dskw_append
                    ret
files_init:           call COLD_SEG:fmf_files_init
                    ret
files_open:           call COLD_SEG:fmf_files_open
                    ret
files_open_drive:     call COLD_SEG:fmf_files_open_drive
                    ret
files_poster:         call COLD_SEG:fmf_files_poster
                    ret
files_refresh:        call COLD_SEG:fmf_files_refresh
                    ret
%ifndef KERN_SMALL
fm_bar_gate:          call COLD_SEG:fmf_fm_bar_gate
                    ret
%endif
fm_focus:             call COLD_SEG:fmf_fm_focus
                    ret                 ; CF out (SPEC.md 22.8): a near ret
                                        ; over a far one, neither of which
                                        ; touches the flags
fm_kinit:             call COLD_SEG:fmf_fm_kinit
                    ret
fm_onclick:           call COLD_SEG:fmf_fm_onclick
                    ret
fm_oncmd:             call COLD_SEG:fmf_fm_oncmd
                    ret
fm_onkey:             call COLD_SEG:fmf_fm_onkey
                    ret
fm_paint:             call COLD_SEG:fmf_fm_paint
                    ret
fm_rclick:            call COLD_SEG:fmf_fm_rclick
                    ret
fm_rcmd:              call COLD_SEG:fmf_fm_rcmd
                    ret
fmv_sync:             call COLD_SEG:fmf_fmv_sync
                    ret
ld_run_body:          call COLD_SEG:ldf_ld_run_body
                    ret
ld_run_name:          call COLD_SEG:ldf_ld_run_name
                    ret
loader_init:          call COLD_SEG:ldf_loader_init
                    ret
loader_run:           call COLD_SEG:ldf_loader_run
                    ret
fdlg_grab:            call COLD_SEG:fdf_fdlg_grab
                    ret
fdlg_onclick:         call COLD_SEG:fdf_fdlg_onclick
                    ret
fdlg_onkey:           call COLD_SEG:fdf_fdlg_onkey
                    ret
fdlg_open:            call COLD_SEG:fdf_fdlg_open
                    ret
fdlg_paint:           call COLD_SEG:fdf_fdlg_paint
                    ret
fdlg_reap:            call COLD_SEG:fdf_fdlg_reap
                    ret
fdlg_top:             call COLD_SEG:fdf_fdlg_top
                    ret

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

section .lowbss
kernel_low_end:
KLOW_SIZE equ kernel_low_end - $$

section .bss
; modules declared their own .bss blocks; NASM accumulates them in order,
; so this lands last
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$

; The boot overlay's file position IS the image rung, which is what makes the
; boot sector's single read land it on FAT_SEG. It cannot be expressed as
; padding inside .text - that would grow KTEXT_SIZE, which grows KIMG_PARA,
; which grows the padding, and there is no fixed point - but as a SECTION
; start it is not circular at all: .ovl's own size is not one of the terms.
COLD_START equ ((KTEXT_SIZE + KBSS_SIZE + 511) / 512) * 512
OVL_START  equ COLD_START + ((COLD_SIZE + 511) / 512) * 512

section .cold
cold_end:
COLD_SIZE equ cold_end - $$

section .ovl
ovl_end:
OVL_SIZE equ ovl_end - $$

; What the Task Manager's RAM view reports (SPEC.md 28), in KB rounded up:
; the whole kernel, buffers and stacks included, because since SPEC.md 2 that
; is one contiguous span and there is nothing of the kernel outside it.
KERN_KB    equ (KERN_SIZE + 1023) / 1024
KBUF_KB    equ ((FAT_PARA + LOW_PARA) * 16 + 1023) / 1024

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
  %assign KS_OVL    OVL_SIZE
  %assign KS_STK0   STK0_SIZE
  %assign KS_IMGP   KIMG_PARA
  %assign KS_COLDP  COLD_PARA
  %assign KS_FATP   FAT_PARA
  %assign KS_LOWP   LOW_PARA
  %assign KS_SIZE   KERN_SIZE
  %assign KS_BUDGET KERN_BUDGET
  %assign KS_CODEM  KERN_CODE_MAX
  %assign KS_END    KERN_END
  %assign KS_KSEG   KERNEL_SEG
  %warning ks: text=KS_TEXT bss=KS_BSS cold=KS_COLD lowbss=KS_LOW ovl=KS_OVL stk0=KS_STK0 imgpara=KS_IMGP coldpara=KS_COLDP fatpara=KS_FATP lowpara=KS_LOWP ksize=KS_SIZE budget=KS_BUDGET codemax=KS_CODEM kend=KS_END kseg=KS_KSEG
%endif

; 1. KERN_BUDGET - the FOOTPRINT. The whole kernel - image, scratch, FAT
;    snapshot, disk buffers and every task stack - is one span starting at
;    KERNEL_SEG, and it fits KERN_BUDGET (72.5KB) just above the BIOS data
;    area. This is the guard the project is steering by; raising KERN_BUDGET
;    is a decision, not a build fix (docs/KERNEL-MEMORY.md).
;
;    AN INSTRUMENTED KERNEL IS NOT THE SHIPPED ONE, and the guard says so now
;    rather than the prose alone (CLAUDE.md: "a benchmark kernel is not bound
;    by KERN_BUDGET; what it IS bound by is parity"). DISK_COUNTERS is built
;    only for `make field` and for a bench run, on machines that all have
;    640KB, and it used to cost the image NOTHING because it landed in the
;    padding to OVL_START - so the exemption was never needed and nobody had
;    to decide anything. The baked typeface and the toast (SPEC.md 6.2, 59)
;    spent that padding, and the same counters now cost 1,821 bytes of .text
;    and two image rungs. Refusing to BUILD a field kernel is the wrong answer
;    to that: the shipped kernel's own number has not moved, and it is the
;    shipped kernel this guard exists to steer. tools/fieldsize.py is the
;    other half - it reports whether the field kernel and the shipped one
;    share a KIMG_PARA rung, so "bigger" stays known about rather than silent.
%ifdef DISK_COUNTERS
KERN_CEIL equ KERN_BUDGET + 4096    ; ...and a BOUND, not a free hand: four
%else                               ; steps, so the instrument cannot quietly
KERN_CEIL equ KERN_BUDGET           ; become the reason the kernel grew
%endif
%if KERN_SIZE > KERN_CEIL
%error "kernel too big: it must fit KERN_BUDGET - see docs/KERNEL-MEMORY.md"
%endif
; 2. KERN_CODE_MAX - the SEGMENT. The kernel's own segment is 64KB like any
;    other, and .text + .bss are both addressed through it, so they have to
;    fit one whether or not the budget above is ever raised. Nobody can raise
;    this one: it is what a 16-bit offset reaches.
%if KTEXT_SIZE + KBSS_SIZE > KERN_CODE_MAX
%error "kernel image + bss overflows KERN_CODE_MAX - one 64KB segment"
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
%if MENU_BARMAX * MB_ENTSZ > 98
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
; 6b. ...and so is every claim in it, which is what a package region rides
;     on now that it is a claim rather than a pool slot: mem_claim rounds to
;     whole KB, so every base it hands out is HEAP_SEG + n*64 paragraphs.
;     MEM_PARA_KB is that 64; if it ever stopped being a multiple of 32 an
;     int 13h read of a package image could straddle a 64KB DMA boundary and
;     the symptom would be "Disk error" on the LARGE packages only.
%if MEM_PARA_KB % 32
%error "a heap claim is not 512-byte aligned - a package image is read into one"
%endif
; 4b. the boot overlay has to fit the FAT window it is read into, because
;    that is the only memory it is ever allowed to occupy: disk_mount writes
;    over it the first time a volume is mounted (drv_boot, the last thing
;    kmain does). Overflowing would run the overlay's tail into LOW_SEG - the
;    task stacks - and the symptom would be arbitrary.
%if OVL_SIZE > FAT_PARA * 16
%error "the boot overlay does not fit the FAT window - see docs/KERNEL-MEMORY.md"
%endif
; 4c. ...and it has to exist. An empty .ovl means every FAT_SEG: far call in
;    kmain lands in whatever the FAT buffer happens to hold.
%if OVL_SIZE < 16
%error "the boot overlay is empty - the FAT_SEG far calls have nothing to reach"
%endif
; 5. the boot sector relocates itself to the TOP OF CONVENTIONAL RAM before
;    it reads a sector, and its stack grows down from there (SPEC.md 2.7), so
;    on any given machine the kernel has to end below both. That address is
;    computed from int 12h and is therefore not a constant this file can
;    check - what it CAN check is the machine we claim to support: at
;    MIN_RAM_KB the sector and its stack occupy the top 2,560 bytes, and
;    everything below that is the kernel's to spend.
;
;    This is a POLICY guard now, like guard 1 and unlike guard 2 - which is
;    the whole reason the constant it reads is named for the machine rather
;    than for an address. It used to be the binding one: KERN_BUDGET was
;    exactly equal to its ceiling, so raising the budget bought nothing and
;    the only way up was to move the sector. Now it is 44.5KB above the
;    budget, and the budget is free to move again the way it always did -
;    once, asked for, and granted.
%if KERNEL_SEG*16 + KERN_SIZE > MIN_RAM_KB*1024 - BOOT_SECT - BOOT_STACK
%error "the kernel does not fit MIN_RAM_KB - see docs/KERNEL-MEMORY.md"
%endif
