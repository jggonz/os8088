; =============================================================================
; brtabres.inc - the table composer's resident half (SPEC.md 71.34.2)
;
; The composer is in PART 7 (brext_part.asm, brtable.inc), out of the primary
; whole. What stays is the call, the far vectors part 9 reaches primary code
; through, and the fetch.
;
; The composer is PART 7's, with the colours and the fonts - one heap claim
; for the three (brext_part.asm). On a colour display br_cpload fetches it at
; the window's start and binds it here; elsewhere the first layout that meets
; a table sets [br_tbw], lays the table out inline, and the UI task's next
; wake fetches part 7 and lays the page out again (br_fnwake).
; =============================================================================

; --- br_tbcall - the layout's table: SI just past D_TAB1 -> br_table's out --
br_tbcall:
    test word [br_opts], OPT_TABLE  ; Options > Tables off: every one inline
    jz .inline
    cmp byte [br_tbok], 0
    je .absent
    call far [br_tbvec]
    ret
.absent:
    cmp byte [br_tbw], 0
    jne .inline
    mov byte [br_tbw], 1            ; the UI task's next wake fetches part 7
.inline:
    stc                             ; ...and meanwhile the table is a box
    ret

; --- br_tbload - part 7, on the UI task, and its table entry bound -> CF=1
; refused. On a colour display br_cpload already fetched it: binding is all
br_tbload:
    push ax
    push bx
    push dx
    push es
    call br_p7get                   ; ES = part 7's data, DX its code
    jc .no
    mov ax, [es:BRTB_ENTRY]
    mov [br_tbvec], ax
    mov [br_tbvec + 2], dx
    mov bx, br_tb_rtab              ; the vectors back: our own code segment
.seg:
    mov [bx + 2], cs
    add bx, 4
    cmp bx, br_tb_rtab + BRTB_RN * 4
    jb .seg
    mov byte [br_tbok], 1
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop dx
    pop bx
    pop ax
    ret

; --- br_p7get - PART 7, the colours, the faces and the composer in one claim
; (brext_part.asm), fetched if it is not in yet. out CF=0 ES = its data, DX =
; its code; CF=1 refused. AX clobbered. It is never dropped here: the other two
; may already hold far vectors into it, so a refusal leaves it as it was
br_p7get:
    mov al, 7
    call br_fetch                   ; (home first, 1.8)
    jc .ret
    mov al, 7
    call op_cseg
    mov dx, ax
    mov al, 7
    call op_seg
    or ax, ax
    jz .no
    or dx, dx
    jz .no
    mov es, ax
    mov [es:BRCL_SELF], ax
    clc
    ret
.no:
    stc
.ret:
    ret

; --- the primary routines the table body calls: a far vector each onto a shim -------
br_tb_rtab:
br_tb_r_coldec:     dw br_tb_s_coldec, 0
br_tb_r_colenc:     dw br_tb_s_colenc, 0
br_tb_r_docensure:  dw br_tb_s_docensure, 0
br_tb_r_docmap:     dw br_tb_s_docmap, 0
br_tb_r_emitirow:   dw br_tb_s_emitirow, 0
br_tb_r_imgdec:     dw br_tb_s_imgdec, 0
br_tb_r_imglaid:    dw br_tb_s_imglaid, 0
br_tb_r_imgreach:   dw br_tb_s_imgreach, 0
br_tb_r_lslot:      dw br_tb_s_lslot, 0
br_tb_r_lwrite:     dw br_tb_s_lwrite, 0
BRTB_RN equ ($ - br_tb_rtab) / 4

br_tb_s_coldec:     call br_coldec
                    retf
br_tb_s_colenc:     call br_colenc
                    retf
br_tb_s_docensure:  call br_docensure
                    retf
br_tb_s_docmap:     call br_docmap
                    retf
br_tb_s_emitirow:   call br_emitirow
                    retf
br_tb_s_imgdec:     call br_imgdec
                    retf
br_tb_s_imglaid:    call br_imglaid
                    retf
br_tb_s_imgreach:   call br_imgreach
                    retf
br_tb_s_lslot:      call br_lslot
                    retf
br_tb_s_lwrite:     call br_lwrite
                    retf

; --- br_tkick - the layout is done: a part 8 or 9 it wanted is the UI task's
; to fetch, so wake it - the worker may have posted its last wake BEFORE this
; layout ran, and nothing else would. All kept
br_tkick:
    cmp byte [br_cwant], 1
    je .kick
    cmp byte [br_tbw], 1
    je .kick
    cmp byte [br_fwant], 1
    jne .ret
.kick:
    push bx
    mov bx, [br_win]
    call OSAPI_WM_WAKE
    pop bx
.ret:
    ret
