; =============================================================================
; apps/isotron/itsweep.inc - THE SWEEP: a new arena onto the glass (SPEC.md
; 108.13.3)
;
; Between rounds the original drew its slab in a fixed order, over the old
; picture, in about 2.2 s on the 5150: the rims and the right face at once,
; the front face left to right, the floor's dither left to right, and the
; white outline - the collision boundary - last. That order is the game's
; visual signature, so it is kept; the pace is ours: ~1.5 s where the
; surface can, never over 2.2.
;
; THE CANVAS IS REBUILT AT ONCE (it_rebuild, with the top level's outline
; VOID: it_rbnoe) and THE GLASS CATCHES UP: the sweep is presentation only.
; Nothing of the game waits on it but READY, and the canvas it ends on is
; the model's byte for byte (it_outline writes the edges back last).
;
; WHAT IS PRESENTED is not the picture. The picture is 320 x 200 and most of
; it does not change between rounds; what can change is the old TOP LEVEL
; (its walls, its outline) and the new one, so each 8-px band of columns
; carries only the rows between the two levels' tops and the lower of their
; bottoms - the old level's front outline, the new level's front face (SPEC.md
; 108.13.3 has why nothing outside that span can differ, for a round at the
; same height, a raise and a reset alike). Rows above the new level that the
; old one held - a reset's - are VOID in the new picture and go out as ONE
; gfx_fill a band, not converted (windowed and EGA: it_pfill); full screen
; converts them, its presenters being direct.
;
; ITEMS, presented in this order and each in one piece:
;
;   SW_NA  A  band b's rows above its floor (the old level's leftovers, the
;             outline row, the rims), between its floor and front face, and
;             below the front face (the right face, the lower levels)
;   SW_NB  B  band b's front outline row and front face (b <= 27)
;   SW_NC  C  band b's floor
;   SW_ND  D  the outline: D0 writes it into the canvas and presents the back
;             and front edges; D1..D13 a band each of the two diagonals
;
; and DUE on the ticks since the sweep started (it_sw_due): every A at once,
; the B bands over ticks 2..8 (4 a tick from SW_TB + 1), the C bands over
; 9..24, D at 25 - 1.37 s. A surface that cannot keep up runs flat out (the windowed
; worker yields rather than sleeping while items are due). W_PAINT presents
; only the items already presented (it_sw_paint): the rest is owed to the
; sweep, and the canvas there is no longer the picture the glass held.
;
; Register contract: DS = the package; ES = the work claim where a caller of
; the presenters says so (they set their own).
; =============================================================================

SW_BANDS    equ 40                  ; 8-px bands of the 317 columns
SW_NA       equ 4 * SW_BANDS        ; A: four passes over the bands, a slot each
SW_NB       equ 28                  ; bands 0..27 hold front face (x <= 216)
SW_NC       equ SW_BANDS
SW_ND       equ 14                  ; D0, then 13 diagonal bands
SW_B0       equ SW_NA
SW_C0       equ SW_B0 + SW_NB
SW_D0       equ SW_C0 + SW_NC
SW_N        equ SW_D0 + SW_ND       ; 242 items
SW_RMAX     equ 4                   ; rects an item makes, at most (A's)
SW_GMAX     equ 8                   ; items a UNION merges, at most (its waste)
SW_PIECEB   equ 512                 ; device bytes a windowed piece, about
SW_PIECEMIN equ 4                   ; ...and its rows, at least
SW_SPN      equ 32                  ; spans a list (it_sw_dspan)
SW_Y0       equ 0                   ; a rect: logical rows [y0, y1) (EVEN: the
SW_Y1       equ 2                   ; full-screen presenters take row pairs),
SW_X0       equ 4                   ; columns [x0, x1] (x0 on the 8-px grid),
SW_X1       equ 6                   ; and 1 = VOID in the new picture, a fill
SW_F        equ 8
SW_SIZE     equ 10
SW_TB       equ 1                   ; the schedule (ticks): B from here...
SW_TBN      equ 7                   ; ...over this many
SW_TC       equ 8                   ; C from here (the B bands' last tick + 1)
SW_TCN      equ 16                  ; ...over this many
SW_TD       equ 25                  ; and D
IT_GSWP     equ 95400               ; a sweep's windowed hold: 80 ms of 838 ns
IT_GSWP1    equ 47700               ; ...on a 1bpp window, 40 ms (the whole
                                    ; present's IT_GBAND): it keeps the pace on it
%if (SW_NB % SW_TBN) || (SW_TB + SW_TBN > SW_TC) || (SW_TC + SW_TCN > SW_TD)
  %error "the sweep's schedule: B whole bands a tick, and the stages in order"
%endif

; -----------------------------------------------------------------------------
; it_sw_off - AL = a tower height 1..4: AX = its level's offset, 24(h - 1)
; -----------------------------------------------------------------------------
it_sw_off:
    dec al
    mov ah, IT_LEVELDY
    mul ah
    ret

; -----------------------------------------------------------------------------
; it_sw_build - the next round's arena, the tower of [it_h] WITHOUT its
; outline, over the canvas, and the grid emptied for it: the two levels'
; offsets first (the old one is the last rebuild's height, the title's 1).
; When WAIT begins (it_flow), or at a game's first round - in a window by
; the worker outside its lock hold (itrules.inc it_bld_do), so the grid's
; ~35 ms and the rebuild's 0.3 - 0.45 s are no hold's. The glass still shows
; the old picture until the sweep presents the new one (it_wait_ol: whole).
; ES = the claim. Clobbers AX..DI, BP.
; -----------------------------------------------------------------------------
it_sw_build:
    call it_grid_reset
    mov al, [it_rbh]
    call it_sw_off
    mov [it_swoo], ax
    mov al, [it_h]
    call it_sw_off
    mov [it_swo], ax
    mov byte [it_rbnoe], 1
    mov al, [it_h]
    call it_rebuild
    mov byte [it_rbnoe], 0
    ret

; -----------------------------------------------------------------------------
; it_wait_ol - WAIT's canvas is the NEXT round's arena, built without its
; top outline (it_rbnoe) for the sweep to draw last. A present of it before
; Space - a toggle, a look, a relayout, a paint - writes the outline in
; first, so what the glass shows is a whole arena, never a slab with no
; boundary (SPEC.md 108.13.2); the sweep's D0 writes the same bytes again.
; Preserves every register.
; -----------------------------------------------------------------------------
it_wait_ol:
    cmp byte [it_state], ST_WAIT
    jne .out
    cmp byte [it_swpre], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call it_outline
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; it_sw_start - the sweep begins (it_round_start, after the rebuild and the
; start cells): the new level's offset, the bands' table (it_sw_tab), item 0,
; spans allowed again, ST_SWEEP, and the result line gone. A whole present
; already owed (a present still going out in bands, a relayout, a game
; started over a built or half-swept arena: it_game_start) means the glass is
; not the old picture the items assume, so the sweep ends at once and that
; present shows the arena. Clobbers AX; and AX..DI when it ends at once
; (it_sw_finish), which its one caller, it_round_start, pops round it.
; -----------------------------------------------------------------------------
it_sw_start:
    mov al, [it_h]
    call it_sw_off
    mov [it_swo], ax
    call it_sw_tab
    xor ax, ax
    mov [it_swk], ax
    mov [it_sgn], ax                ; no present in hand
    mov [it_sgs], ax
    mov [it_spno], al               ; (a refusal is asked again each sweep)
    mov byte [it_state], ST_SWEEP
    mov byte [it_msgd], 1
    cmp byte [it_full], 0
    je .out
    call it_sw_finish
.out:
    ret

; -----------------------------------------------------------------------------
; it_sw_finish - the sweep's end: the outline in the canvas if D0 has not
; written it, every item done, READY's 0.5 s, and in a bracket the steps'
; span hook back on the glass. From the last item, or early when a whole
; present is owed (a toggle, a look, a relayout: that present shows it all).
; Clobbers AX..DI.
; -----------------------------------------------------------------------------
it_sw_finish:
    cmp word [it_swk], SW_D0
    ja .o                           ; (D0 wrote it)
    call it_outline
.o:
    mov word [it_swk], SW_N
    mov ax, [it_sgn]                ; (a present in hand: dropped)
    mov [it_sgs], ax
    IT_MARK it_sw1
    inc word [it_nsweep]
    mov byte [it_state], ST_READY
    call OSAPI_GET_TICKS
    add ax, IT_READYT
    mov [it_tend], ax
    push si
    mov si, it_ph_ready             ; READY's three beeps (itsnd.inc)
    call it_snd_post
    pop si
    cmp byte [it_fsm], 0
    je .w
    mov ax, [it_fshook]             ; (EGA's is the box: it_fs_ega_geom)
    mov [it_spanhk], ax
.w:
    ret

; -----------------------------------------------------------------------------
; it_sw_due - out: AX = the items due now (SW_NA .. SW_N): every A at once,
; whole B bands SW_NB / SW_TBN a tick from SW_TB, C bands 5 / 2 a tick from
; SW_TC, and D at SW_TD. Clobbers CX, DX.
; -----------------------------------------------------------------------------
it_sw_due:
    call OSAPI_GET_TICKS
    sub ax, [it_swt0]
    mov dx, ax                      ; DX = ticks since the start
    mov ax, SW_N
    cmp dx, SW_TD
    jae .out                        ; (and a wrap or a long stall: all due)
    mov cx, SW_NA
    mov ax, dx
    sub ax, SW_TB
    jb .c
    shl ax, 1
    shl ax, 1                       ; SW_NB / SW_TBN = 4 a tick
%if SW_NB / SW_TBN != 4
  %error "it_sw_due multiplies by 4"
%endif
    cmp ax, SW_NB
    jbe .b
    mov ax, SW_NB
.b:
    add cx, ax
.c:
    mov ax, dx
    sub ax, SW_TC
    jb .d
    mov dx, ax
    shl ax, 1
    shl ax, 1
    add ax, dx
    shr ax, 1                       ; 5 / 2 a tick: SW_NC over SW_TCN
    cmp ax, SW_NC
    jbe .cc
    mov ax, SW_NC
.cc:
    add cx, ax
.d:
    mov ax, cx
.out:
    mov cx, [it_tswk]               ; THE HARNESS'S STOP (ittest.inc): no item
    jcxz .free                      ; past it_tswk is due while it is set
    cmp ax, cx
    jbe .free
    mov ax, cx
.free:
    ret

; -----------------------------------------------------------------------------
; it_sw_tab - THE BANDS' TABLE, once a sweep (it_sw_start): it_sw_bandc's
; seven row boundaries of every band, a byte each (logical rows 2 .. 198),
; at it_sbt + 7b. The sweep asks a band's geometry ~250 times and a paint
; ~280: computed each time it was ~2,900 cycles a call with the rects', 140 -
; 160 ms of an XT-VGA sweep (MEASURED, SPEC.md 108.13.4); looked up, ~500.
; Valid while it_swo and it_swoo are the sweep's. Preserves all but AX.
; -----------------------------------------------------------------------------
it_sw_tab:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    xor bx, bx
    mov word [it_sbp], it_sbt
.b:
    push bx
    call it_sw_bandc
    pop bx
    mov di, [it_sbp]
    xor si, si
.r:
    mov al, [it_sby + si]           ; (each word's low byte: 2 .. 198)
    mov [di], al
    inc di
    inc si
    inc si
    cmp si, 14
    jb .r
    mov [it_sbp], di
    inc bx
    cmp bx, SW_BANDS
    jb .b
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; it_sw_band - band BX from the table (it_sw_tab): its columns [it_sbx0,
; it_sbx1], and it_sbp = its seven row boundaries, a byte each, in
; it_sw_bandc's order (+0 R0, +1 T, +2 FL0, +3 FL1, +4 B0, +5 B1, +6 R1).
; Preserves all but AX.
; -----------------------------------------------------------------------------
it_sw_band:
    mov ax, bx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov [it_sbx0], ax
    add ax, 7
    cmp ax, IT_SLABX1
    jbe .x1
    mov ax, IT_SLABX1
.x1:
    mov [it_sbx1], ax
    mov ax, bx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    sub ax, bx                      ; 7b
    add ax, it_sbt
    mov [it_sbp], ax
    ret

; -----------------------------------------------------------------------------
; it_sw_bandc - band BX's geometry (SPEC.md 108.13.3), COMPUTED (it_sw_tab's):
; its columns [it_sbx0, it_sbx1] and seven row boundaries into it_sby, words,
; logical rows, even, non-decreasing:
;
;   [0] R0  the region's top: the higher of the two levels' tops in the band
;   [1] T   the new level's top - [R0, T) is VOID in the new picture
;   [2] FL0 the new floor's first row, [3] FL1 its end (equal: no floor)
;   [4] B0  the new front face's first row (the front outline's), [5] B1
;           its end (equal to R1: no front face in this band)
;   [6] R1  the region's end: the lower of the new level's bottom and the
;           old level's front outline
;
; with o = [it_swo] the new level's offset and oo = [it_swoo] the old's.
; Clobbers AX, CX, DX, SI, DI, BP.
; -----------------------------------------------------------------------------
it_sw_bandc:
    mov ax, bx
    mov cl, 3
    shl ax, cl
    mov [it_sbx0], ax               ; AX = x0 = 8b
    mov dx, ax
    add dx, 7
    cmp dx, IT_SLABX1
    jbe .x1
    mov dx, IT_SLABX1
.x1:
    mov [it_sbx1], dx               ; DX = x1
    ; --- the tops: 74 at x1 >= 100, else 174 - x1 (the left edge's highest)
    mov si, 74
    cmp dx, 100
    jae .t
    mov si, 174
    sub si, dx
.t:
    mov cx, si
    sub cx, [it_swo]                ; CX = the new top
    sub si, [it_swoo]               ; SI = the old one
    cmp si, cx
    jle .r0
    mov si, cx
.r0:
    and si, 0xFFFE
    mov [it_sby], si                ; R0 (down to even)
    and cx, 0xFFFE
    mov [it_sby + 2], cx            ; T (down: the rows above it are VOID)
    ; --- the bottoms: the new level's (the front face's end, or past x = 216
    ; the right face's, min(197, 413 - x0)) and the old level's front outline
    ; (175, or the right edge's 391 - x0), R1 the lower of the two, up to even
    mov cx, 197
    mov si, 175
    cmp ax, 217
    jb .b
    mov cx, 413
    sub cx, ax
    cmp cx, 197
    jle .bb
    mov cx, 197
.bb:
    mov si, 391
    sub si, ax
.b:
    sub cx, [it_swo]
    sub si, [it_swoo]
    cmp si, cx
    jle .r1
    mov cx, si
.r1:
    inc cx
    and cx, 0xFFFE
    mov [it_sby + 12], cx           ; R1
    ; --- the front face: [174, 198) to x = 216, else empty at R1 -----------
    mov bp, cx
    mov di, cx
    cmp ax, 216
    ja .f
    mov bp, 174
    sub bp, [it_swo]
    mov di, 198
    sub di, [it_swo]
.f:
    ; --- the floor: [max(97, 197 - x1), min(174, 390 - x0)) to x0 = 292,
    ; down to even; else, or when it is empty, nothing - at B0
    mov si, bp
    mov cx, bp
    cmp ax, 292
    ja .fl
    mov si, 197
    sub si, dx
    cmp si, 97
    jge .f0
    mov si, 97
.f0:
    mov cx, 390
    sub cx, ax
    cmp cx, 174
    jle .f1
    mov cx, 174
.f1:
    sub si, [it_swo]
    sub cx, [it_swo]
    and si, 0xFFFE
    and cx, 0xFFFE
    cmp si, cx
    jle .fl
    mov si, cx
.fl:
    mov [it_sby + 4], si            ; FL0
    mov [it_sby + 6], cx            ; FL1
    mov [it_sby + 8], bp            ; B0
    mov [it_sby + 10], di           ; B1
    ret


; -----------------------------------------------------------------------------
; it_sw_rects - item AX's rects into it_swr, in FIXED SLOTS (an empty one has
; y0 = y1 = 0), it_swrn = the slots' bytes: an A, B or C item ONE slot - A's
; pass p's rows of its band (pass 0 [R0, T) VOID, 1 [T, FL0), 2 [FL1, B0),
; 3 [B1, R1)), B's [B0, B1) - empty when the old and new levels are one
; level (a round at the same height, the title's level 1), the front face
; being the same pixels on the glass already - C's [FL0, FL1); D0 two, the
; back and the front edge; D1..D13 two, a band of each diagonal. Only the
; item's own slots are written. Clobbers AX..DI, BP.
; -----------------------------------------------------------------------------
it_sw_rects:
    mov word [it_swrn], SW_SIZE
    cmp ax, SW_D0
    jae .d
    mov bx, ax                      ; a band's: A, B or C
    cmp bx, SW_B0
    jb .a
    sub bx, SW_B0
    cmp bx, SW_NB
    jb .bb
    sub bx, SW_NB                   ; C: [FL0, FL1)
    call it_sw_band
    mov si, [it_sbp]
    add si, 2
    jmp short .one
.bb:
    mov ax, [it_swo]                ; B: [B0, B1) - or nothing, one level
    cmp ax, [it_swoo]
    je .none
    call it_sw_band
    mov si, [it_sbp]
    add si, 4
.one:
    xor di, di
    xor dl, dl
    jmp it_sw_add
.none:
    xor ax, ax
    mov [it_swr + SW_Y0], ax
    mov [it_swr + SW_Y1], ax
    ret
.a:
    ; A: pass p = item / 40 over band b = item % 40, one slot - pass 0 [R0, T)
    ; VOID, 1 [T, FL0), 2 [FL1, B0), 3 [B1, R1): it_sbp + it_swap[p]
    mov ax, bx
    mov dl, SW_BANDS
    div dl
    mov bl, ah
    xor bh, bh                      ; BX = the band
    mov dl, al                      ; DL = the pass
    call it_sw_band
    mov al, dl
    xor ah, ah
    mov si, ax
    mov al, [it_swap + si]
    mov si, [it_sbp]
    add si, ax
    xor di, di
    cmp dl, 1                       ; (pass 0: VOID, a fill)
    sbb dl, dl
    neg dl
    jmp it_sw_add
.d:
    ; --- D0: its two straight edges: the back one, row 74 from x = 100, and
    ; the front, row 174 to x = 216 - each a row pair (the full-screen
    ; presenters' unit). The outline is written by whoever presents it
    mov word [it_swrn], 2 * SW_SIZE
    xor di, di
    sub ax, SW_D0
    jnz .diag
    mov ax, 74
    mov cx, 100
    mov dx, IT_SLABX1
    call it_sw_edge
    mov ax, 174
    xor cx, cx
    mov dx, 216
    jmp short it_sw_edge
.diag:
    ; --- D1..D13: band k - 1 of the left edge (x = 174 - t, to x = 99) and
    ; band 26 + k of the right (x = 390 - t, from x = 217), 8 rows each
    mov bx, ax
    dec bx                          ; BX = band k - 1
    call it_sw_band
    mov cx, [it_sbx0]
    mov dx, [it_sbx1]
    cmp dx, 99
    jbe .l1
    mov dx, 99
.l1:
    mov ax, 174                     ; rows [174 - x1, 175 - x0)
    sub ax, dx
    mov si, 175
    sub si, cx
    xor di, di
    call it_sw_diag
    add bx, 26 + 1                  ; band 26 + k
    call it_sw_band
    mov cx, [it_sbx0]
    cmp cx, 217
    jae .r0
    mov cx, 217
.r0:
    mov dx, [it_sbx1]
    mov ax, 390                     ; rows [390 - x1, 391 - x0)
    sub ax, dx
    mov si, 391
    sub si, cx
    mov di, SW_SIZE
    ; (falls into it_sw_diag)

; it_sw_diag - one diagonal band into slot DI: rows [AX, SI) less o, AX down
; and SI up to even, columns [CX, DX]. Preserves BX.
it_sw_diag:
    sub ax, [it_swo]
    sub si, [it_swo]
    and ax, 0xFFFE
    inc si
    and si, 0xFFFE
    jmp short it_sw_put4

; it_sw_edge - a straight edge into slot DI (and DI on): rows [AX - o,
; AX - o + 2), columns [CX, DX]
it_sw_edge:
    sub ax, [it_swo]
    mov si, ax
    add si, 2
    call it_sw_put4
    add di, SW_SIZE
    ret

; it_sw_put4 - slot DI: rows [AX, SI), columns [CX, DX], not VOID
it_sw_put4:
    mov [it_swr + di + SW_Y0], ax
    mov [it_swr + di + SW_Y1], si
    mov [it_swr + di + SW_X0], cx
    mov [it_swr + di + SW_X1], dx
    mov byte [it_swr + di + SW_F], 0
    ret

; A's passes: each one's rows in the band's table entry, [+n, +n + 1]
it_swap:    db 0, 1, 3, 5

; it_sw_add - slot DI (and DI on): rows [[SI], [SI + 1]) of the band
; it_sw_band looked up (bytes), its columns, DL = 1 VOID; EMPTY (y0 = y1 =
; 0) when there are none. Preserves DL, BX.
it_sw_add:
    xor ah, ah
    mov al, [si]
    cmp al, [si + 1]
    jae .no
    mov [it_swr + di + SW_Y0], ax
    mov al, [si + 1]
    mov [it_swr + di + SW_Y1], ax
    mov ax, [it_sbx0]
    mov [it_swr + di + SW_X0], ax
    mov ax, [it_sbx1]
    mov [it_swr + di + SW_X1], ax
    mov [it_swr + di + SW_F], dl
    add di, SW_SIZE
    ret
.no:
    xor ax, ax
    mov [it_swr + di + SW_Y0], ax
    mov [it_swr + di + SW_Y1], ax
    add di, SW_SIZE
    ret

; -----------------------------------------------------------------------------
; it_sw_group - THE NEXT PRESENT: the items due from [it_swk], in one stage
; and at most SW_GMAX of them, merged slot by slot into it_sgr (it_sgn the
; slots' bytes), it_swk past them; D0's outline written into the canvas
; first. out: CF = 1 none is due. Clobbers AX..DI, BP.
;
; WHY MERGE: a band is 8 logical px, 2 device bytes, and a present that
; narrow and that tall is mostly the presenters' cost a ROW - MEASURED on
; MartyPC's XT-VGA, ~660 cycles a device byte against ~330 for a row 16 bytes
; wide or more (SPEC.md 108.13.4). The schedule still releases 8-px bands,
; left to right; what is due together goes out together, so a surface that
; falls behind presents wider and catches up.
;
; HOW: the B, C and D slots and A's last (below the front face) are UNIONED
; - the rows a band adds to its neighbour's columns are rows the sweep has
; already presented there (A before B and C, all of them before D) or rows
; the two pictures share, so they go out again unchanged, never early. A's
; first three border a later stage or are a fill, so they merge only while
; their rows are the same band to band (the back rim's run, x >= 100): a
; union there would show the floor, or fill a wall, before its time.
;
; it_sw_grpl is the same with the limit in CX instead of the items due: a
; paint's replay of the items taken (it_sw_paint), which merges them as the
; sweep did.
; -----------------------------------------------------------------------------
it_sw_group:
    call it_sw_due
    mov cx, ax                      ; CX = the limit: due ...
it_sw_grpl:
    mov ax, [it_swk]
    cmp ax, cx
    jb .some
    stc
    ret
.some:
    ; ... this stage's end, and how it merges: A's passes 0 - 2 EXACT (the
    ; same rows band to band, no cap: such a run wastes nothing), pass 3, B
    ; and C a UNION of at most SW_GMAX, D0 alone and each diagonal band alone
    ; (a union of diagonal boxes is mostly a triangle of nothing)
    mov bx, ax
    mov dl, 1                       ; DL = 1 exact
    mov si, 0x7FFF                  ; SI = the cap
    cmp ax, 3 * SW_BANDS
    jae .np
    push ax                         ; an exact pass: to its end
    push dx
    mov dl, SW_BANDS
    div dl
    inc al
    mul dl
    mov bx, ax
    pop dx
    pop ax
    jmp short .e
.np:
    xor dl, dl
    mov si, SW_GMAX
    mov bx, SW_NA
    cmp ax, bx
    jb .e
    mov bx, SW_C0
    cmp ax, bx
    jb .e
    mov bx, SW_D0
    cmp ax, bx
    jb .e
    mov si, 1
    mov bx, SW_N
.e:
    cmp bx, cx
    jae .e1
    mov cx, bx
.e1:
    mov bx, ax                      ; ... and the cap on
    add bx, si
    cmp bx, cx
    jae .e2
    mov cx, bx
.e2:
    mov [it_sglim], cx
    mov [it_sgx], dl
    cmp ax, SW_D0
    jne .f
    push ax
    call it_outline                 ; the outline, then its edges on the glass
    pop ax
.f:
    push ax
    call it_sw_rects
    mov si, it_swr
    mov di, it_sgr
    mov cx, [it_swrn]
    mov [it_sgn], cx
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    pop ax
.item:
    inc ax
    cmp ax, [it_sglim]
    jae .done
    push ax
    call it_sw_rects
    xor si, si                      ; the exact slots must match
    mov dl, [it_sgx]
.ck:
    shr dl, 1
    jnc .cn
    mov ax, [it_swr + si + SW_Y0]
    cmp ax, [it_sgr + si + SW_Y0]
    jne .stop
    mov ax, [it_swr + si + SW_Y1]
    cmp ax, [it_sgr + si + SW_Y1]
    jne .stop
.cn:
    add si, SW_SIZE
    cmp si, [it_sgn]
    jb .ck
    xor si, si                      ; then every slot takes the band
.mg:
    mov ax, [it_swr + si + SW_Y0]
    cmp ax, [it_swr + si + SW_Y1]
    jge .mn                         ; (an empty band adds nothing)
    mov dx, [it_sgr + si + SW_Y0]
    cmp dx, [it_sgr + si + SW_Y1]
    jl .un
    mov [it_sgr + si + SW_Y0], ax   ; the group's slot was empty: this one
    mov ax, [it_swr + si + SW_Y1]
    mov [it_sgr + si + SW_Y1], ax
    mov ax, [it_swr + si + SW_X0]
    mov [it_sgr + si + SW_X0], ax
    mov al, [it_swr + si + SW_F]
    mov [it_sgr + si + SW_F], al
    jmp short .x1
.un:
    cmp ax, dx                      ; the union: rows widen, columns run on
    jge .u1
    mov [it_sgr + si + SW_Y0], ax
.u1:
    mov ax, [it_swr + si + SW_Y1]
    cmp ax, [it_sgr + si + SW_Y1]
    jle .x1
    mov [it_sgr + si + SW_Y1], ax
.x1:
    mov ax, [it_swr + si + SW_X1]
    mov [it_sgr + si + SW_X1], ax
.mn:
    add si, SW_SIZE
    cmp si, [it_sgn]
    jb .mg
    pop ax
    jmp .item
.stop:
    pop ax
.done:
    mov [it_swk], ax
    clc
    ret

; -----------------------------------------------------------------------------
; it_sw_put - the rect at BX (an it_swr / it_sgr slot) on the glass, minus the
; overlays' text: the window (and EGA's bracket, which borrows its geometry)
; through it_pbox - device rows by the row map cut to [it_swc0, it_swc1),
; bytes x / 4, a VOID one as a fill, whole - and a direct bracket through
; it_fs_area, cells x / 8, whole. out: CF = 1 a blitp refused; it_swdn = 1
; when the rect's last device row is out. Clobbers all but DS.
; -----------------------------------------------------------------------------
it_sw_put:
    mov byte [it_swdn], 1
    mov ax, [bx + SW_Y0]
    cmp ax, [bx + SW_Y1]
    jge .none                       ; an empty slot
    cmp byte [it_fsm], 0
    je .win
    cmp byte [it_fsega], 0
    je .fs
.win:
    mov si, [bx + SW_Y0]
    shl si, 1
    mov ax, [it_rfirst + si]
    mov si, [bx + SW_Y1]
    shl si, 1
    mov dx, [it_rfirst + si]        ; AX, DX = device rows [r0, r1)
    mov cl, [bx + SW_F]
    mov [it_pfill], cl
    or cl, cl
    jnz .rows                       ; (a fill goes whole)
    cmp ax, [it_swc0]
    jge .c0
    mov ax, [it_swc0]
.c0:
    cmp dx, [it_swc1]
    jle .rows
    mov dx, [it_swc1]
    mov byte [it_swdn], 0           ; more of it below
.rows:
    mov [it_pr0], ax
    mov [it_pr1], dx
    mov ax, [bx + SW_X0]
    shr ax, 1
    shr ax, 1
    mov [it_pj0], ax
    mov ax, [bx + SW_X1]
    shr ax, 1
    shr ax, 1
    mov [it_pj1], ax
    call it_sw_clipd                ; (a paint: its damage, else nothing)
    jc .clr
    IT_MARK it_swp0
    call it_pbox
    IT_MARK it_swp1
    mov byte [it_pfill], 0          ; (mov writes no flag: CF comes out)
    ret
.clr:
    mov byte [it_pfill], 0
.none:
    clc
    ret
.fs:
    mov ax, [bx + SW_Y0]
    mov [it_fa0], ax
    mov ax, [bx + SW_Y1]
    mov [it_fa1], ax
    mov ax, [bx + SW_X0]
    mov cl, 3
    shr ax, cl
    mov [it_fac0], ax
    mov ax, [bx + SW_X1]
    shr ax, cl
    inc ax
    mov [it_fac1], ax
    mov al, [bx + SW_F]             ; (a VOID slot: it_fs_rect fills)
    mov [it_pfill], al
    call it_fs_area
    mov byte [it_pfill], 0
    clc
    ret

; it_sw_clipd - in a paint (it_dmgon), the device rect it_pr0..it_pj1 cut to
; the damage it_sdr: CF = 1 nothing left. Outside a paint CF = 0 unless the
; rows are empty. Clobbers AX.
it_sw_clipd:
    cmp byte [it_dmgon], 0
    je .ck
    mov ax, [it_sdr]                ; rows [r0, r1)
    cmp [it_pr0], ax
    jge .a
    mov [it_pr0], ax
.a:
    mov ax, [it_sdr + 2]
    cmp [it_pr1], ax
    jle .b
    mov [it_pr1], ax
.b:
    mov ax, [it_sdr + 4]            ; bytes [j0, j1]
    cmp [it_pj0], ax
    jge .c
    mov [it_pj0], ax
.c:
    mov ax, [it_sdr + 6]
    cmp [it_pj1], ax
    jle .d
    mov [it_pj1], ax
.d:
    mov ax, [it_pj0]
    cmp ax, [it_pj1]
    jg .no
.ck:
    mov ax, [it_pr0]
    cmp ax, [it_pr1]
    jge .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; it_sw_item - item [it_swk] alone (a paint's replay, a hidden window's skip):
; D0's canvas write (the outline), then, AL not 0, its rects whole on the
; glass; it_swk on by one. out: CF = 1 a blitp refused. Clobbers all but DS.
; -----------------------------------------------------------------------------
it_sw_item:
    push ax
    mov ax, [it_swk]
    inc word [it_swk]
    cmp ax, SW_D0
    jne .r
    push ax
    call it_outline
    pop ax
.r:
    call it_sw_rects
    pop ax
    or al, al
    jz .ok
    mov word [it_swc0], 0
    mov word [it_swc1], 0x7FFF
    xor si, si
.p:
    cmp si, [it_swrn]
    jae .ok
    push si
    lea bx, [it_swr + si]
    call it_sw_put
    pop si
    jc .out
    add si, SW_SIZE
    jmp short .p
.ok:
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; it_sw_run - the sweep's presents, from the worker (the lock held, the clip
; and the surface ready, SPEC.md 108.11.4) or a bracket frame: group after
; group (it_sw_group) of the items due, each slot of a group in turn. In a
; window a slot goes out a PIECE (~SW_PIECEB device bytes) at a time, and a
; hold goes on to another piece while the hold so far plus the next one's
; price - the last iteration's cost (the piece AND the group building or D
; item before it, it_bt0 being stamped once an iteration), and never under
; half the budget - stays under the budget (IT_GSWP on a colour window,
; IT_GSWP1 on a 1bpp one); a wake that comes in with
; half the budget already spent (a round's start, its rebuild) presents
; nothing and yields. Work left over that is due makes the worker yield. A
; bracket has no lock to let go and takes every group due, whole. The last
; ends the sweep. out: CF = 1 a blitp refused. Clobbers all but DS.
; -----------------------------------------------------------------------------
it_sw_run:
    cmp byte [it_fsm], 0
    jne .next                       ; (a bracket: no hold to budget)
    mov ax, IT_GSWP / 2             ; THE BUDGET, in half counts (1.68 us)
    cmp byte [it_surf], 0
    jne .bg
    mov ax, IT_GSWP1 / 2
.bg:
    mov [it_swbud], ax
    call it_sw_held                 ; AX = the hold so far
    shl ax, 1
    jc .more
    cmp ax, [it_swbud]
    ja .more                        ; (over half of it already: next wake)
    call pit_now
    mov [it_bt0], ax
    mov [it_bt0 + 2], dx
.next:
    mov si, [it_sgs]
    cmp si, [it_sgn]
    jb .have
    cmp word [it_swk], SW_N         ; every group out: the sweep's end
    jae .fin
    cmp word [it_swk], SW_D0        ; THE OUTLINE AS SPANS (it_sw_dspan),
    jb .grp                         ; where the surface takes them
    call it_sw_due
    cmp [it_swk], ax
    jae .idle
    cmp byte [it_spno], 0           ; (spans refused this sweep: rects)
    jne .grp
    cmp byte [it_fsm], 0
    je .dw
    cmp byte [it_fsega], 0
    jne .dsp
    jmp short .grp
.dw:
    cmp byte [it_pok], 0
    je .grp
.dsp:
    mov ax, [it_swk]
    inc word [it_swk]
    push ax
    call it_sw_dspan
    pop ax
    cmp byte [it_spno], 0
    je .dok
    ; GFX_SPANS REFUSED (a second display, kern_small: SPEC.md 5.10) and
    ; nothing was drawn: the item goes again as rects - D0 itself, or, a
    ; diagonal's half refused, D1 on (as rects D1..D13 hold both diagonals
    ; band by band)
    cmp ax, SW_D0
    je .drw
    mov ax, SW_D0 + 1
.drw:
    mov [it_swk], ax
    jmp .next
.dok:
    jmp .room
.grp:
    call it_sw_group
    jc .idle                        ; (nothing due yet: wait for the tick)
    mov word [it_sgs], 0
    mov word [it_sgy], 0
    jmp .next
.have:
    cmp byte [it_fsm], 0
    je .w
    cmp byte [it_fsega], 0
    jne .w
    lea bx, [it_sgr + si]           ; A BRACKET: the slot whole
    call it_sw_put
    add word [it_sgs], SW_SIZE
    jmp .next
.w:
    mov si, [it_sgs]                ; A WINDOW: a piece of the slot
    lea bx, [it_sgr + si]
    mov si, [bx + SW_Y0]            ; the slot's first device row, if it is
    shl si, 1                       ; only starting
    mov ax, [it_rfirst + si]
    cmp ax, [it_sgy]
    jge .y
    mov ax, [it_sgy]
.y:
    mov [it_swc0], ax
    push ax                         ; a piece of ~SW_PIECEB device bytes: a
    mov ax, [bx + SW_X1]            ; call's fixed part is ~6,900 cycles and a
    shr ax, 1                       ; row's ~475 whatever its width (MEASURED,
    shr ax, 1                       ; the XT-VGA: SPEC.md 108.13.4), so a
    mov cx, [bx + SW_X0]            ; piece is as many rows as its bytes allow
    shr cx, 1
    shr cx, 1
    sub ax, cx
    inc ax                          ; AX = its device bytes across
    mov cx, ax
    mov ax, SW_PIECEB
    xor dx, dx
    div cx
    cmp ax, SW_PIECEMIN
    jae .pr
    mov ax, SW_PIECEMIN
.pr:
    mov cx, ax
    pop ax
    add ax, cx
    mov [it_swc1], ax
    mov [it_sgy], ax
    call it_sw_put
    jc .out
    cmp byte [it_swdn], 0
    je .room
    add word [it_sgs], SW_SIZE      ; the slot is out: the next from its top
    mov word [it_sgy], 0
.room:
    cmp byte [it_fsm], 0
    jne .next                       ; (EGA's bracket: no lock to let go)
    call pit_now                    ; another iteration, if the hold has room
    push ax                         ; for one more like the last (the band
    push dx                         ; rule), in HALF counts: the budget
    sub ax, [it_bt0]                ; passes 16 bits
    sbb dx, [it_bt0 + 2]
    shr dx, 1
    rcr ax, 1
    or dx, dx
    jz .lp
    mov ax, 0xFFFF
.lp:
    mov si, [it_swbud]              ; SI = the next iteration's price: the
    shr si, 1                       ; last one's, and never under half the
    cmp si, ax                      ; budget - a fill or an empty group
    jae .lw                         ; prices the next piece at nothing, and
    mov si, ax                      ; a full band's piece after it was the
.lw:                                ; 110 ms hold (MEASURED, the XT-VGA)
    call it_sw_held                 ; AX = the hold so far
    pop dx
    pop cx
    add ax, si
    jc .more
    cmp ax, [it_swbud]
    ja .more
    mov [it_bt0], cx                ; (the next iteration starts now)
    mov [it_bt0 + 2], dx
    jmp .next
.more:
    mov si, [it_sgs]                ; due work left: yield, not sleep
    cmp si, [it_sgn]
    jb .yield
    call it_sw_due
    cmp [it_swk], ax
    jae .idle
.yield:
    mov byte [it_byield], 1
.idle:
    clc
.out:
    ret
.fin:
    call it_sw_finish
    clc
    ret

; it_sw_held - AX = the worker's hold so far (pit_now - it_gt0), in half
; counts, 0xFFFF past 16 bits. Clobbers DX.
it_sw_held:
    call pit_now
    sub ax, [it_gt0]
    sbb dx, [it_gt0 + 2]
    shr dx, 1
    rcr ax, 1
    or dx, dx
    jz .out
    mov ax, 0xFFFF
.out:
    ret

; it_sw_skip - nothing of the window shows: the due items are done with
; nothing presented (the uncover's paint presents them, it_sw_paint), D0's
; canvas write included, and the group in hand dropped; the last ends the
; sweep. Clobbers all but DS.
it_sw_skip:
    mov ax, [it_sgn]
    mov [it_sgs], ax
.l:
    call it_sw_due
    cmp [it_swk], ax
    jae .x
    xor al, al
    call it_sw_item
    jmp short .l
.x:
    cmp word [it_swk], SW_N
    jb .out
    call it_sw_finish
.out:
    ret

; -----------------------------------------------------------------------------
; it_sw_dspan - D item AX as SPANS (OSAPI_GFX_SPANS, SPEC.md 5.10), where the
; surface takes them: a colour window with nothing over it (it_pok: the clip
; cleared - gfx_spans refuses an armed one) and EGA's bracket. The outline is
; one id, EDGE, one colour in every 16-colour look, and one pixel wide - a
; rect present of it is mostly the presenter's fixed and per-row costs (the
; D items were ~0.3 s of the XT-VGA's sweep); a span is a row of it. D0
; writes the outline into the canvas and draws its two straight edges, D1
; and D2 the left diagonal's two halves, D3 and D4 the right's - a whole
; diagonal was one ~58 ms item on the XT-VGA (MEASURED), which no hold's
; budget can split - and D5..D13 have nothing left to do. No
; overlay meets the outline while the sweep runs (the HUD's corners, SPEC.md
; 108.11.5; no message line). Clobbers all but DS.
; -----------------------------------------------------------------------------
it_sw_dspan:
    push ax
    mov si, [it_lookp]
    mov al, [si + ID_EDGE]
    mov cl, 4
    shr al, cl
    call OSAPI_SET_COLOR
    pop ax
    sub ax, SW_D0
    jz .edges
    dec ax
    cmp ax, 4
    jb .diag
    ret
.edges:
    call it_outline
    mov ax, 74                      ; the back edge: x 100 .. 316
    mov bx, 100
    mov cx, IT_SLABX1
    call .h
    mov ax, 174                     ; the front: x 0 .. 216
    xor bx, bx
    mov cx, 216
.h:
    sub ax, [it_swo]
    mov [it_spc], bx                ; (x = BX on every row: "c - y" with
    mov [it_spc2], cx               ; the slope 0)
    mov cx, 1
    mov dx, 0
    jmp short it_sw_dg
.diag:
    mov cx, 50                      ; AX = 0 .. 3: half AX & 1 of diagonal
    test al, 1                      ; AX >> 1 - 50 rows of t from its start
    mov dx, 0                       ; (mov: the flags are test's)
    jz .dh
    mov dx, cx
.dh:
    test al, 2
    jnz .right
    mov ax, 75                      ; the left: x = 174 - t, t = 75 .. 174
    mov bx, 174
    jmp short .d
.right:
    mov ax, 74                      ; the right: x = 390 - t, t = 74 .. 173
    mov bx, 390
.d:
    add ax, dx
    sub ax, [it_swo]
    sub bx, [it_swo]
    mov [it_spc], bx
    mov dx, 1
    ; (falls into it_sw_dg: CX = 50 rows)

; it_sw_dg - logical rows [AX, AX + CX): DX = 1 a diagonal, the pixel at x =
; [it_spc] - y; DX = 0 a row from x = [it_spc] to [it_spc2]. Every device row
; each one shows, one span, in lists of SW_SPN.
it_sw_dg:
    mov [it_spy], ax
    mov [it_spk], cx
    mov [it_spd], dl
    mov bx, [it_pwb]                ; THE PICTURE'S RIGHT EDGE: gfx_spans
    shl bx, 1                       ; clips nothing and the clip is cleared,
    shl bx, 1                       ; so column 316 is cut here when the
    shl bx, 1                       ; picture is 79 bytes (SPEC.md 108.11.2),
    add bx, [it_ox]                 ; as it_pbox cuts it from a rect
    dec bx
    mov [it_spxm], bx
    mov bx, ax
    shl bx, 1
    mov ax, [it_rfirst + bx]
    mov [it_spr0], ax               ; the first device row
    mov word [it_spn], 0
.y:
    mov bx, [it_spy]
    mov ax, [it_spc]                ; the x interval, device px, absolute
    mov dx, [it_spc2]
    cmp byte [it_spd], 0
    je .xx
    sub ax, bx
    mov dx, ax
.xx:
    shl ax, 1
    add ax, [it_ox]
    shl dx, 1
    add dx, [it_ox]
    inc dx
    cmp dx, [it_spxm]
    jle .xm
    mov dx, [it_spxm]
.xm:
    shl bx, 1
    mov cx, [it_rfirst + bx + 2]
    sub cx, [it_rfirst + bx]        ; CX = the device rows showing y (0 .. 2)
    jcxz .ny
    cmp ax, dx                      ; (wholly past the edge: x1 > x2, which
    jle .r                          ; gfx_spans takes as an empty row - one
    mov ax, dx                      ; for each device row all the same, the
    inc ax                          ; list's rows being consecutive)
.r:
    mov di, [it_spn]
    cmp di, SW_SPN * 4
    jb .a
    push ax
    push cx
    push dx
    call it_sw_flush
    pop dx
    pop cx
    pop ax
    xor di, di
.a:
    mov [it_spl + di], ax
    mov [it_spl + di + 2], dx
    add word [it_spn], 4
    loop .r
.ny:
    inc word [it_spy]
    dec word [it_spk]
    jnz .y
    ; (falls into it_sw_flush: the rest)

; it_sw_flush - the list so far as one OSAPI_GFX_SPANS from device row
; [it_spr0], which moves on past it. A REFUSAL (CF = 1: a second display or
; kern_small - it_pok has ruled out an armed clip) draws nothing, and the API
; says it must be read: it_spno = 1, the rest of the item's lists are
; dropped, and it_sw_run presents the item as rects instead (os88api.inc's
; fallback is a gfx_fill a span: ~240 of them for a diagonal on the VGA,
; where the rects are 13 presents). Clobbers AX, CX, SI.
it_sw_flush:
    mov cx, [it_spn]
    shr cx, 1
    shr cx, 1
    jcxz .out
    mov ax, [it_spr0]
    add [it_spr0], cx
    add ax, [it_oy]
    cmp byte [it_spno], 0
    jne .drop
    mov si, it_spl
    call OSAPI_GFX_SPANS            ; (ES: the stub puts our DS there)
    jnc .drop
    mov byte [it_spno], 1
.drop:
    mov word [it_spn], 0
.out:
    ret

; -----------------------------------------------------------------------------
; it_sw_paint - W_PAINT during a sweep (itwin.inc it_paint_rect): the damage,
; device rows [it_pr0, it_pr1) by bytes [it_pj0, it_pj1], presented from the
; canvas OUTSIDE the sweep's region - each band's rows above and below it,
; where the old picture and the new are one - and inside it only where an
; item already taken presents its rects: "the rest is owed to the sweep"
; (ISOTRON-PLAN 7.2), the canvas there being the NEW arena already and the
; glass around it still the old. out: CF = 1 a blitp refused. Clobbers all
; but DS.
;
; PRICED LIKE THE SWEEP ITSELF (SPEC.md 108.13.3): only the bands the damage
; meets (a band is 2 device bytes); the rows above the region are VOID in
; both pictures and go out as a FILL; the outside rects of neighbouring bands
; with the same rows go out as ONE (R0 is one value from x = 100 on, R1 one
; value to x = 216: ~26 presents, not 80 - 120 two bytes wide); and the items
; taken are replayed MERGED, by it_sw_grpl, the way the sweep presented them
; - each stage's run of them over the damage's bands only.
; -----------------------------------------------------------------------------
it_sw_paint:
    mov ax, [it_pr0]
    mov [it_sdr], ax
    mov ax, [it_pr1]
    mov [it_sdr + 2], ax
    mov ax, [it_pj0]
    mov [it_sdr + 4], ax
    mov ax, [it_pj1]
    mov [it_sdr + 6], ax
    mov word [it_swc0], 0           ; (every rect whole: the damage cuts it)
    mov word [it_swc1], 0x7FFF
    ; --- THE DAMAGE'S BANDS [bd0, bd1] -------------------------------------
    mov dx, [it_pj1]
    or dx, dx
    js .none                        ; (left of the picture)
    shr dx, 1
    cmp dx, SW_BANDS - 1
    jbe .d1
    mov dx, SW_BANDS - 1
.d1:
    mov ax, [it_pj0]
    or ax, ax
    jns .d0
    xor ax, ax
.d0:
    shr ax, 1
    cmp ax, dx
    ja .none
    mov [it_sbd0], ax
    mov [it_sbd1], dx
    ; --- OUTSIDE THE REGION the old picture and the new are the same pixels
    ; (it_sw_bandc), so the damage there is the canvas: three runs - above
    ; the region [0, R0), VOID; below it [R1, 200); and at one level the
    ; front face [B0, B1), the same pixels in both pictures and no item's
    ; (it_sw_rects' B) - each band extending the run whose rows are its own
    xor ax, ax
    mov [it_swr + SW_Y0], ax                ; (every run empty)
    mov [it_swr + SW_Y1], ax
    mov [it_swr + SW_SIZE + SW_Y0], ax
    mov [it_swr + SW_SIZE + SW_Y1], ax
    mov [it_swr + 2 * SW_SIZE + SW_Y0], ax
    mov [it_swr + 2 * SW_SIZE + SW_Y1], ax
    mov bx, [it_sbd0]
.bnd:
    mov [it_sbb], bx
    call it_sw_band
    mov si, [it_sbp]
    xor ax, ax                              ; [0, R0), a fill
    mov dl, [si]
    xor dh, dh
    mov cl, 1
    xor di, di
    call it_sw_prun
    jc .out
    mov si, [it_sbp]                        ; [R1, 200)
    mov al, [si + 6]
    xor ah, ah
    mov dx, IT_CROWS
    xor cl, cl
    mov di, SW_SIZE
    call it_sw_prun
    jc .out
    xor ax, ax                              ; [B0, B1) at one level, else
    xor dx, dx                              ; nothing
    mov cx, [it_swo]
    cmp cx, [it_swoo]
    jne .f
    mov si, [it_sbp]
    mov al, [si + 4]
    mov dl, [si + 5]
.f:
    xor cl, cl
    mov di, 2 * SW_SIZE
    call it_sw_prun
    jc .out
    mov bx, [it_sbb]
    inc bx
    cmp bx, [it_sbd1]
    jbe .bnd
    xor si, si                              ; the three runs' last rects
.fl:
    push si
    lea bx, [it_swr + si]
    call it_sw_put
    pop si
    jc .out
    add si, SW_SIZE
    cmp si, 3 * SW_SIZE
    jb .fl
    ; --- AND INSIDE IT, what the items already taken present, merged: the
    ; sweep's own group state is set aside for it and put back after
    mov si, it_sgr
    mov di, it_sgsv
    mov cx, it_sgx + 1 - it_sgr
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    push word [it_swk]
    mov ax, [it_swk]
    mov [it_sbk], ax                ; the items taken: [0, sbk)
    call it_sw_preplay
    pop word [it_swk]
    pushf
    mov si, it_sgsv
    mov di, it_sgr
    mov cx, it_sgx + 1 - it_sgr
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    popf
    jc .out
.none:
    inc word [it_nswp]              ; (the harness counts them; inc keeps CF)
    clc
.out:
    ret

; it_sw_prun - an outside run (it_sw_paint): slot DI of it_swr, the band
; it_sw_band looked up, its rows [AX, DX), CL = 1 VOID. The same rows as the
; run's: the run takes the band's columns; else the run goes out (it_sw_put;
; an empty one is nothing) and the band starts the next. out: CF = 1 a blitp
; refused. Clobbers all but DS.
it_sw_prun:
    cmp ax, [it_swr + di + SW_Y0]
    jne .new
    cmp dx, [it_swr + di + SW_Y1]
    jne .new
    mov ax, [it_sbx1]
    mov [it_swr + di + SW_X1], ax
    clc
    ret
.new:
    push ax
    push cx
    push dx
    push di
    lea bx, [it_swr + di]
    call it_sw_put
    pop di
    pop dx
    pop cx
    pop ax
    jc .out
    mov [it_swr + di + SW_Y0], ax
    mov [it_swr + di + SW_Y1], dx
    mov [it_swr + di + SW_F], cl
    mov ax, [it_sbx0]
    mov [it_swr + di + SW_X0], ax
    mov ax, [it_sbx1]
    mov [it_swr + di + SW_X1], ax
    clc
.out:
    ret

; it_sw_preplay - the items taken, [0, it_sbk), that the damage's bands
; [it_sbd0, it_sbd1] meet, merged by it_sw_grpl and put out whole: each of
; A's four passes over those bands, B's (to band 27), C's, and every D item
; (D0's edges cross every band; a diagonal band's two rects are cut by the
; damage). out: CF = 1 a blitp refused. Clobbers all but DS.
it_sw_preplay:
    xor cx, cx                      ; A: pass p's items 40p + [bd0, bd1]
.pa:
    push cx
    mov ax, cx
    add ax, [it_sbd0]
    add cx, [it_sbd1]
    inc cx
    call .seg
    pop cx
    jc .out
    add cx, SW_BANDS
    cmp cx, SW_NA
    jb .pa
    mov ax, [it_sbd0]               ; B: bands [bd0, min(bd1, 27)]
    cmp ax, SW_NB
    jae .c
    mov cx, [it_sbd1]
    cmp cx, SW_NB - 1
    jbe .b1
    mov cx, SW_NB - 1
.b1:
    add ax, SW_B0
    add cx, SW_B0 + 1
    call .seg
    jc .out
.c:
    mov ax, [it_sbd0]               ; C: bands [bd0, bd1]
    mov cx, [it_sbd1]
    add ax, SW_C0
    add cx, SW_C0 + 1
    call .seg
    jc .out
    mov ax, SW_D0                   ; D: all of it
    mov cx, SW_N
    ; (falls into .seg)
; .seg - items [AX, CX) cut to the taken, merged and put out whole
.seg:
    cmp cx, [it_sbk]
    jbe .s1
    mov cx, [it_sbk]
.s1:
    cmp ax, cx
    jae .ok
    mov [it_swk], ax
    mov [it_sbe], cx
.g:
    mov cx, [it_sbe]
    call it_sw_grpl                 ; (CF = 1: none left in [swk, sbe))
    jc .ok
    xor si, si
.p:
    cmp si, [it_sgn]
    jae .g
    push si
    lea bx, [it_sgr + si]
    call it_sw_put
    pop si
    jc .out
    add si, SW_SIZE
    jmp short .p
.ok:
    clc
.out:
    ret
