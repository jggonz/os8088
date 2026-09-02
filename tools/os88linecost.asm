; =============================================================================
; os8088 - tools/os88linecost.asm
;
; The stubs tools/os88linecost.py parks the CPU on. NOT part of any build and
; nothing here ships: it is assembled on the host, per measurement, straight
; into an idle kernel .bss buffer of a RUNNING os8088, and every symbol it
; names is passed in with -D by the driver.
;
; PIECE = 0 is a whole alternative 1bpp line walk, against which SPEC.md 5.6.4's
; gfx_line_mono is timed. Everything gfx_line_mono keeps in .bss is in a
; register here, the clip box has been spent BEFORE the loop - which is what an
; interval clip leaves behind - and the row step is inline rather than a
; `call gfx_nextrow`.
;
;   AX = d            DX = SUB2       SI = ADD2      CX = pixel count
;   BL = the rotating bit mask        DI = framebuffer offset
;   BH = the pending byte (ACCUM only)             BP = vid_rowadd
;   ES = the framebuffer
;
;   steep   (y-major): d = 2dx - dy, SUB2 = 2dy, ADD2 = 2dx, count = dy+1
;   shallow (x-major): d = 2dy - dx, SUB2 = 2dx, ADD2 = 2dy, count = dx+1
;
; The split by octant is what removes the state: the major axis steps every
; pixel so it needs no test, and the minor axis's whole decision is the sign of
; one register. tools/os88linecost.py --model proves the pixel set is the
; IDENTICAL one gfx_line_mono lays, over every endpoint pair in a 121x80 box.
;
; Solid WHITE ink only, so the plot is one `or`. Black is the same loop with
; `and [es:di], bl` and BL pre-inverted; the dither is the general path.
;
; SIGNWRAP=1 tests the bank overflow with `js` instead of
; `test di,[vid_wrapbit]`. That is HERCULES ONLY - its wrapbit is 0x8000, which
; IS the sign bit; CGA's is 0x4000 and the shortcut silently lays a different
; line there, which is why the driver diffs the framebuffer rather than
; trusting it.
;
; PIECE = 1..5 price the individual pieces of gfx_line_mono's loop instead, N
; iterations each, so an attribution is measured rather than modelled.
; =============================================================================
cpu 8086
bits 16
org STUB

%ifndef PIECE
  %define PIECE 0
%endif

%if PIECE == 0
; =============================================================================
; The candidate walk
; =============================================================================
start:
    mov es, [VID_RSEG]
    mov ax, Y1
    call GFX_ROWBASE                ; the one thing still shared with gfx_line
    mov di, ax
    mov ax, X1
    mov cl, 3
    shr ax, cl
    add di, ax
    mov cx, X1
    and cl, 7
    mov bl, 0x80
    shr bl, cl
    mov bh, 0
    mov ax, D0
    mov dx, SUB2
    mov si, ADD2
    mov bp, [ROWADD]
    mov cx, COUNT

%macro PLOT 0
  %if NODRAW == 0
    or  [es:di], bl
  %endif
%endmacro

%macro ROWSTEP 0
    add di, bp
  %if SIGNWRAP
    js  %%wrap                      ; Hercules only - see the header
  %else
    test di, [WRAPBIT]
    jnz %%wrap
  %endif
    jmp short %%done
%%wrap:
    add di, [WRAPFIX]
%%done:
%endmacro

%if STEEP
; --- y-major: y steps every pixel, x steps when d > 0 ------------------------
loop_start:
.px:
    PLOT
    or  ax, ax
    jg  .xstep
.noxs:
    add ax, si                      ; d += 2*dx
    add di, bp                      ; ...and one row down
%if SIGNWRAP
    js  .wrap
%else
    test di, [WRAPBIT]
    jnz .wrap
%endif
.back:
    loop .px
    jmp done
.xstep:
    sub ax, dx                      ; d -= 2*dy
    shr bl, 1
    jnz .noxs
    mov bl, 0x80
    inc di
    jmp short .noxs
.wrap:
    add di, [WRAPFIX]
    jmp short .back

%elif ACCUM
; --- x-major, ACCUMULATING: one framebuffer RMW per BYTE, not per pixel ------
; BH carries the bits this byte is owed and is spent when the walk leaves the
; byte - which a shallow line does every eight pixels - or when it leaves the
; ROW, which is the only other way DI moves. Measured: it is worth ~10% and
; not the 8x the store count suggests, because a line shallow enough for a
; byte to hold eight of its pixels is one gfx_line already sends to gfx_hline.
loop_start:
.px:
    or  bh, bl                      ; the pixel, into the pending byte
    or  ax, ax
    jg  .ystep
.noys:
    add ax, si                      ; d += 2*dy
    shr bl, 1
    jnz .back
    or  [es:di], bh                 ; ...off the end of this byte: spend it
    mov bh, 0
    mov bl, 0x80
    inc di
.back:
    loop .px
    or  [es:di], bh                 ; the last, partly-filled byte
    jmp done
.ystep:
    or  [es:di], bh                 ; DI is about to move: spend it first
    mov bh, 0
    sub ax, dx                      ; d -= 2*dx
    ROWSTEP
    jmp short .noys

%else
; --- x-major: x steps every pixel, y steps when d > 0 ------------------------
loop_start:
.px:
    PLOT
    or  ax, ax
    jg  .ystep
.noys:
    add ax, si                      ; d += 2*dy
    shr bl, 1
    jnz .back
    mov bl, 0x80
    inc di
.back:
    loop .px
    jmp done
.ystep:
    sub ax, dx                      ; d -= 2*dx
    ROWSTEP
    jmp short .noys
%endif

done:
    jmp $

%else
; =============================================================================
; The pieces, N iterations each. The bare `loop` is PIECE = 6 and the driver
; takes it off every row, so a number here is the piece and not the harness.
; =============================================================================
start:
    mov es, [VID_RSEG]
    mov di, 0x100
    mov si, 100                     ; a point well inside the box below
    mov dx, 40
    mov cx, N
loop_start:
.it:
%if PIECE == 1
    call GFX_NEXTROW                ; what the walk does, once a row
    sub di, [ROWADD]                ; undone, so DI cannot run away
%elif PIECE == 2
    add di, [ROWADD]                ; ...the same three instructions INLINE
    test di, [WRAPBIT]
    jz .nowrap
    add di, [WRAPFIX]
.nowrap:
    sub di, [ROWADD]
%elif PIECE == 3
    cmp dx, [LN_CY2]                ; SPEC.md 5.6.3's per-pixel clip test,
    jg .out                         ; plus 5.6.6's gfx_ln_wide
    cmp dx, [LN_CY1]
    jl .next
    cmp byte [LN_WIDE], 0
    jne .out
    cmp si, [LN_CX1]
    jl .next
    cmp si, [LN_CX2]
    jg .next
.next:
%elif PIECE == 4
    push cx                         ; the e2 block, through .bss
    mov cx, [LN_ERR]
    mov ax, cx
    add ax, ax
    mov [LN_E2], ax
    mov ax, [LN_DY]
    neg ax
    cmp [LN_E2], ax
    jle .noxs
    sub cx, [LN_DY]
    cmp word [LN_SX], 0
    jl .noxs
.noxs:
    mov ax, [LN_E2]
    cmp ax, [LN_DX]
    jge .noys
    add cx, [LN_DX]
.noys:
    pop cx
%elif PIECE == 7
    jmp word [.tab]                 ; what the four-steep-loops collapse would
.tab: dw .xt                        ; add per x-step: the body can only fall
.xt:                                ; into ONE tail inline, so picking one of
                                    ; four costs an indirect jump. The `dw` is
                                    ; never executed - the jmp above does not
                                    ; fall through.
%elif PIECE == 5
    push ax                         ; ...the same decision, in registers
    mov ax, 1
    or  ax, ax
    jg  .xs
.noxs:
    add ax, si
    jmp short .end
.xs:
    sub ax, dx
    jmp short .noxs
.end:
    pop ax
%endif
    loop .it
.out:
done:
    jmp $
%endif
