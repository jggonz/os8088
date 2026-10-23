; =============================================================================
; drivers/hdd/hdxms.inc - rung 1's READ straight into XMS, on a 386
; (SPEC.md 52.1.4; docs/plans/VIDEO-386-PLAN.md section D,
; docs/plans/LOADALL-PLAN.md 4.2)
;
; A video stream banked or held in XMS crossed conventional memory on the
; way in: the disk into a BOUNCE buffer, the bounce copied up. Rung 1 moves
; every byte itself with `rep insw`, so the bounce is not needed by the
; drive at all - only by the layers above, which carry a segment and not a
; 32-bit address. So a package ARMS this driver for one buffer: "a sector
; this rung would put HERE, put THERE instead", and nothing between the
; package and the instruction changes. A sector redirected is ES's hidden
; base loaded at its XMS address (apps/os88ub.inc) around the one
; `rep insw`, interrupts off for that sector - ~0.25 ms on an ISA disk.
;
; THE MAP MAKES IT EXACT, whatever reached the buffer by other roads: a
; sector the kernel's cache served (SPEC.md 18.95), a disk on rung 0, a
; read this rung did not see - each lands in the real bounce as it always
; did, and its bit stays clear. DISARM hands the package the map, and the
; package copies up only what is clear. Nothing is written anywhere nothing
; owns: an armed range is a buffer the package owns, and a sector outside it
; is read exactly as before.
;
; TIER 2 ONLY, and refused below: a 286's LOADALL borrow is ~150 us, against
; the ~90 us a sector's copy up costs it, so a borrow a sector would be
; slower than the copy it replaces. LOADALL-PLAN 4.2's design - a borrow
; spanning a RUN of sectors, polling DRQ inside it - is the 286's, and stays
; parked with L2.
;
; A20: probed at every ARM (a shut gate aliases a WRITE 1 MB down into
; conventional memory, without a fault). Nothing the arm covers calls the
; BIOS's block move, which is the one thing in the tree that might shut it.
;
; WRITES ARE NEVER REDIRECTED, and that is the door's whole safety
; argument: DSV_PKGCALL reaches two verbs, neither of them a sector, so no
; package can reach DSV_BLK through it (SPEC.md 20.11).
; =============================================================================

hd_xa_on:    db 0               ; 1: a buffer is armed
hd_xa_seg:   dw 0               ; ...its SEGMENT (offset 0): hd_xa_read makes
                                ; it linear, in the 386 half, where that is
                                ; one instruction and not a shift pair
hd_xa_lim:   dw 0               ; ...its bytes less one sector
hd_xa_lin:   dd 0               ; ...the XMS address its first byte stands for
hd_xa_map:   times 8 db 0       ; ...a bit a sector redirected, 64 of them

; -----------------------------------------------------------------------------
; hd_pkg - DSV_PKGCALL: the package door (SPEC.md 20.11, 52.1.4). BL = the
; verb, DS = ours, ES = the calling package's segment. Names its verbs and
; refuses everything else (SPEC.md 51.2.2). UI-task context: the caller is
; about to read a file, which a worker may not (SPEC.md 20.6 rule 7)
; -----------------------------------------------------------------------------
hd_pkg:
    cmp bl, HDV_ARM
    je hd_xa_arm
    cmp bl, HDV_DISARM
    je hd_xa_dis
    cmp bl, HDV_TOOL            ; ...and the INSTALLER (SPEC.md 117.4): one
    jne .no                     ; tool verb, CL = an HDI_*, ES = the caller,
    mov al, HDT_PKG             ; forwarded by the page callbacks' own thunk,
    jmp hd_cp_thunk             ; which loads the tool and banks the registers
.no:                            ; across the load. Still no sector: the tool's
    xor ax, ax                  ; verbs are the Install window's, never hd_raw
    stc                         ; (OSAPI_DRV_CALL's own refusal: AX = 0)
    ret

; hd_xa_arm - HDV_ARM: DX = the buffer's segment (offset 0), CX = its bytes
; (a whole number of sectors, 512..32768), DI:SI = the XMS linear address
; its first byte stands for. out CF = 0 armed, the map clear; CF = 1 and
; AX = 1 not this machine (below a 386), 2 the A20 gate is shut, 3 bad
; arguments. Clobbers AX
hd_xa_arm:
    mov byte [hd_xa_on], 0
    call OSAPI_CPU_INFO         ; AL = the tier, AH the flags
    test ah, CPU_F_PM           ; KERN_MEGA in protected mode: no loaded
    jnz .pm                     ; base exists there - hd_xa_a20 alone loads
    cmp al, CPU_386             ; DS = FFFFh, a #GP (KERN-MEGA-PLAN 14.16)
    mov ax, 1
    jb .no
    mov al, 3
    jcxz .no
    test cx, 511
    jnz .no
    cmp cx, 32768
    ja .no
    mov al, 2
    call hd_xa_a20
    jc .no
    push cx
    mov [hd_xa_lin], si
    mov [hd_xa_lin+2], di
    sub cx, 512
    mov [hd_xa_lim], cx
    mov [hd_xa_seg], dx         ; the SEGMENT, kept as it came: the shift pair
                                ; that made it linear here was 19 bytes, and
                                ; the 386 half does it in one (SPEC.md 117.4 -
                                ; the bytes HDV_TOOL's forwarder is paid with)
    xor ax, ax
    mov [hd_xa_map], ax
    mov [hd_xa_map+2], ax
    mov [hd_xa_map+4], ax
    mov [hd_xa_map+6], ax
    inc byte [hd_xa_on]
    pop cx
    clc                         ; (AX = 0)
    ret
.pm:
    mov al, 1                   ; (for good, as below a 386)
.no:
    xor ah, ah
    stc
    ret

; hd_xa_dis - HDV_DISARM: the map to ES:DI (8 bytes, bit n = the buffer's
; sector n went straight to XMS) and the arm gone. CF = 0; clobbers AX
hd_xa_dis:
    push cx
    push si
    push di
    mov byte [hd_xa_on], 0
    mov si, hd_xa_map
    mov cx, 4
    cld
    rep movsw
    pop di
    pop si
    pop cx
    xor ax, ax
    ret

; hd_xa_a20 - CF = 1 the A20 line is shut: a word at 0000:0500 against its
; alias at FFFF:0510, flipped and flipped back (XMEM.DRV's xm_a20_probe).
; Preserves all but flags
hd_xa_a20:
    push ax
    push bx
    push ds
    push es
    pushf
    cli
    xor ax, ax
    mov es, ax
    dec ax
    mov ds, ax                  ; DS = FFFF
    mov bx, [es:0x0500]
    not word [0x0510]
    mov ax, [es:0x0500]
    not word [0x0510]
    popf
    pop es
    pop ds
    cmp ax, bx
    pop bx
    pop ax
    je .open
    stc
    ret
.open:
    clc
    ret

; -----------------------------------------------------------------------------
; hd_xa_read - rung 1's `rep insw`, armed: DX = the data port, CX = 256,
; ES:DI = where the sector was going ([hd_bseg]:[hd_bofs]). If that is a
; whole sector of the armed buffer, it goes to XMS instead and CF = 0 (DI
; and CX as a `rep insw` leaves them); else CF = 1, nothing moved, every
; register as it came. TIER 2 (an arm is only taken there). DS = ours
; -----------------------------------------------------------------------------
cpu 386
hd_xa_read:
    push eax
    push ebx
    movzx eax, word [hd_bseg]
    movzx ebx, word [hd_xa_seg]
    sub eax, ebx                ; the segments apart, SIGNED...
    shl eax, 4
    movzx ebx, di
    add eax, ebx                ; ...so this is where it was going less the
                                ; armed buffer's start, linear: a sector below
                                ; it is negative, which the unsigned compare
                                ; below reads as huge and refuses
    cmp eax, 0xFFFF
    ja .not
    cmp ax, [hd_xa_lim]
    ja .not
    test ax, 511                ; (a whole sector of it: the map's unit)
    jnz .not
    mov ebx, eax
    add ebx, [hd_xa_lin]        ; EBX = where it goes now
    shr ax, 9
    bts word [hd_xa_map], ax    ; its bit
    pushf
    cli                         ; --- the sector, atomic against every ISR
    push es
    push di
    xor di, di
    call ub_set                 ; ES = the XMS address
    rep insw
    pop di
    pop es                      ; ...and ES as real mode makes it again
    popf
    add di, 512                 ; (as the instruction would have left them)
    pop ebx
    pop eax
    clc
    ret
.not:
    pop ebx
    pop eax
    stc
    ret
cpu 8086

%define UB_ES                   ; ES's base alone (apps/os88ub.inc)
%include "os88ub.inc"
    UB_DATA
