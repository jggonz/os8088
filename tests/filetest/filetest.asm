; =============================================================================
; os8088 - tests/filetest/filetest.asm
;
; FILETEST, the file-API gate package (SPEC.md 18.4/20.3). Like fmtest and
; sbtest it never ships on the apps disks - their directory order is pinned
; - it gets its own scratch image:
;
;   make test TESTAPPS=build/filetest.img
;
; The whole suite runs in the entry proc (a legal UI-task context, SPEC.md
; 20.3) and the window just reports it: one row per check, PASS or FAIL,
; plus the free-space figures before and after. The last check is the one
; that matters most - free space must come back to exactly what it was, or
; the write path leaked clusters - and it is deliberately paired with the
; host-side `os88disk.py --verify`, which catches the leaks a running
; kernel cannot see.
;
; TWO slots, not three (SPEC.md 18.4.1). There is no separate big-file entry
; point to gate any more: 0x0120 and 0x0128 take a far pointer and a 32-bit
; count, and the suite's job is to prove that ONE pair of routines covers
; both shapes of caller. So checks 2..5 drive a 96KB file through them into a
; heap claim - once at a deliberately unaligned offset, once at zero - and
; every check from 6 down drives the same two slots into this package's own
; bss. A machine whose heap cannot fund the claim skips 2..5 and runs the
; rest; refusal is a normal path (SPEC.md 50.3).
;
; Prefix ft_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FILETEST', ft_entry

FT_BIG    equ 1500            ; spans sectors AND ends mid-sector
FT_SMALL  equ 300             ; the replace is smaller: the chain must shrink
FT_ROWS   equ 25              ; result slots: exactly the checks below, when
                              ; the big block's claim was funded (21 without)
FT_BIGKB  equ 128             ; the heap claim the big checks land in, KB
FT_BIGSZ_HI equ 0x0001        ; BIG.DAT is 96KB = 0x00018000 bytes, and its
FT_BIGSZ_LO equ 0x8000        ; byte at offset i is (i >> 9) - one distinct
FT_BIGPROBE equ 0x0088        ; value per 512-byte sector, so a destination
                              ; that failed to advance by segment reads a
                              ; DIFFERENT one. The probe is offset 0x11111,
                              ; sector 0x88, past the 64KB horizon the file
                              ; API used to stop at
FT_BIGOFF equ 0x0033          ; ...and the OFFSET the first big read is aimed
                              ; at inside the claim. Deliberately neither zero
                              ; nor a paragraph multiple: ES:BX is normalised
                              ; to seg+3 : 3 before the transfer walks the
                              ; segment (SPEC.md 18.4.1), and nothing else in
                              ; this suite proves that arithmetic
FT_BIGCAP_HI equ 0x0001       ; the capacity offered for that read: 0x1F000 =
FT_BIGCAP_LO equ 0xF000       ; 124KB, which is what is left of a 128KB claim
                              ; past FT_BIGOFF with room to spare
FT_MAXF   equ 999             ; scratch-name ceiling for the fill check
FT_BSS_TOTAL equ 3068         ; see the bss layout after OS88_IMAGE_END

; -----------------------------------------------------------------------------
; ft_entry - run the suite, then create the window that reports it
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF from wm_create
; -----------------------------------------------------------------------------
ft_entry:
    call ft_run
    push si
    mov si, ft_tpl
    call OSAPI_WM_CREATE
    pop si
    ret

; -----------------------------------------------------------------------------
; ft_run - the checks, in order
; in:  nothing
; out: nothing; ft_res / ft_n / ft_free0 / ft_free1 filled
; -----------------------------------------------------------------------------
ft_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                      ; every buffer here is our own bss

    ; EVERY ft_note below is preceded by `mov dx, <expected>`, NEVER by
    ; `xor dx, dx`. ft_note reads the CF the call under test left, and XOR
    ; clears CF - so an `xor` there silently turns every failed call that was
    ; expected to succeed into a PASS. That is not hypothetical: this suite
    ; shipped with fifteen of them.

    ; 1. free space, before anything. Taken FIRST, so the big-file checks
    ;    below are inside the span check 16 closes.
    call OSAPI_FILE_DFREE       ; DX:AX = bytes, BX = sectors/cluster
    mov [ft_free0], ax
    mov [ft_free0+2], dx
    mov dx, 0
    call ft_note                ; expect success

    ; --- 2..5: past the 64KB horizon, through the ordinary read and write ---
    ; There is no second "big" entry point any more (SPEC.md 18.4.1): the
    ; slots under test here are 0x0128 and 0x0120, the same two every check
    ; below uses. What is different is only where the buffer is - a heap claim
    ; (SPEC.md 50.3) rather than this package's own bss, because a package's
    ; region caps at one segment. A machine that cannot fund the claim records
    ; nothing rather than failing: refusal is a normal path.
    mov ax, FT_BIGKB
    call OSAPI_MEM_CLAIM        ; DX = the claim's base segment
    jc .nobig
    mov [ft_bseg], dx

    ; 2. read BIG.DAT into the claim at a NON-ZERO, non-paragraph offset.
    ;    ONE check, not two: the 32-bit size and the probe byte are both part
    ;    of "it read the file", and splitting them would need the call's CF
    ;    carried across two ft_note calls.
    mov es, dx
    mov bx, FT_BIGOFF
    mov si, ft_nbig
    mov cx, FT_BIGCAP_LO        ; DX:CX = the capacity
    mov dx, FT_BIGCAP_HI
    call OSAPI_FILE_READ        ; out DX:AX = the file's 32-bit size
    jc .rd1note                 ; CF and AX = FERR_* already
    call ft_bigchk              ; size + the byte at FT_BIGOFF + 0x11111
.rd1note:
    mov dx, 0                   ; expect success - and keep CF
    call ft_note

    ; 3. write those 96KB straight back out from the same ES:BX. This is the
    ;    write that used to be impossible: one call, a count that does not fit
    ;    16 bits, and a source that spans segments.
    mov es, [ft_bseg]
    mov bx, FT_BIGOFF
    mov si, ft_nbig2
    mov cx, FT_BIGSZ_LO         ; DX:CX = 96KB
    mov dx, FT_BIGSZ_HI
    call OSAPI_FILE_WRITE
    mov dx, 0
    call ft_note

    ; 4. poison the probe byte, then read the copy back at offset 0 - so the
    ;    normalised path and the already-aligned one are both exercised, and
    ;    the byte can only be right if 96KB actually reached the disk.
    mov es, [ft_bseg]
    mov ax, es
    add ax, 0x1000
    mov es, ax
    mov byte [es:0x1111], 0xAA
    mov es, [ft_bseg]
    xor bx, bx
    mov si, ft_nbig2
    mov cx, FT_BIGCAP_LO
    mov dx, FT_BIGCAP_HI
    call OSAPI_FILE_READ
    jc .rd2note
    call ft_bigchk0             ; size + the byte at 0x11111
.rd2note:
    mov dx, 0
    call ft_note

    ; 5. and take the 96KB back, so check 16 still measures a closed span
    mov si, ft_nbig2
    call OSAPI_FILE_DELETE
    mov dx, 0
    call ft_note

    mov dx, [ft_bseg]           ; the claim has done its work
    call OSAPI_MEM_FREE
    mov word [ft_bseg], 0
.nobig:
    push ds
    pop es                      ; every other buffer here is our own bss

    ; 6. a multi-sector write with a partial final sector
    call ft_fill                ; ft_buf = FT_BIG bytes of pattern
    mov si, ft_n1
    mov bx, ft_buf
    mov cx, FT_BIG
    mov dx, 0                   ; DX:CX = the count (SPEC.md 18.4.1)
    call OSAPI_FILE_WRITE
    mov dx, 0
    call ft_note

    ; 7. read it back whole, into a buffer inside our OWN segment, and
    ;    compare every byte. The in-segment destination is the other half of
    ;    the unified contract and every check from here down uses it.
    call ft_wipe
    mov si, ft_n1
    mov bx, ft_in
    mov cx, FT_BIG
    mov dx, 0
    call OSAPI_FILE_READ        ; DX:AX = bytes read
    mov dx, 0
    call ft_note
    mov cx, FT_BIG
    call ft_same                ; CF=1 (and AX=1) if the bytes differ
    mov dx, 0
    call ft_note

    ; 8. a buffer too small must refuse without touching it
    mov si, ft_n1
    mov bx, ft_in
    mov cx, 100
    mov dx, 0
    call OSAPI_FILE_READ
    mov dx, FERR_BIG
    call ft_note

    ; 9. replace it with a SHORTER file: the chain must shrink, not overlap
    mov si, ft_n1
    mov bx, ft_buf
    mov cx, FT_SMALL
    mov dx, 0
    call OSAPI_FILE_WRITE
    mov dx, 0
    call ft_note
    call ft_wipe
    mov si, ft_n1
    mov bx, ft_in
    mov cx, FT_BIG
    mov dx, 0
    call OSAPI_FILE_READ
    jc .sized                   ; a failure is its own FAIL; do not overwrite
    cmp ax, FT_SMALL            ; the code with a length verdict
    jne .wronglen
    or dx, dx                   ; a 96KB answer in a 300-byte file is a size
    jz .sized                   ; high word that did not get cleared
.wronglen:
    mov ax, 1
    stc
.sized:
    mov dx, 0
    call ft_note
    mov cx, FT_SMALL
    call ft_same
    mov dx, 0
    call ft_note

    ; 10. an empty file is a legal file
    mov si, ft_n3
    mov bx, ft_buf
    xor cx, cx
    mov dx, 0
    call OSAPI_FILE_WRITE
    mov dx, 0
    call ft_note
    mov si, ft_n3
    mov bx, ft_in
    mov cx, FT_BIG
    mov dx, 0
    call OSAPI_FILE_READ
    jc .empty
    mov bx, ax
    or bx, dx                   ; DX:AX must be zero, both words
    jz .empty
    mov ax, 1
    stc
.empty:
    mov dx, 0
    call ft_note

    ; 11. rename, and prove both ends of it
    mov si, ft_n1
    mov di, ft_n2
    call OSAPI_FILE_RENAME
    mov dx, 0
    call ft_note
    mov si, ft_n1
    mov bx, ft_in
    mov cx, FT_BIG
    mov dx, 0
    call OSAPI_FILE_READ
    mov dx, FERR_NOENT
    call ft_note
    mov si, ft_n2
    mov bx, ft_in
    mov cx, FT_BIG
    mov dx, 0
    call OSAPI_FILE_READ
    mov dx, 0
    call ft_note

    ; 12. renaming onto an existing name must be refused
    mov si, ft_n2
    mov di, ft_n3
    call OSAPI_FILE_RENAME
    mov dx, FERR_EXIST
    call ft_note

    ; 13. a malformed name never reaches the disk
    mov si, ft_nbad
    mov bx, ft_buf
    mov cx, 16
    mov dx, 0
    call OSAPI_FILE_WRITE
    mov dx, FERR_NAME
    call ft_note

    ; 14. delete both, and prove the second delete finds nothing
    mov si, ft_n2
    call OSAPI_FILE_DELETE
    mov dx, 0
    call ft_note
    mov si, ft_n3
    call OSAPI_FILE_DELETE
    mov dx, 0
    call ft_note
    mov si, ft_n2
    call OSAPI_FILE_DELETE
    mov dx, FERR_NOENT
    call ft_note

    ; 15. fill the volume until it refuses. This is the path where a bug
    ;     costs a disk: the failing write dies mid-chain, and its rollback
    ;     (SPEC.md 18.4) must leave nothing allocated behind it.
    mov word [ft_made], 0
.fill:
    mov ax, [ft_made]
    call ft_name                ; ft_tmp = "FTnnn.TMP"
    mov si, ft_tmp
    mov bx, ft_buf
    mov cx, FT_BIG
    mov dx, 0
    call OSAPI_FILE_WRITE
    jc .refused
    inc word [ft_made]
    mov ax, [ft_made]
    cmp ax, FT_MAXF
    jb .fill
    mov ax, 1                   ; out of names before out of disk: FAIL
    stc
    jmp short .refnote
.refused:
    cmp ax, FERR_FULL           ; only these two are legitimate refusals
    je .goodref
    cmp ax, FERR_DIRFULL
    je .goodref
    mov ax, 1
    stc
    jmp short .refnote
.goodref:
    clc
.refnote:
    mov dx, 0
    call ft_note

    ; 16. take it all back
    mov word [ft_i], 0
    xor bx, bx                  ; BX = failures
.del:
    mov ax, [ft_i]
    cmp ax, [ft_made]
    jae .deleted
    call ft_name
    mov si, ft_tmp
    call OSAPI_FILE_DELETE
    jnc .delok
    inc bx
.delok:
    inc word [ft_i]
    jmp short .del
.deleted:
    test bx, bx
    jz .delgood
    mov ax, 1
    stc
.delgood:
    mov dx, 0
    call ft_note

    ; 17. and free space must be back to the byte - no leaked clusters, and
    ;     that now covers the 96KB write and delete of checks 3 and 5
    call OSAPI_FILE_DFREE
    mov [ft_free1], ax
    mov [ft_free1+2], dx
    cmp ax, [ft_free0]
    jne .leaked
    cmp dx, [ft_free0+2]
    je .clean
.leaked:
    mov ax, 1
    stc
.clean:
    mov dx, 0
    call ft_note

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_note - record one check's outcome
; in:  CF + AX from the call under test, DX = the expected error code
;      (0 = the call was expected to succeed)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_note:
    push ax
    push bx
    push di
    mov bx, 1                   ; capture CF before anything else touches it
    jc .failed
    xor bx, bx
.failed:
    test bx, bx
    jz .wanted_ok
    cmp ax, dx                  ; it errored: the code must be the expected one
    je .pass
    jmp short .fail
.wanted_ok:
    test dx, dx                 ; it succeeded: only a 0 expectation passes
    jz .pass
.fail:
    mov bl, 1
    jmp short .store
.pass:
    xor bl, bl
.store:
    mov di, [ft_n]
    cmp di, FT_ROWS
    jae .out                    ; more checks than slots: drop the overflow
    mov [di+ft_res], bl
    inc word [ft_n]
.out:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_bigchk / ft_bigchk0 - did a 96KB read actually land, all of it?
; in:  DX:AX = the size the read reported; [ft_bseg] = the claim it went into.
;      ft_bigchk is for a read aimed at FT_BIGOFF, ft_bigchk0 for one aimed at
;      offset 0
; out: CF=0 all correct, or CF=1 with AX = FERR_IO (which is what ft_note
;      wants to see for a check that was expected to succeed)
; clobbers: AX, BX, ES, CF
;
; Two questions in one answer, because the caller has to hand ft_note a single
; CF: the size must be the real 32-bit one, and the byte at file offset
; 0x11111 must be 0x88. That byte is past the 64KB horizon, so it can only be
; there if the transfer walked the destination by SEGMENT - a loop that stayed
; inside one segment would have wrapped and written it somewhere else
; entirely, and one that ignored FT_BIGOFF would have put it 51 bytes low.
; -----------------------------------------------------------------------------
ft_bigchk:
    mov bx, FT_BIGOFF + 0x1111
    jmp short ft_bigchk_do
ft_bigchk0:
    mov bx, 0x1111
ft_bigchk_do:
    cmp dx, FT_BIGSZ_HI         ; the real size, which is more than 16 bits
    jne .bad
    cmp ax, FT_BIGSZ_LO
    jne .bad
    mov ax, [ft_bseg]
    add ax, 0x1000              ; the probe's segment: 64KB into the claim
    mov es, ax
    mov al, [es:bx]
    xor ah, ah
    cmp ax, FT_BIGPROBE
    jne .bad
    clc
    ret
.bad:
    mov ax, FERR_IO
    stc
    ret

; -----------------------------------------------------------------------------
; ft_name - build the nth scratch name, "FTnnn.TMP", into ft_tmp
; in:  AX = n (0..FT_MAXF-1)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_name:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, ft_tmp
    mov byte [di], 'F'
    mov byte [di+1], 'T'
    mov cx, 3
    mov di, ft_tmp + 5          ; write the digits backwards
.digit:
    dec di
    xor dx, dx
    mov bx, 10
    div bx
    add dl, '0'
    mov [di], dl
    loop .digit
    mov di, ft_tmp + 5
    mov byte [di], '.'
    mov byte [di+1], 'T'
    mov byte [di+2], 'M'
    mov byte [di+3], 'P'
    mov byte [di+4], 0
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_fill - lay a recognisable pattern into ft_buf
; in:  nothing
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_fill:
    push ax
    push cx
    push di
    mov di, ft_buf
    mov cx, FT_BIG
    xor ax, ax
.loop:
    mov [di], al
    inc di
    inc al
    loop .loop
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_wipe - poison ft_in, so a short read cannot look like a good one
; in:  nothing
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_wipe:
    push ax
    push cx
    push di
    mov di, ft_in
    mov cx, FT_BIG
    mov al, 0xAA
.loop:
    mov [di], al
    inc di
    loop .loop
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_same - compare CX bytes of ft_buf against ft_in
; in:  CX = byte count
; out: CF=0 identical, CF=1 different (AX = 1 then, for ft_note)
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
ft_same:
    push cx
    push si
    push di
    mov si, ft_buf
    mov di, ft_in
.loop:
    jcxz .same
    mov al, [si]
    cmp al, [di]
    jne .diff
    inc si
    inc di
    dec cx
    jmp short .loop
.diff:
    mov ax, 1
    stc
    jmp short .out
.same:
    xor ax, ax
    clc
.out:
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; ft_paint - W_PAINT: one row per check, then the free-space figures
; in:  SI = window ptr (content white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
ft_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    add ax, 6
    mov [ft_x], ax
    add dx, 6
    mov bp, dx                  ; BP = pen y
    mov al, CBLACK
    call OSAPI_SET_COLOR
    xor di, di                  ; DI = check index
.row:
    cmp di, [ft_n]
    jae .totals
    mov ax, di                  ; "NN "
    inc ax
    call ft_num2
    mov cx, [ft_x]
    mov dx, bp
    mov si, ft_num
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    mov si, ft_s_pass
    cmp byte [di+ft_res], 0
    je .label
    mov si, ft_s_fail
.label:
    mov cx, [ft_x]
    add cx, 28
    mov dx, bp
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add bp, 10
    inc di
    jmp short .row
.totals:
    add bp, 4
    mov si, ft_s_free
    mov cx, [ft_x]
    mov dx, bp
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    mov ax, [ft_free0]          ; the low word is enough to show a change
    call ft_num5
    mov cx, [ft_x]
    add cx, 48
    mov dx, bp
    mov si, ft_num
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    mov ax, [ft_free1]
    call ft_num5
    mov cx, [ft_x]
    add cx, 96
    mov dx, bp
    mov si, ft_num
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ft_num2 / ft_num5 - AX -> a NUL-terminated decimal string in ft_num
; in:  AX = the value
; out: ft_num holds it, 2 or 5 digits, zero-padded; registers preserved
; -----------------------------------------------------------------------------
ft_num2:
    push cx
    mov cx, 2
    jmp short ft_num_do
ft_num5:
    push cx
    mov cx, 5
ft_num_do:
    push ax
    push bx
    push dx
    push di
    mov di, ft_num
    add di, cx
    mov byte [di], 0
.digit:
    dec di
    xor dx, dx
    mov bx, 10
    div bx                      ; AX = quotient, DX = digit
    add dl, '0'
    mov [di], dl
    loop .digit
    pop di
    pop dx
    pop bx
    pop ax
    pop cx
    ret

; --- window template + strings ------------------------------------------------
ft_tpl:
    dw 150, 26, 240, 300
    dw ft_ttl, ft_paint, 0, 0

ft_ttl:    db 'File Test', 0
ft_s_pass: db 'PASS', 0
ft_s_fail: db 'FAIL', 0
ft_s_free: db 'free', 0

; the files this suite creates and removes again
ft_nbig:  db 'BIG.DAT', 0       ; shipped as DATA on the test image
ft_nbig2: db 'FTBIG.DAT', 0     ; ...and the 96KB copy check 3 writes of it
ft_n1:   db 'FTEST1.TXT', 0
ft_n2:   db 'FTEST2.TXT', 0
ft_n3:   db 'FTEST3.TXT', 0
ft_nbad: db 'BAD*NAME.TXT', 0

    OS88_BSS FT_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
ft_n      equ os88_image_end + 0        ; word: checks recorded
ft_x      equ os88_image_end + 2        ; word: paint scratch, text left
ft_free0  equ os88_image_end + 4        ; dword: free bytes before the suite
ft_free1  equ os88_image_end + 8        ; dword: and after it
ft_num    equ os88_image_end + 12       ; 8 bytes: decimal scratch
ft_made   equ os88_image_end + 20       ; word: scratch files created
ft_i      equ os88_image_end + 22       ; word: the delete loop's index
ft_tmp    equ os88_image_end + 24       ; 16 bytes: "FTnnn.TMP"
ft_res    equ os88_image_end + 40       ; FT_ROWS result bytes, 0 = PASS
ft_buf    equ os88_image_end + 66       ; FT_BIG bytes written (65 rounded up)
ft_in     equ os88_image_end + 1566     ; FT_BIG bytes read back
ft_bseg   equ os88_image_end + 3066     ; word: the big checks' claim, 0 = the
                                        ; heap could not fund one
                                        ; total 3068 = FT_BSS_TOTAL
