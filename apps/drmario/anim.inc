; Decorative state never consumes RNG or changes capsule timing. Reference:
; NES $89B6 (doctor), $89C9 (bottle animation), $89D4-$8C27 (virus reactions).
; Native cadence: one actor every four 54.6Hz ticks, doctor every eight,
; each virus color every 24. Staggering bounds video traffic on an XT.
dm_animreset:
    push ds
    pop es
    mov di, dm_animstart
    mov cx, dm_animend-dm_animstart
    xor ax, ax
    rep stosb
    mov byte [dm_animdirty], 1
    ret

dm_animtick:
    ; Finish the odd rows one tick after the even rows. Dense level-20 bottles
    ; must not copy an entire color's cells in one frame on an 8088.
    xor bx, bx
    mov bl, [dm_animpending]
    or bl, bl
    jz .clock
    mov al, [dm_viruspose+bx]
    mov [dm_viruspose+bx+4], al
    mov byte [dm_animpending], 0
    mov byte [dm_dirty], 2
.clock:
    inc byte [dm_animclock]
    cmp byte [dm_animclock], 4
    jb .out
    mov byte [dm_animclock], 0
    xor bx, bx
    mov bl, [dm_animslot]
    inc byte [dm_animslot]
    cmp byte [dm_animslot], 6
    jb .slot
    mov byte [dm_animslot], 0
.slot:
    test bl, 1
    jnz .doctor
    shr bl, 1
    inc bl
    mov [dm_animpending], bl
    xor byte [dm_viruspose+bx], 1
    mov al, [dm_viruspose+bx]
    cmp byte [dm_hits+bx], 0
    je .alive
    dec byte [dm_hits+bx]
    mov al, 2
    jmp .save
.alive:
    cmp byte [dm_colorcount+bx], 0
    jne .save
    mov al, 3
.save:
    mov [dm_actorpose+bx], al
    mov byte [dm_animdirty], 1
    mov byte [dm_dirty], 2
.out: ret
.doctor:
    ; A brief blink about every 4.8 seconds; no head clearing or movement.
    inc byte [dm_blinktimer]
    cmp byte [dm_blinktimer], 32
    je .close
    cmp byte [dm_blinktimer], 33
    jb .out
    mov byte [dm_blinktimer], 0
    xor al, al
    jmp .docsave
.close:
    mov al, 1
.docsave:
    mov [dm_actorpose], al
    mov byte [dm_animdirty], 1
    ret

; Four tiny actor shadows are independent of the board/HUD shadows. Patches
; contain the union of all pose footprints, so no trails or VRAM reads occur.
dm_animpaint:
    cmp byte [dm_animdirty], 0
    je .out
    mov byte [dm_animdirty], 0
    xor bp, bp
.actor:
    mov al, [ds:dm_actorpose+bp]
    cmp al, [ds:dm_actorshadow+bp]
    je .next
    mov [ds:dm_actorshadow+bp], al
    xor ah, ah
    mov bx, bp
    shl bx, 1
    shl bx, 1
    add bx, ax
    shl bx, 1
    mov ax, [dm_fsi+FSI_SEG]
    mov es, ax
    cmp byte [dm_cga], 0
    jne .cga
    mov si, [dm_animptrs_vga+bx]
    mov bx, 0102h
.plane:
    mov dx, 3c4h
    mov ax, bx
    out dx, ax
    call dm_animpatch
    shl bh, 1
    cmp bh, 16
    jb .plane
    jmp .next
.cga:
    mov si, [dm_animptrs_cga+bx]
    call dm_animpatch
    call dm_animpatch
.next:
    inc bp
    cmp bp, 4
    jb .actor
.out: ret

; Trusted offline-compiled absolute spans. CX high byte stays zero.
dm_animpatch:
    xor cx, cx
.span:
    lodsw
    cmp ax, 0ffffh
    je .out
    mov di, ax
    lodsb
    mov cl, al
    rep movsb
    jmp .span
.out: ret

%include "dm-anim-vga.inc"
%include "dm-anim-cga.inc"
