; =============================================================================
; os8088 - apps/win16/w16cens.inc
;
; THE 8088'S CENSUS (SPEC.md 124.4, 124.4.1). A program built for the 80186
; does not fault on an 8088: `0x60`-`0x6F` alias the conditional jumps and
; `0x0F` is `pop cs`, so it runs SOMETHING ELSE - a silent wrong answer. The
; loader asks the CPU first (w16_cputest); a CPU that runs 186 code skips
; this, and on an 8088 or 8086 the program's code is swept here before it
; ever runs.
;
; IT IS tools/os88ne.py's census, run on the machine: recursive descent from
; the program's entry point, its exports and every internal relocation
; target; then `jmp cs:[bx+table]` switch tables, once the straight-line
; code is known; then the prologue heuristic over what is still dark. The
; host tool is the reference and this follows it decision for decision -
; the one difference is that it runs AFTER relocation, so a far call's
; target is read as a real segment: one of the program's (followed) or
; WIN16's own (an import stub: followed no further, and FatalExit and
; FatalAppExit do not return). One byte an opcode comes from
; w16optab.inc, which os88ne.py WRITES, so the two decoders cannot drift.
;
; THE VERDICT IS POST HOC, as the host's is. Every 186/286/386 instruction
; reached is recorded, every Open Watcom style CPU probe - `push sp / pop r /
; cmp r, sp / jnz past` - is recorded as a GUARDED range (an 8086 pushes SP
; after decrementing it, so the jump is taken and the fall-through is the
; 286's side), and only an instruction outside every guarded range counts.
;
; WHAT IT COSTS is one claim for the walk - a bit a byte of code, plus the
; work stack - freed before the program runs, and the sweep's time, which is
; paid only on a CPU that cannot run 186 code. A walk that outgrows its
; stack cannot vouch for what it did not reach, so it REFUSES rather than
; guess.
; =============================================================================

W16C_STK  equ 2048                  ; pending (segment, offset) pairs
W16C_TBL  equ 32                    ; pending switch tables
W16C_GRD  equ 16                    ; guarded ranges
W16C_TIR  equ 64                    ; tiered instructions reached
W16C_STKO equ 0                     ; ...each list's offset past the bitmaps
W16C_TBLO equ W16C_STKO + 4 * W16C_STK
W16C_GRDO equ W16C_TBLO + 4 * W16C_TBL
W16C_TIRO equ W16C_GRDO + 6 * W16C_GRD
W16C_END  equ W16C_TIRO + 6 * W16C_TIR

; -----------------------------------------------------------------------------
; w16_census - may this 8088 run the program? (SPEC.md 124.4)
; in:  the segments copied and relocated, [w16_fseg] = the file
; out: CF=0 run it; CF=1 refused, [w16_l1]/[w16_l2] written
; -----------------------------------------------------------------------------
w16_census:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [w16_fseg]
    xor ax, ax
    mov [w16c_n], ax
    mov [w16c_nt], ax
    mov [w16c_ng], ax
    mov [w16c_ni], ax
    mov [w16c_ov], al
    mov [w16c_tov], al
    mov [w16c_sweep], al

    ; --- one bit a byte of every CODE segment that has bytes in the file ---
    mov si, [w16_ne]
    mov di, si
    add di, [es:si + NE_SEGTAB]     ; ES:DI -> the segment table
    xor bx, bx                      ; BX = segment * 2
    xor ax, ax                      ; AX = the next bitmap's offset
.size:
    mov word [w16_cbm + bx], 0xFFFF
    test word [es:di + 4], NSF_DATA
    jnz .nextsz
    cmp word [es:di], 0             ; no bytes in the file: nothing to sweep
    je .nextsz                      ; (os88ne.py's `s.code and s.data`)
    mov [w16_cbm + bx], ax
    mov dx, [es:di + 2]             ; the length in the file, 0 = 64KB
    dec dx
    mov [w16_clast + bx], dx        ; ...as its last offset
    mov cl, 3
    shr dx, cl
    inc dx
    add ax, dx
.nextsz:
    add di, 8
    inc bx
    inc bx
    mov dx, bx
    shr dx, 1
    cmp dx, [w16_nseg]
    jb .size
    add ax, 15
    and ax, 0xFFF0
    mov [w16c_obase], ax
    add ax, W16C_END + 1023
    rcr ax, 1                       ; (the carry is bit 16: a census of 64KB
    mov cl, 9                       ; of code still sizes right)
    shr ax, cl                      ; AX = KB
    call OSAPI_MEM_CLAIM            ; DX = the block
    jnc .claimed
    mov si, w16_r_mem
    jmp .refuse
.claimed:
    mov [w16c_cseg], dx
    mov es, dx                      ; every bit clear, every list empty
    xor di, di
    mov cx, [w16c_obase]
    add cx, W16C_END
    shr cx, 1
    xor ax, ax
    cld
    rep stosw

    ; --- the seeds: the entry point, the exports, the relocation targets ---
    mov es, [w16_fseg]
    mov si, [w16_ne]
    mov dx, [es:si + NE_CS]
    mov ax, [es:si + NE_IP]
    call w16c_seed
    call w16c_ents
    mov al, 2                       ; w16_relocs' third pass: seed, not apply
    call w16_relocs
    call w16c_run

    ; --- switch tables, once the straight-line code is known --------------
    call w16c_tables

    ; --- the prologue heuristic, over what is still dark ------------------
    mov byte [w16c_sweep], 1        ; a table found now is not read: the host's
    call w16c_prologs               ; pending list is never revisited either

    ; --- the verdict -------------------------------------------------------
    cmp byte [w16c_ov], 0
    je .verdict
    mov si, w16_r_cbig              ; a walk that ran out of room did not see
    jmp short .free                 ; everything, so it cannot vouch
.verdict:
    call w16c_tierof                ; AL = the highest unguarded tier
    mov si, w16_r_c186
    cmp al, 1
    je .free
    mov si, w16_r_c286
    cmp al, 2
    je .free
    mov si, w16_r_c386
    cmp al, 3
    je .free
    mov dx, [w16c_cseg]
    call OSAPI_MEM_FREE
    clc
    jmp short .out
.free:
    mov dx, [w16c_cseg]
    call OSAPI_MEM_FREE
.refuse:
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_name
    mov di, w16_l2
    call w16_strcpy
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16c_seed - DX = a segment NUMBER (1-based), AX = an offset: walk it later
; Anything that is not a code segment of the program is ignored. Keeps all.
; -----------------------------------------------------------------------------
w16c_seed:
    push bx
    or dx, dx
    jz .out
    cmp dx, [w16_nseg]
    ja .out
    mov bx, dx
    dec bx
    shl bx, 1
    call w16c_push
.out:
    pop bx
    ret

; w16c_push - BX = segment * 2, AX = an offset: onto the work stack, if the
; segment is code and the offset inside it (os88ne.py's `seed`). Keeps all.
w16c_push:
    cmp word [w16_cbm + bx], 0xFFFF
    je .out
    cmp ax, [w16_clast + bx]
    ja .out
    push di
    push es
    mov di, [w16c_n]
    cmp di, W16C_STK
    jb .room
    mov byte [w16c_ov], 1
    jmp short .done
.room:
    inc word [w16c_n]
    shl di, 1
    shl di, 1
    add di, [w16c_obase]
    mov es, [w16c_cseg]
    mov [es:di + W16C_STKO], bx
    mov [es:di + W16C_STKO + 2], ax
.done:
    pop es
    pop di
.out:
    ret

; -----------------------------------------------------------------------------
; w16c_ents - every export in the entry table is a seed. ES = the file.
; -----------------------------------------------------------------------------
w16c_ents:
    push ax
    push cx
    push dx
    push si
    mov si, [w16_ne]
    mov dx, si
    add si, [es:si + NE_ENTTAB]
.bundle:
    mov ax, si
    inc ax
    cmp ax, [w16_fend]              ; the count and the indicator
    jae .done
    xor cx, cx
    mov cl, [es:si]
    jcxz .done
    mov dl, [es:si + 1]
    add si, 2
    or dl, dl
    jz .bundle                      ; skipped ordinals: no bytes follow
    cmp dl, 0xFE
    jne .live
    mov ax, cx                      ; constants: three bytes each, no code
    shl ax, 1
    add ax, cx
    add si, ax
    jmp short .bundle
.live:
    mov ax, si
    add ax, 5
    cmp ax, [w16_fend]
    ja .done
    push dx
    cmp dl, 0xFF
    je .movable
    xor dh, dh                      ; fixed: flags, offset; segment = DL
    mov ax, [es:si + 1]
    call w16c_seed
    pop dx
    add si, 3
    jmp short .nextent
.movable:
    xor dh, dh                      ; movable: flags, int 3Fh, segment, offset
    mov dl, [es:si + 3]
    mov ax, [es:si + 4]
    call w16c_seed
    pop dx
    add si, 6
.nextent:
    loop .live
    jmp short .bundle
.done:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16c_run - walk until the work stack is empty
; -----------------------------------------------------------------------------
w16c_run:
    push ax
    push bx
    push di
    push si
    push es
.next:
    cmp byte [w16c_ov], 0
    jne .done
    mov si, [w16c_n]
    or si, si
    jz .done
    dec si
    mov [w16c_n], si
    shl si, 1
    shl si, 1
    add si, [w16c_obase]
    mov es, [w16c_cseg]
    mov bx, [es:si + W16C_STKO]
    mov di, [es:si + W16C_STKO + 2]
    call w16c_walk
    jmp short .next
.done:
    pop es
    pop si
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16c_walk - one straight line of code, from BX = segment * 2, DI = offset
; (os88ne.py's `run`, its inner loop). Clobbers AX, CX, DX, SI, DI, ES.
; -----------------------------------------------------------------------------
w16c_walk:
    push bp
    mov [w16c_cur], bx
    mov ax, [w16_clast + bx]
    mov [w16c_last], ax
    mov ax, [w16_sbase + bx]
    mov [w16c_cs], ax
    mov ax, 0xFFFF
    mov [w16c_h1], ax
    mov [w16c_h2], ax
    mov [w16c_h3], ax
.ins:
    cmp di, [w16c_last]
    ja .end
    mov cx, 1                       ; already an instruction, or inside one
    xor al, al
    call w16c_bits
    jc .end
    mov es, [w16c_cs]
    call w16c_dec                   ; CX = length, SI = the opcode, DH = modrm
    jc .end                         ; bytes that are not an instruction
    xor al, al
    call w16c_bits                  ; ...any of its bytes taken: an overlap
    jc .end
    mov al, 1
    call w16c_bits
    xor ax, ax
    mov al, [w16c_cls]
    shl ax, 1
    mov bp, ax
    jmp word [ds:w16c_jt + bp]

.t186:
    mov al, 1
    call w16c_tier
    jmp short .fall
.t286:
    mov al, 2
    call w16c_tier
    jmp short .fall
.t386:                              ; not followed: the verdict is in
    mov al, 3
    call w16c_tier
    jmp short .end
.br8:
    call w16c_rel8                  ; AX = the target
    call w16c_guard
    call w16c_push
    jmp short .fall
.jmp8:
    call w16c_rel8
    call w16c_push
    jmp short .end
.jmp16:
    call w16c_rel16
    call w16c_push
    jmp short .end
.call16:
    call w16c_rel16
    call w16c_push
    jmp short .fall
.callf:
    call w16c_far                   ; CF=1: an import that never returns
    jc .end
    jmp short .fall
.jmpf:
    call w16c_far
    jmp short .end
.int:
    cmp byte [w16c_int], 0x21
    jne .fall
    call w16c_ahbefore              ; DOS terminate: nothing follows it
    cmp al, 0x4C
    je .end
    jmp short .fall
.grpff:
    mov al, dh
    and al, 0x38
    cmp al, 0x20                    ; reg 4: jmp near indirect
    je .ijmp
    cmp al, 0x28                    ; reg 5: jmp far indirect
    je .end
    jmp short .fall
.ijmp:
    test dl, 1                      ; cs: [base + disp16]: a switch table
    jz .end
    mov al, dh
    and al, 0xC0
    cmp al, 0x80
    jne .end
    mov ax, [es:si + 2]
    call w16c_qtab
.end:
    pop bp
    ret
.fall:
    mov ax, [w16c_h2]
    mov [w16c_h3], ax
    mov ax, [w16c_h1]
    mov [w16c_h2], ax
    mov [w16c_h1], di
    add di, cx
    jc .end                         ; off the end of 64KB
    jmp .ins

w16c_jt:
    dw w16c_walk.fall               ; NEXT
    dw w16c_walk.t186               ; T186
    dw w16c_walk.t286               ; T286
    dw w16c_walk.br8                ; BR8
    dw w16c_walk.jmp8               ; JMP8
    dw w16c_walk.jmp16              ; JMP16
    dw w16c_walk.call16             ; CALL16
    dw w16c_walk.callf              ; CALLF
    dw w16c_walk.jmpf               ; JMPF
    dw w16c_walk.end                ; STOP
    dw w16c_walk.int                ; INT
    dw w16c_walk.grpff              ; GRPFF
    dw w16c_walk.fall               ; GRPF6 (its immediate is in the length)
    dw w16c_walk.fall               ; PREFIX (never: the decoder steps over)
    dw w16c_walk.fall               ; OP0F (never: the decoder says which)
    dw w16c_walk.t386               ; T386

; w16c_rel8 / w16c_rel16 - AX = the target of the instruction at DI, CX long
w16c_rel8:
    push bp
    mov bp, di
    add bp, cx
    mov al, [es:bp - 1]
    cbw
    add ax, bp
    pop bp
    ret
w16c_rel16:
    push bp
    mov bp, di
    add bp, cx
    mov ax, [es:bp - 2]
    add ax, bp
    pop bp
    ret

; -----------------------------------------------------------------------------
; w16c_dec - one instruction at ES:DI (os88ne.py's `decode`)
; out: CF=1 not one (it runs off the segment); else CX = its length, SI = its
;      opcode's offset, DH = its modrm, DL bit 0 = a cs: prefix, [w16c_cls]
;      its class (a 0F instruction answered as T286 or T386) and [w16c_int]
;      an int's number. Keeps AX, BX, DI, BP.
; -----------------------------------------------------------------------------
w16c_dec:
    push ax
    push bp
    mov si, di
    xor dl, dl
    mov byte [w16c_int], 0
.pfx:
    cmp si, [w16c_last]
    ja .bad
    mov al, [es:si]
    push bx
    mov bl, al
    xor bh, bh
    mov ah, [w16_optab + bx]
    pop bx
    mov ch, ah
    mov cl, 4
    shr ch, cl                      ; CH = the class
    cmp ch, W16C_PREFIX
    jne .op
    cmp al, 0x2E
    jne .np
    or dl, 1
.np:
    inc si
    jz .bad
    jmp short .pfx
.op:
    mov [w16c_cls], ch
    mov [w16c_op], al
    mov cl, ah                      ; the table's byte: w16c_mrlen answers in
    mov bp, si                      ;   AX, so AH does not survive a modrm
    inc bp                          ; BP = past the opcode
    cmp ch, W16C_OP0F
    je .op0f
    cmp ch, W16C_T386
    je .chk                         ; a 386 prefix: one byte, and a verdict
    test ah, 8
    jz .imm
    cmp bp, [w16c_last]
    ja .bad
    mov dh, [es:bp]
    inc bp
    call w16c_mrlen
    add bp, ax
    cmp ch, W16C_GRPF6
    jne .imm
    test dh, 0x30                   ; test / not / neg ...: only reg 0 and 1
    jnz .imm                        ; carry an immediate
    mov al, [w16c_op]
    and ax, 1
    inc ax                          ; F6: one byte, F7: two
    add bp, ax
    jmp short .chk
.imm:
    cmp ch, W16C_INT
    jne .plain
    cmp bp, [w16c_last]
    ja .bad
    mov al, [es:bp]
    mov [w16c_int], al
    cmp al, 0x34                    ; the MS C emulator's escapes (SPEC.md
    jb .plain                       ; 124.5): int 34h-3Bh carry the escape's
    cmp al, 0x3D                    ; modrm, 3Ch a segment byte first, and
    ja .plain                       ; 3Dh is a bare fwait
    inc bp
    cmp al, 0x3D
    je .chk
    cmp al, 0x3C
    jne .esc
    inc bp
.esc:
    cmp bp, [w16c_last]
    ja .bad
    mov dh, [es:bp]
    inc bp
    call w16c_mrlen
    add bp, ax
    jmp short .chk
.plain:
    mov al, cl
    and ax, 7                       ; the immediate's bytes
    add bp, ax
.chk:
    cmp bp, si                      ; wrapped past 64KB
    jbe .bad
    mov ax, bp
    dec ax
    cmp ax, [w16c_last]
    ja .bad
    mov cx, bp
    sub cx, di
    clc
    pop bp
    pop ax
    ret
.bad:
    stc
    pop bp
    pop ax
    ret
.op0f:                              ; the 286's system forms, else a 386 one
    cmp bp, [w16c_last]
    ja .bad
    mov al, [es:bp]
    inc bp
    mov byte [w16c_cls], W16C_T286
    cmp al, 3
    ja .no_mr
    cmp bp, [w16c_last]
    ja .bad
    mov dh, [es:bp]
    inc bp
    call w16c_mrlen
    add bp, ax
    jmp short .chk
.no_mr:
    cmp al, 5                       ; loadall
    je .chk
    cmp al, 6                       ; clts
    je .chk
    mov byte [w16c_cls], W16C_T386
    jmp short .chk

; w16c_mrlen - DH = a modrm -> AX = its displacement bytes
w16c_mrlen:
    mov al, dh
    and al, 0xC0
    jz .m0
    cmp al, 0xC0
    je .zero
    cmp al, 0x40
    je .one
    mov ax, 2
    ret
.one:
    mov ax, 1
    ret
.m0:
    mov al, dh
    and al, 7
    cmp al, 6                       ; [disp16]
    jne .zero
    mov ax, 2
    ret
.zero:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; w16c_bits - the walk's bitmap: AL = 0 TEST, 1 MARK; DI = the first offset,
; CX = how many; BX = segment * 2. out (test): CF=1 any of them set. Keeps all
; but the flags.
; -----------------------------------------------------------------------------
w16c_bits:
    push cx
    push dx
    push si
    push di
    push es
    mov es, [w16c_cseg]
.l:
    mov si, di
    shr si, 1
    shr si, 1
    shr si, 1
    add si, [w16_cbm + bx]
    push cx
    mov cx, di
    and cl, 7
    mov dl, 1
    shl dl, cl
    pop cx
    or al, al
    jz .t
    or [es:si], dl
    jmp short .n
.t:
    test [es:si], dl
    jnz .set
.n:
    inc di
    loop .l
    clc
    jmp short .o
.set:
    stc
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16c_tier - AL = a tier (1 186, 2 286, 3 386) reached at [w16c_cur]:DI.
; Recorded for the post-hoc verdict; past the list, the tier stands unguarded.
; -----------------------------------------------------------------------------
w16c_tier:
    push bx
    push si
    push es
    mov si, [w16c_ni]
    cmp si, W16C_TIR
    jb .room
    cmp al, [w16c_tov]
    jbe .out
    mov [w16c_tov], al
    jmp short .out
.room:
    inc word [w16c_ni]
    mov bx, si
    shl si, 1
    add si, bx
    shl si, 1                       ; * 6
    add si, [w16c_obase]
    mov es, [w16c_cseg]
    mov bx, [w16c_cur]
    mov [es:si + W16C_TIRO], bx
    mov [es:si + W16C_TIRO + 2], di
    mov [es:si + W16C_TIRO + 4], al
.out:
    pop es
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16c_guard - is the jump at DI (CX long, target AX) the 286 probe's?
; `push sp / pop r / cmp r, sp / jnz past`, each an instruction this walk
; decoded in a row, the jump forward: [DI+2, target) is the 286's side and is
; recorded as GUARDED (os88ne.py's `_guarded`). Keeps all.
; -----------------------------------------------------------------------------
w16c_guard:
    push ax
    push bx
    push cx
    push si
    push es
    cmp si, di                      ; no prefix on it
    jne .no
    cmp byte [es:di], 0x75
    jne .no
    mov si, di
    sub si, 2
    cmp [w16c_h1], si
    jne .no
    dec si
    cmp [w16c_h2], si
    jne .no
    dec si
    cmp [w16c_h3], si
    jne .no
    cmp byte [es:si], 0x54          ; push sp
    jne .no
    mov cl, [es:si + 1]
    sub cl, 0x58                    ; pop r: CL = r
    cmp cl, 7
    ja .no
    mov ch, cl
    shl ch, 1
    shl ch, 1
    shl ch, 1
    or ch, 0xC4                     ; cmp r, sp as 3B /r ...
    cmp byte [es:si + 2], 0x3B
    jne .alt
    cmp [es:si + 3], ch
    je .yes
    jmp short .no
.alt:
    cmp byte [es:si + 2], 0x39      ; ...or as 39 /r
    jne .no
    or cl, 0xE0
    cmp [es:si + 3], cl
    jne .no
.yes:
    mov bx, di
    add bx, 2
    cmp ax, bx                      ; the jump goes past the 286's side
    jbe .no
    mov si, [w16c_ng]
    cmp si, W16C_GRD
    jae .no                         ; past the list: guards nothing more
    inc word [w16c_ng]
    mov cx, si
    shl si, 1
    add si, cx
    shl si, 1
    add si, [w16c_obase]
    mov es, [w16c_cseg]
    mov cx, [w16c_cur]
    mov [es:si + W16C_GRDO], cx
    mov [es:si + W16C_GRDO + 2], bx
    mov [es:si + W16C_GRDO + 4], ax
.no:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16c_far - a far call or jump at SI (its operand at SI+1): a segment of the
; program is followed, an import stub is not. out: CF=1 an import that never
; returns (KERNEL.1 FatalExit, KERNEL.137 FatalAppExit). Keeps all.
; -----------------------------------------------------------------------------
w16c_far:
    push ax
    push bx
    push cx
    push dx
    mov ax, [es:si + 1]             ; the offset ...
    mov dx, [es:si + 3]             ; ...and the segment, relocated
    mov cx, cs
    cmp dx, cx
    je .import
    xor bx, bx
.seg:
    cmp [w16_sbase + bx], dx
    jne .nextseg
    call w16c_push
.nextseg:
    inc bx
    inc bx
    mov cx, bx
    shr cx, 1
    cmp cx, [w16_nseg]
    jb .seg
    clc
    jmp short .out
.import:
    sub ax, w16_stubs               ; the stub's row
    jb .ret
    xor dx, dx
    mov cx, 6
    div cx
    cmp ax, W16NX
    jae .ret
    mov cx, W16X_SIZE
    mul cx
    mov bx, ax
    cmp byte [w16_exports + bx + W16X_MOD], W16M_KERNEL
    jne .ret
    mov ax, [w16_exports + bx + W16X_ORD]
    cmp ax, 1
    je .noret
    cmp ax, 137
    je .noret
.ret:
    clc
    jmp short .out
.noret:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16c_ahbefore - AL = AH as the instruction just before DI set it (`mov ah,
; imm8` or `mov ax, imm16`, os88ne.py's `_ah_before`), else 0. Keeps all else.
w16c_ahbefore:
    push si
    mov si, [w16c_h1]
    mov ax, di
    sub ax, si
    cmp ax, 2
    jne .three
    cmp byte [es:si], 0xB4
    jne .none
    mov al, [es:si + 1]
    jmp short .out
.three:
    cmp ax, 3
    jne .none
    cmp byte [es:si], 0xB8
    jne .none
    mov al, [es:si + 2]
    jmp short .out
.none:
    xor al, al
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; w16c_qtab - AX = a switch table's offset in segment [w16c_cur]: read it once
; the straight-line code is known. A table the list cannot hold is code the
; walk will not see, so it overflows the census rather than guess.
; -----------------------------------------------------------------------------
w16c_qtab:
    cmp byte [w16c_sweep], 0
    jne .out
    push bx
    push si
    push es
    mov si, [w16c_nt]
    cmp si, W16C_TBL
    jb .room
    mov byte [w16c_ov], 1
    jmp short .done
.room:
    inc word [w16c_nt]
    shl si, 1
    shl si, 1
    add si, [w16c_obase]
    mov es, [w16c_cseg]
    mov bx, [w16c_cur]
    mov [es:si + W16C_TBLO], bx
    mov [es:si + W16C_TBLO + 2], ax
.done:
    pop es
    pop si
    pop bx
.out:
    ret

; -----------------------------------------------------------------------------
; w16c_tables - read every pending switch table, then walk what it names
; (os88ne.py: a word an entry, until one points outside the segment or back
; into the table, or the bytes are code already; at most 256)
; -----------------------------------------------------------------------------
w16c_tables:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
.next:
    cmp byte [w16c_ov], 0
    jne .done
    mov si, [w16c_nt]
    or si, si
    jz .done
    dec si
    mov [w16c_nt], si
    shl si, 1
    shl si, 1
    add si, [w16c_obase]
    mov es, [w16c_cseg]
    mov bx, [es:si + W16C_TBLO]
    mov bp, [es:si + W16C_TBLO + 2] ; BP = t, the table
    mov [w16c_cur], bx
    mov di, bp                      ; DI = p
    xor dx, dx                      ; DX = entries taken
.entry:
    cmp dx, 256
    jae .walk
    mov ax, di
    inc ax
    jz .walk
    cmp ax, [w16_clast + bx]        ; p + 2 <= the length
    ja .walk
    mov cx, 1
    xor al, al
    call w16c_bits
    jc .walk                        ; p is code already
    mov es, [w16_sbase + bx]
    mov ax, [es:di]                 ; e
    cmp ax, [w16_clast + bx]
    ja .walk
    cmp ax, bp                      ; t <= e < p + 2: into the table itself
    jb .take
    mov cx, di
    add cx, 2
    cmp ax, cx
    jb .walk
.take:
    mov cx, 2
    push ax
    mov al, 1
    call w16c_bits                  ; the entry is data
    pop ax
    call w16c_push
    add di, 2
    inc dx
    jmp short .entry
.walk:
    call w16c_run
    jmp short .next
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16c_prologs - the dark bytes of every code segment, for a function
; prologue (os88ne.py's PROLOGUES): each one found is walked at once. A byte
; of bitmap that is all ones is eight offsets skipped in one compare.
; -----------------------------------------------------------------------------
w16c_prologs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor bx, bx
.seg:
    cmp word [w16_cbm + bx], 0xFFFF
    je .nextseg
    xor di, di                      ; DI = o
.o:
    cmp byte [w16c_ov], 0
    jne .done
    test di, 7                      ; a whole byte of the bitmap taken?
    jnz .bit
    mov si, di
    mov cl, 3
    shr si, cl
    add si, [w16_cbm + bx]
    mov es, [w16c_cseg]
    cmp byte [es:si], 0xFF
    jne .bit
    add di, 8
    jc .nextseg
    jmp short .bound
.bit:
    mov cx, 1
    xor al, al
    call w16c_bits
    jc .step
    mov es, [w16_sbase + bx]
    mov si, w16c_pro                ; each pattern: its length, its bytes
.pat:
    xor cx, cx
    mov cl, [si]
    jcxz .step
    inc si
    mov ax, di                      ; inside the segment?
    add ax, cx
    jc .skip
    dec ax
    cmp ax, [w16_clast + bx]
    ja .skip
    push si
    push di
    push cx
.cmp:
    mov al, [es:di]
    cmp al, [si]
    jne .miss
    inc si
    inc di
    loop .cmp
    pop cx                          ; (not ZF: the `inc` above moved it)
    pop di
    pop si
    jmp short .match
.miss:
    pop cx
    pop di
    pop si
    jmp short .skip
.match:
    xor al, al                      ; ...and none of it code already
    call w16c_bits
    jc .skip
    mov [w16c_cur], bx
    mov ax, di
    call w16c_push
    call w16c_run
    jmp short .step
.skip:
    add si, cx
    jmp short .pat
.step:
    inc di
    jz .nextseg
.bound:
    cmp di, [w16_clast + bx]
    jbe .o
.nextseg:
    inc bx
    inc bx
    mov ax, bx
    shr ax, 1
    cmp ax, [w16_nseg]
    jb .seg
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

w16c_pro:                           ; os88ne.py's PROLOGUES, in its order
    db 3, 0x1E, 0x58, 0x90          ; push ds / pop ax / nop   (exported, MS C)
    db 3, 0x8C, 0xD8, 0x90          ; mov ax, ds / nop         (Borland)
    db 4, 0x45, 0x55, 0x8B, 0xEC    ; inc bp / push bp / mov bp, sp
    db 4, 0x45, 0x55, 0x89, 0xE5    ; ...Open Watcom's encoding
    db 3, 0x55, 0x8B, 0xEC          ; push bp / mov bp, sp
    db 3, 0x55, 0x89, 0xE5
    db 0

; -----------------------------------------------------------------------------
; w16c_tierof - AL = the highest tier reached outside every guarded range
; -----------------------------------------------------------------------------
w16c_tierof:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [w16c_cseg]
    mov dl, [w16c_tov]              ; what the list could not hold counts
    mov cx, [w16c_ni]
    mov si, [w16c_obase]
    add si, W16C_TIRO
    jcxz .done
.rec:
    mov bx, [es:si]                 ; the segment * 2
    mov ax, [es:si + 2]             ; the offset
    push cx
    mov cx, [w16c_ng]
    mov di, [w16c_obase]
    add di, W16C_GRDO
    jcxz .counts
.g:
    cmp [es:di], bx
    jne .gnext
    cmp ax, [es:di + 2]
    jb .gnext
    cmp ax, [es:di + 4]
    jb .guarded
.gnext:
    add di, 6
    loop .g
.counts:
    mov al, [es:si + 4]
    cmp al, dl
    jbe .guarded
    mov dl, al
.guarded:
    pop cx
    add si, 6
    loop .rec
.done:
    mov al, dl
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- the census's own scratch, beside the loader's ----------------------------
w16c_cseg:  dw 0                    ; the walk's claim
w16c_obase: dw 0                    ; ...where its lists start, past the bitmaps
w16c_n:     dw 0                    ; work stack depth
w16c_nt:    dw 0                    ; pending tables
w16c_ng:    dw 0                    ; guarded ranges
w16c_ni:    dw 0                    ; tiered instructions recorded
w16c_cur:   dw 0                    ; the segment being walked, * 2
w16c_cs:    dw 0                    ; ...its paragraph
w16c_last:  dw 0                    ; ...its last offset
w16c_h1:    dw 0                    ; the last three instructions' starts
w16c_h2:    dw 0
w16c_h3:    dw 0
w16c_ov:    db 0                    ; 1 = a list ran out: refuse
w16c_tov:   db 0                    ; the highest tier the list could not hold
w16c_sweep: db 0                    ; 1 = the prologue sweep is running
w16c_cls:   db 0                    ; the decoder's answers
w16c_op:    db 0
w16c_int:   db 0

w16_r_c186: db 'Needs a V20, 80186 or better.', 0
w16_r_c286: db 'Needs an 80286 or better.', 0
w16_r_c386: db 'Needs an 80386 or better.', 0
w16_r_cbig: db 'Too large to check on an 8088.', 0
