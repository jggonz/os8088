; =============================================================================
; os8088 - apps/frotz/zharness.inc
;
; THE DEVELOPMENT HARNESS. NOT SHIPPED, and not compiled into anything that
; ships: every line of this file is inside `%ifdef ZHARNESS`, the only build
; that defines it is `make zh` (which writes build/zh/, never build/*.o88),
; and the six call sites elsewhere in apps/frotz are each one %ifdef'd line.
; Grep for ZHARNESS to see the whole of the footprint.
;
; WHY IT EXISTS. A Z-machine is a program that runs somebody else's program,
; and the interesting defects only appear a few thousand instructions into a
; real story - which until now meant a person booting QEMU, double-clicking a
; floppy icon, typing at a window and reading a screendump. That loop is
; minutes long and its output is a picture, so a wrong opcode was found by
; eye, described in prose, and never regression-tested. This gives the story a
; TELETYPE: the text it prints goes out a serial port a byte at a time and the
; keys it reads come back in the same way, so the host can play a story to a
; script, capture the transcript, and diff it against dfrotz running the same
; story on the same script.
;
; WHY A SERIAL PORT rather than a file on B:. A transcript written to the
; floppy is only readable after the machine stops, so a story that hangs or
; halts leaves nothing to read, which is exactly the case worth reading. This
; streams: the host sees the output as it is produced, and a halt arrives with
; everything that preceded it.
;
; COM4 at 0x3E8. SPEC.md 9.5's mouse probe knows 3F8 and 2F8 and hooks the IRQ
; of anything that answers there, and DEBUG.DRV (SPEC.md 58) is at 2E8 - 3E8
; is the one address in the machine with no other owner. Polled, never
; interrupt-driven, so it shares an IRQ line with nothing and needs no vector.
; =============================================================================

ZH_PORT equ 0x3E8
ZH_THR  equ ZH_PORT + 0             ; write: transmit / read: receive
ZH_DLL  equ ZH_PORT + 0             ; with DLAB: divisor low
ZH_IER  equ ZH_PORT + 1
ZH_DLM  equ ZH_PORT + 1             ; with DLAB: divisor high
ZH_FCR  equ ZH_PORT + 2
ZH_LCR  equ ZH_PORT + 3
ZH_MCR  equ ZH_PORT + 4
ZH_LSR  equ ZH_PORT + 5
ZH_LSR_RX equ 0x01                  ; a byte is waiting
ZH_LSR_TX equ 0x20                  ; the holding register is empty

; -----------------------------------------------------------------------------
; zh_init - bring the UART up, polled
; out: nothing; preserves all registers
;
; Divisor 1 (115200) because the host end is a UNIX socket with no line rate
; at all - the divisor only has to be a legal one. If this ever runs against a
; real cable the host has to agree, which is what the tool's --baud is for.
; -----------------------------------------------------------------------------
zh_init:
    push ax
    push dx
    mov dx, ZH_IER
    xor al, al
    out dx, al                      ; polled: no interrupts, no vector, no IRQ
    mov dx, ZH_LCR
    mov al, 0x80
    out dx, al                      ; DLAB on
    mov dx, ZH_DLL
    mov al, 1
    out dx, al
    mov dx, ZH_DLM
    xor al, al
    out dx, al
    mov dx, ZH_LCR
    mov al, 0x03
    out dx, al                      ; 8N1, DLAB off
    mov dx, ZH_FCR
    mov al, 0x07
    out dx, al                      ; FIFO on and cleared where there is one
    mov dx, ZH_MCR
    mov al, 0x03
    out dx, al                      ; DTR | RTS
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_putc - one byte out
; in:  AL = the byte
; out: nothing; preserves all registers
;
; The wait is BOUNDED. An unbounded one would hang the whole machine when
; nothing is draining the port, and the harness's job is to make failures
; visible rather than to add a new way of hanging - a dropped byte shows up in
; the diff, a hang shows up as a person waiting.
; -----------------------------------------------------------------------------
zh_putc:
    push ax
    push cx
    push dx
    mov ah, al
    xor cx, cx
.wait:
    mov dx, ZH_LSR
    in al, dx
    test al, ZH_LSR_TX
    jnz .send
    loop .wait
    jmp short .out                  ; nobody is listening: drop it
.send:
    mov dx, ZH_THR
    mov al, ah
    out dx, al
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_outc - one ZSCII byte out, in the host's line discipline
; in:  AL = the byte, 13 = new-line
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zh_outc:
    push ax
    cmp al, 13
    jne .plain
    mov al, 10
.plain:
    call zh_putc
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_screenc - stream 1's byte, unless zwin is composing rather than printing
; in:  AL = the byte
; out: nothing; preserves all registers
;
; A capture ([zw_capon]) is the status line being built into a buffer instead
; of drawn, and the two interpreters emit that at different moments: dfrotz
; prints a whole status line before the first prompt, os8088 asks the story for
; the room name at the moment it starts reading. On the wire that is a
; divergence in every transcript, about the timing of a redraw.
;
; It lives HERE and not at the call site in ztext.inc for a NASM reason worth
; knowing: [zw_capon] is a %define in zwin.inc, which is included AFTER
; ztext.inc, so the name is not a symbol yet where the hook is - and the
; failure is not "undefined", it is `label ... changed during code generation`
; on six unrelated strings at the bottom of frotz.asm. zharness.inc is included
; last, so every module's names are real by the time this file is read.
; -----------------------------------------------------------------------------
zh_screenc:
    cmp byte [zw_capon], 0
    jne .out
    cmp byte [zw_cur], 0            ; ...and the upper window is the same thing
    jne .out                        ; by another route: from v4 the STORY draws
                                    ; its own status line there, with
                                    ; @set_cursor rather than newlines, so on
                                    ; the wire it arrives welded to the prose
                                    ; around it - "down a gullyAt End Of
                                    ; RoadScore 36Moves 0". dfrotz prints it as
                                    ; a line of its own and a line is
                                    ; separable; this is not.
    call zh_outc
.out:
    ret

; -----------------------------------------------------------------------------
; zh_puts / zh_nl - a NUL-terminated string, and a line ending
; in:  DS:SI = the string (zh_puts)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zh_puts:
    push ax
    push si
.loop:
    mov al, [si]
    inc si
    or al, al
    jz .done
    call zh_outc
    jmp short .loop
.done:
    pop si
    pop ax
    ret

zh_nl:
    push ax
    mov al, 10
    call zh_putc
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_mark - a marker line the host can synchronise on
; in:  DS:SI = the marker's text, without the brackets or the newline
; out: nothing; preserves all registers
;
; Markers are bracketed so a story cannot forge one by printing it: no Z-string
; decodes to a byte outside 32..126 except the new-line the decoder emits as
; 13, and the host only ever accepts a marker that occupies a whole line.
; -----------------------------------------------------------------------------
zh_mark:
    push ax
    push si
    call zh_nl
    mov si, zh_m_open
    call zh_puts
    pop si
    push si
    call zh_puts
    mov si, zh_m_close
    call zh_puts
    call zh_nl
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_getkey - a keystroke from the host, if one has arrived
; out: CF = 0 and AL = the character, AH = 0; CF = 1 = nothing waiting
;      preserves every other register
;
; Line endings arrive as whatever the host's script had in it, and both become
; the carriage return the Z-machine's line editor terminates on. A lone LF
; after a CR would otherwise be read as a second, empty command - which reads
; as the story skipping every other line of the script.
; -----------------------------------------------------------------------------
zh_getkey:
    push dx
    mov dx, ZH_LSR
    in al, dx
    test al, ZH_LSR_RX
    jz .none
    mov dx, ZH_THR
    in al, dx
    cmp al, 10
    jne .have
    mov al, 13
.have:
    xor ah, ah
    pop dx
    clc
    ret
.none:
    pop dx
    stc
    ret

; =============================================================================
; THE SECOND WIRE: what the story DREW, not what it printed
;
; Everything above this line carries stream 1 - the prose. That was enough for
; a wrong opcode and is structurally blind to the defect class next to it: a
; story that puts a quote box, a title screen or a picture in a window OTHER
; than the lower one is, on the prose wire, completely silent. BEAR.Z5 opens on
; such a box; it was blank on screen for a fortnight and every transcript
; matched dfrotz word for word, because the words were never in dispute.
;
; So this half reports the WINDOW MODEL: every @split_window, @set_window,
; @erase_window and repaint as it happens, the upper window's whole character
; grid at every prompt, and every picture the drawing path accepted or refused
; - with the screen coordinates it used, which is what lets the host go and
; look at those pixels.
;
; IT IS THE MODEL AND NOT THE SCREEN, and that distinction is the point rather
; than a shortcoming. The grid is what the interpreter BELIEVES is on display;
; tools/zharness.py photographs the screen at the same instant and compares.
; Agreeing, they are evidence; disagreeing, they say which half is wrong - a
; grid that holds the box against a blank screen is a drawing bug, and an empty
; grid against a blank screen is a window-model bug. One wire alone cannot tell
; those apart, and they need opposite fixes.
;
; Markers are `[[ZH:NAME arg arg]]`, on a line of their own, decimal, unsigned:
; a Z-machine window number of -1 arrives as 65535 and the host reads it back.
; The upper-window dump is bracketed by UPPER/`/UPPER` and its rows are wrapped
; in '|' so a trailing space is still a character the host can count.
; =============================================================================

; -----------------------------------------------------------------------------
; zh_num - AX as unsigned decimal, no padding
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zh_num:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx                      ; CX counts the digits pushed
.split:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .split
.emit:
    pop ax
    add al, '0'
    call zh_putc
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_evbeg / zh_evnum / zh_evend - a marker with any number of arguments
; in:  SI = the event's name (zh_evbeg), AX = an argument (zh_evnum)
; out: nothing; all three preserve every register
;
; Three pieces rather than one call per arity, because the arities run from
; zero (PAINT) to eleven (GEOM) and a routine per shape is ten routines.
; -----------------------------------------------------------------------------
zh_evbeg:
    push ax
    push si
    call zh_nl
    mov si, zh_m_open
    call zh_puts
    pop si
    push si
    call zh_puts
    pop si
    pop ax
    ret

zh_evnum:
    push ax
    mov al, ' '
    call zh_putc
    pop ax
    call zh_num
    ret

zh_evend:
    push si
    mov si, zh_m_close
    call zh_puts
    call zh_nl
    pop si
    ret

; -----------------------------------------------------------------------------
; zh_ev1 - the common shape: one name, one number
; in:  SI = the name, AX = the number; preserves every register
; -----------------------------------------------------------------------------
zh_ev1:
    call zh_evbeg
    call zh_evnum
    call zh_evend
    ret

; -----------------------------------------------------------------------------
; zh_pdata - what @picture_data answered
; in:  AX = picture number, DX = width, CX = height; 0 x 0 = unavailable
; out: nothing; preserves every register
;
; Worth a marker of its own because the Standard lets this answer "no such
; picture" (8.8.6.1) and a story LAYS ITSELF OUT around the reply. A wrong size
; here is invisible in a transcript and rearranges the whole screen.
; -----------------------------------------------------------------------------
zh_pdata:
    push ax
    push si
    mov si, zh_m_pdata
    call zh_evbeg
    call zh_evnum                   ; AX = the picture number
    mov ax, dx
    call zh_evnum                   ; ...width
    mov ax, cx
    call zh_evnum                   ; ...and height
    call zh_evend
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_snap - the window state, at a moment the host chose
; out: nothing; preserves every register INCLUDING ES and SI
;
; ES and SI are the worker's live program counter (docs/FROTZ-PLAN.md 3.1) and
; zw_upptr loads ES to reach the grid, so the save here is load-bearing rather
; than tidy. Called just before READ and KEY - the two moments the story is
; quiescent and the host is about to photograph the screen.
;
; GEOM is emitted with it because a grid row means nothing to the host without
; the pixels it occupies: row r of the upper window is at ty + (stat + r)*lead,
; and every term of that is a live value on a resizable window, not a constant.
; -----------------------------------------------------------------------------
zh_snap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; v6 HAS NO CHARACTER GRID TO DUMP. Its eight windows are pixel-addressed
    ; and zwin6.inc owns them; every name below belongs to zwin.inc and would
    ; report the state a v1-5 story left behind, which is worse than reporting
    ; nothing because the host cannot tell it is stale. The picture markers are
    ; where the v6 evidence is, and they are emitted from zpic.inc regardless.
    cmp byte [zf_ver], 6
    je .out

    ; THE GFX LOCK, and taking it here is not politeness. zi_readline and
    ; zi_readchar drop it before they wait, because a worker that waits under
    ; the lock is a hung machine (SPEC.md 20.6) - so this runs UNLOCKED, and
    ; the row builders below fill zw_rbuf, which is the same one-row scratch
    ; the UI task's W_PAINT uses. Without the lock a repaint landing mid-dump
    ; gets a row this file overwrote underneath it: the harness would corrupt
    ; the very screen it exists to measure, occasionally, and the report would
    ; be a picture of the harness rather than of the interpreter.
    ;
    ; Holding it here is legal where waiting under it is not. Nothing below
    ; yields: it is memory reads and polled port writes, every one of them
    ; bounded (zh_putc drops a byte rather than spin for ever).
    call OSAPI_GFX_LOCK

    mov si, zh_m_geom
    call zh_evbeg
    mov ax, [zw_x0]
    call zh_evnum
    mov ax, [zw_y0]
    call zh_evnum
    mov ax, [zw_w]
    call zh_evnum
    mov ax, [zw_h]
    call zh_evnum
    mov ax, [zw_tx]
    call zh_evnum
    mov ax, [zw_ty]
    call zh_evnum
    mov ax, [zw_cols]
    call zh_evnum
    mov ax, [zw_rows]
    call zh_evnum
    mov ax, [zw_upper]
    call zh_evnum
    call zw_statrows
    call zh_evnum
    mov ax, ZF_LEAD
    call zh_evnum
    mov ax, [zw_uphold]             ; both counts, because the difference is
    call zh_evnum                   ; the whole of the quote-box story
    call zh_evend

    ; --- the upper window, straight out of the grid a repaint would use ------
    ; [zw_uphold] rows, matching zw_up_draw exactly: a row the story has given
    ; up but which is still displayed is on the screen the host photographs,
    ; and dumping only the owned rows would report it as ink with no model
    ; behind it.
    mov si, zh_m_upper
    call zh_evbeg
    mov ax, [zw_uphold]
    call zh_evnum
    mov ax, [zw_cols]
    call zh_evnum
    call zh_evend

    mov al, [zw_style]              ; banked once for both dumps; the builders
    mov [zw_lstyle], al             ; below adopt each row's own style

    mov cx, [zw_uphold]
    jcxz .updone
    xor bx, bx                      ; BX = the grid row
.uprow:
    push cx
    call zw_rowfill                 ; spaces to [zw_cols], NUL-terminated
    mov byte [zw_style], ZST_ROMAN
    mov ax, bx
    call zw_upptr                   ; ES:DI = the row's record, CF=1 no grid
    jc .upemit
    mov cl, [es:di]                 ; ...and its length byte
    xor ch, ch
    jcxz .upsty
    mov al, [es:di+1]               ; the style it was written in, exactly as
    mov [zw_style], al              ; zw_up_draw adopts it
.upsty:
    cmp cx, [zw_cols]
    jbe .uplen
    mov cx, [zw_cols]
.uplen:
    jcxz .upemit
    mov si, di
    add si, ZW_LHDR
    mov di, zw_rbuf                 ; DS:DI - zw_rowfill left the NUL at [cols]
.upcp:
    mov al, [es:si]
    inc si
    cmp al, ' '
    jae .upkeep
    mov al, ' '                     ; a cell the story never wrote is a blank,
.upkeep:                            ; and the claim's zero would end the line
    mov [di], al
    inc di
    loop .upcp
.upemit:
    call zh_row
    inc bx
    pop cx
    loop .uprow
.updone:
    mov si, zh_m_upend
    call zh_evbeg
    call zh_evend

    ; --- and the lower window, through zwin's OWN row builders --------------
    ; zw_storedinto / zw_liveinto are what zw_low_draw calls, so this cannot
    ; drift from what is actually drawn - a dump with its own idea of which
    ; line goes where would agree with the screen only by luck, and the whole
    ; point of the dump is to be evidence about the screen.
    ; THE LIVE ROW AND THE PENDING WORD are arguments three and four, and they
    ; are what keeps the host from reporting the prompt as a lost character.
    ; zwin draws one OSAPI_FONT_RUN per WORD (zwin.inc's opening note), so a
    ; word with no space after it yet - '>' at a prompt is exactly that - is in
    ; zw_wbuf and NOT on the glass, while zw_liveinto, which a repaint uses,
    ; composes it into the row. Both are right. The host subtracts the pending
    ; word from the live row before comparing with the screen, and puts it back
    ; after a repaint, at which point it IS drawn.
    mov si, zh_m_lower
    call zh_evbeg
    call zw_lowrows
    call zh_evnum
    mov ax, [zw_cols]
    call zh_evnum
    mov ax, [zw_sbcount]
    sub ax, [zw_sbtop]
    jnc .havelive
    mov ax, 0xFFFF                  ; scrolled off the top: no live row here
.havelive:
    call zh_evnum
    mov ax, [zw_wbn]
    call zh_evnum
    call zh_evend

    call zw_lowrows                 ; the live style was banked above
    mov cx, ax
    jcxz .lowdone
    mov bx, [zw_sbtop]              ; BX = the logical line
.lowrow:
    push cx
    call zw_rowfill
    mov byte [zw_style], ZST_ROMAN
    mov ax, bx
    cmp ax, [zw_sbcount]
    ja .lowemit                     ; past the live line: a blank row
    jb .lowstored
    call zw_liveinto
    jmp short .lowemit
.lowstored:
    call zw_storedinto
.lowemit:
    call zh_row
    inc bx
    pop cx
    loop .lowrow
.lowdone:
    mov al, [zw_lstyle]
    mov [zw_style], al
    mov si, zh_m_lowend
    call zh_evbeg
    call zh_evend

    call OSAPI_GFX_UNLOCK
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zh_row - zw_rbuf as one dump line: '|', the row padded to [zw_cols], '|'
; out: nothing; preserves every register
;
; The bars are not decoration. A row's trailing spaces are as much a fact as
; its letters - a status line is a run of text and then deliberate blank - and
; without a right-hand delimiter every transport between here and the host is
; free to strip them.
; -----------------------------------------------------------------------------
zh_row:
    push ax
    push cx
    push si
    mov al, [zw_style]              ; THE ROW'S STYLE, ahead of its text. A row
    xor ah, ah                      ; of spaces in REVERSE is a black bar - an
    call zh_num                     ; Inform quote box is framed with three of
                                    ; them - so its characters say blank and
                                    ; its pixels are ink, and both are right.
                                    ; Without this the host has no way to know
                                    ; that and reports the frame as a defect.
    mov al, '|'
    call zh_putc
    mov si, zw_rbuf
    xor cx, cx                      ; CX = characters emitted
.ch:
    mov al, [si]
    or al, al
    jz .pad
    cmp al, ' '
    jae .keep
    mov al, ' '
.keep:
    call zh_putc
    inc si
    inc cx
    cmp cx, [zw_cols]
    jb .ch
.pad:
    cmp cx, [zw_cols]
    jae .end
    mov al, ' '
    call zh_putc
    inc cx
    jmp short .pad
.end:
    mov al, '|'
    call zh_putc
    call zh_nl
    pop si
    pop cx
    pop ax
    ret

zh_m_open:  db '[[ZH:', 0
zh_m_close: db ']]', 0
zh_m_ready: db 'READY', 0
zh_m_halt:  db 'HALT', 0
zh_m_quit:  db 'QUIT', 0
zh_m_read:  db 'READ', 0
zh_m_key:   db 'KEY', 0
zh_m_geom:  db 'GEOM', 0
zh_m_upper: db 'UPPER', 0
zh_m_upend: db '/UPPER', 0
zh_m_lower: db 'LOWER', 0
zh_m_lowend: db '/LOWER', 0
zh_m_split: db 'SPLIT', 0
zh_m_win:   db 'WIN', 0
zh_m_erase: db 'ERASE', 0
zh_m_upclr: db 'UPCLR', 0
zh_m_paint: db 'PAINT', 0
zh_m_pic:   db 'PIC', 0
zh_m_picat: db 'PICAT', 0
zh_m_picno: db 'PICNO', 0
zh_m_pdata: db 'PDATA', 0
zh_story:   db 'STORY.DAT', 0
