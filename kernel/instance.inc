; =============================================================================
; os8088 - instance.inc
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
;
; A package instance can be on EITHER side of that split since SPEC.md 20.6:
; it is born task-less, and a window callback may claim it one worker task
; through inst_pkg_spawn. Once it has one, its region is freed by
; task_exit's release byte from the worker, not by the UI task.
; =============================================================================

; --- instance record, 32 bytes (SPEC.md 29.1) ---------------------------------
I_STATE  equ 0                  ; byte: 0 free, 1 live, 2 dying
I_FLAGS  equ 1                  ; byte: bit0 = minimized
I_KIND   equ 2                  ; byte: KIND_*; bit 7 = package
I_TASK   equ 3                  ; byte: task slot (SPEC.md 8), 0xFF = none
I_WIN    equ 4                  ; word: window record ptr
I_SPTR   equ 6                  ; word: builtin state ptr / package region
                                ; SEGMENT (v3, SPEC.md 29.1 - also the
                                ; segment its callbacks dispatch at, 11)
I_SIZE   equ 8                  ; word: package region PARAGRAPHS (v3, <=
                                ; PKG_MAX_PARA); 0 for builtins
I_ICON   equ 10                 ; word: 16x16 icon body ptr, 0 = generic
I_NAME   equ 12                 ; 16 bytes: NUL-terminated copy (<= 15 chars)
I_CYC    equ 28                 ; dword: CPU cycles billed to this instance's
                                ; window callbacks (SPEC.md 8.1), zeroed at
                                ; alloc; lo word first
I_RECSZ  equ 32                 ; power of two: index<<5 via CL
INST_MAX equ 12

KIND_ABOUT   equ 0
KIND_CLOCK   equ 1              ; (Note Pad was kind 1 until it became the
KIND_BOUNCE  equ 2              ;  NOTEPAD package - SPEC.md 27)
KIND_FILES   equ 3
KIND_TASKMGR equ 4
KIND_CTRL    equ 5
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
KD_WFLAG equ 15                 ; byte: extra W_FLAGS bits OR-ed into the new
                                ; window after wm_create (SPEC.md 29.3) -
                                ; feature bits only (WF_SIZABLE), never 0/1
KD_SIZE  equ 16                 ; stride

inst_kinds:                     ; indexed by KIND_* * 16; caps are binding
    dw app_tpl_about,  0,               0,             0,   0,                app_ttl_about
    dw 0
    db 1, 0                     ; About: stateless singleton
    dw app_tpl_clock,  app_clock_task,  app_clk_pool,  8,   app_clock_kinit,  app_ttl_clock
    dw 0
    db 10, 0                    ; Clock: 10 x 8-byte time state, one task each
    dw app_tpl_bounce, app_bounce_task, app_ball_pool, 8,   app_bounce_kinit, app_ttl_bounce
    dw 0
    db 10, 0                    ; Bounce: 10 x 8-byte ball state, one task each
    dw fm_tpl,         0,               fm_pool,       16,  fm_kinit,         fm_ttl
    dw 0
    db VIEW_SLOTS, WF_SIZABLE   ; Disk: FOUR windows, each with its own
                                ; 16-byte state block and its own VIEW_SEG
                                ; listing cache; resizable (SPEC.md 22/22.1).
                                ; The cap, VIEW_SLOTS and fm_pool's size are
                                ; one number wearing three hats.
    dw tm_tpl,         tm_task,         0,             0,   tm_kinit,         tm_sname
    dw 0
    db 1, 0                     ; Task Manager: singleton (one sampler task)
    dw cp_tpl,         0,               0,             0,   0,                cp_sname
    dw 0
    db 1, 0                     ; Control Panel: stateless task-less singleton

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
    mov di, inst_icons          ; the icon cache too (SPEC.md 29.5): a slot
    mov cx, INST_MAX * 64       ; is only ever read through a live record's
    rep stosb                   ; I_ICON, but a zeroed cache draws as the
                                ; blank-body icon if a bug ever gets there
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
; instance. Without this, every task-less app - About, Disk, and every
; loaded package that owns no worker (SPEC.md 20.6) - would read 0%
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

; =============================================================================
; Callback dispatch (SPEC.md 11, binding since v3)
;
; W_PAINT / W_ONKEY / W_ONCLICK, the AM_ONCMD menu handler, the fdlg
; completion proc and the loader's entry call are all near OFFSETS - the
; window record did not grow - and the SEGMENT comes from the owner at
; every dispatch site. The three routines below are the ONE implementation
; of that rule: inst_cb_seg answers the segment question, and the two call
; thunks do the transfer - near for KERNEL_SEG (the callee `ret`s, exactly
; as before v3), far with the callee's DS switched for a package (the
; callee `retf`s, SPEC.md 20.2). A wrong answer here is a call into
; hyperspace, which is why no site open-codes any of it.
;
; The thunks take the target on the STACK (seg above off) rather than in
; registers because no register is free at every site - W_ONKEY owns AX,
; W_ONCLICK owns CX/DX, AM_ONCMD owns AX and BX - and because the stack
; makes them re-entrant by construction: a package's W_ONCLICK that calls
; an API slot that repaints (wm_show -> wm_paint_all -> other windows'
; W_PAINT) nests dispatches, and a static scratch far pointer would need
; a critical section the callee's run time forbids.
; =============================================================================

; -----------------------------------------------------------------------------
; inst_cb_seg - resolve a callback's code/data segment from its owner
; in:  DI = owning instance record ptr, or 0 (an UNOWNED window);
;      BX = the window ptr, record-aligned - consulted ONLY when DI = 0
;      (0 = no window at all: a kernel context)
; out: AX = the segment: the record's I_SPTR when DI names a PACKAGE record
;      (I_KIND bit 7 - since v3 I_SPTR is that package's region SEGMENT,
;      SPEC.md 29.1); KERNEL_SEG for a built-in's record (its I_SPTR is a
;      kernel state ptr, not a segment); for DI = 0, the window's wm_wseg
;      CREATOR stamp (SPEC.md 11) - KERNEL_SEG for every kernel-created
;      window (the file dialog's included), the creating package's segment
;      for a window that came through osapi_w_wm_create and was never (or
;      not yet) bound by the loader. "Unowned" stopped implying "kernel
;      procs" the moment packages could wm_create through the API: an
;      org-0 paint offset dispatched near at KERNEL_SEG is a wild call
;      into kernel text, which this fallback is what prevents.
; clobbers: nothing else (flags)
;
; Valid under the same conditions the DI answer itself is (SPEC.md 29.2):
; the dispatch sites all hold the gfx lock, a package record's I_SPTR
; never changes between publish and free, and wm_wseg is written only by
; the UI task (wm_create / wm_destroy / the create wrapper).
; -----------------------------------------------------------------------------
inst_cb_seg:
    mov ax, KERNEL_SEG
    or di, di
    jz .unowned
    test byte [di+I_KIND], KIND_PKG
    jz .out
    mov ax, [di+I_SPTR]
.out:
    ret
.unowned:
    or bx, bx                   ; no window either: kernel context
    jz .out
    push si
    call wm_ptr2idx             ; AL = window index, AH = 0 (every dispatch
    mov si, ax                  ; site's BX is a kernel-produced, aligned
    shl si, 1                   ; window ptr - SPEC.md 11)
    mov ax, [wm_wseg+si]
    pop si
    ret

; -----------------------------------------------------------------------------
; inst_cb_call - transfer to a callback at a resolved segment (SPEC.md 11)
; in:  two words pushed by the caller, seg first: the target SEGMENT
;      (inst_cb_seg's answer, or the loader's fresh allocation) above the
;      target OFFSET; every contract register already loaded - all GP
;      registers pass through to the callee untouched
; out: whatever the callee answers - registers AND flags pass straight
;      back (nothing on either return path touches flags, so a CF answer
;      like the entry proc's survives, the SPEC.md 20.3 stub argument
;      pointing the other way). The two argument words are consumed.
; clobbers: nothing of its own (BP is saved around the frame)
;
; KERNEL_SEG -> the near call it always was: DS/ES untouched, the callee
; returns with `ret`. Anything else is a package segment: DS = ES = that
; segment around a far call (SPEC.md 20.2 - CS = DS = ES = the package
; segment on entry, `retf` to return), the caller's DS/ES restored after.
; The far pointer is BUILT IN PLACE by the argument order: off sits at
; [bp+4] with seg at [bp+6], which is exactly the m16:16 that
; `call far [bp+4]` wants - and BP-relative operands address SS, which IS
; the stack on every task (SS = LOW_SEG, SPEC.md 1), the one place the
; SS != DS footgun works for us instead of against.
; -----------------------------------------------------------------------------
inst_cb_call:
    push bp
    mov bp, sp                  ; [bp+2] = ret, [bp+4] = off, [bp+6] = seg
    cmp word [bp+6], KERNEL_SEG
    je .near
    push ds
    push es
    mov ds, [bp+6]              ; the callee's world: DS = ES = CS = its
    mov es, [bp+6]              ; own segment (SPEC.md 20.1)
    call far [bp+4]             ; m16:16 at SS:[bp+4] = off, [bp+6] = seg
    pop es
    pop ds
    pop bp
    ret 4                       ; consume off + seg; RET imm touches no flags
.near:
    call [bp+4]                 ; near indirect through SS - the kernel leg,
                                ; `ret` and all, exactly as before v3
    pop bp
    ret 4

; -----------------------------------------------------------------------------
; inst_cb_call_es - inst_cb_call, but the CALLER's ES is delivered to the
;                   callee instead of the package segment
; in/out: exactly inst_cb_call; the far leg switches DS only
;
; For the one callback whose contract carries an ES:DI pointer INTO the
; kernel: the fdlg completion proc receives the chosen name as ES:DI with
; ES = KERNEL_SEG (SPEC.md 38.6) - fdlg_commit sets ES before pushing the
; target. Everything else about the transfer is identical, the near leg
; included (a kernel requester reads the name flat, ES = DS there).
; -----------------------------------------------------------------------------
inst_cb_call_es:
    push bp
    mov bp, sp                  ; [bp+2] = ret, [bp+4] = off, [bp+6] = seg
    cmp word [bp+6], KERNEL_SEG
    je .near
    push ds
    mov ds, [bp+6]              ; DS = the package segment; ES stays the
                                ; caller's (KERNEL_SEG at the fdlg site)
    call far [bp+4]
    pop ds
    pop bp
    ret 4
.near:
    call [bp+4]
    pop bp
    ret 4

; -----------------------------------------------------------------------------
; app_launch - launch (or front) a built-in app kind (SPEC.md 29.4)
; in:  AL = kind (0..6); UI task only, gfx lock NOT held (takes its own)
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
    mov [ds:bp+I_SPTR], ax      ; free candidate found
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
    mov al, [si+KD_WFLAG]       ; the kind's feature bits (WF_SIZABLE) join
    mov ah, 0                   ; W_FLAGS right after create (SPEC.md 29.3)
    or [bx+W_FLAGS], ax

    ; --- fill + bind + init --------------------------------------------------
    mov al, [inst_knd]
    mov [ds:bp+I_KIND], al
    mov byte [ds:bp+I_TASK], 0xFF
    mov ax, [si+KD_ICON]
    mov [ds:bp+I_ICON], ax
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
    mov di, [ds:bp+I_SPTR]      ; DI = state ptr (0 if stateless)
    push si
    mov si, bp                  ; SI = record (KD_INIT contract)
    call ax
    pop si
.noinit:

    mov byte [ds:bp+I_STATE], 1    ; publish - the LAST record write

    ; --- task, if the kind has one -------------------------------------------
    mov ax, [si+KD_TASK]
    or ax, ax
    jz .show
    mov dx, bp                  ; DX = instance index (the spawn argument)
    sub dx, inst_tab
    mov cl, 5
    shr dx, cl
    mov cx, KERNEL_SEG          ; a built-in's task runs in the kernel
                                ; segment (SPEC.md 8: CX = the frame's
                                ; DS/ES/CS)
    call task_spawn             ; in AX, CX, DX; out CF / AL = slot
    jc .rollback
    mov [ds:bp+I_TASK], al

.show:
    call gfx_lock
    mov bx, [ds:bp+I_WIN]
    call wm_show
    call gfx_unlock
.ok:
    clc
    jmp .out

.rollback:                      ; task table full: undo the half-built launch
    mov byte [ds:bp+I_STATE], 0    ; unpublish first (readers skip state 0)
    call gfx_lock
    mov bx, [ds:bp+I_WIN]
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
; record. Task-owned instance: set the die flag and hide for instant
; feedback; the task notices at its next wake and tears itself down via
; inst_task_die. For a PACKAGE instance the record store frees its region
; too (SPEC.md 29.2 rule 7) - on either branch: a package that took a
; worker (SPEC.md 20.6) reaches the second one, and there the freeing store
; is task_exit's release byte instead.
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
    call snd_release_rec        ; free every sound grant before the record
                                ; goes (SPEC.md 29.4/34.3)
    call xm_release_rec         ; ...and every extended-memory block, or the
                                ; claim is stranded for the session (41.5)
    call wm_destroy             ; frees the wm slot + wm_owner, repaints
    mov word [di+I_WIN], 0
    test byte [di+I_KIND], KIND_PKG
    jz .swept                   ; a package may have wm_created windows the
    push ax                     ; loader never bound (SPEC.md 11/20.2):
    mov ax, [di+I_SPTR]         ; sweep them by their wm_wseg creator stamp
    call wm_destroy_seg         ; BEFORE the region below is freed for
    pop ax                      ; reuse - their procs live in it
.swept:
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
;
; Reached from Clock's and Bounce's own loops (SPEC.md 14) and, for a
; package that took a worker, from inst_pkg_alive below (SPEC.md 20.6).
; -----------------------------------------------------------------------------
inst_task_die:
    call snd_release_rec        ; free every sound grant before the record
                                ; goes (SPEC.md 29.4/34.3)
    call xm_release_rec         ; ...and every extended-memory block (41.5)
    call gfx_lock
    mov bx, [di+I_WIN]
    call wm_destroy             ; frees the wm slot + wm_owner, repaints
    mov word [di+I_WIN], 0
    test byte [di+I_KIND], KIND_PKG
    jz .swept                   ; the app_close_win sweep, same reason
    mov ax, [di+I_SPTR]         ; (SPEC.md 11/20.2): unbound windows die
    call wm_destroy_seg         ; with the instance, before task_exit frees
.swept:                         ; the region they point into
    call gfx_unlock
    mov bx, di                  ; I_STATE is offset 0: the release byte
    jmp task_exit

; =============================================================================
; Package worker tasks (SPEC.md 20.6)
;
; The two API-table targets that let a LOADED PACKAGE own exactly one
; background task - the first time package code is pre-empted against
; anything but the UI task. Everything in this pair's design falls out of
; one property: while a worker exists, the ONLY code that can free its
; instance record is the worker's own task_exit. app_close_win reads
; I_TASK != 0xFF and takes the die-flag path, which frees nothing; the
; task-less path that would free the record (and with it the package
; region, SPEC.md 29.2 rule 7) is unreachable for a worker-owning instance.
; =============================================================================

; -----------------------------------------------------------------------------
; inst_wchk - is BX a plausible window record pointer? (module-internal)
;
; in:  BX = a window ptr that came from a PACKAGE (never trusted)
; out: CF = 0 BX is inside wm_wins and record-aligned; CF = 1 it is not
; clobbers: nothing else (flags)
;
; The two routines below are the first API slots that hand a package-supplied
; pointer to inst_of_win, and inst_of_win is not defensive: wm_ptr2idx's
; `div cl` FAULTS (int 0, no handler installed) once BX - wm_wins exceeds
; 255*WIN_SIZE, and the `wm_owner+si` load that follows is unbounded. The
; loader already refuses to wm_destroy an aborting entry's BX without exactly
; this test (SPEC.md 21 step 9) - this is that test, factored out.
; -----------------------------------------------------------------------------
inst_wchk:
    push ax
    push cx
    mov ax, bx
    sub ax, wm_wins
    jc .bad                     ; below the window table
    cmp ax, MAX_WIN * WIN_SIZE
    jae .bad                    ; past its end
    mov cl, WIN_SIZE            ; AX <= 239 here, so the 8-bit div is safe
    div cl                      ; AH = offset mod WIN_SIZE
    test ah, ah
    jnz .bad                    ; not record-aligned
    clc
    jmp short .out
.bad:
    stc
.out:
    pop cx                      ; POP touches no flags: CF survives
    pop ax
    ret

; -----------------------------------------------------------------------------
; inst_pkg_spawn - give a package instance its one worker task (SPEC.md 20.6)
;
; in:  AX = the worker's entry OFFSET in the package's own segment (org 0,
;           SPEC.md 20.1/20.6)
;      BX = the package's own window ptr (what OSAPI_WM_CREATE returned)
; out: CF = 1 refused, nothing created; CF = 0 and AL = the task slot
; clobbers: AL (an output) and the flags - every other register preserved,
;           AH included
;
; Context (binding): the caller MUST HOLD THE GFX LOCK. Every sanctioned
; site does - W_PAINT / W_ONKEY / W_ONCLICK (SPEC.md 11), an AM_ONCMD menu
; handler (SPEC.md 12.2) and a file-dialog completion proc (SPEC.md 38.6)
; are all dispatched under it. Holding it is not a convenience: it is what
; excludes app_close_win, which also runs under the lock and which would
; otherwise be free to read I_TASK = 0xFF, take its task-less path and free
; the record while the brand-new worker is starting up inside the region.
; Nothing on this path takes a lock or yields, so a held lock is safe.
; (Exclusion against another SPAWNER is task_spawn's own IF=0 window, not
; this lock - W_PAINT is dispatched by whichever task drives the repaint,
; while app_launch spawns lock-free on the UI task. See SPEC.md 8.)
;
; OWNERSHIP FENCE (SPEC.md 20.6), two halves. BX is package input, so
; finding a live record is not enough. Half one: the record found must be
; a PACKAGE instance whose own region contains the entry in AX -
; `AX >> 4 < I_SIZE`, compared in PARAGRAPHS precisely so a full-segment
; region cannot wrap the compare (SPEC.md 2.5) - so the frame's CS/DS/ES
; are seeded from the NAMED record's I_SPTR with the offset bounded by its
; size, and task_spawn can never iret into kernel code or past that
; region. Half two: the record found must BE the caller's own instance.
; At org 0 every package's offsets start at 0x20 and every region is at
; least 32 paragraphs, so the offset test alone passes against essentially
; ANY package record - v2's distinct bases proved identity for free, v3's
; shared org cannot - and a package handing a stranger's (or sibling's)
; window would put a task in the STRANGER's segment and flip the
; stranger's record onto the die-flag close path, leaking record and
; region for the session. The identity the kernel already holds is the
; DISPATCH stamp: every sanctioned call site is a dispatched callback,
; and snd_disp_set stamped its instance index into [snd_inst] (SPEC.md
; 34.3) - the record's index must match it, full stop.
;
; NOT callable from the package entry proc: the loader publishes I_STATE=1
; and binds wm_owner only AFTER the entry returns (SPEC.md 21 step 9), so
; inst_of_win would find nothing and this would answer CF=1.
; -----------------------------------------------------------------------------
inst_pkg_spawn:
    push bx
    push cx
    push dx
    push di
    call inst_wchk              ; a package pointer: bound it before any deref
    jc .fail
    call inst_of_win            ; DI = the record owning BX; CF = unowned
    jc .fail
    cmp byte [di+I_STATE], 1    ; live only - a dying instance spawns nothing
    jne .fail
    cmp byte [di+I_TASK], 0xFF  ; one worker per instance: I_TASK is a single
    jne .fail                   ; byte and is never cleared once set
    test byte [di+I_KIND], KIND_PKG ; the fence: a package instance...
    jz .fail
    push ax                     ; ...whose own region contains the entry:
    mov cl, 4                   ; AX >> 4 < I_SIZE, in PARAGRAPHS - a
    shr ax, cl                  ; full-segment region (I_SIZE up to
    cmp ax, [di+I_SIZE]         ; PKG_MAX_PARA) cannot wrap this compare
    pop ax                      ; (POP touches no flags: CMP's answer is
    jae .fail                   ; still live at the JAE)
    mov bh, ah                  ; bank the caller's AH: task_spawn rebuilds
                                ; the whole of AX, CX carries the segment
                                ; below, and BX - pushed at entry - is done
                                ; serving as the window ptr
    mov dx, di                  ; DX = instance index = (rec - inst_tab) >> 5,
    sub dx, inst_tab            ; the app_launch idiom (SPEC.md 29.4). It
    mov cl, 5                   ; lands in the worker's DX and its DL in
    shr dx, cl                  ; T_INST, so the Task Manager attributes the
                                ; task to this instance (SPEC.md 8/28)
    cmp dl, [snd_inst]          ; the fence's OTHER half (SPEC.md 20.6): the
    jne .fail                   ; record found must BE the instance whose
                                ; callback is running. Every sanctioned call
                                ; site is a dispatched callback, and the
                                ; dispatch stamped its instance index into
                                ; [snd_inst] (SPEC.md 34.3) - without this,
                                ; at org 0 the offset test above passes for
                                ; essentially ANY package record (every
                                ; region starts at 0x20), and a stranger's
                                ; window would flip the STRANGER's I_TASK,
                                ; putting a foreign task in its segment and
                                ; its record on the die-flag close path
                                ; nobody will ever complete
    mov cx, [di+I_SPTR]         ; the worker's DS/ES/CS = the instance's own
                                ; segment (SPEC.md 8/20.6): the frame can
                                ; only iret into THIS record's region
    call task_spawn             ; in AX = entry, CX = segment, DX = arg;
                                ; out CF, AL = slot
    mov ah, bh                  ; caller's AH back - MOV touches no flags, so
                                ; task_spawn's CF is still the live answer
    jc .out                     ; task table full: nothing was created
    mov [di+I_TASK], al         ; the record now owns the worker
    clc
    jmp short .out
.fail:
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; inst_pkg_alive - the worker's liveness check, and its only way to die
;                  (SPEC.md 20.6)
;
; in:  BX = the package's own window ptr; the gfx lock must NOT be held
; out: returns with EVERY register AND the flags preserved if the instance
;      is still live - otherwise NEVER RETURNS: it tears the instance down
;      through inst_task_die (gfx_lock, wm_destroy, gfx_unlock, task_exit
;      with BX = the record, which frees the record and with it the package
;      region inside one IF=0 window)
;
; Called by the worker at least once per outer-loop iteration. gfx_lock is
; NOT reentrant, and inst_task_die takes it - calling this while holding the
; lock deadlocks the drawing of the whole machine (SPEC.md 7).
;
; The liveness QUESTION is asked of BX, per the ABI. The teardown IDENTITY
; is taken from the running task's T_INST instead, and that split is
; deliberate: BX may name nothing at all (a destroyed window reads as
; wm_owner = 0xFF), and answering "no record" by simply exiting would leave
; I_TASK naming a freed slot with the instance record and the package region
; leaked for the rest of the session - the exact failure this routine exists
; to prevent. T_INST is authoritative: task_spawn stored it from
; inst_pkg_spawn's own DL and nothing else ever writes it.
;
; The one caller T_INST cannot speak for is the UI TASK, whose T_INST is a
; hard 0xFF (sched_init) - so a package calling this from a window callback
; with a stale BX would reach .orphan and task_exit slot 0. The UI task
; never sleeps and never exits: that is the invariant task_exit's own
; "resume the outgoing task" fallback rests on, and killing it takes the
; event ladder, menu tracking and every future gfx_unlock with it. Calling
; ALIVE off the worker is a contract violation; the bounded answer to it is
; to return, which is what the guard at .die does.
; -----------------------------------------------------------------------------
inst_pkg_alive:
    pushf                       ; the contract includes the flags: this call
    push di                     ; sits inside the worker's inner loop
    call inst_wchk
    jc .die
    call inst_of_win            ; DI = the record owning BX; CF = unowned
    jc .die
    cmp byte [di+I_STATE], 1    ; 2 = close requested, 0 = already gone
    jne .die
.live:
    pop di
    popf
    ret

.die:                           ; NEVER RETURNS - the frame above is abandoned
    cmp byte [sch_cur], 0       ; ...unless we are the UI task, which must
    je .live                    ; never exit (see the banner)
    mov bl, [sch_cur]           ; our OWN record, via the task record's
    xor bh, bh                  ; T_INST (T_SIZE = 8; the snd.inc idiom)
    mov cl, 3
    shl bx, cl
    mov al, [bx+sch_tasks+T_INST]
    cmp al, INST_MAX
    jae .orphan                 ; no record of ours at all - cannot happen
    call inst_ptr               ; DI = inst_tab + index*32
    cmp word [di+I_WIN], 0
    je .winless                 ; nothing to destroy; wm_destroy with BX = 0
                                ; would zero the cold-entry jmp at 1000:0000
    jmp inst_task_die           ; never returns
.winless:
    call snd_release_rec        ; the inst_task_die order, minus the window
    call xm_release_rec
    mov bx, di                  ; no window, but the record is real: release
    jmp task_exit               ; it anyway (I_STATE is offset 0), or its
                                ; region, dock tile and tm row leak for the
                                ; session - the failure this routine exists
                                ; to prevent
.orphan:
    xor bx, bx                  ; no release byte - the sbl_refill_task
    jmp task_exit               ; precedent (SPEC.md 34.5); never returns

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
inst_icons:  resb 768           ; INST_MAX x 64: the per-instance embedded-
                                ; icon cache (SPEC.md 29.5). The loader
                                ; copies a package's icon body out of its
                                ; segment into slot i and points I_ICON at
                                ; it (SPEC.md 21 step 9), so the dock's
                                ; read stays a plain kernel DS pointer -
                                ; the "side table, not a fatter record"
                                ; answer to SPEC.md 20.7 rule 2. Zeroed by
                                ; inst_init
inst_launch: resw 1             ; deferred launch: 0 = none, else kind + 1
inst_knd:    resb 1             ; app_launch scratch: kind being launched
inst_slot:   resw 1             ; app_launch scratch: state-pool slot
inst_tplbuf: resb 16            ; app_launch scratch: cascaded template copy

section .text
