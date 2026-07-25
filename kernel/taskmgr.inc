; =============================================================================
; jop - taskmgr.inc
;
; The Task Manager window (SPEC.md 28): a live CPU load gauge with an
; oscilloscope-sweep history graph, a RAM readout, and the task table with
; per-task CPU shares from the scheduler's PIT cycle accounting (SPEC.md 8.1).
;
; tm_task is the monitor task and doubles as the system idle soak: it never
; sleeps, spinning { count += 1; task_yield } for 9-tick intervals. The spin
; count measures how quickly a yield loop gets the CPU back, calibrated
; against a rolling maximum - this self-calibrates away the UI task's
; constant busy-poll cost, so the gauge reads ~0% at idle and rises when any
; task (including the UI) holds the CPU.
;
; Locking follows the Clock pattern (SPEC.md 14): tm_paint is a bare,
; unconditional full redraw (wm_paint_all calls it with the gfx lock already
; held); the task's periodic path takes the lock itself, re-checks visible +
; not wm_obscured under it, and redraws only what changed (one graph column,
; the text lines, the bar, the rows) so the lock hold stays Bounce-scale.
; tm_sample runs without the lock and only touches module state; a sample
; landing mid-tm_paint can at worst misplace the sweep gap by one column for
; one frame.
; =============================================================================

; --- geometry / cadence (SPEC.md 28) -----------------------------------------
TM_INT      equ 9               ; sample interval, ticks (~0.5s)
TM_EPOCH    equ 20              ; samples per calibration epoch (~10s)
TM_GW       equ 160             ; graph interior width = history ring size
TM_GH       equ 40              ; graph interior height, px
TM_CPU_Y    equ 4               ; "CPU nnn%" line, content-relative y
TM_GF_Y1    equ 14              ; graph frame top (interior rows 15..54)
TM_GF_Y2    equ 55              ; graph frame bottom
TM_RAM_Y    equ 61              ; "RAM uuuK/tttK" line
TM_BAR_Y1   equ 71              ; RAM bar frame top (interior rows 72..79)
TM_BAR_Y2   equ 80              ; RAM bar frame bottom
TM_HDR_Y    equ 87              ; task-list header line
TM_ROW_Y    equ 97              ; first task row
TM_ROW_H    equ 11              ; row pitch, px

; -----------------------------------------------------------------------------
; tm_init - create the Task Manager window and spawn the monitor task
;
; Runs on task 0 from kmain (after loader_init), so the int 12h call honors
; the only-the-UI-task-calls-BIOS rule. Zeroes all module state first (.bss
; is not zeroed at boot, and tm_task starts running the moment it is
; spawned), then reads total conventional RAM, creates the window (hidden),
; and spawns tm_task into the last free slot - the task table is then full:
; UI, Clock, Bounce, TaskMgr.
;
; in:       nothing
; out:      nothing; window ptr in [tm_win]
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_init:
    push ax
    push bx
    push cx
    push di
    push si
    push es

    push ds                     ; zero the state block (DF=0 per boot path)
    pop es
    mov di, tm_zero_beg
    mov cx, tm_zero_end - tm_zero_beg
    xor al, al
    rep stosb

    int 0x12                    ; AX = conventional memory, KB
    mov [tm_totkb], ax

    mov si, tm_tpl
    call wm_create              ; created hidden; shown by CMD_TASKS
    mov [tm_win], bx

    mov ax, tm_task
    call task_spawn

    pop es
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
tm_tpl:     dw 250, 100, 176, 162, tm_ttl, tm_paint, 0, 0
tm_ttl:     db 'Task Manager', 0

tm_s_cpu:   db 'CPU ', 0
tm_s_ram:   db 'RAM ', 0
tm_s_hdr:   db '#  TASK    ST   CPU', 0
tm_s_ui:    db 'UI', 0
tm_s_clk:   db 'Clock', 0
tm_s_bnc:   db 'Bounce', 0
tm_s_tm:    db 'TaskMgr', 0
tm_s_dash:  db '-', 0
tm_s_run:   db 'run', 0
tm_s_rdy:   db 'rdy', 0
tm_s_slp:   db 'slp', 0
tm_s_free:  db '---', 0

; slot -> name; kmain's spawn order is deterministic and pinned in SPEC.md 28
tm_names:   dw tm_s_ui, tm_s_clk, tm_s_bnc, tm_s_tm

; -----------------------------------------------------------------------------
; tm_task - the monitor task: spin-measure for TM_INT ticks, sample, redraw
; in:  nothing (task entry; never returns)
; -----------------------------------------------------------------------------
tm_task:
.interval:
    mov ax, [ticks]
    mov [tm_t0], ax
    mov word [tm_cnt], 0
    mov word [tm_cnt+2], 0
.spin:
    add word [tm_cnt], 1        ; 32-bit spin counter
    adc word [tm_cnt+2], 0
    call task_yield
    mov ax, [ticks]
    sub ax, [tm_t0]             ; wrap-safe interval check
    cmp ax, TM_INT
    jb .spin

    call tm_sample              ; no lock: computes load, shares, RAM

    call gfx_lock
    mov bx, [tm_win]
    test word [bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14)
    jz .skip
    call wm_obscured
    jc .skip
    call tm_update              ; incremental redraw, lock held
.skip:
    call gfx_unlock
    jmp .interval

; -----------------------------------------------------------------------------
; tm_sample - one measurement pass: load gauge, history push, per-task CPU
;             shares, RAM usage. Touches only module state; no lock needed.
; in:  [tm_cnt] = the interval's spin count
; out: tm_load/tm_hist/tm_pos/tm_lastcol/tm_pct/tm_state/tm_self/tm_usedk/
;      tm_barw updated
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_sample:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; --- phased-window max calibration (SPEC.md 28) --------------------------
    ; Epoch rotation first: every TM_EPOCH samples, the current epoch's max
    ; becomes the previous one and the current restarts from this count. A
    ; poisoned-high calibration (menu_track's cheap poll loop) ages out in
    ; at most two epochs; an all-time max would stick forever.
    inc byte [tm_epc]
    cmp byte [tm_epc], TM_EPOCH
    jb .noroll
    mov byte [tm_epc], 0
    mov ax, [tm_cmax]
    mov [tm_pmax], ax
    mov ax, [tm_cmax+2]
    mov [tm_pmax+2], ax
    mov word [tm_cmax], 0
    mov word [tm_cmax+2], 0
.noroll:
    mov ax, [tm_cnt+2]          ; tm_cmax = max(tm_cmax, count)
    cmp ax, [tm_cmax+2]
    ja .newmax
    jb .maxed
    mov ax, [tm_cnt]
    cmp ax, [tm_cmax]
    jbe .maxed
.newmax:
    mov ax, [tm_cnt]
    mov [tm_cmax], ax
    mov ax, [tm_cnt+2]
    mov [tm_cmax+2], ax
.maxed:
    ; effective max = max(tm_cmax, tm_pmax) - always >= count, since cmax
    ; was just raised to at least count (no clamp needed before the DIV)
    mov cx, [tm_cmax]
    mov dx, [tm_cmax+2]
    cmp dx, [tm_pmax+2]
    ja .load
    jb .usepmax
    cmp cx, [tm_pmax]
    jae .load
.usepmax:
    mov cx, [tm_pmax]
    mov dx, [tm_pmax+2]

    ; --- load% = 100 - 100*count/max_eff -------------------------------------
.load:
    mov ax, [tm_cnt]            ; normalize both to 16 bits (count <= max,
    mov bx, [tm_cnt+2]          ; so count's high word clears first)
.norm:
    test dx, dx
    jz .normed
    shr dx, 1
    rcr cx, 1
    shr bx, 1
    rcr ax, 1
    jmp .norm
.normed:
    jcxz .load0                 ; max is 0: nothing measurable yet
    mov bx, cx                  ; BX = max16
    mov cx, 100
    mul cx                      ; DX:AX = count16 * 100
    div bx                      ; AX = 100*count/max <= 100
    mov cx, 100
    sub cx, ax                  ; CX = load
    jmp .loadok
.load0:
    xor cx, cx
.loadok:
    mov [tm_load], cx

    ; --- history push: v = load*40/100, sweep index advances -----------------
    mov ax, cx
    mov cx, TM_GH
    mul cx
    mov cx, 100
    div cx                      ; AX = 0..40
    mov bx, [tm_pos]
    mov [tm_hist+bx], al
    mov [tm_lastcol], bx
    inc bx
    cmp bx, TM_GW
    jb .posok
    xor bx, bx
.posok:
    mov [tm_pos], bx

    ; --- snapshot scheduler state (one atomic block, SPEC.md 8.1) ------------
    pushf
    cli
    mov si, sch_cycles
    mov di, tm_new
    mov cx, MAX_TASKS * 2
.csnap:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .csnap
    mov si, sch_tasks + T_STATE
    mov di, tm_state
    mov cx, MAX_TASKS
.ssnap:
    mov al, [si]
    mov [di], al
    add si, T_SIZE
    inc di
    loop .ssnap
    mov al, [sch_cur]           ; = our own slot (we are running)
    mov [tm_self], al
    popf

    ; --- per-task diffs + total ----------------------------------------------
    xor ax, ax
    mov [tm_total], ax
    mov [tm_total+2], ax
    xor bx, bx
.diff:
    mov ax, [tm_new+bx]
    mov dx, [tm_new+bx+2]
    sub ax, [tm_old+bx]         ; wrap-safe 32-bit difference
    sbb dx, [tm_old+bx+2]
    mov [tm_diff+bx], ax
    mov [tm_diff+bx+2], dx
    mov cx, [tm_new+bx]
    mov [tm_old+bx], cx
    mov cx, [tm_new+bx+2]
    mov [tm_old+bx+2], cx
    add [tm_total], ax
    adc [tm_total+2], dx
    add bx, 4
    cmp bx, MAX_TASKS * 4
    jb .diff

    ; normalize: shift total and every diff right until total fits 16 bits
.tnorm:
    cmp word [tm_total+2], 0
    je .tnormed
    shr word [tm_total+2], 1
    rcr word [tm_total], 1
    xor bx, bx
.tshr:
    shr word [tm_diff+bx+2], 1
    rcr word [tm_diff+bx], 1
    add bx, 4
    cmp bx, MAX_TASKS * 4
    jb .tshr
    jmp .tnorm
.tnormed:

    ; share_i = diff_i*100/total; total 0 -> all 0; clamp keeps DIV safe
    xor bx, bx
    mov di, tm_pct
.pct:
    mov cx, [tm_total]
    jcxz .pzero
    mov ax, [tm_diff+bx+2]
    or ax, ax
    jnz .pfull                  ; pathological diff > total: clamp
    mov ax, [tm_diff+bx]
    cmp ax, cx
    ja .pfull
    mov si, 100
    mul si                      ; DX:AX = diff*100
    div cx                      ; AX <= 100: no overflow possible
    jmp .pstore
.pfull:
    mov ax, 100
    jmp .pstore
.pzero:
    xor ax, ax
.pstore:
    mov [di], al
    inc di
    add bx, 4
    cmp bx, MAX_TASKS * 4
    jb .pct

    ; --- RAM: kernel footprint + resident package (SPEC.md 28) ---------------
    pushf                       ; gate + header words in one atomic block:
    cli                         ; loader_run zeroes ld_appwin before it
    mov ax, [ld_appwin]         ; overwrites the region, so gate != 0 here
    mov bx, [APP_LOAD_OFF+8]    ; means the header is intact
    mov cx, [APP_LOAD_OFF+10]
    popf
    or ax, ax
    jnz .app
    xor bx, bx
    xor cx, cx
.app:
    mov ax, kernel_bss_end      ; kernel text+bss footprint (bare label);
    add ax, bx                  ; max total 0xA000 + 0x5000 = 61440, no
    add ax, cx                  ; 16-bit overflow possible
    add ax, 1023
    mov cl, 10
    shr ax, cl                  ; usedK, rounded up
    mov [tm_usedk], ax
    mov cx, [tm_totkb]          ; barw = usedK*160/totalK (all KB)
    jcxz .nobar
    mov si, TM_GW
    mul si                      ; DX:AX = usedK*160 (<= 10240)
    div cx
    cmp ax, TM_GW
    jbe .barok
    mov ax, TM_GW
.barok:
    mov [tm_barw], ax
    jmp .ramdone
.nobar:
    mov word [tm_barw], 0
.ramdone:

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing. All routines below run with the gfx lock held by the caller and
; draw self-backgrounding elements (each white-fills its own rect or paints
; both segments), so no separate content clear is needed. [tm_cx]/[tm_cy]
; hold the content origin, cached by tm_paint / tm_update.
; =============================================================================

; -----------------------------------------------------------------------------
; tm_paint - W_PAINT: bare, unconditional full redraw
; in:  SI = window ptr (gfx lock held by caller; no lock, no visibility
;      check in here - SPEC.md 28)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    call wm_content             ; AX = content left, DX = content top
    mov [tm_cx], ax
    mov [tm_cy], dx

    call tm_txt_cpu

    mov byte [gfx_color], CBLACK
    mov ax, [tm_cx]             ; graph frame (6,14)-(167,55)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_GF_Y1
    mov cx, [tm_cx]
    add cx, 167
    mov dx, [tm_cy]
    add dx, TM_GF_Y2
    call gfx_frame

    xor bx, bx                  ; all 160 columns; tm_pos is the sweep gap
.col:
    cmp bx, [tm_pos]
    je .gap
    call tm_col
    jmp .next
.gap:
    call tm_gapcol
.next:
    inc bx
    cmp bx, TM_GW
    jb .col

    call tm_txt_ram

    mov byte [gfx_color], CBLACK
    mov ax, [tm_cx]             ; bar frame (6,71)-(167,80)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_BAR_Y1
    mov cx, [tm_cx]
    add cx, 167
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2
    call gfx_frame
    call tm_bar

    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]             ; task-list header
    add cx, 6
    mov dx, [tm_cy]
    add dx, TM_HDR_Y
    mov si, tm_s_hdr
    call font_str
    call tm_rows

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_update - periodic incremental redraw (gfx lock held, window visible)
;
; Frames and the header are static and already on screen; only the dynamic
; elements are touched: CPU line, the freshly written column + the advanced
; sweep gap, RAM line + bar, and the four task rows.
;
; in:  nothing ([tm_win] set)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_update:
    push ax
    push bx
    push dx

    mov bx, [tm_win]
    call wm_content
    mov [tm_cx], ax
    mov [tm_cy], dx

    call tm_txt_cpu
    mov bx, [tm_lastcol]
    call tm_col
    mov bx, [tm_pos]
    call tm_gapcol
    call tm_txt_ram
    call tm_bar
    call tm_rows

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_col - draw one graph column from the history ring
; in:  BX = column index 0..159; value 0..40 from tm_hist
; out: nothing
; clobbers: nothing (flags only)
; White vline above the value, black vline below - self-backgrounding, so
; a column can be redrawn in place without a clear.
; -----------------------------------------------------------------------------
tm_col:
    push ax
    push bx
    push cx
    push dx
    push si

    mov al, [tm_hist+bx]
    cbw                         ; v is 0..40
    mov si, ax                  ; SI = v
    mov ax, [tm_cx]
    add ax, 7                   ; interior starts at content x=7
    add ax, bx                  ; AX = column x
    mov cx, [tm_cy]             ; CX = content top (scratch)

    cmp si, TM_GH               ; white: rows 15..54-v (skip when v=40)
    jae .black
    mov byte [gfx_color], CWHITE
    mov bx, cx
    add bx, TM_GF_Y1 + 1
    mov dx, cx
    add dx, TM_GF_Y2 - 1
    sub dx, si
    call gfx_vline
.black:
    or si, si                   ; black: rows 55-v..54 (skip when v=0)
    jz .done
    mov byte [gfx_color], CBLACK
    mov bx, cx
    add bx, TM_GF_Y2
    sub bx, si
    mov dx, cx
    add dx, TM_GF_Y2 - 1
    call gfx_vline
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_gapcol - draw one all-white column (the sweep gap at tm_pos)
; in:  BX = column index 0..159
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_gapcol:
    push ax
    push bx
    push dx

    mov ax, [tm_cx]
    add ax, 7
    add ax, bx
    mov bx, [tm_cy]
    mov dx, bx
    add bx, TM_GF_Y1 + 1
    add dx, TM_GF_Y2 - 1
    mov byte [gfx_color], CWHITE
    call gfx_vline

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_txt_cpu - white-fill the CPU line's rect and draw "CPU nnn%"
; in:  [tm_load]
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_txt_cpu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [gfx_color], CWHITE
    mov ax, [tm_cx]
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_CPU_Y
    mov cx, [tm_cx]
    add cx, 90
    mov dx, [tm_cy]
    add dx, TM_CPU_Y + 7
    call gfx_fill

    mov di, tm_str
    mov si, tm_s_cpu
    call tm_copy
    mov ax, [tm_load]
    call tm_put3
    mov byte [di], '%'
    inc di
    mov byte [di], 0

    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 6
    mov dx, [tm_cy]
    add dx, TM_CPU_Y
    mov si, tm_str
    call font_str

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_txt_ram - white-fill the RAM line's rect and draw "RAM uuuK/tttK"
; in:  [tm_usedk], [tm_totkb]
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_txt_ram:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [gfx_color], CWHITE
    mov ax, [tm_cx]
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_RAM_Y
    mov cx, [tm_cx]
    add cx, 167
    mov dx, [tm_cy]
    add dx, TM_RAM_Y + 7
    call gfx_fill

    mov di, tm_str
    mov si, tm_s_ram
    call tm_copy
    mov ax, [tm_usedk]
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], '/'
    inc di
    mov ax, [tm_totkb]
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], 0

    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 6
    mov dx, [tm_cy]
    add dx, TM_RAM_Y
    mov si, tm_str
    call font_str

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_bar - the RAM bar interior: black for barw px from the left, white rest
; in:  [tm_barw] = 0..160
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_bar:
    push ax
    push bx
    push cx
    push dx
    push si

    mov si, [tm_barw]
    or si, si
    jz .rest
    mov byte [gfx_color], CBLACK
    mov ax, [tm_cx]
    add ax, 7
    mov bx, [tm_cy]
    add bx, TM_BAR_Y1 + 1
    mov cx, ax
    add cx, si
    dec cx                      ; x2 = 7 + barw - 1
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2 - 1
    call gfx_fill
.rest:
    cmp si, TM_GW
    jae .done
    mov byte [gfx_color], CWHITE
    mov ax, [tm_cx]
    add ax, 7
    add ax, si                  ; x1 = 7 + barw
    mov bx, [tm_cy]
    add bx, TM_BAR_Y1 + 1
    mov cx, [tm_cx]
    add cx, 166
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2 - 1
    call gfx_fill
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_rows - the four task rows: white-fill each band, build the 20-char row
;           string, draw it (SPEC.md 28 pins the format)
; in:  tm_state/tm_self/tm_pct
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    xor si, si                  ; SI = slot
.row:
    mov ax, si                  ; y1 = cy + TM_ROW_Y + 11*slot
    mov cx, TM_ROW_H
    mul cx
    add ax, TM_ROW_Y
    add ax, [tm_cy]
    mov [tm_rowy], ax

    mov byte [gfx_color], CWHITE
    mov bx, ax                  ; white-fill the band (6,y)-(167,y+7)
    mov dx, ax
    add dx, 7
    mov ax, [tm_cx]
    add ax, 6
    mov cx, [tm_cx]
    add cx, 167
    call gfx_fill

    ; --- build "N  NAME    ST   nn%" in tm_str -------------------------------
    mov di, tm_str
    mov ax, si
    add al, '0'
    mov [di], al
    inc di
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di

    mov bx, si
    cmp byte [tm_state+bx], 0
    je .free

    shl bx, 1                   ; name, left-justified in 7
    push si
    mov si, [tm_names+bx]
    call tm_copy7
    pop si
    mov byte [di], ' '
    inc di

    push si                     ; state: own slot = run, else rdy/slp
    mov ax, si
    cmp al, [tm_self]
    jne .notrun
    mov si, tm_s_run
    jmp .state
.notrun:
    mov bx, si
    cmp byte [tm_state+bx], 2
    je .slp
    mov si, tm_s_rdy
    jmp .state
.slp:
    mov si, tm_s_slp
.state:
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di

    mov bx, si                  ; share, right-aligned in 3, then '%'
    mov al, [tm_pct+bx]
    xor ah, ah
    call tm_put3
    mov byte [di], '%'
    inc di
    mov byte [di], 0
    jmp .draw

.free:                          ; free slot: "-       ---   --"
    push si
    mov si, tm_s_dash
    call tm_copy7
    mov byte [di], ' '
    inc di
    mov si, tm_s_free
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di
    mov byte [di], '-'
    inc di
    mov byte [di], '-'
    inc di
    mov byte [di], 0

.draw:
    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 6
    mov dx, [tm_rowy]
    push si
    mov si, tm_str
    call font_str
    pop si

    inc si
    cmp si, MAX_TASKS
    jb .row

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_copy - copy a NUL string (without the NUL) to DI
; in:  SI = string, DI = dest
; out: DI advanced past the copied chars
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_copy:
    push ax
    push si
.c:
    lodsb                       ; DF=0 per SPEC.md 1
    or al, al
    jz .done
    mov [di], al
    inc di
    jmp .c
.done:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_copy7 - copy a NUL string into exactly 7 chars at DI, space-padded,
;            truncated at 7
; in:  SI = string, DI = dest
; out: DI advanced by 7
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_copy7:
    push ax
    push cx
    push si
    mov cx, 7
.c:
    lodsb
    or al, al
    jz .pad
    mov [di], al
    inc di
    loop .c
    jmp .done
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.done:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_put3 - decimal 0..999 as 3 chars at DI, right-aligned, space-padded
; in:  AX = value, DI = dest
; out: DI advanced by 3
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_put3:
    push ax
    push bx
    push cx
    push dx

    mov bx, 100
    xor dx, dx
    div bx                      ; AX = hundreds, DX = 0..99
    mov cx, dx
    mov bl, ' '                 ; BL = ' ' until a digit is emitted
    or ax, ax
    jz .hpad
    add al, '0'
    mov [di], al
    mov bl, '0'
    jmp .hdone
.hpad:
    mov byte [di], ' '
.hdone:
    inc di

    mov ax, cx
    mov cx, 10
    xor dx, dx
    div cx                      ; AX = tens, DX = ones
    or ax, ax
    jnz .tdig
    cmp bl, '0'
    je .tdig                    ; hundreds printed: this zero counts
    mov byte [di], ' '
    jmp .tdone
.tdig:
    add al, '0'
    mov [di], al
.tdone:
    inc di

    mov ax, dx
    add al, '0'
    mov [di], al
    inc di

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

tm_win:     resw 1              ; window record ptr (ui.inc dispatches on it)

tm_zero_beg:                    ; tm_init zeroes this whole block
tm_hist:    resb TM_GW          ; history ring: column heights 0..40
tm_pos:     resw 1              ; sweep index = next column to write
tm_lastcol: resw 1              ; column written by the latest sample
tm_cnt:     resw 2              ; interval spin count, dword
tm_cmax:    resw 2              ; calibration max, current epoch, dword
tm_pmax:    resw 2              ; calibration max, previous epoch, dword
tm_epc:     resb 1              ; samples into the current epoch
tm_t0:      resw 1              ; interval start tick
tm_load:    resw 1              ; latest load, 0..100
tm_old:     resw MAX_TASKS * 2  ; previous sch_cycles snapshot
tm_new:     resw MAX_TASKS * 2  ; current snapshot
tm_diff:    resw MAX_TASKS * 2  ; per-task interval cycles
tm_total:   resw 2              ; sum of diffs, dword
tm_state:   resb MAX_TASKS      ; T_STATE snapshot
tm_self:    resb 1              ; our own slot ([sch_cur] at snapshot)
tm_pct:     resb MAX_TASKS      ; per-task share, 0..100
tm_usedk:   resw 1              ; used RAM, KB
tm_barw:    resw 1              ; RAM bar black width, 0..160
tm_zero_end:

tm_totkb:   resw 1              ; total conventional RAM (int 12h, boot)
tm_cx:      resw 1              ; cached content origin (draw paths only,
tm_cy:      resw 1              ; always under the gfx lock)
tm_rowy:    resw 1              ; tm_rows scratch: current row y
tm_str:     resb 24             ; line-format scratch

section .text
