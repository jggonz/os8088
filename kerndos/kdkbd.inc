; =============================================================================
; kerndos/kdkbd.inc - THE BIOS KEY BUFFER'S GUARD, ON A MACHINE WITH NO KERNEL
; (SPEC.md 96.50)
;
; **THE FAILURE IS SPEC.md 9.8's, IN THE ONE PLACE 9.8 DOES NOT REACH.** Hold a
; direction key in a game that is busy drawing a frame, and the BIOS key buffer
; fills; the ROM's `int 09h` then BEEPS, and the beep is longer than the
; typematic interval, so the next repeat arrives, overflows, and beeps again.
; It does not stop. 9.8 calls it "unbounded and fatal" and it is the reported
; symptom word for word: *holding left tries to beep me to death*.
;
; os8088 fixed this once. `kbd_ovflow` sits in front of the ROM's handler and,
; on a FULL buffer, un-enqueues the NEWEST entry - the tail winds back one slot
; - so the ROM finds room, stores the arriving key where the previous newest
; sat, and never reaches its beep. What is dropped is the new input; the
; fifteen keys already queued keep their order and reach the application.
;
; **AND `kern_dos` IS THE ONE THING WE SHIP WITHOUT IT**, for a reason that is
; correct as far as it goes: the handoff's step 6 calls `mouse_unhook`
; (kernel/hiber.inc) and MUST, because `kbm_isr` sits at a KERNEL_SEG offset
; that is this image one instruction later. Unhooking is right. Putting nothing
; back in its place is the gap, and this file is the same answer `kdmouse.inc`
; gave for `int 33h`: a SECOND, much smaller driver for a thing the kernel had.
;
; MEASURED, one probe, three machines - the `int 09h` vector:
;
;   os8088 desktop, and the WINDOWED box   0060:3986   = `kbm_isr`, guarded
;   kern_dos, before this file             F000:E987   = the ROM, unguarded
;   IBM DOS 3.30 at its own prompt         F000:E987   = the ROM, unguarded
;
; So this is not a regression and it was never ours: a real DOS leaves the
; identical vector and would beep identically. It is simply that os8088 had
; already decided the failure was unacceptable, and the third arm did not
; inherit the decision.
;
; **IT IS SILENT, AND THAT IS A CHOICE.** SPEC.md 9.8 beeps three times per
; burst because the beep is what tells the user their typing is going nowhere.
; There is no `snd_beep` here and no task context to defer one into - 9.8's own
; rule is that the ISR only ever sets a byte - and the program that provokes
; this is fullscreen with its own sound. So the survivability half ships and
; the announcement does not.
;
; **NO DS, NO BSS, NO STATE.** Dropping the beep drops `kbd_ovn` with it, so
; what is left reads two words of the BIOS data area and writes one. That is
; why this can run at the gate with the PROGRAM's DS in DS and care about
; neither: it never names a variable of ours.
;
; Gate it back off with `make NOKDKBD=1`, which leaves kern_dos exactly as DOS
; leaves it.
; =============================================================================

KDK_VEC     equ 0x09 * 4        ; the IVT slot, and the only one this file takes

; kernel/mouse.inc's own two constants, and LITERAL for its reason: 0040:0080
; and 0040:0082 do not exist on the earliest 5150 ROM, so reading them there is
; reading whatever happens to be at 40:80 - and the failure that produces is a
; tail wound outside the buffer, which is a keyboard that has stopped working.
KDK_BUFB    equ 0x1E            ; 0040:001E, the buffer...
KDK_BUFE    equ 0x3E            ; ...and one past its end

; -----------------------------------------------------------------------------
; kd_kbd_start - put the guard in front of the ROM's int 09h
; in:  DS = ES = KD_SEG
; out: nothing; every register preserved
;
; **BEFORE `dos_save_machine`**, exactly as `kd_mou_start` is and for the same
; reason: the whole IVT is what `dos_restore_machine` puts back, so a vector
; taken after the bank is one the program's own exit silently undoes.
; -----------------------------------------------------------------------------
kd_kbd_start:
    push ax
    push bx
    push es
    xor ax, ax
    mov es, ax
    pushf
    cli                         ; four words, and an interrupt between any two
    mov ax, [es:KDK_VEC]        ; of them hands the 8259 half an address
    mov [kd_kbold], ax
    mov ax, [es:KDK_VEC+2]
    mov [kd_kbold+2], ax
    mov word [es:KDK_VEC], kd_kbd_isr
    mov [es:KDK_VEC+2], cs
    popf
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kd_kbd_stop - the ROM's own handler back
;
; `kd_leave`'s reason, unchanged: every exit below hands the machine to
; somebody else, and OUR ISR sits at a KD_SEG offset that is the next owner's
; code the instant it loads. A second call is a no-op because `kd_leave` has
; two exits.
; -----------------------------------------------------------------------------
kd_kbd_stop:
    push ax
    push es
    cmp word [kd_kbold+2], 0
    je .none                    ; never hooked, or already put back
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [kd_kbold]
    mov [es:KDK_VEC], ax
    mov ax, [kd_kbold+2]
    mov [es:KDK_VEC+2], ax
    mov word [kd_kbold+2], 0
    popf
.none:
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; kd_kbd_isr - the guard, and then the ROM
;
; ON THE INTERRUPTED PROGRAM'S STACK, `kd_mou_isr`'s reason: there are no task
; slices here, and three words in front of a ROM handler that is about to spend
; far more is what an int 09h costs under DOS anyway.
;
; IT CHAINS WITH `jmp far` AND NOT `call`: the ROM's handler ends in `iret` and
; the frame it returns through is the one the CPU pushed at the gate, so this
; must leave that frame exactly as it found it. A `call` would put a return
; address under it and the `iret` would go to the wrong place with the wrong
; flags.
;
; IT PREDICTS NOTHING (SPEC.md 9.8): no scancode is read, so a key RELEASE on a
; full buffer costs the newest queued key too. Every filter that tried to be
; cleverer than that is a guess with holes in it - Alt released with a pending
; Alt-numpad value enqueues a character from a BREAK code - and a hole here is
; not a lost keystroke, it is the hang this file exists to stop.
; -----------------------------------------------------------------------------
kd_kbd_isr:
    push ax
    push bx
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x1A]           ; head - where the reader is
    mov bx, [es:0x1C]           ; tail - where the ROM is about to write
    cmp ax, bx
    je .out                     ; empty: the ordinary state, and the one every
                                ; program that reads its keyboard stays in
    cmp ax, KDK_BUFB            ; a head this arithmetic cannot reason about is
    jb .out                     ; left alone and the ROM's own behaviour stands
    cmp ax, KDK_BUFE
    jae .out
    test al, 1
    jnz .out
    inc bx                      ; the slot the ROM is about to fill...
    inc bx
    cmp bx, KDK_BUFE
    jb .nowrap
    mov bx, KDK_BUFB
.nowrap:
    cmp bx, ax
    jne .out                    ; ...is not the head: there is room, and this is
                                ; the path every ordinary keystroke takes
    sub ax, 4                   ; FULL. head - 4 is the slot BEFORE the newest,
    cmp ax, KDK_BUFB            ; which is where the tail belongs once the
    jae .wind                   ; newest is gone
    add ax, KDK_BUFE - KDK_BUFB
.wind:
    mov [es:0x1C], ax
.out:
    pop es
    pop bx
    pop ax
    jmp far [cs:kd_kbold]       ; ...and the ROM does the real work: the
                                ; scancode, the shift state, the LEDs and
                                ; Ctrl-Alt-Del are all still its business

section .bss
kd_kbold:   resd 1              ; the ROM's own handler. The SEGMENT half being
                                ; zero is `kd_kbd_stop`'s "not hooked" test,
                                ; and nobits is not zeroed by anything here -
                                ; `kd_kbd_start` writes both halves before
                                ; anything can read them, and it is called once
section .text
