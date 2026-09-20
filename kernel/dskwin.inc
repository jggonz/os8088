; =============================================================================
; kernel/dskwin.inc - THE MOUNT-OWNED WINDOW, AT THE BOTTOM OF `.lowbss`
;
; Three buffers and the four constants that size them. They were the last
; thing in `disk.inc`'s `.lowbss` block, which put them at the TOP of the
; rung; they are here, in the FIRST file kernel.asm includes, so that they are
; the FIRST thing in `.lowbss` and therefore sit immediately above FAT_SEG.
;
; WHY THE ADDRESS MATTERS, when no other `.lowbss` byte's does (SPEC.md 2.1.2).
; The FAT window and these three come alive at the SAME MOMENT - the first
; mount - and are untouched before it. Adjacent, they are one contiguous
; 7,936-byte region that is dead for the whole of `kmain`:
;
;     FAT_SEG   4,608   the FAT snapshot, filled at mount
;     +           512   dsk_secbuf  FIRST: the one int 13h TARGET here, so it
;                                   takes the rung's 512-aligned base
;     +           768   disk_dir    "ALWAYS exactly a mount snapshot"
;     +         2,048   disk_icons  "fully rewritten every mount"
;     =         7,936   of which 7,680 is readable (SPEC.md 2.1.2). LOW_SEG is
;                       a rung base (guard 6); dsk_secbuf is aligned because it
;                       is the rung's first bytes, and the %if below holds it
;
; That is what the boot overlay is meant to land in and spill through
; (docs/plans/completed/BOOT-LADDER-PLAN.md stage B): `.ovlw`, the window half of the
; overlay (SPEC.md 2.5.3), is ~5,000 bytes and the FAT window alone is 4,608,
; so the window alone stopped being enough when SPEC.md 2.9.12's register
; moved twenty bodies into the overlay.
;
; NOTHING ELSE MOVED, and that is the point of doing it this way. `.lowbss` is
; `nobits`, so this costs no byte on the disk and no byte of RAM - the rung is
; the same size and holds the same things in a different order. What it buys
; is that the dead bytes are TOGETHER.
;
; POSITION IS BOUND BY INCLUDE ORDER, not by anything declarative: `-f bin`
; lays a section out in the order its contributions appear, and `.lowbss` has
; twelve contributors across the tree. So this file being first in kernel.asm
; is the whole mechanism, and tests/unit/t_lowwin.py is what says so if it
; stops being true. A `section .mntwin` of its own was the obvious alternative
; and is wrong here: these are reached through SS with the rest of `.lowbss`,
; so a segment of their own would be a code change at every access.
; =============================================================================

DSK_DE_SIZE  equ 32              ; the ON-DISK FAT stride, and the width of
                                 ; the record a DRVC_FILE driver hands over
                                 ; (OSAPI_FS_ENT, SPEC.md 19.1/62.9.1)
DSK_DE_STRIDE equ 24             ; ...and the STAGED LISTING's, which is a
                                 ; different question with a different answer.
                                 ; SPEC.md 19.1's record is meaningful to
                                 ; offset 23 - name 0..15, type 16, handle 18,
                                 ; size dword 20..23 - and 24..31 are declared
                                 ; ZERO. The arrays below stored those eight
                                 ; zero bytes per entry for the life of the
                                 ; machine: 256 of `.lowbss`, on the tightest
                                 ; rung in the kernel. THE TWO ARE NOT
                                 ; INTERCHANGEABLE and the %if below says so:
                                 ; dsk_ent stays DSK_DE_SIZE wide because a
                                 ; driver's record and a package HEADER are
                                 ; both copied into it whole
DSK_ICO_SIZE equ 64              ; harvested icon slot stride (SPEC.md 19)

; ...and HOW MANY BODIES THE FLOPPY ARRAYS HOLD (SPEC.md 25.8).
;
; It was one body per entry, `DSK_NENT * DSK_ICO_SIZE` = 2,048 bytes of the
; tightest rung in the kernel, and most of them were provably identical or
; provably blank: every FOLDER got a byte-for-byte copy of the same built-in
; body, and every document got a blank one. So `kern_small` holds a POOL of
; bodies and a one-byte index per entry - 16*64 + 32 = 1,056 against 2,048,
; and 0xFF in the index is the "no icon" the generic-icon sentinel already
; meant.
;
; That was the shape for one cycle. SPEC.md 25.9 then took the bodies out of
; the listing ALTOGETHER, on both kernels: there is one machine-wide store and
; a listing owns a reference byte per entry and nothing else. DSK_ICO_SIZE is
; still the width of one body, in the store.
%ifdef KERN_KNOB
DSK_OVLPAD   equ 1024           ; **AND THE NEXT THING HAS JOINED `.ovlw`: A
                                 ; DIAGNOSTIC KNOB.**
                                 ;
                                 ; This is the case the paragraph below says
                                 ; the label is kept for, arriving on kern_BIG
                                 ; rather than kern_small.  SPEC.md 22.6.3
                                 ; abolished the floor listing, which took
                                 ; 1,600 bytes off kern_big's window and its
                                 ; region with them - 6,720 to 5,120 - and the
                                 ; shipped `.ovlw` is 4,979, which rounds to
                                 ; 5,120 and fits with NOTHING to spare.  A
                                 ; diagnostic that adds so much as one byte
                                 ; then costs a whole 512 at the rounding, and
                                 ; six knob arms stopped assembling on a
                                 ; kernel that ships perfectly: BOOTMARK,
                                 ; BOOTHALT, MOUDIAG, MOUROUND, SNDSNIFF and
                                 ; FONT (tests/unit/t_buildmatrix.py, and
                                 ; t_nasm3 is only where it was SEEN first -
                                 ; nasm 3 has nothing to do with it and both
                                 ; assemblers refuse it identically).
                                 ;
                                 ; TWO RUNGS AND NOT ONE, because the worst
                                 ; arm is not the obvious one: `FONT=<name>`
                                 ; bakes a face into the overlay and takes
                                 ; `.ovlw` to 5,701, where BOOTMARK - the arm
                                 ; that FAILED first and the one it is
                                 ; tempting to size this against - takes it to
                                 ; 5,142. One rung fixes five of the six and
                                 ; leaves `font` refusing with the identical
                                 ; message, which is the shape of a fix that
                                 ; looks finished. Sized on the measurement
                                 ; instead: 6,144 covers every arm in
                                 ; tests/unit/t_buildmatrix.py with 443 bytes
                                 ; to spare.
                                 ;
                                 ; `KERN_KNOB` is the Makefile's own answer to
                                 ; "is this a diagnostic build", already
                                 ; derived from $(KNOBS) with KERN_SMALL and
                                 ; KERN_EMU filtered OUT because those are
                                 ; shipped kernels.  So this reaches no
                                 ; shipped byte: `make`, `make small` and
                                 ; `make emu` are byte-identical to what they
                                 ; were, and the one build that pays 512 bytes
                                 ; of dead `.lowbss` is a build nobody boots
                                 ; (kernel.asm guard 1 already excuses it from
                                 ; KERN_BUDGET for exactly this reason).
                                 ;
                                 ; SHRINKING `.ovlw` was the other way out and
                                 ; is refused HERE rather than left unsaid: it
                                 ; is the OVBCALL move below, it would reach
                                 ; every shipped kernel, and it would be spent
                                 ; buying headroom for a diagnostic.  The room
                                 ; a knob build needs is room a knob build can
                                 ; pay for.
%elifdef KERN_SMALL
DSK_OVLPAD   equ 0              ; **AND IT IS ZERO AGAIN.**
                                 ;
                                 ; docs/plans/LISTING-HOME-PLAN.md 13 took the
                                 ; ENTRIES and their reference index out of
                                 ; `.lowbss` entirely - 800 bytes here - and
                                 ; `.ovlw` is read onto this region (SPEC.md
                                 ; 2.5.3), so for one commit 512 of those 800
                                 ; were held back here rather than given to
                                 ; the heap: the overlay had nowhere else to
                                 ; land. Six boot-only bodies then moved into
                                 ; the BLOB half instead - sched_init,
                                 ; mem_init, font_init, wm_init, files_init
                                 ; and snd_init, through the OVBCALL set that
                                 ; SPEC.md 2.5.3.2 built for exactly this -
                                 ; and `.ovlw` fell 1,900 to 1,342, which
                                 ; rounds to 1,536 and fits the region with
                                 ; the pad at nothing.
                                 ;
                                 ; So this build takes the whole 800 after
                                 ; all, and the label is kept for the third
                                 ; time for the reason it was kept the first:
                                 ; the NEXT thing that joins `.ovlw` needs
                                 ; somewhere to be told about. kernel.asm's
                                 ; `.ovlw` %if is the guard that says whether
                                 ; you are in that case
%else
DSK_OVLPAD   equ 0              ; a plain kern_big: the region is 5,120 and
                                 ; the overlay rounds to exactly that, so
                                 ; there is nothing to hold back and nothing
                                 ; spare either. Stated rather than left
                                 ; undefined so that every arm of this chain
                                 ; names a number and the `%if` below is the
                                 ; only place that asks whether it is zero
%endif

; **THERE IS NO PER-LISTING POOL ANY MORE** (SPEC.md 25.9). A listing owns one
; reference byte per entry and no bodies at all; ICO_NROW in disk.inc is the
; machine-wide store's size and is the only body count left. DSK_ICO_SIZE is
; still the width of one.
DSK_ICOIX_N  equ DSK_NENT        ; one REFERENCE byte per entry, and DSK_NENT
                                 ; on BOTH kernels because there is one
                                 ; listing size now: SPEC.md 22.6 retired the
                                 ; donated claim, so a volume lists into the
                                 ; `.lowbss` floor whatever mounted it. It was
                                 ; DSK_VENT here while a hard disk could list
                                 ; more than a floppy. 64 bytes on kern_big,
                                 ; against the 4,096 of bodies it replaces

; How many entries a listing holds - ONE NUMBER, for every volume on the
; machine. There was a second, DSK_VENT, for a volume whose driver donated a
; 6KB claim to list out of; SPEC.md 22.6 retired it, and the retirement is
; worth one paragraph because the mechanism looked live right up to the end.
; It could only ever buy DSK_VENT > DSK_NENT - a hard disk listing more than a
; floppy out of heap the driver owns, charging a floppy-only machine nothing -
; and the 64-entry pass (SPEC.md 25.9.1) raised the floor to the same 64,
; after which the claim funded a listing the floor already gave for free. What
; settled it is that the two move TOGETHER: a hard disk's directories and a
; floppy's are busy for the same reason - a DOS install is busy wherever it
; sits - so the next raise raises this number and there is nothing to split.
;
; **AND IT IS PER-BUILD SINCE SPEC.md 22.6.2**: 64 on kern_big, 32 on
; kern_small. Sixty-four was taken for the DOS box - "DOS directories are
; busier than ours ever were: LEMMINGS 68 entries, F15 66, TD1 41" - and
; kern_small has no DOS box: the plan to run one there was not realised, and
; `build/small.img` / `build/small360.img` carry 12 root entries with 8 in the
; busiest folder. So on that kernel the second 768 bytes of `disk_dir` list
; nothing any disk it ships has, while `.lowbss` is the tightest rung in the
; kernel and its bottom 2,112 bytes are what the boot overlay's window half
; spills through.
;
; **IT DOES NOT MOVE ALONE.** `.ovlw` is loaded ONTO this region (SPEC.md
; 2.5.3) and kernel.asm's guard is `roundup(OVLW_SIZE, 512) <= FAT_PARA*16 +
; DSK_WIN_BYTES`: at 32 entries that ceiling is 2,336, so `.ovlw` has to be
; 2,048 or less first. SPEC.md 2.5.3.2 is what makes it so - the serial mouse
; probe and the adapter probe go in `.ovl` on this kernel, out of blob stage 1
; already reads. Cutting this number on its own is a `%error`, and moving those
; bodies on their own moves HEAP_SEG by NOTHING. The pair is the change.
;
; The arms are THIS WAY ROUND because tools/os88disk.py parses the first
; `DSK_NENT equ <n>` out of this file for its per-directory build cap, and the
; DEFAULT that cap wants is kern_big's; `--kern-small` is what selects the
; other, and t_mirror is what would notice if the parse stopped working.
%ifndef KERN_SMALL
DSK_NENT     equ 64
%else
DSK_NENT     equ 32
%endif
%if DSK_DE_SIZE != 32
  %error "DSK_DE_SIZE is the ON-DISK FAT directory stride, 32 by FAT12, and it is PUBLISHED to packages (apps/os88api.inc's OSAPI_FS_ENT). It is not free to change: a staged entry that wants a different width needs a stride of its own, leaving this one at 32"
%endif
%if DSK_DE_STRIDE > DSK_DE_SIZE
  %error "DSK_DE_STRIDE is the MEANINGFUL PREFIX of a DSK_DE_SIZE record and cannot be wider than it: dsk_put_dir copies DSK_DE_STRIDE bytes OUT of a DSK_DE_SIZE-byte dsk_ent, and a stride past the end of it reads whatever .bss follows"
%endif
%if DSK_DE_STRIDE < 24
  %error "DSK_DE_STRIDE must cover SPEC.md 19.1's meaningful prefix: name 0..15, type 16, handle 18, size dword 20..23. Below 24 an entry loses its SIZE, and the Disk window letters a stale one"
%endif

; --- DOES THIS BUILD HARVEST ICONS? (SPEC.md 96.44.7) -----------------------
; `disk_icons` is **2,048 bytes of `.lowbss`** and step 4 of `disk_mount` is
; what fills it: a 16x16 body per entry, read with one `int 13h` per type-1
; file. It is the DISK WINDOW's, and `kern_dos` has no window, no
; `fm_draw_icon16` and no file manager - MEASURED on the guest after a
; handoff, the array held 192 non-zero bytes and every reader of it in that
; root was inside the mount that wrote it.
;
; **`disk_dir` STAYS, and that is a correction.** The listing looked equally
; dead and is not: SPEC.md 96.47's live resume finds `HIBERNAT.IMG` with
; `dsk_find_name_x`, which walks `[disk_nfiles]` entries through
; `dsk_get_dir_x` - so the 768-byte half has a real consumer and only the
; 2,048-byte half does not. The two were one switch for a day and the resume
; row is what said so.
%ifndef KD_BUILD
  %define DSK_WANT_ICONS        ; the kernel, either size
%endif

section .lowbss                 ; LOW_SEG: written by int 13h through ES:BX
                                ; (or rep movsw/stosw from the mount), read
                                ; only through disk.inc's dsk_get_*
dsk_win_base:
dsk_secbuf: resb 512            ; sector scratch: BPB read, root-dir scan,
                                ; icon harvest, and the loader's header peek.
                                ; FIRST, and that is an int 13h requirement
                                ; rather than a preference: this is the one
                                ; buffer here that is a TRANSFER BASE (ES:BX
                                ; at dsk_rd1_x), and only a 512-aligned base
                                ; cannot straddle a 64KB DMA page (SPEC.md
                                ; 2.1.1). LOW_SEG is 512-aligned by guard 6,
                                ; so being the rung's first bytes is what
                                ; aligns it - at +2,816 it was 256 into a
                                ; sector and the build's own size decided
                                ; whether the first mount failed with 09h
                                ; **AND NO `disk_dir` AND NO `dsk_icoix`**
                                ; (docs/plans/LISTING-HOME-PLAN.md 13). The
                                ; floor listing was DSK_NENT synthesized
                                ; entries and one icon-reference byte each -
                                ; 1,600 bytes on kern_big and 800 on
                                ; kern_small, in `.lowbss`, which sits BELOW
                                ; HEAP_SEG and so costs the heap and the DOS
                                ; arena byte for byte, on every machine, for
                                ; ever, whether or not anything was listing.
                                ;
                                ; A listing is written where its CALLER keeps
                                ; a store now: a Disk window's FS_VSEG claim,
                                ; which it already held, or one the Standard
                                ; File dialog takes while it is up. Both are
                                ; TRANSIENT, so a machine on the desktop or
                                ; inside a fullscreen game pays nothing at
                                ; all - and a caller with nowhere for a
                                ; listing gets a quiet mount instead of one.
%if DSK_OVLPAD                  ; the PAD decides, not the kernel: it is a
                                ; diagnostic knob that needs this on kern_big
                                ; (see DSK_OVLPAD above) and kern_small that
                                ; needed it first, so keying the block to
                                ; either build name would be keying it to the
                                ; wrong question
dsk_ovlpad: resb DSK_OVLPAD     ; ...AND THE BOOT OVERLAY'S LANDING GROUND,
                                ; which the icon array was doing as a SECOND
                                ; JOB nobody had written down. `.ovlw` is read
                                ; onto the FAT window and the mount buffers
                                ; (SPEC.md 2.5.3) and a byte past them is
                                ; vid_rowtab, live since MARK 7 - so those
                                ; buffers may shed only what the overlay does
                                ; not need, and on this kernel the 2,048 bytes
                                ; of icons were most of the room. kern_big has
                                ; a wider FAT window and needs none of this.
                                ;
                                ; It is DEAD AT RUNTIME and that is the honest
                                ; cost: kern_small saves less than kern_big
                                ; from SPEC.md 25.9 because its boot overlay,
                                ; not its listing, is what sizes this region.
                                ; The %if in kernel.asm is what says whether
                                ; the figure is still right, and shrinking
                                ; `.ovlw` is what gives these bytes back -
                                ; WHICH HAS NOW BEEN DONE (SPEC.md 2.5.3.2),
                                ; and the bytes went to the LISTING's own cut
                                ; rather than to this label: DSK_NENT 64 -> 32
                                ; (SPEC.md 22.6.2) with DSK_OVLPAD still 0, so
                                ; the region fell 3,136 -> 2,336 and HEAP_SEG
                                ; with it. This label emits nothing today and
                                ; is kept because the NEXT thing that joins
                                ; `.ovlw` needs somewhere to be told about
%endif
dsk_win_end:
DSK_WIN_BYTES equ dsk_win_end - dsk_win_base
%if (dsk_secbuf - $$) % 512     ; against the RUNG base, not dsk_win_base: a
  %error "dsk_secbuf is not 512-byte aligned inside LOW_SEG: an int 13h read from an unaligned base can straddle a 64KB DMA page and fail with error 09h. Keep it FIRST in the window (SPEC.md 2.1.1/2.1.2, guard 6)"
%endif                          ; block sorted above this file would slide it

section .text
