; Narrow native bridges, BORATYPE-SPEC 1/3/6 and SPEC.md 73.3.
; cdecl, BP/DS/ES preserved. Desktop files use the shared APPDATA walker;
; no package-private FAT or instance directory mechanism.
section .text
_bt_data_enter:
    call od_enter
    sbb ax,ax
    ret
_bt_data_back:
    call od_back
    ret

; Another live BORATYPE window makes this instance read-only for progress.
; Main calls before publishing its own window. Use the supported ownership
; slot rather than retaining pointers to other instances. Compare the pinned
; package header (§20.2) name, not a title that may change or be localized.
_bt_other_instance:
    push bp
    push es
    xor ax,ax
    mov cx,MAX_WIN
.slot:
    call OSAPI_WM_OWNSEG
    jc .next
    mov bx,ds
    cmp dx,bx
    je .next
    cmp dx,KERNEL_SEG
    je .next
    mov es,dx
    cmp word [es:0],384Fh
    jne .next
    cmp word [es:16],'BO'
    jne .next
    cmp word [es:18],'RA'
    jne .next
    cmp word [es:20],'TY'
    jne .next
    cmp word [es:22],'PE'
    jne .next
    cmp byte [es:24],0
    jne .next
    mov ax,1
    jmp short .out
.next:
    inc ax
    loop .slot
    xor ax,ax
.out:
    pop es
    pop bp
    ret

; BIOS keyboard shift flags are the same flags used by OS-delivered ASCII
; (§9). AH=02h asks state, consumes no key and works through mega's BIOS gate.
; No keyboard ports and no private scancode decoder.
_bt_modifier_state:
    push bp
    push ds
    push es
    mov ah,02h
    int 16h
    mov dl,al
    xor ax,ax
    test dl,2
    jz .right
    or al,1
.right:
    test dl,1
    jz .caps
    or al,2
.caps:
    test dl,40h
    jz .done
    or al,4
.done:
    pop es
    pop ds
    pop bp
    ret

; Idle-drawing deferral only: leave every pending key for the normal OS
; event pump. The existing BIOS bridge also covers kern_mega. Scoring never
; calls this peek or consumes BIOS input outside the exclusive FSX bracket.
_bt_key_pending:
    push bp
    push ds
    push es
    mov ah,01h
    int 16h
    mov ax,0                     ; MOV preserves BIOS ZF
    jz .empty
    inc ax
.empty:
    pop es
    pop ds
    pop bp
    ret

; Copy the kernel's actual 8x8 ASCII glyphs BEFORE a foreign mode. The font
; is returned as DX:SI, not DS or KERNEL_SEG. All rendering then uses ours.
_bt_copy_font:
    push bp
    push es
    call OSAPI_FONT_GLYPHS
    cmp al,32
    ja .out
    cmp ah,126
    jb .out
    xor ah,ah
    mov bx,32
    sub bx,ax
    shl bx,1
    shl bx,1
    shl bx,1
    add si,bx
    mov es,dx
    mov di,_bt_cga_font
    mov cx,760
.copy:
    mov al,[es:si]
    mov [di],al
    inc si
    inc di
    loop .copy
.out:
    pop es
    pop bp
    ret

; The 8KB drawing band becomes CGA's 2KB cell shadow while the native
; renderer is parked. No additional claim or full-screen framebuffer.
_bt_cga_grid_clear:
    mov bx,sp
    push es
    mov ax,ds
    mov es,ax
    mov di,_bt_io
    mov ax,0020h
    cmp word [ss:bx+2],0
    jne .keep
    mov cx,1000
    cld
    rep stosw
    jmp short .out
.keep:
    mov cx,160                    ; rows0..3; retain the finger-hint row4
    cld
    rep stosw
    add di,80
    mov cx,440                    ; rows5..15; retain physical keys16..20
    rep stosw
    add di,480                    ; keyboard plus blank row21
    mov cx,120                    ; caption/footer rows22..24
    rep stosw
.out:
    pop es
    ret

; void bt_cga_text(int x,int y,const char *s,int ink,int paper).
; C ABI scratch registers, preserves BP/DS/ES. Only a bounded package-owned
; character/attribute row is composed; this routine never draws or calls OS.
_bt_cga_text:
    push bp
    mov bp,sp
    mov ax,[bp+4]
    test ax,ax
    js .out
    cmp ax,40
    jae .out
    shl ax,1
    mov di,ax
    mov ax,[bp+6]
    test ax,ax
    js .out
    cmp ax,25
    jae .out
    mov dx,80
    mul dx
    add di,ax
    add di,_bt_io
    mov ax,[bp+12]
    mov cl,4
    shl al,cl
    or al,[bp+10]
    and al,33h
    mov ah,al
    mov cx,40
    sub cx,[bp+4]
    mov si,[bp+8]
    cld
.copy:
    lodsb
    test al,al
    jz .out
    mov [di],ax
    add di,2
    loop .copy
.out:
    pop bp
    ret

_bt_cga_clear_shadow:
    push es
    mov es,[_bt_band]
    xor di,di
    mov ax,0FFFFh
    mov cx,1000
    cld
    rep stosw
    pop es
    ret

; Commit 40x25 opaque character cells. Each changed cell expands its glyph
; into pairs of CGA scanline banks. All bounds and geometry were validated
; against FSI by bt_cga_entry. No desktop OSAPI drawing call in this path.
_bt_cga_flush:
    push bp
    push es
    mov es,[_bt_band]
    xor di,di
    mov si,_bt_io
    mov bp,25
    cld
.row:
    push di
    push si
    mov cx,40
    repe cmpsw                    ; most rows are byte-identical, skip them
    pop si
    pop di
    je .same
    mov cx,40
.cell:
    mov bx,di
    mov ax,[_bt_io+bx]
    cmp ax,[es:di]
    je .next
    mov [es:di],ax
    push cx
    push di
    push es
    push si
    call bt_cga_cell
    pop si
    pop es
    pop di
    pop cx
.next:
    add di,2
    loop .cell
    add si,80
    jmp short .rowdone
.same:
    add di,80
    add si,80
.rowdone:
    dec bp
    jnz .row
    pop es
    pop bp
    ret

; AX character/attribute, DI cell offset (2*index); clobbers all but BP,DS.
bt_cga_cell:
    mov bl,ah
    mov bh,0
    and bx,3
    mov dl,[bt_cga_col+bx]
    mov dh,dl
    mov [bt_cga_ink],dx
    mov bl,ah
    mov cl,4
    shr bl,cl
    xor bh,bh
    and bx,3
    mov dl,[bt_cga_col+bx]
    mov dh,dl
    mov [bt_cga_paper],dx
    xor ah,ah
    cmp al,32
    jb .space
    cmp al,126
    jbe .glyph
.space:
    mov al,32
.glyph:
    sub ax,32
    shl ax,1
    shl ax,1
    shl ax,1
    mov si,_bt_cga_font
    add si,ax
    mov ax,di
    shr ax,1
    xor dx,dx
    mov bx,40
    div bx
    mul word [_bt_cga_stride]
    shl ax,1
    shl ax,1
    ; MUL overwrote DX: recover the column from DI modulo eighty.
    push ax
    mov ax,di
    xor dx,dx
    mov bx,80
    div bx
    pop ax
    add ax,dx
    mov di,ax
    mov es,[_bt_cga_seg]
    mov cx,4
.pair:
    call bt_cga_grow
    add di,[_bt_cga_bstep]
    call bt_cga_grow
    sub di,[_bt_cga_bstep]
    add di,[_bt_cga_stride]
    loop .pair
    ret
bt_cga_grow:
    xor bx,bx
    mov bl,[si]
    inc si
    mov dl,bl
    and bx,15
    mov ah,[bt_cga_expand+bx]
    mov bl,dl
    shr bl,1
    shr bl,1
    shr bl,1
    shr bl,1
    mov al,[bt_cga_expand+bx]
    mov dx,[bt_cga_ink]
    xor dx,[bt_cga_paper]
    and ax,dx
    xor ax,[bt_cga_paper]
    mov [es:di],ax
    ret
bt_cga_expand: db 0,3,12,15,48,51,60,63,192,195,204,207,240,243,252,255
bt_cga_col: db 0,55h,0AAh,0FFh
bt_cga_ink: dw 0
bt_cga_paper: dw 0

; void bt_cga_portrait(int id,int x,int y). The converter emits standard
; 32x40 nibble sprites with colours0/11/5/15. Convert four pixels per byte,
; one bounded source row at a time; nothing per pixel calls the OS.
_bt_cga_portrait:
    push bp
    mov bp,sp
    push es
    mov bx,[bp+4]
    call bx_art_at
    jc .out
    cmp word [es:si],2820h
    jne .out
    add si,2
    mov [bt_cga_psrc],si
    mov [bt_cga_pseg],es
    mov word [bt_cga_prow],0
.row:
    mov es,[bt_cga_pseg]
    mov si,[bt_cga_psrc]
    mov di,bt_cga_line
    mov cx,8
.convert:
    mov dx,[es:si]
    add si,2
    xor bx,bx
    mov bl,dl
    shr bl,1
    shr bl,1
    shr bl,1
    shr bl,1
    mov al,[bt_cga_map+bx]
    shl al,1
    shl al,1
    mov bl,dl
    and bx,15
    or al,[bt_cga_map+bx]
    shl al,1
    shl al,1
    mov bl,dh
    shr bl,1
    shr bl,1
    shr bl,1
    shr bl,1
    or al,[bt_cga_map+bx]
    shl al,1
    shl al,1
    mov bl,dh
    and bx,15
    or al,[bt_cga_map+bx]
    mov [di],al
    inc di
    loop .convert
    mov [bt_cga_psrc],si
    mov ax,[bp+8]
    add ax,[bt_cga_prow]
    mov dx,ax
    and dx,1
    shr ax,1
    mul word [_bt_cga_stride]
    ; The row's parity is recovered after MUL.
    mov bx,[bp+8]
    add bx,[bt_cga_prow]
    test bx,1
    jz .even
    add ax,[_bt_cga_bstep]
.even:
    mov bx,[bp+6]
    shr bx,1
    shr bx,1
    add ax,bx
    mov di,ax
    mov es,[_bt_cga_seg]
    mov si,bt_cga_line
    mov cx,8
    cld
    rep movsb
    inc word [bt_cga_prow]
    cmp word [bt_cga_prow],40
    jb .row
.out:
    pop es
    pop bp
    ret
bt_cga_map: db 0,0,1,1,2,2,2,3,0,1,1,1,2,0,3,3
bt_cga_psrc: dw 0
bt_cga_pseg: dw 0
bt_cga_prow: dw 0
bt_cga_line: times 8 db 0

; CGA colour select belongs to this exclusive mode borrow. WireFleet uses
; the same palette1 bright selection; FSX restores the desktop on return.
_bt_cga_palette:
    mov dx,03D9h
    mov al,30h
    out dx,al
    ret

; void bt_cga_pointer(int x,int y): reversible5px crosshair. Caller clamps
; x2..317,y2..197. Only the foreign VRAM's own2bpp cells are touched.
_bt_cga_pointer:
    push bp
    mov bp,sp
    push es
    mov es,[_bt_cga_seg]
    mov cx,[bp+4]
    mov bx,[bp+6]
    sub cx,2
    mov di,5
.horizontal:
    call bt_cga_xpixel
    inc cx
    dec di
    jnz .horizontal
    mov cx,[bp+4]
    mov bx,[bp+6]
    sub bx,2
    mov di,5
.vertical:
    cmp bx,[bp+6]
    je .skipcentre
    call bt_cga_xpixel
.skipcentre:
    inc bx
    dec di
    jnz .vertical
    pop es
    pop bp
    ret
bt_cga_xpixel:                 ; CX=x,BX=y, ES=foreignVRAM; preservesinputs
    push bx
    push cx
    mov ax,bx
    shr ax,1
    mul word [_bt_cga_stride]
    test bx,1
    jz .even
    add ax,[_bt_cga_bstep]
.even:
    mov bx,cx
    shr bx,1
    shr bx,1
    add bx,ax
    and cl,3
    xor cl,3
    shl cl,1
    mov al,3
    shl al,cl
    xor [es:bx],al
    pop cx
    pop bx
    ret

%include "os88data.inc"
section .text

; Decode one precomputed portrait strip into the existing drawing band.
; int bt_anim_unpack(seg,off,packed,raw), cdecl; raw <=1152, checked in C.
; OSAPI_DECOMP supplies bounded LZ4; shared bx_out supplies clipped output.
_bt_anim_unpack:
    push bp
    mov bp,sp
    push ds
    push es
    mov es,[_bx_bseg]
    mov si,[bp+6]
    mov cx,[bp+8]
    mov dx,[bp+10]
    xor bx,bx
    xor di,di
    xor ax,ax
    mov ds,[bp+4]
    call OSAPI_DECOMP
    pop es
    pop ds
    sbb ax,ax
    pop bp
    ret
section .text
