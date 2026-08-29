; =============================================================================
; tests/assoctest/assoctest.asm - the file type association gate (SPEC.md 54)
;
; Not shipped software: a gate in the sense of tests/filetest and tests/fmtest,
; answering pass/fail against a capability rather than doing anything for a
; user. It rides its own scratch image:
;
;   make test TESTAPPS=build/assoctest.img
;
; then double-click TEST.AST - NOT this package. Every line below is about
; what happens when a DOCUMENT is opened, so launching the program by hand is
; the control: rows 1-4 read '-' and that is correct, not a failure.
;
; What it proves:
;   1  the HEADER DECLARATION (SPEC.md 54.6) reached the kernel - the document
;      was routed here by a rule that exists only in this package's header,
;      and nothing was ever registered at runtime to put it there. This
;      package ships NO ICON, so its block sits at offset 32 rather than 96,
;      which is the layout arithmetic most likely to be got wrong.
;   2  OSAPI_ARG_FILE handed over a name, and it is TEST.AST
;   3  the locator works: FILE_GOTO then FILE_READ returns the bytes the image
;      was built with ('os8088' at the front)
;   4  ARG_FILE is READ-AND-CLEAR - asking twice reports nothing the second
;      time, which is what stops a later instance inheriting a document
;   5  OSAPI_ASSOC_SET takes a registration...
;   6  ...and refuses honestly when the table fills, rather than corrupting it
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'ASSOCTEST', at_entry, 2    ; bit 1: a declaration block, and
                                            ; deliberately no bit 0 (no icon)
    OS88_ASSOC16                            ; ...asserted to be at offset 32
    db 1                                    ; one extension
    OS88_ASSOC_EXT 'AST'
    OS88_ASSOC16_END

AT_CONT_W   equ 318
AT_ROWS     equ 6

at_entry:
    push si
    mov si, at_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [at_win], bx                    ; every API call below clobbers BX,
                                        ; and the loader wants it back as the
                                        ; window pointer - returning whatever
                                        ; the last call left there hands it a
                                        ; garbage window and the launch fails
                                        ; with nothing on screen to say why

    call OSAPI_ARG_FILE                 ; 1 + 2: were we handed a document?
    jc .noarg
    mov byte [at_v0], 1                 ; only a declaration could route it
    mov [at_clus], dx
    mov [at_drv], bl
    mov ax, KERNEL_SEG                  ; the name is a KERNEL pointer: load
    mov es, ax                          ; ES rather than trust it
    mov di, at_name
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .cp
    mov byte [di], 0
.named:
    push ds
    pop es
    mov si, at_name
    mov di, at_s_want
    call at_same
    jc .badname
    mov byte [at_v1], 1
.badname:

    mov dx, [at_clus]                   ; 3: the locator, and the bytes
    mov bl, [at_drv]
    call OSAPI_FILE_GOTO
    jc .noread
    push ds
    pop es
    mov bx, at_buf
    mov cx, 64
    xor dx, dx
    mov si, at_name
    call OSAPI_FILE_READ
    jc .noread
    or ax, ax
    jz .noread
    mov byte [at_v2], 1
.noread:

    call OSAPI_ARG_FILE                 ; 4: read-and-clear
    jc .cleared
    jmp short .noarg                    ; answered twice: leave the row '-'
.cleared:
    mov byte [at_v3], 1
.noarg:

    push ds
    pop es
    mov si, at_reg                      ; 5: a registration is taken
    call OSAPI_ASSOC_SET
    jc .flood
    mov byte [at_v4], 1
.flood:
    mov si, at_reg                      ; seed the mutable block from the
    mov di, at_fill                     ; static one - bss arrives zeroed and
    mov cx, 11                          ; a zero extension names nothing
    cld
    rep movsb
    mov cx, 26                          ; 6: more claims than the table has
    mov al, 'A'                         ; rows, so the tail MUST refuse
.fl:
    push cx
    mov [at_fill+2], al                 ; 'ZZA', 'ZZB', ... all distinct
    push ax
    mov si, at_fill
    call OSAPI_ASSOC_SET
    pop ax
    jnc .took
    mov byte [at_v5], 1
.took:
    inc al
    pop cx
    loop .fl
    mov bx, [at_win]                    ; ...so hand it back explicitly
    clc                                 ; ...and the success contract with it
.out:
    pop si
    ret

; at_same - are two NUL strings equal? in: SI, DI; out: CF=0 = equal
at_same:
    push si
    push di
    push ax
.c:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .c
.yes:
    pop ax
    pop di
    pop si
    clc
    ret
.no:
    pop ax
    pop di
    pop si
    stc
    ret

at_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_WM_CONTENT               ; AX = content left, DX = content top
    mov bx, ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    add dx, 6
    xor di, di
.row:
    mov si, di                          ; 8086 indexes through BX/SI/DI/BP
    shl si, 1                           ; and never through AX
    mov si, [at_labels + si]
    mov cx, bx
    add cx, 6
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    push bx
    mov bx, di
    add bx, at_v0                       ; the six verdicts are one contiguous
    mov al, [bx]                        ; run on purpose - at_paint indexes
    pop bx                              ; them by row
    mov si, at_s_dash
    or al, al
    jz .verdict
    mov si, at_s_pass
.verdict:
    mov cx, bx
    add cx, 262
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add dx, 12
    inc di
    cmp di, AT_ROWS
    jb .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

at_tpl:
    dw 90, 90, 320, 110
    dw at_ttl, at_paint, 0, 0

at_ttl:    db 'Assoc Test', 0
at_s_want: db 'TEST.AST', 0
at_s_pass: db 'PASS', 0
at_s_dash: db '-', 0
at_labels: dw at_l0, at_l1, at_l2, at_l3, at_l4, at_l5
at_l0:     db 'Header declaration routed it', 0
at_l1:     db 'Name is TEST.AST', 0
at_l2:     db 'Read it via its locator', 0
at_l3:     db 'ARG_FILE read-and-clear', 0
at_l4:     db 'ASSOC_SET takes', 0
at_l5:     db 'ASSOC_SET refuses when full', 0
at_reg:    OS88_ASSOC 'ZZZ', 'ASSTEST'

    OS88_BSS 112
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
; The six verdict bytes are one contiguous run because at_paint indexes them
; by row; keep them together and in the label order above.
at_v0     equ os88_image_end + 0
at_v1     equ os88_image_end + 1
at_v2     equ os88_image_end + 2
at_v3     equ os88_image_end + 3
at_v4     equ os88_image_end + 4
at_v5     equ os88_image_end + 5
at_clus   equ os88_image_end + 6      ; word
at_drv    equ os88_image_end + 8
at_win    equ os88_image_end + 10     ; word
at_name   equ os88_image_end + 12     ; 14
at_fill   equ os88_image_end + 26     ; 11: a mutated OS88_ASSOC block
at_buf    equ os88_image_end + 40     ; 72, to the end of the 112
