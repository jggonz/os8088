; =============================================================================
; os8088 - apps/video/vdec.inc
;
; THE VIDEO PLAYER'S FRAME DECODER (SPEC.md 98.1.3, 98.3): VIDEO.O88's, and
; tests/vidbench's, which is how wave 0 measured it against XDC's own code
; (docs/plans/VIDEO-PLAN.md 2.2). One copy, included by both.
;
; A frame's video part is TEN lists, in this order, each ending in a 0 byte:
;
;   list    = segment* 00
;   segment = count(1..127) address(16) entry * count     skip-coded
;           | 80h+count(1..127)         aentry * count    absolute (P1..P6)
;   entry   = skip, then the change      aentry = address(16), the change
;   P1..P6  = 1..6 bytes                 (a list per length)
;   SLICE   = len8 bytes (7..255)        RUN  = len8 value (6..255)
;   SLICEL  = len16 bytes (256+)         RUNL = len16 value (256+)
;
; A LIST PER SHORT LENGTH is wave 0's first finding: a 3-6 byte change sent
; through `rep movsw` + `rep movsb` measured +40-60 cycles against XDC's
; unrolled stores, all of it rep start-up. Here each length's store is
; straight-line - `movsw / movsb` for 3, as XDC writes it - so the only cost
; over XDC is reading the skip.
;
; ABSOLUTE SEGMENTS are the second: a skip segment's set-up measured ~228
; cycles, and half of all segments held ONE entry. An isolated change goes
; into a pooled absolute segment instead - `lodsw / add ax,bp / xchg ax,di`
; and the store, ~11 cycles more than a skip entry and no set-up of its own.
;
; `skip` is the distance from the end of the previous write in the segment
; (from the segment's address for its first entry). tools/os88vid.py's
; to_lists() writes it and decode_lists() is the reference this must match.
;
; ONE DECODER. The addresses are the target adapter's own memory image - a
; file is laid out for its surface on the HOST - so every entry applies as it
; stands, with BP added to every segment address to place the canvas (a
; window's content origin, a letterbox). Wave 0 built the other design too:
; a row-table decoder that placed a canvas on a surface of another layout at
; playback. It cost ~480 cycles a row change (the frames change rows almost
; every entry) and it forced every span to be split at a row end, which is
; what turned the heaviest frames into 290 runs where XDC has one rep stosb.
; A file on the wrong adapter is decoded here into a RAM shadow and its dirty
; band copied to the screen instead (VIDEO-PLAN 3).

; THE INNER LOOPS ARE XDC's STORES WITH THE ADDRESS FACTORED OUT. A 1-byte
; change is `lodsb / add di,ax / movsb` - AH = 0 is an invariant of every
; loop here, so the skip needs no zero-extend - against XDC's
; `mov di,imm16 / movsb`. The pokes are unrolled eight times and entered
; Duff-style on the segment's count, so there is no per-entry loop control.
;
; in:       DS:SI = the ten lists, ES = the surface segment,
;           BP = added to every segment address
; out:      SI past the last list
; clobbers: AX, BX, CX, DX, DI, BP, flags. DS and ES are preserved.
; =============================================================================

; -----------------------------------------------------------------------------
; the native decoder
; -----------------------------------------------------------------------------
vd_native:
    cld
    xor ax, ax
    xor cx, cx                      ; the run list sets only CL: CH must be 0
    call vd_n_p1
    call vd_n_p2
    call vd_n_p3
    call vd_n_p4
    call vd_n_p5
    call vd_n_p6
    call vd_n_slice
    call vd_n_run
    call vd_n_slicel
    call vd_n_runl
    ret

; the stores for a change of 1..6 bytes - XDC_CODE.PAS's own unrolling
%macro VD_ST1 0
    movsb
%endmacro
%macro VD_ST2 0
    movsw
%endmacro
%macro VD_ST3 0
    movsw
    movsb
%endmacro
%macro VD_ST4 0
    movsw
    movsw
%endmacro
%macro VD_ST5 0
    movsw
    movsw
    movsb
%endmacro
%macro VD_ST6 0
    movsw
    movsw
    movsw
%endmacro

; VD_NPOKE name, store - one Duff-entered unrolled list of fixed-size changes
; VD_AE store, n - one absolute entry at unrolled position n
%macro VD_AE 2
.a%2:
    lodsw
    add ax, bp
    xchg ax, di
    %1
%endmacro

%macro VD_NPOKE 2
%1:
.seg:
    lodsb
    or al, al
    jz .done
    js .abs
    mov cx, ax                      ; AH = 0, so CX = the count
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
    mov bx, cx
    and bx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; whole rounds of eight
    or bx, bx
    jz .r8                          ; count is 8k: k >= 1 whole rounds
    inc cx                          ; the partial round is a round too
    shl bx, 1
    jmp [cs:bx + .tab]
.r8:
    lodsb
    add di, ax
    %2
.r7:
    lodsb
    add di, ax
    %2
.r6:
    lodsb
    add di, ax
    %2
.r5:
    lodsb
    add di, ax
    %2
.r4:
    lodsb
    add di, ax
    %2
.r3:
    lodsb
    add di, ax
    %2
.r2:
    lodsb
    add di, ax
    %2
.r1:
    lodsb
    add di, ax
    %2
    loop .r8
    jmp short .seg
.abs:
    and al, 0x7F
    mov cx, ax
    mov bx, cx
    and bx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    or bx, bx
    jz .a8
    inc cx
    shl bx, 1
    jmp [cs:bx + .atab]
    VD_AE %2, 8
    VD_AE %2, 7
    VD_AE %2, 6
    VD_AE %2, 5
    VD_AE %2, 4
    VD_AE %2, 3
    VD_AE %2, 2
    VD_AE %2, 1
    loop .a8
    xor ax, ax                      ; the invariant, back
    jmp .seg
.done:
    ret
.tab:
    dw 0, .r1, .r2, .r3, .r4, .r5, .r6, .r7
.atab:
    dw 0, .a1, .a2, .a3, .a4, .a5, .a6, .a7
%endmacro

    VD_NPOKE vd_n_p1, VD_ST1
    VD_NPOKE vd_n_p2, VD_ST2
    VD_NPOKE vd_n_p3, VD_ST3
    VD_NPOKE vd_n_p4, VD_ST4
    VD_NPOKE vd_n_p5, VD_ST5
    VD_NPOKE vd_n_p6, VD_ST6

; VD_NSPAN name, entry - a Duff-entered list of spans, four to a round. The
; round counter is DX, because a span's own rep needs CX (left 0 by it, which
; the next entry's `mov cl` relies on)
%macro VD_NSPAN 2
%1:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax                      ; the count
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
    xor cx, cx
    mov bx, dx
    and bx, 3
    shr dx, 1
    shr dx, 1                       ; whole rounds of four
    or bx, bx
    jz .r4
    inc dx
    shl bx, 1
    jmp [cs:bx + .tab]
.r4:
    %2
.r3:
    %2
.r2:
    %2
.r1:
    %2
    dec dx
    jnz .r4
    jmp .seg
.done:
    ret
.tab:
    dw 0, .r1, .r2, .r3
%endmacro

; a SLICE: skip, len8, the bytes
%macro VD_SLICE 0
    lodsb
    add di, ax                      ; the skip
    lodsb
    mov cx, ax                      ; the length (AH = 0)
    shr cx, 1
    rep movsw                       ; ~12.5 cycles a byte where movsb is 17
    adc cx, cx
    rep movsb
%endmacro

; a RUN: skip, then len8 and the value in ONE word
%macro VD_RUN 0
    lodsb
    add di, ax                      ; the skip
    lodsw                           ; AL = the length, AH = the value
    mov cl, al                      ; CH = 0: the last rep left CX = 0
    mov al, ah                      ; AX = the value twice
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb
    xor ah, ah                      ; the invariant, back
%endmacro

    VD_NSPAN vd_n_slice, VD_SLICE
    VD_NSPAN vd_n_run, VD_RUN

; the long ones: a span of 256+ bytes, len16 - rare, so a plain loop
vd_n_slicel:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    lodsb
    add di, ax
    lodsw
    mov cx, ax
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.done:
    ret

vd_n_runl:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    lodsb
    add di, ax
    lodsw
    mov cx, ax                      ; the length
    lodsb
    mov ah, al                      ; the value twice
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.done:
    ret
