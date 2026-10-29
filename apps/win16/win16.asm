; =============================================================================
; os8088 - apps/win16/win16.asm
;
; WIN16 (SPEC.md 124): a Windows 3.x program in an os8088 window. A Windows
; program is 8086 machine code for the processor already running, and Windows
; 1.x-3.0 ran such programs in real mode, so this package emulates no CPU: it
; loads the NE image, points the program's imports at code of its own, and
; runs it as a COROUTINE of the UI task (SPEC.md 124.2) - the program has the
; machine from a resume until it next waits, which is Windows 3.x's own
; cooperative contract.
;
; Launched with a document: DOS.O88 owns .EXE (SPEC.md 96.1) and hands an NE
; file to WIN16.O88 through OSAPI_PKG_START's document arm (SPEC.md 124.3).
; Launched empty, it says how to use it.
;
; S2 (docs/plans/WIN16-PLAN.md) is LOAD OR REFUSE: the loader (w16load.inc),
; the gate and the coroutine (w16core.inc), DOS's int 21h as the MS C runtime
; calls it (w16i21.inc), and the first KERNEL and USER functions (w16api.inc).
;
; LICENCE: apps/win16/ is GPL-2.0-or-later (SPEC.md 124.1, WIN16-PLAN.md D7):
; the loader follows winevdm's krnl386/ne_segment.c and task.c, which are
; Wine's (LGPL-2.1-or-later) and winevdm's (GPL-2). See COPYING.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'WIN16', w16_entry, OS88_F_PARTS

; --- the states -------------------------------------------------------------
W16S_IDLE    equ 0              ; launched empty
W16S_LOAD    equ 1              ; a document is waiting for the wake handler
W16S_REFUSED equ 2              ; [w16_l1]/[w16_l2] say why, and it stays up
W16S_RUN     equ 3              ; the program is running (or waiting)
W16S_ENDED   equ 4              ; it called int 21h AH=4Ch; the window closes

W16_MAXSEG   equ 64             ; segments a program may have (WinMine has 2,
                                ; CALC 5; the table is 4 bytes a segment)

; --- sizes the bss below is cut from (w16user.inc, w16gdi.inc) ---------------
W16_MAXCLS   equ 8              ; classes (SPEC.md 124.8)
W16_MAXWND   equ 8              ; windows
W16_QLEN     equ 32             ; posted messages (a power of two)
W16_MAXTMR   equ 4              ; timers
W16C_SIZE    equ 26             ; a class record
W16W_SIZE    equ 36             ; a window record
W16T_SIZE    equ 10             ; a timer record
MSG_SIZE     equ 18             ; a Windows MSG
W16_NSCAN    equ 0x59           ; scan codes with a virtual key
W16_NPTS     equ 32             ; LineTo's point buffer
W16_TXTMAX   equ 80             ; TextOut's longest run
W16_NOBJ     equ 16             ; brushes and pens made at once
W16_BS       equ 96             ; the text band's stride: 720 px, the guard,
                                ;   the spill, to even
W16_GBUF     equ W16_BS * 16    ; the band (16 rows), and SetDIBitsToDevice's
                                ;   4bpp block, never both at once
W16_RRMAX    equ 64             ; RoundRect's largest corner radius

; --- the parts (SPEC.md 20.12, 124.7.1): the System font, one face a display
W16P_SYS16   equ 0              ; VGA
W16P_SYS12   equ 1              ; EGA and Hercules
W16P_SYS08   equ 2              ; CGA

; --- bss (SPEC.md 21 step 5: zeroed by the loader) ---------------------------
%assign W16B 0
%macro W16BSS 2                 ; after the parts standard's OP_BSS (os88parts.inc)
%1 equ os88_image_end + OP_BSS + W16B
%assign W16B W16B + (%2)
%endmacro
    W16BSS w16_win, 2           ; our window
    W16BSS w16_state, 1         ; W16S_*
    W16BSS w16_locked, 1        ; 1 = the callback that resumed holds the lock
    W16BSS w16_name, 14         ; the program's 8.3 name, NUL-terminated
    W16BSS w16_dir, 2           ; ...the folder it is in
    W16BSS w16_vol, 1           ; ...and the volume
    W16BSS w16_l1, 48           ; the two lines the window shows
    W16BSS w16_l2, 48
    W16BSS w16_fseg, 2          ; the file, read whole (freed before the run)
    W16BSS w16_pseg, 2          ; the program's ONE claim
    W16BSS w16_ne, 2            ; the NE header's offset in the file
    W16BSS w16_nseg, 2          ; segments
    W16BSS w16_sbase, 2 * W16_MAXSEG    ; each segment's paragraph
    W16BSS w16_slen, 2 * W16_MAXSEG     ; ...and its size in bytes (0 = 64KB)
    W16BSS w16_dgroup, 2        ; DGROUP's paragraph, which is hInstance
    W16BSS w16_dgsize, 2        ; ...its size in bytes (0 = 64KB)
    W16BSS w16_psp, 2           ; the PSP-shaped block's paragraph
    W16BSS w16_ptop, 2          ; the paragraph past the claim
    W16BSS w16_nmod, 2          ; the program's module-reference count
    W16BSS w16_modid, 2 * 16    ; ...each one's W16M_* (0xFF = not ours)
    W16BSS w16_cpu186, 1        ; 1 = this CPU runs 186 code (SPEC.md 124.3)
    W16BSS w16_cbm, 2 * W16_MAXSEG      ; the census: each code segment's bitmap
    W16BSS w16_clast, 2 * W16_MAXSEG    ; ...and its last offset (w16cens.inc)
    ; --- the coroutine (w16core.inc) ---
    W16BSS w16_hss, 2           ; the UI task's SS (LOW_SEG) ...
    W16BSS w16_hsp, 2           ; ...and its SP at the resume
    W16BSS w16_hflags, 2        ; ...and its FLAGS, for the way back
    W16BSS w16_pss, 2           ; the program's SS:SP while it waits
    W16BSS w16_psp_, 2
    W16BSS w16_rax, 2           ; the register bank the epilogue loads
    W16BSS w16_rdx, 2
    W16BSS w16_rbx, 2
    W16BSS w16_rcx, 2
    W16BSS w16_res, 2
    W16BSS w16_rsi, 2           ; ...and SI/DI, written into the frame when
    W16BSS w16_rdi, 2           ;   [w16_rflags] says so (InitTask)
    W16BSS w16_rflags, 1
    W16BSS w16_pend, 1          ; 1 = the handler is waiting: yield
    W16BSS w16_nargb, 2         ; bytes the finishing call pops (-2: InitTask)
    W16BSS w16_idx, 2           ; the export row the gate is serving
    W16BSS w16_old21, 4         ; the vector int 21h had before the resume
    W16BSS w16_vecs, 5 * 8      ; int 21h AH=25h's vectors: number, far ptr
    W16BSS w16_exit, 1          ; AH=4Ch's AL
    W16BSS w16_mbtext, 64       ; MessageBox's text, one line, in OUR segment
    W16BSS w16_mbcap, 48        ; ...and its caption
    W16BSS w16_scratch, 16
    ; --- calls into the program (w16core.inc, SPEC.md 124.2.2) ---
    W16BSS w16_cbax, 2          ; the callee's answer, DX:AX
    W16BSS w16_cbdx, 2
    W16BSS w16_cbgo, 1          ; 1 = the gate enters the callee
    ; --- USER (w16user.inc, SPEC.md 124.8) ---
    W16BSS w16_inget, 1         ; 1 = waiting in GetMessage
    W16BSS w16_inpeek, 1        ; 1 = ...in a PeekMessage, for the wake
    W16BSS w16_mvcx, 2          ; MoveWindow's size and window
    W16BSS w16_mvcy, 2
    W16BSS w16_mvhwnd, 2
    W16BSS w16_inpaint, 1       ; 1 = resumed inside our W_PAINT
    W16BSS w16_ltook, 1         ; 1 = w16_lock took the gfx lock
    W16BSS w16_top, 1           ; the top-level window's slot + 1, 0 = none
    W16BSS w16_erase, 1
    W16BSS w16_take, 1
    W16BSS w16_titleptr, 2      ; what our title is when no alert is up
    W16BSS w16_wtitle, 48       ; ...the program window's text
    W16BSS w16_namebuf, 16      ; a class name, folded
    W16BSS w16_cwargs, 2        ; CreateWindow's arguments, on its stack
    W16BSS w16_cwslot, 2
    W16BSS w16_cls, W16_MAXCLS * W16C_SIZE
    W16BSS w16_wnd, W16_MAXWND * W16W_SIZE
    W16BSS w16_q, W16_QLEN * MSG_SIZE
    W16BSS w16_qhead, 1
    W16BSS w16_qcount, 1
    W16BSS w16_quit, 1          ; 1 = PostQuitMessage was called
    W16BSS w16_quitcode, 2
    W16BSS w16_tmr, W16_MAXTMR * W16T_SIZE
    W16BSS w16_msg, MSG_SIZE    ; the message being delivered
    W16BSS w16_mx, 2            ; the last pointer position, screen
    W16BSS w16_my, 2
    W16BSS w16_kascii, W16_NSCAN    ; each scan code's last ASCII
    W16BSS w16_capture, 2
    W16BSS w16_sm_hwnd, 2       ; the message being sent or posted
    W16BSS w16_sm_msg, 2
    W16BSS w16_sm_wp, 2
    W16BSS w16_sm_lp, 4
    W16BSS w16_dmg, 8           ; W_PAINT's damage, screen, inclusive
    ; --- GDI (w16gdi.inc) ---
    W16BSS w16_dcon, 1
    W16BSS w16_dctook, 1        ; 1 = the DC took the gfx lock
    W16BSS w16_dcclip, 1        ; 1 = ...and armed the kernel's clip
    W16BSS w16_dchide, 1        ; 1 = nothing of the window is visible
    W16BSS w16_dcpaint, 1
    W16BSS w16_dcwnd, 2
    W16BSS w16_ox, 2            ; the client's origin, screen
    W16BSS w16_oy, 2
    W16BSS w16_cl, 2            ; WIN16's clip, screen, inclusive
    W16BSS w16_ct, 2
    W16BSS w16_cr, 2
    W16BSS w16_cb, 2
    W16BSS w16_prc, 8           ; rcPaint, client
    W16BSS w16_pen, 2
    W16BSS w16_brush, 2
    W16BSS w16_font, 2
    W16BSS w16_curx, 2
    W16BSS w16_cury, 2
    W16BSS w16_gcol, 1
    W16BSS w16_objk, W16_NOBJ   ; each made object's W16K_*, 0 = free
    W16BSS w16_objc, W16_NOBJ   ; ...and its colour
    W16BSS w16_rop, 1           ; SetROP2's mode
    W16BSS w16_tcol, 1          ; the text colour, and its COLORREF
    W16BSS w16_trgb, 4
    W16BSS w16_rgb, 4
    W16BSS w16_lx1, 2
    W16BSS w16_ly1, 2
    W16BSS w16_ldx, 2
    W16BSS w16_ldy, 2
    W16BSS w16_lsx, 2
    W16BSS w16_lsy, 2
    W16BSS w16_npts, 2
    W16BSS w16_pts, W16_NPTS * 4
    W16BSS w16_rl, 2
    W16BSS w16_rt, 2
    W16BSS w16_rr, 2
    W16BSS w16_rb, 2
    W16BSS w16_tx, 2
    W16BSS w16_ty, 2
    W16BSS w16_txt, W16_TXTMAX + 2
    ; --- S4: the display, the face, text, RoundRect, DIBs ---
    W16BSS w16_disp, 1          ; the window's VID_* kind (VESA reads as VGA)
    W16BSS w16_mono, 1          ; 1 = a 1bpp display
    W16BSS w16_scrw, 2          ; ...its size
    W16BSS w16_scrh, 2
    W16BSS w16_capn, 2          ; SM_CYCAPTION - 1: what CreateWindow takes off
    W16BSS w16_home, 2          ; the folder WIN16.O88 is in, and its volume
    W16BSS w16_homev, 1
    W16BSS w16_face, 2          ; the System font part's segment, 0 = none
    W16BSS w16_fpart, 1         ; ...which part, + 1
    W16BSS w16_tn, 2            ; TextOut's characters
    W16BSS w16_tnb, 2           ; ...the band's bytes a row
    W16BSS w16_tb0, 2           ; ...the screen x of its bit 0
    W16BSS w16_r0, 2            ; ...the glyph rows drawn
    W16BSS w16_r1, 2
    W16BSS w16_gbpr, 2          ; ...a glyph's bytes a row (the word's high
                                ;   byte stays 0)
    W16BSS w16_son, 1           ; w16_span's growing rectangle: 1 = one is
    W16BSS w16_sx1, 2           ;   open, and its corners
    W16BSS w16_sx2, 2
    W16BSS w16_sy1, 2
    W16BSS w16_sy2, 2
    W16BSS w16_rrx, 2           ; RoundRect's radii, its pen, its insets
    W16BSS w16_rry, 2
    W16BSS w16_rpen, 1
    W16BSS w16_rrin, W16_RRMAX
    W16BSS w16_dres, 2          ; SetDIBitsToDevice: the rows drawn
    W16BSS w16_dwid, 2          ; ...the DIB's width, height, depth, palette
    W16BSS w16_dhgt, 2          ;   entry size and row stride
    W16BSS w16_dbpp, 1
    W16BSS w16_dpal, 2
    W16BSS w16_dstride, 2
    W16BSS w16_dx0, 2           ; ...the destination's top left, screen
    W16BSS w16_dy0, 2
    W16BSS w16_dsx, 2           ; ...the source column at its left, and row
    W16BSS w16_dtop, 2          ;   at its top (bottom up)
    W16BSS w16_dstart, 2        ; ...the rows lpBits holds
    W16BSS w16_dlines, 2
    W16BSS w16_dbits, 4
    W16BSS w16_dvw, 2           ; ...the visible width, its 4bpp bytes, the
    W16BSS w16_d4s, 2           ;   rows a block holds and the block's top
    W16BSS w16_dband, 2
    W16BSS w16_dby, 2
    W16BSS w16_dmap, 16         ; ...its palette, as os8088 colours
    W16BSS w16_gbuf, W16_GBUF
    OS88_BSS OP_BSS + W16B
W16_BSS_SIZE equ W16B

; =============================================================================
; w16_entry - package entry point (SPEC.md 20.2)
; in:  DS = ours, ES = KERNEL_SEG, gfx lock NOT held
; out: BX = window ptr, CF clear; CF set = abort
; =============================================================================
w16_entry:
    push es
    call op_load                    ; FIRST (os88parts.inc rule 1): our file's
    pop es                          ;   name, which the faces are read out of;
                                    ;   every row is lazy, so it cannot refuse
    call w16_pmcheck                ; SPEC.md 124.1, D6: real mode only
    jnc .real
    ret                             ; CF=1: the launch is torn down
.real:
    push si
    push di
    call OSAPI_FILE_HERE            ; the folder WIN16.O88 was launched from,
    mov [w16_home], dx              ;   banked before anything navigates
    mov [w16_homev], bl
    mov word [w16_capn], 19         ; VGA's, until the display is read
    mov si, w16_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr
    jc .out
    mov [w16_win], bx
    mov ax, w16_wake
    call OSAPI_WM_ONWAKE
    mov ax, w16_onclose
    call OSAPI_WM_ONCLOSE
    mov ax, w16_onup                ; the rest of SPEC.md 124.8's input table
    call OSAPI_WM_ONMOUSEUP
    mov ax, w16_ondrag
    call OSAPI_WM_ONDRAG
    mov ax, w16_onrclick
    call OSAPI_WM_ONRCLICK
    mov ax, w16_ontimer
    call OSAPI_WM_ONTIMER
    mov word [w16_titleptr], w16_name
    call w16_cputest                ; [w16_cpu186]: once, before anything runs
    call OSAPI_ARG_FILE             ; CF=1: launched empty
    jc .idle
    mov [w16_dir], dx
    mov [w16_vol], bl
    mov di, w16_name                ; copy the name out of KERNEL_SEG before
    mov cx, 13                      ; the next call can move it
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    mov si, w16_name
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_s_loading
    mov di, w16_l2
    call w16_strcpy
    mov byte [w16_state], W16S_LOAD
    mov bx, [w16_win]
    mov ax, w16_name                ; the title is the program's file name
    call OSAPI_WM_TITLE             ; (hidden yet, so it draws nothing)
    call OSAPI_WM_WAKE              ; ...and load it from the wake handler,
    jmp short .ok                   ;   the one callback without the gfx lock
.idle:
    call w16_idlelines
.ok:
    mov bx, [w16_win]
    clc
.out:
    pop di
    pop si
    ret

; =============================================================================
; w16_wake - the OSAPI_WM_ONWAKE handler (SPEC.md 74.1)
; in:  SI = our window, UI task, gfx lock NOT held
;
; A wake is a kick and a stale one is possible, so the state moves before the
; work: a second wake finds W16S_RUN or W16S_REFUSED and does nothing.
; =============================================================================
w16_wake:
    cmp byte [w16_state], W16S_ENDED
    je w16_after                    ; it ended inside a W_PAINT (w16_ppaint)
    cmp byte [w16_inpeek], 0
    jne w16_peekwake                ; a PeekMessage that found nothing
    cmp byte [w16_state], W16S_LOAD
    jne .out
    mov byte [w16_state], W16S_RUN
    call w16_load                   ; CF=1: refused, [w16_l1]/[w16_l2] say why
    jc .refused
    call w16_facein                 ; the System font for this display
    jnc .run
.refused:
    mov byte [w16_state], W16S_REFUSED
    call w16_freeprog               ; a face that failed leaves a loaded image
    call w16_repaint
    ret
.run:
    mov si, w16_s_running
    mov di, w16_l2
    call w16_strcpy
    call w16_repaint
    mov byte [w16_locked], 0        ; a wake holds no lock
    xor ax, ax
    xor dx, dx
    call w16_resume                 ; the program's first instruction
    call w16_after
.out:
    ret

; -----------------------------------------------------------------------------
; w16_after - what every resume's caller does when the resume comes back
; in:  anything a callback has; out: nothing
; The program is waiting (nothing to do) or it has ended, and an ended
; program takes its window with it, as a Windows program's did.
; -----------------------------------------------------------------------------
w16_after:
    cmp byte [w16_state], W16S_ENDED
    jne .out
    call w16_freeprog               ; the program's memory, now: the close is
    mov bx, [w16_win]               ;   deferred to the next UI pass
    call OSAPI_WM_CLOSE
.out:
    ret

; w16_freeprog - give back the program's claim, and the face, once
w16_freeprog:
    mov dx, [w16_pseg]
    or dx, dx
    jz .face
    call OSAPI_MEM_FREE
    mov word [w16_pseg], 0
.face:
    mov al, [w16_fpart]
    or al, al
    jz .out
    dec al
    call op_drop
    mov byte [w16_fpart], 0
    mov word [w16_face], 0
.out:
    ret

; -----------------------------------------------------------------------------
; w16_facein - read the display the window is on and fetch the System font
; Windows showed there (SPEC.md 124.7): a part of WIN16.O88, read from the
; folder it was launched from. CF=1: refused, with the reason in [w16_l2].
; UI task, no lock (the wake handler).
; -----------------------------------------------------------------------------
w16_facein:
    call w16_dispinit
    call w16_facepart               ; AL = the part
    push ax
    call OSAPI_FILE_HERE            ; where the user is, to be put back
    push dx
    push bx
    mov dx, [w16_home]
    mov bl, [w16_homev]
    call OSAPI_FILE_GOTO_QM         ; quietly, as w16_load stands in a folder
    pop bx
    pop dx
    pop ax
    push dx
    push bx
    push ax
    call op_fetch                   ; claims, reads, expands; a toast if not
    pop ax
    pop bx
    pop dx
    pushf
    push ax
    call OSAPI_FILE_GOTO_QM
    pop ax
    popf
    jc .no
    inc al
    mov [w16_fpart], al
    dec al
    call op_seg
    mov [w16_face], ax
    push es
    mov es, ax
    mov al, [es:W16F_ROWS]
    pop es
    xor ah, ah
    add ax, 3
    mov [w16_capn], ax              ; SM_CYCAPTION - 1: rows + 3
    clc
    ret
.no:
    mov si, w16_s_noface
    mov di, w16_l2
    call w16_strcpy
    stc
    ret

; -----------------------------------------------------------------------------
; w16_onclose - the close negotiator (SPEC.md 75.1)
; in:  SI = our window, gfx lock held; out: CF=0 the close goes ahead
; A program with a window is asked, as Windows asks it: WM_CLOSE, and the
; close is refused until the program has ended (w16_close, SPEC.md 124.8).
; One without a window is all on its own stack and in its own claim, which
; the kernel frees with the instance; int 21h is never ours between resumes
; (SPEC.md 124.2.1).
; -----------------------------------------------------------------------------
w16_onclose:
    jmp w16_close

; =============================================================================
; The window: two lines on white
; =============================================================================
W16_W   equ 320
W16_H   equ 64

; w16_paint - W_PAINT; in: SI = window, lock held; preserves everything.
; Once the program has a window, the window is the program's (w16_ppaint).
w16_paint:
    cmp byte [w16_top], 0
    je .lines
    jmp w16_ppaint
.lines:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 10
    mov si, w16_l1
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, w16_l2
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_repaint - the content, under a lock WE take (a wake has none). DOS.O88's
; dos_repaint shape. Preserves everything.
w16_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [w16_locked], 0
    jne .held
    call OSAPI_GFX_LOCK
.held:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT
    push ax
    push dx
    mov bx, [w16_win]
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    pop bx
    pop ax
    jc .nofill
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
.nofill:
    mov si, [w16_win]
    call w16_paint
    cmp byte [w16_locked], 0
    jne .out
    call OSAPI_GFX_UNLOCK
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Small helpers
; =============================================================================

; w16_strcpy - SI -> DI, NUL included; out: DI at the NUL; SI past it
w16_strcpy:
    push ax
.l:
    lodsb
    mov [di], al
    or al, al
    jz .z
    inc di
    jmp short .l
.z:
    pop ax
    ret

; w16_pmcheck - CF=1 under protected mode, having said so (SPEC.md 124.1, D6)
; Its own routine so that w16_entry, which loads the idle lines, is not a
; toasting procedure to tests/unit/t_toast.py. Not os88mega.inc's refusal:
; its words send the user to a MEGA apps disk, and WIN16 has no arm there.
w16_pmcheck:
    push ax
    call OSAPI_CPU_INFO             ; AH = CPU_F_*
    test ah, CPU_F_PM
    jz .real
    push cx
    push si
    push es
    push ds
    pop es
    mov si, w16_s_pm
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop si
    pop cx
    pop ax
    stc
    ret
.real:
    pop ax
    clc
    ret

; w16_idlelines - the window's two lines when it was opened with no program.
; Window text, never a toast - its own routine for w16_pmcheck's reason.
w16_idlelines:
    mov si, w16_s_idle1
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_s_idle2
    mov di, w16_l2
    jmp w16_strcpy

; w16_putdec - AX as decimal at DI, NUL-terminated; out: DI at the NUL
w16_putdec:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.put:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .put
    mov byte [di], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_cputest - does this CPU run 186 code? (SPEC.md 124.3)
; out: [w16_cpu186]; preserves everything but the flags
; 6Ah is `push imm8` on a 186 and a V20, and an alias of `jp rel8` on an
; 8088 and an 8086. With PF clear, the jump is not taken and the operand is a
; zero displacement anyway, so either way the next instruction runs: the one
; difference is whether SP moved. That is the exact question the loader has.
w16_cputest:
    push ax
    push bx
    call OSAPI_CPU_INFO
    mov byte [w16_cpu186], 1
    cmp al, CPU_8086
    jne .out                        ; a 286 or a 386 runs everything
    mov al, 1
    or al, al                       ; one bit set: odd parity, PF = 0
    mov bx, sp
    db 0x6A, 0x00                   ; push 0 / jp $+2
    cmp bx, sp
    je .no
    inc sp                          ; the 186's pushed word
    inc sp
    jmp short .out
.no:
    mov byte [w16_cpu186], 0
.out:
    pop bx
    pop ax
    ret

%include "win16/w16core.inc"
%include "win16/w16load.inc"
%include "win16/w16i21.inc"
%include "win16/w16api.inc"
%include "win16/w16user.inc"
%include "win16/w16gdi.inc"
%include "win16/w16cens.inc"
%include "win16/w16optab.inc"

; --- the window template (SPEC.md 11) ----------------------------------------
w16_tpl:
    dw 160, 120, W16_W, W16_H
    dw w16_title, w16_paint, w16_onkey, w16_onclick

w16_title:      db 'WIN16', 0
w16_s_pm:       db 'WIN16 needs real mode', 0
w16_s_idle1:    db 'WIN16 runs Windows 3.x programs.', 0
w16_s_idle2:    db 'Double-click one in a Disk window.', 0
w16_s_loading:  db 'Loading...', 0
w16_s_running:  db 'Running', 0
w16_s_noface:   db 'Its System font could not be read.', 0

; --- the alert (SPEC.md 75.3): MessageBox's face until S6 --------------------
%define OS88UI_ALERT 1
%define OS88UI_AEXTRA           ; OK / Cancel (MB_OKCANCEL)
%define OS88UI_NOGLYPH
%include "os88ui.inc"

; --- the parts (SPEC.md 20.12, 124.7.1): the System font, one face a display,
; each fetched for the display the program's window is on and dropped when it
; ends. tools/w16font.py builds them from apps/win16/fonts/
%include "os88parts.inc"
    OS88_PARTS_BEGIN 3
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_SYS16: sys16.txt
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_SYS12: sys12.txt
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_SYS08: sys08.txt
    OS88_PARTS_END

    OS88_IMAGE_END
