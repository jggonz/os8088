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
; AN IP IS NOT AN OFFSET IN THIS PACKAGE (SPEC.md 88.14.4). It is whatever was
; interrupted, and while the flight is RUNNING that is regularly somewhere
; else: `cs_input` polls the keyboard with `int 16h` (SPEC.md 53.1), which is
; the ROM, and a kernel slot is a far call into KERNEL_SEG. So the CS is
; banked per slot in cs_dcsr and a reading places each IP rather than assuming
; it. The painted block 9 stays the NEWEST sample's CS, which is the one a
; photograph of a FROZEN machine needs; a strip of twelve blocks would not fit
; above the view (88.14.3's arithmetic: CSD_TOP + 12 * CSD_ROWS = 84 against a
; Hercules view at 74).
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
CSD_TOP   equ 36                    ; the device row the strip starts on when
                                    ; the backend leaves room above the view
                                    ; (SPEC.md 88.14.3). NOT 0: KFZ=1's kernel
                                    ; heartbeat owns rows 0-3 and its 30-second
                                    ; report is drawn at MBAR_H + 8 = row 28,
                                    ; eight rows tall - so 36 clears both, and
                                    ; 36 + CSD_BLKS * CSD_ROWS = 72 clears a
                                    ; Hercules view at 74 (cs_vptab) by two

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
    call cs_diag_rows               ; where the strip goes (SPEC.md 88.14.3)
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
; cs_diag_devo - the device byte offset of absolute device row AX, in AX
;
; cs_r_setup's arithmetic, duplicated rather than shared: that routine walks
; the VIEW's rows and takes its row from [cs_vy], and what this wants is a
; row ABOVE it. A diagnostic build may spend forty bytes to avoid changing a
; hot routine's shape. Clobbers CX, DX.
; -----------------------------------------------------------------------------
cs_diag_devo:
    cmp byte [cs_back], CSB_C160
    je .c160
    cmp byte [cs_back], CSB_HERC
    je .herc
    mov dx, ax                      ; CGA: bank = y & 1, 80 bytes a row
    and dx, 1
    push dx
    shr ax, 1
    mov cx, 80
    mul cx
    pop dx
    jmp short .bank
.herc:
    mov dx, ax                      ; HERC: bank = y & 3, 90 bytes a row
    and dx, 3
    push dx
    mov cl, 2
    shr ax, cl
    mov cx, 90
    mul cx
    pop dx
    add ax, 5                       ; ...and the box starts at x = 40
    jmp short .bank
.c160:
    mov cx, 160
    mul cx
    xor dx, dx
.bank:
    mov cl, 5
    shl dx, cl
    mov cl, 8
    shl dx, cl
    add ax, dx
    ret

; -----------------------------------------------------------------------------
; cs_diag_rows - WHERE THE STRIP GOES, once (SPEC.md 88.14.3)
;
; It used to go at the VIEW's top-left. THE BLIT WAS NOT EATING IT - that was
; this file's first explanation and it is measurably wrong: cs_blit copies a
; per-row SPAN and not the box's width, and over all 200 view rows with the
; aeroplane manoeuvring those spans never leave bytes 15 to 64, which is the
; 400-wide window and nothing else. The watchdog's bytes 5 and 6 were never
; touched, on any row, ever.
;
; What IS in the way is the kernel: KFZ=1's heartbeat owns device rows 0-3,
; and its 30-second report is drawn at MBAR_H + 8 - row 28, eight rows tall -
; which the two instruments together put straight through the strip. So it
; goes at CSD_TOP, clear of both and clear of the view, where nothing in
; either the kernel or the package writes at all.
;
; The field mixture that started this - a block whose three rows disagreed, a
; blank fourth row that was not blank, and one block whose lit pixels spanned
; SEVENTEEN columns of a sixteen-pixel field - is therefore STILL UNEXPLAINED,
; and moving the strip is one variable removed rather than a fix for it.
;
; Where there is no room - CGA and Mode X both put the view at row 0 - it
; falls back to the view's own rows, which is exactly what it did before.
; Preserves every register.
; -----------------------------------------------------------------------------
cs_diag_rows:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    push ds
    pop es
    cld
    mov bx, [cs_vy]
    cmp bx, CSD_BLKS * CSD_ROWS + CSD_TOP
    jb .go                          ; no room above: the view's own rows
    mov bx, CSD_TOP
.go:
    mov di, cs_doff
    mov cx, CSD_BLKS * CSD_ROWS
.r:
    push cx
    mov ax, bx
    call cs_diag_devo
    stosw
    inc bx
    pop cx
    loop .r
    pop es
    pop di
    pop dx
    pop cx
    pop bx
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
    mov [cs_dcsr + bx], cx          ; ...and banked WITH the slot (SPEC.md
                                    ; 88.14.4). cs_dcseg is the NEWEST
                                    ; sample's CS and places only that one;
                                    ; the other two slots are ordinarily in a
                                    ; different segment from it and were
                                    ; unplaceable by anything
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
    mov si, [cs_doff + si]          ; ABOVE the view where there is room
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
