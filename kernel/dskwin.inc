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
; `kern_big` KEEPS ONE BODY PER ENTRY and is byte-identical. That is not
; timidity: a `DVK_DRV` volume gets DSK_VENT = 64 entries out of a claim its
; driver funds, and a pool sized for a floppy would degrade the 17th icon on a
; hard disk. `kern_small` can have no such volume at all - SPEC.md 51.0 took
; the driver mechanism out - so there the pool only ever serves these arrays,
; which is what makes the simpler code correct rather than merely smaller.
%ifdef KERN_SMALL
DSK_ICO_N    equ 16              ; bodies in the pool. SIXTEEN is the product
                                 ; judgement: the small system disk's APPS
                                 ; folder holds three icon-bearing packages
                                 ; and GAMES one, the small apps disk's root
                                 ; holds twenty-one, and the seventeenth of
                                 ; those falls back to the generic icon -
                                 ; which is an outcome SPEC.md 25 already has
                                 ; a picture for. Raising it costs 64 bytes a
                                 ; body and SPEC.md 51.0.0's region has 288
                                 ; spare, so 20 is available and 24 is not
%else
DSK_ICO_N    equ DSK_NENT        ; one body per entry, as it always was
%endif

%ifdef KERN_SMALL
DSK_ICOIX_N  equ DSK_NENT        ; the index is one byte per ENTRY, so it is
                                 ; cut from the listing and not from the pool.
                                 ; DSK_NENT and not DSK_VENT because SPEC.md
                                 ; 51.0 leaves this kernel no driver-backed
                                 ; volume to list sixty-four entries from
%endif

; How many entries a listing holds. DSK_NENT is the FLOOR - the .lowbss pair
; every floppy uses, and the fallback for a driver-backed volume whose driver
; could not fund a bigger one. DSK_VENT is what a hard disk gets: 96 bytes an
; entry (32 of directory + 64 of icon), so 64 entries is a 6KB claim, made by
; the driver at mount and mirrored per open Disk window by the file manager
; (SPEC.md 22.6). Both numbers are here so nothing can disagree about them.
DSK_NENT     equ 32
DSK_VENT     equ 64
DSK_VBYTES   equ DSK_VENT * (DSK_DE_STRIDE + DSK_ICO_SIZE)  ; 5,632
DSK_VKB      equ 6              ; ...and the claim a driver funds is still 6KB:
                                ; 5,632 rounds up to 6, so nothing on the
                                ; driver side moves (drivers/os88drv.inc's
                                ; HDD_LISTKB) and no .DRV rebuilds
%if DSK_VBYTES > DSK_VKB * 1024
  %error "the driver-backed listing no longer fits the claim its driver funds: DSK_VKB is what is asked for and DSK_VBYTES is what disk_mount then writes into it, so the overflow lands on whatever the heap handed out next"
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
disk_dir:   resb DSK_NENT*DSK_DE_STRIDE    ; DSK_NENT SYNTHESIZED entries,
                                ; DSK_DE_STRIDE apiece and not DSK_DE_SIZE:
                                ; 19.1's bytes 24..31 are zero and no reader
                                ; in the tree looks at them,
                                ; built by disk_mount from the FAT directory
                                ; (SPEC.md 19) - never a raw on-disk image.
                                ; The FLOOR listing: a driver-backed volume
                                ; lists into its driver's claim instead, and
                                ; [dsk_dseg]/[dsk_doff] say which is in use
%ifdef KERN_SMALL
dsk_icoix:  resb DSK_ICOIX_N    ; entry i's POOL SLOT, or 0xFF for no icon
                                ; (SPEC.md 25.8). Blanked every mount, which
                                ; is 32 bytes of `rep stosb` where the array
                                ; below took 2,048 of `rep stosw`
%endif
disk_icons: resb DSK_ICO_N*DSK_ICO_SIZE    ; harvested icon bodies. On
                                ; kern_big slot i belongs to directory entry
                                ; i and all-zero means no icon; on kern_small
                                ; it is a POOL and dsk_icoix says which body
                                ; an entry uses. Fully rewritten every mount
dsk_win_end:
DSK_WIN_BYTES equ dsk_win_end - dsk_win_base
%if (dsk_secbuf - $$) % 512     ; against the RUNG base, not dsk_win_base: a
  %error "dsk_secbuf is not 512-byte aligned inside LOW_SEG: an int 13h read from an unaligned base can straddle a 64KB DMA page and fail with error 09h. Keep it FIRST in the window (SPEC.md 2.1.1/2.1.2, guard 6)"
%endif                          ; block sorted above this file would slide it

section .text
