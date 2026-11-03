; =============================================================================
; os8088 - tests/dostrap/fcbio.asm
;
; FCBIO.COM - the calls WordPerfect 4.1 makes before its editing screen and on
; its way out (SPEC.md 96.56): AH=33h and AH=37h, and the FCB file calls 0Fh,
; 10h, 11h, 13h, 16h, 27h and 28h. A MEASUREMENT: it prints what it saw, one
; line per question, and runs unchanged under a real DOS, so the two columns
; diff with nothing to interpret (docs/DOS-DEBUGGING.md). IBM DOS 3.30 answers:
;
;   33=3300 DL=00              get break: AH comes back
;   33=3301                    set break: AH comes back
;   37=3700 DL=2F              the switch character
;   16=00 10=00                make an empty file, close it
;   16=00 28=00 CX=0009 10=00  make one, write 9 one-byte records, close
;   0F=00 SZ=0009 RS=0080      open it: its size, and records of 128
;   27=01 CX=0009 HELLO FCB    20 asked, 9 there: end of file
;   27=03 CX=0003 PAD=0000     3 of 4 bytes, 9 there: the third partial, zeroed
;   10=00
;   11=00 FCBTST  CHK          or TV1 - whichever the directory holds first
;   13=00 11=FF                delete both by pattern, and nothing is left
;   0F=FF                      no such file
;
;   nasm -f bin -w+error -o FCBIO.COM tests/dostrap/fcbio.asm
;
; It WRITES, so the disk it runs from must be writable, and it removes what
; it made. NOTHING HERE IS THIRD-PARTY: it is ours, MIT with the rest of the
; tree.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

start:
    mov ax, 0x3300                  ; --- AH=33h, both halves -------------
    mov dl, 0x55
    int 0x21
    push dx
    mov si, s33
    call tag16
    pop ax
    mov si, sdl
    call tag8
    call crlf
    mov ax, 0x3301
    mov dl, 0
    int 0x21
    mov si, s33
    call tag16
    call crlf
    mov ax, 0x3700                  ; --- AH=37h -------------------------
    mov dl, 0x55
    int 0x21
    push dx
    mov si, s37
    call tag16
    pop ax
    mov si, sdl
    call tag8
    call crlf

    mov si, ntv1                    ; --- an empty file, made and closed --
    call setname
    mov ah, 0x16
    call fcbcall
    mov si, s16
    call tag8
    mov ah, 0x10
    call fcbcall
    mov si, s10
    call tag8
    call crlf

    mov si, nchk                    ; --- nine one-byte records ----------
    call setname
    mov ah, 0x16
    call fcbcall
    mov si, s16
    call tag8
    mov word [fcb+0x0E], 1
    call rr0
    mov dx, data
    call setdta
    mov ah, 0x28
    mov cx, 9
    call fcbcall
    push cx
    mov si, s28
    call tag8
    pop ax
    mov si, scx
    call tag16
    mov ah, 0x10
    call fcbcall
    mov si, s10
    call tag8
    call crlf

    mov si, nchk                    ; --- and read back -------------------
    call setname
    mov ah, 0x0F
    call fcbcall
    mov si, s0f
    call tag8
    mov ax, [fcb+0x10]
    mov si, ssz
    call tag16
    mov ax, [fcb+0x0E]
    mov si, srs
    call tag16
    call crlf
    mov word [fcb+0x0E], 1
    call rr0
    mov dx, buf
    call setdta
    mov ah, 0x27
    mov cx, 20
    call fcbcall
    push cx
    mov si, s27
    call tag8
    pop ax
    mov si, scx
    call tag16
    mov al, ' '
    call putc
    mov si, buf
    mov cx, 9
.show:
    lodsb
    call putc
    loop .show
    call crlf
    mov word [buf+9], 0x5555        ; what a partial must zero
    mov word [buf+11], 0x5555
    mov word [fcb+0x0E], 4
    call rr0
    mov ah, 0x27
    mov cx, 3
    call fcbcall
    push cx
    mov si, s27
    call tag8
    pop ax
    mov si, scx
    call tag16
    mov ax, [buf+10]                ; bytes 10 and 11: the third record's
    mov si, spad                    ; last two, which the file never had
    call tag16
    call crlf
    mov ah, 0x10
    call fcbcall
    mov si, s10
    call tag8
    call crlf

    mov si, nall                    ; --- find, delete by pattern, find ---
    call setname
    mov dx, buf
    call setdta
    mov ah, 0x11
    call fcbcall
    mov si, s11
    call tag8
    mov al, ' '
    call putc
    mov si, buf+1
    mov cx, 11
.nm:
    lodsb
    call putc
    loop .nm
    call crlf
    mov si, nall
    call setname
    mov ah, 0x13
    call fcbcall
    mov si, s13
    call tag8
    mov si, nall
    call setname
    mov ah, 0x11
    call fcbcall
    mov si, s11
    call tag8
    call crlf
    mov si, nnone
    call setname
    mov ah, 0x0F
    call fcbcall
    mov si, s0f
    call tag8
    call crlf

    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; setname - a fresh FCB on the current drive named by the 11 bytes at SI
setname:
    mov di, fcb
    xor ax, ax
    stosb
    mov cx, 11
    rep movsb
    mov cx, 25
    rep stosb
    ret
rr0:
    mov word [fcb+0x21], 0
    mov word [fcb+0x23], 0
    ret
setdta:
    mov ah, 0x1A
    int 0x21
    ret
; fcbcall - AH on the FCB; out AL (kept in AX for tag8), CX as the call left it
fcbcall:
    mov dx, fcb
    int 0x21
    ret

; tag8 - "<SI$>=" then AL in hex and a space; tag16 the same for AX
tag8:
    push ax
    call says
    pop ax
    call hex8
    jmp short sp1
tag16:
    push ax
    call says
    pop ax
    push ax
    mov al, ah
    call hex8
    pop ax
    call hex8
sp1:
    mov al, ' '
    jmp short putc
says:
    lodsb
    cmp al, '$'
    je .r
    call putc
    jmp short says
.r:
    ret
hex8:
    push ax
    mov cl, 4
    shr al, cl
    call .nib
    pop ax
.nib:
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
putc:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret
crlf:
    mov al, 13
    call putc
    mov al, 10
    jmp short putc

s33  db '33=$'
s37  db '37=$'
sdl  db 'DL=$'
s16  db '16=$'
s10  db '10=$'
s28  db '28=$'
s27  db '27=$'
s0f  db '0F=$'
s11  db '11=$'
s13  db '13=$'
scx  db 'CX=$'
ssz  db 'SZ=$'
srs  db 'RS=$'
spad db 'PAD=$'
ntv1  db 'FCBTST  TV1'
nchk  db 'FCBTST  CHK'
nall  db 'FCBTST  ???'
nnone db 'NOSUCH  XYZ'
data db 'HELLO FCB'
msg_key db 'KEY$'
fcb  times 40 db 0
buf  times 64 db 0
