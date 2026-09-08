; =============================================================================
; lzfile - THE GATE ON A COMPRESSED FILE READ TRANSPARENTLY (SPEC.md 20.14)
;
; docs/plans/O88-COMPRESSION-PLAN.md 13 makes this wave 5's gate. The disk carries one
; document twice - PLAIN.TXT as it is, PACKED.TXT inside a 'CZ' wrapper - so
; every assertion below is the two of them compared with each other. Nothing
; has to travel from the host and nothing has to be maintained beside the text.
;
; SIX VERDICTS, and the last two are the ones the design turns on:
;
;   read     the two files read back the same length and the same bytes
;   find     OSAPI_FILE_FIND reports PACKED.TXT's UNPACKED size, with bit 0 of
;            +22 set - which is what an application sizes its claim off, and
;            the reason the hint is in the directory entry at all
;   plainrec ...and PLAIN.TXT's own record has that bit CLEAR. Without this
;            row a cell that set it unconditionally would pass `find`
;   raw      OSAPI_FILE_READ_AT is the RAW path and stays raw: it delivers the
;            'CZ' header itself, which is what lets a chunked copy be exact
;   stamp    those raw bytes, written back out under another name, come back
;            EXPANDED - so the hint is derived from the bytes and a copy of a
;            compressed file is still a compressed file
;   clear    ...and a PLAIN file written over that same name reads back plain.
;            A stale mark would send it to the decoder, which would refuse it
;
; NEVER SHIPPED. Its own scratch image, the lzfence precedent:
;   make lzfiletest && python3 tests/lzfile.py
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'LZFILE', lz_entry

LZ_KB       equ 24             ; six 4KB buffers, exactly
LZ_A        equ 0x0000         ; PLAIN.TXT, expanded by nobody
LZ_B        equ 0x1000         ; PACKED.TXT, expanded by the kernel
LZ_C        equ 0x2000         ; PACKED.TXT raw, and the round trip
LZ_D        equ 0x3000         ; WPLAIN.TXT, the tight fixture as it is...
LZ_E        equ 0x4000         ; ...WINDOW.TXT, its LZB form, expanded into
                               ; exactly its own size...
LZ_F        equ 0x5000         ; ...and WLZ4.TXT, its LZ4 form, the same way
LZ_CAP      equ 4096           ; each buffer's capacity - and a multiple of
                               ; every cluster size this disk can have, which
                               ; is OSAPI_FILE_READ_AT's precondition

; -----------------------------------------------------------------------------
lz_entry:
    mov ax, LZ_KB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [lz_seg], dx

    ; --- 1. the two files, read whole ---------------------------------------
    mov si, lz_nplain
    mov bx, LZ_A
    call lz_read
    jc .rdno
    mov [lz_n1], ax
    or dx, dx
    jnz .rdno                   ; a 2KB document does not need a high word

    mov si, lz_npack
    mov bx, LZ_B
    call lz_read
    jc .rdno
    or dx, dx
    jnz .rdno
    cmp ax, [lz_n1]
    jne .rdno                   ; the expanded length is the plain length
    mov cx, ax
    mov di, LZ_A
    mov si, LZ_B
    call lz_cmp                 ; ...and so is every byte of it
    jc .rdno
    mov si, lz_yes
    jmp short .rdsay
.rdno:
    mov si, lz_no
.rdsay:
    mov di, lz_r_read
    call lz_say

    ; --- 2/3. what the listing says about each of them -----------------------
    mov si, lz_npack
    call lz_find
    jc .fno
    mov ax, [lz_rec+18]         ; +18 the size...
    cmp ax, [lz_n1]
    jne .fno                    ; ...which must be the UNPACKED one
    cmp word [lz_rec+20], 0
    jne .fno
    test byte [lz_rec+22], 1    ; +22 bit 0 - "the size above is what this
    jz .fno                     ; file expands to"
    mov si, lz_yes
    jmp short .fsay
.fno:
    mov si, lz_no
.fsay:
    mov di, lz_r_find
    call lz_say

    mov si, lz_nplain
    call lz_find
    jc .pno
    mov ax, [lz_rec+18]
    cmp ax, [lz_n1]
    jne .pno
    test byte [lz_rec+22], 1
    jnz .pno                    ; an uncompressed file must NOT carry the bit
    mov si, lz_yes
    jmp short .psay
.pno:
    mov si, lz_no
.psay:
    mov di, lz_r_plainrec
    call lz_say

    ; --- 4. the raw path stays raw ------------------------------------------
    mov si, lz_npack
    mov es, [lz_seg]            ; ES:BX, like every other file cell - and
    mov bx, LZ_C                ; entering with the kernel's ES is how this
    mov cx, LZ_CAP              ; row failed the first time it ran
    xor dx, dx
    xor ax, ax
    call OSAPI_FILE_READ_AT     ; -> DX:AX = the bytes delivered
    push ds
    pop es
    jc .rwno
    or dx, dx
    jnz .rwno
    or ax, ax
    jz .rwno
    mov [lz_n2], ax             ; the file's ON-DISK length
    cmp ax, [lz_n1]
    jae .rwno                   ; ...which is shorter than what it expands to
    push ds
    mov ds, [lz_seg]
    mov bx, LZ_C
    mov cx, [bx]                ; the first word of the file itself
    pop ds
    cmp cx, 0x5A43              ; 'CZ' - the raw path handed over the wrapper
    jne .rwno
    mov si, lz_yes
    jmp short .rwsay
.rwno:
    mov si, lz_no
.rwsay:
    mov di, lz_r_raw
    call lz_say

    ; --- 5. write those raw bytes back: a COPY, by the shortest route --------
    mov si, lz_ncopy
    mov es, [lz_seg]
    mov bx, LZ_C
    mov cx, [lz_n2]
    xor dx, dx
    call OSAPI_FILE_WRITE
    push ds
    pop es
    jc .stno
    mov si, lz_ncopy
    mov bx, LZ_C                ; ...and read the copy back over the top of
    call lz_read                ; the very bytes it was written from
    jc .stno
    or dx, dx
    jnz .stno
    cmp ax, [lz_n1]
    jne .stno                   ; EXPANDED: the write derived the hint
    mov cx, ax
    mov di, LZ_A
    mov si, LZ_C
    call lz_cmp
    jc .stno
    mov si, lz_yes
    jmp short .stsay
.stno:
    mov si, lz_no
.stsay:
    mov di, lz_r_stamp
    call lz_say

    ; --- 6. ...and a plain file over that same name clears the mark ----------
    mov si, lz_ncopy
    mov es, [lz_seg]
    mov bx, LZ_A
    mov cx, [lz_n1]
    xor dx, dx
    call OSAPI_FILE_WRITE
    push ds
    pop es
    jc .clno
    mov si, lz_ncopy
    mov bx, LZ_C
    call lz_read
    jc .clno                    ; a stale mark sends this to the decoder,
    or dx, dx                   ; which refuses prose - so CF=1 IS the failure
    jnz .clno
    cmp ax, [lz_n1]
    jne .clno
    mov cx, ax
    mov di, LZ_A
    mov si, LZ_C
    call lz_cmp
    jc .clno
    mov si, lz_yes
    jmp short .clsay
.clno:
    mov si, lz_no
.clsay:
    mov di, lz_r_clear
    call lz_say

    ; --- 7 and 8. THE TIGHT BUFFER (SPEC.md 20.14.2, 20.13.7) ---------------
    ; WINDOW.TXT is 4,096 bytes of text wrapped LZB and WLZ4.TXT the same
    ; bytes wrapped LZ4, each read into a capacity of exactly 4,096 - the
    ; tightest a caller sized from the size it was TOLD can be, and the case
    ; that used to need a sliding window for one format and be refused for
    ; the other. A stream ends in a raw tail now and expands in place inside
    ; exactly U, so both arrive whole, and nothing in the read knows they were
    ; the awkward case. The names are history: the window is gone
    mov si, lz_nwpl
    mov bx, LZ_D
    call lz_read
    jc .wno
    mov [lz_n3], ax
    mov si, lz_nwin
    mov bx, LZ_E
    call lz_read
    jc .wno
    or dx, dx
    jnz .wno
    cmp ax, [lz_n3]
    jne .wno                    ; ...the expanded length is the plain length
    mov cx, ax
    mov di, LZ_D
    mov si, LZ_E
    call lz_cmp                 ; ...and so is every byte
    jc .wno
    mov si, lz_yes
    jmp short .wsay
.wno:
    mov si, lz_no
.wsay:
    mov di, lz_r_window
    call lz_say

    mov si, lz_nwl4             ; ...and the LZ4 twin, which the build used to
    mov bx, LZ_F                ; refuse to write at all
    call lz_read
    jc .tno
    or dx, dx
    jnz .tno
    cmp ax, [lz_n3]
    jne .tno
    mov cx, ax
    mov di, LZ_D
    mov si, LZ_F
    call lz_cmp
    jc .tno
    mov si, lz_yes
    jmp short .tsay
.tno:
    mov si, lz_no
.tsay:
    mov di, lz_r_tight
    call lz_say

    mov si, lz_ncopy            ; leave the disk as we found it
    call OSAPI_FILE_DELETE

    mov si, lz_tpl
    call OSAPI_WM_CREATE
    jc .fail
    clc
    ret
.nomem:
.fail:
    stc
    ret

; -----------------------------------------------------------------------------
; lz_read - OSAPI_FILE_READ into [lz_seg]:BX with LZ_CAP of room
; in:  SI -> the name, BX = the offset. out: as OSAPI_FILE_READ. ES is ours
; -----------------------------------------------------------------------------
lz_read:
    push bx
    push cx
    mov es, [lz_seg]
    mov cx, LZ_CAP
    xor dx, dx
    call OSAPI_FILE_READ
    push ds
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; lz_cmp - CX bytes of [lz_seg]:SI against [lz_seg]:DI. out: CF=1 = differ
; -----------------------------------------------------------------------------
lz_cmp:
    push si
    push di
    push cx
    push ds
    push es
    mov ax, [lz_seg]
    mov ds, ax
    mov es, ax
    cld
    repe cmpsb
    pop es
    pop ds
    mov al, 0
    jz .same
    stc
    jmp short .out
.same:
    clc
.out:
    pop cx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; lz_find - walk OSAPI_FILE_FIND to the entry named by DS:SI
; out: CF=0 and lz_rec holds it; CF=1 = it is not in this directory
; -----------------------------------------------------------------------------
lz_find:
    push bx
    push cx
    push si
    push di
    mov [lz_want], si
    xor cx, cx
.next:
    push ds
    pop es
    mov di, lz_rec
    call OSAPI_FILE_FIND        ; CX = the ordinal, and the next one on the way
    jc .out                     ; back out
    mov si, [lz_want]
    mov di, lz_rec
    call lz_streq
    jne .next
    clc
.out:
    pop di
    pop cx
    pop si
    pop bx
    ret

; lz_streq - DS:SI against DS:DI, both NUL-terminated. out: ZF
lz_streq:
    push si
    push di
.ch:
    mov al, [si]
    cmp al, [di]
    jne .done
    or al, al
    jz .done
    inc si
    inc di
    jmp short .ch
.done:
    pop di
    pop si
    ret

; lz_say - copy the 3-byte verdict at DS:SI over the one at DS:DI
lz_say:
    push si
    push di
    push cx
    push es
    push ds
    pop es
    mov cx, 3
    cld
    rep movsb
    pop es
    pop cx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
lz_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 6
    mov si, lz_r_read
    mov bp, 6
.line:
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add dx, 10
    push cx                         ; step SI past this NUL-terminated line
    xor cx, cx
.scan:
    lodsb
    or al, al
    jnz .scan
    pop cx
    dec bp
    jnz .line
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

lz_tpl:
    dw 110, 84, 300, 90
    dw lz_ttl, lz_paint, 0, 0

lz_ttl:     db 'LZ file', 0

lz_nplain:  db 'PLAIN.TXT', 0
lz_npack:   db 'PACKED.TXT', 0
lz_ncopy:   db 'COPY.TXT', 0
lz_nwin:    db 'WINDOW.TXT', 0
lz_nwpl:    db 'WPLAIN.TXT', 0
lz_nwl4:    db 'WLZ4.TXT', 0

lz_yes:     db 'ok '
lz_no:      db 'BAD'

; The verdicts. Three bytes each, then the tag the host greps for - which is
; also what lz_paint draws, so the window and the assertion cannot disagree.
lz_r_read:      db 'xxx read', 0
lz_r_find:      db 'xxx find', 0
lz_r_plainrec:  db 'xxx plainrec', 0
lz_r_raw:       db 'xxx raw', 0
lz_r_stamp:     db 'xxx stamp', 0
lz_r_clear:     db 'xxx clear', 0
lz_r_window:    db 'xxx window', 0
lz_r_tight:     db 'xxx tight', 0

lz_seg:     dw 0
lz_n1:      dw 0                ; PLAIN.TXT's length
lz_n2:      dw 0                ; PACKED.TXT's length on the disk
lz_n3:      dw 0                ; WPLAIN.TXT's length
lz_want:    dw 0
lz_rec:     times OSAPI_FIND_SZ db 0

    OS88_IMAGE_END
