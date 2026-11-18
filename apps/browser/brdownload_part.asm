; Browser part 0: the Download Manager - its window, its Save dialog answer,
; its disk writes and the transfer engine (brdownload_engine.inc), its painter
; (brdownload_paint.inc) and its icons - plus Copy of a page selection and
; the rarely used UI (brcold.inc, SPEC.md 71.24).
; DS is always the primary Browser segment; every imported br_* symbol is an
; offset into that segment (tools/brdlsyms.py). The entry table leads the part
; for the primary's far-call vectors (br_dl_vec, brdownload_const.inc's BRDL_*
; order); each entry is a shim that makes the near call and returns far.
cpu 8086                        ; THE TARGET IS AN 8088 (SPEC.md 1). The part
                                ; is not an OS88_HEADER image, so it does not
                                ; get that macro's `cpu 8086` - and without it
                                ; nasm relaxes a far Jcc into the 386's 0F 8x
                                ; form, which an 8088 runs as POP CS: Copy
                                ; Selection jumped into segment 0 on the XT
bits 16
org 0

BR_UBUF     equ 160
BR_HOSTMAX  equ 64
BR_PATHMAX  equ 96

%include "os88api.inc"
%include "netpkg.inc"           ; the socket verbs; NET_CLASS is [net_cls]
%include "brdownload_const.inc"
%include "brdownload_part_symbols.inc"  ; $(BUILD)/, by -I

; A primary routine is called from the part far, `call far [br_dl_r_<name>]`,
; onto brdownload.inc's shim (br_dl_rs_<name>) - never near

    dw br_dlf_paint                 ; BRDL_PAINT
    dw br_dlf_copy                  ; BRDL_COPY
    dw br_dlf_open                  ; BRDL_OPEN
    dw br_dlf_link                  ; BRDL_LINK
    dw br_dlf_wake                  ; BRDL_WAKE
    dw br_dlf_close                 ; BRDL_CLOSE
    dw br_dlf_timer                 ; BRDL_TIMER
    dw br_dlf_up                    ; BRDL_UP
    dw br_dlf_drag                  ; BRDL_DRAG
    dw br_dlf_saved                 ; BRDL_SAVED
    dw br_dlf_main                  ; BRDL_MAIN
    dw br_dlf_step                  ; BRDL_STEP
    dw br_dlf_ctxshow               ; BRDL_CTXSHOW
    dw br_dlf_ctxclick              ; BRDL_CTXCLICK
    dw br_dlf_src                   ; BRDL_SRC
    dw br_dlf_submit                ; BRDL_SUBMIT
    dw br_dlf_bmcommit              ; BRDL_BMCOMMIT
    dw br_dlf_mapclick              ; BRDL_MAPCLICK
    dw br_dlf_save                  ; BRDL_SAVE
    dw br_dlf_optcmd                ; BRDL_OPTCMD
    dw br_dlf_saveas                ; BRDL_SAVEAS
    dw br_cx_formreset              ; BRDL_FORMRESET
    dw br_cx_labelfocus             ; BRDL_LABELFOCUS
%if ($ - $$) != BRDL_N * 2
%error "part 0's entry table does not match brdownload_const.inc's BRDL_N"
%endif

br_dlf_paint:   call br_dlx_paint
                retf
br_dlf_copy:    call br_copyselection
                retf
br_dlf_open:    call br_dl_openwin
                retf
br_dlf_link:    call br_dlx_link
                retf
br_dlf_wake:    call br_dlx_onwake
                retf
br_dlf_close:   call br_dlx_close
                retf
br_dlf_timer:   call br_dlx_timer
                retf
br_dlf_up:      call br_dlx_onup
                retf
br_dlf_drag:    call br_dlx_ondrag
                retf
br_dlf_saved:   call br_dlx_saved
                retf
br_dlf_main:    call br_dlx_mainclose
                retf
br_dlf_step:    call br_dlx_step
                retf
br_dlf_ctxshow: call br_cx_ctxshow
                retf
br_dlf_ctxclick: call br_cx_ctxclick
                retf
br_dlf_src:     call br_cx_srctoggle
                retf
br_dlf_submit:  call br_cx_submit
                retf
br_dlf_bmcommit: call br_cx_bmcommit
                retf
br_dlf_mapclick: call br_cx_mapclick
                retf
br_dlf_save:    call br_cx_saved
                retf
br_dlf_optcmd:  call br_cx_optcmd
                retf
br_dlf_saveas:  call OSAPI_FILE_HERE        ; where the instance stands, for
                mov [br_dl_dirbak], dx      ; br_fetch's way back (the words
                mov [br_dl_drivebak], bl    ; a download's dialog banks too)
                call br_dlf_pagename        ; SI = the page's own name
                mov al, FDLG_SAVE           ; ...then the chooser, answered by
                mov bx, [br_win]            ; the primary's br_saved
                mov di, br_saved
                call OSAPI_FILE_DLG
                jc .r                       ; refused: no answer will come
                inc byte [br_dl_depth]      ; raised: the part is held until
.r:             retf                        ; br_saved answers (br_dl_cold's
                                            ; own dec leaves this one)

; br_dlf_pagename - Save As suggests the PAGE'S OWN NAME, 8.3, as a download
; does (SPEC.md 71.34.14): the leaf of br_path, its stem to the last dot cut
; to eight legal characters and three of the extension; no extension is
; .HTM and no leaf is INDEX.HTM. Built in br_basescratch, a parse-time
; scratch nothing holds across a menu command - the chooser copies the name
; the moment it opens. Out: SI = the name. AX, BX, CX, DX, DI taken
br_dlf_pagename:
                push es                     ; br_dl_legal STOSBs: ES is the
                push ds                     ; kernel's in a menu command, and
                pop es                      ; the name went into it
                mov si, br_path
                mov bx, si
.leaf:          lodsb                       ; the leaf starts after the last
                call br_dl_nameend          ; '/', '\' or ':'
                je .ended
                cmp al, '/'
                je .sep
                cmp al, 5Ch                 ; '\'
                je .sep
                cmp al, ':'
                jne .leaf
.sep:           mov bx, si
                jmp short .leaf
.ended:         mov si, bx
                xor dx, dx
.dots:          lodsb
                call br_dl_nameend
                je .dotted
                cmp al, '.'
                jne .dots
                lea dx, [si-1]
                jmp short .dots
.dotted:        or dx, dx
                jnz .hasdot
                lea dx, [si-1]              ; no dot: the stem runs to the end
.hasdot:        mov si, bx
                mov di, br_basescratch
                mov cx, 8
                call br_dl_legal
                cmp di, br_basescratch
                jne .stem
                mov word [di], 'IN'         ; nothing legal: the server's index
                mov word [di+2], 'DE'
                mov byte [di+4], 'X'
                add di, 5
                jmp short .htm
.stem:          mov si, dx
                call br_dl_nameend.at
                je .htm
                inc si                      ; past the dot
                mov byte [di], '.'
                inc di
                mov cx, 3
                mov dx, 0FFFFh
                call br_dl_legal
                cmp byte [di-1], '.'
                jne .term
                dec di                      ; an extension with nothing legal
.htm:           mov word [di], '.H'
                mov word [di+2], 'TM'
                add di, 4
.term:          mov byte [di], 0
                mov si, br_basescratch
                pop es
                ret


; --- a form's Reset and a <label>'s click (1.8 size pass): moved from the
; primary as they were, its routines reached through br_dl_rtab -----------
; Restore the bounded controls belonging to form AL from their HTML defaults.
; Text controls retain only source offsets; choices retain one default bit;
; selects rescan their original OPTION list. No submitted values are changed.
br_cx_formreset:
    call .body
    retf
.body:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [br_resetform], al
    mov byte [br_resetidx], 0
.field:
    mov al, [br_resetidx]
    cmp al, [br_fn]
    jae .done
    call far [br_dl_r_fld]
    cmp byte [bx+FLD_FORM], 0FFh
    je .next
    mov al, [bx+FLD_FORM]
    cmp al, [br_resetform]
    jne .next
    cmp byte [bx+FLD_TYPE], 0
    je .text
    cmp byte [bx+FLD_TYPE], 1
    je .select
    mov al, [bx+FLD_SCAN]
    mov [bx+FLD_LEN], al
    call far [br_dl_r_choicepaint]
    jmp .next
.text:
    mov ax, [bx+FLD_FIRST]
    sub ax, [bx+FLD_SCAN]
    mov dx, [bx+FLD_FIRST+2]
    sbb dx, [bx+FLD_SCAN+2]
    or dx, dx
    jnz .next
    mov [br_resetlen], ax
    mov ax, [bx+FLD_SCAN]
    mov dx, [bx+FLD_SCAN+2]
    call far [br_dl_r_srcptr]
    mov [br_astart], si
    mov ax, si
    add ax, [br_resetlen]
    cmp ax, [br_srcwinlen]
    ja .next
    mov [br_aend], ax
    mov di, br_a_value
    call far [br_dl_r_getattr]
    jnc .textvalue
    xor si, si
    xor cx, cx
.textvalue:
    call far [br_dl_r_fsetval]
    jmp .next
.select:
    call br_cx_resetselect
.next:
    inc byte [br_resetidx]
    jmp .field
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Find the first explicitly selected OPTION, or the first option if none was
; selected, then restore both the visible label and successful-control value.
br_cx_resetselect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor ax, ax
    mov al, [br_resetidx]
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, br_seldefault
    mov ax, [si]
    mov dx, [si+2]
    or ax, dx
    jz .out
    call far [br_dl_r_srcptr]
    call far [br_dl_r_seloption]
    jc .out
    push si
    push dx
    mov si, dx
    call far [br_dl_r_srcabs]
    mov [bx+FLD_SCAN], ax
    mov [bx+FLD_SCAN+2], dx
    pop dx
    pop si
    call far [br_dl_r_selvalue]
    call far [br_dl_r_fsetval]
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Resolve the clicked LABEL's `for` attribute against retained field IDs, then
; perform the same activation as clicking that control directly.
br_cx_labelfocus:
    call .body
    retf
.body:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor ax, ax
    mov al, [br_subhit]
    mov bl, BR_SUBSZ
    mul bl
    mov bx, ax
    add bx, br_subs
    mov ax, [bx+SUB_AEND]
    sub ax, [bx+SUB_ASTART]
    mov dx, [bx+SUB_AEND+2]
    sbb dx, [bx+SUB_ASTART+2]
    or dx, dx
    jnz .out
    mov [br_fattrlen], ax
    mov ax, [bx+SUB_ASTART]
    mov dx, [bx+SUB_ASTART+2]
    call far [br_dl_r_srcptr]
    mov [br_astart], si
    mov ax, si
    add ax, [br_fattrlen]
    cmp ax, [br_srcwinlen]
    ja .out
    mov [br_aend], ax
    mov di, br_a_for
    call far [br_dl_r_getattr]
    jc .out
    mov ax, cx
    sub ax, si
    or ax, ax
    jz .out
    cmp ax, BR_IDMAX
    jae .out
    push si
    push cx
    mov di, br_labelfor
    mov bx, BR_IDMAX-1
    call far [br_dl_r_copyspan]
    pop cx
    pop si
    mov byte [br_resetidx], 0
.field:
    mov al, [br_resetidx]
    cmp al, [br_fn]
    jae .out
    xor ah, ah
    mov di, ax
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov di, ax
    add di, br_fids
    mov si, br_labelfor
.compare:
    mov al, [si]
    cmp al, [di]
    jne .next
    or al, al
    jz .activate
    inc si
    inc di
    jmp .compare
.activate:
    mov al, [br_resetidx]
    call far [br_dl_r_fld]
    cmp byte [bx+FLD_TYPE], 0
    je .text
    cmp byte [bx+FLD_TYPE], 1
    je .select
    call far [br_dl_r_choiceclick]
    jmp .out
.text:
    call far [br_dl_r_ffocus]
    jmp .out
.select:
    call far [br_dl_r_selcycle]
    jmp .out
.next:
    inc byte [br_resetidx]
    jmp .field
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


%include "brdownload_paint.inc"
%include "brdownload_engine.inc"

; This optional UI command is called only by the foreground package task.
%include "brdocmarks.inc"
%include "brcopyselection.inc"
%include "brcold.inc"

; The three icons, read only by brdownload_paint.inc. They are BLIT1 bands
; the kernel reads at ES:SI, so br_dl_blit1 points ES at this part's DATA
; segment ([br_dl_partdseg], op_seg's answer) - not at CS, which under
; kern_mega is a code selector (SPEC.md 20.12). 660 bytes the primary
; segment no longer carries (docs/plans/BROWSER-1.7-ROADMAP.md S1).
%include "assets/brdownload_icons.inc"
