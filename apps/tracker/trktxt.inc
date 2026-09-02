; =============================================================================
; os8088 - apps/tracker/trktxt.inc
;
; The fullscreen 80x25 TEXT screen (SPEC.md 45.13). Prefix ttx_; XT mode
; forces this surface and View > Text Screen picks it outright (45.13.7), and
; NOTHING IN THIS FILE KNOWS WHICH - it never read [mp_xt], which is why
; decoupling the two was a gate change and not a port. Included by tracker.asm
; AFTER trkui.inc, so every tui_ helper it borrows -
; tui_scpy, tui_hexb, tui_dec3, tui_viewrow, tui_vu_step - is already defined.
;
; WHY THIS EXISTS, in one paragraph. The FT2 screen (SPEC.md 45.6) is drawn
; out of 8x8 glyph cells, and a glyph cell costs about a MILLISECOND on a
; 4.77 MHz 8088 with a Hercules card (PERFORMANCE.md Part 2). A Hercules
; pattern row is 44 of them, the grid is 29 rows, so a full tui_draw_pat is
; ~1.28 SECONDS and even SPEC.md 45.12's one-row scroll is ~174 ms against a
; row that arrives every 120 ms. That arithmetic is why SPEC.md 45.9.1 turned
; the grid off on a tier-0 machine and left one banded line moving in an empty
; field - the honest answer to a budget that had already been blown.
;
; In a text mode the card's own ROM font does everything font_char was doing.
; A character cell is ONE WORD STORE: no rasterization, no shift, no
; read-modify-write, no bank arithmetic. ~25 clocks for a rep movsw word on an
; 8088 (17 nominal + 4 + 4 for the 8-bit bus), call it ~38 with MDA wait
; states, against ~4,770 for a glyph cell. Two orders of magnitude, and it is
; the same two orders that the single-line view was buying by not drawing.
;
; So the whole 19-row grid comes back, scrolling per row, and the numbers are
; in SPEC.md 45.13.1. The short version: one row change is a 1,121-cell blit
; out of a prepared shadow (~8.9 ms) plus a 59-byte band relight, and rows
; arrive 8.3 times a second on a default-tempo module - about 4% of the
; machine, against the ~44% XT mode's mixer already spends.
;
; THE SHADOW is what makes the blit that cheap. A player never edits, so a
; pattern's 64 rows are constant text for as long as it is the current
; pattern: they are formatted ONCE into ttx_shadow as char/attribute WORDS
; (SPEC.md 45.13.2) - 256 mp_cell2txt calls, ~33 ms, once every eight seconds
; or so - and every row change after that is 19 rep movsw's out of it. The
; shadow carries TTX_HALF rows above row 0 and below row 63, so the window
; into it is `shadow + viewrow * TTX_RW * 2` with no clamp, no branch and no
; special case at the ends of a pattern.
;
; THOSE PAD ROWS ARE THE NEIGHBOURING ORDERS (SPEC.md 45.13.4), which is what
; makes the scroll CONTIGUOUS. They used to be blank, and the ends of a
; pattern were 18 rows of nothing crossing the screen every ~2.2 s of music -
; the grid emptying and refilling while the music carried straight on, which
; is what it looked like and what it was reported as. Above row 0 is the
; PREVIOUS order's pattern, rows 64-TTX_HALF..63; below row 63 is the NEXT
; order's, rows 0..TTX_HALF-1. Both are resolved once per rebuild into
; [ttx_shprev] / [ttx_shnext], and TTX_NOPAT there means "nothing plays either
; side of this" - the end of a song that does not restart at 0 - which is the
; one place a blank row is still the truth.
;
; THE BAND IS AN ATTRIBUTE, not a redraw. The shadow holds every row in the
; normal attribute; the blit lands them all, and 59 attribute bytes turn the
; middle row inverse afterwards. Nothing has to remember which row USED to be
; the band, because the blit rewrote it on the way past.
;
; AND NOTHING HERE CAN FLASH. PERFORMANCE.md Part 1's double-draw defect is
; the erase-and-letter pair; a text cell goes from its old contents to its
; final contents in a single word write, so this file gets SPEC.md 6.1's
; one-store-per-cell property for free - which is also why every field is
; written space-padded to its full width rather than erased first.
;
; WHAT THIS MAY NOT DO. After the OSAPI_FSX_MODE call every kernel drawing
; slot is off-limits until the bracket returns (SPEC.md 53.1) - they render
; DESKTOP geometry into a framebuffer that is now a text page. [trk_tx] is
; the flag that says so, and tui_msg_draw is gated on it: a status message
; set from trk_play or trk_transport while the text screen is up lands in
; [tui_msgp] and is picked up by ttx_draw_dyn's own status line.
; =============================================================================

; --- the 80x25 page ------------------------------------------------------------
TTX_COLS   equ 80                   ; FSXM_TEXT80 is 80x25 on all three
TTX_ROWS   equ 25                   ; adapters (SPEC.md 53.4); only FSI_SEG
TTX_STRIDE equ TTX_COLS * 2         ; genuinely varies - B000 on Hercules and
                                    ; MDA, B800 on the CGA/VGA family - and
                                    ; that is read out of the block, never
                                    ; assumed

; --- one pattern row: 'RR |C-2 01 A0F|...|C-2 01 A0F| RR' ----------------------
TTX_RW     equ 59                   ; characters in a row
TTX_X0     equ (TTX_COLS - TTX_RW) / 2  ; ...centred: column 10
TTX_SEPX   equ 3                    ; the first vertical rule inside the row
TTX_CHD    equ 13                   ; per-channel pitch: rule, space, 10, space
TTX_CELLX  equ 5                    ; cell text = TTX_CELLX + TTX_CHD*channel
TTX_CELLW  equ 10                   ; mp_cell2txt's pinned 'C-2 01 A0F'

; --- the rows of the screen ----------------------------------------------------
TTX_NAMEY  equ 0                    ; nameplate: TRACKER, the module title
TTX_RDY    equ 1                    ; Pos/Ptn/Row/BPM/Spd
TTX_VUY    equ 2                    ; the four volume bars
TTX_STY    equ 3                    ; the status line (SPEC.md 45.9.2)
TTX_HDRY   equ 4                    ; Channel 1..4
TTX_RULEY  equ 5                    ; ...and the rule under them
TTX_PATY   equ 6                    ; first pattern row
TTX_VIS    equ TTX_ROWS - TTX_PATY  ; 19 rows visible
TTX_HALF   equ (TTX_VIS - 1) / 2    ; 9 above the band, 9 below
TTX_BANDY  equ TTX_PATY + TTX_HALF  ; screen row 15

; the shadow carries the rows the ends of a pattern show - the neighbouring
; orders' (SPEC.md 45.13.4) - so the window into it needs no clamp: screen
; row 0 of the area is always shadow row [view], because shadow row
; (r + TTX_HALF) is pattern row r
TTX_SHROWS equ 64 + 2 * TTX_HALF
TTX_SHBYTES equ TTX_SHROWS * TTX_RW * 2
TTX_SHPAD0 equ 64 - TTX_HALF        ; the previous pattern's first shown row
TTX_SHCHUNK equ 4                   ; rows formatted per frame while a
                                    ; periodic rebuild runs, on the TICK
                                    ; clock: ~25 ms of the ~420 ms the whole
                                    ; shadow costs, against a view that moves
                                    ; 7.14 rows a SECOND
TTX_SHCHUNKF equ 1                  ; ...and on the FSXW_FRAME clock, where a
                                    ; frame is a third of the slot and 25 ms
                                    ; would not fit in it (SPEC.md 45.16).
                                    ; Same total work either way - the rebuild
                                    ; takes 1.5 s of a ~7.7 s pattern instead
                                    ; of 1.15, at a quarter of the peak
TTX_NOPAT  equ 0xFF                 ; [ttx_shprev]/[ttx_shnext]: nothing plays
                                    ; that side, so the pad is blank. Never a
                                    ; real pattern - mp_load caps P at 126

; --- attributes. These three are the whole palette, and they are the three an
; --- MDA can actually render, so they mean the same thing on Hercules, CGA and
; --- VGA text alike - the SPEC.md 39.4 discipline in its text-mode clothes ------
TTX_A_NORM equ 0x07                 ; normal
TTX_A_HI   equ 0x0F                 ; bright: headings and live readouts
TTX_A_INV  equ 0x70                 ; reverse: the nameplate and the row band

; --- CP437, which is the ROM font on every adapter that can set this mode -----
TTX_VBAR   equ 0xB3                 ; the channel rules
TTX_HBAR   equ 0xC4                 ; the rule under the header...
TTX_TEE    equ 0xC2                 ; ...and where a channel rule meets it
TTX_BLOCK  equ 0xDB                 ; a volume bar's lit cell

; cell (row, col) -> byte offset in the page
%define TTXAT(r, c) ((r) * TTX_STRIDE + (c) * 2)

; =============================================================================
; Page primitives. Each assumes ES is already the text segment and CLD is set;
; the public drawers below establish both. DI advances past what was written,
; which is what lets a line be composed out of several calls.
; =============================================================================

; ttx_put - SI = NUL string, AH = attribute, ES:DI = the first cell
ttx_put:
    push ax
    push si
.l:
    mov al, [si]
    or al, al
    jz .d
    inc si
    stosw
    jmp .l
.d:
    pop si
    pop ax
    ret

; ttx_putn - the same, space-padded to CX cells. The pad is not an erase in
;            front of the text, it is the rest of the same single pass - so a
;            field that got shorter cannot be blank for a frame.
ttx_putn:
    push ax
    push cx
    push si
.l:
    or cx, cx
    jz .d
    mov al, [si]
    or al, al
    jz .pad
    inc si
    stosw
    dec cx
    jmp .l
.pad:
    mov al, ' '                     ; CX >= 1 here: the loop above tested it
.p:
    stosw
    loop .p
.d:
    pop si
    pop cx
    pop ax
    ret

; ttx_fill - CX cells of AX (AL = char, AH = attribute) at ES:DI
ttx_fill:
    push ax
    push cx
    rep stosw
    pop cx
    pop ax
    ret

; =============================================================================
; ttx_begin - take the screen (SPEC.md 53.4). Bracket context, exclusive task.
; out: CF=0 the text page is live and [ttx_seg] names it; CF=1 refused, and
;      the caller falls back to the graphics bracket having changed nothing.
; =============================================================================
ttx_begin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [ttx_clk], FSXW_TICK   ; the tick clock until ttx_clkpick says
    mov al, TTX_SHCHUNK             ; otherwise (SPEC.md 45.16) - ttx_draw_all
    mov [ttx_chunk], al             ; runs a whole shadow build BEFORE the
    mov byte [ttx_fdiv], 1          ; pick does, and reads these
    mov byte [ttx_fcnt], 1
    push ds
    pop es                          ; the FSI block is ours (the gfx_blit4
    mov di, ttx_fsi                 ; idiom: ES:DI is the caller's own)
    mov al, FSXM_TEXT80
    call OSAPI_FSX_MODE
    jc .out
    mov ax, [ttx_fsi + FSI_SEG]     ; the ONE thing that is not a constant
    mov [ttx_seg], ax
    mov ax, 0x0100                  ; park the CRTC cursor out of sight - it
    mov cx, 0x2000                  ; is ours now, and the mode set left it
    int 0x10                        ; blinking at 0,0 (SPEC.md 53.4)
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The shadow build, in three parts (SPEC.md 45.13.2). It used to be one
; routine that formatted all 64 rows in a single frame, and PERFORMANCE.md
; Part 9's field set says what that costs on the target: 256 mp_cell2txt
; calls plus 3,776 lodsb/stosw pairs plus a 9,676-byte blank at the measured
; 1.76 us/byte, which comes to 140-330 ms depending on how many cells carry
; notes. That is not a frame, it is a Part 1 VISIBLE REDRAW, it happened
; once per pattern - every ~9 s at 125 BPM speed 7 - and it is exactly what
; the field report describes: the screen stopping for about a third of a
; second and then jumping.
;
; So the periodic build is spread: ttx_shstart claims the pattern and arms a
; cursor, ttx_shstep formats TTX_SHCHUNK rows, and ttx_draw_dyn calls it once
; a frame until the cursor runs out. The worst frame goes from ~330 ms to
; ~25 ms and nothing else changes - the blit still comes out of a shadow, and
; a row is still formatted exactly once per pattern.
;
; TTX_SHROWS is 82 rather than 64 since the pads became the neighbouring
; orders, so the whole build is 328 mp_cell2txt calls and ~420 ms - which is
; 21 frames of TTX_SHCHUNK instead of 16 and does not change the frame, only
; how many of them a rebuild occupies (~1.15 s of a ~7.7 s pattern).
;
; Two things make that safe. The cursor is a SHADOW row and starts at the one
; the view needs - shadow row [view] IS the top of the visible window - and
; WRAPS, so a Dxx break or a position jump that lands mid-pattern fills the
; visible window first rather than counting up to it, and a pattern flip,
; where that window is mostly the previous order's tail, fills the pad first;
; and the cursor advances at TTX_SHCHUNK rows per frame against a view that
; advances 7.14 rows a SECOND, so it can never be overtaken.
;
; ttx_shbuild is the synchronous whole thing, and the only caller is
; ttx_draw_all: the bracket has just set the video mode, there are no pixels
; on screen to stop updating, and paying it there is what lets the first
; frame after F be complete. It is also the only place that blanks, and the
; blank is not redundant with the build: a module-less grid formats no rows
; at all.
; =============================================================================

; =============================================================================
; ttx_clkpick - pick the frame clock, once per bracket (SPEC.md 45.16).
;
; THIS USED TO BE A RETRACE PROBE AND THE PROBE WAS NOT THE PART THAT WAS
; WRONG. fsx_wait's retrace path is a poll, so it was measured rather than
; assumed - eight waits timed against the tick, the retrace clock taken only
; between one and seven ticks, zero refused because eight real retraces
; cannot fit in one tick. All of that was sound and all of it answered the
; wrong question: not "is the retrace clock faster" (it is - 29.1 fps
; measured in the field against 18.2) but "what does POLLING it cost the
; worker". A busy-wait is indistinguishable from work to a round-robin
; scheduler, so the drawing side took its half of the machine whether it
; needed it or not. Measured on a cycle-accurate 8088: fsx_insync + fsx_wait
; were 28.8% of the machine, the mixer got 35% against the 47% it gets
; windowed, and the card was fed 4,808 bytes a second against the 5,533 it
; plays - the ring's three-second cushion drained over the first half-minute
; of fullscreen and every second after that dropped audio.
;
; FSXW_FRAME (SPEC.md 53.5.1) is faster AND free: FSXF_FASTTICK is armed by
; trk_fs_enter, so IRQ0 arrives FSX_SUBTICK times a tick and this returns
; 54.6 times a second - quicker than a Hercules' 50 Hz retrace, within 10% of
; a CGA's 60 - and it waits by YIELDING, so the frame time we do not use goes
; to the mixer instead of to a port poll. There is nothing left to measure:
; the rate is a crystal divided by a constant, it cannot fail the way a
; status port can, and it is EVENLY SPACED, which the old clock never was -
; [tui_fpt] now divides every tick into exactly FSX_SUBTICK equal steps
; instead of into however many frames the last tick happened to fit, which is
; SPEC.md 45.15.2's sub-tick interpolation finally getting the even clock it
; was written against.
;
; [ttx_fdiv] is still frames per tick, and it is now known rather than
; estimated - which is what sizes a rebuild's per-frame chunk. The VU needles
; hang off the system tick and not off this (SPEC.md 45.16.1).
; =============================================================================
ttx_clkpick:
    push ax
    mov byte [ttx_clk], FSXW_FRAME  ; the PIT, not the CRTC (SPEC.md 53.5.1)
%ifdef TTXNOFAST
    mov byte [ttx_fdiv], 1          ; no sub-tick: one frame per tick, and
    mov byte [ttx_fcnt], 1          ; FSXW_FRAME then waits on a [sch_subs]
%else                               ; that only moves once a tick
    mov byte [ttx_fdiv], FSX_SUBTICK
    mov byte [ttx_fcnt], FSX_SUBTICK
%endif
    mov al, TTX_SHCHUNKF            ; one row a frame: TTX_SHCHUNK's four are
    mov [ttx_chunk], al             ; ~25 ms and a frame's slot is now 18
    pop ax
    ret

ttx_shbuild:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov di, ttx_shadow
    mov cx, TTX_SHROWS * TTX_RW
    mov ax, (TTX_A_NORM << 8) | ' '
    rep stosw
    call ttx_shstart
.l:
    cmp byte [ttx_shbusy], 0
    je .done
    call ttx_shstep
    jmp short .l
.done:
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ttx_shstart - claim the AUDIBLE pattern, resolve the two neighbouring
;               orders and arm the cursor. No formatting.
;
; The pattern is claimed at the START, not at the end: ttx_draw_dyn's change
; test would otherwise refire on every frame of the build. It also closes an
; old race - the position can move mid-build, and recording it afterwards
; marked a mixed shadow as current. Recorded first, a mid-build move is caught
; by the same test on the next frame and simply restarts.
;
; It is [tui_apat] and not [mp_pattern] because the whole screen shows what is
; being HEARD (SPEC.md 45.15), and the shadow has to hold the pattern the band
; is inside or the grid would be built for one pattern and indexed for another.
;
; The neighbours are ORDERS, not patterns, so [tui_apos] is claimed too: two
; orders naming the same pattern move the pads without moving the pattern, and
; [ttx_shpos] is what lets ttx_draw_dyn see it. [mp_ploop] likewise - in
; pattern-loop mode the pattern's own ends meet.
;
; AND AT A SEQUENTIAL BOUNDARY IT MOVES 18 ROWS RATHER THAN FORMATTING THEM
; (SPEC.md 45.13.5). The rows the new screen needs are already in the shadow,
; at different indices: this pattern's tail becomes the next shadow's pad
; ABOVE, and the pad BELOW - which already holds the next pattern's first nine
; rows - becomes its first nine content rows. That is exactly the window the
; view is sitting on when a boundary passes, so carrying it costs one rep
; movsw and the rebuild then formats only rows BELOW the screen.
;
; It was reported from the field before it was noticed here, and in the right
; words: "a whole line of replacements going down the first column". That is
; the format cursor walking the visible window, one row per frame, rewriting
; rows the user is reading. The carry is what puts those replacements
; off-screen where they belong - the same conclusion as "make the buffer twice
; the screen", reached without a second buffer, because the shadow already
; holds the rows.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; ttx_shvalid - CF=0 when the shadow already holds what the screen wants
;
; Factored out of ttx_draw_dyn so the FRAME test and the ENTRY test (SPEC.md
; 45.13.6) cannot drift apart: the whole value of skipping a build is that the
; two agree about what "still current" means.
;
; in:  tui_sync has run (this reads tui_a*). out: CF, AL spent.
; -----------------------------------------------------------------------------
ttx_shvalid:
    cmp byte [ttx_shok], 0          ; a position move can change the pattern,
    je .no                          ; and a pattern is a whole new shadow
    mov al, [tui_apat]
    cmp al, [ttx_shpat]
    jne .no
    mov al, [mp_ploop]              ; P makes the pattern its own neighbour
    cmp al, [ttx_shloop]            ; both ways (SPEC.md 45.13.4)
    jne .no
    mov al, [tui_apos]              ; the pads are the ORDERS either side, so
    cmp al, [ttx_shpos]             ; two orders naming the same pattern move
    jne .no                         ; them without moving the pattern. A Bxx
    clc                             ; needs no guard here: a stamp carries the
    ret                             ; position its row belonged to, never the
.no:                                ; parked one (SPEC.md 45.15)
    stc
    ret

ttx_shstart:
    push ax
    push bx                         ; mp_order decodes through it
    push cx
    push si
    push di
    push es
    mov al, [ttx_shpat]             ; bank what the shadow HOLDS before the
    mov [ttx_ocpat], al             ; claim below overwrites it - the carry
    mov al, [ttx_shnext]            ; test needs both
    mov [ttx_ocnext], al
    mov al, [ttx_shok]
    mov [ttx_ocok], al
    mov al, [tui_apat]
    mov [ttx_shpat], al
    mov al, [tui_apos]
    mov [ttx_shpos], al
    mov al, [mp_ploop]
    mov [ttx_shloop], al
    mov byte [ttx_shprev], TTX_NOPAT
    mov byte [ttx_shnext], TTX_NOPAT
    mov byte [ttx_shok], 0
    mov byte [ttx_shbusy], 1
    mov byte [ttx_sleft], TTX_SHROWS
    mov al, [tui_cvr]               ; shadow row [view] IS the top of the
    mov [ttx_srow], al              ; visible window, so the cursor starts on
                                    ; what is on screen - which at a pattern
                                    ; flip is mostly the pad it just gained
    cmp byte [mp_loaded], 0
    je .nomod
    cmp byte [mp_ploop], 0
    je .song
    mov al, [ttx_shpat]             ; pattern loop: the pattern never leaves,
    mov [ttx_shprev], al            ; so its own row 63 is followed by its own
    mov [ttx_shnext], al            ; row 0 and the scroll closes on itself
    jmp short .out
.song:
    mov al, [tui_apos]
    inc al
    cmp al, [mp_songlen]            ; past the last order the song restarts,
    jb .nx                          ; and mp_nextrow's restart position is
    mov al, [mp_restart]            ; where it goes
.nx:
    call mp_order
    mov [ttx_shnext], al
    mov al, [tui_apos]
    or al, al
    jnz .pv
    cmp byte [mp_restart], 0        ; position 0 is reached from the last order
    jne .out                        ; only when the song restarts THERE;
    mov al, [mp_songlen]            ; otherwise nothing precedes it, and a
.pv:                                ; blank pad is the truth rather than a gap
    dec al
    call mp_order
    mov [ttx_shprev], al
    jmp short .carry                ; the ONLY path that can carry: a song
                                    ; walking its order list. A pattern loop
                                    ; never changes pattern (so never rebuilds)
                                    ; and position 0 with no restart there has
                                    ; nothing above it to carry from
.nomod:
    mov byte [ttx_shbusy], 0        ; no module: an empty grid IS the truth,
    mov byte [ttx_shok], 1          ; and the blank already wrote it
    jmp short .out
; --- the sequential carry (SPEC.md 45.13.5) ----------------------------------
; Valid only when this build FOLLOWS the one in the shadow: the shadow was
; complete, the pattern now playing is the one it named as next, and the
; pattern before this one is the one it held. Anything else - a position jump,
; a Bxx, a fresh bracket - formats from scratch as before.
.carry:
    cmp byte [ttx_ocok], 0
    je .out
    mov al, [ttx_shpat]
    cmp al, [ttx_ocnext]            ; ...we are the pattern it called next
    jne .out
    mov al, [ttx_shprev]
    cmp al, [ttx_ocpat]             ; ...and it is the pattern we call prev
    jne .out
    push ds
    pop es
    cld
    mov si, ttx_shadow + (64 * TTX_RW * 2)  ; rows 64..81: this pattern's tail
    mov di, ttx_shadow                      ; and the next one's head, which
    mov cx, 2 * TTX_HALF * TTX_RW           ; become rows 0..17 - the window
    rep movsw                               ; the view is sitting on
    mov byte [ttx_srow], 2 * TTX_HALF       ; so the cursor starts BELOW them
    mov byte [ttx_sleft], TTX_SHROWS - 2 * TTX_HALF
%ifdef TRKLOG
    mov al, TLOGF_CARRY             ; so a capture says which of the two a
    call tlog_flag                  ; boundary took, not just that one happened
%endif
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ttx_shstep - format up to TTX_SHCHUNK rows at the cursor; clears
;              [ttx_shbusy] and sets [ttx_shok] on the last of TTX_SHROWS
; -----------------------------------------------------------------------------
ttx_shstep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov al, [ttx_chunk]             ; sized to the frame clock (45.16)
    mov [ttx_sbudget], al
.r:
    cmp byte [ttx_shbusy], 0
    je .done
    mov bl, [ttx_srow]
    call ttx_shline                 ; -> ttx_line, TTX_RW ascii
    mov al, [ttx_srow]
    xor ah, ah
    mov dx, TTX_RW * 2
    mul dx                          ; shadow row * rowbytes; 9,558 max, so DX
    add ax, ttx_shadow              ; is 0 and the product fits AX
    mov di, ax
    mov si, ttx_line
    mov cx, TTX_RW
    mov ah, TTX_A_NORM              ; lodsb leaves AH alone: one attribute
.w:                                 ; rides the whole row
    lodsb
    stosw
    loop .w
    mov al, [ttx_srow]              ; does the screen owe a blit for it? Only
    sub al, [tui_cvr]               ; if the row just formatted is INSIDE the
    jnc .vis                        ; window - the cursor walks all 82 shadow
    add al, TTX_SHROWS              ; rows and 19 of them are visible, so
.vis:                               ; forcing one per step redrew an unchanged
    cmp al, TTX_VIS                 ; screen 77% of the time (SPEC.md 45.16.1)
    jae .novis
    mov byte [ttx_vdirty], 1
.novis:
    mov al, [ttx_srow]
    inc al
    cmp al, TTX_SHROWS              ; the cursor wraps over the whole SHADOW,
    jb .wrapped                     ; pads included - they are rows like any
    xor al, al                      ; other now, and 82 is not a power of two
.wrapped:
    mov [ttx_srow], al
    dec byte [ttx_sleft]
    jnz .budget
    mov byte [ttx_shbusy], 0
    mov byte [ttx_shok], 1
    jmp short .done
.budget:
    dec byte [ttx_sbudget]
    jnz .r
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ttx_shline - shadow row BL (0..TTX_SHROWS-1) -> ttx_line. The whole of
;              SPEC.md 45.13.4: which of the three patterns a shadow row
;              belongs to, and which of its rows.
;
; Shadow row s is pattern row s - TTX_HALF of the CURRENT pattern while that
; lands in 0..63. A row before the first of those is the PREVIOUS order's
; tail, one after the last is the NEXT order's head, and both carry their own
; row numbers - so the numbers down the edges run ..3E 3F 00 01.. across a
; boundary and say where it is with no rule, no colour and no branch in the
; blit.
; -----------------------------------------------------------------------------
ttx_shline:
    push ax
    push bx
    mov al, bl
    mov ah, [ttx_shpat]
    cmp al, TTX_HALF
    jb .above
    sub al, TTX_HALF
    cmp al, 64
    jb .row
    sub al, 64                      ; below row 63: the next order's head,
    mov ah, [ttx_shnext]            ; rows 0..TTX_HALF-1
    jmp short .row
.above:
    add al, TTX_SHPAD0              ; above row 0: the previous order's tail,
    mov ah, [ttx_shprev]            ; rows TTX_SHPAD0..63
.row:
    cmp ah, TTX_NOPAT
    je .blank
    mov [ttx_pat], ah
    mov bl, al
    call ttx_rowtext
    jmp short .out
.blank:
    call ttx_rowblank
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ttx_rowblank - TTX_RW spaces in ttx_line: the end of a song that does not
;                restart, where there genuinely is no row to show
; -----------------------------------------------------------------------------
ttx_rowblank:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov di, ttx_line
    mov cx, TTX_RW
    mov al, ' '
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ttx_rowtext - row BL (0..63) of pattern [ttx_pat] -> ttx_line as TTX_RW
; ascii characters. The row number is on both edges the FT2 way, and a cell
; the decoder refuses is left blank rather than guessed at.
;
; The pattern is a bss byte rather than a register for the same reason the row
; is, and it is [ttx_pat] rather than [mp_pattern] because 18 of a rebuild's
; 82 rows belong to the pattern either side of it (SPEC.md 45.13.4).
;
; The row goes through a bss byte and not BL because mp_cell2txt spends every
; general register - it decodes through BX and CX and returns with ES = DS.
; -----------------------------------------------------------------------------
ttx_rowtext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [ttx_row], bl
    push ds
    pop es
    cld
    mov di, ttx_line
    mov cx, TTX_RW
    mov al, ' '
    rep stosb
    mov di, ttx_line + TTX_SEPX     ; five rules: four channel lefts and the
    mov cx, 5                       ; block's right edge, TTX_SEPX + 4*TTX_CHD
.sep:
    mov byte [di], TTX_VBAR
    add di, TTX_CHD
    loop .sep
    mov al, [ttx_row]
    mov di, ttx_line
    call tui_hexb                   ; the left row number...
    mov al, [ttx_row]
    mov di, ttx_line + TTX_RW - 2
    call tui_hexb                   ; ...and the right one
    mov byte [ttx_chn], 0
.cl:
    mov al, [ttx_pat]
    xor ah, ah
    mov di, ax
    mov al, [ttx_row]
    mov ah, [ttx_chn]
    call mp_cell2txt                ; -> mp_celltxt; clobbers everything
    jc .nx
    mov al, [ttx_chn]
    mov ah, TTX_CHD
    mul ah                          ; AX = channel * pitch
    add ax, ttx_line + TTX_CELLX
    mov di, ax
    mov si, mp_celltxt
    push ds
    pop es
    mov cx, TTX_CELLW
    cld
    rep movsb
.nx:
    inc byte [ttx_chn]
    cmp byte [ttx_chn], 4
    jb .cl
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ttx_blit - the whole 19-row window, out of the shadow, in 19 row copies.
; This is the frame's entire pattern cost: 1,121 word stores, ~8.9 ms. Nothing
; has to remember which row USED to be the band, because the blit rewrites it
; on the way past.
;
; THE BAND IS LANDED INVERSE BY THE SAME PASS, and that is a flicker fix
; rather than a tidy-up (SPEC.md 45.13.3). It used to be a second pass - 59
; attribute bytes written after all 19 rows had gone down in TTX_A_NORM - so
; for the ~9 ms of the copy the band row was on screen drawn NORMAL. On a
; 1bpp text screen TTX_A_INV is black-on-white and TTX_A_NORM is
; white-on-black, so that intermediate state is the white bar going BLACK, and
; the field reported exactly that: "the highlighted line is flicking black,
; sometimes". PERFORMANCE.md Part 1's double-draw defect, in text-mode
; clothes, in the one place on this screen that still had two passes - and
; SPEC.md 45.15.1's interpolation is what made it visible, by taking the row
; changes from 2.4 a second to 7.
;
; It is also CHEAPER than what it replaced: one lodsw/stosw pair per cell
; against a rep movsw plus a read-modify-write of every attribute byte.
; =============================================================================
ttx_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [tui_cvr]               ; shadow row [view] IS screen row 0 of the
    xor ah, ah                      ; area: pattern row r lives at r + HALF and
    mov dx, TTX_RW * 2              ; the area starts at view - HALF
    mul dx
    add ax, ttx_shadow
    mov si, ax
    mov ax, [ttx_seg]
    mov es, ax
    mov di, TTXAT(TTX_PATY, TTX_X0)
    mov bx, TTX_VIS
    cld
.r:
    push si
    push di
    mov cx, TTX_RW
    cmp bx, TTX_VIS - TTX_HALF      ; the BAND row is landed already inverse,
    jne .plain                      ; in the same pass (SPEC.md 45.13.2)
.band:
    lodsw
    mov ah, TTX_A_INV
    stosw
    loop .band
    jmp short .rdone
.plain:
    rep movsw
.rdone:
    pop di
    pop si
    add si, TTX_RW * 2
    add di, TTX_STRIDE
    dec bx
    jnz .r
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The lines above the grid. Each is one padded pass over its own field, so
; none of them can be caught half-written (PERFORMANCE.md Part 1).
; =============================================================================

; --- row 0: TRACKER, the module title, and what the audio is doing -------------
ttx_name:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [ttx_seg]
    mov es, ax
    cld
    mov di, TTXAT(TTX_NAMEY, 0)
    mov cx, TTX_COLS
    mov ax, (TTX_A_INV << 8) | ' '
    call ttx_fill
    mov ah, TTX_A_INV
    mov di, TTXAT(TTX_NAMEY, 2)
    mov si, ttx_s_name
    call ttx_put
    mov si, mp_title                ; the title is hostile blob bytes, but
    cmp byte [mp_loaded], 0         ; mp_load already NUL-terminated it inside
    jne .title                      ; its own 21-byte buffer
    mov si, ttx_s_nomod
.title:
    mov di, TTXAT(TTX_NAMEY, 12)
    mov cx, 21
    mov ah, TTX_A_INV
    call ttx_putn
    call ttx_rate_cell              ; the rate, STAMPED - it used to be the
                                    ; constant 'XT 5500 Hz' and the comment
                                    ; here said why that was a fact: XT mode
                                    ; IS 5,500 Hz and this screen only existed
                                    ; while XT mode was on. SPEC.md 45.13.7
                                    ; retired the second half of that sentence
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ttx_rate_stamp - put the LIVE rate into the nameplate's own string
;
; It was a CONSTANT, and that was correct while the text screen was XT mode's
; and nothing else: XT mode IS 5,500 Hz (SPEC.md 45.9). SPEC.md 45.13.7 made
; the surface a pick, so this screen now runs at 22 or 44 kHz with XT mode
; off - and a header reading `XT 5500 Hz` over a 22,050 Hz stream is the app
; lying about its own state, which is the one thing SPEC.md 45.8's honest
; degradations are all shaped to avoid. K moving the rate under TRKLOG was the
; SAME defect arriving first (docs/FIELD-NOTES.md 16) and had its own bench
; stamper; there is one stamper now, in the shipped build, because the shipped
; build has the problem too.
;
; [mp_mixrate], not the Rate menu's pick: what the header owes is the rate the
; stream was OPENED at, and a pick takes effect at the next Play. mp_init
; seeds it 11,000, so this never reads a zero.
;
; Preserves everything. Leading zeros become spaces and so does the `XT `
; prefix when the mode is off, so the field keeps its width in every state and
; the pen behind it never moves.
; -----------------------------------------------------------------------------
ttx_rate_stamp:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, ttx_s_xtrate
    mov ax, '  '                    ; `XT ` is a FACT only while the mode is
    cmp byte [mp_xt], 0             ; on - off, it is a claim about the rate
    je .pfx                         ; that the digits then contradict
    mov ax, 'XT'
.pfx:
    mov [di], al
    mov [di + 1], ah
    mov di, ttx_s_xtrate + 7        ; the last of the five digits
    mov ax, [mp_mixrate]
    mov cx, 5
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .d
    mov di, ttx_s_xtrate + 3        ; '05500' is not a five-digit rate; four
    mov cx, 4                       ; of the five may blank, never the last
.z:
    cmp byte [di], '0'
    jne .out
    mov byte [di], ' '
    inc di
    loop .z
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ttx_rate_cell - stamp it and draw that one cell. ES = [ttx_seg] on entry from
; ttx_name; ttx_draw_dyn's delta call sets its own. Preserves everything.
; -----------------------------------------------------------------------------
ttx_rate_cell:
    push ax
    push si
    push di
    call ttx_rate_stamp
    mov di, TTXAT(TTX_NAMEY, TTX_COLS - 12)
    mov si, ttx_s_xtrate
    mov ah, TTX_A_INV
    call ttx_put
    mov ax, [mp_mixrate]
    mov [ttx_lrate], ax
    pop di
    pop si
    pop ax
    ret

; --- row 1: Pos / Ptn / Row / BPM / Spd ----------------------------------------
ttx_readout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, ttx_line
    mov si, ttx_s_pos
    call tui_scpy
    mov al, [tui_apos]
    call tui_hexb
    mov si, ttx_s_slash
    call tui_scpy
    mov al, [mp_songlen]            ; the LAST order, FT2's own reading - and
    or al, al                       ; 0 with no module loaded, where a bare
    jz .len                         ; dec would read '/FF'
    dec al
.len:
    call tui_hexb
    mov si, ttx_s_ptn
    call tui_scpy
    mov al, [tui_apat]
    call tui_hexb
    mov si, ttx_s_rowl
    call tui_scpy
    mov al, [tui_arow]
    call tui_hexb
    mov si, ttx_s_bpm
    call tui_scpy
    mov al, [tui_abpm]
    call tui_dec3
    mov si, tui_nbuf
    call tui_scpy
    mov si, ttx_s_spd
    call tui_scpy
    mov al, [tui_aspd]
    call tui_hexb
    mov byte [di], 0
    mov ax, [ttx_seg]
    mov es, ax
    cld
    mov di, TTXAT(TTX_RDY, TTX_X0)
    mov si, ttx_line
    mov cx, TTX_RW
    mov ah, TTX_A_HI
    call ttx_putn
    mov al, [tui_apos]              ; bank what this frame drew
    mov [tui_lpos], al
    mov al, [tui_apat]
    mov [tui_lpat], al
    mov al, [tui_arow]
    mov [tui_lrow], al
    mov al, [tui_abpm]
    mov [tui_lbpm], al
    mov al, [tui_aspd]
    mov [tui_lspd], al
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- row 3: the status line. Same string [tui_msgp] carries everywhere else,
; --- so SPEC.md 45.9.2's transport legend follows the music here too ----------
ttx_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, [tui_msgp]
    or si, si
    jnz .have
    call tui_hintp
.have:
    mov ax, [ttx_seg]
    mov es, ax
    cld
    mov di, TTXAT(TTX_STY, TTX_X0)
    mov cx, TTX_RW
    mov ah, TTX_A_NORM
    call ttx_putn
    mov ax, [tui_msgp]
    mov [ttx_lmsg], ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- rows 4 and 5: the channel header and its rule. Drawn once per bracket -----
ttx_header:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [ttx_seg]
    mov es, ax
    cld
    mov di, TTXAT(TTX_HDRY, TTX_X0)
    mov si, ttx_s_rw
    mov ah, TTX_A_HI
    call ttx_put
    mov byte [ttx_chn], 0
.c:
    mov al, [ttx_chn]
    mov ah, TTX_CHD
    mul ah
    add ax, TTX_X0 + TTX_CELLX
    shl ax, 1                       ; a cell is a word (8086: shift by 1 only)
    add ax, TTXAT(TTX_HDRY, 0)
    mov di, ax
    mov si, ttx_s_chan
    mov ah, TTX_A_HI
    call ttx_put
    mov al, [ttx_chn]
    add al, '1'
    mov ah, TTX_A_HI
    stosw
    inc byte [ttx_chn]
    cmp byte [ttx_chn], 4
    jb .c
    mov di, TTXAT(TTX_HDRY, TTX_X0 + TTX_RW - 2)
    mov si, ttx_s_rw
    mov ah, TTX_A_HI
    call ttx_put
    mov di, TTXAT(TTX_RULEY, TTX_X0)
    mov cx, TTX_RW
    mov ax, (TTX_A_NORM << 8) | TTX_HBAR
    call ttx_fill
    mov di, TTXAT(TTX_RULEY, TTX_X0 + TTX_SEPX)
    mov cx, 5
    mov al, TTX_TEE
.t:
    mov [es:di], al                 ; where a channel rule meets the rule
    add di, TTX_CHD * 2
    loop .t
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Row 2: the four volume bars, aligned with the four pattern columns.
;
; ttx_vuframe draws the brackets and the MUTE flags - a mute is a change of
; state, not of level, so it belongs here and not in the per-frame path - and
; zeroes the delta baseline, exactly as tui_el_scopes does for the graphics
; bars (SPEC.md 45.12).
; =============================================================================
ttx_vuframe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [ttx_seg]
    mov es, ax
    cld
    mov byte [ttx_chn], 0
.c:
    mov al, [ttx_chn]
    mov ah, TTX_CHD
    mul ah
    add ax, TTX_X0 + TTX_CELLX - 1
    shl ax, 1
    add ax, TTXAT(TTX_VUY, 0)
    mov di, ax
    mov ax, (TTX_A_NORM << 8) | '['
    stosw
    mov bl, [ttx_chn]
    xor bh, bh
    mov byte [ttx_vuc + bx], 0      ; the delta baseline, dropped with the
    cmp byte [mp_mute + bx], 0      ; pixels it described
    je .live
    mov si, ttx_s_mute
    mov cx, TTX_CELLW
    mov ah, TTX_A_NORM
    call ttx_putn
    jmp short .close
.live:
    mov cx, TTX_CELLW
    mov ax, (TTX_A_NORM << 8) | ' '
    call ttx_fill
.close:
    mov ax, (TTX_A_NORM << 8) | ']'
    stosw
    inc byte [ttx_chn]
    cmp byte [ttx_chn], 4
    jb .c
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ttx_vu - the levels, delta-drawn: a rising bar writes its growth, a falling
;          one erases its shrinkage, a steady one costs nothing. A muted
;          channel is skipped whole - its cells say MUTE and mp_chvol is 0
;          there anyway, so there is nothing for the needle to say.
ttx_vu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [ttx_seg]
    mov es, ax
    cld
    mov byte [ttx_chn], 0
.c:
    mov bl, [ttx_chn]
    xor bh, bh
    cmp byte [mp_mute + bx], 0
    jne .next
    call tui_vu_step                ; AL = the decayed level 0..64; the needle
    xor ah, ah                      ; rule has one home (SPEC.md 45.12)
    mov cx, TTX_CELLW
    mul cx
    mov cl, 6
    shr ax, cl                      ; level * 10 / 64 -> 0..10 cells
    mov dl, al
    mov bl, [ttx_chn]
    xor bh, bh
    mov dh, [ttx_vuc + bx]
    cmp dl, dh
    je .next
    mov [ttx_vuc + bx], dl
    mov al, [ttx_chn]
    mov ah, TTX_CHD
    mul ah
    add ax, TTX_X0 + TTX_CELLX
    shl ax, 1
    add ax, TTXAT(TTX_VUY, 0)
    mov di, ax
    mov al, dl
    mov ah, dh
    cmp al, ah
    jb .fall
    sub al, ah                      ; rising: light [old .. new-1]
    mov cl, al
    xor ch, ch
    mov al, ah
    xor ah, ah
    shl ax, 1
    add di, ax
    mov ax, (TTX_A_NORM << 8) | TTX_BLOCK
    call ttx_fill
    jmp short .next
.fall:
    mov cl, ah                      ; falling: blank [new .. old-1]
    sub cl, al
    xor ch, ch
    xor ah, ah
    shl ax, 1
    add di, ax
    mov ax, (TTX_A_NORM << 8) | ' '
    call ttx_fill
.next:
    inc byte [ttx_chn]
    cmp byte [ttx_chn], 4
    jb .c
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ttx_draw_all - the whole screen. Bracket entry, and nothing else calls it.
; The shadow is invalidated rather than trusted: a load can have happened
; between two brackets (Open is windowed, SPEC.md 53.7) and the shadow has no
; way to have heard about it.
; =============================================================================
ttx_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tui_sync                   ; the position being HEARD (SPEC.md 45.15)
    mov ax, [ttx_seg]
    mov es, ax
    cld
    xor di, di
    mov cx, TTX_COLS * TTX_ROWS
    mov ax, (TTX_A_NORM << 8) | ' '
    call ttx_fill
    call ttx_shvalid                ; ...but the SHADOW is package bss, so it
    mov byte [ttx_shbusy], 0        ; survived the last bracket (SPEC.md
    jnc .shkeep                     ; 45.13.6): if it still holds this pattern,
    mov byte [ttx_shok], 0          ; at this order, in this loop mode, then
.shkeep:                            ; 328 mp_cell2txt calls are already done
    call ttx_name
    call ttx_header
    call ttx_vuframe
    call ttx_readout
    call ttx_status
    call tui_viewrow                ; before the build: ttx_shstart's cursor
    cmp byte [ttx_shok], 0          ; starts at the visible window
    jne .shdone
    call ttx_shbuild
.shdone:
    call ttx_blit
    mov al, [tui_cvr]
    mov [tui_lcvr], al
    mov al, [mp_playing]
    mov [tui_lplay], al
    mov ax, [mp_mute]
    mov [tui_lmute], ax
    mov ax, [mp_mute + 2]
    mov [tui_lmute + 2], ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ttx_draw_dyn - one frame, change-driven, called once per OSAPI_FSX_WAIT.
; Nothing here redraws what did not move (PERFORMANCE.md Part 5 rule 1); the
; steady-state cost of a playing frame is four comparisons and the bars.
; =============================================================================
ttx_draw_dyn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
%ifdef TRKLOG
    call tlog_fstart                ; this frame's span starts here
%endif
%ifdef TTXNOALL
    jmp .out                        ; the TRUE drawing ceiling (PERFORMANCE.md
%endif                              ; Set 67): TTXNODRAW takes out the grid
                                    ; BLIT and nothing else, and the blit is
                                    ; under half of what this screen costs -
                                    ; the readouts, the VU needles, the shadow
                                    ; rebuild and tui_sync are the rest. A
                                    ; ceiling that only removes the blit is
                                    ; not the ceiling on DRAWING, and reading
                                    ; it as one is how Set 66 concluded that
                                    ; no drawing change could help
    call tui_sync                   ; the position being HEARD (SPEC.md 45.15):
                                    ; everything below quotes tui_a*, so the
                                    ; grid, the readout and the needles all
                                    ; move with the music rather than with the
                                    ; mixer that is seconds ahead of it
    mov al, [mp_playing]            ; the transport, from wherever it changed -
    cmp al, [tui_lplay]             ; a key, a song end, an F00, the SPEC.md
    je .noxn                        ; 45.2 watchdog. trk_transport writes the
    mov [tui_lplay], al             ; legend into [tui_msgp] and parks the view
    call trk_transport              ; row; the status line below spends it
.noxn:
    mov ax, [mp_mixrate]            ; ...and the nameplate's rate cell with it:
    cmp ax, [ttx_lrate]             ; Play opens the stream INSIDE the bracket,
    je .norate                      ; so a header stamped at ttx_name would
    push es                         ; otherwise stand at whatever the rate was
    mov ax, [ttx_seg]               ; before it. One cell, and only when the
    mov es, ax                      ; value moved (SPEC.md 45.13.1)
    call ttx_rate_cell
    pop es
.norate:
    cmp byte [ttx_shbusy], 0        ; a spread rebuild continues before
    jne .step                       ; anything looks at the pattern again
    call ttx_shvalid
    jnc .nosh
.rebuild:
    call ttx_shstart                ; TTX_SHCHUNK rows a frame from here on,
%ifdef TRKLOG
    mov al, TLOGF_REBUILD
    call tlog_flag
%endif
    cmp byte [ttx_shbusy], 0        ; never all TTX_SHROWS in one frame
    je .nosh                        ; (PERFORMANCE.md Part 1: this was a
                                    ; visible redraw)
.step:
    call ttx_shstep                 ; ...which sets [ttx_vdirty] itself, and
%ifdef TRKLOG                       ; only for a row that is on screen
    mov al, TLOGF_STEP
    call tlog_flag
%endif
.nosh:
    call tui_viewrow                ; [tui_cvr]: the playing row, or the
    cmp byte [ttx_vdirty], 0        ; Up/Down scroll row while stopped
    jne .rows
    mov al, [tui_cvr]
    cmp al, [tui_lcvr]
    je .norows
.rows:
    mov byte [ttx_vdirty], 0
%ifdef TTXPAGE
    dec byte [ttx_pgn]              ; the PAGE-FLIP proxy (PERFORMANCE.md Set
    jnz .norows                     ; 66): blit once every TTX_VIS row changes
    mov byte [ttx_pgn], TTX_VIS     ; instead of once per row - the window
%endif                              ; jumping a page rather than sliding a row.
                                    ; It does NOT move the band in between, so
                                    ; it is a LOWER BOUND on the real thing's
                                    ; cost by 59 attribute writes a row
%ifndef TTXNODRAW
    call ttx_blit
%endif
%ifdef TRKLOG
    mov al, TLOGF_BLIT
    call tlog_flag
%endif
    mov al, [tui_cvr]
    mov [tui_lcvr], al
.norows:
    mov al, [tui_apos]
    cmp al, [tui_lpos]
    jne .ro
    mov al, [tui_apat]
    cmp al, [tui_lpat]
    jne .ro
    mov al, [tui_arow]
    cmp al, [tui_lrow]
    jne .ro
    mov al, [tui_abpm]
    cmp al, [tui_lbpm]
    jne .ro
    mov al, [tui_aspd]
    cmp al, [tui_lspd]
    je .noro
.ro:
    call ttx_readout
.noro:
    mov ax, [mp_mute]
    cmp ax, [tui_lmute]
    jne .mu
    mov ax, [mp_mute + 2]
    cmp ax, [tui_lmute + 2]
    je .nomu
.mu:
    mov ax, [mp_mute]
    mov [tui_lmute], ax
    mov ax, [mp_mute + 2]
    mov [tui_lmute + 2], ax
    call ttx_vuframe
.nomu:
    call OSAPI_GET_TICKS            ; the needles fall per CALL, so they are
    cmp ax, [ttx_vutick]            ; stepped once per TICK rather than once
    je .novu                        ; per frame: exactly 18.2 a second on any
    mov [ttx_vutick], ax            ; clock, where a divider taken from the
    call ttx_vu                     ; probe was 1 against a measured 29 fps
.novu:                              ; in the field (SPEC.md 45.16.1)
%ifdef TRKLOG
    call tlog_frame                 ; the log's status line is live, so it is
  %ifndef TTXQSTAT                  ; redrawn every frame: the [ttx_lmsg]
    call ttx_status                 ; pointer compare cannot see digits change
    jmp short .out                  ; inside one buffer
  %endif                            ; ...and TTXQSTAT takes that back, because
.msg:                               ; it is the LOG's cost and not the app's:
%endif                              ; the shipped build redraws this line on a
                                    ; message CHANGE, so a capacity measurement
                                    ; taken with it live prices ttx_putn at 59
                                    ; characters 54.6 times a second that
                                    ; TRACKER.O88 never spends (PERFORMANCE.md
                                    ; Set 66). Bench-only, like everything else
                                    ; behind TRKLOG
    mov ax, [tui_msgp]
    cmp ax, [ttx_lmsg]
    je .out
    call ttx_status
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Strings
; =============================================================================
ttx_s_name:   db 'TRACKER', 0
ttx_s_nomod:  db 'no module loaded', 0
ttx_s_xtrate: db 'XT 00000 Hz', 0   ; STAMPED per paint in every build now
                                    ; (SPEC.md 45.13.7): the rate is not a
                                    ; constant once the surface is a pick.
                                    ; TTX_COLS - 12 leaves room for the fifth
                                    ; digit, which 44,100 needs
ttx_s_pos:    db 'Pos ', 0
ttx_s_slash:  db '/', 0
ttx_s_ptn:    db '  Ptn ', 0
ttx_s_rowl:   db '  Row ', 0
ttx_s_bpm:    db '  BPM ', 0
ttx_s_spd:    db '  Spd ', 0
ttx_s_rw:     db 'Rw', 0
ttx_s_chan:   db 'Channel ', 0
ttx_s_mute:   db '   MUTE', 0

; =============================================================================
; .bss (the TRKB/TRKW/TRKBUF accumulator tracker.asm defines; every name is an
; equ offset from os88_image_end)
; =============================================================================
    TRKW ttx_seg                    ; the text page's segment, out of FSI_SEG
                                    ; - B000 on Hercules and MDA, B800 on the
                                    ; CGA/VGA family. Read, never assumed
    TRKBUF ttx_fsi, FSI_SIZE        ; OSAPI_FSX_MODE's info block
    TRKBUF ttx_shadow, TTX_SHBYTES  ; the current pattern as char/attr words,
                                    ; TTX_HALF rows of the neighbouring orders
                                    ; at each end so the window into it needs
                                    ; no clamp (SPEC.md 45.13.4)
    TRKB ttx_shpat                  ; ...the pattern it holds
    TRKB ttx_shpos                  ; ...the order position it was reached
                                    ; from, which is what names the pads
    TRKB ttx_shloop                 ; ...and the [mp_ploop] they assumed
    TRKB ttx_ocpat                  ; what the shadow HELD, banked across the
    TRKB ttx_ocnext                 ; claim so the carry test can see it
    TRKB ttx_ocok
    TRKB ttx_shprev                 ; the pattern above row 0 (TTX_NOPAT none)
    TRKB ttx_shnext                 ; ...and the one below row 63
    TRKB ttx_shok                   ; ...and whether it holds anything
    TRKB ttx_vdirty                 ; force the next blit (a fresh shadow)
%ifdef TTXPAGE
    TRKB ttx_pgn                    ; TTXPAGE's countdown to the next page
%endif
    TRKBUF ttx_line, TTX_RW + 8     ; one composed line, ascii
    TRKB ttx_row                    ; ttx_rowtext's row (mp_cell2txt spends
                                    ; every general register, so it cannot
                                    ; live in one)
    TRKB ttx_pat                    ; ...and its pattern, for the same reason
    TRKB ttx_srow                   ; the build cursor: the next SHADOW row to
                                    ; format, wrapping over all TTX_SHROWS
    TRKB ttx_sleft                  ; ...rows still owed, TTX_SHROWS down to 0
    TRKB ttx_sbudget                ; ...and this frame's share of them
    TRKB ttx_shbusy                 ; a spread rebuild is in progress
    TRKB ttx_chn                    ; shared small loop counter
    TRKB ttx_clk                    ; fsx_wait's argument: FSXW_TICK / _FRAME
    TRKB ttx_chunk                  ; ...and the rebuild rows a frame it buys
    TRKB ttx_fdiv                   ; frames per tick - FSX_SUBTICK now that
    TRKB ttx_fcnt                   ; the clock is the PIT's, so reported and
                                    ; never inferred from a measurement
    TRKW ttx_vutick                 ; the tick the needles last decayed on
    TRKW ttx_lmsg                   ; the status string last drawn
    TRKW ttx_lrate                  ; ...and the rate last stamped into the
                                    ; nameplate (SPEC.md 45.13.7). The
                                    ; nameplate is drawn ONCE per bracket and
                                    ; the rate can move inside one - Play
                                    ; opens the stream at the pick - so the
                                    ; cell is delta-drawn on this, the
                                    ; [tui_lplay] idiom one field over
    TRKBUF ttx_vuc, 4               ; last-drawn bar widths, CELLS (SPEC.md
                                    ; 45.12's delta baseline, in this mode's
                                    ; own units - never tui_vupx, which is px)
