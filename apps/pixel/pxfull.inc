; =============================================================================
; os8088 - apps/pixel/pxfull.inc
;
; FULL SCREEN, the resident half (SPEC.md 106.23): the doors (View > Full
; Screen, F, Alt+Enter), the modes the window's display offers and View >
; Screen, and the BRACKET'S OWN LOOP - which polls the FULL-SCREEN part
; (apps/pixel/pxfull.asm, every pixel of it) and keeps a hidden decode of a
; next picture going inside the bracket, as W_ONWAKE keeps one going outside
; it: no event is dispatched in there (SPEC.md 53.1), so the loop is the
; pump and takes the worker's answer itself, then COMMITS it.
; =============================================================================

; px_fsoffer - the modes the display the window is on offers, by
; OSAPI_FSX_CAPS's bits for OUR window (SPEC.md 39.18.2): AL = a mask (bit m
; = PXM m), AH = the default (the lowest; PXM_NONE none), DL = the display's
; VID_*. Any context. Preserves all but AX, DX
px_fsoffer:
    push bx
    push cx
    push si
    mov bx, [px_win]
    call OSAPI_FSX_CAPS             ; AX = the FSXM bits, DL = the kind
    xchg ax, bx                     ; BX = the bits
    xor cx, cx                      ; CL = the mask
    mov si, px_fsxbits
.t:
    lodsw                           ; AL = an FSXM bit's number, AH = a PXM
    cmp al, 0xFF                    ; bit; FFh: the VGA family ends, so
    jne .b                          ; with any of it nothing below is wanted
    or cl, cl
    jnz .h
    cmp byte [px_bpp], 1            ; a 4bpp display with no VGA mode (an
    jbe .c                          ; EGA): its own sixteen, same mode
    or cl, 1 << PXM_DESK
.c:
    cmp dl, VID_CGA                 ; C160: a CGA's 200-line text alone (an
    jne .t                          ; EGA's is 350 lines, SPEC.md 98.3.12)
    test bl, 1 << FSXM_TEXT80
    jz .t
    or cl, 1 << PXM_C160
    jmp short .t
.b:
    cmp al, 0xFE
    je .h
    xchg ax, cx                     ; (CL = the bit's number)
    push bx
    shr bx, cl
    test bl, 1
    pop bx
    xchg ax, cx
    jz .t
    or cl, ah
    jmp short .t
.h:
    test bx, 1 << FSXM_HERC
    jz .d
    or cl, 1 << PXM_HERC
.d:
    mov al, cl
    mov ah, PXM_NONE                ; the default: the lowest offered
    xor bx, bx
.l:
    shr cl, 1
    jc .got
    inc bx
    cmp bx, PXM_N
    jb .l
    jmp short .out
.got:
    mov ah, bl
.out:
    pop si
    pop cx
    pop bx
    ret

; FSXM bit -> PXM bit: the VGA's three, FFh, the CGA's two, FEh
px_fsxbits: db FSXM_MODEX, 1 << PXM_MODEX, FSXM_VGA13, 1 << PXM_VGA13
            db FSXM_VGA12, 1 << PXM_VGA12, 0xFF, 0
            db FSXM_CGA320, 1 << PXM_CGA320, FSXM_CGA640, 1 << PXM_CGA640
            db 0xFE, 0

; px_fseff - AL = the mode full screen takes on the window's display: the
; one chosen (View > Screen) when that display offers it, else its default
; (PXM_NONE: none); AH = the mask, DL = the kind. Preserves all but AX, DX
px_fseff:
    call px_fsoffer
    push cx
    mov cl, [px_fsm]
    cmp cl, PXM_N
    jae .def
    mov ch, 1
    shl ch, cl
    test al, ch
    jz .def
    mov ah, cl
.def:
    xchg al, ah
    pop cx
    ret

; px_fsnext - AL = a mode, AH = a mask holding it: AL = the next one the mask
; holds, wrapping (AL itself when it is the only one). Preserves all but AL
px_fsnext:
    push cx
    mov cl, al
.n:
    inc cl
    and cl, PXM_N - 1
    mov ch, 1
    shl ch, cl
    test ah, ch
    jz .n
    mov al, cl
    pop cx
    ret

; px_fscycle - View > Screen: the next mode this display offers
px_fscycle:
    push ax
    push dx
    call px_fseff
    cmp al, PXM_NONE
    je .out
    call px_fsnext
    mov [px_fsm], al
    call px_menulive
.out:
    pop dx
    pop ax
    ret

; px_fsmenu - from px_menulive: View > Full Screen live when there is a
; picture to show and a mode to show it in; View > Screen names the mode,
; live when there is another. CX counts the items that changed. Preserves
; all but AX, BX, DX
px_fsmenu:
    call px_fseff                   ; AL = the mode, AH = the mask
    xor dx, dx
    cmp al, PXM_NONE
    je .f
    cmp byte [px_cur + PXR_HAVE], 0
    je .f
    cmp byte [px_busy], 0
    jne .f
    inc dx
.f:
    push ax
    mov ax, px_mi_full
    mov bx, px_miv + 2 * PX_MV_FULL
    call px_mset1
    pop ax
    xor dx, dx
    cmp al, PXM_NONE
    jne .s
    xor al, al                      ; (none: the first label, grey)
    jmp short .lab
.s:
    mov bl, ah                      ; two bits or more: live
    dec bl
    and bl, ah
    jz .lab
    inc dx
.lab:
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [px_fsmlab + bx]
    mov bx, px_miv + 2 * PX_MV_SCREEN
    ; fall through
; px_mset1 - the item in slot BX is string AX (MENU_DIS first) and live when
; DX = 1; CX counts a change. Preserves all but AX
px_mset1:
    add ax, dx
    cmp [bx], ax
    je .x
    mov [bx], ax
    inc cx
.x:
    ret

; px_fsdoor - AX = a key: F (either case) or Alt+Enter is full screen's door
; (SPEC.md 11.2.1, 11.2.1.1), taken BEFORE a key is spent stopping a
; slideshow, which it carries in; a card or an open list owns every key.
; CF = 0 it was the door; CF = 1 not ours. Preserves all
px_fsdoor:
    cmp ax, KEY_ALTENTER
    je .door
    push ax
    and al, 0xDF
    cmp al, 'F'
    pop ax
    jne .no
.door:
    push bx
    call px_dropon                  ; (either drop-down's list)
    pop bx
    jc .no                         ; (a list owns every key)
    call px_carddown                ; a card: the key takes it down, and
    jc .yes                         ; does nothing else (106.2)
    call px_hstop
    call px_touch
    call px_fsgo
.yes:
    clc
    ret
.no:
    stc
    ret

; px_fsgo - THE DOOR (SPEC.md 106.23): the mode for the window's display,
; the part fetched (a file read: before the bracket, where a refusal can be
; said), the view claim given back, and the bracket - again when M turned to
; the Desktop from a mode the bracket had set, which only a bracket anew can
; take back. Then the window as full screen left it. Lock held
px_fsgo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, px_s_nopico
    cmp byte [px_cur + PXR_HAVE], 0
    je .say
    mov si, px_s_busy
    cmp byte [px_busy], 0
    jne .say
    call px_fseff                   ; AL = the mode, AH = the mask, DL = kind
    mov si, px_s_nofs
    cmp al, PXM_NONE
    je .say
    mov [px_fsmode], al
    mov [px_fsmask], ah
    mov [px_fskind], dl
    mov al, PXPART_EDIT             ; (a claim full screen's eight want back:
    call px_pdrop                   ; SPEC.md 106.24)
    call px_vfree                   ; (the view's 30K first, so the part fits)
    mov al, PXPART_FULL
    mov bl, PXV_INIT
    call px_pcall
    jc .bad
    mov byte [px_fsin], 1
    mov byte [px_fschg], 0
    mov ax, [px_ncur]
    mov [px_fsidx], ax
.again:
    call px_vfree                   ; ...and again on M back from the Desktop,
                                    ; whose repaint claimed it (106.23's
                                    ; eight claims do not count it)
    mov byte [px_fsre], PXM_NONE
    inc word [px_fsn]
    mov ax, px_fsmain
    mov bx, [px_win]
    mov cx, FSXF_KEEPWORKER         ; the worker decodes a next picture
    call OSAPI_FSX_RUN
    jc .after
    mov al, [px_fsre]
    cmp al, PXM_NONE
    je .after
    mov [px_fsmode], al
    jmp short .again
.after:
    mov byte [px_fsin], 0
    mov al, PXPART_FULL
    call px_pdrop
    call px_fsafter
    jmp short .out
.bad:
    cmp ax, PXE_PART                ; op_fetch has said why
    je .out
    mov si, px_s_badpart
.say:
    call px_toast
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_vfree - the view claim given back, its tables and thumbnail with it
; (SPEC.md 106.23: 30 KB, and one claim of §50.2's eight while a hidden
; decode holds three). The next paint claims it again. Preserves all
px_vfree:
    push dx
    mov dx, [px_vseg]
    or dx, dx
    jz .x
    call OSAPI_MEM_FREE
    mov word [px_vseg], 0
.x:
    mov byte [px_tabok], 0
    mov byte [px_thok], 0
    pop dx
    ret

; px_fsafter - the window after the full screen, whose exit has set the
; desktop's mode and repainted every window (SPEC.md 53.6): a picture
; changed inside gets its tables from the worker, as the windowed
; slideshow's commit does (106.21) - the canvas its ground until then; the
; timer, whose event the exit drained, armed again; a slideshow stopped
px_fsafter:
    mov byte [px_slon], 0
    mov byte [px_slmem], 0
    cmp byte [px_fschg], 0
    je .same
    call px_viewclaim
    jc .nov
    call px_spawn
    jc .nov
    mov byte [px_abort], 0
    mov byte [px_slpost], 1
    mov byte [px_job], JOB_TABLES   ; LAST (px_busy is 2 since the exit)
    jmp short .same
.nov:
    mov byte [px_busy], 0           ; none now: the next paint makes them
.same:
    cmp byte [px_cur + PXR_HAVE], 0 ; an Esc in a Next whose decode had the
    jne .have                       ; shown master given back: the picture
    cmp byte [px_busy], 0           ; the window still names, opened again
    jne .have                       ; (and not an empty window under it)
    mov ax, [px_ncur]
    call px_navto
.have:
    call px_flags
    call px_update
    mov bx, [px_win]
    mov ax, PX_MEMT
    call OSAPI_WM_TIMER
    call px_tqarm
    call px_touch
    ret

; px_fscall - the full-screen part's DECODE, verb CL (CH, DI its argument):
; its CF/AX. Preserves all but AX
px_fscall:
    push bx
    mov al, PXPART_FULL
    mov bl, PXV_DECODE
    call px_pcall
    pop bx
    ret

; =============================================================================
; THE BRACKET (SPEC.md 53.1: SI = the window, DS = CS = ours, ES = KERNEL_SEG)
; =============================================================================

; px_fsmain - the part enters the mode and draws; this polls it, keeps a
; hidden decode of the next picture going and commits it, and paces itself
; on FSXW_FRAME, which yields to the kept worker while it waits (53.5.1)
px_fsmain:
    mov [px_win], si
    mov word [px_fsrec], px_cur
    mov byte [px_fsnav], 0
    mov byte [px_fsdue], 0
    mov cl, FSV_ENTER
    mov ch, [px_fsmode]
    call px_fscall
    jnc .loop
    jmp .exit                       ; refused: nothing drawn
.loop:
    call px_fspump                  ; the next picture, decoding hidden
    jc .exit                        ; (the shown one given back and lost)
    mov cl, FSV_POLL
    call px_fscall                  ; AX = what the user asked for
    jc .exit
    cmp ax, FSA_BUSY
    je .loop                        ; a render owed: no wait
    or ax, ax
    jz .idle
    cmp ax, FSA_LEAVE
    je .exit
    cmp ax, FSA_STOP
    jne .v1
    call px_fsstop
    jmp short .wait
.v1:
    cmp ax, FSA_SLIDE
    jne .v2
    call px_fsslide
    jmp short .wait
.v2:
    cmp ax, FSA_MODE
    jne .v3
    call px_fsmodekey
    jc .exit                        ; a bracket anew (the Desktop)
    jmp short .wait
.v3:
    call px_fsnavkey                ; Next, Prev, First, Last
    jmp short .wait
.idle:
    cmp byte [px_slon], 0           ; a slide whole on the glass: its
    je .wait                        ; deadline from now, the next one decoding
    call px_fsslnext
.wait:
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    jmp .loop
.exit:
    call px_hstop                   ; a hidden decode stopped, or a slide
    mov byte [px_fsnav], 0          ; decoded and waiting let go
    cmp byte [px_fschg], 0
    je .x
    call px_compose                 ; the window's words for the new picture
    call px_viewnew                 ; (none of it draws), at Fit
    mov al, [px_bpp]
    mov [px_tdepth], al
    xor al, al                      ; (the hidden decode's worker set
    mov [px_tabok], al              ; [px_tabok] without making a table, as
    mov [px_hok], al                ; it does for a slide: px_hcommit's own
    mov [px_tabdirty], al           ; three)
    mov byte [px_busy], 2           ; its canvas waits for the worker's tables
.x:
    ret

; px_fspump - the hidden decode inside the bracket: the pump when the worker
; asks, its answer when it gives one, the progress line while it runs, the
; commit when it is decoded and wanted (at once for a key's, at the deadline
; for a slide's). CF = 1: the decode gave the shown picture's master back
; and then refused - there is nothing left to show, so full screen ends
px_fspump:
    mov al, [px_hmode]
    cmp al, 2
    je .run
    cmp al, 3
    je .ready
    clc
    ret
.run:
    call px_pumpfill                ; (W_ONWAKE's first half)
    cmp byte [px_job], JOB_NONE
    jne .prog
    mov al, [px_wres]
    push ax
    call px_hdone                   ; a slide's: hmode 3, or let go
    pop ax
    cmp byte [px_hmode], 3
    je .ready
    mov word [px_fsrec], px_cur     ; refused or stopped: the shown one stays
    call px_fsprogoff
    cmp al, PXD_ABORT
    je .gone
    xor ah, ah
    call px_fsrefuse
.gone:
    cmp word [px_cur + PXR_MSEG], 0
    jne .ok
    stc
    ret
.prog:
    xor di, di                      ; the line follows the decode (the part
    mov cl, FSV_PROG                ; reads its rows and draws when the
    call px_fscall                  ; percent moved)
.ok:
    clc
    ret
.ready:
    cmp byte [px_fsnav], 0          ; a key's: now
    jne .commit
    cmp byte [px_slon], 0           ; a slide's: at its deadline
    je .ok
    cmp byte [px_fsdue], 0
    je .ok
    call OSAPI_GET_TICKS
    sub ax, [px_sldue]
    js .ok
.commit:
    call px_fscommit
    clc
    ret

; px_fscommit - the next picture, decoded hidden, IS the picture now: the old
; master given back, the part told (FSV_SHOW chooses its colours, renders)
px_fscommit:
    call px_hcommit0                ; the windowed commit's own head
    mov ax, [px_hidx]
    mov [px_fsidx], ax
    mov byte [px_fschg], 1
    mov byte [px_fsnav], 0
    mov byte [px_fsdue], 0
    mov word [px_fsrec], px_cur
    mov cl, FSV_SHOW
    call px_fscall
    ret

; px_fsstart - AX = a picture in the folder: decoded HIDDEN beside the one
; shown (106.21's machinery, unmasked: 106.23), the folder's place moved to
; it whatever its answer, so the next step goes past a picture refused
px_fsstart:
    mov [px_fsidx], ax
    call px_nfgo
    jc .gone
    mov dl, 2
    call px_hopen
    jc .ref
    mov word [px_fsrec], px_prev    ; the shown one is the banked one now
    ret
.gone:
    mov ax, PXD_NOFILE
.ref:
    call px_fsrefuse
    ret

; px_fsnavkey - AX = FSA_NEXT, _PREV, _FIRST or _LAST: that picture,
; committed the moment it is decoded. Not while one is decoding already
px_fsnavkey:
    cmp ax, FSA_NEXT                ; (nothing else is a step)
    jb .x
    cmp ax, FSA_LAST
    ja .x
    call px_fsdirty                 ; an edit unsaved: said, not left
    jc .x
    cmp byte [px_hmode], 0
    jne .x
    call px_foldok
    jc .x
    mov bx, ax
    mov ax, [px_fsidx]
    cmp bx, FSA_NEXT
    jne .p
    inc ax
    cmp ax, [px_nnames]
    jb .go
    xor ax, ax
    jmp short .go
.p:
    cmp bx, FSA_PREV
    jne .f
    or ax, ax
    jnz .p1
    mov ax, [px_nnames]
.p1:
    dec ax
    jmp short .go
.f:
    xor ax, ax
    cmp bx, FSA_FIRST
    je .e
    mov ax, [px_nnames]
    dec ax
.e:
    cmp ax, [px_ncur]
    je .x
.go:
    mov byte [px_fsnav], 1
    call px_fsstart
.x:
    ret

; px_fsslide - S: the slideshow, from full screen
px_fsslide:
    cmp byte [px_slon], 0
    jne px_fsstop
    call px_fsdirty
    jc .x
    call px_foldok
    jc .x
    mov byte [px_slon], 1
    mov byte [px_fsdue], 0
.x:
    ret

; px_fsstop - a key or a press while a slideshow runs: it stops, and a slide
; decoding or decoded is let go (106.21's rule: the input does nothing else)
px_fsstop:
    mov byte [px_slon], 0
    cmp byte [px_hmode], 0
    je .x
    call px_hstop
    mov word [px_fsrec], px_cur
    call px_fsprogoff
.x:
    ret

; px_fsslnext - the slideshow, a slide whole on the glass: its deadline set
; once, and the next one decoding hidden meanwhile
px_fsslnext:
    cmp byte [px_fsdue], 0
    jne .d
    call OSAPI_GET_TICKS
    add ax, PX_SLIDE
    mov [px_sldue], ax
    mov byte [px_fsdue], 1
.d:
    cmp byte [px_hmode], 0
    jne .x
    mov ax, [px_fsidx]
    inc ax
    cmp ax, [px_nnames]
    jb .s
    xor ax, ax
.s:
    mov byte [px_fsnav], 0          ; (shown at the deadline, not at once)
    call px_fsstart
.x:
    ret

; px_fsmodekey - M: the next mode this display offers. Between two modes the
; kernel sets the part enters it in place; to the Desktop from one it set,
; only a bracket anew brings the desktop's mode back: CF = 1, leave, and
; px_fsgo enters again. Not while a next picture decodes (its palette is in
; [px_pal] then, not the shown one's)
px_fsmodekey:
    cmp byte [px_hmode], 0
    jne .x
    mov al, [px_fsmode]
    mov ah, [px_fsmask]
    call px_fsnext
    cmp al, [px_fsmode]
    je .x
    mov [px_fsm], al
    cmp al, PXM_DESK
    je .re
    mov [px_fsmode], al
    mov ch, al
    mov cl, FSV_ENTER
    call px_fscall
    ret
.re:
    mov [px_fsre], al
    stc
    ret
.x:
    clc
    ret

; px_fsdirty - CF = 1 when the picture has an unsaved edit: the bottom band
; says "Save the edit first" - full screen has no room for a card to ask
; (SPEC.md 106.24). Clobbers AX, CL, DI
px_fsdirty:
    cmp byte [px_dirty], 0
    je .x
    mov di, px_s_sfirst
    mov cl, FSV_MSG
    call px_fscall
    stc
.x:
    ret

; px_fsprogoff - the progress line taken off. Clobbers AX, CL, DI
px_fsprogoff:
    mov di, 0xFFFF
    mov cl, FSV_PROG
    jmp px_fscall

; px_fsrefuse - AX = a PXD_*: the bottom band says it, the toast's words
; ("JPEG: damaged"). Clobbers AX, CL, DI
px_fsrefuse:
    call px_refwords
    jc .x
    mov di, px_cline
    mov cl, FSV_MSG
    call px_fscall
.x:
    ret
