; =============================================================================
; tpexport.inc - PDF 1.4 and PostScript Level 1 writers
;
; PDF 1.4, classic xref (no 1.5 object/xref streams). Uncompressed content
; streams, and Courier: what os8088 measured is an 8x8 monospaced cell, so a
; monospaced face is the only one that can render it back (SPEC.md 69.2).
; The whole file is built in the export claim and written in ONE call.
;
; THE CLAIM GROWS AS THE FILE DOES, AND IS GIVEN BACK WHEN IT IS WRITTEN
; (SPEC.md 69.5). It is TP_EXP_STEP KB at the start of an export and
; tp_xw_al regrows it a step at a time (OSAPI_MEM_REGROW, SPEC.md 50.3.1) up
; to TP_EXP_MAX, so an export holds what the file needs rather than the
; ceiling - and nothing at all once the write has gone. Every write into it
; goes through tp_xw_al, which re-reads [tp_expseg] each time, so a grow that
; MOVED the claim is followed for free.
; =============================================================================

; SI = the 8.3 name to write
tp_export_pdf:
    mov byte [tp_xkind], 0
    jmp short tp_export_do

tp_export_ps:
    mov byte [tp_xkind], 1

tp_export_do:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tp_typeset
    push si                     ; the name, past both writers
    mov ax, TP_EXP_STEP
    call OSAPI_MEM_CLAIM
    mov si, tp_s_nomem
    jc .say
    mov [tp_expseg], dx
    xor ax, ax
    mov [tp_explen], ax
    mov byte [tp_xdrop], al
    mov word [tp_expcap], TP_EXP_STEP * 1024
    cmp byte [tp_xkind], 0
    jne .ps
    call tp_write_pdf
    jmp short .chk
.ps:
    call tp_write_ps
.chk:
    ; tp_xw_al DROPS a byte it has no room for rather than writing past the
    ; claim, so a dropped byte is a file whose xref offsets and stream
    ; lengths point at bytes that were never written. Say so: writing it out
    ; gives a file a reader opens and reports as corrupt, which reads as a
    ; bug in the exporter rather than as a document too big. AT the ceiling
    ; it is the document; short of it a grow was refused, and that is RAM.
    mov si, tp_s_xbig
    cmp byte [tp_xdrop], 0
    je .write
    cmp word [tp_expcap], TP_EXP_MAX
    jae .give
    mov si, tp_s_nomem
    jmp short .give
.write:
    pop si                      ; the name
    push si
    mov es, [tp_expseg]
    xor bx, bx
    mov cx, [tp_explen]
    xor dx, dx
    call OSAPI_FILE_WRITE
    mov si, tp_s_xok
    jnc .give
    mov si, tp_s_xerr
.give:
    mov dx, [tp_expseg]         ; the claim is the export's, and the export
    call OSAPI_MEM_FREE         ; is over
    mov word [tp_expseg], 0
.say:
    call tp_toast
    pop si
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ---- buffer writes ----------------------------------------------------------
; AL -> the export claim at [tp_explen]. Preserves every register.
tp_xw_al:
    push di
    push es
    mov di, [tp_explen]
    cmp di, [tp_expcap]
    jae .grow                   ; (not taken: a byte a call, and the jump that
.put:                           ; falls through is the cheap one on an 8088)
    mov es, [tp_expseg]         ; ...re-read: a grow may have MOVED it
    mov [es:di], al
    inc di
    mov [tp_explen], di
.o:
    pop es
    pop di
    ret
.grow:
    call tp_xgrow
    jnc .put
    mov byte [tp_xdrop], 1
    jmp short .o

; The claim one step bigger, or CF=1 at the ceiling or when the heap says no.
; Preserves every register.
tp_xgrow:
    push ax
    push dx
    mov ax, [tp_expcap]
    cmp ax, TP_EXP_MAX
    jae .no
    mov al, ah                  ; bytes -> KB (the cap is whole KB)
    shr al, 1
    shr al, 1
    xor ah, ah
    add al, TP_EXP_STEP
    mov dx, [tp_expseg]
    call OSAPI_MEM_REGROW       ; out DX = the claim's base NOW
    jc .no
    mov [tp_expseg], dx
    add word [tp_expcap], TP_EXP_STEP * 1024
    clc
    jmp short .o
.no:
    stc
.o:
    pop dx
    pop ax
    ret

; DS:SI NUL -> the claim. Preserves every register.
tp_xw_str:
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

; AX unsigned -> decimal, and tp_xw_numz the same zero-padded to CX digits.
; Both preserve every register.
tp_xw_num:
    push cx
    mov cx, 1
    call tp_xw_numz
    pop cx
    ret

tp_xw_numz:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, 10
    mov si, cx                  ; SI = the fewest digits to write
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
    cmp cx, si                  ; ...and a leading zero a turn until there
    jb .d                       ; are that many (0 / 10 is 0, remainder 0)
.p:
    pop ax
    add al, '0'
    call tp_xw_al
    loop .p
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX, ' ', DX, then the string at SI. Preserves every register.
tp_xw_pair:
    call tp_xw_num
    push ax
    mov al, ' '
    call tp_xw_al
    mov ax, dx
    call tp_xw_num
    pop ax
    jmp short tp_xw_str

; SI = a prefix, AX = a number, DI = a suffix: the three in a row.
tp_xw_sns:
    call tp_xw_str
    call tp_xw_num
    xchg si, di
    call tp_xw_str
    xchg si, di
    ret

tp_xw_cr:
    push ax
    mov al, 10
    call tp_xw_al
    pop ax
    ret

tp_mark_obj:
    ; AX = obj number 1-based: its offset is where the file is now. AX kept.
    push bx
    mov bx, ax
    shl bx, 1
    push word [tp_explen]
    pop word [tp_objoff-2+bx]
    pop bx
    ret

; AX = the PAGE object of page BX, 5 + 2*BX (1 catalog, 2 pages, 3 font,
; 4 info); its contents object is the next.
tp_pgobj:
    mov ax, bx
    shl ax, 1
    add ax, 5
    ret

; AX = the page count the file DECLARES: [tp_npages], and never 0.
tp_npg:
    mov ax, [tp_npages]
    or ax, ax
    jnz .o
    inc ax
.o:
    ret

; The writers clobber what they like: tp_export_do banks every register.
tp_write_pdf:
    mov si, tp_pdf_magic
    call tp_xw_str
    mov ax, 1                   ; obj 1 catalog
    call tp_mark_obj
    mov si, tp_pdf_cat
    call tp_xw_str
    inc ax                      ; obj 2 pages
    call tp_mark_obj
    mov si, tp_pdf_pg0
    call tp_xw_str
    call tp_npg
    mov [tp_tmp], ax            ; n
    xor bx, bx
.kd:
    cmp bx, [tp_tmp]
    jae .kdz
    call tp_pgobj
    call tp_xw_num
    mov si, tp_pdf_ref
    call tp_xw_str
    inc bx
    jmp .kd
.kdz:
    mov si, tp_pdf_pg1
    mov ax, [tp_tmp]
    mov di, tp_pdf_pg2
    call tp_xw_sns
    mov ax, 3                   ; obj 3 font
    call tp_mark_obj
    mov si, tp_pdf_font
    call tp_xw_str
    inc ax                      ; obj 4 document info
    call tp_mark_obj
    mov si, tp_pdf_info
    call tp_xw_str
    xor bx, bx                  ; pages + contents
.pg:
    cmp bx, [tp_tmp]
    jae .xref
    call tp_pgobj
    call tp_mark_obj
    call tp_xw_num
    mov si, tp_pdf_pobj
    call tp_xw_str
    mov ax, [tp_pw]
    mov dx, [tp_ph]
    mov si, tp_pdf_mbox
    call tp_xw_pair
    call tp_pgobj
    inc ax                      ; its contents object
    call tp_xw_num
    mov si, tp_pdf_pobj2
    call tp_xw_str
    call tp_pdf_page_stream
    inc bx
    jmp .pg
.xref:
    push word [tp_explen]       ; the xref's offset, for startxref
    call tp_npg
    shl ax, 1
    add ax, 4
    mov [tp_nobj], ax           ; real objs: 4 + 2*npages
    inc ax                      ; ...and the count includes object 0
    mov si, tp_pdf_xref
    mov di, tp_pdf_free
    call tp_xw_sns
    mov cx, [tp_nobj]
    mov bx, tp_objoff
.xo:
    mov ax, [bx]
    inc bx
    inc bx
    push cx
    mov cx, 10
    call tp_xw_numz
    pop cx
    mov si, tp_pdf_n
    call tp_xw_str
    loop .xo
    mov ax, [tp_nobj]
    inc ax
    mov si, tp_pdf_trl
    mov di, tp_pdf_trl2
    call tp_xw_sns
    pop ax
    mov si, tp_pdf_eof
    jmp tp_xw_num_s             ; startxref's number, then %%EOF

; AX = a number, then the string at SI.
tp_xw_num_s:
    call tp_xw_num
    jmp tp_xw_str

; BX = page index. Its contents object, 6 + 2*BX, whose /Length is five
; zeros written now and patched when the stream's length is known.
tp_pdf_page_stream:
    call tp_pgobj
    inc ax
    call tp_mark_obj
    mov si, tp_pdf_st0
    call tp_xw_num_s            ; 'N 0 obj << /Length '
    push word [tp_explen]       ; where the five digits go
    mov si, tp_pdf_zeros
    call tp_xw_str
    mov si, tp_pdf_st1
    call tp_xw_str
    push word [tp_explen]       ; the stream's start
    push bx
    call tp_pdf_stream_body
    pop bx
    pop ax
    neg ax
    add ax, [tp_explen]         ; its length
    pop dx                      ; ...and where its digits are
    ; THE ONE WRITE THAT GOES BACK. It goes back THROUGH tp_xw_al - [tp_explen]
    ; pointed at the five zeros for five bytes - so it carries tp_xw_al's
    ; bound and cannot land past the claim: on a document too big for the
    ; ceiling the zeros were never written, and patching them anyway used to
    ; put five bytes into whatever the heap handed out next (SPEC.md 50.3).
    push word [tp_explen]
    mov [tp_explen], dx
    mov cx, 5
    call tp_xw_numz
    pop word [tp_explen]
    mov si, tp_pdf_st2
    jmp tp_xw_str

; BX = page: the runs set on it, then its boxes.
tp_pdf_stream_body:
    mov [tp_xpage], bl
    mov si, tp_pdf_bt
    call tp_xw_str
    mov al, 12                  ; the size in effect, selected
    call tp_xsize
    mov bx, tp_runs
    mov di, tp_text
    mov cx, [tp_nruns]
.r:
    jcxz .et
    mov al, [bx]
    and al, TP_RUN_PAGE
    cmp al, [tp_xpage]
    jne .rn
    mov al, [bx+5]
    cmp al, [tp_tmp5]
    je .tm
    call tp_xsize
.tm:
    mov ax, [bx+1]
.show:                          ; one Tm and one Tj at x = AX
    mov si, tp_pdf_m1
    call tp_xw_str
    mov dx, [bx+3]
    mov si, tp_pdf_tm
    call tp_xw_pair
    call tp_xw_esc_n
    mov si, tp_pdf_tj
    call tp_xw_str
    cmp ax, [bx+1]              ; ...and bold is the same text again, 1pt
    jne .rn                     ; right, once
    test byte [bx], TP_RUN_BOLD
    jz .rn
    inc ax
    jmp .show
.rn:
    mov al, [bx+6]              ; past this run and its text
    xor ah, ah
    add di, ax
    add bx, TP_RUN_SZ
    dec cx
    jmp .r
.et:
    mov si, tp_pdf_et
    call tp_xw_str
    mov bx, tp_boxes
    mov cx, [tp_nboxes]
.b:
    jcxz .o
    mov al, [bx]
    cmp al, [tp_xpage]
    jne .bn
    mov ax, [bx+1]
    mov dx, [bx+3]
    mov si, tp_s_spch
    call tp_xw_pair
    mov ax, [bx+5]
    mov dx, 1                   ; a box is one point tall (tp_tab_grid)
    mov si, tp_pdf_re
    call tp_xw_pair
.bn:
    add bx, TP_BOX_SZ
    loop .b
.o:
    ret

; AL = the size to select, in the format being written: "/F1 AL Tf" or
; "/Courier findfont AL scalefont setfont". Keeps BX, CX, DX and DI.
tp_xsize:
    mov [tp_tmp5], al
    push di
    xor ah, ah
    mov si, tp_pdf_f1
    mov di, tp_pdf_tf2
    cmp byte [tp_xkind], 0
    je .w
    mov si, tp_ps_ff
    mov di, tp_ps_sf
.w:
    call tp_xw_sns
    pop di
    ret

; DI = the run's text, BX = the run: '(' its bytes, escaped, ')'.
tp_xw_esc_n:
    push ax
    push cx
    push si
    mov al, '('
    call tp_xw_al
    mov cl, [bx+6]
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
    jbe .put
.dot:
    mov al, '?'
    jmp short .put
.bs:
    push ax
    mov al, '\'
    call tp_xw_al
    pop ax
.put:
    call tp_xw_al
    loop .lp
.z:
    mov al, ')'
    call tp_xw_al
    pop si
    pop cx
    pop ax
    ret

tp_write_ps:
    mov ax, [tp_pw]
    mov dx, [tp_ph]
    mov si, tp_ps_hdr
    call tp_xw_str
    mov si, tp_ps_hdr2
    call tp_xw_pair
    call tp_npg
    mov si, tp_ps_endc
    call tp_xw_num_s
    xor dx, dx                  ; DX = the page, BX = the record, below
.pg:
    cmp dx, [tp_npages]
    jae .eof
    mov [tp_xpage], dl
    mov ax, dx
    inc ax
    push dx
    mov dx, ax
    mov si, tp_ps_page
    call tp_xw_str
    mov si, tp_s_nl
    call tp_xw_pair             ; "%%Page: N N" - DSC wants the same twice
    pop dx
    mov al, 12                  ; the size in effect, selected
    call tp_xsize
    mov bx, tp_runs
    mov di, tp_text
    mov cx, [tp_nruns]
.r:
    jcxz .bx
    mov al, [bx]
    and al, TP_RUN_PAGE
    cmp al, [tp_xpage]
    jne .rn
    ; Re-select the face only when the SIZE CHANGES, which is what the PDF
    ; writer already does: "/Courier findfont N scalefont setfont" in front
    ; of every run is 38 bytes each, and a body of ordinary 12pt text is
    ; hundreds of runs that all ask for the size already set. PAPER.TEX
    ; exported as a 15KB PDF and blew the 24KB buffer as PostScript on that
    ; alone - and before the drop check in tp_export_do it did so SILENTLY.
    mov al, [bx+5]
    cmp al, [tp_tmp5]
    je .sized
    call tp_xsize
.sized:
    push dx
    mov ax, [bx+1]
    mov dx, [bx+3]
    mov si, tp_ps_mt
    call tp_xw_pair
    pop dx
    call tp_xw_esc_n
    mov si, tp_ps_sh
    call tp_xw_str
.rn:
    mov al, [bx+6]              ; past this run and its text
    xor ah, ah
    add di, ax
    add bx, TP_RUN_SZ
    dec cx
    jmp .r
.bx:
    push dx
    mov bx, tp_boxes
    mov cx, [tp_nboxes]
.b:
    jcxz .sp
    mov al, [bx]
    cmp al, [tp_xpage]
    jne .bn
    mov si, tp_ps_np
    call tp_xw_str
    mov ax, [bx+1]              ; (x, y) moveto, then (x+w, y), (x+w, y+h)
    mov dx, [bx+3]              ; and (x, y+h) lineto, then closepath
    mov si, tp_ps_mt
    call tp_xw_pair
    add ax, [bx+5]
    mov si, tp_ps_lt
    call tp_xw_pair
    inc dx                      ; ...one point tall (tp_tab_grid)
    call tp_xw_pair
    mov ax, [bx+1]
    call tp_xw_pair
    mov si, tp_ps_cp
    call tp_xw_str
.bn:
    add bx, TP_BOX_SZ
    loop .b
.sp:
    pop dx
    mov si, tp_ps_sp
    call tp_xw_str
    inc dx
    jmp .pg
.eof:
    mov si, tp_ps_eof
    jmp tp_xw_str

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
tp_pdf_zeros:   db '00000', 0
tp_pdf_st1:     db ' >>', 10, 'stream', 10, 0
tp_pdf_st2:     db 10, 'endstream', 10, 'endobj', 10, 0
tp_pdf_xref:    db 'xref', 10, '0 ', 0
tp_pdf_free:    db 10, '0000000000 65535 f ', 10, 0
tp_pdf_n:       db ' 00000 n ', 10, 0
tp_pdf_trl:     db 'trailer', 10, '<< /Size ', 0
tp_pdf_trl2:    db ' /Root 1 0 R /Info 4 0 R '
                db '/ID [<54655850616431345465585061643134> '
                db '<54655850616431345465585061643134>] >>', 10
                db 'startxref', 10, 0
tp_pdf_eof:     db 10
tp_ps_eof:      db '%%EOF', 10, 0
tp_pdf_bt:      db 'BT', 10, 0
tp_pdf_f1:      db '/F1 ', 0
tp_pdf_tf2:     db ' Tf', 10, 0
tp_pdf_m1:      db '1 0 0 1 ', 0
tp_pdf_tm:      db ' Tm', 10, 0
tp_pdf_tj:      db ' Tj', 10, 0
tp_pdf_et:      db 'ET', 10, 0
tp_pdf_re:      db ' re S'
tp_s_nl:        db 10, 0

tp_ps_hdr:      db '%!PS-Adobe-1.0', 10, '%%Title: TeXPad', 10
                db '%%Creator: os8088 TeXPad', 10
                db '%%BoundingBox: 0 0 ', 0
tp_ps_hdr2:     db 10, '%%Pages: ', 0
tp_ps_endc:     db 10, '%%EndComments', 10, 0
tp_ps_page:     db '%%Page: ', 0
tp_ps_ff:       db '/Courier findfont ', 0
tp_ps_sf:       db ' scalefont setfont', 10, 0
tp_ps_mt:       db ' moveto', 10, 0
tp_ps_sh:       db ' show', 10, 0
tp_ps_np:       db 'newpath', 10, 0
tp_ps_lt:       db ' lineto', 10, 0
tp_ps_cp:       db 'closepath stroke', 10, 0
tp_ps_sp:       db 'showpage', 10, 0
