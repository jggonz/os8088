; =============================================================================
; os8088 - xmem.inc
;
; The kernel's HALF of the store above 1MB (SPEC.md 41). The store itself -
; the A20 gate, the HMA claim, the sizing, the allocator and both copy
; transports - is `XMEM.DRV`, an OVERLAY loaded at boot on the machines that
; have memory up there and on no others (SPEC.md 41.12). What is left here is
; the four published cells, the boot sniff that decides whether to load, and
; the dispatch between them.
;
; WHY IT IS AN OVERLAY AND NOT A DRIVER (SPEC.md 41.12): a driver row buys
; user management - a Drivers-page tick, a SYSTEM.CFG bit, a caption, an
; unload - and there is nothing here for a user to decide. A machine either
; has memory above 1MB or it does not; the sniff below answers that EXACTLY,
; because it is the same `int 15h` AH=88h the overlay sizes with. So this
; takes HDDTOOL.DRV's shape (SPEC.md 52.11): a driver-format image with class
; DRVC_OVL, no row in drv_tab, no publication slot and no Control Panel page,
; loaded and attached by its OWNER - which for HDDTOOL is HDD.DRV and here is
; the kernel. DRV_MAX is still 4 and DRVC_MAX is still 5.
;
; WHAT IT BOUGHT, and it is the whole reason: a machine with no XMS - which is
; every 8088, and the machine this project is calibrated against - was
; reserving about 1.2KB of kernel image forever for a feature it can never
; reach. kern_small had already dropped it outright (SPEC.md 41.11); this is
; the same removal for kern_big, keeping the capability.
;
; NOTHING IN THE TREE ALLOCATES OUT OF THE POOL TODAY, and that is worth
; knowing before reading any comment here as a description of live behaviour.
; Exactly one consumer has ever existed - SPEC.md 53.6.1's fullscreen desktop
; stash - and it was removed. What remains is a published package ABI
; (SPEC.md 20.8 rule 4) with no caller inside the kernel, which is a
; deliberate state and not an oversight; consumers are expected.
;
; [xm_kb] IS THE ONE GATE, and it means two things at once on purpose: "there
; is a store" and "the overlay is attached". The kernel writes it LAST, after
; the service table has been staged, and only on a successful attach - so
; every cell below can test one word and reach either the overlay or the
; tier-0 answer, and there is no second flag to fall out of step with it. It
; is also the identical test each of these routines made when they were
; resident, which is why none of their contracts moved.
;
; The four cells answer TIER 0's answers when nothing is loaded, byte for
; byte, and that is the safety argument rather than a convenience (SPEC.md
; 41.11): the target machine is an 8088, so a kernel on the machine this
; project is calibrated against has always answered precisely this, and every
; shipped package is already exercised against the path on real hardware. It
; is deliberately not one shared refuser - the codes differ and callers read
; them: ALLOC's "no store" is AX = 0, COPY's is AX = 1, and FREE answers CF
; alone with DX:AX preserved.
;
; kern_small keeps the %else at the foot of this file and carries neither the
; row nor the sniff: its floor machine is an 8088 and it has no store at all.
; =============================================================================

%ifdef KERN_BIG

XM_ABI_VER  equ 1               ; the private kernel<->overlay ABI, mirrored
                                ; in drivers/xmem/xmem.asm and passed in AH at
                                ; DRVV_ATTACH. A KERNEL.SYS and an XMEM.DRV of
                                ; different vintages - a half-copied floppy -
                                ; must fail loudly at load rather than quietly
                                ; at the first copy (SPEC.md 52.11's +10 word,
                                ; carried in a register because the image is
                                ; what compares it)

; --- the service table the overlay returns at attach, mirrored from it -------
XMV_CAPS    equ 0               ; out AX = free KB, DX:CX = pool base,
                                ;     BL = free block-table entries
XMV_ALLOC   equ 2               ; in DX:AX = bytes, BL = owner; out CF,
                                ;     DX:AX = base / AX = 0/1/2
XMV_FREE    equ 4               ; in DX:AX = base, BL = owner; out CF
XMV_COPY    equ 6               ; in ES:SI = the XMC_ block below
XMV_RELINST equ 8               ; in AL = an instance slot being torn down
XMV_SIZE    equ 10

; --- the copy's parameter block (SPEC.md 41.12.2) ----------------------------
; BOTH ENDS TRAVEL AS 32-BIT LINEAR ADDRESSES, and that is forced rather than
; chosen: slot 0x01A8 takes the conventional end as ES:SI, and drv_call loads
; ES with KERNEL_SEG on its way into the image, so the caller's segment cannot
; survive the crossing. Folding it down here is arithmetic the resident
; xm_copy did internally anyway - and a block rather than registers because
; the ABI wants seven of them (two 32-bit ends, a count, a direction and an
; instance) and there are not seven to spare.
XMC_EXT     equ 0               ; dd: the extended end
XMC_CONV    equ 4               ; dd: the conventional end
XMC_LEN     equ 8               ; dw: bytes
XMC_DIR     equ 10              ; db: 0 = conventional -> extended, 1 = back
XMC_INST    equ 11              ; db: the requesting instance
XMC_SIZE    equ 12

; -----------------------------------------------------------------------------
; The overlay's row. SIXTEEN BYTES SHAPED LIKE A drv_tab ROW AND NOT IN IT -
; which is the whole point (SPEC.md 41.12): drv_load_at takes any such record,
; so the mount, drv_find, the one MEM_K_DRV claim, the read, drv_check and the
; shared failure tail are the loader that already exists rather than a second
; copy of it, while the settings bitmap, the Drivers page and DRV_MAX never
; hear about us.
;
; .text and not .bss, because -f bin zeroes nothing and this is read at boot.
; DRVR_CLASS is DRVC_OVL, which drv_check compares against the image's own
; header byte and drv_cls_idx refuses by range, so this can never take a
; publication slot even by mistake.
; -----------------------------------------------------------------------------
section .text

xm_fname:   db 'XMEM.DRV', 0
xm_title:   db 'XMS', 0         ; nothing draws it - there is no row and no
                                ; caption - but DRVR_TITLE is not allowed to
                                ; be 0 in a record the shared loader walks

xm_row:
    dw PKG_DISP, 0              ; DRVR_DISP, DRVR_SEG: together the far
                                ; pointer xm_dsp calls through
    dw xm_fname, xm_title       ; DRVR_FILE, DRVR_TITLE
    dw 0, 0                     ; DRVR_KB, DRVR_ENT
    db DRVC_OVL                 ; DRVR_CLASS - not a class the kernel knows
    db 0                        ; DRVR_WANT - xm_sniff is the only writer, and
                                ; no settings file can reach it
    db DRVE_OK                  ; DRVR_ERR
    db 0                        ; DRVR_PAD

xm_kb:      dw 0                ; THE GATE. KB the pool holds, and "the
                                ; overlay is attached" - see the file header.
                                ; Read by mem_sys_kb for SK_XMS (SPEC.md 41.6)

; -----------------------------------------------------------------------------
; xm_sniff - is there memory above 1MB? (SPEC.md 41.12.1)
;
; in:       [cpu_tier] published (cpu_detect has run)
; out:      nothing (all registers preserved). On a hit, xm_row's DRVR_WANT
;           is set and xm_boot will read the image
;
; THE PROBE IS EXACT, NOT HEURISTIC, and that is what separates it from
; drv_snd_sniff (SPEC.md 51.3.1). That one runs an OPL2 timer dance which is
; merely CORRELATED with the driver finding a card, and can be wrong in both
; directions - hence the SNDSNIFF= knob. This runs the same int 15h AH=88h the
; overlay sizes the store with, so a machine it skips is a machine that would
; have published 0 KB anyway. No false negatives by construction.
;
; The one false positive is a machine whose BIOS reports KB and whose A20 gate
; will not VERIFY. It costs a four-sector read, after which the overlay
; refuses at attach and the kernel frees the image - the same answer, and
; nobody is told (SPEC.md 41.12.4).
;
; ASKING AH=88h BEFORE THE GATE IS A STRICT IMPROVEMENT ON THE OLD BOOT PATH.
; xm_init used to test the VERIFIED A20 bit and only then issue AH=88h, so a
; 286 with exactly 1MB poked port 0x92, raced the keyboard controller for
; D1h/DFh and paid two bounded 65,536-poll timeouts to be told there was
; nothing up there. AH=88h needs no gate - it reports what the BIOS read out
; of CMOS - so the whole A20 sequence, including the one with the documented
; reboot hazard in it (SPEC.md 41.2), now runs only on a machine that has
; already said it has RAM to reach. On tier 0 this is one compare.
;
; .ovl, called from kmain, for drv_snd_sniff's own reason: drv_boot's first
; mount writes over the overlay, so a probe called from there would be running
; FAT. The overlay costs no RAM at all, so the sniff is free of both guards.
; -----------------------------------------------------------------------------
section .ovl

xm_sniff:
    push ax
    cmp byte [cpu_tier], CPU_8086
    je .out                     ; the target machine leaves here: an 8088 has
                                ; no A20 line and nothing above 0x0FFFFF
    mov ah, 0x88
    int 0x15                    ; AX = KB above 1MB
    jc .out                     ; no such service
    or ax, ax
    jz .out                     ; a 1MB AT
    mov byte [xm_row+DRVR_WANT], 1
.out:
    pop ax
    ret

section .cold                   ; xm_boot is COLD, and that is not a saving:
                                ; every routine it calls - drv_load_at,
                                ; drv_call, drv_release - is driver.inc's, and
                                ; driver.inc went cold (SPEC.md 2.6). From
                                ; .text those would be three thunks and three
                                ; far calls; from here they are near calls and
                                ; cost nothing at all. It runs once, at boot,
                                ; which is what the cold segment is for
; -----------------------------------------------------------------------------
; xm_boot - load and attach the overlay, if the sniff asked for it
;
; in:       nothing (kmain, after drv_boot, before the first paint)
; out:      nothing (all registers preserved). On success [xm_kb] is the pool
;           size and every cell below reaches the image
;
; drv_load_at does the mount, the claim, the read and drv_check and STOPS -
; an overlay has no class, so it can neither publish nor be sent DRVV_TIER or
; DRVV_READY, and its owner attaches it. That owner is this routine.
;
; A refusal frees the image with drv_release and says NOTHING (SPEC.md
; 41.12.4). Three things reach it - the A20 gate not verifying, a heap that
; cannot fund the image, a missing file on a hand-built floppy - and none of
; them is the user's doing or the user's to fix. The Task Manager's XMS line
; reads 0, which is what SPEC.md 41.8 already tells every package to branch
; on, and a System Info tool is where that diagnosis belongs.
;
; [xm_kb] IS WRITTEN LAST, after the table is staged and the feature bits are
; published - the snd_live ordering of SPEC.md 34.7. Every cell below tests
; that one word, so it must not be visible before what it gates.
; -----------------------------------------------------------------------------
xm_boot_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [xm_row+DRVR_WANT], 0
    je .out                     ; no store on this machine: not one sector
    mov bx, xm_row
    call drv_load_at            ; loaded and validated, NOT attached
    jc .out
    mov bp, [bx+DRVR_ENT]
    mov al, DRVV_ATTACH
    mov ah, XM_ABI_VER          ; ...which the image checks against its own
    call drv_call               ; out: SI = the XMV table, AX = KB, DL = the
    jc .refuse                  ; CPU_F_* that actually verified
    push ax
    push dx
    mov bx, xm_row              ; RE-ESTABLISHED, and this is not belt and
                                ; braces: the attach spends BX on the KB
                                ; figure and says so in its own header, so
                                ; the row pointer drv_call was given does not
                                ; survive the far call. Reading DRVR_SEG
                                ; through the stale BX staged ten bytes of
                                ; whatever the heap happened to hold, and the
                                ; only symptom was a service table of numbers
                                ; that could not be offsets - the KB figure,
                                ; the feature bits and the load itself were
                                ; all correct, so nothing else looked wrong.
                                ; The row is a link-time constant, so asking
                                ; for it again costs three bytes and removes
                                ; the cross-boundary assumption entirely
    mov es, [bx+DRVR_SEG]       ; stage the table out of its segment, the
    mov di, xm_svc              ; dsk_get_dir idiom: every later dispatch is
    mov cx, XMV_SIZE / 2        ; then a near read and one far call
.stage:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .stage
    pop dx
    pop ax
    mov [cpu_feat], dl          ; the A20/HMA/UNREAL bits, so cpu_info's AH
                                ; (slot 0x0188) keeps meaning what it always
                                ; meant. The image is their only writer now,
                                ; and on a machine where it never loads this
                                ; byte reads 0 - no gate verified, no HMA, no
                                ; unreal mode - which is true and is exactly
                                ; what tier 0 answers
    mov [xm_kb], ax             ; LAST, and deliberately so
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
.refuse:
    mov bx, xm_row              ; ...and here too, for the same reason. This
                                ; path worked BY LUCK on the machine it was
                                ; first exercised on: an 8088 refusal returns
                                ; from xm_attach before anything has spent BX,
                                ; so drv_release got a valid row and the claim
                                ; really was freed. A refusal further in - a
                                ; gate that will not verify, which is the one
                                ; case SPEC.md 41.12.1 says to expect - would
                                ; have handed drv_release a KB count and
                                ; leaked the image with DRVR_SEG still armed
    mov byte [bx+DRVR_ERR], DRVE_HW ; nothing READS this - the row is not in
                                ; drv_tab, so drv_status never sees it and
                                ; SPEC.md 41.12.4 says a failure is silent -
                                ; but a debugger reading the row should find
                                ; the truth rather than the DRVE_OK the load
                                ; stamped on its way past
    call drv_release            ; BX = the row; the claim goes back and
    jmp short .out              ; nothing was hooked

; --- the far entry kmain reaches xm_boot through (SPEC.md 2.6) --------------
xmf_xm_boot:        call xm_boot_x
                    retf

section .text

xm_boot:            call COLD_SEG:xmf_xm_boot
                    ret

; -----------------------------------------------------------------------------
; xm_dsp - dispatch one of the overlay's service procs
; in:  BP = the staged near offset, every other register = that proc's own
;      arguments
; out: whatever it returns
; clobbers: BP; DS and ES restored
;
; drv_svc_call with the row baked in rather than taken in BX, and that is the
; point: BX is an ARGUMENT here (BL carries the requesting instance to ALLOC
; and FREE), so a dispatcher that spent it would silently become the owner
; byte. drv_svc_call's own header records that exact class of bug happening
; twice in the sound ABI. Nothing but BP is touched.
;
; Callers must gate on [xm_kb] first: this one does not, because every caller
; needs its own tier-0 answer anyway and a shared refusal would have to invent
; a code that fits none of them.
; -----------------------------------------------------------------------------
xm_dsp:
    push ds
    push es
    push cs
    pop es                      ; ES = KERNEL_SEG: what the image reads the
    mov ds, [es:xm_row+DRVR_SEG]    ; things it was handed through
    call far [es:xm_row+DRVR_DISP]
    pop es
    pop ds
    ret

; -----------------------------------------------------------------------------
; xm_ownbl - who is asking? (the SPEC.md 34.3 grant-stamping rule, reused)
;
; in:       -
; out:      BL = the instance slot to stamp: the dispatched callback's stamp
;           if there is one, else the running task's. 0xFF = the kernel.
; clobbers: BL (flags)
;
; Stamped HERE and not in the image, for osapi_snd_fm's documented reason:
; snd_req_inst reads [snd_inst] and the running task's T_INST, both of which
; are kernel state, and a loaded image that had to ask would need an API slot
; of its own to do it. BL rather than AL because AX is the low half of a
; 32-bit argument to two of the three callers.
;
; Extended-memory blocks are stamped and force-freed at teardown exactly like
; a SPEC.md 34.6 sound grant, so they use the same answer to the same question
; rather than a second implementation that could drift from it.
; -----------------------------------------------------------------------------
xm_ownbl:
    push dx
    call snd_req_inst           ; DH = the requesting instance
    mov bl, dh
    pop dx
    ret

; -----------------------------------------------------------------------------
; xm_caps - what is left up there? (osapi slot 0x0190's body)
;
; in:       -
; out:      AX = KB the pool can still hand out (0 = none, and xm_alloc /
;           xm_free / xm_copy will all refuse), DX:CX = the pool's 32-bit
;           linear base, BL = free block-table entries of XM_MAX_BLKS
; clobbers: nothing else (BH, SI, DI and the rest preserved; flags)
;
; This is the figure a package branches on. NEVER the tier byte: a 386 whose
; A20 gate did not verify answers 0 here just as an 8088 does, and so does a
; machine the overlay never loaded on (SPEC.md 41.8).
;
; The no-store answer reports every entry of the block table free, which is
; what tier 0 reports - a table with nothing in it, above a pool with nothing
; in it. A package that reads BL and not AX was already broken on the target
; machine.
; -----------------------------------------------------------------------------
xm_caps:
    cmp word [xm_kb], 0
    je .none
    push bp
    mov bp, [xm_svc+XMV_CAPS]
    call xm_dsp
    pop bp
    ret
.none:
    xor ax, ax
    xor dx, dx
    xor cx, cx
    mov bl, XM_MAX_BLKS
    ret

; -----------------------------------------------------------------------------
; xm_alloc - first-fit a run of extended memory (osapi slot 0x0198's body)
;
; in:       DX:AX = bytes wanted (rounded up to 1KB)
; out:      CF = 0 and DX:AX = the block's 32-bit linear base;
;           CF = 1 and AX = 0 no store / 1 no free block-table entry /
;           2 no contiguous run that big
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
xm_alloc:
    cmp word [xm_kb], 0
    je .none
    push bp
    push bx
    call xm_ownbl               ; BL = who is asking
    mov bp, [xm_svc+XMV_ALLOC]
    call xm_dsp
    pop bx                      ; pop leaves CF alone, and BX is no output
    pop bp
    ret
.none:
    xor ax, ax
    stc
    ret

; -----------------------------------------------------------------------------
; xm_free - give a block back (osapi slot 0x01A0's body)
;
; in:       DX:AX = a base this caller owns
; out:      CF = 0 freed and merged with the adjacent free runs;
;           CF = 1 not a base, or not the caller's
; clobbers: nothing (DX:AX preserved; flags)
; -----------------------------------------------------------------------------
xm_free:
    cmp word [xm_kb], 0
    je .none
    push bp
    push bx
    call xm_ownbl
    mov bp, [xm_svc+XMV_FREE]
    call xm_dsp
    pop bx
    pop bp
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; xm_copy - move bytes between conventional and extended memory
;           (osapi slot 0x01A8's body, SPEC.md 41.5)
;
; in:       ES:SI = the conventional end, DX:AX = the extended end (32-bit
;           linear), CX = bytes (even, <= XM_MAX_COPY), DI = 0 conventional
;           -> extended, 1 the other way
; out:      CF = 0 done; CF = 1 and AX = 1 no store (or the transport
;           refused) / 2 the range is not inside a block this caller owns /
;           3 bad length
; clobbers: nothing else (ES restored per SPEC.md 1; flags)
;
; UI-TASK CONTEXT (SPEC.md 41.8): the entry proc or any window callback, and
; THE GFX LOCK MAY BE HELD - a callback moving a buffer to or from the store
; is the case that permission was written for. This touches no VRAM and takes
; no drawing lock of its own. NOT from an ISR and NOT from a worker task: on
; tier 1 the image enters int 15h AH=87h (a BIOS call, kept on the UI task per
; SPEC.md 7; some 286 BIOSes implement it by a transparent CPU reset), and the
; block below is shared scratch serialized by sch_lock against a switch, not
; against re-entry.
;
; [sch_lock] IS RAISED HERE and no longer inside the transports, because a
; loaded image cannot reach it - there is no API slot, and drivers/debug's
; header records what that costs it. dsk_xfer raises it before dispatching
; DSV_BLK for the same reason and hands a block driver a locked scheduler;
; this is that, and it is strictly wider than the two windows it replaces.
;
; The length is NOT checked here even though it could be: the image owns the
; error codes, and a second opinion about what is too long is a second place
; to disagree.
; -----------------------------------------------------------------------------
xm_copy:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp word [xm_kb], 0
    je .e1
    mov [xm_cblk+XMC_EXT], ax   ; the extended end, as handed to us
    mov [xm_cblk+XMC_EXT+2], dx
    mov [xm_cblk+XMC_LEN], cx
    mov al, 0
    or di, di
    jz .dir
    mov al, 1
.dir:
    mov [xm_cblk+XMC_DIR], al
    mov ax, es                  ; the conventional end, as linear ES*16 + SI
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, si
    adc dx, 0
    mov [xm_cblk+XMC_CONV], ax
    mov [xm_cblk+XMC_CONV+2], dx
    call xm_ownbl
    mov [xm_cblk+XMC_INST], bl
    mov si, xm_cblk             ; xm_dsp points ES at KERNEL_SEG itself
    inc byte [sch_lock]
    mov bp, [xm_svc+XMV_COPY]
    call xm_dsp
    dec byte [sch_lock]         ; dec leaves CF alone
    jmp short .out
.e1:
    mov ax, 1
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; xm_release_rec - force-free every block an instance holds, BY RECORD
;
; in:       DI = instance record ptr
; out:      nothing
; clobbers: nothing (flags)
;
; snd_release_rec's shape, byte for byte, and for the same reason: the
; SPEC.md 29.4 teardown sites hold the record, the release wants the slot, and
; the conversion belongs beside the release rather than at three call sites.
;
; Called from the three sites snd_release_rec is called from, immediately
; beside it: app_close_win's synchronous leg, inst_task_die, and
; inst_pkg_alive's winless leg. `git grep snd_release_rec kernel/instance.inc`
; is still the list, and a fourth teardown site owes both.
;
; It was called from none of them for a year: the #51 integration merge
; dropped all three and the comment describing them survived the code, which
; is docs/UPSTREAM.md's hazard in its purest form - the merge assembled,
; booted and said nothing, because a call that is simply absent breaks no
; build. tests/xmtest + tests/xmcheck.py is the gate that makes the next such
; loss loud, and it is verified to FAIL with the three calls removed.
;
; ONE TRAP, and it is not this routine's: a block claimed in a package's ENTRY
; PROC is stamped 0xFF (the kernel), not the instance, because inst_caller
; answers with the running task's T_INST when no callback is being dispatched
; - and the loader far-calls the entry on the UI task. Such a block matches no
; slot and this will never free it. Claim from a callback (SPEC.md 41.5.1).
; -----------------------------------------------------------------------------
xm_release_rec:
    cmp word [xm_kb], 0
    je .out                     ; no overlay, so no table and nothing to free
    push ax
    push bx
    push cx
    push bp
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl                  ; AL = the instance slot
    mov bp, [xm_svc+XMV_RELINST]
    call xm_dsp
    pop bp
    pop cx
    pop bx
    pop ax
.out:
    ret

section .bss

xm_svc:     resb XMV_SIZE       ; the overlay's table, staged at attach
xm_cblk:    resb XMC_SIZE       ; the copy's parameter block, read by the
                                ; image through ES = KERNEL_SEG

%else
; =============================================================================
; kern_small: the four slots keep their NUMBERS and their CONTRACTS
; (SPEC.md 20.8 rule 4, SPEC.md 41.11, docs/KERN-SPLIT-PLAN.md 3)
;
; The tables must stay the same length in both builds, or a package built
; against one kernel far-calls into whatever follows the other's table - which
; here is the debug registry, not code. It assembles, it loads, it launches,
; and it dies somewhere unrelated. So a kern_big-only slot is present here as
; a cell pointing at a body that answers, exactly as retired slot 0x01E8 is.
;
; THE ANSWERS ARE TIER 0's, BYTE FOR BYTE, and that is the safety argument
; rather than a convenience. The target machine is an 8088, so a kern_big
; kernel on the machine this project is calibrated against ALREADY answers
; precisely this - and since SPEC.md 41.12 so does any kern_big machine the
; overlay did not load on. Two things follow. No package can tell a kern_small
; kernel from the 8088 it was written for, and every shipped package is
; already exercised against this path on real hardware, which is a stronger
; statement than any harness here could make about a refusal invented for this
; build.
;
; It is deliberately NOT one shared refuser, because the codes differ and
; callers read them (SPEC.md 41.5): xm_alloc's "no store" is AX = 0, xm_copy's
; is AX = 1, and xm_free answers CF alone with DX:AX preserved.
;
; Cost to kern_small: these four bodies and nothing else. No row, no sniff, no
; block table, no copy scratch, and nothing on the boot path.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; xm_caps - what is left up there? (osapi slot 0x0190's body). Nothing.
;
; out:      AX = 0 (no store), DX:CX = 0 (the pool base), BL = XM_MAX_BLKS
; clobbers: nothing else (BH is the caller's and stays that way; flags)
;
; AX = 0 is the figure a package branches on, and there is no CF in this
; contract: it always answers. BL reports every entry of the block table free,
; which is what tier 0 reports - a table with nothing in it, above a pool with
; nothing in it. A package that reads BL and not AX was already broken on the
; target machine.
; -----------------------------------------------------------------------------
xm_caps:
    xor ax, ax
    xor dx, dx
    xor cx, cx
    mov bl, XM_MAX_BLKS
    ret

; -----------------------------------------------------------------------------
; xm_alloc - first-fit a run of extended memory (slot 0x0198). There is none.
;
; in:       DX:AX = bytes wanted
; out:      CF = 1 and AX = 0 - "no store", xm_alloc's .nostore answer
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
xm_alloc:
    xor ax, ax
    stc
    ret

; -----------------------------------------------------------------------------
; xm_free - give a block back (slot 0x01A0). You cannot hold one.
;
; in:       DX:AX = a base this caller owns
; out:      CF = 1 - not a base. DX:AX preserved, per the contract
; clobbers: nothing (flags)
; -----------------------------------------------------------------------------
xm_free:
    stc
    ret

; -----------------------------------------------------------------------------
; xm_copy - move bytes to or from the store (slot 0x01A8). There is nowhere.
;
; in:       ES:SI, DX:AX, CX, DI as SPEC.md 41.8 documents them
; out:      CF = 1 and AX = 1 - "no store", xm_copy's .e1 answer
; clobbers: nothing else (ES untouched, so restored by construction; flags)
; -----------------------------------------------------------------------------
xm_copy:
    mov ax, 1
    stc
    ret

; -----------------------------------------------------------------------------
; xm_release_rec - free every block an instance holds. It holds none.
;
; in:       DI = instance record ptr
; out:      nothing
; clobbers: nothing (flags)
;
; NOT an API slot - this one is internal, called from SPEC.md 29.4's three
; teardown sites. It is a `ret` here rather than three %ifdefs in
; instance.inc, and that is the point: THE TEARDOWN PATH DOES NOT KNOW WHICH
; BUILD IT IS. instance.inc is not a file about extended memory, and a
; conditional there is one a fourth teardown site could forget - where a
; missing `call` fails silently, which is exactly how these three were lost in
; the first place (SPEC.md 41.5).
; -----------------------------------------------------------------------------
xm_release_rec:
    ret

%endif  ; KERN_BIG

section .text
