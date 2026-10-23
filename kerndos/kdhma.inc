; =============================================================================
; kerndos/kdhma.inc - kern_dos IN THE HMA: its low half (-DKD_HMA, which
; -DKD_MEGA implies; docs/plans/KERN-MEGA-DOS-PLAN.md 7, wave W4)
;
; On kern_mega the image runs at FFFF:0010, which is memory only while A20 is
; open. kern_mega leaves it open across the bracket, and a program has three
; ways to shut it: XMS (whose A20 calls succeed and change nothing, SPEC.md
; 96.15.2), int 15h AH=24h, and the gate itself - port 92h or the 8042. So
; every vector that names the image is redirected through a LOW TRAMPOLINE that
; proves A20 and reopens it if it must, then far-jumps on: DOS=HIGH's own
; design. The proof is mega_a20t's, at FFFF:FFF0 against 0000:FFE0, which no
; image reaches; the reopen is port 92h and then the 8042's output port.
;
; A slot is eight bytes in LOW_SEG - `call kdh_tchk` / `jmp far FFFF:target` -
; and the call returns to the jump with the stack exactly as the vector left it,
; so an interrupt frame or a far call is untouched. The check preserves every
; register and the flags.
;
; THE PASS RUNS ONCE, after every vector kern_dos takes and before the program,
; so every AH=35h the program makes sees trampolines. What the CORE publishes
; later - the PSP's terminate, break and error addresses, the IRQ0 hooks of the
; mouse and the packet driver, the XMS entry int 2Fh AX=4310h hands out - goes
; through the core's `dos_hvec`, which calls kdh_vslot through `dos_hslot`, so
; it is a slot too (KERN-MEGA-DOS-PLAN 10.5 defect 4). A slot is never freed:
; kdh_vslot hands back the one that already names a target, so a target costs
; one slot however often it is published, and the pool is sized for the
; distinct targets, of which dos_ivtab alone is 29.
; =============================================================================
KDH_NSLOT   equ 48                  ; distinct targets; ~40 are reachable

kdh_tramp:
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    push es
    push dx
    ; --- a slot for every IVT vector that names KD_SEG --------------------------
    xor ax, ax
    mov ds, ax                      ; DS = the IVT
    xor si, si
.v:
    cmp word [si+2], KD_SEG
    jne .next
    mov ax, [si]
    call kdh_vslot                  ; DX:AX = LOW_SEG:slot, or the vector
    pushf                           ; itself when the pool is full
    cli
    mov [si], ax
    mov [si+2], dx
    popf
.next:
    add si, 4
    cmp si, 0x400
    jb .v
    pop dx
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; kdh_vslot - AX = a target in KD_SEG: DX:AX = the far address to publish for
; it, LOW_SEG:slot - the slot that already names AX if there is one - or
; KD_SEG:AX itself when the pool is full, which works for as long as A20 is
; open. Reached through the core's `dos_hslot` as well, so it names its own
; cell through CS. Every other register kept
kdh_vslot:
    push cx
    push di
    push es
    mov dx, LOW_SEG
    mov es, dx
    mov di, kdh_slots
    mov cx, [cs:kdh_sused]
    jcxz .new
.find:
    cmp [es:di+4], ax
    je .have
    add di, 8
    loop .find
.new:
    cmp word [cs:kdh_sused], KDH_NSLOT
    jae .full
    cmp word [cs:kdh_sused], 0
    jne .mk
    call kdh_lowcode                ; the checker, before the first slot names it
.mk:
    call kdh_mkslot
    inc word [cs:kdh_sused]
.have:
    mov ax, di
    jmp short .out
.full:
    mov dx, cs
.out:
    pop es
    pop di
    pop cx
    ret

kdh_sused:  dw 0                    ; slots in use: the image is loaded fresh
                                    ; for every run, so this starts at zero

; kdh_mkslot - ES:DI = a slot in LOW_SEG, AX = a target in KD_SEG: the slot
; becomes `call kdh_tchk` / `jmp far KD_SEG:target`. Every register kept
kdh_mkslot:
    push ax
    mov byte [es:di], 0xE8          ; call near kdh_tchk
    mov ax, kdh_tchk
    sub ax, di
    sub ax, 3
    mov [es:di+1], ax
    pop ax
    mov byte [es:di+3], 0xEA        ; jmp far KD_SEG:target
    mov [es:di+4], ax
    mov word [es:di+6], KD_SEG
    ret

; kdh_lowcode - the checker into LOW_SEG, and the slot every ROM call that
; returns INTO the image comes back through (kdx_87). Idempotent, so a caller
; that may run before kdh_tramp - the XMS door at the arena's setup - can make
; sure of it itself. Every register kept
kdh_lowcode:
    push ax
    push cx
    push si
    push di
    push es
    mov ax, LOW_SEG                 ; NAMED, not SS: the XMS door may run on
    mov es, ax                      ; the program's stack
    mov si, kdh_tchk_src
    mov di, kdh_tchk
    mov cx, kdh_tchk_end - kdh_tchk_src
    cld
    rep movsb
    mov di, kdh_s87
    mov ax, kdx_87.back
    call kdh_mkslot
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; kdh_tchk_src - THE CHECK, assembled here and run from LOW_SEG: position-
; independent, no data, every register and the flags kept
kdh_tchk_src:
    pushf
    cli                             ; ONE STEP: an IRQ nested between the two
    push ax                         ; `not`s below, with A20 shut, would leave
    push bx                         ; 0:FFE0 inverted for good, and one between
    push cx                         ; `out 64h, D1` and `out 60h, DF` sends
    push ds                         ; DFh to the KEYBOARD as a command
    push es                         ; (KERN-MEGA-DOS-PLAN 10.5 defect 4)
    xor ax, ax
    mov ds, ax
    dec ax
    mov es, ax                      ; DS = 0000, ES = FFFF
    call .t
    je .open
    in al, 0x92                     ; SHUT: the fast gate...
    or al, 2
    and al, 0xFE                    ; (bit 0 is a reset)
    out 0x92, al
    call .t
    je .open                        ; ...which a machine with no port 92h
    call .kw                        ; ignores, so the 8042's output port,
    mov al, 0xD1                    ; which every AT has
    out 0x64, al
    call .kw
    mov al, 0xDF
    out 0x60, al
    call .kw
    xor cx, cx                      ; ...and WAIT for the gate to follow: the
.w:                                 ; 8042 switches it when it gets to it, and
    call .t                         ; the far jump after this return fetches
    je .open                        ; from the HMA
    loop .w
.open:
    pop es
    pop ds
    pop cx
    pop bx
    pop ax
    popf
    ret
.t:                                 ; ZF = 1: A20 is OPEN (the flip at
    mov bx, [0xFFE0]                ; FFFF:FFF0 went a megabyte up and left
    not word [es:0xFFF0]            ; 0:FFE0 alone). Clobbers AX, BX
    mov ax, [0xFFE0]
    not word [es:0xFFF0]
    cmp ax, bx
    ret
.kw:                                ; the 8042's input buffer empty, or ~64K
    push cx                         ; polls
    xor cx, cx
.k:
    in al, 0x64
    test al, 2
    loopnz .k
    pop cx
    ret
kdh_tchk_end:

; kdh_dptsync - int 1Eh's LOW copy brought up to date with dsk_dpt, which is
; the table the disk layer writes (KERN-MEGA-DOS-PLAN 10.5 defect 1). dsk_xfer
; stores the mounted volume's sectors-per-track into dsk_dpt+4 before every
; transfer (SPEC.md 18.92) and dskw_fmt_low writes the format's fields there,
; and on this arm the BIOS reads the copy instead - taken once at entry, it
; held whichever volume the kernel touched last before the handoff, so a
; multi-track transfer stopped at the wrong EOT, carried on at head 1 sector 1
; and answered CF = 0: the wrong sectors, written as if they were right. Called
; immediately before each floppy int 13h. Preserves every register and FLAGS
kdh_dptsync:
    pushf
    push cx
    push si
    push di
    push es
    push ax
    mov ax, LOW_SEG                 ; ES = LOW_SEG, where kdh_dpt lives -
    mov es, ax                      ; NAMED, whatever SS is
    pop ax
    mov si, dsk_dpt
    mov di, kdh_dpt
    mov cx, DPT_LEN
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    popf
    ret

section .lowbss
kdh_dpt:    resb DPT_LEN            ; int 1Eh's table, low (kd_entry)
kdh_tchk:   resb kdh_tchk_end - kdh_tchk_src
kdh_slots:  resb KDH_NSLOT * 8
kdh_s87:    resb 8                  ; kdx_87's way back from the ROM
section .text
