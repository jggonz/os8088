; =============================================================================
; os8088 - apps/waverack/wredit.inc - editing, undo, the clipboard (SPEC.md
;          123.3, 123.9)
;
; EVERY EDIT BUILDS A NEW PIECE LIST and moves no audio. The list is built in
; wx_tmp by wx_range (the document's pieces over a frame range) and wx_emit,
; which COALESCES - a piece that continues the last one's source where it
; stopped extends it, so a cut pasted back where it came from is one piece
; again and repeated edits do not leak pieces. A result past WO_NPC is
; refused before anything changes (`Too many edits; save`). tools/wrsim.py's
; pc_range / pc_add / Editor are this file in Python, and the soak row
; tests/waverackedit.py holds the guest's piece table to them.
;
; THE HISTORY is one heap claim (WX_UKB) of WX_NSLOT = WO_NUNDO + 1 states,
; a ring: a state is the piece list, the cursor, the selection and its
; anchor. The current state is slot [wo_upos] of [wo_un]; an edit writes the
; selection the user has NOW into it, drops the redo side, and appends its
; result, the oldest falling off - so Undo is upos - 1 and Redo upos + 1,
; eight steps each way at most. The claim is made by the first edit and given
; back by New, Open and a save over a source (wx_uclear).
;
; THE CLIPBOARD is WaveRack's own (SPEC.md 123.3): at most WO_NCLIP pieces
; and the format they were cut in, kept for the instance's life across New
; and Open. A paste into a document of another format is refused, naming the
; clipboard's; a paste into an EMPTY document (after New) adopts it.
;
; SOURCES ARE SWEPT, NOT COUNTED: wo_sweep keeps a source while the document,
; a state in the history or the clipboard names it, and gives back every
; other one (its envelope claim with it). Four is the most there are, so a
; scan of every list is a few hundred compares - cheaper in bytes than four
; counters kept right through every edit, undo, paste and drop.
; =============================================================================

WO_NCLIP    equ 16                  ; the clipboard's pieces
WO_NUNDO    equ 8                   ; undo steps (SPEC.md 123.3)
WX_NSLOT    equ WO_NUNDO + 1        ; ...the states the claim holds
WX_UHDR     equ 22                  ; a state: npc, frames, cur, s, e, anc...
WX_USLOT    equ WX_UHDR + WO_NPC * WP_SIZE  ; ...and the pieces: 662 bytes
WX_UKB      equ (WX_NSLOT * WX_USLOT + 1023) / 1024     ; 6 KB

; =============================================================================
; BUILDING A PIECE LIST
; =============================================================================

; wx_begin - an empty list in wx_tmp. keeps all
wx_begin:
    mov word [wx_tn], 0
    mov byte [wx_tov], 0
    ret

; wx_tptr - BX = an index into wx_tmp -> BX = that piece. keeps the rest
wx_tptr:
    push ax
    push dx
    mov ax, WP_SIZE
    mul bx
    add ax, wx_tmp
    mov bx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wx_emit - the piece {[wx_es], [wx_ef], [wx_ec]} onto wx_tmp, COALESCED with
;           the last one when it continues it; an empty one is nothing; past
;           WO_NPC pieces [wx_tov] is set and nothing more is kept.
;           keeps every register
; -----------------------------------------------------------------------------
wx_emit:
    push ax
    push bx
    push dx
    mov ax, [wx_ec]
    or ax, [wx_ec+2]
    jz .out
    mov bx, [wx_tn]
    or bx, bx
    jz .new
    dec bx
    call wx_tptr                    ; BX = the last piece
    mov ax, [bx+WP_SRC]
    cmp ax, [wx_es]
    jne .new
    mov ax, [bx+WP_FIRST]           ; does it end where this one begins?
    mov dx, [bx+WP_FIRST+2]
    add ax, [bx+WP_CNT]
    adc dx, [bx+WP_CNT+2]
    cmp ax, [wx_ef]
    jne .new
    cmp dx, [wx_ef+2]
    jne .new
    mov ax, [wx_ec]                 ; yes: it grows
    add [bx+WP_CNT], ax
    mov ax, [wx_ec+2]
    adc [bx+WP_CNT+2], ax
    jmp short .out
.new:
    mov bx, [wx_tn]
    cmp bx, WO_NPC
    jb .room
    mov byte [wx_tov], 1            ; too many: refused by wx_commit
    jmp short .out
.room:
    inc word [wx_tn]
    call wx_tptr
    mov ax, [wx_es]
    mov [bx+WP_SRC], ax
    mov ax, [wx_ef]
    mov [bx+WP_FIRST], ax
    mov ax, [wx_ef+2]
    mov [bx+WP_FIRST+2], ax
    mov ax, [wx_ec]
    mov [bx+WP_CNT], ax
    mov ax, [wx_ec+2]
    mov [bx+WP_CNT+2], ax
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wx_range - the document's frames [DX:AX, CX:BX) onto wx_tmp, piece by piece
;            (tools/wrsim.py's pc_range). clobbers AX, BX, CX, DX
; -----------------------------------------------------------------------------
wx_range:
    mov [wx_ra], ax
    mov [wx_ra+2], dx
    mov [wx_rb], bx
    mov [wx_rb+2], cx
    push si
    push di
    xor ax, ax
    mov [wx_rpos], ax
    mov [wx_rpos+2], ax
    mov si, wo_pc
    mov cx, [wo_npc]
.p:
    or cx, cx
    jnz .pc
    jmp .out
.pc:
    mov ax, [si+WP_CNT]             ; hi = min(b, pos + count)
    mov dx, [si+WP_CNT+2]
    add ax, [wx_rpos]
    adc dx, [wx_rpos+2]
    cmp dx, [wx_rb+2]
    jb .h
    ja .hb
    cmp ax, [wx_rb]
    jbe .h
.hb:
    mov ax, [wx_rb]
    mov dx, [wx_rb+2]
.h:
    mov bx, [wx_rpos]               ; lo = max(a, pos) -> DI:BX
    mov di, [wx_rpos+2]
    cmp di, [wx_ra+2]
    ja .l
    jb .la
    cmp bx, [wx_ra]
    jae .l
.la:
    mov bx, [wx_ra]
    mov di, [wx_ra+2]
.l:
    sub ax, bx                      ; the count, hi - lo: none when it borrows
    sbb dx, di
    jc .skip
    mov [wx_ec], ax
    mov [wx_ec+2], dx
    sub bx, [wx_rpos]               ; its first: the piece's + (lo - pos)
    sbb di, [wx_rpos+2]
    add bx, [si+WP_FIRST]
    adc di, [si+WP_FIRST+2]
    mov [wx_ef], bx
    mov [wx_ef+2], di
    mov ax, [si+WP_SRC]
    mov [wx_es], ax
    call wx_emit
.skip:
    mov ax, [si+WP_CNT]             ; pos += count
    add [wx_rpos], ax
    mov ax, [si+WP_CNT+2]
    adc [wx_rpos+2], ax
    add si, WP_SIZE
    dec cx
    jmp .p
.out:
    pop di
    pop si
    ret

; wx_upto - the document's [0, DX:AX) onto wx_tmp. clobbers AX, BX, CX, DX
wx_upto:
    mov bx, ax
    mov cx, dx
    xor ax, ax
    xor dx, dx
    jmp wx_range

; wx_rest - the document's [DX:AX, frames) onto wx_tmp. clobbers AX, BX,
; CX, DX
wx_rest:
    mov bx, [wo_frames]
    mov cx, [wo_frames+2]
    jmp wx_range

; wx_sum - SI = a list, CX = its pieces -> DX:AX = their frames. keeps the rest
wx_sum:
    push cx
    push si
    xor ax, ax
    xor dx, dx
    jcxz .out
.p:
    add ax, [si+WP_CNT]
    adc dx, [si+WP_CNT+2]
    add si, WP_SIZE
    loop .p
.out:
    pop si
    pop cx
    ret

; =============================================================================
; THE COMMANDS (wr_cmd's; each is live - wr_cmdok asked first). Every one
; in: BX = the window. clobbers AX, BX, CX, DX, SI, DI
; =============================================================================

; Trim: the document becomes [s, e); the cursor at nought, no selection
wx_trim:
    call wx_begin
    mov ax, [wo_s]
    mov dx, [wo_s+2]
    mov bx, [wo_e]
    mov cx, [wo_e+2]
    call wx_range
    xor ax, ax
    mov [wx_na], ax
    mov [wx_na+2], ax
    mov [wx_nb], ax
    mov [wx_nb+2], ax
    mov si, wr_s_trim
    jmp wx_commit

; Delete: [s, e) goes and the gap closes; the cursor at the seam
wx_delete:
    mov si, wr_s_del
wx_delsay:
    push si
    call wx_begin
    mov ax, [wo_s]
    mov dx, [wo_s+2]
    call wx_upto
    mov ax, [wo_e]
    mov dx, [wo_e+2]
    call wx_rest
    mov ax, [wo_s]
    mov dx, [wo_s+2]
    mov [wx_na], ax
    mov [wx_na+2], dx
    mov [wx_nb], ax
    mov [wx_nb+2], dx
    pop si
    jmp wx_commit

; Copy: [s, e) to the clipboard, in the document's format. The document is
; untouched (no history, not dirty). CF = 1 refused (more than WO_NCLIP
; pieces: `Selection too complex`)
wx_copy:
    call wx_begin
    mov ax, [wo_s]
    mov dx, [wo_s+2]
    mov bx, [wo_e]
    mov cx, [wo_e+2]
    call wx_range
    cmp word [wx_tn], WO_NCLIP
    jbe .fits
    mov si, wr_s_cplx
    call wr_say
    stc
    ret
.fits:
    mov cx, [wx_tn]
    mov [wo_ncp], cx
    mov ax, WP_SIZE / 2
    mul cx
    mov cx, ax
    mov si, wx_tmp
    mov di, wo_clip
    push es
    push ds
    pop es
    cld
    rep movsw
    pop es
    mov si, wo_clip
    mov cx, [wo_ncp]
    call wx_sum
    mov [wo_cfr], ax
    mov [wo_cfr+2], dx
    mov ax, [wo_rate]               ; ...and the format it was cut in
    mov [wo_crate], ax
    mov al, [wo_bits]
    mov [wo_cbits], al
    mov al, [wo_ch]
    mov [wo_cch], al
    mov al, [wo_sh]
    mov [wo_csh], al
    call wo_sweep                   ; what the old clipboard alone held goes
    mov si, wr_s_copied
    call wr_say
    clc
    ret

; Cut: Copy, then Delete
wx_cut:
    call wx_copy
    jc .out
    mov si, wr_s_cut
    jmp wx_delsay
.out:
    ret

; -----------------------------------------------------------------------------
; Paste: the clipboard inserted at the cursor, or over the selection; the
; selection is the pasted range after. Into an EMPTY document (after New) the
; clipboard's format is adopted (SPEC.md 123.3).
; -----------------------------------------------------------------------------
wx_paste:
    cmp byte [wo_doc], 0
    jne .have
    call wx_adopt
    jnc .have
    mov si, ww_e15                  ; 'Not enough memory'
    jmp wr_say
.have:
    mov ax, [wo_cur]                ; a, b: the selection, or the cursor twice
    mov dx, [wo_cur+2]
    mov bx, ax
    mov cx, dx
    call we_hassel
    jc .ab
    mov ax, [wo_s]
    mov dx, [wo_s+2]
    mov bx, [wo_e]
    mov cx, [wo_e+2]
.ab:
    mov [wx_na], ax
    mov [wx_na+2], dx
    push bx
    push cx
    call wx_begin
    call wx_upto                    ; [0, a)
    mov si, wo_clip                 ; the clipboard
    mov cx, [wo_ncp]
.c:
    jcxz .rest
    mov ax, [si+WP_SRC]
    mov [wx_es], ax
    mov ax, [si+WP_FIRST]
    mov [wx_ef], ax
    mov ax, [si+WP_FIRST+2]
    mov [wx_ef+2], ax
    mov ax, [si+WP_CNT]
    mov [wx_ec], ax
    mov ax, [si+WP_CNT+2]
    mov [wx_ec+2], ax
    call wx_emit
    add si, WP_SIZE
    dec cx
    jmp short .c
.rest:
    pop dx
    pop ax
    call wx_rest                    ; [b, frames)
    mov ax, [wx_na]                 ; the pasted range: [a, a + its frames)
    mov dx, [wx_na+2]
    add ax, [wo_cfr]
    adc dx, [wo_cfr+2]
    mov [wx_nb], ax
    mov [wx_nb+2], dx
    mov si, wr_s_pasted
    jmp wx_commit

; wx_adopt - an empty document takes the clipboard's format and is named
; UNTITLED.WAV: its claims made, no frames yet. CF = 1 no memory (nothing
; held). clobbers AX, CX, DX, SI, DI
wx_adopt:
    mov ax, WO_IOKB
    call OSAPI_MEM_CLAIM
    jc .out
    mov [wo_ioseg], dx
    mov ax, WE_VKB
    call OSAPI_MEM_CLAIM
    jnc .vc
    mov dx, [wo_ioseg]
    call wo_free
    mov [wo_ioseg], dx
    stc
    ret
.vc:
    mov [we_vseg], dx
    mov ax, [wo_crate]
    mov [wo_rate], ax
    mov al, [wo_cbits]
    mov [wo_bits], al
    mov al, [wo_cch]
    mov [wo_ch], al
    mov cl, [wo_csh]
    mov [wo_sh], cl
    mov al, 1
    shl al, cl
    mov [wo_ba], al
    mov si, wr_s_untitled
    mov di, wo_name
    mov cx, 13
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    mov byte [wo_titled], 0
    mov byte [wv_z], 26             ; Fit, once there are frames (wx_relaid)
    mov byte [wo_doc], 1
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; wx_commit - the list in wx_tmp becomes the document, with the selection
;             [wx_na, wx_nb) (the cursor at its start, anchored there): the
;             history records it, the view follows, the document is DIRTY,
;             the status bar says SI. A list past WO_NPC is refused and
;             nothing changes. out CF = 1 refused. clobbers AX, BX, CX, DX,
;             SI, DI
; -----------------------------------------------------------------------------
wx_commit:
    cmp byte [wx_tov], 0
    je .fits
    mov si, wr_s_npc                ; 'Too many edits; save' (SPEC.md 123.3's
    jmp short .no                   ; sentence, in 22 characters)
.fits:
    push si
    call wx_urec0                   ; the state before, in the history
    pop si
    jnc .rec
    mov si, ww_e15                  ; 'Not enough memory'
.no:
    call wr_say
    stc
    ret
.rec:
    push si
    mov cx, [wx_tn]                 ; the new list
    mov [wo_npc], cx
    mov ax, WP_SIZE / 2
    mul cx
    mov cx, ax
    mov si, wx_tmp
    mov di, wo_pc
    push es
    push ds
    pop es
    cld
    rep movsw
    pop es
    call wx_frames
    mov ax, [wx_na]                 ; the selection on the result
    mov dx, [wx_na+2]
    mov [wo_s], ax
    mov [wo_s+2], dx
    mov [wo_anc], ax
    mov [wo_anc+2], dx
    mov [wo_cur], ax
    mov [wo_cur+2], dx
    mov ax, [wx_nb]
    mov [wo_e], ax
    mov ax, [wx_nb+2]
    mov [wo_e+2], ax
    call wx_urec1                   ; ...and the state after
    mov byte [wo_dirty], 1
    call wx_relaid
    call wo_sweep                   ; the oldest state may have held a source
    call wr_retitle
    pop si
    call wr_say
    clc
    ret

; wx_frames - [wo_frames] = the pieces' frames. keeps the rest
wx_frames:
    push ax
    push cx
    push dx
    push si
    mov si, wo_pc
    mov cx, [wo_npc]
    call wx_sum
    mov [wo_frames], ax
    mov [wo_frames+2], dx
    pop si
    pop dx
    pop cx
    pop ax
    ret

; Undo / Redo: the history's previous / next state, dirty, the view following
wx_undo:
    dec byte [wo_upos]
    mov si, wr_s_undone
    jmp short wx_load
wx_redo:
    inc byte [wo_upos]
    mov si, wr_s_redone
wx_load:
    push si
    mov al, [wo_upos]
    call wx_uload
    mov byte [wo_dirty], 1
    call wx_relaid
    call wr_retitle
    pop si
    jmp wr_say

; -----------------------------------------------------------------------------
; wx_relaid - the pieces changed: the envelope's block for exactness (the
;             largest any piece's source has), Fit for the new length, the
;             view clamped and its cache rebuilt FROM THE PIECES (no rescan:
;             a source's envelope is its own and never changes), every
;             element that shows the document owed, and the cursor in view.
;             clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wx_relaid:
    mov al, 4                       ; B >= 16 with nothing to say otherwise
    mov si, wo_pc
    mov cx, [wo_npc]
    jcxz .bs
.p:
    push ax
    mov ax, [si+WP_SRC]
    call wx_srec                    ; BX = its source
    pop ax
    cmp al, [bx+WS_BS]
    jae .n
    mov al, [bx+WS_BS]
.n:
    add si, WP_SIZE
    loop .p
.bs:
    mov [we_bs], al
    call we_zfit
    mov al, [wv_zfit]
    cmp [wv_z], al
    jbe .z
    mov [wv_z], al
.z:
    call we_clamp
    call wv_build
    call wo_moved
    call we_smoved
    call we_curcol                  ; the cursor out of view: centred on it
    jnc .out
    mov ax, [wo_cur]
    mov dx, [wo_cur+2]
    jmp we_centre
.out:
    ret

; wx_srec - AX = a source's index -> BX = its record. keeps the rest
wx_srec:
    push ax
    push dx
    mov bx, WS_SIZE
    mul bx
    add ax, wo_src
    mov bx, ax
    pop dx
    pop ax
    ret

; =============================================================================
; THE HISTORY (one claim, a ring of WX_NSLOT states)
; =============================================================================

; wx_urec0 - before an edit: the claim (made by the first), and the state the
; user has NOW at [wo_upos] - the whole of it when the history is empty, else
; only its cursor and selection (its pieces are already there) - and the redo
; side dropped. CF = 1 no memory. clobbers AX, DX, SI, DI
wx_urec0:
    cmp word [wo_useg], 0
    jne .have
    mov ax, WX_UKB
    call OSAPI_MEM_CLAIM            ; pinned: no relocation proc (SPEC.md 66)
    jc .out
    mov [wo_useg], dx
    mov byte [wo_un], 0
.have:
    cmp byte [wo_un], 0
    jne .hdr
    mov byte [wo_ubase], 0
    mov byte [wo_upos], 0
    mov byte [wo_un], 1
    xor al, al
    call wx_ustore
    clc
    ret
.hdr:
    mov al, [wo_upos]
    call wx_uhdr
    inc al
    mov [wo_un], al                 ; nothing to redo after an edit
    clc
.out:
    ret

; wx_urec1 - after it: the result appended at upos + 1, the oldest state
; falling off a full ring. clobbers AX, SI, DI
wx_urec1:
    cmp byte [wo_un], WX_NSLOT
    jb .room
    inc byte [wo_ubase]             ; the oldest goes
    cmp byte [wo_ubase], WX_NSLOT
    jb .b
    mov byte [wo_ubase], 0
.b:
    dec byte [wo_un]
    dec byte [wo_upos]
.room:
    mov al, [wo_upos]
    inc al
    mov [wo_upos], al
    inc al
    mov [wo_un], al
    dec al
    jmp wx_ustore

; wx_uaddr - AL = a state (0 = the oldest) -> DI = its offset in the claim.
; keeps the rest
wx_uaddr:
    push ax
    push dx
    add al, [wo_ubase]
    cmp al, WX_NSLOT
    jb .p
    sub al, WX_NSLOT
.p:
    xor ah, ah
    mov dx, WX_USLOT
    mul dx
    mov di, ax
    pop dx
    pop ax
    ret

; wx_ustore - AL = a state: the document's now (pieces, frames, cursor,
; selection, anchor). keeps AX; clobbers SI, DI
wx_ustore:
    push ax
    push cx
    push es
    call wx_uaddr
    mov es, [wo_useg]
    cld
    mov ax, [wo_npc]
    stosw
    mov si, wo_frames
    movsw
    movsw
    call wx_uhdr0                   ; cursor, s, e, anc
    mov cx, [wo_npc]
    mov ax, WP_SIZE / 2
    mul cx
    mov cx, ax
    mov si, wo_pc
    rep movsw
    pop es
    pop cx
    pop ax
    ret

; wx_uhdr - AL = a state: only its cursor and selection, the document's now.
; keeps AX; clobbers SI, DI
wx_uhdr:
    push es
    call wx_uaddr
    add di, 6
    mov es, [wo_useg]
    cld
    call wx_uhdr0
    pop es
    ret

; wx_uhdr0 - cursor, s, e and anc to ES:DI (on past them). clobbers SI
wx_uhdr0:
    mov si, wo_cur
    movsw
    movsw
    mov si, wo_s
    movsw
    movsw
    mov si, wo_e
    movsw
    movsw
    mov si, wo_anc
    movsw
    movsw
    ret

; wx_uload - AL = a state: it becomes the document's. clobbers AX, CX, SI, DI
wx_uload:
    push es
    call wx_uaddr
    mov es, [wo_useg]
    mov ax, [es:di]
    mov [wo_npc], ax
    mov ax, [es:di+2]
    mov [wo_frames], ax
    mov ax, [es:di+4]
    mov [wo_frames+2], ax
    mov ax, [es:di+6]
    mov [wo_cur], ax
    mov ax, [es:di+8]
    mov [wo_cur+2], ax
    mov ax, [es:di+10]
    mov [wo_s], ax
    mov ax, [es:di+12]
    mov [wo_s+2], ax
    mov ax, [es:di+14]
    mov [wo_e], ax
    mov ax, [es:di+16]
    mov [wo_e+2], ax
    mov ax, [es:di+18]
    mov [wo_anc], ax
    mov ax, [es:di+20]
    mov [wo_anc+2], ax
    mov ax, WP_SIZE / 2
    mul word [wo_npc]
    mov cx, ax
    lea si, [di+WX_UHDR]
    mov di, wo_pc
    mov ax, [wo_useg]
    push ds
    pop es
    push ds
    mov ds, ax
    cld
    rep movsw
    pop ds
    pop es
    ret

; wx_uclear - no history: its claim given back. keeps all but flags
wx_uclear:
    push dx
    mov dx, [wo_useg]
    call wo_free
    mov [wo_useg], dx
    mov byte [wo_un], 0
    mov byte [wo_upos], 0
    mov byte [wo_ubase], 0
    pop dx
    ret

; =============================================================================
; SOURCES: what references them
; =============================================================================

; -----------------------------------------------------------------------------
; wo_sweep - every source nothing names any more - not the document, not a
;            state in the history, not the clipboard - given back, its
;            envelope claim with it (and an unused record never keeps one).
;            keeps every register
; -----------------------------------------------------------------------------
wo_sweep:
    push ax
    push bx
    push dx
    push si
    xor bx, bx
    mov si, wo_src
.s:
    cmp byte [si+WS_USED], 0
    je .free
    call wx_refd
    jnc .next
.free:
    mov dx, [si+WS_ENV]
    call wo_free
    mov [si+WS_ENV], dx
    mov byte [si+WS_USED], 0
    cmp si, [wo_csrc]               ; the I/O claim's run was its
    jne .next
    mov byte [wo_cv], 0
.next:
    add si, WS_SIZE
    inc bx
    cmp bx, WO_NSRC
    jb .s
    pop si
    pop dx
    pop bx
    pop ax
    ret

; wx_refd - CF = 0 when source BX is named by the document, the clipboard or
; a state in the history. keeps every register
wx_refd:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, wo_pc
    mov cx, [wo_npc]
    call wx_has
    jnc .yes
    mov si, wo_clip
    mov cx, [wo_ncp]
    call wx_has
    jnc .yes
    cmp word [wo_useg], 0
    je .no
    mov es, [wo_useg]
    xor al, al
.u:
    cmp al, [wo_un]
    jae .no
    call wx_uaddr
    mov cx, [es:di]
    lea si, [di+WX_UHDR]
    call wx_has
    jnc .yes
    inc al
    jmp short .u
.no:
    stc
.yes:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; wx_docref - CF = 0 when source BX is named by the DOCUMENT. keeps all
wx_docref:
    push cx
    push si
    push es
    push ds
    pop es
    mov si, wo_pc
    mov cx, [wo_npc]
    call wx_has
    pop es
    pop si
    pop cx
    ret

; wx_has - ES:SI = a list, CX = its pieces: CF = 0 when one names source BX.
; clobbers CX, SI
wx_has:
    jcxz .no
.l:
    cmp [es:si+WP_SRC], bx
    je .yes
    add si, WP_SIZE
    loop .l
.no:
    stc
    ret
.yes:
    clc
    ret

; wx_clipdrop - the clipboard without source BX's pieces (a save replaced
; that file: SPEC.md 123.10). keeps every register
wx_clipdrop:
    push ax
    push cx
    push dx
    push si
    push di
    mov si, wo_clip
    mov di, si
    mov cx, [wo_ncp]
    xor dx, dx
    jcxz .done
.p:
    cmp [si+WP_SRC], bx
    je .skip
    mov ax, [si]                    ; kept: moved down over a dropped one
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+4]
    mov [di+4], ax
    mov ax, [si+6]
    mov [di+6], ax
    mov ax, [si+8]
    mov [di+8], ax
    add di, WP_SIZE
    inc dx
.skip:
    add si, WP_SIZE
    loop .p
.done:
    mov [wo_ncp], dx
    mov cx, dx
    mov si, wo_clip
    call wx_sum
    mov [wo_cfr], ax
    mov [wo_cfr+2], dx
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; wx_clipsrcs - AX = how many sources the clipboard names (an Open needs a
; record free: SPEC.md 123.3). keeps the rest
wx_clipsrcs:
    push bx
    push cx
    push si
    push es
    push ds
    pop es
    xor ax, ax
    xor bx, bx
.s:
    mov si, wo_clip
    mov cx, [wo_ncp]
    call wx_has
    jc .n
    inc ax
.n:
    inc bx
    cmp bx, WO_NSRC
    jb .s
    pop es
    pop si
    pop cx
    pop bx
    ret

; wx_clipfmt - CF = 0 when the clipboard's format is the document's (or there
; is no document: a paste adopts it). keeps every register
wx_clipfmt:
    cmp byte [wo_doc], 0
    je .yes
    push ax
    mov ax, [wo_crate]
    cmp ax, [wo_rate]
    jne .no
    mov ax, word [wo_cbits]         ; bits and channels, one compare
    cmp al, [wo_bits]
    jne .no
    cmp ah, [wo_ch]
    jne .no
    pop ax
.yes:
    clc
    ret
.no:
    pop ax
    stc
    ret

; wx_clipsay - Paste's refusal, naming the clipboard's format in 22
; characters: 'Clip: 22050 16b stereo' (tools/wrsim.py clip_text).
; clobbers AX, DX, SI, DI
wx_clipsay:
    mov di, wr_tbuf2
    mov si, wr_s_clip
    call wr_put
    mov ax, [wo_crate]
    call dec_u16
    mov byte [di], ' '
    inc di
    mov al, [wo_cbits]
    call wr_num
    mov si, wr_s_bmono
    cmp byte [wo_cch], 2
    jne .m
    mov si, wr_s_bster
.m:
    call wr_put
    mov byte [di], 0
    mov si, wr_tbuf2
    jmp wr_say
