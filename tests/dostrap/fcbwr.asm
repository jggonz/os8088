; =============================================================================
; os8088 - tests/dostrap/fcbwr.asm
;
; FCBWR.COM - the rest of DOS's FCB family (SPEC.md 96.56.3): the WRITES 15h
; and 22h, 17h rename, 23h file size, 12h find next, the EXTENDED FCB on the
; calls that read its attribute, the PSP's two FCBs (parsed from the command
; tail at the top, copied out of the parameter block by an EXEC), 1Ah/2Fh,
; 0Dh and 2Eh/54h. A MEASUREMENT: one line per group of questions, and it runs
; unchanged under a real DOS, so tests/dosfcbwr.py holds what IBM DOS 3.30
; printed for this binary and this DOS must print the same.
;
;   FCBWR FOO.TXT Z:BAR     the row runs it so, under both
;
; Lines (each a few calls' answers - the screen is 25 rows under IBM's prompt):
;   PSP  the AX it was entered with and the two FCBs at 5Ch/6Ch
;   KID  the same, in a child it EXECs with a parameter block of its own
;   DTA  1Ah's AX, 2Fh's AX/BX and whether ES came back as ours, 0Dh's AX,
;        2Eh's AX and 54h's AL after a set of 1 and of 0 (0Dh's AH alone)
;   15   16h, then three 15h of 10-byte records: AL each, block, current,
;        the random record (preset 1234h), the size field
;   22   22h of record 5: AL, block, current, random record, size
;   28   28h of two records at 6: AL, CX, block, current, random, size
;   0F   close, open: AL, record size, size, whether the date and time are
;        the directory entry's (an extended search's), block, current
;   27   27h at 5 for four: AL, CX, block, current, random, sum of 30 bytes
;   23   23h at record size 10, 7, 128 and 0 (AL, RR each), and a miss
;   17   rename DAT->DAX, then 0Fh on the old name and 23h on the new
;   REN  two .TMPs renamed by pattern, onto an existing name, from nothing
;   11   11h/12h/12h on the renamed pair: AL, the drive, the name; 11h on the
;        DAX: its attribute and size out of the directory entry
;   X11  11h by extended FCB: the DTA's first eight bytes and the name; the
;        folder found by attribute 10h, and not by a plain FCB
;   XFC  16h/10h by extended FCB, a plain 11h, an extended 13h and 11h; an
;        extended 0Fh/10h on the DAX: its record size and size
;
; (Not asked, as this DOS's HANDLE layer cannot answer them and this probe
; is about the FCB layer over it: a hidden file - AH=3Ch keeps no attribute -
; and a volume label, which no package can see, SPEC.md 96.12.1.)
;   13   delete the pair and the DAX
;
; It WRITES, so its disk must be writable, and it removes what it made.
; NOTHING HERE IS THIRD-PARTY: it is ours, MIT with the rest of the tree.
;
;   nasm -f bin -w+error -o FCBWR.COM tests/dostrap/fcbwr.asm
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

start:
    mov [entax], ax
    cld
    mov si, 0x81                    ; a CHILD is the run whose tail says /K
.tl:
    lodsb
    cmp al, 13
    je parent
    cmp al, '/'
    jne .tl
    mov si, s_kid
    call psp
    mov ax, 0x4C00
    int 0x21

parent:
    mov si, s_psp
    call psp

    cli
    mov sp, endall                  ; the stack INTO the block it keeps
    sti
    mov bx, (endall - start + 0x100 + 15) >> 4
    mov ah, 0x4A                    ; room for the child, as DOS needs
    int 0x21
    mov [parm+4], ds
    mov [parm+8], ds
    mov [parm+12], ds
    mov dx, s_self
    mov bx, parm
    mov [sv_ss], ss
    mov [sv_sp], sp
    mov ax, 0x4B00
    int 0x21
    push cs
    pop ds
    push cs
    pop es
    cli
    mov ss, [cs:sv_ss]              ; (DOS 2 leaves SS:SP the child's)
    mov sp, [cs:sv_sp]
    sti

    ; --- DTA: 1Ah, 2Fh, 0Dh, 2Eh/54h --------------------------------------
    mov si, s_dta
    call puts
    mov dx, dta
    mov ax, 0x1A5A
    int 0x21
    call hex4s
    mov bx, 0x5A5A
    push ds
    pop es
    xor ax, ax
    mov es, ax
    mov ax, 0x2F5A
    int 0x21
    call hex4s
    mov ax, bx
    call hex4s
    mov ax, es
    mov dx, cs
    cmp ax, dx
    mov al, '1'
    je .sg
    mov al, '0'
.sg:
    call putc
    call space
    push cs
    pop es
    mov ax, 0x0D5A
    int 0x21
    mov al, ah                      ; (AH: IBM DOS hands AL back as 01h,
    call hex2s                      ; which is no documented answer)
    mov ax, 0x2E01
    xor dl, dl
    int 0x21
    call hex4s
    mov ax, 0x545A
    int 0x21
    call hex2s
    mov ax, 0x2E00
    xor dl, dl
    int 0x21
    mov ax, 0x545A
    int 0x21
    call hex2s
    call crlf

    ; --- 15: make, three sequential writes --------------------------------
    mov si, s_15
    call puts
    mov si, n_dat
    call setname
    mov ah, 0x16
    call fcbal
    mov word [fcb+0x0E], 10
    mov word [fcb+0x21], 0x1234
    mov al, 'A'
.w:
    call fillrec
    push ax
    mov ah, 0x15
    call fcbal
    pop ax
    inc ax
    cmp al, 'D'
    jb .w
    call state
    call crlf

    ; --- 22: one random write at record 5 ---------------------------------
    mov si, s_22
    call puts
    mov word [fcb+0x21], 5
    mov al, 'F'
    call fillrec
    mov ah, 0x22
    call fcbal
    call state
    call crlf

    ; --- 28: two records at 6 ---------------------------------------------
    mov si, s_28
    call puts
    mov word [fcb+0x21], 6
    mov al, 'G'
    call fillrec
    mov di, dta + 10
    mov al, 'H'
    mov cx, 10
    rep stosb
    mov cx, 2
    mov ah, 0x28
    call fcbal
    mov ax, cx
    call hex4s
    call state
    call crlf

    ; --- 0F: close and open again -----------------------------------------
    mov si, s_0f
    call puts
    mov ah, 0x10
    call fcbal
    mov si, n_dat
    call setname
    mov word [fcb+0x14], 0x5A5A     ; (so an open that leaves them is seen)
    mov word [fcb+0x16], 0x5A5A
    mov ah, 0x0F
    call fcbal
    mov ax, [fcb+0x0E]
    call hex4s
    mov ax, [fcb+0x12]
    call hex4
    mov ax, [fcb+0x10]
    call hex4s
    mov si, n_dat                   ; the date and time against the entry
    xor al, al                      ; a search finds (an extended one, so
    call xname                      ; the entry is at DTA+8)
    mov dx, xfcb
    mov ah, 0x11
    int 0x21
    mov ax, [fcb+0x14]
    cmp ax, [dta+8+0x18]
    jne .dt0
    mov ax, [fcb+0x16]
    cmp ax, [dta+8+0x16]
    mov al, '1'
    je .dt
.dt0:
    mov al, '0'
.dt:
    call putc
    call space
    mov ax, [fcb+0x0C]
    call hex4s
    mov al, [fcb+0x20]
    call hex2s
    call crlf

    ; --- 27: four at 5, three there ---------------------------------------
    mov si, s_27
    call puts
    mov word [fcb+0x0E], 10
    mov word [fcb+0x21], 5
    mov di, dta
    mov cx, 40
    mov al, 0xFF
    rep stosb
    mov cx, 4
    mov ah, 0x27
    call fcbal
    mov ax, cx
    call hex4s
    call bcr
    mov si, dta
    mov cx, 15
    xor dx, dx
.sm:
    lodsw
    add dx, ax
    loop .sm
    mov ax, dx
    call hex4s
    call crlf
    mov ah, 0x10
    call fcbal0

    ; --- 23: the size in records, four record sizes and a miss ------------
    mov si, s_23
    call puts
    mov bx, sizes
.sz:
    mov si, n_dat
    call setname
    mov ax, [bx]
    mov [fcb+0x0E], ax
    mov word [fcb+0x21], 0x5A5A
    mov word [fcb+0x23], 0x5A5A
    mov ah, 0x23
    call fcbal
    mov ax, [fcb+0x23]
    call hex4
    mov ax, [fcb+0x21]
    call hex4s
    inc bx
    inc bx
    cmp bx, sizes + 8
    jb .sz
    mov si, n_none
    call setname
    mov ah, 0x23
    call fcbal
    call crlf

    ; --- 17: rename the file, then look for both names --------------------
    mov si, s_17
    call puts
    mov si, n_dat
    call setname
    mov si, n_dax
    mov di, fcb + 0x11
    mov cx, 11
    rep movsb
    mov ah, 0x17
    call fcbal
    mov si, n_dat
    call setname
    mov ah, 0x0F
    call fcbal
    mov si, n_dax
    call setname
    mov ah, 0x23
    call fcbal
    mov ax, [fcb+0x21]
    call hex4s
    call crlf

    ; --- REN: by pattern, onto a name that is there, from nothing ---------
    mov si, s_ren
    call puts
    mov si, n_w1
    call mkempty
    mov si, n_w2
    call mkempty
    mov si, n_wq
    mov di, n_vq
    call ren
    mov si, n_v1
    mov di, n_dax
    call ren
    mov si, n_wq
    mov di, n_vq
    call ren
    call crlf

    ; --- 11: find first, next, next; then the DAX's directory entry -------
    mov si, s_11
    call puts
    mov si, n_vq
    call setname
    mov dx, dta
    mov ah, 0x1A
    int 0x21
    mov ah, 0x11
    call found
    mov ah, 0x12
    call found
    mov ah, 0x12
    call fcbal
    mov si, n_dax
    call setname
    mov ah, 0x11
    call fcbal
    mov al, [dta+0x0C]
    call hex2s
    mov ax, [dta+0x1F]
    call hex4
    mov ax, [dta+0x1D]
    call hex4
    call crlf

    ; --- X11: by extended FCB ---------------------------------------------
    mov si, s_x11
    call puts
    mov si, n_vq
    xor al, al
    call xname
    mov ah, 0x11
    call xcall
    mov si, dta
    mov cx, 8
.xb:
    lodsb
    call hex2
    loop .xb
    call space
    mov si, dta + 8
    call name11
    mov si, n_sub
    mov al, 0x10
    call xname
    mov ah, 0x11
    call xcall
    mov si, n_sub
    call setname
    mov ah, 0x11
    call fcbal
    call crlf

    ; --- XFC: a file made, found and removed by extended FCB --------------
    mov si, s_xfc
    call puts
    mov si, n_xfc
    xor al, al
    call xname
    mov ah, 0x16
    call xcall
    mov ah, 0x10
    call xcall
    mov si, n_xfc
    call setname
    mov ah, 0x11
    call fcbal
    mov si, n_xfc
    xor al, al
    call xname
    mov ah, 0x13
    call xcall
    mov ah, 0x11
    call xcall
    mov si, n_dax
    xor al, al
    call xname
    mov ah, 0x0F
    call xcall
    mov ax, [xfcb+7+0x0E]
    call hex4s
    mov ax, [xfcb+7+0x10]
    call hex4s
    mov ah, 0x10
    call xcall
    call crlf

    ; --- 13: tidy up ------------------------------------------------------
    mov si, s_13
    call puts
    mov si, n_vq
    call setname
    mov ah, 0x13
    call fcbal
    mov si, n_dax
    call setname
    mov ah, 0x13
    call fcbal
    call crlf

    mov si, s_done
    call puts
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

; --- psp: SI = the label; the entry AX and both FCBs (16 bytes, 12) ---------
psp:
    call puts
    mov ax, [entax]
    call hex4s
    mov si, 0x5C
    call pfcb
    mov si, 0x5C + 12
    mov cx, 4
.x:
    lodsb
    call hex2
    loop .x
    call space
    mov si, 0x6C
    call pfcb
    jmp crlf
pfcb:
    lodsb
    call hex2
    call space
name11:
    mov cx, 11
.n:
    lodsb
    call putc
    loop .n
    jmp space

; --- found: AH = 11h or 12h; AL, the drive byte and the name -----------------
found:
    call fcbal
    mov al, [dta]
    call hex2s
    mov si, dta + 1
    jmp short name11

; --- state: block, current, the random record, the size ---------------------
state:
    call bcr
    mov ax, [fcb+0x12]
    call hex4
    mov ax, [fcb+0x10]
    jmp hex4s
bcr:
    mov ax, [fcb+0x0C]
    call hex4s
    mov al, [fcb+0x20]
    call hex2s
    mov ax, [fcb+0x23]
    call hex4
    mov ax, [fcb+0x21]
    jmp hex4s

; --- fillrec: the DTA's first ten bytes to AL -------------------------------
fillrec:
    push cx
    mov di, dta
    mov cx, 10
    rep stosb
    pop cx
    ret

; --- mkempty: SI = a name; make it and close it -----------------------------
mkempty:
    call setname
    mov ah, 0x16
    call fcbal0
    mov ah, 0x10
    jmp fcbal0

; --- ren: SI -> DI by AH=17h; prints AL ------------------------------------
ren:
    push di
    call setname
    pop si
    mov di, fcb + 0x11
    mov cx, 11
    rep movsb
    mov ah, 0x17
    jmp fcbal

; --- setname: a fresh FCB on the current drive named by the 11 bytes at SI --
setname:
    mov di, fcb
    xor ax, ax
    stosb
    mov cx, 11
    rep movsb
    mov cx, 25
    rep stosb
    ret
; --- xname: SI = a name, AL = the attribute: an extended FCB ----------------
xname:
    mov di, xfcb
    mov byte [di], 0xFF
    inc di
    push ax
    xor ax, ax
    mov cx, 5
    rep stosb
    pop ax
    stosb
    xor al, al
    stosb
    mov cx, 11
    rep movsb
    mov cx, 25
    rep stosb
    ret
xcall:
    mov dx, xfcb
    jmp short fcbdx

; --- fcbal: AH on the FCB, then 'AL ' printed; fcbal0 prints nothing --------
fcbal:
    mov dx, fcb
fcbdx:
    int 0x21                        ; (the printing keeps CX: 27h/28h's
    jmp short hex2s                 ; answer is still there afterwards)
fcbal0:
    mov dx, fcb
    int 0x21
    ret

hex4s:
    call hex4
space:
    mov al, ' '
    jmp short putc
hex2s:
    call hex2
    jmp short space
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
hex2:
    push ax
    push cx
    mov cl, 4
    shr al, cl
    call .d
    pop cx
    pop ax
.d:
    and al, 0x0F
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
    mov si, s_crlf
puts:
    lodsb
    or al, al
    jz .o
    call putc
    jmp short puts
.o:
    ret

s_psp:  db 'PSP ', 0
s_kid:  db 'KID ', 0
s_dta:  db 'DTA ', 0
s_15:   db '15 ', 0
s_22:   db '22 ', 0
s_28:   db '28 ', 0
s_0f:   db '0F ', 0
s_27:   db '27 ', 0
s_23:   db '23 ', 0
s_17:   db '17 ', 0
s_ren:  db 'REN ', 0
s_11:   db '11 ', 0
s_x11:  db 'X11 ', 0
s_xfc:  db 'XFC ', 0
s_13:   db '13 ', 0
s_crlf: db 13, 10, 0
s_done: db 'DONE', 13, 10, 0
s_self: db 'FCBWR.COM', 0
n_dat:  db 'FCBWR   DAT'
n_dax:  db 'FCBWR   DAX'
n_none: db 'NOSUCH  XYZ'
n_w1:   db 'FCBW1   TMP'
n_w2:   db 'FCBW2   TMP'
n_wq:   db 'FCBW?   TMP'
n_vq:   db 'FCBV?   TMP'
n_v1:   db 'FCBV1   TMP'
n_sub:  db 'SUB        '
n_xfc:  db 'FCBXFC  TMP'
sizes:  dw 10, 7, 128, 0
tail:   db 4, ' /K ', 13
kfcb1:   db 2, 'KIDNAME TXT', 'abcd'
kfcb2:   db 0, 'OTHER   DAT', 'efgh'
parm:   dw 0, tail, 0, kfcb1, 0, kfcb2, 0
entax:  dw 0
sv_ss:  dw 0
sv_sp:  dw 0
fcb:    times 40 db 0
xfcb:   times 48 db 0
dta:    times 64 db 0
        times 256 db 0
endall:
