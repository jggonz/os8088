%ifdef CSDIAG
; =============================================================================
; os8088 - apps/skies/csdiag.inc
;
; THE WATCHDOG (SPEC.md 88.14) - A DIAGNOSTIC BUILD ONLY. No shipped floppy
; carries this; `make skiesdiag` is what builds it.
;
; A hard freeze inside an fsx bracket takes the whole machine with it, and a
; field machine has no debugger: the only instrument is a photograph of the
; glass. So this hooks int 08h for the length of the bracket and paints,
; every tick and straight into VRAM, the last CSD_SLOTS interrupted IPs and a
; TICK COUNTER. The frozen screen then says where the machine is stuck.
;
; The counter is half the answer on its own. If it is still moving when the
; picture has stopped, IRQ0 is alive, IF is set, and the freeze is a loop in
; this package that the IPs beside it name. If it has stopped too, the
; machine is dead below us - IF clear, or the tick chain gone - and no amount
; of looking at Clear Skies will find it.
;
; A word is painted BIG-ENDIAN as sixteen pixels, bit 15 leftmost, CSD_ROWS
; scan rows tall, at the top-left corner of the VIEW. The frame overwrites it
; constantly, which is the point: what survives is what was painted after the
; last frame that ever finished.
;
; It chains rather than replacing: the kernel's sch_isr is what schedules and
; what sends the EOI, and this runs in front of it on the interrupted task's
; stack, which is ~20 bytes of one slice for as long as the bracket lasts.
; =============================================================================

CSD_SLOTS equ 3                     ; interrupted IPs kept
CSD_BLKS  equ 9                     ; ...and the blocks painted in all
CSD_ROWS  equ 4                     ; scan rows a word is painted in
CSD_CANB  equ 16                    ; the watchdog's own guard, beside the
CSD_PAT   equ 0xA55A                ; bytes a wild write was seen to reach

; -----------------------------------------------------------------------------
; cs_diag_on / cs_diag_off - take and give back the int 08h vector. Called
; with the bracket up and cs_devoff built. Preserve everything
; -----------------------------------------------------------------------------
cs_diag_on:
    push ax
    push es
    xor ax, ax
    mov es, ax
    mov word [cs_dhead], 0
    mov word [cs_dtick], 0
    mov word [cs_dframe], 0
    mov byte [cs_dbroke], 0
    mov byte [cs_dbwhich], 0
    mov byte [cs_dbstage], 0
    mov word [cs_dbtick], 0
    push cx                         ; ...and lay the watchdog's own guard
    push di
    push es
    push ds
    pop es
    mov di, cs_dcan
    mov cx, CSD_CANB / 2
    mov ax, CSD_PAT
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pushf                           ; CLAUDE.md: pushf/cli ... popf, so an
    cli                             ; interrupt disabled here comes back
    mov ax, [es:8*4]
    mov [cs_dold], ax
    mov ax, [es:8*4 + 2]
    mov [cs_dold + 2], ax
    mov word [es:8*4], cs_diag_isr
    mov ax, cs
    mov [es:8*4 + 2], ax
    popf
    pop es
    pop ax
    ret

cs_diag_off:
    push ax
    push es
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [cs_dold]
    mov [es:8*4], ax
    mov ax, [cs_dold + 2]
    mov [es:8*4 + 2], ax
    popf
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_diag_ck - ARE THE GUARDS STILL THERE? (SPEC.md 88.14.1)
;
; Called from CSSTAGE, so eleven times a frame rather than once a tick: what
; that buys is the PHASE. A tick samples a 55 ms window and names a routine;
; a stage names sky/ground, or the scene, or the panel, or the blit.
;
; Four regions, and none of them is arbitrary. cs_spguard0/1/2 were declared
; around the two span sets by whoever built them and are referenced by
; NOTHING - their last two words each are span0's and span1's y0/y1 header,
; and the rest has never been written by anything, so zero is what they hold.
; cs_dcan is the watchdog's own, next to the bytes a wild write was actually
; seen to reach (the stage byte came back holding 130).
;
; It latches ONCE. After that the four painted words are the moment memory
; first went wrong, and nothing that happens on the way to the machine dying
; can overwrite the reading.
; Preserves every register.
; -----------------------------------------------------------------------------
cs_diag_ck:
    cmp byte [cs_dbroke], 0
    je .look
    ret
.look:
    push ax
    push bx
    push cx
    push si
    mov bl, 1                       ; region 1: before span0
    mov si, cs_spguard0
    mov cx, 2
    call .zeroes
    jc .broke
    inc bl                          ; region 2: before span1
    mov si, cs_spguard1
    mov cx, 2
    call .zeroes
    jc .broke
    inc bl                          ; region 3: after span1
    mov si, cs_spguard2
    mov cx, 4
    call .zeroes
    jc .broke
    inc bl                          ; region 4: the watchdog's own
    mov si, cs_dcan
    mov cx, CSD_CANB / 2
.p:
    lodsw
    cmp ax, CSD_PAT
    jne .broke
    loop .p
    jmp short .out
.broke:
    mov byte [cs_dbroke], 1
    mov [cs_dbwhich], bl
    mov al, [cs_dstage]
    mov [cs_dbstage], al
    mov ax, [cs_dtick]
    mov [cs_dbtick], ax
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret
.zeroes:                            ; CX words at DS:SI, all zero? CF=1 if not
    lodsw
    or ax, ax
    jnz .no
    loop .zeroes
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cs_diag_isr - int 08h, in front of the kernel's
; -----------------------------------------------------------------------------
cs_diag_isr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ax, cs                      ; a package's CS is its DS (SPEC.md 20)
    mov ds, ax
    cld
    mov bp, sp                      ; SS != DS (CLAUDE.md), and [bp] wants SS
    mov ax, [bp + 9*2]              ; nine pushes down to the IRET frame's IP
    mov cx, [bp + 10*2]             ; ...and the CS above it, which is what
    mov [cs_dcseg], cx              ; says whether an IP is ours at all
    mov bx, [cs_dhead]
    mov [cs_dring + bx], ax
    add bx, 2
    cmp bx, CSD_SLOTS * 2
    jb .head
    xor bx, bx
.head:
    mov [cs_dhead], bx
    inc word [cs_dtick]
    call cs_diag_paint
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp far [cs:cs_dold]            ; the kernel's, which EOIs and schedules

; -----------------------------------------------------------------------------
; cs_diag_paint - the ring and the counter into VRAM. Clobbers everything
; -----------------------------------------------------------------------------
cs_diag_paint:
    cmp byte [cs_back], 0           ; no bracket, no framebuffer
    je .out
    mov es, [cs_fsi + FSI_SEG]
    cmp byte [cs_back], CSB_MODEX
    jne .go
    mov dx, 0x3C4                   ; every plane, or a Mode X word lands in
    mov ax, 0x0F02                  ; whichever one was last selected
    out dx, ax
.go:
    xor bx, bx                      ; BX = the slot
.slot:
    mov si, bx
    shl si, 1
    mov ax, [cs_dring + si]
    cmp bx, CSD_SLOTS
    jb .word
    je .tick
    cmp bx, CSD_SLOTS + 1
    je .age
    cmp bx, CSD_SLOTS + 2
    je .stage
    cmp bx, CSD_SLOTS + 3
    je .broke
    cmp bx, CSD_SLOTS + 4
    je .btick
    mov ax, [cs_dcseg]              ; BLOCK 9: the interrupted CS - not ours
    jmp short .word                 ; means the CPU had already left
.btick:
    mov ax, [cs_dbtick]             ; BLOCK 8: the TICK a guard first went
    jmp short .word
.broke:
    mov al, [cs_dbstage]            ; BLOCK 7: the STAGE it went in, and WHICH
    mov ah, [cs_dbwhich]            ; guard - zero for both means memory was
    jmp short .word                 ; still whole when everything stopped
.stage:
    mov al, [cs_dstage]             ; BLOCK 6: WHERE IN A FRAME, and the low
    mov ah, [cs_par]                ; byte of the frame parity beside it
    jmp short .word
.tick:
    mov ax, [cs_dtick]              ; BLOCK 4: is IRQ0 alive at all
    jmp short .word
.age:
    mov ax, [cs_dtick]              ; BLOCK 5: TICKS SINCE THE LAST FRAME
    sub ax, [cs_dframe]             ; FINISHED - this is what tells a machine
                                    ; whose PICTURE stopped while the tick
                                    ; went on (a big number: the freeze is
                                    ; ours) from one where BOTH stopped
                                    ; together (a small one: it is below us)
.word:                              ; AH goes down first, so the leftmost eight
    mov di, bx                      ; pixels are bits 15..8 and the next eight
                                    ; are bits 7..0 - a word read left to right
    shl di, 1
    shl di, 1                       ; CSD_ROWS = 4: the slot's first row
    mov cx, CSD_ROWS
.row:
    mov si, di
    shl si, 1
    mov si, [cs_devoff + si]
    cmp cx, 1                       ; the last row of a block is left BLANK,
    jne .put                        ; so four blocks read as four in a
    xor ax, ax                      ; photograph rather than as one smear
.put:
    mov [es:si], ah
    mov [es:si + 1], al
    inc di
    loop .row
    inc bx
    cmp bx, CSD_BLKS
    jb .slot
.out:
    ret

%endif
