; =============================================================================
; jop - taskmgr.inc
;
; The Task Manager window (SPEC.md 28): a live CPU load gauge with an
; oscilloscope-sweep history graph, a RAM readout, and the process list with
; per-instance CPU shares and memory from the PIT cycle accounting
; (SPEC.md 8.1).
;
; The list is an INSTANCE list, not a task list (SPEC.md 29). A task list
; would show only the four kinds that own a task; every task-less app -
; About, Note Pad, Disk, and every loaded package, which cannot spawn at all
; (SPEC.md 20.2/21) - runs purely inside window callbacks on the UI task and
; would be invisible. Each instance's row therefore adds two counters: the
; I_CYC that wm.inc/ui.inc bill to it around every W_PAINT / W_ONKEY /
; W_ONCLICK, and, if it owns a task, that task's sch_cycles. Row 0 is
; "System" - task 0's remainder, i.e. the kernel's own work once every
; callback has been debited away. The two counters are disjoint by
; construction (task_debit moves cycles rather than copying them), so the
; rows partition one total.
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
TM_CPU_Y    equ 4               ; "CPU nnn% SCH xxxxxxx" line, content-rel y
TM_GF_Y1    equ 14              ; graph frame top (interior rows 15..54)
TM_GF_Y2    equ 55              ; graph frame bottom
TM_RAM_Y    equ 61              ; "RAM uuuK/tttK" line
TM_BAR_Y1   equ 71              ; RAM bar frame top (interior rows 72..79)
TM_BAR_Y2   equ 80              ; RAM bar frame bottom
TM_HDR_Y    equ 87              ; process-list header line
TM_ROW_Y    equ 97              ; first process row
TM_ROW_H    equ 11              ; row pitch, px
TM_ROWS     equ INST_MAX + 1    ; "System" plus one row per instance slot

; -----------------------------------------------------------------------------
; tm_init - read total conventional RAM once (boot thread only)
;
; Runs on task 0 from kmain (after loader_init), so the int 12h call honors
; the only-the-UI-task-calls-BIOS rule. Nothing else happens at boot: the
; window and monitor task exist only while an instance is open (SPEC.md 28).
;
; in:       nothing
; out:      nothing; [tm_totkb] set
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_init:
    push ax
    int 0x12                    ; AX = conventional memory, KB
    mov [tm_totkb], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_kinit - KD_INIT for the Task Manager kind (SPEC.md 29.3)
;
; Zeroes all module state including the history ring (.bss is not zeroed at
; boot, and a relaunched instance must not inherit the previous one's
; calibration or graph) and caches the window ptr.
;
; in:  BX = window ptr, DI = state ptr (0 - module state), SI = record
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tm_kinit:
    push ax
    push cx
    push di
    push es

    push ds                     ; zero the state block (DF=0 per SPEC.md 1)
    pop es
    mov di, tm_zero_beg
    mov cx, tm_zero_end - tm_zero_beg
    xor al, al
    rep stosb
    mov [tm_win], bx

    pop es
    pop di
    pop cx
    pop ax
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
tm_tpl:     dw 250, 100, 176, 264, tm_ttl, tm_paint, 0, 0
tm_ttl:     db 'Task Manager', 0
tm_sname:   db 'TaskMgr', 0     ; KD_NAME: fits the 7-char NAME column

tm_s_cpu:   db 'CPU ', 0
tm_s_pre:   db 'SCH preempt', 0 ; read-only scheduler-mode field, chars 9..19
tm_s_coop:  db 'SCH coop   ', 0 ; both exactly 11: the padding erases the
                                ; longer word when the mode changes
tm_s_ram:   db 'RAM ', 0
tm_s_hdr:   db 'NAME    ST  CPU MEM', 0
tm_s_sys:   db 'System', 0      ; row 0: the kernel's own share
tm_s_dash:  db '-', 0
tm_s_run:   db 'run', 0
tm_s_rdy:   db 'rdy', 0
tm_s_slp:   db 'slp', 0
tm_s_evt:   db 'evt', 0         ; task-less: runs only in window callbacks
tm_s_die:   db 'die', 0         ; I_STATE 2: closing, task not yet reaped
tm_s_free:  db '---', 0
tm_s_frtl:  db '  -    -', 0    ; free row's CPU + MEM columns

; -----------------------------------------------------------------------------
; tm_task - the monitor task: spin-measure for TM_INT ticks, sample, redraw,
;           die on request
; in:  DX = instance index (the spawn argument, SPEC.md 8); never returns
; -----------------------------------------------------------------------------
tm_task:
    mov al, dl
    call inst_ptr               ; DI = our instance record
    mov bp, di                  ; BP = record for life
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

    cmp byte [bp+I_STATE], 2    ; close requested? (once per interval)
    je .die

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

.die:
    mov di, bp
    jmp inst_task_die           ; destroys the window, frees the record,
                                ; task_exit - never returns

; -----------------------------------------------------------------------------
; tm_sample - one measurement pass: load gauge, history push, per-instance CPU
;             shares, RAM usage. Touches only module state; no lock needed.
; in:  [tm_cnt] = the interval's spin count
; out: tm_load/tm_hist/tm_pos/tm_lastcol/tm_pct/tm_state/tm_self/tm_usedk/
;      tm_barw and the whole instance snapshot block updated
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

    ; --- snapshot scheduler + instance state (one atomic block, 8.1/28) ------
    ; Task cycles and T_STATE, then the whole instance table: I_STATE,
    ; I_TASK, I_SIZE, I_CYC and the 16 I_NAME bytes of every slot. One cli
    ; window for the lot - task_exit frees a record atomically with its task
    ; slot (SPEC.md 8), so nothing read here can be torn against itself.
    pushf
    cli
    mov si, sch_cycles
    mov di, tm_tnew
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

    mov si, inst_tab            ; SI = record, BX = instance index
    xor bx, bx
.isnap:
    mov al, [si+I_STATE]
    mov [tm_ist+bx], al
    mov al, [si+I_TASK]
    mov [tm_itsk+bx], al
    mov di, bx
    shl di, 1                   ; DI = index*2
    mov ax, [si+I_SIZE]
    mov [tm_isz+di], ax
    shl di, 1                   ; DI = index*4
    mov ax, [si+I_CYC]
    mov [tm_inew+di], ax
    mov ax, [si+I_CYC+2]
    mov [tm_inew+di+2], ax
    mov ax, bx                  ; name goes to tm_inm + index*16
    mov cl, 4
    shl ax, cl
    mov di, ax
    add di, tm_inm
    push si
    add si, I_NAME
    mov cx, 16
.incpy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .incpy
    pop si
    add si, I_RECSZ
    inc bx
    cmp bx, INST_MAX
    jb .isnap
    popf

    ; --- per-task diffs ------------------------------------------------------
    xor bx, bx
.tdiff:
    mov ax, [tm_tnew+bx]
    mov dx, [tm_tnew+bx+2]
    sub ax, [tm_told+bx]        ; wrap-safe 32-bit difference
    sbb dx, [tm_told+bx+2]
    mov cx, [tm_tnew+bx]
    mov [tm_told+bx], cx
    mov cx, [tm_tnew+bx+2]
    mov [tm_told+bx+2], cx
    test dx, dx                 ; negative diff = the counter regressed (a
    jns .tsign                  ; slot reused mid-interval, a rare PIT stamp
    xor ax, ax                  ; race, or a task_debit that reached back
    xor dx, dx                  ; into the previous interval): force 0 - a
.tsign:                         ; wrapped "huge" diff would clamp rows to 100%
    mov si, bx                  ; appeared-slot rule (SPEC.md 28): a slot
    shr si, 1                   ; free last sample but used now had its
    shr si, 1                   ; counter reset by task_spawn - the diff is
    cmp byte [tm_pstate+si], 0  ; meaningless, force 0
    jne .tkeep
    cmp byte [tm_state+si], 0
    je .tkeep
    xor ax, ax
    xor dx, dx
.tkeep:
    mov [tm_tdif+bx], ax
    mov [tm_tdif+bx+2], dx
    add bx, 4
    cmp bx, MAX_TASKS * 4
    jb .tdiff

    ; --- per-instance diffs (same two rules; inst_alloc zeroes I_CYC) --------
    xor bx, bx
.idiff:
    mov ax, [tm_inew+bx]
    mov dx, [tm_inew+bx+2]
    sub ax, [tm_iold+bx]
    sbb dx, [tm_iold+bx+2]
    mov cx, [tm_inew+bx]
    mov [tm_iold+bx], cx
    mov cx, [tm_inew+bx+2]
    mov [tm_iold+bx+2], cx
    test dx, dx
    jns .isign
    xor ax, ax
    xor dx, dx
.isign:
    mov si, bx
    shr si, 1
    shr si, 1
    cmp byte [tm_pist+si], 0
    jne .ikeep
    cmp byte [tm_ist+si], 0
    je .ikeep
    xor ax, ax
    xor dx, dx
.ikeep:
    mov [tm_idif+bx], ax
    mov [tm_idif+bx+2], dx
    add bx, 4
    cmp bx, INST_MAX * 4
    jb .idiff

    xor bx, bx                  ; this sample's states become the previous
.pcopy:
    mov al, [tm_state+bx]
    mov [tm_pstate+bx], al
    inc bx
    cmp bx, MAX_TASKS
    jb .pcopy
    xor bx, bx
.picopy:
    mov al, [tm_ist+bx]
    mov [tm_pist+bx], al
    inc bx
    cmp bx, INST_MAX
    jb .picopy

    ; --- fold task + instance cycles into one cycle figure per ROW ----------
    ; Row 0 is the UI task's remainder (callbacks have already been debited
    ; off it); row 1+i is instance i's callback time plus, if it owns a task,
    ; that task's slice. Cycles of a task whose instance died this interval
    ; belong to no row and simply drop out of the total.
    mov ax, [tm_tdif]
    mov [tm_rcyc], ax
    mov ax, [tm_tdif+2]
    mov [tm_rcyc+2], ax

    xor bx, bx                  ; BX = instance index
.rowc:
    mov si, bx
    shl si, 1
    shl si, 1                   ; SI = index*4
    mov ax, [tm_idif+si]
    mov dx, [tm_idif+si+2]
    cmp byte [tm_ist+bx], 0     ; a free record's I_TASK byte is stale
    je .rnotask
    mov cl, [tm_itsk+bx]
    cmp cl, MAX_TASKS           ; catches 0xFF (task-less) and any garbage
    jae .rnotask
    xor ch, ch
    shl cx, 1
    shl cx, 1
    mov di, cx
    add ax, [tm_tdif+di]
    adc dx, [tm_tdif+di+2]
.rnotask:
    mov di, si
    add di, 4                   ; row index = instance index + 1
    mov [tm_rcyc+di], ax
    mov [tm_rcyc+di+2], dx
    inc bx
    cmp bx, INST_MAX
    jb .rowc

    xor ax, ax                  ; total = sum of the rows, so the shares of
    mov [tm_total], ax          ; a fully accounted system add up to 100
    mov [tm_total+2], ax
    xor bx, bx
.tsum:
    mov ax, [tm_rcyc+bx]
    mov dx, [tm_rcyc+bx+2]
    add [tm_total], ax
    adc [tm_total+2], dx
    add bx, 4
    cmp bx, TM_ROWS * 4
    jb .tsum

    ; normalize: shift total and every row right until total fits 16 bits
.tnorm:
    cmp word [tm_total+2], 0
    je .tnormed
    shr word [tm_total+2], 1
    rcr word [tm_total], 1
    xor bx, bx
.tshr:
    shr word [tm_rcyc+bx+2], 1
    rcr word [tm_rcyc+bx], 1
    add bx, 4
    cmp bx, TM_ROWS * 4
    jb .tshr
    jmp .tnorm
.tnormed:

    ; share_i = row_i*100/total; total 0 -> all 0; clamp keeps DIV safe
    xor bx, bx
    mov di, tm_pct
.pct:
    mov cx, [tm_total]
    jcxz .pzero
    mov ax, [tm_rcyc+bx+2]
    or ax, ax
    jnz .pfull                  ; pathological row > total: clamp
    mov ax, [tm_rcyc+bx]
    cmp ax, cx
    ja .pfull
    mov si, 100
    mul si                      ; DX:AX = row*100
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
    cmp bx, TM_ROWS * 4
    jb .pct

    ; --- RAM: kernel footprint + resident package regions (SPEC.md 28) -------
    ; Straight off the snapshot above - built-ins carry I_SIZE 0, so summing
    ; every in-use record counts exactly the resident package regions. Dying
    ; instances still count: their region is still resident. The loader keeps
    ; the sum <= APP_MAX_SIZE, so the 16-bit total cannot overflow.
    xor bx, bx
    xor si, si
.rsum:
    cmp byte [tm_ist+si], 0
    je .rnext
    mov di, si
    shl di, 1
    add bx, [tm_isz+di]
.rnext:
    inc si
    cmp si, INST_MAX
    jb .rsum
    mov ax, kernel_bss_end      ; kernel text+bss footprint (bare label);
    add ax, bx                  ; max total 0xA000 + 0x5000 = 61440, no
                                ; 16-bit overflow possible
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
; elements are touched: the CPU + scheduler-mode line, the freshly written
; column + the advanced sweep gap, RAM line + bar, and the task rows.
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
; tm_txt_cpu - white-fill the CPU line's rect and draw the 20-char
;              "CPU nnn% SCH xxxxxxx" line (SPEC.md 28): chars 0..7 the load,
;              8 a space, 9..19 the read-only scheduler-mode field. The whole
;              line is rewritten every interval, so a mode flip from the
;              Control Panel (SPEC.md 31) shows up within one sample period.
; in:  [tm_load]; live mode from sched_mode_get (SPEC.md 8.2)
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
    add cx, 167
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
    mov byte [di], ' '
    inc di
    mov si, tm_s_pre            ; chars 9..19: the live scheduler mode
    call sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
    or al, al
    jz .mode
    mov si, tm_s_coop
.mode:
    call tm_copy                ; 11 chars either way, space-padded
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
; tm_rows - the TM_ROWS process rows: white-fill each band, build the 20-char
;           row string, draw it (SPEC.md 28 pins the format)
; in:  tm_self/tm_state/tm_pct + the instance snapshot block
; out: nothing
; clobbers: nothing (flags only)
;
; Row 0 is "System"; row 1+i mirrors instance slot i, so a row's position is
; stable for as long as the instance lives (same slot<->row rule as the dock,
; SPEC.md 30).
; -----------------------------------------------------------------------------
tm_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    xor si, si                  ; SI = row index
.row:
    mov ax, si                  ; y1 = cy + TM_ROW_Y + 11*row
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

    ; --- build "NAME    ST  nnn%mmmK" in tm_str ------------------------------
    mov di, tm_str
    or si, si
    jnz .inst

    ; row 0: the kernel - task 0 minus everything debited to an instance
    push si
    mov si, tm_s_sys
    call tm_copy7
    mov byte [di], ' '
    inc di
    mov si, tm_s_rdy            ; task 0 never sleeps; it shows "run" only
    cmp byte [tm_self], 0       ; when it happened to be current at snapshot
    jne .sysst
    mov si, tm_s_run
.sysst:
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di
    mov bx, si
    mov al, [tm_pct+bx]
    call tm_cpucol
    mov ax, kernel_bss_end      ; the kernel's own image + bss (bare label)
    call tm_memcol
    jmp .draw

.inst:
    mov bx, si
    dec bx                      ; BX = instance index, held to the end
    cmp byte [tm_ist+bx], 0
    je .free

    push si                     ; name: the I_NAME snapshot, 7 chars
    mov ax, bx
    mov cl, 4
    shl ax, cl
    mov si, ax
    add si, tm_inm
    call tm_copy7
    pop si
    mov byte [di], ' '
    inc di

    push si                     ; state (SPEC.md 28)
    cmp byte [tm_ist+bx], 2
    je .stdie
    mov cl, [tm_itsk+bx]
    cmp cl, MAX_TASKS           ; 0xFF = task-less: lives in callbacks only
    jae .stevt
    cmp cl, [tm_self]
    je .strun
    xor ch, ch
    mov si, cx                  ; SI = the owning task's slot
    cmp byte [tm_state+si], 2
    je .stslp
    mov si, tm_s_rdy
    jmp .stput
.stslp:
    mov si, tm_s_slp
    jmp .stput
.strun:
    mov si, tm_s_run
    jmp .stput
.stevt:
    mov si, tm_s_evt
    jmp .stput
.stdie:
    mov si, tm_s_die
.stput:
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di

    mov bx, si                  ; share, right-aligned in 3, then '%'
    mov al, [tm_pct+bx]
    call tm_cpucol
    mov bx, si                  ; region bytes (0 for a built-in kind)
    dec bx
    shl bx, 1
    mov ax, [tm_isz+bx]
    call tm_memcol
    jmp .draw

.free:                          ; free slot: "-       ---   -    -"
    push si
    mov si, tm_s_dash
    call tm_copy7
    mov byte [di], ' '
    inc di
    mov si, tm_s_free
    call tm_copy
    mov byte [di], ' '
    inc di
    mov si, tm_s_frtl
    call tm_copy
    pop si

.draw:
    mov byte [di], 0
    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 6
    mov dx, [tm_rowy]
    push si
    mov si, tm_str
    call font_str
    pop si

    inc si
    cmp si, TM_ROWS
    jb .row

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_cpucol - the CPU column: share right-aligned in 3, then '%'
; in:  AL = share 0..100, DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_cpucol:
    push ax
    xor ah, ah
    call tm_put3
    mov byte [di], '%'
    inc di
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_memcol - the MEM column: KB right-aligned in 3, then 'K'; an instance
;             that owns no region of its own (every built-in kind) shows
;             "   -" instead of a misleading 0 (SPEC.md 28)
; in:  AX = bytes, DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_memcol:
    push ax
    push cx
    or ax, ax
    jz .none
    add ax, 1023                ; round up; AX <= 0xA000 here, no overflow
    mov cl, 10
    shr ax, cl
    call tm_put3
    mov byte [di], 'K'
    inc di
    jmp .out
.none:
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di
    mov byte [di], ' '
    inc di
    mov byte [di], '-'
    inc di
.out:
    pop cx
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
tm_told:    resw MAX_TASKS * 2  ; previous sch_cycles snapshot
tm_tnew:    resw MAX_TASKS * 2  ; current snapshot
tm_tdif:    resw MAX_TASKS * 2  ; per-task interval cycles
tm_state:   resb MAX_TASKS      ; T_STATE snapshot
tm_pstate:  resb MAX_TASKS      ; previous sample's T_STATE (appeared rule)
tm_self:    resb 1              ; our own slot ([sch_cur] at snapshot)
tm_iold:    resw INST_MAX * 2   ; previous I_CYC snapshot
tm_inew:    resw INST_MAX * 2   ; current snapshot
tm_idif:    resw INST_MAX * 2   ; per-instance interval callback cycles
tm_ist:     resb INST_MAX       ; I_STATE snapshot
tm_pist:    resb INST_MAX       ; previous sample's I_STATE (appeared rule)
tm_itsk:    resb INST_MAX       ; I_TASK snapshot (owning task or 0xFF)
tm_isz:     resw INST_MAX       ; I_SIZE snapshot, bytes
tm_inm:     resb INST_MAX * 16  ; I_NAME snapshots
tm_rcyc:    resw TM_ROWS * 2    ; per-ROW cycles: task + instance, dword
tm_total:   resw 2              ; sum of the rows, dword
tm_pct:     resb TM_ROWS        ; per-row share, 0..100
tm_usedk:   resw 1              ; used RAM, KB
tm_barw:    resw 1              ; RAM bar black width, 0..160
tm_zero_end:

tm_totkb:   resw 1              ; total conventional RAM (int 12h, boot)
tm_cx:      resw 1              ; cached content origin (draw paths only,
tm_cy:      resw 1              ; always under the gfx lock)
tm_rowy:    resw 1              ; tm_rows scratch: current row y
tm_str:     resb 24             ; line-format scratch

section .text
