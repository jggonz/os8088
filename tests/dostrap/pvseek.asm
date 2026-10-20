; =============================================================================
; os8088 - tests/dostrap/pvseek.asm
;
; PVSEEK.COM - A SMALL READ AFTER A BIG ONE, WHICH IS WHERE PRINCE STOPS
;
;   nasm -f bin -w+error -o PVSEEK.COM tests/dostrap/pvseek.asm
;   PVSEEK                  PV.DAT, the name built in below
;   PVSEEK FILENAME.EXT     ...or one you type
;
; **THIS IS PRINCE OF PERSIA'S LAST FIVE CALLS, REPLAYED** (SPEC.md 96.11.5).
; Aligned against a real IBM DOS 3.30 on the same machine and the same disk,
; the two runs agree call for call - the same sites, the same registers, the
; same answers - for nine thousand calls, and then the game reads ONE BYTE and
; ours puts up *"Please insert Prince of Persia Disk into Drive B:"* while the
; real one carries on into the level.
;
; The sequence is:
;
;   open PV.DAT, read 6 at 0      the header
;   seek 61ABh, read 33Ah         the index, at the far end of the file
;   seek 0C15h, read 1            ...and ONE BYTE back near the start
;
; PV.DAT holds `E7` at 0C15h.  Under a real DOS the program's next register
; carries E7; under ours it carried 10, which is the byte four further on.
;
; So the four reads below are the same one byte asked for four different ways,
; and which of them disagree says which layer is wrong:
;
;   1  the full sequence, exactly as the game makes it;
;   2  the same one-byte read AGAIN, with nothing in between - a window that
;      is merely stale gives the right answer the second time;
;   3  the same read on a FRESH handle with no big read before it - this is
;      the control that says whether the big read is what does it;
;   4  eight bytes from the same offset, so a one-byte read being special
;      (a window refilled at the wrong base, a length rounded to zero) is
;      separated from the offset being wrong.
;
; Every read goes into a buffer poisoned with EEh and the SPAN written is
; printed beside the count DOS reported, for rdsmall.asm's reason.
;
; It runs unchanged under a real DOS (docs/DOS-DEBUGGING.md's rule), so every
; line is a comparison rather than an assertion about ourselves.
; OURS, MIT with the rest of the tree.
; =============================================================================
    org 0x100
    cpu 8086
BIGOFF  equ 0x61AB                  ; the index, at the far end of PV.DAT
BIGLEN  equ 0x033A
RECLEN  equ 0x0064                  ; the record Prince checksums
SMOFF   equ 0x0C15                  ; ...and the byte the game reads next
start:
    cmp byte [0x80], 0
    je .have
    mov si, 0x81
.skip:
    lodsb
    cmp al, ' '
    je .skip
    cmp al, 9
    je .skip
    dec si
    mov di, name
.cp:
    lodsb
    cmp al, 13
    je .end
    cmp al, ' '
    je .end
    stosb
    jmp short .cp
.end:
    xor al, al
    stosb
.have:
    mov si, s_hdr
    call puts
    mov si, name
    call puts
    call eol

    call openit
    jc .no

    ; --- 1: the game's own sequence ------------------------------------------
    mov cx, 6
    xor dx, dx
    call seekrd                     ; the header
    mov si, s_1
    call puts
    mov ax, BIGOFF
    call bigread
    mov ax, SMOFF
    mov cx, 1
    call one
    call report

    ; --- 2: the same byte again, nothing in between --------------------------
    mov si, s_2
    call puts
    mov ax, SMOFF
    mov cx, 1
    call one
    call report

    ; --- 3: a fresh handle, no big read --------------------------------------
    call closeit
    call openit
    jc .no
    mov si, s_3
    call puts
    mov ax, SMOFF
    mov cx, 1
    call one
    call report

    ; --- 4: eight bytes from the same offset, after the big read -------------
    call closeit
    call openit
    jc .no
    mov cx, 6
    xor dx, dx
    call seekrd
    mov ax, BIGOFF
    call bigread
    mov si, s_4
    call puts
    mov ax, SMOFF
    mov cx, 8
    call one
    call report

    ; --- 5: THE GAME'S OWN TEST - the checksum byte, then the record ---------
    ; `PRINCE.EXE` at CS-load:A4D0 seeks, reads ONE byte into `[bp-1]`, then
    ; reads the record SEQUENTIALLY - no seek between - sums it, complements
    ; and compares.  Test 1 above proves the one byte is right; this is the
    ; read that FOLLOWS it, and a position advanced wrongly by a one-byte read
    ; puts the record one place out with the count still correct.
    call closeit
    call openit
    jc .no
    mov cx, 6
    xor dx, dx
    call seekrd
    mov ax, BIGOFF
    call bigread
    mov si, s_5
    call puts
    mov ax, SMOFF
    mov cx, 1
    call one                        ; the checksum byte, into buf
    mov al, [buf]
    mov [want8], al
    mov bx, [fh]                    ; ...and the record, straight on
    mov ah, 0x3F
    mov cx, RECLEN
    mov dx, rec
    int 0x21
    mov [got], ax
    mov dh, 0                       ; Prince's own sum: add, then NOT
    mov si, rec
    mov cx, RECLEN
.sum:
    lodsb
    add dh, al
    loop .sum
    not dh
    mov [gotck], dh
    mov si, s_want
    call puts
    mov al, [want8]
    call hex2
    mov si, s_calc
    call puts
    mov al, [gotck]
    call hex2
    mov si, s_ax
    call puts
    mov ax, [got]
    call hexw
    mov si, s_by
    call puts
    mov si, rec
    mov cx, 8
.b5:
    lodsb
    push cx
    call hex2
    mov al, ' '
    call putc
    pop cx
    loop .b5
    call eol


    ; --- 6: ...WITH THE DISK CHECK IN THE MIDDLE, WHICH IS THE WHOLE POINT ----
    ; Prince of Persia's real sequence has one more call in it, and tests 1-5
    ; leave it out: between the big read and the seek it asks `AH=4Eh` with the
    ; VOLUME-LABEL mask - its check that the floppy is its own (SPEC.md
    ; 96.12.1.1), which it makes after every single open.  Tests 1-5 pass on
    ; both DOSes; this is the one that does not.
    call closeit
    call openit
    jc .no
    mov cx, 6
    xor dx, dx
    call seekrd
    mov ax, BIGOFF
    call bigread
    call findlabel                  ; <-- the only difference from test 5
    mov si, s_6
    call puts
    call byterec

    ; --- 7: the same, with the check AFTER the seek instead of before --------
    ; Which half does it break - the POSITION the seek set, or the window the
    ; read would have come out of?  If 6 fails and 7 passes the seek is being
    ; undone; if both fail it is the window.
    call closeit
    call openit
    jc .no
    mov cx, 6
    xor dx, dx
    call seekrd
    mov ax, BIGOFF
    call bigread
    mov si, s_7
    call puts
    mov ax, SMOFF                   ; seek FIRST...
    mov cx, 1
    call one
    call findlabel                  ; ...then the check, then read on
    mov al, [buf]
    call chkrec


    ; --- 8: PRINCE'S FOUR VISITS, IN ORDER ----------------------------------
    ; Tests 1-7 all pass on both DOSes and the game still dies here, so what
    ; they leave out is the HISTORY: Prince opens PV.DAT four times in a row,
    ; and it is the FOURTH that reads the wrong bytes.  The three before it are
    ; the state, and this replays them - open, header, index, disk check, seek,
    ; checksum byte, record, close - at the offsets the game really uses.
    mov si, s_8
    call puts
    call eol
    cmp byte [churn], 0
    je .nochurn
    mov si, n_kid                   ; **AND THE CACHE'S OWN HISTORY**: the box
    call churnfile                  ; reads ahead into a window that outlives
    mov si, n_title                 ; the handle, so a file read in ISOLATION
    call churnfile                  ; exercises an empty cache and a file read
.nochurn:                           ; after two others does not
    mov word [visit], 0
.v:
    call closeit
    call openit
    jc .no
    mov cx, 6
    xor dx, dx
    call seekrd
    mov ax, BIGOFF
    call bigread
    call findlabel
    mov bx, [visit]
    add bx, bx
    mov ax, [visits + bx]
    push ax
    mov si, s_at
    call puts
    mov ax, [visits + bx]
    call hexw
    mov si, s_sp2
    call puts
    pop ax
    mov cx, 1
    call one
    mov al, [buf]
    call chkrec
    inc word [visit]
    cmp word [visit], 4
    jb .v


    ; --- 9: ...AND WITH THE TOP OF MEMORY SCRIBBLED ON -----------------------
    ; The box keeps its read window in a segment of its own, and on this
    ; machine that segment is `PSP:0002` - the first paragraph past the
    ; program.  A .COM never goes near it; a game that fills memory does.  So
    ; this writes 8KB of AAh from there and repeats test 8: if the visits now
    ; read the wrong bytes, the window is inside what a program can reach and
    ; the bytes it gives back are the PROGRAM's.
    push ds
    mov ax, [0x0002]                ; PSP:0002 - the first paragraph past us
    mov es, ax
    xor di, di
    mov cx, 0x1000
    mov ax, 0xAAAA
    cld
    rep stosw
    pop ds
    mov si, s_9
    call puts
    call eol
    mov byte [churn], 0
    mov word [visit], 0
.v9:
    call closeit
    call openit
    jc .no
    mov cx, 6
    xor dx, dx
    call seekrd
    mov ax, BIGOFF
    call bigread
    call findlabel
    mov bx, [visit]
    add bx, bx
    mov ax, [visits + bx]
    push ax
    mov si, s_at
    call puts
    mov ax, [visits + bx]
    call hexw
    mov si, s_sp2
    call puts
    pop ax
    mov cx, 1
    call one
    mov al, [buf]
    call chkrec
    inc word [visit]
    cmp word [visit], 4
    jb .v9

    call closeit
    jmp short .fin
.no:
    mov si, s_noopen
    call puts
.fin:
    mov si, s_rdy
    call puts
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C2C
    int 0x21

; --- findlabel - Prince's own disk check: FindFirst with the LABEL mask ------
findlabel:
    push ax
    push cx
    push dx
    mov ah, 0x4E
    mov cx, 0x0008                  ; VOLUME LABEL and nothing else
    mov dx, wild
    int 0x21
    pop dx
    pop cx
    pop ax
    ret

; --- byterec - seek, read the checksum byte, read the record, compare --------
byterec:
    mov ax, SMOFF
    mov cx, 1
    call one
    mov al, [buf]
chkrec:
    mov [want8], al
    mov bx, [fh]
    mov ah, 0x3F
    mov cx, RECLEN
    mov dx, rec
    int 0x21
    mov [got], ax
    mov dh, 0
    mov si, rec
    mov cx, RECLEN
.s:
    lodsb
    add dh, al
    loop .s
    not dh
    mov [gotck], dh
    mov si, s_want
    call puts
    mov al, [want8]
    call hex2
    mov si, s_calc
    call puts
    mov al, [gotck]
    call hex2
    mov si, s_ax
    call puts
    mov ax, [got]
    call hexw
    mov si, s_by
    call puts
    mov si, rec
    mov cx, 8
.b:
    lodsb
    push cx
    call hex2
    mov al, ' '
    call putc
    pop cx
    loop .b
    jmp eol

; --- churnfile - read a whole file past, to leave the cache as a game does ---
churnfile:
    push ax
    push bx
    push cx
    push dx
    push si
    mov di, name2
    push si
.cn:
    lodsb
    stosb
    or al, al
    jnz .cn
    pop si
    mov ax, 0x3D00
    mov dx, name2
    int 0x21
    jc .cdone
    mov bx, ax
.cloop:
    mov ah, 0x3F
    mov cx, BIGLEN
    mov dx, big
    int 0x21
    jc .cclose
    or ax, ax
    jz .cclose
    jmp short .cloop
.cclose:
    mov ah, 0x3E
    int 0x21
.cdone:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- openit / closeit --------------------------------------------------------
openit:
    mov ax, 0x3D00
    mov dx, name
    int 0x21
    jc .o
    mov [fh], ax
    clc
.o:
    ret
closeit:
    mov bx, [fh]
    mov ah, 0x3E
    int 0x21
    ret

; --- seekrd - seek to DX:0 and read CX into buf, no reporting ----------------
seekrd:
    push cx
    push dx
    mov bx, [fh]
    mov ax, 0x4200
    xor cx, cx
    int 0x21
    pop dx
    pop cx
    mov bx, [fh]
    mov ah, 0x3F
    mov dx, buf
    int 0x21
    ret

; --- bigread - seek to AX and read BIGLEN into big ---------------------------
bigread:
    mov dx, ax
    mov bx, [fh]
    mov ax, 0x4200
    xor cx, cx
    int 0x21
    mov bx, [fh]
    mov ah, 0x3F
    mov cx, BIGLEN
    mov dx, big
    int 0x21
    ret

; --- one - poison, seek to AX, read CX into buf; banks AX and the span -------
one:
    push ax
    push cx
    mov di, buf                     ; POISON, for rdsmall.asm's reason: a read
    mov cx, 32                      ; that writes MORE than it was asked for is
    mov al, 0xEE                    ; invisible in the count and wrecks the
    push es                         ; caller's next variable
    push ds
    pop es
    cld
    rep stosb
    pop es
    pop cx
    pop ax
    mov [want], cx
    mov dx, ax
    mov bx, [fh]
    mov ax, 0x4200
    xor cx, cx
    int 0x21
    mov [pos], ax
    mov bx, [fh]
    mov ah, 0x3F
    mov cx, [want]
    mov dx, buf
    int 0x21
    mov [got], ax
    mov word [span], 0
    mov si, buf
    mov cx, 32
    xor bx, bx
.sp:
    lodsb
    cmp al, 0xEE
    je .nx
    mov [span], bx
    inc word [span]
.nx:
    inc bx
    loop .sp
    ret

; --- report - pos, AX, span and the first eight bytes ------------------------
report:
    mov si, s_pos
    call puts
    mov ax, [pos]
    call hexw
    mov si, s_ax
    call puts
    mov ax, [got]
    call hexw
    mov si, s_span
    call puts
    mov ax, [span]
    call hexw
    mov si, s_by
    call puts
    mov si, buf
    mov cx, 8
.b:
    lodsb
    push cx
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    pop ax
    call hexd
    mov al, ' '
    call putc
    pop cx
    loop .b
    call eol
    ret

hex2:
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    pop ax
    call hexd
    ret
hexw:
    push ax
    push ax
    mov al, ah
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    pop ax
    mov al, ah
    call hexd
    pop ax
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    pop ax
    call hexd
    ret
hexd:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
putc:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp putc
puts:
    lodsb
    or al, al
    jz .o
    call putc
    jmp short puts
.o:
    ret

s_hdr:    db 'os8088 pv seek gate - ', 0
s_1:      db '1 big then one   ', 0
s_2:      db '2 one again      ', 0
s_3:      db '3 fresh, no big  ', 0
s_4:      db '4 big then eight ', 0
s_5:      db '5 byte then rec  ', 0
s_6:      db '6 +label check   ', 0
s_7:      db '7 check AFTER sk ', 0
s_8:      db '8 the four visits', 0
s_9:      db '9 top of RAM scribbled, then the four again', 0
s_at:     db '   at ', 0
s_sp2:    db ' ', 0
visits:   dw 0x3986, 0x5AC0, 0x0006, 0x0C15
wild:     db 'B:????????.???', 0
s_want:   db 'ck=', 0
s_calc:   db ' calc=', 0
s_pos:    db 'pos=', 0
s_ax:     db ' AX=', 0
s_span:   db ' span=', 0
s_by:     db '  ', 0
s_noopen: db 'OPEN FAILED', 13, 10, 0
s_rdy:    db 'READY - press a key to exit with code 44', 13, 10, 0
fh:       dw 0
want:     dw 0
pos:      dw 0
got:      dw 0
span:     dw 0
visit:    dw 0
churn:    db 1
n_kid:    db 'KID.DAT', 0
n_title:  db 'TITLE.DAT', 0
name2:    times 16 db 0
want8:    db 0
gotck:    db 0
name:     db 'PV.DAT', 0
          times 10 db 0
buf:      times 64 db 0
big:      times BIGLEN db 0
rec:      times RECLEN db 0
