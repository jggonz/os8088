; =============================================================================
; os8088 - apps/pixelstein/pxstein.asm
;
; PIXELSTEIN 3D's LOADER - the IMAGE of PXSTEIN.O88, and the whole of what the
; kernel launches (SPEC.md 96.9, 20.12.10; apps/skies/csload.asm's shape).
;
; It reads the parts, tells the program what it cannot ask for itself, and
; asks the kernel to treat the program as the program. Then its region is
; freed and it is gone: what runs is apps/pixelstein/pxgame.asm, at PART 0,
; in the parts carve, with an instance and a window and a name of its own.
;
; THREE PARTS (96.9):
;   0  the program, a whole .o88 image with its bss shipped inside it,
;      OP_COMP so the disk pays for the zeros of two 4KB maps and two
;      spotvis arrays as a run of nothing;
;   1  the level stream tools/pxslevel.py writes (96.7) - EAGER and plain:
;      ~1KB, three sectors of the run. The plan carried it lazy, but the
;      parts table dies with this image, so a lazy part needs a directory
;      the program reads back through OSAPI_FILE_READ_AT (what SKIES does
;      for a world) - worth it for 60KB of art in wave 2, not for this;
;   2  1KB of scratch the program copies its four DDA bodies into (96.2.1),
;      OP_OPT: a machine that cannot spare it runs them out of the image.
;      Wave 2's scaler set and column driver grow this row.
;
; IT MUST NOT CREATE A WINDOW (SPEC.md 20.12.10.6): its region is about to be
; freed, so a window whose W_SEG named it would far-call a dead claim on its
; first repaint. The program's window is the one the user sees.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Pixelstein 3D', pxl_entry, OS88_F_ICON | OS88_F_PARTS, OS88_STACK_DEFAULT

    OS88_ICON16                     ; the SAME icon the program carries (the
%include "pxicon.inc"               ; Disk window draws this one before the
    OS88_ICON16_END                 ; launch, the dock the program's after)

%include "os88parts.inc"
%include "pxlev.inc"                ; for PXL_NLEV alone: the level count is
                                    ; the generator's and read here rather
                                    ; than restated

PX_PART_BODY equ 0                  ; the program - a whole .o88 image
PX_PART_LEV  equ 1                  ; the level stream (96.7)
PX_PART_GEN  equ 2                  ; the bodies' scratch (96.2.1), optional
PX_NPARTS    equ 3
PX_GENKB     equ 1                  ; ...and how much of it: the four bodies
                                    ; are 232 bytes; wave 2 asks for ~37KB

; --- the handoff, at the head of the PROGRAM's bss (SPEC.md 96.9) -----------
; ONE PACKAGE, TWO SOURCES: apps/pixelstein/pxgame.asm declares these and this
; file is the other end of them. The kernel is not involved: it does not zero
; a part, which is the whole of what makes this work.
PXH_MAGIC  equ 0                    ; word: 'PX' - the loader ran
PXH_LEV    equ 2                    ; word: the level stream's segment
PXH_GEN    equ 4                    ; word: the scratch part's segment, 0 = refused
PXH_COLD   equ 6                    ; word: the cold part's segment (96.9), 0 = none
PXH_NLEV   equ 8                    ; word: levels in the stream
PXH_LEVLEN equ 10                   ; word: the level stream's length in bytes
                                    ; (the row's OP_R_LEN): the bound the
                                    ; program holds the stream's own lengths
                                    ; to (96.7)
PXH_SIZE   equ 12

LD_H_IMG   equ 8                    ; ...and the two header fields it reads
LD_H_BSS   equ 10                   ; them at, which are the FORMAT's

; -----------------------------------------------------------------------------
; pxl_entry - the loader's entry proc (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the name of the file we
;      came out of, gfx lock NOT held
; out: BX = 0, CF clear - and the kernel re-homes instead of publishing us
; -----------------------------------------------------------------------------
pxl_entry:
    call op_load                    ; FIRST, for SPEC.md 20.2's reason: SI is
    jc .no                          ; an offset into the KERNEL's segment at a
                                    ; buffer the loader reuses on the next
                                    ; launch. A refusal is fatal: a body that
                                    ; did not arrive is not a plainer game
    mov al, PX_PART_BODY
    call op_seg
    or ax, ax
    jz .no
    mov es, ax                      ; ES = where the program is
    mov di, [es:LD_H_IMG]           ; its bss begins here, which the part's
                                    ; own header says
    mov word [es:di+PXH_MAGIC], 'PX'
    mov al, PX_PART_LEV
    call op_seg
    mov [es:di+PXH_LEV], ax
    call op_row                     ; SI -> the level row (csload.asm's use):
    mov ax, [si+OP_R_LEN]           ; its length is what bounds every length
    mov [es:di+PXH_LEVLEN], ax      ; READ OUT OF the stream (96.7)
    mov al, PX_PART_GEN             ; 0 when the optional row was refused:
    call op_seg                     ; op_seg answers 0 for a part that is not
    mov [es:di+PXH_GEN], ax         ; there, and the program falls back to
                                    ; its own copy of the bodies (96.2.1)
    mov word [es:di+PXH_COLD], 0    ; no cold part yet (96.9)
    mov word [es:di+PXH_NLEV], PXL_NLEV

    ; --- the hand-over -------------------------------------------------------
    ; AX is what the kernel bounds the part's image + bss against, so it is
    ; OUR word for what is actually there (SPEC.md 20.12.10.4): the part is
    ; padded to image + bss, so its own two header fields ARE that length.
    mov ax, [es:LD_H_IMG]
    add ax, [es:LD_H_BSS]
    mov dx, es
    call OSAPI_PKG_REHOME
    jc .no
    xor bx, bx                      ; NO WINDOW: ours is the region that is
    clc                             ; about to be freed (SPEC.md 20.12.10.6)
    ret
.no:
    stc                             ; ...and the kernel tears down what exists.
    ret                             ; op_load has already said why in a toast

; --- the table, and the standard's own code after it (SPEC.md 20.12.3) ------
    OS88_PARTS_BEGIN PX_NPARTS
      OS88_PART OP_SEG,   OP_COMP   ; 0 THE PROGRAM
      OS88_PART OP_ASSET            ; 1 the level stream, eager and plain
      OS88_PART OP_SEG,   OP_ZERO | OP_OPT, PX_GENKB   ; 2 the bodies' scratch
    OS88_PARTS_END

    OS88_BSS OP_BSS
    OS88_IMAGE_END
