; =============================================================================
; jop - instance.inc
;
; The instance table (SPEC.md 29): one record per running application
; instance, built-in kind or loaded package. The table is the single source
; of truth for "what is running" - the dock renders from it, the Task
; Manager names tasks through it, and the package-region allocator derives
; occupancy from it.
;
; Concurrency (SPEC.md 29.2, binding): records publish with an I_STATE=1
; byte store as the LAST write (task_spawn precedent); task-less instances
; are freed by the UI task under the gfx lock; task-owned instances are
; freed by task_exit's release byte, atomically with the task slot.
; Lock-held readers only dereference records read as I_STATE=1 during the
; same hold; the Task Manager snapshots under pushf/cli. Only the UI task
; allocates records. Only the owning task destroys a task-owned instance's
; window - the UI task merely hides it and sets the die flag.
; =============================================================================

; --- instance record, 32 bytes (SPEC.md 29.1) ---------------------------------
I_STATE  equ 0                  ; byte: 0 free, 1 live, 2 dying
I_FLAGS  equ 1                  ; byte: bit0 = minimized
I_KIND   equ 2                  ; byte: KIND_*; bit 7 = package
I_TASK   equ 3                  ; byte: task slot (SPEC.md 8), 0xFF = none
I_WIN    equ 4                  ; word: window record ptr
I_SPTR   equ 6                  ; word: builtin state ptr / package base
I_SIZE   equ 8                  ; word: package region bytes; 0 for builtins
I_ICON   equ 10                 ; word: 16x16 icon body ptr, 0 = generic
I_NAME   equ 12                 ; 16 bytes: NUL-terminated copy (<= 15 chars)
I_CYC    equ 28                 ; dword: CPU cycles billed to this instance's
                                ; window callbacks (SPEC.md 8.1), zeroed at
                                ; alloc; lo word first
I_RECSZ  equ 32                 ; power of two: index<<5 via CL
INST_MAX equ 12

KIND_ABOUT   equ 0
KIND_NOTE    equ 1
KIND_CLOCK   equ 2
KIND_BOUNCE  equ 3
KIND_FILES   equ 4
KIND_TASKMGR equ 5
KIND_PKG     equ 0x80           ; bit 7: package instance

; --- kind descriptor table (SPEC.md 29.3) -------------------------------------
KD_TPL   equ 0                  ; word: wm_create template ptr
KD_TASK  equ 2                  ; word: task entry, 0 = task-less
KD_POOL  equ 4                  ; word: state pool base, 0 = stateless
KD_SSIZE equ 6                  ; word: pool stride, bytes per instance
KD_INIT  equ 8                  ; word: init proc or 0 - in: BX = window,
                                ; DI = state ptr (0 if stateless), SI =
                                ; record; preserves all; UI task, no lock,
                                ; window not yet visible
KD_NAME  equ 10                 ; word: NUL name string (copied to I_NAME)
KD_ICON  equ 12                 ; word: 16x16 icon body ptr, 0 = generic
KD_CAP   equ 14                 ; byte: max simultaneous instances
KD_SIZE  equ 16                 ; stride (1 pad byte)

inst_kinds:                     ; indexed by KIND_* * 16; caps are binding
    dw app_tpl_about,  0,               0,             0,   0,                app_ttl_about
    dw 0
    db 1, 0                     ; About: stateless singleton
    dw app_tpl_note,   0,               app_note_pool, 514, app_note_kinit,   app_ttl_note
    dw 0
    db 2, 0                     ; Note Pad: 2 x (len word + 512-byte buffer)
    dw app_tpl_clock,  app_clock_task,  app_clk_pool,  8,   app_clock_kinit,  app_ttl_clock
    dw 0
    db 10, 0                    ; Clock: 10 x 8-byte time state, one task each
    dw app_tpl_bounce, app_bounce_task, app_ball_pool, 8,   app_bounce_kinit, app_ttl_bounce
    dw 0
    db 10, 0                    ; Bounce: 10 x 8-byte ball state, one task each
    dw fm_tpl,         0,               0,             0,   fm_kinit,         fm_ttl
    dw 0
    db 1, 0                     ; Disk: singleton (module-global mount state)
    dw tm_tpl,         tm_task,         0,             0,   tm_kinit,         tm_sname
    dw 0
    db 1, 0                     ; Task Manager: singleton (one sampler task)

; -----------------------------------------------------------------------------
; inst_init - clear the instance table and the deferred-launch word
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
inst_init:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor ax, ax
    mov di, inst_tab
    mov cx, INST_MAX * I_RECSZ
    rep stosb
    mov word [inst_launch], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; inst_ptr - instance index to record pointer
; in:  AL = instance index (0..INST_MAX-1)
; out: DI = inst_tab + index*32
; clobbers: nothing else
; -----------------------------------------------------------------------------
inst_ptr:
    push ax
    push cx
    mov ah, 0
    mov di, ax
    mov cl, 5
    shl di, cl
    add di, inst_tab
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; inst_of_win - find the instance owning a window
; in:  BX = window ptr (record-aligned)
; out: CF = 0, DI = instance record ptr; CF = 1 if the window is unowned
;      (DI undefined). BX preserved.
; clobbers: nothing else
; -----------------------------------------------------------------------------
inst_of_win:
    push ax
    push si
    call wm_ptr2idx             ; AL = window index, AH = 0
    mov si, ax
    mov al, [wm_owner+si]       ; owning instance index, 0xFF = none
    cmp al, 0xFF
    je .none
    call inst_ptr               ; DI = record
    clc
    jmp .out
.none:
    stc
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; inst_find_kind - first LIVE instance of a kind
; in:  AL = kind byte (exact match, bit 7 included)
; out: CF = 0, DI = first record with I_STATE = 1 and I_KIND = AL;
;      CF = 1 if none
; clobbers: nothing else
; -----------------------------------------------------------------------------
inst_find_kind:
    push cx
    mov di, inst_tab
    mov cx, INST_MAX
.scan:
    cmp byte [di+I_STATE], 1
    jne .next
    cmp [di+I_KIND], al
    je .hit
.next:
    add di, I_RECSZ
    loop .scan
    stc
    jmp .out
.hit:
    clc
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; inst_alloc - claim a free record (UI task only; does NOT publish)
; in:  nothing
; out: CF = 0, DI = record with I_FLAGS/I_SPTR/I_SIZE/I_ICON/reserved
;      zeroed; CF = 1 table full
; clobbers: nothing else
; -----------------------------------------------------------------------------
inst_alloc:
    push cx
    mov di, inst_tab
    mov cx, INST_MAX
.scan:
    cmp byte [di+I_STATE], 0
    je .hit
    add di, I_RECSZ
    loop .scan
    stc
    jmp .out
.hit:
    mov byte [di+I_FLAGS], 0
    mov word [di+I_SPTR], 0
    mov word [di+I_SIZE], 0
    mov word [di+I_ICON], 0
    mov word [di+I_CYC], 0      ; a reused record never inherits a dead
    mov word [di+I_CYC+2], 0    ; instance's CPU tally (SPEC.md 8.1)
    clc
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; inst_set_name - copy a name into a record's I_NAME field
; in:  DI = record, SI = source: NUL-terminated OR NUL-padded (a package
;      header's 16-byte name field is safe); at most 15 chars are taken
; out: nothing (all registers preserved); I_NAME zero-filled first, so
;      byte I_NAME+15 is always 0
; -----------------------------------------------------------------------------
inst_set_name:
    push ax
    push cx
    push si
    push di
    add di, I_NAME
    push di
    xor al, al                  ; zero all 16 bytes first
    mov cx, 16
.z:
    mov [di], al
    inc di
    loop .z
    pop di
    mov cx, 15
.copy:
    lodsb                       ; DF=0 per SPEC.md 1
    or al, al
    jz .done
    mov [di], al
    inc di
    loop .copy
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; inst_bind_win - attach a window to a record and claim wm_owner
; in:  DI = record, BX = window ptr
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
inst_bind_win:
    push ax
    push cx
    push si
    mov [di+I_WIN], bx
    mov ax, di                  ; record index = (DI - inst_tab) / 32
    sub ax, inst_tab
    mov cl, 5
    shr ax, cl
    push ax
    call wm_ptr2idx             ; AL = window index, AH = 0
    mov si, ax
    pop ax
    mov [wm_owner+si], al
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; inst_win_owner - inst_of_win with the "no owner" case folded into the result
; in:  BX = window ptr (record-aligned)
; out: DI = instance record ptr, or 0 if the window is unowned. BX preserved.
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
inst_win_owner:
    call inst_of_win
    jnc .out
    xor di, di
.out:
    ret

; -----------------------------------------------------------------------------
; inst_charge - bill a stretch of the running task's CPU time to an instance
;
; in:  DI = instance record (non-zero), DX:AX = stamp from task_cycles
; out: nothing (all registers preserved except flags)
;
; Window callbacks (W_PAINT / W_ONKEY / W_ONCLICK) run on whichever task
; drives the repaint or the event - nearly always the UI task, which owns no
; instance. Without this, every task-less app - About, Note Pad, Disk, and
; EVERY loaded package (SPEC.md 21 - packages cannot spawn) - would read 0%
; forever while its cost showed up as kernel time. task_debit moves the
; cycles instead of copying them, so instance rows and task rows still add
; up to one total (SPEC.md 8.1/28).
;
; Safe against a concurrent teardown: callbacks only ever run under the gfx
; lock, and a task-owned instance destroys its window (clearing wm_owner)
; under that same lock before its record is freed (SPEC.md 29.2), so the
; record named by wm_owner stays live for the whole charged stretch.
; -----------------------------------------------------------------------------
inst_charge:
    push ax
    push dx
    call task_debit             ; DX:AX = elapsed, taken off the task
    add [di+I_CYC], ax
    adc [di+I_CYC+2], dx
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_launch - launch (or front) a built-in app kind (SPEC.md 29.4)
; in:  AL = kind (0..5); UI task only, gfx lock NOT held (takes its own)
; out: CF = 1 launch failed (instance/window/task table full), CF = 0 done
; clobbers: nothing else (flags only)
;
; At the kind's cap, "launch" fronts + un-minimizes the existing live
; instance (singleton semantics); if only dying instances exist, it fails -
; the user retries after a task period. Otherwise: allocate a record, pick
; the first free state-pool slot (a slot is held iff a same-kind record
; with I_STATE != 0 points at it), create the window from the kind's
; template cascaded (+16,+16) per pool slot, fill + bind + init, publish
; I_STATE=1 (LAST record write), spawn the kind's task (rollback on a full
; task table: unpublish, destroy the window), then show the window.
; -----------------------------------------------------------------------------
app_launch:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov [inst_knd], al
    push ax                     ; SI = kind descriptor
    push cx
    mov ah, 0
    mov si, ax
    mov cl, 4
    shl si, cl
    add si, inst_kinds
    pop cx
    pop ax

    ; --- cap check: count records of this kind with I_STATE != 0 ------------
    xor cx, cx                  ; CL = count
    mov di, inst_tab
    mov dx, INST_MAX
.count:
    cmp byte [di+I_STATE], 0
    je .cnext
    cmp al, [di+I_KIND]
    jne .cnext
    inc cl
.cnext:
    add di, I_RECSZ
    dec dx
    jnz .count
    cmp cl, [si+KD_CAP]
    jb .alloc

    call inst_find_kind         ; at cap: front the live instance instead
    jc .fail                    ; only dying ones exist: retry later
    call gfx_lock
    and byte [di+I_FLAGS], 0xFE ; un-minimize
    mov bx, [di+I_WIN]
    call wm_show
    call gfx_unlock
    jmp .ok

.alloc:
    call inst_alloc
    jc .fail
    mov bp, di                  ; BP = our record for the duration

    ; --- state-pool slot pick (SPEC.md 29.4) ---------------------------------
    mov word [inst_slot], 0
    cmp word [si+KD_POOL], 0
    je .tpl                     ; stateless: I_SPTR stays 0
.pslot:
    mov ax, [si+KD_SSIZE]
    mul word [inst_slot]        ; AX = slot * stride (pools are small)
    add ax, [si+KD_POOL]        ; AX = candidate state ptr
    mov di, inst_tab            ; held by any same-kind record in use?
    mov cx, INST_MAX
.pscan:
    cmp byte [di+I_STATE], 0
    je .pnext
    mov bl, [inst_knd]
    cmp bl, [di+I_KIND]
    jne .pnext
    cmp ax, [di+I_SPTR]
    je .ptaken
.pnext:
    add di, I_RECSZ
    loop .pscan
    mov [bp+I_SPTR], ax         ; free candidate found
    jmp .tpl
.ptaken:
    inc word [inst_slot]
    mov cl, [si+KD_CAP]
    xor ch, ch
    cmp [inst_slot], cx
    jb .pslot
    jmp .fail                   ; cannot happen (count < cap); fail safe

.tpl:
    ; --- window from the template, cascaded per pool slot --------------------
    push si
    mov si, [si+KD_TPL]
    mov di, inst_tplbuf
    push ds
    pop es
    mov cx, 8
    rep movsw                   ; 16-byte template copy
    pop si
    mov ax, [inst_slot]         ; cascade: (+16,+16) per pool slot so
    mov cl, 4                   ; instances don't stack exactly
    shl ax, cl
    add [inst_tplbuf], ax       ; x
    add [inst_tplbuf+2], ax     ; y
    push si
    mov si, inst_tplbuf
    call wm_create              ; BX = window (hidden), CF = table full
    pop si
    jc .fail                    ; record was never published: still free

    ; --- fill + bind + init --------------------------------------------------
    mov al, [inst_knd]
    mov [bp+I_KIND], al
    mov byte [bp+I_TASK], 0xFF
    mov ax, [si+KD_ICON]
    mov [bp+I_ICON], ax
    push si
    mov si, [si+KD_NAME]
    mov di, bp
    call inst_set_name
    pop si
    mov di, bp
    call inst_bind_win          ; I_WIN + wm_owner

    mov ax, [si+KD_INIT]
    or ax, ax
    jz .noinit
    mov di, [bp+I_SPTR]         ; DI = state ptr (0 if stateless)
    push si
    mov si, bp                  ; SI = record (KD_INIT contract)
    call ax
    pop si
.noinit:

    mov byte [bp+I_STATE], 1    ; publish - the LAST record write

    ; --- task, if the kind has one -------------------------------------------
    mov ax, [si+KD_TASK]
    or ax, ax
    jz .show
    mov dx, bp                  ; DX = instance index (the spawn argument)
    sub dx, inst_tab
    mov cl, 5
    shr dx, cl
    call task_spawn             ; in AX, DX; out CF / AL = slot
    jc .rollback
    mov [bp+I_TASK], al

.show:
    call gfx_lock
    mov bx, [bp+I_WIN]
    call wm_show
    call gfx_unlock
.ok:
    clc
    jmp .out

.rollback:                      ; task table full: undo the half-built launch
    mov byte [bp+I_STATE], 0    ; unpublish first (readers skip state 0)
    call gfx_lock
    mov bx, [bp+I_WIN]
    call wm_destroy             ; also resets the wm_owner entry
    call gfx_unlock
.fail:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; app_close_win - quit the instance owning a window (SPEC.md 29.4)
; in:  BX = window ptr; caller HOLDS the gfx lock; UI task only
; out: nothing (all registers preserved)
;
; Unowned window: plain wm_hide (defensive fallback). Task-less instance:
; synchronous teardown under the held lock - destroy the window, free the
; record (for a package instance that final store frees its region,
; SPEC.md 29.2 rule 7). Task-owned instance: set the die flag and hide for
; instant feedback; the task notices at its next wake and tears itself
; down via inst_task_die.
; -----------------------------------------------------------------------------
app_close_win:
    push di
    call inst_of_win            ; DI = record; CF = unowned
    jc .hide
    cmp byte [di+I_STATE], 1
    jne .hide                   ; already dying: re-hide, idempotent
    cmp byte [di+I_TASK], 0xFF
    jne .tasked
    mov byte [di+I_STATE], 2    ; dying: lock-held readers skip it from here
    call wm_destroy             ; frees the wm slot + wm_owner, repaints
    mov word [di+I_WIN], 0
    mov byte [di+I_STATE], 0    ; record (and any package region) free
    jmp .out
.tasked:
    mov byte [di+I_STATE], 2    ; the die flag (SPEC.md 14/29)
.hide:
    call wm_hide                ; instant visual feedback + repaint
.out:
    pop di
    ret

; -----------------------------------------------------------------------------
; inst_minimize - send a window's instance to the dock
; in:  BX = window ptr; caller holds the gfx lock
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
inst_minimize:
    push di
    call inst_of_win
    jc .hide                    ; unowned: plain hide
    or byte [di+I_FLAGS], 1
.hide:
    call wm_hide
    pop di
    ret

; -----------------------------------------------------------------------------
; inst_restore - bring a minimized instance back
; in:  DI = instance record (I_STATE = 1); caller holds the gfx lock
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
inst_restore:
    push bx
    and byte [di+I_FLAGS], 0xFE
    mov bx, [di+I_WIN]
    call wm_show
    pop bx
    ret

; -----------------------------------------------------------------------------
; inst_task_die - teardown for a task-owned instance (SPEC.md 29.4)
; in:  DI = the CURRENT task's instance record (I_STATE = 2); gfx lock NOT
;      held
; out: NEVER RETURNS - ends in task_exit
; -----------------------------------------------------------------------------
inst_task_die:
    call gfx_lock
    mov bx, [di+I_WIN]
    call wm_destroy             ; frees the wm slot + wm_owner, repaints
    mov word [di+I_WIN], 0
    call gfx_unlock
    mov bx, di                  ; I_STATE is offset 0: the release byte
    jmp task_exit

; -----------------------------------------------------------------------------
; inst_launch_post - post a deferred launch request (SPEC.md 29.4)
; in:  AL = kind; callable under the gfx lock (one atomic word store).
;      Drained by ui_task step 3 (SPEC.md 13); rapid posts coalesce.
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
inst_launch_post:
    push ax
    mov ah, 0
    inc ax
    mov [inst_launch], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

inst_tab:    resb 384           ; INST_MAX x I_RECSZ (12 x 32; literal per
                                ; the no-forward-ref-in-bss rule)
inst_launch: resw 1             ; deferred launch: 0 = none, else kind + 1
inst_knd:    resb 1             ; app_launch scratch: kind being launched
inst_slot:   resw 1             ; app_launch scratch: state-pool slot
inst_tplbuf: resb 16            ; app_launch scratch: cascaded template copy

section .text
