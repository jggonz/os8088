; =============================================================================
; os8088 - taskmgr.inc
;
; The Task Manager window (SPEC.md 28): a live CPU load gauge with an
; oscilloscope-sweep history graph, a RAM readout, and the process list with
; per-instance CPU shares and memory from the PIT cycle accounting
; (SPEC.md 8.1). A content click toggles a second, memory view: the RAM
; readout, a conventional-memory map, a kernel-segment map with each
; resident package's region drawn in its slot's dither pattern, and the
; process list re-columned as NAME/ADDR/SIZE with a matching legend square
; per package row.
;
; The list is an INSTANCE list, not a task list (SPEC.md 29). A task list
; would show only the kinds that own a task; every task-less app - About,
; Disk, and every loaded package that has not claimed a worker (SPEC.md
; 20.6) - runs purely inside window callbacks on the UI task and
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
TM_GW       equ 184             ; graph interior width = history ring size
                                ; (7..TM_RW-1 inclusive; the rows are 22
                                ; chars = 176px, so both stop clear of the
                                ; scroll-free right edge at TM_RW)
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
TM_RW       equ 191             ; the rows' right edge, content-relative

; --- memory-view geometry (SPEC.md 28) ---------------------------------------
TMM_RAM_Y   equ 4               ; "RAM uuuK/tttK" line (same string, new y)
TMM_M1_Y1   equ 14              ; conventional-memory map frame top
TMM_M1_Y2   equ 29              ; frame bottom (interior rows 15..28)
TMM_CAP_Y   equ 33              ; "HEAP nnnK/nnnK" caption line
TMM_M2_Y1   equ 43              ; package-pool map frame top
TMM_M2_Y2   equ 58              ; frame bottom (interior rows 44..57)
TMM_HDR_Y   equ 62              ; "NAME    ADDR SIZE  CLM" header line

; --- what the kernel reserves, in KB (SPEC.md 2/28) --------------------------
; The two figures the RAM readout was missing, and the reason the audit found
; it reporting 145K on a machine that was 224K committed. Both are compile-
; time constants because both are reservations, not allocations: nothing else
; can ever be given any of this memory, whether or not a byte of it is live.
TM_KLOW_KB  equ ((KERNEL_SEG - FAR_SEG) * 16 + 1023) / 1024
                                ; ALL of low memory: the far-code blob, the
                                ; FAT snapshot, .lowbss and task 0's stack -
                                ; the stack every built-in app runs on
TM_KSEG_KB  equ (KERN_MAX + 1023) / 1024
                                ; the kernel's own segment budget, headroom
                                ; included: the pool starts at KERN_MAX, so
                                ; the unused part is the kernel's and no one
                                ; else's
TM_POOL_KB0 equ PKG_SEG / 64    ; where the package pool starts, in KB
TM_POOL_KB  equ PKG_PARA / 64   ; ...and how big it is
TMM_ROW_Y   equ 74              ; first process row (same TM_ROW_H pitch)
TMM_ROWS    equ INST_MAX + 3    ; System, the two headings, every instance -
                                ; 15 x TM_ROW_H from TMM_ROW_Y is 239 of the
                                ; 245-pixel content, which is what decides
                                ; that free slots are not drawn at all

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
tm_tpl:     dw 250, 100, 200, 264, tm_ttl, tm_paint, 0, tm_click
tm_ttl:     db 'Task Manager', 0
tm_sname:   db 'TaskMgr', 0     ; KD_NAME: fits the 7-char NAME column

tm_s_cpu:   db 'CPU ', 0
tm_s_pre:   db 'SCH preempt', 0 ; read-only scheduler-mode field, chars 9..19
tm_s_coop:  db 'SCH coop   ', 0 ; both exactly 11: the padding erases the
                                ; longer word when the mode changes
tm_s_ram:   db 'RAM ', 0
tm_s_hdr:   db 'NAME     ST  CPU  MEM', 0   ; 9-char NAME field: 7 for the
                                ; name, one for the built-ins' indent, one
                                ; separator (tm_name9). Two spaces before
                                ; MEM, not one: 100% next to 335K ran the
                                ; two figures together
tm_s_sys:   db 'System', 0      ; row 0: the kernel's own share
tm_s_dash:  db '-', 0
tm_s_run:   db 'run', 0
tm_s_rdy:   db 'rdy', 0
tm_s_slp:   db 'slp', 0
tm_s_evt:   db 'evt', 0         ; no worker task: runs only in callbacks
tm_s_die:   db 'die', 0         ; I_STATE 2: closing, task not yet reaped
tm_s_free:  db '---', 0
tm_s_frtl:  db '  -     -', 0   ; free row's CPU + MEM columns

; --- memory view (SPEC.md 28) ------------------------------------------------
tm_s_mhdr:  db 'NAME    ADDR SIZE  CLM', 0    ; 22 chars, the row width
tm_s_heap:  db 'HEAP ', 0       ; + claimed/size KB (SPEC.md 50)
tm_s_bhdr:  db 'Builtins    ', 0 ; + used/max of the kernel segment they share
tm_s_phdr:  db 'Packages    ', 0 ; + used/max of the package pool
tm_s_mfr:   db '   -    -', 0   ; the ADDR+SIZE pair a built-in has no answer
                                ; for: it owns no region of its own
tm_s_dash4: db '   -', 0        ; one empty KB column
tm_s_sys0:  db '0600', 0        ; where the kernel's reservation starts:
                                ; FAR_SEG, the bottom of low memory

; Slot patterns: 12 x 8 row bytes, pattern i = tm_pats + i*8, bit set =
; white (SPEC.md 5/28). Black-on-white hatch textures, none of them the
; AA/55 50% checker (that means "system-reserved" in the maps).
tm_pats:
    db 0x77, 0xFF, 0xDD, 0xFF, 0x77, 0xFF, 0xDD, 0xFF   ; 0: sparse dots
    db 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55   ; 1: fine vertical
    db 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00   ; 2: fine horizontal
    db 0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0xFE   ; 3: thin diagonal
    db 0x00, 0x77, 0x77, 0x77, 0x00, 0x77, 0x77, 0x77   ; 4: grid
    db 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F   ; 5: thin diagonal /
    db 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33   ; 6: thick vertical
    db 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00   ; 7: thick horizontal
    db 0x00, 0xF7, 0xF7, 0xF7, 0x00, 0x7F, 0x7F, 0x7F   ; 8: bricks
    db 0x7E, 0xBD, 0xDB, 0xE7, 0xE7, 0xDB, 0xBD, 0x7E   ; 9: diamond
    db 0x3F, 0x9F, 0xCF, 0xE7, 0xF3, 0xF9, 0xFC, 0x7E   ; 10: thick diagonal
    db 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00   ; 11: dense dots

; -----------------------------------------------------------------------------
; Everything above stays in the kernel segment: tm_init and tm_kinit (both
; self-contained, ~40 bytes) plus all the strings and the window template,
; which far code reaches through DS (SPEC.md 33 rule 2). Everything below is
; code and moves to .fartext.
;
; Three entry points keep near names as shims: tm_paint and tm_click,
; because tm_tpl and the wm/ui dispatchers want near pointers, and
; tm_task, because task_spawn builds a frame with CS = KERNEL_SEG
; (SPEC.md 8) and can only launch a near entry. tm_task never returns, so
; the shim's own `ret` is dead code and its `call far` return address is
; simply abandoned with the rest of the stack at task_exit.
; -----------------------------------------------------------------------------
    FARSHIM tm_paint, tmf_paint
    FARSHIM tm_click, tmf_click
    FARSHIM tm_task,  tmf_task

section .fartext

; -----------------------------------------------------------------------------
; tmf_task - the monitor task: spin-measure for TM_INT ticks, sample, redraw,
;           die on request
; in:  DX = instance index (the spawn argument, SPEC.md 8); never returns
; -----------------------------------------------------------------------------
tmf_task:
    mov al, dl
    KCALL inst_ptr               ; DI = our instance record
    mov bp, di                  ; BP = record for life
.interval:
    mov ax, [ticks]
    mov [tm_t0], ax
    mov word [tm_cnt], 0
    mov word [tm_cnt+2], 0
.spin:
    add word [tm_cnt], 1        ; 32-bit spin counter
    adc word [tm_cnt+2], 0
    KCALL task_yield
    mov ax, [ticks]
    sub ax, [tm_t0]             ; wrap-safe interval check
    cmp ax, TM_INT
    jb .spin

    cmp byte [ds:bp+I_STATE], 2    ; close requested? (once per interval)
    je .die

    call tm_sample              ; no lock: computes load, shares, RAM

    KCALL gfx_lock
    mov bx, [tm_win]
    test word [bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14)
    jz .skip
    KCALL wm_obscured
    jc .skip
    call tm_update              ; incremental redraw, lock held
.skip:
    KCALL gfx_unlock
    jmp .interval

.die:
    mov di, bp
    jmp KERNEL_SEG:inst_task_die    ; destroys the window, frees the
                                ; record, task_exit - never returns, so a far
                                ; jmp needs no wrapper and leaves no frame

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
    mov al, [si+I_KIND]         ; bit 7 = package: the memory view groups on
    mov [tm_iknd+bx], al        ; it, and bills claims by it (SPEC.md 28/50)
    mov di, bx
    shl di, 1                   ; DI = index*2
    mov ax, [si+I_SIZE]
    mov [tm_isz+di], ax
    mov ax, [si+I_SPTR]         ; region base for the memory view's map
    mov [tm_ispt+di], ax        ; (meaningless for built-ins: I_SIZE 0
                                ; filters them out, SPEC.md 28)
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
    mov ax, bx                  ; the pool's resident regions, in KB
    add ax, 1023
    mov cl, 10
    shr ax, cl
    add ax, TM_KLOW_KB          ; + ALL of low memory (SPEC.md 2.1): the far
                                ; blob, the FAT snapshot, .lowbss and task
                                ; 0's stack. The kernel owns 0x00600..0x0FFFF
                                ; entire, so all of it is spoken for - the
                                ; old figure billed only .lowbss + .fartext
                                ; and left 32KB of it looking free
    add ax, TM_KSEG_KB          ; + the kernel's own segment budget, headroom
                                ; included: the pool starts where it ends, so
                                ; nothing else can be given the slack
    KCALL mem_claimed_kb        ; + everything claimed out of the heap
                                ; (SPEC.md 50): the back buffer while it is
                                ; armed, the menu save-under, each open Disk
                                ; window's listing cache, and every package's
                                ; claim - live, not a constant
    mov [tm_usedk], ax          ; ...and what is left really is free: the
                                ; unallocated pool and the unclaimed heap
                                ; are the only two things not counted here,
                                ; and both can be handed out on demand
    mov cx, [tm_totkb]          ; barw = usedK*TM_GW/totalK (all KB)
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
; tm_paint - W_PAINT: bare, unconditional full redraw of the active view
; in:  SI = window ptr (gfx lock held by caller; no lock, no visibility
;      check in here - SPEC.md 28)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tmf_paint:
    call tm_draw_full
    retf                        ; far body: reached through the tm_paint shim

; -----------------------------------------------------------------------------
; tm_draw_full - full redraw of whichever view [tm_view] selects (also the
;                tail of tmf_click's in-place view swap)
; in:  SI = window ptr (gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; tm_ylim_set - cache the lowest row top this window can still hold
;
; The row lists are fixed-pitch and TM_ROWS long, and nothing in the kernel
; clips a draw to a WINDOW - vga_rect_setup clips to the SCREEN. That was
; harmless while every template fitted, but since SPEC.md 39.7 wm_create
; clamps a frame onto the live screen, and on a 200-row CGA this window's
; 264px template becomes 156px: unbounded, the last rows would be white-filled
; and lettered over the dock strip, once a second.
; in:       BX = window ptr
; out:      [tm_ylim] = the largest row top whose 8px band stays inside the
;           frame's bottom border
; clobbers: flags
; -----------------------------------------------------------------------------
tm_ylim_set:
    push ax
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    sub ax, 9                   ; 1px bottom border + the row's 8px band
    mov [tm_ylim], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
tm_draw_full:
    cmp byte [tm_view], 0
    jne tm_draw_mem
    ; fall through: performance view

tm_draw_perf:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    KCALL wm_content             ; AX = content left, DX = content top
    mov [tm_cx], ax
    mov [tm_cy], dx
    call tm_ylim_set

    call tm_txt_cpu

    mov byte [gfx_color], CBLACK
    mov ax, [tm_cx]             ; graph frame (6,14)-(TM_RW,55)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_GF_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TM_GF_Y2
    KCALL gfx_frame

    xor bx, bx                  ; every column; tm_pos is the sweep gap
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
    mov ax, [tm_cx]             ; bar frame (6,71)-(TM_RW,80)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TM_BAR_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2
    KCALL gfx_frame
    call tm_bar

    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]             ; task-list header
    add cx, 6
    mov dx, [tm_cy]
    add dx, TM_HDR_Y
    mov si, tm_s_hdr
    KCALL font_str
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
; elements are touched. Performance view: the CPU + scheduler-mode line,
; the freshly written column + the advanced sweep gap, RAM line + bar, and
; the task rows. Memory view: the RAM line, the pool caption, both map
; interiors and the rows (SPEC.md 28).
;
; in:  nothing ([tm_win] set)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_update:
    cmp byte [tm_view], 0
    jne tm_upd_mem
    push ax
    push bx
    push dx

    mov bx, [tm_win]
    KCALL wm_content
    mov [tm_cx], ax
    mov [tm_cy], dx
    call tm_ylim_set

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
; tmf_click - W_ONCLICK (far body via the tm_click shim): ANY content click
;             toggles the view (SPEC.md 28). ui.inc only feeds clicks to the
;             front window, so a raising click never lands here. The lock is
;             already held: white-fill the whole content and run the new
;             view's full paint body in place.
; in:  CX = x, DX = y (absolute, unused), SI = window ptr
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tmf_click:
    push ax
    push bx
    push cx
    push dx

    xor byte [tm_view], 1
    mov bx, si
    KCALL wm_content             ; AX = content left, DX = content top
    mov byte [gfx_color], CWHITE
    mov bx, dx                  ; content is 198x245 (SPEC.md 28)
    mov cx, ax
    add cx, 197
    mov dx, bx
    add dx, 244
    KCALL gfx_fill
    call tm_draw_full           ; SI = window ptr still

    pop dx
    pop cx
    pop bx
    pop ax
    retf                        ; far body: reached through the tm_click shim

; -----------------------------------------------------------------------------
; tm_upd_mem - the memory view's periodic redraw (gfx lock held): everything
;              but the two static map frames and the header
; in:  nothing ([tm_win] set)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_upd_mem:
    push ax
    push bx
    push dx

    mov bx, [tm_win]
    KCALL wm_content
    mov [tm_cx], ax
    mov [tm_cy], dx
    call tm_ylim_set

    mov ax, TMM_RAM_Y
    call tm_txt_ram_y
    call tm_cap_heap
    call tm_map_ram
    call tm_map_pool
    call tm_rows_mem

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_draw_mem - the memory view's full redraw: frames + header + everything
;               tm_upd_mem redraws
; in:  SI = window ptr (gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_draw_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    KCALL wm_content             ; AX = content left, DX = content top
    mov [tm_cx], ax
    mov [tm_cy], dx
    call tm_ylim_set

    mov ax, TMM_RAM_Y
    call tm_txt_ram_y

    mov byte [gfx_color], CBLACK
    mov ax, [tm_cx]             ; RAM map frame (6,14)-(TM_RW,29)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TMM_M1_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TMM_M1_Y2
    KCALL gfx_frame

    mov ax, [tm_cx]             ; pool map frame (6,43)-(TM_RW,58)
    add ax, 6
    mov bx, [tm_cy]
    add bx, TMM_M2_Y1
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TMM_M2_Y2
    KCALL gfx_frame

    mov cx, [tm_cx]             ; header, at the rows' text x
    add cx, 16
    mov dx, [tm_cy]
    add dx, TMM_HDR_Y
    mov si, tm_s_mhdr
    KCALL font_str

    call tm_cap_heap
    call tm_map_ram
    call tm_map_pool
    call tm_rows_mem

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_cap_heap - white-fill the caption band and draw "HEAP nnnK/nnnK"
;               (claimed / heap size, SPEC.md 50)
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
;
; The third of the four things memory is: the kernel's fixed reservation
; (System's row), the package pool (the map below), and this - everything
; handed out on demand, kernel and package claims together.
; -----------------------------------------------------------------------------
tm_cap_heap:
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
    add bx, TMM_CAP_Y
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TMM_CAP_Y + 7
    KCALL gfx_fill

    KCALL mem_total_kb          ; AX = claimed KB, BX = heap size KB
    mov di, tm_str
    push bx
    push ax
    mov si, tm_s_heap
    call tm_copy
    pop ax
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], '/'
    inc di
    pop ax
    call tm_put3
    mov byte [di], 'K'
    inc di
    mov byte [di], 0

    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 6
    mov dx, [tm_cy]
    add dx, TMM_CAP_Y
    mov si, tm_str
    KCALL font_str

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_map_ram - the conventional-memory map interior (SPEC.md 28/50)
;
; Four kinds of band, and between them they now cover every byte the machine
; has: gray = the kernel's fixed reservation (low memory, then its own
; segment), black = the package pool, gray = every live heap claim at its
; real address, white = free. The old version painted a band for SND_SEG
; that no longer exists and left SAVE_SEG, FAT_SEG and task 0's stack white
; while billing them nowhere - the audit's headline finding.
; in:  [tm_totkb]
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_map_ram:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [gfx_color], CWHITE
    xor ax, ax                  ; whole interior white first
    mov cx, TM_GW - 1
    mov bx, TMM_M1_Y1
    call tm_map_rect
    KCALL gfx_fill

    xor ax, ax                  ; [0, kernel segment end): the BIOS area, the
    call tm_ramkb2x             ; far blob, the FAT snapshot, .lowbss, task
    mov si, ax                  ; 0's stack and the kernel's own 45KB - all
    mov ax, TM_KLOW_KB + TM_KSEG_KB - 1     ; of it reserved, none of it
    call tm_ramkb2x             ; available to anything else
    mov cx, ax
    mov ax, si
    mov bx, TMM_M1_Y1
    call tm_map_rect
    KCALL gfx_fill_gray

    mov ax, TM_POOL_KB0         ; the package pool, black: it is not free
    call tm_ramkb2x             ; memory, it is the pool
    mov si, ax
    mov ax, TM_POOL_KB0 + TM_POOL_KB - 1
    call tm_ramkb2x
    mov cx, ax
    mov ax, si
    mov bx, TMM_M1_Y1
    call tm_map_rect
    mov byte [gfx_color], CBLACK
    KCALL gfx_fill

    xor si, si                  ; every live claim, at its real address
.claim:
    mov ax, si
    KCALL mem_claim_get         ; out CF=1 = free record; DX = seg, CX = paras
    jc .cnext
    push si
    mov ax, dx                  ; segment -> KB
    mov cl, 6
    shr ax, cl
    push ax
    mov ax, cx                  ; paragraphs -> KB
    mov cl, 6
    shr ax, cl
    mov bx, ax
    pop ax
    push bx
    call tm_ramkb2x
    mov si, ax
    pop bx
    pop ax                      ; (AX = the claim's KB base again below)
    push ax
    mov ax, dx
    mov cl, 6
    shr ax, cl
    add ax, bx
    dec ax                      ; inclusive end KB
    call tm_ramkb2x
    mov cx, ax
    mov ax, si
    pop si
    push si
    mov bx, TMM_M1_Y1
    call tm_map_rect
    KCALL gfx_fill_gray
    pop si
.cnext:
    inc si
    cmp si, MEM_MAX
    jb .claim

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_map_pool - the package-pool map interior (SPEC.md 20.1/28): white =
;               unallocated, one pattern region per resident package. The
;               scale is the pool's own 60KB, not a 64KB segment - the pool
;               left the kernel's segment and is its own address space now.
; in:  tm_ist/tm_isz/tm_ispt snapshot
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_map_pool:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [gfx_color], CWHITE
    xor ax, ax                  ; whole interior white first
    mov cx, TM_GW - 1
    mov bx, TMM_M2_Y1
    call tm_map_rect
    KCALL gfx_fill

    xor si, si                  ; SI = instance slot
.slot:
    cmp byte [tm_ist+si], 0
    je .next
    mov di, si
    shl di, 1
    mov ax, [tm_isz+di]
    or ax, ax                   ; built-ins own no region
    jz .next
    mov bx, [tm_ispt+di]        ; BX = base SEGMENT
    sub bx, PKG_SEG             ; -> paragraphs into the pool
    mov cl, 4
    shr ax, cl                  ; I_SIZE bytes -> paragraphs
    add ax, bx
    dec ax                      ; inclusive end, in paragraphs
    call tm_para2x
    mov cx, ax                  ; CX = end px
    mov ax, bx
    call tm_para2x              ; AX = start px
    mov bx, si                  ; [gfx_pat] = tm_pats + slot*8
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, tm_pats
    mov [gfx_pat], bx
    mov bx, TMM_M2_Y1
    call tm_map_rect
    KCALL gfx_fill_pat
.next:
    inc si
    cmp si, INST_MAX
    jb .slot

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_map_rect - map a pair of interior pixel columns + a frame-top constant
;               to the absolute fill rect of a 14-row map band, clamping so
;               no region drops below 1px (SPEC.md 28)
; in:  AX = x1 px (0..159), CX = x2 px, BX = map frame top (TMM_M*_Y1)
; out: AX = x1, BX = y1, CX = x2, DX = y2, absolute
; clobbers: flags
; -----------------------------------------------------------------------------
tm_map_rect:
    add ax, 7
    add ax, [tm_cx]
    add cx, 7
    add cx, [tm_cx]
    cmp cx, ax
    jae .wok
    mov cx, ax                  ; sub-pixel region: still show 1px
.wok:
    add bx, [tm_cy]
    inc bx                      ; interior = frame rows +1..+14
    mov dx, bx
    add dx, 13
    ret

; -----------------------------------------------------------------------------
; tm_ramkb2x - KB -> conventional-map interior pixel column
; in:  AX = KB
; out: AX = px = KB*160/totalK, clamped to 0..159
; clobbers: flags
; -----------------------------------------------------------------------------
tm_ramkb2x:
    push cx
    push dx
    mov cx, TM_GW
    mul cx                      ; DX:AX = KB*160
    mov cx, [tm_totkb]
    jcxz .zero                  ; int 12h can't return 0, but stay DIV-safe
    div cx
    jmp .clamp
.zero:
    xor ax, ax
.clamp:
    cmp ax, TM_GW - 1
    jbe .done
    mov ax, TM_GW - 1
.done:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_para2x - pool paragraph -> pool-map interior pixel column
; in:  AX = offset
; out: AX = px = para*160/PKG_PARA (<= 159 by construction: the pool is
;      PKG_PARA paragraphs and a region ends inside it)
; clobbers: flags
; -----------------------------------------------------------------------------
tm_para2x:
    push cx
    push dx
    mov cx, TM_GW
    mul cx                      ; DX:AX = para * 160 (<= 614,400)
    mov cx, PKG_PARA
    div cx                      ; <= 159 by construction
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tm_rows_mem - the memory view's rows (SPEC.md 28)
;
; Grouped, not slot-ordered: System, then a Builtins heading and every live
; built-in indented under it, then a Packages heading and every live package.
; Free slots are not drawn at all - a column of dashes told the reader
; nothing that "12 slots, 3 used" does not.
;
; The two headings carry the numbers that used to be nowhere: how much of the
; KERNEL SEGMENT the built-ins share is spent (they have no memory of their
; own - they are kernel code running on the UI task's stack), and how much of
; the PACKAGE POOL is allocated. The CLAIM column is the heap (SPEC.md 50):
; what this instance asked for on top of everything above, which for Paint is
; most of the machine and used to be invisible.
;
; in:  the instance snapshot block
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_rows_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov word [tm_mrow], 0

    ; --- row 0: the kernel's fixed reservation --------------------------------
    call tm_mrow_open
    push si
    mov si, tm_s_sys
    call tm_copy7
    pop si
    mov byte [di], ' '          ; row 0 has no indent, so it pays the
    inc di                      ; separator here instead
    push si
    mov si, tm_s_sys0           ; it starts at the bottom of low memory
    call tm_copy
    pop si
    mov byte [di], ' '
    inc di
    mov ax, TM_KLOW_KB + TM_KSEG_KB     ; low memory + the kernel's own 45KB
    call tm_kcol
    xor ax, ax
    KCALL mem_kernel_kb         ; AX += the kernel's own heap claims, KB
    call tm_kcol
    call tm_sq_frame            ; solid black legend square
    call tm_sq_int
    mov byte [gfx_color], CBLACK
    KCALL gfx_fill
    call tm_mrow_close

    ; --- the Builtins heading, then every live built-in -----------------------
    call tm_mrow_open
    push si
    mov si, tm_s_bhdr
    call tm_copy
    pop si
    mov ax, kernel_bss_end      ; used: the kernel's text + bss, in KB
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call tm_put3
    mov byte [di], '/'
    inc di
    mov ax, TM_KSEG_KB          ; ...of the segment they all live in
    call tm_put3
    mov byte [di], 'K'
    inc di
    call tm_mrow_close

    xor si, si
.blt:
    cmp byte [tm_ist+si], 0
    je .bnext
    test byte [tm_iknd+si], KIND_PKG
    jnz .bnext
    call tm_inst_row            ; SI = slot; built-in shape (no region)
.bnext:
    inc si
    cmp si, INST_MAX
    jb .blt

    ; --- the Packages heading, then every live package ------------------------
    ; The sum comes FIRST, because it needs DI and tm_mrow_open hands DI over
    ; as the string cursor - the two cannot overlap.
    xor bx, bx                  ; used: the sum of resident regions
    push si
    xor si, si
.psum:
    cmp byte [tm_ist+si], 0
    je .pnext
    mov di, si
    shl di, 1
    add bx, [tm_isz+di]
.pnext:
    inc si
    cmp si, INST_MAX
    jb .psum
    pop si
    call tm_mrow_open
    push si
    mov si, tm_s_phdr
    call tm_copy
    pop si
    mov ax, bx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call tm_put3
    mov byte [di], '/'
    inc di
    mov ax, TM_POOL_KB
    call tm_put3
    mov byte [di], 'K'
    inc di
    call tm_mrow_close

    xor si, si
.pkg:
    cmp byte [tm_ist+si], 0
    je .pknext
    test byte [tm_iknd+si], KIND_PKG
    jz .pknext
    call tm_inst_row
.pknext:
    inc si
    cmp si, INST_MAX
    jb .pkg

    ; --- blank whatever rows the list did not reach ---------------------------
.blank:
    cmp word [tm_mrow], TMM_ROWS
    jae .done
    call tm_mrow_open
    call tm_mrow_close
    jmp .blank
.done:

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_inst_row - one instance's row, indented under its heading
; in:  SI = instance slot; the snapshot block
; out: nothing (SI preserved); the row is drawn and [tm_mrow] advanced
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_inst_row:
    push ax
    push bx
    push cx
    push dx
    push di
    call tm_mrow_open
    mov byte [di], ' '          ; the indent: these live INSIDE the region
    inc di                      ; their heading names
    push si
    mov ax, si                  ; name: the I_NAME snapshot. Seven wide and
    mov cl, 4                   ; NO separator after it - the indent already
    shl ax, cl                  ; spent the column the space would have had,
    mov si, ax                  ; and a truncated name is worse than a tight
    add si, tm_inm              ; one
    call tm_copy7
    pop si

    mov bx, si
    shl bx, 1
    mov ax, [tm_isz+bx]         ; I_SIZE: 0 for a built-in
    mov dx, [tm_ispt+bx]        ; I_SPTR: the package's SEGMENT
    or ax, ax
    jz .noreg

    xchg ax, dx                 ; a package: segment as hex, size in KB
    call tm_put4x
    mov byte [di], ' '
    inc di
    xchg ax, dx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call tm_kcol
    call tm_sq_frame            ; legend square in the slot's pattern, which
    push si                     ; is how the row keys the pool map above
    mov si, bx
    shl si, 1
    shl si, 1
    add si, tm_pats
    mov [gfx_pat], si
    pop si
    call tm_sq_int
    KCALL gfx_fill_pat
    jmp short .claim
.noreg:                         ; a built-in: it owns no region, and says so
    push si
    mov si, tm_s_mfr
    call tm_copy
    pop si
.claim:
    call tm_inst_claim          ; SI = slot; AX = KB claimed off the heap
    call tm_kcol
    call tm_mrow_close
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_inst_claim - how much heap has this instance claimed? (SPEC.md 50.5)
; in:  SI = instance slot
; out: AX = KB
; clobbers: nothing else (flags)
;
; A package's claims are stamped with its segment and a built-in's with its
; slot (SPEC.md 50.2), so the owner word depends on which this is.
; -----------------------------------------------------------------------------
tm_inst_claim:
    push bx
    mov bx, si
    test byte [tm_iknd+si], KIND_PKG
    jz .have
    shl bx, 1
    mov bx, [tm_ispt+bx]        ; the package's segment IS its owner word
.have:
    KCALL mem_owned_kb          ; in BX; out AX
    pop bx
    ret

; -----------------------------------------------------------------------------
; tm_mrow_open - start a memory-view row: compute y, white the band, DI =
;                the text buffer
; in:  [tm_mrow]
; out: DI = tm_str, [tm_rowy] set
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
tm_mrow_open:
    push ax
    push bx
    push cx
    push dx
    mov ax, [tm_mrow]           ; y1 = cy + TMM_ROW_Y + 11*row
    mov cx, TM_ROW_H
    mul cx
    add ax, TMM_ROW_Y
    add ax, [tm_cy]
    mov [tm_rowy], ax
    mov byte [gfx_color], CWHITE
    mov bx, ax
    mov dx, ax
    add dx, 7
    mov ax, [tm_cx]
    add ax, 6
    mov cx, [tm_cx]
    add cx, TM_RW
    KCALL gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    mov di, tm_str
    ret

; -----------------------------------------------------------------------------
; tm_mrow_close - terminate and draw the row, then advance [tm_mrow]
; in:  DI = one past the text, [tm_rowy]
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tm_mrow_close:
    push ax
    push cx
    push dx
    push si
    push di
    mov byte [di], 0
    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 16
    mov dx, [tm_rowy]
    mov si, tm_str
    KCALL font_str
    inc word [tm_mrow]
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_kcol - one right-aligned KB column: 3 digits + 'K', or "   -" for zero
; in:  AX = KB, DI = dest
; out: DI advanced by 5 (a leading space, then the four)
; clobbers: nothing else
; -----------------------------------------------------------------------------
tm_kcol:
    push ax
    mov byte [di], ' '
    inc di
    or ax, ax
    jz .none
    call tm_put3
    mov byte [di], 'K'
    inc di
    jmp short .out
.none:
    push si
    mov si, tm_s_dash4
    call tm_copy
    pop si
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_sq_frame - the 1px black frame of the current row's legend square,
;               (6,rowy)-(13,rowy+7)
; in:  [tm_cx], [tm_rowy]
; out: nothing
; clobbers: flags ([gfx_color] left black)
; -----------------------------------------------------------------------------
tm_sq_frame:
    push ax
    push bx
    push cx
    push dx
    mov byte [gfx_color], CBLACK
    mov ax, [tm_cx]
    add ax, 6
    mov bx, [tm_rowy]
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    KCALL gfx_frame
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_sq_int - the legend square's interior rect, ready for a fill
; in:  [tm_cx], [tm_rowy]
; out: AX = x1, BX = y1, CX = x2, DX = y2 = (7,rowy+1)-(12,rowy+6)
; clobbers: flags
; -----------------------------------------------------------------------------
tm_sq_int:
    mov ax, [tm_cx]
    add ax, 7
    mov bx, [tm_rowy]
    inc bx
    mov cx, ax
    add cx, 5
    mov dx, bx
    add dx, 5
    ret

; -----------------------------------------------------------------------------
; tm_put4x - AX as exactly 4 uppercase hex chars at DI
; in:  AX = value, DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_put4x:
    push ax
    push bx
    push cx
    mov bx, ax
    mov cx, 0x0404              ; CH = digits left, CL = rotate count
.dig:
    rol bx, cl
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 'A' - '0' - 10
.put:
    mov [di], al
    inc di
    dec ch
    jnz .dig
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_col - draw one graph column from the history ring
; in:  BX = column index 0..TM_GW-1; value 0..40 from tm_hist
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
    KCALL gfx_vline
.black:
    or si, si                   ; black: rows 55-v..54 (skip when v=0)
    jz .done
    mov byte [gfx_color], CBLACK
    mov bx, cx
    add bx, TM_GF_Y2
    sub bx, si
    mov dx, cx
    add dx, TM_GF_Y2 - 1
    KCALL gfx_vline
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
    KCALL gfx_vline

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
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, TM_CPU_Y + 7
    KCALL gfx_fill

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
    KCALL sched_mode_get         ; AL = 0 pre-emptive, 1 cooperative
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
    KCALL font_str

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_txt_ram - white-fill the RAM line's rect and draw "RAM uuuK/tttK"
;              (tm_txt_ram_y draws the same line at any y - the memory view
;              puts it at the top, SPEC.md 28)
; in:  [tm_usedk], [tm_totkb]; tm_txt_ram_y: AX = content-relative y
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
tm_txt_ram:
    push ax
    mov ax, TM_RAM_Y
    call tm_txt_ram_y
    pop ax
    ret

tm_txt_ram_y:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [tm_liny], ax
    mov byte [gfx_color], CWHITE
    mov ax, [tm_cx]
    add ax, 6
    mov bx, [tm_cy]
    add bx, [tm_liny]
    mov cx, [tm_cx]
    add cx, TM_RW
    mov dx, [tm_cy]
    add dx, [tm_liny]
    add dx, 7
    KCALL gfx_fill

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
    add dx, [tm_liny]
    mov si, tm_str
    KCALL font_str

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_bar - the RAM bar interior: black for barw px from the left, white rest
; in:  [tm_barw] = 0..TM_GW
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
    KCALL gfx_fill
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
    add cx, TM_RW - 1
    mov dx, [tm_cy]
    add dx, TM_BAR_Y2 - 1
    KCALL gfx_fill
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tm_order - the performance view's row order: built-ins first, then packages,
;            then the free slots (SPEC.md 28)
; in:  the tm_ist/tm_iknd snapshot
; out: tm_ord[0..INST_MAX-1] = instance indices, grouped
; clobbers: nothing (flags)
;
; This is the one place the Task Manager stops mirroring the instance table
; slot for slot. Grouping is what makes the list readable: the built-ins the
; user cannot close sit together under System, everything they launched sits
; below, and the free slots fall to the bottom instead of punching holes
; through the middle. The cost is that a row can move when an instance dies -
; the dock is what keeps the stable slot<->tile mapping (SPEC.md 30), and this
; list never used it for anything, it only inherited it.
; -----------------------------------------------------------------------------
tm_order:
    push ax
    push bx
    push cx
    push si
    xor bx, bx                  ; BX = write cursor into tm_ord
    xor cx, cx                  ; CX = the pass: 0 built-ins, 1 packages, 2 free
.pass:
    xor si, si
.scan:
    mov al, [tm_ist+si]
    or al, al
    jz .isfree
    mov al, [tm_iknd+si]
    and al, KIND_PKG
    jz .isbuilt
    mov al, 1
    jmp short .cls
.isbuilt:
    xor al, al
    jmp short .cls
.isfree:
    mov al, 2
.cls:
    cmp al, cl
    jne .next
    mov ax, si
    mov [tm_ord+bx], al
    inc bx
.next:
    inc si
    cmp si, INST_MAX
    jb .scan
    inc cx
    cmp cx, 3
    jb .pass
    pop si
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
; Row 0 is "System"; rows 1.. walk tm_order's grouping, so the built-ins are
; indented directly under it and the packages follow. The share in tm_pct is
; still indexed by INSTANCE (row = instance + 1 when it was built), which is
; why every read of it goes through [tm_rowi] rather than the loop counter.
; -----------------------------------------------------------------------------
tm_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call tm_order               ; group the rows before laying any of them out
    xor si, si                  ; SI = row index
.row:
    mov ax, si                  ; y1 = cy + TM_ROW_Y + 11*row
    mov cx, TM_ROW_H
    mul cx
    add ax, TM_ROW_Y
    add ax, [tm_cy]
    mov [tm_rowy], ax

    mov byte [gfx_color], CWHITE
    mov bx, ax                  ; white-fill the band (6,y)-(TM_RW,y+7)
    mov dx, ax
    add dx, 7
    mov ax, [tm_cx]
    add ax, 6
    mov cx, [tm_cx]
    add cx, TM_RW
    KCALL gfx_fill

    ; --- build "NAME     ST  nnn%mmmK" in tm_str -----------------------------
    mov di, tm_str
    or si, si
    jnz .inst

    ; row 0: the kernel - task 0 minus everything debited to an instance
    push si
    mov si, tm_s_sys
    xor al, al                  ; the one row that is never indented
    call tm_name9
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
    mov ax, TM_KLOW_KB + TM_KSEG_KB     ; the kernel's fixed reservation:
                                ; low memory entire, and its own segment
                                ; budget (SPEC.md 2/28) - the same two terms
                                ; the RAM line above uses, so the rows still
                                ; sum to the bar
    KCALL mem_kernel_kb         ; + the kernel's OWN heap claims (SPEC.md
                                ; 42.2): the save-under and the back buffer.
                                ; A package's claim belongs to its own row.
    call tm_memcol_kb
    jmp .draw

.inst:
    mov bx, si
    dec bx
    mov bl, [tm_ord+bx]         ; BX = the instance this ROW shows, held to
    xor bh, bh                  ; the end; [tm_rowi] is the same thing for the
    mov [tm_rowi], bl           ; stretches where BX is needed for something
    cmp byte [tm_ist+bx], 0
    je .free

    push si                     ; name: the I_NAME snapshot, 7 chars
    mov ax, bx
    mov cl, 4
    shl ax, cl
    mov si, ax
    add si, tm_inm
    mov al, [tm_iknd+bx]        ; a built-in sits indented under System, a
    not al                      ; package at the top level with it
    and al, KIND_PKG
    call tm_name9
    pop si

    push si                     ; state (SPEC.md 28)
    cmp byte [tm_ist+bx], 2
    je .stdie
    mov cl, [tm_itsk+bx]
    cmp cl, MAX_TASKS           ; 0xFF = no worker: lives in callbacks only
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

    mov bl, [tm_rowi]           ; share, right-aligned in 3, then '%'
    xor bh, bh                  ; (tm_pct is indexed by instance + 1, not by
    inc bx                      ; the display row - see tm_order)
    mov al, [tm_pct+bx]
    call tm_cpucol
    mov bl, [tm_rowi]           ; region bytes (0 for a built-in kind)...
    xor bh, bh
    shl bx, 1
    mov ax, [tm_isz+bx]
    add ax, 1023                ; ...as KB, rounded up; 0 stays 0
    mov cl, 10
    shr ax, cl
    mov dx, ax
    push si
    mov si, bx
    shr si, 1                   ; SI = instance slot
    call tm_inst_claim          ; ...plus everything it holds off the claim
    pop si                      ; heap (SPEC.md 50.5). The MEM column is what
    add ax, dx                  ; this app COSTS, and a package that claimed a
    call tm_memcol_kb0          ; canvas costs far more than its region - the
    jmp .draw                   ; same sum the System row already shows

.free:                          ; free slot: "-        ---   -    -"
    push si
    mov si, tm_s_dash
    xor al, al
    call tm_name9
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
    KCALL font_str
    pop si

    inc si
    cmp si, TM_ROWS
    jae .rdone
    mov ax, [tm_rowy]           ; and stop at the bottom of the FRAME, not the
    add ax, TM_ROW_H            ; screen (tm_ylim_set)
    cmp ax, [tm_ylim]
    jbe .row
.rdone:

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
    mov byte [di], ' '          ; the gap MEM needs: 100% against 335K with
    inc di                      ; nothing between reads as one number
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
    add ax, 1023                ; round up; AX <= 0xB000 here, no overflow
    mov cl, 10
    shr ax, cl
.kb:
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
; tm_memcol_kb - tm_memcol's tail for a caller that already has KB in AX
;                (nonzero) - the System row adds the 150K back buffer,
;                which cannot be expressed as a 16-bit byte count
; in:  AX = KB (> 0), DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_memcol_kb:
    push ax
    push cx
    jmp tm_memcol.kb

; -----------------------------------------------------------------------------
; tm_memcol_kb0 - the same, but 0 KB still means "owns nothing" and prints "-"
; in:  AX = KB, DI = dest
; out: DI advanced by 4
; clobbers: nothing else (internal helper)
; -----------------------------------------------------------------------------
tm_memcol_kb0:
    push ax
    push cx
    or ax, ax
    jnz tm_memcol.kb
    jmp tm_memcol.none

; -----------------------------------------------------------------------------
; tm_name9 - the performance view's NAME field: an optional one-space indent,
;            the name in 7, then padding out to exactly 9 columns
; in:  SI = name, AL = 0 top level / non-zero indented, DI = dest
; out: DI advanced by 9
; clobbers: nothing else (internal helper)
;
; Nine rather than eight because the indent has to come from somewhere and
; the alternative was taking it out of the name - which truncated "TaskMgr"
; to "TaskMg", the one name in the tree that uses all seven.
; -----------------------------------------------------------------------------
tm_name9:
    push ax
    push cx
    mov cl, al
    or al, al
    jz .noind
    mov byte [di], ' '
    inc di
.noind:
    call tm_copy7
    mov byte [di], ' '
    inc di
    or cl, cl
    jnz .out
    mov byte [di], ' '          ; not indented: the column the indent would
    inc di                      ; have taken is padding instead
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
tm_ispt:    resw INST_MAX       ; I_SPTR snapshot (memory view, SPEC.md 28)
tm_iknd:    resb INST_MAX       ; I_KIND snapshot: the Builtins/Packages split
                                ; (SPEC.md 28) needs bit 7, and the CLAIM
                                ; column needs to know which owner word an
                                ; instance's claims carry (SPEC.md 50.2)
tm_mrow:    resw 1              ; the memory view's row cursor
tm_view:    resb 1              ; 0 = performance view, 1 = memory view
tm_inm:     resb INST_MAX * 16  ; I_NAME snapshots
tm_rcyc:    resw TM_ROWS * 2    ; per-ROW cycles: task + instance, dword
tm_total:   resw 2              ; sum of the rows, dword
tm_pct:     resb TM_ROWS        ; per-row share, 0..100
tm_usedk:   resw 1              ; used RAM, KB
tm_barw:    resw 1              ; RAM bar black width, 0..TM_GW
tm_zero_end:

tm_totkb:   resw 1              ; total conventional RAM (int 12h, boot)
tm_cx:      resw 1              ; cached content origin (draw paths only,
tm_cy:      resw 1              ; always under the gfx lock)
tm_ord:     resb INST_MAX       ; tm_order: display row - 1 -> instance slot
tm_rowi:    resb 1              ; ...the one tm_rows is drawing right now
tm_rowy:    resw 1              ; tm_rows/tm_rows_mem scratch: current row y
tm_ylim:    resw 1              ; ...and the lowest row top the frame holds
                                ; (tm_ylim_set): the row lists are fixed-pitch
                                ; and nothing clips a draw to a window
tm_liny:    resw 1              ; tm_txt_ram_y scratch: the line's y
tm_str:     resb 24             ; line-format scratch

section .text
