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
; unsigned pdu_read_at_seg(const char *name, unsigned seg, unsigned cap,
;                          unsigned off_lo, unsigned off_hi)
; os88_file_read_at() with the buffer in a CLAIM rather than in our segment:
; OSAPI_FILE_READ_AT takes ES:BX. The base is a claim's (512-aligned, SPEC.md
; 2.1.1); off and cap are whole clusters (18.4.4). Answers the bytes, 0 at the
; end or refused - os88_ferr() says which, as the SDK thunk's does.
; -----------------------------------------------------------------------------
_pdu_read_at_seg:
    push bp
    mov bp, sp
    push si
    push es
    mov si, [bp+4]
    mov es, [bp+6]
    xor bx, bx
    mov cx, [bp+8]
    mov ax, [bp+10]
    mov dx, [bp+12]
    call OSAPI_FILE_READ_AT
    sbb cx, cx
    and cx, ax
    mov [cc_ferr], cx
    sub ax, cx
    pop es
    pop si
    pop bp
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
