; =============================================================================
; os8088 - tests/megacp/megacp.asm
;
; MEGACP, a TEST instrument (nothing under tests/ ships): kern_mega's POOL
; COMPACTION (docs/plans/KERN-MEGA-PLAN.md 14.15), from a package that says
; OS88_HIGH | OS88_HIGHMEM, so its claims of 64KB or less are selectors over
; pool blocks and its own region is one too - and both are blocks the
; compactor may move while this very call is inside the kernel.
;
;   1. claim 60KB blocks until the pool has no run for another (a claim that
;      comes back CONVENTIONAL is the pool full: it goes back and the walk
;      stops), stamping each one at three offsets with its own index;
;   2. free every other one, so the pool's free KB are 60KB HOLES that no
;      64KB claim fits in one of;
;   3. claim 64KB: only a compaction can find it a run. Conventional = the
;      compactor did nothing (or the pool never had the KB at all);
;   4. read back every held block's stamps THROUGH ITS OWN SELECTOR, which
;      the compactor must not have changed - only the descriptor's base.
;
; One line on port E9, for tools/os88bochs.py:
;
;   MEGACP: n NN freed MM big SSSS ok    or    MEGACP: no CC
;
; NN the 60KB blocks taken, MM freed, SSSS the 64KB claim (bit 2 set: a
; selector, which is the pass). CC the step that failed: 1 too few blocks to
; make a hole pattern, 2 the 64KB claim refused outright, 3 it came back
; conventional (no compaction), 4 a stamp moved or was lost.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGACP', mc_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM

MC_MAX  equ 32
MC_KB   equ 60
MC_TMAX equ 24

mc_entry:
    push si
    push di
    push bp
    xor bp, bp                      ; --- 1. fill ---------------------------
.fill:
    cmp bp, MC_MAX
    jae .filled
    mov ax, MC_KB
    call OSAPI_MEM_CLAIM
    jc .filled
    test dl, 4
    jnz .sel
    call OSAPI_MEM_FREE             ; conventional: the pool is full
    jmp short .filled
.sel:
    mov bx, bp
    shl bx, 1
    mov [mc_tab + bx], dx
    mov es, dx
    mov ax, bp
    or ax, 0xC000
    mov [es:0], ax
    mov [es:30000], ax
    mov [es:MC_KB * 1024 - 2], ax
    inc bp
    jmp short .fill
.filled:
    mov [mc_n], bp
    mov cl, 1
    cmp bp, 3
    jb .no
    mov ax, 32                      ; ...and the TAIL, in ever smaller claims,
.tail:                              ; so the holes made below are the only
    push ax                         ; free runs the pool has left
    call OSAPI_MEM_CLAIM
    jc .tnext
    test dl, 4
    jnz .tkeep
    call OSAPI_MEM_FREE
.tnext:
    pop ax
    shr ax, 1
    jnz .tail
    jmp short .tdone
.tkeep:
    mov bx, [mc_nt]
    cmp bx, MC_TMAX * 2
    jae .tfree
    mov [mc_ttab + bx], dx
    add word [mc_nt], 2
    pop ax
    jmp short .tail                 ; the same size again until it stops fitting
.tfree:
    call OSAPI_MEM_FREE
    jmp short .tnext
.tdone:
    mov si, mc_s_caps
    call mc_say
    call OSAPI_XMEM_CAPS
    call mc_hex4
    xor bx, bx                      ; --- 2. holes: free the odd ones -------
    xor cx, cx
.free:
    test bl, 2
    jz .keep
    mov dx, [mc_tab + bx]
    call OSAPI_MEM_FREE
    mov word [mc_tab + bx], 0
    inc cx
.keep:
    add bx, 2
    mov ax, bp
    shl ax, 1
    cmp bx, ax
    jb .free
    mov [mc_freed], cx
    mov si, mc_s_caps
    call mc_say
    call OSAPI_XMEM_CAPS
    call mc_hex4
    call mc_nl
    mov ax, 64                      ; --- 3. the claim no hole holds -------
    call OSAPI_MEM_CLAIM
    mov cl, 2
    jc .no
    mov [mc_big], dx
    mov cl, 3
    test dl, 4
    jz .nofree
    mov es, dx                      ; written end to end: its window is 64KB
    mov word [es:0], 0x1234
    mov word [es:0xFFFE], 0x4321
    xor bx, bx                      ; --- 4. every survivor's stamps -------
.chk:
    mov dx, [mc_tab + bx]
    or dx, dx
    jz .cnext
    mov es, dx
    mov ax, bx
    shr ax, 1
    or ax, 0xC000
    mov cl, 4
    cmp [es:0], ax
    jne .nofree
    cmp [es:30000], ax
    jne .nofree
    cmp [es:MC_KB * 1024 - 2], ax
    jne .nofree
.cnext:
    add bx, 2
    mov ax, bp
    shl ax, 1
    cmp bx, ax
    jb .chk
    mov es, [mc_big]
    mov cl, 4
    cmp word [es:0], 0x1234
    jne .nofree
    cmp word [es:0xFFFE], 0x4321
    jne .nofree
    mov si, mc_s_n                  ; --- the line --------------------------
    call mc_say
    mov ax, [mc_n]
    call mc_hex2
    mov si, mc_s_freed
    call mc_say
    mov ax, [mc_freed]
    call mc_hex2
    mov si, mc_s_big
    call mc_say
    mov ax, [mc_big]
    call mc_hex4
    mov si, mc_s_ok
    call mc_say
    call mc_nl
    call mc_freeall
    mov dx, [mc_big]
    call OSAPI_MEM_FREE
    stc                             ; (no window: a refusal, so the teardown
    jmp short .out                  ; is the kernel's ordinary one)
.nofree:
    mov dx, [mc_big]
    or dx, dx
    jz .no
    push cx
    call OSAPI_MEM_FREE
    pop cx
.no:
    push cx
    call mc_freeall
    pop cx
    mov si, mc_s_no
    call mc_say
    mov al, cl
    call mc_hex2
    call mc_nl
    stc
.out:
    pop bp
    pop di
    pop si
    ret

mc_freeall:
    xor bx, bx
.t:
    cmp bx, [mc_nt]
    jae .c
    mov dx, [mc_ttab + bx]
    call OSAPI_MEM_FREE
    add bx, 2
    jmp short .t
.c:
    mov word [mc_nt], 0
    xor bx, bx
.l:
    cmp bx, MC_MAX * 2
    jae .o
    mov dx, [mc_tab + bx]
    or dx, dx
    jz .n
    call OSAPI_MEM_FREE
    mov word [mc_tab + bx], 0
.n:
    add bx, 2
    jmp short .l
.o:
    ret

mc_say:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mc_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mc_hex4:
    push ax
    mov al, ah
    call mc_hex2
    pop ax
mc_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

mc_s_n:     db 'MEGACP: n ', 0
mc_s_freed: db ' freed ', 0
mc_s_big:   db ' big ', 0
mc_s_ok:    db ' ok', 0
mc_s_no:    db 'MEGACP: no ', 0
mc_s_caps:  db 'MEGACP: free KB ', 0

    OS88_BSS MC_MAX * 2 + 8 + MC_TMAX * 2 + 2
    OS88_IMAGE_END

mc_tab      equ os88_image_end + 0
mc_n        equ os88_image_end + MC_MAX * 2
mc_freed    equ os88_image_end + MC_MAX * 2 + 2
mc_big      equ os88_image_end + MC_MAX * 2 + 4
mc_nt       equ os88_image_end + MC_MAX * 2 + 8
mc_ttab     equ os88_image_end + MC_MAX * 2 + 10
