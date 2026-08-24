; =============================================================================
; os8088 - tests/stackprobe/stackprobe.asm
;
; STKPROBE, the task-stack margin probe (SPEC.md 8/20.6). Like the gates and
; benchmarks it never ships on the apps disks - it rides its own scratch
; image:
;
;   make stackprobe                            (build both image sizes)
;   make test TESTAPPS=build/stkprobe.img      (drive it under QEMU)
;
; ...and the 360KB pair is the point: boot build/os8088-360.img on the real
; machine, put build/stkprobe360.img in the other drive (or swap it into the
; only one and open Disk A again), and launch STKPROBE.O88.
;
; WHAT IT MEASURES, AND WHY QEMU IS NOT ENOUGH. A background task owns a
; 384-byte stack slice (SCH_STACK, SPEC.md 8), sized against the deepest
; 0xCC-fill mark ever recorded - but that probe ran under QEMU, where
; SeaBIOS services its interrupt entries on an internal stack of its own.
; A real IBM BIOS runs int 09h on whichever task stack is current, and it
; STIs early, so the tick and the mouse IRQ nest ON TOP of the keyboard
; handler's frame. That nesting is exactly what this package exists to see:
; the worker fills its own slice with 0xCC, then SPINS, so every interrupt
; the machine takes while it is running lands on the watched slice, and a
; twice-a-second scan reports the deepest byte anything ever touched.
;
; Numbers to read: HIGH WATER is bytes used from the slice top, the worker's
; own few frames plus every ISR that landed (the kernel switch frame is 24
; of them); UNTOUCHED is what is left above the canary. Hold keys down, mash
; the mouse, play a Tracker module - then compare HIGH WATER against 384.
;
; **AND `OTHER TASKS` IS EVERY SLICE THAT IS NOT OURS**, with the slot it was
; found in. That is the line to watch while something else works: our own
; reading is the ISR cost and says nothing about how deep the network stack, a
; Tracker worker, or a package's own callback chain goes. It can do this
; because `task_spawn` fills EVERY slice with 0xCC on the shipping kernel now
; (SPEC.md 8.3) - so each one already carries its own high water and this only
; has to walk them. A slice with no canary was never spawned; one whose task
; has exited still reads true until the slot is reused.
; The canary line mirrors SPEC.md 8's SCH_MAGIC: if the probe cannot find it
; where the slice arithmetic says the slice bottom is, it says SLICE?
; and falls back to watching a 200-byte window under its own SP rather than
; inventing a base (and if the canary DIES, sch_switch halts the machine at
; the next switch - a halt during this test IS the overrun being reported).
;
; The slice top is SP at worker entry and the base is one SPB_SLICE below it,
; NOT SP rounded up to a 256-byte boundary: at 384 the odd slots sit at 128
; mod 256 and that rounding invented a top 128 bytes high. The canary check is
; what notices if SPB_SLICE ever stops matching the kernel's SCH_STACK.
;
; Prefix spb_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'StackProbe', spb_entry

SPB_MAGIC   equ 0x5A57          ; SPEC.md 8's SCH_MAGIC, mirrored: the word
                                ; at the bottom of every slice
SPB_SLICE   equ 384             ; SCH_STACK, mirrored (SPEC.md 8) - and it has
                                ; to move WITH it: the SDK publishes MAX_TASKS
                                ; and not the slice size, so this is the one
                                ; number in here a kernel change can falsify.
                                ; The canary test is what notices - a wrong
                                ; SPB_SLICE puts the base somewhere with no
                                ; SCH_MAGIC in it and the window says SLICE?
SPB_FILL    equ 0xCC            ; the fill byte, the kernel probe's own
SPB_WINF    equ 200             ; fallback watch window when the canary is
                                ; not where the aligned math says (bytes
                                ; below the worker's SP - safely inside the
                                ; slice while the worker is this shallow)
SPB_PERIOD  equ 9               ; ticks between scans, ~0.5s

; -----------------------------------------------------------------------------
; spb_entry - create the window; the worker spawns at first paint
; in:  DS = CS = our segment, ES = KERNEL_SEG, gfx lock NOT held
; out: CF = 0 and BX = the window - the loader BINDS the instance to the BX
;      this returns (inst_bind_win) and then shows it itself, so the entry
;      must neither restore the caller's BX nor call OSAPI_WM_SHOW. Getting
;      that wrong leaves the record unpublished (I_STATE 0, I_WIN 0) and
;      every later OSAPI_TASK_SPAWN is refused with nothing on screen to
;      say why. CF = 1 would abort the load.
; -----------------------------------------------------------------------------
spb_entry:
    push ax
    push si
    mov si, spb_tpl
    call OSAPI_WM_CREATE
    jc .fail
    mov [spb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP          ; 8-align the content so FONT_RUN's
                                ; single-store path letters the lines - every
                                ; adapter now (SPEC.md 11.94)
                                ; (preserves FLAGS, so the CF we owe the
                                ; loader is untouched)
    pop si
    pop ax
    clc
    ret
.fail:
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; spb_paint - W_PAINT: static labels, then the live numbers
; in:  SI = window ptr (ES = KERNEL_SEG), content white-filled, lock HELD
; -----------------------------------------------------------------------------
spb_paint:
    push ax
    push bx
    push cx
    push dx
    push si

    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov [spb_cx], ax
    mov [spb_cy], dx

    push ax
    mov al, 0                   ; CBLACK: [gfx_color] is whatever the last
    call OSAPI_SET_COLOR        ; drawer left it
    pop ax

    mov cx, ax
    add cx, 8
    mov dx, [spb_cy]
    add dx, 6
    mov si, spb_s_title
    call OSAPI_FONT_STR
    mov cx, [spb_cx]
    add cx, 8
    mov dx, [spb_cy]
    add dx, 62
    mov si, spb_s_hint1
    call OSAPI_FONT_STR
    mov cx, [spb_cx]
    add cx, 8
    mov dx, [spb_cy]
    add dx, 72
    mov si, spb_s_hint2
    call OSAPI_FONT_STR

    call spb_lines              ; the three live lines share one drawer

    cmp byte [spb_spawned], 0   ; first paint: claim the worker (lock held,
    jne .out                    ; callback context - the two things
    mov ax, spb_worker          ; OSAPI_TASK_SPAWN requires)
    mov bx, [spb_win]
    call OSAPI_TASK_SPAWN
    jc .out                     ; refused is transient: retry next paint
    mov byte [spb_spawned], 1
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; spb_lines - the three dynamic lines, at [spb_cx]/[spb_cy]; lock HELD, and
;             a clip region may be armed (FONT_RUN letters per cell under a
;             cut - SPEC.md 6.1.2 - so no gate is needed here)
; -----------------------------------------------------------------------------
spb_lines:
    push ax
    push cx
    push dx
    push si

    mov ax, [spb_max]           ; "High water:  NNN of 384"
    mov si, spb_n_used
    call spb_utoa
    mov cx, [spb_cx]
    add cx, 8
    mov dx, [spb_cy]
    add dx, 20
    mov si, spb_s_used
    mov ax, 0x0F00              ; AL = ink 0 (black), AH = background 15
    call OSAPI_FONT_RUN

    mov ax, [spb_free]          ; "Untouched:   NNN"
    mov si, spb_n_free
    call spb_utoa
    mov cx, [spb_cx]
    add cx, 8
    mov dx, [spb_cy]
    add dx, 30
    mov si, spb_s_free
    mov ax, 0x0F00
    call OSAPI_FONT_RUN

    mov si, spb_s_cok           ; "Canary OK  " / "SLICE?     " + samples
    cmp byte [spb_mode], 1
    je .canary
    mov si, spb_s_cbad
.canary:
    push si
    mov ax, [spb_scans]
    mov si, spb_n_scan
    call spb_utoa
    pop si
    mov cx, [spb_cx]
    add cx, 8
    mov dx, [spb_cy]
    add dx, 40
    mov ax, 0x0F00
    call OSAPI_FONT_RUN
    mov ax, 0x0F00
    mov cx, [spb_cx]
    add cx, 8 + 12*8            ; the sample counter, after the canary text
    mov si, spb_s_scan
    call OSAPI_FONT_RUN

    mov ax, [spb_any]           ; "Other tasks: NNN slot NNN" - EVERY OTHER
    mov si, spb_n_any           ; slice's high water, not just the one we are
    call spb_utoa               ; spinning in (SPEC.md 8.3)
    mov al, [spb_anyt]
    xor ah, ah
    mov si, spb_n_anyt
    call spb_utoa
    mov cx, [spb_cx]
    add cx, 8
    mov dx, [spb_cy]
    add dx, 50
    mov si, spb_s_any
    mov ax, 0x0F00
    call OSAPI_FONT_RUN

    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; spb_worker - the probe itself (SPEC.md 20.6: our one background task)
;
; Fill once, then spin-and-scan forever. The spin is the measurement: while
; this task is running, every interrupt the machine takes pushes its frames
; onto THIS slice, which is the thing being watched. OSAPI_TASK_ALIVE every
; loop is the teardown contract - the close box makes it never return.
; -----------------------------------------------------------------------------
spb_worker:
    ; --- find the slice: top = SP at entry, base = top - SPB_SLICE ---------
    mov ax, sp                  ; SP IS the slice top at worker entry: the
    mov [spb_top], ax           ; scheduler pops the spawn frame before the
    sub ax, SPB_SLICE           ; proc runs, and nothing is pushed yet. The
    mov [spb_base], ax          ; old (SP-1)|255,+1 round-up assumed 256-
                                ; aligned tops; slices are 384 apart now and
                                ; odd slots sit at 128 mod 256, so it invented
                                ; a top 128 high and read mid-slice 0xCC where
                                ; the canary lives - SLICE? on a healthy slot

    mov bx, ax                  ; the canary should be the word at [ss:base]
    mov ax, [ss:bx]
    cmp ax, SPB_MAGIC
    jne .window
    mov byte [spb_mode], 1
    mov ax, [spb_base]
    add ax, 2                   ; never touch the canary word itself
    mov [spb_flo], ax
    mov ax, [spb_top]
    mov [spb_ceil], ax

    ; --- ...and WHERE THE SLICES START, so every one can be read ------------
    ; Slot n's slice is sch_stacks + (n-1)*SCH_STACK and they are contiguous,
    ; so walking DOWN from ours while a canary keeps appearing lands on slot
    ; 1's. Bounded by MAX_TASKS, because below sch_stacks is the font's glyph
    ; block and 0x5A57 can occur in it by chance - one step too far would
    ; invent a slice and read the glyphs as a stack.
    mov bx, [spb_base]
    mov cx, MAX_TASKS
.down:
    mov si, bx                  ; SI probes, BX keeps the last CONFIRMED base -
    sub si, SPB_SLICE           ; the first version stepped BX itself and left
    cmp word [ss:si], SPB_MAGIC ; it one slice BELOW sch_stacks on the way out,
    jne .bottom                 ; so every slot was numbered one high and the
    mov bx, si                  ; probe skipped the wrong one as "ours"
    loop .down
.bottom:
    mov [spb_first], bx
    mov ax, [spb_base]          ; ...and which slot WE are, so the reading
    sub ax, bx                  ; below can leave us out: the slices are
    xor cx, cx                  ; SPB_SLICE apart, and SPB_SLICE is 384 -
.slot:                          ; the old high-byte trick divided by 256 and
    inc cx                      ; numbered every slot past 2 wrong, so the
    sub ax, SPB_SLICE           ; scan excluded the wrong slot as 'ours'.
    jnc .slot                   ; A subtract loop is the 8086's div here:
    mov ax, cx                  ; MAX_TASKS iterations at most
    mov [spb_slot], al
    jmp short .fill
.window:                        ; canary not found: the aligned math missed
    mov byte [spb_mode], 0      ; (SLICE? on screen) - watch a window under
    mov ax, sp                  ; our own SP instead of inventing a base
    sub ax, SPB_WINF
    mov [spb_flo], ax
    mov ax, sp
    mov [spb_ceil], ax

.fill:                          ; 0xCC over [flo, SP) - everything below SP
    pushf                       ; is free space where future pushes (ours
    cli                         ; and every ISR's) will land; IF=0 so a
    mov ax, ss                  ; mid-fill interrupt cannot leave a mark the
    mov es, ax                  ; rest of the fill erases
    mov di, [spb_flo]
    mov cx, sp
    sub cx, di
    mov al, SPB_FILL
    cld
    rep stosb
    popf

    call OSAPI_GET_TICKS
    mov [spb_lastt], ax

    ; --- the loop: stay busy, scan twice a second ---------------------------
.loop:
    mov bx, [spb_win]
    call OSAPI_TASK_ALIVE       ; never returns once the close box is hit

    mov cx, 6000                ; the busy phase: cheap, pointless, RUNNING -
.spin:                          ; which is what parks the IRQ frames here
    nop
    loop .spin

    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [spb_lastt]
    cmp ax, SPB_PERIOD
    jb .loop
    mov [spb_lastt], dx

    ; scan from the low end for the first byte anything overwrote
    mov bx, [spb_flo]
.scan:
    cmp byte [ss:bx], SPB_FILL
    jne .mark
    inc bx
    cmp bx, [spb_ceil]
    jb .scan
.mark:
    mov ax, [spb_ceil]          ; high water = ceil - first touched byte
    sub ax, bx
    cmp ax, [spb_max]
    jbe .free
    mov [spb_max], ax
.free:
    mov ax, bx                  ; untouched = first touched - watch floor
    sub ax, [spb_flo]
    mov [spb_free], ax
    inc word [spb_scans]

    call spb_scan_all           ; every OTHER slice, which is the whole reason
                                ; this probe is worth running while something
                                ; else is busy (SPEC.md 8.3). BEFORE the canary
                                ; block below and not after it: both of that
                                ; block's exits are `je .draw`, so a call
                                ; appended to its tail runs ONLY when the
                                ; canary has died - which reads exactly like a
                                ; scan that finds nothing

    cmp byte [spb_mode], 1      ; re-verify the canary while we are here -
    jne .draw                   ; if it ever dies the next switch halts the
    mov bx, [spb_base]          ; machine (sch_stkdie), so this line mostly
    mov ax, [ss:bx]             ; exists to prove the probe watched the
    cmp ax, SPB_MAGIC           ; right word
    je .draw
    mov byte [spb_mode], 0

.draw:
    call OSAPI_GFX_LOCK
    mov bx, [spb_win]
    call OSAPI_WM_CLIP_SET      ; CF=1: nothing of us is visible this frame
    jc .undraw
    call spb_lines
.undraw:
    call OSAPI_GFX_UNLOCK
    jmp .loop

; -----------------------------------------------------------------------------
; spb_scan_all - the deepest of every slice that is NOT ours
;
; **This is what makes the probe worth running while something else works.**
; The line above it reports OUR slice, which is where every interrupt lands
; while we spin - a good reading of what the ISRs cost and no reading at all of
; what the network stack, or a Tracker worker, or a package's own callback
; chain is doing. `task_spawn` fills every slice with 0xCC (SPEC.md 8.3), so
; each one already carries its own high water; this walks them.
;
; A slice with no canary was never spawned and is skipped. A slice whose task
; has EXITED keeps its fill and still reads true - the deepest that task got -
; until the slot is reused, when the next spawn refills it. So the number is
; live rather than latched, and a re-launch legitimately resets it.
;
; in:  [spb_first] = slot 1's base, [spb_slot] = ours (spb_worker's set-up)
; out: [spb_any] = the deepest, [spb_anyt] = whose. Preserves nothing.
; -----------------------------------------------------------------------------
spb_scan_all:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [spb_any], 0
    mov byte [spb_anyt], 0
    mov bx, [spb_first]
    mov dl, 1                   ; DL = the slot BX is the base of
.slice:
    cmp dl, [spb_slot]
    je  .next                   ; ours: the High water line already has it
    cmp word [ss:bx], SPB_MAGIC
    jne .next                   ; never spawned - no canary, no fill
    mov si, bx
    inc si
    inc si                      ; ...and never read the canary word itself
.byte:
    cmp byte [ss:si], SPB_FILL
    jne .depth
    inc si
    mov ax, si
    sub ax, bx
    cmp ax, SPB_SLICE
    jb  .byte
.depth:
    mov ax, bx                  ; depth = top - the first byte anything touched
    add ax, SPB_SLICE
    sub ax, si
    cmp ax, [spb_any]
    jbe .next
    mov [spb_any], ax
    mov [spb_anyt], dl
.next:
    add bx, SPB_SLICE
    inc dl
    cmp dl, MAX_TASKS
    jb  .slice
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; spb_utoa - AX unsigned -> 3 right-aligned decimal digits at DS:SI
; (values here are 0..SPB_SLICE; 999 caps the display, not the measurement)
; -----------------------------------------------------------------------------
spb_utoa:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp ax, 999
    jbe .in
    mov ax, 999
.in:
    mov bx, 10
    mov cx, 3
    add si, 2                   ; write digits backwards
.digit:
    xor dx, dx
    div bx
    add dl, '0'
    mov [si], dl
    dec si
    loop .digit
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template: x, y, w, h, title, paint, onkey, onclick ---------------
spb_tpl:    dw 150, 120, 272, 112, spb_s_wt, spb_paint, 0, 0
spb_s_wt:   db 'Stack Probe', 0

spb_s_title: db 'Worker slice: 384 bytes', 0
spb_s_used: db 'High water:  '
spb_n_used: db '  0 of 384', 0
spb_s_free: db 'Untouched:   '
spb_n_free: db '  0', 0
spb_s_cok:  db 'Canary OK   ', 0
spb_s_cbad: db 'SLICE?      ', 0
spb_s_scan: db 'x'
spb_n_scan: db '  0', 0
spb_s_any:   db 'Other tasks: '
spb_n_any:   db '  0 slot '
spb_n_anyt:  db '  0', 0
spb_s_hint1: db 'Type, mouse, transfer - then', 0
spb_s_hint2: db 'read the two numbers.', 0

; --- state (image data, not bss: a fresh copy per launch either way) ---------
spb_win:     dw 0
spb_spawned: db 0
spb_mode:    db 0               ; 1 = full slice (canary found), 0 = window
spb_base:    dw 0               ; slice bottom (mode 1)
spb_top:     dw 0               ; slice top (mode 1)
spb_flo:     dw 0               ; first watched byte
spb_ceil:    dw 0               ; one past the watch (SP at fill / slice top)
spb_max:     dw 0               ; deepest high water ever seen, bytes
spb_free:    dw 0               ; untouched bytes above the floor, latest
spb_scans:   dw 0
spb_first:   dw 0               ; slot 1's slice base - the bottom of sch_stacks
spb_slot:    db 0               ; ...and which slot WE are, so the line below
spb_any:     dw 0               ; can leave us out: the deepest OTHER slice,
spb_anyt:    db 0               ; and whose
spb_lastt:   dw 0
spb_cx:      dw 0               ; content origin, banked by the last paint
spb_cy:      dw 0

    OS88_IMAGE_END
