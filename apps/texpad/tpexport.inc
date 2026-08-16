; =============================================================================
; tpexport.inc - PDF 1.4 and PostScript Level 1 writers
;
; PDF 1.4, classic xref (no 1.5 object/xref streams). Uncompressed content
; streams, and Courier: what os8088 measured is an 8x8 monospaced cell, so a
; monospaced face is the only one that can render it back (SPEC.md 66.2).
; The whole file is built in the export claim and written in ONE call.
; =============================================================================

tp_claim_exp:
    cmp word [tp_expseg], 0
    jne .ok
    mov ax, TP_EXP_KB
    call OSAPI_MEM_CLAIM
    jc .bad
    mov [tp_expseg], dx
.ok:
    mov word [tp_explen], 0
    clc
    ret
.bad:
    stc
    ret

; SI = 8.3 name
tp_export_pdf:
    mov byte [tp_xkind], 0
    call tp_typeset
    call tp_export_do
    ret

tp_export_ps:
    mov byte [tp_xkind], 1
    call tp_typeset
    call tp_export_do
    ret

tp_export_do:
    ; SI = name
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, tp_xname
    call tp_cpat
    mov byte [di], 0
    call tp_claim_exp
    jc .nomem
    cmp byte [tp_xkind], 0
    jne .ps
    call tp_write_pdf
    jmp .chk
.ps:
    call tp_write_ps
.chk:
    jc .fail
    cmp word [tp_explen], 0
    je .fail
    ; tp_xw_al DROPS bytes once the claim is full rather than growing past it,
    ; so a saturated buffer is a document that did not fit - the xref offsets
    ; and the stream lengths in it point at bytes that were never written. Say
    ; so; writing it out gives a file a reader opens and reports as corrupt,
    ; which reads as a bug in the exporter rather than as a document too big.
    cmp word [tp_explen], TP_EXP_MAX
    jae .toobig
    mov es, [tp_expseg]
    xor bx, bx
    mov cx, [tp_explen]
    xor dx, dx
    mov si, tp_xname
    call OSAPI_FILE_WRITE
    jc .fail
    mov si, tp_s_xok
    call tp_toast
    jmp .out
.nomem:
    mov si, tp_s_nomem
    call tp_toast
    jmp .out
.toobig:
    mov si, tp_s_xbig
    call tp_toast
    jmp .out
.fail:
    mov si, tp_s_xerr
    call tp_toast
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; patch the public wrappers to use tp_export_do
; (tp_export_pdf / ps already call tp_export - redirect)

; ---- buffer writes (ES = expseg) -------------------------------------------
tp_xw_al:
    push di
    push es
    mov es, [tp_expseg]
    mov di, [tp_explen]
    cmp di, TP_EXP_MAX
    jae .o
    mov [es:di], al
    inc word [tp_explen]
.o:
    pop es
    pop di
    ret

tp_xw_str:
    ; DS:SI NUL
    push ax
    push si
.lp:
    lodsb
    or al, al
    jz .o
    call tp_xw_al
    jmp .lp
.o:
    pop si
    pop ax
    ret

tp_xw_num:
    ; AX unsigned
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, 10
    xor cx, cx
    or ax, ax
    jnz .d
    mov al, '0'
    call tp_xw_al
    jmp .o
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.p:
    pop ax
    add al, '0'
    call tp_xw_al
    loop .p
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_xw_cr:
    push ax
    mov al, 10
    call tp_xw_al
    pop ax
    ret

; write (escaped) string from DS:SI
tp_xw_esc:
    push ax
    push si
    mov al, '('
    call tp_xw_al
.lp:
    lodsb
    or al, al
    jz .z
    cmp al, '('
    je .bs
    cmp al, ')'
    je .bs
    cmp al, '\'
    je .bs
    cmp al, 32
    jb .oct
    cmp al, 126
    ja .oct
    call tp_xw_al
    jmp .lp
.bs:
    push ax
    mov al, '\'
    call tp_xw_al
    pop ax
    call tp_xw_al
    jmp .lp
.oct:
    push ax
    mov al, '\'
    call tp_xw_al
    pop ax
    ; 3 octal digits
    push ax
    push cx
    xor ah, ah
    mov cl, 6
    shr al, cl
    and al, 7
    add al, '0'
    call tp_xw_al
    pop cx
    pop ax
    push ax
    push cx
    xor ah, ah
    mov cl, 3
    shr al, cl
    and al, 7
    add al, '0'
    call tp_xw_al
    pop cx
    pop ax
    and al, 7
    add al, '0'
    call tp_xw_al
    jmp .lp
.z:
    mov al, ')'
    call tp_xw_al
    pop si
    pop ax
    ret

tp_mark_obj:
    ; AX = obj number 1-based. store offset. AX preserved.
    push ax
    push bx
    dec ax
    shl ax, 1
    mov bx, ax
    mov ax, [tp_explen]
    mov [tp_objoff+bx], ax
    pop bx
    pop ax
    ret

tp_write_pdf:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [tp_explen], 0
    mov si, tp_pdf_magic
    call tp_xw_str
    ; obj 1 catalog
    mov ax, 1
    call tp_mark_obj
    mov si, tp_pdf_cat
    call tp_xw_str
    ; obj 2 pages
    mov ax, 2
    call tp_mark_obj
    mov si, tp_pdf_pg0
    call tp_xw_str
    mov ax, [tp_npages]
    or ax, ax
    jnz .np
    inc ax
.np:
    mov [tp_tmp], ax            ; n
    ; kids: page object = 5 + bx*2 (1 cat, 2 pages, 3 font, 4 info)
    xor bx, bx
.kd:
    cmp bx, [tp_tmp]
    jae .kdz
    mov ax, bx
    shl ax, 1
    add ax, 5
    call tp_xw_num
    mov si, tp_pdf_ref
    call tp_xw_str
    inc bx
    jmp .kd
.kdz:
    mov si, tp_pdf_pg1
    call tp_xw_str
    mov ax, [tp_tmp]
    call tp_xw_num
    mov si, tp_pdf_pg2
    call tp_xw_str
    ; obj 3 font
    mov ax, 3
    call tp_mark_obj
    mov si, tp_pdf_font
    call tp_xw_str
    ; obj 4 document info
    mov ax, 4
    call tp_mark_obj
    mov si, tp_pdf_info
    call tp_xw_str
    ; pages + contents
    xor bx, bx
.pg:
    cmp bx, [tp_tmp]
    jae .xref
    ; page object
    mov ax, bx
    shl ax, 1
    add ax, 5
    call tp_mark_obj
    call tp_xw_num
    mov si, tp_pdf_pobj
    call tp_xw_str
    mov ax, [tp_pw]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [tp_ph]
    call tp_xw_num
    mov si, tp_pdf_mbox
    call tp_xw_str
    ; contents id
    mov ax, bx
    shl ax, 1
    add ax, 6
    call tp_xw_num
    mov si, tp_pdf_pobj2
    call tp_xw_str
    ; contents: build length first into a side pass
    ; write a dummy length then... we build stream in scratch then emit
    call tp_pdf_page_stream     ; uses BX = page, result in explen growth
    inc bx
    jmp .pg
.xref:
    mov ax, [tp_explen]
    mov [tp_tmp2], ax           ; xref offset
    mov si, tp_pdf_xref
    call tp_xw_str
    ; real objs: 4 + 2*npages (catalog, pages, font, info, page+contents)
    mov ax, [tp_tmp]
    shl ax, 1
    add ax, 4
    mov [tp_nobj], ax
    inc ax                      ; including object 0
    call tp_xw_num
    call tp_xw_cr
    mov si, tp_pdf_free
    call tp_xw_str
    xor bx, bx
.xo:
    cmp bx, [tp_nobj]
    jae .tr
    shl bx, 1
    mov ax, [tp_objoff+bx]
    shr bx, 1
    call tp_pdf_off10
    mov si, tp_pdf_n
    call tp_xw_str
    inc bx
    jmp .xo
.tr:
    mov si, tp_pdf_trl
    call tp_xw_str
    mov ax, [tp_nobj]
    inc ax
    call tp_xw_num
    mov si, tp_pdf_trl2
    call tp_xw_str
    mov ax, [tp_tmp2]
    call tp_xw_num
    mov si, tp_pdf_eof
    call tp_xw_str
    clc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_pdf_zeros:   db '00000', 0

; AX = value, patch 5 digits at tp_tmp3 in the export buffer.
;
; This is the ONE write into the export claim that does not go through
; tp_xw_al, so it is the one that has to bound itself. tp_xw_al saturates
; [tp_explen] at TP_EXP_MAX rather than growing past it, so on a document big
; enough to fill 24KB the five zeros this comes back to overwrite were never
; written - and patching them anyway puts five bytes past the end of the claim,
; into whatever the heap handed out next (SPEC.md 50.3). Refuse instead: the
; caller turns a truncated export into an error rather than a bad PDF.
tp_pdf_patch5:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov di, [tp_tmp3]
    add di, 4
    cmp di, TP_EXP_MAX
    jae .o
    mov es, [tp_expseg]
    mov bx, 10
    mov cx, 5
.d:
    xor dx, dx
    div bx
    add dl, '0'
    mov [es:di], dl
    dec di
    loop .d
.o:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_pdf_off10:
    ; write AX as 10-digit
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    mov cx, 10
    xor dx, dx
    ; push 10 digits
    mov cx, 10
.d:
    xor dx, dx
    div bx
    push dx
    loop .d
    mov cx, 10
.p:
    pop ax
    add al, '0'
    call tp_xw_al
    loop .p
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; BX = page index. write contents object 6+2*bx
tp_pdf_page_stream:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; mark contents obj
    mov ax, bx
    shl ax, 1
    add ax, 6
    push ax
    call tp_mark_obj
    call tp_xw_num
    mov si, tp_pdf_st0
    call tp_xw_str
    mov ax, [tp_explen]
    mov [tp_tmp3], ax           ; offset of length digits
    mov si, tp_pdf_zeros
    call tp_xw_str
    mov si, tp_pdf_st1
    call tp_xw_str
    mov ax, [tp_explen]
    mov [tp_tmp2], ax           ; stream start
    call tp_pdf_stream_body
    mov ax, [tp_explen]
    sub ax, [tp_tmp2]
    call tp_pdf_patch5
    mov si, tp_pdf_st2
    call tp_xw_str
    pop ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_pdf_stream_body:
    ; page BX
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, tp_pdf_bt
    call tp_xw_str
    mov si, tp_pdf_tf
    call tp_xw_str
    mov byte [tp_tmp5], 12      ; last size
    xor cx, cx
.r:
    cmp cx, [tp_nruns]
    jae .et
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    mov al, [si]
    xor ah, ah
    cmp ax, bx
    jne .rn
    mov al, [si+6]
    cmp al, [tp_tmp5]
    je .tm
    mov [tp_tmp5], al
    mov si, tp_pdf_f1
    call tp_xw_str
    mov al, [tp_tmp5]
    xor ah, ah
    call tp_xw_num
    mov si, tp_pdf_tf2
    call tp_xw_str
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
.tm:
    mov al, '1'
    call tp_xw_al
    mov al, ' '
    call tp_xw_al
    mov al, '0'
    call tp_xw_al
    mov al, ' '
    call tp_xw_al
    mov al, '0'
    call tp_xw_al
    mov al, ' '
    call tp_xw_al
    mov al, '1'
    call tp_xw_al
    mov al, ' '
    call tp_xw_al
    mov ax, [si+2]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+4]
    call tp_xw_num
    mov si, tp_pdf_tm
    call tp_xw_str
    ; text
    push si
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    mov di, [si+8]
    add di, tp_text
    push cx
    xor ch, ch
    mov cl, [si+7]
    call tp_xw_esc_n
    pop cx
    pop si
    mov si, tp_pdf_tj
    call tp_xw_str
    ; bold overprint
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    test byte [si+1], 1
    jz .un
    mov ax, [si+2]
    inc ax
    push ax
    mov al, '1'
    call tp_xw_al
    mov si, tp_pdf_tm1
    call tp_xw_str
    pop ax
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    mov ax, [si+4]
    call tp_xw_num
    mov si, tp_pdf_tm
    call tp_xw_str
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    mov di, [si+8]
    add di, tp_text
    push cx
    xor ch, ch
    mov cl, [si+7]
    call tp_xw_esc_n
    pop cx
    mov si, tp_pdf_tj
    call tp_xw_str
.un:
    test byte [si+1], 2
    ; underline is a box; handled in boxes? we draw as line in boxes list
    ; skip here
.rn:
    inc cx
    jmp .r
.et:
    mov si, tp_pdf_et
    call tp_xw_str
    ; boxes
    xor cx, cx
.b:
    cmp cx, [tp_nboxes]
    jae .o
    mov ax, cx
    mov dx, TP_BOX_SZ
    mul dx
    mov si, ax
    add si, tp_boxes
    mov al, [si]
    xor ah, ah
    cmp ax, bx
    jne .bn
    mov ax, [si+2]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+4]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+6]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+8]
    call tp_xw_num
    mov si, tp_pdf_re
    call tp_xw_str
.bn:
    inc cx
    jmp .b
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; DI = bytes, CL = len (CH may be trash)
tp_xw_esc_n:
    push ax
    push cx
    push si
    mov al, '('
    call tp_xw_al
    xor ch, ch
    jcxz .z
    mov si, di
.lp:
    lodsb
    cmp al, '('
    je .bs
    cmp al, ')'
    je .bs
    cmp al, '\'
    je .bs
    cmp al, 32
    jb .dot
    cmp al, 126
    ja .dot
    call tp_xw_al
    jmp .n
.bs:
    push ax
    mov al, '\'
    call tp_xw_al
    pop ax
    call tp_xw_al
    jmp .n
.dot:
    mov al, '?'
    call tp_xw_al
.n:
    loop .lp
.z:
    mov al, ')'
    call tp_xw_al
    pop si
    pop cx
    pop ax
    ret

tp_write_ps:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [tp_explen], 0
    mov si, tp_ps_hdr
    call tp_xw_str
    mov ax, [tp_pw]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [tp_ph]
    call tp_xw_num
    mov si, tp_ps_hdr2
    call tp_xw_str
    mov ax, [tp_npages]
    or ax, ax
    jnz .n
    inc ax
.n:
    call tp_xw_num
    call tp_xw_cr
    mov si, tp_ps_endc
    call tp_xw_str
    xor bx, bx
.pg:
    cmp bx, [tp_npages]
    jae .eof
    cmp word [tp_npages], 0
    jne .p1
    jmp .eof
.p1:
    mov si, tp_ps_page
    call tp_xw_str
    ; "%%Page: label ordinal", and DSC wants the SAME number twice here. The
    ; separator has to be pushed around, because `mov al, ' '` leaves AX =
    ; 0x0020 and the second tp_xw_num then printed the space as the number:
    ; every page came out "%%Page: N 32".
    mov ax, bx
    inc ax
    call tp_xw_num
    push ax
    mov al, ' '
    call tp_xw_al
    pop ax
    call tp_xw_num
    call tp_xw_cr
    mov si, tp_ps_font
    call tp_xw_str
    mov byte [tp_tmp5], 12      ; ...which is the size now in effect
    ; runs
    xor cx, cx
.r:
    cmp cx, [tp_nruns]
    jae .bx
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    mov al, [si]
    xor ah, ah
    cmp ax, bx
    jne .rn
    ; Re-select the face only when the SIZE CHANGES, which is what
    ; tp_pdf_stream_body already does with its own [tp_tmp5] and what this
    ; did not: "/Courier findfont N scalefont setfont" in front of every run
    ; is 38 bytes each, and a body of ordinary 12pt text is hundreds of runs
    ; that all ask for the size already set. PAPER.TEX exported as a 15KB PDF
    ; and blew the 24KB buffer as PostScript on that alone - and before the
    ; saturation check in tp_export_do it did so SILENTLY, writing a .PS that
    ; stops mid-document.
    mov al, [si+6]
    cmp al, [tp_tmp5]
    je .sized
    mov [tp_tmp5], al
    mov si, tp_ps_ff
    call tp_xw_str
    mov al, [tp_tmp5]
    xor ah, ah
    call tp_xw_num
    mov si, tp_ps_sf
    call tp_xw_str
.sized:
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    mov ax, [si+2]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+4]
    call tp_xw_num
    mov si, tp_ps_mt
    call tp_xw_str
    mov ax, cx
    mov dx, TP_RUN_SZ
    mul dx
    mov si, ax
    add si, tp_runs
    mov di, [si+8]
    add di, tp_text
    push cx
    xor ch, ch
    mov cl, [si+7]
    call tp_xw_esc_n
    pop cx
    mov si, tp_ps_sh
    call tp_xw_str
.rn:
    inc cx
    jmp .r
.bx:
    xor cx, cx
.b:
    cmp cx, [tp_nboxes]
    jae .sp
    mov ax, cx
    mov dx, TP_BOX_SZ
    mul dx
    mov si, ax
    add si, tp_boxes
    mov al, [si]
    xor ah, ah
    cmp ax, bx
    jne .bn
    mov si, tp_ps_np
    call tp_xw_str
    mov ax, cx
    mov dx, TP_BOX_SZ
    mul dx
    mov si, ax
    add si, tp_boxes
    mov ax, [si+2]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+4]
    call tp_xw_num
    mov si, tp_ps_mt
    call tp_xw_str
    mov ax, cx
    mov dx, TP_BOX_SZ
    mul dx
    mov si, ax
    add si, tp_boxes
    mov ax, [si+2]
    add ax, [si+6]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+4]
    call tp_xw_num
    mov si, tp_ps_lt
    call tp_xw_str
    mov ax, cx
    mov dx, TP_BOX_SZ
    mul dx
    mov si, ax
    add si, tp_boxes
    mov ax, [si+2]
    add ax, [si+6]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+4]
    add ax, [si+8]
    call tp_xw_num
    mov si, tp_ps_lt
    call tp_xw_str
    mov ax, cx
    mov dx, TP_BOX_SZ
    mul dx
    mov si, ax
    add si, tp_boxes
    mov ax, [si+2]
    call tp_xw_num
    mov al, ' '
    call tp_xw_al
    mov ax, [si+4]
    add ax, [si+8]
    call tp_xw_num
    mov si, tp_ps_lt
    call tp_xw_str
    mov si, tp_ps_cp
    call tp_xw_str
.bn:
    inc cx
    jmp .b
.sp:
    mov si, tp_ps_sp
    call tp_xw_str
    inc bx
    jmp .pg
.eof:
    mov si, tp_ps_eof
    call tp_xw_str
    clc
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_xname:       times 14 db 0

tp_pdf_magic:   db '%PDF-1.4', 10, '%', 0xE2, 0xE3, 0xCF, 0xD3, 10, 0
tp_pdf_cat:     db '1 0 obj', 10
                db '<< /Type /Catalog /Version /1.4 /Pages 2 0 R '
                db '/ViewerPreferences << /DisplayDocTitle true >> >>', 10
                db 'endobj', 10, 0
tp_pdf_pg0:     db '2 0 obj', 10, '<< /Type /Pages /Kids [ ', 0
tp_pdf_ref:     db ' 0 R ', 0
tp_pdf_pg1:     db '] /Count ', 0
tp_pdf_pg2:     db ' >>', 10, 'endobj', 10, 0
tp_pdf_font:    db '3 0 obj', 10
                db '<< /Type /Font /Subtype /Type1 /BaseFont /Courier '
                db '/Encoding /StandardEncoding >>', 10, 'endobj', 10, 0
tp_pdf_info:    db '4 0 obj', 10
                db '<< /Title (TeXPad) /Creator (TeXPad) '
                db '/Producer (os8088 TeXPad) >>', 10
                db 'endobj', 10, 0
tp_pdf_pobj:    db ' 0 obj', 10, '<< /Type /Page /Parent 2 0 R '
                db '/MediaBox [ 0 0 ', 0
tp_pdf_mbox:    db ' ] /Contents ', 0
tp_pdf_pobj2:   db ' 0 R /Resources << /Font << /F1 3 0 R >> >> >>', 10
                db 'endobj', 10, 0
tp_pdf_st0:     db ' 0 obj', 10, '<< /Length ', 0
tp_pdf_st1:     db ' >>', 10, 'stream', 10, 0
tp_pdf_st2:     db 10, 'endstream', 10, 'endobj', 10, 0
tp_pdf_xref:    db 'xref', 10, '0 ', 0
tp_pdf_free:    db '0000000000 65535 f ', 10, 0
tp_pdf_n:       db ' 00000 n ', 10, 0
tp_pdf_trl:     db 'trailer', 10, '<< /Size ', 0
tp_pdf_trl2:    db ' /Root 1 0 R /Info 4 0 R '
                db '/ID [<54655850616431345465585061643134> '
                db '<54655850616431345465585061643134>] >>', 10
                db 'startxref', 10, 0
tp_pdf_eof:     db 10, '%%EOF', 10, 0
tp_pdf_bt:      db 'BT', 10, 0
tp_pdf_tf:      db '/F1 12 Tf', 10, 0
tp_pdf_f1:      db '/F1 ', 0
tp_pdf_tf2:     db ' Tf', 10, 0
tp_pdf_tm:      db ' Tm', 10, 0
tp_pdf_tm1:     db ' 0 0 1 ', 0
tp_pdf_tj:      db ' Tj', 10, 0
tp_pdf_et:      db 'ET', 10, 0
tp_pdf_re:      db ' re S', 10, 0

tp_ps_hdr:      db '%!PS-Adobe-1.0', 10, '%%Title: TeXPad', 10
                db '%%Creator: os8088 TeXPad', 10
                db '%%BoundingBox: 0 0 ', 0
tp_ps_hdr2:     db 10, '%%Pages: ', 0
tp_ps_endc:     db '%%EndComments', 10, 0
tp_ps_page:     db '%%Page: ', 0
tp_ps_font:     db '/Courier findfont 12 scalefont setfont', 10, 0
tp_ps_ff:       db '/Courier findfont ', 0
tp_ps_sf:       db ' scalefont setfont', 10, 0
tp_ps_mt:       db ' moveto', 10, 0
tp_ps_sh:       db ' show', 10, 0
tp_ps_np:       db 'newpath', 10, 0
tp_ps_lt:       db ' lineto', 10, 0
tp_ps_cp:       db 'closepath stroke', 10, 0
tp_ps_sp:       db 'showpage', 10, 0
tp_ps_eof:      db '%%EOF', 10, 0
