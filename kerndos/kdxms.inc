; =============================================================================
; kerndos/kdxms.inc - THE XMS DOOR ON kern_mega (SPEC.md 96.15.4,
;                     docs/plans/KERN-MEGA-DOS-PLAN.md 6)
;
; **kern_mega ONLY, AND THE WHOLE FILE IS.** `kdos.asm` includes it under
; `%ifdef KD_MEGA`; on every other build `kdback.inc`'s four XMS doors stay
; `stc`/`ret`, because an 8088 has no store and the probe would be resident
; bytes against the 39KB (docs/plans/KERN-DOS-PLAN.md 1).
;
; What the kernel hands over is ONE block above 1MB - the XMS ARENA,
; `KDL_XBASE`/`KDL_XKB` in the launch block's kern_mega tail - which is
; everything the pool had left after the park (12.1 item 7). The kernel is
; not running while DOS runs and nothing else can use it, so this file is the
; arena's only owner for the length of the bracket and its allocations die
; with it, which is XMS's own rule for a program that exits without freeing.
;
; THREE PIECES, each the box's contract for the same door (SPEC.md 96.15):
;
;   - the ALLOCATOR: a table of KDX_N = XMS_NH (offset KB, size KB) rows over
;     the arena, first fit, 1KB granular. The table is the HOLES' complement,
;     so there is no free list to coalesce - a freed row is a gap and the next
;     first-fit walk sees the gap. It is `kernel/mega.inc`'s `mx_alloc` shape
;     and not its code: that file is another lane's, and the arena is not the
;     pool (no owner byte, no instance to force-free, one claimant);
;   - the TRANSPORT: `int 15h AH=87h`, which every AT BIOS has. It is called
;     through the ROM's own vector saved at the hook, so the filter below
;     never sees kern_dos's own moves;
;   - the FILTER: `int 15h` is hooked for the program. AH=88h answers 0 - no
;     raw extended memory - and AH=87h is refused (CF=1, AH=03h) when either
;     end touches anything at or above 1MB outside the arena. Everything else
;     chains to the ROM. KERN-MEGA-DOS-PLAN 5.2 is the table this implements:
;     the park, the kernel's tables and `.cold` are above 1MB in the same
;     machine, and a raw move is the one way a well-behaved program could
;     reach them.
;
; **THE ARENA'S CONTRACT, which the kernel side owes:** a 32-bit linear base
; at or above 100000h with its low ten bits zero (1KB aligned), and a size in
; KB. Neither zeroing nor any other alignment is needed - XMS promises a
; program nothing about a fresh block's contents. A base that fails the
; checks, or KB = 0, is NO ARENA: `AH=4300h` then answers "no driver", which
; is SPEC.md 96.15.1's rule. Whatever lies at or above 16MB is CUT OFF here:
; `AH=87h`'s descriptors carry a 24-bit base on a 286 and the 386 byte at
; +17h/+1Fh is not honoured by every BIOS, so a move up there is a move to
; the wrong address on some machines.
; =============================================================================

KDX_N       equ XMS_NH              ; one row per handle the core can open: the
                                    ; core is the allocator's only caller, so it
                                    ; can never ask for a ninth block
KDX_ROW     equ 4                   ; dw offset KB from the base, dw size KB
                                    ; (0 = a free row)
KDX_INT     equ 0x15 * 4            ; the vector we filter

section .text

; -----------------------------------------------------------------------------
; kdx_init - the arena out of the launch block, and the int 15h hook
; in:  [kd_lbp] = the block; out: nothing, every register preserved
;
; **BEFORE `dos_save_machine`**, kd_kbd_start's reason: the whole IVT is what
; `dos_restore_machine` puts back, so a vector taken after the bank is one the
; program's own exit silently undoes.
; -----------------------------------------------------------------------------
kdx_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov si, [kd_lbp]
    xor bx, bx                      ; no arena unless the base is sane
    mov ax, [si+KDL_XBASE]
    mov dx, [si+KDL_XBASE+2]
    test ax, 0x03FF                 ; 1KB aligned...
    jnz .none
    cmp dx, 0x0010                  ; ...at or above 1MB...
    jb .none
    mov bx, 0x0100                  ; ...and below 16MB, where AH=87h's
    sub bx, dx                      ; 24-bit descriptors stop
    jbe .nobase
    mov [kdx_base], ax
    mov [kdx_base+2], dx
    mov cl, 6
    shl bx, cl                      ; KB from DX's 64KB step to 16MB...
    mov cl, 10
    shr ax, cl
    sub bx, ax                      ; ...less the base's KB inside that step
    mov ax, [si+KDL_XKB]
    cmp ax, bx
    ja .none                        ; the arena runs past 16MB: cut at it
    xchg ax, bx
    jmp short .none
.nobase:
    xor bx, bx
.none:
    mov [kdx_kb], bx

    xor ax, ax
    mov es, ax
    pushf
    cli                             ; two words, and an interrupt between them
    mov ax, [es:KDX_INT]            ; would hand the next caller half an address
    mov [kdx_old15], ax
    mov ax, [es:KDX_INT+2]
    mov [kdx_old15+2], ax
    mov word [es:KDX_INT], kdx_i15
    mov [es:KDX_INT+2], cs
    popf
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kdx_stop - the ROM's int 15h back, from `kd_leave` beside `kd_kbd_stop`
;
; Before `kd_resume` and anything that follows it: our handler sits at a
; KD_SEG offset that is the next owner's code the instant it loads, and the
; way back moves the PARK - which is exactly what the filter refuses. A
; second call is a no-op, `kd_kbd_stop`'s rule.
; -----------------------------------------------------------------------------
kdx_stop:
    push ax
    push es
    cmp word [kdx_old15+2], 0
    je .none                        ; never hooked, or already put back
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [kdx_old15]
    mov [es:KDX_INT], ax
    mov ax, [kdx_old15+2]
    mov [es:KDX_INT+2], ax
    mov word [kdx_old15+2], 0
    popf
.none:
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; kdx_i15 - the program's int 15h
;
; ON THE PROGRAM'S STACK, with DS the program's: every memory reference is
; `cs:`, and the flags the caller gets are the ones the gate pushed with CF
; changed in place, so IF comes back as the program had it.
; -----------------------------------------------------------------------------
kdx_i15:
    cmp ah, 0x87
    je .move
    cmp ax, 0xE801                  ; THE OTHER WAYS TO ASK how much there is
    je .unsup                       ; (KERN-MEGA-DOS-PLAN 10.4): E801h and its
    cmp ax, 0xE881                  ; 32-bit twin, E820h's map and the PS/2's
    je .unsup                       ; C7h would report the real extended size
    cmp ax, 0xE820                  ; and say nothing of who owns it - so they
    je .unsup                       ; are unsupported here, as on an AT that
    cmp ah, 0xC7                    ; predates them, and a program falls back
    je .unsup                       ; to 88h's 0 and takes XMS
    cmp ah, 0x8A
    je .unsup
    cmp ah, 0x88
    jne .rom
    xor ax, ax                      ; AH=88h: NO extended memory to take raw.
    push bp                         ; HIMEM.SYS answers the same, and a program
    mov bp, sp                      ; that honours it takes XMS instead
    and byte [bp+6], 0xFE           ; CF = 0 in the frame
    pop bp
    iret
.move:
    push bp
    mov bp, sp
    push ax
    push bx
    push dx
    cmp cx, 0x8000                  ; the call's own ceiling, 64KB: a longer
    ja .no                          ; count is not a move this can bound
    mov bx, 0x12                    ; the SOURCE descriptor's base (+10h, +2)
    call .end
    jc .no
    mov bx, 0x1A                    ; ...and the DESTINATION's (+18h, +2)
    call .end
    jc .no
    pop dx
    pop bx
    pop ax
    pop bp
.rom:
    jmp far [cs:kdx_old15]
.unsup:
    mov ah, 0x86                    ; "function not supported"
    push bp
    mov bp, sp
    or byte [bp+6], 1               ; CF = 1 in the frame
    pop bp
    iret
.no:
    pop dx
    pop bx
    pop ax
    mov ah, 0x03                    ; the BIOS's own "A20 failed": a code every
    or byte [bp+6], 1               ; AT BIOS defines for "the move did not
    pop bp                          ; happen", and that a program reads as no
    iret                            ; memory rather than as a retry
.end:
    ; ES:SI+BX = a descriptor's base field; CX = the words
    cmp byte [es:bx+si+5], 0        ; the 386's base 31..24 byte. Nonzero is a
    jne .bad                        ; move above 16MB, or one a 286 BIOS would
                                    ; do at a DIFFERENT address - refused either
                                    ; way, the arena being cut at 16MB
    mov ax, [es:bx+si]
    mov dl, [es:bx+si+2]
    xor dh, dh                      ; DX:AX = the 24-bit base
    jmp short kdx_where
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; kdx_where - may a raw move touch [start, start + 2 * CX)?
; in:  DX:AX = start (linear), CX = WORDS (<= 8000h)
; out: CF=0 the range is wholly below 1MB or wholly inside the arena; CF=1
;      it touches something else. Clobbers AX, BX, DX; reads through CS
;
; Below 1MB is the program's own machine and the IVT's - a real-mode program
; can write there with a `mov` anyway. The HMA is NOT below 1MB here: it is
; kern_dos's from wave 4 (KERN-MEGA-DOS-PLAN 7) and XMS reports none.
; -----------------------------------------------------------------------------
kdx_where:
    push dx                         ; is the start below the arena's base?
    push ax
    sub ax, [cs:kdx_base]
    sbb dx, [cs:kdx_base+2]
    sbb bx, bx                      ; BX = -1 if it is, 0 if not
    pop ax
    pop dx
    push bx
    mov bx, cx
    shl bx, 1                       ; the bytes, bit 16 in CF (CX = 8000h)
    adc dx, 0
    add ax, bx
    adc dx, 0                       ; DX:AX = the end, exclusive
    pop bx
    cmp dx, 0x0010
    jb .ok                          ; ends below 1MB: conventional memory
    ja .hi
    or ax, ax
    jz .ok                          ; ...or exactly at it
.hi:
    or bx, bx
    jnz .bad                        ; starts below the arena and ends above 1MB
    sub ax, [cs:kdx_base]
    sbb dx, [cs:kdx_base+2]
    call kdx_l2k                    ; the end, in whole KB from the base
    cmp ax, [cs:kdx_kb]
    ja .bad
.ok:
    clc
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; kdx_l2k / kdx_l2kf - DX:AX bytes (< 64MB) to KB, rounded up / down
; out: AX = KB; clobbers DX, preserves CX
; -----------------------------------------------------------------------------
kdx_l2k:
    add ax, 1023
    adc dx, 0
kdx_l2kf:
    push cx
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; kdx_k2l - AX = KB from the arena's base; out DX:AX = that linear address
; -----------------------------------------------------------------------------
kdx_k2l:
    mov dx, 1024
    mul dx
    add ax, [kdx_base]
    adc dx, [kdx_base+2]
    ret

; -----------------------------------------------------------------------------
; kdx_inblk - is [DX:AX, DX:AX + CX) inside ONE block the program holds?
; in:  DX:AX = linear, CX = bytes; out CF=0 yes; clobbers AX, BX, DX
;
; EXACT IN KB: every block starts and ends on a KB boundary, so the start
; rounded DOWN and the end rounded UP fall inside a block exactly when the
; bytes do. The 16MB test comes first because KB is a 16-bit answer and a
; wrapped offset (the core adds a program's 32-bit offset to a base) must not
; alias back into a block.
; -----------------------------------------------------------------------------
kdx_inblk:
    sub ax, [kdx_base]
    sbb dx, [kdx_base+2]
    jb .bad                         ; below the arena, or wrapped
    cmp dx, 0x0100
    jae .bad
    push ax
    push dx
    add ax, cx
    adc dx, 0
    call kdx_l2k
    mov bx, ax                      ; BX = the end's KB, rounded up
    pop dx
    pop ax
    call kdx_l2kf                   ; AX = the start's, rounded down
    push si
    mov si, kdx_tab
.scan:
    cmp word [si+2], 0
    je .next
    cmp ax, [si]
    jb .next
    mov dx, [si]
    add dx, [si+2]
    cmp bx, dx
    jbe .yes
.next:
    add si, KDX_ROW
    cmp si, kdx_tab_end
    jb .scan
    pop si
.bad:
    stc
    ret
.yes:
    pop si
    clc
    ret

; -----------------------------------------------------------------------------
; THE FOUR DOORS (OSAPI_XMEM_*'s contracts, apps/os88api.inc; SPEC.md 96.15)
; -----------------------------------------------------------------------------

; out: AX = KB the arena can still hand out (0 = none), DX:CX = its base,
;      BL = free rows, DI = the LARGEST free run in KB (XMS 08h's AX, which
;      is not the total once a block sits in the middle); CF=0
dos_k_xcaps:
    push si
    mov ax, [kdx_kb]
    xor bl, bl
    mov si, kdx_tab
.scan:
    sub ax, [si+2]
    cmp word [si+2], 0
    jne .next
    inc bl
.next:
    add si, KDX_ROW
    cmp si, kdx_tab_end
    jb .scan
    call kdx_largest
    mov cx, [kdx_base]
    mov dx, [kdx_base+2]
    pop si
    clc
    ret

; kdx_largest - DI = the largest free run in the arena, KB. A run starts at 0
; or at a block's end and stops at the nearest block start past it, or the
; arena's end. Clobbers SI; every other register kept
kdx_largest:
    push ax
    push bx
    push cx
    xor di, di
    mov si, kdx_tab - KDX_ROW       ; SI = the block whose END is the start
.cand:                              ; (the first pass: 0)
    xor bx, bx
    cmp si, kdx_tab - KDX_ROW
    je .have
    cmp word [si+2], 0
    je .ncand
    mov bx, [si]
    add bx, [si+2]                  ; BX = the candidate start
.have:
    mov cx, [kdx_kb]                ; CX = where the run stops
    push si
    mov si, kdx_tab
.lim:
    cmp word [si+2], 0
    je .lnext
    mov ax, [si]
    cmp ax, bx
    jb .inside                      ; a block starting below: is BX inside it?
    cmp ax, cx
    jae .lnext
    mov cx, ax                      ; a nearer start past it
    jmp short .lnext
.inside:
    add ax, [si+2]
    cmp ax, bx
    jbe .lnext
    mov cx, bx                      ; BX is inside a block: a run of nothing
.lnext:
    add si, KDX_ROW
    cmp si, kdx_tab_end
    jb .lim
    pop si
    sub cx, bx
    jbe .ncand
    cmp cx, di
    jbe .ncand
    mov di, cx
.ncand:
    add si, KDX_ROW
    cmp si, kdx_tab_end
    jb .cand
    pop cx
    pop bx
    pop ax
    ret

; in:  DX:AX = bytes, rounded up to 1KB
; out: CF=0 DX:AX = the block's linear base; CF=1 AX = 0 no store / 1 no free
;      row / 2 no run that big
dos_k_xalloc:
    push bx
    push cx
    push si
    push di
    cmp word [kdx_kb], 0
    je .nostore
    cmp dx, 0x0100                  ; 16MB or more: never, and keeps the KB
    jae .norun                      ; figure in 16 bits
    call kdx_l2k
    xchg ax, cx                     ; CX = KB wanted
    or cx, cx
    jnz .slot
    inc cx                          ; XMS allows a 0KB handle; it gets one KB
.slot:
    mov di, kdx_tab - KDX_ROW
.free:
    add di, KDX_ROW
    cmp di, kdx_tab_end
    jae .full
    cmp word [di+2], 0
    jne .free
    xor bx, bx                      ; the candidate, KB from the base
.restart:
    mov dx, bx
    add dx, cx
    cmp dx, [kdx_kb]
    ja .norun
    mov si, kdx_tab
.scan:
    mov ax, [si+2]
    or ax, ax
    jz .snext
    add ax, [si]                    ; AX = this block's end
    cmp bx, ax
    jae .snext                      ; the candidate starts past it
    cmp [si], dx
    jae .snext                      ; ...or it starts past the candidate
    xchg bx, ax                     ; overlap: try again just past it
    jmp short .restart
.snext:
    add si, KDX_ROW
    cmp si, kdx_tab_end
    jb .scan
    mov [di], bx
    mov [di+2], cx
    xchg ax, bx
    call kdx_k2l                    ; DX:AX = the base; CF=0 (no carry out of
    jmp short .out                  ; a sum below 16MB)
.norun:
    mov al, 2
    jmp short .fail
.full:
    mov al, 1
    jmp short .fail
.nostore:
    xor al, al
.fail:
    cbw                             ; AX = the code, mx_alloc's three
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

; in:  DX:AX = a base this table handed out; out CF=0 freed, CF=1 not a base
dos_k_xfree:
    push ax
    push dx
    push si
    sub ax, [kdx_base]
    sbb dx, [kdx_base+2]
    jb .bad
    test ax, 0x03FF
    jnz .bad
    cmp dx, 0x0100
    jae .bad
    call kdx_l2kf                   ; AX = KB from the base, exactly
    mov si, kdx_tab
.scan:
    cmp word [si+2], 0
    je .next
    cmp [si], ax
    jne .next
    mov word [si+2], 0              ; the row is a gap now, and first fit sees it
    clc
    jmp short .out
.next:
    add si, KDX_ROW
    cmp si, kdx_tab_end
    jb .scan
.bad:
    stc
.out:
    pop si
    pop dx
    pop ax
    ret

; in:  ES:SI = the conventional end, or with DI = 2 a LINEAR source (ES its
;      high word - SPEC.md 96.15.4's extended-to-extended move), DX:AX = the
;      extended end (linear), CX = bytes (even, <= 32768), DI = 0 out to
;      extended / 1 in from extended / 2 extended to extended
; out: CF=0 done; CF=1 AX = 1 no store (or the BIOS refused) / 2 a range not
;      inside a block, or a conventional end past 1MB / 3 bad length.
;      Every register but AX preserved
dos_k_xcopy:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, 1
    cmp word [kdx_kb], 0
    je .fail
    mov bl, 3
    test cl, 1
    jnz .fail
    cmp cx, 0x8000
    ja .fail
    jcxz .done
    mov bl, 2
    mov [kdx_dst], ax               ; the extended end, which has to be inside
    mov [kdx_dst+2], dx             ; a block
    call kdx_inblk
    jc .fail2
    mov ax, si                      ; ...and the other one
    mov dx, es
    cmp di, 2
    jne .conv
    mov [kdx_src], ax
    mov [kdx_src+2], dx
    call kdx_inblk
    jnc .go
.fail2:
    mov bx, 2
    jmp short .fail
.conv:
    push cx                         ; ES:SI -> linear
    mov cl, 4
    rol dx, cl
    mov bx, dx
    and dx, 0x000F
    and bx, 0xFFF0
    add ax, bx
    adc dx, 0
    pop cx
    mov [kdx_src], ax
    mov [kdx_src+2], dx
    add ax, cx                      ; ...and it ENDS at or below 1MB: FFFF:0010
    adc dx, 0                       ; and up is the HMA and then the park's
    cmp dx, 0x0010                  ; kernel tables, which a seg:off can reach
    jb .dir
    ja .fail2
    or ax, ax
    jnz .fail2
.dir:
    cmp di, 1
    jne .go
    mov ax, [kdx_src]               ; IN from extended: the extended end is
    xchg ax, [kdx_dst]              ; the source
    mov [kdx_src], ax
    mov ax, [kdx_src+2]
    xchg ax, [kdx_dst+2]
    mov [kdx_src+2], ax
.go:
    call kdx_87
    mov bx, 1
    jc .fail
.done:
    clc
    jmp short .out
.fail:
    xchg ax, bx
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; kdx_87 - the move itself: int 15h AH=87h through the ROM's own vector
; in:  [kdx_src], [kdx_dst] = linear ends below 16MB, CX = bytes (even,
;      1..32768); out: CF and AH as the BIOS answered
;
; THE ROM'S VECTOR AND NOT `int 15h`, so the filter above never sees a move
; of ours and a program that hooked int 15h after us is not in our path
; either. The table is in `.bss`: the four descriptors the BIOS fills (08h,
; 20h, 28h) it rewrites every call and the null one at 00h is zeroed at entry
; and never written, so only the two ends are built here.
; -----------------------------------------------------------------------------
kdx_87:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld                             ; the program's DF is not ours
    mov si, kdx_src
    mov di, kdx_gdt + 0x10
    call kdx_desc                   ; +10h the source...
    call kdx_desc                   ; ...+18h the destination
    shr cx, 1                       ; words
    mov si, kdx_gdt
%ifdef KD_HMA
    ; THE ROM RETURNS THROUGH A LOW SLOT (KERN-MEGA-DOS-PLAN 10.5 defect 2):
    ; an IBM-AT-style ROM turns A20 OFF at the end of a block move, and an
    ; iret straight back to FFFF:next would then run wrapped low memory. So
    ; the frame its iret pops names kdh_s87, which proves A20 (reopening it if
    ; it must) and far-jumps to .back - its FLAGS, CF and all, carried through
    ; the check untouched
    call kdh_lowcode
    pushf
    mov ax, LOW_SEG
    push ax
    mov ax, kdh_s87
    push ax
    mov ah, 0x87
    jmp far [kdx_old15]
.back:
%else
    mov ah, 0x87
    pushf
    call far [kdx_old15]
%endif
    pop es
    pop di
    pop si
    pop cx
    ret

; kdx_desc - ES:DI = an 8-byte descriptor to write, DS:SI = its linear base
; (advanced by 4, DI by 8): limit FFFFh, a present data segment
kdx_desc:
    mov ax, 0xFFFF
    stosw                           ; +0 the limit
    movsw                           ; +2 base 15..0
    lodsw                           ; AL = 23..16, AH = 31..24
    stosb                           ; +4
    mov al, 0x93
    stosb                           ; +5 access: present, ring 0, data, RW
    xor al, al
    stosb                           ; +6
    mov al, ah
    stosb                           ; +7 the 386's base 31..24 (zero here)
    ret

section .bss
kdx_old15:  resd 1                  ; the ROM's int 15h; segment 0 = not hooked
kdx_base:   resd 1                  ; the arena's linear base
kdx_kb:     resw 1                  ; ...and its KB, 0 = no arena
kdx_src:    resd 1                  ; THESE TWO ARE ADJACENT AND IN THIS ORDER:
kdx_dst:    resd 1                  ; kdx_87 walks them with one SI
kdx_tab:    resb KDX_N * KDX_ROW
kdx_tab_end:
kdx_gdt:    resb 0x30               ; AH=87h's six descriptors
section .text
