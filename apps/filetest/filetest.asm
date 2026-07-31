; =============================================================================
; os8088 - apps/filetest/filetest.asm
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
; Prefix ft_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FILETEST', ft_entry

FT_BIG    equ 1500            ; spans sectors AND ends mid-sector
FT_SMALL  equ 300             ; the replace is smaller: the chain must shrink
FT_ROWS   equ 24              ; result slots: exactly the checks below
FT_MAXF   equ 999             ; scratch-name ceiling for the fill check
FT_BSS_TOTAL equ 3064         ; see the bss layout after OS88_IMAGE_END

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

    ; 1. free space, before anything
    call OSAPI_FILE_DFREE       ; DX:AX = bytes, BX = sectors/cluster
    mov [ft_free0], ax
    mov [ft_free0+2], dx
    xor dx, dx
    call ft_note                ; expect success

    ; 2. a multi-sector write with a partial final sector
    call ft_fill                ; ft_buf = FT_BIG bytes of pattern
    mov si, ft_n1
    mov bx, ft_buf
    mov cx, FT_BIG
    call OSAPI_FILE_WRITE
    xor dx, dx
    call ft_note

    ; 3. read it back whole, and compare every byte
    call ft_wipe
    mov si, ft_n1
    mov bx, ft_in
    mov cx, FT_BIG
    call OSAPI_FILE_READ        ; AX = bytes read
    xor dx, dx
    call ft_note
    mov cx, FT_BIG
    call ft_same                ; CF=1 (and AX=1) if the bytes differ
    xor dx, dx
    call ft_note

    ; 4. a buffer too small must refuse without touching it
    mov si, ft_n1
    mov bx, ft_in
    mov cx, 100
    call OSAPI_FILE_READ
    mov dx, FERR_BIG
    call ft_note

    ; 5. replace it with a SHORTER file: the chain must shrink, not overlap
    mov si, ft_n1
    mov bx, ft_buf
    mov cx, FT_SMALL
    call OSAPI_FILE_WRITE
    xor dx, dx
    call ft_note
    call ft_wipe
    mov si, ft_n1
    mov bx, ft_in
    mov cx, FT_BIG
    call OSAPI_FILE_READ
    cmp ax, FT_SMALL
    je .sized
    stc                         ; wrong length: force a FAIL
    mov ax, 1
.sized:
    xor dx, dx
    call ft_note
    mov cx, FT_SMALL
    call ft_same
    xor dx, dx
    call ft_note

    ; 6. an empty file is a legal file
    mov si, ft_n3
    mov bx, ft_buf
    xor cx, cx
    call OSAPI_FILE_WRITE
    xor dx, dx
    call ft_note
    mov si, ft_n3
    mov bx, ft_in
    mov cx, FT_BIG
    call OSAPI_FILE_READ
    test ax, ax
    jz .empty
    stc
    mov ax, 1
.empty:
    xor dx, dx
    call ft_note

    ; 7. rename, and prove both ends of it
    mov si, ft_n1
    mov di, ft_n2
    call OSAPI_FILE_RENAME
    xor dx, dx
    call ft_note
    mov si, ft_n1
    mov bx, ft_in
    mov cx, FT_BIG
    call OSAPI_FILE_READ
    mov dx, FERR_NOENT
    call ft_note
    mov si, ft_n2
    mov bx, ft_in
    mov cx, FT_BIG
    call OSAPI_FILE_READ
    xor dx, dx
    call ft_note

    ; 8. renaming onto an existing name must be refused
    mov si, ft_n2
    mov di, ft_n3
    call OSAPI_FILE_RENAME
    mov dx, FERR_EXIST
    call ft_note

    ; 9. a malformed name never reaches the disk
    mov si, ft_nbad
    mov bx, ft_buf
    mov cx, 16
    call OSAPI_FILE_WRITE
    mov dx, FERR_NAME
    call ft_note

    ; 10. delete both, and prove the second delete finds nothing
    mov si, ft_n2
    call OSAPI_FILE_DELETE
    xor dx, dx
    call ft_note
    mov si, ft_n3
    call OSAPI_FILE_DELETE
    xor dx, dx
    call ft_note
    mov si, ft_n2
    call OSAPI_FILE_DELETE
    mov dx, FERR_NOENT
    call ft_note

    ; 11. fill the volume until it refuses. This is the path where a bug
    ;     costs a disk: the failing write dies mid-chain, and its rollback
    ;     (SPEC.md 18.4) must leave nothing allocated behind it.
    mov word [ft_made], 0
.fill:
    mov ax, [ft_made]
    call ft_name                ; ft_tmp = "FTnnn.TMP"
    mov si, ft_tmp
    mov bx, ft_buf
    mov cx, FT_BIG
    call OSAPI_FILE_WRITE
    jc .refused
    inc word [ft_made]
    mov ax, [ft_made]
    cmp ax, FT_MAXF
    jb .fill
    stc                         ; out of names before out of disk: FAIL
    mov ax, 1
    jmp short .refnote
.refused:
    cmp ax, FERR_FULL           ; only these two are legitimate refusals
    je .goodref
    cmp ax, FERR_DIRFULL
    je .goodref
    stc
    mov ax, 1
    jmp short .refnote
.goodref:
    clc
.refnote:
    xor dx, dx
    call ft_note

    ; 12. take it all back
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
    stc
    mov ax, 1
.delgood:
    xor dx, dx
    call ft_note

    ; 13. and free space must be back to the byte - no leaked clusters
    call OSAPI_FILE_DFREE
    mov [ft_free1], ax
    mov [ft_free1+2], dx
    cmp ax, [ft_free0]
    jne .leaked
    cmp dx, [ft_free0+2]
    je .clean
.leaked:
    stc
    mov ax, 1
.clean:
    xor dx, dx
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
    call OSAPI_FONT_STR
    mov si, ft_s_pass
    cmp byte [di+ft_res], 0
    je .label
    mov si, ft_s_fail
.label:
    mov cx, [ft_x]
    add cx, 28
    mov dx, bp
    call OSAPI_FONT_STR
    add bp, 10
    inc di
    jmp short .row
.totals:
    add bp, 4
    mov si, ft_s_free
    mov cx, [ft_x]
    mov dx, bp
    call OSAPI_FONT_STR
    mov ax, [ft_free0]          ; the low word is enough to show a change
    call ft_num5
    mov cx, [ft_x]
    add cx, 48
    mov dx, bp
    mov si, ft_num
    call OSAPI_FONT_STR
    mov ax, [ft_free1]
    call ft_num5
    mov cx, [ft_x]
    add cx, 96
    mov dx, bp
    mov si, ft_num
    call OSAPI_FONT_STR
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
ft_buf    equ os88_image_end + 64       ; FT_BIG bytes written
ft_in     equ os88_image_end + 1564     ; FT_BIG bytes read back
                                        ; total 3064 = FT_BSS_TOTAL
