; =============================================================================
; os8088 - apps/pixelstein/pxwin.inc
;
; THE WINDOWED HALF (SPEC.md 97.3's WIN1, 97.8): the worker that is the game
; loop on the desktop, the geometry asked EVERY FRAME (nothing tells a package
; its window moved - apps/dotdel/ddrend.inc), the one OSAPI_GFX_BLIT1 that
; puts the band on the glass with a pen on VGA, the focus check, and the
; text line under the black HUD band.
;
; The frame is composed LOCK-FREE into the shadow (SPEC.md 20.6 rule 3: a
; worker computes without the lock and takes it for a short burst), then the
; lock is taken for the blit and the line and given back. The bracket is the
; UI task's (pxrast.inc) and never the worker's: while it runs the worker is
; frozen with everything else (SPEC.md 53.2) - and px_go_fsx waits for the
; compose to finish before it asks for the freeze (97.8).
; =============================================================================

; -----------------------------------------------------------------------------
; px_worker - the windowed game loop (apps/dotdel's dd_worker, SPEC.md 93.6)
; -----------------------------------------------------------------------------
; px_worker_rs - THE RESTART POINT (SPEC.md 66.6.2): the kernel re-enters the
;                worker HERE, DS = CS = the new segment, when it moved our
;                region with the worker parked - which it is only inside
;                OSAPI_TASK_ALIVE at the top of the loop below (no
;                OSAPI_MEM_PARKSAFE is declared, so a worker blocked in
;                OSAPI_GFX_LOCK is never frameless). Standing there it holds
;                no lock, px_composing is 0 and its stack holds nothing of
;                ours; the column memory (97.5) moved with the bss and the
;                shadow is a claim of its own, so nothing is stale - but the
;                frame is forced whole anyway, the path the gates poke, so a
;                restart can never leave a glass the memory does not describe
px_worker_rs:
    inc byte [px_wrs]               ; the gate reads it (tests/pxsmove.py leg
    mov byte [px_force], 1          ; 2b): a restart that never ran would
px_worker:                          ; pass a gate that only counted frames
    call OSAPI_GET_TICKS
    mov [px_last], ax
.loop:
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE           ; the lock must NOT be held here; a
                                    ; clicked close box never returns
    cmp byte [px_inbr], 0
    jne .sleep                      ; the bracket's, until it returns
    call OSAPI_GET_TICKS
    mov bx, ax
    sub bx, [px_last]               ; signed and wrap-safe by subtraction
    jz .nowork
    js .resync
    cmp bx, PX_MAXSTEP
    jbe .have
    mov bx, PX_MAXSTEP              ; a machine that was away does not get
.have:                              ; to walk through a wall
    mov [px_last], ax
    mov cx, bx
    cmp byte [px_state], PXST_PLAY  ; THE HELD KEYS ARE PLAY'S (97.13): a card
    jne .nokeys                     ; reads none, and the drain would eat the
    call px_keys_read               ; A, D, W and S of a name or a code typed
    call px_kbdrain                 ; on one. The held keys, once at the
.nokeys:                            ; first owed step; the steering scancodes
    cmp byte [px_pause], 0          ; eaten so a typed command keeps its place
    jne .paused
    cmp byte [px_abon], 0           ; SUSPENDED behind the About card (apps/
    jne .paused                     ; dotdel's dd_step): nothing under it moves
.steps:
    push cx
    call px_step
    pop cx
    loop .steps
.paused:
    call px_focus_ck
    call px_render_win
    ; DO NOT PACE THE FRAME TO THE TICK (SPEC.md 85.6, measured at 18.5% of
    ; the frame): a frame that took longer than a tick has a step owed
    ; already, and sleeping a tick on top of it quantised the windowed frame
    ; to whole ticks - 109.9 ms for a 70 ms draw on the Hercules desktop.
    ; Sleep only when nothing is owed; otherwise give the UI task its turn
    call OSAPI_GET_TICKS            ; and go round
    sub ax, [px_last]
    jz .sleep
    call OSAPI_TASK_YIELD
    jmp .loop
.resync:
    mov [px_last], ax               ; the counter wrapped or the clock moved
.nowork:
.sleep:
    mov ax, 1
    call OSAPI_TASK_SLEEP           ; one tick: the deadline is the tick
    jmp .loop                       ; counter itself

; -----------------------------------------------------------------------------
; px_render_win - one windowed frame: compose lock-free, then the burst
; clobbers everything
;
; A WINDOW NOBODY CAN SEE COSTS NOTHING (SPEC.md 97.8): once a frame ends at
; .skip - hidden, or not a pixel of it showing - px_hidden is set and every
; pass returns here until a W_PAINT or W_ONRESIZE clears it, which any
; re-exposure brings. The first cut cast and composed a whole frame every
; tick for a window under another, 46 ms of every 55 ms tick on the 8088.
; -----------------------------------------------------------------------------
px_render_win:
    cmp word [px_shseg], 0
    je .out                         ; no shadow: nothing to draw with
    cmp byte [px_hidden], 0
    jne .out                        ; nothing to draw ON
    cmp byte [px_abon], 0           ; the About card is over the band (apps/
    jne .out                        ; dotdel's dd_abon): a blit would take it
                                    ; off the glass with the byte still up;
                                    ; px_abdismiss repaints (97.8)
    mov byte [px_composing], 1
    call px_back_ck                 ; the backend THIS display wants (97.14)
    call px_frame_begin
    mov byte [px_composing], 0
    jc .frame
    cmp byte [px_whole], 0          ; nothing changed - but a W_PAINT that
    jne .whole                      ; landed mid-compose blackened the glass:
    mov al, [px_lined]              ; the band goes back whole, NO cast (the
    or al, [px_hudw]                ; first cut owed a whole frame, 34 ms of
    jz .out                         ; cast for a compose that then wrote
    call px_lock_clip               ; nothing) - or the text line alone (a
    jc .lskip                       ; pause, 97.8), or the bar a paint
    call px_geom_win                ; blackened (97.13)
    call px_hud_blit
    call px_line_ck
    call OSAPI_GFX_UNLOCK
    ret
.whole:
    call px_lock_clip
    jc .lskip
    cmp byte [px_abon], 0           ; THE CARD IS TESTED UNDER THE LOCK TOO
    jne .card                       ; (apps/dotdel's dd_draw tests dd_abon
                                    ; with the lock held): the UI task can
                                    ; open About inside the lock-free
                                    ; compose above, and a blit then wipes
                                    ; the card while px_abon still says it
                                    ; is up - the game freezes behind a card
                                    ; nobody can see
    call px_geom_win
    call px_blit_win                ; consumes px_whole: rows 0..79
    call px_hud_blit
    call px_line_ck
    call OSAPI_GFX_UNLOCK
    ret
.frame:
    cmp byte [px_inbr], 0           ; the bracket took the machine while
    jne .out                        ; this composed: its own frame follows
    call px_lock_clip
    jc .skip
    cmp byte [px_abon], 0           ; ...and the card, under the lock (above)
    jne .card
    call px_geom_win                ; where are we? asked every frame
    cmp byte [px_repaint], 0
    je .noblack
    call px_black_around            ; the Size shrank: the glass beside the
.noblack:                           ; band keeps the old picture otherwise
    call px_blit_win
.blitted:                           ; (tests/pxswin.py --price brackets the
                                    ; present from .noblack to here)
    call px_hud_blit                ; the bar's rectangle, if it moved (97.13)
    call px_line_ck
    call OSAPI_GFX_UNLOCK
    call px_frame_end
    ret
.skip:
    mov byte [px_force], 1          ; the shadow moved and the glass did not
.lskip:                             ; follow: the next frame that can draw
    mov byte [px_hidden], 1         ; owes a whole one - and until then,
    call OSAPI_GFX_UNLOCK           ; nothing
.out:
    ret
.card:
    call OSAPI_GFX_UNLOCK           ; the card owns the glass: nothing blitted,
    ret                             ; and px_abdismiss's px_paint puts the band
                                    ; back whole (px_whole) when it comes down

; px_back_ck - the window's backend follows its DISPLAY (97.14): WIN4 where
;              px_w4want says, WIN1 where it does not. Asked by the worker at
;              the top of every pass, inside px_composing (so a paint that
;              lands meanwhile owes the whole band rather than blitting half
;              a switch), and nothing but a compare when nothing moved. A
;              drag across a seam re-asks the depth in px_onresize's
;              px_adapter, and the pick is taken HERE, between frames - the
;              sets of the new class then built by px_apply (px_pend)
px_back_ck:
    call px_w4want
    cmp al, [px_back]
    je .out
    call px_r_setup_win
    mov byte [px_pend], 1
.out:
    ret

; px_lock_clip - the lock, then the two questions (apps/dotdel's dd_render):
;                out CF = 1 not a pixel of us is showing, the lock still held
px_lock_clip:
    call px_wait_begin              ; another task's hold on the lock is not
    call OSAPI_GFX_LOCK             ; this frame's work (97.8): what the
    call px_wait_end                ; selector judges is the draw
    mov ax, KERNEL_SEG              ; A WORKER MUST LOAD ES ITSELF - only a
    mov es, ax                      ; callback arrives with ES = KERNEL_SEG
    mov bx, [px_win]
    test word [es:bx + W_FLAGS], 2  ; still visible?
    jz .no
    call OSAPI_WM_CLIP_SET          ; CF = 1: not one pixel of us is showing
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; px_geom_win - the content box and the display we are on (SPEC.md 93.3)
; out: [px_cx]/[px_cy]/[px_cw]/[px_ch], [px_bpp], and the band's origin
;      [px_bx]/[px_by] - the content's left rounded up to the byte grid
;      (OSAPI_WM_SNAP makes that the left itself), the line's row [px_ly]
; clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
px_geom_win:
    mov bx, [px_win]
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [px_cx], ax
    mov [px_cy], dx
    mov bx, [px_win]
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    jc .hidden
    mov [px_cw], cx
    mov [px_ch], dx
.hidden:
    mov bx, [px_win]
    call OSAPI_WM_DISPLAY           ; ...ABOUT THE DISPLAY WE ARE ON
    mov [px_bpp], dh
    mov ax, [px_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [px_bx], ax
    mov ax, [px_cy]
    mov [px_by], ax
    add ax, PX_ROWS + PX_HUDROWS    ; the line under the black HUD band
    mov cx, [px_cy]
    add cx, [px_ch]
    sub cx, 16
    cmp ax, cx
    jbe .ly
    mov ax, cx                      ; ...or as low as the box allows
.ly:
    mov [px_ly], ax
    ret

; -----------------------------------------------------------------------------
; px_blit_win - the dirty rows of the band onto the glass (SPEC.md 5.4.2)
; in:  the lock held, the clip armed, px_geom_win done, [px_r0]..[px_r1] -
;      or [px_whole] = 1, which sends rows 0..79 and is consumed here
; clobbers AX, BX, CX, DX, SI, DI, BP, ES (DI: px_blit_w4's strip)
;
; ONE BLIT1 a frame at most; on WIN4 one BLIT4 or BLITP per 25-row strip
; (px_blit_w4, up to four). ES:SI is the shadow's first dirty row at the
; band's byte, BP the shadow stride, the width 512 pixels and the height the
; row range - WHICH IS THE WHOLE BAND ON EVERY FRAME A PLAYER SEES (97.5):
; the range is one min/max over all columns, any column nearer than 2.5
; tiles spans rows 0..79, and a turn moves one of those in every scene, so
; the measured blit is 80 rows and 24.7 ms on the 5150 whatever moved. The
; range collapses only on a frame in which no near column changed. The pen
; is light grey on black on a 4bpp display; on a 1bpp one it is ignored and
; the band's bits are the picture (SPEC.md 5.4.2.2). px_whole is a W_PAINT's
; word (97.5): the GLASS was blackened, the shadow is still the whole
; picture, so the next blit sends all of it whatever the compose wrote -
; and px_r0/px_r1 stay the worker's own bytes, never a paint's
; -----------------------------------------------------------------------------
px_blit_win:
    call px_w4want                  ; THE BACKEND IN FORCE IS NOT THE ONE THIS
    cmp al, [px_back]               ; DISPLAY WANTS (97.14): a seam drag onto
    je .ok                          ; a 1bpp display, or Colour toggled, since
    mov byte [px_force], 1          ; the worker's last px_back_ck. The shadow
    ret                             ; holds the OTHER class's bytes - WIN4's
                                    ; colour indices would go out through
                                    ; BLIT4 on a Hercules as a thresholded
                                    ; picture and be drawn over at the next
                                    ; frame (review, wave 5) - so nothing is
                                    ; blitted and px_whole is KEPT: the
                                    ; worker's switch composes a whole frame
                                    ; (px_force) and its blit sends the band
.ok:
    mov al, [px_r0]
    mov ah, [px_r1]
    cmp byte [px_whole], 0
    je .range
    mov byte [px_whole], 0
    xor al, al
    mov ah, PX_ROWS - 1
.range:
    cmp al, ah
    ja .out
    cmp byte [px_back], PXB_WIN4    ; THE 16-COLOUR WINDOW (97.14): the
    jne .one                        ; world's bytes in strips - and a card,
    cmp byte [px_scard], 0          ; which is BITS, as WIN1 sends it
    jne .one
    jmp px_blit_w4
.one:
    push ax
    cmp byte [px_bpp], 1
    jbe .mono
    mov al, CLGRAY
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
.mono:
    pop ax
    mov cl, ah
    sub cl, al                      ; CL = rows - 1
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [px_row80 + bx]
    mov bl, [px_x0]
    xor bh, bh
    add si, bx
    inc cl
    xor ch, ch
    mov dx, cx                      ; DX = rows
    mov bx, [px_by]
    add bx, ax                      ; BX = y
    mov ax, [px_bx]                 ; AX = x
    mov cl, [px_size]               ; Size x 8 pixels (64 at most in a
    xor ch, ch                      ; window, px_res_apply)
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov bp, PX_STRIDE
    mov es, [px_shseg]
    call OSAPI_GFX_BLIT1
.out:
    ret

; -----------------------------------------------------------------------------
; px_blit_w4 - WIN4's present (97.14): rows AL..AH of the band, in strips of
;              PX_W4ROWS, each EXPANDED from the shadow's Mode X bytes (0..31)
;              through the 32 -> 16 table into the strip at PX_W4BUF and put
;              on the glass - by ONE OF TWO PRIMITIVES, chosen once a frame:
;
;              PLANAR (the usual case): the window is unobscured
;              (OSAPI_WM_OBSCURED) and OSAPI_GFX_BLITP's probe (SPEC.md
;              5.4.3.2) says yes for the band. The clip px_lock_clip armed is
;              disarmed (an armed region is one of BLITP's refusals, and an
;              unobscured window has nothing to cut against), each strip is
;              expanded into FOUR PLANES - a WIN4 byte is eight pixels of one
;              colour at an 8-aligned x, so every plane byte is 0x00 or 0xFF
;              - and goes out with ONE OSAPI_GFX_BLITP: the bytes and nothing
;              else, where BLIT4 sent a textured row whole to the 106.9-clk
;              plane decoder (97.14's table). The strip's row is 256 bytes,
;              plane p at +64p (plane step 64, row stride 256), so the four
;              stores of a pair reach their planes on 8-bit displacements.
;              The clip is re-armed after the last strip, for the bar and
;              the line.
;
;              PACKED (the fallback: covered, a straddle, a probe that says
;              no, or a BLITP that refuses after it said yes): four bytes of
;              packed 4bpp a shadow byte - eight pixels, the colour in both
;              nibbles - and ONE OSAPI_GFX_BLIT4 (SPEC.md 5.4.1) a strip,
;              which honours the clip. A refused BLITP re-arms the clip and
;              sends the SAME strip again this way, and the rest of the frame
;              follows it.
;
;              Four strips for the whole band (25 + 25 + 25 + 5 rows), one
;              for a range of 25 or fewer. The source, the table and the strip
;              are one segment - the shadow claim - so no inner-loop byte
;              carries an override. The lock held, the clip armed,
;              px_geom_win done.
; clobbers AX, BX, CX, DX, SI, DI, BP, ES
; -----------------------------------------------------------------------------
px_blit_w4:
    mov [px_w4r], al
    mov [px_w4e], ah
    mov byte [px_w4pl], 0           ; ONE CHOICE A FRAME, under the lock:
    mov bx, [px_win]                ; nothing can move the window between
    call OSAPI_WM_OBSCURED          ; here and the last strip
    jc .strip                       ; covered: BLIT4 through the clip
    call OSAPI_WM_CLIP_CLEAR        ; (BLITP refuses an armed region)
    mov ax, [px_bx]
    mov bx, [px_by]
    call px_w4_wid                  ; CX = Size x 8 pixels
    mov dx, PX_ROWS
    mov di, 0x8000                  ; DI BIT 15: THE PROBE - nothing drawn
    call OSAPI_GFX_BLITP
    jc .rearm                       ; no (a straddle, off the screen's side)
    mov byte [px_w4pl], 1
    jmp .strip
.rearm:
    call px_w4_clip                 ; the clip back for BLIT4
.strip:
    mov al, [px_w4r]
    mov ah, [px_w4e]
    sub ah, al
    inc ah                          ; the rows left
    cmp ah, PX_W4ROWS
    jbe .n
    mov ah, PX_W4ROWS
.n:
    push ax                         ; AL = the strip's first row, AH its rows
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [px_row80 + bx]
    mov bl, [px_x0]
    xor bh, bh
    add si, bx                      ; SI = the first row's band
    mov cl, ah                      ; CL = rows
    mov dl, [px_size]               ; DL = bytes a row
    mov dh, [px_sadv]               ; DH = the advance past the band
    mov bx, PX_W4TAB                ; THE ROW'S TABLE (wave 6): the even
    test al, 1                      ; rows' at PX_W4TAB, the odd rows' 32
    jz .ev                          ; on - a dark face is its lit colour on
    mov bl, (PX_W4TAB + 32) & 0xFF  ; one and black on the other, and each
.ev:                                ; row swaps them with one `xor`
    cmp byte [px_w4pl], 0
    jne .planes
    mov di, PX_W4BUF
    mov es, [px_shseg]
    push ds
    mov ds, [px_shseg]              ; ONE segment from here to the pop
    cld
.row:
    mov ch, dl
    shr ch, 1                       ; TWO BYTES A PASS: every Size is even
.b:                                 ; (48..64 in a window, px_sizes)
    lodsb                           ; NO MASK: every byte a compose writes is
    xlat                            ; 0..31 (Mode X's byte set, 97.14), and
    mov ah, al                      ; tests/pxswin.py --qemu holds the band
    stosw                           ; to it - the mask was 4 clk and 2 fetched
    stosw                           ; bytes a shadow byte, ~9 ms a frame on
    lodsb                           ; the 8088 (review, wave 5)
    xlat
    mov ah, al
    stosw
    stosw
    dec ch
    jnz .b
    mov al, dh
    xor ah, ah
    add si, ax
    xor bl, 0x20                    ; the next row's table
    dec cl
    jnz .row
    pop ds
.xdone:                             ; (--price: the strip expanded; the
    pop ax                          ; blit is from here to .bdone)
    add [px_w4r], ah                ; the next strip's first row
    mov dl, ah
    xor dh, dh                      ; DX = rows
    mov bl, al
    xor bh, bh
    add bx, [px_by]                 ; BX = y
    call px_w4_wid                  ; CX = Size x 8 pixels
    mov bp, cx
    shr bp, 1                       ; BP = the strip's stride: Size x 4 bytes
    mov ax, [px_bx]
    mov si, PX_W4BUF
    call OSAPI_GFX_BLIT4            ; ES:SI = the strip
.bdone:
    inc word [px_nb4]
.next:
    mov al, [px_w4r]
    cmp al, [px_w4e]
    jbe .strip
    cmp byte [px_w4pl], 0           ; the planar path took the clip away:
    je .out                         ; the bar and the line get it back
    call px_w4_clip
.out:
    ret

; THE PLANAR EXPAND: a shadow byte through the table is its colour in both
; nibbles, so bit 7 is plane 3's bit - `cbw` makes that bit a whole byte in
; AH (1 byte, 2 clk), and each `shl al, 1` brings the next plane's bit up
; to it. Four byte stores a shadow byte, no word to assemble (a word store
; is two bus cycles on the 8088 all the same). DI is plane 2's byte, so
; planes 0..3 are [di-128], [di-64], [di], [di+64] - 8-bit displacements.
; Two shadow bytes a pass (every Size is even). 97.14 has the price.
.planes:
    mov bp, dx                      ; BP = advance:size
    mov di, PX_W4BUF + 128
    push ds
    mov ds, [px_shseg]              ; ONE segment from here to the pop
    cld
.prow:
    push di
    mov ax, bp
    shr al, 1
    mov ch, al                      ; CH = pairs
.p:
%rep 2
    lodsb
    xlat                            ; AL = the colour, both nibbles
    cbw
    mov [di + 64], ah               ; plane 3
    shl al, 1
    cbw
    mov [di], ah                    ; plane 2
    shl al, 1
    cbw
    mov [di - 64], ah               ; plane 1
    shl al, 1
    cbw
    mov [di - 128], ah              ; plane 0
    inc di
%endrep
    dec ch
    jnz .p
    pop di
    add di, 256                     ; the strip's next row
    mov ax, bp
    mov al, ah
    xor ah, ah
    add si, ax
    xor bl, 0x20                    ; the next row's table
    dec cl
    jnz .prow
    pop ds
.pxdone:                            ; (--price: the planes expanded; the
    pop ax                          ; blit is from here to .pbdone)
    mov dl, ah
    xor dh, dh                      ; DX = rows
    mov bl, al
    xor bh, bh
    add bx, [px_by]                 ; BX = y
    call px_w4_wid                  ; CX = Size x 8 pixels
    push dx
    mov ax, [px_bx]
    mov es, [px_shseg]
    mov si, PX_W4BUF                ; ES:SI = plane 0's first row
    mov di, 64                      ; the plane step
    mov bp, 256                     ; the row stride inside a plane
    call OSAPI_GFX_BLITP
    pop dx
    jc .refused
.pbdone:
    add [px_w4r], dl                ; the next strip's first row
    inc word [px_nbp]
    jmp .next
.refused:                           ; REFUSED AFTER THE PROBE SAID YES (apps/
    mov byte [px_w4pl], 0           ; paccman's pmc_blit_band): the clip back
    call px_w4_clip                 ; and the SAME strip again, packed - and
    jmp .strip                      ; the rest of the frame with it

; px_w4_wid - CX = Size x 8, the band's width in pixels
px_w4_wid:
    mov cl, [px_size]
    xor ch, ch
    shl cx, 1
    shl cx, 1
    shl cx, 1
    ret

; px_w4_clip - re-arm the clip region px_lock_clip armed (the lock is still
;              held; CF is not read - the window was unobscured under this
;              same hold). clobbers BX, ES
px_w4_clip:
    mov bx, KERNEL_SEG
    mov es, bx
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    ret

; px_line_ck - the line, if it is owed (px_lined). The lock held,
;              px_geom_win done. (Wave 3's eight stat cells are gone: the
;              bar carries the health and the ammo since wave 4, 97.13)
px_line_ck:
    cmp byte [px_lined], 0
    jne px_line_draw
    ret

; -----------------------------------------------------------------------------
; px_line_draw - the text line: the rung, the resolution and the hint, one
;                OSAPI_FONT_RUN (SPEC.md 6.1) in white on black
; in:  the lock held, px_geom_win done.  clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
px_line_draw:
    mov byte [px_lined], 0
    mov di, px_lbuf
    mov si, px_s_ldet
    call px_strcat
    mov si, px_s_lwire
    cmp byte [px_rung], PXR_WIRE
    je .r
    mov si, px_s_ltex
    cmp byte [px_rung], PXR_TEX
    je .r
    mov si, px_s_lflat
.r:
    call px_strcat
    mov si, px_s_lsize              ; "  Size 64" - the size IN FORCE, which
    call px_strcat                  ; a window caps at 64 (px_res_apply)
    mov al, [px_size]
    aam                             ; AH = tens, AL = units
    add ax, '00'
    mov [di], ah
    mov [di + 1], al
    inc di
    inc di
    mov si, px_s_lfull
    cmp byte [px_lowres], 0
    je .q
    mov si, px_s_llow
.q:
    call px_strcat
    mov al, [px_lrefuse]            ; a refusal, once - the next line is
    mov byte [px_lrefuse], 0        ; the usual one:
    mov si, px_s_lnomode            ; 1 F or T with no mode (px_go_fsx),
    cmp al, 1
    je .h
    mov si, px_s_lnopause           ; 2 Game > Pause outside PLAY (px_oncmd)
    cmp al, 2
    je .h
    mov si, px_s_lpause
    cmp byte [px_pause], 0
    jne .h
    mov si, px_s_lhint
    cmp byte [px_tier], CPU_8086
    je .h
    mov si, px_s_lnone
.h:
    call px_strcat
    ; PADDED TO THE LAST LINE'S LENGTH (SPEC.md 27.2's trick: the padding IS
    ; the erase, one pass): "Full res" to "Low res" left an 's' on the
    ; glass, and unpausing left "PAUSED (P)" standing, because font_run
    ; paints only under its string
    ; ...AND HELD TO THE CONTENT'S WIDTH: the frame is PX_WINW = 528 px, 65
    ; cells of 8 with the border, and a line past that is drawn onto the
    ; desktop beside the window, where no pad of ours ever erases it -
    ; wave 3's health and ammo took "Textured  Size 64  Full res  Press F
    ; for full screen" to 69 cells and tests/pxsfsx.py found three cells of
    ; "een" standing right of the window after a Textured bracket
    mov ax, di
    sub ax, px_lbuf                 ; AX = this line's length
    cmp ax, PX_LINEMAX
    jbe .fits
    mov ax, PX_LINEMAX
    lea di, [px_lbuf + PX_LINEMAX]
.fits:
    mov cl, [px_llen]
    mov [px_llen], al
    xor ch, ch
    sub cx, ax
    jbe .nopad
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.nopad:
    mov byte [di], 0
    mov cx, [px_bx]
    mov dx, [px_ly]
    mov si, px_lbuf
    mov al, CWHITE
    mov ah, CBLACK
    call OSAPI_FONT_RUN
    ret

; px_dec3 - AL (0..255) in decimal at DI, no leading zeros. clobbers AX, CX
px_dec3:
    xor ah, ah
    mov cl, 100
    div cl                          ; AL = hundreds, AH = the rest
    or al, al
    jz .tens
    add al, '0'
    mov [di], al
    inc di
    mov al, ah
    aam
    add ax, '00'
    mov [di], ah
    mov [di + 1], al
    inc di
    inc di
    ret
.tens:
    mov al, ah
    aam                             ; AH = tens, AL = units
    or ah, ah
    jz .units
    add ah, '0'
    mov [di], ah
    inc di
.units:
    add al, '0'
    mov [di], al
    inc di
    ret

; px_strcat - append the NUL string at SI to DI (no NUL written)
px_strcat:
    cld                             ; reached from the worker and the UI task
.c:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short .c
.out:
    ret

; px_detail_str - the rung and the resolution in force at DI, "Textured  Low
;                 res": the bar's message when Auto steps in a bracket
;                 (97.13) - the same words the line uses. DI past it;
;                 clobbers AX, SI
px_detail_str:
    mov al, [px_rung]
    mov ah, [px_lowres]
; px_detail_stra - the same for AL = a rung, AH = a resolution (the timedemo
;                  card's, kept at the run's end). clobbers AX, SI
px_detail_stra:
    mov si, px_s_lwire
    cmp al, PXR_WIRE
    je .r
    mov si, px_s_ltex
    cmp al, PXR_TEX
    je .r
    mov si, px_s_lflat
.r:
    call px_strcat
    mov si, px_s_lfull
    or ah, ah
    je .q
    mov si, px_s_llow
.q:
    jmp px_strcat

px_s_ldet:   db 'Detail: ', 0
px_s_lwire:  db 'Wire', 0
px_s_lflat:  db 'Flat', 0
px_s_ltex:   db 'Textured', 0
px_s_lsize:  db '  Size ', 0
px_s_lfull:  db '  Full res', 0
px_s_llow:   db '  Low res', 0
px_s_lhint:  db '  F: full screen', 0   ; (16 cells: the line stays under PX_LINEMAX with the health and ammo on it)
px_s_lpause: db '  PAUSED (P)', 0
px_s_lnone:  db 0
px_s_lnomode: db '  No full-screen mode', 0
px_s_lnopause: db '  Pause is for play', 0  ; (no longer than the line above)

; -----------------------------------------------------------------------------
; px_focus_ck - told when we gain the front, never when we lose it (SPEC.md
;               44.8): both questions, and a sticky pause on losing
; -----------------------------------------------------------------------------
px_focus_ck:
    push ax
    push bx
    cmp byte [px_inbr], 0
    jne .out                        ; in a bracket we ARE the machine
    call OSAPI_WM_TOP
    cmp bx, [px_win]
    jne .away
    call OSAPI_MENU_OWNER           ; ...a click on the bare desktop hands
    cmp bx, [px_win]                ; the bar to Locator and moves nothing
    jne .away
    mov byte [px_hasfoc], 1
    jmp short .out
.away:
    mov byte [px_hasfoc], 0
    cmp byte [px_pause], 0
    jne .out
    cmp byte [px_state], PXST_PLAY  ; (a card is still already)
    jne .out
    mov byte [px_pause], 1          ; sticky: a game that restarts the instant
    mov byte [px_lined], 1          ; a window is raised is one nobody was
    call px_pause_msg               ; watching yet - and the bar says so
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_spawn_ck - the worker starts at the first paint (apps/dotdel's
;               dd_spawn_ck): OSAPI_TASK_SPAWN refuses from an entry proc
; -----------------------------------------------------------------------------
px_spawn_ck:
    cmp byte [px_spawned], 0
    jne .out
    cmp word [px_shseg], 0
    je .out                         ; nothing to draw with: no loop either
    push ax
    push bx
    mov ax, px_worker
    mov bx, [px_win]
    call OSAPI_TASK_SPAWN
    jc .no
    mov byte [px_spawned], 1
    OS88_WORKER_RESTARTABLE px_worker_rs    ; the other half (SPEC.md
                                    ; 66.6.2): without it task_spawn's frame
                                    ; pins the region however it is declared
.no:
    pop bx
    pop ax
.out:
    ret
