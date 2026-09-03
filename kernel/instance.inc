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
I_SPTR   equ 6                  ; word: builtin state ptr / package base
I_SIZE   equ 8                  ; word: package region bytes; 0 for builtins
I_ICO_PKG equ 1                 ; the I_ICON value meaning "in this
                                ; instance's own region, at LD_HDR_SIZE": 1
                                ; can never be a real .text body pointer, the
                                ; kernel image starting well above it
I_ICON   equ 10                 ; word: 16x16 icon body ptr, 0 = generic,
                                ; I_ICO_PKG = "in this instance's own region"
                                ; - reached through inst_icon_ptr, never
                                ; dereferenced directly
I_NAME   equ 12                 ; 16 bytes: NUL-terminated copy (<= 15 chars)
I_CYC    equ 28                 ; dword: CPU cycles billed to this instance's
                                ; window callbacks (SPEC.md 8.1), zeroed at
                                ; alloc; lo word first
I_RECSZ  equ 32                 ; power of two: index<<5 via CL
INST_MAX equ 12                 ; mirrored in apps/os88api.inc (SPEC.md 20.9
                                ; buffer sizing) - change together, rebuild
                                ; every .o88

INST_FNSZ equ 13                ; the per-instance Standard File name below:
                                ; fdlg.inc's FD_NAMEMAX (12) plus the NUL, and
                                ; asserted against it there rather than here,
                                ; because this file is included first

KIND_ABOUT   equ 0
KIND_TIMER   equ 1              ; (Note Pad was kind 1 until it became the
KIND_BOUNCE  equ 2              ;  NOTEPAD package - SPEC.md 27; kind 1 was
                                ;  the Clock until it gained its buttons and
                                ;  became the Timer - SPEC.md 14)
KIND_FILES   equ 3
KIND_CTRL    equ 4              ; (the Task Manager was kind 4 until it
                                ;  became the TASKMGR package - SPEC.md 28)
KIND_HIBER   equ 5              ; the Hibernate window (SPEC.md 86.1): the
                                ; confirmation, and the boot-time question.
                                ; kern_big only
KIND_PKG     equ 0x80           ; bit 7: package instance

; --- osapi_sys_snapshot's buffer, API slot 0x0298 (SPEC.md 20.9) ------------------
; A scheduler header, then one record per instance. Both halves are copied in
; one cli window, which is the whole reason this is a snapshot rather than a
; getter - see the routine at the foot of this file. ABI: mirrored in
; apps/os88api.inc, and every offset here is derived from MAX_TASKS (sched)
; or INST_MAX (above), so the layout follows the tables it measures.
SS_TICKS   equ 0                ; word: [ticks] at the moment of the snapshot
SS_CUR     equ 2                ; byte: the running task slot ([sch_cur])
SS_COOP    equ 3                ; byte: 0 pre-emptive, 1 cooperative (8.2)
SS_NTASK   equ 4                ; byte: MAX_TASKS, the same figure AX answers
SS_NINST   equ 5                ; byte: INST_MAX, likewise BX
SS_TSTATE  equ 6                ; MAX_TASKS bytes: T_STATE per slot
SS_TCYC    equ SS_TSTATE + MAX_TASKS       ; MAX_TASKS dwords: sch_cycles
SS_INST    equ SS_TCYC + MAX_TASKS * 4     ; ...then the instance records
SSI_STATE  equ 0                ; byte: I_STATE - 0 free, 1 live, 2 dying
SSI_TASK   equ 1                ; byte: I_TASK - the task slot, 0xFF = none
SSI_KIND   equ 2                ; byte: I_KIND - bit 7 (KIND_PKG) = package
SSI_RSVD   equ 3                ; byte: pad, so every word below sits even
SSI_SIZE   equ 4                ; word: I_SIZE - region BYTES, 0 for built-ins
SSI_SEG    equ 6                ; word: I_SPTR - the region's base segment
SSI_CYC    equ 8                ; dword: I_CYC, the cycles billed to this row
SSI_KB     equ 12               ; word: KB this instance holds off the heap -
                                ; the ONE field filled outside the cli window
SSI_NAME   equ 14               ; 16 bytes: I_NAME, NUL-terminated
SSI_RECSZ  equ 30
SYS_SNAPSHOT_SIZE equ SS_INST + INST_MAX * SSI_RECSZ

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
    dw inst_ico_about
    db 1, 0                     ; About: stateless singleton
    dw app_tpl_timer,  app_tmr_task,    app_tmr_pool,  TMR_SIZE, app_tmr_kinit, app_ttl_timer
    dw inst_ico_timer
    db 10, 0                    ; Timer: 10 x 16-byte time state, one task each
                                ; (8 of the 16 are the cells on screen, so a
                                ; redraw can be the ones that moved - SPEC.md 14)
    dw app_tpl_bounce, app_bounce_task, app_ball_pool, 8,   app_bounce_kinit, app_ttl_bounce
    dw inst_ico_bounce
    db 10, 0                    ; Bounce: 10 x 8-byte ball state, one task each
    dw fm_tpl,         0,               fm_pool,       FS_SIZE,  fm_kinit,     fm_ttl
    dw inst_ico_disk
    db VIEW_SLOTS, WF_SIZABLE   ; Disk: FOUR windows, each with its own
                                ; FS_SIZE-byte state block and its own claimed
                                ; listing cache; resizable (SPEC.md 22/22.1).
                                ; The cap, VIEW_SLOTS and fm_pool's size are
                                ; one number wearing three hats.
    dw cp_tpl,         0,               0,             0,   cp_kinit,         cp_sname
    dw inst_ico_ctrl
    db 1, 0                     ; Control Panel: stateless task-less singleton
%ifdef KERN_BIG
    dw hb_tpl,         0,               0,             0,   hb_kinit,         hb_ttl
    dw 0                        ; the generic icon: it is never in the dock
    db 1, 0                     ; Hibernate: stateless task-less singleton
                                ; (SPEC.md 86.1), painted by its module
%endif

; --- the built-in kinds' icons (SPEC.md 25/29.3) -----------------------------
; 16 mask words then 16 data words, bit 15 leftmost - icon_draw16's layout,
; the same one a package embeds in its header (SPEC.md 20.2). They show in
; the dock (SPEC.md 30). KD_ICON 0 still means "the generic icon" and still
; works; nothing in the tree uses it now except a package that ships without
; one, which apps/hello does deliberately.
; inst_ico_about - the os8088 DIP chip, the same logo the System menu
; carries (menu_logo_bmp) at icon size
;   ................
;   ................
;   ...####oo####...
;   .##############.
;   ...##########...
;   .##############.
;   ...##########...
;   .##############.
;   ...##########...
;   .##############.
;   ...##########...
;   .##############.
;   ...##########...
;   ................
;   ................
;   ................
inst_ico_about:
    dw 0x0000                       ; 16 mask rows
    dw 0x3FFC
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x3FFC
    dw 0x0000
    dw 0x0000
    dw 0x0000                       ; 16 data rows
    dw 0x0000
    dw 0x1E78
    dw 0x7FFE
    dw 0x1FF8
    dw 0x7FFE
    dw 0x1FF8
    dw 0x7FFE
    dw 0x1FF8
    dw 0x7FFE
    dw 0x1FF8
    dw 0x7FFE
    dw 0x1FF8
    dw 0x0000
    dw 0x0000
    dw 0x0000

; inst_ico_timer - a stopwatch: crown on top, hands at 12 and 3
;   ......####......
;   ......####......
;   .....######.....
;   ....##oooo##....
;   ...##oooooo##...
;   ..##ooo#oooo##..
;   .##oooo#ooooo##.
;   .#ooooo#oooooo#.
;   .#ooooo#oooooo#.
;   .#ooooo####ooo#.
;   .#oooooooooooo#.
;   .##oooooooooo##.
;   ..##oooooooo##..
;   ...##oooooo##...
;   ....##oooo##....
;   .....######.....
inst_ico_timer:
    dw 0x03C0                       ; 16 mask rows
    dw 0x03C0
    dw 0x07E0
    dw 0x0FF0
    dw 0x1FF8
    dw 0x3FFC
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x3FFC
    dw 0x1FF8
    dw 0x0FF0
    dw 0x07E0
    dw 0x03C0                       ; 16 data rows
    dw 0x03C0
    dw 0x07E0
    dw 0x0C30
    dw 0x1818
    dw 0x310C
    dw 0x6106
    dw 0x4102
    dw 0x4102
    dw 0x41E2
    dw 0x4002
    dw 0x6006
    dw 0x300C
    dw 0x1818
    dw 0x0C30
    dw 0x07E0

; inst_ico_bounce - a ball, its trajectory and the floor it will come off
;   ................
;   ..####..........
;   .######.........
;   .######.........
;   ..####..........
;   .......#........
;   ........#.......
;   .........#......
;   ..........#.....
;   ...........#....
;   ............#...
;   .............#..
;   ..............#.
;   ................
;   .##############.
;   ................
inst_ico_bounce:
    dw 0x7E00                       ; 16 mask rows
    dw 0xFF00
    dw 0xFF00
    dw 0xFF00
    dw 0xFF80
    dw 0x7FC0
    dw 0x03E0
    dw 0x01F0
    dw 0x00F8
    dw 0x007C
    dw 0x003E
    dw 0x001F
    dw 0x000F
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0000                       ; 16 data rows
    dw 0x3C00
    dw 0x7E00
    dw 0x7E00
    dw 0x3C00
    dw 0x0100
    dw 0x0080
    dw 0x0040
    dw 0x0020
    dw 0x0010
    dw 0x0008
    dw 0x0004
    dw 0x0002
    dw 0x0000
    dw 0x7FFE
    dw 0x0000

; inst_ico_disk - a folder: the Disk window browses a volume, so it is
; the browser that gets an icon here, not the drive - desk.inc
; already draws the floppy on the desktop
;   ................
;   .######.........
;   .#oooo#########.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .#oooooooooooo#.
;   .##############.
;   ................
;   ................
inst_ico_disk:
    dw 0xFF00                       ; 16 mask rows
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0000
    dw 0x0000                       ; 16 data rows
    dw 0x7E00
    dw 0x43FE
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x4002
    dw 0x7FFE
    dw 0x0000
    dw 0x0000

; inst_ico_ctrl - a panel of three sliders, each knob somewhere else
;   ................
;   .##############.
;   .#oooooooooooo#.
;   .#o##########o#.
;   .#oo##oooooooo#.
;   .#oooooooooooo#.
;   .#o##########o#.
;   .#oooooooo##oo#.
;   .#oooooooooooo#.
;   .#o##########o#.
;   .#ooooo##ooooo#.
;   .#oooooooooooo#.
;   .##############.
;   ................
;   ................
;   ................
inst_ico_ctrl:
    dw 0xFFFF                       ; 16 mask rows
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0000
    dw 0x0000
    dw 0x0000                       ; 16 data rows
    dw 0x7FFE
    dw 0x4002
    dw 0x5FFA
    dw 0x4C02
    dw 0x4002
    dw 0x5FFA
    dw 0x4032
    dw 0x4002
    dw 0x5FFA
    dw 0x4182
    dw 0x4002
    dw 0x7FFE
    dw 0x0000
    dw 0x0000
    dw 0x0000

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
; -----------------------------------------------------------------------------
; inst_park_wait - hold this worker still while the heap is compacted
; in:  DI = the calling worker's own instance record
; out: nothing (all registers and the flags preserved)
;
; SPEC.md 66.5. Sets this instance's parked byte, spins on task_yield until
; the compactor withdraws the request, and clears it again. It runs with
; [sch_lock] CLEAR by construction - mem_compact drops the lock before asking
; and takes it back after - which is what lets the yield here make progress.
;
; It cannot deadlock the machine: the request is withdrawn unconditionally on
; every path out of mem_compact, including the one where the plan turned out
; not to be worth executing. A worker that never arrives here - asleep,
; blocked on the gfx lock, or built against an SDK that predates this - simply
; keeps its claims for that pass, which is the outcome the compactor already
; has to handle.
; -----------------------------------------------------------------------------
inst_park_wait:
    pushf
    push ax
    push bx
    push di
    mov bx, di
    sub bx, inst_tab
    push cx                     ; bank CX BEFORE the shift count lands in it:
    mov cl, 5                   ; I_RECSZ = 32. The first cut wrote CL and
    shr bx, cl                  ; then pushed, so the pop restored a 5, and
    pop cx                      ; OSAPI_TASK_ALIVE - which promises every
                                ; register back - handed a worker CL = 5 on
                                ; exactly the pass a compaction was pending
    cmp bx, INST_MAX
    jae .out                    ; not a record at all: park nothing
    mov bl, [sch_cur]           ; PER TASK and not per instance (SPEC.md
    xor bh, bh                  ; 66.5.5): a driver's worker has T_INST = 0xFF
    mov byte [bx+sch_parked], 1 ; and no record to be parked in at all
.spin:
    cmp byte [inst_parkreq], 0
    je .done
    call task_yield
    jmp short .spin
.done:
    mov byte [bx+sch_parked], 0
.out:
    pop di
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; inst_park_lk / inst_park_unlk - park while BLOCKED ON THE GFX LOCK (66.5.4)
; in:  nothing (the running task's own instance)
; out: nothing (all registers and the flags preserved)
;
; THE LIMIT THIS EXISTS TO LIFT. A worker parks at OSAPI_TASK_ALIVE; a worker
; that DRAWS takes the gfx lock once a pass; and a claim that triggers
; compaction almost always comes from a window callback holding that same
; lock. So the worker blocks in gfx_lock, never reaches ALIVE, never parks,
; and mem_can_move refuses every claim its instance owns. Measured before this
; existed: Tracker's 114KB module sat still with 38KB free directly beneath it
; (SPEC.md 66.5.3).
;
; A task spinning in gfx_lock is not executing package code, so parking it
; there is safe IF its stack holds no pointer derived from a movable claim -
; and that is the package's business, not the kernel's. So it is DECLARED:
; only an instance that called OSAPI_MEM_PARKSAFE is marked here, and the
; default is not to be. A package that forgets pays a missed optimisation;
; there is no direction in which forgetting costs memory.
;
; The mark is set immediately before the yield and cleared immediately after,
; so it is set exactly across the window in which this task is descheduled
; inside gfx_lock. The few instructions either side are inside gfx_lock's own
; retry loop, which is the state being asserted about. A compactor sampling
; the mark in that gap simply waits another tick - inst_park_all is a poll -
; and one sampling it late refuses the move, which is the safe direction.
; -----------------------------------------------------------------------------
inst_park_lk:
    pushf
    push bx
    call inst_park_me           ; BX = our parked byte, or CF = 1 for none
    jc .out
    mov byte [bx], 1
.out:
    pop bx
    popf
    ret

inst_park_unlk:
    pushf
    push bx
    call inst_park_me
    jc .out
    mov byte [bx], 0
.out:
    pop bx
    popf
    ret

; inst_park_me - BX = the running task's sch_parked byte; CF = 1 when it has
; none (the UI task, or a task with no record) or has not declared itself
; park-safe. Clobbers BX and the flags and nothing else.
inst_park_me:
    push ax
    push cx
    mov bl, [sch_cur]
    xor bh, bh
    mov cl, 3
    shl bx, cl                  ; T_SIZE = 8, the snd.inc idiom
    mov al, [bx+sch_tasks+T_INST]
    cmp al, INST_MAX
    jae .no
    mov bl, al
    xor bh, bh
    cmp byte [bx+inst_parksafe], 0
    je .no                      ; never declared: park it at ALIVE and nowhere
                                ; else (SPEC.md 66.5.4)
    mov bl, [sch_cur]           ; ...and the MARK is the running task's
    xor bh, bh
    add bx, sch_parked
    pop cx
    pop ax
    clc
    ret
.no:
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; inst_parksafe_set - API slot 0x0408: "you may park me while I am blocked in
;                     the kernel" (SPEC.md 66.5.4)
; in:  AL = 1 declare / 0 withdraw, ES = the caller's segment (the X stub's)
; out: CF = 0 recorded; CF = 1 the caller is not a live package instance
; clobbers: flags
; -----------------------------------------------------------------------------
inst_parksafe_set:
    push ax
    push bx
    push cx
    push di
    mov di, inst_tab
    xor bx, bx                  ; BL = the slot
    mov cx, INST_MAX
.scan:
    cmp byte [di+I_STATE], 0
    je .next
    test byte [di+I_KIND], KIND_PKG
    jz .next
    mov ah, al
    mov ax, es
    cmp [di+I_SPTR], ax
    mov al, ah
    je .hit
.next:
    inc bl
    add di, I_RECSZ
    loop .scan
    pop di
    pop cx
    pop bx
    pop ax
    stc
    ret
.hit:
    mov [bx+inst_parksafe], al
    pop di
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; inst_park_req - ask every package worker to stand still, and wait for them
; in:  nothing
; out: nothing (all registers and the flags preserved)
;
; Bounded by INST_PARKW ticks and NOT by the workers agreeing: whoever has
; parked by the deadline gets its claims compacted and whoever has not keeps
; them. That asymmetry is the whole safety property - the compactor asks and
; never assumes, so a worker that cannot answer costs a missed optimisation
; rather than a moved block under a running mixer.
;
; It returns as soon as every worker has parked, which on an idle machine is
; the very next tick.
; -----------------------------------------------------------------------------
INST_PARKW equ 4                ; ticks: ~220ms, long enough for a worker that
                                ; sleeps a tick between passes and short
                                ; enough not to be felt inside a claim that is
                                ; already going to pause the machine

inst_park_req:
%ifdef NOPARK
    ret                         ; make HEAPPARK=0: nobody is ever asked, so
                                ; nobody ever parks and inst_seg_parked keeps
                                ; refusing - the A/B for SPEC.md 66.5 alone,
                                ; with the compactor itself left in place
%else
    pushf
    push ax
    push cx
    push dx
    mov byte [inst_parkreq], 1
    mov ax, [ticks]
    add ax, INST_PARKW
    mov dx, ax                  ; DX = the deadline
.wait:
    call inst_park_all          ; CF = 0: everybody who has a worker has parked
    jnc .out
    mov ax, [ticks]
    sub ax, dx
    jns .out                    ; SPEC.md 45.15's rule: two free-running
                                ; counters compare by the SIGN of the
                                ; difference, never with jg
    call task_yield
    jmp short .wait
.out:
    pop dx
    pop cx
    pop ax
    popf
    ret
%endif

; inst_park_end - withdraw the request; every parked worker resumes
inst_park_end:
    mov byte [inst_parkreq], 0
    retf

; -----------------------------------------------------------------------------
; inst_park_all - has every live package worker parked? (module internal)
; out: CF = 0 yes (or there are none), CF = 1 somebody is still running
; clobbers: flags
; -----------------------------------------------------------------------------
inst_park_all:
    push ax
    push cx
    push di
    mov di, inst_tab
    xor ax, ax                  ; AL = the index
    mov cx, INST_MAX
.scan:
    cmp byte [di+I_STATE], 0
    je .next
    test byte [di+I_KIND], KIND_PKG
    jz .next
    cmp byte [di+I_TASK], 0xFF
    je .next                    ; no worker: nothing to wait for
    push bx
    mov bl, [di+I_TASK]         ; the worker's OWN task slot
    xor bh, bh
    cmp byte [bx+sch_parked], 0
    pop bx
    je .busy
.next:
    inc al
    add di, I_RECSZ
    loop .scan
    pop di
    pop cx
    pop ax
    clc
    ret
.busy:
    pop di
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; inst_seg_parked - is the package running at segment BX standing still?
; in:  BX = a package's region segment
; out: CF = 0 it is parked (or owns no worker), CF = 1 it may be running
; clobbers: flags
;
; mem_can_move's question, asked of instance.inc because inst_tab is this
; module's (SPEC.md 29). A segment that matches no instance answers CF = 1:
; a driver's image matches none, and "I cannot tell" has to mean "it is busy".
; -----------------------------------------------------------------------------
inst_seg_parked:
    push ax
    push cx
    push di
    mov di, inst_tab
    xor ax, ax
    mov cx, INST_MAX
.scan:
    cmp byte [di+I_STATE], 0
    je .next
    test byte [di+I_KIND], KIND_PKG
    jz .next
    cmp [di+I_SPTR], bx
    jne .next
    cmp byte [di+I_TASK], 0xFF
    je .free                    ; no worker at all
    push bx
    mov bl, [di+I_TASK]
    xor bh, bh
    cmp byte [bx+sch_parked], 0
    pop bx
    jne .free                   ; parked: it is standing at the top of its own
                                ; loop and holds nothing derived
    jmp short .busy
.next:
    inc al
    add di, I_RECSZ
    loop .scan
    call inst_svc_parked        ; no instance owns it, so it is a DRIVER image
    jnc .free                   ; (SPEC.md 51.3) - and a driver's workers are
                                ; the TF_SERVICE tasks (SPEC.md 66.5.5)
.busy:
    pop di
    pop cx
    pop ax
    stc
    retf
.free:
    pop di
    pop cx
    pop ax
    clc
    retf
; -----------------------------------------------------------------------------
; inst_svc_parked - has every DRIVER worker parked? (SPEC.md 66.5.5)
; out: CF = 0 yes (or there are none), CF = 1 somebody is still running
; clobbers: flags
;
; A driver has no instance record, so drv_task stamps its worker T_INST = 0xFF
; and marks it TF_SERVICE - which is the only handle the kernel has on "a task
; running inside a driver". It cannot say WHICH driver, so this is deliberately
; all-or-nothing: no driver-owned claim moves while any service task is
; running. Conservative, and the conservative direction is a claim that stays
; where it is.
; -----------------------------------------------------------------------------
inst_svc_parked:
    push ax
    push bx
    push cx
    mov bx, sch_tasks
    xor cx, cx                  ; CL = the slot
.scan:
    cmp byte [bx+T_STATE], 0
    je .next                    ; a free slot
    test byte [bx+T_FLAGS], TF_SERVICE
    jz .next
    push bx
    mov bl, cl
    xor bh, bh
    cmp byte [bx+sch_parked], 0
    pop bx
    je .busy
.next:
    inc cl
    add bx, T_SIZE
    cmp cl, MAX_TASKS
    jb .scan
    pop cx
    pop bx
    pop ax
    clc
    ret
.busy:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; inst_task_park - API slot 0x0410: a DRIVER's worker parks here (SPEC.md
;                  66.5.5), the way a package's parks at OSAPI_TASK_ALIVE
; in:  nothing
; out: nothing (all registers and the flags preserved)
;
; Its own slot rather than a branch of ALIVE, for drv_task's reason: ALIVE's
; whole body is a chain of tests on an INSTANCE RECORD and a driver has none.
; It costs one compare when nothing is asking.
; -----------------------------------------------------------------------------
inst_task_park:
    pushf
    push bx
    cmp byte [inst_parkreq], 0
    je .out
    mov bl, [sch_cur]
    xor bh, bh
    mov byte [bx+sch_parked], 1
.spin:
    cmp byte [inst_parkreq], 0
    je .done
    call task_yield
    jmp short .spin
.done:
    mov byte [bx+sch_parked], 0
.out:
    pop bx
    popf
    ret



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
;      zeroed, and the slot's inst_parksafe byte with them; CF = 1 table full
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
    call inst_fhome_clr         ; ...nor its Standard File location (38.10)
    push bx                     ; ...nor a dead instance's PARKSAFE (66.5.4):
    call inst_fhome_idx         ; the byte is a side table keyed by slot, and
    mov byte [bx+inst_parksafe], 0 ; a package that never declared it must
    pop bx                      ; not be parked in gfx_lock because the last
    clc                         ; tenant of its slot said so
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; inst_caller - which instance is asking? (SPEC.md 34.3's rule, generalised)
; out:      DH = the requesting instance slot: the dispatched callback's stamp
;           ([snd_inst], set at the I_CYC billing sites), else the running
;           task's T_INST. 0xFF = the kernel itself.
; clobbers: DH (the output), flags
;
; This body was snd_req_inst and is instance.inc's now, because the question
; it answers - who is the kernel doing this on behalf of? - stopped being the
; sound tier's alone when the file API needed it (SPEC.md 19.2.1).
; snd_req_inst is a jump to it, so SPEC.md 34.3's contract is untouched.
;
; The stamp is only the answer for a caller running INSIDE that callback, and
; the callback runs on one task. A worker that pre-empts it is a different
; task, and honouring the stamp there would attribute its work to whichever
; app happened to be dispatching. So the stamp carries the task that wrote it
; and is spent only by that task; everyone else falls through to their own
; T_INST.
;
; THE PACKAGE ENTRY PROC IS A STAMPED DISPATCH TOO (SPEC.md 41.5.1), and for
; a while it was the one that was not. It runs on the UI task, whose T_INST is
; 0xFF, so before loader_run step 8 bracketed it this routine answered KERNEL
; for everything an app asked while starting up - a sound grant that was never
; released, a block above 1MB stranded for the session, a toast that outlived
; its app, and the file API answering with the machine's directory instead of
; the instance's. It is the FIRST thing a package runs and the natural place
; to size itself, which is what made it worth the four instructions.
; -----------------------------------------------------------------------------
inst_caller:
    push bx
    mov bx, [snd_inst]          ; BL = the stamped instance, BH = the task
    cmp bh, [sch_cur]           ; that stamped it. A DIFFERENT task is running
    jne .task                   ; now, so it pre-empted that callback and the
    mov dh, bl                  ; callback's instance is not its to spend
    cmp dh, 0xFF
    jne .out
.task:
    mov bl, [sch_cur]           ; not our callback: the running task's
    xor bh, bh                  ; instance (0xFF for the UI task)
    shl bx, 1                   ; T_SIZE = 8
    shl bx, 1
    shl bx, 1
    mov dh, [bx+sch_tasks+T_INST]
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; inst_vol_enter - stand the machine in the CALLING instance's directory
;                  (SPEC.md 19.2.1)
; in:  nothing; UI-task context, no gfx lock held (the file API's own terms)
; out: nothing at all - not even CF. All registers and the flags are
;      preserved, because this sits underneath every file API entry and none
;      of them has a register to spare
;
; The file API resolves a name in the current directory (SPEC.md 19.2), and
; that is ONE pair of words for the whole machine. This is what stops that
; being the same thing as "wherever the user last dragged a Disk window":
; before an instance's file operation runs, the volume is put where THAT
; instance believes it is standing.
;
; **The compare is what makes it free.** A machine with one app doing file
; work never moves, so banked == current on every call and the cost is six
; compares. A mount is paid only when something really did move the volume
; underneath this app - which is exactly the case that used to write the
; file into the wrong folder in silence.
;
; QUIET (SPEC.md 18.9), because a file operation reads no listing: the scan,
; the sort and one icon-harvest read per file are all bought for nothing
; here. The [dsk_lstale] debt it leaves is deliberately never collected, on
; the same terms as drv_vol_back's and assoc_back's - every reader of the
; global snapshot consults that flag, and the one that cannot (the modal
; file dialog, SPEC.md 38.2) does a full dsk_chdir of its own on the way in
; and on every navigation.
;
; A failure needs no reporting and gets none: dsk_chdir_q's failure path
; leaves the volume at a root with [dsk_mntok] shut, so the operation about
; to run refuses with FERR_NODISK - which is the truth, and is what the
; caller would have been told anyway.
; -----------------------------------------------------------------------------
inst_vol_enter:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call inst_caller            ; DH = the instance behind this call
    cmp dh, INST_MAX
    jae .out                    ; 0xFF: the kernel or a driver, which has no
                                ; folder of its own and uses the machine's
    mov bl, dh
    xor bh, bh
    mov dl, [inst_fdrv+bx]      ; dsk_chdir's arguments, loaded before the
    shl bx, 1                   ; compare because they ARE the compare
    mov ax, [inst_fcwd+bx]
    cmp dl, [disk_drive]
    jne .go
    cmp ax, [dsk_cwd]
    je .out                     ; already standing there: nothing to undo,
.go:                            ; and the remount is the whole cost
    call dsk_chdir_q
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; inst_vol_mark - record where the calling instance is now standing (19.2.1)
; in:  the volume is where that instance should be left
; out: nothing (all registers and the flags preserved)
;
; The other direction, for the two paths that MOVE an instance on purpose:
; OSAPI_FILE_GOTO, and the dialog's own fdlg_home_dir. Everything else leaves
; the volume where it found it, so nothing else has to call this.
; -----------------------------------------------------------------------------
inst_vol_mark:
    pushf
    push ax
    push bx
    push dx
    call inst_caller
    cmp dh, INST_MAX
    jae .out
    mov bl, dh
    xor bh, bh
    mov al, [disk_drive]
    mov [inst_fdrv+bx], al
    shl bx, 1
    mov ax, [dsk_cwd]
    mov [inst_fcwd+bx], ax
.out:
    pop dx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; inst_fhome_idx - a record pointer to its per-instance file-home index
; in:  DI = a record inside inst_tab
; out: BX = the slot index (0..INST_MAX-1)
; clobbers: BX (the output), flags
;
; The one place the side table's indexing lives, so instance.inc and fdlg.inc
; cannot come to disagree about which row belongs to which record.
; -----------------------------------------------------------------------------
inst_fhome_idx:
    push cx
    mov bx, di
    sub bx, inst_tab
    mov cl, 5                   ; I_RECSZ is a power of two for exactly this
    shr bx, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; inst_fhome_clr - seed a slot's own current directory (SPEC.md 19.2.1)
; in:  DI = the record being (re)allocated
; out: nothing (all registers preserved, flags included)
;
; Called from inst_alloc for I_CYC's reason: a reused record must not inherit
; a dead instance's folder, or the first Save from a freshly launched app
; would open somewhere the user never took IT.
;
; It SEEDS rather than blanks, and that is the whole difference between a
; per-app folder and a per-app MEMORY of one: the new instance starts
; standing where it was launched from, which for a package is the folder its
; own file came out of. So there is no "this instance has no folder" state
; for inst_vol_enter to have a policy about. Whether the USER has since
; chosen one is the separate question [inst_fpick] answers, and only the
; dialog's default (SPEC.md 38.10) reads it.
; -----------------------------------------------------------------------------
inst_fhome_clr:
    pushf
    push ax
    push bx
    push cx
    push si
    call inst_fhome_idx         ; BX = the slot
    mov al, [disk_drive]        ; where this app is being launched FROM
    mov [inst_fdrv+bx], al
    mov byte [inst_fpick+bx], 0 ; ...which the user has not CHOSEN
    mov si, bx
    shl si, 1
    mov ax, [dsk_cwd]
    mov [inst_fcwd+si], ax
    mov al, bl
    mov cl, INST_FNSZ
    mul cl                      ; AX = slot * INST_FNSZ (AL is 0..11, so the
    mov si, ax                  ; product cannot leave AX)
    mov byte [inst_fname+si], 0
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; inst_set_name - copy a name into a record's I_NAME field
; in:  DI = record, SI = source: NUL-terminated OR NUL-padded (a package
;      header's 16-byte name field is safe); at most 15 chars are taken
; out: nothing (all registers preserved); I_NAME zero-filled first, so
;      byte I_NAME+15 is always 0
; -----------------------------------------------------------------------------
inst_set_name:
    push es
    push ds
    pop es                      ; the kernel's own strings; the ES-taking
    call inst_set_name_x        ; body below is the one that copies
    pop es
    ret

; -----------------------------------------------------------------------------
; inst_set_name_x - inst_set_name with the source at ES:SI (SPEC.md 20.1)
; in:  DI = record, ES:SI = the name; at most 15 chars are taken
; out: nothing (all registers preserved)
;
; The loader's copy of a package's header name crosses a segment boundary
; now that a package owns its segment, and this is the one place that has to
; know it.
; -----------------------------------------------------------------------------
inst_set_name_x:
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
    mov al, [es:si]
    inc si
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
; inst_icon_ptr - one instance's 16x16 icon body, in the KERNEL segment
; in:  DI = instance record
; out: SI -> 64 bytes of icon body; CF = 1 = this instance has no icon of its
;      own and the caller should use the generic one
; clobbers: SI (the output), flags
;
; I_ICON is either 0 (none), a plain pointer into .text (a built-in kind's,
; out of its KD_ICON) or I_ICO_PKG - "the icon is in this instance's own
; region, at the fixed offset every package header puts it at".
;
; **That third case is why there is no icon table any more.** There used to be
; 768 bytes of kernel .bss holding a per-slot COPY of each package's embedded
; body, made at load time - and the original was in the package's region the
; whole time, alive for exactly as long as the instance that owns it. The copy
; was pure duplication: a dock tile is drawn when the dock changes, so staging
; 64 bytes then costs nothing measurable, and the record's own I_SPTR is the
; segment to stage them from. A dying instance draws no tile (I_STATE is 2
; before wm_destroy repaints, SPEC.md 29.2), so the region is never freed
; under a draw.
; -----------------------------------------------------------------------------
inst_icon_ptr:
    push ax
    push cx
    push dx
    push di
    mov si, [di+I_ICON]
    or si, si
    jz .none
    cmp si, I_ICO_PKG
    jne .have                   ; a .text pointer: nothing to stage
    cmp word [di+I_SIZE], 0
    je .none                    ; not a package: I_SPTR is a state block
    mov dx, [di+I_SPTR]         ; the REGION, and byte 32 of it is the body
    mov si, LD_HDR_SIZE
    mov di, inst_icobuf
    mov cx, 64
    mov ax, cs
    call COLD_SEG:dkf_dsk_copy_seg
    mov si, inst_icobuf
.have:
    pop di
    pop dx
    pop cx
    pop ax
    clc
    ret
.none:
    pop di
    pop dx
    pop cx
    pop ax
    stc
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
%ifdef WM_ANIM
    call inst_unmin             ; un-minimize, and 11.99.3's other half
%else
    and byte [di+I_FLAGS], 0xFE ; un-minimize
%endif
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
    xor bx, bx                  ; a built-in kind's task is kernel code
    call task_spawn             ; in AX, BX, DX; out CF / AL = slot
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
    call wm_ask_close           ; SPEC.md 75.1: the window may REFUSE, and a
    jc .refused                 ; refusal means nothing at all happens - not
                                ; even the hide, because a window that is
                                ; asking "save changes?" has to stay on screen
                                ; to be answered. ABOVE the ownership test on
                                ; purpose: an unowned window's close reduces to
                                ; wm_hide, and the kernel's own dialogs read
                                ; that hide AS the cancel (SPEC.md 38.2, 75.3),
                                ; so their negotiator - if one is ever
                                ; installed - has to be able to refuse it too
    call inst_of_win            ; DI = record; CF = unowned
    jc .hide
    cmp byte [di+I_STATE], 1
    jne .hide                   ; already dying: re-hide, idempotent
    cmp byte [di+I_TASK], 0xFF
    jne .tasked
    mov byte [di+I_STATE], 2    ; dying: lock-held readers skip it from here
    call toast_owner_gone       ; ITS OLD MESSAGE GOES FIRST, before anything
                                ; below can raise a NEW one (SPEC.md 59.6).
                                ; cp_flush_close writes SYSTEM.CFG and says
                                ; whether that worked - the panel's last act
                                ; is a report about the panel - so a kill
                                ; ordered after it would take that report
                                ; with it, and the ordering is what makes
                                ; that impossible rather than a claim about
                                ; whose slot inst_caller answers with
%ifdef KERN_BIG
    cmp byte [di+I_KIND], KIND_HIBER
    jne .nohib
    call COLD_SEG:hbf_closed    ; the Hibernate window went by a route its
.nohib:                         ; module did not take - the close box, Close
%endif                          ; Window, the Task Manager - so the image can
                                ; go now (SPEC.md 86.1)
    cmp byte [di+I_KIND], KIND_CTRL
    jne .noflush                ; the Control Panel defers its settings write
    call COLD_SEG:drv_cp_closed_x ; ...and tell the drivers, which is where the
                                ; hard disk's tool image goes back (SPEC.md
                                ; 31.9.1). BEFORE the settings write, so a
                                ; driver that stages something on the way out
                                ; is staging it into the blob that is about to
                                ; be written rather than the one after
    call COLD_SEG:cpf_cp_flush_close ; to here (SPEC.md 31.8): one floppy write per
                                ; session instead of one per click. Only the
                                ; TASK-LESS branch needs it - the panel owns
                                ; no task, so it can never reach
                                ; inst_task_die - and only on CLOSE, because
                                ; minimizing leaves it open
.noflush:
    call snd_release_rec        ; free every sound grant before the record
                                ; goes (SPEC.md 29.4/34.3)
    call xm_release_rec         ; ...and every extended-memory block with it
                                ; (SPEC.md 41.5). It may run BEFORE the windows
                                ; where mem_free_rec may not, and the reason is
                                ; the rule below read exactly: a freed REGION
                                ; makes a survivor's procs a wild call, and
                                ; nothing up there is ever code (SPEC.md 20.8
                                ; rule 1) or even addressable in real mode
    call wm_destroy             ; frees the wm slot + wm_owner, repaints -
    mov word [di+I_WIN], 0      ; and the windows go BEFORE the claims: the
    cmp word [di+I_SIZE], 0     ; repaint inside a destroy far-calls every
    je .swept                   ; survivor, and a survivor whose procs live
    push ax                     ; in a freed region is a wild call. For a
    mov ax, [di+I_SPTR]         ; package, sweep the region's OTHER windows
    call wm_destroy_seg         ; too (SPEC.md 20.2) - I_SPTR is its segment
    pop ax                      ; (a built-in's is a state ptr; I_SIZE tells
.swept:                         ; them apart)
    call mem_free_rec           ; ...then every heap claim (SPEC.md 50.4)
    mov byte [di+I_STATE], 0    ; record (and any package region) free
    jmp .out
.tasked:
    mov byte [di+I_STATE], 2    ; the die flag (SPEC.md 14/29)
.hide:
    call wm_hide                ; instant visual feedback + repaint
.refused:
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
%ifdef WM_ANIM
    call wm_an_ok               ; MAY IT? (SPEC.md 76.11.1) This site called
    jnc .hide                   ; wm_anim DIRECTLY and was the one path that
                                ; did not ask - so the Theme page's switch
                                ; turned the OPEN zoom off and left this one
                                ; running, and WF_NOANIM and the fullscreen
                                ; test were bypassed with it: a minimize while
                                ; an fsx bracket owns the screen drew an XOR
                                ; outline over somebody's borrowed surface,
                                ; which is the hazard 11.99 names
    call inst_an_tile           ; SPEC.md 11.99: the window flies to its tile -
                                ; BEFORE wm_hide, while it is still on the glass
    push si
    push di
    mov si, bx
    mov di, wm_an_s
    call wm_an_setw
    call wm_anim
    pop di
    pop si
%endif
.hide:
    call wm_hide
    pop di
    ret

%ifdef WM_ANIM
; -----------------------------------------------------------------------------
; inst_an_tile - fill wm_an_d with this record's dock tile (SPEC.md 11.99)
; in:  DI = the instance record
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
inst_an_tile:
    push ax
    push bx
    push cx
    push dx
    mov ax, di                  ; slot = (record - inst_tab) / I_RECSZ, which
    sub ax, inst_tab            ; is a shift: I_RECSZ is 32 by construction
    mov cl, 5
    shr ax, cl
    mov cx, ax
    call dock_tile_rect         ; AX/BX/CX/DX
    mov [wm_an_d], ax
    mov [wm_an_d+2], bx
    mov [wm_an_d+4], cx
    mov [wm_an_d+6], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; inst_unmin - clear the minimized bit AND say where the window is coming from
; in:  DI = the instance record; caller holds the gfx lock and calls wm_show
;      itself immediately afterwards
; out: nothing; every register preserved
;
; SPEC.md 11.99.3. All four un-minimize paths used to be a bare
; `and byte [di+I_FLAGS], 0xFE` followed by wm_show, and that is exactly one
; instruction too clever: it throws away the only fact that says the window is
; coming out of the dock rather than opening fresh, and wm_show cannot get it
; back. One routine does both halves so a fifth path cannot forget the second.
; -----------------------------------------------------------------------------
inst_unmin:
    test byte [di+I_FLAGS], 1   ; ...AND ONLY A REAL UN-MINIMIZE ARMS A TILE.
    jz .out                     ; Three of the four callers front an instance
                                ; that need not be minimized at all -
                                ; inst_find_kind and its two siblings match on
                                ; I_STATE and never look at this bit - and an
                                ; arm there flies an outline out of a dock tile
                                ; onto a window that never left the screen
    and byte [di+I_FLAGS], 0xFE
    push ax
    push bx
    push cx
    push dx
    call inst_an_tile           ; ...into wm_an_d, which wm_an_arm does not
    mov ax, [wm_an_d]           ; read - so move it across
    mov bx, [wm_an_d+2]
    mov cx, [wm_an_d+4]
    mov dx, [wm_an_d+6]
    call wm_an_arm
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret
%endif

; -----------------------------------------------------------------------------
; inst_restore - bring a minimized instance back
; in:  DI = instance record (I_STATE = 1); caller holds the gfx lock
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
inst_restore:
    push bx
%ifdef WM_ANIM
    call inst_unmin             ; the bit, AND where it is flying from (11.99.3)
%else
    and byte [di+I_FLAGS], 0xFE
%endif
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
; Reached from Timer's and Bounce's own loops (SPEC.md 14) and, for a
; package that took a worker, from inst_pkg_alive below (SPEC.md 20.6).
; -----------------------------------------------------------------------------
inst_task_die:
    call snd_release_rec        ; free every sound grant before the record
                                ; goes (SPEC.md 29.4/34.3)
    call xm_release_rec         ; ...and every extended-memory block (41.5)
    call toast_owner_gone       ; ...and its message (SPEC.md 59)
    call gfx_lock
    mov bx, [di+I_WIN]
    call wm_destroy             ; frees the wm slot + wm_owner, repaints -
    mov word [di+I_WIN], 0      ; and BEFORE mem_free_rec: the repaint
    cmp word [di+I_SIZE], 0     ; far-calls every surviving window, so none
    je .swept                   ; whose procs live in the dying region may
    mov ax, [di+I_SPTR]         ; outlive it. For a package, that includes
    call wm_destroy_seg         ; any window it made beyond the bound one
.swept:                         ; (SPEC.md 20.2)
    call gfx_unlock
    call mem_free_rec           ; every heap claim, region included (50.4)
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
; in:  AX = near entry point inside the package image (a whole-word package
;           address, relocated as class 0 by os88pkg.py, SPEC.md 20.2)
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
; OWNERSHIP FENCE (SPEC.md 20.6). BX is package input, so finding a live
; record is not enough: the record found must be a PACKAGE instance whose
; own region contains the entry point in AX. Without that test a package
; could hand over a stranger's window - a stale pointer, or one harvested
; from wm_wins - and the kernel would write the STRANGER's I_TASK, flipping
; that instance onto the die-flag close path (its close box would then hide
; it forever, waiting on a worker it does not own) while the caller's own
; record stayed task-less and its close took the synchronous path, freeing
; the region its worker is still executing in. Region containment is the
; cheap way to say "this entry belongs to this record": a second instance
; of the same package lives at a different base, so it is refused too.
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
    mov cx, es                  ; ...whose SEGMENT is the one calling us.
    cmp cx, [di+I_SPTR]         ; ES is the caller's DS, stamped by the API
    jne .fail                   ; stub (SPEC.md 20.3), and a package's DS is
                                ; its own segment - so this is an identity
                                ; test, not the containment approximation it
                                ; had to be while every package shared one
    cmp ax, [di+I_SIZE]         ; ...and an entry inside its own image+bss
    jae .fail
    mov ch, ah                  ; bank the caller's AH: task_spawn rebuilds
                                ; the whole of AX but preserves CX
    mov dx, di                  ; DX = instance index = (rec - inst_tab) >> 5,
    sub dx, inst_tab            ; the app_launch idiom (SPEC.md 29.4). It
    mov cl, 5                   ; lands in the worker's DX and its DL in
    shr dx, cl                  ; T_INST, so the Task Manager attributes the
                                ; task to this instance (SPEC.md 8/28)
    mov bx, [di+I_SPTR]         ; the worker runs in the package's segment
    call task_spawn             ; in AX = entry, BX = seg, DX = arg
    mov ah, ch                  ; caller's AH back - MOV touches no flags, so
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
%ifndef NOPARK
    cmp byte [inst_parkreq], 0  ; THE COMPACTION PARK (SPEC.md 66.5). This is
    je .out                     ; the one call rule 2 already makes mandatory
    call inst_park_wait         ; once per outer loop, and therefore the one
%endif
.out:                           ; point at which a worker is known to hold no
    pop di                      ; pointer derived from any claim of its own -
    popf                        ; it is at the top of its own loop
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
                                ; would zero the cold-entry jmp at 0800:0000
    jmp inst_task_die           ; never returns
.winless:
    call snd_release_rec        ; the same two releases inst_task_die makes,
    call xm_release_rec         ; in the same order (SPEC.md 29.4/34.3/41.5) -
                                ; this leg differs only in having no BOUND
                                ; window to destroy
    cmp word [di+I_SIZE], 0     ; ...but a package can have made windows it
    je .nwin                    ; never bound to the record, and they must not
    call gfx_lock               ; outlive the region either (SPEC.md 20.2)
    mov ax, [di+I_SPTR]
    call wm_destroy_seg
    call gfx_unlock
.nwin:
    call mem_free_rec
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
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
    pop ax
    retf

; =============================================================================
; osapi_sys_snapshot - API slot 0x0298: the scheduler and the instance table, in
;                  ONE cli window (SPEC.md 20.9/28.2)
;
; in:  ES:DI = a SYS_SNAPSHOT_SIZE buffer the caller owns
; out: AX = MAX_TASKS, BX = INST_MAX; the buffer filled; DI unchanged
; clobbers: AX/BX (outputs) and the flags
;
; This is the whole of what a task monitor cannot get any other way, and it
; is ONE call rather than two on purpose. task_exit frees an instance record
; inside the same IF=0 window that frees its task slot (SPEC.md 8), so a
; reader that takes the scheduler half and the instance half separately can
; see a slot live in one and gone in the other. What that costs is not a
; wrong row for one frame: both halves feed a per-interval DIFF of a running
; cycle counter, so a torn pair bills a real slice to the wrong row and the
; error persists in the percentages until something else disturbs them.
;
; The names ride inside the window too. They do not have to - a name is
; written once at inst_alloc and never again - but they are 192 of the 414
; bytes and splitting the copy would put a second loop over the same table
; for no other reason. The whole window is roughly 3.5-4 ms at 4.77MHz - the
; copy is field-at-a-time loops (the gather is strided), around 40-55 cycles
; per byte moved with the 8088's EA and prefetch costs - which is the
; longest IF=0 window in the system and safe by about 2x: the 8250 holds one
; mouse byte and the next is ~8.3 ms behind it at 1200 baud, and the PIT is
; only delayed, never lost. That margin is the reason for the twice-a-second
; caller rule below, and it shrinks if MAX_TASKS or INST_MAX ever grow -
; re-derive it before growing either.
;
; SSI_KB is the one field filled OUTSIDE it. mem_owned_kb is a scan of the
; claim table, which is not what the window is protecting, and it takes
; INST_MAX scans of MEM_MAX records - four hundred iterations that have no
; business running with interrupts off.
; =============================================================================
osapi_sys_snapshot:
    push cx
    push dx
    push si
    push di
    push bp

    mov bp, di                  ; BP = the buffer base, for life

    pushf
    cli
    mov ax, [ticks]
    mov [es:di+SS_TICKS], ax
    mov al, [sch_cur]
    mov [es:di+SS_CUR], al
    mov al, [sch_coop]
    mov [es:di+SS_COOP], al
    mov byte [es:di+SS_NTASK], MAX_TASKS
    mov byte [es:di+SS_NINST], INST_MAX

    mov si, sch_tasks + T_STATE ; T_STATE per slot
    add di, SS_TSTATE
    mov cx, MAX_TASKS
.ssnap:
    mov al, [si]
    mov [es:di], al
    add si, T_SIZE
    inc di
    loop .ssnap
    mov bl, [sch_idleslot]      ; ...and the IDLE TASK is named by a state of
    cmp bl, 0xFF                ; its own (SPEC.md 28.7): it is READY like any
    je .noidle                  ; other ready task and reading it as one makes
    xor bh, bh                  ; the process page charge 97% of the machine
    mov di, bp                  ; to a row it cannot explain. A fourth value
    add di, SS_TSTATE           ; costs no byte of the snapshot, which a slot
    add di, bx                  ; index in the header would have - and every
    mov byte [es:di], 3         ; field after it moving is an ABI break for
.noidle:                        ; something the Task Manager alone reads

    mov si, sch_cycles          ; ...and the cycle counters, MAX_TASKS dwords
    mov di, bp
    add di, SS_TCYC
    mov cx, MAX_TASKS * 2
.csnap:
    mov ax, [si]
    mov [es:di], ax
    add si, 2
    add di, 2
    loop .csnap

    mov si, inst_tab            ; the instance table, record for record
    mov di, bp
    add di, SS_INST
    mov dx, INST_MAX
.isnap:
    mov al, [si+I_STATE]
    mov [es:di+SSI_STATE], al
    mov al, [si+I_TASK]
    mov [es:di+SSI_TASK], al
    mov al, [si+I_KIND]
    mov [es:di+SSI_KIND], al
    mov byte [es:di+SSI_RSVD], 0
    mov ax, [si+I_SIZE]
    mov [es:di+SSI_SIZE], ax
    mov ax, [si+I_SPTR]
    mov [es:di+SSI_SEG], ax
    mov ax, [si+I_CYC]
    mov [es:di+SSI_CYC], ax
    mov ax, [si+I_CYC+2]
    mov [es:di+SSI_CYC+2], ax
    push si
    push di
    add si, I_NAME
    add di, SSI_NAME
    mov cx, 16
.incpy:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .incpy
    pop di
    pop si
    add si, I_RECSZ
    add di, SSI_RECSZ
    dec dx
    jnz .isnap
    popf

    ; --- and the heap share per instance, with interrupts back on -----------
    ; A package's claims are stamped with the segment it runs in and a
    ; built-in's with its slot (SPEC.md 50.2), so which owner word to ask for
    ; depends on the record we just copied.
    mov di, bp
    add di, SS_INST
    xor si, si                  ; SI = slot
.claim:
    xor ax, ax
    cmp byte [es:di+SSI_STATE], 0
    je .cstore
    mov bx, si
    test byte [es:di+SSI_KIND], KIND_PKG
    jz .cown
    mov bx, [es:di+SSI_SEG]
.cown:
    call COLD_SEG:mem_owned_kb_x ; in BX = owner word; out AX = KB
.cstore:
    mov [es:di+SSI_KB], ax
    add di, SSI_RECSZ
    inc si
    cmp si, INST_MAX
    jb .claim

    mov ax, MAX_TASKS
    mov bx, INST_MAX
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
section .bss

inst_tab:    resb 384           ; INST_MAX x I_RECSZ (12 x 32; literal per
                                ; the no-forward-ref-in-bss rule). It STAYS in
                                ; the kernel's own segment: I_NAME is handed
                                ; out as an ordinary near string pointer - a
                                ; Disk window's W_TITLE aims INTO it
                                ; (files.inc), and the dock, the menu bar and
                                ; the Task Manager all letter it through DS -
                                ; so moving the table to LOW_SEG would need a
                                ; segment beside every one of those pointers,
                                ; which is the MB_SEG trap of SPEC.md 12.2 for
                                ; 384 bytes

; THE INSTANCE'S OWN CURRENT DIRECTORY (SPEC.md 19.2.1), and the name it last
; picked in the Standard File dialog (SPEC.md 38.10).
;
; A side table rather than four more record fields: I_RECSZ is 32, it is
; full (I_CYC's dword ends it), and index<<5 is what makes a record cheap to
; reach - so widening the record to hold 17 bytes would cost every walk in
; the kernel a multiply. These are ordinary DS-relative data, so fdlg.inc
; reaches them from COLD code with no thunk involved.
;
; [disk_drive]/[dsk_cwd] remain the ONE position the hardware is actually at -
; there is one floppy in one drive and one mount snapshot describing it. What
; these words hold is where each instance BELIEVES it is standing, and
; inst_vol_enter is what makes the hardware agree, lazily, at the moment that
; instance does a file operation. So a Disk window taking the volume to C:
; moves the machine and moves nobody's idea of where their documents are.
inst_fdrv:   resb INST_MAX      ; the volume this instance is standing in...
inst_fcwd:   resw INST_MAX      ; ...and the directory inside it. Seeded at
                                ; inst_alloc from wherever the app was
                                ; LAUNCHED from, so an instance has a folder
                                ; from its first breath and there is no "not
                                ; set yet" case for a file operation to meet
inst_fpick:  resb INST_MAX      ; 0 = the user has never chosen a folder for
                                ; this app in a dialog, so the dialog opens on
                                ; the default (MEDIA, SPEC.md 38.10) rather
                                ; than on the launch folder above. A separate
                                ; byte and not a sentinel inside inst_fdrv,
                                ; because that word now always holds a real
                                ; volume - it is the answer a file operation
                                ; resolves against
inst_fname:  resb INST_MAX * INST_FNSZ  ; ...and the name it picked there,
                                ; NUL-terminated; an empty string = none

inst_parkreq: resb 1            ; SPEC.md 66.5: the compactor is asking every
                                ; package worker to stand still
inst_parksafe: resb INST_MAX    ; ...and which instances said the kernel may
                                ; park them while they are BLOCKED IN IT, not
                                ; only at OSAPI_TASK_ALIVE (SPEC.md 66.5.4).
                                ; Declared, never assumed: what a task's stack
                                ; holds while it spins in gfx_lock is the
                                ; package's business
sch_parked:   resb MAX_TASKS    ; ...and which TASKS have. PER TASK and not per
                                ; instance (SPEC.md 66.5.5): a driver's worker
                                ; has T_INST = 0xFF and no record to be parked
                                ; in, and an instance can have a package worker
                                ; and a driver's stream task at once

inst_launch: resw 1             ; deferred launch: 0 = none, else kind + 1
inst_knd:    resb 1             ; app_launch scratch: kind being launched
inst_slot:   resw 1             ; app_launch scratch: state-pool slot
inst_tplbuf: resb 16            ; app_launch scratch: cascaded template copy
inst_icobuf: resb 64            ; ...and inst_icon_ptr's staging slot, the
                                ; dsk_get_icon idiom (SPEC.md 25/30)


section .text
