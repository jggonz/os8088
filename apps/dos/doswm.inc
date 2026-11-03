; =============================================================================
; os8088 - apps/dos/doswm.inc
;
; **INT 33h IN A WINDOW** (SPEC.md 96.54.4, docs/plans/DOS-WINDOWED-PLAN.md
; wave 5). A program running live in its window (dosw.inc) sees a virtual
; 80x25 text screen that is the window's console band. Its mouse has to be on
; THAT screen: column * 8 and row * 8 over INT 33h's 640x200, measured from the
; band's top-left and the first buffer row the band shows - not the desktop's
; pointer scaled as if the program owned the glass, which is what the
; bracket's translation (96.10) answers and what a window got until this file.
;
; **THE WHOLE OF IT IS TWO HOST HOOKS** (96.44.3). Every INT 33h answer the
; core gives - 03h, 05h/06h, 0Bh's derived mickeys, the 07h/08h window, the
; 0Ch event handler off the tick - is computed downstream of `DHK_MOUSE`, so
; translating the one read translates every function, and the core does not
; change by a byte. `dwm_start` binds both hooks for the run and `dwm_stop`
; puts the bracket's back; each hook also tests `[dvw_run]`/`[dvw_fs]` itself,
; so the full screen of 96.54.1 gets the bracket's whole-screen translation
; unchanged (`dos_vw`/`dos_vh` are set for the run in `dvw_runprog`).
;
; **THE BUTTONS ARE THE WINDOW'S ONLY WHEN THE PRESS WAS** - DESQview's rule
; for the foreground window, made exact: a button is reported down iff its
; DOWN EDGE was read while the pointer was over the band and the window was
; frontmost at this read AND at the one before it, and then it stays down
; until it is released wherever the pointer has gone (a drag clamps, it does
; not let go). So the click that RAISES a window behind another is the
; desktop's and never the program's, whichever side of the raise the program
; happened to poll on; a press begun on the title bar and dragged in is not a
; press; and a position outside the band is clamped to its edge, as a driver
; clamps to the screen.
;
; **THE BAND IS ASKED FOR, NOT REMEMBERED.** `[dos_conx]`/`[dos_cony]` are
; written at a PAINT, and a moved window calls no paint proc (SPEC.md
; 11.96.12) - so a quiet program whose window was dragged would be handed the
; old origin for ever. The origin is `OSAPI_WM_CONTENT` plus dos_con_geom's
; own margin, recomputed from `OSAPI_WM_GEOM` once a BIOS tick (dwm_sample,
; and dwm_mouse's head says why not every read); the COLUMN
; and ROW counts are the paint's, which only a resize changes and a resize
; repaints. THE TWO MUST AGREE: this is dos_con_geom's arithmetic, kept beside
; it in words rather than shared, because that routine also tells the console
; (`dos_con_pub`) and may not run from an interrupt.
;
; **NO TEXT CURSOR.** In the window the pointer is the desktop's own arrow,
; so `DHK_TXT` refuses (96.10.5.1's windowed case) - and FORGETS any cell the
; core believes it drew: `dos_fsi` outlives the bracket that set it, so a
; window run after a bracketed one would otherwise have the core wipe a stale
; B800 cell, which on a CGA desktop is the desktop's own framebuffer.
; =============================================================================

%ifdef DVW
%ifndef DVW_SPLIT                   ; the part's, whole (doswabi.inc)
%ifndef DVW_PART                    ; ...whose own cells doswp.asm lays out
dwm_own     equ dos_hbss + DOS_B_DWM + 0    ; the buttons the program has
dwm_last    equ dwm_own + 1                 ; the live mask at the last read
dwm_wasf    equ dwm_own + 2                 ; 1 = in front at the sample before
dwm_frnt    equ dwm_own + 3                 ; 1 = in front (and shown) now
dwm_tick    equ dwm_own + 4                 ; word: the tick of that sample
dwm_bx      equ dwm_own + 6                 ; word: the band's left...
dwm_by      equ dwm_own + 8                 ; word: ...and top, on the screen
%endif

; -----------------------------------------------------------------------------
; dwm_start - bind the window's two mouse hooks (dvw_runprog, worker)
; dwm_stop  - ...and give the bracket's back (dvw_fin)
; clobbers: nothing
;
; dwm_start runs BEFORE dos_hook_vectors, whose dos_m33_hidden wipes: the
; cell [dos_m33dv] names is on a screen this run does not have. It also
; clears [dos_m33hk] (dos_m33_arm's hook-once flag), which nothing else
; clears.
; -----------------------------------------------------------------------------
dwm_start:
    push es
    push ax
    xor ax, ax
    mov [PS dwm_own], ax               ; dwm_own and dwm_last...
    mov [PS dwm_wasf], ax              ; ...and both samples "behind"
    mov [dos_m33dv], ax
                                    ; ([dos_m33hk] is dos_hook_vectors' to
                                    ; clear now, for every run, SPEC.md
                                    ; 96.54.8 - it was cleared here, for the
                                    ; window's runs only)
    mov al, 0x40                    ; AX = 0040h: AH is the 0 above
    mov es, ax
    mov ax, [es:0x6C]
    dec ax                          ; a tick that is not now: the first read
    mov [PS dwm_tick], ax              ; samples
    pop ax
    pop es
    mov word [dos_hkv + DHK_MOUSE], HENT(dwm_mouse)
    mov word [dos_hkv + DHK_TXT], HENT(dwm_txt)
    ret

dwm_stop:
    mov word [dos_hkv + DHK_MOUSE], dos_hk_mouse
    mov word [dos_hkv + DHK_TXT], dos_hk_txt
    ret

; -----------------------------------------------------------------------------
; dwm_kick - the event handler, as the worker wakes (dvw_nap, world in)
; in:  the caller's stack, IF = 0; out: every register preserved
;
; A program waiting on a key in its window sleeps through its ticks with its
; world OUT, so IRQ0 lands on the desktop's vector and the core's
; `dos_m33_tick` - which is in the PROGRAM's table - never runs: an `0Ch`
; handler installed by a program that then waits for input (Microsoft Works'
; whole mouse) was never called. Every wake is where that tick would have
; been, so the dispatch runs here instead, with the tick's own state: what
; it already reported it does not report twice.
; -----------------------------------------------------------------------------
dwm_kick:
    push ds
    HOSTDS
    HCALL dos_m33_evt
    pop ds
    ret

; -----------------------------------------------------------------------------
; dwm_txt - DHK_TXT for a run in the window: none, and nothing left drawn
; -----------------------------------------------------------------------------
dwm_txt:
    call dwm_desk
    jc .desk                        ; the full screen has the bracket's text
    mov word [dos_m33dv], 0         ; screen, and the bracket's hook
    stc
    ret
.desk:
    HJMP dos_hk_txt

; --- dwm_desk - CF=0 = a run IN THE WINDOW, CF=1 = no run or the full screen -
; [dvw_run] set and [dvw_fs] clear; both hooks' first question
dwm_desk:
    cmp byte [dvw_run], 1
    jb .r                           ; no run: CF=1
    cmp byte [PS dvw_fs], 1            ; CF=1 when NOT on the full screen...
    cmc                             ; ...and the other way up
.r:
    ret

; -----------------------------------------------------------------------------
; dwm_mouse - DHK_MOUSE for a run in the window
; out: BX = the buttons the program may see, CX = x, DX = y on the virtual
;      640x200 text screen; every other register preserved
;
; Reached from INT 33h on the program's stack, from `dos_getkey`'s poll and
; from `dos_m33_tick` inside IRQ0. The kernel calls are readers that take no
; lock (OSAPI_MOUSE, WM_GEOM, WM_CONTENT, WM_TOP), so all three paths may
; make them.
;
; **THE WINDOW IS SAMPLED ONCE A TICK, NOT ONCE A READ.** GEOM, CONTENT and
; TOP together measured ~10,000 cycles on the 8088 - a poll loop of `03h` and
; `05h` ran 143 times a second against 428 with the bracket's hooks - so the
; band's origin and "am I in front" are refreshed when the BIOS tick at
; 0040:006C has moved since the last read, and a read inside the same tick
; costs OSAPI_MOUSE and arithmetic. A dragged window's origin is therefore up
; to one tick late, which the pointer is too.
;
; "In front at the read before" is the PREVIOUS SAMPLE, and samples are taken
; only by reads - so the first read after a press always has, as its previous
; sample, one taken before the press: the click that raised the window from
; behind is never the program's, however long the program went without
; asking. (The price: a window brought forward by other means while the
; program read nothing has its first click after that swallowed too.)
; -----------------------------------------------------------------------------
dwm_mouse:
    call dwm_desk
    jnc .win
    HJMP dos_hk_mouse                ; the full screen: the bracket's scale
.win:
    push ax
    push si
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]               ; the BIOS tick, low word
    pop es
    cmp ax, [PS dwm_tick]
    je .cached
    mov [PS dwm_tick], ax
    call dwm_sample
.cached:
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = the buttons
    and al, 3
    mov bx, [PS dwm_wasf]              ; BL = in front at the sample before, BH
    and bh, bl                      ; = now (0 when hidden, dwm_sample): both
    neg bh                          ; BH = FF ours, 00 NOT OURS
    mov bl, al                      ; BL = the live mask
    xchg ax, cx                     ; AX = the pointer's x...
    sub ax, [PS dwm_bx]                ; ...IN the band
    mov si, [dos_concols]
    call dwm_clamp                  ; into 0..cols*8-1, which is <= M33_VW-1
    xchg ax, dx                     ; DX = x, AX = the y in the band
    sub ax, [PS dwm_by]
    mov si, [dos_conrows]
    call dwm_clamp                  ; ...into the rows shown
    mov si, [con_vtop]              ; ...from the first buffer row shown
    mov cl, 3
    shl si, cl
    add ax, si
    cmp ax, M33_VH - 1
    jbe .ycap
    mov ax, M33_VH - 1
.ycap:
    xchg ax, dx                     ; DX = y, AX = x
    xchg ax, cx                     ; CX = x
    mov ah, [PS dwm_last]
    mov [PS dwm_last], bl
    not ah
    and ah, bl                      ; AH = the buttons gone DOWN since then...
    and ah, bh                      ; ...that are the window's
    mov al, [PS dwm_own]
    and al, bl                      ; a release lets go wherever it happens
    or al, ah
    mov [PS dwm_own], al
    cbw                             ; AH = 0: AL is two bits
    xchg ax, bx
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; dwm_sample - the window, once a tick: the band's origin and "in front"
; out: [dwm_bx]/[dwm_by], [dwm_frnt] (0 when hidden or behind), [dwm_wasf] =
;      the sample before; clobbers AX, BX, CX, DX
;
; The origin is dos_con_geom's (see the file's head): content origin plus the
; centring margin over [dos_concols] cells, and DOS_BARH down.
; -----------------------------------------------------------------------------
dwm_sample:
    mov al, [PS dwm_frnt]
    mov [PS dwm_wasf], al
    mov bx, [dos_win]
    call OSAPI_WM_GEOM              ; CX = the content width; CF = hidden
    sbb al, al
    inc al                          ; AL = 1 visible, 0 hidden
    mov [PS dwm_frnt], al
    mov ax, [dos_concols]
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; AX = the band's width in pixels
    sub cx, ax
    jnc .slack                      ; a window shrunk since the paint: no
    xor cx, cx                      ; margin, until the repaint says otherwise
.slack:
    shr cx, 1
    and cx, 0xFFF8
    call OSAPI_WM_CONTENT           ; AX = content left, DX = its top
    add ax, cx
    mov [PS dwm_bx], ax
    add dx, DOS_BARH
    mov [PS dwm_by], dx
    call OSAPI_WM_TOP               ; BX = the frontmost window
    cmp bx, [dos_win]
    je .front
    mov byte [PS dwm_frnt], 0
.front:
    ret

; -----------------------------------------------------------------------------
; dwm_clamp - AX (signed) into 0..SI*8-1; BH = 0 when it had to move
; clobbers CL, SI. **NO X CAP FOLLOWS IT**: [dos_concols] is at most
; DOS_CONCOLS (dos_con_geom), so the x it answers is at most M33_VW - 1
; -----------------------------------------------------------------------------
%if DOS_CONCOLS * 8 > M33_VW
%error "dwm_mouse relies on DOS_CONCOLS * 8 <= M33_VW: put its x cap back"
%endif
dwm_clamp:
    mov cl, 3
    shl si, cl                      ; SI = the cells, in pixels
    or ax, ax
    js .zero
    cmp ax, si
    jb .out
    mov ax, si
    dec ax
    jns .moved
.zero:
    xor ax, ax                      ; left of the band, or no cells at all
.moved:
    mov bh, 0
.out:
    ret

%endif                              ; DVW_SPLIT
%endif                              ; DVW
