; =============================================================================
; os8088 - apps/word/scutil.inc
;
; WORD'S SEARCH PATTERN (SPEC.md 68.7) and the three Utilities commands that
; are algorithms rather than screens (SPEC.md 68.9): Sort, Renumber and Table
; of Contents. All four port because none of them touches Windows -
; Opus/search.c, Opus/sort.c, Opus/renum.c and Opus/toc.c are C over the
; document, and the document is the one thing this port has.
; =============================================================================

; --- the compiled search pattern (Opus/search.c's SetSpecialMatch) -----------
; One word per element: a value 0..255 is that character, and the two special
; values below are the wildcards. The escape letters are Opus/inter.h's.
SCP_ANY      equ 0x0100         ; '?'  - any one character
SCP_WHITE    equ 0x0200         ; '^w' - one or more spaces or tabs

; -----------------------------------------------------------------------------
; sc_pcomp - compile [sc_fpat] into [sc_fpw] / [sc_fpwn]
; out: nothing; preserves all registers
;
; search.c's rules, in its order:
;   ?          any character - UNLESS every character of the pattern is a '?',
;              in which case they are literal question marks (that is the
;              rule that lets a user search for '???')
;   ^^ ^?      a literal caret / question mark
;   ^p ^t      a paragraph mark (13) / a tab (9)
;   ^w         white space, one or more, greedily
;   ^<digits>  the character with that decimal code
;   ^x         x, literally - which is search.c's own default and what makes
;              ^s, ^-, ^~ and ^d (characters this font and this document model
;              have no room for - SPEC.md 6/68.3) fail honestly rather than
;              compile to something that can never occur
; -----------------------------------------------------------------------------
sc_pcomp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, sc_fpat
    xor di, di                      ; DI = compiled elements
    xor cx, cx                      ; CL = raw characters, CH = unescaped '?'
.step:
    mov al, [si]
    or al, al
    jz .done
    cmp di, SC_PATMAX
    jae .done
    inc si
    inc cl
    cmp al, '?'
    je .any
    cmp al, '^'
    je .caret
    xor ah, ah
    jmp short .put
.any:
    inc ch
    mov ax, SCP_ANY
    jmp short .put
.caret:
    mov al, [si]
    or al, al
    jz .lit                         ; a trailing caret is a caret
    inc si
    cmp al, '0'
    jb .named
    cmp al, '9'
    ja .named
    xor ah, ah                      ; ^<digits>: a decimal character code
    sub al, '0'
    mov bx, ax
    mov dx, 2                       ; at most three digits, as search.c's
.dig:                               ; CchChFromAnsiOrOem takes
    mov al, [si]
    cmp al, '0'
    jb .digdone
    cmp al, '9'
    ja .digdone
    sub al, '0'
    xor ah, ah
    push ax
    mov ax, bx
    push dx
    mov dx, 10
    mul dx
    pop dx
    mov bx, ax
    pop ax
    add bx, ax
    inc si
    dec dx
    jnz .dig
.digdone:
    mov ax, bx
    and ax, 0x00FF
    jmp short .put
.named:
    cmp al, 'w'
    jne .n1
    mov ax, SCP_WHITE
    jmp short .put
.n1:
    cmp al, 't'
    jne .n2
    mov ax, 9
    jmp short .put
.n2:
    cmp al, 'p'
    jne .lit
    mov ax, 13
    jmp short .put
.lit:
    xor ah, ah                      ; ^x is x - and so is a lone '^'
.put:
    mov bx, di
    shl bx, 1
    mov [bx+sc_fpw], ax
    inc di
    jmp .step
.done:
    or cl, cl                       ; a pattern of nothing but '?' is a
    jz .fin                         ; pattern of question MARKS (search.c)
    cmp ch, cl
    jne .fin
    xor bx, bx
.qq:
    cmp bx, di
    jae .fin
    push bx
    shl bx, 1
    mov word [bx+sc_fpw], '?'
    pop bx
    inc bx
    jmp short .qq
.fin:
    mov [sc_fpwn], di
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_rxpand - expand the replacement's specials into [sc_frx] / [sc_frxn]
; in:  [sc_fmst] / [sc_fmen] = the match this replacement is for
; out: CF=1 = it could not be built (the toast is already set); preserves all
;
; inter.h's two: ^m (chUseMatched) is the text that matched and ^c
; (chUseScrap) is the clipboard's contents. ^^ is a caret and every other ^x
; is x, the same rule the pattern uses.
; -----------------------------------------------------------------------------
sc_rxpand:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, sc_frep
    xor di, di
.step:
    mov al, [si]
    or al, al
    jz .done
    inc si
    cmp al, '^'
    jne .lit
    mov al, [si]
    or al, al
    jz .caret
    inc si
    cmp al, 'm'
    je .matched
    cmp al, 'c'
    je .scrap
    cmp al, '^'
    je .lit
    jmp short .lit
.caret:
    mov al, '^'
.lit:
    cmp di, SC_FRXMAX
    jae .full
    mov bx, di
    mov [bx+sc_frx], al
    inc di
    jmp short .step
.matched:
    mov bx, [sc_fmst]
.mm:
    cmp bx, [sc_fmen]
    jae .step
    cmp di, SC_FRXMAX
    jae .full
    push bx
    mov ax, bx
    call sc_docb2                   ; AL = the document byte at AX
    pop bx
    push bx
    mov bx, di
    mov [bx+sc_frx], al
    pop bx
    inc di
    inc bx
    jmp short .mm
.scrap:
    push si
    push di
    mov cx, SC_FRXMAX               ; ES:DI = the buffer, CX = its room
    sub cx, di
    add di, sc_frx
    push ds
    pop es
    call OSAPI_CLIP_GET             ; CF=1 = empty; CX = the bytes copied
    pop di
    pop si
    jc .noclip
    add di, cx
    jmp short .step
.noclip:
    mov ax, sc_m_noclip
    call sc_saymsg
    stc
    jmp short .out
.full:
    mov ax, sc_m_replong
    call sc_saymsg
    stc
    jmp short .out
.done:
    mov [sc_frxn], di
    clc
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
; THE SPAN THE THREE COMMANDS OPERATE ON (SPEC.md 68.9)
;
; The selection, or the whole document when there is none - Word's own rule -
; snapped out to whole paragraphs, because a paragraph is the unit Sort,
; Renumber and Table of Contents are all defined over.
; =============================================================================

SC_UMAXP     equ 128            ; paragraphs one command may touch. Past this
                                ; the command REFUSES and says so: the index
                                ; array is what bounds it, and a sort that
                                ; silently did half the selection would be
                                ; worse than one that did none (SPEC.md 47)
SC_SCHALF    equ SC_MAXKB*1024  ; the scratch claim's two halves: the rebuilt
                                ; text, then its CHP twin at this offset

; -----------------------------------------------------------------------------
; sc_uspan - the operating span, in [sc_us0] / [sc_us1]
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_uspan:
    push ax
    push bx
    push ds
    mov ax, [sc_sel0]
    mov bx, [sc_sel1]
    cmp byte [sc_selon], 0
    jne .have
    xor ax, ax
    mov bx, [sc_len]
.have:
    mov ds, [cs:sc_dseg]
.back:
    or ax, ax                       ; back to the start of the paragraph the
    jz .fwd                         ; selection begins in
    push bx
    mov bx, ax
    dec bx
    cmp byte [bx], 13
    pop bx
    je .fwd
    dec ax
    jmp short .back
.fwd:
    cmp bx, [cs:sc_len]             ; forward past the mark the selection ends
    jae .done                       ; in - unless it is already on a boundary
    push ax
    mov ax, bx
    or ax, ax
    jz .scan
    dec ax
    push bx
    mov bx, ax
    cmp byte [bx], 13
    pop bx
    je .popdone
.scan:
    pop ax
.scan2:
    cmp bx, [cs:sc_len]
    jae .done
    push ax
    mov ax, bx
    push bx
    mov bx, ax
    mov al, [bx]
    pop bx
    cmp al, 13
    pop ax
    je .incdone
    inc bx
    jmp short .scan2
.incdone:
    inc bx
    jmp short .done
.popdone:
    pop ax
.done:
    mov [cs:sc_us0], ax
    mov [cs:sc_us1], bx
    pop ds
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_plist - the paragraph starts inside the span, into [sc_upst]
; out: CF=0 and [sc_upn] = how many; CF=1 = more than SC_UMAXP (the toast is
;      already set). Preserves all registers.
;
; Entry n's paragraph runs from [sc_upst + 2n] to [sc_upst + 2n+2], the last
; one ending at [sc_us1] - so the array carries n+1 entries and no length
; array exists to disagree with it.
; -----------------------------------------------------------------------------
sc_plist:
    push ax
    push bx
    push cx
    push ds
    mov word [cs:sc_upn], 0
    mov ax, [cs:sc_us0]
    mov cx, [cs:sc_us1]
    xor bx, bx
    mov ds, [cs:sc_dseg]
.para:
    cmp ax, cx
    jae .done
    cmp bx, SC_UMAXP
    jae .full
    push bx
    shl bx, 1
    mov [cs:bx+sc_upst], ax
    pop bx
    inc bx
.scan:
    cmp ax, cx
    jae .end
    push bx
    mov bx, ax
    cmp byte [bx], 13
    pop bx
    je .mark
    inc ax
    jmp short .scan
.mark:
    inc ax
.end:
    jmp short .para
.done:
    mov [cs:sc_upn], bx
    push bx                         ; the sentinel: where the last paragraph
    shl bx, 1                       ; ends
    mov ax, [cs:sc_us1]
    mov [cs:bx+sc_upst], ax
    pop bx
    push cs
    pop ds
    clc
    jmp short .out
.full:
    push cs
    pop ds
    mov ax, sc_m_toomanyp
    call sc_saymsg
    stc
.out:
    pop ds
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_scget / sc_scdrop - the transient rebuild scratch
; out: CF=0 with [sc_scseg] set; CF=1 = refused (the toast is set)
; Preserves all registers. Two halves, SC_SCHALF apart: the rebuilt text and
; its CHP twin, so the splice moves both in step exactly as every other
; operation in this app does (SPEC.md 68.3).
; -----------------------------------------------------------------------------
sc_scget:
    push ax
    push dx
    mov word [sc_scn], 0
    mov ax, (2 * SC_SCHALF) / 1024
    call OSAPI_MEM_CLAIM
    jc .no
    mov [sc_scseg], dx
    clc
    jmp short .out
.no:
    mov word [sc_scseg], 0
    mov ax, sc_e_nomem
    call sc_saymsg
    stc
.out:
    pop dx
    pop ax
    ret

sc_scdrop:
    pushf
    push ax
    push dx
    mov dx, [sc_scseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [sc_scseg], 0
.out:
    pop dx
    pop ax
    popf
    ret

; sc_uput - append character AL wearing attribute AH to the scratch.
; CF=1 = it is full. Preserves all registers.
sc_uput:
    push bx
    push es
    mov bx, [sc_scn]
    cmp bx, SC_SCHALF
    jae .full
    mov es, [sc_scseg]
    mov [es:bx], al
    mov [es:bx + SC_SCHALF], ah
    inc word [sc_scn]
    clc
    jmp short .out
.full:
    stc
.out:
    pop es
    pop bx
    ret

; sc_uputp - append the document's characters [BX, CX) to the scratch, each
; with its own attribute. CF=1 = full. Preserves all registers.
sc_uputp:
    push ax
    push bx
    push cx
    push dx
.l:
    cmp bx, cx
    jae .ok
    mov ax, bx
    call sc_dchb                    ; AL = the CHP byte - taken FIRST, and
    mov dl, al                      ; banked in DL: `pop ax` after a fetch
    mov ax, bx                      ; restores AH as well as AL, which is how
    call sc_docb2                   ; the attribute used to get lost
    mov ah, dl
    call sc_uput
    jc .no
    inc bx
    jmp short .l
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_dchb - AL = the CHP byte at document index AX. Preserves all others.
sc_dchb:
    push bx
    push ds
    mov bx, ax
    mov ds, [cs:sc_cseg]
    mov al, [bx]
    pop ds
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_usplice - replace the span with the scratch's contents, as ONE undo group
; out: CF=1 = refused and nothing changed; preserves all registers
; -----------------------------------------------------------------------------
sc_usplice:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call sc_uclose
    mov ax, [sc_len]                ; will the document hold the result?
    sub ax, [sc_us1]
    add ax, [sc_us0]
    add ax, [sc_scn]
    jc .no
    call sc_capfor
    jc .no
    mov ax, [sc_us0]
    mov cx, [sc_us1]
    sub cx, ax
    call sc_urec_bulk               ; the span as it stands, into the blob
    jc .big                         ; ...which is SC_UMAXKB and can refuse
    mov byte [sc_unolog], 1         ; every edit below is inside that record
    mov bx, [sc_us0]
    mov cx, [sc_us1]
    sub cx, bx
    jcxz .noskip
    call sc_delspan
.noskip:
    mov bx, [sc_us0]
    mov cx, [sc_scn]
    jcxz .filled
    call sc_gaproom
    jc .undone
    mov di, bx
    xor si, si
.f:
    push ds
    mov ds, [cs:sc_scseg]           ; DS:SI walks the scratch's two halves
    mov al, [si]
    mov ah, [si + SC_SCHALF]
    pop ds
    push es
    mov es, [sc_dseg]
    mov [es:di], al
    mov es, [sc_cseg]
    mov [es:di], ah
    pop es
    inc si
    inc di
    dec cx
    jnz .f
.filled:
    mov byte [sc_unolog], 0
    call sc_urec_bulkend_at
    mov ax, [sc_us0]
    add ax, [sc_scn]
    mov [sc_cur], ax
    call sc_selclr
    mov byte [sc_hdirty], 1
    clc
    jmp short .out
.undone:
    mov byte [sc_unolog], 0
    call sc_urec_bulkend_at
    jmp short .no
.big:
    mov ax, sc_m_noundo2            ; the span is bigger than the undo blob:
    call sc_saymsg                  ; refuse rather than do it unundoably
.no:
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
; UTILITIES > SORT (SPEC.md 68.9, Opus/sort.c)
;
; A STABLE insertion sort over an index array: equal keys keep their document
; order, which is what makes a second sort on a second field a working
; two-level sort - Word's own idiom. The paragraphs themselves are rebuilt in
; the new order, text and CHP together, so each one carries its own ¶ mark
; and therefore its own paragraph format with it.
; =============================================================================

SCSO_DESC    equ 1              ; [sc_sopt] bits
SCSO_NUM     equ 2
SCSO_TAB     equ 4
SCSO_CASE    equ 8

; -----------------------------------------------------------------------------
; sc_dosort - the Sort dialog's OK
; in:  SI = window ptr; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
sc_dosort:
    call sc_uspan
    call sc_plist
    jc .out
    cmp word [sc_upn], 2
    jb .out                         ; nothing to reorder
    xor bx, bx                      ; the order array starts at identity
.init:
    cmp bx, [sc_upn]
    jae .isort
    mov si, bx
    shl si, 1
    mov [si+sc_uord], bx
    inc bx
    jmp short .init
.isort:
    mov bx, 1                       ; --- the insertion sort ---------------
.outer:
    cmp bx, [sc_upn]
    jae .sorted
    mov si, bx
    shl si, 1
    mov dx, [si+sc_uord]            ; DX = the value being placed
    mov di, bx                      ; DI = where it is going
.inner:
    or di, di
    jz .place
    mov si, di
    dec si
    shl si, 1
    mov ax, [si+sc_uord]
    call sc_socmp                   ; CF=1 = DX sorts before AX
    jnc .place
    mov si, di
    shl si, 1
    mov [si+sc_uord], ax
    dec di
    jmp short .inner
.place:
    mov si, di
    shl si, 1
    mov [si+sc_uord], dx
    inc bx
    jmp short .outer
.sorted:
    call sc_scget                   ; --- rebuild the span in the new order -
    jc .out
    call sc_utail                   ; [sc_utl] = the span reaches the tail
    xor bx, bx
.emit:
    cmp bx, [sc_upn]
    jae .done
    push bx
    shl bx, 1
    mov ax, [bx+sc_uord]
    pop bx
    call sc_uemitp                  ; paragraph AX, always ¶-terminated
    jc .drop
    inc bx
    jmp short .emit
.done:
    call sc_utrim                   ; the last mark comes off if the span had
    call sc_usplice                 ; none
    jc .drop
    call sc_scdrop
    mov si, sc_m_sorted
    call sc_setmsg
    call sc_editinv
    mov byte [sc_follow], 1
    ret
.drop:
    call sc_scdrop
.out:
    ret

; -----------------------------------------------------------------------------
; sc_uemitp - copy paragraph AX into the scratch, ¶-terminated
; out: CF=1 = the scratch is full; preserves all registers
;
; The tail paragraph has no mark of its own, so one is written for it out of
; [sc_pap_tail]: inside a rebuild every paragraph needs a mark, and sc_utrim
; takes the final one off again if the span it replaces had none.
; -----------------------------------------------------------------------------
sc_uemitp:
    push ax
    push bx
    push cx
    push dx
    mov bx, ax
    shl bx, 1
    mov cx, [bx+sc_upst+2]          ; where the paragraph ends
    mov bx, [bx+sc_upst]
    mov dx, cx
    or dx, dx
    jz .copy
    mov ax, dx                      ; AX (the paragraph index) is spent: BX
    dec ax                          ; and CX carry everything from here, and
    call sc_docb2                   ; preserving it across the fetch is what
                                    ; used to throw the fetched byte away
    cmp al, 13                      ; does it end with a mark of its own?
    je .copy
    call sc_uputp                   ; no - it is the tail paragraph
    jc .no
    mov al, 13
    mov ah, [sc_pap_tail]
    call sc_uput
    jc .no
    jmp short .ok
.copy:
    call sc_uputp
    jc .no
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_utail - [sc_utl] = 1 when the span runs to the document's end without a
; closing ¶ (so the rebuilt span must not have one either). Preserves all.
sc_utail:
    push ax
    mov byte [sc_utl], 0
    mov ax, [sc_us1]
    cmp ax, [sc_len]
    jne .out
    or ax, ax
    jz .set
    dec ax
    call sc_docb2
    cmp al, 13
    je .out
.set:
    mov byte [sc_utl], 1
.out:
    pop ax
    ret

; sc_utrim - take the rebuilt span's final ¶ off when [sc_utl] says the span
; it replaces had none, and make its format the tail's. Preserves all.
sc_utrim:
    push ax
    push bx
    push es
    cmp byte [sc_utl], 0
    je .out
    mov bx, [sc_scn]
    or bx, bx
    jz .out
    dec bx
    mov es, [sc_scseg]
    cmp byte [es:bx], 13
    jne .out
    mov al, [es:bx + SC_SCHALF]
    mov [sc_pap_tail], al
    mov [sc_scn], bx
.out:
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_socmp - does paragraph DX sort before paragraph AX?
; out: CF=1 = yes; preserves all registers
; -----------------------------------------------------------------------------
sc_socmp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test byte [sc_sopt], SCSO_DESC  ; Descending is the same comparison with
    jz .fwd                         ; its arguments the other way round
    xchg ax, dx
.fwd:
    push ax
    mov ax, dx
    call sc_sokey                   ; BX..CX = the first key
    mov si, bx
    mov di, cx
    pop ax
    call sc_sokey                   ; BX..CX = the second
    mov [sc_sokb], bx
    mov [sc_soke], cx
    test byte [sc_sopt], SCSO_NUM
    jnz .num
    call sc_socmpa
    jmp short .out
.num:
    call sc_socmpn
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_socmpa - alphanumeric: is SI..DI before BX..CX? CF=1 = yes.
; Preserves all but the flags.
sc_socmpa:
    push ax
    push bx
    push dx
.l:
    cmp si, di
    jae .aend
    cmp bx, cx
    jae .no                         ; the other ran out first: it is smaller
    mov ax, si
    call sc_docb2
    mov dl, al
    mov ax, bx
    call sc_docb2
    test byte [sc_sopt], SCSO_CASE
    jnz .exact
    push ax
    mov al, dl
    call sc_foldc
    mov dl, al
    pop ax
    call sc_foldc
.exact:
    cmp dl, al
    jb .yes
    ja .no
    inc si
    inc bx
    jmp short .l
.aend:
    cmp bx, cx                      ; a prefix sorts before what extends it
    jb .yes
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; sc_socmpn - numeric: is the value in SI..DI below the one in BX..CX?
; A field with no number sorts before every field that has one. CF=1 = yes.
sc_socmpn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call sc_sonum                   ; DX = the first key's value, CF = it had
    mov bx, dx                      ; one
    mov al, 0
    jnc .a0
    mov al, 1
.a0:
    mov si, [sc_sokb]               ; the second key, banked by sc_socmp
    mov di, [sc_soke]
    call sc_sonum
    mov ah, 0
    jnc .b0
    mov ah, 1
.b0:
    cmp al, ah                      ; a field with no number sorts before
    je .both                        ; every field that has one
    or al, al
    jz .yes
    jmp short .no
.both:
    or al, al
    jz .no                          ; neither has one: equal, so not before
    cmp bx, dx
    jl .yes
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_sokey - the key field of paragraph AX
; out: BX = its first index, CX = one past its last; preserves all others
;
; Field [sc_sofld], 1-based, between separators inside the paragraph's own
; text (the ¶ mark is never part of a key). A paragraph with fewer fields
; than that keys on the empty string.
; -----------------------------------------------------------------------------
sc_sokey:
    push ax
    push dx
    push si
    mov si, ax
    shl si, 1
    mov cx, [si+sc_upst+2]
    mov bx, [si+sc_upst]
    or cx, cx                       ; the ¶ is not part of the key
    jz .have
    mov ax, cx
    dec ax
    call sc_docb2
    cmp al, 13
    jne .have
    dec cx
.have:
    mov dx, [sc_sofld]
    or dx, dx
    jz .out
    dec dx                          ; DX = separators to step over
    jz .field
.skip:
    cmp bx, cx
    jae .out                        ; ran out of fields: the empty string
    push ax
    mov ax, bx
    call sc_docb2
    call sc_issep
    pop ax
    jc .nosep
    inc bx
    dec dx
    jz .field
    jmp short .skip
.nosep:
    inc bx
    jmp short .skip
.field:
    mov si, bx                      ; the field ends at the next separator
.fe:
    cmp si, cx
    jae .setend
    push ax
    mov ax, si
    call sc_docb2
    call sc_issep
    pop ax
    jnc .setend
    inc si
    jmp short .fe
.setend:
    mov cx, si
.out:
    pop si
    pop dx
    pop ax
    ret

; sc_issep - CF=0 if AL is the key separator. Preserves all registers.
sc_issep:
    test byte [sc_sopt], SCSO_TAB
    jnz .tab
    cmp al, ','
    je .yes
    stc
    ret
.tab:
    cmp al, 9
    je .yes
    stc
    ret
.yes:
    clc
    ret

; sc_sonum - DX = the signed integer at the head of SI..DI; CF=1 = there was
; one. Preserves all registers but DX.
sc_sonum:
    push ax
    push bx
    push cx
    push si
    xor dx, dx
    xor cx, cx                      ; CL = a digit was seen, CH = negative
.sp:
    cmp si, di
    jae .none
    mov ax, si
    call sc_docb2
    cmp al, ' '
    jne .sign
    inc si
    jmp short .sp
.sign:
    cmp al, '-'
    jne .dig
    mov ch, 1
    inc si
.dig:
    cmp si, di
    jae .done
    mov ax, si
    call sc_docb2
    cmp al, '0'
    jb .done
    cmp al, '9'
    ja .done
    mov cl, 1
    sub al, '0'
    xor ah, ah
    mov bx, dx
    shl dx, 1
    shl dx, 1
    add dx, bx
    shl dx, 1                       ; DX = 10 * old
    add dx, ax
    inc si
    jmp short .dig
.done:
    or cl, cl
    jz .none
    or ch, ch
    jz .pos
    neg dx
.pos:
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret
.none:
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

; =============================================================================
; UTILITIES > RENUMBER (SPEC.md 68.9, Opus/renum.c)
;
; The Format string is Word's: the FIRST digit-or-letter in it is the
; number's place and its kind - 1 arabic, I/i roman, A/a alphabetic - and
; everything around it is kept verbatim, so '1.' gives '1.', '(1)' gives
; '(1)' and 'I.' gives 'I.'. A tab separates the number from the text, as
; Word's does.
; =============================================================================

SCRN_ALL     equ 0              ; [sc_rnact]
SCRN_NUMD    equ 1
SCRN_REMOVE  equ 2

; -----------------------------------------------------------------------------
; sc_dorenum - the Renumber dialog's OK
; in:  SI = window ptr; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
sc_dorenum:
    call sc_uspan
    call sc_plist
    jc .out
    cmp word [sc_upn], 0
    je .out
    call sc_scget
    jc .out
    call sc_utail
    mov ax, [sc_rnfrom]             ; the running number
    mov [sc_rnnow], ax
    xor bx, bx
.para:
    cmp bx, [sc_upn]
    jae .done
    mov si, bx
    shl si, 1
    mov cx, [si+sc_upst+2]
    mov si, [si+sc_upst]            ; SI = the paragraph's first character
    push si
    call sc_rnskip                  ; SI past an existing number, CF=1 = it
    pushf                           ; had one
    cmp byte [sc_rnact], SCRN_REMOVE
    je .strip
    cmp byte [sc_rnact], SCRN_NUMD
    jne .number
    popf
    pushf
    jc .number
    popf                            ; 'Numbered Only' and it was not: leave
    pop si                          ; the paragraph exactly as it is
    push si
    jmp short .body
.strip:
    popf
    jmp short .body                 ; Remove: the text without its number
.number:
    popf
    call sc_rnemit                  ; the new number and its tab
    jc .drop2
    inc word [sc_rnnow]
.body:
    pop ax                          ; the paragraph's start, no longer needed
    push bx
    mov bx, si
    call sc_uputp
    pop bx
    jc .drop
    cmp cx, 0
    je .next
    mov ax, cx
    dec ax
    call sc_docb2
    cmp al, 13                      ; a tail paragraph gets the mark a
    je .next                        ; rebuild needs
    mov al, 13
    mov ah, [sc_pap_tail]
    call sc_uput
    jc .drop
.next:
    inc bx
    jmp .para
.done:
    call sc_utrim
    call sc_usplice
    jc .drop
    call sc_scdrop
    mov si, sc_m_renumd
    call sc_setmsg
    call sc_editinv
    mov byte [sc_follow], 1
    ret
.drop2:
    pop ax
.drop:
    call sc_scdrop
.out:
    ret

; -----------------------------------------------------------------------------
; sc_rnskip - step SI past an existing number
; in:  SI = a paragraph's first character, CX = one past its last
; out: SI advanced past 'number TAB' when there was one, CF=1 = there was;
;      preserves all other registers
;
; A number is a leading run of digits, letters and the punctuation a format
; string can carry, terminated by a TAB - which is exactly the shape this
; command writes, so what it renumbers is what it wrote.
; -----------------------------------------------------------------------------
sc_rnskip:
    push ax
    push bx
    push dx
    mov bx, si
    xor dx, dx                      ; DL = characters seen, DH = an alnum
.l:
    cmp bx, cx
    jae .no
    cmp dl, 12
    ja .no
    mov ax, bx
    call sc_docb2
    cmp al, 9
    je .tab
    call sc_isnumc
    jc .no
    call sc_isalnum
    jc .notal
    mov dh, 1
.notal:
    inc bx
    inc dl
    jmp short .l
.tab:
    or dh, dh
    jz .no
    inc bx
    mov si, bx
    pop dx
    pop bx
    pop ax
    stc
    ret
.no:
    pop dx
    pop bx
    pop ax
    clc
    ret

; sc_isnumc - CF=0 if AL may appear in a number's rendering. Preserves all.
sc_isnumc:
    call sc_isalnum
    jnc .yes
    cmp al, '('
    je .yes
    cmp al, ')'
    je .yes
    cmp al, '.'
    je .yes
    cmp al, '-'
    je .yes
    stc
    ret
.yes:
    clc
    ret

; sc_isalnum - CF=0 if AL is a letter or a digit. Preserves all registers.
sc_isalnum:
    cmp al, '0'
    jb .no
    cmp al, '9'
    jbe .yes
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

; -----------------------------------------------------------------------------
; sc_rnemit - write [sc_rnnow] into the scratch in the Format's shape
; out: CF=1 = the scratch is full; preserves all registers
; -----------------------------------------------------------------------------
sc_rnemit:
    push ax
    push bx
    push cx
    push dx
    mov bx, sc_rnfmt
    xor dl, dl                      ; DL = the kind, 0 = none seen yet
.pre:
    mov al, [bx]
    or al, al
    jz .noslot
    call sc_isalnum
    jc .lit
    mov dl, al
    inc bx
    jmp short .num
.lit:
    xor ah, ah
    call sc_uput
    jc .no
    inc bx
    jmp short .pre
.noslot:
    mov dl, '1'                     ; a format with no slot numbers arabic
.num:
    mov ax, [sc_rnnow]
    call sc_rnval
    jc .no
.post:
    mov al, [bx]
    or al, al
    jz .tab
    xor ah, ah
    call sc_uput
    jc .no
    inc bx
    jmp short .post
.tab:
    mov al, 9
    xor ah, ah
    call sc_uput
    jc .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rnval - write value AX in kind DL. CF=1 = full. Preserves all.
sc_rnval:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    push si                         ; the roman table walks SI, and the
                                    ; caller is holding a paragraph pointer
    cmp dl, 'I'
    je .rom
    cmp dl, 'i'
    je .rom
    cmp dl, 'A'
    je .alp
    cmp dl, 'a'
    je .alp
    xor cx, cx                      ; --- arabic ---------------------------
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.de:
    pop ax
    add al, '0'
    xor ah, ah
    call sc_uput
    jc .no
    loop .de
    jmp short .ok
.alp:                               ; --- alphabetic: 1 = A, 27 = AA -------
    or ax, ax
    jz .ok
    xor cx, cx
    mov bx, 26
.a:
    dec ax
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .a
.ae:
    pop ax
    add al, 'A'
    cmp dl, 'a'
    jne .aup
    add al, 32
.aup:
    xor ah, ah
    call sc_uput
    jc .no
    loop .ae
    jmp short .ok
.rom:                               ; --- roman ----------------------------
    or ax, ax
    jz .ok
    cmp ax, 4000
    jae .ok
    xor bx, bx                      ; BX = the table index
.r:
    mov cx, bx
    shl cx, 1
    mov si, cx
    mov cx, [si+sc_rnvals]
    or cx, cx
    jz .ok
.rsub:
    cmp ax, cx
    jb .rnext
    sub ax, cx
    push ax
    mov si, bx
    shl si, 1
    mov si, [si+sc_rnnums]
    call sc_rnstr
    pop ax
    jc .no
    jmp short .rsub
.rnext:
    inc bx
    jmp short .r
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_rnstr - write the NUL string at CS:SI, cased by DL. CF=1 = full.
sc_rnstr:
    push ax
    push si
.l:
    mov al, [cs:si]
    or al, al
    jz .ok
    cmp dl, 'i'
    jne .up
    add al, 32
.up:
    xor ah, ah
    call sc_uput
    jc .no
    inc si
    jmp short .l
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop ax
    ret

sc_rnvals: dw 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1, 0
sc_rnnums: dw sc_rn_m, sc_rn_cm, sc_rn_d, sc_rn_cd, sc_rn_c, sc_rn_xc
           dw sc_rn_l, sc_rn_xl, sc_rn_x, sc_rn_ix, sc_rn_v, sc_rn_iv
           dw sc_rn_i, sc_rn_i
sc_rn_m:  db 'M', 0
sc_rn_cm: db 'CM', 0
sc_rn_d:  db 'D', 0
sc_rn_cd: db 'CD', 0
sc_rn_c:  db 'C', 0
sc_rn_xc: db 'XC', 0
sc_rn_l:  db 'L', 0
sc_rn_xl: db 'XL', 0
sc_rn_x:  db 'X', 0
sc_rn_ix: db 'IX', 0
sc_rn_v:  db 'V', 0
sc_rn_iv: db 'IV', 0
sc_rn_i:  db 'I', 0

; =============================================================================
; INSERT > TABLE OF CONTENTS (SPEC.md 68.9, Opus/toc.c)
;
; Word takes an entry's level from the paragraph's STYLE code (toc.c:
; iLevel = stcLevLast - vpapFetch.stc + 1) and this port has no style sheet,
; so the two radios say what it does instead - and both are facts the
; document carries rather than guesses:
;
;   Use Heading Paragraphs  a paragraph whose FIRST character is bold is a
;                           heading, and its level is 1 + left indent /
;                           SC_TABSTOP. Bold-from-the-first-character is what
;                           a heading looks like in draft view with one font,
;                           and the indent is the only paragraph-level rank
;                           the PAP carries.
;   Use Table Entry Fields  a paragraph beginning with a HIDDEN '.C.' run is
;                           an entry, the rest of that run is its text, and
;                           the level is the count of characters between the
;                           dots. Word's own table-entry convention from
;                           before the field, and hidden text is something
;                           this port really has (SPEC.md 68.1).
;
; One forward walk does both jobs: it counts ROWS by the same greedy word
; wrap the layout uses, so an entry's page is [line] / SC_PGLINES + 1 at the
; moment its paragraph starts - and the whole document is walked once rather
; than once per entry.
; =============================================================================

; -----------------------------------------------------------------------------
; sc_dotoc - the Table of Contents dialog's OK
; in:  SI = window ptr; out: nothing; clobbers as a callback
; -----------------------------------------------------------------------------
sc_dotoc:
    call sc_scget
    jc .out
    mov word [sc_tcn], 0
    mov word [sc_tcline], 0
    mov word [sc_tccol], 0
    xor bx, bx                      ; BX = the walking index
    mov byte [sc_tcpst], 1          ; a paragraph starts here
.walk:
    cmp bx, [sc_len]
    ja .done
    cmp byte [sc_tcpst], 0
    je .row
    mov byte [sc_tcpst], 0
    push bx
    call sc_tcentry                 ; is the paragraph at BX an entry?
    pop bx
.row:
    cmp bx, [sc_len]
    jae .done
    push bx
    mov ax, bx
    call sc_docb2
    pop bx
    cmp al, 13
    je .eop
    cmp al, 9
    je .tab
    inc word [sc_tccol]
    jmp short .wrap
.tab:
    mov ax, [sc_tccol]              ; a tab runs to the next default stop
    xor dx, dx
    mov cx, SC_TABSTOP
    div cx
    inc ax
    mul cx
    mov [sc_tccol], ax
    jmp short .wrap
.eop:
    inc word [sc_tcline]
    mov word [sc_tccol], 0
    mov byte [sc_tcpst], 1
    inc bx
    jmp short .walk
.wrap:
    mov ax, [sc_tccol]
    cmp ax, [sc_rcols]
    jb .next
    inc word [sc_tcline]
    mov word [sc_tccol], 0
.next:
    inc bx
    jmp short .walk
.done:
    cmp word [sc_tcn], 0
    je .none
    mov ax, [sc_cur]                ; the collection lands at the caret, as
    mov [sc_us0], ax                ; one bulk record
    mov [sc_us1], ax
    mov byte [sc_utl], 0
    call sc_usplice
    jc .drop
    call sc_scdrop
    call sc_editinv
    mov byte [sc_follow], 1
    ret
.none:
    call sc_scdrop
    mov ax, sc_m_notoc
    call sc_saymsg
    ret
.drop:
    call sc_scdrop
.out:
    ret

; -----------------------------------------------------------------------------
; sc_tcentry - is the paragraph starting at BX an entry, and if so collect it
; in:  BX = a paragraph's first character, [sc_tcline] = the row it starts on
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
sc_tcentry:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, bx                      ; CX = where the paragraph's text ends
.end:
    cmp cx, [sc_len]
    jae .have
    mov ax, cx                      ; AX is scratch here - preserving it
    call sc_docb2                   ; across the fetch is what throws the
    cmp al, 13                      ; fetched byte away
    je .have
    inc cx
    jmp short .end
.have:
    cmp bx, cx
    jae .out                        ; an empty paragraph is never an entry
    cmp byte [sc_tcsrc], 0
    jne .field
    mov ax, bx                      ; --- Use Heading Paragraphs -----------
    call sc_dchb
    test al, SCAT_BOLD
    jz .out
    call sc_tclevel                 ; DX = 1 + left indent / SC_TABSTOP
    mov si, bx
    mov di, cx
    jmp short .emit
.field:
    mov ax, bx                      ; --- Use Table Entry Fields -----------
    call sc_dchb
    test al, SCAT_HID
    jz .out
    mov ax, bx
    call sc_docb2
    cmp al, '.'
    jne .out
    mov si, bx
    inc si
    xor dx, dx
.dots:
    cmp si, cx
    jae .out
    mov ax, si
    call sc_docb2
    cmp al, '.'
    je .dotend
    inc dx
    inc si
    cmp dx, 9
    ja .out
    jmp short .dots
.dotend:
    or dx, dx
    jz .out
    inc si                          ; SI = the entry's text, DX its level
    mov di, si                      ; ...which ends where the hidden run does
.hid:
    cmp di, cx
    jae .emit
    mov ax, di
    call sc_dchb
    test al, SCAT_HID
    jz .emit
    inc di
    jmp short .hid
.emit:
    cmp dx, [sc_tcfrom]             ; From..To filters by the level either
    jb .out                         ; rule produced
    cmp dx, [sc_tcto]
    ja .out
    call sc_tctrim                  ; leading and trailing white off, as
    cmp si, di                      ; toc.c's CchSkipSpace and its loop do
    jae .out
    call sc_tcput
    inc word [sc_tcn]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sc_tclevel - DX = 1 + the left indent of the paragraph at BX, in tab stops,
; clamped to 9. Preserves all registers but DX.
sc_tclevel:
    push ax
    push bx
    push cx
    push si
    push ds
    mov cx, bx
.mark:
    cmp cx, [cs:sc_len]
    jae .tail
    mov ds, [cs:sc_dseg]
    mov si, cx
    cmp byte [si], 13
    push cs
    pop ds
    je .found
    inc cx
    jmp short .mark
.tail:
    mov al, [cs:sc_pap_tail]
    jmp short .idx
.found:
    mov ds, [cs:sc_cseg]
    mov si, cx
    mov al, [si]
    push cs
    pop ds
.idx:
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    mov ds, [cs:sc_pseg]
    mov al, [si+1]                  ; the entry's left indent, in cells
    push cs
    pop ds
    xor ah, ah
    xor dx, dx
    mov cx, SC_TABSTOP
    div cx
    mov dx, ax
    inc dx
    cmp dx, 9
    jbe .out
    mov dx, 9
.out:
    pop ds
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sc_tctrim - move SI forward and DI back over white space. Preserves all
; other registers.
sc_tctrim:
    push ax
.lead:
    cmp si, di
    jae .out
    mov ax, si
    call sc_docb2
    call sc_iswhite
    jc .trail
    inc si
    jmp short .lead
.trail:
    cmp di, si
    jbe .out
    mov ax, di
    dec ax
    call sc_docb2
    call sc_iswhite
    jc .out
    dec di
    jmp short .trail
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_tcput - write one entry into the scratch
; in:  SI..DI = its text, DX = its level, [sc_tcline] = the row it starts on
; out: nothing; preserves all registers
;
; text, a tab, the page number, and a ¶ whose PAP has the entry indented one
; SC_TABSTOP per level below the first - so the levels show as indents the
; ruler can see rather than as spaces in the text.
; -----------------------------------------------------------------------------
sc_tcput:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, dx                      ; the level is BANKED before anything
                                    ; else: the page division below writes
                                    ; DX, and an indent taken from the
                                    ; remainder wraps every entry to one
                                    ; character a row
.text:
    cmp si, di
    jae .tab
    mov ax, si
    call sc_docb2
    xor ah, ah
    call sc_uput
    jc .out
    inc si
    jmp short .text
.tab:
    mov al, 9
    xor ah, ah
    call sc_uput
    jc .out
    mov ax, [sc_tcline]             ; the page this row falls on
    xor dx, dx
    mov cx, SC_PGLINES
    div cx
    inc ax
    mov dl, '1'
    call sc_rnval
    jc .out
    mov word [sc_pfb], 0            ; the entry's own paragraph format: one
    mov word [sc_pfb+2], 0          ; SC_TABSTOP of left indent per level
    mov ax, bx                      ; below the first
    dec ax
    mov cl, SC_TABSTOP
    mul cl
    mov [sc_pfb+1], al
    call sc_papfind
    jc .plain
    jmp short .mark
.plain:
    xor al, al
.mark:
    mov ah, al
    mov al, 13
    call sc_uput
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
