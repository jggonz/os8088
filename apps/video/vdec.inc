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
    jmp vd_n_p1                     ; ...and the lists CHAINED, each ending in
                                    ; a jump to the next and the last in the
                                    ; ret: ten calls were ~250 cycles a frame

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

; VD_NPOKE name, store, c160, next - one Duff-entered unrolled list of
; fixed-size changes; `next` is the list its end jumps on to (0: it returns)
; VD_AE store, n - one absolute entry at unrolled position n
%macro VD_AE 2-3 0
.a%2:
    lodsw
%if %3
    shl ax, 1                       ; C160: a cell's attribute, at 2a + BP
%endif
    add ax, bp
    xchg ax, di
    %1
%endmacro

; VD_SKIP c160 - an entry's skip byte, onto DI: a cell is TWO screen bytes
; on C160 (AH = 0 throughout, so the doubled add needs no shift)
%macro VD_SKIP 1
    lodsb
    add di, ax
%if %1
    add di, ax
%endif
%endmacro

%macro VD_NPOKE 2-4 0, 0
%if %3
%assign VD_U 4                      ; C160's stores are twice the bytes: a
%else                               ; round of four keeps the body in reach
%assign VD_U 8
%endif
%1:
.seg:
    lodsb
    or al, al
    jz .done
    js .abs
    mov cx, ax                      ; AH = 0, so CX = the count
    lodsw
%if %3
    shl ax, 1
%endif
    add ax, bp
    xchg ax, di
    xor ax, ax
    mov bx, cx
    add cx, VD_U - 1                ; the rounds, the partial one a round too
    shr cx, 1
    shr cx, 1
%if VD_U == 8
    shr cx, 1
%endif
    and bx, VD_U - 1                ; ...entered at count mod U, a whole
    shl bx, 1                       ; round (.tab's first) at 0
    jmp [cs:bx + .tab]
%assign i VD_U
%rep VD_U
.r %+ i:
    VD_SKIP %3
    %2
%assign i i-1
%endrep
    loop .r %+ VD_U
    jmp short .seg
.abs:
    and al, 0x7F                    ; 80h: a count of 0 is no segment, and
    jz .seg                         ; not the 65,536 rounds `loop` makes of it
    mov cx, ax
    mov bx, ax
    add cx, VD_U - 1
    shr cx, 1
    shr cx, 1
%if VD_U == 8
    shr cx, 1
%endif
    and bx, VD_U - 1
    shl bx, 1
    jmp [cs:bx + .atab]
%assign i VD_U
%rep VD_U
    VD_AE %2, i, %3
%assign i i-1
%endrep
    loop .a %+ VD_U
    xor ax, ax                      ; the invariant, back
    jmp .seg
.done:
%ifidn %4, 0
    ret
%else
    jmp %4                          ; the next list (the invariants hold)
%endif
.tab:
    dw .r %+ VD_U
%assign i 1
%rep VD_U - 1
    dw .r %+ i
%assign i i+1
%endrep
.atab:
    dw .a %+ VD_U
%assign i 1
%rep VD_U - 1
    dw .a %+ i
%assign i i+1
%endrep
%endmacro

    VD_NPOKE vd_n_p1, VD_ST1, 0, vd_n_p2
    VD_NPOKE vd_n_p2, VD_ST2, 0, vd_n_p3
    VD_NPOKE vd_n_p3, VD_ST3, 0, vd_n_p4
    VD_NPOKE vd_n_p4, VD_ST4, 0, vd_n_p5
    VD_NPOKE vd_n_p5, VD_ST5, 0, vd_n_p6
    VD_NPOKE vd_n_p6, VD_ST6, 0, vd_n_slice

; VD_NSPAN name, entry, c160, next - a Duff-entered list of spans, four to a
; round, ending in a jump to `next` as VD_NPOKE does. The
; round counter is DX, because a span's own rep needs CX (left 0 by it, which
; the next entry's `mov cl` relies on)
%macro VD_NSPAN 2-4 0, 0
%1:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax                      ; the count
    lodsw
%if %3
    shl ax, 1
%endif
    add ax, bp
    xchg ax, di
    xor ax, ax
    xor cx, cx
    mov bx, dx
    add dx, 3                       ; rounds of four, the partial one too
    shr dx, 1
    shr dx, 1
    and bx, 3
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
%ifidn %4, 0
    ret
%else
    jmp %4
%endif
.tab:
    dw .r4, .r1, .r2, .r3
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

    VD_NSPAN vd_n_slice, VD_SLICE, 0, vd_n_run
    VD_NSPAN vd_n_run, VD_RUN, 0, vd_n_slicel

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
    jmp vd_n_runl

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

%ifdef VD_C160
; =============================================================================
; THE C160 DECODER (SPEC.md 98.3.12.1): the same ten lists, written STRAIGHT
; to the text screen. A C160 file's addresses are the PACKED image's - a byte
; per cell, the attribute - and the screen holds each at the odd address of a
; two-byte cell, so canvas byte a is screen byte 2a + BP, BP being twice the
; packed origin plus one. It replaced a decode into a RAM shadow and a copy of
; every dirty row at the stride of two, which measured FOUR TIMES the decode:
; 67% of a 5150 against 16% on a 160 x 75 canvas (VIDEO-PLAN 15.8).
;
; Every change is its bytes at every other address: `movsb / inc di` each,
; the character byte between them never touched (it is 0DEh for the whole
; play, vp_c16fill). A RUN writes the cells whole - `rep stosw` of 0DEh and
; the value - which is the one place the character is stored, as itself.
;
; in:       DS:SI = the ten lists, ES = the text screen, BP = 2 x origin + 1
; out:      SI past the last list
; clobbers: AX, BX, CX, DX, DI, flags. DS, ES and BP are preserved
; =============================================================================
vd_c160:
    cld
    xor ax, ax
    xor cx, cx
    jmp vd_c_p1                     ; (chained, as vd_native's are)

%macro VD_CST1 0
    movsb
    inc di
%endmacro
%macro VD_CST2 0
    VD_CST1
    VD_CST1
%endmacro
%macro VD_CST3 0
    VD_CST2
    VD_CST1
%endmacro
%macro VD_CST4 0
    VD_CST2
    VD_CST2
%endmacro
%macro VD_CST5 0
    VD_CST4
    VD_CST1
%endmacro
%macro VD_CST6 0
    VD_CST4
    VD_CST2
%endmacro

    VD_NPOKE vd_c_p1, VD_CST1, 1, vd_c_p2
    VD_NPOKE vd_c_p2, VD_CST2, 1, vd_c_p3
    VD_NPOKE vd_c_p3, VD_CST3, 1, vd_c_p4
    VD_NPOKE vd_c_p4, VD_CST4, 1, vd_c_p5
    VD_NPOKE vd_c_p5, VD_CST5, 1, vd_c_p6
    VD_NPOKE vd_c_p6, VD_CST6, 1, vd_c_slice

; a SLICE: skip, len8, the bytes - a cell's attribute each, eight to a round
%macro VD_CSLICE 0
    VD_SKIP 1
    lodsb
    mov cx, ax                      ; the length (7..255, AH = 0)
    call vd_c_mov
%endmacro

; a RUN: skip, then len8 and the value - the cells whole, 0DEh and the value
%macro VD_CRUN 0
    VD_SKIP 1
    lodsw                           ; AL = the length, AH = the value
    mov cl, al                      ; CH = 0: the last rep left CX = 0
    mov al, 0xDE
    dec di                          ; (the cell's character byte)
    rep stosw
    inc di
    xor ah, ah                      ; the invariant, back
%endmacro

    VD_NSPAN vd_c_slice, VD_CSLICE, 1, vd_c_run
    VD_NSPAN vd_c_run, VD_CRUN, 1, vd_c_slicel

; vd_c_mov - CX (>= 1) bytes from DS:SI, each to an odd address from ES:DI:
; eight to a round, entered Duff-style. Out: CX = 0, BX clobbered
vd_c_mov:
    mov bx, cx
    and bx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    or bx, bx
    jz .r8
    inc cx
    shl bx, 1
    jmp [cs:bx + .tab]
.r8:
    VD_CST1
.r7:
    VD_CST1
.r6:
    VD_CST1
.r5:
    VD_CST1
.r4:
    VD_CST1
.r3:
    VD_CST1
.r2:
    VD_CST1
.r1:
    VD_CST1
    loop .r8
    ret
.tab:
    dw 0, .r1, .r2, .r3, .r4, .r5, .r6, .r7

vd_c_slicel:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax
    lodsw
    shl ax, 1
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_SKIP 1
    lodsw
    mov cx, ax
    call vd_c_mov
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.done:
    jmp vd_c_runl

vd_c_runl:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax
    lodsw
    shl ax, 1
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_SKIP 1
    lodsw
    mov cx, ax                      ; the length
    lodsb
    mov ah, al                      ; the value, over 0DEh
    mov al, 0xDE
    dec di
    rep stosw
    inc di
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.done:
    ret
%endif
