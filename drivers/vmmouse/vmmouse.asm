; =============================================================================
; os8088 - VMMOUSE.DRV
;
; The VMware absolute pointer (SPEC.md 9.11): a grabless mouse under v86 in
; the browser, and under QEMU, VMware and VirtualBox on a desktop. The host
; already knows where the pointer is; this asks it, so os8088's arrow sits
; under the user's real cursor with no capture and no pointer lock.
;
; **IT IS AN OVERLAY, NOT A DRIVER, AND THE REASON IS 386 INSTRUCTIONS.**
; The backdoor protocol is 32-bit by construction - the magic is a dword, the
; command register is ECX and the call itself is `in eax, dx`. On the 8088
; this project is calibrated against there are no 32-bit registers and 0x66 is
; not a prefix but an invalid opcode, so every byte of this file is code that
; MUST NEVER EXECUTE ON THE TARGET MACHINE.
;
; The first draft of this feature put it in kernel/mouse.inc, gated on
; [cpu_tier]. That gating was sound as far as it went, and it still cost the
; XT **548 bytes of resident kernel image** - one whole 512-byte footprint
; rung, which is 512 bytes of every machine's RAM (tools/kernsize.py said so
; in those words), spent on a machine that can never reach a hypervisor. It is
; SPEC.md 41.12's argument for XMEM.DRV, one for one, and it gets the same
; answer: the feature ships as a FILE and the machines that cannot use it
; never read it.
;
; It also closes a hole that no amount of [cpu_tier] gating could: SPEC.md 87's
; hibernate writes the machine's RAM wholesale and validates five things on
; resume, none of them the CPU. Resident 386 code plus a restored "the backdoor
; is on" byte is an 8088 executing `push eax` after a disk is moved between
; machines. Hibernate DETACHES every driver that is not DRVC_DISK or
; DRVC_FILE before it writes (SPEC.md 87.4 step 1), so an image cannot carry
; this file's code back to a machine that would trap on it.
;
; --- WHAT IT HOOKS: NOTHING -------------------------------------------------
; No interrupt vector, no IRQ line, no DMA channel, no port it keeps. The
; queue is POLLED from the kernel's own service point, so DRVV_ATTACH is
; all-or-nothing for free - a refusal has nothing to undo. The one piece of
; machine state it changes is the 8042's aux enable (vmm_p2wake), and DETACH
; puts that back.
;
; --- WHY IT IS DRVC_OVL AND STILL HAS A drv_tab ROW -------------------------
; A class is a PUBLICATION slot (SPEC.md 51.2.1) and nothing needs to FIND
; this image by class: the kernel is its only caller and holds the row. So it
; takes no class - DRVC_MAX stays 5 - and HDDTOOL.DRV's and XMEM.DRV's shape
; is what it is. What it DOES have that those two do not is a row in drv_tab,
; and that buys the one thing SPEC.md 41.12 correctly said XMEM did not need:
; a decision. Whether the backdoor exists is not a matter of opinion, but
; whether the user WANTS a grabless pointer is - and the probe is 386 code, so
; there is no resident sniff that could answer the first question anyway.
; The SYSTEM.CFG bit is therefore both the request and the record of it, and
; the Drivers page is where it is turned off again.
; =============================================================================

%include "os88drv.inc"
%include "vmmabi.inc"

    OS88_OVERLAY 'VMware Mouse', VMM_ABI_VER, vmm_entry

; --- the protocol (SPEC.md 9.11.1) -------------------------------------------
VMM_MAGIC   equ 0x564D5868      ; 'VMXh' - EAX in, and EBX back on success
VMM_PORT    equ 0x5658          ; 'VX'   - the backdoor port, in DX

VMM_GETVER  equ 10              ; commands, in ECX
VMM_DATA    equ 39
VMM_STATUS  equ 40
VMM_COMMAND equ 41

VMM_READID  equ 0x45414552      ; EBX for VMM_COMMAND: READ_ID - queues the
                                ; version word AND clears a disabled backdoor
VMM_ABS     equ 0x53424152      ; ...REQUEST_ABSOLUTE

VMM_S_LEFT  equ 0x20            ; buttons in the status word
VMM_S_RIGHT equ 0x10
                                ; the RELATIVE_PACKET bit is 0x00010000,
                                ; tested in the high word as 0x0001

VMM_QMIN    equ 4               ; words queued before a packet can be read
VMM_FLUSHMX equ 64              ; ...and the BOUND on the drain below. See
                                ; vmm_flush: an unbounded loop at attach time
                                ; is a hang on a host that answers wrongly,
                                ; and a hang here is a machine that never
                                ; finishes booting

; --- the 8042, for vmm_p2wake ------------------------------------------------
VMM_P2CMD   equ 0x64            ; the controller's command/status port
VMM_P2DAT   equ 0x60            ; ...and its data port
VMM_P2TMO   equ 2               ; ticks to wait for one byte, mou_p2rd's own
VMM_P2AUX   equ 0x20            ; status bit 5: the byte in the buffer is the
                                ; AUXILIARY device's, not the keyboard's

; =============================================================================
; vmm_entry - the dispatcher's landing site
; in:  AL = a DRVV_*; DS = CS = ours, ES = KERNEL_SEG
; out: per the verb
; =============================================================================
; Each arm is `jne` over an explicit near `jmp`, and that is not a style
; choice: the three bodies are 300-odd bytes below this, so a bare `je` is out
; of the 8086's +/-127 range and NASM synthesises the pair itself - at a size
; that changed between passes and failed the assembly outright
; (label-redef-late). Written out, it is the same four bytes an arm and it
; converges.
vmm_entry:
    cmp al, DRVV_ATTACH
    jne .nota
    jmp vmm_attach
.nota:
    cmp al, DRVV_DETACH
    jne .notd
    jmp vmm_detach
.notd:
    cmp al, VMMV_READ
    jne .no
    jmp vmm_read
.no:
    stc                         ; a verb from a newer kernel than this image -
    ret                         ; refuse it rather than run another one

; -----------------------------------------------------------------------------
; vmm_bd - one backdoor call
;
; in:  CX = command, [vmm_ebx] = the EBX argument
; out: [vmm_eax]/[vmm_ebx]/[vmm_ecx]/[vmm_edx] = the four result registers;
;      every 8086 register preserved, and the 32-bit halves with them
; clobbers: nothing (flags restored with IF by the popf)
;
; **THE cli IS THE WHOLE CORRECTNESS ARGUMENT, and it is xmem.asm's, taken
; properly this time.** The first draft ran this window with IF as the caller
; left it and cited xmem.asm as licensing that; xmem.asm does no such thing -
; its islands sit inside pushf/cli...popf and its own header says that window
; is what makes them safe. The hazard is not hypothetical here: this is called
; from task_yield, so a tick between `mov eax, VMM_MAGIC` and `in eax, dx`
; switches tasks, and sch_switch saves 16-bit registers only. XMEM.DRV's
; movers are 32-bit and are exactly the other tenant of a machine with memory
; above 1MB; SeaBIOS is 16-bit code compiled from C and spends EAX freely. A
; clobbered top half means the magic is wrong, the host does not answer, and
; the mouse stops - intermittently, which is the worst way for it to stop.
;
; The window is ~20 instructions with one `in`, so the interrupt latency it
; adds is well inside what mou_p2rd already spends.
; -----------------------------------------------------------------------------
cpu 386                         ; ---- 386-only island, tier 2 only ----------
vmm_bd:
    pushf
    cli
    push eax
    push ebx
    push ecx
    push edx
    mov  eax, VMM_MAGIC
    movzx ecx, cx
    mov  ebx, [vmm_ebx]
    mov  edx, VMM_PORT
    in   eax, dx
    mov  [vmm_eax], eax
    mov  [vmm_ebx], ebx
    mov  [vmm_ecx], ecx
    mov  [vmm_edx], edx
    pop  edx
    pop  ecx
    pop  ebx
    pop  eax
    popf
    ret
cpu 8086                        ; ---- island closed ------------------------

; -----------------------------------------------------------------------------
; vmm_cmd - stage a 32-bit EBX argument and make one backdoor call
; in:  CX = command, DX:AX = the EBX argument (DX high)
; out: as vmm_bd
; clobbers: nothing (flags)
;
; Three call sites staged those two words by hand and it was six lines each
; time; this is the same six lines once.
; -----------------------------------------------------------------------------
vmm_cmd:
    mov [vmm_ebx], ax
    mov [vmm_ebx+2], dx
    jmp short vmm_bd

; -----------------------------------------------------------------------------
; vmm_flush - read the queue to empty, discarding, in <= 6-word chunks
;
; in:  none.  out: nothing (all registers preserved)
;
; The 4-word packet framing only holds if reads start from an empty queue.
; READ_ID queues a LONE version word, and QEMU's vmmouse disables the backdoor
; outright if a DATA read asks for more words than are queued - so a misframed
; read that then underflows costs the mouse for the session. Draining after
; every enable keeps the framing honest.
;
; **IT IS BOUNDED, and that is a fix rather than a flourish.** "The count
; strictly fell, so this terminates" is true of a host that behaves; this runs
; at ATTACH, inside the boot sequence, and a host whose STATUS count does not
; fall after a DATA read would hang the machine before it ever reached a
; desktop. VMM_FLUSHMX passes at 6 words each is 384 words, far more than any
; queue this protocol defines, so the bound cannot be hit by a working host.
; -----------------------------------------------------------------------------
vmm_flush:
    push ax
    push bx
    push cx
    push dx
    mov  bx, VMM_FLUSHMX        ; BX, which nothing below spends: AX and DX are
                                ; vmm_cmd's argument and CX is its command
.f:
    xor  ax, ax
    xor  dx, dx
    mov  cx, VMM_STATUS
    call vmm_cmd
    mov  ax, [vmm_eax+2]
    cmp  ax, 0xFFFF             ; disabled - the caller re-enables, nothing
    je   .fout                  ; here to drain
    mov  ax, [vmm_eax]          ; words queued
    or   ax, ax
    jz   .fout
    cmp  ax, 6
    jbe  .rd
    mov  ax, 6                  ; at most 6 a call (QEMU's cap)
.rd:
    xor  dx, dx
    mov  cx, VMM_DATA
    call vmm_cmd                ; ...into vmm_eax..edx, discarded
    dec  bx
    jnz  .f
.fout:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vmm_enable - READ_ID, go ABSOLUTE, then drain (SPEC.md 9.11.1)
;
; in:  none.  out: nothing (all registers preserved)
;
; READ_ID (0x45414552) is the one command that clears QEMU's disabled state
; (status = 0xFFFF) - REQUEST_ABSOLUTE alone is ignored while disabled - and it
; queues a version word as a side effect. So enable is always: READ_ID,
; REQUEST_ABSOLUTE, then vmm_flush to throw away that word and anything the
; re-added handler queued behind it. The version is NOT read back and checked:
; that check is a DATA read whose size can only be guessed, and guessing wrong
; is what desyncs the framing and sticks the pointer. The GETVERSION probe in
; vmm_attach is the gate; this path only has to not make things worse.
; -----------------------------------------------------------------------------
vmm_enable:
    push ax
    push cx
    push dx
    mov  ax, VMM_READID & 0xFFFF
    mov  dx, VMM_READID >> 16
    mov  cx, VMM_COMMAND
    call vmm_cmd                ; READ_ID: clears 0xFFFF, re-adds the handler

    mov  ax, VMM_ABS & 0xFFFF
    mov  dx, VMM_ABS >> 16
    mov  cx, VMM_COMMAND
    call vmm_cmd                ; ABSOLUTE

    call vmm_flush              ; ...and start the framing from empty
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; The 8042 half (SPEC.md 9.11.2)
;
; os8088 reads positions from the backdoor and never decodes a PS/2 packet.
; But v86 only starts DELIVERING mouse events to the page - and only hides the
; host cursor - once the guest ENABLES the PS/2 mouse stream. Until then the
; browser's mouse handler is dead and the pointer does not move at all. QEMU
; and VMware deliver regardless; v86 gates on it.
;
; So: 0xF4 (enable - the host flips its handler on), then immediately 0xF5
; (stop the stream) so no PS/2 packet is ever queued and int 74h can stay
; unhooked. 0xF5 does NOT undo the host's "mouse enabled" state, so
; mouse-absolute keeps flowing to the backdoor.
;
; **THE BRACKET IS NOT OPTIONAL, and its absence was a defect in the first
; draft.** SPEC.md 9.9.1 step 1 is explicit that mou_p2_init's 0xAD is "not
; tidiness": IRQ1 is tied to OBF and gated by the command byte's bit 0, so THE
; CONTROLLER'S OWN REPLIES RAISE IT TOO, and an int 09h that reads 0x60 takes
; them. The first draft wrote 0xD4/0xF4/0xD4/0xF5 with IRQ1 unmasked, the
; keyboard interface live, and reads that tested OBF alone - so each 0xFA ack
; could be delivered to int 09h as scan 0x7A, and a keystroke arriving in the
; window could be eaten from the keyboard instead. It also could not rely on
; kbm_isr's own aux filter, which is gated on [mou_p2].
;
; This does what mou_p2_init does: mask IRQ1, disable the keyboard interface,
; talk, filter every read on status bit 5, and put both back on every exit
; INCLUDING the timeout ones. Best-effort about the DEVICE - a controller that
; stalls was already delivering - but never about the keyboard.
; =============================================================================

; -----------------------------------------------------------------------------
; vmm_p2wait - wait for the input buffer to clear (status bit 1)
; out: CF = 1 = it never did.  clobbers: flags
; -----------------------------------------------------------------------------
vmm_p2wait:
    push ax
    push cx
    mov  cx, 0xFFFF
.w:
    in   al, VMM_P2CMD
    test al, 0x02
    jz   .ok
    loop .w
    stc
    jmp short .out
.ok:
    clc
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vmm_p2cw / vmm_p2dw - write a controller command / a data byte
; in:  AL = the byte.  out: CF = 1 = the buffer never cleared
; -----------------------------------------------------------------------------
vmm_p2cw:
    call vmm_p2wait
    jc   .out
    out  VMM_P2CMD, al
    clc
.out:
    ret

vmm_p2dw:
    call vmm_p2wait
    jc   .out
    out  VMM_P2DAT, al
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; vmm_p2aux - read one byte that is the AUXILIARY device's
;
; out: CF = 0 and AL = the byte; CF = 1 = it never came
; clobbers: AX (flags)
;
; **BIT 5, AND THAT IS THE POINT.** mou_p2rd tests OBF alone, which is correct
; inside mou_p2_init's 0xAD window where the keyboard cannot speak. Here the
; window is ours but the rule is worth keeping locally rather than inherited:
; a byte without bit 5 is the keyboard's and is LEFT IN THE BUFFER, not
; consumed - taking it is how a keystroke goes missing.
;
; The wait is a tick count and not a spin count, so a host that never answers
; costs VMM_P2TMO ticks and not a wall-clock guess.
; -----------------------------------------------------------------------------
vmm_p2aux:
    push bx
    push cx
    push es
    mov  bx, 0x40
    mov  es, bx
    mov  bx, [es:0x6C]          ; the BIOS tick, low word - the deadline base
.w:
    in   al, VMM_P2CMD
    test al, 0x01               ; OBF: is there anything at all?
    jz   .tick
    test al, VMM_P2AUX          ; ...and is it the AUX device's?
    jz   .tick                  ; no - the KEYBOARD's. Leave it for int 09h
    in   al, VMM_P2DAT
    clc
    jmp short .out
.tick:
    mov  cx, [es:0x6C]
    sub  cx, bx
    cmp  cx, VMM_P2TMO
    jb   .w
    stc
.out:
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; vmm_p2wake - tell the emulated 8042 the guest wants the mouse
; in:  AL = the device command to send (0xF4 enable / 0xF5 disable)
; out: nothing (all registers preserved)
;
; ONE command, bracketed. The caller sends 0xF4 then 0xF5; doing them in one
; bracket would leave the aux stream enabled for the width of two acks with
; int 74h unhooked, and a 3-byte packet queued in that window is three garbage
; scancodes and, if the buffer fills, a starved keyboard.
; -----------------------------------------------------------------------------
vmm_p2wake:
    push ax
    push bx
    mov  bl, al                 ; the device command, banked across the setup

    in   al, 0x21               ; --- IRQ1 masked FIRST (SPEC.md 9.9.1) ------
    push ax                     ; ...and the old mask kept for the restore
    or   al, 0x02
    out  0x21, al

    mov  al, 0xAD               ; ...and the keyboard interface disabled, so
    call vmm_p2cw               ; the controller's replies cannot reach int 09h

    mov  al, 0xD4               ; "the next 0x60 byte is for the AUX device"
    call vmm_p2cw
    jc   .done
    mov  al, bl
    call vmm_p2dw
    jc   .done
    call vmm_p2aux              ; its 0xFA ack - taken, so it is not left in
                                ; the buffer for the BIOS to read as a
                                ; scancode the moment 0xAE goes back
.done:
    mov  al, 0xAE               ; --- and BOTH back, on every exit -----------
    call vmm_p2cw
    pop  ax
    out  0x21, al
    pop  bx
    pop  ax
    ret

; -----------------------------------------------------------------------------
; vmm_p2both - 0xF4 then 0xF5, each in its own bracket
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
vmm_p2both:
    push ax
    mov  al, 0xF4               ; ENABLE reporting -> the host flips its own
    call vmm_p2wake             ; mouse handler on
    mov  al, 0xF5               ; ...and STOP the stream: no packet, no IRQ12
    call vmm_p2wake
    pop ax
    ret

; =============================================================================
; The verbs
; =============================================================================

; -----------------------------------------------------------------------------
; vmm_attach - DRVV_ATTACH: probe the backdoor and arm ABSOLUTE mode
;
; in:       AH = VMM_ABI_VER as the kernel understands it
; out:      CF = 0 - the backdoor answered and absolute mode is armed;
;           CF = 1 - no backdoor, AND NOTHING WAS HOOKED
; clobbers: AX, BX, CX, DX, SI, DI (flags)
;
; There is nothing to detect but the backdoor itself. The GETVERSION probe
; reads an undriven port on bare metal - 0x5658 was chosen because no hardware
; decodes it - and EBX comes back unchanged. No v86 detection, no hypervisor
; CPUID bit, and no [cpu_tier] test either: THE KERNEL DID THAT BEFORE IT READ
; THIS FILE (SPEC.md 9.11.2). A machine that cannot run 386 code never gets as
; far as loading the image, which is the whole reason the image exists.
; -----------------------------------------------------------------------------
vmm_attach:
    cmp ah, VMM_ABI_VER
    jne .no                     ; a kernel of another vintage: refuse loudly at
                                ; load rather than quietly at the first poll

    xor ax, ax                  ; --- GETVERSION ---
    xor dx, dx
    mov cx, VMM_GETVER
    call vmm_cmd
    mov ax, [vmm_ebx]
    cmp ax, VMM_MAGIC & 0xFFFF
    jne .no
    mov ax, [vmm_ebx+2]
    cmp ax, VMM_MAGIC >> 16
    jne .no
    mov ax, [vmm_eax+2]         ; EAX != 0xFFFFFFFF: the high half alone tells
    cmp ax, 0xFFFF              ; an undriven port
    je  .no

    call vmm_enable             ; READ_ID / ABSOLUTE / drain
    call vmm_p2both             ; ...and make v86 start delivering events
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; vmm_detach - DRVV_DETACH: put the 8042 back. Cannot fail (SPEC.md 51.2)
; in:  none.  out: CF = 0
; clobbers: AX, BX, CX (flags)
;
; The backdoor itself needs nothing: no vector, no line, no port kept. What
; DOES need undoing is vmm_p2both's aux enable, and it needs undoing for a
; reason beyond tidiness - a stream left enabled across an int 19h warm boot
; is packets arriving at a kernel that has not hooked int 74h, which the next
; BIOS reads as scancodes.
; -----------------------------------------------------------------------------
vmm_detach:
    push ax
    mov  al, 0xF5
    call vmm_p2wake
    pop  ax
    clc
    ret

; -----------------------------------------------------------------------------
; vmm_read - VMMV_READ: one report out of the queue, RAW
;
; in:       none
; out:      CF = 0 and AX = x, BX = y, CL = the buttons in mouse_btn's own two
;           bits, CH = 1 if this is a RELATIVE delta rather than a position;
;           CF = 1 = nothing queued, and the kernel stops asking this pass
; clobbers: AX, BX, CX, DX, SI, DI (flags)
;
; **RAW, and the scaling is deliberately not here.** The report is 0..0xFFFF
; on both axes; turning that into a pixel needs [vid_w]/[vid_h], the live
; screen rather than the reference one (CLAUDE.md's three-adapters rule), and
; on a two-display machine it needs SPEC.md 39.15.4's geometry as well. All of
; that is kernel state that changes under this image's feet - vid_switch can
; run while we are loaded - so the kernel scales and this returns what the
; host said.
;
; The queue is drained one report a call because a BUTTON TRANSITION is not
; idempotent the way a position is: collapsing four packets into "the last
; position and the current buttons" loses a click that opened and closed
; inside one poll. The kernel bounds how many times it asks.
; -----------------------------------------------------------------------------
vmm_read:
    xor ax, ax                  ; --- how much is queued? ---
    xor dx, dx
    mov cx, VMM_STATUS
    call vmm_cmd
    mov ax, [vmm_eax+2]         ; high half of the status word
    cmp ax, 0xFFFF              ; 0xFFFF???? -> the backdoor disabled itself
    je  .redo                   ; (a DATA underflow, or a v86 queue overflow)
    mov ax, [vmm_eax]           ; low half = WORDS queued
    cmp ax, VMM_QMIN            ; a whole 4-word packet?
    jb  .none
    test al, 3                  ; ...and 4-aligned. vmm_enable drains to empty
    jnz .resync                 ; so this never fails - but a stray word would
                                ; misframe EVERY packet behind it, so a
                                ; non-multiple of 4 means throw the lot away

    mov ax, 4                   ; --- the packet ---
    xor dx, dx
    mov cx, VMM_DATA
    call vmm_cmd
    ; [vmm_eax] status, [vmm_ebx] x, [vmm_ecx] y, [vmm_edx] z (wheel: dropped)

    xor cx, cx                  ; buttons into mouse_btn's own two bits
    test byte [vmm_eax], VMM_S_LEFT
    jz  .nol
    or  cl, 0x01
.nol:
    test byte [vmm_eax], VMM_S_RIGHT
    jz  .nor
    or  cl, 0x02
.nor:
    mov ax, [vmm_ebx]           ; x, raw
    mov bx, [vmm_ecx]           ; y, raw
    test byte [vmm_eax+2], 0x01 ; RELATIVE_PACKET (0x00010000) in the high
    jz  .out                    ; word: v86 sends these while the host pointer
    mov ch, 1                   ; is LOCKED (the user hit "capture pointer").
                                ; Not needed here, but it works
.out:
    clc
    ret

.redo:
    call vmm_enable             ; READ_ID clears the disable, then drain. The
                                ; kernel keeps polling; the next pass's STATUS
                                ; confirms it took. Under v86 the disable is a
                                ; transient queue overflow; under QEMU it is a
                                ; DATA underflow vmm_flush now prevents
    jmp short .none
.resync:
    call vmm_flush              ; misframed: drop everything and start clean on
                                ; the next 4-word packet
.none:
    stc
    ret

; =============================================================================
; State. IN THE IMAGE and written `dd 0`, because A DRIVER HAS NO BSS
; (drivers/os88drv.inc): its zeroed data is bytes on the floppy, which buys a
; load path with exactly one claim in it, made at the size the directory entry
; already reported.
;
; Sixteen bytes, and they are the only writable storage this image has. Both
; halves of the protocol reach them: the 386 island writes all four as dwords
; and the 8086 half reads them back as word pairs, which is what makes the
; island 59 bytes rather than the whole driver.
; =============================================================================
vmm_eax     dd 0                ; vmm_bd's four result registers. vmm_ebx is
vmm_ebx     dd 0                ; also the EBX *input* - the command's argument
vmm_ecx     dd 0                ; - which is why vmm_cmd stages it there and
vmm_edx     dd 0                ; every caller reads it back afterwards

    OS88_DRV_END
