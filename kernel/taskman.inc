; =============================================================================
; jop - taskman.inc
;
; The built-in Task Manager window: a live CPU sweep graph with a percentage,
; two memory bars (conventional RAM and the kernel segment) and one row per
; task with its state and CPU share. tm_init creates the window HIDDEN at
; boot - it opens from Special > Task Manager (CMD_TASKS) - and spawns the
; tm_task background task. tm_task samples the scheduler's per-task clock
; accumulators every iteration whether or not the window can be seen (so the
; accumulators stay bounded and the history ring keeps flowing, same
; discipline as the Clock task's time-keeping) and only the drawing is
; conditional on visible + not wm_obscured, re-checked under the gfx lock
; (SPEC.md 14). Like every background task it uses jop primitives only -
; no BIOS calls (SPEC.md 7); the one int 12h lives in tm_init, which runs on
; task 0 during the boot sequence.
;
; The graph is deliberately a SWEEP, not a scroll: an update repaints exactly
; three of the 288 columns (the newest bar plus a two-column white gap that
; runs ahead of it), which keeps a 4.77 MHz XT from redrawing the whole
; interior twice a second.
; =============================================================================

TM_HIST_N   equ 288             ; history ring entries = graph interior columns
TM_BAR_W    equ 288             ; interior width of the two memory bars, px
TM_RESERVED_KB equ 192          ; RAM jop reserves: the 64K below KERNEL_SEG
                                ; (IVT/BDA/boot), the 64K kernel segment
                                ; (code, .bss, task stacks, the loaded-program
                                ; region, task 0's stack at 0xFFFE) and the
                                ; 64K SAVE_SEG save-under heap

; --- content-relative geometry, INCLUSIVE (see wm_content for the origin) ----
TM_LBL_X    equ 6               ; left margin for every label / text line
TM_RIGHT    equ 295             ; right edge of every framed element
TM_CPU_Y    equ 4               ; "CPU" label row
TM_PCT_X    equ 260             ; CPU percent string, right of the label
TM_PCT_Y2   equ 11              ; its erase rect bottom

TM_GF_X1    equ 6               ; graph frame
TM_GF_Y1    equ 14
TM_GF_Y2    equ 65
TM_GX0      equ 7               ; graph interior: column 0 ... column 287
TM_GY0      equ 15              ; interior top row
TM_GY1      equ 64              ; interior bottom row (50 rows tall)

TM_MEM_Y    equ 72              ; "Memory" label row
TM_RF_Y1    equ 84              ; conventional-RAM bar frame
TM_RF_Y2    equ 97
TM_RI_Y1    equ 85              ; ... its interior
TM_RI_Y2    equ 96
TM_RTXT_Y   equ 101             ; "RAM ... used ... free ..." line
TM_RTXT_Y2  equ 108

TM_KF_Y1    equ 114             ; kernel-segment bar frame
TM_KF_Y2    equ 127
TM_KI_Y1    equ 115             ; ... its interior
TM_KI_Y2    equ 126
TM_KTXT_Y   equ 131             ; "kernel ... program ... free ..." line
TM_KTXT_Y2  equ 138

TM_BX0      equ 7               ; both memory bars: interior x 7..294
TM_BX1      equ 294

TM_SEP_Y    equ 146             ; separator rule above the task list
TM_HDR_Y    equ 152             ; column headers
TM_COL_NAME equ 6               ; task-row columns
TM_COL_STATE equ 120
TM_COL_PCT  equ 180
TM_ROW_Y0   equ 166             ; row r top = TM_ROW_Y0 + r*TM_ROW_H
TM_ROW_H    equ 14
TM_ROWS_Y2  equ 221             ; bottom of the rows erase block
TM_RB_X1    equ 212             ; per-row CPU bar frame
TM_RB_X2    equ 295
TM_RB_IX    equ 213             ; ... its interior, 82 px wide
TM_RB_W     equ 82

; -----------------------------------------------------------------------------
; tm_init - reset module state, create the (hidden) window, spawn tm_task
; in:  nothing
; out: nothing; window ptr stored in tm_win
; clobbers: nothing (flags only)
; .bss is not zeroed by the loader and tm_task starts running the instant it
; is spawned, so every variable it reads is written here first. int 12h is
; safe at this point: we are still task 0 in the boot sequence (SPEC.md 7
; only bars the BIOS from the running tasks).
; -----------------------------------------------------------------------------
tm_init:
    push ax
    push bx
    push cx
    push si
    push di

    mov word [tm_hpos], 0
    mov byte [tm_cpu], 0
    mov word [tm_totw], 0
    mov byte [tm_shift], 8
    mov word [tm_cx], 0
    mov word [tm_cy], 0

    mov si, tm_pct_tab          ; no rep stosb: ES belongs to the caller and
    mov cx, MAX_TASKS           ; these two blocks are small enough
.zpct:
    mov byte [si], 0
    inc si
    loop .zpct

    mov si, tm_hist
    mov cx, TM_HIST_N
.zhist:
    mov byte [si], 0
    inc si
    loop .zhist

    int 0x12                    ; AX = conventional memory, KB
    test ax, ax
    jnz .gotram
    mov ax, 640                 ; a BIOS that answers 0 must not divide by 0
.gotram:
    mov [tm_ram_kb], ax

    mov si, tm_tpl_win
    call wm_create
    mov [tm_win], bx            ; stays hidden until CMD_TASKS

    mov ax, tm_task
    mov si, tm_name
    call task_spawn

    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
; Width 304 = TM_RIGHT (295) + the same 6px margin the left side has, plus the
; two frame columns; a wider window just adds a lopsided right gutter.
tm_tpl_win: dw 150, 100, 304, 242, tm_ttl, tm_paint, 0, 0
tm_ttl:     db 'Task Manager', 0
tm_name:    db 'Tasks', 0       ; T_NAME of our own task

tm_s_cpu:   db 'CPU', 0         ; doubles as the graph label and the column head
tm_s_mem:   db 'Memory', 0
tm_s_htask: db 'Task', 0
tm_s_hstate: db 'State', 0
tm_s_ram:   db 'RAM ', 0        ; "RAM 640K  used 192K  free 448K"
tm_s_used:  db 'K  used ', 0
tm_s_free:  db 'K  free ', 0
tm_s_kk:    db 'K', 0
tm_s_kern:  db 'kernel ', 0     ; "kernel 21K  program 0K  free 43K"
tm_s_prog:  db 'K  program ', 0
tm_s_run:   db 'Run', 0
tm_s_ready: db 'Ready', 0
tm_s_sleep: db 'Sleep', 0
tm_s_tfb:   db '(task ', 0      ; fallback name for a task with T_NAME = 0

; =============================================================================
; the background task
; =============================================================================

; -----------------------------------------------------------------------------
; tm_task - background task: sample the CPU accountancy, redraw twice a second
; in:  nothing (task entry; never returns)
; The sampling runs every iteration - it drains sch_cpu_take's accumulators,
; which must not be left to grow while the window is closed - and only the
; drawing is gated on visible + not wm_obscured, re-checked under the lock
; (SPEC.md 14). tm_sample runs OUTSIDE the lock: it touches no screen state,
; and holding the lock across it would only lengthen the critical section.
; -----------------------------------------------------------------------------
tm_task:
.loop:
    mov ax, 9                   ; ~0.5 s
    call task_sleep

    call tm_sample              ; unconditional: keeps the ring and the
                                ; accumulators moving even while hidden

    call gfx_lock
    mov bx, [tm_win]
    test word [bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14)
    jz .skip
    call wm_obscured
    jc .skip
    call tm_draw_dyn
.skip:
    call gfx_unlock
    jmp .loop

; -----------------------------------------------------------------------------
; tm_sample - take a CPU snapshot, turn it into percentages, advance the ring
; in:  nothing (no gfx lock needed - touches no screen state)
; out: nothing; [tm_cpu], [tm_pct_tab], [tm_hist], [tm_hpos] updated
; clobbers: nothing (flags only)
;
; sch_cpu_take hands us MAX_TASKS per-task accumulators followed by the idle
; accumulator, all in PIT input clocks since the previous call. Overall CPU
; usage is 100 - idle%, which is what a user means by "CPU": task 0's own
; poll loop is charged to idle by ui.inc's sch_idle_flag.
; -----------------------------------------------------------------------------
tm_sample:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call sch_cpu_take

    xor ax, ax                  ; DX:AX = sum of all MAX_TASKS+1 accumulators
    xor dx, dx
    xor si, si                  ; SI = byte index into the snapshot arrays
    mov cx, MAX_TASKS+1
.sum:
    add ax, [si+sch_snap_lo]
    adc dx, [si+sch_snap_hi]
    add si, 2
    loop .sum

    ; Scale the total down to a single word. A nominal 0.5 s interval is about
    ; 600k PIT clocks, so >> 8 leaves plenty of headroom - but the interval is
    ; NOT bounded by the sleep. tm_task blocks on gfx_lock, and menu_track
    ; holds that lock for as long as the user keeps a pull-down open; disk_read
    ; holds sch_lock across its int 13h retries, which stops task switching
    ; altogether. Past 2^24 clocks (14 s) a >> 8 total no longer fits a word.
    ; A percentage is a ratio, so shifting BOTH sides by 16 instead rescues the
    ; sample exactly and only costs resolution - whereas reporting nothing
    ; would report 0% CPU for precisely the interval the machine was pegged,
    ; and burn that false zero into the history ring for a whole sweep.
    mov byte [tm_shift], 8
    mov bl, ah                  ; BX = (DX:AX) >> 8
    mov bh, dl
    test dh, dh
    jz .scaled
    mov byte [tm_shift], 16
    mov bx, dx                  ; BX = (DX:AX) >> 16, which always fits
.scaled:
    mov [tm_totw], bx
    test bx, bx
    jz .zero                    ; under 256 clocks measured: nothing to divide

    mov si, 0                   ; per-task percentages
    mov di, tm_pct_tab
    mov cx, MAX_TASKS
.each:
    mov ax, [si+sch_snap_lo]
    mov dx, [si+sch_snap_hi]
    call tm_pct
    mov [di], al
    inc di
    add si, 2
    loop .each

    mov ax, [si+sch_snap_lo]    ; SI now points at the idle entry
    mov dx, [si+sch_snap_hi]
    call tm_pct
    mov bx, 100
    sub bx, ax                  ; busy = 100 - idle
    mov [tm_cpu], bl
    jmp short .hist

.zero:                          ; only reachable when the whole sample measured
    mov byte [tm_cpu], 0        ; under 256 clocks - i.e. nothing at all
    mov si, tm_pct_tab
    mov cx, MAX_TASKS
.zl:
    mov byte [si], 0
    inc si
    loop .zl

.hist:
    mov ax, [tm_hpos]           ; advance the ring, then store this sample
    call tm_next
    mov [tm_hpos], ax
    mov si, ax
    mov al, [tm_cpu]
    shr al, 1                   ; 0..100 -> 0..50, the interior height
    mov [si+tm_hist], al

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_pct - percentage of one accumulator against [tm_totw]
; in:  DX:AX = value in PIT clocks, [tm_totw] = total >> [tm_shift] (non-zero)
; out: AX = 0..100
; clobbers: DX, flags (internal helper)
; Both sides are shifted right by the same amount, so the arithmetic fits the
; 8086's 16-bit DIV without changing the ratio: value>>n <= total>>n, so the
; quotient is <= 100 and DIV cannot fault. tm_sample picks n = 8 normally and
; n = 16 when the interval was long enough to overflow a >> 8 total.
; -----------------------------------------------------------------------------
tm_pct:
    push cx

    cmp byte [tm_shift], 8
    jne .s16
    test dh, dh
    jnz .full                   ; cannot happen (value <= total) - be safe
    mov al, ah                  ; AX = (DX:AX) >> 8
    mov ah, dl
    jmp short .scaled
.s16:
    mov ax, dx                  ; AX = (DX:AX) >> 16
.scaled:
    mov cx, 100
    mul cx                      ; DX:AX = scaled value * 100
    mov cx, [tm_totw]
    jcxz .zero
    div cx
    cmp ax, 100
    jbe .out
.full:
    mov ax, 100
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_next - step a ring index: AX = (AX + 1) MOD TM_HIST_N
; in:  AX = index
; out: AX = next index
; clobbers: flags (internal helper)
; -----------------------------------------------------------------------------
tm_next:
    inc ax
    cmp ax, TM_HIST_N
    jb .out
    xor ax, ax
.out:
    ret

; =============================================================================
; painting
; =============================================================================

; -----------------------------------------------------------------------------
; tm_paint - W_PAINT for the Task Manager window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Draws the static furniture, then the whole history ring (the content is
; already white, so only the bars need drawing) minus the two sweep-gap
; columns, then the dynamic content via tm_draw_dyn. BP carries the window
; ptr across the string/frame drawing, which needs BX for coordinates.
; -----------------------------------------------------------------------------
tm_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bp, si
    mov bx, si
    call wm_content             ; AX = content left, DX = content top
    mov [tm_cx], ax
    mov [tm_cy], dx

    mov byte [gfx_color], CBLACK
    mov si, tm_s_cpu            ; --- labels ---
    mov cx, TM_LBL_X
    mov dx, TM_CPU_Y
    call tm_rstr
    mov si, tm_s_mem
    mov cx, TM_LBL_X
    mov dx, TM_MEM_Y
    call tm_rstr

    mov ax, TM_GF_X1            ; --- frames: graph, RAM bar, kernel bar ---
    mov bx, TM_GF_Y1
    mov cx, TM_RIGHT
    mov dx, TM_GF_Y2
    call tm_rframe
    mov ax, TM_LBL_X
    mov bx, TM_RF_Y1
    mov cx, TM_RIGHT
    mov dx, TM_RF_Y2
    call tm_rframe
    mov ax, TM_LBL_X
    mov bx, TM_KF_Y1
    mov cx, TM_RIGHT
    mov dx, TM_KF_Y2
    call tm_rframe

    mov ax, TM_LBL_X            ; --- separator + column headers ---
    mov bx, TM_RIGHT
    mov dx, TM_SEP_Y
    call tm_rhline
    mov si, tm_s_htask
    mov cx, TM_COL_NAME
    mov dx, TM_HDR_Y
    call tm_rstr
    mov si, tm_s_hstate
    mov cx, TM_COL_STATE
    mov dx, TM_HDR_Y
    call tm_rstr
    mov si, tm_s_cpu
    mov cx, TM_COL_PCT
    mov dx, TM_HDR_Y
    call tm_rstr

    mov ax, [tm_hpos]           ; the sweep gap stays white
    call tm_next
    mov [tm_gap1], ax
    call tm_next
    mov [tm_gap2], ax

    xor ax, ax                  ; every other column's bar (tm_col_bar keeps AX)
.hloop:
    cmp ax, [tm_gap1]
    je .hnext
    cmp ax, [tm_gap2]
    je .hnext
    call tm_col_bar
.hnext:
    inc ax
    cmp ax, TM_HIST_N
    jb .hloop

    mov bx, bp
    call tm_draw_dyn

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_draw_dyn - repaint everything that changes: three graph columns, the
;               CPU percent, both memory bars and their text, the task rows
; in:  BX = window ptr (gfx lock held by the caller)
; out: nothing
; clobbers: nothing (flags only)
; The graph is a sweep: only the newest column and the two-column white gap
; ahead of it are touched, never all 288.
; -----------------------------------------------------------------------------
tm_draw_dyn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call wm_content             ; BX = window ptr -> AX = left, DX = top
    mov [tm_cx], ax
    mov [tm_cy], dx

    mov ax, [tm_hpos]           ; --- newest column: erase, then its bar ---
    call tm_col_erase
    call tm_col_bar
    call tm_next                ; --- two-column white sweep gap ahead of it --
    call tm_col_erase
    call tm_next
    call tm_col_erase

    mov ax, TM_PCT_X            ; --- CPU percent (font draws transparent, so
    mov bx, TM_CPU_Y            ;     the old digits must be erased first) ---
    mov cx, TM_RIGHT
    mov dx, TM_PCT_Y2
    mov byte [gfx_color], CWHITE
    call tm_rfill
    mov di, tm_strbuf
    mov al, [tm_cpu]
    xor ah, ah
    call tm_u16
    mov byte [di], '%'
    mov byte [di+1], 0
    mov byte [gfx_color], CBLACK
    mov si, tm_strbuf
    mov cx, TM_PCT_X
    mov dx, TM_CPU_Y
    call tm_rstr

    call tm_draw_ram
    call tm_draw_kseg
    call tm_draw_rows

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_col_erase - white-fill one graph interior column
; in:  AX = column index 0..TM_HIST_N-1
; out: nothing
; clobbers: nothing (flags only; internal helper)
; -----------------------------------------------------------------------------
tm_col_erase:
    push ax
    push bx
    push dx

    add ax, TM_GX0
    mov bx, TM_GY0
    mov dx, TM_GY1
    mov byte [gfx_color], CWHITE
    call tm_rvline

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_col_bar - draw one graph column's black bar, bottom-anchored
; in:  AX = column index 0..TM_HIST_N-1 (height from tm_hist)
; out: nothing
; clobbers: nothing (flags only; internal helper)
; Height 0 draws nothing at all - a 1px stub would read as 2% CPU.
; -----------------------------------------------------------------------------
tm_col_bar:
    push ax
    push bx
    push dx
    push si

    mov si, ax
    mov dl, [si+tm_hist]
    xor dh, dh                  ; DX = height, 0..50
    test dx, dx
    jz .done
    add ax, TM_GX0              ; AX = column x
    mov bx, TM_GY1+1
    sub bx, dx                  ; BX = y1 = bottom - h + 1
    mov dx, TM_GY1              ; DX = y2 = bottom
    mov byte [gfx_color], CBLACK
    call tm_rvline
.done:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_draw_ram - the conventional-memory bar and its text line
; in:  nothing (gfx lock held; [tm_cx]/[tm_cy] set)
; out: nothing
; clobbers: nothing (flags only; internal helper)
; Reserved RAM is dithered gray, free RAM white. The reserved fraction is
; TM_RESERVED_KB * TM_BAR_W / tm_ram_kb - mul then div, so the product keeps
; its full 32-bit width in DX:AX.
; -----------------------------------------------------------------------------
tm_draw_ram:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, TM_BX0              ; white interior first
    mov bx, TM_RI_Y1
    mov cx, TM_BX1
    mov dx, TM_RI_Y2
    mov byte [gfx_color], CWHITE
    call tm_rfill

    mov ax, TM_RESERVED_KB
    mov cx, TM_BAR_W
    mul cx
    mov cx, [tm_ram_kb]         ; never 0: tm_init substitutes 640
    div cx
    cmp ax, TM_BAR_W
    jbe .clamped
    mov ax, TM_BAR_W
.clamped:
    test ax, ax
    jz .text
    mov cx, ax
    add cx, TM_BX0 - 1          ; CX = x2 = left + px - 1
    mov ax, TM_BX0
    mov bx, TM_RI_Y1
    mov dx, TM_RI_Y2
    call tm_rgray               ; ignores [gfx_color] by design

.text:
    mov ax, TM_LBL_X            ; erase the old line, then rebuild it
    mov bx, TM_RTXT_Y
    mov cx, TM_RIGHT
    mov dx, TM_RTXT_Y2
    mov byte [gfx_color], CWHITE
    call tm_rfill

    mov di, tm_strbuf
    mov si, tm_s_ram
    call tm_scat
    mov ax, [tm_ram_kb]
    call tm_u16
    mov si, tm_s_used
    call tm_scat
    mov ax, TM_RESERVED_KB
    call tm_u16
    mov si, tm_s_free
    call tm_scat
    mov ax, [tm_ram_kb]
    sub ax, TM_RESERVED_KB
    jnc .free
    xor ax, ax                  ; a machine smaller than what we reserve
.free:
    call tm_u16
    mov si, tm_s_kk
    call tm_scat
    mov byte [di], 0

    mov byte [gfx_color], CBLACK
    mov si, tm_strbuf
    mov cx, TM_LBL_X
    mov dx, TM_RTXT_Y
    call tm_rstr

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_draw_kseg - the kernel-segment bar (64K) and its text line
; in:  nothing (gfx lock held; [tm_cx]/[tm_cy] set)
; out: nothing
; clobbers: nothing (flags only; internal helper)
; Gray = kernel image + .bss (kernel_bss_end, the last label in kernel.asm),
; solid black = the resident package ([ld_size], 0 when none is loaded),
; white = the free tail of the segment.
; -----------------------------------------------------------------------------
tm_draw_kseg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, TM_BX0
    mov bx, TM_KI_Y1
    mov cx, TM_BX1
    mov dx, TM_KI_Y2
    mov byte [gfx_color], CWHITE
    call tm_rfill

    mov ax, kernel_bss_end      ; forward ref: resolves to image + bss bytes
    call tm_bpx
    mov [tm_kpx], ax
    mov ax, [ld_size]
    call tm_bpx
    mov [tm_ppx], ax

    mov ax, [tm_kpx]            ; clamp the pair to the bar width
    cmp ax, TM_BAR_W
    jbe .kok
    mov ax, TM_BAR_W
    mov [tm_kpx], ax
    mov word [tm_ppx], 0
.kok:
    add ax, [tm_ppx]
    cmp ax, TM_BAR_W
    jbe .pok
    mov ax, TM_BAR_W
    sub ax, [tm_kpx]
    mov [tm_ppx], ax
.pok:

    mov ax, [tm_kpx]
    test ax, ax
    jz .prog
    mov cx, ax
    add cx, TM_BX0 - 1
    mov ax, TM_BX0
    mov bx, TM_KI_Y1
    mov dx, TM_KI_Y2
    call tm_rgray

.prog:
    mov ax, [tm_ppx]
    test ax, ax
    jz .text
    mov cx, [tm_kpx]
    add cx, TM_BX0              ; program starts where the kernel ends
    mov bx, cx
    add cx, ax
    dec cx                      ; CX = x2
    mov ax, bx                  ; AX = x1
    mov bx, TM_KI_Y1
    mov dx, TM_KI_Y2
    mov byte [gfx_color], CBLACK
    call tm_rfill

.text:
    mov ax, TM_LBL_X
    mov bx, TM_KTXT_Y
    mov cx, TM_RIGHT
    mov dx, TM_KTXT_Y2
    mov byte [gfx_color], CWHITE
    call tm_rfill

    mov di, tm_strbuf
    mov si, tm_s_kern
    call tm_scat
    mov ax, kernel_bss_end
    call tm_kb
    mov [tm_kkb], ax
    call tm_u16
    mov si, tm_s_prog
    call tm_scat
    mov ax, [ld_size]
    call tm_kb
    mov [tm_pkb], ax
    call tm_u16
    mov si, tm_s_free
    call tm_scat
    mov ax, 64                  ; the segment is 64K; free = 64 - k - p
    sub ax, [tm_kkb]
    jc .zerofree
    sub ax, [tm_pkb]
    jnc .free
.zerofree:
    xor ax, ax
.free:
    call tm_u16
    mov si, tm_s_kk
    call tm_scat
    mov byte [di], 0

    mov byte [gfx_color], CBLACK
    mov si, tm_strbuf
    mov cx, TM_LBL_X
    mov dx, TM_KTXT_Y
    call tm_rstr

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_bpx - kernel-segment bar pixels for a byte count
; in:  AX = bytes (0..65535)
; out: AX = pixels, 0..TM_BAR_W
; clobbers: flags (internal helper)
; px = bytes * TM_BAR_W / 65536, evaluated as (bytes >> 8) * TM_BAR_W / 256 so
; the product stays inside DX:AX and the quotient inside AX.
;
; The shift-first scaling costs a byte of precision, which annihilates any
; package under 256 bytes (HELLO.JOP is 151): the bar would show no black at
; all while the text line beside it reads "program 1K", because tm_kb rounds
; up. A non-zero byte count therefore floors at one pixel, so the bar and the
; text always agree on whether something is resident.
; -----------------------------------------------------------------------------
tm_bpx:
    push bx
    push cx
    push dx

    mov bx, ax                  ; keep the unscaled count for the floor test
    mov cl, 8
    shr ax, cl
    mov cx, TM_BAR_W
    mul cx
    mov cx, 256
    div cx                      ; quotient <= 287, so DIV cannot fault
    test ax, ax
    jnz .out
    test bx, bx
    jz .out                     ; genuinely nothing: draw nothing
    mov ax, 1
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_kb - byte count to whole KB, rounded UP
; in:  AX = bytes (well under 64512, so the round-up cannot wrap)
; out: AX = KB
; clobbers: flags (internal helper)
; -----------------------------------------------------------------------------
tm_kb:
    push cx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_draw_rows - erase the task-list block and redraw one row per used slot
; in:  nothing (gfx lock held; [tm_cx]/[tm_cy] set)
; out: nothing
; clobbers: nothing (flags only; internal helper)
; Free slots (T_STATE = 0) are skipped and the used ones packed upward, so a
; hole in sch_tasks never leaves a hole in the list.
; -----------------------------------------------------------------------------
tm_draw_rows:
    push ax
    push bx
    push cx
    push dx

    mov ax, TM_LBL_X
    mov bx, TM_ROW_Y0
    mov cx, TM_RIGHT
    mov dx, TM_ROWS_Y2
    mov byte [gfx_color], CWHITE
    call tm_rfill

    mov word [tm_slot], 0
    mov word [tm_row], 0
.next:
    mov bx, [tm_slot]
    mov cl, 3
    shl bx, cl                  ; T_SIZE = 8
    add bx, sch_tasks           ; BX = task record
    cmp byte [bx+T_STATE], 0
    je .skip
    call tm_row_draw
    inc word [tm_row]
.skip:
    inc word [tm_slot]
    cmp word [tm_slot], MAX_TASKS
    jb .next

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_row_draw - one task row: name, state, percent, CPU bar
; in:  BX = task record ptr, [tm_slot] = its index, [tm_row] = display row
; out: nothing
; clobbers: nothing (flags only; internal helper)
; A record with T_NAME = 0 gets a "(task N)" label - the null is never
; dereferenced.
; -----------------------------------------------------------------------------
tm_row_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, [tm_row]
    mov cx, TM_ROW_H
    mul cx
    add ax, TM_ROW_Y0
    mov [tm_rowy], ax

    mov si, [tm_slot]
    mov al, [si+tm_pct_tab]
    xor ah, ah
    mov [tm_rowpct], ax

    mov byte [gfx_color], CBLACK

    mov si, [bx+T_NAME]         ; --- name ---
    test si, si
    jnz .name
    mov di, tm_strbuf
    mov si, tm_s_tfb
    call tm_scat
    mov ax, [tm_slot]
    call tm_u16
    mov byte [di], ')'
    mov byte [di+1], 0
    mov si, tm_strbuf
.name:
    mov cx, TM_COL_NAME
    mov dx, [tm_rowy]
    call tm_rstr

    mov al, [sch_cur]           ; --- state ---
    xor ah, ah
    cmp ax, [tm_slot]
    je .run
    mov si, tm_s_ready
    cmp byte [bx+T_STATE], 2
    jne .state
    mov si, tm_s_sleep
    jmp short .state
.run:
    mov si, tm_s_run
.state:
    mov cx, TM_COL_STATE
    mov dx, [tm_rowy]
    call tm_rstr

    mov di, tm_strbuf           ; --- percent ---
    mov ax, [tm_rowpct]
    call tm_u16
    mov byte [di], '%'
    mov byte [di+1], 0
    mov si, tm_strbuf
    mov cx, TM_COL_PCT
    mov dx, [tm_rowy]
    call tm_rstr

    mov ax, TM_RB_X1            ; --- share bar (BX is free from here on) ---
    mov bx, [tm_rowy]
    mov cx, TM_RB_X2
    mov dx, bx
    add dx, 8
    call tm_rframe

    mov ax, [tm_rowpct]
    mov cx, TM_RB_W
    mul cx
    mov cx, 100
    div cx                      ; AX = filled pixels, 0..82
    test ax, ax
    jz .done
    mov cx, ax
    add cx, TM_RB_IX - 1        ; CX = x2
    mov ax, TM_RB_IX
    mov bx, [tm_rowy]
    inc bx                      ; y1 = top + 1
    mov dx, [tm_rowy]
    add dx, 7                   ; y2 = top + 7
    mov byte [gfx_color], CBLACK
    call tm_rfill

.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; content-relative wrappers around the gfx/font primitives
;
; Every coordinate in this module is relative to the window's content origin
; (inclusive on both ends); these five helpers add [tm_cx]/[tm_cy] and call
; the primitive. All of them preserve every register, so a caller can keep
; its own coordinates in AX..DX across a call.
; =============================================================================

; -----------------------------------------------------------------------------
; tm_rfill - gfx_fill in content coordinates
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (color from [gfx_color])
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rfill:
    push ax
    push bx
    push cx
    push dx
    add ax, [tm_cx]
    add bx, [tm_cy]
    add cx, [tm_cx]
    add dx, [tm_cy]
    call gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rgray - gfx_fill_gray in content coordinates (ignores [gfx_color])
; in:  AX = x1, BX = y1, CX = x2, DX = y2
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rgray:
    push ax
    push bx
    push cx
    push dx
    add ax, [tm_cx]
    add bx, [tm_cy]
    add cx, [tm_cx]
    add dx, [tm_cy]
    call gfx_fill_gray
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rframe - gfx_frame in content coordinates
; in:  AX = x1, BX = y1, CX = x2, DX = y2
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rframe:
    push ax
    push bx
    push cx
    push dx
    add ax, [tm_cx]
    add bx, [tm_cy]
    add cx, [tm_cx]
    add dx, [tm_cy]
    call gfx_frame
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rhline / tm_rvline - gfx_hline / gfx_vline in content coordinates
; in:  hline: AX = x1, BX = x2, DX = y
;      vline: AX = x,  BX = y1, DX = y2
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rhline:
    push ax
    push bx
    push dx
    add ax, [tm_cx]
    add bx, [tm_cx]
    add dx, [tm_cy]
    call gfx_hline
    pop dx
    pop bx
    pop ax
    ret

tm_rvline:
    push ax
    push bx
    push dx
    add ax, [tm_cx]
    add bx, [tm_cy]
    add dx, [tm_cy]
    call gfx_vline
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rstr - font_str in content coordinates
; in:  SI = NUL string, CX = x, DX = y (color from [gfx_color])
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rstr:
    push cx
    push dx
    add cx, [tm_cx]
    add dx, [tm_cy]
    call font_str
    pop dx
    pop cx
    ret

; =============================================================================
; string helpers
; =============================================================================

; -----------------------------------------------------------------------------
; tm_scat - append a NUL string to a buffer, without a terminator
; in:  SI = NUL source, DI = destination
; out: DI past the last byte written, SI past the source NUL
; clobbers: AL (internal helper)
; -----------------------------------------------------------------------------
tm_scat:
    lodsb                       ; DF = 0 per SPEC.md 1
    test al, al
    jz .done
    mov [di], al
    inc di
    jmp tm_scat
.done:
    ret

; -----------------------------------------------------------------------------
; tm_u16 - unsigned word to decimal, no leading zeros and no terminator
; in:  AX = value, DI = destination (needs up to 5 bytes)
; out: DI past the last digit
; clobbers: nothing else (flags only; internal helper)
; Digits come out least-significant first, so they are pushed and popped back
; in print order; 0 prints as "0" because the divide loop always runs once.
; -----------------------------------------------------------------------------
tm_u16:
    push ax
    push bx
    push cx
    push dx

    mov bx, 10
    xor cx, cx                  ; CX = digit count
.div:
    xor dx, dx
    div bx
    add dl, '0'
    push dx
    inc cx
    test ax, ax
    jnz .div
.emit:
    pop ax
    mov [di], al
    inc di
    loop .emit

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; task manager state
; =============================================================================
section .bss

tm_win:     resw 1              ; window record ptr (ui.inc dispatches on this)
tm_ram_kb:  resw 1              ; int 12h conventional memory, KB
tm_hpos:    resw 1              ; newest history ring slot
tm_cpu:     resb 1              ; overall CPU usage, 0..100
tm_pct_tab: resb MAX_TASKS      ; per-task CPU share, 0..100
tm_hist:    resb 288            ; CPU history ring, 0..50 (TM_HIST_N; literal
                                ; per the no-forward-ref-in-bss rule)
tm_strbuf:  resb 48             ; text assembly scratch

tm_totw:    resw 1              ; sample scratch: total clocks >> [tm_shift]
tm_shift:   resb 1              ; sample scratch: that shift, 8 or 16
tm_spad:    resb 1              ; keeps the words below word-aligned
tm_cx:      resw 1              ; draw scratch: content left
tm_cy:      resw 1              ; draw scratch: content top
tm_gap1:    resw 1              ; paint scratch: the two sweep-gap columns
tm_gap2:    resw 1
tm_kpx:     resw 1              ; draw scratch: kernel bar pixels
tm_ppx:     resw 1              ; draw scratch: resident package bar pixels
tm_kkb:     resw 1              ; draw scratch: kernel KB, rounded up
tm_pkb:     resw 1              ; draw scratch: package KB, rounded up
tm_slot:    resw 1              ; row walk: task table index
tm_row:     resw 1              ; row walk: display row (used slots packed)
tm_rowy:    resw 1              ; row draw: row top, content-relative
tm_rowpct:  resw 1              ; row draw: that task's CPU share

section .text
