; =============================================================================
; os8088 - drivers/sound/radval.inc  (RADPLAY.DRV)
;
; The validator, SPEC.md 96.4.4 to the byte: the procedure V1..V8 and TRACK in
; exactly the published order, answering the same (RADE_CORRUPT, RADC_*,
; offset) triple as tools/radsim.py's `validate`. tests/radopl3.py and
; tests/radopl2.py feed tests/unit/radrows.py's whole hostile-file table to
; this code on the machine and compare every answer with radsim's.
;
; It runs against the COPY in the claim (RAD_TUNE), never the caller's buffer,
; so what it proved is what the interrupt-time replayer reads.
;
; Registers: SI is the cursor p as a CLAIM offset, BP the file's end (RAD_TUNE
; + n) as a plain register - never an addressing base, since SS is not this
; segment at verb time. A failure unwinds to [rv_sp] from any depth.
; =============================================================================

; -----------------------------------------------------------------------------
; rv_valid - verb 4 step 6
; in:  DS = the claim, CX = the file's length n (17..RAD_MAXLEN, steps 1-5
;      passed by the resident: signature, version, length, OPL3)
; out: CF = 0 AL = version byte; CF = 1 AL = RADE_CORRUPT, AH = RADC_*,
;      CX = the file offset of the failure
; clobbers: AH, BX, DX, SI, DI (flags)
; -----------------------------------------------------------------------------
rv_valid:
    ; STKBALANCE-OK: a refusal unwinds to [rv_sp] from any call depth -
    ; rv_fail reloads SP from it and pops the one register rv_valid banked -
    ; so a failure path's static depth is not the depth it returns at
    push bp
    mov [rv_sp], sp
    cld
    mov bp, RAD_TUNE
    add bp, cx                  ; BP = end
    mov al, [RAD_TUNE+0x10]
    mov [rv_ver], al
    ; --- V1 ------------------------------------------------------------------
    mov si, RAD_TUNE+0x11
    mov di, si
    mov ax, 1
    call rv_need
    mov dl, [si]                ; flags
    inc si
    ; --- V2 ------------------------------------------------------------------
    cmp byte [rv_ver], 0x21
    jne .v1flags
    test dl, 0x80
    jnz .flagfail
    test dl, 0x20
    jz .desc
    mov di, si                  ; need(2, 12h)
    mov ax, 2
    call rv_need
    mov ax, [si]
    cmp ax, 46
    jb .bpmfail
    cmp ax, 300
    ja .bpmfail
    add si, 2
    jmp short .desc
.bpmfail:
    mov ah, RADC_BPM
    jmp rv_fail
.flagfail:
    mov di, RAD_TUNE+0x11
    mov ah, RADC_FLAGS
    jmp rv_fail
.v1flags:
    test dl, 0x20
    jnz .flagfail
    test dl, 0x80
    jz .insts
    ; --- V3 ------------------------------------------------------------------
.desc:
    mov di, si                  ; d
.dl:
    mov ax, 1
    call rv_need
    lodsb
    or al, al
    jnz .dl
    ; --- V4 ------------------------------------------------------------------
.insts:
    xor dh, dh                  ; last
.il:
    mov di, si                  ; i
    mov ax, 1
    call rv_need
    lodsb                       ; num
    or al, al
    jz .order
    cmp byte [rv_ver], 0x21
    jne .i1
    cmp al, 127
    ja .instfail
    cmp al, dh
    jbe .instfail
    mov dh, al
    mov ax, 1                   ; need(1, i)
    call rv_need
    lodsb                       ; len
    xor ah, ah
    inc ax
    call rv_need                ; need(len + 1, i)
    dec ax
    add si, ax
    mov al, [si]                ; alg
    push ax
    and al, 7
    cmp al, 7
    jne .fm
    mov ax, 7
    call rv_need
    test byte [si+2], 0xF0
    jz .midiok
    mov ah, RADC_MIDI
    jmp rv_fail
.midiok:
    add si, 7
    jmp short .iriff
.fm:
    mov ax, 24
    call rv_need
    add si, 24
.iriff:
    pop ax
    test al, 0x80
    jz .il
    push dx                     ; DH is `last`, and TRACK spends it
    mov dl, 1                   ; TRACK(instrument riff, rec = i)
    call rv_track
    pop dx
    jmp .il
.i1:
    cmp al, 31
    ja .instfail
    cmp al, dh
    jbe .instfail
    mov dh, al
    mov ax, 11
    call rv_need
    add si, 11
    jmp .il
.instfail:
    mov ah, RADC_INST
    jmp rv_fail
    ; --- V5 ------------------------------------------------------------------
.order:
    mov di, si                  ; o
    mov ax, 1
    call rv_need
    lodsb                       ; len
    or al, al
    jz .ordlenfail
    cmp al, 128
    ja .ordlenfail
    xor ah, ah
    call rv_need                ; need(len, o)
    mov cx, ax
    xor bx, bx                  ; k
.ol:
    mov al, [si+bx]
    test al, 0x80
    jz .opat
    and al, 0x7F                ; t
    cmp al, cl
    jae .jumpfail
    xor ah, ah
    mov di, si
    add di, ax
    test byte [di], 0x80
    jnz .jumpfail
    cmp byte [rv_ver], 0x21
    je .onext
    or bx, bx
    jz .jumpfail
    jmp short .onext
.opat:
    cmp byte [rv_ver], 0x21
    je .o2
    cmp al, 32
    jae .orderfail
    jmp short .onext
.o2:
    cmp al, 100
    jae .orderfail
.onext:
    inc bx
    cmp bx, cx
    jb .ol
    add si, cx
    cmp byte [rv_ver], 0x21
    jne rv_v8
    ; --- V6 ------------------------------------------------------------------
.pl:
    mov di, si                  ; r
    mov ax, 1
    call rv_need
    lodsb
    cmp al, 0xFF
    je .riffs
    cmp al, 100
    jae .patnumfail
    call rv_seenset
    jc .patnumfail
    mov dl, 0                   ; TRACK(pattern, rec = r)
    call rv_track
    jmp .pl
.ordlenfail:
    mov ah, RADC_ORDLEN
    jmp rv_fail
.jumpfail:
    lea di, [si+bx]
    mov ah, RADC_JUMP
    jmp rv_fail
.orderfail:
    lea di, [si+bx]
    mov ah, RADC_ORDER
    jmp rv_fail
.patnumfail:
    mov ah, RADC_PATNUM
    jmp rv_fail
    ; --- V7 ------------------------------------------------------------------
.riffs:
    push di                     ; a fresh seen-set for the riff ids
    push es
    push ds
    pop es
    mov di, rv_seen
    mov cx, 32
    xor al, al
    rep stosb
    pop es
    pop di
.rl:
    mov di, si                  ; r
    mov ax, 1
    call rv_need
    lodsb                       ; id
    cmp al, 0xFF
    je .end2
    mov ah, al
    and ah, 0x0F
    jz .riffidfail
    cmp ah, 9
    ja .riffidfail
    cmp al, 0xA0                ; id >> 4 > 9
    jae .riffidfail
    call rv_seenset
    jc .riffidfail
    mov dl, 2                   ; TRACK(channel riff, rec = r)
    call rv_track
    jmp .rl
.riffidfail:
    mov ah, RADC_RIFFID
    jmp rv_fail
.end2:
    cmp si, bp
    je rv_ok
    mov di, si
    mov ah, RADC_EXTRA
    jmp rv_fail

; --- V8: the 1.0 pattern table and the patterns it names ---------------------
rv_v8:
    mov di, si                  ; t
    mov ax, 64
    call rv_need
    mov dx, si                  ; DX = t
    xor bx, bx                  ; 2k
.pk:
    mov di, dx
    add di, bx                  ; q
    mov ax, [di]                ; off, a FILE offset
    or ax, ax
    jz .pnext
    mov cx, dx
    sub cx, RAD_TUNE - 64       ; t + 64, as a file offset
    cmp ax, cx
    jb .patoff
    mov cx, bp
    sub cx, RAD_TUNE            ; n
    cmp ax, cx
    jae .patoff
    mov si, ax
    add si, RAD_TUNE            ; p
    push bx
    push dx
    xor cl, cl                  ; the smallest line number still allowed
.lines:
    mov di, si                  ; lp
    cmp si, bp
    jae .ltrunc
    lodsb                       ; lb
    mov ah, al
    and ah, 0x7F
    cmp ah, 64
    jae .linefail
    cmp ah, cl
    jb .linefail
    inc ah
    mov cl, ah
    push ax                     ; lb, for the last-line test
    xor ch, ch                  ; the smallest channel still allowed
.ents:
    mov di, si                  ; c
    mov ax, 3
    call rv_need
    mov al, [si]
    and al, 0x7F
    cmp al, 9
    jae .chanfail
    cmp al, ch
    jb .chanfail
    inc al
    mov ch, al
    add si, 3
    mov al, [di+1]
    and al, 0x0F
    cmp al, 13
    je .notefail
    cmp al, 14
    je .notefail
    test byte [di+2], 0x0F
    jz .noparam
    push di
    mov di, si                  ; q2
    mov ax, 1
    call rv_need
    cmp byte [si], 99
    ja .efffail
    inc si
    pop di
.noparam:
    test byte [di], 0x80
    jz .ents
    pop ax
    test al, 0x80
    jz .lines
    pop dx
    pop bx
.pnext:
    add bx, 2
    cmp bx, 64
    jb .pk
    jmp rv_ok
.patoff:
    mov ah, RADC_PATOFF
    jmp rv_fail
.ltrunc:
    mov ah, RADC_TRUNC
    jmp rv_fail
.linefail:
    mov ah, RADC_LINE
    jmp rv_fail
.chanfail:
    mov ah, RADC_CHAN
    jmp rv_fail
.notefail:
    inc di                      ; the note byte, c + 1
    mov ah, RADC_NOTE
    jmp rv_fail
.efffail:
    mov ah, RADC_EFFECT
    jmp rv_fail

rv_ok:
    ; STKBALANCE-OK: a refusal unwinds to [rv_sp] from any call depth -
    ; rv_fail reloads SP from it and pops the one register rv_valid banked -
    ; so a failure path's static depth is not the depth it returns at
    mov sp, [rv_sp]
    pop bp
    mov al, [rv_ver]
    clc
    ret

; -----------------------------------------------------------------------------
; rv_fail - the first failure is the answer
; in:  AH = RADC_*, DI = the claim offset it is reported at
; -----------------------------------------------------------------------------
rv_fail:
    ; STKBALANCE-OK: a refusal unwinds to [rv_sp] from any call depth -
    ; rv_fail reloads SP from it and pops the one register rv_valid banked -
    ; so a failure path's static depth is not the depth it returns at
    mov sp, [rv_sp]
    pop bp
    mov cx, di
    sub cx, RAD_TUNE
    mov al, RADE_CORRUPT
    stc
    ret

; -----------------------------------------------------------------------------
; rv_need - need(k, at): p + k > n fails RADC_TRUNC at `at`
; in:  AX = k, SI = p, DI = at, BP = end
; -----------------------------------------------------------------------------
rv_need:
    ; STKBALANCE-OK: a refusal unwinds to [rv_sp] from any call depth -
    ; rv_fail reloads SP from it and pops the one register rv_valid banked -
    ; so a failure path's static depth is not the depth it returns at
    push ax
    add ax, si
    cmp ax, bp
    pop ax
    ja .trunc
    ret
.trunc:
    mov ah, RADC_TRUNC
    jmp rv_fail

; -----------------------------------------------------------------------------
; rv_seenset - test and set bit AL of rv_seen
; out: CF = 1 it was already set
; -----------------------------------------------------------------------------
rv_seenset:
    push ax
    push bx
    push cx
    mov bl, al
    xor bh, bh
    mov cl, 3
    shr bx, cl                  ; the byte
    mov cl, al
    and cl, 7
    mov al, 1
    shl al, cl                  ; the bit
    test [rv_seen+bx], al
    jnz .yes
    or [rv_seen+bx], al
    pop cx
    pop bx
    pop ax
    clc
    ret
.yes:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; rv_track - TRACK(kind, rec)
; in:  DL = kind (0 pattern, 1 instrument riff, 2 channel riff), DI = rec,
;      SI = p at the size word
; out: SI past the track
; clobbers: AX, BX, CX, DH, DI
; -----------------------------------------------------------------------------
rv_track:
    ; STKBALANCE-OK: a refusal unwinds to [rv_sp] from any call depth -
    ; rv_fail reloads SP from it and pops the one register rv_valid banked -
    ; so a failure path's static depth is not the depth it returns at
    mov ax, 2
    call rv_need                ; need(2, rec)
    lodsw                       ; size
    mov cx, bp
    sub cx, si                  ; what is left of the file
    cmp ax, cx
    jbe .fits
    mov ah, RADC_TRUNC
    jmp rv_fail
.fits:
    mov bx, si
    add bx, ax                  ; BX = e
    xor cl, cl                  ; the smallest line still allowed
.lines:
    mov di, si                  ; lp
    cmp si, bx
    jae .ptrunc
    lodsb                       ; lb
    mov ah, al
    and ah, 0x7F
    cmp ah, 64
    jae .linefail
    cmp ah, cl
    jb .linefail
    inc ah
    mov cl, ah
    push ax                     ; lb
    xor ch, ch                  ; the smallest channel still allowed
.ents:
    mov di, si                  ; c
    cmp si, bx
    jae .ptrunc
    lodsb
    mov dh, al                  ; x
    cmp dl, 2
    jne .pchan
    test dh, 0x80
    jz .chanfail
    jmp short .fields
.pchan:
    mov ah, dh
    and ah, 0x0F
    cmp ah, 9
    jae .chanfail
    cmp ah, ch
    jb .chanfail
    inc ah
    mov ch, ah
.fields:
    test dh, 0x40
    jz .noinst0
    mov di, si                  ; q
    cmp si, bx
    jae .ptrunc
    mov al, [si]
    and al, 0x0F
    jz .notefail
    cmp al, 13
    je .notefail
    cmp al, 14
    je .notefail
    inc si
.noinst0:
    test dh, 0x20
    jz .noeff0
    mov di, si
    cmp si, bx
    jae .ptrunc
    mov al, [si]
    or al, al
    jz .insfail
    cmp al, 128
    jae .insfail
    inc si
.noeff0:
    test dh, 0x10
    jz .endent
    mov di, si
    mov ax, si
    add ax, 2
    cmp ax, bx
    ja .ptrunc
    cmp byte [si], 31
    ja .efffail
    cmp byte [si+1], 99
    ja .efffail
    add si, 2
.endent:
    cmp dl, 2
    je .endline
    test dh, 0x80
    jz .ents
.endline:
    pop ax
    test al, 0x80
    jz .lines
    cmp si, bx
    je .done
    mov di, si
    mov ah, RADC_EXTRA
    jmp rv_fail
.done:
    ret
.ptrunc:
    mov ah, RADC_PTRUNC
    jmp rv_fail
.linefail:
    mov ah, RADC_LINE
    jmp rv_fail
.chanfail:
    mov ah, RADC_CHAN
    jmp rv_fail
.notefail:
    mov ah, RADC_NOTE
    jmp rv_fail
.insfail:
    mov ah, RADC_INSNUM
    jmp rv_fail
.efffail:
    mov ah, RADC_EFFECT
    jmp rv_fail
