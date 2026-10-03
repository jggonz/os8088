; =============================================================================
; os8088 - apps/pixel/pxsimple.inc
;
; THE FORMATS' FRONT DOOR, resident (SPEC.md 106.10, 106.18-106.20): the
; header's dispatch to the decoder PART that reads the format - since wave 4
; every one has a part, the simple five (BMP, PCX, TGA, PNM, PIX) a part of
; their own LINKED against the package (apps/pixel/pxsimp.asm) - and the
; resident halves of the part protocol: px_hpart (HEAD, with the next head a
; JPEG asks for and the tail a PCX asks for), px_hmore, px_pcxtail (a file
; read, so the resident's), px_dpart (DECODE on the worker).
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

; px_pcxtail - an 8-bit PCX's palette: the file's last 769 bytes, a 0x0C and
; 256 R,G,B triples, read into [px_spal] when the file is long enough to
; have one (SPEC.md 106.10). CF = 1 there is none: the picture is grey.
; In memory (a CZ file) it is read from there; else with OSAPI_FILE_READ_AT
; into the head's claim, a cluster at a time. Preserves all
px_pcxtail:
    mov byte [px_pvalid], 0         ; (px_tbuf is px_plan's bytes)
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [px_cur + PXR_FSIZE]    ; the file must hold the header and it
    mov dx, [px_cur + PXR_FSIZE + 2]
    sub ax, 128 + 769
    sbb dx, 0
    jb .none
    mov ax, [px_cur + PXR_FSIZE]    ; DX:AX = where the tail starts
    mov dx, [px_cur + PXR_FSIZE + 2]
    sub ax, 769
    sbb dx, 0
    mov [px_ttail], ax
    mov [px_ttail + 2], dx
    mov word [px_tgot], 0
    cmp byte [px_rflat], 0
    jne .flat
    call px_clbytes
    mov [px_tcl], ax
.chunk:                             ; the cluster holding byte [px_ttail] +
    mov ax, [px_ttail]              ; [px_tgot]
    mov dx, [px_ttail + 2]
    add ax, [px_tgot]
    adc dx, 0
    mov cx, [px_tcl]
    div cx                          ; AX = the cluster, DX = the byte in it
    mov si, dx
    mul cx                          ; DX:AX = its offset
    push si
    mov es, [px_hseg]               ; into the head claim's SPARE cluster,
    mov bx, [px_hcap]               ; past the head: the PCX's HEAD parses
    sub bx, cx                      ; the head again once this is read
    mov si, px_cur + PXR_NAME       ; (SPEC.md 106.20)
    call OSAPI_FILE_READ_AT         ; DX:AX = bytes delivered
    pop si
    jc .none
    or dx, dx
    jnz .some
    cmp ax, si
    jbe .none
.some:
    mov cx, [px_tcl]                ; copy what this cluster holds of it
    sub cx, si
    mov ax, 769
    sub ax, [px_tgot]
    cmp cx, ax
    jbe .cp
    mov cx, ax
.cp:
    mov di, px_tbuf
    add di, [px_tgot]
    add [px_tgot], cx
    add si, [px_hcap]               ; (the spare cluster's place)
    sub si, [px_tcl]
.cl:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cl
    cmp word [px_tgot], 769
    jb .chunk
    jmp short .have
.flat:                              ; in memory: segment arithmetic
    mov ax, [px_ttail]
    mov dx, [px_ttail + 2]
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_flatseg]
    mov es, ax
    mov di, px_tbuf
    mov cx, 769
.fl:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .fl
.have:
    cmp byte [px_tbuf], 0x0C
    jne .none
    push ds
    pop es
    mov si, px_tbuf + 1
    mov di, px_spal
    mov cx, 768
    cld
    rep movsb
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
.own:
    cmp ax, PXD_MORE
    jne .tail
    call px_hmore                   ; the next head, or CF AX = why not
    jnc .call
    ret
.tail:
    cmp ax, PXD_TAIL                ; a PCX's 769-byte palette: a file read,
    jne .link                       ; so the resident's (SPEC.md 106.20)
    call px_pcxtail
    mov byte [px_k + PXK_HCNT], 1
    jnc .call
    mov byte [px_k + PXK_HCNT], 2
    jmp short .call
.link:
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

; px_hmore - the NEXT head a JPEG's HEAD asked for (SPEC.md 106.19): its
; marker is at file offset [px_k + PXK_HNEXT]. READ_AT reads from a cluster
; multiple, so the head is read from the cluster that holds the marker and
; the walk starts inside it (PXK_HPOS); an in-memory file's head is a
; pointer moved. Either way it holds the 2,048 bytes from the marker, or to
; the file's end. CF = 1 AX = PXD_READ
px_hmore:
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, [px_k + PXK_HNEXT]
    mov dx, [px_k + PXK_HNEXT + 2]
    cmp byte [px_rflat], 0
    jne .flat
    mov cx, [px_clsz]               ; a power of two
    dec cx
    mov bx, ax
    and bx, cx                      ; BX = the marker's place in its cluster
    not cx
    and ax, cx                      ; DX:AX = the cluster's offset
    mov [px_k + PXK_HPOS], bx
    mov [px_k + PXK_HBASE], ax
    mov [px_k + PXK_HBASE + 2], dx
    push bx
    mov es, [px_hseg]
    xor bx, bx
    mov cx, [px_hcap]
    mov si, px_fname
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    pop bx
    jc .rd
    add bx, PX_HEADMAX              ; the head: what was read, to 2,048
    or dx, dx                       ; past the marker
    jnz .cap
    cmp ax, bx
    jbe .len
.cap:
    mov ax, bx
.len:
    mov [px_k + PXK_HLEN], ax
    clc
    jmp short .out
.rd:
    mov ax, PXD_READ
    stc
    jmp short .out
.flat:
    mov bx, ax
    and bx, 15                      ; the paragraph's place
    and ax, 0xFFF0
    mov [px_k + PXK_HPOS], bx
    mov [px_k + PXK_HBASE], ax
    mov [px_k + PXK_HBASE + 2], dx
    push ax                         ; the head: the in-memory file at the
    push dx                         ; marker's paragraph
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_flatseg]
    mov [px_k + PXK_HSEG], ax
    pop dx
    pop ax
    mov cx, [px_flatlen]            ; its bytes from there: to the file's
    mov si, [px_flatlen + 2]        ; end, at most 2,048 past the marker
    sub cx, ax
    sbb si, dx
    add bx, PX_HEADMAX
    or si, si
    jnz .fc
    cmp cx, bx
    jbe .fl
.fc:
    mov cx, bx
.fl:
    mov [px_k + PXK_HLEN], cx
    clc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

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
