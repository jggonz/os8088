; Speedy Basic C-package shim and the far-claim screen/editor movers.

%define OS88UI_ABOUT
%define OS88UI_NOBTN

%ifdef SB_VM_ASM
%define CC_PKG_NAME 'BASICVM'
%define CC_HAS_PARTS
%define CC_OVL_PART 0
%else
%define CC_PKG_NAME 'SPEEDYBA'
%endif
%define CC_HAS_ONKEY
%define CC_HAS_ONCLICK
%define CC_HAS_ONRESIZE
%define CC_HAS_MENUS
%define CC_HAS_ABOUT
%define CC_HAS_FDLG
%define CC_HAS_ONWAKE
%define CC_HAS_ONTIMER
%define CC_HAS_OVL
%define CC_ICON "speedybasic/icon.inc"

%include "cc/crt0.asm"
%ifdef SB_VM_ASM
    CC_PARTS_BEGIN 2
      OS88_PART OP_SEG
      OS88_PART OP_ASSET
    CC_PARTS_END
%include "speedybasicvm.gen.asm"
%else
%include "speedybasic.gen.asm"
%endif
%include "os88ui.inc"

section .text

; void sb_fb_clear(unsigned seg, int color)
_sb_fb_clear:
    push bp
    mov bp, sp
    push es
    push di
    push cx
    mov es, [bp+4]
    mov ax, [bp+6]
    and al, 0x0f
    mov ah, al
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, ah
    mov ah, al
    xor di, di
    mov cx, 16000                 ; 320 x 200 packed 4bpp / two
    cld
    rep stosw
    pop cx
    pop di
    pop es
    pop bp
    ret

; void sb_fb_pixel(unsigned seg, int x, int y, int color)
_sb_fb_pixel:
    push bp
    mov bp, sp
    push es
    push bx
    push dx
    push di
    mov ax, [bp+8]                ; y * 160 + x / 2
    mov bx, 160
    mul bx
    mov di, [bp+6]
    shr di, 1
    add di, ax
    mov es, [bp+4]
    mov dl, [es:di]
    mov al, [bp+10]
    and al, 0x0f
    test byte [bp+6], 1
    jnz .low
    and dl, 0x0f
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or dl, al
    jmp short .put
.low:
    and dl, 0xf0
    or dl, al
.put:
    mov [es:di], dl
    pop di
    pop dx
    pop bx
    pop es
    pop bp
    cld
    ret

; void sb_fb_blit(unsigned seg, int x, int y)
_sb_fb_blit:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov ax, [bp+6]
    mov bx, [bp+8]
    xor si, si
    mov bp, 160
    mov cx, 320
    mov dx, 200
    call OSAPI_GFX_BLIT4
    pop es
    pop bp
    cld
    ret

; Decode SCREEN 9's four exact 640x350 EGA planes one scaled row at a time.
; Five rows use an 800-byte resident band.  SCREEN 9 therefore spends no heap
; beyond its four exact planes and still avoids one kernel call per scanline.
_sb_fb_blit9:
    push bp
    mov bp, sp
    sub sp, 6                       ; row, source byte row, caller DS
    mov [ss:bp-6], ds
    push ds
    push es
    push bx
    push si
    push di
    mov word [ss:bp-2], 0
.b9_row:
    mov ax, [ss:bp-2]
    mov bx, 7
    mul bx
    shr ax, 1
    shr ax, 1                       ; source y = output y * 7 / 4
    mov bx, 80
    mul bx
    mov [ss:bp-4], ax
    mov es, [ss:bp-6]
    mov ax, [ss:bp-2]
    xor dx, dx
    mov bx, 5
    div bx
    mov ax, dx
    mov bx, 160
    mul bx
    mov di, ax
    add di, _sbs_band
    xor bx, bx                      ; output x
.b9_pair:
%macro B9_PIXEL 0
    mov si, bx
    shr si, 1
    shr si, 1
    add si, [ss:bp-4]
    mov cx, bx
    and cx, 3
    shl cx, 1
    mov dl, 0x80
    shr dl, cl
    xor al, al
    mov ds, [ss:bp+6]
    test [ds:si], dl
    jz %%p1
    or al, 1
%%p1:
    mov ds, [ss:bp+8]
    test [ds:si], dl
    jz %%p2
    or al, 2
%%p2:
    mov ds, [ss:bp+10]
    test [ds:si], dl
    jz %%p3
    or al, 4
%%p3:
    mov ds, [ss:bp+12]
    test [ds:si], dl
    jz %%done
    or al, 8
%%done:
%endmacro
    B9_PIXEL
    xor dh, dh
    mov dl, al
    mov si, dx
    mov al, [cs:_sbs_ega_map+si]
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov ah, al
    inc bx
    B9_PIXEL
    xor dh, dh
    mov dl, al
    mov si, dx
    mov al, [cs:_sbs_ega_map+si]
    or al, ah
    mov [es:di], al
    inc di
    inc bx
    cmp bx, 320
    jb .b9_pair
    inc word [ss:bp-2]
    mov ax, [ss:bp-2]
    xor dx, dx
    mov bx, 5
    div bx
    test dx, dx
    jnz .b9_row
    mov ds, [ss:bp-6]
    mov es, [ss:bp-6]
    mov si, _sbs_band
    mov ax, [ss:bp+14]
    mov bx, [ss:bp+16]
    add bx, [ss:bp-2]
    sub bx, 5
    push bp
    mov bp, 160
    mov cx, 320
    mov dx, 5
    call OSAPI_GFX_BLIT4
    pop bp
    cmp word [ss:bp-2], 200
    jb .b9_row
    pop di
    pop si
    pop bx
    pop es
    pop ds
    mov sp, bp
    pop bp
    cld
    ret

; The packed surface uses 32,000 of its 32,768 bytes.  Its tail retains the
; 256-entry VGA-index to desktop-colour map without resident BSS.
_sb_map_get:
    push bp
    mov bp, sp
    push es
    push bx
    mov es, [bp+4]
    mov bx, [bp+6]
    and bx, 255
    xor ax, ax
    mov al, [es:bx+32000]
    pop bx
    pop es
    pop bp
    ret

_sb_map_set:
    push bp
    mov bp, sp
    push es
    push bx
    mov es, [bp+4]
    mov bx, [bp+6]
    and bx, 255
    mov al, [bp+8]
    mov [es:bx+32000], al
    pop bx
    pop es
    pop bp
    ret

; void sb_plane_clear(unsigned seg, int set, int kb)
_sb_plane_clear:
    push bp
    mov bp, sp
    push es
    push di
    mov es, [bp+4]
    xor di, di
    xor ax, ax
    cmp word [bp+6], 0
    je .pc_value
    dec ax
.pc_value:
    mov cx, [bp+8]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1                    ; kilobytes * 512 words
    cld
    rep stosw
    pop di
    pop es
    pop bp
    ret

; void sb_byte_clear(unsigned seg, int value)
_sb_byte_clear:
    push bp
    mov bp, sp
    push es
    push di
    mov es, [bp+4]
    xor di, di
    mov al, [bp+6]
    mov ah, al
    mov cx, 16384
    cld
    rep stosw
    pop di
    pop es
    pop bp
    ret

; void sb_plane_pixel(unsigned seg, unsigned off, int bit, int set)
_sb_plane_pixel:
    push bp
    mov bp, sp
    push es
    push bx
    push di
    mov es, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    mov bl, 1
    shl bl, cl
    cmp word [bp+10], 0
    je .pp_clear
    or [es:di], bl
    jmp short .pp_done
.pp_clear:
    not bl
    and [es:di], bl
.pp_done:
    pop di
    pop bx
    pop es
    pop bp
    ret

; int sb_plane_point(unsigned seg, unsigned off, int bit)
_sb_plane_point:
    push bp
    mov bp, sp
    push es
    push bx
    push di
    mov es, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    mov bl, 1
    shl bl, cl
    xor ax, ax
    test [es:di], bl
    jz .pg_done
    inc ax
.pg_done:
    pop di
    pop bx
    pop es
    pop bp
    ret

; byte accessors for the two 32K halves of the SCREEN 13 index surface.
_sb_byte_set:
    push bp
    mov bp, sp
    push es
    push di
    mov es, [bp+4]
    mov di, [bp+6]
    mov ax, [bp+8]
    mov [es:di], al
    pop di
    pop es
    pop bp
    ret

_sb_byte_get:
    push bp
    mov bp, sp
    push es
    push di
    mov es, [bp+4]
    mov di, [bp+6]
    xor ax, ax
    mov al, [es:di]
    pop di
    pop es
    pop bp
    ret

; Rebuild the 320x200 packed window image from exact SCREEN 13 indices.
; The near MAP table maps each virtual DAC entry to the nearest OS colour.
_sb_fb_map13:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    mov es, [bp+4]
    xor di, di
    xor si, si
    mov cx, 16000
.m13_pair:
    mov ds, [ss:bp+6]
    lodsb
    push es
    pop ds
    xor ah, ah
    mov bx, ax
    mov al, [bx+32000]
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov [es:di], al
    mov ds, [ss:bp+6]
    lodsb
    push es
    pop ds
    xor ah, ah
    mov bx, ax
    mov al, [bx+32000]
    or [es:di], al
    inc di
    loop .m13_pair
    ; second half: input starts at offset zero in HI; DI continues at 16384.
    xor si, si
    mov cx, 15616
.m13_hi_pair:
    mov ds, [ss:bp+8]
    lodsb
    push es
    pop ds
    xor ah, ah
    mov bx, ax
    mov al, [bx+32000]
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov [es:di], al
    mov ds, [ss:bp+8]
    lodsb
    push es
    pop ds
    xor ah, ah
    xor bx, bx
    mov bl, al
    mov al, [bx+32000]
    or [es:di], al
    inc di
    loop .m13_hi_pair
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

; int sb_seg_insert(unsigned seg, unsigned len, unsigned pos, int ch)
_sb_seg_insert:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    mov ax, [bp+6]
    cmp ax, 32767
    jae .ins_done
    mov dx, [bp+8]
    cmp dx, ax
    jbe .ins_pos_ok
    mov dx, ax
.ins_pos_ok:
    mov ds, [bp+4]
    mov es, [bp+4]
    mov cx, ax
    sub cx, dx
    jz .ins_store
    mov si, ax
    dec si
    mov di, ax
    std
    rep movsb
.ins_store:
    mov di, dx
    mov dx, [bp+10]
    mov [es:di], dl
    inc ax
    mov di, ax
    mov byte [es:di], 0
.ins_done:
    cld
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

; int sb_seg_delete(unsigned seg, unsigned len, unsigned pos)
_sb_seg_delete:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    mov ax, [bp+6]
    mov dx, [bp+8]
    cmp dx, ax
    jae .del_done
    mov ds, [bp+4]
    mov es, [bp+4]
    mov cx, ax
    sub cx, dx
    dec cx
    mov si, dx
    inc si
    mov di, dx
    cld
    rep movsb
    dec ax
    mov di, ax
    mov byte [es:di], 0
.del_done:
    cld
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

%include "speedybasic/sbnum.inc"
%include "speedybasic/sbmem.inc"

    CC_IMAGE_END
