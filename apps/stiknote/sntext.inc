; =============================================================================
; apps/stiknote/sntext.inc - the notes: a folder of text files (SPEC.md 110.13)
;
; A NOTE is any *.TXT in SYSTEM\APPDATA\NOTES on the boot volume, in directory
; order - a file Note Pad, a Disk window or a DOS PC put there is a note too.
; Nothing of them is held but the CURRENT one's first SN_HEAD bytes (the
; HEAD): the pad shows a title and at most eight lines of 21 cells, under 210
; bytes of file with CR LF on every line, and a longer note shows its top.
;
; THE WALK (sn_walk) is one OSAPI_FILE_FIND pass over the folder, and it is
; the whole of the directory the program keeps: it counts the notes, finds
; the current one by NAME (adding or deleting a note does not move it) or by
; its place, marks the NOTEnnnn numbers in use for New and the colour entries
; whose notes still exist. Turning to a note is one walk and one
; OSAPI_FILE_READ_AT of the head (decision 4: read on demand, no directory
; held in RAM).
;
; THE TEXT is Note Pad's: CR LF line ends (a lone CR or LF reads as one), and
; a byte outside 32..126 is not drawn and takes no cell, as np_nlfold drops
; it. Line 1 is the TITLE. A body line beginning `[ ] `, `[x] ` or `[X] ` is a
; CHECK BOX, drawn as the box glyph with the four bytes hidden: Note Pad and a
; DOS PC show the brackets, which read correctly.
;
; THE WALK OF THE TEXT (sn_body, sn_vline, sn_vadv) cuts the body into VISUAL
; lines for a width in cells - [it_w] for a text paragraph, [it_wb] for a
; box's, which is indented. A line that does not fit breaks after its last
; space (the space is the line's end and is not drawn), or hard at the width
; when it has none.
; =============================================================================

; -----------------------------------------------------------------------------
; sn_go - stand in SYSTEM\APPDATA\NOTES on HOME (os88stik.inc's stik_enter:
; is the disk in the drive still home?). CF = 1 it cannot be reached, and the
; instance is home again. Spends AX..DI and ES
;
; NOTES IS BANKED (SPEC.md 110.13.2). The first visit walks - SYSTEM, APPDATA
; and NOTES each found by name in its parent, NOTES made when it is gone - and
; keeps NOTES' first cluster in [sn_ndir] with the witness's answer in
; [sn_okey]: the first cluster of our own file in home. Every later visit is
; stik_enter's home stand and witness and then ONE word to the bank, which is
; trusted only on a disk whose copy of us is at the cluster it was at: a disk
; that passes the witness but is laid out otherwise walks. What is done there
; is by name or a count, so a bank that is not NOTES any more - a folder
; deleted under the pad - can only find nothing, and a banked walk that
; counts no notes walks by name once before it is believed (sn_rdin, by
; [sn_bank]). What is WRITTEN where no name was just found - a new note, the
; folder itself, a note handed to the editor - stands on a WALK: sn_gow,
; apps/os88data.inc's rule
; -----------------------------------------------------------------------------
sn_gow:
    mov word [sn_ndir], 0       ; by name, whatever is banked
sn_go:
    test byte [stik_st], STIK_S_NOHOME
    stc
    jnz .r                      ; no SYSTEM\APPDATA: no notes either
    mov dx, [sn_ndir]
    call stik_enter             ; HOME? then the bank, or (0) SYSTEM\APPDATA
    jc .r                       ; ...AX = our own file's first cluster
    mov [sn_bank], dx           ; how we came: the bank, or 0 a walk
    or dx, dx
    jz .walk
    cmp ax, [sn_okey]           ; the disk the bank was made on? (CF = 0
    jne sn_gow                  ; when it is) - else by name, from home
    ret
.walk:
    mov [sn_okey], ax
    mov si, sn_dnotes
    call stik_seek              ; AX = its type, DX = its first cluster
    jnc .got
    call OSAPI_FILE_MKDIR       ; a disk not built by us, or a user who
    mov si, sn_dnotes           ; deleted it: one cluster, 14 or 30 notes
    call stik_seek
    jc .no
.got:
    cmp al, OSAPI_FT_DIR        ; (a type is 0..3)
    jne .no                     ; a FILE called NOTES
    call stik_goqm
    jc .no
    mov [sn_ndir], dx           ; banked once it has been stood in
.r:
    ret
.no:
    call stik_leave
    stc
    ret

; sn_read - the current note again: AL = 0 by its NAME (and by [sn_want],
; its place, when the name is gone), else by [sn_want] alone; the walk and
; the head. Nothing changes when the folder cannot be reached. Spends AX..DI
; and ES
sn_read:
    mov [sn_byo], al
    call sn_go
    jc sn_rhead.r
; sn_rdin - sn_read's, standing in NOTES already: the walk, the head, home
sn_rdin:
    call sn_walk
    jnc .h
    cmp word [sn_bank], 0       ; NO NOTES, BY THE BANK (sn_walk dropped
    je .l                       ; nothing): by name, once
    call sn_gow
    jc .r                       ; (sn_go is home again on a refusal)
    call sn_walk
    jc .l
.h:
    call sn_rhead
.l:
    jmp stik_leave
.r:
    ret

; sn_rhead - the current note's first SN_HEAD bytes into sn_hbuf, [sn_hlen]
; of them (0 with no notes, or a read refused). OSAPI_FILE_READ_AT is RAW,
; so a COMPRESSED file (FIND's +22) is read whole by OSAPI_FILE_READ, which
; expands it - into a head that holds it, or not at all. Spends AX..DI, ES
sn_rhead:
    xor ax, ax
    cmp [sn_n], al
    je .put
    xor dx, dx                  ; DX:AX = offset 0 / DX:CX = the capacity
    mov cx, SN_HEAD
    mov bx, sn_hbuf
    mov si, sn_doc
    push ds
    pop es
    test byte [sn_cz], OSAPI_FIND_CZ
    jz .at
    call OSAPI_FILE_READ
    jmp short .got
.at:
    call OSAPI_FILE_READ_AT     ; kern_big's: whole SECTORS (SPEC.md 18.4.4.2)
.got:
    jnc .put
    xor ax, ax
.put:
    mov [sn_hlen], ax
.r:
    ret

; -----------------------------------------------------------------------------
; sn_walk - the folder, once (standing in it): [sn_n] the notes, [sn_cur]
; the current one's place and sn_doc its name - the payload's name when it is
; there and [sn_byo] = 0, else the note at [sn_want] or the last before it.
; The payload's name follows sn_doc (a change is owed a save); [sn_used] the
; NOTEnnnn numbers 1..32 in use; and a colour entry whose note is gone is
; dropped. CF = 1 the walk failed part way (nothing dropped). Spends AX..DI
; and ES
; -----------------------------------------------------------------------------
sn_walk:
    push ds
    pop es
    cld
    xor ax, ax
    mov di, sn_wz               ; sn_n, sn_cur, sn_hit, the two bit sets
    mov cx, SN_WZ
    rep stosb
    mov [sn_doc], al
.next:                          ; CX = the ordinal
    push ds
    pop es
    mov di, stik_fnd
    call OSAPI_FILE_FIND        ; CX = the next one
    jc .end
    push cx
    cmp word [stik_fnd + 14], OSAPI_FT_DIR
    jae .skip                   ; a folder, or '..'
    mov si, stik_fnd
.dot:
    lodsb
    or al, al
    jz .skip
    cmp al, '.'
    jne .dot
    cmp word [si], 'TX'
    jne .skip
    cmp word [si + 2], 'T'      ; 'T', NUL
    jne .skip
    call sn_isnum
    mov si, stik_fnd
    call sn_colfind             ; CL = its colour entry's index
    jc .nc
    mov al, cl
    mov bx, sn_seen
    call sn_bset
.nc:
    cmp byte [sn_hit], 0
    jne .cnt                    ; found by name: the rest only counts
    cmp byte [sn_byo], 0
    jne .ord
    mov si, stik_fnd
    mov di, STIK_PAY + SNP_NAME
    call sn_streq
    jne .ord
    inc byte [sn_hit]
    jmp short .take
.ord:
    mov al, [sn_n]              ; by place: every note up to [sn_want],
    cmp al, [sn_want]           ; so a place past the end is the last
    ja .cnt
.take:
    mov al, [sn_n]
    mov [sn_cur], al
    mov al, [stik_fnd + 22]
    mov [sn_cz], al
    mov si, stik_fnd
    mov di, sn_doc
    call sn_cpy
.cnt:
    inc byte [sn_n]
.skip:
    pop cx
    jmp .next
.end:
    cmp ax, FERR_NOENT          ; the end, not a failure
    stc
    jne .r
    cmp [sn_n], ah              ; NONE, AND WE CAME BY THE BANK (sn_go): it
    jne .some                   ; may not be NOTES any more. Nothing dropped,
    cmp word [sn_bank], 0       ; CF = 1, and sn_rdin walks to it by name
    stc                         ; (AH = 0: FERR_NOENT's high byte)
    jne .r
.some:
    push ds
    pop es
    mov cl, [STIK_PAY + SNP_NC] ; a colour no note is wearing goes
.sw:
    dec cl
    js .nm
    mov al, cl
    mov bx, sn_seen
    call sn_btest
    jnz .sw
    call sn_colat
    call sn_coldel
    jmp short .sw
.nm:
    mov si, sn_doc
    call sn_setname
    clc
.r:
    ret

; sn_setname - SI = a note's name: the payload's current one, owed a save
; when it moved - the page is remembered, not an edit (ES = DS). AL, CX, SI,
; DI spent
sn_setname:
    push si
    mov di, STIK_PAY + SNP_NAME
    call sn_streq
    pop si
    je .r
    mov di, STIK_PAY + SNP_NAME
    call sn_cpy
    jmp stik_touch
.r:
    ret

; sn_isnum - stik_fnd named NOTEnnnn.TXT with nnnn 1..32: its bit in
; [sn_used]. AX, BX spent
sn_isnum:
    cmp word [stik_fnd], 'NO'
    jne .r
    cmp word [stik_fnd + 2], 'TE'
    jne .r
    cmp word [stik_fnd + 4], '00'
    jne .r
    mov ax, [stik_fnd + 6]
    sub ax, '00'                ; AL = the tens, AH = the units
    cmp al, 9
    ja .r
    cmp ah, 9
    ja .r
    xchg al, ah
    aad                         ; AL = the number
    dec ax
    cmp al, 31
    ja .r                       ; 0, or past 32: never the lowest free
    mov bx, sn_used
    jmp short sn_bset
.r:
    ret

; sn_bset / sn_btest - bit AL of the set at BX: set it / ZF = 0 it is set.
; AH, BX spent
sn_bset:
    call sn_bit
    or [bx], ah
    ret
sn_btest:
    call sn_bit
    test [bx], ah
    ret

; sn_bit - BX += AL / 8, AH = 1 << (AL & 7). The rest kept
sn_bit:
    push cx
    mov cl, al
    shr al, 1
    shr al, 1
    shr al, 1
    add bl, al
    adc bh, 0
    and cl, 7
    mov ah, 1
    shl ah, cl
    pop cx
    ret

; sn_streq - ZF = 1 the NUL-terminated names at SI and DI are equal (ES =
; DS). AL, SI, DI spent
sn_streq:
    lodsb
    scasb
    jne .r
    or al, al
    jnz sn_streq
.r:
    ret

; sn_cpy - the NUL-terminated name at SI into the 13 bytes at DI, the rest of
; them zero (ES = DS). AL, CX, SI, DI spent
sn_cpy:
    mov cx, 13
.l:
    lodsb
    stosb
    or al, al
    loopnz .l
    rep stosb                   ; AL = 0 once the NUL went
    ret

; -----------------------------------------------------------------------------
; THE COLOUR TABLE (the payload's, SPEC.md 110.13.2): an entry is a note's
; 8-byte stem, space-padded, then its paper. Only a paper that is not yellow
; has one
; -----------------------------------------------------------------------------

; sn_colfind - SI = a NUL 8.3 name: its stem into sn_stem, BX = its entry
; and CL its index; CF = 1 it has none (BX past the last, CL the count). SI
; kept; AX, CX, DI spent
sn_colfind:
    push si
    mov di, sn_stem
    mov cx, 8
.s:
    mov al, [si]
    cmp al, '.'
    je .p
    or al, al
    jz .p
    inc si
    jmp short .st
.p:
    mov al, ' '                 ; past the stem: SI stays on it
.st:
    mov [di], al
    inc di
    loop .s
    pop si
    mov bx, STIK_PAY + SNP_TAB
    cld
.f:
    cmp cl, [STIK_PAY + SNP_NC]
    cmc
    jc .r                       ; (CX = 0 on entry: CL counts)
    push si
    push cx
    mov si, sn_stem
    mov di, bx
    mov cx, 4
    push es
    push ds
    pop es
    repe cmpsw
    pop es
    pop cx
    pop si
    je .r                       ; equal: CF = 0
    add bx, SN_COLSZ
    inc cx
    jmp short .f
.r:
    ret

; sn_colat - CL = an entry's index: BX = it. AX spent
sn_colat:
    mov al, SN_COLSZ
    mul cl
    add ax, STIK_PAY + SNP_TAB
    xchg bx, ax
    ret

; sn_coldel - the entry at BX (index CL) out of the table: the ones after it
; down one, the last zeroed, owed a save. AX spent
sn_coldel:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov al, [STIK_PAY + SNP_NC]
    dec ax
    mov [STIK_PAY + SNP_NC], al
    sub al, cl                  ; the entries after it
    mov ah, SN_COLSZ
    mul ah
    xchg cx, ax
    mov di, bx
    lea si, [bx + SN_COLSZ]
    cld
    rep movsb
    mov cx, SN_COLSZ
    xor al, al
    rep stosb
    pop es
    pop di
    pop si
    pop cx
    jmp stik_touch

; -----------------------------------------------------------------------------
; THE TEXT, in the head
; -----------------------------------------------------------------------------

; sn_txt - SI = the current note's head, DI = past its last byte
sn_txt:
    mov si, sn_hbuf
    mov di, si
    add di, [sn_hlen]
    ret

; sn_tend - AX = the title's length: to its first CR or LF. The rest kept
sn_tend:
    push si
    push di
    call sn_txt
    mov ax, si
.l:
    cmp si, di
    jae .x
    cmp byte [si], 13
    je .x
    cmp byte [si], 10
    je .x
    inc si
    jmp short .l
.x:
    sub si, ax
    xchg ax, si
    pop di
    pop si
    ret

; sn_eol - AL = a line end just passed, SI past it: past the LF of a CR LF
sn_eol:
    cmp al, 13
    jne .r
    cmp si, [it_e]
    jae .r
    cmp byte [si], 10
    jne .r
    inc si
.r:
    ret

; sn_body - start the walk over the current note's body: [it_e] its end, and
; the first visual line laid out. CF = 1 the note has no body (no line end).
; AX spent
sn_body:
    push cx
    push si
    push di
    call sn_txt
    mov [it_e], di
.l:
    cmp si, di
    jae .none
    lodsb
    cmp al, 13
    je .b
    cmp al, 10
    jne .l
.b:
    call sn_eol
    call sn_vpara               ; SI = the body's first byte
    call sn_vline
    clc
    jmp short .x
.none:
    stc
.x:
    pop di
    pop si
    pop cx
    ret

; sn_vpara - SI = a paragraph's first byte: a `[ ] ` or `[x] ` there is its
; box - [it_k] 1 open, 2 ticked - and SI goes past it; [it_p] its first
; line's start, [it_f] = 1. AX spent
sn_vpara:
    mov word [it_k], 1 << 8     ; [it_k] = 0, [it_f] = 1
    lea ax, [si + 4]
    cmp ax, [it_e]
    ja .x
    cmp byte [si], '['
    jne .x
    cmp word [si + 2], '] '
    jne .x
    mov al, [si + 1]
    mov ah, 1
    cmp al, ' '
    je .k
    inc ah
    or al, 20h                  ; X or x
    cmp al, 'x'
    jne .x
.k:
    mov [it_k], ah
    add si, 4
.x:
    mov [it_p], si
    ret

; sn_vline - lay out the visual line at [it_p]: [it_le] past its content,
; [it_n] the next line's start, [it_lf] / [it_end] how it ended. Every
; register kept
sn_vline:
    push ax
    push bx
    push cx
    push si
    mov si, [it_p]
    mov cl, [it_w]
    cmp byte [it_k], 0
    je .w
    mov cl, [it_wb]
.w:
    xor ch, ch
    mov word [it_lf], 0         ; [it_lf] and [it_end]
    xor bx, bx                  ; the last space seen, 0 none
.s:
    cmp si, [it_e]
    jae .eon
    mov al, [si]
    cmp al, 13
    je .lf
    cmp al, 10
    je .lf
    jcxz .full
    cmp al, ' '
    jne .ns
    mov bx, si
.ns:
    inc si
    cmp al, ' '                 ; a byte Note Pad drops takes no cell
    jb .s
    cmp al, 126
    ja .s
    dec cx
    jmp short .s
.full:                          ; SI = the first byte that does not fit
    cmp al, ' '
    je .sp
    or bx, bx
    jz .hard
    mov si, bx                  ; back to the last space
.sp:
    mov [it_le], si
    inc si
    mov [it_n], si
    jmp short .x
.hard:
    cmp si, [it_p]
    jne .h
    inc si                      ; a width of 0 would never move on
.h:
    mov [it_le], si
    mov [it_n], si
    jmp short .x
.lf:
    mov [it_le], si
    inc si
    call sn_eol
    mov [it_n], si
    mov byte [it_lf], 1
    jmp short .x
.eon:
    mov [it_le], si
    mov [it_n], si
    mov byte [it_end], 1
.x:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sn_vadv - on to the next visual line, laid out. CF = 1 there is none. Every
; register kept
sn_vadv:
    cmp byte [it_end], 0
    jne .none
    push ax
    push si
    mov si, [it_n]
    cmp byte [it_lf], 0
    je .cont
    call sn_vpara
    jmp short .l
.cont:
    mov [it_p], si
    mov byte [it_f], 0
.l:
    call sn_vline
    pop si
    pop ax
    clc
    ret
.none:
    stc
    ret

; sn_vseek - the walk onto body line AX (0-based). CF = 1 there is no such
; line (the walk is then on the last). Every register kept
sn_vseek:
    push ax
    push cx
    mov cx, ax
    call sn_body
    jc .x
    jcxz .x
.l:
    call sn_vadv
    jc .x
    loop .l
    clc
.x:
    pop cx
    pop ax
    ret
