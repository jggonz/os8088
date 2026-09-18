; =============================================================================
; kerndos/kdlayout.inc - the segment ladder
;
; kern_dos sits at the bottom of conventional memory and everything above it
; is the DOS program's, which is the whole point (docs/plans/KERN-DOS-PLAN.md
; its §1: ~39 KB for all of this against a 600 KB target).
;
; THE LADDER IS THE KERNEL'S SHAPE WITH THE MIDDLE TAKEN OUT: image, then the
; FAT snapshot, then the mount buffers, and no cold rung and no task stacks
; because there is no boot overlay and no scheduler (KERN-DOS-PLAN §4.2).  The kernel
; derives each rung from the section it follows; here the image rung is
; DECLARED and asserted at the foot of kerndos.asm, because a two-pass
; derivation buys nothing when there is one arm to size.
; =============================================================================
; **`KD_BUILD` IS "THIS IS NOT THE KERNEL"**, and every root that includes a
; kernel file gets it here rather than declaring it: `kerndos.asm` and
; `kdos.asm` both start with this file, and `KD_BACKEND` is the DOS core's own
; switch and says nothing about the disk layer.  What it gates in `kernel/` is
; code that cannot work under this root at all - a module image with no
; `mod_need` to load it, a UI with no window - so a kernel build sees none of
; it and measures byte-identical.
%define KD_BUILD

KD_SEG      equ 0x0060          ; linear 0x00600, above the IVT and the BDA
KERNEL_SEG  equ KD_SEG          ; ...and the kernel's own files call it this

; **ONE RUNG FOR BOTH ARMS SINCE SPEC.md 96.45**, and the conditional that
; used to be here is gone rather than updated. It read 35 with the core as a
; PART and 34 with it inline (96.44.5.1: the reservation is 489 bytes and
; that crossed 34, which is what took the DOS program 586 KB -> 585). The
; mouse's 392 bytes then put the INLINE arm over 34 as well - 35,047 against
; 34,816 - so the two arms want the same number and a conditional that always
; answers the same thing is a thing to get wrong later.
;
; **THE SHIPPED FIGURE DOES NOT MOVE.** What ships is the extcore arm and it
; was already 35, so the DOS program keeps its 585 KB; what rises is the
; `KD_GATE` build (tests/kdos.py), which is a test artefact on no floppy.
;
; **35 -> 36 AT SPEC.md 96.49, AND THIS ONE IS PAID BY THE PROGRAM.** The live
; resume is 996 bytes - 36,472 against 35,840 - of which ~440 is 87.5's stub,
; which has to BE somewhere to be staged and is the irreducible half. The DOS
; program goes 585 KB -> 584, which is the trade docs/plans/KERN-DOS-PLAN.md
; 8.1 costed at ~1,200 and refused: the estimate was sound and its OTHER
; argument - that the extent list has nowhere to live - was wrong about WHEN,
; the list going in the text framebuffer at a moment the program has already
; exited. 392 bytes of the new rung are left, and by this comment's own rule
; the next wave lowers it again rather than banking slack.
;
; **AND 36 -> 35 AT 96.44.6, WHICH IS THE WAVE THAT LINE ASKED FOR.** The
; refusal wall was 1,432 bytes of `stc`/`retf` under CORE_ORG and it is gone
; because the calls it caught are; CORE_ORG falls 0x0600 -> 0x0080 in BOTH
; hosts. So the live resume's 996 bytes are paid for and the program is ahead:
; MEASURE this rather than subtracting the two, because the assertion at the
; foot of each root is what the number has to clear.
;
; **AND 35 -> 34 AT 96.44.8**, the recursive-delete pipeline: 529 bytes, dead
; because DOS has no such call - AH=3Ah removes an empty directory and refuses
; a populated one, which is the strict `dskw_rmdir` arm `dos_k_rmdir` already
; takes. 381 bytes of the rung left.
KD_IMG_KB   equ 35              ; 34 until SPEC.md 96.11.7's character devices
                                ; raised CORE_MAX by a kilobyte, and this rung
                                ; reserves the core's block - so a kilobyte
                                ; there is a kilobyte here, which is a kilobyte
                                ; off the DOS program. Task #27 is where both
                                ; come back; the owner's call was *"working at
                                ; all is most important"*.
                                ; the image rung: every progbits section PLUS
KIMG_PARA   equ KD_IMG_KB * 64  ; the bss, rounded up to whole KB and asserted
                                ; at the foot of each root.  IT HAS TO CLEAR
                                ; THE WHOLE THING and not just `.text`: FAT_SEG
                                ; sits on top of it, so a rung short by a
                                ; kilobyte puts the FAT snapshot INSIDE the
                                ; code and the mount overwrites what called it.
                                ; At wave 4 kdos.asm measures .text 32,425 +
                                ; .cold 11,397 + .ovlw 762 + .modf 1,235 =
                                ; 45,827 of image and 12,316 of `.bss` above
                                ; it, so 58,144 all in and 60KB is the rung
                                ; that holds it.  **BOTH HALVES COST A ROUND
                                ; OF DEBUGGING**: 40 cleared neither and 56
                                ; cleared the image and missed the bss by 800
                                ; bytes, which ran anyway.
                                ;
                                ; **THIS IS NOT A RUNG AND IT IS NOT FREE.**
                                ; `LOW_SEG` sits on it and the arena sits on
                                ; that, so a KB here is a KB OFF THE DOS
                                ; PROGRAM - the one quantity
                                ; docs/plans/KERN-DOS-PLAN.md 1 is a budget
                                ; for. So it is raised to the smallest whole
                                ; KB that fits and never to a round one: 60 ->
                                ; 61 at wave 5, the image having measured
                                ; 61,482 (SPEC.md 96.40.2's refusal table is
                                ; 1,432 of it and 96.30.7's console the rest),
                                ; which leaves 982 bytes and costs the program
                                ; 560 -> 559 KB
                                ;
                                ; **61 -> 43 -> 34 AT WAVE 8** (SPEC.md 96.43,
                                ; 96.43.2): 43,302 with the cable network, the
                                ; console and the FORMAT module gated out, and
                                ; 34,166 once the WINDOW HALF went with them -
                                ; twenty-seven whole kilobytes back to the
                                ; program. 650 bytes spare, which is what this
                                ; comment's own rule asks for - the next wave
                                ; lowers it again rather than banking slack
                                ;
                                ; **35 -> 34 WITH THE REFUSAL WALL** (SPEC.md
                                ; 96.44.6). The wall was 1,432 bytes of
                                ; `stc`/`retf` under CORE_ORG and it is gone
                                ; because the calls it caught are: the image
                                ; measures 33,953 against 35,361, and CORE_ORG
                                ; falls 0x0600 -> 0x0080 in BOTH hosts. 863
                                ; bytes spare
; --- the exit code's mailbox (SPEC.md 96.41, docs/plans/KERN-DOS-PLAN.md 8.2)
; MIRRORED in kernel/hiber.inc, and tests/unit/t_mirror.py is the guard.
KDB_SEG     equ 0x0040          ; the BDA...
KDB_OFF     equ 0x00F0          ; ...and its intra-application area, which the
                                ; BIOS sets up at POST and never touches again
KDB_SIG     equ 0x3158444B      ; 'KDX1', little-endian
KDB_CODE    equ 4               ; dw the program's exit code, behind the magic
KDB_AKB     equ 6               ; dw ...and the ARENA it was given, in KB
                                ;    (SPEC.md 96.41.1). It rides here for
                                ;    KDB_CODE's own reason: the box cannot
                                ;    work it out - the arena is THIS host's
                                ;    arithmetic off [kd_top] - and `int 19h`
                                ;    plus a whole boot is between the two

COLD_SEG    equ KD_SEG          ; NO COLD RUNG: nothing here is boot-only, so
COLD_PARA   equ 0               ; .cold is ordinary code in the one image

FAT_SEG     equ KD_SEG + KIMG_PARA        ; the mount-time FAT snapshot
FAT_PARA    equ DSK_FAT_SECS * 32         ; 512 bytes = 32 paragraphs
LOW_SEG     equ FAT_SEG + FAT_PARA        ; ...and the mount buffers above it


KD_LOW_KB   equ 3               ; how much .lowbss the layer's buffers want
                                ; PLUS the stack above them, asserted in
                                ; kerndos.asm.
                                ;
                                ; **8 -> 5 ON A MEASUREMENT** (SPEC.md
                                ; 96.43.1): the stack's water mark is **134
                                ; bytes** of the 4,864 that eight kilobytes
                                ; left it, read off a machine that had mounted
                                ; a floppy, loaded a .COM and serviced its INT
                                ; 21h calls. `KDSTKDIAG=1` and
                                ; `tools/kdstkwater.py` re-derive it
                                ;
                                ; **5 -> 3 WITH THE ICON HARVEST OUT** (SPEC.md
                                ; 96.44.7). `disk_icons` is 2,048 bytes of the
                                ; window that no build with no window reads, so
                                ; `.lowbss` is dsk_secbuf + disk_dir = 1,280 -
                                ; leaving 1,792 for the stack, which is what
                                ; five left it before. **disk_dir STAYS**: it
                                ; looked as dead as the icons and 96.47's live
                                ; resume reads it, which is why this is 3 and
                                ; not 2
KD_STACK    equ KD_LOW_KB * 1024 ; ...and the stack sits at the top of it and
                                ; grows DOWN toward the buffers

; The top of the DOS program's memory is the MACHINE's, and it arrives in the
; launch block rather than being a constant here: `KDL_CAP` is the BDA's own
; KB, which is what `int 12h` answers and what a real DOS uses. Wave 4's gate
; asked 0x9000 (576KB) on the reasoning that it was below the adapters' RAM on
; anything this can boot; the BDA says 640 on those same machines, which is
; 60KB the arithmetic was throwing away.
KD_CAP_MAX  equ 640             ; KB. The clamp, not the answer: 640 * 64 is
                                ; 0xA000 paragraphs and the last figure that
                                ; fits the `shl` in 16 bits, and an 8086 has
                                ; nowhere above it to put a .COM anyway

; --- where the stub leaves the launch block (docs/plans/KERN-DOS-PLAN.md §7 step 7) ---
; The image's first bytes are a FIXED HEADER, because the stub has no symbol
; table: a near jump to the entry, then a word saying where to put the block.
; Putting the block itself here would cost 512 bytes of the part on disk for
; every machine that never uses them; a pointer costs four and lets it live in
; `.bss`, which is nobits.
KD_H_JMP    equ 0               ; 3: `jmp kd_entry`
KD_H_LBP    equ 4               ; dw: where the block goes
KD_H_LBSZ   equ 6               ; dw: ...and how many bytes of it there are
