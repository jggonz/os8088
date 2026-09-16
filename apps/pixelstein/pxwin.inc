; =============================================================================
; os8088 - apps/pixelstein/pxwin.inc
;
; THE WINDOWED HALF (SPEC.md 96.3's WIN1, 96.8): the worker that is the game
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
; compose to finish before it asks for the freeze (96.8).
; =============================================================================

; -----------------------------------------------------------------------------
; px_worker - the windowed game loop (apps/dotdel's dd_worker, SPEC.md 93.6)
; -----------------------------------------------------------------------------
px_worker:
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
    call px_keys_read               ; the held keys, once at the first owed
    call px_kbdrain                 ; step; the steering scancodes eaten so
    cmp byte [px_pause], 0          ; a typed command keeps its place
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
; A WINDOW NOBODY CAN SEE COSTS NOTHING (SPEC.md 96.8): once a frame ends at
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
                                    ; px_abdismiss repaints (96.8)
    mov byte [px_composing], 1
    call px_frame_begin
    mov byte [px_composing], 0
    jc .frame
    cmp byte [px_whole], 0          ; nothing changed - but a W_PAINT that
    jne .whole                      ; landed mid-compose blackened the glass:
    cmp byte [px_lined], 0          ; the band goes back whole, NO cast (the
    je .out                         ; first cut owed a whole frame, 34 ms of
    call px_lock_clip               ; cast for a compose that then wrote
    jc .lskip                       ; nothing) - or the text line alone (a
    call px_geom_win                ; pause, 96.8)
    call px_line_draw
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
    cmp byte [px_lined], 0
    je .wnol
    call px_line_draw
.wnol:
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
    call px_blit_win
    cmp byte [px_lined], 0
    je .nol
    call px_line_draw
.nol:
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

; px_lock_clip - the lock, then the two questions (apps/dotdel's dd_render):
;                out CF = 1 not a pixel of us is showing, the lock still held
px_lock_clip:
    call px_wait_begin              ; another task's hold on the lock is not
    call OSAPI_GFX_LOCK             ; this frame's work (96.8): what the
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
; clobbers AX, BX, CX, DX, SI, BP, ES
;
; ONE call a frame at most. ES:SI is the shadow's first dirty row at the
; band's byte, BP the shadow stride, the width 512 pixels and the height the
; row range - WHICH IS THE WHOLE BAND ON EVERY FRAME A PLAYER SEES (96.5):
; the range is one min/max over all columns, any column nearer than 2.5
; tiles spans rows 0..79, and a turn moves one of those in every scene, so
; the measured blit is 80 rows and 24.7 ms on the 5150 whatever moved. The
; range collapses only on a frame in which no near column changed. The pen
; is light grey on black on a 4bpp display; on a 1bpp one it is ignored and
; the band's bits are the picture (SPEC.md 5.4.2.2). px_whole is a W_PAINT's
; word (96.5): the GLASS was blackened, the shadow is still the whole
; picture, so the next blit sends all of it whatever the compose wrote -
; and px_r0/px_r1 stay the worker's own bytes, never a paint's
; -----------------------------------------------------------------------------
px_blit_win:
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
    add si, PX_X0
    inc cl
    xor ch, ch
    mov dx, cx                      ; DX = rows
    mov bx, [px_by]
    add bx, ax                      ; BX = y
    mov ax, [px_bx]                 ; AX = x
    mov cx, PX_BAND * 8             ; 512 pixels
    mov bp, PX_STRIDE
    mov es, [px_shseg]
    call OSAPI_GFX_BLIT1
.out:
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
    mov si, px_s_lflat
.r:
    call px_strcat
    mov si, px_s_lfull
    cmp byte [px_lowres], 0
    je .q
    mov si, px_s_llow
.q:
    call px_strcat
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
    mov ax, di
    sub ax, px_lbuf                 ; AX = this line's length
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

px_s_ldet:   db 'Detail: ', 0
px_s_lwire:  db 'Wire', 0
px_s_lflat:  db 'Flat', 0
px_s_lfull:  db '  Full res', 0
px_s_llow:   db '  Low res', 0
px_s_lhint:  db '  Press F for full screen', 0
px_s_lpause: db '  PAUSED (P)', 0
px_s_lnone:  db 0

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
    mov byte [px_pause], 1          ; sticky: a game that restarts the instant
    mov byte [px_lined], 1          ; a window is raised is one nobody was
.out:                               ; watching yet
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
.no:
    pop bx
    pop ax
.out:
    ret
