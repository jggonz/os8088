; =============================================================================
; os8088 - apps/artful/atdoc.inc
;
; ArtfulType's document core: the gap buffer, the visual-line layout, and the
; incremental relayout (SPEC.md 46.2/46.3).
;
; THE GAP BUFFER. The document is always canonical markdown text (newline =
; 10) in one AT_DOCCAP-byte buffer with a movable gap: text is
; [0..at_gs) ++ [at_ge..AT_DOCCAP), logical length = at_gs + AT_DOCCAP-at_ge.
; The gap moves to the caret only when an EDIT happens (at_gapto), so a
; keystroke costs a byte store plus the gap's travel since the last edit -
; caret locality keeps that travel small, which is the difference between a
; 4.77MHz 8088 keeping up with typing and visibly not. Caret MOVEMENT never
; touches the gap; at_getb branches around it instead.
;
; THE LAYOUT TABLE. at_lstart[] holds each visual line's starting LOGICAL
; offset (gap-independent, so the table survives gap moves untouched);
; at_lattr[] carries the line's heading level (bits 0-1), a continuation
; flag (bit 2, set on the second and later wraps of a logical line) and the
; inline-span state at the line's start (bits 4-7: bold/italic/code/strike,
; carried by the same scan so a span crosses a WRAP correctly - but never a
; newline: span state RESETS at each logical line end, which is also what
; makes paragraphs lay out independently of each other).
;
; Word wrap measures RAW characters (delimiters count toward the wrap
; column) in both view modes - that is what makes layout independent of the
; caret, so caret motion never reflows anything; hidden delimiters just
; leave styled lines a little shorter than their slot, invisible in
; ragged-right text. Only the heading CELL WIDTH differs between the modes
; (Writer scales headings, Markdown renders everything plain), so a mode
; switch is one relayout and nothing else is.
;
; INCREMENTAL RELAYOUT (at_relayout). Because paragraphs are independent,
; an ordinary edit reflows exactly one paragraph: rescan it into a small
; staging window, splice the fresh entries over the old ones, shift every
; later start by the edit's byte delta in one add loop. An edit that
; inserts or deletes newlines converges after the paragraphs it touched;
; anything that overflows the staging window (a bulk paste) falls back to
; the full at_layout, whose cost such operations already carry.
; =============================================================================

; -----------------------------------------------------------------------------
; at_dlen - AX = logical document length; preserves all else
; -----------------------------------------------------------------------------
at_dlen:
    mov ax, AT_DOCCAP
    sub ax, [at_ge]
    add ax, [at_gs]
    ret

; -----------------------------------------------------------------------------
; at_getb - read one logical byte
; in:  BX = logical offset (< length; caller's burden)
; out: AL = the byte; everything else preserved
; -----------------------------------------------------------------------------
at_getb:
    cmp bx, [at_gs]
    jb .low
    push bx
    add bx, [at_ge]
    sub bx, [at_gs]
    mov al, [bx+at_doc]
    pop bx
    ret
.low:
    mov al, [bx+at_doc]
    ret

; -----------------------------------------------------------------------------
; at_gapto - move the gap so its start sits at a logical offset
; in:  AX = logical offset (0..length)
; out: [at_gs] = AX; preserves all registers
; -----------------------------------------------------------------------------
at_gapto:
    push ax
    push cx
    push si
    push di
    cmp ax, [at_gs]
    je .done
    jb .back
    ; forward: pull [at_ge .. at_ge+n) down to start at [at_gs]
    mov cx, ax
    sub cx, [at_gs]
    mov si, [at_ge]
    mov di, [at_gs]
    add si, at_doc
    add di, at_doc
    add [at_ge], cx
    mov [at_gs], ax
    cld
    rep movsb
    jmp short .done
.back:
    ; backward: push [pos .. at_gs) up so it ends at [at_ge]
    mov cx, [at_gs]
    sub cx, ax
    mov si, [at_gs]
    dec si
    add si, at_doc
    mov di, [at_ge]
    dec di
    add di, at_doc
    sub [at_ge], cx
    mov [at_gs], ax
    std
    rep movsb
    cld                             ; DF=0 is the standing state (SPEC.md 1)
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_ins - insert one byte at the caret (the gap must sit at the caret:
;          every edit path goes through at_edit_at first)
; in:  AL = byte
; out: CF=1 document full (nothing stored), else CF=0, caret advanced
; -----------------------------------------------------------------------------
at_ins:
    push bx
    mov bx, [at_gs]
    cmp bx, [at_ge]
    jae .full
    mov [bx+at_doc], al
    inc word [at_gs]
    inc word [at_caret]
    clc
    pop bx
    ret
.full:
    stc
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_insn - insert CX bytes from DS:SI at the caret (paste/load path)
; out: CF=1 not enough room (NOTHING inserted), else CF=0
; -----------------------------------------------------------------------------
at_insn:
    push ax
    push cx
    push si
    push di
    push es
    mov ax, [at_ge]
    sub ax, [at_gs]                 ; AX = gap size = room
    cmp ax, cx
    jb .full
    push ds
    pop es
    mov di, [at_gs]
    add di, at_doc
    add [at_gs], cx
    add [at_caret], cx
    cld
    rep movsb
    clc
    jmp short .out
.full:
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_delrange - delete logical range [AX, DX)
; out: caret = AX; preserves all registers
; The range is swallowed into the gap: move the gap to AX, then widen it.
; -----------------------------------------------------------------------------
at_delrange:
    push ax
    push dx
    cmp ax, dx
    jae .out
    call at_gapto
    sub dx, ax
    add [at_ge], dx
    mov [at_caret], ax
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_copyout - copy logical range [AX, DX) to ES:DI
; in:  AX = from, DX = to (>= from), ES:DI = destination
; out: DI advanced past the copy; AX/DX preserved
; Two straight runs either side of the gap - never byte-at-a-time.
; -----------------------------------------------------------------------------
at_copyout:
    push ax
    push bx
    push cx
    push si
    cld
    mov bx, [at_gs]                 ; BX = gap start
    cmp ax, bx
    jae .high
    mov cx, dx
    cmp cx, bx
    jbe .cap
    mov cx, bx
.cap:
    sub cx, ax                      ; CX = low-run length
    mov si, ax
    add si, at_doc
    add ax, cx                      ; AX = min(to, gs) for the high run
    rep movsb
.high:
    cmp dx, ax
    jbe .out
    mov si, ax
    add si, [at_ge]
    sub si, bx
    add si, at_doc
    mov cx, dx
    sub cx, ax
    rep movsb
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_lrange - a visual line's logical [start, end) with the newline excluded
; in:  BX = line index (< at_nlines)
; out: AX = start, DX = end; preserves all else
; -----------------------------------------------------------------------------
at_lrange:
    push bx
    push si
    mov si, bx
    shl si, 1
    inc bx
    cmp bx, [at_nlines]
    jb .mid
    call at_dlen
    mov dx, ax
    jmp short .trim
.mid:
    mov dx, [si+at_lstart+2]
.trim:
    mov ax, [si+at_lstart]
    cmp dx, ax
    je .out
    push bx
    push ax                         ; at_getb answers in AL - the start's
    mov bx, dx                      ; low byte must survive it
    dec bx
    call at_getb
    cmp al, AT_NL
    pop ax
    pop bx
    jne .out
    dec dx                          ; the newline belongs to no slice
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_slice - copy a visual line's slice into at_lbuf
; in:  BX = line index
; out: CX = slice length (newline excluded), AX = slice logical start,
;      at_lbuf filled; preserves BX and all else
; -----------------------------------------------------------------------------
at_slice:
    push dx
    push di
    push es
    push ds
    pop es
    call at_lrange                  ; AX = start, DX = end
    mov cx, dx
    sub cx, ax
    cmp cx, AT_LBUFCAP
    jbe .fits
    mov cx, AT_LBUFCAP              ; cannot happen (wrap bounds a line) -
    mov dx, ax                      ; but a bound is a bound
    add dx, cx
.fits:
    mov di, at_lbuf
    call at_copyout
    pop es
    pop di
    pop dx
    ret

; -----------------------------------------------------------------------------
; at_line_of - find the visual line containing a logical offset
; in:  AX = logical offset
; out: BX = line index (last line whose start <= AX); preserves all else
; Binary search over at_lstart - the LineContaining port.
; -----------------------------------------------------------------------------
at_line_of:
    push cx
    push dx
    push si
    xor bx, bx
    mov cx, [at_nlines]
    jcxz .out
    dec cx                          ; low = 0, high = nlines-1
.loop:
    cmp bx, cx
    jae .out
    mov si, cx
    sub si, bx
    inc si
    shr si, 1
    add si, bx                      ; SI = mid = low + (high-low+1)/2
    mov dx, si
    shl si, 1
    cmp [si+at_lstart], ax
    ja .below
    mov bx, dx                      ; start <= pos: low = mid
    jmp short .loop
.below:
    mov cx, dx
    dec cx                          ; high = mid-1
    jmp short .loop
.out:
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; at_lhome - back up a visual line index to its logical line's first line
; in:  BX = line index
; out: BX = first non-continuation line at or before it; preserves all else
; -----------------------------------------------------------------------------
at_lhome:
.back:
    or bx, bx
    jz .out
    test byte [bx+at_lattr], 4
    jz .out
    dec bx
    jmp short .back
.out:
    ret

; -----------------------------------------------------------------------------
; at_lgeom - a line's cell width and row height in pixels
; in:  BX = line index
; out: AX = cell width, DX = row height; preserves all else
; Writer mode scales headings; Markdown mode consults level 0 always (the
; ClearStyles rule: raw view is uniform body text).
; -----------------------------------------------------------------------------
at_lgeom:
    push bx
    push cx
    mov al, [bx+at_lattr]
    and ax, 3
    cmp byte [at_writer], 0
    jne .lvl
    xor ax, ax
.lvl:
    mov bx, ax
    mov cl, [at_zoom]
    xor ch, ch
    shl cx, 1
    shl cx, 1
    add bx, cx                      ; BX = zoom*4 + level
    mov al, [bx+at_cellwtab]
    mov dl, [bx+at_rowhtab]
    xor ah, ah
    xor dh, dh
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_layout - full document relayout
; out: at_lstart/at_lattr/at_nlines rebuilt; preserves all registers
; -----------------------------------------------------------------------------
at_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [at_lput], 0
    mov byte [at_stgmode], 0        ; emit straight into the main table
    xor ax, ax
    push ax
    call at_dlen
    mov cx, ax
    pop ax
    call at_scan
    call at_endsnl                  ; a trailing newline owns one more,
    jnc .count                      ; empty, logical line - the caret must
    call at_emitend                 ; have somewhere to live after it
.count:
    mov ax, [at_lput]
    mov [at_nlines], ax
    or ax, ax
    jnz .out
    mov word [at_nlines], 1         ; an empty document still has one line
    mov word [at_lstart], 0
    mov byte [at_lattr], 0
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_scan - lay out logical range [AX, CX) appending entries via at_emit
; in:  AX = start (MUST be a logical-line start), CX = end (exclusive)
; out: [at_lput] advanced; clobbers AX/BX/DX/SI/DI/BP; preserves CX
;
; One pass, one logical line at a time: detect the heading prefix, then
; walk characters accumulating cell widths, breaking at the last space when
; the next character would not fit [at_tw]. The span nibble starts at 0 for
; every logical line and is updated per delimiter (at_span); each emitted
; entry records the nibble as it stood at its own first character.
; -----------------------------------------------------------------------------
at_scan:
    push cx
    mov [at_scend], cx
    mov [at_scpos], ax
.lline:
    mov si, [at_scpos]
    cmp si, [at_scend]
    jae .done
    mov byte [at_scspan], 0         ; spans never cross a newline
    mov word [at_scskip], 0FFFFh
    ; --- heading prefix: 1..3 '#' then a space -----------------------------
    mov bx, si
    xor cx, cx
.hashes:
    cmp bx, [at_scend]
    jae .hdone
    call at_getb
    cmp al, '#'
    jne .hdone
    inc cx
    inc bx
    cmp cx, 3
    jbe .hashes
.hdone:
    or cx, cx
    jz .haveh
    cmp cx, 3
    ja .nohead
    cmp bx, [at_scend]
    jae .nohead
    call at_getb                    ; BX = first byte after the hashes
    cmp al, ' '
    je .haveh
.nohead:
    xor cx, cx
.haveh:
    mov [at_schl], cl
    ; cell width for this logical line
    mov bl, cl
    xor bh, bh
    cmp byte [at_writer], 0
    jne .wmode
    xor bx, bx
.wmode:
    mov al, [at_zoom]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    add bx, ax
    mov al, [bx+at_cellwtab]
    xor ah, ah
    mov [at_sccw], ax
    mov byte [at_sccont], 0
.vline:
    ; --- one visual line from [at_scpos] -----------------------------------
    mov si, [at_scpos]
    mov [at_sclst], si              ; this line's start (for at_respan)
    call at_emit
    mov byte [at_sccont], 4         ; any further line of this lline wraps
    mov di, [at_tw]                 ; DI = room left, px
    mov bp, 0FFFFh                  ; BP = last space position (none)
    mov dl, [at_scspan]
.ch:
    mov bx, si
    cmp bx, [at_scend]
    jae .enddoc
    call at_getb
    cmp al, AT_NL
    je .endline
    call at_span
    cmp di, [at_sccw]
    jb .wrap
    sub di, [at_sccw]
    cmp al, ' '
    jne .nsp
    mov bp, si
.nsp:
    inc si
    jmp short .ch
.wrap:
    cmp bp, 0FFFFh
    je .hard
    mov si, bp
    inc si                          ; break after the last space
.hard:
    mov [at_scpos], si
    call at_respan                  ; the walk ran past the break: re-derive
    jmp short .vline                ; the nibble for the next line's start
.endline:
    inc si                          ; step over the newline
    mov [at_scpos], si
    jmp .lline
.enddoc:
    mov [at_scpos], si
.done:
    pop cx
    ret

; -----------------------------------------------------------------------------
; at_respan - recompute the span nibble over [at_sclst, at_scpos)
; out: [at_scspan] = the nibble at at_scpos; clobbers AX/DL
; Lines are at most ~90 characters, so the rescan is noise - and it is what
; keeps the nibble exact when a space-break rewinds the scan position.
; -----------------------------------------------------------------------------
at_respan:
    push bx
    push si
    mov si, [at_sclst]
    mov dl, [at_scspan]
    mov word [at_scskip], 0FFFFh
.walk:
    cmp si, [at_scpos]
    jae .out
    mov bx, si
    call at_getb
    call at_span
    inc si
    jmp short .walk
.out:
    mov [at_scspan], dl
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_span - update the inline-span nibble for one character
; in:  AL = char, DL bits 4-7 = state (AT_SP_*), BX = the char's logical pos
; out: DL updated; preserves AL/BX and everything else
;
; Toggle semantics (SPEC.md 46.3): '`' toggles code; while code is on,
; nothing else is a delimiter. '**' toggles bold (both chars consumed via
; [at_scskip]), a lone '*' toggles italic, '~~' toggles strike, a lone '~'
; is literal.
; -----------------------------------------------------------------------------
AT_SP_B  equ 10h
AT_SP_I  equ 20h
AT_SP_C  equ 40h
AT_SP_S  equ 80h

at_span:
    cmp bx, [at_scskip]
    je .skip
    cmp al, '`'
    je .code
    test dl, AT_SP_C
    jnz .lit                        ; inside code: everything else literal
    cmp al, '*'
    je .star
    cmp al, '~'
    je .tilde
.lit:
    ret
.skip:
    ret
.code:
    xor dl, AT_SP_C
    ret
.star:
    push ax
    call at_peek
    cmp al, '*'
    pop ax
    je .bold
    xor dl, AT_SP_I
    ret
.bold:
    xor dl, AT_SP_B
    jmp short .eat2
.tilde:
    push ax
    call at_peek
    cmp al, '~'
    pop ax
    jne .lit
    xor dl, AT_SP_S
.eat2:
    push bx
    inc bx
    mov [at_scskip], bx
    pop bx
    ret

; at_endsnl - CF=1 when the document is nonempty and ends with a newline
at_endsnl:
    push ax
    push bx
    call at_dlen
    or ax, ax
    jz .no
    mov bx, ax
    dec bx
    call at_getb
    cmp al, AT_NL
    jne .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop bx
    pop ax
    ret

; at_emitend - append the trailing empty line (start = document length)
at_emitend:
    push ax
    push si
    mov byte [at_schl], 0
    mov byte [at_sccont], 0
    mov byte [at_scspan], 0
    call at_dlen
    mov si, ax
    call at_emit
    pop si
    pop ax
    ret

; at_peek - AL = logical byte at BX+1, or 0 at/past [at_scend]; preserves BX
at_peek:
    push bx
    inc bx
    cmp bx, [at_scend]
    jae .no
    call at_getb
    pop bx
    ret
.no:
    xor al, al
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_emit - append one line entry (main table, or staging under relayout)
; in:  SI = the line's logical start; [at_schl]/[at_sccont]/[at_scspan]
; out: [at_lput] advanced; a full table/window quietly stops growing (the
;      relayout treats a full window as its fallback cue, and at_inslim
;      refuses input long before the main table can fill)
; -----------------------------------------------------------------------------
at_emit:
    push ax
    push bx
    mov al, [at_schl]
    or al, [at_sccont]
    or al, [at_scspan]
    mov bx, [at_lput]
    cmp byte [at_stgmode], 0
    jne .stg
    cmp bx, AT_MAXLN
    jae .out
    mov [bx+at_lattr], al
    shl bx, 1
    mov [bx+at_lstart], si
    jmp short .adv
.stg:
    cmp bx, AT_STGCAP
    jae .out
    mov [bx+at_stga], al
    shl bx, 1
    mov [bx+at_stgs], si
.adv:
    inc word [at_lput]
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_relayout - incremental relayout after an edit
; in:  AX = lowest logical offset touched (NEW coordinates),
;      DX = SIGNED byte delta (inserted - deleted)
; out: table updated; [at_dfrom] = first visual line whose pixels may have
;      changed (the repaint cue); preserves all registers
; -----------------------------------------------------------------------------
at_relayout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [at_rldel], dx
    ; F = first visual line of the logical line containing the edit. An
    ; edit AT a line boundary can affect the PREVIOUS visual line too (a
    ; deleted char can let the next word wrap back up), which backing up to
    ; the logical line start already covers - a wrap can only move within
    ; its own paragraph. But an edit that JOINED two paragraphs (backspace
    ; over a newline) sits at the join, whose logical line now begins in
    ; what was the earlier paragraph - at_line_of + at_lhome lands there
    ; correctly because the table still describes the OLD document: use
    ; the OLD coordinate of the edit point for the search.
    mov bx, ax
    sub bx, dx                      ; edits report the NEW offset; the table
    jns .oldok                      ; is old - search with the old offset,
    xor bx, bx                      ; clamped (a pure insert at 0)
.oldok:
    ; at_line_of expects AX; keep the smaller of new/old so a deletion's
    ; whole former extent is covered by the rescan start
    cmp ax, bx
    jbe .useax
    mov ax, bx
.useax:
    call at_line_of
    call at_lhome
    mov [at_dfrom], bx
    ; ...but at_lstart[F] is an OLD offset; for entries at or before the
    ; edit point old == new (the edit only moved what follows it), so it
    ; is valid as a NEW rescan start too.
    mov si, bx
    shl si, 1
    mov ax, [si+at_lstart]
    mov [at_rlst], ax
    ; --- rescan paragraphs into staging until convergence ------------------
    mov word [at_lput], 0
    mov byte [at_stgmode], 1
    mov ax, [at_rlst]
.para:
    ; scan limit = end of the logical line at AX (one past its newline)
    push ax
    call at_dlen
    mov cx, ax
    pop ax
    push ax                         ; the start must survive the walk -
    mov bx, ax                      ; at_getb answers in AL
.findnl:
    cmp bx, cx
    jae .atend
    call at_getb
    cmp al, AT_NL
    je .nl
    inc bx
    jmp short .findnl
.nl:
    inc bx
    mov cx, bx                      ; CX = this paragraph's end
.atend:
    pop ax
    call at_scan                    ; appends this paragraph's lines
    mov ax, [at_lput]
    cmp ax, AT_STGCAP
    jae .fallback                   ; window full: bulk edit, full layout
    mov ax, cx                      ; AX = next paragraph's start (new)
    push cx
    call at_dlen
    mov cx, ax
    pop dx                          ; DX = next paragraph start (new)
    cmp dx, cx
    jb .converge                    ; more document follows
    call at_endsnl                  ; the end: a trailing newline still
    jnc .tailend                    ; owes its empty line
    call at_emitend
    jmp short .tailend
.converge:
    ; convergence: the old table must hold a NON-CONTINUATION entry whose
    ; start == DX - delta, strictly after F
    mov ax, dx
    sub ax, [at_rldel]
    call at_line_of                 ; BX = candidate old line
    mov si, bx
    shl si, 1
    cmp [si+at_lstart], ax
    jne .more
    test byte [bx+at_lattr], 4
    jnz .more
    cmp bx, [at_dfrom]
    jbe .more                       ; must make progress
    mov [at_rlj], bx
    jmp short .splice
.more:
    mov ax, dx                      ; not yet: scan the next paragraph too
    jmp short .para
.tailend:
    mov ax, [at_nlines]
    mov [at_rlj], ax                ; nothing survives
.splice:
    mov ax, [at_lput]
    mov [at_rlk], ax                ; staged count = the changed-line span
    call at_splice
    jmp short .done
.fallback:
    mov byte [at_stgmode], 0
    mov byte [at_rlfull], 1         ; the repaint must go whole-screen
    call at_layout
.done:
    mov byte [at_stgmode], 0
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_splice - install the staging window into the main table
; in:  [at_dfrom] = F, [at_lput] = K fresh entries in staging,
;      [at_rlj] = J, the first OLD index that survives, [at_rldel] = delta
; out: table/at_nlines final; clobbers AX/BX/CX/SI/DI
;
; Order matters: the surviving tail is moved (and delta-shifted) FIRST,
; because staging entries land where the old F..J region was.
; -----------------------------------------------------------------------------
at_splice:
    push es
    push ds
    pop es
    cld
    ; new tail index = F + K
    mov di, [at_dfrom]
    add di, [at_lput]               ; DI = destination index
    mov si, [at_rlj]                ; SI = source index
    mov cx, [at_nlines]
    sub cx, si                      ; CX = surviving entries
    ; final line count (clamped to the table)
    mov ax, di
    add ax, cx
    cmp ax, AT_MAXLN
    jbe .fits
    mov ax, AT_MAXLN
    sub ax, di
    jns .cok
    xor ax, ax
.cok:
    mov cx, ax
    mov ax, AT_MAXLN
.fits:
    mov [at_nlines], ax
    jcxz .fresh
    ; move starts (words) + attrs (bytes); pick direction by index motion
    cmp di, si
    je .shift
    ja .movup
    ; moving down: forward copy
    push cx
    push si
    push di
    shl si, 1
    shl di, 1
    add si, at_lstart
    add di, at_lstart
    rep movsw
    pop di
    pop si
    pop cx
    push cx
    push di
    add si, at_lattr
    add di, at_lattr
    rep movsb
    pop di
    pop cx
    jmp short .shift
.movup:
    ; moving up: backward copy from the last entry
    push cx
    push si
    push di
    mov ax, cx
    dec ax
    add si, ax
    add di, ax
    shl si, 1
    shl di, 1
    add si, at_lstart
    add di, at_lstart
    std
    rep movsw
    cld
    pop di
    pop si
    pop cx
    push cx
    push di
    mov ax, cx
    dec ax
    add si, ax
    add di, ax
    add si, at_lattr
    add di, at_lattr
    std
    rep movsb
    cld
    pop di
    pop cx
.shift:
    ; delta-shift the moved tail's starts
    mov si, di
    shl si, 1
    add si, at_lstart
    mov ax, [at_rldel]
.shl:
    add [si], ax
    add si, 2
    loop .shl
.fresh:
    ; copy staging into [F .. F+K) - clamped like the tail move above:
    ; a paste can put F+K past the table, and an unclamped copy would
    ; run at_lstart into at_lattr and at_lattr into the scan state
    mov cx, [at_lput]
    jcxz .none
    mov ax, AT_MAXLN
    sub ax, [at_dfrom]              ; AX = entries the table has room for
    jle .none
    cmp cx, ax
    jbe .stg
    mov cx, ax
.stg:
    mov si, at_stgs
    mov di, [at_dfrom]
    shl di, 1
    add di, at_lstart
    push cx
    rep movsw
    pop cx
    mov si, at_stga
    mov di, [at_dfrom]
    add di, at_lattr
    rep movsb
.none:
    ; a fully deleted document still has one line
    cmp word [at_nlines], 0
    jne .out
    mov word [at_nlines], 1
    mov word [at_lstart], 0
    mov byte [at_lattr], 0
.out:
    pop es
    ret
