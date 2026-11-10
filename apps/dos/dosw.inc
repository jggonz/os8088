; =============================================================================
; os8088 - apps/dos/dosw.inc
;
; **A DOS PROGRAM IN ITS WINDOW, LIVE** (docs/plans/DOS-WINDOWED-PLAN.md wave 1,
; SPEC.md 96.54). The bracket (96.2) hands a program the whole machine and the
; desktop stands still behind it. This runs the program on the instance's own
; WORKER instead, so the scheduler pre-empts it like any other task: its INT
; 21h output lands in the console band as it is printed, and the rest of the
; desktop - and other DOS windows - go on running.
;
; **NOT ONE KERNEL BYTE.** Everything a pre-empted DOS program needs that the
; kernel does not give a worker is here, and it rests on one kernel fact,
; SPEC.md 8.1.2.2: ONLY IRQ0 FALLS INTO `sch_switch`. So while this instance's
; task holds the CPU - and only then - its own world is in the interrupt table:
;
;   * the INT 21h family, a virtual video BIOS (10h), a virtual keyboard
;     BIOS (16h) fed from the window's keys, a refusing 13h - and IVT 08h, so
;     the timer interrupt arrives HERE first (`dvw_irq0`);
;   * the BDA's video block, so a C runtime reading 0040:0049 sees mode 3 and
;     80 columns and not the desktop's graphics mode.
;
; `dvw_irq0` moves a foreign SS (the program's stack, or the core's) onto the
; worker's own LOW_SEG slice, EXCHANGES the instance's world OUT, and calls
; the kernel's own int 08h - which may switch tasks. The switch saves the
; whole frame, including this stub's return, so when this task is resumed it
; comes back through the stub, which exchanges the world IN again. Every
; VOLUNTARY way off the CPU is a call this file makes (`dvw_nap`), and it
; exchanges out first. So the invariant is exact: **an instance's vectors are
; in the IVT exactly while its own task runs**, every other task sees the
; desktop's, and a teardown at any moment needs no cleanup at all.
;
; **IVT 08h AND 09h ARE EXCHANGED, NOT REFUSED.** A program that hooks the
; timer or the keyboard through AH=25h and chains - Microsoft Works does both
; - then sees its own handler run on every IRQ during its own slices and
; never during anyone else's. One that does NOT chain 08h stops the machine,
; exactly as it would wedge any other multitasker of the period.
;
; A worker may not touch a file (SPEC.md 20.6 rule 7), so every `dos_be` door
; that reaches the disk is POSTED to the UI task (`dvw_post`), which runs it
; through the very routine the bracket uses, on its own stack. 96.4.1.1's
; test decides which: `DBE_HERE` and `DBE_VKIND` are table reads and stay.
;
; WHAT IS NOT HERE: a lone Hercules's second page is never switched on for a
; capture (96.54.6); a program that reprograms the desktop card's 6845 itself
; is not repaired (96.54.7); and the vector set is not narrowed, so a window
; costs a CPU-bound program ~12% (96.54.3). The mouse is doswm.inc's
; (96.54.4), Alt+Enter and the shunt dvw_fsmain's (96.54.1), captures and
; TopView dvw_capt's (96.54.5-6).
; =============================================================================

%ifdef DVW
%include "doswabi.inc"              ; the boundary's macros (SPEC.md 96.54.9)

DVWC_TV     equ 1               ; [dvw_cmode]: TopView's buffer is con_scr
DVWC_SEG    equ 2               ; ...the other card's text RAM is captured
DVWC_HP     equ 3               ; ...a lone Hercules's page 1 is WATCHED
DVW_KBUSY   equ 18              ; ticks after a key in which a poll does not
                                ; sleep (SPEC.md 96.54.5.1)
DVWC_OWN    equ 0x0FFE          ; the card's word that names its holder
                                ; (96.54.13): past 80x25's 4,000 bytes and
                                ; under 4K, so on an MDA, a CGA's page 0 and
                                ; a Hercules's page 1 alike nothing shows it
DVWC_EVERY  equ 3               ; a capture pass every 4th tick (mask)
%ifndef DVW_SPACE
DVW_SPACE   equ 4               ; a SCROLL's paint at most every 4th tick from
%endif                          ; the program's own slices (SPEC.md 96.54.14)

DVW_GAP     equ 96              ; the IRQ0 stub's frames go this far below the
                                ; gate's own frame top ([dos_dstk]), so a
                                ; program-side tick never lands on a gate
                                ; frame. Only a CALLBACK (INT 33h's event
                                ; handler, wave 5) can have both live at once

; --- the exchange block's layout (`dvw_walk` writes it in this order) -------
DVW_XB_IRQ  equ 4               ; vector 08h's slot: 00h (4), then 08h-0Fh
DVW_XB_V10  equ DVW_XB_IRQ + 8 * 4  ; vector 10h's slot, after them
DVW_XB_V16  equ DVW_XB_V10 + (0x16 - 0x10) * 4
DVW_XB_BDA  equ (2 + 16 + 0x30 * 2) * 2 ; 228: 0040:0013's word, then...
DVW_XB_VID  equ DVW_XB_BDA + 2  ; ...0040:0049..0066 (30), 0040:0084..0089 (6)
DVW_XB_SIZE equ DVW_XB_VID + 36

%ifndef DVW_SPLIT                   ; --- [P]: the part (doswp.asm) ---------
; -----------------------------------------------------------------------------
; dvw_copy - both directions of one exchange: the runs, as `rep movsw`
; in:  BX = the block each live run is copied INTO, DX = the block each live
;      run is then put back FROM; IF = 0
; out: nothing; every register preserved
;
; Six runs, and the list is the contract: IVT 00h (divide - a C runtime hooks
; it), 08h-0Fh (the master PIC's eight IRQs: 08h and 09h a program chains,
; and 0Ah-0Fh are ITS while it runs, so a run that hooks one leaves it in its
; own world and never in another run's bank - SPEC.md 96.54.12), 10h-3Fh
; (the BIOS services it is given virtual ones of, and the whole DOS block);
; then the BDA's memory size word, its video block and its EGA rows
; (0040:008Ah on is the AT's floppy state, which is the kernel's).
;
; **TWO BLOCKS AND COPIES, NOT ONE BLOCK AND AN EXCHANGE** (SPEC.md 96.54.3).
; It was one routine walking a word at a time - read, `xchg`, store, and a
; test of a mode byte on every word - which an exchange needs, being its own
; inverse. MEASURED: 15.3% of every sample of a windowed CPU-bound program
; landed in that loop, and the program ran at 81.1% of its bracketed speed.
; Two `rep movsw` per run is the same 133 words each way at a fraction of
; the clocks, for one more block of bss; IN takes the desktop's copy first
; and OUT puts it back, so nothing the desktop holds can drift in between -
; no desktop code runs while the world is in. Each run is GOT and then PUT
; before the next, which is the same as all gets then all puts because no
; two runs overlap. BX = DX is a GET alone (the PUT writes back what it read:
; dvw_runprog's priming).
; -----------------------------------------------------------------------------
dvw_runs:
    dw 0x0000, 0x00 * 4, 2          ; 00h
    dw 0x0000, 0x08 * 4, 8 * 2      ; 08h..0Fh
    dw 0x0000, 0x10 * 4, 0x30 * 2   ; 10h..3Fh
    dw 0x0040, 0x0013, 1            ; INT 12h's source (dos_hook_vectors')
    dw 0x0040, 0x0049, 15           ; mode .. palette
    dw 0x0040, 0x0084, 3            ; rows-1, char height, EGA info
    dw 0, 0, 0
dvw_copy:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    cld
    mov bp, PADDR(dvw_runs)
.r:
    mov cx, [cs:bp + 4]
    jcxz .done
    mov ds, [cs:bp]                 ; GET: the live run -> CS:BX
    mov si, [cs:bp + 2]
    PARTES
    mov di, bx
    push cx
    rep movsw   ; DVWCHK-OK: the live run -> the block (ES the part)
    pop cx
    mov bx, di
    mov es, [cs:bp]                 ; PUT: CS:DX -> the live run
    mov di, [cs:bp + 2]
    PARTDS
    mov si, dx
    rep movsw   ; DVWCHK-OK: the block (DS the part) -> the live run
    mov dx, si
    add bp, 6
    jmp short .r
.done:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; dvw_xflip - the world IN when it is out and OUT when it is in, flipping
; [dvw_in]; IF = 0, every register preserved
dvw_xflip:
    push bx
    push dx
    mov bx, PADDR(dvw_xdsk)                ; IN: the desktop's world, kept...
    mov dx, PADDR(dvw_xblk)                ; ...and the program's, put
    xor byte [cs:dvw_in], 1
    jnz .go
    xchg bx, dx                     ; OUT: the program's kept, the desktop's
.go:                                ; put back
    call dvw_copy
    pop dx
    pop bx
    ret

dvw_xin:                        ; IF = 0; preserves everything
    cmp byte [cs:dvw_in], 0
    jne .r
    call dvw_xflip
    cmp byte [cs:dvw_fs], 0     ; (not on the full screen: the card is in
    jne .r                      ; TEXT there, and 3B8h = 0Ah is graphics)
    cmp byte [cs:dvw_chp], 0    ; A LONE HERCULES'S PAGE 1 (96.54.6.1): its
    je .r                       ; decode allowed again, a mode set or an
    push ax                     ; unblank having put 3BFh back to 1 while
    mov al, 3                   ; the world was out
    call dvw_hpage
    pop ax
.r:
    ret

; dvw_hpage - 3BFh = AL, then 3B8h = the desktop's own 0Ah (96.54.6.1).
; The mode register is written again because a card may latch its decode
; there (MartyPC's re-derives its memory mask on 3B8h and nowhere else), and
; 0Ah is what dvw_vguard writes anyway: graphics, page 0 SHOWN, video on.
; Preserves all but AL and flags
dvw_hpage:
    push dx
    mov dx, 0x3BF
    out dx, al
    mov dl, 0xB8
    mov al, 0x0A
    out dx, al
    pop dx
    ret

dvw_xout:                       ; IF = 0; preserves everything
    cmp byte [cs:dvw_in], 0
    je dvw_xin.r
    call dvw_xflip
    ; fall through: the desktop card's mode register, the world being out

; -----------------------------------------------------------------------------
; dvw_vguard - put the DESKTOP card's mode register back (SPEC.md 96.54.7)
; in:  IF = 0, the world out; out: every register preserved
;
; **A PROGRAM MAY WRITE A CARD'S PORTS, AND AN 8086 CANNOT STOP IT.** Measured
; with Microsoft Works: its blink setting writes the same byte to BOTH mode
; registers, 3B8h and 3D8h, and to the attribute controller at 3C0h - so on
; a Hercules desktop bit 1 drops, the card falls to TEXT, and the desktop's
; graphics framebuffer is shown as characters until something sets a mode.
; Works touches no 6845 register, so the mode register is the whole repair,
; and it is written each time the world goes out: one OUT on a Hercules or a
; CGA, three on a planar card, against a register the program may have
; written in the slice that just ended. A clobber shows for at most a tick.
; Never on the full screen, where the card is the program's.
; -----------------------------------------------------------------------------
dvw_vguard:
    cmp byte [cs:dvw_fs], 0
    jne .r
    push ax
    push dx
    mov al, [cs:dvw_dkind]
    cmp al, VID_HERC
    je .herc
    cmp al, VID_CGA
    je .cga
    cmp al, VID_VGA
    je .planar
    cmp al, VID_EGA
    jne .out                        ; VESA: its own driver's business
.planar:
    mov dx, 0x3DA                   ; reset the AC's flip-flop to INDEX
    in al, dx
    mov dl, 0xC0                    ; (DH = 3 still)
    mov al, 0x30                    ; register 10h, palette address source on
    out dx, al
    mov al, 0x01                    ; mode control: graphics, no blink - mode
    out dx, al                      ; 12h's and mode 10h's alike
    jmp short .out
.herc:
    mov dx, 0x3B8
    mov al, 0x0A                    ; graphics, page 0, video on: what
    out dx, al                      ; vid_unblank_kind writes
    jmp short .out
.cga:
    mov dx, 0x3D8
    mov al, [cs:dvw_xdsk + DVW_XB_VID + 0x1C]  ; 0040:0065, the DESKTOP'S
    out dx, al                      ; 3D8h as its mode set left it
.out:
    pop dx
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; dvw_irq0 - IVT 08h while this instance runs (the file header's whole design)
; -----------------------------------------------------------------------------
dvw_irq0:
    mov [cs:dvw_tax], ax
    mov ax, ss                  ; a FOREIGN stack - the program's, or a
    sub ax, [cs:dvw_lss]        ; child's: onto the worker's own slice, below
    jz .go                      ; the gate's frame top, where the kernel's
    call dvw_tolow              ; scheduler can park it. IF is 0 from the
.go:                            ; gate to the iret (dvw_tolow)
    push ax                     ; NZ = this frame moved off a foreign stack
    mov ax, [cs:dvw_tax]
    call dvw_xpostt             ; the world out, and the paint posted
    pushf
    call far [cs:dvw_k08]       ; the kernel's own tick: it may switch away,
                                ; and resuming this task irets HERE (IF = 0)
    cmp byte [cs:dvw_frz], 0    ; **FROZEN** (SPEC.md 96.54.1): the world
    je .live                    ; stays out and the task sleeps here until
    call dvw_park               ; Alt+Enter thaws it or a close kills it
.live:
    call dvw_xin
    mov [cs:dvw_tax], ax
    pop ax
    or ax, ax
    jz .ret
    mov ss, [cs:dvw_fss]
    mov sp, [cs:dvw_fsp]
    cmp byte [cs:dvw_kill], 0   ; **THE WINDOW IS CLOSING**: the program
    je .ret                     ; leaves through DOS's own terminate, so its
    push bp                     ; files are swept and closed (96.52) - the
    mov bp, sp                  ; interrupted CS:IP becomes dvw_kexit's.
    mov word [bp+2], PADDR(dvw_kexit)  ; Only from program code: in a handler the
    PARTCS [bp+4]              ; next tick out of it does this
    pop bp
.ret:
    mov ax, [cs:dvw_tax]
    iret

dvw_kexit:
    in al, 0x61                 ; **THE SPEAKER OFF**: a program stopped
    and al, 0xFC                ; between turning it on and off (a BEEP, a
    out 0x61, al                ; PLAY note) would leave the tone running for
    mov ax, 0x4CFF              ; ever - nothing of it is left to turn it off
    int 0x21

; dvw_tolow - off a FOREIGN stack onto the worker's own slice, DVW_GAP below
; the gate's frame top; the foreign SS:SP (our return popped) into
; dvw_fss:dvw_fsp. in: IF = 0, AX free; out: AX = our return, so NONZERO.
; dvw_irq0 and dvw_nap share the two words: each holds them only with IF = 0
; or with the world OUT, and with the world out IVT 08h is not dvw_irq0, so
; neither can land inside the other. SS and SP move as a pair with nothing
; able to land between
dvw_tolow:
    ; STKBALANCE-OK: the return is popped into AX and taken by `jmp ax` -
    ; the stack it was on is not the one we leave on
    pop ax
    mov [cs:dvw_fss], ss
    mov [cs:dvw_fsp], sp
    DVW_LOWSTK                      ; SS = LOW_SEG, SP = the gate's mark
    sub sp, DVW_GAP
    jmp ax      ; DVWCHK-OK: our own return, popped above

; dvw_xpost - the world OUT, a capture's pass (96.54.6), and the band's paint
; posted if the console owes one; IF = 0, every register preserved.
; **THE PAINT IS POSTED FROM HERE**: once a tick, whatever the program
; printed, so a chatty program costs the band 18 repaints a second at most
; and a quiet one none. OSAPI_WM_WAKE is ISR-safe and coalesces
dvw_xpostt:                     ; ...from a TICK the program was holding:
    mov byte [PS dvw_ptk], 1    ; its scroll's paint is paced (below)
dvw_xpost:
    call dvw_xout               ; the desktop's world, sch_isr back at 08h
    sub byte [cs:dvw_kbusy], 1  ; a tick of 96.54.5.1's window spent - and
    adc byte [cs:dvw_kbusy], 0  ; none below zero
    push ds
    HOSTDS
    cmp byte [dvw_dirty], 0     ; printed or marked since the last tick: a
    je .quiet                   ; key's burst is still going (96.54.5.1) -
    call dvw_kmore              ; tested BEFORE dvw_cpace, whose post is the
.quiet:                         ; pass's door and not a change
    call dvw_cpace              ; (DS the host: its .post marks the band)
    cmp byte [PS dvw_ptk], 0    ; the ticks since the last post, saturating
    je .gap
    inc byte [PS dvw_pgap]
    jnz .gap
    dec byte [PS dvw_pgap]
.gap:
    cmp byte [dvw_dirty], 0
    je .r
    ; **A SCROLL'S PAINT IS PACED** (SPEC.md 96.54.14). The band follows a
    ; scroll by moving every pixel of it that stays - 192 scanlines of a
    ; Hercules band, the largest single cost of a program that prints - and
    ; a teletype that runs at a line a tick paid that move once a LINE. Held
    ; to every DVW_SPACE'th tick, one move carries the lines of all of them.
    ; Only from a tick, and only with a scroll owed: a nap is the program
    ; WAITING, which is exactly when what it printed must be on the glass,
    ; and a screen that only changed in place is cells, not a move
    cmp byte [PS dvw_ptk], 0
    je .post
    cmp word [con_scrl], 0
    je .post
    cmp byte [PS dvw_pgap], DVW_SPACE
    jb .r
.post:
    mov byte [PS dvw_pgap], 0
    mov byte [dvw_dirty], 0
    push bx
    mov bx, [dos_win]
    call OSAPI_WM_WAKE
    pop bx
.r:
    mov byte [PS dvw_ptk], 0
    pop ds
    ret

; -----------------------------------------------------------------------------
; dvw_nap1 / dvw_nap0 - give up the CPU with the instance's world OUT, for
; a tick's sleep or a yield ([dvw_narg]: 0 yields, else ticks to sleep)
; in:  any stack; out: every register and the flags preserved
; -----------------------------------------------------------------------------
dvw_nap1:
    mov word [cs:dvw_narg], 1
    jmp short dvw_nap
dvw_nap0:
    mov word [cs:dvw_narg], 0
dvw_nap:
    pushf
    cli
    push ax                     ; the argument, on the CALLER's stack
    mov ax, ss                  ; foreign: the scheduler wants LOW_SEG, and so
    sub ax, [cs:dvw_lss]        ; does every OSAPI cell (os88api.inc's head)
    jz .go
    call dvw_tolow
.go:
    push ax
    call dvw_xpost                  ; **AND THE PAINT IS POSTED HERE TOO**:
    push ds                         ; dvw_irq0 posts it only when a tick lands
    HOSTDS                         ; on the program, and a program that
                                    ; prints its question and then waits for
    sti                             ; the answer spends every tick asleep in
                                    ; here - so its question reached the band
                                    ; only when something else repainted the
                                    ; window (reported off Prince of Persia's
                                    ; installer: "it updates after a drag")
    cmp word [PS dvw_narg], 0      ; read back through our own cell: the
    je .yield                   ; argument is on the other stack now
    mov ax, [PS dvw_narg]
    call OSAPI_TASK_SLEEP
    jmp short .woke
.yield:
    call OSAPI_TASK_YIELD
.woke:
    cmp byte [PS dvw_frz], 0       ; the other way off the CPU parks too, so a
    je .nofrz                   ; program frozen inside a key wait or a
    call dvw_park               ; posted door is held as surely as one in
.nofrz:                         ; its own code
    cli
    pop ds
    call dvw_xin
    pop ax
    or ax, ax
    jz .back
    mov ss, [cs:dvw_fss]
    mov sp, [cs:dvw_fsp]
.back:
    call dwm_kick               ; the mouse ticks it slept through (96.54.4)
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; dvw_park - sleep, world OUT, while [dvw_frz] stands (SPEC.md 96.54.1)
; in:  LOW_SEG stack, the world out; out: IF = 0, every register preserved
; [dvw_parked] is what the UI task waits on before it reads the program's
; screen, so nothing the program does can land between the read and the thaw.
; -----------------------------------------------------------------------------
dvw_park:
    push ax
    push ds
    HOSTDS
    mov byte [PS dvw_parked], 1
    sti
.p:
    cmp byte [PS dvw_kill], 0          ; a close thaws it: the kill is heard on
    jne .go                         ; the way back to the program
    cmp byte [PS dvw_frz], 0
    je .go
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short .p
.go:
    cli
    mov byte [PS dvw_parked], 0
    pop ds
    pop ax
    ret

; -----------------------------------------------------------------------------
; dvw_post - a door, run by the UI task (SPEC.md 20.6 rule 7)
; reached as `call dvw_post / dw dos_k_xxx` from dvw_betab's stubs, with the
; door's own registers; out: the door's registers and flags
;
; **THREE STATES AND NOT TWO**: 1 posted, 2 being served, 0 done. A window
; closing while a request stands may only CANCEL one the UI task has not
; started - a served door is running OUR code on the UI task, and the
; instance teardown that follows a kill would free it under that task.
; -----------------------------------------------------------------------------
dvw_post:
    ; STKBALANCE-OK: the `pop bx` below takes the STUB's return - the address
    ; of the `dw` after its `call dvw_post` - as data, so dvw_rpop's `ret`
    ; returns to whoever called the stub, one word above this routine's entry
    pushf                       ; the ten, AX lowest, banked by dvw_rsave
    push ds
    push es
    push bp
    push di
    push si
    push dx
    push cx
    push bx
    push ax
    call dvw_rsave
    add sp, 20
    pop bx                      ; the stub's return IS the address of its word
    HOSTDS
                                ; DS = CS from here: dvw_nap0 keeps it
    mov bx, [bx]   ; DVWCHK-OK: the HOST stub's word, DS the host
    mov [dvw_rtgt], bx
    mov byte [PS dvw_rq], 1
.wait:
    mov bx, [dos_win]
    call OSAPI_WM_WAKE          ; every pass: a full ring refuses one
    call dvw_nap0
    cmp byte [PS dvw_rq], 0
    je .done
    cmp byte [PS dvw_kill], 0
    je .wait
    pushf
    cli
    cmp byte [PS dvw_rq], 1
    jne .busy
    mov byte [PS dvw_rq], 0        ; cancelled before it started
    popf
    mov word [PS dvw_r_ax], 5      ; DOS's ACCESS DENIED, as good as any: the
    or byte [PS dvw_r_fl], 1       ; program is about to be stopped. CF = 1 on
.done:                          ; the caller's own flags, the rest its own
    sub sp, 20
    call dvw_rload
    DVW_POSTRET
.busy:
    popf
    jmp short .wait

; dvw_rsave / dvw_rload - the ten words just above the caller's return (AX
; lowest, then BX CX DX SI DI BP ES DS and the FLAGS: the order the dvw_r_*
; block is laid out in) into the block, or the block into them. dvw_rpop
; then pops the ten. Both clobber AX CX SI DI DS ES and the flags, which the
; ten already hold
dvw_rsave:
    push ss
    pop ds
    mov si, sp
    PARTES
    mov di, PADDR(dvw_r_ax)
    cld
    lodsw   ; DVWCHK-OK: SS's word, read past                       ; past our return
    jmp short dvw_rmv
dvw_rload:
    PARTDS
    mov si, PADDR(dvw_r_ax)
    push ss
    pop es
    mov di, sp
    cld
    scasw   ; DVWCHK-OK: ES is SS, read past                       ; past our return
dvw_rmv:
    mov cx, 10
    rep movsw   ; DVWCHK-OK: SS <-> the part's block
    ret
dvw_rpop:
    pop ax
    pop bx
    pop cx
    pop dx
    pop si
    pop di
    pop bp
    pop es
    pop ds
    popf
    ret

; -----------------------------------------------------------------------------
; dvw_serve - the UI task's half of dvw_post (from dos_wake, lock FREE)
; -----------------------------------------------------------------------------
dvw_serve:
    pushf
    cli
    cmp byte [PS dvw_rq], 1
    jne .no
    mov byte [PS dvw_rq], 2
    popf
    pushf                       ; the UI task's own ten, for dvw_rpop
    push ds
    push es
    push bp
    push di
    push si
    push dx
    push cx
    push bx
    push ax
    sub sp, 20
    call dvw_rload              ; the posted ten (their flags are not used)
    pop ax
    pop bx
    pop cx
    pop dx
    pop si
    pop di
    pop bp
    pop es
    pop ds
    HICALL dvw_rtgt     ; ...the posted FLAGS' word still on top
    pushf                       ; the door's ten, over it
    push ds
    push es
    push bp
    push di
    push si
    push dx
    push cx
    push bx
    push ax
    call dvw_rsave
    add sp, 22                  ; ...and the posted flags' word with them
    mov byte [cs:dvw_rq], 0     ; LAST, after every answer is in place
                                ; (DS is SS: dvw_rsave)
    jmp short dvw_rpop
.no:
    popf
    ret

%endif                              ; [P]
%ifndef DVW_PART                    ; --- [R]: RESIDENT in a split host ------
                                    ; the core near-calls these addresses
; --- the windowed door table: dos_betab's order, posting all but two --------
%macro DVWP 1
    PCALL dvw_post
    dw %1
%endmacro
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dvw_betab:
    dw .goto, .read, .find, .rdat, .write
    dw .append, .delete, .dfree, .mkdir, .rmdir
    dw .xcaps, .xalloc, .xfree, .xcopy, .rename
    dw .copy, .move, .path, .vstat, .wrat
    dw dos_k_here, dos_k_vkind, .rseq, .wseq, .fsop
.goto:   DVWP dos_k_goto
.read:   DVWP dos_k_read
.find:   DVWP dos_k_find
.rdat:   DVWP dos_k_rdat
.write:  DVWP dos_k_write
.append: DVWP dos_k_append
.delete: DVWP dos_k_delete
.dfree:  DVWP dos_k_dfree
.mkdir:  DVWP dos_k_mkdir
.rmdir:  DVWP dos_k_rmdir
.xcaps:  DVWP dos_k_xcaps
.xalloc: DVWP dos_k_xalloc
.xfree:  DVWP dos_k_xfree
.xcopy:  DVWP dos_k_xcopy
.rename: DVWP dos_k_rename
.copy:   DVWP dos_k_copy
.move:   DVWP dos_k_move
.path:   DVWP dos_k_path
.vstat:  DVWP dos_k_vstat
.wrat:   DVWP dos_k_wrat
.rseq:   DVWP dos_k_rseq
.wseq:   DVWP dos_k_wseq
.fsop:   DVWP dos_k_fsop         ; the operation's bar draws: the UI task's
%if ($ - dvw_betab) != DBE_NENT * 2 + 23 * 5
 %error "dvw_betab has drifted from DBE_NENT: every door in dos_betab needs a slot here, in the same order"
%endif

WIN_CODE_END
%endif                              ; [R]
%ifndef DVW_SPLIT                   ; --- [P] -------------------------------
; -----------------------------------------------------------------------------
; dvw_start - post the program to the worker (dos_run, UI task, lock FREE)
; out: CF=1 no worker could be had, and the caller takes the bracket instead
; -----------------------------------------------------------------------------
dvw_start:
    DVW_HBIND                       ; the host's segment, first (doswabi.inc)
    push ax
    push bx
    call OSAPI_GFX_LOCK             ; OSAPI_TASK_SPAWN wants it held
    cmp byte [dvw_spawn], 0
    jne .have
    mov ax, dvw_worker
    mov bx, [dos_win]
    call OSAPI_TASK_SPAWN
    jc .no
    mov byte [dvw_spawn], 1
    OS88_WORKER_RESTARTABLE_NOKEEP dvw_worker   ; its loop's top is a clean
.have:                              ; place to begin again: the state is here
    cmp byte [dos_page], DOS_PAGE_MAIN  ; **THE BAND IS THE MAIN PAGE'S**:
    je .onmain                      ; a run started from Setup (the Memory
    mov byte [dos_page], DOS_PAGE_MAIN  ; limit is there) would draw it over
    mov bx, [dos_win]               ; that page's fields. dos_swap wants the
    HCALL dos_swap                   ; lock, which is held here
.onmain:                            ; the run's flags, cleared a PAIR at a
    xor ax, ax                      ; time (dos.asm's dvw_want block):
    DVW_CLRDK              ; done, kill
    mov [PS dvw_kh], ax                ; the key ring's head and tail
    mov [PS dvw_closeq], ax            ; closeq, fs
    mov [PS dvw_frz], ax               ; frz, fsreq
    mov [PS dvw_parked], ax            ; parked (0 between runs anyway), gfx
    mov ax, 0x0101
    mov [dvw_run], ax               ; run, then go - LAST, and one
                                    ; instruction: the worker reads it
                                    ; lock-free
    call OSAPI_GFX_UNLOCK
    pop bx
    pop ax
    clc
    ret
.no:
    call OSAPI_GFX_UNLOCK
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dvw_regrow - DVWE_REGROW: the program did not fit the window's cap, and the
; Memory limit is AUTOMATIC (empty) - so the cap grows to the program
; (SPEC.md 96.54.2.1)
; in:  AL = the DER_* dos_run's load answered; [dvw_want] set, the arena still
;      claimed; DS = the host; UI task, lock FREE
; out: CF = 1 RAISED: [dvw_cap] = what the program's own header asks for,
;      rounded up to DVW_STEPKB, and the arena FREED - dos_run sizes again.
;      CF = 0 the refusal stands, and [dvw_cap] = 0 when it was the MACHINE
;      that was short and not the cap, so dos_err_line does not blame the
;      window's 128K. AX, BX, DS and ES kept; CX, DX and SI SPENT, which
;      dos_run's `.freeerr` and `.floor` both reload before reading.
;
; **THE HEADER IS READ AGAIN, NOT TRUSTED WHERE IT LIES.** dos_load leaves it
; at the program's segment when its own fit test refused, and dos_exe_setup
; does when its PSP + image + MINALLOC test did - but a file refused WHOLE (a
; compressed one, or kern_small's cluster-granule probe) was never read, and
; that arena holds whatever the heap last held. One sector through
; OSAPI_FILE_READ_AT costs one read and is THIS file; on kern_small, whose
; granule is a cluster, it refuses, and the window keeps its cap.
;
; THE FIGURE IS THE LARGER OF dos_load's AND dos_exe_setup's TWO TESTS: the
; whole file in pages (the header is read into the block, before the move
; that strips it), and the image plus MINALLOC - spelled here as the pages
; plus (MINALLOC - header), which over-counts the image by under a page. Plus
; the PSP. The shortfall is taken against the block THIS cap produced, so the
; file window off the arena's top (SPEC.md 96.11) is counted without being
; known here.
; -----------------------------------------------------------------------------
dvw_regrow:
    DVW_HBIND                       ; the host may have moved since the last
    cmp al, DER_FIT                 ; run (a compaction wake, 66.6.1.2)
    jne .no
    cmp word [dos_memkb], 0         ; THE USER'S OWN NUMBER IS A DECISION and
    je .auto                        ; refuses as it always did
.no:
    clc
    ret
.auto:
    push ax
    push bx
    push es
    mov ax, [dvw_cap]
    cmp ax, [dos_akb]
    jne .short                      ; the arena was LESS than the cap: no
                                    ; raise reaches what the machine lacks
    mov es, [dos_arena]             ; ANY sector of the arena: it is ours,
    xor bx, bx                      ; and the load that refused is over
    mov cx, 512                     ; READ_AT's granule (SPEC.md 18.4.4.2)
    xor ax, ax
    cwd
    mov si, [dos_ldname]
    call OSAPI_FILE_READ_AT         ; DBE_RDAT's own door (dos_k_rdat): we
    jc .keep                        ; stand where dos_run's goto put us
    mov ax, [es:bx]
    cmp ax, 0x5A4D                  ; 'MZ', or 'ZM': dos_is_exe's two
    je .mz
    cmp ax, 0x4D5A
    jne .keep                       ; a .COM is 64K by construction
.mz:
    mov ax, [es:bx + MZ_CP]
    mov cl, 5
    shl ax, cl                      ; header + image, in paragraphs (past
                                    ; 2,047 pages it wraps, reads SMALL, and
                                    ; dos_load's own refusal is what stands)
    mov dx, [es:bx + MZ_MINALLOC]
    sub dx, [es:bx + MZ_CPARHDR]
    jbe .img                        ; MINALLOC within the header: the file
    add ax, dx                      ; is the larger of the two. A wrap here
.img:                               ; or below reads SMALL, and refuses
    add ax, 16                      ; ...and the PSP
    sub ax, [dos_ldpara]            ; paragraphs SHORT of the block it had
    jbe .keep                       ; it would have fitted: not this refusal
    dec ax
    mov cl, 6
    shr ax, cl                      ; KB short, less one (rounded up below)
    add ax, [dvw_cap]
    add ax, DVW_STEPKB              ; ...and the cap, up a step at least, so
    and al, -DVW_STEPKB             ; every raise is a real one and the retry
    mov [dvw_cap], ax               ; cannot go round for ever
    xor dx, dx
    xchg dx, [dos_arena]
    call OSAPI_MEM_FREE             ; dos_run claims again, at the new cap
    stc
    jmp short .pop
.short:
    and word [dvw_cap], 0
.keep:
    clc
.pop:
    pop es
    pop bx
    pop ax
    ret

%endif                              ; [P]
%ifndef DVW_PART                    ; --- [R]: the worker's loop is the
                                    ; host's: its idle sleep is what the
                                    ; region's restart point names, and
                                    ; the part may be gone between runs
; -----------------------------------------------------------------------------
; dvw_worker - the instance's one worker (SPEC.md 20.6): never returns
; -----------------------------------------------------------------------------
dvw_worker:
.loop:
    mov bx, [dos_win]
    call OSAPI_TASK_ALIVE
    cmp byte [dvw_go], 0
    je .idle
    mov byte [dvw_go], 0
    PCALL dvw_runprog
    mov byte [dvw_done], 1
    mov bx, [dos_win]
    call OSAPI_WM_WAKE
    jmp short .loop
.idle:
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short .loop

%endif                              ; [R]
%ifndef DVW_SPLIT                   ; --- [P], to the end -------------------
; -----------------------------------------------------------------------------
; dvw_runprog - dos_fsx_main without the full screen, on the worker
;
; **NOTHING IS PUSHED BEFORE [dos_sv_sp] IS BANKED, AND THAT IS THE RETURN.**
; dos_terminate puts SP back to that bank and jumps to dos_prog_done, whose
; DHK_SNAP hook is dvw_fin below: it drops its own return into the core and
; `ret`s - which, with nothing pushed here, lands on this routine's caller.
; That is how the windowed run skips dos_restore_machine (a whole-IVT and
; BDA restore that is right for a bracket and would rewind a live desktop)
; without one byte of the core changing.
; -----------------------------------------------------------------------------
dvw_runprog:
    mov [PS dvw_lss], ss
    call OSAPI_VIDEO                ; INT 33h's scale (dos_fsx_main's reason)
    mov [dos_vw], ax
    mov [dos_vh], bx
    mov [PS dvw_dkind], dl             ; ...and the desktop's kind, dvw_vguard's
    HCALL dos_m33_wall
    call dwm_start                  ; ...and the window's own (96.54.4)
    HCALL dos_build_psp
    push ds                         ; the posting doors
    pop es
    mov si, dvw_betab
    mov di, dos_bevec
    mov cx, DBE_NENT
    cld
    rep movsw   ; DVWCHK-OK: the host's door table -> the host's
    mov word [dos_hkv + DHK_SNAP], HENT(dvw_fin)
    ; --- THE PROGRAM'S WORLD, PRIMED FROM THE DESKTOP'S ------------------------
    cli
    in al, 0x21                     ; **WHAT THE RUN MAY LEAVE BEHIND, AS IT
    mov [dos_pic1], al              ; FOUND IT** (SPEC.md 96.54.12): the mask
    push ds                         ; and the whole table, into the core's
    xor si, si                      ; bracket cells - dvw_mend's, at the end.
    mov ds, si                      ; ES is the host's (the door copy above)
    mov di, dos_ivt
    mov cx, 512
    rep movsw   ; DVWCHK-OK: the live IVT (DS 0) -> the host's dos_ivt (ES)
    pop ds
    mov bx, PADDR(dvw_xblk)                ; the program's block = the live table,
    mov dx, bx                      ; so every vector we do not own starts
    call dvw_copy                   ; the same (BX = DX: a GET alone)
    DVW_PCOPY
    mov si, PADDR(dvw_bdat)                ; ...and the video block a text mode's
    mov di, PADDR(dvw_xblk) + DVW_XB_VID
    mov cx, 18
    rep movsw   ; DVWCHK-OK: the part's to the part's (DVW_PCOPY)
    DVW_PCEND
    mov ax, [con_cy]                ; the cursor is the console's
    mov ah, al
    mov al, [con_cx]
    mov [PS dvw_xblk + DVW_XB_VID + 7], ax     ; 0040:0050, page 0
    mov [PS dvw_csview], ds         ; TopView's answer, and its update's test
                                    ; (DS is the host's: DVW_PCEND)
    mov byte [PS dvw_cmode], 0
    call dvw_cdetect                ; **A SCREEN THE WINDOW CAN READ**
    jc .nocap                       ; (96.54.6): the other card's text RAM
    mov al, DVWC_HP                 ; A LONE HERCULES'S PAGE 1 (96.54.6.1) is
    cmp byte [PS dvw_chp], 0           ; WATCHED, int 10h staying ours: mode 3
    jne .crtc                       ; at B800, the status port its own 3BAh
    mov al, DVWC_SEG                ; (a CGA's 3DAh reads FFh there)
    mov byte [dos_inbr], 1          ; DOS's teletype is the ROM's: one truth
    cmp word [PS dvw_cseg], 0xB000
    jne .seed
    mov byte [PS dvw_xblk + DVW_XB_VID], 7         ; mode 7 at B000, its CRTC
.crtc:
    mov word [PS dvw_xblk + DVW_XB_VID + 0x1A], 0x03B4
    cmp al, DVWC_HP
    je .copy
.seed:
    call dvw_cprog                  ; its 6845 RUNNING, or a snow-avoiding
.copy:                              ; writer waits for a retrace for ever
    mov [PS dvw_cmode], al
    push ds                         ; the console onto it, so the program's
    push es                         ; screen continues the session the way
    mov es, [PS dvw_cseg]              ; the bracket's seed does (96.34.4)
    mov si, con_scr
    xor di, di
    mov cx, CON_SCRSZ / 2
    rep movsw   ; DVWCHK-OK: the host's console -> the card
    pop es
    pop ds
    cmp byte [PS dvw_cmode], DVWC_HP   ; ...and the page's row sums taken, so
    jne .nocap                      ; the probe knows what it seeded
    mov byte [PS dvw_hprow], 0
    xor bx, bx
.hsum:
    call dvw_hpsum
    push bx
    shl bx, 1
    mov [PS dvw_sums + bx], dx
    pop bx
    inc bx
    cmp bx, CON_ROWS
    jb .hsum
.nocap:
    xor ax, ax
    mov es, ax
    mov ax, [es:0x08 * 4]
    mov [PS dvw_k08], ax
    mov ax, [es:0x08 * 4 + 2]
    mov [PS dvw_k08 + 2], ax
    call dvw_xin
    mov si, PADDR(dvw_ivt)                 ; ...and OURS, before anything below can
    mov cx, 4                       ; `sti`: a tick with the DOS block in and
.ivt:                               ; sch_isr still at 08h would leave it in
    PSRC lodsw                           ; for whoever ran next
    xchg ax, di
    PSRC movsw
    PARTCS ax
    stosw       ; DVWCHK-OK: ES = 0, the vector table
    loop .ivt
    HCALL dos_hook_vectors           ; the DOS block, into the PROGRAM's table
    mov ax, [dos_arena]
    add ax, DOS_PSPP
    mov [dos_dtaseg], ax
    mov word [dos_dta], 0x80
    mov ax, [dos_dir]
    mov [dos_curdir], ax
    mov al, [dos_vol]
    HCALL dos_drv_bank
    HCALL dos_date_init
    mov ax, ss
    mov [dos_sv_ss], ax
    mov [dos_sv_sp], sp
    mov [dos_dstk], sp
    HCALL dos_prog_enter             ; ...and away; back through dvw_fin

dvw_ivt:                            ; dvw_runprog's four: where, and whose
    dw 0x08 * 4, PADDR(dvw_irq0)
    dw 0x10 * 4, PADDR(dvw_int10)
    dw 0x13 * 4, PADDR(dvw_int13)
    dw 0x16 * 4, PADDR(dvw_int16)

dvw_fin:                            ; DHK_SNAP, from dos_prog_done
    ; STKBALANCE-OK: dvw_runprog's header - the `add` drops the return into
    ; the core, so the `ret` is dvw_runprog's own, at [dos_sv_sp]
    add sp, 2                       ; not back into the core (dvw_runprog)
    cmp byte [PS dvw_fs], 0            ; ENDED ON THE FULL SCREEN: its last
    je .notfs                       ; screen is the console's, exactly as a
    HCALL dos_snap                   ; bracketed run's (96.34) - with the world
    mov byte [PS dvw_cmode], 0         ; still in, the BDA is the program's - and
.notfs:                             ; NO capture pass may copy a stale card
                                    ; over it (the card's is torn down by
                                    ; dvw_wake, after the run's LAST pass)
    cli
    call dvw_xout
    call dvw_mend                   ; ...and what the world never held
    sti
    mov word [dos_hkv + DHK_SNAP], dos_snap
    call dwm_stop                   ; the bracket's INT 33h hooks (96.54.4)
    HCALL dos_be_bind

    ret

; -----------------------------------------------------------------------------
; dvw_mend - the machine state a run can leave OUTSIDE its world (SPEC.md
; 96.54.12); in: DS = the host, IF = 0, the world out, dvw_runprog's bank in
; dos_ivt and dos_pic1; out: every register preserved
;
; Galaxy Trek closed in the middle of a note left channel 0 at 32 times the
; tick rate for the rest of the session (a slow machine, a 15 ms double-click,
; every tick-timed wait short) - the exchange gives IVT 08h back and nothing
; gives the PIT back. So: channel 0 the kernel's; every vector whose segment
; lies in the arena - code that is about to be freed - its banked value; and
; the mask the banked one's live levels, plus its bit for a level whose vector
; went back - or whose vector the program's world still holds in the arena,
; 08h-0Fh being exchanged and so never live here with the world out. A vector
; the program did not leave in its own memory is not touched, so a driver
; mounted meanwhile keeps its hook.
; -----------------------------------------------------------------------------
dvw_mend:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov al, 0x34                    ; channel 0: mode 2, divisor 0 - the
    out 0x43, al                    ; bytes dos_restore_machine writes (96.5.3)
    xor ax, ax
    out 0x40, al
    out 0x40, al
    mov es, ax
    mov si, dos_ivt
    xor di, di
    xor bx, bx                      ; BL: the IRQ levels whose vector went back
.v:
    mov ax, [es:di + 2]
    sub ax, [dos_arena]
    cmp ax, [dos_apara]
    jae .next                       ; not the program's memory: not ours
    mov ax, [si]
    mov [es:di], ax
    mov ax, [si + 2]
    mov [es:di + 2], ax
    lea ax, [di - 0x08 * 4]
    cmp ax, 8 * 4
    jae .next                       ; not an IRQ's (08h-0Fh)
    mov cl, 2
    shr ax, cl
    xchg ax, cx
    mov al, 1
    shl al, cl
    or bl, al
.next:
    add si, 4
    add di, 4
    cmp di, 0x400
    jb .v
    mov si, DVW_XB_IRQ              ; ...and an IRQ's vector the exchange took
    mov ah, 1                       ; out with the program's world: its level
.q:                                 ; is put back as the bank had it too
    mov cx, [PS dvw_xblk + si + 2]
    sub cx, [dos_arena]
    cmp cx, [dos_apara]
    jae .qn
    or bl, ah
.qn:
    add si, 4
    shl ah, 1
    jnz .q
    mov ah, [dos_pic1]
    and bl, ah                      ; put back and masked then: masked again
    in al, 0x21
    and al, ah                      ; live then: live again
    or al, bl
    out 0x21, al
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; the video block a mode-3 screen leaves in 0040:0049..0066, then 0084..0089
dvw_bdat:
    db 3                            ; 49 mode
    dw 80                           ; 4A columns
    dw 4096                         ; 4C page size
    dw 0                            ; 4E page offset
    times 8 dw 0                    ; 50 the eight cursors (page 0 patched)
    dw 0x0607                       ; 60 cursor shape
    db 0                            ; 62 active page
    dw 0x03D4                       ; 63 CRTC base
    db 0x29                         ; 65 mode register
    db 0x30                         ; 66 palette
    db 24                           ; 84 rows - 1
    dw 8                            ; 85 character height
    db 0, 0, 0                      ; 87..89 no EGA
%if ($ - dvw_bdat) != 36
 %error "dvw_bdat must be exactly the 36 bytes dvw_copy moves"
%endif

; -----------------------------------------------------------------------------
; dvw_int10 - the virtual video BIOS, onto con_scr (DOS-WINDOWED-PLAN 3.3)
; -----------------------------------------------------------------------------
dvw_int10:
    sti
    cmp ah, 0xFE                    ; **TOPVIEW'S PAIR** (SPEC.md 96.54.5),
    jae .tv                         ; in every mode: a program asks once
    cmp byte [cs:dvw_fs], 0         ; **FULL SCREEN** (SPEC.md 96.54.1): the
    jne .pass                       ; machine's own BIOS, on the machine's
    cmp byte [cs:dvw_cmode], DVWC_SEG   ; ...and the CAPTURE's (96.54.6): the
    jne .virt                       ; ROM writes the other card's text RAM,
    or ah, ah                       ; which the window copies - all but a mode
    jnz .pass                       ; set, which on a mono primary programs
    call dvw_cmset                  ; the WRONG CARD (39.19.4)
    jnc .cmdone
    jmp .shunt
.cmdone:
    iret
.virt:
    push ds
    HOSTDS
    cmp ah, 0x0E                    ; the teletype first: it is most of them
    je .tty
    cmp ah, 0x02                    ; ...and then the PRINT of a compiled BASIC
    je .setcur                      ; program, which is AH=02h, AH=09h and
    cmp ah, 0x09                    ; AH=02h a character (SPEC.md 96.54.14):
    je .wca                         ; seven compares further down each, eleven
    cmp ah, 0x0F                    ; times a character
    je .mode
    cmp ah, 0x13
    je .str
    cmp ah, 0x0A                    ; ...and below it, by RANGE
    ja .out
    cmp ah, 0x09
    jae .wca                        ; 09h, 0Ah
    cmp ah, 0x08
    je .rca
    cmp ah, 0x06
    jae .scroll                     ; 06h, 07h
    cmp ah, 0x03
    je .getcur
    or ah, ah
    jz .setmode
.out:
    pop ds
    iret

.tty:
    HCALL con_write
    call dvw_cdone
    jmp short .out

.setcur:
    call dvw_setcur
    jmp short .out

.getcur:
    push ax
    call dvw_ccur
    xchg ax, dx
    pop ax
    mov cx, 0x0607
    jmp short .out

.mode:
    mov ax, 0x5003                  ; 80 columns, mode 3
    xor bh, bh
    jmp short .out

.rca:
    push bx
    push di
    call dvw_coff
    mov ax, [con_scr + di]
    pop di
    pop bx
    jmp short .out

.wca:                               ; AH 09h = char + BL attribute, 0Ah = char
    cmp cx, 1                       ; alone; CX of them, the cursor stays.
    jne .wcan                       ; **ONE CELL IS ITS OWN ARM** (SPEC.md
    push bx                         ; 96.54.14): a BASIC PRINT is CX = 1 every
    push cx                         ; time, and the run's loop, its bounds and
    push di                         ; its row test are most of the general arm
    mov ch, bl                      ; CH = the attribute, past dvw_coff
    call dvw_coff                   ; DI = the cell, BX = its row
    cmp di, CON_SCRSZ
    jae .w1out                      ; (no cell: a pending wrap's past the end)
    mov [con_scr + di], al
    cmp ah, 0x09
    jne .w1mk
    mov [con_scr + di + 1], ch
.w1mk:
    mov cl, [con_cx]
    mov ch, cl
    HCALL con_marks                 ; BX = the row, the one column
    mov byte [dvw_dirty], 1
.w1out:
    pop di
    pop cx
    pop bx
    jmp .out
.wcan:
    push ax
    push bx
    push cx
    push dx
    push di
    jcxz .wdone
    mov dh, ah
    mov dl, bl
    call dvw_coff                   ; DI = the cursor's cell, BX = its row:
    push di                         ; no crossing (SPEC.md 96.54.14)
.wnext:
    cmp di, CON_SCRSZ
    jae .wmark
    mov [con_scr + di], al
    cmp dh, 0x09
    jne .wskip
    mov [con_scr + di + 1], dl
.wskip:
    inc di
    inc di
    loop .wnext
.wmark:
    pop ax
    shr ax, 1                       ; the first cell...
    shr di, 1
    dec di                          ; ...and the last one written
    cmp di, ax
    jb .wdone                       ; the cursor was past the last cell
    mov cx, di                      ; **ONE ROW IS ONE MARK** (SPEC.md
    sub cx, ax                      ; 96.54.14): a BASIC PRINT is AH=09h with
    add cx, [con_cx]                ; CX = 1 a character, and con_markcells'
    cmp cx, CON_COLS                ; two divides for a run inside the row the
    jae .wmany                      ; cursor is on were most of its cost
    mov ch, cl
    mov cl, [con_cx]
    HCALL con_marks                 ; BX = the row, CL..CH its columns
    jmp short .wdirty
.wmany:
    mov bx, di
    HCALL con_markcells             ; AX = the first cell, BX = the last
.wdirty:
    mov byte [dvw_dirty], 1
.wdone:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    jmp .out

.scroll:
    call dvw_scroll
    jmp .out

.setmode:
    push ax
    and al, 0x7F
    cmp al, 3
    jbe .text
    cmp al, 7
    jne .gfx                        ; a graphics mode: wave 3's full screen
.text:
    mov byte [con_attr], 0x07
    HCALL con_clear
    mov byte [dvw_dirty], 1
    call dvw_bcur
    pop ax                          ; **A TEXT MODE STAYS IN THE WINDOW**: it
    jmp .out                        ; is a clear and a home, and nothing more
.gfx:
    pop ax                          ; **A GRAPHICS MODE IS THE ONE THING THE
    pop ds                          ; WINDOW CAN SEE COMING** (SPEC.md
.shunt:
    call dvw_shunt                  ; 96.54.1): to the full screen first, then
    jnc .pass                       ; the mode set, so not a pixel of it lands
    iret                            ; anywhere else. Refused: no mode set
.pass:
    or ah, ah                       ; a mode set is remembered: a GRAPHICS
    jnz .rom                        ; program may not come back to the window
    push ax                         ; (there is no text to bring)
    and al, 0x7F                    ; AH = 0: a text mode
    cmp al, 3
    jbe .setg
    cmp al, 7
    je .setg
    inc ah
.setg:
    mov [cs:dvw_gfx], ah
    pop ax
.rom:
    jmp far [cs:dvw_xdsk + DVW_XB_V10]  ; the ROM's, the world being in

.tv:                                ; FEh: ES:DI = the buffer the program
    jne .tvupd                      ; meant to write -> the one it SHOULD.
    cmp byte [cs:dvw_fs], 0         ; On the full screen and in a capture
    jne .tvsame                     ; that IS the screen, so ES:DI comes back
    cmp byte [cs:dvw_cmode], DVWC_SEG   ; as it went, which is also what it
    je .tvsame                      ; reads with no multitasker at all
    mov byte [cs:dvw_cmode], DVWC_TV
    HOSTES                         ; ...in the window it is the console's
                                    ; own cells, already text VRAM's layout
    mov di, con_scr                 ; (96.34): writes land where the band
.tvsame:                            ; is drawn from, and the row sums find
    iret                            ; them
.tvupd:                             ; FFh: ES:DI, CX cells changed
    cmp cx, CON_COLS                ; **MORE THAN A ROW IS LEFT TO THE SUMS**
    ja .tvsame                      ; (SPEC.md 96.54.5.1): WordPerfect says
                                    ; "all 2,000 cells" on every pass of its
                                    ; main loop, idle or not - which marked 25
                                    ; rows a call - and dvw_capt's row sums,
                                    ; every fourth tick, find what moved
    cmp byte [cs:dvw_cmode], DVWC_TV
    jne .tvsame
    push ax
    mov ax, es
    cmp ax, [cs:dvw_csview]         ; ...ours: CS, banked at the run's start
    pop ax
    jne .tvsame
    call dvw_tvmark
    iret

.str:
    call dvw_str
    jmp .out

; --- dvw_cdone - a cursor that moved: the BDA hears, the band is owed -------
dvw_cdone:
    mov byte [dvw_dirty], 1
    ; fall through
dvw_bcur:                           ; 0040:0050 = the console's cursor
    push ax
    call dvw_ccur
dvw_bput:                           ; ...AX, with the caller's AX pushed
    push es
    push bx
    mov bx, 0x40
    mov es, bx
    mov [es:0x50], ax
    pop bx
    pop es
    pop ax
    ret

dvw_ccur:                           ; AH = the console's row, AL its column
    mov ah, [con_cy]                ; clamped (a pending wrap is past it)
    mov al, [con_cx]
    cmp al, CON_COLS - 1
    jbe .c
    mov al, CON_COLS - 1
.c:
    ret

; --- dvw_coff - DI = the cursor's offset in con_scr, BX = its row ------------
; (DS the host.) row * 160 is row * 32 * 5: con_celloff's `mul` and a host
; crossing were two of the five a BASIC PRINT paid per character
dvw_coff:
    push cx
    mov bx, [con_cy]
    mov di, bx
    mov cl, 5
    shl di, cl
    mov cx, di
    shl di, 1
    shl di, 1
    add di, cx
    mov cx, [con_cx]
    add di, cx
    add di, cx
    pop cx
    ret

; --- dvw_setcur - DH = row, DL = column, clamped -----------------------------
; **NO MARK** (SPEC.md 96.54.14): the caret's cells are con_curmove's, which
; marks where the underline was drawn and where it is now at the head of the
; band's next pass. A compiled BASIC program moves the cursor twice for every
; character it prints, and two host crossings and two marks apiece were the
; largest share of its windowed time
dvw_setcur:
    push ax
    push bx
    mov ax, dx
    cmp al, CON_COLS - 1
    jbe .x
    mov al, CON_COLS - 1
.x:
    cmp ah, CON_ROWS - 1
    jbe .y
    mov ah, CON_ROWS - 1
.y:
    xor bx, bx                      ; AL = the column, AH = the row: the
    mov bl, al                      ; words take them zero-extended, and
    mov [con_cx], bx                ; 0040:0050 takes AX exactly as it is
    mov bl, ah
    mov [con_cy], bx
    pop bx
    mov byte [con_pwrap], 0
    mov byte [dvw_dirty], 1
    jmp short dvw_bput              ; (dvw_bput pops the AX pushed above)

; -----------------------------------------------------------------------------
; dvw_scroll - AH 06h up / 07h down, AL lines (0 = clear), BH the fill
; attribute, CH,CL the top left, DH,DL the bottom right
;
; **ITS STATE IS ITS OWN FRAME, NOT CELLS** (SPEC.md 96.54.9): the window's
; top left, the fill and the direction are the caller's own CX, BH and AH,
; which the entry pushes anyway and nothing changes - so they are read where
; they were pushed, [bp+...] on SS - and only the clamped bottom right and
; the line count are pushed beside them. A cell of the layer's is a CS:
; prefix in the part on every read; a frame slot is a shorter operand in
; both shapes. 6 bytes more of the program's stack (BP and the two words)
; -----------------------------------------------------------------------------
DVWS_L  equ 10                      ; [bp+DVWS_L]  CL the left (CX pushed)
DVWS_T  equ 11                      ; [bp+DVWS_T]  CH the top
DVWS_A  equ 13                      ; [bp+DVWS_A]  BH the fill (BX pushed)
DVWS_UP equ 15                      ; [bp+DVWS_UP] AH 06h or 07h (AX pushed)
DVWS_R  equ -2                      ; [bp+DVWS_R]  DL the right, clamped
DVWS_B  equ -1                      ; [bp+DVWS_B]  DH the bottom, clamped
DVWS_N  equ -4                      ; [bp+DVWS_N]  the lines, clamped
dvw_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    mov bp, sp
    push ds
    pop es
    cmp dh, CON_ROWS - 1
    jbe .h
    mov dh, CON_ROWS - 1
.h:
    cmp dl, CON_COLS - 1
    jbe .w
    mov dl, CON_COLS - 1
.w:
    cmp ch, dh
    ja .out
    cmp cl, dl
    ja .out
    push dx                         ; [bp-2] DL right, DH bottom
    mov bh, dh
    sub bh, ch
    inc bh                          ; BH = the window's height
    or al, al
    jz .all
    cmp al, bh
    jbe .n
.all:
    mov al, bh
.n:
    push ax                         ; [bp-4] AL the lines
    cld
    xor bl, bl
.row:                               ; BL = 0..height-1, the row's ORDER
    mov al, [bp+DVWS_T]             ; top
    cmp byte [bp+DVWS_UP], 0x06
    je .uprow
    mov al, [bp+DVWS_B]             ; down: bottom upwards
    sub al, bl
    mov ah, al
    sub ah, [bp+DVWS_N]             ; the source row, above
    jmp short .have
.uprow:
    add al, bl
    mov ah, al
    add ah, [bp+DVWS_N]             ; the source row, below
.have:                              ; AL = the destination row, AH = source
    push bx
    mov dl, al
    call .addr                      ; DI = the destination
    mov di, si
    mov cl, [bp+DVWS_R]
    sub cl, [bp+DVWS_L]
    inc cl
    xor ch, ch                      ; CX = the window's width
    mov al, ah
    cmp byte [bp+DVWS_UP], 0x06
    je .srcup
    cmp al, [bp+DVWS_T]
    jl .blank                       ; above the window: no source
    jmp short .copy
.srcup:
    cmp al, [bp+DVWS_B]
    jg .blank                       ; below the window: no source
.copy:
    call .addr                      ; SI = the source
    rep movsw   ; DVWCHK-OK: con_scr to con_scr, DS = ES = the host
    jmp short .marked
.blank:
    mov al, ' '
    mov ah, [bp+DVWS_A]
    rep stosw   ; DVWCHK-OK: into con_scr, ES the host
.marked:
    mov bl, dl
    xor bh, bh
    HCALL con_mark
    pop bx
    inc bl
    cmp bl, bh
    jb .row
    mov byte [dvw_dirty], 1
    pop di                          ; the two words of the frame (DI is
    pop di                          ; popped again below)
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.addr:                              ; AL = a row -> SI = its window's left cell
    push ax
    push dx
    xor ah, ah
    mov dx, CON_COLS * 2
    mul dx
    mov dl, [bp+DVWS_L]
    xor dh, dh
    add ax, dx
    add ax, dx
    add ax, con_scr
    mov si, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dvw_str - AH 13h: AL bit 0 = move the cursor, bit 1 = the string carries
; its attributes; BL the attribute, CX the length, DH,DL where, ES:BP it
; -----------------------------------------------------------------------------
dvw_str:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push word [con_cx]
    push word [con_cy]
    mov ah, [con_attr]
    push ax
    call dvw_setcur
    jcxz .done
.next:
    mov ah, al                      ; AH = the mode, kept
    mov al, [es:bp]
    inc bp
    test ah, 2
    jz .plain
    mov bl, [es:bp]
    inc bp
.plain:
    mov [con_attr], bl
    push ax
    HCALL con_write
    pop ax
    mov al, ah
    loop .next
.done:
    pop ax
    mov [con_attr], ah              ; the current attribute is the console's
    pop si                          ; cy
    pop dx                          ; cx (DX: it is spent)
    test al, 1
    jnz .keep
    mov [con_cy], si                ; bit 0 clear: the cursor goes back
    mov [con_cx], dx
.keep:
    call dvw_cdone
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dvw_int13 - a program's own disk BIOS call, refused (DOS-WINDOWED-PLAN 3.4):
; a raw transfer may not share the drive with the file layer mid-operation
; -----------------------------------------------------------------------------
dvw_int13:
    sti
    mov ah, 0x80                    ; TIMEOUT - no drive answered
    stc
    retf 2

; -----------------------------------------------------------------------------
; dvw_int16 - the virtual keyboard BIOS, off the window's keys
;
; **A WAIT SLEEPS AND SO DOES A POLL** (DOS-WINDOWED-PLAN 3.6): AH=00h with
; nothing typed costs the machine a tick of sleep, and every eighth empty
; AH=01h sleeps one too - a program spinning on "key ready?" is the
; commonest way one DOS program would otherwise eat the desktop's whole time.
; It was a YIELD and that was measured wrong: with nothing else ready a yield
; returns at once, so a waiting program spent its whole slice exchanging its
; world in and out.
; -----------------------------------------------------------------------------
dvw_int16:
    sti
    cmp ah, 0x10                    ; the enhanced three are the plain three
    jb .fn
    cmp ah, 0x12
    ja .none
    sub ah, 0x10
.fn:
    or ah, ah
    jz .get
    cmp ah, 1
    je .peek
    cmp ah, 2
    je .rom
.none:
    iret
.rom:
    jmp far [cs:dvw_xdsk + DVW_XB_V16]  ; the shift state is the machine's
.get:
    call dvw_kpop
    jnz .got
    cmp byte [cs:dvw_kill], 0
    jne .kill
    call dvw_nap1
    jmp short .get
.got:
    iret
.kill:
    jmp dvw_kexit                   ; from the program's own int: its frame
.peek:                              ; is the one terminate unwinds
    push bp
    mov bp, sp
    call dvw_kpeek
    jnz .pk
    cmp byte [cs:dvw_kill], 0       ; **THE CLOSE IS HEARD HERE TOO**: a
    jne .pkill                      ; program waiting in INT 21h's own key
    cmp byte [cs:dvw_kbusy], 0      ; **NOT WHILE A KEY IS BEING ANSWERED**
    jne .pz                         ; (96.54.5.1): see dvw_kpeek
    inc byte [cs:dvw_poll]          ; read polls from the GATE's stack, where
    test byte [cs:dvw_poll], 7      ; dvw_irq0 never sees a foreign SS
    jnz .pz
    call dvw_nap1                   ; A TICK, NOT A YIELD: a yield with
.pz:                                ; nothing else ready comes straight back,
                                    ; and the poll became a loop of exchanges
                                    ; - measured, the CPU was in dvw_walk at
                                    ; every sample of a program waiting for
                                    ; a key
    or byte [bp+6], 0x40            ; ZF = 1: nothing waiting
    pop bp
    iret
.pk:
    and byte [bp+6], 0xBF           ; ZF = 0, AX = the key, not consumed
    pop bp
    iret
.pkill:
    pop bp
    jmp short .kill                 ; INT 21h AH=4Ch from inside the gate is
                                    ; a nested call; terminate puts SS:SP
                                    ; back to [dos_sv_sp] and drops them all

; --- the ring: 16 keys, filled by dos_key (UI task) and drained here --------
dvw_kput:                           ; AX = a key; a full ring drops it
    push bx                         ; (UI task: DS = CS)
    pushf
    cli
    mov bl, [PS dvw_kt]
    xor bh, bh
    shl bx, 1
    mov [PS dvw_kq + bx], ax           ; the tail's slot is never a live key,
    shr bx, 1                       ; so it is written even when full
    inc bx
    and bl, 15
    cmp bl, [PS dvw_kh]
    je .full
    mov [PS dvw_kt], bl
.full:
    popf
    pop bx
    ret

dvw_kpeek:                          ; ZF = 1 empty, else AX = the next key
    push bx
    mov bl, [cs:dvw_kh]
    cmp bl, [cs:dvw_kt]
    je .r
    xor bh, bh
    shl bx, 1
    mov ax, [cs:dvw_kq + bx]
    mov byte [cs:dvw_kbusy], DVW_KBUSY  ; **A KEY IS WORK, NOT A WAIT**: for
    or bx, 1                        ; ZF = 0; a second after one, an empty
                                    ; poll does not sleep (96.54.5.1)
.r:
    pop bx
    ret

; dvw_kmore - the program's screen MOVED: a key's busy window, if one is
; open, starts again (SPEC.md 96.54.5.1). A screen that moves with no key in
; front of it - a clock - opens none, so an idle program stays throttled
dvw_kmore:
    cmp byte [cs:dvw_kbusy], 0
    je .r
    mov byte [cs:dvw_kbusy], DVW_KBUSY
.r:
    ret

dvw_kpop:                           ; ZF = 1 empty, else AX = it, taken
    pushf
    cli
    call dvw_kpeek
    jz .e
    inc byte [cs:dvw_kh]
    and byte [cs:dvw_kh], 15
    popf
    or sp, sp                       ; ZF = 0
    ret
.e:
    popf
    cmp ax, ax                      ; ZF = 1
    ret

; -----------------------------------------------------------------------------
; dvw_wake - dos_wake's windowed arm: serve, paint, and finish
; -----------------------------------------------------------------------------
dvw_wake:
    cmp byte [PS dvw_fsreq], 0         ; Alt+Enter, or a graphics mode set
    je .nofs
    call OSAPI_GFX_LOCK             ; THE FULL SCREEN, with the worker KEPT
    mov ax, HENT(dvw_fsmain)
    mov bx, [dos_win]
    mov cx, FSXF_KEEPWORKER         ; the program goes on running in there
    call OSAPI_FSX_RUN
    jnc .fsok
    mov byte [PS dvw_fsreq], 0         ; refused: dvw_shunt hears no
.fsok:
    call OSAPI_GFX_UNLOCK
.nofs:
    call dvw_serve
    call dvw_capt                   ; what the program wrote itself (96.54.5-6)
    call OSAPI_GFX_LOCK
    mov bx, [dos_win]               ; **CUT TO WHAT IS VISIBLE** (SPEC.md
    call OSAPI_WM_CLIP_SET          ; 96.54.10): this is a wake, not W_PAINT,
    jc .owe                         ; so no clip is armed, and the band went
    call OSAPI_WM_OBSCURED          ; straight over a window dragged in front
    jnc .draw                       ; of it. Covered in PART, a SCROLL is
    cmp word [con_scrl], 0          ; owed and not drawn: GFX_SCROLL refuses
    je .draw                        ; under a clip and the fallback is all 25
.owe:                               ; rows a line - the lock held 99% of the
    mov byte [PS dvw_owe], 1        ; time, measured. Owed is the whole band,
    jmp short .hid                  ; at the first wake with nothing over it
.draw:                              ; (dos_paint kicks one after an expose,
    xor al, al                      ; whose own draw is cut to the exposed
    xchg al, [PS dvw_owe]           ; part and spends every mark)
    or al, al
    jz .now
    HCALL con_markall
.now:
    HCALL dos_con_draw
.hid:
    call OSAPI_GFX_UNLOCK
    cmp byte [dvw_done], 0
    je .r
    mov byte [dvw_done], 0
    mov byte [dvw_run], 0
    call dvw_ccheck                 ; (a card the desktop took is not ours to
                                    ; mark or blank: 96.54.6)
    cmp word [PS dvw_cseg], 0          ; THE CARD GIVEN BACK (96.54.13): the
    je .nomark                      ; holder's mark cleared, a Hercules's
    mov al, 3                       ; page decoded for the write (with it off
    cmp byte [PS dvw_chp], 0        ; B800 is B000 on some cards, the
    je .mark                        ; desktop's own pixels)
    call dvw_hpage
.mark:
    push es
    mov es, [PS dvw_cseg]
    mov word [es:DVWC_OWN], 0
    pop es
.nomark:
    cmp byte [PS dvw_chp], 0           ; THE CAPTURE TORN DOWN, its last pass
    je .nopage                      ; taken above: a Hercules's page 1 has
    mov al, 1                       ; its decode off again, as vid_setmode
    call dvw_hpage                  ; leaves it (the page was never shown)...
    mov byte [PS dvw_chp], 0
.nopage:
    cmp word [PS dvw_cseg], 0          ; ...the other card dark again, as the
    je .nocard                      ; kernel's Single mode leaves it
    call dvw_cblank
    mov word [PS dvw_cseg], 0
.nocard:
    mov byte [PS dvw_cmode], 0
    mov byte [dos_inbr], 0          ; a capture's teletype was the ROM's
    mov byte [dos_state], DST_RAN   ; the bracket's own tail (dos_run .ran
    mov dx, [dos_arena]             ; through .out), which the post skipped
    or dx, dx
    jz .na
    call OSAPI_MEM_FREE
    mov word [dos_arena], 0
.na:
    mov byte [dos_cpw], 0
    HCALL dos_pkt_shut
    HCALL dos_drv_back
    HCALL dos_cache_free
    xor ax, ax                      ; the close dvw_onclose refused, now
    xchg al, [PS dvw_closeq]           ; that nothing of the run is left
    or al, al
    jz .said
    mov bx, [dos_win]
    call OSAPI_WM_CLOSE             ; deferred to the next UI pass: return
    ret
.said:
    HCALL dos_con_ended
    HCALL dos_repaint
.r:
    ret

; -----------------------------------------------------------------------------
; dvw_shunt - ask for the full screen, and wait until it is up (worker side)
; out: CF=0 the program is on the full screen; CF=1 it was refused, or the
; window is closing. Any stack; every register preserved
; -----------------------------------------------------------------------------
dvw_shunt:
    push ds
    HOSTDS
    mov byte [PS dvw_fsreq], 1
.w:
    mov byte [dvw_dirty], 1         ; **THE WAKE IS dvw_xpost's, NOT OURS**:
    call dvw_nap1                   ; this runs inside the program's int 10h,
                                    ; on the PROGRAM'S stack, and evq_push
                                    ; writes the ring through SS - a wake
                                    ; posted here landed in the program's own
                                    ; data and left the window deaf (W_WKQ
                                    ; set, no record). dvw_nap posts it from
                                    ; the worker's LOW_SEG slice
    cmp byte [PS dvw_fs], 0
    jne .up                         ; (CF = 0: nothing is below zero)
    cmp byte [PS dvw_kill], 0
    jne .no
    cmp byte [PS dvw_fsreq], 0
    jne .w
.no:
    stc
.up:
    pop ds
    ret

; -----------------------------------------------------------------------------
; dvw_fsmain - the bracket's proc: the UI task's half of a live full screen
;
; **THE PROGRAM IS NOT STOPPED TO GET HERE.** The worker is FSXF_KEEPWORKER's,
; so it runs straight through the mode set; the virtual video BIOS passes to
; the ROM from the moment [dvw_fs] is set, and its keys come from the ROM's
; buffer through this loop into the same ring the window fills. The doors are
; served from here too - the UI task is inside this proc for the whole
; bracket, so nothing else would.
;
; ALT+ENTER BACK is text modes only: a program that set a graphics mode has no
; text to bring. Going back PARKS the program first ([dvw_frz], and waits for
; [dvw_parked]), so its screen is read with nothing able to write it, and
; leaves it frozen in the window on the screen it last drew - DESQview's
; shape for a program that draws its own screen, which on an 8088 is the only
; honest one (docs/plans/DOS-WINDOWED-PLAN.md 4).
; -----------------------------------------------------------------------------
dvw_fsmain:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, [dos_win]
    mov ch, 0               ; CH = 0: BX's window's display (R12)
    call OSAPI_FSX_CAPS             ; the DISPLAY's kind (dos_fsx's reason)
    mov [con_tkind], dl
    push ds
    pop es
    mov di, dos_fsi
    mov al, FSXM_TEXT80
    call OSAPI_FSX_MODE
    jnc .on
    mov byte [PS dvw_fsreq], 0         ; refused: dvw_shunt hears no
    jmp .ret
.on:
    mov ax, [dos_fsi + FSI_SEG]     ; ...and the window's screen onto it:
    mov [con_tseg], ax              ; dos_fsx_con's seed
    mov byte [con_tcur], 0xFF
    HCALL con_tx_ice
    mov byte [con_thint], 0xFF
    HCALL con_markall
    HCALL dos_fsx_owed
    mov dh, [con_cy]                ; the ROM's cursor where the window's is,
    mov dl, [con_cx]                ; so the program's next teletype lands
    cmp dl, CON_COLS - 1            ; under what it already wrote
    jbe .cx
    mov dl, CON_COLS - 1
.cx:
    xor bh, bh
    mov ah, 0x02
    int 0x10
    cli                             ; **THE PROGRAM'S VIDEO BLOCK BECOMES THE
    push ds                         ; MACHINE'S**: its ROM calls read the BDA,
    DVW_PESDS                         ; and the window's mode-3 story is wrong
                                    ; on a Hercules (mode 7 at B000). The
    mov ax, 0x40                    ; world is out, so the block holds the
    mov ds, ax                      ; program's half
    mov si, 0x49
    mov di, PADDR(dvw_xblk) + DVW_XB_VID
    mov cx, 15
    cld
    rep movsw   ; DVWCHK-OK: the BDA -> the part's block (DVW_PESDS)
    mov si, 0x84
    mov cx, 3
    rep movsw   ; DVWCHK-OK: the BDA -> the part's block
    pop ds
    DVW_PESEND
    mov byte [dos_aedn], 1          ; the Alt+Enter that brought us here is
    mov byte [dos_inbr], 1          ; still held: SEEDED DOWN (96.33.5.1). And
    mov word [PS dvw_fs], 1         ; DOS's teletype goes to the ROM now -
    mov byte [PS dvw_fsreq], 0      ; and frz, the byte after fs, is 0: a
                                    ; FROZEN program thaws onto it
    sti
.loop:
    call dvw_serve                  ; (a parking program may be waiting on a
    cmp byte [dvw_done], 0          ; door to get there)
    jne .out                        ; it ended: dvw_fin has the screen
    cmp byte [PS dvw_frz], 0           ; going back: no keys, only the park
    jne .park
    HCALL dos_fsx_ae
    jc .back
.key:
    mov ah, 1                       ; the ROM's keys, the world being out on
    int 0x16                        ; this task, into the program's ring
    jz .idle
    xor ah, ah
    int 0x16
    call dvw_kput
    jmp short .key
.park:
    cmp byte [PS dvw_parked], 0
    jne .parked
.idle:
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    jmp short .loop
.back:
    cmp byte [PS dvw_gfx], 0
    jne .loop                       ; graphics: there is no text to bring back
    mov byte [PS dvw_frz], 1           ; ...else park it, round the loop above
    jmp short .loop
.parked:
    cli                             ; the PROGRAM's BDA and screen, read with
    call dvw_xin                    ; its world swapped in for one copy and
    HCALL dos_snap                   ; out again - dos_snap is memory only, so
    call dvw_xout                   ; nothing can take an interrupt through a
    sti                             ; half-swapped table
.out:
    mov byte [dos_inbr], 0          ; ...the console's teletype again, unless
    cmp byte [PS dvw_cmode], DVWC_SEG  ; the window is a capture, where the ROM's
    jne .inbr                       ; is the only one (96.54.6)
    mov byte [dos_inbr], 1
.inbr:
    mov byte [PS dvw_fs], 0
    HCALL con_markall                ; the band is owed whole on the way back
.ret:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

;
; =============================================================================
; WHAT THE PROGRAM WRITES ITSELF (SPEC.md 96.54.5, 96.54.6)
; =============================================================================
; Two ways a window can see a screen it did not draw through its own BIOS:
;
;   DVWC_TV   TopView's int 10h AH=FEh - the program ASKS where to write, and
;             is told con_scr. Its writes land in the band's own cells; a
;             row sum per row finds the ones that changed, and AH=FFh marks
;             them at once for a program that says so.
;   DVWC_SEG  the OTHER CARD'S text RAM: on a desktop with a second card that
;             is not part of it, that card's B800 or B000 is RAM nobody
;             shows. The program is told mode 3 or 7 to match and writes
;             there - directly or through the ROM - and the window copies
;             the rows that differ. Microsoft Works is the program this is
;             for: it asks the mode and then writes the screen itself.
;
; A pass runs on the UI task from dvw_wake, every fourth tick the program
; holds the CPU or sleeps (dvw_cpace), and never on the full screen or a
; frozen program, whose console is the captured glass.

; --- dvw_cpace - post the pass, every DVWC_EVERY+1 ticks (dvw_xpost's) --------
; in: DS = the host, every cell of the layer's own through CS
dvw_cpace:
    cmp byte [cs:dvw_cmode], 0
    je .r
    inc byte [cs:dvw_ctick]
    test byte [cs:dvw_ctick], DVWC_EVERY
    jnz .r
    cmp byte [cs:dvw_cmode], DVWC_HP    ; **A PAGE NOBODY HAS WRITTEN IS
    jne .post                       ; PROBED HERE** (96.54.6.1), one row, and
    cmp byte [cs:dvw_hprow], 0xFF   ; the UI task is woken only when it moved
    je .post                        ; - posting the pass to look was 1.9 of a
    cmp word [cs:dvw_fs], 0         ; (and not while the full screen or a
    jne .r                          ; freeze owns the console - [dvw_frz] is
                                    ; the byte after: dvw_capt would not look
                                    ; either)
    push ax                         ; spinning program's 88 points
    push bx
    push cx
    push dx
    push si
    pushf
    cld
    mov bl, [cs:dvw_hprow]
    xor bh, bh
    call dvw_hpsum
    shl bx, 1
    cmp [cs:dvw_sums + bx], dx
    jne .moved
    shr bx, 1                       ; unmoved: the next row, next time
    inc bx
    cmp bl, CON_ROWS
    jb .nx
    xor bl, bl
.nx:
    mov [cs:dvw_hprow], bl
    jmp short .pend
.moved:
    call .post                      ; (popf below is the CALLER's flags)
.pend:
    popf
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret
.post:
    mov byte [dvw_dirty], 1         ; the paint post below is the pass's door
    ret

; -----------------------------------------------------------------------------
; dvw_cdetect - is there a screen to capture? (worker, IF = 0, world OUT)
; out: CF=0 and [dvw_cseg] = B800h or B000h; CF=1 none. Every register kept
;
; THE CANDIDATE is the card the desktop is NOT on: B800 under a Hercules,
; B000 under anything else. It must be RAM (a pattern reads back), it must not
; be the desktop's own framebuffer seen twice (the pattern leaves the other
; segment's word alone - MartyPC's lone Hercules decodes B800 onto B000, and a
; capture there would read the desktop's pixels and let the program write
; them), and the desktop must not be EXTENDED onto that card (OSAPI_FSX_CAPS
; with CH = 2 asks about display 1, and CF = 1 is "there is none", SPEC.md
; 53.1.2) - in Extend, that RAM is the second monitor's desktop.
; -----------------------------------------------------------------------------
dvw_cdetect:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    call OSAPI_VIDEO                ; DL = the primary's kind
    mov si, 0xB800                  ; under a Hercules, the CGA's
    mov cx, 0xB000
    cmp dl, VID_HERC
    je .cand
    xchg si, cx                     ; under the rest, the mono card's
.cand:
    mov byte [PS dvw_chp], 0
    push cx                         ; (CX is the other segment)
    mov ch, 2                       ; display 1 (R12)
    call OSAPI_FSX_CAPS             ; CF = 0: a second display IS desktop
    pop cx                          ; (`pop` writes no flag)
    jnc .no
.try:
    mov ds, cx                      ; the OTHER segment's word, which a
    mov dx, [0]                     ; mirror would move
    mov ds, si
    mov bx, [0]                     ; ...and the candidate's own, kept
    mov ax, 0x5AA5
    mov [0], ax
    cmp [0], ax
    jne .back
    not ax                          ; A55Ah
    mov [0], ax
    cmp [0], ax
    jne .back
    mov ds, cx
    cmp [0], dx                     ; unmoved: two memories, not one
    mov ds, si
.back:
    mov [0], bx                     ; (ZF = 0 from either jne)
    jne .miss
    mov ax, [DVWC_OWN]              ; **ONE WINDOW AT A TIME HOLDS THE CARD**
    PARTCS dx                       ; (SPEC.md 96.54.13): its holder's layer
    cmp ax, dx                      ; segment is marked in the card's own
    je .mine                        ; unused tail, and a holder still HOLDING
    or ax, ax                       ; (none marked)
    jz .mine
    mov ds, ax                      ; it - its dvw_cseg this card - refuses
    cmp [ds:dvw_cseg], si   ; DVWCHK-OK: the holder's own cell, another instance's layer
    mov ds, si                      ; it. Without it a lone Hercules's page 1,
    je .held                        ; left decoded by the first window, passed
.mine:                              ; the CGA test in the second - whose ROM
    mov [DVWC_OWN], dx              ; int 10h then wrote the DESKTOP (B000)
    mov [cs:dvw_cseg], si
    clc
    jmp short .x
.held:
    mov byte [cs:dvw_chp], 0        ; (its decode is the holder's: left on)
    jmp short .no
.miss:
    HOSTDS                         ; **NO CGA AT B800 UNDER A HERCULES: ITS
                                    ; OWN PAGE 1 IS** (96.54.6.1). 3BFh bit 1
    cmp si, 0xB800                  ; turns the second 32KB's decode on, which
    jne .no                         ; vid_setmode leaves off only because a
    mov al, 3                       ; CGA would answer there too - and the
    xor byte [PS dvw_chp], 1           ; test above says none does. Page 0 stays
    jnz .page                       ; what is SHOWN; the same test then
    mov al, 1                       ; decides, so a card that mirrors (or
.page:                              ; lacks the page) is refused, the decode
    pushf                           ; put back
    call dvw_hpage
    popf
    jnz .try
.no:
    stc
.x:
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dvw_ccheck - is the capture card STILL nobody's? (UI task, DS = the host)
; out: nothing; every register kept
;
; dvw_cdetect asks once, at the launch, and the Control Panel can change the
; answer under a live run: Extend onto the card, or make it the primary. So
; the test that decides it is made again - display 1 desktop or not, and the
; primary's kind against the launch's [dvw_dkind] - at every capture pass and
; before the run's end gives the card back. Either one moved: the capture is
; DROPPED where it stands, [dvw_cseg] zeroed with no holder's mark written and
; no blank (the card is the desktop's now), and the run goes on as a plain
; window, as 96.54.13's second window does. A program that writes the card
; itself cannot be stopped (96.54.1); its int 10h and DOS's teletype can.
; -----------------------------------------------------------------------------
dvw_ccheck:
    cmp word [PS dvw_cseg], 0
    je .r
    push ax
    push bx
    push cx
    push dx
    mov ch, 2                       ; display 1 (R12)
    call OSAPI_FSX_CAPS             ; CF = 0: a second display IS desktop
    jnc .lost
    call OSAPI_VIDEO                ; DL = the primary's kind
    cmp dl, [PS dvw_dkind]
    je .x
.lost:
    mov word [PS dvw_cseg], 0
    mov byte [PS dvw_cmode], 0      ; int 10h the virtual one again, and...
    mov byte [dos_inbr], 0          ; ...DOS's teletype the console's
.x:
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; dvw_cprog / dvw_cblank - the capture card's 6845, up for text and back dark
;
; **A CARD NOBODY PROGRAMMED HAS NO RETRACE** - MEASURED with Microsoft Works:
; its screen writer waits for the CGA's status port (3DAh) to show a retrace
; before EVERY character, the snow-avoidance loop of the period, and on a
; second card that Single mode only BLANKS (vid_blank_kind writes the mode
; register and nothing else, SPEC.md 39.19) the 6845 was never given a
; timing: the bits never move and Works waited for ever with nothing drawn.
; So the capture programs the card for 80x25 text and turns its video on -
; the second monitor shows the program too, which is the period's own look -
; and dvw_cblank turns the video off again at the run's end, the one bit the
; kernel's blank writes. Its 6845 is left timed for text: Extend's bring-up is
; a full mode set (vid_disp_init), so nothing reads the old timing again.
; -----------------------------------------------------------------------------
dvw_crtc_cga:   db 0x71, 0x50, 0x5A, 0x0A, 0x1F, 0x06, 0x19, 0x1C
                db 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00
dvw_crtc_mono:  db 0x61, 0x50, 0x52, 0x0F, 0x19, 0x06, 0x19, 0x19
                db 0x02, 0x0D, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x00
dvw_cprog:
    push ax
    push cx
    push dx
    push si
    mov si, PADDR(dvw_crtc_cga)
    mov dx, 0x3D8
    mov al, 0x21                    ; text, 80 columns, video OFF while timed
    cmp word [cs:dvw_cseg], 0xB000
    jne .mode
    mov si, PADDR(dvw_crtc_mono)
    mov dx, 0x3B8
    mov al, 0x20                    ; mono text, video off (3BFh stays 1:
.mode:                              ; graphics ALLOWED is not graphics ON)
    out dx, al
    push ax
    sub dx, 4                       ; 3D4h / 3B4h
    xor ah, ah                      ; AH = the register
    mov cx, 16
.reg:
    mov al, ah
    out dx, al
    inc dx
    cs lodsb    ; DVWCHK-OK: our own table, through CS
    out dx, al
    dec dx
    inc ah
    loop .reg
    pop ax
    add dx, 4
    or al, 0x08                     ; ...and the video ON
    out dx, al
    pop si
    pop dx
    pop cx
    pop ax
    ret

dvw_cblank:
    push ax
    push dx
    mov dx, 0x3D8
    mov al, 0x21
    cmp word [cs:dvw_cseg], 0xB000
    jne .out
    mov dx, 0x3B8
    mov al, 0x20
.out:
    out dx, al                      ; video OFF, the one bit vid_blank_kind's
    pop dx                          ; own blank clears
    pop ax
    ret

; --- dvw_cmset - int 10h AH=00h inside a capture (program context, world in) -
; A TEXT mode clears the captured screen and homes the cursor, and is NOT
; passed to the ROM: on a mono primary the ROM's mode set is equipment-driven
; and programs the desktop's own card (SPEC.md 39.19.4). CF=1 a graphics mode,
; which the caller shunts to the full screen like any other.
dvw_cmset:
    push ax
    and al, 0x7F
    cmp al, 3
    jbe .text
    cmp al, 7
    je .text
    pop ax
    stc
    ret
.text:
    push cx
    push di
    push es
    mov es, [cs:dvw_cseg]
    xor di, di
    mov ax, 0x0720
    mov cx, CON_SCRSZ / 2
    cld
    rep stosw   ; DVWCHK-OK: into the captured card's RAM
    mov ax, 0x40
    mov es, ax
    mov [es:0x50], cx               ; page 0's cursor home (CX = 0)
    pop es
    pop di
    pop cx
    pop ax
    clc
    ret

; --- dvw_tvmark - TopView AH=FFh: ES:DI, CX cells changed in con_scr ---------
dvw_tvmark:
    push ax
    push bx
    push cx
    push dx
    push ds
    HOSTDS
    mov ax, di
    sub ax, con_scr
    jb .out
    shr ax, 1                       ; the first cell
    jcxz .out
    mov bx, ax
    add bx, cx
    dec bx                          ; ...and the last
    cmp bx, CON_SCRSZ / 2 - 1
    jbe .have
    mov bx, CON_SCRSZ / 2 - 1
.have:
    cmp ax, bx
    ja .out
    HCALL con_markcells
    mov byte [dvw_dirty], 1
.out:
    pop ds
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dvw_capt - one capture pass (dvw_wake: UI task, lock free)
; -----------------------------------------------------------------------------
dvw_capt:
    cmp byte [PS dvw_cmode], 0
    je .r
    cmp word [PS dvw_fs], 0            ; the full screen owns the glass, and a
    jne .r                          ; FROZEN program's console is its capture
                                    ; ([dvw_frz], the byte after)
    call dvw_ccheck                 ; ...and the card still nobody's (96.54.6)
    cmp byte [PS dvw_cmode], 0
    je .r
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push ds
    pop es
    xor bx, bx                      ; BX = the row
    mov al, [PS dvw_cmode]
    cmp al, DVWC_SEG
    je .seg
    cmp al, DVWC_HP
    je .hp
    mov si, con_scr
.sum:                               ; --- TopView: a sum per row -----------
    call dvw_rsum                   ; (SI to the next row)
    call dvw_sumst
    je .snext
    HCALL con_mark
    call dvw_kmore                  ; (96.54.5.1)
.snext:
    inc bx
    cmp bx, CON_ROWS
    jb .sum
    jmp .hpend
.hp:                                ; --- a lone Hercules's page 1 --------
    mov al, [PS dvw_hprow]             ; (96.54.6.1): ONE row a pass while
    cmp al, 0xFF                    ; nothing has written it - int 10h is
    je .hpall                       ; ours, so only a DIRECT writer can -
    mov bl, al                      ; and every row once one has (the
    call dvw_hprw                   ; tick's probe has already advanced
    ; past every unmoved row, so the one it stopped on is the one that moved)
    jnc .hpend
    mov byte [PS dvw_hprow], 0xFF
    jmp short .hpend
.hpall:
    call dvw_hprw
    inc bx
    cmp bx, CON_ROWS
    jb .hpall
.hpend:
    jmp .done
.seg:                               ; --- the other card: copy what moved --
    xor si, si                      ; DS:SI the card's row...
    mov di, con_scr                 ; ...ES:DI ours
.segrow:
    push ds
    mov ds, [cs:dvw_cseg]
    mov cx, CON_COLS
    repe cmpsw  ; DVWCHK-OK: the card's row against con_scr (ES the host)                      ; equal to the end: both past the row
    je .same
    dec si                          ; ...else the rest from the first word
    dec si                          ; that differs, which leaves both past
    dec di                          ; it too
    dec di
    inc cx
    rep movsw   ; DVWCHK-OK: the card's row -> con_scr
    pop ds
    HCALL con_mark
    call dvw_kmore                  ; (96.54.5.1)
    jmp short .gnext
.same:
    pop ds
.gnext:
    inc bx
    cmp bx, CON_ROWS
    jb .segrow
    mov ax, [PS dvw_xblk + DVW_XB_VID + 7]     ; the program's cursor, its BDA
    xor dh, dh                              ; being in the block while the
    mov dl, al                              ; world is out
    cmp dl, CON_COLS - 1
    jbe .cx
    mov dl, CON_COLS - 1
.cx:
    mov al, ah
    xor ah, ah
    cmp al, CON_ROWS - 1
    jbe .cy
    mov al, CON_ROWS - 1
.cy:
    cmp dx, [con_cx]
    jne .mv
    cmp ax, [con_cy]
    je .done
.mv:
    HCALL con_markcur
    mov [con_cx], dx
    mov [con_cy], ax
    mov byte [con_pwrap], 0
    HCALL con_markcur
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; dvw_rsum - DX = the sum of the row at DS:SI, order-sensitive (a swap is a
; change); SI past it. Clobbers AX, CX
dvw_rsum:
    xor dx, dx
    mov cx, CON_COLS
.add:
    lodsw       ; DVWCHK-OK: con_scr (DS the host) or the card's
    add dx, ax
    rol dx, 1
    loop .add
    ret

; dvw_hpsum - DX = the sum of page 1's row BX, SI its offset there.
; Clobbers AX, CX
dvw_hpsum:
    push ds
    mov ax, CON_COLS * 2
    mul bx
    mov si, ax
    mov ds, [cs:dvw_cseg]
    call dvw_rsum
    pop ds
    sub si, CON_COLS * 2
    ret

; dvw_sumst - DX = row BX's sum now: ZF=1 it is the one [dvw_sums] holds
; (and CF=0), else it is stored there. Clobbers DI
dvw_sumst:
    mov di, bx
    shl di, 1
    cmp [PS dvw_sums + di], dx
    je .r
    mov [PS dvw_sums + di], dx         ; (ZF = 0 still)
.r:
    ret

; dvw_hprw - page 1's row BX: CF=1 it moved, and it is copied into the
; console and marked. Keeps BX; clobbers AX, CX, DX, SI, DI
dvw_hprw:
    call dvw_hpsum
    call dvw_sumst
    je .same                        ; (CF = 0)
    mov di, si
    add di, con_scr
    push ds
    mov ds, [cs:dvw_cseg]
    mov cx, CON_COLS
    rep movsw   ; DVWCHK-OK: the card's row -> con_scr (ES the host)
    pop ds
    HCALL con_mark
    call dvw_kmore                  ; (96.54.5.1)
    stc
.same:
    ret

; --- dvw_onclose - OSAPI_WM_ONCLOSE: a running program is stopped, not kept --
; **THE CLOSE IS REFUSED, AND FINISHED BY US** (SPEC.md 75.1's own pattern).
; Letting it through hides the window and leaves the worker to free the region
; at its next OSAPI_TASK_ALIVE - which it reaches the moment the stopped
; program has gone, while the UI task may still be INSIDE dvw_wake finishing
; the run. Measured: the region was freed under it and the machine ran off
; into the heap. So the program is stopped, dvw_wake ends the run on the UI
; task as it ends any run, and only then asks for the close it owes - after
; which the worker dies from its idle loop, where nothing of ours is running
; anywhere else.
dvw_onclose:
    cmp byte [dvw_run], 0
    je .ok                          ; (CF = 0)
    mov byte [PS dvw_kill], 1          ; the worker's next tick terminates it...
    mov byte [PS dvw_closeq], 1        ; ...and dvw_wake closes us afterwards
    stc
.ok:
    ret

%endif                              ; [P]
%endif                              ; DVW
