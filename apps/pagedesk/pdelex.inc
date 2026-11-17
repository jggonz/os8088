; =============================================================================
; os8088 - apps/pagedesk/pdelex.inc                  PDENG's lexer, the hot loop
;
; The character classes and the word scan of pdelex.c (PAGEDESK-SPEC 7.1,
; 20.2), in 8086 assembly, included by pdeng.asm after the compiled C. Their
; C twins are pdelex.c's PD_HOST branch, which every host harness runs.
;
; Measured on MartyPC (5150, 4.77 MHz) before this file: a character of a
; number cost ~1,700 cycles through lx_peek, is_ws and is_delim - six and
; ten compares each in SmallerC, a near call apiece - which was 40% of
; drawing HELLO.PDF. A class here is two table reads and a test.
;
;   int  is_ws(int c)       1 for NUL, TAB, LF, FF, CR, space (PDF 7.2.2)
;   int  is_delim(int c)    1 for ( ) < > [ ] { } / %
;   pdw  lx_run(const pdb *src, pdw left, pdb *dst, pdw cap, pdw stop)
;        consume src while its bytes are neither white space nor a
;        delimiter nor `stop` ('#' for a name, 0 for a word), at most `left`
;        of them; copy the first `cap` consumed to dst. Answers how many
;        were consumed.
;   pdw  lx_num(const pdb *t, pdw n, struct pd_u32 *iv)
;        t[0..n) as a PDF number - [+-], digits, one '.' at most, a digit
;        somewhere: 0 = not one, else 1 | 2 (a '.') | 4 (the integer part
;        is 2^31 or more; *iv saturates at 0xFFFFFFFF). *iv = the integer
;        part, written only for a number.
;
; cdecl as the C calls them (SPEC.md 73.3): arguments on the stack, AX out,
; BP preserved and the rest the caller's to lose (pdmath.inc's rule); DS = the
; module; ES is the caller's and is put back.
; =============================================================================

section .rodata
; a bit per character 0..127, character c in byte c >> 3, bit c & 7;
; 128..255 are in no class
pde_wsmap:  db 0x01, 0x36, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00   ; 0 9 10 12 13 / 32
            db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
pde_dlmap:  db 0x00, 0x00, 0x00, 0x00, 0x20, 0x83, 0x00, 0x50   ; % ( ) / < >
            db 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x28   ; [ ] { }
pde_tmap:   db 0x01, 0x36, 0x00, 0x00, 0x21, 0x83, 0x00, 0x50   ; the two together
            db 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x28
pde_bit:    db 1, 2, 4, 8, 16, 32, 64, 128
section .text

_is_ws:
    mov bx, pde_wsmap
    jmp short pde_inmap
_is_delim:
    mov bx, pde_dlmap
pde_inmap:                          ; BX = the map, [SP+2] = c
    pop dx                          ; the return address
    pop ax                          ; c
    push ax
    push dx
pde_cls:                            ; AX = c, BX = a map -> AX = 0/1, ZF = !AX
    cmp ax, 128
    jae .no                         ; also -1 (EOF) and 128..255
    mov cx, ax
    and cl, 7
    shr al, 1
    shr al, 1
    shr al, 1
    xlatb                           ; AL = map[c >> 3]
    shr al, cl
    and ax, 1                       ; AH is 0: c < 128
    ret
.no:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; the lexer's buffer (pdelex.c, struct lxs in pde.h: buf +0, n +2, i +4,
; mode +6, fpos +8, smode +14 - THOSE OFFSETS ARE THIS FILE'S, and so is
; struct pde_val's text at +22); _L is the current lexer
; -----------------------------------------------------------------------------
LXS_BUF     equ 0
LXS_N       equ 2
LXS_I       equ 4
LXS_MODE    equ 6
LXS_FPOS    equ 8
LXS_SMODE   equ 14
PDV_TEXT    equ 22                  ; struct pde_val's text[] (pde.h)

_lx_num:
    push bp
    mov bp, sp
    mov si, [bp + 4]                ; t
    mov cx, [bp + 6]
    add cx, si                      ; its end
    xor bx, bx                      ; DI:BX = the integer part
    xor di, di
    xor dx, dx                      ; DL: 1 a digit, 2 a '.', 4 saturated
    cmp si, cx
    je .no
    mov al, [si]
    cmp al, '+'
    je .sign
    cmp al, '-'
    jne .next
.sign:
    inc si
.next:
    cmp si, cx
    jae .end
    lodsb
    cmp al, '.'
    jne .dig
    test dl, 2
    jnz .no                         ; a second '.'
    or dl, 2
    jmp short .next
.dig:
    sub al, '0'
    cmp al, 9
    ja .no
    or dl, 1
    test dl, 6
    jnz .next                       ; a fraction digit, or saturated
    push dx
    push cx
    xor ah, ah
    mov cx, ax                      ; the digit
    mov ax, 10
    mul di
    test dx, dx
    jnz .sat
    mov di, ax                      ; hi * 10
    mov ax, 10
    mul bx                          ; DX:AX = lo * 10
    add ax, cx
    adc dx, 0
    mov bx, ax
    add di, dx
    jc .sat
    pop cx
    pop dx
    jmp short .next
.sat:
    pop cx
    pop dx
    or dl, 4
    mov bx, 0xFFFF
    mov di, bx
    jmp short .next
.end:
    test dl, 1
    jz .no
    cmp di, 0x8000
    jb .put
    or dl, 4
.put:
    mov si, [bp + 8]
    mov [si], bx
    mov [si + 2], di
    mov al, dl
    xor ah, ah
    pop bp
    ret
.no:
    xor ax, ax
    pop bp
    ret

_lx_peek:                           ; int lx_peek(void): the next byte, -1 at the end
    mov bx, [_L]
    mov ax, [bx + LXS_I]
    cmp ax, [bx + LXS_N]
    jb .have
    call _lx_fill                   ; AX = the bytes read; L->i = 0
    test ax, ax
    jz .eof
    mov bx, [_L]
    xor ax, ax
.have:
    mov bx, [bx + LXS_BUF]
    add bx, ax
    mov al, [bx]
    xor ah, ah
    ret
.eof:
    dec ax                          ; -1
    ret

_lx_getc:                           ; int lx_getc(void): lx_peek, and past it
    call _lx_peek
    test ax, ax
    js .r
    mov bx, [_L]
    inc word [bx + LXS_I]
.r:
    ret

_lx_next:                           ; int lx_next(void): lx_skipws, then lx_peek
    call _lx_skipws
    jmp _lx_peek

_lx_skipws:                         ; void lx_skipws(void): blanks and comments
.top:
    call _lx_peek
    cmp ax, '%'
    je .cmt
    mov bx, pde_wsmap
    call pde_cls
    jz .r
    mov bx, [_L]
    inc word [bx + LXS_I]
    jmp short .top
.cmt:
    call _lx_getc
    test ax, ax
    js .r                           ; the end: lx_peek answers it again
    cmp al, 10
    je .top
    cmp al, 13
    jne .cmt
    jmp short .top
.r:
    ret

_lx_tell:                           ; void lx_tell(struct pd_u32 *out): the next byte's offset
    mov bx, [_L]
    mov ax, [bx + LXS_FPOS]
    mov dx, [bx + LXS_FPOS + 2]
    mov cx, [bx + LXS_N]
    sub cx, [bx + LXS_I]
    jbe .s                          ; n <= i: nothing buffered
    sub ax, cx
    sbb dx, 0
.s:
    pop cx                          ; the return address
    pop bx                          ; out
    push bx
    push cx
    mov [bx], ax
    mov [bx + 2], dx
    ret

_lx_seek:                           ; void lx_seek(const struct pd_u32 *pos)
    pop cx                          ; the return address
    pop bx                          ; pos
    push bx
    push cx
    mov ax, [bx]
    mov dx, [bx + 2]
    mov bx, [_L]
    ; perf: an offset the file buffer still holds, [fpos - n, fpos], is a
    ; move of i - the "n g R" look-ahead and the dict cache seek back
    ; inside it - and not a refill
    cmp word [bx + LXS_MODE], 0
    jne .far
    cmp dx, [bx + LXS_FPOS + 2]
    jne .far
    mov cx, [bx + LXS_FPOS]
    sub cx, ax                      ; fpos - pos
    jb .far
    cmp cx, [bx + LXS_N]
    ja .far
    mov ax, [bx + LXS_N]
    sub ax, cx
    mov [bx + LXS_I], ax
    jmp short .sm
.far:
    mov [bx + LXS_FPOS], ax
    mov [bx + LXS_FPOS + 2], dx
    xor ax, ax
    mov [bx + LXS_N], ax
    mov [bx + LXS_I], ax
.sm:
    mov word [bx + LXS_SMODE], 0
    ret

_ov_clear:                          ; void ov_clear(void): ov a NULL, every field 0
    push es
    push ds
    pop es
    mov di, _ov
    xor ax, ax
    mov cx, PDV_TEXT / 2 + 1        ; the fields and text[0..1]
    rep stosw
    mov word [_ov], PDK_NULL
    pop es
    ret

_str_eq:                            ; int str_eq(const pdb *a, const char *b)
    push bp
    mov bp, sp
    mov si, [bp + 4]
    mov bx, [bp + 6]
.l:
    lodsb
    cmp al, [bx]
    jne .no
    inc bx
    test al, al
    jnz .l
    mov ax, 1
    pop bp
    ret
.no:
    xor ax, ax
    pop bp
    ret

_hexv:                              ; int hexv(int c): 0..15, or -1
    pop dx
    pop ax
    push ax
    push dx
    mov cx, ax
    sub ax, '0'
    cmp ax, 10
    jb .r                           ; '0'..'9' (c < '0' wraps high)
    mov ax, cx
    or al, 0x20                     ; 'A'..'F' -> 'a'..'f'
    sub ax, 'a'
    cmp ax, 6
    jae .no
    add ax, 10
.r:
    ret
.no:
    mov ax, -1
    ret

_lx_run:
    push bp
    mov bp, sp
    push es
    push ds
    pop es                          ; stosb into our own segment
    mov si, [bp+4]                  ; src
    mov cx, [bp+6]                  ; left
    mov di, [bp+8]                  ; dst
    mov dx, [bp+10]                 ; cap
    xor bh, bh
.next:
    jcxz .end
    lodsb
    test al, al
    js .take                        ; 128..255: never a class
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov ah, [pde_tmap + bx]
    mov bl, al
    and bl, 7
    test ah, [pde_bit + bx]
    jnz .stop
    cmp al, [bp + 12]
    je .stop
.take:
    dec cx
    test dx, dx
    jz .next                        ; past cap: consumed, not kept
    stosb
    dec dx
    jmp short .next
.stop:
    dec si                          ; the terminator stays unread
.end:
    mov ax, si
    sub ax, [bp+4]
    pop es
    pop bp
    ret
