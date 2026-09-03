; =============================================================================
; os8088 - apps/audio/aplist.inc
;
; The Audio Player's playlist store (SPEC.md 86.10). At most AP_MAXTRK
; entries; each entry is AP_ENTSZ (16) bytes:
;
;   [0..12]  8.3 name, NUL-terminated (<= 12 chars)
;   [13]     volume index
;   [14..15] directory first cluster (0 = root)
;
; No full paths (SPEC.md 86.10): a track is reached by
; OSAPI_FILE_GOTO_Q(dir, vol) then a name lookup - the ARG_FILE / fdlg model.
; Shuffle keeps a separate order array apl_ord[] of entry indices; Repeat is a
; flag consulted only at the end of the list. Prefix apl_.
; =============================================================================

    APB apl_tvol                   ; scratch for apl_add
    APW apl_tdir

aplist_init:
    mov byte [apl_n], 0
    mov byte [apl_cur], 0
    mov byte [apl_opos], 0
    ret

apl_clear:
    mov byte [apl_n], 0
    mov byte [apl_cur], 0
    mov byte [apl_opos], 0
    ret

; apl_count -> AX
apl_count:
    xor ah, ah
    mov al, [apl_n]
    ret

; apl_entptr: AX = index -> BX = &entry ; preserves everything else
apl_entptr:
    push cx
    mov bx, ax
    mov cl, 4
    shl bx, cl
    add bx, apl_ent
    pop cx
    ret

; =============================================================================
; apl_add - append a track
; in:  SI -> NUL name (<= 12) in this segment, DX = dir cluster, BL = volume
; out: CF = 1 list full; else CF = 0
; clobbers: nothing (all pushed)
; =============================================================================
apl_add:
    push ax
    push bx
    push cx
    push si
    push di
    mov al, [apl_n]
    cmp al, AP_MAXTRK
    jae .full
    mov [apl_tvol], bl
    mov [apl_tdir], dx
    xor ah, ah
    call apl_entptr                ; BX = &entry
    mov di, bx
    xor cx, cx
.cn:
    cmp cx, 12
    jae .term
    mov al, [si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    inc cx
    jmp .cn
.term:
    mov byte [di], 0
.named:
    xor ah, ah
    mov al, [apl_n]
    call apl_entptr                ; BX = &entry again (BL was clobbered)
    mov al, [apl_tvol]
    mov [bx+13], al
    mov ax, [apl_tdir]
    mov [bx+14], ax
    inc byte [apl_n]
    cmp byte [ap_shuffle], 0
    je .ok
    call apl_shuffle_build
.ok:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.full:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; =============================================================================
; apl_find - is this exact track already in the playlist?
; in:  SI -> NUL name (this segment), DX = dir cluster, BL = volume
; out: CF = 0 and AL = its index; CF = 1 not present
; clobbers: AX (BX/CX/DX/SI/DI preserved)
; =============================================================================
apl_find:
    push bx
    push cx
    push dx
    push si
    push di
    mov [apl_tvol], bl
    mov [apl_tdir], dx
    xor cx, cx
.e:
    cmp cl, [apl_n]
    jae .none
    mov al, cl
    xor ah, ah
    call apl_entptr               ; BX = &entry
    mov ax, [bx+14]
    cmp ax, [apl_tdir]
    jne .next
    mov al, [bx+13]
    cmp al, [apl_tvol]
    jne .next
    push si
    mov di, bx                    ; entry name
    call ap_nameeq                ; ZF = 1 equal (SI = query name)
    pop si
    je .hit
.next:
    inc cx
    jmp .e
.hit:
    mov al, cl
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.none:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; apl_curptr -> BX = &current entry ; CF = 1 if the list is empty
apl_curptr:
    cmp byte [apl_n], 0
    je .empty
    push ax
    mov al, [apl_cur]
    xor ah, ah
    call apl_entptr
    pop ax
    clc
    ret
.empty:
    stc
    ret

; apl_cur_name -> SI = &name of the current entry ; CF = 1 empty
apl_cur_name:
    call apl_curptr
    jc .e
    mov si, bx
    clc
    ret
.e:
    stc
    ret

; =============================================================================
; apl_goto_cur - move this instance into the current entry's directory
; out: CF = 1 empty or the goto failed
;
; OSAPI_FILE_GOTO_QM: the by-name file ops that follow in ap_open_track
; (ap_filesize, apw_parse, every refill) resolve in the INSTANCE's own current
; folder, and GOTO_Q does not move it (SPEC.md 86.11.1) - QM is the quiet stand
; that DOES. Not OSAPI_FILE_GOTO either: that is the listing remount (directory
; scan, sort, an icon read per file - seconds on a 4.77 MHz XT, SPEC.md 19.2.2)
; and it would be paid under the gfx lock at every track boundary, the
; automatic one between two songs included. A track handed over from another
; folder, or a playlist that spans folders, still gets the real move.
; =============================================================================
apl_goto_cur:
    call apl_curptr
    jc .e
    push ax
    push dx
    mov dx, [bx+14]
    mov bl, [bx+13]
    call OSAPI_FILE_GOTO_QM       ; DX = cluster, BL = volume; quiet, and moves us
    pop dx
    pop ax
    ret
.e:
    stc
    ret

; =============================================================================
; apl_advance - step the current-track pointer
; in:  AL = 1 (next) or 0xFF (prev)
; out: CF = 1 => the list ended and Repeat is off; the caller should stop.
;      CF = 0 => apl_cur now names the track to play.
; clobbers: nothing (all pushed)
; =============================================================================
apl_advance:
    push ax
    push bx
    push cx
    push dx
    cmp byte [apl_n], 0
    je .stop
    cmp byte [ap_shuffle], 0
    jne .shuf

    mov cl, [apl_cur]
    mov ch, [apl_n]
    cmp al, 1
    je .lnext
    or cl, cl
    jnz .lpd
    mov cl, ch
.lpd:
    dec cl
    mov [apl_cur], cl
    jmp .go
.lnext:
    inc cl
    cmp cl, ch
    jb .lset
    cmp byte [ap_repeat], 0
    je .stop
    xor cl, cl
.lset:
    mov [apl_cur], cl
    jmp .go

.shuf:
    mov cl, [apl_opos]
    mov ch, [apl_n]
    cmp al, 1
    je .snext
    or cl, cl
    jnz .spd
    mov cl, ch
.spd:
    dec cl
    jmp .sset
.snext:
    inc cl
    cmp cl, ch
    jb .sset
    cmp byte [ap_repeat], 0
    je .stop
    call apl_shuffle_build
    xor cl, cl
.sset:
    mov [apl_opos], cl
    xor ch, ch
    mov bx, apl_ord
    add bx, cx
    mov al, [bx]
    mov [apl_cur], al
    jmp .go

.go:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.stop:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; apl_set_shuffle - AL = 0 off / non-zero on
apl_set_shuffle:
    mov [ap_shuffle], al
    or al, al
    jz .ret
    call apl_shuffle_build
.ret:
    ret

; =============================================================================
; apl_shuffle_build - random permutation of the entry indices into apl_ord[],
;                     Fisher-Yates via OSAPI_RAND; apl_opos <- where the
;                     current track landed.
; clobbers: nothing (all pushed)
; =============================================================================
apl_shuffle_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, [apl_n]
    or cl, cl
    jz .out
    xor ch, ch                     ; CX = n

    xor bx, bx                     ; identity fill
.fill:
    mov di, apl_ord
    add di, bx
    mov al, bl
    mov [di], al
    inc bx
    cmp bx, cx
    jb .fill

    mov bx, cx
    dec bx                         ; i = n-1
.fy:
    or bx, bx
    jz .locate
    call OSAPI_RAND                ; AX = random word
    xor dx, dx
    mov si, bx
    inc si                         ; divisor = i+1
    div si                        ; DX = AX mod (i+1)
    mov di, apl_ord
    add di, bx
    mov si, apl_ord
    add si, dx
    mov al, [di]
    mov ah, [si]
    mov [di], ah
    mov [si], al
    dec bx
    jmp .fy

.locate:
    mov al, [apl_cur]
    xor bx, bx
.loc:
    mov di, apl_ord
    add di, bx
    cmp [di], al
    je .set
    inc bx
    cmp bl, [apl_n]
    jb .loc
    xor bx, bx
.set:
    mov [apl_opos], bl
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
