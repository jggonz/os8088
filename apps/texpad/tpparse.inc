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
    call tp_ts_init
    call tp_ts_scan
    call tp_ts_end
    mov byte [tp_setok], 1
    mov byte [tp_needset], 0
    mov word [tp_ppage], 0
    mov word [tp_pscroll], 0
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
    push cx
    push si
    push di
    push ds
    pop es
    xor ax, ax
    mov di, tp_nruns            ; tp_nruns, tp_nboxes, tp_tused
    stosw
    stosw
    stosw
    mov [tp_page], ax
    mov di, tp_inpara           ; every per-document byte from tp_inpara to
    mov cx, tp_onpage + 1 - tp_inpara   ; tp_onpage: the style, the numbering,
    rep stosb                   ; the stacks and the centred line
    mov [tp_title], al
    mov [tp_author], al
    mov [tp_date], al
    mov [tp_si], ax
    inc ax
    mov [tp_npages], ax
    mov [tp_needind], al
    call tp_geom
    mov al, [tp_pad]            ; padding: indent, skip, lead, column gap
    mov ah, 8
    mul ah
    add ax, tp_padtab
    xchg ax, si
    mov di, tp_parind
    mov cx, 4
    rep movsw
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
    mov ax, [tp_srclen]
    mov [tp_se], ax
    pop di
    pop si
    pop cx
    pop ax
    ret

; tp_parind, tp_parskip, tp_tlead, tp_tabsep for Compact, Normal and Loose
tp_padtab:
    dw 16, 0, 12, 3
    dw 24, 6, 14, 6
    dw 28, 10, 16, 8

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
    mov si, tp_s_spch
    call tp_emit_csi
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
    cmp al, '`'                 ; `` and '' are both a straight double quote
    je .qq
    cmp al, 39
    jne .coll
.qq:
    inc si
    cmp si, [tp_se]
    jae .ch0
    cmp [es:si], al
    jne .ch0
    inc si
    mov [tp_si], si
    mov word [tp_wbuf], '"'     ; '"' and its NUL
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
    ; force command-name collect on any ASCII letter
    call tp_isalpha
    jnc .word
    inc word [tp_si]
    ; \\ is a line break, not two printable slashes
    cmp al, '\'
    je .nl
    cmp al, ' '
    je .sp
    cmp al, ','
    je .sp
    push ds                     ; \% \& \$ \# \_ \{ \} are the character
    pop es
    mov di, tp_s_lit
    mov cx, TP_NLIT
    repne scasb
    jne .o
    jmp short .lit
.nl:
    call tp_nl
    jmp .o
.sp:
    call tp_space
    jmp .o
.lit:
    xor ah, ah
    mov [tp_wbuf], ax           ; the character and its NUL
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
    mov [tp_cmdn], cl           ; its length, which tp_cmd_disp compares first
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
    mov si, tp_cmdtab
    mov bl, [tp_cmdn]
.lp:
    mov al, [si]                ; an entry is the name's LENGTH, the name and
    or al, al                   ; its handler, so a name of another length is
    jz .unk                     ; passed over on one compare
    cmp al, bl
    jne .nx
    call tp_cmdent
    je .hit
.nx:
    xor ah, ah
    add si, ax
    add si, 3
    jmp .lp
.hit:
    xor ah, ah
    add si, ax
    mov ax, [si+1]
    pop si
    pop bx
    push ds
    pop es
    jmp ax
.unk:
    pop si
    pop bx
    call tp_skip_opt_store
    call tp_peek
    cmp al, '{'
    jne .o
    jmp tp_read_group
.o:
    ret

; SI = a tp_cmdtab entry: ZF=1 when tp_cmd is its name. Preserves every
; register but the flags.
tp_cmdent:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov cl, [si]
    xor ch, ch
    inc si
    mov di, tp_cmd
    repe cmpsb
    jne .o
    cmp byte [di], 0            ; ...and tp_cmd ends where the name does
.o:
    pop es
    pop di
    pop si
    pop cx
    ret

; ZF=1 when the command just read (tp_cmd) is \textbackslash. Preserves every
; register but the flags.
tp_is_bsl:
    push si
    mov si, tp_cn_bsl
    call tp_cmdent
    pop si
    ret

tp_c_nop:
    ret

tp_c_dclass:
    call tp_skip_opt_store      ; the options, in tp_arg
    mov si, tp_kw_size
    call tp_argkw               ; 10pt / 11pt; 12pt is what is set already
    cmp cx, 2
    jae .paper
    mov al, 10
    add al, cl
    mov [tp_fsize], al
    mov [tp_csize], al
    mov [tp_tsize], al
    add cl, 12                  ; ...and the lead, 12 or 13
    mov [tp_tlead], cx
.paper:
    mov si, tp_kw_paper
    call tp_argkw               ; legal / a4 / a5 / letter -> sizes 1, 2, 3, 0
    cmp cx, 4
    jae .side
    inc cx
    and cl, 3
    mov [tp_psize], cl
.side:
    mov di, tp_k_twoside
    call tp_has
    jne .cls
    mov byte [tp_bind], 1
.cls:
    call tp_read_group          ; the class
    mov di, tp_k_book
    call tp_has
    jne .geo
    cmp byte [tp_bind], 2
    je .geo
    mov byte [tp_bind], 1
.geo:
    jmp tp_geom

tp_c_skipg:
    call tp_skip_opt_store
    jmp tp_read_group

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
    mov di, tp_title
    mov cx, TP_TITLE_MAX
    jmp short tp_setfield

tp_c_author:
    mov di, tp_author
    mov cx, TP_AUTHOR_MAX
    jmp short tp_setfield

tp_c_date:
    mov di, tp_date
    mov cx, TP_DATE_MAX

; DI = a field, CX = its ceiling: the command's {argument}, commands taken
; out, copied in under the ceiling.
tp_setfield:
    call tp_read_group
    call tp_plain_arg
    mov si, tp_wbuf
    call tp_cpatn
    mov byte [di], 0
    ret

tp_c_maketitle:
    cmp byte [tp_made], 0
    jne .done
    call tp_endpara
    sub word [tp_y], 12
    mov byte [tp_csize], 18
    mov si, tp_title
    call tp_mkline
    jz .au
    sub word [tp_y], 6
.au:
    call tp_c_norm
    mov si, tp_author
    call tp_mkline
    mov si, tp_date
    call tp_mkline
    mov byte [tp_align], 0
    mov ax, [tp_left]
    mov bx, [tp_y]
    mov cx, [tp_tw]
    call tp_emit_box
    sub word [tp_y], 16
    mov byte [tp_made], 1
    mov byte [tp_needind], 1
.done:
    ret

; SI = a title-block line: set centred and ended, ZF=0 - or ZF=1 and nothing
; when it is empty
tp_mkline:
    cmp byte [si], 0
    je .o
    mov byte [tp_align], 1
    call tp_emit_line
    call tp_nl
    or si, si                   ; (ZF=0: SI is an address)
.o:
    ret

tp_c_begin:
    call tp_read_group
    mov si, tp_kw_env
    call tp_argkw
    mov bx, cx
    shl bx, 1
    jmp [tp_bgtab+bx]
.ctr:
    call tp_endpara
    call tp_sty_push
    mov byte [tp_align], 1
    mov al, 1
    jmp tp_env_push
.abs:
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
    ret
.enu:
    mov al, 4
    jmp short .doi2
.itm:
    mov al, 3
.doi2:
    call tp_endpara
    call tp_env_push
    add word [tp_left], 18
    mov byte [tp_itemn], 0
    mov byte [tp_needind], 0
    ret
.tab:
    call tp_skip_opt_store
    call tp_read_group
    jmp tp_do_tabular
.tbl:
    call tp_skip_opt_store
    mov al, 5
    jmp tp_env_push
.o:                             ; document (only \maketitle builds the title
    ret                         ; block: running it here printed it twice),
                                ; and anything unknown

tp_bgtab:   dw tp_c_begin.o, tp_c_begin.ctr, tp_c_begin.doq, tp_c_begin.abs
            dw tp_c_begin.itm, tp_c_begin.enu, tp_c_begin.tab, tp_c_begin.tbl
            dw tp_c_begin.o

tp_c_end:
    call tp_read_group
    mov si, tp_kw_env
    call tp_argkw
    jcxz .doc                   ; document
    loop .ctr1
    call tp_endpara             ; center
    call tp_sty_pop
    jmp tp_env_pop
.ctr1:                          ; (CX is one less than the index now)
    cmp cl, 3
    jb .quo                     ; quote, abstract
    cmp cl, 5
    jae .o                      ; (tabular and table end where they began)
    call tp_endpara             ; itemize, enumerate
    call tp_env_pop
    sub word [tp_left], 18
    ret
.quo:
    call tp_endpara
    call tp_env_pop
    sub word [tp_left], 36
    add word [tp_right], 36
    ret
.doc:
    mov ax, [tp_se]
    mov [tp_si], ax
.o:
    ret

tp_c_section:
    call tp_hdarg
    sub word [tp_y], 10
    inc byte [tp_secno]
    mov byte [tp_subno], 0
    mov di, tp_nbuf
    mov al, [tp_secno]
    call tp_u8_di
    mov ax, '. '
    stosw
    call tp_hdtail
    mov byte [tp_csize], 14
    mov byte [tp_align], 0
    call tp_emit_line
    call tp_c_norm
    call tp_nl
    sub word [tp_y], 4
    mov byte [tp_needind], 1
    ret

tp_c_subsec:
    call tp_hdarg
    sub word [tp_y], 6
    inc byte [tp_subno]
    mov di, tp_nbuf
    mov al, [tp_secno]
    call tp_u8_di
    mov al, '.'
    stosb
    mov al, [tp_subno]
    call tp_u8_di
    mov al, ' '
    stosb
    call tp_hdtail
    call tp_emit_line
    call tp_nl
    sub word [tp_y], 2
    mov byte [tp_needind], 1
    ret

tp_c_chapter:
    call tp_hdarg
    call tp_newpage
    inc byte [tp_chno]
    mov byte [tp_secno], 0
    mov di, tp_nbuf
    mov si, tp_s_chap
    call tp_cpat
    mov al, [tp_chno]
    call tp_u8_di
    mov byte [di], 0
    mov byte [tp_align], 1
    mov si, tp_nbuf
    call tp_emit_line
    call tp_nl
    mov byte [tp_csize], 18
    mov si, tp_wbuf
    call tp_emit_line
    call tp_c_norm
    mov byte [tp_align], 0
    call tp_nl
    sub word [tp_y], 12
    mov byte [tp_needind], 1
    ret

; A heading's argument: the optional [..] skipped, the {..} read and its
; commands taken out into tp_wbuf, and the paragraph before it ended.
tp_hdarg:
    call tp_skip_opt_store
    call tp_read_group
    call tp_plain_arg
    jmp tp_endpara

; ...and the heading's text after its number in tp_nbuf, under the ceiling
; tp_nbuf_left works out: SI = tp_nbuf, ready to set.
tp_hdtail:
    mov si, tp_wbuf
    call tp_nbuf_left
    call tp_cpatn
    mov byte [di], 0
    mov si, tp_nbuf
    ret

; AL = 0..255 -> decimal at DI (tp_u16_di's)
tp_u8_di:
    xor ah, ah
    jmp tp_u16_di

tp_c_para:
    call tp_read_group
    call tp_plain_arg
    call tp_endpara
    mov byte [tp_needind], 0
    mov byte [tp_bold], 1
    mov si, tp_wbuf
    call tp_emit_csi
    mov byte [tp_bold], 0
    jmp tp_space

tp_c_textbf:
    mov bx, tp_bold
    jmp short tp_styarg

tp_c_emph:
    mov bx, tp_under

; BX = a style flag: the {argument} set with it on, and the flag put back.
tp_styarg:
    call tp_read_group
    mov al, [bx]
    push ax
    mov byte [bx], 1
    call tp_feed_arg
    pop ax
    mov [bx], al
    ret

tp_c_texttt:
    call tp_read_group
    jmp tp_feed_arg

; \textbackslash plus following letters is one token ("\title"), so
; wrap cannot leave a lone backslash on the line.
tp_c_bslash:
    push ax
    push cx
    push si
    push di
    push es
    mov di, tp_wbuf             ; tp_wbuf is OURS and ES is the SOURCE's
    mov byte [di], '\'          ; below, so every store here is DS-relative:
    inc di                      ; a stosb wrote the name into the source
    mov es, [tp_srcseg]         ; segment at tp_wbuf's offset - past the end
    mov si, [tp_si]             ; of a small claim, into the next heap block
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
    mov [di], al
    inc di
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
    call tp_c_norm
    mov byte [tp_align], 0
    jmp tp_nl

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
    mov si, tp_kw_pstyle
    call tp_argkw               ; empty, myheadings, headings, or plain
    mov bx, cx
    mov al, [tp_pstab+bx]
    mov [tp_pstyle], al
    ret

tp_pstab:   db 0, 3, 2, 1

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

; A dimension command's {argument} in points: CF=0 and AX - or, with no
; group after it, CF=1 and the command set as text (\name), which is what
; reading a group that is not there would otherwise have eaten
tp_dimarg:
    call tp_needgrp
    jnc .g
    call tp_emit_bsname
    stc
    ret
.g:
    call tp_read_group
    call tp_parse_dim
    clc
    ret

; ...and AX under 36pt as an offset from the 72pt default margin
tp_dimmarg:
    call tp_dimarg
    jc .o
    cmp ax, 36
    jae .o
    add ax, 72                  ; (CF=0: no carry out of a small sum)
.o:
    ret

tp_c_oddm:
    call tp_dimmarg
    jc tp_dimset.o
    mov [tp_left], ax
    mov [tp_tleft], ax
    add ax, [tp_tw]
    mov [tp_right], ax
    ret

tp_c_topm:
    call tp_dimmarg
    jc tp_dimset.o
    mov bx, [tp_ph]
    sub bx, ax
    mov [tp_ttop], bx
    ret

tp_c_tw:
    call tp_dimarg
    jc tp_dimset.o
    mov [tp_tw], ax
    add ax, [tp_left]
    mov [tp_right], ax
    ret

tp_c_th:
    jmp tp_dimarg               ; read and set aside: there is no text height

tp_c_pind:
    mov bx, tp_parind
    jmp short tp_dimset

tp_c_pskip:
    mov bx, tp_parskip
    jmp short tp_dimset

tp_c_base:
    mov bx, tp_tlead
    jmp short tp_dimset

tp_c_tsep:
    mov bx, tp_tabsep

; BX = a dimension variable: the argument goes in it
tp_dimset:
    call tp_dimarg
    jc .o
    mov [bx], ax
.o:
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
; whatever follows it, with the overrun length under the .TEX file's
; control.
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
    ; named command: textbackslash -> '\'; else skip
    dec si
    call tp_argname
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

; DI = a needle: ZF=1 when it occurs in tp_arg (both NUL-terminated).
; Preserves every register but the flags.
tp_has:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, tp_arg
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
    xor ax, ax                  ; ZF = 1
    jmp short .o
.no:
    or al, 1                    ; ZF = 0
.o:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; SI = keywords, one NUL-terminated string after another and an empty one
; last: CX = the index of the FIRST that occurs in tp_arg, or the count of
; them when none does. Preserves everything else. A keyword that is part of
; another goes AFTER it - "headings" is inside "myheadings".
tp_argkw:
    push si
    push di
    xor cx, cx
.l:
    cmp byte [si], 0
    je .o
    mov di, si
    call tp_has
    je .o
    inc cx
.z:
    lodsb
    or al, al
    jnz .z
    jmp .l
.o:
    pop di
    pop si
    ret

tp_kw_size:     db '10pt', 0, '11pt', 0, 0
tp_kw_paper:    db 'legalpaper', 0, 'a4paper', 0, 'a5paper', 0
                db 'letterpaper', 0, 0
tp_k_twoside:   db 'twoside', 0
tp_k_book:      db 'book', 0
tp_kw_env:      db 'document', 0, 'center', 0, 'quote', 0, 'abstract', 0
                db 'itemize', 0, 'enumerate', 0, 'tabular', 0, 'table', 0, 0
tp_kw_pstyle:   db 'empty', 0, 'myheadings', 0, 'headings', 0, 0

; THE COMMANDS: the name's length, the name and its handler, so tp_cmd_disp
; passes over a name of another length on one compare
%macro TPC 2
%strlen %%n %1
    db %%n, %1
    dw %2
%endmacro
tp_cmdtab:
    TPC 'documentclass', tp_c_dclass
    TPC 'usepackage', tp_c_skipg
    TPC 'title', tp_c_title
    TPC 'author', tp_c_author
    TPC 'date', tp_c_date
    TPC 'maketitle', tp_c_maketitle
    TPC 'begin', tp_c_begin
    TPC 'end', tp_c_end
    TPC 'section', tp_c_section
    TPC 'subsection', tp_c_subsec
    TPC 'chapter', tp_c_chapter
    TPC 'paragraph', tp_c_para
    TPC 'textbf', tp_c_textbf
    TPC 'emph', tp_c_emph
    TPC 'textit', tp_c_emph
    TPC 'underline', tp_c_emph
    TPC 'texttt', tp_c_texttt
tp_cn_bsl:
    TPC 'textbackslash', tp_c_bslash
    TPC 'newline', tp_c_nl
    TPC 'par', tp_c_par
    TPC 'noindent', tp_c_noindent
    TPC 'newpage', tp_c_newpage
    TPC 'clearpage', tp_c_newpage
    TPC 'pagebreak', tp_c_newpage
    TPC 'vspace', tp_c_vspace
    TPC 'bigskip', tp_c_big
    TPC 'medskip', tp_c_med
    TPC 'smallskip', tp_c_sma
    TPC 'centering', tp_c_centering
    TPC 'item', tp_c_item
    TPC 'caption', tp_c_caption
    TPC 'label', tp_c_skipg
    TPC 'cite', tp_c_cite
    TPC 'pagestyle', tp_c_pstyle
    TPC 'oddsidemargin', tp_c_oddm
    TPC 'topmargin', tp_c_topm
    TPC 'textwidth', tp_c_tw
    TPC 'textheight', tp_c_th
    TPC 'parindent', tp_c_pind
    TPC 'parskip', tp_c_pskip
    TPC 'baselineskip', tp_c_base
    TPC 'tabcolsep', tp_c_tsep
    TPC 'hline', tp_c_nop
    TPC 'large', tp_c_large
    TPC 'Large', tp_c_Large
    TPC 'small', tp_c_small
    TPC 'normalsize', tp_c_norm
    TPC 'TeX', tp_c_tex
    TPC 'LaTeX', tp_c_latex
    TPC 'today', tp_c_today
    db 0

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
    push dx
    call tp_strlen
    or ax, ax
    jz .o
    push ax
    call tp_ensure
    call tp_charw
    pop dx
    mul dx
    xchg ax, dx                 ; DX = the width, len * the size
    cmp byte [tp_align], 0
    je .left
    call tp_c_append
    jmp short .o
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
    pop dx
    pop bx
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
    call tp_center
    jmp short .o
.l:
    mov ax, [tp_left]
    mov [tp_x], ax
    call tp_put_run
.o:
    pop bx
    pop ax
    ret

; SI = a NUL string: one run of it, centred between tp_left and tp_right.
; Preserves every register.
tp_center:
    push ax
    push bx
    push cx
    push dx
    call tp_strlen
    push ax
    call tp_charw
    pop dx
    mul dx                      ; AX = its width
    mov cx, [tp_right]
    sub cx, [tp_left]
    sub cx, ax
    jns .p
    xor cx, cx
.p:
    shr cx, 1
    add cx, [tp_left]
    mov [tp_x], cx
    call tp_put_run
    pop dx
    pop cx
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
    push bx
    push si
    mov bl, [tp_clen]
    xor bh, bh
    mov byte [tp_cline+bx], 0
    mov byte [tp_align], 0      ; emit_line uses align; we center here
    mov si, tp_cline
    call tp_center
    mov byte [tp_clen], 0
    pop si
    pop bx
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

; One inter-word space, or none at the start of a line. PRESERVES EVERY
; REGISTER, and that is a fix: it took SI for its own string, and tp_feed_arg
; walks its argument in SI across this call - so the walk resumed on the
; space it had just emitted, emitted it again, and never came back. Any
; \textbf / \emph / \texttt group of two words or more did that, under the
; gfx lock, until the run array, the page count and then tp_x itself wrapped.
tp_space:
    cmp byte [tp_inpara], 0
    je .o
    push si
    mov si, tp_s_spch
    cmp byte [tp_align], 0
    je .l
    call tp_c_append
    jmp short .p
.l:
    push ax
    mov ax, [tp_left]
    cmp [tp_x], ax
    pop ax
    jbe .p
    call tp_emit_csi
.p:
    pop si
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
    xchg ax, di
    sub ax, tp_nbuf             ; its length in cells...
    mov ah, 10                  ; ...at 10pt, ten points a cell
    mul ah
    xchg ax, cx                 ; CX = the text width
    mov al, [tp_csize]
    push ax
    mov byte [tp_csize], 10
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
    pop ax
    mov [tp_csize], al
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

; AX=x BX=y SI=NUL: one run at (x, y) in the current style - or nothing,
; when the run array or the text pool is full (SPEC.md 69.3: a document that
; overruns either is TRUNCATED, never overrun)
tp_put_run_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xchg ax, dx                 ; DX = x
    cmp word [tp_nruns], TP_MAX_RUNS
    jae .o
    call tp_strlen
    or ax, ax
    jz .o
    cmp ax, 255
    jbe .ln
    mov ax, 255
.ln:
    mov cx, ax
    add ax, [tp_tused]
    cmp ax, TP_TEXT_MAX
    jae .o
    mov di, [tp_tused]          ; the text goes where the last run's ended
    mov [tp_tused], ax
    add di, tp_text
    push ds
    pop es
    push cx
    rep movsb
    pop cx
    mov ax, [tp_nruns]
    inc word [tp_nruns]
    mov di, ax
    shl ax, 1
    shl ax, 1
    shl ax, 1
    sub ax, di                  ; * TP_RUN_SZ, which is 8 - 1
    add ax, tp_runs
    xchg ax, di                 ; DI = the record
    mov al, [tp_under]
    and al, 1
    shl al, 1
    mov ah, [tp_bold]
    and ah, 1
    or al, ah
    mov ah, cl                  ; AH = the length, for +6
    mov cl, 4
    shl al, cl                  ; TP_RUN_BOLD and TP_RUN_UNDER...
    or al, [tp_page]            ; ...over the page
    stosb
    xchg ax, dx                 ; +1 x (and DH = the length)
    stosw
    xchg ax, bx                 ; +3 y
    stosw
    mov al, [tp_csize]          ; +5 size
    mov ah, dh                  ; +6 length
    stosw
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
    ; AX=x BX=y CX=w, a zero w drawn as 1, and every box ONE POINT TALL
    ; (tp_tab_grid has why). Preserves every register.
    push ax
    push di
    push es
    cmp word [tp_nboxes], TP_MAX_BOXES
    jae .o
    push ax
    mov ax, [tp_nboxes]
    inc word [tp_nboxes]
    mov di, ax
    shl ax, 1
    shl ax, 1
    shl ax, 1
    sub ax, di                  ; * TP_BOX_SZ, which is 8 - 1
    add ax, tp_boxes
    xchg ax, di
    push ds
    pop es
    mov al, [tp_page]
    stosb
    pop ax
    stosw                       ; +1 x
    xchg ax, bx
    stosw                       ; +3 y
    xchg ax, bx
    push ax
    mov ax, cx
    or ax, ax
    jnz .w
    inc ax
.w:
    stosw                       ; +5 w
    pop ax
    mov byte [tp_onpage], 1
.o:
    pop es
    pop di
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

; SI = a command name's first letter in tp_arg: the name (24 letters at
; most) into tp_cmd, SI past it, and ZF=1 when it is \textbackslash. Keeps
; every other register.
tp_argname:
    push cx
    push di
    mov di, tp_cmd
    mov cx, 24
.en:
    mov al, [si]
    call tp_isalpha             ; (a NUL is not a letter)
    jc .ed
    mov [di], al
    inc di
    inc si
    loop .en
.ed:
    mov byte [di], 0
    pop di
    pop cx
    jmp tp_is_bsl

; feed tp_arg as text (simple). A command handler's, so it banks nothing; it
; leaves BX alone, which tp_styarg keeps its flag in.
tp_feed_arg:
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
    ; word - capped at TP_WBUF_MAX like every other writer into tp_wbuf
    ; (SPEC.md 69.7): tp_arg holds up to TP_ARG_MAX, and a longer word ran
    ; off tp_wbuf's end. A word past the cap goes out as two runs
    mov di, tp_wbuf
.w:
    cmp di, tp_wbuf + TP_WBUF_MAX
    jae .ew
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
.em:                            ; tp_wbuf out as one run, when it holds any
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
    call tp_argname
    jne .lp
    ; \textbackslash + letters -> one run ("\documentclass")
    mov di, tp_wbuf
    mov al, '\'
    stosb
    mov cx, 24
.gl:
    mov al, [si]
    call tp_isalpha
    jc .ew
    stosb
    inc si
    loop .gl
    jmp short .ew
.escch:
    ; \% \{ \} \\ \& \$ \# \_
    xor ah, ah
    mov [tp_wbuf], ax
    inc si
    jmp short .em
.o:
    ret

; ---- tabular ----------------------------------------------------------------
; THE TABLE WALK IS A COMMAND HANDLER'S, and a handler may clobber anything
; (tp_ts_cmd banks what tp_ts_scan holds), so tp_do_tabular and the four
; routines under it bank nothing: only tp_tab_note and tp_tab_cell, which the
; walks call with the column in BX, keep what they are handed.
tp_do_tabular:
    ; tp_arg holds column spec
    call tp_endpara
    call tp_tab_spec

tp_tab_body:
    ; gather rows from source until \end{tabular}: measure the column widths
    ; on one walk, then set the cells on a second from the same start
    push word [tp_si]           ; the body's start
    push ds                     ; tp_colw is OURS: with ES still the SOURCE's
    pop es                      ; segment this cleared 12 bytes there at
    mov di, tp_colw             ; tp_colw's offset - past the end of an 8KB
    mov cx, TP_COL_MAX          ; source claim, into whatever block came next
    xor ax, ax                  ; (a Disk window's raise cache, which put the
    rep stosw                   ; zeroed plane back on the glass as cyan)
    call tp_tab_measure
    pop word [tp_si]
    call tp_tab_emit

; skip \end{tabular}
tp_tab_skip_end:
    mov es, [tp_srcseg]
    mov si, [tp_si]
    mov cx, 20
.lp:
    cmp si, [tp_se]
    jae .o
    call tp_endtab
    jnc .hit
    inc si
    loop .lp
.o:
    ret
.hit:
    mov [tp_si], si
    ret

tp_tab_spec:
    push ax
    push cx
    push si
    push di
    mov si, tp_arg
    xor di, di                  ; col count
.lp:
    lodsb
    or al, al
    jz .d
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
    inc di
    jmp .lp
.d:
    or di, di
    jnz .n
    inc di
    mov byte [tp_colk], 'l'
.n:
    mov [tp_tmp4], di           ; ncols
    pop di
    pop si
    pop cx
    pop ax
    ret

tp_tab_measure:
    mov es, [tp_srcseg]
    xor bx, bx                  ; col
    xor cx, cx                  ; cell len
    mov [tp_tmp2], cx           ; rows
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .d
    cmp byte [es:si], '\'
    jne .ch
    call tp_endtab              ; \end{tabular}?
    jnc .d
    inc si
    ; hline or double-backslash
    cmp byte [es:si], '\'
    jne .hl
    inc si
    mov [tp_si], si
    call tp_tab_note
    xor bx, bx
    xor cx, cx
    inc word [tp_tmp2]
    jmp .lp
.hl:
    ; skip command name
    inc si
    mov [tp_si], si
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
    call tp_skip_com            ; (the source holds no CR: tp_crlf_fix)
    jmp .lp
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

; SI = a place in the source (ES = its segment): CF=0 and SI past it when an
; \end{tabular} starts there, CF=1 and SI as it was when not. Preserves
; everything else.
tp_endtab:
    push ax
    push di
    push si
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
    pop ax                      ; (the SI it came in with)
    pop di
    pop ax
    clc
    ret
.no:
    pop si
    pop di
    pop ax
    stc
    ret

tp_tab_emit:
    ; total width
    xor ax, ax
    mov cx, [tp_tmp4]           ; (tp_tab_spec made it at least 1)
    mov [tp_tabn], cx
    xor si, si
.tw:
    add ax, [tp_colw+si]
    add si, 2
    loop .tw
    mov [tp_tabw], ax
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
    mov bx, [tp_tmp2]
    or bx, bx
    jnz .nr
    inc bx
.nr:
    mul bx
    xchg ax, cx                 ; table h
    mov ax, [tp_y]
    sub ax, cx
    cmp ax, [tp_tbot]
    jae .fit
    call tp_newpage
.fit:
    mov ax, [tp_y]
    mov [tp_tabtop], ax
    mov [tp_tabbot], ax
    mov [tp_tmp2], ax           ; current row top
    ; walk rows again
    mov es, [tp_srcseg]
    xor bx, bx                  ; col
    mov [tp_clen], bl           ; an empty cell
.lp:
    mov si, [tp_si]
    cmp si, [tp_se]
    jae .fin
    call tp_endtab
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
.app:                           ; AL to the cell, which keeps the first 70.
    push bx                     ; THE CELL IS tp_cline, and [tp_clen] its
    xor bh, bh                  ; length: a centred line's buffer, which
    mov bl, [tp_clen]           ; tp_endpara flushed before the table began
    cmp bl, 70                  ; and nothing in a table refills
    jae .skc2
    mov [tp_cline+bx], al
    inc byte [tp_clen]
    inc bx
    mov byte [tp_cline+bx], 0
.skc2:
    pop bx
    jmp .lp
.amp:
    call tp_tab_cell
    inc bx
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
    jmp .lp
.cmd:
    mov [tp_si], si             ; past the backslash
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
    call tp_is_bsl
    jne .lp
    mov al, '\'
    jmp .app
.tesc:
    inc word [tp_si]
    jmp .app                    ; AL is the escaped char
.com:
    call tp_skip_com            ; (the source holds no CR: tp_crlf_fix)
    jmp .lp
.fin:
    cmp byte [tp_clen], 0
    je .grid
    call tp_tab_cell
.grid:
    ; restore y to bottom
    mov ax, [tp_tmp2]
    sub ax, 8
    mov [tp_y], ax
    jmp tp_tab_grid

; emit current cell (tp_cline, [tp_clen] long) at col BX, row top tmp2, and
; empty it. Preserves every register.
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
    cmp byte [tp_clen], 0
    je .o
    ; x = left + sum of prior colw, and SI = 2 * col, this column's word
    mov ax, [tp_left]
    xor si, si
    mov cx, bx
    jcxz .x
.add:
    add ax, [tp_colw+si]
    inc si
    inc si
    loop .add
.x:
    add ax, [tp_tabsep]
    xchg ax, cx                 ; CX = the cell's left
    call tp_charw
    mul byte [tp_clen]
    xchg ax, dx                 ; DX = text w
    mov ah, [tp_colk+bx]
    cmp ah, 'c'
    je .al
    cmp ah, 'r'
    jne .put
.al:                            ; the slack the column leaves, all of it on
    mov al, ah                  ; the left for 'r' and half for 'c'
    push ax
    mov ax, [tp_colw+si]
    sub ax, [tp_tabsep]
    sub ax, [tp_tabsep]
    sub ax, dx
    jns .sl
    xor ax, ax
.sl:
    pop dx
    cmp dl, 'r'
    je .r
    shr ax, 1
.r:
    add cx, ax
.put:
    xchg ax, cx                 ; AX = x
    ; PDF y is the top of the 8px run. Hang it a few points
    ; under the top rule, not on the bottom rule.
    mov bx, [tp_tmp2]
    sub bx, 5
    mov si, tp_cline
    call tp_put_run_at
.o:
    mov byte [tp_clen], 0       ; ...and the cell is empty again
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; THE GRID'S RULES ARE ALL ONE POINT TALL, the frame and the column rules
; included (SPEC.md 69.3): a box has no height. That is what this package has
; always set - tp_emit_box's multiply clobbered the DX that carried the height,
; so every box came out 1pt tall - and what the preview's box painter can
; draw: a frame clipped to a scroll band would draw the two sides the clip
; invents (tp_sheet_edges' reason). So the frame is its bottom rule and each
; column rule a point at the foot of the column; drawing them full height is
; a change to both, not a fix to this one.
tp_tab_grid:
    push ax
    push bx
    push cx
    push si
    mov ax, [tp_left]
    mov bx, [tp_tabbot]
    mov cx, [tp_tabw]
    or cx, cx
    jnz .ww
    mov cx, 120
.ww:
    call tp_emit_box
    mov cx, 1
    xor si, si
.v:
    cmp si, [tp_tabn]
    jae .hs
    shl si, 1
    add ax, [tp_colw+si]        ; the right edge of this column
    shr si, 1
    call tp_emit_box
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
    mov cx, [tp_tabw]
.hr:
    sub ax, [tp_rowh]
    cmp ax, [tp_tabbot]
    jle .o
    mov bx, ax
    push ax
    mov ax, [tp_left]
    call tp_emit_box
    pop ax
    jmp .hr
.o:
    pop si
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
tp_s_lit:       db '%&$#_{}'        ; what a backslash makes a plain character
TP_NLIT         equ $ - tp_s_lit
