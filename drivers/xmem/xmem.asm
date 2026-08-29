; =============================================================================
; os8088 - XMEM.DRV: the store above 1MB (SPEC.md 41)
;
; Sizing, a small first-fit allocator over it, and one copy routine with ONE
; ABI over TWO transports - together with its own prerequisites, the A20 gate
; (SPEC.md 41.2) and the HMA claim (41.3), which are here because reaching the
; store is the only thing they are for.
;
; THIS IS AN OVERLAY, NOT A DRIVER (SPEC.md 41.12, 52.11), and the distinction
; is the whole reason the file exists. It has a driver's header, a driver's
; org 0, a driver's three-byte dispatcher and a driver's one-claim load - and
; class DRVC_OVL, which the kernel deliberately does not know: no row in
; drv_tab, no publication slot, no Drivers-page tick, no SYSTEM.CFG bit and no
; Control Panel page. HDDTOOL.DRV is the precedent; the only difference is
; that ITS owner is another driver and OURS IS THE KERNEL.
;
; It is not a driver because none of what a driver row buys is wanted here.
; There is nothing for a user to decide: a machine either has memory above 1MB
; or it does not, the kernel's own boot sniff answers that EXACTLY (it is the
; same int 15h AH=88h this file sizes with), and a tick box would only ever be
; a way to break a working machine. So the kernel loads this when the sniff
; says yes and never otherwise, and nothing anywhere offers to turn it off.
;
; WHY IT IS OUT OF THE KERNEL AT ALL: a machine with no XMS - which is every
; 8088, and the machine this project is calibrated against - was reserving
; about 1.2KB of image forever for a feature it can never reach. That is the
; whole of it. kern_small had already dropped the feature outright (41.11);
; this is the same removal for kern_big, without giving up the capability.
;
; THE HARD CEILING, because it is the rule most likely to be violated later:
; real mode forms an address as seg<<4 + off, so the highest byte any CS:IP
; can name is 0x10FFEF. Everything handed out here is a DATA STORE ONLY - no
; code, no jump table, no callback, no relocated package image, on any tier.
; Unreal mode does not move that line by one byte: it widens a DATA segment's
; limit and touches neither the width of CS nor of IP.
;
; Unreal mode uses FS AND GS, and that choice is the whole safety argument
; (SPEC.md 41.4). A segment register's hidden descriptor cache keeps its limit
; until the register is RELOADED; one plain real-mode `mov fs, ax` anywhere
; would reset it to 0xFFFF and the 4GB window would be gone - silently, with
; no fault, as a copy that wraps inside 64KB. A case-insensitive search for FS
; or GS across every kernel module, every shipped package, every driver and
; boot/boot.asm finds nothing but this file. THAT RULE NOW HAS TO HOLD ACROSS
; A WINDOW IN WHICH THIS IMAGE IS NOT LOADED AND THE LIMITS ARE STILL WIDE
; (41.12): a detach frees the image and cannot un-widen FS and GS, which is
; harmless in itself - a wider limit faults nothing - and is why the rule is
; restated here rather than inherited.
;
; BIOS re-arm, and it is not optional either. The kernel enters the BIOS
; constantly after boot - int 16h AH=01h on EVERY pass of the UI loop, plus
; int 13h/10h/1Ah/12h. Nothing in os8088 touches FS or GS, but BIOS code is
; not os8088 code and a 386-aware BIOS may load them for its own purposes and
; hand them back with real-mode limits. So the tier-2 copy re-arms from the
; resident GDT - and, because the BIOS is re-entered by INTERRUPTS and not
; only by kernel calls, it re-arms inside the same cli window as the accesses
; that depend on it. That is why the copy is chunked (xm_ucopy).
;
; Every 386 instruction in this file sits inside a scoped `cpu 386` island
; closed by `cpu 8086`, AND behind a run-time [xm_tier] test. The island is
; assembly-time permission only (SPEC.md 41.9 rule 2): an island reached on a
; 286 is an illegal-opcode trap on a machine with no handler for it.
;
; NO BULK CLAIM, EVER, AND THAT IS AN INVARIANT RATHER THAN AN OBSERVATION
; (SPEC.md 41.12.3). The image is claimed MEM_K_DRV and mem_sum_kb counts it
; under System BY THAT TAG, not by drv_tab membership - which is what lets an
; overlay with no row account correctly. A heap claim WE made would carry our
; own segment as its owner word, and drv_owns_seg answers about drv_tab rows,
; so it would belong to nobody and appear in no line of the Task Manager's
; list. The block table below is 64 bytes inside this image for exactly that
; reason. The day this wants a buffer of its own is the day it needs a row.
;
; Context (SPEC.md 41.8) is unchanged and is now the KERNEL's to enforce: the
; allocator and the copy are UI-task context, the gfx lock MAY be held, and
; [sch_lock] is raised by the kernel's own slot body around the dispatch -
; exactly as dsk_xfer raises it before dispatching DSV_BLK, and for the same
; reason, which is that a loaded image cannot reach it: no API slot publishes
; it, so a driver that needs the scheduler held cannot ask for it itself.
; =============================================================================

%include "os88drv.inc"

XM_ABI_VER  equ 1               ; the private kernel<->overlay ABI (SPEC.md
                                ; 52.11's +10 word). The kernel passes what it
                                ; expects in AH at DRVV_ATTACH and we refuse a
                                ; mismatch, so a KERNEL.SYS and an XMEM.DRV of
                                ; different vintages - a half-copied floppy -
                                ; fail loudly at load instead of quietly at
                                ; the first copy

OS88_OVERLAY 'XMS', XM_ABI_VER, xm_entry

; --- what the kernel dispatches (the XMV_* table, mirrored in kernel/xmem.inc)
XMV_CAPS    equ 0               ; out AX = free KB, DX:CX = pool base,
                                ;     BL = free block-table entries
XMV_ALLOC   equ 2               ; in DX:AX = bytes, BL = owner; out CF,
                                ;     DX:AX = base / AX = 0/1/2
XMV_FREE    equ 4               ; in DX:AX = base, BL = owner; out CF
XMV_COPY    equ 6               ; in ES:SI = an XMC_ block in KERNEL_SEG
XMV_RELINST equ 8               ; in AL = an instance slot being torn down
XMV_SIZE    equ 10

; --- the kernel's copy parameter block, read through ES (SPEC.md 41.12.2) ----
; ES is KERNEL_SEG inside every call the kernel makes here, and the two ends
; are 32-bit LINEAR addresses because the conventional one cannot arrive in a
; segment register: drv_call loads ES itself, so the caller's ES is gone by
; the time we run. The kernel folds ES:SI into a linear address before it
; dispatches, which is arithmetic xm_copy used to do internally anyway.
XMC_EXT     equ 0               ; dd: the extended end
XMC_CONV    equ 4               ; dd: the conventional end
XMC_LEN     equ 8               ; dw: bytes
XMC_DIR     equ 10              ; db: 0 = conventional -> extended, 1 = back
XMC_INST    equ 11              ; db: the requesting instance (the kernel
                                ; stamps it - snd_req_inst is kernel state)
XMC_SIZE    equ 12

XM_SEL_FLAT     equ 0x08        ; GDT entry 1: base 0, limit 4GB, data r/w
XM_MAX_COPY     equ 32768       ; bytes per copy - a SCHEDULER rule, not a
                                ; BIOS one: AH=87h runs with interrupts off
                                ; inside the BIOS, and a 64KB move at AT
                                ; speeds drops PIT ticks and overruns the
                                ; mouse UART's receive buffer (SPEC.md 9).
                                ; A caller moving more calls twice.
XM_UCHUNK       equ 1024        ; bytes per tier-2 chunk, each one re-armed
                                ; and moved inside one cli window (xm_ucopy).
                                ; ~0.3ms of IF=0 on the slowest tier-2
                                ; machine - well inside one PIT tick and one
                                ; 1200-baud mouse byte.
XM_MAX_BLKS     equ 8           ; the fixed block table, entries: a bulk store
                                ; for a handful of large claims, not a malloc
XM_HMA_KB       equ 64          ; what a successful xm_hma_claim takes off

HMA_SEG         equ 0xFFFF      ; HMA_SEG:0010 is linear 0x100000 and
HMA_MIN_OFF     equ 0x0010      ; HMA_SEG:FFFF is 0x10FFEF

; One block-table entry. Offsets are kept in KB FROM THE POOL BASE, not as
; 32-bit linear addresses: the pool is at most 65,535KB, so a word holds any
; offset and every comparison in the allocator is 16-bit. The public ABI is
; still 32-bit linear - the conversion happens once, at each boundary.
XB_OFF          equ 0           ; word: KB from [xm_base]
XB_KB           equ 2           ; word: size in KB. ZERO MEANS THE ENTRY IS
                                ; FREE - there is no separate in-use flag.
XB_OWN          equ 4           ; byte: the stamping instance (0xFF = kernel)
XM_BLKSZ        equ 8           ; padded to 8 so indexing is a shift

XM_OWN_KERN     equ 0xFF        ; a claim made outside any instance. 0xFF and
                                ; not 0, because 0 is a real instance slot,
                                ; and inst_caller - the kernel's "who is
                                ; asking" answer, which stamps our BL -
                                ; already spells the kernel 0xFF.

; -----------------------------------------------------------------------------
; xm_entry - the kernel far-calls this through the header's dispatcher
;
; in:       AL = verb, AH = the ABI version the kernel expects (ATTACH only),
;           DS = CS = our segment, ES = KERNEL_SEG
; out:      per verb
;
; It NAMES every verb it handles and REFUSES anything else (SPEC.md 51.2.2).
; That is not defensive tidiness: `snd_entry` let unknown verbs fall into its
; attach body, so DRVV_READY - added to the kernel later - ran a second
; complete attach and orphaned a 12KB claim on every machine with a Sound
; Blaster. A verb added later must land on a refusal, never on work.
;
; The kernel sends us neither READY nor TIER - an overlay has no publication
; slot to be told about and no tiers - but that is the kernel's promise, not
; ours to rely on.
; -----------------------------------------------------------------------------
xm_entry:
    cmp al, DRVV_ATTACH
    je xm_attach
    cmp al, DRVV_DETACH
    je xm_detach
    stc                         ; every other verb, named or not
    ret

; -----------------------------------------------------------------------------
; xm_attach - size the store, claim the HMA, arm unreal mode (SPEC.md 41.5)
;
; in:       AH = XM_ABI_VER as the kernel understands it
; out:      CF = 0, SI = the XMV_* table, AX = the pool's total KB,
;           DL = the CPU_F_* bits that actually verified;
;           CF = 1 = no reachable store, AND NOTHING WAS HOOKED
; clobbers: AX, BX, CX, DX, SI, DI (flags)
;
; The all-or-nothing rule (SPEC.md 51.2) applies unchanged, and what counts as
; "hooked" is worth being exact about, because this attaches no interrupt and
; owns no port: it opens the A20 GATE, which is machine state rather than a
; hooked resource. Nothing in os8088 depends on 1MB wraparound, so a gate left
; open by a refusal is safe and closing it again would buy nothing - see the
; file header on unreal mode, which is the same argument.
;
; It HAND-ZEROES the block table, because a second attach after a detach must
; not inherit the first one's blocks.
;
; The A20 test is not belt-and-braces. SPEC.md 41.2 makes the verified bit -
; not "we wrote to the gate" - the thing every consumer keys off, and a 386
; whose gate never answered has no reachable store no matter what AH=88h says.
; -----------------------------------------------------------------------------
xm_attach:
    cmp ah, XM_ABI_VER
    jne .no                     ; a kernel of another vintage: refuse loudly
                                ; at load rather than quietly at the first copy

    mov di, xm_tab              ; the table, zeroed before anything can fill it
    mov cx, (XM_MAX_BLKS * XM_BLKSZ) / 2
    xor ax, ax
    push es                     ; ES is KERNEL_SEG on entry and stos writes
    push ds                     ; through it
    pop es
    cld
    rep stosw
    pop es
    mov [xm_kb], ax             ; the safe answer, in place before any probe
    mov [xm_base_lo], ax
    mov [xm_base_hi], ax
    mov [xm_feat], al

    call OSAPI_CPU_INFO         ; AL = the tier, AH = the kernel's own bits
    mov [xm_tier], al           ; ...and we keep our own copy: this image is
                                ; the only writer of the A20/HMA/UNREAL bits
                                ; now, and it reports them back at the end
    cmp al, CPU_8086
    je .no                      ; tier 0: no A20, no HMA, no store. The
                                ; kernel's sniff should have spared us the
                                ; read, but a refusal here is what makes that
                                ; an optimisation rather than a dependency

    call xm_a20_enable          ; ...and VERIFY it (SPEC.md 41.2)
    jc .no                      ; the gate never answered: nothing up there is
                                ; reachable, whatever the CPU is

    mov ah, 0x88
    int 0x15                    ; AX = KB above 1MB
    jc .no                      ; no such service
    or ax, ax
    jz .no                      ; a 1MB AT: zero bytes above 0x0FFFFF
    mov bx, ax                  ; BX = the KB figure
    call xm_hma_claim           ; AX is still that figure - the claim needs it
    mov dx, 0x0010              ; pool base high word: linear 0x00100000
    jc .nohma
    sub bx, XM_HMA_KB           ; the HMA is the first 64KB of exactly this
    mov dx, 0x0011              ; RAM, so the pool starts above it (SPEC.md
.nohma:                         ; 2.4) - one of the two owns those 64KB,
    or bx, bx                   ; never both
    jz .no                      ; the HMA took all there was
    cmp byte [xm_tier], CPU_386
    je .base                    ; tier 2 addresses the lot with 32 bits
    mov ax, dx                  ; tier 1's transport is int 15h AH=87h, whose
    mov cl, 6                   ; descriptors carry 24 address bits: clamp the
    shl ax, cl                  ; pool to 16MB. (base_hi * 64 = base in KB.)
    mov cx, 16384
    sub cx, ax
    cmp bx, cx
    jbe .base
    mov bx, cx
.base:
    mov [xm_base_hi], dx        ; xm_base_lo stays 0 - both bases are on a
    cmp byte [xm_tier], CPU_386 ; 64KB boundary
    jne .publish
    call xm_arm                 ; unreal mode, once, here (SPEC.md 41.4)
.publish:
    mov [xm_kb], bx             ; LAST, and deliberately so: it is the gate
                                ; every entry point below tests
    mov ax, bx                  ; the kernel banks the total for SK_XMS
    mov dl, [xm_feat]           ; ...and the bits, so cpu_info's AH stays true
    mov si, xm_svc
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; xm_detach - give every block back. Cannot fail (SPEC.md 51.2)
;
; Nothing in the kernel can reach this today - there is no Drivers row and no
; tick - and it is written correctly anyway, because "unreachable" is a fact
; about this month's kernel and the force-free is four lines. Every
; outstanding block is about to lose its allocator.
;
; It does NOT close the A20 gate and does NOT un-widen FS and GS; see the file
; header. Both are machine state that nothing depends on the absence of, and
; the second cannot be undone from here in any case.
; -----------------------------------------------------------------------------
xm_detach:
    push ax
    mov al, XM_OWN_KERN
    call xm_relall
    mov word [xm_kb], 0
    pop ax
    ret

; --- the service table the kernel copies at attach ---------------------------
xm_svc:
    dw xm_caps                  ; XMV_CAPS
    dw xm_alloc                 ; XMV_ALLOC
    dw xm_free                  ; XMV_FREE
    dw xm_copy                  ; XMV_COPY
    dw xm_release_inst          ; XMV_RELINST

; =============================================================================
; The A20 gate and the HMA claim - the store's own PREREQUISITES (SPEC.md 41.2
; / 41.3), and they live here because that is the only thing they are for.
;
; They keep the CPU_F_* vocabulary rather than growing one of their own,
; because [xm_feat] is handed back at attach and the kernel publishes it
; through cpu_info's AH (slot 0x0188) exactly as it always did. On a machine
; where this image never loads, that byte reads 0 - no gate verified, no HMA,
; no unreal mode - which is both true and precisely what tier 0 answers.
; =============================================================================

; The A20 wraparound probe's scratch. Linear 0x00500..0x005FF is the 256 free
; bytes of SPEC.md 2 (the boot stack that grew down from 0x7C00 is dead by
; kmain), which is why the probe goes there and not at 0000:0000 - the kernel
; hooks int 08h and IRQ4 and the IVT is live. It is still free at the moment
; this runs, which is later than it used to be: the kernel starts at linear
; 0x600 (KERNEL_SEG 0x0060) and SPEC.md 18.92's diskette parameter table is at
; 0000:0580, so the two bytes below are the only ones in reach and both are
; saved and restored. The alias is arithmetic: HMA_SEG:0510 = 0xFFFF0 + 0x510
; = linear 0x100500, the same byte with A20 shut and 1MB higher with it open.
XM_A20_OFF     equ 0x0500
XM_A20_ALIAS   equ HMA_MIN_OFF + XM_A20_OFF    ; = 0x0510
XM_A20_PAT1    equ 0x1234              ; written low
XM_A20_PAT2    equ 0x4321              ; written through the alias
XM_A20_SETTLE  equ 512                 ; probes to allow a gate to take

; -----------------------------------------------------------------------------
; xm_a20_probe - is the A20 line open RIGHT NOW? (SPEC.md 41.2)
;
; in:       -
; out:      CF = 0 open, CF = 1 shut; CPU_F_A20 in [xm_feat] set to match.
;           THIS ROUTINE IS THE ONLY WRITER OF THAT BIT.
; clobbers: nothing else (flags)
;
; Write a known word to 0000:0500, a different word through the alias at
; HMA_SEG:0510, then read 0000:0500 back. Unchanged means the second write
; landed a megabyte up and the line is open; mutated means it wrapped.
;
; The saved words are restored in the opposite order to the writes, which is
; correct in BOTH cases: with the line shut the two addresses are the same
; word, and putting back the alias value (which is then just the pattern we
; wrote low) before the low value leaves the original byte pair intact.
;
; The whole thing runs inside one pushf/cli ... popf - an interrupt that
; touched conventional memory between the write and the read-back would make
; the probe lie - and every register including ES is restored.
; -----------------------------------------------------------------------------
xm_a20_probe:
    push ax
    push bx
    push dx
    push es
    pushf
    cli                         ; --- the probe window ---------------------
    xor ax, ax
    mov es, ax
    mov bx, [es:XM_A20_OFF]            ; save the low word
    mov word [es:XM_A20_OFF], XM_A20_PAT1
    mov ax, HMA_SEG
    mov es, ax
    mov dx, [es:XM_A20_ALIAS]          ; save the aliased word (= PAT1 if
                                        ; the line is shut - see above)
    mov word [es:XM_A20_ALIAS], XM_A20_PAT2
    xor ax, ax
    mov es, ax
    mov ax, [es:XM_A20_OFF]            ; did the high write land down here?
    push ax
    mov ax, HMA_SEG
    mov es, ax
    mov [es:XM_A20_ALIAS], dx          ; restore, alias first
    xor ax, ax
    mov es, ax
    mov [es:XM_A20_OFF], bx
    pop ax
    popf                        ; --- probe window closed ------------------
    and byte [xm_feat], 0xFF - CPU_F_A20
    cmp ax, XM_A20_PAT1
    jne .shut                   ; the low word was overwritten: it wrapped
    or byte [xm_feat], CPU_F_A20
    pop es
    pop dx
    pop bx
    pop ax
    clc
    ret
.shut:
    pop es
    pop dx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; xm_a20_settle - re-probe a few times, to let a gate take effect
;
; The keyboard controller's gate command in particular is not instantaneous:
; the controller acknowledges long before the line moves. A bounded retry is
; the difference between "this machine has no A20" and "we asked too early".
; -----------------------------------------------------------------------------
xm_a20_settle:
    push cx
    mov cx, XM_A20_SETTLE
.spin:
    call xm_a20_probe
    jnc .open
    loop .spin
    pop cx
    stc
    ret
.open:
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; xm_kbc_wait - wait for the keyboard controller's input buffer to empty
;
; out:      CF = 0 empty, CF = 1 timed out
;
; The timeout is what stops a wedged controller from hanging the machine
; (SPEC.md 41.2). 65,536 polls of an ISA port is on the order of 60ms - paid
; at most three times, and only on a machine whose controller is dead.
; -----------------------------------------------------------------------------
xm_kbc_wait:
    push ax
    push cx
    xor cx, cx                  ; 65,536 polls
.poll:
    in al, 0x64
    test al, 0x02               ; input buffer full?
    jz .empty
    loop .poll
    pop cx
    pop ax
    stc
    ret
.empty:
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; xm_fast_a20 - the fast gate at port 0x92
;
; out:      nothing (the PROBE decides whether it worked)
;
; Read the port, set bit 1, write it back. NEVER WRITE BIT 0: that is the
; fast-reset line and a 1 there reboots the machine (SPEC.md 41.2). A read of
; 0xFF - "no such port", the common answer on a machine that has none -
; already has bit 1 set, so this declines to write at all rather than
; guessing; the keyboard controller is the next step either way.
; -----------------------------------------------------------------------------
xm_fast_a20:
    push ax
    pushf
    cli
    in al, 0x92
    test al, 0x02
    jnz .out                    ; already set, and the line is still shut:
                                ; 0x92 is not the gate on this machine
    or al, 0x02
    and al, 0xFE                ; mask bit 0 - fast reset - unconditionally
    out 0x92, al
.out:
    popf
    pop ax
    ret

; -----------------------------------------------------------------------------
; xm_kbc_a20 - ask the keyboard controller to open the gate
;
; Command D1h "write output port" to 0x64, then the output-port value DFh to
; 0x60 - bit 1 of that port is A20, and DFh is the standard value that sets it
; while leaving the reset line (bit 0) alone.
;
; THE WHOLE SEQUENCE IS ONE pushf/cli ... popf WINDOW (SPEC.md 41.2), and that
; is not tidiness: between the D1h and the DFh the controller is armed to take
; the NEXT byte written to 0x60 as its output port. The kernel does not hook
; int 09h, so the BIOS keyboard ISR is live here; a key down or repeating at
; that instant makes it read 0x60 and write a keyboard command back - and the
; 8042 would consume THAT as the output port value. Bit 0 of that port is the
; active-low CPU RESET line, so 0xF4 or 0xF6 (both perfectly ordinary bytes
; for the BIOS to send) reboot the machine. The mild version of the same race
; is the ISR's own 0x64 traffic satisfying the pending D1h with something
; benign, leaving A20 shut and the machine with no extended memory it can
; prove it has.
;
; MOVING THIS OUT OF THE KERNEL SHRANK ITS BLAST RADIUS RATHER THAN GROWING IT
; (SPEC.md 41.12.1). It used to run on every 286+ boot, including the ones
; with exactly 1MB, because the kernel asked the gate before it asked the
; BIOS how much memory there was. The kernel's sniff asks AH=88h first now, so
; this sequence is only ever reached on a machine that has already reported
; RAM above 1MB.
; -----------------------------------------------------------------------------
xm_kbc_a20:
    push ax
    pushf
    cli                         ; --- the gate window ----------------------
    call xm_kbc_wait
    jc .out
    mov al, 0xD1
    out 0x64, al
    call xm_kbc_wait
    jc .out
    mov al, 0xDF
    out 0x60, al
    call xm_kbc_wait           ; let the command drain before probing
.out:
    popf                        ; --- window closed ------------------------
    pop ax
    ret

; -----------------------------------------------------------------------------
; xm_a20_enable - open the A20 line, and VERIFY it (SPEC.md 41.2)
;
; out:      CF = 0 and CPU_F_A20 set in [xm_feat]: the line is verified open.
;           CF = 1 and the bit clear: it is not, on any tier.
;
; Three steps in a binding order:
;
;   1. Test before enabling. Every machine the harness boots comes up with A20
;      already open, and so do many later AT BIOSes. If the probe says open,
;      no gate is touched at all.
;   2. The fast gate at port 0x92 (xm_fast_a20), then re-probe.
;   3. The keyboard controller (xm_kbc_a20), then re-probe.
; -----------------------------------------------------------------------------
xm_a20_enable:
    push ax
    and byte [xm_feat], 0xFF - CPU_F_A20
    call xm_a20_probe
    jnc .done                   ; already open - do not poke a working line
    call xm_fast_a20           ; step 2
    call xm_a20_settle
    jnc .done
    call xm_kbc_a20            ; step 3
    call xm_a20_settle
.done:
    pop ax
    test byte [xm_feat], CPU_F_A20      ; the PROBE decides, never the poke
    jz .fail
    clc
    ret
.fail:
    stc
    ret

; -----------------------------------------------------------------------------
; xm_hma_claim - claim the High Memory Area for one named owner (SPEC.md 41.3)
;
; in:       AX = extended-memory KB as int 15h AH=88h reported it
; out:      CF = 0 claimed, CPU_F_HMA set; CF = 1 refused
;
; Refuses when the BIOS reports fewer than XM_HMA_KB, which is not
; hypothetical: a 1MB AT has zero bytes above 0x0FFFFF and claiming an HMA
; there would hand out 64KB of nothing.
;
; There is no HMA allocator and there will not be one. A 65,520-byte region
; with two implicit owners is the bug factory SPEC.md 2.2 refuses to build, so
; the claim is all-or-nothing and the claimant names itself in the source.
; NOTHING IS CLAIMING IT TODAY - the bit exists so the pool knows where it
; starts (SPEC.md 2.4) and so the first claimant is a two-line change rather
; than an allocator.
; -----------------------------------------------------------------------------
xm_hma_claim:
    and byte [xm_feat], 0xFF - CPU_F_HMA
    test byte [xm_feat], CPU_F_A20      ; the verified bit, not the poke
    jz .no
    cmp ax, XM_HMA_KB
    jb .no                      ; less than 64KB up there: nothing to claim
    or byte [xm_feat], CPU_F_HMA
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; xm_arm - enter unreal mode: give FS and GS a 4GB limit (SPEC.md 41.4)
;
; out:      FS and GS cached with a base-0, limit-4GB data descriptor;
;           CPU_F_UNREAL set. A no-op on any tier below 2.
; clobbers: nothing (flags)
;
; Called once from xm_attach, and then once per XM_UCHUNK-byte chunk from
; xm_ucopy - inside that chunk's cli window, so that nothing can enter the
; BIOS between the arm and the accesses that depend on it (see the file
; header). Calling it repeatedly is a non-event: the transition is idempotent
; and preserves every register and the flags.
;
; The whole transition runs inside one pushf/cli ... popf with NMI masked
; (CMOS index port 0x70 bit 7) across it: a real-mode IVT is meaningless while
; CR0.PE is set, so an interrupt taken in the window is a triple fault.
; Nothing in the kernel touches 0x70 outside clock.inc's int 1Ah calls, so
; there is no conflict; the read of 0x71 after each write to 0x70 is the
; classic requirement to leave the RTC's index/data pair settled.
;
; THAT THE KERNEL'S OWN ISRs ARE LIVE BY THE TIME THIS FIRST RUNS IS NOT NEW
; (SPEC.md 41.12.1). The kernel used to call it before sched_init, on the
; reasoning that no kernel ISR was installed yet - a comfort, not a
; requirement, because xm_ucopy has always re-armed at run time with int 08h
; hooked and IRQ3/IRQ4 live. If an ISR could break the transition the shipped
; transport would have been broken since it was written; what protects it is
; the cli and the NMI mask, and both are here.
;
; There is no far jump. CS is never reloaded and keeps its real-mode base,
; which is what makes the instruction after `mov cr0` reachable at all.
;
; Two entries, not three, in the GDT: there is no code descriptor because
; THERE IS NO FAR JUMP AND CS IS NEVER RELOADED. The whole point of the
; sequence is the hidden descriptor caches of FS and GS.
; -----------------------------------------------------------------------------
xm_arm:
    cmp byte [xm_tier], CPU_386
    jne .out                    ; the run-time half of SPEC.md 41.9 rule 2 -
                                ; the island below must not be REACHED on a
                                ; 286, whatever the assembler allowed
    push ax
    push bx
    push cx
    push dx
    pushf
    cli                         ; --- the transition window ----------------
    mov al, 0x80
    out 0x70, al                ; NMI off (bit 7); index 0 is harmless
    in  al, 0x71                ; settle the RTC index/data pair
    mov ax, ds                  ; the lgdt base is LINEAR: DS<<4 + xm_gdt.
    mov dx, ax                  ; DS is OUR segment - a heap claim, so this
    mov cl, 4                   ; is computed and never baked in, which is
    shl ax, cl                  ; what lets the table live in a loaded image
    mov cl, 12
    shr dx, cl
    add ax, xm_gdt
    adc dx, 0
    mov [xm_gdtr+2], ax
    mov [xm_gdtr+4], dx
cpu 386                         ; ---- 386-only island, tier 2 only --------
    lgdt [xm_gdtr]              ; (16-bit operand size loads 24 base bits;
                                ; the image is a heap claim below 1MB, so
                                ; that is exact)
    mov eax, cr0
    or al, 1                    ; CR0.PE - protected mode, briefly
    mov cr0, eax
    jmp short .flush1           ; flush the prefetch queue
.flush1:
    mov bx, XM_SEL_FLAT
    mov fs, bx                  ; THE ONLY WRITES TO FS OR GS IN THE TREE.
    mov gs, bx                  ; Their hidden caches keep the 4GB limit
    mov eax, cr0                ; after PE goes away again.
    and al, 0xFE
    mov cr0, eax
    jmp short .flush2
.flush2:
cpu 8086                        ; ---- island closed -----------------------
    xor al, al
    out 0x70, al                ; NMI back on
    in  al, 0x71
    popf                        ; --- window closed ------------------------
    or byte [xm_feat], CPU_F_UNREAL
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; xm_caps - what is left up there? (XMV_CAPS, behind slot 0x0190)
;
; out:      AX = KB the pool can still hand out (0 = none, and alloc, free and
;           copy will all refuse), DX:CX = the pool's 32-bit linear base,
;           BL = free block-table entries of XM_MAX_BLKS
; clobbers: nothing else (BH, SI, DI and the rest preserved; flags)
;
; This is the figure a package branches on. NEVER the tier byte: a 386 whose
; A20 gate did not verify answers 0 here just as an 8088 does (SPEC.md 41.8) -
; and so does a machine where this image never loaded, which the kernel's own
; cell answers without reaching us at all.
; -----------------------------------------------------------------------------
xm_caps:
    push si
    mov ax, [xm_kb]
    mov dx, [xm_base_hi]
    xor bl, bl                  ; BH is the caller's, and stays that way
    mov si, xm_tab
    mov cx, XM_MAX_BLKS         ; CX is an output, so the loop may have it
.scan:
    cmp word [si+XB_KB], 0
    jne .used
    inc bl
    jmp short .next
.used:
    sub ax, [si+XB_KB]
.next:
    add si, XM_BLKSZ
    loop .scan
    mov cx, [xm_base_lo]        ; ...and it is filled in last
    pop si
    ret

; -----------------------------------------------------------------------------
; xm_alloc - first-fit a run of extended memory (XMV_ALLOC, slot 0x0198)
;
; in:       DX:AX = bytes wanted (rounded up to 1KB), BL = the owner to stamp
; out:      CF = 0 and DX:AX = the block's 32-bit linear base;
;           CF = 1 and AX = 0 no store / 1 no free block-table entry /
;           2 no contiguous run that big
; clobbers: nothing else (flags)
;
; XM_MAX_BLKS entries, 1KB granularity. Deliberately small: extended memory
; here is a bulk store for a handful of large claims - a render buffer, a
; captured sample, a package's own heap - not a malloc. A caller that wants
; many small pieces takes one block and subdivides it itself.
;
; THE OWNER ARRIVES IN BL AND IS NOT ASKED FOR HERE (SPEC.md 41.12.2). It used
; to call snd_req_inst, which reads [snd_inst] and the running task's T_INST -
; both kernel state - so a loaded image that had to ask would need an API slot
; of its own. The kernel's cell stamps it, exactly as osapi_snd_fm stamps DH.
;
; Free space is IMPLICIT - it is whatever no in-use entry covers - so the
; search is: start the candidate at offset 0, walk the table, and every time
; the candidate overlaps a block push it past that block's end and start over.
; Each restart moves the candidate strictly forward past at least one block,
; so it terminates in at most XM_MAX_BLKS restarts, and a freed block merges
; with its neighbours for free because nothing records the gaps.
; -----------------------------------------------------------------------------
xm_alloc:
    push bx
    push cx
    push si
    push di
    mov [xm_own], bl            ; bank the owner: BX is about to be the walk
    cmp word [xm_kb], 0
    je .nostore
    add ax, 1023                ; round the request up to whole KB
    adc dx, 0
    jc .norun                   ; the round-up carried out of 32 bits: a
                                ; request in the top 1023 bytes of the range
                                ; would otherwise wrap to nearly zero and be
                                ; granted 1KB as a SUCCESS
    cmp dx, 0x0400
    jae .norun                  ; over 64MB: no run that big can exist
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    mov cx, ax                  ; CX = KB wanted
    or cx, cx
    jnz .slot
    inc cx                      ; a zero-byte request still costs 1KB
.slot:
    xor di, di                  ; DI = a free table entry, 0 = none found
    mov bx, xm_tab
    mov ax, XM_MAX_BLKS
.fscan:
    cmp word [bx+XB_KB], 0
    jne .fnext
    mov di, bx
    jmp short .fdone
.fnext:
    add bx, XM_BLKSZ
    dec ax
    jnz .fscan
.fdone:
    or di, di
    jz .full
    xor si, si                  ; SI = the candidate offset, KB from the base
.restart:
    mov bx, xm_tab
    mov ax, XM_MAX_BLKS
.scan:
    cmp word [bx+XB_KB], 0
    je .snext                   ; a free entry covers nothing
    mov dx, [bx+XB_OFF]
    add dx, [bx+XB_KB]
    cmp si, dx
    jae .snext                  ; the candidate starts at or after this block
    mov dx, si
    add dx, cx
    jc .norun                   ; the candidate's end wrapped: too big
    cmp [bx+XB_OFF], dx
    jae .snext                  ; this block starts at or after our end
    mov si, [bx+XB_OFF]         ; overlap - push the candidate past it and
    add si, [bx+XB_KB]          ; re-check every block from the top
    jmp short .restart
.snext:
    add bx, XM_BLKSZ
    dec ax
    jnz .scan
    mov dx, si                  ; SI is clear of every block: does it fit in
    add dx, cx                  ; the pool?
    jc .norun
    cmp dx, [xm_kb]
    ja .norun
    mov al, [xm_own]
    pushf
    cli                         ; the publish is ONE store as far as any other
    mov [di+XB_OFF], si         ; task is concerned. Ungated, a teardown
    mov [di+XB_KB], cx          ; running between the XB_KB and XB_OWN stores
    mov [di+XB_OWN], al         ; sees a live entry carrying a STALE owner byte
    popf                        ; and frees the block out from under us - and
                                ; the next alloc hands the same range to a
                                ; second package
    mov ax, si                  ; the public answer is 32-bit linear:
    mov dx, si                  ; base + offset * 1024
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    add ax, [xm_base_lo]
    adc dx, [xm_base_hi]
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret
.nostore:
    xor ax, ax
    jmp short .fail
.full:
    mov ax, 1
    jmp short .fail
.norun:
    mov ax, 2
.fail:
    pop di
    pop si
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; xm_free - give a block back (XMV_FREE, slot 0x01A0)
;
; in:       DX:AX = a base this caller owns, BL = the caller
; out:      CF = 0 freed and merged with the adjacent free runs;
;           CF = 1 not a base, or not the caller's
; clobbers: nothing (DX:AX preserved; flags)
;
; The merge is free: a released entry stops covering its range and the
; allocator's candidate walk sees one longer gap (see xm_alloc).
; -----------------------------------------------------------------------------
xm_free:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [xm_own], bl
    sub ax, [xm_base_lo]        ; offset = (base - pool base) / 1024, exactly
    sbb dx, [xm_base_hi]
    jc .bad                     ; below the pool
    test ax, 0x03FF
    jnz .bad                    ; not 1KB-aligned: never something we handed out
    cmp dx, 0x0400
    jae .bad
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    mov bx, ax                  ; BX = the KB offset
    mov al, [xm_own]
    mov cx, XM_MAX_BLKS
    mov si, xm_tab
    pushf
    cli                         ; match and release are one operation against
                                ; a concurrent teardown (see xm_alloc)
.scan:
    cmp word [si+XB_KB], 0
    je .next
    cmp [si+XB_OFF], bx
    jne .next
    cmp [si+XB_OWN], al
    jne .next                   ; someone else's block: not yours to free
    mov word [si+XB_KB], 0
    mov byte [si+XB_OWN], XM_OWN_KERN   ; never leave a live instance slot
    popf                                ; stamped on a free entry
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.next:
    add si, XM_BLKSZ
    loop .scan
    popf                        ; nothing matched
.bad:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; xm_release_inst - force-free every block an instance holds (XMV_RELINST)
;
; in:       AL = instance slot
; out:      nothing
; clobbers: nothing (flags)
;
; The SPEC.md 41.5 teardown leg, dispatched from the kernel's xm_release_rec
; at SPEC.md 29.4's three teardown sites - the same shape, and the same three
; sites, as snd_release_rec beside it.
;
; It runs on the DYING TASK (inst_task_die, gfx lock NOT held) while the UI
; task may be inside xm_alloc or xm_free, so the pushf/cli is not decoration:
; a scan interleaved with either mutator reads a half-published entry. Safe on
; a slot that holds nothing.
;
; Those three calls were absent for a year - the #51 integration merge dropped
; all of them and the comment describing them survived the code, which is
; docs/UPSTREAM.md's hazard in its purest form: the merge assembled, booted
; and said nothing, because a call that is simply absent breaks no build.
; tests/xmtest + tests/xmcheck.py is the gate that makes the next such loss
; loud, and it is verified to FAIL with the three calls removed.
; -----------------------------------------------------------------------------
xm_release_inst:
    push ax
    call xm_relall
    pop ax
    ret

; xm_relall - the walk itself. in AL = the owner to release; every block
; carrying it is freed. XM_OWN_KERN releases everything, which is what detach
; wants and what no instance teardown can ever ask for (a free entry is
; stamped XM_OWN_KERN, and a zero XB_KB is what the scan tests first).
xm_relall:
    push cx
    push si
    pushf
    cli
    mov cx, XM_MAX_BLKS
    mov si, xm_tab
.scan:
    cmp word [si+XB_KB], 0
    je .next
    cmp al, XM_OWN_KERN
    je .kill                    ; detach: everything goes
    cmp [si+XB_OWN], al
    jne .next
.kill:
    mov word [si+XB_KB], 0
    mov byte [si+XB_OWN], XM_OWN_KERN   ; a free entry never carries a live
.next:                                  ; instance slot
    add si, XM_BLKSZ
    loop .scan
    popf
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; xm_chk - does a range lie wholly inside a block this caller owns?
;
; in:       DX:AX = 32-bit linear extended address, CX = bytes,
;           [xm_own] = the caller
; out:      CF = 0 yes, CF = 1 no
; clobbers: nothing (flags)
;
; This check is the only thing between a package and every byte of the
; machine's RAM, which is why xm_copy runs it BEFORE the tier branch rather
; than inside either arm of it (SPEC.md 41.5).
;
; It works in KB, because every block boundary is KB-aligned: the range is
; inside the block iff the block starts at or below the KB the range starts
; in, and ends at or above the KB the range's last byte ends in. That turns a
; 32-bit interval test into two 16-bit compares.
; -----------------------------------------------------------------------------
xm_chk:
    push ax
    push bx
    push cx
    push dx
    push si
    sub ax, [xm_base_lo]
    sbb dx, [xm_base_hi]
    jc .no                      ; below the pool base
    cmp dx, 0x0400
    jae .no                     ; over 64MB past it: past any pool
    mov bx, ax                  ; k1 = the KB the range starts in
    mov si, dx
    push cx
    mov cl, 10
    shr bx, cl
    mov cl, 6
    shl si, cl
    pop cx
    or bx, si
    add ax, cx                  ; k2 = one past the last KB it touches
    adc dx, 0
    add ax, 1023
    adc dx, 0
    cmp dx, 0x0400
    jae .no
    push cx
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    pop cx
    or ax, dx
    mov dx, ax                  ; DX = k2, BX = k1
    mov al, [xm_own]
    mov si, xm_tab
    mov cx, XM_MAX_BLKS
.scan:
    cmp word [si+XB_KB], 0
    je .next
    cmp [si+XB_OWN], al
    jne .next
    push ax
    mov ax, [si+XB_OFF]
    cmp ax, bx
    ja .npop                    ; the block starts after the range does
    add ax, [si+XB_KB]
    cmp dx, ax
    ja .npop                    ; the range ends after the block does
    pop ax
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.npop:
    pop ax
.next:
    add si, XM_BLKSZ
    loop .scan
.no:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; xm_copy - move bytes between conventional and extended memory
;           (XMV_COPY, behind slot 0x01A8, SPEC.md 41.5)
;
; in:       ES:SI = the kernel's XMC_ block (ES is KERNEL_SEG by construction)
; out:      CF = 0 done; CF = 1 and AX = 1 no store (or the transport refused)
;           / 2 the range is not inside a block this caller owns / 3 bad length
; clobbers: nothing else (flags)
;
; BOTH ENDS ARRIVE AS 32-BIT LINEAR ADDRESSES, and that is forced rather than
; chosen (SPEC.md 41.12.2): the public slot takes the conventional end as
; ES:SI, and drv_call loads ES with KERNEL_SEG on its way in, so the caller's
; segment cannot survive the crossing. The kernel folds it down before it
; dispatches - arithmetic this routine used to do internally anyway - and the
; block carries the direction and the requesting instance with it.
;
; [sch_lock] IS THE KERNEL'S NOW. It used to be raised inside both transports;
; a loaded image cannot reach it (there is no API slot), so the kernel's cell
; raises it around the whole dispatch, which is strictly wider and is exactly
; how dsk_xfer hands a locked scheduler to a block driver.
;
; One ABI, two transports, and the caller cannot tell which ran - that is what
; makes the slot safe to hand to an 8086-only package.
; -----------------------------------------------------------------------------
xm_copy:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp word [xm_kb], 0
    je .e1
    mov al, [es:si+XMC_INST]
    mov [xm_own], al
    mov cx, [es:si+XMC_LEN]
    or cx, cx
    jz .e3
    test cx, 1
    jnz .e3                     ; AH=87h counts words: even lengths only
    cmp cx, XM_MAX_COPY
    ja .e3
    mov ax, [es:si+XMC_EXT]
    mov dx, [es:si+XMC_EXT+2]
    call xm_chk                 ; bounds FIRST, before the tier branch
    jc .e2
    mov bp, cx                  ; BP = the byte count
    mov di, [es:si+XMC_CONV]
    mov bx, [es:si+XMC_CONV+2]
    cmp byte [es:si+XMC_DIR], 0
    jne .inbound
    mov [xm_src], di            ; conventional -> extended
    mov [xm_src+2], bx
    mov [xm_dst], ax
    mov [xm_dst+2], dx
    jmp short .go
.inbound:
    mov [xm_dst], di            ; extended -> conventional
    mov [xm_dst+2], bx
    mov [xm_src], ax
    mov [xm_src+2], dx
.go:
    cmp byte [xm_tier], CPU_386
    je .unreal
    call xm_bios                ; tier 1
    jc .e1
    jmp short .ok
.unreal:
    call xm_ucopy               ; tier 2
    jc .e1
.ok:
    xor ax, ax
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.e1:
    mov ax, 1
    jmp short .fail
.e2:
    mov ax, 2
    jmp short .fail
.e3:
    mov ax, 3
.fail:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; xm_ucopy - the tier-2 transport: 32-bit moves through FS and GS
;
; in:       [xm_src] / [xm_dst] = 32-bit linear ends, BP = bytes (even)
; out:      CF = 0 moved, CF = 1 not tier 2 (see the guard below)
; clobbers: nothing (flags)
;
; Load through FS, store through GS, both flat, so the routine never touches
; ES or DS and serves both directions with one loop. It is a load/store loop
; and not `rep movsd` for a structural reason: MOVS takes its destination from
; ES:(E)DI and ES is the ONE segment a string move cannot override - so a flat
; destination would mean widening ES, which SPEC.md 41.4 forbids precisely
; because ES is reloaded constantly.
;
; THE COPY RUNS IN XM_UCHUNK-BYTE CHUNKS, AND EACH CHUNK RE-ARMS INSIDE ITS
; OWN pushf/cli ... popf. That structure is the whole correctness argument,
; and the obvious cheaper shapes are both wrong:
;
;   - Re-arming once and then copying with IF=1 does not work. sch_isr chains
;     `call far [sch_old08]` into the BIOS timer handler on EVERY tick, and
;     IRQ1 reaches the BIOS keyboard handler the same way. A 386-aware BIOS
;     that loads FS or GS for its own use hands them back with real-mode
;     limits - the exact hazard this module exists to defend against - and the
;     rest of the copy then wraps inside 64KB with no fault, or takes a #GP
;     into an IVT slot the kernel never fills.
;   - [sch_lock] does not close that hole either. It stops TASK SWITCHES, not
;     interrupts, so it cannot protect a loop from an ISR. The KERNEL raises it
;     around this whole dispatch for the two things it genuinely does: it
;     keeps the scratch below to one copy at a time, and it stops a switch
;     landing between two chunks.
;   - One cli around the whole transfer would be correct but expensive: 32KB
;     at 386SX speeds is milliseconds of IF=0, which drops PIT ticks and
;     overruns the 1200-baud mouse UART (SPEC.md 9).
;
; A 1KB chunk is ~0.3ms of IF=0 on the slowest tier-2 machine and the arm
; costs a few microseconds, so the whole cap of 32 chunks adds well under a
; tick. A 16-bit ISR taken BETWEEN chunks is harmless: the loop keeps its
; state in [xm_src]/[xm_dst] and BP, and the next chunk re-arms anyway.
; -----------------------------------------------------------------------------
xm_ucopy:
    cmp byte [xm_tier], CPU_386
    jne .refuse                 ; the island below must never be REACHED on a
                                ; 286 (SPEC.md 41.9 rule 2). xm_copy's branch
                                ; already guarantees that; this is the guard
                                ; that does not depend on a caller being right.
    push ax
    push cx
    push dx
    push si
    push di
    push bp
.chunk:
    mov cx, bp
    or cx, cx
    jz .fin
    cmp cx, XM_UCHUNK
    jbe .have
    mov cx, XM_UCHUNK
.have:
    sub bp, cx
    mov dx, cx                  ; the byte count, kept for the odd tail
    pushf
    cli                         ; --- one chunk, atomic against every ISR --
    call xm_arm                 ; re-armed INSIDE the window that uses it
cpu 386                         ; ---- 386-only island, tier 2 only --------
    mov esi, [xm_src]
    mov edi, [xm_dst]
    movzx ecx, cx
    add [xm_src], ecx           ; advance both ends for the next chunk
    add [xm_dst], ecx
    shr ecx, 2                  ; whole dwords
    jz .tail
.dword:
    mov eax, [fs:esi]
    mov [gs:edi], eax
    add esi, 4
    add edi, 4
    dec ecx
    jnz .dword
.tail:
    test dl, 2                  ; the odd tail, moved as a word
    jz .cdone
    mov ax, [fs:esi]
    mov [gs:edi], ax
.cdone:
cpu 8086                        ; ---- island closed -----------------------
    popf                        ; --- chunk window closed ------------------
    jmp short .chunk
.fin:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret
.refuse:
    stc
    ret

; -----------------------------------------------------------------------------
; xm_bios - the tier-1 transport: int 15h AH=87h block move
;
; in:       [xm_src] / [xm_dst] = 32-bit linear ends, BP = bytes (even)
; out:      CF = 0 moved, CF = 1 the BIOS refused or an end is out of range
; clobbers: nothing else (flags)
;
; AH=87h wants ES:SI pointing at a 48-byte block of six 8-byte descriptors -
; dummy, GDT, source, destination, BIOS CS, stack - of which the caller fills
; only source and destination; the BIOS builds the other four itself, which is
; why the whole block is zeroed first and the reserved bytes are left alone.
; CX is a WORD count, and that is where xm_copy's even-length rule comes from.
;
; The descriptor layout, which is the most error-prone part of the whole
; thing:
;     +0  word  limit = length - 1
;     +2  word  base 15..0
;     +4  byte  base 23..16
;     +5  byte  access: 0x93 = present, ring 0, data, writable
;     +6  byte  reserved - MUST be 0 (a 386 reads it as limit 19..16/flags)
;     +7  byte  base 31..24 - MUST be 0 on a 286
; so the bases are 24-bit, which is why xm_attach clamps the tier-1 pool to
; 16MB and why both high words are re-checked here before the call.
;
; The block must live in conventional memory and is addressed ES:SI - it is in
; OUR image, which is a heap claim well below 1MB, so ES is pointed at DS for
; the call and restored. [sch_lock] is the kernel's, raised around the whole
; dispatch for the int 13h reason: the BIOS runs this with interrupts off and
; a switch on the way out would be taken with the machine's state
; half-restored.
; -----------------------------------------------------------------------------
xm_bios:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es                      ; the block is in our own image
    mov di, xm_x87
    mov cx, 24
    xor ax, ax
    cld
    rep stosw                   ; all 48 bytes: every field the BIOS fills,
                                ; and every reserved byte, must start at 0
    mov cx, bp
    dec cx                      ; limit = length - 1
    mov [xm_x87 + 0x10], cx     ; source descriptor
    mov [xm_x87 + 0x18], cx     ; destination descriptor
    mov ax, [xm_src + 2]
    or ah, ah
    jnz .bad                    ; above 16MB: not expressible here
    mov [xm_x87 + 0x14], al     ; base 23..16
    mov ax, [xm_src]
    mov [xm_x87 + 0x12], ax     ; base 15..0
    mov byte [xm_x87 + 0x15], 0x93
    mov ax, [xm_dst + 2]
    or ah, ah
    jnz .bad
    mov [xm_x87 + 0x1C], al
    mov ax, [xm_dst]
    mov [xm_x87 + 0x1A], ax
    mov byte [xm_x87 + 0x1D], 0x93
    mov si, xm_x87
    mov cx, bp
    shr cx, 1                   ; AH=87h counts WORDS
    mov ah, 0x87
    int 0x15
    jc .bad
    or ah, ah
    jnz .bad                    ; 1 parity, 2 exception, 3 A20 - all fatal
    pop es                      ; to this call and none of them retryable
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; Data. A driver has NO BSS (drivers/os88drv.inc): zeroed data is written as
; db 0 here and ships in the image, which is what buys a load path with
; exactly one heap claim in it.
;
; The GDT is initialised data and could not be bss in any case; the lgdt base
; is computed at run time from DS, so nothing here depends on where the kernel
; put us.
; -----------------------------------------------------------------------------
xm_tier:        db 0            ; our own copy of the CPU tier (OSAPI_CPU_INFO)
xm_feat:        db 0            ; CPU_F_* - what actually VERIFIED. This image
                                ; is the only writer of these three bits now,
                                ; and hands them back at attach so cpu_info's
                                ; AH keeps meaning what it always meant
xm_own:         db 0            ; the owner the kernel stamped for this call
                                ; (SPEC.md 41.12.2) - banked because the walk
                                ; below needs BX
xm_kb:          dw 0            ; KB the pool may hand out. PUBLISHED LAST by
                                ; xm_attach (the snd_live ordering of SPEC.md
                                ; 34.7): it is the single gate every entry
                                ; point here tests, so it must not be visible
                                ; before the base and the table behind it
xm_base_lo:     dw 0            ; the pool's 32-bit linear base: 0x00100000,
xm_base_hi:     dw 0            ; or 0x00110000 when the HMA was claimed

xm_gdt:
    dw 0, 0, 0, 0               ; entry 0: the null descriptor
    dw 0xFFFF                   ; entry 1 (XM_SEL_FLAT): limit 15..0
    dw 0x0000                   ;   base 15..0
    db 0x00                     ;   base 23..16
    db 0x92                     ;   present, ring 0, data, read/write
    db 0xCF                     ;   G=1 (4KB granules), D/B=1, limit 19..16
    db 0x00                     ;   base 31..24  -> base 0, limit 4GB
xm_gdt_end:

xm_gdtr:
    dw xm_gdt_end - xm_gdt - 1  ; the pseudo-descriptor lgdt loads
    dw 0, 0                     ;   32-bit linear base, filled by xm_arm

xm_src:         dd 0            ; the copy's two ends, ordered, as 32-bit
xm_dst:         dd 0            ; linear addresses
xm_x87:         times 48 db 0   ; int 15h AH=87h's descriptor block - it must
                                ; itself be in conventional memory, and this is
xm_tab:         times XM_MAX_BLKS * XM_BLKSZ db 0   ; the block table

OS88_DRV_END
