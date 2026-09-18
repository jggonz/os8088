; =============================================================================
; tests/pathtest/pathtest.asm - OSAPI_FILE_PATH's gate (SPEC.md 19.2.4)
;
; Not shipped software. It answers the question the slot's design makes a
; claim about, and the claim is not "does it work" - it is **how many disk
; operations does it take**.
;
; WHY THE COUNT IS THE ASSERTION. A walk built out of the published slots
; calls OSAPI_FILE_GOTO_QM per level, which is dsk_chdir_q - and dsk_here_ok
; can only skip a mount when the caller is ALREADY STANDING at that exact
; cluster, which a walk never is. So each level MOUNTS, and a floppy mount
; outside a batch bracket re-reads LBA 0 and the FAT. dsk_path walks with the
; directory walker instead, which takes a cluster and stands nowhere, so it
; mounts at no level at all. That difference is invisible from inside the
; guest, which is why the harness counts what the FLOPPY CONTROLLER was asked
; to do, from outside (os88marty's disk()).
;
; TWO ARMS, each on its own keystroke so the harness's bracket contains the
; arm and nothing else - not the launch, not the first paint, not the icon
; harvest:
;
;   'p'  arm A: OSAPI_FILE_PATH, TWICE. The first is the measurement; the
;        second says whether SPEC.md 19.2.3's cached window is warm, which is
;        a claim of its own and one that fails as a number rather than a hang.
;   'g'  arm B: three round trips with OSAPI_FILE_GOTO_QM - the moves a
;        three-level package-side walk would make and NOTHING ELSE. It reads
;        no directory entry, so it is a FLOOR under such a walk rather than an
;        estimate of one.
;
; It must be launched from a SUBDIRECTORY for any of this to mean anything -
; the gate disk puts it three deep - because a package in the root answers `\`
; having read nothing.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PATHTEST', pt_entry, 0

PT_BUFSZ    equ 128

pt_entry:
    push si
    mov si, pt_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [pt_win], bx                ; the loader wants BX back as the window
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; pt_key - W_ONKEY. 'p' runs arm A, 'g' runs arm B.
; -----------------------------------------------------------------------------
pt_key:
    cmp al, 'p'
    je .path
    cmp al, 'P'
    je .path
    cmp al, 'g'
    je .moves
    cmp al, 'G'
    je .moves
    clc
    ret

.path:
    mov di, pt_path                 ; ES is not set here on purpose: an X cell
    mov cx, PT_BUFSZ                ; puts the CALLER's DS in ES for you
    call OSAPI_FILE_PATH
    jc .pfail
    mov [pt_len], cx
    mov byte [pt_stat], 0
    jmp short .again
.pfail:
    mov [pt_stat], al               ; FERR_* in AL, the buffer untouched
    mov word [pt_len], 0

.again:
    ; ...and again. The harness reads the counters between the two, so what
    ; this second call costs is 19.2.3's window answering warm.
    mov byte [pt_mark], 1
    mov di, pt_path2
    mov cx, PT_BUFSZ
    call OSAPI_FILE_PATH
    jc .p2fail
    mov byte [pt_stat2], 0
    jmp short .pdone
.p2fail:
    mov [pt_stat2], al
.pdone:
    mov byte [pt_done], 0xA5
    call pt_show
    clc
    ret

.moves:
    call OSAPI_FILE_HERE            ; DX = our cluster, BL = our drive
    jc .mfail
    mov [pt_clus], dx
    mov [pt_drv], bl
    mov cx, 3
.mloop:
    push cx
    xor dx, dx                      ; ...to the root
    mov bl, [pt_drv]
    call OSAPI_FILE_GOTO_QM
    mov dx, [pt_clus]               ; ...and back where we started
    mov bl, [pt_drv]
    call OSAPI_FILE_GOTO_QM
    pop cx
    loop .mloop
    mov byte [pt_mdone], 0xA5
    clc
    ret
.mfail:
    mov byte [pt_mdone], 0xEE
    clc
    ret

; -----------------------------------------------------------------------------
; pt_show - put the answer on the glass. ONE font_run, so the ground and the
; glyphs land in one pass (SPEC.md 6.1) and the line is never momentarily
; blank - which is the rule a gate has no excuse to break.
; -----------------------------------------------------------------------------
pt_show:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [pt_win]
    or bx, bx
    jz .out
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax
    add cx, 6
    add dx, 24
    mov si, pt_path
    cmp byte [pt_stat], 0
    je .draw
    mov si, pt_l_fail
.draw:
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pt_paint - W_PAINT. The hint, and the answer if there is one yet.
; -----------------------------------------------------------------------------
pt_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_WM_CONTENT
    mov cx, ax
    push ax
    add cx, 6
    add dx, 6
    mov si, pt_l_hint
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop ax
    mov cx, ax
    add cx, 6
    add dx, 18
    mov si, pt_l_wait
    cmp byte [pt_done], 0xA5
    jne .draw
    mov si, pt_path
    cmp byte [pt_stat], 0
    je .draw
    mov si, pt_l_fail
.draw:
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; =============================================================================
; DATA
; =============================================================================
pt_tpl:
    dw 60, 70, 340, 80
    dw pt_ttl, pt_paint, pt_key, 0

pt_ttl:     db 'Path Test', 0
pt_l_hint:  db 'p = OSAPI_FILE_PATH    g = the moves alone', 0
pt_l_wait:  db '(press p)', 0
pt_l_fail:  db 'REFUSED', 0

; -----------------------------------------------------------------------------
; THE RESULT BLOCK. tests/pathcost.py finds it at seg*16 + image, the way
; tests/heapcheck.py reads tests/heapfrag's - so these offsets are this gate's
; ABI with that script and must not be reordered.
; -----------------------------------------------------------------------------
pt_stat    equ os88_image_end + 0      ; byte: 0 = ok, else FERR_*
pt_stat2   equ os88_image_end + 1      ; byte: the second call's
pt_done    equ os88_image_end + 2      ; byte: 0xA5 once arm A has run
pt_mark    equ os88_image_end + 3      ; byte: 1 between the two calls
pt_mdone   equ os88_image_end + 4      ; byte: 0xA5 once arm B has run
pt_len     equ os88_image_end + 6      ; word: the path's length
pt_clus    equ os88_image_end + 8      ; word
pt_drv     equ os88_image_end + 10     ; byte
pt_win     equ os88_image_end + 12     ; word
pt_path    equ os88_image_end + 16     ; 128: the first call's answer
pt_path2   equ os88_image_end + 144    ; 128: the second's

    OS88_BSS 144 + PT_BUFSZ
    OS88_IMAGE_END
