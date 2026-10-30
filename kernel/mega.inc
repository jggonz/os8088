; =============================================================================
; kernel/mega.inc - kern_mega's protected mode (docs/plans/KERN-MEGA-PLAN.md)
;
; kern_big boots in real mode exactly as it does on every other machine, and
; at the end of kmain, with the desktop painted and before task 0 becomes the
; UI task, mega_enter switches the CPU into 286 PROTECTED MODE and leaves it
; there. This file is that switch and everything protected mode needs that
; real mode got from the ROM. It is wave M1 of KERN-MEGA-PLAN 10.
;
; THE MAP (KERN-MEGA-PLAN 3). Every segment value the kernel and its packages
; already hold keeps meaning what it means:
;
;   GDT entry i, for every i from 8 to 8191, is DATA with base i x 128 and a
;   64KB limit, so selector N is segment N whenever N is a multiple of 8. That
;   is every heap base (all of them are multiples of 32 paragraphs), LOW_SEG,
;   0x0040, 0xA000, 0xB000, 0xB800 and 0xF000.
;
;   The EXCEPTIONS are the kernel's two code segments: entry 12 is KERNEL_SEG
;   (0x0060) as READABLE CODE, because every package far-calls 0x0060:cell,
;   and entry COLD_SEG/8 is `.cold` as code. Entries 1-7 have bases inside the
;   IVT, which nothing loads as a segment, so they carry the kernel's own
;   selectors (MEGA_SEL_*).
;
;   LDT entry i, for i from 0 to 2047, is READABLE CODE with base i x 512, so
;   any 512-aligned conventional segment S has a code alias (S >> 2) | 4 - a
;   package, a driver or a module is far-called through that. Its TI bit is
;   set, so it can never equal an identity selector.
;
; Both tables and the IDT live above 1MB (MEGA_*_LIN), which is why this is
; kern_big's and why xm_sniff keeps XMEM.DRV out on this build: kern_mega
; owns the memory up there.
;
; THE ROM (KERN-MEGA-PLAN 2.2). A 286 cannot leave protected mode except by
; resetting the CPU, so nothing on a per-event path may go through the BIOS:
;
;   * IRQ0's ROM half (the tick at 40:6C, the floppy motor-off count and the
;     EOI) is mega_t08, and sch_isr reaches it through the [sch_old08] chain
;     it already makes - the ISR itself is unchanged.
;   * IRQ1's ROM half (scancode to the BIOS buffer at 40:1E) is mega_k09, the
;     target of kbm_isr's existing `jmp far [cs:kbm_old9]`.
;   * int 16h is mega_i16, a native reader of that same buffer, so every
;     caller of int 16h - the kernel's and a package's - is unchanged.
;   * every other int 10h-1Fh is REFLECTED (mega_svc): the CPU goes back to
;     real mode, the BIOS's own vectors for the tick and the keyboard are in
;     the IVT, every IRQ that is not the BIOS's own is masked, the call is
;     made, and the CPU comes back. That is the disk, the video mode sets and
;     the equipment queries.
;
; TWO WAYS BACK TO REAL MODE. A 386 clears CR0.PE. A 286 cannot - lmsw only
; sets it - so it resets the CPU: CMOS shutdown code 0Ah sends the BIOS
; straight to 40:67, which mega_r286 owns, with RAM, A20 and every device as
; they were. QEMU CANNOT TEST THE 286's ARM: its CPU is a 386 (so it takes the
; other one), and its reset resets the whole machine, the VGA with it. That
; arm is the Victory 286's to check.
;
; THE FAULT REPORTER. Every exception and every software interrupt nobody
; handles ends in mega_fault, which writes the vector, the error code, CS:IP,
; SS:SP and the registers to QEMU's debug console (port E9h) and stops. In
; real mode a wild write corrupts something; here it is a #GP at the
; instruction that did it, and that is only worth having if it is reported.
; =============================================================================

%ifdef KERN_MEGA

; THE TABLES ARE ONE BLOCK, SIZED PER MACHINE (KERN-MEGA-MEMORY-PLAN MC7.4's
; step 6): the GDT at its base, the LDT ON TOP OF IT - the LDT's base is the
; GDT's, so its entries below MEGA_LDT_FREE0 are the GDT's own and its
; allocator starts right after them, wasting no entry - and the IDT after the
; LDT's last entry. [mega_ldtn] is the LDT's entries (the GDT's included) and
; [mega_tblkb] the block's KB, both a pure function of [mega_extkb]
; (mega_layoutf), so every boot of one machine lays it out the same and a
; park's anchor is where the next boot looks. Above 1MB at the TOP of
; extended memory under `.cold` and `.mega` (MC2), or in conventional memory
; on a machine too small for that (mega_tlowf). WHERE is [mega_tbllin].
;
; THE GDT IS THE IDENTITY MAP UP TO `.cold`'s SEGMENT AND NO FURTHER: entry S/8
; has base S x 16 for every segment the kernel itself loads (KERNEL_SEG, the
; FAT window, LOW_SEG, the BDA, COLD_SEG, re-aimed when `.cold` goes above
; 1MB). Every claim, region, image, frame buffer and ROM is an LDT selector
; (MC7.4), so a segment loaded past the GDT's limit is a #GP at the
; instruction - a missed conversion reported where it is, never memory read
; through whatever identity entry happened to sit there.
MEGA_IDT_SIZE   equ 256 * 8
MEGA_HMA_KB     equ 64              ; the HMA, the pool's first 64KB, kept for
                                    ; the DOS trip's real-mode half (2.7)
MEGA_GDT_ID     equ (COLD_SEG >> 3) + 1 ; the identity entries
MEGA_GDT_SCR    equ ((MEGA_GDT_ID + 12) & ~7) + 3 ; the scratches after them,
                                    ; placed so their last LINE (64 bytes) is
                                    ; K5..XB exactly: the DOS park sums the GDT
                                    ; in lines and banks that one apart, XA and
                                    ; XB being the copy's own (megados.inc's
                                    ; mega_dsave); the entries between are 0
MEGA_GDT_N      equ MEGA_GDT_SCR + 13   ; ...and the GDT's limit
MEGA_LDT_FREE0  equ MEGA_GDT_N      ; the LDT's allocator (mega_ldt_alloc): its
                                    ; first entry is the first past the GDT's.
                                    ; Every identity selector is below it, so a
                                    ; TI-and-range test still tells a kernel
                                    ; segment from an allocator selector
MEGA_LDT_BASE   equ 512             ; allocator entries every machine gets...
MEGA_LDT_PER16  equ 1               ; ...and one more per 16KB of extended
                                    ; memory: OSAPI_MEM_HUGE's finest step over
                                    ; all of it, beside the pairs, SUBs, windows
                                    ; and the video selectors
MEGA_LDT_MAX    equ 8192            ; the CPU's
%if (FAT_SEG & 4) && FAT_SEG >= MEGA_LDT_FREE0 * 8
  %error "FAT_SEG reads as a pool selector: a FAT window above 1MB could equal it (KERN-MEGA-PLAN item 86)"
%endif
%if (MEGA_GDT_SCR + 5) % 8
  %error "the scratches' last line is not K5..XB (mega_dsave)"
%endif
%if FAT_SEG >= COLD_SEG || LOW_SEG >= COLD_SEG
  %error "the identity map stops at COLD_SEG: the FAT window and LOW_SEG must be below it"
%endif
MEGA_SEL_FD     equ (MEGA_GDT_SCR - 1) * 8  ; data, base moved per use: the
                                    ; file doors' segment arm (DSKW_LD) - in
                                    ; the padding before the block, so the
                                    ; park's line stays K5..XB
%if MEGA_GDT_SCR - 1 < MEGA_GDT_ID
  %error "MEGA_SEL_FD has no padding entry to sit in"
%endif
MEGA_SEL_VB     equ (MEGA_GDT_SCR - 2) * 8  ; data: the planar decoder's buffer
                                    ; ([rom_vgabw], vga_blit_prow's DS), which
                                    ; the switch moves to COLD_RAM and MEGA_RAM -
                                    ; rungs past the identity map (mega_vbaimf)
%if MEGA_GDT_SCR - 2 < MEGA_GDT_ID
  %error "MEGA_SEL_VB has no padding entry to sit in"
%endif
MEGA_SEL_K0     equ (MEGA_GDT_SCR + 0) * 8  ; data, base moved per use: SIX
MEGA_SEL_K1     equ (MEGA_GDT_SCR + 1) * 8  ; scratches a kernel MODULE aims at
MEGA_SEL_K2     equ (MEGA_GDT_SCR + 2) * 8  ; the real paragraphs its own
MEGA_SEL_K3     equ (MEGA_GDT_SCR + 3) * 8  ; arithmetic makes - one per stream
MEGA_SEL_K4     equ (MEGA_GDT_SCR + 4) * 8  ; it keeps live, so no stream's aim
MEGA_SEL_K5     equ (MEGA_GDT_SCR + 5) * 8  ; moves another's (KSEG, DISKOPS.DRV;
                                    ; KERN-MEGA-MEMORY-PLAN MC7.6)
MEGA_SEL_BACK   equ (MEGA_GDT_SCR + 6) * 8  ; data, base moved per use: an LZ
                                    ; match's source 64KB behind the output,
                                    ; whose ES is MEGA_SEL_ODD and whose input
                                    ; DS is MEGA_SEL_SLD (mega_backf, lz.inc).
                                    ; K0..BACK ASCEND: mega_slidef slides any
                                    ; of them onto itself by that range
MEGA_SEL_PSCR   equ (MEGA_GDT_SCR + 7) * 8  ; data, 64KB: THE PARK'S SCRATCH,
                                    ; the tail of `.mega`'s rung at
                                    ; MEGA_SCR_OFF (megados.inc's MPS_*;
                                    ; KERN-MEGA-PLAN 14.45), and banked by the
                                    ; park with the rest of its line
MEGA_SEL_CODE   equ (MEGA_GDT_SCR + 8) * 8  ; `.mega`'s CS. NOT the identity of
                                    ; its own rung, as COLD_SEG is of its: that
                                    ; rung is heap after the switch, and a claim
                                    ; based there would be handed a CODE
                                    ; selector (KERN-MEGA-SEGMENTS-PLAN 3.1)
MEGA_SEL_SLD    equ (MEGA_GDT_SCR + 9) * 8  ; data, base moved per use:
                                    ; mega_slidef's scratch
MEGA_SEL_LDTD   equ (MEGA_GDT_SCR + 10) * 8 ; data, base [mega_ldtlin]: the LDT,
                                    ; for the allocator
MEGA_SEL_XA     equ (MEGA_GDT_SCR + 11) * 8 ; scratch: a linear copy's source
MEGA_SEL_XB     equ (MEGA_GDT_SCR + 12) * 8 ; ...and its destination

MEGA_SEL_LOW0   equ 0x08            ; data, base 0: the IVT and the BDA, for
                                    ; code that used ES = 0 (selector 0 is NULL)
MEGA_SEL_KDATA  equ 0x10            ; data, base KERNEL_SEG x 16: the kernel's
                                    ; DS. 0x0060 is CODE (above), so a store
                                    ; through it would fault
MEGA_SEL_LDT    equ 0x18            ; the LDT's own descriptor
MEGA_SEL_WIN    equ 0x20            ; data, a WINDOW whose base is moved to
                                    ; reach a table above 1MB
MEGA_SEL_ODD    equ 0x30            ; data, base moved per use: a selector for
                                    ; a segment OFF the 8-paragraph grid
                                    ; (mega_oddsegf)
MEGA_SEL_IDT    equ 0x38            ; data, base the IDT's: the IDT, for
                                    ; mega_ivt_sync's rebuilt gates
MEGA_SEL_BCODE  equ 0x28            ; code, base KERNEL_SEG x 16: the
                                    ; BOOTSTRAP's CS between the first lmsw
                                    ; and the far jump into the real tables,
                                    ; and after that the CS the ROM-half
                                    ; chains (mega_t08, mega_k09) are entered
                                    ; through

MEGA_PIC_BASE   equ 0x20            ; IRQ0-7 in protected mode. 08h-0Fh are
                                    ; CPU exceptions there (8 is the double
                                    ; fault), so the master 8259 moves
MEGA_BDA        equ 0x0040          ; an identity selector, and the BDA

%if (KERNEL_SEG % 8) || (LOW_SEG % 8) || (COLD_SEG % 8)
 %error "kern_mega's identity map needs KERNEL_SEG, LOW_SEG and COLD_SEG on a multiple of 8 paragraphs (KERN-MEGA-PLAN 3.1)"
%endif
%if (COLD_SEG >> 3) == (KERNEL_SEG >> 3)
 %error "COLD_SEG and KERNEL_SEG share a GDT entry"
%endif

section .text
cpu 286

; ---- state, in .text: written only while DS is the kernel's -----------------
                                    ; k_dsel - the kernel's data segment for
                                    ; KDS/KES - lives beside the API table in
                                    ; kernel.asm (its note says why)
mega_on     db 0                    ; 1 = the CPU is in protected mode
mega_extkb  dw 0                    ; xm_sniff's int 15h AH=88h answer
mega_bgdt:                          ; THE BOOTSTRAP GDT, six entries. Entry 0
    dw 6*8 - 1                      ; doubles as lgdt's pseudo-descriptor,
    dw 0, 0                         ; which the CPU never reads as an entry
    dw 0                            ; (the base is filled in at run time)
    dw 0x04FF, 0x0000               ; 0x08 LOW0: data, base 0 - the IVT and
                                    ; the BDA and NOT ONE BYTE MORE, so a store
                                    ; meant for the kernel that arrives with a
                                    ; base of 0 faults instead of landing on
                                    ; the API table at 0x600
    db 0x00, 0x92
    dw 0
    dw 0xFFFF, KERNEL_SEG << 4      ; 0x10 KDATA: data, base 0x600
    db (KERNEL_SEG >> 12), 0x92
    dw 0
    dw 0, 0, 0, 0                   ; 0x18 (the LDT, in the real GDT only)
mega_bwin:
    dw 0xFFFF, 0                    ; 0x20 WIN: data, base moved per table -
    db 0, 0x92                      ; the GDT's first (mega_tset)
    dw 0
    dw 0xFFFF, KERNEL_SEG << 4      ; 0x28 BCODE: code, base 0x600
    db (KERNEL_SEG >> 12), 0x9A
    dw 0
mega_gdtr   dw MEGA_GDT_N*8 - 1     ; the real tables, aimed by mega_tset:
            dw 0, 0                 ; at the top of extended memory or in
mega_idtr   dw 256*8 - 1            ; the conventional claim mega_tlowf takes
            dw 0, 0
mega_ldtlin dd 0                    ; the LDT's linear base: the GDT's
mega_ldtn   dw 0                    ; the LDT's entries, the GDT's included,
mega_ldtend dw 0                    ; ...that in bytes (the scans' end), and
mega_tblkb  dw 0                    ; the block's KB - mega_layoutf's (MC7's
                                    ; step 6)
mega_tbllin dd 0                    ; THE TABLES' BLOCK: the GDT's base (MC2)
mega_coldlin dd 0                   ; `.cold` above 1MB, when [mega_codehi]
mega_fixkb  dw 0                    ; KB from 1MB to the lowest fixed byte
                                    ; above it: the pool's whole extent
mega_codehi db 0                    ; 1: `.cold` and `.mega` fit above 1MB
mega_rmidtr dw 0x03FF               ; real mode's: the IVT
            dw 0, 0
mega_nullidt dw 0                   ; the 286's reset: an IDT that holds nothing
            dw 0, 0
mega_rss    dw 0                    ; the reflection's stack, across a 286 reset
mega_rsp    dw 0
mega_old08  dd 0                    ; the BIOS's own int 08h and int 09h, put
mega_old09  dd 0                    ; back in the IVT for every reflection
mega_ses    dw 0                    ; mega_svc's: ES and DS as the caller held
mega_sds    dw 0                    ; them, as the BIOS was given them, the
mega_res    dw 0                    ; vector, the tick it started on and the
mega_rds    dw 0                    ; IMRs it masked
mega_t0     dw 0
mega_vbsel  dw VGABUF_SEG           ; what vga_blit_prow loads for the planar
                                    ; decoder's buffer: [rom_vgabw] in real
                                    ; mode, MEGA_SEL_VB once mega_vbaimf has
                                    ; aimed it (MC7's step 6)
mega_rhval  dw 0                    ; mem_reown_x's answer to a re-home: the
                                    ; trimmed carve's own selector, which the
                                    ; program runs in (loader step 8a, MC7)
mega_rv     db 0
mega_imr1   db 0
mega_imr2   db 0
mega_old15  dd 0                    ; the int 15h mega_r15 chains to

; -----------------------------------------------------------------------------
; mega_r15 - int 15h IN A REFLECTION: the key-state map kept through the BIOS
;            (KERN-MEGA-PLAN 14.119, SPEC.md 9.7.2)
;
; A reflection runs the BIOS in real mode on the IVT, so an IRQ1 taken inside
; one is the ROM's own int 09h - kbm_isr and mega_k09 never see the byte, and
; SPEC.md 9.7's map misses it. On a 286 a reflection is a CPU reset each way,
; and a mode set on a slow card is long, so the break of the Alt+Enter that
; asked for it lands there: the map then holds Alt or Enter DOWN for good,
; os88alt_edge reads a hold that is not there and the game's pad reads Start.
; The ROM's int 09h calls int 15h AH=4Fh with the byte it read (SPEC.md
; 9.7.2's own door), so this is that door for the reflections: the byte to
; kbd_track, nothing translated, and on to the int 15h mega_pmtail found.
; Real mode, CS = KERNEL_SEG; mega_dosgo and mega_hbgor put that int 15h back
; for code that outlives the kernel. Every register and flag is the ROM's
; -----------------------------------------------------------------------------
mega_r15:
    cmp ah, 0x4F
    jne .c
    pushf
    push ds
    push cs
    pop ds
    cmp byte [kbd_armed], 0
    je .n
    call kbd_track                  ; preserves AL and every register
.n:
    pop ds
    popf
.c:
    jmp far [cs:mega_old15]

; -----------------------------------------------------------------------------
; mega_enter - switch to protected mode (KERN-MEGA-PLAN 2.1)
;
; in:       real mode, DS = ES = KERNEL_SEG, SS = LOW_SEG; called from kmain
;           immediately before `jmp ui_task`
; out:      protected mode with DS = ES = MEGA_SEL_KDATA, or - on a CPU or a
;           machine M1 cannot drive - real mode exactly as it came in. Every
;           register but AX, CX, DX, SI, DI and the segment registers kept
; -----------------------------------------------------------------------------
mega_enter:
    call COLD_SEG:mega_gatef        ; whether it can be made at all - `.cold`,
    jnc .room                       ; as every refusal is (KERN-MEGA-PLAN 14.45)
    ret
.room:
    cli
    in al, 0x70                     ; NMI off across the switch: a real-mode
    or al, 0x80                     ; IVT is not an IDT
    out 0x70, al

    les ax, [sch_old08]             ; the BIOS's own handlers, kept for the
    mov [mega_old08], ax            ; reflection's IVT...
    mov [mega_old08+2], es
%ifndef KBD_I15
    les ax, [kbm_old9]
%else
    ; ...UNLESS mouse_init hooked int 15h AH=4Fh INSTEAD (SPEC.md 9.7.2).
    ; Protected mode needs the int 09h shape whatever the ROM offers: the IDT
    ; is built FROM the IVT (mega_idt_build), so IVT 09h must be kbm_isr or
    ; IRQ1 becomes the EOI stub - a dead keyboard, and an 8042 nobody reads,
    ; which stops the PS/2 mouse too (Bochs, measured) - and a reflection
    ; runs the BIOS's int 09h on the IVT, whose 4Fh must not land in kbm_i15,
    ; whose KDS would load a protected-mode selector. So it becomes that
    ; shape here: the ROM's int 15h back, kbm_isr into int 09h, the ROM's
    ; int 09h into kbm_old9, and [kbm_ivec] int 09h's
    cmp byte [kbm_ivec], 0x54
    jne .k9
    xor ax, ax
    mov es, ax
    mov ax, [kbm_old9]
    mov dx, [kbm_old9+2]
    mov [es:0x54], ax
    mov [es:0x56], dx
    mov ax, kbm_isr
    xchg ax, [es:0x24]
    mov dx, KERNEL_SEG
    xchg dx, [es:0x26]
    mov [kbm_old9], ax
    mov [kbm_old9+2], dx
    mov byte [kbm_ivec], 0x24
.k9:
    les ax, [kbm_old9]          ; the ROM's int 09h, either way
%endif
    mov [mega_old09], ax
    mov [mega_old09+2], es
    push ds
    pop es

    ; --- the bootstrap: protected mode on a six-entry GDT ---------------------
    mov ax, ds                      ; lgdt takes a LINEAR base
    mov dx, ax
    shl ax, 4
    shr dx, 12
    add ax, mega_bgdt
    adc dx, 0
    mov [mega_bgdt+2], ax
    mov [mega_bgdt+4], dl
    lgdt [mega_bgdt]
    smsw ax
    or al, 1
    lmsw ax
    db 0xEA                         ; jmp far MEGA_SEL_BCODE:.boot
    dw .boot, MEGA_SEL_BCODE
.boot:
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov ax, MEGA_SEL_WIN
    mov es, ax
    cld

    ; --- the GDT, through WIN at [mega_tbllin] --------------------------------
    xor di, di
    xor bx, bx                      ; BX = the entry
.gdt:
    cmp bx, 8
    jb .gspecial
    cmp bx, MEGA_GDT_ID             ; past `.cold`'s entry: the scratches (and
    jae .gz                         ; the padding before them), aimed per use
    mov ax, 0xFFFF                  ; IDENTITY: base BX x 128
    stosw
    mov ax, bx
    shl ax, 7
    stosw
    mov ax, bx
    shr ax, 9                       ; base 23:16 (BX < 8192, so < 16)
    mov ah, 0x92
    cmp bx, KERNEL_SEG >> 3
    je .gcode
    cmp bx, COLD_SEG >> 3
    jne .gput
.gcode:
    mov ah, 0x9A                    ; ...but the kernel's code is CODE
.gput:
    stosw
    xor ax, ax
    stosw
    jmp short .gnext
.gspecial:                          ; entries 0-7: the bootstrap's own, plus
    mov si, bx                      ; the LDT's
    shl si, 3
    add si, mega_bgdt
    cmp bx, MEGA_SEL_LDT >> 3
    jne .gcopy
    mov si, mega_ldtd
.gcopy:
    or bx, bx
    jz .gz
    cmp bx, 6                       ; the bootstrap has six; 6 is ODD's,
    jb .gmov                        ; written per use, and 7 the IDT's alias
    mov si, mega_idtd
    cmp bx, MEGA_SEL_IDT >> 3
    je .gmov
.gz:
    mov si, mega_zero8
.gmov:
    movsw
    movsw
    movsw
    movsw
.gnext:
    inc bx
    cmp bx, MEGA_GDT_N
    jb .gdt

    ; --- the LDT's allocator, FREE, straight after the GDT: the LDT's base is
    ; the GDT's (MC7.4's step 6), so DI is already its first entry. An access
    ; byte of 0 is "not present", which mega_ldt_alloc looks for. Then the IDT
    mov cx, [mega_ldtn]
    sub cx, MEGA_LDT_FREE0
    shl cx, 2                       ; (words)
    xor ax, ax
    rep stosw
    call mega_idt_build             ; ES:DI = the IDT, 256 gates

    ; --- the real tables -------------------------------------------------------
    lgdt [mega_gdtr]
    lidt [mega_idtr]
    mov ax, MEGA_SEL_LDT
    lldt ax
    db 0xEA                         ; jmp far KERNEL_SEG:.pm - CS is 0x0060
    dw .pm, KERNEL_SEG              ; again, now as GDT entry 12
.pm:
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov es, ax
    mov ax, ss                      ; LOW_SEG, an identity selector
    mov ss, ax
    mov word [k_dsel], MEGA_SEL_KDATA
    mov byte [mega_on], 1
    mov byte [ss:mega_pm], 1        ; ...and .cold's copy of the same fact
    mov word [k_seg0], MEGA_SEL_LOW0 ; low memory's selector (MEGA_SEG0)
    or byte [cpu_feat], CPU_F_PM | CPU_F_A20 ; OSAPI_CPU_INFO says so
                                    ; (KERN-MEGA-PLAN 12.1), and A20 is
                                    ; VERIFIED open - mega_a20 did, and no
                                    ; XMEM.DRV runs here to say it
    mov word [ss:mega_seg0], MEGA_SEL_LOW0
    mov ax, MEGA_SEL_WIN            ; the LDT as DATA, for its allocator: a
    mov es, ax                      ; descriptor of exactly the table, so
    mov di, MEGA_SEL_LDTD           ; nothing written through it can reach
    mov ax, [mega_ldtend]           ; the IDT or `.cold` beyond it
    dec ax
    mov [es:di], ax
    mov ax, [mega_ldtlin]
    mov [es:di+2], ax
    mov al, [mega_ldtlin+2]
    mov ah, 0x92
    mov [es:di+4], ax
    mov word [es:di+6], 0
    push ds
    pop es
    mov ax, [rom_vgabw]             ; the decoder's buffer, wherever it is,
    call COLD_SEG:mega_vbaimf       ; through MEGA_SEL_VB from here on
    call mega_coldup                ; M3: `.cold` and `.mega` above 1MB, their
                                    ; rungs to the heap - FIRST, because
                                    ; everything after it is `.mega`'s
    call MEGA_SEL_CODE:mega_pmtail  ; (KERN-MEGA-SEGMENTS-PLAN 3.2)
    ret

section .text
; -----------------------------------------------------------------------------
; mega_idt_build - the IDT, from the IVT as the boot left it
;
; in:       protected mode, DS = KDATA, ES:DI = where the 256 gates go
; out:      ES:DI past them. Clobbers AX, BX, CX, DX, SI
;
; Exceptions 00h-0Fh get the fault reporter; 10h-1Fh are the BIOS's software
; services; 20h-27h are IRQ0-7, taken from IVT 08h-0Fh; everything else is
; taken from the IVT entry of the same number. An IVT entry whose segment is
; the kernel's becomes a gate to the same offset in the same segment (0x0060
; or COLD_SEG are code selectors); a heap image's segment becomes its pair's
; code half; anything else - the ROM - becomes the EOI stub for an IRQ and
; the fault reporter for a software interrupt, which nothing here should make.
; -----------------------------------------------------------------------------
mega_idt_build:
    xor bx, bx                      ; BL = the vector
.v:
    call mega_idt_gate
    inc bx
    cmp bx, 256
    jb .v
    ret

; mega_idt_gate - one gate: BX = the vector, ES:DI = where it goes -> DI past
; it. Clobbers AX, CX, DX, SI. mega_ivt_sync rebuilds the IRQ rows with it.
mega_idt_gate:
    mov ax, bx
    cmp al, 0x10
    jae .notx
    mov dx, mega_nmi                ; NMI: the BIOS's job, done here
    mov cx, MEGA_SEL_CODE           ; (`.mega`, as every handler here that
    cmp al, 2                       ; runs in protected mode alone is -
    je .put                         ; KERN-MEGA-SEGMENTS-PLAN 3.6)
    mov dx, ax                      ; an EXCEPTION: stub BX
    shl dx, 3                       ; (mega_xs is 8 bytes a stub)
    add dx, mega_xs
    jmp .put
.notx:
    cmp al, 0x20
    jae .notsvc
    mov dx, ax                      ; a BIOS SERVICE
    sub dx, 0x10
    shl dx, 3
    add dx, mega_ss
    mov cx, MEGA_SEL_CODE
    cmp al, 0x16
    jne .put
    mov dx, mega_i16                ; ...the keyboard is native
    mov cx, MEGA_SEL_CODE
    jmp .put
.notsvc:
    mov si, ax                      ; IVT slot to translate
    cmp al, 0x28
    jae .ivt
    sub si, MEGA_PIC_BASE - 8       ; 20h-27h are IRQ0-7: IVT 08h-0Fh
.ivt:
    push es
    push ax
    mov ax, MEGA_SEL_LOW0
    mov es, ax
    shl si, 2
    mov dx, [es:si]                 ; DX = offset
    mov cx, [es:si+2]               ; CX = segment
    pop ax
    pop es
    cmp cx, KERNEL_SEG
    je .put
    cmp cx, COLD_SEG
    je .put
    cmp cx, 0xA000                  ; the ROM and the option ROMs
    jae .rom
    test cl, 4                      ; bit 2 clear: a REAL-MODE segment, which a
    jz .seg                         ; heap image's is (512-aligned) at any depth
    cmp cx, MEGA_LDT_FREE0 * 8      ; an image ABOVE 1MB (KERN-MEGA-PLAN items
    jb .rom                         ; 78, 82): the IVT names its pair - the
    cmp word [k_dsel], KERNEL_SEG   ; CODE half where its own `mov [..], cs`
    je .rom                         ; wrote it, the DATA half where the move
    push es                         ; re-pointed a vector (mem_region_reloc).
    mov si, MEGA_SEL_LDTD           ; NOT in mega_enter's own build: the
    mov es, si                      ; bootstrap GDT has no LDTD, nothing is
    mov si, cx                      ; above 1MB yet, and a ROM's segment can
    and si, 0xFFF8                  ; have bit 2 set - the mr286's does, and
    mov si, [es:si+4]               ; loading LDTD there triple-faulted it
    pop es                          ; into a boot loop. SI high = the access
    test si, 0x8000                 ; byte: a FREE entry is a real segment
    jz .rom                         ; that only looks like a pair
    test si, 0x0800                 ; executable?
    jnz .put                        ; the code half: as it is
    sub cx, 8                       ; the data half: its code twin
    jmp .put
.seg:
    test cx, 31
    jnz .rom
    cmp word [k_dsel], KERNEL_SEG   ; A HEAP IMAGE: its pair's CODE half
    je .rom                         ; (KERN-MEGA-MEMORY-PLAN MC7.4). The
    push dx                         ; switch's own build has no pairs yet and
    mov dx, cx                      ; leaves it to mega_pmtail's mega_ivt_sync,
    call MEGA_SEL_CODE:mega_p2df    ; which runs once mega_mpall has made them
    mov cx, dx
    pop dx
    jc .rom
    sub cx, 8
    jmp .put
.rom:
    call mega_irqrom                ; an OPTION ROM's IRQ: reflected (DX)
    jnc .romk
    mov cx, MEGA_SEL_CODE           ; (mega_irv is `.mega`'s)
    jmp .put
.romk:
    mov cx, MEGA_SEL_CODE
    mov dx, mega_unh                ; a software interrupt: nobody's
    cmp al, 0x28
    jb .irqm
    cmp al, 0x70
    jb .put
    cmp al, 0x78
    jae .put
    mov dx, mega_eoi2               ; IRQ8-15 the ROM owned
    jmp .put
.irqm:
    mov dx, mega_fdisr              ; IRQ6 the ROM owned: the floppy's flag,
    cmp al, 0x26                    ; which the native path waits on
    je .put
    mov dx, mega_eoi1               ; IRQ0-7 the ROM owned
.put:
    mov ax, dx                      ; an INTERRUPT gate (IF off), DPL 0
    stosw
    mov ax, cx
    stosw
    mov ax, 0x8600
    stosw
    xor ax, ax
    stosw
    ret

; -----------------------------------------------------------------------------
; mega_irqrom - is IDT vector AL an IRQ an OPTION ROM owns? CX = the IVT
; segment its handler is in. CF = 1 yes, DX = the reflecting stub; else CF = 0
; and DX kept. Clobbers nothing else.
;
; THE PICOMEM'S MULTIPLEXER (SPEC.md 34.10.1) is why: a PicoMEM 1.x has one
; interrupt line, its option ROM owns it, and on it the card has the CPU copy
; each 128-byte "DMA" block and raises its emulated Sound Blaster's IRQ as a
; software int 8+n. Dismissed with an EOI, as every ROM IRQ was, no block was
; ever copied: Tracker on the Victory 286 cycled Buffering / Playing three
; times a second as SOUND.DRV's watchdog ended each stream. So an option ROM's
; IRQ goes to real mode and back (mega_svc, marked as an IRQ). The system
; BIOS's own (F000) stay dismissed - they are its unexpected-interrupt
; handlers - except IRQ9, whose F000 handler is the AT's redirect to int 0Ah,
; when that vector is an option ROM's: a card jumpered to IRQ2.
; -----------------------------------------------------------------------------
mega_irqrom:
    cmp al, 0x26                    ; the floppy is native
    je .no
    cmp al, 0x71
    jne .notr
    cmp cx, 0xF000                  ; IRQ9 at the BIOS's redirect...
    jb .rng
    push es
    push ax
    mov ax, MEGA_SEL_LOW0
    mov es, ax
    mov ax, [es:0x0A*4+2]           ; ...to an option ROM's int 0Ah?
    cmp ax, 0xC000
    jb .no2
    cmp ax, 0xF000
    jae .no2
    pop ax
    pop es
    jmp short .yes
.no2:
    pop ax
    pop es
    jmp short .no
.notr:
    cmp al, 0x22
    jb .no
    cmp al, 0x28
    jb .rng
    cmp al, 0x70
    jb .no
    cmp al, 0x78
    jae .no
.rng:
    cmp cx, 0xC000
    jb .no
    cmp cx, 0xF000
    jae .no
.yes:
    push ax                         ; DX = the stub: 0Ah-0Fh for 22h-27h,
    cmp al, 0x28                    ; 70h-77h for themselves, 10 bytes apart
    jae .slv
    sub al, 0x22
    jmp short .idx
.slv:
    sub al, 0x70 - 6
.idx:
    mov ah, 6
    mul ah
    add ax, mega_irv
    mov dx, ax
    pop ax
    stc
    ret
.no:
    clc
    ret

; the reflecting stubs: the REAL-MODE vector, marked (high byte 1) as an IRQ
; for mega_svc - IVT 0Ah-0Fh for IRQ2-7, 70h-77h for IRQ8-15
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_irv:
%assign mega_q 0x0A
%rep 6
    push word 0x100 | mega_q
    jmp near mega_svc
  %assign mega_q mega_q + 1
%endrep
%assign mega_q 0x70
%rep 8
    push word 0x100 | mega_q
    jmp near mega_svc
  %assign mega_q mega_q + 1
%endrep
%if $ - mega_irv != 14*6
 %error "mega_irv is six bytes a stub"
%endif

; mega_capon / mega_capoff - REAL MODE, DS = KERNEL_SEG, around an IRQ
; reflection: every IVT entry for IRQ2-7 that names kernel or driver code -
; the software vectors 0Ah-0Fh a ROM handler may raise, and the hardware ones
; 22h-27h the PIC delivers to in real mode - is a capture stub for the while,
; because that code is protected-mode code. What it caught is replayed through
; the IDT once we are back (mega_replay)
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_capon:
    pusha
    push es
    xor ax, ax
    mov es, ax
    mov [mega_irqpend], al
    mov si, mega_capsave
    mov bx, 0x0A*4                  ; IRQ2's software vector...
    call .pair
    mov bx, 0x22*4                  ; ...and its hardware one
    call .pair
    mov ax, [es:0x33*4]             ; ...and int 33h, when it names a driver
    mov dx, [es:0x33*4+2]           ; image (KERN-MEGA-PLAN 14.117): the
    mov [mega_c33sv], ax            ; PicoMEM's multiplexer hands each USB
    mov [mega_c33sv+2], dx          ; mouse report to `int 33h AX=0060h`
    cmp dx, 0xA000                  ; from inside the IRQ this reflection
    jae .n33                        ; runs, and USBMOUSE.DRV's handler is
    or dx, dx                       ; protected-mode code that calls the
    jz .n33                         ; kernel - so the report is CAUGHT here
    mov word [es:0x33*4], mega_cap33 ; and handed to it once we are back
    mov word [es:0x33*4+2], KERNEL_SEG ; (mega_m33go)
.n33:
    pop es
    popa
    ret
.pair:
    mov di, mega_cap2               ; six lines, the floppy's skipped
    mov cx, 6
.e:
    mov ax, [es:bx]
    mov dx, [es:bx+2]
    mov [si], ax
    mov [si+2], dx
    add si, 4
    cmp bx, 0x0E*4                  ; the floppy, both vectors
    je .k
    cmp bx, 0x26*4
    je .k
    cmp dx, 0xA000                  ; a ROM's: it runs in real mode as it is
    jae .k
    or dx, dx
    jz .k
    mov word [es:bx], di
    mov word [es:bx+2], KERNEL_SEG
.k:
    add bx, 4
    add di, 6
    loop .e
    ret
mega_capoff:
    pusha
    push es
    xor ax, ax
    mov es, ax
    mov si, mega_capsave
    mov bx, 0x0A*4
    call .pair
    mov bx, 0x22*4
    call .pair
    mov ax, [mega_c33sv]            ; int 33h as it was
    mov [es:0x33*4], ax
    mov ax, [mega_c33sv+2]
    mov [es:0x33*4+2], ax
    pop es
    popa
    ret
.pair:
    mov cx, 6
.e:
    mov ax, [si]
    mov dx, [si+2]
    mov [es:bx], ax
    mov [es:bx+2], dx
    add si, 4
    add bx, 4
    loop .e
    ret
mega_cap2:                          ; six bytes a line: IRQ n -> bit n
%assign mega_q 2
%rep 6
    push ax
    mov al, 1 << mega_q
    jmp near mega_capc
  %assign mega_q mega_q + 1
%endrep
mega_capc:
    or [cs:mega_irqpend], al        ; real mode: code is memory
    pop ax
    iret
; mega_cap33 - int 33h for the length of an IRQ reflection, in REAL mode. AX =
; 0060h is a PicoMEM USB mouse report (DX the buttons, BX dx, CX dy, signed):
; summed into mega_m33* for mega_m33go. Anything else is nobody's here - the
; vector it replaced may be a SELECTOR (a driver hooked it in protected mode),
; which real mode cannot chain to - so it returns untouched
mega_cap33:
    cmp ax, 0x0060
    jne .x
    inc byte [cs:mega_m33n]
    mov [cs:mega_m33b], dx
    add [cs:mega_m33x], bx
    add [cs:mega_m33y], cx
.x:
    iret
mega_capsave times 12 dd 0
mega_c33sv  dw 0, 0                 ; int 33h across a reflection
mega_m33x   dw 0                    ; ...the reports it caught: dx, dy summed,
mega_m33y   dw 0
mega_m33b   dw 0                    ; the last buttons,
mega_m33n   db 0                    ; and how many (0 = none to deliver)
mega_irqpend db 0

; mega_replay - PROTECTED MODE, after an IRQ reflection: each line a capture
; stub caught, delivered through its IDT gate, lowest line first
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_replay:
    push ax
    xor al, al
    xchg al, [mega_irqpend]
%ifdef MEGA_DIAG
    or al, al
    jz .none
    inc byte [mega_pmq+5]           ; lines caught and replayed (mega_rfsec)
.none:
%endif
    test al, 1 << 2
    jz .n3
    int 0x22
.n3:
    test al, 1 << 3
    jz .n4
    int 0x23
.n4:
    test al, 1 << 4
    jz .n5
    int 0x24
.n5:
    test al, 1 << 5
    jz .n7
    int 0x25
.n7:
    test al, 1 << 7
    jz .x
    int 0x27
.x:
    cmp byte [mega_m33n], 0         ; ...and a mouse report mega_cap33 caught
    je .x2
    call mega_m33go
.x2:
    pop ax
    ret

; mega_m33go - PROTECTED MODE, IF = 0, DS = KDATA: what mega_cap33 caught,
; delivered as the card would have - `int 33h` with AX = 0060h, DX the
; buttons, BX dx and CX dy - through a gate rebuilt from IVT 33h as it stands
; (the driver's image as a real segment if it hooked before the switch, its
; code selector if after; either moves with the image). Two reports in one
; reflection arrive as one, their sum held to a report's own range. Every
; register kept (KERN-MEGA-PLAN 14.117)
mega_m33go:
    pusha
    push es
    mov ax, MEGA_SEL_IDT
    mov es, ax
    mov bx, 0x33
    mov di, 0x33 * 8
    call KERNEL_SEG:mega_idt_gatef
    xor ax, ax
    mov [mega_m33n], al
    xchg ax, [mega_m33x]
    call .clamp
    xchg ax, bx
    xor ax, ax
    xchg ax, [mega_m33y]
    call .clamp
    xchg ax, cx
    mov dx, [mega_m33b]
    mov ax, 0x0060
    int 0x33
    pop es
    popa
    ret
.clamp:                             ; AX -> -127..127
    cmp ax, 127
    jle .lo
    mov ax, 127
.lo:
    cmp ax, -127
    jge .ok
    mov ax, -127
.ok:
    ret

; -----------------------------------------------------------------------------
; the PIC to MEGA_PIC_BASE, once at mega_enter and again only if a 286's BIOS
; re-initialised it during a reset (mega_r286). Never per reflection: ICW1
; resets the edge latches (mega_svc). The IMR the 8259 forgets is kept
; -----------------------------------------------------------------------------
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_pic_pm:
    in al, 0x21
    push ax
    mov al, 0x11                    ; ICW1: edge, cascade, ICW4
    out 0x20, al
    mov al, MEGA_PIC_BASE           ; ICW2: the base
    out 0x21, al
    mov al, 0x04                    ; ICW3: the slave on IRQ2
    out 0x21, al
    mov al, 0x01                    ; ICW4: 8086 mode
    out 0x21, al
    pop ax
    out 0x21, al
    ret

; mega_kdrain - the 8042's output buffer emptied, RIGHT AFTER an ICW1 (real
; mode or protected; AL clobbered). ICW1 resets the edge sense of every line,
; and the 8042 holds IRQ1/IRQ12 HIGH while a byte waits in its one buffer, so
; a byte that was there makes no new edge: nobody reads it, and the controller
; sends nothing more from the keyboard OR the mouse - both dead, IRQ0 and the
; drawing alive (KERN-MEGA-PLAN 14.62). Read straight after the ICW1 such a
; byte has no request behind it to go spurious, so dropping it is exact: one
; key or one packet byte lost, where leaving it loses both devices for good.
; A byte that comes after makes its edge and is delivered when its line opens
mega_kdrain:
    push cx
    mov cx, 16
.l:
    in al, 0x64
    test al, 1
    jz .x
    in al, 0x60
    loop .l
.x:
    pop cx
    ret

; mega_leds - the keyboard's lock lights from KB_FLAG (DS = the BDA): command
; EDh and the LED byte (bit 0 Scroll, 1 Num, 2 Caps = KB_FLAG bits 4-6), each
; ACK read here - unless the byte waiting is the PS/2 mouse's, which is left
; for IRQ12. 40:97's low bits follow, as the ROM keeps them.
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_leds:
    mov al, [0x17]
    shr al, 4
    and al, 7
    mov ah, al
    mov al, [0x97]
    and al, 0xF8
    or al, ah
    mov [0x97], al
    call mm_kbw
    jnz .out
    mov al, 0xED
    out 0x60, al
    call mega_kack
    call mm_kbw
    jnz .out
    mov al, ah
    out 0x60, al
    call mega_kack
.out:
    ret
mega_kack:                          ; wait for the keyboard's ACK and take it
    xor cx, cx
.w:
    in al, 0x64
    test al, 1
    jnz .got
    loop .w
    ret
.got:
    test al, 0x20                   ; the aux port's byte: not ours
    jnz .ret
    in al, 0x60
.ret:
    ret
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)

; -----------------------------------------------------------------------------
; mega_t08 - the ROM's half of IRQ0, reached through sch_isr's
; `pushf / call far [sch_old08]` exactly as the ROM was: the tick at 40:6C and
; its midnight wrap, the floppy motor-off count at 40:40, the EOI, iret.
; int 1Ch, which the ROM calls from here, is NOT called: nothing in the tree
; hooks it outside DOS, which is real mode (KERN-MEGA-PLAN 2.7).
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_t08:
    push ax
    push dx
    push ds
    mov ax, MEGA_BDA
    mov ds, ax
    add word [0x6C], 1
    adc word [0x6E], 0
    cmp word [0x6E], 0x18
    jne .day
    cmp word [0x6C], 0xB0
    jne .day
    xor ax, ax
    mov [0x6C], ax
    mov [0x6E], ax
    mov byte [0x70], 1
.day:
    cmp byte [0x40], 0
    je .eoi
    dec byte [0x40]
    jnz .eoi
    and byte [0x3F], 0xF0           ; every motor off, as the ROM does
    mov al, 0x0C
    mov dx, 0x3F2
    out dx, al
.eoi:
    mov ax, MEGA_SEL_KDATA          ; (`.mega` is protected mode only, so the
    mov ds, ax                      ; kernel's data selector is a constant)
%ifdef MEGA_DIAG
    dec byte [mega_rfdiv]           ; once a second: the IRQ reflections' rate
                                    ; and cost, for the menu bar (mega_tell;
                                    ; MEGADIAG=1 only, KERN-MEGA-PLAN 14.65)
    jnz .eoi2
    mov byte [mega_rfdiv], 18
    call mega_rfsec
.eoi2:
%endif
    mov al, 0x20
    out 0x20, al
%ifndef NO_KBDWD
    call mega_kwd                   ; ...and after the EOI, the 8042's lost
%endif                              ; edge (KERN-MEGA-PLAN 14.64)
    pop ds
    pop dx
    pop ax
    iret

%ifndef NO_KBDWD
; -----------------------------------------------------------------------------
; mega_kwd - THE 8042's LOST-EDGE WATCHDOG (KERN-MEGA-PLAN 14.64, SPEC.md
; 96.45.2.5). The controller has ONE output buffer for the keyboard and the
; PS/2 mouse and holds IRQ1 or IRQ12 HIGH while a byte waits in it, sending
; nothing more from either; an ISA line is edge-triggered, so a byte whose
; edge an 8259 initialisation erased is never delivered and both devices are
; dead for the session. On a 286 every BIOS call kern_mega reflects is a CPU
; reset, and a BIOS that re-initialises the 8259 on that path erases whatever
; edge was pending - the Victory's does, and a hand on the mouse while the
; live resume makes its calls is a byte at one of those instants. 14.62's
; drains run at fixed moments; this runs on every tick.
;
; A byte is STRANDED when it is in the buffer, its line is UNMASKED at the
; PIC (IRQ1; or IRQ12 at the slave AND the cascade at the master), the
; master latches no request for it (IRR) and nothing is in service (ISR) -
; on TWO CONSECUTIVE TICKS, because a byte the controller has only just put
; there has OBF set before its pin is raised. A request that IS latched is
; delivered the moment the tick's iret puts IF back, so it cannot still be
; there a tick later. Every polled 8042 sequence on this kernel holds its
; line MASKED (hbm_wake, hbm_dosrun, mega_dwake) or runs with IF = 0 inside
; an ISR (mega_leds), so nothing a poll is waiting for is ever touched.
;
; The byte is RE-DELIVERED rather than dropped: `int` through the line's own
; gate, which is what the lost edge would have done - IRQ1's is kbm_isr on
; this kernel always (mega_ivt_sync leaves 20h/21h alone), IRQ12's is whoever
; owns the vector, mou_p2_isr or a package. Their EOIs are no-ops here, the
; tick's own having gone and nothing else being in service. An AUX byte whose
; vector is the ROM's (its gate only EOIs, or reflects) is read and dropped.
; So a key is not lost and a packet does not desync, and a tick that misjudges
; a byte merely delivers it early: its ISR finds the buffer empty after.
;
; DS = KDATA, IF = 0, AX and DX free (mega_t08's). The common tick is one IN
; -----------------------------------------------------------------------------
mega_kwd:
    in al, 0x64
    test al, 1
    jz .x                           ; nothing waiting: every ordinary tick
    mov ah, al                      ; AH = the status, kept to the end
    mov dl, 0x02                    ; DL = its line at the master: IRQ1...
    test ah, 0x20
    jz .kb
    in al, 0xA1
    test al, 0x10                   ; IRQ12 masked at the slave: a poll's
    jnz .x
    mov dl, 0x04                    ; ...or the cascade, for an AUX byte
.kb:
    in al, 0x21
    test al, dl                     ; masked at the master: a poll's
    jnz .x
    mov al, 0x0A                    ; OCW3: the IRR
    out 0x20, al
    in al, 0x20
    test al, dl                     ; a request latched: its ISR will take
    jnz .x                          ; it at the iret
    mov al, 0x0B                    ; OCW3: the ISR...
    out 0x20, al
    in al, 0x20
    mov dh, al
    mov al, 0x0A                    ; ...and the IRR selected again, as the
    out 0x20, al                    ; 8259 is left everywhere else
    or dh, dh
    jnz .x                          ; something in service: not now
    push es
    mov dx, LOW_SEG
    mov es, dx
    mov dx, [ticks]                 ; [ticks] is the tick BEFORE this one
    dec dx                          ; (sch_isr counts after the chain), so
    cmp dx, [es:mega_kwdt]          ; the last sighting was the tick before
    je .lost                        ; if it is one less
    inc dx
    mov [es:mega_kwdt], dx          ; a first sighting: remembered
    pop es
.x:
    ret
.lost:
    pop es
    push si
    mov si, mega_mkwd               ; "kern_mega: 8042 edge lost, 64=ss"
    call mm_puts
    mov al, ah
    call mm_hex2
    mov al, 10
    call mm_putc
    pop si
    test ah, 0x20
    jnz .aux
    int MEGA_PIC_BASE + 1           ; IRQ1's gate: kbm_isr
    ret
.aux:
    push es
    mov dx, MEGA_SEL_LOW0
    mov es, dx
    cmp word [es:0x74*4+2], 0xA000  ; IRQ12 the ROM's: no gate to deliver to
    pop es
    jae .drop
    int 0x74                        ; IRQ12's gate (the slave stays at 70h)
    ret
.drop:
    in al, 0x60
    ret
mega_mkwd   db "kern_mega: 8042 edge lost, 64=", 0
%endif

%ifdef MEGA_DIAG                    ; the menu bar's RFL and Q lines (14.65)
; mega_rfsec - the second's IRQ reflections: twenty or more and nothing on the
; bar for fifteen seconds puts "RFL rrrr/s uuuu" up - the count and the
; average microseconds, in hex. A 286 leaves protected mode only by a reset,
; so a reflection is a trip through the BIOS's reset path, and a PicoMEM
; Sound Blaster takes one per 128-byte block: this is what that costs
; (KERN-MEGA-PLAN 10.3). DS = KDATA. Clobbers AX, DX
mega_rfsec:
    push bx
    cmp byte [mega_rfhold], 0
    je .open
    dec byte [mega_rfhold]
    jmp short .clr
.open:
    mov bx, [mega_rfn]
    cmp bx, 20
    jb .clr
    mov ax, [mega_rfsum]
    mov dx, [mega_rfsum+2]
    cmp dx, bx                      ; the quotient must fit a word
    jae .clr
    div bx                          ; AX = PIT counts a reflection
    mov dx, 838                     ; ...x 0.838 us
    mul dx
    mov bx, 1000
    cmp dx, bx
    jae .clr
    div bx
    mov [mega_rfus], ax
    mov ax, [mega_rfn]
    mov [mega_rfr], ax
    or byte [mega_tellq], 8 | 16    ; RFL, and the PicoMEM's line after it
    mov byte [mega_rfhold], 15
    mov ax, [mega_pmq]
    mov [mega_pmqs], ax
    mov ax, [mega_pmq+2]
    mov [mega_pmqs+2], ax
    mov ax, [mega_pmq+4]
    mov [mega_pmqs+4], ax
.clr:
    xor ax, ax
    mov [mega_pmq], ax
    mov [mega_pmq+2], ax
    mov [mega_pmq+4], ax
    mov [mega_rfn], ax
    mov [mega_rfsum], ax
    mov [mega_rfsum+2], ax
    pop bx
    ret

; mega_pmpre / mega_pmpost - an IRQ reflection's PicoMEM state, for the bar.
; The card's multiplexer works off a request list in its shared RAM, at ROM
; segment + 4356h (the firmware's IRQ_VAR_OFFS, 356h, in the 16K the BIOS
; maps after itself): one byte per source, [0] a DMA copy, [1] an audio IRQ
; (its Sound Blaster's), [2]-[5] network, mouse, keyboard, PC command. The
; ROM segment is the IVT handler's. Before: which sources were pending -
; counted [0] copies, [1] audio, [2] other - and the IMR the ROM will see
; [4], since its audio stub raises the SB's IRQ only on an unmasked line.
; After: an audio request that left no SB line caught [3] is the failure.
; DS = KDATA, interrupts off. Preserves everything
mega_pmpre:
    push ax
    push bx
    push es
    mov byte [mega_pmaud], 0
    mov ax, MEGA_SEL_LOW0
    mov es, ax
    mov bl, [mega_rv]
    xor bh, bh
    shl bx, 2
    mov ax, [es:bx+2]               ; the handler's segment
    mov word [mega_pmseg], 0
    cmp ax, 0xC000
    jb .x
    cmp ax, 0xF000
    jae .x
    test al, 7                      ; an identity selector only on the grid
    jnz .x
    mov [mega_pmseg], ax
    mov es, ax
    cmp byte [es:0x4356], 0         ; a DMA copy
    je .n0
    inc byte [mega_pmq]
.n0:
    cmp byte [es:0x4357], 0         ; an audio IRQ
    je .n1
    inc byte [mega_pmq+1]
    mov byte [mega_pmaud], 1
.n1:
    mov ax, [es:0x4358]             ; anything else
    or ax, [es:0x435A]
    jz .n2
    inc byte [mega_pmq+2]
.n2:
    in al, 0x21
    mov [mega_pmq+4], al
.x:
    pop es
    pop bx
    pop ax
    ret
mega_pmpost:
    cmp byte [mega_pmaud], 0
    je .x
    cmp byte [mega_irqpend], 0      ; asked for its SB's IRQ, and nothing
    jne .x                          ; of ours was raised
    inc byte [mega_pmq+3]
.x:
    ret

; mega_pit - AX = PIT channel 0's count, latched. Interrupts must be off
mega_pit:
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    ret

%endif

; -----------------------------------------------------------------------------
; mega_k09 - the ROM's half of IRQ1: scancode set 1 (the 8042 translates) to
; the BIOS's own buffer at 40:1E, with the shift state at 40:17, so that
; mega_i16 - and a reflected int 16h, were there ever one - read the same
; thing. Reached through kbm_isr's `jmp far [cs:kbm_old9]`.
; -----------------------------------------------------------------------------
mega_k09:
    pusha
    push ds
    push MEGA_SEL_KDATA             ; NOT port 60h: kbm_isr, the IDT's IRQ1
    pop ds                          ; gate, read it already and left the byte
    mov al, [kbm_kbyte]             ; here (SPEC.md 9.7.2) - one read, so an
    push MEGA_BDA                   ; emulated 8042 that loads its next byte
    pop ds                          ; on a read cannot lose one
    cmp al, 0xE0
    jne .note0
    or byte [0x96], 0x02            ; the ROM's own E0 flag (40:96 bit 1)
    jmp .eoi
.note0:
    cmp al, 0xE1                    ; Pause: swallowed
    je .eoi
    mov ah, [0x96]
    and byte [0x96], 0xFD           ; E0 is spent by this code either way
    mov bl, al
    and bl, 0x7F                    ; BL = the key
    mov bh, 0
    ; --- the shift keys, make and break ---
    mov cl, 0x02                    ; left shift
    cmp bl, 0x2A
    je .shift
    mov cl, 0x01                    ; right shift
    cmp bl, 0x36
    je .shift
    mov cl, 0x04                    ; ctrl, either
    cmp bl, 0x1D
    je .shift
    mov cl, 0x08                    ; alt, either
    cmp bl, 0x38
    je .shift
    test al, 0x80
    jz .make
    mov cl, 0x40                    ; a LOCK key's break: no longer held
    cmp bl, 0x3A                    ; (40:18, which is what stops a typematic
    je .lockup                      ; repeat toggling it again)
    mov cl, 0x20
    cmp bl, 0x45
    je .lockup
    mov cl, 0x10
    cmp bl, 0x46
    jne .eoi                        ; every other break is nothing
.lockup:
    not cl
    and [0x18], cl
    jmp .eoi
.make:
    mov cl, 0x40                    ; caps lock
    cmp bl, 0x3A
    je .toggle
    mov cl, 0x20                    ; num lock
    cmp bl, 0x45
    je .toggle
    mov cl, 0x10                    ; scroll lock
    cmp bl, 0x46
    je .toggle
    test ah, 0x02
    jz .plain
    ; --- an E0 key: the grey cluster, keypad Enter and keypad / ---
    mov al, 0xE0
    cmp bl, 0x1C
    jne .e0slash
    mov ax, 0xE00D
    jmp .store
.e0slash:
    cmp bl, 0x35
    jne .e0grey
    mov ax, 0xE02F
    jmp .store
.e0grey:
    test byte [0x17], 0x08          ; alt + grey = (scan + 50h) << 8
    jz .e0ctrl
    add bl, 0x50
    mov ah, bl
    xor al, al
    jmp .store
.e0ctrl:
    mov ah, bl
    jmp .store
.shift:                             ; E0 2A / E0 AA are the keyboard's fake
    test ah, 0x02                   ; shifts around a grey key: ignored
    jz .shreal
    cmp bl, 0x2A
    je .eoi
    cmp bl, 0x36
    je .eoi
.shreal:
    test al, 0x80
    jnz .shbreak
    or [0x17], cl
    jmp .eoi
.shbreak:
    not cl
    and [0x17], cl
    jmp .eoi
.toggle:
    test [0x18], cl                 ; held: a repeat, not a press
    jnz .eoi
    or [0x18], cl
    xor [0x17], cl
    call mega_leds                  ; ...and the light, which is the HOST's
    jmp .eoi                        ; to set: a keyboard never sets its own
.plain:
    cmp bl, 0x58
    ja .eoi
    mov dl, [0x17]
    ; Ctrl+Alt+Del: the ROM's warm boot, by the 8042's reset line
    mov dh, dl
    and dh, 0x0C
    cmp dh, 0x0C
    jne .noreset
    cmp bl, 0x53
    jne .noreset
    mov word [0x72], 0x1234
    call mm_kbw
    mov al, 0xFE
    out 0x64, al
.hang:
    hlt
    jmp .hang
.noreset:
    mov si, bx                      ; SI = the key, for the tables
    cmp bl, 0x3B                    ; F1-F10: 3B-44
    jb .chars
    cmp bl, 0x44
    jbe .fkey
    cmp bl, 0x57                    ; F11, F12: 85h, 86h
    jb .pad
    add bl, 0x85 - 0x57
    mov ah, bl
    test dl, 0x08
    jz .f1112s
    add ah, 6                       ; alt: 8Bh, 8Ch
    jmp .f1112o
.f1112s:
    test dl, 0x04
    jz .f1112h
    add ah, 4                       ; ctrl: 89h, 8Ah
    jmp .f1112o
.f1112h:
    test dl, 0x03
    jz .f1112o
    add ah, 2                       ; shift: 87h, 88h
.f1112o:
    xor al, al
    jmp .store
.fkey:
    mov ah, bl
    test dl, 0x08
    jz .fctrl
    add ah, 0x68 - 0x3B             ; alt: 68h-71h
    jmp .fout
.fctrl:
    test dl, 0x04
    jz .fshift
    add ah, 0x5E - 0x3B             ; ctrl: 5Eh-67h
    jmp .fout
.fshift:
    test dl, 0x03
    jz .fout
    add ah, 0x54 - 0x3B             ; shift: 54h-5Dh
.fout:
    xor al, al
    jmp .store
.pad:                               ; 45-56: the keypad
    cmp bl, 0x47
    jb .chars
    cmp bl, 0x53
    ja .chars
    mov dh, dl
    and dh, 0x20                    ; num lock...
    test dl, 0x03
    jz .padnl
    xor dh, 0x20                    ; ...xor shift
.padnl:
    mov ah, bl
    xor al, al
    or dh, dh
    jz .store                       ; a navigation key: ASCII 0
    mov al, [cs:mega_kpad + si - 0x47]
    jmp .store
.chars:
    cmp bl, 0x39                    ; past SPACE the tables have nothing
    ja .eoi
    mov ah, bl
    test dl, 0x08
    jz .ctrl
    cmp bl, 0x1C                    ; ALT+ENTER IS NOT TYPED (14.119): no
    je .eoi                         ; ROM's AH=00h hands it over, and here
                                    ; it read as KEY_ALTENTER itself, so every
                                    ; typematic repeat was another toggle and
                                    ; a bracket's own drain took it for Enter.
                                    ; kbd_ae's latch, an edge, carries the key
    xor al, al                      ; alt + a key: ASCII 0, the scan code
    cmp bl, 0x02                    ; ...and alt + 1-0, - and = are 78h-83h
    jb .store
    cmp bl, 0x0D
    ja .store
    add ah, 0x78 - 0x02
    jmp .store
.ctrl:
    mov al, [cs:mega_kbase + si]
    test dl, 0x04
    jz .shf
    cmp al, 'a'
    jb .ctrl2
    cmp al, 'z'
    ja .ctrl2
    sub al, 0x60                    ; ctrl + a letter: 1-26
    jmp .store
.ctrl2:
    cmp al, 0x0D
    je .store                       ; ctrl+Enter keeps its CR
    cmp al, 0x08
    jne .ctrl3
    mov al, 0x7F
    jmp .store
.ctrl3:
    ; ...and the rest of the ROM's control codes (KERN-MEGA-PLAN 14.110):
    ; ctrl + [ \ ] are 1Bh 1Ch 1Dh - ^] is Telnet's way into full screen and
    ; out (apps/telnet/tetxt.inc) - ctrl + 6 and - are 1Eh and 1Fh, and Esc
    ; and SPACE keep their ASCII. Every other ctrl key is ASCII 0
    cmp al, '['
    jb .ctrl4
    cmp al, ']'
    ja .ctrl4
    sub al, 0x40
    jmp .store
.ctrl4:
    cmp al, 0x1B
    je .store
    cmp al, ' '
    je .store
    mov ah, 0x1E
    cmp al, '6'
    je .ctrl5
    mov ah, 0x1F
    cmp al, '-'
    je .ctrl5
    mov ah, 0                       ; (AH is the scan code again below)
.ctrl5:
    mov al, ah
    mov ah, bl
    jmp .store
.shf:
    mov dh, dl
    and dh, 0x03                    ; shift...
    cmp al, 'a'
    jb .shf2
    cmp al, 'z'
    ja .shf2
    test dl, 0x40                   ; ...xor caps, for a letter
    jz .shf2
    xor dh, 0x03
.shf2:
    or dh, dh
    jz .store
    mov al, [cs:mega_kshift + si]
.store:
    or ax, ax
    jz .eoi
    mov bx, [0x1C]                  ; the tail, and the slot after it
    mov si, bx
    inc si
    inc si
    cmp si, [0x82]
    jb .wrap
    mov si, [0x80]
.wrap:
    cmp si, [0x1A]
    je .eoi                         ; full: the ROM beeps, this drops
    mov [bx], ax
    mov [0x1C], si
.eoi:
    mov al, 0x20
    out 0x20, al
    pop ds
    popa
    iret

; the unshifted and the shifted ASCII for scan codes 00h-39h (SPACE is 39h),
; and the keypad's digits for 47h-53h. Anything past 39h but the keypad and
; the function keys has no ASCII on the ROM either
mega_kbase:
    db 0, 0x1B, '1234567890-=', 0x08, 0x09
    db 'qwertyuiop[]', 0x0D, 0
    db 'asdfghjkl;', "'", '`', 0, '\'
    db 'zxcvbnm,./', 0, '*', 0, ' '
mega_kshift:
    db 0, 0x1B, '!@#$%^&*()_+', 0x08, 0
    db 'QWERTYUIOP{}', 0x0D, 0
    db 'ASDFGHJKL:"~', 0, '|'
    db 'ZXCVBNM<>?', 0, '*', 0, ' '
mega_kpad:
    db '789-456+1230.'
%if mega_kshift - mega_kbase != 0x3A || mega_kpad - mega_kshift != 0x3A
 %error "mega_k09's tables are one byte a scan code, 00h-39h"
%endif

; -----------------------------------------------------------------------------
; mega_i16 - int 16h, natively, over the buffer mega_k09 fills
; AH = 00h/10h read (waits), 01h/11h peek (ZF = 1 empty), 02h/12h the shift
; state, 05h store CX. Anything else returns with AX unchanged, which is what
; a ROM without the function does.
; -----------------------------------------------------------------------------
mega_i16:
    push bp
    mov bp, sp                      ; [bp+2] IP, [bp+4] CS, [bp+6] FLAGS
    push bx
    push si
    push ds
    mov bx, MEGA_BDA
    mov ds, bx
    cmp ah, 0x00
    je .get
    cmp ah, 0x10
    je .get
    cmp ah, 0x01
    je .peek
    cmp ah, 0x11
    je .peek
    cmp ah, 0x02
    je .shift
    cmp ah, 0x12
    je .shift
    cmp ah, 0x05
    je .put
    jmp .out
.get:
    mov si, ax                      ; SI's high byte = the function
.wait:
    cli
    mov bx, [0x1A]
    cmp bx, [0x1C]
    jne .have
    sti
    hlt                             ; the tick or the key wakes it
    jmp .wait
.have:
    mov ax, [bx]
    inc bx
    inc bx
    cmp bx, [0x82]
    jb .gw
    mov bx, [0x80]
.gw:
    mov [0x1A], bx
    sti
    test si, 0xF000                 ; AH = 00h: an E0 ASCII reads as 0
    jnz .out
    cmp al, 0xE0
    jne .out
    xor al, al
    jmp .out
.peek:
    mov si, ax
    mov bx, [0x1A]
    cmp bx, [0x1C]
    jne .pfull
    or word [bp+6], 0x0040          ; ZF = 1: empty
    jmp .out
.pfull:
    and word [bp+6], ~0x0040
    mov ax, [bx]
    test si, 0xF000
    jnz .out
    cmp al, 0xE0
    jne .out
    xor al, al
    jmp .out
.shift:
    mov al, [0x17]
    cmp ah, 0x12
    jne .out
    mov ah, [0x18]
    jmp .out
.put:
    mov bx, [0x1C]
    mov si, bx
    inc si
    inc si
    cmp si, [0x82]
    jb .pw
    mov si, [0x80]
.pw:
    cmp si, [0x1A]
    mov al, 1                       ; AL = 1: full
    je .out
    mov [bx], cx
    mov [0x1C], si
    xor al, al
.out:
    pop ds
    pop si
    pop bx
    pop bp
    iret

; -----------------------------------------------------------------------------
; the BIOS's software services, 10h-1Fh: sixteen stubs of eight bytes, each
; `push imm8 vector / jmp near mega_svc / three bytes of padding`
; -----------------------------------------------------------------------------
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_ss:
%assign mega_v 0x10
%rep 16
    push byte mega_v
    jmp near mega_svc
    times 3 nop
  %assign mega_v mega_v + 1
%endrep
%if $ - mega_ss != 16*8
 %error "mega_ss is eight bytes a stub"
%endif

; the exceptions, 00h-0Fh, the same eight bytes. 8 and 10-14 arrive with an
; error code under the return address; the rest do not, and their stub pushes
; a 0 in its place, so mega_fault always sees [vector] [error] [IP] [CS] [FLAGS]
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_xs:
%assign mega_v 0
%rep 16
 %if mega_v == 8 || (mega_v >= 10 && mega_v <= 14)
    push byte mega_v
    jmp near mega_fault
    times 3 nop
 %else
    push byte 0
    push byte mega_v
    jmp near mega_fault
    nop
 %endif
  %assign mega_v mega_v + 1
%endrep
%if $ - mega_xs != 16*8
 %error "mega_xs is eight bytes a stub"
%endif

mega_unh:                           ; a software interrupt nothing handles
    push byte 0                     ; (the vector is not known here; the
    push byte -1                    ; reporter prints FF)
    jmp mega_fault

mega_eoi2:                          ; an IRQ the ROM owned and nothing now
    push ax                         ; does: dismissed
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    pop ax
    iret
mega_eoi1:
    push ax
    mov al, 0x20
    out 0x20, al
    pop ax
    iret

; -----------------------------------------------------------------------------
; mega_nmi - vector 2, which real mode gives the BIOS and this IDT gave the
;            fault reporter: the Victory 286 stopped on it mid-copy, C: to B:
;
; The AT BIOS's handler is the model. Port 61h says which line latched - bit 7
; a parity check, bit 6 an I/O channel check - and neither is a spurious NMI,
; which it dismisses. A latched one it reports, and so does a MEGADIAG=1
; build (KERN-MEGA-PLAN 14.65; the shipped one says nothing): [mega_nmi61] and
; the count go to the menu bar from ui_task (mega_nmitell), where drawing is
; legal, since an NMI can land inside anything. The latch is cleared by
; pulsing the source's disable bit (61h bit 2 parity, bit 3 channel check),
; and after MEGA_NMIMAX of them both stay off - the BIOS's own "shut off NMI"
; answer - so a line held asserted cannot loop us here. Bits 4-7 read status
; and ignore a write; bits 0-1 are the speaker's and are written back as read
; -----------------------------------------------------------------------------
MEGA_NMIMAX equ 8
mega_nmi:
    push ax
    push ds
    mov ax, MEGA_SEL_KDATA          ; (an IDT gate: protected mode only)
    mov ds, ax
    in al, 0x61
    test al, 0xC0
    jz .out                         ; spurious: nothing latched
    mov [mega_nmi61], al
%ifdef MEGA_DIAG
    push bp                         ; WHERE it landed, which a parity NMI
    mov bp, sp                      ; reaches a few cycles after the read
    mov ax, [bp+6]                  ; that tripped it: [bp+6] IP, [bp+8] CS
    mov [mega_nmiip], ax
    mov ax, [bp+8]
    mov [mega_nmics], ax
    pop bp
    mov al, [mega_fdbusy]           ; ...and if a native floppy transfer was
    mov [mega_nmifd], al            ; in flight, its DMA: the CPU sits in
    or al, al                       ; mega_fdirq's hlt then, so the read that
    jz .nofd                        ; tripped parity was the 8237's
    mov ax, [mega_fdlin]
    mov [mega_nmilin], ax
    mov al, [mega_fdlin+2]
    mov [mega_nmilin+2], al
    mov al, [mega_fdop]
    mov [mega_nmiop], al
    mov al, [mega_fdn]
    mov [mega_nmisn], al
.nofd:
    or byte [mega_tellq], 1         ; ...and on the menu bar (MEGADIAG=1)
%endif
    mov al, [mega_nmi61]
    inc byte [mega_nmin]
    and al, 0x0F                    ; the writable half, as it reads
    mov ah, al
    or al, 0x0C                     ; both sources off: clears both latches
    out 0x61, al
    cmp byte [mega_nmin], MEGA_NMIMAX
    jae .out                        ; ...and they stay off
    mov al, ah
    out 0x61, al                    ; ...or back as they were
.out:
    pop ds
    pop ax
    iret

; mega_tell - ui_task, with nothing held: what an interrupt or a reset path
; could only note, on the menu bar ([mega_tellq] bit 0 an NMI, bit 1 the
; floppy's BDA bytes moved across a 286 reset; bit 7 is not a request).
; **Bit 6, a package closed for faulting, is the only one a shipped kern_mega
; sets** - the rest are MEGADIAG=1's field readings (KERN-MEGA-PLAN 14.65)
mega_tell:
    test byte [mega_tellq], 0x40    ; a package that faulted: closed first,
    jz .nofk                        ; and its toast goes over whatever is up
    call mega_fkdo                  ; (KERN-MEGA-PLAN 14.42)
.nofk:
%ifdef MEGA_DIAG
    cmp word [toast_on], 0          ; one line at a time: a toast up (or
    je .go                          ; wanted) keeps the rest queued
    ret
.go:
    test byte [mega_tellq], 1
    jz .nonmi
    and byte [mega_tellq], 0xFE
    jmp mega_nmitell
.nonmi:
    test byte [mega_tellq], 2
    jz .x
    and byte [mega_tellq], 0xFD
    pusha
    push es
    mov di, mega_rsts + 7           ; "RST 3E aa>bb 3F cc>dd"
    mov al, [mega_fdpre]
    call mega_hexb
    inc di
    mov al, [mega_fdpost]
    call mega_hexb
    add di, 4
    mov al, [mega_fdpre+1]
    call mega_hexb
    inc di
    mov al, [mega_fdpost+1]
    call mega_hexb
    mov si, mega_rsts
    jmp mega_nmitell.say
.x:
    test byte [mega_tellq], 8
    jz .x3
    and byte [mega_tellq], 0xF7
    pusha
    push es
    mov di, mega_rfs + 4            ; "RFL rrrr/s uuuu"
    mov al, [mega_rfr+1]
    call mega_hexb
    mov al, [mega_rfr]
    call mega_hexb
    add di, 3
    mov al, [mega_rfus+1]
    call mega_hexb
    mov al, [mega_rfus]
    call mega_hexb
    mov si, mega_rfs
    jmp mega_nmitell.say
.x3:
    test byte [mega_tellq], 16
    jz .x4
    and byte [mega_tellq], 0xEF
    pusha
    push es
    mov di, mega_pms + 2            ; "Q cc ss oo bb vv rr"
    mov si, mega_pmqs
    mov cx, 6
.q:
    lodsb
    push cx
    call mega_hexb
    pop cx
    inc di
    loop .q
    mov si, mega_pms
    jmp mega_nmitell.say
.x4:
    test byte [mega_tellq], 4
    jz .y
    and byte [mega_tellq], 0xFB
    pusha
    push es
    mov di, mega_fdrs + 12          ; "FDC RESET R nn F nn"
    mov al, [mega_fdrst]
    call mega_hexb
    add di, 3
    mov al, [mega_fdrst+1]
    call mega_hexb
    mov si, mega_fdrs
    jmp mega_nmitell.say
.y:
    test byte [mega_tellq], 0x20    ; the DOS park's trip (megados.inc)
    jz .z
    and byte [mega_tellq], 0xDF
    jmp mega_dtell
.z:
%endif
    ret

%ifdef MEGA_DIAG
; mega_nmitell - the NMI on the menu bar, once per batch. "NMI 61h=XX x N"
; - the port says which line; N how many so far
mega_nmitell:
    pusha
    push es
    cmp byte [mega_nmifd], 0
    je .cs
    mov di, mega_nmid + 4           ; "NMI ss W llllll nn": the floppy DMA
    mov al, [mega_nmi61]
    call mega_hexb
    mov al, 'R'
    cmp byte [mega_nmiop], 3
    jne .op
    mov al, 'W'
.op:
    mov [di+1], al
    add di, 3
    mov al, [mega_nmilin+2]
    call mega_hexb
    mov al, [mega_nmilin+1]
    call mega_hexb
    mov al, [mega_nmilin]
    call mega_hexb
    inc di
    mov al, [mega_nmisn]
    call mega_hexb
    mov si, mega_nmid
    jmp short .say
.cs:
    mov di, mega_nmis + 4           ; "NMI ss xnn cccc:iiii"
    mov al, [mega_nmi61]
    call mega_hexb
    add di, 2
    mov al, [mega_nmin]
    call mega_hexb
    inc di
    mov al, [mega_nmics+1]
    call mega_hexb
    mov al, [mega_nmics]
    call mega_hexb
    inc di
    mov al, [mega_nmiip+1]
    call mega_hexb
    mov al, [mega_nmiip]
    call mega_hexb
    mov si, mega_nmis
.say:
    push ds
    pop es
    mov cx, 182                     ; ten seconds: long enough to photograph
    call KERNEL_SEG:cw_toast_show
    call KERNEL_SEG:mega_putsf      ; ...and the debug port (the line is the
                                    ; kernel's DATA, which `.mega`'s CS cannot
                                    ; read: the low writer does), with how many
    mov al, ' '                     ; 286 resets the session has made
    call mm_putc
    mov ax, [mega_rstn]
    call mm_hex4
    mov al, 10
    call mm_putc
    pop es
    popa
    ret

; mega_hexb - AL as two hex digits at [DI], DI past them. Clobbers AX
mega_hexb:
    mov ah, al
    shr al, 4
    call .nib
    mov al, ah
.nib:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 'A' - '9' - 1
.put:
    mov [di], al
    inc di
    ret
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_nmis   db "NMI ss xnn cccc:iiii", 0
mega_nmid   db "NMI ss W llllll nn", 0
mega_rsts   db "RST 3E aa>bb 3F cc>dd", 0
mega_fdrs   db "FDC RESET R nn F nn", 0
mega_rfs    db "RFL rrrr/s uuuu", 0
mega_pms    db "Q cc ss oo bb vv rr", 0
mega_rfdiv  db 18                   ; ticks to the next mega_rfsec
mega_fdpre  dw 0                    ; 40:3E/3F before a 286 reset...
mega_fdpost dw 0                    ; ...and the first different after one
%endif ; MEGA_DIAG

; -----------------------------------------------------------------------------
; mega_fault - report and stop
; in:       [SP] = vector, [SP+2] = error code (0 where the CPU gives none),
;           then the IP, CS and FLAGS the fault pushed
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_fault:
    cli
    pusha                           ; [SP] DI SI BP SP BX DX CX AX, then the
    push ds                         ; vector
    push es
    mov bp, sp
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
%ifdef MEGA_TRAPLOG
    cmp byte [ss:bp+20], 0x0D       ; MEGATRAP=1: every #GP to E9 before it is
    jne .nolog                      ; fixed up or reported (mega_tlog)
    call mega_tlog
.nolog:
%endif
    cmp byte [ss:bp+20], 0x0D
    je mega_gpfix                   ; a far transfer to an image's DATA segment
    cmp byte [ss:bp+20], 0x01
    je mega_db                      ; mega_null0's step
mega_report:                        ; is finished there, not reported
    call mega_fkill                 ; a PACKAGE's fault closes the package and
                                    ; does not come back (KERN-MEGA-PLAN 14.42)
    mov si, mega_mflt
    call mm_puts
    mov ax, [ss:bp+20]              ; the vector
    call mm_hex2
    mov si, mega_mfst
    call mm_puts
    mov cx, 16                      ; sixteen words of whatever the CPU left: the
    lea bx, [bp+22]                 ; error code (or IP), IP, CS, FLAGS, ...
.st:
    mov ax, [ss:bx]
    call mm_hex4
    mov al, ' '
    call mm_putc
    inc bx
    inc bx
    loop .st
    mov si, mega_mreg
    call mm_puts
    mov cx, 10                      ; ES DS DI SI BP SP BX DX CX AX
    mov bx, bp
.rg:
    mov ax, [ss:bx]
    call mm_hex4
    mov al, ' '
    call mm_putc
    inc bx
    inc bx
    loop .rg
    mov ax, ss
    call mm_hex4
    mov al, 10
    call mm_putc
    ; --- ...and on the GLASS, for a machine with no debug port: one line
    ; under the menu bar, through font_run, the way KFZTRACE's strip is
    ; drawn (sched.inc). Once only: a fault inside the drawing comes back
    ; here, and the second time the E9 line above is all there is
    cmp byte [mega_infault], 0
    jne .dead
    mov byte [mega_infault], 1
    push ds
    pop es
    mov di, mega_fbuf + MEGA_FB_V
    mov ax, [ss:bp+20]              ; the vector
    call mega_bhex2
    mov di, mega_fbuf + MEGA_FB_E
    mov ax, [ss:bp+22]              ; the error code
    call mega_bhex4
    mov di, mega_fbuf + MEGA_FB_C
    mov ax, [ss:bp+26]              ; CS
    call mega_bhex4
    mov di, mega_fbuf + MEGA_FB_I
    mov ax, [ss:bp+24]              ; IP
    call mega_bhex4
    mov di, mega_fbuf + MEGA_FB_S
    mov ax, [ss:bp+10]              ; SP as pusha saw it
    call mega_bhex4
    mov byte [gfx_lock_flag], 0     ; whatever the fault interrupted, nobody
    mov word [wm_clip_n], 0         ; is drawing now
    mov byte [gfx_dis], 0
    mov cx, 8
    mov dx, MBAR_H + 8
    mov si, mega_fbuf
    mov ax, (CWHITE << 8) | CBLACK
    call KERNEL_SEG:cw_font_run
.dead:
    hlt
    jmp .dead

; -----------------------------------------------------------------------------
; mega_fkill - A FAULT IN A PACKAGE CLOSES THE PACKAGE (KERN-MEGA-PLAN 14.42)
;
; mega_report's first act. Returns when the fault is the kernel's - or nobody's
; it can name - and today's screen follows; never returns when it is a
; package's, which it closes. In: BP = mega_fault's frame (ES DS DI SI BP SP BX
; DX CX AX, vector, error, IP, CS, FLAGS at [ss:bp]).
;
; WHOSE: the faulting CS is a package's code (an LDT alias or a pair's code
; half: mega_cs2ds gives its data segment S), and S is a package instance's
; region, the region being loaded ([ld_base]), or inside a claim an instance
; owns (an overlay, a part). A GDT CS - the kernel, `.cold`, `.mega`, a BIOS -
; is never a package's, and a driver's or a module's image is no instance's.
; A kernel routine faulting on a pointer a package handed it stays FATAL: the
; kernel frame that faulted may hold a lock or be half way through a table,
; and nothing records which.
;
; WHERE TO: the running task's innermost CATCH (mega_pcin_*), which
; wm_pkgcall and the loader's entry call push around every far call into a
; package. The catch must name the same instance and be ABOVE the fault on the
; stack; then the package's frames are dropped and the call returns CF = 1 as
; if the callback had refused - the kernel frames under it are intact, because
; nothing between the catch and the fault is kernel code. With no catch, a
; package's own WORKER is killed outright through inst_task_die. Anything else
; (the UI task outside a callback, the FSXF_RATE hook inside IRQ0, the
; scheduler locked) is fatal.
;
; NO LOOPS: a fault in an instance already being closed is the same incident
; (at most MEGA_FKCAS of them); a fault in ANY package while another is being
; closed, or within MEGA_FKGAP ticks of the last incident's start, is fatal.
; -----------------------------------------------------------------------------
MEGA_FKINST equ 12                  ; INST_MAX and TOAST_MAX + 1, which are
MEGA_FKMSG  equ 25                  ; defined after this file (kernel.asm checks)
MEGA_FKCAS  equ 8                   ; faults one incident may take
MEGA_FKGAP  equ 55                  ; ticks (3 s) before another may begin
MEGA_FKVEC  equ 0x38F9              ; vectors 0 3 4 5 6 7 0B 0C 0D: the ones a
                                    ; program's own instruction raises
mega_fkill:
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov byte [mega_n0], 0           ; a reported fault never finishes a step of
                                    ; mega_null0's: none is pending after it
    mov ax, LOW_SEG
    mov es, ax                      ; ES = .lowbss throughout
    mov bx, [ss:bp+20]              ; --- the vector: a program's own
    cmp bx, 0xFFFF                  ; (FF: a software interrupt nobody
    je .vok                         ; handles, mega_unh)
    cmp bx, 16
    jae .ret
    mov cl, bl
    mov ax, MEGA_FKVEC
    shr ax, cl
    test al, 1
    jz .ret
.vok:
    cmp word [sch_rbusy], 0         ; the rate hook in flight, or the
    jne .ret                        ; scheduler locked ([sch_lock], the next)
    mov ax, [ss:bp+26]              ; --- the code: a package's
    test al, 4
    jz .ret                         ; a GDT CS
    call mega_cs2ds                 ; AX = S
    jc .ret
    call mega_fown                  ; DI = the record, DX = its region
    jc .ret
    ; --- the catch
    mov bl, [sch_cur]
    xor bh, bh
    add bx, bx
    mov si, [es:bx+mega_pcj]
    or si, si
    jz .nocatch
    mov ax, [es:si-4]               ; the far return into the kernel that the
    mov cx, [es:si-2]               ; call pushed just under the catch
    cmp ax, ..@mega_pcret_w
    jne .catl
    cmp cx, KERNEL_SEG
    je .catok
.catl:
    cmp ax, ..@mega_pcret_l
    jne .ret                        ; overwritten: no way home
    cmp cx, COLD_SEG
    jne .ret
.catok:
    mov ax, ss                      ; ...and the fault is above it on the
    cmp ax, LOW_SEG                 ; same stack (a package on a stack of its
    jne .catst                      ; own is unwound all the same)
    cmp bp, si
    jae .ret
.catst:
    mov ax, [es:si+6]               ; the image the catch called into: the
    cmp ax, dx                      ; faulting one's region, or one of its
    je .catme                       ; overlays or parts
    push di
    push dx
    call mega_fown                  ; DX = ITS region
    mov ax, dx
    pop dx
    pop di
    jc .ret                         ; (POP and MOV keep CF)
    cmp ax, dx
    jne .ret
.catme:
    mov cl, 1                       ; CL = 1 unwind
    jmp short .inc
.nocatch:
    mov al, [sch_cur]               ; no catch: only the instance's own
    or al, al                       ; worker may be killed
    jz .ret
    cmp al, [di+I_TASK]
    jne .ret
    mov ax, ss                      ; ...on its own stack
    cmp ax, LOW_SEG
    jne .ret
    xor cl, cl                      ; CL = 0 the worker
    ; --- the incident
.inc:
    mov ax, di
    sub ax, inst_tab
    shr ax, 5
    inc ax                          ; AL = slot + 1
    cmp byte [es:mega_fkslot], 0
    je .new
    cmp al, [es:mega_fkslot]        ; one is being closed: this must be it
    jne .ret
    cmp dx, [es:mega_fks]
    jne .ret
    inc byte [es:mega_fkn]
    cmp byte [es:mega_fkn], MEGA_FKCAS
    ja .ret
    jmp short .go
.new:
    cmp byte [es:mega_fkn], 0       ; the first ever, or long enough after
    je .first                       ; the last
    mov bx, [ticks]
    sub bx, [es:mega_fkt]
    cmp bx, MEGA_FKGAP
    jb .ret
.first:
    mov [es:mega_fkslot], al
    mov [es:mega_fks], dx
    mov bx, [ticks]
    mov [es:mega_fkt], bx
    mov byte [es:mega_fkn], 1
    push cx
    call mega_fkmsg_mk              ; the toast and the E9 line, once
    pop cx
.go:
    or byte [mega_tellq], 0x40      ; ui_task closes it and says so
    cmp byte [di+I_STATE], 1        ; dying from here: its own API calls
    jne .st                         ; refuse, its worker leaves at its next
    mov byte [di+I_STATE], 2        ; OSAPI_TASK_ALIVE
.st:
    or cl, cl
    jz .worker
    cmp word [es:si-4], ..@mega_pcret_l
    je .unwind                      ; the loader aborts the launch itself
    mov bx, di
    sub bx, inst_tab
    shr bx, 4                       ; the slot x 2
    mov [es:bx+mega_fkq], dx        ; a callback: ui_task tears it down
.unwind:
    xor dl, dl                      ; DL = 1: the callback took the gfx lock,
    mov al, [sch_cur]               ; and nothing above will give it back
    cmp byte [gfx_lock_flag], 0
    je .nolk
    cmp al, [gfx_lock_own]
    jne .nolk
    cmp byte [es:si+4], 0           ; held at the call...
    je .lk
    cmp al, [es:si+5]               ; ...by us: the caller's, it stays
    je .nolk
.lk:
    inc dx
.nolk:
    mov bx, [es:si+2]               ; the callback's SI, as it was handed
    mov ax, LOW_SEG
    cli
    mov ss, ax
    lea sp, [si-4]                  ; the far return, on top
    sti
    push bx
    or dl, dl
    jz .ul
    mov bp, gfx_unlock
    call KERNEL_SEG:mega_tcall
.ul:
    pop si
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov es, ax
    xor ax, ax
    xor bx, bx
    xor cx, cx
    xor dx, dx
    xor di, di
    xor bp, bp
    cld
    stc                             ; "refused": an entry proc's abort
    retf
.worker:
    lea ax, [bp+30]                 ; the worker's SP at the fault
    cli
    mov sp, ax
    sti
    mov al, [sch_cur]
    cmp byte [gfx_lock_flag], 0
    je .wnl
    cmp al, [gfx_lock_own]
    jne .wnl
    mov bp, gfx_unlock              ; inst_task_die takes the lock itself
    call KERNEL_SEG:mega_tcall
.wnl:
    mov ax, MEGA_SEL_KDATA
    mov es, ax
    cld
    jmp KERNEL_SEG:inst_task_die    ; DI = the record; never returns
.ret:
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    ret

; mega_fown - AX = a data segment -> DI = the package instance it is part of,
; DX = that instance's region; CF = 1 none. A PUBLISHED record is asked
; first: [ld_base] outlives its launch, and a region the compactor has since
; moved onto that segment belongs to its own record and not to [ld_rec]. Only
; then the region being loaded ([ld_base]'s, whose record is [ld_rec] before
; step 9 publishes it). ES = .lowbss. Clobbers AX, BX, CX
mega_fown:
    mov dx, ax
    call .byseg
    jnc .ok
    mov di, [ld_rec]
    cmp ax, [ld_base]
    jne .scan
    or di, di
    jnz .ok
.scan:
    test al, 7                      ; a paragraph below the ROM: an overlay
    jnz .no                         ; or a part, inside a claim
    cmp ax, 0xA000
    jae .no
    mov bx, mem_tab
    mov cx, MEM_MAX
.w:
    mov dx, ax
    cmp word [es:bx+MC_SEG], 0
    je .wn
    sub dx, [es:bx+MC_SEG]
    jb .wn
    cmp dx, [es:bx+MC_PARA]
    jb .hit
.wn:
    add bx, MC_SIZE
    loop .w
.no:
    stc
    ret
.hit:
    mov ax, [es:bx+MC_OWN]          ; an instance's slot, or a package's
    cmp ax, INST_MAX                ; segment
    jae .seg
    shl ax, 5
    add ax, inst_tab
    mov di, ax
    cmp byte [di+I_STATE], 0
    je .no
    test byte [di+I_KIND], KIND_PKG
    jz .no
    mov dx, [di+I_SPTR]
.ok:
    clc
    ret
.seg:
    mov dx, ax
    call .byseg
    jc .no
    ret
.byseg:                             ; DX = a region -> DI its record, CF
    mov di, inst_tab
.l:
    cmp byte [di+I_STATE], 0
    je .n
    test byte [di+I_KIND], KIND_PKG
    jz .n
    cmp [di+I_SPTR], dx
    je .y
.n:
    add di, I_RECSZ
    cmp di, inst_tab + INST_MAX * I_RECSZ
    jb .l
    stc
    ret
.y:
    clc
    ret

; mega_fkmsg_mk - the toast, "NAME vv at cccc:iiii", into mega_fkmsg, and
; the E9 line "kern_mega CLOSED NAME vv at cccc:iiii err eeee". In: BP = the
; frame, DX = the region (its header's +16 is the name), ES = .lowbss.
; Clobbers AX, BX, CX
mega_fkmsg_mk:
    push si
    push di
    mov di, mega_fkmsg
    push ds
    mov ds, dx
    mov si, 16
    mov cx, 8
.nm:
    lodsb
    cmp al, ' '
    jb .ne
    cmp al, 0x7F
    jae .ne
    stosb
    loop .nm
.ne:
    pop ds
    mov al, ' '
    stosb
    mov ax, [ss:bp+20]
    call mega_bhex2
    mov ax, ' a'
    stosw
    mov ax, 't '
    stosw
    mov ax, [ss:bp+26]
    call mega_bhex4
    mov al, ':'
    stosb
    mov ax, [ss:bp+24]
    call mega_bhex4
    mov byte [es:di], 0
    mov byte [es:mega_fktm], 1
    mov si, mega_mclo
    call mm_puts
    mov si, mega_fkmsg
.e9:
    mov al, [es:si]
    inc si
    or al, al
    jz .e9d
    call mm_putc
    jmp .e9
.e9d:
    mov si, mega_mcle
    call mm_puts
    mov ax, [ss:bp+22]
    call mm_hex4
    mov al, 10
    call mm_putc
    pop di
    pop si
    ret
mega_mclo   db 10, "kern_mega CLOSED ", 0
mega_mcle   db " err ", 0

; mega_fkdo - ui_task, nothing held ([mega_tellq] bit 6): close what
; mega_fkill unwound out of, say so, and end the incident once its record is
; gone. DS = the kernel's data. Every register kept
mega_fkdo:
    pusha
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov si, mega_fkq
    mov di, inst_tab
.k:
    xor ax, ax
    xchg ax, [es:si]
    or ax, ax
    jz .kn
    cmp byte [di+I_STATE], 0
    je .kn
    test byte [di+I_KIND], KIND_PKG
    jz .kn
    cmp [di+I_SPTR], ax
    jne .kn                         ; (gone, and the slot taken again)
    call mega_fkclose
.kn:
    add si, 2
    add di, I_RECSZ
    cmp di, inst_tab + INST_MAX * I_RECSZ
    jb .k
    cmp byte [es:mega_fktm], 0
    je .nt
    mov byte [es:mega_fktm], 0
    mov si, mega_fkmsg              ; ES:SI
    mov cx, 182                     ; ten seconds, mega_nmitell's
    call KERNEL_SEG:cw_toast_show
.nt:
    mov al, [es:mega_fkslot]
    or al, al
    jz .clr
    xor ah, ah
    dec ax
    shl ax, 5
    add ax, inst_tab
    mov di, ax
    cmp byte [di+I_STATE], 0
    je .shut
    mov ax, [es:mega_fks]
    cmp [di+I_SPTR], ax
    je .keep                        ; its worker has not left yet
.shut:
    mov byte [es:mega_fkslot], 0
.clr:
    and byte [mega_tellq], 0xBF
.keep:
    pop es
    popa
    ret

; mega_fkclose - DI = a live package record: what app_close_win does to it,
; without asking it (its code is what faulted) - inst_task_die's order. With
; a worker, the die flag and the hide, and the worker leaves by itself.
%macro MEGA_TC 1
    mov bp, %1
    call KERNEL_SEG:mega_tcall
%endmacro
mega_fkclose:
    push es
    mov ax, MEGA_SEL_KDATA
    mov es, ax
    mov byte [di+I_STATE], 2
    MEGA_TC gfx_lock
    mov bx, [di+I_WIN]
    cmp byte [di+I_TASK], 0xFF
    jne .tasked
    MEGA_TC toast_owner_gone
    MEGA_TC inst_rel_rec
    mov bx, [di+I_WIN]
    MEGA_TC inst_unwin
    call COLD_SEG:desk_drop_x
    MEGA_TC gfx_unlock
    call COLD_SEG:mem_free_rec_x
    mov byte [di+I_STATE], 0
    pop es
    ret
.tasked:
    or bx, bx
    jz .u
    MEGA_TC wm_hide
.u:
    MEGA_TC gfx_unlock
    pop es
    ret

; -----------------------------------------------------------------------------
; mega_pcin_x / mega_pcin_n - push a CATCH (KERN-MEGA-PLAN 14.42) under the
; far return, and _x also wm_nest_pushd: wm_pkgcall's, and the loader's
; entry call's (_n, which needs no bracket). In: DS = the image about to be
; called, ES = the kernel's data, SI = the callee's SI, SS = LOW_SEG. Every
; register and the flags kept. The frame, at the catch SP and upward: the
; task's previous catch, SI, the gfx lock's flag/owner word at the call, the
; image, the task index x 2 - and the far call that follows puts its return
; just under it, which is where mega_fkill finds the way home. The catch is
; per TASK (mega_pcj, .lowbss): a worker reaches wm_pkgcall too.
; mega_pcout_x / mega_pcout_n - the frame off and the previous catch back
; (_x also wm_nest_pop, DS = the kernel's data). Every register and the flags.
; Both run in real mode as well (an AUTOSTART package's entry), where nothing
; reads the catch.
; -----------------------------------------------------------------------------
section .cold
mega_pcin_x:
    ; STKBALANCE-NET: +5 - mega_pcin_n's frame
    call wm_nest_pushd
mega_pcin_n:
    ; STKBALANCE-NET: +5 - leaves the catch frame on the CALLER's stack; mega_pcout_* takes it off
    push ax                         ; five words for the frame
    push ax
    push ax
    push ax
    push ax
    pushf
    push bp
    push bx
    mov bp, sp                      ; +0 BX +2 BP +4 F, +16 IP +18 CS
    mov bx, [ss:bp+16]
    mov [ss:bp+6], bx               ; the return to the top
    mov bx, [ss:bp+18]
    mov [ss:bp+8], bx
    mov [ss:bp+12], si              ; frame +2 SI
    mov bx, [es:gfx_lock_flag]
    mov [ss:bp+14], bx              ; frame +4 the lock
    mov bx, ds
    mov [ss:bp+16], bx              ; frame +6 the image
    mov bl, [es:sch_cur]
    xor bh, bh
    add bx, bx
    mov [ss:bp+18], bx              ; frame +8 the task
    push word [ss:bx+mega_pcj]
    pop word [ss:bp+10]             ; frame +0 the previous catch
    mov [ss:bx+mega_pcj], bp
    add word [ss:bx+mega_pcj], 10   ; the catch: SP once the retf is done
    pop bx
    pop bp
    popf
    retf

mega_pcout_x:
    ; STKBALANCE-NET: -5 - mega_pcout_n's
    call wm_nest_pop
mega_pcout_n:
    ; STKBALANCE-NET: -5 - takes mega_pcin_*'s catch frame off the caller's stack
    pushf
    push bp
    push bx
    mov bp, sp                      ; +0 BX +2 BP +4 F +6 IP +8 CS, +10 frame
    mov bx, [ss:bp+18]              ; the task
    push word [ss:bp+10]
    pop word [ss:bx+mega_pcj]       ; its previous catch back
    mov bx, [ss:bp+8]               ; ...and everything up ten bytes
    mov [ss:bp+18], bx
    mov bx, [ss:bp+6]
    mov [ss:bp+16], bx
    mov bx, [ss:bp+4]
    mov [ss:bp+14], bx
    mov bx, [ss:bp+2]
    mov [ss:bp+12], bx
    mov bx, [ss:bp]
    mov [ss:bp+10], bx
    lea sp, [bp+10]
    pop bx
    pop bp
    popf
    retf
section .text
mega_tcall:                         ; `.mega`'s way into a near .text routine:
    call bp                         ; BP = it (mega_fkill, mega_fkdo)
    retf
section .mega

%ifdef MEGA_TRAPLOG
; -----------------------------------------------------------------------------
; mega_tlog - MEGATRAP=1's census line (KERN-MEGA-PLAN 10.3, track 4): one E9
; line per #GP, whatever mega_gpfix, mega_gpw or mega_null0 then makes of it
;
;   T eeee cccc:iiii dddd ssss NAME
;
; the error code, the faulting CS:IP, DS and ES as the code had them, and the
; name at +16 of the image CS runs in when that image starts with the 'O8'
; magic - a package, a driver or a driver's overlay - else '-'. An error code
; that is not 0 is a far transfer (mega_gpfix); 0 with DS or ES 0 is
; mega_null0's; any other 0 is a write through a code segment (mega_gpw).
; `tools/megatraps.py` ranks the sites. A knob build only: the shipped
; kern_mega carries none of it.
; in:       BP = mega_fault's frame, DS = the kernel's data
; out:      every register as it was
; -----------------------------------------------------------------------------
mega_tlog:
    push ax
    push si
    push es
    mov al, 'T'
    call mm_putc
    mov al, ' '
    call mm_putc
    mov ax, [ss:bp+22]              ; the error code
    call mm_hex4
    mov al, ' '
    call mm_putc
    mov ax, [ss:bp+26]              ; CS
    call mm_hex4
    mov al, ':'
    call mm_putc
    mov ax, [ss:bp+24]              ; IP
    call mm_hex4
    mov al, ' '
    call mm_putc
    mov ax, [ss:bp+2]               ; DS
    call mm_hex4
    mov al, ' '
    call mm_putc
    mov ax, [ss:bp+0]               ; ES
    call mm_hex4
    mov al, ' '
    call mm_putc
    mov ax, [ss:bp+26]
    call mega_cs2ds                 ; AX = the image's data segment
    jc .anon
    mov es, ax
    cmp word [es:0], 0x384F         ; 'O8': a package, driver or overlay
    jne .anon
    mov si, 16
.nm:
    mov al, [es:si]
    or al, al
    jz .eol
    call mm_putc
    inc si
    cmp si, 32
    jb .nm
    jmp short .eol
.anon:
    mov al, '-'
    call mm_putc
.eol:
    mov al, 10
    call mm_putc
    pop es
    pop si
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; mega_svc - a BIOS service, REFLECTED to real mode (KERN-MEGA-PLAN 2.2)
;
; in:       [SP] = the vector, then the interrupt's IP, CS, FLAGS; every
;           register is the caller's argument to the BIOS
; out:      every register the BIOS's answer, and its CF, ZF and the rest in
;           the FLAGS the iret restores (IF stays the caller's)
;
; DS and ES go to the BIOS as SEGMENTS: an identity selector already is one,
; the kernel's data alias is KERNEL_SEG, LOW0 is 0, and an LDT code alias is
; its 512-aligned base. Coming back, a register the BIOS left alone is given
; its own selector back, and one it changed - int 13h AH=08h's ES:DI, int 10h
; AX=1130h's ES:BP - is taken as the identity selector it now is.
; -----------------------------------------------------------------------------
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
section .mega                   ; the PROTECTED-MODE halves (KERN-MEGA-SEGMENTS-PLAN W3)
mega_svc:
    pusha
    push ds
    push es
    mov bp, sp                      ; +0 ES +2 DS +4..+18 DI..AX +20 vector
    mov ax, MEGA_SEL_KDATA          ; +22 IP +24 CS +26 FLAGS
    mov ds, ax
    cmp byte [ss:bp+20], 0x19       ; int 19h: nothing to come back to - the
    jne .refl                       ; 8042's reset line, with the CMOS
    mov al, 0x0F                    ; shutdown byte cleared so the POST is a
    out 0x70, al                    ; whole one
    xor al, al
    out 0x71, al
    push MEGA_BDA                   ; ...but a WARM one: 1234h at 40:72 skips
    pop es                          ; the RAM test, as Ctrl-Alt-Del does. This
    mov word [es:0x72], 0x1234      ; is Restart's way out (dsk_fdd_park_x)
    call mm_kbw
    mov al, 0xFE
    out 0x64, al
.hang:
    hlt
    jmp .hang
.refl:
    cmp byte [ss:bp+20], 0x13       ; a floppy transfer, natively if it can
    jne .refl1                      ; be (mega_fd13)
    call mega_fd13
    jc .refl1
    pop es
    pop ds
    popa
    add sp, 2                       ; the vector
    iret
.refl1:
    cmp byte [ss:bp+20], 0x13       ; a FIXED disk's int 13h, reflected: said
    jne .refl2                      ; once on port E9, because HDD.DRV's rung 1
    test byte [ss:bp+14], 0x80      ; is meant to leave a BIOS-known drive
    jz .refl2                       ; nothing to reflect (SPEC.md 52.1.1) and a
    cmp byte [ss:bp+19], 0x02       ; field run should be able to see which it
    jb .refl2                       ; got. Transfers only (02h/03h), not the
    cmp byte [ss:bp+19], 0x03       ; one-off questions
    ja .refl2
    cmp byte [mega_hdsaid], 0
    jne .refl2
    mov byte [mega_hdsaid], 1
    push si
    mov si, mega_mhd13
    call mm_say
    pop si
.refl2:
    mov ax, [ss:bp+0]               ; ES and DS as SEGMENTS
    mov [mega_ses], ax
    call KERNEL_SEG:mega_s2segf
    mov [ss:bp+0], ax
    mov [mega_res], ax
    cmp ax, 0xFFFF                  ; ES above 1MB: no real-mode address
    jne .esok                       ; reaches it, and a BIOS transfer there
    cmp word [ss:bp+18], 0x0200     ; would land wherever FFFF:BX wraps to.
    jb .esok                        ; A transfer (int 13h 02h/03h) BOUNCES
    cmp word [ss:bp+18], 0x0400     ; through a buffer below 1MB, a run at a
    jae .esok                       ; time (KERN-MEGA-PLAN 14.1 answer 11):
    cmp byte [ss:bp+20], 0x13       ; it used to be refused 09h, as the 8237
    jne .esok                       ; would refuse it
    mov ax, [mega_ses]
    mov [ss:bp+0], ax
    call MEGA_SEL_CODE:mega_hibnc        ; .cold: the segment has no room for it
    pop es
    pop ds
    popa
    add sp, 2
    iret
.esok:
    mov ax, [ss:bp+2]
    mov [mega_sds], ax
    call KERNEL_SEG:mega_s2segf
    mov [ss:bp+2], ax
    mov [mega_rds], ax
    mov ax, [ss:bp+20]
    mov [mega_rv], al
    mov [mega_irqr], ah             ; 1: an option ROM's IRQ (mega_irqrom)
%ifdef MEGA_DIAG
    call mega_pit                   ; ...and when it started (mega_rfsec)
    mov [mega_rft0], ax
    cmp byte [mega_irqr], 0
    je .nopm
    call mega_pmpre                 ; a PicoMEM's: what it was asked to do
.nopm:
%endif
    push es
    mov ax, MEGA_BDA
    mov es, ax
    mov ax, [es:0x6C]               ; the tick, so the ones the BIOS takes
    pop es                          ; while it holds the CPU are credited
    mov [mega_t0], ax
    in al, 0x21                     ; every IRQ but the BIOS's own masked:
    mov [mega_imr1], al             ; 0 tick, 1 keyboard, 2 cascade, 6 floppy
    in al, 0xA1                     ; on the master, 14 and 15 (the disks) on
    mov [mega_imr2], al             ; the slave. A kernel ISR running in real
    cmp byte [mega_irqr], 0         ; mode would load a SELECTOR as a segment.
    jne .imrkeep                    ; An IRQ's reflection KEEPS the mask: the
    call MEGA_SEL_CODE:mega_rmaskf       ; BX = B8h/3Fh, less the MOUSE's line
                                    ; when it can run in real mode (14.1
    mov al, [mega_imr1]             ; answer 18), banked for mega_r286.
    or al, bl                       ; PicoMEM raises its SB's IRQ only when
    out 0x21, al                    ; that line is unmasked, and mega_capon
    mov al, [mega_imr2]             ; catches every kernel line instead
    or al, bh
    out 0xA1, al
.imrkeep:
    ; NO re-initialisation of the 8259 for real mode: ICW1 resets its edge
    ; latches, so a byte the serial mouse sent while IRQ4 was masked above
    ; was LOST - and the UART then holds its line high and never makes the
    ; next edge, so the mouse was dead for the rest of the session. The PIC
    ; stays at MEGA_PIC_BASE and real mode finds the BIOS's handlers there
    ; (mega_enter copies IVT 08h-0Fh to 20h-27h); the masked edge is still
    ; latched when the masks come off
    cmp word [spk_seg], 0           ; THE SPEAKER'S SAMPLE ISR IS A GATE
    je .nospk                       ; (34.11.1.1): in real mode IRQ0 is the
    mov al, 0x34                    ; BIOS's, which would count a tick a
    out 0x43, al                    ; sample - so channel 0 runs at the ROM's
    xor al, al                      ; rate while the BIOS has the CPU, and
    out 0x40, al                    ; the play waits for it
    out 0x40, al
.nospk:
    jmp KERNEL_SEG:mega_svc_lo      ; ...and down into the window real mode
section .text                       ; can reach, which is all that stays low
; mega_svc_lo - mega_svc's REAL-MODE WINDOW: out of protected mode, the call,
; and back in, entered far from `.mega` with the frame on the stack
mega_svc_lo:
    cmp byte [cpu_tier], CPU_386
    jb .r286
    push ds                         ; ES WRITABLE FIRST (Intel SDM 9.9.2): a
    pop es                          ; 386 keeps a segment's ATTRIBUTES across
                                    ; the switch and real mode only reloads
                                    ; its base, so a caller's CODE alias in ES
                                    ; - HIBER.DRV's `push cs / pop es`, found
                                    ; in Bochs (KERN-MEGA-DOS-PLAN 10.3) -
                                    ; made every BIOS store through ES fault,
                                    ; in real mode, for ever. DS is KDATA
    mov al, 0x8D                    ; NMI off across the switch, as mega_enter:
    out 0x70, al                    ; real mode with the PM IDTR (index 0Dh)
cpu 386
    mov eax, cr0                    ; REAL MODE, the 386's way: clear PE
    and al, 0xFE
    mov cr0, eax
cpu 286
    db 0xEA                         ; jmp far KERNEL_SEG:.rm - reloads CS as a
    dw .rm, KERNEL_SEG              ; real-mode segment, the same base
.r286:                              ; REAL MODE, THE 286's WAY: reset the CPU.
    mov [mega_rss], ss              ; lmsw cannot clear PE, so the BIOS is told
    mov [mega_rsp], sp              ; where to come back to - 40:67, with CMOS
    push es                         ; shutdown code 0Ah, "jump there, no POST,
    mov ax, MEGA_BDA                ; no EOI" - and the CPU is triple-faulted.
    mov es, ax                      ; RAM, the A20 gate and every device keep
    mov word [es:0x67], mega_r286   ; their state; the CPU loses its registers,
    mov word [es:0x69], KERNEL_SEG  ; GDTR, IDTR and LDTR, which is why the way
%ifdef MEGA_DIAG                    ; back reloads all three. The floppy's
    mov ax, [es:0x3E]               ; 40:3E/3F, for mega_r286 to compare
    mov [mega_fdpre], ax            ; (MEGADIAG=1's RST line)
%endif
    pop es
    mov al, 0x8F                    ; NMI off, CMOS index 0Fh
    out 0x70, al
    mov al, 0x0A
    out 0x71, al
    lidt [mega_nullidt]             ; an IDT with no room in it, then a fault:
    int 3                           ; nothing can be delivered, the 286 shuts
.rhang:                             ; down, and the AT's logic resets it
    hlt
    jmp .rhang
.rm:
    lidt [cs:mega_rmidtr]
    mov al, 0x0D                    ; ...and on again under the real IVT (the
    out 0x70, al                    ; 286's way has mega_r286's already: twice)
.rmc:
    mov ax, ss                      ; LOW_SEG: the selector IS the segment
    mov ss, ax
    mov ax, KERNEL_SEG
    mov ds, ax
    mov [k_dsel], ax                ; THE MOUSE ISR RUNS HERE TOO (14.1 answer
                                    ; 18): every KDS in it loads this word,
                                    ; which is the real segment now
%ifdef PIC_LOSE
    ; PICLOSE=1 (Makefile, KERN-MEGA-PLAN 14.64): what the Victory 286's BIOS
    ; is inferred to do on the reset path of every reflection - initialise
    ; both 8259s, which erases every pending edge, and put the masks back as
    ; they were, so mega_r286 sees nothing moved and no drain follows. Here on
    ; BOTH ways down, so a 386 (Bochs) loses edges as the 286 does. The
    ; reset path is long and runs with IF = 0, so a moving mouse has a byte
    ; waiting when the BIOS gets there: that WINDOW is modelled too - up to
    ; 65,536 polls for a byte in the 8042, then 8,192 more so its line has
    ; risen (Bochs raises it on the controller's next timer, not with OBF)
    xor cx, cx
.plw:
    in al, 0x64
    test al, 1
    jnz .plgot
    loop .plw
.plgot:
    mov cx, 8192
.plw2:
    in al, 0x64
    loop .plw2
    in al, 0x21
    mov ah, al
    in al, 0xA1
    xchg al, ah                     ; AL = the master's mask, AH = the slave's
    push ax
    mov al, 0x11                    ; ICW1: edge, cascade, ICW4
    out 0x20, al
    out 0xA0, al
    mov al, MEGA_PIC_BASE           ; ICW2
    out 0x21, al
    mov al, 0x70
    out 0xA1, al
    mov al, 0x04                    ; ICW3: the slave on IRQ2...
    out 0x21, al
    mov al, 0x02                    ; ...and its cascade identity
    out 0xA1, al
    mov al, 0x01                    ; ICW4: 8086 mode
    out 0x21, al
    out 0xA1, al
    pop ax
    out 0x21, al
    mov al, ah
    out 0xA1, al
%endif
    mov al, [mega_rv]               ; real mode: code is memory again
    mov [.intop+1], al
    mov byte [.stiop], 0xFB         ; sti: a service runs with IF on...
    cmp byte [mega_irqr], 0
    je .go0
    call mega_capon                 ; an IRQ: kernel lines caught, not run
    mov byte [.stiop], 0x90         ; ...and an ISR is entered with IF off
.go0:
    jmp short .go                   ; ...and the prefetch queue is flushed
.go:
    pop es
    pop ds
    popa                            ; SP is at the vector word
.stiop:
    sti
.intop:
    int 0x10                        ; (the vector, patched above)
    cli
    pushf                           ; +20 the BIOS's FLAGS, +22 vector, +24 IP
    pusha
    push ds
    push es
    mov bp, sp
    mov ax, KERNEL_SEG
    mov ds, ax
    cmp byte [mega_irqr], 0
    je .nocap
    call mega_capoff
.nocap:
    mov al, 0x8D                    ; NMI off until the selectors are back: the
    out 0x70, al                    ; PM IDTR in real mode reads gates as IVT
    lgdt [mega_gdtr]                ; all three again: a 286's reset cleared
    lidt [mega_idtr]                ; them (and a 386's CR0 path kept them,
    smsw ax                         ; which costs it nothing to redo). lmsw
    or al, 1                        ; can SET PE on either CPU
    lmsw ax
    db 0xEA                         ; jmp far 0x0060:.pm - GDT entry 12 now
    dw .pm, KERNEL_SEG
.pm:
    mov ax, MEGA_SEL_LDT
    lldt ax
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov es, ax                      ; ES TOO: it still holds whatever SEGMENT
                                    ; the BIOS returned, which is no selector -
                                    ; the `push es / pop es` below reloaded
                                    ; FFFFh as one and the #GP handler then
                                    ; faulted itself (Redline above 1MB,
                                    ; KERN-MEGA-PLAN 14.14). The caller's ES
                                    ; comes back from the frame at .out
    mov [k_dsel], ax                ; ...and the selector again, IF = 0 since
                                    ; the BIOS returned
    mov ax, ss
    mov ss, ax
    mov al, 0x0D                    ; ...and NMI on: every register a gate
    out 0x70, al                    ; needs is a selector again
    jmp MEGA_SEL_CODE:mega_svc_back ; ...and the rest is protected mode's
section .mega
mega_svc_back:
    ; STKBALANCE-OK: mega_svc's back half - entered by a far jump on the frame its
    ; front half built (es ds pusha vector, the BIOS's FLAGS above it), left by iret
    xor ax, ax                      ; the pointer's frame buffer back to its
    xchg ax, [mega_vsw]             ; SELECTORS (mega_vrm), before the mask
    or ax, ax                       ; below opens its line in protected mode:
    jz .novs                        ; IF is 0 from here to the iret
    mov [vid_seg], ax
    mov ax, [mega_vsw+2]
    mov [k_vsa], ax
.novs:
    mov al, [mega_imr1]
    out 0x21, al
    mov al, [mega_imr2]
    out 0xA1, al
    cmp word [spk_seg], 0           ; ...and back at the sample's count
    je .nospk2
    mov al, 0x34
    out 0x43, al
    mov ax, [spk_n]
    out 0x40, al
    mov al, ah
    out 0x40, al
.nospk2:
    call mega_rfclk                 ; the IRQ0s the ROM took meanwhile
    mov ax, [ss:bp+0]               ; ES back: its own selector if the BIOS
    cmp ax, [mega_res]              ; left it alone, and a selector for the
    jne .esnew                      ; real segment it handed back if it did
    mov ax, [mega_ses]              ; not (mega_rseg)
    jmp short .esput
.esnew:
    call mega_rseg
.esput:
    mov [ss:bp+0], ax
    mov ax, [ss:bp+2]
    cmp ax, [mega_rds]
    jne .dsnew
    mov ax, [mega_sds]
    jmp short .dsput
.dsnew:
    call mega_rseg
.dsput:
    mov [ss:bp+2], ax
    cmp byte [mega_irqr], 0         ; an IRQ: the interrupted code's flags
    jne .irqx                       ; are its own, not the ROM's answer
    mov ax, [ss:bp+20]              ; the BIOS's arithmetic flags into the
    and ax, 0x08D5                  ; caller's FLAGS: OF SF ZF AF PF CF
    and word [ss:bp+28], ~0x08D5
    or [ss:bp+28], ax
    cmp byte [mega_fdpend], 0       ; a native floppy fallback: did the BIOS
    je .rh                          ; do what it could not? (mega_fdjudge)
    call mega_fdjudge
.rh:
    cmp word [sch_rpend], 0         ; THE PERIODS THE ROM TOOK, TO THE HOOK
    je .out                         ; NOW (KERN-MEGA-PLAN 14.73): every IRQ0
    cmp byte [sch_fast], SCH_RATE   ; that fell in the call was the ROM's, so
    jne .out                        ; an FSXF_RATE hook (SPEC.md 53.2.2) ran
    pusha                           ; at none of them - where a real-mode
    mov bp, sch_rhook               ; kernel's runs through the same read. A
    call KERNEL_SEG:mega_tcall      ; caller that chained reads back to back
    popa                            ; starved it outright: the next tick fell
.out:                               ; in the next reflection. sch_rhook's
                                    ; own guards (busy, the ROM's chain, a
                                    ; stack not a task's) still decide
    pop es
    pop ds
    popa
    add sp, 4                       ; the BIOS's FLAGS and the vector
    iret
.irqx:
    mov byte [mega_irqr], 0
%ifdef MEGA_DIAG
    call mega_pmpost                ; ...and how its ROM says it ended
    call mega_pit                   ; the PIT counts DOWN: t0 - t1, modulo
    neg ax                          ; its 65536, is the time it took
    add ax, [mega_rft0]
    add [mega_rfsum], ax
    adc word [mega_rfsum+2], 0
    inc word [mega_rfn]
%endif
    call mega_replay                ; what the ROM raised, and what arrived
    jmp short .out                  ; on a kernel line meanwhile

; mega_rfclk - the IRQ0s the ROM took while a reflection held the CPU, given
; to the kernel's clocks. DS = KDATA; clobbers AX BX CX DX (mega_svc_back's
; frame restores every register).
;
; The ROM's handler counts each one at 40:6C, so the difference from mega_t0
; is EXACTLY how many periods channel 0 delivered - and outside a bracket, or
; under the speaker's (mega_svc put channel 0 back at the ROM's rate for the
; call), a period is a tick. INSIDE a rate or fast-tick bracket it is not: the
; ROM counted the bracket's periods as ticks, so the time of day ran ahead,
; and the hook that the kernel's own IRQ0 would have paid them to never heard
; of them. A program paced on its hook - the Video Player on its FSXF_RATE -
; then lost every reflected int 13h and every PicoMEM IRQ from its clock and
; PLAYED SLOW WITHOUT EVER STARVING, on a disk the BIOS serves (KERN-MEGA-
; PLAN 14.36). So the periods are paid exactly as sch_isr pays them: owed to
; the hook and folded into the rate accumulator, whose carries are the ticks
; - or, under FASTTICK, counted down [sch_fcnt] - and the BIOS's clock is
; taken back to the ticks they really were.
mega_rfclk:
    push es
    mov ax, MEGA_BDA
    mov es, ax
    mov ax, [es:0x6C]
    sub ax, [mega_t0]               ; AX = the periods the ROM took
    jz .out
    mov cl, [sch_fast]
    or cl, cl
    jz .ticks                       ; the ROM's own rate: each is a tick
    cmp word [spk_seg], 0
    jne .ticks                      ; ...and so under the speaker's bracket
    mov si, ax                      ; SI = the periods, for the clock below
    cmp cl, SCH_RATE
    jne .fast
    add [sch_rpend], ax             ; owed to the hook, as sch_isr would
    mul word [sch_rdiv]             ; DX:AX = their PIT counts...
    add [sch_racc], ax
    adc dx, 0                       ; ...and DX the whole 65536s: ticks
    mov bx, dx
    jmp short .fix
.fast:                              ; FASTTICK: N periods a tick
    mov dx, ax
    xor bx, bx
.fl:
    dec byte [sch_fcnt]
    jnz .fn
    mov [sch_fcnt], cl
    inc bx
.fn:
    dec dx
    jnz .fl
.fix:
    sub si, bx                      ; the ROM's surplus: periods it counted
    sub [es:0x6C], si               ; as ticks that were not, off the 32-bit
    sbb word [es:0x6E], 0           ; time of day
    mov ax, bx
.ticks:
    add [ticks], ax
.out:
    pop es
    ret

section .text
; mega_r286 - where a 286's reset comes back to (40:67, CMOS shutdown 0Ah),
; in real mode with every register undefined and interrupts off. The stack
; the reflection was on is put back, the shutdown code cleared so the next
; reset is a real one, and the reflection carries on at mega_svc's real-mode
; half - which reloads the IDTR (the reset left it at the IVT anyway).
mega_r286:
    cli
    mov ax, KERNEL_SEG
    mov ds, ax
    mov ss, [mega_rss]
    mov sp, [mega_rsp]
%ifdef MEGA_DIAG
    inc word [mega_rstn]
    push es                         ; did the BIOS's way back from the reset
    mov ax, MEGA_BDA                ; touch the floppy - the calibrate bits,
    mov es, ax                      ; the IRQ6 flag a controller reset raises,
    mov ax, [es:0x3E]               ; the motors? The Victory's drive stutters
    cmp byte [es:0x40], 0           ; through a C: copy, and every C: call on
    pop es                          ; a 286 comes through here. The first
    jne .fdcmp                      ; difference of a boot goes on the glass.
    mov ah, [mega_fdpre+1]          ; A tick that ran the motor count out is
.fdcmp:                             ; the ROM's job done, so 3F is not asked
    cmp ax, [mega_fdpre]
    je .fdsame
    test byte [mega_tellq], 0x80
    jnz .fdsame
    mov [mega_fdpost], ax
    or byte [mega_tellq], 0x82      ; 2: say it; 80h: said for this boot
.fdsame:
%endif
    push es                         ; ...and the controller itself: reset on
    mov ax, MEGA_BDA                ; the way here, with nobody told?
    mov es, ax
    mov cl, 0
    call mega_fdrprobe
    pop es
    mov al, 0x8F
    out 0x70, al
    xor al, al
    out 0x71, al                    ; shutdown code 0: a normal reset next time
    mov al, 0x0D                    ; ...and NMI back on (index 0Dh, a read-only
    out 0x70, al                    ; register, bit 7 clear), as mega_enter
    mov bx, [mega_rmask]            ; the masks THIS reflection set: a
    cmp byte [mega_irqr], 0         ; service's ORs B8h/3Fh in, an IRQ's
    je .msk                         ; keeps the IMR as it was (mega_svc).
    xor bx, bx                      ; Expecting B8h on an IRQ's read as a
.msk:                               ; BIOS reset every time, re-initialised
    in al, 0x21                     ; the 8259 and MASKED the PicoMEM's SB
    mov ah, [mega_imr1]             ; line - so its multiplexer skipped every
    or ah, bl                       ; block IRQ and Tracker stopped after the
    cmp al, ah                      ; pre-roll, on the 286 and never on a
    je .picok                       ; 386, whose CR0 path does not come here.
    call mega_pic_pm                ; A BIOS that re-initialised the 8259 on
    mov al, [mega_imr1]             ; its way here moved its base back to 08h
    or al, bl                       ; and cleared the masks, and the masks are
    out 0x21, al                    ; what says so: put ours back (mega_svc's
    mov al, [mega_imr2]             ; note on why the 8259 is otherwise left
    or al, bh                       ; alone)
    out 0xA1, al
    call mega_kdrain                ; ...and what the two ICW1s stranded in
.picok:                             ; the 8042 (KERN-MEGA-PLAN 14.62)
    cmp byte [mega_drop], 0         ; THE DOS PARK's way down (megados.inc):
    jne mega_dropr                  ; the same reset, a different landing
    jmp mega_svc_lo.rm

; -----------------------------------------------------------------------------
; mega_slidef - FAR: AX = a data selector, DX = paragraphs -> AX = a data
; selector for the same memory DX x 16 bytes on. An identity selector by
; addition; anything else - an LDT window above 1MB, a package placed high -
; through MEGA_SEL_SLD, whose base is moved per use (lz.inc's source slide,
; KERN-MEGA-PLAN 13.3 step 4). MEGA_SEL_ODD's caveat applies: one scratch, so
; a task switch that reloads DS mid-decode on another task's slide would read
; the other base. Flags kept
; -----------------------------------------------------------------------------
mega_slidef:
    pushf
    cmp word [cs:k_dsel], KERNEL_SEG ; REAL MODE: a segment, and the sum is
    jne .desc                       ; the answer - asked first, since a real
    add ax, dx                      ; segment can carry bit 2. In protected
    popf                            ; mode EVERY selector slides by its
    retf                            ; descriptor, the scratch ones (ODD, SLD)
.desc:                              ; included (KERN-MEGA-MEMORY-PLAN MC7.4)
    push bx
    push cx
    push dx
    push di
    push es
    mov di, MEGA_SEL_SLD            ; the scratch it lands on: SLD, but an LZ
    cmp ax, MEGA_SEL_K0             ; MATCH's source slides onto its own, BACK
    jb .sld                         ; - SLD is the decoder's INPUT, which the
    cmp ax, MEGA_SEL_BACK           ; match has on the stack (lz_match) - and
    ja .sld                         ; a module's K scratch onto ITSELF, its
    mov di, ax                      ; stream's own (KSEG)
.sld:
    mov bx, dx                      ; BX = paragraphs, SIGNED: a module steps
    call MEGA_SEL_CODE:mega_selbasef ; back a granule too (DX:AX = the base,
    mov cx, bx                      ; CL spent)
    shl cx, 4
    sar bx, 12
    add ax, cx
    adc dx, bx                      ; DX:AX = the base, slid
    push ax
    mov ax, MEGA_SEL_WIN            ; the GDT itself, as data
    mov es, ax
    pop ax
    pushf
    cli                             ; (mega_gdtaim's reason)
    mov word [es:di], 0xFFFF
    mov [es:di+2], ax
    mov dh, 0x92
    mov [es:di+4], dx
    mov word [es:di+6], 0
    popf
    mov ax, di
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    popf
    retf

; -----------------------------------------------------------------------------
; mega_backf - FAR: AX = a data selector or segment -> AX = one for the same
; memory 64KB BACK (lz_match's source when a match reaches over a 64KB
; boundary of the output): the difference in real mode, and in protected mode
; MEGA_SEL_BACK aimed at the base less 64KB - the output's ES being the ODD
; scratch and the input's DS the SLD one, a match wants a third. Every other
; register and FLAGS kept
; mega_bkaimf - FAR: AX = a data selector -> AX = MEGA_SEL_BACK over the SAME
; memory in protected mode, AX unchanged in real mode: an LZ match whose source
; may slide, or that sits on the ODD scratch the output's bump re-aims, is
; read through a scratch nothing else moves (lz_match). Every other register
; and FLAGS kept
; mega_sldsegf - FAR: AX = a REAL segment -> AX = a data selector over it:
; itself in real mode, MEGA_SEL_SLD in protected mode (the LZ decoder's input
; when the file doors read a packed stream onto the segment arm, whose output
; holds MEGA_SEL_ODD - KERN-MEGA-MEMORY-PLAN MC7.4). Every other register and
; FLAGS kept
; -----------------------------------------------------------------------------
section .cold
mega_bkaimf:
    pushf
    cmp byte [ss:mega_pm], 0
    je .rm
    push dx
    push di
    call MEGA_SEL_CODE:mega_selbasef ; DX:AX = the base, unmoved
    jmp short mega_backf.aim
.rm:
    popf
    retf
mega_backf:
    pushf
    cmp byte [ss:mega_pm], 0
    jne .pm
    sub ax, 0x1000
    popf
    retf
.pm:
    push dx
    push di
    call MEGA_SEL_CODE:mega_selbasef ; DX:AX = the base
    dec dx                          ; ...64KB back
.aim:
    mov di, MEGA_SEL_BACK
    call mega_gdtaim
    pop di
    pop dx
    popf
    retf
mega_sldsegf:
    pushf
    cmp byte [ss:mega_pm], 0
    je .rm
    push dx
    push di
    mov dx, ax
    shl ax, 4
    shr dx, 12                      ; DX:AX = its base
    mov di, MEGA_SEL_SLD
    call mega_gdtaim
    pop di
    pop dx
.rm:
    popf
    retf
; mega_gdtaim - DI = a GDT scratch selector, DX:AX = a base -> that entry a
; 64KB data descriptor there, AX = the selector. Clobbers DX
mega_gdtaim:
    push es
    push ax
    mov ax, MEGA_SEL_WIN            ; the GDT itself, as data
    mov es, ax
    pop ax
    pushf                           ; one descriptor, written whole: an ISR
    cli                             ; that reloads this selector mid-write
    mov word [es:di], 0xFFFF        ; (its push/pop of DS) would read half of
    mov [es:di+2], ax               ; each base
    mov dh, 0x92
    mov [es:di+4], dx
    mov word [es:di+6], 0
    popf
    mov ax, di
    pop es
    ret
; mega_vbaimf - FAR: AX = the planar decoder's buffer's new PARAGRAPH ->
; [rom_vgabw] = AX (mem_floor_ax's word) and [mega_vbsel] = MEGA_SEL_VB aimed
; there, or AX itself in real mode. Every register and FLAGS kept
mega_vbaimf:
    push ax
    push di
    mov [rom_vgabw], ax
    mov di, MEGA_SEL_VB
    call COLD_SEG:mega_kaimf
    mov [mega_vbsel], ax
    pop di
    pop ax
    retf
; mega_kaimf - FAR: AX = a REAL paragraph, DI = one of MEGA_SEL_K0..K5 -> AX
; = that scratch aimed there in protected mode; AX unchanged in real mode.
; Every other register and FLAGS kept (KSEG, kernel.asm)
mega_kaimf:
    pushf
    cmp byte [ss:mega_pm], 0
    je .rm
    push dx
    mov dx, ax
    shl ax, 4
    shr dx, 12                      ; DX:AX = its base
    call mega_gdtaim
    pop dx
.rm:
    popf
    retf
section .text

; -----------------------------------------------------------------------------
; mega_oddsegf - a selector for ANY paragraph (far; KERN-MEGA-PLAN 3.1)
; in:  AX = a real-mode segment. out: AX = a data selector with that base -
;      itself when it is on the identity grid, MEGA_SEL_ODD with its base
;      moved when it is not. Everything else preserved, FLAGS included. In
;      REAL MODE (before mega_enter) AX comes back as it went in.
; ONE scratch descriptor: a caller holds MEGA_SEL_ODD only until its next
; call here, and nothing that can run in between (an ISR) asks for one.
; The loader's decompressor target is the one off-grid segment in M1 (a
; package's prefix is 64 bytes, so its body starts 4 paragraphs along, and
; lz_decomp_x wants DI = 0).
; -----------------------------------------------------------------------------
mega_oddsegf:
    pushf
    cmp word [cs:k_dsel], KERNEL_SEG ; REAL MODE - the boot, before mega_enter:
    je .grid                        ; a segment is its own answer. The boot
                                    ; expands every compressed driver it loads
                                    ; through here, and the protected-mode arm
                                    ; run in real mode wrote a descriptor into
                                    ; the IVT and handed back 0030h, so the
                                    ; driver landed on the API table
    cmp ax, MEGA_GDT_ID * 8         ; past the GDT's identity map (MC7's step
    jae .odd                        ; 6): no selector of its own
    test al, 7
    jz .grid
.odd:
    push di
    push es
    push dx
    mov dx, ax
    shl ax, 4                       ; base 15:0...
    shr dx, 12                      ; ...and 23:16
    mov dh, 0x92
    mov di, MEGA_SEL_WIN            ; the GDT itself, as data
    mov es, di
    mov di, MEGA_SEL_ODD
    cli                             ; one descriptor, written whole (mega_gdtaim's
    mov word [es:di], 0xFFFF        ; reason): the entry's FLAGS come back at
    mov [es:di+2], ax               ; .grid's popf
    mov [es:di+4], dx
    mov word [es:di+6], 0
    pop dx
    mov ax, MEGA_SEL_ODD
    pop es
    pop di
.grid:
    popf
    retf

; -----------------------------------------------------------------------------
; A NEW TASK'S FRAME (sch_frame) names its segment three times - DS, ES and the
; iret's CS - and in protected mode those want three different selectors for
; one segment: the identity DATA selector, the kernel's data alias for ES, and
; the CODE alias for CS. Both helpers are bimodal: before mega_enter (the idle
; task, a boot worker) they answer the real-mode value, and mega_enter fixes
; the frames those left behind (mega_frames_fix).
; in: BP = the task's segment (KERNEL_SEG for a kernel task). out: AX
; -----------------------------------------------------------------------------
mega_frame_ds:
    mov ax, bp
    cmp ax, KERNEL_SEG
    jne .out
    mov ax, [cs:k_dsel]
.out:
    ret
mega_frame_cs:
    cmp byte [cs:mega_on], 0
    je .out
    cmp ax, KERNEL_SEG
    je .out
    test al, 4
    jz .out                         ; (a GDT selector is no package's)
    sub ax, 8                       ; a package's region: its pair's code half
.out:                               ; (KERN-MEGA-MEMORY-PLAN MC7.4)
    ret

; mega_frames_fix - every live task but the running one was suspended in real
; mode, so its saved DS and ES may say KERNEL_SEG, which is CODE now: a store
; through either would fault. Both become the data alias. Its CS is right as
; it is - 0x0060 is code - and a package's worker cannot exist yet, nothing
; having been launched before the desktop.
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_frames_fix:
    push ax
    push bx
    push cx
    push di
    mov bx, sch_tasks
    xor cx, cx
.t:
    cmp byte [bx+T_STATE], 0
    je .n
    cmp cl, [sch_cur]
    je .n
    mov di, [bx+T_SP]
    cmp word [ss:di], KERNEL_SEG    ; the saved DS...
    jne .es
    mov word [ss:di], MEGA_SEL_KDATA
.es:
    cmp word [ss:di+2], KERNEL_SEG  ; ...and ES
    jne .n
    mov word [ss:di+2], MEGA_SEL_KDATA
.n:
    add bx, T_SIZE
    inc cx
    cmp cx, MAX_TASKS
    jb .t
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mega_gpfix - #GP(S) where S is an image's segment: a FAR TRANSFER through the
; data segment that real mode lets code call, finished through S's code alias
;
; A loaded image - a driver, a module, a package's region - is 512-aligned, so
; its segment S is both an identity DATA selector (GDT entry S/8) and, through
; the LDT, an exact CODE alias (S >> 2) | 4. Kernel code converts at the call
; (MEGA_PCALL), but an image that far-calls ANOTHER image by the segment it was
; handed - HDD.DRV into HDDTOOL.DRV and back through hd_tfar and hd_rdisp, the
; Victory 286's Control Panel Hard Drive page - cannot know it should. Loading
; S into CS is #GP with S as the error code, and nothing else loads a data
; identity selector and faults, so the error code says what happened: this
; decodes the instruction at CS:IP to be sure, and does the transfer itself.
;
; Taken: FF /3 and FF /5 (call/jmp far [mem], any 16-bit modrm, with ES, CS, SS
; or DS overrides), 9A and EA (the immediate forms), CB and CA (retf). Anything
; else, or a target that is not S, goes to the reporter unchanged. The frame
; mega_fault built (ES DS DI SI BP SP BX DX CX AX vector error IP CS FLAGS, at
; [ss:bp]) sits directly below the interrupted stack, so a CALL moves it down
; four bytes to make room for the return address and a RETF moves it up over
; the words the transfer popped. Inline, not called: the frame moves.
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_gpfix:
    ; STKBALANCE-OK: rewrites the fault frame in place - SP comes back from BP,
    ; moved by the bytes the emulated CALL pushed or RETF popped
    mov ax, [ss:bp+22]              ; the error code: S, TI and EXT clear
    or ax, ax
    jz mega_gpw                     ; #GP(0): a WRITE through a code segment
    test al, 0x1F                   ; ...512-aligned (and so TI = EXT = 0)
    jnz .pairq
    cmp ax, 0xA000                  ; a heap image, not the ROM or video
    jae mega_report
.gpok:
    mov [mega_gps], ax
    mov es, [ss:bp+26]              ; ES:SI = the faulting instruction (a code
    mov si, [ss:bp+24]              ; selector is readable)
    mov dx, [ss:bp+2]               ; DX = its data segment: DS unless told
    xor bx, bx                      ; BL = 1 once a prefix named one
    mov cx, 4                       ; prefixes, at most
.pfx:
    mov al, [es:si]
    mov di, [ss:bp+0]
    cmp al, 0x26                    ; ES:
    je .seg
    mov di, [ss:bp+26]
    cmp al, 0x2E                    ; CS:
    je .seg
    mov di, ss
    cmp al, 0x36                    ; SS:
    je .seg
    mov di, [ss:bp+2]
    cmp al, 0x3E                    ; DS:
    jne .op
.seg:
    mov dx, di
    mov bl, 1
    inc si
    loop .pfx
    jmp mega_report
.op:
    cmp al, 0xFF
    je .ff
    cmp al, 0x9A
    je .imm
    cmp al, 0xEA
    je .imm
    cmp al, 0xCB
    je .retf
    cmp al, 0xCA
    je .retf
    jmp mega_report
.imm:                               ; 9A/EA ptr16:16
    mov cx, [es:si+1]               ; CX = the offset
    mov di, [es:si+3]               ; DI = the segment
    add si, 5                       ; SI = the next instruction
    mov ah, al
    cmp ah, 0x9A
    jmp .target                     ; ZF = 1: a call
.ff:                                ; FF /3 or /5 [mem]
    mov ah, [es:si+1]               ; AH = modrm
    mov al, ah
    and al, 0x38
    cmp al, 0x18                    ; /3: call far
    je .modrm
    cmp al, 0x28                    ; /5: jmp far
    jne mega_report
.modrm:
    mov [mega_gpop], al
    mov al, ah
    shr al, 6                       ; mod
    cmp al, 3
    je mega_report                  ; a register is not a far pointer
    mov cl, ah
    and cl, 7                       ; CL = rm
    xor di, di                      ; DI = the effective address
    cmp al, 0
    jne .base
    cmp cl, 6
    jne .base
    mov di, [es:si+2]               ; mod 0 rm 6: [disp16]
    add si, 4
    jmp short .ea
.base:
    cmp cl, 4
    je .rsi
    cmp cl, 5
    je .rdi
    cmp cl, 7
    je .rbx
    cmp cl, 6
    je .rbp
    test cl, 2                      ; 0,1: BX+..; 2,3: BP+..
    jnz .bp2
    add di, [ss:bp+12]
    jmp short .idx
.bp2:
    add di, [ss:bp+8]
    or bl, bl
    jnz .idx
    mov dx, ss                      ; BP-based: SS, unless overridden
.idx:
    test cl, 1
    jnz .rdi
.rsi:
    add di, [ss:bp+6]
    jmp short .disp
.rdi:
    add di, [ss:bp+4]
    jmp short .disp
.rbx:
    add di, [ss:bp+12]
    jmp short .disp
.rbp:
    add di, [ss:bp+8]
    or bl, bl
    jnz .disp
    mov dx, ss
.disp:
    cmp al, 1
    jne .d16
    mov al, [es:si+2]
    cbw
    add di, ax
    add si, 3
    jmp short .ea
.d16:
    cmp al, 2
    jne .d0
    add di, [es:si+2]
    add si, 4
    jmp short .ea
.d0:
    add si, 2
.ea:
    push ds
    mov ds, dx
    mov cx, [di]                    ; CX = the offset
    mov di, [di+2]                  ; DI = the segment
    pop ds
    cmp byte [mega_gpop], 0x18
.target:                            ; ZF = 1 a call, 0 a jmp; CX:DI the target
    pushf
    cmp di, [mega_gps]
    jne .nottarget
    call .alias                     ; AX = the code alias
    popf
    jnz .jmp
    ; --- CALL: the frame down four bytes, the return address above it -----
    mov dx, [ss:bp+26]              ; the caller's CS
    mov bx, si                      ; BX = the return IP
    xor si, si
    sub sp, 4                       ; SP over the moved frame: an NMI (IF = 0
                                    ; does not stop one) pushes below it
.dn:
    mov di, [ss:bp+si]
    mov [ss:bp+si-4], di
    add si, 2
    cmp si, 30
    jb .dn
    sub bp, 4
    mov [ss:bp+30], bx              ; the return IP...
    mov [ss:bp+32], dx              ; ...and CS, where the call put them
.jmp:
    mov [ss:bp+24], cx
    mov [ss:bp+26], ax
.resume:
    mov sp, bp
    pop es
    pop ds
    popa
    add sp, 4                       ; vector and error code
    iret
.nottarget:
    popf
    jmp mega_report
.alias:                             ; AX = DI's code selector: its pair's
    mov ax, di                      ; code half - every image is a pair since
    test al, 4                      ; KERN-MEGA-MEMORY-PLAN MC7.4, and the
    jz .anone                       ; static aliases went with MC7's step 6
    sub ax, 8
    ret
.anone:                             ; a GDT selector: no image's, reported
    mov sp, bp
    jmp mega_report
.pairq:                             ; A PACKAGE PLACED ABOVE 1MB (KERN-MEGA-
    test al, 3                      ; PLAN 13.3 step 4) far-transferring
    jnz mega_report                 ; through its own DS: the error code is
    test al, 4                      ; its pair's DATA selector, and only one
    jz mega_report                  ; mega_codesel calls a pair's data half
    push dx                         ; is taken
    mov dx, ax
    call mega_codesel
    pop dx
    jc .stale
    cmp ax, [ss:bp+22]
    je mega_report                  ; a code selector answered as itself
    mov ax, [ss:bp+22]
    jmp .gpok
.stale:                             ; A FREED SELECTOR POPPED BACK: a claim's
    test al, 4                      ; (an LDT one only - a GDT index read as
    jz mega_report                  ; an LDT one ran off its alias and faulted
    cmp ax, [ss:mega_ldte]          ; in here, KERN-MEGA-PLAN 14.14)
    jae mega_report
    and ax, 0xFFF8                  ; (OS88_HIGHMEM) or a window's, given back
    cmp ax, MEGA_LDT_FREE0 * 8      ; while DS or ES still held it - which
    jb mega_report                  ; real mode lets a program do, the segment
    mov di, ax                      ; being only a number there. A `pop es` or
    mov ax, MEGA_SEL_LDTD           ; `pop ds` of exactly that selector, its
    mov es, ax                      ; entry free, gets the NULL selector
    cmp byte [es:di+5], 0           ; instead: it loads, and a use of it is
    jne mega_report                 ; #GP(0) naming the use, not the pop
    mov es, [ss:bp+26]
    mov si, [ss:bp+24]
    mov al, [es:si]
    cmp al, 0x07                    ; pop es
    je .snull
    cmp al, 0x1F                    ; pop ds
    jne mega_report
.snull:
    mov ax, [ss:bp+30]              ; the word the pop is taking
    and ax, 0xFFF8
    cmp ax, di
    jne mega_report
    mov word [ss:bp+30], 0
    jmp .resume
.retf:                              ; CB / CA iw: [T] = IP, [T+2] = CS
    mov cx, [ss:bp+30]
    mov di, [ss:bp+32]
    cmp di, [mega_gps]
    jne mega_report
    mov dx, 4                       ; DX = the bytes the retf takes away
    cmp al, 0xCA
    jne .rup
    add dx, [es:si+1]
.rup:
    call .alias
    mov [ss:bp+24], cx
    mov [ss:bp+26], ax
    mov si, 28                      ; the frame UP by DX, top word first
.up:
    mov di, [ss:bp+si]
    push si
    add si, dx
    mov [ss:bp+si], di
    pop si
    sub si, 2
    jns .up
    add bp, dx
    jmp .resume

; -----------------------------------------------------------------------------
; mega_gpw - #GP(0) from a WRITE through a code segment, done as the code meant
;
; Real mode lets an image use its own segment as data, and on kern_mega that
; segment is read-only. The kernel's own modules are converted at the source
; (PUSH_CSDATA, CSDO); the PACKAGES - some 1,240 such writes and 146
; `push cs / pop` pairs across 37 of them, Pixel alone 594 - run on every
; kernel, so they are met here instead, without a change to any package:
;   * DS or ES is a CODE selector - a package's LDT alias, or the kernel's
;     0x0060 - because the code said `push cs / pop ds`: the register gets the
;     image's DATA segment, which is also the value real mode would have put
;     there, and the instruction runs again;
;   * the instruction carries a CS: prefix: CS cannot be made writable, so the
;     one instruction is copied to mega_tramp with DS: in place of CS:, run
;     there with DS = the image's data segment, and control comes back after
;     it with DS as it was - FLAGS from the instruction, IF as the caller had
;     it. IF is off while it runs, so nothing else reaches the trampoline.
; A fault per write, so a hot loop is slow and right: converting it at the
; source is the fast path. Anything this cannot decode is reported as before.
; -----------------------------------------------------------------------------
mega_gpw:
    ; STKBALANCE-OK: returns by iret from the fault frame, never by ret
    mov al, [mega_scal]             ; mega_strcal's own fault: measure, skip
    test al, 0x7F
    jnz mega_strcal_gp
    mov word [mega_tcs], 0          ; no prefix yet: a REPORTED fault left the
                                    ; last one set, .copied being its only reset
    mov ax, [ss:bp+26]              ; AX = the image's data segment, from CS
    call mega_cs2ds                 ; (.cold's CS has none: DX = 0, and only
    jnc .dsok                       ; the CS: arm needs one)
    xor ax, ax
.dsok:
    mov dx, ax
    mov es, [ss:bp+26]              ; ES:SI = the instruction
    mov si, [ss:bp+24]
    mov cx, 4                       ; prefixes, at most
    xor bx, bx                      ; BX = where the CS: prefix is, +1
.pfx:
    mov al, [es:si+bx]
    cmp al, 0x2E
    je .csp
    cmp al, 0x26
    je .np
    cmp al, 0x36
    je .np
    cmp al, 0x3E
    je .np
    cmp al, 0xF3
    je .np
    cmp al, 0xF2
    je .np
    cmp al, 0xF0
    jne .op
.np:
    inc bx
    loop .pfx
    jmp mega_report
.csp:
    mov [mega_tcs], bx
    inc word [mega_tcs]
    inc bx
    loop .pfx
    jmp mega_report
.op:
    cmp word [mega_tcs], 0
    jne .tramp
    ; --- no CS: prefix - a code selector in DS or ES -----------------------
    mov cx, 0                       ; CX = registers fixed
    mov ax, [ss:bp+2]
    call mega_cs2dsw
    jc .es
    mov [ss:bp+2], ax
    inc cx
.es:
    mov ax, [ss:bp+0]
    call mega_cs2dsw
    jc .fixed
    mov [ss:bp+0], ax
    inc cx
    call mega_strfix                ; a MOVS/STOS: the 286 has moved on
.fixed:
    jcxz .noreg
    jmp mega_gpfix.resume           ; the same instruction, again
.noreg:
    jmp mega_null0                  ; ...or segment 0 in DS or ES
.tramp:
    ; --- a CS: write: BX = prefix bytes, AL = the opcode -------------------
    or dx, dx
    jz mega_report                  ; a CS: write from .cold
    mov di, bx                      ; DI = the instruction's length so far
    inc di                          ; ...with the opcode
    mov ah, 0                       ; AH = the immediate's bytes
    cmp al, 0xA2
    je .moffs
    cmp al, 0xA3
    je .moffs
    mov cl, al                      ; ALU r/m,reg: 00-3F with low bits 0/1
    cmp al, 0x40
    jae .grp
    and cl, 0x06
    jnz mega_report                 ; (a reg,r/m or AL,imm form: no write)
    jmp .modrm
.grp:
    cmp al, 0x80
    je .ib
    cmp al, 0x82
    je .ib
    cmp al, 0x83
    je .ib
    cmp al, 0x81
    je .iw
    cmp al, 0xC6
    je .ib
    cmp al, 0xC7
    je .iw
    cmp al, 0xC0
    je .ib
    cmp al, 0xC1
    je .ib
    cmp al, 0x86
    jb mega_report
    cmp al, 0x89
    jbe .modrm                      ; xchg, mov r/m,reg
    cmp al, 0x8C
    je .modrm
    cmp al, 0x8F
    je .modrm
    cmp al, 0xD0
    jb mega_report
    cmp al, 0xD3
    jbe .modrm
    cmp al, 0xFE
    jae .modrm
    cmp al, 0xF6
    je .modrm
    cmp al, 0xF7
    je .modrm
    jmp mega_report
.iw:
    inc ah
.ib:
    inc ah
.modrm:
    push bx
    mov bl, [es:si+bx+1]            ; the modrm byte, after the opcode
    inc di
    mov bh, bl
    and bh, 0xC0
    cmp bh, 0xC0
    je .bad                         ; a register: no memory written
    cmp bh, 0x40
    je .d1
    cmp bh, 0x80
    je .d2
    and bl, 7                       ; mod 0: only rm 6 has a displacement
    cmp bl, 6
    jne .dn
.d2:
    inc di
.d1:
    inc di
.dn:
    pop bx
    mov al, [es:si+bx]              ; F6/F7 /0 and /1 (TEST) carry an immediate
    cmp al, 0xF6
    je .test
    cmp al, 0xF7
    jne .len
.test:
    push bx
    mov bl, [es:si+bx+1]
    test bl, 0x30
    pop bx
    jnz .len
    mov ah, 1
    cmp al, 0xF6
    je .len
    inc ah
    jmp short .len
.bad:
    pop bx
    jmp mega_report
.moffs:
    add di, 2                       ; A2/A3: a 16-bit address
.len:
    mov al, ah
    xor ah, ah
    add di, ax                      ; DI = the whole length
    cmp di, MEGA_TRN - 2            ; (- 2: room for 8C's immediate, below)
    ja mega_report
    ; --- copy it, CS: made DS:, NOPs after --------------------------------
    push bx                         ; the opcode's index, for 8C below
    push ds
    pop es                          ; ES = the kernel's data, for the copy
    mov cx, di
    push ds
    mov ds, [ss:bp+26]              ; DS:SI = the instruction, through CS's
    xor bx, bx                      ; selector (readable)
.cp:
    mov al, [si+bx]
    cmp al, 0x2E
    jne .cp1
    cmp bx, [es:mega_tcs]           ; THE prefix (BX = its index + 1 - 1)
    jae .cp1
    mov al, 0x3E
.cp1:
    mov [es:mega_tramp+bx], al
    inc bx
    loop .cp
    pop ds
    ; --- MOV r/m16, DS or CS (8C /3, 8C /1): the trampoline runs with DS =
    ; the image's data segment and CS = KERNEL_SEG, so either would store the
    ; wrong segment (KERN-MEGA-PLAN 10.3 items 60 and 63). It becomes MOV
    ; r/m16, imm16 (C7 /0, two bytes longer) with the value the code had as
    ; its immediate: the DS it faulted with, or its own CS. ES and SS are
    ; the caller's in the trampoline already.
    pop si                          ; SI = the opcode's index
    cmp byte [mega_tramp+si], 0x8C
    jne .nop
    mov al, [mega_tramp+si+1]       ; the modrm byte
    mov ah, al
    and ah, 0x38                    ; ...its reg field
    mov cx, [ss:bp+2]               ; reg 3: the caller's DS
    cmp ah, 0x18
    je .sreg
    mov cx, [ss:bp+26]              ; reg 1: the caller's CS
    cmp ah, 0x08
    jne .nop
.sreg:
    and al, 0xC7                    ; reg 0, the same r/m and displacement
    mov byte [mega_tramp+si], 0xC7
    mov [mega_tramp+si+1], al
    mov [mega_tramp+bx], cx         ; BX = the copied length: the immediate
    inc bx
    inc bx
.nop:
    cmp bx, MEGA_TRN
    jae .copied
    mov byte [mega_tramp+bx], 0x90
    inc bx
    jmp short .nop
.copied:
    mov word [mega_tcs], 0
    mov ax, [ss:bp+24]              ; back to just past it
    add ax, di
    mov [mega_tret], ax
    mov ax, [ss:bp+26]
    mov [mega_tret+2], ax
    mov ax, [ss:bp+2]               ; DS as it was, put back after
    mov [mega_tds], ax
    mov [ss:bp+2], dx               ; ...and the image's for the instruction
    mov byte [mega_tif], 0x90       ; IF: back on after, if it was on
    test byte [ss:bp+29], 0x02
    jz .noif
    mov byte [mega_tif], 0xFB       ; sti
.noif:
    and byte [ss:bp+29], 0xFD       ; ...and off while it runs
    mov word [ss:bp+24], mega_tramp
    mov word [ss:bp+26], KERNEL_SEG
    jmp mega_gpfix.resume

; mega_cs2dsw - mega_cs2ds for a selector a WRITE is about to go through: DS
; or ES in mega_gpw's no-prefix arm (KERN-MEGA-PLAN 10.3 items 59 and 63).
; It walked the claim map once, because the LDT held a code alias for every
; 512-byte base below A000 and a segment built by one-paragraph arithmetic
; loaded as the alias of some other base. Those aliases went with MC7's step
; 6 (KERN-MEGA-MEMORY-PLAN): an LDT selector below the allocator's range is
; the GDT's own entry now, which mega_cs2ds refuses, so the two are one.
mega_cs2dsw equ mega_cs2ds

; AX = a CODE selector -> AX = the DATA selector with its base, CF = 0; CF = 1
; for anything else (a data selector, the null one, .cold's code). An image's
; code selector is the CODE half of its pair, whose data is at + 8
; (KERN-MEGA-MEMORY-PLAN MC7.4); below the allocator's range the LDT is the
; GDT it sits on, and nothing there is an image's (MC7's step 6).
mega_cs2dsf:                        ; ...for a FAR caller (.cold)
    call mega_cs2ds
    retf
mega_cs2ds:
    test al, 4
    jnz .ldt
    cmp ax, KERNEL_SEG
    jne .no
    mov ax, MEGA_SEL_KDATA
    clc
    ret
.ldt:
    and al, 0xF8
    cmp ax, MEGA_LDT_FREE0 * 8      ; the allocator's: the CODE half of a
    jb .no                          ; pair has its data at + 8, and anything
    or al, 4                        ; else there is not an image's
    jmp mega_ldt2ds
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; mega_null0 - SEGMENT 0 in DS or ES (KERN-MEGA-PLAN 10.1 item 17)
;
; Real-mode code reaches the IVT and the BDA through segment 0. In protected
; mode 0 is the NULL selector and the first access through it is #GP(0) - and
; a DRIVER does it to hook its IRQ (SB.DRV, USBMOUSE.DRV) where the kernel's
; own sites take MEGA_SEG0. So the instruction is RUN rather than rewritten:
; the null register becomes LOW0 for exactly one instruction, under the trap
; flag with IF off, and mega_db puts the 0 back and re-reads the IRQ vectors
; into the IDT (mega_ivt_sync), which a hook written after mega_enter would
; otherwise never reach. No decoding: whatever the instruction is, the CPU
; runs it. A REP string op traps after each element and comes back here for
; the next, which is slow and correct.
; -----------------------------------------------------------------------------
mega_null0:
    ; STKBALANCE-OK: returns by iret from the fault frame, never by ret
    xor al, al                      ; AL = what mega_db puts back: 1 DS, 2 ES,
    cmp word [ss:bp+2], 0           ; 4 IF was on
    jne .es
    mov word [ss:bp+2], MEGA_SEL_LOW0
    or al, 1
.es:
    cmp word [ss:bp+0], 0
    jne .any
    mov word [ss:bp+0], MEGA_SEL_LOW0
    or al, 2
    call mega_strfix                ; a STOS/MOVS into segment 0: the same
.any:                               ; 286 restart as mega_gpw's
    or al, al
    jz .no                          ; neither: a #GP(0) of some other kind
    cmp byte [mega_n0], 0
    jne .no                         ; already stepping one: never nests
    test byte [ss:bp+29], 0x02
    jz .noif
    or al, 4
.noif:
    mov [mega_n0], al
    and byte [ss:bp+29], 0xFD       ; IF off for the step...
    or byte [ss:bp+29], 0x01        ; ...and TF on
    jmp mega_gpfix.resume
.no:
    jmp mega_report

; mega_db - #DB, the trap after mega_null0's one instruction. Anything else
; that arrives here is a real debug exception and is reported.
mega_db:
    ; STKBALANCE-OK: returns by iret from the fault frame, never by ret
    mov al, [mega_n0]
    or al, al
    jz .rep
    mov byte [mega_n0], 0
    test al, 1
    jz .es
    cmp word [ss:bp+2], MEGA_SEL_LOW0 ; ...unless the instruction loaded DS
    jne .es                         ; itself (LDS from the IVT)
    mov word [ss:bp+2], 0
.es:
    test al, 2
    jz .f
    cmp word [ss:bp+0], MEGA_SEL_LOW0
    jne .f
    mov word [ss:bp+0], 0
.f:
    and byte [ss:bp+29], 0xFE       ; TF off
    test al, 4
    jz .sync
    or byte [ss:bp+29], 0x02        ; IF back
.sync:
    call mega_ivt_sync
    jmp mega_gpfix.resume
.rep:
    jmp mega_report

; -----------------------------------------------------------------------------
; mega_ivt_sync - the IRQ vectors, IVT to IDT, when they have changed
;
; in:   protected mode, DS = KDATA. Every register preserved but FLAGS.
;
; IVT 0Ah-0Fh are IDT 22h-27h and IVT 70h-77h are IDT 70h-77h. They are
; compared against the copy taken at the last sync (64 bytes) and the gates
; rebuilt only on a difference, so the common case is one REPE CMPSW. A
; vector a driver stamped with its own CS holds a PROTECTED-MODE selector -
; its pair's code half - which real mode cannot use and the next reflection
; would jump through, so it is written back as the image's segment (its
; descriptor's base) first, and the gate builder turns that into the pair.
; mega_ivt_syncf is the far door, for a kernel site that writes a vector
; through MEGA_SEG0 (which does not trap).
; -----------------------------------------------------------------------------
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_ivt_syncf:
    cmp word [cs:k_dsel], KERNEL_SEG
    je .rm                          ; before mega_enter there is no IDT to sync
    push ds
    push word MEGA_SEL_KDATA
    pop ds
    call MEGA_SEL_CODE:mega_ivt_syncm ; (`.mega`'s: KERN-MEGA-SEGMENTS-PLAN)
    pop ds
.rm:
    retf

section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_ivt_sync:
    pushf
    cli                             ; no IRQ through a gate half rebuilt
    pusha
    push es
    mov ax, MEGA_SEL_LOW0
    mov es, ax
    cld
    mov si, 0x08 * 4                ; --- an alias written back as a segment
    call .fixrow
    mov si, 0x70 * 4
    call .fixrow
    mov si, 0x08 * 4                ; --- changed since the last time?
    mov di, mega_ivtsh
    call .cmprow
    jne .build
    mov si, 0x70 * 4
    mov di, mega_ivtsh + 32
    call .cmprow
    je .out
.build:
    push ds                         ; the copy, for next time
    pop es
    mov ax, MEGA_SEL_LOW0
    mov ds, ax
    mov si, 0x08 * 4
    mov di, mega_ivtsh
    mov cx, 16
    rep movsw
    mov si, 0x70 * 4
    mov cx, 16
    rep movsw
    push ds                         ; ...IVT 20h-27h, which is where an IRQ
    pop es                          ; lands in REAL mode too, the PIC staying
    mov si, 0x08 * 4                ; at 20h across a reflection
    mov di, 0x20 * 4
    mov cx, 16
    rep movsw
    push ss                         ; (any data selector but LOW0, for ES -
    pop es                          ; DS is put right from it below)
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov ax, MEGA_SEL_IDT            ; ...and the gates - from IRQ2: IRQ0 and
    mov es, ax                      ; IRQ1 are the kernel's own here (sch_isr,
    mov bx, 0x22                    ; kbm_isr), and their IVT cells hold the
    mov di, 0x22 * 8                ; BIOS's halves FOR A REFLECTION (mega_enter)
.g1:
    call KERNEL_SEG:mega_idt_gatef
    inc bx
    cmp bx, 0x28
    jb .g1
    mov bx, 0x70
    mov di, 0x70 * 8
.g2:
    call KERNEL_SEG:mega_idt_gatef
    inc bx
    cmp bx, 0x78
    jb .g2
.out:
    pop es
    popa
    popf
    ret
.cmprow:                            ; LOW0:SI = 8 IVT cells, KDATA:DI = their
    push ds                         ; copy -> ZF = 1 the same. CMPSW reads
    push es                         ; DS:SI against ES:DI, so the two swap
    pop ds                          ; for it and back
    pop es
    mov cx, 16
    repe cmpsw
    push ds
    push es
    pop ds
    pop es
    ret
.fixrow:                            ; ES:SI = 8 IVT cells
    mov cx, 8
.fx:
    mov ax, [es:si+2]
    mov dx, ax
    and dl, 7
    cmp dl, 4                       ; TI = 1, RPL 0: an LDT selector...
    jne .fn
    cmp ax, MEGA_LDT_FREE0 * 8      ; ...the allocator's - a pair's half a
    jb .fn                          ; driver stamped with its own CS
    call KERNEL_SEG:mega_s2segf     ; ...whose image is the segment real mode
    cmp ax, 0xFFFF                  ; wants (above 1MB there is none: left
    je .fn                          ; for the gate, the IRQ masked across a
    mov [es:si+2], ax               ; reflection)
.fn:
    add si, 4
    loop .fx
    ret

section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_n0     db 0                    ; mega_null0 -> mega_db: what to put back
mega_ivtsh  times 32 dw 0           ; IVT 08h-0Fh and 70h-77h at the last sync

MEGA_TRN    equ 12                  ; the longest instruction it will copy
mega_tramp:                         ; ...run here, at KERNEL_SEG (written
    times MEGA_TRN nop              ; through the data alias: same bytes)
    mov ds, [cs:mega_tds]
mega_tif:   nop                     ; sti, when the caller's IF was on
    jmp far [cs:mega_tret]
mega_tds    dw 0
mega_tret   dw 0, 0
mega_tcs    dw 0                    ; the CS: prefix's index + 1, 0 = none

mega_gps    dw 0                    ; mega_gpfix's S
mega_gpop   db 0                    ; ...and FF's /reg

; -----------------------------------------------------------------------------
; THE 286 RESTARTS A FAULTED STRING WRITE ONE ELEMENT LATE (KERN-MEGA-PLAN
; 10.3 item 54.1). mega_gpw and mega_null0 fix a WRITE through a code selector
; or segment 0 by converting the register in the fault frame and running the
; instruction again - which is exact on a 386 (and Bochs) and is NOT on the
; Victory's 80286: a REP MOVSB into HDDTOOL's code alias came back with its
; first byte never written and every later byte in place (`#ERNEL`), so the
; frame's SI and DI had ALREADY moved past the element whose write faulted.
; Nothing was written - ES named a code segment for the whole instruction, so
; the very first write is the one that faults - so the right restart is the
; instruction's own starting registers, and the only question is how far the
; CPU moved them. That is MEASURED rather than assumed: mega_strcal faults a
; REP MOVSB and a plain MOVSB of its own once, at mega_enter, and keeps what
; the frame said (how many elements SI, DI and CX had moved); mega_strfix takes it
; back off every MOVS/STOS restart. A 386 measures 0 and nothing changes.
; -----------------------------------------------------------------------------
mega_scal   db 0                    ; armed: the probe's instruction length;
                                    ; 80h once its fault has been taken
mega_scsi   dw 0                    ; the frame's SI, DI, CX at that fault
mega_scdi   dw 0
mega_sccx   dw 0
mega_sdr    db 0, 0, 0, 0           ; REP MOVS: elements SI, DI, CX moved,
                                    ; then the saved IP's offset into it
mega_sdp    db 0, 0, 0, 0           ; a plain MOVS: the same (CX unused)
mega_scip   dw 0                    ; where the probe's fault resumes
mega_scfip  dw 0                    ; ...and the IP the CPU saved
MEGA_SCOFF  equ 0x0100              ; the probe's SI and DI: any offset, the
                                    ; write never happens
MEGA_SCCNT  equ 8                   ; ...and its CX: room for an advance of
                                    ; more than one element to be SEEN

section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_strcal_gp:                     ; the probe's #GP: record and skip it
    ; STKBALANCE-OK: returns by iret from the fault frame, never by ret
    mov ax, [ss:bp+6]
    mov [mega_scsi], ax
    mov ax, [ss:bp+4]
    mov [mega_scdi], ax
    mov ax, [ss:bp+16]
    mov [mega_sccx], ax
    mov ax, [ss:bp+24]
    mov [mega_scfip], ax            ; where the CPU says it faulted
    mov ax, [mega_scip]
    mov [ss:bp+24], ax              ; resume after it, at a LABEL: nothing
    mov byte [mega_scal], 0x80      ; written, and no length assumed
    jmp mega_gpfix.resume

; mega_strcal - measure it (mega_enter, protected mode, DS = KDATA). The
; result goes to E9 as `kern_mega: string restart r<si><di><cx><ip> p...`.
; Preserves every register
mega_strcal:
    pusha
    push es
    pushf
    cli
    cld
    mov ax, KERNEL_SEG              ; the kernel's CODE selector: read-only
    mov es, ax
    mov bx, mega_sdr
    mov word [mega_scip], .r1
    mov byte [mega_scal], 1
    call .probe
.i1:
    rep movsb
.r1:
    mov dx, .i1
    call .take
    mov bx, mega_sdp
    mov word [mega_scip], .r2
    mov byte [mega_scal], 1
    call .probe
.i2:
    movsb
.r2:
    mov dx, .i2
    call .take
    mov byte [mega_scal], 0
    mov si, mega_mscal
    call mm_puts
    mov si, mega_sdr
    mov cx, 8
.say:
    cmp cx, 4
    jne .d
    push si
    mov si, mega_mscalp
    call mm_puts
    pop si
.d:
    mov al, [si]
    add al, '0'
    call mm_putc
    inc si
    loop .say
    mov al, 10
    call mm_putc
    popf
    pop es
    popa
    ret
.probe:                             ; SI = DI = MEGA_SCOFF, CX = 2
    mov si, MEGA_SCOFF
    mov di, MEGA_SCOFF
    mov cx, MEGA_SCCNT              ; WHAT .take MEASURES AGAINST: it was
    ret                             ; "as given", so a boot that reached
                                    ; mega_enter with CX = 0 ran a REP MOVSB
                                    ; of nothing, took no fault, and read
                                    ; "restarts exactly" on a 286 that does
                                    ; not (KERN-MEGA-PLAN 14.30)
.take:                              ; [BX] = SI, DI, CX moved, in ELEMENTS
    cmp byte [mega_scal], 0x80
    jne .t0                         ; no fault: nothing measured, all 0
    mov ax, [mega_scsi]
    sub ax, MEGA_SCOFF
    call .one
    mov [bx], al
    mov ax, [mega_scdi]
    sub ax, MEGA_SCOFF
    call .one
    mov [bx+1], al
    mov ax, MEGA_SCCNT
    sub ax, [mega_sccx]
    call .one
    mov [bx+2], al
    mov ax, [mega_scfip]            ; ...and how far past the instruction's
    sub ax, dx                      ; first byte the CPU put IP: 0 is the
    mov [bx+3], al                  ; prefix, as the manual says. Reported
.t0:                                ; only - nothing acts on it
    ret
.one:                               ; AX = 0..MEGA_SCCNT-1 as it is; anything
    cmp ax, MEGA_SCCNT              ; else -> 0. It WAS "1 -> 1, else 0", and
    jb .o1                          ; 86Box's 80286 takes CX down by TWO at
    xor al, al                      ; the first faulting element of a REP
.o1:                                ; MOVSB while SI and DI move by one - so
    ret                             ; the probe read 0, and every restart
                                    ; copied two bytes short (KERN-MEGA-PLAN
                                    ; 14.32). Measured, not capped: the
                                    ; figure is what strfix gives back

; mega_strfix - a MOVS or STOS whose ES the fault frame has just had
; converted: take the 286's early advance back off SI, DI and CX (mega_strcal's
; figures, DF's direction, the element's size). Anything else returns at
; once. BP = the frame, DS = KDATA. Preserves every register
mega_strfix:
    push ax
    mov al, [mega_sdr]
    or al, [mega_sdr+1]
    or al, [mega_sdr+2]
    or al, [mega_sdp]
    or al, [mega_sdp+1]
    jz .ret0                        ; a CPU that restarts exactly
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [ss:bp+26]              ; ES:SI = the instruction
    mov si, [ss:bp+24]
    mov bx, mega_sdp                ; BX = the figures: plain, until a REP
    mov cx, 4
.pfx:
    mov al, [es:si]
    cmp al, 0xF3
    je .rep
    cmp al, 0xF2
    je .rep
    cmp al, 0x26
    je .np
    cmp al, 0x2E
    je .np
    cmp al, 0x36
    je .np
    cmp al, 0x3E
    je .np
    cmp al, 0xF0
    jne .op
    jmp short .np
.rep:
    mov bx, mega_sdr
.np:
    inc si
    loop .pfx
    jmp short .out
.op:
    xor dx, dx
    mov dl, 1                       ; DX = one element, signed by DF
    test al, 1
    jz .b
    inc dx
.b:
    and al, 0xFE
    cmp al, 0xA4                    ; MOVSB/MOVSW
    je .dir
    cmp al, 0xAA                    ; STOSB/STOSW
    jne .out
    mov byte [mega_sdtmp], 0        ; (STOS: SI is not its)
    jmp short .dir2
.dir:
    mov byte [mega_sdtmp], 1
.dir2:
    test byte [ss:bp+29], 0x04      ; DF
    jz .fwd
    neg dx
.fwd:
    mov cl, [bx+1]                  ; DI back, by the elements it moved
    mov ch, 0
    jcxz .nodi
.di:
    sub [ss:bp+4], dx
    loop .di
.nodi:
    cmp byte [mega_sdtmp], 0
    je .nosi
    mov cl, [bx]                    ; SI back, the same
    jcxz .nosi
.si:
    sub [ss:bp+6], dx
    loop .si
.nosi:
    cmp bx, mega_sdr
    jne .out
    mov cl, [bx+2]                  ; ...and CX up by what it was taken down
    add [ss:bp+16], cx              ; (CH is 0)
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
.ret0:
    pop ax
    ret
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_sdtmp  db 0

; -----------------------------------------------------------------------------
; mega_dsimg / mega_csdata - an image's own DATA segment, for PUSH_CSDATA and
; CSDO (kernel/kernel.asm). FAR, and called from the image: the CS a far call
; pushes IS the caller's, so it is read off the return address rather than
; passed. In real mode that CS is the segment itself; in protected mode it is
; the CODE half of the image's pair, whose data is + 8 (KERN-MEGA-MEMORY-PLAN
; MC7.4) - or the kernel's own CS, whose data is MEGA_SEL_KDATA. FLAGS and
; every register but the answer preserved.
; -----------------------------------------------------------------------------
mega_dsimg:                         ; out: DS = the caller's image, as data
    push bp
    mov bp, sp
    pushf
    push ax
    mov ax, [ss:bp+4]               ; the caller's CS
    cmp word [cs:k_dsel], KERNEL_SEG
    je .rm
    and al, 0xF8
    cmp ax, MEGA_LDT_FREE0 * 8      ; an image: a CODE/DATA PAIR from the
    jae .pair                       ; allocator, data = code + 8
    mov ax, MEGA_SEL_KDATA          ; ...or the kernel's own CS
.rm:
    mov ds, ax
    pop ax
    popf
    pop bp
    retf
.pair:
    add ax, 8 | 4
    mov ds, ax
    pop ax
    popf
    pop bp
    retf

mega_csdata:                        ; the word under the return address - the
    push bp                         ; caller's `push cs` - made its data
    mov bp, sp                      ; segment
    pushf
    cmp word [cs:k_dsel], KERNEL_SEG
    je .rm
    and byte [ss:bp+6], 0xF8
    cmp word [ss:bp+6], MEGA_LDT_FREE0 * 8
    jae .pair                       ; an image: the pair's data half...
    mov word [ss:bp+6], MEGA_SEL_KDATA ; ...or the kernel's own data
.rm:
    popf
    pop bp
    retf
.pair:
    add word [ss:bp+6], 8 | 4
    popf
    pop bp
    retf


; mega_mods_fix - the module slots the BOOT armed hold a real-mode segment
; (mod_check stores the CODE half only once mega_pm is set), so every loaded
; module is converted here, after mega_mpall has made its claim's pair
; (KERN-MEGA-MEMORY-PLAN MC7.4): MODR_SEG becomes the pair's DATA selector and
; a slot naming its image's segment the pair's CODE half, data - 8. A
; disarmed slot (mod_gone, KERNEL_SEG) is already right. mod_fpr's
; arithmetic, inline: that one is `.cold`.
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_mods_fix:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, mod_tab
.r:
    cmp bx, mod_tab_end
    jae .done
    mov ax, [bx+MODR_SEG]
    mov dx, ax
    call mega_p2d                   ; DX = its claim's data selector
    jc .n
    mov [bx+MODR_SEG], dx
    sub dx, 8                       ; DX = its code half
    mov di, bx
    shr di, 1
    mov cx, [di + mod_fpt + 2 - (MOD_TAB_OFF >> 1)]
    mov di, [di + mod_fpt - (MOD_TAB_OFF >> 1)]
    sub cx, di
    shr cx, 1
    shr cx, 1
    jcxz .n
.s:
    cmp [di+2], ax
    jne .k
    mov [di+2], dx
.k:
    add di, 4
    loop .s
.n:
    add bx, MODR_SIZE
    jmp short .r
.done:
    mov di, dkv                     ; ...and the two vector tables a module
    mov cx, DKI_N                   ; writes ITSELF into with `mov reg, cs`
    call mega_vfix                  ; (DOCK.DRV's dkv, WIN95.DRV's thv): right
    mov di, thv                     ; in protected mode, a real-mode segment
    mov cx, THV_N                   ; if the boot hooked them
    call mega_vfix
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; DI = a table of CX far pointers: every segment that is a claim's base - a
; module image's - becomes that claim's CODE half. Clobbers AX, CX, DI, flags.
mega_vfix:
    push dx
.l:
    mov dx, [di+2]
    call mega_p2d
    jc .k
    sub dx, 8
    mov [di+2], dx
.k:
    add di, 4
    loop .l
    pop dx
    ret

; mega_p2d - DX = a real-mode segment -> DX = the DATA selector of the live
; claim based there, CF = 0; CF = 1 (DX kept) none is. At the switch, for a
; word real mode wrote (KERN-MEGA-MEMORY-PLAN MC7.4). Clobbers nothing else
mega_p2d:
    push bx
    push si
    or dx, dx
    jz .no
    mov si, mem_tab
    mov bx, mem_sel
.l:
    cmp [ss:si+MC_SEG], dx
    je .f
    add si, MC_SIZE
    inc bx
    inc bx
    cmp si, mem_tab + MEM_MAX * MC_SIZE
    jb .l
.no:
    stc
    jmp short .o
.f:
    cmp word [ss:bx], 0
    je .no
    mov dx, [ss:bx]
    clc
.o:
    pop si
    pop bx
    ret

; mega_s2segf - mega_s2seg for a FAR caller (.cold): AX = a selector in, the
; real-mode segment with its base out, everything else preserved. retf.
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_s2segf:
    call mega_s2seg
    retf

; AX = a selector -> AX = the real-mode segment with the same base. READ OFF
; THE DESCRIPTOR rather than assumed: an identity selector's base is N x 16 by
; construction, but GDT entries 1-7 are the kernel's own and MEGA_SEL_ODD's
; base moves, and a DMA transfer handed the wrong segment lands where nothing
; can watch it - it took the API table out once, from a package load whose
; decompressor target was MEGA_SEL_ODD. A base above 1MB has no real-mode
; segment and comes back as 0xFFFF, which no BIOS transfer can land on.
mega_s2seg:
    cmp word [cs:k_dsel], KERNEL_SEG ; REAL MODE: AX is a segment already
    je .ret                         ; (mega_oddsegf's note)
    or ax, ax
    jz .ret                         ; the null selector: 0, as it was
    test al, 4
    jz .gdt
    push es                         ; LDT: its descriptor, through LDTD (the
    push bx                         ; GDT's own entries below the allocator's
    and al, 0xF8                    ; range, MC7's step 6)
    mov bx, ax
    mov ax, MEGA_SEL_LDTD
    jmp short .rd
.ret:
    ret
.gdt:
    push es
    push bx
    and al, 0xF8
    mov bx, ax
    mov ax, MEGA_SEL_WIN            ; the GDT, as data
.rd:
    mov es, ax
    mov ax, [es:bx+2]               ; base 15:0
    mov bl, [es:bx+4]               ; base 23:16
    cmp bl, 0x0F
    ja .high
    shr ax, 4
    mov bh, bl
    shl bh, 4                       ; base 23:16 is segment 15:12
    or ah, bh
    jmp short .out
.high:
    mov ax, 0xFFFF
.out:
    pop bx
    pop es
    ret

section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_drvsayf:                       ; FAR: ES = a driver image just placed
    push ax                         ; above 1MB (drv_hi_move): its name to E9,
    push si                         ; for a debug port to read (item 82)
    mov si, mega_mdrv
    call mm_puts
    xor si, si
.n:
    mov al, [es:si+16]
    or al, al
    jz .e
    call mm_putc
    inc si
    cmp si, 15
    jb .n
.e:
    mov al, 10
    call mm_putc
    pop si
    pop ax
    retf
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
; THE DEBUG PORT'S WRITERS, twice from one body: `.text`'s for the code that
; runs in real mode or below the switch, and `.mega`'s (mm_*) for the code that
; lives there - each reads its strings through ITS OWN CS, so a string goes in
; the segment of the code that prints it (KERN-MEGA-SEGMENTS-PLAN 3.6)
%macro MEGA_E9IO 1
%1 %+ say:                              ; SI = a NUL string: to E9 and return
    call %1 %+ puts
    ret
%1 %+ puts:
    push ax
.l:
    mov al, [cs:si]
    inc si
    or al, al
    jz .d
    call %1 %+ putc
    jmp .l
.d:
    pop ax
    ret
%1 %+ putc:
    out 0xE9, al
    ret
%1 %+ hex4:
    push ax
    mov al, ah
    call %1 %+ hex2
    pop ax
%1 %+ hex2:
    push ax
    shr al, 4
    call .n
    pop ax
.n:
    push ax
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .o
    add al, 7
.o:
    call %1 %+ putc
    pop ax
    ret
%endmacro
    MEGA_E9IO mega_
section .mega                       ; ...and `.mega`'s, with ITS strings and
    MEGA_E9IO mm_                   ; the 8042's wait (kept beside mega_kbw)
mm_kbw:
    xor cx, cx
.w:
    in al, 0x64
    test al, 2
    loopnz .w
    ret
mega_mon    db "kern_mega: protected mode", 10, 0
mega_mhd13  db "kern_mega: a fixed-disk transfer went to the BIOS (int 13h reflected)", 10, 0
mega_mdrv   db "kern_mega: above 1MB: ", 0
mega_mfdoff db "kern_mega: native floppy OFF - eight fallbacks in a row", 10, 0
mega_mfdon  db "kern_mega: native floppy ", 0
mega_mfdfb  db "kern_mega: floppy fell back to the BIOS at step ", 0
mega_mflt   db 10, "kern_mega FAULT vector ", 0
mega_mfst   db "  stack: ", 0
mega_mreg   db 10, "  ES DS DI SI BP SP BX DX CX AX SS: ", 0
mega_mscal  db 'kern_mega: string restart r', 0
mega_mscalp db ' p', 0
section .text

; ---- the API's hot cells' stubs (OSAPI_SLOT's kern_mega arm) --------------
; One per OSAPI_SLOT, in table order: DS = the kernel's data, then on to the
; routine, whose near `ret` goes back to the cell's `pop ds / retf`.
%assign mega_i 0
%rep mega_nslot
mega_sl_%[mega_i]:
    mov ds, [cs:k_dsel]
    jmp mega_slt_%[mega_i]
 %assign mega_i mega_i + 1
%endrep

; ---- ...and OSAPI_FJCELL's: DS = the kernel's data, then the far jump to the
; .cold body, which pops the DS the cell pushed and retf's for it
%assign mega_i 0
%rep mega_nfj
mega_fj_%[mega_i]:
    mov ds, [cs:k_dsel]
    jmp COLD_SEG:mega_fjt_%[mega_i]
 %assign mega_i mega_i + 1
%endrep

; ---- the inline cells that store a register (MEGA_ICW's kern_mega arm) ----
%macro MEGA_ICW_STUB 2
mega_icw_%1:
    push ds
    mov ds, [cs:k_dsel]
    mov [%1], %2
    pop ds
    retf
%endmacro
    MEGA_ICW_STUB gfx_color, al
    MEGA_ICW_STUB osapi_seed, ax
%ifndef BANDCOMP
    MEGA_ICW_STUB gfx_b1ink, ax
%endif
    MEGA_ICW_STUB ico_c1, ax
%macro MEGA_ICWB_STUB 1
mega_icwb_%1:
    push ds
    mov ds, [cs:k_dsel]
    mov [bx+%1], ax
    pop ds
    retf
%endmacro
    MEGA_ICWB_STUB W_ONSIZE
    MEGA_ICWB_STUB W_ONSZ
    MEGA_ICWB_STUB W_ONWK
    MEGA_ICWB_STUB W_ONRC
    MEGA_ICWB_STUB W_ONCLICK
    MEGA_ICWB_STUB W_ONWH           ; (the wheel's, SPEC.md 13.18)

; mega_bhex4 / mega_bhex2 - AX (AL) as 4 (2) hex digits at ES:DI
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_bhex4:
    push ax
    mov al, ah
    call mega_bhex2
    pop ax
mega_bhex2:
    push ax
    shr al, 4
    call .n
    pop ax
.n:
    push ax
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .o
    add al, 7
.o:
    stosb
    pop ax
    ret

; -----------------------------------------------------------------------------
; mega_hibnc - int 13h AH=02h/03h into or out of a buffer ABOVE 1MB, for a
;              caller that is not the file layer (KERN-MEGA-PLAN 14.1 answer
;              11): a run at a time through a buffer below it - the
;              read-ahead's landing, nine page-safe sectors that are scratch
;              between fills, or dsk_secbuf when the landing is shed - each
;              run a NESTED int 13h, which goes native or is reflected like
;              any other. The file layer bounces its own (dsk_xfer's
;              `.bounce`) and never reaches this; a program that issues its
;              own int 13h from a high window does
; in:  BP = mega_svc's frame (ES restored to the caller's selector), DS = KDATA,
;      IF = 0
; out: the frame's AH = the status, AL = sectors moved, CF in its FLAGS
; clobbers: AX..DI, ES (mega_svc pops the frame). FAR: it is .cold, because
;           kern_mega's .text + .bss is the one 64KB segment and was within
;           33 bytes of it
;
; A request stays inside one track, as the BIOS's own rule is, so a run
; advances CL's sector number and nothing else. IF stays 0 between runs
; (the gate's), so no task can take the landing between the copy and the
; transfer: dsk_xfer's own fills raise [sch_lock] around theirs
; -----------------------------------------------------------------------------
; mega_rmaskf - FAR: the IRQ lines a service reflection masks (KERN-MEGA-PLAN
; 14.1 answer 18). BX = B8h on the master and 3Fh on the slave, less the
; MOUSE's line where its own ISR can run in real mode: everything on its path
; is 8086 code at KERNEL_SEG, and the one word that names a selector,
; [k_dsel], is KERNEL_SEG for the reflection (mega_svc). So the pointer moves
; through a reflected disk transfer, under SPEC.md 7.4's gates as on kern_big.
; The line stays masked when the mouse has not settled, when a DRIVER feeds
; it (its ISR may be an image above 1MB), when the vector is not the kernel's
; (a thief the theft repair has not been synced from), and for int 10h - the
; arrow must not draw in the middle of a mode set or a font load. A line whose
; real-mode vector is an OPTION ROM's is left open as well, on every service
; (KERN-MEGA-PLAN 14.73): a PicoMEM's multiplexer copies its Sound Blaster's
; blocks there, and its disk is one of those services.
; THE ONE WORD WAS NOT ONE (KERN-MEGA-PLAN 14.75): since MC7.4 the cursor's
; frame buffer is a SELECTOR too - [vid_seg] and VSEG's [k_vsa] - and the
; ISR loaded it as a segment, so every arrow it moved through a reflection
; was drawn into kernel memory at selector x 16 and the one on the glass was
; left there. mega_vrm makes them segments for the call and mega_svc_back
; puts them back; a display it cannot do that for keeps the line shut.
; in: BP = mega_svc's frame, DS = KDATA. out: BX. Clobbers AX
; -----------------------------------------------------------------------------
section .cold
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_rmaskf:
    mov bx, 0x3FB8
    cmp byte [ss:bp+20], 0x10
    je .out
    cmp byte [mou_seen], 0
    je .out
    push es
    mov ax, MEGA_SEL_LOW0
    mov es, ax
    mov al, [mou_port]
    cmp al, MOU_P2ROW
    je .p2
    cmp al, MOU_FEEDROW
    jae .k
    mov al, [mou_line]
    mov ah, 0x24                    ; IRQ4's vector in real mode (base 20h)
    cmp al, MOU_L4
    je .vec
    mov ah, 0x23
    cmp al, MOU_L3
    jne .k
.vec:
    push bx
    mov bl, ah
    xor bh, bh
    shl bx, 2
    cmp word [es:bx+2], KERNEL_SEG
    pop bx
    jne .k
    not al
    and bl, al
    jmp short .k
.p2:
    cmp word [es:0x74*4+2], KERNEL_SEG
    jne .k
    and bh, ~0x10 & 0xFF            ; IRQ12, the slave's bit 4
.k:
    pop es
    cmp bx, 0x3FB8                  ; THE LINE IS OPEN, SO THE ARROW WILL BE
    je .out                         ; DRAWN IN REAL MODE (KERN-MEGA-PLAN
    call mega_vrm                   ; 14.75): the frame buffer's words become
    jnc .out                        ; the SEGMENTS they name - and where they
    mov bx, 0x3FB8                  ; cannot, the line stays shut
.out:
    push es                         ; ...AND EVERY LINE AN OPTION ROM OWNS
    push dx                         ; (KERN-MEGA-PLAN 14.73): its handler is
    push si                         ; real-mode code in a ROM, which runs in
    mov ax, MEGA_SEL_IDT            ; the reflection as it is - and a
    mov es, ax                      ; PicoMEM's multiplexer (SPEC.md 34.10.1)
    mov si, 0x20*8                  ; is how its Sound Blaster's every block
    mov dx, 1                       ; is copied, and how its sample clock's
.rl:                                ; IRQs arrive: masked for a whole
    test bx, dx                     ; reflected int 13h - its own disk's -
    jz .rn                          ; the card played nothing it was not
    cmp word [es:si+2], MEGA_SEL_CODE   ; given and dropped the block IRQs.
    jne .rn                         ; THE KERNEL'S OWN VERDICT, not the
    mov ax, [es:si]                 ; vector's value: an IVT cell can hold
    sub ax, mega_irv                ; a SELECTOR (a driver's ISR, any value
    cmp ax, 14*6                    ; at all), so the test is the line's
    jae .rn                         ; gate - a reflecting stub (mega_irv),
    not dx                          ; which mega_irqrom builds only where the
    and bx, dx                      ; IVT cell named an option ROM. The SB's
    not dx                          ; own line stays masked, and that bit is
.rn:                                ; what the multiplexer reads before it
    add si, 8                       ; raises a software int
    cmp dx, 0x80                    ; (the master's last: the slave's gates
    jne .rs                         ; are at 70h)
    mov si, 0x70*8
.rs:
    shl dx, 1
    jnz .rl
    pop si
    pop dx
    pop es
    mov [mega_rmask], bx
    retf

; mega_vrm - the words the pointer's ISR draws through, made REAL SEGMENTS
; for a service reflection whose mask lets the ISR run (mega_rmaskf;
; KERN-MEGA-PLAN 14.75). cur_move reaches the frame buffer through exactly
; two: [vid_seg] (the 1bpp move and the planar draw) and VSEG's [k_vsa] (the
; planar save and restore) - both selectors since MC7.4, which a real-mode
; `mov es` takes as a paragraph. PROTECTED MODE, DS = KDATA, IF = 0 (the
; gate's, to mega_svc_back), so no protected-mode draw sees them changed.
; out: CF = 0 done, the selectors banked in [mega_vsw] for mega_svc_back;
;      CF = 1 refused and nothing changed - a PACKED display (its cursor is a
;      driver's far call, VIDEXT, through a selector), more than one display
;      (cur_move_multi's vid_ctx_act loads each context's selectors), or a
;      frame buffer above 1MB. Clobbers AX
mega_vrm:
    cmp byte [vid_planes], 4
    ja .no
    cmp byte [vid_ndisp], 1
    ja .no
    mov ax, [vid_seg]
    call KERNEL_SEG:mega_s2segf     ; AX = the segment with its base...
    cmp ax, 0xFFFF                  ; ...or none, above 1MB
    je .no
    push word [vid_seg]
    pop word [mega_vsw]
    mov [vid_seg], ax
    push word [k_vsa]
    pop word [mega_vsw+2]
    mov word [k_vsa], VGA_SEG       ; (mega_vfixall's own source for it)
    clc
    ret
.no:
    stc
    ret

mega_hibnc:
    mov ax, [dsk_rah_bnc]
    xor bx, bx
    mov cl, DSK_RAH_SECS
    or ax, ax
    jnz .have
    mov ax, ss                      ; LOW_SEG: one sector at a time
    mov bx, dsk_secbuf
    mov cl, 1
.have:
    mov [mega_hbseg], ax
    mov [mega_hboff], bx
    mov [mega_hbcap], cl
    mov al, [ss:bp+18]              ; AL = the caller's sectors
    mov [mega_hbleft], al
    mov byte [mega_hbdone], 0
    mov ax, [ss:bp+12]              ; BX
    mov [mega_hbsi], ax
    mov ax, [ss:bp+16]              ; CX
    mov [mega_hbcx], ax
    mov al, [ss:bp+15]              ; DH, the head: a run is carried across
    mov [mega_hbdh], al             ; heads and cylinders here (.adv)
.run:
    mov al, [mega_hbleft]
    or al, al
    jz .ok
    cmp al, [mega_hbcap]
    jbe .n
    mov al, [mega_hbcap]
.n:
    mov ah, [disk_spt]              ; ...and stops at its TRACK's end: the
    mov cl, [mega_hbcx]             ; caller's run may cross a head - a fixed
    and cl, 0x3F                    ; disk's goes to the cylinder's end
    sub ah, cl                      ; (SPEC.md 18.91.5), a floppy's too
    inc ah                          ; (18.91.1) - and the ROM carried that in
    jle .nk                         ; one call, which a chunk at a time must
    cmp al, ah                      ; do itself. AH = the track's sectors
    jbe .nk                         ; left, by the geometry the caller's CHS
    mov al, ah                      ; was made with ([disk_spt]); a sector
.nk:                                ; past it leaves the old cap alone
    mov [mega_hbn], al
    cmp byte [ss:bp+19], 3
    jne .xfer
    xor ax, ax                      ; a write: the caller's run down first
    call .copy
.xfer:
    push bp
    mov ah, [ss:bp+19]
    mov al, [mega_hbn]
    mov cx, [mega_hbcx]
    mov dl, [ss:bp+14]
    mov dh, [mega_hbdh]
    mov bx, [mega_hboff]
    mov es, [mega_hbseg]
    int 0x13                        ; native or reflected, the landing low
    pop bp
    jc .err
    cmp byte [ss:bp+19], 2
    jne .adv
    mov al, 1                       ; a read: the run up after
    call .copy
.adv:
    mov al, [mega_hbn]
    sub [mega_hbleft], al
    add [mega_hbdone], al
    mov cx, [mega_hbcx]             ; THE NEXT CHS: the sector on, and past
    mov ah, cl                      ; the track's end (where .n stopped the
    and ah, 0x3F                    ; run) sector 1 of the next head, and
    add ah, al                      ; past the last head the next cylinder -
    and cl, 0xC0                    ; it was `add CL, AL`, which asked a
    cmp ah, [disk_spt]              ; ROM for sector 18 of a 17-sector track
    jbe .samet                      ; and was the field's "Disk error"
    mov ah, 1                       ; opening a file off the hard disk into
    inc byte [mega_hbdh]            ; a claim above 1MB (KERN-MEGA-PLAN
    mov al, [mega_hbdh]             ; 14.20)
    cmp al, [disk_heads]
    jb .samet
    mov byte [mega_hbdh], 0
    inc ch
    jnz .samet
    add cl, 0x40                    ; (cylinder bits 8-9 in CL's top)
.samet:
    or cl, ah
    mov [mega_hbcx], cx
    mov al, [mega_hbn]
    xor ah, ah
    xchg al, ah                     ; AX = sectors x 256...
    shl ax, 1                       ; ...x 2 = bytes
    add [mega_hbsi], ax
    jmp .run
.ok:
    xor ah, ah
    and byte [ss:bp+26], 0xFE
    jmp short .ans
.err:
    or byte [ss:bp+26], 1
.ans:
    mov al, [mega_hbdone]
    mov [ss:bp+18], ax
    retf
; AL = 0 the caller's buffer to the low one, 1 back. Clobbers AX, CX, SI, DI,
; DX, BX, ES; DS kept
.copy:
    push ds
    xor ch, ch
    mov ch, [mega_hbn]              ; CX = sectors x 256 words
    xor cl, cl
    mov si, [mega_hbsi]
    mov di, [mega_hboff]
    mov bx, [mega_hbseg]
    mov dx, [ss:bp+0]               ; the caller's selector
    or al, al
    jnz .back
    mov es, bx
    mov ds, dx
    jmp short .mv
.back:
    xchg si, di
    mov es, dx
    mov ds, bx
.mv:
    cld
    rep movsw
    pop ds
    ret

; -----------------------------------------------------------------------------
; mega_hugef - FAR: OSAPI_MEM_HUGE in protected mode (KERN-MEGA-PLAN 14.1
;              answers 6 and 7, 14.37): ONE pool block of AX KB and a RUN of
;              consecutive LDT data selectors over it, a 64KB window each
; in:  AX = KB (1..65535), ES = the caller, DS = KDATA, BX = 8000h for a
;      window every 32KB (below)
; out: CF = 0, DX = the first selector, CX = 8 (the step); CF = 1 not here -
;      not protected mode, not an OS88_HIGHMEM package (or OS88_DRV_HICLAIM
;      driver), no room, or no run of free LDT entries - and the cell goes
;      on to the conventional heap. Every other register kept, but BX = 0
;      when a window every 32KB was asked and given
;
; BX = 8000h (KERN-MEGA-PLAN 14.41): selector k is the 64KB window from 32KB
; x k, cut at the claim's end - OSAPI_XMEM_RUN's shape on a claim that moves,
; so a reader of records of 32KB or less finds ANY of them whole in the window
; at (offset >> 15), at (offset & 7FFFh). MX_FH32 says so; mx_unsel frees
; (KB + 31) / 32 selectors, the regrow refuses it, and a move takes 64KB
; pieces with FSXF_RATE's hook held off throughout (the odd windows cross
; every cut: mx_cpflags, KERN-MEGA-PLAN 14.49) - one step with interrupts
; off only while the package has a sample ISR open
;
; The pieces are contiguous because the block is: a selector's base is the
; block's plus 64KB x its index, and its limit 64KB - 1 but the last's, which
; is the claim's own end. The run is the block's MX_SEL, its first selector,
; MX_FHUGE says so, and mx_unsel frees all of it with the block - at
; OSAPI_MEM_FREE or at the instance's teardown, exactly as a one-selector
; claim is. The block MOVES (mx_compactf): the selectors stay, their bases
; follow it. "No room" is the pool's FREE KB, not its largest run - mx_alloc
; packs every movable block down before it refuses
; -----------------------------------------------------------------------------
section .cold                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_hugef:
    push ax
    push bx
    push si
    push di
    push bp
    push es
    cmp byte [mega_on], 0
    je .no
    or ax, ax
    jz .no
    mov si, bx                      ; SI = 8000h: a window every 32KB (14.41)
    call MEGA_SEL_CODE:mega_ownerf     ; BL = 0A0h | slot (or 0C0h | row)
    jc .no
    inc byte [sch_lock]             ; the base is aimed before any other
    mov bp, ax                      ; task can compact it away
    mov dx, ax
    shl ax, 10
    shr dx, 6                       ; DX:AX = bytes
    call MEGA_SEL_CODE:mx_allocf       ; DX:AX = the block's base
    jc .nol
    mov cx, bp
    call mega_hgcntx                ; CX = the selectors for SI's step
    mov di, MEGA_SEL_LDTD
    mov es, di
    pushf
    cli                             ; the scan and the claim are one step
    call mega_hgfree                ; DI = a run of CX free entries
    jc .full
    push bx
    mov bx, bp
    call mega_hgfillx               ; ...filled at SI's step
    pop bx
    popf
    lea bp, [di+4]                  ; BP = the first selector (TI = 1)
    call mega_mxat                  ; DI = the block's entry
    mov [ss:di+MX_SEL], bp
    or byte [ss:di+MX_FL], MX_FHUGE
    mov dx, bp
    mov cx, 8
    cmp si, 0x8000
    jne .n32
    or byte [ss:di+MX_FL], MX_FH32
    jmp short .met
.n32:
    cmp si, 0x4000
    jne .ok
    or byte [ss:di+MX_FL], MX_FH16
.met:
    mov bp, sp
    mov word [ss:bp+8], 0           ; the caller's BX: 0, the ask was met
.ok:
    clc
    jmp short .out
.full:
    popf                            ; no run: the block goes back
    call MEGA_SEL_CODE:mx_freef
.nol:
    stc
.out:
    dec byte [sch_lock]             ; (dec leaves CF)
    jmp short .o
.no:
    stc
.o:
    pop es
    pop bp
    pop di
    pop si
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; mega_hugeqf - FAR: OSAPI_MEM_HUGE's QUERY in protected mode (KERN-MEGA-
; MEMORY-PLAN MC5, D6): AX = the largest CONVENTIONAL claim (mem_avail_lvl_x's
; answer), ES = the caller -> AX = the largest one claim of the cell's can
; be: the pool alone (mx_maxrun - a caller its pool owner rule admits), the
; conventional heap alone, or the BRIDGE - the pool's run cut to whole 64KB
; windows and then the heap less its 64KB reserve (D8). Exact for the pack
; the claim will make; real mode, AX unchanged. Every other register kept
; -----------------------------------------------------------------------------
mega_hugeqf:
    cmp byte [mega_on], 0
    je .r
    push bx
    push cx
    push dx
    mov cx, ax                      ; CX = the heap's
    call MEGA_SEL_CODE:mega_ownerf  ; may it have the pool at all?
    jc .o
    call MEGA_SEL_CODE:mx_maxrunf   ; AX = the pool's
    mov dx, ax
    cmp ax, cx
    jae .b
    mov ax, cx                      ; AX = the better of the two alone
.b:
    cmp dx, 64
    jb .d
    cmp cx, MEGA_RESKB
    jbe .d
    and dl, 0xC0                    ; whole windows, then the heap above its
    sub cx, MEGA_RESKB              ; reserve
    add dx, cx
    jnc .b2
    mov dx, 0xFFFF
.b2:
    cmp dx, ax
    jbe .d
    mov ax, dx
.d:
    pop dx
    pop cx
    pop bx
.r:
    retf
.o:
    mov ax, cx
    jmp short .d

; -----------------------------------------------------------------------------
; mega_hugebf - FAR: OSAPI_MEM_HUGE's BRIDGE (KERN-MEGA-MEMORY-PLAN MC5, D7),
; tried when neither the pool nor the heap alone could take the claim.
; in:  AX = KB, BX = the ask (a 32KB or 16KB step is never bridged), ES = the
;      caller, DS = KDATA
; out: CF = 0, DX = the first of ONE run of data selectors, a 64KB window
;      each, CX = 8: the first windows over a POOL block of whole 64KB, the
;      rest over a CONVENTIONAL claim of the remainder (whose own pair it
;      keeps), the heap left at least MEGA_RESKB; CF = 1 nothing claimed.
;
; Each piece is the record it would be alone, so it MOVES alone: a pool
; block's descriptors follow it (mx_cpmove), a claim's follow it
; (mega_mpmovef), and each piece's windows are freed with it (mx_unsel for
; the pool's, mega_cdfree for the claim's). The pool record says MX_FBRIDGE
; and names the claim in MX_AUX, so OSAPI_MEM_FREE of DX frees both; the
; instance's teardown frees each as its own. Not regrown (CF = 1)
; -----------------------------------------------------------------------------
mega_hugebf:
    cmp byte [mega_on], 0
    je .no0
    cmp bx, 0x4000
    je .no0
    cmp bx, 0x8000
    je .no0
    push ax
    push bx
    push si
    push di
    push bp
    push es
    mov bp, ax                      ; BP = KB wanted
    call MEGA_SEL_CODE:mega_ownerf  ; BL = the pool owner
    jc .no
    mov [mega_bfown], bl
    call MEGA_SEL_CODE:mx_maxrunf   ; AX = the pool's best run
    and al, 0xC0                    ; ...in whole 64KB windows
    mov si, bp
    dec si
    and si, 0xFFC0                  ; (less than the whole claim: the pool
    cmp ax, si                      ; alone has been asked and could not)
    jbe .p
    mov ax, si
.p:
    or ax, ax
    jz .no
    mov [mega_bfp], ax              ; P = the pool's piece
    sub bp, ax                      ; BP = C, the heap's
    push bx
    call COLD_SEG:mmf_osapi_mem_avail ; AX = the heap's largest claim
    pop bx
    sub ax, MEGA_RESKB
    jb .no
    cmp ax, bp
    jb .no                          ; ...less the reserve: short
    mov ax, bp                      ; --- the CONVENTIONAL piece, first: it
    xor cx, cx                      ; is the one a compaction may take time
    call COLD_SEG:mmf_osapi_mem_claim_dma ; over, and nothing is held yet
    jc .no
    mov [mega_bfc], dx              ; (its pair's data selector)
    inc byte [sch_lock]             ; --- the POOL piece, and the run, one step
    mov ax, [mega_bfp]
    mov dx, ax
    shl ax, 10
    shr dx, 6
    mov bl, [mega_bfown]
    call MEGA_SEL_CODE:mx_allocf    ; DX:AX = its base
    jc .noc
    mov [mega_bfbase], ax
    mov [mega_bfbase + 2], dx
    mov cx, [mega_bfp]
    call mega_hgcnt
    mov si, cx                      ; SI = the pool's windows
    mov cx, bp
    call mega_hgcnt                 ; CX = the heap's
    add cx, si
    mov di, MEGA_SEL_LDTD
    mov es, di
    pushf
    cli
    call mega_hgfree                ; DI = a run of CX free entries
    jc .full
    push di
    mov cx, si
    mov bx, [mega_bfp]
    mov ax, [mega_bfbase]
    mov dx, [mega_bfbase + 2]
    call mega_hgfill                ; the pool's windows...
    shl si, 3
    add di, si
    push di
    mov ax, [mega_bfc]
    call MEGA_SEL_CODE:mega_selbasef ; DX:AX = the heap piece's base
    pop di
    mov cx, bp
    call mega_hgcnt
    mov bx, bp
    call mega_hgfill                ; ...and the heap's, after them
    pop di
    popf
    lea bp, [di+4]                  ; BP = the run's first selector
    mov ax, [mega_bfbase]
    mov dx, [mega_bfbase + 2]
    mov bl, [mega_bfown]            ; (mega_mxat matches on the owner too)
    call mega_mxat                  ; DI = the pool block's entry
    mov [ss:di+MX_SEL], bp
    or byte [ss:di+MX_FL], MX_FHUGE | MX_FBRIDGE
    mov ax, [mega_bfc]
    mov [ss:di+MX_AUX], ax
    dec byte [sch_lock]
    mov dx, bp
    mov cx, 8
    clc
    jmp short .out
.full:
    popf
    mov ax, [mega_bfbase]
    mov dx, [mega_bfbase + 2]
    call MEGA_SEL_CODE:mx_freef
.noc:
    dec byte [sch_lock]
    mov dx, [mega_bfc]              ; the heap's piece back
    mov bp, sp
    mov es, [ss:bp]                 ; (the caller's ES, for its ownership)
    call COLD_SEG:mmf_osapi_mem_free
.no:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop bx
    pop ax
    retf
.no0:
    stc
    retf

; mega_hgcnt - CX = KB -> CX = a run's selectors, one per 64KB
mega_hgcnt:
    add cx, 63
    rcr cx, 1
    shr cx, 5
    ret

; mega_hugec - OSAPI_MEM_HUGE's CONVENTIONAL arm in protected mode (KERN-MEGA-
; MEMORY-PLAN MC7.4): DX = the claim osapi_mem_claim_dma_x has just made (its
; pair's data selector), AX = its KB, BX = the ask (8000h a window every 32KB,
; 4000h every 16KB, else every 64KB) -> CF = 0, DX = the first of a RUN of
; data selectors over it, a 64KB window each (the last its end), CX = 8, BX =
; 0 when a 32KB or 16KB step was asked and given; CF = 1 no run of LDT entries
; was free (the caller gives the claim back). The pair is remade as the entry
; BEFORE the run, so the run's first selector IS the claim's value - its code
; half at - 8, as for every claim - and every window is based in the block:
; mega_mpmovef rebases them with a move and mega_mpfreef frees them with it.
; Not regrown past its first 64KB. Clobbers nothing else
mega_hugec:
    push ax
    push bx
    push si
    push di
    push bp
    push es
    mov si, bx                      ; SI = the ask
    mov bp, ax                      ; BP = KB
    mov bx, mem_sel
.f:
    cmp [ss:bx], dx
    je .got
    inc bx
    inc bx
    cmp bx, mem_sel + MEGA_MSELN * 2
    jb .f
    jmp .no
.got:
    push bx                         ; [the claim's mem_sel slot]
    mov cx, bp
    call mega_hgcntx                ; CX = the windows...
    inc cx                          ; ...and the code half before them
    push dx                         ; [the old data selector]
    mov di, dx
    and di, 0xFFF8
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    mov ax, [es:di+2]
    mov dl, [es:di+4]
    xor dh, dh                      ; DX:AX = the claim's base
    pushf
    cli                             ; the scan and the claim are one step
    call mega_hgfree                ; DI = a run of CX free entries
    jc .full
    push bx
    mov bx, 0xFFFF                  ; the code half: the first 64KB...
    cmp bp, 64
    jae .cl
    mov bx, bp                      ; ...or the claim's end
    shl bx, 10
    dec bx
.cl:
    mov [es:di], bx
    pop bx
    mov [es:di+2], ax
    mov [es:di+4], dl
    mov byte [es:di+5], 0x9A
    mov word [es:di+6], 0
    add di, 8
    dec cx
    push bx
    mov bx, bp
    call mega_hgfillx               ; the windows, at SI's step
    pop bx
    popf
    pop ax                          ; the old pair goes back
    push ax
    sub ax, 8
    call MEGA_SEL_CODE:mega_ldt_freef
    pop ax
    call MEGA_SEL_CODE:mega_ldt_freef
    pop bx                          ; the slot...
    lea dx, [di+4]                  ; ...holds the run's first selector
    mov [ss:bx], dx
    mov cx, 8
    cmp si, 0x8000
    je .met
    cmp si, 0x4000
    jne .ok
.met:
    mov bp, sp
    mov word [ss:bp+8], 0           ; the caller's BX: 0, the ask was met
.ok:
    clc
    jmp short .o
.full:
    popf
    pop dx
    pop bx
.no:
    stc
.o:
    pop es
    pop bp
    pop di
    pop si
    pop bx
    pop ax
    ret

; mega_hgcntx - CX = KB, SI = the ask (8000h a window every 32KB, 4000h
; every 16KB, else 64KB) -> CX = the run's selectors. Clobbers nothing else
mega_hgcntx:
    cmp si, 0x8000
    je .c32
    cmp si, 0x4000
    jne mega_hgcnt
    add cx, 15                      ; one per 16KB
    rcr cx, 1
    shr cx, 3
    ret
.c32:
    add cx, 31                      ; ...or one per 32KB
    rcr cx, 1
    shr cx, 4
    ret

; mega_hgfillx - mega_hgfill at SI's step (mega_hgcntx's). Clobbers nothing
mega_hgfillx:
    cmp si, 0x8000
    je mega_hgfill32
    cmp si, 0x4000
    je mega_hgfill16
    jmp mega_hgfill

; mega_hgchk - ES = MEGA_SEL_LDTD, DI = an entry's offset, CX = a count ->
; CF = 0 the CX entries from DI are all free (DI and CX kept); CF = 1 one is
; taken or the LDT ends first, and DI = that entry. Clobbers nothing else
mega_hgchk:
    push cx
    push di
.l:
    cmp di, [ss:mega_ldte]
    jae .no
    cmp byte [es:di+5], 0
    jne .no
    add di, 8
    loop .l
    pop di
    pop cx
    clc
    ret
.no:
    add sp, 2
    pop cx
    stc
    ret

; mega_hgfree - ES = MEGA_SEL_LDTD, CX = a count -> CF = 0 and DI = the
; first of CX consecutive free entries; CF = 1 there is no such run. The
; caller holds interrupts off from here to its fill. Clobbers nothing else
mega_hgfree:
    mov di, MEGA_LDT_FREE0 * 8
.s:
    call mega_hgchk
    jnc .r
    add di, 8                       ; past the taken one
    cmp di, [ss:mega_ldte]
    jb .s
    stc
.r:
    ret

; mega_hgfill - ES:DI = a run's first entry, CX = its length, DX:AX = the
; block's base, BX = its KB: the run's descriptors, present data, a 64KB
; window each and the last ending at the claim's end. SI = 8000h: a window
; every 32KB from mega_hgfill32 instead, each still 64KB long (OSAPI_XMEM_RUN's
; shape, KERN-MEGA-PLAN 14.41). Clobbers nothing
mega_hgfill16:
    pusha
    mov bp, 2
    jmp short mega_hgfill.go
mega_hgfill32:
    pusha
    mov bp, 1
    jmp short mega_hgfill.go
mega_hgfill:
    pusha
    xor bp, bp
.go:
.fill:
    mov word [es:di], 0xFFFF        ; a whole 64KB...
    cmp bx, 64
    jae .lim
    push bx                         ; ...or the claim's end
    shl bx, 10
    dec bx
    mov [es:di], bx
    pop bx
.lim:
    mov [es:di+2], ax
    mov [es:di+4], dl
    mov byte [es:di+5], 0x92        ; data, present: taken
    mov word [es:di+6], 0
    or bp, bp
    jnz .h32
    inc dl                          ; the next 64KB
    sub bx, 64
    jmp short .nx
.h32:
    cmp bp, 1
    jne .h16
    add ax, 0x8000                  ; ...or the next 32KB
    adc dl, 0
    sub bx, 32
    jmp short .nx
.h16:
    add ax, 0x4000                  ; ...or the next 16KB
    adc dl, 0
    sub bx, 16
.nx:
    add di, 8
    loop .fill
    popa
    ret

; mega_mxat - DX:AX = a block's linear base, BL = its owner -> DI = its
; mx_tab entry (the caller knows there is one). Clobbers nothing else
mega_mxat:
    push ax
    push dx
    sub dx, MEGA_XPOOL >> 16
    shr ax, 10
    shl dx, 6
    or ax, dx                       ; AX = the block's KB from the pool
    mov di, mx_tab
.f:
    cmp word [ss:di+MX_KB], 0
    je .fn
    cmp [ss:di+MX_OFF], ax
    jne .fn
    cmp [ss:di+MX_OWN], bl
    je .fs
.fn:
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jb .f
.fs:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mega_xmrunf - FAR: OSAPI_XMEM_RUN's body in protected mode (KERN-MEGA-PLAN
;               14.16): a RUN of data selectors over an OSAPI_XMEM_ALLOC block,
;               one every 32KB and each a 64KB window, so ANY 32KB of the block
;               lies wholly inside one of them - the one at (offset >> 15),
;               at (offset & 7FFFh). What a reader of records of 32KB or less
;               needs to read them IN PLACE, where OSAPI_MEM_HUGE's run (a
;               window every 64KB) cuts a record that straddles 64KB in two
; in:  DX:AX = the block's token (its linear base), BL = the caller, DS = KDATA
; out: CF = 0, DX = the first selector, CX = 8 (the step); CF = 1, AX = 2 not
;      the base of a block the caller owns, or the block has a selector
;      already (OSAPI_XMEM_SEL's or a run), AX = 1 no run of free LDT entries.
;      Every other register kept
; The run is the block's MX_SEL with bit 0 set (an RPL nobody hands out):
; mx_unsel frees (KB + 31) / 32 entries for it, and mega_xmsel refuses to
; re-aim it. The block MOVES (KERN-MEGA-MEMORY-PLAN MC3): DX:AX is its token,
; the run is made where the block is, and the compactor rebases every
; descriptor of it with the block, cut every 64KB with the package's rate
; hook held off (mx_cpflags) since every one of its windows crosses a cut
; -----------------------------------------------------------------------------
mega_xmrunf:
    push bx
    push si
    push di
    push bp
    push es
    cmp byte [mega_on], 0           ; (real mode: none, AX = 1)
    jne .on
    mov ax, 1
    jmp .fail
.on:
    test ax, 0x03FF                 ; a block's base is a whole KB
    jnz .not
    or dh, dh
    jnz .not                        ; above 16MB: no pool there
    mov si, ax                      ; DX:SI = the base, kept
    push dx
    sub dx, MEGA_XPOOL >> 16
    jc .notp                        ; below the pool
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; AX = its KB from the pool's base
    pop dx
    inc byte [sch_lock]             ; the scan to the MX_SEL store: the
    mov di, mx_tab                  ; block stays where MX_OFF says (14.37)
.f:
    cmp word [ss:di+MX_KB], 0
    je .fn
    cmp [ss:di+MX_OWN], bl
    jne .fn
    cmp [ss:di+MX_AUX], ax          ; its TOKEN (MC3): OSAPI_XMEM_RUN is the
    je .fs                          ; cell's alone, so the owner is one
.fn:
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jb .f
    jmp .notl
.fs:
    cmp word [ss:di+MX_SEL], 0
    je .fs1
    jmp .notl                       ; (a selector already: one view a block)
.fs1:
    mov si, [ss:di+MX_OFF]          ; DX:SI = where the block is now
    mov dx, si
    shl si, 10
    shr dx, 6
    add dx, MEGA_XPOOL >> 16
    mov bp, [ss:di+MX_KB]              ; BP = KB
    mov cx, bp
    add cx, 31
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; CX = the selectors, one per 32KB
    push di
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    mov di, MEGA_LDT_FREE0 * 8
    pushf
    cli                             ; the scan and the claim are one step
.scan:
    push di
    push cx
.fr:
    cmp di, [ss:mega_ldte]
    jae .full
    cmp byte [es:di+5], 0
    jne .taken
    add di, 8
    loop .fr
    pop cx                          ; a run of CX free entries from the DI
    pop di                          ; under it
    push di
    push cx
    mov ax, si                      ; DX:AX = window 0's base
    mov bx, bp                      ; BX = KB from it to the block's end
.fill:
    mov word [es:di], 0xFFFF        ; a whole 64KB...
    cmp bx, 64
    jae .lim
    push bx                         ; ...or to the block's end
    mov bh, bl
    xor bl, bl
    shl bx, 1
    shl bx, 1                       ; (KB x 1024: under 64KB here)
    dec bx
    mov [es:di], bx
    pop bx
.lim:
    mov [es:di+2], ax
    mov [es:di+4], dl
    mov byte [es:di+5], 0x92        ; data, present: taken
    mov word [es:di+6], 0
    add ax, 0x8000                  ; the next 32KB
    adc dl, 0
    sub bx, 32
    add di, 8
    loop .fill
    pop cx
    pop di
    popf
    lea dx, [di+4]                  ; DX = the first selector (TI = 1)
    pop di
    mov ax, dx
    or al, 1                        ; MX_SEL: the run, marked
    mov [ss:di+MX_SEL], ax
    dec byte [sch_lock]
    mov cx, 8
    clc
    jmp short .out
.taken:
    pop cx
    pop di
    add di, 8                       ; the run starts one entry on
    jmp .scan
.full:
    pop cx
    pop di
    popf
    pop di
    dec byte [sch_lock]
    mov ax, 1                       ; no run of free entries
    jmp short .fail
.notl:
    dec byte [sch_lock]
    jmp short .not
.notp:
    pop dx
.not:
    mov ax, 2
.fail:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop bx
    retf
; mx_runcntf - FAR, for mx_unsel: AX = an OSAPI_XMEM_RUN's MX_SEL (bit 0
; set), SI = its entry -> AX = the first selector, CX = the run's length,
; one per 32KB of the block
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mx_runcntf:
    dec ax
    mov cx, [ss:si+MX_KB]
    add cx, 31
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    retf
section .text

; =============================================================================
; THE FLOPPY, NATIVELY (KERN-MEGA-PLAN 10, M2): int 13h AH=02h/03h on drive
; 0 or 1, run by the 765 and the 8237 in protected mode instead of reflected.
; A reflected transfer is ~200 ms of real mode with every IRQ but the BIOS's
; own masked (mega_svc), so the pointer and the sound stopped for each one;
; this one waits for IRQ6 with interrupts ON, so they do not.
;
; IT IS AN ACCELERATOR, NOT A SECOND DRIVER. It shares the BIOS's own state in
; the BDA - the rate last programmed at 40:8B, the recalibrated bits at
; 40:3E, the motor at 40:3F/40:40 - and it hands back to the BIOS everything
; it does not recognise: a call that is not dsk_xfer's, a geometry it does not
; know, the change line set, a
; buffer that crosses a DMA page, any other function, and ANY failure but a
; write-protected disk. The BIOS then does what it always did, so the native
; path can only ever be faster or equal. Eight fallbacks in a row switch it
; off for the session ([mega_fdoff]) and say so on port E9.
; =============================================================================
MEGA_FD_DOR equ 0x3F2               ; digital output: motors, DMA, drive
MEGA_FD_MSR equ 0x3F4               ; main status: RQM, DIO
MEGA_FD_DAT equ 0x3F5
MEGA_FD_CCR equ 0x3F7               ; write: data rate. read: DIR, bit 7

; mega_fdisr - IRQ6, in protected mode: the BIOS's own flag (40:3E bit 7),
; which is what both this path and a reflected BIOS call wait on
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_fdisr:
    push ax
    push ds
    mov ax, MEGA_BDA
    mov ds, ax
    or byte [0x3E], 0x80
    mov al, 0x20
    out 0x20, al
    pop ds
    pop ax
    iret

; mega_fd13 - in: BP = mega_svc's frame, DS = KDATA, IF = 0.
; out: CF = 0 handled - the frame holds the answer (AH status, AL sectors, CF
; in FLAGS); CF = 1 not taken, reflect it. Preserves DS; clobbers the rest
mega_fd13:
    cmp byte [mega_fdoff], 0
    jne .no
    mov ax, [ss:bp+18]              ; AH = the function, AL = the count
    cmp ah, 2
    je .fn
    cmp ah, 3
    jne .no
.fn:
    mov [mega_fdop], ah
    or al, al
    jz .no
    cmp al, 72                      ; a cylinder of the largest geometry; the
    ja .no                          ; DMA count below must fit 16 bits
    mov [mega_fdn], al
    mov dx, [ss:bp+14]              ; DL = drive, DH = head
    cmp dl, 1
    ja .no
    cmp dh, 1
    ja .no
    mov [mega_fdd], dl
    mov [mega_fdh], dh
    mov cx, [ss:bp+16]              ; CH = cylinder, CL = sector (a floppy's
    test cl, 0xC0                   ; cylinder has no bits 9:8)
    jnz .no
    mov [mega_fdc], ch
    mov [mega_fds], cl
    cmp byte [mega_fdx], 0          ; only dsk_xfer's own transfers: the EOT
    je .no                          ; in dsk_dpt is ITS volume's, and a run
                                    ; across a head with a wrong one reads the
                                    ; wrong sectors without failing
    ; --- the media, from the volume and the drive - NOT 40:90, whose
    ; encoding is the BIOS's own (Bochs's says 25h for an established 1.2MB
    ; disk). Nine sectors in a 1.2MB drive is a 360KB disk: 300 kbps and
    ; every cylinder stepped twice; nine anywhere else is 250; 15 and 18 are
    ; 500. A guess that is wrong fails the transfer, and that falls back
    mov al, [dsk_dpt+4]
    mov ah, 0x10                    ; 500 kbps
    cmp al, 18
    je .rate
    cmp al, 15
    je .rate
    cmp al, 9
    jne .no
    call mega_fdtype                ; AL = the drive's CMOS type
    mov ah, 0x90                    ; 250 kbps
    cmp al, 2
    jne .t250
    mov ah, 0x70                    ; 300 kbps, double-stepped
    jmp short .rate
.t250:
    cmp al, 1
    jb .no
    cmp al, 4
    ja .no
.rate:
    mov [mega_fdms], ah
    ; --- the buffer, as a 24-bit address that must not cross a 64KB page --
    mov ax, [ss:bp+0]               ; ES
    call mega_selbase               ; DX:AX = its base
    jc .no
    add ax, [ss:bp+12]              ; + BX
    adc dx, 0
    or dh, dh
    jnz .no                         ; above 16MB: the 8237 cannot reach it
    mov [mega_fdlin], ax
    mov [mega_fdlin+2], dl
    xor ch, ch
    mov cl, [mega_fdn]
    xchg ch, cl                     ; CX = sectors x 256...
    shl cx, 1                       ; ...x 2 = bytes (<= 36864)
    dec cx                          ; the 8237 counts N - 1
    mov [mega_fdcnt], cx
    add ax, cx
    jc .no                          ; it would cross: the BIOS says 09h
    ; --- taken: from here every exit restores the motor count -------------
    mov ax, MEGA_BDA                ; ES = the BIOS's data for everything below
    mov es, ax                      ; (the caller's ES is its BUFFER)
    inc byte [sch_lock]             ; nothing switches under the 765
    mov byte [mega_fdbusy], 1
    sti
    call mega_fdgo                  ; CF = 1 fall back; else AH = status
    cli
    mov byte [mega_fdbusy], 0
    pushf
    push ax
    mov ax, MEGA_BDA
    mov es, ax
    mov al, [dsk_dpt+2]
    mov [es:0x40], al               ; the motor-off count, ROM's own value
    and byte [es:0x3F], 0x7F        ; ...and no write in progress
    pop ax
    popf
    dec byte [sch_lock]
    jc .fall
    mov [es:0x41], ah               ; the BIOS's last status
    mov byte [mega_fdfail], 0
    push cx                         ; the first one per drive, on port E9:
    mov cl, [mega_fdd]              ; a field run can see the path was taken
    mov ch, 1
    shl ch, cl
    test [mega_fdsaid], ch
    jnz .said
    or [mega_fdsaid], ch
    push si
    push ax
    mov si, mega_mfdon
    call mm_puts
    mov al, 'A'
    add al, cl
    call mm_putc
    mov al, 10
    call mm_putc
    pop ax
    pop si
.said:
    pop cx
    mov al, [mega_fdn]
    or ah, ah
    jz .ok
    xor al, al                      ; write protected: nothing moved
.ok:
    mov [ss:bp+18], ax
    and byte [ss:bp+26], 0xFE       ; CF in the caller's FLAGS
    or ah, ah
    jz .done
    or byte [ss:bp+26], 1
.done:
    clc
    ret
.fall:
    push si
    mov si, mega_mfdfb
    call mm_puts
    mov al, [mega_fdstep]           ; ...the step it got to, and the 765's
    call mm_hex2                  ; ST0 ST1 ST2
    mov al, ' '
    call mm_putc
    mov ax, [mega_fdres]
    call mm_hex4
    mov al, [mega_fdres+2]
    call mm_hex2
    mov al, ' '
    call mm_putc
    mov al, [mega_fdd]              ; the drive, the media byte, the CHS
    call mm_hex2
    mov al, [mega_fdms]
    call mm_hex2
    mov al, ' '
    call mm_putc
    mov al, [mega_fdc]
    call mm_hex2
    mov al, [mega_fdh]
    call mm_hex2
    mov al, [mega_fds]
    call mm_hex2
    mov al, [mega_fdn]
    call mm_hex2
    mov al, 10
    call mm_putc
    pop si
    mov byte [mega_fdpend], 1       ; ...and whether it COUNTS is the BIOS's
                                    ; to say: mega_fdjudge, on its way back
.no:
    stc
    ret

; mega_fdjudge - a native fallback, judged by what the BIOS then made of the
; same call (mega_svc_back, BP = its frame, the BIOS's FLAGS at +20). Only a
; call the BIOS COMPLETED is a strike against the native path: one the BIOS
; failed too is the DISK's - an empty drive, a disk that is not there or not
; readable - and an empty A: probed eight times switched a working native
; path off for the session (KERN-MEGA-PLAN 14.34). DS = KDATA. Preserves
; every register
mega_fdjudge:
    mov byte [mega_fdpend], 0
    test byte [ss:bp+20], 1         ; the BIOS's CF: it failed as well
    jz .strike
    ret
.strike:
    push ax
%ifdef MEGA_DIAG                    ; (MEGADIAG=1's toast, KERN-MEGA-PLAN 14.65)
    cmp byte [mega_fdtold], 0       ; the FIRST fallback of a boot on the
    jne .told                       ; glass, which is all a field machine
    mov byte [mega_fdtold], 1       ; shows: one that alternates with
    push ax                         ; successes never reaches the eight
    push cx                         ; below, and the BIOS doing every write
    push di                         ; again is heard and not seen.
    push si                         ; "FD BIOS ss s0s1s2 Dmm": the step, ST0-2,
    push es                         ; the drive and the media byte
    mov di, mega_tfdfb + 8
    mov al, [mega_fdstep]
    call mega_hexb
    inc di
    mov al, [mega_fdres]
    call mega_hexb
    mov al, [mega_fdres+1]
    call mega_hexb
    mov al, [mega_fdres+2]
    call mega_hexb
    inc di
    mov al, 'A'
    add al, [mega_fdd]
    mov [di], al
    inc di
    mov al, [mega_fdms]
    call mega_hexb
    push ds
    pop es
    mov si, mega_tfdfb
    mov cx, 182                     ; ten seconds
    call KERNEL_SEG:cw_toast_show
    pop es
    pop si
    pop di
    pop cx
    pop ax
.told:
%endif
    inc byte [mega_fdfail]
    cmp byte [mega_fdfail], 8
    jb .out
    mov byte [mega_fdoff], 1
    push si
    push cx
    push es
    mov si, mega_mfdoff
    call mm_say
%ifdef MEGA_DIAG
    push ds                         ; ...and on the GLASS, which is all a
    pop es                          ; field machine shows: toast_show only
    mov si, mega_tfdoff             ; stages it, the UI task draws it
    mov cx, 146
    call KERNEL_SEG:cw_toast_show
%endif
    pop es
    pop cx
    pop si
.out:
    pop ax
    ret

; mega_fdgo - the transfer itself, IF = 1 and the switch lock held. ES = BDA.
; out: CF = 1 fall back to the BIOS; CF = 0 and AH = 0 done, or 03h write
; protected. Clobbers AX BX CX DX SI DI
mega_fdgo:
    mov cl, 1
    call mm_fdrprobe              ; reset by a reflected service? recalibrate
    mov cl, [mega_fdd]
    mov al, 0x10
    shl al, cl                      ; this drive's motor bit
    mov byte [es:0x40], 0xFF        ; no motor-off while we work
    in al, 0x21
    and al, 0xBF                    ; IRQ6 open: the BIOS wants it so too
    out 0x21, al
    mov ah, [es:0x3F]
    mov dl, 1
    shl dl, cl                      ; this drive's 3F bit
    test ah, dl
    pushf                           ; ZF = 1: the motor was OFF
    and ah, 0xCF                    ; 3F: bits 4-5 = the drive selected
    or ah, dl
    push cx
    mov ch, cl
    mov cl, 4
    shl ch, cl
    or ah, ch
    pop cx
    cmp byte [mega_fdop], 3
    jne .nw
    or ah, 0x80                     ; bit 7: a write in progress
.nw:
    mov [es:0x3F], ah
    mov al, ah
    and al, 0x0F                    ; the motors as DOR bits 4-7
    mov cl, 4
    shl al, cl
    or al, 0x0C                     ; not reset, DMA on
    or al, [mega_fdd]               ; ...this drive selected
    mov dx, MEGA_FD_DOR
    out dx, al
    popf
    jnz .spun
    mov al, [dsk_dpt+10]            ; motor start, 1/8 s
    xor ah, ah
    mov cl, 9
    mul cl
    add ax, 3
    shr ax, 1
    shr ax, 1                       ; ...in ticks: 125 / 55 ~ 9 / 4
    call mega_fdtk
.spun:
    mov byte [mega_fdstep], 1
    mov dx, MEGA_FD_CCR
    in al, dx
    mov [mega_fddir], al            ; bit 7: the change line, a disk put in or
                                    ; taken out since the last step pulse -
                                    ; cleared by the recalibrate below, which
                                    ; is forced for it (the kernel gives 06h
                                    ; no meaning of its own: it retries, and
                                    ; a swapped disk is found by the volume)
    mov al, [mega_fdms]
    and al, 0xC0                    ; the rate the BIOS established...
    and byte [es:0x8B], 0x3F
    or [es:0x8B], al                ; ...as the one last programmed (so a
    rol al, 1                       ; reflected call does not skip it)
    rol al, 1
    out dx, al
    mov byte [mega_fdstep], 2
    mov al, 0x03                    ; SPECIFY, from the parameter table
    call mm_fdout
    jc .fall
    mov al, [dsk_dpt+0]
    call mm_fdout
    jc .fall
    mov al, [dsk_dpt+1]
    and al, 0xFE                    ; DMA, not PIO
    call mm_fdout
    jc .fall
    mov byte [mega_fdstep], 3
    ; --- recalibrate if the BIOS's reset left this drive needing it ---------
    mov cl, [mega_fdd]
    mov dl, 1
    shl dl, cl
    test byte [mega_fddir], 0x80
    jnz .uncal                      ; the change line forces it
    test [es:0x3E], dl
    jnz .cal
.uncal:
    not dl
    and [es:0x3E], dl               ; the BIOS's view too: not calibrated
    mov si, 2                       ; twice: a 765 steps 77 tracks at most
.recal:
    and byte [es:0x3E], 0x7F
    mov al, 0x07
    call mm_fdout
    jc .fall
    mov al, [mega_fdd]
    call mm_fdout
    jc .fall
    mov byte [mega_fdstep], 0x31
    call mega_fdirq
    jc .fall
    mov byte [mega_fdstep], 0x32
    call mm_fdsense
    mov [mega_fdres], ax
    jc .fall
    mov byte [mega_fdstep], 0x33
    test al, 0x10                   ; ST0 EC: track 0 not reached yet
    jz .calok
    dec si
    jnz .recal
    jmp .fall
.calok:
    test byte [mega_fddir], 0x80    ; was it the change line? A recalibrate
    jz .calset                      ; at track 0 pulses nothing, so a seek to
    push bx                         ; cylinder 1 clears it; still set after
    mov bl, 1                       ; that is NO DISK - the BIOS's to report
    call mega_fdseek
    pop bx
    jc .fall
    mov dx, MEGA_FD_CCR
    in al, dx
    test al, 0x80
    jnz .fall
.calset:
    mov cl, [mega_fdd]
    mov dl, 1
    shl dl, cl
    or [es:0x3E], dl
.cal:
    mov byte [mega_fdstep], 4
    ; --- seek, every call: the 765 knows where it is, and a seek to where
    ; it already is is one IRQ. The cylinder is PHYSICAL - doubled for a
    ; 360KB disk in a 1.2MB drive (40:90 bit 5)
    mov bl, [mega_fdc]
    test byte [mega_fdms], 0x20
    jz .ss
    shl bl, 1
.ss:
    call mega_fdseek
    jc .fall
    xor bh, bh                      ; ...and the BIOS's cylinder byte made
    mov bl, [mega_fdd]              ; unknown, so its next call seeks rather
    mov byte [es:0x94+bx], 0xFF     ; than trusting a number of ours
    mov cl, [dsk_dpt+9]             ; head settle, ms
    call mega_fdms_wait
    ; --- the 8237, channel 2 ---------------------------------------------
    cli
    mov al, 0x06
    out 0x0A, al                    ; channel 2 masked
    out 0x0C, al                    ; the byte flip-flop cleared
    mov al, 0x46                    ; single, increment, WRITE to memory
    cmp byte [mega_fdop], 3
    jne .dm
    mov al, 0x4A                    ; ...or READ from it
.dm:
    out 0x0B, al
    mov ax, [mega_fdlin]
    out 0x04, al
    mov al, ah
    out 0x04, al
    mov al, [mega_fdlin+2]
    out 0x81, al                    ; the page, bits 16-23
    mov ax, [mega_fdcnt]
    out 0x05, al
    mov al, ah
    out 0x05, al
    mov al, 0x02
    out 0x0A, al                    ; channel 2 open
    sti
    mov byte [mega_fdstep], 6
    ; --- READ / WRITE DATA: MT MFM SK, as the BIOS's own ------------------
    and byte [es:0x3E], 0x7F
    mov al, 0xE6
    cmp byte [mega_fdop], 3
    jne .cm
    mov al, 0xC5
.cm:
    call mm_fdout
    jc .fall
    mov al, [mega_fdh]
    shl al, 1
    shl al, 1
    or al, [mega_fdd]
    call mm_fdout
    jc .fall
    mov al, [mega_fdc]
    call mm_fdout
    jc .fall
    mov al, [mega_fdh]
    call mm_fdout
    jc .fall
    mov al, [mega_fds]
    call mm_fdout
    jc .fall
    mov al, 2                       ; 512 bytes
    call mm_fdout
    jc .fall
    mov al, [dsk_dpt+4]             ; EOT: dsk_xfer wrote the volume's spt
    call mm_fdout
    jc .fall
    mov al, [dsk_dpt+5]
    call mm_fdout
    jc .fall
    mov al, [dsk_dpt+6]
    call mm_fdout
    jc .fall
    call mega_fdirq
    jc .fall
    mov byte [mega_fdstep], 7
    mov di, mega_fdres              ; the seven result bytes
    mov cx, 7
.res:
    call mm_fdin
    jc .fall
    mov [di], al
    inc di
    loop .res
    mov byte [mega_fdstep], 8
    mov al, [mega_fdres]            ; ST0
    test al, 0xC0
    jz .good
    test byte [mega_fdres+1], 0x02  ; ST1 NW: write protected - a fact, not
    jz .fall                        ; a failure to fall back on
    mov ah, 0x03
    clc
    ret
.good:
    xor ah, ah
    clc
    ret
.fall:
    stc
    ret

; mega_fdseek - SEEK drive [mega_fdd], head [mega_fdh], to PHYSICAL cylinder
; BL, and check it got there. CF = 1 it did not. Clobbers AX
mega_fdseek:
    and byte [es:0x3E], 0x7F
    mov al, 0x0F
    call mm_fdout
    jc .x
    mov al, [mega_fdh]
    shl al, 1
    shl al, 1
    or al, [mega_fdd]
    call mm_fdout
    jc .x
    mov al, bl
    call mm_fdout
    jc .x
    call mega_fdirq
    jc .x
    mov byte [mega_fdstep], 5
    call mm_fdsense               ; AL = ST0, AH = PCN
    jc .x
    test al, 0xC0
    jnz .no
    cmp ah, bl
    je .x                           ; (ZF: CF is clear from the cmp)
.no:
    stc
.x:
    ret

; THE 765's BYTE LEVEL and the reset probe, twice from one body: mega_r286
; probes in REAL mode, so `.text` keeps a copy (mega_*), and the native path
; in `.mega` calls its own (mm_*) rather than far-calling per command byte
; (KERN-MEGA-SEGMENTS-PLAN 3.6)
%macro MEGA_FDIO 1
; one byte to the 765 / from it, as soon as it asks. CF = 1 it never did
%1 %+ fdout:
    push cx
    push dx
    push ax
    mov dx, MEGA_FD_MSR
    xor cx, cx
.w:
    in al, dx
    and al, 0xC0
    cmp al, 0x80                    ; RQM, and DIO = to the controller
    je .go
    loop .w
    pop ax
    pop dx
    pop cx
    stc
    ret
.go:
    pop ax
    inc dx
    out dx, al
    pop dx
    pop cx
    clc
    ret
%1 %+ fdin:
    push cx
    push dx
    mov dx, MEGA_FD_MSR
    xor cx, cx
.w:
    in al, dx
    and al, 0xC0
    cmp al, 0xC0                    ; RQM, and DIO = to the CPU
    je .go
    loop .w
    pop dx
    pop cx
    stc
    ret
.go:
    inc dx
    in al, dx
    pop dx
    pop cx
    clc
    ret

; SENSE INTERRUPT STATUS -> AL = ST0, AH = the present cylinder
%1 %+ fdsense:
    mov al, 0x08
    call %1 %+ fdout
    jc .x
    call %1 %+ fdin
    jc .x
    push ax
    call %1 %+ fdin
    mov ah, al
    pop dx
    mov al, dl
.x:
    ret

; -----------------------------------------------------------------------------
; %1 %+ fdrprobe - was the 765 RESET behind our back? A reset controller
; believes every head is at cylinder 0 wherever it really is, so the next
; seek to N steps N more and can run the carriage into its end stop - which
; the Victory 286 reported as a drive that stutters through a C: copy and
; rails out of line at the next power-on. A reset leaves an interrupt pending
; for each of the four drives until SENSE INTERRUPT STATUS is issued; an idle
; controller answers that with 80h, "invalid command". So one SENSE tells.
; Seen: the rest are sensed, every drive is marked uncalibrated in 40:3E (the
; BIOS's own record, which it then also obeys) and its motor off in 40:3F,
; and [mega_fdrst + CL] counts it for mega_tell (MEGADIAG=1, 14.65).
; in:  CL = 0 from mega_r286 (real mode), 1 from mega_fdgo; ES = the BDA
; out: CF = 1 it had been reset. Preserves everything else
; -----------------------------------------------------------------------------
%1 %+ fdrprobe:
    push ax
    push bx
    push cx
    push dx
    cmp byte [mega_fdbusy], 0       ; an IRQ reflected out of the middle of
    jne .no                         ; our own transfer: the 765 is ours
    mov dx, MEGA_FD_MSR
    in al, dx
    cmp al, 0x80                    ; idle, no drive seeking, waiting for a
    jne .no                         ; command - anything else is not ours
    mov al, 0x08
    call %1 %+ fdout
    jc .no
    call %1 %+ fdin
    jc .no
    cmp al, 0x80
    je .no                          ; nothing pending: not reset
    call %1 %+ fdin                  ; its cylinder byte
    mov bl, 3
.more:
    call %1 %+ fdsense               ; the other three drives' (a missing
    dec bl                          ; second byte times out, harmlessly)
    jnz .more
    and byte [es:0x3E], 0x70        ; no drive calibrated, no IRQ pending
    and byte [es:0x3F], 0xF0        ; no motor running
%ifdef MEGA_DIAG
    xor ch, ch                      ; ...and the menu bar's FDC RESET line
    mov bx, cx
    inc byte [mega_fdrst+bx]
    or byte [mega_tellq], 4
%endif
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

%endmacro
    MEGA_FDIO mm_
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
    MEGA_FDIO mega_
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
; wait for IRQ6 (40:3E bit 7), at most two seconds, halting between - CF = 1
; it never came. Clears the flag
mega_fdirq:
    push ax
    push bx
    mov bx, [ticks]
.l:
    test byte [es:0x3E], 0x80       ; SPUN, not halted: the Victory 286 took
    jnz .ok                         ; a PARITY NMI on this loop's hlt twice,
    mov ax, [ticks]                 ; both times during a native track WRITE
    sub ax, bx                      ; - the 8237 reading RAM under a halted
    cmp ax, 37                      ; CPU, which the BIOS's own spin never
    ja .to                          ; does - and ten 1MB copies through the
    jmp short .l                    ; BIOS raised none (KERN-MEGA-PLAN 10.3)
.ok:
    and byte [es:0x3E], 0x7F
    pop bx
    pop ax
    clc
    ret
.to:
    pop bx
    pop ax
    stc
    ret

; AX ticks of nothing, halting
mega_fdtk:
    push bx
    push cx
    mov cx, ax
    mov bx, [ticks]
.l:
    mov ax, [ticks]
    sub ax, bx
    cmp ax, cx
    jae .x
    hlt
    jmp short .l
.x:
    pop cx
    pop bx
    ret

; CL milliseconds: port 61h bit 4 is the AT's refresh request, which toggles
; every 15 us, so a millisecond is ~66 toggles. Each toggle is bounded, so a
; board that does not toggle it waits a bounded time rather than for ever
mega_fdms_wait:
    push ax
    push cx
    push dx
    xor ch, ch
    jcxz .x
.ms:
    mov dx, 66
.t:
    in al, 0x61
    and al, 0x10
    mov ah, al
    push cx
    mov cx, 200
.e:
    in al, 0x61
    and al, 0x10
    cmp al, ah
    loope .e
    pop cx
    dec dx
    jnz .t
    loop .ms
.x:
    pop dx
    pop cx
    pop ax
    ret

; mega_fdtype - AL = the CMOS diskette type of drive [mega_fdd] (1 360KB, 2
; 1.2MB, 3 720KB, 4 1.44MB), read once - index 10h, A in the high nibble
mega_fdtype:
    mov al, [mega_fdcmos]
    cmp al, 0xFF
    jne .have
    pushf
    cli
    mov al, 0x10                    ; NMI stays on: bit 7 clear, as mega_enter
    out 0x70, al                    ; left it
    in al, 0x71
    popf
    mov [mega_fdcmos], al
.have:
    cmp byte [mega_fdd], 0
    jne .b
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.b:
    and al, 0x0F
    ret

; mega_selbase - AX = a selector -> DX:AX = its 24-bit base, CF = 1 for the
; null one. Every descriptor is read: the LDT through LDTD (the GDT's own
; entries below the allocator's range since MC7's step 6), the GDT through WIN
mega_selbase:
    or ax, ax
    jz .no
    test al, 4
    jz .gdt
    and al, 0xF8
    push es
    push bx
    mov bx, ax
    mov ax, MEGA_SEL_LDTD
    jmp short .rd
.gdt:
    push es
    push bx
    and al, 0xF8
    mov bx, ax
    mov ax, MEGA_SEL_WIN
.rd:
    mov es, ax
    mov ax, [es:bx+2]
    xor dh, dh
    mov dl, [es:bx+4]
    pop bx
    pop es
    clc
    ret
.no:
    stc
    ret

section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_fdoff  db 0                    ; 1: switched off for the session
mega_fdfail db 0                    ; fallbacks in a row
mega_fdpend db 0                    ; 1: a fallback awaits the BIOS's verdict
mega_fdop   db 0                    ; 02h / 03h
mega_fdn    db 0                    ; sectors
mega_fdd    db 0                    ; drive
mega_fdh    db 0                    ; head
mega_fdc    db 0                    ; cylinder (logical)
mega_fds    db 0                    ; sector
mega_fdms   db 0                    ; 40:90's byte for the drive
mega_fdlin  dw 0, 0                 ; the buffer's 24-bit address
mega_fdcnt  dw 0                    ; bytes - 1
mega_fdres  times 7 db 0            ; ST0 ST1 ST2 C H R N
%ifdef MEGA_DIAG                    ; MEGADIAG=1's two toasts (14.65)
mega_tfdoff db "native floppy off: BIOS from now", 0
mega_tfdfb  db "FD BIOS ss s0s1s2 Dmm", 0
mega_fdtold db 0                    ; 1 = mega_tfdfb has been shown this boot
%endif
mega_fdstep db 0
mega_fdsaid db 0
mega_hdsaid db 0
mega_rmask  dw 0x3FB8               ; the OR masks the last service reflection
                                    ; set, master low and slave high
                                    ; (mega_rmaskf), for mega_r286 to compare
mega_vsw    dw 0, 0                 ; [vid_seg] and [k_vsa] as SELECTORS while
                                    ; a reflection holds them as segments
                                    ; (mega_vrm), 0 when none does
mega_hbseg  dw 0                   ; mega_hibnc's: the low buffer's segment,
mega_hboff  dw 0                    ; ...its offset, its sectors,
mega_hbcap  db 0
mega_hbleft db 0                    ; ...the caller's sectors left and done,
mega_hbdone db 0
mega_hbn    db 0                    ; ...this run's,
mega_hbsi   dw 0                    ; ...where in the caller's buffer it is,
mega_hbcx   dw 0                    ; ...and the CX the run is asked with
mega_hbdh   db 0                    ; ...and the head (DH)
mega_fdcmos db 0xFF                 ; CMOS byte 10h, once read
mega_fdx    db 0                    ; 1 while dsk_xfer's int 13h is in flight
mega_fddir  db 0                    ; the CCR read: bit 7 the change line

; =============================================================================
; M3: MEMORY ABOVE 1MB (KERN-MEGA-PLAN 10, wave M3; 12.2). Two things live
; there once mega_enter has run, and both are reached by a DESCRIPTOR, which
; is the whole of what protected mode adds:
;
;   * `.cold`, copied up and its GDT entry re-based (mega_coldup). Its
;     selector does not change, so no far reference anywhere is touched - not
;     the API table's, not a module's, not a task frame's - and its 37KB rung
;     in conventional memory becomes package heap through the ROM path's own
;     word ([rom_vgabw], mem_floor_ax), the planar decoder's buffers moving to
;     the bottom of the dead rung exactly as they do when a ROM adopts `.cold`.
;   * the XMEM pool, from MEGA_XPOOL to the top of extended memory, served by
;     the kernel itself behind the four published cells (xm_dsp's kern_mega
;     arm): XMEM.DRV's allocator, its block table and its error codes, with
;     the copy a `rep movsw` through two scratch descriptors - no BIOS, no
;     reset, no LOADALL. Every package that uses those cells gets the memory
;     with no change (12.2's table).
;
; THE TWO SCRATCH DESCRIPTORS are MEGA_SEL_XA and MEGA_SEL_XB, the GDT's last
; two entries. They are written through MEGA_SEL_WIN, as mega_oddsegf writes
; its own, and are only ever held for the length of one copy under the
; switch lock.
; =============================================================================
MEGA_XPOOL  equ 0x100000            ; the pool: from 1MB up to the fixed
                                    ; area mega_layoutf puts at the TOP of
                                    ; extended memory (KERN-MEGA-MEMORY-PLAN
                                    ; MC2), the HMA its first, pinned, 64KB.
                                    ; Its low word is 0, which every
                                    ; `add dx, MEGA_XPOOL >> 16` rests on.
                                    ; (0x140000 until MC2: `.cold`, `.mega`
                                    ; and ~50KB of padding sat under it)
MEGA_SCR_OFF equ (MEGA_SIZE + 15) & ~15 ; ...its offset from `.mega`'s base

; mega_xdesc - BX = a GDT selector, DX:AX = a 24-bit base -> a 64KB data
; descriptor there. Clobbers nothing
mega_xdesc:
    push es
    push di
    mov di, MEGA_SEL_WIN
    mov es, di
    mov di, bx
    and di, 0xFFF8
    mov word [es:di], 0xFFFF
    mov [es:di+2], ax
    mov [es:di+4], dl
    mov byte [es:di+5], 0x92
    mov word [es:di+6], 0
    pop di
    pop es
    ret

; mega_mdesc - DX:AX = a 24-bit base -> MEGA_SEL_CODE, `.mega`'s CODE
; descriptor, there, its limit `.mega`'s own last byte, and MEGA_SEL_PSCR at
; the scratch past it. Clobbers nothing
mega_mdesc:
    push bx
    mov bx, MEGA_SEL_CODE
    call mega_xdesc
    push es
    push di
    mov di, MEGA_SEL_WIN
    mov es, di
    mov di, MEGA_SEL_CODE
    mov word [es:di], MEGA_SIZE - 1
    mov byte [es:di+5], 0x9A        ; present, ring 0, code, readable
    pop di
    pop es
    push ax                         ; ...and the park's scratch, past its end
    push dx
    add ax, MEGA_SCR_OFF
    adc dx, 0
    mov bx, MEGA_SEL_PSCR
    call mega_xdesc
    pop dx
    pop ax
    pop bx
    ret

; mega_lincopy - CX bytes (even, 2..65534) from linear [mega_xsrc] to linear
; [mega_xdst], DS = KDATA. Clobbers AX BX CX DX SI DI; DS and ES preserved.
; The body is `.mega`'s (mx_lcopyf, KERN-MEGA-PLAN 14.49): these four bytes
; are what `.text` keeps, for mega_coldup and HIBER.DRV's near callers
mega_lincopy:
    call MEGA_SEL_CODE:mx_lcopyf
    ret

; mega_hilin - DX:AX = `.mega`'s linear base above 1MB: `.cold`'s, at its
; rung's distance, as in the ladder (MEGA_HI_LIN until KERN-MEGA-MEMORY-PLAN
; MC2 made the fixed area move with the machine). Clobbers nothing else
mega_hilin:
    mov ax, [mega_coldlin]
    mov dx, [mega_coldlin+2]
    add ax, (COLD_PARA * 16) & 0xFFFF
    adc dx, (COLD_PARA * 16) >> 16
    ret

; -----------------------------------------------------------------------------
; mega_coldup - `.cold` and `.mega` above 1MB, and their rungs to the heap. In
; mega_enter, with the real tables loaded and nothing executing in either
; (mega_enter is .text, called from kmain's .text). CF = 1 and nothing moved
; when the machine has too little extended memory for it - and `.mega` then
; runs from its own rung, which stays: MEGA_SEL_CODE is written either way,
; and BEFORE anything can far-call it (KERN-MEGA-SEGMENTS-PLAN 3.3)
; -----------------------------------------------------------------------------
mega_coldup:
    mov ax, (MEGA_RAM << 4) & 0xFFFF ; `.mega` where the boot put it...
    mov dx, MEGA_RAM >> 12
    call mega_mdesc
    cmp byte [mega_codehi], 0       ; mega_layoutf: no room at the top
    je .no
    mov word [mega_xsrc], (COLD_RAM << 4) & 0xFFFF
    mov word [mega_xsrc+2], COLD_RAM >> 12
    mov ax, [mega_coldlin]
    mov dx, [mega_coldlin+2]
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov cx, (COLD_SIZE + 1) & 0xFFFE
    call mega_lincopy
    mov word [mega_xsrc], (MEGA_RAM << 4) & 0xFFFF ; ...and `.mega`, a copy of
    mov word [mega_xsrc+2], MEGA_RAM >> 12 ; its own: the two can pass 64KB
    call mega_hilin                 ; DX:AX = where it goes
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov cx, (MEGA_SIZE + 1) & 0xFFFE
    call mega_lincopy
    call mega_hilin                 ; ...whose descriptor follows it up
    call mega_mdesc
    ; --- the descriptor: COLD_SEG:COLD_VSTART is the copy's first byte -----
    push es
    mov ax, MEGA_SEL_WIN
    mov es, ax
    mov di, COLD_SEG & 0xFFF8
    mov ax, [mega_coldlin]
    mov dx, [mega_coldlin+2]
    sub ax, COLD_VSTART & 0xFFFF
    sbb dx, COLD_VSTART >> 16
    mov [es:di+2], ax
    mov [es:di+4], dl
    pop es
    ; --- ...and the rung: the decoder's buffers to its bottom, the floor
    ; onto it - [rom_vgabw] is both (mem_floor_ax, ROM-PLAN 3.4.2)
    mov ax, COLD_RAM
    call COLD_SEG:mega_vbaimf
%if VGABUF_PARA
    test byte [vid_avail], VID_A_VGA
    jz .floor
    add ax, VGABUF_PARA
.floor:
%endif
    mov [mem_base], ax
    mov byte [mega_coldhi], 1
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; THE XMEM POOL'S SERVICES - XMEM.DRV's XMV_* table, in the kernel. The block
; table, the first-fit walk, the 1KB granularity and every error code are
; that image's (drivers/xmem/xmem.asm, whose notes say why each is what it
; is); only the transport differs. Entered by xm_dsp's kern_mega arm with DS
; = KDATA and ES = KDATA, which is what the overlay was handed too
; -----------------------------------------------------------------------------
MX_BLKSZ    equ 10
MX_OFF      equ 0                   ; word: KB from the pool base
MX_KB       equ 2                   ; word: size in KB, 0 = a free entry
MX_OWN      equ 4                   ; byte: the instance (0xFF the kernel)
MX_FL       equ 5                   ; byte: MX_FHUGE, cleared by every take
MX_FHUGE    equ 1                   ; an OSAPI_MEM_HUGE claim: its MX_SEL is
                                    ; the FIRST of a run, one per 64KB, and
                                    ; OSAPI_MEM_REGROW keeps it in the pool at
                                    ; any size (KERN-MEGA-PLAN 14.37)
MX_FH32     equ 2                   ; ...with MX_FHUGE: a window every 32KB,
                                    ; each 64KB long (BX = 8000h asked it,
                                    ; KERN-MEGA-PLAN 14.41) - mx_unsel frees
                                    ; one per 32KB, and it does not regrow
MX_SEL      equ 6                  ; word: OSAPI_XMEM_SEL's selector over a
                                    ; window of this block, 0 = none - freed
                                    ; with the block (mx_free, mx_relall)
MX_FPURGE   equ 4                   ; MX_FL: a kernel CACHE (MX_KOWN) the next
                                    ; claim may take back, its tag in MX_AUX
                                    ; and its one selector in MX_SEL
                                    ; (KERN-MEGA-MEMORY-PLAN MC1)
MX_AUX      equ 8                   ; word: an MX_FPURGE block's MEM_P_* tag,
                                    ; whose high byte is its purge level - or
                                    ; a TOKEN block's token, in KB from the
                                    ; pool's base (mx_istok, KERN-MEGA-MEMORY-
                                    ; PLAN MC3)
MX_FPIN     equ 8                   ; MX_FL: PINNED - something holds its
                                    ; LINEAR address (the DOS trip's park and
                                    ; arena, the hibernate's resume staging),
                                    ; so the compactor leaves it where it is.
                                    ; Every other block but the HMA moves (MC3)
MX_FBRIDGE  equ 0x40                ; ...with MX_FHUGE: a BRIDGED claim's pool
                                    ; piece (KERN-MEGA-MEMORY-PLAN MC5), its
                                    ; run going on over a conventional claim
                                    ; whose pair's data selector is MX_AUX:
                                    ; OSAPI_MEM_FREE frees both, REGROW refuses
MX_FH16     equ 0x20                ; ...with MX_FHUGE: a window every 16KB,
                                    ; each 64KB long (BX = 4000h asked it,
                                    ; KERN-MEGA-MEMORY-PLAN MC7.4), MX_FH32's
                                    ; rules otherwise
MX_FWIN     equ 0x10                ; MX_FL: an OSAPI_SEG BOUND window lives in
                                    ; it (SEG_BWIN), which a rate hook may read
                                    ; through: it moves with the hook held off
                                    ; (mx_cpflags, KERN-MEGA-MEMORY-PLAN 9.1)
MX_NBLK     equ 96                  ; entries (KERN-MEGA-PLAN 14.49): every
                                    ; pool claim, region, driver image and
                                    ; kernel block is one. Nine programs and
                                    ; SOUND.DRV take 35 under Bochs (Word,
                                    ; Scribe, the Video Player, Paint,
                                    ; Sheet, Tracker, Note Pad, TexPad,
                                    ; Calculator), where 24 ran out; a full
                                    ; INST_MAX of twelve is ~60. 8 bytes of
                                    ; LOW_SEG each, none of the segment

; mega_xminit - the pool behind the cells, from mega_enter
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_xminit:
    push cx                         ; the table empty: .lowbss is not zeroed
    push di                         ; at boot, and every entry with a KB of
    push es                         ; 0 is a free one
    push ss
    pop es
    mov di, mx_tab
    mov cx, MX_NBLK * MX_BLKSZ / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    mov ax, [mega_fixkb]            ; the pool: from 1MB to the fixed area
    cmp ax, MEGA_HMA_KB             ; (mega_layoutf, KERN-MEGA-MEMORY-PLAN
    jbe .none                       ; MC2) - and nothing past the HMA, none.
                                    ; THE HMA IS POOL (MC4): its first 64KB,
                                    ; reached last by every top-down claim;
                                    ; a DOS trip keeps its own claims above
                                    ; it ([mega_mxflr]) and saves what stands
                                    ; there with the park
    mov word [xm_svc+XMV_CAPS], mx_caps
    mov word [xm_svc+XMV_ALLOC], mx_alloc
    mov word [xm_svc+XMV_FREE], mx_free
    mov word [xm_svc+XMV_COPY], mx_copy
    mov word [xm_svc+XMV_RELINST], mx_relinst
    mov [xm_kb], ax                 ; THE GATE, last (xmem.inc's note)
.none:
    ret

section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
; mx_maxrun - AX = the largest claim ONE pool block can be given now
; (KERN-MEGA-MEMORY-PLAN MC5): the pool as mx_alloc leaves it after its
; pack (every movable block UP against the pinned one above it or the pool's
; top, mx_packupf) with every cache shed (a program outranks each one,
; mx_shed), above [mega_mxflr]. So each stretch between two pinned blocks
; offers its length less the movable, non-cache KB that start in it, and
; the answer is the best stretch. Exact for that pack; every other register
; kept
mx_maxrun:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    xor bp, bp                      ; BP = the best so far
    mov di, mx_tab - MX_BLKSZ       ; DI = the pinned block whose END starts
.lo:                                ; the stretch (the floor, first)
    mov bx, [mega_mxflr]
    cmp di, mx_tab
    jb .have
    cmp word [ss:di+MX_KB], 0
    je .next
    test byte [ss:di+MX_FL], MX_FPIN
    jz .next
    mov bx, [ss:di+MX_OFF]
    add bx, [ss:di+MX_KB]
    cmp bx, [mega_mxflr]
    jb .next
.have:                              ; BX = lo; DX = the next pinned start
    mov dx, [xm_kb]
    mov si, mx_tab
.h:
    cmp word [ss:si+MX_KB], 0
    je .hn
    test byte [ss:si+MX_FL], MX_FPIN
    jz .hn
    mov ax, [ss:si+MX_OFF]
    cmp ax, bx
    jb .hn
    cmp ax, dx
    jae .hn
    mov dx, ax
.hn:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .h
    mov cx, dx                      ; CX = the stretch's KB...
    sub cx, bx
    jbe .next
    mov si, mx_tab
.m:
    cmp word [ss:si+MX_KB], 0
    je .mn
    test byte [ss:si+MX_FL], MX_FPIN | MX_FPURGE
    jnz .mn                         ; (pinned: a wall; a cache: shed)
    mov ax, [ss:si+MX_OFF]
    cmp ax, bx
    jb .mn
    cmp ax, dx
    jae .mn
    sub cx, [ss:si+MX_KB]           ; ...less what is packed up into it
    jnc .mn
    xor cx, cx
.mn:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .m
    cmp cx, bp
    jbe .next
    mov bp, cx
.next:
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jb .lo
    mov ax, bp
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
mx_maxrunf:
    call mx_maxrun
    retf

; mx_caps - OSAPI_XMEM_CAPS's answer (KERN-MEGA-PLAN 14.122): AX = the KB
; the pool can still hand out, BL = the table entries a claim can still have.
; A kernel CACHE (MX_FPURGE) counts as FREE in both, because every claim that
; reaches the pool through a cell outranks it and mx_alloc sheds it on the
; way (mx_shed, MC1) - the rule mem_avail_lvl_x applies to a conventional
; cache (SPEC.md 66.4) and mx_maxrun to this pool. Subtracting it made the
; read-ahead store's 32KB read as used to every package that plans from here
mx_caps:
    push si
    mov ax, [xm_kb]
    xor bl, bl
    mov si, mx_tab
.scan:
    test byte [ss:si+MX_FL], MX_FPURGE
    jnz .free                       ; (a cache: the claim sheds it)
    sub ax, [ss:si+MX_KB]
    cmp word [ss:si+MX_KB], 0
    jne .next
.free:
    inc bl
.next:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .scan
    mov dx, MEGA_XPOOL >> 16
    xor cx, cx
    pop si
    ret

mx_alloc:
    push bx
    push cx
    push si
    push di
    push bp
    inc byte [sch_lock]             ; ONE STEP for every task: the walk, the
                                    ; take and a compaction (KERN-MEGA-PLAN
                                    ; 14.37) - interrupts stay on
    add ax, 1023
    adc dx, 0
    jc .nr
    call mx_tokb
    jc .nr                          ; (64MB or more: no pack can make it)
    xchg ax, cx                     ; CX = KB wanted
    or cx, cx
    jnz .slot
    inc cx
.slot:
    mov di, mx_tab - MX_BLKSZ
.fscan:
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jae .full
    cmp word [ss:di+MX_KB], 0
    jne .fscan
                                    ; EVERY CLAIM FROM THE TOP (KERN-MEGA-
                                    ; MEMORY-PLAN MC3, D1): free memory is
                                    ; then one run from the pool's base up,
                                    ; and the HMA - its first 64KB - is
                                    ; reached last
.top:                               ; first fit DOWNWARD: the candidate
    mov bp, [xm_kb]                 ; ends at the pool's top, and an overlap
    sub bp, cx                      ; moves it to end where that block
    jc .norun                       ; starts - strictly falling, so it ends
.trestart:
    cmp bp, [mega_mxflr]            ; nothing under the FLOOR: a DOS trip's
    jb .norun                       ; own claims stay out of the HMA (MC4)
    mov dx, bp
    add dx, cx
    mov si, mx_tab
.tscan:
    mov ax, [ss:si+MX_KB]
    or ax, ax
    jz .tnext
    add ax, [ss:si+MX_OFF]
    cmp bp, ax
    jae .tnext
    cmp [ss:si+MX_OFF], dx
    jae .tnext
    mov bp, [ss:si+MX_OFF]
    sub bp, cx
    jc .norun
    jmp short .trestart
.tnext:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .tscan
    xor ax, ax                      ; AX = MX_AUX: 0, or a TOKEN block's
    call mx_istok                   ; token (an OSAPI_XMEM_ALLOC block, MC3):
    jne .take                       ; a range no live token overlaps, first
    call mx_tokfit                  ; tried where the block IS - so it is the
    jc .norun                       ; block's base until the block moves
.take:
    pushf
    cli
    mov word [ss:di+MX_SEL], 0
    mov [ss:di+MX_AUX], ax
    mov [ss:di+MX_OFF], bp
    mov [ss:di+MX_KB], cx
    mov bh, 0
    mov [ss:di+MX_OWN], bx             ; ...and MX_FL clear
    popf
    call mx_istok                   ; a token block is NAMED by its token,
    jne .phys                       ; everything else by where it is
    mov bp, ax
.phys:
    mov ax, bp
    call mx_kblin                   ; DX:AX = the name, linear
    jmp short .out
.norun:
    cmp byte [mega_cpry], 0         ; NO RUN, BUT THE POOL MAY HOLD IT IN
    jne .shed                       ; PIECES (KERN-MEGA-PLAN 14.15): pack the
    mov byte [mega_cpry], 1         ; movable blocks UP once - against the
    mov ax, [mega_mxflr]            ; fixed area or a pinned block, so every
    call MEGA_SEL_CODE:mx_packupf   ; free KB below them is one run (MC3) -
    jmp .top                        ; and try again (CX the KB, DI the free
                                    ; entry: both kept)
.shed:                              ; ...THEN A CACHE THE CLAIMANT OUTRANKS
    call mx_shed                    ; GOES (KERN-MEGA-MEMORY-PLAN MC1), and
    jc .nr                          ; the whole walk runs again - a compaction
    mov byte [mega_cpry], 0         ; included, the hole being new
    jmp .slot
.nr:
    mov al, 2
    jmp short .fail
.full:
    call mx_shed                    ; no free ENTRY: a cache's frees one
    jnc .slot
    mov al, 1
.fail:
    cbw
    stc
.out:
    mov byte [mega_cpry], 0
    dec byte [sch_lock]             ; (dec leaves CF)
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; mx_shed - for mx_alloc's two refusals: the cheapest purgeable cache above
; 1MB that the claimant outranks goes (mx_shedf, `.cold`, mem_shed_one's rule
; for the pool). BL = the claimant's owner; for MX_KOWN, BH = its level (a
; cache tag's high byte, else below MEM_PG_MIN); every other owner is a
; program's or a driver's and outranks every cache. CF = 0 one went, CF = 1
; none may. Clobbers nothing else
mx_shed:
    push bx
    cmp bl, MX_KOWN
    je .k
    mov bh, 0xFF                    ; MEM_LVL_TOP's byte
.k:
    call COLD_SEG:mx_shedf
    pop bx
    ret

; -----------------------------------------------------------------------------
; THE TOKEN (KERN-MEGA-MEMORY-PLAN MC3). An OSAPI_XMEM_ALLOC block is the one
; kind a package names by a NUMBER rather than a selector - DX:AX, which it
; adds offsets to and hands back to OSAPI_XMEM_COPY, _SEL, _RUN and _FREE. It
; used to be the block's linear base, so the block could never move. It is a
; TOKEN now: a KB range of its own in MX_AUX, chosen where the block is when
; it is taken (so the two agree until it first moves) and never overlapping
; another live token, and every cell finds the block by it and translates to
; where the block is NOW. No package reads a token in place on kern_mega
; (LOADALL and unreal mode are real-mode techniques, gated off under
; CPU_F_PM), so the number is all it ever was to them.
; -----------------------------------------------------------------------------

; mx_istok - BL = an owner -> ZF = 1 its blocks are named by TOKEN: a slot
; (00h-7Fh, an instance's OSAPI_XMEM_ALLOC) or FFh (the cell called outside
; any instance - a driver's store). Clobbers nothing
mx_istok:
    cmp bl, 0xFF
    je .o
    test bl, 0x80
.o:
    ret

; mx_key - SI = an entry, BL = its owner -> AX = the KB it is NAMED by: its
; token's for a token owner, where it is for every other. Clobbers nothing else
mx_key:
    mov ax, [ss:si+MX_OFF]
    call mx_istok
    jne .o
    mov ax, [ss:si+MX_AUX]
.o:
    ret

; mx_tophys - SI = an entry, BL = its owner, DX:AX = a linear address in the
; block's NAME -> DX:AX = the same byte where the block is now, CF = 0 (a
; block that moved UP subtracts a negative distance, whose borrow is not an
; answer). Clobbers nothing else
mx_tophys:
    call mx_istok
    jne .o
    push bx
    push cx
    mov bx, [ss:si+MX_AUX]
    sub bx, [ss:si+MX_OFF]          ; KB the name is above the block, signed
    mov cx, bx
    shl bx, 10
    sar cx, 6                       ; CX:BX = bytes
    sub ax, bx
    sbb dx, cx
    pop cx
    pop bx
.o:
    clc
    ret

; mx_tokfit - for mx_alloc: CX = KB, BP = where the block will be -> CF = 0
; and AX = a token KB whose CX KB no live token block overlaps, the first
; tried being BP; CF = 1 none under 64MB. Clobbers nothing else
mx_tokfit:
    push bx
    push dx
    push si
    mov ax, bp
.re:
    mov dx, ax
    add dx, cx                      ; DX = the range's end
    jc .o
    mov si, mx_tab
.l:
    cmp word [ss:si+MX_KB], 0
    je .n
    mov bl, [ss:si+MX_OWN]
    call mx_istok
    jne .n
    mov bx, [ss:si+MX_AUX]
    cmp bx, dx
    jae .n                          ; starts at or past our end
    add bx, [ss:si+MX_KB]
    cmp bx, ax
    jbe .n                          ; ends at or before our start
    xchg ax, bx                     ; overlaps: try just past it
    jmp short .re
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .l
    clc
.o:
    pop si
    pop dx
    pop bx
    ret

mx_free:
    push ax
    push cx
    push dx
    push si
    test ax, 0x03FF
    jnz .bad
    call mx_rel
    jc .bad
    mov si, mx_tab
    pushf
    cli
.scan:
    cmp word [ss:si+MX_KB], 0
    je .next
    cmp [ss:si+MX_OWN], bl
    jne .next
    xchg ax, cx
    call mx_key                     ; AX = what it is named by (MC3)
    xchg ax, cx
    cmp cx, ax
    jne .next
    call mx_unsel                   ; its OSAPI_XMEM_SEL selector goes too
    mov word [ss:si+MX_KB], 0
    mov byte [ss:si+MX_OWN], 0xFF
    popf
    jmp short .out
.next:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .scan
    popf
.bad:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

mx_relall:
    push si
    pushf
    cli
    mov si, mx_tab
.scan:
    cmp [ss:si+MX_OWN], al
    jne .next
    call mx_unsel
    mov word [ss:si+MX_KB], 0
    mov byte [ss:si+MX_OWN], 0xFF
.next:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .scan
    mov si, mega_wtab               ; ...and the owner's OSAPI_SEG windows
.w:                                 ; (mega_segf, KERN-MEGA-MEMORY-PLAN MC7)
    cmp word [ss:si], 0
    je .wn
    cmp [ss:si+2], al
    jne .wn
    push ax
    mov ax, [ss:si]
    call mega_ldt_free
    pop ax
    mov word [ss:si], 0
.wn:
    add si, 4
    cmp si, mega_wtab_end
    jb .w
    popf
    pop si
    ret

; mx_relinst - an instance's teardown (XMV_RELINST): AL = its slot -> its
; OSAPI_XMEM_ALLOC blocks and the CLAIMS it took above 1MB (owner 0A0h | slot,
; OS88_HIGHMEM), each with its selector. NOT its region (80h | slot), which
; the region's own teardown gives back once nothing of it runs. Clobbers
; nothing
mx_relinst:
    call mx_relall
    push ax
    or al, 0xA0
    call mx_relall
    pop ax
    ret

; mx_unsel - SI = an mx_tab entry: its OSAPI_XMEM_SEL selector, if it has
; one, back to the allocator. Clobbers nothing
mx_unsel:
    push ax
    push cx
    xor ax, ax
    xchg ax, [ss:si+MX_SEL]
    or ax, ax
    jz .none
    mov cx, 1
    test al, 1                  ; A WINDOW RUN (OSAPI_XMEM_RUN, mega_xmrunf):
    jz .nrun                    ; bit 0 marks it - AX and CX in .cold, the
    call MEGA_SEL_CODE:mx_runcntf    ; segment binding on kern_mega
    jmp short .l
.nrun:
    test byte [ss:si+MX_FL], MX_FHUGE  ; A RUN (OSAPI_MEM_HUGE, mega_hugef): a
    jz .l                       ; selector per 64KB, consecutive - a
    mov cx, [ss:si+MX_KB]          ; package's or a driver's. Every other block
                                ; has the one selector it was given
    test byte [ss:si+MX_FL], MX_FH32   ; (or one per 32KB, KERN-MEGA-PLAN 14.41)
    jz .h16
    add cx, 31
    shr cx, 5
    jmp short .l
.h16:
    test byte [ss:si+MX_FL], MX_FH16   ; (or per 16KB, KERN-MEGA-MEMORY-PLAN
    jz .h64                            ; MC7.4)
    add cx, 15
    shr cx, 4
    jmp short .l
.h64:
    add cx, 63
    shr cx, 6
.l:
    call mega_ldt_free
    add ax, 8
    loop .l
.none:
    pop cx
    pop ax
    call mx_dfree                   ; ...and its OSAPI_SEG SUB selectors
    ret

; mx_dfree - SI = an mx_tab entry: when it is a PROGRAM's or a DRIVER's claim
; (a slot, 0A0h-0DFh or FFh: not a region, an image or the kernel's own, whose
; selectors are freed by their own owners), every allocator DATA descriptor
; still present whose base lies inside the block is free, a SEG_SUBC pair's
; code half too - OSAPI_SEG's SUB selectors and bound windows, which are tied
; to the block
; (KERN-MEGA-MEMORY-PLAN MC7). Clobbers nothing
mx_dfree:
    push ax
    mov al, [ss:si+MX_OWN]
    cmp al, 0xFF
    je .go
    cmp al, 0x80
    jb .go
    cmp al, 0xA0
    jb .o
    cmp al, 0xE0
    jae .o
.go:
    push bx
    push cx
    push dx
    push di
    push es
    mov ax, [ss:si+MX_OFF]
    call mx_kblin
    mov cx, ax
    mov bx, dx                      ; BX:CX = the block's base
    mov ax, [ss:si+MX_OFF]
    add ax, [ss:si+MX_KB]
    call mx_kblin                   ; DX:AX = its end
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov di, MEGA_LDT_FREE0 * 8
.d:
    cmp byte [es:di+5], 0
    je .dn
    cmp [es:di+4], bl               ; base >= the block's? (data or code: a
                                    ; SEG_SUBC pair's halves both go)
    jb .dn
    ja .ge
    cmp [es:di+2], cx
    jb .dn
.ge:
    cmp [es:di+4], dl               ; ...and below its end?
    ja .dn
    jb .in
    cmp [es:di+2], ax
    jae .dn
.in:
    push ax
    mov ax, di
    call mega_iswin                 ; a window is its owner's (14.80)
    jnc .din
    lea ax, [di+4]
    call mega_ldt_free
.din:
    pop ax
.dn:
    add di, 8
    cmp di, [ss:mega_ldte]
    jb .d
    pop es
    pop di
    pop dx
    pop cx
    pop bx
.o:
    pop ax
    ret
; ...and the FAR doors into .text for mega_hugef and mx_compactf (.cold)
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_lincopyf:                      ; (mega_lincopy's far door: the body is
    call MEGA_SEL_CODE:mx_lcopyf    ; `.mega`'s, KERN-MEGA-PLAN 14.49)
    retf
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mx_allocf:
    call mx_alloc
    retf
mx_freef:
    call mx_free
    retf
mx_cpmovef:
    call mx_cpmove
    retf
mega_ownerf:
    call mega_ownerof
    retf
mega_xmself:
    call mega_xmsel
    retf

; -----------------------------------------------------------------------------
; mega_xmsel - OSAPI_XMEM_SEL's body in protected mode (KERN-MEGA-PLAN 12.2):
; DX:AX = a linear address, CX = bytes (0 = 65,536), BL = the caller ->
; CF = 0 and AX = a DATA selector whose descriptor covers exactly that window,
; which must lie inside ONE block the caller owns; CF = 1 and AX = 2 it does
; not, AX = 1 no LDT entry is free. ONE SELECTOR A BLOCK: a second call for
; the same block re-aims it (a segment register already holding it keeps the
; old window until it is reloaded), and freeing the block - or the instance
; closing - frees it. The selector does not move when the block's bytes
; would, which is why no relocation proc is owed for it. DS = KDATA.
; Clobbers nothing else
; -----------------------------------------------------------------------------
mega_xmsel:
    push bx
    push cx
    push dx
    push si
    push di
    inc byte [sch_lock]             ; the scan to the MX_SEL it leaves: no
                                    ; other task moves the block or re-aims
                                    ; one between (14.37; it nests)
    mov [mxs_lo], ax                ; the window, banked: the arithmetic
    mov [mxs_hi], dx                ; below spends every register (the XMEM
    mov [mxs_len], cx               ; cells are the UI task's, so one copy)
    or dh, dh
    jnz .not                        ; above 16MB: no pool there
    sub dx, MEGA_XPOOL >> 16
    jc .not                         ; below the pool
    mov si, ax                      ; SI = the window's first KB from the
    mov di, dx                      ; pool's base
    push cx
    mov cl, 10
    shr si, cl
    mov cl, 6
    shl di, cl
    or si, di
    pop cx
    jcxz .full                      ; DX:AX = its end, exclusive
    add ax, cx
    adc dx, 0
    jmp short .end
.full:
    inc dx                          ; 0 = 65,536 bytes
.end:
    add ax, 1023                    ; ...rounded up to a KB
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; AX = the first KB past the window
    mov di, mx_tab
.scan:
    cmp word [ss:di+MX_KB], 0
    je .next
    cmp [ss:di+MX_OWN], bl
    jne .next
    mov dx, [ss:di+MX_OFF]          ; where it is - or, for a token block,
    call mx_istok                   ; what it is NAMED (MC3)
    jne .k
    mov dx, [ss:di+MX_AUX]
.k:
    cmp dx, si
    ja .next                        ; the block starts after the window...
    add dx, [ss:di+MX_KB]
    cmp dx, ax
    jae .found                      ; ...or ends at or after its end: ours
.next:
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jb .scan
.not:
    mov ax, 2                       ; not inside a block the caller owns
    jmp short .fail
.found:
    mov ax, [mxs_lo]
    mov dx, [mxs_hi]                ; DX:AX = the descriptor's base, where
    mov si, di                      ; the block is now
    call mx_tophys
    mov cx, [mxs_len]
    dec cx                          ; ...and its limit (0 - 1 = 64KB)
    mov si, [ss:di+MX_SEL]
    or si, si
    jz .new
    test si, 1                      ; (an OSAPI_XMEM_RUN: it is not one
    jnz .not                        ; window, so it is not re-aimed)
    push es                         ; RE-AIM the block's one selector
    push di
    mov di, MEGA_SEL_LDTD
    mov es, di
    mov di, si
    and di, 0xFFF8
    pushf
    cli
    mov [es:di], cx
    mov [es:di+2], ax
    mov [es:di+4], dl
    popf
    pop di
    pop es
    xchg ax, si                     ; AX = the selector (CF = 0: the `and`)
    jmp short .out
.new:
    mov bl, 0x92                    ; (BL is spent: the scan is done)
    call mega_ldt_alloc             ; AX = the selector
    jc .noldt
    mov [ss:di+MX_SEL], ax
    jmp short .out
.noldt:
    mov ax, 1                       ; no LDT entry free
.fail:
    stc
.out:
    dec byte [sch_lock]             ; (dec leaves CF)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mega_codesel - OSAPI_MEM_CODESEL's body in protected mode: DX = a segment or
; selector naming a claim -> CF = 0 and AX = the selector to far call code in
; it; CF = 1 there is none. The CODE half of an allocator pair is itself, and
; the DATA half its code twin (- 8): every claim is a pair (KERN-MEGA-MEMORY-
; PLAN MC7.4), and the static code aliases (S >> 2) | 4 went with MC7's step
; 6. Anything else - a GDT segment, the allocator's lone data selectors (the
; read-ahead store, OSAPI_XMEM_SEL's windows) - has no code view and is
; refused rather than guessed at. Clobbers nothing else
; -----------------------------------------------------------------------------
mega_codesel:
    mov ax, dx
    test al, 4
    jz .no                          ; the GDT's: no claim's
    test al, 3
    jnz .no                         ; RPL 0 only
    cmp ax, MEGA_LDT_FREE0 * 8
    jb .no                          ; below the allocator's: the GDT's own
    cmp ax, [ss:mega_ldte]
    jae .no
    push bx
    push es
    mov bx, MEGA_SEL_LDTD
    mov es, bx
    mov bx, ax
    and bx, 0xFFF8
    test byte [es:bx+5], 0x08       ; code?
    jnz .pop                        ; ...a pair's code half: itself
    cmp bx, MEGA_LDT_FREE0 * 8 + 8
    jb .popno
    test byte [es:bx-8+5], 0x08     ; the entry below is CODE over the SAME
    jz .popno                       ; base: this is a pair's data half
    push ax
    mov ax, [es:bx+2]
    cmp ax, [es:bx-8+2]
    jne .popax
    mov al, [es:bx+4]
    cmp al, [es:bx-8+4]
.popax:
    pop ax
    jne .popno
    sub ax, 8
.pop:
    pop es
    pop bx
.yes:
    clc
    ret
.popno:
    pop es
    pop bx
.no:
    stc
    ret

section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mxs_lo      dw 0                    ; mega_xmsel's window, banked
mxs_hi      dw 0
mxs_len     dw 0

; DX:AX = linear (mx_rel, as KB from the pool base) or bytes (mx_tokb, as
; KB) -> AX; CF = 1 below the pool or 64MB or more. Clobbers CL, DX
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mx_rel:
    sub dx, MEGA_XPOOL >> 16
    jc mx_tokb.out
mx_tokb:
    cmp dh, 0x04
    cmc
    jc .out
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
.out:
    ret

; DX:AX = linear - a NAME: a token block's token, every other block's place
; (mx_istok, MC3) - CX = bytes, BL = the caller -> CF = 0 inside one of its
; blocks, and DX:AX = where those bytes are NOW (mx_tophys); CF = 1 not, and
; DX:AX kept. Clobbers nothing else
mx_chk:
    push cx
    push si
    push di
    push ax
    push dx
    mov di, ax
    and di, 0x03FF
    add di, cx
    add di, 1023
    mov cl, 10
    shr di, cl
    call mx_rel
    jc .no
    add di, ax
    jc .no
    mov si, mx_tab
.scan:
    cmp [ss:si+MX_OWN], bl
    jne .next
    cmp word [ss:si+MX_KB], 0
    je .next
    push ax
    call mx_key                     ; AX = the KB it is named by
    xchg ax, cx
    pop ax
    cmp cx, ax
    ja .next
    add cx, [ss:si+MX_KB]
    cmp cx, di
    jae .found
.next:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .scan
.no:
    pop dx
    pop ax
    stc
    jmp short .out
.found:
    pop dx
    pop ax
    call mx_tophys                  ; (CF = 0: mx_tophys keeps it)
.out:
    pop di
    pop si
    pop cx
    ret

; mx_copy - ES:SI = the kernel's XMC_ block. out CF = 0 done; CF = 1 and AX
; = 2 not inside a block the caller owns / 3 bad length. Clobbers nothing
; else (the cell banked what it needs)
mx_copy:
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [es:si+XMC_LEN]
    mov ax, 3
    jcxz .fail
    test cl, 1
    jnz .fail
    cmp cx, 32768
    ja .fail
    mov ax, [es:si+XMC_EXT]
    mov dx, [es:si+XMC_EXT+2]
    mov bl, [es:si+XMC_INST]
    call mx_chk                     ; DX:AX = where the block's bytes are now:
    jc .notmine                     ; the extended end is a TOKEN (MC3)
    push dx
    push ax
    mov ax, [es:si+XMC_CONV]        ; the conventional end: below 1MB...
    mov dx, [es:si+XMC_CONV+2]
    cmp dx, 0x10
    jb .lowend
    mov bl, [es:si+XMC_INST]        ; ...or inside the caller's OWN memory
    or bl, 0x80                     ; above it: its region (OS88_HIGH)...
    call mx_chk
    jnc .lowend
    or bl, 0xA0                     ; ...or a claim (OS88_HIGHMEM), each a
    call mx_chk                     ; place: the kernel took it off the
    jnc .lowend                     ; caller's selector a moment ago
    pop ax
    pop ax
    mov ax, 3
    jmp short .fail
.notmine:
    mov ax, 2
    jmp short .fail
.lowend:
    pop di                          ; BX:DI = the extended end
    pop bx
    cmp byte [es:si+XMC_DIR], 0
    jne .in
    mov [mega_xsrc], ax             ; out: conventional -> extended
    mov [mega_xsrc+2], dx
    mov [mega_xdst], di
    mov [mega_xdst+2], bx
    jmp short .go
.in:
    mov [mega_xsrc], di             ; in: extended -> conventional
    mov [mega_xsrc+2], bx
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
.go:
    call KERNEL_SEG:mega_lincopyf
    clc
    jmp short .out
.fail:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; THE LDT ALLOCATOR (KERN-MEGA-PLAN 13.3 step 1). Entries MEGA_LDT_FREE0 to
; MEGA_LDT_N - 1 (768 of them) are descriptors with ANY 24-bit base and limit,
; handed out and taken back by selector - where the entries below are the
; fixed aliases, base i x 512, that a 512-aligned image is far-called through.
; An entry is free when its access byte is 0 (not present), which is how
; mega_enter leaves the whole range. Everything that READS a selector's base
; asks the descriptor in this range (mega_selbase, mega_s2seg), and nothing
; that converts a code alias to its image (mega_cs2ds, mega_ivt_sync) takes
; one of these for an alias.
;
; KERN-MEGA-PLAN 13.4 is the decision about the memory behind them: a
; kernel-owned block above 1MB comes out of the SAME table as the XMEM cells'
; (mx_alloc, under MX_KOWN), so extended memory has one allocator, one
; first-fit walk and one free figure, and the Task Manager's XMS line counts
; what the kernel took.
; -----------------------------------------------------------------------------
MX_KOWN     equ 0xFE                ; mx_tab's owner for kern_mega's OWN blocks:
                                    ; not an instance, and not 0xFF - the cells'
                                    ; kernel-owned blocks, which a package's
                                    ; proc frees through mx_free
MX_KDRV     equ 0xFD                ; ...and for a DRIVER'S IMAGE (drv_hi_move):
                                    ; from the top like MX_KOWN's, but MOVED by
                                    ; the compactor, every holder of it naming
                                    ; its selector pair (KERN-MEGA-PLAN 14.37)

; mega_ldt_alloc - DX:AX = a 24-bit base, CX = the limit (bytes - 1), BL = the
; access byte (0x92 data, 0x9A code) -> CF = 0 and AX = the selector (TI = 1,
; RPL 0); CF = 1 and AX kept when all 768 are taken. Clobbers nothing else
mega_ldt_alloc:
    push di
    push es
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov di, MEGA_LDT_FREE0 * 8
    pushf
    cli                             ; the scan and the claim are one step
.scan:
    cmp byte [es:di+5], 0
    je .got
    add di, 8
    cmp di, [ss:mega_ldte]
    jb .scan
    popf
    stc
    jmp short .out
.got:
    mov [es:di], cx
    mov [es:di+2], ax
    mov [es:di+4], dl
    mov [es:di+5], bl               ; present: the entry is taken
    mov word [es:di+6], 0
    popf
    lea ax, [di+4]
    clc
.out:
    pop es
    pop di
    ret

; mega_ldt_alloc2 - a CODE/DATA PAIR over one block: DX:AX = the base, CX =
; the limit -> CF = 0 and AX = the code selector, readable, with its data
; twin at AX + 8 (the rule mega_cs2ds, mega_dsimg and mega_csdata apply to
; this range); CF = 1 and AX kept when no two adjacent entries are free.
; Clobbers nothing else
mega_ldt_alloc2:
    push di
    push es
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov di, MEGA_LDT_FREE0 * 8
    pushf
    cli
.scan:
    cmp byte [es:di+5], 0
    jne .next
    cmp byte [es:di+8+5], 0
    je .got
.next:
    add di, 8
    cmp di, [ss:mega_ldtl]
    jb .scan
    popf
    stc
    jmp short .out
.got:
    mov [es:di], cx
    mov [es:di+2], ax
    mov [es:di+4], dl
    mov byte [es:di+5], 0x9A        ; code, readable
    mov word [es:di+6], 0
    mov [es:di+8], cx
    mov [es:di+8+2], ax
    mov [es:di+8+4], dl
    mov byte [es:di+8+5], 0x92      ; ...and its data
    mov word [es:di+8+6], 0
    popf
    lea ax, [di+4]
    clc
.out:
    pop es
    pop di
    ret

; mega_ldt2ds - AX = a selector in the allocator's range -> CF = 0 and AX =
; its data twin (+ 8) when it is a pair's CODE half; CF = 1 otherwise.
; Clobbers nothing else
;
; A LONE code selector from mega_ldt_alloc (DESK.DRV's library above 1MB,
; mega_dklibf) is in this range too, and the entry after it is somebody
; else's or free: so the twin is taken only when it IS one - a present
; data entry over the same base. Without that, MEGATRAP's mega_tlog loaded
; the free entry after the library's selector into ES and faulted on every
; Sticky App's first callback (KERN-MEGA-PLAN 14.39, 14.46), and
; mega_hifree would have freed a neighbour's entry with a lone selector
mega_ldt2ds:
    push bx
    push es
    mov bx, MEGA_SEL_LDTD
    mov es, bx
    mov bx, ax
    and bx, 0xFFF8
    test byte [es:bx+5], 0x08       ; the access byte's EXECUTABLE bit
    jz .no
    cmp bx, [ss:mega_ldtl]    ; the last entry has no twin
    jae .no
    push ax
    mov al, [es:bx+8+5]
    and al, 0xFE                    ; (the CPU sets the ACCESSED bit)
    cmp al, 0x92                    ; present, data, writable: alloc2's
    jne .lone
    mov ax, [es:bx+2]               ; the same base, low word...
    cmp ax, [es:bx+8+2]
    jne .lone
    mov al, [es:bx+4]               ; ...and its third byte
    cmp al, [es:bx+8+4]
    jne .lone
    pop ax
    add ax, 8
    clc
    jmp short .out
.lone:
    pop ax
.no:
    stc
.out:
    pop es
    pop bx
    ret

; mega_ldt_free - AX = a selector mega_ldt_alloc gave -> its entry is free;
; CF = 1 and nothing touched for any other selector. Clobbers nothing else.
; The caller makes sure no segment register still holds it
mega_ldt_free:
    push di
    push es
    test al, 4
    jz .no
    mov di, ax
    and di, 0xFFF8
    cmp di, MEGA_LDT_FREE0 * 8
    jb .no
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    pushf
    cli
    mov word [es:di], 0
    mov word [es:di+2], 0
    mov word [es:di+4], 0
    mov word [es:di+6], 0
    popf
    push si                         ; ...and a WINDOW naming it is no longer
    mov si, mega_wtab               ; one: a block's free takes a window
.wt:                                ; based in it (mega_cdfree, mx_dfree),
    mov di, [ss:si]                 ; and its owner's end must not free the
    xor di, ax                      ; entry again once it is somebody else's
    and di, 0xFFF8
    jnz .wn
    mov word [ss:si], 0
.wn:
    add si, 4
    cmp si, mega_wtab_end
    jb .wt
    pop si
    push bp                         ; THE CALLER'S ES MAY BE THIS SELECTOR -
    mov bp, sp                      ; a claim freed with ES still on it, which
    mov di, [bp+2]                  ; PiXEL's Blur and Sepia do: the pop below
    xor di, ax                      ; then loaded a free entry, #GP that
    and di, 0xFFFC                  ; mega_gpfix's .stale arm turned into the
    jnz .kept                       ; NULL selector, a trap a free. It gets
    mov [bp+2], di                  ; that NULL here instead (KERN-MEGA-PLAN
.kept:                              ; 14.46)
    pop bp
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    ret

; mega_hiclaim - CX = bytes (1..65535), BX = the claim's MEM_* tag -> CF = 0
; and AX = a DATA selector over that many bytes above 1MB, all of them the
; caller's; CF = 1 and nothing taken (no pool, the pool full, no LDT entry).
; DS = KDATA. Clobbers nothing else. mega_hiclaimf is the same for a FAR
; caller (.cold).
;
; THE TAG IS KEPT (KERN-MEGA-MEMORY-PLAN MC1). A purgeable tag (MEM_P_*, its
; high byte MEM_PG_MIN..MEM_PG_MAX) marks the block MX_FPURGE with the tag in
; MX_AUX, so the next claim it is cheaper than can take it back (mx_shedf),
; and it is also the RANK this claim sheds with - a cache never sheds one
; worth more. The selector is the block's own (MX_SEL, mega_xmsel), so
; mx_free gives both back. Any other tag is the kernel's to hold: pinned,
; and outranking every cache
mega_hiclaim:
    push bx
    push cx
    push dx
    push si
    cmp word [xm_kb], 0
    je .no                          ; no pool: the machine refused it, or the
    mov si, bx                      ; switch never happened. SI = the tag
    cmp bh, MEM_PG_MIN
    jb .top
    cmp bh, MEM_PG_MAX
    jbe .lv                         ; BH = its level...
.top:
    mov bh, 0                       ; ...or below every one: the top rank
.lv:
    mov bl, MX_KOWN
    mov ax, cx
    xor dx, dx
    inc byte [sch_lock]             ; to the MX_FPURGE mark: the base is
    call mx_alloc                   ; used before any other task can move
    jc .nol                         ; it (14.37). DX:AX = its linear base
    push dx
    push ax
    call mega_xmsel                 ; AX = the selector, in its MX_SEL (BL is
    pop cx                          ; MX_KOWN still: the owner it scans for)
    pop dx
    jnc .got
    xchg ax, cx                     ; no entry: the block goes back
    call mx_free
.nol:
    dec byte [sch_lock]
.no:
    stc
    jmp short .out
.got:
    push ax
    xchg ax, cx                     ; DX:AX = the base again
    mov bx, si
    cmp bh, MEM_PG_MIN
    jb .pin
    cmp bh, MEM_PG_MAX
    ja .pin
    push si
    mov bl, MX_KOWN
    call mx_find                    ; SI = its entry
    pop bx                          ; BX = the tag
    jc .pin
    mov [ss:si+MX_AUX], bx
    or byte [ss:si+MX_FL], MX_FPURGE
.pin:
    pop ax
    dec byte [sch_lock]
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret
mega_hiclaimf:
    call mega_hiclaim
    retf

; mx_find - DX:AX = a block's linear base, BL = its owner -> SI = its mx_tab
; entry, CF = 0; CF = 1 there is none. Clobbers AX, CL, DX
mx_find:
    call mx_rel                     ; AX = its KB from the pool's base
    jc .o
    mov si, mx_tab
.l:
    cmp word [ss:si+MX_KB], 0
    je .n
    cmp [ss:si+MX_OFF], ax
    jne .n
    cmp [ss:si+MX_OWN], bl
    je .o                           ; (CF = 0: equal)
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .l
    stc
.o:
    ret

; mega_hiclaim2 - mega_hiclaim for CODE: CX = bytes -> CF = 0, AX = the code
; selector of a pair (mega_ldt_alloc2) over that many bytes above 1MB, and DX:BX
; = their linear base; CF = 1 nothing taken. Clobbers nothing else
mega_hiclaim2:
    push cx
    cmp word [xm_kb], 0
    je .no
    push cx
    mov ax, cx
    xor dx, dx
    mov bx, MX_KOWN                 ; (BH = 0: code outranks every cache)
    inc byte [sch_lock]             ; to the pair: the base is aimed before
    call mx_alloc                   ; any other task can compact it away
    pop cx                          ; (14.37). DX:AX = the base
    jc .unl
    dec cx
    mov bx, ax
    call mega_ldt_alloc2            ; AX = the code selector
    jnc .unl                        ; (DX:BX the base, CF = 0)
    xchg ax, bx                     ; no pair: the block goes back
    push bx
    mov bl, MX_KOWN
    call mx_free
    pop bx
    stc
.unl:
    dec byte [sch_lock]             ; (dec leaves CF)
    jmp short .out
.no:
    stc
.out:
    pop cx
    ret

; mega_hifree - AX = a selector mega_hiclaim or mega_hiclaim2 gave -> its
; block and its entry (both, for a pair) are free. Clobbers nothing.
; mega_hifreef for a FAR caller
mega_hifree:
    push ax
    push bx
    push cx
    push dx
    push ax
    call mega_selbase               ; DX:AX = the block's base
    mov bl, MX_KOWN
    call mx_free
    pop ax
    push ax
    call mega_ldt2ds                ; a pair's code half: its data too
    jc .one
    call mega_ldt_free
.one:
    pop ax
    call mega_ldt_free
    pop dx
    pop cx
    pop bx
    pop ax
    ret
mega_hifreef:
    call mega_hifree
    retf

; mega_modhif - FAR (mod_hi, `.cold`): a just-loaded module image ABOVE 1MB
; (KERN-MEGA-PLAN 13.5). AX = its conventional segment, CX = its claim's bytes
; (even, 2..65534) -> CF = 0 and AX = the CODE selector of a pair holding a
; copy of all of it, image and bss; CF = 1 and nothing taken. The caller
; re-arms the slots and frees the conventional claim. The copy runs under
; sch_lock, which is what serializes mega_lincopy's two scratch descriptors
; (xmem.inc's note). Clobbers nothing else
mega_modhif:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax
    inc byte [sch_lock]             ; to the copy's end: the base is used
    call mega_hiclaim2              ; before any other task can move it
    jc .out                         ; (14.37). AX = the code selector, DX:BX
                                    ; the base
    mov [mega_xdst], bx
    mov [mega_xdst+2], dx
    push ax
    mov ax, bp                      ; the source's base, off its
    push cx                         ; DESCRIPTOR: a selector since KERN-MEGA-
    call mega_selbase               ; MEMORY-PLAN MC7.4 (an identity segment
    pop cx                          ; reads N x 16 there all the same)
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    call KERNEL_SEG:mega_lincopyf
    pop ax
    clc
.out:
    dec byte [sch_lock]             ; (dec leaves CF)
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; mega_modsayf - FAR (mod_hi): DS:SI = a module's NUL file name, AX = the code
; selector it now runs under -> `kern_mega: above 1MB: CTRL.DRV 00FF3800` on
; the debug port, the driver line's shape (mega_drvsayf), so a field run and
; an emulator both read where every module went (KERN-MEGA-PLAN 14.47).
; Every register kept
mega_modsayf:
    push ax
    push dx
    push si
    push si
    mov si, mega_mdrv
    call mm_puts
    pop si
.n:
    mov dl, [si]
    or dl, dl
    jz .e
    xchg ax, dx
    call mm_putc
    xchg ax, dx
    inc si
    jmp short .n
.e:
    call mega_selbase               ; DX:AX = the pair's base
    xchg ax, dx
    push ax
    mov al, ' '
    call mm_putc
    pop ax
    call mm_hex4
    xchg ax, dx
    call mm_hex4
    mov al, 10
    call mm_putc
    pop si
    pop dx
    pop ax
    retf

; mega_modvfixf - FAR (mod_hiboot): AX = a module's conventional CODE ALIAS,
; DX = the pair's code selector it has just moved to -> every far pointer in
; the two tables a module writes ITSELF into (DOCK.DRV's dkv, WIN95.DRV's
; thv - mega_mods_fix's pair) that named the alias names the pair. A module
; loaded in protected mode moves before it hooks and never needs this; one
; the BOOT loaded hooked in real mode. Every register kept
mega_modvfixf:
    push cx
    push di
    pushf
    cli
    mov di, dkv
    mov cx, DKI_N
    call .t
    mov di, thv
    mov cx, THV_N
    call .t
    popf
    pop di
    pop cx
    retf
.t:
    cmp [di+2], ax
    jne .k
    mov [di+2], dx
.k:
    add di, 4
    loop .t
    ret

; mega_xmwalk - osapi_xmem_walk's body (kernel.asm): ES:DI = CX records of 6
; bytes -> AX = records written, CF = 0: the pool's live blocks, {KB offset,
; KB, owner, class}, read with IF = 0 so no record is torn. Every other
; register kept.
;
; TWO ADDITIONS FOR THE TASK MANAGER (KERN-MEGA-PLAN 14.70.2), both inert for
; a caller that asks neither. A DRIVER's block - its image (MX_KDRV) or a
; claim of its (0C0h | row) - carries the driver's DRVC_* class in the byte
; that was always 0 (mega_xmcls), so a list can say WHICH driver holds what.
; And BX = XMW_FIXED puts the fixed areas ABOVE the pool in front of the
; blocks as records of their own: the GDT, the LDT and IDT, and `.cold` with
; `.mega`, from where mega_layoutf put them at the top of extended memory
; (KERN-MEGA-MEMORY-PLAN MC2). The HMA is not among them since MC2: it is the
; pool's own first 64KB, and since MC4 ordinary pool, so what stands there is
; listed as the blocks it is. There is no padding any more to name.
;
; AND BX = XMW_LONG (KERN-MEGA-PLAN 14.92) is XMW_FIXED with every record
; EIGHT bytes: the six, then the block's whole purge TAG for a kernel cache
; (MX_AUX of an MX_FPURGE block, whose low byte the class byte never carried -
; the window slot a raise cache serves), 0 for every other record. That low
; byte is how a caller tells ITS OWN cache from anybody else's: the Task
; Manager's quiet page repainted after every drop of its own pool cache
; without it (SPEC.md 28.8.1's loop, one layer down). A caller that does not
; ask gets the six-byte records it always did
mega_xmwalk:
    push cx
    push dx
    push si
    push di
    xor dx, dx                      ; DX = records written
    cmp bx, XMW_LONG                ; (LONG records name the fixed areas too)
    je .fx
    cmp bx, XMW_FIXED
    jne .pool
.fx:                                ; THE FIXED AREAS ABOVE THE POOL, from
    cmp word [mega_tbllin+2], 0x10  ; where mega_layoutf put them (MC2): the
    jb .fxc                         ; tables only when they are above 1MB
    mov si, [mega_fixkb]            ; (mega_tlowf's are conventional), the
    mov ax, [mega_tblkb]            ; HMA not here - it is the pool's own
    or ah, XMW_GDT                  ; first block - and no padding, there
    call .rec                       ; being none. ONE record: the LDT sits on
                                    ; the GDT since MC7's step 6
.fxc:
    cmp byte [mega_codehi], 0
    je .pool
    mov si, [mega_fixkb]
    add si, [mega_tblkb]
    mov ax, (XMW_KCODE << 8) | MEGA_CODE_KB
    call .rec
.pool:
    mov si, mx_tab
    pushf
    cli
.l:
    jcxz .done
    cmp word [ss:si+MX_KB], 0
    je .n
    mov ax, [ss:si+MX_OFF]
    stosw
    mov ax, [ss:si+MX_KB]
    stosw
    mov al, [ss:si+MX_OWN]
    call mega_xmcls                 ; AH = a driver's class, else 0
    stosw
    cmp ah, MEM_PG_MIN              ; ...a purge level only for a kernel
    mov ax, 0                       ; CACHE (mega_xmcls), whose tag is MX_AUX
    jb .t                           ; (mega_hiclaim): AX = the tag, else 0
    mov ax, [ss:si+MX_AUX]
.t:
    call .tag
    inc dx
    dec cx
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .l
.done:
    popf
.out:
    xchg ax, dx
    pop di
    pop si
    pop dx
    pop cx
    clc
    ret

.rec:                               ; ES:DI = one fixed record: SI = its
    jcxz .rx                        ; KB from 1MB, AL its KB, AH its owner
    xchg ax, si                     ; (SI is spent)
    stosw
    xchg ax, si
    push ax
    mov ah, 0
    stosw
    pop ax
    mov al, ah
    mov ah, 0
    stosw
    xor ax, ax                      ; ...and no tag
    call .tag
    inc dx
    dec cx
.rx:
    ret

.tag:                               ; AX = the record's tag: written when BX
    cmp bx, XMW_LONG                ; asked for LONG records
    jne .tx
    stosw
.tx:
    ret

; mega_xmcls - AL = the owner of the mx_tab entry at SS:SI -> AH = the
; DRVC_* class of the driver that holds it, or 0: 0C0h | row names its
; drv_tab row outright; MX_KDRV is the image whose pair (drv_hi_move) is
; based where the block is, found by asking each row above 1MB for its
; DATA selector's base (mega_selbase, mx_rel). A driver loaded outside drv_tab
; (the screen saver, the Customize window's palette) answers 0. Keeps AL and
; everything else
mega_xmcls:
    push bx
    mov ah, 0
    cmp al, MX_KOWN                 ; A KERNEL CACHE says its purge level
    jne .ncache                     ; (KERN-MEGA-MEMORY-PLAN MC1), the byte
    test byte [ss:si+MX_FL], MX_FPURGE ; that is 0 for every other kernel
    jz .ncache                      ; block
    mov ah, [ss:si+MX_AUX+1]
.ncache:
    mov bl, al
    and bl, 0xE0
    cmp bl, 0xC0
    jne .img
    mov bl, al
    and bx, 0x1F
    cmp bx, DRV_MAX
    jae .o
    shl bx, 1                       ; DRVR_SIZE = 16
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ah, [bx+drv_tab+DRVR_CLASS]
    jmp short .o
.img:
    cmp al, MX_KDRV
    jne .o
    push cx
    push dx
    push ax
    mov bx, drv_tab
.r:
    mov ax, [bx+DRVR_SEG]
    test al, 4                      ; an LDT selector: a row above 1MB
    jz .rn
    call mega_selbase               ; DX:AX = its base (CL spent)
    jc .rn
    call mx_rel                     ; AX = KB from the pool's base
    jc .rn
    cmp ax, [ss:si+MX_OFF]
    je .rf
.rn:
    add bx, DRVR_SIZE
    cmp bx, drv_tab + DRV_MAX * DRVR_SIZE
    jb .r
    pop ax
    jmp short .rx
.rf:
    pop ax
    mov ah, [bx+DRVR_CLASS]
.rx:
    pop dx
    pop cx
.o:
    pop bx
    ret

; THE FIXED AREAS' WALK OWNERS (OSAPI_XMEM_WALK, BX = XMW_FIXED): kern_mega's
; records, from mega_layoutf's variables (KERN-MEGA-MEMORY-PLAN MC2) - the
; tables and the code above the pool, and the HMA's as the pool's own first
; block. (kernel.asm checks DRVR_SIZE for mega_xmcls, once driver.inc has
; defined it)
XMW_FIXED   equ 0x4658              ; MIRRORED in apps/os88api.inc
XMW_LONG    equ 0x4C58              ; ...and LONG records (KERN-MEGA-PLAN 14.92)
XMW_HMA     equ 0xF0
XMW_GDT     equ 0xF1
XMW_LDT     equ 0xF2
XMW_KCODE   equ 0xF3
XMW_PAD     equ 0xF4                ; (none since MC2: kept for the SDK)

; mega_slotof - ES = a package's segment (its DS, as the X stub stamped it):
; BL = its instance slot, CF = 0; CF = 1 it is no package's. The region being
; loaded is [ld_base]'s, whose record is [ld_rec] (the entry proc claims before
; step 9 publishes it). Clobbers nothing else
mega_slotof:
    push ax
    push cx
    push di
    mov ax, es
    mov di, [ld_rec]
    cmp ax, [ld_base]
    jne .scan
    or di, di
    jnz .slot
.scan:
    mov di, inst_tab
.l:
    cmp byte [di+I_STATE], 0
    je .n
    test byte [di+I_KIND], KIND_PKG
    jz .n
    cmp [di+I_SPTR], ax
    je .slot
.n:
    add di, I_RECSZ
    cmp di, inst_tab + INST_MAX * I_RECSZ
    jb .l
    stc
    jmp short .o
.slot:
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                       ; I_RECSZ = 32 (ld_slot's shift)
    shr ax, cl
    mov bl, al
    clc
.o:
    pop di
    pop cx
    pop ax
    ret

; mega_ownerof - ES = a caller -> BL = the pool owner its claims above 1MB
; are made under, CF = 0: 0A0h | slot for a package whose header says
; OS88_HIGHMEM, 0C0h | row for a driver whose says OS88_DRV_HICLAIM (KERN-
; MEGA-PLAN items 81, 83); CF = 1 neither. Clobbers nothing else
mega_ownerof:
    call mega_slotof
    jc .drv
    test byte [es:LD_H_CLASS], 0x40 ; OS88_HIGHMEM: read only once ES is
    jz .no                          ; known to be a package's
    or bl, 0xA0
    clc
    ret
.drv:
    push ax
    push bx
    mov bx, es
    call COLD_SEG:drv_row_ixf       ; AL = the row ES is the image of
    pop bx
    jc .dno
    test byte [es:0x0F], 0x40       ; DRV_H_DSV's OS88_DRV_HICLAIM
    jz .dno
    or al, 0xC0
    mov bl, al
    pop ax
    clc
    ret
.dno:
    pop ax
.no:
    stc
    ret

; mx_relallf - mx_relall for a FAR caller (drv_release): AL = the owner byte
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mx_relallf:
    cmp word [xm_kb], 0             ; no pool - real mode, or none above 1MB:
    je .r                           ; nothing of the owner's is there, and
    call MEGA_SEL_CODE:mx_relallm   ; `.mega` is not reachable from real mode
.r:
    retf

; THE CLAIM FAMILY IS .cold (a size pass for kern_mega's one 64KB segment):
; every entry is far and reached from .cold, and its .text helpers through
; their four-byte far doors
section .cold
; mega_claimhf - FAR (osapi_mem_claim / _hi, `.cold`): A CLAIM ABOVE 1MB for a
; package whose header says OS88_HIGHMEM (KERN-MEGA-PLAN 13.6, decision 14.8).
; in:  AX = KB, ES = the caller. out: CF = 0, DX = a DATA selector whose
;      descriptor covers exactly those AX KB of a pool block owned 0A0h | slot;
;      CF = 1 not taken here - not protected mode, not a package, no OS88_HIGHMEM
;      in its header, 0 or more than 64KB, or no room or no LDT entry - and the
;      caller goes on to the conventional heap exactly as before. The selector
;      lives in the block's MX_SEL (OSAPI_XMEM_SEL's word), so mx_free and the
;      instance's teardown (mx_relinst) give it back with the block. AX and
;      every other register kept
; -----------------------------------------------------------------------------
; mega_gatef - FAR, in REAL MODE from mega_enter (.cold is still conventional
; then, and this is reached on an 8088 too, so it is 8086 code): WHETHER THE
; SWITCH CAN BE MADE - a 286, no package task to convert, A20 open and
; proved, and room for the tables. Every refusal says so on the debug port and
; on the GLASS (a field run that stays in real mode must not look like one
; that never tried), and gives `.mega`'s rung to the heap: real mode can never
; run what is on it (KERN-MEGA-SEGMENTS-PLAN 3.3). The decoder's buffers drop
; to its bottom and the floor onto them, as mega_coldup does for both rungs.
; All of it is `.cold` and not the 64KB segment, being run once, low, before
; anything is in protected mode (KERN-MEGA-PLAN 14.45).
; out: CF = 0 go on; CF = 1 refused. Clobbers AX CX DX SI
; -----------------------------------------------------------------------------
mega_gatef:
    cmp byte [cpu_tier], CPU_286
    mov dx, mega_t86                ; (MOV keeps the flags)
    jb .refuse                      ; an 8088 or 8086: no protected mode at all
    call mega_tasks_ok              ; nothing suspended in real mode that the
    mov dx, mega_ttask              ; switch cannot convert
    jc .refuse
    call mega_a20                   ; (the tables' room is asked LAST: a claim
    mov dx, mega_ta20               ; made before a refusal would be left
    jc .refuse                      ; behind by it)
    COLDCALL mega_layoutf           ; the tables at the TOP of extended
    jc .tlow                        ; memory (KERN-MEGA-MEMORY-PLAN MC2)...
    mov ax, [mega_tbllin]
    mov dx, [mega_tbllin+2]
    call mega_tset
    jmp short .go
.tlow:
    call COLD_SEG:mega_tlowf        ; ...or 82KB of conventional memory: no
    mov dx, mega_tmem               ; minimum beyond FITTING (answer 16)
    jnc .go
.refuse:
    mov si, mega_mrm                ; "kern_mega: " and the toast's own line
    call KERNEL_SEG:mega_putsf
    mov si, dx
    call KERNEL_SEG:mega_putsf
    mov al, 10
    out 0xE9, al
    push es
    push ds
    pop es
    mov si, dx
    mov cx, 146
    call KERNEL_SEG:cw_toast_show
    pop es
    mov ax, MEGA_RAM                ; ...and `.mega`'s rung to the heap
    call COLD_SEG:mega_vbaimf
%if VGABUF_PARA
    test byte [vid_avail], VID_A_VGA
    jz .mfloor
    add ax, VGABUF_PARA
.mfloor:
%endif
    mov [mem_base], ax
    mov byte [mega_mfree], 1
    stc
.go:
    retf

; mega_tasks_ok - CF = 0 when every task but the running one is the KERNEL'S
; (no instance, not a driver's service worker). A package's worker suspended
; in real mode holds real-mode code segments in its stack - the frame's CS and
; every far return into the package beneath it - and nothing can find those to
; convert them, so the switch is refused instead. Only AUTOSTART (SPEC.md
; 26.10.5) can launch one before mega_enter. Clobbers CX, flags.
mega_tasks_ok:
    push bx
    mov bx, sch_tasks
    xor cx, cx
.t:
    cmp byte [bx+T_STATE], 0
    je .n
    cmp cl, [sch_cur]
    je .n
    cmp byte [bx+T_INST], 0xFF
    jne .no
    test byte [bx+T_FLAGS], TF_SERVICE
    jnz .no
.n:
    add bx, T_SIZE
    inc cx
    cmp cx, MAX_TASKS
    jb .t
    pop bx                          ; CF = 0 from the compare (CX < MAX_TASKS
    clc                             ; failed), said outright
    ret
.no:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; mega_a20 - open the A20 line and prove it (CF = 1 it will not open)
; Port 92h first, which every AT-class chipset since the PS/2 has; then the
; 8042's output port, which every AT has. The proof is the only thing that
; counts: 0000:0500 and FFFF:0510 are the same byte with the gate shut.
; -----------------------------------------------------------------------------
mega_a20:
    call mega_a20t
    jnc .ret                        ; already open: do not poke a working line
    pushf                           ; --- the FAST gate, port 92h -----------
    cli
    in al, 0x92
    test al, 2                      ; set (an absent port reads FFh) and the
    jnz .nofast                     ; line still shut: not the gate here
    or al, 2
    and al, 0xFE                    ; bit 0 is a RESET
    out 0x92, al
.nofast:
    popf
    call mega_a20s
    jnc .ret
    pushf                           ; --- the 8042, in ONE window: between D1h
    cli                             ; and DFh the next byte written to 60h is
    call mega_kbw                   ; its output port, and a keyboard ISR's
    jnz .kbx                        ; own traffic there can reset the CPU
    mov al, 0xD1
    out 0x64, al
    call mega_kbw
    jnz .kbx
    mov al, 0xDF                    ; A20 on, reset line high
    out 0x60, al
    call mega_kbw
.kbx:
    popf
    call mega_a20s                  ; ...and the controller acknowledges long
.ret:                               ; before the line moves, hence the settle
    ret
mega_a20s:                          ; SETTLE: up to 512 probes for a gate to
    mov cx, 512                     ; take (XMEM.DRV's XM_A20_SETTLE, whose
.p:                                 ; sequence this is - the first version
    call mega_a20t                  ; here probed once, and on the Victory 286
    jnc .o                          ; that read SHUT: "real mode: A20 will
    loop .p                         ; not open")
.o:
    ret
mega_kbw:
    xor cx, cx
.w:
    in al, 0x64
    test al, 2
    loopnz .w
    ret
mega_a20t:                          ; CF = 1 the gate is SHUT. Complement
    push ds                         ; the word at FFFF:0510 and see whether
    push es                         ; 0000:0500 moved, then complement it back
    push bx                         ; (whichever word it landed on), inside
    pushf                           ; one cli: an ISR touching low memory in
    cli                             ; between would make the probe lie
    xor ax, ax
    mov es, ax
    dec ax
    mov ds, ax
    mov bx, [es:0x0500]
    not word [0x0510]
    mov ax, [es:0x0500]
    not word [0x0510]
    popf
    cmp ax, bx                      ; equal: the flip went a megabyte up
    pop bx
    pop es
    pop ds
    je .open
    stc
    ret
.open:
    clc
    ret

; -----------------------------------------------------------------------------
; mega_tlowf - FAR, in REAL MODE from mega_enter (.cold is still conventional
; then): kern_mega's three tables in CONVENTIONAL memory, for a machine whose
; extended memory cannot hold them (KERN-MEGA-PLAN 14.1 answer 16, 14.17).
; [mega_tblkb] claimed top-down and pinned (MEM_K_MTBL), laid out exactly as
; above 1MB - the GDT at its base, the LDT on it, the IDT after - and every base
; the switch reads (the bootstrap's window, GDTR, IDTR, the LDT's descriptor
; and its alias, the IDT's alias) aimed at it. No pool and `.cold` stays low:
; mega_xminit and mega_coldup already refuse a machine this small.
; out: CF = 0 placed; CF = 1 the heap had no room for it, nothing changed
; -----------------------------------------------------------------------------
mega_tlowf:
    push ax
    push bx
    push dx
    push bp
    mov ax, [mega_tblkb]            ; (mega_layoutf sized it, refusing)
    mov bx, MEM_K_MTBL
    mov bp, mem_claim_hi_x          ; TOP-DOWN, pinned
    call COLD_SEG:wm_cbd            ; DX = the block
    jc .out
    mov ax, dx                      ; DX:AX = its linear base
    shl ax, 4
    shr dx, 12
    call mega_tset
    clc
.out:
    pop bp
    pop dx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; mega_tset - DX:AX = where the tables' block goes ([mega_tblkb]: the GDT at
; DX:AX and the LDT on top of it, the IDT [mega_ldtend] in) -> every word the
; switch and the kernel read a table's place from aimed there: the
; bootstrap's window, GDTR, IDTR, [mega_tbllin], [mega_ldtlin], the LDT's and
; the IDT's descriptors and the DOS park's IDT line (mega_dsidt). Real mode
; (mega_gatef, before the switch). Clobbers nothing
; -----------------------------------------------------------------------------
mega_tset:
    push ax
    push dx
    mov [mega_tbllin], ax
    mov [mega_tbllin+2], dx
    mov [mega_bwin+2], ax           ; the GDT
    mov [mega_bwin+4], dl
    mov [mega_gdtr+2], ax
    mov [mega_gdtr+4], dx
    mov [mega_ldtlin], ax           ; the LDT: the same base
    mov [mega_ldtlin+2], dx
    mov [mega_ldtd+2], ax
    mov [mega_ldtd+4], dl
    push ax
    mov ax, [mega_ldtend]
    mov [ss:mega_ldte], ax          ; ...the scans' end, where any DS reads it
    sub ax, 8                       ; - HERE and not in mega_layoutf, whose
    mov [ss:mega_ldtl], ax          ; boot-blob caller (mega_wrprobe) runs on
    add ax, 7                       ; the loader's stack, not LOW_SEG
    mov [mega_ldtd], ax             ; ...its limit, the allocator's last byte
    add ax, 0x101
    mov [mega_dsidt], ax            ; the park's line of gates 20h-21h
    pop ax
    add ax, [mega_ldtend]           ; the IDT, past the LDT's last entry
    adc dx, 0
    mov [mega_idtr+2], ax
    mov [mega_idtr+4], dx
    mov [mega_idtd+2], ax
    mov [mega_idtd+4], dl
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mega_layoutf - FAR, real or protected mode: WHERE kern_mega's fixed area
; goes above 1MB (KERN-MEGA-MEMORY-PLAN MC2) - from the TOP of extended
; memory down: `.cold`, `.mega` and the park's scratch at the very top
; (MEGA_CODE_KB), the tables' block (MEGA_TBL_KB) under them, and the pool
; from 1MB up to there, the HMA its first 64KB. So free memory is one run that
; ends at 1MB and the HMA is the last of it a claim reaches.
;
; A PURE FUNCTION OF [mega_extkb]: every boot of one machine computes the same
; places, which is what lets the NEXT boot read a park's anchor in GDT entry 0
; (mega_wrprobe, before anything is switched) and a resume find the tables
; where its image had them.
;
; out: [mega_codehi] 1 when the code fits too (then [mega_coldlin] its base);
;      CF = 0 the tables fit: [mega_tbllin] their base, [mega_fixkb] the KB
;      from 1MB to it, the pool's extent; CF = 1 they do not - mega_tlowf's
;      case - and [mega_fixkb] is all of extended memory. Clobbers nothing
; -----------------------------------------------------------------------------
MEGA_CODE_KB equ (COLD_PARA * 16 + MEGA_SCR_OFF + MPS_SIZE + 1023) / 1024
mega_layoutf:
    push ax
    push dx
    mov byte [mega_codehi], 0
    mov ax, [mega_extkb]
    cmp ax, 15360                   ; the 8237 and a 286 both stop at 16MB
    jbe .e
    mov ax, 15360
.e:
    mov [mega_fixkb], ax
    push ax                         ; THE TABLES' SIZE, off the same KB: the
    shr ax, 4                       ; GDT, MEGA_LDT_BASE allocator entries and
    add ax, MEGA_LDT_FREE0 + MEGA_LDT_BASE + 7 ; one per 16KB of extended
    and ax, ~7                      ; memory, in whole 64-byte lines (the
    mov [mega_ldtn], ax             ; DOS park sums lines), then the IDT
    shl ax, 3
    mov [mega_ldtend], ax
    add ax, MEGA_IDT_SIZE + 1023
    shr ax, 10
    mov [mega_tblkb], ax
    pop ax
    mov dx, [mega_tblkb]
    add dx, MEGA_HMA_KB + MEGA_CODE_KB
    cmp ax, dx
    jb .nocode
    sub ax, MEGA_CODE_KB            ; the code at the very top...
    mov byte [mega_codehi], 1
    push ax
    call .lin
    mov [mega_coldlin], ax
    mov [mega_coldlin+2], dx
    pop ax
    jmp short .tbl
.nocode:
    mov dx, [mega_tblkb]
    add dx, MEGA_HMA_KB
    cmp ax, dx
    jb .no
.tbl:
    sub ax, [mega_tblkb]            ; ...the tables under it...
    mov [mega_fixkb], ax            ; ...and the pool under them
    call .lin
    mov [mega_tbllin], ax
    mov [mega_tbllin+2], dx
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop ax
    retf
.lin:                               ; AX = KB from 1MB -> DX:AX = its linear
    push cx
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    add dx, 0x10
    pop cx
    ret

; -----------------------------------------------------------------------------
; mx_compactf - FAR (OSAPI_MEM_REGROW's .hpack): PACK THE POOL'S MOVABLE BLOCKS
; DOWN (KERN-MEGA-PLAN 14.1 answer 12, 14.15), so the free KB they were
; splitting become one run. The blocks are taken in ascending order of where
; they are; a cursor follows them, a PINNED block raises it past itself, and
; a MOVABLE one slides down to it - always into free space, every block below
; having been placed already. What moves is what nothing names by ADDRESS,
; OF ANY SIZE (KERN-MEGA-PLAN 14.37) - which since KERN-MEGA-MEMORY-PLAN MC3
; is every block but the HMA and an MX_FPIN one (mx_canmove): OSAPI_XMEM_ALLOC
; blocks are named by a token the cells translate, and the kernel's own by
; selector. mx_alloc packs UP (mx_packupf) since MC3; this is .hpack's, which
; wants the free KB directly above one claim. A block MOVES WITH NO RELOCATION PROC: its selectors are
; stable and only their descriptors' bases change - every present LDT
; descriptor whose base is inside it is rebased - and a segment register that
; holds one picks the new base up at its next load. Every holder outside this
; call is a pushed register (a suspended task's frame, a cell's push ds, an
; interrupted ISR's) and reloads on its pop; inside it, ES is reloaded at the
; end. It runs inside mx_alloc, under [sch_lock], so no OTHER TASK runs
; until it is done; an ISR does, and sees no half-moved 64KB: mx_cpmove.
; DS = KDATA. Clobbers nothing
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mx_compactf:
    pusha
    push es
    xor bp, bp                      ; BP = the cursor, KB from the pool base
    xor bx, bx                      ; BX = the least offset still to visit
.next:
    mov si, mx_tab
    xor di, di                      ; DI = the next block, CX its offset
    mov cx, 0xFFFF
.scan:
    cmp word [ss:si+MX_KB], 0
    je .sn
    mov ax, [ss:si+MX_OFF]
    cmp ax, bx
    jb .sn
    cmp ax, cx
    jae .sn
    mov cx, ax
    mov di, si
.sn:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .scan
    or di, di
    jz .done
    mov bx, cx                      ; the next visit is past this block
    inc bx
    call mx_canmove                 ; OF ANY SIZE (14.37)
    je .mov
.pin:
    mov ax, cx
    add ax, [ss:di+MX_KB]
    cmp ax, bp
    jbe .next
    mov bp, ax
    jmp short .next
.mov:
    cmp cx, bp
    je .stay
    push bx
    call mx_cpmove                  ; DI's block from CX to BP
    pop bx
.stay:
    add bp, [ss:di+MX_KB]
    jmp short .next
.done:
    pop es                          ; reloaded: a moved block's selector in ES
    popa                            ; reads the NEW base from here on
    retf

; mx_cpmove - DI = an mx_tab entry, CX = where it is (KB from the pool base),
; BP = where it goes, below or above it and overlapping it or not: a rise
; takes the pieces and their chunks from the TOP down and copies each from its
; end (KERN-MEGA-PLAN 14.49), so either way a copy only ever overwrites bytes
; of pieces already moved, or of the one it is. Its bytes are copied and every descriptor over them rebased, and
; MX_OFF = BP after (KERN-MEGA-PLAN 14.15, 14.37). A PIECE AT A TIME, 64KB
; or less where it can be: each piece's copy and the rebase of every LDT
; descriptor whose base is in it are one step with interrupts off, and
; interrupts are ON between pieces - so a block of megabytes is never seconds
; of a deaf machine, and an ISR reading or writing it through any of its
; selectors finds each piece whole, either still at the old place (where
; nothing has written yet: a copy only ever overwrites bytes of pieces already
; moved, or of this one) or at the new. mx_pcut cuts the pieces where no
; descriptor runs across (KERN-MEGA-PLAN 14.48): every 64KB for a run, a pair
; or a claim's selector, a part at a time for a re-homed carve, and as few
; pieces as it must past 64KB when descriptors overlap - a block of 64KB or
; less is one step, as it always was. In 512-byte units, the parts' own
; grid (SPEC.md 20.12.2), so two parts sharing a KB still cut between them.
; THE ONE BLOCK WHOSE CUTS ARE ALL CROSSED is an MX_FH32 claim (the Video
; Player's ring, 14.41), and a package's is cut every 64KB anyway, with its
; windows half-moved between pieces and FSXF_RATE's hook - the only code that
; could read them - held off until the last piece (mx_cpflags says why that is
; all of it). mega_slidef's GDT scratch is
; rebased at the end: only a TASK loads it, and no other task runs under the
; [sch_lock] mx_alloc holds. Clobbers AX BX CX DX SI ES
MX_CPRISE   equ 1                   ; mx_cpmove's [bp+4]: the block goes UP -
                                    ; pieces and chunks from the top down,
                                    ; each copied from its end (KERN-MEGA-PLAN
                                    ; 14.49), so a destination above an
                                    ; overlapping source reads nothing it has
                                    ; written
MX_CPHOLD   equ 2                   ; ...a package's MX_FH32 claim, whose odd
                                    ; windows cross every cut: cut every 64KB
                                    ; anyway, with FSXF_RATE's hook held off
                                    ; for the whole move (mx_cpflags)
mx_cpmove:
    push di                         ; [bp+16] the entry
    push bp                         ; [bp+14] where it goes
    mov ax, cx
    sub ax, bp                      ; the drop in KB: negative is a rise
    mov dx, ax
    shl ax, 10
    sar dx, 6                       ; DX:AX = the drop in bytes, signed
    push dx                         ; [bp+12]
    push ax                         ; [bp+10]
    mov ax, cx
    call mx_kblin
    push dx                         ; [bp+8] the old base, 24 bits
    push ax                         ; [bp+6]
    call mx_cpflags
    push ax                         ; [bp+4] MX_CPRISE | MX_CPHOLD
    mov ax, [ss:di+MX_KB]
    shl ax, 1
    push ax                         ; [bp+2] its size in 512-BYTE UNITS (a
                                    ; part of a re-homed carve starts on
                                    ; one, SPEC.md 20.12.2; 14.48)
    push ax                         ; [bp+0] this piece's units (mx_pcut's)
    mov bp, sp
    test byte [bp+4], MX_CPHOLD
    jz .h0
    inc byte [sch_rbusy]            ; the hook HELD: sch_rhook skips while
.h0:                                ; this is up, its periods owed in
                                    ; [sch_rpend] for the call after
    xor bx, bx                      ; BX = the piece's first unit in the block
    test byte [bp+4], MX_CPRISE
    jz .piece
    mov bx, [bp+2]                  ; (a rise: BX = the first piece's END)
.piece:
    test byte [bp+4], MX_CPRISE
    jnz .rcut
    call mx_pcut                    ; [bp+0] = where no descriptor is cut
    jmp short .cut
.rcut:
    call mx_pcutr                   ; ...the same, cut below its end
    sub bx, [bp+0]                  ; BX = its first unit
.cut:
%ifdef MEGA_TRAPLOG
    inc byte [bp+5]                 ; (MEGATRAP=1: the pieces, for the M line)
%endif
    pushf
    cli
    mov cx, [bp+0]                  ; CX = this piece's units still to copy
.chunk:                             ; 32KB at a time
    mov ax, cx
    cmp ax, 64
    jbe .last
    mov ax, 64                      ; AX = this chunk's units
.last:
    mov si, bx
    test byte [bp+4], MX_CPRISE
    jnz .rise
    add si, [bp+0]                  ; upward from the piece's start: a
    sub si, cx                      ; destination below reads nothing it
    jmp short .at                   ; has written
.rise:
    add si, cx                      ; a rise: downward from its top
    sub si, ax
.at:                                ; SI = the chunk's first unit
    push ax
    mov ax, si
    mov dx, si
    shl ax, 9
    shr dx, 7                       ; DX:AX = its offset in the block
    add ax, [bp+6]
    adc dx, [bp+8]
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    sub ax, [bp+10]                 ; ...and less the drop, where it goes
    sbb dx, [bp+12]
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    pop ax
    push ax
    push cx
    push bx
    push bp
    mov cx, ax
    shl cx, 9                       ; bytes (32KB at most: no 64KB wrap)
    test byte [bp+4], MX_CPRISE
    jnz .back
    call mx_lcopy
    jmp short .cd
.back:
    call mx_lcopyb
.cd:
    pop bp
    pop bx
    pop cx
    pop ax
    sub cx, ax
    jnz .chunk
    mov ax, MEGA_SEL_LDTD           ; --- this piece's descriptors -------
    mov es, ax
    mov si, MEGA_LDT_FREE0 * 8
    mov cx, [ss:mega_ldte]
    mov dx, bx
    add dx, [bp+0]                  ; (never past its end, mx_pcut's rule:
    call mx_desc                    ; the next block's descriptors start there)
    popf                            ; ...and an interrupt may come in
    test byte [bp+4], MX_CPRISE
    jnz .rnext
    add bx, [bp+0]
    cmp bx, [bp+2]
    jb .piece
    jmp short .moved
.rnext:
    or bx, bx                       ; (a rise: the next piece ends where
    jnz .piece                      ; this one began)
.moved:
%ifdef MEGA_TRAPLOG
    call mx_cplog
%endif
    test byte [bp+4], MX_CPHOLD
    jz .h1
    dec byte [sch_rbusy]            ; every window whole again: the hook
.h1:                                ; runs at the next period, owed them all
    mov ax, MEGA_SEL_WIN            ; --- mega_slidef's scratch, the GDT's --
    mov es, ax
    mov si, MEGA_SEL_SLD
    lea cx, [si+8]
    xor bx, bx
    mov dx, [bp+2]
    call mx_desc
    mov ax, [bp+14]
    mov di, [bp+16]
    mov [ss:di+MX_OFF], ax          ; ...and the table says where it is now
    add sp, 14
    pop bp
    pop di
    ret

%ifdef MEGA_TRAPLOG
; mx_cplog - MEGATRAP=1: one E9 line per move, 'M kkkk pp ff' - the block's
; KB, the interrupts-off steps it took and mx_cpmove's flags (1 a rise, 2 the
; hook held), KERN-MEGA-PLAN 14.49. Clobbers nothing
mx_cplog:
    push ax
    mov al, 'M'
    call mm_putc
    mov al, ' '
    call mm_putc
    mov ax, [bp+2]
    shr ax, 1
    call mm_hex4
    mov al, ' '
    call mm_putc
    mov al, [bp+5]
    call mm_hex2
    mov al, ' '
    call mm_putc
    mov al, [bp+4]
    call mm_hex2
    mov al, 10
    call mm_putc
    pop ax
    ret
%endif

; mx_cpflags - for mx_cpmove: DI = the entry, CX = where it is, BP = where it
; goes -> AX = MX_CPRISE when BP is above CX, | MX_CPHOLD for a package's
; MX_FH32 claim (KERN-MEGA-PLAN 14.49). Its windows are 64KB every 32KB, so
; every cut is crossed and mx_pcut alone would make the whole claim ONE
; interrupts-off step - ~80 ms for the Video Player's 512KB ring on a 12 MHz
; 286. No TASK runs during a move (mx_alloc's [sch_lock]), and the only code
; that reads a package's claim from an ISR is the package's own FSXF_RATE
; hook, which mx_cpmove holds off, and its sample ISR (SPEC.md 34.11), which
; it cannot: with one open the claim moves in one step, as it did. A
; driver's (0C0h | row) FH32 claim does too - its own ISRs could read it.
; Clobbers nothing else
mx_cpflags:
    xor ax, ax
    cmp bp, cx
    jbe .h
    mov al, MX_CPRISE
.h:
    test byte [ss:di+MX_FL], MX_FH32 | MX_FH16
    jz .run
    cmp byte [ss:di+MX_OWN], 0xA0
    jb .o
    cmp byte [ss:di+MX_OWN], 0xC0
    jae .o
    jmp short .hold
.run:                               ; ...and a package's OSAPI_XMEM_RUN block,
    test byte [ss:di+MX_FL], MX_FWIN ; (or one a BOUND window reads through)
    jnz .runh
    test byte [ss:di+MX_SEL], 1     ; the same shape - a 64KB window every
    jz .o                           ; 32KB - since such a block moves too
.runh:
    cmp byte [ss:di+MX_OWN], 0x80   ; (KERN-MEGA-MEMORY-PLAN MC3)
    jae .o
.hold:
    cmp word [spk_seg], 0
    jne .o
    or al, MX_CPHOLD
.o:
    ret

; mx_lcopy / mx_lcopyb - mega_lincopy's body (KERN-MEGA-PLAN 14.49): CX bytes
; (even, 2..65534) from linear [mega_xsrc] to linear [mega_xdst], DS = KDATA;
; mx_lcopyb copies from the END down, for a destination ABOVE an overlapping
; source. XA, XB and the two dwords are ONE pair for the machine, so the
; caller holds [sch_lock] or IF = 0 from its first store to [mega_xsrc] until
; this returns - MEGATRAP=1 checks it (mx_lcchk). Clobbers AX BX CX DX SI DI;
; DS and ES kept
mx_lcopyf:
    call mx_lcopy
    retf

; mega_bparaf - FAR (mem_bpara in protected mode, KERN-MEGA-MEMORY-PLAN
; MC7.4): AX paragraphs (1..64, a compactor chunk) from paragraph SI to
; paragraph DI, LINEARLY - the conventional claims' compactor named both by
; segment, which is the identity GDT step 6 cuts. DX != 0 copies from the
; last word, for a move up that overlaps itself. IF = 0 for the chunk, which
; is mega_lincopy's rule for its scratch pair. Every register kept
mega_bparaf:
    pusha
    pushf
    cli
    mov bp, dx                      ; BP = the direction
    mov cx, ax
    shl cx, 4                       ; CX = bytes
    mov ax, si
    mov dx, ax
    shl ax, 4
    shr dx, 12
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    mov ax, di
    mov dx, ax
    shl ax, 4
    shr dx, 12
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    or bp, bp
    jz .f
    call mx_lcopyb
    jmp short .o
.f:
    call mx_lcopy
.o:
    popf
    popa
    retf
mx_lcopyb:
    stc
    jmp short mx_lcopy.go
mx_lcopy:
    clc
.go:
    push bp
    sbb bp, bp                      ; BP = -1 from the end, 0 from the start
%ifdef MEGA_TRAPLOG
    call mx_lcchk
%endif
    mov bx, MEGA_SEL_XA
    mov ax, [mega_xsrc]
    mov dx, [mega_xsrc+2]
    call KERNEL_SEG:mega_xdescf
    mov bx, MEGA_SEL_XB
    mov ax, [mega_xdst]
    mov dx, [mega_xdst+2]
    call KERNEL_SEG:mega_xdescf
    push ds
    push es
    mov ax, MEGA_SEL_XA
    mov ds, ax
    mov ax, MEGA_SEL_XB
    mov es, ax
    shr cx, 1
    xor si, si
    or bp, bp
    jz .f
    mov si, cx                      ; the last word
    dec si
    shl si, 1
    std
.f:
    mov di, si
    rep movsw
    cld
    pop es
    pop ds
    pop bp
    ret

%ifdef MEGA_TRAPLOG
; mx_lcchk - MEGATRAP=1: mega_lincopy's rule ASSERTED (KERN-MEGA-PLAN 14.49).
; IF = 0 or [sch_lock] held, else one E9 line - 'L', then the words over
; mx_lcopy's frame, for a far caller its CS:IP and the word above - and the
; copy goes on. Clobbers nothing
mx_lcchk:
    pushf
    push ax
    push bp
    mov bp, sp                      ; +4 FLAGS +6 mx_lcopy +8 its BP +10 ...
    test byte [bp+5], 2             ; IF
    jz .ok
    cmp byte [sch_lock], 0
    jne .ok
    mov al, 'L'
    call mm_putc
    mov al, ' '
    call mm_putc
    mov ax, [bp+14]
    call mm_hex4
    mov al, ':'
    call mm_putc
    mov ax, [bp+12]
    call mm_hex4
    mov al, ' '
    call mm_putc
    mov ax, [bp+16]
    call mm_hex4
    mov al, 10
    call mm_putc
.ok:
    pop bp
    pop ax
    popf
    ret
%endif

; mx_canmove - DI = an mx_tab entry: ZF = 1 the compactor may move it, OF ANY
; SIZE (14.37). EVERY BLOCK MOVES (KERN-MEGA-MEMORY-PLAN MC3, D2) but one
; marked MX_FPIN (the HMA's own record went with MC4): a program's region, claims and
; OSAPI_XMEM_ALLOC blocks (named by token), a driver's image, claims and
; store, and the kernel's caches and modules - all named by SELECTOR, and a
; module running is no exception, a far return reloading its CS from the
; rebased descriptor. Clobbers AL
mx_canmove:
    test byte [ss:di+MX_FL], MX_FPIN
    ret                             ; (ZF = 1: not pinned)

; mx_packupf - FAR (OSAPI_MEM_REGROW's last try, KERN-MEGA-PLAN 14.49): AX =
; a floor, KB from the pool base. Every MOVABLE block at or above it slides
; UP, packed against the pool's top or the pinned block above it - the
; DESCENDING half of a compaction, which kern_big's has (SPEC.md 66.4.1) - so
; the free KB between the floor and the first block above it are every free
; KB up to the first pinned block. The blocks are taken in DESCENDING order
; of where they are; a ceiling follows them, a pinned block lowers it to
; itself, and a movable one rises to end at it (mx_cpmove's rise, which
; copies from the top down). DS = KDATA, under [sch_lock]. Clobbers nothing
mx_packupf:
    pusha
    push es
    mov bx, ax                      ; BX = the floor
    mov bp, [xm_kb]                 ; BP = the ceiling
    mov dx, 0xFFFF                  ; DX = visit only blocks below this
.next:
    mov si, mx_tab
    xor di, di                      ; DI = the next block, CX its offset
.scan:
    cmp word [ss:si+MX_KB], 0
    je .sn
    mov ax, [ss:si+MX_OFF]
    cmp ax, bx
    jb .sn
    cmp ax, dx
    jae .sn
    or di, di
    jz .tk
    cmp ax, cx
    jbe .sn
.tk:
    mov cx, ax
    mov di, si
.sn:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .scan
    or di, di
    jz .done
    mov dx, cx                      ; the next visit is below this block
    call mx_canmove
    jne .ceil
    mov ax, bp
    sub ax, [ss:di+MX_KB]           ; AX = where it ends at the ceiling
    cmp ax, cx
    jbe .ceil                       ; (already there)
    push bx
    push dx
    push bp
    mov bp, ax
    call mx_cpmove                  ; DI's block from CX up to BP
    pop bp
    pop dx
    pop bx
    mov cx, [ss:di+MX_OFF]
.ceil:
    mov bp, cx                      ; the ceiling is its start now
    jmp short .next
.done:
    pop es                          ; reloaded: a moved block's selector in ES
    popa                            ; reads the NEW base from here on
    retf

; mx_desc - for mx_cpmove's frame at [bp]: every present descriptor at
; ES:SI, ES:SI+8, ... below ES:CX whose base lies between unit BX (included)
; and unit DX (not) of the block's OLD place, rebased by the drop. Clobbers
; SI, flags
mx_desc:
    push di
.l:
    cmp byte [es:si+5], 0
    je .n
    push ax
    push cx
    call mx_dkb                     ; AX = its first unit in the block
    jc .x
    cmp ax, bx
    jb .x
    cmp ax, dx
    jae .x
    mov ax, si
    call mega_iswin                 ; a window is its owner's (14.80)
    jnc .x
    mov di, [bp+10]
    mov cl, [bp+12]
    sub [es:si+2], di
    sbb [es:si+4], cl
.x:
    pop cx
    pop ax
.n:
    add si, 8
    cmp si, cx
    jb .l
    pop di
    ret

; mx_dkb - for mx_cpmove's frame: the descriptor at ES:SI -> CF = 0, AX = the
; 512-byte unit of the block its base is in and CL:DI its byte offset from
; the block's OLD base; CF = 1 it is below the block. Clobbers nothing else
mx_dkb:
    mov di, [es:si+2]
    mov cl, [es:si+4]               ; CL:DI = the base
    sub di, [bp+6]
    sbb cl, [bp+8]                  ; ...less the block's: below it borrows
    jnc mx_dunit
    ret
mx_dunit:                           ; (CL:DI an offset -> AX its unit)
    mov al, cl
    xor ah, ah
    shl ax, 7
    push di
    shr di, 9
    or ax, di                       ; AX = its unit in the block, CF = 0
    pop di
    ret

; mx_pcut - for mx_cpmove's frame: BX = a piece's first unit in the block ->
; [bp+0] = the piece's units, cut so that NO LDT descriptor whose base is in
; the block runs across the cut (KERN-MEGA-PLAN 14.48). The cut is the
; largest at 64KB or under where none crosses; failing that, the least past
; 64KB - a descriptor is moved with ONE piece, its copy and its rebase in one
; interrupts-off step, so an ISR never reads it half-moved. A block whose
; selectors each lie in one 64KB tile (a run, a pair, a claim) is cut every
; 64KB as before; a re-homed carve (PIXELSTEIN's pair and its parts'
; selectors, each as long as its part, every part on a 512-byte boundary)
; a part at a time. A block of 64KB or less is one piece and asks nothing.
; Interrupts are ON while it scans: no other task runs under mx_alloc's
; [sch_lock], and no ISR makes a descriptor. Clobbers AX CX DX SI DI ES
mx_pcut:
    lea dx, [bx+128]                ; DX = the cut: 64KB on
    cmp dx, [bp+2]
    jae .end                        ; the block's end is never crossed
    test byte [bp+4], MX_CPHOLD     ; an FH32 claim's windows cross every
    jnz .set                        ; cut, and nothing reads them (14.49)
    mov ax, MEGA_SEL_LDTD
    mov es, ax
.dn:                                ; DOWN to a descriptor's start
    call mx_xcut
    jnc .set
    mov dx, ax
    cmp dx, bx
    ja .dn
    lea dx, [bx+128]                ; ...none within 64KB: UP to an end
.up:
    call mx_xcut
    jnc .set
    mov dx, cx
    cmp dx, [bp+2]
    jb .up
.end:
    mov dx, [bp+2]
.set:
    sub dx, bx
    mov [bp+0], dx
    ret

; mx_pcutr - mx_pcut for a RISE (KERN-MEGA-PLAN 14.49), which mx_cpmove takes
; from the top down: BX = a piece's END unit -> [bp+0] = its units, cut where
; no LDT descriptor whose base is in the block runs across - the largest at
; 64KB or under, failing that the least past it. Clobbers AX CX DX SI DI ES
mx_pcutr:
    mov dx, bx
    sub dx, 128                     ; DX = the cut: 64KB below
    jbe .zero                       ; (the block's start is never crossed)
    test byte [bp+4], MX_CPHOLD
    jnz .set
    mov ax, MEGA_SEL_LDTD
    mov es, ax
.up:                                ; UP to a descriptor's end
    call mx_xcut
    jnc .set
    mov dx, cx
    cmp dx, bx
    jb .up
    mov dx, bx                      ; ...none within 64KB: DOWN to a start
    sub dx, 128
.dn:
    call mx_xcut
    jnc .set
    mov dx, ax
    or dx, dx
    jnz .dn
.zero:
    xor dx, dx
.set:
    mov ax, bx
    sub ax, dx
    mov [bp+0], ax
    ret

; mx_xcut - for mx_pcut: CF = 1 when an LDT descriptor whose base is in the
; block starts before unit DX and ends after it, AX = its first unit and CX
; one past its last; CF = 0 none does. ES = MEGA_SEL_LDTD. Clobbers AX CX SI
; DI
mx_xcut:
    mov si, MEGA_LDT_FREE0 * 8
.l:
    cmp byte [es:si+5], 0
    je .n
    call mx_dkb                     ; AX = its first unit, CL:DI its offset
    jc .n
    cmp ax, dx
    jae .n                          ; starts at or past the cut
    add di, [es:si]
    adc cl, 0                       ; CL:DI = its last byte
    push ax
    call mx_dunit
    inc ax
    xchg ax, cx                     ; CX = one past its last unit
    pop ax
    cmp cx, dx
    ja .x                           ; ...past the cut: it crosses
.n:
    add si, 8
    cmp si, [ss:mega_ldte]
    jb .l
    clc
    ret
.x:
    stc
    ret

; mx_kblin - AX = KB from the pool base -> DX:AX = the linear address
mx_kblin:
    mov dx, ax
    shl ax, 10
    shr dx, 6
    add dx, MEGA_XPOOL >> 16
    ret

section .cold                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_claimhf:
    push ax
    push bx
    push cx
    push si
    cmp byte [mega_on], 0
    je .no
    or ax, ax
    jz .no
    cmp ax, 64
    ja .no
    call MEGA_SEL_CODE:mega_ownerf               ; BL = the owner: 0A0h | slot for an
    jc .no                          ; OS88_HIGHMEM package, 0C0h | row for an
                                    ; OS88_DRV_HICLAIM driver
    mov si, ax                      ; SI = KB
    mov dx, ax
    shl ax, 10
    shr dx, 6                       ; DX:AX = bytes
    inc byte [sch_lock]             ; the base is aimed before any other
    call MEGA_SEL_CODE:mx_allocf    ; task can compact it away (14.37)
    jc .nol                         ; DX:AX = the block's base
    mov cx, si
    shl cx, 10                      ; CX = bytes: 64KB is 0, mega_xmsel's
    push dx                         ; 65,536
    push ax
    call MEGA_SEL_CODE:mega_xmself                 ; AX = the selector, in the block's MX_SEL
    pop cx
    pop dx
    jnc .got
    xchg ax, cx                     ; no entry: the block goes back
    call MEGA_SEL_CODE:mx_freef
.nol:
    dec byte [sch_lock]
.no:
    pop si
    pop cx
    pop bx
    pop ax
    stc
    retf
.got:
    dec byte [sch_lock]
    mov dx, ax
    pop si
    pop cx
    pop bx
    pop ax
    clc
    retf

; mega_claimfreef - FAR (osapi_mem_free, `.cold`): DX = a selector
; mega_claimhf gave, ES = the caller -> CF = 0 its block and selector are
; free; CF = 1 it is no claim of the caller's. Every register kept
mega_claimfreef:
    push ax
    push bx
    push si
    call MEGA_SEL_CODE:mega_ownerf
    jc .no
    mov si, mx_tab
.l:
    cmp word [ss:si+MX_KB], 0
    je .n
    cmp [ss:si+MX_SEL], dx
    jne .n
    cmp [ss:si+MX_OWN], bl
    jne .n
    xor ax, ax                      ; A BRIDGE's conventional piece, for
    test byte [ss:si+MX_FL], MX_FBRIDGE ; mmf_osapi_mem_free to free after
    jz .nb                          ; this one (KERN-MEGA-MEMORY-PLAN MC5)
    mov ax, [ss:si+MX_AUX]
.nb:
    mov [mega_bfpart], ax
    push dx
    mov ax, [ss:si+MX_OFF]             ; its base: the pool's plus KB x 1024
    mov dx, ax
    shl ax, 10
    shr dx, 6
    add dx, MEGA_XPOOL >> 16
    call MEGA_SEL_CODE:mx_freef                    ; ...and mx_unsel takes the selector
    pop dx
    jmp short .o
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .l
.no:
    stc
.o:
    pop si
    pop bx
    pop ax
    retf

; mega_claimfind - DX = a selector, ES = the caller: SI = its claim's mx_tab
; entry and BL = its owner (0A0h | slot), CF = 0; CF = 1 it is no claim of
; the caller's. Clobbers nothing else
mega_claimfind:
    call MEGA_SEL_CODE:mega_ownerf
    jc mega_selfind.o
; mega_selfind - the same for an owner already in BL (MX_KOWN: the kernel's
; own blocks, mega_kregrowf). Clobbers nothing else
mega_selfind:
    mov si, mx_tab
.l:
    cmp word [ss:si+MX_KB], 0
    je .n
    cmp [ss:si+MX_SEL], dx
    jne .n
    cmp [ss:si+MX_OWN], bl
    je .o                           ; (CF = 0: the compare was equal)
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .l
    stc
.o:
    ret

; mega_claimkbf - FAR (osapi_mem_regrow, `.cold`): DX = a selector, ES = the
; caller -> AX = its claim's KB, CF = 0; CF = 1 it is no claim of the caller's.
; Every other register kept
mega_claimkbf:
    push bx
    push si
    call mega_claimfind
    jc .o
    test byte [ss:si+MX_FL], MX_FHUGE  ; a run (14.37): the pool's refusal is
    jnz .h                          ; final - never moved to conventional
    mov ax, [ss:si+MX_KB]              ; memory, whose step is not 8
    jmp short .o                    ; (CF = 0: the test)
.h:
    stc
.o:
    pop si
    pop bx
    retf

; mega_mxbase - SI = an mx_tab entry -> DX:AX = its block's linear base.
; Clobbers nothing else
mega_mxbase:
    mov ax, [ss:si+MX_OFF]
    mov dx, ax
    shl ax, 10
    shr dx, 6
    add dx, MEGA_XPOOL >> 16
    ret

; mega_claimregrowf - FAR (osapi_mem_regrow, `.cold`): an OS88_HIGHMEM claim
; resized INSIDE THE POOL (KERN-MEGA-PLAN 13.6). An OSAPI_MEM_HUGE claim
; (MX_FHUGE) is .huge's, at any size (14.37). DX = its selector, AX = the
; new KB (1..64), ES = the caller -> CF = 0 and DX the SAME selector over a
; block of the new size, holding the old one's bytes (as many as fit); CF = 1
; not done here - not the caller's claim, 0 or over 64KB, no room or no LDT
; entry - and nothing changed. A shrink or level is the block's length and the
; descriptor's limit. A grow takes the KB above the block where they are free
; - at once, or once the pool is packed round it (.hpack, KERN-MEGA-PLAN
; 14.49) if no block of the new size is to be had. Otherwise it is a new block, a SCRATCH selector over it for
; the copy, then the two blocks trade selectors - the old block goes back to
; mx_free with the scratch one, and the caller's is re-aimed at the new - so
; the caller's DS or ES, reloaded on the way out of the cell, names the new
; bytes and no word the package holds has to change. Every register but DX
; kept (and DX is what it was)
; mega_kregrowf - the same for one of the KERNEL's own pool blocks
; (mega_hiclaim, MX_KOWN): mem_regrow's arm for a mem_hitag claim above 1MB
; (KERN-MEGA-PLAN 14.121 - the desktop shortcuts' table grows a KB per eight)
mega_kregrowf:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov bl, MX_KOWN
    or ax, ax
    jz mega_claimregrowf.no
    mov bp, ax
    call mega_selfind
    jmp short mega_claimregrowf.found
mega_claimregrowf:
    push ax
    push bx
    push cx
    push dx                         ; (the selector: the answer either way)
    push si
    push di
    push bp
    push ds
    push es
    or ax, ax
    jz .no
    mov bp, ax                      ; BP = the new KB
    call mega_claimfind             ; SI = the claim's entry, BL = its owner
.found:
    jc .no
    inc byte [sch_lock]             ; every base below is used before any
    test byte [ss:si+MX_FL], MX_FHUGE  ; other task can compact it away
    jz .one
    jmp .huge
.one:
    cmp bp, 64
    ja .nol
    cmp bp, [ss:si+MX_KB]
    ja .grow
    mov [ss:si+MX_KB], bp              ; shrink or level: in place
    call mega_mxbase
    jmp .aim
.grow:
    call .hroom                     ; the KB above it free: grown in place
    jnc .ogrow                      ; (KERN-MEGA-PLAN 14.49)
    mov ax, bp
    mov dx, ax
    shl ax, 10
    shr dx, 6                       ; DX:AX = the new size in bytes
    call MEGA_SEL_CODE:mx_allocf                   ; DX:AX = the new block's base
    jnc .gnew
    call .hpack                     ; no block that size: the pool packed
    jc .nol                         ; round it, and in place
.ogrow:
    mov [ss:si+MX_KB], bp
    call mega_mxbase
    jmp .aim
.gnew:
    push dx
    push ax
    mov cx, bp
    shl cx, 10                      ; (64KB = 0, mega_xmsel's 65,536)
    call MEGA_SEL_CODE:mega_xmself                 ; AX = a scratch selector, its MX_SEL
    jnc .copy
    pop ax
    pop dx
    call MEGA_SEL_CODE:mx_freef                    ; no LDT entry: the new block goes back
.nol:
    dec byte [sch_lock]
.no:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    retf
.copy:
    push bx
    mov bx, ax                      ; BX = the scratch selector
    mov cx, [ss:si+MX_KB]              ; the old size: the smaller of the two
    shl cx, 9                       ; KB -> words
    push si
    push ds
    push es                         ; (ES must not hold the scratch selector
    mov es, bx                      ; when mx_free gives it back)
    mov ds, [ss:si+MX_SEL]             ; the claim as it stands
    xor si, si
    xor di, di
    cld
    rep movsw
    pop es
    pop ds
    pop si
    mov di, mx_tab                  ; the new block's entry: MX_SEL = scratch
.fnew:
    cmp word [ss:di+MX_KB], 0
    je .fnext
    cmp [ss:di+MX_SEL], bx
    je .swap
.fnext:
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jb .fnew                        ; (cannot fall out: mega_xmsel put it there)
.swap:
    pushf
    cli
    mov ax, [ss:si+MX_SEL]
    mov [ss:di+MX_SEL], ax             ; the new block takes the caller's...
    mov [ss:si+MX_SEL], bx             ; ...and the old one the scratch
    popf
    pop bx                          ; BL = the owner again
    pop ax
    pop dx                          ; DX:AX = the new block's base
    push dx
    push ax
    mov cx, bp                      ; THE CALLER'S SELECTOR ONTO IT FIRST:
    shl cx, 10                      ; freeing the old block frees every
    call MEGA_SEL_CODE:mega_xmself  ; descriptor still based in it (mx_dfree),
    call mega_mxbase                ; which until now was the caller's - the
    call MEGA_SEL_CODE:mx_freef     ; old block, and the scratch selector
    pop ax
    pop dx
.aim:
    mov cx, bp
    shl cx, 10
    call MEGA_SEL_CODE:mega_xmself                 ; the caller's selector over the new size
    dec byte [sch_lock]
    pop es                          ; (it cannot fail: the block is the
    pop ds                          ; caller's and already has its selector)
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    retf

; --- A RUN (MX_FHUGE: OSAPI_MEM_HUGE's) AT ANY SIZE (KERN-MEGA-PLAN 14.37).
; SI = its entry, BL = its owner, BP = the new KB, ES = anything. The bytes:
; a shrink or a level keeps them where they are; a grow takes the KB above
; the block if they are free, else moves it into a fresh block of the new
; size (mx_cpmove: piece by piece, its run following it), else the pool is
; packed down and then, above the claim, UP (.hpack, KERN-MEGA-PLAN 14.49)
; and the KB above it taken: so a grow is refused only when the pool's free
; KB below the first pinned block above the claim are short. The selectors: the run is cut or extended where it is, or
; when the entries after it are taken, moved to a free run of the new length
; and the old one freed - DX is then a NEW first selector, the step still 8.
; A refusal (no KB, or no run of LDT entries) leaves MX_KB as it was, and
; the claim's bytes and run whole, wherever the pass left them
.huge:
    test byte [ss:si+MX_FL], MX_FH32 | MX_FH16 | MX_FBRIDGE ; (windows
    jz .h64                         ; 32KB or 16KB apart: not regrown, 14.41;
                                    ; nor a bridge, MC5)
    jmp .nol
.h64:
    cmp bp, [ss:si+MX_KB]
    jbe .hsel                       ; a shrink or a level: in place
    call .hroom
    jnc .hsel                       ; the KB above it are free
    mov ax, bp
    mov dx, ax
    shl ax, 10
    shr dx, 6
    call MEGA_SEL_CODE:mx_allocf    ; DX:AX = a fresh block of the new size
    jc .hagain
    call mega_mxat                  ; DI = its entry
    push si
    push bx
    push bp
    push di
    mov cx, [ss:si+MX_OFF]             ; from
    mov bp, [ss:di+MX_OFF]             ; to: clear of it, either side
    mov di, si
    call MEGA_SEL_CODE:mx_cpmovef   ; the claim's bytes there, its run aimed
    pop di
    pop bp
    pop bx
    pop si
    mov word [ss:di+MX_KB], 0          ; ...and the fresh entry is the claim's
    mov byte [ss:di+MX_OWN], 0xFF      ; (it has no selector of its own)
    jmp short .hsel
.hagain:
    call .hpack                     ; the pool packed round it (14.49)
    jnc .hsel
.hnol:
    jmp .nol
.hsel:                              ; --- the selectors, for BP KB --------
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    mov cx, [ss:si+MX_KB]
    call mega_hgcnt
    mov ax, cx                      ; AX = the run's length now
    mov cx, bp
    call mega_hgcnt                 ; CX = the length it needs
    mov di, [ss:si+MX_SEL]
    and di, 0xFFF8                  ; DI = its first entry
    cmp cx, ax
    jae .hlong
    push ax                         ; --- shorter: its tail entries go ----
    push cx
    sub ax, cx
    xchg ax, cx                     ; CX = the entries to free
    shl ax, 3
    add ax, [ss:si+MX_SEL]             ; AX = the first of them
.hcut:
    call MEGA_SEL_CODE:mega_ldt_freef
    add ax, 8
    loop .hcut
    pop cx
    pop ax
    jmp short .hset
.hlong:
    je .hset                        ; as long: as it is
    pushf
    cli                             ; scan and claim, one step
    push di
    push cx
    mov dx, ax
    shl dx, 3
    add di, dx                      ; DI = the entry after the run
    sub cx, ax                      ; CX = the entries more it needs
    call mega_hgchk
    pop cx
    pop di
    jnc .hlfill                     ; free: it grows where it is
    call mega_hgfree                ; DI = a free run of the new length
    jc .hnorun
    push cx                         ; ...and the old run goes
    push ax
    mov cx, ax
    mov ax, [ss:si+MX_SEL]
.hold:
    call MEGA_SEL_CODE:mega_ldt_freef
    add ax, 8
    loop .hold
    pop ax
    pop cx
    lea dx, [di+4]
    mov [ss:si+MX_SEL], dx
.hlfill:
    call .hfill
    popf
    jmp short .hok
.hnorun:
    popf
    jmp short .hnol
.hset:
    call .hfill
.hok:
    mov dx, [ss:si+MX_SEL]             ; --- the answer: the run's first -----
    mov bp, sp
    mov [bp+10], dx                 ; (the DX this proc pushed)
    dec byte [sch_lock]
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    retf

; .hfill - ES:DI = the run's first entry, CX = its length, BP = the KB: the
; claim is BP KB and the run's descriptors say so, over its block's base
.hfill:
    mov [ss:si+MX_KB], bp
    push bx
    mov bx, bp
    call mega_mxbase                ; DX:AX = the block's base
    call mega_hgfill
    pop bx
    ret

; .hpack - SI = the claim's entry, BP = the KB it wants: the pool packed DOWN
; (mx_compactf), then every movable block above the claim packed UP against
; the pinned one above it (mx_packupf), so that every free KB below that
; block sits directly over the claim (KERN-MEGA-PLAN 14.49: [R][B..][free] is
; refused no more when the free KB are enough) -> .hroom's CF. Clobbers
; nothing else
.hpack:
    push ax
    call MEGA_SEL_CODE:mx_compactf
    mov ax, [ss:si+MX_OFF]
    add ax, [ss:si+MX_KB]           ; the floor: the claim's end, where the
    call MEGA_SEL_CODE:mx_packupf   ; compaction left it
    pop ax
                                    ; (on into .hroom)
; .hroom - SI = the claim's entry, BP = the KB it wants: CF = 0 the KB
; between its end and BP KB from its start are free and inside the pool,
; CF = 1 they are not. Clobbers nothing else
.hroom:
    push ax
    push cx
    push dx
    push di
    mov dx, [ss:si+MX_OFF]
    add dx, bp                      ; DX = the end it wants
    jc .hrn
    cmp dx, [xm_kb]
    ja .hrn
    mov ax, [ss:si+MX_OFF]
    add ax, [ss:si+MX_KB]              ; AX = the end it has
    mov di, mx_tab
.hr:
    cmp word [ss:di+MX_KB], 0
    je .hrx
    cmp di, si
    je .hrx
    cmp [ss:di+MX_OFF], dx
    jae .hrx                        ; starts at or past the end it wants
    mov cx, [ss:di+MX_OFF]
    add cx, [ss:di+MX_KB]
    cmp cx, ax
    ja .hrn                         ; ...or ends past the end it has: there
.hrx:
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jb .hr
    clc
    jmp short .hro
.hrn:
    stc
.hro:
    pop di
    pop dx
    pop cx
    pop ax
    ret

section .text

; mega_pkghif - FAR (ld_hi, `.cold`): a just-loaded PACKAGE region ABOVE 1MB
; (KERN-MEGA-PLAN 13.3 step 4). AX = its conventional segment, CX = its bytes
; (the region, image and bss, 1..65534), DL = the instance slot -> CF = 0 and
; AX = the DATA selector of a pair holding a copy of it all, its code half
; being AX - 8; CF = 1 and nothing taken. The block's owner is 80h | slot:
; mx_relall at the instance's teardown frees the slot's OWN XMEM blocks and
; not this one, and no cell will hand it to a package, both matching the
; owner byte exactly. The copy runs under sch_lock, which serializes
; mega_lincopy's scratch descriptors. Clobbers nothing else
;
; SI NON-ZERO IS A KEEP OVER 64KB (ld_hisi, KERN-MEGA-PLAN 14.48): CX is then
; its PARAGRAPHS (0x1000 and up) and SI the bytes the pair covers - ONE block
; of the whole keep, so it moves and is freed as one, the pair over its head
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_pkghif:
    push bx
    mov bl, dl
    or bl, 0x80                     ; BL = the owner, 80h | slot
    or si, si
    jnz .huge
    call MEGA_SEL_CODE:mega_imghif
    pop bx
    retf
.huge:
    call mega_imghuge
    pop bx
    retf

; mega_imghuge - mega_imghif for a block OVER 64KB (KERN-MEGA-PLAN 14.48): AX
; = the conventional segment, CX = its PARAGRAPHS, SI = the bytes the pair
; covers (1..65535), BL = the owner -> CF = 0 and AX = the pair's DATA
; selector, its base the block's; CF = 1 nothing taken. The copy is 32KB a
; call under sch_lock, which serializes mega_lincopy's scratch. The block is
; not MX_FHUGE: its selectors are the pair, freed by mega_imghifree, and
; whatever OSAPI_MEM_SUBSEG made inside it, freed by mega_rgnsubfree.
; Clobbers nothing else
mega_imghuge:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax                      ; BP = the source segment
    cmp word [xm_kb], 0
    je .no
    mov ax, cx
    mov dx, cx
    shl ax, 4
    shr dx, 12                      ; DX:AX = the bytes
    inc byte [sch_lock]             ; to the copy's end: the base is used
    call mx_alloc                   ; DX:AX = the block's base
    jc .nol
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    push cx
    mov cx, si
    dec cx                          ; the pair's limit
    call mega_ldt_alloc2            ; AX = its code selector
    pop cx
    jnc .got
    mov ax, [mega_xdst]             ; no pair: the block goes back
    mov dx, [mega_xdst+2]
    call mx_free
.nol:
    dec byte [sch_lock]
.no:
    stc
    jmp short .out
.got:
    push ax
    mov ax, bp                      ; the source's base, off its
    push cx                         ; DESCRIPTOR: a selector since KERN-MEGA-
    call mega_selbase               ; MEMORY-PLAN MC7.4 (an identity segment
    pop cx                          ; reads N x 16 there all the same)
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
.c:
    mov ax, 0x800                   ; 32KB a call, in paragraphs
    cmp cx, ax
    jae .f
    mov ax, cx
.f:
    push ax
    push cx
    mov cx, ax
    shl cx, 4                       ; bytes, even
    call KERNEL_SEG:mega_lincopyf
    pop cx
    pop ax
    add word [mega_xsrc], 0x8000    ; (only a whole chunk is ever followed)
    adc word [mega_xsrc+2], 0
    add word [mega_xdst], 0x8000
    adc word [mega_xdst+2], 0
    sub cx, ax
    jnz .c
    dec byte [sch_lock]
    pop ax
    add ax, 8                       ; the DATA half names the region
    clc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mega_imghif - FAR: mega_pkghif for any image, BL = the pool owner byte
; (MX_KDRV for a driver, KERN-MEGA-PLAN item 82, 14.37). AX = the conventional
; segment, CX = the bytes -> CF = 0 and AX = the pair's DATA selector; CF = 1
; nothing taken. Clobbers nothing else
;
; CX = FFFFh IS A WHOLE 64KB REGION, and it is not a rare case: ld_alloc
; saturates I_SIZE to FFFFh for any package whose image + bss rounds past 63KB
; (65024 bytes is 64KB of claim, and 64 << 10 is 0 in a word), and ld_hi hands
; that word here. `inc cx` then made the block, the limit and the copy all
; ZERO bytes, so WIREFLET (44,128 + 20,896) ran out of a 64KB pair holding none
; of its image and faulted before its entry proc's first instruction
; (KERN-MEGA-PLAN 14.67). So 0 after the round-up means 10000h: the block is
; 64KB, the limit FFFFh, and the copy 65534 bytes, mega_lincopy's ceiling -
; the last two are past image + bss <= FE00h and are nobody's
mega_imghif:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax
    inc cx
    and cl, 0xFE                    ; the copy moves words
    cmp word [xm_kb], 0
    je .no
    push cx
    mov ax, cx
    xor dx, dx
    or ax, ax
    jnz .sized
    inc dx                          ; CX was FFFFh: DX:AX = 10000h (above)
.sized:
    inc byte [sch_lock]             ; to the copy's end: the base is used
    call mx_alloc                   ; DX:AX = the block's base
    pop cx
    jc .nol
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    push bx
    push cx
    dec cx                          ; the limit
    mov bx, ax
    call mega_ldt_alloc2            ; AX = the pair's code selector
    pop cx
    pop bx
    jnc .got
    mov ax, [mega_xdst]             ; no pair: the block goes back
    mov dx, [mega_xdst+2]
    call mx_free
.nol:
    dec byte [sch_lock]
.no:
    stc
    jmp short .out
.got:
    push ax
    mov ax, bp                      ; the source's base, off its
    push cx                         ; DESCRIPTOR: a selector since KERN-MEGA-
    call mega_selbase               ; MEMORY-PLAN MC7.4 (an identity segment
    pop cx                          ; reads N x 16 there all the same)
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    or cx, cx
    jnz .copy
    mov cx, 0xFFFE                  ; a 64KB region: mega_lincopy's ceiling
.copy:
    call KERNEL_SEG:mega_lincopyf
    dec byte [sch_lock]
    pop ax
    add ax, 8                       ; the DATA half names the region
    clc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; mega_pkghifree - FAR: AX = a region mega_pkghif gave, DL = its slot -> the
; block, both of the pair's entries AND every other LDT descriptor whose base
; is inside the block are free - OSAPI_MEM_SUBSEG's (KERN-MEGA-PLAN 14.44),
; so a selector a program asked for cannot outlive it. Clobbers nothing
mega_pkghifree:
    push bx
    mov bl, dl
    or bl, 0x80
    call mega_rgnsubfree
    call MEGA_SEL_CODE:mega_imghifree
    pop bx
    retf

; mega_rgnblk - AX = a region's DATA selector, BL = its pool owner (80h |
; slot) -> CF = 0, SI = its mx_tab entry and DX:AX = the block's linear base
; (the pair's base IS the block's: mega_imghif); CF = 1 it is no such block.
; Clobbers CL
mega_rgnblk:
    call mega_selbase               ; DX:AX = the descriptor's base
    jc .no
    push ax
    push dx
    call mx_rel                     ; AX = KB from the pool's base
    jc .pno
    mov si, mx_tab
.f:
    cmp word [ss:si+MX_KB], 0
    je .n
    cmp [ss:si+MX_OWN], bl
    jne .n
    cmp [ss:si+MX_OFF], ax
    je .y
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .f
.pno:
    pop dx
    pop ax
.no:
    stc
    ret
.y:
    pop dx
    pop ax
    clc
    ret

; mega_rgnsubfree - AX = a region's DATA selector, BL = its owner -> every
; present LDT descriptor in the allocator's range whose base lies inside the
; block is free, except the region's own pair (mega_imghifree's to free).
; Nothing, and CF = 1, when it is no region block. Clobbers nothing
mega_rgnsubfree:
    pusha
    push es
    mov bp, ax
    and bp, 0xFFF8                  ; BP = the pair's DATA entry
    call mega_rgnblk                ; SI = the entry, DX:AX = the base
    jc .out
    mov cx, ax
    mov bx, dx                      ; BL:CX = the base
    mov ax, [ss:si+MX_KB]
    mov di, ax
    shl di, 10
    shr ax, 6
    mov bh, al                      ; BH:DI = its bytes, 24 bits: a block
                                    ; over 64KB (14.48) holds part selectors
                                    ; past its first 64KB
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    mov si, MEGA_LDT_FREE0 * 8
.d:
    cmp byte [es:si+5], 0
    je .dn
    cmp si, bp
    je .dn
    lea ax, [si+8]
    cmp ax, bp
    je .dn                          ; the pair's code half
    mov ax, [es:si+2]
    mov dl, [es:si+4]
    sub ax, cx
    sbb dl, bl
    jc .dn                          ; below the block
    cmp dl, bh
    ja .dn
    jb .in
    cmp ax, di
    jae .dn                         ; at or past its end: another block's
.in:
    lea ax, [si+4]
    call mega_ldt_free
.dn:
    add si, 8
    cmp si, [ss:mega_ldte]
    jb .d
    clc
.out:
    pop es
    popa
    ret

; -----------------------------------------------------------------------------
; mega_subsegf - FAR: OSAPI_MEM_SUBSEG's body in protected mode (KERN-MEGA-
; PLAN 14.44). ES = the caller's DS (the X stub's), AX = a PARAGRAPH offset
; into it, BL bit 0 = it will be far-called -> CF = 0 and DX = a DATA
; selector whose base is that paragraph and whose limit runs to the end of
; the caller's region or 64KB, whichever is first; with bit 0 its CODE twin
; is DX - 8. BL bit 1 (14.48): CX = the window's length in paragraphs, which
; ends it sooner - a part's selector over its part alone, so no descriptor
; crosses another part and mx_cpmove can move the block a part at a time.
; A descriptor already over that base, of that type and limit, is answered
; instead of a new one, so a program uses one LDT entry (two for code) per
; distinct paragraph it asks for, and every one of them is freed with the
; region (mega_rgnsubfree). They move with it too: mx_cpmove rebases every
; descriptor whose base is inside a block it moves. CF = 1, every register
; kept, when ES is not a package's region (above 1MB or, since KERN-MEGA-
; MEMORY-PLAN MC7.4, a conventional claim's pair), the paragraph is
; at or past its end, or no LDT entry is free - and the caller's answer then
; is its own segment + AX, which is right wherever the region is in real
; memory. DS = KDATA
; -----------------------------------------------------------------------------
mega_subsegf:
    pusha
    push es
    inc byte [sch_lock]             ; no task compacts the pool between
                                    ; reading the block's base and aiming
                                    ; a descriptor at it (mx_alloc's rule)
    mov bp, ax                      ; BP = the paragraph offset
    mov bh, bl                      ; BH bit 0 = a code twin wanted
    mov ax, es
    test al, 4                      ; an identity segment: below 1MB, and
    jz .no                          ; the caller's sum is right there
    call mega_slotof                ; BL = the instance ES is the region of
    jc .no
    or bl, 0x80
    call mega_rgnblk                ; SI = its block, DX:AX = its base
    jc .conv
    mov cx, [ss:si+MX_KB]
    shl cx, 6                       ; the block in paragraphs (under 1MB)
    jmp short .sz
.conv:
    mov ax, es                      ; ...or a CONVENTIONAL region, named by
    call mega_rgnconv               ; its claim's pair (KERN-MEGA-MEMORY-
    jc .no                          ; PLAN MC7.4): CX, DX:AX the same
.sz:
    sub cx, bp
    jbe .no                         ; at or past its end
    test bh, 2                      ; BL bit 1: the caller's CX is the
    jz .len                         ; window's length in paragraphs (14.48),
    mov di, sp                      ; so a part's selector ends where the part
    mov di, [ss:di+14]              ; does - pusha's CX, under ES
    dec di
    cmp di, cx
    jae .len                        ; (0 or past the end: to the end)
    inc di
    mov cx, di
.len:
    cmp cx, 0x1000
    jb .lim
    mov cx, 0x1000                  ; 64KB at most
.lim:
    shl cx, 4
    dec cx                          ; the limit (0x1000 -> 0xFFFF)
    mov si, bp
    shr si, 12
    shl bp, 4
    add ax, bp
    adc dx, si                      ; DX:AX = the paragraph's linear address
    mov bl, 0x92                    ; BL = the access byte wanted: data, or
    test bh, 1                      ; a pair's code half (its data twin is
    jz .typ                         ; at + 8 by the allocator's own rule,
    mov bl, 0x9A                    ; which mega_codesel already relies on)
.typ:
    mov si, MEGA_SEL_LDTD
    mov es, si
    mov di, MEGA_LDT_FREE0 * 8
.s:
    mov bh, [es:di+5]
    and bh, 0xFE                    ; (the CPU sets the ACCESSED bit)
    cmp bh, bl
    jne .sn
    cmp [es:di+2], ax
    jne .sn
    cmp [es:di], cx                 ; ...and that limit
    jne .sn
    cmp [es:di+4], dl
    je .hit                         ; one over that base already
.sn:
    add di, 8
    cmp di, [ss:mega_ldte]
    jb .s
    cmp bl, 0x92
    jne .nc
    call mega_ldt_alloc             ; AX = the selector
    jnc .got
.no:
    dec byte [sch_lock]
    pop es
    popa
    stc
    retf
.nc:
    call mega_ldt_alloc2            ; AX = the code half
    jc .no
.got:
    xchg ax, di
    sub di, 4                       ; DI = the entry, as .hit has it
.hit:
    lea dx, [di+4]                  ; the selector...
    cmp bl, 0x92
    je .ok
    add dx, 8                       ; ...or the pair's data half
.ok:
    mov bp, sp
    mov [bp+2+10], dx               ; pusha's DX, under the ES pushed after
    dec byte [sch_lock]
    pop es
    popa
    clc
    retf

; mega_rgnconv - AX = a conventional claim's DATA selector -> CF = 0, CX =
; its paragraphs and DX:AX = its base; CF = 1 no claim is named by it.
; Clobbers nothing else
mega_rgnconv:
    push bx
    push si
    mov si, mem_tab
    mov bx, mem_sel
.l:
    cmp [ss:bx], ax
    je .f
    add si, MC_SIZE
    inc bx
    inc bx
    cmp bx, mem_sel + MEGA_MSELN * 2
    jb .l
    stc
    jmp short .o
.f:
    mov cx, [ss:si+MC_PARA]
    mov ax, [ss:si+MC_SEG]
    mov dx, ax
    shl ax, 4
    shr dx, 12
    clc
.o:
    pop si
    pop bx
    ret

; mega_drvhifree - FAR (drv_release): AX = a driver image drv_hi placed ->
; its block and pair are free. Clobbers nothing
mega_drvhifree:
    push bx
    mov bl, MX_KDRV
    call MEGA_SEL_CODE:mega_imghifree
    pop bx
    retf

; mega_imghifree - FAR: AX = an image's DATA selector, BL = its pool owner
; -> the block and both of the pair's entries are free. Clobbers nothing
mega_imghifree:
    push ax
    push bx
    push cx
    push dx
    push ax
    call mega_selbase               ; DX:AX = the block's base
    call mx_free
    pop ax
    push ax
    sub ax, 8
    call mega_ldt_free              ; the code half...
    pop ax
    call mega_ldt_free              ; ...and the data half
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; mega_segf - FAR: OSAPI_SEG's body in protected mode (SPEC.md 50.6.11,
; KERN-MEGA-MEMORY-PLAN MC7). BL = the verb, ES = the caller's DS, DS = KDATA.
; Every register but the answer kept.
;   SEG_LIN   the descriptor's base (mega_selbase)
;   SEG_REAL  the base as a real-mode segment, when it is one
;   SEG_WIN   a selector over a physical range, owned by the caller and
;             recorded in mega_wtab, so its end (mx_relall) frees it
;   SEG_SUB   a selector at a paragraph inside one of the caller's segments:
;             inside a POOL BLOCK it is a descriptor based in the block, which
;             the compactor rebases (mx_desc) and the block's free takes with
;             it (mx_dfree); anywhere else - a conventional claim, a region
;             below 1MB - it is a window (above), a 64KB reach at most, which
;             is what the segment + AX it replaces reached
;   SEG_FREE  a window back early; a block's selector goes with its block
; A window or a SUB asked again with the same base and reach answers the same
; selector, so a caller that asks per access holds one entry, not many
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_segf:
    cmp bl, SEG_LIN
    je .lin
    cmp bl, SEG_REAL
    je .real
    cmp bl, SEG_WIN
    je .win
    cmp bl, SEG_SUB
    je .sub
    cmp bl, SEG_FREE
    je .free
    cmp bl, SEG_SUBC
    je .subc
    cmp bl, SEG_BWIN
    je .bwin
    cmp bl, SEG_AIM
    je .aimj
    cmp bl, SEG_AT
    je .at
    stc
    retf
.aimj:
    jmp mega_segaim
.at:                                ; SEG_AT: DX's base + AX paragraphs, a
    push ax                         ; 64KB window, CX the caller's to re-aim -
    push bx                         ; never matched against another window,
    push cx                         ; so two walks never share one
    push si
    push di
    mov si, cx                      ; SI = the window to re-aim, or 0
    mov di, ax                      ; DI = the paragraphs
    mov ax, dx
    call mega_selbase               ; DX:AX = the segment's base
    jc .atno
    mov cx, di
    shl di, 4
    add ax, di
    adc dl, 0
    shr cx, 12
    add dx, cx                      ; DX:AX = the window's base
    mov cx, 0xFFFF
    call mega_segown                ; BL = the caller's owner byte
    xor di, di                      ; (no match)
    call mega_wset                  ; AX = the window
    jc .atno
    xchg ax, dx
    jmp short .atout
.atno:
    xor dx, dx                      ; none: the null selector, CF = 0
.atout:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    retf
.bwin:
    jmp mega_segbw
.lin:
    push cx
    mov ax, dx
    call mega_selbase               ; DX:AX = its base (CF = 1 the null one)
    pop cx
    retf
.real:
    push ax
    push cx
    push dx
    mov ax, dx
    call mega_selbase
    jc .rno
    test al, 0x0F
    jnz .rno                        ; off the 16-byte grid
    cmp dl, 0x0F
    ja .rno                         ; at or above 1MB
    shr ax, 4
    shl dl, 4
    or ah, dl                       ; AX = the segment
    pop dx
    mov dx, ax
    pop cx
    pop ax
    clc
    retf
.rno:
    pop dx
    pop cx
    pop ax
    stc
    retf
.win:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, ax                      ; SI = the window to re-aim, or 0
    mov ax, dx
    mov dx, ax
    shl ax, 4
    shr dx, 12                      ; DX:AX = the paragraph's linear address
    dec cx                          ; the limit (0 - 1 = 64KB)
    call mega_segown                ; BL = the caller's owner byte
    call mega_wfind                 ; DI = a window of the caller's
    jmp .wput
.subc:
    push ax
    push bx
    push cx
    push si
    push di
    mov bh, 0x9A                    ; a PAIR: its code half, data at + 8
    jmp short .sub1
.sub:
    push ax
    push bx
    push cx
    push si
    push di
    mov bh, 0x92                    ; data alone
.sub1:
    mov si, ax                      ; SI = the paragraph offset
    mov di, cx                      ; DI = paragraphs, 0 = to the end
    mov ax, dx
    call mega_selbase               ; DX:AX = the segment's base
    jc .sno
    push si
    shl si, 4                       ; + paragraphs x 16
    add ax, si
    pop si
    adc dl, 0
    shr si, 12
    add dx, si                      ; DX:AX = the window's base
    mov cx, di                      ; CX = its reach in bytes - 1: the asked
    dec cx                          ; paragraphs, or 64KB
    cmp cx, 0x0FFF
    jae .s64
    shl cx, 4
    add cx, 15
    jmp short .sblk
.s64:
    mov cx, 0xFFFF
.sblk:
    call mega_bclip                 ; inside a pool block or a conventional
    jc .snb                         ; claim: clipped to its end, CF = 0 - a
    mov bl, bh                      ; descriptor of the block's
    call mega_dfind                 ; one already over it?
    jnc .sok
    cmp bl, 0x92
    jne .spair
    call mega_ldt_alloc             ; AX = a new one
    jnc .sok
.snone:
    mov ax, 1                       ; no LDT entry free
    jmp short .sno1
.spair:
    call mega_ldt_alloc2            ; AX = a new pair's code half
    jc .snone
.sok:
    cmp bl, 0x92
    je .sdat
    add ax, 8                       ; a pair answers its DATA half
.sdat:
    mov dx, ax
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    retf
.snb:
    cmp bh, 0x92                    ; a PAIR is a block's or nothing
    je .snw
    mov ax, 3
    jmp short .sno1
.snw:
    xor si, si                      ; anywhere else: a window of the caller's
    call mega_segown
    call mega_wfind
.wput:                              ; DX:AX = the base, CX = the limit, BL =
    call mega_wset                  ; the owner, SI = a window to re-aim or 0,
    jc .sno1                        ; DI = a match or 0 -> AX = the selector
    mov dx, ax
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    retf
.sno:
    mov ax, 2                       ; (the null selector)
.sno1:
    pop di
    pop si
    pop cx
    pop bx
    add sp, 2                       ; the caller's AX: the reason instead
    stc
    retf
.free:
    push di
    call mega_wslot                 ; DI = DX's row in mega_wtab
    jc .fo                          ; not a window: a block's goes with it
    push ax
    mov ax, dx
    call mega_ldt_free
    pop ax
    mov word [ss:di], 0
.fo:
    pop di
    clc
    retf

; mega_segown - ES = the caller's DS -> BL = the owner a window is recorded
; under: its instance slot (mx_relinst's), 0C0h | its drv_tab row (a
; driver's, drv_release's mx_relallf), or FFh (nobody's to end - SEG_FREE
; only). Clobbers nothing else
mega_segown:
    call mega_slotof                ; BL = the slot
    jnc .o
    push ax
    push bx
    mov bx, es
    call COLD_SEG:drv_row_ixf       ; AL = the row
    pop bx
    mov bl, 0xFF
    jc .p
    or al, 0xC0
    mov bl, al
.p:
    pop ax
.o:
    ret

; mega_wfind - DX:AX = a base, CX = a limit, BL = an owner -> DI = that
; owner's window already over exactly that, or 0. Clobbers nothing else
mega_wfind:
    push si
    push es
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov si, mega_wtab
.l:
    mov di, [ss:si]
    or di, di
    jz .n
    cmp [ss:si+2], bl
    jne .n
    cmp byte [ss:si+3], 0           ; a BOUND window (SEG_BWIN) is its
    jne .n                          ; owner's alone to aim
    and di, 0xFFF8
    cmp [es:di+2], ax
    jne .n
    cmp [es:di+4], dl
    jne .n
    cmp [es:di], cx
    jne .n
    mov di, [ss:si]                 ; the selector
    jmp short .o
.n:
    add si, 4
    cmp si, mega_wtab_end
    jb .l
    xor di, di
.o:
    pop es
    pop si
    ret

; mega_wset - DX:AX = a base, CX = a limit, BL = the owner, SI = the
; caller's window to re-aim (0 = none), DI = a match (0 = none) -> CF = 0 and
; AX = the selector: SI re-aimed when it is the owner's window; else the
; match; else a new entry and a row of mega_wtab. CF = 1, AX = 1 no LDT
; entry or no row. Clobbers DI, SI
;
; THE CALLER'S OWN WINDOW FIRST, the match only when it named none. The match
; came first, and a re-aim that landed exactly where ANOTHER of the caller's
; windows already pointed was answered with that other window - so two words
; the caller holds named ONE descriptor, and re-aiming the second moved the
; first with it. The C64 re-aims its three ROM-bank windows in turn after its
; carve moves (c64_rbset), and a move of 5000h bytes - exactly KERNAL's
; offset less CHARGEN's - left KERNAL's window on CHARGEN: the 6510 fetched
; its reset code out of the character set and jammed (KERN-MEGA-PLAN 14.80)
mega_wset:
    or si, si
    jz .nm
    push di
    push dx
    mov dx, si
    call mega_wslot                 ; DI = SI's row
    pop dx
    jc .nown
    cmp [ss:di+2], bl
    je .own                         ; the caller's own: re-aim it
.nown:
    pop di
.nm:
    or di, di
    jz .new
    mov ax, di
    clc
    ret
.own:
    add sp, 2                       ; (the match: not wanted)
    push es
    push di
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov di, si
    and di, 0xFFF8
    pushf
    cli
    mov [es:di], cx                 ; RE-AIMED: limit and base
    mov [es:di+2], ax
    mov [es:di+4], dl
    popf
    pop di
    pop es
    mov ax, si
    clc
    ret
.new:
    mov si, mega_wtab               ; a free row first...
.r:
    cmp word [ss:si], 0
    je .rf
    add si, 4
    cmp si, mega_wtab_end
    jb .r
    mov ax, 1                       ; none
    stc
    ret
.rf:
    push bx
    mov bl, 0x92
    call mega_ldt_alloc             ; ...then the entry
    pop bx
    jnc .ok
    mov ax, 1
    ret
.ok:
    mov [ss:si], ax
    mov [ss:si+2], bl
    mov byte [ss:si+3], 0           ; not bound
    clc
    ret

; mega_wslot - DX = a selector -> CF = 0 and DI = its row in mega_wtab;
; CF = 1 it is no window. Clobbers nothing else
mega_wslot:
    mov di, mega_wtab
.l:
    cmp [ss:di], dx
    je .o                           ; (CF = 0)
    add di, 4
    cmp di, mega_wtab_end
    jb .l
    stc
.o:
    ret

; mega_blkof - DX:AX = a linear address -> CF = 0, DI:SI = the base of the
; block that holds it - a POOL block, or a CONVENTIONAL claim (KERN-MEGA-
; MEMORY-PLAN MC7.4) - and DX:AX = its end; CF = 1 it is in neither. A pool
; block's entry is left in BX (0 for a claim). Clobbers nothing else
mega_blkof:
    push cx
    cmp dx, MEGA_XPOOL >> 16
    jb .conv
    call mx_rel                     ; AX = its KB from the pool's base
    jc .no
    push si
    call mx_inblk                   ; SI = the block
    mov bx, si
    pop si
    jc .no
    mov ax, [ss:bx+MX_OFF]
    call mx_kblin
    mov di, dx
    mov si, ax                      ; DI:SI = the block's base
    mov ax, [ss:bx+MX_OFF]
    add ax, [ss:bx+MX_KB]
    call mx_kblin                   ; DX:AX = its end
    clc
    jmp short .o
.conv:
    shr ax, 4
    shl dx, 12
    or ax, dx                       ; AX = its paragraph
    mov si, mem_tab
.l:
    mov cx, [ss:si+MC_SEG]
    jcxz .n
    cmp ax, cx
    jb .n
    mov dx, cx
    add dx, [ss:si+MC_PARA]
    cmp ax, dx
    jb .f
.n:
    add si, MC_SIZE
    cmp si, mem_tab + MEM_MAX * MC_SIZE
    jb .l
.no:
    stc
    jmp short .o
.f:
    mov ax, dx
    shl ax, 4
    shr dx, 12                      ; DX:AX = the claim's end
    mov si, cx
    mov di, cx
    shl si, 4
    shr di, 12                      ; DI:SI = its base
    xor bx, bx
    clc
.o:
    pop cx
    ret

; mega_bclip - DX:AX = a base, CX = a limit -> CF = 0 when the base lies in a
; block (mega_blkof's: the pool's, or a conventional claim's), CX clipped so
; the reach ends at the block's end; CF = 1 in none. Clobbers nothing else
mega_bclip:
    push ax
    push bx
    push dx
    push si
    push di
    push ax
    push dx
    call mega_blkof                 ; DX:AX = the block's end
    pop di
    pop si                          ; DI:SI = the base asked for
    jc .o
    sub ax, si
    sbb dx, di                      ; DX:AX = bytes to its end
    jnz .yes                        ; 64KB or more: CX stands
    dec ax
    cmp cx, ax
    jbe .yes
    mov cx, ax                      ; clipped at the block's end
.yes:
    clc
.o:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; mega_segbw - SEG_BWIN's body (FAR's tail, mega_segf): DX = a selector of the
; caller's -> CF = 0, DX = a NEW data selector over the block its base lies in
; - the pool's or a conventional claim - aimed at that base, 64KB long or to
; the block's end: a WINDOW the caller re-aims with SEG_AIM, recorded in
; mega_wtab as BOUND (its row's byte 3) under the caller's owner, so its end
; frees it. Based in the block, it moves with it (mx_desc, mega_mpmovef) and
; goes with it; a pool block holding one moves with the rate hook held off,
; as an OSAPI_XMEM_RUN block does (mx_cpflags, MX_FWIN). CF = 1, AX = 1 no
; LDT entry or row, 2 the null selector, 3 in no block
mega_segbw:
    push ax
    push bx
    push cx
    push si
    push di
    mov ax, dx
    call mega_selbase               ; DX:AX = its base
    jc .null
    mov cx, 0xFFFF
    call mega_bclip                 ; CX = the reach
    jnc .in
    mov ax, 3
    jmp short .no
.null:
    mov ax, 2
    jmp short .no
.in:
    push ax
    push dx
    call mega_blkof                 ; BX = a pool block's entry, 0 a claim's
    pop dx
    pop ax
    or bx, bx
    jz .nofl
    or byte [ss:bx+MX_FL], MX_FWIN
.nofl:
    call mega_segown                ; BL = the caller's owner
    mov si, mega_wtab               ; a free row...
.r:
    cmp word [ss:si], 0
    je .rf
    add si, 4
    cmp si, mega_wtab_end
    jb .r
    jmp short .none
.rf:
    push bx
    mov bl, 0x92
    call mega_ldt_alloc             ; ...and a FRESH entry, never a match:
    pop bx                          ; re-aiming it must move nobody else's
    jc .none
    mov [ss:si], ax
    mov [ss:si+2], bl
    mov byte [ss:si+3], 1           ; BOUND
    mov dx, ax
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    retf
.none:
    mov ax, 1
.no:
    pop di
    pop si
    pop cx
    pop bx
    add sp, 2                       ; the caller's AX: the reason instead
    stc
    retf

; mega_segaim - SEG_AIM's body (mega_segf): DX = a BOUND window of the caller's
; (SEG_BWIN), CX:AX = a byte offset into its block -> CF = 0, DX = the same
; window, its base the block's + the offset and its reach 64KB or to the
; block's end, AX = 0, the offset to use in it. CF = 1, AX = 4 not a bound
; window of the caller's, 5 the offset at or past the block's end; DX kept.
; LEGAL IN AN FSXF_RATE HOOK (SPEC.md 53.2.2's exception for this verb): IF is
; never set, nothing is locked or waited on, no task can run, and the whole
; call - the cell's own frame included - is about 40 bytes of the caller's
; stack. The block is found afresh each call, so a move between two aims is
; followed
mega_segaim:
    push bx
    push cx
    push si
    push di
    push ax                         ; [the offset's low word]
    push dx
    call mega_wslot                 ; DI = its row
    jc .not
    cmp byte [ss:di+3], 0
    je .not                         ; not bound
    mov si, di
    call mega_segown                ; BL = the caller's owner
    cmp [ss:si+2], bl
    jne .not
    mov ax, dx
    push cx
    call mega_selbase               ; DX:AX = where it is aimed now...
    pop cx
    call mega_blkof                 ; ...DI:SI = its block, DX:AX = the end
    jc .not
    mov bx, sp
    add si, [ss:bx+2]               ; the base + the offset (CX:[bx+2])
    adc di, cx
    jc .past
    sub ax, si
    sbb dx, di                      ; bytes from the new base to the end
    jc .past
    mov cx, 0xFFFF
    or dx, dx
    jnz .lim
    or ax, ax
    jz .past                        ; exactly at the end
    dec ax
    mov cx, ax
.lim:
    mov bx, [ss:bx]                 ; the window
    and bx, 0xFFF8
    push es
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pushf
    cli
    mov [es:bx], cx
    mov [es:bx+2], si
    mov ax, di
    mov [es:bx+4], al
    popf
    pop es
    pop dx
    add sp, 2
    xor ax, ax
    pop di
    pop si
    pop cx
    pop bx
    clc
    retf
.past:
    mov ax, 5
    jmp short .no
.not:
    mov ax, 4
.no:
    pop dx
    add sp, 2
    pop di
    pop si
    pop cx
    pop bx
    stc
    retf

; mega_sblk - DX:AX = a base, CX = a limit -> CF = 0 when the base lies in a
; POOL BLOCK, CX clipped so the reach ends at the block's end; CF = 1 not in
; one. Clobbers nothing else
mega_sblk:
    push ax
    push bx
    push dx
    push si
    cmp dx, MEGA_XPOOL >> 16
    jb .no
    push cx
    mov bx, ax
    mov si, dx                      ; SI:BX = the base
    call mx_rel                     ; AX = its KB from the pool's base
    pop cx
    jc .no
    push cx
    call mx_inblk                   ; SI = the block's entry
    pop cx
    jc .no
    mov ax, [ss:si+MX_OFF]
    add ax, [ss:si+MX_KB]
    call mx_kblin                   ; DX:AX = the block's end
    pop si
    push si
    push di
    mov di, sp
    mov di, [ss:di+4]               ; the base's high word, as pushed
    sub ax, bx
    sbb dx, di                      ; DX:AX = bytes from the base to the end
    pop di
    or dx, dx
    jnz .yes                        ; 64KB or more: CX stands
    dec ax
    cmp cx, ax
    jbe .yes
    mov cx, ax                      ; clipped at the block's end
.yes:
    clc
    jmp short .o
.no:
    stc
.o:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; mx_inblk - AX = a KB from the pool's base -> CF = 0 and SI = the live block
; that holds it; CF = 1 none. Clobbers nothing else
mx_inblk:
    push cx
    mov si, mx_tab
.l:
    cmp word [ss:si+MX_KB], 0
    je .n
    mov cx, [ss:si+MX_OFF]
    cmp ax, cx
    jb .n
    add cx, [ss:si+MX_KB]
    cmp ax, cx
    jb .o                           ; (CF = 1 there: inverted below)
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .l
    pop cx
    stc
    ret
.o:
    pop cx
    clc
    ret

; mega_dfind - DX:AX = a base, CX = a limit, BL = an access byte -> CF = 0
; and AX = an allocator descriptor already exactly that; CF = 1 none, AX
; kept. Clobbers nothing else
mega_dfind:
    push di
    push es
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov di, MEGA_LDT_FREE0 * 8
.l:
    push ax
    mov al, [es:di+5]
    and al, 0xFE                    ; (the CPU sets the ACCESSED bit)
    cmp al, bl
    pop ax
    jne .n
    cmp [es:di+2], ax
    jne .n
    cmp [es:di+4], dl
    jne .n
    cmp [es:di], cx
    jne .n
    lea ax, [di+4]
    clc
    jmp short .o
.n:
    add di, 8
    cmp di, [ss:mega_ldte]
    jb .l
    stc
.o:
    pop es
    pop di
    ret

; -----------------------------------------------------------------------------
; A CONVENTIONAL CLAIM'S PAIR (KERN-MEGA-MEMORY-PLAN MC7.4). In protected mode
; every mem_tab record has a code/data pair over its block, the DATA selector
; in mem_sel at the record's index and the CODE half at data - 8 for every
; claim. These keep the pair in step with the record: made at the publish and
; at the switch, re-aimed when the record's base or length changes, freed
; with the record, rebased when the compactor moves the block. Each is FAR
; (memory.inc is `.cold`), DS = KDATA, SI = the record, and keeps every
; register; the callers test [mega_on] first
; -----------------------------------------------------------------------------

; mega_ishif - FAR (MEGA_ISHI): AX = a value -> CF = 0 it is a POOL selector,
; an allocator LDT selector whose base is at or above 1MB; CF = 1 anything
; else. Clobbers AX
mega_ishif:
    push cx
    push dx
    test al, 4
    jz .no
    cmp ax, MEGA_LDT_FREE0 * 8
    jb .no
    call mega_selbase               ; DX:AX = its base
    jc .no
    cmp dl, MEGA_XPOOL >> 16
    jb .no
    clc
    jmp short .o
.no:
    stc
.o:
    pop dx
    pop cx
    retf

; mega_mpix - SI = a mem_tab record -> BX = its mem_sel slot's offset.
; Clobbers nothing else
mega_mpix:
    push ax
    push dx
    mov ax, si
    sub ax, mem_tab
    xor dx, dx
    mov bx, MC_SIZE
    div bx                          ; AX = the record's index
    shl ax, 1
    add ax, mem_sel
    xchg ax, bx
    pop dx
    pop ax
    ret

; mega_mpbase - SI = a record -> DX:AX = its base, CX = its limit (64KB at
; most: a block over 64KB is reached in windows, step 5.7). Clobbers nothing
; else
mega_mpbase:
    mov ax, [ss:si+MC_SEG]
    mov dx, ax
    shl ax, 4
    shr dx, 12
    mov cx, [ss:si+MC_PARA]
    cmp cx, 0x1000
    jb .l
    mov cx, 0x1000
.l:
    shl cx, 4
    dec cx                          ; (0x1000 paragraphs: 0xFFFF)
    ret

; mega_mpnewf - FAR: a pair for record SI -> CF = 0 made (or there already);
; CF = 1 no two adjacent LDT entries free, nothing changed
mega_mpnewf:
    push ax
    push bx
    push cx
    push dx
    call mega_mpix                  ; BX = the slot
    cmp word [ss:bx], 0
    jne .ok                         ; (one already: the switch's pass)
    push bx
    call mega_mpbase
    call mega_ldt_alloc2            ; AX = the code half
    pop bx
    jc .o
    add ax, 8
    mov [ss:bx], ax                 ; the DATA half is the claim's value
.ok:
    clc
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; mega_mpsyncf - FAR: record SI's base or length changed -> its pair is
; re-aimed at the block as it is now (a regrow, a re-home's trim)
mega_mpsyncf:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call mega_mpix
    mov di, [ss:bx]
    or di, di
    jz .o
    call mega_mpbase
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    and di, 0xFFF8
    pushf
    cli
    mov [es:di], cx                 ; the data half...
    mov [es:di+2], ax
    mov [es:di+4], dl
    mov [es:di-8], cx               ; ...and the code half, the same window
    mov [es:di-8+2], ax
    mov [es:di-8+4], dl
    popf
.o:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; mega_mpfreef - FAR: record SI is going -> its pair is free, and so is every
; other allocator DATA descriptor based in its block (OSAPI_SEG's SUBs, which
; go with their claim as a pool block's do: mx_dfree's rule)
mega_mpfreef:
    push ax
    push bx
    push cx
    push dx
    call mega_mpix
    xor ax, ax
    xchg ax, [ss:bx]                ; the slot is empty from here
    or ax, ax
    jz .o
    push ax
    sub ax, 8
    call mega_ldt_free              ; the code half...
    pop ax
    call mega_ldt_free              ; ...and the data half
    mov ax, [ss:si+MC_SEG]
    mov cx, [ss:si+MC_PARA]
    call mega_cdfree                ; ...and any SUB inside it
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; mega_mpmovef - FAR: the compactor moved record SI's block from paragraph AX
; to the record's MC_SEG now -> every allocator descriptor based in the OLD
; block is rebased by the move, the pair and any SUB alike (mx_desc's rule for
; a pool block). One pass with interrupts off: the copy before it is done
mega_mpmovef:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    push es
    mov cx, [ss:si+MC_PARA]         ; CX = paragraphs
    mov bx, [ss:si+MC_SEG]
    sub bx, ax                      ; BX = the move in paragraphs, signed
    mov bp, bx
    sar bp, 12
    shl bx, 4                       ; BP:BX = the move in bytes, signed
    mov dx, ax
    shl ax, 4
    shr dx, 12                      ; DL:AX = the old base
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov di, MEGA_LDT_FREE0 * 8
    pushf
    cli
.d:
    cmp byte [es:di+5], 0
    je .n
    call mega_cdin                  ; based in [DL:AX, + CX paragraphs)?
    jc .n
    add [es:di+2], bx
    push bx
    mov bx, bp
    adc [es:di+4], bl
    pop bx
.n:
    add di, 8
    cmp di, [ss:mega_ldte]
    jb .d
    popf
    pop es
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; mega_mpall - at the switch (mega_pmtail): mem_sel emptied (`.lowbss` is
; not zeroed at boot), then a pair for every claim real mode made. Clobbers
; nothing
mega_mpall:
    push ax
    push cx
    push si
    push di
    push es
    push ss
    pop es
    mov di, mem_sel
    mov cx, MEGA_MSELN
    xor ax, ax
    cld
    rep stosw
    mov si, mem_tab
.l:
    cmp word [ss:si+MC_SEG], 0
    je .n
    call MEGA_SEL_CODE:mega_mpnewf
.n:
    add si, MC_SIZE
    cmp si, mem_tab + MEM_MAX * MC_SIZE
    jb .l
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; mega_rseg - AX = a REAL-MODE segment the BIOS handed back in ES or DS (a
; parameter table at F000h, a buffer) -> AX = a selector for it: itself on
; the GDT's identity map, a claim's base its pair's data half, anything else
; mega_useg's selector over its 64KB (KERN-MEGA-MEMORY-PLAN MC7's step 6: the
; identity map stops at `.cold`, so the segment alone loads nothing). AX
; kept when there is none, and the load faults where it is. Clobbers nothing
; else
mega_rseg:
    cmp ax, MEGA_GDT_ID * 8
    jae .u
    test al, 7
    jz .r                           ; the identity map's (0 is the null one)
.u:
    push dx
    mov dx, ax
    call mega_p2d
    jc .nu
    mov ax, dx
    pop dx
.r:
    ret
.nu:
    pop dx
    jmp mega_useg

; mega_useg - AX = a real-mode segment at or above A000h (a frame buffer, a
; ROM) -> AX = the kernel's data selector over its 64KB, CF = 0: made the
; first time and the same one after (mega_ustab) - what the identity GDT used
; to answer for it before MC7.4 cut it. CF = 1, AX kept, when no LDT entry or
; row is free. Clobbers nothing else
mega_useg:
    push bx
    push cx
    push dx
    push si
    mov si, mega_ustab
.l:
    cmp [ss:si], ax
    je .hit
    cmp word [ss:si], 0
    je .new
    add si, 4
    cmp si, mega_ustab_end
    jb .l
    stc
    jmp short .o
.new:
    mov bx, ax
    mov dx, ax
    shl ax, 4
    shr dx, 12                      ; DX:AX = its base
    mov cx, 0xFFFF
    push bx
    mov bl, 0x92
    call mega_ldt_alloc
    pop bx
    jc .no
    mov [ss:si], bx
    mov [ss:si+2], ax
    clc
    jmp short .o
.no:
    mov ax, bx
    jmp short .o
.hit:
    mov ax, [ss:si+2]
    clc
.o:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mega_vword - DI = a kernel word that names a SEGMENT (DS = KDATA): a frame
; buffer's or a ROM's becomes mega_useg's selector over it, a claim's base its
; pair's data half (mega_p2d), anything else stays. Clobbers nothing
mega_vword:
    push ax
    push dx
    mov ax, [di]
    cmp ax, 0xA000
    jb .lo
    call mega_useg
    jc .o
    mov [di], ax
    jmp short .o
.lo:
    mov dx, ax
    call mega_p2d
    jc .o
    mov [di], dx
.o:
    pop dx
    pop ax
    ret

; mega_vfixall - at the switch (mega_pmtail), after mega_mpall: VSEG's four
; words become selectors, and so does every segment the video and font layers
; hold - vid_tab's column, the live block's three, each display context's
; three, the full-screen modes' rows and the glyph table's (KERN-MEGA-MEMORY-
; PLAN MC7.4). Clobbers nothing
mega_vfixall:
    push ax
    push cx
    push di
    push es
    push ss
    pop es
    mov di, mega_ustab              ; (`.lowbss` is not zeroed at boot)
    mov cx, MEGA_USTN * 2
    xor ax, ax
    cld
    rep stosw
    pop es
    mov ax, VGA_SEG
    call mega_useg
    mov [k_vsa], ax
    mov ax, 0xB000
    call mega_useg
    mov [k_vsb], ax
    mov ax, 0xB800
    call mega_useg
    mov [k_vsc], ax
    mov ax, 0xF000
    call mega_useg
    mov [k_vsf], ax
%ifdef GFX_VGA
    mov di, vid_tab
%else
    mov di, vid_tab + VID_REC       ; (row 0 is no row on a build with no
%endif                              ; VGA renderer: viddet.inc)
.t:
    call mega_vword
    add di, VID_REC
    cmp di, vid_tab_end
    jb .t
    mov di, vid_seg
    call mega_vword
    mov di, vid_rseg
    call mega_vword
    mov di, vid_tseg
    call mega_vword
    mov di, vid_ctx
    mov cx, VID_NDISP_MAX
.c:
    call mega_vword
    add di, vid_rseg - vid_seg
    call mega_vword
    add di, vid_tseg - vid_rseg
    call mega_vword
    add di, VID_CTX_SZ - (vid_tseg - vid_seg)
    loop .c
    mov di, fsx_mtab
    mov cx, FSXM_COUNT
.f:
    call mega_vword
    add di, FSXR_SIZE
    loop .f
    mov di, font_seg
    call mega_vword
%ifdef BANDCOMP
    mov di, bnd_seg
    call mega_vword
%endif
    pop di
    pop cx
    pop ax
    ret

%ifdef VID_EXT
; mega_vxpm - at the switch (mega_pmtail), after mega_mpall and mega_vfixall,
; IF = 0: the VESA desktop into protected mode (KERN-MEGA-PLAN 14.115).
; vx_boot_x armed [vx_ep] in real mode with the image's PARAGRAPH, and every
; hook far-calls through it - so before the first interrupt can draw the
; arrow, the image is told its selectors (VXV_PM: window A's and the
; kernel's data, which the boot's question already showed it takes) and the
; hook is re-pointed at its pair's CODE half.
;
; NOT THROUGH drv_call, and not from the claim walk after the switch: every
; holder still names the claim by paragraph until kmain's mem_xlatf, which
; rewrites [vx_ep+2] (a MEM_RR_ONE holder, for the compactor) to the DATA
; half - a far call through which is #GP(sel). So the pair comes from
; mega_p2d, the image is called the way drv_pkg_disp does it (DS its data
; half, code half : PKG_DISP), and once [vx_ep+2] is the code half it no
; longer matches the paragraph mem_xlatf looks for. A refusal leaves the
; hook on vx_null, which draws nothing and faults nothing. Clobbers nothing
mega_vxpm:
    cmp word [vx_ep+2], 0
    je .ret                         ; not armed: no VESA desktop
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov dx, [vx_ep+2]
    call mega_p2d                   ; DX = the image's DATA selector
    jc .off
    mov ax, VGA_SEG
    call mega_useg                  ; AX = window A's data selector
    jc .off
    mov cx, dx                      ; CX = the image's data half
    mov dx, ax                      ; DX = window A, VXV_PM's argument
    mov bp, [vesa_row+DRVR_ENT]
    mov ax, cx
    sub ax, 8
    push ax                         ; the far target: code half...
    mov ax, PKG_DISP
    push ax                         ; ...: the dispatcher
    mov si, sp
    push ds
    pop es                          ; ES = KDATA, the kernel the image banks
    push ds
    mov ds, cx                      ; DS = the image's data half
    mov al, VXV_PM
    call far [ss:si]
    pop ds
    pop ax                          ; (pops: CF is the image's answer)
    pop ax
    jc .off
    mov [vx_ep+2], ax               ; the code half
    jmp short .out
.off:
    mov word [vx_ep], vx_null       ; draws nothing, faults nothing
    mov word [vx_ep+2], KERNEL_SEG
.out:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret
%endif

; mega_cdin -ES:DI = a present descriptor, DL:AX = a block's base, CX = its
; paragraphs -> CF = 0 the descriptor's base lies in the block; CF = 1 not.
; Clobbers nothing
mega_cdin:
    push ax
    push bx
    push dx
    mov bx, [es:di+2]
    mov dh, [es:di+4]               ; DH:BX = the descriptor's base
    sub bx, ax
    sbb dh, dl                      ; DH:BX = its offset into the block
    jc .no                          ; below it
    push cx
    mov ax, cx
    mov dl, ah
    shr dl, 4
    shl ax, 4                       ; DL:AX = the block's bytes
    pop cx
    cmp dh, dl
    ja .no
    jb .yes
    cmp bx, ax
    jae .no
.yes:
    mov ax, di                      ; ...unless it is an OSAPI_SEG WINDOW:
    call mega_iswin                 ; its owner's, wherever it is based
    cmc                             ; (CF = 0 a window -> "not in")
    jmp short .o
.no:
    stc
.o:
    pop dx
    pop bx
    pop ax
    ret

; mega_iswin - AX = a descriptor's offset in the LDT (index x 8) -> CF = 0
; it is an UNBOUND OSAPI_SEG window (a mega_wtab row, byte 3 clear); CF = 1
; anything else. Clobbers nothing.
;
; A WINDOW IS ITS OWNER'S AND NOBODY ELSE'S (SPEC.md 50.6.11): only its
; owner's end or SEG_FREE frees it, and only its owner re-aims it. Its base
; is a physical paragraph the owner CHOSE, and it may lie in any block at all
; - the C64's ROM-bank windows are based BELOW its ROM part, in whatever
; claim happens to sit there (apps/c64/c64cpu.inc's c64_rbset) - so the
; free or the move of the block it happens to be based in is not its event.
; Freeing it there handed the C64 a dead selector and jammed the 6510 at
; PC 0008 the moment another package's claim was freed (KERN-MEGA-PLAN
; 14.80); rebasing it would have pointed it at the wrong bytes. A SUB, a
; SUBC pair and a BOUND window (SEG_BWIN) are the block's, and still go
; with it
mega_iswin:
    push si
    push di
    mov si, mega_wtab
.l:
    mov di, [ss:si]
    or di, di
    jz .n
    xor di, ax
    and di, 0xFFF8
    jnz .n
    cmp byte [ss:si+3], 0
    je .y                           ; (CF = 0)
.n:
    add si, 4
    cmp si, mega_wtab_end
    jb .l
    stc
.y:
    pop di
    pop si
    ret

; mega_cdfree - AX = a block's paragraph, CX = its paragraphs -> every
; allocator descriptor still present and based in it, data or code, is free. Clobbers
; nothing
mega_cdfree:
    push ax
    push dx
    push di
    push es
    mov dx, ax
    shl ax, 4
    shr dx, 12                      ; DL:AX = the base
    push ax
    mov ax, MEGA_SEL_LDTD
    mov es, ax
    pop ax
    mov di, MEGA_LDT_FREE0 * 8
.d:
    cmp byte [es:di+5], 0
    je .n
    call mega_cdin                  ; data or code: a SUBSEG twin too
    jc .n
    push ax
    lea ax, [di+4]
    call mega_ldt_free
    pop ax
.n:
    add di, 8
    cmp di, [ss:mega_ldte]
    jb .d
    pop es
    pop di
    pop dx
    pop ax
    ret

; mega_dklibf - FAR (DESK.DRV's LIB, 11.103.10): AX = DESK.DRV's code
; selector when its image is ABOVE 1MB, CX = the library's offset in it, DX =
; the library's bytes -> CF = 0 and DX = a code selector whose base is the
; library's own first byte, made once and kept in [mega_dklib] until the
; image is dropped (mega_dklibfree); CF = 1 none could be made. Its clients
; far-call it and read its magic through it, as they do the alias below 1MB.
; Clobbers nothing else
mega_dklibf:
    push ax
    push bx
    push cx
    mov bx, [mega_dklib]
    or bx, bx
    jnz .have
    pushf                           ; the image's base to the alias in one
    cli                             ; step: DESK.DRV's image moves (14.37)
    push dx
    push cx
    call mega_selbase               ; DX:AX = the image's base (CL spent)
    pop cx
    add ax, cx
    adc dx, 0
    pop cx                          ; the library's bytes
    dec cx                          ; ...as a limit
    mov bl, 0x9A
    call mega_ldt_alloc
    sbb cx, cx                      ; CX = -1: no entry
    popf
    shr cx, 1                       ; (CF back)
    jc .out
    mov [mega_dklib], ax
    xchg ax, bx
.have:
    mov dx, bx
    clc
.out:
    pop cx
    pop bx
    pop ax
    retf

; mega_dklibfree - FAR: the library's selector goes with DESK.DRV's image.
; Clobbers nothing
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_dklibfree:
    push ax
    xor ax, ax
    xchg ax, [mega_dklib]
    or ax, ax
    jz .none
    call MEGA_SEL_CODE:mega_ldt_freef
.none:
    pop ax
    retf

mega_dklib  dw 0                    ; DESK.DRV's library above 1MB, 0 = none

mega_xsrc   dw 0, 0                 ; mega_lincopy's two linear ends
mega_xdst   dw 0, 0
mega_coldhi db 0                    ; 1: `.cold` runs above 1MB
mega_mfree  db 0                    ; 1: `.mega`'s rung is heap and `.mega` is
                                    ; NOT there - protected mode was refused
mega_cpry   db 0                    ; 1: mx_alloc has compacted once this call
mega_bfpart dw 0                    ; mega_claimfreef's answer: a bridge's
                                    ; conventional piece, 0 none (MC5)
mega_bfc    dw 0                    ; mega_hugebf's: the heap piece's selector,
mega_bfp    dw 0                    ; ...the pool piece's KB,
mega_bfbase dw 0, 0                 ; ...its base,
mega_bfown  db 0                    ; ...and the owner it is claimed under
MEGA_RESKB  equ 64                  ; THE CONVENTIONAL RESERVE a bridge leaves
                                    ; (KERN-MEGA-MEMORY-PLAN D8, 2's measure)
mega_mxflr  dw 0                    ; mx_alloc's floor, KB from the pool's
                                    ; base: 64 while a DOS trip claims its park
                                    ; and arena (the HMA is kern_dos's then),
                                    ; 0 otherwise (KERN-MEGA-MEMORY-PLAN MC4)

mega_infault db 0
mega_fbuf   db " kern_mega FAULT vv err eeee at cccc:iiii sp ssss - reboot "
            db 0
MEGA_FB_V   equ 17
MEGA_FB_E   equ 24
MEGA_FB_C   equ 32
MEGA_FB_I   equ 37
MEGA_FB_S   equ 45

mega_zero8  dw 0, 0, 0, 0
mega_idtd   dw 256*8 - 1, 0         ; the IDT as DATA (GDT entry 7), for
            db 0, 0x92                          ; mega_ivt_sync (mega_tset)
            dw 0
mega_ldtd:                          ; the LDT's descriptor (GDT entry 3)
    dw 0, 0                         ; (its base and limit: mega_tset)
    db 0, 0x82
    dw 0
mega_mrm    db "kern_mega: ", 0     ; ...the debug port's line is the toast's
mega_t86    db "real mode: 8086-class CPU", 0
mega_tmem   db "real mode: no room for kern_mega's tables", 0
mega_ta20   db "real mode: A20 will not open", 0
mega_ttask  db "real mode: a task predates it", 0
%ifdef MEGA_DIAG                    ; the field build names the way BACK
mega_mtoast db "protected, 286 reset", 0
mega_mtoast3 db "protected, 386 CR0", 0
%else                               ; ...a shipped one only that it got in,
mega_mtoast db "Entered protected mode", 0 ; which is true on any CPU
%endif

section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
; mega_pmtail - the rest of the switch, once `.mega` is where MEGA_SEL_CODE
; says (mega_coldup): every task and module slot converted, the pool, the
; ROM's halves in the ISR chains, the IVT kept for a reflection, the 8259,
; and the string-restart measurement. FAR, from mega_enter
mega_pmtail:
    call mega_frames_fix
    call mega_xminit                ; the XMEM pool behind the cells
    call mega_mpall                 ; ...and every claim's pair (MC7.4)
    call mega_mods_fix              ; ...which the modules are named by
    call mega_vfixall               ; ...and the frame buffers, ROMs and font
%ifdef VID_EXT
    call mega_vxpm                  ; ...and a VESA desktop's renderer (14.115)
%endif

    ; --- the ROM's halves, in the chains the ISRs already make ---------------
    mov word [sch_old08], mega_t08  ; THROUGH MEGA_SEL_CODE: both live in
    mov word [sch_old08+2], MEGA_SEL_CODE ; `.mega`. It is not 0x0060, which
    mov word [kbm_old9], mega_k09   ; matters - kbm_isr refuses to chain to
    mov word [kbm_old9+2], MEGA_SEL_CODE ; its OWN CS (the `.selfchain`
                                    ; guard) and would swallow every key

    ; --- the IVT keeps the BIOS's own tick and keyboard, for a reflection ----
    mov ax, MEGA_SEL_LOW0
    mov es, ax
    mov ax, [mega_old08]
    mov [es:0x08*4], ax
    mov ax, [mega_old08+2]
    mov [es:0x08*4+2], ax
    mov ax, [mega_old09]
    mov [es:0x09*4], ax
    mov ax, [mega_old09+2]
    mov [es:0x09*4+2], ax
    mov ax, mega_r15                ; ...and int 15h through mega_r15, so the
    xchg ax, [es:0x15*4]            ; ROM's int 09h tells the key-state map
    mov [mega_old15], ax            ; what it took (14.119)
    mov ax, KERNEL_SEG
    xchg ax, [es:0x15*4+2]
    mov [mega_old15+2], ax
    push ds                         ; ...and a COPY of IVT 08h-0Fh at 20h-27h:
    push cx                         ; the 8259 stays at its protected-mode base
    push es                         ; through every reflection (mega_svc), so
    pop ds                          ; an IRQ the BIOS takes in real mode comes
    mov si, 0x08*4                  ; in on 20h-27h. Those are DOS's vectors,
    mov di, MEGA_PIC_BASE*4         ; which nothing here has
    mov cx, 16
    cld
    rep movsw
    pop cx
    pop ds
    push ds
    pop es
    call mega_ivt_sync  ; its copy of the IRQ vectors, from now

    call KERNEL_SEG:mega_pic_pmf    ; IRQ0-7 to 20h-27h
    in al, 0x70                     ; NMI back on
    and al, 0x7F
    out 0x70, al
    sti
    call mega_strcal                ; how this CPU restarts a faulted string
                                    ; write (KERN-MEGA-PLAN 10.3 item 54.1),
                                    ; before anything can fault one
                                    ; (the boot's FAT windows and read-ahead
                                    ; store go above 1MB in mem_hicaches_f,
                                    ; AFTER mem_xlatf: freed here, by
                                    ; paragraph, they were not - KERN-MEGA-
                                    ; PLAN 14.121)
    mov si, mega_mon
    call mm_say
    push es                         ; ...and on the GLASS, for a field machine
    push ds                         ; with no debug port: the menu bar says it
    pop es                          ; for a few seconds (toast_show only
    mov si, mega_mtoast             ; stages; the UI task's first idle pass
%ifdef MEGA_DIAG
    cmp byte [cpu_tier], CPU_386    ; ...naming the way BACK it will use,
    jb .t286                        ; which is the thing a field run is for
    mov si, mega_mtoast3
.t286:
%endif
    mov cx, 146                     ; draws it), for eight seconds: long enough
                                    ; to photograph
    call KERNEL_SEG:cw_toast_show
    pop es
    retf

section .text

; -----------------------------------------------------------------------------
; THE DOORS BETWEEN `.text` AND `.mega` (KERN-MEGA-SEGMENTS-PLAN 3.6). A body
; called both near, from its own segment, and from the other one is entered
; there through four bytes of `call / retf`. tools/os88ovlchk.py refuses a near
; call across and a far call naming the wrong segment, which is what makes
; this list complete.
; -----------------------------------------------------------------------------
mega_xdescf:                        ; low bodies `.mega` calls...
    call mega_xdesc
    retf
mega_putsf:                         ; (a DS string: the kernel's CS reads it)
    call mega_puts
    retf
mega_pic_pmf:
    call mega_pic_pm
    retf
mega_irqromf:
    call mega_irqrom
    retf
mega_idt_gatef:
    call mega_idt_gate
    retf
section .mega                       ; ...and `.mega` bodies low code calls,
mega_strcalf:                       ; every one of them in protected mode
    call mega_strcal
    retf
mega_fd13f:
    call mega_fd13
    retf
mega_selbasef:
    call mega_selbase
    retf
mega_p2df:
    call mega_p2d
    retf
mx_relallm:
    call mx_relall
    retf
mega_ldt_freef:
    call mega_ldt_free
    retf
mega_tellf:
    call mega_tell
    retf
mega_ivt_syncm:
    call mega_ivt_sync
    retf
mega_xmwalkf:
    call mega_xmwalk
    retf
; mega_imgnf - FAR (osapi_img_name's BX = 1 arm, KERN-MEGA-PLAN 14.70.3): AX =
; a pool block's KB offset -> CF = 0, SI = the segment word of the drv_tab,
; ss_row or mod_tab row whose image is based there, AL = 0 a driver / 1 a
; module; CF = 1 none. mega_xmcls's match - the row's selector, its LDT base,
; mx_rel - over every row that names an image. DS = KDATA. Keeps the rest
mega_imgnf:
    push bx
    push cx
    push dx
    mov bx, ax                      ; BX = the offset wanted
    mov si, drv_tab + DRVR_SEG
.d:
    call .is
    je .drv
    add si, DRVR_SIZE
    cmp si, drv_tab + DRVR_SEG + DRV_MAX * DRVR_SIZE
    jb .d
    mov si, ss_row + DRVR_SEG
    call .is
    je .drv
    mov si, mod_tab + MODR_SEG
.m:
    call .is
    je .mod
    add si, MODR_SIZE
    cmp si, mod_tab_end
    jb .m
    mov ax, bx
    stc
    jmp short .out
.drv:
    mov al, 0
    jmp short .ok
.mod:
    mov al, 1
.ok:
    clc
.out:
    pop dx
    pop cx
    pop bx
    retf
; .is - ZF = 1 the selector at [SI] names an image based at BX KB from the
; pool's base. Clobbers AX, CL, DX
.is:
    mov ax, [si]
    test al, 4                      ; an LDT selector: an image above 1MB
    jz .isno
    call mega_selbase               ; DX:AX = its base (CL spent)
    jc .isno
    call mx_rel                     ; AX = KB from the pool's base
    jc .isno
    cmp ax, bx
    ret
.isno:
    or si, si                       ; (SI is never 0: ZF = 0)
    ret
mega_codeself:
    call mega_codesel
    retf
mega_bpcall:                        ; xm_dsp's: BP = a `.mega` routine
    call bp
    retf
section .text

%include "megados.inc"          ; M6: DOS's park (KERN-MEGA-DOS-PLAN)

section .bss
mega_nmi61  resb 1                  ; port 61h at the last latched NMI
mega_fdbusy resb 1                  ; mega_fdgo is running
mega_irqr   resb 1                  ; mega_svc is reflecting an IRQ
mega_nmin   resb 1                  ; ...how many, saturating at the cap's use
%ifdef MEGA_DIAG                    ; the menu bar's field readings (14.65)
mega_nmics  resw 1                  ; ...and the CS:IP it interrupted
mega_nmifd  resb 1                  ; 1: a native floppy transfer was in
mega_nmiop  resb 1                  ; flight - its op, DMA address and
mega_nmilin resb 3                  ; sector count
mega_nmisn  resb 1
mega_rft0   resw 1                  ; PIT ch0 at an IRQ reflection's start
mega_rfsum  resw 2                  ; ...this second's total counts
mega_rfn    resw 1                  ; ...and how many
mega_rfr    resw 1                  ; the last second shown: how many
mega_rfus   resw 1                  ; ...and their average, in us
mega_rfhold resb 1                  ; seconds before the next is shown
mega_pmseg  resw 1                  ; this IRQ's ROM segment, 0 = none
mega_pmaud  resb 1                  ; it had an audio request pending
mega_pmq    resb 6                  ; this second: DMA copies, audio IRQs,
                                    ; other, audio with nothing raised, the
                                    ; IMR, replays (mega_pmpre/post,
                                    ; mega_replay)
mega_pmqs   resb 6                  ; ...the second last shown
mega_nmiip  resw 1
mega_rstn   resw 1                  ; 286 reflections (resets), for E9
mega_fdrst  resb 2                  ; 765 resets found: at mega_r286, and
                                    ; at mega_fdgo (mega_fdrprobe)
%endif
mega_tellq  resb 1                  ; lines ui_task owes the menu bar
                                    ; (mega_tell)
section .lowbss
mega_pcj    resw MAX_TASKS          ; each task's innermost CATCH, 0 = none
                                    ; (mega_pcin_*, KERN-MEGA-PLAN 14.42)
mega_fkq    resw MEGA_FKINST        ; per slot: a faulted instance's region,
                                    ; owed a close by ui_task (mega_fkdo)
mega_fkmsg  resb MEGA_FKMSG         ; its toast
mega_fkt    resw 1                  ; [ticks] when the last incident began
%ifndef NO_KBDWD
mega_kwdt   resw 1                  ; [ticks] at the last stranded 8042 byte
%endif                              ; seen (mega_kwd): .lowbss, so not one
                                    ; byte of the 64KB segment
mega_fks    resw 1                  ; ...its instance's region
mega_fkslot resb 1                  ; ...and slot + 1, 0 = none being closed
mega_fkn    resb 1                  ; faults in it (0 = none ever)
mega_fktm   resb 1                  ; 1 = its toast is owed
mega_seg0   resw 1                  ; MEGA_SEG0's word for code outside .text
MEGA_USTN   equ 8                   ; mega_useg's selectors over real segments
mega_ustab  resw MEGA_USTN * 2      ; at or above A000h: {segment, selector}
mega_ustab_end equ mega_ustab + MEGA_USTN * 4 ; a row, 0 a free one
MEGA_MSELN  equ 32                  ; mem_tab's MEM_MAX (memory.inc checks)
mem_sel     resw MEGA_MSELN         ; a conventional claim's DATA selector,
                                    ; by mem_tab index, 0 = none (its CODE
                                    ; half is - 8): KERN-MEGA-MEMORY-PLAN MC7.4
MEGA_WTN    equ 32                  ; OSAPI_SEG's windows (mega_segf): a row
mega_wtab   resb MEGA_WTN * 4       ; each, the selector and its owner byte,
mega_wtab_end equ mega_wtab + MEGA_WTN * 4 ; 0 a free row (KERN-MEGA-MEMORY-PLAN MC7)
mx_tab      resb MX_NBLK * MX_BLKSZ ; THE POOL'S BLOCK TABLE (KERN-MEGA-PLAN
mx_tab_end  equ mx_tab + MX_NBLK * MX_BLKSZ ; 14.49): in LOW_SEG beside mem_tab,
                                    ; read through SS (SPEC.md 2.1: SS is
                                    ; LOW_SEG everywhere the kernel runs), so
                                    ; its size costs no byte of the 64KB
                                    ; kernel segment. Zeroed by mega_xminit:
                                    ; .lowbss is not zero at boot
mega_pm     resb 1                  ; 1 = protected mode, for .cold (KDATAR_C):
                                    ; .lowbss is zeroed, which is the right
                                    ; answer before mega_enter
mega_ldte   resw 1                  ; [mega_ldtend] where any DS reads it: the
mega_ldtl   resw 1                  ; allocator scans' end, and that less one
                                    ; entry (mega_layoutf, MC7's step 6)
section .text
cpu 8086
%endif
