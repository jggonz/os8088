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

zh_m_open:  db '[[ZH:', 0
zh_m_close: db ']]', 0
zh_m_ready: db 'READY', 0
zh_m_halt:  db 'HALT', 0
zh_m_quit:  db 'QUIT', 0
zh_m_read:  db 'READ', 0
zh_m_key:   db 'KEY', 0
zh_story:   db 'STORY.DAT', 0
