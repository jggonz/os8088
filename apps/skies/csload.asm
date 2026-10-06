; =============================================================================
; os8088 - apps/skies/csload.asm
;
; CLEAR SKIES' LOADER - the IMAGE of SKIES.O88, and the whole of what the
; kernel launches (SPEC.md 88.10.4, 20.12.10).
;
; It reads the two parts, tells the program where the art went, and asks the
; kernel to treat the program as the program. Then its region is freed and it
; is gone: what runs is apps/skies/skies.asm, at PART 0, in the parts carve,
; with an instance and a window and a name of its own and no idea any of this
; happened.
;
; WHY IT EXISTS IS A NUMBER. `image + bss` is bounded by APP_MAX_SIZE - 61,440
; bytes, and it bounds ONE segment because a package addresses itself with
; 16-bit offsets. Clear Skies was at 61,100 of it, with the DIAGNOSTIC and
; PROBE builds 436 and 585 bytes OVER, so `make skiesdiag` did not assemble
; and two registered rows were skipping. §88.10.3 took the title art out of
; the image and bought 3,268; this takes the READER out too, and buys the
; disk back (88.10.3.1) - a parted image cannot be compressed, so with the
; body inside it SKIES.O88 went 37,534 -> 49,031 bytes on a 360KB apps disk
; with 8 clusters spare. As a PART the body is OP_COMP like everything else.
;
; IT MUST NOT CREATE A WINDOW (SPEC.md 20.12.10.6): its region is about to be
; freed, so a window whose W_SEG named it would far-call a dead claim on its
; first repaint. The program's window is the one the user sees.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SKIES', csl_entry, 1 | OS88_F_PARTS, OS88_STACK_DEFAULT

%include "csicon.inc"           ; ...and the SAME icon the program carries: the
                                ; Disk window draws this one before the launch
                                ; and the dock draws the program's after it

%include "os88parts.inc"

CS_PART_BODY equ 0              ; the program - a whole .o88 image (20.12.10)
CS_PART_ART  equ 1              ; the title bands (88.10.3)
CS_PART_WLD0 equ 2              ; ...and the world streams (88.10.5)
CS_NPARTS    equ CS_PART_WLD0 + CSH_NDIR

; --- the handoff, at the head of the PROGRAM's bss (SPEC.md 20.12.10.2) ------
; ONE PACKAGE, TWO SOURCES: apps/skies/skies.asm declares these and this file
; is the other end of them. The kernel is not involved and has no opinion; it
; does not zero a part, which is the whole of what makes this work.
CSH_MAGIC  equ 0                ; word: 'CS' - the loader ran
CSH_ART    equ 2                ; word: where it put the title bands
CSH_CLB    equ 4                ; word: this volume's bytes per cluster
CSH_WDIR   equ 6                ; 9 rows of (sector, packed length): the shared
CSH_NDIR   equ 9                ; vocabulary and then the eight worlds
CSH_SIZE   equ CSH_WDIR + CSH_NDIR * 4

LD_H_IMG   equ 8                ; ...and the two header fields it reads them
LD_H_BSS   equ 10               ; at, which are the FORMAT's and not ours

; -----------------------------------------------------------------------------
; csl_art - fetch the title bands (SPEC.md 88.10.4.1)
; out: AX = the segment holding the expanded bands (csart.inc's
;      CS_ART_SIZE), or 0
; clobbers: BX, CX, DX, SI, DI, ES, flags
;
; THE BANDS ARE AN OP_COMP | OP_LAZY PART, so op_fetch does all of it: one
; claim, the packed stream read R paragraphs up it, and the expansion down
; onto its base (SPEC.md 20.12.7.4). This used to be a plain lazy row of a
; stream tools/csart.py packed, fetched into one claim and expanded through
; OSAPI_DECOMP into a second - two of MEM_OWNER_MAX's eight while it ran, a
; stream and a decoder the package had to keep agreeing with, and the reason
; given for it, that a lazy row could not be OP_COMP, was withdrawn by
; 20.12.7.4 a cycle before anybody came back for it.
;
; THE CLAIM IS THE BANDS' FOR THE SESSION and it is NOT in the carve: after
; the re-home it is a slot-owned data claim with no proc, so it never moves,
; and the program's bare region proc stays right (SPEC.md 66.6.1.1). It is
; op_lazykb's figure - R plus the read of the packed part - while it expands,
; and op_fetch then shrinks it in place to the head slack plus the bands
; (88.10.4.1), the same 11KB the old exact-size claim had on a floppy. There
; is no second claim at any point.
;
; EVERY REFUSAL IS SURVIVABLE and answers 0, which is the plainer title page
; the program has always been able to draw - the title lettered in the 8x8
; face and no aeroplane (SPEC.md 88.10.2). A machine too full for 11KB still
; flies, and op_fetch has already said why in a toast.
; -----------------------------------------------------------------------------
csl_art:
    mov al, CS_PART_ART
    call op_fetch                   ; claims, reads, expands (20.12.7.4)
    mov ax, 0                       ; `mov` and not `xor`: CF is the answer
    jc .out
    mov al, CS_PART_ART
    call op_seg                     ; AX = the bands, at the claim's base
.out:                               ; plus its head slack
    ret

; -----------------------------------------------------------------------------
csl_entry:
    call op_load                    ; FIRST, for SPEC.md 20.2's reason: SI is
    jc .no                          ; an offset into the KERNEL's segment at a
                                    ; buffer the loader reuses on the next
                                    ; launch. A REFUSAL IS FATAL HERE and it
                                    ; was not in 88.10.3: a body that did not
                                    ; arrive is not a plainer title page

    mov al, CS_PART_BODY
    call op_seg
    or ax, ax
    jz .no
    mov dx, ax                      ; DX = where the program is

    push dx                         ; ...and the ART
    call csl_art                    ; AX = the expanded bands, or 0
    pop dx
    push ax

    ; --- the handoff, into the head of the program's bss --------------------
    mov es, dx
    mov di, [es:LD_H_IMG]           ; the bss begins here, which the part's own
    mov word [es:di+CSH_MAGIC], 'CS'
    pop ax
    mov [es:di+CSH_ART], ax         ; header says

    ; --- and the world DIRECTORY (SPEC.md 88.10.5) --------------------------
    ; Nine rows of (sector, packed length), straight out of the part table -
    ; which is in THIS image and about to stop existing, so the program cannot
    ; read it for itself. The cluster size goes with them: it is the one thing
    ; about the volume OSAPI_FILE_READ_AT needs and nothing in the program
    ; could work out.
    push ax
    mov ax, [op_clb]
    mov [es:di+CSH_CLB], ax
    xor cx, cx                      ; CX = the row we are copying
.dir:
    mov ax, cx
    add al, CS_PART_WLD0
    call op_row                     ; SI -> the table row, AX preserved
    mov ax, cx
    shl ax, 1
    shl ax, 1
    add ax, CSH_WDIR
    add ax, di
    xchg ax, bx
    mov ax, [si+OP_R_OFF]
    mov [es:bx], ax
    mov ax, [si+OP_R_LEN]
    mov [es:bx+2], ax
    inc cx
    cmp cx, CSH_NDIR
    jb .dir
    pop ax

    ; --- and the hand-over ---------------------------------------------------
    ; AX is what the kernel bounds the part's image + bss against, so it is OUR
    ; word for what is actually there (SPEC.md 20.12.10.4). The part is padded
    ; to image + bss, so its own two header fields ARE that length - said by
    ; adding them rather than by a constant this file would have to keep in
    ; step with the other one.
    mov ax, [es:LD_H_IMG]
    add ax, [es:LD_H_BSS]
    call OSAPI_PKG_REHOME
    jc .no
    xor bx, bx                      ; NO WINDOW: ours is the region that is
    clc                             ; about to be freed (SPEC.md 20.12.10.6)
    ret
.no:
    stc                             ; ...and the kernel tears down what exists.
    ret                             ; op_load has already said why in a toast

; --- the table, and the standard's own code after it (SPEC.md 20.12.3) ------
    OS88_PARTS_BEGIN CS_NPARTS
      OS88_PART OP_SEG,   OP_COMP   ; 0 THE PROGRAM: a whole .o88 image, its
                                    ;   bss shipped inside it because the
                                    ;   kernel does not zero a part (20.12.10).
                                    ;   OP_COMP is what buys the disk back -
                                    ;   13,777 of those bytes are the bss, and
                                    ;   a run of zeros is what LZ4 is best at
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP
                                    ; 1 the title bands, RAW from
                                    ;   tools/csart.py and packed by
                                    ;   os88pkg.py. LAZY so a refusal is a
                                    ;   plainer title page and not a refused
                                    ;   launch: an EAGER row would fit the
                                    ;   carve since SPEC.md 20.12.11, but the
                                    ;   carve is all-or-nothing and the bands
                                    ;   would become a reason not to fly
                                    ;   (88.10.4.1)
      ; --- and the WORLDS (SPEC.md 88.10.5): the shared vocabulary, then the
      ;     eight world blobs, each an LZ4 stream tools/csworlds.py packed.
      ;     ALL LAZY, and none of them is ever fetched by THIS image: what the
      ;     program gets is a DIRECTORY of where each one sits in the file, and
      ;     it reads the one it wants with OSAPI_FILE_READ_AT. A lazy row costs
      ;     nothing until it is fetched and is not in the run - and none of
      ;     them is in the program's way at launch.
      %rep CSH_NDIR
        OS88_PART OP_ASSET, OP_LAZY
      %endrep
    OS88_PARTS_END

    OS88_BSS OP_BSS
    OS88_IMAGE_END
