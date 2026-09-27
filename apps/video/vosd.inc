; =============================================================================
; THE FULL SCREEN'S TEXT (SPEC.md 98.3.13): "Paused", where a seek will land,
; "Repeat on". No kernel drawing slot is legal in a bracket that has set a
; mode (SPEC.md 53.7), so the box is drawn here, from the kernel's own 8x8
; glyphs (OSAPI_FONT_GLYPHS), one renderer a screen mode:
;
;   one bit    CGA 640, Hercules, mode 12h - the glyph's byte as it is
;   two bits   CGA 320 - each bit doubled, colour 3
;   a byte     13h - each bit a pixel, 255 (a VGA8 palette is sorted
;              darkest first, so 255 is its brightest) or 0
;   Mode X     the same, a plane at a time
;   C160       the text hack's cells, two pixels an attribute
;
; THE SCREEN IS THE DECODER'S REFERENCE. Played onto the screen, a record
; writes only what changed, so a byte the box covers must hold the PICTURE
; whenever a record is decoded. The bytes under the box are saved before it
; is drawn, and while it is up the hook decodes through vd_clip, which puts
; a write that lands in the box INTO THE SAVE instead of on the screen: the
; text never leaves the glass, and the save is always the picture under it
; (98.3.13.1). Mode X flipping decodes into the page that is not on the
; glass, so there the page is put back before the hook decodes into it
; (vo_pre) and the box drawn on it before it is shown (vo_flipon) - a save
; a page. Through the shadow the screen is only a copy: the copy is the
; restore, and it is made round the box, so it is drawn once (vo_bparts).
;
; The main loop changes what is shown only with IRQ0 held off (vo_show), so
; the hook never meets a box half changed.
; =============================================================================
VO_ROWS     equ 12                  ; the box: 2 rows of ground, the glyph's
VO_YOFF     equ 4                   ; 8, 2 more - 4 rows into the canvas
VO_MAXC     equ 10                  ; the longest line: "Repeat off"
VO_SAV      equ 1152                ; a page's save at the widest: 96 x 12
VO_TOASTT   equ 27                  ; a toast's ticks: 1.5 s
VO_INK      equ 255                 ; 256 colours' brightest (the palette's
                                    ; order, SPEC.md 98.2.3)
VOK_PAUSE   equ 1                   ; what the box says
VOK_SEEK    equ 2
VOK_REPON   equ 3
VOK_REPOFF  equ 4
VOK_NOKEY   equ 5
VOM_1       equ 1                   ; the renderers
VOM_2       equ 2
VOM_8       equ 3
VOM_X       equ 4
VOM_T       equ 5

; vo_setup - a bracket's surface is up: the renderer its screen takes, and
; how many characters the canvas fits. None in the window (98.3.7), whose
; controls say what the keys did. Clobbers AX, BX, CX, DX, SI
vo_setup:
    xor ax, ax
    mov [vo_mode], al
    mov [vo_drawn], al
    mov [vo_on], al
    mov [vo_chg], al
    mov [vo_kind], al
    mov [vo_pz], al
    mov [vo_toast], al
    mov [vo_skp], al
    mov [vo_clip], al
    cmp byte [vp_winm], 0
    jne .out
    push es
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL..AH, CX bytes a glyph
    pop es
    cmp cx, 8
    jne .out
    mov [vo_gseg], dx
    mov [vo_goff], si
    mov [vo_gfst], al
    mov [vo_glst], ah
    mov bl, [vp_fsmode]             ; this screen's renderer, the bytes a
    cmp bl, FSXM_MODEX              ; character takes (a plane's), and the
    ja .out                         ; box's left, in the same bytes
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax
    mov al, [vo_mtab+bx]
    or al, al
    jz .out
    mov cl, [vo_mtab+bx+1]
    mov [vo_bpc], cl
    mov cl, [vo_mtab+bx+2]
    xor ch, ch
    mov [vo_xoff], cx
    mov ax, [vp_wb]                 ; the characters the canvas holds
    sub ax, cx
    jbe .out
    xor dx, dx
    mov cl, [vo_bpc]
    div cx
    sub ax, 2
    jbe .out
    cmp ax, VO_MAXC
    jbe .cm
    mov ax, VO_MAXC
.cm:
    mov [vo_cmax], al
    cmp word [vp_h], VO_YOFF + VO_ROWS
    jb .out
    mov bl, [vo_mtab+bx]            ; (the renderer, again)
    mov cl, 1                       ; the planes a save takes: Mode X's four,
    mov ch, 0                       ; VGA4's four, else one
    cmp bl, VOM_X
    jne .p1
    mov cx, 0x0104
    jmp short .pl
.p1:
    cmp byte [vp_fsmode], FSXM_VGA12
    jne .pl
    mov ch, 1                       ; mode 12h: the planes are the ports'
    cmp byte [vp_planar], 2
    jne .pl
    mov cl, 4
.pl:
    mov [vo_pl], cl
    mov [vo_mx], ch
    cmp bl, VOM_X
    jne .mk
    mov byte [vo_mx], 1
.mk:
    mov [vo_mode], bl
.out:
    ret

; vo_show - AL = what the box says (0 nothing), SI = its text, AH = 1 to draw
; it again though the kind is the same (a seek's time, moved). The screen is
; made to match NOW, with IRQ0 held off. Preserves all
vo_show:
    cmp byte [vo_mode], 0
    je .ret
    or ah, ah
    jnz .set
    cmp al, [vo_kind]
    jne .set
.ret:
    ret
.set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    cli
    mov [vo_kind], al
    mov di, vo_text
    xor cx, cx
.cp:
    mov ah, [si]
    or ah, ah
    jz .cd
    cmp cx, VO_MAXC
    jae .cd
    mov [di], ah
    inc di
    inc si
    inc cx
    jmp short .cp
.cd:
    mov byte [di], 0
    mov [vo_n], cl
    mov byte [vo_on], 0
    or al, al
    jz .chg
    jcxz .chg
    cmp cl, [vo_cmax]
    ja .chg                         ; wider than the canvas: not shown
    mov byte [vo_on], 1
.chg:
    mov byte [vo_chg], 1
    call vo_post
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vo_update - the main loop's: what the box should say now - a seek on its
; way says its own (vp_skkey), then a toast while it lasts, then "Paused" if
; Space paused the play. Preserves all
vo_update:
    cmp byte [vo_mode], 0
    je .ret
    cmp byte [vo_skp], 0
    jne .ret
    push ax
    push si
    mov al, [vo_toast]
    or al, al
    jz .p
    push dx
    call OSAPI_GET_TICKS
    pop dx
    sub ax, [vo_ttk]
    cmp ax, VO_TOASTT
    mov al, [vo_toast]
    jb .sh
    mov byte [vo_toast], 0
.p:
    xor al, al
    cmp byte [vo_pz], 0
    je .sh
    mov al, VOK_PAUSE
.sh:
    mov si, vo_s_none
    cmp al, VOK_PAUSE
    jne .s1
    mov si, vo_s_pause
.s1:
    cmp al, VOK_REPON
    jne .s2
    mov si, vo_s_ron
.s2:
    cmp al, VOK_REPOFF
    jne .s3
    mov si, vo_s_roff
.s3:
    cmp al, VOK_NOKEY
    jne .s4
    mov si, vo_s_nokey
.s4:
    xor ah, ah
    call vo_show
    pop si
    pop ax
.ret:
    ret

; vo_toast_rep - R in the full screen: Repeat's new state, said for 1.5 s
vo_toast_rep:
    push ax
    mov al, VOK_REPON
    cmp byte [vp_rep], 0
    jne .k
    mov al, VOK_REPOFF
.k:
    call vo_toastk
    pop ax
    ret

; vo_toastk - AL = a toast's kind: shown, from now. Preserves all
vo_toastk:
    push ax
    push dx
    mov [vo_toast], al
    call OSAPI_GET_TICKS
    mov [vo_ttk], ax
    mov byte [vo_kind], 0xFF        ; (shown even if it was already)
    pop dx
    pop ax
    jmp vo_update

; vo_off - the bracket is ending: nothing shown, the picture whole. The
; hook must be idle. Preserves all
vo_off:
    mov byte [vo_skp], 0
    mov byte [vo_toast], 0
    mov byte [vo_pz], 0
    push ax
    push si
    xor ax, ax
    mov si, vo_s_none
    call vo_show
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; the hook's two (and vo_show's): the box off the page about to be decoded
; into, and the box as it should be once the frames are drawn. Clobber all
; -----------------------------------------------------------------------------
vo_pre:
    cmp byte [vo_mode], 0
    je .ret
    cmp byte [vo_clip], 0           ; decoded round (vd_clip): nothing to do
    jne .ret
    cmp byte [vp_shadow], 0         ; through the shadow the copy restores
    jne .ret
    mov bl, 1                       ; the page the next record goes into
    cmp word [vp_poff], 0
    je .p
    inc bl
.p:
    test [vo_drawn], bl
    jz .ret
    not bl
    and [vo_drawn], bl
    not bl
    shr bl, 1                       ; (bit -> page index)
    mov al, 1
    jmp vo_io
.ret:
    ret

vo_post:
    cmp byte [vo_mode], 0
    je .ret
    cmp byte [vp_shadow], 0
    jne .shd
    cmp byte [vo_on], 0             ; ONTO THE SCREEN: off, or a new text,
    je .un                          ; and every page it is on is put back
    cmp byte [vo_chg], 0
    je .dr
.un:
    call vo_unall
    mov byte [vo_chg], 0
    cmp byte [vo_on], 0
    je .ret
.dr:
    mov bl, 1                       ; ...then onto the page on the glass
    cmp word [vp_foff], 0
    je .g
    inc bl
.g:
    test [vo_drawn], bl
    jnz .ret
    or [vo_drawn], bl
    mov al, [vo_n]                  ; the width it is drawn at - which the
    mov [vo_dn], al                 ; save has to be taken at, too
    shr bl, 1
    push bx
    xor al, al
    call vo_io                      ; what it covers, kept
    pop bx
    call vo_draw
    jmp vc_setup                    ; ...and decoded round from now on
.ret:
    ret
.shd:                               ; THROUGH THE SHADOW: the copy of the
    cmp byte [vo_on], 0             ; box's rows is the restore
    je .su
    cmp byte [vo_chg], 0
    je .sd
.su:
    cmp byte [vo_drawn], 0
    je .sc
    mov byte [vo_drawn], 0          ; (so the copy takes the box's bytes too)
    mov ax, [vp_dy0]
    cmp ax, VO_YOFF
    jbe .y0
    mov word [vp_dy0], VO_YOFF
.y0:
    cmp word [vp_dy1], VO_YOFF + VO_ROWS
    jae .y1
    mov word [vp_dy1], VO_YOFF + VO_ROWS
.y1:
    call vp_blit
.sc:
    mov byte [vo_chg], 0
    cmp byte [vo_on], 0
    je .ret
.sd:
    cmp byte [vo_drawn], 0          ; drawn, and no copy has crossed it
    jne .ret
    mov byte [vo_drawn], 1
    xor bl, bl
    jmp vo_draw

; vo_unall - every page the box is on, put back. Clobbers all
vo_unall:
    mov byte [vo_clip], 0           ; (the save is the picture: the screen
                                    ; is the reference again)
    test byte [vo_drawn], 1
    jz .p1
    xor bl, bl
    mov al, 1
    call vo_io
.p1:
    test byte [vo_drawn], 2
    jz .p2
    mov bl, 1
    mov al, 1
    call vo_io
.p2:
    mov byte [vo_drawn], 0
    ret

; vo_flipon - FLIPPING (98.3.8): the page just decoded is about to be shown,
; and the text goes onto it FIRST - drawn after the flip, it was off the
; glass until the beam had passed its rows, 17 looks in 20 (98.3.13.1).
; Preserves all
vo_flipon:
    cmp byte [vo_mode], 0
    je .ret
    cmp byte [vo_on], 0
    je .ret
    cmp byte [vo_chg], 0            ; (a new text is vo_post's)
    jne .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bl, 1
    cmp word [vp_poff], 0
    je .p
    inc bl
.p:
    test [vo_drawn], bl
    jnz .o
    or [vo_drawn], bl
    mov al, [vo_n]
    mov [vo_dn], al
    shr bl, 1
    push bx
    xor al, al
    call vo_io
    pop bx
    call vo_draw
.o:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; vo_bparts - THROUGH THE SHADOW, vp_blit's row CX in three parts: [vb_n1]
; units copied, [vb_n2] skipped - the text's, while it is on the glass, so
; a copy never goes over it (98.3.13.1) - and [vb_n3] copied. Units are the
; canvas's: bytes, or C160's cells. Preserves all
vo_bparts:
    push ax
    mov ax, [vp_wb]
    mov [vb_n1], ax
    xor ax, ax
    mov [vb_n2], ax
    mov [vb_n3], ax
    cmp byte [vo_drawn], 0
    je .out
    mov ax, cx
    sub ax, VO_YOFF
    jb .out
    cmp ax, VO_ROWS
    jae .out
    mov ax, [vo_xoff]
    mov [vb_n1], ax
    mov al, [vo_dn]
    add al, 2
    mul byte [vo_bpc]
    mov [vb_n2], ax
    mov ax, [vp_wb]
    sub ax, [vb_n1]
    sub ax, [vb_n2]
    mov [vb_n3], ax
.out:
    pop ax
    ret

; vo_page - BL = a page index -> [vo_pgo] its offset (Mode X's second page
; at VP_PAGE; nothing else has one)
vo_page:
    mov word [vo_pgo], 0
    or bl, bl
    jz .ret
    mov word [vo_pgo], VP_PAGE
.ret:
    ret

; vo_addr - AX = a row of the box -> DI = its first byte on the screen, the
; page left out. Preserves all but DI
vo_addr:
    push ax
    push bx
    add ax, [vp_ty0]
    add ax, VO_YOFF
    mov bl, [vp_tlay]
    call vp_rowaddr
    add ax, [vp_tx0]
    add ax, [vo_xoff]
    cmp byte [vo_mode], VOM_T       ; C160: cells, each an attribute at the
    jne .d                          ; odd byte of two
    shl ax, 1
    inc ax
.d:
    mov di, ax
    pop bx
    pop ax
    ret

; vo_io - AL = 0 keep what the box covers on page BL, 1 put it back: every
; plane, [vo_dn] characters wide. Clobbers all
vo_io:
    mov [vo_dir], al
    call vo_page
    mov bp, vo_sav
    or bl, bl
    jz .b
    add bp, VO_SAV
.b:
    mov al, [vo_dn]
    add al, 2
    mul byte [vo_bpc]
    mov [vo_w], ax
    cld
    xor cx, cx                      ; CL = the plane
.pl:
    cmp byte [vo_mx], 0
    je .rows
    cmp byte [vo_pl], 1             ; mode 12h, one bit a pixel: every
    jne .sel                        ; plane holds it - plane 0 read, all
    cmp byte [vo_dir], 0            ; four written
    jne .all
.sel:
    call vp_mxsel
    jmp short .rows
.all:
    call vp_mxall
.rows:
    xor ax, ax                      ; AX = the row
.r:
    call vo_addr
    add di, [vo_pgo]
    push cx
    mov cx, [vo_w]
    cmp byte [vo_dir], 0
    jne .put
    mov si, di                      ; KEEP: the screen -> the save
    mov di, bp
    push ds
    pop es
    push ds
    mov ds, [vp_vseg]
    rep movsb
    pop ds
    mov bp, di
    jmp short .n
.put:
    mov si, bp                      ; PUT BACK: the save -> the screen
    mov es, [vp_vseg]
    rep movsb
    mov bp, si
.n:
    pop cx
    inc ax
    cmp ax, VO_ROWS
    jb .r
    inc cl
    cmp cl, [vo_pl]
    jb .pl
    cmp byte [vo_mx], 0
    je .ret
    call vp_mxall
.ret:
    ret

; vo_row - AX = a row of the box -> vo_bits: its one-bit image, a byte of
; ground each side of the characters. Preserves all
vo_row:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov di, vo_bits
    mov byte [di], 0
    inc di
    mov cl, [vo_n]
    xor ch, ch
    sub ax, 2                       ; the glyph's row, if this is one
    cmp ax, 8
    jae .blank
    jcxz .e
    mov es, [vo_gseg]
    mov si, vo_text
.c:
    mov bl, [si]
    inc si
    sub bl, [vo_gfst]
    jb .z
    mov bh, [vo_glst]
    sub bh, [vo_gfst]
    cmp bl, bh
    ja .z
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, ax
    add bx, [vo_goff]
    mov bl, [es:bx]
    jmp short .s
.z:
    xor bl, bl
.s:
    mov [di], bl
    inc di
    loop .c
    jmp short .e
.blank:
    jcxz .e
.bl:
    mov byte [di], 0
    inc di
    loop .bl
.e:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; vo_bit - BX = a pixel of vo_bits -> CF = lit. Preserves all but flags
vo_bit:
    push ax
    push bx
    push cx
    mov cl, bl
    and cl, 7
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov al, [vo_bits+bx]
    shl al, cl
    shl al, 1
    pop cx
    pop bx
    pop ax
    ret

; vo_draw - BL = a page index: the box drawn on it, in this screen's pixels.
; Clobbers all
vo_draw:
    call vo_page
    mov al, [vo_n]
    mov [vo_dn], al
    add al, 2
    xor ah, ah
    mov [vo_nb], ax                 ; the one-bit row's bytes
    mov es, [vp_vseg]
    cld
    cmp byte [vo_mode], VOM_X
    je .mx
    cmp byte [vo_mx], 0             ; mode 12h: every plane
    je .rows
    call vp_mxall
.rows:
    xor ax, ax
.r:
    call vo_row
    call vo_addr
    add di, [vo_pgo]
    push ax
    mov si, vo_bits
    mov cx, [vo_nb]
    mov bl, [vo_mode]
    cmp bl, VOM_1
    jne .m2
    rep movsb                       ; ONE BIT: the glyph's bytes
    jmp short .rn
.m2:
    cmp bl, VOM_2
    jne .m8
.b2:                                ; TWO BITS: each bit twice, colour 3
    lodsb
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov ah, [vo_x2+bx]
    mov bl, al
    and bl, 15
    mov al, [vo_x2+bx]
    xchg al, ah
    stosw
    loop .b2
    jmp short .rn
.m8:
    cmp bl, VOM_8
    jne .mt
.b8:                                ; A BYTE: each bit a pixel
    lodsb
    mov ah, al
    mov bl, 8
.b8p:
    xor al, al
    shl ah, 1
    jnc .b8s
    mov al, VO_INK
.b8s:
    stosb
    dec bl
    jnz .b8p
    loop .b8
    jmp short .rn
.mt:                                ; C160: two pixels a cell, the LEFT the
    shl cx, 1                       ; attribute's high nibble (98.1.3.3)
    shl cx, 1                       ; (a character is four cells)
    xor bx, bx
.bt:
    xor al, al
    call vo_bit
    jnc .tl
    mov al, 0xF0
.tl:
    inc bx
    call vo_bit
    jnc .tr
    or al, 0x0F
.tr:
    inc bx
    mov [es:di], al
    add di, 2
    loop .bt
.rn:
    pop ax
    inc ax
    cmp ax, VO_ROWS
    jb .r
    ret
.mx:                                ; MODE X: a plane at a time, a byte a
    xor cx, cx                      ; pixel, pixel x = 4 x byte + plane
.xp:
    call vp_mxsel
    xor ax, ax
.xr:
    call vo_row
    call vo_addr
    add di, [vo_pgo]
    push ax
    push cx
    mov bx, cx                      ; BX = the pixel
    xor ch, ch
    mov cl, byte [vo_nb]
    shl cx, 1                       ; two plane bytes a character
.xb:
    xor al, al
    call vo_bit
    jnc .xs
    mov al, VO_INK
.xs:
    stosb
    add bx, 4
    loop .xb
    pop cx
    pop ax
    inc ax
    cmp ax, VO_ROWS
    jb .xr
    inc cl
    cmp cl, 4
    jb .xp
    jmp vp_mxall

; =============================================================================
; DECODING ROUND THE BOX (SPEC.md 98.3.13.1). The flicker the owner saw was
; the box put back for every decode and drawn again after it: off the glass
; for as long as a frame takes. vd_clip is vd_native's lists walked round it
; instead. The box is VO_ROWS byte RANGES, a row each, in address order, and
; a page table (vc_pg: a 256-byte page -> the first range ending past it)
; finds the one at or after an address in a lookup. A skip-coded segment's
; addresses only rise, so a poke list holds the next range's start in BX and
; a change short of it is ONE COMPARE more than vd_native's; one that reaches
; it is cut at the range's edges - the box's bytes into the SAVE, every plane
; the write's mask names, the rest onto the screen. It is used only while
; the box is on the glass and the screen is the reference (not through the
; shadow, not flipping), so a play with no text pays one compare a record.
; DS is the record's; everything here is CS-relative
; =============================================================================
; vc_setup - the box just drawn on page 0: its ranges and the page table,
; and [vo_clip] on. Nothing through the shadow or when flipping. Clobbers all
vc_setup:
    mov byte [vo_clip], 0
    cmp byte [vp_shadow], 0
    jne .ret
    cmp byte [vp_flip], 0
    jne .ret
    mov al, [vo_dn]                 ; the box's bytes a row, as it is drawn
    add al, 2
    mul byte [vo_bpc]
    mov [vc_w], ax
    mov cx, ax
    mov ax, VO_ROWS                 ; ...and a plane of the save
    mul cx
    mov [vc_pls], ax
    xor si, si                      ; SI = the row, x 2
    mov dx, vo_sav                  ; DX = its row of the save
.r:
    mov ax, si
    shr ax, 1
    add ax, VO_YOFF
    mov bl, [vp_tlay]
    call vp_rowaddr
    add ax, [vp_org]
    add ax, [vo_xoff]               ; AX = its first byte in the box
    mov di, si                      ; in address order: the banks
.ins:                               ; interleave the rows
    or di, di
    jz .put
    cmp ax, [vc_rs+di-2]
    jae .put
    mov bx, [vc_rs+di-2]
    mov [vc_rs+di], bx
    mov bx, [vc_rv+di-2]
    mov [vc_rv+di], bx
    sub di, 2
    jmp short .ins
.put:
    mov [vc_rs+di], ax
    mov [vc_rv+di], dx
    add dx, [vc_w]
    add si, 2
    cmp si, VO_ROWS * 2
    jb .r
    xor si, si                      ; each range's end
.e:
    mov ax, [vc_rs+si]
    add ax, [vc_w]
    mov [vc_re+si], ax
    add si, 2
    cmp si, VO_ROWS * 2
    jb .e
    xor bx, bx                      ; the page table: each page, the first
    xor di, di                      ; range ending past its first byte
    xor dx, dx
.pg:
    cmp bx, VO_ROWS * 2
    jae .pp
    cmp [vc_re+bx], dx
    ja .pp
    add bx, 2
    jmp short .pg
.pp:
    mov [vc_pg+di], bl
    add dx, 256
    inc di
    cmp di, 256
    jb .pg
    mov byte [vo_clip], 1
.ret:
    ret

; VC_POKE n - the list of n-byte changes, round the box: BX the next range's
; start less n-1, so the compare is the whole test while the addresses rise
%macro VC_POKE 1
.s%1:
    lodsb
    xor ah, ah
    or al, al
    jz .z%1
    js .a%1
    mov dx, ax                      ; SKIP-CODED: the count, the address,
    lodsw                           ; then a skip before each change
    add ax, bp
    mov di, ax
    mov cx, %1 - 1
    call vc_bound
    xor ax, ax
.e%1:
    lodsb
    add di, ax
    cmp di, bx
    jae .h%1
    VD_ST%1
    dec dx
    jnz .e%1
    jmp short .s%1
.h%1:
    mov cx, %1                      ; it reaches a range: cut there
    call vc_span
    mov cx, %1 - 1
    call vc_bnext
    xor ax, ax
    dec dx
    jnz .e%1
    jmp short .s%1
.a%1:
    and al, 0x7F                    ; ABSOLUTE: an address before each
    jz .s%1                         ; (80h, a count of 0: no segment)
    mov dx, ax
.b%1:
    lodsw
    add ax, bp
    mov di, ax
    mov cx, %1
    call vc_put
    dec dx
    jnz .b%1
    jmp short .s%1
.z%1:
%endmacro

; vd_clip - vd_native's contract (vdec.inc): DS:SI the ten lists, ES the
; screen, BP the origin; [vc_mask] the planes the stores reach. SI past
; them. Clobbers AX, BX, CX, DX, DI, flags
vd_clip:
    cld
    mov byte [cs:vc_fillf], 0
    VC_POKE 1
    VC_POKE 2
    VC_POKE 3
    VC_POKE 4
    VC_POKE 5
    VC_POKE 6
    mov byte [cs:vc_lst], 6         ; the spans: SLICE, RUN, SLICEL, RUNL
.sl:
    lodsb
    xor ah, ah
    or al, al
    jz .sn
    js .sa
    mov dx, ax
    lodsw
    add ax, bp
    mov di, ax
    xor cx, cx                      ; the next range's start, while the
    call vc_bound                   ; addresses rise
    mov [cs:vc_nb], bx
    mov byte [cs:vc_sk], 1
.se:
    lodsb
    xor ah, ah
    add di, ax
    call vc_one
    dec dx
    jnz .se
    jmp short .sl
.sa:
    and al, 0x7F
    jz .sl                          ; (80h, a count of 0: no segment)
    mov dx, ax
    mov byte [cs:vc_sk], 0
.sb:
    lodsw
    add ax, bp
    mov di, ax
    call vc_one
    dec dx
    jnz .sb
    jmp short .sl
.sn:
    inc byte [cs:vc_lst]
    cmp byte [cs:vc_lst], 10
    jb .sl
    ret

; vc_one - list [vc_lst]'s span at DI, its length and bytes (or a run's
; value) off DS:SI, stored round the box. Clobbers AX, BX, CX
vc_one:
    mov al, [cs:vc_lst]
    cmp al, 7
    jb .slice
    je .run
    cmp al, 8
    je .slicel
    lodsw                           ; RUNL: len16, the value
    mov cx, ax
    lodsb
    jmp short .fill
.slice:
    lodsb
    xor ah, ah
    mov cx, ax
    jmp short vc_st
.slicel:
    lodsw
    mov cx, ax
    jmp short vc_st
.run:
    lodsw                           ; AL = the length, AH = the value
    mov cl, al
    xor ch, ch
    mov al, ah
.fill:
    mov byte [cs:vc_fillf], 1
    mov [cs:vc_val], al
    call vc_st
    mov byte [cs:vc_fillf], 0
    ret

; vc_st - vc_put, but in a skip-coded segment [vc_nb] is the next range's
; start and a span short of it is stored with no lookup. Clobbers AX, BX, CX
vc_st:
    cmp byte [cs:vc_sk], 0
    je vc_put
    jcxz .r
    mov ax, di
    add ax, cx
    cmp ax, [cs:vc_nb]
    ja .hit
    cmp byte [cs:vc_fillf], 0
    jne .f
    rep movsb
.r:
    ret
.f:
    mov al, [cs:vc_val]
    rep stosb
    ret
.hit:
    call vc_span                    ; (CX = 0 after it)
    call vc_bnext
    mov [cs:vc_nb], bx
    ret

; vc_put - CX bytes at DI (DS:SI's, or [vc_val] when [vc_fillf]) round the
; box: the one rep store when they reach no range. Clobbers AX, BX, CX
vc_put:
    jcxz .r
    call vc_find
    cmp bx, VO_ROWS * 2
    jae .all
    mov ax, di
    add ax, cx
    cmp ax, [cs:vc_rs+bx]
    jbe .all
    jmp short vc_span
.all:
    cmp byte [cs:vc_fillf], 0
    jne .f
    rep movsb
.r:
    ret
.f:
    mov al, [cs:vc_val]
    rep stosb
    ret

; vc_span - CX bytes at DI that reach range [vc_ri]: cut at its edges, the
; box's into the save and the rest onto the screen, and on through the ones
; after it. Clobbers AX, BX, CX
vc_span:
    push dx
.lp:
    jcxz .out
    mov bx, [cs:vc_ri]
    call vc_adv                     ; (past any it has left behind)
    cmp bx, VO_ROWS * 2
    jae .all
    mov ax, [cs:vc_rs+bx]
    sub ax, di
    jbe .in
    call vc_scr                     ; short of it: onto the screen
    jmp short .lp
.in:
    mov ax, [cs:vc_re+bx]           ; in it: to its end, into the save
    sub ax, di
    mov dx, di
    sub dx, [cs:vc_rs+bx]
    add dx, [cs:vc_rv+bx]
    call vc_sav
    jmp short .lp
.all:
    mov ax, cx
    call vc_scr
.out:
    pop dx
    ret

; vc_find - BX and [vc_ri] = the first range ending past DI, x 2 (VO_ROWS x
; 2: none). vc_adv is the same from range BX on. Preserves the rest
vc_find:
    mov bx, di
    mov bl, bh
    xor bh, bh
    mov bl, [cs:vc_pg+bx]
vc_adv:
    cmp bx, VO_ROWS * 2
    jae .x
    cmp [cs:vc_re+bx], di
    ja .x
    add bx, 2
    jmp short vc_adv
.x:
    mov [cs:vc_ri], bx
    ret

; vc_bound - CX = n-1: BX = the next range's start at or after DI, less CX
; (0 at the least), or FFFFh past the last. vc_bnext from [vc_ri] on.
; Preserves the rest
vc_bound:
    call vc_find
    jmp short vc_bx
vc_bnext:
    mov bx, [cs:vc_ri]
    call vc_adv
vc_bx:
    cmp bx, VO_ROWS * 2
    jae .none
    mov bx, [cs:vc_rs+bx]
    sub bx, cx
    jnc .r
    xor bx, bx
.r:
    ret
.none:
    mov bx, 0xFFFF
    ret

; vc_scr - up to AX of the span's CX bytes onto the screen at ES:DI; AX =
; how many, CX less them. Preserves BX, DX
vc_scr:
    cmp ax, cx
    jbe .n
    mov ax, cx
.n:
    sub cx, ax
    push cx
    mov cx, ax
    cmp byte [cs:vc_fillf], 0
    jne .f
    rep movsb
    pop cx
    ret
.f:
    push ax
    mov al, [cs:vc_val]
    rep stosb
    pop ax
    pop cx
    ret

; vc_sav - up to AX of the span's CX bytes into the save at DX - every plane
; [vc_mask] names - and DI past them on the screen; CX less them. Clobbers
; AX, DX
vc_sav:
    cmp ax, cx
    jbe .n
    mov ax, cx
.n:
    sub cx, ax
    mov [cs:vc_n], ax
    push cx
    push es
    push bx
    mov bx, dx
    add di, ax
    push cs
    pop es
    mov dl, [cs:vc_mask]
    mov dh, [cs:vo_pl]
.pl:
    shr dl, 1
    jnc .np
    push di
    push si
    mov di, bx
    mov cx, [cs:vc_n]
    cmp byte [cs:vc_fillf], 0
    jne .f
    rep movsb
    jmp short .d
.f:
    mov al, [cs:vc_val]
    rep stosb
.d:
    pop si
    pop di
.np:
    add bx, [cs:vc_pls]
    dec dh
    jnz .pl
    pop bx
    pop es
    cmp byte [cs:vc_fillf], 0
    jne .x
    add si, [cs:vc_n]
.x:
    pop cx
    ret

; -----------------------------------------------------------------------------
; the text's parts
; -----------------------------------------------------------------------------
; vo_time - AX = a frame -> [DI] its time, m:ss, and a NUL; DI on the NUL.
; Clobbers AX, BX, CX, DX
vo_time:
    mul word [vp_spf]               ; frames x samples...
    cmp dx, [vp_rate]
    jb .d
    mov ax, 0xFFFF
    jmp short .s
.d:
    div word [vp_rate]              ; ...over the rate: seconds
.s:
    xor dx, dx
    mov cx, 60
    div cx
    push dx
    mov bx, vo_nbuf + 6             ; the minutes, backwards into a buffer
    mov byte [bx], 0
    mov cx, 10
.l:
    xor dx, dx
    div cx
    add dl, '0'
    dec bx
    mov [bx], dl
    or ax, ax
    jnz .l
.c:
    mov al, [bx]
    or al, al
    jz .m
    mov [di], al
    inc di
    inc bx
    jmp short .c
.m:
    mov byte [di], ':'
    inc di
    pop ax
    aam                             ; AH tens, AL ones
    add ax, '00'
    mov [di], ah
    mov [di+1], al
    add di, 2
    mov byte [di], 0
    ret

; the renderers by FSXM id: renderer, bytes a character (a plane's), the
; box's left in those bytes
vo_mtab:
    db VOM_T, 4, 4                  ; FSXM_TEXT80: C160, through the shadow
    db 0, 0, 0                      ; FSXM_TEXT40
    db VOM_2, 2, 2                  ; FSXM_CGA320
    db VOM_1, 1, 1                  ; FSXM_CGA640
    db VOM_1, 1, 1                  ; FSXM_HERC
    db 0, 0, 0                      ; FSXM_VGA0D
    db VOM_8, 8, 8                  ; FSXM_VGA13
    db VOM_1, 1, 1                  ; FSXM_VGA12
    db VOM_X, 2, 2                  ; FSXM_MODEX
vo_x2:        db 0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F
              db 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
vo_s_none:    db 0
vo_s_pause:   db 'Paused', 0
vo_s_ron:     db 'Repeat on', 0
vo_s_roff:    db 'Repeat off', 0
vo_s_nokey:   db 'No seeking', 0
vo_mode:      db 0                  ; the renderer, 0 none
vo_bpc:       db 0
vo_xoff:      dw 0
vo_cmax:      db 0                  ; the characters the canvas holds
vo_pl:        db 0                  ; the planes a save takes
vo_mx:        db 0                  ; ...and whether they are the VGA's ports
vo_gseg:      dw 0                  ; the kernel's glyphs
vo_goff:      dw 0
vo_gfst:      db 0
vo_glst:      db 0
vo_kind:      db 0                  ; what is shown
vo_on:        db 0                  ; ...and whether it is to be on the glass
vo_chg:       db 0                  ; ...changed since it was drawn
vo_drawn:     db 0                  ; the pages it is on (bits 0, 1)
vo_n:         db 0                  ; its characters
vo_dn:        db 0                  ; ...as drawn, for the put-back
vo_text:      times VO_MAXC + 1 db 0
vo_bits:      times VO_MAXC + 2 db 0
vo_nbuf:      times 7 db 0
vo_nb:        dw 0
vo_w:         dw 0
vo_pgo:       dw 0
vo_dir:       db 0
vo_pz:        db 0                  ; Space paused the play here
vo_toast:     db 0                  ; a toast's kind, while it lasts...
vo_ttk:       dw 0                  ; ...since this tick
vo_skp:       db 0                  ; a seek is on its way (vp_skkey)
vo_clip:      db 0                  ; the hook decodes round the box (vd_clip)
vc_mask:      db 1                  ; vd_clip: the planes a store reaches
vc_w:         dw 0                  ; ...the box's bytes a row
vc_pls:       dw 0                  ; ...and a plane of the save
vc_rs:        times VO_ROWS dw 0    ; ...its rows' ranges, in address order:
vc_re:        times VO_ROWS dw 0    ; first byte, past the last, and the
vc_rv:        times VO_ROWS dw 0    ; save's row
vc_ri:        dw 0                  ; ...the range at or after a store, x 2
vc_lst:       db 0                  ; ...the span list walked
vc_sk:        db 0                  ; ...skip-coded: [vc_nb] holds
vc_nb:        dw 0                  ; ...the next range's start
vc_n:         dw 0                  ; vc_sav: the piece
vc_fillf:     db 0                  ; ...a run, of
vc_val:       db 0
vb_n1:        dw 0                  ; vo_bparts: a shadow row's three parts
vb_n2:        dw 0
vb_n3:        dw 0
