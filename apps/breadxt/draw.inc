; Breadboard XT drawing core (SPEC.md 130.6.2). Original.
; Near cdecl: arguments at [bp+4].., DS = package data, SS = stack. Preserves
; BP, DS and DF (73.3); everything else is the caller's.
;
; Every face element is composed into ONE band in the caller's buffer and put
; down with one call. The band is ALREADY THE CARD'S FORMAT, so nothing is
; converted on the way out: on a colour display it is four bit planes (plane
; k at k * pw * h, pw = w / 8 bytes a row) for OSAPI_GFX_BLITP, which walks
; the clip region; on a 1bpp display it is one plane for OSAPI_GFX_BLIT1,
; each colour drawn as its 2x2 pattern from bx_mono. A fill is then a masked
; byte per plane row and a `rep stosb` between - the packed band and planar
; conversion it replaced cost ~64 cycles a pixel of every strip. Where BLITP
; refuses (a window part off the screen) bx_rows says so first, the strip
; holds half the rows, and bx_out unpacks the planes into the other half for
; OSAPI_GFX_BLIT4. The band's x and width are multiples of 8, so a band bit's
; x parity is the screen's. Art (sprites, fonts) is read from the art part's
; segment; with none, text uses the kernel's 8x8 glyphs and sprites draw
; nothing (the plain face, 130.6.3).
section .text
%ifdef BX_ART2
  %ifndef BX_TXID
    %fatal "BX_ART2 needs BX_TXID: the art ID of the first of the eight text records a handle names"
  %endif
BX_TSLOT  equ 64                    ; one resolved handle: 63 characters and a NUL
BX_TSLOTS equ 4                     ; ...four of them in the ring
%endif

; void bx_setup(unsigned seg,void *band,unsigned cap,unsigned artseg,int bpp)
; seg:band is the band, cap bytes; seg 0 = the package's own segment (DS,
; read afresh at every strip, since the region may move). A heap claim's
; segment lives in bx_bseg, which the package's os88_onmove keeps.
_bx_setup:
    push bp
    mov bp,sp
    mov ax,[bp+4]
    mov [_bx_bseg],ax
    mov ax,[bp+6]
    mov [bx_band],ax
    mov ax,[bp+8]
    mov [bx_cap],ax
    mov ax,[bp+10]
    mov [bx_art],ax
    mov ax,1
    cmp word [bp+12],4
    jb .np
    mov al,4
.np:
    mov [bx_np],ax
    mov byte [bx_lastc],0FFh
    call OSAPI_FONT_GLYPHS          ; DX:SI table, AL first, AH last
    mov [bx_gseg],dx
    mov [bx_goff],si
    mov [bx_gfirst],al
    mov [bx_glast],ah
    pop bp
    ret

; bx_resolve: bx_bs = the band's segment for this strip. Clobbers AX.
bx_resolve:
    mov ax,[_bx_bseg]
    test ax,ax
    jnz .s
    mov ax,ds
.s:
    mov [bx_bs],ax
    ret

; int bx_rows(int x,int y,int w,int h) - most rows a strip w wide holds, for
; a paint of this rect. On colour it asks BLITP first (a probe, walking any
; region); a refusal halves the rows to leave room for the packed copy.
_bx_rows:
    push bp
    mov bp,sp
    call bx_resolve
    mov byte [bx_nop],0
    mov ax,[bp+4]
    mov bx,[bp+6]
    mov cx,[bp+8]
    mov dx,[bp+10]
    jcxz .zero
    mov [bx_t1],cx
    cmp word [bx_np],1
    je .mono
    mov es,[bx_bs]
    mov si,[bx_band]
    mov di,0C000h                   ; probe, walking a region (5.4.3.6)
    mov bp,cx
    shr bp,1
    shr bp,1
    shr bp,1
    call OSAPI_GFX_BLITP
    mov bp,sp                       ; (BLITP preserved ours: the pw it took)
    mov cx,[bx_t1]
    jnc .planar
    inc byte [bx_nop]               ; planes and the packed copy: w a row
    jmp .div
.planar:
    shr cx,1                        ; four planes: w / 2 bytes a row
    jmp .div
.mono:
    shr cx,1
    shr cx,1
    shr cx,1                        ; one plane: w / 8
.div:
    xor dx,dx
    mov ax,[bx_cap]
    div cx
    cmp ax,255
    jbe .r
    mov ax,255
.r:
    pop bp
    ret
.zero:
    xor ax,ax
    pop bp
    ret

; void bx_new(int x,int y,int w,int h,int colour) - x, w multiples of 8;
; colour < 0 leaves the band as it is, for a scene that covers every pixel
_bx_new:
    push bp
    mov bp,sp
    call bx_resolve
    mov ax,[bp+4]
    mov [bx_x0],ax
    mov ax,[bp+6]
    mov [bx_y0],ax
    mov ax,[bp+8]
    mov [bx_w],ax
    mov cl,3
    shr ax,cl
    mov [bx_pw],ax
    mov cx,[bp+10]
    mov [bx_h],cx
    mul cx
    mov [bx_psz],ax
    mov al,[bp+12]
    test al,al
    js .nofill                      ; colour < 0: the caller covers every pixel
    call bx_solid
    mov ax,[bx_x0]
    mov bx,[bx_y0]
    mov cx,ax
    add cx,[bx_w]
    dec cx
    mov dx,bx
    add dx,[bx_h]
    dec dx
    call bx_rect
.nofill:
    pop bp
    ret

; bx_solid: AL = colour -> bx_pv for it on every row and x parity.
bx_solid:
    and al,15
    cmp al,[bx_lastc]               ; the same solid colour as the last ink:
    je .same                        ; bx_pv already holds it
    mov [bx_lastc],al
    mov ah,al
    mov [bx_cc],ax
    mov [bx_cc+2],ax
    jmp bx_ink
.same:
    ret
; bx_ink: bx_cc = colours for (even x, even y), (odd x, even y), (even x,
; odd y), (odd x, odd y) -> bx_pv[r * 4 + k], the byte plane k takes on a
; row of screen parity r. Clobbers AX, BX, CX, DX, SI, DI.
bx_ink:
    mov si,bx_cc
    mov di,bx_pv
    xor dx,dx                       ; DL = the row parity
.r:
    xor bx,bx
    cmp word [bx_np],1
    jne .col
    mov cl,dl
    shl cl,1                        ; this row's two bits of the 2x2 pattern
    mov bl,[si]
    mov al,[_bx_mono+bx]
    mov bl,[si+1]
    mov ah,[_bx_mono+bx]
    shr al,cl
    shr ah,cl
    xor ch,ch
    test al,1
    jz .m1
    or ch,0AAh
.m1:
    test ah,2
    jz .m2
    or ch,55h
.m2:
    mov [di],ch
    jmp .next
.col:
    mov bl,[si]
    mov bh,[si+1]
    mov cx,4
    push di
.k:
    xor al,al
    shr bl,1
    jnc .k1
    or al,0AAh
.k1:
    shr bh,1
    jnc .k2
    or al,55h
.k2:
    mov [di],al
    inc di
    loop .k
    pop di
.next:
    add di,4
    add si,2
    inc dl
    cmp dl,2
    jb .r
    ret

; bx_clip: AX,BX,CX,DX = screen x1,y1,x2,y2 -> band-local and clipped.
; CF = 1: nothing of it is in the band. Clobbers SI.
bx_clip:
    sub ax,[bx_x0]
    sub cx,[bx_x0]
    sub bx,[bx_y0]
    sub dx,[bx_y0]
    test ax,ax
    jge .x1
    xor ax,ax
.x1:
    mov si,[bx_w]
    dec si
    cmp cx,si
    jle .x2
    mov cx,si
.x2:
    cmp ax,cx
    jg .no
    test bx,bx
    jge .y1
    xor bx,bx
.y1:
    mov si,[bx_h]
    dec si
    cmp dx,si
    jle .y2
    mov dx,si
.y2:
    cmp bx,dx
    jg .no
    clc
    ret
.no:
    stc
    ret

; bx_rect: AX,BX,CX,DX = screen rect, painted with bx_pv (and bx_pat, the
; dotted-span mask). Per plane, one register-only row loop: a masked first
; byte, a `rep stosb`, a masked last byte (each `x ^ ((x ^ v) & m)`), and
; the even/odd row bytes swapped - the 8088 pays for every memory operand,
; so nothing per row reads a variable. Clobbers AX, BX, CX, DX, SI, DI.
bx_rect:
    call bx_clip
    jc .none
    push bp
    mov es,[bx_bs]                  ; the band
    cld
    mov [bx_ry1],bx
    sub dx,bx
    inc dx
    mov [bx_rh],dx                  ; rows
    mov si,ax
    mov di,cx
    mov cl,al
    and cl,7
    mov al,0FFh
    shr al,cl
    and al,[bx_pat]
    mov [bx_lm],al
    mov ax,di
    and al,7
    mov cl,7
    sub cl,al
    mov al,0FFh
    shl al,cl
    and al,[bx_pat]
    mov [bx_rm],al
    mov cl,3
    shr si,cl
    shr di,cl
    sub di,si                       ; DI = bytes after the first (0: one byte)
    mov [bx_nb],di
    jnz .two
    mov al,[bx_rm]
    and [bx_lm],al
.two:
    mov ax,[bx_pw]
    mul word [bx_ry1]
    add ax,[bx_band]
    add ax,si
    mov [bx_rp],ax                  ; plane 0's first byte
    mov ax,[bx_pw]
    sub ax,[bx_nb]
    dec ax
    mov [bx_rskip],ax               ; from past a row's last byte to the next row's first
    mov ax,[bx_ry1]
    add ax,[bx_y0]
    and ax,1
    shl ax,1
    shl ax,1
    mov [bx_rpar],ax                ; the first row's parity, as a bx_pv offset
    xor ax,ax
    mov [bx_rk],ax                  ; the plane
.plane:
    mov bx,[bx_rk]
    cmp bx,[bx_np]
    jae .done
    add bx,[bx_rpar]
    mov dl,[bx_pv+bx]               ; DL = the first row's byte, DH = the next's
    xor bx,4
    mov dh,[bx_pv+bx]
    mov bl,[bx_lm]
    mov bh,[bx_rm]
    mov bp,[bx_nb]
    mov si,[bx_rh]
    mov di,[bx_rp]
    cmp byte [bx_pat],0FFh
    jne .mrow
    test bp,bp                      ; one byte wide in a solid colour (a
    jnz .row                        ; frame side, a wire): set or clear its
    cmp dl,dh                       ; bits, a row an OR or an AND
    jne .row
    mov al,bl
    mov cx,si
    mov si,[bx_pw]
    inc dl
    jz .cor                         ; FFh: set
    dec dl
    jnz .colx                       ; neither: the general loop
    not al
.cand:
    and [es:di],al
    add di,si
    loop .cand
    jmp short .pnext
.cor:
    or [es:di],al
    add di,si
    loop .cor
    jmp short .pnext
.colx:
    mov si,cx                       ; (put SI back: the row count)
.row:
    mov al,[es:di]
    xor al,dl
    and al,bl
    xor [es:di],al
    inc di
    mov cx,bp
    jcxz .next
    dec cx
    mov al,dl
    mov ah,dl
    shr cx,1                        ; words: 14 clocks for two bytes, not 20
    rep stosw
    adc cx,cx
    rep stosb
    mov al,[es:di]
    xor al,dl
    and al,bh
    xor [es:di],al
    inc di
.next:
    add di,[bx_rskip]
    xchg dl,dh
    dec si
    jnz .row
    jmp .pnext
.mrow:                              ; a dotted span: every byte masked
    mov al,[es:di]
    xor al,dl
    and al,bl
    xor [es:di],al
    inc di
    mov cx,bp
    jcxz .mnext
    dec cx
    jcxz .mlast
.mmid:
    mov al,[es:di]
    xor al,dl
    and al,[bx_pat]
    xor [es:di],al
    inc di
    loop .mmid
.mlast:
    mov al,[es:di]
    xor al,dl
    and al,bh
    xor [es:di],al
    inc di
.mnext:
    add di,[bx_rskip]
    xchg dl,dh
    dec si
    jnz .mrow
.pnext:
    mov ax,[bx_psz]
    add [bx_rp],ax
    inc word [bx_rk]
    jmp .plane
.done:
    pop bp
.none:
    ret

; void bx_fill(int x1,int y1,int x2,int y2,int colour) - inclusive, clipped
_bx_fill:
    push bp
    mov bp,sp
    mov al,[bp+12]
    call bx_solid
    jmp bx_rectargs

; void bx_dither(int x1,int y1,int x2,int y2,int c1,int c2,int pat)
; pat 0 checker (c1 where x+y is even), 1 rows (c1 on even rows, c2 on odd),
; 2 sparse (c2 only where x and y are both odd).
_bx_dither:
    push bp
    mov bp,sp
    mov al,[bp+12]
    and al,15
    mov ah,[bp+14]
    and ah,15
    mov bx,[bp+16]
    mov dl,al                       ; DL, DH, CL, CH = the four cells
    mov dh,al
    mov cl,ah
    mov ch,ah
    cmp bx,1
    je .go
    mov cl,al
    cmp bx,2
    je .go
    mov dh,ah                       ; checker: c1 where x + y is even
    mov cl,ah
    mov ch,al
.go:
    mov [bx_cc],dx
    mov [bx_cc+2],cx
    mov byte [bx_lastc],0FFh
    call bx_ink
bx_rectargs:
    mov ax,[bp+4]
    mov bx,[bp+6]
    mov cx,[bp+8]
    mov dx,[bp+10]
    call bx_rect
    pop bp
    ret

; void bx_dots(int x1,int y1,int x2,int y2,int step,int colour)
; A dotted horizontal (y1 == y2) or vertical line, one pixel every step.
_bx_dots:
    push bp
    mov bp,sp
    cmp word [bp+12],1
    jge .h
    mov word [bp+12],1
.h:
    mov ax,[bp+6]
    cmp ax,[bp+10]
    jne .loop                       ; vertical: a dot at a time
    mov bx,[bp+12]
    mov al,0AAh
    cmp bx,2
    je .pat
    mov al,88h
    cmp bx,4
    je .pat
    mov al,80h
    cmp bx,8
    jne .loop
.pat:                               ; horizontal, step 2/4/8: ONE patterned
    mov cx,[bp+4]                   ; span, the dots at x1 + k * step
    and cl,7
    mov ah,al
    ror ax,cl                       ; (the pattern is byte-periodic, so a
    mov [bx_pat],al                 ; rotate places it at x1's bit)
    mov al,[bp+14]
    call bx_solid
    mov ax,[bp+4]
    mov bx,[bp+6]
    mov cx,[bp+8]
    mov dx,bx
    call bx_rect
    mov byte [bx_pat],0FFh
    pop bp
    ret
.loop:
    mov al,[bp+14]
    call bx_solid
    mov bx,[bp+6]
    cmp bx,[bp+10]
    jne .dot
    sub bx,[bx_y0]                  ; a horizontal run at any step: its row
    cmp bx,[bx_h]                   ; once, then a dot every step
    jae .done
    call bx_rowat
    mov es,[bx_bs]
    mov ax,[bp+4]
    sub ax,[bx_x0]
    mov cx,[bp+8]
    sub cx,[bx_x0]
.hd:
    cmp ax,cx
    jg .done
    call bx_plotr
    add ax,[bp+12]
    jmp .hd
.dot:
    mov ax,[bp+4]
    sub ax,[bx_x0]
    mov bx,[bp+6]
    sub bx,[bx_y0]
    call bx_plot
    mov ax,[bp+12]
    mov bx,[bp+6]
    cmp bx,[bp+10]
    jne .vert
    add [bp+4],ax
    mov ax,[bp+4]
    cmp ax,[bp+8]
    jle .dot
    jmp .done
.vert:
    add [bp+6],ax
    mov ax,[bp+6]
    cmp ax,[bp+10]
    jle .dot
.done:
    pop bp
    ret

; unsigned bx_size(int id) - a sprite's width | height << 8; 0 when absent
_bx_size:
    mov bx,sp
    mov bx,[ss:bx+2]
    call bx_art_at
    jc .none
    mov ax,[es:si]
    ret
.none:
    xor ax,ax
    ret

; int bx_artstr(int id,int n,char *buf) - the art text record's line n (from
; 0) into buf, at most 47 characters and a NUL. Answers 0 with buf = "" when
; there is no art part or no such line, else 1.
%ifdef BX_ART2
; Under BX_ART2 every text record is INDEXED (SPEC.md 131.10.7): dw n, n word
; offsets from the record's start, then the NUL-terminated lines - line n is
; one lookup, not a walk past n lines.
_bx_artstr:
    push bp
    mov bp,sp
    mov di,[bp+8]
    mov byte [di],0
    mov bx,[bp+4]
    mov ax,[bp+6]
    call bx_tline
    jc .no
    mov cx,47
    call bx_tcopy
    mov ax,1
    pop bp
    ret
.no:
    xor ax,ax
    pop bp
    ret

; bx_tline: BX = art ID, AX = line -> ES:SI = that line of the indexed text
; record; CF = 1 no such record or line. Clobbers AX, BX.
bx_tline:
    push ax
    call bx_art_at
    pop bx
    jc .no
    cmp bx,[es:si]
    jae .no
    shl bx,1
    add si,[es:si+bx+2]
    clc
    ret
.no:
    stc
    ret

; bx_tcopy: ES:SI -> DS:DI, at most CX characters, then a NUL. Clobbers AL,
; CX, SI, DI.
bx_tcopy:
    mov al,[es:si]
    inc si
    mov [di],al
    inc di
    test al,al
    jz .r
    loop bx_tcopy
    mov byte [di],0
.r:
    ret

; const char *bx_str(const char *s) - a text handle (1000h..1FFFh: line
; s & 1FFh of text record BX_TXID + ((s >> 9) & 7)) as a DS string in the
; next slot of the ring; any other value comes back unchanged (SPEC.md
; 131.10.7). A slot holds 63 characters and lasts four resolutions.
_bx_str:
    mov bx,sp
    mov ax,[ss:bx+2]
; bx_hstr: AX = a string or a handle -> AX = a DS string. Clobbers BX, CX,
; DX, SI, DI, ES.
bx_hstr:
    cmp ax,1000h
    jb .r
    cmp ax,2000h
    jae .r
    mov dx,[bx_tnext]               ; this slot; the next one after it
    mov bx,dx
    add bx,BX_TSLOT
    cmp bx,bx_tring + BX_TSLOT * BX_TSLOTS
    jb .k
    mov bx,bx_tring
.k:
    mov [bx_tnext],bx
    mov bx,ax
    mov cl,9
    shr bx,cl
    add bx,BX_TXID - 8
    and ax,1FFh
    call bx_tline
    mov di,dx
    mov byte [di],0
    jc .e
    mov cx,BX_TSLOT - 1
    call bx_tcopy
.e:
    mov ax,dx
.r:
    ret

; void bx_setart2(unsigned seg) - the second art part's segment: art IDs
; 256..511 are read from it with index ID - 256 (0 = none)
_bx_setart2:
    mov bx,sp
    mov ax,[ss:bx+2]
    mov [bx_art2],ax
    ret
%else
_bx_artstr:
    push bp
    mov bp,sp
    mov di,[bp+8]
    mov byte [di],0
    mov bx,[bp+4]
    call bx_art_at
    jc .no
    mov cx,[bp+6]
.skip:                              ; past n lines
    jcxz .copy
    cmp byte [es:si],0
    je .no                          ; the record's end (its last line's NUL + 0)
.sk1:
    mov al,[es:si]
    inc si
    test al,al
    jnz .sk1
    loop .skip
.copy:
    cmp byte [es:si],0
    je .no
    mov cx,47
.c1:
    mov al,[es:si]
    inc si
    mov [di],al
    inc di
    test al,al
    jz .yes
    loop .c1
    mov byte [di],0
.yes:
    mov ax,1
    pop bp
    ret
.no:
    xor ax,ax
    pop bp
    ret
%endif

; bx_art_at: BX = art ID -> ES:SI = its record; CF = 1 absent (no art part,
; or the ID has no record). Clobbers AX, BX. Under BX_ART2 an ID of 256..511
; is the second part's, index ID - 256 (SPEC.md 131.10.7).
bx_art_at:
    mov ax,[bx_art]
%ifdef BX_ART2
    test bh,bh
    jz .p1
    mov ax,[bx_art2]
    mov bh,0
.p1:
%endif
    test ax,ax
    jz .no
    mov es,ax
    shl bx,1
    mov si,[es:bx+4]
    test si,si
    jz .no
    clc
    ret
.no:
    stc
    ret

; int bx_spr(int x,int y,int id) - a sprite, light magenta transparent, each
; colour through bx_map. Answers its width (0 = absent, nothing drawn). Only
; the rows inside the strip are read; each row's address is found once.
_bx_spr:
    push bp
    mov bp,sp
    mov bx,[bp+8]
    call bx_art_at
    jc .zero
    xor ax,ax
    mov al,[es:si]
    mov [bx_sw],ax
    mov al,[es:si+1]
    mov [bx_sh],ax
    add si,2
    mov [bx_ssrc],si
    mov [bx_sseg],es
    mov ax,[bx_sw]
    inc ax
    shr ax,1
    mov [bx_sst],ax
    mov word [bx_sr],0
.row:
    mov ax,[bx_sr]
    cmp ax,[bx_sh]
    jae .done
    mov bx,ax
    add bx,[bp+6]
    sub bx,[bx_y0]
    cmp bx,[bx_h]
    jae .next                       ; the row is outside the strip
    call bx_rowat                   ; DI = its plane-0 row, BX = its bx_pv
    mov [bx_spar],bx
    mul word [bx_sst]
    add ax,[bx_ssrc]
    mov si,ax
    mov es,[bx_sseg]                ; the row into bx_srow, then ES = the band
    mov cx,[bx_sst]
    xor bx,bx
.cp:
    mov al,[es:si+bx]
    mov [bx_srow+bx],al
    inc bx
    loop .cp
    mov word [bx_srow+bx],0DDDDh    ; then eight transparent pixels, so a
    mov word [bx_srow+bx+2],0DDDDh  ; group of eight never reads past the row
    mov es,[bx_bs]
    mov ax,[bp+4]
    sub ax,[bx_x0]                  ; AX = local x of the sprite's column 0
    cmp word [bx_np],1
    je .mono
    call bx_sgrow                   ; colour: eight pixels a step
    jmp .next
.mono:
    xor cx,cx                       ; CX = the column
.col:
    cmp cx,[bx_sw]
    jae .next
    mov bx,cx
    shr bx,1
    mov dl,[bx_srow+bx]
    test cl,1
    jnz .lo
    push cx
    mov cl,4
    shr dl,cl
    pop cx
.lo:
    and dl,0Fh
    cmp dl,13
    je .skip
    push ax
    add ax,cx
    cmp ax,[bx_w]
    jae .clip
    mov bx,_bx_map
    xchg ax,dx
    xlat
    xchg ax,dx                      ; DL = the colour, AX = local x
    call bx_pixel
.clip:
    pop ax
.skip:
    inc cx
    jmp .col
.next:
    inc word [bx_sr]
    jmp .row
.done:
    mov ax,[bx_sw]
    pop bp
    ret
.zero:
    xor ax,ax
    pop bp
    ret

; bx_sgrow: a colour sprite row, eight pixels a step - AX = local x of its
; column 0, bx_srow = its nibbles (padded), ES:DI = the band row (plane 0).
; Each step gathers eight pixels into four plane bytes and a mask through
; bx_map (so a remap costs nothing extra), then merges them at the group's
; x as two masked bytes a plane. Clobbers AX, BX, CX, DX, SI.
bx_sgrow:
    push bp
    push di
    mov si,bx_srow
    mov bp,[bx_sw]                  ; pixels still to do
.group:
    push ax
    mov bx,_bx_map                  ; (eight shifts replace every bit of
    mov word [bx_sgn],4             ; CL, DL, DH, CH and AH: no clearing)             ; four bytes, two pixels each
.byte:
    mov al,[si]
    shr al,1
    shr al,1
    shr al,1
    shr al,1
    call bx_sgpx                    ; the high nibble is the left pixel
    mov al,[si]
    and al,0Fh
    call bx_sgpx
    inc si
    dec word [bx_sgn]
    jnz .byte
    mov [bx_sg],dx
    mov [bx_sg+2],ch
    mov [bx_sg+3],ah
    mov [bx_sg+4],cl
    pop ax
    push ax
    push si
    push di
    call bx_sgmerge
    pop di
    pop si
    pop ax
    add ax,8
    sub bp,8
    jg .group
    pop di
    pop bp
    ret
; bx_sgpx: AL = a nibble -> one pixel shifted into CL (mask), DL, DH, CH, AH
bx_sgpx:
    cmp al,13
    je .key
    xlat
    stc
    jmp .in
.key:
    xor al,al                       ; (clears CF: not drawn)
.in:
    rcl cl,1
    shr al,1
    rcl dl,1
    shr al,1
    rcl dh,1
    shr al,1
    rcl ch,1
    shr al,1
    rcl ah,1
    ret
; bx_sgmerge: AX = local x of a group, ES:DI = the band row (plane 0),
; bx_sg = its four plane bytes and mask -> merged, clipped to the strip.
bx_sgmerge:
    mov cx,ax
    and cl,7
    sar ax,1
    sar ax,1
    sar ax,1                        ; AX = the group's first byte (signed)
    add di,ax
    mov dh,[bx_sg+4]
    xor dl,dl
    shr dx,cl                       ; DH, DL = the mask in its two bytes
    xor si,si                       ; SI = the plane
.k:
    mov bh,[bx_sg+si]
    xor bl,bl
    shr bx,cl                       ; BH, BL = this plane's two bytes
    cmp ax,[bx_pw]
    jae .b2                         ; (a negative index is a large one)
    mov ch,[es:di]
    xor ch,bh
    and ch,dh
    xor [es:di],ch
.b2:
    inc ax
    cmp ax,[bx_pw]
    jae .b3
    mov ch,[es:di+1]
    xor ch,bl
    and ch,dl
    xor [es:di+1],ch
.b3:
    dec ax
    add di,[bx_psz]
    inc si
    cmp si,4
    jb .k
    ret

; bx_pixel: AX = local x (inside the strip), DL = colour, ES:DI = its row in
; plane 0, [bx_spar] = the row's bx_pv offset. Preserves everything but DX,
; DI's planes walk and the flags: AX, BX, CX, DI, SI are kept.
bx_pixel:
    push ax
    push bx
    push cx
    push di
    mov bx,ax
    and bx,7
    mov dh,[bx_bits+bx]             ; DH = the pixel's bit
    mov cl,3
    shr ax,cl
    add di,ax
    cmp word [bx_np],1
    je .mono
    mov cx,4
.k:
    shr dl,1
    jnc .clr
    or [es:di],dh
    jmp .nx
.clr:
    mov al,dh
    not al
    and [es:di],al
.nx:
    add di,[bx_psz]
    loop .k
    jmp .out
.mono:                              ; the colour's 2x2 pattern bit here
    mov bl,dl
    xor bh,bh
    mov al,[_bx_mono+bx]
    mov cx,[bx_spar]
    shr cx,1                        ; row parity * 2
    pop bx                          ; (DI, saved: the row; x parity from it)
    push bx
    mov bx,sp
    mov bx,[ss:bx+6]                ; the caller's AX: local x
    and bx,1
    add cx,bx
    shr al,cl
    test al,1
    jz .mclr
    or [es:di],dh
    jmp .out
.mclr:
    not dh
    and [es:di],dh
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; bx_face: AX = font ID (negative = the kernel's 8x8). Sets the bx_f* face.
bx_face:
    test ax,ax
    js .sys
    mov bx,ax
    call bx_art_at
    jc .sys
    mov [bx_fseg],es
    mov [bx_frec],si
    xor ax,ax
    mov al,[es:si]
    mov [bx_fh],ax
    mov al,[es:si+2]
    mov [bx_ffirst],ax
    mov al,[es:si+3]
    mov [bx_fn],ax
    mov byte [bx_fsys],0
    ret
.sys:
    mov ax,[bx_gseg]
    mov [bx_fseg],ax
    mov word [bx_fh],8
    xor ax,ax
    mov al,[bx_gfirst]
    mov [bx_ffirst],ax
    mov al,[bx_glast]
    sub al,[bx_gfirst]
    inc ax
    mov [bx_fn],ax
    mov byte [bx_fsys],1
    ret

; bx_gwid: AL = character -> AX = its advance, ES:SI = its glyph (0 =
; nothing to draw): in the art a record (db top, db rows, two bytes a row),
; in the kernel's 8x8 eight bytes. Needs bx_face. Clobbers BX, CX, ES.
bx_gwid:
    xor ah,ah
    sub ax,[bx_ffirst]
    jb .none
    cmp ax,[bx_fn]
    jae .none
    mov es,[bx_fseg]
    cmp byte [bx_fsys],0
    jne .sys
    mov bx,[bx_frec]
    add bx,ax
    xor cx,cx
    mov cl,[es:bx+4]
    mov bx,[bx_frec]
    add bx,4
    add bx,[bx_fn]
    shl ax,1
    add bx,ax
    mov si,[es:bx]
    test si,si
    jz .w
    add si,[bx_frec]
.w:
    mov ax,cx
    ret
.sys:
    mov cl,3
    shl ax,cl
    add ax,[bx_goff]
    mov si,ax
    mov ax,8
    ret
.none:
    xor ax,ax
    xor si,si
    ret

; int bx_twidth(const char *s,int font) - pixels
_bx_twidth:
    push bp
    mov bp,sp
%ifdef BX_ART2
    mov ax,[bp+4]                   ; a text handle names a record's line
    call bx_hstr
    mov [bp+4],ax
%endif
    mov ax,[bp+6]
    call bx_face
    xor dx,dx
    mov di,[bp+4]
.c:
    mov al,[di]
    test al,al
    jz .e
    push dx
    call bx_gwid
    pop dx
    add dx,ax
    inc di
    jmp .c
.e:
    mov ax,dx
    pop bp
    ret

; int bx_fonth(int font) - the face's height in rows
_bx_fonth:
    mov bx,sp
    mov ax,[ss:bx+2]
    call bx_face
    mov ax,[bx_fh]
    ret

; int bx_text(int x,int y,const char *s,int colour,int font)
; Transparent glyphs over whatever the band holds (the band itself is the
; opaque ground: nothing reaches the glass twice). y is the face's top row.
; Answers the advance in pixels. A glyph row is read WHOLE (two bytes in the
; art, one in the kernel's 8x8), shifted to the pen's bit once, and merged
; into each plane as up to three masked bytes - no bit is walked. Only rows
; that are lit and inside the strip are touched; a glyph cut by the strip's
; left or right edge falls back to a pixel at a time.
_bx_text:
    push bp
    mov bp,sp
%ifdef BX_ART2
    mov ax,[bp+8]                   ; a text handle names a record's line
    call bx_hstr
    mov [bp+8],ax
%endif
    mov ax,[bp+10]
    cmp word [bx_np],1              ; 1bpp: a glyph in a grey's dot pattern
    jne .ink                        ; cannot be read, so text is white where
    and ax,15                       ; the colour is all lit, else black
    mov bx,ax
    cmp byte [_bx_mono+bx],15
    mov al,0
    jne .ink
    mov al,15
.ink:
    call bx_solid                   ; bx_pv: the colour's plane bytes
    mov ax,[bp+12]
    call bx_face
    mov ax,[bp+4]
    sub ax,[bx_x0]
    mov [bx_tx],ax                  ; local x of the pen
    mov [bx_tx0],ax
    mov ax,[bp+6]
    sub ax,[bx_y0]
    mov [bx_ty],ax                  ; local y of the face's top row
    xor bx,bx                       ; r0 = max(0, -ty)
    neg ax
    jle .r0
    mov bx,ax
.r0:
    mov [bx_tr0],bx
    mov ax,[bx_h]                   ; r1 = min(fh, h - ty)
    sub ax,[bx_ty]
    cmp ax,[bx_fh]
    jle .r1
    mov ax,[bx_fh]
.r1:
    mov [bx_tr1],ax
    mov ax,[bp+8]
    mov [bx_tp],ax
.char:
    mov si,[bx_tp]
    mov al,[si]
    test al,al
    jz .end
    inc word [bx_tp]
    call bx_gwid                    ; AX = advance, ES:SI = the glyph
    mov [bx_gw],ax
    test si,si
    jz .adv
    test ax,ax
    jz .adv
    mov cx,[bx_tx]
    cmp cx,[bx_w]
    jge .adv                        ; right of the strip
    add cx,ax
    jle .adv                        ; left of it
    mov byte [bx_tslow],0
    cmp cx,[bx_w]
    jg .slow
    cmp word [bx_tx],0
    jge .fast
.slow:
    mov byte [bx_tslow],1
.fast:
    xor ax,ax                       ; AX = the glyph's first lit row, CX = count
    mov cx,8
    mov word [bx_tstr],1
    cmp byte [bx_fsys],0
    jne .rows
    mov al,[es:si]
    mov cl,[es:si+1]
    add si,2
    mov word [bx_tstr],2
.rows:
    mov dx,ax                       ; DX = the first row to draw: max(top, r0)
    cmp dx,[bx_tr0]
    jge .a
    mov dx,[bx_tr0]
.a:
    add cx,ax                       ; CX = past the last: min(top + n, r1)
    cmp cx,[bx_tr1]
    jle .b
    mov cx,[bx_tr1]
.b:
    cmp dx,cx
    jge .adv
    mov [bx_tre],cx
    mov bx,dx                       ; skip (first - top) rows of the glyph
    sub bx,ax
    cmp word [bx_tstr],1
    je .s1
    shl bx,1
.s1:
    add si,bx
    mov [bx_gr],dx
.grow:
    mov bx,[bx_gr]
    cmp bx,[bx_tre]
    jge .adv
    inc word [bx_gr]
    mov es,[bx_fseg]                ; the glyph's segment, then the band's
    mov ah,[es:si]                  ; AX = the row, leftmost pixel in bit 15
    xor al,al
    cmp word [bx_tstr],1
    je .g1
    mov al,[es:si+1]
.g1:
    add si,[bx_tstr]
    test ax,ax
    jz .grow
    add bx,[bx_ty]                  ; BX = the local row, inside the strip
    mov es,[bx_bs]
    call bx_rowat                   ; DI = its plane-0 row, BX = its bx_pv
    cmp byte [bx_tslow],0
    jne .px
    mov cx,[bx_tx]
    and cx,7
    xor dl,dl                       ; DL collects what falls off the right
    jcxz .sh0
.sh:
    shr ax,1
    rcr dl,1
    loop .sh
.sh0:                               ; AH, AL, DL = the row's three bytes
    mov cx,[bx_tx]
    shr cx,1
    shr cx,1
    shr cx,1
    add di,cx                       ; DI = the pen's byte in plane 0
    mov bp,[bx_np]                  ; (BP is free: the arguments are read)
.k:                                 ; a solid colour is 00 or FFh a plane:
    mov dh,[bx_pv+bx]               ; set or clear the glyph's bits; only a
    inc dh                          ; 1bpp pattern needs the masked merge
    jz .or
    dec dh
    jnz .gen
    not ax
    not dl
    and [es:di],ah
    and [es:di+1],al
    and [es:di+2],dl
    not ax
    not dl
    jmp short .k3
.or:
    or [es:di],ah
    or [es:di+1],al
    or [es:di+2],dl
    jmp short .k3
.gen:
    mov cl,[es:di]
    xor cl,dh
    and cl,ah
    xor [es:di],cl
    mov cl,[es:di+1]
    xor cl,dh
    and cl,al
    xor [es:di+1],cl
    mov cl,[es:di+2]
    xor cl,dh
    and cl,dl
    xor [es:di+2],cl
.k3:
    add di,[bx_psz]
    inc bx
    dec bp
    jnz .k
    jmp .grow
.px:                                ; cut by the strip's edge: pixel by pixel
    mov dx,ax
    mov ax,[bx_tx]
    mov cx,[bx_gw]
.pb:
    shl dx,1
    jnc .pn
    call bx_plotr
.pn:
    inc ax
    loop .pb
    jmp .grow
.adv:
    mov ax,[bx_gw]
    add [bx_tx],ax
    jmp .char
.end:
    mov ax,[bx_tx]
    sub ax,[bx_tx0]
    pop bp
    ret

; bx_plot: AX = local x, BX = local y (either may be outside the band), in
; the colour bx_pv holds. Preserves everything but the flags.
bx_plot:
    cmp bx,[bx_h]
    jae .no
    push bx
    push di
    push es
    mov es,[bx_bs]
    call bx_rowat
    call bx_plotr
    pop es
    pop di
    pop bx
.no:
    ret

; bx_rowat: BX = local y -> DI = its row in plane 0, BX = its parity's
; offset into bx_pv. Preserves AX, CX, DX, SI.
bx_rowat:
    push ax
    push dx
    mov ax,[bx_pw]
    mul bx
    add ax,[bx_band]
    mov di,ax
    add bx,[bx_y0]
    and bx,1
    shl bx,1
    shl bx,1
    pop dx
    pop ax
    ret

; bx_plotr: AX = local x on the row bx_rowat gave (DI, BX), ES = the band
; (bx_bs). Clipped in x.
; Preserves everything but the flags.
bx_plotr:
    cmp ax,[bx_w]
    jae .no
    push ax
    push bx
    push cx
    push dx
    push di
    mov cx,ax
    and cx,7
    xchg cx,bx
    mov dh,[bx_bits+bx]             ; DH = the pixel's bit
    mov bx,cx
    mov cl,3
    shr ax,cl
    add di,ax
    mov cx,[bx_np]
.k:
    mov al,[bx_pv+bx]
    test al,dh
    jz .clr
    or [es:di],dh
    jmp .nx
.clr:
    mov al,dh
    not al
    and [es:di],al
.nx:
    add di,[bx_psz]
    inc bx
    loop .k
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
.no:
    ret

; int bx_out(void) - put the band on the glass. 0 drawn, -1 refused.
_bx_out:
    push bp
    mov es,[bx_bs]
    mov si,[bx_band]
    mov bp,[bx_pw]
    mov ax,[bx_x0]
    mov bx,[bx_y0]
    mov cx,[bx_w]
    mov dx,[bx_h]
    cmp word [bx_np],1
    je .mono
    cmp byte [bx_nop],0
    jne .packed
    mov di,[bx_psz]
    or di,4000h                     ; WALK an armed clip region (5.4.3.6)
    call OSAPI_GFX_BLITP
    jmp .done
.mono:
    call OSAPI_GFX_BLIT1
.done:
    sbb ax,ax
    pop bp
    ret
.packed:
    call bx_unplane
    mov es,[bx_bs]
    mov si,[bx_dst]
    mov bp,[bx_w]
    shr bp,1
    mov ax,[bx_x0]
    mov bx,[bx_y0]
    mov cx,[bx_w]
    mov dx,[bx_h]
    call OSAPI_GFX_BLIT4
    xor ax,ax
    pop bp
    ret

; bx_unplane: the four planes -> packed 4bpp at [bx_dst] (just past them),
; high nibble = the even x. Only where BLITP refused, so it may be slow.
bx_unplane:
    mov si,[bx_band]
    mov di,[bx_psz]
    shl di,1
    shl di,1
    add di,si
    mov [bx_dst],di
    mov cx,[bx_psz]
    mov es,[bx_bs]                  ; the band, both sides
    cld
.byte:
    push cx
    push si
    mov bl,[es:si]
    add si,[bx_psz]
    mov bh,[es:si]
    add si,[bx_psz]
    mov dl,[es:si]
    add si,[bx_psz]
    mov dh,[es:si]
    mov ch,4
.pair:
    mov cl,2
    xor al,al
.px:
    shl dh,1
    rcl al,1
    shl dl,1
    rcl al,1
    shl bh,1
    rcl al,1
    shl bl,1
    rcl al,1
    dec cl
    jnz .px
    stosb
    dec ch
    jnz .pair
    pop si
    inc si
    pop cx
    loop .byte
    ret

section .data
_bx_bseg: dw 0                       ; the band's heap claim; 0 = DS (bx_setup)
_bx_map: db 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
bx_pat: db 0FFh                     ; bx_rect's dot mask (bx_dots)
bx_bits: db 80h,40h,20h,10h,8,4,2,1
; 2x2 lit pattern per colour: bit0 (even x, even y), bit1 (odd x, even y),
; bit2 (even x, odd y), bit3 (odd x, odd y). SPEC.md 130.6.2's mono table.
_bx_mono: db 0,0,0,0,0,0,9,7,6,0,15,15,15,15,15,15
%ifdef BX_ART2
bx_tnext: dw bx_tring               ; the ring slot bx_hstr fills next
%endif
section .bss
%ifdef BX_ART2
bx_art2: resw 1
bx_tring: resb BX_TSLOT * BX_TSLOTS
%endif
bx_band: resw 1
bx_bs: resw 1
%ifdef BX_ART2
bx_srow: resb 64                    ; a row of up to 120 pixels (BPART2's LINKED), packed, plus 4 pad
%else
bx_srow: resb 36
%endif
bx_sg: resb 5
bx_sgn: resw 1
bx_cap: resw 1
bx_art: resw 1
bx_np: resw 1
bx_gseg: resw 1
bx_goff: resw 1
bx_gfirst: resb 1
bx_glast: resb 1
bx_nop: resb 1
bx_lm: resb 1
bx_lastc: resb 1
bx_rm: resb 1
bx_x0: resw 1
bx_y0: resw 1
bx_w: resw 1
bx_h: resw 1
bx_pw: resw 1
bx_psz: resw 1
bx_dst: resw 1
bx_t1: resw 1
bx_ry1: resw 1
bx_ry2: resw 1
bx_b1: resw 1
bx_nb: resw 1
bx_cc: resb 4
bx_pv: resb 8
bx_sw: resw 1
bx_sh: resw 1
bx_sst: resw 1
bx_ssrc: resw 1
bx_sseg: resw 1
bx_sr: resw 1
bx_spar: resw 1
bx_fseg: resw 1
bx_frec: resw 1
bx_fh: resw 1
bx_ffirst: resw 1
bx_fn: resw 1
bx_fsys: resb 1
bx_tx: resw 1
bx_tx0: resw 1
bx_ty: resw 1
bx_tp: resw 1
bx_gw: resw 1
bx_gr: resw 1
bx_tr0: resw 1
bx_tr1: resw 1
bx_rp: resw 1
bx_tre: resw 1
bx_tstr: resw 1
bx_tslow: resb 1
bx_rh: resw 1
bx_rskip: resw 1
bx_rpar: resw 1
bx_rk: resw 1
section .text

; Breadboard XT's own tail. A package sharing the band core as source
; (Byteopoly, SPEC.md 131.8) defines BX_CORE_ONLY and takes the core alone.
%ifndef BX_CORE_ONLY
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
%endif
