; =============================================================================
; os8088 - apps/skies/csload.asm
;
; CLEAR SKIES' LOADER - the IMAGE of SKIES.O88, and the whole of what the
; kernel launches (SPEC.md 88.10.4, 20.12.10).
;
; It reads the program part, tells it where its streams are, and asks the
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
CS_PART_STR  equ 1              ; ...and csw.z: every stream the PROGRAM reads
                                ; for itself - the shared vocabulary, the eight
                                ; worlds and the title bands (SPEC.md 88.10.7)
CS_NPARTS    equ 2

; --- the handoff, at the head of the PROGRAM's bss (SPEC.md 20.12.10.2) ------
; ONE PACKAGE, TWO SOURCES: apps/skies/skies.asm declares these and this file
; is the other end of them. The kernel is not involved and has no opinion; it
; does not zero a part, which is the whole of what makes this work.
CSH_MAGIC  equ 0                ; word: 'CS' - the loader ran
CSH_SEC    equ 2                ; word: the sector csw.z begins at
CSH_CLB    equ 4                ; word: this volume's bytes per cluster

LD_H_IMG   equ 8                ; ...and the two header fields it reads them
LD_H_BSS   equ 10               ; at, which are the FORMAT's and not ours

; -----------------------------------------------------------------------------
csl_entry:
    call op_load                    ; FIRST, for SPEC.md 20.2's reason: SI is
    jc .no                          ; an offset into the KERNEL's segment at a
                                    ; buffer the loader reuses on the next
                                    ; launch. A REFUSAL IS FATAL HERE: a body
                                    ; that did not arrive is not a plainer
                                    ; title page

    mov al, CS_PART_BODY
    call op_seg
    or ax, ax
    jz .no

    ; --- the handoff, into the head of the program's bss --------------------
    ; WHERE csw.z IS AND WHAT A CLUSTER IS, and nothing else. The part table
    ; is in THIS image and about to stop existing, so the program cannot read
    ; it for itself; the streams' offsets inside csw.z are the program's own
    ; (tools/csworlds.py writes them into its index), and the cluster size is
    ; the one thing about the volume OSAPI_FILE_READ_AT needs and nothing in
    ; the program could work out. THE ART IS NOT FETCHED HERE ANY MORE
    ; (88.10.7): the program reads it into the bss union it shares with the
    ; bracket's tables, so this image has no claim to make for it.
    mov dx, ax                      ; DX = where the program is, which is
    mov es, ax                      ; OSAPI_PKG_REHOME's own input (20.12.10)
    mov di, [es:LD_H_IMG]           ; the bss begins here, which the part's own
    mov word [es:di+CSH_MAGIC], 'CS' ; header says
    mov ax, [op_clb]
    mov [es:di+CSH_CLB], ax
    mov al, CS_PART_STR
    call op_row                     ; SI -> the table row
    mov ax, [si+OP_R_OFF]
    mov [es:di+CSH_SEC], ax

    ; --- and the hand-over --------------------------------------------------
    ; In the KEEP form (SPEC.md 20.12.10.5): the standard works out, from the
    ; table, where the carve's last part ends, and the kernel cuts the carve
    ; there - so the read's margin past the program goes back to the heap
    ; instead of standing in the region for the session. The same figure is
    ; what bounds the part's image + bss (20.12.10.4).
    OS88_REHOME
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
                                    ;   most of those bytes are the bss, and a
                                    ;   run of zeros is what LZ4 is best at
      OS88_PART OP_ASSET, OP_LAZY | OP_SELF
                                    ; 1 csw.z, which tools/csworlds.py packed
                                    ;   stream by stream: LAZY so it is not in
                                    ;   the run, and never fetched by THIS
                                    ;   image - the program reads each stream
                                    ;   it wants with OSAPI_FILE_READ_AT, so
                                    ;   OP_SELF tells the standard to carry no
                                    ;   op_fetch for it (SPEC.md 20.12.9.1):
                                    ;   2,102 -> 1,456 bytes, five sectors to
                                    ;   three, so SKIES.O88 is 1,024 bytes
                                    ;   shorter on the disk. It
                                    ;   was NINE rows and the art a tenth, and
                                    ;   os88pkg.py starts every part on a
                                    ;   sector, so the file carried 2,278
                                    ;   bytes of padding between them
                                    ;   (88.10.7)
    OS88_PARTS_END

    OS88_BSS OP_BSS
    OS88_IMAGE_END
