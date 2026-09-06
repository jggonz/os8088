; =============================================================================
; os8088 - apps/word/wdrtf.inc
;
; RTF IN AND OUT (SPEC.md 68.8) - Word's other portable format, and the one
; that is pure text transform: Opus/RTFOUT.C writes it and Opus/RTFIN.C reads
; it, neither touching Windows. Both directions live here.
;
; The extension decides which format a save writes: a name ending .RTF gets
; this, everything else gets the Word file (SPEC.md 68.4). On the way in the
; CONTENT decides - a file beginning '{\rtf' is RTF whatever it is called -
; because a file that arrives from elsewhere may be named anything.
; =============================================================================

WD_RTFDEPTH  equ 16             ; group nesting. Word's own reader has a
                                ; limit too; past this the import stops with
                                ; a toast and keeps what it had
WD_RTFCW     equ 20             ; the longest control word this reads

; =============================================================================
; THE WRITER
; =============================================================================

; -----------------------------------------------------------------------------
; wd_rtfimg - assemble the RTF text into the staging claim
; in:  ES = the staging claim, DS = the package
; out: CF=0 and [wd_dend] = the byte count; CF=1 = it did not fit.
;      Clobbers AX BX CX DX SI DI.
; -----------------------------------------------------------------------------
wd_rtfimg:
    cld
    xor di, di
    mov byte [wd_rfull], 0
    mov si, wd_r_head
    call wd_res
    xor bx, bx                      ; BX = the character index
    mov byte [wd_rat], 0            ; the attributes currently OPEN
    mov byte [wd_rnew], 1           ; a paragraph is about to start
.step:
    cmp byte [wd_rnew], 0
    je .body
    mov byte [wd_rnew], 0
    call wd_rpap                    ; \pard plus this paragraph's own
    mov byte [wd_rat], 0            ; controls; \plain closed every run
.body:
    cmp bx, [wd_len]
    jae .tail
    push ds
    mov ds, [cs:wd_dseg]
    mov al, [bx]
    pop ds
    cmp al, 13
    je .para
    push ax
    push ds
    mov ds, [cs:wd_cseg]
    mov al, [bx]
    pop ds
    call wd_rdress                  ; open/close what this character wears
    pop ax
    call wd_rch
    inc bx
    jmp short .step
.para:
    mov si, wd_r_par
    call wd_res
    inc bx
    mov byte [wd_rnew], 1
    jmp short .step
.tail:
    mov si, wd_r_par                ; the tail paragraph's own \par: a Word
    call wd_res                     ; file's text ends with a mark and so
    mov si, wd_r_tail               ; does RTF's (SPEC.md 68.4)
    call wd_res
    mov [wd_dend], di
    cmp byte [wd_rfull], 0
    jne .no
    clc
    ret
.no:
    stc
    ret

; wd_rch - emit one document character, escaped. Preserves all but flags.
wd_rch:
    push ax
    push si
    cmp al, 9
    je .tab
    cmp al, '\'
    je .esc
    cmp al, '{'
    je .esc
    cmp al, '}'
    je .esc
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    call wd_re
    jmp short .out
.esc:
    push ax
    mov al, '\'
    call wd_re
    pop ax
    call wd_re
    jmp short .out
.tab:
    mov si, wd_r_tab
    call wd_res
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rdress - emit the controls that turn AL's attributes on and the ones
;             that turn the previous run's off
; in:  AL = the attribute byte this character wears
; out: [wd_rat] = AL; preserves all registers
; -----------------------------------------------------------------------------
wd_rdress:
    push ax
    push bx
    push cx
    push si
    mov cl, al
    mov ch, [wd_rat]
    cmp cl, ch
    je .out
    mov bl, cl
    xor bl, ch                      ; BL = what changed
    test bl, WDAT_BOLD
    jz .ni
    mov si, wd_r_b
    test cl, WDAT_BOLD
    jnz .b1
    mov si, wd_r_b0
.b1:
    call wd_res
.ni:
    test bl, WDAT_ITAL
    jz .ns
    mov si, wd_r_i
    test cl, WDAT_ITAL
    jnz .i1
    mov si, wd_r_i0
.i1:
    call wd_res
.ns:
    test bl, WDAT_SCAP
    jz .nh
    mov si, wd_r_sc
    test cl, WDAT_SCAP
    jnz .s1
    mov si, wd_r_sc0
.s1:
    call wd_res
.nh:
    test bl, WDAT_HID
    jz .nu
    mov si, wd_r_v
    test cl, WDAT_HID
    jnz .h1
    mov si, wd_r_v0
.h1:
    call wd_res
.nu:
    mov al, bl                      ; the three underlines are one state, so
    and al, WDAT_UL|WDAT_WUL|WDAT_DUL   ; any change closes and reopens
    jz .done
    mov si, wd_r_ul0
    call wd_res
    mov si, wd_r_ul
    test cl, WDAT_UL
    jnz .u1
    mov si, wd_r_ulw
    test cl, WDAT_WUL
    jnz .u1
    mov si, wd_r_uld
    test cl, WDAT_DUL
    jnz .u1
    jmp short .done
.u1:
    call wd_res
.done:
    mov [wd_rat], cl
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rpap - emit \pard\plain and the controls of the paragraph starting at BX
; Preserves all registers.
; -----------------------------------------------------------------------------
wd_rpap:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    mov si, wd_r_pard
    call wd_res
    call wd_rpapidx                 ; AL = the paragraph's dictionary index
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    mov ds, [cs:wd_pseg]
    mov ax, [si]
    mov bx, [si+2]
    push cs
    pop ds
    mov [wd_rpp], ax                ; the four PAP bytes are BANKED, not held
    mov [wd_rpp+2], bx              ; in registers: SI is the string pointer
                                    ; every emit below needs
    mov al, [wd_rpp]
    and al, WDPA_ALIGN
    jz .nj                          ; left is \pard's own default
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [bx+wd_r_qtab]
    call wd_res
.nj:
    mov al, [wd_rpp+1]              ; left indent
    or al, al
    jz .nl
    xor ah, ah
    call wd_dtwip
    push ax
    mov si, wd_r_li
    call wd_res
    pop ax
    call wd_rnum
.nl:
    mov al, [wd_rpp+2]              ; first-line indent, SIGNED
    or al, al
    jz .nf
    cbw
    call wd_dtwip
    push ax
    mov si, wd_r_fi
    call wd_res
    pop ax
    call wd_rnum
.nf:
    mov al, [wd_rpp+3]              ; right indent
    or al, al
    jz .nr
    xor ah, ah
    call wd_dtwip
    push ax
    mov si, wd_r_ri
    call wd_res
    pop ax
    call wd_rnum
.nr:
    mov al, [wd_rpp]                ; line spacing: 0/1/2 -> 240/360/480
    and al, WDPA_SPACE
    jz .nsp
    shr al, 1
    shr al, 1
    xor ah, ah
    mov cx, 120
    mul cx
    add ax, 240
    push ax
    mov si, wd_r_sl
    call wd_res
    pop ax
    call wd_rnum
.nsp:
    test byte [wd_rpp], WDPA_SB     ; space before: half a line, 120 twips
    jz .nsb
    mov si, wd_r_sb
    call wd_res
    mov ax, 120
    call wd_rnum
.nsb:
                                    ; NO trailing space here: every control
                                    ; word this emits already carries the one
                                    ; that delimits it (the literal strings
                                    ; end in one, wd_rnum appends one), and a
                                    ; second space is TEXT - it arrived as a
                                    ; leading space on every paragraph
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_rpapidx - AL = the PAP index of the paragraph starting at BX. Preserves
; all other registers.
wd_rpapidx:
    push bx
    push cx
    push ds
    mov cx, [cs:wd_len]
    mov ds, [cs:wd_dseg]
.scan:
    cmp bx, cx
    jae .tail
    cmp byte [bx], 13
    je .mark
    inc bx
    jmp short .scan
.mark:
    mov ds, [cs:wd_cseg]
    mov al, [bx]
    jmp short .out
.tail:
    mov al, [cs:wd_pap_tail]
.out:
    pop ds
    pop cx
    pop bx
    ret

; wd_re - emit AL into the staging claim at ES:DI. Sets [wd_rfull] and stops
; storing once the claim is full. Preserves all registers but DI.
wd_re:
    cmp di, WD_DOCCAP * 1024 - 2
    jae .full
    mov [es:di], al
    inc di
    ret
.full:
    mov byte [wd_rfull], 1
    ret

; wd_res - emit the NUL-terminated CS string at SI. Preserves all but DI.
wd_res:
    push ax
    push si
.step:
    mov al, [cs:si]
    or al, al
    jz .out
    inc si
    call wd_re
    jmp short .step
.out:
    pop si
    pop ax
    ret

; wd_rnum - emit signed AX in decimal, then one space. Preserves all but DI.
wd_rnum:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    or ax, ax
    jge .pos
    neg ax
    push ax
    mov al, '-'
    call wd_re
    pop ax
.pos:
    mov cx, 0
    mov bx, 10
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    call wd_re
    loop .emit
    mov al, ' '
    call wd_re
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the writer's strings ----------------------------------------------------
wd_r_head: db '{\rtf1\ansi\deff0{\fonttbl{\f0\fmodern Courier New;}}', 13, 10
           db '{\stylesheet{\snext0 Normal;}}', 13, 10, 0
wd_r_tail: db '}', 13, 10, 0
wd_r_pard: db '\pard\plain ', 0
wd_r_par:  db '\par', 13, 10, 0
wd_r_tab:  db '\tab ', 0
wd_r_b:    db '\b ', 0
wd_r_b0:   db '\b0 ', 0
wd_r_i:    db '\i ', 0
wd_r_i0:   db '\i0 ', 0
wd_r_sc:   db '\scaps ', 0
wd_r_sc0:  db '\scaps0 ', 0
wd_r_v:    db '\v ', 0
wd_r_v0:   db '\v0 ', 0
wd_r_ul:   db '\ul ', 0
wd_r_ulw:  db '\ulw ', 0
wd_r_uld:  db '\uldb ', 0
wd_r_ul0:  db '\ulnone ', 0
wd_r_ql:   db '\ql ', 0
wd_r_qc:   db '\qc ', 0
wd_r_qr:   db '\qr ', 0
wd_r_qj:   db '\qj ', 0
wd_r_li:   db '\li', 0
wd_r_fi:   db '\fi', 0
wd_r_ri:   db '\ri', 0
wd_r_sl:   db '\sl', 0
wd_r_sb:   db '\sb', 0
wd_r_qtab: dw wd_r_ql, wd_r_qc, wd_r_qr, wd_r_qj

; =============================================================================
; THE READER
; =============================================================================

; wd_isrtfimg - does the staging claim begin '{\rtf'?
; in:  AX = the file's byte count; out: CF=0 = yes. Preserves every register.
wd_isrtfimg:
    push ax
    push si
    push es
    mov es, [wd_stgseg]
    cmp ax, 5
    jb .no
    cmp byte [es:0], '{'
    jne .no
    cmp byte [es:1], '\'
    jne .no
    cmp byte [es:2], 'r'
    jne .no
    cmp byte [es:3], 't'
    jne .no
    cmp byte [es:4], 'f'
    jne .no
    pop es
    pop si
    pop ax
    clc
    ret
.no:
    pop es
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wd_rtfparse - read the RTF in the staging claim into the document
; in:  [wd_stgseg], AX = the file's byte count
; out: CF=0 loaded, CF=1 refused. Clobbers everything.
;
; A tokeniser over groups, control words, control symbols and text, with a
; group stack that saves and restores the character attributes so a \b's
; scope ends with its group, and a destination rule: {\* ...} and the known
; non-text destinations are skipped whole (SPEC.md 68.8).
; -----------------------------------------------------------------------------
wd_rtfparse:
    mov [wd_dfsize], ax
    mov byte [wd_dtrunc], 0
    mov ax, WD_MAXKB
    call wd_resize
    jc .no
    mov word [wd_len], 0
    mov word [wd_papn], 1           ; a fresh dictionary: entry 0 is Normal
    push es
    mov es, [wd_pseg]
    mov word [es:0], 0
    mov word [es:2], 0
    pop es
    mov word [wd_pfb], 0
    mov word [wd_pfb+2], 0
    mov byte [wd_rat], 0
    mov word [wd_rdep], 0
    mov word [wd_rskip], 0
    xor si, si                      ; SI = where we are in the file
.step:
    cmp si, [wd_dfsize]
    jae .done
    push es
    mov es, [wd_stgseg]
    mov al, [es:si]
    pop es
    inc si
    cmp al, '{'
    je .open
    cmp al, '}'
    je .close
    cmp al, '\'
    je .ctrl
    cmp al, 13
    je .step
    cmp al, 10
    je .step
    cmp word [wd_rskip], 0
    jne .step
    call wd_rput
    jmp short .step
.open:
    mov ax, [wd_rdep]
    cmp ax, WD_RTFDEPTH
    jae .deep
    mov bx, ax
    shl bx, 1
    mov al, [wd_rat]
    mov [bx+wd_rstk], al
    inc word [wd_rdep]
    jmp short .step
.close:
    cmp word [wd_rdep], 0
    je .step
    dec word [wd_rdep]
    mov bx, [wd_rdep]
    shl bx, 1
    mov al, [bx+wd_rstk]
    mov [wd_rat], al
    mov ax, [wd_rskip]
    or ax, ax
    jz .step
    dec ax                          ; skipping stops when the group that
    cmp [wd_rdep], ax               ; started it closes
    ja .step
    mov word [wd_rskip], 0
    jmp short .step
.ctrl:
    call wd_rtok                    ; consumes the control word or symbol
    jmp short .step
.deep:
    mov byte [wd_dtrunc], 1         ; nested past what this reads: keep what
    jmp short .done                 ; arrived and say so
.done:
    call wd_rtail                   ; the tail paragraph's format
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; wd_rput - append character AL to the document, wearing [wd_rat]
; Preserves all registers. A document that reaches its ceiling stops taking
; characters and raises [wd_dtrunc].
; -----------------------------------------------------------------------------
wd_rput:
    push ax
    push bx
    push es
    cmp al, 9                       ; \tab arrives here, and a tab is one of
    je .store                       ; the three control codes this document
    cmp al, 32                      ; model holds (SPEC.md 68.3)
    jb .out
    cmp al, 126
    ja .out
.store:
    mov bx, [wd_len]
    cmp bx, [wd_cap]
    jae .full
    mov es, [wd_dseg]
    mov [es:bx], al
    mov es, [wd_cseg]
    mov al, [wd_rat]
    mov [es:bx], al
    inc word [wd_len]
    jmp short .out
.full:
    mov byte [wd_dtrunc], 1
.out:
    pop es
    pop bx
    pop ax
    ret

; wd_rputm - append a paragraph mark carrying the current PAP candidate.
; Preserves all registers.
wd_rputm:
    push ax
    push bx
    push es
    call wd_papfind                 ; AL = the dictionary index (CF=1 = full,
    jc .plain                       ; and Normal is the honest fallback)
    jmp short .have
.plain:
    xor al, al
.have:
    mov bx, [wd_len]
    cmp bx, [wd_cap]
    jae .full
    push ax
    mov es, [wd_dseg]
    mov byte [es:bx], 13
    pop ax
    mov es, [wd_cseg]
    mov [es:bx], al                 ; the mark's CHP byte IS the paragraph's
                                    ; dictionary index (SPEC.md 68.3)
    inc word [wd_len]
    jmp short .out
.full:
    mov byte [wd_dtrunc], 1
.out:
    pop es
    pop bx
    pop ax
    ret

; wd_rtail - the tail paragraph's format, for wd_ldpost. Preserves all.
wd_rtail:
    push ax
    call wd_papfind
    jnc .have
    xor al, al
.have:
    mov [wd_dtail], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; wd_rtok - read the control word or symbol at SI and act on it
; in:  SI = one past the backslash
; out: SI advanced past it (and past one trailing space); preserves all others
; -----------------------------------------------------------------------------
wd_rtok:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov es, [wd_stgseg]
    cmp si, [wd_dfsize]
    jae .out
    mov al, [es:si]
    call wd_risal
    jc .symbol
    xor di, di                      ; --- a control word --------------------
.name:
    cmp si, [wd_dfsize]
    jae .named
    mov al, [es:si]
    call wd_risal
    jc .named
    cmp di, WD_RTFCW - 1
    jae .nostore
    mov [di+wd_rcw], al
    inc di
.nostore:
    inc si
    jmp short .name
.named:
    mov byte [di+wd_rcw], 0
    xor cx, cx                      ; CX = the parameter, DX = it has one
    xor dx, dx
    mov bx, 1
    cmp si, [wd_dfsize]
    jae .noparam
    mov al, [es:si]
    cmp al, '-'
    jne .digits
    mov bx, -1
    inc si
.digits:
    cmp si, [wd_dfsize]
    jae .noparam
    mov al, [es:si]
    cmp al, '0'
    jb .noparam
    cmp al, '9'
    ja .noparam
    mov dx, 1
    sub al, '0'
    xor ah, ah
    push ax
    mov ax, cx
    push bx
    push dx                         ; DX says the control word HAD a
    mov bx, 10                      ; parameter, and a 16-bit mul writes
    mul bx                          ; DX:AX - losing it reads \b0 as \b
    pop dx
    pop bx
    mov cx, ax
    pop ax
    add cx, ax
    inc si
    jmp short .digits
.noparam:
    or bx, bx
    jge .signed
    neg cx
.signed:
    cmp si, [wd_dfsize]             ; exactly one trailing space belongs to
    jae .act                        ; the control word
    cmp byte [es:si], ' '
    jne .act
    inc si
.act:
    call wd_rword
    jmp short .out
.symbol:
    inc si                          ; --- a control symbol ------------------
    cmp al, '*'
    je .star
    cmp al, 0x27                    ; \'hh - a hex character
    je .hex
    cmp word [wd_rskip], 0
    jne .out
    cmp al, '\'
    je .lit
    cmp al, '{'
    je .lit
    cmp al, '}'
    je .lit
    cmp al, '~'
    jne .out
    mov al, ' '
    call wd_rput
    jmp short .out
.lit:
    call wd_rput
    jmp short .out
.star:
    mov ax, [wd_rdep]               ; \* names the group a skipper
    mov [wd_rskip], ax
    jmp short .out
.hex:
    xor ax, ax
    cmp si, [wd_dfsize]
    jae .out
    mov al, [es:si]
    call wd_rhex
    jc .out
    mov ah, al
    inc si
    cmp si, [wd_dfsize]
    jae .out
    mov al, [es:si]
    call wd_rhex
    jc .out
    inc si
    push cx
    mov cl, 4
    shl ah, cl
    pop cx
    or al, ah
    cmp word [wd_rskip], 0
    jne .out
    call wd_rput
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wd_risal - CF=0 if AL is an ASCII letter. Preserves all registers.
wd_risal:
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .no
    clc
    ret
.up:
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    ja .no
    clc
    ret
.no:
    stc
    ret

; wd_rhex - AL = the value of hex digit AL; CF=1 = it is not one.
wd_rhex:
    cmp al, '0'
    jb .no
    cmp al, '9'
    ja .a
    sub al, '0'
    clc
    ret
.a:
    or al, 32
    cmp al, 'a'
    jb .no
    cmp al, 'f'
    ja .no
    sub al, 'a' - 10
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; wd_rword - act on the control word at [wd_rcw] with parameter CX (DX = it
;            had one)
; Preserves all registers.
;
; An unrecognised control word is IGNORED - not its group. \* is what says
; 'skip the group', and honouring that distinction is what stops an RTF file
; from arriving empty (SPEC.md 68.8).
; -----------------------------------------------------------------------------
wd_rword:
    push ax
    push bx
    push si
    push di
    mov bx, wd_r_tbl
.look:
    mov si, [bx]
    or si, si
    jz .none
    mov di, wd_rcw
    call wd_rstreq
    jc .next
    mov al, [bx+2]                  ; the action
    jmp short .do
.next:
    add bx, 3
    jmp short .look
.none:
    pop di
    pop si
    pop bx
    pop ax
    ret
.do:
    cmp al, WDR_SKIP
    jne .n1
    cmp word [wd_rskip], 0          ; a destination this reader has no use
    jne .fin                        ; for: skip to its closing brace
    mov si, [wd_rdep]
    mov [wd_rskip], si
    jmp short .fin
.n1:
    cmp word [wd_rskip], 0
    jne .fin                        ; inside a skipped group nothing acts
    cmp al, WDR_PAR
    jne .n2
    call wd_rputm
    jmp short .fin
.n2:
    cmp al, WDR_TAB
    jne .n3
    mov al, 9
    call wd_rput
    jmp short .fin
.n3:
    cmp al, WDR_PLAIN
    jne .n4
    mov byte [wd_rat], 0
    jmp short .fin
.n4:
    cmp al, WDR_PARD
    jne .n5
    mov word [wd_pfb], 0
    mov word [wd_pfb+2], 0
    jmp short .fin
.n5:
    call wd_rword2                  ; the attribute, alignment and indent
                                    ; actions - split out because the block
                                    ; above ran out of short jumps
.fin:
    pop di
    pop si
    pop bx
    pop ax
    ret

; wd_rword2 - the rest of the actions, split out because the first block
; above ran out of short jumps. in: AL = the action, CX = the parameter,
; DX = it had one, BX = the table row. Preserves all registers.
wd_rword2:
    push ax
    push bx
    push si
    cmp al, WDR_BOLD
    jb .npos
    cmp al, WDR_HID
    ja .npos
    sub al, WDR_BOLD                ; the four flag attributes, in order
    xor ah, ah
    mov si, ax
    mov ah, [si+wd_r_bits]
    or dx, dx                       ; no parameter means ON; a 0 parameter
    jz .on                          ; means OFF
    or cx, cx
    jz .off
.on:
    or [wd_rat], ah
    jmp .out
.off:
    not ah
    and [wd_rat], ah
    jmp .out
.npos:
    cmp al, WDR_UL
    jb .nul
    cmp al, WDR_ULNONE
    ja .nul
    and byte [wd_rat], ~(WDAT_UL|WDAT_WUL|WDAT_DUL) & 0xFF
    cmp al, WDR_ULNONE
    je .out
    or dx, dx
    jz .ulon
    or cx, cx
    jz .out
.ulon:
    sub al, WDR_UL
    xor ah, ah
    mov si, ax
    mov al, [si+wd_r_ulbits]
    or [wd_rat], al
    jmp short .out
.nul:
    cmp al, WDR_QL
    jb .nq
    cmp al, WDR_QJ
    ja .nq
    sub al, WDR_QL
    and byte [wd_pfb], ~WDPA_ALIGN & 0xFF
    or [wd_pfb], al
    jmp short .out
.nq:
    cmp al, WDR_LI
    je .li
    cmp al, WDR_FI
    je .fi
    cmp al, WDR_RI
    je .ri
    cmp al, WDR_SL
    je .sl
    cmp al, WDR_SB
    je .sb
    jmp short .out
.li:
    call wd_rcell
    mov [wd_pfb+1], al
    jmp short .out
.fi:
    call wd_rcell
    mov [wd_pfb+2], al
    jmp short .out
.ri:
    call wd_rcell
    mov [wd_pfb+3], al
    jmp short .out
.sl:
    mov ax, cx
    or ax, ax
    jge .slp
    neg ax                          ; \sl-240 is 'exactly 240'
.slp:
    mov ah, 0
    cmp ax, 300
    jb .sl0
    mov ah, 1
    cmp ax, 420
    jb .sl0
    mov ah, 2
.sl0:
    shl ah, 1
    shl ah, 1
    and byte [wd_pfb], ~WDPA_SPACE & 0xFF
    or [wd_pfb], ah
    jmp short .out
.sb:
    or cx, cx
    jz .out
    or byte [wd_pfb], WDPA_SB
.out:
    pop si
    pop bx
    pop ax
    ret

; wd_rcell - AL = parameter CX in twips as signed cells, clamped. Preserves
; all registers but AX.
wd_rcell:
    push bx
    push cx
    push dx
    mov ax, cx
    cwd
    mov cx, WD_TWIPCELL
    idiv cx
    cmp ax, WD_INDMAX
    jle .lo
    mov ax, WD_INDMAX
.lo:
    cmp ax, -WD_INDMAX
    jge .ok
    mov ax, -WD_INDMAX
.ok:
    pop dx
    pop cx
    pop bx
    ret

; wd_rstreq - compare the NUL string at CS:SI with the one at DS:DI.
; CF=0 = equal. Preserves all registers.
wd_rstreq:
    push ax
    push si
    push di
.step:
    mov al, [cs:si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .step
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

; --- the reader's control-word table -----------------------------------------
WDR_SKIP    equ 1                   ; a destination to swallow whole
WDR_PAR     equ 2
WDR_TAB     equ 3
WDR_PLAIN   equ 4
WDR_PARD    equ 5
WDR_BOLD    equ 7                   ; the four flag attributes, in wd_r_bits
WDR_ITAL    equ 8                   ; order
WDR_SCAP    equ 9
WDR_HID     equ 10
WDR_UL      equ 11                  ; the three underlines...
WDR_ULW     equ 12
WDR_ULDB    equ 13
WDR_ULNONE  equ 14                  ; ...and the one that clears them
WDR_QL      equ 15                  ; the four alignments, in jc order
WDR_QC      equ 16
WDR_QR      equ 17
WDR_QJ      equ 18
WDR_LI      equ 19
WDR_FI      equ 20
WDR_RI      equ 21
WDR_SL      equ 22
WDR_SB      equ 23

wd_r_bits:   db WDAT_BOLD, WDAT_ITAL, WDAT_SCAP, WDAT_HID
wd_r_ulbits: db WDAT_UL, WDAT_WUL, WDAT_DUL

wd_r_tbl:
    dw wd_rw_par
    db WDR_PAR
    dw wd_rw_line
    db WDR_PAR
    dw wd_rw_tab
    db WDR_TAB
    dw wd_rw_plain
    db WDR_PLAIN
    dw wd_rw_pard
    db WDR_PARD
    dw wd_rw_b
    db WDR_BOLD
    dw wd_rw_i
    db WDR_ITAL
    dw wd_rw_scaps
    db WDR_SCAP
    dw wd_rw_v
    db WDR_HID
    dw wd_rw_ul
    db WDR_UL
    dw wd_rw_ulw
    db WDR_ULW
    dw wd_rw_uldb
    db WDR_ULDB
    dw wd_rw_ulnone
    db WDR_ULNONE
    dw wd_rw_ql
    db WDR_QL
    dw wd_rw_qc
    db WDR_QC
    dw wd_rw_qr
    db WDR_QR
    dw wd_rw_qj
    db WDR_QJ
    dw wd_rw_li
    db WDR_LI
    dw wd_rw_fi
    db WDR_FI
    dw wd_rw_ri
    db WDR_RI
    dw wd_rw_sl
    db WDR_SL
    dw wd_rw_sb
    db WDR_SB
    dw wd_rw_fonttbl
    db WDR_SKIP
    dw wd_rw_colortbl
    db WDR_SKIP
    dw wd_rw_stylesheet
    db WDR_SKIP
    dw wd_rw_info
    db WDR_SKIP
    dw wd_rw_pict
    db WDR_SKIP
    dw wd_rw_footnote
    db WDR_SKIP
    dw wd_rw_header
    db WDR_SKIP
    dw wd_rw_footer
    db WDR_SKIP
    dw wd_rw_xe
    db WDR_SKIP
    dw wd_rw_tc
    db WDR_SKIP
    dw 0
    db 0

wd_rw_par:        db 'par', 0
wd_rw_line:       db 'line', 0
wd_rw_tab:        db 'tab', 0
wd_rw_plain:      db 'plain', 0
wd_rw_pard:       db 'pard', 0
wd_rw_b:          db 'b', 0
wd_rw_i:          db 'i', 0
wd_rw_scaps:      db 'scaps', 0
wd_rw_v:          db 'v', 0
wd_rw_ul:         db 'ul', 0
wd_rw_ulw:        db 'ulw', 0
wd_rw_uldb:       db 'uldb', 0
wd_rw_ulnone:     db 'ulnone', 0
wd_rw_ql:         db 'ql', 0
wd_rw_qc:         db 'qc', 0
wd_rw_qr:         db 'qr', 0
wd_rw_qj:         db 'qj', 0
wd_rw_li:         db 'li', 0
wd_rw_fi:         db 'fi', 0
wd_rw_ri:         db 'ri', 0
wd_rw_sl:         db 'sl', 0
wd_rw_sb:         db 'sb', 0
wd_rw_fonttbl:    db 'fonttbl', 0
wd_rw_colortbl:   db 'colortbl', 0
wd_rw_stylesheet: db 'stylesheet', 0
wd_rw_info:       db 'info', 0
wd_rw_pict:       db 'pict', 0
wd_rw_footnote:   db 'footnote', 0
wd_rw_header:     db 'header', 0
wd_rw_footer:     db 'footer', 0
wd_rw_xe:         db 'xe', 0
wd_rw_tc:         db 'tc', 0
