; =============================================================================
; apps/os88la.inc - LOADALL on a 286: memory above 1 MB IN PLACE
; (docs/plans/LOADALL-PLAN.md, SPEC.md 41.13)
;
; THE 286'S LOADALL (0F 05) loads every register of the CPU from a 102-byte
; table at physical 0x800 - including each segment's HIDDEN descriptor cache,
; a 24-bit base the CPU addresses through. In real mode a cache is normally
; `selector x 16`; loaded by LOADALL with base 0x2A0000, `DS:SI` reaches 2.6
; MB with no mode switch and no reset, until the next load of DS.
;
; WRITTEN ONCE, ASSEMBLED BY EACH CALLER (LOADALL-PLAN 6): XMEM.DRV's tier-1
; copy, VIDDISK's L rows, and the hot loops that read a window in place.
; It is VIDDISK L's borrow as it ran on the owner's Victory 286 (2026-10-14,
; docs/reports/LOADALL-VICTORY286-2026-10-14.md), lifted with one change: the
; CS and SS caches are built at EVERY borrow from the running CS and SS, not
; at set-up, because a driver image can move (docs/plans/HEAP-UNPIN-PLAN.md)
; and a caller running from the HMA has a CS no constant names (6.1).
;
; TIER 1 ONLY, and behind la_probe. A 386 executes 0F 05 as an invalid
; opcode (its LOADALL is 0F 07, another table); `[cpu_tier]` = 1 is decided
; by FLAGS' top nibble and IS a 286. la_capture, la_probe and la_borrow emit
; 286 instructions by hand (SPEC.md 1 rule 1's written exception, 41.13):
; nothing here may be reached on an 8086 or a 386.
;
; PHYSICAL 0x800 IS KERNEL_SEG:0x0200 - THE PUBLISHED API TABLE (LOADALL-PLAN
; 2). Nothing can be moved out of the way, so every LOADALL is a BORROW inside
; one pushf / cli ... popf: the 102 bytes saved, the table written, LOADALL,
; the work, the bytes put back. Nothing executes or reads those cells while
; interrupts are off - no ISR in the tree far-calls a cell.
;
; THE HIDDEN BASE ENDS AT ANY SEGMENT LOAD (`mov ds`, `pop ds`, `lds`), and
; every ISR pushes and pops DS and ES - so between la_borrow and la_restore
; interrupts stay off, nothing is called that loads a segment, and the
; caller's own data is reached through CS: overrides.
;
; A20 MUST BE OPEN: the base goes out on the bus as it is and a shut gate
; aliases it 1 MB down, without a fault. That is the caller's check, made
; before the borrow (XMEM.DRV probes it per call).
;
; THE CALLER DEFINES THE DATA, wherever suits it - LA_DATA below lays it out
; for a caller with ordinary bss, XMEM.DRV equs it into its scratch:
;   la_save   LA_SZ bytes  the API cells while they are borrowed
;   la_msw    word         smsw, at la_capture
;   la_idt    6 bytes      sidt, as the table holds it: base 24, 0, limit
;   la_gdt    6 bytes      sgdt, the same
;   la_dsc    dword        DS's cache base, 24-bit linear, for the next borrow
;   la_esc    dword        ...ES's
;   la_dsv    word         DS's VISIBLE selector meanwhile (see la_borrow)
;   la_esv    word         ...ES's
;   la_csacc  byte         the CS cache's access byte: 93h (VIDDISK L ran it
;                          on an AMD 286; 9Bh is its M key)
;   la_fault  byte         la_ud: 1 = the instruction trapped (la_probe, or
;                          a caller hooking INT 6 itself, LA_NOPROBE)
; =============================================================================

LA_TAB      equ 0x0080              ; the table's segment: physical 0x800
LA_SZ       equ 0x66                ; 102 bytes
LT_MSW      equ 0x06                ; (86Box's opLOADALL, read 2026-10-14,
LT_FL       equ 0x18                ; which agrees with the published layout)
LT_IP       equ 0x1A
LT_DS       equ 0x1E
LT_SS       equ 0x20
LT_CS       equ 0x22
LT_ES       equ 0x24
LT_SP       equ 0x2C
LT_ESC      equ 0x36                ; the caches: 3 bytes base, access, limit
LT_CSC      equ 0x3C
LT_SSC      equ 0x42
LT_DSC      equ 0x48
LT_GDT      equ 0x4E
LT_IDT      equ 0x5A

%macro LA_DATA 0
la_save:    times LA_SZ db 0
la_msw:     dw 0
la_idt:     times 6 db 0
la_gdt:     times 6 db 0
la_dsc:     dd 0
la_esc:     dd 0
la_dsv:     dw 0
la_esv:     dw 0
la_csacc:   db 0x93
la_fault:   db 0
%endmacro

; -----------------------------------------------------------------------------
; la_capture - the MSW, IDTR and GDTR as the CPU has them, into la_msw,
; la_idt and la_gdt: written into every table as they already are, so LOADALL
; changes nothing it is not asked to. TIER 1 ONLY. DS = the caller's data.
; Preserves all but flags
; -----------------------------------------------------------------------------
la_capture:
    push ax
    db 0x0F, 0x01, 0xE0             ; smsw ax
    mov [la_msw], ax
    db 0x0F, 0x01, 0x0E             ; sidt [la_idt]: limit, then base 24
    dw la_idt
    db 0x0F, 0x01, 0x06             ; sgdt [la_gdt]
    dw la_gdt
    mov ax, [la_idt]                ; ...and both from the CPU's order
    xchg ax, [la_idt + 4]           ; (limit, base 24, junk) into the
    xchg ax, [la_idt + 2]           ; table's (base 24, 0, limit)
    mov [la_idt], ax
    mov byte [la_idt + 3], 0
    mov ax, [la_gdt]
    xchg ax, [la_gdt + 4]
    xchg ax, [la_gdt + 2]
    mov [la_gdt], ax
    mov byte [la_gdt + 3], 0
    pop ax
    ret

%ifndef LA_NOPROBE                  ; (a caller that hooks INT 6 around its
                                    ; own first borrow need not carry this)
; -----------------------------------------------------------------------------
; la_probe - a borrow that changes nothing (DS's and ES's caches what the two
; segments already are), with INT 6 hooked across it. CF = 1 the instruction
; trapped (and [la_fault] = 1), CF = 0 it ran. Call la_capture first.
; DS = the caller's data. Preserves all but flags
; -----------------------------------------------------------------------------
la_probe:
    push ax
    push dx
    push es
    mov byte [la_fault], 0
    mov ax, ds
    mov [la_dsv], ax
    mov [la_esv], ax
    call la_lin
    mov [la_dsc], ax
    mov [la_dsc + 2], dx
    mov [la_esc], ax
    mov [la_esc + 2], dx
    xor ax, ax
    mov es, ax
    pushf
    cli
    push word [es:6 * 4]
    push word [es:6 * 4 + 2]
    mov word [es:6 * 4], la_ud
    mov [es:6 * 4 + 2], cs
    call la_borrow
    call la_restore
    xor ax, ax
    mov es, ax
    pop word [es:6 * 4 + 2]
    pop word [es:6 * 4]
    popf
    pop es
    pop dx
    pop ax
    cmp byte [la_fault], 1
    cmc                             ; CF = 1 iff it trapped
    ret
%endif ; LA_NOPROBE

; INT 6 while la_probe holds it: note it and step over the two bytes
la_ud:
    mov byte [cs:la_fault], 1
    push bp
    mov bp, sp
    add word [bp + 2], 2
    pop bp
    iret

; la_lin - AX = a segment -> DX:AX = its linear base. Clobbers nothing else
la_lin:
    push cx
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    pop cx
    ret

; la_cache - AX = a segment -> the cache entry at ES:DI: base AX * 16,
; access BL, limit FFFFh. Clobbers AX
la_cache:
    push dx
    call la_lin
    mov [es:di], ax
    mov [es:di + 2], dl
    mov [es:di + 3], bl
    mov word [es:di + 4], 0xFFFF
    pop dx
    ret

; -----------------------------------------------------------------------------
; la_borrow - IF = 0 on entry, and it must stay so until la_restore.
; The 102 bytes at 0080:0000 saved into la_save; the table written - the MSW,
; IDTR and GDTR la_capture took, CS and SS as they are now, DS's cache at
; [la_dsc] and ES's at [la_esc], FLAGS with IF, TF and DF clear, IP and SP to
; carry straight on - and LOADALL.
; in:  DS = the caller's data; [la_dsc]/[la_esc] the two bases;
;      [la_dsv]/[la_esv] the selectors DS and ES SHOW meanwhile. The caller
;      chooses those so that a cache which did not take addresses somewhere
;      harmless: the CPU then addresses through selector x 16.
; out: DS and ES address through those caches. Every general register is
;      as it was. Reach the caller's own data with CS: until la_restore
; -----------------------------------------------------------------------------
la_borrow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cld
    push ds
    pop es                          ; ES = ours
    mov di, la_save
    mov ax, LA_TAB
    mov ds, ax
    xor si, si
    mov cx, LA_SZ / 2
    rep movsw                       ; the API cells, saved
    push es
    pop ds                          ; DS = ours
    mov es, ax                      ; ES = the table
    xor di, di
    mov cx, LA_SZ / 2
    xor ax, ax
    rep stosw                       ; everything not named below: 0 (the
                                    ; general registers come off the stack)
    mov ax, [la_msw]
    mov [es:LT_MSW], ax
    mov si, la_idt
    mov di, LT_IDT
    movsw
    movsw
    movsw
    mov si, la_gdt
    mov di, LT_GDT
    movsw
    movsw
    movsw
    mov bl, [la_csacc]
    mov ax, cs
    mov [es:LT_CS], ax
    mov di, LT_CSC
    call la_cache
    mov bl, 0x93
    mov ax, ss
    mov [es:LT_SS], ax
    mov di, LT_SSC
    call la_cache
    mov ax, [la_dsv]
    mov [es:LT_DS], ax
    mov ax, [la_esv]
    mov [es:LT_ES], ax
    mov ax, [la_dsc]
    mov [es:LT_DSC], ax
    mov ax, [la_dsc + 2]
    mov ah, 0x93
    mov [es:LT_DSC + 2], ax
    mov word [es:LT_DSC + 4], 0xFFFF
    mov ax, [la_esc]
    mov [es:LT_ESC], ax
    mov ax, [la_esc + 2]
    mov ah, 0x93
    mov [es:LT_ESC + 2], ax
    mov word [es:LT_ESC + 4], 0xFFFF
    pushf
    pop ax
    and ax, 0xF8FF                  ; IF, TF and DF clear
    mov [es:LT_FL], ax
    mov word [es:LT_IP], .resume
    mov [es:LT_SP], sp              ; LAST: nothing pushes from here on
    db 0x0F, 0x05                   ; loadall (a 286 instruction: tier 1 only)
.resume:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; la_restore - the API cells put back from la_save; DS and ES are the
; caller's again (CS's, which a package's and a driver's DS both are).
; IF is still 0: the caller's popf opens it. Preserves every general register
; -----------------------------------------------------------------------------
la_restore:
    push cx
    push si
    push di
    push cs
    pop ds
    mov si, la_save
    mov di, LA_TAB
    mov es, di
    xor di, di
    mov cx, LA_SZ / 2
    cld
    rep movsw
    push ds
    pop es
    pop di
    pop si
    pop cx
    ret
