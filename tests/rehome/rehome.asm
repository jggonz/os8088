; =============================================================================
; os8088 - tests/rehome/rehome.asm
;
; REHOME - the consumer that proves SPEC.md 20.12.10, a LOADER handing its
; identity to one of its own parts. A TEST package: `make rehome` builds it
; and no shipped floppy carries it, like tests/multiseg (SPEC.md 78.9).
;
; THIS FILE IS THE IMAGE, and its whole job is to stop existing:
;
;   1. op_load claims the carve and reads both parts;
;   2. op_seg says where each one landed;
;   3. the vector - a magic, the asset's segment and the carve's base - is
;      written into the HEAD OF THE PROGRAM'S BSS, which is inside part 0 and
;      which the kernel will not zero (SPEC.md 20.12.10.2);
;   4. OSAPI_PKG_REHOME names part 0 and the bytes available at it;
;   5. it returns CF=0 with BX=0 - NO WINDOW, which SPEC.md 20.12.10.6 makes a
;      refusal, its region being about to be freed.
;
; Then ld_start's step 8a frees this image's region, re-owns the carve to the
; instance slot, and runs step 8 again against part 0. The program's window is
; the one the user sees and this package's name is never on the screen.
;
; WHY THE IMAGE IS PADDED ODD is tests/multiseg's finding and applies here for
; the same reason: a part starts on a 512-byte boundary in the FILE, and
; op_claim's head slack (SPEC.md 20.12.2) is what bridges that to the cluster
; boundary OSAPI_FILE_READ_AT will start a read on - 1KB on a 360KB disk, 512
; bytes on a 1.44MB one. Unpadded the first part lands on a cluster on BOTH
; geometries, the slack is zero, and the arithmetic this row exists for never
; runs. IT MATTERS MORE HERE THAN THERE: a zero slack makes part 0's segment
; equal the carve's base, which is precisely the case SPEC.md 50.3.4 says the
; old fence answered correctly by accident.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'REHOME', rh_entry, OS88_F_PARTS

%include "os88parts.inc"

RH_PARTS   equ 2
RH_PROG    equ 0                    ; part 0: a whole .o88 image
RH_ASSET   equ 1                    ; part 1: bytes the program looks for

; --- the program's own layout, which this loader has to know ----------------
; ONE PACKAGE, TWO SOURCES: tests/rehome/rhprog.asm declares these and this
; file is the other end of them. The kernel is not involved and has no opinion.
RP_HAND    equ 0                    ; word: 'RH'
RP_ASSET   equ 2                    ; word: where part 1 went
RP_CARVE   equ 4                    ; word: the carve's own base
LD_H_IMG   equ 8                    ; ...and the two header fields it reads
LD_H_BSS   equ 10                   ; them at, which are the format's

    OS88_PARTS_BEGIN RH_PARTS
      OS88_PART OP_SEG              ; 0 the PROGRAM - kind OP_SEG because that
                                    ;   is what it is, though the standard does
                                    ;   nothing differently for it: the kernel
                                    ;   learns it is an image from the header
                                    ;   it validates at step 8a, not from here
      OS88_PART OP_ASSET            ; 1 the asset
    OS88_PARTS_END

; -----------------------------------------------------------------------------
; rh_entry - package entry (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the name of the file we
;      came out of, gfx lock NOT held
; out: BX = 0, CF clear - and the kernel re-homes instead of publishing us
;
; op_load IS THE FIRST THING CALLED, for SPEC.md 20.2's reason: SI is an offset
; into the KERNEL's segment at a buffer the loader reuses on the next launch.
; -----------------------------------------------------------------------------
rh_entry:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    call op_load                    ; CF=1: it has already said why
    jc .refuse

    mov al, RH_PROG                 ; --- where the program landed
    call op_seg
    or ax, ax
    jz .refuse
    mov [rh_pseg], ax

    mov al, RH_ASSET                ; --- ...and the asset
    call op_seg
    or ax, ax
    jz .refuse
    mov [rh_aseg], ax

    ; --- the handoff, into the head of the program's bss --------------------
    ; The bss begins at LD_H_IMG bytes into the part, which the part's own
    ; header says. NOTHING PUBLISHED, NOTHING STAMPED: both ends are this
    ; package's code (SPEC.md 20.12.10.2).
    mov es, [rh_pseg]
    mov di, [es:LD_H_IMG]
    mov word [es:di+RP_HAND], 'RH'
    mov ax, [rh_aseg]
    mov [es:di+RP_ASSET], ax
    mov ax, [op_base]               ; the carve's own base, for the program's
    mov [es:di+RP_CARVE], ax        ; fourth check - it may not free this

    ; --- and the hand-off itself -------------------------------------------
    ; In the KEEP form (SPEC.md 20.12.10.5): the standard keeps the carve to
    ; the end of its last part - the ASSET, here, which the program goes on
    ; reading - and the kernel cuts the read's margin past it. tests/rehome.py
    ; asserts the claim ends exactly there.
    mov dx, [rh_pseg]
    OS88_REHOME
    jc .refuse
    xor bx, bx                      ; NO WINDOW (SPEC.md 20.12.10.6): ours is
    clc                             ; the region that is about to be freed
    jmp short .out
.refuse:
    stc                             ; ...and the loader tears down what exists
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    mov di, 0xDEAD                  ; **DI IS CLOBBERED ON PURPOSE, and AFTER
                                    ; the pops so nothing puts it back.** An
                                    ; entry proc promises the kernel nothing
                                    ; about DI, and ld_start's step 8a needs it
                                    ; for ld_slot - which it did not set, so
                                    ; the arm read whatever the loader happened
                                    ; to leave and this fixture happened to
                                    ; leave the right thing. Clear Skies'
                                    ; loader did not, and its program was
                                    ; refused every claim it made while the
                                    ; loader's own region leaked (SPEC.md
                                    ; 88.10.4). This line is what makes the row
                                    ; see it: with step 8a's `mov di, [ld_rec]`
                                    ; taken out, the carve comes back owned by
                                    ; 0x06C0 and two claims stand on the slot
    ret

; --- the image is padded to an ODD number of sectors, deliberately ----------
; THREE, and the reason is in the header comment: a zero head slack makes part
; 0's segment equal the carve's base, which is the one case SPEC.md 50.3.4's
; old fence answered correctly by accident - so this row would pass without
; testing the thing it is for. If the code outgrows it `times` goes negative
; and NASM says so, which is the right failure: somebody has to pick the next
; ODD multiple and check the row still means something.
    times 1536 - ($ - $$) db 0

    OS88_BSS OP_BSS + RH_BSS
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) ----------------------------------
; THE STANDARD'S WORDS COME FIRST (apps/os88parts.inc), and ours follow.
rh_pseg    equ os88_image_end + OP_BSS + 0
rh_aseg    equ os88_image_end + OP_BSS + 2
RH_BSS     equ 4
