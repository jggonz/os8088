; =============================================================================
; os8088 - xmem.inc
;
; Memory above 1MB (SPEC.md 41.4-41.5): sizing, a small first-fit allocator
; over it, and one copy routine with ONE ABI over TWO transports.
;
; On tier 0 - the target machine - this module publishes zero KB and does
; nothing else, ever. xm_init stores its zeroes and returns before the first
; BIOS call; every other entry point refuses on [xm_kb] = 0.
;
; THE HARD CEILING, because it is the rule most likely to be violated later:
; real mode forms an address as seg<<4 + off, so the highest byte any CS:IP
; can name is 0x10FFEF. Everything this module hands out is a DATA STORE
; ONLY - no code, no jump table, no callback, no relocated package image, on
; any tier. Unreal mode does not move that line by one byte: it widens a
; DATA segment's limit and touches neither the width of CS nor of IP.
;
; Unreal mode uses FS AND GS, and that choice is the whole safety argument
; (SPEC.md 41.4). A segment register's hidden descriptor cache keeps its
; limit until the register is RELOADED; one plain real-mode `mov fs, ax`
; anywhere would reset it to 0xFFFF and the 4GB window would be gone -
; silently, with no fault, as a copy that wraps inside 64KB. A
; case-insensitive search for FS or GS across every kernel module, every
; shipped package and boot/boot.asm finds nothing, so "only xmem.inc writes
; FS or GS" is a rule the tree can actually keep. ES and DS were never
; candidates: ES is the most contended register in the kernel (cursor
; save-unders, SAVE_SEG, FAT_SEG, VIEW_SEG, SND_SEG, int 13h ES:BX) and DS
; is the kernel's own segment - either would be reloaded with a real-mode
; value within microseconds and the limit would be gone before the first
; copy.
;
; BIOS re-arm, and it is not optional either. The kernel enters the BIOS
; constantly after boot - int 16h AH=01h on EVERY pass of the UI loop, plus
; int 13h/10h/1Ah/12h. Nothing in os8088 touches FS or GS, but BIOS code is
; not os8088 code and a 386-aware BIOS may load them for its own purposes
; and hand them back with real-mode limits. So the tier-2 copy re-arms from
; the resident GDT - and, because the BIOS is re-entered by INTERRUPTS and
; not only by kernel calls, it re-arms inside the same cli window as the
; accesses that depend on it. That is why the copy is chunked (xm_ucopy).
;
; Every 386 instruction in this file sits inside a scoped `cpu 386` island
; closed by `cpu 8086`, AND behind a run-time [cpu_tier] test. The island is
; assembly-time permission only (SPEC.md 41.9 rule 2): an island reached on a
; 286 is an illegal-opcode trap on a machine with no handler for it.
;
; Context (SPEC.md 20.3/41.9 rule 4, binding): the allocator and the copy are
; UI-task / window-callback only. On tier 1 xm_copy goes through int 15h
; AH=87h, and some 286 BIOSes implement that call by RESETTING THE CPU to
; get back to real mode. A worker task calling it - or any caller holding the
; gfx lock while it runs - is a machine that stops.
; =============================================================================

XM_SEL_FLAT     equ 0x08        ; GDT entry 1: base 0, limit 4GB, data r/w
XM_MAX_COPY     equ 32768       ; bytes per xm_copy - a SCHEDULER rule, not a
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
                                ; not 0, because 0 is a real instance slot
                                ; and snd_req_inst - the tree's existing
                                ; "who is asking" answer, which xm_owner
                                ; reuses - already spells the kernel 0xFF.

xm_kb           dw 0            ; KB the pool may hand out. PUBLISHED LAST by
                                ; xm_init (the snd_live ordering of SPEC.md
                                ; 34.7): it is the single gate every entry
                                ; point here tests, so it must not be visible
                                ; before the base and the table behind it.
                                ; Initialized data, NOT .bss - nothing zeroes
                                ; .bss at boot.
xm_base_lo      dw 0            ; the pool's 32-bit linear base: 0x00100000,
xm_base_hi      dw 0            ; or 0x00110000 when the HMA was claimed

; The GDT unreal mode loads. It lives in .text because far code keeps
; DS = KERNEL_SEG (SPEC.md 33 rule 2) and because its contents are not zero;
; the lgdt base is computed at run time from DS, so nothing here depends on
; KERNEL_SEG being spelled out twice.
;
; Two entries, not three: there is no code descriptor because THERE IS NO FAR
; JUMP AND CS IS NEVER RELOADED. The whole point of the sequence is the
; hidden descriptor caches of FS and GS; CS must keep its real-mode base
; throughout, since the instruction after `mov cr0` is the next one in the
; same segment.
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

; -----------------------------------------------------------------------------
; xm_init - size the store, set the pool base, arm unreal mode (SPEC.md 41.5)
;
; in:       [cpu_tier] and [cpu_feat] published (cpu_detect and
;           cpu_a20_enable have run)
; out:      [xm_kb], [xm_base_lo/hi], the block table zeroed, CPU_F_HMA and
;           CPU_F_UNREAL settled
; clobbers: nothing (flags)
;
; Called from kmain after far_init (this writes .bss, and the .fartext blob
; sits on top of .bss until far_init has moved it) and before sched_init
; (the last moment at which no kernel ISR is installed). Runs on task 0, so
; the int 15h call honours SPEC.md 7's only-the-UI-task-calls-BIOS rule, and
; exactly once.
;
; It HAND-ZEROES the block table: nothing clears .bss at boot, and an
; uninitialised first-fit table would hand out garbage 32-bit bases.
;
; The A20 test is not belt-and-braces. SPEC.md 41.2 makes the verified bit -
; not "we wrote to the gate" - the thing every consumer keys off, and a 386
; whose gate never answered has no reachable store no matter what AH=88h
; says.
; -----------------------------------------------------------------------------
xm_init:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    push ds
    pop es                      ; the table is kernel .bss, reached through DS
    mov di, xm_tab
    mov cx, (XM_MAX_BLKS * XM_BLKSZ) / 2
    xor ax, ax
    cld
    rep stosw
    mov [xm_kb], ax             ; the safe answer, in place before any probe
    mov [xm_base_lo], ax
    mov [xm_base_hi], ax
    cmp byte [cpu_tier], CPU_8086
    je .done                    ; tier 0: no A20, no HMA, no store. Done.
    test byte [cpu_feat], CPU_F_A20
    jz .done                    ; the gate never verified: nothing up there
                                ; is reachable, whatever the CPU is
    mov ah, 0x88
    int 0x15                    ; AX = KB above 1MB
    jc .done                    ; no such service
    or ax, ax
    jz .done                    ; a 1MB AT: zero bytes above 0x0FFFFF
    mov bx, ax                  ; BX = the KB figure
    call cpu_hma_claim          ; AX is still that figure - the claim needs it
    mov dx, 0x0010              ; pool base high word: linear 0x00100000
    jc .nohma
    sub bx, XM_HMA_KB           ; the HMA is the first 64KB of exactly this
    mov dx, 0x0011              ; RAM, so the pool starts above it (SPEC.md
.nohma:                         ; 2.4) - one of the two owns those 64KB,
    or bx, bx                   ; never both
    jz .done                    ; the HMA took all there was
    cmp byte [cpu_tier], CPU_386
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
    cmp byte [cpu_tier], CPU_386        ; 64KB boundary
    jne .publish
    call xm_arm                 ; unreal mode, once, here (SPEC.md 41.4)
.publish:
    mov [xm_kb], bx             ; LAST, and deliberately so
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; xm_arm - enter unreal mode: give FS and GS a 4GB limit (SPEC.md 41.4)
;
; in:       -
; out:      FS and GS cached with a base-0, limit-4GB data descriptor;
;           CPU_F_UNREAL set. A no-op on any tier below 2.
; clobbers: nothing (flags)
;
; Called once from xm_init, and then once per XM_UCHUNK-byte chunk from
; xm_ucopy - inside that chunk's cli window, so that nothing can enter the
; BIOS between the arm and the accesses that depend on it (see the file
; header). It is neither an ISR path nor a hot path, so a few microseconds
; per kilobyte costs nothing measurable, and the alternative is a copy that
; wraps at 64KB and corrupts conventional memory without a fault. Calling it
; repeatedly is a non-event: the transition is idempotent and preserves
; every register and the flags.
;
; The whole transition runs inside one pushf/cli ... popf with NMI masked
; (CMOS index port 0x70 bit 7) across it: a real-mode IVT is meaningless
; while CR0.PE is set, so an interrupt taken in the window is a triple
; fault. Nothing in the kernel touches 0x70 outside clock.inc's int 1Ah
; calls, so there is no conflict; the read of 0x71 after each write to 0x70
; is the classic requirement to leave the RTC's index/data pair settled.
;
; There is no far jump. CS is never reloaded and keeps its real-mode base,
; which is what makes the instruction after `mov cr0` reachable at all.
; -----------------------------------------------------------------------------
xm_arm:
    cmp byte [cpu_tier], CPU_386
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
    mov dx, ax                  ; DS is KERNEL_SEG here and in far code alike
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, xm_gdt
    adc dx, 0
    mov [xm_gdtr+2], ax
    mov [xm_gdtr+4], dx
cpu 386                         ; ---- 386-only island, tier 2 only --------
    lgdt [xm_gdtr]              ; (16-bit operand size loads 24 base bits;
                                ; the table is inside the kernel segment,
                                ; well under 16MB, so that is exact)
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
    or byte [cpu_feat], CPU_F_UNREAL
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; xm_owner - who is asking? (the SPEC.md 34.3 grant-stamping rule, reused)
;
; in:       -
; out:      AL = the instance slot to stamp: the dispatched callback's stamp
;           if there is one, else the running task's. XM_OWN_KERN = kernel.
; clobbers: nothing else (flags)
;
; Extended-memory blocks are stamped and force-freed at teardown exactly like
; a SPEC.md 34.6 sound grant, so they use the same answer to the same
; question rather than a second implementation that could drift from it.
; -----------------------------------------------------------------------------
xm_owner:
    push dx
    call snd_req_inst           ; DH = the requesting instance
    mov al, dh
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
; A20 gate did not verify answers 0 here just as an 8088 does (SPEC.md 41.8).
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
; xm_alloc - first-fit a run of extended memory (osapi slot 0x0198's body)
;
; in:       DX:AX = bytes wanted (rounded up to 1KB)
; out:      CF = 0 and DX:AX = the block's 32-bit linear base;
;           CF = 1 and AX = 0 no store / 1 no free block-table entry /
;           2 no contiguous run that big
; clobbers: nothing else (flags)
;
; XM_MAX_BLKS entries, 1KB granularity, stamped with the calling instance.
; Deliberately small: extended memory here is a bulk store for a handful of
; large claims - a render buffer, a captured sample, a package's own heap -
; not a malloc. A caller that wants many small pieces takes one block and
; subdivides it itself, exactly as a SPEC.md 34.6 sound grant is subdivided.
;
; Free space is IMPLICIT - it is whatever no in-use entry covers - so the
; search is: start the candidate at offset 0, walk the table, and every time
; the candidate overlaps a block push it past that block's end and start
; over. Each restart moves the candidate strictly forward past at least one
; block, so it terminates in at most XM_MAX_BLKS restarts, and a freed block
; merges with its neighbours for free because nothing records the gaps.
; -----------------------------------------------------------------------------
xm_alloc:
    push bx
    push cx
    push si
    push di
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
    call xm_owner               ; outside the window: it is a plain read
    pushf
    cli                         ; the publish is ONE store as far as any other
    mov [di+XB_OFF], si         ; task is concerned (the snd_release_inst
    mov [di+XB_KB], cx          ; shape). Ungated, a teardown running between
    mov [di+XB_OWN], al         ; the XB_KB and XB_OWN stores sees a live
    popf                        ; entry carrying a STALE owner byte and frees
                                ; the block out from under us - and the next
                                ; xm_alloc hands the same range to a second
                                ; package.
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
; xm_free - give a block back (osapi slot 0x01A0's body)
;
; in:       DX:AX = a base this caller owns
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
    call xm_owner               ; AL = the caller
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
; xm_release_inst - force-free every block an instance holds
;
; in:       AL = instance slot
; out:      nothing
; clobbers: nothing (flags)
;
; The SPEC.md 41.5 teardown leg, and the exact shape of snd_release_inst -
; INCLUDING ITS pushf/cli, which is not decoration. This runs on the dying
; task (inst_task_die, gfx lock NOT held) while the UI task may be inside
; xm_alloc or xm_free, and a scan interleaved with either mutator reads a
; half-published entry. Call it from both SPEC.md 29.4 paths so a package
; that exits without freeing does not strand its claim for the session. Safe
; on a slot that holds nothing, and safe on tier 0 (the table is all zeroes).
; -----------------------------------------------------------------------------
xm_release_inst:
    push cx
    push si
    pushf
    cli
    mov cx, XM_MAX_BLKS
    mov si, xm_tab
.scan:
    cmp word [si+XB_KB], 0
    je .next
    cmp [si+XB_OWN], al
    jne .next
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
; xm_release_rec - xm_release_inst addressed by RECORD, not by slot
;
; in:       DI = instance record ptr
; out:      nothing
; clobbers: nothing (flags)
;
; snd_release_rec's shape, byte for byte, and for the same reason: the
; SPEC.md 29.4 teardown sites hold the record, the release wants the slot,
; and the conversion belongs beside the release rather than at three call
; sites. Called from all three of them - app_close_win's synchronous leg,
; inst_task_die, and inst_pkg_alive's winless leg - immediately beside the
; sound release, so a package that exits without freeing does not strand its
; claim for the session.
; -----------------------------------------------------------------------------
xm_release_rec:
    push ax
    push cx
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    call xm_release_inst        ; in: AL = instance slot
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; xm_chk - does a range lie wholly inside a block this caller owns?
;
; in:       DX:AX = 32-bit linear extended address, CX = bytes
; out:      CF = 0 yes, CF = 1 no
; clobbers: nothing (flags)
;
; This check is the only thing between a package and every byte of the
; machine's RAM, which is why xm_copy runs it BEFORE the tier branch rather
; than inside either arm of it (SPEC.md 41.5).
;
; It works in KB, because every block boundary is KB-aligned: the range is
; inside the block iff the block starts at or below the KB the range starts
; in, and ends at or above the KB the range's last byte ends in. That turns
; a 32-bit interval test into two 16-bit compares.
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
    call xm_owner               ; AL = the caller
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
; UI-TASK / WINDOW-CALLBACK CONTEXT ONLY (SPEC.md 41.9 rule 4): never from an
; ISR, never under the gfx lock, never on a hot path. On tier 1 this enters
; int 15h AH=87h, and some 286 BIOSes implement that by resetting the CPU.
;
; One ABI, two transports, and the caller cannot tell which ran - that is
; what makes the slot safe to hand to an 8086-only package. Both ends are
; converted to 32-bit linear addresses here so neither transport has to care
; which direction it was asked for.
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
    or cx, cx
    jz .e3
    test cx, 1
    jnz .e3                     ; AH=87h counts words: even lengths only
    cmp cx, XM_MAX_COPY
    ja .e3
    call xm_chk                 ; bounds FIRST, before the tier branch
    jc .e2
    mov [xm_ext], ax            ; stash the extended end
    mov [xm_ext+2], dx
    mov bp, cx                  ; BP = the byte count; CL is about to be a
                                ; shift count
    mov ax, es                  ; the conventional end, as linear ES*16 + SI
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, si
    adc dx, 0
    or di, di
    jnz .inbound
    mov [xm_src], ax            ; conventional -> extended
    mov [xm_src+2], dx
    mov ax, [xm_ext]
    mov dx, [xm_ext+2]
    mov [xm_dst], ax
    mov [xm_dst+2], dx
    jmp short .go
.inbound:
    mov [xm_dst], ax            ; extended -> conventional
    mov [xm_dst+2], dx
    mov ax, [xm_ext]
    mov dx, [xm_ext+2]
    mov [xm_src], ax
    mov [xm_src+2], dx
.go:
    cmp byte [cpu_tier], CPU_386
    je .unreal
    call xm_bios                ; tier 1
    jc .e1
    jmp short .ok
.unreal:
    call xm_ucopy               ; tier 2
    jc .e1
.ok:
    xor ax, ax
    pop es
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
    pop es
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
; and not `rep movsd` for a structural reason: MOVS takes its destination
; from ES:(E)DI and ES is the ONE segment a string move cannot override -
; so a flat destination would mean widening ES, which SPEC.md 41.4 forbids
; precisely because ES is reloaded constantly. Two 32-bit moves per dword
; cost a few clocks more and keep the FS/GS rule intact.
;
; THE COPY RUNS IN XM_UCHUNK-BYTE CHUNKS, AND EACH CHUNK RE-ARMS INSIDE ITS
; OWN pushf/cli ... popf. That structure is the whole correctness argument,
; and the obvious cheaper shapes are both wrong:
;
;   - Re-arming once and then copying with IF=1 does not work. sch_isr chains
;     `call far [sch_old08]` into the BIOS timer handler on EVERY tick, before
;     it so much as looks at [sch_lock], and IRQ1 reaches the BIOS keyboard
;     handler the same way. A 386-aware BIOS that loads FS or GS for its own
;     use hands them back with real-mode limits - the exact hazard this
;     module exists to defend against - and the rest of the copy then wraps
;     inside 64KB with no fault, or takes a #GP into an IVT slot the kernel
;     never fills.
;   - [sch_lock] does not close that hole either. It stops TASK SWITCHES, not
;     interrupts, so it cannot protect a loop from an ISR. It is still raised
;     here, before the first arm rather than after it, for the two things it
;     genuinely does: it keeps [xm_src]/[xm_dst] - shared .bss scratch - to
;     one copy at a time, and it stops a switch landing between two chunks.
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
    cmp byte [cpu_tier], CPU_386
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
    inc byte [sch_lock]         ; raised BEFORE the first arm, not after it
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
    dec byte [sch_lock]         ; resume switching (dec leaves CF alone)
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
; only source and destination; the BIOS builds the other four itself, which
; is why the whole block is zeroed first and the reserved bytes are left
; alone. CX is a WORD count, and that is where xm_copy's even-length rule
; comes from.
;
; The descriptor layout, which is the most error-prone part of the whole
; thing:
;     +0  word  limit = length - 1
;     +2  word  base 15..0
;     +4  byte  base 23..16
;     +5  byte  access: 0x93 = present, ring 0, data, writable
;     +6  byte  reserved - MUST be 0 (a 386 reads it as limit 19..16/flags)
;     +7  byte  base 31..24 - MUST be 0 on a 286
; so the bases are 24-bit, which is why xm_init clamps the tier-1 pool to
; 16MB and why both high words are re-checked here before the call.
;
; The block itself must live in conventional memory (it is kernel .bss) and
; is addressed ES:SI, so ES is pointed at DS for the call and restored.
; [sch_lock] is raised across it for the int 13h reason: the BIOS runs this
; with interrupts off and a switch on the way out would be taken with the
; machine's state half-restored.
; -----------------------------------------------------------------------------
xm_bios:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es                      ; the block is kernel .bss
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
    inc byte [sch_lock]
    mov ah, 0x87
    int 0x15
    dec byte [sch_lock]         ; dec leaves CF alone
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

section .bss

xm_tab:     resb XM_MAX_BLKS * XM_BLKSZ ; the block table, hand-zeroed by
                                        ; xm_init - nothing clears .bss
xm_ext:     resd 1                      ; xm_copy scratch: the extended end
xm_src:     resd 1                      ; and the two ends, ordered, as
xm_dst:     resd 1                      ; 32-bit linear addresses
xm_x87:     resb 48                     ; int 15h AH=87h's descriptor block -
                                        ; it must itself be in conventional
                                        ; memory, and this is (SPEC.md 41.5)

section .text
