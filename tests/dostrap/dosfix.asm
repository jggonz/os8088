; DOSFIX.COM - the two handle defects docs/plans/completed/DOS-STREAM-PLAN.md 3 found,
; as a probe. OURS, MIT with the rest of the tree.
;
; 1. FDIR: open SUB\X.DAT from the ROOT and read all of it. X.DAT is 12 KB,
;    so the read crosses the box's 8 KB window and REFILLS, and a refill
;    that resolves the bare name where the program STANDS finds the DECOY -
;    a root X.DAT whose every byte is 0xEE - where the real one's byte i is
;    (i >> 10) + 1. A wrong folder is therefore a wrong BYTE, named with its
;    offset, and not an end of file that could be mistaken for a short read.
; 2. INTERLEAVE: two created files written in turn, 700 bytes a go, so
;    each switch flushes the other's window PARTIAL (ILV ok / ILV BAD).
; 3. NOCLOSE: create NOCLOSE.DAT, write 3,000 bytes of 'N' and EXIT without
;    AH=3Eh. DOS closes every handle of a process that terminates; the host
;    reads the file off the floppy afterwards and wants all 3,000.
; 4. CWD: AH=47h straight after 1, whose refill stood the MACHINE in SUB.
;    The program never left the root, so DOS answers "" - and a box that
;    asks the machine where it stands without standing it where the
;    program is first answers "SUB" (CWD ok / CWD BAD).
;
; Runs under a real DOS unchanged. It waits for a key after READY, so the
; host can read the screen; the exit that follows is still without AH=3Eh.

    org 0x100
    cpu 8086

XSIZE   equ 12288

start:
    ; --- 1. FDIR -----------------------------------------------------------
    mov ax, 0x3D00
    mov dx, n_sub
    int 0x21
    mov byte [stage], 'O'
    jc fail
    mov [fh], ax
    xor si, si                  ; SI = the file offset of buf[0]
.rd:
    mov ah, 0x3F
    mov bx, [fh]
    mov cx, 1000                ; not a cluster multiple on purpose
    mov dx, buf
    int 0x21
    mov byte [stage], 'R'
    jc fail
    or ax, ax
    jz .eof
    mov cx, ax
    mov di, buf
.ck:
    mov ax, si
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    inc al                      ; byte i of the real X.DAT is (i >> 10) + 1
    cmp [di], al
    jne .bad
    inc si
    inc di
    loop .ck
    jmp .rd
.bad:
    mov dx, s_fbad
    call puts
    mov ax, si
    call putn
    mov dx, s_got
    call puts
    mov al, [di]
    xor ah, ah
    call putn
    call crlf
    jmp .nc
.eof:
    mov byte [stage], 'L'
    cmp si, XSIZE
    jne fail
    mov dx, s_fok
    call puts
    mov ah, 0x3E
    mov bx, [fh]
    int 0x21
.nc:
    ; --- 4. CWD: the program is still in the root, whatever 1 refilled ---
    mov ah, 0x47
    xor dl, dl
    mov si, cwd
    int 0x21
    mov dx, s_cbad
    jc .cw
    cmp byte [cwd], 0
    jne .cw
    mov dx, s_cok
.cw:
    call puts
    ; --- 2. INTERLEAVE: two CREATED files written in turn, 700 bytes a go --
    ; Each switch takes the box's one window from the other file, so each
    ; file's window is flushed PARTIAL, at a size that is not a cluster
    ; multiple - and a flush after that has to extend a file the kernel's
    ; append refuses to extend. DOS writes both whole. ILV ok, or ILV BAD.
    mov ah, 0x3C
    xor cx, cx
    mov dx, n_ia
    int 0x21
    mov byte [stage], 'a'
    jc fail
    mov [fha], ax
    mov ah, 0x3C
    xor cx, cx
    mov dx, n_ib
    int 0x21
    mov byte [stage], 'b'
    jc fail
    mov [fhb], ax
    mov di, buf
    mov al, 'I'
    mov cx, 700
    cld
    rep stosb
    mov word [nil], 20
.il:
    mov bx, [fha]
    call w700
    jc .ilbad
    mov bx, [fhb]
    call w700
    jc .ilbad
    dec word [nil]
    jnz .il
    mov ah, 0x3E
    mov bx, [fha]
    int 0x21
    jc .ilbad
    mov ah, 0x3E
    mov bx, [fhb]
    int 0x21
    jc .ilbad
    mov dx, s_iok
    call puts
    jmp short .ild
.ilbad:
    mov dx, s_ibad
    call puts
    mov ax, [nil]
    call putn
    call crlf
.ild:
    ; --- 3. NOCLOSE, LAST: its window must still be dirty at the exit ----
    mov ah, 0x3C
    xor cx, cx
    mov dx, n_nc
    int 0x21
    mov byte [stage], 'C'
    jc fail
    mov bx, ax
    mov di, buf
    mov al, 'N'
    mov cx, 3000
    cld
    rep stosb
    mov ah, 0x40
    mov cx, 3000
    mov dx, buf
    int 0x21
    mov byte [stage], 'W'
    jc fail
    mov dx, s_ready
    call puts
    mov ah, 0x08                ; a key first, so the host can read the
    int 0x21                    ; screen before the desktop comes back
    mov ax, 0x4C00              ; ...and NO AH=3Eh: the exit must close it
    int 0x21

w700:                           ; 700 bytes of 'I' to handle BX
    mov ah, 0x40
    mov cx, 700
    mov dx, buf
    int 0x21
    jc .x
    cmp ax, 700
    je .x
    stc
.x:
    ret

fail:
    mov dx, s_fail
    call puts
    mov dl, [stage]
    mov ah, 0x02
    int 0x21
    call crlf
    mov dx, s_ready
    call puts
    mov ax, 0x4C01
    int 0x21

puts:
    mov ah, 0x09
    int 0x21
    ret

crlf:
    mov dx, s_crlf
    jmp puts

putn:                           ; AX, unsigned decimal
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.p:
    pop dx
    add dl, '0'
    mov ah, 0x02
    int 0x21
    loop .p
    ret

n_sub:   db 'SUB\X.DAT', 0
n_nc:    db 'NOCLOSE.DAT', 0
n_ia:    db 'ILVA.DAT', 0
n_ib:    db 'ILVB.DAT', 0
s_iok:   db 'ILV ok', 13, 10, '$'
s_ibad:  db 'ILV BAD, rounds left $'
s_fok:   db 'FDIR ok', 13, 10, '$'
s_cok:   db 'CWD ok', 13, 10, '$'
s_cbad:  db 'CWD BAD', 13, 10, '$'
s_fbad:  db 'FDIR BAD at $'
s_got:   db ' got $'
s_ready: db 'READY', 13, 10, '$'
s_fail:  db 'FAILED at $'
s_crlf:  db 13, 10, '$'
fh:      dw 0
fha:     dw 0
fhb:     dw 0
nil:     dw 0
stage:   db 0
cwd:     times 64 db 0
buf:
