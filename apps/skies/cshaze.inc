; =============================================================================
; THE HAZE (SPEC.md 88.4.1.1) - the horizon fades, at High and above
;
; The sky goes pale toward the horizon and the ground goes pale to meet it,
; which is the fading horizon every DOS flight simulator of the period had.
; It is FOUR PARALLEL LINES where the plain pass has one: the horizon, two
; into the sky (S and 2S, S a 32nd of the view) and ONE into the ground, S as
; well - three thin bands, which is what the field settled on (88.4.1.1.1):
; the first cut's eighth-of-a-view ground band reached the bases of buildings
; beside the aeroplane, and 1.5 S still read as tall. A pixel's
; ink is how many of the four it is on the ground side of:
;
;   0 SKY | 1 HZS2 | 2 HZS1 || 3 HZG1 | 4 GROUND
;
; and because the four are parallel and wound the same way, a row meets them
; in a fixed order and its ink steps by one at each crossing.
;
; THE LINES ARE OFFSET THE WAY THE HORIZON IS PARAMETERISED. In the FLAT form
; (|slope| < 1) the horizon is a row per column and its parallel is the same
; line moved H rows; in the STEEP form it is a column per row and the parallel
; is moved H pixels across. At the 45-degree seam the two agree, so the bands
; do not jump when the roll crosses it. The crossings are taken off the
; horizon's two ENDS (cs_hzya/cs_hzyb, cs_hzxa/cs_hzxb) and not out of cs_xl,
; because the moved lines reach rows the trace never did - a horizon just
; above the view still puts its ground bands into it.
;
; IT IS PRICED THE WAY THE PLAIN PASS IS (88.3.1), and the first build is why:
; a divide per line per row and six cs_hrun calls a row cost 351 ms of a
; 623 ms banked frame on the 8088. So:
;   - the crossings STEP: the horizon's x is a 16.16 accumulator a row, and
;     each line is that plus a constant, so a crossing is two adds;
;   - a row no line crosses INSIDE the view is one ink, and goes to
;     cs_hzrows - the plain pass's incremental refill, which leaves a row
;     alone when its kind has not changed. Level flight is almost all of
;     these;
;   - a row a line does cross is laid in ONE PASS by cs_hzfill, byte by byte,
;     each crossing's byte composed through the adapter's pixel mask - the
;     fused band's own method (88.3.1.3), with up to four crossings where it
;     has one - and its span is the crossings' bytes and one either side,
;     the band's rule (88.3.1.1). Mode X lays its runs through cs_hrun, its
;     filler selecting planes as it goes, and every Mode X row is refilled
;     anyway.
; Nothing writes a pixel twice, which is why this is a pass of its own and not
; polygons drawn over the ground.
; =============================================================================

CS_HZN equ 4                        ; the lines: the horizon, two into the
                                    ; sky and one into the ground (88.4.1.1.1)
CS_HZL equ 6                        ; a line: its x from the horizon's, whole
                                    ; pixels, and the first and last row it
                                    ; SPLITS

; -----------------------------------------------------------------------------
; cs_hazerows - cs_skyground's rows, with the bands. Entered by a JUMP from
; cs_skyground's .rows with its state as that label has it; returns for it.
; Clobbers everything
; -----------------------------------------------------------------------------
cs_hazerows:
    ; --- the five inks, by how many lines a pixel is on the ground side of,
    ;     and each one's pattern table --------------------------------------
    mov word [cs_hzik], CSI_SKY | (CSI_HZS2 << 8)
    mov word [cs_hzik + 2], CSI_HZS1 | (CSI_HZG1 << 8)
    mov byte [cs_hzik + 4], CSI_GROUND
    xor bx, bx
.pp:
    mov al, [cs_hzik + bx]
    call cs_inkpat
    push bx
    shl bx, 1
    mov [cs_hzpt + bx], si
    pop bx
    inc bx
    cmp bx, CS_HZN + 1
    jb .pp
    xor bx, bx                      ; ...and each row PHASE's five patterns,
.ph:                                ; so a row picks its table by one index
    mov di, bx                      ; DI = the phase x 8, then the ink
    mov cl, 3
    shl di, cl
    xor si, si
.pi:
    push si
    shl si, 1
    mov si, [cs_hzpt + si]
    mov al, [si + bx]
    pop si
    push di
    add di, si
    mov [cs_hzpp + di], al
    pop di
    inc si
    cmp si, CS_HZN + 1
    jb .pi
    inc bx
    cmp bx, 4
    jb .ph
    mov ax, 7                       ; the crossing byte's pixel mask, as the
    mov bx, cs_hlm                  ; plain pass's .mk takes it
    cmp byte [cs_kshift], 3
    je .mk
    mov ax, 3
    mov bx, cs_clm
.mk:
    mov [cs_hzmm], ax
    mov [cs_hzmt], bx
    ; --- which way the ground is --------------------------------------------
    xor ax, ax
    cmp byte [cs_hzl], CSI_GROUND
    jne .gl
    inc al                          ; 1: the ground is LEFT of a crossing, so
.gl:                                ; a row is met deepest ground first and
    mov [cs_hzgl], al               ; its ink steps toward the sky
    xor al, 1
    mov [cs_hzgr], al               ; ...and what a line wholly LEFT of the
    mov ax, 1                       ; view counts; the step across one
    cmp byte [cs_hzgl], 0
    je .dw
    neg ax
.dw:
    mov [cs_hzdw], ax
    xor ax, ax
    cmp byte [cs_hzab], CSI_GROUND
    jne .ag
    inc al
.ag:
    mov [cs_hzag], al               ; a row wholly ABOVE a line counts it...
    xor ax, ax
    cmp byte [cs_hzbl], CSI_GROUND
    jne .bg
    inc al
.bg:
    mov [cs_hzbg], al               ; ...and one wholly below
    ; --- the offsets, toward the GROUND, off the view's height, in the order
    ;     a row meets them left to right -------------------------------------
    mov ax, [cs_wh]
    mov cl, 5
    shr ax, cl
    inc ax                          ; S = a band: a 32nd of the view, the
    mov [cs_hzof], ax               ; ground's one the same (88.4.1.1.1)
                                    ; +S, 0, -S, -2S
    mov word [cs_hzof + 2], 0
    neg ax
    mov [cs_hzof + 4], ax
    shl ax, 1
    mov [cs_hzof + 6], ax
    neg ax
    mov [cs_hzw], ax                ; 2S, the furthest any line reaches
    cmp byte [cs_hzgl], 0
    jne .ord                        ; the ground on the RIGHT: the other way
    mov ax, [cs_hzof]               ; round - four entries, two swaps
    xchg ax, [cs_hzof + 6]
    mov [cs_hzof], ax
    mov ax, [cs_hzof + 2]
    xchg ax, [cs_hzof + 4]
    mov [cs_hzof + 2], ax
.ord:
    cmp byte [cs_hzform], 2
    jne .steep
    jmp .flat
    ; --- STEEP: x = xa + r (xb - xa)/(wh - 1); a line is the horizon moved
    ;     across, and it splits every row ------------------------------------
.steep:
    mov ax, [cs_hzxb]
    sub ax, [cs_hzxa]
    mov cx, [cs_wh]
    dec cx
    call cs_hzslope                 ; [cs_hzst] = the step a row, 16.16
    mov ax, [cs_hzxa]
    mov [cs_hzx + 2], ax            ; ...from row 0
    mov word [cs_hzx], 0
    xor si, si
    mov di, cs_hzln
.sl:
    mov ax, [cs_hzof + si]          ; moved INTO the ground: left of the
    cmp byte [cs_hzgl], 0           ; horizon when that is where it is
    je .sr
    neg ax
.sr:
    mov [di], ax
    mov word [di + 2], -32768
    mov word [di + 4], 32767
    add di, CS_HZL
    inc si
    inc si
    cmp si, CS_HZN * 2
    jb .sl
    xor ax, ax                      ; every row is in reach
    mov dx, [cs_wh]
    dec dx
    jmp .rc
    ; --- FLAT: x = wx0 + (r - ya)(wx1 - wx0)/(yb - ya); a line is the
    ;     horizon moved down or up, and splits the rows it spans --------------
.flat:
    mov ax, [cs_wx1]
    sub ax, [cs_wx0]
    mov cx, [cs_hzyb]
    sub cx, [cs_hzya]
    call cs_hzslope                 ; (a level line's step is never used)
    mov ax, [cs_hzya]               ; the accumulator is the horizon's x at
    neg ax                          ; row 0: wx0 - ya x step
    call cs_hzmul                   ; DX:AX = -ya x step
    add dx, [cs_wx0]
    mov [cs_hzx], ax
    mov [cs_hzx + 2], dx
    mov ax, [cs_hzya]
    mov dx, [cs_hzyb]
    cmp ax, dx
    jle .fo
    xchg ax, dx
.fo:
    mov [cs_hzyl], ax               ; the horizon's own rows
    mov [cs_hzyh], dx
    xor si, si
    mov di, cs_hzln
.fl:
    mov bx, [cs_hzof + si]          ; BX rows toward the ground: down when
    cmp byte [cs_hzbg], 0           ; the ground is below
    jne .fd
    neg bx
.fd:
    push bx
    mov ax, bx                      ; the line at row r is the horizon at
    neg ax                          ; row r - BX: x offset -BX x step
    call cs_hzmul
    add ax, 0x8000                  ; ...rounded to the pixel
    adc dx, 0
    mov [di], dx
    pop bx
    mov ax, [cs_hzyl]
    add ax, bx
    mov dx, [cs_hzyh]
    add dx, bx
    cmp ax, dx                      ; A LEVEL LINE splits no row: its own row
    jne .fs                         ; is its upper side's, as the plain pass
    inc ax                          ; lays it - first > last, and below
.fs:                                ; `first` reads as above
    mov [di + 2], ax
    mov [di + 4], dx
    add di, CS_HZL
    inc si
    inc si
    cmp si, CS_HZN * 2
    jb .fl
    mov ax, [cs_hzyl]               ; the rows in reach: the line's own,
    mov dx, [cs_hzyh]               ; widened by the furthest line either way
    mov bx, [cs_hzw]
    sub ax, bx
    add dx, bx
    ; --- the rows in reach, clamped to the view ------------------------------
.rc:
    or ax, ax
    jns .r1
    xor ax, ax
.r1:
    cmp ax, [cs_wh]                 ; a band wholly below the view: every
    jle .r2                         ; row is above it
    mov ax, [cs_wh]
.r2:
    cmp dx, [cs_wh]
    jl .r3
    mov dx, [cs_wh]
    dec dx
.r3:
    cmp dx, -1                      ; ...wholly above it: every row below
    jge .r4
    mov dx, -1
.r4:
    mov [cs_hze0], ax
    mov [cs_hze1], dx
    ; --- the accumulator to the first row in reach ---------------------------
    call cs_hzmul                   ; AX = e0: DX:AX = e0 x step
    add [cs_hzx], ax
    adc [cs_hzx + 2], dx
    ; --- the sides swapping is a whole row each (88.3.1.1.3) ----------------
    mov dx, [cs_hzl]
    cmp dx, [cs_hzsides]
    mov [cs_hzsides], dx
    mov al, 0
    je .sd
    mov al, 1
.sd:
    cmp byte [cs_hzlast], 0         ; ...and so is every row of the first
    jne .sd1                        ; haze frame after a plain one, whose
    mov al, 1                       ; rows' end inks this pass never saw
.sd1:
    mov [cs_hzwide], al
    mov byte [cs_hzlast], 1
    ; --- above them: wholly beyond all four, the plain pass's own rows ------
    mov es, [cs_tseg]
    cmp byte [cs_back], CSB_MODEX   ; (cs_skyground selects the planes after
    jne .m                          ; the label this was reached from)
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
.m:
    xor bx, bx
    mov di, [cs_tbase]
    add di, [cs_wb0]
    mov cx, [cs_hze0]
    mov al, [cs_hzab]
    call cs_hzrows                  ; (BX, DI step to the first row in reach)
.row:
    cmp bx, [cs_hze1]
    jg .below
    push bx
    push di
    call cs_hazerow
    pop di
    pop bx
    mov ax, [cs_hzst]               ; the horizon one row down
    add [cs_hzx], ax
    mov ax, [cs_hzst + 2]
    adc [cs_hzx + 2], ax
    inc bx
    add di, 80
    jmp short .row
.below:
    cmp byte [cs_back], CSB_MODEX   ; a run selects its own planes: every
    jne .p                          ; plane back for the whole rows
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
.p:
    mov es, [cs_tseg]
    mov cx, [cs_wh]
    sub cx, bx
    mov al, [cs_hzbl]
    jmp cs_hzrows

; cs_hzslope - [cs_hzst] = AX x 65536 / CX, signed, 16.16; CX = 0 leaves 0.
;              Clobbers AX, BX, CX, DX
cs_hzslope:
    xor bx, bx                      ; BL = the sign
    mov word [cs_hzst], 0
    mov word [cs_hzst + 2], 0
    jcxz .out
    or ax, ax
    jns .a
    neg ax
    not bl
.a:
    or cx, cx
    jns .c
    neg cx
    not bl
.c:
    xor dx, dx
    div cx                          ; the whole part...
    mov [cs_hzst + 2], ax
    xor ax, ax
    div cx                          ; ...and the fraction, off the remainder
    mov [cs_hzst], ax
    or bl, bl
    jz .out
    not word [cs_hzst]
    not word [cs_hzst + 2]
    add word [cs_hzst], 1
    adc word [cs_hzst + 2], 0
.out:
    ret

; cs_hzmul - DX:AX = AX x [cs_hzst], modulo 2^32 - which is exact wherever the
;            product is a crossing a row can use, and that is the only place
;            it is read. Clobbers BX, CX
cs_hzmul:
    xor cx, cx                      ; CL = the sign
    or ax, ax
    jns .p
    neg ax
    not cl
.p:
    mov bx, ax
    mul word [cs_hzst]              ; the fraction's product, whole
    push ax
    push dx
    mov ax, bx
    mul word [cs_hzst + 2]          ; ...and the whole part's, low word only
    pop dx
    add dx, ax
    pop ax
    or cl, cl
    jz .out
    not ax
    not dx
    add ax, 1
    adc dx, 0
.out:
    ret

; -----------------------------------------------------------------------------
; cs_hazerow - row BX, first view byte ES:DI, the horizon's x in [cs_hzx].
; Clobbers everything but ES
;
; THE LINES ARE UNROLLED (CS_HZLINE): on the 8088 a row is priced by the
; bytes of code it runs through, and a loop over a table pays a pointer and a
; counter on every line for nothing. A line is two compares and an add, and
; what it leaves is its share of the ink at the view's LEFT edge, and its
; crossing in cs_hzcx if that falls inside the view - in order, because the
; lines are taken left to right and are parallel.
; -----------------------------------------------------------------------------
%macro CS_HZLINE 1                  ; %1 = the line's index
    cmp bx, [cs_hzln + %1 * CS_HZL + 2]
    jl %%ab                         ; wholly above it
    cmp bx, [cs_hzln + %1 * CS_HZL + 4]
    jg %%bl                         ; ...wholly below
    mov ax, bp
    add ax, [cs_hzln + %1 * CS_HZL]
    cmp ax, [cs_wx0]
    jle %%lf                        ; crossing at or left of the view
    cmp ax, [cs_wx1]
    jg %%rt                         ; ...past it
    mov [si], ax
    inc si
    inc si
%%rt:
    add cl, [cs_hzgl]
    jmp short %%nx
%%lf:
    add cl, [cs_hzgr]
    jmp short %%nx
%%ab:
    add cl, [cs_hzag]
    jmp short %%nx
%%bl:
    add cl, [cs_hzbg]
%%nx:
%endmacro

cs_hazerow:
    mov [cs_hzrr], bx
    mov bp, [cs_hzx + 2]            ; BP = the horizon's x on this row
    mov si, cs_hzcx
    xor cl, cl                      ; CL = the ink at the view's left edge
    CS_HZLINE 0
    CS_HZLINE 1
    CS_HZLINE 2
    CS_HZLINE 3
%if CS_HZN != 4
  %error "cs_hazerow unrolls CS_HZN lines"
%endif
    mov [cs_hzg], cl
    mov [cs_hzce], si
    cmp si, cs_hzcx
    jne .split
    ; --- WHOLE: no line crosses the view, so the row is one ink - the plain
    ;     pass's incremental row ---------------------------------------------
    mov bl, cl
    xor bh, bh
    mov al, [cs_hzik + bx]
    mov bx, [cs_hzrr]
    mov cx, 1
    jmp cs_hzrows
.split:
    cmp byte [cs_back], CSB_MODEX
    jne cs_hzfill
    ; --- MODE X: its runs, each selecting its own planes -------------------
    mov ax, [cs_wx0]
    mov [cs_hzxs], ax
    mov word [cs_hzcp], cs_hzcx
.run:
    mov si, [cs_hzcp]
    cmp si, [cs_hzce]
    jae .last
    mov cx, [si]                    ; this crossing: the run ends before it
    dec cx
    mov ax, [cs_hzxs]
    cmp cx, ax
    jl .skip
    call cs_hzrun
.skip:
    mov si, [cs_hzcp]
    mov ax, [si]
    mov [cs_hzxs], ax
    add word [cs_hzcp], 2
    mov al, [cs_hzdw]
    add [cs_hzg], al
    jmp short .run
.last:
    mov ax, [cs_hzxs]
    mov cx, [cs_wx1]
    call cs_hzrun
    mov dx, 0x3C4                   ; every plane back for the next whole row
    mov ax, 0x0F02
    out dx, ax
    ret

; cs_hzrun - AX..CX of row [cs_hzrr] in ink number [cs_hzg]. Clobbers all
cs_hzrun:
    mov bl, [cs_hzg]
    xor bh, bh
    push ax
    mov al, [cs_hzik + bx]
    call cs_setink
    pop ax
    mov bx, [cs_hzrr]
    jmp cs_hrun

; -----------------------------------------------------------------------------
; cs_hzfill - a SPLIT row laid in one pass (Hercules, CGA, 160x100): row
; [cs_hzrr] at ES:DI, its ink [cs_hzg] at the view's left edge, the crossings
; cs_hzcx..[cs_hzce], all inside the view and in order. The bytes between two
; crossings go in one `rep`, a crossing's byte is composed of the two inks
; through the adapter's mask (the fused band's own method, 88.3.1.3), and two
; crossings in one byte compose it twice - read back from the shadow, which is
; the one place a byte is touched twice and the glass never sees. Clobbers
; everything but ES
; -----------------------------------------------------------------------------
cs_hzfill:
    mov bx, [cs_hzrr]               ; --- this row phase's five patterns -----
    and bx, 3
    mov cl, 3
    shl bx, cl
    add bx, cs_hzpp
    mov al, [cs_hzg]
    xor ah, ah
    add bx, ax
    mov [cs_hzgp], bx               ; where the ink being laid is...
    mov dl, [bx]                    ; DL = ...and its pattern
    mov [cs_hzg0], al               ; ...and the row's left end's, for its key
    mov bp, di                      ; BP + a byte column = that byte's address
    sub bp, [cs_wb0]
    mov si, cs_hzcx
    cld
.x:
    lodsw                           ; AX = the crossing
    mov bx, ax
    and bx, [cs_hzmm]
    add bx, [cs_hzmt]
    mov dh, [bx]                    ; DH = its mask: pixels i.., the new ink's
    mov cl, [cs_kshift]
    shr ax, cl
    add ax, bp                      ; AX = its byte's address
    mov cx, ax
    sub cx, di                      ; the whole bytes before it
    jb .same
    mov al, dl
    rep stosb
    mov ah, dl                      ; it starts as the ink being laid
    jmp short .cmp
.same:
    dec di                          ; the last crossing's byte, as written
    mov ah, [es:di]
.cmp:
    mov bx, [cs_hzgp]               ; the next ink
    add bx, [cs_hzdw]
    mov [cs_hzgp], bx
    mov dl, [bx]
    mov al, dh                      ; acc & ~mask | new & mask
    and al, dl
    not dh
    and ah, dh
    or al, ah
    stosb
    cmp si, [cs_hzce]
    jb .x
    mov cx, bp                      ; --- the rest of the row ---------------
    add cx, [cs_wb0]
    add cx, [cs_wbn]
    sub cx, di
    mov al, dl
    mov ah, dl
    shr cx, 1
    rep stosw
    jnc .span
    stosb
.span:
    ; --- THE ROW'S SPAN: the crossings' bytes and one either side (88.3.1.1),
    ;     clamped to the view - or the whole view if the row was not split
    ;     last frame or the sides swapped (88.3.1.1.3) -------------------------
    mov cl, [cs_kshift]
    mov ax, [cs_hzcx]
    shr ax, cl
    dec ax
    cmp ax, [cs_wb0]
    jge .lo
    mov ax, [cs_wb0]
.lo:
    mov si, [cs_hzce]
    mov dx, [si - 2]
    shr dx, cl
    inc dx
    mov cx, [cs_wb0]
    add cx, [cs_wbn]
    dec cx
    cmp dx, cx
    jle .hi
    mov dx, cx
.hi:
    mov ah, dl
    mov bx, [cs_hzrr]
    ; --- ...AND THE ROW'S TWO END INKS, which a tight span takes for granted:
    ;     a band crossing the view's edge, or the bank jumping, changes what
    ;     lies beyond every crossing, and no span this frame or last covers
    ;     it. So a row whose ends are not last frame's carries all of itself
    mov dx, [cs_hzgp]
    sub dx, cs_hzpp
    and dl, 7                       ; DL = the right end's ink number
    mov cl, 3
    shl dl, cl
    or dl, [cs_hzg0]                ; ...and the left's
    cmp dl, [cs_hzrend + bx]
    mov [cs_hzrend + bx], dl
    jne .full
    cmp byte [cs_rowkind + bx], 3
    jne .full
    cmp byte [cs_hzwide], 0
    je .put
.full:
    mov ax, [cs_fullspan]
.put:
    mov byte [cs_rowkind + bx], 3
    shl bx, 1
    add bx, [cs_spcur]
    mov [bx], ax
    mov bx, [cs_spcur]              ; ...and the set's row range takes it
    mov ax, [cs_hzrr]
    cmp ax, [bx - 4]
    jge .r0
    mov [bx - 4], ax
.r0:
    cmp ax, [bx - 2]
    jle .r1
    mov [bx - 2], ax
.r1:
    ret
