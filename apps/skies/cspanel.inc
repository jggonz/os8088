; =============================================================================
; CLEAR SKIES - the cockpit (SPEC.md 88.9.2)
;
; Drawn ONCE per target when the panel is first painted: the face, the
; instrument windows, their outlines, and a round bezel for the attitude
; indicator - all from shapes, in box coordinates, off a 320-wide layout
; through cs_hscalex, so it is one drawing on three adapters and round on
; each (cs_pasp). A bitmap would have been seven kilobytes a width and
; round on one of them. The readings are then the panel items of 88.9,
; lettered into the windows; the attitude indicator is redrawn on the same
; rate gate as the readings.
; =============================================================================

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; cs_pcells - AX = a width in CELLS; out AX = it in the 320-wide layout's own
;             units. Clobbers CX, DX (SPEC.md 88.9.5)
;
; A cell is 8 DEVICE pixels on every adapter, and the layout is 320 wide, so
; the two agree only where the box is 320 wide as well. On Hercules the box
; is 640 and cs_hscalex doubles, so eight layout units are FOUR cells there
; and not eight. Everything that sized a box or spaced a number by "8 at 320
; wide" was therefore twice as wide as it meant to be on the one adapter with
; the pixels to notice: the windows were 2.5x their text and the digits sat
; eight cells from their label instead of four.
; -----------------------------------------------------------------------------
cs_pcells:
    mov cx, 8 * 8
    mul cx
    div word [cs_hsx]
    ret

; cs_pcolx - AX = a signed CELL COLUMN; out AX = its x in the 320-wide
;            layout. Clobbers CX, DX (SPEC.md 88.9.5)
;
; Positive counts cells from the left edge, NEGATIVE from the right - so a
; right-hand readout is written as -(its cells + its margin) and lands the
; same distance in on every adapter, where a fixed layout x lands twice as
; far in on Hercules.
cs_pcolx:
    or ax, ax
    jns cs_pcells
    neg ax
    call cs_pcells
    neg ax
    add ax, 320
    ret

; -----------------------------------------------------------------------------
; cs_isqrt - AX = n; out AX = floor(sqrt(n)). Clobbers BX, CX, DX
;
; Two bits of the radicand a round, the schoolbook square root. It is here
; for cs_pdisc and runs once per target, so short beats quick.
; -----------------------------------------------------------------------------
cs_isqrt:
    mov bx, ax                      ; BX = what is left of n
    xor ax, ax                      ; AX = the root
    xor dx, dx                      ; DX = the remainder
    mov cx, 8
.b:
    shl ax, 1
    shl dx, 1                       ; rem = rem x 4 + the next two bits
    rol bx, 1
    adc dx, 0
    shl dx, 1
    rol bx, 1
    adc dx, 0
    push ax
    add ax, ax                      ; AX = 2 x root
    cmp dx, ax
    jbe .z                          ; rem <= 2 root: this bit is a zero
    sub dx, ax
    dec dx
    pop ax
    inc ax
    jmp short .n
.z:
    pop ax
.n:
    loop .b
    ret

; -----------------------------------------------------------------------------
; cs_pdisc - a FILLED ellipse: centre AX/BX, radii CX/DX, in the current ink
;            and under the caller's clip. Clobbers everything (SPEC.md 88.9.6)
;
; A round instrument used to be given a SQUARE plate and a round bezel drawn
; inside it, so the plate's four corners stood outside the glass as black
; boxes on the cockpit's face - which is what the field reported as "black
; boxes outside of them". The plate is the same shape as the bezel now.
; -----------------------------------------------------------------------------
cs_pdisc:
    call cs_eltop
.row:
    mov ax, [cs_eldy]
    call cs_elhw
    mov bx, [cs_elcx]
    mov cx, bx
    sub bx, ax
    add cx, ax                      ; BX..CX = the run
    mov ax, bx
    mov bx, [cs_elcy]
    add bx, [cs_eldy]
    mov dx, bx
    call cs_prect                   ; one row of it
    call cs_elnext
    jle .row
    ret

; -----------------------------------------------------------------------------
; cs_pring - the same ellipse as an OUTLINE, a row at a time and gap-free:
;            centre AX/BX, radii CX/DX (SPEC.md 88.9.6.3)
;
; It replaces a 24-segment polygon, which at the attitude indicator's size
; had a 7.8-pixel chord and read as a lumpy nut rather than a bezel. Each row
; lights from the PREVIOUS row's half-width to this one's, both sides, so a
; row where the edge moves several columns is filled across and the flat cap
; at the top and the bottom comes out of the same rule rather than a case.
; -----------------------------------------------------------------------------
cs_pring:
    call cs_eltop
    mov word [cs_elprev], 0         ; the row above the top has no width
.row:
    mov ax, [cs_eldy]
    call cs_elhw
    mov bx, [cs_elprev]
    mov [cs_elprev], ax
    cmp bx, ax
    jle .ord
    xchg ax, bx
.ord:
    mov [cs_ello], bx               ; the nearer edge...
    mov [cs_elhi], ax               ; ...and the further
    mov ax, [cs_elcy]
    add ax, [cs_eldy]
    mov [cs_elrow], ax
    mov ax, [cs_elcx]               ; --- the left edge ---
    sub ax, [cs_elhi]
    mov cx, [cs_elcx]
    sub cx, [cs_ello]
    mov bx, [cs_elrow]
    mov dx, bx
    call cs_prect
    mov ax, [cs_elcx]               ; --- and the right ---
    add ax, [cs_ello]
    mov cx, [cs_elcx]
    add cx, [cs_elhi]
    mov bx, [cs_elrow]
    mov dx, bx
    call cs_prect
    call cs_elnext
    jle .row
    ret

; cs_eltop - AX/BX = the centre, CX/DX = the radii: banked, and cs_eldy put on
;            the TOP row. Clobbers DX
cs_eltop:
    mov [cs_elcx], ax
    mov [cs_elcy], bx
    mov [cs_elrx], cx
    mov [cs_elry], dx
    neg dx
    mov [cs_eldy], dx
    ret

; cs_elnext - the next row; the flags say whether it is still in the ellipse
cs_elnext:
    mov ax, [cs_eldy]
    inc ax
    mov [cs_eldy], ax
    cmp ax, [cs_elry]
    ret

; cs_elhw - AX = a row's dy; out AX = rx sqrt(ry^2 - dy^2) / ry, the ellipse's
;           half width there. Every term is under 2^15: a radius is rows, and
;           rows are under 160. Clobbers BX, CX, DX
cs_elhw:
    imul ax
    mov bx, ax
    mov ax, [cs_elry]
    imul ax
    sub ax, bx
    jns .ok
    xor ax, ax                      ; (a row outside it: no width at all)
.ok:
    call cs_isqrt
    mul word [cs_elrx]
    div word [cs_elry]
    ret

; =============================================================================
; THE COCKPITS (SPEC.md 88.9.8). One record per aeroplane, and every one of
; them is laid out in the two units the panel actually has:
;
;   a WINDOW is (cell column, row, cells, rows). The column is signed - from
;   the left edge when positive and from the RIGHT when negative - and the
;   width is in CELLS, so a readout is the size of its own text and the same
;   distance in from its edge on all three adapters. `cells` = 0 is the whole
;   320, which is what a message strip wants.
;
;   an ITEM is (cell column, row), the same signed column, and its row is
;   two rows inside its window: a glyph is eight rows and a window twelve.
;
; The layout rows are shared and they are the BEZEL's, not the window's
; (SPEC.md 88.9.7.1): a readout occupies three pixels more than its rect on
; every side - one for its outline and two for its corner brackets - so the
; four levels are 5, 21, 37 and 53 with a window NINE rows tall, which puts
; a clear row at 1, 17, 33 and 49 and leaves row 0 to the panel's own top
; edge. Nine rows is what the face actually wants: the 8x8 cell's ink is
; seven rows deep, so a nine-row plate has exactly one blank row above the
; glyph and one below it.
;
; The message is at 67 and whatever a panel wears along the bottom at 82.
; Round instruments keep the LAYOUT's x, because they are round rather than
; lettered - and CGA is the adapter to lay them out against, its pixels
; being the widest per row (a dial's x radius is its rows over cs_pasp).
; tests/unit/t_cspanel.py holds all five to all three, bezels and all.
; =============================================================================

; --- THE ONE-LINE STRIP (SPEC.md 88.15.5), which is not an aeroplane's panel
;     at all: it is the ONE the 16-colour text hack uses, whatever is being
;     flown. The box there is a hundred rows tall and every one of them is
;     worth four of a CGA's, so a cockpit's 88 would leave the view twelve -
;     and 160 pixels is TWENTY cells, which no cockpit's six windows fit
;     across. What fits is the three readings a pilot actually scans, in
;     one line: speed, altitude and heading.
;
;       cell 0        6              14           19
;            S 120    A 01500        H 090        .
;
;     THERE ARE NO WINDOWS ON IT. A cockpit's window is an outline and two
;     corner brackets, so it occupies three pixels more than its rect on
;     every side and a nine-row one wants fifteen; and it buys nothing here,
;     because cs_glyph_c160 letters every cell OPAQUELY on [cs_pbg8], which
;     is black - so each reading already sits on a black plate exactly the
;     size of its own text, on a dark grey face.
;
;     Rows: 0 is the strip's own top edge, 3..10 the glyph, 1..12 what a
;     message erases. Every other item is (0, 0) - "this cockpit has not got
;     one" - which cs_pitem has meant since 88.9.5.1, so no drawing code
;     learns anything about the strip. THE MESSAGE IS ITEM 7 AT THE SAME
;     ROW, on purpose: a line this short cannot hold both, so a message
;     takes the strip and the three readings stand down while it is up
;     (88.15.6).
cs_ck_strip:
    dw 0, 0                         ; CSK_WIN, CSK_NWIN: NO WINDOWS AT ALL
    dw .items                       ; CSK_ITEMS
    dw 0, 0                         ; CSK_ADCX, CSK_ADCY: no attitude indicator
    dw 0                            ; CSK_ADRY
    dw 0                            ; CSK_BARW: no throttle bar either
    dw 0, 0                         ; CSK_DECO, CSK_NDECO: nothing that only
.items:                             ; looks the part - there is no room for it
    dw   0,  3                      ;   the speed
    dw   6,  3                      ;   the altitude
    dw  14,  3                      ;   the heading
    dw   0,  0                      ;   no attitude indicator
    dw   0,  0                      ;   no throttle
    dw   0,  0                      ;   no state line: the message says it
    dw   0,  0                      ;   no throttle bar
    dw   0,  3                      ;   the message, ACROSS the readings
    dw   0,  0                      ;   no variometer

; --- the Cessna's: a TRAINER's six-pack. Three readouts down the left, the
;     throttle down the right, and four round instruments either side of the
;     attitude indicator - which is what the aeroplane actually has, and
;     what the first version of this panel left out entirely ----------------
cs_ck_c172:
    dw .win, 6                      ; CSK_WIN, CSK_NWIN
    dw .items                       ; CSK_ITEMS
    dw 160, 24                      ; CSK_ADCX, CSK_ADCY
    dw 20                              ; CSK_ADRY (88.9.6)
    dw 8                            ; CSK_BARW, in cells
    dw .deco, 7                     ; CSK_DECO, CSK_NDECO
.win:                               ; cell column, row, cells, rows
    dw   1,  5,  9,  9              ;   SPD 000
    dw   1, 21, 11,  9              ;   ALT 00000
    dw   1, 37,  9,  9              ;   HDG 040
    dw -10,  5,  9,  9              ;   THR 100
    dw -10, 21,  9,  8              ;   the throttle bar's slot
    dw   1, 53, 15,  9              ;   ON THE GROUND
.items:                             ; cell column, row
    dw   2,  6                      ;   SPD
    dw   2, 22                      ;   ALT
    dw   2, 38                      ;   HDG
    dw   0,  0                      ;   (the attitude indicator draws itself)
    dw  -9,  6                      ;   THR
    dw   2, 54                      ;   the state
    dw  -9, 22                      ;   the throttle bar
    dw   0, 67                      ;   the message (centred; the row)
    dw   0,  0                      ;   no variometer: an aeroplane with an
                                    ;   engine does not carry one (88.7.6)
.deco:                              ; kind, x (layout), y, radius in rows, arg
    dw CSDK_DIAL,   115, 13, 10, 8000    ; the tachometer, showing cruise
    dw CSDK_DIAL,   205, 13, 10, 54000   ; oil pressure and temperature
    dw CSDK_DIAL,   115, 37, 10, 29000   ; the fuel gauges
    dw CSDK_DIAL,   205, 37, 10, 45000   ; the turn coordinator
    dw CSDK_DIAL,   190, 57,  7, 12000   ; the ammeter, charging
    dw CSDK_DIAL,   230, 57,  7, 20000   ; the vacuum gauge
    dw CSDK_RAIL,    40, 82,  4, 8 | (0xD7 << 8)
                                         ; the rail: master, both magnetos,
                                         ; carburettor heat COLD, the beacon,
                                         ; the landing light off in the day,
                                         ; the fuel pump, the pitot heat off
                                         ; and the avionics master

; --- the PITTS SPECIAL's (SPEC.md 88.9.3): the same readings, put where an
;     aerobatic biplane's are rather than in two tidy columns - the altimeter
;     where the pilot's eye falls and the attitude indicator off the
;     centreline, because that is what the aeroplane is like ----------------
cs_ck_pitts:
    dw .win, 6                      ; CSK_WIN, CSK_NWIN
    dw .items                       ; CSK_ITEMS
    dw 175, 26                      ; CSK_ADCX, CSK_ADCY: off centre, right
    dw 18                              ; CSK_ADRY (88.9.6)
    dw 8                            ; CSK_BARW
    dw .deco, 6                     ; CSK_DECO, CSK_NDECO
.win:
    dw   1,  5,  9,  9              ;   SPD 000, high on the left
    dw   1, 21, 11,  9              ;   ALT 00000, under the eye
    dw -10, 37,  9,  9              ;   HDG 040, low right
    dw -10,  5,  9,  9              ;   THR 100, high right
    dw -10, 21,  9,  8              ;   the throttle bar's slot
    dw   1, 53, 15,  9              ;   ON THE GROUND
.items:
    dw   2,  6                      ;   SPD
    dw   2, 22                      ;   ALT
    dw  -9, 38                      ;   HDG
    dw   0,  0                      ;   (the attitude indicator)
    dw  -9,  6                      ;   THR
    dw   2, 54                      ;   the state
    dw  -9, 22                      ;   the throttle bar
    dw   0, 67                      ;   the message
    dw   0,  0                      ;   no variometer (88.7.6)
.deco:
    dw CSDK_DIAL,   112, 12, 10, 8000    ; the tachometer
    dw CSDK_DIAL,   112, 38, 10, 54000   ; oil pressure, up in the green
    dw CSDK_DIAL,   165, 57,  7, 45000   ; the G meter, parked at 1
    dw CSDK_DIAL,   210, 57,  7, 26000   ; the fuel, on the right where an
    dw CSDK_DIAL,   255, 57,  7, 20000   ; aerobatic tank and its pressure are
    dw CSDK_RAIL,    40, 82,  4, 7 | (0x57 << 8)
                                         ; master, both magnetos, the SMOKE
                                         ; off, the fuel pump, the inverted
                                         ; tank's valve shut and the strobes

; --- the Magister's: a JET's panel (SPEC.md 88.7.5). The attitude indicator
;     is the centrepiece and the biggest of the five, because at 190 metres a
;     second it is the instrument you fly on; there is no tachometer, and the
;     dial under the left hand is the ENGINE, which visibly lags the throttle
cs_ck_fouga:
    dw .win, 6                      ; CSK_WIN, CSK_NWIN
    dw .items                       ; CSK_ITEMS
    dw 160, 25                      ; CSK_ADCX, CSK_ADCY: centred, and BIG
    dw 22                              ; CSK_ADRY (88.9.6)
    dw 8                            ; CSK_BARW
    dw .deco, 6                     ; CSK_DECO, CSK_NDECO
.win:
    dw   1,  5,  9,  9              ;   SPD 000, top left: the one you watch
    dw   1, 21, 11,  9              ;   ALT 00000, under it
    dw -10,  5,  9,  9              ;   HDG 040, top right
    dw -10, 21,  9,  9              ;   THR 100
    dw -10, 37,  9,  8              ;   the throttle bar's slot
    dw   1, 53, 15,  9              ;   ON THE GROUND
.items:
    dw   2,  6                      ;   SPD
    dw   2, 22                      ;   ALT
    dw  -9,  6                      ;   HDG
    dw   0,  0                      ;   (the attitude indicator)
    dw  -9, 22                      ;   THR
    dw   2, 54                      ;   the state
    dw  -9, 38                      ;   the throttle bar
    dw   0, 67                      ;   the message
    dw   0,  0                      ;   no variometer (88.7.6)
.deco:
    dw CSDK_DIAL,   112, 14, 10, 20000   ; % RPM - THE SPOOL, under the hand
    dw CSDK_DIAL,   208, 14, 10, 40000   ; the jet-pipe temperature
    dw CSDK_DIAL,   170, 57,  7, 12000   ; the fuel flow
    dw CSDK_DIAL,   215, 57,  7, 30000   ; the hydraulic pressure
    dw CSDK_DIAL,   260, 57,  7, 20000   ; the oxygen contents
    dw CSDK_RAIL,    40, 82,  4, 8 | (0xB7 << 8)
                                         ; both igniters, the generator, the
                                         ; airbrakes IN, the boost pumps, the
                                         ; second generator, no drop tanks and
                                         ; the pitot heat, on for altitude

; --- the Bijave's: A GLIDER'S PANEL (SPEC.md 88.7.6). There is no throttle,
;     so the window and the bar that would show one are simply not in the
;     list - a THR 000 that can never be anything else reads as a defect -
;     and what stands where they were is the VARIOMETER, the one instrument
;     a sailplane is actually flown on. The airbrake lever is the switch ----
cs_ck_bijave:
    dw .win, 5                      ; CSK_WIN, CSK_NWIN
    dw .items                       ; CSK_ITEMS
    dw 160, 25                      ; CSK_ADCX, CSK_ADCY
    dw 19                              ; CSK_ADRY (88.9.6)
    dw 8                            ; CSK_BARW: unused - no bar is drawn
    dw .deco, 6                     ; CSK_DECO, CSK_NDECO
.win:
    dw   1,  5,  9,  9              ;   SPD 000
    dw   1, 21, 11,  9              ;   ALT 00000
    dw -10,  5,  9,  9              ;   HDG 040
    dw -10, 21,  8,  9              ;   UP 015 - THE VARIOMETER, where a
                                    ;   powered aeroplane keeps its throttle
    dw   1, 53, 15,  9              ;   ON THE GROUND
.items:
    dw   2,  6                      ;   SPD
    dw   2, 22                      ;   ALT
    dw  -9,  6                      ;   HDG
    dw   0,  0                      ;   (the attitude indicator)
    dw   0,  0                      ;   NO THROTTLE
    dw   2, 54                      ;   the state
    dw   0,  0                      ;   ...and no throttle bar either
    dw   0, 67                      ;   the message
    dw  -9, 22                      ;   THE VARIOMETER
.deco:
    dw CSDK_DIAL,   112, 13, 10, 16384   ; the vario's own face, needle level
    dw CSDK_DIAL,   208, 13, 10, 49000   ; the total-energy compensator
    dw CSDK_DIAL,   208, 38, 10, 8000    ; the second altimeter, in feet
    dw CSDK_DIAL,   170, 57,  7, 16384   ; the yaw string's own gauge
    dw CSDK_DIAL,   215, 57,  7, 45000   ; the barograph
    dw CSDK_RAIL,   130, 82,  5, 3 | (0x04 << 8)
                                         ; airbrakes IN, the release, and the
                                         ; trim - a glider has no switches to
                                         ; speak of, so these are levers, and
                                         ; there are three of them

; --- the A5's: A MODERN LSA'S (SPEC.md 88.7.7). One wide glass panel rather
;     than a scatter of round dials, so the readings sit in a single block
;     left of a big attitude indicator - and it carries the VARIOMETER too,
;     which a light amphibian really does. The switch pair is the WATER
;     RUDDER, the one control that only means something on this aeroplane --
cs_ck_a5:
    dw .win, 7                      ; CSK_WIN, CSK_NWIN
    dw .items                       ; CSK_ITEMS
    dw 232, 25                      ; CSK_ADCX, CSK_ADCY: right, in the glass
    dw 20                              ; CSK_ADRY (88.9.6)
    dw 8                            ; CSK_BARW
    dw .deco, 7                     ; CSK_DECO, CSK_NDECO
.win:
    dw   1,  5,  9,  9              ;   SPD 000
    dw   1, 21, 11,  9              ;   ALT 00000
    dw   1, 37,  9,  9              ;   HDG 040
    dw  14,  5,  9,  9              ;   THR 100
    dw  14, 21,  9,  8              ;   the throttle bar's slot
    dw   1, 53, 15,  9              ;   ON THE WATER
    dw  14, 37,  8,  9              ;   UP 015 - the variometer
.items:
    dw   2,  6                      ;   SPD
    dw   2, 22                      ;   ALT
    dw   2, 38                      ;   HDG
    dw   0,  0                      ;   (the attitude indicator)
    dw  15,  6                      ;   THR
    dw   2, 54                      ;   the state
    dw  15, 22                      ;   the throttle bar
    dw   0, 67                      ;   the message
    dw  15, 38                      ;   THE VARIOMETER
.deco:
    dw CSDK_DIAL,   290, 12, 10, 8000    ; the engine, on one glass page with
    dw CSDK_DIAL,   290, 38, 10, 54000   ; its temperatures beside it
    dw CSDK_DIAL,   196, 13,  6, 20000   ; the fuel and the flaps, in the band
    dw CSDK_DIAL,   196, 38,  6, 30000   ; between the glass and the horizon
    dw CSDK_DIAL,   160, 57,  7, 12000   ; the volts
    dw CSDK_DIAL,   200, 57,  7, 45000   ; the trim
    dw CSDK_RAIL,    40, 82,  5, 8 | (0xAF << 8)
                                         ; master, both pumps, THE WATER
                                         ; RUDDER down, the gear UP for a
                                         ; splash, the bilge pump - the one
                                         ; control no other aeroplane here
                                         ; has - no strobes, and the nav
                                         ; lights

; -----------------------------------------------------------------------------
; cs_ckrec - SI = the cockpit record this panel is drawn from: the aeroplane's
;            own, or the shared ONE-LINE STRIP on the 16-colour text hack,
;            where 20 cells hold no cockpit at all (SPEC.md 88.15.5).
;            Clobbers nothing but SI
;
; It is a proc rather than three copies of the test because the plane's record
; is read from three places, and a fourth backend arriving at one of them and
; not the others is exactly the shape 88.9.5.1 was.
; -----------------------------------------------------------------------------
cs_ckrec:
    cmp byte [cs_back], CSB_C160
    je .strip
    mov si, [cs_plane]
    mov si, [si + CSP_COCKPIT]
    ret
.strip:
    mov si, cs_ck_strip
    ret

; cs_pcell - BX = an item: out BX = its x at 320 wide, DX = its row below the
;            view, off the panel's cockpit. Clobbers SI
cs_pcell:
    call cs_ckrec
    mov si, [si + CSK_ITEMS]
    shl bx, 1
    shl bx, 1
    add si, bx
    mov dx, [si + 2]
    mov ax, [si]
    or ax, ax                       ; (0, 0) is still "this cockpit has none"
    jnz .col
    or dx, dx
    jz .none
.col:
    push dx
    call cs_pcolx
    pop dx
.none:
    mov bx, ax
    ret

; -----------------------------------------------------------------------------
; cs_pface - the cockpit, once. Clobbers everything
; -----------------------------------------------------------------------------
cs_pface:
    mov al, CSI_PFACE               ; --- the face -------------------------------
    call cs_setink
    xor ax, ax
    mov bx, [cs_pany]
    mov cx, [cs_vw]
    dec cx
    mov dx, [cs_vh]
    dec dx
    call cs_prect
    mov al, CSI_PBG                 ; --- the windows ----------------------------
    call cs_setink
    call cs_ckrec
    mov cx, [si + CSK_NWIN]
    mov si, [si + CSK_WIN]
    jcxz .now                       ; a panel with NO windows (88.15.5) - and
.w:                                 ; `loop` from zero is 65,536 of them
    push cx
    push si
    call cs_pwrect
    call cs_prect
    pop si
    pop cx
    add si, 8
    loop .w
.now:
    call cs_pclip                   ; --- their outlines, and the panel's top ---
    mov al, CSI_PFG
    call cs_setink
    call cs_ckrec
    mov cx, [si + CSK_NWIN]
    mov si, [si + CSK_WIN]
    jcxz .noo
.o:
    push cx
    push si
    call cs_pwrect
    dec ax
    dec bx
    inc cx
    inc dx
    call cs_pbox
    pop si
    pop cx
    add si, 8
    loop .o
.noo:
    xor ax, ax
    mov bx, [cs_pany]
    mov cx, [cs_vw]
    dec cx
    mov dx, bx
    call cs_seg
    ; --- the attitude indicator: a round bezel round a square window ---------
    call cs_ckrec
    mov word [cs_adry], 0           ; A ZERO BEZEL IS NO INSTRUMENT (88.15.5):
    cmp word [si + CSK_ADRY], 0     ; the one-line strip has no room for one,
    jne .adi                        ; and a ring of radius 0 at (0, 0) would
    call cs_vclip                   ; be two stray pixels at the strip's left.
    jmp .noadi                      ; THE CLIP IS GIVEN BACK ON THE WAY PAST:
.adi:                               ; cs_pclip SAVES the view's edges and a
    mov ax, [si + CSK_ADCX]         ; second arm would save the panel's over
    call cs_hscalex                 ; them, so skipping the ADI's cs_vclip
                                    ; would cost the whole bracket its clip
    mov [cs_adcx], ax
    mov ax, [cs_pany]
    add ax, [si + CSK_ADCY]
    mov [cs_adcy], ax
    mov ax, [si + CSK_ADRY]
    mov [cs_adry], ax
    mov ah, al                      ; round on this adapter: rx = ry / aspect
    xor al, al                      ; (AX = ry x 256)
    xor dx, dx
    div word [cs_pasp]
    call cs_hscalex                 ; ...AND INTO BOX PIXELS, which the first
                                    ; build did not do: cs_pasp is rows per
                                    ; LOGICAL x unit (the 320-wide layout's),
                                    ; so the quotient is a logical radius and
                                    ; the ellipse wants box pixels. On CGA and
                                    ; Mode X the two are the same and it was
                                    ; round; on HERCULES the box is 640 wide
                                    ; and the bezel came out half as wide as
                                    ; it should be - the tall oval in every
                                    ; Hercules photograph of the panel
                                    ; (SPEC.md 88.9.2.1)
    mov [cs_adrx], ax
    sub ax, 2                       ; THE GLASS IS THE BEZEL'S INSIDE, and it
    mov [cs_adhw], ax               ; used to be 0.625 of it - which is why
    mov ax, [cs_adry]               ; the black disc sat in the middle of the
    sub ax, 2                       ; ring with a band of the cockpit's face
    mov [cs_adhh], ax               ; between them (SPEC.md 88.9.6)
    call cs_adsize                  ; SMALL shrinks the glass, and every mode
                                    ; but FULL wants the table (88.9.2.5)
    mov al, CSI_PFG
    call cs_setink
    mov ax, [cs_adcx]
    mov bx, [cs_adcy]
    mov cx, [cs_adrx]
    mov dx, [cs_adry]
    call cs_pring
    mov ax, [cs_adcx]
    mov bx, [cs_adcy]
    mov cx, [cs_adrx]
    add cx, cx
    shr cx, 1                       ; (CX = rx again: the two rings are)
    add cx, 3                       ; three columns and one row apart
    mov dx, [cs_adry]
    inc dx
    call cs_pring
    call cs_vclip
    call cs_adwin                   ; ...and the window, black
.noadi:
    ; --- and the instruments that only look the part (SPEC.md 88.9.3) -------
    call cs_ckrec
    mov cx, [si + CSK_NDECO]
    jcxz .nodeco
    mov si, [si + CSK_DECO]
    mov al, CSI_PFG
    call cs_setink
    call cs_pclip
.d:
    push cx
    push si
    call cs_pdeco
    pop si
    pop cx
    add si, CSD_SIZE
    loop .d
    call cs_vclip
.nodeco:
    call cs_pclip                   ; ...and the four LABELS that never change
    call cs_plabels                 ; (88.9.4), under the panel's own clip and
    call cs_vclip                   ; OUTSIDE the decoration loop, which a
    ret                             ; cockpit with no decorations skips whole

; -----------------------------------------------------------------------------
; cs_pdeco - one decoration (SPEC.md 88.9.3). SI -> its five words; the ink
;            and the clip are the caller's. Clobbers everything
;
; Its radius is given in ROWS and the x radius derived through cs_pasp, the
; same way the attitude indicator's bezel is, so a dial is round on all three
; adapters rather than round on one and an egg on the others.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; cs_plabels - SPD, ALT, HDG and THR, once per target (SPEC.md 88.9.4)
;
; They were part of every reading: cs_pnum built 'ALT ' + five digits into one
; buffer and lettered the lot, so nine cells were drawn where five had
; changed - and on Mode X, where a cell is four bits deep and cs_panel was
; measured at 21.8% of the frame, that is most of the panel's cost. The
; label is drawn with the cockpit instead and the digits alone are redrawn.
; The variometer is NOT in this list: its label is UP or DN, which is the
; sign of the reading and changes with it.
; -----------------------------------------------------------------------------
cs_plabels:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                      ; DI = the item
.l:
    mov si, di
    shl si, 1
    add si, [cs_plblp]              ; a cockpit's SPD/ALT/HDG/THR, or the
    mov si, [si]                    ; strip's one letter each (88.15.5)
    or si, si
    jz .next                        ; this item has no fixed label
    mov bx, di
    push di
    push si
    call cs_pcell                   ; BX = its x, DX = its row
    pop si
    or bx, bx                       ; ...and this cockpit has no such window.
    jnz .lbl                        ; IT IS BOTH WORDS, which cs_pitem has
    or dx, dx                       ; always tested and this tested only the
    jz .n2                          ; x - so a label at CELL 0 was dropped,
.lbl:                               ; which no cockpit has and the ONE-LINE
    call cs_pline                   ; strip's speed does: its number was
.n2:                                ; lettered and its S was not (88.15.5)
    pop di
.next:
    inc di
    cmp di, CS_PI_THR
    jbe .l
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the fixed label of items 0..CS_PI_THR; 0 where there is none
cs_plbl:    dw cs_s_spd, cs_s_alt, cs_s_hdg, 0, cs_s_thr
; ...and the ONE-LINE strip's, where a label is a letter and a space: three
; four-letter ones plus their numbers is 23 cells of the 20 there are
; (SPEC.md 88.15.5)
cs_plbl160: dw cs_s_s1, cs_s_a1, cs_s_h1, 0, 0
cs_s_s1:    db 'S', 0
cs_s_a1:    db 'A', 0
cs_s_h1:    db 'H', 0

cs_pdeco:
    mov [cs_dsi], si
    mov ax, [si + CSD_Y]
    add ax, [cs_pany]
    mov [cs_dcy], ax
    mov ax, [si + CSD_R]
    mov [cs_dry], ax
    mov ah, al                      ; rx = ry / aspect (AX = ry x 256)...
    xor al, al
    xor dx, dx
    div word [cs_pasp]
    call cs_hscalex                 ; ...and into box pixels (88.9.2.1)
    mov [cs_drx], ax
    mov ax, [si + CSD_ARG]
    mov [cs_darg], ax
    cmp word [si + CSD_KIND], CSDK_RAIL
    je .rail
    mov si, [cs_dsi]
    mov ax, [si + CSD_X]
    call cs_hscalex
    mov [cs_dcx], ax
    call cs_dplate
    mov si, [cs_dsi]
    cmp word [si + CSD_KIND], CSDK_SWITCH
    je .switch
    ; --- a dial: the bezel, then a needle from the centre at CSD_ARG -------
    mov ax, [cs_dcx]
    mov bx, [cs_dcy]
    mov cx, [cs_drx]
    mov dx, [cs_dry]
    call cs_pring
    mov ax, [cs_darg]               ; ...three quarters of the way out
    call cs_cos
    mov bx, [cs_drx]
    MUL14
    mov cx, ax
    sar cx, 1
    sar cx, 1
    sub ax, cx                      ; AX = 0.75 x rx cos
    add ax, [cs_dcx]
    mov [cs_dpx], ax
    mov ax, [cs_darg]
    call cs_sin
    mov bx, [cs_dry]
    MUL14
    mov cx, ax
    sar cx, 1
    sar cx, 1
    sub ax, cx                      ; AX = 0.75 x ry sin
    add ax, [cs_dcy]
    mov dx, ax
    mov cx, [cs_dpx]
    mov ax, [cs_dcx]
    mov bx, [cs_dcy]
    jmp cs_seg                      ; the needle
.switch:
    ; --- a toggle: a stalk up or down out of its plate --------------------
    mov ax, [cs_dcx]
    mov bx, [cs_dcy]
    mov cx, ax
    mov dx, [cs_dry]
    add dx, 1
    cmp word [cs_darg], 0
    je .down
    neg dx                          ; up
.down:
    add dx, bx
    jmp cs_seg
.rail:
    ; --- A ROW OF TOGGLES (SPEC.md 88.9.3.1): the count in CSD_ARG's low
    ;     byte, which way each is thrown in its high one, CS_RAILSTEP apart
    mov ax, [cs_darg]
    xor ah, ah
    mov [cs_dn], ax
    mov ax, [cs_darg]
    mov al, ah
    xor ah, ah
    mov [cs_dmask], ax
    mov si, [cs_dsi]
    mov ax, [si + CSD_X]
    mov [cs_dxl], ax
.r:
    cmp word [cs_dn], 0
    jle .rdone
    mov ax, [cs_dxl]
    call cs_hscalex
    mov [cs_dcx], ax
    mov ax, [cs_dmask]
    and ax, 1
    mov [cs_darg], ax
    call cs_dplate
    call .switch                    ; (a tail jmp to cs_seg, so it returns)
    shr word [cs_dmask], 1
    mov ax, [cs_dxl]
    add ax, CS_RAILSTEP
    mov [cs_dxl], ax
    dec word [cs_dn]
    jmp short .r
.rdone:
    ret

; cs_dplate - A DARK FACE FIRST, and it is not decoration on decoration: the
;             cockpit's face is a 25% dither (CSI_PFACE) and a white outline
;             on it reads as noise at this size. The instrument windows solve
;             the same problem the same way - a black ground, a white edge -
;             so a gauge gets the plate it would have in the metal. It is
;             ROUND, not a square with a circle drawn inside it (88.9.6), and
;             it leaves the ink set to CSI_PFG for whatever goes on top
cs_dplate:
    mov al, CSI_PBG
    call cs_setink
    mov ax, [cs_dcx]
    mov bx, [cs_dcy]
    mov cx, [cs_drx]
    mov dx, [cs_dry]
    call cs_pdisc
    mov al, CSI_PFG
    jmp cs_setink

; cs_pwrect - SI -> a window row: AX/BX/CX/DX = its box corners
cs_pwrect:
    mov ax, [si]                    ; the cell column...
    call cs_pcolx
    mov [cs_pwl], ax
    mov ax, [si + 4]                ; ...and the width in CELLS, so a window
    or ax, ax                       ; is the size of what goes in it on every
    jnz .cells                      ; adapter (SPEC.md 88.9.5). Zero cells is
    mov ax, 320                     ; the WHOLE panel, and a NEGATIVE count is
    jmp short .wide                 ; that many cells CENTRED - which is what
.cells:                             ; a message plaque wants and what no
    jns .plain                      ; column can say on two adapters at once
    neg ax
    call cs_pcells
    mov cx, ax
    shr cx, 1
    mov ax, 160
    sub ax, cx
    mov [cs_pwl], ax                ; centred: the middle less half its width
    mov ax, cx
    add ax, ax
    jmp short .wide
.plain:
    call cs_pcells
.wide:
    add ax, [cs_pwl]
    dec ax
    call cs_hscalex
    mov [cs_pwr], ax
    mov ax, [cs_pwl]
    call cs_hscalex
    mov cx, [cs_pwr]
    mov bx, [si + 2]
    add bx, [cs_pany]
    mov dx, [si + 6]                ; the height in ROWS, which every adapter
    add dx, bx                      ; shares
    dec dx
    ret

; -----------------------------------------------------------------------------
; cs_pbox - a readout's BEZEL: the outline of AX/BX (top left) .. CX/DX and
;           four corner brackets two pixels outside it, under cs_pclip.
;           Clobbers everything (SPEC.md 88.9.7)
;
; The brackets are what makes a window read as an instrument set into a panel
; rather than as a white rectangle painted on one. A second full outline was
; the other candidate and it loses: the face is a 25% dither, so two lines
; two pixels apart read as one thick smear at this size, where four corners
; read as a bezel. They cost eight short segments a window, once per target.
; -----------------------------------------------------------------------------
CS_PBRK equ 5                       ; how far a bracket runs along each edge

cs_pbox:
    mov [cs_adx1], ax
    mov [cs_ady1], bx
    mov [cs_adx2], cx
    mov [cs_ady2], dx
    mov dx, bx                      ; the top
    call cs_seg
    mov ax, [cs_adx1]               ; the bottom
    mov bx, [cs_ady2]
    mov cx, [cs_adx2]
    mov dx, bx
    call cs_seg
    mov ax, [cs_adx1]               ; the left
    mov bx, [cs_ady1]
    mov cx, ax
    mov dx, [cs_ady2]
    call cs_seg
    mov ax, [cs_adx2]               ; the right
    mov bx, [cs_ady1]
    mov cx, ax
    mov dx, [cs_ady2]
    call cs_seg
    ; --- and four corner brackets, two pixels outside ---------------------
    mov ax, [cs_adx1]
    sub ax, 2
    mov [cs_adx1], ax
    mov ax, [cs_ady1]
    sub ax, 2
    mov [cs_ady1], ax
    mov ax, [cs_adx2]
    add ax, 2
    mov [cs_adx2], ax
    mov ax, [cs_ady2]
    add ax, 2
    mov [cs_ady2], ax
    mov ax, [cs_adx1]               ; top left, along and down
    mov bx, [cs_ady1]
    mov cx, ax
    add cx, CS_PBRK
    mov dx, bx
    call cs_seg
    mov ax, [cs_adx1]
    mov bx, [cs_ady1]
    mov cx, ax
    mov dx, bx
    add dx, CS_PBRK
    call cs_seg
    mov ax, [cs_adx2]               ; top right
    mov bx, [cs_ady1]
    mov cx, ax
    sub cx, CS_PBRK
    mov dx, bx
    call cs_seg
    mov ax, [cs_adx2]
    mov bx, [cs_ady1]
    mov cx, ax
    mov dx, bx
    add dx, CS_PBRK
    call cs_seg
    mov ax, [cs_adx1]               ; bottom left
    mov bx, [cs_ady2]
    mov cx, ax
    add cx, CS_PBRK
    mov dx, bx
    call cs_seg
    mov ax, [cs_adx1]
    mov bx, [cs_ady2]
    mov cx, ax
    mov dx, bx
    sub dx, CS_PBRK
    call cs_seg
    mov ax, [cs_adx2]               ; bottom right
    mov bx, [cs_ady2]
    mov cx, ax
    sub cx, CS_PBRK
    mov dx, bx
    call cs_seg
    mov ax, [cs_adx2]
    mov bx, [cs_ady2]
    mov cx, ax
    mov dx, bx
    sub dx, CS_PBRK
    jmp cs_seg

; cs_adwin - the attitude indicator's window, black
cs_adwin:
    mov al, CSI_PBG
    call cs_setink
    mov ax, [cs_adcx]
    mov bx, [cs_adcy]
    mov cx, [cs_adhw]
    mov dx, [cs_adhh]
    cmp word [cs_adtn], 0           ; the table is built: walk it (88.9.2.5).
    jne cs_addisc                   ; A bezel too tall for it falls back here
    jmp cs_pdisc                    ; - ROUND either way, because the glass is
                                    ; round and so is the black behind it
                                    ; (SPEC.md 88.9.6)

; -----------------------------------------------------------------------------
; cs_adsize - the ADI's half-width TABLE (SPEC.md 88.9.2.5). Called from
;             cs_pface once a layout, which is the only time cs_adrx/cs_adry
;             can move.
;
; **The erase is a square root a row and the radius never changes in flight.**
; cs_elhw is `sqrt(ry^2 - dy^2) . rx / ry`, and cs_pdisc takes it again on
; every row of every redraw for an answer that was the same last frame and
; will be the same next: 35 roots a redraw, 41 ms of a banked frame. The rows
; are symmetric about the centre, so the table is hh + 1 entries.
; -----------------------------------------------------------------------------
cs_adsize:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [cs_adtn], 0
    mov ax, [cs_adhh]
    or ax, ax
    jle .out
    cmp ax, CS_ADHMAX
    ja .out                         ; too tall for the table: keep the roots
    mov [cs_elry], ax
    mov ax, [cs_adhw]
    mov [cs_elrx], ax
    xor si, si                      ; SI = dy, 0 .. hh
.t:
    mov ax, si
    call cs_elhw                    ; the ONE place these roots are taken now
    mov bx, si
    shl bx, 1
    mov [cs_adtab + bx], ax
    inc si
    cmp si, [cs_adhh]
    jbe .t
    mov ax, [cs_adhh]
    mov [cs_adtn], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_addisc - cs_pdisc's picture off cs_adtab: the same rows, the same runs,
;             no root. in: AX/BX the centre, CX/DX the radii (DX = [cs_adtn])
; -----------------------------------------------------------------------------
cs_addisc:
    mov [cs_elcx], ax
    mov [cs_elcy], bx
    mov si, dx
    neg si                          ; SI = dy, -hh .. +hh
.row:
    mov bx, si
    or bx, bx
    jns .pos
    neg bx
.pos:
    shl bx, 1
    mov ax, [cs_adtab + bx]         ; the half-width, looked up
    mov bx, [cs_elcx]
    mov cx, bx
    sub bx, ax
    add cx, ax                      ; BX..CX = the run
    mov ax, bx
    mov bx, [cs_elcy]
    add bx, si
    mov dx, bx
    push si
    call cs_prect                   ; one row of it, as cs_pdisc lays one
    pop si
    inc si
    cmp si, [cs_adtn]
    jle .row
    ret

; -----------------------------------------------------------------------------
; The attitude indicator (SPEC.md 88.9.2): the horizon as one line in the
; window, sloped by the roll and moved by the pitch, and the aeroplane as two
; bars and a dot that stay put. Keyed by the two angles' top bytes - 256 to
; the turn, 1.4 degrees, which is a pixel and a half at the line's ends -
; and read on the panel's rate gate like the other instruments.
; -----------------------------------------------------------------------------
cs_k_adi:
    mov al, [cs_roll + 1]
    mov ah, [cs_pitch + 1]
    ret

cs_d_adi:
    cmp word [cs_adry], 0           ; ...nor is it redrawn on the gate
    je .nadi
    call cs_adwin                   ; the window over the last line
    ; --- THE ATTITUDE AND NOT THE EULER TRIPLE (SPEC.md 88.9.2.6). A triple
    ;     names each attitude twice and the panel wants the other one: past
    ;     the vertical the horizon comes BACK, and pitch 180 with the wings
    ;     rolled level is an aeroplane straight and level. Raw, its top byte
    ;     is 0x80 and the offset is -128 rows on a glass ten rows high, so
    ;     88.9.6.2's chord finds d >= R and .none draws nothing - which is
    ;     the field's "the attitude line is completely gone". The fold is
    ;     (180 - pitch, roll + 180) and it is EXACT, sin(180 - p) being
    ;     sin(p): the representation moves and the aeroplane does not
    mov ax, [cs_pitch]
    mov bx, [cs_roll]
    mov cx, ax
    add cx, 16384
    jns .att                        ; 88.7.8.1's test: cos(pitch) < 0
    neg ax
    add ax, 0x8000                  ; 180 - pitch, wrapping for free
    xor bh, 0x80                    ; ...and the aeroplane is upside down
.att:
    mov [cs_adroll], bx
    ; --- the offset: nose up moves the horizon DOWN, a row per 1.4 degrees --
    mov al, ah
    cbw
    mov [cs_adoff], ax
    ; --- the slope: rows over the half-width, aspect in: t = hw x aspect ----
    mov ax, [cs_adhw]
    mul word [cs_pasp]
    mov al, ah
    mov ah, dl                      ; AX = t, rows for a slope of one
    push ax
    mov ax, [cs_adroll]             ; ...the SINE first, and the cosine LAST,
    call cs_sin                     ; straight into the divisor (88.9.2.4):
    pop bx                          ; BX = t              this line used to
    imul bx                         ; DX:AX = t sin       hold cos in CX
    push dx                                             ; across cs_sin, which
    push ax                                             ; is documented to
    mov ax, [cs_adroll]                                 ; clobber it
    call cs_cos
    mov bx, ax                      ; BX = cos, and nothing runs before the
    pop ax                          ; divide that could touch it
    pop dx
    ; --- COS IS EXACTLY ZERO AT 90 DEGREES (SPEC.md 88.9.2.2) --------------
    ; The note here used to read "CX = cos, over 0.5 within MAXROLL", and
    ; that is true of a TRAINER. cs_att_free and cs_att_lag have no roll
    ; clamp at all (88.7.2), so an aerobatic aeroplane rolls straight through
    ; the vertical - and the angle steps 64 units at a time through the
    ; table, so cos reads EXACTLY 0 for that whole window at +-90 and +-201
    ; either side of it (tests/unit/t_cssin.py asserts the crossings stay
    ; exact under the quarter table, 88.5.9). A bare idiv there is a DIVIDE
    ; BY ZERO, and on an 8088 int 0 returns past it with AX undefined, which
    ; puts a garbage rise into a segment the walk then draws off the end of
    ; the shadow.
    call cs_cdiv                    ; the guarded divide (85.5) answers +-30000
                                    ; rather than faulting, zero divisor and all
    mov bx, [cs_adhh]               ; ...and the rise is clamped to four times
    shl bx, 1                       ; the window's half height: past that the
    shl bx, 1                       ; bar is vertical however much further it
    cmp ax, bx                      ; would go, and every number stays in a
    jle .dyhi                       ; word
    mov ax, bx
.dyhi:
    neg bx
    cmp ax, bx
    jge .dylo
    mov ax, bx
.dylo:
    mov [cs_addy], ax
    ; --- THE ENDS ARE A CHORD OF THE ROUND GLASS (SPEC.md 88.9.6.2) -------
    ; Scale x by R/W and the glass is a CIRCLE of radius R = cs_adhh, in
    ; which the line is v = off + k.u/R. Then
    ;   a  = R^2 + k^2
    ;   d  = R.|off| / sqrt(a)          the line's distance from the centre
    ;   L  = sqrt(R^2 - d^2)            half the chord
    ;   u* = -off.k.R / a               the foot of the perpendicular
    ; and the ends are u* +- L.R/sqrt(a). Every term is a word but one
    ; 32-bit product whose quotient is a word, and d >= R is the whole of
    ; "the horizon is not on the glass".
    mov ax, [cs_adhh]
    mov bx, ax
    imul bx
    mov [cs_adrr], ax               ; R^2
    mov ax, [cs_addy]
    mov bx, ax
    imul bx
    add ax, [cs_adrr]
    mov [cs_ada], ax                ; a = R^2 + k^2
    call cs_isqrt
    or ax, ax
    jz .none                        ; (a glass with no size: divide by none)
    mov [cs_adsa], ax
    mov ax, [cs_adoff]
    or ax, ax
    jns .o1
    neg ax
.o1:
    mul word [cs_adhh]
    div word [cs_adsa]              ; AX = d
    cmp ax, [cs_adhh]
    jge .none                       ; the line misses the glass entirely
    mov bx, ax
    imul bx                         ; d^2
    mov bx, [cs_adrr]
    sub bx, ax
    mov ax, bx
    call cs_isqrt                   ; L
    or ax, ax
    jz .none
    dec ax                          ; a pixel in, so rounding cannot poke a
    mul word [cs_adhh]              ; tip out onto the cockpit's face
    div word [cs_adsa]
    mov [cs_adstep], ax             ; L.R / sqrt(a)
    mov ax, [cs_adoff]
    imul word [cs_addy]             ; off.k, a word
    imul word [cs_adhh]             ; DX:AX = off.k.R
    idiv word [cs_ada]
    neg ax
    mov [cs_adus], ax               ; u*
    sub ax, [cs_adstep]
    mov si, cs_adx1
    call cs_adend
    mov ax, [cs_adus]
    add ax, [cs_adstep]
    mov si, cs_adx2
    call cs_adend
    call cs_pclip
    mov al, CSI_PFG
    call cs_setink
    mov ax, [cs_adx1]
    mov bx, [cs_ady1]
    mov cx, [cs_adx2]
    mov dx, [cs_ady2]
    call cs_seg
    jmp short .plane
.none:
    call cs_pclip
    mov al, CSI_PFG
    call cs_setink
.plane:                             ; --- the aeroplane: two bars and a dot ---
    mov bx, [cs_adhw]
    shr bx, 1                       ; the bars: from a quarter to a half of
    mov cx, bx                      ; the half-width, either side
    shr cx, 1
    mov ax, [cs_adcx]
    sub ax, bx
    push cx
    mov cx, [cs_adcx]
    pop dx
    sub cx, dx                      ; CX = cx - hw/4
    mov bx, [cs_adcy]
    mov dx, bx
    call cs_seg
    mov bx, [cs_adhw]
    shr bx, 1
    mov cx, bx
    shr cx, 1
    mov ax, [cs_adcx]
    add ax, cx
    mov cx, [cs_adcx]
    add cx, bx
    mov bx, [cs_adcy]
    mov dx, bx
    call cs_seg
    mov ax, [cs_adcx]               ; the dot
    mov cx, ax
    mov bx, [cs_adcy]
    mov dx, bx
    call cs_seg
    jmp cs_vclip
.nadi:
    ret

; cs_adend - AX = the end's u; SI -> the (x, y) pair to fill in. Its x is
;            u.W/R and its y off + u.k/R, both back in box coordinates
cs_adend:
    push ax
    imul word [cs_adhw]
    idiv word [cs_adhh]
    add ax, [cs_adcx]
    mov [si], ax
    pop ax
    imul word [cs_addy]
    idiv word [cs_adhh]
    add ax, [cs_adoff]
    add ax, [cs_adcy]
    mov [si + 2], ax
    ret
