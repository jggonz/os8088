; =============================================================================
; os8088 - apps/telnet/teansi.inc
;
; THE ANSI-BBS PARSER (SPEC.md 70.9)
;
; SPEC.md 70's "recognised and discarded" was the honest choice while nothing
; here acted on a sequence. What replaces it is not printing them either - it
; is ACTING on them, and the discard survives as the fall-through for every
; sequence still not implemented.
;
; **A STATE MACHINE OVER SINGLE BYTES, WITH NO LOOKAHEAD AND NO BUFFER.** That
; is the property that matters on this machine and not a style: bytes arrive in
; whatever fragments TCP hands over, an escape sequence is routinely split
; across two NETV_RECV calls, and a parser that searched a buffer would have to
; reassemble one first. Every byte of state lives in bss - there is not one
; local carried across a call - so a stream split at ANY offset behaves like
; one that is not split at all.
;
; **tools/ansisim.py IS THE SECOND READER** (SPEC.md 70.12), the same state
; machine in Python, and tests/telansi.py compares all 4,000 bytes of con_scr
; with what it produced for the same stream. Where this file and that one
; disagree, one of them is wrong and the diff says which cell - which is
; htmsim's role for the browser and weavesim's for Weave. **Never change
; ansisim to agree with this file.**
;
; WHAT IS NOT HERE. The renderers (SPEC.md 70.8), which consume the same dirty
; bitmap this marks; the Telnet option layer (te_byte, SPEC.md 70.10.1), which
; is above this and hands it application bytes; and the Zmodem receiver
; (SPEC.md 70.11), which is a file of its own (apps/telnet/tezm.inc) and which
; te_zdetect hands the stream to at tz_start.
; =============================================================================

; --- the states (SPEC.md 70.9.1) ---------------------------------------------
; Seven states and one sub-state, which is the contract's own count. PS_STRE is
; the sub-state: the one byte of lookahead a string terminator needs, spelled as
; a STATE so that a stream split between the ESC and the backslash behaves like
; one that is not - which is 70.9's whole shape in one line.
PS_GROUND   equ 0
PS_ESC      equ 1
PS_CSIE     equ 2                   ; CSI_ENTRY - no parameter seen yet
PS_CSIP     equ 3                   ; CSI_PARAM
PS_CSII     equ 4                   ; CSI_IGNORE - consumed to its final, no-op
PS_STR      equ 5                   ; OSC / DCS / APC
PS_STRE     equ 6                   ; ...and the ESC inside one
PS_MUSIC    equ 7                   ; ESC[M / ESC[N, swallowed to 0x0E

TE_PMAX     equ 8                   ; parameters; the ninth goes to a SINK
TE_ANS      equ 12                  ; the DSR answer is at most `ESC[25;80R`

; -----------------------------------------------------------------------------
; te_pbyte - AL = one application byte through the parser
; in:  AL = the byte; the worker's task, NO gfx lock (rule 3: con_scr is written
;      lock-free and te_show settles the debt under the lock)
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
te_pbyte:
    push ax
    push bx
    inc word [te_soff]              ; the STREAM offset, which is what
                                    ; ansisim's `offset` is and what
                                    ; [te_zat] is published in
    mov bl, [te_pst]
    or bl, bl
    jnz .notground

    ; --- GROUND: the detector runs FIRST, and it draws on the way past -----
    ; SPEC.md 70.9.6. The matched bytes are put on the screen as ordinary
    ; glyphs by the fall-through below, so at handover the glass holds `**B0`:
    ; the 0x18 is consumed as CAN and the final `0` is never drawn, because the
    ; detector fires on it and this proc returns before te_ground sees it.
    call te_zdetect
    cmp byte [te_zon], 0
    jne .out
    call te_ground
    jmp short .out

.notground:
    ; **THE DETECTOR ADVANCES ONLY IN GROUND** (SPEC.md 70.9.6): a byte
    ; consumed anywhere else resets it, so `*` `CSI 0 m` `*` 0x18 `B` `0` `0`
    ; does not fire. Two stars separated by a sequence are two stars of
    ; somebody's artwork.
    mov byte [te_zdet], 0
    cmp bl, PS_ESC
    je .esc
    cmp bl, PS_CSII
    jbe .csi                        ; PS_CSIE, PS_CSIP, PS_CSII
    cmp bl, PS_STR
    je .str
    cmp bl, PS_STRE
    je .stre
    call te_music
    jmp short .out
.esc:
    call te_escape
    jmp short .out
.csi:
    call te_csibyte
    jmp short .out
.str:
    call te_string
    jmp short .out
.stre:
    call te_stresc
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE ZMODEM AUTO-START DETECTOR (SPEC.md 70.9.6)
;
; The six bytes `*` `*` 0x18 `B` `0` `0` - a hex ZRQINIT header's opening.
;
; **A MISMATCH MAY NOT SIMPLY RESTART THE COUNT**, because the pattern repeats
; its first byte. A detector that fell back to "one matched" MISSES `***\x18B00`
; - after three stars the correct state is "two matched", not one. So five
; bytes of table and a loop that RE-TESTS the byte at the shorter count, which
; is Knuth-Morris-Pratt's failure function for a six-byte pattern and is the
; whole of it.
; =============================================================================
te_zpat:    db '*', '*', 0x18, 'B', '0', '0'
te_zfail:   db 0, 0, 1, 0, 0, 0

te_zdetect:                         ; AL = the byte
    push ax
    push bx
    push cx
    mov cl, [te_zdet]
.try:
    mov bl, cl
    xor bh, bh
    cmp al, [te_zpat + bx]
    je .adv
    or cl, cl
    jz .save                        ; nothing matched: stay at zero
    mov cl, [te_zfail + bx]
    jmp short .try                  ; ...and RE-TEST at the shorter count
.adv:
    inc cl
    cmp cl, 6
    jb .save
    xor cl, cl
    mov [te_zdet], cl
    call tz_start                   ; the handover: everything from here is
    jmp short .out                  ; the receiver's (SPEC.md 70.11)
.save:
    mov [te_zdet], cl
.out:
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; GROUND
; =============================================================================
te_ground:                          ; AL = the byte
    cmp al, 0x1B
    je .esc
    cmp al, 0x18                    ; CAN and SUB are CONSUMED in GROUND and
    je .drop                        ; not dropped: 0x18 is ZDLE and the third
    cmp al, 0x1A                    ; byte of the auto-start sequence, so a
    je .drop                        ; detector that never saw it could not fire
    call te_isctl
    jc te_control                   ; one of the seven, tail-called
    jmp con_putc                     ; ...and EVERY other byte is a CP437 GLYPH,
.esc:                               ; 0x00-0x1F and 0x7F included: BBS art is
    mov byte [te_pst], PS_ESC       ; made of them (SPEC.md 70.9.2)
    call te_pclear
.drop:
    ret

; --- te_isctl - CF=1 if AL is one of the SEVEN recognised controls -----------
; The seven are named rather than expressed as a range, which is the single
; biggest difference between this and a VT-family parser: in GROUND every OTHER
; byte below 0x20 is a glyph. **All seven are below 0x10**, so the list is a
; sixteen-bit mask rather than a thirty-two-byte table - bits 0, 7, 8, 9, 10, 12
; and 13, which are NUL, BEL, BS, HT, LF, FF and CR.
TE_CTLMASK  equ 0x3781

te_isctl:
    push ax
    push cx
    cmp al, 0x10
    jae .no
    mov cl, al
    mov ax, 1
    shl ax, cl                      ; `shl reg, CL` is 8086; `shl reg, imm` is
    test ax, TE_CTLMASK             ; not (SPEC.md 1)
    jz .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop cx                          ; ...and `pop` writes no flag on an 8086,
    pop ax                          ; so one epilogue serves both answers
    ret

; --- te_pclear - a new sequence: parameters, prefix and the sink -------------
; The three are ADJACENT bytes of bss, so the first two go in one word store -
; and the assertion below is what keeps that true, because a bss reorder that
; separated them would leave this clearing somebody else's byte and silently
; not clearing the sink.
; (the assertion is at the foot of telnet.asm, where the bss equs exist)
te_pclear:
    mov word [te_pn], 0
    mov byte [te_ppfx], 0
    ret

; =============================================================================
; THE CONTROLS (SPEC.md 70.9.2)
; =============================================================================
te_control:                         ; AL = one of the seven
    cmp al, 0x00
    je .none                        ; NUL: dropped, and it is the ONLY byte
    cmp al, 0x07                    ; GROUND drops
    je te_bell                      ; ...and neither of these two MOVES the
                                    ; cursor, so neither owes a mark. The five
                                    ; below all do, and a board sends CR and LF
                                    ; by the thousand - so the marks are here
                                    ; and not in a prologue every byte pays
    push ax
    call con_markcur                 ; the row the cursor is LEAVING owes a
                                    ; redraw: the underline is composed into
                                    ; its band (SPEC.md 70.8.2)
    cmp al, 0x08
    je .bs
    cmp al, 0x09
    je .ht
    cmp al, 0x0A
    je .lf
    cmp al, 0x0C
    je .ff
    cmp al, 0x0D
    je .cr
.out:
    call con_markcur
    pop ax
.none:
    ret

.bs:                                ; left one, stopping at column 0, AND NO
    mov byte [con_pwrap], 0          ; ERASE: a board draws with the cursor
    cmp word [con_cx], 0
    je .out
    dec word [con_cx]
    jmp short .out

.ht:                                ; to the next multiple of eight, clamped to
    mov byte [con_pwrap], 0          ; column 79, and NO ERASE. con_putc wrote
    mov ax, [con_cx]                 ; eight spaces before this file, which is
    add ax, 8                       ; why a board's aligned menu came out with
    and ax, 0FFF8h                  ; holes in it
    cmp ax, CON_COLS
    jb .htset
    mov ax, CON_COLS - 1
.htset:
    mov [con_cx], ax
    jmp short .out

.lf:                                ; **AN INDEX ONLY - NO CARRIAGE RETURN.**
    mov byte [con_pwrap], 0          ; A terminal that homes the column on a
    call con_nextrow                 ; line feed puts every second line of a
    jmp short .out                  ; two-column menu against the left margin

.cr:
    mov byte [con_pwrap], 0
    mov word [con_cx], 0
    jmp short .out

.ff:                                ; clear with the CURRENT attribute, home
    mov byte [con_pwrap], 0
    call con_clear
    jmp short .out

; --- te_bell - and it NEVER BLOCKS (SPEC.md 70.9.2) -------------------------
; OSAPI_SND_TONE programs the sink and stamps an owner record; snd_tick retires
; the duration inside IRQ0, so nothing here waits. It REFUSES when a
; higher-priority owner holds the channel, and a refused beep is a beep that
; does not happen - which is correct: a board that sends BEL forty times in an
; ANSI animation must not be able to stop the terminal.
te_bell:
    push ax
    push cx
    push dx
    mov ax, 880
    mov cx, 2
    mov dl, 0x40                    ; the priority packages use
    call OSAPI_SND_TONE             ; ...which preserves BX, as every API slot
    pop dx                          ; preserves everything but its documented
    pop cx                          ; outputs (CLAUDE.md)
    pop ax
    ret

; =============================================================================
; ESCAPE
; =============================================================================
te_escape:                          ; AL = the byte after ESC
    cmp al, 0x1B
    je .stay                        ; **ESC ESC RESTARTS AND DOES NOT SWALLOW.**
                                    ; A stream that lost bytes between two
                                    ; sequences must not eat the second one's
                                    ; introducer
    cmp al, 0x18
    je .abort
    cmp al, 0x1A
    je .abort
    cmp al, 0x7F
    je .stay                        ; ignored, and never drawn
    cmp al, 0x20
    jae .print
    call te_isctl
    jc .exec                        ; one of the seven executes and STAYS here
    ret                             ; every other C0: ignored, never drawn
.exec:
    jmp te_control
.stay:
    ret
.abort:
    mov byte [te_pst], PS_GROUND
    ret
.print:
    cmp al, '['
    je .csi
    cmp al, ']'                     ; OSC
    je .string
    cmp al, 'P'                     ; DCS
    je .string
    cmp al, '_'                     ; APC
    je .string
    cmp al, '7'
    je .save
    cmp al, '8'
    je .restore
    mov byte [te_pst], PS_GROUND    ; anything else is SWALLOWED
    ret
.csi:
    mov byte [te_pst], PS_CSIE
    jmp te_pclear
.string:
    mov byte [te_pst], PS_STR
    ret
.save:                              ; the cursor POSITION, into the ONE slot
    mov byte [te_pst], PS_GROUND    ; CSI s uses - and never the attribute
    jmp te_savecur
.restore:
    mov byte [te_pst], PS_GROUND
    jmp te_restcur

; --- te_savecur / te_restcur - ONE SLOT, shared by ESC 7/8 and CSI s/u ------
; Two slots would be two states to keep in step for no gain (SPEC.md 70.9.3).
te_savecur:
    push ax
    mov ax, [con_cx]
    mov [te_sx], ax
    mov ax, [con_cy]
    mov [te_sy], ax
    pop ax
    ret

; **AND IT CLAMPS**, which is defence in depth rather than a rule of the
; protocol: the slot is written by one routine and read by another, and the w3
; review's BLOCKER 1 was a THIRD writer - OSAPI_FSX_MODE, through a te_fsi that
; aliased these two words - putting 47,104 in [te_sx]. con_celloff has no range
; check and con_putc is the one unguarded write in the package, so a cursor that
; cannot be out of range by construction is worth two compares.
te_restcur:
    push ax
    call con_markcur
    mov ax, [te_sx]
    cmp ax, CON_COLS
    jb .cxok
    mov ax, CON_COLS - 1
.cxok:
    mov [con_cx], ax
    mov ax, [te_sy]
    cmp ax, CON_ROWS
    jb .cyok
    mov ax, CON_ROWS - 1
.cyok:
    mov [con_cy], ax
    mov byte [con_pwrap], 0
    call con_markcur
    pop ax
    ret

; =============================================================================
; STRING (OSC / DCS / APC) and its one byte of lookahead
; =============================================================================
te_string:
    cmp al, 0x1B
    je .esc
    cmp al, 0x07
    je .end                         ; BEL ends a string AND DOES NOT RING
    cmp al, 0x18
    je .end
    cmp al, 0x1A
    je .end
    ret                             ; everything else is swallowed, C0 included
.esc:
    mov byte [te_pst], PS_STRE
    ret
.end:
    mov byte [te_pst], PS_GROUND
    ret

; **ESC then anything but a backslash ends the string AND HANDS THE BYTE TO
; ESC** - vtparse's rule. `ESC ] junk ESC [ 31 m` leaves the colour set, where
; a parser that swallowed the byte would lose the whole CSI behind it.
te_stresc:
    cmp al, '\'
    je .st
    mov byte [te_pst], PS_ESC
    jmp te_escape
.st:
    mov byte [te_pst], PS_GROUND
    ret

; =============================================================================
; MUSIC (SPEC.md 70.9.1) - swallowed to 0x0E
;
; 0x0E is CP437's eighth note and a GLYPH in GROUND; it terminates a music
; string only inside MUSIC. [te_pst] is what tells the two apart, which is the
; whole reason this is a state machine and not a flag.
; =============================================================================
te_music:
    cmp al, 0x0E
    je .end
    cmp al, 0x18
    je .end
    cmp al, 0x1A
    je .end
    cmp al, 0x1B
    je .restart                     ; ...so a board that sent ESC[M and then
    cmp al, 0x20                    ; crashed cannot swallow the rest of the
    jae .swallow                    ; session
    call te_isctl
    jc te_control                   ; execute, and stay
.swallow:
    ret
.end:
    mov byte [te_pst], PS_GROUND
    ret
.restart:
    mov byte [te_pst], PS_ESC
    jmp te_pclear

; =============================================================================
; THE CSI STATES (SPEC.md 70.9.1)
; =============================================================================
te_csibyte:                         ; AL = the byte, BL = the state
    cmp al, 0x1B
    je .restart
    cmp al, 0x18
    je .abort
    cmp al, 0x1A
    je .abort
    cmp al, 0x7F
    je .ignore                      ; ignored, and never drawn
    cmp al, 0x20
    jb .c0
    cmp al, 0x40
    jb .notfinal
    cmp al, 0x7E
    ja .notfinal                    ; 0x80..0xFF is NOT a final: it falls to
                                    ; the intermediate rule below and makes the
                                    ; sequence a no-op, which is what a stray
                                    ; art byte inside a CSI deserves
    ; --- 0x40..0x7E: A FINAL BYTE, ALWAYS, dispatched or not --------------
    mov byte [te_pst], PS_GROUND
    cmp bl, PS_CSII
    je .ignore                      ; consumed, and nothing happens
    push bx
    cmp bl, PS_CSIE
    jne .notentry
    mov bl, 1                       ; BL = "the sequence is BARE", which is the
    jmp short .disp                 ; whole of the music test below
.notentry:
    mov bl, 0
.disp:
    call te_csidisp
    pop bx
    ret
.notfinal:
    cmp bl, PS_CSII
    je .ignore                      ; a no-op sequence eats its parameters too
    cmp al, '0'
    jb .interm                      ; 0x20..0x2F
    cmp al, '9'
    jbe .digit
    cmp al, ';'
    je .semi
    cmp al, 0x3C
    jb .interm                      ; ':' - a subparameter, which this does not
    cmp al, 0x3F                    ; have and therefore will not act on
    jbe .priv                       ; 0x3C..0x3F
.interm:
    ; AN INTERMEDIATE, and the WHOLE SEQUENCE BECOMES A NO-OP. It is consumed
    ; to its final and draws nothing - a parser that ignored the space in
    ; `ESC[1 q` would have executed `CSI 1 q`.
    mov byte [te_pst], PS_CSII
.ignore:
    ret
.abort:
    mov byte [te_pst], PS_GROUND
    ret
.restart:
    mov byte [te_pst], PS_ESC
    jmp te_pclear
.c0:
    ; **ONE OF THE SEVEN EXECUTES AND THE SEQUENCE CONTINUES**, which is DEC's
    ; rule and the one that matters on a board: a host that sends `CSI 1;` CR
    ; `1H` has put a carriage return in the middle of a cursor move, and a
    ; parser that aborted would lose the move. Every OTHER C0 is IGNORED and
    ; never drawn - a byte that is art where it is art is noise where it lands
    ; inside a sequence.
    call te_isctl
    jc te_control
    ret

.priv:
    cmp bl, PS_CSIE
    jne .noop                       ; a private prefix AFTER a parameter is the
    cmp byte [te_ppfx], 0           ; same no-op as an intermediate
    jne .noop                       ; ...and only the FIRST one is the prefix
    mov [te_ppfx], al
    ret
.noop:
    mov byte [te_pst], PS_CSII
    ret

.semi:
    mov byte [te_pst], PS_CSIP
    call te_p0
    cmp byte [te_pn], TE_PMAX
    jae .sink                       ; **THE NINTH AND BEYOND GO TO A SINK**,
    push bx                         ; not into the eighth: accumulating them
    mov bl, [te_pn]                 ; there would have set an eighth parameter
    xor bh, bh                      ; of 3144 for `...;31;44m`
    mov byte [te_prm + bx], 0
    inc byte [te_pn]
    pop bx
    ret
.sink:
    mov byte [te_psink], 1
    ret

.digit:
    mov byte [te_pst], PS_CSIP
    cmp byte [te_psink], 0
    jne .ignore
    call te_p0
    push ax
    push bx
    push cx
    push dx
    mov bl, [te_pn]
    dec bl
    xor bh, bh
    mov cl, al
    sub cl, '0'
    mov al, [te_prm + bx]
    xor ah, ah
    mov dx, 10
    mul dx                          ; **MUL WRITES DX** (SPEC.md 1) - and DX is
    xor ch, ch                      ; pushed, so the ten survives nothing
    add ax, cx
    cmp ax, 255
    jbe .dset
    mov ax, 255                     ; ...SATURATING, so `ESC[99999;1H` is
.dset:                              ; `ESC[255;1H` and then clamps to row 25
    mov [te_prm + bx], al
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- te_p0 - make sure parameter 0 exists, as ansisim's `params = [0]` does --
te_p0:
    cmp byte [te_pn], 0
    jne .out
    mov byte [te_prm], 0
    mov byte [te_pn], 1
.out:
    ret

; =============================================================================
; THE CSI TABLE (SPEC.md 70.9.3)
; in:  AL = the final byte, BL = 1 if the sequence came from CSI_ENTRY
;
; Every clamp is to the 80x25 buffer. `Pn` is parameter 0 unless stated, and
; the DEFAULT is the handler's rather than the parser's - `CSI J` defaults to 0
; and `CSI A` to 1, so a parser that supplied one would be wrong for the other.
; =============================================================================
te_csidisp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; --- ANSI music, and the design's one genuine collision ----------------
    ; A BARE `CSI M` or `CSI N` - no parameter, no private prefix, no
    ; intermediate - is MUSIC; anything with a parameter is not. So Delete Line
    ; is reachable only as `CSI <n> M`, and the whole rule is this one test.
    ; An intermediate never reaches here at all, having made the sequence a
    ; no-op.
    or bl, bl
    jz .noprefix
    cmp byte [te_ppfx], 0
    jne .noprefix
    cmp al, 'M'
    je .music
    cmp al, 'N'
    je .music
.noprefix:
    cmp byte [te_ppfx], 0
    jne .private

    ; --- the two readings of parameter 0 -----------------------------------
    xor cx, cx                      ; CX = raw0: 0 where none was given
    mov dx, 1                       ; DX = n: the ANSI default of 1
    cmp byte [te_pn], 0
    je .have
    mov cl, [te_prm]
    or cl, cl
    jz .have
    mov dx, cx
.have:
    ; **THE MARK IS A DIFFERENCE AND NOT A HABIT.** A board's art is mostly
    ; SGR, and marking the cursor's row for every final would redraw a row that
    ; did not change - 80 cells composed and blitted - thousands of times a
    ; screen. So the position is remembered, the final runs, and the two rows
    ; are marked only if it MOVED (the cursor's underline is in the band, so
    ; the row it left owes a redraw as much as the row it arrived on). The
    ; finals that change a CELL mark their own rows in con_fillcells.
    mov si, [con_cy]
    mov di, [con_cx]
    call te_final
    cmp di, [con_cx]
    jne .moved
    cmp si, [con_cy]
    je .out
.moved:
    mov bx, si
    call con_mark
    call con_markcur
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.music:
    mov byte [te_pst], PS_MUSIC
    jmp short .out

.private:
    ; **ONLY `?25h` / `?25l` IS ACTED ON, and it acts if ANY parameter is 25**,
    ; so `CSI ?25;7l` hides the cursor. Every other `?`, `=`, `>` and `<`
    ; sequence is consumed and ignored: a board sends `?7h` (autowrap), `?33`
    ; and `=255`, which either describe what this already does or describe a
    ; thing it does not have, and consuming them silently is the difference
    ; between a clean screen and a screen with `?7h` written across it.
    cmp byte [te_ppfx], '?'
    jne .out
    cmp al, 'h'
    je .decset
    cmp al, 'l'
    jne .out
.decset:
    mov dl, 0
    cmp al, 'h'
    jne .dscan
    mov dl, 1
.dscan:
    mov cl, [te_pn]
    or cl, cl
    jz .out                         ; no parameter: nothing is 25
    xor bx, bx
.dnext:
    cmp byte [te_prm + bx], 25
    jne .dmore
    mov [con_cvis], dl
    call con_markcur                 ; the underline appears or goes
    jmp short .out
.dmore:
    inc bx
    dec cl
    jnz .dnext
    jmp short .out

; -----------------------------------------------------------------------------
; te_final - AL = the final, CX = raw0, DX = n (the ANSI default applied)
; -----------------------------------------------------------------------------
; A TABLE AND NOT A COMPARE CHAIN. Twenty-three `cmp al, imm` / `je` pairs are
; four bytes each; a row here is three, and the scan is the same handful of
; cycles a chain averages. BX is free - te_csidisp banked it, and no handler
; below reads it on entry.
te_ftab:
    db 'A'
    dw te_f_cuu
    db 'B'
    dw te_f_cud
    db 'C'
    dw te_f_cuf
    db 'D'
    dw te_f_cub
    db 'E'
    dw te_f_cnl
    db 'F'
    dw te_f_cpl
    db 'G'
    dw te_f_cha
    db 'H'
    dw te_f_cup
    db 'f'
    dw te_f_cup                     ; HVP is CUP, and always was
    db 'J'
    dw te_f_ed
    db 'K'
    dw te_f_el
    db 'L'
    dw te_f_il
    db 'M'
    dw te_f_dl                      ; ...reachable only WITH a parameter: a
                                    ; bare `CSI M` never gets here (te_csidisp)
    db '@'
    dw te_f_ich
    db 'P'
    dw te_f_dch
    db 'X'
    dw te_f_ech
    db 'S'
    dw te_f_su
    db 'T'
    dw te_f_sd
    db 's'
    dw te_savecur                   ; the ONE slot, and never the attribute
    db 'u'
    dw te_restcur
    db 'n'
    dw te_f_dsr
    db 'c'
    dw te_f_da
    db 'm'
    dw te_f_sgr
    db 0

te_final:
    mov bx, te_ftab
.scan:
    mov ah, [bx]
    or ah, ah
    jz .done                        ; an unknown final is CONSUMED and ignored,
    cmp ah, al                      ; which is SPEC.md 70's discard now scoped
    je .hit                         ; to the one place it belongs
    add bx, 3
    jmp short .scan
.hit:
    jmp word [bx+1]
.done:
    ret

; --- the cursor-motion finals -----------------------------------------------
; Every one of them ASSIGNS a row or a column, which is exactly the closed list
; of things that clears the pending wrap (SPEC.md 70.9.5).
; Four finals and two shapes: BACK, which floors at 0, and FORWARD, which
; ceilings at the axis's last index. BX carries WHICH coordinate - te_csidisp
; banked it - so the arithmetic is written once each way rather than four
; times, and a clamp that was right in three places and wrong in the fourth is
; not a shape this can take.
te_f_cuu:
    mov bx, con_cy
    jmp short te_f_back
te_f_cub:
    mov bx, con_cx
te_f_back:
    mov byte [con_pwrap], 0
    mov ax, [bx]
    sub ax, dx
    jns .set
    xor ax, ax
.set:
    mov [bx], ax
    ret

te_f_cud:
    mov bx, con_cy
    mov cx, CON_ROWS - 1
    jmp short te_f_fwd
te_f_cuf:
    mov bx, con_cx
    mov cx, CON_COLS - 1
te_f_fwd:
    mov byte [con_pwrap], 0
    mov ax, [bx]
    add ax, dx
    cmp ax, cx
    jbe .set
    mov ax, cx
.set:
    mov [bx], ax
    ret

te_f_cnl:
    call te_f_cud                   ; ...and the column afterwards rather than
    jmp short te_f_col0             ; before, which is the same answer and one
te_f_cpl:                           ; store instead of two
    call te_f_cuu
te_f_col0:
    mov word [con_cx], 0
    ret

te_f_cha:
    mov byte [con_pwrap], 0
    mov ax, dx
    dec ax
    cmp ax, CON_COLS
    jb .set
    mov ax, CON_COLS - 1
.set:
    mov [con_cx], ax
    ret

te_f_cup:                           ; 1-BASED, both axes, and clamped
    mov byte [con_pwrap], 0
    mov ax, dx                      ; DX is already parameter 0's default-1
    dec ax
    cmp ax, CON_ROWS
    jb .rset
    mov ax, CON_ROWS - 1
.rset:
    mov [con_cy], ax
    mov ax, 1
    cmp byte [te_pn], 2
    jb .cset                        ; a missing second parameter is 1
    mov al, [te_prm + 1]
    or al, al
    jnz .cset
    mov al, 1                       ; ...and so is an explicit zero
.cset:
    xor ah, ah
    dec ax
    cmp ax, CON_COLS
    jb .cput
    mov ax, CON_COLS - 1
.cput:
    mov [con_cx], ax
    ret

; --- the erases -------------------------------------------------------------
; **SPACE AND THE CURRENT ATTRIBUTE fills every gap on this screen** - not just
; an erase's - which is ANSI.SYS's convention and not the VT's, and is what
; makes a board's coloured background work: it sets a blue background, clears
; the screen, and gets a blue screen (SPEC.md 70.9.3).
te_f_ed:
    or cx, cx
    jz .from
    cmp cx, 1
    je .to
    cmp cx, 2
    je .all
    ret                             ; any other Pn does nothing
.from:
    call te_here
    mov cx, CON_CELLS
    sub cx, ax
    jmp con_fillcells
.to:
    call te_here
    mov cx, ax
    inc cx
    xor ax, ax
    jmp con_fillcells
.all:                               ; **and it HOMES the cursor** - ANSI.SYS's
    mov byte [con_pwrap], 0          ; behaviour and not the VT's, and every
    jmp con_clear                    ; board on the wire was written against
                                    ; ANSI.SYS

te_f_el:
    call te_rowbase                 ; AX = the row's first cell
    or cx, cx
    jz .from
    cmp cx, 1
    je .to
    cmp cx, 2
    je .whole
    ret
.from:
    add ax, [con_cx]
    mov cx, CON_COLS
    sub cx, [con_cx]
    jmp con_fillcells
.to:
    mov cx, [con_cx]
    inc cx
    jmp con_fillcells
.whole:
    mov cx, CON_COLS
    jmp con_fillcells

te_f_ech:
    call te_here
    mov cx, CON_COLS
    sub cx, [con_cx]
    cmp cx, dx
    jbe con_fillcells
    mov cx, dx
    jmp con_fillcells

; --- line and character editing ---------------------------------------------
; **NONE OF THESE MOVES THE CURSOR**, they operate on rows cy..24 and columns
; cx..79 (there are no scroll regions), and their counts clamp to what is left.
; Six finals, three pairs, and each pair is one clamp with the routine to call
; carried in SI. The count clamps to WHAT IS LEFT - rows cy..24 or columns
; cx..79 - because there are no scroll regions here and a count larger than the
; region is a count the region absorbs.
te_f_il:
    mov si, te_insline
    jmp short te_f_rows
te_f_dl:
    mov si, te_delline
te_f_rows:
    mov ax, CON_ROWS
    sub ax, [con_cy]
    jmp short te_f_clamp

te_f_ich:
    mov si, te_inschar
    jmp short te_f_cols
te_f_dch:
    mov si, te_delchar
te_f_cols:
    mov ax, CON_COLS
    sub ax, [con_cx]
te_f_clamp:
    cmp dx, ax
    jbe .go
    mov dx, ax
.go:
    mov ax, dx
    jmp si

te_f_su:
    mov si, con_scrollup
    jmp short te_f_screen
te_f_sd:
    mov si, con_scrolldown
te_f_screen:
    mov ax, CON_ROWS
    jmp short te_f_clamp

; --- what the terminal SAYS BACK --------------------------------------------
; The answers go out through the TX queue as WHOLE MESSAGES (SPEC.md 70.10.3):
; half of a DSR reply on the wire is a reply the host waits for the end of.
te_f_dsr:
    cmp cx, 6
    je .pos
    cmp cx, 5
    je .ok
    ret
.pos:                               ; `ESC [ <row> ; <col> R`, 1-BASED
    push es
    push ds
    pop es                          ; ...ES is the KERNEL's on a callback and
    cld                             ; te_ans is OURS (SPEC.md 20)
    mov di, te_ans
    mov al, 0x1B
    stosb
    mov al, '['
    stosb
    mov ax, [con_cy]
    inc ax
    call te_dec
    mov al, ';'
    stosb
    mov ax, [con_cx]
    inc ax
    call te_dec
    mov al, 'R'
    stosb
    pop es
    mov cx, di
    sub cx, te_ans
    mov si, te_ans
    jmp te_reply
.ok:
    mov si, te_s_dsrok
    mov cx, 4
    jmp te_reply

te_f_da:                            ; **ONLY for Pn = 0 and only with NO
    or cx, cx                       ; private prefix**, so `CSI ?c` is consumed
    jnz .out                        ; silently (the prefix arm never reaches
    mov si, te_s_da                 ; here)
    mov cx, 7
    jmp te_reply
.out:
    ret

te_s_dsrok: db 0x1B, '[', '0', 'n'
te_s_da:    db 0x1B, '[', '?', '1', ';', '0', 'c'

; --- te_dec - AX (0..255) as decimal at ES:DI, no leading zeros --------------
; Three divisions written out rather than a digit stack. The stack form -
; `push dx` per digit and `loop` them back off - is what every hand-rolled
; itoa does and `tools/stkbalance.py` cannot pair it: the count lives in a
; register, so a static walk sees a routine that returns at a depth it was not
; entered at (SPEC.md 8.7.3's own exemption exists for exactly two routines in
; the tree, and a number under 256 does not need to be the third).
te_dec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx                      ; CL = 1 once a digit has been written, so
    xor dx, dx                      ; a leading zero is suppressed and a bare
    mov bx, 100                     ; zero still prints
    div bx                          ; AL = hundreds, DX = the rest
    or al, al
    jz .tens
    add al, '0'
    stosb
    mov cl, 1
.tens:
    mov ax, dx
    xor dx, dx
    mov bx, 10
    div bx                          ; AL = tens, DL = units
    or al, al
    jnz .temit
    or cl, cl
    jz .units
.temit:
    add al, '0'
    stosb
.units:
    mov al, dl
    add al, '0'
    stosb
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; SGR IS APPLIED TO LOGICAL STATE, AND THE ATTRIBUTE IS DERIVED (SPEC.md 70.9.4)
;
; **REVERSE IS A FLAG, NOT A SWAP OF THE ATTRIBUTE'S NIBBLES**, and it is the
; single easiest thing in the parser to get wrong. `CSI 7;31m` must mean BLACK
; ON RED. A parser that swaps the nibbles when it sees the `7` and then writes
; the `31` into the low nibble draws BLUE ON WHITE - a plausible picture,
; entirely wrong, and one that only shows up on the boards that put the 7
; first.
;
; So six logical bytes, and [con_attr] is derived from them after every SGR:
;
;   fg = te_lfg & 0x0F              bg = te_lbg & 0x07
;   if te_rev:  fg, bg = (bg | (fg & 8)), (fg & 7)      ; intensity stays on fg
;   if te_con:  fg = bg
;   con_attr = (te_blk ? 0x80 : 0) | ((bg & 7) << 4) | (fg & 0x0F)
;
; The four check values, which this and ansisim must both produce:
; `CSI 0;7m` 0x70, `CSI 1;7m` 0x78, `CSI 7;31m` 0x10, `CSI 44;8m` 0x44.
; =============================================================================
te_f_sgr:
    push bx
    push cx
    mov cl, [te_pn]
    or cl, cl
    jnz .loop
    xor al, al                      ; **`CSI m` WITH NO PARAMETER IS `CSI 0 m`**:
    call te_sgr1                    ; missing is zero, and zero is what SGR
    jmp short .done                 ; means by it
.loop:
    xor bx, bx
.each:
    mov al, [te_prm + bx]
    push bx
    push cx
    call te_sgr1                    ; an unknown code is ignored and the REST
    pop cx                          ; are still applied: `0;1;44;31` must not
    pop bx                          ; lose the 31
    inc bx
    dec cl
    jnz .each
.done:
    call te_derive
    pop cx
    pop bx
    ret

te_sgr1:                            ; AL = one SGR code
    push bx
    or al, al
    jz .reset
    cmp al, 1
    je .bold
    cmp al, 4
    je .out                         ; underline: consumed, and it reaches the
    cmp al, 5                       ; glass NOWHERE. A cell is two bytes and
    je .blink                       ; has nowhere to put a third bit; a third
    cmp al, 7                       ; byte per cell is 2,000 bytes of package
    je .rev                         ; bss for something no board sends
    cmp al, 8
    je .con
    cmp al, 22
    je .nobold
    cmp al, 24
    je .out                         ; underline off: the same nothing
    cmp al, 25
    je .noblink
    cmp al, 27
    je .norev
    cmp al, 28
    je .nocon
    cmp al, 30
    jb .out
    cmp al, 37
    jbe .fg
    cmp al, 39
    je .fgdef
    cmp al, 40
    jb .out
    cmp al, 47
    jbe .bg
    cmp al, 49
    je .bgdef
    cmp al, 90
    jb .out
    cmp al, 97
    jbe .fgbright
    cmp al, 100
    jb .out
    cmp al, 107
    jbe .bgbright
.out:
    pop bx
    ret
.reset:
    mov byte [te_lfg], 0x07
    mov byte [te_lbg], 0x00
    mov byte [te_blk], 0
    mov byte [te_rev], 0
    mov byte [te_con], 0
    jmp short .out
.bold:
    or byte [te_lfg], 0x08
    jmp short .out
.nobold:
    and byte [te_lfg], 0xF7
    jmp short .out
.blink:
    mov byte [te_blk], 1
    jmp short .out
.noblink:
    mov byte [te_blk], 0
    jmp short .out
.rev:
    mov byte [te_rev], 1
    jmp short .out
.norev:
    mov byte [te_rev], 0
    jmp short .out
.con:
    mov byte [te_con], 1
    jmp short .out
.nocon:
    mov byte [te_con], 0
    jmp short .out
.fg:                                ; 30-37: **the intensity SURVIVES a colour
    sub al, 30                      ; change**
    mov bl, [te_lfg]
    and bl, 0x08
    or al, bl
    mov [te_lfg], al
    jmp short .out
.fgdef:                             ; 39: the WHOLE nibble, so intensity goes
    mov byte [te_lfg], 0x07
    jmp short .out
.bg:
    sub al, 40
    mov [te_lbg], al
    jmp short .out
.bgdef:
    mov byte [te_lbg], 0
    jmp short .out
.fgbright:
    sub al, 90
    or al, 0x08
    mov [te_lfg], al
    jmp .out
.bgbright:                          ; 100-107 SET THE BLINK BIT and that is not
    sub al, 100                     ; a bug: the attribute has one bit there,
    mov [te_lbg], al                ; which blinks under hardware blink and is a
    mov byte [te_blk], 1            ; bright background under iCE. Its
    jmp .out                        ; consequence is that `CSI 25m` also turns a
                                    ; bright background off

te_derive:
    push ax
    push bx
    push cx
    mov al, [te_lfg]
    and al, 0x0F                    ; AL = fg
    mov bl, [te_lbg]
    and bl, 0x07                    ; BL = bg
    cmp byte [te_rev], 0
    je .noswap
    mov ah, al                      ; the swap, with the INTENSITY staying on
    and al, 0x08                    ; the foreground
    or al, bl
    mov bl, ah
    and bl, 0x07
.noswap:
    cmp byte [te_con], 0
    je .nocon
    mov al, bl                      ; concealed: fg := bg
.nocon:
    mov cl, 4
    shl bl, cl
    and al, 0x0F
    or al, bl
    cmp byte [te_blk], 0
    je .set
    or al, 0x80
.set:
    mov [con_attr], al
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE BUFFER OPERATIONS
;
; Every one of them marks the rows it touched. The bitmap is 70.8.1's and both
; renderers walk it, so a fill that forgot to mark is a fill nothing draws.
; =============================================================================

; --- te_here - AX = the cursor's CELL index (not its byte offset) -----------
te_here:
    push dx
    mov ax, CON_COLS
    mul word [con_cy]                ; **MUL WRITES DX** (SPEC.md 1)
    add ax, [con_cx]
    pop dx
    ret

; --- te_rowbase - AX = the cursor row's first CELL --------------------------
te_rowbase:
    push dx
    mov ax, CON_COLS
    mul word [con_cy]
    pop dx
    ret
te_insline:
    push ax
    push bx
    push cx
    push dx
    push si
    or ax, ax
    jz .out
    mov bx, CON_COLS
    mul bx                          ; AX = n*80, the cells the gap opens
    mov si, ax                      ; SI = the gap
    call te_rowbase
    mov bx, ax                      ; BX = the first cell of row cy
    mov cx, CON_CELLS
    sub cx, bx                      ; CX = cells from there to the bottom
    cmp si, cx
    jae .whole                      ; the gap covers the rest of the screen
    sub cx, si                      ; CX = the cells that survive, moved down
    mov ax, bx
    mov dx, bx
    add dx, si
    call con_cmove
    mov cx, si
.whole:
    mov ax, bx
    call con_fillcells               ; CX is the gap, or the whole tail
    mov bx, [con_cy]
.mk:
    call con_mark                    ; every row the edit MOVED (SPEC.md 70.9.3)
    inc bx
    cmp bx, CON_ROWS
    jb .mk
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

te_delline:
    push ax
    push bx
    push cx
    push dx
    push si
    or ax, ax
    jz .out
    mov bx, CON_COLS
    mul bx
    mov si, ax                      ; SI = the cells the delete closes
    call te_rowbase
    mov bx, ax
    mov cx, CON_CELLS
    sub cx, bx
    cmp si, cx
    jae .whole
    sub cx, si
    mov ax, bx
    add ax, si
    mov dx, bx
    call con_cmove
    mov ax, CON_CELLS
    sub ax, si                      ; ...and the blanks arrive at the BOTTOM
    mov cx, si
    call con_fillcells
    jmp short .mark
.whole:
    mov ax, bx
    call con_fillcells
.mark:
    mov bx, [con_cy]
.mk:
    call con_mark
    inc bx
    cmp bx, CON_ROWS
    jb .mk
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; te_inschar / te_delchar - AX = cells, at [con_cx] in the cursor's row
;
; The same two shapes one region smaller: the region is the cursor's row from
; [con_cx] to column 79 rather than the screen from [con_cy] to row 24, and
; con_fillcells marks the one row that changed.
; -----------------------------------------------------------------------------
te_inschar:
    push ax
    push bx
    push cx
    push dx
    push si
    or ax, ax
    jz .out
    mov si, ax                      ; SI = the gap
    call te_here
    mov bx, ax                      ; BX = lo, the first cell it opens
    mov cx, CON_COLS
    sub cx, [con_cx]                 ; CX = the cells from lo to the row's end
    cmp si, cx
    jae .whole
    sub cx, si                      ; CX = the cells that survive, moved right
    mov ax, bx
    mov dx, bx
    add dx, si
    call con_cmove
    mov cx, si
.whole:
    mov ax, bx
    call con_fillcells
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

te_delchar:
    push ax
    push bx
    push cx
    push dx
    push si
    or ax, ax
    jz .out
    mov si, ax
    call te_here
    mov bx, ax
    mov cx, CON_COLS
    sub cx, [con_cx]
    cmp si, cx
    jae .whole
    sub cx, si
    mov ax, bx
    add ax, si
    mov dx, bx
    call con_cmove
    call te_rowbase
    add ax, CON_COLS
    sub ax, si                      ; the blanks arrive at the RIGHT
    mov cx, si
    call con_fillcells
    jmp short .out
.whole:
    mov ax, bx
    call con_fillcells
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE ZMODEM HANDOVER (SPEC.md 70.9.6 -> SPEC.md 70.11)
;
; The receiver is `tz_start` in tezm.inc and this file's only business with it
; is the six-byte trigger above. SPEC.md 70.9.6's contract is what the two
; share: the handover offset is just past the final `0`, and **the parser is
; fed nothing more until the transfer ends** - te_byte sends every later
; application byte to tz_byte instead, so ansisim's `feed()` returning short at
; the same offset leaves both readers stopped with the same 4,000 bytes
; (SPEC.md 70.12).
;
; A STUB LIVED HERE THROUGH WAVE 3 and its whole behaviour was "fed nothing
; more": te_feed stopped and te_step issued no new NETV_RECV while [te_zon] was
; set. The receiver kept the second half of that - it is still the RECEIVER
; that decides whether another byte may be taken (tz_canrx), and TCP's own
; window is still what holds the sender.
; =============================================================================
