; =============================================================================
; os8088 - tests/megaxm/megaxm.asm
;
; MEGAXM, a TEST instrument (nothing under tests/ ships): the XMEM cells end
; to end, from a package - CAPS, a 64KB ALLOC, sixteen 4KB patterns COPYed
; out and read back in and compared, and the FREE. Every result on port E9,
; one line each, so tools/os88bochs.py can read the verdict:
;
;   MEGAXM: caps NNNN          the KB OSAPI_XMEM_CAPS answers
;   MEGAXM: alloc DDDDAAAA     the block's linear base
;   MEGAXM: sel SSSS code CCCC   OSAPI_XMEM_SEL's selector and
;                              OSAPI_MEM_CODESEL's answer for our own DS
;                              (KERN-MEGA-PLAN 12.2) - the block read IN
;                              PLACE through the selector, the selector
;                              re-aimed, a window outside the block
;                              refused; on a kernel without CPU_F_PM both
;                              must answer their real-mode way instead
;   MEGAXM: file SSSS          (protected mode) the FILE LAYER against a
;                              buffer above 1MB (KERN-MEGA-PLAN 13.3 step 4):
;                              MZ_FLEN bytes written from the selector,
;                              read back with FILE_READ at an odd offset and
;                              with READ_AT into a second block, compared,
;                              and this package's own .o88 read into the
;                              window (a compressed one exercises the
;                              expansion); SSSS is that file's size. A
;                              write from a window EXACTLY the file's size
;                              is in it too (step 2D-2F): the hint stamp
;                              must read the file's byte 0 and not one past
;                              the window (KERN-MEGA-PLAN 14.27)
;   MEGAXM: i13 ok             (protected mode) int 13h issued by the PACKAGE
;                              into a buffer above 1MB (KERN-MEGA-PLAN 14.1
;                              answer 11, mega_hibnc): drive A's track 0,
;                              18 sectors - two runs of the landing's nine -
;                              read into the block at an odd offset and
;                              compared with the same sectors read low, then
;                              written back from the block and read again
;   MEGAXM: ok | bad NN | no NN    the verdict, or the step that refused
;
; It was written for kern_mega's own pool (docs/plans/KERN-MEGA-PLAN.md 12.2,
; mega.inc's mx_*), and runs on any kernel with a store: tier 0 answers caps 0
; and stops there.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGAXM', mz_entry

MZ_CHUNK    equ 4096
MZ_N        equ 16

mz_entry:
    push si
    push di
    mov si, mz_s_caps
    call mz_say
    call OSAPI_XMEM_CAPS
    call mz_hex4
    call mz_nl
    or ax, ax
    jz .done
    xor ax, ax                  ; DX:AX = 64KB
    mov dx, 1
    call OSAPI_XMEM_ALLOC
    mov cl, 1
    jc .no
    mov [mz_base], ax
    mov [mz_base+2], dx
    mov si, mz_s_alloc
    call mz_say
    xchg ax, dx
    call mz_hex4
    xchg ax, dx
    call mz_hex4
    call mz_nl
    ; --- out: chunk k holds bytes (i * 7 + k) for i in 0..4095 -------------
    xor bx, bx
.out:
    call mz_fill                ; mz_buf = pattern BL
    mov di, 0                   ; conventional -> extended
    call mz_copy
    mov cl, 2
    jc .no
    inc bx
    cmp bx, MZ_N
    jb .out
    ; --- in, and compare -------------------------------------------------
    xor bx, bx
.in:
    push bx
    mov di, mz_buf              ; clear the buffer first, so a copy that
    mov cx, MZ_CHUNK / 2        ; moved nothing cannot pass
    xor ax, ax
    push ds
    pop es
    cld
    rep stosw
    pop bx
    mov di, 1                   ; extended -> conventional
    call mz_copy
    mov cl, 3
    jc .no
    call mz_cmp                 ; CF = 1 a byte differs
    mov cl, bl                  ; ...and the verdict names the chunk
    jc .bad
    inc bx
    cmp bx, MZ_N
    jb .in
    call mz_sel                 ; CL = the step, CF = 1 refused, ZF = 0 bad
    jc .no
    jnz .bad
    call mz_file                ; ...and the file layer, the same way
    jc .no
    jnz .bad
    call mz_i13                 ; ...and a program's OWN int 13h
    jc .no
    jnz .bad
    mov ax, [mz_base]
    mov dx, [mz_base+2]
    call OSAPI_XMEM_FREE
    mov cl, 5
    jc .no
    mov si, mz_s_ok
    call mz_say
    call mz_nl
    jmp short .done
.no:                            ; CL = the step that refused
    mov si, mz_s_no
    jmp short .code
.bad:
    mov si, mz_s_bad
.code:
    call mz_say
    mov al, cl
    call mz_hex2
    call mz_nl
.done:
    pop di
    pop si
    stc                         ; no window: the instance ends here
    ret

; mz_sel - the two cells of KERN-MEGA-PLAN 12.2 against the block the copy
; round-trip just verified. out: CF = 0, ZF = 1 every check passed; CF = 1 a
; cell refused where it must not (CL = the step); ZF = 0 an answer was wrong
; (CL = the step). Chunk k's byte i is (k + 7i) & 0xFF (mz_fill)
mz_sel:
    push ax
    push bx
    push dx
    push es
    call OSAPI_CPU_INFO         ; AH = the CPU_F_* bits
    test ah, CPU_F_PM
    jnz .pm
    mov ax, [mz_base]           ; --- no protected mode: both answer the
    mov dx, [mz_base+2]         ; real-mode way
    xor cx, cx
    call OSAPI_XMEM_SEL
    mov cl, 0x10
    jnc .wrong                  ; it must refuse...
    cmp ax, 1                   ; ...with 1
    jne .wrong
    mov dx, ds
    call OSAPI_MEM_CODESEL
    mov cl, 0x11
    jc .refused
    mov dx, ds
    cmp ax, dx                  ; the segment itself
    jne .wrong
    jmp .say
.pm:
    mov ax, [mz_base]           ; --- the whole block, 64KB, in place
    mov dx, [mz_base+2]
    xor cx, cx
    call OSAPI_XMEM_SEL
    mov cl, 6
    jc .refused
    mov [mz_xsel], ax
    mov es, ax
    mov cl, 7
    cmp byte [es:5*MZ_CHUNK + 100], (5 + 7*100) & 0xFF
    jne .wrong
    mov byte [es:9*MZ_CHUNK], 0xA5  ; a WRITE through it, read back below
    mov ax, [mz_base]           ; --- re-aimed: 16 bytes at chunk 3 + 10
    add ax, 3*MZ_CHUNK + 10
    mov dx, [mz_base+2]
    adc dx, 0
    mov cx, 16
    call OSAPI_XMEM_SEL
    mov cl, 8
    jc .refused
    mov cl, 9
    cmp ax, [mz_xsel]           ; ONE selector a block
    jne .wrong
    mov es, ax                  ; reload: the cached window is the old one
    mov cl, 0x0A
    cmp byte [es:0], (3 + 7*10) & 0xFF
    jne .wrong
    mov ax, [mz_base]           ; --- the write landed: back over the whole
    mov dx, [mz_base+2]         ; block and read it
    xor cx, cx
    call OSAPI_XMEM_SEL
    mov es, ax
    mov cl, 0x0B
    cmp byte [es:9*MZ_CHUNK], 0xA5
    jne .wrong
    mov ax, [mz_base]           ; --- a window past the block: refused, 2
    mov dx, [mz_base+2]
    inc dx                      ; + 64KB, the block's end
    mov cx, 16
    call OSAPI_XMEM_SEL
    mov cl, 0x0C
    jnc .wrong
    cmp ax, 2
    jne .wrong
    mov dx, ds                  ; --- CODESEL: our region's segment is its
    call OSAPI_MEM_CODESEL      ; LDT code alias...
    mov cl, 0x0D
    jc .refused
    mov dx, ds
    shr dx, 1
    shr dx, 1
    or dl, 4
    cmp ax, dx
    jne .wrong
    mov dx, [mz_xsel]           ; ...and a data-only selector has none
    call OSAPI_MEM_CODESEL
    mov cl, 0x0E
    jnc .wrong
.say:
    mov si, mz_s_sel
    call mz_say
    mov ax, [mz_xsel]
    call mz_hex4
    mov si, mz_s_code
    call mz_say
    mov dx, ds
    call OSAPI_MEM_CODESEL
    call mz_hex4
    call mz_nl
    xor ax, ax                  ; ZF = 1, CF = 0
    jmp short .out
.refused:
    stc
    jmp short .out
.wrong:
    or cl, cl                   ; ZF = 0 (every step is non-zero), CF = 0
.out:
    pop es
    pop dx
    pop bx
    pop ax
    ret

; mz_file - the file layer with ES = a selector ABOVE 1MB. Protected mode
; only (a real-mode kernel has no such selector to hand it). out as mz_sel's
MZ_FLEN     equ 40000           ; past 32KB, so a read crosses lz's slide, and
MZ_FOFF     equ 0x1234
MZ_XLEN     equ 32768           ; the exact-window write: WHOLE sectors, so the
                                ; run moves [dskw_src] to the window's end (a
                                ; partial tail goes through the stage and
                                ; leaves it inside, which is why 40,000 did
                                ; not fault on the kernel with the defect)
MZ_BIGLEN   equ 50000           ; BIG.TXT unpacked: packed small, so R is large
                                ; and the decoder's source starts past 32KB          ; not a sector multiple; the read lands at an
                                ; offset that is not one either
mz_file:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    call OSAPI_CPU_INFO
    test ah, CPU_F_PM
    jnz .pm
    xor ax, ax                  ; nothing to do: ZF = 1, CF = 0
    jmp .out
.pm:
    mov ax, [mz_base]           ; the first block, whole, as the source
    mov dx, [mz_base+2]
    xor cx, cx
    call OSAPI_XMEM_SEL
    mov cl, 0x20
    jc .refused
    mov [mz_xsel], ax
    xor ax, ax                  ; a second 64KB block for the reads
    mov dx, 1
    call OSAPI_XMEM_ALLOC
    mov cl, 0x21
    jc .refused
    mov [mz_base2], ax
    mov [mz_base2+2], dx
    xor cx, cx
    call OSAPI_XMEM_SEL
    mov cl, 0x22
    jc .refused
    mov [mz_xsel2], ax
    mov es, [mz_xsel]           ; --- WRITE straight from above 1MB --------
    xor bx, bx
    xor dx, dx
    mov cx, MZ_FLEN
    mov si, mz_s_tmp
    call OSAPI_FILE_WRITE
    mov cl, 0x23
    jc .refused
    call mz_clr2                ; --- FILE_READ, at an odd offset ----------
    mov es, [mz_xsel2]
    mov bx, MZ_FOFF
    xor dx, dx
    mov cx, 0xFFFF - MZ_FOFF
    mov si, mz_s_tmp
    call OSAPI_FILE_READ
    mov cl, 0x24
    jc .refused
    mov cl, 0x25
    cmp ax, MZ_FLEN
    jne .wrong
    mov di, MZ_FOFF
    mov cx, MZ_FLEN
    call mz_cmp2
    mov cl, 0x26
    jnz .wrong
    call mz_clr2                ; --- READ_AT, 16KB from offset 0 ----------
    mov es, [mz_xsel2]
    xor bx, bx
    mov cx, 16384
    xor ax, ax
    xor dx, dx
    mov si, mz_s_tmp
    call OSAPI_FILE_READ_AT
    mov cl, 0x27
    jc .refused
    mov cl, 0x28
    cmp ax, 16384
    jne .wrong
    xor di, di
    mov cx, 16384
    call mz_cmp2
    mov cl, 0x29
    jnz .wrong
    mov si, mz_s_tmp
    call OSAPI_FILE_DELETE
    mov ax, [mz_base]           ; --- a write from a window EXACTLY the
    mov dx, [mz_base+2]         ; file's size: the hint stamp reads the
    mov cx, MZ_XLEN             ; file's byte 0 after the data is down, and
    call OSAPI_XMEM_SEL         ; read it at the END of the bytes above 1MB
    mov cl, 0x2D                ; - one past this window's limit, the #GP
    jc .refused                 ; FTPD's STOR took (KERN-MEGA-PLAN 14.27)
    mov es, ax
    xor bx, bx
    xor dx, dx
    mov cx, MZ_XLEN
    mov si, mz_s_tmp
    call OSAPI_FILE_WRITE
    mov cl, 0x2E
    jc .refused
    mov si, mz_s_tmp
    call OSAPI_FILE_DELETE
    mov ax, [mz_base]           ; ...and the whole block again, for the
    mov dx, [mz_base+2]         ; legs below
    xor cx, cx
    call OSAPI_XMEM_SEL
    mov cl, 0x2F
    jc .refused
    call mz_clr2                ; --- a PACKED file, expanded in place ----
    mov es, [mz_xsel2]
    xor bx, bx
    xor dx, dx
    mov cx, 0xFFFF
    mov si, mz_s_big
    call OSAPI_FILE_READ
    mov cl, 0x2A
    jc .refused
    push ax
    mov si, mz_s_file
    call mz_say
    pop ax
    call mz_hex4
    call mz_nl
    mov cl, 0x2B
    cmp ax, MZ_BIGLEN
    jne .wrong
    xor di, di                  ; byte i is 'A' + i mod 26
    mov al, 'A'
    mov cx, MZ_BIGLEN
.big:
    cmp [es:di], al
    jne .bigbad
    inc di
    inc al
    cmp al, 'Z' + 1
    jb .bign
    mov al, 'A'
.bign:
    loop .big
    jmp short .bigok
.bigbad:
    mov [mz_diff], di
    mov cl, 0x2C
    jmp .wrong
.bigok:
    push ds                     ; ES off the block before its selector goes
    pop es                      ; (the cell's exit reloads the caller's ES)
    mov ax, [mz_base2]
    mov dx, [mz_base2+2]
    call OSAPI_XMEM_FREE
    xor ax, ax                  ; ZF = 1, CF = 0
    jmp short .out
.refused:
    stc
    jmp short .out
.wrong:
    push si
    mov si, mz_s_at
    call mz_say
    mov ax, [mz_diff]
    call mz_hex4
    call mz_nl
    pop si
    or cl, cl
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; mz_i13 - int 13h AH=02h/03h with ES = a selector above 1MB, issued by this
; package and not by the file layer. Protected mode only; out as mz_sel's
MZ_I13OFF   equ 0x0123              ; odd, so nothing about it is a sector
mz_i13:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    call OSAPI_CPU_INFO
    test ah, CPU_F_PM
    jnz .pm
    xor ax, ax
    jmp .out
.pm:
    mov es, [mz_xsel]           ; --- 18 sectors into the block ----------
    mov bx, MZ_I13OFF
    mov ax, 0x0212
    mov cx, 0x0001              ; cylinder 0, sector 1
    xor dx, dx                  ; head 0, drive A
    int 0x13
    mov cl, 0x30
    jc .refused
    mov cl, 0x31
    cmp al, 18
    jne .wrong
    mov cl, 0x32                ; --- sectors 1..8, low, against it -------
    mov di, MZ_I13OFF
    mov al, 1
    call .cmp8
    jnz .wrong
    mov cl, 0x33                ; --- 9..16: across the landing's run ------
    mov di, MZ_I13OFF + 4096
    mov al, 9
    call .cmp8
    jnz .wrong
    mov es, [mz_xsel]           ; --- written back FROM the block ----------
    mov bx, MZ_I13OFF
    mov ax, 0x0312
    mov cx, 0x0001
    xor dx, dx
    int 0x13
    mov cl, 0x34
    jc .refused
    mov cl, 0x35                ; --- ...and the disk still says the same --
    mov di, MZ_I13OFF + 4096
    mov al, 9
    call .cmp8
    jnz .wrong
    mov si, mz_s_i13
    call mz_say
    call mz_nl
    xor ax, ax                  ; ZF = 1, CF = 0
    jmp short .out
.refused:
    stc
    jmp short .out
.wrong:
    or cl, cl
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret
; AL = the first sector: 8 of them read into mz_buf and compared with the
; block from DI. ZF = 1 equal. Keeps CL
.cmp8:
    push cx
    push ds
    pop es
    mov bx, mz_buf
    mov cl, al
    xor ch, ch
    mov ax, 0x0208
    xor dx, dx
    int 0x13
    jc .cbad
    mov si, mz_buf
    mov es, [mz_xsel]
    mov cx, 4096
    cld
    repe cmpsb
    pop cx
    ret
.cbad:
    or sp, sp                   ; ZF = 0
    pop cx
    ret

; the second block zeroed, through its selector
mz_clr2:
    push ax
    push cx
    push di
    push es
    mov es, [mz_xsel2]
    xor di, di
    mov cx, 0x8000
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; CX bytes of block 1 from 0 against block 2 from DI -> ZF = 1 equal
mz_cmp2:
    push si
    push ds
    push es
    mov es, [mz_xsel2]
    mov ds, [mz_xsel]
    xor si, si
    cld
    repe cmpsb
    pop es
    pop ds
    pop si
    jz .eq
    push ax                     ; where it differed, for the verdict line
    lea ax, [di-1]
    mov [mz_diff], ax
    pop ax
    or ax, ax                   ; (ZF from the compare is gone: re-made)
    or sp, sp
.eq:
    ret

; BL = the chunk. mz_buf <- its pattern
mz_fill:
    push ax
    push cx
    push di
    push ds
    pop es
    mov di, mz_buf
    mov al, bl
    mov cx, MZ_CHUNK
.l:
    stosb
    add al, 7
    loop .l
    pop di
    pop cx
    pop ax
    ret

; BL = the chunk -> CF = 1 if mz_buf is not its pattern
mz_cmp:
    push ax
    push cx
    push si
    mov si, mz_buf
    mov al, bl
    mov cx, MZ_CHUNK
.l:
    cmp [si], al
    jne .no
    inc si
    add al, 7
    loop .l
    clc
    jmp short .x
.no:
    stc
.x:
    pop si
    pop cx
    pop ax
    ret

; BL = the chunk, DI = the direction -> OSAPI_XMEM_COPY of mz_buf and
; base + chunk * 4KB. CF as the cell's. Keeps BX
mz_copy:
    push bx
    push si
    mov ax, bx
    mov cl, 12
    shl ax, cl                  ; chunk * 4096: under 64KB, no carry
    mov dx, [mz_base+2]
    add ax, [mz_base]
    adc dx, 0
    push ds
    pop es
    mov si, mz_buf
    mov cx, MZ_CHUNK
    call OSAPI_XMEM_COPY
    pop si
    pop bx
    ret

mz_say:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mz_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mz_hex4:
    push ax
    mov al, ah
    call mz_hex2
    pop ax
mz_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

mz_s_caps:  db 'MEGAXM: caps ', 0
mz_s_alloc: db 'MEGAXM: alloc ', 0
mz_s_ok:    db 'MEGAXM: ok', 0
mz_s_no:    db 'MEGAXM: no ', 0
mz_s_bad:   db 'MEGAXM: bad ', 0
mz_s_sel:   db 'MEGAXM: sel ', 0
mz_s_code:  db ' code ', 0
mz_s_file:  db 'MEGAXM: file ', 0
mz_s_tmp:   db 'MEGAXM.TMP', 0
mz_s_big:   db 'ZBIG.TXT', 0
mz_s_at:    db 'MEGAXM: at ', 0
mz_s_i13:   db 'MEGAXM: i13 ok', 0

    OS88_BSS 14 + MZ_CHUNK
    OS88_IMAGE_END

mz_base     equ os88_image_end + 0
mz_xsel     equ os88_image_end + 4
mz_base2    equ os88_image_end + 6
mz_xsel2    equ os88_image_end + 10
mz_diff     equ os88_image_end + 12
mz_buf      equ os88_image_end + 14
