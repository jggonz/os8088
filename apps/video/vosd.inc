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
;   C512       the same, each cell 0DEh and its attribute, written whole
;   TEXT       the text screen as it is: the characters, one row of them
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
VO_MAXC     equ 18                  ; the longest line: "Buffering 37% ~24s"
VO_SAV      equ (VO_MAXC + 2) * 8 * VO_ROWS ; a page's save at the widest: a
                                    ; character and its ground at 8 bytes
VO_TOASTT   equ 27                  ; a toast's ticks: 1.5 s
VO_INK      equ 255                 ; 256 colours' brightest (the palette's
                                    ; order, SPEC.md 98.2.3) - and an `sbb`
                                    ; of a lit bit's carry (vo_draw)
VOK_PAUSE   equ 1                   ; what the box says
VOK_SEEK    equ 2
VOK_REPON   equ 3
VOK_REPOFF  equ 4
VOK_NOKEY   equ 5
VOK_SNDOFF  equ 6                   ; M, S: muted (98.3.17)
VOK_LOWMEM  equ 7                   ; the ring short of the stream's
VOK_SNDON   equ 8                   ; ...and on again
VOK_SNDNX   equ 9                   ; ...from the next play (no key to seek)
VOK_BUF     equ 10                  ; the bank's prefill (98.3.18.3)
VOK_BIGKEY  equ 11                  ; no seek: keys past one read here
VOM_1       equ 1                   ; the renderers
VOM_2       equ 2
VOM_8       equ 3
VOM_X       equ 4
VOM_T       equ 5
VOM_C       equ 6                   ; C512 (98.3.12.2): the same text mode,
                                    ; its cells written whole
VOM_A       equ 7                   ; TEXT (98.3.16): the characters THEMSELVES,
VO_TATTR    equ 0x70                ; a cell each, on one row, black on grey -
                                    ; which an MDA draws as a colour card does

; vo_setup - a bracket's surface is up: the renderer its screen takes, and
; how many characters the canvas fits. None in the window (98.3.7), whose
; controls say what the keys did. Clobbers AX, BX, CX, DX, SI
vo_setup:
    xor ax, ax
    mov [vo_mode], ax               ; (and vo_drawn: the ten bytes in front
    mov [vo_on], ax                 ; of vo_rows, five words - vo_on, vo_chg,
    mov [vo_kind], ax               ; vo_kind, vo_pz, vo_toast, vo_skp,
    mov [vo_toast], ax              ; vo_clip, vo_dir)
    mov [vo_clip], ax
    cmp byte [vp_winm], 0
    je .g
.r0:
    ret                             ; (a ret in a short jump's reach: the
.g:                                 ; exits below were jumps over a jump)
    push es
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL..AH, CX bytes a glyph
    pop es
    cmp cx, 8
    jne .r0
    mov [vo_gseg], dx
    mov [vo_goff], si
    mov [vo_gfst], ax               ; (and vo_glst)
    mov bl, [vp_fsmode]             ; this screen's renderer and the bytes a
    cmp bl, FSXM_MODEX              ; character takes (a plane's) - which is
    ja .r0                          ; the box's left, in the same bytes
    xor bh, bh
    shl bx, 1
    mov bx, [vo_mtab+bx]            ; BL = the renderer, BH = the bytes
    or bl, bl
    jz .r0
    mov cl, bh
    mov ax, VO_ROWS                 ; the box's rows of the canvas, and how
    mov dx, VO_YOFF                 ; far down it is
    cmp byte [vp_pixfmt], PF_C512   ; C512 shares C160's mode, and the
    jne .nc5                        ; screen is its canvas: a character is
    mov cl, 8                       ; four 0DEh CELLS, eight bytes
    mov bl, VOM_C
.nc5:
    cmp byte [vp_pixfmt], PF_TEXT   ; TEXT: a character is a CELL, two
    jne .ntx                        ; bytes, the box one row of them a row
    mov cl, 2                       ; down and a cell in
    mov bl, VOM_A
    mov al, 1
    mov dx, ax
.ntx:
    mov [vo_rows], ax
    mov [vo_yoff], dx
    shl ax, 1
    mov [vo_rows2], ax
    mov [vo_bpc], cl
    xor ch, ch
    mov [vo_xoff], cx
    mov ax, [vp_wb]                 ; the characters the canvas holds
    sub ax, cx
    jbe .out
    xor dx, dx
    div cx
    sub ax, 2
    jbe .out
    cmp ax, VO_MAXC
    jbe .cm
    mov ax, VO_MAXC
.cm:
    mov [vo_cmax], al
    mov ax, [vo_yoff]
    add ax, [vo_rows]
    cmp [vp_h], ax
    jb .out
    mov cx, 1                       ; the planes a save takes: Mode X's four,
    cmp bl, VOM_X                   ; VGA4's four, else one - and whether
    jne .p1                         ; they are the VGA's ports (vo_mx)
    mov cx, 0x0104
    jmp short .pl
.p1:
    cmp byte [vp_fsmode], FSXM_VGA12
    je .p12
    cmp byte [vp_fsmode], FSXM_VGA0D    ; (0Dh: a VGA4 screen of its own)
    jne .pl
.p12:
    mov ch, 1                       ; mode 12h: the planes are the ports'
    cmp byte [vp_planar], 2
    jne .pl
    mov cl, 4
.pl:
    mov [vo_pl], cx                 ; (and vo_mx)
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
    pop ax                          ; (on into vo_update, which follows)

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
    cbw                             ; (a kind is below 80h: AH = 0)
    xchg ax, si
    mov al, [vo_stab+si]            ; its text, a byte from vo_s_none
    cbw                             ; (all of them inside 128 bytes)
    add ax, vo_s_none
    xchg ax, si                     ; SI = the text, AX = the kind
    call vo_show
    pop si
    pop ax
.ret:
    ret

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
    cmp byte [vp_fpg], 0            ; (flipped and built ahead: the page is
    jne .ret                        ; the record's, vp_fpdec's to clear)
    cmp byte [vp_shadow], 0         ; through the shadow the copy restores
    jne .ret
    mov ax, [vp_poff]               ; the page the next record goes into
    call vo_savbit
    test [vo_drawn], bl
    jz .ret
    xor [vo_drawn], bl              ; (the bit, which is set: cleared)
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
    mov ax, [vp_foff]               ; ...unless it is A NEW TEXT AS WIDE as
    call vo_savbit                     ; the one up, on the page on the glass
                                    ; and only there: drawn straight over
                                    ; it, ground and glyph in one pass - put
                                    ; back first, the box was off the glass
    cmp [vo_drawn], bl              ; between, which a prefill's line
    jne .un                         ; changing every chunk made a flicker
    mov al, [vo_n]                  ; (98.3.18.3) - on a FLIPPED file too,
    cmp al, [vo_dn]                 ; page-ahead's among them: one page
    jne .un                         ; holds the box while nothing decodes
    mov byte [vo_chg], 0
    shr bl, 1
    jmp vo_draw
.un:
    call vo_unall
    mov byte [vo_chg], 0
    cmp byte [vo_on], 0
    je .ret
.dr:
    mov ax, [vp_foff]               ; ...then onto the page on the glass
    call vo_savbit
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
    push word [vp_dy0]              ; ...and ONLY the box's rows: vo_show is
    push word [vp_dy1]              ; at IF=0, and the rest of the band is the
    mov ax, [vo_yoff]               ; hook's next copy, which goes round the
    mov [vp_dy0], ax                ; box if it is up again by then
    add ax, [vo_rows]
    mov [vp_dy1], ax
    call vp_blit
    pop word [vp_dy1]
    pop word [vp_dy0]
.sc:
    mov byte [vo_chg], 0
    cmp byte [vo_on], 0
    je .ret
.sd:
    cmp byte [vo_drawn], 0          ; drawn, and no copy has crossed it
    jne .ret
    call vp_c16sync                 ; (C160 decoded onto the screen: the
    mov byte [vo_drawn], 1          ; shadow the picture again, 98.3.12.1)
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
    mov ax, [vp_poff]
    call vo_savbit
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

; vo_lfix - FLIPPING, after vp_lcopy (98.3.8.1): the rows it copied off
; the glass carry the box when the page on the glass has it, and the page
; drawn next must hold PICTURE there, or vo_flipon saves the text as picture
; and vo_unall bakes it into the frame when the toast ends. The glass page's
; own save is that picture, as of the frame on the glass - which is the frame
; the copy brings the other page to - so it goes onto the other page's box.
; Nothing when the box is not on the glass or the copied rows miss it.
; Clobbers all
vo_lfix:
    cmp byte [vo_mode], 0
    je .ret
    mov ax, [vp_foff]               ; the page on the glass
    call vo_savbit
    test [vo_drawn], bl
    jz .ret
    mov ax, [vo_yoff]               ; the box's rows against the copy's,
    cmp ax, [vp_pry1]               ; both of the canvas
    jae .ret
    add ax, [vo_rows]
    cmp ax, [vp_pry0]
    jbe .ret
    mov bh, bl                      ; BH = the glass page's save...
    shr bh, 1                       ; (bit -> page index)
    mov bl, bh                      ; ...BL = the page drawn next
    xor bl, 1
    mov al, 1
    jmp vo_iox
.ret:
    ret

; vo_cfix - after a COPY (98.3.8.5): AX = its source. When that page has the
; box drawn and the copied rows cross it, the copy carried the TEXT onto
; [vp_poff], which must hold picture - and the source's save is the picture
; under its box, so it goes there. vo_lfix's case for any two pages.
; Clobbers all
vo_cfix:
    cmp byte [vo_mode], 0
    je .ret
    call vo_savbit
    test [vo_drawn], bl
    jz .ret
    mov ax, [vo_yoff]
    cmp ax, [vp_pry1]
    jae .ret
    add ax, [vo_rows]
    cmp ax, [vp_pry0]
    jbe .ret
    mov bh, bl
    shr bh, 1                       ; (bit -> the save)
    mov ax, [vp_poff]
    mov [vo_pgo], ax
    mov al, 1
    jmp vo_iop
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
    sub ax, [vo_yoff]
    jb .out
    cmp ax, [vo_rows]
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

; vo_page - BL = a page index -> [vo_pgo] its offset (the second page at
; [vp_page]: Mode X's VP_PAGE, or a screen's rows x 80, 98.3.8.2)
vo_page:
    push ax                         ; (THE SCREEN'S page: Mode X's 19,200,
    mov ax, [vo_sl0]                ; 640 x 400's 32,000, a Hercules's
    or bl, bl                       ; 8000h - VP_PAGE put a 640 x 400 box
    jz .s                           ; for page 1 into page 0's row 240 -
    mov ax, [vo_sl1]                ; or ON N PAGES whichever page each of
.s:                                 ; the two saves is on, vo_pgsave)
    mov [vo_pgo], ax
    pop ax
    ret

; vo_savbit - AX = a page's offset -> BL = the bit in vo_drawn of the save
; on that page, 1 or 2, or 0 when neither of the two is (98.3.8.4).
; Preserves all but BL and the flags
vo_savbit:
    mov bl, 1
    cmp ax, [vo_sl0]
    je .r
    inc bl
    cmp ax, [vo_sl1]
    je .r
    mov bl, 0
.r:
    ret

; vo_pgsave - ON N PAGES (98.3.8.4): AX = a page about to be shown gets one of
; the text's two saves. The box is only ever on the page shown and the one
; just left, so when neither save is on AX's page the one NOT on the glass
; gives its page up - its box put back there first - and moves to AX's.
; Preserves all
vo_pgsave:
    cmp ax, [vo_sl0]
    je .r
    cmp ax, [vo_sl1]
    je .r
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ax
    xor bl, bl                      ; BL = the save off the glass
    mov ax, [vp_foff]
    cmp ax, [vo_sl0]
    jne .s
    inc bl
.s:
    mov bh, 1
    mov cl, bl
    shl bh, cl                      ; BH = its bit
    test [vo_drawn], bh
    jz .m
    xor [vo_drawn], bh              ; (set: cleared. vo_io takes BL alone)
    push bx
    mov al, 1
    call vo_io                      ; (its box put back, on its old page)
    pop bx
.m:
    pop ax
    or bl, bl
    jnz .m1
    mov [vo_sl0], ax
    jmp short .x
.m1:
    mov [vo_sl1], ax
.x:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; vo_addr - AX = a row of the box -> DI = its first byte on the screen, the
; page left out. Preserves all but DI
vo_addr:
    push ax
    push bx
    add ax, [vp_ty0]
    add ax, [vo_yoff]
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
; vo_iox - the same, with BH naming the page whose SAVE is used and BL the
; page written or read (vo_lfix's: one page's save onto the other)
vo_io:
    mov bh, bl
vo_iox:
    call vo_page
vo_iop:                             ; (...or [vo_pgo] the page, vo_cfix's)
    mov [vo_dir], al
    mov dx, ds                      ; DX = ours, for the rows' DS, and BX:BP
    mov bp, vo_sav                  ; = the save: page 0's in our bss, page
    or bh, bh                       ; 1's its claim's (98.3.13.2)
    mov bx, dx
    jz .b
    xor bp, bp
    mov bx, [vo_s1seg]
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
    mov es, bx
    mov ds, [vp_vseg]
    rep movsb
    mov ds, dx
    mov bp, di
    jmp short .n
.put:
    mov si, bp                      ; PUT BACK: the save -> the screen
    mov es, [vp_vseg]
    mov ds, bx
    rep movsb
    mov ds, dx
    mov bp, si
.n:
    pop cx
    inc ax
    cmp ax, [vo_rows]
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
    push dx                         ; (a glyph row's alone)
    mov dx, [vo_gfst]               ; DL = the first glyph, DH = how many
    sub dh, dl                      ; past it (outside the character loop)
    add ax, [vo_goff]               ; AX = the row in the glyphs
.c:
    mov bl, [si]
    inc si
    sub bl, dl
    jb .z
    cmp bl, dh
    ja .z
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, ax
    mov bl, [es:bx]
    jmp short .s
.z:
    xor bl, bl
.s:
    mov [di], bl
    inc di
    loop .c
    pop dx
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
    cmp byte [vo_mode], VOM_X       ; (Mode X's own loop first: in a short
    jne .nx                         ; jump's reach of the test)
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
    mov cx, [vo_nb]                 ; (under 256)
    shl cx, 1                       ; two plane bytes a character
.xb:
    call vo_bit
    sbb al, al                      ; lit: VO_INK, FFh (0 not)
    stosb
    add bx, 4
    loop .xb
    pop cx
    pop ax
    inc ax
    cmp ax, [vo_rows]
    jb .xr
    inc cl
    cmp cl, 4
    jb .xp
    jmp vp_mxall
.nx:
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
    cmp byte [vo_mx], 0             ; ONE BIT: the glyph's bytes...
    jne .b1i
    rep movsb
    jmp .rn
.b1i:                               ; ...on SIXTEEN, onto the planes of the
    push cx                         ; ink (the palette's brightest, vp_inkset)
    push di                         ; and 0 onto the rest, so ground is
    mov dx, 0x3C4                   ; colour 0, black in every scene
    mov ah, [vo_ink4]
    mov al, 2
    out dx, ax
    rep movsb
    pop di
    pop cx
    mov ah, [vo_ink4]
    not ah
    and ah, 0x0F
    jz .b1a
    mov al, 2
    out dx, ax
    xor al, al
    rep stosb
.b1a:
    mov ax, 0x0F02                  ; (every plane again, as vp_mxall left it)
    out dx, ax
    jmp .rn
.m2:
    cmp bl, VOM_2
    jne .m8
    mov bx, vo_x2
.b2:                                ; TWO BITS: each bit twice, colour 3
    lodsb
    mov ah, al
    and al, 15
    xlatb                           ; the low nibble's byte...
    xchg al, ah
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlatb                           ; ...and the high one's, first
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
    shl ah, 1
    sbb al, al                      ; lit: VO_INK, FFh (0 not)
    stosb
    dec bl
    jnz .b8p
    loop .b8
    jmp short .rn
.mt:
    cmp bl, VOM_A                   ; TEXT: the characters themselves, a
    jne .mtc                        ; cell of ground each side
    mov ax, VO_TATTR * 256 + ' '
    stosw
    mov si, vo_text
    mov cl, [vo_n]
    xor ch, ch
    jcxz .tae
.tal:
    lodsb
    stosw
    loop .tal
.tae:
    mov al, ' '
    stosw
    jmp short .rn
.mtc:                               ; C160: two pixels a cell, the LEFT the
    shl cx, 1                       ; attribute's high nibble (98.1.3.3)
    shl cx, 1                       ; (a character is four cells)
    xor bx, bx
.bt:
    call vo_bit
    sbb al, al
    and al, 0xF0
    inc bx
    call vo_bit
    jnc .tr
    or al, 0x0F
.tr:
    inc bx
    cmp byte [vo_mode], VOM_C       ; C512: the cell whole, 0DEh and its
    je .tc                          ; attribute (98.3.12.2)
    mov [es:di], al
    add di, 2
    loop .bt
    jmp short .rn
.tc:
    mov ah, al
    mov al, 0xDE
    stosw
    loop .bt
.rn:
    pop ax
    inc ax
    cmp ax, [vo_rows]
    jb .r
    ret

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
    je .g
.r0:
    ret
.g:
    cmp byte [vp_flip], 1           ; (FLIPPED, every frame onto the page
    je .r0                          ; not shown: the box put back there -
    cmp byte [vp_fpg], 0            ; built ahead or not)
    jne .r0
    cmp byte [vp_flip], 2           ; PAGE-AHEAD decodes on the page SHOWN:
    jne .pg0                        ; round the box there, as unflipped - its
    mov ax, [vp_foff]               ; save that page's (vp_pgdec sets it up
    call vo_savbit                     ; again at every flip)
    test [vo_drawn], bl
    jz .r0                          ; (not on that page: nothing to go round)
.pg0:
    mov al, [vo_dn]                 ; the box's bytes a row, as it is drawn
    add al, 2
    mul byte [vo_bpc]
    mov [vc_w], ax
    mov cx, ax
    mov ax, [vo_rows]               ; ...and a plane of the save
    mul cx
    mov [vc_pls], ax
    xor si, si                      ; SI = the row, x 2
    mov dx, vo_sav                  ; DX = its row of the save, [vc_sseg]
    mov bx, cs                      ; its segment (page 1's is the claim's,
    mov ax, [vp_foff]               ; vo_io's - the save on the page shown,
    cmp ax, [vo_sl0]                ; 98.3.8.4, 98.3.13.2)
    je .sv
    xor dx, dx
    mov bx, [vo_s1seg]
.sv:
    mov [vc_sseg], bx
.r:
    mov ax, si
    shr ax, 1
    add ax, [vo_yoff]
    mov bl, [vp_tlay]
    call vp_rowaddr
    add ax, [vp_org]
    add ax, [vp_foff]               ; (the page shown: 0 but page-ahead's)
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
    cmp si, [vo_rows2]
    jb .r
    xor si, si                      ; each range's end
.e:
    mov ax, [vc_rs+si]
    add ax, [vc_w]
    mov [vc_re+si], ax
    add si, 2
    cmp si, [vo_rows2]
    jb .e
    xor bx, bx                      ; the page table: each page, the first
    xor di, di                      ; range ending past its first byte
    xor dx, dx
.pg:
    cmp bx, [vo_rows2]
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
    shr word [cs:vd_map], 1         ; SUB2's list map (98.1.3.2.4): the
    jnc .z%1                        ; next list is in a short jump's reach
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
%if %1 == 1
    xor cx, cx                      ; (n - 1)
%else
    mov cx, %1 - 1
%endif
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
    call vc_span                    ; (CX = 0 after it)
%if %1 > 1
    mov cx, %1 - 1
%endif
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
    mov word [cs:vd_map], 0xFFFF    ; every list there
    jmp short vd_clgo
vd_clipm:                           ; ...or a SUB2 sub-record's: its list map
    lodsw                           ; first (98.1.3.2.4)
    mov [cs:vd_map], ax
vd_clgo:
    cld
    mov byte [cs:vc_fillf], 0
    VC_POKE 1
    VC_POKE 2
    VC_POKE 3
    VC_POKE 4
    VC_POKE 5
    VC_POKE 6
    mov byte [cs:vc_lst], 6         ; the spans: SLICE, RUN, SLICEL, RUNL
.sg:
    shr word [cs:vd_map], 1
    jnc .sn
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
    jb .sg
    ret

; vc_m3b - vd_walk's routine for a COLOUR sub-record round the box (SPEC.md
; 98.1.3.2.4): DI the byte's screen address, AL its mask, the GC in write
; mode 3. Outside the box the store is the screen's; inside it, the save's
; byte in each plane takes the mask's bits set where the colour [vp_m3c] has
; that plane's and cleared where not - which is what the card would have
; done to it. Clobbers AX, BX, CX, DX
vc_m3b:
    call vc_find
    cmp bx, [cs:vo_rows2]
    jae .scr
    mov dx, di                      ; in the box: its byte of the save
    sub dx, [cs:vc_rs+bx]           ; (short of the range: the screen's -
    jb .scr                         ; the subtraction is the compare)
    add dx, [cs:vc_rv+bx]
    mov bx, dx
    push es
    les dx, [cs:vc_pls]             ; DX a plane of the save, ES its segment
    mov ah, [cs:vp_m3c]             ; (vc_sseg, the word after: 98.3.13.2)
    mov cl, [cs:vo_pl]
    xor ch, ch
.p:
    shr ah, 1
    jnc .c
    or [es:bx], al                  ; (each path its own tail: no jump
    add bx, dx                      ; over the other's)
    loop .p
    pop es
    ret
.c:
    not al
    and [es:bx], al
    not al
    add bx, dx
    dec cx                          ; (a byte over LOOP's: what follows
    jnz .p                          ; keeps its parity)
    pop es
    ret
.scr:
    cmp byte [cs:vp_m3ok], 0        ; (no write mode 3: a plane at a time)
    je .pl
    and [es:di], al
    ret
.pl:
    jmp vp_m3pl

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
    cmp bx, [cs:vo_rows2]
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
    cmp bx, [cs:vo_rows2]
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
    cmp bx, [cs:vo_rows2]
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
    cmp bx, [cs:vo_rows2]
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
    mov es, [cs:vc_sseg]
    mov dx, [cs:vc_mask]            ; DL the planes it reaches, DH the
                                    ; save's (vo_pl, the byte after)
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

; the renderers by FSXM id: renderer, bytes a character (a plane's) - and
; the box's left, a character in
vo_mtab:
    db VOM_T, 4                     ; FSXM_TEXT80: C160, through the shadow
    db 0, 0                         ; FSXM_TEXT40
    db VOM_2, 2                     ; FSXM_CGA320
    db VOM_1, 1                     ; FSXM_CGA640
    db VOM_1, 1                     ; FSXM_HERC
    db VOM_1, 1                     ; FSXM_VGA0D: a VGA4 screen of its own
    db VOM_8, 8                     ; FSXM_VGA13
    db VOM_1, 1                     ; FSXM_VGA12
    db VOM_X, 2                     ; FSXM_MODEX
vo_x2:        db 0x00, 0x03, 0x0C, 0x0F, 0x30, 0x33, 0x3C, 0x3F
              db 0xC0, 0xC3, 0xCC, 0xCF, 0xF0, 0xF3, 0xFC, 0xFF
vo_s_pause:   db 'Paused'
vo_s_none:    db 0                  ; (an empty text: Paused's own NUL)
vo_s_ron:     db 'Repeat on', 0
vo_s_roff:    db 'Repeat off', 0
vo_s_soff:    db 'Sound off', 0
vo_s_son:     db 'Sound on', 0
vo_s_snx:     db 'Next play', 0
vo_s_nokey:   db 'Seek: no keyframes', 0
vo_s_bigkey:  db 'Seek: keys too big', 0
vo_s_lowmem:  db 'Low memory', 0    ; (VO_MAXC: eighteen at most)
vo_stab:      db 0                  ; vo_update's: each kind's text, its
              db vo_s_pause - vo_s_none ; offset from vo_s_none (SEEK and
              db 0                  ; BUF say their own, not through it)
              db vo_s_ron - vo_s_none
              db vo_s_roff - vo_s_none
              db vo_s_nokey - vo_s_none
              db vo_s_soff - vo_s_none
              db vo_s_lowmem - vo_s_none
              db vo_s_son - vo_s_none
              db vo_s_snx - vo_s_none
              db 0
              db vo_s_bigkey - vo_s_none
%if vo_s_lowmem - vo_s_none > 127 || VOK_PAUSE != 1 || VOK_SEEK != 2 || VOK_REPON != 3 || VOK_REPOFF != 4 || VOK_NOKEY != 5 || VOK_SNDOFF != 6 || VOK_LOWMEM != 7 || VOK_SNDON != 8 || VOK_SNDNX != 9 || VOK_BUF != 10 || VOK_BIGKEY != 11
%error "vo_stab: a kind out of its order, or a text past cbw's reach"
%endif
; (vo_setup zeroes the first ten as five words, and every word is at an
; even offset from vo_mode - which is put on an even address, and the pad
; that takes is paid again at the end, so what follows keeps its parity in
; every arm)
..@vo_dbeg:
    align 2, db 0
vo_mode:      db 0                  ; the renderer, 0 none
vo_drawn:     db 0                  ; the pages it is on (bits 0, 1)
vo_on:        db 0                  ; whether it is to be on the glass
vo_chg:       db 0                  ; ...changed since it was drawn
vo_kind:      db 0                  ; what is shown
vo_pz:        db 0                  ; Space paused the play here
vo_toast:     db 0                  ; a toast's kind, while it lasts...
vo_skp:       db 0                  ; a seek is on its way (vp_skkey)
vo_clip:      db 0                  ; the hook decodes round the box (vd_clip)
vo_dir:       db 0                  ; (vo_iop's: set before every use)
vo_rows:      dw VO_ROWS            ; the box's rows of the canvas (TEXT's 1),
vo_rows2:     dw VO_ROWS * 2        ; ...twice that,
vo_yoff:      dw VO_YOFF            ; ...and the row it starts on
vo_xoff:      dw 0
vo_gseg:      dw 0                  ; the kernel's glyphs
vo_goff:      dw 0
vo_nb:        dw 0
vo_w:         dw 0
vo_pgo:       dw 0
vo_sl0:       dw 0                  ; the page each of the two saves is on:
vo_sl1:       dw VP_PAGE            ; 0 and 1, or any two of N (vo_pgsave)
vo_ttk:       dw 0                  ; ...a toast's, since this tick
vo_s1seg:     dw 0                  ; the SECOND save's claim (98.3.13.2): a
                                    ; play on two pages or more's, 0 none
vc_w:         dw 0                  ; vd_clip: the box's bytes a row
vc_pls:       dw 0                  ; ...and a plane of the save, and the
vc_sseg:      dw 0                  ; save's segment - ours, or [vo_s1seg]
                                    ; (vc_m3b loads the two with one LES)
vc_rs:        times VO_ROWS dw 0    ; ...its rows' ranges, in address order:
vc_re:        times VO_ROWS dw 0    ; first byte, past the last, and the
vc_rv:        times VO_ROWS dw 0    ; save's row
vc_ri:        dw 0                  ; ...the range at or after a store, x 2
vc_nb:        dw 0                  ; ...the next range's start
vc_n:         dw 0                  ; vc_sav: the piece
vb_n1:        dw 0                  ; vo_bparts: a shadow row's three parts
vb_n2:        dw 0
vb_n3:        dw 0
vo_bpc:       db 0
vo_cmax:      db 0                  ; the characters the canvas holds
vc_lst:       db 0                  ; vd_clip: the span list walked (here,
                                    ; so what follows keeps its parity)...
vc_mask:      db 1                  ; ...and the planes a store reaches -
                                    ; which vc_sav reads with vo_pl, a word
vo_pl:        db 0                  ; the planes a save takes
vo_mx:        db 0                  ; ...and whether they are the VGA's ports
vo_ink4:      db 15                 ; sixteen colours' ink: the brightest
vo_gfst:      db 0                  ; (the kernel's first and last glyph,
vo_glst:      db 0                  ; one word)
vo_n:         db 0                  ; its characters
vo_dn:        db 0                  ; ...as drawn, for the put-back
vo_text:      times VO_MAXC + 1 db 0
vo_bits:      times VO_MAXC + 2 db 0
vo_nbuf:      times 7 db 0
vc_sk:        db 0                  ; ...skip-coded: [vc_nb] holds
vc_fillf:     db 0                  ; ...a run, of
vc_val:       db 0
    times vo_mode - ..@vo_dbeg db 0
