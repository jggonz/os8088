; =============================================================================
; tpparse.inc - TeXPad typesetter (text, sections, tables, Letter, B&W)
; =============================================================================

tp_typeset:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tp_claim_src
    jc .fail
    call tp_ts_init
    call tp_ts_scan
    call tp_ts_end
    mov byte [tp_setok], 1
    mov byte [tp_needset], 0
    mov word [tp_ppage], 0
    mov word [tp_pscroll], 0
    jmp .out
.fail:
    mov byte [tp_setok], 0
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_ts_init:
    push ax
    push ds
    pop es
    mov word [tp_nruns], 0
    mov word [tp_nboxes], 0
    mov word [tp_tused], 0
    mov word [tp_npages], 1
    mov word [tp_page], 0
    call tp_geom
    mov byte [tp_chno], 0
    mov byte [tp_secno], 0
    mov byte [tp_subno], 0
    mov byte [tp_itemn], 0
    mov byte [tp_made], 0
    mov byte [tp_sty_n], 0
    mov byte [tp_env_n], 0
    mov byte [tp_clen], 0
    mov byte [tp_inpara], 0
    mov byte [tp_needind], 1
    mov byte [tp_bold], 0
    mov byte [tp_under], 0
    mov byte [tp_align], 0
    mov word [tp_chno], 0
    mov word [tp_secno], 0
    mov word [tp_subno], 0
    mov byte [tp_title], 0
    mov byte [tp_author], 0
    mov byte [tp_date], 0
    mov byte [tp_onpage], 0
    ; padding
    mov word [tp_parind], 24
    mov word [tp_parskip], 6
    mov word [tp_tlead], 14
    mov word [tp_tabsep], 6
    mov word [tp_stretch], 12
    cmp byte [tp_pad], 0
    jne .p1
    mov word [tp_parind], 16
    mov word [tp_parskip], 0
    mov word [tp_tlead], 12
    mov word [tp_tabsep], 3
    mov word [tp_stretch], 10
    jmp .fs
.p1:
    cmp byte [tp_pad], 2
    jne .fs
    mov word [tp_parind], 28
    mov word [tp_parskip], 10
    mov word [tp_tlead], 16
    mov word [tp_tabsep], 8
    mov word [tp_stretch], 14
.fs:
    mov al, [tp_fsize]
    or al, al
    jnz .fsok
    mov al, 12
.fsok:
    mov [tp_csize], al
    mov [tp_tsize], al
    mov ax, [tp_left]
    mov [tp_x], ax
    mov ax, [tp_ttop]
    mov [tp_y], ax
    mov word [tp_si], 0
    mov ax, [tp_srclen]
    mov [tp_se], ax
    pop ax
    ret

tp_ts_end:
    call tp_flush_c
    call tp_endpara
    cmp byte [tp_onpage], 0
    jne .foot
    cmp word [tp_npages], 1
    jbe .foot
    dec word [tp_npages]
    dec word [tp_page]
    ret
.foot:
    call tp_footer
    ret

tp_ts_scan:
    push ax
    push bx
    push cx
    push si
    push es
    mov es, [tp_srcseg]
    mov ax, [tp_se]
    add ax, 16
    mov [tp_budget], ax
.lp:
    dec word [tp_budget]
    jz .done
    mov es, [tp_srcseg]
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .done
    mov [tp_scanat], si         ; detect a stalled cursor
    mov al, [es:si]
    cmp al, '%'
    je .com
    cmp al, '\'
    je .cmd
    cmp al, '{'
    je .psh
    cmp al, '}'
    je .pop
    cmp al, 10
    je .nl
    cmp al, 13
    je .nl
    cmp al, ' '
    je .sp
    cmp al, 9
    je .sp
    cmp al, '$'
    je .math
    cmp al, '~'
    je .nbsp
    cmp al, '&'
    je .amp
    call tp_ts_word
    jmp .adv
.com:
    call tp_skip_com
    jmp .adv
.cmd:
    call tp_ts_cmd
    jmp .adv
.psh:
    inc word [tp_si]
    call tp_sty_push
    jmp .adv
.pop:
    inc word [tp_si]
    call tp_sty_pop
    jmp .adv
.nl:
    call tp_eat_nl
    cmp al, 2
    jb .spnl
    call tp_endpara
    jmp .adv
.spnl:
    call tp_space
    jmp .adv
.sp:
    inc word [tp_si]
    call tp_space
    jmp .adv
.math:
    inc word [tp_si]
    jmp .adv
.nbsp:
    inc word [tp_si]
    mov al, ' '
    mov cx, 1
    mov si, tp_s_spch
    mov byte [tp_s_spch], ' '
    call tp_emit_str
    jmp .adv
.amp:
    inc word [tp_si]
    mov si, tp_s_amp
    call tp_emit_csi
    jmp .adv
.adv:
    ; A handler that does not consume the current byte would spin
    ; forever under the gfx lock and the screen goes black.
    mov ax, [tp_si]
    cmp ax, [tp_scanat]
    jne .lp
    inc word [tp_si]
    jmp .lp
.done:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_skip_com:
    push ax
    push es
    mov es, [tp_srcseg]
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .o
    mov al, [es:si]
    inc word [tp_si]
    cmp al, 10
    je .o
    cmp al, 13
    jne .lp
.o:
    pop es
    pop ax
    ret

; AL = number of newlines consumed (1 or 2+)
tp_eat_nl:
    push bx
    push es
    mov es, [tp_srcseg]
    xor bl, bl
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .d
    mov al, [es:si]
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
    jmp .d
.cr:
    inc word [tp_si]
    inc si
    cmp si, [tp_se]
    jae .one
    cmp byte [es:si], 10
    jne .one
    inc word [tp_si]
.one:
    inc bl
    jmp .lp
.lf:
    inc word [tp_si]
    inc bl
    jmp .lp
.d:
    mov al, bl
    pop es
    pop bx
    ret

tp_ts_word:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov di, tp_wbuf
    xor cx, cx
    mov si, [tp_si]
    cmp byte [es:si], '\'
    jne .notbs
    pop es
    pop di
    pop si
    pop cx
    pop ax
    jmp tp_ts_cmd
.notbs:
    ; dashes / quotes
    mov al, [es:si]
    cmp al, '-'
    jne .q
    inc si
    cmp si, [tp_se]
    jae .ch0
    cmp byte [es:si], '-'
    jne .ch0
    inc si
    cmp si, [tp_se]
    jae .em
    cmp byte [es:si], '-'
    jne .em
    inc si
.em:
    mov [tp_si], si
    mov si, tp_s_dash
    call tp_emit_csi
    jmp .o
.q:
    cmp al, '`'
    je .qq
    cmp al, 39
    jne .coll
    inc si
    cmp si, [tp_se]
    jae .ch0
    cmp byte [es:si], 39
    jne .ch0
    inc si
    mov [tp_si], si
    mov al, '"'
    mov [tp_wbuf], al
    mov byte [tp_wbuf+1], 0
    mov si, tp_wbuf
    call tp_emit_csi
    jmp .o
.qq:
    inc si
    cmp si, [tp_se]
    jae .ch0
    cmp byte [es:si], '`'
    jne .ch0
    inc si
    mov [tp_si], si
    mov al, '"'
    mov [tp_wbuf], al
    mov byte [tp_wbuf+1], 0
    mov si, tp_wbuf
    call tp_emit_csi
    jmp .o
.ch0:
    mov si, [tp_si]
.coll:
    cmp si, [tp_se]
    jae .ew
    mov al, [es:si]
    call tp_is_wordc
    jc .ew
    cmp cx, 70
    jae .ew
    mov [di], al
    inc di
    inc cx
    inc si
    jmp .coll
.ew:
    mov [tp_si], si
    mov byte [di], 0
    or cx, cx
    jnz .emitw
    ; breaker with no word: still consume one byte
    mov ax, [tp_si]
    cmp ax, [tp_se]
    jae .o
    inc word [tp_si]
    jmp .o
.emitw:
    mov si, tp_wbuf
    call tp_emit_csi
.o:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; CF=1 if AL is a breaker
tp_is_wordc:
    cmp al, ' '
    je .no
    cmp al, 9
    je .no
    cmp al, 10
    je .no
    cmp al, 13
    je .no
    cmp al, '\'
    je .no
    cmp al, '{'
    je .no
    cmp al, '}'
    je .no
    cmp al, '%'
    je .no
    cmp al, '$'
    je .no
    cmp al, '&'
    je .no
    cmp al, '~'
    je .no
    cmp al, '['
    je .no
    cmp al, ']'
    je .no
    clc
    ret
.no:
    stc
    ret

; AL = source byte at tp_si. CF=1 at end. Does not advance.
tp_srcb:
    push es
    push si
    mov es, [tp_srcseg]
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .e
    mov al, [es:si]
    pop si
    pop es
    clc
    ret
.e:
    xor al, al
    pop si
    pop es
    stc
    ret

tp_ts_cmd:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    inc word [tp_si]            ; consume the backslash
    call tp_srcb
    jc .o
    mov es, [tp_srcseg]
    mov si, [tp_si]
    ; \\ is a line break, not two printable slashes
    cmp al, '\'
    jne .notnl
    inc word [tp_si]
    call tp_nl
    jmp .o
.notnl:
    ; force command-name collect on any ASCII letter
    cmp al, 'A'
    jb .one
    cmp al, 'Z'
    jbe .word
    cmp al, 'a'
    jb .one
    cmp al, 'z'
    jbe .word
.one:
    inc word [tp_si]
    cmp al, '\'
    je .nl
    cmp al, '%'
    je .lit
    cmp al, '&'
    je .lit
    cmp al, '$'
    je .lit
    cmp al, '#'
    je .lit
    cmp al, '_'
    je .lit
    cmp al, '{'
    je .lit
    cmp al, '}'
    je .lit
    cmp al, ' '
    je .sp
    cmp al, ','
    je .sp
    jmp .o
.nl:
    call tp_nl
    jmp .o
.sp:
    call tp_space
    jmp .o
.lit:
    mov [tp_wbuf], al
    mov byte [tp_wbuf+1], 0
    mov si, tp_wbuf
    call tp_emit_csi
    jmp .o
.word:
    mov di, tp_cmd
    xor cx, cx
.cl:
    cmp si, [tp_se]
    jae .cd
    mov al, [es:si]
    cmp al, 'A'
    jb .cd
    cmp al, 'Z'
    jbe .let
    cmp al, 'a'
    jb .cd
    cmp al, 'z'
    ja .cd
.let:
    cmp cx, 24
    jae .cd
    mov [di], al
    inc di
    inc si
    inc cx
    jmp .cl
.cd:
    mov [tp_si], si
    mov byte [di], 0
    mov [tp_cmdn], cx
    ; swallow one space
    cmp si, [tp_se]
    jae .disp
    mov al, [es:si]
    cmp al, ' '
    je .sw
    cmp al, 9
    jne .disp
.sw:
    inc word [tp_si]
.disp:
    call tp_cmd_disp
.o:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_isalpha:
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    jbe .yes
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    jbe .yes
.no:
    stc
    ret
.yes:
    clc
    ret

tp_cmd_disp:
    push bx
    push si
    push di
    mov bx, tp_cmdtab
.lp:
    mov si, [bx]
    or si, si
    jz .unk
    mov di, tp_cmd
    call tp_streq
    je .hit
    add bx, 4
    jmp .lp
.hit:
    mov ax, [bx+2]
    pop di
    pop si
    pop bx
    push ds
    pop es
    call ax
    ret
.unk:
    pop di
    pop si
    pop bx
    call tp_skip_opt
    call tp_peek
    cmp al, '{'
    jne .o
    call tp_read_group
.o:
    ret

; ZF=1 if DS:SI == DS:DI (NUL strings)
tp_streq:
    push ax
    push si
    push di
.lp:
    mov al, [si]
    cmp al, [di]
    jne .no
    inc si
    inc di
    or al, al
    jnz .lp
    pop di
    pop si
    pop ax
    xor ax, ax
    ret
.no:
    pop di
    pop si
    pop ax
    or al, 1
    ret

tp_c_nop:
    ret

tp_c_dclass:
    call tp_skip_opt_store
    ; look at tp_arg for 10pt/11pt/12pt
    mov si, tp_arg
    mov di, tp_k_10pt
    call tp_has
    jne .a11
    mov byte [tp_fsize], 10
    mov byte [tp_csize], 10
    mov byte [tp_tsize], 10
    mov word [tp_tlead], 12
    jmp .paper
.a11:
    mov si, tp_arg
    mov di, tp_k_11pt
    call tp_has
    jne .paper
    mov byte [tp_fsize], 11
    mov byte [tp_csize], 11
    mov byte [tp_tsize], 11
    mov word [tp_tlead], 13
.paper:
    mov si, tp_arg
    mov di, tp_k_legal
    call tp_has
    jne .a4
    mov byte [tp_psize], 1
    jmp .side
.a4:
    mov si, tp_arg
    mov di, tp_k_a4
    call tp_has
    jne .a5
    mov byte [tp_psize], 2
    jmp .side
.a5:
    mov si, tp_arg
    mov di, tp_k_a5
    call tp_has
    jne .ltr
    mov byte [tp_psize], 3
    jmp .side
.ltr:
    mov si, tp_arg
    mov di, tp_k_letter
    call tp_has
    jne .side
    mov byte [tp_psize], 0
.side:
    mov si, tp_arg
    mov di, tp_k_twoside
    call tp_has
    jne .cls
    mov byte [tp_bind], 1
.cls:
    call tp_read_group
    mov si, tp_arg
    mov di, tp_k_book
    call tp_has
    jne .geo
    mov byte [tp_class], 1
    cmp byte [tp_bind], 2
    je .geo
    mov byte [tp_bind], 1
.geo:
    call tp_geom
    ret

tp_c_skipg:
    call tp_skip_opt
    call tp_read_group
    ret

; \title, \author and \date all land in a fixed bss field, and the text going
; into them is the document's - so each copy carries the field's own ceiling,
; one short of the size in the bss map at the foot of texpad.asm. tp_date is
; the one that is actually smaller than a full tp_wbuf.
TP_TITLE_MAX  equ 79            ; tp_title, 80
TP_AUTHOR_MAX equ 79            ; tp_author, 80
TP_DATE_MAX   equ 39            ; tp_date, 40

; CX = how much of tp_nbuf is still free with DI where it is, so a heading can
; be assembled out of several pieces and the LAST one - the document's own
; text, the only piece with no length of its own - takes whatever is left.
; tp_nbuf is 48 bytes and the numbering in front of a \subsection title can be
; eight of them, so the ceiling has to be computed rather than assumed.
TP_NBUF_MAX equ 47              ; tp_nbuf, 48, less the NUL

tp_nbuf_left:
    mov cx, tp_nbuf + TP_NBUF_MAX
    sub cx, di
    jnc .o
    xor cx, cx
.o:
    ret

tp_c_title:
    call tp_read_group
    call tp_plain_arg
    mov si, tp_wbuf
    mov di, tp_title
    mov cx, TP_TITLE_MAX
    call tp_cpatn
    mov byte [di], 0
    ret

tp_c_author:
    call tp_read_group
    call tp_plain_arg
    mov si, tp_wbuf
    mov di, tp_author
    mov cx, TP_AUTHOR_MAX
    call tp_cpatn
    mov byte [di], 0
    ret

tp_c_date:
    call tp_read_group
    call tp_plain_arg
    mov si, tp_wbuf
    mov di, tp_date
    mov cx, TP_DATE_MAX
    call tp_cpatn
    mov byte [di], 0
    ret

tp_c_maketitle:
    cmp byte [tp_made], 0
    jne .done
    call tp_endpara
    sub word [tp_y], 12
    cmp byte [tp_title], 0
    je .au
    mov al, 18
    mov [tp_csize], al
    mov byte [tp_align], 1
    mov si, tp_title
    call tp_emit_line
    call tp_nl
    add word [tp_y], 0
    sub word [tp_y], 6
.au:
    mov al, [tp_tsize]
    mov [tp_csize], al
    cmp byte [tp_author], 0
    je .dt
    mov byte [tp_align], 1
    mov si, tp_author
    call tp_emit_line
    call tp_nl
.dt:
    cmp byte [tp_date], 0
    je .rule
    mov byte [tp_align], 1
    mov si, tp_date
    call tp_emit_line
    call tp_nl
.rule:
    mov byte [tp_align], 0
    mov ax, [tp_left]
    mov bx, [tp_y]
    mov cx, [tp_tw]
    xor dx, dx
    call tp_emit_box
    sub word [tp_y], 16
    mov byte [tp_made], 1
    mov byte [tp_needind], 1
.done:
    ret

tp_c_begin:
    call tp_read_group
    mov si, tp_arg
    mov di, tp_k_document
    call tp_has
    jne .ctr
    ; Only \maketitle builds the title block. Auto-running it here
    ; printed "Hello" / GUIDE titles twice.
    jmp .o
.ctr:
    mov si, tp_arg
    mov di, tp_k_center
    call tp_has
    jne .quo
    call tp_endpara
    call tp_sty_push
    mov byte [tp_align], 1
    mov al, 1
    call tp_env_push
    jmp .o
.quo:
    mov si, tp_arg
    mov di, tp_k_quote
    call tp_has
    je .doq
    mov si, tp_arg
    mov di, tp_k_abstract
    call tp_has
    jne .itm
    call tp_endpara
    mov byte [tp_align], 1
    mov si, tp_s_abs
    call tp_emit_line
    call tp_nl
    mov byte [tp_align], 0
.doq:
    call tp_endpara
    mov al, 2
    call tp_env_push
    add word [tp_left], 36
    sub word [tp_right], 36
    mov byte [tp_needind], 0
    jmp .o
.itm:
    mov si, tp_arg
    mov di, tp_k_itemize
    call tp_has
    je .doi
    mov si, tp_arg
    mov di, tp_k_enumerate
    call tp_has
    jne .tab
    mov al, 4
    jmp .doi2
.doi:
    mov al, 3
.doi2:
    call tp_endpara
    call tp_env_push
    add word [tp_left], 18
    mov byte [tp_itemn], 0
    mov byte [tp_needind], 0
    jmp .o
.tab:
    mov si, tp_arg
    mov di, tp_k_tabular
    call tp_has
    jne .tbl
    call tp_skip_opt
    call tp_read_group
    call tp_do_tabular
    jmp .o
.tbl:
    mov si, tp_arg
    mov di, tp_k_table
    call tp_has
    jne .o
    call tp_skip_opt
    mov al, 5
    call tp_env_push
.o:
    ret

tp_c_end:
    call tp_read_group
    mov si, tp_arg
    mov di, tp_k_document
    call tp_has
    jne .ctr
    mov ax, [tp_se]
    mov [tp_si], ax
    jmp .o
.ctr:
    mov si, tp_arg
    mov di, tp_k_center
    call tp_has
    jne .quo
    call tp_endpara
    call tp_sty_pop
    call tp_env_pop
    jmp .o
.quo:
    mov si, tp_arg
    mov di, tp_k_quote
    call tp_has
    je .unq
    mov si, tp_arg
    mov di, tp_k_abstract
    call tp_has
    jne .itm
.unq:
    call tp_endpara
    call tp_env_pop
    sub word [tp_left], 36
    add word [tp_right], 36
    jmp .o
.itm:
    mov si, tp_arg
    mov di, tp_k_itemize
    call tp_has
    je .uni
    mov si, tp_arg
    mov di, tp_k_enumerate
    call tp_has
    jne .o
.uni:
    call tp_endpara
    call tp_env_pop
    sub word [tp_left], 18
.o:
    ret

tp_c_section:
    call tp_skip_opt
    call tp_read_group
    call tp_plain_arg
    call tp_endpara
    sub word [tp_y], 10
    inc byte [tp_secno]
    mov byte [tp_subno], 0
    mov di, tp_nbuf
    mov al, [tp_secno]
    xor ah, ah
    call tp_u16_di
    mov al, '.'
    stosb
    mov al, ' '
    stosb
    mov si, tp_wbuf
    call tp_nbuf_left
    call tp_cpatn
    mov byte [di], 0
    mov al, 14
    mov [tp_csize], al
    mov byte [tp_align], 0
    mov si, tp_nbuf
    call tp_emit_line
    mov al, [tp_tsize]
    mov [tp_csize], al
    call tp_nl
    sub word [tp_y], 4
    mov byte [tp_needind], 1
    ret

tp_c_subsec:
    call tp_skip_opt
    call tp_read_group
    call tp_plain_arg
    call tp_endpara
    sub word [tp_y], 6
    inc byte [tp_subno]
    mov di, tp_nbuf
    mov al, [tp_secno]
    xor ah, ah
    call tp_u16_di
    mov al, '.'
    stosb
    mov al, [tp_subno]
    xor ah, ah
    call tp_u16_di
    mov al, ' '
    stosb
    mov si, tp_wbuf
    call tp_nbuf_left
    call tp_cpatn
    mov byte [di], 0
    mov si, tp_nbuf
    call tp_emit_line
    call tp_nl
    sub word [tp_y], 2
    mov byte [tp_needind], 1
    ret

tp_c_chapter:
    call tp_skip_opt
    call tp_read_group
    call tp_plain_arg
    call tp_endpara
    call tp_newpage
    inc byte [tp_chno]
    mov byte [tp_secno], 0
    mov di, tp_nbuf
    mov si, tp_s_chap
    call tp_cpat
    mov al, [tp_chno]
    xor ah, ah
    call tp_u16_di
    mov byte [di], 0
    mov byte [tp_align], 1
    mov si, tp_nbuf
    call tp_emit_line
    call tp_nl
    mov al, 18
    mov [tp_csize], al
    mov si, tp_wbuf
    call tp_emit_line
    mov al, [tp_tsize]
    mov [tp_csize], al
    mov byte [tp_align], 0
    call tp_nl
    sub word [tp_y], 12
    mov byte [tp_needind], 1
    ret

tp_c_para:
    call tp_read_group
    call tp_plain_arg
    call tp_endpara
    mov byte [tp_needind], 0
    mov byte [tp_bold], 1
    mov si, tp_wbuf
    call tp_emit_csi
    mov byte [tp_bold], 0
    call tp_space
    ret

tp_c_textbf:
    call tp_read_group
    mov al, [tp_bold]
    push ax
    mov byte [tp_bold], 1
    call tp_feed_arg
    pop ax
    mov [tp_bold], al
    ret

tp_c_emph:
    call tp_read_group
    mov al, [tp_under]
    push ax
    mov byte [tp_under], 1
    call tp_feed_arg
    pop ax
    mov [tp_under], al
    ret

tp_c_texttt:
    call tp_read_group
    call tp_feed_arg
    ret

; \textbackslash plus following letters is one token ("\title"), so
; wrap cannot leave a lone backslash on the line.
tp_c_bslash:
    push ax
    push cx
    push si
    push di
    push es
    mov di, tp_wbuf
    mov al, '\'
    stosb
    mov es, [tp_srcseg]
    mov si, [tp_si]
    xor cx, cx
.gl:
    cmp si, [tp_se]
    jae .ge
    mov al, [es:si]
    cmp al, 'A'
    jb .ge
    cmp al, 'Z'
    jbe .let
    cmp al, 'a'
    jb .ge
    cmp al, 'z'
    ja .ge
.let:
    cmp cx, 24
    jae .ge
    stosb
    inc si
    inc cx
    jmp .gl
.ge:
    mov [tp_si], si
    mov byte [di], 0
    mov si, tp_wbuf
    call tp_emit_csi
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

tp_c_nl:
    call tp_nl
    ret

tp_c_par:
    call tp_endpara
    ret

tp_c_noindent:
    mov byte [tp_needind], 0
    ret

tp_c_newpage:
    call tp_endpara
    call tp_newpage
    ret

tp_c_vspace:
    call tp_read_group
    call tp_parse_dim
    call tp_endpara
    sub [tp_y], ax
    mov ax, [tp_y]
    cmp ax, [tp_tbot]
    jae .o
    call tp_newpage
.o:
    ret

tp_c_big:
    call tp_endpara
    sub word [tp_y], 12
    ret

tp_c_med:
    call tp_endpara
    sub word [tp_y], 8
    ret

tp_c_sma:
    call tp_endpara
    sub word [tp_y], 4
    ret

tp_c_centering:
    mov byte [tp_align], 1
    ret

tp_c_item:
    call tp_endpara
    mov byte [tp_needind], 0
    mov byte [tp_inpara], 1
    mov ax, [tp_left]
    sub ax, 14
    mov [tp_x], ax
    ; enumerate vs itemize: env top
    cmp byte [tp_env_n], 0
    je .bul
    mov bl, [tp_env_n]
    dec bl
    xor bh, bh
    mov al, [tp_env+bx]
    cmp al, 4
    jne .bul
    inc byte [tp_itemn]
    mov di, tp_nbuf
    mov al, [tp_itemn]
    xor ah, ah
    call tp_u16_di
    mov al, '.'
    stosb
    mov byte [di], 0
    mov si, tp_nbuf
    call tp_emit_csi
    jmp .x
.bul:
    mov si, tp_s_bullet
    call tp_emit_csi
.x:
    mov ax, [tp_left]
    mov [tp_x], ax
    ret

tp_c_caption:
    call tp_read_group
    call tp_plain_arg
    call tp_endpara
    mov byte [tp_align], 1
    mov al, 10
    mov [tp_csize], al
    mov si, tp_wbuf
    call tp_emit_line
    mov al, [tp_tsize]
    mov [tp_csize], al
    mov byte [tp_align], 0
    call tp_nl
    ret

tp_c_cite:
    call tp_read_group
    call tp_plain_arg
    mov si, tp_s_lbr
    call tp_emit_csi
    mov si, tp_wbuf
    call tp_emit_csi
    mov si, tp_s_rbr
    call tp_emit_csi
    ret

tp_c_pstyle:
    call tp_read_group
    mov si, tp_arg
    mov di, tp_k_empty
    call tp_has
    jne .plain
    mov byte [tp_pstyle], 0
    jmp .o
.plain:
    mov si, tp_arg
    mov di, tp_k_head
    call tp_has
    jne .my
    mov byte [tp_pstyle], 2
    jmp .o
.my:
    mov si, tp_arg
    mov di, tp_k_myhead
    call tp_has
    jne .on
    mov byte [tp_pstyle], 3
    jmp .o
.on:
    mov byte [tp_pstyle], 1
.o:
    ret

tp_needgrp:
    call tp_skip_ws
    call tp_peek
    cmp al, '{'
    je .y
    stc
    ret
.y:
    clc
    ret

tp_emit_bsname:
    push si
    mov si, tp_s_bsl
    call tp_emit_csi
    mov si, tp_cmd
    call tp_emit_csi
    pop si
    ret

tp_c_oddm:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    cmp ax, 36
    jae .a
    add ax, 72
.a:
    mov [tp_left], ax
    mov [tp_tleft], ax
    add ax, [tp_tw]
    mov [tp_right], ax
    ret

tp_c_topm:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    cmp ax, 36
    jae .a
    add ax, 72
.a:
    mov bx, [tp_ph]
    sub bx, ax
    mov [tp_ttop], bx
    ret

tp_c_tw:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    mov [tp_tw], ax
    mov bx, [tp_left]
    add bx, ax
    mov [tp_right], bx
    ret

tp_c_th:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    ret

tp_c_pind:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    mov [tp_parind], ax
    ret

tp_c_pskip:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    mov [tp_parskip], ax
    ret

tp_c_base:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    mov [tp_tlead], ax
    ret

tp_c_tsep:
    call tp_needgrp
    jc tp_emit_bsname
    call tp_read_group
    call tp_parse_dim
    mov [tp_tabsep], ax
    ret

tp_c_large:
    mov al, [tp_tsize]
    add al, 2
    mov [tp_csize], al
    ret

tp_c_Large:
    mov al, [tp_tsize]
    add al, 4
    mov [tp_csize], al
    ret

tp_c_small:
    mov al, [tp_tsize]
    sub al, 2
    cmp al, 8
    jae .o
    mov al, 8
.o:
    mov [tp_csize], al
    ret

tp_c_norm:
    mov al, [tp_tsize]
    mov [tp_csize], al
    ret

tp_c_tex:
    mov si, tp_s_tex
    call tp_emit_csi
    ret

tp_c_latex:
    mov si, tp_s_latex
    call tp_emit_csi
    ret

tp_c_today:
    mov si, tp_s_today
    call tp_emit_csi
    ret

; ---- group / opt / dim ------------------------------------------------------
tp_peek:
    push si
    push es
    mov es, [tp_srcseg]
    mov si, [tp_si]
    xor al, al
    cmp si, [tp_se]
    jae .o
    mov al, [es:si]
.o:
    pop es
    pop si
    ret

tp_skip_ws:
    push ax
    push es
    mov es, [tp_srcseg]
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .o
    mov al, [es:si]
    cmp al, ' '
    je .s
    cmp al, 9
    jne .o
.s:
    inc word [tp_si]
    jmp .lp
.o:
    pop es
    pop ax
    ret

tp_skip_opt:
    call tp_skip_ws
    call tp_peek
    cmp al, '['
    jne .o
    inc word [tp_si]
    push ax
    push cx
    push es
    mov es, [tp_srcseg]
    mov cx, 1
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .d
    mov al, [es:si]
    inc word [tp_si]
    cmp al, '['
    jne .rb
    inc cx
    jmp .lp
.rb:
    cmp al, ']'
    jne .lp
    dec cx
    jnz .lp
.d:
    pop es
    pop cx
    pop ax
.o:
    ret

tp_skip_opt_store:
    mov byte [tp_arg], 0
    call tp_skip_ws
    call tp_peek
    cmp al, '['
    jne .o
    inc word [tp_si]
    push ax
    push cx
    push di
    push es
    mov es, [tp_srcseg]
    mov di, tp_arg
    mov cx, 1
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .d
    mov al, [es:si]
    inc word [tp_si]
    cmp al, '['
    jne .rb
    inc cx
    jmp .st
.rb:
    cmp al, ']'
    jne .st
    dec cx
    jz .d
.st:
    cmp di, tp_arg + TP_ARG_MAX - 2
    jae .lp
    mov [di], al
    inc di
    jmp .lp
.d:
    mov byte [di], 0
    pop es
    pop di
    pop cx
    pop ax
.o:
    ret

tp_read_group:
    call tp_skip_ws
    call tp_peek
    cmp al, '{'
    je .br
    ; one char / command
    mov byte [tp_arg], 0
    ret
.br:
    inc word [tp_si]
    push ax
    push cx
    push di
    push es
    mov es, [tp_srcseg]
    mov di, tp_arg
    mov cx, 1
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .d
    mov al, [es:si]
    inc word [tp_si]
    cmp al, '\'
    je .esc
    cmp al, '{'
    je .ob
    cmp al, '}'
    je .cb
    jmp .st
.esc:
    cmp di, tp_arg + TP_ARG_MAX - 3
    jae .lp
    mov [di], al
    inc di
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .lp
    mov al, [es:si]
    inc word [tp_si]
    jmp .st
.ob:
    inc cx
    jmp .st
.cb:
    dec cx
    jz .d
.st:
    cmp di, tp_arg + TP_ARG_MAX - 2
    jae .lp
    mov [di], al
    inc di
    jmp .lp
.d:
    mov byte [di], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; strip commands from tp_arg -> tp_wbuf
;
; The source is tp_arg, which tp_read_group fills to TP_ARG_MAX (180) from the
; document, and the destination is the 82-byte tp_wbuf that every other writer
; in here caps at 70. So the copy is capped at 70 as well: without it a
; \title{...} of more than 82 characters runs off the end of tp_wbuf and into
; tp_linebuf, with the overrun length under the .TEX file's control.
TP_WBUF_MAX equ 70

tp_plain_arg:
    push ax
    push cx
    push si
    push di
    mov si, tp_arg
    mov di, tp_wbuf
    mov cx, TP_WBUF_MAX
.lp:
    jcxz .z
    lodsb
    or al, al
    jz .z
    cmp al, '\'
    je .cmd
    cmp al, '{'
    je .lp
    cmp al, '}'
    je .lp
    cmp al, 10
    je .sp
    cmp al, 13
    je .sp
    cmp al, 9
    je .sp
    stosb
    dec cx
    jmp .lp
.sp:
    mov al, ' '
    stosb
    dec cx
    jmp .lp
.cmd:
    lodsb
    or al, al
    jz .z
    call tp_isalpha
    jc .esc
    ; named command: textbackslash -> '\'; else skip. CX is the tp_wbuf budget
    ; from .lp, and this inner loop needs a counter of its own - so it borrows
    ; CX and puts it back, rather than the budget silently becoming a command
    ; name's length.
    dec si
    push cx
    push di
    mov di, tp_cmd
    xor cx, cx
.en:
    mov al, [si]
    or al, al
    jz .ed
    call tp_isalpha
    jc .ed
    cmp cx, 24
    jae .ed
    mov [di], al
    inc di
    inc si
    inc cx
    jmp .en
.ed:
    mov byte [di], 0
    pop di
    pop cx
    push si
    mov si, tp_cmd
    push di
    mov di, tp_cn_bsl
    call tp_streq
    pop di
    pop si
    jne .grp
    mov al, '\'
    stosb
    dec cx
    jmp .lp
.esc:
    stosb
    dec cx
    jmp .lp
.grp:
    cmp byte [si], '{'
    jne .lp
    inc si
    jmp .lp
.z:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; parse number+unit in tp_arg -> AX points
tp_parse_dim:
    push bx
    push cx
    push dx
    push si
    mov si, tp_arg
    xor ax, ax
    xor bx, bx
    xor cx, cx                  ; 1 if seen digit
.lp:
    mov bl, [si]
    or bl, bl
    jz .num
    cmp bl, '0'
    jb .unit
    cmp bl, '9'
    ja .unit
    inc si
    mov ch, 1
    mov cl, 10
    mul cl
    xor bh, bh
    sub bl, '0'
    add ax, bx
    jmp .lp
.unit:
    cmp bl, ' '
    jne .u
    inc si
    jmp .lp
.u:
    ; in / em / pt
    cmp byte [si], 'i'
    je .in
    cmp byte [si], 'e'
    je .em
    jmp .num
.in:
    mov bx, 72
    mul bx
    jmp .done
.em:
    mov bl, [tp_csize]
    xor bh, bh
    mul bx
    jmp .done
.num:
    or ax, ax
    jnz .done
    mov ax, 12
.done:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; SI = haystack, DI = needle (both NUL). ZF=1 if needle occurs in haystack.
tp_has:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, di
    mov cx, 200
.scan:
    dec cx
    jz .no
    mov al, [si]
    or al, al
    jz .no
    push si
    push di
.cmp:
    mov al, [di]
    or al, al
    jz .yes
    cmp al, [si]
    jne .nx
    inc si
    inc di
    jmp .cmp
.nx:
    pop di
    pop si
    inc si
    jmp .scan
.yes:
    pop di
    pop si
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    xor ax, ax
    ret
.no:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    or al, 1
    ret

tp_k_10pt:      db '10pt', 0
tp_k_11pt:      db '11pt', 0
tp_k_book:      db 'book', 0
tp_k_document:  db 'document', 0
tp_k_center:    db 'center', 0
tp_k_quote:     db 'quote', 0
tp_k_abstract:  db 'abstract', 0
tp_k_itemize:   db 'itemize', 0
tp_k_enumerate: db 'enumerate', 0
tp_k_tabular:   db 'tabular', 0
tp_k_table:     db 'table', 0
tp_k_empty:     db 'empty', 0
tp_k_letter:    db 'letterpaper', 0
tp_k_legal:     db 'legalpaper', 0
tp_k_a4:        db 'a4paper', 0
tp_k_a5:        db 'a5paper', 0
tp_k_twoside:   db 'twoside', 0
tp_k_head:      db 'headings', 0
tp_k_myhead:    db 'myheadings', 0

tp_cmdtab:
    dw tp_cn_dclass, tp_c_dclass
    dw tp_cn_usep, tp_c_skipg
    dw tp_cn_title, tp_c_title
    dw tp_cn_author, tp_c_author
    dw tp_cn_date, tp_c_date
    dw tp_cn_make, tp_c_maketitle
    dw tp_cn_begin, tp_c_begin
    dw tp_cn_end, tp_c_end
    dw tp_cn_sec, tp_c_section
    dw tp_cn_sub, tp_c_subsec
    dw tp_cn_ch, tp_c_chapter
    dw tp_cn_parag, tp_c_para
    dw tp_cn_bf, tp_c_textbf
    dw tp_cn_emph, tp_c_emph
    dw tp_cn_it, tp_c_emph
    dw tp_cn_ul, tp_c_emph
    dw tp_cn_tt, tp_c_texttt
    dw tp_cn_bsl, tp_c_bslash
    dw tp_cn_nl, tp_c_nl
    dw tp_cn_par, tp_c_par
    dw tp_cn_ni, tp_c_noindent
    dw tp_cn_np, tp_c_newpage
    dw tp_cn_cp, tp_c_newpage
    dw tp_cn_pb, tp_c_newpage
    dw tp_cn_vs, tp_c_vspace
    dw tp_cn_big, tp_c_big
    dw tp_cn_med, tp_c_med
    dw tp_cn_sma, tp_c_sma
    dw tp_cn_cen, tp_c_centering
    dw tp_cn_item, tp_c_item
    dw tp_cn_cap, tp_c_caption
    dw tp_cn_lab, tp_c_skipg
    dw tp_cn_cite, tp_c_cite
    dw tp_cn_ps, tp_c_pstyle
    dw tp_cn_od, tp_c_oddm
    dw tp_cn_tm, tp_c_topm
    dw tp_cn_tw, tp_c_tw
    dw tp_cn_th, tp_c_th
    dw tp_cn_pi, tp_c_pind
    dw tp_cn_psk, tp_c_pskip
    dw tp_cn_bs, tp_c_base
    dw tp_cn_tcs, tp_c_tsep
    dw tp_cn_hl, tp_c_nop
    dw tp_cn_lg, tp_c_large
    dw tp_cn_Lg, tp_c_Large
    dw tp_cn_sm, tp_c_small
    dw tp_cn_ns, tp_c_norm
    dw tp_cn_tex, tp_c_tex
    dw tp_cn_ltx, tp_c_latex
    dw tp_cn_td, tp_c_today
    dw 0

tp_cn_dclass:   db 'documentclass', 0
tp_cn_usep:     db 'usepackage', 0
tp_cn_title:    db 'title', 0
tp_cn_author:   db 'author', 0
tp_cn_date:     db 'date', 0
tp_cn_make:     db 'maketitle', 0
tp_cn_begin:    db 'begin', 0
tp_cn_end:      db 'end', 0
tp_cn_sec:      db 'section', 0
tp_cn_sub:      db 'subsection', 0
tp_cn_ch:       db 'chapter', 0
tp_cn_parag:    db 'paragraph', 0
tp_cn_bf:       db 'textbf', 0
tp_cn_emph:     db 'emph', 0
tp_cn_it:       db 'textit', 0
tp_cn_ul:       db 'underline', 0
tp_cn_tt:       db 'texttt', 0
tp_cn_bsl:      db 'textbackslash', 0
tp_cn_nl:       db 'newline', 0
tp_cn_par:      db 'par', 0
tp_cn_ni:       db 'noindent', 0
tp_cn_np:       db 'newpage', 0
tp_cn_cp:       db 'clearpage', 0
tp_cn_pb:       db 'pagebreak', 0
tp_cn_vs:       db 'vspace', 0
tp_cn_big:      db 'bigskip', 0
tp_cn_med:      db 'medskip', 0
tp_cn_sma:      db 'smallskip', 0
tp_cn_cen:      db 'centering', 0
tp_cn_item:     db 'item', 0
tp_cn_cap:      db 'caption', 0
tp_cn_lab:      db 'label', 0
tp_cn_cite:     db 'cite', 0
tp_cn_ps:       db 'pagestyle', 0
tp_cn_od:       db 'oddsidemargin', 0
tp_cn_tm:       db 'topmargin', 0
tp_cn_tw:       db 'textwidth', 0
tp_cn_th:       db 'textheight', 0
tp_cn_pi:       db 'parindent', 0
tp_cn_psk:      db 'parskip', 0
tp_cn_bs:       db 'baselineskip', 0
tp_cn_tcs:      db 'tabcolsep', 0
tp_cn_hl:       db 'hline', 0
tp_cn_lg:       db 'large', 0
tp_cn_Lg:       db 'Large', 0
tp_cn_sm:       db 'small', 0
tp_cn_ns:       db 'normalsize', 0
tp_cn_tex:      db 'TeX', 0
tp_cn_ltx:      db 'LaTeX', 0
tp_cn_td:       db 'today', 0

; ---- emit / wrap ------------------------------------------------------------
; One em = body point size. Preview maps 8px per em, so 12pt is
; one 8x8 cell. The old 0.6em width collapsed to ~5px and words overlapped.
tp_charw:
    mov al, [tp_csize]
    xor ah, ah
    or ax, ax
    jnz .s
    mov al, 12
.s:
    ret

tp_strlen:
    push si
    xor ax, ax
.lp:
    cmp byte [si], 0
    je .o
    inc ax
    inc si
    jmp .lp
.o:
    pop si
    ret

tp_emit_csi:
    ; SI = DS NUL string, current style
    push ax
    push bx
    push cx
    push dx
    push si
    call tp_strlen
    mov cx, ax
    jcxz .o
    call tp_ensure
    call tp_charw
    mov bl, al
    xor bh, bh
    mul cl                      ; AX = width  (CL * charw) - only if CX<256
    ; use BX * CX more carefully
    mov al, [tp_csize]
    ; recompute width = len * charw
    call tp_charw
    mov bl, al
    xor bh, bh
    mov ax, cx
    mul bx                      ; DX:AX width
    mov dx, ax                  ; width
    cmp byte [tp_align], 0
    je .left
    call tp_c_append
    jmp .o
.left:
    mov ax, [tp_x]
    cmp ax, [tp_left]
    jbe .fit
    add ax, dx
    cmp ax, [tp_right]
    jbe .fit
    call tp_nl
.fit:
    call tp_put_run
    add [tp_x], dx
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_emit_str:
    ; SI DS string CX len (not necessarily NUL)
    push ax
    push si
    mov di, tp_wbuf
    cmp cx, 70
    jbe .c
    mov cx, 70
.c:
    push cx
    jcxz .z
    rep movsb
.z:
    pop cx
    mov bx, cx
    mov byte [tp_wbuf+bx], 0
    mov si, tp_wbuf
    call tp_emit_csi
    pop si
    pop ax
    ret

tp_emit_line:
    ; SI whole line, honor align
    push ax
    push bx
    call tp_flush_c
    call tp_ensure
    cmp byte [tp_align], 0
    je .l
    call tp_strlen
    call tp_charw
    mov bl, al
    xor bh, bh
    mul bl                      ; wait AX was len, then charw overwrote
    ; redo
    call tp_strlen
    mov cx, ax
    call tp_charw
    mov bl, al
    xor bh, bh
    mov ax, cx
    mul bx
    mov cx, [tp_right]
    sub cx, [tp_left]
    sub cx, ax
    jns .p
    xor cx, cx
.p:
    shr cx, 1
    mov ax, [tp_left]
    add ax, cx
    mov [tp_x], ax
    call tp_put_run
    jmp .o
.l:
    mov ax, [tp_left]
    mov [tp_x], ax
    call tp_put_run
.o:
    pop bx
    pop ax
    ret

tp_c_append:
    ; append SI to cline
    push ax
    push si
    push di
    mov di, tp_cline
    xor ah, ah
    mov al, [tp_clen]
    add di, ax
.lp:
    lodsb
    or al, al
    jz .z
    cmp byte [tp_clen], TP_CLINE_MAX - 1
    jae .z
    mov [di], al
    inc di
    inc byte [tp_clen]
    jmp .lp
.z:
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

tp_flush_c:
    cmp byte [tp_clen], 0
    je .o
    push ax
    push bx
    push cx
    push si
    mov byte [tp_cline + TP_CLINE_MAX], 0
    xor ah, ah
    mov al, [tp_clen]
    mov bx, ax
    mov byte [tp_cline+bx], 0
    mov si, tp_cline
    mov byte [tp_align], 0      ; emit_line uses align; we center here
    call tp_strlen
    mov cx, ax
    call tp_charw
    mov bl, al
    xor bh, bh
    mov ax, cx
    mul bx
    mov cx, [tp_right]
    sub cx, [tp_left]
    sub cx, ax
    jns .p
    xor cx, cx
.p:
    shr cx, 1
    mov ax, [tp_left]
    add ax, cx
    mov [tp_x], ax
    mov si, tp_cline
    call tp_put_run
    mov byte [tp_clen], 0
    pop si
    pop cx
    pop bx
    pop ax
.o:
    ret

tp_ensure:
    cmp byte [tp_inpara], 0
    jne .o
    mov byte [tp_inpara], 1
    cmp byte [tp_needind], 0
    je .ni
    cmp byte [tp_align], 0
    jne .ni
    mov ax, [tp_left]
    add ax, [tp_parind]
    mov [tp_x], ax
    jmp .d
.ni:
    mov ax, [tp_left]
    mov [tp_x], ax
.d:
    mov byte [tp_needind], 0
.o:
    ret

tp_space:
    cmp byte [tp_inpara], 0
    je .o
    cmp byte [tp_align], 0
    je .l
    mov si, tp_s_spch
    call tp_c_append
    jmp .o
.l:
    mov cx, [tp_left]
    cmp [tp_x], cx
    jbe .o
    mov si, tp_s_spch
    call tp_emit_csi
.o:
    ret

tp_nl:
    call tp_flush_c
    push ax
    mov ax, [tp_tlead]
    cmp al, [tp_csize]
    jae .s
    mov al, [tp_csize]
    add al, 4
    xor ah, ah
.s:
    sub [tp_y], ax
    mov ax, [tp_y]
    cmp ax, [tp_tbot]
    jae .x
    call tp_newpage
    jmp .d
.x:
    mov ax, [tp_left]
    add ax, [tp_indent]
    mov [tp_x], ax
.d:
    mov word [tp_indent], 0
    pop ax
    ret

tp_endpara:
    call tp_flush_c
    cmp byte [tp_inpara], 0
    je .o
    push ax
    call tp_nl
    mov ax, [tp_parskip]
    sub [tp_y], ax
    mov byte [tp_inpara], 0
    mov byte [tp_needind], 1
    mov ax, [tp_left]
    mov [tp_x], ax
    pop ax
.o:
    ret

tp_newpage:
    call tp_flush_c
    cmp byte [tp_onpage], 0
    je .o
    call tp_footer
    inc word [tp_page]
    inc word [tp_npages]
    cmp word [tp_npages], TP_MAX_PAGES
    jbe .ok
    mov word [tp_npages], TP_MAX_PAGES
    dec word [tp_page]
.ok:
    call tp_geom
    mov ax, [tp_ttop]
    mov [tp_y], ax
    mov ax, [tp_left]
    mov [tp_x], ax
    mov word [tp_indent], 0
    mov byte [tp_onpage], 0
.o:
    ret

tp_footer:
    cmp byte [tp_pstyle], 0
    je .o
    push ax
    push bx
    push cx
    push si
    push di
    mov di, tp_nbuf
    mov ax, [tp_page]
    inc ax
    call tp_u16_di
    mov byte [di], 0
    mov si, tp_nbuf
    call tp_strlen
    mov cx, ax
    mov al, 10
    push word [tp_csize]
    mov [tp_csize], al
    call tp_charw
    mov bl, al
    xor bh, bh
    mov ax, cx
    mul bx
    mov cx, ax                  ; text width
    mov bx, 36
    cmp byte [tp_pstyle], 2
    jne .yok
    mov bx, [tp_ph]
    sub bx, 28
.yok:
    cmp byte [tp_pstyle], 3
    je .outer
    mov ax, [tp_pw]
    sub ax, cx
    shr ax, 1
    jmp .put
.outer:
    mov ax, [tp_page]
    and ax, 1
    jnz .ol
    mov ax, [tp_pw]
    sub ax, cx
    sub ax, 36
    jmp .put
.ol:
    mov ax, 36
.put:
    mov si, tp_nbuf
    call tp_put_run_at
    pop word [tp_csize]
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.o:
    ret

; SI string at current x,y
tp_put_run:
    mov ax, [tp_x]
    mov bx, [tp_y]
    sub bx, 2
    call tp_put_run_at
    ret

; AX=x BX=y SI=NUL
tp_put_run_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [tp_rx], ax
    mov [tp_ry], bx
    mov cx, [tp_nruns]
    cmp cx, TP_MAX_RUNS
    jae .o
    call tp_strlen
    or ax, ax
    jz .o
    cmp ax, 255
    jbe .ln
    mov ax, 255
.ln:
    mov cx, ax
    mov ax, [tp_tused]
    add ax, cx
    cmp ax, TP_TEXT_MAX
    jae .o
    mov di, [tp_tused]
    add di, tp_text
    push cx
    push si
    push ds
    pop es
    rep movsb
    pop si
    pop cx
    mov ax, [tp_nruns]
    mov dx, TP_RUN_SZ
    mul dx
    mov di, ax
    add di, tp_runs
    mov al, [tp_page]
    mov [di], al
    mov al, [tp_bold]
    and al, 1
    mov ah, [tp_under]
    and ah, 1
    shl ah, 1
    or al, ah
    mov [di+1], al
    mov ax, [tp_rx]
    mov [di+2], ax
    mov ax, [tp_ry]
    mov [di+4], ax
    mov al, [tp_csize]
    mov [di+6], al
    mov [di+7], cl
    mov ax, [tp_tused]
    mov [di+8], ax
    add [tp_tused], cx
    inc word [tp_nruns]
    mov byte [tp_onpage], 1
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_emit_box:
    ; AX=x BX=y CX=w DX=h
    push ax
    push bx
    push cx
    push dx
    push di
    mov si, [tp_nboxes]
    cmp si, TP_MAX_BOXES
    jae .o
    push ax
    mov ax, si
    mov si, TP_BOX_SZ
    mul si
    mov di, ax
    add di, tp_boxes
    pop ax
    push ax
    mov al, [tp_page]
    mov [di], al
    mov byte [di+1], 0
    pop ax
    or cx, cx
    jnz .w
    inc cx
.w:
    or dx, dx
    jnz .h
    inc dx
.h:
    mov [di+2], ax
    mov [di+4], bx
    mov [di+6], cx
    mov [di+8], dx
    inc word [tp_nboxes]
    mov byte [tp_onpage], 1
.o:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_sty_push:
    push ax
    push bx
    mov al, [tp_sty_n]
    cmp al, TP_STY_MAX
    jae .o
    xor ah, ah
    mov bl, 4
    mul bl
    mov bx, ax
    add bx, tp_sty
    mov al, [tp_bold]
    mov [bx], al
    mov al, [tp_under]
    mov [bx+1], al
    mov al, [tp_csize]
    mov [bx+2], al
    mov al, [tp_align]
    mov [bx+3], al
    inc byte [tp_sty_n]
.o:
    pop bx
    pop ax
    ret

tp_sty_pop:
    push ax
    push bx
    cmp byte [tp_sty_n], 0
    je .o
    dec byte [tp_sty_n]
    mov al, [tp_sty_n]
    xor ah, ah
    mov bl, 4
    mul bl
    mov bx, ax
    add bx, tp_sty
    mov al, [bx]
    mov [tp_bold], al
    mov al, [bx+1]
    mov [tp_under], al
    mov al, [bx+2]
    mov [tp_csize], al
    mov al, [bx+3]
    mov [tp_align], al
.o:
    pop bx
    pop ax
    ret

tp_env_push:
    ; AL = type
    push bx
    mov bl, [tp_env_n]
    cmp bl, TP_ENV_MAX
    jae .o
    xor bh, bh
    mov [tp_env+bx], al
    inc byte [tp_env_n]
.o:
    pop bx
    ret

tp_env_pop:
    cmp byte [tp_env_n], 0
    je .o
    dec byte [tp_env_n]
.o:
    ret

; feed tp_arg as text (simple)
tp_feed_arg:
    push ax
    push si
    push di
    mov si, tp_arg
.lp:
    mov al, [si]
    or al, al
    jz .o
    cmp al, '\'
    je .cmd
    cmp al, ' '
    je .sp
    cmp al, 9
    je .sp
    cmp al, 10
    je .sp
    cmp al, 13
    je .sp
    cmp al, '$'
    je .sk1
    cmp al, '{'
    je .sk1
    cmp al, '}'
    je .sk1
    ; word
    mov di, tp_wbuf
.w:
    mov al, [si]
    or al, al
    jz .ew
    cmp al, ' '
    je .ew
    cmp al, 9
    je .ew
    cmp al, '\'
    je .ew
    cmp al, '{'
    je .ew
    cmp al, '}'
    je .ew
    cmp al, '$'
    je .ew
    mov [di], al
    inc di
    inc si
    jmp .w
.ew:
    mov byte [di], 0
    push si
    mov si, tp_wbuf
    cmp byte [si], 0
    je .n
    call tp_emit_csi
.n:
    pop si
    jmp .lp
.sp:
    inc si
    call tp_space
    jmp .lp
.sk1:
    inc si
    jmp .lp
.cmd:
    inc si
    mov al, [si]
    or al, al
    jz .o
    call tp_isalpha
    jc .escch
    ; named: textbackslash -> '\'; other names are dropped
    mov di, tp_cmd
    xor cx, cx
.en:
    mov al, [si]
    or al, al
    jz .ed
    call tp_isalpha
    jc .ed
    cmp cx, 24
    jae .ed
    mov [di], al
    inc di
    inc si
    inc cx
    jmp .en
.ed:
    mov byte [di], 0
    push si
    mov si, tp_cmd
    mov di, tp_cn_bsl
    call tp_streq
    pop si
    jne .lp
    ; \textbackslash + letters -> one run ("\documentclass")
    mov di, tp_wbuf
    mov al, '\'
    stosb
    xor cx, cx
.gl:
    mov al, [si]
    or al, al
    jz .ge
    cmp al, 'A'
    jb .ge
    cmp al, 'Z'
    jbe .glet
    cmp al, 'a'
    jb .ge
    cmp al, 'z'
    ja .ge
.glet:
    cmp cx, 24
    jae .ge
    stosb
    inc si
    inc cx
    jmp .gl
.ge:
    mov byte [di], 0
    push si
    mov si, tp_wbuf
    call tp_emit_csi
    pop si
    jmp .lp
.escch:
    ; \% \{ \} \\ \& \$ \# \_
    mov [tp_wbuf], al
    mov byte [tp_wbuf+1], 0
    inc si
    push si
    mov si, tp_wbuf
    call tp_emit_csi
    pop si
    jmp .lp
.o:
    pop di
    pop si
    pop ax
    ret

; ---- tabular ----------------------------------------------------------------
tp_do_tabular:
    ; tp_arg holds column spec
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tp_endpara
    call tp_tab_spec
    call tp_tab_body
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_tab_spec:
    push ax
    push si
    push di
    mov si, tp_arg
    xor di, di                  ; col count
    xor ah, ah                  ; pending bar
    mov byte [tp_colbar], 0
.lp:
    lodsb
    or al, al
    jz .d
    cmp al, '|'
    jne .col
    mov ah, 1
    jmp .lp
.col:
    cmp al, 'l'
    je .ok
    cmp al, 'c'
    je .ok
    cmp al, 'r'
    je .ok
    cmp al, 'p'
    jne .lp
    mov al, 'l'
    ; skip {..}
    cmp byte [si], '{'
    jne .ok
    inc si
    mov cl, 1
.sk:
    lodsb
    or al, al
    jz .ok
    cmp al, '{'
    jne .rc
    inc cl
    jmp .sk
.rc:
    cmp al, '}'
    jne .sk
    dec cl
    jnz .sk
    mov al, 'l'
.ok:
    cmp di, TP_COL_MAX
    jae .lp
    mov [tp_colk+di], al
    mov [tp_colbar+di], ah
    xor ah, ah
    inc di
    jmp .lp
.d:
    mov [tp_colbar+di], ah       ; trailing
    or di, di
    jnz .n
    inc di
    mov byte [tp_colk], 'l'
    mov byte [tp_colbar], 0
.n:
    mov [tp_tmp4], di           ; ncols
    pop di
    pop si
    pop ax
    ret

tp_tab_body:
    ; gather rows from source until \end{tabular}
    ; We measure and emit in one walk using a simple row buffer in linebuf.
    ; First pass: measure col widths. Second: emit.
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov ax, [tp_si]
    mov [tp_tmp], ax            ; body start
    ; measure
    xor ax, ax
    mov cx, TP_COL_MAX
    mov di, tp_colw
    xor ax, ax
    push cx
    push di
    xor ax, ax
    mov cx, TP_COL_MAX
    rep stosw
    pop di
    pop cx
    mov word [tp_tmp2], 0       ; nrows
    mov word [tp_tmp3], 0       ; hline flags packed? skip, always rule if seen
    call tp_tab_measure
    ; emit
    mov ax, [tp_tmp]
    mov [tp_si], ax
    call tp_tab_emit
    ; skip \end{tabular}
    call tp_tab_skip_end
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_tab_skip_end:
    push ax
    push cx
    push si
    push es
    mov es, [tp_srcseg]
    mov si, [tp_si]
    mov cx, 20
.lp:
    cmp si, [tp_se]
    jae .o
    cmp byte [es:si], '\'
    je .try
    inc si
    loop .lp
    jmp .o
.try:
    ; match end{tabular}
    push si
    inc si
    push di
    mov di, tp_s_endtab
    ; compare
.m:
    mov al, [di]
    or al, al
    jz .hit
    cmp al, [es:si]
    jne .no
    inc si
    inc di
    jmp .m
.hit:
    pop di
    pop ax
    mov [tp_si], si
    jmp .o
.no:
    pop di
    pop si
    inc si
    jmp .lp
.o:
    pop es
    pop si
    pop cx
    pop ax
    ret

tp_tab_measure:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov es, [tp_srcseg]
    xor bx, bx                  ; col
    xor cx, cx                  ; cell len
    mov word [tp_tmp2], 0
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .d
    ; \end{tabular}?
    cmp byte [es:si], '\'
    jne .ch
    call tp_tab_at_end
    jc .cmd
    jmp .d
.cmd:
    inc si
    ; hline or double-backslash
    cmp byte [es:si], '\'
    jne .hl
    add si, 1
    mov [tp_si], si
    call tp_tab_note
    xor bx, bx
    xor cx, cx
    inc word [tp_tmp2]
    jmp .lp
.hl:
    ; skip command name
    mov [tp_si], si
    inc word [tp_si]
    jmp .skc
.ch:
    mov al, [es:si]
    inc si
    mov [tp_si], si
    cmp al, '&'
    je .amp
    cmp al, '%'
    je .com
    cmp al, 10
    je .lp
    cmp al, 13
    je .lp
    inc cx
    jmp .lp
.amp:
    call tp_tab_note
    inc bx
    xor cx, cx
    jmp .lp
.com:
    ; skip to nl
.cs:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .lp
    mov al, [es:si]
    inc word [tp_si]
    cmp al, 10
    je .lp
    jmp .cs
.skc:
    ; skip letters
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .lp
    mov al, [es:si]
    call tp_isalpha
    jc .lp
    inc word [tp_si]
    jmp .skc
.d:
    ; last row if any
    or cx, cx
    jnz .last
    or bx, bx
    jz .o
.last:
    call tp_tab_note
    inc word [tp_tmp2]
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; CX = cell char count, BX = col. update colw
tp_tab_note:
    push ax
    push bx
    push dx
    cmp bx, TP_COL_MAX
    jae .o
    call tp_charw
    xor ah, ah
    mul cx                      ; width
    add ax, [tp_tabsep]
    add ax, [tp_tabsep]
    shl bx, 1
    cmp [tp_colw+bx], ax
    jae .o
    mov [tp_colw+bx], ax
.o:
    pop dx
    pop bx
    pop ax
    ret

; CF=0 if at \end{tabular}
tp_tab_at_end:
    push ax
    push si
    push di
    mov si, [tp_si]
    cmp byte [es:si], '\'
    jne .no
    inc si
    mov di, tp_s_endtab
.m:
    mov al, [di]
    or al, al
    jz .yes
    cmp al, [es:si]
    jne .no
    inc si
    inc di
    jmp .m
.yes:
    pop di
    pop si
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop ax
    stc
    ret

tp_tab_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; total width
    xor ax, ax
    mov cx, [tp_tmp4]
    or cx, cx
    jnz .tw0
    inc cx
.tw0:
    xor si, si
.tw:
    add ax, [tp_colw+si]
    add si, 2
    loop .tw
    mov [tp_tmp5], ax           ; table width
    mov [tp_tabw], ax
    mov ax, [tp_tmp4]
    mov [tp_tabn], ax
    ; Row must clear an em of type plus the rules. 16pt was too
    ; short: 12pt glyphs sat on the bottom rule.
    mov al, [tp_csize]
    xor ah, ah
    add ax, 10
    cmp ax, 20
    jae .rh
    mov ax, 20
.rh:
    mov [tp_rowh], ax
    mov ax, [tp_rowh]
    mov bx, [tp_tmp2]
    or bx, bx
    jnz .nr
    inc bx
.nr:
    mul bx
    mov cx, ax                  ; table h
    mov ax, [tp_y]
    sub ax, cx
    cmp ax, [tp_tbot]
    jae .fit
    call tp_newpage
.fit:
    mov ax, [tp_y]
    mov [tp_tabtop], ax
    mov [tp_tabbot], ax
    mov [tp_tmp3], ax
    ; walk rows again
    mov es, [tp_srcseg]
    xor bx, bx                  ; col
    mov di, tp_linebuf          ; cell
    mov byte [tp_linebuf], 0
    mov word [tp_clen], 0       ; used as cell len here - careful
    mov byte [tp_clen], 0
    mov ax, [tp_y]
    mov [tp_tmp2], ax           ; current row top
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .fin
    call tp_tab_at_end
    jnc .fin
    mov al, [es:si]
    cmp al, '\'
    je .bs
    inc word [tp_si]
    cmp al, '&'
    je .amp
    cmp al, '%'
    je .com
    cmp al, 10
    je .lp
    cmp al, 13
    je .lp
    ; append char
    push bx
    xor bh, bh
    mov bl, [tp_clen]
    cmp bl, 70
    jae .skc2
    mov [tp_linebuf+bx], al
    inc byte [tp_clen]
    inc bx
    mov byte [tp_linebuf+bx], 0
.skc2:
    pop bx
    jmp .lp
.amp:
    call tp_tab_cell
    inc bx
    mov byte [tp_clen], 0
    mov byte [tp_linebuf], 0
    jmp .lp
.bs:
    inc si
    cmp byte [es:si], '\'
    jne .cmd
    inc si
    mov [tp_si], si
    call tp_tab_cell
    ; next row
    mov ax, [tp_tmp2]
    sub ax, [tp_rowh]
    mov [tp_tmp2], ax
    mov [tp_tabbot], ax
    mov [tp_y], ax
    xor bx, bx
    mov byte [tp_clen], 0
    mov byte [tp_linebuf], 0
    jmp .lp
.cmd:
    inc word [tp_si]            ; past backslash
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .lp
    mov al, [es:si]
    call tp_isalpha
    jc .tesc
    ; named: textbackslash -> '\'; hline skipped
    mov di, tp_cmd
    xor cx, cx
.tn:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .td
    mov al, [es:si]
    call tp_isalpha
    jc .td
    cmp cx, 24
    jae .td
    mov [di], al
    inc di
    inc word [tp_si]
    inc cx
    jmp .tn
.td:
    mov byte [di], 0
    mov si, tp_cmd
    mov di, tp_cn_bsl
    call tp_streq
    jne .lp
    mov al, '\'
    jmp .tadd
.tesc:
    inc word [tp_si]
    ; fall through, AL is the escaped char
.tadd:
    push bx
    xor bh, bh
    mov bl, [tp_clen]
    cmp bl, 70
    jae .tad2
    mov [tp_linebuf+bx], al
    inc byte [tp_clen]
    inc bx
    mov byte [tp_linebuf+bx], 0
.tad2:
    pop bx
    jmp .lp
.com:
.cs:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .lp
    mov al, [es:si]
    inc word [tp_si]
    cmp al, 10
    je .lp
    jmp .cs
.fin:
    cmp byte [tp_clen], 0
    je .grid
    call tp_tab_cell
.grid:
    ; restore y to bottom
    mov ax, [tp_tmp2]
    sub ax, 8
    mov [tp_y], ax
    call tp_tab_grid
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; emit current cell at col BX, row top tmp2
tp_tab_cell:
    push ax
    push bx
    push cx
    push dx
    push si
    ; BX counts the & separators in the ROW, and nothing in a .TEX file makes a
    ; row agree with the \begin{tabular}{...} preamble - so a row with more
    ; cells than there are columns arrives here with BX past the end of both
    ; tp_colw (TP_COL_MAX words) and tp_colk. tp_tab_note already refuses to
    ; WRITE past them; this is the reading side, and unclamped it walks off both
    ; and lays the cell out from whatever follows in the bss. Extra cells stack
    ; up in the last column, which is what the eye expects from a ragged row.
    cmp bx, TP_COL_MAX
    jb .col
    mov bx, TP_COL_MAX - 1
.col:
    ; x = left + sum of prior colw
    mov ax, [tp_left]
    xor si, si
    mov cx, bx
    jcxz .x
.add:
    add ax, [tp_colw+si]
    add si, 2
    loop .add
.x:
    add ax, [tp_tabsep]
    ; alignment
    mov cl, [tp_colk+bx]
    push ax
    mov si, tp_linebuf
    call tp_strlen
    mov cx, ax
    call tp_charw
    mov dl, al
    xor dh, dh
    mov ax, cx
    mul dx
    mov dx, ax                  ; text w
    pop ax
    mov ch, [tp_colk+bx]
    cmp ch, 'c'
    je .c
    cmp ch, 'r'
    je .r
    jmp .put
.c:
    push ax
    mov ax, bx
    shl ax, 1
    mov si, ax
    mov ax, [tp_colw+si]
    sub ax, [tp_tabsep]
    sub ax, [tp_tabsep]
    sub ax, dx
    jns .c2
    xor ax, ax
.c2:
    shr ax, 1
    pop si
    add ax, si
    jmp .put
.r:
    push ax
    mov ax, bx
    shl ax, 1
    mov si, ax
    mov ax, [tp_colw+si]
    sub ax, [tp_tabsep]
    sub ax, [tp_tabsep]
    sub ax, dx
    jns .r2
    xor ax, ax
.r2:
    pop si
    add ax, si
.put:
    mov si, tp_linebuf
    cmp byte [si], 0
    je .o
    ; PDF y is the top of the 8px run. Hang it a few points
    ; under the top rule, not on the bottom rule.
    mov bx, [tp_tmp2]
    sub bx, 5
    call tp_put_run_at
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_tab_grid:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [tp_tabtop]
    sub ax, [tp_tabbot]
    cmp ax, 8
    jae .hh
    mov ax, 8
.hh:
    mov dx, ax
    mov ax, [tp_left]
    mov bx, [tp_tabbot]
    mov cx, [tp_tabw]
    or cx, cx
    jnz .ww
    mov cx, 120
.ww:
    call tp_emit_box
    mov ax, [tp_left]
    xor si, si
.v:
    cmp si, [tp_tabn]
    jae .hs
    push ax
    push si
    shl si, 1
    add ax, [tp_colw+si]
    mov bx, [tp_tabbot]
    mov cx, 1
    mov dx, [tp_tabtop]
    sub dx, [tp_tabbot]
    call tp_emit_box
    pop si
    pop ax
    push si
    shl si, 1
    add ax, [tp_colw+si]
    pop si
    inc si
    jmp .v
.hs:
    mov ax, [tp_rowh]
    or ax, ax
    jnz .hr0
    mov ax, 16
    mov [tp_rowh], ax
.hr0:
    mov ax, [tp_tabtop]
    sub ax, [tp_rowh]
.hr:
    cmp ax, [tp_tabbot]
    jle .o
    push ax
    mov bx, ax
    mov ax, [tp_left]
    mov cx, [tp_tabw]
    mov dx, 1
    call tp_emit_box
    pop ax
    sub ax, [tp_rowh]
    jmp .hr
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ---- strings used by parser -------------------------------------------------
tp_s_spch:      db ' ', 0
tp_s_amp:       db '&', 0
tp_s_dash:      db '--', 0
tp_s_abs:       db 'Abstract', 0
tp_s_chap:      db 'Chapter ', 0
tp_s_bullet:    db '*', 0
tp_s_lbr:       db '[', 0
tp_s_rbr:       db ']', 0
tp_s_bsl:       db '\', 0
tp_s_tex:       db 'TeX', 0
tp_s_latex:     db 'LaTeX', 0
tp_s_today:     db 'August 2026', 0
tp_s_endtab:    db 'end{tabular}', 0
