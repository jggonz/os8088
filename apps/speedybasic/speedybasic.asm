; Speedy Basic C-package shim and the far-claim screen/editor movers.

%define OS88UI_ABOUT
%define OS88UI_NOBTN

%define CC_PKG_NAME 'SPEEDYBA'
%define CC_HAS_OVL
%define CC_HAS_ONKEY
%define CC_HAS_ONCLICK
%define CC_HAS_ONRESIZE
%define CC_HAS_MENUS
%define CC_HAS_ABOUT
%define CC_HAS_FDLG
%define CC_HAS_ONWAKE
%define CC_HAS_ONTIMER
%define CC_ICON "speedybasic/icon.inc"

%include "cc/crt0.asm"
%include "speedybasic.gen.asm"
%include "os88ui.inc"

section .text

_sb_far_peek:
    push bp
    mov bp, sp
    push es
    push bx
    mov es, [bp+4]
    mov bx, [bp+6]
    xor ax, ax
    mov al, [es:bx]
    pop bx
    pop es
    pop bp
    ret

_sb_far_poke:
    push bp
    mov bp, sp
    push es
    push bx
    mov es, [bp+4]
    mov bx, [bp+6]
    mov ax, [bp+8]
    mov [es:bx], al
    pop bx
    pop es
    pop bp
    ret

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

; Zero a newly claimed BASIC array without near-pointer aliasing.
_sb_memzero:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov cx, [bp+6]
    xor di, di
    xor ax, ax
    cld
    rep stosb
    pop es
    pop bp
    ret

; Map the retained mode-13h indices to the desktop's 16-colour surface.
_sb_fb_map:
    push bp
    mov bp, sp
    push ds
    push es
    mov es, [bp+6]
    mov dx, [bp+4]
    mov bx, [bp+8]
    xor si, si
    xor di, di
    mov cx, 32000
.pair:
    push ds
    mov ds, dx
    lodsw
    pop ds
    push ax
    xlatb
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    pop bp
    ; The second byte is the high byte of the saved pixel pair.
    xchg ax, bp
    mov al, ah
    xlatb
    or ax, bp
    stosb
    loop .pair
    pop es
    pop ds
    pop bp
    ret

%include "speedybasic/sbfloat.inc"
    CC_IMAGE_END
