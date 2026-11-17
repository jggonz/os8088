; =============================================================================
; os8088 - apps/pagedesk/pdui.inc     PageDesk's hand-written seam
;                                     (PAGEDESK-SPEC 3.1, 20.6)
;
; %included by apps/pagedesk/pagedesk.asm AFTER the compiled C (the C names
; these labels and declares the two words the module call answers into) and
; AFTER os88ui.inc (the scroll-bar bridge calls into it). Everything here is
; here because C cannot say it: a far call through a vector, a segment
; register, a parts-table row, an nasm label.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; AX the answer;
; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to lose
; (apps/loom/lmui.inc's convention, which the compiled C relies on).
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; void pdu_mbind(int mod, unsigned seg) - stamp module `mod`'s door into its
; far vector, or forget it with seg = 0. The OFFSET is PDM_ENTRY (8), the
; header's fixed size (PAGEDESK-SPEC 3.1). On kern_mega the segment is turned
; into its CODE selector (apps/loom/lmpv.inc's reason): the part's data
; selector cannot be far-called.
; -----------------------------------------------------------------------------
_pdu_mbind:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    shl bx, 1
    shl bx, 1                       ; 4 bytes a vector
    mov ax, [bp+6]
    or ax, ax
    jz .set
    OS88_CODESEL ax, byte [cc_cpuf]
.set:
    mov [pdu_vec + bx + 2], ax
    mov word [pdu_vec + bx], 8      ; PDM_ENTRY
    pop bp
    ret

; unsigned pdu_mvecseg(int mod) - the segment half of module `mod`'s bound
; vector (already a code selector on kern_mega): what pdxb_init.eng_seg hands
; PDREND and PDWRT so they can far-call PDENG themselves (3.3).
_pdu_mvecseg:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    shl bx, 1
    shl bx, 1
    mov ax, [pdu_vec + bx + 2]
    pop bp
    ret

; unsigned pdu_mcall(...) - the one call into a module - is pdstk.inc's: it
; runs every verb on the private module stack (PAGEDESK-SPEC 20.15, D23).

; -----------------------------------------------------------------------------
; unsigned pdu_part_off(int i) - part i's offset in the package FILE, in
; PARAGRAPHS (os88pkg.py's `off`, SPEC.md 20.12.2.1); 0 for a scratch row.
; unsigned pdu_part_len(int i) - its UNPACKED length in bytes.
; unsigned pdu_part_flags(int i) - its flags byte (OP_*).
; -----------------------------------------------------------------------------
_pdu_part_off:
    mov bx, OP_R_OFF
    jmp short pdu_rowword
_pdu_part_len:
    mov bx, OP_R_LEN
    jmp short pdu_rowword
_pdu_part_flags:
    mov bx, OP_R_FLAG
pdu_rowword:
    push bp
    mov bp, sp
    push si
    mov al, [bp+4]
    call op_row                     ; SI -> the row, AX kept
    mov ax, [si+bx]
    cmp bx, OP_R_FLAG
    jne .w
    xor ah, ah
.w:
    pop si
    pop bp
    ret

; char *pdu_pkgname(void) - the file this instance came out of (op_load
; banked it at launch, SPEC.md 20.12 rule 1): what a lazy OP_SELF part is
; read out of.
_pdu_pkgname:
    mov ax, op_name
    ret

; -----------------------------------------------------------------------------
; int pdu_clk(unsigned *w) - OSAPI_CLK_GET (SPEC.md 37) into a STATIC
; four-word array: w[0] year, w[1] month<<8 | day, w[2] hour<<8 | minute,
; w[3] second. Answers 1 when a hardware RTC was found, 0 when the clock is
; the PIT's since boot and may never have been set - a save's /CreationDate
; is then left empty (pdxb_save.date "", PAGEDESK-SPEC 20.6) rather than
; stamped 1980.
; -----------------------------------------------------------------------------
_pdu_clk:
    push bp
    mov bp, sp
    push si
    call OSAPI_CLK_GET
    mov si, [bp+4]
    mov [si], cx
    mov [si+2], dx
    mov [si+4], bx
    mov [si+6], ah
    mov byte [si+7], 0
    and ax, 1
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; unsigned pdu_muldiv(unsigned a, unsigned b, unsigned c) - a * b / c with a
; 32-bit intermediate, unsigned; 0xFFFF when the quotient does not fit and 0
; for c = 0. Layout arithmetic only (thumbnail cells, scroll-bar ratios) -
; page geometry goes through pdmath (PAGEDESK-SPEC 5.3).
; -----------------------------------------------------------------------------
_pdu_muldiv:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mul word [bp+6]
    mov cx, [bp+8]
    jcxz .zero
    cmp dx, cx
    jae .big
    div cx
    jmp short .out
.zero:
    xor ax, ax
    jmp short .out
.big:
    mov ax, 0xFFFF
.out:
    pop bp
    ret

; -----------------------------------------------------------------------------
; void pdu_ctl_paint_a(int i, struct pdu_ctl *c) - one control of the layout
; (PAGEDESK-SPEC 20.21.3-4), pdudraw.c's pdu_ctl_paint in assembly: that C was
; 1,692 bytes, the largest function the face added, and 20.21.9 named it the
; first room to take back (PAGEDESK-SPEC 20.22.2.9). The C keeps a one-line
; shim that passes &pdu_L.ctl[i]; the logic, the cache key (pdu_cid/cx1/cy1/
; cst/clab) and every call it makes are the C's, in the same order. Its locals
; are static (the click chain's depth, D22), every call is through the C SDK's
; thunks and the C's own helpers - which may lose AX..DI and ES - so nothing
; is kept in a register across one. ES is put back: C never touches it, but a
; callback's ES is the kernel's (SPEC.md 20).
; -----------------------------------------------------------------------------
PDU_CF_FIELD    equ 1
PDU_CF_EMPTY    equ 16
PDU_CF_SEP      equ 32
PDU_CF_FLAT     equ 64
PDU_CF_ICON     equ 128
PDU_CF_NOCAP    equ 256
PDU_CF_DROP     equ 512
PDU_CF_LABEL    equ 1024

_pdu_ctl_paint_a:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [bp+4]
    mov [cp_i], ax
    mov si, [bp+6]
    mov [cp_c], si
    mov ax, [si+10]
    mov [cp_fl], ax
    test al, PDU_CF_SEP
    jz .ctl
    cmp word [_pdu_cground], 0      ; the etched rule: with its ground
    jl .ret
    push word [_pdu_clo]
    call _os88_set_color
    pop cx
    mov bx, 2
    call .rule
    push word [_pdu_chi]
    call _os88_set_color
    pop cx
    mov bx, 6
    call .rule
    jmp .ret

.rule:                              ; vline at x = c[BX], y1 + 1 .. y2 - 1
    mov si, [cp_c]
    mov ax, [si+8]
    dec ax
    push ax
    mov ax, [si+4]
    inc ax
    push ax
    push word [si+bx]
    call _os88_gfx_vline
    add sp, 6
    ret

.ctl:
    xor ax, ax
    cmp word [_pdu_L], 2            ; PDU_L_COMPACT
    jne .c0
    inc ax
.c0:
    mov [cp_comp], ax
    push word [si]
    call _pdu_label
    pop cx
    mov [cp_s], ax
    mov si, [cp_c]
    push word [si]
    call _pdu_active
    pop cx
    neg ax
    sbb ax, ax
    neg ax
    mov [cp_act], ax
    mov si, [cp_c]
    push word [si]
    call _pdu_why
    pop cx
    neg ax
    sbb ax, ax
    neg ax                          ; dis: a reason is a disabled control
    mov [cp_dis], ax
    mov si, [cp_c]
    xor dx, dx                      ; foc: where Tab is (PD-23), enabled
    mov bx, [si]
    cmp bx, [_pdu_focus]
    jne .f0
    or ax, ax
    jnz .f0
    inc dx
.f0:
    mov [cp_foc], dx
    mov ax, [_pdu_cbar]             ; the ground it stands on
    test byte [cp_fl], PDU_CF_EMPTY
    jz .g0
    mov ax, 15                      ; OS88_WHITE
.g0:
    mov [cp_gnd], ax
    xor ax, ax                      ; its picture, with the kit
    test byte [cp_fl], PDU_CF_ICON
    jz .i0
    call _pk_haskit
    or ax, ax
    jz .i0
    mov si, [cp_c]
    mov bx, [si]
    mov al, [_pdu_cico + bx]
    xor ah, ah
.i0:
    mov [cp_ico], ax
    ; the look: 0 label, 1 field, 2 flat tool, 3 raised, 4 plain (COMPACT)
    mov ax, 4
    cmp word [cp_comp], 0
    jne .l0
    mov bx, [cp_fl]
    xor ax, ax
    test bh, PDU_CF_LABEL >> 8
    jnz .l0
    inc ax
    test bl, PDU_CF_FIELD
    jnz .l0
    inc ax
    test bl, PDU_CF_FLAT
    jnz .l0
    inc ax
.l0:
    mov [cp_look], ax
    mov cx, [cp_gnd]                ; paper and ink
    cmp ax, 1
    jne .p0
    mov cx, 15
.p0:
    xor dx, dx                      ; OS88_BLACK
    cmp word [cp_act], 0
    je .p1
    cmp word [cp_dis], 0
    jne .p1
    or ax, ax
    jz .p1
    mov cx, [_pdu_cact]
    mov dx, 15
.p1:
    cmp word [cp_dis], 0
    je .p2
    cmp word [_pdu_mono], 0
    jne .p2
    mov dx, 8                       ; OS88_DGRAY
.p2:
    mov [cp_pap], cx
    mov [cp_ink], dx
    mov bx, 1                       ; the inset: 2 raised, 0 label, else 1
    cmp ax, 3
    jne .n0
    inc bx
.n0:
    or ax, ax
    jnz .n1
    dec bx
.n1:
    mov [cp_in], bx
    ; the cache key: 1 | act 2 | dis 4 | foc 8 | paper << 5 | ink << 9
    mov ax, dx
    mov cl, 4
    shl ax, cl
    or ax, [cp_pap]
    inc cx
    shl ax, cl
    or al, 1
    mov bx, [cp_act]
    shl bx, 1
    or ax, bx
    mov bx, [cp_dis]
    shl bx, 1
    shl bx, 1
    or ax, bx
    mov bx, [cp_foc]
    mov cl, 3
    shl bx, cl
    or ax, bx
    mov [cp_st], ax
    ; the caption, cut to the inside
    xor ax, ax                      ; PK_CAP
    cmp word [cp_comp], 0
    je .k0
    mov al, 3                       ; PK_SYS
.k0:
    mov [cp_f], ax
    push ax
    call _pk_adv
    pop cx
    mov [cp_adv], ax
    xor ax, ax
    test word [cp_fl], PDU_CF_NOCAP
    jz .s0
    cmp word [cp_ico], 0
    jne .s1
.s0:
    push word [cp_s]
    call _os88_strlen
    pop cx
.s1:
    mov [cp_n], ax
    mov si, [cp_c]
    mov ax, [si+6]
    sub ax, [si+2]
    inc ax
    sub ax, [cp_in]
    sub ax, [cp_in]
    cmp word [cp_comp], 0
    jne .w0
    sub ax, 4
.w0:
    cmp word [cp_ico], 0
    je .w1
    sub ax, 20
.w1:
    test word [cp_fl], PDU_CF_DROP
    jz .w2
    sub ax, [cp_adv]
    sub ax, 4
.w2:
    cwd
    idiv word [cp_adv]              ; max, toward zero as C's /
    or ax, ax
    jns .m0
    xor ax, ax
.m0:
    cmp ax, 15
    jbe .m1
    mov ax, 15
.m1:
    mov cx, [cp_n]
    cmp cx, ax
    jbe .m2
    mov cx, ax
.m2:
    mov [cp_n], cx
    push ds
    pop es
    cld
    mov si, [cp_s]
    mov di, _pdu_cut
    rep movsb
    mov byte [di], 0
    ; the same as last time? (id, x1, y1, look) and then the caption
    mov bx, [cp_i]
    shl bx, 1
    mov si, [cp_c]
    xor dx, dx
    mov ax, [si]
    cmp ax, [_pdu_cid + bx]
    jne .q0
    mov ax, [si+2]
    cmp ax, [_pdu_cx1 + bx]
    jne .q0
    mov ax, [si+4]
    cmp ax, [_pdu_cy1 + bx]
    jne .q0
    mov ax, [cp_st]
    cmp ax, [_pdu_cst + bx]
    jne .q0
    inc dx
.q0:
    cmp word [_pdu_cground], 0
    jl .q1
    xor dx, dx                      ; a fresh ground: all of it
    jmp short .frame
.q1:
    or dx, dx
    jz .frame
    call .clab                      ; DI = its slot, CX = n + 1
    mov si, _pdu_cut
    repe cmpsb
    je .ret                         ; nothing changed
    mov dx, 2                       ; the band alone
.frame:
    or dx, dx
    jnz .band
    cmp word [cp_look], 0
    je .band
    cmp word [cp_dis], 0
    je .d0
    mov ax, 1
    push ax
    call _os88_gfx_pen
    jmp short .d1
.d0:
    xor ax, ax
    push ax
    call _os88_set_color
.d1:
    pop cx
    mov ax, [cp_act]
    or ax, [cp_foc]
    mov [cp_af], ax
    cmp word [cp_look], 2           ; an idle tool has no frame
    jne .d3
    or ax, ax
    jnz .d3
    push word [cp_gnd]
    call _os88_set_color
    pop cx
    cmp word [_pdu_cground], 0
    jge .d4
.d3:
    mov si, [cp_c]
    push word [si+8]
    push word [si+6]
    push word [si+4]
    push word [si+2]
    call _os88_gfx_frame
    add sp, 8
.d4:
    cmp word [cp_dis], 0
    je .d5
    xor ax, ax
    push ax
    call _os88_gfx_pen
    pop cx
.d5:
    cmp word [cp_look], 3
    jne .band
    mov ax, [_pdu_chi]
    mov dx, [_pdu_clo]
    cmp word [cp_act], 0
    je .d6
    mov ax, dx
    mov dx, [cp_pap]
.d6:
    push dx
    push ax
    mov si, [cp_c]
    push word [si+8]
    push word [si+6]
    push word [si+4]
    push word [si+2]
    call _pdu_bevel
    add sp, 12
.band:
    mov si, [cp_c]
    mov bx, [cp_in]
    mov ax, [si+2]
    add ax, bx
    mov [_pdu_bc_x], ax
    mov ax, [si+4]
    add ax, bx
    mov [_pdu_bc_y], ax
    shl bx, 1
    mov ax, [si+6]
    sub ax, [si+2]
    inc ax
    sub ax, bx
    mov [_pdu_bc_w], ax
    mov ax, [si+8]
    sub ax, [si+4]
    inc ax
    sub ax, bx
    mov [_pdu_bc_h], ax
    mov word [_pdu_bc_cut], _pdu_cut
    mov ax, [cp_fl]
    cmp word [cp_comp], 0
    je .b0
    xor ax, ax
.b0:
    mov [_pdu_bc_fl], ax
    mov ax, [cp_ico]
    mov [_pdu_bc_ico], ax
    mov ax, [cp_ink]
    mov [_pdu_bc_ink], ax
    mov ax, [cp_pap]
    mov [_pdu_bc_pap], ax
    mov ax, [cp_dis]
    mov [_pdu_bc_dis], ax
    xor ax, ax
    cmp word [cp_look], 0
    je .b1
    mov ax, [cp_foc]
.b1:
    mov [_pdu_bc_foc], ax
    mov word [_pdu_bc_dxy], 0
    mov ax, [cp_f]
    mov [_pdu_bc_f], ax
    call _pdu_bandcap
    mov bx, [cp_i]                  ; remember what it shows
    shl bx, 1
    mov si, [cp_c]
    mov ax, [si]
    mov [_pdu_cid + bx], ax
    mov ax, [si+2]
    mov [_pdu_cx1 + bx], ax
    mov ax, [si+4]
    mov [_pdu_cy1 + bx], ax
    mov ax, [cp_st]
    mov [_pdu_cst + bx], ax
    call .clab
    mov si, _pdu_cut
    rep movsb
.ret:
    pop es
    pop di
    pop si
    pop bp
    ret

.clab:                              ; DI = pdu_clab[i], CX = n + 1, ES = DS
    mov di, [cp_i]
    mov cl, 4
    shl di, cl
    add di, _pdu_clab
    mov cx, [cp_n]
    inc cx
    push ds
    pop es
    cld
    ret

section .bss
alignb 2
cp_i:       resw 1
cp_c:       resw 1
cp_fl:      resw 1
cp_comp:    resw 1
cp_s:       resw 1
cp_act:     resw 1
cp_dis:     resw 1
cp_foc:     resw 1
cp_af:      resw 1
cp_gnd:     resw 1
cp_ico:     resw 1
cp_look:    resw 1
cp_pap:     resw 1
cp_ink:     resw 1
cp_in:      resw 1
cp_st:      resw 1
cp_f:       resw 1
cp_adv:     resw 1
cp_n:       resw 1
section .text

; -----------------------------------------------------------------------------
; void pdu_bandcap(void) - pdudraw.c's caption band in assembly (20.21.3; its C
; was 542 bytes, PAGEDESK-SPEC 20.22.2.9): compose the caption, picture, drop
; mark and focus ring that the pdu_bc_* statics describe into ONE band and put
; it down with one BLIT1. Same calls, same order, same arithmetic (C's /2
; truncates toward zero: cwd/sub/sar).
; -----------------------------------------------------------------------------
_pdu_bandcap:
    mov ax, [_pdu_bc_w]
    or ax, ax
    jle .out
    cmp word [_pdu_bc_h], 0
    jle .out
    push word [_pdu_bc_x]
    push word [_pdu_bc_h]
    push ax
    call _pk_begin
    add sp, 6
    or ax, ax
    jz .out
    push word [_pdu_bc_f]
    call _pk_adv
    pop cx
    mov [bcp_adv], ax
    push word [_pdu_bc_cut]
    call _os88_strlen
    pop cx
    mov cx, ax                      ; n
    mul word [bcp_adv]              ; cw = n * adv
    cmp word [_pdu_bc_ico], 0
    je .c0
    add ax, 16
    jcxz .c0
    add ax, 4
.c0:
    test word [_pdu_bc_fl], PDU_CF_DROP
    jz .c1
    add ax, [bcp_adv]
    add ax, 4
.c1:
    neg ax
    add ax, [_pdu_bc_w]             ; (w - cw) / 2 + dxy, at least 0
    call .half
    jns .c2
    xor ax, ax
.c2:
    mov [bcp_x], ax
    cmp word [_pdu_bc_ico], 0
    je .c3
    mov ax, [_pdu_bc_ico]
    dec ax
    push ax
    mov ax, [_pdu_bc_h]
    sub ax, 16
    call .half
    push ax
    push word [bcp_x]
    call _pk_icon
    add sp, 6
    add word [bcp_x], 20
.c3:
    push word [_pdu_bc_f]
    call _pk_rows
    pop cx
    neg ax
    add ax, [_pdu_bc_h]
    call .half
    mov [bcp_ty], ax
    push word [_pdu_bc_f]
    push word [_pdu_bc_cut]
    push ax
    push word [bcp_x]
    call _pk_text                   ; (bx, ty, cut, f): AX = the pen after
    add sp, 8
    test word [_pdu_bc_fl], PDU_CF_DROP
    jz .c4
    mov word [_pdu_lbuf], 0x001F    ; the down triangle (20.21.2), NUL
    push word [_pdu_bc_f]
    mov dx, _pdu_lbuf
    push dx
    push word [bcp_ty]
    add ax, 4
    push ax
    call _pk_text
    add sp, 8
.c4:
    cmp word [_pdu_bc_foc], 0       ; PD-23: where Tab is, a ring
    je .c6
    mov ax, [_pdu_bc_w]
    dec ax
    mov [bcp_x], ax                 ; w - 1
    xor ax, ax
    call .hl0                       ; the top row...
    mov ax, [_pdu_bc_h]
    dec ax
    mov [bcp_ty], ax
    call .hl0                       ; ...the bottom, and the sides between
.c5:
    dec word [bcp_ty]
    jle .c6
    push word [bcp_ty]
    xor ax, ax
    push ax
    push ax
    call _pk_hline
    add sp, 6
    push word [bcp_ty]
    push word [bcp_x]
    push word [bcp_x]
    call _pk_hline
    add sp, 6
    jmp short .c5
.c6:
    xor ax, ax                      ; grey = dis && mono
    cmp word [_pdu_bc_dis], 0
    je .c7
    mov ax, [_pdu_mono]
.c7:
    push ax
    push word [_pdu_bc_pap]
    push word [_pdu_bc_ink]
    push word [_pdu_bc_y]
    call _pk_pen
    add sp, 8
    or ax, ax
    jz .out
    push word [pk_h]
    push word [pk_w]
    push word [_pdu_bc_y]
    push word [_pdu_bc_x]
    push word [pk_st]
    xor ax, ax
    push ax
    push word [pk_bseg]
    call _os88_gfx_blit1_seg
    add sp, 14
    call _pk_pen0
.out:
    ret

.half:                              ; AX = AX / 2 (toward zero) + dxy; flags
    cwd
    sub ax, dx
    sar ax, 1
    add ax, [_pdu_bc_dxy]
    ret

.hl0:                               ; pk_hline(0, w - 1, AX)
    push ax
    push word [bcp_x]
    xor ax, ax
    push ax
    call _pk_hline
    add sp, 6
    ret

section .bss
alignb 2
bcp_adv:    resw 1
bcp_x:      resw 1
bcp_ty:     resw 1
section .text

; -----------------------------------------------------------------------------
; THE SCROLL BAR (SPEC.md 13.10, os88ui.inc's OS88UI_SCROLL) - apps/loom/
; lmui.inc's three bridges, verbatim in shape. `blk` is the seven-word block:
; x1 y1 x2 y2 (absolute, inclusive), total, fit, pos.
; -----------------------------------------------------------------------------
_pdu_sbar:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    call os88ui_sbar
    pop bx
    pop bp
    ret

_pdu_sbmove:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov ax, [bp+6]
    call os88ui_sbmove
    pop bx
    pop bp
    ret

_pdu_sbhit:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov bx, [bp+4]
    mov cx, [bp+6]
    mov dx, [bp+8]
    call os88ui_sbhit
    xor ah, ah
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; nav: the thumb's drag (os88ui.inc's OS88UI_SBDRAG, SPEC.md 13.10.5). Rate 0 on
; an 8086 - the view follows only at the release, because one commit is a
; third of a second there and a thumb that waits is better than a page that
; lags - and every 2 ticks on a 286 or better.
_pdu_sbgrab:                    ; int pdu_sbgrab(int *blk, int y): 1 = live
    push bp
    mov bp, sp
    push bx
    push dx
    mov bx, [bp+4]
    mov dx, [bp+6]
    mov ax, 0200h
    call os88ui_sbrate
    call os88ui_sbgrab
    mov ax, 0
    jc .out
    inc ax
.out:
    pop dx
    pop bx
    pop bp
    ret

_pdu_sbtrack:                   ; int pdu_sbtrack(int *blk, int y): the pos owed, or -1
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    mov bx, [bp+4]
    mov dx, [bp+6]
    call os88ui_sbtrack
    jnc .out
    mov ax, -1
.out:
    pop dx
    pop cx
    pop bx
    pop bp
    ret

_pdu_sbdrop:                    ; int pdu_sbdrop(int *blk): the final pos, or -1
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    call os88ui_sbdrop
    jnc .out
    mov ax, -1
.out:
    pop bx
    pop bp
    ret

section .data
pdu_vec:    times 4 dw 0, 0         ; (offset, segment) per module: PDENG,
                                    ; PDREND, PDWRT, and a spare. Segment 0 =
                                    ; not bound
section .text
