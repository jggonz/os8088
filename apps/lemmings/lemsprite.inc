; =============================================================================
; os8088 - apps/lemmings/lemsprite.inc    SPRITES, OBJECTS AND THE BACKGROUND
;                                         (SPEC.md 116.4.6, wave 3)
;
; %included by apps/lemmings/lemmings.asm AFTER lemblit.inc, whose helpers
; (lb_gc, lb_seq, lb_dbl, lb_rowoff8, the dirty spans, lb_rpix, lb_pnib) and
; state (lb_kind, lb_fb, lb_world, lb_shadow, lb_scroll, lb_t) it uses, and
; after lemmask.inc (lm_seg, lm_nib2, lem_rowoff). The cdecl contract in
; lemmask.inc's header applies word for word: arguments at [bp+4] up, AX the
; answer, AX/BX/CX/DX the caller's to lose, SI/DI/BP/DS/SS/ES preserved and DF
; left clear.
;
; WHAT IS ON THE GLASS AND WHAT IT IS MADE OF, per backend (SPEC.md 116.4.6):
;
;   VGA       T, the four world planes in RAM (terrain only, the level's own
;             colours, the bricks in colour 7), is the BACKGROUND. VRAM holds
;             T + the interactive objects + the sprites. A game frame builds
;             every rectangle it changes - each lemming's old box and new one,
;             each destruction, each object whose frame moved - in the
;             compose scratch (lem_r_cband): T, then the objects over it, then
;             EVERY lemming, then the pointer, and latch-copies each rectangle
;             on in one pass (write mode 1: one byte moves four planes). A
;             sprite is therefore never SAVED UNDER and never taken off the
;             glass: its old box is rebuilt from what the background IS.
;   CGA/Herc  the mask is the terrain; W (the world claim) is the mask by
;             colour CLASS plus the objects, and the SHADOW is a 320x200
;             window of W plus the sprites. A sprite is taken off by copying
;             its rectangle of W back into the shadow, so the shadow backends
;             need no save-under at all and the order does not matter; the
;             shadow is off the glass until the present, so nothing composed
;             there is ever seen half done.
;
; THE CURSOR IS THE ONE SPRITE WITH A SAVE-UNDER ON EVERY CARD, because it is
; the one sprite that crosses onto the PANEL, and the panel's pixels are not in
; W or T: they are MAIN.DAT's bitmap plus the status line plus the counts plus
; the minimap, and rebuilding them under a pointer would be the whole panel's
; worth of drawing. On VGA a frame's compose brings its save-under up to date
; and draws it into the scratch (lem_r_csave, lem_r_cdraw), so a game frame
; never takes it off; the panel side takes it off only around a write under
; its own box (lemdraw.c ovl_pan_hit).
;
; ATTRIBUTION. Nothing here is derived from the original but lem_r_spray's
; sixteen particle colours and its -128 rule, which are Lemmix's (Eric
; Langedijk, under Lemmix's own licence): src/Game.pas ParticleColorIndices
; (:54-56) carries the same sixteen indices, and DrawParticles (:2492-2515)
; skips a pair with `(X <> -128) and (Y <> -128)` false - either byte - which
; is the rule this file ships; Lemmings.ts's particle-table.ts (Thomas
; Zeugner, MIT) tests the same. The trajectory table itself is data (LEMMAIN
; item 10). The VGA latch copy,
; the CRTC and the interleaves are facts about IBM and Hercules hardware;
; Lemmings is (C) 1991 DMA Design / Psygnosis and README.TXT carries the full
; attribution.
; =============================================================================

section .text

LEM_SPMAX     equ 168               ; save records: 80 lemmings, their 80
                                    ; countdown digits, and room to spare
LEM_SPREC     equ 6                 ; word a, byte cnt, byte n, word b
LEM_PBINS     equ 10                ; lem_r_spray's bins: the world's 160
                                    ; rows in 16-row bands (lemdraw.c's
                                    ; LEM_PBINS is the same number)
LEM_VCURSAVE  equ 65000             ; the cursor's own VGA save-under, above
                                    ; every sprite's: two parts of <= 3 x 14
LEM_SMK       equ 800               ; one sprite or object's mask rows: the
                                    ; largest object in the five sets is 11
                                    ; shifted bytes x 20 rows and the tallest
                                    ; 3 x 71, so 800 holds every one of them

; -----------------------------------------------------------------------------
; lb_clipx - the column clip, done once per draw so no inner loop tests.
; in: AX = x in the destination's BIT units (VGA/Hercules-W pixels, or TWICE a
;     pixel on a 2-bits-a-pixel destination), BX = source bytes a row in the
;     same units, DX = the destination's last byte column.
; out: lbp_s, lbp_bx0, lbp_i0, lbp_main, lbp_tail, lbp_cnt; CF = 1 when not
;     one byte of it is on the destination. Clobbers AX, CX, DX.
; -----------------------------------------------------------------------------
lb_clipx:
    mov cx, ax
    and cx, 7
    mov [lbp_s], cx
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov [lbp_bx0], ax
    xor cx, cx
    or ax, ax
    jns .i0
    mov cx, ax
    neg cx
.i0:
    mov [lbp_i0], cx
    sub dx, ax                      ; the last column, relative to bx0
    cmp dx, bx
    jle .i1
    mov dx, bx
.i1:
    cmp dx, cx
    jl .none
    xor ax, ax
    cmp dx, bx
    jne .nt
    inc ax                          ; byte bpr (cur = 0) is in range
    dec dx
.nt:
    mov [lbp_tail], ax
    sub dx, cx
    inc dx
    mov [lbp_main], dx
    add dx, ax
    mov [lbp_cnt], dx
    clc
    ret
.none:
    stc
    ret

; lb_clipy - the row clip. in: AX = y, BX = h, DX = the destination's row
; count. out: lbp_r0, lbp_n; CF = 1 when no row is on it. Clobbers AX, CX, DX.
lb_clipy:
    xor cx, cx
    or ax, ax
    jns .a
    mov cx, ax
    neg cx
.a:
    mov [lbp_r0], cx
    sub dx, ax
    cmp dx, bx
    jle .b
    mov dx, bx
.b:
    sub dx, cx
    jle .none
    mov [lbp_n], dx
    clc
    ret
.none:
    stc
    ret

; lb_shrow - one row of main + tail SHIFTED bytes out to ES:DI.
; in: DS:SI = the source row's byte i0 (and [si-1] is the row's byte i0-1 when
; lbp_i0 > 0), CL = the shift. The identity is wave 2's: byte i of the result
; is the low byte of ((src[i-1] << 8) | src[i]) >> s, src[-1] = src[bpr] = 0.
; Clobbers AX, CH, DX, SI, DI.
lb_shrow:
    xor ah, ah
    cmp word [cs:lbp_i0], 0
    je .np
    mov ah, [si-1]
.np:
    mov dx, [cs:lbp_main]
    or dx, dx
    jz .t
.l:
    lodsb
    mov ch, al
    shr ax, cl
    stosb
    mov ah, ch
    dec dx
    jnz .l
.t:
    cmp word [cs:lbp_tail], 0
    je .r
    xor al, al
    shr ax, cl
    stosb
.r:
    ret

; lb_srcrow - in: AX = the k'th clipped row. out: SI = that row's plane-0
; offset in the source, with the flip and lbp_off applied (not i0).
; Clobbers AX, DX.
lb_srcrow:
    add ax, [lbp_r0]
    cmp word [lbp_flip], 0
    je .nf
    neg ax
    add ax, [lbp_h]
    dec ax
.nf:
    mul word [lbp_bpr]
    add ax, [lbp_off]
    mov si, ax
    ret

; lb_orrow - OR of the colour planes of source row SI, bpr bytes, into
; lb_sorb (our segment). DS is ours on entry and on exit. Clobbers AX, BX, CX,
; DX, DI.
lb_orrow:
    push si
    push ds
    mov cx, [lbp_np]
    mov dx, [lbp_ps]
    mov bx, [lbp_bpr]
    mov ax, [lbp_seg]
    mov ds, ax
    xor di, di
.b:
    push si
    push cx
    xor al, al
.p:
    or al, [si]
    add si, dx
    loop .p
    pop cx
    pop si
    mov [cs:lb_sorb+di], al
    inc si
    inc di
    cmp di, bx
    jb .b
    pop ds
    pop si
    ret

; -----------------------------------------------------------------------------
; lb_vcore - one sprite or object frame into VGA memory, from the block.
;
; Plane-major: the shifted mask of every row goes into lb_smk first, so the
; Read Map Select and the Map Mask move four times a sprite rather than four
; times a row. Then d = (d & ~M) | (shifted plane & M) for each plane, with
; the plane's byte taken out of the colour planes it has (np), a single ink
; (np = 1) or nothing at all (a plane above np is clear where the mask is).
; -----------------------------------------------------------------------------
lb_vcore:
    push bp
    mov bp, sp
    sub sp, 4                       ; [bp-2] k, [bp-4] plane
    ; --- 1. every row's mask into lb_smk ---------------------------------
    mov word [bp-2], 0
.mrow:
    mov ax, [bp-2]
    cmp ax, [lbp_n]
    jb .mgo
    jmp .planes
.mgo:
    call lb_srcrow                  ; SI = the row's plane 0
    mov ax, [bp-2]
    mul word [lbp_cnt]
    add ax, lb_smk
    mov di, ax                      ; where this row's mask goes
    push cs
    pop es
    cmp word [lbp_mp], 0
    jne .mplane
    push di
    call lb_orrow                   ; (it takes CX with it)
    pop di
    mov cx, [lbp_s]                 ; the shift AFTER the OR, which used to
                                    ; run first and leave CL = np: every
                                    ; sprite's mask shifted by two whatever
                                    ; its x, and a lemming drawn as a sliver
    mov si, lb_sorb
    add si, [lbp_i0]
    call lb_shrow                   ; DS is ours: lb_sorb
    jmp .msol
.mplane:
    mov ax, [lbp_mp]
    mul word [lbp_ps]
    add si, ax
    add si, [lbp_i0]
    mov cx, [lbp_s]
    mov ax, [lbp_seg]
    push ds
    mov ds, ax
    call lb_shrow
    pop ds
.msol:
    cmp word [lbp_sol], 0
    je .mnext
    ; only on terrain (1) or no-overwrite (2), against the SOLID MASK under
    ; the destination bytes - the same row offset the world has in VRAM
    mov ax, [bp-2]
    call lem_rowoff                 ; DI = k * 200
    add di, [lbp_solo]
    mov ax, [bp-2]
    mul word [lbp_cnt]
    add ax, lb_smk
    mov si, ax
    mov ax, [lm_seg]
    mov es, ax
    mov cx, [lbp_cnt]
.ms:
    mov al, [es:di]
    cmp word [lbp_sol], 1
    je .msand
    not al
.msand:
    and [si], al
    inc si
    inc di
    loop .ms
.mnext:
    inc word [bp-2]
    jmp .mrow

    ; --- 2. the four planes --------------------------------------------
.planes:
    mov word [bp-4], 0
.plane:
    mov ax, [bp-4]
    mov ah, al
    mov al, 4                       ; Read Map Select
    call lb_gc
    mov cx, [bp-4]
    mov ax, 1
    shl ax, cl
    mov ah, al
    mov al, 2                       ; Map Mask
    call lb_seq
    mov word [bp-2], 0
.prow:
    mov ax, [bp-2]
    cmp ax, [lbp_n]
    jb .pgo
    jmp .pnext
.pgo:
    ; the shifted source byte row for this plane into lb_srow, unless the
    ; plane is an ink or above np
    mov word [lbp_how], 0           ; 0 combine lb_srow, 1 set, 2 clear
    cmp word [lbp_np], 1
    jne .pnotink
    mov ax, [lbp_ink]
    mov cx, [bp-4]
    shr ax, cl
    test al, 1
    mov word [lbp_how], 1
    jnz .phave
    mov word [lbp_how], 2
    jmp .phave
.pnotink:
    mov ax, [bp-4]
    cmp ax, [lbp_np]
    jb .psrc
    mov word [lbp_how], 2
    jmp .phave
.psrc:
    mov ax, [bp-2]
    call lb_srcrow
    mov ax, [bp-4]
    mul word [lbp_ps]
    add si, ax
    add si, [lbp_i0]
    push cs
    pop es
    mov di, lb_srow
    mov cx, [lbp_s]
    mov ax, [lbp_seg]
    push ds
    mov ds, ax
    call lb_shrow
    pop ds
.phave:
    ; the destination row
    mov ax, [bp-2]
    call lem_rowoff                 ; DI = k * 200
    add di, [lbp_dst]
    mov ax, [bp-2]
    mul word [lbp_cnt]
    add ax, lb_smk
    mov bx, ax                      ; M
    mov si, lb_srow
    mov ax, [lb_fb]
    mov es, ax
    mov cx, [lbp_cnt]
    mov dx, [lbp_how]
.pb:
    mov ah, [bx]                    ; m
    cmp dx, 1
    je .pset
    cmp dx, 2
    je .pclr
    mov al, [si]
    and al, ah
    not ah
    and ah, [es:di]
    or al, ah
    mov [es:di], al
    jmp .padv
.pset:
    or [es:di], ah
    jmp .padv
.pclr:
    not ah
    and [es:di], ah
.padv:
    inc bx
    inc si
    inc di
    loop .pb
    inc word [bp-2]
    jmp .prow
.pnext:
    inc word [bp-4]
    cmp word [bp-4], 4
    jae .done
    jmp .plane
.done:
    mov ah, 0x0F                    ; every plane, the state the rest of the
    mov al, 2                       ; raster assumes
    call lb_seq
    add sp, 4
    pop bp
    ret

; lb_vlatch - CX rows of AX bytes, from DS:SI to ES:DI through the latches.
; BX = the source's row step, DX = the destination's. Write mode 1 is set
; around it, and DS and ES are both the framebuffer. Clobbers AX, BX, CX, DX,
; SI, DI.
lb_vlatch:
    push ax
    push bx
    push cx
    push dx
    mov ah, 1
    mov al, 5                       ; write mode 1: a write stores the LATCHES
    call lb_gc
    mov ah, 0x0F                    ; ...into EVERY plane, which is not the
    mov al, 2                       ; ambient: lemfont.inc's cell leaves the
    call lb_seq                     ; Map Mask on its last plane, and a save
                                    ; taken under 0x08 kept plane 3 alone - the
                                    ; pointer then came off as an orange block
                                    ; (build/port-shots/wave3-v2-start.png)
    pop dx
    pop cx
    pop bx
    pop ax
    cld
.r:
    push cx
    push si
    push di
    mov cx, ax
    rep movsb
    pop di
    pop si
    pop cx
    add si, bx
    add di, dx
    loop .r
    mov ah, 0
    mov al, 5
    call lb_gc
    ret

; lb_wcopy - shadow row AX, bytes BX..DX inclusive, out of W, and dirty.
; CGA: a straight copy of the window's bytes. Hercules: W is 1bpp undoubled,
; so shadow byte b is the doubled nibble of W byte b>>1. Clobbers AX, BX, CX,
; DX, SI, DI.
lb_wcopy:
    push es
    push ds
    push ax
    push bx
    push dx
    push dx
    push bx
    push ax
    call lem_r_dirty                ; cdecl: row, b0, b1 (right to left above)
    add sp, 6
    pop dx
    pop bx
    pop ax
    push ax
    call lb_rowoff8                 ; DI = row * 80
    add di, bx
    pop ax
    mov cx, dx
    sub cx, bx
    inc cx
    push di
    call lem_rowoff                 ; DI = row * 200
    mov si, di
    pop di
    mov dx, [lb_shadow]
    mov es, dx
    cmp word [lb_kind], LEM_KIND_CGA
    jne .h
    shl si, 1                       ; the CGA world is 400 bytes a row
    mov dx, [lb_scroll]
    shr dx, 1
    shr dx, 1
    add si, dx
    add si, bx
    mov dx, [lb_world]
    mov ds, dx
    cld
    rep movsb
    jmp .o
.h:
    mov dx, [lb_scroll]
    shr dx, 1
    shr dx, 1
    shr dx, 1
    add si, dx
    mov dx, bx
    shr dx, 1
    add si, dx
    mov dx, [lb_world]
    mov ds, dx
.hb:
    mov al, [si]
    test bl, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    jmp .dbl
.lo:
    and al, 15
    inc si                          ; the next shadow byte reads the next W byte
.dbl:
    push bx
    mov bl, al
    xor bh, bh
    mov al, [cs:lb_dbl+bx]
    pop bx
    mov [es:di], al
    inc di
    inc bx
    loop .hb
.o:
    pop ds
    pop es
    ret

; -----------------------------------------------------------------------------
; void lem_r_sprite(unsigned seg, unsigned off, int x, int y, int w, int h,
;                   int mode)
;
; A lemming animation frame (or its countdown digit) at WORLD (x, y), top
; left - recorded first on the shadow backends, composed on VGA. mode = bpp | ink << 4: a frame is 2, 3 or 4 colour
; planes stored one after another with no row padding (MAIN section 0); a
; one-plane bitmap (the countdown digits, MAIN section 1) is drawn in `ink`.
; Colour 0 is transparent: the mask is the OR of the colour planes.
; -----------------------------------------------------------------------------
_lem_r_sprite:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cld
    call lb_msvga                   ; the buttons, latched through a long
                                    ; compose (SPEC.md 116.7 point 3)
    cmp word [lb_spn], LEM_SPMAX
    jb .room
    jmp .out
.room:
    mov ax, [bp+4]
    mov [lbp_seg], ax
    mov ax, [bp+6]
    mov [lbp_off], ax
    mov ax, [bp+14]                 ; h
    mov [lbp_h], ax
    mov ax, [bp+12]                 ; w
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov [lbp_bpr], ax
    mul word [bp+14]
    mov [lbp_ps], ax
    mov ax, [bp+16]
    mov bx, ax
    and ax, 15
    mov [lbp_np], ax
    mov cl, 4
    shr bx, cl
    mov [lbp_ink], bx
    xor ax, ax
    mov [lbp_mp], ax
    mov [lbp_flip], ax
    mov [lbp_sol], ax
    ; the rows: the world's 160, on every card
    mov ax, [bp+10]
    mov bx, [bp+14]
    mov dx, LEM_VIEWH
    call lb_clipy
    jnc .rows
    jmp .out
.rows:
    cmp word [lb_kind], LEM_KIND_VGA
    je .vga
    jmp .shadow

    ; --- VGA: NO SAVE-UNDER AND NO RECORD (SPEC.md 116.4.6). The frame
    ;     composes every rectangle that changed in the scratch - T, the
    ;     objects, then every sprite - and latches it on, so a sprite is never
    ;     taken off the glass at all: its old box is recomposed from T and the
    ;     objects, which is what the background IS. Composing (lem_r_cband):
    ;     clipped to the BAND's rows and drawn into the scratch. Not composing
    ;     (the first frame, and 'f'): straight onto the world in VRAM, which
    ;     the T load has just covered anyway.
.vga:
    cmp word [lb_vcomp], 0
    jne .vc
    mov ax, [bp+8]
    mov bx, [lbp_bpr]
    mov dx, LEM_MSTRIDE - 1
    call lb_clipx
    jc .vout
    mov ax, [bp+10]
    add ax, [lbp_r0]
    call lem_rowoff
    add di, LEM_VTERR
    jmp short .vdst
.vc:
    mov ax, [bp+10]
    sub ax, [lb_by0]
    mov bx, [bp+14]
    mov dx, [lb_bh]
    call lb_clipy
    jc .vout
    mov ax, [bp+8]
    mov bx, [lbp_bpr]
    mov dx, LEM_MSTRIDE - 1
    call lb_clipx
    jc .vout
    mov ax, [bp+10]
    add ax, [lbp_r0]
    call lem_rowoff
    add di, [lb_vbase]
.vdst:
    add di, [lbp_bx0]
    add di, [lbp_i0]
    mov [lbp_dst], di
    call lb_vcore
.vout:
    jmp .out

    ; --- CGA and Hercules: record the rectangle, then OR into the shadow -
.shadow:
    mov ax, [bp+8]
    sub ax, [lb_scroll]
    shl ax, 1                       ; two bits a pixel
    mov bx, [lbp_bpr]
    shl bx, 1
    mov dx, LEM_SSTRIDE - 1
    call lb_clipx
    jnc .sx
    jmp .out
.sx:
    mov bx, [lb_spn]
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    add bx, lb_sprec
    mov ax, [bp+10]
    add ax, [lbp_r0]
    mov [bx], ax                    ; a = the first shadow row
    mov al, [lbp_cnt]
    mov [bx+2], al
    mov al, [lbp_n]
    mov [bx+3], al
    mov ax, [lbp_bx0]
    add ax, [lbp_i0]
    mov [bx+4], ax                  ; b = the first shadow byte
    inc word [lb_spn]
    call lb_score
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; lb_score - the shadow half of a sprite: the doubled OR-mask of each row,
; shifted, OR'd into the shadow (both 1bpp cards draw a sprite in the SOLID
; class), and the rows dirtied. The record lem_r_sprite just wrote is the
; rectangle. Clobbers AX, BX, CX, DX, SI, DI, ES.
lb_score:
    push bp
    mov bp, sp
    sub sp, 4                       ; [bp-2] k, [bp-4] the first shadow row
    mov bx, [lb_spn]
    dec bx
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    add bx, lb_sprec
    mov ax, [bx]
    mov [bp-4], ax
    mov word [bp-2], 0
.row:
    mov ax, [bp-2]
    cmp ax, [lbp_n]
    jb .go
    jmp .done
.go:
    call lb_srcrow
    call lb_orrow                   ; lb_sorb[0..bpr-1], SI unchanged
    ; double it: lb_sorb2[2i], [2i+1] = the two nibbles of lb_sorb[i]
    xor si, si
    xor di, di
.dbl:
    mov al, [lb_sorb+si]
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov ah, [lb_dbl+bx]
    mov [lb_sorb2+di], ah
    mov bl, al
    and bl, 15
    mov ah, [lb_dbl+bx]
    mov [lb_sorb2+di+1], ah
    add di, 2
    inc si
    cmp si, [lbp_bpr]
    jb .dbl
    ; shift it into lb_mrow
    push cs
    pop es
    mov di, lb_mrow
    mov si, lb_sorb2
    add si, [lbp_i0]
    mov cx, [lbp_s]
    call lb_shrow
    ; OR it into the shadow, and dirty the bytes it touched
    mov ax, [bp-4]
    add ax, [bp-2]
    push ax
    call lb_rowoff8                 ; DI = row * 80
    mov bx, [lbp_bx0]
    add bx, [lbp_i0]
    add di, bx
    mov si, lb_mrow
    mov cx, [lbp_cnt]
    mov ax, [lb_shadow]
    mov es, ax
.or:
    lodsb
    or [es:di], al
    inc di
    loop .or
    pop ax
    mov dx, bx
    add dx, [lbp_cnt]
    dec dx
    push dx
    push bx
    push ax
    call lem_r_dirty
    add sp, 6
    inc word [bp-2]
    jmp .row
.done:
    add sp, 4
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_srest(void) - every sprite drawn since the last call, taken off:
; the shadow backends copy each rectangle of W back in, in any order, and the
; list is empty after. VGA keeps no list (lem_r_sprite), so it is a no-op there.
; void lem_r_sforget(void) - ...empty it WITHOUT restoring, which is what a
; shadow backend does after a scroll has recomposed the whole window anyway.
; -----------------------------------------------------------------------------
_lem_r_srest:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cld
.next:
    cmp word [lb_spn], 0
    je .done
    dec word [lb_spn]
    mov bx, [lb_spn]
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    add bx, lb_sprec
    xor ax, ax
    mov al, [bx+2]                  ; cnt
    xor cx, cx
    mov cl, [bx+3]                  ; n
    ; rows a .. a+n-1, bytes b .. b+cnt-1, out of W (VGA keeps no records:
    ; its sprites are composed, never saved under - lem_r_sprite)
    mov dx, [bx+4]
    add dx, ax
    dec dx                          ; b1
    mov si, [bx]                    ; the first row
    mov bx, [bx+4]                  ; b0
.shr:
    push cx
    push bx
    push dx
    push si
    mov ax, si
    call lb_wcopy
    pop si
    pop dx
    pop bx
    pop cx
    inc si
    loop .shr
    jmp .next
.done:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

_lem_r_sforget:
    mov word [lb_spn], 0
    ret

; -----------------------------------------------------------------------------
; void lem_r_spray(unsigned seg, unsigned off, int x, int y, int b0, int b1,
;                  int r0, int r1, int erase)
;
; ONE FRAME OF THE EXPLOSION'S SPRAY (SPEC.md 116.8, decision 5): seg:off is
; the frame's 160 bytes of LEMMAIN item 10 - eighty signed (dx, dy) pairs out
; of PARTICLES.DAT, a pair with EITHER byte -128 not drawn (Lemmix
; DrawParticles) - placed at WORLD (x, y), the bomber's foot. Particle p is
; ONE pixel in the original's colour for p mod 16 (Lemmix
; ParticleColorIndices; the level palette's own indices). Only a particle
; inside world bytes b0..b1 and rows r0..r1 is touched: that box is the C's
; record of where this frame is drawn, which is what its erasure covers. The
; C keeps the box inside the world.
;
;   erase 2, 3  THE FRAME'S BINS, VGA ONLY and drawing nothing (wave 6's
;             second review): each particle that would be drawn widens the
;             bin of its 16-row band - lb_pbins, ten of {byte lo, byte hi,
;             row lo, row hi}, an empty one lo = 0FFh - so the VGA frame's
;             rectangles for a spray are one a band it occupies (lemdraw.c
;             ovl_spray_bins) and not one box over the whole cloud: 3.6x
;             less area on the real table. 2 empties the bins first, 3 adds
;             to what is there, so the old frame's and the new frame's
;             pixels fold into one bin a band. lb_pbins is bytes 320..359 of
;             lb_sprec, which on CGA and Hercules are LIVE sprite records
;             (54-60 of the 168), so a shadow card is refused at the door
;             (wave 6's third review) rather than left to corrupt its own
;             save-under. Each walk is 80 pairs whatever the box holds -
;             ~45 instructions a binned pair, ~44K cycles, ~9 ms on a
;             4.77 MHz 8088 - which is why lemdraw.c takes the bins only for
;             a lone spray on an uncrowded list.
;
;   VGA       write mode 2 and the Bit Mask a pixel (lb_rpix's batch): into
;             the compose scratch inside a band, rows outside it skipped, or
;             straight onto the world in VRAM outside one (the first frame and
;             'f', which the T load has just covered). `erase` is a no-op: a
;             compose rebuilds the old box from T.
;   CGA/Herc  the shadow, in the SOLID class every sprite is drawn in
;             (lb_score), the byte's span dirtied; `erase` copies each
;             particle's byte back out of W instead (lb_wcopy). Columns
;             outside the 320-pixel window are skipped.
;
; ATTRIBUTION. The table's layout, the -128 rule and the sixteen colours are
; the original's as Lemmix's src/Game.pas carries them (TParticleTable,
; ParticleColorIndices, DrawParticles; Eric Langedijk, Lemmix's own licence)
; and Lemmings.ts's particle-table.ts (MIT) reads them; the sixteen indices
; below are carried VERBATIM from Lemmix's ParticleColorIndices (this file's
; header says so too) and nothing else is copied.
; -----------------------------------------------------------------------------
lb_pcol:    db 4, 15, 14, 13, 12, 11, 10, 9, 8, 11, 10, 9, 8, 7, 6, 2

_lem_r_spray:
    push bp
    mov bp, sp
    sub sp, 2                       ; [bp-2] the particle, 0..79
    push si
    push di
    push es
    push ds
    cld
    mov ax, [bp+20]
    cmp ax, 2
    jb .draw
    cmp word [lb_kind], LEM_KIND_VGA
    je .vbin
    jmp .out                        ; the bins are VGA's alone (above)
.vbin:
    cmp ax, 3
    je .go                          ; 3: onto the bins as they are
    push ds
    pop es
    mov di, lb_pbins
    mov cx, LEM_PBINS * 2
    mov ax, 0x00FF                  ; lo 0FFh, hi 0: empty
    rep stosw
    jmp short .go
.draw:
    cmp word [lb_kind], LEM_KIND_VGA
    jne .go
    or ax, ax
    jz .vin
    jmp .out
.vin:
    call lb_msample                 ; the buttons, through a long compose
    call lb_batch_in                ; write mode 2, every plane
    mov ax, [lb_fb]
    mov es, ax
.go:
    mov word [bp-2], 0
.next:
    mov si, [bp-2]
    shl si, 1
    add si, [bp+6]
    push ds
    mov ax, [bp+4]
    mov ds, ax
    mov ax, [si]                    ; AL = dx, AH = dy
    pop ds
    cmp al, 0x80
    je .skip1
    cmp ah, 0x80
    je .skip1
    mov dl, ah
    cbw
    add ax, [bp+8]
    mov bx, ax                      ; BX = the world x
    mov al, dl
    cbw
    add ax, [bp+10]
    mov cx, ax                      ; CX = the world y
    cmp cx, [bp+16]
    jl .skip1
    cmp cx, [bp+18]
    jg .skip1
    mov ax, bx
    sar ax, 1
    sar ax, 1
    sar ax, 1                       ; AX = the world byte
    cmp ax, [bp+12]
    jl .skip1
    cmp ax, [bp+14]
    jle .inbox
.skip1:
    jmp .skip
.inbox:
    cmp word [bp+20], 2
    jb .nobin
    jmp .bin
.nobin:
    cmp word [lb_kind], LEM_KIND_VGA
    je .vga
    jmp .shadow

.vga:
    mov dx, ax                      ; the byte
    mov ax, cx
    cmp word [lb_vcomp], 0
    je .vglass
    sub ax, [lb_by0]
    jl .skip1
    cmp ax, [lb_bh]
    jge .skip1
    mov ax, cx
    call lem_rowoff                 ; DI = y * 200
    add di, [lb_vbase]
    jmp short .vput
.vglass:
    call lem_rowoff
    add di, LEM_VTERR
.vput:
    add di, dx
    mov cl, bl
    and cl, 7
    mov ah, 0x80
    shr ah, cl
    mov al, 8                       ; the Bit Mask: this pixel alone
    call lb_gc
    mov bx, [bp-2]
    and bx, 15
    mov al, [cs:lb_pcol+bx]
    mov ah, [es:di]                 ; the latches, so the other seven stay
    mov [es:di], al                 ; write mode 2: AL's nibble is the colour
    jmp .skip

.shadow:
    mov ax, bx
    sub ax, [lb_scroll]
    jl .skip2
    cmp ax, 320                     ; the window's width in world pixels
    jge .skip2
    mov dx, ax
    shr dx, 1
    shr dx, 1                       ; DX = the shadow byte: four pixels a byte
                                    ; on CGA, four doubled ones on Hercules
    cmp word [bp+20], 0
    jne .erase
    push ax
    mov ax, cx
    call lb_rowoff8                 ; DI = y * 80
    pop ax
    add di, dx
    push cx
    mov cl, al
    and cl, 3
    shl cl, 1
    mov al, 0xC0                    ; the pixel's two bits: the solid class
    shr al, cl
    pop cx
    mov bx, [lb_shadow]
    mov es, bx
    or [es:di], al
    mov bx, cx                      ; the row's dirty span, widened to DL
    cmp byte [lb_dlo+bx], 0xFF
    jne .wd
    mov [lb_dlo+bx], dl
    mov [lb_dhi+bx], dl
    jmp short .skip
.wd:
    cmp dl, [lb_dlo+bx]
    jae .wd1
    mov [lb_dlo+bx], dl
.wd1:
    cmp dl, [lb_dhi+bx]
    jbe .skip
    mov [lb_dhi+bx], dl
.skip2:
    jmp short .skip
.erase:
    mov ax, cx
    mov bx, dx
    call lb_wcopy                   ; row AX, bytes BX..DX, out of W, dirtied
    jmp short .skip
.bin:
    mov di, cx                      ; the row's band: (y >> 4) * 4
    shr di, 1
    shr di, 1
    and di, 0xFFFC
    add di, lb_pbins
    cmp al, [di]
    jae .bn1
    mov [di], al
.bn1:
    cmp al, [di+1]
    jbe .bn2
    mov [di+1], al
.bn2:
    cmp cl, [di+2]
    jae .bn3
    mov [di+2], cl
.bn3:
    cmp cl, [di+3]
    jbe .skip
    mov [di+3], cl
.skip:
    inc word [bp-2]
    cmp word [bp-2], 80
    jae .done
    jmp .next
.done:
    cmp word [bp+20], 0             ; only the VGA's drawing arm opened a batch
    jne .out
    cmp word [lb_kind], LEM_KIND_VGA
    jne .out
    call lb_batch_out
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 2
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_cursor(const unsigned char *bits, int sx, int sy)
;
; THE APP'S OWN 14x14 POINTER (SPEC.md 116.4.6), in SCREEN coordinates - 0..319
; by 0..199, the world view above row 160 and the panel below it - top left.
; Whatever the last call drew is taken off first; `bits` 0 draws nothing new.
; `bits` is DS-relative: TWO PLANES of 14 rows of 2 bytes, plane 0 first, so a
; pixel is colour p0 | p1 << 1 - Lemmix's three pointer colours, palette 1, 2
; and 3 (lemdraw.c's lem_cur0) - drawn as colours on VGA, the way a 2bpp
; lemming is, and as their OR in the solid class on the shadow backends.
;
; void lem_r_cplace(bits, sx, sy) - THE SAME PLACE AND SHAPE, SET AND NOT
; DRAWN, for a VGA compose that moves the pointer inside itself (lemdraw.c
; ovl_vframe): nothing is taken off the glass (the compose recomposes the old
; box), nothing is saved and nothing drawn (lem_r_csave and lem_r_cdraw do
; both, into the scratch). The caller keeps both boxes wholly in the world, so
; there is no panel piece to place.
;
; ON VGA IT IS TWO PIECES, because the screen is two places in VRAM: the rows
; above the line compare are the WORLD at the view's own panned offset and the
; rows below it are the PANEL at offset 0, unpanned. Each piece is saved under
; and drawn separately, and taken off the same way.
; -----------------------------------------------------------------------------
_lem_r_cplace:
    mov byte [lb_cnod], 1
    mov word [lb_curn0], 0          ; nothing of it is on the glass to take
    mov word [lb_curn1], 0          ; off: the compose rebuilds the old box
_lem_r_cursor:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cld
    cmp byte [lb_cnod], 0
    jne .noff
    call lb_curoff
.noff:
    cmp word [bp+4], 0
    jne .on
    jmp .out
.on:
    mov ax, [bp+6]
    mov [lb_cursx], ax              ; the place and the shape, for lem_r_cdraw
    mov ax, [bp+8]
    mov [lb_cursy], ax
    mov ax, [bp+4]
    mov [lb_curbits], ax
    call lb_curset
    cmp word [lb_kind], LEM_KIND_VGA
    je .vga
    jmp .shadow

.vga:
    ; --- the world piece: rows sy .. 159 -------------------------------
    mov ax, [bp+8]
    mov bx, 14
    mov dx, LEM_VIEWH
    call lb_clipy
    jc .panel
    mov ax, [lb_scroll]
    add ax, [bp+6]
    mov bx, 2
    mov dx, LEM_MSTRIDE - 1
    call lb_clipx
    jc .panel
    mov ax, [bp+8]
    add ax, [lbp_r0]
    mov [lb_curr], ax               ; the world row and byte the piece starts
    call lem_rowoff                 ; at, for lem_r_csave
    mov ax, [lbp_bx0]
    add ax, [lbp_i0]
    mov [lb_curb], ax
    add di, ax
    add di, LEM_VTERR
    mov [lbp_dst], di
    mov si, di
    mov di, LEM_VCURSAVE
    mov [lb_cur0], si
    mov ax, [lbp_cnt]
    mov [lb_curc0], ax
    mov ax, [lbp_n]
    mov [lb_curn0], ax
    cmp byte [lb_cnod], 0           ; lem_r_cplace: placed, and that is all
    jne .out
    mov ax, [lbp_cnt]
    mov bx, LEM_VSTRIDE
    mov dx, ax
    push ds
    mov cx, [lb_fb]
    mov ds, cx
    mov es, cx
    mov cx, [cs:lbp_n]
    call lb_vlatch
    pop ds
    call lb_vcore
.panel:
    cmp byte [lb_cnod], 0
    jne .out
    ; --- the panel piece: rows 160 .. sy+13, as panel rows 0.. ---------
    mov ax, [bp+8]
    sub ax, LEM_VIEWH
    mov bx, 14
    mov dx, 40
    call lb_clipy
    jnc .pgo
    jmp .out
.pgo:
    mov ax, [bp+6]
    mov bx, 2
    mov dx, 39                      ; the panel is the first 40 bytes a row
    call lb_clipx
    jnc .pgo2
    jmp .out
.pgo2:
    mov ax, [bp+8]
    sub ax, LEM_VIEWH
    add ax, [lbp_r0]
    call lem_rowoff
    add di, [lbp_bx0]
    add di, [lbp_i0]
    mov [lbp_dst], di
    mov si, di
    mov di, LEM_VCURSAVE + 64
    mov [lb_cur1], si
    mov ax, [lbp_cnt]
    mov [lb_curc1], ax
    mov ax, [lbp_n]
    mov [lb_curn1], ax
    mov ax, [lbp_cnt]
    mov bx, LEM_VSTRIDE
    mov dx, ax
    push ds
    mov cx, [lb_fb]
    mov ds, cx
    mov es, cx
    mov cx, [cs:lbp_n]
    call lb_vlatch
    pop ds
    call lb_vcore
    jmp .out

.shadow:
    ; --- the shadow is one place: rows 0..199, bytes 0..79 --------------
    mov ax, [bp+8]
    mov bx, 14
    mov dx, LEM_SROWS
    call lb_clipy
    jnc .sgo
    jmp .out
.sgo:
    mov ax, [bp+6]
    shl ax, 1
    mov bx, 4
    mov dx, LEM_SSTRIDE - 1
    call lb_clipx
    jnc .sgo2
    jmp .out
.sgo2:
    ; save the bytes under it into lb_cursv, rows r0.., cnt a row
    mov ax, [bp+8]
    add ax, [lbp_r0]
    mov [lb_cur0], ax               ; the first shadow row
    mov ax, [lbp_bx0]
    add ax, [lbp_i0]
    mov [lb_cur1], ax               ; the first shadow byte
    mov ax, [lbp_cnt]
    mov [lb_curc0], ax
    mov ax, [lbp_n]
    mov [lb_curn0], ax
    mov word [lb_curn1], 0
    mov ax, [lb_shadow]
    mov es, ax
    xor bx, bx                      ; k
    mov dx, lb_cursv
.ssave:
    cmp bx, [lbp_n]
    jae .sdraw
    mov ax, [lb_cur0]
    add ax, bx
    push bx
    call lb_rowoff8
    add di, [lb_cur1]
    mov si, di
    mov di, dx
    mov cx, [lbp_cnt]
.sv:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .sv
    mov dx, di
    pop bx
    inc bx
    jmp .ssave
.sdraw:
    ; a fake record so lb_score can read its first row, then the draw
    mov bx, [lb_spn]
    push bx
    mov ax, bx
    shl bx, 1
    add bx, ax
    shl bx, 1
    add bx, lb_sprec
    mov ax, [lb_cur0]
    mov [bx], ax
    inc word [lb_spn]
    call lb_score
    pop bx
    mov [lb_spn], bx                ; ...and gone again: the cursor is not a
                                    ; sprite the frame restores
.out:
    mov byte [lb_cnod], 0
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; lb_curset - the pointer's bitmap (DS:AX, two planes of 14 rows of 2 bytes)
; as the source lb_vcore / lb_score draw: a 2bpp sprite, colours 1-3. Clobbers
; AX.
lb_curset:
    mov [lbp_off], ax
    mov ax, ds
    mov [lbp_seg], ax
    mov word [lbp_bpr], 2
    mov word [lbp_ps], 28
    mov word [lbp_h], 14
    mov word [lbp_np], 2
    mov word [lbp_ink], 0
    xor ax, ax
    mov [lbp_mp], ax
    mov [lbp_flip], ax
    mov [lbp_sol], ax
    ret

; lb_curoff - take the pointer off, wherever it last went. Clobbers AX, BX,
; CX, DX, SI, DI, ES.
lb_curoff:
    cmp word [lb_curn0], 0
    jne .some
    cmp word [lb_curn1], 0
    jne .some
    ret
.some:
    cmp word [lb_kind], LEM_KIND_VGA
    jne .sh
    push ds
    mov ax, [lb_fb]
    mov ds, ax
    mov es, ax
    mov cx, [cs:lb_curn0]
    jcxz .p1
    mov si, LEM_VCURSAVE
    mov di, [cs:lb_cur0]
    mov ax, [cs:lb_curc0]
    mov bx, ax
    mov dx, LEM_VSTRIDE
    call lb_vlatch
.p1:
    mov cx, [cs:lb_curn1]
    jcxz .vd
    mov si, LEM_VCURSAVE + 64
    mov di, [cs:lb_cur1]
    mov ax, [cs:lb_curc1]
    mov bx, ax
    mov dx, LEM_VSTRIDE
    call lb_vlatch
.vd:
    pop ds
    jmp .clr
.sh:
    mov ax, [lb_shadow]
    mov es, ax
    xor bx, bx
    mov si, lb_cursv
.sr:
    cmp bx, [lb_curn0]
    jae .clr
    mov ax, [lb_cur0]
    add ax, bx
    push bx
    push ax
    call lb_rowoff8
    add di, [lb_cur1]
    mov cx, [lb_curc0]
    mov dx, di
.sb:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .sb
    pop ax                          ; the row, dirtied over the bytes put back
    mov bx, [lb_cur1]
    mov dx, bx
    add dx, [lb_curc0]
    dec dx
    push si
    push dx
    push bx
    push ax
    call lem_r_dirty
    add sp, 6
    pop si
    pop bx
    inc bx
    jmp .sr
.clr:
    mov word [lb_curn0], 0
    mov word [lb_curn1], 0
    ret

; -----------------------------------------------------------------------------
; void lem_r_bgrect(int x, int y, int w, int h, int sync)
;
; THE BACKGROUND, PUT BACK over a world rectangle - widened to whole bytes -
; before the objects over it are drawn again. VGA copies T into VRAM, and with
; `sync` first brings T into line with the mask (sync 2: ONLY that, and no
; copy - the VGA frame syncs every destruction rectangle first and composes
; them with everything else afterwards, lemdraw.c ovl_vframe): a pixel the mask lost is
; cleared from all four planes, and a pixel the mask GAINED that T has no colour
; for is a builder's brick, colour 7 (planes 0-2 set, plane 3 clear -
; lemmings_main_dat_file_format.txt section 2: "Color idx 7 is the color used
; to render the build bricks"). The shadow backends re-derive W's bytes from
; the mask, by class.
; -----------------------------------------------------------------------------
_lem_r_bgrect:
    push bp
    mov bp, sp
    sub sp, 6                       ; [bp-2] row, [bp-4] b0, [bp-6] bytes
    push si
    push di
    push es
    push ds
    cld
    call lb_msvga                   ; the buttons, latched through a long
                                    ; compose (SPEC.md 116.7 point 3)
    ; rows y .. y+h-1, clipped to the world
    mov ax, [bp+6]
    mov bx, [bp+10]
    mov dx, LEM_VIEWH
    call lb_clipy
    jnc .r
    jmp .out
.r:
    mov ax, [bp+6]
    add ax, [lbp_r0]
    mov [bp-2], ax
    ; bytes x>>3 .. (x+w-1)>>3, clipped to 0..199 (the mask's own bytes)
    mov ax, [bp+4]
    sar ax, 1
    sar ax, 1
    sar ax, 1
    mov bx, [bp+4]
    add bx, [bp+8]
    dec bx
    sar bx, 1
    sar bx, 1
    sar bx, 1
    cmp ax, 0
    jge .c0
    xor ax, ax
.c0:
    cmp bx, LEM_MSTRIDE - 1
    jle .c1
    mov bx, LEM_MSTRIDE - 1
.c1:
    cmp bx, ax
    jge .c2
    jmp .out
.c2:
    mov [bp-4], ax
    sub bx, ax
    inc bx
    mov [bp-6], bx
    cmp word [lb_kind], LEM_KIND_VGA
    je .vga
    jmp .shadow

.vga:
    cmp word [lb_t], 0
    jne .vt
    jmp .out
.vt:
    cmp word [bp+12], 0
    je .vcopy
    ; --- sync T with the mask, byte by byte ----------------------------
    xor cx, cx                      ; k
.syrow:
    cmp cx, [lbp_n]
    jb .sygo
    cmp word [bp+12], 2             ; 2 = the sync alone: the frame composes
    je .syout                       ; the rectangle with everything else
    jmp .vcopy
.syout:
    jmp .out
.sygo:
    push cx
    mov ax, [bp-2]
    add ax, cx
    call lem_rowoff
    add di, [bp-4]                  ; the byte offset in every plane
    mov cx, [bp-6]
.syb:
    push cx
    mov ax, [lm_seg]
    mov es, ax
    mov dl, [es:di]                 ; m
    mov ax, [lb_t]
    mov es, ax
    mov al, [es:di]                 ; t0
    mov bx, [lb_t]
    add bx, LEM_TPLANE
    mov es, bx
    mov ah, [es:di]                 ; t1
    or al, ah
    add bx, LEM_TPLANE
    mov es, bx
    or al, [es:di]                  ; t2
    add bx, LEM_TPLANE
    mov es, bx
    or al, [es:di]                  ; t3: AL = any colour at all
    not al
    and al, dl                      ; nb = m & ~colour: the new bricks
    mov dh, al
    ; t3 &= m, then t2, t1, t0 = (t & m) | nb
    and [es:di], dl
    mov cx, 3
.syp:
    sub bx, LEM_TPLANE
    mov es, bx
    mov al, [es:di]
    and al, dl
    or al, dh
    mov [es:di], al
    loop .syp
    inc di
    pop cx
    loop .syb
    pop cx
    inc cx
    jmp .syrow

.vcopy:
    ; --- T into VRAM, plane by plane - or into the compose scratch -------
    mov ax, LEM_VTERR
    cmp word [lb_vcomp], 0
    je .vb
    mov ax, [lb_vbase]
.vb:
    mov [lbp_vb], ax
    mov dx, 1
    mov bx, [lb_t]
.vp:
    push dx
    push bx
    mov ah, dl
    mov al, 2
    call lb_seq
    pop bx
    pop dx
    xor cx, cx
.vrow:
    cmp cx, [lbp_n]
    jae .vpn
    push cx
    mov ax, [bp-2]
    add ax, cx
    call lem_rowoff
    add di, [bp-4]
    mov si, di
    add di, [lbp_vb]
    mov ax, [lb_fb]
    mov es, ax
    mov cx, [bp-6]
    push ds
    mov ds, bx
    rep movsb
    pop ds
    pop cx
    inc cx
    jmp .vrow
.vpn:
    add bx, LEM_TPLANE
    shl dl, 1
    cmp dl, 16
    jb .vp
    mov ah, 0x0F
    mov al, 2
    call lb_seq
    jmp .out

.shadow:
    ; --- W's bytes out of the mask, by class (lem_m_derive's two arms) --
    xor cx, cx
.srow:
    cmp cx, [lbp_n]
    jb .sgo
    jmp .out
.sgo:
    push cx
    mov ax, [bp-2]
    add ax, cx
    push ax
    call lem_rowoff                 ; DI = row * 200, the mask's own row
    add di, [bp-4]
    mov si, di
    pop ax
    mov cx, [bp-6]
    mov dx, [lb_world]
    mov es, dx
    cmp word [lb_kind], LEM_KIND_CGA
    jne .sh
    shl di, 1                       ; the CGA world is 400 bytes a row
.scb:
    push ds
    mov dx, [cs:lm_seg]
    mov ds, dx
    mov al, [si]
    pop ds
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov ah, [lm_nib2+bx]
    mov [es:di], ah
    mov bl, al
    and bl, 15
    mov ah, [lm_nib2+bx]
    mov [es:di+1], ah
    add di, 2
    inc si
    loop .scb
    jmp .snext
.sh:
    mov ah, 0xAA
    test al, 1
    jz .shd
    mov ah, 0x55
.shd:
    push ds
    mov dx, [cs:lm_seg]
    mov ds, dx
.shb:
    mov al, [si]
    and al, ah
    mov [es:di], al
    inc si
    inc di
    loop .shb
    pop ds
.snext:
    pop cx
    inc cx
    jmp .srow
.out:
    pop ds
    pop es
    pop di
    pop si
    add sp, 6
    pop bp
    ret

; -----------------------------------------------------------------------------
; THE VGA COMPOSE (SPEC.md 116.4.6). Nothing that changes in a game frame is
; drawn onto the glass: it is built in the scratch at LEM_VCOMP - T, the
; objects over it, every lemming, and the pointer - and LATCH-COPIED on in one
; pass a rectangle (write mode 1: a byte moves four planes), so no pixel of a
; lemming, an object or the pointer is ever missing from the glass between two
; frames. Wave 3 first restored every save-under straight onto the glass and
; drew the frame there, and a lemming was off the glass from its restore to its
; draw: the whole frame on an XT (wave 3's second review).
;
; THE SCRATCH SHARES THE WORLD'S 200-BYTE STRIDE so nothing inside the draws
; changes: their row offset is row * 200 + byte as ever, and only the BASE
; added to it moves - LEM_VCOMP - y0 * 200, which may wrap the 16-bit offset
; and wraps straight back on the add. It holds a BAND of LEM_VCOMPH rows, every
; one of the world's 200 bytes wide, so every rectangle of a frame that falls
; in one band is composed at once and a lemming crossing two of them is drawn
; ONCE.
;
;   int  lem_r_cbegin(y0, h, b0, b1)  the band AND the clip are that one
;                                     rectangle; 0 = not composing (another
;                                     card, or h outside 1..LEM_VCOMPH) and
;                                     the caller draws onto the glass
;   int  lem_r_cband(y0, h)           a band; the clip is the whole band
;   void lem_r_cclip(y0, h, b0, b1)   the clip lem_r_obj, lem_r_clatch and
;                                     lem_r_csave obey (inside the band)
;   void lem_r_clatch(void)           the clip, scratch -> glass
;   void lem_r_cend(void)             composing no more
;   void lem_r_csave(void)            the POINTER's save-under, brought up to
;                                     date from the scratch over the clip
;   void lem_r_cdraw(void)            ...and the pointer drawn INTO the scratch
;
; The pointer is the reason for the last two: its world piece lives in the
; world VRAM the latch writes, so a rectangle copied on under a lit pointer
; would rub it out and leave its save-under stale. Instead every rectangle's
; part of its box is copied into the save first, from the scratch - the
; picture the glass is about to hold - and the pointer is then drawn into the
; scratch, so the latch puts it straight back where it was. It never leaves.
; -----------------------------------------------------------------------------
_lem_r_cbegin:
    push bp
    mov bp, sp
    push word [bp+6]
    push word [bp+4]
    call _lem_r_cband
    add sp, 4
    or ax, ax
    jz .no
    mov ax, [bp+8]
    mov [lb_cb0], ax
    mov ax, [bp+10]
    mov [lb_cb1], ax
    mov ax, 1
.no:
    pop bp
    ret

_lem_r_cband:
    push bp
    mov bp, sp
    push di
    xor ax, ax
    cmp word [lb_kind], LEM_KIND_VGA
    jne .no
    mov cx, [bp+6]
    cmp cx, 0
    jle .no
    cmp cx, LEM_VCOMPH
    jg .no
    mov [lb_bh], cx
    mov [lb_ch], cx
    mov ax, [bp+4]
    mov [lb_by0], ax
    mov [lb_cy0], ax
    call lem_rowoff                 ; DI = y0 * 200
    mov ax, LEM_VCOMP
    sub ax, di
    mov [lb_vbase], ax
    mov word [lb_cb0], 0
    mov word [lb_cb1], LEM_MSTRIDE - 1
    mov word [lb_vcomp], 1
    mov ax, 1
.no:
    pop di
    pop bp
    ret

_lem_r_cclip:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [lb_cy0], ax
    mov ax, [bp+6]
    mov [lb_ch], ax
    mov ax, [bp+8]
    mov [lb_cb0], ax
    mov ax, [bp+10]
    mov [lb_cb1], ax
    pop bp
    ret

_lem_r_cend:
    mov word [lb_vcomp], 0
    ret

_lem_r_clatch:
    cmp word [lb_vcomp], 0
    jne .go
    ret
.go:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    call lb_msvga                   ; the buttons, latched through a long
                                    ; compose (SPEC.md 116.7 point 3)
    mov cx, [lb_ch]
    jcxz .none
    mov ax, [lb_cy0]
    call lem_rowoff
    add di, [lb_cb0]
    mov si, di
    add si, [lb_vbase]              ; the scratch
    add di, LEM_VTERR               ; the glass
    mov ax, [lb_cb1]
    sub ax, [lb_cb0]
    inc ax                          ; bytes a row
    mov bx, LEM_VSTRIDE
    mov dx, LEM_VSTRIDE
    push ax
    mov ax, [lb_fb]
    mov ds, ax
    mov es, ax
    pop ax
    call lb_vlatch
.none:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; lem_r_csave - the clip's share of the pointer's WORLD piece, scratch -> its
; save-under. The piece is rows lb_curr .. +lb_curn0-1 and bytes lb_curb ..
; +lb_curc0-1 of the world, packed lb_curc0 bytes a row at LEM_VCURSAVE.
_lem_r_csave:
    cmp word [lb_vcomp], 0
    je .ret
    cmp word [lb_curn0], 0
    jne .go
.ret:
    ret
.go:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    ; rows: max(cy0, curr) .. min(cy0+ch, curr+curn0) - 1
    mov ax, [lb_cy0]
    mov bx, [lb_curr]
    cmp ax, bx
    jge .r0
    mov ax, bx
.r0:
    mov cx, [lb_cy0]
    add cx, [lb_ch]
    mov dx, bx
    add dx, [lb_curn0]
    cmp cx, dx
    jle .r1
    mov cx, dx
.r1:
    sub cx, ax                      ; CX = rows, AX = the first
    jle .none
    push ax                         ; [the first row]
    ; bytes: max(cb0, curb) .. min(cb1 + 1, curb + curc0) - 1
    mov si, [lb_cb0]
    mov bx, [lb_curb]
    cmp si, bx
    jge .b0
    mov si, bx
.b0:
    mov dx, [lb_cb1]
    inc dx
    mov di, bx
    add di, [lb_curc0]
    cmp dx, di
    jle .b1
    mov dx, di
.b1:
    sub dx, si                      ; DX = bytes, SI = the first
    jg .some
    pop ax
    jmp short .none
.some:
    pop ax
    push dx
    push cx
    push si
    push ax
    ; the save's offset: LEM_VCURSAVE + (row - curr) * curc0 + (byte - curb)
    sub ax, [lb_curr]
    mul word [lb_curc0]
    add ax, LEM_VCURSAVE
    sub si, [lb_curb]
    add ax, si
    mov bx, ax                      ; BX = the save
    pop ax
    call lem_rowoff                 ; DI = first row * 200
    pop si
    add si, di
    add si, [lb_vbase]              ; SI = the scratch
    mov di, bx
    pop cx                          ; rows
    pop ax                          ; bytes a row
    mov bx, LEM_VSTRIDE
    mov dx, [lb_curc0]
    push ax
    mov ax, [lb_fb]
    mov ds, ax
    mov es, ax
    pop ax
    call lb_vlatch
.none:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; lem_r_cdraw - the pointer's world piece drawn into the scratch, at the
; position and in the shape it is lit at, clipped to the band. Its save-under
; is NOT touched (lem_r_csave did that, from the picture without it).
_lem_r_cdraw:
    cmp word [lb_vcomp], 0
    je .ret
    cmp word [lb_curn0], 0
    jne .go
.ret:
    ret
.go:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cld
    mov ax, [lb_curbits]
    call lb_curset
    mov ax, [lb_cursy]              ; the band is inside the world's 160
    sub ax, [lb_by0]                ; rows, so this clip keeps the PANEL
    mov bx, 14                      ; piece out as well
    mov dx, [lb_bh]
    call lb_clipy
    jc .out
    mov ax, [lb_scroll]
    add ax, [lb_cursx]
    mov bx, 2
    mov dx, LEM_MSTRIDE - 1
    call lb_clipx
    jc .out
    mov ax, [lb_cursy]
    add ax, [lbp_r0]
    call lem_rowoff
    add di, [lb_vbase]
    add di, [lbp_bx0]
    add di, [lbp_i0]
    mov [lbp_dst], di
    call lb_vcore
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; int lem_r_shit(int sx0, int sy0, int sx1, int sy1)
;
; Does any sprite record - what lem_r_srest will put back - cross the SCREEN
; rectangle sx0..sx1, sy0..sy1 (inclusive, rows 0..159)? It is the pointer's
; question on the two SHADOW backends (lemdraw.c ovl_cur_hit): a restore under
; a lit pointer writes over it and leaves its save-under stale. A record is a
; shadow row and byte (4 pixels a byte on both cards). VGA keeps no records -
; its frame composes the pointer in with everything else (lem_r_cdraw) - so
; the answer there is always 0. 1 = yes.
; -----------------------------------------------------------------------------
_lem_r_shit:
    push bp
    mov bp, sp
    push si
    push di
    mov si, lb_sprec
    mov cx, [lb_spn]
.rec:
    jcxz .none
    push cx
    mov di, [si]                    ; the first shadow row
    mov ax, [si+4]
    shl ax, 1
    shl ax, 1                       ; the first screen pixel
    xor bx, bx
    mov bl, [si+2]
    shl bx, 1
    shl bx, 1
    ; x: [ax, ax + bx - 1] against [sx0, sx1]
    cmp ax, [bp+8]
    jg .miss
    add ax, bx
    dec ax
    cmp ax, [bp+4]
    jl .miss
    ; y: [di, di + n - 1] against [sy0, sy1]
    cmp di, [bp+10]
    jg .miss
    xor bx, bx
    mov bl, [si+3]
    add di, bx
    dec di
    cmp di, [bp+6]
    jl .miss
    pop cx
    mov ax, 1
    jmp .out
.miss:
    pop cx
    add si, LEM_SPREC
    dec cx
    jmp .rec
.none:
    xor ax, ax
.out:
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_show(int x, int y, int w, int h)
;
; The shadow backends: W's rectangle, where it falls inside the view, copied
; into the shadow and dirtied - which is how a rebuilt background and a
; redrawn object reach the glass on a card that composes. VGA: nothing, its
; background IS the screen.
; -----------------------------------------------------------------------------
_lem_r_show:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cld
    cmp word [lb_kind], LEM_KIND_VGA
    jne .go
    jmp .out
.go:
    mov ax, [bp+6]
    mov bx, [bp+10]
    mov dx, LEM_VIEWH
    call lb_clipy
    jnc .r
    jmp .out
.r:
    ; screen bytes (x - view) >> 2 .. (x + w - 1 - view) >> 2, 0..79
    mov ax, [bp+4]
    sub ax, [lb_scroll]
    sar ax, 1
    sar ax, 1
    mov bx, [bp+4]
    add bx, [bp+8]
    dec bx
    sub bx, [lb_scroll]
    sar bx, 1
    sar bx, 1
    cmp ax, 0
    jge .c0
    xor ax, ax
.c0:
    cmp bx, LEM_SSTRIDE - 1
    jle .c1
    mov bx, LEM_SSTRIDE - 1
.c1:
    cmp bx, ax
    jl .out
    mov [lbp_bx0], ax
    mov [lbp_s], bx
    xor cx, cx
.row:
    cmp cx, [lbp_n]
    jae .out
    push cx
    mov ax, [bp+6]
    add ax, [lbp_r0]
    add ax, cx
    mov bx, [lbp_bx0]
    mov dx, [lbp_s]
    call lb_wcopy
    pop cx
    inc cx
    jmp .row
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_obj(unsigned seg, unsigned off, int x, int y, int w, int h,
;                int flags)
;
; ONE INTERACTIVE OBJECT FRAME into the background - VRAM on VGA, W on the
; shadow backends. A frame is FIVE planes, four colour and a mask
; (docs/lemband-format.md), and `flags` are Lemmix's odf_ bits as the level
; record carries them (Level.Loader.pas TranslateLevel):
;     1 only on terrain    the mask AND the solid mask under it
;     2 upside down        the rows flipped
;     4 no overwrite       the mask AND NOT the solid mask under it
; (Lemmix's no-overwrite also refuses pixels an earlier OBJECT drew; this port
; keeps no object layer, so it tests the terrain alone - SPEC.md 116.4.6.)
;
; On the shadow backends a pixel becomes its colour CLASS (lb_spix's rule):
; 1..7 solid, 8..15 the terrain class, and an opaque 0 black - built eight
; pixels at a time with bit operations, not a pixel at a time, because water
; and the exits' torches animate on every game frame.
; -----------------------------------------------------------------------------
_lem_r_obj:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    cld
    call lb_msvga                   ; the buttons, latched through a long
                                    ; compose (SPEC.md 116.7 point 3)
    mov ax, [bp+4]
    mov [lbp_seg], ax
    mov ax, [bp+6]
    mov [lbp_off], ax
    mov ax, [bp+14]
    mov [lbp_h], ax
    mov ax, [bp+12]
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov [lbp_bpr], ax
    mul word [bp+14]
    mov [lbp_ps], ax
    mov word [lbp_np], 4
    mov word [lbp_mp], 4
    mov word [lbp_ink], 0
    xor ax, ax
    test word [bp+16], 2
    jz .nf
    inc ax
.nf:
    mov [lbp_flip], ax
    xor ax, ax
    test word [bp+16], 1
    jz .nt
    mov ax, 1
    jmp .sf
.nt:
    test word [bp+16], 4
    jz .sf
    mov ax, 2
.sf:
    mov [lbp_sol], ax
    mov ax, [bp+10]
    mov bx, [bp+14]
    mov dx, LEM_VIEWH
    call lb_clipy
    jnc .r
    jmp .out
.r:
    cmp word [lb_kind], LEM_KIND_VGA
    je .vga
    jmp .w
.vga:
    cmp word [lb_vcomp], 0
    jne .vc
    mov ax, [bp+8]
    mov bx, [lbp_bpr]
    mov dx, LEM_MSTRIDE - 1
    call lb_clipx
    jnc .vx
    jmp .out
.vx:
    mov ax, [bp+10]
    add ax, [lbp_r0]
    call lem_rowoff
    add di, [lbp_bx0]
    add di, [lbp_i0]
    mov [lbp_solo], di
    add di, LEM_VTERR
    jmp .vgo
    ; --- composing (lem_r_cbegin): clipped to the compose rectangle and
    ;     drawn into the scratch at LEM_VCOMP, never onto the glass ----------
.vc:
    mov ax, [bp+10]
    sub ax, [lb_cy0]
    mov bx, [bp+14]
    mov dx, [lb_ch]
    call lb_clipy
    jnc .vcy
    jmp .out
.vcy:
    mov ax, [lb_cb0]
    mov cl, 3
    shl ax, cl
    neg ax
    add ax, [bp+8]                  ; x relative to the rectangle's byte 0
    mov bx, [lbp_bpr]
    mov dx, [lb_cb1]
    sub dx, [lb_cb0]
    call lb_clipx
    jnc .vcx
    jmp .out
.vcx:
    mov ax, [bp+10]
    add ax, [lbp_r0]
    call lem_rowoff
    add di, [lb_cb0]
    add di, [lbp_bx0]
    add di, [lbp_i0]
    mov [lbp_solo], di              ; the solid mask is the WORLD's, as ever
    add di, [lb_vbase]              ; ...and the destination the scratch's
.vgo:
    mov [lbp_dst], di
    call lb_vcore
    jmp .out
.w:
    mov ax, [bp+8]
    mov [lbp_x], ax
    mov ax, [bp+10]
    mov [lbp_y], ax
    call lb_wobj
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; lb_wobj - the shadow backends' half of lem_r_obj: into W, by class.
; Per clipped row k: for each source byte i (eight pixels), the mask m (with
; the terrain test), the four colour bytes, then
;     A = m & ~p3 & (p0|p1|p2)    the solid class
;     B = m & p3                  the terrain class
; CGA (2 bits a pixel, W 400 bytes a row): colour hi = A|B, lo = A, spread to
; bit pairs through lm_nib2 (hi) and lm_nib2>>1 (lo), the mask through lb_dbl.
; Hercules (W 200 bytes a row, 1bpp): out = A | (B & the 50% dither of that
; world row and column). Both are then shifted into place and merged
; d = (d & ~mm) | (c & mm).
lb_wobj:
    push bp
    mov bp, sp
    sub sp, 6                       ; [bp-2] k, [bp-4] i, [bp-6] dither
    cmp word [lb_kind], LEM_KIND_CGA
    jne .hx
    mov ax, [lbp_x]
    shl ax, 1
    mov bx, [lbp_bpr]
    shl bx, 1
    mov dx, 399
    call lb_clipx
    jnc .k0
    jmp .done
.hx:
    mov ax, [lbp_x]
    mov bx, [lbp_bpr]
    mov dx, LEM_MSTRIDE - 1
    call lb_clipx
    jnc .k0
    jmp .done
.k0:
    mov word [bp-2], 0
.row:
    mov ax, [bp-2]
    cmp ax, [lbp_n]
    jb .rgo
    jmp .done
.rgo:
    ; the dither for this row and the object's x: 0xAA where (x + row) is
    ; even, which is lem_m_derive's pattern seen from the object's own bits
    mov ax, [lbp_y]
    add ax, [lbp_r0]
    add ax, [bp-2]
    add ax, [lbp_x]
    mov dx, 0x00AA
    test al, 1
    jz .dok
    mov dx, 0x0055
.dok:
    mov [bp-6], dx
    mov ax, [bp-2]
    call lb_srcrow                  ; SI = plane 0 of the source row
    mov word [bp-4], 0
.byte:
    mov bx, [bp-4]
    cmp bx, [lbp_bpr]
    jb .bgo
    jmp .shift
.bgo:
    ; the mask byte and the four colour bytes
    push si
    add si, bx
    push ds
    mov ax, [cs:lbp_seg]
    mov ds, ax
    mov dx, [cs:lbp_ps]
    mov cl, [si]                    ; p0
    add si, dx
    mov ch, [si]                    ; p1
    add si, dx
    mov ah, [si]                    ; p2
    add si, dx
    mov al, [si]                    ; p3
    add si, dx
    mov dl, [si]                    ; the mask
    pop ds
    pop si
    ; the terrain test, against the solid mask under these eight pixels
    cmp word [lbp_sol], 0
    je .nosol
    push ax
    push cx
    push dx
    call lb_wsol                    ; AL = the solid bits, aligned to byte i
    pop dx
    pop cx
    cmp word [lbp_sol], 1
    je .sand
    not al
.sand:
    and dl, al
    pop ax
.nosol:
    ; A and B
    or cl, ch
    or cl, ah                       ; p0|p1|p2
    mov dh, al                      ; p3
    not al
    and al, cl
    and al, dl                      ; A = m & ~p3 & (p0|p1|p2)
    and dh, dl                      ; B = m & p3
    cmp word [lb_kind], LEM_KIND_CGA
    jne .hb
    ; CGA: two colour bytes and two mask bytes per source byte. hi = A | B
    ; carries each pixel's high bit and lo = A its low one, spread to bit
    ; pairs through lm_nib2 (the "10" of each pixel) and lm_nib2 >> 1 (its
    ; "01"); the mask is doubled through lb_dbl.
    mov [lbw_lo], al
    or al, dh
    mov [lbw_hi], al
    mov [lbw_m], dl
    mov di, [bp-4]
    shl di, 1                       ; 2i
    xor bh, bh
    mov bl, [lbw_hi]
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov al, [lm_nib2+bx]
    mov bl, [lbw_lo]
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov ah, [lm_nib2+bx]
    shr ah, 1
    or al, ah
    mov [lb_sorb+di], al
    mov bl, [lbw_hi]
    and bl, 15
    mov al, [lm_nib2+bx]
    mov bl, [lbw_lo]
    and bl, 15
    mov ah, [lm_nib2+bx]
    shr ah, 1
    or al, ah
    mov [lb_sorb+di+1], al
    mov bl, [lbw_m]
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov al, [lb_dbl+bx]
    mov [lb_sorb2+di], al
    mov bl, [lbw_m]
    and bl, 15
    mov al, [lb_dbl+bx]
    mov [lb_sorb2+di+1], al
    jmp .bnext
.hb:
    ; Hercules: one colour byte and one mask byte
    mov bx, [bp-4]
    mov ah, [bp-6]
    and dh, ah                      ; B on the dither
    or al, dh                       ; A | (B & dither)
    mov [lb_sorb+bx], al
    mov [lb_sorb2+bx], dl
.bnext:
    inc word [bp-4]
    jmp .byte
.shift:
    ; both rows shifted into place: colour into lb_srow, mask into lb_mrow
    push cs
    pop es
    mov cx, [lbp_s]
    mov si, lb_sorb
    add si, [lbp_i0]
    mov di, lb_srow
    call lb_shrow
    mov cx, [lbp_s]
    mov si, lb_sorb2
    add si, [lbp_i0]
    mov di, lb_mrow
    call lb_shrow
    ; ...and merged into W at the destination row and byte
    mov ax, [lbp_y]
    add ax, [lbp_r0]
    add ax, [bp-2]
    call lem_rowoff                 ; DI = row * 200
    cmp word [lb_kind], LEM_KIND_CGA
    jne .wh
    shl di, 1
.wh:
    add di, [lbp_bx0]
    add di, [lbp_i0]
    mov ax, [lb_world]
    mov es, ax
    mov si, lb_srow
    mov bx, lb_mrow
    mov cx, [lbp_cnt]
.mg:
    mov ah, [bx]
    mov al, [si]
    and al, ah
    not ah
    and ah, [es:di]
    or al, ah
    mov [es:di], al
    inc si
    inc bx
    inc di
    loop .mg
    inc word [bp-2]
    jmp .row
.done:
    add sp, 6
    pop bp
    ret

; lb_wsol - the solid mask's bits under source byte [bp-4] of the row being
; drawn, aligned to the SOURCE: world pixels x + 8i .. x + 8i + 7 of the
; destination row. out: AL. Reads the caller's frame. Clobbers AX, BX, CX,
; DX, DI, ES.
lb_wsol:
    mov ax, [lbp_y]
    add ax, [lbp_r0]
    add ax, [bp-2]
    call lem_rowoff                 ; DI = the mask row
    mov ax, [lm_seg]
    mov es, ax
    mov ax, [lbp_x]
    mov cx, ax
    and cx, 7
    sar ax, 1
    sar ax, 1
    sar ax, 1
    add ax, [bp-4]                  ; the mask byte holding pixel x + 8i
    xor bx, bx                      ; BX = (mask[k] << 8) | mask[k+1]
    cmp ax, 0
    jl .k1
    cmp ax, LEM_MSTRIDE
    jge .k1
    push di
    add di, ax
    mov bh, [es:di]
    pop di
.k1:
    inc ax
    cmp ax, 0
    jl .sh
    cmp ax, LEM_MSTRIDE
    jge .sh
    add di, ax
    mov bl, [es:di]
.sh:
    shl bx, cl
    mov al, bh
    ret

; -----------------------------------------------------------------------------
; void lem_r_glyph8(const unsigned char *bits, int x, int y, int h, int ink,
;                   int paper)
;
; An 8-wide, h-tall one-bit bitmap at PANEL (x, y), OPAQUE: a set bit is
; `ink` and a clear one `paper`. It is the skill counts' two-digit cell
; (Lemmix Game.SkillPanel.pas DrawSkillCount: the left half of the left digit
; and the right half of the right one, OR'd), at x = button * 16 + 4, which is
; never byte aligned - so it goes through lb_rpix inside one batch rather than
; through a byte copy. 64 plots, a count a button press.
; -----------------------------------------------------------------------------
_lem_r_glyph8:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    call lb_batch_in
    mov si, [bp+4]
    xor dx, dx                      ; the row
.row:
    cmp dx, [bp+10]
    jge .out
    mov ah, [si]
    xor di, di                      ; the column
.col:
    mov al, [bp+14]                 ; paper
    test ah, 0x80
    jz .plot
    mov al, [bp+12]                 ; ink
.plot:
    push ax
    push dx
    push di
    mov bx, [bp+6]
    add bx, di
    mov cx, [bp+8]
    add cx, dx
    call lb_rpix                    ; the shadow arm takes DI and DX with it
    pop di
    pop dx
    pop ax
    shl ah, 1
    inc di
    cmp di, 8
    jb .col
    inc si
    inc dx
    jmp .row
.out:
    call lb_batch_out
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void lem_r_panrect(unsigned seg, unsigned off, int x, int y, int w, int h)
;
; THE OUTLINE of a panel rectangle put back out of the panel's own bitmap
; (MAIN section 6, the lem_r_panel source) - which is how the original takes
; a skill's selection frame down (Lemmix Game.SkillPanel.pas
; DrawButtonSelector: "remove selector by copying the original", its four
; edges). The frame layout is lem_r_panel's, because lb_pnib reads it:
; [bp+4] seg, [bp+6] off, [bp-2] row, [bp-6] column.
; -----------------------------------------------------------------------------
_lem_r_panrect:
    push bp
    mov bp, sp
    sub sp, 6
    push si
    push di
    push es
    push ds
    call lb_batch_in
    mov word [bp-4], 0              ; the row within the rectangle
.row:
    mov ax, [bp-4]
    cmp ax, [bp+14]
    jl .rgo
    jmp .out
.rgo:
    mov ax, [bp+10]
    add ax, [bp-4]
    mov [bp-2], ax                  ; the panel row lb_pnib reads
    xor si, si                      ; the column within the rectangle
.col:
    cmp si, [bp+12]
    jge .rnext
    ; an outline: the first and last rows whole, the rest their two ends
    mov ax, [bp-4]
    cmp ax, 0
    je .plot
    mov bx, [bp+14]
    dec bx
    cmp ax, bx
    je .plot
    cmp si, 0
    je .plot
    mov bx, [bp+12]
    dec bx
    cmp si, bx
    je .plot
    mov si, bx                      ; the interior is skipped, not walked
    jmp .col
.plot:
    mov ax, [bp+8]
    add ax, si
    mov [bp-6], ax
    call lb_pnib                    ; AL = the panel's nibble there
    mov bx, [bp-6]
    mov cx, [bp-2]
    call lb_rpix
    inc si
    jmp .col
.rnext:
    inc word [bp-4]
    jmp .row
.out:
    call lb_batch_out
    pop ds
    pop es
    pop di
    pop si
    add sp, 6
    pop bp
    ret

section .bss
lbp_seg:    resw 1                  ; the draw's source segment...
lbp_off:    resw 1                  ; ...plane 0, row 0
lbp_bpr:    resw 1                  ; source bytes a row
lbp_ps:     resw 1                  ; one plane's bytes
lbp_np:     resw 1                  ; colour planes (1 = one plane in lbp_ink)
lbp_mp:     resw 1                  ; 0 = the mask is the OR of the colour
                                    ; planes, else the plane that holds it
lbp_ink:    resw 1
lbp_flip:   resw 1                  ; the source rows upside down
lbp_sol:    resw 1                  ; 0, 1 only-on-terrain, 2 no-overwrite
lbp_h:      resw 1                  ; the source's whole height (the flip)
lbp_r0:     resw 1                  ; the first clipped source row
lbp_n:      resw 1                  ; ...and how many
lbp_s:      resw 1                  ; the shift
lbp_bx0:    resw 1                  ; the first destination byte, unclipped
lbp_i0:     resw 1                  ; the first source byte index kept
lbp_main:   resw 1                  ; bytes with a source byte of their own
lbp_tail:   resw 1                  ; 1 = the shift's carry byte is kept too
lbp_cnt:    resw 1                  ; main + tail
lbp_dst:    resw 1                  ; the destination of row r0, byte i0
lbp_solo:   resw 1                  ; ...and the solid mask's, the same place
lbp_how:    resw 1                  ; lb_vcore's per-plane case
lbp_x:      resw 1                  ; lb_wobj's world position
lbp_y:      resw 1
lb_vcomp:   resw 1                  ; 1 = composing (lem_r_cband)
lb_vbase:   resw 1                  ; ...the scratch's base for row 0
lb_by0:     resw 1                  ; ...the band: world rows
lb_bh:      resw 1
lb_cy0:     resw 1                  ; ...and the clip: world rows
lb_ch:      resw 1
lb_cb0:     resw 1                  ; ...and bytes, inclusive
lb_cb1:     resw 1
lbp_vb:     resw 1                  ; lem_r_bgrect's destination base
lb_spn:     resw 1                  ; sprite records in use (shadow cards)
lb_cur0:    resw 1                  ; the pointer's save: VGA piece 0's VRAM
lb_cur1:    resw 1                  ; offset and piece 1's (shadow: the first
lb_curc0:   resw 1                  ; row and the first byte), the bytes a row
lb_curc1:   resw 1                  ; and the rows of each
lb_curn0:   resw 1
lb_curn1:   resw 1
lb_curr:    resw 1                  ; VGA: piece 0's first world row and byte
lb_curb:    resw 1
lb_curbits: resw 1                  ; ...and the shape and the place it is lit
lb_cursx:   resw 1                  ; at (lem_r_cdraw)
lb_cursy:   resw 1
lb_cnod:    resw 1                  ; 1 = lem_r_cplace: place, do not draw
lb_cursv:   resb 14 * 5             ; the shadow backends' pointer save-under
_lem_sprec:                         ; THE SAME BYTES TWICE, ONE CARD AT A TIME:
lb_sprec:   resb LEM_SPMAX * LEM_SPREC  ; the shadow cards' sprite records,
                                    ; and on VGA - which keeps no records -
                                    ; the C's last-drawn lemming boxes
                                    ; (lemdraw.c lem_obox, 80 x 4 = 320 of
                                    ; these 1,008), so the VGA compose's own
                                    ; bookkeeping costs no bss of its own
%if LEM_SPMAX * LEM_SPREC < 80 * 4
  %fatal "lb_sprec no longer holds lemdraw.c's 80 four-byte boxes"
%endif                              ; ...and the 80 is lemgame.c's LEM_MAXLEM,
                                    ; which this file cannot see: the host
                                    ; harness reads LEM_SPMAX and LEM_SPREC
                                    ; out of THIS file and checks them against
                                    ; the C's own number (lemtest.c
                                    ; check_sprec), so neither moves alone
lb_pbins    equ lb_sprec + 80 * 4   ; lem_r_spray's bins (erase 2, 3), VGA
                                    ; only, after the 80 boxes in the same
                                    ; records the VGA keeps none of: no bss
%if LEM_SPMAX * LEM_SPREC < 80 * 4 + LEM_PBINS * 4
  %fatal "lb_sprec no longer holds the 80 boxes and the spray's bins"
%endif
lb_smk:     resb LEM_SMK            ; one draw's mask rows (lb_vcore)
lbw_lo:     resb 1                  ; lb_wobj's three bytes of one source
lbw_hi:     resb 1                  ; byte, kept in memory because the
lbw_m:      resb 1                  ; CGA arm needs more than four registers
            resb 1
lb_sorb:    resb 24                 ; one source row, OR'd or classed (and
lb_sorb2:   resb 24                 ; doubled): 10 bytes of the widest object,
lb_srow:    resb 24                 ; 20 of it doubled, 21 shifted - so 24
section .text
