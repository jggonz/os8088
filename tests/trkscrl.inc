; =============================================================================
; os8088 - tests/trkscrl.inc
;
; THE SCROLL PATH'S BENCH HOOKS (SPEC.md 45.12.2). Four counters and two keys,
; and between them they answer the two questions tui_scrl's generalisation
; raises: does it draw the RIGHT pixels for a jump of n rows, and does it stop
; the full-repaint ratchet the field reported.
;
; This is BENCH CODE and it does not ship. `apps/tracker` carries a handful of
; one-line hooks, every one of them inside %ifdef TRKDBG, and everything else
; lives here; TRACKER.O88 is built without -DTRKDBG and is byte-identical with
; and without this file. The bench build is TRKSCRL.O88 on build/trkscrl.img
; (`make trkscrl`), and tests/trkscrl.py is what drives it.
;
; WHY A KEY THAT JUMPS THE VIEW. A stopped view moves on Up/Down, and the
; bracket's loop reads exactly ONE key and then draws a frame (trk_fsx_main) -
; so no number of Down presses can ever put more than one row between two
; frames, and n > 1 is unreachable from the keyboard. It is reachable from the
; MUSIC, which is precisely the case that cannot be single-stepped: the row
; moves on while the harness is taking its screenshot, so "the same view drawn
; two ways" has no meaning there. trk_dbg_jump moves the STOPPED view by n in
; one frame, which is the same delta tui_scrl sees from a late frame and is
; the only form of it a screenshot can hold still.
;
; ...and G is the other half of the pair: it repaints the grid through
; tui_draw_pat WITHOUT moving the view, so the harness can diff the scroll
; path's output against the full repaint's, pixel for pixel, over the row
; area. Identical is the only acceptable answer and it is what the strip
; arithmetic is checked by.
;
;   TDS_FRAME  tui_draw_dyn calls - the frame rate the bracket actually held
;   TDS_SCRL   scrolls tui_scrl answered
;   TDS_NO     ...and the times it refused, which is a full repaint each
;   TDS_PAT    tui_draw_pat calls. On a healthy machine this is the PATTERN
;              CHANGE rate and nothing else - once every 7.7 s on a 125 BPM
;              speed-6 module - so anything above that is the ratchet, named.
;
; ...and TDS_Y0/TDS_Y1, which are not counters: they are the row area's screen
; bounds, published so the harness can diff exactly the band tui_scrl owns.
; They are PUBLISHED rather than written down in the test because the layout is
; picked by surface height (tui_layout_for), so a figure hardcoded for the VGA
; one silently measures the wrong strip of screen under `make test VIDEO=cga`.
; The harness cannot read `tui_lay` itself - the bss map it uses is harvested
; from encoded `[name]` operands and every reference to that record carries a
; `+ TL_*` - so these two exist to be bare.
; =============================================================================

; --- the jump keys, and what each moves the stopped view by -------------------
; NOT l/p/r/x/d/w/m/1-4/f/space/Esc/Home, which the bracket already answers.
;
; **AND NOT `v`, WHICH THIS LIST LEFT OUT.** V is the fullscreen SURFACE
; (tracker.asm, SPEC.md 45.13), and its `cmp al, 'v' / je .txxt` sits THREE
; LINES ABOVE the `%ifdef TRKDBG` block that calls this table - so a `v` here
; is answered by the surface and never reaches `trk_dbg_key` at all. The entry
; was `db 'v', -2` and it was dead: tests/trkscrl.py failed on exactly one of
; its seven cases, "v moved 0 rows, not -2", identically at both ends of a
; kernel size pass, and read for a week as a defect in the tracker's SCROLL.
; Its neighbours pass, including the same direction from further away, which
; was the tell - nothing about -2 from view 2 is special, and a key that is
; never delivered looks exactly like a scroll that refuses.
;
; A key added here must not be one the two handlers above already compare
; against. `u` is free in both.
trk_dbg_keys:
    db 'j', 2
    db 'k', 3
    db 'n', 4
    db 'u', -2
    db 'b', -3
    db 'c', -4
    db 0

; -----------------------------------------------------------------------------
; trk_dbg_key - AL = ascii; CF=0 if it was one of ours and has been answered
; -----------------------------------------------------------------------------
trk_dbg_key:
    push ax
    push si
    mov si, trk_dbg_keys
.scan:
    cmp byte [si], 0
    je .no
    cmp al, [si]
    je .hit
    add si, 2
    jmp short .scan
.hit:
    mov al, [si + 1]
    call trk_dbg_jump
    pop si
    pop ax
    clc
    ret
.no:
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; trk_dbg_area - publish the row area's screen bounds, [tds_y0] .. [tds_y1]-1.
;                Called beside the G repaint, so they describe the layout the
;                comparison is about to be made against. Preserves everything.
; -----------------------------------------------------------------------------
trk_dbg_area:
    push ax
    push cx
    mov ax, [tui_lay + TL_ROWT]     ; the first upper strip...
    add ax, [tui_oy]
    mov [tds_y0], ax
    mov ax, [tui_lay + TL_RBE]      ; ...and one past the last lower one
    mov cl, 3
    shl ax, cl
    add ax, [tui_lay + TL_LOWY]
    add ax, [tui_oy]
    mov [tds_y1], ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_dbg_jump - move the STOPPED view by AL signed rows in one frame, clamped
;                to 0..63. Preserves every register.
; -----------------------------------------------------------------------------
trk_dbg_jump:
    push ax
    push bx
    cmp byte [mp_playing], 0        ; playing, the row comes from the music and
    jne .out                        ; [tui_vrow] is not what the view reads
    mov bl, [tui_vrow]
    add bl, al
    js .out
    cmp bl, 63
    ja .out
    mov [tui_vrow], bl
.out:
    pop bx
    pop ax
    ret

; --- .bss (the TRKW accumulator, like every other file here) -------------------
    TRKW tds_frame                  ; frames drawn
    TRKW tds_scrl                   ; ...of which a scroll answered
    TRKW tds_no                     ; ...and tui_scrl refused
    TRKW tds_pat                    ; full pattern repaints
    TRKW tds_y0                     ; the row area's first screen row...
    TRKW tds_y1                     ; ...and one past its last
