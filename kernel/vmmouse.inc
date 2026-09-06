; =============================================================================
; vmmouse.inc - the kernel's half of the VMware absolute pointer (SPEC.md 9.11)
;
; A row, a gate byte, a dispatch, a boot attach and a pump. EVERYTHING ELSE -
; the protocol, the 386 island, the 8042 bracket - is VMMOUSE.DRV, read off
; the system disk when SYSTEM.CFG asks for it and on no other machine.
;
; **WHY THE SPLIT.** The backdoor protocol is 32-bit: `in eax, dx` with a dword
; magic. An 8088 has no EAX and 0x66 is not a prefix there, so the protocol
; cannot be spoken by the machine this project is calibrated against and there
; is nothing on that machine to speak it to. Carried resident it cost the XT
; **548 bytes of kernel image**, which tools/kernsize.py priced as a crossed
; 512-byte footprint rung - 512 bytes of every machine's RAM, gone. That is
; SPEC.md 41.12's argument for XMEM.DRV word for word, and it gets the same
; answer.
;
; It is also the only arrangement that closes the hibernate hole. A [cpu_tier]
; gate in front of resident 386 code is sound while the machine stays the same
; machine; SPEC.md 87 writes RAM wholesale and validates magic, build, stamp,
; mem_top and vid_kind on resume - not the CPU - so an image taken on a 386
; and resumed on an 8088 restores both the code and the "it is on" byte and
; the 8088 executes `push eax`. CLAUDE.md names xt-mfm as the machine to
; hibernate on. Hibernate DETACHES every driver that is not DRVC_DISK or
; DRVC_FILE before it writes (SPEC.md 87.4 step 1), so a file cannot make that
; trip.
;
; **DRVC_OVL WITH A drv_tab ROW**, which is a combination nothing else in the
; tree uses, and each half is doing work:
;   - DRVC_OVL, so it takes no publication class (SPEC.md 51.2.1) and
;     DRVC_MAX stays 5. A class is how a PACKAGE finds a driver; the kernel is
;     this one's only caller and holds the row, so there is nothing to find.
;   - a row, so SYSTEM.CFG carries a bit for it and the Control Panel's
;     Drivers page can turn it off. SPEC.md 41.12 declined a row for XMEM on
;     the grounds that "a machine either has memory up there or it does not"
;     and a tick box "would only ever be a way to break a working machine".
;     Half of that applies here and half does not: whether the backdoor EXISTS
;     is likewise not a matter of opinion, but whether the user wants the
;     pointer ungrabbed is - and the probe is 386 code, so unlike xm_sniff
;     there is no resident test that could answer the first question anyway.
;     The bit is therefore both the request and the record of it.
;
; KERN_BIG only, and by the same reasoning as XMEM's: kern_small is the
; 128-256KB product, every machine it runs on is an 8086, and drv_load_at and
; the row would be a hook left outside its %ifdef - docs/KERN-SPLIT-PLAN.md 2's
; first leak.
; =============================================================================

%ifdef KERN_BIG

%include "vmmabi.inc"           ; the kernel <-> image contract, SHARED as
                                ; source with drivers/vmmouse/vmmouse.asm and
                                ; never copied (CLAUDE.md)

MOU_VMROW   equ 6               ; [mou_port] once the backdoor has won. Serial
                                ; rows are byte offsets into a word table, so
                                ; they are 0 and 2; mou_p2_byte's is
                                ; MOU_P2ROW = 4. SIX IS REACHABLE BY NO DEVICE,
                                ; which is the point: every `cmp bl,[mou_port]`
                                ; in the serial decoder then fails, and
                                ; mou_lockon retires both UARTs because
                                ; neither matches either

; -----------------------------------------------------------------------------
; The gate. THE ROW IS drv_tab's row 5, declared in driver.inc beside the other
; five so that DRV_MAX, the settings bitmap and drv_memk cannot drift from it;
; `vmm_row` is its label there.
;
; In .text and not .bss, because -f bin zeroes nothing and this is read at
; boot - and, more sharply than for XMEM, because [vmm_on] is what stands
; between an 8088 and a far call into an image that is not loaded. A .bss byte
; survives an int 19h warm boot with the previous kernel's value in it; a .text
; one is re-read from the disk.
; -----------------------------------------------------------------------------
vmm_on      db 0                ; THE GATE. 1 = the image is attached and the
                                ; backdoor won the contest. Every poll site
                                ; compares this byte and nothing else, so a
                                ; machine without the file pays one compare
vmm_busy    db 0                ; vmm_poll's re-entrancy guard: task_yield is a
                                ; caller, so two task slices can race the
                                ; image's scratch
vmm_drn     db 0                ; ...and its drain counter. A byte and not a
                                ; register, because the apply path spends SI.
                                ; Live only inside one poll

; -----------------------------------------------------------------------------
; vmm_dsp - dispatch one of the image's verbs (xm_dsp's shape, SPEC.md 41.12)
; in:  AL = the verb, every other register = that verb's own arguments
; out: whatever it returns
; clobbers: BP; DS and ES restored
;
; **BX IS NOT BANKED, AND THAT IS THE CONTRACT RATHER THAN AN OMISSION**:
; VMMV_READ ANSWERS IN IT. The first draft staged the row pointer in BX and
; pushed it round the far call, which restored the row address over the y
; coordinate the image had just returned - and the symptom was a pointer whose
; x tracked perfectly and whose y never moved off whatever it happened to be,
; which reads as a scaling bug and is not one. xm_dsp's header records the same
; class of defect twice in the sound ABI and answers it the same way: name the
; row directly. It is a link-time constant, so that costs nothing and spends no
; register at all.
; -----------------------------------------------------------------------------
vmm_dsp:
    push ds
    push es
    push cs
    pop  es                     ; ES = KERNEL_SEG: what the image reads the
                                ; things it was handed through
    mov  bp, [es:vmm_row+DRVR_ENT]
    mov  ds, [es:vmm_row+DRVR_SEG]
    call far [es:vmm_row+DRVR_DISP]
    pop  es
    pop  ds
    ret

; -----------------------------------------------------------------------------
; vmm_poll - drain the image's queue and apply each report (SPEC.md 9.11.3)
;
; in:  [vmm_on] set - the VMM_POLL macro is the gate and every caller uses it
; out: nothing (all registers preserved)
;
; Called from ui_task's pass and from task_yield, so from EVERY task. The body
; only ever runs under a hypervisor, where the CPU is not the target's - but
; the STACK it runs on is every task's, and that is a space question rather
; than a time one.
;
; **THE PRIVATE-STACK SWAP IS THE WHOLE OF THAT ANSWER.** SPEC.md 9.10 moved
; the mou_apply -> cur_move chain onto mou_pstack precisely so it stops being
; "a cost every slice must be sized for simultaneously", and kernel.asm's
; STK0_SIZE was halved to 512 citing that removal in terms - "what lands on a
; task stack is that task's own call chain and nothing arriving from outside
; it". Polling the backdoor from task_yield puts that same chain back on the
; caller's stack by a different door: measured at task_yield's deepest path
; going 28 -> 66 bytes, which computes the 128-byte idle slice to within one
; word of sch_stkdie. So the apply runs on mou_pstack, and #147's invariant
; holds unchanged.
;
; It is safe to borrow because it is PROVABLY IDLE while [vmm_on] is set:
; vmm_boot_x settles the contest on MOU_VMROW and calls mou_lockon, which
; clears IER on both UARTs, masks both lines at the master 8259 and calls
; mou_p2_off. Neither mouse ISR can fire, so neither can be inside
; MOUPRIV_ENTER, and [mou_psave] has no other writer.
;
; The swap is INSIDE the cli, and that is load-bearing rather than tidy: SP is
; what sch_switch saves into the task's record, so a tick taken while SP points
; into mou_pstack would bank the mouse's stack as that task's and never get it
; back.
; -----------------------------------------------------------------------------
vmm_poll:
    mov al, 1                   ; RE-ENTRANCY GUARD (xchg is atomic on 8086):
    xchg al, [vmm_busy]         ; task_yield is a caller, so an IF=1 window in
    or  al, al                  ; the drain can be preempted into another task
    jz  .go                     ; that yields and also polls - and the image's
    ret                         ; scratch is one buffer. The other slice will
                                ; drain it; skip. AL and the flags are spent
                                ; on this path, which is why the header says
                                ; "all registers preserved" only below
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es                     ; mou_apply's tail is written for ISR context,
                                ; where every register was saved on entry -
                                ; cur_move spends SI/DI/BP and loads ES for
                                ; VRAM. From a task this routine owes all of
                                ; that back itself
    mov byte [vmm_drn], VMM_DRAIN   ; a MEMORY counter, not a register: the
                                    ; apply path below spends SI
.pass:
    mov al, VMMV_READ
    call vmm_dsp                ; out: CF=1 nothing queued; else AX/BX raw,
    jc  .out                    ; CL buttons, CH 1 = a relative delta
    cmp ch, 0
    jne .rel

    ; --- absolute: scale 0..0xFFFF onto the LIVE screen ----------------------
    ; [vid_w]/[vid_h] and never SCREEN_W/SCREEN_H (CLAUDE.md): the reference
    ; values are VGA's and this has to land on the pixel on all four
    ; geometries. DX = (x * vid_w) >> 16, so 0 -> 0 and 0xFFFF -> vid_w - 1 for
    ; every width - the top of the range can never address one past the edge,
    ; which is what leaves mou_clamp nothing to catch on a single display.
    push cx
    mov  si, ax                 ; x, banked - mul spends AX and DX
    mov  ax, [vid_h]
    mul  bx
    mov  bx, dx                 ; BX = y, scaled
    mov  ax, [vid_w]
    mul  si
    mov  ax, dx                 ; AX = x, scaled
    pop  cx
    pushf
    cli                         ; ...and the swap inside it (see the header)
    MOUPRIV_ENTER
    call mou_apply_abs
    MOUPRIV_LEAVE
    popf
    jmp short .more
.rel:
    neg bx                      ; positive is UP on this wire, like the PS/2
                                ; one, so the screen's dy is its negation
                                ; (SPEC.md 9.9.3)
    pushf
    cli
    MOUPRIV_ENTER
    call mou_apply
    MOUPRIV_LEAVE
    popf
.more:
    dec byte [vmm_drn]
    jnz .pass
.out:
    mov byte [vmm_busy], 0
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endif                          ; KERN_BIG

; VMM_POLL - drain the backdoor's queue if it won the contest (SPEC.md 9.11.3).
; Expands to nothing on kern_small. Used by ui_task's pass and by menu_drop,
; both of which already hold DS = KERNEL_SEG; task_yield's site is written out
; longhand in sched.inc because it does not.
;
; DS-RELATIVE HERE and cs-relative at task_yield's site, which is not an
; inconsistency: a segment override costs two bytes, and these two callers
; already hold the kernel's DS, so paying it here would buy nothing at either.
; task_yield's caller may be a package, so that one site pays it and gets back
; far more - see sched.inc.
%macro VMM_POLL 0
%ifdef KERN_BIG
    cmp byte [vmm_on], 0
    jz %%no_vmm
    call vmm_poll
%%no_vmm:
%endif
%endmacro

%ifdef KERN_BIG
section .cold
; -----------------------------------------------------------------------------
; vmm_boot_x - attach the image and settle the contest (SPEC.md 9.11.2)
;
; in:  nothing.  out: nothing (all registers preserved)
;
; kmain calls this straight after drv_boot_x, which is what loaded the row if
; SYSTEM.CFG asked for it. It runs AFTER mouse_init, so the serial identify
; window and the PS/2 probe have already happened and the contest may already
; be settled on a real mouse - which is why this settles it again rather than
; skipping ahead of it. That order is not free (a browser pays MOU_IDWIN for a
; mouse it will not use) and it is the right one anyway: a machine whose
; backdoor turns out not to answer still has whatever it really has.
;
; **THE CPU GATE IS HERE, AND IT IS THE LAST ONE.** Below this point there is
; no [cpu_tier] test anywhere, because below this point there is no kernel code
; that could need one - the 386 instructions are all in a file that a machine
; failing this compare never reads a sector of. cpu_detect runs from the boot
; overlay long before drv_boot_x, so the tier is published.
;
; IN `.cold`, WHICH IS NOT WHERE xm_boot_x IS, and the difference is worth
; stating. `.ovl` is given back to the heap at spl_finish and would be the
; better home - boot-only code that costs nothing afterwards - but the blob is
; full: adding this there trips kernel.asm's `OVL_AT + OVL_SIZE > BOOT2_PAD`
; assertion, and BOOT2_SECS is a decision about how many sectors stage 1 reads
; before it jumps, not a build fix. So this takes `.cold` instead, which is
; resident for the life of the machine and costs the XT about a hundred bytes
; against the 548 the resident version cost - inside the cold rung's existing
; headroom, so KERN_BUDGET does not move.
;
; It is also where it wants to be on its own merits: drv_call and drv_release
; are driver.inc's and driver.inc IS `.cold` (SPEC.md 2.6), so from here they
; are near calls. Only mou_lockon and the mouse's state bytes are reached
; across a boundary, and CS is COLD_SEG here rather than KERNEL_SEG - which is
; why the writes below go through ES.
; -----------------------------------------------------------------------------
vmm_boot_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop  es                     ; ES = KERNEL_SEG for the whole body: this is
                                ; `.cold`, so CS is COLD_SEG and every kernel
                                ; byte below - the row, [cpu_tier], the mouse's
                                ; state - is DS's. DS is already the kernel's
                                ; (SPEC.md 1's near model); ES is what the
                                ; `[es:...]` overrides below name, so that the
                                ; reader can see at a glance which side of the
                                ; boundary each store is on
    cmp byte [es:cpu_tier], CPU_386
    jne .off                    ; an 8086 or a 286: the image is 386 code and
                                ; must not be READ, let alone entered. If a
                                ; SYSTEM.CFG from a faster machine asked for
                                ; it, say so in the row rather than obey it
    mov bx, vmm_row
    cmp word [es:bx+DRVR_SEG], 0    ; drv_boot_x loads a wanted row; a DRVC_OVL
    je  .out                        ; one stops after drv_check with the
                                    ; segment live. Zero = not wanted, or the
                                    ; load failed and the row already carries
                                    ; the DRVE_*
    mov bp, [es:bx+DRVR_ENT]
    mov al, DRVV_ATTACH
    mov ah, VMM_ABI_VER         ; ...which the image checks against its own
    call drv_call
    jc  .refuse

    ; --- won: settle the contest, as mou_claim / mou_p2_byte do --------------
    mov byte [es:mou_port],  MOU_VMROW
    mov byte [es:mou_line],  MOU_P2LINE
    mov byte [es:mou_seen],  1
    mov byte [es:mou_ptr],   1     ; the machine HAS a pointer (SPEC.md 9.6.6)
    mov byte [es:mou_idany], 1     ; ...so mou_hotplug stands down too
    call KERNEL_SEG:ovw_mou_lockon  ; retire both UARTs: IER 0, both lines
                                    ; masked, and mou_p2_off for the aux side.
                                    ; FAR - this body is `.cold` and CS is
                                    ; COLD_SEG here, not KERNEL_SEG
    mov byte [es:vmm_on], 1        ; LAST, and deliberately so: it is the gate
    jmp short .out              ; every poll site reads

.refuse:
    mov bx, vmm_row             ; RE-ESTABLISHED: drv_call's callee may spend
    mov byte [es:bx+DRVR_ERR], DRVE_HW  ; BX, and xm_boot_x records what reading a
    call drv_release   ; stale one cost. No backdoor on this
    jmp short .out                   ; machine: hand the image straight back
.off:
    mov bx, vmm_row
    mov byte [es:bx+DRVR_ERR], DRVE_HW  ; 'No hardware found', and it is the
                                ; honest answer rather than a spared string: a
                                ; machine that cannot execute the image has no
                                ; hardware this driver can drive
    cmp word [es:bx+DRVR_SEG], 0    ; ...and if drv_boot_x read it anyway - it
    je  .out                        ; has no tier test of its own - give it back
    call drv_release
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf                        ; FAR: kmain reaches this as
                                ; `call COLD_SEG:vmm_boot_x`, drv_notice_x's
                                ; shape

section .text
%endif                          ; KERN_BIG
