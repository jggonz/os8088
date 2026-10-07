; =============================================================================
; font.inc - 8x8 text for mode 12h (SPEC.md 6)
;
; No font bytes are hard-coded unless the BUILD asks for some: font_init asks
; the VGA BIOS for its ROM 8x8 character set (int 10h AX=1130h BH=03h) and
; REMEMBERS WHERE IT IS - [font_seg]:[font_base], glyph 32 - and every
; renderer reads the ROM's own rows through it (SPEC.md 6). There is no RAM
; copy: the 768 bytes of .lowbss the table and its blank cell were are not
; resident on any machine. The ROM's SPACE (glyph 32) is the blank cell an
; unprintable character draws. `make FONT=<name>` replaces the probe with a
; typeface carried in the boot overlay and copied into .lowbss after all
; (SPEC.md 6.2, ovl_font_init at the foot of this file) - the pointer then
; names that copy, and nothing downstream can tell the difference.
;
; Drawing uses Set/Reset with the Bit
; Mask so the background stays untouched (transparent): the glyph row is
; widened to a 16-bit window and shifted right by x&7, giving the masks for
; the one or two VRAM bytes the row lands on. A read of each byte loads the
; latches, so unmasked pixels are written back unchanged.
; =============================================================================

FONT_FIRST  equ 32              ; first glyph kept from the ROM set
FONT_LAST   equ 126             ; last  glyph kept
FONT_BYTES  equ (FONT_LAST - FONT_FIRST + 1) * 8    ; 95 glyphs x 8 rows = 760
%ifdef KERN_BIG
 %ifndef NOSEAMCUT
  %define SEAMCUT               ; SPEC.md 39.14.11 is on. ONE symbol for the
 %endif                         ; five sites below, because a knob spelled out
%endif                          ; five times is a knob spelled wrong once
%ifdef SEAMCUT
FONT_SEAM_A equ FONT_LAST + 2   ; SPEC.md 39.14.11: the two codes that address
FONT_SEAM_B equ FONT_LAST + 3   ; font_seam_a/font_seam_b through the glyph
                                ; arithmetic already in font_char's body,
                                ; with [font_base] pointed at them for the one
                                ; draw (font_ch_seam). Private to font_ch_cut,
                                ; which is what [font_ch_on] is for; nothing
                                ; else may pass them
%endif
%ifdef BAKED_FONT
 %macro FONT_BLANK 1            ; the blank cell an unprintable character
    mov %1, font_zero           ; draws: the copy's own, beside it in LOW_SEG
 %endmacro
%else
 %macro FONT_BLANK 1            ; ...and on the ROM's table its SPACE, which
    mov %1, [font_base]         ; is glyph FONT_FIRST and eight zero rows on
 %endmacro                      ; every set the probe can find (SPEC.md 6)
%endif

; SPEC.md 6.1.7: the widest run any adapter can show, in cells. A run stops at
; [vid_cwm8], so this is vid_w / 8 for the widest screen os8088 drives -
; Hercules at 720 - and the run-major scan can therefore never overflow the
; table. It has a fallback for it anyway, because a constant that is a bound
; on another module's geometry is one somebody will change.
FONT_RN_MAX equ 90

; SPEC.md 6.1.8: the shortest trailing span worth a `rep stosb`. The tail block
; is 17 bytes against a plain cell's 14, so it breaks even at 2 and wins from
; 3; below that the run takes the plain walk and pays nothing at all.
FONT_RN_KMIN equ 3

SNAP_ENT     equ 8              ; bytes per caller-log entry: x, y, char, ret
SNAP_NCALL   equ 24             ; SPEC.md 11.94.2: off-grid callers logged per
                                ; reset. A repaint draws hundreds of glyphs and
                                ; the first two dozen name every routine in it

; -----------------------------------------------------------------------------
; SPEC.md 11.94.1's INSTRUMENT: `make SNAPAUDIT=1` histograms the x & 7 of
; every glyph the machine draws, so "which app does not align its text" is a
; number read out of a running machine rather than a guess made by reading
; layout constants. It only means anything now that alignment is the DEFAULT
; (11.94): with every window's content origin on a multiple of 8, a non-zero
; bucket is the APP's own offset and nothing else.
;
; Two histograms because the two callers answer different questions - a
; font_run is a whole line and a font_char is one cell, so counting them
; together would let one app's 40-cell string outvote another's 40 strings.
;
; A plain build emits NOT ONE BYTE of this: the macro is empty and the
; counters are inside the same %ifdef. It preserves the flags because
; font_char's own contract does and its callers are inner loops.
; -----------------------------------------------------------------------------
%ifdef SNAPAUDIT
%macro SNAPAUDIT_TALLY 2        ; %1 = the audited window's table, %2 = the other
    pushf
    push ax
    push bx
    mov ax, [cs:snap_cur]       ; which window's callback are we inside?
    or ax, ax
    jz %%other                  ; none: kernel chrome, menus, the desktop
    mov bx, [cs:snap_win]
    or bx, bx
    jz %%mine                   ; no filter set: every callback counts
    cmp bx, ax
    jne %%other                 ; a DIFFERENT window - a repaint pass calls
%%mine:                         ; several, and only one is being asked about
    mov bx, %1
    jmp short %%have
%%other:
    mov bx, %2
%%have:
    mov ax, cx                  ; CX = the pen x, both callers
    and ax, 7
    shl ax, 1
    add bx, ax
    inc word [cs:bx]            ; cs: - .text, and CS is KERNEL_SEG here

    ; --- WHO DREW IT: the off-grid caller names itself (SPEC.md 11.94.2) -----
    ; A bucket count says an app is off-grid and not WHERE, and reading layout
    ; constants to find out is what left Piano, Arkanoid and Missile unresolved.
    ; So an off-grid glyph inside the AUDITED window records the pen and its
    ; caller's return address, and tools/os88snap.py maps that back to a symbol
    ; through os88sym. First SNAP_NCALL of them, because a repaint draws
    ; hundreds and the first few name the routine just as well.
    or ax, ax
    jz %%done                   ; bucket 0: aligned, nothing to report
    cmp bx, %1 + 16             ; the audited window's table? (its 8 words end
    jae %%done                  ; there) - the chrome's callers are not the ask
    mov bx, [cs:snap_ncall]
    cmp bx, SNAP_NCALL * SNAP_ENT
    jae %%done
    add word [cs:snap_ncall], SNAP_ENT
    mov bx, sp                  ; the macro pushed flags, ax, bx - so the frame
                                ; from SP is [+0] bx, [+2] ax, [+4] flags,
                                ; [+6] the caller's return address. ss: because
                                ; SS is not DS in this kernel
    mov ax, [ss:bx + 2]         ; the caller's AX: AL is the CHARACTER, and
    mov [cs:snap_tmpc], ax      ; reading [+0] instead gets the saved BX, which
                                ; is constant inside font_str_x's loop and
                                ; therefore looks exactly like a real answer
    mov ax, [ss:bx + 6]
    mov [cs:snap_tmpr], ax
    mov bx, [cs:snap_ncall]     ; ...and the slot this entry goes in
    sub bx, SNAP_ENT
    mov ax, cx
    mov [ss:bx + snap_calls], ax        ; pen x
    mov ax, dx
    mov [ss:bx + snap_calls + 2], ax    ; pen y
    mov ax, [cs:snap_tmpc]
    mov ah, 0
    mov [ss:bx + snap_calls + 4], ax    ; the character
    mov ax, [cs:snap_tmpr]
    mov [ss:bx + snap_calls + 6], ax    ; who called
%%done:
    pop bx
    pop ax
    popf
%endmacro
%else
%macro SNAPAUDIT_TALLY 2
%endmacro
%endif

%ifndef BAKED_FONT
; -----------------------------------------------------------------------------
; FONT_PICK - which ROM 8x8 table to read, TIMED (SPEC.md 6.0.1)
;
; in:       %1 = the label prefix of a FONT_TIMERS expansion in reach
; out:      DX:BP = glyph FONT_FIRST of the ROM table to read - the planar
;           one unless the BIOS's answer is an eighth faster -
;           BX = its PIT count, CX = RAM's for the same read; the three
;           counts are also left in the BDA's intra-application area at
;           0040:00F8 behind the word 'FP' (FONT_ICA), for tests/romfont
;           and tests/fontpick to read - the kernel keeps none of them
; clobbers: AX, SI, DI, ES, flags. DS = KERNEL_SEG untouched
;
; A MACRO because its two callers can reach no common copy: font_init runs
; from the boot overlay, which is gone by the desktop, and hbm_wake runs from
; HIBER.DRV, which is not there at boot. One body assembled into each, so a
; resume asks the reading machine exactly what a fresh boot of it would.
;
; THE CANDIDATES. int 10h AX=1130h BH=3 names the table the adapter's BIOS
; uses - on an EGA or VGA the card's OPTION ROM, which on an 8-bit ISA card is
; read through the card's own bus cycle (an IBM 5150 with a PVGA1A is the
; case this was written for). F000:FA6E is the IBM PC's fixed address for the
; same set, and every PC-compatible BIOS keeps it there because the CGA
; graphics modes draw from it - but it is a candidate only when its 95 glyphs
; are BYTE-IDENTICAL to the answer, so the choice never changes what text
; looks like. A BIOS that answers nothing (pre-EGA) makes the planar table
; the only candidate: it is compared with itself and timed twice, 2 ms of a
; boot on a 5150, which is cheaper in bytes than a branch around it.
;
; THE CLOCK. PIT channel 0 latched, before and after a `rep lodsb` over the
; 760 bytes with IF clear. Whichever mode the channel is in - the ROM's mode
; 3 counts by two, the kernel's mode 2 by one (SPEC.md 8.1) - all three
; readings are taken in it, so the comparison is a RATIO and the unit
; cancels; the down-count's modular difference is right across a reload.
; -----------------------------------------------------------------------------
FONT_PLANAR equ 0xFA6E + FONT_FIRST * 8    ; F000:FB6E, the planar glyph 32
FONT_ICA    equ 0x00F8                      ; 0040:00F8: 'FP', best, RAM, BIOS.
                                            ; kern_dos's mailbox is F0..F7
                                            ; (KDB_OFF), so the two halves of
                                            ; the 16 bytes never meet

%macro FONT_TIMERS 1
; %1_time - AX:SI = a table; out AX = PIT counts to read FONT_BYTES of it.
; Clobbers CX, DX, SI and the flags.
%1_time:
    push ds
    mov ds, ax
    pushf
    cli                         ; nothing may land inside the measurement
    call %1_pit
    xchg dx, ax
    mov cx, FONT_BYTES
    rep lodsb
    call %1_pit
    popf
    pop ds
    sub dx, ax                  ; a DOWN-count: start - end, modular
    xchg ax, dx
    ret
; %1_pit - AX = channel 0's count, latched. Clobbers nothing else.
%1_pit:
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    ret
%endmacro

%macro FONT_PICK 1
    xor ax, ax                  ; zero the answer first: on a pre-EGA BIOS
    mov es, ax                  ; AH=11h is not implemented and returns with
    xor bp, bp                  ; ES:BP untouched
    mov ax, 0x1130              ; get font information
    mov bh, 0x03                ; pointer wanted: ROM 8x8 character set
    int 0x10                    ; -> ES:BP = font table
    mov dx, es
    add bp, FONT_FIRST * 8
    or dx, dx                   ; a table is never in segment 0: no answer
    jnz %%timea
    mov dx, 0xF000              ; no answer: the planar ROM's own set
    mov bp, FONT_PLANAR
%%timea:
    mov ax, dx                  ; BX = the answer's count
    mov si, bp
    push dx
    call %1_time
    pop dx
    xchg bx, ax
    push bx                     ; ...kept for the ICA
    push ds                     ; is the planar set the SAME glyphs?
    mov ds, dx
    mov si, bp
    mov ax, 0xF000
    mov es, ax
    mov di, FONT_PLANAR
    mov cx, FONT_BYTES
    repe cmpsb
    pop ds
    jne %%ram                   ; a different face: the card's, the only one
    push dx
    mov ax, es                  ; AX = 0xF000
    mov si, FONT_PLANAR
    call %1_time
    pop dx
    mov si, bx                  ; the planar ROM unless the answer beats it
    shr si, 1                   ; by an EIGHTH: a tie goes to the system
    shr si, 1                   ; board, which is zero-wait on every PC, and a
    shr si, 1                   ; margin is what keeps two boots of one
    add si, bx                  ; machine from picking two tables on noise
    cmp ax, si
    ja %%ram
    xchg bx, ax                 ; take the planar ROM
    mov dx, 0xF000
    mov bp, FONT_PLANAR
%%ram:
    push dx
    mov ax, ds                  ; ...and RAM: the kernel's own first bytes
    xor si, si
    call %1_time
    pop dx
    xchg cx, ax                 ; CX = RAM
    mov ax, 0x0040
    mov es, ax
    mov di, FONT_ICA
    mov ax, 'FP'
    stosw
    xchg ax, bx
    stosw                       ; best
    xchg ax, bx
    xchg ax, cx
    stosw                       ; RAM
    xchg ax, cx
    pop ax
    stosw                       ; the BIOS answer's own
%endmacro

; -----------------------------------------------------------------------------
; font_init - pick the ROM 8x8 table and point [font_seg]:[font_base] at it
;
; in:       nothing (must run after vid_init and mem_init; uses int 10h)
; out:      nothing
; clobbers: nothing
;
; NOT ASSEMBLED under BAKED_FONT (SPEC.md 6.2), where ovl_font_init at the
; foot of this file does the same job out of the boot overlay. The two are
; alternatives rather than a fallback pair on purpose: a probe kept behind a
; baked table would be dead code on every machine that ever runs it, and a
; baked table kept behind a probe would never be the thing being tested.
;
; IN THE BLOB HALF OF THE BOOT OVERLAY (`.ovl`) ON BOTH KERNELS, reached by
; BLOBCALL: SPEC.md 6.0.1 grew the blob to ten sectors for exactly this body,
; so it costs no resident byte and no window byte. One caller, kmain.
;
; THE TABLE IS READ IN PLACE (SPEC.md 6) - unless even the fastest candidate
; FONT_PICK timed is slower than RAM by more than a quarter. Then the 760
; bytes go into a 1KB heap claim, MEM_K_FONT, and the renderers read that:
; what the 768 resident bytes used to buy, paid only by the machine that
; measured the need and billed to the arena rather than to every kernel. It
; is claimed TOP-DOWN and PINNED, before any driver, module or region
; exists, so it takes the arena's ceiling and everything later stacks under
; it - never a barrier to either compaction pass. A refused claim keeps the
; ROM pointer: slower is not wrong.
;
; WHY A QUARTER: a glyph byte is one read in a cell writer that makes two or
; more framebuffer accesses per row, so a table 25% slower than RAM is under
; ~10% of a cell - the most a copy could buy back for its kilobyte - and the
; 8088's own jitter between two passes of the same loop is under 1%
; (tests/romfont). A machine that crosses it, an unshadowed 8-bit option ROM
; on an AT bus, crosses it by multiples. `make FONTSLOW=1` takes the copy on
; any machine, so the copy path has a row.
; -----------------------------------------------------------------------------
section .ovl
font_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    FONT_PICK fi                ; DX:BP = the table, BX = its count, CX = RAM's
%ifndef FONTSLOW
    mov ax, cx
    shr ax, 1
    shr ax, 1
    add ax, cx                  ; RAM x 1.25
    cmp bx, ax
    jbe .point                  ; within a quarter of RAM: read in place
%endif
    mov si, dx                  ; slower than that (or FONTSLOW): copy it
    push bp
    mov ax, 1                   ; 1KB, the claim granule
    mov bx, MEM_K_FONT
    mov bp, mem_claim_hi_x      ; TOP-DOWN, pinned (see above)
    call COLD_SEG:wm_cbd        ; DX = the block; clobbers nothing else
    pop bp
    jc .refused
    push ds
    mov es, dx
    mov ds, si
    mov si, bp
    xor di, di
    mov cx, FONT_BYTES / 2
    rep movsw
    pop ds
    xor bp, bp                  ; the copy's glyph FONT_FIRST is its byte 0
    jmp short .point
.refused:
    mov dx, si                  ; no room: the ROM, where it is
.point:
    mov [font_base], bp         ; glyph FONT_FIRST, a SPACE, is where the table
    mov [font_seg], dx          ; starts and what an unprintable one draws.
                                ; (39.14.11's seam cells need no clearing:
                                ; font_ch_cut writes all eight rows of each)
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf                        ; FAR: kmain reaches this through BLOBCALL
    FONT_TIMERS fi
section .text
%endif

%ifdef SEAMCUT
; -----------------------------------------------------------------------------
; SPEC.md 39.14.11's two doors into font_char, and they are ABOVE it rather
; than below because an 8086 has no near conditional jump and font_char's own
; body is longer than a short one reaches. Both are cold: no cell that fits its
; display and no machine with one card ever arrives at either.
; -----------------------------------------------------------------------------
font_ch_hi:                     ; AL is past FONT_LAST - normally nothing to
    cmp byte [font_ch_on], 0    ; draw, and still nothing unless the cut is the
    je font_ch_drop             ; caller. The counter is what keeps the two
                                ; codes PRIVATE, so OSAPI_FONT_CHAR with 128
                                ; draws what it has always drawn
    cmp al, FONT_SEAM_A         ; the part that IS here, masked in both axes -
    je font_char.edgeok         ; so the edge tests have nothing left to say
    cmp al, FONT_SEAM_B         ; and are stepped over; the spill is tested
    je font_char.chok
    jmp short font_ch_drop      ; any other code past FONT_LAST: nothing

font_ch_spill:                  ; the cell runs off this display's edge
    call font_ch_cut
    jc font_ch_drop             ; refused: 39.14.2's whole-cell drop stands
    mov al, FONT_SEAM_A         ; ...and what is left is the part that IS here,
    call font_ch_seam           ; drawn by a font_char of its own at the same
    jmp short font_ch_drop      ; pen, NESTED on this display (no second
                                ; translation), and then this cell is done

; -----------------------------------------------------------------------------
; font_ch_seam - draw 39.14.11's half-cell AL (FONT_SEAM_A or _B) at CX, DX
;
; The seam cells are RAM - font_ch_cut composes them - and the glyph table is
; the ROM's (SPEC.md 6), so the draw points the table at the cells for the
; one call: [font_seg] = LOW_SEG and [font_base] biased so that font_char's
; own `(al - FONT_FIRST) * 8 + [font_base]` lands on font_seam_a/_b. Nothing
; in any glyph's path tests for the seam; the two words are simply what the
; renderers already read.
; -----------------------------------------------------------------------------
font_ch_seam:
    push word [font_seg]
    push word [font_base]
    mov [font_seg], ss
    mov word [font_base], font_seam_a - (FONT_SEAM_A - FONT_FIRST) * 8
    inc byte [font_ch_on]       ; the two codes are private to the cut
    call font_char
    dec byte [font_ch_on]
    pop word [font_base]
    pop word [font_seg]
    ret
%endif

; -----------------------------------------------------------------------------
; font_ch_drop - the one `jmp font_char.done` that six refusals share.
;
; An 8086 has NO NEAR CONDITIONAL JUMP, so NASM spells one that cannot reach as
; `Jcc' $+5 / jmp near` - five bytes instead of two - and on a SEAMCUT kernel
; font_char's `.done` is about 250 bytes from its entry with six refusals
; aiming at it. One trampoline inside short reach of all six is -15.
;
; **AND ONLY ON THAT KERNEL.** The finding this comes from says to put it
; outside the `%ifdef` so kern_small gets it too; MEASURED on this tree it is
; **+2 there**, because a kernel with no seam cut has no font_ch_hi, no
; font_ch_spill and no font_ch_cut, which leaves `.done` already inside short
; reach of all five of its refusals - so the trampoline would buy nothing and
; cost its own three bytes. The two refusals that are not already inside a
; SEAMCUT arm therefore carry a two-way spelling of their own below.
;
; An `equ` or a `%define` aliasing this name on the other arm would be tidier
; to read and is REFUSED: tools/stkbalance.py treats any line that names a
; label without branching to it as an address being taken, which promotes a
; continuation to a routine entered at depth 0 - and it then reports this
; label's `ret at depth -7`, which is font_char's epilogue seen from nowhere.
; Written this way the name appears only as a jump target and the walker sees
; what it is.
;
; Fragile in the way every trampoline is, and in the RIGHT way: it works
; because all six sites are within a short jump of this point, and a future
; edit that puts one out of reach is a BUILD ERROR rather than a silent bug.
; The refusals are cold by construction - an unprintable character, a cell off
; a screen edge, a cell the clip region cuts - and a cell that draws is not on
; this jump at all.
; -----------------------------------------------------------------------------
%ifdef SEAMCUT
font_ch_drop:
    jmp font_char.done
%endif

; -----------------------------------------------------------------------------
; font_char - draw one 8x8 glyph, transparent background
;
; in:       CX = x, DX = y (absolute screen px), AL = character,
;           color from [gfx_color]; caller holds the gfx lock
; out:      nothing
; clobbers: nothing
;
; Characters outside 32..126 draw nothing (the caller's pen still advances).
; Clipping is whole-char: if any part of the 8x8 cell would leave the screen
; the glyph is skipped entirely (acceptable v1 behavior per SPEC.md 6) - and,
; while a background task's clip region is armed, if any part of the cell
; would leave THAT (SPEC.md 11.3). A glyph is one shape or none; the region
; is the only thing here that could produce half of one, and half an 8x8
; glyph is unreadable anyway.
; -----------------------------------------------------------------------------
font_char:
    SNAPAUDIT_TALLY snap_hchar, snap_kchar
    call fnt_unlazy
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    GFXDENTERCD                 ; the cell goes WHOLE onto the display holding
                                ; its top-left corner (SPEC.md 39.14.2), and
                                ; the two edge tests below are that display's

    cmp al, FONT_FIRST          ; unprintable -> space -> nothing to draw
%ifdef SEAMCUT
    jb font_ch_drop
    cmp al, FONT_LAST
    ja font_ch_hi               ; ...or one of 39.14.11's two half-cells, the
.chok:                          ; only thing above FONT_LAST that draws
    cmp cx, [vid_cwm8]          ; unsigned: negative x fails this too
    ja font_ch_spill            ; SPEC.md 39.14.11: a SEAM is a cut, not a
    cmp dx, [vid_chm8]          ; drop - and the cut hangs off the branch that
    ja font_ch_spill            ; was already here, so a cell that fits pays
.edgeok:                        ; nothing for it on either kernel
%else                           ; ...and with no seam cut `.done` is already in
    jb .done                    ; short reach, so these five aim straight at it
    cmp al, FONT_LAST
    ja .done
    cmp cx, [vid_cwm8]          ; unsigned: negative x fails this too
    ja .done
    cmp dx, [vid_chm8]
    ja .done
%endif
    CLIPQ                        ; ...and against the clip region, which
    je .noclip                  ; answers in ROWS (SPEC.md 11.3.2): a cell an
    push ax                     ; edge crosses horizontally draws the rows on
    mov ax, cx                  ; our side of it rather than nothing at all,
    mov bx, dx                  ; which is what left a half-covered Timer
    add cx, 7                   ; frozen at the second it was covered.
    add dx, 7                   ; x2 and y2 are built IN PLACE: CX and DX
                                ; already hold x and y, so the `mov cx,ax` and
                                ; `mov dx,bx` that used to open this restored
                                ; two registers to what they were holding.
                                ; (The pair BELOW the call is a different
                                ; thing and stays - it undoes these adds.)
    call wm_clip_rows
    mov cx, ax                  ; MOV and POP leave CF alone
    mov dx, bx
    pop ax
%ifdef SEAMCUT
    jc font_ch_drop
%else
    jc .done
%endif
    jmp short .rows
.noclip:
    ; The whole cell, said in the one place the renderers below read it from -
    ; and said in ONE word store rather than two byte ones, because this is the
    ; path every unclipped glyph in the system takes and an 8088 pays 4.34
    ; clocks per instruction BYTE (PERFORMANCE.md Part 9), so 6 bytes against
    ; 12 is worth having. MEASURED on fontbench's PAIR rows it is worth less
    ; than the byte count suggests - 2.18% -> 2.04% - because what dominates is
    ; the four-instruction bias below, not these stores; the arithmetic said
    ; ~0.8% and the machine said 0.14%, which is why the figure quoted here is
    ; the machine's. The two bytes are adjacent by assertion, not by hope
    ; - see wm_clip_r0.
    mov word [wm_clip_r0], (8 << 8) | 0
%ifdef KERN_BIG
    mov byte [wm_clip_cm], 0xFF ; every column (SPEC.md 11.3.4)
%endif
.rows:

    sub al, FONT_FIRST          ; SI = glyph row data. The arithmetic stays in
    cbw                         ; AX and lands in SI with an `xchg`: `add
    shl ax, 1                   ; ax,imm16` is the 3-byte accumulator form
    shl ax, 1                   ; where `add si,imm16` is 4, and `xchg ax,si`
    shl ax, 1                   ; is 1 where `mov si,ax` is 2. AL is 0..94
    add ax, [font_base]         ; here - the two range tests above proved it -
    xchg ax, si                 ; so `cbw` lands AH = 0 in one byte. The
                                ; table is at [font_seg]:[font_base] - the
                                ; ROM's own (SPEC.md 6)
    mov al, [wm_clip_r0]        ; AL is spent (SI carries the glyph now, and
    cbw                         ; AX holds SI's old junk), so the row bias
    add si, ax                  ; costs no register: start at the glyph's row
                                ; r0 - [wm_clip_r0] is 0..7, so `cbw` again
    add dx, ax                  ; ...and at the screen row it belongs on


%ifdef GFX_VGA
    cmp byte [vid_mono], 0      ; 1bpp? the software renderer IS the direct
    je .vram                    ; path there (SPEC.md 39.5). Without a VGA in
%endif                          ; the build there is nothing to ask: the call
    call font_char_bb           ; below is the only cell writer, and `.done`
    push cs                     ; still owns the frame this pushed - with DS
    pop ds                      ; the kernel's again: font_char_bb leaves it
%ifdef GFX_VGA                  ; on the framebuffer. With no VGA in the build
    jmp .done                   ; `.done` is the NEXT instruction, and nasm
.vram:                          ; spends two bytes on `jmp short $+2`
    mov di, cx                  ; x/8 goes STRAIGHT into DI and x&7 stays in
    shr di, 1                   ; CL: the old spelling hopped x through BX and
    shr di, 1                   ; back (`mov bx,cx` ... `mov ax,bx` ...
    shr di, 1                   ; `mov cl,bl`) to free CX for a shift count it
    and cl, 7                   ; then took from CL anyway. Four `mov r16,r16`
                                ; fewer - 6 bytes and ~34.7 clocks by the
                                ; prefetch floor, better on both terms
    mov ax, ROW_BYTES
    mul dx                      ; AX = y * 80 (DX clobbered, product fits) -
    add di, ax                  ; DI = y * 80 + x / 8. CH is x's high byte
                                ; here and is loaded with [wm_clip_rn] three
                                ; instructions down, before anything reads it

    mov es, [font_seg]          ; ES = the glyph table's segment (SPEC.md 6)

    mov dx, 0x3CE               ; GC index port
    mov ah, [gfx_color]
    mov al, 0
    out dx, ax                  ; GC0 Set/Reset = color
    mov ax, 0x0F01
    out dx, ax                  ; GC1 enable Set/Reset on all planes

    mov ch, [wm_clip_rn]        ; row counter: the rows the region allows,
                                ; 8 whenever it is not cutting this cell
    mov ax, VGA_SEG             ; ...and DS = the FRAMEBUFFER for the loop, so
    mov ds, ax                  ; its four latch accesses carry no override:
                                ; nothing below reads a kernel word but the
                                ; column mask, through CS
.row:
    mov ah, [es:si]             ; glyph row in the high byte of the window
%ifdef KERN_BIG
    and ah, [cs:wm_clip_cm]     ; the region's column mask (SPEC.md 11.3.4) -
%endif                          ; KERN_BIG only, so the floor machine's glyph
                                ; loops are byte-identical (11.3.4.1)
    inc si
    xor al, al
    shr ax, cl                  ; AH = first-byte mask, AL = second-byte mask
    mov bx, ax                  ; keep both masks in BH/BL

    or bh, bh                   ; anything landing in the first byte?
    jz .no_first
    mov al, 8
    mov ah, bh
    out dx, ax                  ; GC8 Bit Mask = first-byte glyph bits
    xchg [di], al               ; load the latches and write back in ONE
                                ; instruction: an 8088's xchg reads the byte
                                ; and then writes it, which is the latch
                                ; load and the masked write in that order -
                                ; and the value written is don't-care, every
                                ; plane taking Set/Reset (GC1 = 0Fh above).
                                ; 2 bytes and 22 clocks against the mov
                                ; pair's 4 and 27
.no_first:
    or bl, bl                   ; spill into the second byte?
    jz .no_second
    mov al, 8
    mov ah, bl
    out dx, ax                  ; GC8 Bit Mask = second-byte glyph bits
    xchg [di+1], al             ; load latches and write, as above
.no_second:
    add di, ROW_BYTES
    dec ch
    jnz .row

    mov ax, 0x0001
    out dx, ax                  ; GC1 enable Set/Reset = 0 (default, SPEC 1.7)
    mov ax, 0xFF08
    out dx, ax                  ; GC8 Bit Mask = FF (default)
    push cs                     ; ...and DS the kernel's, for `.done`
    pop ds
%endif                          ; ...end of font_char's VGA cell writer
.done:
    GFXDLEAVEI
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef SEAMCUT
; -----------------------------------------------------------------------------
; font_ch_cut - SPEC.md 39.14.11: the one cell a display seam crosses, CUT
;               instead of dropped
;
; in:       CX = x, DX = y, LOCAL to the display font_char entered; AL = the
;           character; one of the two edge tests has just failed
; out:      CF = 0 - font_seam_a holds this display's part of the cell, and the
;           spill has ALREADY been drawn on the display across the seam with
;           this one put back current. The caller draws font_seam_a in place
;           and must NOT re-test the edges: nothing is left outside them, and
;           the coordinates that failed have not moved
;           CF = 1 - refused, and 39.14.2's whole-cell drop stands
; clobbers: nothing but the flags
;
; The seam is a multiple of 8 and the cell's top-left is left of it, so the
; cell's two byte-columns fall one wholly each side: the cut lands exactly
; where the renderers already split, and neither of them gains an instruction.
; What is cut is the GLYPH, into the two scratch cells beside the table, and
; each half is then drawn by an ORDINARY font_char at ordinary VIRTUAL
; coordinates - which resolves its own display, runs its own clip region and
; picks its own renderer, because 39.14.10's question is asked per display and
; the two cards need not be alike.
;
; **The spill is drawn FIRST and this display's part built after it**, and that
; order is not a preference: a spill can run off the far display's OTHER edge
; and cut again, and that cut writes font_seam_a. Building ours last is what
; makes the recursion harmless - and it is bounded at two, because a spill sits
; at coordinate 0 in the axis it crossed and only one axis carries a seam.
; -----------------------------------------------------------------------------
font_ch_cut:
    cmp byte [gfx_dnest], 1     ; 0 = one card, above 1 = a caller owns the
    je .live                    ; translation: 39.14.2 stands for both
.no:
    stc
    ret
.live:
    cmp cx, [vid_cw]            ; is the ANCHOR even on this display? 39.2.1's
    jae .no                     ; dead zone answers with the primary, and a
    cmp dx, [vid_ch]            ; cell there fails the same edge test with
    jae .no                     ; nothing across it - and would cut for ever
    test byte [vid_cw], 7       ; a width that is not a multiple of 8 would
    jnz .no                     ; leave our own half spilling into the next
                                ; row. 39.14.7.2 refuses an odd cut rather
                                ; than approximating it and so does this; every
                                ; adapter here is 640 or 720 wide, so what the
                                ; compare buys is the property, not the case
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov es, [font_seg]          ; ES:SI -> the source rows, wherever the table
    sub al, FONT_FIRST          ; is (SPEC.md 6) - and this is the same
    cbw                         ; arithmetic that will address the scratch
    shl ax, 1                   ; cell on the way back in. `cbw` AFTER the
    shl ax, 1                   ; subtract, and this is the ONE site with no
    shl ax, 1                   ; range test above it: AL can also be
    add ax, [font_base]         ; FONT_SEAM_A/_B (96/97), and 97-32 = 65, still
    xchg ax, si                 ; (ES is font_char's to clobber: it banked it)
                                ; positive. AX carries SI's old value out of
                                ; the xchg and is spent below - `mov al,1`
                                ; then vid_ctx_ptr, which takes its index in
                                ; AL and does `mul dl`, so AH is that
                                ; instruction's OUTPUT and not an input

    mov bx, [vid_cw]            ; BX = k, this display's columns of the cell,
    sub bx, cx                  ; and BP = n, its rows. Both are 1..8: the
    cmp bx, 8                   ; anchor is on the display and the cell is 8
    jbe .kok
    mov bx, 8
.kok:
    mov bp, [vid_ch]
    sub bp, dx
    cmp bp, 8
    jbe .nok
    mov bp, 8
.nok:

    mov al, 1                   ; which axis carries the SEAM (39.19.2)? The
    call vid_ctx_ptr            ; second display's VX names it and no mode byte
    mov ax, [di+VID_CTX_VX]     ; has to be read - gfx_blit4's cut asks exactly
    or ax, ax                   ; this. vid_ctx_ptr keeps AX, which is why the
    jz .below                   ; index goes in through AL

    cmp bx, 8                   ; --- RIGHT: the seam is a COLUMN
    jae .part                   ; ...which this cell does not reach: its
                                ; overhang is a screen edge with nothing beyond
                                ; it, and .part clips it to what is here
    mov di, font_seam_b         ; B[i] = src[i] << k - the columns past the
    mov cx, bx                  ; seam, lifted to the far display's first. All
    mov ch, 8                   ; eight ROWS go: it runs its own y test, and
.bxr:                           ; its height is its own
    mov al, [es:si]
    shl al, cl
    mov [ss:di], al
    inc si
    inc di
    dec ch
    jnz .bxr
    sub si, 8                   ; SI back to the source, for .part
    add dx, [vid_oy]            ; ...and the spill goes AT the seam, at the
    mov cx, [vid_ox]            ; cell's own y
    add cx, [vid_cw]
    jmp short .spill

.below:                         ; --- BELOW: the seam is a ROW
    cmp bp, 8
    jae .part
    add cx, [vid_ox]            ; the spill's x is the cell's own, banked here
    push cx                     ; because the loop below wants CX
    mov di, font_seam_b         ; B[i] = src[i+n] - the rows past the seam,
    add si, bp                  ; lifted to the far display's first. All eight
    mov cx, 8                   ; COLUMNS go: it runs its own x test
    sub cx, bp
.byr:
    mov al, [es:si]
    mov [ss:di], al
    inc si
    inc di
    loop .byr                   ; CX = 8 - bp, 1..7 (`jae .part` above proved
                                ; bp < 8). A CX of 0 would run 65,536 times -
                                ; and so would the `dec cx / jnz` this
                                ; replaces, to the iteration, so the hazard is
                                ; unchanged rather than introduced
    mov cx, bp                  ; ...and the rows the cell has not got there
    xor al, al                  ; are blank, which draws nothing at all,
.byz:                           ; text being transparent
    mov [ss:di], al
    inc di
    loop .byz                   ; CX = bp, 1..8
    sub si, 8
    pop cx
    mov dx, [vid_oy]
    add dx, [vid_ch]

.spill:
    mov al, [vid_cur]           ; 39.14.3 leaves the LAST display drawn on
    push ax                     ; current, so ours has to be named to come back
    dec byte [gfx_dnest]        ; the spill takes its own enter
    mov al, FONT_SEAM_B
    call font_ch_seam           ; (the two codes are private to this routine)
    inc byte [gfx_dnest]
    pop ax
    call vid_ctx_act            ; ...and this display is live again, which is
                                ; what the caller's fall-through rests on

.part:
    mov di, font_seam_a         ; A[i] = src[i] masked to the k columns and n
    mov cx, bx                  ; rows that are HERE, the rest zeroed. THE
    mov al, 0xFF                ; ZEROING IS THE WHOLE MASK: a blank row writes
    shr al, cl                  ; nothing, and a glyph with no ink past the
    not al                      ; seam leaves AL zero after the renderers' own
    mov ah, al                  ; shift - which their `or al, al / jz` skips
    mov cx, 8                   ; already. k = 8 shifts the byte away and `not`
.par:                           ; brings back FF, so no mask is no work
    mov al, [es:si]
    inc si
    and al, ah
    dec bp                      ; ONE loop over all eight rows: the first n
    jns .pkeep                  ; (BP = n, 1..8, counted down to -1) keep
    xor al, al                  ; their columns and the rest are zeroed - what
.pkeep:                         ; two loops and a `jz` around the second did.
    mov [ss:di], al             ; BP is spent; the ladder gives it back
    inc di
    loop .par
    clc                         ; POP leaves the flags alone, so the answer
    jmp kret_bp
%endif

; -----------------------------------------------------------------------------
; font_char_bb - the software renderer's glyph body (SPEC.md 32/39.5)
;
; The same shifted-row masks as the VRAM path, applied to all four planes
; in software: the [gfx_color] plane bit picks or (set bits) or and-not
; (clear bits). When x is byte-aligned the second-byte mask is always 0
; and that write is skipped, so nothing lands past the row's last byte.
; in:       CX = x, DX = y (cell fully on screen - font_char clipped),
;           SI -> 8 glyph row bytes
; clobbers: ax, bx, cx, dx, si, di, es (font_char's epilogue restores all)
;
; **This is the mono adapters' ONLY text renderer** (SPEC.md 39.5), so its
; row loop is the innermost loop of every string os8088 draws on a Hercules
; or CGA machine - and those are the slow machines. Three things it does that
; the straight port did not, all of them the VRAM path's own tricks arriving
; late:
;
; - **The ink test is hoisted.** [gfx_color]'s plane bit cannot change inside
;   a plane, so `test ch, 1` belongs above the eight rows rather than in them.
;   Two loops instead of one branch per row.
; - **A blank glyph row is skipped whole.** or-ing in 0 and and-ing in FF are
;   both identity, and a read-modify-write of framebuffer memory costs the
;   same on an 8088 whether or not it changes anything. MEASURED on a real
;   5150 (PERFORMANCE.md Part 9): **79.6 clocks per byte** on a Hercules and
;   81.0 on a CGA, for the whole five-instruction step. This comment used to
;   say ~30, which was low by 2.6x AND attributed to the wrong thing - only
;   about 7 of those clocks are the framebuffer, the rest is the 8088
;   executing the loop, and the same loop against plain RAM measures 72.8.
;   Most glyphs have a blank descender row and many a blank top row; a SPACE
;   is eight of them, and the Task Manager's columns are largely spaces.
; - **gfx_nextrow is inlined.** Its body is three instructions and the call
;   and ret around them cost as much again - about a third of a row's work
;   once the redundant writes above are gone. The CS overrides come with it:
;   see gfx_nextrow for why they are not optional.
; -----------------------------------------------------------------------------
font_char_bb:
    mov bx, cx                  ; BX = the byte column (x / 8), and BX is what
    shr bx, 1                   ; gfx_rowbase PRESERVES (viddet.inc, both its
    shr bx, 1                   ; arms). The parked value moves from DI - which
    shr bx, 1                   ; is this routine's own OUTPUT - to CX, which
    push cx                     ; it has to reload anyway, so one register hop
    mov ax, dx                  ; goes. Three single shifts cost 6 bytes to
    call gfx_rowbase            ; `mov cl,3 / shr ax,cl`'s 4 and win in place
    add ax, bx                  ; because four other moves go with them
    mov di, ax                  ; (~121.9 -> 100.9 clocks). DX is dead here.
    pop cx
    and cl, 7                   ; CL = right shift for the 16-bit window; CH is
                                ; x's high byte and is overwritten by
                                ; `mov ch,[gfx_color]` below before any read

    mov es, [font_seg]          ; ES = the glyph table (SPEC.md 6); DS becomes
                                ; the framebuffer below, once the last kernel
                                ; word has been read.
                                ; [vid_rseg] is THE framebuffer segment - not
                                ; a plane base (SPEC.md 39.3, 39.26).  There was a plane
                                ; LOOP here, walking [vid_planes] passes and
                                ; stepping [vid_rpara] between them, with SI
                                ; and DI parked in two .bss cells so each pass
                                ; could restart from them.  IT COULD ONLY EVER
                                ; RUN ONCE: the only entry to this routine is
                                ; font_char's, under `cmp byte [vid_mono],0 /
                                ; je .vram` on a VGA build and unconditional
                                ; on one without, so [vid_planes] is 1 here by
                                ; vid_depth_set - the fourth and last body of
                                ; SPEC.md 39.26's cluster.  Two stores and two
                                ; loads off EVERY GLYPH the mono adapters draw
    mov ch, [gfx_color]
    call font_ink               ; CH = colour bits; on mono, 00 or FF so
                                ; `test ch,1` still works
    mov bh, [wm_clip_rn]        ; row counter - see font_char's .rows
%ifdef KERN_BIG
    mov bl, [wm_clip_cm]        ; ...and BL, the plane counter it once was, is
%endif                          ; the region's column mask (SPEC.md 11.3.4), a
                                ; register where the rows read a memory word
    mov ds, [vid_rseg]          ; DS = THE framebuffer to font_char's restore:
                                ; the row loops' writes carry no override, and
                                ; the two kernel words left are read through CS
    test ch, 1                  ; this plane's ink, decided ONCE rather than
    jnz .srow                   ; once per row - and so is the disabled
    cmp byte [cs:font_dith], 0  ; dither, which is mono-only and therefore
    jne .dstart                 ; always on the clear-bits side
    jmp .crow
.srow:
    mov ah, [es:si]             ; glyph row in the window's high byte
%ifdef KERN_BIG
    and ah, bl                  ; the region's column mask (SPEC.md 11.3.4) -
%endif                          ; KERN_BIG only, so the floor machine's glyph
                                ; loops are byte-identical (11.3.4.1)
    inc si
    or ah, ah                   ; a blank row ors in nothing, whatever the
    jz .snext                   ; shift does to it
    xor al, al
    shr ax, cl                  ; AH = first-byte mask, AL = second-byte mask
    or [di], ah
    or al, al                   ; ...and nothing spilled into the second byte
    jz .snext                   ; at all when x is byte-aligned
    or [di+1], al
.snext:
    add di, [cs:vid_rowadd]     ; gfx_nextrow, inlined (see the note above)
    test di, [cs:vid_wrapbit]
    jz .snw
    add di, [cs:vid_wrapfix]
.snw:
    dec bh
    jnz .srow
    ret                         ; one plane, so the cell is finished
.crow:
    mov ah, [es:si]
%ifdef KERN_BIG
    and ah, bl                  ; the region's column mask (SPEC.md 11.3.4) -
%endif                          ; KERN_BIG only, so the floor machine's glyph
                                ; loops are byte-identical (11.3.4.1)
    inc si
    or ah, ah                   ; a blank row masks nothing out either
    jz .cnext
    xor al, al
    shr ax, cl
    not ah
    and [di], ah
    or al, al
    jz .cnext
    not al
    and [di+1], al
.cnext:
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .cnw
    add di, [cs:vid_wrapfix]
.cnw:
    dec bh
    jnz .crow
    ret

; --- disabled text, mono only (SPEC.md 47) -----------------------------------
; .crow with the shifted glyph masked to a checkerboard. A separate loop
; rather than an `and ah, [font_dmask]` in .crow itself: that is the innermost
; loop of every string on the slowest machines we run on, and a memory-operand
; AND per byte per row is real money there for something almost no glyph wants.
; The phase is the CELL's row 0, so a whole line is in step - text rows are 8px
; apart, which is even, so lines are in step with each other too. It is seeded
; off [wm_clip_r0] rather than always AA because the first row DRAWN is not
; always the cell's first (SPEC.md 11.3.2): a clip edge cutting a cell biases
; both the glyph pointer and the screen row, and an unbiased seed would flip
; the checkerboard's phase against the rows drawn either side of it.
.dstart:
    mov dl, 0xAA                ; DL is the checkerboard: DX is dead here (the
    test byte [cs:wm_clip_r0], 1 ; row base went through AX), so the mask
    jz .drow                    ; a .bss byte used to carry is a register
    mov dl, 0x55
.drow:
    mov ah, [es:si]
%ifdef KERN_BIG
    and ah, bl                  ; the region's column mask (SPEC.md 11.3.4) -
%endif                          ; KERN_BIG only, so the floor machine's glyph
                                ; loops are byte-identical (11.3.4.1)
    inc si
    or ah, ah                   ; a blank row masks nothing out...
    jz .dnext
    xor al, al
    shr ax, cl
    and ah, dl                  ; ...and half of what is left goes too
    and al, dl
    or ah, ah
    jz .dsecond                 ; the mask can empty a byte the glyph filled
    not ah
    and [di], ah
.dsecond:
    or al, al
    jz .dnext
    not al
    and [di+1], al
.dnext:
    not dl                      ; every row, blank ones included, or the
                                ; checkerboard turns into stripes
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .dnw
    add di, [cs:vid_wrapfix]
.dnw:
    dec bh
    jnz .drow
    ret                         ; ...and the third exit.  All three were
                                ; `jmp .pnext` into `add dx,[vid_rpara] /
                                ; shr ch,1 / dec bl / jnz .plane / ret`

; -----------------------------------------------------------------------------
; font_ink - internal: font_char_bb's per-plane colour register
;
; The glyph loop tests bit 0 of CH once per plane and shifts, so on VGA the
; colour index goes in untouched. On mono there is one plane and one bit to
; give it: gfx_ink's black or white. The middle greys round to black - a
; dithered 8x8 glyph costs half its strokes, and Piano's coloured letters
; want contrast, not texture.
;
; **[gfx_dis] IS THE ONE EXCEPTION** (SPEC.md 47): a disabled control's text
; is masked to a checkerboard instead - half the strokes, which is exactly
; what the 1bpp Macintosh did to a greyed-out menu item and exactly as
; readable. Without it a disabled STRING came out black, pixel-identical to a
; live one on Hercules and CGA, so a menu item that greyed out simply stopped
; saying so and MENU_DIS's whole visual signal was missing (SPEC.md 12.2).
;
; **It keys off the FLAG and not off CDGRAY**, which was the first attempt:
; the renderer cannot ask whether a string is a control, so every other use of
; that colour got caught too - Minesweeper's digit 8 (SPEC.md 23) came out
; dotted, looking like a menu item you cannot pick. A flag says what is meant
; rather than inferring it from what was picked, and gfx_pen_* keeps the two
; from disagreeing.
;
; in:       CH = [gfx_color], [gfx_dis]
; out:      CH = the colour index (VGA) or 00/FF (mono); [font_dith] set
; clobbers: flags
; -----------------------------------------------------------------------------
font_ink:
    push ax                     ; AT THE ENTRY, so AX is preserved on MORE
                                ; paths than before rather than fewer - the
                                ; VGA early-out below used to skip the pair.
                                ; **AND IT IS SLOWER, not neutral**, which is
                                ; worth writing down because the finding that
                                ; produced it said the opposite: the mono path
                                ; goes 54.9 -> 56.7 clocks (+1.8) and the VGA
                                ; early-out pays the push and the pop it did
                                ; not, +7.9. Against ~900 us for a glyph cell
                                ; that is 0.04%, and the 5 bytes are worth it;
                                ; a comment claiming a speed win that is a loss
                                ; is how the next pass inherits a wrong fact
    mov byte [font_dith], 0     ; cleared on EVERY path, VGA included: the
                                ; glyph loop reads it per plane and .bss is
                                ; not zeroed by -f bin
    mov al, [vid_mono]          ; MONO, OR THE WINDOW SAID SO (SPEC.md
    or al, [gfx_mono1]          ; 11.96.17.1) - two booleans, so an OR of the
    jz .out                     ; bytes is non-zero on exactly that pair of
                                ; conditions. A cache one plane deep cannot
                                ; carry a third
                                ; colour, and 47 rule 1's disabled pen is solid
                                ; CDGRAY on a VGA - which an application cannot
                                ; fix from outside, font_ink being the
                                ; kernel's. The path below already answers it:
                                ; it reduces the ink to 00h/FFh and stipples
                                ; the disabled case, which IS the claim
    mov al, ch
    cmp byte [gfx_dis], 0       ; DISABLED IS ASKED FIRST (SPEC.md 76.6). It
    je .ink                     ; used to be asked only after `cmp al, 0xFF`
    mov byte [font_dith], 1     ; sent a WHITE pen straight to .white, which
%ifndef DISINK0                 ; was sound while ink was black by definition
    mov al, [gfx_disink]        ; (`make DISINK0=1` is the A/B: the reduction
%endif                          ; below then sends every grey to black again)
                                ; - and under the Dark theme the chrome's ink
                                ; IS white, so a greyed caption came out
                                ; SOLID and pixel-identical to a live one:
                                ; SPEC.md 47 rule 1's own failure, arriving
                                ; through the renderer rather than a caller
                                ;
                                ; **AND THE STIPPLE IS APPLIED TO THE LIVE
                                ; INK'S DIRECTION, NOT TO THE GREY** (SPEC.md
                                ; 76.6.1). The reduction below sends anything
                                ; that is not 0xFF to BLACK, and every
                                ; disabled pen in the machine is a middle grey
                                ; - so the checkerboard was always laid on
                                ; black, which is a legible stipple on white
                                ; and NOTHING AT ALL on Dark's black chrome.
                                ; [gfx_disink] is what the pen that set the
                                ; flag says a greyed glyph stipples in, and it
                                ; is that pen's own live ink: black for the
                                ; content pens, [thm_inkc] for the chrome's
.ink:
    call gfx_ink                ; CH is untouched by it - still the index
    cmp al, 0xFF                ; only pure white lights a mono pixel
    je .white
    xor al, al
.white:
    mov ch, al
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_str - draw a NUL-terminated string left to right
;
; in:       CX = x, DX = y, SI -> ASCIIZ string, color from [gfx_color];
;           caller holds the gfx lock
; out:      nothing
; clobbers: nothing
; -----------------------------------------------------------------------------
font_str:
    push es                     ; the kernel's own strings live in DS; the
    push ds                     ; ES-taking body below is the one that draws
    pop es                      ; (SPEC.md 20.1: a package's are in its own
    call font_str_x             ; segment, and wm/menu pass that in ES)
    pop es
    retf

; -----------------------------------------------------------------------------
; font_str_x - font_str with the string at ES:SI
;
; in:       CX = x, DX = y, ES:SI -> ASCIIZ, [gfx_color] = pen
; out:      nothing
; clobbers: nothing else (flags)
;
; Since packages left the kernel segment (SPEC.md 20.1) a drawn string can
; belong to the kernel or to any loaded package, and the caller is the only
; one who knows which. Five call sites in the tree pass a package segment -
; the window title (wm_draw_win), the app name and menu titles
; (menu_draw_bar), and the item strings (menu_drop, menu_widest); every other
; caller reaches this through font_str above with ES = DS.
; -----------------------------------------------------------------------------
font_str_x:
    push ax
    push cx
    push si
.next:
    mov al, [es:si]
    inc si
    or al, al
    jz .done
    call font_char              ; out-of-range chars draw nothing; pen still
    add cx, 8                   ; advances, so they read as spaces
    jmp .next
.done:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; font_run - draw a string as OPAQUE cells: background and glyph in one pass
;
; in:       CX = x, DX = y, SI -> ASCIIZ (font_run_x: ES:SI), AL = text
;           colour, AH = background colour; caller holds the gfx lock
; out:      nothing
; clobbers: flags, and [gfx_color] - the pen is SPENT: it is left at the run's
;           foreground on a drawn run and at its background on one whose cells
;           were all refused by the clip region (font_run_scell sets it before
;           the test). A caller that draws afterwards sets its own pen
;
; **Every text-bearing element in this system erases a rect and then letters
; it**, and each of them writes those bytes twice: once as a fill and once,
; read-modify-write, as a glyph. This does it once. On a 1bpp adapter with a
; byte-aligned x an 8x8 cell row IS one framebuffer byte, so the whole
; operation is `mov [es:di], al` per row per cell - no shift, no read, no
; second byte, and no separate fill pass at all.
;
; **It also collapses the granularity rule** (SPEC.md 11.3) for its callers.
; A fill clips per pixel and a glyph per whole cell, so an erase-then-letter
; pair can disagree and leave a cell blank; here they are one decision about
; one cell, taken once. A caller using this cannot produce that failure.
;
; THREE prologues reach one row pass, and `.slow` is the narrow case now
; rather than the wide one. THIS BLOCK USED TO SAY "anything else: one gfx_fill
; of the run's rect and font_str over it" - it was written before SPEC.md
; 6.1.10 and 6.1.11 and was stale for both of them, and SPEC.md 5.4.2 carried
; the same sentence until DOT DELIRIUM (apps/dotdel) read it, believed VGA
; had no fast path, and hand-rolled a text renderer on the strength of it.
;   - `[vid_mono]` (both mono adapters), x byte-aligned: the per-plane loop
;     below - an 8x8 cell row IS one framebuffer byte, so it is one store.
;     On mono `[vid_mono]` is permanently 1 (SPEC.md 39.5).
;   - VGA (KERN_BIG): `.planar`, SPEC.md 6.1.10. The planes are grouped ONCE a
;     run - Set/Reset answers for the planes both colours agree on - so what
;     is left per cell row is the same `stosb`. It needs one colour's plane
;     bits to be a SUBSET of the other's, which 6.1.10.1 is the caller's rule
;     for: if either colour is CBLACK or CWHITE it always holds.
;   - unaligned (KERN_BIG): SPEC.md 6.1.11, on either prologue. A run of n
;     cells off the grid is n-1 whole stores plus TWO merges, not 4n accesses.
;   - `.slow`, and only then: one gfx_fill of the run's rect in the background
;     colour and font_str over it - the pair every caller wrote by hand before
;     6.1. On kern_big that is a colour pair sharing no plane in either
;     direction (SPEC.md 6.1.10's `.plno`); kern_small takes it for any planar
;     or unaligned run by budget decision.
; =============================================================================
; fnt_unlazy - a glyph under an ARMED clip region is confined to it by
; wm_clip_test (it draws a cell whole or not at all), so a cursor outside that
; region is provably safe. Unarmed, a glyph lands wherever it is told and the
; deferred hide is owed (SPEC.md 7.1.4).
fnt_unlazy:
    cmp word [wm_clip_n], 0
    jne .out
    CURUNLAZY
.out:
    ret

font_run:
    GFXAUDIT_HOOK
    push es                     ; the deferred hide is spent in font_run_x and
    push ds                     ; NOT here, which is SPEC.md 6.1.9: this entry
    pop es                      ; is the kernel's, and the API slot 0x01E5 is
    call font_run_x             ; an X STUB straight to font_run_x - so a hide
    pop es                      ; owed here was owed by kernel callers only
    ret

; SPEC.md 6.1.7: one cell of a row pass, and the row advance after it. Macros
; because the padded and unpadded row loops differ only in the tail, and two
; copies of a five-instruction body that must agree is how they stop agreeing.
%macro FONT_RN_PX 0
    cs lodsw                    ; AX = this cell's glyph base (CS:SI, the table:
                                ; DS is the glyph table's through the pass)
    xchg ax, bx                 ; BX = it, in one byte
    add bx, bp                  ; ...+ the row this pass is on
    mov al, [bx]                ; the glyph row byte (SPEC.md 6: DS = its segment)
    and al, dl
    xor al, dh
    stosb
%endmacro

; SPEC.md 6.1.12: the end of one row of a RUN - the row advance, and the
; checkerboard's per-row swap. [font_rn_dt] is 0 for every run that is not
; disabled text on a 1bpp adapter, so the XOR is unconditional and free of a
; branch. NOT folded into FONT_RN_NEXTROW: font_rn_edge's column passes use
; DL as a BIT MASK and share that macro.
%macro FONT_RN_ROWEND 0
    FONT_RN_NEXTROW
    xor dl, [cs:font_rn_dt]     ; CS: DS is the glyph table's in every pass
%endmacro

%macro FONT_RN_NEXTROW 0
    add di, [cs:vid_rowadd]     ; gfx_nextrow, ONCE a row
    test di, [cs:vid_wrapbit]
    jz %%nw
    add di, [cs:vid_wrapfix]
%%nw:
%endmacro

font_run_x:
    SNAPAUDIT_TALLY snap_hrun, snap_krun
    call fnt_unlazy             ; SPEC.md 6.1.9: HERE, not at font_run above,
                                ; because slot 0x01E5's X cell names THIS
                                ; label - so every package's opaque run drew
                                ; over the mouse pointer with gfx_lock's
                                ; promised hide unspent, and gfx_unlock then
                                ; took cur_lazyend's "the arrow never left the
                                ; screen" path and did not put it back
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                     ; SPEC.md 6.1.7's row pass holds the glyph row
                                ; in BP, and 7.1.4.1 is what a routine that
                                ; forgets to save it costs: a background task
                                ; keeps BP for its whole life
%ifdef KERN_BIG
    ; --- SPEC.md 39.14.6: A RUN IS NOT A SHAPE. font_char is whole-cell
    ; because a cell is 8x8 and goes wholly onto one display; a run is up to a
    ; window wide, and treating it the same way cuts a line of text dead at
    ; the seam. A straddled Note Pad showed exactly that in the field: typing
    ; landed on one card only, and because SPEC.md 27.2 makes the run's own
    ; padding the ERASE, backspace did not erase on the other.
    ;
    ; So a run that does not fit one display goes PER CELL, untranslated -
    ; each font_char then picks its own display, which is the machinery
    ; SPEC.md 6.1.2 already built for a clip edge crossing a run. It skips the
    ; enter entirely, so it must skip the leave as well: .cells_nx has its own
    ; tail rather than a flag for .out to test.
%ifndef NOSPLIT                 ; `make NOSPLIT=1` is the A/B: the same session
                                ; through the kernel that has no split, which
                                ; is the only way to show that the pixels the
                                ; second card gets are THIS change's doing
    cmp byte [vid_ndisp], 1
    jbe .nosplit
    cmp byte [gfx_dnest], 0
    jne .nosplit                ; an outer hook already chose a display
    push ax
    push bx
    push cx
    push dx
    call font_width_x           ; AX = 8 * length (CX/DX/SI preserved)
    or ax, ax                   ; empty: nothing to cross - and `or` leaves CF
    jz .spanok                  ; CLEAR, which is the answer .spanok reads
    add ax, cx
    dec ax
    xchg ax, cx                 ; AX = x1, CX = x2
    mov bx, dx                  ; BX = y1
    add dx, 7                   ; DX = y2
    call vid_span_one
.spanok:                        ; THE POPS ARE ABOVE THE BRANCH, so both arms
    pop dx                      ; share them: `pop` touches no flag on an 8086
    pop cx                      ; (only `popf` does), so CF survives them and
    pop bx                      ; the test below is still vid_span_one's. The
    pop ax                      ; empty-string early-out arrives here with CF
    jnc .nosplit                ; already clear from its own `or ax,ax`, which
    mov [font_rn_fg], al        ; is the answer that arm wants; and AX is
    mov [font_rn_bg], ah        ; restored before it is read, as it was
    jmp .cells_nx
.nosplit:
%endif
%endif
    GFXDENTERCD                 ; ...and a run with it - its .slow path is
                                ; gfx_fill + font_str, which the nesting count
                                ; keeps from translating a second time

    mov [font_rn_fg], al
    mov [font_rn_bg], ah
    mov byte [font_rn_dt], 0    ; SPEC.md 6.1.12: no checkerboard unless the
                                ; mono prologue below finds one. Set HERE and
                                ; not there, so the planar prologue and every
                                ; fallback get it without knowing it exists
%ifdef KERN_BIG
    mov byte [font_rn_pl], 0    ; SPEC.md 6.1.10: which prologue set the masks
%endif
%ifdef KERN_BIG
    mov ax, cx                  ; SPEC.md 6.1.11: the pen's phase, and the
    and al, 7                   ; whole of what UNALIGNED costs from here -
    mov [font_rn_k7], al        ; the prologue, the glyph table and the span
%ifdef NOUNAL                   ; are the run's either way
    jnz .slow                   ; `make NOUNAL=1` is the A/B: the same session
%endif                          ; through the kernel that has no 6.1.11
%else
    test cx, 7                  ; kern_small keeps the pair for an unaligned
    jnz .slq                    ; run, exactly as it keeps it for a PLANAR one
                                ; (6.1.10): the 128-256KB machine's budget is
                                ; the tighter of the two by design, and 6.1.11
                                ; is +262 bytes of .text
%endif
    cmp byte [vid_mono], 0
%ifdef KERN_BIG
    je .planar                  ; SPEC.md 6.1.10: mode 12h has a single-store
%else                           ; prologue of its own. kern_small does not
    je .slq                     ; carry it - gfx_blit1's precedent (5.4.2)
%endif
    ; SPEC.md 6.1.12: a DISABLED control's glyph is checkerboarded, and this
    ; path used to hand it to .slow for that reason. It does not any more -
    ; the mask FOLDS INTO the run's own, below, and costs the row loop one
    ; memory XOR a row rather than one of anything a cell.

    ; --- what belongs to the RUN, done once (SPEC.md 6.1.6) ------------------
    ; A run is ONE y, ONE pair of colours and a column that advances by one
    ; byte a cell. font_run_cell used to derive all of it per cell - calling
    ; gfx_rowbase, whose 16-bit MUL an 8088 charges ~120 clocks for, and
    ; font_ink TWICE - for answers that cannot change until the run ends.
    cmp dx, [vid_chm8]          ; the run's y is every cell's y, so the
    ja .outq                    ; vertical edge test is the run's too
    cmp cx, [vid_cwm8]          ; ...but x is per cell, and DI only tracks it
    jbe .xok                    ; by `inc di`. That lockstep is exact for a run
    jmp .cells                  ; that STARTS on screen - x then cannot reach
.outq:                          ; (the door's far exits, below the one jump
    jmp .out                    ; in short reach of them: a two-byte `jcc`
%ifndef KERN_BIG                ; with a 4-clock not-taken where the five-byte
.slq:                           ; ones cost 16 on every run)
    jmp .slow
%endif
.xok:                           ; 0x10000 in 80 cells - and it breaks on the
                                ; one run that can wrap: a NEGATIVE x, which
                                ; this unsigned test catches. Two cells later x
                                ; is 0 and DI is 0x2000 past where it belongs,
                                ; so that run goes per cell (SPEC.md 6.1.2's
                                ; path), which recomputes both and is the
                                ; documented same-pixels fallback
    push si                     ; (the string: .p1's frame, see .p1pre)
    push cx                     ; font_ink returns in CH and gfx_rowbase
    push dx                     ; clobbers CL and DX - between them that is
                                ; both of the coordinates
    mov bh, [gfx_dis]           ; BH = is this DISABLED text (SPEC.md 47)?
    mov byte [gfx_dis], 0       ; NEITHER font_ink call may act on it: the
                                ; BACKGROUND is never the disabled pen, and
                                ; with the flag live both calls answer with
                                ; [gfx_disink] and the run comes out solid
    mov ch, [font_rn_fg]
    or bh, bh
    jz .fgink
    mov ch, [gfx_disink]        ; SPEC.md 76.6.1: the stipple is laid on the
.fgink:                         ; LIVE ink's direction, which is what that pen
    call font_ink               ; records - black for the content pens,
    mov bl, ch                  ; [thm_inkc] for the chrome's
    mov ch, [font_rn_bg]
    call font_ink
    mov [gfx_dis], bh           ; ...and the flag back, for font_char below
    mov [font_rn_bm], ch        ; the byte a CLEAR glyph bit composes to...
    xor bl, ch                  ; ...and ink XOR background (SPEC.md 6.1.5)
    or bh, bh                   ; live text: DL is the same mask for all eight
    jz .nodith                  ; rows and [font_rn_dt] is already 0
    mov [font_rn_dt], bl        ; disabled: the two dither masks PARTITION the
    mov bh, 0xAA                ; run's mask, so XORing DL with the whole of it
                                ; swaps one for the other - one memory XOR a
                                ; ROW, and nothing at all per cell.
                                ;
                                ; 0xAA UNCONDITIONALLY, because font_char's
                                ; phase is GLYPH-relative and not screen-
                                ; relative: its seed is [wm_clip_r0] & 1, which
                                ; is 0 for an unclipped cell whatever y it is
                                ; on. Seeding off y's parity instead inverts
                                ; the checkerboard on half the screen rows,
                                ; which a pixel diff catches and an eye does
                                ; not
%ifdef KERN_BIG
    test byte [font_rn_k7], 1   ; ...but HORIZONTALLY the phase is screen-
    jz .dseed                   ; absolute: font_char masks the SHIFTED bytes
    mov bh, 0x55                ; and 6.1.11's row walk masks the cell before
%endif                          ; shifting it, so an odd skew takes the other
.dseed:                         ; mask to land in the same columns
    and bl, bh
.nodith:
    mov [font_rn_xm], bl        ; DL's value for the FIRST row
.rowb:                          ; THE ROW BASE IS A PROPERTY OF THE RUN, not of
                                ; the renderer (SPEC.md 6.1.6), so the planar
                                ; prologue jumps here instead of carrying its
                                ; own copy of these nine instructions. Both
                                ; arrive the same way: the stack holding si,
                                ; cx then dx - .p1's frame - so gfx_rowbase
                                ; may clobber any of them, and the coordinates
                                ; are READ off it rather than popped and
                                ; pushed back. (DF = 0 is the system contract,
                                ; SPEC.md 1 rule 3: no `cld` on either arm.)
    mov bp, sp
    mov ax, [bp]                ; y (BP addresses SS, which holds the frame)
    call gfx_rowbase            ; AX = rowbase(y), once for the whole run
    mov cx, [bp+2]              ; ...and x
    mov di, cx
    shr di, 1                   ; three single shifts rather than one by CL:
    shr di, 1                   ; 6 clocks against 20, and CL stays the x it
    shr di, 1                   ; still needs to be
    add di, ax                  ; DI = the run's first framebuffer byte

.p1pre:
    ; --- PASS 1: which cells get drawn, and where their glyphs live ----------
    ; SPEC.md 6.1.7. The string is walked ONCE into a table of glyph pointers,
    ; which is what frees ES - it holds the caller's string right up to here -
    ; so the drawing pass can point it at the framebuffer for the whole run.
    ; SI, CX and DX are on the stack (.xok and .planar push them), wanted
    ; again if this falls back to the per-cell path below
    mov dx, [font_base]         ; DX, the y, carries the table's
                                ; base through the walk: one register `add` a
                                ; cell where a memory operand would be twelve
                                ; clocks more (SPEC.md 6)
    mov bx, font_rn_tab
    xor bp, bp                  ; BP = cells that will actually be drawn
.p1:
    mov al, [es:si]
    or al, al
    jz .p1end
    cmp cx, [vid_cwm8]          ; the first cell off the right edge ends the
    ja .p1end                   ; run - x only grows, so the drawn cells are
    cmp bp, FONT_RN_MAX         ; a PREFIX, which is what lets stosb walk them
    jae .p1full
    inc si
    add cx, 8
    cmp al, FONT_FIRST
    jb .p1blank
    cmp al, FONT_LAST
    ja .p1blank
    sub al, FONT_FIRST
    cbw                         ; AL is 0..94: the two tests above proved it
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, dx
    jmp short .p1put
.p1blank:
%ifdef BAKED_FONT
    FONT_BLANK ax               ; an unprintable cell is still PAINTED
%else
    mov ax, dx                  ; an unprintable cell is still PAINTED: the
%endif                          ; ROM's SPACE, glyph FONT_FIRST
.p1put:
    mov [bx], ax
    inc bx                      ; 2 bytes to `add bx,2`'s 3 and the same 4
    inc bx                      ; clocks, so it is better on the fetch floor
                                ; (the canonical shape of that trade). `inc`
                                ; leaves CF where `add` sets it, and nothing
                                ; here reads CF
    inc bp
    jmp short .p1
.p1full:                        ; unreachable while the table covers the widest
    pop dx                      ; screen, and a fallback rather than a truncation
    pop cx                      ; if that ever stops being true
    pop si
%ifdef KERN_BIG
.escape:                        ; WHICH PER-CELL PATH, said once. It is a
    mov al, [font_rn_pl]        ; property of the RUN - planar (SPEC.md 6.1.10)
    or al, [font_rn_k7]         ; or off the byte grid (6.1.11) - and both
    jne .cells                  ; flags are set once, right after GFXDENTERCD,
                                ; so the answer does not depend on WHY the row
                                ; walk was left. The armed-clip arm 500 bytes
                                ; below jumps here instead of carrying its own
                                ; copy; SI (the string), CX (the pen x) and DI
                                ; (the run's first framebuffer byte) are live
                                ; and equal at both arrivals, and AX is dead at
                                ; both - .cell and .cells each reload AL from
                                ; [es:si]
%endif
.jcell:                         ; (kern_small's clipped-run `jc` rides this)
    jmp .cell                   ; NOT `jmp short`: it reaches by 12 bytes today
                                ; and this is the cold path of the pair
.p1end:
    or bp, bp
    jz .p1none
    mov [font_rn_n], bp
    mov [font_rn_n2], bp        ; the default is NO span: every cell drawn
    mov word [font_rn_k], 0     ; singly, and the plain row walk below
    cmp bp, 2
    jb .p1none                  ; one cell cannot repeat anything
    mov ax, [bx-2]              ; the last cell's glyph against the one before
    cmp ax, [bx-4]              ; it. On a run with no span THIS is the whole
    jne .p1none                 ; test - two words and a branch, not a walk,
                                ; which is what took the unpadded rows from
                                ; +1.50% back to the noise
    ; --- the TRAILING SPAN, found once (SPEC.md 6.1.8) ----------------------
    ; This system pads deliberately - 27.2 makes a Note Pad row's padding its
    ; ERASE, 12.9 composes the bar out to the clock, 28's columns are largely
    ; spaces - so the last cells of a run are usually the SAME cell repeated,
    ; and repeated cells compose to one byte. That is a `rep stosb`. The scan
    ; is here, once, rather than a compare per cell per row; SI and CX are
    ; free because the values they held are on the stack.
    mov si, bx                  ; AX already holds the repeated glyph
    sub si, 4                   ; ...and the one before it
    mov cx, 1                   ; the last cell always repeats itself
.p1trl:
    cmp cx, bp
    jae .p1trle
    cmp [si], ax
    jne .p1trle
    dec si
    dec si
    inc cx
    jmp short .p1trl
.p1trle:
    mov [font_rn_k], cx         ; cells the tail writes...
    mov ax, bp
    sub ax, cx
    mov [font_rn_n2], ax        ; ...and the ones drawn singly before it
.p1none:
    pop dx
    pop cx
    pop si
    or bp, bp
    jz .out                     ; nothing on screen: no rows to write
    CLIPQ                   
    je .rm
    ; A region is armed. stosb walks CONSECUTIVE bytes, so it is only correct
    ; if every cell is drawn - one rejected in the middle breaks the run into
    ; pieces. So the whole run has to sit inside one fragment; a cut one goes
    ; per cell, which is where a per-cell clip test lives anyway (SPEC.md 6.1.2).
    push cx
    push dx
    push si
    push di
    mov ax, cx                  ; AX = x1
    mov bx, dx                  ; BX = y1
    mov cx, bp
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx                      ; CX = x2 = x + 8n - 1
    add dx, 7                   ; DX = y2, in place - DX is already y1
    call wm_clip_test
    pop di
    pop si
    pop dx
    pop cx
%ifdef KERN_BIG
    jnc .rm
    jmp .escape                 ; SPEC.md 6.1.10/6.1.11: font_run_cell is the
                                ; MONO, byte-ALIGNED cell and there is no
                                ; planar or off-grid spelling of it - a cut run
                                ; that is either goes to font_run_scell, which
%else                           ; is gfx_fill + font_char and blind to both
    jc .jcell                   ; the adapter and the phase
%endif

    ; --- PASS 2: one ROW of the whole run at a time --------------------------
    ; The cells of a run are CONSECUTIVE BYTES within a row, so a row is a
    ; stosb walk and gfx_nextrow is paid once a row instead of once a row per
    ; cell - 12 of the old 27-byte row body, eight times over, per cell.
.rm:
    mov dx, [font_rn_xm]        ; DL = ink XOR background, DH = background:
                                ; the run's masks (SPEC.md 6.1.5) for all 8
                                ; rows, read as ONE word - the pair is
                                ; adjacent by %if below, xm first
    mov es, [vid_tseg]          ; ES:DI is the framebuffer from here on - the
    xor bp, bp                  ; string has been read and will not be touched
    mov ds, [font_seg]          ; ...and DS:BX the glyph rows (SPEC.md 6), to
                                ; the `push cs / pop ds` each pass ends with:
                                ; every kernel word a pass reads goes through
                                ; CS, the table's `lodsw` included
%ifdef KERN_BIG
    cmp byte [cs:font_rn_k7], 0
    jne .rmu                    ; SPEC.md 6.1.11: off the byte grid, so a cell
                                ; does not own a byte - but the RUN still owns
                                ; all but two of them
%endif
    cmp word [cs:font_rn_k], FONT_RN_KMIN
    jb .rmp                     ; no span worth a `rep`: take the plain walk,
                                ; and take it at the RUN level. The span is a
                                ; property of the run, so a per-ROW test would
                                ; ask the same question eight times and charge
                                ; every unpadded run for the answer - measured
                                ; at +3.06% before this branch existed
.rmrow:
    mov si, font_rn_tab
    mov cx, [cs:font_rn_n2]     ; the cells before the trailing span
    push di                     ; this row's first byte
    jcxz .rmtail                ; ...of which there may be none at all
.rmpx:
    FONT_RN_PX
    loop .rmpx
.rmtail:
    mov cx, [cs:font_rn_k]      ; the repeated tail: one compose, then the
    cs lodsw                    ; string instruction writes every byte of it.
    xchg ax, bx                 ; SI already points at the first tail entry,
    add bx, bp                  ; and they all carry the same glyph
    mov al, [bx]
    and al, dl
    xor al, dh
    rep stosb
    pop di
    FONT_RN_ROWEND
    inc bp
    cmp bp, 8
    jb .rmrow
    push cs                     ; DS the kernel's again (and not through
    pop ds                      ; .rmout: one jump, not two)
    jmp .out
.rmp:                           ; ...and the same rows with no tail at all
    mov si, font_rn_tab
    mov cx, [cs:font_rn_n]
    push di
.rmppx:
    FONT_RN_PX
    loop .rmppx
    pop di
    FONT_RN_ROWEND
    inc bp
    cmp bp, 8
    jb .rmp
.rmout:
    push cs                     ; DS the kernel's again
    pop ds
    jmp .out

%ifdef KERN_BIG
; --- SPEC.md 6.1.11: the run owns all but TWO of its bytes -------------------
; 6.1.4 argued that unaligned cannot be made fast, and it is right about a CELL
; and wrong about a RUN. Its figure - "4 accesses per cell row" - counts the
; neighbours whose bits must be preserved. In a run, for every cell but the
; last, that neighbour is THE NEXT CELL OF THE SAME RUN: bits the run owns and
; is about to write anyway. A run of n cells covers n+1 framebuffer bytes, of
; which n-1 are wholly inside it, so a row is (n-1) plain stores and TWO merges
; - n+3 accesses against 4n, and 104 against 320 for the ten cells 6.1.4 prices.
;
; The shift is one instruction a cell and no table. With k = x & 7 and c[] the
; composed cells, output byte j is (c[j-1] << (8-k)) | (c[j] >> k), which is
; exactly what `mov ah,c[j-1]` / `mov al,c[j]` / `shr ax,cl` leaves in AL. An
; 8088 charges 8+4k for that shift, so 12-36 clocks against an aligned cell
; row's ~40 - the whole reason this is worth doing rather than the pair.
;
; THE TWO EDGE BYTES ARE COLUMN-MAJOR, NOT PER ROW. Their masks are constant
; for the whole run, so doing them per row would set the VGA Bit Mask sixteen
; times and read-modify-write in the middle of a `stosb` walk. Instead each
; row STASHES its two edge bytes, and two column passes at the end write them
; with the mask set ONCE. gfx_fill_gray's .lcol / .irow / .rcol, in the same
; order and for the same reason.
;
; CX carries the SHIFT in CL and the previous composed byte in CH, so there is
; no register left for a cell counter - the walk ends on SI reaching the glyph
; table's end instead. That is why [font_rn_send] exists.
%macro FONT_RN_UPX 0
    cs lodsw                    ; AX = this cell's glyph base (CS:SI, the table)
    add ax, bp                  ; ...+ the row this pass is on
    xchg ax, bx                 ; BX = it, and the old BX is spent
    mov al, [bx]                ; the glyph row byte (DS = its segment)
    and al, dl
    xor al, dh                  ; AL = this cell, composed
    mov ah, ch                  ; AH = the cell BEFORE it...
    mov ch, al                  ; ...and this one becomes that for the next
    shr ax, cl                  ; AL = the framebuffer byte the two straddle
%endmacro

.rmu:
    mov ax, [cs:font_rn_n]      ; where the cell walk ends (see above)
    shl ax, 1
    add ax, font_rn_tab
    mov [cs:font_rn_send], ax
    mov cl, [cs:font_rn_k7]
    push di                     ; the run's first byte, for the column passes
.rmurow:
    mov si, font_rn_tab
    xor ch, ch                  ; no cell before the first: its bits fall off
    push di                     ; the top of the shift and are masked away
    FONT_RN_UPX                 ; ...so this is the LEFT edge byte
    mov [cs:bp + font_rn_le], al ; **THE `cs:` IS LOAD-BEARING.** SS != DS, so
                                ; a [bp+disp] operand addresses SS by default
                                ; (SPEC.md 1 rule 2) and font_rn_le is .bss,
                                ; i.e. KERNEL_SEG - which is CS here, DS being
                                ; the glyph table's. Without the override this store
                                ; lands at a .bss offset inside LOW_SEG -
                                ; ~42 KB past the top of task 0's stack, past
                                ; VGABUF_SEG and INTO THE CLAIM HEAP, which
                                ; stkwater.py and sched.inc's canary would both
                                ; stay silent about. The prefix is 5 bytes
                                ; against `mov bx,bp` + `mov [bx+d16],al`'s 6,
                                ; and 21.7 clocks against 26 by the floor, so
                                ; it is better on both terms and BX stays free.
                                ; BP is the row index, 0..7 against `resb 8`,
                                ; bounded six lines from here
    inc di                      ; it is written by the column pass, not here
    cmp si, [cs:font_rn_send]
    jae .rmuend                 ; a one-cell run: no interior at all
.rmumid:
    FONT_RN_UPX
    stosb                       ; the interior: bytes the run owns whole
    cmp si, [cs:font_rn_send]
    jb .rmumid
.rmuend:
    mov ah, ch                  ; the RIGHT edge: the last cell's tail, with
    xor al, al                  ; nothing after it
    shr ax, cl
    mov [cs:bp + font_rn_re], al ; ...and the same override, for the same
                                ; reason - see the left edge above
    pop di
    FONT_RN_ROWEND
    inc bp
    cmp bp, 8
    jb .rmurow

    ; --- the two edge columns, each with its mask set ONCE ------------------
    push cs                     ; DS the kernel's again: the columns read the
    pop ds                      ; edge bytes out of .bss
    pop di                      ; back to the run's first byte
    mov al, 0xFF
    shr al, cl                  ; AL = the LEFT byte's mask: our (8-k) low bits
    mov dl, al
    push di
    mov si, font_rn_le
    call fnt_rn_edge
    pop di
    add di, [font_rn_n]         ; ...and the right byte sits n bytes along
    not dl                      ; THE RIGHT BYTE'S MASK IS THE LEFT'S
                                ; COMPLEMENT by definition - its k high bits
                                ; against the left's 8-k low ones - so DH is
                                ; not needed to carry it across the call.
                                ; fnt_rn_edge restores DX by push/pop and its
                                ; VGA arm DESTROYS DL inside (it loads the GC
                                ; port into DX), so that restore is
                                ; load-bearing here and not incidental
    mov si, font_rn_re
    call fnt_rn_edge
.jout:
    jmp .out

%endif

.cell:                          ; the per-cell path: a clip-cut run, and the
    mov al, [es:si]             ; overflow that cannot happen
    or al, al
%ifdef KERN_BIG
    jz .jout                    ; (the `jmp .out` just above: two bytes, not
%else                           ; five, and 4 clocks a cell not-taken)
    jz .out
%endif
    inc si
    call font_run_cell          ; CX/DX/DI = this cell; preserves all three
    add cx, 8
    inc di                      ; an aligned cell is one byte wide, so the
    jmp short .cell             ; next one is the next byte along

%ifdef KERN_BIG
; --- SPEC.md 6.1.10: mode 12h's single-store prologue -------------------------
; The mono fast path exists because a 1bpp cell OWNS its framebuffer byte, so
; the background and the glyph arrive in one store. On VGA a byte is eight
; pixels of FOUR planes, and font_char reaches them by selecting Set/Reset per
; cell and masking per ROW - two `out`s a cell and, per row, another `out` and
; one or two LATCHED read-modify-writes. But that is a property of the CELL
; loop and not of the adapter: a plane is one bit of the colour INDEX, and a
; run has ONE pair of colours, so which planes vary and which are constant is
; a question the run answers once.
;
;   A = fg & ~bg   the planes that want the glyph byte
;   B = ~fg & bg   ...and the planes that want its complement
;   C = ~(fg ^ bg) the planes both colours agree on - a CONSTANT byte, which
;                  is exactly what Set/Reset is for
;
; With GC1 (Enable Set/Reset) = C and GC0 = fg, one CPU byte written at Bit
; Mask FF gives C its constant and hands A|B the data. **The Bit Mask being FF
; is what removes the read**: every bit comes from Set/Reset or from the CPU,
; so no latch has to be loaded first. The `out`s go with it - they are the
; run's now, not the cell's.
;
; One pass therefore serves any pair where A or B is EMPTY, which is to say
; where one colour's plane bits are a SUBSET of the other's: black on white,
; white on black, black on any colour, any colour on white, white on any
; colour. That is every pen this system sets bar one - the Color theme's
; disabled chrome, CDGRAY on CLGRAY (8 against 7), which shares no plane in
; either direction and takes .slow. 6.1.10 has the census.
;
; The composed byte is the MONO PATH'S OWN - (glyph & xm) ^ bm, with xm = FF
; and bm = 00 to write the glyph or FF to write its complement - so the row
; pass, the glyph table and 6.1.8's trailing span below are shared code and
; not a second renderer. That is the whole reason this is a prologue and not
; a routine.
.planar:
    cmp dx, [vid_chm8]          ; the run's y, once, exactly as on the mono
    ja .jout                    ; path (SPEC.md 6.1.6)
    cmp cx, [vid_cwm8]          ; ...and a negative x goes per cell, same rule
    ja .cells
    push si                     ; .p1's frame (see .p1pre): the grouping spends
    push cx                     ; AX and BX only, but gfx_rowbase below spends
    push dx                     ; CX and DX
    mov al, [font_rn_fg]
    mov ah, [font_rn_bg]
    and ax, 0x0F0F              ; a colour is four planes and nothing else
    mov bl, al
    xor bl, ah                  ; BL = the planes the two colours differ on
    mov bh, al
    and bh, bl                  ; BH = A, the planes that want the glyph
    and ah, bl                  ; AH = B, the planes that want its complement
    or bh, bh
    jz .plcpl                   ; A empty: every varying plane wants ~glyph
    or ah, ah
    jnz .plno                   ; both non-empty: two passes, not this one
    mov byte [font_rn_bm], 0x00 ; B empty: the CPU byte IS the glyph
    jmp short .plset
.plcpl:
    mov byte [font_rn_bm], 0xFF
.plset:
    mov byte [font_rn_xm], 0xFF ; the compose is a pass-through either way...
    cmp byte [gfx_mono1], 0     ; ...UNLESS THIS WINDOW DECLARED TWO COLOURS
    je .plgo                    ; AND THE RUN IS DISABLED (SPEC.md 11.96.17.1).
    cmp byte [gfx_dis], 0       ; [gfx_dis] AND NOT [font_rn_dt], which is 0
    je .plgo                    ; here by construction: 6.1.12's mask is
                                ; computed in the MONO prologue and this
                                ; branch left before it.
                                ; A greyed control on a VGA says so by being
                                ; CDGRAY, and a window that claims one plane
                                ; cannot carry colour 8 - so gfx_pen_dis
                                ; rounds the pen to black for it, and then the
                                ; ONLY thing left to say "dead" is 47 rule 1's
                                ; checkerboard, which this prologue was
                                ; throwing away. Without it a greyed caption
                                ; comes out pixel-identical to a live one,
                                ; which is that rule's own failure.
                                ;
                                ; xm is a mask over the GLYPH here and not an
                                ; ink-xor-background, so it halves the ink for
                                ; either bm: 00 writes (glyph & AA) and FF
                                ; writes its complement, which is the same
                                ; pixels read the other way up.
    push ax
    mov al, 0xAA
%ifdef KERN_BIG
    test byte [font_rn_k7], 1   ; ...on the other phase for an odd skew, so
    jz .plph                    ; the checkerboard lands in the same screen
    mov al, 0x55                ; columns whatever the cell's x is
.plph:
%endif
    mov [font_rn_xm], al
    mov byte [font_rn_dt], 0xFF ; ...and the per-row swap is the whole mask,
    pop ax                      ; the two colours having been reduced to
.plgo:                          ; 00/FF by font_ink before we got here
    mov byte [font_rn_pl], 1
    not bl
    and bl, 0x0F                ; BL = C, the planes Set/Reset answers for
    mov ah, al                  ; ...with fg's bits, and on those planes fg
    mov al, 0                   ; and bg agree, so either colour would do
    mov dx, VGA_GC
    out dx, ax                  ; GC0 Set/Reset = the constant planes' colour
    mov ah, bl
    mov al, 1
    out dx, ax                  ; GC1 Enable Set/Reset = C
    jmp .rowb                   ; ...and the row base is the mono prologue's,
                                ; the same instructions on the same frame
.plno:                          ; the pair shares no plane in either direction
    pop dx                      ; and needs two passes: .slow draws it
    pop cx
    pop si
%endif

.slow:
    ; The fallback: what every caller wrote by hand, one gfx_fill of the run's
    ; rect and font_str over it - EXCEPT that the pair clips at two different
    ; granularities, so under an armed clip region an edge crossing the run
    ; fills the visible rows and letters none of them, and the run goes BLANK
    ; rather than stale (SPEC.md 11.3/6.1.2). That is affordable for a caller
    ; who knows to gate on it; it is not affordable for a primitive the kernel
    ; itself draws through. So the pair is used only where it cannot
    ; disagree - no region armed, or the whole run inside one fragment - and a
    ; run an edge actually cuts goes per cell.
    call font_width_x           ; AX = 8 * length (CX/DX/SI preserved)
    or ax, ax
    jz .out                     ; empty string: nothing to fill and nothing
                                ; to letter
    push cx                     ; the pen, wanted again by font_str_x below
    push dx
    add ax, cx
    dec ax                      ; AX = x2
    mov bx, dx                  ; BX = y1
    add dx, 7                   ; DX = y2
    xchg ax, cx                 ; AX,BX,CX,DX = the run's rect - built ONCE,
                                ; above the test rather than once on each side
                                ; of it. wm_clip_test's contract is "clobbers:
                                ; flags", so the rect survives the call and
                                ; the fill below can have it as it stands
    CLIPQ                   
    je .whole                   ; disarmed: the pair cannot disagree
    call wm_clip_test
    jnc .whole                  ; ...or the whole run is inside one fragment
    pop dx                      ; an edge cuts it: the pen back into CX/DX,
    pop cx                      ; which is what .cells draws from. AX is x1
    jmp .cells                  ; here where it used to be the width, and
                                ; .cells opens `mov al,[es:si]` - dead either
                                ; way. BX is y1 in both spellings
.whole:
    push ax
    mov al, [font_rn_bg]
    mov [gfx_color], al
    pop ax
    call gfx_fill
    pop dx
    pop cx
    mov al, [font_rn_fg]
    mov [gfx_color], al
    call font_str_x
    jmp short .out
.cells:
    mov al, [es:si]
    or al, al
    jz .out
    inc si
    call font_run_scell
    add cx, 8
    jmp short .cells
.out:
%ifdef KERN_BIG
    cmp byte [font_rn_pl], 0    ; SPEC.md 6.1.10: the planar prologue enabled
    je .outd                    ; Set/Reset on the planes both colours agree
    push ax                     ; on, and vga12.inc's standing contract is that
    push dx                     ; every routine leaves Enable Set/Reset = 0.
    mov dx, VGA_GC              ; GC0 is NOT part of that resting state - the
    mov ax, 0x0001              ; Bit Mask and the Map Mask are, and neither
    out dx, ax                  ; was touched - so only GC1 is put back
    pop dx
    pop ax
.outd:
%endif
    GFXDLEAVEI
.outp:                          ; ...and BELOW the leave, which is the whole
    jmp kret_bp
%ifdef KERN_BIG
.cells_nx:                      ; SPEC.md 39.14.6: per cell and UNTRANSLATED,
    mov al, [es:si]             ; so each font_char resolves its own display -
    or al, al                   ; and no GFXDLEAVE, because no enter was taken
    jz .outp
    inc si
    call font_run_scell
    add cx, 8
    jmp short .cells_nx
%endif

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; fnt_rn_edge - internal: one of an unaligned run's two edge COLUMNS
;               (SPEC.md 6.1.11)
;
; in:       DI = the column's first framebuffer byte, DL = the mask (a 1 bit is
;           the RUN's), SI -> the 8 bytes that row pass stashed, ES = the
;           framebuffer, [font_rn_pl] = which renderer
; out:      nothing; clobbers AX, BX, SI, DI, flags. BP IS NO LONGER TOUCHED
;           and CX is: both row loops below count in CX, which this routine
;           already banks, where they used to count in BP, which it did not.
;           DX is banked too and that pair is LOAD-BEARING rather than
;           incidental - the VGA arm loads the GC port into DX and so destroys
;           DL, which is the caller's mask.
;
; A column and not a row because the mask is the RUN's - constant for all eight
; rows - so a VGA Bit Mask is set ONCE here instead of sixteen times inside the
; interior's stosb walk. That is the whole reason the row pass stashes rather
; than merging as it goes.
;
; On VGA the Bit Mask IS the merge: the bits outside it come from the latches,
; which is what the read is for. On a 1bpp adapter there are no latches and the
; merge is done in the register.
; -----------------------------------------------------------------------------
fnt_rn_edge:
    push cx
    push dx
%ifdef KERN_BIG
    cmp byte [font_rn_pl], 0
    je .mono
    push ax
    mov dh, dl                  ; GC8 Bit Mask = the run's bits, once
    mov al, 8
    mov ah, dh
    mov dx, VGA_GC
    out dx, ax
    pop ax
    mov cx, 8                   ; a fixed eight rows: `loop` is 2 bytes and 17
.vrow:                          ; clocks where `inc bp / cmp bp,8 / jb` is 6
    mov al, [si]                ; and ~22, and FONT_RN_NEXTROW touches only DI
    inc si                      ; and the flags
    xchg [es:di], al            ; the latches (the read), then AL (the write):
                                ; the masked-out bits come back from them
                                ; untouched. xchg is that read and that write
                                ; in one instruction - font_char's latch idiom
    FONT_RN_NEXTROW
    loop .vrow
    mov ax, 0xFF08              ; ...and the Bit Mask back to its resting FF
    mov dx, VGA_GC              ; (vga12.inc's standing contract, SPEC.md 1.7)
    out dx, ax
    jmp short .out
%endif
.mono:
    mov bl, dl
    not bl                      ; BL = the bits that are NOT ours
    mov cx, 8                   ; ...and the same eight here
.mrow:
    mov al, [si]
    inc si
    and al, dl
    mov ah, [es:di]
    and ah, bl
    or al, ah
    mov [es:di], al
    FONT_RN_NEXTROW
    loop .mrow
.out:
    pop dx
    pop cx
    ret

%endif
; -----------------------------------------------------------------------------
; font_run_cell - internal: one opaque 8x8 cell, byte-aligned, [vid_mono] path
; in:       AL = character, CX = x (multiple of 8), DX = y, DI = the cell's
;           first framebuffer byte, [font_rn_xm]/[font_rn_bm] = the run's masks
; out:      nothing (all registers preserved)
;
; The row loop is font_char_bb's with the read-modify-write replaced by a
; store: the cell owns its whole byte, so there is nothing underneath to
; preserve, and the background arrives in the same write as the glyph. The
; byte is (glyph & fg) | (~glyph & bg) computed as (glyph & (fg^bg)) ^ bg,
; both terms handed in by the caller (SPEC.md 6.1.5).
;
; THERE IS NO PLANE LOOP, and that is font_run_x's gate rather than an
; assumption here: [vid_mono] is tested three instructions above the .cell
; loop, and a 1bpp target has one plane. A planar run takes .slow (SPEC.md
; 6.1), so the 4-plane walk this used to carry could never run.
;
; What is left is exactly what differs between two cells of one word: the
; character, the x edge test, the clip test and one byte of DI. Everything a
; RUN shares - the y test, both masks, the row base - is the caller's, once
; (SPEC.md 6.1.6).
; -----------------------------------------------------------------------------
font_run_cell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp cx, [vid_cwm8]          ; x only: the run's y was tested once, by the
    ja .outq                    ; caller (SPEC.md 6.1.6)
    CLIPQ                        ; the cell against the region in ROWS, exactly
    je .noclip                  ; as its glyphs are, background with them
    push ax                     ; (SPEC.md 11.3.2)
    mov ax, cx                  ; ...and x2/y2 in place: CX and DX already
    mov bx, dx                  ; hold x and y
    add cx, 7
    add dx, 7
    call wm_clip_rows           ; CX and DX are spent here and NOT put back:
    pop ax                      ; nothing below reads them, the masks arriving
    jc .outq                    ; in CX from memory instead
%ifdef KERN_BIG
    mov dl, [wm_clip_cm]        ; SPEC.md 11.3.4: the COLUMNS too. DX is free
                                ; from here to the epilogue, which puts the
                                ; caller's back
%endif
    jmp short .have_rows
.outq:                          ; (the two exits, in short reach below the
    jmp .out                    ; one jump there is: `jcc` two bytes and not
.noclip:                        ; five, 4 clocks a cell not-taken where 16)
    mov word [wm_clip_r0], (8 << 8) | 0     ; the whole cell, in ONE store -
                                ; font_char's own spelling eight lines from
                                ; here, and the pair is adjacent by assertion
                                ; (wm.inc)
%ifdef KERN_BIG
    mov dl, 0FFh                ; ...and every column
%endif
.have_rows:
    cmp al, FONT_FIRST
    jb .blank
    cmp al, FONT_LAST
    ja .blank
    sub al, FONT_FIRST
    cbw                         ; 0..94, and the arithmetic stays in AX for the
    shl ax, 1                   ; accumulator form of the `add` - see font_char
    shl ax, 1
    shl ax, 1
    add ax, [font_base]
    xchg ax, si
    jmp short .have
.blank:
    FONT_BLANK si               ; an unprintable character is a blank cell -
.have:                          ; which still has to be PAINTED here

    mov es, [font_seg]          ; ES:SI = the glyph (SPEC.md 6); DS becomes the
                                ; framebuffer (SPEC.md 39.3) below, once the
                                ; last kernel word is read. DI arrived
    mov cx, [font_rn_xm]        ; pointing at this cell's first byte.
                                ; CL = ink XOR background, CH = background,
                                ; one word (the %if below pins the order)
    mov ax, [wm_clip_r0]        ; AL = where the rows start, AH = how many -
    mov bh, ah                  ; the same trick on wm.inc's own asserted pair,
    cbw                         ; and cheaper THIS way round because
                                ; `mov al,[addr]` is already the 3-byte
                                ; accumulator form. AL is spent below (SI
                                ; carries the glyph); BL is free until .row
    test al, 1                  ; SPEC.md 6.1.12: the caller seeded the mask
    jz .r0even                  ; for the run's y, and a clipped cell starts
    xor cl, [font_rn_dt]        ; r0 rows below it - odd means the other phase
.r0even:
    mov ds, [vid_rseg]          ; DS = the framebuffer, to the `pop ds` below
    or al, al
    jz .row                     ; the whole cell: the common case skips this
    add si, ax                  ; the glyph's first drawable row...
    mov bl, al
.skip:
    add di, [cs:vid_rowadd]     ; ...and DI down to the screen row it goes on.
    test di, [cs:vid_wrapbit]   ; Stepped rather than multiplied because the
    jz .sknw                    ; layout is BANKED (SPEC.md 39.3) - r0 is 1..7
    add di, [cs:vid_wrapfix]    ; on a path that until now drew nothing at all
.sknw:
    dec bl
    jnz .skip
.row:
    mov al, [es:si]             ; AL = the glyph row
    inc si
    and al, cl                  ; a set bit picks ink, a clear bit background
    xor al, ch                  ; - the same byte, three instructions fewer
%ifdef KERN_BIG
    cmp dl, 0FFh                ; SPEC.md 11.3.4.2: a column cut. The region
    jne .merge                  ; overlaps the cell now rather than containing
.store:                         ; it, so a window over part of the byte is
%endif                          ; NOT ours to store over
    mov [di], al                ; the cell owns the byte: store, never merge
    xor cl, [cs:font_rn_dt]     ; SPEC.md 6.1.12: 0 unless this is disabled
    add di, [cs:vid_rowadd]     ; (gfx_nextrow inlined - see font_char_bb)
    test di, [cs:vid_wrapbit]
    jz .nw
    add di, [cs:vid_wrapfix]
.nw:
    dec bh
    jnz .row
    push cs                     ; DS the kernel's again
    pop ds
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%ifdef KERN_BIG
.merge:                         ; ...so only the mask's bits change: old ^
    mov ah, [di]                ; ((old ^ new) & mask). Out of line, because a
    xor al, ah                  ; cut cell is at most the two at a run's ends
    and al, dl                  ; and the whole one is every other
    xor al, ah
    jmp short .store
%endif

; -----------------------------------------------------------------------------
; font_run_scell - internal: one opaque cell the SLOW way, for a run an armed
;                  clip region cuts (see font_run_x's .cells)
; in:       AL = character, CX = x, DX = y
; out:      nothing (all registers preserved)
;
; Fill the cell, letter the cell - but behind ONE wm_clip_test about that
; cell, so the two halves cannot disagree about it. That is the same decision
; font_run_cell makes on the fast path; this is just the version that works
; on a planar target or at an unaligned x, where the cell does not own its
; framebuffer byte and there is nothing cleverer to do.
; -----------------------------------------------------------------------------
font_run_scell:
    mov [font_rn_ch], al
    push ax
    push bx
    push cx
    push dx
    mov al, [font_rn_bg]        ; THE FILL'S PEN, set here where AL is free -
    mov [gfx_color], al         ; the character is in [font_rn_ch] and the
                                ; rect has not been built yet. In the middle,
                                ; where it used to be, it cost a push/pop pair
                                ; around it because AL was x1's low byte by
                                ; then. Nothing between here and gfx_fill
                                ; writes [gfx_color] (wm_clip_rows clobbers
                                ; flags only).
                                ; It does mean a cell the region REFUSES now
                                ; leaves the pen at the background where it
                                ; used to leave it alone. font_run already
                                ; changes the pen on every fallback path and
                                ; its header does not promise otherwise, so
                                ; this widens a documented asymmetry rather
                                ; than creating one - and a run whose every
                                ; cell is refused has drawn nothing either way
    mov ax, cx                  ; AX,BX,CX,DX = this cell's rect, inclusive -
    mov bx, dx                  ; x2 and y2 built in place, CX and DX being
    add cx, 7                   ; x and y already
    add dx, 7
    call wm_clip_rows           ; ...in ROWS (SPEC.md 11.3.2), so a cell an
    jc .out                     ; edge crosses draws its visible band rather
                                ; than nothing - and on kern_big a vertical
                                ; cut too (11.3.4): gfx_fill clips the band per
                                ; pixel and font_char masks the columns
    push ax                     ; bank the cell's own origin - the fill wants
    push bx                     ; the band, font_char wants the cell
    mov dx, [wm_clip_r0]        ; DL = r0, DH = rn: ONE load, the pair being
    add bl, dl                  ; adjacent (wm.inc's %if says so)
    adc bh, 0                   ; BX = the band's first row...
    mov dl, dh
    mov dh, 0
    add dx, bx
    dec dx                      ; ...and DX its last
    call gfx_fill               ; the BAND, not the cell: the erase has to
                                ; cover what the glyph is about to letter and
                                ; nothing else, or the two disagree again
    mov al, [font_rn_fg]        ; the glyph's pen; AL is free again, the
    mov [gfx_color], al         ; character coming back out of [font_rn_ch]
    pop dx                      ; ...and the two banked coordinates ARE y1 and
    pop cx                      ; x1, which is exactly where font_char takes
                                ; them - no hop through AX and BX at all. BX
                                ; is left holding whatever gfx_fill did and
                                ; nothing below reads it before the outer
                                ; epilogue pops the caller's
    mov al, [font_rn_ch]
    call font_char              ; ...and asks the region the same question,
.out:                           ; so it letters exactly the rows just erased
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; -----------------------------------------------------------------------------
; font_width - pixel width of a NUL-terminated string
;
; in:       SI -> ASCIIZ string
; out:      AX = 8 * length
; clobbers: nothing else
; -----------------------------------------------------------------------------
font_width:                     ; FAR-ENTERED, no near caller of its own
    push es                     ; (SPEC.md 2.6.1): the `cw_font_width` shim
    push ds                     ; that used to `call` this and `retf` is gone
    pop es                      ; and every site far-calls the body itself
    call font_width_x           ; (font_width_x stays NEAR and is unaffected)
    pop es
    retf

; -----------------------------------------------------------------------------
; font_width_x - font_width with the string at ES:SI (SPEC.md 20.1)
; in:       ES:SI -> ASCIIZ string
; out:      AX = 8 * length
; clobbers: nothing else
; -----------------------------------------------------------------------------
font_width_x:
    push si
    xor ax, ax
.next:
    cmp byte [es:si], 0
    je .done
    inc si
    add ax, 8
    jmp .next
.done:
    pop si
    ret

; -----------------------------------------------------------------------------
%ifdef BAKED_FONT
section .lowbss                 ; LOW_SEG (SPEC.md 2.1): a BAKED face (SPEC.md
                                ; 6.2) is overlay bytes and has to be copied
                                ; somewhere that stays - and only then. The
                                ; ROM's face is read where it is
font_glyphs:    resb 760        ; glyphs 32..126, 8 bytes per row (FONT_BYTES)
font_zero:      resb 8          ; the blank cell an unprintable character
                                ; draws, in the table's own segment: .lowbss
                                ; is not zeroed by anything, so ovl_font_init
                                ; clears it by hand
%endif
%ifdef SEAMCUT
section .lowbss
font_seam_a:    resb 8          ; SPEC.md 39.14.11: the two halves of the one
font_seam_b:    resb 8          ; cell a display seam crosses - the part on
                                ; this display and the part that spills onto
                                ; the next. RAM, being composed per cut, and
                                ; reached by the glyph arithmetic already in
                                ; font_char's body with [font_seg] = LOW_SEG
                                ; and [font_base] pointed so FONT_SEAM_A lands
                                ; on font_seam_a (font_ch_seam). font_ch_cut
                                ; writes all eight rows of each, so nothing
                                ; clears them
%if font_seam_b - font_seam_a != 8 || FONT_SEAM_B - FONT_SEAM_A != 1
  %error "font_seam_b must be the cell after font_seam_a, as FONT_SEAM_B is the code after FONT_SEAM_A - see 39.14.11"
%endif
%endif
section .bss
%ifdef SEAMCUT
font_ch_on:     resb 1          ; SPEC.md 39.14.11: non-zero while font_ch_cut
                                ; is drawing a half-cell, and the only thing
                                ; that lets FONT_SEAM_A/B past font_char's
                                ; FONT_LAST test. A COUNTER rather than a flag
                                ; because a spill can cut again - it is the one
                                ; piece of the cut's state that outlives a
                                ; recursive call
%endif
font_seg:       resw 1          ; THE GLYPH TABLE (SPEC.md 6): its segment - the
font_base:      resw 1          ; ROM's - and the offset of glyph FONT_FIRST in
                                ; it. font_init writes both at boot; every
                                ; renderer reads the rows through DS or ES
                                ; loaded from here, and OSAPI_FONT_GLYPHS
                                ; hands the pair out as DX:SI
font_rn_fg:     resb 1          ; font_run: the run's two colours as the
font_rn_bg:     resb 1          ; caller gave them, for the .slow path
font_rn_dt:     resb 1          ; SPEC.md 6.1.12: what FONT_RN_ROWEND XORs
                                ; into DL at every row - 0 for an ordinary
                                ; run, and the run's whole mask for DISABLED
                                ; text, whose two checkerboard phases the
                                ; mask partitions
; ADJACENT AND IN THIS ORDER: font_run_x's .rm and font_run_cell both read the
; pair as one word (`mov dx,[font_rn_xm]`, `mov cx,[font_rn_xm]`), which is
; wm_clip_r0/rn's arrangement one file along and carries wm.inc's assertion
; with it. The %if is at the foot of this file, where both labels exist.
font_rn_xm:     resb 1          ; ...and the run's reduced masks (SPEC.md
font_rn_bm:     resb 1          ; 6.1.5/6.1.6): ink XOR background, and
                                ; background. Derived ONCE per run and read
                                ; into CX by every cell, which is why they are
                                ; here and not in registers: CX is the x the
                                ; cell loop is walking
%ifdef KERN_BIG
font_rn_pl:     resb 1          ; SPEC.md 6.1.10: 1 = the masks came from the
                                ; PLANAR prologue, so the per-cell escapes go
                                ; to font_run_scell rather than font_run_cell
                                ; and the Graphics Controller is owed a reset
%endif
%ifdef KERN_BIG
font_rn_k7:     resb 1          ; SPEC.md 6.1.11: the pen's phase, x & 7. 0 is
                                ; the aligned path and every other value is the
                                ; shift the row pass runs at
font_rn_send:   resw 1          ; ...and where the cell walk stops, because CX
                                ; is carrying the shift and the previous byte
                                ; and has nothing left to count with
font_rn_le      equ ico_ibuf + FONT_RN_MAX*2     ; UNION, see font_rn_tab
font_rn_re      equ ico_ibuf + FONT_RN_MAX*2 + 8 ; the two edge bytes each row
                                ; composed, held
                                ; back for the column passes. ADJACENT and in
                                ; this order is not required - fnt_rn_edge is
                                ; handed a pointer - but they are 16 bytes and
                                ; keeping them together is one cache line of
                                ; a reader's attention
%endif
font_rn_ch:     resb 1          ; font_run_scell's character, parked because
                                ; AL is the rect's low byte by the time the
                                ; letter is drawn
font_rn_n2:     resw 1          ; cells drawn singly, before the trailing span
font_rn_k:      resw 1          ; ...the span's length, at least 1
font_rn_n:      resw 1          ; SPEC.md 6.1.7: cells the run will draw, and
font_rn_tab     equ ico_ibuf    ; a glyph pointer for each. 180 bytes, taken
                                ; deliberately: it is what lets the drawing
                                ; pass go ROW-major, which pays gfx_nextrow
                                ; once a row instead of once a row per cell.
                                ; **A UNION WITH THE ICON RENDERER'S SCRATCH**
                                ; (kernel size pass 4): ico_ibuf/ico_stage is
                                ; per-call scratch of icon_draw_ix/icon_draw_x,
                                ; this table (and font_rn_le/_re above it, on
                                ; kern_big) is per-call scratch of font_run_x,
                                ; both run under the gfx lock and neither calls
                                ; the other, so no two of them are ever live
                                ; at once. The assertion in icons.inc is what
                                ; keeps the three inside the cell
font_dith:      resb 1          ; 1 = this glyph is disabled text on a mono
                                ; adapter (SPEC.md 47); font_ink decides it
                                ; per glyph and writes it on every path
                                ; (font_dmask, the row's half of the
                                ; checkerboard, is DL in font_char_bb now)

%if (font_rn_bm - font_rn_xm) != 1
  %error "font_run_x's .rm and font_run_cell both read the run's masks as ONE word (`mov dx,[font_rn_xm]`, `mov cx,[font_rn_xm]` -> low = xm, high = bm): they must stay adjacent, xm first (SPEC.md 6.1.5)"
%endif

%ifdef BAKED_FONT
; =============================================================================
; The baked typeface (SPEC.md 6.2), in the BOOT OVERLAY (SPEC.md 2.5).
;
; 760 bytes that cost neither guard anything: .ovl lands in the FAT window and
; is written over by drv_boot's first mount, which is the last thing kmain
; does - and this runs well before it, beside ovl_clk_init and drv_snd_sniff.
; So the whole price of carrying our own font is ONE floppy sector at boot.
;
; It is an overlay ENTRY rather than a table .text reaches into, and that is
; the section's own contract rather than taste: `.ovl` has a vstart of its own
; and DS is KERNEL_SEG throughout it, so overlay data is reachable only
; through CS - which is what this does, and what kernel.asm's section header
; says anything added here would have to. tools/os88ovlchk.py cannot see a
; data reference across that boundary, only a call, so the rule is worth
; keeping to rather than working around.
; =============================================================================
%ifndef KERN_SMALL
section .ovlw
%else
section .ovl                    ; kern_small: the BLOB half, font_init's own
                                ; placement and for its reason - that kernel's
                                ; `.ovlw` sits on the FAT window and the mount
                                ; buffers, and 760 more bytes overran it, so
                                ; FONT= did not build on kern_small at all
%endif
; -----------------------------------------------------------------------------
; ovl_font_init - copy the baked typeface into font_glyphs
;
; in:       nothing (CS = the blob's, DS = KERNEL_SEG, SS = LOW_SEG)
; out:      nothing
; clobbers: nothing - it is called once, from kmain, exactly where the ROM
;           probe used to be called, and owes the same contract
; -----------------------------------------------------------------------------
ovl_font_init:
    push ax
    push cx
    push si
    push di
    push ds
    push es

    push cs                     ; the glyphs are our OWN data: DS has to leave
    pop ds                      ; KERNEL_SEG to reach them, and comes back below
    mov si, ovl_font_bits
    mov ax, LOW_SEG             ; the table is in .lowbss (SPEC.md 2.1), so the
    mov es, ax                  ; copy runs segment to segment and touches
    mov di, font_glyphs         ; neither one's own
    mov cx, FONT_BYTES / 2
    cld
    rep movsw

    mov cx, 4                   ; ...and the blank cell beside it, which
    xor ax, ax                  ; nothing else zeroes: .lowbss is nobits. ES is
    rep stosw                   ; still LOW_SEG and DI is font_zero, font_glyphs'
                                ; end, from the copy above
    pop es
    pop ds
    mov word [font_seg], LOW_SEG    ; ...and the table is the copy (SPEC.md 6):
    mov word [font_base], font_glyphs ; DS is KERNEL_SEG again for the stores
    pop di
    pop si
    pop cx
    pop ax
    retf                        ; FAR: kmain reaches this through OVLCALL

%if font_zero != font_glyphs + FONT_BYTES
  %error "ovl_font_init clears font_zero straight off the end of its copy"
%endif
ovl_font_bits:
%include "font8x8.inc"          ; GENERATED by tools/os88font.py from the
                                ; fonts/*.f8 that FONT= named (-I build/)
%endif
section .text

%ifdef SNAPAUDIT
; SPEC.md 11.94.2's counters (see SNAPAUDIT_TALLY at the top of this file).
; In .text with real initialisers rather than .bss, because -f bin zeroes
; nothing and a host script reads these through os88sym.py by name.
;
; Four tables, not two: what a WINDOW draws inside its own callback and what
; the KERNEL draws around it are different questions with different owners, and
; mixed together the chrome's dozen title-bar glyphs are indistinguishable from
; an app's own skew. wm_pkgcall keeps the depth.
snap_hchar: times 8 dw 0        ; font_char inside a window callback
snap_hrun:  times 8 dw 0        ; font_run  inside a window callback
snap_kchar: times 8 dw 0        ; font_char outside one - chrome, menus, desktop
snap_krun:  times 8 dw 0        ; font_run  outside one
section .lowbss                 ; ...but the LOG goes to .lowbss, which costs
snap_calls: resw SNAP_NCALL*(SNAP_ENT/2)   ; the 64KB segment nothing (SPEC.md
section .text                   ; 2.1). 192 bytes here is what put `make
                                ; SNAPAUDIT=1` 15 bytes over guard 2, and guard
                                ; 2 is the one nobody can raise. It needs no
                                ; initialiser and that is not luck: snap_ncall
                                ; below stays in .text at 0, tools/os88snap.py
                                ; reads IT first and returns early when it is
                                ; 0, and never reads past it - so nothing ever
                                ; looks at a byte of this that was not written.
                                ; os88sym.linear answers LOW_SEG for it, so the
                                ; reader needs no change. 11.94.2's log:
                                ; (pen x, pen y,
                                ; character, return address) per off-grid
                                ; glyph in the audited window - what turns
                                ; "this app is off-grid" into the STRING
snap_ncall: dw 0                ; bytes used of it, so 0 means empty
snap_tmpc:  dw 0                ; the log's scratch: the caller's AX and
snap_tsave: dw 0                ; wm_draw_title's banked snap_cur: the
                                ; caption is CHROME (SPEC.md 11.94.2)
snap_tmpr:  dw 0                ; its return address, banked while BX is
                                ; needed for the slot address
snap_cur:   dw 0                ; the window whose callback is running, 0 =
                                ; none (wm_pkgcall stacks it)
snap_win:   dw 0                ; the HOST's filter: only this window's glyphs
                                ; go to snap_h*, 0 = any. Written from outside
                                ; the guest by tools/os88snap.py
%endif
