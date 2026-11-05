; =============================================================================
; os8088 - apps/c64/c64jit.asm         THE RECOMPILER (C64-SPEC section 4.9)
;
; Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
;
; A PART OF C64.O88 (OP_SEG): the package copies it into a 64KB claim of its
; own - this code, apps/m6502/m65tmpl.py's templates, and past them the
; lookup table, the page tables and the code cache. Straight-line RAM code
; is translated here into that cache, a template an instruction, and run
; there; the fast core (m65gen.py) far-jumps in at a RAM control transfer
; through JIT_HOOK when its page map says so, and every block leaves back
; into the core through the prelude's doors (xa_resume, xa_slice).
;
; THE SEGMENT, BY OFFSET:
;   $0000  the CODE map (32 bytes, a bit a page) and the flush request -
;          m65gen.py's JC_CODE and JC_FLUSH, which the cores' marks test
;   $0022  this file's variables
;   $00F0  the far vectors: JIT_HOOK (the core's door), then jl_init
;   $0100  the prelude (m65tmpl.py), whose last instruction is xa_go's
;          `jle xa_slice` - falling through into jl_go, here
;          ...the translator, then the templates' part
;   LT     the lookup table: 1,024 rows of [PC, value] - a value below
;          $0100 is a count of entries, $0100 is "never", and from CACHE up
;          it is the block's offset
;   JPG    a byte a page: 1 where blocks may start, 0 where they may not
;          (ROM, I/O, the stack, pages 2 and 3 - section 4.9.2 - and every
;          page made VOLATILE)
;   FC     a byte a page: the flushes its writes have caused
;   CACHE  the blocks, to CACHE_END
;
; THE CONVENTIONS are the core's (m65gen.py's header): A = AL, N Z C in AH,
; X = CL, Y = CH, S = DI, BP the countdown, DS the guest's 64KB, SP = F the
; frame - and SI the PC on the way in and out. Everything here that is not a
; block runs with the 6502's registers saved and put back.
; =============================================================================
cpu 8086
bits 16
org 0

%include "c64tmpl.inc"

LT          equ 0x5800              ; 1,024 x 4 bytes
JPG         equ 0x6800
FC          equ 0x6900
FINE        equ JC_FINE             ; $0000-$CFFF a bit a byte: set under every
                                    ; byte a block was made from (4.9.3)
PATCHED     equ FINE + MAPLEN       ; ...and under every byte a write found
                                    ; there - kept across flushes (4.9.3)
MAPLEN      equ 0xD000 / 8
CACHE       equ PATCHED + MAPLEN
CACHE_END   equ 0xFD00                ; (in a 64KB claim; the last 768 bytes
                                    ; are the D64's two sector slots, C64-SPEC
                                    ; 11.5.1, and room - kept out of the cache)
    times (CACHE != 0x9E00) * -1 db 0   ; (c64.c's C64J_CACHE_PARA)
BLOCK_MAX   equ 34 * JT_MAXT + 16   ; HEAD, 32 instructions, the exit
BLOCK_LEN   equ 32                  ; instructions a block
VOLATILE_N  equ 16                  ; flushes a page may cause (4.9.3)
THRESH      equ 64                  ; a block's entries before it is made
PEND_N      equ 256                 ; exits waiting for their target's block
PEND_BYTES  equ PEND_N * 4          ; (4.9.4: [PC, site], under jv_end)

; --- the variables -----------------------------------------------------------
jv_ptr      equ 0x22                ; the cache's next free byte
jv_thresh   equ 0x24                ; THRESH, or 1 when forced
jv_force    equ 0x25                ; 1: every page looked at, every entry made
jv_vals     equ 0x70                ; a word per site kind, JK_OPB..JK_OPA (to $85)
jv_costat   equ 0x3A                ; where HEAD's COST site landed
jv_n        equ 0x3C                ; instructions in the block so far
jv_fp       equ 0x3E                ; F, the frame, for the core's segment
jv_start    equ 0x40                ; the block's offset
jv_bpc      equ 0x42                ; ...and its PC
jv_ntrans   equ 0x44                ; blocks made (the status's counts)
jv_nflush   equ 0x46                ; flushes
jv_nvol     equ 0x48                ; pages made volatile
jv_pend     equ 0x4A                ; cycles pure templates left to the sum
jv_chtg     equ 0x4C                ; the last template's chainable jumps
jv_chnx     equ 0x4E
jv_prevk    equ 0x50
jv_end      equ 0x52                ; where the cache stops: the claim's size
jv_dyn      equ 0x54                ; this instruction's PATCHED bytes: 0 none,
                                    ; 1 an operand's, 2 its opcode's
jv_pendt    equ 0x56                ; the pending exits' table (4.9.4)
jv_pendi    equ 0x58                ; ...and its next row, a ring

    ; (every variable below JIT_HOOK; jv_vals' last word is JK_OPA's)
    times (jv_vals + 2 * JK_OPA + 2 > JIT_HOOK) * -1 db 0
    times JIT_HOOK - ($-$$) db 0
jv_vhook:
    jmp near jl_hook                ; JIT_HOOK - the core's door
jv_vinit:
    jmp near jl_init                ; JIT_HOOK + 3 - FAR, the package's
    dw jt_blob                      ; JIT_HOOK + 6 - where the templates' part
                                    ; is (c64cputest's controls patch it)

    times JC_PRE - ($-$$) db 0
jt_pre:
    incbin "c64tmpl.bin", JT_PRE_OFF, JT_PRE_LEN
    ; (the prelude ends `xa_go: jle xa_slice`: a transfer with the budget
    ; left falls into the lookup)

; -----------------------------------------------------------------------------
; jl_go / jl_hook - SI is a PC control goes to, BP above 0. Its block when it
; has one; else its count goes up by DH and, past the threshold, the block
; is made and entered; else the core runs it.
; -----------------------------------------------------------------------------
jl_go:
    mov dh, JIT_W_BLOCK
jl_hook:
%ifdef JTRACE
    ; DEBUG ONLY: SI and BP of every lookup into a ring at $00A0
    push bx
    mov bx, [cs:0x9E]
    mov [cs:bx+0xA0], si
    mov [cs:bx+0xA2], bp
    add bx, 4
    and bx, 0x3F
    mov [cs:0x9E], bx
    pop bx
%endif
    cmp byte [cs:JC_FLUSH+1], 0
    jne .flush
.look:
    mov bx, si
    and bx, 0x03FF
    shl bx, 1
    shl bx, 1
    cmp [cs:bx+LT], si
    jne .count
    mov bx, [cs:bx+LT+2]
    cmp bx, CACHE
    jb .count
    xor dl, dl
    jmp bx
.flush:
    mov [cs:jv_fp], sp
    call jt_flush
    jmp short .look
.count:
    mov dl, dh
    xor dh, dh
    mov bx, si
    and bx, 0x03FF
    shl bx, 1
    shl bx, 1
    cmp [cs:bx+LT], si
    je .have
    mov [cs:bx+LT], si              ; (another PC's row: it gives way)
    mov word [cs:bx+LT+2], 0
.have:
    cmp word [cs:bx+LT+2], 0x0100
    jae .core                       ; never
    add [cs:bx+LT+2], dx
    mov dl, [cs:jv_thresh]
    cmp [cs:bx+LT+2], dx
    jb .core
    mov [cs:jv_fp], sp
    push bx
    call jt_trans                   ; -> BX = the block, 0 for none
    mov dx, bx
    pop bx
    mov [cs:bx+LT], si              ; (a flush in there emptied the row)
    or dx, dx
    jz .never
    mov [cs:bx+LT+2], dx
    mov bx, dx
    xor dl, dl
    jmp bx
.never:
    mov word [cs:bx+LT+2], 0x0100
.core:
    xor dl, dl
    jmp JP_xa_resume

; -----------------------------------------------------------------------------
; jt_flush - every block gone (C64-SPEC 4.9.3): the lookup table, the CODE
; map and the cache's pointer; the page a write named counted, and VOLATILE
; once it has asked VOLATILE_N times; the core's page map rebuilt from JPG.
; jv_fp is the frame. Preserves everything.
; -----------------------------------------------------------------------------
jt_flush:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cld
    push cs
    pop es
    inc word [cs:jv_nflush]
    cmp byte [cs:JC_FLUSH+1], 1
    jne .nob
    ; THE BYTE IS PATCHED from now on: an instruction whose operand it is
    ; reads it where it lies, and one whose opcode it is stays the core's
    mov bx, [cs:JC_HITA]
    cmp bh, 0xD0
    jae .np
    mov cl, bl
    and cl, 7
    mov al, 0x80
    shr al, cl
    shr bx, 1
    shr bx, 1
    shr bx, 1
    or [cs:bx+PATCHED], al
.np:
    mov bl, [cs:JC_FLUSH]
    xor bh, bh
    inc byte [cs:bx+FC]
    cmp byte [cs:bx+FC], VOLATILE_N
    jb .nob
    cmp byte [cs:bx+JPG], 0
    je .nob
    mov byte [cs:bx+JPG], 0         ; VOLATILE: the core's for good
    inc word [cs:jv_nvol]
.nob:
    ; THE BYTE MAP, only where the CODE map says a page has blocks: 32
    ; bytes a page, not the whole 6.5KB
    xor bx, bx
.fp:
    mov al, bl
    mov cl, 3
    shr al, cl                      ; (the byte of the CODE map)
    mov cl, bl
    and cl, 7
    mov ah, 0x80
    shr ah, cl
    push bx
    mov bl, al
    xor bh, bh
    test [cs:bx+JC_CODE], ah
    pop bx
    jz .fn
    mov di, bx
    mov cl, 5
    shl di, cl
    add di, FINE
    xor ax, ax
    mov cx, 16
    rep stosw
.fn:
    inc bx
    cmp bx, 0xD0
    jb .fp
    xor ax, ax
    mov di, LT
    mov cx, 0x0800
    rep stosw
    mov di, JC_CODE
    mov cx, 16
    rep stosw
    mov [cs:JC_FLUSH], ax
    mov word [cs:jv_ptr], CACHE
    mov di, [cs:jv_pendt]           ; ...and every waiting exit with them
    mov cx, PEND_BYTES / 2
    rep stosw
    mov [cs:jv_pendi], ax
    mov bx, [cs:jv_fp]
    mov es, [ss:bx+F_CORE]
    mov si, JPG
    mov di, PGMAP
    mov cx, 0xD0
    mov ah, [cs:jv_force]
.p:
    mov al, [cs:si]
    inc si
    or ah, ah
    jz .w
    ror al, 1                       ; (forced: 1 is $80, looked at always)
.w:
    stosb
    loop .p
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; jt_trans - the block at SI (C64-SPEC 4.9.2), into the cache: HEAD, then a
; template an instruction until one ends the block, one is the core's, a
; page is not ours, or BLOCK_LEN; the worst case into HEAD; the pages it
; covers into the CODE map, and its first page into the core's map. -> BX
; = its offset, or 0 when its first instruction is not ours to run.
; jv_fp is the frame. Preserves everything else.
; -----------------------------------------------------------------------------
jt_trans:
    push ax
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cld
    push cs
    pop es
    mov di, [cs:jv_end]
    sub di, BLOCK_MAX
    cmp [cs:jv_ptr], di
    mov di, [cs:jv_ptr]
    jbe .room
    mov byte [cs:JC_FLUSH+1], 2     ; full: a flush no page is blamed for
    call jt_flush
    mov di, [cs:jv_ptr]
.room:
    mov [cs:jv_start], di
    mov [cs:jv_bpc], si
    mov word [cs:jv_pend], 0
    mov [cs:jv_vals+2*JK_PC], si
    mov bx, [cs:jt_blob+JT_HEAD]
    call jt_emit
    xor bp, bp                      ; the worst case
    mov byte [cs:jv_n], 0
.ins:
    cmp byte [cs:jv_n], BLOCK_LEN
    jb .more
    jmp .go
.more:
    mov bx, si
    mov bl, bh
    xor bh, bh
    cmp byte [cs:bx+JPG], 0
    je .core1
    mov bl, [si]                    ; the opcode
    xor bh, bh
    mov cl, 4
    shl bx, cl                      ; (JT_ROW)
    add bx, [cs:jt_blob+JT_OPTAB]
    add bx, jt_blob                 ; its row: length, kind, rule, cycles,
    cmp byte [cs:bx+1], KD_CORE     ; its two templates
    je .core1
    mov cl, [cs:bx]
    xor ch, ch
    mov dx, si
    add dx, cx                      ; NEXT
    push bx
    mov bx, dx
    dec bx                          ; its last byte must be ours too
    mov bl, bh
    xor bh, bh
    cmp byte [cs:bx+JPG], 0
    pop bx
    je .core1
    ; THE SITES' VALUES for this instruction
    mov [cs:jv_vals+2*JK_PC], si
    mov [cs:jv_vals+2*JK_NEXT], dx
    mov ax, si
    inc ax
    mov [cs:jv_vals+2*JK_OPA], ax
    mov ax, si
    add ax, 2
    mov [cs:jv_vals+2*JK_RET], ax
    mov ax, [si+1]
    cmp cl, 3
    je .w
    xor ah, ah                      ; a byte operand, as a word
.w:
    mov [cs:jv_vals+2*JK_OPW], ax
    mov [cs:jv_vals+2*JK_OPB], ax
    mov [cs:jv_vals+2*JK_TARGET], ax
    cmp byte [cs:bx+1], KD_TERM
    jne .nb
    cmp cl, 2
    jne .nb
    cbw                             ; A BRANCH: NEXT + the offset, and 4
    add ax, dx                      ; cycles taken across a page, 3 inside
    mov [cs:jv_vals+2*JK_TARGET], ax
    xor ax, dx
    mov al, 3
    or ah, ah
    jz .tc
    inc al
.tc:
    mov [cs:jv_vals+2*JK_TCYC], al
.nb:
    ; WHAT STAYS THE CORE'S BY POLICY. A read of $D011, $D012 or $D019 is a
    ; raster wait the core runs in whole passes (4.7.1), and only there; a
    ; DEX, DEY or DEC abs with a BNE back onto itself is a delay loop the
    ; core runs in one step (4.7.2)
    cmp byte [cs:bx+2], SEL_ABSRD
    jne .np
    mov ax, [cs:jv_vals+2*JK_OPW]
    cmp ax, 0xD011
    je .core1
    cmp ax, 0xD012
    je .core1
    cmp ax, 0xD019
    je .core1
.np:
    mov al, [si]
    cmp al, 0xCA
    je .dx
    cmp al, 0x88
    je .dx
    cmp al, 0xCE
    jne .nl
    cmp word [si+3], 0xFBD0
    je .core1
    jmp short .nl
.dx:
    cmp word [si+1], 0xFDD0
    jne .nl
.core1:
    jmp .core
.nl:
    ; PATCHED BYTES (4.9.3): the opcode, and the instruction is the core's;
    ; an operand byte, and variant 2 reads the operand where it lies
    mov byte [cs:jv_dyn], 0
%ifdef NODYN
    jmp .stat
%endif
    mov cl, [cs:bx]
    xor ch, ch
    push bx
    push si
.pt:
    mov bx, si
    mov al, bl
    and al, 7
    push cx
    mov cl, al
    mov al, 0x80
    shr al, cl
    pop cx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    test [cs:bx+PATCHED], al
    jz .pn
    mov byte [cs:jv_dyn], 1
    cmp si, [cs:jv_vals+2*JK_PC]
    jne .pn
    mov byte [cs:jv_dyn], 2         ; (the opcode itself)
    jmp short .pe
.pn:
    inc si
    loop .pt
.pe:
    pop si
    pop bx
    cmp byte [cs:jv_dyn], 2
    je .core1
    cmp byte [cs:jv_dyn], 0
    je .stat
    mov dx, [cs:bx+8]               ; variant 2, or the core's
    or dx, dx
    jz .core1
    jmp short .t0
.stat:
    ; THE TEMPLATE: variant 1 where the operand proves it (jt_var)
    mov dl, [cs:bx+2]
    mov ax, [cs:jv_vals+2*JK_OPW]
    call jt_var
    mov dx, [cs:bx+4]
    jnc .t0
    cmp word [cs:bx+6], 0
    je .t0
    mov dx, [cs:bx+6]
.t0:
    push bx
    mov bx, dx
    call jt_emit
    pop bx
    mov al, [cs:bx+3]
    xor ah, ah
    add bp, ax                      ; its worst case
    inc byte [cs:jv_n]
    push bx                         ; its bytes into the byte map - but
    push si                         ; for variant 2 only its opcode: the
    mov cl, [cs:bx]                 ; operand is read where it lies, and
    xor ch, ch                      ; writing it is no change to the code
    cmp byte [cs:jv_dyn], 0
    je .fb
    mov cl, 1
.fb:
    mov bx, si
    mov al, bl
    and al, 7
    push cx
    mov cl, al
    mov al, 0x80
    shr al, cl
    pop cx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    or [cs:bx+FINE], al
    inc si
    loop .fb
    pop si
    pop bx
    mov cl, [cs:bx]
    xor ch, ch
    add si, cx
    cmp byte [cs:bx+1], KD_TERM
    je .term
    jmp .ins
.term:                              ; ITS EXITS CHAINED where they can be
%ifdef NOCHAIN
    jmp short .done
%endif
    push si
    mov si, [cs:jv_vals+2*JK_TARGET]
    mov bx, [cs:jv_chtg]
    call jt_chain
    mov si, [cs:jv_vals+2*JK_NEXT]
    mov bx, [cs:jv_chnx]
    call jt_chain
    pop si
    jmp short .done
.go:                                ; the length: on at SI, through the lookup
    mov [cs:jv_vals+2*JK_PC], si
    mov bx, [cs:jt_blob+JT_XGO]
    call jt_emit
    jmp short .done
.core:                              ; an instruction the core runs
    cmp byte [cs:jv_n], 0
    je .none
    mov [cs:jv_vals+2*JK_PC], si
    mov bx, [cs:jt_blob+JT_XCORE]
    call jt_emit
.done:
    mov bx, [cs:jv_costat]
    mov [cs:bx], bp                 ; HEAD's worst case
    ; ITS PAGES: every one its bytes cover into the CODE map, and the first
    ; into the core's page map as one where blocks start
    mov bx, [cs:jv_fp]
    mov es, [ss:bx+F_CORE]
    mov ax, [cs:jv_bpc]
    mov dx, si
    dec dx
    mov bl, ah
    xor bh, bh
    mov byte [es:bx+PGMAP], 0x80
    mov al, ah
.pg:
    mov bl, al
    xor bh, bh
    mov cl, al
    and cl, 7
    mov ch, 0x80
    shr ch, cl
    shr bl, 1
    shr bl, 1
    shr bl, 1
    or [cs:bx+JC_CODE], ch
    cmp al, dh
    je .pgd
    inc al
    jmp short .pg
.pgd:
    ; (the marks' same-page short cut is broken, so the next write to a page
    ; takes the path that tests the CODE map - m65gen.py's c64_mark)
    mov byte [C64SCR + S_ANY], 1
    mov [cs:jv_ptr], di
    inc word [cs:jv_ntrans]
%ifndef NOLINK
    call jt_link                    ; the exits that were waiting for it
%endif
    mov bx, [cs:jv_start]
    add bx, JT_HENTRY               ; (entered past HEAD's exit)
    jmp short .out
.none:
    xor bx, bx
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; jt_emit - the template record BX (an offset in the templates' part) to
; ES:DI, its sites patched from jv_vals; DI past it. COST's site is noted in
; jv_costat, and a REL site - a near jump or call into the prelude, stored as
; if the template began at 0 - has the template's offset taken off.
; Uses AX, BX, CX, DX.
; -----------------------------------------------------------------------------
jt_emit:
    push si
    add bx, jt_blob
    mov si, [cs:bx]
    add si, jt_blob
    mov cl, [cs:bx+2]
    xor ch, ch
    mov word [cs:jv_chtg], 0
    mov word [cs:jv_chnx], 0
%ifdef NOBATCH
    jmp short .paid
%endif
    test byte [cs:bx+3], 0x80
    jz .paid
    ; A PURE TEMPLATE (m65tmpl.py's pure()): its last three bytes are
    ; `sub bp, cycles`, which go into the sum instead (4.9.4)
    sub cx, 3
    push bx
    mov bx, si
    add bx, cx
    mov al, [cs:bx+2]
    pop bx
    xor ah, ah
    add [cs:jv_pend], ax
    jmp short .cp0
.paid:
    call jt_pay
.cp0:
    mov dx, di                      ; where the template is
    jcxz .cpd                       ; (NOP, batched, is no bytes at all)
.cp:
    mov al, [cs:si]
    inc si
    stosb
    loop .cp
.cpd:
    mov cl, [cs:bx+3]
    and cl, 0x7F
    lea si, [bx+4]
    mov byte [cs:jv_prevk], 0       ; the kind of the site before
    jcxz .done
.s:
    mov bl, [cs:si]
    xor bh, bh
    add bx, dx                      ; the site
    mov al, [cs:si+1]               ; its kind
    add si, 2
    cmp al, JK_REL
    je .rel
    cmp al, JK_COST
    jne .v
    mov [cs:jv_costat], bx
.v:
    xor ah, ah
    shl ax, 1
    push si
    mov si, ax
    mov ax, [cs:si+jv_vals]
    pop si
    cmp byte [cs:si-1], JK_OPB
    je .b
    cmp byte [cs:si-1], JK_TCYC
    je .b
    mov [es:bx], ax
    jmp short .nx
.b:
    mov [es:bx], al
    jmp short .nx
.rel:
    sub [es:bx], dx
    ; THE JUMPS A CHAIN MAY TAKE (4.9.4): the near jump after TARGET is
    ; the taken one, after NEXT the one that falls through
    cmp byte [cs:jv_prevk], JK_TARGET
    jne .r1
    mov [cs:jv_chtg], bx
.r1:
    cmp byte [cs:jv_prevk], JK_NEXT
    jne .nx
    mov [cs:jv_chnx], bx
.nx:
    mov al, [cs:si-1]
    mov [cs:jv_prevk], al
    loop .s
.done:
    pop si
    ret

; jt_pay - the cycles pure templates left to the sum, as one `sub bp, n`
; before whatever comes next can leave or be tested. Uses AX.
jt_pay:
    mov ax, [cs:jv_pend]
    or ax, ax
    jz .r
    push ax
    mov ax, 0xED81                  ; sub bp, imm16
    stosw
    pop ax
    stosw
    mov word [cs:jv_pend], 0
.r:
    ret

; jt_chain - SI = a 6502 PC, BX = a near jump's rel16 in the cache that goes
; to xa_go with SI that PC: made a jump straight into the block for that PC
; when there is one - this block itself, or one already made. The target's
; HEAD tests the budget, spent or short, as xa_go would have (4.9.4).
; Uses AX, DX.
jt_chain:
    or bx, bx
    jz .r
    mov ax, [cs:jv_start]
    add ax, JT_HENTRY
    cmp si, [cs:jv_bpc]
    je .go
    push bx
    mov bx, si
    and bx, 0x03FF
    shl bx, 1
    shl bx, 1
    cmp [cs:bx+LT], si
    mov ax, [cs:bx+LT+2]
    pop bx
    jne .pend
    cmp ax, CACHE
    jb .pend
.go:
    sub ax, bx
    sub ax, 2
    mov [es:bx], ax
.r:
    ret
    ; NO BLOCK FOR IT YET: the site waits in the ring for one (4.9.4).
    ; Chaining only to blocks already made left a loop whose blocks were made
    ; in the wrong order going through the lookup at every branch for good -
    ; PCUG's snow shift, 8% of a 286 in jl_go
.pend:
    push di
    mov di, [cs:jv_pendi]
    add di, [cs:jv_pendt]
    mov [cs:di], si
    mov [cs:di+2], bx
    pop di
    mov ax, [cs:jv_pendi]
    add ax, 4
    and ax, PEND_BYTES - 1
    mov [cs:jv_pendi], ax
    ret

; jt_link - the block just made (jv_bpc, jv_start): every exit waiting in the
; ring for its PC jumps straight into it now, as jt_chain would have had it
; been made first. Preserves everything.
jt_link:
    push ax
    push cx
    push dx
    push si
    push di
    mov dx, [cs:jv_bpc]
    mov si, [cs:jv_pendt]
    mov cx, PEND_N
.l:
    cmp [cs:si], dx
    jne .n
    mov di, [cs:si+2]
    or di, di
    jz .n
    mov ax, [cs:jv_start]
    add ax, JT_HENTRY
    sub ax, di
    sub ax, 2
    mov [cs:di], ax
    mov word [cs:si+2], 0
.n:
    add si, 4
    loop .l
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; jt_var - CF set when the rule DL lets the operand AX take variant 1:
; m65tmpl.py's variant(), and its self-test holds both templates to it
; -----------------------------------------------------------------------------
jt_var:
    cmp dl, SEL_ABSRD
    je .ram
    cmp dl, SEL_ABSWR
    je .wr
    cmp dl, SEL_ABSRMW
    je .rmw
    cmp dl, SEL_IDXRD
    je .iram
    cmp dl, SEL_IDXWR
    je .iwr
    cmp dl, SEL_IDXRMW
    je .irmw
    cmp dl, SEL_ZPWR
    je .ge2
    cmp dl, SEL_INDY
    je .indy
.no:
    clc
    ret
.yes:
    stc
    ret
.rmw:
    cmp ax, 2
    jb .no
.ram:                               ; below $A000, or $C000-$CFFF
    cmp ah, 0xA0
    jb .yes
    cmp ah, 0xC0
    jb .no
    cmp ah, 0xD0
    jb .yes
    jmp short .no
.wr:
    cmp ax, 2
    jb .no
    cmp ah, 0xD0
    jb .yes
    jmp short .no
.irmw:
    cmp ax, 2
    jb .no
.iram:                              ; all 256 bytes of its reach such RAM
    cmp ax, 0xA000 - 255
    jb .yes
    cmp ax, 0xC000
    jb .no
    cmp ax, 0xD000 - 255
    jb .yes
    jmp short .no
.iwr:
    cmp ax, 2
    jb .no
    cmp ax, 0xD000 - 255
    jb .yes
    jmp short .no
.ge2:
    cmp ax, 2
    jb .no
    jmp short .yes
.indy:
    cmp al, 2
    jb .no
    cmp al, 0xFF
    je .no
    jmp short .yes

; -----------------------------------------------------------------------------
; jl_init - FAR, once, from the package (apps/c64/c64fast.inc's c64j_setup):
; AX = the fast core's segment, whose page map is written here; DH = the
; claim in KB (64, or less where the heap had less: the cache ends sooner);
; DL = 1 to FORCE - every RAM page looked at on every transfer, a block made on its
; first entry: c64cputest's recompiler leg
; -----------------------------------------------------------------------------
jl_init:
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    push es
    cld
    mov bx, ax
    push cs
    pop ds
    push cs
    pop es
    xor ax, ax
    mov di, JC_CODE
    mov cx, (JIT_HOOK - JC_CODE) / 2
    rep stosw
    mov di, LT
    mov cx, (CACHE - LT) / 2
    rep stosw
    mov al, 1
    mov di, JPG + 0x04              ; $0400-$9FFF and $C000-$CFFF
    mov cx, 0xA0 - 0x04
    rep stosb
    mov di, JPG + 0xC0
    mov cx, 0x10
    rep stosb
    mov word [jv_ptr], CACHE
    mov word [jv_end], CACHE_END
    cmp dh, 64
    jae .big
    mov al, dh
    xor ah, ah
    mov cl, 10
    shl ax, cl
    sub ax, 0x300                   ; (CACHE_END's reserve)
    mov [jv_end], ax
.big:
    sub word [jv_end], PEND_BYTES   ; the pending exits' ring, under the end
    mov ax, [jv_end]
    mov [jv_pendt], ax
    mov word [jv_pendi], 0
    mov di, ax
    mov cx, PEND_BYTES / 2
    xor ax, ax
    rep stosw
    mov [jv_force], dl
    mov byte [jv_thresh], THRESH
    or dl, dl
    jz .nf
    mov byte [jv_thresh], 1
.nf:
    mov es, bx
    mov si, JPG
    mov di, PGMAP
    mov cx, 0xD0
.p:
    lodsb
    or dl, dl
    jz .w
    ror al, 1
.w:
    stosb
    loop .p
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; THE TEMPLATES' PART (m65tmpl.py): the header, the prelude again, the
; opcode table and every template record. Offsets in it are its own.
; -----------------------------------------------------------------------------
jt_blob:
    incbin "c64tmpl.bin"
jt_end:
    times LT - ($-$$) db 0          ; (the image ends below the tables: a
                                    ;  negative TIMES is the assembly error)
