; =============================================================================
; os8088 - apps/pixelstein/pxgen.inc
;
; THE STATIC LADDERS and what wave 1 has of THE GENERATOR (SPEC.md 96.3).
;
; Two Duff ladders, in the image because `row(r) = r * 80` is known at
; assembly time: eighty `mov [di + 256 + r*80], reg` from row 79 down to row
; 0 and a near ret. Entering at store (80 - n) draws the LAST n stores - rows
; n-1..0 - and a DI biased by the first row's offset lands them on any range,
; so one ladder is the ceiling, the floor and the wall. The tone is a PAIR:
; even rows store one register and odd rows the other, so a 1bpp backend's
; dither is a checkerboard, and px_lad swaps the pair for a range that starts
; on an odd row. The 256 bias makes every store the 4-byte disp16 form the
; bench priced (rows 0 and 1 would otherwise assemble short).
;
; The generator, in this wave, is a COPY: the four quadrant bodies of
; pxcast.inc are moved into the scratch part the loader carved (96.9's part
; 2) when there is one, and left in the image when there is not. Either way
; px_qoff/px_qcur name where they are and the cast far-calls them (96.2.1).
; =============================================================================

; --- the byte ladder: BL on even rows, BH on odd ------------------------------
px_ladb:
%assign PXR PX_ROWS - 1
%rep PX_ROWS
%if PXR & 1
    mov [di + 256 + PXR * PX_STRIDE], bh
%else
    mov [di + 256 + PXR * PX_STRIDE], bl
%endif
%assign PXR PXR - 1
%endrep
    ret

; --- the word ladder (Resolution: Low res): AX on even rows, DX on odd -------
px_ladw:
%assign PXR PX_ROWS - 1
%rep PX_ROWS
%if PXR & 1
    mov [di + 256 + PXR * PX_STRIDE], dx
%else
    mov [di + 256 + PXR * PX_STRIDE], ax
%endif
%assign PXR PXR - 1
%endrep
    ret
PX_LADSTEP  equ 4                   ; bytes a store, both ladders
; ...AND THE ASSEMBLER CHECKS IT (pxcast.inc's PXB_SIZE guard, the same
; hazard): px_lad enters a ladder at (PX_ROWS - n) * PX_LADSTEP by a computed
; jmp, which is right only while EVERY store is the 4-byte disp16 form. The
; +256 bias is what keeps rows 0 and 1 out of disp8, and a change to the
; bias, PX_STRIDE or PX_ROWS that let one store assemble short would land
; every entry above it inside an instruction - nothing fails at assembly and
; nothing fails at the first frame. Zero bytes, and the comment above stops
; being something the reader is asked to believe.
%if (px_ladw - px_ladb) != PX_ROWS * PX_LADSTEP + 1
%error "px_ladb is not PX_ROWS stores of PX_LADSTEP bytes plus a ret"
%endif
%if ($ - px_ladw) != PX_ROWS * PX_LADSTEP + 1
%error "px_ladw is not PX_ROWS stores of PX_LADSTEP bytes plus a ret"
%endif
%if PX_LADSTEP != 4
%error "px_lad scales the entry by two shifts: PX_LADSTEP is 4 or px_lad changes"
%endif

; r * 80 for r = 0..79: the ladder entry's DI bias, a table rather than two
; shifts and an add per call (SPEC.md 85.3.4's tk_rowoff)
px_row80:
%assign PXR 0
%rep PX_ROWS
    dw PXR * PX_STRIDE
%assign PXR PXR + 1
%endrep

; -----------------------------------------------------------------------------
; px_lad - n rows of one tone pair down a column (DS = the destination)
; in:  AL = the first row, CX = the count (1..80), BX = the tone pair (BL the
;      even-row byte, BH the odd), [cs:px_cdi] = the column's base byte - 256
; clobbers AX, BX, DX, SI, DI
; -----------------------------------------------------------------------------
px_lad:
    xor ah, ah
    mov si, ax
    shl si, 1
    mov di, [cs:px_row80 + si]
    add di, [cs:px_cdi]
    test al, 1
    jz .even
    xchg bl, bh                     ; the pair follows the row's parity
.even:
    mov si, PX_ROWS
    sub si, cx
    shl si, 1
    shl si, 1                       ; * PX_LADSTEP (4, asserted above)
    add si, [cs:px_ladp]            ; px_ladb or px_ladw: the resolution's
    cmp byte [cs:px_bpc], 1
    jne .word
    jmp si                          ; the ladder's ret is ours
.word:
    mov ah, bl
    mov al, bl                      ; AX = the even byte, twice
    mov dh, bh
    mov dl, bh                      ; DX = the odd
    jmp si

; -----------------------------------------------------------------------------
; px_gen_init - where the bodies live for this instance, once at entry
; in:  [px_hand + PXH_GEN] = the scratch part's segment, or 0
; out: [px_bseg], px_qoff[4], px_qcur/px_qcurv/px_qcurh's segments
; clobbers AX, CX, SI, DI, ES
; -----------------------------------------------------------------------------
px_gen_init:
    mov ax, [px_hand + PXH_GEN]
    or ax, ax
    jz .image
    mov es, ax                      ; the copy: the four bodies at offset 0
    xor di, di
    mov si, px_bodies
    mov cx, PXB_ALL
    cld
    rep movsb
    xor si, si                      ; ...so body q is at q * PXB_SIZE
    jmp short .tabs
.image:
    mov ax, ds                      ; no scratch: they run where they are
    mov si, px_bodies
.tabs:
    mov [px_bseg], ax
    mov [px_qcur + 2], ax
    mov [px_qcurv + 2], ax
    mov [px_qcurh + 2], ax
    mov [px_qoff + 0], si
    add si, PXB_SIZE
    mov [px_qoff + 2], si
    add si, PXB_SIZE
    mov [px_qoff + 4], si
    add si, PXB_SIZE
    mov [px_qoff + 6], si
    ret
