; SEQCOST.COM - what a DOS program's sequential READ and WRITE cost, by
; POSITION in the file (docs/plans/completed/DOS-STREAM-PLAN.md W0).
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; diskcost.asm counts int 13h from outside; this one times from INSIDE, in
; BIOS ticks (int 1Ah, 54.9 ms), because the cost it is after is the CPU the
; OS spends walking a file's cluster chain - which grows with the offset and
; touches no drive once the FAT is cached. So what it prints is the time of
; each BLOCK of chunks, and the shape of those numbers is the finding:
;
;   flat      the layer steps from where it was (a real DOS; a stream)
;   rising    it walks from the front every call (READ_AT, APPEND)
;
; Three phases, one file, BIGSEQ.DAT, created where the program stands:
;
;   W   NCH chunks of CHUNK bytes, chunk k filled with byte k
;   R   the same NCH chunks read back, first and last byte of each checked
;   S   NSEEK pairs of (seek to the last chunk, read it; seek to 0, read it)
;
; Every line is "<phase> <block> <ticks>" and a total per phase. It runs
; under a real DOS unchanged, which is the reference. The file is left on
; the disk, so the host can check it with an independent reader.
;
; It waits for a key at the end for diskcost.asm's reason: under os8088 the
; desktop comes straight back when the program ends.

    org 0x100
    cpu 8086

%ifndef NCH
%define NCH 32                  ; 256 KB at 8 KB: fits a 360 KB floppy
%endif
%ifndef CHUNK
%define CHUNK 8192
%endif
%ifndef BLK
%define BLK 8                   ; chunks a timing line
%endif
%ifndef NSEEK
%define NSEEK 4
%endif

start:
    mov dx, s_hello
    call puts
    ; --- W ---------------------------------------------------------------
    mov ah, 0x3C
    xor cx, cx
    mov dx, fname
    int 0x21
    mov byte [stage], 'C'
    jc fail
    mov [fh], ax
    mov byte [ph], 'W'
    call tstart
    xor bp, bp                  ; BP = chunk index
.wl:
    mov ax, bp
    mov di, buf
    mov cx, CHUNK
    cld
    rep stosb                   ; chunk k is byte k, everywhere
    mov ah, 0x40
    mov bx, [fh]
    mov cx, CHUNK
    mov dx, buf
    int 0x21
    mov byte [stage], 'W'
    jc fail
    cmp ax, CHUNK
    jne fail
    inc bp
    call tick
    cmp bp, NCH
    jb .wl
    mov ah, 0x3E
    mov bx, [fh]
    int 0x21
    mov byte [stage], 'X'
    jc fail
    call tend
    ; --- R ---------------------------------------------------------------
    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    mov byte [stage], 'O'
    jc fail
    mov [fh], ax
    mov byte [ph], 'R'
    call tstart
    xor bp, bp
.rl:
    call rdchk
    inc bp
    call tick
    cmp bp, NCH
    jb .rl
    call tend
    ; --- S ---------------------------------------------------------------
    mov byte [ph], 'S'
    call tstart
    mov word [nsk], NSEEK
.sl:
    mov bp, NCH - 1
    call seekrd
    xor bp, bp
    call seekrd
    dec word [nsk]
    jnz .sl
    call tend
    mov ah, 0x3E
    mov bx, [fh]
    int 0x21
    mov dx, s_ready
    call puts
    mov ah, 0x08                ; a key, through DOS: every host answers it
    int 0x21
    mov ax, 0x4C00
    int 0x21

; seekrd - AH=42h to chunk BP, then rdchk
seekrd:
    mov ax, bp
    mov cx, CHUNK
    mul cx                      ; DX:AX = the offset
    mov cx, dx
    mov dx, ax
    mov ax, 0x4200
    mov bx, [fh]
    int 0x21
    mov byte [stage], 'K'
    jc fail
    ; fall through

; rdchk - read one chunk and check it is chunk BP's
rdchk:
    mov ah, 0x3F
    mov bx, [fh]
    mov cx, CHUNK
    mov dx, buf
    int 0x21
    mov byte [stage], 'R'
    jc fail
    cmp ax, CHUNK
    jne fail
    mov ax, bp
    mov byte [stage], 'V'
    cmp [buf], al
    jne fail
    cmp [buf + CHUNK - 1], al
    jne fail
    ret

; --- timing ---------------------------------------------------------------
now:                            ; out AX = the tick's low word
    push cx
    push dx
    xor ah, ah
    int 0x1A
    mov ax, dx
    pop dx
    pop cx
    ret

tstart:
    call now
    mov [t0], ax
    mov [tb], ax
    ret

; tick - after chunk BP-1: a line every BLK chunks
tick:
    mov ax, bp
    xor dx, dx
    mov cx, BLK
    div cx
    or dx, dx
    jnz .no
    push ax                     ; the block number, 1-based
    call now
    mov cx, ax
    sub ax, [tb]
    mov [tb], cx
    mov [tv], ax
    mov dl, [ph]
    call putc
    mov dl, ' '
    call putc
    pop ax
    call putn
    mov dl, ' '
    call putc
    mov ax, [tv]
    call putn
    call crlf
.no:
    ret

tend:
    call now
    sub ax, [t0]
    mov [tv], ax
    mov dl, [ph]
    call putc
    mov dx, s_total
    call puts
    mov ax, [tv]
    call putn
    call crlf
    ret

; --- output ---------------------------------------------------------------
fail:
    mov dx, s_fail
    call puts
    mov dl, [stage]
    call putc
    mov dl, ' '
    call putc
    mov ax, bp
    call putn
    call crlf
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C01
    int 0x21

putc:                           ; DL
    push ax
    mov ah, 0x02
    int 0x21
    pop ax
    ret

puts:                           ; DX -> '$'-terminated
    push ax
    mov ah, 0x09
    int 0x21
    pop ax
    ret

crlf:
    push dx
    mov dl, 13
    call putc
    mov dl, 10
    call putc
    pop dx
    ret

putn:                           ; AX, unsigned decimal
    push ax
    push bx
    push cx
    push dx
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
    call putc
    loop .p
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fname:   db 'BIGSEQ.DAT', 0
s_hello: db 'SEQCOST: ticks per block', 13, 10, '$'
s_total: db ' total $'
s_ready: db 'READY', 13, 10, '$'
s_fail:  db 'FAILED at $'
fh:      dw 0
nsk:     dw 0
t0:      dw 0
tb:      dw 0
tv:      dw 0
ph:      db 0
stage:   db 0
buf:                            ; NOT emitted: a .COM owns the bytes after
                                ; its image (diskcost.asm's first trap)
