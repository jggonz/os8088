; =============================================================================
; os8088 - apps/win16/w16lzver.inc
;
; LZEXPAND AND VER (SPEC.md 124.24.8, 124.24.9), in part 9 with the other
; system libraries (w16syslib.inc's header says how a handler here is
; reached). An installer's two: LZEXPAND expands COMPRESS.EXE's SZDD files as
; they are read, and VER reads an NE's VERSIONINFO, says where a file goes
; and copies one into place - through LZEXPAND, so a packed file is read as
; its expansion. tools/w16font.py's szdd() is the host's expansion.
;
; An LZ handle's state is a fixed global block in the program's own heap,
; 5,152 bytes: the 4 KB window, a 1 KB read buffer and the words below. The
; files are WIN16's DOS's handles; DS is the image here as everywhere in the
; part, except inside the expander, which runs with DS = the state.
; =============================================================================

section .w16sys

LZERROR_BADINHANDLE equ -1
LZERROR_READ        equ -3
LZERROR_WRITE       equ -4
LZERROR_GLOBALLOC   equ -5
LZERROR_BADVALUE    equ -7
LZERROR_UNKNOWNALG  equ -8

W16Z_N      equ 16                  ; LZ handles open at once
W16Z_H0     equ 0x400               ; the first one's number

LZS_FD      equ 0                   ; the file's handle
LZS_SIZE    equ 2                   ; its expanded length, a dword
LZS_CUR     equ 6                   ; the bytes expanded so far, a dword
LZS_WANT    equ 10                  ; the position asked for, a dword
LZS_WPOS    equ 14                  ; the window's next write
LZS_CTL     equ 16                  ; the control byte's bits not yet used
LZS_BITS    equ 17                  ; ...how many, less one (FFh: none)
LZS_MLEN    equ 18                  ; a match's bytes still to come
LZS_MPOS    equ 20                  ; ...from here in the window
LZS_IPOS    equ 22                  ; the read buffer's next byte
LZS_IEND    equ 24                  ; ...and its end
LZS_EOF     equ 26                  ; 1: the file has given all it has
LZS_WIN     equ 32
LZS_IBUF    equ LZS_WIN + 4096
LZS_IBSZ    equ 1024
LZS_LEN     equ LZS_IBUF + LZS_IBSZ ; 5,152
W16Z_CBUF   equ 1024                ; LZCopy's buffer, while it runs

; =============================================================================
; LZEXPAND's calls
; =============================================================================

; LZInit (LZEXPAND.3) - args: +0 hfSrc
W16SYS w16z_lzinit
    ICALL w16_args
    mov ax, [es:bx]
    call w16z_init
    ret

; LZOpenFile (LZEXPAND.2) - args: +0 wStyle, +2 lpOFStruct, +6 lpFileName:
; OpenFile's own, so OpenFile reads them - and again with the name's last
; letter '_' when the first fails
W16SYS w16z_lzopen
    push si
    push di
    SLCALL w16k_openfile
    cmp ax, -1
    jne .have
    ICALL w16_args
    push word [es:bx + 6]           ; the program's name, put back after
    push word [es:bx + 8]
    les si, [es:bx + 6]
    mov di, w16v_path               ; XSEC-OK: the part's buffer, read through cs:
    mov word [cs:w16v_lim], w16v_path + W16V_PMAX - 4  ; XSEC-OK: a bound,
                                    ;   room left for w16z_mangle's three
    call w16v_cpy
    call w16z_mangle
    ICALL w16_args
    mov word [es:bx + 6], w16v_path ; XSEC-OK: OpenFile reads it at CS:
    mov [es:bx + 8], cs
    SLCALL w16k_openfile
    push ax
    ICALL w16_args
    pop ax
    pop word [es:bx + 8]
    pop word [es:bx + 6]
    cmp ax, -1
    je .out
.have:
    push ax
    ICALL w16_args
    pop ax
    test word [es:bx], 0xFF8F       ; not OF_READ (share bits aside): as is
    jnz .out
    mov si, ax
    call w16z_init
    or ax, ax
    jns .out
    mov ax, si                      ; not expandable: the plain handle
.out:
    pop di
    pop si
    ret

; LZRead (LZEXPAND.5) - args: +0 cbRead, +2 lpBuffer, +6 hFile. A plain
; handle is _lread, whose arguments these are
W16SYS w16z_lzread
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    mov cx, [es:bx]
    les di, [es:bx + 2]
    push es
    call w16z_st
    pop es
    jc .plain
    call w16z_read
    jmp short .out
.plain:
    SLCALL w16k_lread
.out:
    pop di
    pop si
    ret

; LZSeek (LZEXPAND.4) - args: +0 nMode, +2 lOffset, +6 hFile. DX:AX = the
; position in the expansion. A plain handle is _llseek
W16SYS w16z_lzseek
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    mov cx, [es:bx]
    mov si, [es:bx + 2]
    mov di, [es:bx + 4]
    call w16z_st
    jc .plain
    jcxz .chk                       ; 0: from the start
    cmp cx, 2
    ja .bad
    je .end
    add si, [es:LZS_WANT]           ; 1: from the position
    adc di, [es:LZS_WANT + 2]
    jmp short .chk
.end:
    add si, [es:LZS_SIZE]           ; 2: from the end
    adc di, [es:LZS_SIZE + 2]
.chk:
    or di, di                       ; 0 to the length
    js .bad
    cmp di, [es:LZS_SIZE + 2]
    ja .bad
    jb .ok
    cmp si, [es:LZS_SIZE]
    ja .bad
.ok:
    mov [es:LZS_WANT], si
    mov [es:LZS_WANT + 2], di
    mov ax, si
    mov dx, di
    jmp short .out
.bad:
    mov ax, LZERROR_BADVALUE
    cwd
    jmp short .out
.plain:
    SLCALL w16k_llseek
.out:
    pop di
    pop si
    ret

; LZClose (LZEXPAND.6) - args: +0 hFile
W16SYS w16z_lzclose
    ICALL w16_args
    mov ax, [es:bx]
    call w16z_close
    xor ax, ax
    ret

; LZCopy (LZEXPAND.1), CopyLZFile (LZEXPAND.8) - args: +0 hfDest,
; +2 hfSource. DX:AX = the bytes written, or LZERROR_*. A plain source goes
; through LZInit, and the LZ handle made for it is closed after
W16SYS w16z_lzcopy
    push si
    push di
    ICALL w16_args
    mov di, [es:bx]
    mov ax, [es:bx + 2]
    xor si, si                      ; SI = 1: an LZ handle made here
    call w16z_st
    jnc .copy
    mov cx, ax
    call w16z_init
    or ax, ax
    js .zero                        ; (Wine's answer: nothing copied)
    cmp ax, cx
    je .copy
    inc si
.copy:
    mov [cs:w16z_src], ax
    mov bx, di
    call w16z_copy                  ; DX:AX, or CF=1 AX = the error
    jnc .done
    cwd
.done:
    or si, si
    jz .out
    push ax
    mov ax, [cs:w16z_src]
    call w16z_close
    pop ax
    jmp short .out
.zero:
    xor ax, ax
    cwd
.out:
    pop di
    pop si
    ret

; GetExpandedName (LZEXPAND.10) - args: +0 lpszBuffer, +4 lpszSource. The
; source's name with an SZDD header's last letter put back. 1, or
; LZERROR_BADINHANDLE
W16SYS w16z_expname
    push si
    push di
    ICALL w16_args
    mov dx, [es:bx + 4]
    mov es, [es:bx + 6]
    mov ax, 0x3D00
    ICALL w16_dcall
    mov dx, LZERROR_BADINHANDLE
    jc .ans
    mov bx, ax
    mov ah, 0x3F
    mov cx, 14
    mov dx, w16z_hdr                ; XSEC-OK: the part's buffer, ES = CS
    W16CSEG es
    ICALL w16_dcall
    pushf
    push ax
    mov ah, 0x3E
    ICALL w16_dcall
    pop cx
    popf
    mov dl, 0                       ; DL = the letter to put back
    jc .copy
    cmp cx, 14
    jne .copy
    call w16z_magic
    jne .copy
    mov dl, [cs:w16z_hdr + 9]
.copy:
    ICALL w16_args
    push ds
    lds si, [es:bx + 4]
    les di, [es:bx]
    mov bx, di                      ; BX = the last name's start
    cld
.c:
    lodsb
    stosb
    cmp al, '\'
    je .sep
    cmp al, ':'
    je .sep
    or al, al
    jnz .c
    jmp short .fix
.sep:
    mov bx, di
    jmp short .c
.fix:
    pop ds
    or dl, dl
    jz .one
    mov dh, 1                       ; DH = 1: no capital in the name
    xor si, si                      ; SI = its '.', 0 none
    mov di, bx
.f:
    mov al, [es:di]
    or al, al
    jz .fend
    cmp al, '.'
    jne .cap
    or si, si
    jnz .next
    mov si, di
    jmp short .next
.cap:
    cmp al, 'A'
    jb .next
    cmp al, 'Z'
    ja .next
    mov dh, 0
.next:
    inc di
    jmp short .f
.fend:
    or si, si
    jz .one
    cmp byte [es:si + 1], 0
    jne .last
    mov byte [es:si], 0             ; "NAME." loses its '.'
    jmp short .one
.last:
    mov al, dl
    call w16v_up
    cmp al, 'A'
    jb .put
    cmp al, 'Z'
    ja .put
    or dh, dh
    jz .put
    add al, 0x20                    ; a lower-case name, a lower-case letter
.put:
    mov [es:di - 1], al
.one:
    mov dx, 1
.ans:
    mov ax, dx
    pop di
    pop si
    ret

; LZStart (LZEXPAND.7) and LZDone (LZEXPAND.9) are w16_ans1 and w16_ans0.

; =============================================================================
; LZEXPAND, inside
; =============================================================================

; w16z_st - AX = a handle: CF=0 an LZ handle, ES = its state; CF=1 a plain
; one. Keeps all but ES
w16z_st:
    push bx
    mov bx, ax
    sub bx, W16Z_H0
    cmp bx, W16Z_N
    jae .no
    shl bx, 1
    cmp word [cs:w16z_seg + bx], 0
    je .no
    mov es, [cs:w16z_seg + bx]
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; w16z_magic - ZF=1 when w16z_hdr's first eight bytes are SZDD's
w16z_magic:
    cmp word [cs:w16z_hdr], 'SZ'
    jne .out
    cmp word [cs:w16z_hdr + 2], 'DD'
    jne .out
    cmp word [cs:w16z_hdr + 4], 0xF088
    jne .out
    cmp word [cs:w16z_hdr + 6], 0x3327
.out:
    ret

; w16z_init - LZInit: AX = a plain handle. AX = an LZ handle on it, the
; handle itself put back at 0 when it is not SZDD, or LZERROR_*. Keeps the
; rest
w16z_init:
    push bx
    push cx
    push dx
    push si
    push es
    mov si, ax                      ; SI = the file
    mov bx, ax
    mov ah, 0x3F
    mov cx, 14
    mov dx, w16z_hdr                ; XSEC-OK: the part's buffer, ES = CS
    W16CSEG es
    ICALL w16_dcall
    mov dx, LZERROR_BADINHANDLE
    jc .ans
    cmp ax, 14
    jne .plain
    call w16z_magic
    jne .plain
    mov dx, LZERROR_UNKNOWNALG
    cmp byte [cs:w16z_hdr + 8], 'A'
    jne .ans
    mov dx, LZERROR_GLOBALLOC
    xor bx, bx                      ; a free slot
.s:
    cmp word [cs:w16z_seg + bx], 0
    je .take
    add bx, 2
    cmp bx, W16Z_N * 2
    jb .s
    jmp short .ans
.take:
    push dx
    mov ax, LZS_LEN
    xor dx, dx
    xor cl, cl                      ; GMEM_FIXED
    ICALL w16_gnew                  ; AX = the handle, DX = the block
    W16SEG es, dx
    pop dx
    or ax, ax
    jz .ans
    mov [cs:w16z_seg + bx], es
    mov [es:LZS_FD], si
    mov ax, [cs:w16z_hdr + 10]
    mov [es:LZS_SIZE], ax
    mov ax, [cs:w16z_hdr + 12]
    mov [es:LZS_SIZE + 2], ax
    xor ax, ax
    mov [es:LZS_WANT], ax
    mov [es:LZS_WANT + 2], ax
    push ds
    push es
    pop ds
    call w16z_reset
    pop ds
    mov dx, bx
    shr dx, 1
    add dx, W16Z_H0
    jmp short .ans
.plain:
    mov ax, 0x4200                  ; not SZDD: back to its start
    mov bx, si
    xor cx, cx
    xor dx, dx
    ICALL w16_dcall
    mov dx, si
.ans:
    mov ax, dx
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16z_close - AX = a handle, LZ or plain: its file closed, an LZ one's
; state freed. Keeps all
w16z_close:
    push ax
    push bx
    push es
    call w16z_st
    jc .plain
    mov bx, ax
    sub bx, W16Z_H0
    shl bx, 1
    mov word [cs:w16z_seg + bx], 0
    mov bx, [es:LZS_FD]
    W16VOF ax, es                   ; the block's handle, which on the arm
    ICALL w16_gfreeh                ;   is not the selector ES holds
    jmp short .close
.plain:
    mov bx, ax
.close:
    mov ah, 0x3E
    ICALL w16_dcall
    pop es
    pop bx
    pop ax
    ret

; w16z_seek - AX = a handle, LZ or plain; CX:DX = a position from the
; start. CF=1 refused. Keeps all
w16z_seek:
    push ax
    push bx
    push cx
    push dx                         ; (DOS answers the position in DX:AX)
    push es
    call w16z_st
    jc .plain
    cmp cx, [es:LZS_SIZE + 2]
    ja .bad
    jb .ok
    cmp dx, [es:LZS_SIZE]
    ja .bad
.ok:
    mov [es:LZS_WANT], dx
    mov [es:LZS_WANT + 2], cx
    clc
    jmp short .out
.bad:
    stc
    jmp short .out
.plain:
    mov bx, ax
    mov ax, 0x4200
    ICALL w16_dcall
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16z_read - AX = a handle, LZ or plain; ES:DI = a buffer, CX = bytes.
; CF=0 AX = the bytes read; CF=1 AX = -1 (a plain handle's error). Keeps
; the rest
w16z_read:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push es
    call w16z_st
    jnc .lz
    pop es
    mov bx, ax
    mov dx, di
    mov ah, 0x3F
    ICALL w16_dcall
    jnc .out
    mov ax, -1
    jmp short .out
.lz:
    pop bp                          ; BP = the buffer's segment
    mov [cs:w16z_ids], ds
    push ds
    push es
    pop ds                          ; DS = the state
    mov es, bp
    call w16z_xread
    pop ds
    clc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16z_xread - DS = an LZ state, ES:DI = a buffer, CX = bytes: from the
; position asked for. AX = the bytes produced; the position after them.
; BX, CX, DX, SI, DI, BP clobbered
w16z_xread:
    cld
    push cx
    mov ax, [LZS_WANT]              ; before what is expanded: from the start
    mov dx, [LZS_WANT + 2]
    cmp dx, [LZS_CUR + 2]
    jne .cmp
    cmp ax, [LZS_CUR]
.cmp:
    jae .skip
    call w16z_rew
.skip:
    mov cx, [LZS_WANT]              ; up to it, expanded and dropped
    mov dx, [LZS_WANT + 2]
    sub cx, [LZS_CUR]
    sbb dx, [LZS_CUR + 2]
    jz .sk
    mov cx, 0x8000
.sk:
    jcxz .read
    xor bp, bp
    call w16z_dec
    jcxz .skip
    pop cx                          ; the file ended before it
    xor ax, ax
    ret
.read:
    pop cx
    push cx
    mov bp, 1
    call w16z_dec                   ; CX = what was not there
    pop ax
    sub ax, cx
    mov dx, [LZS_CUR]
    mov [LZS_WANT], dx
    mov dx, [LZS_CUR + 2]
    mov [LZS_WANT + 2], dx
    ret

; w16z_dec - DS = an LZ state: CX bytes expanded and stored at ES:DI (BP not
; 0) or dropped (BP = 0). CX = the bytes the file did not have. AX, BX, DX,
; SI clobbered
w16z_dec:
    xor bx, bx                      ; BX = what is past the length
    mov ax, [LZS_SIZE]
    mov dx, [LZS_SIZE + 2]
    sub ax, [LZS_CUR]
    sbb dx, [LZS_CUR + 2]
    jb .none
    jnz .fits
    cmp ax, cx
    jae .fits
    mov bx, cx
    sub bx, ax
    mov cx, ax
    jmp short .fits
.none:
    mov bx, cx
    xor cx, cx
.fits:
    push bx
    jcxz .end
.next:
    mov bx, [LZS_MLEN]
    or bx, bx
    jnz .copy
    dec byte [LZS_BITS]
    jns .bit
    call w16z_in                    ; a control byte: eight more items
    jc .end
    mov [LZS_CTL], al
    mov byte [LZS_BITS], 7
.bit:
    shr byte [LZS_CTL], 1
    jnc .match
    call w16z_in                    ; 1: a literal
    jc .end
    jmp short .put
.match:
    call w16z_in                    ; 0: a match, two bytes
    jc .end
    mov dl, al
    call w16z_in
    jc .end
    mov bl, dl
    mov bh, al
    and al, 0x0F
    add al, 3
    cbw
    mov [LZS_MLEN], ax
    shr bh, 1                       ; the position: the high byte's top four
    shr bh, 1                       ;   bits above the low byte
    shr bh, 1
    shr bh, 1
    mov [LZS_MPOS], bx
.copy:
    mov bx, [LZS_MPOS]
    mov al, [LZS_WIN + bx]
    inc bx
    and bh, 0x0F
    mov [LZS_MPOS], bx
    dec word [LZS_MLEN]
.put:
    mov bx, [LZS_WPOS]
    mov [LZS_WIN + bx], al
    inc bx
    and bh, 0x0F
    mov [LZS_WPOS], bx
    add word [LZS_CUR], 1
    adc word [LZS_CUR + 2], 0
    or bp, bp
    jz .drop
    stosb
.drop:
    loop .next
.end:
    pop bx
    add cx, bx
    ret

; w16z_in - DS = an LZ state: AL = the file's next byte. CF=1 it has none.
; SI clobbered
w16z_in:
    mov si, [LZS_IPOS]
    cmp si, [LZS_IEND]
    jb .have
    call w16z_fill
    jc .out
    mov si, LZS_IBUF
.have:
    lodsb
    mov [LZS_IPOS], si
    clc
.out:
    ret

; w16z_fill - DS = an LZ state: its read buffer filled from the file. CF=1
; the file had nothing more. Keeps all but AX
w16z_fill:
    cmp byte [LZS_EOF], 0
    jne .end
    push bx
    push cx
    push dx
    push es
    mov bx, [LZS_FD]
    push ds
    pop es                          ; ES = the state: the call's DS
    mov ds, [cs:w16z_ids]
    mov ah, 0x3F
    mov cx, LZS_IBSZ
    mov dx, LZS_IBUF
    ICALL w16_dcall
    push es
    pop ds
    pop es
    pop dx
    pop cx
    pop bx
    jc .eof
    or ax, ax
    jz .eof
    add ax, LZS_IBUF
    mov [LZS_IEND], ax
    mov word [LZS_IPOS], LZS_IBUF
    clc
    ret
.eof:
    mov byte [LZS_EOF], 1
.end:
    stc
    ret

; w16z_rew - DS = an LZ state: the file back at its first item, and the
; expansion at its start. Keeps all
w16z_rew:
    push ax
    push bx
    push cx
    push dx
    mov bx, [LZS_FD]
    mov ax, 0x4200
    xor cx, cx
    mov dx, 14
    push ds
    mov ds, [cs:w16z_ids]
    ICALL w16_dcall
    pop ds
    call w16z_reset
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16z_reset - DS = an LZ state: nothing expanded, the window all spaces.
; Keeps all
w16z_reset:
    push ax
    push cx
    push di
    push es
    xor ax, ax
    mov [LZS_CUR], ax
    mov [LZS_CUR + 2], ax
    mov [LZS_MLEN], ax
    mov [LZS_EOF], al
    mov [LZS_BITS], al
    mov word [LZS_WPOS], 4096 - 16
    mov word [LZS_IPOS], LZS_IBUF
    mov word [LZS_IEND], LZS_IBUF
    push ds
    pop es
    mov di, LZS_WIN
    mov cx, 2048
    mov ax, 0x2020
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; w16z_copy - AX = a source (LZ or plain) and BX = a destination file: the
; source from its position to its end, written. CF=0 DX:AX = the bytes;
; CF=1 AX = LZERROR_READ, LZERROR_WRITE or LZERROR_GLOBALLOC
w16z_copy:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov si, ax                      ; SI = the source, BP = the destination
    mov bp, bx
    mov ax, W16Z_CBUF
    xor dx, dx
    xor cl, cl
    ICALL w16_gnew
    or ax, ax
    jnz .buf
    mov ax, LZERROR_GLOBALLOC
    stc
    jmp short .ret
.buf:
    mov [cs:w16z_cbuf], dx
    xor ax, ax
    mov [cs:w16z_tot], ax
    mov [cs:w16z_tot + 2], ax
.lp:
    W16SEG es, [cs:w16z_cbuf]       ; a block of ours: a virtual paragraph on
                                    ;   the arm, which ES may not be loaded with
    xor di, di
    mov cx, W16Z_CBUF
    mov ax, si
    call w16z_read
    jc .rerr
    or ax, ax
    jz .done
    mov cx, ax
    mov bx, bp
    xor dx, dx
    mov ah, 0x40
    ICALL w16_dcall                 ; ES = the buffer: the call's DS
    jc .werr
    cmp ax, cx
    jne .werr
    add [cs:w16z_tot], ax
    adc word [cs:w16z_tot + 2], 0
    jmp short .lp
.rerr:
    mov ax, LZERROR_READ
    jmp short .err
.werr:
    mov ax, LZERROR_WRITE
.err:
    stc
    jmp short .free
.done:
    mov ax, [cs:w16z_tot]
    mov dx, [cs:w16z_tot + 2]
    clc
.free:
    pushf
    push ax
    push dx                         ; (the count's high word: the free may
    mov ax, [cs:w16z_cbuf]          ;   take DX)
    ICALL w16_gfreeh
    pop dx
    pop ax
    popf
.ret:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; w16z_mangle - w16v_path, copied with three bytes to spare: its last letter
; '_', as COMPRESS.EXE names what it packs: "._" added to a name with no
; extension, "_" to a shorter one
w16z_mangle:
    push ax
    push bx
    push si
    mov si, w16v_path               ; XSEC-OK: read through cs:
    mov bx, si                      ; BX = the last name's start
.n:
    mov al, [cs:si]
    or al, al
    jz .end
    inc si
    cmp al, '\'
    je .sep
    cmp al, ':'
    jne .n
.sep:
    mov bx, si
    jmp short .n
.end:                               ; SI = the NUL
.dot:
    cmp bx, si
    jae .none
    cmp byte [cs:bx], '.'
    je .ext
    inc bx
    jmp short .dot
.ext:
    mov ax, si
    sub ax, bx
    cmp ax, 4                       ; '.' and three
    jb .add
    mov byte [cs:si - 1], '_'
    jmp short .out
.none:
    mov byte [cs:si], '.'
    inc si
.add:
    mov word [cs:si], '_'           ; '_' and the NUL
.out:
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; VER's calls
; =============================================================================

VFFF_ISSHAREDFILE equ 1
VFF_CURNEDEST     equ 1
VFF_BUFFTOOSMALL  equ 4
VIFF_DONTDELETEOLD equ 2
VIF_OUTOFSPACE    equ 0x0100        ; (low word)
VIF_CANNOTCREATE  equ 0x0800
VIF_CANNOTDELETECUR equ 0x4000
VIF_CANNOTREADSRC equ 0x0001        ; (HIGH word: 10000h)
W16V_PMAX   equ 128                 ; a path's room, its NUL among it
RT_VERSION  equ 16

; GetFileResourceSize (VER.2) - args: +0 lpdwFileOffset, +4 lpszResId,
; +8 lpszResType, +12 lpszFileName. DX:AX = its length, 0 none
W16SYS w16v_ressize
    push si
    push di
    ICALL w16_args
    les ax, [es:bx + 4]
    mov [cs:w16v_ido], ax
    mov [cs:w16v_ids], es
    ICALL w16_args
    les ax, [es:bx + 8]
    mov [cs:w16v_tyo], ax
    mov [cs:w16v_tys], es
    mov ax, 12
    jmp short w16v_size

; GetFileVersionInfoSize (VER.6) - args: +0 lpdwHandle, +4 lpszFileName.
; Its VERSIONINFO's length, and its offset at *lpdwHandle
W16SYS w16v_vsize
    push si
    push di
    call w16v_isver
    mov ax, 4
w16v_size:                          ; AX = the name's word: the length, and
    call w16v_open                  ;   the offset where +0 points
    jc w16v_zero
    call w16v_find
    pushf
    call w16z_close
    popf
    jc w16v_zero
    ICALL w16_args
    les bx, [es:bx]
    mov ax, es
    or ax, bx
    jz .len
    mov ax, [cs:w16v_off]
    mov [es:bx], ax
    mov ax, [cs:w16v_off + 2]
    mov [es:bx + 2], ax
.len:
    mov ax, [cs:w16v_len]
    mov dx, [cs:w16v_len + 2]
    jmp short w16v_out
w16v_zero:
    xor ax, ax
w16v_dx0:
    xor dx, dx
w16v_out:
    pop di
    pop si
    ret

; GetFileResource (VER.3) - args: +0 lpvData, +4 dwResLen, +8 dwFileOffset,
; +12 lpszResId, +16 lpszResType, +20 lpszFileName. Found again unless
; dwFileOffset says where it is
W16SYS w16v_res
    push si
    push di
    ICALL w16_args
    les ax, [es:bx + 12]
    mov [cs:w16v_ido], ax
    mov [cs:w16v_ids], es
    ICALL w16_args
    les ax, [es:bx + 16]
    mov [cs:w16v_tyo], ax
    mov [cs:w16v_tys], es
    mov ax, 20
    mov bl, 1                       ; BL = 1: an offset given is used
    jmp short w16v_get

; GetFileVersionInfo (VER.7) - args: +0 lpvData, +4 dwLen, +8 dwHandle,
; +12 lpszFileName. The VERSIONINFO always looked for
W16SYS w16v_vinfo
    push si
    push di
    call w16v_isver
    mov ax, 12
    xor bl, bl
w16v_get:                           ; AX = the name's word; BL = 1 to trust
    call w16v_open                  ;   +8: the first +4 bytes into +0
    jc w16v_zero
    mov [cs:w16v_h], ax
    push bx
    ICALL w16_args
    mov ax, [es:bx + 8]
    mov dx, [es:bx + 10]
    mov [cs:w16v_off], ax
    mov [cs:w16v_off + 2], dx
    mov si, [es:bx + 4]             ; DI:SI = the room
    mov di, [es:bx + 6]
    pop bx
    or bl, bl
    jz .find
    or ax, dx
    jnz .at
.find:
    mov ax, [cs:w16v_h]
    call w16v_find
    jc .fail
    cmp di, [cs:w16v_len + 2]       ; no more than the resource
    jb .at
    ja .cut
    cmp si, [cs:w16v_len]
    jbe .at
.cut:
    mov si, [cs:w16v_len]
    mov di, [cs:w16v_len + 2]
.at:
    or di, di                       ; one far buffer's worth at most
    jnz .big
    cmp si, 0xFFF0
    jbe .n
.big:
    mov si, 0xFFF0
.n:
    mov ax, [cs:w16v_h]
    mov dx, [cs:w16v_off]
    mov cx, [cs:w16v_off + 2]
    call w16z_seek
    jc .fail
    ICALL w16_args
    les di, [es:bx]
    mov cx, si
    mov ax, [cs:w16v_h]
    call w16z_read
    jc .fail
    mov ax, [cs:w16v_h]
    call w16z_close
    mov ax, 1
    jmp w16v_dx0
.fail:
    mov ax, [cs:w16v_h]
    call w16z_close
    jmp w16v_zero

; w16v_isver - the resource to look for is VERSIONINFO: type 16, id 1
w16v_isver:
    xor ax, ax
    mov [cs:w16v_tys], ax
    mov [cs:w16v_ids], ax
    mov word [cs:w16v_tyo], RT_VERSION
    mov word [cs:w16v_ido], 1
    ret

; VerQueryValue (VER.11) - args: +0 lpuLen, +4 lplpBuffer, +8 lpSubBlock,
; +12 lpBlock. A path through the block, a key a step after each '\'
W16SYS w16v_query
    push si
    push di
    push bp
    push ds
    ICALL w16_args
    mov [cs:w16v_ap], bx
    mov [cs:w16v_ap + 2], es
    lds si, [es:bx + 8]             ; DS:SI = the path
    les di, [es:bx + 12]            ; ES:DI = the node, the root first
    mov bp, di                      ; BP = the block's start
    cld
.step:
    lodsb
    cmp al, '\'
    je .step
    dec si
    or al, al
    jz .found
    call w16v_kids                  ; BX = the first child, DX = the end
.child:
    cmp bx, dx
    jae .no
    cmp word [es:bx], 0
    je .no
    call w16v_keyeq                 ; CF=0: its key, SI past the step
    jnc .take
    add bx, [es:bx]
    call w16v_align
    jmp short .child
.take:
    mov di, bx
    jmp short .step
.found:
    call w16v_val                   ; BX = the value
    mov ax, [es:di + 2]
    mov [cs:w16v_len], ax
    mov [cs:w16v_off], bx
    mov [cs:w16v_off + 2], es
    lds bx, [cs:w16v_ap]
    les di, [bx + 4]
    mov ax, [cs:w16v_off]
    mov [es:di], ax
    mov ax, [cs:w16v_off + 2]
    mov [es:di + 2], ax
    les di, [bx]
    mov ax, es
    or ax, di
    jz .yes
    mov ax, [cs:w16v_len]
    mov [es:di], ax
.yes:
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop ds
    pop bp
    pop di
    pop si
    ret

; w16v_val - ES:DI = a version block's node, BP = the block's start: BX =
; its value, the next dword after its key. Keeps the rest
w16v_val:
    lea bx, [di + 4]
.k:
    cmp byte [es:bx], 0
    je .z
    inc bx
    jmp short .k
.z:
    inc bx
    ; fall into w16v_align

; w16v_align - BX up to the block's next dword (from BP, its start)
w16v_align:
    sub bx, bp
    add bx, 3
    and bl, 0xFC
    add bx, bp
    ret

; w16v_kids - ES:DI = a node: BX = its first child, DX = its end
w16v_kids:
    call w16v_val
    add bx, [es:di + 2]
    call w16v_align
    mov dx, di
    add dx, [es:di]
    ret

; w16v_keyeq - DS:SI = a step (to a '\' or the NUL), ES:BX = a node: CF=0
; its key is the step, case ignored, and SI is past it. Keeps the rest
w16v_keyeq:
    push ax
    push si
    push di
    lea di, [bx + 4]
.c:
    lodsb
    cmp al, '\'
    je .e
    or al, al
    jz .e
    call w16v_up
    mov ah, al
    mov al, [es:di]
    inc di
    call w16v_up
    cmp al, ah
    je .c
.ne:
    pop di
    pop si
    pop ax
    stc
    ret
.e:
    cmp byte [es:di], 0
    jne .ne
    dec si
    pop di
    pop ax                          ; (SI's old value: dropped)
    pop ax
    clc
    ret

; w16v_up - AL in capitals
w16v_up:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 0x20
.out:
    ret

; VerLanguageName (VER.10) - args: +0 nSize, +2 szLang, +6 wLang. The
; characters copied
W16SYS w16v_lang
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    mov cx, [es:bx]
    les di, [es:bx + 2]
    mov si, w16v_langs              ; XSEC-OK: read through cs:
.l:
    mov dx, [cs:si]
    cmp dx, ax
    je .hit
    or dx, dx
    jz .hit
    add si, 2
    mov dl, [cs:si]
    xor dh, dh
    inc si
    add si, dx
    jmp short .l
.hit:
    xor ax, ax                      ; AX = the characters copied
    jcxz .out
    dec cx
    mov dl, [cs:si + 2]
    xor dh, dh
    add si, 3
    cmp cx, dx
    jbe .n
    mov cx, dx
.n:
    mov ax, cx
    cld
    jcxz .z
.c:
    mov dl, [cs:si]
    mov [es:di], dl
    inc si
    inc di
    loop .c
.z:
    mov byte [es:di], 0
.out:
    xor dx, dx
    pop di
    pop si
    ret

; VerFindFile (VER.8) - args: +0 lpuDestDirLen, +4 szDestDir,
; +8 lpuCurDirLen, +12 szCurDir, +16 szAppDir, +20 szWinDir,
; +24 szFileName, +28 uFlags. VFF_*
W16SYS w16v_findfile
    push si
    push di
    mov byte [cs:w16v_rv], 0
    call w16v_sysdir
    mov word [cs:w16v_cur], w16v_empty  ; XSEC-OK: a far pointer, CS its segment
    mov [cs:w16v_cur + 2], cs
    ICALL w16_args
    test byte [es:bx + 28], VFFF_ISSHAREDFILE
    jz .own
    mov word [cs:w16v_dst], w16v_sys    ; XSEC-OK: a far pointer
    mov [cs:w16v_dst + 2], cs
    mov ax, w16v_dst                ; XSEC-OK: the variable's offset
    call w16v_there
    jnc .put
    ICALL w16_args
    les ax, [es:bx + 16]            ; then the program's own directory
    call w16v_appdir
    jmp short .put
.own:
    les ax, [es:bx + 16]
    mov [cs:w16v_dst], ax
    mov [cs:w16v_dst + 2], es
    mov dx, es
    or dx, ax
    jnz .app
    mov word [cs:w16v_dst], w16v_empty  ; XSEC-OK: a far pointer
    mov [cs:w16v_dst + 2], cs
.app:
    mov ax, w16v_dst                ; XSEC-OK: the variable's offset
    call w16v_there
    jnc .put
    mov word [cs:w16v_tmp], w16v_sys    ; XSEC-OK: a far pointer
    mov [cs:w16v_tmp + 2], cs
    mov ax, w16v_tmp                ; XSEC-OK: the variable's offset
    call w16v_there
    jc .put
    mov ax, [cs:w16v_tmp]
    mov [cs:w16v_cur], ax
    mov ax, [cs:w16v_tmp + 2]
    mov [cs:w16v_cur + 2], ax
    or byte [cs:w16v_rv], VFF_CURNEDEST
.put:
    mov si, w16v_dst                ; XSEC-OK: the variable's offset
    xor bx, bx                      ; the destination: +4, its length +0
    call w16v_give
    mov si, w16v_cur                ; XSEC-OK: the variable's offset
    mov bx, 8
    call w16v_give
    mov al, [cs:w16v_rv]
    xor ah, ah
    pop di
    pop si
    ret

; w16v_appdir - ES:AX = szAppDir: when the file is there, it is where the
; file is now, not where it goes
w16v_appdir:
    mov [cs:w16v_tmp], ax
    mov [cs:w16v_tmp + 2], es
    mov dx, es
    or dx, ax
    jz .out
    mov ax, w16v_tmp                ; XSEC-OK: the variable's offset
    call w16v_there
    jc .out
    mov ax, [cs:w16v_tmp]
    mov [cs:w16v_cur], ax
    mov ax, [cs:w16v_tmp + 2]
    mov [cs:w16v_cur + 2], ax
    or byte [cs:w16v_rv], VFF_CURNEDEST
.out:
    ret

; w16v_there - CS:AX = a far pointer to a directory: CF=0 the call's
; szFileName (+24) is in it, which is then where it is now (w16v_cur)
w16v_there:
    push si
    mov si, ax
    ICALL w16_args
    mov cx, [es:bx + 24]
    mov dx, [es:bx + 26]
    mov ax, cx
    or ax, dx
    stc
    jz .out
    push si
    mov bx, [cs:si + 2]
    mov si, [cs:si]
    mov di, w16v_path               ; XSEC-OK: the part's buffer
    call w16v_join
    pop si
    jc .out
    mov dx, w16v_path               ; XSEC-OK: the part's buffer, ES = CS
    W16CSEG es
    mov ax, 0x3D00
    ICALL w16_dcall
    jc .out
    mov bx, ax
    mov ah, 0x3E
    ICALL w16_dcall
    mov ax, [cs:si]
    mov [cs:w16v_cur], ax
    mov ax, [cs:si + 2]
    mov [cs:w16v_cur + 2], ax
    clc
.out:
    pop si
    ret

; w16v_give - CS:SI = a far pointer to a directory, BX = the call's words
; for it (+4 the buffer, +0 its length's): as much as the length allows,
; the length set to what it needs, VFF_BUFFTOOSMALL when it was less
w16v_give:
    push ds
    mov ax, bx
    ICALL w16_args
    add bx, ax
    push word [es:bx + 4]           ; the buffer, read before LES takes the
    push word [es:bx + 6]           ;   ES the arguments are read through
    les di, [es:bx]                 ; ES:DI = the length's word
    pop ax
    pop dx                          ; AX:DX = the buffer
    mov bx, ax
    or bx, dx
    jz .out
    mov bx, es
    or bx, di
    jz .out
    mov cx, [es:di]                 ; CX = the room
    push es
    push di                         ; (the length's word)
    mov es, ax                      ; ES:DI = the buffer
    mov di, dx
    lds si, [cs:si]
    xor bx, bx                      ; BX = its characters
.len:
    cmp byte [si + bx], 0
    je .have
    inc bx
    jmp short .len
.have:
    inc bx                          ; BX = what it needs, its NUL among it
    cmp cx, bx
    jae .cp
    or byte [cs:w16v_rv], VFF_BUFFTOOSMALL
    jcxz .set
    jmp short .cut
.cp:
    mov cx, bx
.cut:
    dec cx
    cld
    rep movsb
    mov byte [es:di], 0
.set:
    pop di
    pop es
    mov [es:di], bx
.out:
    pop ds
    ret

; w16v_sysdir - w16v_sys: WIN16's system directory, SYSTEM under its Windows
; directory (SPEC.md 124.20.5.1, 124.24.9)
w16v_sysdir:
    push si
    push di
    push es
    mov al, [w16_homev]
    add al, 'A'
    mov [cs:w16v_sys], al
    mov si, w16s_dir
    mov di, w16v_sys + 1            ; XSEC-OK: the part's buffer, ES = CS
    W16CSEG es
    mov cx, w16s_dirlen - 1
    cld
    rep movsb
    mov al, '\'                     ; SYSTEM under it (od_s_system, the
    stosb                           ;   name the library search dives
    mov si, od_s_system             ;   into), the NUL with it
    mov cx, od_s_syslen
    rep movsb
    pop es
    pop di
    pop si
    ret

; VerInstallFile (VER.9) - args: +0 lpuTmpFileLen, +4 szTmpFile,
; +8 szCurDir, +12 szDestDir, +16 szSrcDir, +20 szDestFileName,
; +24 szSrcFileName, +28 uFlags. DX:AX = VIF_*, 0 installed
W16SYS w16v_install
    push si
    push di
    xor ax, ax
    mov [cs:w16v_rv], ax
    mov [cs:w16v_rvh], ax
    ICALL w16_args                  ; the source...
    mov cx, [es:bx + 24]
    mov dx, [es:bx + 26]
    mov si, [es:bx + 16]
    mov bx, [es:bx + 18]
    mov di, w16v_path               ; XSEC-OK: the part's buffer
    call w16v_join
    jc .noread
    mov dx, w16v_path               ; XSEC-OK: the part's buffer, ES = CS
    W16CSEG es
    mov ax, 0x3D00
    ICALL w16_dcall
    jc .noread
    mov si, ax
    call w16z_init                  ; ...expanded when it is SZDD
    or ax, ax
    jns .src
    mov bx, si
    mov ah, 0x3E
    ICALL w16_dcall
    jmp .noread
.src:
    mov [cs:w16v_h], ax
    ICALL w16_args                  ; the destination
    mov cx, [es:bx + 20]
    mov dx, [es:bx + 22]
    mov si, [es:bx + 12]
    mov bx, [es:bx + 14]
    mov di, w16v_path2              ; XSEC-OK: the part's buffer
    call w16v_join
    jc .nocreate
    mov dx, w16v_path2              ; XSEC-OK: the part's buffer, ES = CS
    W16CSEG es
    xor cx, cx
    mov ah, 0x3C
    ICALL w16_dcall
    jc .nocreate
    mov bx, ax
    mov ax, [cs:w16v_h]
    call w16z_copy
    pushf
    push ax
    mov ah, 0x3E                    ; the destination closed
    ICALL w16_dcall
    mov ax, [cs:w16v_h]
    call w16z_close
    pop ax
    popf
    jnc .old
    mov dx, w16v_path2              ; a partial copy deleted; XSEC-OK: ES = CS
    W16CSEG es
    push ax
    mov ah, 0x41
    ICALL w16_dcall
    pop ax
    cmp ax, LZERROR_WRITE
    je .full
    mov word [cs:w16v_rvh], VIF_CANNOTREADSRC
    jmp .ans
.full:
    mov word [cs:w16v_rv], VIF_OUTOFSPACE
    jmp .ans
.old:
    ICALL w16_args                  ; the copy where it was, gone unless
    test byte [es:bx + 28], VIFF_DONTDELETEOLD  ; asked to stay
    jnz .ans
    les si, [es:bx + 8]
    mov ax, es
    or ax, si
    jz .ans
    cmp byte [es:si], 0
    je .ans
    ICALL w16_args
    push ds
    lds si, [es:bx + 8]
    les di, [es:bx + 12]
    call w16v_same                  ; CF=0 the same directory
    pop ds
    jnc .ans
    ICALL w16_args
    mov cx, [es:bx + 20]
    mov dx, [es:bx + 22]
    mov si, [es:bx + 8]
    mov bx, [es:bx + 10]
    mov di, w16v_path               ; XSEC-OK: the part's buffer
    call w16v_join
    jc .nodel
    mov dx, w16v_path               ; XSEC-OK: the part's buffer, ES = CS
    W16CSEG es
    mov ah, 0x41
    ICALL w16_dcall
    jnc .ans
.nodel:
    or word [cs:w16v_rv], VIF_CANNOTDELETECUR
    jmp short .ans
.nocreate:
    mov ax, [cs:w16v_h]
    call w16z_close
    mov word [cs:w16v_rv], VIF_CANNOTCREATE
    jmp short .ans
.noread:
    mov word [cs:w16v_rvh], VIF_CANNOTREADSRC
.ans:
    mov ax, [cs:w16v_rv]
    mov dx, [cs:w16v_rvh]
    pop di
    pop si
    ret

; w16v_same - DS:SI and ES:DI = two directories: CF=0 the same, case and a
; last '\' ignored. Keeps the rest
w16v_same:
    push ax
    push si
    push di
.c:
    lodsb
    call w16v_up
    mov ah, al
    mov al, [es:di]
    inc di
    call w16v_up
    cmp al, ah
    jne .tail
    or al, al
    jnz .c
.yes:
    pop di
    pop si
    pop ax
    clc
    ret
.tail:                              ; "X:\DIR\" against "X:\DIR"
    cmp ah, '\'
    jne .t2
    cmp byte [si], 0
    jne .no
    or al, al
    jz .yes
.t2:
    cmp al, '\'
    jne .no
    or ah, ah
    jnz .no
    cmp byte [es:di], 0
    je .yes
.no:
    pop di
    pop si
    pop ax
    stc
    ret

; =============================================================================
; VER, inside
; =============================================================================

; w16v_join - BX:SI = a directory (NULL or "" for none), DX:CX = a name, CS:DI
; = a W16V_PMAX buffer: the path, a '\' between them. CF=1 too long. Keeps
; all but DI (at the NUL)
w16v_join:
    push ax
    push es
    lea ax, [di + W16V_PMAX - 1]
    mov [cs:w16v_lim], ax
    push di
    mov es, bx
    mov ax, bx
    or ax, si
    jz .name
    call w16v_cpy
    jc .long
    pop ax
    push ax
    cmp di, ax
    je .name
    mov al, [cs:di - 1]
    cmp al, '\'
    je .name
    cmp al, ':'
    je .name
    cmp di, [cs:w16v_lim]
    jb .sep
    stc
    jmp short .long
.sep:
    mov byte [cs:di], '\'
    inc di
.name:
    mov es, dx
    mov si, cx
    call w16v_cpy
.long:
    pop ax
    pop es
    pop ax
    ret

; w16v_cpy - ES:SI = a string: onto CS:DI, up to [w16v_lim]. DI at its NUL;
; CF=1 it did not fit (cut there). Keeps the rest
w16v_cpy:
    push ax
    push si
.c:
    cmp di, [cs:w16v_lim]
    jae .long
    mov al, [es:si]
    mov [cs:di], al
    or al, al
    jz .end
    inc si
    inc di
    jmp short .c
.long:
    mov byte [cs:di], 0
    stc
    jmp short .out
.end:
    clc
.out:
    pop si
    pop ax
    ret

; w16v_open - AX = the call's word holding a file name: it opened, through
; LZInit - AX = the handle, LZ or plain. CF=1 it cannot be read
w16v_open:
    push bx
    push dx
    push si
    push es
    mov si, ax
    ICALL w16_args
    mov dx, [es:bx + si]
    mov es, [es:bx + si + 2]
    mov ax, 0x3D00
    ICALL w16_dcall
    jc .out
    mov si, ax
    call w16z_init
    or ax, ax
    jns .out                        ; (CF=0)
    mov bx, si
    mov ah, 0x3E
    ICALL w16_dcall
    stc
.out:
    pop es
    pop si
    pop dx
    pop bx
    ret

; w16v_find - AX = an open file: CF=0 its NE resource of the type and id in
; w16v_tyo/tys and w16v_ido/ids (a far string, or a number with segment 0),
; its offset and length in w16v_off and w16v_len. Keeps all
w16v_find:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [cs:w16v_h], ax
    xor cx, cx                      ; the MZ header
    xor dx, dx
    mov si, 64
    mov di, w16v_buf                ; XSEC-OK: the part's buffer
    call w16v_at
    jc .no
    cmp word [cs:w16v_buf], 'MZ'
    jne .no
    mov dx, [cs:w16v_buf + 0x3C]    ; the NE header
    mov cx, [cs:w16v_buf + 0x3E]
    call w16v_at
    jc .no
    cmp word [cs:w16v_buf], 'NE'
    jne .no
    mov ax, [cs:w16v_buf + 0x24]    ; its resource table...
    cmp ax, [cs:w16v_buf + 0x26]    ; ...is empty where the names start
    je .no
    add dx, ax
    adc cx, 0
    mov [cs:w16v_rt], dx
    mov [cs:w16v_rt + 2], cx
    mov si, 2
    call w16v_at                    ; the alignment shift
    jc .no
    mov ax, [cs:w16v_buf]
    mov [cs:w16v_sh], ax
    add dx, 2
    adc cx, 0
    mov si, 8
.type:
    call w16v_at                    ; a TYPEINFO
    jc .no
    mov ax, [cs:w16v_buf]
    or ax, ax
    jz .no
    mov bx, [cs:w16v_buf + 2]       ; BX = its resources
    add dx, 8
    adc cx, 0
    push si
    mov si, w16v_tyo                ; XSEC-OK: the variable's offset
    call w16v_match
    pop si
    jnc .names
    mov ax, 12
    push dx
    mul bx
    pop dx
    add dx, ax
    adc cx, 0
    jmp short .type
.names:
    mov si, 12
.name:
    or bx, bx
    jz .no
    call w16v_at                    ; a NAMEINFO
    jc .no
    mov ax, [cs:w16v_buf + 6]
    push si
    mov si, w16v_ido                ; XSEC-OK: the variable's offset
    call w16v_match
    pop si
    jnc .found
    add dx, 12
    adc cx, 0
    dec bx
    jmp short .name
.found:
    mov cx, [cs:w16v_sh]
    mov ax, [cs:w16v_buf]           ; its offset and length, in units
    xor dx, dx
    mov si, [cs:w16v_buf + 2]
    xor di, di
    jcxz .set
.sh:
    shl ax, 1
    rcl dx, 1
    shl si, 1
    rcl di, 1
    loop .sh
.set:
    mov [cs:w16v_off], ax
    mov [cs:w16v_off + 2], dx
    mov [cs:w16v_len], si
    mov [cs:w16v_len + 2], di
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16v_at - CX:DX = a place in [w16v_h]'s file, SI = bytes, CS:DI = a buffer:
; read there. CF=1 fewer came. Keeps all
w16v_at:
    push ax
    push cx
    push es
    mov ax, [cs:w16v_h]
    call w16z_seek
    jc .out
    W16CSEG es
    mov cx, si
    call w16z_read
    jc .out
    cmp ax, si
.out:
    pop es
    pop cx
    pop ax
    ret

; w16v_match - AX = a resource table's type or id word, CS:SI = the far
; pointer asked for: CF=0 the same - a number (segment 0) against the
; word's with its top bit, a name against the table's string at [w16v_rt]
; plus the word, case ignored. Keeps all
w16v_match:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    les di, [cs:si]
    mov bx, es
    or bx, bx
    jnz .str
    or di, 0x8000
    cmp ax, di
    je .yes
    jmp short .no
.str:
    test ax, 0x8000
    jnz .no
    mov dx, [cs:w16v_rt]
    mov cx, [cs:w16v_rt + 2]
    add dx, ax
    adc cx, 0
    push di
    mov si, 32
    mov di, w16v_nm                 ; XSEC-OK: the part's buffer
    call w16v_at
    pop di
    jc .no
    mov si, w16v_nm                 ; XSEC-OK: read through cs:
    mov cl, [cs:si]
    xor ch, ch
    cmp cx, 31
    ja .no
    inc si
.c:
    mov al, [es:di]
    call w16v_up
    mov ah, al
    jcxz .e
    mov al, [cs:si]
    call w16v_up
    cmp al, ah
    jne .no
    inc si
    inc di
    dec cx
    jmp short .c
.e:
    or ah, ah
    jnz .no
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; LZEXPAND's and VER's words, in the part: they go with the program
w16z_ids:   dw 0                    ; WIN16's DS, for the expander's reads
w16z_src:   dw 0
w16z_cbuf:  dw 0
w16z_tot:   dd 0
w16z_seg:   times W16Z_N dw 0       ; each LZ handle's state, 0 a free slot
w16z_hdr:   times 14 db 0           ; an SZDD header
w16v_h:     dw 0
w16v_rv:    dw 0                    ; VFF_* / VIF_*'s low word...
w16v_rvh:   dw 0                    ; ...and its high one
w16v_lim:   dw 0
w16v_tyo:   dw 0                    ; the resource asked for
w16v_tys:   dw 0
w16v_ido:   dw 0
w16v_ids:   dw 0
w16v_off:   dd 0                    ; ...where it is
w16v_len:   dd 0
w16v_rt:    dd 0                    ; the resource table
w16v_sh:    dw 0
w16v_ap:    dd 0
w16v_cur:   dd 0                    ; VerFindFile's two answers
w16v_dst:   dd 0
w16v_tmp:   dd 0
w16v_empty: db 0
w16v_buf:   times 64 db 0
w16v_nm:    times 32 db 0
w16v_sys:   times 32 db 0
w16v_path:  times W16V_PMAX db 0
w16v_path2: times W16V_PMAX db 0
; VerLanguageName's names: Windows 3.1's own VER.DLL's (SPEC.md 124.24.9)
w16v_langs:  dw 0x0401
             db 6, 'Arabic'
             dw 0x0402
             db 9, 'Bulgarian'
             dw 0x0403
             db 7, 'Catalan'
             dw 0x0404
             db 19, 'Traditional Chinese'
             dw 0x0405
             db 5, 'Czech'
             dw 0x0406
             db 6, 'Danish'
             dw 0x0407
             db 6, 'German'
             dw 0x0408
             db 5, 'Greek'
             dw 0x0409
             db 12, 'U.S. English'
             dw 0x040A
             db 17, 'Castilian Spanish'
             dw 0x040B
             db 7, 'Finnish'
             dw 0x040C
             db 6, 'French'
             dw 0x040D
             db 6, 'Hebrew'
             dw 0x040E
             db 9, 'Hungarian'
             dw 0x040F
             db 9, 'Icelandic'
             dw 0x0410
             db 7, 'Italian'
             dw 0x0411
             db 8, 'Japanese'
             dw 0x0412
             db 6, 'Korean'
             dw 0x0413
             db 5, 'Dutch'
             dw 0x0414
             db 18, 'Norwegian - Bokmal'
             dw 0x0415
             db 6, 'Polish'
             dw 0x0416
             db 20, 'Brazilian Portuguese'
             dw 0x0417
             db 14, 'Rhaeto-Romanic'
             dw 0x0418
             db 8, 'Romanian'
             dw 0x0419
             db 7, 'Russian'
             dw 0x041A
             db 22, 'Croato-Serbian (latin)'
             dw 0x041B
             db 6, 'Slovak'
             dw 0x041C
             db 8, 'Albanian'
             dw 0x041D
             db 7, 'Swedish'
             dw 0x041E
             db 4, 'Thai'
             dw 0x041F
             db 7, 'Turkish'
             dw 0x0420
             db 4, 'Urdu'
             dw 0x0421
             db 6, 'Bahasa'
             dw 0x0804
             db 18, 'Simplified Chinese'
             dw 0x0807
             db 12, 'Swiss German'
             dw 0x0809
             db 12, 'U.K. English'
             dw 0x080A
             db 15, 'Mexican Spanish'
             dw 0x080C
             db 14, 'Belgian French'
             dw 0x0810
             db 13, 'Swiss Italian'
             dw 0x0813
             db 13, 'Belgian Dutch'
             dw 0x0814
             db 19, 'Norwegian - Nynorsk'
             dw 0x0816
             db 10, 'Portuguese'
             dw 0x081A
             db 25, 'Serbo-Croatian (cyrillic)'
             dw 0x0C0C
             db 15, 'Canadian French'
             dw 0x100C
             db 12, 'Swiss French'
             dw 0                ; the end, and what any other id answers
             db 16, 'Unknown Language'

section .text
