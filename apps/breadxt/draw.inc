; Breadboard XT original drawing cores. SPEC.md 130 / 73.11.
; Near cdecl; DS is package data, SS is stack, ES is preserved.
section .text

; void bb_bytes(void *dst,unsigned n,unsigned byte)
_bb_bytes:
    push bp
    mov bp,sp
    push es
    push ds
    pop es
    mov di,[bp+4]
    mov cx,[bp+6]
    mov ax,[bp+8]
    cld
    rep stosb
    pop es
    pop bp
    ret

; void bb_board_row(void *bits,int stride,int cols,int pitch,const unsigned *marks)
; One hole row, pitch rows high. White ground, 3x3 black holes; selected
; physical strip uses solid sides; other connected net strips use dots.
; marks[0..1] are physical, marks[2..3] electrical. Centres start at x=8.
_bb_board_row:
    push bp
    mov bp,sp
    mov di,[bp+4]
    mov ax,[bp+6]
    mul word [bp+10]
    mov cx,ax
    push es
    push ds
    pop es
    mov al,255
    cld
    rep stosb
    pop es
    mov word [bb_dr_col],0
    mov word [bb_dr_x],8
.col:
    mov ax,[bb_dr_col]
    cmp ax,[bp+8]
    jae .done
    mov si,[bp+12]
    mov bx,ax
    mov cl,4
    shr bx,cl
    shl bx,1
    mov dx,[si+bx]
    mov cx,ax
    and cx,15
    shr dx,cl
    and dx,1
    jz .net
    mov dx,2
    jmp .marked
.net:
    mov dx,[si+bx+4]
    shr dx,cl
    and dx,1
.marked:
    mov [bb_dr_mark],dx
    mov ax,[bp+4]
    mov [bb_dr_rowptr],ax
    mov word [bb_dr_y],0
.row:
    mov ax,[bb_dr_y]
    cmp ax,[bp+10]
    jae .next
    mov bx,[bb_dr_x]
    cmp ax,4
    jb .mark
    cmp ax,6
    ja .mark
    dec bx
    call bb_dr_black
    inc bx
    call bb_dr_black
    inc bx
    call bb_dr_black
.mark:
    cmp word [bb_dr_mark],0
    je .rnext
    cmp word [bb_dr_mark],2
    je .sides
    test byte [bb_dr_y],1
    jnz .rnext
.sides:
    mov bx,[bb_dr_x]
    sub bx,4
    call bb_dr_black
    add bx,8
    call bb_dr_black
.rnext:
    mov ax,[bp+6]
    add [bb_dr_rowptr],ax
    inc word [bb_dr_y]
    jmp .row
.next:
    mov ax,[bp+10]
    add [bb_dr_x],ax
    inc word [bb_dr_col]
    jmp .col
.done:
    pop bp
    ret
; BX local x, current y. Preserve BX so three adjacent pixels are cheap.
bb_dr_black:
    push ax
    push bx
    push cx
    push di
    mov di,[bb_dr_rowptr]
    mov cx,bx
    and cx,7
    shr bx,1
    shr bx,1
    shr bx,1
    add di,bx
    mov al,80h
    shr al,cl
    not al
    and [di],al
    pop di
    pop cx
    pop bx
    pop ax
    ret

; void bb_scope_line(bits,stride,x1,y1,x2,y2,dash)
; Coordinates preclipped by UI. Set trace bits in a black-ground scope band.
_bb_scope_line:
    push bp
    mov bp,sp
    mov ax,[bp+8]
    mov [bb_dr_lx],ax
    mov ax,[bp+10]
    mov [bb_dr_ly],ax
    mov ax,[bp+12]
    sub ax,[bb_dr_lx]
    mov word [bb_dr_sx],1
    jns .dx
    neg ax
    mov word [bb_dr_sx],-1
.dx:
    mov [bb_dr_dx],ax
    mov ax,[bp+14]
    sub ax,[bb_dr_ly]
    mov word [bb_dr_sy],1
    jns .dy
    neg ax
    mov word [bb_dr_sy],-1
.dy:
    neg ax
    mov [bb_dr_dy],ax
    add ax,[bb_dr_dx]
    mov [bb_dr_err],ax
.loop:
    cmp word [bp+16],0
    je .pixel
    ; Spatial phase survives short sample/hold segments and redraw boundaries.
    mov ax,[bb_dr_lx]
    add ax,[bb_dr_ly]
    test al,2
    jnz .skip
.pixel:
    mov ax,[bb_dr_ly]
    mul word [bp+6]
    mov di,[bp+4]
    add di,ax
    mov bx,[bb_dr_lx]
    mov cx,bx
    and cx,7
    shr bx,1
    shr bx,1
    shr bx,1
    add di,bx
    mov al,80h
    shr al,cl
    or [di],al
.skip:
    mov ax,[bb_dr_lx]
    cmp ax,[bp+12]
    jne .advance
    mov ax,[bb_dr_ly]
    cmp ax,[bp+14]
    je .done
.advance:
    mov ax,[bb_dr_err]
    shl ax,1
    mov dx,ax
    cmp ax,[bb_dr_dy]
    jl .onlyy
    mov ax,[bb_dr_dy]
    add [bb_dr_err],ax
    mov ax,[bb_dr_sx]
    add [bb_dr_lx],ax
.onlyy:
    cmp dx,[bb_dr_dx]
    jg .step
    mov ax,[bb_dr_dx]
    add [bb_dr_err],ax
    mov ax,[bb_dr_sy]
    add [bb_dr_ly],ax
.step:
    jmp .loop
.done:
    pop bp
    ret

; int bb_ask(const char *message,void *window,int buttons)
_bb_ask:
    push bp
    mov bp,sp
    push es
    mov si,[bp+4]
    mov bx,[bp+6]
    mov ax,[bp+8]
    mov di,bb_dr_askdone
    call os88ui_ask
    mov ax,0
    jnc .ok
    dec ax
.ok:
    pop es
    pop bp
    ret
bb_dr_askdone:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    xor ah,ah
    cmp al,OS88UI_ACANCEL
    jne .have
    mov ax,-1
.have:
    push ax
    call _bb_alertdone
    add sp,2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
section .bss
bb_dr_col: resw 1
bb_dr_x: resw 1
bb_dr_y: resw 1
bb_dr_mark: resw 1
bb_dr_rowptr: resw 1
bb_dr_lx: resw 1
bb_dr_ly: resw 1
bb_dr_dx: resw 1
bb_dr_dy: resw 1
bb_dr_sx: resw 1
bb_dr_sy: resw 1
bb_dr_err: resw 1
section .text
; unsigned bb_muldiv(unsigned a,unsigned b,unsigned denominator)
; UI unit scaling only: checked 16x16->32/16, saturation on overflow.
section .text
_bb_muldiv:
    push bp
    mov bp,sp
    mov ax,[bp+4]
    mul word [bp+6]
    mov cx,[bp+8]
    or cx,cx
    jz .zero
    cmp dx,cx
    jae .sat
    div cx
    pop bp
    ret
.sat:
    mov ax,0FFFFh
    pop bp
    ret
.zero:
    xor ax,ax
    pop bp
    ret
; void bb_timefmt(char *out,unsigned time_lo,unsigned time_hi)
; Decimal seconds with four fractional digits from the 32-bit100us clock.
_bb_timefmt:
    push bp
    mov bp,sp
    mov ax,[bp+8]
    xor dx,dx
    mov cx,10000
    div cx
    mov [bb_dr_thi],ax
    mov ax,[bp+6]
    div cx
    mov [bb_dr_tlo],ax
    mov [bb_dr_frac],dx
    mov si,bb_dr_num+11
    mov byte [si],0
.convert:
    mov ax,[bb_dr_thi]
    xor dx,dx
    mov cx,10
    div cx
    mov [bb_dr_thi],ax
    mov ax,[bb_dr_tlo]
    div cx
    mov [bb_dr_tlo],ax
    add dl,'0'
    dec si
    mov [si],dl
    mov ax,[bb_dr_tlo]
    or ax,[bb_dr_thi]
    jnz .convert
    mov di,[bp+4]
.copy:
    lodsb
    or al,al
    jz .fraction
    mov [di],al
    inc di
    jmp .copy
.fraction:
    mov byte [di],'.'
    inc di
    mov bx,1000
.fd:
    mov ax,[bb_dr_frac]
    xor dx,dx
    div bx
    mov [bb_dr_frac],dx
    add al,'0'
    mov [di],al
    inc di
    mov ax,bx
    xor dx,dx
    mov cx,10
    div cx
    mov bx,ax
    or bx,bx
    jnz .fd
    mov byte [di],0
    pop bp
    ret
section .bss
bb_dr_thi: resw 1
bb_dr_tlo: resw 1
bb_dr_frac: resw 1
bb_dr_num: resb 12
section .text
