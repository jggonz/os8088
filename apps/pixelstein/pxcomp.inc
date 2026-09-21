; =============================================================================
; os8088 - apps/pixelstein/pxcomp.inc
;
; THE COMPOSE (SPEC.md 96.5): the column arrays px_cast filled, into the
; destination's 80-byte rows, writing only what changed since the last frame
; that landed on this page.
;
; DS IS THE DESTINATION for the whole of px_compose - the ladders store with
; no segment override, which is the instruction the bench priced (96.3: DS =
; the destination) - so everything of this package's own is read through CS,
; which is the same segment as its DS (SPEC.md 20.1). A `[bp]` operand would
; address SS; every one below carries the `cs:` that puts it right.
;
; Per column, Flat: a column whose four bytes did not move is skipped whole;
; one whose TONE stood (mat and side) and whose wall is tall enough to keep
; a middle writes ONLY ITS TWO ENDS - the rows between max(top, ltop) and
; min(bot, lbot) already hold this tone in this parity, and re-laying them
; was 42-68% of the turn frame's stores (px_compose's .fgeo); otherwise the
; wall [top, bot] is written whole, the ceiling put back over [ltop, top)
; only when the wall shrank from the top and the floor over (bot, lbot] only
; when it shrank from the bottom. Wire: two runs a column - the upper in rows
; 0..39, the lower in 40..79, kept apart by the projection (a wall is
; centred on the horizon) - a column whose four run ends did not move is
; skipped whole, a run that did not move writes nothing, a single-row edge
; that moved to a single row is two direct stores (the ground back, the
; edge on), and only a run with a vertical part is written as the UNION of
; its old and new rows - ground, edge, ground - through the ladder. The
; rows any column wrote are px_r0..px_r1, which is what the present sends
; (96.5) - AND THAT RANGE IS THE WHOLE BAND ON EVERY FRAME A PLAYER SEES,
; on both rungs: it is one min/max over ALL columns, a column nearer than
; 2.5 tiles clips to rows 0..79 (h = 51200 / nx >= 80 at nx <= 640,
; pxcast.inc), and every scene has one that a turn moves. Measured on the
; 5150 the present is 21.2 ms turning against 22.5 on a full repaint, and
; tests/pixelstein.py asserts the range against tools/pxssim.py's model of
; this rule rather than reporting it. What the two skips above buy is the
; COMPOSE (30.4 -> 18.8 ms at the default), not the present; a present that
; collapses needs a per-column range, which is 96.5's wave-2 question.
; =============================================================================

; -----------------------------------------------------------------------------
; px_compose - every column of the frame (DS switches to the destination here)
; in:  px_top/px_bot/px_mat/px_side, [px_dseg], [px_dbase], [px_pgofs]
; out: [px_r0]..[px_r1] the dirty rows (r0 > r1: nothing written)
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_compose:
    mov byte [px_r0], 0xFF
    mov byte [px_r1], 0
    mov word [px_qp], PXG_Q         ; the draw queue, empty (96.3)
%ifdef PXPROBE
    mov word [px_pr_lad], 0         ; tests/pxsperf.py --probe's counters:
    mov word [px_pr_skip], 0        ; ladder entries and columns skipped
%endif                              ; this frame (the queue's length is
                                    ; readable off px_qp in every build)
    mov es, [px_hand + PXH_GEN]     ; ES = part 2 for the queue's entries
    mov ax, [px_dbase]              ; the frame's column base, summed ONCE:
    add ax, [px_cbias]              ; the page's base (Mode X alternates it)
    mov [px_cbase], ax              ; and px_x0 - 256 - the band's first byte
                                    ; (the Size in force, px_res_apply) and
                                    ; the ladder's bias (pxgen.inc). THE
                                    ; VARIABLE, not a constant: the presents
                                    ; read px_x0 and the first cut wrote
                                    ; every column at 8, so Size 48 drew 8
                                    ; bytes left of where it was shown and
                                    ; Size 80 spilled its last 8 columns into
                                    ; the next row. Two cs: adds a column
                                    ; were ~700 clk a frame (review, wave 2)
    push ds
    push bp
    mov ds, [px_dseg]
    xor bx, bx                      ; BX = the column
.col:
    mov bp, bx                      ; ...kept across the ladder calls
    mov di, bx
    cmp byte [cs:px_bpc], 1
    je .b1
    shl di, 1                       ; two bytes a column at Low res
.b1:
    add di, [cs:px_cbase]
    mov [cs:px_cdi], di
    mov si, bx
    add si, [cs:px_pgofs]           ; this page's memory of the column
    cmp byte [cs:px_rung], PXR_TEX  ; THE DEFAULT RUNG IS ASKED FIRST: one
    je .tex                         ; 5-byte cs: compare a column for it,
    cmp byte [cs:px_rung], PXR_WIRE ; ~1,300 clk a frame off the Textured
    je .wire                        ; compose (review, wave 2); Flat pays the
                                    ; two it paid, Wire one more
    ; --- FLAT --------------------------------------------------------------
    mov al, [cs:px_top + bx]        ; t
    mov ah, [cs:px_bot + bx]        ; b
    mov cl, [cs:px_ltop + si]       ; lt
    mov ch, [cs:px_lbot + si]       ; lb
    ; A COLUMN THAT DID NOT CHANGE IS SKIPPED WHOLE (96.5): a flat tone that
    ; has not moved is already on the destination, so neither the ladder
    ; nor the dirty rows see it - four compares against the ~100,000 clk a
    ; frame of writing and presenting every wall that stood still. A forced
    ; frame writes an impossible material into lmat (px_force_all) so it
    ; draws everything
    cmp al, cl
    jne .fchg
    cmp ah, ch
    jne .fchg
    mov dl, [cs:px_mat + bx]
    cmp dl, [cs:px_lmat + si]
    jne .fchg
    mov dl, [cs:px_side + bx]
    cmp dl, [cs:px_lside + si]
    jne .fchg
%ifdef PXPROBE
    inc word [cs:px_pr_skip]
%endif
    jmp .next
.fchg:
    mov [cs:px_ltop + si], al
    mov [cs:px_lbot + si], ah
    mov bl, al                      ; the dirty rows: min(t, lt)..max(b, lb)
    cmp bl, cl                      ; (BX is free until .next: BP holds the
    jbe .f1                         ; column)
    mov bl, cl
.f1:
    cmp bl, [cs:px_r0]
    jae .f2
    mov [cs:px_r0], bl
.f2:
    mov bl, ah
    cmp bl, ch
    jae .f3
    mov bl, ch
.f3:
    cmp bl, [cs:px_r1]
    jbe .f4
    mov [cs:px_r1], bl
.f4:
    mov dl, [cs:px_mat + bp]
    mov dh, [cs:px_side + bp]
    ; THE TONE STOOD AND ONLY THE GEOMETRY MOVED (96.5): the destination
    ; holds this tone over [lt, lb] in this parity - px_ltop/px_lbot/px_lmat/
    ; px_lside are the memory of exactly that, per page on Mode X, and a
    ; forced frame writes 0xFF into lmat so it never lands here - so the
    ; rows [max(t, lt), min(b, lb)] are right already and only the two ENDS
    ; are written: the rows the wall took from the ground in its tone, the
    ; rows it gave back in the ground's. Two ladder entries at most and
    ; |t - lt| + |b - lb| stores against b - t + 1: on the host's count of
    ; the two pinned scenes the whole-wall write re-laid 452 of 1,082 rows
    ; identically on scene A's turn (42%) and 1,162 of 1,706 on scene B's
    ; (68%). The projection centres every wall on the horizon (t <= 38,
    ; b >= 40: h >= 3 on a 64-tile map), so the old and new walls always
    ; overlap; the two compares below keep the arm honest if a later writer
    ; (wave 3's px_touch) ever puts something else in the memory, and a
    ; short wall takes the whole-wall write, where one entry (280 clk)
    ; beats the second entry the two-ends write would cost it
    cmp dl, [cs:px_lmat + si]
    jne .ftone
    cmp dh, [cs:px_lside + si]
    jne .ftone
    cmp al, ch
    ja .ftone                       ; t > lb: no middle (cannot happen)
    cmp ah, cl
    jb .ftone                       ; b < lt: likewise
    mov bl, ah
    sub bl, al
    cmp bl, PX_GEOMIN
    jae .fgeo
.ftone:
    mov [cs:px_lmat + si], dl
    mov [cs:px_lside + si], dh
    cmp al, cl                      ; the wall shrank from the top: the
    jbe .noceil                     ; ceiling back over [lt, t)
    push ax
    push cx
    sub al, cl
    xchg al, cl                     ; AL = lt, CL = t - lt
    xor ch, ch
    mov bx, [cs:px_inkc]
    call px_lad
    pop cx
    pop ax
.noceil:
    push ax                         ; the wall [t, b], its material's tone
    push cx
    mov cl, ah
    sub cl, al
    inc cl
    xor ch, ch
    mov bl, [cs:px_mat + bp]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    cmp byte [cs:px_side + bp], 0
    je .lit
    inc bx
    inc bx
.lit:
    mov bx, [cs:px_inkt + bx]
    call px_lad
    pop cx
    pop ax
    cmp ah, ch                      ; ...and shrank from the bottom: the
    jae .nofloor                    ; floor back over (b, lb]
    mov cl, ch
    sub cl, ah
    xor ch, ch
    mov al, ah
    inc al
    mov bx, [cs:px_inkf]
    call px_lad
.nofloor:
    jmp .next
.fgeo:
    ; --- the top end: AL = t, CL = lt --------------------------------------
    cmp al, cl
    je .gbot
    jb .gwtop
    push ax                         ; t > lt: the ceiling back over [lt, t)
    push cx
    sub al, cl
    xchg al, cl                     ; AL = lt, CL = t - lt
    xor ch, ch
    mov bx, [cs:px_inkc]
    call px_lad
    pop cx
    pop ax
    jmp short .gbot
.gwtop:
    push ax                         ; t < lt: the wall over [t, lt)
    push cx
    sub cl, al                      ; AL = t, CL = lt - t
    xor ch, ch
    call px_wtone
    call px_lad
    pop cx
    pop ax
.gbot:
    ; --- the bottom end: AH = b, CH = lb -----------------------------------
    cmp ah, ch
    je .next
    ja .gwbot
    mov cl, ch                      ; b < lb: the floor back over (b, lb]
    sub cl, ah
    xor ch, ch
    mov al, ah
    inc al
    mov bx, [cs:px_inkf]
    call px_lad
    jmp .next
.gwbot:
    mov al, ch                      ; b > lb: the wall over (lb, b]
    inc al
    mov cl, ah
    sub cl, ch
    xor ch, ch
    call px_wtone
    call px_lad
    jmp .next
    ; --- WIRE --------------------------------------------------------------
.wire:
    mov al, [cs:px_top + bx]        ; t
    mov ah, [cs:px_bot + bx]        ; b
    mov cl, al                      ; CL/CH = the upper run u0..u1
    mov ch, al
    mov dl, ah                      ; DL/DH = the lower run l0..l1
    mov dh, ah
    or bx, bx
    jz .wruns                       ; column 0 has no neighbour
    push ax                         ; a corner or a material change makes
    mov al, [cs:px_mat + bx - 1]    ; the run reach the neighbour's edge
    cmp al, [cs:px_mat + bx]
    jne .wchg
    mov al, [cs:px_side + bx - 1]
    cmp al, [cs:px_side + bx]
    jne .wchg
    mov al, [cs:px_top + bx - 1]
    cmp al, cl
    jne .wchg
    mov al, [cs:px_bot + bx - 1]
    cmp al, dl
    jne .wchg
    pop ax
    jmp short .wruns
.wchg:
    pop ax
    mov al, [cs:px_top + bx - 1]    ; pt
    cmp al, cl
    jae .w1
    mov cl, al
.w1:
    cmp al, ch
    jbe .w2
    mov ch, al
.w2:
    mov al, [cs:px_bot + bx - 1]    ; pb
    cmp al, dl
    jae .w3
    mov dl, al
.w3:
    cmp al, dh
    jbe .w4
    mov dh, al
.w4:
.wruns:
    ; THE SKIP (96.5): four run ends against last frame's, and a column
    ; whose edges did not move is left alone - no store, no dirty rows
    cmp cl, [cs:px_lu0 + si]
    jne .wchg2
    cmp ch, [cs:px_lu1 + si]
    jne .wchg2
    cmp dl, [cs:px_ll0 + si]
    jne .wchg2
    cmp dh, [cs:px_ll1 + si]
    jne .wchg2
    jmp .next
.wchg2:
    push si
    push dx
    push cx
    mov al, [cs:px_lu0 + si]        ; the upper run against last frame's
    mov ah, [cs:px_lu1 + si]
    mov [cs:px_lu0 + si], cl
    mov [cs:px_lu1 + si], ch
    mov dx, cx
    mov byte [cs:px_wclip], 0
    call px_wrun
    pop cx
    pop dx
    pop si
    push si
    mov al, ch                      ; the lower run, clipped past the upper's
    inc al                          ; end: those rows are the upper's this
    mov [cs:px_wclip], al           ; frame (the horizon keeps the two apart
    mov al, [cs:px_ll0 + si]        ; everywhere else - 96.3)
    mov ah, [cs:px_ll1 + si]
    mov [cs:px_ll0 + si], dl
    mov [cs:px_ll1 + si], dh
    call px_wrun
    pop si
    jmp .next                       ; (the first cut fell into the Textured
                                    ; arm here and queued a wall per Wire
                                    ; column)
    ; --- TEXTURED (wave 2, 96.3) ------------------------------------------
    ; The same memory as Flat's plus the texture column px_lu AND the
    ; quantised height px_lh: a column whose six bytes stood is skipped
    ; whole; one that moved has the ceiling and the floor put back over
    ; what the wall gave up (the ladders, as Flat) and its wall QUEUED -
    ; (scaler, SI, DI) into part 2 - for the driver, which the end of this
    ; routine enters with ONE far call. A textured wall whose height moved
    ; must be redrawn whole (its texels shift), so there is no two-ends arm
    ; here. THE HEIGHT IS ITS OWN BYTE: top and bot clip to 0..79 for every
    ; h >= 80, so eight scalers (80..120) share one (top, bot) and draw
    ; eight different pictures - the five-byte skip of the first cut froze
    ; the centre columns of a wall walked straight at, where mat, side and
    ; u stand by construction, at whichever scaler they crossed 80 on
.tex:
    mov dl, [cs:px_u + bx]
    mov cl, [cs:px_ush]             ; the texture column: u >> 3, or u >> 4
    shr dl, cl                      ; on C160 (a column is a texel pair) -
                                    ; the count is the backend's, set once
    mov al, [cs:px_top + bx]        ; t
    mov ah, [cs:px_bot + bx]        ; b
    mov cl, [cs:px_ltop + si]       ; lt
    mov ch, [cs:px_lbot + si]       ; lb
    cmp al, cl
    jne .tchg
    cmp ah, ch
    jne .tchg
    mov dh, [cs:px_h + bx]
    cmp dh, [cs:px_lh + si]
    jne .tchg
    mov dh, [cs:px_mat + bx]
    cmp dh, [cs:px_lmat + si]
    jne .tchg
    mov dh, [cs:px_side + bx]
    cmp dh, [cs:px_lside + si]
    jne .tchg
    cmp dl, [cs:px_lu + si]
    jne .tchg
%ifdef PXPROBE
    inc word [cs:px_pr_skip]
%endif
    jmp .next
.tchg:
    mov [cs:px_ltop + si], al
    mov [cs:px_lbot + si], ah
    mov [cs:px_lu + si], dl
    mov dl, [cs:px_h + bx]
    mov [cs:px_lh + si], dl
    mov dl, [cs:px_mat + bx]
    mov [cs:px_lmat + si], dl
    mov dl, [cs:px_side + bx]
    mov [cs:px_lside + si], dl
    mov bl, al                      ; the dirty rows: min(t, lt)..max(b, lb)
    cmp bl, cl
    jbe .t1
    mov bl, cl
.t1:
    cmp bl, [cs:px_r0]
    jae .t2
    mov [cs:px_r0], bl
.t2:
    mov bl, ah
    cmp bl, ch
    jae .t3
    mov bl, ch
.t3:
    cmp bl, [cs:px_r1]
    jbe .t4
    mov [cs:px_r1], bl
.t4:
    ; THE ONE-ROW END TAKES px_pix, NOT THE LADDER (px_wrun's idiom, 96.5):
    ; the generated heights step by 2 up to 80, so top = (80 - h) >> 1
    ; moves by exactly ONE row for the smallest change in h, and a column
    ; whose wall moved without crossing the clip has an end of one row at
    ; either side - the MODAL end of a turn under Textured, not the rare one.
    ; A ladder entry is ~280 clk before its first store and px_pix ~150 with
    ; the store in it: ~158 an end, two ends x 32 columns = ~10,000 clk =
    ; 2.1 ms of the 88.6 ms turn frame (review, wave 2)
    cmp al, cl                      ; the wall shrank from the top: the
    jbe .tnoceil                    ; ceiling back over [lt, t)
    push ax
    push cx
    sub al, cl
    xchg al, cl                     ; AL = lt, CL = t - lt
    mov bx, [cs:px_inkc]
    cmp cl, 1
    je .tc1
    xor ch, ch
    call px_lad
    jmp short .tc0
.tc1:
    call px_pix                     ; one row: no ladder entry
.tc0:
    pop cx
    pop ax
.tnoceil:
    cmp ah, ch                      ; ...and from the bottom: the floor
    jae .tnofloor                   ; back over (b, lb]
    mov cl, ch
    sub cl, ah
    mov al, ah
    inc al
    mov bx, [cs:px_inkf]
    cmp cl, 1
    je .tf1
    xor ch, ch
    call px_lad
    jmp short .tnofloor
.tf1:
    call px_pix                     ; one row: no ladder entry
.tnofloor:
    ; the wall: a queue entry - the scaler of this column's quantised
    ; height in this resolution's directory, SI = (mat - 1) * 2048 + side *
    ; 1024 + column * 32 into part 3 (96.4's layout), DI = the column's
    ; base byte (px_cdi is biased by -256 for the ladders)
    mov bl, [cs:px_h + bp]
    xor bh, bh
    shl bx, 1
    add bx, [cs:px_gsc]
    mov ax, [cs:bx]
    mov di, [cs:px_qp]
    mov [es:di], ax
    mov al, [cs:px_mat + bp]
    dec al                          ; material 1 is slot 0
    mov ah, al
    shl ah, 1
    shl ah, 1
    shl ah, 1                       ; (mat - 1) << 11
    mov al, [cs:px_side + bp]
    shl al, 1
    shl al, 1
    or ah, al                       ; + side << 10
    xor al, al
    mov si, bp                      ; SI AGAIN: px_lad clobbered it, and the
    add si, [cs:px_pgofs]           ; first cut read another column's texture
    mov bl, [cs:px_lu + si]         ; the column, banked above
    xor bh, bh
    mov cl, 5
    shl bx, cl                      ; column * 32 (per COLUMN, not per pixel)
    add ax, bx
    mov [es:di + 2], ax
    mov ax, [cs:px_cdi]
    add ax, 256
    mov [es:di + 4], ax
    add di, PXG_QSZ
    mov [cs:px_qp], di
.next:
    mov bx, bp
    inc bx
    cmp bl, [cs:px_cols]
    jae .done
    jmp .col
.done:
    ; THE ONE FAR CALL (96.3): every queued wall drawn by the driver in part
    ; 2, DS still the destination, ES the textures once it is in there
    mov di, [cs:px_qp]
    cmp di, PXG_Q
    je .noq
    mov [es:PXG_QEND], di
    call far [cs:px_drvp]
.noq:
    pop bp
    pop ds
    ret

; px_wtone - BX = the tone pair of column BP's wall: its material's word in
;            px_inkt, the dark twin two bytes along (DS = the destination)
; clobbers BX
px_wtone:
    mov bl, [cs:px_mat + bp]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    cmp byte [cs:px_side + bp], 0
    je .lit
    inc bx
    inc bx
.lit:
    mov bx, [cs:px_inkt + bx]
    ret

; -----------------------------------------------------------------------------
; px_wrun - one Wire run: nothing if it stands where it stood; two direct
;           stores if one row moved to one row; else ground / edge / ground
;           over the union of the run's old rows and new (DS = the destination)
; in:  DL/DH = the new run's first/last row, AL/AH = last frame's (AL > AH:
;      there was none), [cs:px_wclip] = the first row this run may write
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
px_wrun:
    cmp dx, ax
    je .out                         ; the run did not move: nothing to write
    cmp dl, dh
    jne .union
    cmp al, ah
    jne .union
    cmp al, [cs:px_wclip]           ; (the horizon keeps both rows past the
    jb .union                       ; clip; the compares keep it honest)
    cmp dl, [cs:px_wclip]
    jb .union
    ; ONE ROW MOVED TO ONE ROW: the ground back over the old, the edge on
    ; the new - two px_pix stores and no ladder entry, the common column of
    ; a turn (96.5)
    mov bl, al                      ; the dirty rows: both of them
    mov bh, dl
    cmp bl, bh
    jbe .o1
    xchg bl, bh
.o1:
    cmp bl, [cs:px_r0]
    jae .o2
    mov [cs:px_r0], bl
.o2:
    cmp bh, [cs:px_r1]
    jbe .o3
    mov [cs:px_r1], bh
.o3:
    push dx
    mov bx, [cs:px_inkc]
    cmp al, PX_HORIZON
    jb .og
    mov bx, [cs:px_inkf]
.og:
    call px_pix
    pop dx
    mov al, dl
    mov bx, [cs:px_inke]
    jmp px_pix                      ; its ret is ours
.union:
    mov bl, dl                      ; BL = max(min(dl, al), clip)
    cmp bl, al
    jbe .lo
    mov bl, al
.lo:
    cmp bl, [cs:px_wclip]
    jae .lo2
    mov bl, [cs:px_wclip]
.lo2:
    mov bh, dh                      ; BH = max(dh, ah)
    cmp bh, ah
    jae .hi
    mov bh, ah
.hi:
    cmp bl, bh
    ja .out                         ; nothing of this run is ours
    cmp bl, [cs:px_r0]              ; the dirty rows
    jae .r0
    mov [cs:px_r0], bl
.r0:
    cmp bh, [cs:px_r1]
    jbe .r1
    mov [cs:px_r1], bh
.r1:
    cmp dl, [cs:px_wclip]           ; the new run, clipped
    jae .nc
    mov dl, [cs:px_wclip]
.nc:
    push bx                         ; ground [lo, dl)
    push dx
    mov al, bl
    mov cl, dl
    sub cl, bl
    jz .g1
    xor ch, ch
    call px_ground
.g1:
    pop dx
    pop bx
    cmp dl, dh                      ; edge [dl, dh]
    ja .noedge
    push bx
    push dx
    mov al, dl
    mov cl, dh
    sub cl, dl
    inc cl
    xor ch, ch
    mov bx, [cs:px_inke]
    call px_lad
    pop dx
    pop bx
.noedge:
    mov al, dh                      ; ground (dh, hi], from lo if the run
    inc al                          ; was clipped away
    cmp al, bl
    jae .g2
    mov al, bl
.g2:
    cmp al, bh
    ja .out
    mov cl, bh
    sub cl, al
    inc cl
    xor ch, ch
    call px_ground
.out:
    ret

; -----------------------------------------------------------------------------
; px_pix - ONE row of a column: AL = the row, BX = the tone pair (BL even, BH
;          odd), [cs:px_cdi] = the column's base byte - 256, [cs:px_bpc] the
;          store's width (DS = the destination). A ladder entry costs ~280
;          clk before its first store; this is ~150 with the store in it
; clobbers AX, BX, SI, DI
; -----------------------------------------------------------------------------
px_pix:
    test al, 1
    jz .even
    mov bl, bh                      ; the pair follows the row's parity
.even:
    xor ah, ah
    mov si, ax
    shl si, 1
    mov di, [cs:px_row80 + si]
    add di, [cs:px_cdi]
    cmp byte [cs:px_bpc], 1
    jne .word
    mov [di + 256], bl
    ret
.word:
    mov bh, bl                      ; two bytes a column at Low res
    mov [di + 256], bx
    ret

; -----------------------------------------------------------------------------
; px_ground - CX rows of ground from row AL: the ceiling tone above the
;             horizon, the floor tone below, split where a range crosses it
; clobbers AX, BX, DX, SI, DI (CX preserved)
; -----------------------------------------------------------------------------
px_ground:
    mov bx, [cs:px_inkc]
    cmp al, PX_HORIZON
    jae .floor
    mov dl, al
    add dl, cl                      ; the row past the range
    cmp dl, PX_HORIZON
    jbe .one                        ; all ceiling
    push dx
    push cx
    mov cl, PX_HORIZON              ; the ceiling part [al, 40)
    sub cl, al
    xor ch, ch
    call px_lad
    pop cx
    pop dx
    mov al, PX_HORIZON              ; ...and the floor part [40, end)
    push cx
    mov cl, dl
    sub cl, PX_HORIZON
    xor ch, ch
    mov bx, [cs:px_inkf]
    call px_lad
    pop cx
    ret
.floor:
    mov bx, [cs:px_inkf]
.one:
    jmp px_lad

; -----------------------------------------------------------------------------
; px_force_all - every column's memory to "the whole view was drawn", so the
;                next frame writes and presents all of it (96.5). NOT the
;                same as [px_force] = 1 alone, which only bypasses the idle
;                predicate: the next frame then composes against an
;                unchanged memory and writes what MOVED - nothing, on a
;                still eye. The gates poke these SEVEN arrays (pxslib's
;                force_all; not px_lside, px_lu or px_lh, which neither
;                skip reaches once lmat mismatches) when they want a full
;                repaint measured
; preserves every register
; -----------------------------------------------------------------------------
px_force_all:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov di, px_ltop
    mov cx, 2 * PX_COLMAX
    xor al, al
    rep stosb
    mov di, px_lbot
    mov cx, 2 * PX_COLMAX
    mov al, PX_ROWS - 1
    rep stosb
    mov di, px_lmat                 ; a material no level can hold, so no
    mov cx, 2 * PX_COLMAX           ; Flat column reads as unchanged (96.5)
    mov al, 0xFF
    rep stosb
    mov di, px_lu0
    mov cx, 2 * PX_COLMAX
    xor al, al
    rep stosb
    mov di, px_lu1
    mov cx, 2 * PX_COLMAX
    mov al, PX_HORIZON - 1
    rep stosb
    mov di, px_ll0
    mov cx, 2 * PX_COLMAX
    mov al, PX_HORIZON
    rep stosb
    mov di, px_ll1
    mov cx, 2 * PX_COLMAX
    mov al, PX_ROWS - 1
    rep stosb
    mov byte [px_force], 1
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_frame_begin - is there a frame to draw, and if so cast and compose it
; out: CF=0 nothing changed since the last frame (nothing composed); CF=1
;      the destination holds a new frame and px_r0..px_r1 says which rows -
;      the caller presents it and calls px_frame_end
; clobbers everything
;
; NOTHING DIRTY, NOTHING COMPOSED (96.5): a still player costs the machine
; nothing but the wait. The clock starts here and stops in px_frame_end, so
; the selector times cast + compose + present and never the wait.
; -----------------------------------------------------------------------------
px_frame_begin:
    cmp byte [px_pend], 0           ; a Detail or Resolution pick, applied
    je .nopend                      ; BETWEEN frames and never under one:
    mov byte [px_pend], 0           ; the menu handler is the UI task and
    call px_apply                   ; the compose may be the worker's
.nopend:
    cmp byte [px_force], 0
    jne .go
    cmp byte [px_dirty], 0
    jne .go
    clc
    ret
.go:
    call pit_now
    mov [px_t0], ax
    mov [px_t0 + 2], dx
    xor ax, ax
    mov [px_twait], ax              ; no wait charged yet (px_wait_*)
    mov [px_twait + 2], ax
    mov byte [px_force], 0
    mov byte [px_dirty], 0
    call px_cast
    call px_compose
    stc
    ret

; -----------------------------------------------------------------------------
; px_frame_end - the frame is on the glass: count it, time it, let the
;                selector see it (96.8)
; clobbers AX, DX
; -----------------------------------------------------------------------------
px_frame_end:
    inc word [px_frames]
    call pit_now
    sub ax, [px_t0]
    sbb dx, [px_t0 + 2]
    sub ax, [px_twait]              ; THE WORK, NOT THE WAIT (96.8): the
    sbb dx, [px_twait + 2]          ; retrace OSAPI_FSX_PAGE waits for on
    mov [px_ftime], ax              ; Mode X and another task's hold on the
    mov [px_ftime + 2], dx          ; gfx lock are bracketed out, or the
    call px_auto_frame              ; selector steps on a 16.7 ms bucket
    ret

; px_wait_begin / px_wait_end - bracket an idle wait inside a timed frame so
; px_frame_end can subtract it. clobbers AX, DX, flags
px_wait_begin:
    call pit_now
    mov [px_tw0], ax
    mov [px_tw0 + 2], dx
    ret

px_wait_end:
    call pit_now
    sub ax, [px_tw0]
    sbb dx, [px_tw0 + 2]
    add [px_twait], ax
    adc [px_twait + 2], dx
    ret
