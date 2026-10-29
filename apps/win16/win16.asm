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

    OS88_HEADER 'WIN16', w16_entry, 0

; --- the states -------------------------------------------------------------
W16S_IDLE    equ 0              ; launched empty
W16S_LOAD    equ 1              ; a document is waiting for the wake handler
W16S_REFUSED equ 2              ; [w16_l1]/[w16_l2] say why, and it stays up
W16S_RUN     equ 3              ; the program is running (or waiting)
W16S_ENDED   equ 4              ; it called int 21h AH=4Ch; the window closes

W16_MAXSEG   equ 64             ; segments a program may have (WinMine has 2,
                                ; CALC 5; the table is 4 bytes a segment)

; --- bss (SPEC.md 21 step 5: zeroed by the loader) ---------------------------
%assign W16B 0
%macro W16BSS 2
%1 equ os88_image_end + W16B
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
    OS88_BSS W16B
W16_BSS_SIZE equ W16B

; =============================================================================
; w16_entry - package entry point (SPEC.md 20.2)
; in:  DS = ours, ES = KERNEL_SEG, gfx lock NOT held
; out: BX = window ptr, CF clear; CF set = abort
; =============================================================================
w16_entry:
    call w16_pmcheck                ; SPEC.md 124.1, D6: real mode only
    jnc .real
    ret                             ; CF=1: the launch is torn down
.real:
    push si
    push di
    mov si, w16_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr
    jc .out
    mov [w16_win], bx
    mov ax, w16_wake
    call OSAPI_WM_ONWAKE
    mov ax, w16_onclose
    call OSAPI_WM_ONCLOSE
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
    cmp byte [w16_state], W16S_LOAD
    jne .out
    mov byte [w16_state], W16S_RUN
    call w16_load                   ; CF=1: refused, [w16_l1]/[w16_l2] say why
    jnc .run
    mov byte [w16_state], W16S_REFUSED
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
    mov dx, [w16_pseg]              ; the program's memory, now: the close is
    or dx, dx                       ; deferred to the next UI pass
    jz .nomem
    call OSAPI_MEM_FREE
    mov word [w16_pseg], 0
.nomem:
    mov bx, [w16_win]
    call OSAPI_WM_CLOSE
.out:
    ret

; -----------------------------------------------------------------------------
; w16_onclose - the close negotiator (SPEC.md 75.1)
; in:  SI = our window, gfx lock held; out: CF=0 the close goes ahead
; A program that is waiting is all on its own stack and in its own claim, and
; the kernel frees both with the instance; int 21h is never ours between
; resumes (SPEC.md 124.2.1). So there is nothing to negotiate.
; -----------------------------------------------------------------------------
w16_onclose:
    clc
    ret

; =============================================================================
; The window: two lines on white
; =============================================================================
W16_W   equ 320
W16_H   equ 64

; w16_paint - W_PAINT; in: SI = window, lock held; preserves everything
w16_paint:
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
%include "win16/w16cens.inc"
%include "win16/w16optab.inc"

; --- the window template (SPEC.md 11) ----------------------------------------
w16_tpl:
    dw 160, 120, W16_W, W16_H
    dw w16_title, w16_paint, 0, 0

w16_title:      db 'WIN16', 0
w16_s_pm:       db 'WIN16 needs real mode', 0
w16_s_idle1:    db 'WIN16 runs Windows 3.x programs.', 0
w16_s_idle2:    db 'Double-click one in a Disk window.', 0
w16_s_loading:  db 'Loading...', 0
w16_s_running:  db 'Running', 0

; --- the alert (SPEC.md 75.3): MessageBox's face until S6 --------------------
%define OS88UI_ALERT 1
%define OS88UI_AEXTRA           ; OK / Cancel (MB_OKCANCEL)
%define OS88UI_NOGLYPH
%include "os88ui.inc"

    OS88_IMAGE_END
