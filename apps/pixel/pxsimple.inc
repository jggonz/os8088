; =============================================================================
; os8088 - apps/pixel/pxsimple.inc
;
; THE FORMATS' FRONT DOOR, resident (SPEC.md 106.10, 106.18-106.20): the
; header's dispatch to the decoder PART that reads the format - since wave 4
; every one has a part, the simple five (BMP, PCX, TGA, PNM, PIX) a part of
; their own LINKED against the package (apps/pixel/pxsimp.asm) - and the
; resident halves of the part protocol: px_hpart (HEAD, and a PNG inside an
; ICO handed on) and px_dpart (DECODE on the worker). A next head is read by
; the part that wants it (apps/pixel/pxhmore.inc, SPEC.md 106.25).
;
; Every answer is CF = 0, or CF = 1 with AX = a PXD_*. A part never speaks
; and neither does this file: px_refusal (pxpump.inc) words the number.
; =============================================================================

; (DF_* are apps/pixel/pxrec.inc's: the SIMPLE part reads them too)

; =============================================================================
; px_hdrparse - the header of [px_cur]'s file, from the head. The format is
; the sniff's (106.6). out CF = 0 and the decode set up; CF = 1 AX = PXD_*
; =============================================================================
px_hdrparse:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; [px_spal] := black, so a palette shorter
    mov di, px_spal                 ; than 256 reads black past its end
    mov cx, 768 / 2
    xor ax, ax
    cld
    rep stosw
    mov [px_drawsz], ax
    mov [px_dlinsz], ax
    mov [px_dtop], al
    mov [px_bgr], al                ; R, G, B unless a reader says not
    mov [px_cur + PXR_PACK], al
    mov [px_cur + PXR_NPAL], ax
    mov [px_dsc], ax                ; no decoder scratch...
    mov [px_dsc + 2], ax
    mov [px_dsc + 4], ax
    mov [px_dsc + 6], ax
    mov [px_k + PXK_RBLK], al       ; ...every row of a block summed...
    mov [px_k + PXK_FLAGS], al      ; ...and rows at the source's scale
    mov [px_sbase], ax              ; ...from the file's first byte (a PNG
    mov [px_sbase + 2], ax          ; inside an ICO moves it, 106.25)
    mov al, [px_cur + PXR_FMT]
    call px_fpart                   ; the decoder part this format needs, and
    call px_kneed                   ; no other held (SPEC.md 106.18)
    mov es, [px_hseg]
    mov al, [px_cur + PXR_FMT]
    call px_fpart                   ; every format PiXEL reads has its part
    or al, al                       ; now: the simple five too (106.20)
    jz .none
    call px_hpart
    jmp short .out
.none:
    mov ax, PXD_NOTPIC              ; nothing PiXEL names
    cmp byte [px_cur + PXR_FMT], PXF_NONE
    je .no
    mov ax, PXD_NOTYET              ; a format whose decoder is a later wave
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; px_dims - AX = a width, BX = a height, both proved non-zero and at most
; PX_DIMMAX by the caller: into the record. Preserves all
px_dims:
    mov [px_cur + PXR_SW], ax
    mov [px_cur + PXR_SH], bx
    ret

; =============================================================================
; THE ROWS, ON THE WORKER
; =============================================================================

; px_decode - the decode, from the top of the stream: every format's is a
; decoder part's since wave 4 (SPEC.md 106.20). out CF = 0, or CF = 1 AX = a
; PXD_* (pxpump.inc's px_wjob says what it was)
px_decode:
    jmp px_dpart

; =============================================================================
; A DECODER PART (SPEC.md 106.18): its HEAD on the UI task, its DECODE here
; =============================================================================

; px_hpart - the header, by the part [px_kheld] names: the head through the
; context, the answers into the record. A JPEG's HEAD may ask for the NEXT
; head (PXD_MORE, SPEC.md 106.19): it is read and HEAD called again. CF = 1
; AX = a PXD_*, or PXD_QUIET when the part could not be fetched (op_fetch
; has said why). A HEAD writes its part's OWN memory (pxpng.asm's pz_w,
; pz_rowb, ...) and that part's DECODE trusts it: what keeps a HEAD from
; running while a DECODE is inside the same part is px_open's px_busy refusal
px_hpart:
    mov ax, [px_hseg]
    mov [px_k + PXK_HSEG], ax
    mov ax, [px_hlen]
    mov [px_k + PXK_HLEN], ax
    xor ax, ax
    mov [px_k + PXK_HPOS], ax
    mov [px_k + PXK_HBASE], ax
    mov [px_k + PXK_HBASE + 2], ax
    mov [px_k + PXK_HCNT], al
    mov [px_k + PXK_FLAGS], al
    mov ax, [px_fsize]
    mov [px_k + PXK_FSZ], ax
    mov ax, [px_fsize + 2]
    mov [px_k + PXK_FSZ + 2], ax
.call:
    mov al, [px_kheld]
    mov bl, PXV_HEAD
    mov di, px_k
    call px_pcall
    jnc .ok
    cmp byte [px_pfail], 0          ; the part's own answer: a PXD_*
    je .own
    cmp ax, PXE_PART
    je .quiet
    mov si, px_s_badpart            ; fetched, and not a part of this ABI
    call px_toast
.quiet:
    mov ax, PXD_QUIET
.no:
    stc
    ret
.own:                               ; (a next head is the part's own read
.redir:                             ; since 106.25: pxhmore.inc)
    cmp ax, PXD_REDIR               ; a PNG inside an ICO (106.25): the head
    jne .tail                       ; moved down to it and the stream's base
    mov byte [px_cur + PXR_PROG], 2 ; set by the EXTRAS part - the PNG part
    mov al, PXPART_PNG              ; reads it, and px_fpart says so from
    call px_kneed                   ; now on (the plans, px_kkeep)
    jmp short .call
.tail:                              ; (a PCX's tail is the SIMPLE part's own
.link:                              ; read since 106.25)
    cmp ax, PXD_LINK                ; another build's SIMPLE part
    jne .no
    mov si, px_s_badpart
    call px_toast
    mov ax, PXD_QUIET
    jmp short .no
.ok:
    mov ax, [px_k + PXK_SW]
    mov bx, [px_k + PXK_SH]
    call px_dims
    mov al, [px_k + PXK_RF]
    mov [px_cur + PXR_RF], al
    mov al, [px_k + PXK_BITS]
    mov [px_cur + PXR_BITS], al
    mov al, [px_k + PXK_PACK]
    mov [px_cur + PXR_PACK], al
    mov ax, [px_k + PXK_NPAL]
    mov [px_cur + PXR_NPAL], ax
    mov ax, [px_k + PXK_DPARA]      ; the scratch: one size at every scale,
    mov [px_dsc], ax                ; or the part's own four
    mov [px_dsc + 2], ax
    mov [px_dsc + 4], ax
    mov [px_dsc + 6], ax
    mov al, [px_k + PXK_FLAGS]
    test al, PXKF_DSC
    jz .nd
    push si
    push di
    push es
    push ds
    pop es
    mov si, px_k + PXK_DSC
    mov di, px_dsc
    movsw
    movsw
    movsw
    movsw
    pop es
    pop di
    pop si
.nd:
    mov byte [px_dfmt], DF_PART
    mov byte [px_dtop], 1           ; (an interlaced picture says which of its
    test al, PXKF_UP                ; rows are complete itself; a JPEG turned
    jz .td                          ; upside down arrives bottom-up)
    mov byte [px_dtop], 0
.td:
    test al, PXKF_PROG
    jz .np
    mov byte [px_cur + PXR_PROG], 1
.np:
    clc                             ; ([px_doff] is the SIMPLE part's own:
    ret                             ; its DECODE skips to it, SPEC.md 106.20)

; px_dpart - the worker: the context's decode half, the part's DECODE, and
; what the stream told it back into the record. out CF/AX as any decoder's
px_dpart:
    mov al, [px_cur + PXR_SCL]
    mov [px_k + PXK_SCL], al
    mov ax, [px_cur + PXR_MW]
    mov [px_k + PXK_MW], ax
    mov ax, [px_cur + PXR_MH]
    mov [px_k + PXK_MH], ax
    mov ax, [px_wseg]
    mov [px_k + PXK_RSEG], ax
    add ax, [px_wo_dec]
    mov [px_k + PXK_DSEG], ax
    mov word [px_k + PXK_SPAL], px_spal
    mov byte [px_k + PXK_RBLK], 0
    push ds
    pop es
    mov si, px_kst
    mov di, px_k + PXK_SVC
    mov cx, PXS_N
    cld
    rep movsw
    mov al, [px_kheld]
    mov bl, PXV_DECODE
    mov di, px_k
    call px_wcall
    pushf
    push ax
    mov al, [px_k + PXK_PACK]
    mov [px_cur + PXR_PACK], al
    mov al, [px_k + PXK_BITS]
    mov [px_cur + PXR_BITS], al
    mov ax, [px_k + PXK_NPAL]
    mov [px_cur + PXR_NPAL], ax
    pop ax
    popf
    ret
