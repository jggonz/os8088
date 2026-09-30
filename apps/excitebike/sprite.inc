; Sprite loader (SPEC.md 102.1): the compiler stores each pose ONCE as four 1-bit
; masks (opaque, ink 1, 2, 3; 24 x 24); this expands them at load time into
; the memory phases the back end draws, in one heap claim.
;
;   VGA: x mod 8 in {0,2,4,6}.  A blob is four layers (0 = the outline, the
;        opaque pixels no ink layer claims; 1..3 the inks), each a word count
;        then (word offset from the box's first byte, byte mask) records for the
;        non-empty bytes, in row order.  The colour of a layer is NOT stored:
;        the blitter sets it (the rider's is a per-bike variable).
;   CGA: x mod 4 in {0,2}; see the CGA half below.
;
; The claim starts with a table of (pose * phases + phase) word offsets.
; Cost: ~2.2M clocks (0.46 s) once, inside the loading screen.

XS_CLAIM_V      equ 44                  ; KB: the plan's VGA budget
XS_CLAIM_C      equ 17                  ; KB: the plan's CGA budget
XS_CLAIM_CC     equ 28                  ; KB: ...and with the compiled hot poses after the blobs (SPEC.md 102.7.2)

; -----------------------------------------------------------------------------
; xb_spr_load - CF=1 if the claim was refused or the expansion would not fit
; -----------------------------------------------------------------------------
xb_spr_load:
    SAVE
    mov byte [xb_nspr], 0
    mov ax, XS_CLAIM_V
    cmp byte [xb_cga], 0
    je .claim
    mov ax, XS_CLAIM_CC             ; the compiled poses ride in the claim after the blobs; a machine
    mov [xb_sprkb], ax              ; without the room for them plays on the interpreted draw
    call OSAPI_MEM_CLAIM
xs_claim1 equ $                     ; (tests/excitebike_video.py --smallclaim refuses the big claim here)
    jnc .got
    mov ax, XS_CLAIM_C
.claim:
    mov [xb_sprkb], ax
    call OSAPI_MEM_CLAIM
    jc .nomem
.got:
    mov [xb_sprseg], dx
    mov es, dx
    cmp byte [xb_cga], 0
    jne .cga
    call xs_load_vga
    jc .done
    call xs_compile_v               ; the rider's own poses as code (SPEC.md 102.7.2); no room = interpreted
    clc
    jmp .done
.cga:
    call xs_load_cga
    jc .done
    cmp word [xb_sprkb], XS_CLAIM_CC
    je .compile
    clc                             ; (the small claim: nothing compiled, xs_ctab stays empty; the
    jmp .done                       ; cmp's own borrow is NOT an error - it took this path to a
                                    ; false 'load failed' on every machine that refused the big claim)
.compile:
    call xs_compile
    clc
.done:
    jc .free
    mov byte [xb_nspr], EXB_NPOSES
    RESTORE
    clc
    ret
.free:
    call xb_spr_free
    jmp .fail
.nomem:
    mov byte [xb_nomem], 1
.fail:
    RESTORE
    stc
    ret

xb_spr_free:
    push dx
    mov dx, [xb_sprseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [xb_sprseg], 0
    mov byte [xb_nspr], 0
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; VGA
; -----------------------------------------------------------------------------
xs_load_vga:
    push es
    push ds
    pop es
    mov di, xs_ctab                 ; no pose is compiled until xs_compile_v says so
    mov cx, EXB_NPOSES * XB_PHASES_V
    xor ax, ax
    rep stosw
    pop es
    xor ax, ax
    mov cx, EXB_NPOSES * XB_PHASES_V
    mov di, cx
    shl di, 1                       ; blobs start after the table
    mov word [xs_p], 0
.pose:
    mov word [xs_k], 0
.phase:
    cmp di, XS_CLAIM_V * 1024 - 2048
    ja .fail                        ; the largest blob is under 2 KB
    mov ax, [xs_p]
    shl ax, 1
    shl ax, 1
    add ax, [xs_k]
    shl ax, 1
    mov bx, ax
    mov [es:bx], di                 ; the table entry
    mov ax, [xs_k]
    shl ax, 1
    mov [xs_ph], al                 ; the pixel shift 0,2,4,6
    call xs_shift
    call xs_emit
    inc word [xs_k]
    cmp word [xs_k], XB_PHASES_V
    jb .phase
    inc word [xs_p]
    cmp word [xs_p], EXB_NPOSES
    jb .pose
    mov [xb_sprused], di
    clc
    ret
.fail:
    stc
    ret

; the pose's four masks shifted right by [xs_ph] pixels into xs_tmp: mask-major,
; row, then the 4 bytes of the 32-bit row (a 24-pixel row shifted by up to 6 is
; 30 pixels).  Layer 0 is then turned into the OUTLINE: opaque and not any ink.
xs_shift:
    push ds
    push di
    mov ax, [xs_p]
    mov cx, 4 * 72
    mul cx
    mov si, ax
    mov bp, [xb_recs + EXBR_POSES * 2]
    inc bp                          ; past the pose count
    add si, bp
    mov ds, [cs:xb_gfxseg]
    mov bx, xs_tmp
    mov bp, 4
.mask:
    mov ch, XB_POSE_H
.row:
    lodsb
    mov dh, al
    lodsb
    mov dl, al
    lodsb
    mov ah, al
    xor al, al                      ; DX:AX = row << 8
    mov cl, [cs:xs_ph]
    or cl, cl
    jz .store
.sh:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .sh
.store:
    mov [cs:bx], dh
    mov [cs:bx + 1], dl
    mov [cs:bx + 2], ah
    mov [cs:bx + 3], al
    add bx, 4
    dec ch
    jnz .row
    dec bp
    jnz .mask
    pop di
    pop ds
    mov bx, xs_tmp
    mov cx, 96
.outline:
    mov al, [bx + 96]
    or al, [bx + 192]
    or al, [bx + 288]
    not al
    and [bx], al
    inc bx
    loop .outline
    ret

; the four layers of xs_tmp as a blob at ES:DI
%macro XS_BYTE 1
    mov al, [si + %1]
    test al, al
    jz %%skip
    mov [es:di + 2], al
    mov ax, dx
    add ax, %1
    stosw
    inc di
    inc cx
%%skip:
%endmacro

xs_emit:
    xor bp, bp
.layer:
    mov ax, bp
    mov cx, 96
    mul cx
    add ax, xs_tmp
    mov si, ax
    mov [xs_cnt], di                ; where this layer's count goes
    xor ax, ax
    stosw
    xor cx, cx
    xor dx, dx                      ; row * 40
    mov byte [xs_row], XB_POSE_H
.row:
    XS_BYTE 0
    XS_BYTE 1
    XS_BYTE 2
    XS_BYTE 3
    add si, 4
    add dx, 40
    dec byte [xs_row]
    jnz .row
    mov bx, [xs_cnt]
    mov [es:bx], cx
    inc bp
    cmp bp, 4
    jb .layer
    ret

; -----------------------------------------------------------------------------
; CGA: x mod 4 in {0,2}.  The claim starts with EXB_NPOSES x 2 word offsets ((pose * 2 +
; phase / 2) * 2), then per (pose, phase) 24 rows of
;
;       byte first, byte count, then `count` (AND, OR) byte pairs
;
; a row TRIMMED to the bytes that hold an opaque pixel (a pose is ~8 opaque pixels
; a row of 24: two or three of six bytes), so the draw touches ~70 bytes rather
; than 168.  A row is 6 bytes at phase 0, 7 at phase 2 (24 pixels shifted by 2
; spill into a seventh).  AND has 0s over the opaque pixels and OR their ink (2 bits
; a pixel, the palette's p0_high map of the layer's slot; the outline is layer 0).
;
; Built a BYTE at a time, not a pixel: a 256-word table expands 8 mask bits to 16
; (pixel i -> bits 15-2i and 14-2i), so a row is a dozen table lookups and ANDs, and
; the whole 56 blobs take ~0.3 s where the pixel loop this replaced took ~2.5 s.
; -----------------------------------------------------------------------------
xs_load_cga:
    push es
    push ds
    pop es
    mov di, xs_ctab                 ; no pose is compiled until xs_compile says so
    mov cx, EXB_NPOSES * XB_PHASES_C
    xor ax, ax
    rep stosw
    pop es
    call xs_mkexp
    push ds
    mov bx, [xb_recs + EXBR_PAL * 2]
    mov si, bx
    add si, 34                      ; the four pose ink slots
    mov di, xs_ink55
    mov ds, [cs:xb_gfxseg]
    mov cx, 4
.ink:
    lodsb
    xor ah, ah
    push bx
    add bx, ax                      ; the slot indexes the palette's p0_high map
    mov al, [bx]                    ; the 2-bit ink
    pop bx
    mov ah, 55h
    mul ah                          ; ink * 55h: the ink in all four pixel positions
    mov [cs:di], al
    inc di
    loop .ink
    pop ds
    mov di, EXB_NPOSES * XB_PHASES_C * 2   ; the blobs start after the table
    mov word [xs_p], 0
.pose:
    mov word [xs_k], 0
.phase:
    cmp di, XS_CLAIM_C * 1024 - 1024
    ja .fail
    mov ax, [xs_p]
    shl ax, 1
    add ax, [xs_k]
    shl ax, 1
    mov bx, ax
    mov [es:bx], di                 ; the table entry
    mov word [xs_rowi], 0
.row:
    call xs_cgarow
    inc word [xs_rowi]
    cmp word [xs_rowi], XB_POSE_H
    jb .row
    inc word [xs_k]
    cmp word [xs_k], XB_PHASES_C
    jb .phase
    inc word [xs_p]
    cmp word [xs_p], EXB_NPOSES
    jb .pose
    mov [xb_sprused], di
    clc
    ret
.fail:
    stc
    ret

; xs_exp[b] = the 8 bits of b as 16 (11 for a set bit), leftmost pixel in bits 15-14
xs_mkexp:
    mov di, xs_exp
    xor bx, bx
.b:
    xor ax, ax
    mov cx, 8
    mov dl, bl
.i:
    shl ax, 1
    shl ax, 1
    shl dl, 1
    jnc .z
    or al, 3
.z:
    loop .i
    mov [di], ax
    add di, 2
    inc bx
    cmp bx, 256
    jb .b
    ret

; one pose row, [xs_p] / [xs_rowi] / [xs_k], emitted at ES:DI (advanced)
xs_cgarow:
    push ds
    mov ax, [xs_p]
    mov cx, 4 * 72
    mul cx
    add ax, [xb_recs + EXBR_POSES * 2]
    inc ax
    mov si, ax
    mov ax, [xs_rowi]
    mov bx, ax
    shl ax, 1
    add ax, bx
    add si, ax                      ; the row's first opaque-mask byte
    mov ds, [cs:xb_gfxseg]
    xor bp, bp                      ; 2 * the source byte 0..2
.q:
    mov bl, [si + 72]               ; ink 1
    xor bh, bh
    shl bx, 1
    mov ax, [cs:xs_exp + bx]
    mov dh, [cs:xs_ink55 + 1]
    mov dl, dh
    and ax, dx
    mov [cs:xs_w], ax               ; the OR word so far
    mov bl, [si + 144]              ; ink 2
    xor bh, bh
    shl bx, 1
    mov ax, [cs:xs_exp + bx]
    mov dh, [cs:xs_ink55 + 2]
    mov dl, dh
    and ax, dx
    or [cs:xs_w], ax
    mov bl, [si + 216]              ; ink 3
    xor bh, bh
    shl bx, 1
    mov ax, [cs:xs_exp + bx]
    mov dh, [cs:xs_ink55 + 3]
    mov dl, dh
    and ax, dx
    or [cs:xs_w], ax
    mov al, [si + 72]               ; the outline: opaque and no ink layer
    or al, [si + 144]
    or al, [si + 216]
    not al
    and al, [si]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [cs:xs_exp + bx]
    mov dh, [cs:xs_ink55]
    mov dl, dh
    and ax, dx
    or [cs:xs_w], ax
    mov bl, [si]                    ; the opaque bits
    xor bh, bh
    shl bx, 1
    mov ax, [cs:xs_exp + bx]        ; the AND is the complement, below
    mov [cs:xs_opb + bp], ah
    mov [cs:xs_opb + bp + 1], al
    mov ax, [cs:xs_w]
    mov [cs:xs_orb + bp], ah
    mov [cs:xs_orb + bp + 1], al
    inc si
    add bp, 2
    cmp bp, 6
    jb .q
    pop ds
    mov cx, 6
    cmp word [xs_k], 0
    je .trim
    ; phase 2: shift the two 6-byte arrays right by 4 bits into 7 bytes
    mov bx, xs_opb
    call xs_shift4
    mov bx, xs_orb
    call xs_shift4
    mov cx, 7
.trim:
    xor bx, bx                      ; the first byte holding an opaque pixel
.first:
    cmp bx, cx
    jae .empty
    cmp byte [xs_opb + bx], 0
    jne .have
    inc bx
    jmp .first
.have:
    mov si, cx
.last:
    dec si
    cmp byte [xs_opb + si], 0
    je .last                        ; SI = the last one (BX <= SI: one exists)
    mov ax, si
    sub ax, bx
    inc al                          ; the count
    mov ah, al
    mov al, bl                      ; the first
    stosw
    mov cl, ah
    xor ch, ch                      ; CX = the count
.pair:
    mov al, [xs_opb + bx]
    not al
    mov ah, [xs_orb + bx]
    stosw
    inc bx
    loop .pair
    ret
.empty:
    xor ax, ax
    stosw
    ret

; BX -> 8 bytes holding 6: shifted right 4 bits into 7 (the low nibble of the last
; byte spills into the seventh)
xs_shift4:
    mov al, [bx + 5]
    mov cl, 4
    shl al, cl
    mov [bx + 6], al
    mov si, 5
.s:
    mov al, [bx + si]
    shr al, cl
    or si, si
    jz .s0
    mov ah, [bx + si - 1]
    shl ah, cl
    or al, ah
.s0:
    mov [bx + si], al
    dec si
    jns .s
    ret

; -----------------------------------------------------------------------------
; xs_compile - the hot poses as straight-line code (SPEC.md 102.7.2).  The interpreter above
; (xc_draw's `lodsw / and / or / stosb / loop`) costs ~85 clocks a byte, and the rider and every
; opponent draw ~90 bytes of it a frame: poses 0, 1 and 12 are 80% of a lap's frames and nine
; poses are 94% (xs_hot), so those are emitted, for both phases, as the code that does what the
; loop does with the data folded into the instructions:
;
;   a fully opaque byte (AND = 0)          mov byte [es:di+d], imm            ~24 clocks
;   two of them side by side               mov word [es:di+d], imm16          ~28
;   anything else                          mov al,[es:di+d] / and al,imm /
;                                          or al,imm / mov [es:di+d],al       ~52
;                                          (words: the same on AX)
;   a byte that changes nothing (AND = FF, OR = 0)   nothing at all
;   a row                                  add di,dx  (DX = the box's stride)
;
; The 8088 is fetch-bound (4 clocks a code byte), so an ES-prefixed instruction costs what its
; length says, and the fully opaque bytes, 45% of all pose bytes, cost under a third of the loop's.
; Called with a far call whose target is [xs_cfar]: ES = the package (the box buffer), DI = the
; pose's first row pointer, DX = the box stride.  Nothing is pushed and no flag is kept.
; Entry: ES = the sprite claim, DI = the first free byte, CS = the package.  A block is under
; 1 KB (the tightest pose is ~600 bytes); when the claim has no room for one more, the rest stay
; interpreted (their xs_ctab entry is 0).
; -----------------------------------------------------------------------------
xs_hot: db 0, 1, 12, 15, 16, 9, 2, 11, 10, 0ffh

%macro XE 1
    mov al, %1
    stosb
%endmacro

xs_compile:
    SAVE
    push ds
    push es
    pop ds                          ; DS = ES = the claim: the blobs are read from it
    mov word [cs:xs_hp], xs_hot
.pose:
    mov bx, [cs:xs_hp]
    mov al, [cs:bx]
    cmp al, 0ffh
    je .done
    inc word [cs:xs_hp]
    xor ah, ah
    shl ax, 1                       ; pose * 2
    mov [cs:xs_cp], ax
    mov word [cs:xs_ck], 0
.phase:
    cmp di, XS_CLAIM_CC * 1024 - 1024
    ja .done
    mov bp, [cs:xs_cp]
    add bp, [cs:xs_ck]
    shl bp, 1                       ; the byte offset in the blob table and in xs_ctab
    mov si, bp
    mov si, [si]                    ; the blob
    mov [cs:xs_ctab + bp], di       ; ...and where its code starts
    mov byte [cs:xs_crow], XB_POSE_H
.row:
    cmp byte [cs:xs_crow], XB_POSE_H
    je .first
    mov ax, 0d701h                  ; add di, dx
    stosw
.first:
    lodsw                           ; AL = first byte, AH = count
    mov dl, al                      ; DL = the displacement of the next byte
    mov cl, ah
    xor ch, ch
.byte:
    jcxz .re
    jmp .have
.re:
    jmp .rowdone
.have:
    mov ax, [si]                    ; AL = AND, AH = OR of this byte
    cmp ax, 00ffh
    jne .doit
    add si, 2                       ; it changes nothing: not a byte of code
    inc dl
    dec cx
    jmp .byte
.doit:
    cmp cx, 2
    jb .byteop
    mov bx, [si + 2]
    cmp bx, 00ffh
    je .byteop                      ; the next one changes nothing: this one alone
    ; ---- two neighbours: one word op
    mov [cs:xs_w0], ax
    mov [cs:xs_w1], bx
    cmp al, 0
    jne .wmix
    cmp bl, 0
    jne .wmix
    XE 26h                          ; both opaque: mov word [es:di+d], OR0 | OR1 << 8
    XE 0c7h
    call xs_mod
    mov al, [cs:xs_w0 + 1]
    stosb
    mov al, [cs:xs_w1 + 1]
    stosb
    jmp .wdone
.wmix:
    XE 26h                          ; mov ax, [es:di+d]
    XE 8bh
    call xs_mod
    XE 25h                          ; and ax, AND0 | AND1 << 8
    mov al, [cs:xs_w0]
    stosb
    mov al, [cs:xs_w1]
    stosb
    mov al, [cs:xs_w0 + 1]
    or al, [cs:xs_w1 + 1]
    jz .wstore
    XE 0dh                          ; or ax, OR0 | OR1 << 8
    mov al, [cs:xs_w0 + 1]
    stosb
    mov al, [cs:xs_w1 + 1]
    stosb
.wstore:
    XE 26h                          ; mov [es:di+d], ax
    XE 89h
    call xs_mod
.wdone:
    add si, 4
    add dl, 2
    sub cx, 2
    jmp .byte
.byteop:
    mov [cs:xs_w0], ax
    cmp al, 0
    jne .bmix
    XE 26h                          ; mov byte [es:di+d], OR
    XE 0c6h
    call xs_mod
    mov al, [cs:xs_w0 + 1]
    stosb
    jmp .bdone
.bmix:
    XE 26h                          ; mov al, [es:di+d]
    XE 8ah
    call xs_mod
    cmp byte [cs:xs_w0], 0ffh
    je .bor
    XE 24h                          ; and al, AND
    mov al, [cs:xs_w0]
    stosb
.bor:
    cmp byte [cs:xs_w0 + 1], 0
    je .bstore
    XE 0ch                          ; or al, OR
    mov al, [cs:xs_w0 + 1]
    stosb
.bstore:
    XE 26h                          ; mov [es:di+d], al
    XE 88h
    call xs_mod
.bdone:
    add si, 2
    inc dl
    dec cx
    jmp .byte
.rowdone:
    dec byte [cs:xs_crow]
    jnz .row
    XE 0cbh                         ; retf
    inc word [cs:xs_ck]
    cmp word [cs:xs_ck], XB_PHASES_C
    jb .phase
    jmp .pose
.done:
    mov [cs:xb_sprused], di
    pop ds
    RESTORE
    ret

; the ModRM of [es:di + d] with the register field 0 (AL / AX): mod 00 rm 101 (05h) when d = 0,
; else mod 01 rm 101 (45h) and the disp8.  DL = d; DL and the flags of the caller are not kept.
xs_mod:
    or dl, dl
    jnz .d
    XE 05h
    ret
.d:
    XE 45h
    mov al, dl
    stosb
    ret

; -----------------------------------------------------------------------------
; xs_compile_v - the VGA's hot poses as code (SPEC.md 102.7.2).  The draw above is a record loop
; (`lodsw / lodsb / out / mov / xchg / loop`, ~85 clocks a (byte, layer) pair); a pose's records
; are known, so they become the instructions themselves:
;
;       mov al, MASK ; out dx, al        the layer's pixels of this byte (skipped when the mask
;                                        is the last one written: the port keeps it)
;       mov al, COLOUR                   an immediate (layers 0, 2, 3) or `mov al,bl` (layer 1: the
;                                        rider's colour is a per-bike variable)
;       xchg al, [es:di + off]           the read loads the latches, the write puts COLOUR under MASK
;
; ~45 clocks a pair.  Only poses 0, 1 and 12 (80% of a lap's frames) in all four phases: 12 blocks
; of about a kilobyte, so the claim stays inside the plan's 44 KB.
; Called with a far call whose target is [xs_cfar]: DX = 3CFh with GC index 8 selected, ES = the
; card, DI = the pose's first byte, BL = the rider's colour.
; Entry: ES = the sprite claim, DI = the first free byte.
; -----------------------------------------------------------------------------
xs_hot_v: db 0, 1, 12, 0ffh

xs_compile_v:
    SAVE
    push ds
    ; the layers' colours from the palette record's pose ink slots (after the five themes)
    mov bx, [xb_recs + EXBR_PAL * 2]
    add bx, 240
    mov ds, [cs:xb_gfxseg]
    mov al, [bx]
    mov [cs:xs_vink], al
    mov al, [bx + 2]
    mov [cs:xs_vink + 2], al
    mov al, [bx + 3]
    mov [cs:xs_vink + 3], al
    push es
    pop ds                          ; DS = ES = the claim
    mov word [cs:xs_hp], xs_hot_v
.pose:
    mov bx, [cs:xs_hp]
    mov al, [cs:bx]
    cmp al, 0ffh
    je .done
    inc word [cs:xs_hp]
    xor ah, ah
    shl ax, 1
    shl ax, 1                       ; pose * 4
    mov [cs:xs_cp], ax
    mov word [cs:xs_ck], 0
.phase:
    cmp di, XS_CLAIM_V * 1024 - 2048
    ja .done
    mov bp, [cs:xs_cp]
    add bp, [cs:xs_ck]
    shl bp, 1
    mov si, bp
    mov si, [si]                    ; the blob
    mov [cs:xs_ctab + bp], di
    mov byte [cs:xs_crow], 0        ; the layer
    mov word [cs:xs_w0], 0ffffh     ; the last mask written: none
.layer:
    lodsw
    mov cx, ax                      ; the layer's record count
.rec:
    jcxz .layerend
    lodsw
    mov bx, ax                      ; the offset from the box's first byte
    lodsb                           ; the mask
    xor ah, ah
    cmp ax, [cs:xs_w0]
    je .samemask
    mov [cs:xs_w0], ax
    mov ah, al
    XE 0b0h                         ; mov al, MASK
    mov al, ah
    stosb
    XE 0eeh                         ; out dx, al
.samemask:
    cmp byte [cs:xs_crow], 1
    jne .imm
    XE 88h                          ; mov al, bl
    XE 0d8h
    jmp .xchg
.imm:
    XE 0b0h                         ; mov al, COLOUR of the layer
    push bx
    mov bl, [cs:xs_crow]
    xor bh, bh
    mov al, [cs:xs_vink + bx]
    pop bx
    stosb
.xchg:
    XE 26h                          ; xchg al, [es:di + off]
    XE 86h
    cmp bx, 128
    jae .wide
    XE 45h
    mov al, bl
    stosb
    dec cx
    jmp .rec
.wide:
    XE 85h
    mov ax, bx
    stosw
    dec cx
    jmp .rec
.layerend:
    inc byte [cs:xs_crow]
    cmp byte [cs:xs_crow], 4
    jb .layer
    XE 0cbh                         ; retf
    inc word [cs:xs_ck]
    cmp word [cs:xs_ck], XB_PHASES_V
    jb .phase
    jmp .pose
.done:
    mov [cs:xb_sprused], di
    pop ds
    RESTORE
    ret
