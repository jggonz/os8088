; =============================================================================
; os8088 - tests/pmdma/pmdma.asm
;
; PMDMA: WHERE DOES A PICOMEM 1.x COPY FROM? (docs/FIELD-NOTES.md 66)
;
; A PicoMEM 1.x has no DMA lines. It learns where a Sound Blaster transfer
; lives by SNOOPING the program's writes to the 8237 - ports 00h-0Ch and the
; page register 83h - and then has the CPU copy 128-byte blocks into its own
; RAM from its interrupt handler. On the field 286 its Sound Blaster plays
; LOUD STATIC from the first sample and the machine resets two seconds later.
; Static from the first sample with every block IRQ on time is exactly what
; the card copying from the WRONG ADDRESS sounds like, and the card's own
; boot self-test of the snoop checks the address register and never the page.
;
; The card answers the question itself, in RAM the PC can read: before every
; copy it writes the source seg:off it computed at BIOS:4364h, and the copy
; lands at BIOS:5800h. So this runs one 64-byte single-cycle DSP transfer -
; speaker off - per way of programming channel 1, and reads back both:
;
;   A  sb.inc's sequence, back to back      (what SOUND.DRV does)
;   B  the same sequence, I/O reads between  (a card too slow for A)
;   C  PMINIT's order, back to back          (mode, address, PAGE, count)
;   D  A with the page written FIRST
;
; Each row prints the address programmed, the address the card copied from,
; and how many of the 64 bytes arrived. The verdict line names the pattern:
; the page wrong in every row is a 83h write the card never sees; A wrong and
; B right is a card that misses back-to-back writes.
;
; The field card turned out to be neither: which variant failed moved from
; run to run, and every wrong value was the PORT'S OWN NUMBER - the address
; sampled as data. So two more follow:
;
;   G  the address LOW byte swept through its sixteen high nibbles under
;      four ways of programming, the count's low byte swept plainly, and
;      what the card heard for 45 and C5 - the low byte's bit 6 is what the
;      card trips on (docs/FIELD-NOTES.md 66)
;   E  a 2KB auto-init stream of silence, its start VERIFIED against the
;      card's first copy and made again a different way each time, so the
;      start that is heard names the way that works - last, because the
;      stream has frozen the field 286
;
; It wants SOUND.DRV loaded with the card's Sound Blaster up (the pmsys disks
; do that at boot) and touches nothing SOUND.DRV is using: the DSP is reset
; before and after, and channel 1 is left masked, as a stopped stream leaves
; it. Nothing here ships: `make picomemtest` puts it on the pmsys disks.
;
;   ...boot pmsys720.img, open PMDMA.O88, click the window (or press R),
;   photograph it. The photograph is the result.
;
; K is the card's DISK path, a different question (docs/reports/
; VIDDISK-PICOMEM1-286-2026-10-12.md): how long the CPU takes to copy one
; 512-byte sector out of the card, and what the firmware's own 'DK01' record
; says it spent per sector on the SD card and waiting for that copy. F flips
; the firmware between its multi-sector read and the stock one, for the A/B.
; I times the BUS itself: a byte read and written in RAM, in the card's
; memory and in the screen's, and an 8-bit I/O read of the card and of the
; board, so the copy K times can be taken apart.
; =============================================================================

%include "os88api.inc"

BL_ARENA_BYTES equ 4600             ; a ~60-line report (benchlib.inc)

    OS88_HEADER 'PMDMA', pd_entry

section .bss align=1 follows=.text
section .text

PD_LEN      equ 64                  ; bytes a transfer moves
; THE CARD'S RAM LAYOUT IS NOT A CONSTANT. The first build of this read the
; parameter block at 4364h, out of the firmware SOURCE of October 2026, and the
; field card's November 2025 firmware keeps it at 4164h - the whole shared area
; 200h lower - so every variant read "no copy" while the card was copying
; correctly (the 8237 it rewrote said so: 40BBh is variant D's last byte + 1).
; So the block is FOUND: the ROM's own copy loop is `les di,[cs:dst]` /
; `mov cx,[cs:len]` / `lds si,[cs:src]` (2E C4 3E, 2E 8B 0E, 2E C5 36), and
; `src` is the block. Its layout has not moved between the two:
                                    ; +0 src off, +2 src seg, +4 dest off,
                                    ; +6 dest seg, +8 len, +10 new 8237 addr,
                                    ; +12 new count, +14 channel
                                    ; the destination is read from +4/+6,
                                    ; and BV_DMANoUpdate is the byte the
                                    ; loop sets after its `rep movsw`
PD_TICKS    equ 0x006C              ; BDA: the tick count

PD_R_RIGHT  equ 0                   ; [pd_res + variant]: what it came to
PD_R_PAGE   equ 1                   ; right offset, wrong 64KB page
PD_R_WRONG  equ 2                   ; anywhere else
PD_R_NONE   equ 4                   ; no copy at all (a bit of its own, so
                                    ; AND/OR over the four bytes can ask
                                    ; "all" and "any")

; -----------------------------------------------------------------------------
; pd_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
pd_entry:
    push si
    mov ax, pd_bss_end
    cmp ax, os88_image_end + PD_BSS_TOTAL
    ja .refuse
    call bl_blank
    mov si, pd_s_title
    call bl_sline
    mov si, pd_s_hint
    call bl_sline
    mov si, pd_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [pd_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop si
    ret

pd_paint:
    call bl_paint
    ret

pd_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pd_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    cmp bl, 'd'
    je .drv
    cmp bl, 's'
    je .snd
    cmp bl, 'm'
    je .msk
    cmp bl, 'u'
    je .msk
    cmp bl, 'p'
    je .pmp
    cmp bl, 'a'
    je .ahl
    cmp bl, 'b'
    je .blc
    cmp bl, 'k'
    je .dsk
    cmp bl, 'f'
    je .dfl
    cmp bl, 'i'
    je .isa
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call pd_run
    call pd_repaint
    jmp short .out
.drv:
    call pd_drv
    call pd_save
    call pd_repaint
    jmp short .out
.snd:
    call pd_snd
    push si                         ; S has a file of its own, so the A page
    mov si, pd_s_sfile              ; it usually follows is not lost
    call pd_save.si
    pop si
    call pd_repaint
    jmp short .out
.msk:
    call pd_mask                    ; BL = 'm' mask / 'u' unmask
    call pd_repaint
    jmp short .out
.pmp:
    call pd_pump
    call pd_repaint
    jmp short .out
.blc:
    call pd_bloc
    call pd_repaint
    jmp short .out
.ahl:
    call pd_ah
    call pd_save
    call pd_repaint
    jmp short .out
.dsk:
    call pd_disk
    push si                         ; K too has a file of its own
    mov si, pd_s_kfile
    call pd_save.si
    pop si
    call pd_repaint
    jmp short .out
.dfl:
    call pd_dflip
    call pd_repaint
    jmp short .out
.isa:
    call pd_isa
    push si                         ; I too has a file of its own
    mov si, pd_s_ifile
    call pd_save.si
    pop si
    call pd_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pd_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [pd_win], si
    call pd_run
    call pd_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

;
; pd_save - the whole report, every page of it, to PMDMA.TXT (S: PMSND.TXT) in the folder
; PMDMA was started from: a field run is read back over FTP whole rather than
; photographed a screen at a time. Not without a window - tests/picomem.py
; runs pd_run with no kernel under it at all
pd_save:
    cmp word [pd_win], 0
    jne .go
    ret
.go:
    push si
    mov si, pd_s_file
    call bl_save
    pop si
    ret
.si:                                ; SI = the file name
    cmp word [pd_win], 0
    je .none
    call bl_save
.none:
    ret

pd_repaint:
    cmp word [pd_win], 0            ; no window yet: tests/picomem.py runs
    jne .go                         ; pd_run with no kernel under it at all
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [pd_win]
    call OSAPI_WM_CONTENT
    mov [pd_cx], ax
    mov [pd_cy], dx
    mov bx, [pd_win]
    call OSAPI_WM_GEOM
    mov [pd_cw], cx
    mov [pd_ch], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [pd_cx]
    mov bx, [pd_cy]
    mov cx, ax
    add cx, [pd_cw]
    dec cx
    mov dx, bx
    add dx, [pd_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [pd_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; pd_run - the whole measurement, into a fresh report
; =============================================================================
pd_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov word [pd_res], PD_R_NONE * 0x0101  ; every variant "no copy" until
    mov word [pd_res+2], PD_R_NONE * 0x0101  ; it reports otherwise
    mov si, pd_s_title
    call bl_sline

    pushf                           ; IF as the callback found it: the card's
    pop ax                          ; copy runs in ITS interrupt, so with IF
    mov si, pd_r_if                 ; clear nothing below can happen
    mov di, pd_s_on
    test ax, 0x0200
    jnz .ifon
    mov di, pd_s_off
.ifon:
    call bl_kvs

    call pd_card
    jnc .card
    mov si, pd_s_nocard
    call bl_sline
    jmp .done
.card:
    call pd_dsp
    jnc .dsp
    mov si, pd_s_nodsp
    call bl_sline
    jmp .done
.dsp:
    call bl_blank

    call pd_repaint                 ; PAINTED AS IT GOES: the first field
    mov byte [pd_var], 0            ; run of E froze the machine with nothing
.var:                               ; on the glass, because the report was
    call pd_one                     ; only painted at the end - so a freeze
    call pd_repaint                 ; now leaves the last step it reached
    inc byte [pd_var]
    cmp byte [pd_var], 4
    jb .var

    call pd_tail                    ; the 8237 as the card's ROM left it
    call pd_verdict
    call bl_blank
    call pd_fid                     ; G: how often, and what it hears instead
    call pd_save                    ; ON DISK before E, which can freeze
    call bl_blank
    mov si, pd_s_erun
    call bl_sline
    call pd_repaint
    call pd_auto                    ; E LAST: it has frozen the field 286
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    call pd_save                    ; ...and again with E in it
    ret

; -----------------------------------------------------------------------------
; pd_card - the card, asked in the order its safety rule fixes (SPEC.md
;           34.10): 2A3h must COUNT before int 13h's PicoMEM functions are
;           asked anything
; out: CF = 0 and [pd_bseg] = its BIOS segment, report lines written
; -----------------------------------------------------------------------------
pd_card:
    mov dx, 0x2A3
    pushf
    cli
    in al, dx
    mov ah, al
    mov cx, 8
.ramp:
    in al, dx
    inc ah
    cmp al, ah
    jne .noramp
    loop .ramp
    popf
    mov ax, 0x6000
    mov dx, 0x1234
    int 0x13
    cmp dx, 0xAA55
    jne .no
    mov [pd_bseg], bx
    mov [pd_cirq], ch
    call pd_scan
    jc .no
    push bx
    call bl_lclr
    mov si, pd_r_blk
    xor di, di
    call bl_lput
    mov ax, [pd_prm]
    mov di, BL_C_N
    call bl_hex4
    mov si, pd_s_noupd
    mov di, BL_C_N + 6
    call bl_lput
    mov ax, [pd_noupd]
    mov di, BL_C_N + 19
    call bl_hex4
    call bl_lcommit
    pop bx
    call bl_lclr
    mov si, pd_r_card
    xor di, di
    call bl_lput
    mov ax, bx
    mov di, BL_C_N
    call bl_hex4
    mov si, pd_s_irq
    mov di, BL_C_N + 6
    call bl_lput
    mov al, [pd_cirq]
    xor ah, ah
    xor dx, dx
    mov cx, 2
    mov di, BL_C_N + 10
    call bl_dec
    mov es, bx
    mov si, pd_s_noupd
    mov di, BL_C_N + 14
    call bl_lput
    mov si, [pd_noupd]
    mov al, [es:si]
    xor ah, ah
    mov di, BL_C_N + 27
    call bl_hex4
    call bl_lcommit
    clc
    ret
.noramp:
    popf
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; pd_scan - find the ROM's copy loop and read the block's address out of it
; in:  [pd_bseg]; out: CF = 0 and [pd_prm], [pd_noupd]; CF = 1 not found
; -----------------------------------------------------------------------------
pd_scan:
    push ax
    push si
    push es
    mov es, [pd_bseg]
    xor si, si
.s:
    cmp word [es:si], 0xC42E        ; 2E C4 3E  les di,[cs:dst]
    jne .n
    cmp byte [es:si+2], 0x3E
    jne .n
    cmp word [es:si+5], 0x8B2E      ; 2E 8B 0E  mov cx,[cs:len]
    jne .n
    cmp word [es:si+10], 0xC52E     ; 2E C5 36  lds si,[cs:src]
    jne .n
    cmp byte [es:si+12], 0x36
    jne .n
    mov ax, [es:si+13]
    mov [pd_prm], ax
.f:
    cmp word [es:si], 0xA5F3        ; rep movsw, then the flag's
    jne .fn                         ; `mov byte [cs:x], 1`
    cmp word [es:si+2], 0xC62E
    jne .fn
    mov ax, [es:si+5]
    mov [pd_noupd], ax
    clc
    jmp short .out
.fn:
    inc si
    cmp si, 0x3FF0
    jb .f
    jmp short .none
.n:
    inc si
    cmp si, 0x3FF0
    jb .s
.none:
    stc
.out:
    pop es
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; pd_dsp - find the DSP the card installed (SPEC.md 34.10.2's port walk)
; out: CF = 0 and [pd_base] = its base, a report line written
; -----------------------------------------------------------------------------
pd_dsp:
    mov si, pd_ports
.p:
    lodsw
    or ax, ax
    jz .none
    mov [pd_base], ax
    call pd_reset
    jc .p
    mov al, 0xE1                    ; the DSP version, for the record
    call pd_dw
    call pd_dr
    mov [pd_ver], al
    call pd_dr
    mov [pd_ver+1], al
    call bl_lclr
    mov si, pd_r_dsp
    xor di, di
    call bl_lput
    mov ax, [pd_base]
    mov di, BL_C_N
    call bl_hex4
    mov si, pd_s_ver
    mov di, BL_C_N + 6
    call bl_lput
    mov al, [pd_ver]
    xor ah, ah
    xor dx, dx
    mov cx, 1
    mov di, BL_C_N + 10
    call bl_dec
    mov byte [bl_lscr + BL_C_N + 11], '.'
    mov al, [pd_ver+1]
    mov cx, 2
    mov di, BL_C_N + 12
    call bl_dec
    call bl_lcommit
    clc
    ret
.none:
    stc
    ret

; pd_reset - DSP reset at [pd_base]; CF = 0 when it answered AAh
pd_reset:
    push ax
    push cx
    push dx
    mov dx, [pd_base]
    add dx, 6
    mov al, 1
    out dx, al
    in al, dx                       ; >= 3 us on anything
    in al, dx
    in al, dx
    in al, dx
    xor al, al
    out dx, al
    add dx, 8                       ; base+0Eh: data available
    mov cx, 0xFFFF
.w:
    in al, dx
    test al, 0x80
    jnz .rd
    loop .w
    jmp short .bad
.rd:
    sub dx, 4                       ; base+0Ah
    in al, dx
    cmp al, 0xAA
    jne .bad
    clc
    jmp short .out
.bad:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

; pd_dw - AL to the DSP (bounded write-poll)
pd_dw:
    push cx
    push dx
    push ax
    mov dx, [pd_base]
    add dx, 0x0C
    mov cx, 0xFFFF
.w:
    in al, dx
    test al, 0x80
    jz .go
    loop .w
.go:
    pop ax
    out dx, al
    pop dx
    pop cx
    ret

; pd_dr - AL from the DSP (bounded)
pd_dr:
    push cx
    push dx
    mov dx, [pd_base]
    add dx, 0x0E
    mov cx, 0xFFFF
.w:
    in al, dx
    test al, 0x80
    jnz .rd
    loop .w
.rd:
    sub dx, 4
    in al, dx
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; pd_buf - this variant's 64 bytes. EACH VARIANT HAS ITS OWN, so a card that
;          keeps the previous variant's address copies from somewhere this
;          one did not program and reads WRONG, where one shared buffer would
;          hand it the right bytes from a stale register. The four sit in one
;          256-byte run that never crosses a 64KB page, as an 8237 needs.
;          Sets [pd_boff] (offset in this segment), [pd_addr]/[pd_page] (what
;          the 8237 is given) and [pd_eseg]/[pd_eoff] (the same address
;          normalised as the card writes it: off = linear & 0Fh)
; -----------------------------------------------------------------------------
pd_buf:
    mov bx, pd_pat
.try:
    mov ax, ds
    mov cx, 16
    mul cx
    add ax, bx
    adc dx, 0                       ; DX:AX = the run's linear address
    cmp ax, 0x10000 - PD_LEN * 4
    jbe .ok
    add bx, PD_LEN * 4              ; the second run cannot cross as well
    jmp short .try
.ok:
    mov cl, [pd_var]                ; ...and this variant's quarter of it
    mov ch, PD_LEN
    xchg ax, cx
    mul ah
    xchg ax, cx                     ; CX = variant * PD_LEN, AX back
    add bx, cx
    add ax, cx                      ; cannot carry: the run fits its page
    mov [pd_boff], bx
    mov [pd_addr], ax
    mov [pd_page], dl
    mov cx, ax
    and cx, 0x000F
    mov [pd_eoff], cx
    mov cl, 4                       ; seg = linear >> 4: DL's nibble on top
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    mov [pd_eseg], ax
    ret

; -----------------------------------------------------------------------------
; pd_one - one way of programming channel 1, one transfer, one report block
; -----------------------------------------------------------------------------
pd_one:
    push bp
    call pd_buf                     ; this variant's own 64 bytes
    mov es, [pd_bseg]
    mov bp, [pd_prm]
    ; --- a pattern only this variant writes, and the card's side cleared --
    mov di, [pd_boff]
    mov al, [pd_var]
    mov ah, 0x31
    mul ah
    add al, 0x40                    ; AL = this variant's first byte
    mov cx, PD_LEN
.fill:
    mov [di], al
    add al, 7
    inc di
    loop .fill
    mov word [es:bp], 0xFFFF        ; "no copy yet": the card rewrites all of
    mov word [es:bp+2], 0xFFFF      ; it before the copy it asks for

    ; --- the DSP idle, quiet, at a rate that finishes 64 bytes at once ----
    call pd_reset
    mov al, 0xD3                    ; speaker OFF
    call pd_dw
    mov al, 0x40                    ; time constant: D3h = 22 kHz, 3 ms
    call pd_dw
    mov al, 0xD3
    call pd_dw

    ; --- channel 1, programmed the variant's way ------------------------
    mov bl, [pd_var]
    xor bh, bh
    shl bx, 1
    call [pd_progs + bx]

    ; --- one single-cycle transfer of PD_LEN --------------------------------
    mov al, 0x14
    call pd_dw
    mov al, PD_LEN - 1
    call pd_dw
    xor al, al
    call pd_dw

    ; --- wait for the card's parameter block, then for the copy -----------
    mov byte [pd_got], 0
    mov cx, 18                      ; a second of ticks
    call pd_waitprm
    jc .noprm
    mov byte [pd_got], 1
    mov cx, 3                       ; the copy follows the block at once;
    call pd_wait                    ; ...give it a few ticks either way
.noprm:
    call pd_reset                   ; stop, whatever happened
    mov al, 0x05
    out 0x0A, al                    ; channel 1 masked, as a stopped stream
                                    ; leaves it

    ; --- the report block ---------------------------------------------------
    mov bl, [pd_var]
    xor bh, bh
    shl bx, 1
    mov si, [pd_names + bx]
    call bl_sline

    call bl_lclr                    ; what we programmed
    mov si, pd_r_prog
    mov di, 2
    call bl_lput
    mov ax, [pd_eseg]
    mov di, BL_C_N
    call bl_hex4
    mov byte [bl_lscr + BL_C_N + 4], ':'
    mov ax, [pd_eoff]
    mov di, BL_C_N + 5
    call pd_hex1
    mov si, pd_s_page
    mov di, BL_C_N + 8
    call bl_lput
    mov al, [pd_page]
    xor ah, ah
    mov di, BL_C_N + 13
    call bl_hex4
    call bl_lcommit

    call bl_lclr                    ; what the card copied from
    mov si, pd_r_card2
    mov di, 2
    call bl_lput
    cmp byte [pd_got], 0
    jne .gotprm
    mov si, pd_s_nocopy
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit
    pop bp
    ret
.gotprm:
    mov ax, [es:bp+2]
    mov di, BL_C_N
    call bl_hex4
    mov byte [bl_lscr + BL_C_N + 4], ':'
    mov ax, [es:bp]
    mov di, BL_C_N + 5
    call pd_hex1
    mov si, pd_s_len
    mov di, BL_C_N + 8
    call bl_lput
    mov ax, [es:bp+8]
    xor dx, dx
    mov cx, 4
    mov di, BL_C_N + 12
    call bl_dec
    ; the verdict on the address, kept per variant for pd_verdict
    mov cl, PD_R_RIGHT
    mov si, pd_s_right
    mov ax, [es:bp+2]
    cmp ax, [pd_eseg]
    jne .wrong
    mov al, [es:bp]
    cmp al, [pd_eoff]
    je .say
.wrong:
    mov cl, PD_R_WRONG
    mov si, pd_s_wrong
    mov ax, [es:bp+2]           ; the same 64KB-relative address in a
    xor ax, [pd_eseg]               ; different page differs only in the
    test ax, 0x0FFF                 ; segment's top nibble
    jnz .say
    mov al, [es:bp]
    cmp al, [pd_eoff]
    jne .say
    mov cl, PD_R_PAGE
    mov si, pd_s_wpage
.say:
    mov bl, [pd_var]
    xor bh, bh
    mov [pd_res + bx], cl
    mov di, BL_C_N + 18
    call bl_lput
    call bl_lcommit

    call bl_lclr                    ; how many bytes arrived intact
    mov si, pd_r_data
    mov di, 2
    call bl_lput
    xor dx, dx                      ; DX = matches
    mov si, [pd_boff]
    mov di, [es:bp+4]               ; where the card says it put them
    push es
    mov es, [es:bp+6]
    mov cx, PD_LEN
.cmp:
    mov al, [si]
    cmp al, [es:di]
    jne .nm
    inc dx
.nm:
    inc si
    inc di
    loop .cmp
    sub di, PD_LEN
    mov bx, [es:di]                 ; the first two bytes it holds
    pop es
    mov ax, dx
    xor dx, dx
    mov cx, 4
    mov di, BL_C_N
    call bl_dec
    mov si, pd_s_of64
    mov di, BL_C_N + 5
    call bl_lput
    mov ax, bx                      ; ...and the first two bytes the card
    mov di, BL_C_N + 22             ; holds, which say what it copied when
    call bl_hex4                    ; the address is wrong
    call bl_lcommit
    pop bp
    ret

; pd_hex1 - AL's low nibble as one hex digit at column DI
pd_hex1:
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 'A' - '9' - 1
.d:
    mov [bl_lscr + di], al
    pop ax
    ret

; pd_waitprm - up to CX ticks for the card's parameter block. CF = 1: none
pd_waitprm:
    push ax
    push dx
    push bx
    push es
    mov es, [pd_bseg]
    mov bx, [pd_prm]
.t:
    call pd_tick
    mov dx, ax
.w:
    cmp word [es:bx+2], 0xFFFF
    jne .ok
    call pd_tick
    cmp ax, dx
    je .w
    loop .t
    stc
    jmp short .out
.ok:
    clc
.out:
    pop es
    pop bx
    pop dx
    pop ax
    ret

; pd_wait - CX ticks, whatever happens
pd_wait:
    push ax
    push dx
.t:
    call pd_tick
    mov dx, ax
.w:
    call pd_tick
    cmp ax, dx
    je .w
    loop .t
    pop dx
    pop ax
    ret

pd_tick:
    push es
    xor ax, ax
    mov es, ax
    mov ax, [es:0x0400 + PD_TICKS]
    pop es
    ret

; =============================================================================
; THE FOUR WAYS. In: [pd_addr], [pd_page]; the count is PD_LEN - 1, the mode
; 49h (single, read, channel 1, NOT auto-init: one transfer and stop).
; =============================================================================
PD_MODE equ 0x49

; A - sb.inc's sbl_dma_auto, instruction for instruction
pd_prog_a:
    pushf
    cli
    mov al, 0x05
    out 0x0A, al
    out 0x0C, al
    mov ax, [pd_addr]
    out 0x02, al
    mov al, ah
    out 0x02, al
    out 0x0C, al
    mov al, (PD_LEN - 1) & 0xFF
    out 0x03, al
    mov al, (PD_LEN - 1) >> 8
    out 0x03, al
    mov al, PD_MODE
    out 0x0B, al
    mov al, [pd_page]
    out 0x83, al
    mov al, 0x01
    out 0x0A, al
    popf
    ret

; B - the same writes with four I/O reads after each one
%macro PD_GAP 0
    push ax
    in al, 0x21
    in al, 0x21
    in al, 0x21
    in al, 0x21
    pop ax
%endmacro
pd_prog_b:
    pushf
    cli
    mov al, 0x05
    out 0x0A, al
    PD_GAP
    out 0x0C, al
    PD_GAP
    mov ax, [pd_addr]
    out 0x02, al
    PD_GAP
    mov al, ah
    out 0x02, al
    PD_GAP
    out 0x0C, al
    PD_GAP
    mov al, (PD_LEN - 1) & 0xFF
    out 0x03, al
    PD_GAP
    mov al, (PD_LEN - 1) >> 8
    out 0x03, al
    PD_GAP
    mov al, PD_MODE
    out 0x0B, al
    PD_GAP
    mov al, [pd_page]
    out 0x83, al
    PD_GAP
    mov al, 0x01
    out 0x0A, al
    popf
    ret

; C - PMINIT's setup_dma_transfer order: mask, clear, MODE, address, PAGE,
;     count, unmask
pd_prog_c:
    pushf
    cli
    mov al, 0x05
    out 0x0A, al
    out 0x0C, al
    mov al, PD_MODE
    out 0x0B, al
    mov ax, [pd_addr]
    out 0x02, al
    mov al, ah
    out 0x02, al
    mov al, [pd_page]
    out 0x83, al
    mov al, (PD_LEN - 1) & 0xFF
    out 0x03, al
    mov al, (PD_LEN - 1) >> 8
    out 0x03, al
    mov al, 0x01
    out 0x0A, al
    popf
    ret

; D - A with the page written before anything else
pd_prog_d:
    pushf
    cli
    mov al, [pd_page]
    out 0x83, al
    mov al, 0x05
    out 0x0A, al
    out 0x0C, al
    mov ax, [pd_addr]
    out 0x02, al
    mov al, ah
    out 0x02, al
    out 0x0C, al
    mov al, (PD_LEN - 1) & 0xFF
    out 0x03, al
    mov al, (PD_LEN - 1) >> 8
    out 0x03, al
    mov al, PD_MODE
    out 0x0B, al
    mov al, 0x01
    out 0x0A, al
    popf
    ret

; -----------------------------------------------------------------------------
; pd_tail - the 8237's channel 1 and page register as they read now: the
;           card's ROM rewrites address and count after every copy, so this
;           is the card's own idea of where it got to
; -----------------------------------------------------------------------------
pd_tail:
    call bl_blank
    call bl_lclr
    mov si, pd_r_8237
    xor di, di
    call bl_lput
    pushf
    cli
    out 0x0C, al
    in al, 0x02
    mov ah, al
    in al, 0x02
    xchg al, ah
    mov bx, ax                      ; BX = address
    in al, 0x03
    mov ah, al
    in al, 0x03
    xchg al, ah
    mov cx, ax                      ; CX = count
    in al, 0x83
    popf
    mov dl, al
    mov ax, bx
    mov di, BL_C_N
    call bl_hex4
    mov si, pd_s_cnt
    mov di, BL_C_N + 5
    call bl_lput
    mov ax, cx
    mov di, BL_C_N + 11
    call bl_hex4
    mov si, pd_s_page
    mov di, BL_C_N + 16
    call bl_lput
    mov al, dl
    xor ah, ah
    mov di, BL_C_N + 21
    call bl_hex4
    call bl_lcommit
    ret

; -----------------------------------------------------------------------------
; pd_verdict - the pattern, in one line
; -----------------------------------------------------------------------------
pd_verdict:
    call bl_blank
    mov si, pd_v_nocopy             ; nothing copied at all
    mov ax, [pd_res]
    and ax, [pd_res+2]
    cmp ax, PD_R_NONE * 0x0101
    je .say
    mov si, pd_v_allok              ; every way right
    mov ax, [pd_res]
    or ax, [pd_res+2]
    jz .say
    mov si, pd_v_page               ; every way copied, each from the right
    mov ax, [pd_res]                ; offset in the WRONG page
    and ax, [pd_res+2]
    cmp ax, PD_R_PAGE * 0x0101
    jne .notpage
    mov ax, [pd_res]
    or ax, [pd_res+2]
    cmp ax, PD_R_PAGE * 0x0101
    je .say
.notpage:
    mov si, pd_v_fast               ; SOUND.DRV's way fails, the spaced one
    cmp byte [pd_res], PD_R_RIGHT   ; works
    je .mixed
    cmp byte [pd_res+1], PD_R_RIGHT
    je .say
.mixed:
    mov si, pd_v_mixed
.say:
    call bl_sline
    ret


; =============================================================================
; E - A STREAM, the way playback runs one: auto-init, a ring, the DSP's block
; size, and the SB's interrupt taken by a handler of OURS. The single-cycle
; variants above are what the card does with ONE request; Tracker and MIDIRack
; are a 16 kHz auto-init stream with SOUND.DRV's ISR on the SB's line, and the
; field machine squeals and resets on exactly that. So this is that, minus
; everything of os8088's that is not the DMA and the interrupt: a 2KB ring of
; SILENCE (80h - static here is the card's and nobody else's), 512-byte blocks,
; two seconds, and a count of every copy the card asks for, every one that
; falls outside the ring, every byte it copied that was not silence, and every
; block interrupt - and whether that arrived as a REAL IRQ (the 8259's
; in-service bit set) or as the multiplexer's software `int`.
; =============================================================================
PD_RING     equ 2048
PD_RBUF     equ 4096                ; the silent region E's rings are cut from
PD_BLK      equ 512

pd_auto:
    push bp
    mov si, pd_n_e
    call bl_sline
    mov si, pd_n_e2
    call bl_sline
    mov si, pd_n_e3
    call bl_sline
    ; --- the ring: 2KB of silence that never crosses a 64KB page ----------
    mov bx, pd_ring
.try:
    mov ax, ds
    mov cx, 16
    mul cx
    add ax, bx
    adc dx, 0
    cmp ax, 0x10000 - PD_RBUF
    jbe .ok
    add bx, PD_RBUF
    jmp short .try
.ok:
    mov [pd_rbase], ax              ; the region; each start cuts its ring
    mov [pd_page], dl               ; out of it (pd_ecfg)
    mov di, bx
    mov cx, PD_RBUF / 2
    mov ax, 0x8080
    push es
    push ds
    pop es
    rep stosw
    pop es
    xor ax, ax
    mov [pd_ncp], ax
    mov [pd_nout], ax
    mov [pd_nloud], ax
    mov [pd_nirq], ax
    mov [pd_nhw], ax
    mov [pd_last], ax
    mov [pd_last+2], ax

    ; --- the block cleared, as before each variant: what variant D left in it
    ; is D's copy and not this stream's, and the first build counted it - one
    ; copy "outside the ring" and D's pattern as "not silence", on a model
    ; card that was behaving perfectly
    push es
    mov es, [pd_bseg]
    mov bx, [pd_prm]
    mov word [es:bx], 0xFFFF
    mov word [es:bx+2], 0xFFFF
    pop es

    ; --- our handler on every line the SB could be on but the card's own --
    call pd_hook

    ; --- the stream, started until the card has heard its address --------
    ; Every wrong copy the field card made recorded a PORT NUMBER in place of
    ; the byte written there (G counts it), so a start is checked against the
    ; first copy the card asks for and made again until that copy comes from
    ; the ring - which is what SOUND.DRV would have to do
    mov byte [pd_tries], 0
.again:
    inc byte [pd_tries]
    push es
    mov es, [pd_bseg]
    mov bx, [pd_prm]
    mov word [es:bx], 0xFFFF
    mov word [es:bx+2], 0xFFFF
    pop es
    call pd_reset
    mov al, 0xD3
    call pd_dw
    mov al, 0x40
    call pd_dw
    mov al, 0xC2                    ; 256 - 1e6/16000
    call pd_dw
    ; start k is ring k (pd_ecfg): a start the card mishears is misheard the
    ; same way every time, so each retry is a DIFFERENT ring - its start's
    ; low byte and its count - and the start that is heard names one
    ; SOUND.DRV could use
    mov bl, [pd_tries]
    xor bh, bh
    dec bx
    mov si, bx
    shl si, 1
    shl si, 1                       ; SI = 4 * (k - 1)
    add si, pd_ecfg
    mov ax, [pd_rbase]
    mov cl, [si]
    cmp cl, 0xFF
    je .asis
    sub cl, al                      ; up to the next address whose low byte
    xor ch, ch                      ; is the one asked for
    add ax, cx
.asis:
    mov [pd_addr], ax
    mov [pd_rlin], ax
    mov dl, [pd_page]
    xor dh, dh
    mov [pd_rlin + 2], dx
    mov [pd_taddr], ax
    mov [pd_tpage], dl
    mov ax, [si + 1]
    mov [pd_tcnt], ax
    inc ax
    mov [pd_rlen], ax
    mov al, [si + 3]
    mov [pd_tstrat], al
    mov byte [pd_tmode], 0x59       ; single, auto-init, read, channel 1
    call pd_prog_t                  ; sb.inc's order: E is SOUND.DRV's start
    mov al, 0x48                    ; the block, then the auto-init start
    call pd_dw
    mov al, (PD_BLK - 1) & 0xFF
    call pd_dw
    mov al, (PD_BLK - 1) >> 8
    call pd_dw
    mov al, 0x1C
    call pd_dw
    mov cx, 18
    call pd_waitprm
    jc .started                     ; no copy at all: the watch says so
    push es
    mov es, [pd_bseg]
    mov bx, [pd_prm]
    mov ax, [es:bx+10]              ; what the card means to leave in the
    mov [pd_fnaddr], ax             ; 8237 after it: the address and count
    mov ax, [es:bx+12]              ; it heard, one copy on
    mov [pd_fnsize], ax
    call pd_lin                     ; DL:AX = the copy's source
    pop es
    cmp ax, [pd_addr]
    jne .bad
    cmp dl, [pd_page]
    jne .bad
    mov ax, [pd_fnsize]             ; ...and the count: the first copy is
    add ax, 128                     ; 128 bytes of a ring this long, so it
    cmp ax, [pd_tcnt]               ; leaves count - 128 behind
    je .started
.bad:
    cmp byte [pd_tries], PD_MAXTRY
    jb .again                       ; ...and if it never hears it, the watch
                                    ; reports a stream that left the ring
.started:

    ; --- two seconds of watching ---------------------------------------------
    mov es, [pd_bseg]
    mov bp, [pd_prm]
    call pd_tick
    add ax, 36
    mov [pd_tend], ax
.watch:
    mov ax, [es:bp+2]               ; the block's source seg:off
    mov dx, [es:bp]
    cmp ax, 0xFFFF                  ; the card is between writing it
    je .next
    cmp ax, [pd_last+2]
    jne .new
    cmp dx, [pd_last]
    je .next
.new:
    mov [pd_last+2], ax
    mov [pd_last], dx
    inc word [pd_ncp]
    ; linear = seg * 16 + off, against [ring, ring + 2KB)
    push dx
    mov cx, 16
    mul cx
    pop cx
    add ax, cx
    adc dx, 0
    sub ax, [pd_rlin]
    sbb dx, [pd_rlin+2]
    jnz .out
    cmp ax, [pd_rlen]
    jb .in
.out:
    cmp word [pd_nout], 0           ; the FIRST one is reported: where the
    jne .out2                       ; card went, and how much it took
    mov ax, [pd_last]
    mov [pd_fout], ax
    mov ax, [pd_last+2]
    mov [pd_fout+2], ax
    mov ax, [es:bp+8]
    mov [pd_flen], ax
.out2:
    inc word [pd_nout]
.in:
    ; ...and what the copy carried: every byte must be the silence we gave
    mov cx, [es:bp+8]
    cmp cx, 256
    jbe .lenok
    mov cx, 256
.lenok:
    mov di, [es:bp+4]
    push es
    mov es, [es:bp+6]
.byte:
    jcxz .bytes
    cmp byte [es:di], 0x80
    je .quiet
    inc word [pd_nloud]
.quiet:
    inc di
    dec cx
    jmp short .byte
.bytes:
    pop es
.next:
    call pd_tick
    cmp ax, [pd_tend]
    jne .watch

    ; --- stop, and put everything back ----------------------------------------
    mov al, 0xDA                    ; exit auto-init, then the reset
    call pd_dw
    call pd_reset
    mov al, 0x05
    out 0x0A, al
    call pd_unhook

    ; --- the report -----------------------------------------------------------
    call bl_lclr
    mov si, pd_r_ring
    mov di, 2
    call bl_lput
    mov ax, [pd_rlin]
    mov dx, [pd_rlin+2]
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    mov di, BL_C_N
    call bl_hex4
    mov byte [bl_lscr + BL_C_N + 4], ':'
    mov ax, [pd_rlin]
    mov di, BL_C_N + 5
    call pd_hex1
    mov si, pd_s_page
    mov di, BL_C_N + 8
    call bl_lput
    mov al, [pd_page]
    xor ah, ah
    mov di, BL_C_N + 13
    call bl_hex4
    mov si, pd_s_cnt
    mov di, BL_C_N + 19
    call bl_lput
    mov ax, [pd_tcnt]
    mov di, BL_C_N + 25
    call bl_hex4
    call bl_lcommit
    call bl_lclr                    ; the 8259 as found, and what we opened
    mov si, pd_r_imr
    mov di, 2
    call bl_lput
    mov al, [pd_imr]
    xor ah, ah
    mov di, BL_C_N
    call bl_hex4
    mov si, pd_s_open
    mov di, BL_C_N + 6
    call bl_lput
    mov al, [pd_open]
    xor ah, ah
    mov di, BL_C_N + 13
    call bl_hex4
    call bl_lcommit
    mov si, pd_r_ncp
    mov ax, [pd_ncp]
    call pd_kv
    mov si, pd_r_nout
    mov ax, [pd_nout]
    call pd_kv
    cmp word [pd_nout], 0
    je .noout
    call bl_lclr                    ; the first copy that left the ring
    mov si, pd_r_fout
    mov di, 2
    call bl_lput
    mov ax, [pd_fout+2]
    mov di, BL_C_N
    call bl_hex4
    mov byte [bl_lscr + BL_C_N + 4], ':'
    mov ax, [pd_fout]
    mov di, BL_C_N + 5
    call bl_hex4
    mov si, pd_s_len
    mov di, BL_C_N + 11
    call bl_lput
    mov ax, [pd_flen]
    mov di, BL_C_N + 15
    call bl_hex4
    call bl_lcommit
.noout:
    mov si, pd_r_nloud
    mov ax, [pd_nloud]
    call pd_kv
    mov si, pd_r_tries
    mov al, [pd_tries]
    xor ah, ah
    call pd_kv
    call bl_lclr                    ; the 8237 the card meant to leave after
    mov si, pd_r_fnew               ; its first copy: count = 2047 - the len
    xor di, di                      ; if it heard the count too
    call bl_lput
    mov ax, [pd_fnaddr]
    mov di, BL_C_N
    call bl_hex4
    mov si, pd_s_cnt
    mov di, BL_C_N + 6
    call bl_lput
    mov ax, [pd_fnsize]
    mov di, BL_C_N + 12
    call bl_hex4
    call bl_lcommit
    mov si, pd_r_nirq
    mov ax, [pd_nirq]
    call pd_kv
    mov si, pd_r_nhw
    mov ax, [pd_nhw]
    call pd_kv
    ; the verdict on the stream: BX = the row of five
    mov bx, pd_vtab_e
    mov si, [bx]
    cmp word [pd_ncp], 0
    je .say
    mov si, [bx+2]
    cmp word [pd_nout], 0
    jne .say
    mov si, [bx+4]
    cmp word [pd_nloud], 0
    jne .say
    mov si, [bx+6]
    cmp word [pd_nirq], 0
    je .say
    mov si, [bx+8]
.say:
    call bl_sline
    pop bp
    ret

; pd_kv - SI = label, AX = a count
pd_kv:
    push cx
    push dx
    xor dx, dx
    mov cx, 6
    call bl_kv
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; pd_hook / pd_unhook - our handler on IRQ 3, 5 and 7 bar the card's own line
; (that one is its multiplexer, and the SB is never on it - SPEC.md 34.10.1)
; -----------------------------------------------------------------------------
pd_hook:
    push ax
    push bx
    push si
    push di
    push es
    in al, 0x21                     ; the 8259's mask as we found it: the
    mov [pd_imr], al                ; card's multiplexer delivers an audio
    mov byte [pd_open], 0           ; IRQ only on an UNMASKED line, so a line
    xor ax, ax                      ; nobody had opened would read as "no
    mov es, ax                      ; IRQ" whatever the card did
    xor si, si                      ; SI = the line's index, DI = 4 * it
    xor di, di
.l:
    mov al, [pd_lines + si]
    cmp al, [pd_cirq]
    je .n
    add al, 8
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    pushf
    cli
    mov ax, [es:bx]
    mov [pd_old + di], ax
    mov ax, [es:bx+2]
    mov [pd_old + di + 2], ax
    shr di, 1
    mov ax, [pd_isrs + di]
    shl di, 1
    mov [es:bx], ax
    mov [es:bx+2], cs
    popf
    mov al, [pd_bits + si]          ; ...and the line opened, remembering
    and al, [pd_imr]                ; only the bits WE cleared
    or [pd_open], al
.n:
    add di, 4
    inc si
    cmp si, 3
    jb .l
    pushf
    cli
    in al, 0x21
    mov ah, [pd_open]
    not ah
    and al, ah
    out 0x21, al
    popf
    pop es
    pop di
    pop si
    pop bx
    pop ax
    ret

pd_unhook:
    push ax
    push bx
    push si
    push di
    push es
    xor ax, ax
    mov es, ax
    xor si, si
    xor di, di
.l:
    mov al, [pd_lines + si]
    cmp al, [pd_cirq]
    je .n
    add al, 8
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov bx, ax
    pushf
    cli
    mov ax, [pd_old + di]
    mov [es:bx], ax
    mov ax, [pd_old + di + 2]
    mov [es:bx+2], ax
    popf
.n:
    add di, 4
    inc si
    cmp si, 3
    jb .l
    pushf                           ; the lines we opened, closed again -
    cli                             ; and nothing anybody else changed
    in al, 0x21
    or al, [pd_open]
    out 0x21, al
    popf
    pop es
    pop di
    pop si
    pop bx
    pop ax
    ret

; the handlers: count, ack the DSP, and EOI ONLY a line the 8259 has in
; service - the multiplexer's software `int` sets no in-service bit, and an
; EOI it does not owe would end somebody else's
pd_isr3:
    push ax
    mov al, 0x08
    jmp short pd_isrc
pd_isr5:
    push ax
    mov al, 0x20
    jmp short pd_isrc
pd_isr7:
    push ax
    mov al, 0x80
pd_isrc:
    push bx
    push dx
    mov bl, al
    inc word [cs:pd_nirq]
    mov dx, [cs:pd_base]
    add dx, 0x0E
    in al, dx                       ; the 8-bit ack
    mov al, 0x0B
    out 0x20, al
    in al, 0x20                     ; the in-service register
    test al, bl
    jz .soft
    inc word [cs:pd_nhw]
    mov al, 0x20
    out 0x20, al
.soft:
    pop dx
    pop bx
    pop ax
    iret

; =============================================================================
; G - WHICH VALUES THE CARD HEARS. (The run before this tested four WAYS of
; writing - plain, slow, twice, the page again - and all four read the SAME
; pattern, value for value: it is the values and not the writing, and the
; firmware says why it can be at all - the PIO switches its multiplexed pins
; from the address to the data and the CORE reads the data afterwards, so a
; read that lands early is the address's low byte, the port number. The rows
; below are what replaced those four.)
;
; WHICH WRITES THE CARD MISHEARS, AND WHAT UNDOES IT. The sweeps of the
; run before put it on ONE thing: the LOW byte written first to a register
; pair (the flip-flop's first write). Bit 6 set there and the card goes wrong
; - 45-75 lost the page, C5-F5 lost the low byte and the count, every other
; value and the high byte and the page at every value read right, and reads
; before the page write changed nothing. E's ring was at 3E and still lost the
; page, its COUNT's low byte being FF. So now the low address byte is swept
; under four ways of programming - plain, ~1 ms of nothing after every write,
; the whole sequence twice, and the page written again after everything else
; - then the count's low byte plainly, and what the card heard, field by
; field, for 45 and C5. The card only READS a copy's source, so the address
; can be anything. One character a copy:
;   .  right                      -  no copy in a second
;   l h p  that field heard as the PORT NUMBER (02h, 02h, 3)
;   L H P  that field wrong some other way
;   C  the length (the count) wrong      *  two or more fields wrong
; Then a grid of address against count said no VALUES are safe for a stream:
; the count's high byte failed at every value but 0. So the rows now test the
; ECHO (pd_emit, PD_S_ECHO): the core samples an IOW's data one nop after the
; multiplexer switches, so an early sample takes what SD0-7 last held - and
; making that the value cures it if that is the mechanism. Plain rows are the
; control; E's starts mix echoed rings with the plain one SOUND.DRV uses.
; =============================================================================
PD_MAXTRY   equ 8                   ; E: starts before it reports the stream
PD_TBASE    equ 0x5515              ; G: the address a sweep holds still
PD_S_SLOW   equ 1                   ; pd_tstrat: ~1 ms after every write
PD_S_TWICE  equ 2                   ; ...the whole sequence twice
PD_S_PAGE   equ 4                   ; ...the page again after the rest
PD_S_ECHO   equ 8                   ; ...each value read back from an EVEN
                                    ; address immediately before its OUT

pd_fid:
    call pd_fwlog                   ; an os8088-pm114 firmware: its own log
    jnc pd_fidfw                    ; answers what the sweeps only infer
    mov si, pd_n_g
    call bl_sline
    mov si, pd_n_g2
    call bl_sline
    mov si, pd_r_gnib
    mov di, pd_s_gnib
    call bl_kvs
    call pd_repaint
    mov byte [pd_tmode], PD_MODE
    mov byte [pd_swn], 16
    mov byte [pd_nolen], 0
    mov byte [pd_swk], 0            ; the address's low byte: plain (the
    mov byte [pd_tstrat], 0         ; control, the same row three runs in a
    mov si, pd_g_lo                 ; row), then echoed
    call pd_sweep
    mov byte [pd_tstrat], PD_S_ECHO
    mov si, pd_g_loe
    call pd_sweep
    mov byte [pd_nolen], 1          ; the length follows the count from here
    mov byte [pd_swk], 6            ; the count's high byte, which a stream
    mov byte [pd_tstrat], 0         ; cannot keep at 0: plain, then echoed
    mov si, pd_g_chi
    call pd_sweep
    mov byte [pd_tstrat], PD_S_ECHO
    mov si, pd_g_chie
    call pd_sweep
    mov byte [pd_swk], 4            ; ...and its low byte, echoed
    mov si, pd_g_cnte
    call pd_sweep
    mov byte [pd_tstrat], 0
    mov byte [pd_nolen], 0
    mov al, 0x45                    ; what the card heard, plainly, for one
    mov si, pd_g_h45                ; value of each failing kind
    call pd_detail
    mov al, 0xC5
    mov si, pd_g_hc5
    call pd_detail
    ret

; -----------------------------------------------------------------------------
; THE CARD'S OWN LOG, version 2. The os8088-pm114 firmware (Elendilon/
; ISA-PicoMEM, branch os8088-pm114) logs every DMA-register write it takes at
; BIOS:5C00h: the port it decoded, the data byte read the stock way (EARLY),
; the raw address word its PIO latched - (A19-A0) << 12, so a cycle that was
; really a memory fetch shows its high address bits - and 16 samples of the
; bus taken back to back from then on, ~90 ns apart: the data byte, and a
; control byte (bit 0 MEMR, 1 MEMW, 2 IOR, 3 IOW, 4 mux = address, 5 mux =
; A8-A19, 6 IOCHRDY held). Version 1's one read ~1 us on found the bus already
; in the next cycles, and its log showed the PAGE write taken with the port
; number as its data and four writes decoded as ports 81-84 / 01-04 - so this
; is a TIMELINE of each write rather than a second guess at one moment.
; Only the stock read is used (mode 0), so E and the players behave as stock.
; -----------------------------------------------------------------------------
PD_FW       equ 0x5C00              ; the log, in the card's BIOS segment
PD_FW_MODE  equ PD_FW + 8
PD_FW_IDX   equ PD_FW + 10
PD_FW_TICKS equ PD_FW + 12
PD_FW_LOG   equ PD_FW + 16
PD_FW_NS    equ 16                  ; samples an entry
PD_FW_ESZ   equ 8 + 2 * PD_FW_NS    ; bytes an entry
PD_FW_N     equ 22                  ; entries in the ring
PD_FW_SHOW  equ 16                  ; entries shown after each copy

; pd_fwlog - CF = 0 when the card's firmware carries the log
pd_fwlog:
    push si
    push di
    push cx
    push es
    mov es, [pd_bseg]
    mov si, pd_s_fwsig
    mov di, PD_FW
    mov cx, 8
    cld
    repe cmpsb
    pop es
    pop cx
    pop di
    pop si
    je .yes
    stc
    ret
.yes:
    clc
    ret

pd_fwmode:                          ; AL = 0 early (stock) / k = sample k-1
    push es
    mov es, [pd_bseg]
    mov [es:PD_FW_MODE], al
    pop es
    ret

pd_fidfw:
    mov si, pd_n_gfw
    call bl_sline
    mov si, pd_n_gfw2
    call bl_sline
    mov si, pd_n_gfw3
    call bl_sline
    mov si, pd_n_g2
    call bl_sline
    mov si, pd_r_gnib
    mov di, pd_s_gnib
    call bl_kvs
    call pd_repaint
    mov byte [pd_tmode], PD_MODE
    mov byte [pd_tstrat], 0
    mov byte [pd_swn], 16
    mov byte [pd_nolen], 0
    xor al, al                      ; the stock read, throughout
    call pd_fwmode
    mov byte [pd_swk], 0
    mov si, pd_g_lofe
    call pd_sweep
    mov al, 0x45                    ; ...and the card's own timeline of the
    mov si, pd_g_h45                ; writes behind one copy of each kind
    call pd_detail
    call pd_logdump
    mov al, 0xC5
    mov si, pd_g_hc5
    call pd_detail
    call pd_logdump
    ret

; pd_hex2 - AL as two hex digits into the line scratch at column DI.
; Preserves every register
pd_hex2:
    push ax
    push di
    add di, bl_lscr
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call .dig
    pop ax
    inc di
    call .dig
    pop di
    pop ax
    ret
.dig:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .st
    add al, 'A' - '0' - 10
.st:
    mov [di], al
    ret

; pd_logdump - the card's last PD_FW_SHOW logged writes, oldest first, two
; lines each: the port it decoded, the latched address (A19-A0), the early
; byte and the 16 data samples; then the 16 control samples under them
pd_logdump:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    mov es, [pd_bseg]
    call bl_lclr                    ; the sampling time, from the header
    mov si, pd_s_ltick
    mov di, 2
    call bl_lput
    mov ax, [es:PD_FW_TICKS]
    mov di, 42
    call bl_hex4
    call bl_lcommit
    call bl_lclr
    mov si, pd_s_lhead
    mov di, 2
    call bl_lput
    call bl_lcommit
    mov word [pd_ldn], PD_FW_SHOW
.e:
    mov ax, [es:PD_FW_IDX]          ; entry (idx - k) mod PD_FW_N; k < N, so
    xor dx, dx                      ; from idx mod N, less k, plus N if it
    mov bx, PD_FW_N                 ; went below 0
    div bx
    sub dx, [pd_ldn]
    jns .pos
    add dx, PD_FW_N
.pos:
    mov ax, dx
    mov bl, PD_FW_ESZ
    mul bl
    add ax, PD_FW_LOG
    mov bx, ax
    call bl_lclr
    mov al, [es:bx + 3]             ; the port: A9-A8, then A7-A0
    and al, 3
    mov di, 2
    call pd_hex2
    mov al, [es:bx]
    mov di, 4
    call pd_hex2
    mov al, [es:bx + 7]             ; the latched address, (A19-A0) << 12:
    mov di, 7                       ; byte 7 is A19-A12, byte 6 A11-A4, and
    call pd_hex2                    ; the top nibble of byte 5 A3-A0
    mov al, [es:bx + 6]
    mov di, 9
    call pd_hex2
    mov al, [es:bx + 5]
    mov di, 11
    call pd_hex2
    mov byte [bl_lscr + 12], ' '
    mov byte [bl_lscr + 13], 'e'
    mov al, [es:bx + 1]
    mov di, 14
    call pd_hex2
    mov byte [bl_lscr + 17], ':'
    mov si, 8                       ; the data samples
    mov di, 19
.d:
    mov al, [es:bx + si]
    call pd_hex2
    add si, 2
    add di, 3
    cmp si, PD_FW_ESZ
    jb .d
    call bl_lcommit
    call bl_lclr
    mov si, pd_s_lctl
    mov di, 7
    call bl_lput
    mov si, 9                       ; ...and the control samples under them
    mov di, 19
.c:
    mov al, [es:bx + si]
    call pd_hex2
    add si, 2
    add di, 3
    cmp si, PD_FW_ESZ
    jb .c
    call bl_lcommit
    dec word [pd_ldn]
    jnz .e
    call pd_repaint
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; pd_sweep - one row of [pd_swn] values, two copies each, under the label at
; SI. [pd_swk]: 0 the address's low byte n5, 4 the count's low byte nF, 6 the
; count's HIGH byte n with its low 3F
pd_sweep:
    push si
    mov di, pd_swl
    xor bx, bx                      ; BX = n
.n:
    mov word [pd_taddr], PD_TBASE
    mov word [pd_tcnt], PD_LEN - 1
    mov al, [pd_page]
    mov [pd_tpage], al
    mov al, bl
    mov cl, 4
    shl al, cl
    mov cl, [pd_swk]
    cmp cl, 0
    jne .c
    or al, 5                        ; n * 16 + 5
    mov [pd_taddr], al
    jmp short .go
.c:
    cmp cl, 4
    jne .h
    or al, 0x0F                     ; n * 16 + 15
    mov [pd_tcnt], al
    jmp short .go
.h:
    mov [pd_tcnt + 1], bl           ; 6: n, 3F
.go:
    call pd_try
    mov [di], al
    call pd_try
    mov [di + 1], al
    mov byte [di + 2], ' '
    add di, 3
    inc bx
    cmp bl, [pd_swn]
    jb .n
    mov byte [di], 0
    pop si
    mov di, pd_swl
    call bl_kvs
    call pd_repaint
    ret

; pd_detail - one plain copy with the address low byte AL, and the line under
; SI: what the card heard - its address, page and length
pd_detail:
    mov ah, PD_TBASE >> 8
    mov [pd_taddr], ax
    mov word [pd_tcnt], PD_LEN - 1
    mov al, [pd_page]
    mov [pd_tpage], al
    mov byte [pd_tstrat], 0
    call pd_try
    call bl_lclr
    xor di, di
    call bl_lput
    mov ax, [pd_haddr]
    mov di, BL_C_N
    call bl_hex4
    mov si, pd_s_page
    mov di, BL_C_N + 6
    call bl_lput
    mov al, [pd_hpage]
    xor ah, ah
    mov di, BL_C_N + 11
    call bl_hex4
    mov si, pd_s_len
    mov di, BL_C_N + 17
    call bl_lput
    mov ax, [pd_hlen]
    mov di, BL_C_N + 21
    call bl_hex4
    call bl_lcommit
    call pd_repaint
    ret

; pd_try - one single-cycle copy programmed by pd_prog_t; AL = its character,
; and [pd_haddr], [pd_hpage], [pd_hlen] what the card heard (all FFFFh for
; no copy). Every other register preserved
pd_try:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [pd_haddr], 0xFFFF
    mov word [pd_hpage], 0xFFFF
    mov word [pd_hlen], 0xFFFF
    mov es, [pd_bseg]
    mov bx, [pd_prm]
    mov word [es:bx], 0xFFFF
    mov word [es:bx + 2], 0xFFFF
    call pd_reset
    mov al, 0xD3                    ; speaker off, 22 kHz: 3 ms of nothing
    call pd_dw
    mov al, 0x40
    call pd_dw
    mov al, 0xD3
    call pd_dw
    call pd_prog_t
    mov al, 0x14
    call pd_dw
    mov al, PD_LEN - 1
    call pd_dw
    xor al, al
    call pd_dw
    mov cx, 18
    call pd_waitprm
    mov al, '-'
    jc .stop
    mov cx, 1                       ; the block is whole before the copy is
    call pd_wait                    ; asked for; a tick covers both
    call pd_lin                     ; DL:AX = where it copied from
    mov [pd_haddr], ax
    mov [pd_hpage], dl
    mov cx, [es:bx + 8]
    mov [pd_hlen], cx
    xor cx, cx                      ; CL = fields wrong, CH = the character
    mov ch, '.'
    cmp al, [pd_taddr]
    je .hi
    inc cl
    mov ch, 'L'
    cmp al, 0x02
    jne .hi
    mov ch, 'l'
.hi:
    cmp ah, [pd_taddr + 1]
    je .pg
    inc cl
    mov ch, 'H'
    cmp ah, 0x02
    jne .pg
    mov ch, 'h'
.pg:
    cmp dl, [pd_tpage]
    je .len
    inc cl
    mov ch, 'P'
    cmp dl, 0x03
    jne .len
    mov ch, 'p'
.len:
    cmp byte [pd_nolen], 0
    jne .sum
    cmp word [es:bx + 8], PD_LEN
    je .sum
    inc cl
    mov ch, 'C'
.sum:
    cmp cl, 1
    jbe .one
    mov ch, '*'
.one:
    mov al, ch
.stop:
    push ax
    call pd_reset
    mov al, 0x05
    out 0x0A, al
    pop ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pd_lin - ES:BX = the card's block: DL = the source's page, AX = its address
; in that page. Clobbers DH (0)
pd_lin:
    push cx
    mov ax, [es:bx + 2]
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    add ax, [es:bx]
    adc dl, 0
    pop cx
    ret

; pd_prog_t - channel 1 in sb.inc's order from [pd_taddr], [pd_tcnt],
; [pd_tmode] and [pd_tpage], the way [pd_tstrat] asks: PD_S_SLOW ~1 ms of
; nothing after every write, PD_S_TWICE the whole sequence twice, PD_S_PAGE
; the page written again after the rest. Every write is `out dx, al` (G read
; imm8 and dx the same, 64 of 64). Clobbers AX
pd_prog_t:
    push bx
    push cx
    push dx
    push si
    mov ax, [pd_taddr]
    mov [pd_seq + 5], al
    mov [pd_seq + 7], ah
    mov ax, [pd_tcnt]
    mov [pd_seq + 11], al
    mov [pd_seq + 13], ah
    mov al, [pd_tmode]
    mov [pd_seq + 15], al
    mov al, [pd_tpage]
    mov [pd_seq + 17], al
    mov [pd_seq + 19], al
    pushf
    cli
    mov bl, 1                       ; BL = passes
    test byte [pd_tstrat], PD_S_TWICE
    jz .pass
    inc bl
.pass:
    mov si, pd_seq
    mov cx, 9                       ; mask .. the page
.w:
    call pd_emit
    loop .w
    test byte [pd_tstrat], PD_S_PAGE
    jz .skip
    call pd_emit                    ; the page again
    jmp short .last
.skip:
    add si, 2
.last:
    call pd_emit                    ; unmask
    dec bl
    jnz .pass
    popf
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pd_emit - SI -> (port, value): written, SI past it, and ~1 ms of nothing
; after it under PD_S_SLOW. Clobbers AL, DX
;
; PD_S_ECHO is the cure G tests: the 1.x's core reads an IOW's data with
; gpio_get_all() one nop after its PIO switches the multiplexer to data
; (pm_isa_rp2040_m2_dev.h:155), so a read that lands early takes whatever
; the low data lines last carried - and in both ways PMDMA writes, that is
; the PORT NUMBER: `out imm8, al`'s own last byte, or the even byte of the
; (port, value) word the table emitter reads it from. Reading the value from
; an even address last puts the VALUE there instead
pd_emit:
    xor dh, dh
    mov dl, [si]
    mov al, [si + 1]
    test byte [pd_tstrat], PD_S_ECHO
    jz .out
    mov [pd_echo], al               ; ECHO: the value stored at an even
    mov al, [pd_echo]               ; address and read straight back, so the
.out:                               ; byte on SD0-7 as the OUT begins is the
    out dx, al                      ; value and not the port number
    add si, 2
    test byte [pd_tstrat], PD_S_SLOW
    jz .r
    push cx
    mov cx, 1500
.d:
    loop .d
    pop cx
.r:
    ret

; =============================================================================
; D - THE DRIVERS PAGE, AS THE KERNEL HOLDS IT. A field report: on the
; PicoMEM 1.x the USB Mouse tick will not come off while Ethernet's does, and
; nothing on the page says why. drv_unload frees a row unless its wait on
; [drv_wcnt] runs out, and the same untick frees on the CH375 path under
; MartyPC - so this reads what the machine itself holds: every row, the
; worker count, int 33h, the pointer class's published segment, and
; USBMOUSE.DRV's own four state bytes. Read-only; press D before the click
; and after it. The offsets are THIS build's (pmdma_k.inc, mkkern.py)
; =============================================================================
%include "pmdma_k.inc"

; =============================================================================
; S - SOUND.DRV AS IT IS PLAYING. On the field 286 a PicoMEM 1.x copies E's
; 16 kHz test stream at full rate - 250 copies and 62 block IRQs in 2 s - and
; plays Tracker and the Video Player in ~110 ms bursts ~0.6 s apart
; (docs/FIELD-NOTES.md 66). So this reads the driver's own stream record and
; counters while a player runs: start one, come back here and press S. Two
; snapshots a second apart, and the difference, so rates are on the page.
; Read-only; the offsets are THIS build's (pmdma_k.inc, mkkern.py)
; =============================================================================
pd_snd:
    push es
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov si, pd_n_snd
    call bl_sline
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, PDK_DRV_TAB
    mov cx, PDK_DRV_MAX
.f:
    cmp byte [es:bx + PDK_DRVR_CLASS], PDK_DRVC_SOUND
    jne .nx
    mov ax, [es:bx + PDK_DRVR_SEG]
    or ax, ax
    jnz .found
.nx:
    add bx, PDK_DRVR_SIZE
    loop .f
    mov si, pd_n_nosnd
    call bl_sline
    jmp .out
.found:
    mov [pd_sseg], ax
    mov si, pd_n_shdr
    call bl_sline
    call pd_dmasmp                  ; the live 8237 channel 1, first
    mov di, pd_sv0
    call .snap
    call pd_spin                    ; one second, counting loop passes
    mov di, pd_sv1
    call .snap
    mov word [pd_sptr], pd_stab
    xor bx, bx                      ; BX = the value's index * 2
.p:
    mov si, [pd_sptr]
    mov ax, [si]
    or ax, ax
    jz .out
    call bl_lclr
    mov si, ax
    mov di, 2
    call bl_lput
    mov ax, [pd_sv0 + bx]
    mov di, 40
    call bl_hex4
    mov ax, [pd_sv1 + bx]
    mov di, 47
    call bl_hex4
    sub ax, [pd_sv0 + bx]
    mov di, 54
    call bl_hex4
    call bl_lcommit
    add bx, 2
    add word [pd_sptr], 5
    jmp short .p
.out:
    pop es
    ret
.snap:                              ; every table row's value, as words at DI
    push es
    push si
    mov es, [pd_sseg]
    mov si, pd_stab
.s:
    cmp word [si], 0
    je .sd
    mov bx, [si + 2]
    mov ax, [es:bx]
    cmp byte [si + 4], 0
    jne .w
    xor ah, ah
.w:
    mov [di], ax
    add di, 2
    add si, 5
    jmp short .s
.sd:
    pop si
    pop es
    ret

; pd_spin - wait 18 ticks, counting passes of a short loop into
; [pd_spn] (dword): how much CPU this task got while the stream played. Taken
; with nothing playing as well, the ratio is the machine's lost share
pd_spin:
    push ax
    push cx
    push dx
    mov word [pd_spn], 0
    mov word [pd_spn + 2], 0
    call pd_tick
    mov dx, ax
.w0:
    call pd_tick                    ; start on a tick edge
    cmp ax, dx
    je .w0
    mov dx, ax
    mov cx, 18
.l:
    add word [pd_spn], 1
    adc word [pd_spn + 2], 0
    call pd_tick
    cmp ax, dx
    je .l
    mov dx, ax
    loop .l
    call bl_lclr
    mov si, pd_sl_spin
    mov di, 2
    call bl_lput
    mov ax, [pd_spn + 2]
    mov di, 40
    call bl_hex4
    mov ax, [pd_spn]
    mov di, 44
    call bl_hex4
    call bl_lcommit
    pop dx
    pop cx
    pop ax
    ret

; pd_dmasmp - channel 1's current count, read eight times back to back, and
; the status register: a count that MOVES between reads is the real 8237
; transferring, which a PicoMEM 1.x - no DMA line - never needs
pd_dmasmp:
    push ax
    push bx
    push cx
    push di
    call bl_lclr
    mov si, pd_sl_dsmp
    mov di, 2
    call bl_lput
    mov cx, 8
    mov di, 22
.r:
    pushf
    cli
    out 0x0C, al
    in al, 0x03
    mov bl, al
    in al, 0x03
    mov bh, al
    popf
    mov ax, bx
    call bl_hex4
    add di, 5
    loop .r
    call bl_lcommit
    call bl_lclr
    mov si, pd_sl_dst
    mov di, 2
    call bl_lput
    in al, 0x08
    xor ah, ah
    mov di, 40
    call bl_hex4
    call bl_lcommit
    pop di
    pop cx
    pop bx
    pop ax
    ret

; pd_mask - M: mask DMA channel 1 NOW and make SOUND.DRV keep it masked at
; every start ([sbl_drqmask] = 4); U: the reverse. A live A/B for a stream
; that plays in bursts: press it while the player runs and listen
pd_mask:
    push es
    push ax
    push bx
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, PDK_DRV_TAB
    mov cx, PDK_DRV_MAX
.f:
    cmp byte [es:si + PDK_DRVR_CLASS], PDK_DRVC_SOUND
    jne .nx
    mov ax, [es:si + PDK_DRVR_SEG]
    or ax, ax
    jnz .found
.nx:
    add si, PDK_DRVR_SIZE
    loop .f
    mov si, pd_n_nosnd
    call bl_sline
    jmp short .out
.found:
    mov es, ax
    mov al, 0x05                    ; masked...
    mov ah, 4
    mov si, pd_n_mask
    cmp bl, 'm'
    je .set
    mov al, 0x01                    ; ...or open
    xor ah, ah
    mov si, pd_n_unmask
.set:
    out 0x0A, al
    mov [es:PDK_SBL_DRQMASK], ah
    call bl_sline
.out:
    pop bx
    pop ax
    pop es
    ret

; pd_pump - P: flip the os8088-pm114 firmware's disk-loop audio pump. On a
; 1.x the Sound Blaster's copies and block IRQs come from the Pico's main
; loop, which a disk call holds for its whole length - so a video streamed
; from the card's own disk played with repeated sound on a clock ~30% slow
; (docs/FIELD-NOTES.md 66). That firmware pumps the SB worker inside the
; disk loop unless the byte at BIOS:5C09 is nonzero; this flips it, for an
; A/B on the same video
PD_FW_PUMPOFF equ PD_FW + 9

; pd_cardseg - CF = 0 and BX = [pd_bseg] = the card's BIOS segment, found
; the way R finds it: the test register's ramp, then PM BIOS function 0.
; Clobbers AX, CX, DX
pd_cardseg:
    mov dx, 0x2A3                   ; the card first: its test register
    pushf                           ; counts, an empty bus does not
    cli
    in al, dx
    mov ah, al
    mov cx, 8
.ramp:
    in al, dx
    inc ah
    cmp al, ah
    jne .nocard
    loop .ramp
    popf
    mov ax, 0x6000
    mov dx, 0x1234
    int 0x13
    cmp dx, 0xAA55
    jne .no
    mov [pd_bseg], bx
    clc
    ret
.nocard:
    popf
.no:
    stc
    ret

pd_pump:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    call pd_cardseg
    jc .nofw
    call pd_fwlog                   ; DMALOG2: the firmware with the switch
    jc .nofw
    mov es, bx
    xor byte [es:PD_FW_PUMPOFF], 1
    mov si, pd_n_pumpon
    cmp byte [es:PD_FW_PUMPOFF], 0
    je .say
    mov si, pd_n_pumpoff
    jmp short .say
.nofw:
    mov si, pd_n_nopump
.say:
    call bl_sline
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

; =============================================================================
; K - THE CARD'S DISK PATH. A sector reaches the program in two halves: the
; card reads it off the SD card into its 2KB transfer buffer at BIOS:5000h,
; then the PC copies it out with REP MOVSW (BIOS pm_cmd.asm), one 8-bit ISA
; read a byte. The streaming rate is set by the slower of the two, and only
; the copy needs the CPU. The rows time the copy itself; the 'DK01' record at
; BIOS:5BC0h (the firmware's os8088-pm114 branch, OS88_DIAG) says what the card
; measured. Press K, play or run VIDDISK, press K again.
; =============================================================================
PD_DB       equ 0x5000              ; the card's disk transfer buffer
PD_DK       equ 0x5BC0              ; 'DK01', past the DMA copy buffer's 768
PD_DK_STOCK equ PD_DK + 4           ; byte: nonzero = the stock per-sector read
PD_DK_RESET equ PD_DK + 5           ; byte: 1 = clear the counters

pd_disk:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov si, pd_n_dk
    call bl_sline
    call pd_cardseg
    jc .nocard
    mov [pd_kseg], bx
    call bl_baseline
    call bl_head
    mov word [bl_n], 64
    xor al, al                      ; method P: PIT-timed, interrupts off
    mov word [bl_body], pd_k_cw
    mov si, pd_k_l1
    call bl_run
    mov word [bl_body], pd_k_cb
    mov si, pd_k_l2
    call bl_run
    mov word [bl_body], pd_k_rw
    mov si, pd_k_l3
    call bl_run
    mov word [bl_body], pd_k_poll
    mov si, pd_k_l4
    call bl_run
    call bl_blank
    call pd_drec
    jmp short .out
.nocard:
    mov si, pd_n_nodkc
    call bl_sline
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

; pd_drec - the firmware's 'DK01' record, as report lines, then cleared. In:
; [pd_kseg]. Its own routine so tests/picomem.py, which has no kernel to time
; rows with, can run it alone
pd_drec:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    mov es, [pd_kseg]
    cmp word [es:PD_DK], 'DK'
    jne .norec
    cmp word [es:PD_DK + 2], '01'
    jne .norec
    mov si, pd_n_dkfast
    cmp byte [es:PD_DK_STOCK], 0
    je .mode
    mov si, pd_n_dkstk
.mode:
    call bl_sline
    mov si, pd_dktab
.r:
    mov ax, [si]
    or ax, ax
    jz .done
    push si
    mov bx, [si + 2]
    mov si, ax
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    mov cx, 10
    call bl_kv
    pop si
    add si, 4
    jmp short .r
.done:
    mov byte [es:PD_DK_RESET], 1    ; ...and the card starts a fresh record
    mov si, pd_n_dkclr
    jmp short .say
.norec:
    mov si, pd_n_nodk
.say:
    call bl_sline
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

; F - flip the firmware between its multi-sector read and the stock one
pd_dflip:
    push es
    push bx
    push si
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    call pd_cardseg
    jc .no
    mov es, bx
    cmp word [es:PD_DK], 'DK'
    jne .no
    xor byte [es:PD_DK_STOCK], 1
    mov si, pd_n_dkfast
    cmp byte [es:PD_DK_STOCK], 0
    je .say
    mov si, pd_n_dkstk
    jmp short .say
.no:
    mov si, pd_n_nodk
.say:
    call bl_sline
    pop si
    pop bx
    pop es
    ret

; -----------------------------------------------------------------------------
; I - THE BUS. Each row is 512 accesses of one kind with REP, so a row less
; its RAM twin is 512 bus cycles; the derived lines print that a byte. Card
; memory is the transfer buffer K copies from (written too: it is the disk
; read's scratch and nothing reads it between reads), card I/O the card's
; test port 2A3h, board I/O the keyboard controller's port B (61h), and the
; screen is only read
; -----------------------------------------------------------------------------
pd_isa:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov si, pd_n_isa
    call bl_sline
    call pd_cardseg
    jc .nocard
    mov [pd_kseg], bx
    call bl_baseline
    call bl_head
    mov word [bl_n], 64
    mov di, pd_isat
.row:
    mov ax, [di]
    or ax, ax
    jz .rows
    mov word [bl_body], ax           ; each row sets its own, from the table
    mov si, [di + 2]
    xor al, al
    call bl_run
    mov ax, [bl_lastus]             ; hundredths of a us for the 512
    mov dx, [bl_lastus + 2]
    mov bx, [di + 4]
    mov [bx], ax
    mov [bx + 2], dx
    add di, 6
    jmp short .row
.rows:
    call bl_blank
    mov si, pd_isa_l9               ; ns a byte, net of the RAM twin:
    mov ax, [pd_t_cr]               ; (row - twin) hundredths of a us / 512
    mov dx, [pd_t_cr + 2]           ; x 10 = ns
    mov bx, pd_t_rr
    call pd_isa_net
    mov si, pd_isa_l10
    mov ax, [pd_t_cw]
    mov dx, [pd_t_cw + 2]
    mov bx, pd_t_rw
    call pd_isa_net
    mov si, pd_isa_l11
    mov ax, [pd_t_vr]
    mov dx, [pd_t_vr + 2]
    mov bx, pd_t_rr
    call pd_isa_net
    mov si, pd_isa_l12
    mov ax, [pd_t_ci]
    mov dx, [pd_t_ci + 2]
    mov bx, pd_t_bi
    call pd_isa_net
    jmp short .out
.nocard:
    mov si, pd_n_nodkc
    call bl_sline
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pd_isa_net - SI = label, DX:AX = a row's hundredths of a us, BX -> its
; twin's: one line, (row - twin) * 10 / 512 ns a byte, 0 when it is under
pd_isa_net:
    push ax
    push cx
    push dx
    sub ax, [bx]
    sbb dx, [bx + 2]
    jnc .pos
    xor ax, ax
    xor dx, dx
.pos:
    mov cx, 10
    call bl_mul48
    mov cx, 512
    call bl_div48
    call bl_get32
    mov cx, 10
    call bl_kv
    pop dx
    pop cx
    pop ax
    ret

pd_isat:    dw pd_i_rr, pd_isa_l1, pd_t_rr
            dw pd_i_cr, pd_isa_l2, pd_t_cr
            dw pd_i_vr, pd_isa_l3, pd_t_vr
            dw pd_i_rw, pd_isa_l4, pd_t_rw
            dw pd_i_cw, pd_isa_l5, pd_t_cw
            dw pd_i_ci, pd_isa_l6, pd_t_ci
            dw pd_i_bi, pd_isa_l7, pd_t_bi
            dw 0

pd_i_rr:                            ; RAM read: REP LODSB
    mov si, pd_ring
    mov cx, 512
    cld
    rep lodsb
    ret
pd_i_cr:                            ; card memory read
    push ds
    mov ds, [cs:pd_kseg]
    mov si, PD_DB
    mov cx, 512
    cld
    rep lodsb
    pop ds
    ret
pd_i_vr:                            ; screen memory read (A000h)
    push ds
    mov ax, 0xA000
    mov ds, ax
    xor si, si
    mov cx, 512
    cld
    rep lodsb
    pop ds
    ret
pd_i_rw:                            ; RAM write: REP STOSB
    push es
    push cs
    pop es
    mov di, pd_ring
    mov cx, 512
    xor al, al
    cld
    rep stosb
    pop es
    ret
pd_i_cw:                            ; card memory write
    push es
    mov es, [pd_kseg]
    mov di, PD_DB
    mov cx, 512
    xor al, al
    cld
    rep stosb
    pop es
    ret
pd_i_ci:                            ; card I/O read, 2A3h
    push es
    push cs
    pop es
    mov di, pd_ring
    mov dx, 0x2A3
    mov cx, 512
    cld
.l:
    in al, dx                       ; IN + STOSB, not INSB: an 8088 runs this
    stosb
    loop .l
    pop es
    ret
pd_i_bi:                            ; board I/O read, 61h: the twin
    push es
    push cs
    pop es
    mov di, pd_ring
    mov dx, 0x61
    mov cx, 512
    cld
.l:
    in al, dx                       ; IN + STOSB, not INSB: an 8088 runs this
    stosb
    loop .l
    pop es
    ret

pd_n_isa:   db 'I  THE BUS: 512 accesses a row (REP), then ns a byte net of RAM', 0
pd_isa_l1:  db 'RAM read x512 lodsb', 0
pd_isa_l2:  db 'card mem read x512', 0
pd_isa_l3:  db 'screen read x512', 0
pd_isa_l4:  db 'RAM write x512 stosb', 0
pd_isa_l5:  db 'card mem write x512', 0
pd_isa_l6:  db 'card I/O read x512', 0
pd_isa_l7:  db 'board I/O read x512', 0
pd_isa_l9:  db 'card read, ns a byte', 0
pd_isa_l10: db 'card write, ns a byte', 0
pd_isa_l11: db 'screen read, ns a byte', 0
pd_isa_l12: db 'card I/O over board, ns', 0

; the bodies: one 512-byte sector each way
pd_k_cw:                            ; card -> RAM, REP MOVSW: the BIOS's copy
    push ds
    push es
    push cs
    pop es
    mov di, pd_ring
    mov ds, [cs:pd_kseg]
    mov si, PD_DB
    mov cx, 256
    cld
    rep movsw
    pop es
    pop ds
    ret

pd_k_cb:                            ; card -> RAM, REP MOVSB
    push ds
    push es
    push cs
    pop es
    mov di, pd_ring
    mov ds, [cs:pd_kseg]
    mov si, PD_DB
    mov cx, 512
    cld
    rep movsb
    pop es
    pop ds
    ret

pd_k_rw:                            ; RAM -> RAM, REP MOVSW: the reference
    push es
    push cs
    pop es
    mov di, pd_ring
    mov si, pd_ring + 512
    mov cx, 256
    cld
    rep movsw
    pop es
    ret

pd_k_poll:                          ; 512 single reads of one card byte: the
    push es                         ; handshake's polling loop
    mov es, [pd_kseg]
    mov cx, 512
.p:
    cmp byte [es:PD_DB], 0
    loop .p
    pop es
    ret

pd_dktab:   dw pd_dk_l0, PD_DK + 8
            dw pd_dk_l1, PD_DK + 12
            dw pd_dk_l2, PD_DK + 16
            dw pd_dk_l3, PD_DK + 20
            dw pd_dk_l4, PD_DK + 24
            dw pd_dk_l5, PD_DK + 28
            dw pd_dk_l6, PD_DK + 32
            dw pd_dk_l7, PD_DK + 36
            dw pd_dk_l8, PD_DK + 40
            dw 0
pd_n_dk:    db 'K  THE CARD',39,'S DISK PATH: the copy, and what the card measured', 0
pd_k_l1:    db 'card->RAM 512 movsw', 0
pd_k_l2:    db 'card->RAM 512 movsb', 0
pd_k_l3:    db 'RAM->RAM 512 movsw', 0
pd_k_l4:    db 'card byte read x512', 0
pd_n_dkfast: db '  read path: MULTI-SECTOR (F flips it)', 0
pd_n_dkstk: db '  read path: STOCK, a sector at a time (F flips it)', 0
pd_n_nodk:  db '  this card firmware keeps no disk record', 0
pd_n_nodkc: db '  no PicoMEM answered', 0
pd_n_dkclr: db '  (cleared: read now, then press K again)', 0
pd_dk_l0:   db 'read calls', 0
pd_dk_l1:   db 'sectors read', 0
pd_dk_l2:   db '...by the multi-sector path', 0
pd_dk_l3:   db 'us on the SD card', 0
pd_dk_l4:   db 'us waiting for the PC', 0
pd_dk_l5:   db 'us in read calls', 0
pd_dk_l6:   db 'SD us per sector', 0
pd_dk_l7:   db 'PC-wait us per sector', 0
pd_dk_l8:   db 'call us per sector', 0

; =============================================================================
; A - THE CARD'S AUDIO HEALTH, since the last A. The os8088-pm114 firmware
; counts, at BIOS:5F80h ('AH01', or 'AH02' with B's switch), how the Sound Blaster's own stages fared:
; the FIFO found empty, its lowest level, the backlog of samples the timer
; asked for, the longest gap between worker passes, the longest wait for the
; PC to copy, and the disk calls. A video streamed from the card's own disk
; drops 2-6 ms of sound several times a second and plays clean from RAM
; (docs/FIELD-NOTES.md 66). Press A, play, press A: the second page is that
; play alone (A asks the card to clear the record after reading it).
; =============================================================================
PD_AH       equ 0x5F80
PD_AH_RESET equ PD_AH + 4
PD_AH_STOCK equ PD_AH + 5           ; 'AH02' firmware only: see pd_bloc

pd_ah:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov si, pd_n_ah
    call bl_sline
    call pd_cardseg
    jc .none
    mov es, bx
    cmp word [es:PD_AH], 'AH'
    jne .none
    xor dx, dx                      ; DX = 1: the AH03 rows follow
    cmp word [es:PD_AH + 2], '01'
    je .old
    cmp word [es:PD_AH + 2], '02'
    je .new
    cmp word [es:PD_AH + 2], '03'
    je .ah3
    cmp word [es:PD_AH + 2], '04'
    je .ah4
    cmp word [es:PD_AH + 2], '05'
    je .ah5
    cmp word [es:PD_AH + 2], '06'
    je .ah5
    cmp word [es:PD_AH + 2], '07'
    je .ah5
    cmp word [es:PD_AH + 2], '08'
    jne .none
    inc dx                          ; AH08: the stale copy it dropped
.ah5:
    inc dx                          ; AH05: where the last silences fell
.ah4:
    inc dx                          ; AH04: the silence-by-site table too
.ah3:
    inc dx
.new:
    mov si, pd_n_blbig                ; AH02 on: say which sound path ran
    cmp byte [es:PD_AH_STOCK], 0
    je .mode
    mov si, pd_n_blstk
.mode:
    push dx
    call bl_sline
    pop dx
.old:
    mov si, pd_ahtab
.r:
    mov ax, [si]
    or ax, ax
    jnz .row
    or dx, dx
    jz .done
    dec dx                          ; past one table's end: the next one's
    add si, 2
    jmp short .r
.row:
    push dx
    call bl_lclr
    push si
    mov si, ax
    mov di, 2
    call bl_lput
    pop si
    mov bx, [si + 2]                ; the counter's offset
    mov ax, [es:bx + 2]
    mov di, 44
    call bl_hex4
    mov ax, [es:bx]
    mov di, 48
    call bl_hex4
    call bl_lcommit
    pop dx
    add si, 4
    jmp short .r
.done:
    mov byte [es:PD_AH_RESET], 1    ; ...and the card starts a fresh record
    mov si, pd_n_ahclr
    call bl_sline
    jmp short .out
.none:
    mov si, pd_n_noah
    call bl_sline
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

; =============================================================================
; B - the card's SOUND PATH, os8088's or stock. The 'AH02' firmware copies a
; Sound Blaster stream from the PC 512 bytes a trip and holds 1,024 samples
; in hand before it plays, where stock copies 128 and holds 512; 'AH03'
; also runs the mixer inside the SD driver's waits. The speaker's queue is
; 4 mixed buffers (5 ms) and only core 0's main loop refills it, so a card
; slow to answer (~42 ms) emptied it and dropped sound (docs/FIELD-NOTES.md
; 66). The byte at BIOS:5F85 picks stock when nonzero; this flips it.
; =============================================================================
pd_bloc:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    call pd_cardseg
    jc .nofw
    mov es, bx
    cmp word [es:PD_AH], 'AH'
    jne .nofw
    cmp word [es:PD_AH + 2], '02'
    je .flip
    cmp word [es:PD_AH + 2], '03'
    je .flip
    cmp word [es:PD_AH + 2], '04'
    je .flip
    cmp word [es:PD_AH + 2], '05'
    je .flip
    cmp word [es:PD_AH + 2], '06'
    je .flip
    cmp word [es:PD_AH + 2], '07'
    jne .nofw
.flip:
    xor byte [es:PD_AH_STOCK], 1
    mov si, pd_n_blbig
    cmp byte [es:PD_AH_STOCK], 0
    je .say
    mov si, pd_n_blstk
    jmp short .say
.nofw:
    mov si, pd_n_nobl
.say:
    call bl_sline
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    ret

pd_drv:
    push es
    mov word [bl_nrow], 0
    mov word [bl_used], 0
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov si, pd_n_drv
    call bl_sline
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, pd_r_wcnt
    mov al, [es:PDK_DRV_WCNT]
    xor ah, ah
    call pd_kv
    call bl_lclr                    ; int 33h, and class 6's published seg
    mov si, pd_r_i33
    xor di, di
    call bl_lput
    push es
    xor ax, ax
    mov es, ax
    mov ax, [es:0x33 * 4 + 2]
    mov dx, [es:0x33 * 4]
    pop es
    mov di, BL_C_N
    call bl_hex4
    mov byte [bl_lscr + BL_C_N + 4], ':'
    mov ax, dx
    mov di, BL_C_N + 5
    call bl_hex4
    mov si, pd_s_ptr
    mov di, BL_C_N + 11
    call bl_lput
    mov ax, [es:PDK_DRV_FSEG6]
    mov di, BL_C_N + 19
    call bl_hex4
    call bl_lcommit
    mov si, pd_s_dhdr
    mov di, pd_s_dhdr2
    call bl_kvs
    mov bx, PDK_DRV_TAB
    xor cx, cx
.row:
    call bl_lclr
    mov si, [es:bx + PDK_DRVR_TITLE] ; the row's own name, out of the kernel
    mov di, 2
.t:
    mov al, [es:si]
    or al, al
    jz .td
    cmp di, BL_C_N - 1
    jae .td
    mov [bl_lscr + di], al
    inc si
    inc di
    jmp short .t
.td:
    mov ax, [es:bx + PDK_DRVR_SEG]
    mov di, BL_C_N
    call bl_hex4
    mov ax, [es:bx + PDK_DRVR_ENT]
    mov di, BL_C_N + 5
    call bl_hex4
    mov al, [es:bx + PDK_DRVR_CLASS]
    call .hex2
    mov di, BL_C_N + 10
    call bl_hex4
    mov al, [es:bx + PDK_DRVR_WANT]
    call .hex2
    mov di, BL_C_N + 15
    call bl_hex4
    mov al, [es:bx + PDK_DRVR_ERR]
    call .hex2
    mov di, BL_C_N + 20
    call bl_hex4
    call bl_lcommit
    cmp byte [es:bx + PDK_DRVR_CLASS], PDK_DRVC_POINT
    jne .next
    mov ax, [es:bx + PDK_DRVR_SEG]  ; the pointer driver's own bytes
    or ax, ax
    jz .next
    push es
    mov es, ax
    call bl_lclr
    mov si, pd_r_um
    xor di, di
    call bl_lput
    mov al, [es:PDK_UM_PM]
    call .hex2
    mov di, BL_C_N
    call bl_hex4
    mov al, [es:PDK_UM_ALIVE]
    call .hex2
    mov di, BL_C_N + 5
    call bl_hex4
    mov al, [es:PDK_UM_STOP]
    call .hex2
    mov di, BL_C_N + 10
    call bl_hex4
    mov al, [es:PDK_UM_BTN]
    call .hex2
    mov di, BL_C_N + 15
    call bl_hex4                    ; pm alive stop btn, under seg ent cls want
    pop es
    call bl_lcommit
.next:
    add bx, PDK_DRVR_SIZE
    inc cx
    cmp cx, PDK_DRV_MAX
    jb .row
    pop es
    ret
; AL -> AX, so bl_hex4 prints it as 00nn
.hex2:
    xor ah, ah
    ret

%define BL_NUMBERED                 ; a field run saves NAME1, NAME2... (benchlib)
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

pd_tpl:
    dw 7, 22, 632, 448
    dw pd_ttl, pd_paint, pd_onkey, pd_onclick

pd_ttl:     db 'PicoMEM DMA', 0

pd_s_title: db 'PMDMA - where a PicoMEM 1.x copies a DMA transfer from', 0
pd_n_snd:   db 'S  SOUND.DRV, now and one second later (start a player first)', 0
pd_n_mask:  db 'M: DMA channel 1 MASKED now, and SOUND.DRV keeps it so at every start', 0
pd_n_unmask: db 'U: DMA channel 1 OPEN now, and SOUND.DRV opens it at every start', 0
pd_sl_spin: db 'loop passes in that second (hex)', 0
pd_sl_dsmp: db '8237 ch1 count x8', 0
pd_sl_dst:  db '8237 status (08h)', 0
pd_n_pumpon:  db 'P: the card feeds the Sound Blaster DURING disk calls (fixed)', 0
pd_n_pumpoff: db 'P: the card feeds the Sound Blaster only BETWEEN disk calls (stock)', 0
pd_n_nopump:  db 'P: this card firmware has no disk-loop pump switch', 0
pd_n_blbig: db 'B: copies 512, FIFO starts 1,024, mixes in SD waits (os8088)', 0
pd_n_blstk: db 'B: copies 128, FIFO starts 512, no SD-wait mixing (stock)', 0
pd_n_nobl:  db 'B: this card firmware has no sound path switch', 0
pd_n_ah:    db "A  THE CARD'S SOUND BLASTER, since the last A (hex, 32-bit)", 0
pd_n_noah:  db '  this card firmware keeps no audio health record', 0
pd_n_ahclr: db '  (cleared: play now, then press A again)', 0
pd_ahtab:   dw pd_ah_und, PD_AH + 8
            dw pd_ah_min, PD_AH + 12
            dw pd_ah_lvl, PD_AH + 44
            dw pd_ah_bck, PD_AH + 16
            dw pd_ah_gap, PD_AH + 20
            dw pd_ah_wkn, PD_AH + 40
            dw pd_ah_cpy, PD_AH + 24
            dw pd_ah_cpn, PD_AH + 28
            dw pd_ah_i13, PD_AH + 32
            dw pd_ah_i1m, PD_AH + 36
            dw pd_ah_i1s, PD_AH + 48
            dw 0
            dw pd_ah_sil, PD_AH + 52      ; AH03 only
            dw pd_ah_slm, PD_AH + 56
            dw pd_ah_sdm, PD_AH + 60
            dw pd_ah_hep, PD_AH + 64
            dw 0
            dw pd_as_0, PD_AH + 68        ; AH04 only: what core 0 ran
            dw pd_as_1, PD_AH + 72        ; at each silent buffer (the
            dw pd_as_2, PD_AH + 76        ; firmware's pm_site.h)
            dw pd_as_3, PD_AH + 80
            dw pd_as_4, PD_AH + 84
            dw pd_as_5, PD_AH + 88
            dw pd_as_6, PD_AH + 92
            dw pd_as_7, PD_AH + 96
            dw pd_as_8, PD_AH + 100
            dw pd_as_9, PD_AH + 104
            dw pd_as_10, PD_AH + 108
            dw pd_as_11, PD_AH + 112
            dw 0
            dw pd_ah_slt, PD_AH + 116     ; AH05 only
            dw pd_ah_smx, PD_AH + 120
            dw 0
            dw pd_ah_stl, PD_AH + 124     ; AH08 only
            dw 0
pd_ah_und:  db 'FIFO empty (AH05: dry spells mid-play)', 0
pd_ah_min:  db 'AH07: DSP rate at last start, Hz', 0
pd_ah_lvl:  db 'FIFO highest level (AH06)', 0
pd_ah_bck:  db 'most samples asked, not yet in FIFO', 0
pd_ah_gap:  db 'longest gap between SB worker passes, us', 0
pd_ah_wkn:  db 'SB worker passes', 0
pd_ah_cpy:  db 'longest copy request to PC ack, us', 0
pd_ah_cpn:  db 'copy requests', 0
pd_ah_i13:  db 'AH07: its block bytes | autoinit<<16', 0
pd_ah_i1m:  db 'AH07: DMA starts', 0
pd_ah_i1s:  db 'AH07: block IRQs sent', 0
pd_ah_sil:  db 'speaker fed SILENCE, 1.29 ms buffers', 0
pd_ah_slm:  db '...the longest run of them', 0
pd_ah_sdm:  db 'mixes run inside SD card waits', 0
pd_ah_stl:  db 'AH08: stale copy bytes dropped at a start', 0
pd_ah_hep:  db 'samples DROPPED, FIFO full (AH06)', 0
pd_as_0:    db 'silent while: the main loop', 0
pd_as_1:    db 'silent while: USB host', 0
pd_as_2:    db 'silent while: SB worker', 0
pd_as_3:    db 'silent while: mixing', 0
pd_as_4:    db 'silent while: 100 ms housekeeping', 0
pd_as_5:    db 'silent while: a PC command, not int 13h', 0
pd_as_6:    db 'silent while: int 13h, other', 0
pd_as_7:    db 'silent while: FatFs / SD transfer', 0
pd_as_8:    db 'silent while: SD card wait', 0
pd_as_9:    db 'silent while: PC sector copy', 0
pd_as_10:   db 'silent while: waiting on a PC command', 0
pd_ah_slt:  db 'last silence: site, PM command (hex)', 0
pd_ah_smx:  db 'longest run: site, PM command (hex)', 0
pd_as_11:   db 'silent while: network send', 0
pd_n_nosnd: db '  SOUND.DRV is not loaded.', 0
pd_n_shdr:  db '                                        now    +1 s   diff', 0
; (label, offset in SOUND.DRV, 0 = byte / 1 = word)
pd_stab:    dw pd_sl_act, PDK_SBL_STR_ACT
            db 0
            dw pd_sl_st, PDK_SBL_STR_STATE
            db 0
            dw pd_sl_pau, PDK_SBL_PAUSED
            db 0
            dw pd_sl_exp, PDK_SBL_EXPECT
            db 0
            dw pd_sl_aut, PDK_SBL_AUTO
            db 0
            dw pd_sl_dir, PDK_SBL_DIRECT
            db 0
            dw pd_sl_ext, PDK_SBL_EXT
            db 0
            dw pd_sl_rng, PDK_SBL_RING_ON
            db 0
            dw pd_sl_hs, PDK_SBL_HISP
            db 0
            dw pd_sl_wid, PDK_SBL_WIDE
            db 0
            dw pd_sl_flg, PDK_SBL_OFLAGS
            db 0
            dw pd_sl_tc, PDK_SBL_TC
            db 0
            dw pd_sl_half, PDK_SBL_HALF
            db 1
            dw pd_sl_rm, PDK_SBL_RMASK
            db 1
            dw pd_sl_irq, PDK_SBL_IRQ
            db 0
            dw pd_sl_base, PDK_SBL_BASE
            db 1
            dw pd_sl_drq, PDK_SBL_DRQMASK
            db 0
            dw pd_sl_wd, PDK_SBL_WD
            db 1
            dw pd_sl_wdr, PDK_SBL_WD_RST
            db 1
            dw pd_sl_ply, PDK_SBL_PLAY
            db 0
            dw pd_sl_val, PDK_SBL_VALID
            db 1
            dw pd_sl_tot, PDK_SBL_TOTAL
            db 1
            dw pd_sl_fed, PDK_SBL_FED
            db 1
            dw pd_sl_con, PDK_SBL_CONSUMED
            db 1
            dw pd_sl_und, PDK_SBL_UNDER
            db 1
            dw 0
pd_sl_act:  db 'stream open', 0
pd_sl_st:   db 'state: 0 play 1 underrun 2 ended', 0
pd_sl_pau:  db 'paused (D0h sent)', 0
pd_sl_exp:  db 'a block IRQ expected', 0
pd_sl_aut:  db 'auto-init', 0
pd_sl_dir:  db 'direct: the card reads the ring', 0
pd_sl_ext:  db 'external ring', 0
pd_sl_rng:  db 'ring mode', 0
pd_sl_hs:   db 'high-speed', 0
pd_sl_wid:  db 'wide (41h)', 0
pd_sl_flg:  db 'open flags', 0
pd_sl_tc:   db 'time constant', 0
pd_sl_half: db 'block (half) bytes', 0
pd_sl_rm:   db 'ring mask', 0
pd_sl_irq:  db 'IRQ', 0
pd_sl_base: db 'DSP base', 0
pd_sl_drq:  db 'DRQ1 kept masked (4)', 0
pd_sl_wd:   db 'watchdog / ticks left', 0
pd_sl_wdr:  db 'watchdog reload', 0
pd_sl_ply:  db 'half playing', 0
pd_sl_val:  db 'halves valid (hi 1, lo 0)', 0
pd_sl_tot:  db 'bytes fed (total)', 0
pd_sl_fed:  db 'bytes copied to the buffer', 0
pd_sl_con:  db 'bytes played (consumed)', 0
pd_sl_und:  db 'underrun pauses', 0
pd_n_drv:   db 'D  DRIVERS, as the kernel holds them (press D again to refresh)', 0
pd_r_wcnt:  db '  workers (drv_wcnt)', 0
pd_r_i33:   db '  int 33h', 0
pd_s_ptr:   db 'pointer', 0
pd_s_dhdr:  db '  row', 0
pd_s_dhdr2: db 'seg  ent  cls  want err', 0
pd_r_um:    db '  USBMOUSE pm alive stop btn', 0
pd_s_hint:  db 'R D S M/U P A B - see FIELD-NOTES 66', 0
pd_s_nocard: db 'No PicoMEM answered, or its ROM has no copy loop I know.', 0
pd_s_nodsp: db 'No DSP at 220-260h: load SOUND.DRV with the SB first.', 0

pd_r_if:    db 'interrupts at run', 0
pd_s_on:    db 'on', 0
pd_s_off:   db 'OFF - nothing can copy', 0
pd_r_card:  db 'card BIOS segment', 0
pd_s_irq:   db 'IRQ', 0
pd_s_noupd: db 'DMANoUpdate', 0
pd_r_dsp:   db 'DSP base', 0
pd_s_ver:   db 'ver', 0

pd_names:   dw pd_n_a, pd_n_b, pd_n_c, pd_n_d
pd_progs:   dw pd_prog_a, pd_prog_b, pd_prog_c, pd_prog_d
pd_n_a:     db 'A  sb.inc order, back to back (SOUND.DRV)', 0
pd_n_b:     db 'B  sb.inc order, I/O reads between writes', 0
pd_n_c:     db 'C  PMINIT order (mode, addr, page, count)', 0
pd_n_d:     db 'D  sb.inc order, page written first', 0

pd_r_prog:  db 'programmed', 0
pd_r_card2: db 'card copied from', 0
pd_r_data:  db 'bytes intact', 0
pd_s_page:  db 'page', 0
pd_s_len:   db 'len', 0
pd_s_cnt:   db 'count', 0
pd_s_right: db 'RIGHT', 0
pd_s_wrong: db 'WRONG ADDRESS', 0
pd_s_wpage: db 'WRONG PAGE ONLY', 0
pd_s_nocopy: db 'NO COPY in a second', 0
pd_s_of64:  db 'of 64; card has', 0
pd_r_8237:  db '8237 ch1 now: addr', 0

pd_v_nocopy: db 'VERDICT: the card copied nothing - its DMA snoop is off?', 0
pd_v_allok: db 'VERDICT: every way copies from the right place.', 0
pd_v_page:  db 'VERDICT: the card never sees the page register (83h).', 0
pd_v_fast:  db 'VERDICT: back-to-back 8237 writes are missed; spaced work.', 0
pd_v_mixed: db 'VERDICT: mixed - photograph this and send it.', 0

pd_vtab_e:  dw pd_e_nocp, pd_e_out, pd_e_loud, pd_e_noirq, pd_e_ok
pd_s_erun:  db 'E RUNNING - if the machine stops here, the stream did it.', 0
pd_n_e:     db 'E  auto-init, VERIFIED; start k: ring lo/count, E = echoed writes', 0
pd_n_e2:    db '   1 as found/07FF 2 same,E 3 00/07FF,E 4 00/0800,E 5 same,E+slow', 0
pd_n_e3:    db '   6 00/0400,E 7 same,E twice 8 00/0200', 0
pd_ecfg:    db 0xFF                 ; start k: the ring start's low byte (FF
            dw 0x07FF               ; = as found), the count, and how the
            db 0                    ; writes are made. 1 is SOUND.DRV today
            db 0xFF
            dw 0x07FF
            db PD_S_ECHO
            db 0x00
            dw 0x07FF
            db PD_S_ECHO
            db 0x00
            dw 0x0800
            db PD_S_ECHO
            db 0xFF
            dw 0x07FF
            db PD_S_ECHO | PD_S_SLOW
            db 0x00
            dw 0x0400
            db PD_S_ECHO
            db 0xFF
            dw 0x07FF
            db PD_S_ECHO | PD_S_TWICE
            db 0x00
            dw 0x0200
            db 0
pd_r_tries: db '  starts until heard', 0
pd_r_fnew:  db '  after 1st copy: addr', 0
pd_n_g:     db 'G  sweeps: . right, l h p heard as the PORT NO., L H P other,', 0
pd_n_g2:    db '   C count wrong, * two or more, - no copy; 2 copies a value', 0
pd_r_gnib:  db '  high nibble', 0
pd_s_gnib:  db '0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F', 0
pd_g_lo:    db '  lo n5, count 3F', 0
pd_n_gfw:   db 'G  THE CARD LOGS EACH DMA WRITE: the address it latched, 16 bus samples', 0
pd_n_gfw2:  db '   ctrl: 01 MEMR 02 MEMW 04 IOR 08 IOW 10 mux=address 20 A8-19 40 wait', 0
pd_n_gfw3:  db '   samples ~90 ns apart from the stock read on; the stock read is used', 0
pd_g_lofe:  db '  lo n5, EARLY (stock)', 0
pd_s_fwsig: db 'DMALOG2', 0
pd_s_file:  db 'PMDMA.TXT', 0
pd_s_sfile: db 'PMSND.TXT', 0
pd_s_kfile: db 'PMDISK.TXT', 0
pd_s_ifile: db 'PMBUS.TXT', 0
pd_s_ltick: db '16 samples took this many cycles (hex)', 0
pd_s_lhead: db 'port addr  early: data samples, oldest first', 0
pd_s_lctl:  db 'ctrl', 0
pd_g_loe:   db '  lo n5, ECHOED', 0
pd_g_chi:   db '  count hi n, lo 3F', 0
pd_g_chie:  db '  count hi n, ECHOED', 0
pd_g_cnte:  db '  count lo nF, ECHOED', 0
pd_g_h45:   db '  lo 45 heard as', 0
pd_g_hc5:   db '  lo C5 heard as', 0
pd_r_ring:  db 'ring', 0
pd_r_ncp:   db '  copies the card asked', 0
pd_r_nout:  db '  ...outside the ring', 0
pd_r_nloud: db '  bytes not silence', 0
pd_r_nirq:  db '  block IRQs (want ~63)', 0
pd_r_nhw:   db '  ...as real IRQs', 0
pd_r_blk:   db 'copy block at', 0
pd_e_nocp:  db 'E: the card asked for no copy at all.', 0
pd_e_out:   db 'E: COPIES LEAVE THE RING - the stream reads the wrong place.', 0
pd_e_loud:  db 'E: the card copied bytes that were not ours - not silence.', 0
pd_e_noirq: db 'E: copies fine, but no block interrupt reached us.', 0
pd_e_ok:    db 'E: the stream behaves - copies in the ring, silence, IRQs.', 0
pd_lines:   db 3, 5, 7
pd_bits:    db 0x08, 0x20, 0x80
pd_r_imr:   db '  8259 mask found', 0
pd_s_open:  db 'opened', 0
pd_r_fout:  db '  first outside from', 0
pd_isrs:    dw pd_isr3, pd_isr5, pd_isr7

pd_ports:   dw 0x220, 0x240, 0x230, 0x250, 0x260, 0

pd_win:     dw 0
pd_cx:      dw 0
pd_cy:      dw 0
pd_cw:      dw 0
pd_ch:      dw 0
pd_bseg:    dw 0
pd_kseg:    dw 0
pd_t_rr:    dw 0, 0                 ; I's rows, hundredths of a us each
pd_t_cr:    dw 0, 0
pd_t_vr:    dw 0, 0
pd_t_rw:    dw 0, 0
pd_t_cw:    dw 0, 0
pd_t_ci:    dw 0, 0
pd_t_bi:    dw 0, 0
pd_prm:     dw 0
pd_noupd:   dw 0
pd_base:    dw 0
pd_boff:    dw 0
pd_addr:    dw 0
pd_eseg:    dw 0
pd_eoff:    dw 0
pd_ver:     db 0, 0
pd_page:    db 0
pd_cirq:    db 0
pd_var:     db 0
pd_got:     db 0
pd_res:     db 0, 0, 0, 0       ; PD_R_* per variant, A B C D
pd_rbase:   dw 0                ; E: the silent region's address in its page
pd_rlen:    dw 0                ; E: this start's ring, in bytes
pd_tries:   db 0                ; E: starts until the card heard the ring
pd_fnaddr:  dw 0                ; E: the 8237 the card meant to leave
pd_fnsize:  dw 0
pd_taddr:   dw 0                ; pd_prog_t's address, count, mode and
pd_tcnt:    dw 0                ; page - and the four reads before the page
pd_tmode:   db 0
pd_tpage:   db 0
pd_tstrat:  db 0                ; PD_S_*: how pd_prog_t writes
pd_nolen:   db 0                ; G: the length is not scored
pd_swk:     db 0                ; G: what pd_sweep sweeps
pd_swn:     db 16               ; G: ...and over how many values
    align 2
pd_echo:    dw 0                ; PD_S_ECHO's value: EVEN, so it is the byte
                                ; on SD0-7 when it is read
pd_haddr:   dw 0                ; what the card heard, off pd_try
pd_hpage:   dw 0
pd_hlen:    dw 0
pd_seq:     db 0x0A, 0x05, 0x0C, 0, 0x02, 0, 0x02, 0, 0x0C, 0, 0x03, 0
            db 0x03, 0, 0x0B, 0, 0x83, 0, 0x83, 0, 0x0A, 0x01
                                ; pd_prog_t's (port, value) pairs, filled in
pd_swl:     times 16 * 3 + 1 db 0
pd_rlin:    dw 0, 0
pd_last:    dw 0, 0
pd_tend:    dw 0
pd_ncp:     dw 0
pd_ldn:     dw 0
pd_sseg:    dw 0
pd_spn:     dw 0, 0
pd_sptr:    dw 0
pd_sv0:     times 32 dw 0
pd_sv1:     times 32 dw 0
pd_nout:    dw 0
pd_nloud:   dw 0
pd_nirq:    dw 0
pd_nhw:     dw 0
pd_fout:    dw 0, 0
pd_flen:    dw 0
pd_imr:     db 0
pd_open:    db 0
pd_old:     dw 0, 0, 0, 0, 0, 0

PD_BSS_OWN  equ PD_LEN * 8 + PD_RBUF * 2
PD_BSS_TOTAL equ PD_BSS_OWN + BL_BSS_SIZE + 16
    OS88_BSS PD_BSS_TOTAL
    OS88_IMAGE_END

section .bss
pd_pat:     resb PD_LEN * 8         ; four variants' patterns; the second
                                    ; run is the fallback when the first
                                    ; crosses a page
pd_ring:    resb PD_RBUF * 2        ; the stream's region; the second half is
                                    ; the fallback when the first crosses
pd_bl:      resb BL_BSS_SIZE
pd_bss_end:
section .text

    BL_BSS pd_bl
