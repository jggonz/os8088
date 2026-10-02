; =============================================================================
; os88cz.asm - OS88CZ.COM, the split set and the 'CZ' file on MS-DOS
; (SPEC.md 20.14, 20.17)
;
; The DOS twin of tools/os88cz.py, for the machine at either end of a disk
; walk that has DOS on it rather than os8088 or a Python:
;
;   OS88CZ J PART [DEST]        join a split set from any of its parts. A part
;                               that is not there is ASKED FOR - "insert the
;                               disk with CLIP.002" - so a set can be joined
;                               straight off a pile of floppies onto a hard
;                               disk, which the os8088 verb cannot do yet
;                               (SPEC.md 22.23.5). Never on the drive the
;                               result is going to: that disk cannot go out
;   OS88CZ S FILE [SIZE] [DEST] [/S] [/P]
;                               split FILE into NAME.001 ... of SIZE (360,
;                               720, 1200, 1440 - a fresh disk of that size -
;                               or bytes; 720 by default), LZ4 where a block
;                               gets smaller and stored where it does not. /S
;                               stores every block; /P stops before each part
;                               so a fresh floppy can go in
;   OS88CZ U FILE OUT           expand a 'CZ' file (LZ4 or LZB) - one the
;                               machine's own Compress wrote, say. Whole, so
;                               it must fit in memory
;
; **THE DECODER IS THE KERNEL'S**: kernel/lz.inc is %included as it is. It
; touches no kernel data (its own banner says so), which is why it can: the
; Video Player's build and kern_dos include it the same way, so there is one
; decoder in the tree and three hosts.
;
; **THE ENCODER IS THIS FILE'S**, and it only has to be VALID, not the
; reference's twin: a greedy LZ4 parse over one 32KB block with a 4,096-slot
; hash of four-byte prefixes, one candidate, no chains. What it must get right
; is SPEC.md 20.13.7's raw tail - the stream is cut at the FIRST peak of
; (produced - consumed), tracked exactly as os88lz._cut tracks it - because a
; stream with any other cut still decodes on the host and would overrun an
; in-place expansion on the machine. tests/unit/t_cz.py runs this program's
; parse under an 8086 model... no: it runs the MIRROR of it (`_dos_lz4` in
; that file) and requires the program's output on a fixed corpus to equal it
; byte for byte, through QEMU when the row has it.
;
; MEMORY: the program and its stack are one 64KB segment, and three buffers
; follow it - A (a block's source or a record's payload), B (the encoder's
; output, or a block expanded) with the hash table at B:9000h, and for U the
; whole file from A onwards. 192KB of conventional memory is enough for J and
; S; U wants the packed and the unpacked file at once.
;
; Assemble: nasm -f bin -w+error -I kernel/ -o OS88CZ.COM dostools/os88cz.asm
; =============================================================================

    cpu 8086
    org 0x100

section .text
section .cold follows=.text
section .bss follows=.cold nobits

BLOCK       equ 0x8000          ; every block but the set's last expands to it
CS_HDR      equ 32              ; a part's header (SPEC.md 20.17.1)
REC_HDR     equ 10              ; a block's (20.17.2)
MAXPARTS    equ 999
MAXBLK      equ 4096            ; blocks a split can count: 128MB
HT          equ 0x9000          ; the encoder's hash table, in buffer B
MINLEN      equ 12              ; LZ4: no match starts in the last 12...
LASTLITS    equ 5               ; ...and the last 5 are literals
PSP_TOP     equ 2               ; the PSP's top-of-memory segment
MAXARG      equ 64              ; an argument's characters, at most

section .text

start:
    cld
    mov di, bss_start           ; DOS does not zero a .COM's .bss
    mov cx, bss_end - bss_start
    xor al, al
    rep stosb
    mov ax, cs
    add ax, 0x1000
    mov [bufa], ax
    add ax, 0x1000
    mov [bufb], ax
    add ax, 0x1000
    cmp [PSP_TOP], ax           ; A and B and a margin
    jae .mem
    mov dx, s_nomem
    jmp fatal
.mem:
    call parse
    mov al, [cmd]
    cmp al, 'J'
    jne .s
    jmp do_join
.s:
    cmp al, 'S'
    jne .u
    jmp do_split
.u:
    cmp al, 'U'
    jne .use
    jmp do_unpack
.use:
    mov dx, s_usage
    jmp fatal

; -----------------------------------------------------------------------------
; parse - the command tail into [cmd], up to three arguments (arg1..arg3, NUL
;         strings in argbuf) and the flags /S and /P
; -----------------------------------------------------------------------------
parse:
    mov si, 0x81
    mov cl, [0x80]
    xor ch, ch
    mov bx, si
    add bx, cx
    mov byte [bx], 0            ; the tail NUL-ended where DOS left a CR
    mov di, argbuf
    xor bp, bp                  ; arguments seen, the command included
.skip:
    lodsb
    or al, al
    jz .done
    cmp al, ' '
    je .skip
    cmp al, 9
    je .skip
    cmp al, '/'
    jne .word
    lodsb                       ; a flag
    and al, 0xDF
    cmp al, 'S'
    jne .p
    mov byte [fstore], 1
    jmp short .skip
.p:
    cmp al, 'P'
    jne .bad
    mov byte [fpause], 1
    jmp short .skip
.bad:
    mov dx, s_usage
    jmp fatal
.word:
    or bp, bp
    jnz .arg
    and al, 0xDF                ; the command letter
    mov [cmd], al
    inc bp
.cmdrest:                       ; ...and anything glued to it is ignored
    lodsb
    or al, al
    jz .done
    cmp al, ' '
    jne .cmdrest
    jmp short .skip
.arg:
    cmp bp, 4
    jae .bad
    mov bx, bp
    dec bx
    shl bx, 1
    mov [args+bx], di
    inc bp
    xor cx, cx                  ; its length: 64, DOS's own path limit, is
.copy:                          ; what every path buffer below is sized from
    inc cx
    cmp cx, MAXARG
    ja .bad
    cmp al, 'a'                 ; upper-case: DOS names are
    jb .st
    cmp al, 'z'
    ja .st
    sub al, 0x20
.st:
    stosb
    lodsb
    or al, al
    jz .end
    cmp al, ' '
    je .end
    cmp al, '/'
    je .endf
    jmp short .copy
.endf:
    dec si                      ; a flag glued to a word: parse it next
.end:
    mov byte [di], 0
    inc di
    or al, al
    jnz .skip
.done:
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; the join (SPEC.md 22.23.5's checks, DOS's I/O)
; -----------------------------------------------------------------------------
do_join:
    mov si, [args]
    or si, si
    jnz .have
    mov dx, s_usage
    jmp fatal
.have:
    mov di, ppath               ; the part's path, whose digits get rewritten
    call strcpy
    mov si, ppath
    call digits                 ; SI -> the three digits, or CF
    jnc .isp
    mov dx, s_notpart
    jmp fatal
.isp:
    mov [pdig], si
    mov si, [args+2]            ; DEST, with a separator after it
    mov di, dpath
    call dirof
    mov [dend], di
    xor ax, ax
    mov [done], ax
    mov [done+2], ax
    mov word [k], 0
    mov word [nparts], 1
.part:
    mov ax, [k]
    inc ax
    cmp ax, [nparts]
    jbe .next
    jmp .end
.next:
    mov [k], ax
    call setdig
.open:
    mov dx, ppath
    mov ax, 0x3D00
    int 0x21
    jnc .opened
    cmp word [hout], 0          ; **NOT ON THE RESULT'S DRIVE** once it is
    je .ask                     ; open: os8088 disks share one serial and one
    mov si, ppath               ; label, and a drive with no change line
    call drvof                  ; cannot see a swap at all, so DOS would put
    mov bl, al                  ; the first disk's FAT and the growing entry
    mov si, dpath               ; on the parts disk. The window's join
    call drvof                  ; refuses the same case (cmz_jfloppy)
    cmp al, bl
    jne .ask
    mov dx, s_missing
    call puts
    mov dx, ppath
    call putz
    mov dx, s_onout
    jmp jfail
.ask:
    mov dx, s_insert            ; not there: ask for the disk it is on
    call puts
    mov dx, ppath
    call putz
    mov dx, s_presskey
    call puts
    mov ah, 0x08
    int 0x21
    push ax
    mov dx, s_crlf
    call puts
    pop ax
    cmp al, 27
    jne .open
    mov dx, s_stopped
    jmp jfail
.opened:
    mov [hin], ax
    mov bx, ax                  ; --- the part header -------------------
    mov cx, CS_HDR
    mov dx, hdr
    mov ah, 0x3F
    int 0x21
    jc .rderr
    cmp ax, CS_HDR
    jne .notcs
    cmp word [hdr], 'CS'
    jne .notcs
    cmp word [hdr+2], 1
    jne .notcs
    mov ax, [hdr+4]
    cmp ax, [k]
    jne .wrong
    cmp ax, 1
    jne .later
    mov ax, [hdr+6]             ; part 1 says what the set is
    dec ax
    cmp ax, MAXPARTS - 1
    ja .notcs
    inc ax
    mov [nparts], ax
    mov ax, [hdr+8]
    mov [total], ax
    mov dx, [hdr+10]
    mov [total+2], dx
    or ax, dx
    jz .notcs
    mov ax, [hdr+16]
    mov [setid], ax
    mov ax, [hdr+18]
    mov [setid+2], ax
    call jcreate                ; the result's name free; OS88CZ.$$$ open
    jmp short .offs
.later:
    mov ax, [hdr+6]
    cmp ax, [nparts]
    jne .wrong
    mov si, hdr+8
    mov di, total
    mov cx, 2
    repe cmpsw
    jne .wrong
    mov si, hdr+16
    mov di, setid
    mov cx, 2
    repe cmpsw
    jne .wrong
.offs:
    mov ax, [hdr+12]
    cmp ax, [done]
    jne .wrong
    mov ax, [hdr+14]
    cmp ax, [done+2]
    jne .wrong
    mov byte [anyrec], 0
.rec:                           ; --- one block ---------------------------
    mov bx, [hin]
    mov cx, REC_HDR
    mov dx, rec
    mov ah, 0x3F
    int 0x21
    jc .rderr
    or ax, ax
    jz .pend                    ; the part is over
    cmp ax, REC_HDR
    jne .bad
    mov cx, [rec]               ; S
    mov dx, [rec+2]             ; N
    mov ax, [rec+4]             ; M and the zero
    mov bx, cx
    dec bx
    cmp bx, BLOCK - 1
    ja .bad
    mov bx, dx
    dec bx
    cmp bx, BLOCK - 1
    ja .bad
    cmp ax, 2
    ja .bad
    or ax, ax
    jnz .comp
    cmp cx, dx
    jne .bad
.comp:
    test word [done], BLOCK - 1
    jnz .bad                    ; only the set's last block is short
    mov bx, [done]
    mov si, [done+2]
    add bx, dx
    adc si, 0
    cmp si, [total+2]
    ja .bad
    jb .fits
    cmp bx, [total]
    ja .bad
.fits:
    push ds                     ; the payload, into A
    mov bx, [hin]
    mov ds, [bufa]
    xor dx, dx
    mov ah, 0x3F
    int 0x21
    pop ds
    jc .rderr
    cmp ax, [rec]
    jne .bad                    ; the part ends inside a block
    mov ax, [bufa]
    cmp byte [rec+4], 0
    je .have2
    push ds                     ; expand it into B
    mov ds, [bufa]
    xor si, si
    mov es, [cs:bufb]
    xor di, di
    xor bx, bx
    mov cx, [cs:rec]
    mov dx, [cs:rec+2]
    mov al, [cs:rec+4]
    dec al
    call lz_decomp_x
    pop ds
    push cs
    pop es
    jc .bad
    mov ax, [bufb]
.have2:
    mov [dseg], ax
    push ds
    mov ds, ax
    xor si, si
    mov cx, [cs:rec+2]
    call csum                   ; AX = A, DX = B
    pop ds
    cmp ax, [rec+6]
    jne .bad
    cmp dx, [rec+8]
    jne .bad                    ; a damaged copy
    push ds                     ; out it goes
    mov bx, [hout]
    mov cx, [rec+2]
    mov ds, [dseg]
    xor dx, dx
    mov ah, 0x40
    int 0x21
    pop ds
    jc .wrerr
    cmp ax, [rec+2]
    jne .full
    add [done], ax
    adc word [done+2], 0
    mov byte [anyrec], 1
    mov al, '.'
    call putc
    jmp .rec
.pend:
    cmp byte [anyrec], 0
    je .bad                     ; a part with no block in it
    mov bx, [hin]
    mov ah, 0x3E
    int 0x21
    mov word [hin], 0
    jmp .part
.end:
    mov ax, [done]
    cmp ax, [total]
    jne .bad
    mov ax, [done+2]
    cmp ax, [total+2]
    jne .bad
    mov bx, [hout]
    mov ah, 0x3E
    int 0x21
    mov word [hout], 0
    mov dx, tpath               ; OS88CZ.$$$ takes the name
    mov di, fpath
    mov ah, 0x56
    int 0x21
    jc .wrerr
    mov dx, s_crlf
    call puts
    mov dx, fpath
    call putz
    mov dx, s_joined
    call puts
    mov ax, [total]
    mov dx, [total+2]
    call putu32
    mov dx, s_bytes
    call puts
    mov ax, 0x4C00
    int 0x21
.notcs:
    cmp word [k], 1
    ja .wrong
    mov dx, s_notpart
    jmp jfail
.wrong:
    mov dx, s_wrong
    call puts
    mov dx, ppath
    call putz
    mov dx, s_crlf
    jmp jfail
.bad:
    mov dx, s_bad
    jmp jfail
.rderr:
    mov dx, s_rderr
    jmp jfail
.wrerr:
    mov dx, s_wrerr
    jmp jfail
.full:
    mov dx, s_full
    ; ...into jfail

; jfail - say DX, close both files, delete OS88CZ.$$$, stop
jfail:
    call puts
    mov bx, [hin]
    or bx, bx
    jz .ni
    mov ah, 0x3E
    int 0x21
.ni:
    mov bx, [hout]
    or bx, bx
    jz .no
    mov ah, 0x3E
    int 0x21
    mov dx, tpath
    mov ah, 0x41
    int 0x21
.no:
    mov ax, 0x4C01
    int 0x21

; jcreate - fpath = DEST + the set's name, which must not exist; tpath =
;           DEST + OS88CZ.$$$, created and open as [hout]
jcreate:
    mov si, hdr+20              ; the name came off a floppy: an 8.3 NAME and
    mov di, sname               ; nothing else, or "\CONFIG.SYS" and
    mov cx, 12                  ; "..\X.BAT" land outside DEST
.n:
    lodsb
    or al, al
    jz .nd
    stosb
    loop .n
.nd:
    mov byte [di], 0
    mov si, sname
    call check83
    jnc .ok83
    mov dx, s_notpart
    jmp jfail
.ok83:
    mov si, dpath
    mov di, fpath
    call strcpy
    dec di
    mov si, sname
    call strcpy
    mov dx, fpath
    mov ax, 0x3D00
    int 0x21
    jc .free
    mov bx, ax
    mov ah, 0x3E
    int 0x21
    mov dx, fpath
    call putz
    mov dx, s_exists
    jmp jfail
.free:
    mov si, dpath
    mov di, tpath
    call strcpy
    dec di
    mov si, s_tmp
    call strcpy
    mov dx, tpath
    xor cx, cx
    mov ah, 0x3C
    int 0x21
    jnc .ok
    mov dx, s_wrerr
    jmp jfail
.ok:
    mov [hout], ax
    ret

; -----------------------------------------------------------------------------
; the split
; -----------------------------------------------------------------------------
do_split:
    mov si, [args]
    or si, si
    jnz .have
    mov dx, s_usage
    jmp fatal
.have:
    mov dx, si
    mov ax, 0x3D00
    int 0x21
    jnc .op
    mov dx, s_noopen
    jmp fatal
.op:
    mov [hin], ax
    mov bx, ax
    mov ax, 0x4202              ; its size
    xor cx, cx
    xor dx, dx
    int 0x21
    mov [total], ax
    mov [total+2], dx
    or ax, dx
    jnz .nz
    mov dx, s_empty
    jmp fatal
.nz:
    mov ax, 0x4200              ; ...and back to its start for pass 1
    xor cx, cx
    xor dx, dx
    int 0x21
    mov si, [args]              ; --- the name it rejoins as -----------
    call basename               ; SI -> after the last \ or :
    mov di, sname
    mov cx, 13
.cn:
    lodsb
    stosb
    or al, al
    jz .cnd
    loop .cn
    mov dx, s_name83
    jmp fatal
.cnd:
    mov si, sname
    call check83                ; CF = not an 8.3 name, or a part's
    jnc .n83
    mov dx, s_name83
    jmp fatal
.n83:
    mov word [cap], 0           ; --- the part size --------------------
    mov word [cap+2], 0
    mov si, [args+2]
    or si, si
    jz .d720
    call atou32                 ; DX:AX
    jc .sz
    call sizekb                 ; 360 / 720 / 1200 / 1440 -> bytes
    jmp short .capd
.sz:
    mov dx, s_usage
    jmp fatal
.d720:
    mov ax, 720
    xor dx, dx
    call sizekb
.capd:
    sub ax, CS_HDR
    sbb dx, 0
    mov [cap], ax
    mov [cap+2], dx
    cmp dx, 0                   ; room for one stored block at least
    ja .capok
    cmp ax, REC_HDR + BLOCK
    jae .capok
    mov dx, s_small
    jmp fatal
.capok:
    mov si, [args+4]            ; --- DEST ---------------------------
    mov di, dpath
    call dirof
    mov [dend], di
    cmp byte [fpause], 0        ; /P swaps DEST's disk, so FILE must not be
    je .np                      ; on it: its open handle would go on reading
    mov si, [args]              ; whatever disk went in next
    call drvof
    mov bl, al
    mov si, dpath
    call drvof
    cmp al, bl
    jne .np
    mov dx, s_samedrv
    jmp fatal
.np:
    mov si, dpath               ; the part names: DEST + BASE + .NNN
    mov di, ppath
    call strcpy
    dec di
    mov si, sname
.cb:
    lodsb
    cmp al, '.'
    je .cbd
    or al, al
    jz .cbd
    stosb
    jmp short .cb
.cbd:
    mov al, '.'
    stosb
    mov [pdig], di
    mov ax, '00'
    stosw
    stosw
    mov byte [di-1], 0
    mov ax, [total]             ; how many blocks
    mov dx, [total+2]
    add ax, BLOCK - 1
    adc dx, 0
    mov cx, 15
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx
    jnz .toobig
    cmp ax, MAXBLK
    jbe .nblk
.toobig:
    mov dx, s_toobig
    jmp fatal
.nblk:
    mov [nblk], ax
    push es                     ; --- the set ID: the tick count and size
    xor ax, ax
    mov es, ax
    mov ax, [es:0x46C]
    mov dx, [es:0x46E]
    pop es
    xor ax, [total]
    xor dx, [total+2]
    mov [setid], ax
    mov [setid+2], dx

    mov dx, s_pass1             ; --- pass 1: how big is each record? ---
    call puts
    xor ax, ax
    mov [bi], ax
.p1:
    mov ax, [bi]
    cmp ax, [nblk]
    jae .p1d
    call readblk                ; CX = its length, into A
    call encode                 ; AX = the record's length, header included
    mov bx, [bi]
    shl bx, 1
    mov [recsz+bx], ax
    inc word [bi]
    mov al, '.'
    call putc
    jmp short .p1
.p1d:
    mov dx, s_crlf
    call puts
    call plan                   ; [nparts], or it refuses
    mov bx, [hin]               ; --- pass 2: write them ---------------
    mov ax, 0x4200
    xor cx, cx
    xor dx, dx
    int 0x21
    xor ax, ax
    mov [bi], ax
    mov [done], ax
    mov [done+2], ax
    mov [k], ax
    mov word [hout], 0
.p2:
    mov ax, [bi]
    cmp ax, [nblk]
    jae .p2d
    call readblk
    call encode                 ; AX = the record, in B at REC's offset
    mov bx, [bi]
    shl bx, 1
    cmp ax, [recsz+bx]
    je .same
    mov dx, s_nondet            ; the two passes must agree
    jmp sfail
.same:
    mov [rlen], ax
    cmp word [hout], 0
    je .newp
    mov cx, [used]              ; does it fit what is left of this part?
    mov dx, [used+2]
    add cx, ax
    adc dx, 0
    cmp dx, [cap+2]
    jb .put
    ja .newp
    cmp cx, [cap]
    jbe .put
.newp:
    call newpart
.put:
    call putrec
    mov ax, [rlen]
    add [used], ax
    adc word [used+2], 0
    mov ax, [blen]
    add [done], ax
    adc word [done+2], 0
    inc word [bi]
    jmp short .p2
.p2d:
    call closep
    mov ax, [k]
    cmp ax, [nparts]
    je .fin
    mov dx, s_nondet
    jmp sfail
.fin:
    mov dx, sname
    call putz
    mov dx, s_split
    call puts
    mov ax, [nparts]
    xor dx, dx
    call putu32
    mov dx, s_parts
    call puts
    mov ax, 0x4C00
    int 0x21

; readblk - block [bi] of the input into A: CX = its length
readblk:
    push ds
    mov bx, [hin]
    mov cx, BLOCK
    mov ds, [bufa]
    xor dx, dx
    mov ah, 0x3F
    int 0x21
    pop ds
    jc .err
    or ax, ax
    jz .err
    mov cx, ax
    mov [blen], ax
    ret
.err:
    mov dx, s_rderr
    jmp sfail

; plan - the parts pass 2 will write, off recsz[]: the same packing, counted
plan:
    xor ax, ax
    mov [used], ax
    mov [used+2], ax
    mov word [nparts], 1
    xor si, si
.l:
    cmp si, [nblk]
    jae .d
    mov bx, si
    shl bx, 1
    mov ax, [recsz+bx]
    mov cx, [used]
    mov dx, [used+2]
    or cx, cx
    jnz .chk
    or dx, dx
    jz .add
.chk:
    add cx, ax
    adc dx, 0
    cmp dx, [cap+2]
    jb .add
    ja .brk
    cmp cx, [cap]
    jbe .add
.brk:
    inc word [nparts]
    mov word [used], 0
    mov word [used+2], 0
.add:
    add [used], ax
    adc word [used+2], 0
    inc si
    jmp short .l
.d:
    cmp word [nparts], MAXPARTS
    jbe .ok
    mov dx, s_toomany
    jmp fatal
.ok:
    ret

; newpart - close the part being written, open the next: its header written
newpart:
    call closep
    inc word [k]
    mov ax, [k]
    call setdig
    cmp byte [fpause], 0
    je .go
    mov dx, s_ready
    call puts
    mov dx, ppath
    call putz
    mov dx, s_presskey
    call puts
    mov ah, 0x08
    int 0x21
    push ax
    mov dx, s_crlf
    call puts
    pop ax
    cmp al, 27
    jne .go
    mov dx, s_stopped
    jmp sfail
.go:
    mov dx, ppath
    xor cx, cx
    mov ah, 0x3C
    int 0x21
    jnc .ok
    mov dx, s_wrerr
    jmp sfail
.ok:
    mov [hout], ax
    mov word [hdr], 'CS'        ; the header (SPEC.md 20.17.1)
    mov word [hdr+2], 1
    mov ax, [k]
    mov [hdr+4], ax
    mov ax, [nparts]
    mov [hdr+6], ax
    mov ax, [total]
    mov [hdr+8], ax
    mov ax, [total+2]
    mov [hdr+10], ax
    mov ax, [done]
    mov [hdr+12], ax
    mov ax, [done+2]
    mov [hdr+14], ax
    mov ax, [setid]
    mov [hdr+16], ax
    mov ax, [setid+2]
    mov [hdr+18], ax
    mov di, hdr+20
    mov si, sname
    mov cx, 12
.nm:
    lodsb
    stosb
    or al, al
    jz .pad
    loop .nm
    jmp short .hw
.pad:
    dec cx
    xor al, al
    rep stosb
.hw:
    mov dx, hdr
    mov cx, CS_HDR
    call wout
    mov word [used], 0
    mov word [used+2], 0
    mov dx, ppath
    call putz
    mov dx, s_crlf
    call puts
    ret

closep:
    mov bx, [hout]
    or bx, bx
    jz .n
    mov ah, 0x3E
    int 0x21
    mov word [hout], 0
.n:
    ret

; putrec - the record encode left: its header, then its payload
putrec:
    mov dx, rec
    mov cx, REC_HDR
    call wout
    push ds
    mov cx, [rec]
    mov ds, [paysg]
    xor dx, dx
    call wout
    pop ds
    ret

; wout - CX bytes at DS:DX to [cs:hout], all of them or stop
wout:
    mov bx, [cs:hout]
    mov ah, 0x40
    int 0x21
    jc .err
    cmp ax, cx
    jne .full
    ret
.err:
    push cs
    pop ds
    mov dx, s_wrerr
    jmp sfail
.full:
    push cs
    pop ds
    mov dx, s_full
    ; ...into sfail

; sfail - say DX, close what is open, stop. A part half-written is deleted
sfail:
    call puts
    mov bx, [hout]
    or bx, bx
    jz .n
    mov ah, 0x3E
    int 0x21
    mov dx, ppath
    mov ah, 0x41
    int 0x21
.n:
    mov ax, 0x4C01
    int 0x21

; -----------------------------------------------------------------------------
; encode - one block, CX bytes at A:0, into a record: [rec] its header, the
;          payload at [paysg]:0. out: AX = the record's length, header included
; -----------------------------------------------------------------------------
encode:
    mov [rec+2], cx             ; N
    push ds
    mov ds, [bufa]
    xor si, si
    call csum
    pop ds
    mov [rec+6], ax
    mov [rec+8], dx
    mov ax, [bufa]              ; stored, until LZ4 proves smaller
    mov [paysg], ax
    mov ax, [rec+2]
    mov [rec], ax
    mov word [rec+4], 0
    cmp byte [fstore], 0
    jne .out
    call lz4enc                 ; AX = the stream's length in B, or CF
    jc .out
    cmp ax, [rec+2]
    jae .out                    ; not smaller: stored
    mov [rec], ax
    mov word [rec+4], 1         ; M = LZ4
    mov ax, [bufb]
    mov [paysg], ax
.out:
    mov ax, [rec]
    add ax, REC_HDR
    ret

; -----------------------------------------------------------------------------
; lz4enc - A:0, [rec+2] bytes, into a SPEC.md 20.13.7 stream at B:0
; out: AX = the stream's length. clobbers: everything but DS
;
; Greedy, one candidate a position out of a 4,096-slot table of four-byte
; prefixes (B:9000h). The boundaries it tracks are the ends of MATCH-bearing
; sequences and the start, exactly os88lz._cut's: the lead (produced -
; consumed, consumed counting the T word) is compared SIGNED against a maximum
; that starts at 0, and only strictly greater moves the cut.
; -----------------------------------------------------------------------------
lz4enc:
    push es
    push ds
    mov es, [bufb]
    mov di, HT                  ; the table empty
    mov cx, 4096
    xor ax, ax
    rep stosw
    mov ax, [rec+2]
    mov [e_n], ax
    sub ax, MINLEN
    mov [e_lim], ax             ; (under 0 for a block this short: never taken)
    add ax, MINLEN - LASTLITS
    mov [e_mend], ax
    xor ax, ax
    mov [e_anc], ax
    mov [e_mx], ax
    mov [e_kp], ax
    mov word [e_kc], 2
    mov ds, [bufa]
    xor si, si
    mov di, 2                   ; the T word, filled at the end
    cmp word [cs:e_n], MINLEN
    ja .loop
    jmp .final
.loop:
    cmp si, [cs:e_lim]
    jb .look
    jmp .final
.look:
    mov ax, [si]
    mov dx, [si+2]
    mov bx, dx
    mov cl, 5
    rol bx, cl
    xor bx, ax
    mov cl, bh
    shr cl, 1
    xor bl, cl
    and bx, 0x0FFF
    shl bx, 1
    mov bp, [es:HT+bx]
    lea cx, [si+1]
    mov [es:HT+bx], cx
    or bp, bp
    jz .miss
    dec bp                      ; the candidate
    cmp ax, [ds:bp]
    jne .miss
    cmp dx, [ds:bp+2]
    jne .miss
    mov [cs:e_out], di          ; --- four bytes match: how many more ---
    mov bx, si
    push es
    push ds
    pop es
    lea di, [bp+4]
    add si, 4
    mov cx, [cs:e_mend]
    sub cx, si
    jbe .len0
    repe cmpsb
    je .len
    dec si                      ; one past the byte that differed
    jmp short .len
.len0:
    mov si, [cs:e_mend]         ; (cannot happen below e_lim; kept exact)
.len:
    pop es
    mov ax, si
    sub ax, bx                  ; the match's length, at least 4
    mov si, bx
    mov di, [cs:e_out]
    mov dx, bx
    sub dx, bp                  ; ...and its offset
    call .emit
    add si, ax                  ; past the match
    mov [cs:e_anc], si
    mov ax, si                  ; the boundary: lead = produced - consumed
    sub ax, di
    cmp ax, [cs:e_mx]
    jle .loop
    mov [cs:e_mx], ax
    mov [cs:e_kp], si
    mov [cs:e_kc], di
    jmp .loop
.miss:
    inc si
    jmp .loop

.final:                         ; the last literals: a sequence with no match
    mov si, [cs:e_n]
    xor ax, ax
    call .emit
    ; the cut: T = n - kp, the symbols to kc, then src[kp..n] raw
    mov di, [cs:e_kc]
    mov si, [cs:e_kp]
    mov cx, [cs:e_n]
    sub cx, si
    mov [es:0], cx              ; T
    rep movsb                   ; the raw tail
    mov ax, di                  ; the stream's length
    pop ds
    pop es
    clc
    ret

; .emit - a sequence: literals [e_anc..SI), then a match of AX bytes at
;         offset DX (AX = 0: none). ES:DI the output, advanced. AX, SI kept
.emit:
    push ax
    push si
    mov cx, si
    sub cx, [cs:e_anc]          ; L
    mov bl, 15                  ; the token
    cmp cx, 15
    jae .lt
    mov bl, cl
.lt:
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    or ax, ax
    jz .tok
    push ax
    sub ax, 4
    cmp ax, 15
    jb .mt
    mov al, 15
.mt:
    or bl, al
    pop ax
.tok:
    mov [es:di], bl
    inc di
    push cx
    cmp cx, 15                  ; the literal length's extension
    jb .nlx
    sub cx, 15
.lx:
    cmp cx, 255
    jb .lxl
    mov byte [es:di], 255
    inc di
    sub cx, 255
    jmp short .lx
.lxl:
    mov [es:di], cl
    inc di
.nlx:
    pop cx
    mov si, [cs:e_anc]          ; the literals
    rep movsb
    pop si
    pop ax
    or ax, ax
    jz .done
    mov [es:di], dx             ; the offset
    inc di
    inc di
    mov cx, ax
    sub cx, 4
    cmp cx, 15                  ; the match length's extension
    jb .done
    sub cx, 15
.mx:
    cmp cx, 255
    jb .mxl
    mov byte [es:di], 255
    inc di
    sub cx, 255
    jmp short .mx
.mxl:
    mov [es:di], cl
    inc di
.done:
    ret

; -----------------------------------------------------------------------------
; csum - SPEC.md 20.17.2's check over DS:SI, CX bytes: AX = A, DX = B
; -----------------------------------------------------------------------------
csum:
    xor bx, bx
    xor dx, dx
    shr cx, 1
    pushf
    jcxz .odd
.w:
    lodsw
    add bx, ax
    add dx, bx
    loop .w
.odd:
    popf
    jnc .done
    lodsb
    xor ah, ah
    add bx, ax
    add dx, bx
.done:
    mov ax, bx
    ret

; -----------------------------------------------------------------------------
; the 'CZ' file (SPEC.md 20.14), whole
; -----------------------------------------------------------------------------
do_unpack:
    mov si, [args]
    or si, si
    jz .use
    cmp word [args+2], 0
    jnz .have
.use:
    mov dx, s_usage
    jmp fatal
.have:
    mov dx, si
    mov ax, 0x3D00
    int 0x21
    jnc .op
    mov dx, s_noopen
    jmp fatal
.op:
    mov [hin], ax
    mov bx, ax
    mov ax, 0x4202
    xor cx, cx
    xor dx, dx
    int 0x21
    sub ax, 8                   ; P, the stream's length
    sbb dx, 0
    jb .notcz
    mov [p32], ax
    mov [p32+2], dx
    mov ax, 0x4200
    xor cx, cx
    xor dx, dx
    int 0x21
    mov cx, 8
    mov dx, hdr
    mov ah, 0x3F
    int 0x21
    jc .rd
    cmp ax, 8
    jne .notcz
    cmp word [hdr], 'CZ'
    jne .notcz
    cmp byte [hdr+2], 1
    ja .notcz
    cmp byte [hdr+3], 0
    jne .notcz
    cmp word [hdr+6], 0x100
    jae .notcz                  ; 16MB, the hint's 24 bits
    mov ax, [p32]               ; memory: P then U, paragraph-rounded
    mov dx, [p32+2]
    call paras
    jc .mem
    add ax, [bufa]
    jc .mem
    mov [dseg], ax              ; the output's segment
    mov bx, ax
    mov ax, [hdr+4]
    mov dx, [hdr+6]
    call paras
    jc .mem
    add ax, bx
    jc .mem
    cmp ax, [PSP_TOP]
    jbe .fits
.mem:
    mov dx, s_nomem
    jmp fatal
.fits:
    mov ax, [bufa]              ; the stream, 32KB at a time
    mov [paysg], ax
.rl:
    push ds
    mov bx, [hin]
    mov cx, BLOCK
    mov ds, [paysg]
    xor dx, dx
    mov ah, 0x3F
    int 0x21
    pop ds
    jc .rd
    add word [paysg], BLOCK / 16
    cmp ax, BLOCK
    je .rl
    mov bx, [hin]
    mov ah, 0x3E
    int 0x21
    push ds                     ; expand: lz_decomp_big takes AH:CX
    mov cx, [p32]
    mov ah, [p32+2]
    mov al, [hdr+2]
    mov bx, [hdr+6]
    mov dx, [hdr+4]
    mov es, [dseg]
    mov ds, [bufa]
    xor si, si
    xor di, di
    call lz_decomp_big
    pop ds
    push cs
    pop es
    jnc .ok
    mov dx, s_bad
    jmp fatal
.ok:
    mov dx, [args+2]            ; the output must not be there already
    mov ax, 0x3D00
    int 0x21
    jc .new
    mov dx, s_exists
    jmp fatal
.new:
    mov dx, [args+2]
    xor cx, cx
    mov ah, 0x3C
    int 0x21
    jnc .cr
    mov dx, s_wrerr
    jmp fatal
.cr:
    mov [hout], ax
    mov ax, [hdr+4]
    mov [total], ax
    mov ax, [hdr+6]
    mov [total+2], ax
.wl:
    mov cx, BLOCK
    cmp word [total+2], 0
    jne .wn
    cmp [total], cx
    jae .wn
    mov cx, [total]
.wn:
    jcxz .wd
    push ds
    mov bx, [hout]
    mov ds, [dseg]
    xor dx, dx
    mov ah, 0x40
    int 0x21
    pop ds
    jc .wr
    cmp ax, cx
    jne .wr
    sub [total], ax
    sbb word [total+2], 0
    add word [dseg], BLOCK / 16
    jmp short .wl
.wd:
    mov bx, [hout]
    mov ah, 0x3E
    int 0x21
    mov dx, [args+2]
    call putz
    mov dx, s_joined
    call puts
    mov ax, [hdr+4]
    mov dx, [hdr+6]
    call putu32
    mov dx, s_bytes
    call puts
    mov ax, 0x4C00
    int 0x21
.notcz:
    mov dx, s_notcz
    jmp fatal
.rd:
    mov dx, s_rderr
    jmp fatal
.wr:
    mov dx, s_full
    jmp fatal

; -----------------------------------------------------------------------------
; small things
; -----------------------------------------------------------------------------
; paras - DX:AX bytes -> AX paragraphs, rounded up, plus one
;         CF=1 = more than a 16-bit count: no 8086 holds it, so refuse
paras:
    add ax, 15
    adc dx, 0
    jc .big
    mov cx, 4
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    or dx, dx                   ; 1MB and more: the high word is NOT zero,
    jnz .big                    ; and dropping it wrapped the memory check
    inc ax
    jz .big
    clc
    ret
.big:
    stc
    ret

; fatal - say DX and stop with errorlevel 1
fatal:
    call puts
    mov ax, 0x4C01
    int 0x21

; puts - a NUL string at DS:DX to standard output; putz is the same thing
puts:
putz:
    push ax
    push bx
    push cx
    push si
    mov si, dx
    xor cx, cx
.l:
    cmp byte [si], 0
    je .w
    inc si
    inc cx
    jmp short .l
.w:
    mov bx, 1
    mov ah, 0x40
    int 0x21
    pop si
    pop cx
    pop bx
    pop ax
    ret

putc:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

; putu32 - DX:AX in decimal
putu32:
    mov di, numbuf + 11
    mov byte [di], 0
    mov cx, 10
.l:
    mov bx, ax                  ; DX:AX / 10, two steps
    mov ax, dx
    xor dx, dx
    div cx
    xchg ax, bx
    div cx                      ; AX = the low quotient, DX = the digit
    add dl, '0'
    dec di
    mov [di], dl
    mov dx, bx
    or bx, ax
    jnz .l
    mov dx, di
    jmp puts

; atou32 - the decimal at DS:SI into DX:AX; CF = not a number
atou32:
    xor ax, ax
    xor dx, dx
    cmp byte [si], 0
    je .bad
.l:
    mov bl, [si]
    or bl, bl
    jz .ok
    sub bl, '0'
    cmp bl, 9
    ja .bad
    push bx                     ; DX:AX = DX:AX * 10 + BL
    mov bx, dx
    mov cx, 10
    mul cx
    push ax
    push dx
    mov ax, bx
    mul cx
    pop bx
    add bx, ax
    pop ax
    mov dx, bx
    pop bx
    xor bh, bh
    add ax, bx
    adc dx, 0
    inc si
    jmp short .l
.ok:
    clc
    ret
.bad:
    stc
    ret

; sizekb - 360/720/1200/1440 as a fresh disk's data area; anything else is
;          already bytes. DX:AX in and out
sizekb:
    or dx, dx
    jnz .b
    cmp ax, 360
    jne .a
    mov ax, 354 * 1024 & 0xFFFF
    mov dx, (354 * 1024) >> 16
    ret
.a:
    cmp ax, 720
    jne .c
    mov ax, (713 * 1024) & 0xFFFF
    mov dx, (713 * 1024) >> 16
    ret
.c:
    cmp ax, 1200
    jne .d
    mov ax, (2371 * 512) & 0xFFFF
    mov dx, (2371 * 512) >> 16
    ret
.d:
    cmp ax, 1440
    jne .b
    mov ax, (2847 * 512) & 0xFFFF
    mov dx, (2847 * 512) >> 16
.b:
    ret

; strcpy - DS:SI to ES:DI with its NUL; DI ends one past the NUL
strcpy:
    lodsb
    stosb
    or al, al
    jnz strcpy
    ret

; basename - SI -> the character after the last '\' or ':'
basename:
    mov bx, si
.l:
    lodsb
    or al, al
    jz .d
    cmp al, '\'
    je .m
    cmp al, ':'
    jne .l
.m:
    mov bx, si
    jmp short .l
.d:
    mov si, bx
    ret

; digits - SI = a path: SI -> its extension's three digits, or CF
digits:
    call basename
.dot:
    lodsb
    or al, al
    jz .no
    cmp al, '.'
    jne .dot
    mov bx, si
    mov cx, 3
.d:
    lodsb
    sub al, '0'
    cmp al, 9
    ja .no
    loop .d
    cmp byte [si], 0
    jne .no
    mov si, bx
    clc
    ret
.no:
    stc
    ret

; setdig - [pdig]'s three digits := AX, 1..999
setdig:
    mov di, [pdig]
    mov bl, 10
    div bl
    add ah, '0'
    mov [di+2], ah
    xor ah, ah
    div bl
    add ax, '00'
    mov [di], ax
    ret

; dirof - DS:SI (or nothing, SI = 0) as a directory prefix at ES:DI: a
;         separator added unless it ends in one. DI -> its NUL
dirof:
    or si, si
    jz .none
    cmp byte [si], 0
    je .none
    call strcpy
    dec di
    mov al, [di-1]
    cmp al, '\'
    je .none
    cmp al, ':'
    je .none
    mov al, '\'
    stosb
.none:
    mov byte [di], 0
    ret

; drvof - the drive DS:SI names: its "X:", or the current one. AL = 0 for A:
;         clobbers: AH
drvof:
    cmp byte [si], 0
    je .cur
    cmp byte [si+1], ':'
    jne .cur
    mov al, [si]                ; parse upper-cased it
    sub al, 'A'
    ret
.cur:
    mov ah, 0x19
    int 0x21
    ret

; check83 - DS:SI an 8.3 name that is not itself a part's: CF = it is not
check83:
    xor cx, cx                  ; CL = the base's length, CH = the ext's
    mov bl, 0                   ; 0 = in the base, 1 = in the extension
.l:
    lodsb
    or al, al
    jz .end
    cmp al, '.'
    jne .ch
    or bl, bl
    jnz .no
    inc bl
    jmp short .l
.ch:
    cmp al, ' '
    jbe .no
    cmp al, '*'
    je .no
    cmp al, '?'
    je .no
    cmp al, '\'                 ; ...and no path: a join reads this name
    je .no                      ; off the part's header, and a separator in
    cmp al, '/'                 ; it is a file outside DEST
    je .no
    cmp al, ':'
    je .no
    or bl, bl
    jnz .ext
    inc cl
    cmp cl, 8
    ja .no
    jmp short .l
.ext:
    inc ch
    cmp ch, 3
    ja .no
    jmp short .l
.end:
    or cl, cl
    jz .no
    push si
    mov si, sname
    call digits                 ; ...and NAME.123 would be its own part
    pop si
    jnc .no
    clc
    ret
.no:
    stc
    ret

; --- the kernel's decoder, as it is -------------------------------------------
%include "lz.inc"

section .text
s_usage:    db 'OS88CZ - split sets and CZ files for os8088 (SPEC.md 20.17)', 13, 10
            db '  OS88CZ J PART [DEST]                  join a set from any part', 13, 10
            db '  OS88CZ S FILE [SIZE] [DEST] [/S] [/P] split: SIZE 360 720 1200 1440 or bytes', 13, 10
            db '                                        /S store only, /P pause for each disk', 13, 10
            db '  OS88CZ U FILE.CZ OUT                  expand a CZ file', 13, 10, 0
s_nomem:    db 'Not enough memory', 13, 10, 0
s_notpart:  db 'Not a part of a split set', 13, 10, 0
s_notcz:    db 'Not a CZ file', 13, 10, 0
s_wrong:    db 'Wrong part: ', 0
s_bad:      db 13, 10, 'Cannot expand this - a damaged or short set', 13, 10, 0
s_rderr:    db 13, 10, 'Read error', 13, 10, 0
s_wrerr:    db 13, 10, 'Cannot write', 13, 10, 0
s_full:     db 13, 10, 'Disk full', 13, 10, 0
s_exists:   db ' already exists', 13, 10, 0
s_insert:   db 'Insert the disk with ', 0
s_missing:  db 13, 10, 'Missing ', 0
s_onout:    db ' - the result is on that drive, so its disk cannot be swapped', 13, 10, 0
s_samedrv:  db '/P swaps the disk in DEST, so FILE must be on another drive', 13, 10, 0
s_ready:    db 'Ready for ', 0
s_presskey: db ' and press a key (Esc stops) ', 0
s_stopped:  db 'Stopped', 13, 10, 0
s_crlf:     db 13, 10, 0
s_joined:   db ': ', 0
s_bytes:    db ' bytes', 13, 10, 0
s_noopen:   db 'Cannot open it', 13, 10, 0
s_empty:    db 'An empty file has nothing to carry', 13, 10, 0
s_name83:   db 'Not an 8.3 name, or named like a part', 13, 10, 0
s_small:    db 'A part that small cannot hold a block', 13, 10, 0
s_toobig:   db 'Over 128MB', 13, 10, 0
s_toomany:  db 'Over 999 parts - use a bigger size', 13, 10, 0
s_nondet:   db 13, 10, 'Internal: the two passes disagree', 13, 10, 0
s_pass1:    db 'Measuring', 0
s_split:    db ': ', 0
s_parts:    db ' part(s)', 13, 10, 0
s_tmp:      db 'OS88CZ.$$$', 0

section .bss
bss_start:
bufa:       resw 1              ; buffer A's segment
bufb:       resw 1              ; buffer B's
cmd:        resb 1
fstore:     resb 1
fpause:     resb 1
anyrec:     resb 1
args:       resw 3
argbuf:     resb 130
ppath:      resb 80             ; a part's path, digits rewritten per part
pdig:       resw 1              ; ...where its digits are
dpath:      resb 80             ; DEST, with its separator
dend:       resw 1
fpath:      resb 96             ; the result's path
tpath:      resb 96             ; OS88CZ.$$$'s
sname:      resb 14             ; the 8.3 name a split rejoins as
hdr:        resb CS_HDR
rec:        resb REC_HDR
hin:        resw 1
hout:       resw 1
k:          resw 1              ; the part being read or written
nparts:     resw 1
total:      resd 1              ; the original's size
done:       resd 1              ; ...and how much of it is through
setid:      resd 1
cap:        resd 1              ; a part's room for records
used:       resd 1              ; ...and how much of it is spent
nblk:       resw 1
bi:         resw 1              ; the block being split
blen:       resw 1              ; ...its length
rlen:       resw 1              ; ...and its record's
paysg:      resw 1              ; where the payload is
dseg:       resw 1              ; where a block's N bytes are
p32:        resd 1
numbuf:     resb 12
e_n:        resw 1              ; the encoder's block length...
e_lim:      resw 1              ; ...the last position a match may start...
e_mend:     resw 1              ; ...the last it may reach...
e_anc:      resw 1              ; ...the first literal not yet emitted...
e_out:      resw 1
e_mx:       resw 1              ; ...and the cut: the best lead so far,
e_kp:       resw 1              ; where in the source,
e_kc:       resw 1              ; and where in the stream
recsz:      resw MAXBLK         ; pass 1's record lengths
bss_end:
