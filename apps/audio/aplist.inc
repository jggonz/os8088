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
; OSAPI_FILE_GOTO_QM(dir, vol) then a name lookup - the ARG_FILE / fdlg model.
; Shuffle keeps a separate order array apl_ord[] of entry indices; Repeat is a
; flag consulted only at the end of the list. Prefix apl_.
;
; (No init: the loader zeroes a package's bss, which is an empty list.)
; =============================================================================

; apl_clear - empty the list
apl_clear:
    mov word [apl_n], 0            ; apl_n and apl_cur (adjacent, audio.asm)
    mov byte [apl_opos], 0
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
    cmc
    jc .out                        ; CF = 1: full
    mov ch, bl                     ; the volume, across apl_entptr
    xor ah, ah
    call apl_entptr                ; BX = &entry
    mov di, bx
    mov cl, 12
.cn:
    mov al, [si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    dec cl
    jnz .cn
    mov byte [di], 0               ; twelve characters: the NUL at [12]
.named:
    mov [bx+13], ch
    mov [bx+14], dx
    inc byte [apl_n]
    cmp byte [ap_shuffle], 0
    je .ok
    call apl_shuffle_build
.ok:
    clc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
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
    push di
    mov cl, bl                     ; the volume, across apl_entptr
    xor ax, ax                     ; AX = the index
.e:
    cmp al, [apl_n]
    jae .none
    call apl_entptr               ; BX = &entry
    cmp [bx+14], dx
    jne .next
    cmp [bx+13], cl
    jne .next
    mov di, bx                    ; entry name
    call ap_nameeq                ; ZF = 1 equal (SI = query name), CF = 0
    je .hit
.next:
    inc ax
    jmp .e
.none:
    stc
.hit:
    pop di
    pop cx
    pop bx
    ret

; apl_curptr -> BX = &current entry ; CF = 1 if the list is empty
apl_curptr:
    cmp byte [apl_n], 1            ; CF = 1 iff there is none
    jc .r
    push ax
    mov al, [apl_cur]
    cbw                            ; (< AP_MAXTRK)
    call apl_entptr
    pop ax
    clc
.r:
    ret

; apl_cur_name -> SI = &name of the current entry ; CF = 1 empty (SI spent)
apl_cur_name:
    call apl_curptr
    mov si, bx
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
.e:
    ret

; =============================================================================
; apl_advance - step the current-track pointer
; in:  AL = 1 (next) or 0xFF (prev)
; out: CF = 1 => the list ended and Repeat is off; the caller should stop.
;      CF = 0 => apl_cur now names the track to play.
; clobbers: nothing (all pushed)
;
; The cursor stepped is apl_cur itself, or under Shuffle apl_opos, the
; position in apl_ord[] - whose entry then becomes apl_cur.
; =============================================================================
apl_advance:
    push ax
    push bx
    push cx
    mov ch, [apl_n]
    or ch, ch
    jz .stop
    mov bx, apl_cur
    cmp byte [ap_shuffle], 0
    je .c
    mov bx, apl_opos
.c:
    mov cl, [bx]
    cmp al, 1
    je .next
    or cl, cl                      ; Previous always wraps
    jnz .pd
    mov cl, ch
.pd:
    dec cl
    jmp short .set
.next:
    inc cl
    cmp cl, ch
    jb .set
    cmp byte [ap_repeat], 0
    je .stop                       ; the end, and Repeat is off
    xor cl, cl
    cmp bx, apl_cur
    je .set
    call apl_shuffle_build         ; a Shuffle wrap: a fresh order
.set:
    mov [bx], cl
    cmp bx, apl_cur
    je .go
    mov bl, cl
    xor bh, bh
    mov al, [apl_ord + bx]
    mov [apl_cur], al
.go:
    clc
    jmp short .out
.stop:
    stc
.out:
    pop cx
    pop bx
    pop ax
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
    mov [apl_ord + bx], bl
    inc bx
    cmp bx, cx
    jb .fill

    dec bx                         ; i = n-1
.fy:
    or bx, bx
    jz .locate
    call OSAPI_RAND                ; AX = random word
    xor dx, dx
    mov si, bx
    inc si                         ; divisor = i+1
    div si                        ; DX = AX mod (i+1)
    mov si, dx
    mov al, [apl_ord + bx]
    xchg al, [apl_ord + si]
    mov [apl_ord + bx], al
    dec bx
    jmp .fy

.locate:
    mov al, [apl_cur]              ; BX = 0 here
.loc:
    cmp [apl_ord + bx], al
    je .set
    inc bx
    cmp bl, [apl_n]
    jb .loc
    xor bx, bx
.set:
    mov [apl_opos], bl
.out:
    jmp ap_ret6
