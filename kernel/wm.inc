; =============================================================================
; wm.inc - window manager (SPEC.md 11)
;
; Window records, z-order, frame drawing, hit testing, paint-all. Every used
; window is always present in wm_zord (index 0 = backmost); membership is set
; at wm_create and removed only by wm_destroy - wm_hide only clears the
; visible bit, and wm_show/wm_front only reorder. The visible flag alone
; decides whether a window is painted, hit-tested, or counted by
; wm_top/wm_obscured.
;
; All drawing here goes through the vga12/font primitives with [gfx_color];
; every caller of a repainting routine already holds the gfx lock (SPEC 1.6).
; =============================================================================

; --- window record, 20 bytes, fixed offsets (SPEC.md 11) ---------------------
W_FLAGS equ 0                   ; word: bit0 = used, bit1 = visible,
                                ; bit2 = resizable, bit3 = fullscreen,
                                ; bit4 = content-origin snapped (SPEC.md 11.94)
W_X     equ 2                   ; word, frame left (screen coords)
W_Y     equ 4                   ; word, frame top (below menu bar: y >=
                                ; MBAR_H - except under WF_FULL, SPEC 11.2)
W_W     equ 6                   ; word, frame width  (outer, includes border)
W_H     equ 8                   ; word, frame height (includes title bar)
W_TITLE equ 10                  ; word: near ptr to NUL title string
W_PAINT equ 12                  ; word: near ptr, in: SI = window ptr
W_ONKEY equ 14                  ; word: near ptr or 0; AL=ascii AH=scan SI=win
W_ONCLICK equ 16                ; word: near ptr or 0; CX=x DX=y SI=win, called
                                ; under the gfx lock for content clicks on the
                                ; front window (SPEC.md 13)
W_MENUS equ 18                  ; word: near ptr to this window's app menu set
                                ; (SPEC.md 12.2), 0 = no menus of its own.
                                ; NOT a template word - wm_create zeroes it;
                                ; a built-in's KD_INIT or a package's
                                ; menu_win_set fills it in. Whichever window
                                ; is frontmost owns the menu bar (SPEC.md 12)
W_DISP  equ 20                  ; dword: the far pointer wm_pkgcall calls to
W_SEG   equ 22                  ; reach this window's procs - {PKG_DISP, the
                                ; owning package's segment}. W_SEG doubles as
                                ; the SEGMENT every near pointer in this
                                ; record is relative to: title, paint, onkey,
                                ; onclick, menus. Zero for a kernel window,
                                ; and zero is what makes the dispatch near
                                ; again for About/Timer/Disk/TaskMgr/Ctrl and
                                ; the file dialog (SPEC.md 11/20.1).
W_ONSIZE equ 24                 ; word: near ptr or 0 - the resize negotiator
                                ; (SPEC.md 11.1). Called BEFORE a new size is
                                ; committed, with SI = window, CX/DX = the
                                ; proposed frame size; it returns the size it
                                ; will accept in CX/DX, which may be the one
                                ; the window already has (a refusal) or
                                ; anything in between. NOT a template word:
                                ; wm_create zeroes it and wm_onsize sets it,
                                ; so no existing window template changes.
W_ONMOUSEUP equ 26              ; word: near ptr or 0 - the release half of a
                                ; content click (SPEC.md 13.7). CX = x, DX = y,
                                ; SI = window, same environment as W_ONCLICK.
                                ; Called ONLY if this window's W_ONCLICK ran
                                ; for the matching press, and called EVEN IF
                                ; the release landed outside the window - a
                                ; package that is never told cannot un-draw a
                                ; pressed state or decline to act. NOT a
                                ; template word: wm_create zeroes it and
                                ; wm_onmouseup sets it, so no existing window
                                ; template changes (the W_ONSIZE precedent)
%ifdef KERN_BIG
; --- SPEC.md 13.8.2 and 13.9 are kern_big's ALONE -----------------------------
; Three words and six bytes a record, which is 72 bytes of .bss across
; MAX_WIN, plus the routines that walk them. kern_small was measured at ZERO
; footprint spare when this split was made and it is the guard the 128KB
; machine lives under - so it carries the API slots as refusing stubs
; (gfx_blit1's precedent, SPEC.md 5.4.2), and none of the state. WIN_SIZE is
; therefore 34 here and 28 there, which is safe because a window INDEX never
; leaves the kernel: a package is given a pointer, and nothing outside
; kernel/ strides this table (checked - no package names WIN_SIZE at all).
W_ONDRAG equ 28                 ; word: near ptr or 0 - the pointer MOVED while
                                ; this window's press was armed (SPEC.md
                                ; 13.8.2). CX = x, DX = y, SI = window, the
                                ; same environment as W_ONCLICK and
                                ; W_ONMOUSEUP, through the same ui_ptcall.
                                ; Delivered between the two edges of ONE
                                ; gesture, only when the point CHANGED, and
                                ; outside the window as freely as the release
                                ; - a control cannot un-draw itself as the
                                ; pointer leaves if it is not told it left.
                                ; NOT a template word: wm_create zeroes it and
                                ; wm_ondrag sets it, so no existing window
                                ; template changes (W_ONSIZE's precedent, and
                                ; W_ONMOUSEUP's after it)
W_ONTIMER equ 30                ; word: near ptr or 0 - the one-shot timer's
                                ; handler (SPEC.md 13.9). CX = DX = 0,
                                ; SI = window; W_ONCLICK's environment, through
                                ; the same ui_ptcall. NOT a template word:
                                ; wm_create zeroes it, wm_ontimer sets it
W_TIMER  equ 32                 ; word: the deadline in [ticks], 0 = DISARMED.
                                ; wm_timer writes it; the scan clears it
                                ; BEFORE dispatching, so a handler that wants
                                ; a repeat re-arms inside itself and one that
                                ; does not fires exactly once. Clearing after
                                ; would silently cancel that re-arm
WIN_SIZE equ 34                 ; NOT a power of two: never shift, multiply
%else
WIN_SIZE equ 28                 ; ...and kern_small stops where SPEC.md 13.7
%endif                          ; left it
MAX_WIN  equ 12                 ; mirrored in apps/os88api.inc (SPEC.md 11)

; --- the title bar's two boxes (SPEC.md 11) ----------------------------------
; The close box on the left, the minimize box mirrored at the right, both the
; same size and both on the same rows. NAMED rather than written out at the
; four places wm_hit compares against them, because a scripted click has to
; land inside one and the harness had no way to ask: tests/xmcheck.py wrote
; down (W_X+14, W_Y+5) for a window it assumed sat at x=180, the window came
; up at 175, the click landed on the TITLE BAR instead of the box, and the
; gate reported an extended-memory LEAK for a window that had never closed.
; tools/os88geom.py mirrors these four and tests/unit/t_mirror.py fails on a
; stale copy, which is what makes the harness's arithmetic follow the kernel.
WM_BOX_X0 equ 8                 ; the close box's left, relative to W_X...
WM_BOX_W  equ 10                ; ...and its width, so it spans X0 .. X0+W
WM_BOX_Y0 equ 4                 ; both boxes' top and bottom, relative to W_Y
WM_BOX_Y1 equ 14

WF_SIZABLE equ 4                ; W_FLAGS bit2: resizable (SPEC.md 11.1)
WF_FULL    equ 8                ; W_FLAGS bit3: fullscreen (SPEC.md 11.2)
WF_SAVEU   equ 32               ; W_FLAGS bit5: this window PROMISES that its
                                ; content does not change while it is not
                                ; drawing, so the raise cache may put its old
                                ; pixels back instead of asking for new ones
                                ; (SPEC.md 11.96.1). Opt-in, and it has to be:
                                ; only the application knows
WF_OWNBG   equ 64               ; W_FLAGS bit6: this window PAINTS EVERY PIXEL
                                ; of its content itself, so wm_draw_win's white
                                ; fill is skipped for it (SPEC.md 11.90.1).
                                ; Opt-in, because the fill is what every other
                                ; app's W_PAINT draws on top of
WF_NOSNAP  equ 16               ; W_FLAGS bit4: this window OPTS OUT of having
                                ; its CONTENT origin kept on a multiple of 8
                                ; (SPEC.md 11.94/6.1). The SENSE IS INVERTED
                                ; on purpose - alignment is the DEFAULT for
                                ; every window in the system, and this bit is
                                ; how the rare window that must sit where it
                                ; was put says so. wm_create writes W_FLAGS =
                                ; 1 and wm_destroy writes 0, so a fresh or
                                ; reused record is snapped with nothing to
                                ; set and no template word to carry
WF_NOANIM  equ 0x4000          ; W_FLAGS bit14: this window does NOT zoom on
                               ; show or destroy (SPEC.md 11.99.2). THE TOP OF
                               ; the shape field and not its bottom, for the
                               ; reason WF_STALE gives below: the REST of that
                               ; byte is SPEC.md 7.2.1's cursor shape, and at
                               ; bit 8 this bit IS CUR_CROSSSH. Bits 9-14 are
                               ; free because both writers refuse a shape at
                               ; or past CUR_NSHAPE (wm_cursor here,
                               ; cur_shape_set in mouse.inc), so the field
                               ; only ever holds 0 or 1 - but the two places
                               ; that touch the byte must mask this bit off
                               ; with WF_STALE. kern_big only - kern_small has
                               ; no animation to opt out of, and the bit is
                               ; defined in both so a test is not %ifdef'd
WF_1BPP    equ 0x2000          ; W_FLAGS bit13: EVERY PIXEL OF THIS WINDOW'S
                               ; CONTENT IS COLOUR 0 OR COLOUR 15 (SPEC.md
                               ; 11.96.17). The second half of 11.96.1's
                               ; promise, carried on the same call -
                               ; wm_saveu's AL bit 1 - because a claim about
                               ; the depth means nothing without the
                               ; save-under it exists to make affordable.
                               ; Four identical planes are one plane of
                               ; content, so the claim is worth a QUARTER of
                               ; the memory and a quarter of the write
                               ; traffic: Map Mask 0Fh with Set/Reset
                               ; disabled puts one CPU byte in all four,
                               ; which is the resting state 5.4.2.2 already
                               ; ships on. In this byte with the other two
                               ; kernel bits and not down at 8, for WF_STALE's
                               ; reason below
WF_STALE   equ 0x8000          ; W_FLAGS bit15: THE PIXELS ON THE GLASS NO
                               ; LONGER DESCRIBE THIS WINDOW'S CONTENT (SPEC.md
                               ; 11.96.16.1), so nothing may bank them until it
                               ; has been drawn whole again. THE TOP BIT of the
                               ; high byte and not its bottom one, because the
                               ; REST of that byte is SPEC.md 7.2.1's cursor
                               ; shape: at bit 8 this bit WAS CUR_CROSSSH, so a
                               ; window handed a new listing wore the aiming
                               ; crosshair and a package asking for one refused
                               ; itself a cache. Kernel-internal and
                               ; deliberately NOT published in apps/os88api.inc:
                               ; a package cannot get into this state, because
                               ; the only way in is the kernel replacing a
                               ; window's data behind its back - fmv_store
                               ; swapping a Disk window's listing for a folder
                               ; that changed (SPEC.md 22.14). Set THERE and
                               ; cleared in fm_repaint, the one place a Disk
                               ; window is drawn from that listing - NOT in
                               ; wm_draw_win, which every navigation bypasses;
                               ; and read in ONE place, wm_su_take
; THE KERNEL'S BITS IN THE SHAPE BYTE, named once (SPEC.md 11.96.17). This
; was a rule in prose - WF_NOANIM's banner says "the two places that touch the
; byte must mask this bit off with WF_STALE" - and a prose rule is one a third
; flag can be half-added to: wm_cursor KEEPS these bits when it writes a shape
; and mou_apply DROPS them when it reads one, so a bit added to one mask and
; not the other either eats the shape or is read as one. Both derive from here
; now, so there is one place to add the next.
WF_HIBITS  equ (WF_STALE | WF_NOANIM | WF_1BPP) >> 8
%if WF_HIBITS & (CUR_NSHAPE - 1)
  %error "WF_HIBITS overlaps the cursor shape field - SPEC.md 7.2.1/11.96.17"
%endif

WF_KEEPH   equ 128             ; W_FLAGS bit7: this window's HEIGHT is not the
                                ; kernel's to reduce (SPEC.md 11.93). wm_fit
                                ; shortens a frame that will not fit the
                                ; desktop band, which is right for anything
                                ; that reflows and wrong for a fixed layout -
                                ; the content is still DRAWN, it is merely
                                ; outside the frame, so it takes no clicks.
                                ; Opt-in, because only the application knows
                                ; whether its layout can give a row up
WMIN_W     equ 96               ; smallest frame a resize can leave, outer px
WMIN_H     equ 64               ; (SPEC.md 11.1; mirrored in apps/os88api.inc)

; -----------------------------------------------------------------------------
; wm_strseg - point ES at the segment a window's strings live in
; in:  DI = window record
; out: ES = [DI+W_SEG], or KERNEL_SEG when that is 0
; clobbers: ES (the output) and nothing else
;
; W_TITLE, and every string a package's menu set points at, are offsets in
; the OWNER's segment. One place decides which that is.
; -----------------------------------------------------------------------------
wm_strseg:
    push cs
    pop es
    cmp word [di+W_SEG], 0
    je .out
    mov es, [di+W_SEG]
.out:
    ret

; -----------------------------------------------------------------------------
; wm_pkgcall - invoke one of a window's callbacks, wherever its code lives
;
; in:  SI = window record, BP = the callback's offset (non-zero), every other
;      register = that callback's own arguments. SI rather than BX because
;      EVERY callback contract in SPEC.md 11/12.2/38.6 already passes the
;      window in SI - so the dispatch site has nothing to shuffle, and BX
;      stays free for the one callback that wants it (AM_ONCMD's menu set).
; out: whatever the callback returns, CF included
; clobbers: BP (it is the input) - DS and ES are restored
;
; W_SEG = 0 is a kernel window and the call is an ordinary near one. Anything
; else is a package: DS becomes its segment, ES is pointed at KERNEL_SEG (the
; window record it was handed lives there, and so does the file dialog's name
; buffer - a package reads both through ES, SPEC.md 20.5), and the far call
; goes to the DISPATCHER at PKG_DISP in the package's own image, which is a
; three-byte `call bp / retf`. That indirection is what lets every package
; callback stay an ordinary near proc with an ordinary near `ret`: a package
; author never writes `retf`, and a missed one cannot exist.
;
; Re-entrant by construction - the far pointer is read out of the record, not
; out of a global - which it has to be: a package's callback may call
; OSAPI_WM_SHOW, which repaints, which dispatches another package's paint
; proc.
; -----------------------------------------------------------------------------
wm_pkgcall:
    push word [gfx_mono1]       ; WHOSE CONTENT IS ABOUT TO BE DRAWN (SPEC.md
                                ; 11.96.17.1), banked exactly as snap_cur is
                                ; below and for its reason: a repaint pass
                                ; calls several windows' callbacks inside one
                                ; lock hold, so the question is WHICH window,
                                ; not how deep. Outside the ds/es pair, so the
                                ; matching pop is too and the stack stays
                                ; symmetric; the SET is inside, where ES is
                                ; the kernel and the record can be read
%ifdef SNAPAUDIT
    push word [snap_cur]        ; OUTSIDE the ds/es pair, because the restore
                                ; below addresses DS and DS is the PACKAGE's
                                ; until `pop ds` runs - inside it, the write
                                ; landed 0xD5F9 bytes past the package's base
                                ; (SPEC.md 51.2.4, which is drv_call's bug in
                                ; the audit build)
%endif
    push ds
    push es
    push cs
    pop es                      ; ES = KERNEL_SEG: the record lives here and
                                ; the callback may read it
%ifdef SNAPAUDIT
    mov [snap_cur], si          ; SPEC.md 11.94.2: everything drawn from here
                                ; down belongs to THIS WINDOW, and this is the
                                ; one site that covers both kinds - a package
                                ; through its dispatcher, a kernel window
                                ; through BP. Stacked rather than a depth
                                ; count, because a repaint pass calls several
                                ; windows' callbacks and the QUESTION is which
                                ; window drew, not how deep we are. PUSH and
                                ; MOV write no flags, so the cmp below is
                                ; unaffected
%endif
    push ax                     ; ...and it is this window's claim now. AX is
    mov al, [es:si+W_FLAGS+1]   ; the callback's, so it is banked around three
    and al, WF_1BPP >> 8        ; instructions rather than spent - and the
    mov [gfx_mono1], al         ; LOW BYTE alone is written, which is what
    pop ax                      ; keeps the word above safe to push and pop
    cmp word [es:si+W_SEG], 0   ; (the cmp sets the flags the branch reads, so
    je .near                    ; the `and` above cannot reach it)
    mov ds, [es:si+W_SEG]       ; DS = the package's own segment
    call far [es:si+W_DISP]     ; ...and off to its dispatcher
    jmp short .out
.near:
    call bp                     ; a kernel window: DS is already ours
.out:
    pop es
    pop ds
%ifdef SNAPAUDIT
    pop word [snap_cur]         ; POP writes no flags, and a callback returns
                                ; values IN THEM (CF from an entry proc,
                                ; W_ONSIZE's answer) - so this needs no
                                ; pushf/popf around it and must not grow one
%endif
    pop word [gfx_mono1]        ; ...and the outer window's claim is back.
                                ; A pop writes no flags either, which the
                                ; callback's own CF depends on
    ret

; -----------------------------------------------------------------------------
; wm_init - zero the window table, z-order array and count; orphan wm_owner
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_init:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor ax, ax
    mov di, wm_wins             ; wins + zord + zn are contiguous in .bss
    mov cx, MAX_WIN*WIN_SIZE + MAX_WIN + 1 + MAX_WIN*2
    rep stosb
    mov al, 0xFF                ; wm_owner is NOT part of the zero run: its
    mov di, wm_owner            ; empty value is 0xFF (unowned, SPEC.md 11)
    mov cx, MAX_WIN
    rep stosb
    mov word [wm_fs], 0         ; no fullscreen window (SPEC.md 11.2); .bss
                                ; is never zeroed at boot, so this is not
                                ; redundant
    xor ax, ax                  ; the close negotiators, the right-click
    mov di, wm_oncl             ; handlers and the two words of SPEC.md 75.2's
    mov cx, MAX_WIN*4 + 4       ; request (contiguous in .bss). UNLIKE wm_onwk
    rep stosb                   ; and wm_onsz above, these ARE read before any
                                ; wm_create writes them: wm_ask_close asks
                                ; EVERY window on its way out, handler or not,
                                ; ui_rdown reads wm_onrc for every frontmost
                                ; window a right press lands on (SPEC.md
                                ; 13.11), and wm_close_pass reads [wm_clreq]
                                ; on the machine's first UI pass
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_fit - clamp a just-created window's frame onto the live screen
;
; Every window template in the tree - built-in, dialog and package alike -
; was authored against 640x480, and a Hercules screen is 132 rows shorter
; while a CGA one is 280 rows shorter. Rather than make every caller
; adapter-aware (which would mean rebuilding every .o88), the geometry is
; clamped once, here, at the single point all four creation paths funnel
; through. On VGA it is a no-op.
;
; Size first, then position, and the y floor last so it wins: a window taller
; than the desktop is better pinned under the menu bar and hanging over the
; dock than pushed up behind the bar.
; in:       BX -> the window record, W_X..W_H already filled
; out:      W_X/W_Y/W_W/W_H clamped
; clobbers: flags
; -----------------------------------------------------------------------------
; THE BOX A FRAME MUST FIT IN is AX/CX = x0 and xe, SI/DI = y0 and ye, and it
; is a DISPLAY's rather than the desktop's (SPEC.md 39.16.1). Against a union
; the old bounds were the wrong shape twice: they would allow a window in the
; dead zone, and they would apply the PRIMARY's dock row to a display that has
; no dock. Four registers rather than four variables because that is all it
; is - and on a machine with one display wm_fit_box does not exist and the
; three loads below are the numbers this routine always used.
;
; DX IS A FIFTH: the display's own bottom, which is the floor for a window
; that will not fit the desktop band at all (SPEC.md 11.93).
wm_fit:
    push ax
    push cx
    push dx
    push si
    push di
    xor ax, ax                  ; x0
    mov cx, [vid_pw]            ; xe
    mov si, MBAR_H              ; y0
    mov di, [vid_dock_y0]       ; ye
    mov dx, [vid_ph]            ; ...and the hard floor under it
%ifdef KERN_BIG
    call wm_fit_box             ; ...unless the window is on another display
%endif
    push cx                     ; w = min(w, xe - x0)
    sub cx, ax
    cmp [bx+W_W], cx
    jle .wok
    mov [bx+W_W], cx
.wok:
    pop cx
    push di                     ; h = min(h, ye - y0) - and ONE PIXEL SHORT,
    sub di, si                  ; because the drop shadow lives on row y+h and
    dec di                      ; wm_dock_clear tests y+h with `jae`. Clamping
                                ; to exactly the band handed out a window flush
                                ; against the strip, which then made every
                                ; window opened over it pay a dock-under
                                ; repaint (SPEC.md 11.90). This is the one
                                ; place that decides it for every fixed-size
                                ; package at once
    cmp [bx+W_H], di
    jle .hfit                   ; fits the band: the dock is the floor, and
                                ; every window in the tree but one takes this
                                ; branch on every adapter
%ifndef NOKEEPH                 ; ...and `make KEEPH=0` is the A/B: no test, so
                                ; every window falls into .hcut and is
                                ; shortened, which is exactly the pre-11.93
                                ; behaviour rather than an imitation of it
    test word [bx+W_FLAGS], WF_KEEPH
    jz .hcut                    ; a window that reflows is SHORTENED, exactly
                                ; as it always was
    pop di                      ; WF_KEEPH: the dock stops being the floor and
    mov di, dx                  ; the display's own bottom becomes it (SPEC.md
    push di                     ; 11.93) - for the height clamp below AND for
    sub di, si                  ; the y clamp further down, which is why DI is
    dec di                      ; written rather than a copy of it
    cmp [bx+W_H], di
    jle .hfit
%endif
.hcut:
    mov [bx+W_H], di            ; still too tall: now it really is the screen
.hfit:
    pop di
    push cx                     ; ...AND THE FLOOR LAST, SO IT WINS (SPEC.md
    push dx                     ; 11.100.2). HERE and not below the position
    sub cx, ax                  ; clamps, so the origin is settled against the
    sub dx, si                  ; size the window really ends up with.
    call wm_min_floor           ; The height cap is the display's own bottom
    pop dx                      ; less the TOP OF THE BAND rather than less
    pop cx                      ; W_Y, and that is what makes it provable: a
                                ; frame that big can only start at y0, and the
                                ; y clamp below floors y there - so a window
                                ; whose minimum binds ends exactly ON the
                                ; display's last row, never past it
    push cx                     ; x = min(x, xe - w), floor x0
    sub cx, [bx+W_W]
    cmp [bx+W_X], cx
    jle .xok
    mov [bx+W_X], cx
.xok:
    pop cx
    cmp [bx+W_X], ax
    jge .xfl
    mov [bx+W_X], ax
.xfl:
    call wm_snap_win            ; ...and back onto a multiple of 8 if this
                                ; window asked for it (SPEC.md 11.94). After
                                ; both clamps, never before: it moves left,
                                ; which only stays legal once the right edge
                                ; has been dealt with
    push di                     ; y = min(y, ye - h - 1), floor y0 - the same
    sub di, [bx+W_H]            ; pixel, for the same reason
    dec di
    cmp [bx+W_Y], di
    jle .yok
    mov [bx+W_Y], di
.yok:
    pop di
    cmp [bx+W_Y], si
    jge .out
    mov [bx+W_Y], si
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; wm_disp_of - which display is this window primarily ON? (SPEC.md 39.17)
; in:       BX = the window
; out:      AL = the display index
; clobbers: AX, flags
;
; THE CENTRE, then the ORIGIN, then the primary. Overlap area is the intuitive
; answer and needs a 32-bit multiply - 720 x 348 is 250,560 and overflows a
; word - so "which display does more of this window touch" costs a mul pair
; per display on a machine whose whole design is about not paying for
; arithmetic. The centre is one point-in-rect test per display and lands on
; the same answer in every case anybody will construct. Three rungs because
; the virtual desktop is not a rectangle (39.2.1): a centre can fall in the
; dead zone, and so can an origin.
;
; ONE RULE, not two: both fullscreens, wm_zoom and eventually fsx all ask it,
; so none of them can drift about which monitor an app is on.
; -----------------------------------------------------------------------------
wm_disp_of:
    push bx
    push si
    mov si, bx                  ; SI, because vid_disp_find wants BX = y
    mov ax, [si+W_W]
    shr ax, 1
    add ax, [si+W_X]
    mov bx, [si+W_H]
    shr bx, 1
    add bx, [si+W_Y]
    call vid_disp_find          ; the CENTRE
    jnc .out
    mov ax, [si+W_X]            ; ...else the ORIGIN
    mov bx, [si+W_Y]
    call vid_disp_find
    jnc .out
    xor al, al                  ; ...else the primary
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; wm_disp_span - that display's standard-state box (SPEC.md 39.17.2)
; in:       BX = the window; AX/CX/SI/DX = the primary's (x0, width, y0,
;           band height), already loaded
; out:      AX = x0, CX = width, SI = y0, DX = band height (one pixel short,
;           wm_fit's drop-shadow rule)
; clobbers: AX, CX, SI, DX, flags
; -----------------------------------------------------------------------------
wm_disp_span:
    cmp byte [vid_ndisp], 1
    jbe .out                    ; one display: the caller's numbers stand
    push di
    call wm_disp_of
    call vid_ctx_ptr
    or al, al                   ; (the movs leave the flags for the jz)
    mov ax, [di+VID_CTX_VX]
    mov cx, [di+VID_CTX_CW]
    jz .prim                    ; the PRIMARY keeps the caller's band: bar to
                                ; dock, which its context record cannot know
    mov si, [di+VID_CTX_VY]     ; a secondary has no bar and no dock, so its
    mov dx, [di+VID_CTX_CH]     ; whole height is desktop - wm_fit_box's own
    dec dx                      ; rule (SPEC.md 39.16.1), one short like it
.prim:
    pop di
.out:
    ret
%endif

; -----------------------------------------------------------------------------
; wm_zoom_xmax - the largest x a window of width CX may have on ITS display
;                (SPEC.md 39.17.2)
; in:       BX = the window, CX = the width
; out:      DX = that x, floored at the display's left edge
; clobbers: DX, flags
; -----------------------------------------------------------------------------
wm_zoom_xmax:
    push ax
    push di
    xor ax, ax                  ; the primary's left edge...
    mov dx, [vid_pw]            ; ...and one past its right
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .have
    call wm_disp_of
    call vid_ctx_ptr
    mov ax, [di+VID_CTX_VX]
    mov dx, ax
    add dx, [di+VID_CTX_CW]
.have:
%endif
    sub dx, cx
    cmp dx, ax                  ; ...floored at THAT edge, not at 0
    jge .out
    mov dx, ax
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_fs_setrect - a fullscreen window's rect: the display it is ON
;                 (SPEC.md 39.17.1)
; in:       BX = the window
; out:      W_X/W_Y/W_W/W_H filled
; clobbers: flags
; -----------------------------------------------------------------------------
wm_fs_setrect:
    push ax
%ifdef KERN_BIG
    push di
    cmp byte [vid_ndisp], 1
    jbe .one
    call wm_disp_of
    call vid_ctx_ptr
    mov ax, [di+VID_CTX_VX]
    mov [bx+W_X], ax
    mov ax, [di+VID_CTX_VY]
    mov [bx+W_Y], ax
    mov ax, [di+VID_CTX_CW]
    mov [bx+W_W], ax
    mov ax, [di+VID_CTX_CH]
    mov [bx+W_H], ax
    jmp short .out
.one:
%endif
    xor ax, ax
    mov [bx+W_X], ax
    mov [bx+W_Y], ax
    mov ax, [vid_pw]
    mov [bx+W_W], ax
    mov ax, [vid_ph]
    mov [bx+W_H], ax
%ifdef KERN_BIG
.out:
    pop di
%endif
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; wm_land_fit - the window has been PUT DOWN: has the adapter under it changed,
;               and if so what size does it want here? (SPEC.md 39.16.3.3)
; in:       BX = the window, its rect already where the user left it
; out:      nothing; every register preserved, FLAGS INCLUDED
;
; **THE RULE IS ABOUT ADAPTER IDENTITY AND NOT ABOUT GEOMETRY**, which is the
; whole of docs/WINDOW-SIZING-PLAN.md 12 and the correction of everything
; above it. What a window wants is not "a size that fits the box the drag left
; me in" but "my size for adapter kind Y, and I am standing on adapter kind
; Y" - so the TRIGGER is a change of kind and nothing else. A move within one
; adapter changes no size at all, however far it goes and however much of the
; frame ends up past the bottom of the screen.
;
; Three separate mechanisms used to share this job and each produced its own
; defect. ui_drag restored the NATURAL BANK on every release, so a drag from
; one place to another inside the CGA handed the window its HERCULES size; the
; straddle cut geometrically to the rows both displays have, which is an
; arbitrary number rather than a designed one and turned Missile Command into
; a letterbox; and a landing wholly on the short display was clamped by
; nothing at all, and then (11's withdrawn 3.6) by a rule derived from the
; union's BOUNDING BOX, which reads rows no display has as a hole in the
; desktop.
;
; wm_kind_now answers the question - including the straddle, where it is the
; MORE RESTRICTIVE of the two displays, which is what makes a Hercules-to-CGA
; round trip exactly two resizes instead of four: one entering the straddle
; and one leaving it on the far side.
;
; THE SNAP AND THE HANG ARE THE SAME MECHANISM SEEN FROM TWO SIDES (12.4). A
; window whose floor beats the room has its origin moved up until the frame
; ends on the last row the display has - because a user cannot SEE a window's
; floor while dragging it, so a placement that floors out crossing a seam was
; never an informed one. A move within one adapter never reaches here, so it
; hangs, and that is informed.
;
; AND A KERNEL-INITIATED RESIZE ALWAYS ASKS (SPEC.md 11.1). wm_ask_size is put
; the size this arrives at and answers with the one the window will accept -
; per axis, so a shrink that would lose columns and not rows still gets its
; rows - and a REFUSAL means the window keeps its size and HANGS rather than
; being cropped. apps/paint is the consumer and it answers out of its own
; canvas state rather than the record, which is why the ask can follow the
; write instead of having to precede it.
; -----------------------------------------------------------------------------
wm_land_fit:
    pushf
    push ax
    push dx
    cmp byte [vid_ndisp], 1
    jbe .out                    ; one display: a drag can never change a kind
    push ax                     ; **THE DISPLAY IS DECIDED ONCE, HERE** (SPEC.md
    call wm_disp_now            ; 39.16.4.2). Armed before the first question is
    mov [wm_lf_disp], al        ; asked and spent at .fin, so the resize this
    mov [wm_lf_win], bx         ; routine applies cannot change the answer under
    pop ax                      ; the steps that follow it
    call wm_kind_now            ; **NEITHER OF THESE MAY BE BANKED IN AH.**
    mov dl, al                  ; wm_kind_was goes through wm_ptr2idx, which
    call wm_kind_was            ; answers AL = index and AH = 0, so `mov ah,al`
    cmp al, dl                  ; before it compared the banked kind against
    je .fin                     ; VID_VGA - and `push ax`/`pop ax` around it
                                ; restores AH along with AL, which is the same
                                ; bug wearing the fix's clothes. Both shipped;
                                ; the first is why a drag from one place to
                                ; another INSIDE one adapter still re-derived a
                                ; size, and the second is why a drag STRAIGHT
                                ; from one card to the other did nothing at all
                                ; (measured: AX = 0x0202 against a banked
                                ; 0x0001). DL survives both calls
    call wm_strads
    jc .land
    call wm_strad_fit           ; ...across the seam: the constrained axis
    jmp short .ask              ; ONLY, or wm_fit's x clamp would yank the frame
.land:                          ; wholly onto one display and undo the drag
    call wm_apply_size_wh
.ask:
    push cx
    push dx
    mov cx, [bx+W_W]
    mov dx, [bx+W_H]
    call wm_ask_size            ; the window's own veto (SPEC.md 11.1)
    mov [bx+W_W], cx
    mov [bx+W_H], dx
    pop dx
    pop cx
    call wm_land_snap           ; ...and the origin follows whatever it ended
    call wm_kind_bank           ; up with
    call wm_sz_notify           ; TOLD EVEN IF THE BOX DID NOT MOVE: the ADAPTER
                                ; did, and half the tree caches facts about it
                                ; (SPEC.md 11.98). wm_refit has always done this
                                ; and the drag path never did. IT IS INSIDE THE
                                ; LATCH: the handler asks OSAPI_WM_DISPLAY and
                                ; has to be told the card the kernel just sized
                                ; it for, not the one its new frame sits on
.fin:
    mov word [wm_lf_win], 0     ; ...and 0 is no window, wm_wins starting well
.out:                           ; above it
    pop dx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_apply_size_wh - the size half of wm_apply_size, for a window the USER has
;                    just placed (SPEC.md 39.16.3.3)
; in:       BX = the window
; out:      nothing; every register preserved, FLAGS INCLUDED
;
; wm_apply_size restores the natural rect WHOLE, which is right when the screen
; changed under a window that nobody has touched and wrong the moment a hand is
; on it: the origin is the user's and only the size is in question. The third
; row of 12.5's table is here rather than in wm_fit - a window that is not
; sizable and has published nothing for this adapter is left ALONE, because the
; kernel cannot see a layout and shortening a fixed one buys nothing and costs
; a rect nothing will repaint (39.16.3.1).
; -----------------------------------------------------------------------------
wm_apply_size_wh:
    pushf
    push ax
    call wm_nat_take_wh         ; the size it ASKED for, not what a clamp left
    call wm_pref_now            ; ...or the one it published for this adapter
    jnc .fit
    call wm_reflows
    jnc .done
.fit:
    call wm_fit
.done:
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_land_snap - the origin follows the size, so a frame that will not fit from
;                where it was dropped ends ON the display's last row
; in:       BX = the window
; out:      nothing; every register preserved, FLAGS INCLUDED
;
; The floor outranks the box (11.100.2), so a window whose minimum is taller
; than the room left below the pointer cannot honour both - and this is the
; half that keeps it whole: y = min(y, limit - h), floored at the top of the
; band. A frame too tall for the band at all lands at the top and hangs down
; from there, which is the best available answer for a window that REFUSED to
; shrink (apps/paint with a picture in it): nothing is cropped and as much of
; it as the display has is on screen.
;
; The limit is the nearer of the two far edges while the frame straddles, and
; the landing display's own otherwise. wm_fit already did this once with the
; pre-ask size; it is done again because the ask may have given the height
; back.
; -----------------------------------------------------------------------------
wm_land_snap:
    pushf
    push ax
    push cx
    push dx
    push si
    push di
    push bp
    xor ax, ax                  ; the NEAR edges of the display it is on -
    mov cx, [vid_pw]            ; MBAR_H on the primary and the card's own vy
    mov si, MBAR_H              ; anywhere else, since nothing but the primary
    mov di, [vid_dock_y0]       ; has a menu bar (39.16.2). ui_ylow answers the
    mov dx, [vid_ph]            ; same question and may not be used: it reads
    call wm_fit_box             ; the DRAG's proposed x, which is meaningless
    mov bp, si                  ; on the ui_grow path
    push ax                     ; ...both banked, DX being about to be the axis
    call wm_sf_axis             ; DL = the constrained axis
    call wm_sf_lim              ; SI = one past the last row (column) available
    pop ax
    or dl, dl
    jz .x
    mov cx, si
    sub cx, [bx+W_H]
    cmp [bx+W_Y], cx
    jle .yfl
    mov [bx+W_Y], cx            ; may go NEGATIVE for a frame taller than the
.yfl:                           ; band, and the floor below takes it straight
    cmp [bx+W_Y], bp            ; back - which is the REFUSAL case landing at
    jge .done                   ; the top and hanging down from there
    mov [bx+W_Y], bp
    jmp short .done
.x:
    mov cx, si
    sub cx, [bx+W_W]
    cmp [bx+W_X], cx
    jle .xfl
    mov [bx+W_X], cx
.xfl:
    cmp [bx+W_X], ax
    jge .done
    mov [bx+W_X], ax
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_sf_axis - DL = 1 the displays tile SIDE BY SIDE (y is what has to come
;              in), 0 STACKED (x does)
; out:      DL; every other register preserved
; clobbers: DL, flags
;
; Side by side (39.19.2's Right) they are contiguous in x and overlap in y - so
; spanning x is the whole point of extending and must stay free, while y is
; what has to come in. Stacked (Below) it is the other way round. The test is
; geometric rather than a read of [vid_dlay]: display 1's VY against display
; 0's height, display 0 being at the virtual origin by construction.
; -----------------------------------------------------------------------------
wm_sf_axis:
    push ax
    mov ax, [vid_ctx + VID_CTX_SZ + VID_CTX_VY]
    cmp ax, [vid_ctx + VID_CTX_CH]
    mov dl, 1                   ; the MOV writes no flags, so the cmp decides
    jb .out
    xor dl, dl
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_sf_lim - SI = one past the last row (column) BOTH displays have
; in:       DL = the axis, from wm_sf_axis; BX = the window
; out:      SI; every other register preserved
; clobbers: SI, flags
;
; The nearer of the two far edges while the frame straddles, and the display
; the window is ON otherwise - so this one routine bounds a straddle and a
; landing alike.
; -----------------------------------------------------------------------------
wm_sf_lim:
    push ax
    push di
    call wm_strads
    jc .own
    xor al, al
    call vid_ctx_ptr
    call wm_sf_far
    mov si, ax
    mov al, 1
    call vid_ctx_ptr
    call wm_sf_far
    cmp ax, si
    jae .out
    mov si, ax
    jmp short .out
.own:
    push bx
    mov ax, [bx+W_X]
    mov bx, [bx+W_Y]
    call vid_disp_of
    pop bx
    call vid_ctx_ptr
    call wm_sf_far
    mov si, ax
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_strads - does this window's frame reach BOTH displays?
; in:       BX = the window
; out:      CF = 0 it straddles, CF = 1 it does not (a one-display machine
;           included); every register preserved
; clobbers: flags
; -----------------------------------------------------------------------------
wm_strads:
    push ax
    push di
    cmp byte [vid_ndisp], 1
    jbe .no
    xor al, al
    call vid_ctx_ptr
    call wm_sf_reach
    jc .no
    mov al, 1
    call vid_ctx_ptr
    call wm_sf_reach
    jc .no
    pop di
    pop ax
    clc
    ret
.no:
    pop di
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wm_strad_fit - a window across the seam takes the size for the MORE
;                RESTRICTIVE of the two displays (SPEC.md 39.16.3)
; in:       BX = the window, known to straddle and known to have changed kind
; out:      nothing; every register preserved, FLAGS INCLUDED
;
; WHAT IS WRONG WITH A STRADDLING WINDOW is not the straddle - SPEC.md 39.14's
; split draws one fragment per display and that works. It is that the virtual
; desktop is a UNION and not a rectangle (39.2.1), so beyond the seam the two
; displays stop agreeing about which rows exist: a 720x348 Hercules beside a
; 640x200 CGA leaves rows 220..347 present on the left and absent on the
; right. A window tall enough to use them, dragged until its right-hand edge
; is over the CGA, has its bottom-right corner in the DEAD ZONE - drawn
; nowhere, clickable nowhere, and containing whatever the app put there.
;
; **THE SIZE COMES FROM THE ADAPTER AND THE CLAMP MERELY BOUNDS IT.** This
; routine used to do only the second half, which is why it produced a number
; nobody had designed: the size it arrived at was whatever the geometry left,
; so a game whose playfield IS its rules came out a letterbox. Now the natural
; size or the published preference is taken FIRST, and the clamp is what stops
; the answer reaching into the dead zone.
;
; IT NEVER MOVES THE ORIGIN - wm_land_snap owns that, after the window has had
; its say about the size. "The more restrictive size" is a statement about
; size.
;
; AND IT ONLY SHORTENS A WINDOW THAT WILL LAY ITSELF OUT AGAIN (39.16.3.1).
; The frame is not the pixels: the gfx primitives clip to the SCREEN and not
; to a window (SPEC.md 11.3), so a fixed layout drawn against remembered
; offsets puts exactly the same pixels on the glass whatever this writes into
; W_H - the rows below the shortened frame are still drawn, they are simply no
; longer inside anything the window manager thinks the window owns. So the
; shrink buys nothing at all there and costs a rect nothing will ever repaint.
; wm_reflows is the question, and it is a NARROWER question than it was: an
; 11.98 handler means "tell me when my box moved", not "you may pick any box
; for me" (12.5), so a non-sizable window opts in by PUBLISHING A PREFERENCE
; instead - a promise about three boxes rather than about every box.
; -----------------------------------------------------------------------------
wm_strad_fit:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call wm_nat_take_wh         ; the size it ASKED for...
    call wm_pref_now            ; ...or the one it published for this adapter
    jnc .clamp
%ifndef STRAD_ALL               ; ...and `make STRAD=all` is the A/B: no test,
    call wm_reflows             ; so every straddling window is shortened,
    jnc .done                   ; which is exactly what 39.16.3.1 reports
%endif                          ; rather than an imitation of it
.clamp:
    call wm_sf_axis             ; DL = the axis that has to come in
    call wm_sf_lim              ; SI = one past the last row (column) both
                                ; displays have
    or dl, dl
    jz .limx

    ; --- y: the rows available from where the window stands -----------------
    mov cx, si
    sub cx, [bx+W_Y]
    jg .yroom
    xor cx, cx
.yroom:
    cmp [bx+W_H], cx
    jbe .yfl
    mov [bx+W_H], cx
.yfl:
    mov ax, si                  ; ...and the floor cannot be cut through. The
    sub ax, MBAR_H              ; cap is the limit less the TOP OF THE BAND
    call wm_min_ax              ; rather than less W_Y, which is what makes it
    cmp [bx+W_H], ax            ; provable: a frame that big can only start at
    jae .done                   ; the top, and wm_land_snap floors it there
    mov [bx+W_H], ax
    jmp short .done

    ; --- x: the same, for displays that tile vertically ---------------------
.limx:
    mov cx, si
    sub cx, [bx+W_X]
    jg .xroom
    xor cx, cx
.xroom:
    cmp [bx+W_W], cx
    jbe .xfl
    mov [bx+W_W], cx
.xfl:
    mov ax, si
    call wm_min_ax
    cmp [bx+W_W], ax
    jae .done
    mov [bx+W_W], ax
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_sf_far - DI = a display record: one past its last row, or column
; in:       DI = the record, DL != 0 = the y axis / 0 = the x axis
; out:      AX = the far edge, in VIRTUAL coordinates
; clobbers: AX, flags
; -----------------------------------------------------------------------------
wm_sf_far:
    or dl, dl
    jz .x
    mov ax, [di+VID_CTX_VY]
    add ax, [di+VID_CTX_CH]
    ret
.x:
    mov ax, [di+VID_CTX_VX]
    add ax, [di+VID_CTX_CW]
    ret

; -----------------------------------------------------------------------------
; wm_sf_reach - does window BX's frame overlap the display DI describes?
; in:       BX = the window, DI = the display record
; out:      CF = 0 it overlaps, CF = 1 it does not
; clobbers: AX, flags - CX and DX are LIVE in the caller
; -----------------------------------------------------------------------------
wm_sf_reach:
    push cx
    mov ax, [di+VID_CTX_VX]
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    cmp cx, ax
    jbe .no                     ; wholly left of it
    add ax, [di+VID_CTX_CW]
    cmp [bx+W_X], ax
    jae .no                     ; wholly right of it
    mov ax, [di+VID_CTX_VY]
    mov cx, [bx+W_Y]
    add cx, [bx+W_H]
    cmp cx, ax
    jbe .no                     ; wholly above it
    add ax, [di+VID_CTX_CH]
    cmp [bx+W_Y], ax
    jae .no                     ; wholly below it
    pop cx
    clc
    ret
.no:
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; wm_reflows - will this window DRAW ITSELF INTO a box the kernel shortens?
;              (SPEC.md 39.16.3.1)
; in:       BX = the window
; out:      CF = 1 yes, it re-derives its layout from the live content box;
;           CF = 0 no, so its height is not to be reduced. Every register
;           preserved.
;
; THE KERNEL CANNOT SEE A LAYOUT, so it can only ask what the window has
; already said, and two things say it. WF_SIZABLE is a grow box, which moves
; the content box on the user's whim - an app that takes one has no choice but
; to lay out from the live one, and every one in this tree does (apps/telnet
; says so in as many words). SPEC.md 11.98's size-CHANGED handler is the other
; and covers the app that is NOT resizable and still follows: apps/calc is not
; sizable at all and re-folds its history pane on the notice, which is exactly
; the case this rule must not throw away.
;
; WF_KEEPH OUTRANKS BOTH, because it is the window saying the opposite in the
; first person (SPEC.md 11.93) - and apps/browser is both sizable and KEEPH,
; so the order is not academic.
;
; It is a question about the WINDOW and not about the geometry, so it is asked
; before any of wm_strad_fit's arithmetic: the cheapest way to not shorten a
; window is to not work out by how much.
; -----------------------------------------------------------------------------
wm_reflows:
    test word [bx+W_FLAGS], WF_KEEPH
    jnz .no                     ; "not the kernel's to reduce" - and it is the
                                ; FIRST test for browser's sake, which is
                                ; sizable AND keeps its height
    test word [bx+W_FLAGS], WF_SIZABLE
    jz .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wm_fit_box - the box for the display this window's ORIGIN is on
;              (SPEC.md 39.16.1)
; in:       BX = the window; AX/CX/SI/DI/DX = the primary's box and hard floor
; out:      the same five, for that display
; clobbers: AX, CX, DX, SI, DI, flags
;
; The ORIGIN and not the centre, deliberately: wm_disp_of's centre test is for
; deciding which monitor an app OWNS (docs/DUAL-DISPLAY-PLAN.md 6.5.1), and
; this is deciding which edges a frame may not pass - which is a question
; about the corner the clamps are written from. A window whose origin is in
; the dead zone gets the primary, the only answer that cannot leave it
; unreachable.
;
; DX IS AN OUTPUT TOO (SPEC.md 11.93), and it is the reason the display index
; is banked on the stack rather than kept in DL: this used to preserve DX, and
; the caller's fifth number is the bottom a window that will not fit the band
; may hang down to. On a secondary that IS ye, there being no dock to hang
; over; on the primary it is [vid_ph], the number already loaded.
; -----------------------------------------------------------------------------
wm_fit_box:
    cmp byte [vid_ndisp], 1
    jbe .out
    push bx
    mov ax, [bx+W_X]
    mov bx, [bx+W_Y]
    call vid_disp_of            ; AL = the display holding the origin
    push ax                     ; ...banked: DX is an output now
    mov dl, al
    call vid_ctx_ptr            ; DI = its record
    mov ax, [di+VID_CTX_VX]
    mov cx, ax
    add cx, [di+VID_CTX_CW]
    mov si, [di+VID_CTX_VY]
    add si, [di+VID_CTX_CH]     ; ...ye first, while DI is still the record
    push si
    mov si, [di+VID_CTX_VY]
    pop di                      ; DI = ye, SI = y0
    mov dx, di                  ; a secondary's hard floor is its own bottom
    pop ax
    or al, al
    jnz .done                   ; a secondary has no menu bar and no dock, so
    mov si, MBAR_H              ; its whole height is desktop
    mov di, [vid_dock_y0]
    mov dx, [vid_ph]            ; ...and the primary's floor is its own height
.done:
    pop bx
.out:
    ret
%endif

; -----------------------------------------------------------------------------
; wm_dmg_bands - seed the clip list with the DESKTOP BAND of every display the
;                damage rect touches (SPEC.md 39.16.2.1)
;
; in:       AX = x1, BX = y1, CX = x2, DX = y2 - the damage, VIRTUAL, already
;           clamped to the desktop
; out:      CF = 0: [wm_clip_n] seeds are armed and AX/BX/CX/DX is their
;           bounding box. CF = 1: this rect holds no desktop row on any
;           display, and nothing is armed
; clobbers: flags, SI, DI and the four outputs
;
; MBAR_H .. [vid_dock_y0]-1 was the answer while there was one display, and it
; is still the answer ON display 0 - the menu bar and the dock are the
; PRIMARY's and no other card carries either. On any other display the whole
; height is desktop.
;
; **IT TAKES A RECT BECAUSE A POINT CANNOT ANSWER** (SPEC.md 39.16.2.1). This
; was `wm_dmg_band`, which took one (x, y) and answered one band, and
; wm_paint_dmg asked it about the damage rect's top-left corner and applied the
; answer to the whole rect. A drag across the seam is exactly the case where
; that rect spans two displays with different bands: dragging a Disk window
; back onto a CGA primary from a Hercules secondary put x1 on the CGA, so the
; CGA's dock ceiling of 175 was applied to the Hercules part as well and every
; vacated row below it there was never dithered - 3,844 stale pixels, which is
; 320 columns x 24 rows half-lit in a 50% dither to within four
; (docs/WM-ARTIFACTS.md 2). A one-display machine could not show it and never
; will, so the shape of the contract is the only thing that stops it coming
; back: there is no point-shaped entry point here any more to reach for.
;
; The SPLIT already existed and was never the problem - gfx_disp_run intersects
; a rect with each display and translates it, and a fragment on no display is
; drawn by nobody (SPEC.md 39.2.1). So this emits the bands as CLIP SEEDS and
; leaves the caller one fill: the region confines it and gfx_disp_run splits
; it, which is one region build and one primitive call rather than one of each
; per display - PERFORMANCE.md Part 2's ~756us per call is the thing not being
; spent twice.
; -----------------------------------------------------------------------------
wm_dmg_bands:
    mov [wm_db_r+0], ax
    mov [wm_db_r+2], bx
    mov [wm_db_r+4], cx
    mov [wm_db_r+6], dx
    mov word [wm_clip_n], 0
    mov word [wm_db_b+0], 0x7FFF        ; the bounding box, empty
    mov word [wm_db_b+2], 0x7FFF
    mov word [wm_db_b+4], 0x8000
    mov word [wm_db_b+6], 0x8000
    xor si, si                          ; SI = the display being asked
.disp:
%ifdef KERN_BIG
    mov al, [vid_ndisp]
    xor ah, ah
    cmp si, ax
    jae .done
    mov ax, si
    call vid_ctx_ptr                    ; DI = its record (AX preserved)
    mov ax, [di+VID_CTX_VX]
    mov cx, ax
    add cx, [di+VID_CTX_CW]
    dec cx
    mov bx, [di+VID_CTX_VY]
    mov dx, bx
    add dx, [di+VID_CTX_CH]
    dec dx
%else
    or si, si
    jnz .done
    xor ax, ax                          ; one display, and it IS the screen
    xor bx, bx
    mov cx, [vid_wm1]
    mov dx, [vid_hm1]
%endif
    cmp ax, [wm_db_r+0]                 ; ...intersected with the damage
    jge .x1
    mov ax, [wm_db_r+0]
.x1:
    cmp cx, [wm_db_r+4]
    jle .x2
    mov cx, [wm_db_r+4]
.x2:
    cmp bx, [wm_db_r+2]
    jge .y1
    mov bx, [wm_db_r+2]
.y1:
    cmp dx, [wm_db_r+6]
    jle .y2
    mov dx, [wm_db_r+6]
.y2:
    or si, si                           ; ...and the CHROME is display 0's
    jnz .band                           ; alone (SPEC.md 39.16.2)
    cmp bx, MBAR_H
    jge .top
    mov bx, MBAR_H
.top:
    push ax
    mov ax, [vid_dock_y0]               ; stopping above the strip, which
    dec ax                              ; dock_force_x restores: damage that
    cmp dx, ax                          ; reaches it already forces the span,
    jle .bot                            ; so dithering those rows is drawn only
    mov dx, ax                          ; to be painted over (SPEC.md 30.3.2)
.bot:
    pop ax
.band:
    cmp ax, cx                          ; nothing of this display is desktop
    jg .next                            ; here - and an empty seed must not
    cmp bx, dx                          ; grow the bounding box either
    jg .next
    mov [wm_cl_fx1], ax
    mov [wm_cl_fy1], bx
    mov [wm_cl_fx2], cx
    mov [wm_cl_fy2], dx
    call wm_clip_add                    ; WM_CLIP_MAX is 16 and this appends at
                                        ; most VID_NDISP_MAX, so the full case
                                        ; is unreachable rather than handled
    cmp ax, [wm_db_b+0]
    jge .b1
    mov [wm_db_b+0], ax
.b1:
    cmp bx, [wm_db_b+2]
    jge .b2
    mov [wm_db_b+2], bx
.b2:
    cmp cx, [wm_db_b+4]
    jle .b3
    mov [wm_db_b+4], cx
.b3:
    cmp dx, [wm_db_b+6]
    jle .b4
    mov [wm_db_b+6], dx
.b4:
.next:
    inc si
    jmp .disp                   ; NEAR: the body outgrew a short jump
.done:
    cmp word [wm_clip_n], 0
    je .none
    mov ax, [wm_db_b+0]
    mov bx, [wm_db_b+2]
    mov cx, [wm_db_b+4]
    mov dx, [wm_db_b+6]
    clc
    ret
.none:
    stc
    ret

wm_db_r     times 4 dw 0        ; wm_dmg_bands: the damage it was handed, and
wm_db_b     times 4 dw 0        ; the bounding box of the seeds it emitted -
                                ; .text for wm_clip_n's reason, and read back
                                ; by wm_dmg_gray, which is its only caller

; -----------------------------------------------------------------------------
; The natural bank: the frame a window goes back to when the screen grows again
; (SPEC.md 39.11.2.1).
;
; wm_fit is a CLAMP, and a clamp throws the number away. That cost nothing
; while the screen was decided once at boot, and it stopped costing nothing at
; vid_switch: a 640x480 window fitted onto a 640x200 CGA loses 280 rows, and
; the record is the only place the old height lived - so VGA -> CGA -> VGA left
; the Task Manager 155 rows tall on a 480-row screen, and its own layout still
; drawing the 284 rows' worth it had computed at launch, through the bottom of
; its frame and onto the desktop.
;
; So the rect a window ASKED FOR is banked beside it, and wm_refit replays that
; instead of re-clamping whatever the last clamp left behind. One entry per
; window SLOT, a side table rather than four more words in the record, for
; wm_zoomr's reason one screen up: WIN_SIZE is what wm_idx2ptr multiplies by,
; and every reader of a window would pay for a wider record whether the machine
; it runs on has a second adapter or not.
;
; NR_W = 0 is "nothing banked" - wm_zoomr's sentinel, safe for wm_zoomr's
; reason (a banked width is a live window's, which WMIN_W floors at 96) - and
; wm_destroy clears it, or a reused slot restores a stranger's rect.
;
; IT IS A BACKSTOP AND NOT THE ARGUMENT, deliberately, because the tree
; disagrees with itself about whether .bss arrives zeroed (kernel.asm's
; overlay note says it does, vid_disp_relayout's says -f bin zeroes nothing)
; and this must not be the thing that settles it. What actually holds is that
; wm_refit only visits records whose USED bit is set, wm_init clears that bit
; for every slot at boot, and the one routine that sets it is wm_create -
; which banks. So a bank is always present by the time anything can take one,
; whatever memory the machine came up holding.
;
; WHAT MAY BANK, and this is the whole list, because a writer that forgets
; leaves a window restoring to a rect it stopped having some time ago:
;
;   wm_create      the TEMPLATE, before wm_fit can see it. The one place the
;                  unclamped rect exists at all, and what lets a window created
;                  while the machine is on the CGA still reach its full size
;                  when the machine goes back to the VGA
;   wm_resize_nb   an app sizing itself (SPEC.md 11.1) - and wm_zoom's .go with
;                  it, that path ending here
;   wm_zoom        ...and its RESTORE path, which writes the record directly
;   ui_drag        the user put it there
;   ui_grow        ...and sized it there
;
; WHAT MAY NOT, which is the half worth stating: wm_fit and wm_snap_win, the
; clamp itself - banking there would make the clamp its own source and the bank
; would say nothing; wm_dock_snap, which runs inside the two ui_ paths and is
; covered by their bank after it; and BOTH fullscreen paths, because
; wm_fs_setrect takes the whole screen and wm_fs_drop gives back what
; wm_fs_save holds, and neither is a statement about the size the window wants
; as a window.
; -----------------------------------------------------------------------------
NR_X    equ 0
NR_Y    equ 2
NR_W    equ 4                   ; ...and the "nothing banked" sentinel
NR_H    equ 6
NR_SIZE equ 8

; -----------------------------------------------------------------------------
; wm_nat_slot - SI -> this window's entry in the natural bank (module internal)
; in:  BX = window ptr
; out: SI = wm_natr + index * NR_SIZE; every other register preserved
; clobbers: flags
; -----------------------------------------------------------------------------
wm_nat_slot:
    push ax
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1                   ; x8: NR_SIZE, and an 8086 has no shl by imm
    add si, wm_natr
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_nat_bank - this window's rect is the one it wants (module internal)
; in:  BX = window ptr
; out: nothing - every register preserved, FLAGS INCLUDED
;
; The flags matter: two of the five callers sit between a compare and the jump
; that reads it (ui_drag's "did it move", wm_create's CF), and a bank is
; bookkeeping that no caller should have to make room for.
; -----------------------------------------------------------------------------
wm_nat_bank:
    pushf
    push ax
    push si
    call wm_nat_slot
    mov ax, [bx+W_X]
    mov [si+NR_X], ax
    mov ax, [bx+W_Y]
    mov [si+NR_Y], ax
    mov ax, [bx+W_W]
    mov [si+NR_W], ax
    mov ax, [bx+W_H]
    mov [si+NR_H], ax
    pop si
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_nat_take - put the banked rect back into the record (module internal)
; in:  BX = window ptr
; out: nothing (all registers preserved); an empty slot leaves the record alone
; clobbers: flags
; -----------------------------------------------------------------------------
wm_nat_take:
    push ax
    push si
    call wm_nat_slot
    mov ax, [si+NR_W]
    or ax, ax
    jz .out                     ; nothing banked: whatever is in the record is
    mov [bx+W_W], ax            ; the best answer there is
    mov ax, [si+NR_X]
    mov [bx+W_X], ax
    mov ax, [si+NR_Y]
    mov [bx+W_Y], ax
    mov ax, [si+NR_H]
    mov [bx+W_H], ax
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_nat_bank_xy - bank this window's POSITION and leave its size alone
; in:  BX = window ptr
; out: nothing - every register preserved, FLAGS INCLUDED
;
; SPEC.md 11.100.3. A drag moves a window and does not resize it, so what it has
; to say about the bank is where the window now is. The SIZE in there is what
; the window last ASKED for, and the two clamps a release runs - wm_strad_fit
; and 39.16.3's hole rule - are clamps: banking their output would make the
; clamp its own source, which is the one thing the bank exists to stop.
; -----------------------------------------------------------------------------
%ifdef KERN_BIG
wm_nat_bank_xy:
    pushf
    push ax
    push si
    call wm_nat_slot
    mov ax, [bx+W_X]
    mov [si+NR_X], ax
    mov ax, [bx+W_Y]
    mov [si+NR_Y], ax
    pop si
    pop ax
    popf
    ret

%endif

; -----------------------------------------------------------------------------
; wm_nat_take_wh - put the banked SIZE back, leaving the position where it is
; in:  BX = window ptr
; out: nothing (all registers preserved); an empty slot leaves the record alone
; clobbers: flags
;
; IN BOTH BUILDS, unlike wm_nat_bank_xy above: wm_apply_wh is what the three
; public sizing cells go through and they exist on kern_small too.
; -----------------------------------------------------------------------------
wm_nat_take_wh:
    push ax
    push si
    call wm_nat_slot
    mov ax, [si+NR_W]
    or ax, ax
    jz .out                     ; nothing banked: what is in the record is the
    mov [bx+W_W], ax            ; best answer there is
    mov ax, [si+NR_H]
    mov [bx+W_H], ax
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_refit - put EVERY live window back on the screen (SPEC.md 39.11.2)
;
; in:       nothing (the live geometry has just changed)
; out:      every used record's W_X..W_H replayed from its bank and re-clamped
; clobbers: flags
;
; wm_fit runs once per window at creation because the screen never used to
; change afterwards. It can now (vid_switch), and a 640x480 machine switched
; to CGA has every window hanging off the bottom of a 200-row screen - with
; its title bar below the dock, which is a window the user cannot reach to
; drag back. One pass over wm_wins puts them all inside the new one.
;
; IT REPLAYS THE BANK RATHER THAN RE-CLAMPING THE RECORD, and that is the whole
; of what makes a switch reversible: clamping what the last clamp produced is
; idempotent in the shrinking direction and has nothing at all to say in the
; growing one (see the bank above).
;
; A free slot is skipped rather than clamped: W_W/W_H are whatever the last
; owner left there, and wm_create fits the record it fills in anyway.
; -----------------------------------------------------------------------------
wm_refit:
    push bx
    push cx
    mov bx, wm_wins
    mov cx, MAX_WIN
.next:
    test word [bx+W_FLAGS], 1   ; used?
    jz .skip
    test word [bx+W_FLAGS], WF_FULL
    jnz .full
    call wm_apply_size          ; what it ASKED for, then its own answer for
                                ; the adapter it now finds itself on, then the
                                ; clamp - SPEC.md 11.100's one sequence, the
                                ; same three wm_prefer and wm_keeph run. This
                                ; is 11.100.4's single-display half: switching a
                                ; VGA machine to its CGA row gives every window
                                ; its CGA size rather than the VGA one cut to
                                ; the band (39.11.2.1)
    jmp short .told
.full:
    call wm_fs_setrect          ; a fullscreen window wants the NEW screen, and
                                ; its bank is the WINDOWED rect - replayed here
                                ; it would un-fullscreen a window that is still
                                ; flagged WF_FULL and still owns [wm_fs], with
                                ; nothing on screen saying so (SPEC.md 11.2).
                                ; wm_fit is wrong for it too: it clamps to the
                                ; desktop band, and a fullscreen frame covers
                                ; the bar and the dock by definition
.told:
    call wm_sz_notify           ; ...and NOW tell it, unconditionally and while
                                ; the caller's repaint is still ahead of us. An
                                ; app that relayouts here is painted correctly
                                ; the first time; and it is told even when its
                                ; box did not move, because the ADAPTER may have
                                ; and half the tree caches facts about that
                                ; (SPEC.md 11.98)
.skip:
    add bx, WIN_SIZE
    loop .next
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wm_dock_snap - a window left barely over the dock is nudged back above it
; in:  BX = window ptr (its rect already clamped by the caller)
; out: CF = 1 if W_Y moved, CF = 0 if nothing changed. All registers preserved.
;
; A window's drop shadow is on row y+h, so y+h >= [vid_dock_y0] means it is
; covering the strip - and the strip is drawn UNDER windows, so every later
; dock repaint has to put that window back over it (wm_dock_under, SPEC.md
; 11.90). Landing there by a few pixels is almost always a miss rather than
; an intention, and it is the expensive kind of miss.
;
; TWO gates, and both matter:
;
; - **Less than half the strip.** DOCK_H/2 rows of overlap is the line
;   between "the drag stopped a little low" and "the user put it there".
;   Past that it stays where it was put; the dock goes under it and
;   wm_dock_under pays, which is what that routine is for.
; - **It has to FIT.** Only a window shorter than the desktop band can be
;   moved off the strip at all, and a window that cannot is left exactly
;   alone. That is the case that must not be broken: Paint grown to nearly
;   the whole screen is a deliberate, legal size, and snapping - or worse,
;   refusing - would make the grow box lie about what it does.
;
; MBAR_H is the floor, so this can only ever move a window UP into space it
; already fits in.
; -----------------------------------------------------------------------------
wm_dock_snap:
    push ax
    push cx
    push dx
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]            ; AX = the drop shadow's own row
    mov cx, [vid_dock_y0]
    cmp ax, cx
    jb .no                      ; clear of the strip already
    sub ax, cx
    inc ax                      ; AX = rows of the strip it covers
    cmp ax, DOCK_H / 2
    jae .no                     ; deliberate: leave it, and let the dock go
                                ; under it
    mov dx, cx                  ; the highest y that keeps y+h off the strip
    dec dx
    sub dx, [bx+W_H]
    cmp dx, MBAR_H
    jl .no                      ; it does not fit above the dock at all - a
                                ; grown Paint, and its size is not ours to
                                ; second-guess
    cmp [bx+W_Y], dx
    jle .no                     ; already at or above it
    mov [bx+W_Y], dx
    pop dx
    pop cx
    pop ax
    stc
    ret
.no:
    pop dx
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; wm_create - allocate a window from a template
; in:  SI -> 16-byte template {x, y, w, h, title, paint, onkey, onclick}
;      (8 words)
; out: BX = window ptr, CF clear; CF set (BX = 0) if the table is full
;
; The window is created hidden (used bit only); its index is appended to
; wm_zord - frontmost position - and wm_zn is incremented. Does not repaint,
; so it is callable without the gfx lock (SPEC.md 11).
; -----------------------------------------------------------------------------
wm_create:
    push ax
    push cx
    push si
    push di
    push es
    mov bx, wm_wins
    mov cx, MAX_WIN
.slot:
    test word [bx+W_FLAGS], 1
    jz .free
    add bx, WIN_SIZE
    loop .slot
    xor bx, bx                  ; table full
    stc
    jmp .out
.free:
    mov word [bx+W_FLAGS], 1    ; used, not visible
    lea di, [bx+W_X]
    mov cx, 8
    push ds                     ; the template is at ES:SI - the CALLER's
    push es                     ; segment (SPEC.md 11) - and the record is
    pop ds                      ; ours, so swap the two for the movsw and
    pop es                      ; swap them straight back
    cld
    rep movsw                   ; copy the template into x..onclick
    push ds
    push es
    pop ds
    pop es
    mov word [bx+W_MENUS], 0    ; menus are NOT a template word (SPEC 11/12.2)
    mov word [bx+W_ONSIZE], 0   ; ...and neither is the resize negotiator
    mov word [bx+W_ONMOUSEUP], 0    ; ...nor the release handler (SPEC.md 13.7).
                                ; The template copy above is EIGHT words and
                                ; must stay eight: growing it would read one
                                ; word past every existing package's template
%ifdef KERN_BIG
    mov word [bx+W_ONDRAG], 0   ; ...nor the tracking edge (SPEC.md 13.8.2),
                                ; for that same reason and no other
    mov word [bx+W_ONTIMER], 0  ; ...nor the timer's handler, nor its deadline
    mov word [bx+W_TIMER], 0    ; (SPEC.md 13.9) - and the deadline especially,
                                ; because a REUSED slot inheriting a live one
                                ; would fire a stranger's handler at a window
                                ; that has only just been made
%endif
    mov word [bx+W_DISP], PKG_DISP  ; the far pointer wm_pkgcall uses: the
    mov ax, es                      ; dispatcher's fixed offset, and the
    mov cx, cs                      ; segment the caller called us in. Ours
    cmp ax, cx                      ; means a KERNEL window, which is what
    jne .pkgseg                     ; W_SEG = 0 says (SPEC.md 20.1)
    xor ax, ax
.pkgseg:
    mov [bx+W_SEG], ax
    mov cx, 8                   ; (wm_fit and the index math below want CX
                                ; free; the rep consumed it)
    xor ax, ax                  ; ...and NO size-changed handler (SPEC.md
    call wm_onresize            ; 11.98). The `xor` is NOT redundant: three
                                ; lines up AX is 0 only for a KERNEL window and
                                ; is the PACKAGE's own segment otherwise, so
                                ; passing it through would install a handler at
                                ; whatever offset that segment number happens to
                                ; be - a near call into the middle of the
                                ; package's image on the first adapter change.
                                ; Cleared here and not only in wm_destroy where
                                ; its siblings are: destroy covers a slot that
                                ; has been USED, and this covers the first
                                ; window in a slot that never has
    call wm_nat_bank            ; the TEMPLATE's own rect, banked while it is
                                ; still in the record: this is the only moment
                                ; the UNCLAMPED size exists, and without it a
                                ; window created on the CGA could never grow
                                ; back when the machine returns to its VGA
                                ; (SPEC.md 39.11.2.1)
    push si                     ; ...and a fresh slot owns NO preference and NO
    call wm_pm_slot             ; minimum of the last tenant's (SPEC.md 11.100):
    mov word [wm_pref+si], 0    ; cleared HERE rather than in wm_destroy,
    mov word [wm_minw+si], 0    ; because wm_create is the one routine that
    mov word [wm_minh+si], 0    ; takes a slot - so no slot can be read before
    shr si, 1                   ; it has been written, whatever memory the
    mov byte [wm_usrsz+si], 0   ; machine came up holding. Nobody has sized a
    pop si                      ; window that has just been made, either
    call wm_fit                 ; templates are authored for 640x480; on a
                                ; smaller screen the record, not the template,
                                ; is the truth (SPEC.md 39.7)
%ifdef KERN_BIG
    push ax                     ; ...and the adapter it was sized FOR, which is
    call wm_kind_now            ; what 11.100.4 compares a drag's landing against
    call wm_kind_bank
    pop ax
%endif
    mov ax, bx                  ; index = (ptr - wm_wins) / WIN_SIZE
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    mov di, wm_zord
    add di, cx
    mov [di], al                ; append: new window is frontmost
    inc byte [wm_zn]
    clc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_onsize - install (or clear) a window's resize negotiator (SPEC.md 11.1)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_onsize:
    mov [bx+W_ONSIZE], ax
    ret

; -----------------------------------------------------------------------------
; wm_onresize - install (or clear) a window's size-CHANGED handler
;               (public; API 0x03D8, SPEC.md 11.98)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers preserved)
;
; THE OTHER HALF OF W_ONSIZE, and the two are not interchangeable. W_ONSIZE is
; a NEGOTIATOR: the kernel proposes a size, the app answers with one it will
; take, and it runs BEFORE anything moves (SPEC.md 11.1). This is the NOTICE
; after the fact - "your content box is now this; the frame has already
; changed" - and it exists because there is a case the negotiator cannot cover
; at all: SPEC.md 39.11.2's adapter change, where the screen moves under every
; window at once and no app proposed anything.
;
; The reported symptom was the Task Manager drawing its process list through
; the bottom of its own frame after a VGA -> CGA switch. 39.11.2.1 fixed the
; FRAME - it comes back the size it asked for - and left the other half
; standing: an app that derived a layout from the geometry ONCE, at launch,
; still holds the number it computed then, so while the machine is on the
; smaller adapter it lays out for a box it no longer has. The kernel is the
; only thing that knows the box moved, so the kernel has to say so.
;
; A SIDE TABLE, not a word in the record: WIN_SIZE is what wm_idx2ptr
; multiplies by and every reader of a window pays for a wider record, and
; wm_about (SPEC.md 12.2) is the precedent for a per-slot HANDLER living
; beside the table rather than in it. It also costs no .o88 a rebuild, which a
; record that grew would.
; -----------------------------------------------------------------------------
wm_onresize:
    push si
    call wm_szh_slot
    mov [si], ax
    pop si
    ret

; -----------------------------------------------------------------------------
; The window's own idea of its size: a PREFERENCE and a FLOOR (SPEC.md 11.100)
;
; wm_fit is a CLAMP and the natural bank is a REPLAY, and between them the
; kernel had no way to say "this is how big I want to be on THIS adapter".
; Every template in the tree is authored for 640x480, so on a CGA the clamp is
; what decides - ten of the nineteen windows on the apps floppy come out
; exactly 155 rows tall, which is the desktop band, and not one of them chose
; it. Eleven packages had grown per-adapter arithmetic of their own to escape
; that, in eleven shapes.
;
; The sequence is one routine, wm_sizefit, so the three cannot be composed
; differently in two places:
;
;   the PREFERENCE proposes -> the DISPLAY clamps it down -> the FLOOR lifts it
;
; and the floor is LAST so that it wins, which is this tree's own idiom rather
; than a new rule (ui_grow: "high bound first, low wins").
; -----------------------------------------------------------------------------
PREF_SIZE equ 12                ; three (w, h) pairs, VID_VGA / VID_HERC /
                                ; VID_CGA - which is 0, 1, 2, so the kind
                                ; indexes it and no lookup table is needed

; -----------------------------------------------------------------------------
; wm_pm_slot - SI = index*2, the stride wm_pref / wm_minw / wm_minh all share
; in:  BX = window ptr
; out: SI = index*2 (add the table's own base); every other register preserved
; clobbers: flags
; -----------------------------------------------------------------------------
wm_pm_slot:
    push ax
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    shl si, 1
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_min_ax - the minimum this window will accept on one axis, capped
; in:       BX = window ptr, DL != 0 = the HEIGHT axis / 0 = the WIDTH axis,
;           AX = the cap: how much of that axis the window's own DISPLAY can
;           give it from where the window starts
; out:      AX = the effective minimum; every other register preserved
; clobbers: AX, flags
;
; A window that declared nothing answers ZERO, and that is the whole of how
; this stays byte-identical for the rest of the tree: wm_fit's floor then lifts
; nothing at all. It deliberately does NOT default to WMIN_W / WMIN_H - those
; are the floor a USER RESIZE may not go below, not a size every window is
; entitled to, and applying them here would have grown the Timer (160x60, and
; WMIN_H is 64) on every machine. wm_min_axd is the variant the two resize
; paths want.
;
; THE CAP IS WHAT STOPS A MINIMUM COSTING A WINDOW ITS TITLE BAR (SPEC.md
; 11.100.2), and it is 11.93's fourth deliberate thing generalised: "the floor
; is the DISPLAY's bottom and not infinity, so a template taller than the whole
; adapter is still cut". A cap that has gone NEGATIVE means the window starts
; past that edge - vid_disp_of answers the PRIMARY for an origin in the dead
; zone, so it is reachable - and the honest minimum there is none at all.
; -----------------------------------------------------------------------------
wm_min_ax:
    push cx
    push si
    mov cx, ax                  ; the cap, before wm_pm_slot needs AX
    call wm_pm_slot             ; SI = index*2 (preserves AX)
    or dl, dl
    jz .x
    mov ax, [wm_minh+si]
    jmp short .cap
.x:
    mov ax, [wm_minw+si]
.cap:
    or ax, ax
    jz .out                     ; nothing declared: no floor, and no change
    or cx, cx
    jle .zero                   ; SIGNED: a negative cap is no room at all
    cmp ax, cx
    jbe .out
    mov ax, cx
.out:
    pop si
    pop cx
    ret
.zero:
    xor ax, ax
    jmp short .out

; -----------------------------------------------------------------------------
; wm_min_axd - ...or the SYSTEM floor, for the two paths a USER drives
; in/out:   wm_min_ax's, except that a window which declared nothing gets
;           WMIN_W / WMIN_H rather than 0
; clobbers: AX, flags
;
; ui_grow and wm_resize floored at those two long before a window could declare
; anything, and they still do - "the smallest frame a resize can leave" is a
; statement about the grow box and not about every window in the machine. The
; default is deliberately NOT capped, which is what it always was: low bound
; last, so it wins.
; -----------------------------------------------------------------------------
wm_min_axd:
    call wm_min_ax
    or ax, ax
    jnz .out
    or dl, dl
    jz .w
    mov ax, WMIN_H
    ret
.w:
    mov ax, WMIN_W
.out:
    ret

; -----------------------------------------------------------------------------
; wm_min_floor - lift W_W/W_H back to this window's minimum (module internal)
; in:       BX = window ptr, CX = the width cap, DX = the height cap (see
;           wm_min_ax)
; out:      W_W/W_H raised where they are under it; all registers preserved
; clobbers: flags
;
; It runs INSIDE wm_fit, after the size clamps and before the position ones, so
; the origin is settled against the size the window actually ends up with.
; -----------------------------------------------------------------------------
wm_min_floor:
    push ax
    push cx
    push dx
    mov ax, cx                  ; --- width ---
    xor dl, dl
    call wm_min_ax
    cmp [bx+W_W], ax
    jae .hgt
    mov [bx+W_W], ax
.hgt:
    pop dx                      ; --- height: the cap back, whole ---
    push dx
    mov ax, dx
    mov dl, 1
    call wm_min_ax
    cmp [bx+W_H], ax
    jae .out
    mov [bx+W_H], ax
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_usr_mark - "somebody chose this size by hand" (SPEC.md 11.100.5)
; in:  BX = window ptr
; out: nothing - every register preserved, FLAGS INCLUDED
;
; ui_grow and wm_zoom are the only two things in the machine a USER sizes a
; window with, so they are the only two callers. wm_resize is deliberately not
; one: that is an APPLICATION sizing itself, which is the same voice the
; preference speaks in.
; -----------------------------------------------------------------------------
wm_usr_mark:
    pushf
    push ax
    push si
    call wm_ptr2idx
    mov si, ax
    mov byte [wm_usrsz+si], 1
    pop si
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_pref_take - adopt this window's preferred size for adapter kind AL
; in:  BX = window ptr, AL = a VID_* kind
; out: CF = 0 and W_W/W_H written when a preference was declared for that kind;
;      CF = 1 = nothing to say, record untouched. Every register preserved.
;
; The table lives in the WINDOW'S OWN SEGMENT and the kernel keeps only the
; offset (SPEC.md 11.100.1), so it is read through wm_strseg - the same routine
; W_TITLE and every menu string go through, which answers KERNEL_SEG for a
; kernel window and so needs no special case for one.
;
; A (0, 0) pair is "nothing to say about that adapter" and leaves the record
; alone, which is what lets a window declare only the CGA entry it cares about.
; A pair with one zero in it is a pair, not a half-answer: both are written.
; -----------------------------------------------------------------------------
wm_pref_take:
    push ax
    push cx
    push si
    push di
    push es
    call wm_pm_slot
    push si
    shr si, 1
    cmp byte [wm_usrsz+si], 0
    pop si
    jne .no                     ; A USER OUTRANKS AN APPLICATION (SPEC.md
                                ; 11.100.5): once somebody has sized this window
                                ; by hand, the size it publishes is not the
                                ; kernel's to put back
    mov si, [wm_pref+si]
    or si, si
    jz .no                      ; no table: one word, and this is the whole cost
                                ; to a window that declared nothing
    mov di, bx
    call wm_strseg              ; ES = the window's own segment (0 = ours)
    mov cl, al
    mov ch, 0
    shl cx, 1
    shl cx, 1                   ; kind * 4: one (w, h) pair
    add si, cx
    mov ax, [es:si]
    mov cx, [es:si+2]
    or ax, ax
    jz .no                      ; (0, 0): nothing to say about this adapter
    or cx, cx
    jz .no
    mov bx, di
    mov [bx+W_W], ax
    mov [bx+W_H], cx
    pop es
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wm_kind_now - AL = the adapter kind window BX is standing on
; in:  BX = window ptr
; out: AL = a VID_* kind; every other register preserved
; clobbers: AX, flags
;
; The PRIMARY's kind on a one-display machine, and the kind of the display the
; window's ORIGIN is on otherwise - wm_fit_box's question rather than
; wm_disp_of's, because this decides which size a frame is clamped against and
; that is a question about the corner the clamps are written from.
; -----------------------------------------------------------------------------
wm_kind_now:
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .prim
    push di
    call wm_disp_now
    call vid_ctx_ptr
    mov al, [di+VID_CTX_KIND]
    pop di
    ret
.prim:
%endif
    mov al, [vid_kind]
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; wm_disp_now - WHICH DISPLAY IS THIS WINDOW'S? (SPEC.md 39.16.4.1)
; in:       BX = the window
; out:      AL = the display index; every other register preserved
; clobbers: AX, flags
;
; **ONE ANSWER, because two of them disagreed and the field found it.** A
; straddling window is SIZED for the more restrictive of the two displays
; (WINDOW-SIZING-PLAN 12.2) and was TOLD about the one its origin sits on - so
; the Task Manager straddling a Hercules and a CGA was handed the CGA's frame
; and the Hercules' geometry, worked out that one column fits, and stayed one
; column in a window twice that wide. Both callers come here now.
;
; Not straddling - or in the dead zone, where nothing does - the origin is the
; only answer that cannot leave a window unreachable (39.17).
; -----------------------------------------------------------------------------
wm_disp_now:
    cmp [wm_lf_win], bx         ; **A LANDING DECIDES THIS ONCE** (SPEC.md
    jne .ask                    ; 39.16.4.2): wm_land_fit arms the latch with
    mov al, [wm_lf_disp]        ; the answer it acted on, because the RESIZE it
    ret                         ; then applies can stop the straddle being
.ask:                           ; true - and every step after it re-derived
    push bx                     ; the answer from the new geometry
    push dx
    call wm_strads
    jc .one
    call wm_sf_axis             ; DL = the axis they tile on
    call wm_disp_rest           ; ...and AL = the MORE RESTRICTIVE of the two
    jmp short .out
.one:
    mov ax, [bx+W_X]
    mov bx, [bx+W_Y]
    call vid_disp_of
.out:
    pop dx
    pop bx
    ret
%endif

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; wm_disp_rest - which of the two displays is the MORE RESTRICTIVE?
; in:       DL = the axis, from wm_sf_axis
; out:      AL = 0 or 1, the display index; every other register preserved
; clobbers: AX, flags
;
; A STRADDLING WINDOW READS AS THE SMALLER CARD, which is what makes the seam
; one transition in each direction rather than two: dragging on from the
; straddle to the small display is then not a change of kind at all, so
; nothing resizes twice (WINDOW-SIZING-PLAN 12.2).
;
; The comparison is the USABLE BAND and not the raw extent, because the
; primary carries the menu bar and the dock and a secondary carries neither -
; a 348-row Hercules primary offers 304 rows where a 348-row secondary would
; offer 348. A tie goes to the primary, which is where windows are born.
; -----------------------------------------------------------------------------
wm_disp_rest:
    push cx
    push di
    mov al, 1
    call vid_ctx_ptr            ; (preserves AX and DX)
    or dl, dl
    jz .x
    mov cx, [vid_dock_y0]
    sub cx, MBAR_H
    cmp [di+VID_CTX_CH], cx
    jb .one
    jmp short .zero
.x:
    mov cx, [vid_pw]
    cmp [di+VID_CTX_CW], cx
    jb .one
.zero:
    xor al, al
    jmp short .out
.one:
    mov al, 1
.out:
    pop di
    pop cx
    ret
%endif

; -----------------------------------------------------------------------------
; wm_pref_now - take this window's preference for the adapter it is standing on
; in:  BX = window ptr
; out: CF as wm_pref_take's; every other register preserved
; clobbers: flags
;
; The pair wm_apply_size and wm_refit both want, in one place so the two cannot
; disagree about which display's kind decides.
; -----------------------------------------------------------------------------
wm_pref_now:
    push ax
    call wm_kind_now
    call wm_pref_take
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; wm_kind_bank - record the adapter kind this window has just been sized for
; in:  BX = window ptr
; out: nothing; every register preserved, FLAGS INCLUDED
;
; SPEC.md 11.100.4's comparand. Without it the adoption fires on every drag
; inside one display and overwrites whatever the app did with wm_resize between
; two of them.
; -----------------------------------------------------------------------------
wm_kind_bank:
    pushf
    push ax
    push si
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    call wm_kind_now            ; AL = the kind it is standing on
    mov [wm_pkind+si], al
    pop si
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_kind_was - AL = the adapter kind this window was last SIZED for
; in:  BX = window ptr
; out: AL = a VID_* kind; every other register preserved
; clobbers: AX, flags
; -----------------------------------------------------------------------------
wm_kind_was:
    push si
    call wm_ptr2idx
    mov si, ax
    mov al, [wm_pkind+si]
    pop si
    ret
%endif

; -----------------------------------------------------------------------------
; wm_display - OSAPI_VIDEO for the display THIS WINDOW is on (public; API
;              0x0488, SPEC.md 39.16.4)
; in:  BX = the window
; out: AX = width, BX = height, CX = the first row the DOCK owns, SI = the
;      first row the desktop band has, DL = 0 VGA / 1 Hercules / 2 CGA,
;      DH = bits per pixel; DI preserved
;
; **OSAPI_VIDEO ANSWERS ABOUT THE PRIMARY AND SAYS SO** (39.2.1), and it is
; right to: windows open there, everything a package draws is window-relative,
; and answering the UNION made Arkanoid lay itself out for a 1360px screen.
; What it cannot do is answer a package standing on the OTHER monitor - so
; every 11.98 handler in the tree re-read the wrong screen the moment its
; window was dragged across. sol_onresize on a 640x200 CGA re-derived HERCULES
; card metrics; pt_screen banked pt_scrh = 348 with its window on a 200-row
; display. The handlers followed **Activate Mode** correctly, that being a
; change of primary, and did not follow a DRAG.
;
; This is 53.7.1's OSAPI_FSX_SURF arriving in the windowed half of the system,
; and the same sentence covers both: *fullscreen is not the whole desktop, and
; neither is a window's screen.*
;
; SI IS THE BAND'S TOP AND IS NOT A CONSTANT. OSAPI_VIDEO implies MBAR_H
; because the primary is the only display with a menu bar; a secondary has
; neither bar nor dock, so its whole height is desktop and an app that assumed
; MBAR_H would waste 20 rows there and mis-centre everything. The band is
; CX - SI on either.
;
; Identical to OSAPI_VIDEO on every one-display machine by construction - the
; same four words come out of wm_fit_box's own early return.
; -----------------------------------------------------------------------------
wm_display:
%ifndef KERN_BIG
    mov si, MBAR_H              ; ONE DISPLAY, so this IS OSAPI_VIDEO and the
    jmp osapi_video             ; band starts under the menu bar. SHARING the
                                ; body rather than copying it is what makes
                                ; "identical on a one-display machine" a fact
                                ; instead of a claim - and osapi_video touches
                                ; no DI, so the contract holds without a frame
%else
    push di
    cmp byte [vid_ndisp], 1
    jbe .prim
    call wm_disp_now            ; --- which display is this window's? (39.16.4.1)
                                ; THE SAME ANSWER wm_kind_now uses, so a
                                ; straddling window is told about the display
                                ; it was sized for
    call vid_ctx_ptr            ; DI = its record (preserves AX and DX)
    or al, al
    jnz .second
.prim:                          ; display 0 IS the primary, so this arm is
    mov ax, [vid_pw]            ; the two-display answer for a window on the
    mov bx, [vid_ph]            ; near card
    mov cx, [vid_dock_y0]
    mov si, MBAR_H
    mov dl, [vid_kind]
    jmp short .bpp
.second:
    mov ax, [di+VID_CTX_CW]
    mov bx, [di+VID_CTX_CH]
    mov cx, bx                  ; NO DOCK, so the last row is the last row...
    xor si, si                  ; ...and NO MENU BAR, so the band starts at 0
    mov dl, [di+VID_CTX_KIND]
.bpp:
    mov dh, 1
    or dl, dl
    jnz .r                      ; VGA is the only 4bpp adapter there is (39.4)
    mov dh, 4
.r:
    pop di
%endif
    ret

; -----------------------------------------------------------------------------
; wm_prefer - "this is how big I want to be on each adapter" (public; API
;             0x0478, SPEC.md 11.100.1)
; in:  BX = window ptr, SI = the offset of a PREF_SIZE table in the window's own
;      segment, or 0 to withdraw
; out: nothing - every register preserved, FLAGS INCLUDED
;
; It registers AND APPLIES, and it re-fits from the natural bank for wm_keeph's
; reason: by the time an entry proc can call this wm_create has already run
; wm_fit, so the size the template asked for survives only in the bank. The
; FLAGS-preserving contract is wm_keeph's too - an entry proc's CF is the
; loader's return value, so a cmp in here would abort the launch.
; -----------------------------------------------------------------------------
wm_prefer:
    pushf
    push ax
    push si
    push di
    mov di, si
    call wm_pm_slot
    mov [wm_pref+si], di
    call wm_apply_wh            ; ...and take it now, WITHOUT moving the window
                                ; (SPEC.md 11.100.9)
    pop di
    pop si
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_minsize - "do not cut me past this" (public; API 0x0480, SPEC.md 11.100.2)
; in:  BX = window ptr, CX = minimum outer width, DX = minimum outer height
;      (0/0 withdraws)
; out: nothing - every register preserved, FLAGS INCLUDED
; -----------------------------------------------------------------------------
wm_minsize:
    pushf
    push ax
    push si
    call wm_pm_slot
    mov [wm_minw+si], cx
    mov [wm_minh+si], dx
    call wm_apply_wh            ; SPEC.md 11.100.9: a floor is a size and says
                                ; nothing about where the window is
    pop si
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_apply_size - replay the bank, take the preference, re-fit (module internal)
; in:  BX = window ptr
; out: W_X..W_H settled; every register preserved, FLAGS INCLUDED
;
; The one sequence SPEC.md 11.100 names - "preference proposes, display clamps,
; floor lifts" - and the floor itself lives inside wm_fit, where every other
; clamp already is.
;
; **wm_refit IS ITS ONLY CALLER, and the position is why** (SPEC.md 11.100.9).
; It replays the WHOLE banked rect, which is right for the one event it is for:
; the adapter changed under a window nobody has touched, so putting it back
; where it was before a clamp moved it is the whole of 39.11.2.1. It is wrong
; the moment a hand is on the window, and the three public cells below all go
; through wm_apply_wh instead.
; -----------------------------------------------------------------------------
wm_apply_size:
    pushf
    push ax
    call wm_nat_take            ; the rect it ASKED for, POSITION INCLUDED
    jmp short wm_apply_pf       ; (SPEC.md 39.11.2.1)

; -----------------------------------------------------------------------------
; wm_apply_wh - the same sequence with the ORIGIN LEFT ALONE (SPEC.md 11.100.9)
; in:  BX = window ptr
; out: W_W..W_H settled; every register preserved, FLAGS INCLUDED
;
; **A SIZING CELL PROPOSES A SIZE AND HAS NOTHING TO SAY ABOUT WHERE A WINDOW
; IS.** All three public cells reach this - wm_prefer, wm_minsize and wm_keeph -
; and at LAUNCH the distinction is invisible, because the bank IS the record
; that moment. It stops being invisible the first time one of them is called
; from an 11.98 handler, which is called from inside a DRAG's own notification:
; apps/browser's br_onresize re-asserts WF_KEEPH there, so every drag that
; changed adapter put the window back at its banked origin and then let wm_fit
; clamp the size to the room right of it. Reported from the field as three
; separate things - a drag that moves the window not at all, a drag that moves
; it and does not resize it, and a drag clear across that lands it against the
; edge "chopped thin in the X direction" - which are one cause: a Hercules x
; restored under a CGA landing puts wm_fit_box back on the Hercules, and 720
; minus that x is what the width becomes.
; -----------------------------------------------------------------------------
wm_apply_wh:
    pushf
    push ax
%ifdef KERN_BIG
    call wm_strads              ; **AND wm_fit IS SINGLE-DISPLAY** (SPEC.md
    jc .one                     ; 11.100.9.1): it confines a frame to the box
    call wm_strad_fit           ; the ORIGIN is in, so run on a straddling
    jmp short wm_apply_out      ; window it cuts the width to the columns the
%endif                          ; near card has left. wm_strad_fit IS this
.one:                           ; same sequence with the right clamp on the end
    call wm_nat_take_wh         ; the SIZE it asked for and not the position
wm_apply_pf:                    ; ...and from here the three are one routine
    call wm_pref_now            ; its own answer for the adapter it is on
    call wm_fit
wm_apply_out:                   ; NOT a local label: wm_apply_pf sits between
    pop ax                      ; the jump and the target, so `.out` would
    popf                        ; scope to the wrong one and not assemble
    ret

; -----------------------------------------------------------------------------
; wm_szh_slot - SI -> this window's size-changed handler word (module internal)
; in:  BX = window ptr
; out: SI = wm_onsz + index*2; every other register preserved
; clobbers: flags
; -----------------------------------------------------------------------------
wm_szh_slot:
    push ax
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    shl si, 1
    add si, wm_onsz
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_sz_notify - tell BX its environment moved, if it asked to be told
;                (SPEC.md 11.98)
; in:  BX = window ptr; the gfx lock held by the caller
; out: nothing (all registers preserved, FLAGS INCLUDED)
;
; THERE IS NO COMPARE, AND THAT IS THE POINT. The first version fired only
; when the content box had actually changed, which is the obvious economy and
; is wrong for the case this exists for: what goes stale across an adapter
; change is not only a window's BOX. apps/missile decides mc_mono, mc_ecoarse
; and mc_caps from the adapter at launch, apps/arkanoid ark_bpp,
; apps/solitaire sol_bpp, apps/paint pt_bpp - and a window small enough to fit
; two geometries unchanged (100x100 across VGA -> Hercules) would have got no
; notice at all and kept a stale bpp. On a 1bpp adapter apps/missile with
; mc_mono = 0 is SPEC.md 48.8's unplayable case, reached by a change that told
; it nothing.
;
; So the notice is "your environment moved - re-derive", one per window per
; refit, and the app decides what of it matters: OSAPI_WM_GEOM for the box,
; OSAPI_VIDEO for the adapter, both callable from here. It costs a handful of
; callbacks on an event a human causes by clicking Activate Mode.
;
; It runs with the LOCK HELD and the handler may not draw - that is the
; contract, and it is W_ONSIZE's contract too. It is also the only ordering
; that works: the caller's repaint comes after, so an app that relayouts here
; is painted correctly the first time, where a deferred notice would let one
; frame go up in the old layout.
; -----------------------------------------------------------------------------
wm_sz_notify:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call wm_szh_slot
    mov bp, [si]
    or bp, bp
    jz .out                     ; never asked to be told: one word, and this is
                                ; the whole cost to every window that has not
    call wm_geom                ; CX/DX = the content size the record has NOW
    mov si, bx                  ; SI = the window
    call wm_pkgcall             ; (BP is the handler; wm_pkgcall puts ES =
                                ; KERNEL_SEG for it, like every other callback)
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_onmouseup - install (or clear) a window's release handler (SPEC.md 13.7)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_onmouseup:
    mov [bx+W_ONMOUSEUP], ax
    ret

; -----------------------------------------------------------------------------
; wm_ondrag - install (or clear) a window's tracking handler (SPEC.md 13.8.2)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers preserved)
;
; A separate word from W_ONMOUSEUP rather than the same proc with a flag, and
; the reason is the failure mode: a package that ignored such a flag would
; treat every pointer movement as the release and fire its control on the way
; past. Two words is two contracts a package opts into one at a time.
; -----------------------------------------------------------------------------
%ifdef KERN_BIG
wm_ondrag:
    mov [bx+W_ONDRAG], ax
    ret

; -----------------------------------------------------------------------------
; wm_ontimer - install (or clear) a window's timer handler (SPEC.md 13.9)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_ontimer:
    mov [bx+W_ONTIMER], ax
    ret

; -----------------------------------------------------------------------------
; wm_timer - arm this window's one-shot timer (SPEC.md 13.9)
; in:  BX = window ptr, AX = ticks from now (0 = cancel)
; out: nothing (all registers preserved)
;
; TWO SENTINELS MEET HERE and the bias is what keeps them apart. AX = 0 means
; cancel; W_TIMER = 0 means disarmed - so a deadline that lands exactly on 0,
; which [ticks] + AX does once every 65,536 ticks, would arm a timer that
; never fires. It is biased to 1: one tick early, imperceptible, and the case
; stops existing rather than being reasoned about later.
;
; [wm_tarm] is the gate the scan reads. Only arming sets it and only the scan
; clears it, so the flag can be STALE (one wasted walk) and never MISSING (a
; timer that never fires) - which is the direction that matters, and is why
; this is a self-healing flag rather than a count that three places would
; have to keep right.
; -----------------------------------------------------------------------------
wm_timer:
    push ax
    or ax, ax
    jz .cancel
    add ax, [ticks]
    jnz .set                    ; ...and never 0, which means disarmed
    inc ax
.set:
    mov [bx+W_TIMER], ax
    mov byte [wm_tarm], 1
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
    pop ax
    ret
.cancel:
    mov [bx+W_TIMER], ax        ; AX is 0 here: cancel is a store of the
    pop ax                      ; sentinel and nothing else. [wm_tarm] is NOT
    ret                         ; cleared - the scan owns that, and clearing it
                                ; here would need to know whether any OTHER
                                ; window still has one
%endif                          ; KERN_BIG

; -----------------------------------------------------------------------------
; wm_onwake - install (or clear) a window's WAKE handler (SPEC.md 74.1)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers AND the flags preserved)
;
; wm_onresize's shape exactly and for its reason: a SIDE TABLE, not a word in
; the record, because WIN_SIZE is the stride every reader of a window pays and
; growing it costs every existing .o88 a rebuild. Cleared by wm_destroy, so a
; reused slot never inherits a stranger's near pointer.
;
; **AND THE FLAGS, which it did not promise until SPEC.md 54.10** - wm_onclose
; already did, for exactly the reason that now applies here. The SDK tells a
; package to install this from its ENTRY PROC after OSAPI_WM_CREATE, where the
; CF the entry owes the loader (SPEC.md 21) is still riding in the flags, and
; wm_wkh_slot's `div` writes them. Every associated app installs one now
; (SPEC.md 54.10), so the alternative was a pushf/popf pair at nine call sites
; and a silent aborted launch at whichever one forgot.
; -----------------------------------------------------------------------------
wm_onwake:
    pushf
    push si
    call wm_wkh_slot
    mov [si], ax
    pop si
    popf
    ret

; -----------------------------------------------------------------------------
; wm_onclose - install (or clear) a window's CLOSE negotiator (SPEC.md 75.1)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers AND the flags preserved)
;
; wm_onwake's shape and reason - a SIDE TABLE, not a word in the record,
; because WIN_SIZE is the stride every reader of a window pays. Cleared by
; wm_destroy, so a reused slot never inherits a stranger's near pointer, and
; by wm_init, because unlike every other side table here this one is read for
; a window that never installed anything.
; -----------------------------------------------------------------------------
wm_onclose:
    pushf                       ; ...and the FLAGS, which the other installers
                                ; do not promise: this one is called from an
                                ; entry proc, where the CF a package owes the
                                ; loader is still riding in them (SPEC.md 21),
                                ; and wm_ptr2idx's shift writes them
    push ax
    push si
    push ax                     ; the handler, banked: wm_clh_slot spends AX
    call wm_clh_slot            ; SI -> this window's word
    pop ax
    mov [si], ax
    pop si
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_clh_slot - SI -> this window's close-negotiator word (module internal)
; in:  BX = window ptr
; out: SI = wm_oncl + index*2; AX clobbered, every other register preserved
; -----------------------------------------------------------------------------
wm_clh_slot:
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    shl si, 1
    add si, wm_oncl
    ret

; -----------------------------------------------------------------------------
; wm_onrclick - install (or clear) a window's RIGHT-CLICK handler (SPEC.md
;               13.11, slot OSAPI_WM_ONRCLICK)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers preserved)
;
; wm_onwake's shape and reason - a SIDE TABLE, not a word in the record,
; because WIN_SIZE is the stride every reader of a window pays and growing it
; costs every existing .o88 a rebuild. Cleared by wm_destroy, so a reused slot
; never inherits a stranger's near pointer, and by wm_init, because like
; wm_oncl this word is read for a window that never installed anything.
;
; It is in kern_small too: a side table is 24 bytes of .bss and the dispatch
; is a word read on a path that today does nothing at all for a package
; window - the record-growing that made SPEC.md 13.8.2 and 13.9 kern_big's
; alone is exactly what this does not do.
;
; The FLAGS are preserved as well, which wm_onwake does not promise and
; wm_onclose does: this is installed from an entry proc, where the CF a
; package owes the loader is still riding in them (SPEC.md 21), and
; wm_ptr2idx's div writes them.
; -----------------------------------------------------------------------------
wm_onrclick:
    pushf
    push si
    call wm_rch_slot
    mov [si], ax
    pop si
    popf
    ret

; -----------------------------------------------------------------------------
; wm_rch_slot - SI -> this window's right-click handler word (module internal)
; in:  BX = window ptr
; out: SI = wm_onrc + index*2; every other register preserved
; clobbers: flags
;
; wm_clh_slot's answer plus the distance between the two tables, rather than a
; second copy of the index arithmetic: they are adjacent in .bss and declared
; adjacent on purpose (wm_init zeroes them in one run), so the distance is a
; constant NASM computes. kern_small had 68 bytes of image rung left before
; this feature and has 1 after it, which is the whole reason five bytes were
; worth writing down - and is the number the next change here has to beat.
; -----------------------------------------------------------------------------
wm_rch_slot:
    push ax                     ; wm_clh_slot spends it; the caller's AX is
    call wm_clh_slot            ; the handler being installed
    pop ax
    add si, wm_onrc - wm_oncl
    ret

; -----------------------------------------------------------------------------
; wm_ask_close - put the close to the window before it happens (SPEC.md 75.1)
; in:  BX = window ptr; the caller HOLDS the gfx lock and is the UI task
; out: CF = 1 the window REFUSED and nothing may be torn down; CF = 0 go
;      ahead. Every register preserved.
;
; A window with no negotiator - every kernel window, and every package that
; never called OSAPI_WM_ONCLOSE - costs one compare and answers yes, which is
; what every close in this system did before this existed.
;
; UNBILLED, exactly as the close box already is, and for app_close_win's own
; reason: the record the cycles would go to is the one about to be freed.
; STAMPED all the same (SPEC.md 34.3), because a negotiator is a callback and
; a sound grant taken inside an unstamped one gets snd_req_inst's 0xFF
; fallback - which no instance owns and nothing ever releases (SPEC.md 38.6's
; bug, in the one place it would otherwise be repeated).
;
; THE FENCE IS NOT DECORATION. The negotiator runs under the gfx lock on the
; UI task, so what it may do is what W_ONCLICK may do - and that includes
; raising an alert (SPEC.md 75.3) or a file dialog, either of which can end
; up back in a close path. Asking the same window again would recurse on a
; 256-byte task stack (SPEC.md 8): sch_stkdie, not a wrong answer. The fence
; answers REFUSED for the one window whose negotiator is live, so the
; recursion terminates at depth one and the outer ask still decides. It is
; SAVED AND RESTORED rather than set and cleared, because closing a DIFFERENT
; window of your own from inside a negotiator is legal and the outer fence has
; to survive it.
; -----------------------------------------------------------------------------
wm_ask_close:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, bx
    call wm_clh_slot            ; SI -> the word... and BX must stay the window
    mov bp, [si]
    mov si, bx                  ; SI = the window: wm_pkgcall's contract
    or bp, bp
    jz .yes                     ; no negotiator: the close goes ahead
    cmp [wm_clask], si
    je .no                      ; its own negotiator is already on the stack
    push word [wm_clask]        ; nested, not replaced - AND ON THE STACK, the
    mov [wm_clask], si          ; way [snd_inst] is two lines below. A register
                                ; will not do it: wm_pkgcall restores DS and ES
                                ; and NOTHING else, and a negotiator may clobber
                                ; every one of AX/BX/CX/DX/SI/DI - np_onclose
                                ; already does, through os88ui_ask. Banked in CX
                                ; the fence came back holding whatever the
                                ; package left there and was never 0 again, so
                                ; the recursion this exists to terminate ran
                                ; unguarded from the first dirty close onward.
                                ; POP writes no flags, so the negotiator's CF
                                ; still reaches the branch below untouched
    call inst_win_owner         ; DI = the owning record, 0 = unowned
    push word [snd_inst]
    call snd_disp_set
    call wm_pkgcall             ; SI = window, BP = the negotiator; CF is its
                                ; ANSWER, so nothing between here and the
                                ; branch may write the flags - and POP does
                                ; not (SPEC.md 20.3's own argument for the
                                ; API cell's shape)
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop word [wm_clask]         ; ...and the outer ask's fence
    jc .no
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_close_req - a package asks for its own window to be closed (SPEC.md 75.2)
; in:  BX = a window ptr
; out: nothing (all registers preserved)
;
; DEFERRED, and that is the contract rather than an implementation detail. The
; caller is a package's own callback - typically its answer to the alert its
; negotiator raised - so it is standing INSIDE its own segment, and
; app_close_win frees that segment (SPEC.md 29.4). The far return would then
; execute the three dispatcher bytes at PKG_DISP out of memory the heap has
; already handed back. So the request is one word, and wm_close_pass spends it
; on the next UI pass with no package code on the stack at all.
;
; It does NOT ask the negotiator again: this IS the answer to the question the
; negotiator asked, so a second one would be a loop every app would have to
; break with a latch of its own. wm_close_pass retires the negotiator instead,
; which puts "no more questions" in one place rather than in every package.
; -----------------------------------------------------------------------------
wm_close_req:
    mov [wm_clreq], bx
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
    ret

; -----------------------------------------------------------------------------
; wm_close_pass - spend a deferred close (SPEC.md 75.2)
; in:  nothing; the UI task's idle section, NO lock held
; out: nothing (all registers preserved)
;
; fdlg_reap's shape: one compare per pass when nothing is pending, which is
; nearly always. The request is cleared BEFORE the close, so nothing this
; reaches can see a request that has already been spent - and wm_destroy
; clears it too, so a slot that died between the request and this pass cannot
; be closed through a pointer that now means a different window.
; -----------------------------------------------------------------------------
wm_close_pass:
    cmp word [wm_clreq], 0
    je .out
    push ax
    push bx
    call gfx_lock               ; ...AND THE REQUEST IS SPENT UNDER THE LOCK,
    mov bx, [wm_clreq]          ; not before it. wm_destroy cancels a request
    or bx, bx                   ; naming the window it is freeing and runs with
    jz .done                    ; the lock held, so taking the word before the
    mov word [wm_clreq], 0      ; wait left exactly the interval that guard was
                                ; written to cover: a request read here, the
                                ; window destroyed while this pass waited, and
                                ; the cancellation written into a word this
                                ; pass had already emptied
    test word [bx+W_FLAGS], 1   ; still a live record? A minimized window is
    jz .done                    ; closable, so this is bit 0 and not bit 1
    xor ax, ax                  ; retire the negotiator: the request IS its
    call wm_onclose             ; answer (see wm_close_req)
    call app_close_win
.done:
    call gfx_unlock
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_wkh_slot - SI -> this window's wake handler word (module internal)
; in:  BX = window ptr
; out: SI = wm_onwk + index*2; every other register preserved
; clobbers: flags
; -----------------------------------------------------------------------------
wm_wkh_slot:
    push ax
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    shl si, 1
    add si, wm_onwk
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_wake - post an EVT_WAKE for a window (SPEC.md 74.1, slot OSAPI_WM_WAKE)
; in:  BX = window ptr
; out: CF=0 a wake is queued for this window (posted now, or one was already
;      waiting - the two are the same promise); CF=1 the queue was full and
;      nothing was posted. Every register preserved.
;
; ISR- and worker-safe: the record is filled and pushed under one
; pushf/cli ... popf, so two posters cannot interleave in the one static
; record, and evq_push's own guard nests inside it harmlessly.
;
; COALESCED, one queued wake per window: the ring is 16 records and drops
; silently when full (SPEC.md 10), and a package that kicks from every
; callback - which is what SPEC.md 71 tells it to do, so a dropped wake cannot
; stall it - would otherwise be the thing that fills the ring and drops the
; mouse. The flag is per SLOT (wm_wkq), set here and cleared by wm_wake_disp
; BEFORE the handler runs, so a handler that re-posts itself gets exactly one
; more record.
;
; **THE FLAG AND THE RECORD ARE ONE FACT, AND A DRAIN CAN BREAK THEM APART**
; (SPEC.md 74.1.1). Four loops pop records they are not interested in and
; throw them away - ui_drag and ui_grow drain everything that is not an
; EVT_MUP, fsx and snd empty the ring outright - and an EVT_WAKE eaten there
; used to leave this flag set with no record behind it. The window is then
; DEAF FOR THE REST OF ITS LIFE: every wm_wake after it reads "one is already
; queued" and answers CF=0, a promise nothing will ever keep. So the flag has
; three states and every drain calls wm_wake_eaten.
; -----------------------------------------------------------------------------
WKQ_NONE    equ 0               ; nothing owed
WKQ_RING    equ 1               ; ...owed, and the record is in the ring
WKQ_EATEN   equ 2               ; ...owed, and a drain ate the record

wm_wake:
    push ax
    push si
    call wm_ptr2idx             ; AL = index (AH = 0)
    mov si, ax
    pushf
    cli
    cmp byte [wm_wkq+si], WKQ_RING
    je  .queued                 ; one is already in the ring: same promise.
                                ; **`je 1`, NOT `jne 0`** - WKQ_EATEN is the
                                ; state where the promise is owed and the
                                ; record is GONE, and that one has to post
    mov ax, [evq_count]
    cmp ax, EVQ_CAP
    jae .full                   ; evq_push would drop it silently; say so
    mov byte [wm_wkq+si], WKQ_RING
    mov word [wm_wk_rec+EV_TYPE], EVT_WAKE
    mov [wm_wk_rec+EV_A], bx
    mov si, wm_wk_rec
    call evq_push
.queued:
    popf
    clc
    jmp short .out
.full:
    popf
    stc
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_wake_eaten - a drain threw a record away; if it was a wake, say so
; in:  DI = the record evq_pop just filled
; out: nothing. EVERY register preserved, because every caller is a loop that
;      is in the middle of deciding something else.
;
; Called by all four drains (SPEC.md 74.1.1). It is cheap on the common path -
; one word compare - and it is what keeps wm_wkq honest: the flag drops from
; WKQ_RING to WKQ_EATEN, which is "still owed, no record", and wm_wake_redo
; puts a fresh record in the ring the moment the drain's owner is done.
;
; A wake for a window that has since died is marked all the same: wm_wake_redo
; posts it, wm_wake_disp sees a free record and calls nothing. That is the
; existing "a wake for the dead" path and it costs one ring slot, once.
; -----------------------------------------------------------------------------
wm_wake_eaten:
    cmp word [di+EV_TYPE], EVT_WAKE
    jne .out
    push ax
    push bx
    push si
    mov bx, [di+EV_A]
    call wm_ptr2idx             ; AL = index (AH = 0)
    mov si, ax
    cmp byte [wm_wkq+si], WKQ_RING
    jne .pop                    ; not ours to downgrade: a stale record for a
                                ; slot already re-promised, or already eaten
    mov byte [wm_wkq+si], WKQ_EATEN
    mov byte [wm_wkdef], 1      ; ...and the one byte ui_task tests per pass
.pop:
    pop si
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_wake_redo - put back every wake a drain ate. Called once per ui_task pass
; out: nothing. Every register preserved.
;
; NOT called from inside a drain: re-posting there would be popped and eaten
; again by the same loop, and with nothing else in the ring that is a spin
; rather than a recovery. The right moment is the top of the pass AFTER the
; drag, resize, full-screen app or sound bracket has finished - which is here,
; and is why the wake is only ever DELAYED by one of those and never lost.
; -----------------------------------------------------------------------------
wm_wake_redo:
    cmp byte [wm_wkdef], 0
    je  .out                    ; the whole cost on every other pass
    push ax
    push bx
    push cx
    push si
    mov byte [wm_wkdef], 0
    xor si, si
    mov cx, MAX_WIN
.l:
    pushf                       ; the test and the clear are ONE decision, and
    cli                         ; wm_wake runs on WORKERS (OSAPI_WM_WAKE): a
    cmp byte [wm_wkq+si], WKQ_EATEN ; switch between them lets one post a
    je  .redo                   ; record and set WKQ_RING for this slot, and
    popf                        ; the clear would then drop that record's flag
    jmp short .n                ; on the floor and post a SECOND for it
.redo:
    mov byte [wm_wkq+si], WKQ_NONE  ; ...so wm_wake below really posts
    popf                            ; released BEFORE the call below, which
                                    ; takes its own guard
    mov ax, si
    call wm_idx2ptr             ; BX = the window record
    call wm_wake
    jnc .n
    mov byte [wm_wkq+si], WKQ_EATEN ; the ring was full: still owed, and the
    mov byte [wm_wkdef], 1          ; next pass tries again
.n:
    inc si
    loop .l
    pop si
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_wake_disp - dispatch one EVT_WAKE (SPEC.md 74.1); called by ui_task
; in:  SI = the window ptr the record carried; the UI task, gfx lock NOT held
; out: nothing
; clobbers: AX, BX, CX, DX, SI, DI, BP (a dispatch site, like ui_ptcall's)
;
; The slot's flag is cleared FIRST, so the handler may re-post from inside
; itself. A window that died between the post and the pop is a record with
; bit 0 clear, and nothing is called; a slot reused by a new tenant since the
; post gets one early wake, which a wake handler must be indifferent to by
; construction (it is a KICK, not a message). Billed to the owning instance
; and stamped for it - ui_ptcall's environment minus the lock - so the file
; slots the handler is entitled to call (SPEC.md 74.1) resolve in the
; handler's own instance's folder, not the machine's.
; -----------------------------------------------------------------------------
wm_wake_disp:
    mov bx, si
    test word [bx+W_FLAGS], 1
    jz wm_wk_none               ; the record is free: a wake for the dead
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    mov byte [wm_wkq+si], 0     ; the promise is spent by this dispatch
    shl si, 1
    jmp short wm_wk_go

; -----------------------------------------------------------------------------
; wm_wake_call - call a window's wake handler NOW (SPEC.md 54.10)
; in:  SI = the window ptr; the UI task, gfx lock NOT held
; out: nothing
; clobbers: AX, BX, CX, DX, SI, DI, BP - wm_wake_disp's, because it IS
;           wm_wake_disp from the second half down
;
; The same dispatch with no queued promise to spend: the caller is not a
; popped EVT_WAKE but the kernel itself, standing where SPEC.md 74.1 says the
; handler stands. So [wm_wkq] is deliberately NOT touched - a wake this window
; really did post is still owed after this one, and clearing the flag here
; would leave a record in the ring with nothing saying it is coming.
;
; Its one caller is assoc_run (SPEC.md 54.10), which uses it to say "your
; window is on the glass now, spend the document you were handed". That is a
; kick like any other and the handler tells the two apart the only way a wake
; handler ever can: by its own pending flag.
; -----------------------------------------------------------------------------
wm_wake_call:
    mov bx, si
    test word [bx+W_FLAGS], 1
    jz wm_wk_none               ; the record is free, the entry proc's window
    call wm_ptr2idx             ; having failed or gone
    mov si, ax
    shl si, 1
wm_wk_go:
    mov bp, [wm_onwk+si]
    or bp, bp
    jz wm_wk_none               ; never asked to be woken
    mov si, bx
    call ui_ptcall              ; SI = window, BP = the handler; CX/DX are
                                ; carried and meaningless here
wm_wk_none:
    ret

; -----------------------------------------------------------------------------
; wm_ask_size - put a proposed size to the window before committing it
; in:  BX = window ptr, CX = proposed width, DX = proposed height
; out: CX/DX = the size the window will accept; every other register
;      preserved
; clobbers: nothing else (flags)
;
; The other half of what docs/PAINT-NOTES.md asked for. wm_resize is how an
; app changes its OWN size; this is how the kernel asks before changing it
; for them - and the answer is a size rather than a yes/no, because the case
; that motivated it (apps/paint refusing to crop artwork) is per-axis: a drag
; that would lose columns but not rows should still get its rows.
;
; A window with no negotiator - every kernel window, and every package that
; never called OSAPI_WM_ONSIZE - answers by leaving CX/DX alone, so the cost
; of the feature to everything that does not use it is one compare.
; -----------------------------------------------------------------------------
wm_ask_size:
    push ax
    push si
    push bp
    cmp word [bx+W_ONSIZE], 0
    je .out
    mov si, bx
    mov bp, [bx+W_ONSIZE]
    call wm_pkgcall             ; SI = window, CX/DX = the proposal
.out:
    pop bp
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_resize - set a window's size from its owner (SPEC.md 11.1)
;
; in:  BX = window ptr, CX = new outer width, DX = new outer height;
;      caller HOLDS the gfx lock (a window callback does)
; out: nothing (all registers preserved); the record carries the clamped
;      size and everything is repainted
;
; The sanctioned version of what apps/paint had to do behind the kernel's
; back (docs/PAINT-NOTES.md): an app that adopts a picture's dimensions has
; to move its own frame, and ui_grow and wm_fullscreen were the only things
; that could. Clamps exactly as a drag does - never smaller than WMIN_W/
; WMIN_H, never past the screen edge or the dock - and then re-fits the
; origin the way ui_drag does, so a window that grew at the right edge slides
; left instead of hanging off it.
; -----------------------------------------------------------------------------
wm_resize:
    call wm_rz_bank             ; where it was, for the repaint at the end
wm_resize_nb:                   ; ...and the entry for a caller that has
                                ; banked it already, because it is about to
                                ; move the ORIGIN too (wm_zoom's .go)
    push ax
    push cx
    push dx
    cmp cx, WMIN_W
    jae .wok
    mov cx, WMIN_W
.wok:
    mov ax, [vid_w]             ; never wider than the screen
    cmp cx, ax
    jbe .wok2
    mov cx, ax
.wok2:
    cmp dx, WMIN_H
    jae .hok
    mov dx, WMIN_H
.hok:
    mov ax, [vid_dock_y0]       ; ...nor taller than the desktop band
    sub ax, MBAR_H
    cmp dx, ax
    jbe .hok2
    mov dx, ax
.hok2:
    mov [bx+W_W], cx
    mov [bx+W_H], dx
    push cx                     ; ...AND THE FLOOR LAST, SO IT WINS (SPEC.md
    push dx                     ; 11.100.2). Below both ceilings, or the band
    mov cx, [vid_w]             ; clamp above would cut a declared minimum
    mov dx, [vid_ph]            ; straight back off again
    sub dx, MBAR_H
    call wm_min_floor
    pop dx
    pop cx
    mov cx, [bx+W_W]            ; ...and the position clamps below are written
    mov dx, [bx+W_H]            ; from CX/DX, so they read what was stored
    mov ax, [vid_w]             ; x + w <= screen width
    sub ax, cx
    jns .xc
    xor ax, ax
.xc:
    cmp [bx+W_X], ax
    jle .xs
    mov [bx+W_X], ax
.xs:
    call wm_snap_win            ; the width changed, so the right-edge clamp
                                ; may have moved x (SPEC.md 11.94)
    mov ax, [vid_dock_y0]       ; y + h <= the row the dock owns
    sub ax, dx
    cmp ax, MBAR_H
    jge .yc2
    mov ax, MBAR_H
.yc2:
    cmp [bx+W_Y], ax
    jle .paint
    mov [bx+W_Y], ax
.paint:
    call wm_nat_bank            ; an app's own size is the one it goes back to
                                ; on an adapter change (SPEC.md 39.11.2.1)

    ; --- A PURE SHRINK OWES ITS CONTENT NOTHING HERE TOO (SPEC.md 11.90.3.1)
    ; ui_grow's release arms this for a hand on the grow box; a package that
    ; resizes itself BY NAME comes through here instead, and was paying a full
    ; content repaint for a window whose every surviving pixel is still its
    ; own. Same predicate, off the rect wm_rz_bank put aside - and it reaches
    ; only a WF_OWNBG window that ASKS for a damage rect, because that is
    ; where wm_damage consumes it. A package that draws its whole face every
    ; paint (cal_hist_toggle folds its history pane through here) never sees
    ; it and is repainted exactly as before.
    push ax
    mov word [wm_dmg_rzwin], 0
    mov ax, [bx+W_X]
    cmp ax, [wm_rz_ox]
    jne .norz                   ; the origin moved: the content went with it
    mov ax, [bx+W_Y]
    cmp ax, [wm_rz_oy]
    jne .norz
    mov ax, [bx+W_W]
    cmp ax, [wm_rz_ow]
    ja .norz                    ; wider: the new columns were never drawn
    mov ax, [bx+W_H]
    cmp ax, [wm_rz_oh]
    ja .norz
    mov [wm_dmg_rzwin], bx
.norz:
    pop ax
    call wm_rz_paint            ; ...and only what the move disturbed (11.95.1)
    mov word [wm_dmg_rzwin], 0  ; a one-shot, exactly as ui_grow's is
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_rz_swap - exchange BX's rect with the one wm_rz_bank put aside
; in:  BX = the window; [wm_rz_o*] holds the other rect
; out: nothing; DI preserved, and AX/CX/SI are NOT
; clobbers: AX, CX, SI, flags
;
; Four words each and laid out the same way, so this is one loop rather than
; eight named moves - and calling it TWICE puts everything back, which is what
; makes wm_rz_precover a bracket rather than a rewrite.
;
; IT SAVES DI AND NOTHING ELSE, which is a size decision and not a style one:
; its one caller sits inside wm_rz_paint, past that routine's own push of AX,
; CX, DX and SI, so three of the four registers are already on the stack. Six
; bytes, and the DISKCNT knob kernel is close enough to MIN_RAM_KB
; (docs/KERNEL-MEMORY.md) that six bytes is the difference between this
; assembling and not.
; -----------------------------------------------------------------------------
wm_rz_swap:
    push di
    mov si, wm_rz_ox
    mov di, bx
    add di, W_X
    mov cx, 4
.w:
    mov ax, [si]
    xchg ax, [di]
    mov [si], ax
    inc si
    inc si
    inc di
    inc di
    loop .w
    pop di
    ret

; -----------------------------------------------------------------------------
; wm_rz_precover - bank what a window about to GROW is going to cover
; in:  BX = the window, its record holding the NEW rect and [wm_rz_o*] the old;
;      caller holds the gfx lock and nothing has been drawn yet
; out: nothing; AX/CX/SI are clobbered, which wm_rz_paint has already saved
;
; SPEC.md 11.96.16.2. wm_su_precover (11.96.16) banks the windows an incoming
; window is about to land on, and it had exactly one caller - wm_show_b - so a
; window covered by the FRONT one being ZOOMED over it was banked by nothing
; at all, and the raise back was always a full repaint. Measured: a maximize
; over the Task Manager took 0 caches.
;
; THE TWO TESTS WANT THE RECT AT TWO DIFFERENT MOMENTS, which is the whole of
; why this is not four lines. "Do we land on it" is about the rect we are
; about to have; "is its content still its own" is about the GLASS, which
; still shows us where we were. wm_show_b gets that free - a window that has
; never been drawn is invisible to wm_obscured - and a resize does not, so the
; record goes BACK to the old rect for the walk and the new one is handed to
; the overlap test through wm_su_pcrp.
;
; Get it the other way round and it banks the coverer's own pixels as the
; covered window's content, for every window the OLD rect was already sitting
; on: a restore then paints this window's face into somebody else's.
;
; A window ABOVE us may be banked here, which wm_show_b's caller cannot do
; because it goes to the front. It is waste and not a defect - wm_obscured
; still says its content is its own, so what is banked is correct - and the
; alternative is a z-order compare in a walk that has none.
; -----------------------------------------------------------------------------
wm_rz_precover:
    pushf
    push di
    call wm_rz_swap             ; the record back to where the GLASS has it
    mov word [wm_su_pcrp], wm_rz_ox - W_X   ; ...and the NEW rect, now in the
                                ; bank, is what "do we land on it" asks against
    mov di, bx
    call wm_su_precover
    mov word [wm_su_pcrp], 0
    call wm_rz_swap             ; ...and the record forward again
    pop di
    popf
    ret

; -----------------------------------------------------------------------------
; wm_dg_precover - the same, for a window that MOVED rather than resized
; in:  SI = the window, its record holding the NEW rect; AX/BX/CX/DX = the
;      x/y/w/h it LEFT; caller holds the gfx lock and nothing has been drawn
; out: nothing (every register and the flags preserved)
;
; SPEC.md 11.96.16.2. wm_rz_paint is the funnel for every GEOMETRY change and
; catches the zoom, the zoom's restore and the grow box - and a drag is none
; of them: ui_drag writes x/y into the record itself and repaints through
; wm_paint_dmg, so dragging a window wholly over another banked nothing.
;
; The old rect arrives in registers rather than being read back, because
; ui_drag has it in [ui_orig*]/[ui_drag*] and the record does not - it moved
; two screens up. wm_rz_o* is free at that moment: it is banked across ONE
; geometry change and a drop is not one, so this borrows the same four words
; wm_rz_swap above already knows how to exchange.
; -----------------------------------------------------------------------------
wm_dg_precover:
    pushf
    push bx
    mov [wm_rz_ox], ax
    mov [wm_rz_oy], bx
    mov [wm_rz_ow], cx
    mov [wm_rz_oh], dx
    mov bx, si
    call wm_rz_precover
    pop bx
    popf
    ret

; -----------------------------------------------------------------------------
; wm_rz_bank  - bank BX's rect before a geometry change
; wm_rz_paint - put back what that change actually disturbed
; in:  BX = the window; wm_rz_paint also needs [wm_rz_o*] and the gfx lock
; out: nothing (all registers preserved)
;
; **A window that GREW reveals nothing** (SPEC.md 11.95.1). Every pixel it
; gave up it also covers, so there is no desktop to put back, no drive zone
; to redraw and no other window to repaint - which is SPEC.md 11.90's whole
; argument about raising, arriving at a resize. Both of these used to end in
; wm_paint_all: a screen's worth of dither, every drive zone, both strips and
; every window's W_PAINT, to enlarge one window over ground it already owned.
;
; The test is the four edges plus one question: nothing may be ABOVE us, or it
; would have to be drawn back on top. That holds by construction for the case
; this exists for - SPEC.md 11.95's zoom is the SECOND press on a title bar,
; and the first one raised the window - but it is tested rather than assumed,
; because wm_resize is a published slot any application may call.
;
; Anything else is the union of where it was and where it is, through
; wm_paint_dmg (SPEC.md 11.91) - which is what a shrink genuinely owes, and
; still an enormous improvement on the whole screen. The union is computed one
; pixel wide at the far edges, which is exactly the drop shadow.
; -----------------------------------------------------------------------------
wm_rz_bank:
    push ax
    mov ax, [bx+W_X]
    mov [wm_rz_ox], ax
    mov ax, [bx+W_Y]
    mov [wm_rz_oy], ax
    mov ax, [bx+W_W]
    mov [wm_rz_ow], ax
    mov ax, [bx+W_H]
    mov [wm_rz_oh], ax
    pop ax
    ret

wm_rz_paint:
    KFZ 17                      ; every GEOMETRY change funnels here
    push ax
    push bx
    push cx
    push dx
    push si
%ifndef REDRAWFULL
    call wm_rz_precover         ; SPEC.md 11.96.16.2, and BEFORE either branch
%endif                          ; below puts a pixel anywhere

    mov ax, [bx+W_X]            ; --- did the new rect swallow the old one?
    cmp ax, [wm_rz_ox]
    jg .dmg
    mov ax, [bx+W_Y]
    cmp ax, [wm_rz_oy]
    jg .dmg
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    mov cx, [wm_rz_ox]
    add cx, [wm_rz_ow]
    cmp ax, cx
    jl .dmg
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    mov cx, [wm_rz_oy]
    add cx, [wm_rz_oh]
    cmp ax, cx
    jl .dmg

    mov si, bx                  ; --- and is there nothing above it?
    call wm_top                 ; BX = the frontmost visible window
    cmp bx, si
    mov bx, si
    jne .dmg
    push bp                     ; wm_draw_win takes "which window is FRONTMOST"
    mov bp, bx                  ; in BP, and this path has just proved we are
                                ; it. Without this the title bar is drawn with
                                ; whatever BP the caller happened to hold, so a
                                ; zoomed window came up with no pinstripes and
                                ; NO CLOSE OR MINIMIZE BOX - which wm_hit still
                                ; reports from geometry, so they were invisible
                                ; and clickable. wm_paint_all and wm_raise set
                                ; it; this was the one site that did not
    call wm_draw_win            ; then this window is the whole of it
    pop bp                      ; ...and wm_rz_paint preserves every register
    call menu_draw_bar          ; (the chrome may still have changed owner)
    call dock_paint
    jmp short .out

.dmg:                           ; --- otherwise: where it was, and where it is
    mov ax, [bx+W_X]
    cmp ax, [wm_rz_ox]
    jle .x1
    mov ax, [wm_rz_ox]
.x1:
    mov si, [bx+W_Y]
    cmp si, [wm_rz_oy]
    jle .y1
    mov si, [wm_rz_oy]
.y1:
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    mov dx, [wm_rz_ox]
    add dx, [wm_rz_ow]
    cmp cx, dx
    jge .x2
    mov cx, dx
.x2:
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    mov bx, [wm_rz_oy]
    add bx, [wm_rz_oh]
    cmp dx, bx
    jge .y2
    mov dx, bx
.y2:
    mov bx, si                  ; AX,BX,CX,DX = the union, shadow included
    call wm_paint_dmg
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; The zoom bank: the rect a zoomed window goes back to (SPEC.md 11.95).
; One entry per window SLOT, a side table rather than four more words in the
; record, because WIN_SIZE is what wm_idx2ptr multiplies by and every reader
; of a window pays for a wider record whether it zooms or not.
;
; ZR_W = 0 is "nothing banked", and it is a safe sentinel because a banked
; width is a live window's, which WMIN_W floors at 96. .bss arrives zeroed
; (the overlay note in kernel.asm), so a slot starts empty; wm_destroy clears
; it again, or a reused slot would restore to a stranger's rect.
; -----------------------------------------------------------------------------
ZR_X    equ 0
ZR_Y    equ 2
ZR_W    equ 4                   ; ...and the "nothing banked" sentinel
ZR_H    equ 6
ZR_SIZE equ 8

; -----------------------------------------------------------------------------
; wm_zoom - the Macintosh zoom box, on a double-clicked title bar
;           (SPEC.md 11.95)
;
; in:  BX = window ptr - resizable and not fullscreen, both the CALLER's to
;      check (ui.inc's title-bar branch is the only one); caller HOLDS the
;      gfx lock
; out: nothing (all registers preserved); the record carries the new rect and
;      the screen is repainted
;
; A TOGGLE between the two states the Macintosh names: the STANDARD state,
; which here is the whole desktop band, and the USER state, which is whatever
; the window was before it went there. Not wm_fullscreen (SPEC.md 11.2) and
; not an fsx bracket (SPEC.md 53) - the window keeps its border, its title
; bar, its boxes and its place in the z-order, so nothing is taken away that
; has to be given back, and everything that reads a window rect keeps reading
; what it always did.
;
; **Which state it is in is DERIVED, never tracked.** The test is whether the
; record already holds the standard rect; there is no "is zoomed" flag,
; because a flag would have to be cleared by every one of the four sites that
; write W_X and the several that write W_W/W_H, and a missed one is a window
; that answers wrong about itself. Deriving it also makes a DRAG the natural
; way to leave the standard state: a window moved off the band is simply not
; in it any more, and the next double-click zooms afresh, banking where the
; drag left it.
;
; The standard rect is wm_fit's clamp said forwards instead of backwards -
; full screen width, from the menu bar down to ONE PIXEL SHORT of the dock.
; That pixel is not a rounding choice: the drop shadow lives on row y+h and
; wm_dock_clear tests y+h with `jae`, so a frame flush against the strip makes
; every window later shown over this one pay a wm_dock_under pass (SPEC.md
; 11.90). A zoom is the one operation guaranteed to land on that boundary, so
; getting it wrong here would make the feature expensive for everything else
; on the screen.
;
; Three things it borrows rather than repeats:
;
;   - wm_snap_want (SPEC.md 11.94) for the WF_SNAP question. wm_snap_ax
;     itself cannot serve: it moves LEFT and re-tests the window's CURRENT
;     width, and at full screen width x = 7 always fails that test - so a
;     snapped window would zoom to x = 0 and lose the fast path it asked for.
;     Here x moves RIGHT to 7 and the width gives back exactly the seven
;     pixels x took, which is the one place a snap can be paid for.
;   - wm_ask_size, because this is a KERNEL-initiated resize and those always
;     ask (SPEC.md 11.1) - ONCE for the state being tested and once more for
;     the one being gone to. Growing, every negotiator accepts; SHRINKING
;     back can be refused, and that refusal is the feature working: apps/paint
;     will not crop artwork for a restore any more than it will for a drag of
;     the grow box, and it says so in its own toast. A refusal leaves the
;     window in the standard state with its bank intact, so the restore can be
;     asked for again once there is nothing to lose.
;   - wm_resize for the commit, which is both clamps, the WF_SNAP re-fit and
;     the repaint. The one thing it will not do is move a window UP or LEFT -
;     it only ever clamps a frame back down onto the screen - so the origin is
;     written here first and wm_resize then finds it already legal.
; -----------------------------------------------------------------------------
wm_zoom:
    KFZ 16                      ; the zoom/unzoom itself (SPEC.md 11.98)
    push ax
    push cx
    push dx
    push si
    xor ax, ax                  ; the standard state: x = the left edge...
    mov cx, [vid_pw]            ; ...and w = the whole live screen (SPEC 39.2)
    mov si, MBAR_H              ; ...y = under the bar, h = the desktop band,
    mov dx, [vid_dock_y0]       ; one pixel short of the dock - wm_fit's
    sub dx, MBAR_H + 1          ; clamp, for wm_fit's reason
%ifdef KERN_BIG
    call wm_disp_span           ; ...ALL FOUR of the DISPLAY this window is on
                                ; (SPEC.md 39.17.2), or a zoom would move it
                                ; to the other monitor - which the x half
                                ; alone once guaranteed only until the y half
                                ; wrote MBAR_H onto a BELOW-layout secondary
%endif
    call wm_snap_want
    jnc .nosnap
    call wm_flush_ck            ; ...spanning the SCREEN? Then x = 0 has no
    jc .nosnap                  ; left border and IS the aligned origin, so
                                ; the rect stands as it is (SPEC.md 11.95.2)
    add ax, 7                   ; WF_SNAP on a SECONDARY display (39.17.2),
    sub cx, 7                   ; where x = 0 is the boundary with the display
                                ; beside it rather than a screen edge: the
                                ; leftmost frame x whose CONTENT origin is
                                ; byte-aligned ON ITS DISPLAY - every display
                                ; origin is byte-aligned, so local and virtual
                                ; agree - and the width hands back exactly
                                ; what x took. (`mov ax,7` here was the
                                ; relocation: it overwrote the display origin
                                ; wm_disp_span just answered)
.nosnap:
    call wm_ask_size            ; ...as the window will have it (SPEC.md 11.1),
                                ; which is what makes the compare below hold
                                ; for a negotiator with a quantum of its own
    cmp [bx+W_X], ax            ; is the window already in the standard state?
    jne .go
    cmp [bx+W_Y], si
    jne .go
    cmp [bx+W_W], cx
    jne .go
    cmp [bx+W_H], dx
    jne .go

    ; --- it is: put the user state back --------------------------------------
    ; NOT through wm_resize, and that is the one asymmetry in this routine.
    ; wm_resize DERIVES a legal origin - it holds y + h to the dock row - which
    ; is right for an app sizing itself and wrong for replaying a rect the
    ; window manager itself last had, because ui_drag is deliberately looser:
    ; a window may be dragged until only its title bar is on screen, and one
    ; left more than half over the dock is left exactly there (SPEC.md 11.90).
    ; Put back through wm_resize, such a window came home 39 rows too high.
    ; The banked rect needs no clamping - every writer of it clamped already -
    ; and the negotiator's answer is trusted the same way ui_grow trusts it:
    ; the proposal was legal, so the reply is written as given.
    call wm_zoom_slot           ; SI -> this window's bank
    mov cx, [si+ZR_W]
    or cx, cx                   ; nothing banked - the window was CREATED this
    jz .out                     ; size and has never been anywhere else, so
                                ; there is nothing to go back to and nothing
                                ; is drawn.
                                ;
                                ; NOT `jcxz`, which is SHORT-ONLY on an 8086
                                ; and had 14 bytes of headroom left here
                                ; (displacement 113 of 127): the next thing
                                ; that grew this routine broke the BUILD, and
                                ; it did - a scheduler change nowhere near
                                ; this file was enough, because NASM's jump
                                ; optimiser reaches a different fixpoint when
                                ; anything shifts and the short jumps in
                                ; between resize with it. `.out` is four pops
                                ; and a ret, so nothing downstream reads the
                                ; flags this costs
    mov ax, [si+ZR_X]
    mov dx, [si+ZR_H]
    call wm_ask_size            ; preserves SI, which is why the bank can still
                                ; be read after it. The bank is NOT spent: a
                                ; refused shrink has to be re-askable, and a
                                ; zoom re-banks anyway
    ; Refused OUTRIGHT? Then the restore did not happen, and NOTHING moves -
    ; SPEC.md 11.1's rule that a refusal costs no repaint, plus the reason the
    ; origin does not travel on its own: a window is not "partly back" because
    ; it went three pixels down while keeping the size it was refused.
    ;
    ; The test is against what was ASKED and not against the record, and that
    ; distinction is the whole of it: a window DRAGGED while it was zoomed
    ; banks the standard SIZE with a different origin, so "the answer equals
    ; the record" is also exactly what a perfectly accepted restore of that
    ; window looks like. Asking the other question tells the two apart.
    cmp cx, [si+ZR_W]
    jne .rref
    cmp dx, [si+ZR_H]
    je .rgo                     ; we were given the size we asked for
.rref:
    cmp cx, [bx+W_W]            ; not that size - but if what came back is the
    jne .rgo                    ; size it ALREADY has, on both axes, then
    cmp dx, [bx+W_H]            ; nothing was given back at all. One axis is a
    je .out                     ; partial restore, and it travels
.rgo:
    push dx                     ; x is replayed and then CLAMPED, because on
    call wm_zoom_xmax           ; that partial path the size is not the one it
                                ; was banked with - a negotiator refusing to
                                ; shrink hands back the STANDARD width, and
                                ; the banked x plus that runs off the right
                                ; edge of its display (SPEC.md 39.17.2)
    cmp ax, dx
    jle .rxc
    mov ax, dx
.rxc:
    pop dx
    call wm_rz_bank             ; the standard state we are LEAVING - what the
                                ; restore reveals is all of it bar the rect
                                ; being replayed (SPEC.md 11.95.1)
    mov [bx+W_W], cx
    mov [bx+W_H], dx
    mov [bx+W_X], ax
    mov ax, [si+ZR_Y]           ; y is replayed and never clamped: what bounds
    mov [bx+W_Y], ax            ; it is the TITLE BAR being on screen, which is
                                ; independent of the height (SPEC.md 11.90)
    call wm_snap_win            ; ...and back onto a byte boundary (11.94),
                                ; which the x clamp above may have taken it off
    call wm_nat_bank            ; the user state is the natural one again - and
                                ; this path writes the record directly, so it
                                ; is not covered by wm_resize_nb's bank the way
                                ; .go below is (SPEC.md 39.11.2.1)
    call wm_usr_mark            ; ...and a zoom is a USER sizing it (11.100.5)
    call wm_rz_paint            ; the band it is vacating is most of the
    jmp short .out              ; screen - but only that band, not the whole
                                ; of it, and not the chrome (SPEC.md 11.95.1)
.go:
    ; --- it is not: bank where it stands, and go to the standard state -------
    push si                     ; the standard y, which wm_zoom_slot spends SI on
    call wm_zoom_slot
    call wm_rz_bank             ; ...for the REPAINT as well as for the user
                                ; state - and it has to happen here, before
                                ; the origin moves, or wm_resize would bank
                                ; the new x/y with the old w/h and the band
                                ; the window VACATED would never be redrawn
    push ax                     ; the standard x, which the bank writes over
    mov ax, [bx+W_X]
    mov [si+ZR_X], ax
    mov ax, [bx+W_Y]
    mov [si+ZR_Y], ax
    mov ax, [bx+W_W]
    mov [si+ZR_W], ax
    mov ax, [bx+W_H]
    mov [si+ZR_H], ax
    pop ax
    mov [bx+W_X], ax
    pop si                      ; ...and the standard y back
    mov [bx+W_Y], si            ; its DISPLAY's standard y (SPEC.md 39.17.2):
                                ; wm_resize's y clamp only ever pushes a window
                                ; UP OFF the dock, never down off the bar, so
                                ; this is the half it cannot do
    call wm_resize_nb           ; ...and this is the half it can (the bank is
                                ; ours, above)
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_zoom_slot - SI -> this window's entry in the zoom bank
; in:  BX = window ptr
; out: SI = wm_zoomr + index * ZR_SIZE; every other register preserved
; clobbers: flags
; -----------------------------------------------------------------------------
wm_zoom_slot:
    push ax
    call wm_ptr2idx             ; AL = index, AH = 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1                   ; x8: ZR_SIZE, and an 8086 has no shl by imm
    add si, wm_zoomr
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_destroy - free a window record and repaint everything
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Clears W_FLAGS (used + visible), resets the slot's wm_owner entry to 0xFF,
; removes the window's index from wm_zord (compacting the byte array over
; it, decrementing wm_zn), then repaints all. The record slot becomes
; reusable by wm_create.
; -----------------------------------------------------------------------------
wm_destroy:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wm_su_drop             ; the slot is about to be reusable, and a new
                                ; window in it would inherit this cache
    mov si, bx                  ; the rect it is vacating, banked before the
    call wm_win_rect            ; record is cleared out from under us
    test word [si+W_FLAGS], 2
    jnz .vis
    mov ax, 1                   ; already hidden: its pixels went at the
    xor bx, bx                  ; wm_hide, so an empty rect - the chrome is
    xor cx, cx                  ; all this owes (x1 > x2 is the empty test)
    xor dx, dx
    jmp short .bank
.vis:
    test word [si+W_FLAGS], WF_FULL
    jz .bank
    mov ax, [si+W_X]            ; fullscreen: its rect WITHOUT the shadow -
    mov bx, [si+W_Y]            ; the record still holds it, wm_fs_drop not
    mov cx, ax                  ; having run yet. A constant screen rect here
    add cx, [si+W_W]            ; would repaint the PRIMARY because a window
    dec cx                      ; on the other display went away (39.17.1)
    mov dx, bx
    add dx, [si+W_H]
    dec dx
.bank:
    mov [wm_hide_r], ax
    mov [wm_hide_r+2], bx
    mov [wm_hide_r+4], cx
    mov [wm_hide_r+6], dx
    mov bx, si
    call wm_fs_drop             ; a dying fullscreen window must release the
                                ; screen (SPEC.md 11.2)
    cmp [wm_clreq], bx          ; ...and a deferred close naming it (SPEC.md
    jne .noclreq                ; 75.2) goes with it, or the next tenant of
    mov word [wm_clreq], 0      ; this slot is closed by a stale pointer
.noclreq:
    mov word [bx+W_FLAGS], 0    ; free the record slot
    mov ax, bx                  ; AL = window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov si, ax                  ; orphan the slot's owner entry so a future
    mov byte [wm_owner+si], 0xFF; wm_create reuse starts unowned (SPEC.md 11)
    shl si, 1                   ; ...and its About handler with it, or a
    mov word [wm_about+si], 0   ; reused slot inherits a stranger's (12.2)
    mov word [wm_onsz+si], 0    ; ...and its size-changed handler, which is the
                                ; same rule again and the same index (11.98):
                                ; a stranger's near pointer called in a new
                                ; tenant's segment is not a wrong answer, it is
                                ; a jump into the middle of its image
    mov word [wm_onwk+si], 0    ; ...and its wake handler (SPEC.md 74.1), the
                                ; same rule a third time
    mov word [wm_onrc+si], 0    ; ...and its right-click handler (SPEC.md
                                ; 13.11), which ui_rdown reads for every
                                ; frontmost window a right press lands on -
                                ; so a stale one is a far call into a new
                                ; tenant's image, exactly as below
    mov word [wm_oncl+si], 0    ; ...and its close negotiator (SPEC.md 75.1),
                                ; a fourth - and here the rule is sharper than
                                ; elsewhere, because wm_ask_close reads this
                                ; word for EVERY window on its way out, so a
                                ; stale one is not a missed feature but a far
                                ; call into a new tenant's image on the first
                                ; click of its close box
    shr si, 1
    mov byte [wm_wkq+si], 0     ; ...and its "a wake is queued" flag, or the
    shl si, 1                   ; next tenant of this slot could never post one
    shl si, 1                   ; ...and the four BAND extents (SPEC.md
    mov word [wm_su_ext+si], 0  ; 11.96.11), which is the same rule a fourth
    mov word [wm_su_ext+si+2], 0; time: the fragment layout is a function of
                                ; them, so a new tenant that named no band has
                                ; its cache laid out for the last one's and
                                ; restores the wrong rows into the middle of
                                ; its own content. Their declaration says all
                                ; four zero is what every window has "by
                                ; construction rather than by initialisation",
                                ; and that is true of a VIRGIN slot only
    shl si, 1                   ; ...and its zoom bank, for the same reason:
                                ; a reused slot must not restore to the rect
                                ; the last tenant was zoomed out of (11.95).
    mov word [wm_zoomr+si+ZR_W], 0  ; the sentinel word is the whole of it
    mov word [wm_natr+si+NR_W], 0   ; ...and its natural rect, for the same
                                ; reason again (SPEC.md 39.11.2.1). The two
                                ; tables share a stride, so they share the
                                ; index SI already holds
    mov ch, 0
    mov cl, [wm_zn]
    jcxz .paint                 ; empty z-order: nothing to remove
    mov si, wm_zord
.find:
    cmp [si], al
    je .remove
    inc si
    loop .find
    jmp .paint                  ; not in z-order (cannot happen when used)
.remove:
    dec cx                      ; entries above us; compact them down a slot
    jcxz .count
.shift:
    mov ah, [si+1]
    mov [si], ah
    inc si
    loop .shift
.count:
    dec byte [wm_zn]
.paint:
    mov ax, [wm_hide_r]
    mov bx, [wm_hide_r+2]
    mov cx, [wm_hide_r+4]
    mov dx, [wm_hide_r+6]
    call wm_paint_dmg
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_destroy_seg - destroy every window whose callbacks live in segment AX
; in:  AX = a package region's segment (never 0); caller holds the gfx lock
; out: nothing (all registers preserved)
;
; The teardown sweep for windows the loader never bound: a package may
; wm_create more windows than the one it returned to the loader (SPEC.md
; 20.2), and those carry no instance record - only the W_SEG creator stamp
; ties them to the region. Called with the dying instance's I_SPTR
; (app_close_win / inst_task_die / the winless worker death) and with an
; aborted or dying load's region segment, always BEFORE the region is freed:
; an orphan still carries W_SEG/W_DISP into it, and the next repaint would
; far-call whatever claims that memory. main enforced this with the wm_wseg
; side table; the stamp moved into the record (W_SEG) and the sweep moves
; with it.
; -----------------------------------------------------------------------------
wm_destroy_seg:
    push ax
    push bx
    push cx
    mov bx, wm_wins
    mov cx, MAX_WIN
.scan:
    test word [bx+W_FLAGS], 1   ; a used record only - a free slot's W_SEG
    jz .next                    ; is a previous tenant's leftover
    cmp [bx+W_SEG], ax
    jne .next
    call wm_destroy             ; clears the used bit, so a slot can never
                                ; match twice
.next:
    add bx, WIN_SIZE
    loop .scan
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_show - make a window visible, raise it, and draw JUST it (SPEC.md 11.90)
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Showing a window is the one z-order change that REVEALS NOTHING. The
; window lands on top, so every pixel that changes is either its own or
; chrome it does not cover; everything already on screen either stays as it
; is or gets drawn over. That makes the full back-to-front pass avoidable,
; and the pass is expensive: a 640x480 planar desktop dither plus every
; visible window's frame and W_PAINT, to put one window on screen.
;
; Raising an ALREADY visible window is the same argument - it moves up, so
; it can only cover more - which is why this path does not care whether the
; visible bit was already set. wm_front on its own still does the full pass,
; because a raise there is reached from a click that may also have to
; UNCOVER something, and that is genuinely unknown.
;
; Four things change and all four are drawn here:
;   - the menu bar, because menu_activate just handed it to this window;
;   - the dock, because the instance that owns this window may be new;
;   - the OUTGOING front window's title bar, which loses its pinstripes and
;     its two boxes to us (wm_draw_title, drawn BEFORE us so we cover it);
;   - the window itself, drawn last and therefore on top.
;
; The desktop and its drive icons are deliberately not redrawn: showing a
; window cannot change them, and desk_zone_redraw (SPEC.md 26) already owns
; the one thing that can.
;
; **It no longer falls back at all.** A fullscreen window used to send both
; this and wm_front to the full pass, on the grounds that SPEC.md 11.2
; suppresses the chrome and none of the cheap path's reasoning survived that.
; It survives it exactly: a window shown OVER a fullscreen one reveals
; nothing, like any other, and a fullscreen window shown over everything
; reveals nothing because it covers the screen. The chrome is the only part
; that had to change, and wm_raise calls the bar and the dock anyway and lets
; each DECLINE for itself (wm_fs_vis, SPEC.md 30.3.2) rather than the
; caller repainting the world to avoid drawing it. Opening the Standard File
; dialog over a fullscreen player was a whole-screen repaint including that
; player's W_PAINT, twice - once to show the dialog and once to take it away.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; wm_lk / wm_unlk - THE DRAWING MUTEX, FOR THE THREE ENTRIES A PACKAGE REACHES
;                   WITH IT FREE (SPEC.md 11.101.2)
;
; `wm_show`, `wm_hide` and `wm_front` are API slots (0x0080/0x0088/0x0090), and
; a package's ENTRY PROC runs with the gfx lock free (SPEC.md 20.2) - which is
; exactly where a package shows the window it has just created. Three places in
; this tree already say these three take the lock themselves - gfxbench's
; header ("WM_SHOW/HIDE/FRONT take it themselves, so from inside a window
; callback they are a deadlock"), muptest's entry proc ("FREE (SPEC.md 20.2),
; unlike a callback") and kbm_ui's own comment - and none of them did. Every
; KERNEL caller took it for them, so the gap was invisible until a package used
; the API the way the SDK documents it.
;
; **What it costs is a save-under with the ARROW IN IT.** wm_show banks what
; the new window is about to cover (wm_su_precover, 11.96.16) and wm_hide banks
; what is going down; both read the screen through gfx_save, whose cur_unlazy
; spends a hide that only a LOCK HOLD ever promised (SPEC.md 7.1.4). With no
; hold there is no promise, the arrow is standing on the pixels being banked,
; and it is stored as somebody's window content. Nothing shows it at the time:
; a cache is only wrong when it is used, so the arrow appears when that window
; is next uncovered - a whole one, or the part of it the rect caught - and it
; stays, because nothing repaints what it did not damage. docs/FIELD-NOTES.md
; 34.1 is the report, and "the window that opens must be ABOVE the cursor" is
; the reader having found the precover rect from the outside.
;
; It is a RECURSIVE-AWARE take and not a bare gfx_lock, because every kernel
; caller already holds it and gfx_lock is not reentrant: ours is a no-op,
; anybody else's waits, free is taken. Unlike fprog's (12.8.4) this one may
; block - all three run on the UI task and never inside dsk_xfer's [sch_lock].
;
; in:  nothing;  out: wm_lk: AL = 1 we took it and owe wm_unlk, 0 it was ours.
;      wm_unlk: AL as wm_lk answered. Neither touches another register.
; -----------------------------------------------------------------------------
wm_lk:
    cmp byte [gfx_lock_flag], 0
    je wm_lk_take               ; free: take it
    push ax
    mov al, [sch_cur]
    cmp al, [gfx_lock_own]
    pop ax
    jne wm_lk_take              ; SOMEBODY ELSE'S: gfx_lock waits for them,
                                ; which is what a mutex is for
    xor al, al                  ; ours: the caller's hold covers all of it
    ret
wm_lk_take:
    call gfx_lock
    mov al, 1
    ret

wm_unlk:
    or al, al
    jz wm_unlk_out
    call gfx_unlock             ; ...and the arrow comes back on finished
wm_unlk_out:                    ; pixels, with the caches already clean
    ret

wm_show:
    push ax                     ; the caller's AX first: wm_lk answers in AL
    call wm_lk
    push ax
    call wm_show_b
    pop ax
    call wm_unlk
    pop ax
    ret

wm_show_b:
    CURLOG 'W'
    push ax
    push bx
    push bp
    push di
    mov di, bx                  ; DI = the window being shown
    call wm_top                 ; BX = the OUTGOING front window (0 if none).
    mov bp, bx                  ; BEFORE the visible bit goes on: wm_create
    mov bx, di                  ; already appended us to wm_zord, so once we
    test word [bx+W_FLAGS], 2   ; are visible wm_top answers with US
    jnz .onglass                ; ...and for the same reason this is the last
    call wm_su_precover         ; moment the windows we are about to land on
    or word [bx+W_FLAGS], 2     ; can be banked (SPEC.md 11.96.16). A window
%ifdef WM_ANIM                  ; ALREADY on the glass is skipped: what it
    call wm_an_open             ; covers is covered by IT, so a bank there
%endif                          ; would store this window's own pixels as
    jmp short .raise            ; somebody else's content
                                ;
                                ; SPEC.md 11.99: the zoom, BEFORE the window is
                                ; drawn - it is an outline over what is on the
                                ; screen now, and the window lands when it ends
.onglass:
    or word [bx+W_FLAGS], 2
%ifdef WM_ANIM
    mov byte [wm_an_armed], 0   ; ...AND A WINDOW ALREADY ON THE GLASS DOES NOT
                                ; ZOOM. 11.99 is about a window that APPEARS,
                                ; and this routine is deliberately the RAISE
                                ; path as well (see the header): four fronting
                                ; paths reach it with the window still on the
                                ; screen - an app relaunched at its instance
                                ; cap, File > Task Manager, a drive whose Disk
                                ; window is showing, a second os88ui_ask - and
                                ; PERFORMANCE.md Part 5 prices a raise of the
                                ; frontmost window at NO WINDOW AT ALL. The arm
                                ; is still SPENT, for wm_an_open's own .drop
                                ; reason: the next window that really does
                                ; appear must not inherit this one's tile
%endif
.raise:
    mov al, 1                   ; a newly visible window has no pixels on
    call wm_raise               ; screen: it is drawn WHOLE
    pop di
    pop bp
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_fs_vis - is a VISIBLE fullscreen window covering the screen?
; out: CF = 1 yes - the desktop, its drive zones, the dock and the menu bar
;      are all under it and must not be drawn (SPEC.md 11.2). CF = 0 no.
; clobbers: nothing else (flags)
;
; The one question four painters ask, and they used to ask it three different
; ways: wm_paint_all and wm_paint_dmg open-coded it, and the cheap raise path
; treated it as a reason to give up entirely.
; -----------------------------------------------------------------------------
wm_fs_vis:
    push bx
    mov bx, [wm_fs]
    or bx, bx
    jz .no
    test word [bx+W_FLAGS], 2
    jz .no
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .yes
    push ax                     ; ...and is it on the display the CHROME is
    call wm_disp_of             ; on? A fullscreen window covering the second
    or al, al                   ; monitor covers no bar, no dock and no drive
    pop ax                      ; column, and all four painters below have to
    jnz .no                     ; draw them (SPEC.md 39.17.1). `pop` leaves
%endif                          ; the flags alone
.yes:
    pop bx
    stc
    ret
.no:
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; wm_fs_zpos - where the fullscreen window sits in wm_zord (module internal)
; in:  SI = wm_zord, DI = one past its last entry; wm_fs_vis has said yes
; out: SI = its slot, or unchanged if it somehow is not in the order
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
wm_fs_zpos:
    push ax
    push bx
    push cx
    mov bx, [wm_fs]
    mov ax, bx                  ; AL = its window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; (AH = 0: the pointer is record-aligned)
    mov cx, di
    sub cx, si
    jcxz .out
    push si
.scan:
    cmp [si], al
    je .found
    inc si
    loop .scan
    pop si                      ; not in the order: walk the lot, as before
    jmp short .out
.found:
    add sp, 2
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_raise - the paint half of "this window is coming to the front"
; in:  BX = DI = the window (its visible bit already set), BP = the window
;      that was frontmost BEFORE this one moved (0 or == DI = there was none),
;      AL = 1 draw the window whole / 0 its title bar is all that changed /
;      2 draw it, but it was ALREADY ON THE GLASS, so only the part something
;      was sitting on has to be put back (SPEC.md 11.96.10). Only wm_front may
;      say 2: wm_show's window has no pixels on screen to keep.
;      Caller holds the gfx lock and has already passed wm_fast_ok.
; out: nothing; AX/BX/BP/DI clobbered (both callers saved them)
;
; Four things change when a window comes to the top and all four are drawn
; here, in this order:
;   - the menu bar, because menu_activate just handed it to this window;
;   - the dock, because the owning instance may be new and because the focus
;     cue moves with the front window (SPEC.md 30);
;   - the OUTGOING front window's title bar, which loses its pinstripes and
;     its two boxes to us - drawn BEFORE us, so we cover it;
;   - this window, drawn last and therefore on top.
;
; The desktop and its drive icons are deliberately not redrawn: coming to the
; front cannot change them, and desk_zone_redraw (SPEC.md 26) already owns the
; one thing that can.
; -----------------------------------------------------------------------------
wm_raise:
    mov byte [cur_shchk], 1     ; the window under a STANDING pointer just
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
                                ; changed (SPEC.md 7.2): cur_shape_pass gates
                                ; on the pointer having moved, and this is one
                                ; of the two ways the answer moves under it
    push ax                     ; a Disk window whose folder changed under it
    xor al, al                  ; is re-listed BEFORE a pixel is drawn, so
    call COLD_SEG:fm_focus_x    ; this raise puts the new listing up rather
    pop ax                      ; than the old one and then the new. AL = 0:
    jnc .listed                 ; the drawing below is ours, not its (22.8)
    mov al, 1                   ; and CF = 1 upgrades a title-bar raise to a
.listed:                        ; whole-window one - the CONTENT changed too, so
                                ; AL = 2 is downgraded here as well and a
                                ; re-listed window is drawn whole
    push ax
%ifndef REDRAWFULL
    cmp al, 2                   ; AL = 2: put back only what was COVERED
    jne .nocov                  ; (SPEC.md 11.96.10). Computed HERE, because
    call wm_cov_rect            ; wm_lift below makes this window the top of the
.nocov:                         ; z-order and the answer is then always "nothing"
%endif
    call menu_activate          ; BX owns the menu bar from here (SPEC.md 12)

    ; --- the OUTGOING front window is finished with first (SPEC.md 11.96.7) --
    ; This block used to sit below the three calls that follow it, and
    ; wm_su_take's own comment claimed it ran "before the window going over it
    ; has drawn". That was false whenever wm_dock_under actually drew: it is
    ; wm_dmg_wins, so a window on the dock strip is REDRAWN there - over this
    ; one - and the bank then read the covering window's pixels off the glass
    ; and stored them as this window's content. Nothing detected it, because a
    ; cache is only wrong when it is used: the ghost appeared on the NEXT
    ; restore, one operation later and somewhere else.
    ;
    ; So the bank happens while the window is genuinely still on top - before
    ; wm_lift, so the z-order still agrees with the glass as well.
    cmp bp, di
    je .lift                    ; already the front window: no outgoing one
    or bp, bp
    jz .lift
    test word [di+W_FLAGS], WF_FULL
    jnz .lift                   ; we are about to cover the whole screen: the
                                ; outgoing window's title bar is a strip drawn
                                ; only to be painted over
    xchg bp, di                 ; DI = the outgoing front, BP = the incoming
    call wm_draw_title          ; DI != BP now, so it loses its stripes
    call wm_grow_erase          ; ...and its grow box, which belongs to the
                                ; front window just as its two title-bar
                                ; boxes do (SPEC.md 11.1.1) - BEFORE the bank
                                ; below, so the cache holds the window as it
                                ; now looks
    call wm_su_take             ; ...and its content is banked HERE, which is
                                ; the last moment it is both correct and whole
    xchg bp, di
.lift:
    call wm_lift                ; the top of the z-order
    call menu_draw_bar          ; BOTH guard themselves on wm_fs_vis, and that
    call wm_dock_under          ; is the point: under a fullscreen window each
                                ; draws nothing AND records that its pixels are
                                ; being destroyed, which is the whole of how
                                ; the strips come back when it goes (SPEC.md
                                ; 30.3.2). This used to test wm_fs_vis HERE and
                                ; skip them - so on the one path that matters,
                                ; wm_fullscreen's own raise, neither module was
                                ; ever told, and quitting a 11.2 app left its
                                ; bar and a blank strip on screen for good
    mov bx, di
    mov bp, di                  ; we ARE the front window now
    pop ax
    or al, al
    jz .titleonly
%ifndef REDRAWFULL
    cmp al, 2
    jne .whole
    push bx                     ; BX IS THE WINDOW wm_draw_win is about to be
    push cx                     ; handed and a rect argument spends FOUR
    push dx                     ; registers - and this routine's contract lists
                                ; AX/BX/BP/DI as the clobbered ones, so CX and
                                ; DX are the CALLER's (SPEC.md 11.96.10.1).
                                ; wm_front saves exactly the four the contract
                                ; names, and ui_dispatch is holding the
                                ; mousedown point in CX/DX across it - so a
                                ; window dragged from BEHIND another one was
                                ; dropped at the covered rect's bottom-right
                                ; corner instead of under the pointer
    mov ax, [wm_cov_x1]
    mov bx, [wm_cov_y1]
    mov cx, [wm_cov_x2]
    mov dx, [wm_cov_y2]
    call wm_su_sub
    pop dx
    pop cx
    pop bx
.whole:
%endif
    jmp wm_draw_win             ; tail call
.titleonly:
    call wm_draw_title          ; DI == BP, so it gains its stripes...
    jmp wm_grow_paint           ; ...AND ITS GROW BOX (SPEC.md 11.1.1), which
                                ; this path alone forgot - BX is still the
                                ; window. wm_grow_erase takes the outgoing
                                ; window's off forty lines above and
                                ; wm_paint_dmg's promotion draws the incoming
                                ; one's; this is the third site and the
                                ; COMMONEST, because it is what a click on a
                                ; background window runs whenever wm_obscured
                                ; says nothing was on top of us. Drag a window
                                ; OFF a Disk window and call the Disk window
                                ; to the front: its content is already on the
                                ; glass so only the title bar is drawn, and it
                                ; came up with stripes, a close box, a
                                ; minimize box and no grow box. 11.91.2 is why
                                ; that was permanent rather than a flicker -
                                ; nothing repaints it afterwards - and wm_hit
                                ; reports AL = 4 from geometry alone, so the
                                ; corner stayed invisible-but-clickable

; -----------------------------------------------------------------------------
; wm_dock_clear - is the dock strip free of window pixels?
; in:  nothing
; out: CF = 0 = no visible window's frame or drop shadow reaches the dock
; clobbers: nothing else (flags)
;
; wm_fit keeps a window above the dock, but ui_grow's own clamp is looser
; (it stops at the screen edge), so a grown window CAN hang over it. Where
; that happens the dock has to be drawn before that window and the cheap
; path cannot order it, so it declines instead.
; -----------------------------------------------------------------------------
wm_dock_clear:
    push ax
    push bx
    push cx
    push si
    mov si, wm_zord
    mov cl, [wm_zn]
    xor ch, ch
    jcxz .clear
.scan:
    mov al, [si]
    inc si
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jz .next
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]            ; the drop shadow's own row, y+h
    cmp ax, [vid_dock_y0]
    jae .dirty
.next:
    loop .scan
.clear:
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.dirty:
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wm_hide - clear the visible bit and repaint what it uncovered
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Hiding DOES reveal, which is why this cannot take wm_show's path - but it
; reveals only inside the rect the window occupied, and every pixel outside
; that rect is already correct. wm_paint_dmg (SPEC.md 11.91) is that argument.
; The rect is taken BEFORE the visible bit drops, because wm_fs_drop can
; restore a fullscreen window's smaller windowed geometry on the way past and
; the pixels to repair are the ones that were actually lit.
; -----------------------------------------------------------------------------
wm_hide:
    push ax
    call wm_lk                  ; SPEC.md 11.101.2, and wm_su_take below is
    push ax                     ; exactly why: it BANKS the window going down
    call wm_hide_b
    pop ax
    call wm_unlk
    pop ax
    ret

wm_hide_b:
    push ax
    push bx
    push cx
    push dx
    push si
%ifdef REDRAWFULL
    call wm_su_drop             ; the reference build (SPEC.md 11.96.5)
%else
    push di                     ; MINIMIZING IS NOT COVERING (SPEC.md 11.96.1)
    mov di, bx                  ; and this used to DROP the cache for that
    call wm_su_take             ; reason. Taking one is strictly safer than
    pop di                      ; either dropping or keeping: it is banked
                                ; HERE, at the last moment the window's pixels
                                ; are correct and still on the glass, so no
                                ; older cache can survive a hide and a restore
                                ; puts back what went down (SPEC.md 11.96.5).
                                ; It must run before the visible bit drops
                                ; below - wm_su_take refuses an invisible
                                ; window, having no pixels to read
%endif
    mov si, bx
    call wm_win_rect            ; AX,BX,CX,DX = the rect it is vacating
    test word [si+W_FLAGS], WF_FULL
    jz .drop
    mov ax, [si+W_X]            ; ...so that is the damage, whatever wm_fs_drop
    mov bx, [si+W_Y]            ; is about to put back in the record - and it
    mov cx, ax                  ; is that window's DISPLAY, not the machine's
    add cx, [si+W_W]            ; screen (SPEC.md 39.17.1)
    dec cx
    mov dx, bx
    add dx, [si+W_H]
    dec dx
.drop:
    mov [wm_hide_r], ax
    mov [wm_hide_r+2], bx
    mov [wm_hide_r+4], cx
    mov [wm_hide_r+6], dx
    mov bx, si
    call wm_fs_drop             ; a hidden fullscreen window releases the
                                ; screen and re-shows windowed (SPEC.md 11.2)
    and word [bx+W_FLAGS], 0xFFFD
    mov ax, [wm_hide_r]
    mov bx, [wm_hide_r+2]
    mov cx, [wm_hide_r+4]
    mov dx, [wm_hide_r+6]
    call wm_paint_dmg
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_fs_drop - release the fullscreen latch if BX holds it (module internal)
; in:  BX = window ptr
; out: nothing (all registers preserved)
; If BX is [wm_fs]: restore the saved windowed geometry into the record,
; clear WF_FULL, zero the latch (SPEC.md 11.2). Does not repaint - every
; caller repaints right after.
; -----------------------------------------------------------------------------
wm_fs_drop:
    cmp bx, [wm_fs]
    jne .out
    push ax
    mov ax, [wm_fs_save]
    mov [bx+W_X], ax
    mov ax, [wm_fs_save+2]
    mov [bx+W_Y], ax
    mov ax, [wm_fs_save+4]
    mov [bx+W_W], ax
    mov ax, [wm_fs_save+6]
    mov [bx+W_H], ax
    and word [bx+W_FLAGS], 0xFFF7
    mov word [wm_fs], 0
    call wm_fit                 ; the saved rect was fitted to the screen this
                                ; window went fullscreen ON, and the adapter
                                ; can have changed under it since - wm_refit
                                ; deliberately leaves a WF_FULL window's bank
                                ; alone, so nothing else would catch it
                                ; (SPEC.md 39.11.2.1). A no-op on every other
                                ; path: an unchanged screen re-fits a rect that
                                ; already fitted it
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_snap - opt a window OUT of content-origin alignment, or back in
;           (public; API 0x0268, SPEC.md 11.94)
; in:  BX = window ptr, AL = 0 = do NOT keep me aligned / non-zero = do
; out: nothing - every register preserved, FLAGS INCLUDED (see below)
;
; THE CALL'S CONTRACT IS UNCHANGED AND ITS DEFAULT IS NOT. AL = 1 has always
; meant "keep my content origin on a multiple of 8" and still does; what moved
; is what a window that never calls at all gets, and that is now ALIGNED
; (SPEC.md 11.94). So this slot is an OPT-OUT: AL = 0 is the interesting
; direction now, and every historical AL = 1 caller is asking for something it
; already has. The stored bit is WF_NOSNAP and its sense is inverted to match -
; which is what lets wm_create's `mov word [bx+W_FLAGS], 1` be the whole of
; how the default is applied, with no template word and nothing to remember at
; the twelve places a window is made.
;
; Kept rather than retired, for two reasons. A window MAY opt back in - the
; §20.8 rule 4 alternative was withdrawing the slot, and a package that wants
; alignment for one of its windows and not another needs to say so. And
; snapping on demand is what makes the opt-IN direction still do work: it
; snaps immediately as well as clearing the flag, because the intended caller
; is an entry proc or a KD_INIT - before the window is shown, where moving it
; costs nothing and repainting is not owed. Calling it on a visible window is
; legal and moves it at the next paint the caller performs, exactly like
; wm_sizable's grow box.
;
; "My CONTENT origin", not "my W_X": the frame carries a 1px border, so
; wm_content returns W_X + 1 and the alignment a caller actually needs is one
; pixel off the frame's. Publishing the frame number would make every app
; rediscover that `inc ax`, and they would each get it wrong differently.
;
; UI-task context only, same as wm_sizable and for the same reason: every
; W_FLAGS writer runs on the UI task or under the gfx lock.
; -----------------------------------------------------------------------------
wm_snap:
    pushf                       ; FLAGS INCLUDED, and that is not politeness.
                                ; A package's ENTRY PROC returns CF to the
                                ; loader (SPEC.md 20.2) and calls this from
                                ; there, after wm_create's CF has already been
                                ; consumed by a branch - so the carry riding
                                ; in the flags at that point IS the return
                                ; value. wm_snap_ax exits on a `cmp` against
                                ; the screen width, which leaves CF set for
                                ; every window that fits; without this, asking
                                ; to be snapped aborted the launch. That used
                                ; to be a MONO-ONLY bug, the cmp being inside
                                ; a [vid_mono] gate that no longer exists - it
                                ; loaded perfectly on VGA and answered "Load
                                ; failed" on Hercules. The gate has gone
                                ; (wm_snap_want), so every adapter now reaches
                                ; that cmp and this line is the whole defence
    or al, al
    jz .clear
    and word [bx+W_FLAGS], ~WF_NOSNAP & 0xFFFF  ; AL != 0: "keep me aligned",
    call wm_snap_win                            ; which is already the default
    popf                                        ; - so this is a no-op for
    ret                                         ; every window that never
                                                ; opted out, and is kept
                                                ; because a window MAY opt
                                                ; back in
.clear:
    or word [bx+W_FLAGS], WF_NOSNAP ; AL = 0: OPT OUT. The window is left
    popf                            ; exactly where it stands - wherever that
    ret                             ; is, it is legal - and simply stops being
                                    ; moved by the four W_X writers

; -----------------------------------------------------------------------------
; wm_snap_win - put this window's content origin back on a multiple of 8
; in:  BX = window ptr, W_X already clamped by the caller
; out: W_X possibly moved; all registers preserved
; -----------------------------------------------------------------------------
wm_snap_win:
    push ax
    mov ax, [bx+W_X]
    call wm_snap_ax
    mov [bx+W_X], ax
%ifndef NOSIZESNAP
    mov ax, [bx+W_W]            ; ...AND THE SIZE, not only the origin (SPEC.md
    call wm_snap_w              ; 11.94.5). The x snap alone leaves the FAR
    mov [bx+W_W], ax            ; edge wherever the width put it, so seven
%endif                          ; widths in eight end the content mid-byte.
                                ; SECOND, because narrowing can never make a
                                ; snapped x illegal while moving x CAN change
                                ; wm_flush_ck's answer about the width
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_snap_want - does this window's content origin want to be 8-aligned?
; in:  BX = window ptr
; out: CF = 1 yes, CF = 0 no; every register preserved
;
; SPEC.md 11.94's GATE, on its own, because two places ask the question and
; only one of them can use wm_snap_ax to act on it: that routine moves a
; candidate x LEFT and re-tests the CURRENT width against the right edge,
; and wm_zoom (SPEC.md 11.95) is exactly the case where x wants to move
; RIGHT and the width is about to shrink to pay for it. One answer, two
; users, rather than a second copy of three tests that has to agree.
;
; EVERY ADAPTER, and there is no [vid_mono] test here on purpose. This gate
; was mono-only for most of the feature's life, on the premise that what
; snapping buys is font_run's single-store cell path (SPEC.md 6.1) - which is
; genuinely 1bpp-only - and that "on VGA it would cost 8px drag steps for
; nothing". The first half is true and the second does not follow from it, and
; it is measurably false: MODE 12h IS EIGHT PIXELS TO A BYTE, four planes of
; one bit each, so an unaligned 8x8 cell spills into a second framebuffer byte
; there exactly as it does on a Hercules - and font_char's .vram path pays for
; that spill with a second GC8 Bit Mask `out` and a second latched
; read-modify-write, once per row per cell. SPEC.md 6.1.4 is the arithmetic,
; and it was never a claim about one adapter: the reason unaligned cannot be
; made fast is that the cell stops owning its byte, which is as true of a
; plane as it is of a 1bpp framebuffer.
;
; Measured, cycle-accurate 4.77MHz 8088, tests/typebench's 40 keystrokes:
;
;   adapter    CHAR aligned   CHAR skewed 5   what alignment is worth
;   Hercules      1424 ms        1472 ms               3.4%
;   VGA           1013 ms        1108 ms               9.4%
;
; So the adapter the gate excluded gains 2.8x what the adapter it was written
; for gains. tests/gfxbench prices the primitive on VGA the same way: PAIR 10
; aligned 6984.93us against skewed 7855.51us, 12.5%.
;
; What is still mono-only is font_run's fast path, and that is a fact about
; font_run rather than about alignment - on VGA a run takes the gfx_fill +
; font_str fallback and every glyph in it is then byte-aligned, which is where
; the 9.4% is. Conflating the two is what kept this gate closed.
;
; WF_FULL is left alone: a fullscreen window is at x=0 with no border, so its
; content origin is 0 and already aligned (SPEC.md 11.2) - which is why the
; tracker has had the fast path all along without any of this.
; -----------------------------------------------------------------------------
wm_snap_want:
    test word [bx+W_FLAGS], WF_NOSNAP
    jnz .no                     ; ...opted out (SPEC.md 11.94's default is ON)
    test word [bx+W_FLAGS], WF_FULL
    jnz .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wm_flush - is this window's LEFT BORDER suppressed? (SPEC.md 11.95.2)
; in:  BX = window ptr
; out: CF = 1 yes - the content starts AT W_X and is W_W-1 wide;
;      every register preserved
;
; A border separates a window from what is beside it, and at the screen's left
; edge there is nothing beside it. So a snapped frame reaching from x = 0 to
; the last column of the screen does not draw one, and its content origin is
; therefore 0 - which is byte-aligned, which is the whole reason 11.95's zoom
; used to sit seven pixels in from the edge to buy.
;
; DERIVED, NEVER TRACKED, which is 11.95's own rule for its own reason: a flag
; would have to be cleared by every site that writes W_X or W_W, and a missed
; one is a window that answers wrong about itself. Asked again at each paint,
; a window dragged or resized off this geometry simply has its border back.
;
; wm_snap_want is in it for two separate reasons and neither is decoration. A
; WF_FULL window is ALREADY borderless and its content IS its frame (11.2), so
; adjusting it here would inset it twice; and a WF_NOSNAP window has opted out
; of having its geometry decided for it (11.94.1).
;
; IT COMPOSES WITH wm_snap_ax RATHER THAN FIGHTING IT, which is why that
; routine needed no change. At x = 0 the snapped position is 7, and wm_snap_ax
; already REFUSES to move a window there when 7 + W_W would hang it off the
; right edge - exactly the geometry named here. So a snapped window is either
; snappable to 7, in which case all four W_X writers have put it there and it
; is never at 0, or it spans the screen, in which case 0 is the only aligned x
; it has. wm_zoom's x = 0 therefore survives wm_resize_nb's wm_snap_win.
; -----------------------------------------------------------------------------
wm_flush:
    call wm_snap_want
    jnc .no                     ; opted out, or fullscreen: not ours to move
    push ax
    push cx
    mov ax, [bx+W_X]
    mov cx, [bx+W_W]
    call wm_flush_ck
    pop cx                      ; (pop writes no flags, so CF carries out)
    pop ax
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wm_bord - how wide is this window's SIDE border? (SPEC.md 11.95.3)
; in:  BX = window ptr
; out: AX = 1 normally, 0 for a window that spans the screen; every other
;      register preserved, flags clobbered
;
; **The value form of wm_flush, and the reason there has to be one.** The flag
; is read at the LEFT edge and the identical answer is wanted again at the
; RIGHT, several instructions later - by which time the arithmetic in between
; has destroyed CF. Every site that wanted both banked it by hand or asked
; twice, and what they all actually want is one sentence:
;
;     content = W_X + b  ..  W_X + W_W - 1 - b
;
; -----------------------------------------------------------------------------
wm_bord:
    call wm_flush
    mov ax, 0                   ; (mov writes no flag, so wm_flush still
    jc .out                     ; decides at the jump)
    inc ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_bordr - ...and the RIGHT-hand one on its own (SPEC.md 11.95.3)
; in:  BX = window ptr; out: AX = 0 or 1, every other register preserved
;
; The same answer as wm_bord, and this exists only so that `make NOFLUSHR=1`
; can put the right border back without touching the left - which is what
; makes the A/B measure 11.95.3 rather than the whole of 11.95.2.
; -----------------------------------------------------------------------------
wm_bordr:
%ifdef NOFLUSHR
    mov ax, 1
    ret
%else
    jmp wm_bord
%endif

; -----------------------------------------------------------------------------
; wm_disp_xw - the x origin and width of the DISPLAY a window is on
; in:  BX = window ptr
; out: AX = that display's first column, CX = its width; every other register
;      preserved
;
; **[vid_w] IS THE WHOLE DESKTOP AND A SCREEN EDGE IS NOT** (SPEC.md
; 11.95.2.1). Before §39.16 the two were the same number, so every question of
; the form "does this reach the edge of the screen" was asked against
; [vid_w] - and on an extended desktop those questions silently started asking
; about the far side of the OTHER monitor.
; -----------------------------------------------------------------------------
wm_disp_xw:
    xor ax, ax                  ; one display: the desktop IS the display, and
    mov cx, [vid_w]             ; this is the answer the whole tree had before
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .out
    push di
    call wm_disp_of             ; AL = the display this window is on
    call vid_ctx_ptr            ; DI = its record
    mov ax, [di+VID_CTX_VX]
    mov cx, [di+VID_CTX_CW]
    pop di
.out:
%endif
    ret

; -----------------------------------------------------------------------------
; wm_flush_ck - would a frame at AX of width CX reach both screen edges?
; in:  AX = candidate frame x, CX = candidate frame width
; out: CF = 1 yes; every register preserved
;
; The GEOMETRY half of wm_flush on its own, because wm_zoom (SPEC.md 11.95)
; has to ask it of a rect it has not committed to the record yet - and on a
; secondary display (39.17.2) the answer is no, x = 0 there being the boundary
; with the display beside it rather than a screen edge.
; -----------------------------------------------------------------------------
wm_flush_ck:
    push ax
    push cx
    push dx
    push di
    mov di, ax                  ; DI = the candidate x, DX = its width
    mov dx, cx
    call wm_disp_xw             ; AX = ITS DISPLAY's first column, CX = its
    cmp di, ax                  ; width - and NOT 0 and [vid_w], which on an
    jne .no                     ; extended desktop is the far side of the other
    add di, dx                  ; monitor (SPEC.md 11.95.2.1)
    add ax, cx
    cmp di, ax
    jb .no
    pop di
    pop dx
    pop cx
    pop ax
    stc
    ret
.no:
    pop di
    pop dx
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; wm_snap_ax - snap a candidate frame x for a WF_SNAP window
; in:  AX = candidate x (the caller's clamps already applied), BX = window ptr
; out: AX = the x to use; every other register preserved, flags clobbered
;
; The whole of SPEC.md 11.94's ARITHMETIC, in one place because four sites
; write W_X and every one of them has to agree; the gate above is the fifth
; site's half of it.
;
; The arithmetic moves LEFT, to the nearest x with x+1 a multiple of 8. Left
; because the caller has already clamped against the right edge and moving
; left cannot violate it. The one exception is x = 0..6, where there is no
; such x at or below it and the answer has to be 7 - the leftmost snapped
; position there is, so a snapped window cannot sit flush against the left
; edge. That is the visible cost of the feature and it is 7 pixels.
; -----------------------------------------------------------------------------
wm_snap_ax:
    call wm_snap_want
    jnc .out
    push cx
    mov cx, ax
    inc cx
    and cx, 0xFFF8              ; content left, rounded DOWN to a multiple of 8
    dec cx                      ; ...and back to a frame x
    jns .fits
    add cx, 8                   ; x was 0..6: the next one up is the only one
.fits:
    push cx                     ; only the x=0..6 case can move RIGHT, but the
    push dx                     ; right-edge test is cheap and it is the one
    push ax                     ; way this could hand back an illegal rect
    push word [bx+W_X]          ; **AND THE QUESTION IS ABOUT THE RECT BEING
    mov [bx+W_X], cx            ; PROPOSED** (SPEC.md 11.95.2.1). wm_zoom_xmax
                                ; asks wm_disp_of, which reads the RECORD - and
                                ; ui_drag calls this before it stores, because
                                ; its own "did it move" compare still needs the
                                ; old W_X. So a drop on the OTHER display was
                                ; measured against the display the window came
                                ; from and refused, and the drops that skip
                                ; ui_drag_phase - the ones where wm_land_fit
                                ; also moved the size - kept an unaligned
                                ; content origin for the session, which is
                                ; font_run's fast path gone on every text row.
                                ; Two instructions and a stack slot; the record
                                ; is put back below before anything else can
                                ; see it
    mov ax, cx
    mov cx, [bx+W_W]
    call wm_zoom_xmax           ; DX = the largest x this width may take on the
    pop word [bx+W_X]           ; window's OWN display (SPEC.md 39.17.2), and
    cmp ax, dx                  ; not on the whole extended desktop - where a
    pop ax                      ; MAXIMIZED window on the primary was allowed
    pop dx                      ; to walk 7px right and sat with the desktop's
    pop cx                      ; dither showing down its left (11.95.2.1).
    ja .pop                     ; `pop` writes no flags, so the cmp carries
    mov ax, cx                  ; would hang off: leave it unsnapped, and it
.pop:                           ; simply misses the fast path
    pop cx
.out:
    ret

; -----------------------------------------------------------------------------
; wm_snap_w - snap a frame width so the CONTENT is a whole number of bytes
; in:  BX = window ptr, AX = candidate frame width
; out: AX = the snapped width, or unchanged; every other register preserved
;
; SPEC.md 11.94.5, and wm_snap_ax's twin: it REFUSES rather than clamping - a
; width it may not snap comes back unchanged and the window simply misses the
; fast path. 11.94's own argument for the FIRST cell in a row - mode 12h is
; eight pixels to a byte, so an unaligned 8x8 cell spills into a second
; framebuffer byte there exactly as it does on a Hercules - is word for word
; the argument for the last one.
;
; **UP WHERE IT FITS, AND THAT IS 11.94.5.1 RATHER THAN A PREFERENCE.** This
; rounded DOWN unconditionally when it landed, which is wm_snap_ax's direction
; and looks like the matching half of it. It is not: x is the kernel's to move
; and W_W is a number the PACKAGE asked for. Eleven of the thirteen window
; templates in this tree carry a layout constant derived from that number -
; CP_CW, FD_W, the Browser's column table - and taking six columns off the
; content those constants describe does not make the package draw a narrower
; layout, it makes it draw the SAME layout six pixels past the content and
; over its own right border and drop shadow. The Control Panel shipped exactly
; that for a cycle: cp_page fills di..di+CP_CW-1, the clip stopped it at the
; shadow, and the window came up with no right edge down its content rows.
; Growing cannot do it: the surplus columns are white-filled by wm_draw_win
; and a package that never draws into them leaves them looking like the empty
; content they are.
;
; DOWN IS THE FALLBACK AND IT IS NOT DEAD CODE. A frame within 7px of the
; glass has nowhere up to go - TeXPad is 628 wide at x = 6, and 634 at the
; snapped x = 7 is one column past a 640 screen - and refusing outright costs
; that window its raise cache entirely, because a ragged right edge is
; exactly what 11.96.17's one-plane claim needs not to have. So UP is tried
; first and DOWN is taken when it will not fit. Every window that reaches
; the DOWN branch is one nearly as wide as the screen, and a window that wide
; cannot carry a fixed layout in this tree anyway: it would not fit a CGA. All
; three - TeXPad, the Browser and Word - size themselves from OSAPI_WM_GEOM.
;
; Refusals, each a defect it would otherwise be:
;
;   the screen   11.95.2's rect. Only the LEFT border is suppressed there, so
;                the content is W_W-1 - 639 on VGA, which is 7 mod 8 and NOT
;                aligned - and snapping it either way costs a maximized window
;                columns 11.95.2 was written to stop giving up. wm_zoom
;                declines its own +7 on this same wm_flush_ck
;   the floor    wm_min_axd's, not WMIN_W, and reachable only from the DOWN
;                branch: a window that declared more than 96 through 11.100.2
;                would otherwise be narrowed below its ask
;   one cell     a content under 8px wide has no aligned width to go DOWN to
;
; The right-edge test is not a refusal any more - it is the branch that
; chooses the direction.
; -----------------------------------------------------------------------------
wm_snap_w:
    call wm_snap_want
    jnc .out                    ; opted out, or fullscreen: not ours to size
    push cx
    push dx
    push ax
    mov cx, ax                  ; wm_flush_ck judges a CANDIDATE pair, which is
    mov ax, [bx+W_X]            ; what this width still is (SPEC.md 11.95.2)
    call wm_flush_ck
    pop ax                      ; (pop writes no flags, so CF carries out)
    jc .pop                     ; it spans the screen: the rect stands as it is
    mov dx, ax
    sub dx, 2                   ; content = W_W - 2, the flush case having just
    jbe .pop                    ; been refused; narrower than its own chrome
    add dx, 7                   ; UP FIRST, so the package is not handed LESS
    jc .down                    ; content than its template asked for. A width
    and dx, 0xFFF8              ; within 7 of 0xFFFF is nonsense the caller's
    add dx, 2                   ; low clamps cannot produce, and the `jc` is
                                ; one byte against having to argue that
    push ax                     ; THE RIGHT EDGE - wm_snap_ax's own test in the
    push cx                     ; other axis of this rect, and the one way
    mov ax, [bx+W_X]            ; growing could hand back an illegal one. It
    add ax, dx                  ; reads the SNAPPED x: wm_snap_win runs that
    push ax                     ; first, precisely so this test is the truth
    call wm_disp_xw             ; ...and it is THIS WINDOW'S DISPLAY's edge,
    add ax, cx                  ; not the extended desktop's, which is the far
    mov cx, ax                  ; side of the other monitor (SPEC.md 11.95.2.1)
    pop ax
    cmp ax, cx
    pop cx                      ; (pops write no flags)
    pop ax
    jbe .take                   ; it fits, and nothing is short
.down:
    mov dx, ax                  ; ...it does not, so DOWN - and only a window
    sub dx, 2                   ; within 7px of the glass ever gets here
    and dx, 0xFFF8
    jz .pop                     ; under one cell wide: nothing to snap to
    add dx, 2
    push ax                     ; THE FLOOR, which only the DOWN branch can
    push dx                     ; break: wm_min_axd's and not WMIN_W, so a
    mov ax, [vid_w]             ; window that declared more than 96 through
    xor dl, dl                  ; 11.100.2 is not narrowed below its ask. AX
    call wm_min_axd             ; is banked across it because the answer comes
    pop dx                      ; back there and .pop owes the CANDIDATE - a
    cmp dx, ax                  ; refusal used to hand back the MINIMUM
    pop ax                      ; instead (pop writes no flags)
    jb .pop
.take:
    mov ax, dx
.pop:
    pop dx
    pop cx
.out:
    ret


; -----------------------------------------------------------------------------
; wm_sizable - set or clear a window's WF_SIZABLE bit (public; API 0x0108)
; in:  BX = window ptr, AL = 0 clear / non-zero set
; out: nothing (all registers preserved)
; No repaint: the grow box appears at the next paint. UI-task context only
; (entry procs and window callbacks qualify, SPEC.md 11.1).
; -----------------------------------------------------------------------------
wm_sizable:
    or al, al
    jz .clear
    or word [bx+W_FLAGS], WF_SIZABLE
    ret
.clear:
    and word [bx+W_FLAGS], 0xFFFB
    ret

; -----------------------------------------------------------------------------
; wm_fullscreen - enter or leave fullscreen (public; API 0x0110, SPEC.md 11.2)
; in:  AL = 1 enter / 0 exit; BX = the caller's OWN window ptr either way;
;      caller HOLDS the gfx lock
; out: CF = 1 refused (the screen is another window's, entering or leaving),
;      else CF = 0. All registers preserved.
; Enter saves the windowed geometry, grows the frame to the whole screen,
; sets WF_FULL and raises (wm_front repaints under the held lock).
; Re-entering with the same window is a CF=0 no-op, as is exiting with no
; owner. Exit restores through wm_fs_drop and repaints.
;
; BX BINDS ON EXIT, and it did not used to: the exit path opened by
; overwriting BX with [wm_fs], so AL=0 dropped whatever window owned the
; screen rather than the caller's - a package could take another package's
; fullscreen down with its own window pointer, and the two would then
; disagree about who was fullscreen with nothing on screen saying so. Every
; caller in the tree already passed its own window (apps/artful, apps/missile
; twice, tests/gfxbench), so honouring it changed no shipped behaviour; what
; it removes is a way to be wrong that nothing could have reported.
; -----------------------------------------------------------------------------
wm_fullscreen:
    push ax
    push bx
    or al, al
    jz .exit
    mov ax, [wm_fs]
    or ax, ax
    jz .claim
    cmp ax, bx
    je .ok                      ; already ours: no-op
    stc                         ; someone else owns the screen
    jmp .out
.claim:
    mov [wm_fs], bx
    mov ax, [bx+W_X]
    mov [wm_fs_save], ax
    mov ax, [bx+W_Y]
    mov [wm_fs_save+2], ax
    mov ax, [bx+W_W]
    mov [wm_fs_save+4], ax
    mov ax, [bx+W_H]
    mov [wm_fs_save+6], ax
    call wm_fs_setrect          ; the display this window is primarily ON
                                ; (SPEC.md 39.17.1), which is the primary on a
                                ; machine with one
    or word [bx+W_FLAGS], WF_FULL
                                ; Raise it AND draw it WHOLE. Not wm_front:
                                ; that asks wm_obscured whether anything was
                                ; on top of us and draws only the title bar
                                ; when nothing was - a question about the rect
                                ; we just REPLACED. An app already frontmost
                                ; (the usual case - it is responding to its
                                ; own click) took the degenerate chrome-only
                                ; path and its new full-screen content was
                                ; never painted at all. wm_fast_ok used to
                                ; hide this by sending every fullscreen call
                                ; to wm_paint_all; nothing else in the tree
                                ; changes a window's geometry and then raises
                                ; it, which is why wm_front never had to know.
    push bp
    push di
    mov di, bx
    call wm_top                 ; BX = the outgoing front (may be us)
    mov bp, bx
    mov bx, di
    mov al, 1                   ; a new frame has no pixels on screen
    call wm_raise
    pop di
    pop bp
    jmp .ok
.exit:
    mov ax, [wm_fs]
    or ax, ax
    jz .ok                      ; nothing fullscreen: no-op
    cmp ax, bx                  ; ...and it has to be YOURS to put down. BX is
    je .drop                    ; the latch holder on this path, so wm_fs_drop
    stc                         ; is handed exactly what it was handed before
    jmp short .out
.drop:
    call wm_fs_drop             ; restore geometry, clear latch
    call wm_paint_all
.ok:
    clc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_front - raise a window to the front of the z-order (SPEC.md 11.90)
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Raising REVEALS NOTHING, for exactly the reason showing does not: the window
; moves up, so for every other window the covered area can only grow. What
; changes is this window's own pixels where something used to sit on it, the
; two title bars that trade the pinstripes, and the chrome - never anything
; underneath. So this shares wm_show's path rather than repainting the world.
;
; How much of THIS window has to be drawn is the one extra question, and
; wm_obscured answers it - asked BEFORE wm_lift, while the z-order still says
; what was on top of us. Nothing was: only the title bar changed, so only the
; title bar is drawn. That is the common case, because a click on a background
; window's title bar comes through here.
;
; Raising a window that is already frontmost repaints no window at all. It is
; not a no-op - menu_activate, the bar and the dock still run, because the
; caller may be re-asserting ownership - but the window itself is untouched.
;
; Raising a window that is not visible has no pixels to raise: wm_show is the
; entry point for that, and this declines to the full pass rather than
; silently drawing a hidden window.
; -----------------------------------------------------------------------------
wm_front:
    push ax
    call wm_lk                  ; SPEC.md 11.101.2
    push ax
    call wm_front_b
    pop ax
    call wm_unlk
    pop ax
    ret

wm_front_b:
    test word [bx+W_FLAGS], 2
    jz .slow

    push ax
    push bx
    push bp
    push di
    mov di, bx
    call wm_obscured            ; BEFORE wm_lift: was anything on top of us?
    mov al, 0
    jnc .titled
    mov al, 2                   ; yes: whatever it covered has to come back -
                                ; and only that, which wm_cov_rect works out
                                ; from the same z-order this just asked about
                                ; (SPEC.md 11.96.10). This window was on the
                                ; glass a moment ago (the visible test above),
                                ; which is what separates it from wm_show's
                                ; AL = 1 - a window with no pixels on screen
.titled:
    push ax
    call wm_top                 ; BX = the outgoing front (may be us)
    mov bp, bx
    mov bx, di
    pop ax
    cmp bp, di
    jne .raise
    xor al, al                  ; already frontmost: not even the title bar
    mov bp, di                  ; changes - but wm_raise still owes the bar
.raise:                         ; and the dock, so it is not skipped
    cmp bp, di
    jne .go
    or al, al
    jnz .go
    call menu_activate          ; the degenerate case: chrome only
    call wm_lift
    call wm_fs_vis              ; ...and under a fullscreen window, not even
    jc .out                     ; that
    call menu_draw_bar
    call wm_dock_under
    jmp short .out
.go:
    call wm_raise
.out:
    pop di
    pop bp
    pop bx
    pop ax
    ret
.slow:
    call menu_activate          ; BX owns the menu bar from here (SPEC.md 12)
    call wm_lift
    jmp wm_paint_all            ; tail call; same register contract

; -----------------------------------------------------------------------------
; wm_lift - move a window to the top of the z-order, drawing nothing
; in:  BX = window ptr
; out: nothing (all registers preserved)
;
; Split out of wm_front so wm_show can reorder without committing to the
; full repaint that used to be welded to it.
; -----------------------------------------------------------------------------
wm_lift:
    push ax
    push cx
    push si
    mov ax, bx                  ; AL = window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    jcxz .out                   ; empty z-order: nothing to reorder
    mov si, wm_zord
.find:
    cmp [si], al
    je .lift
    inc si
    loop .find
    jmp short .out              ; not found (cannot happen for a used window)
.lift:
    dec cx                      ; entries above us; shift them back one slot
    jcxz .place
.shift:
    mov ah, [si+1]
    mov [si], ah
    inc si
    loop .shift
.place:
    mov [si], al                ; and we land on top
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_cursor - the picture the pointer wears over THIS window's content
; in:  BX = window ptr, AL = CUR_*SH (SPEC.md 7.2)
; out: nothing (all registers preserved)
;
; The shape rides in W_FLAGS' HIGH BYTE, so no record grew and no template
; changed - and wm_create's `mov word [bx+W_FLAGS], 1` and wm_destroy's
; `mov word [bx+W_FLAGS], 0` already zero it, which is CUR_ARROWSH. A package
; that never asks gets the arrow because the byte is 0, not because anything
; put it there; and a package that dies with a crosshair set cannot leave one
; behind, because its record is zeroed on the way out and cur_shape_pass
; re-asks on the next UI pass.
;
; THE BYTE IS SHARED: bits 0..6 are the shape and bit 7 is WF_STALE, so the
; store is a READ-MODIFY-WRITE under the flags already saved above. A whole-byte
; store was the same bit twice - a stale Disk window wore the crosshair, and
; Missile Command asking for one marked itself stale and could never be
; banked.
;
; It does not draw. The pointer may not even be over this window, and the one
; place that knows is cur_shape_pass - so this stores and posts, exactly as
; every other "the answer may have changed" site in the tree does.
; -----------------------------------------------------------------------------
wm_cursor:
    pushf                       ; FLAGS INCLUDED, and wm_snap above is the
                                ; whole reason this line is here rather than
                                ; being learned again: a package's ENTRY PROC
                                ; returns CF to the loader (SPEC.md 20.2) and
                                ; sets its cursor from there, after
                                ; wm_create's CF has been consumed by a
                                ; branch - so the carry riding in the flags at
                                ; that point IS the return value. The `cmp`
                                ; below leaves CF SET for every shape that is
                                ; legal, so without this, asking for a
                                ; crosshair aborted the launch and the file
                                ; row just sat there selected
    push ax
    cmp al, CUR_NSHAPE
    jae .out                    ; refuse a shape nobody ships HERE as well as
                                ; in cur_shape_set: a bad byte written into
                                ; the record would be re-read every pass
    cli                         ; the pushf above is this section's other half:
                                ; wm_cursor runs on a PACKAGE task while
                                ; wm_su_stale ORs WF_STALE into the same word
                                ; from the UI task, so the read-modify-write
                                ; below cannot be interruptible
    mov ah, [bx + W_FLAGS + 1]
    and ah, WF_HIBITS           ; keep the kernel's BITS, drop the old shape -
                                ; three of them live in this byte now, and
                                ; WF_HIBITS is the one place they are named
    or al, ah
    mov [bx + W_FLAGS + 1], al
    mov byte [cur_shchk], 1
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
.out:
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_top - find the frontmost visible window
; in:  nothing
; out: BX = window ptr, or 0 if no window is visible
; -----------------------------------------------------------------------------
wm_top:
    push ax
    push cx
    push si
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    add si, cx
    jcxz .none
.next:
    dec si
    mov al, [si]
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jnz .done
    loop .next
.none:
    xor bx, bx
.done:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_hit - find the topmost visible window containing a point
; in:  CX = x, DX = y
; out: BX = window ptr (0 if none)
;      AL = 0 content, 1 title bar, 2 close box, 3 minimize box
;
; AL=2/AL=3 are reported only when BX is the frontmost visible window - the
; only one with the boxes drawn; the same regions on any other window are
; title bar (AL=1).
; -----------------------------------------------------------------------------
wm_hit:
    push si
    push di
    push bp
    push ax                     ; AH is preserved; AL patched on the way out
    mov al, [wm_zn]
    mov ah, 0
    mov si, wm_zord
    add si, ax                  ; walk front to back
    mov di, 1                   ; 1 until the first visible window is passed
.next:
    cmp si, wm_zord
    je .none
    dec si
    mov al, [si]
    call wm_idx2ptr             ; BX = candidate
    test word [bx+W_FLAGS], 2
    jz .next                    ; hidden windows don't count for anything
    mov bp, di                  ; BP = "candidate is the frontmost visible"
    xor di, di
    mov ax, [bx+W_X]            ; point-in-frame-rect test
    cmp cx, ax
    jl .next
    add ax, [bx+W_W]
    cmp cx, ax
    jge .next
    mov ax, [bx+W_Y]
    cmp dx, ax
    jl .next
    add ax, [bx+W_H]
    cmp dx, ax
    jge .next
    test word [bx+W_FLAGS], WF_FULL
    jnz .plain                  ; fullscreen: no chrome - every point is
                                ; content (SPEC.md 11.2)
    mov ax, dx                  ; hit: AX = dy within the frame
    sub ax, [bx+W_Y]
    cmp ax, TITLE_H
    jge .content
    test bp, bp                 ; the boxes exist on the frontmost only
    jz .title
    cmp ax, WM_BOX_Y0           ; both boxes occupy rows W_Y+4 .. W_Y+14
    jl .title
    cmp ax, WM_BOX_Y1
    jg .title
    mov ax, [bx+W_X]            ; close box columns W_X+8 .. W_X+18
    add ax, WM_BOX_X0
    cmp cx, ax
    jl .minbox
    add ax, WM_BOX_W
    cmp cx, ax
    jg .minbox
    mov al, 2
    jmp .out
.minbox:
    mov ax, [bx+W_X]            ; minimize box: W_X+W_W-19 .. W_X+W_W-9
    add ax, [bx+W_W]            ; (the close box mirrored, SPEC.md 11)
    sub ax, WM_BOX_X0 + WM_BOX_W + 1
    cmp cx, ax
    jl .title
    add ax, WM_BOX_W
    cmp cx, ax
    jg .title
    mov al, 3
    jmp .out
.title:
    mov al, 1
    jmp .out
.content:
    test bp, bp                 ; grow box: frontmost + WF_SIZABLE only
    jz .plain                   ; (SPEC.md 11.1; WF_FULL was peeled off
    test word [bx+W_FLAGS], WF_SIZABLE ; above)
    jz .plain
    mov ax, [bx+W_X]            ; 13x13 rect: cols W_X+W_W-14 .. W_X+W_W-2
    add ax, [bx+W_W]
    sub ax, 14
    cmp cx, ax
    jl .plain
    add ax, 12
    cmp cx, ax
    jg .plain
    mov ax, [bx+W_Y]            ; rows W_Y+W_H-14 .. W_Y+W_H-2
    add ax, [bx+W_H]
    sub ax, 14
    cmp dx, ax
    jl .plain
    add ax, 12
    cmp dx, ax
    jg .plain
    mov al, 4
    jmp .out
.plain:
    mov al, 0
    jmp .out
.none:
    xor bx, bx
    mov al, 0
.out:
    mov bp, sp                  ; patch AL into the saved AX, keeping AH
    mov [bp], al
    pop ax
    pop bp
    pop di
    pop si
    ret

; 1 << n for a window index - MAX_WIN is 12, an 8086 has no variable shift
; worth the code, and this is read once per window per damage repaint.
wm_bit1: dw 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048

; -----------------------------------------------------------------------------
; wm_win_rect - a window's occupied rect, drop shadow included
; in:  SI = window ptr
; out: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; clobbers: the four outputs only
; -----------------------------------------------------------------------------
wm_win_rect:
    mov ax, [si+W_X]
    mov bx, [si+W_Y]
    mov cx, ax
    add cx, [si+W_W]
    mov dx, bx
    add dx, [si+W_H]
    test word [si+W_FLAGS], WF_FULL
    jz .out
    dec cx                      ; fullscreen draws no shadow (SPEC.md 11.2)
    dec dx
.out:
    ret

; -----------------------------------------------------------------------------
; wm_dmg_hit - does the rect in AX,BX,CX,DX overlap the damage rect?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive); [wm_dmg_*] set
; out: CF = 1 they overlap; all registers preserved
; -----------------------------------------------------------------------------
wm_dmg_hit:
    cmp ax, [wm_dmg_x2]
    jg .no
    cmp cx, [wm_dmg_x1]
    jl .no
    cmp bx, [wm_dmg_y2]
    jg .no
    cmp dx, [wm_dmg_y1]
    jl .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wm_dmg_vacate - the rect a moved window is LEAVING (SPEC.md 11.91.2)
; in:  AX = x1, BX = y1, CX = x2, DX = y2 - where the window WAS, drop shadow
;      included; SI = the window
; out: nothing (all registers preserved)
;
; A window that MOVED damages the union of where it was and where it is, and
; wm_dmg_wins marked everything overlapping that union - so dragging a window
; ACROSS another one redrew the one underneath, although the only ground given
; up was a strip nowhere near it.
;
; The rect it is LEAVING is the whole of where a pixel can have gone stale.
; Everywhere else in the damage, one of two things is true and both are right:
; the moved window writes the pixel (it is on top, so its value is the answer),
; or nothing in this pass writes it at all - wm_dmg_gray subtracts every
; visible window (11.91.1), so the dither never reaches inside one - and the
; window underneath keeps the pixel it already had.
;
; It is a superset of what is actually uncovered - old MINUS new is an L and
; this is the whole of old - which costs a repaint on a short drag and is the
; safe direction. The caller owes one fact: the window is going to be drawn in
; this pass. ui_drag can say it, because a drag is only reached after a raise.
; -----------------------------------------------------------------------------
wm_dmg_vacate:
    mov [wm_dmg_st1], ax
    mov [wm_dmg_sy1], bx
    mov [wm_dmg_st2], cx
    mov [wm_dmg_sy2], dx
    mov [wm_dmg_stwin], si
    ret

; -----------------------------------------------------------------------------
; wm_dmg_bit - the wm_dmg_mask/wm_dmg_shad bit for window SI
; in:  SI = window ptr
; out: AX = its bit; every other register and the flags' usefulness are the
;      caller's - CX and DI are preserved because the marking pass's inner
;      loop holds live values in both
; -----------------------------------------------------------------------------
wm_dmg_bit:
    push cx
    push di
    mov ax, si
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; (AH = 0: a window ptr is record-aligned)
    xor ah, ah
    mov di, ax
    shl di, 1
    mov ax, [wm_bit1+di]
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; wm_dmg_shadowed - does the damage rect reach this window's DROP SHADOW?
; in:  AX,BX,CX,DX = its occupied rect (wm_win_rect: shadow included), SI = it;
;      the caller has already established that the rect overlaps the damage
; out: CF = 1 = the shadow is inside the damage. All registers preserved.
;
; SPEC.md 11.91.4. The shadow is the L at (+1,+1): the column x+w, which is
; CX, and the row y+h, which is DX. Given an overlap the caller has already
; proved, the damage reaches one of them exactly when it extends to or past
; that column or that row - two compares, and the direction is the safe one,
; since claiming a shadow that was not dithered costs two drawing calls and
; missing one leaves a black line eaten out of a window nobody touched.
; -----------------------------------------------------------------------------
wm_dmg_shadowed:
    test word [si+W_FLAGS], WF_FULL
    jnz .no                     ; fullscreen draws none (SPEC.md 11.2), and
                                ; wm_win_rect has shrunk the rect to say so
    cmp [wm_dmg_x2], cx
    jge .yes
    cmp [wm_dmg_y2], dx
    jge .yes
.no:
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; wm_dmg_stale - can any pixel of this window have gone stale?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (the window's rect); wm_dmg_hit has
;      already said it overlaps the damage
; out: CF = 1 = no: nothing of it was uncovered, so it owes nothing.
;      CF = 0 = mark it. All registers preserved.
;
; SPEC.md 11.91.2, and it asks TWO questions. Did this window overlap the rect
; the moved window LEFT - if not, nothing of it can be stale. And if it did,
; is that overlap now back under the window at its NEW position - if so, every
; one of those pixels is written again by the move and nothing is exposed.
; The first test alone was the first cut, and on a four-window desktop the
; second is where most of the remaining waste was: a short drag leaves the
; window it was sitting on entirely back underneath.
;
; The second test is against the moved window's FRAME and not the rect
; wm_win_rect answers with. wm_draw_win writes every pixel of the frame, while
; the drop shadow is two LINES - so the occupied box holds two corners,
; (x+w, y) and (x, y+h), that nothing writes, and claiming those would strand
; the old shadow's pixel on a rightward drag. Under-claiming by two lines
; marks a window that only sat under a shadow: the safe direction.
;
; With nothing armed the answer is always "mark", which is what every caller
; but ui_drag and ui_grow gets and what this pass has always done.
; -----------------------------------------------------------------------------
wm_dmg_stale:
%ifdef REDRAWFULL
    clc                         ; the reference build (SPEC.md 11.91.2): mark
    ret                         ; against the whole damage rect, as this pass
%endif                          ; did before the vacated rect existed
    cmp word [wm_dmg_stwin], 0
    je .mark
    cmp ax, [wm_dmg_st2]        ; ...did it overlap where the window WAS?
    jg .clean
    cmp cx, [wm_dmg_st1]
    jl .clean
    cmp bx, [wm_dmg_sy2]
    jg .clean
    cmp dx, [wm_dmg_sy1]
    jl .clean

    ; It did. Is that overlap wholly back under the window's NEW frame? Then
    ; the move re-covered every pixel of it and nothing is exposed.
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [wm_dmg_stwin]
    cmp ax, [wm_dmg_st1]        ; clip this window to the vacated rect: that
    jge .ox1                    ; intersection is the only part of it the
    mov ax, [wm_dmg_st1]        ; move can have touched
.ox1:
    cmp bx, [wm_dmg_sy1]
    jge .oy1
    mov bx, [wm_dmg_sy1]
.oy1:
    cmp cx, [wm_dmg_st2]
    jle .ox2
    mov cx, [wm_dmg_st2]
.ox2:
    cmp dx, [wm_dmg_sy2]
    jle .oy2
    mov dx, [wm_dmg_sy2]
.oy2:
    cmp ax, [si+W_X]            ; ...and ask whether THAT is inside the new
    jl .exposed                 ; FRAME - x .. x+w-1, y .. y+h-1
    cmp bx, [si+W_Y]
    jl .exposed
    push ax
    mov ax, [si+W_X]
    add ax, [si+W_W]
    dec ax
    cmp cx, ax
    pop ax
    jg .exposed
    push ax
    mov ax, [si+W_Y]
    add ax, [si+W_H]
    dec ax
    cmp dx, ax
    pop ax
    jg .exposed
    pop si                      ; covered again: nothing of it is showing
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.exposed:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.mark:
    clc
    ret
.clean:
    stc
    ret

; -----------------------------------------------------------------------------
; wm_dmg_add - grow the damage rect to cover AX,BX,CX,DX as well
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_dmg_add:
    cmp ax, [wm_dmg_x1]
    jge .x1
    mov [wm_dmg_x1], ax
.x1:
    cmp bx, [wm_dmg_y1]
    jge .y1
    mov [wm_dmg_y1], bx
.y1:
    cmp cx, [wm_dmg_x2]
    jle .x2
    mov [wm_dmg_x2], cx
.x2:
    cmp dx, [wm_dmg_y2]
    jle .y2
    mov [wm_dmg_y2], dx
.y2:
    ret

; -----------------------------------------------------------------------------
; wm_win_hit - do two windows' occupied rects overlap?
; in:  SI = window A, DI = window B
; out: CF = 1 they overlap; all registers preserved
; Both rects include the 1px drop shadow, exactly as wm_obscured tests them.
; -----------------------------------------------------------------------------
wm_win_hit:
    push ax
    push dx
    mov ax, [si+W_X]            ; A.left > B.right?
    mov dx, [di+W_X]
    add dx, [di+W_W]
    cmp ax, dx
    jg .no
    mov ax, [si+W_X]            ; A.right < B.left?
    add ax, [si+W_W]
    cmp ax, [di+W_X]
    jl .no
    mov ax, [si+W_Y]            ; A.top > B.bottom?
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    cmp ax, dx
    jg .no
    mov ax, [si+W_Y]            ; A.bottom < B.top?
    add ax, [si+W_H]
    cmp ax, [di+W_Y]
    jl .no
    pop dx
    pop ax
    stc
    ret
.no:
    pop dx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; wm_dmg_union - grow a rect to cover a window's occupied rect as well
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive), SI = window ptr
; out: the same four, grown; SI preserved
;
; For the one caller that has to name two rects at once: ui_drag knows where
; the window was and the record knows where it is, and wm_paint_dmg wants one
; rect. Separate from wm_dmg_add because that one edits the armed damage rect
; and this runs BEFORE there is one.
; -----------------------------------------------------------------------------
wm_dmg_union:
    push di
    mov di, [si+W_X]
    cmp di, ax
    jge .x1
    mov ax, di
.x1:
    mov di, [si+W_Y]
    cmp di, bx
    jge .y1
    mov bx, di
.y1:
    mov di, [si+W_X]
    add di, [si+W_W]            ; the drop shadow's own column
    cmp di, cx
    jle .x2
    mov cx, di
.x2:
    mov di, [si+W_Y]
    add di, [si+W_H]
    cmp di, dx
    jle .y2
    mov dx, di
.y2:
    pop di
    ret

; -----------------------------------------------------------------------------
; wm_paint_chrome - the dock and the menu bar, and nothing else
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; What is left to draw when a window that was ALREADY hidden goes away: its
; pixels left the screen at the wm_hide, so nothing is revealed - but the dock
; loses a tile and the menu bar may lose its owner. That is the second half of
; closing a task-owned app (SPEC.md 29), where the close box hides and the
; worker destroys a moment later; the destroy used to repaint the whole screen
; a second time for two strips' worth of change.
;
; The dock strip is drawn UNDER windows (SPEC.md 30), which wm_dock_under
; owns: it puts back whatever window is sitting on the pixels it just drew,
; and does nothing at all when the strip did not change.
; -----------------------------------------------------------------------------
wm_paint_chrome:
    call wm_dock_under
    jmp menu_draw_bar           ; tail call

; -----------------------------------------------------------------------------
; wm_paint_dmg - repaint only what a rectangle's worth of change touched
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive; may hang off the screen)
;      caller holds the gfx lock
; out: nothing (all registers preserved)
;
; The counterpart to wm_show's argument (SPEC.md 11.90). SHOWING a window
; reveals nothing, so it costs one window; HIDING, destroying or MOVING one
; does reveal - but only inside the rect it vacated, and everything outside
; that rect is already correct on screen. A window on the right of the screen
; therefore does not redraw because a window on the left closed.
;
; What is drawn, in wm_paint_all's order so the layering is identical:
;   1. the desktop dither, clipped to the damage rect;
;   2. every drive zone the rect touches, whole (SPEC.md 26);
;   3. the dock and the menu bar, always - both carry state that a hide or a
;      destroy just changed (a tile leaves, or the focus cue moves);
;   4. every window that needs it, back to front.
;
; "Needs it" is a two-part rule, and the second part is what makes it safe.
; A window is marked if it overlaps the damage rect - and ALSO if it overlaps
; a window already marked below it, because that window is redrawn WHOLE and
; would otherwise paint over this one. Marking runs bottom-to-top, so one
; pass reaches the whole transitive closure.
;
; A touched drive zone is folded into the rect before the marking pass rather
; than special-cased in it: it is drawn whole, so a window over it has to be
; redrawn, and growing the rect is what makes the marking notice. The DOCK is
; not folded in, and that is not symmetry for its own sake - the strip is
; full width, so a rect grown to it erases the whole width for the damage's
; entire height, which took the drive icons out from under a window that
; merely reached the bottom of the screen. It is a per-window test instead.
; -----------------------------------------------------------------------------
wm_paint_dmg:
    mov byte [cur_shchk], 1     ; ...and the other way (SPEC.md 7.2): a window
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
                                ; hidden, destroyed, dragged or grown out from
                                ; under a pointer that never moved. Every one
                                ; of those pays a repaint through here, which
                                ; is why this is the second store and not the
                                ; fifth
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov byte [wm_dmg_fs], 0     ; a visible fullscreen window covers the
    mov byte [wm_dmg_dk], 0     ; desktop, its drive zones, the dock and the
    call wm_fs_vis              ; bar - so none of those is drawn, and the
    jnc .clamp                  ; window pass is the whole of the work
    mov byte [wm_dmg_fs], 1
    call dock_paint             ; ...drawn, but still TOLD: both early-out on
    call menu_draw_bar          ; wm_fs_vis and mark themselves owed, so the
                                ; repaint after the window goes puts them back
                                ; (SPEC.md 30.3.2). All registers preserved, so
                                ; the damage rect below is untouched
.clamp:
    or ax, ax                   ; onto the live screen (SPEC.md 39.2)
    jns .cx1
    xor ax, ax
.cx1:
    or bx, bx
    jns .cy1
    xor bx, bx
.cy1:
    cmp cx, [vid_wm1]
    jle .cx2
    mov cx, [vid_wm1]
.cx2:
    cmp dx, [vid_hm1]
    jle .cy2
    mov dx, [vid_hm1]
.cy2:
    cmp ax, cx
    jg .chrome                  ; nothing was uncovered, but a caller with an
    cmp bx, dx                  ; empty rect is still telling us SOMETHING
    jg .chrome                  ; changed - the dock's tiles, the bar's owner
    mov [wm_dmg_x1], ax
    mov [wm_dmg_y1], bx
    mov [wm_dmg_x2], cx
    mov [wm_dmg_y2], dx

    cmp byte [wm_dmg_fs], 0
    jne .wins                   ; fullscreen: no zones to grow the rect to
    call COLD_SEG:desk_dmg_zones_x ; AL = the zone mask; the rect grows to cover
    mov [wm_dmg_zn], al         ; every zone it touched
    or al, al
    jz .fill                    ; ...and a zone is redrawn WHOLE, so a window
    mov word [wm_dmg_stwin], 0  ; over one is damaged by something other than
                                ; the move: 11.91.2's rect no longer describes
                                ; everything that can have gone stale, so it
                                ; is disarmed rather than reasoned about

.fill:
    mov ax, [wm_dmg_x1]         ; the desktop dither, clipped to the damage,
    mov bx, [wm_dmg_y1]         ; to EACH display's own chrome band and to the
    mov cx, [wm_dmg_x2]         ; windows about to cover it. The band used to
    mov dx, [wm_dmg_y2]         ; be worked out here, from this rect's top-left
    call wm_dmg_gray            ; corner alone - see wm_dmg_bands
.zones:
    mov al, [wm_dmg_zn]
    call COLD_SEG:desk_paint_mask_x ; the touched drive zones, whole
    mov ax, [wm_dmg_y2]         ; reached the strip? then the dither above
    cmp ax, [vid_dock_y0]       ; just ran over it, and whatever WAS covering
    jl .dock                    ; it has moved - the one case that owes the
    mov ax, [wm_dmg_x1]         ; strip a restore rather than a tile. Only the
    mov cx, [wm_dmg_x2]         ; DAMAGED span of it, though: this used to
    call dock_force_x           ; force all 640px and rebuild every tile for a
                                ; window that closed in one corner (SPEC 30.3)
.dock:
    call dock_paint             ; CF = 1 if it actually put pixels on the strip
    jnc .bar                    ; - and only then does a window sitting on the
    mov byte [wm_dmg_dk], 1     ; strip owe a redraw (SPEC.md 11.91)
.bar:
    call menu_draw_bar
.wins:
    call wm_dmg_wins            ; mark and draw; leaves wm_dmg_mask for below

    ; --- the window this uncovered is the front window now -----------------
    ; Hiding or destroying the frontmost window promotes whatever was under
    ; it, and the promotion is visible: the pinstripes, the close box and the
    ; minimize box belong to the front window alone (SPEC.md 11). If the new
    ; front was redrawn above it already has them; if it sat well clear of
    ; the damage it was never touched, and this is the one rect it owes.
.promote:
    call wm_top
    or bx, bx
    jz .out
    mov ax, bx
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = its index (AH = 0: record-aligned)
    xor ah, ah
    mov di, ax
    shl di, 1
    mov ax, [wm_bit1+di]
    test [wm_dmg_mask], ax
    jnz .out                    ; redrawn in this pass: it has them already
    mov di, bx
    mov bp, bx                  ; DI == BP: draw it as the FRONT window
    call wm_draw_title
    call wm_grow_paint          ; ...and it GAINS a grow box, the other half
    jmp short .out              ; of SPEC.md 11.1.1 (BX is still the window)

.chrome:
    cmp byte [wm_dmg_fs], 0     ; "nothing was uncovered but the chrome
    jne .out                    ; changed" - and under a fullscreen window
    call wm_paint_chrome        ; the chrome is not on screen to change
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ...and the pass itself, which wm_dock_under borrows to put the windows back
; over a strip it has just redrawn.
; -----------------------------------------------------------------------------
; wm_dmg_wins - redraw every window that a damage rect touched, back to front
; in:  wm_dmg_x1/y1/x2/y2 set; [wm_dmg_dk] = 1 if the DOCK STRIP was just
;      drawn, which damages every window sitting on it however far that is
;      from the rect; caller holds the gfx lock
; out: wm_dmg_mask = which windows were drawn. [wm_dmg_dk] consumed (a
;      one-shot argument, so it can never be left set for the next caller).
;      All registers preserved.
;
; The dock flag is an ARGUMENT and not something read from the strip, because
; dock_paint is incremental (SPEC.md 30): it keys each tile on its icon and
; its live/minimized/active state, so a quiet desktop costs no pixels at all.
; This test used to be unconditional - the comment said "the dock is repainted
; unconditionally above", which stopped being true when the tiles got their
; keys - and the cost was one whole window repaint, W_PAINT included, on every
; damage pass, for a strip that had not changed. A window dragged low enough
; to sit on the strip therefore redrew itself every time a volume was mounted,
; and the marking is transitive, so everything overlapping it went too.
;
; A wrong 1 costs a needless repaint; a wrong 0 leaves a window with the dock
; drawn through it. So the flag is cleared HERE rather than by the caller: a
; path that forgets to set it can only be one that did not draw the strip.
; -----------------------------------------------------------------------------
wm_dmg_wins:
    KFZ 22                      ; the damage pass over the windows
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    ; --- mark: overlaps the damage, or overlaps something already marked ---
    ; Bottom to top, so "already marked" means "marked and below us" and one
    ; pass reaches the whole transitive closure. Nothing here may keep a loop
    ; counter in a general register: wm_win_rect writes all four of them.
    mov word [wm_dmg_mask], 0
    mov word [wm_dmg_shad], 0
    xor bp, bp                  ; BP = index into wm_zord
.mark:
    mov al, [wm_zn]
    xor ah, ah
    cmp bp, ax
    jae .draw
    mov si, wm_zord
    add si, bp
    mov al, [si]
    call wm_idx2ptr             ; BX = window ptr (AX and CX survive)
    test word [bx+W_FLAGS], 2
    jz .mnext
    mov si, bx                  ; SI = our window for the rest of the pass
    call wm_win_rect            ; AX,BX,CX,DX = our rect, shadow included
    call wm_dmg_hit
    jnc .mnodmg
    cmp si, [wm_dmg_stwin]
    je .mset                    ; the window that moved draws itself, always
    call wm_dmg_stale           ; ...and one that never overlapped the rect it
    jnc .mset                   ; VACATED had nothing uncovered (SPEC.md
                                ; 11.91.2). It still falls through to the two
                                ; tests below: the dock is drawn UNDER windows,
                                ; and a marked window BELOW is redrawn whole
                                ; and reaches wherever its own rect reaches
    call wm_dmg_shadowed        ; ...but it still owes its DROP SHADOW, because
    jnc .mnodmg                 ; 11.91.1's dither subtracts the frame and eats
    push ax                     ; the L (SPEC.md 11.91.4). Two lines, not a
    call wm_dmg_bit             ; repaint - and NOT wm_dmg_mask, or this would
    or [wm_dmg_shad], ax        ; be 11.91.2 undone.
    pop ax                      ;
                                ; AX IS BANKED BECAUSE dock_px_hit BELOW READS
                                ; IT AS x1 and wm_dmg_bit answers in it: this
                                ; route falls through into .mnodmg with the
                                ; window's z-bit where the rect's left edge
                                ; should be, which reads as a dock overlap
                                ; test that is right by accident or not at all
.mnodmg:
    cmp byte [wm_dmg_dk], 0     ; the strip was drawn, and it is drawn UNDER
    je .mnodock                 ; windows, so a window the strip was drawn
    call dock_px_hit            ; under is damaged by it - per window, rather
    jc .mset                    ; than by widening the rect, which would erase
.mnodock:                       ; the whole width. Gated, because dock_paint
                                ; draws nothing at all on a quiet desktop.
                                ; It is a RECT overlap against the strip that
                                ; was actually painted (SPEC.md 30.3.3) and
                                ; was `dx >= [vid_dock_y0]` alone, which
                                ; ignored both the span and which DISPLAY the
                                ; window is on
    mov word [wm_dmg_j], 0      ; not damaged directly - but a marked window
.mbelow:                        ; below is redrawn WHOLE and would paint on us
    mov ax, [wm_dmg_j]
    cmp ax, bp
    jae .mnext
    mov di, wm_zord
    add di, ax
    mov al, [di]                ; AL = that window's index
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [wm_bit1+bx]
    test [wm_dmg_mask], bx
    jz .mbnext
    call wm_idx2ptr             ; BX = its record (AL still its index)
    mov di, bx
    call wm_win_hit             ; SI = ours, DI = theirs; preserves both
    jc .mset
.mbnext:
    inc word [wm_dmg_j]
    jmp short .mbelow
.mset:
    call wm_dmg_bit
    or [wm_dmg_mask], ax
.mnext:
    inc bp
    jmp .mark

    ; --- draw the marked ones, back to front ------------------------------
    ;
    ; FROM HERE wm_dmg_x1..y2 MEANS WHAT THIS PASS HAS PAINTED, not what it was
    ; asked to repair (SPEC.md 11.96.6). It is the same quantity desk_dmg_zones
    ; already grew it to - a drive zone is drawn whole, so the rect had to cover
    ; it - continued through the windows: each one adds its own frame, because
    ; wm_draw_win writes that whether its cache hit or not. Nothing reads the
    ; rect as damage after the marking pass above, and wm_paint_dmg's .promote
    ; below reads only wm_dmg_mask.
.draw:
    mov ax, [wm_dmg_mask]
    or ax, [wm_dmg_shad]        ; a pass may owe nothing but a shadow
    jz .wdone
    call wm_top
    mov bp, bx                  ; BP = frontmost visible (0 if none)
    xor si, si                  ; SI = index into wm_zord
.dwin:
    mov al, [wm_zn]
    xor ah, ah
    cmp si, ax
    jae .wdone
    mov di, wm_zord
    add di, si
    mov al, [di]
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [wm_bit1+bx]
    test [wm_dmg_mask], bx
    jnz .dfull
    test [wm_dmg_shad], bx      ; not redrawn, but the dither ate its drop
    jz .dnext                   ; shadow: put the L back and nothing else
    call wm_idx2ptr             ; (SPEC.md 11.91.4). IN THIS LOOP rather than
    test word [bx+W_FLAGS], 2   ; in a pass of its own, so it lands at this
    jz .dnext                   ; window's own place in the z-order
    push si
    mov di, bx
    call wm_chrome_clip         ; confined exactly as wm_draw_win confines its
    call thm_ink                ; own chrome (SPEC.md 11.97) - a window above
    call wm_draw_shadow         ; may be sitting on this shadow, and an
    mov word [wm_clip_n], 0     ; unmarked one will not redraw over us
    pop si
    jmp short .dnext
.dfull:
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jz .dnext
    call wm_covered             ; wholly hidden: every pixel it would write
    jc .dnext                   ; is written again by something above it
%ifndef REDRAWFULL
    push bx                     ; BX IS THE WINDOW wm_draw_win is about to be
    push si                     ; handed and SI is the z-order cursor, and a
    mov si, bx                  ; rect argument spends four registers - this
    call wm_su_owed             ; loop's own rule, one line further out
    call wm_su_sub              ; (SPEC.md 11.96.6)
    pop si
    pop bx
%endif
%ifndef REDRAWFULL
    mov byte [wm_dmg_cullq], 1  ; SPEC.md 11.3.3: THIS pass, and no other,
%endif                          ; redraws everything above afterwards
    call wm_draw_win
%ifndef REDRAWFULL
    mov byte [wm_dmg_cullq], 0
    push si                     ; ...and it has now painted its own FRAME,
    mov si, bx                  ; cache hit or not - so every window above it
    call wm_win_rect            ; owes that too. (SI is the z-order cursor here
    call wm_dmg_add             ; and wm_win_rect wants the window.)
    pop si
%endif
.dnext:
    inc si
    jmp .dwin
.wdone:
    mov byte [wm_dmg_dk], 0     ; consumed: a one-shot argument must not be
    mov word [wm_dmg_stwin], 0  ; left set for a caller that draws no strip -
    mov word [wm_su_son], 0     ; and the vacated rect is one for the same
                                ; reason (SPEC.md 11.91.2). The sub-rect is the
                                ; third: wm_covered can skip the window it was
                                ; armed for, and it must not reach the next
                                ; wm_draw_win from anywhere in the system
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_dock_under - bring the dock strip up to date, and put back whatever
;                 window is sitting on it
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; The strip is drawn UNDER windows (SPEC.md 30), and wm_fit only keeps a
; window above it - ui_grow's clamp is looser, so a grown window CAN hang over
; it. This used to be the cheap raise path's one veto: wm_fast_ok declined and
; the caller repainted the whole screen, so with a single oversized window on
; screen EVERY focus change, show and un-minimize cost a full repaint.
;
; It costs a rectangle instead. Two tests come first and both are usually no:
; dock_paint says whether it drew anything at all, and wm_dock_clear whether
; any window is over the strip. Only when both are yes does anything get
; redrawn, and then only the windows the strip's own rect touches.
; -----------------------------------------------------------------------------
wm_dock_under:
    push ax
    call dock_paint             ; CF = 1 if it put pixels on the strip
    jnc .out
    call wm_dock_clear          ; CF = 1 if a window is sitting on them
    jnc .out
    mov ax, [dock_px1]          ; ...and the damage is the span dock_paint
    mov [wm_dmg_x1], ax         ; PAINTED, not the whole width (SPEC.md
    mov ax, [vid_dock_y0]       ; 30.3.3): a focus change moves two tiles, and
    mov [wm_dmg_y1], ax         ; the strip's empty end was marking every
    mov ax, [dock_px2]          ; window standing over it for nothing. CF = 1
    mov [wm_dmg_x2], ax         ; above, so the span is non-empty by
    mov ax, [vid_phm1]          ; construction
    mov [wm_dmg_y2], ax
    mov byte [wm_dmg_dk], 1     ; dock_paint said CF = 1 above, so the strip
    call wm_dmg_wins            ; really was drawn (the rect covers it anyway,
.out:                           ; but the flag is an argument and says so)
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_paint_all - full screen repaint
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Desktop gray dither below the menu bar, then the menu bar, then every
; visible window back to front: frame, white content fill, W_PAINT.
; -----------------------------------------------------------------------------
wm_paint_all:
    KFZ 23                      ; a WHOLE desktop repaint
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call wm_fs_vis              ; a visible fullscreen window covers the
    jc .fswins                  ; desktop, dock and menu bar - painting
                                ; them under it is pure waste (SPEC.md 11.2)
    xor ax, ax                  ; desktop: 50% dither under the bar, and
    mov bx, MBAR_H              ; STOPPING ABOVE THE DOCK (SPEC.md 30.3.2) -
    mov cx, [vid_pwm1]           ; dock_paint is the next call but one and owns
    mov dx, [vid_dock_y0]       ; every row from here down, so dithering them
    dec dx                      ; was a full-width band drawn to be painted
    call thm_desk               ; over: the double-draw flash, and the reason
                                ; every tile had to be rebuilt afterwards
%ifdef KERN_BIG
    call vid_disp_desk        ; ...and every OTHER display, whole (SPEC.md
                                ; 39.14.4): the chrome is the primary's, so a
                                ; second monitor is pure desktop. One compare
                                ; on a machine with one card
%endif
    call COLD_SEG:desk_paint_x  ; drive icons live on the desktop, under
                                ; every window (SPEC.md 26)
    call dock_paint             ; ...so the strip is UNTOUCHED and usually has
                                ; nothing to do. No dock_force: nothing here
                                ; drew on it any more
    call menu_draw_bar          ; and no menu_force either - the dither starts
                                ; at MBAR_H, so nothing in this routine has
                                ; ever been able to touch the bar (SPEC 12.9).
                                ; The one thing that covers it is a fullscreen
                                ; window, and menu_draw_bar sees that itself
.wins:
    call wm_top
    mov bp, bx                  ; BP = frontmost visible (0 if none)
    mov si, wm_zord
    mov al, [wm_zn]
    mov ah, 0
    mov di, si
    add di, ax                  ; DI = one past the front
    jmp short .win

    ; --- ...and under a fullscreen window the walk STARTS at it ------------
    ; Everything below a window covering the whole screen is covered by it,
    ; entirely and by construction. wm_covered would reach the same answer
    ; one region-arithmetic pass per window later; this is the same fact
    ; known in advance, and it is what stops a fullscreen player from
    ; examining - and, before wm_covered existed, redrawing - every window
    ; parked behind it.
.fswins:
    call dock_paint             ; ...but both strips are still TOLD (SPEC.md
    call menu_draw_bar          ; 30.3.2): each early-outs on wm_fs_vis without
                                ; drawing and marks itself owed, so whatever
                                ; repaints when the window goes puts it back
    call wm_top
    mov bp, bx                  ; BP = frontmost visible
    mov si, wm_zord
    mov al, [wm_zn]
    mov ah, 0
    mov di, si
    add di, ax                  ; DI = one past the front
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .fsone
    call vid_disp_desk        ; wm_fs_vis said display 0, so the OTHER
                                ; display is not under it (SPEC.md 39.17.1):
                                ; its desktop still dithers...
    jmp short .win              ; ...and its windows still draw - the walk
                                ; starts at the BOTTOM, and wm_covered skips
                                ; everything the fullscreen window really
                                ; covers. That is one region pass per covered
                                ; window, on a path that is already a full
                                ; repaint; started AT the fullscreen window
                                ; it skipped every display-1 window below it
                                ; in z and left that monitor stale for good
.fsone:
%endif
    call wm_fs_zpos             ; SI = the fullscreen window's slot in wm_zord
.win:
    cmp si, di
    je .done
    mov al, [si]
    inc si
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jz .win
    call wm_covered             ; a window nothing shows of writes no pixel
    jc .win                     ; that a window above it does not overwrite
    call wm_draw_win
    jmp .win
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_content - content-area origin of a window
; in:  BX = window ptr
; out: AX = content left (W_X+1), DX = content top (W_Y+TITLE_H);
;      WF_FULL set: AX = W_X, DX = W_Y - no border, no title bar (SPEC 11.2)
; -----------------------------------------------------------------------------
wm_content:
    mov ax, [bx+W_X]
    mov dx, [bx+W_Y]
    test word [bx+W_FLAGS], WF_FULL
    jnz .full
    add dx, TITLE_H
    call wm_flush               ; a window spanning the screen has no LEFT
    jc .full                    ; border, so its content starts at W_X and is
    inc ax                      ; aligned there (SPEC.md 11.95.2)
.full:
    ret

; -----------------------------------------------------------------------------
; wm_about_set - register a window's About handler (public; API 0x01E0,
;                SPEC.md 12.2)
; in:  BX = window ptr, SI = the handler's offset in the CALLER's segment
;      (0 = this window has no About)
; out: nothing - every register preserved, FLAGS INCLUDED
;
; The whole of the opt-in for the name menu. A window that declares a handler
; gets its application's NAME in the bar as a real pull-down carrying one
; item, 'About <Name>'; picking it dispatches this offset exactly like
; W_ONCLICK - through the window's own dispatcher, so a package writes an
; ordinary near proc ending in `ret` (SPEC.md 20.1). A window that declares
; none keeps the plain label the bar has always drawn, which is why every
; existing application is unaffected and no package can be handed a menu
; index it was never written for.
;
; STORED, never dereferenced here - like menu_win_set, whose flag-preserving
; contract this shares for the same reason: the one place it is meant to be
; called from is a package entry proc sitting between wm_create and its own
; `ret`, which owes the loader wm_create's CF (SPEC.md 20.2).
; -----------------------------------------------------------------------------
wm_about_set:
    pushf
    push ax
    push di
    call wm_ptr2idx             ; AX = window index (AH = 0)
    mov di, ax
    shl di, 1
    mov [wm_about+di], si
    cmp bx, [menu_win]
    jne .out
    call menu_relayout          ; the active application's bar changed
.out:
    pop di
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_about_get - a window's About handler offset, 0 = none (module internal
;                to the bar: menu_relayout and ui_dispatch)
; in:  BX = window ptr
; out: AX = the offset wm_about_set registered, 0 = none
; clobbers: flags only
; -----------------------------------------------------------------------------
wm_about_get:
    push di
    call wm_ptr2idx             ; AX = window index (AH = 0)
    mov di, ax
    shl di, 1
    mov ax, [wm_about+di]
    pop di
    ret

; -----------------------------------------------------------------------------
; wm_geom - a window's CONTENT size, and whether it is on screen (public;
;           API 0x01B0, SPEC.md 11)
; in:  BX = window ptr
; out: CX = content width, DX = content height, both in pixels;
;      CF = 1 the window is not visible (CX/DX are still filled in).
;      Everything else preserved.
;
; The companion to wm_content, which answers the origin: together they are
; the whole content rect. A package can read W_W/W_H out of the
; record itself (ES = KERNEL_SEG in every callback, SPEC.md 20.1) and most
; of them do - but that is FRAME size, and every caller then repeats the
; same `sub 2` / `sub TITLE_H+1`. This is that arithmetic in one place, so
; a package never repeats it and never gets it subtly wrong.
;
; Callable from any context, lock held or not: it reads four words. The
; answer is only STABLE under the lock, which is when painters ask.
; -----------------------------------------------------------------------------
wm_geom:
    mov cx, [bx+W_W]
    mov dx, [bx+W_H]
    test word [bx+W_FLAGS], WF_FULL
    jnz .vis                    ; fullscreen: the frame IS the content
    sub dx, TITLE_H + 1         ; content rows y+TITLE_H .. y+h-2
    push ax                     ; content cols x+b .. x+w-1-b, so the WIDTH is
    call wm_bord                ; w - 2b (SPEC.md 11) - and a window that SPANS
    sub cx, ax                  ; THE SCREEN has neither side border, so b is 0
    call wm_bordr               ; and the content is the whole frame width
    sub cx, ax                  ; (SPEC.md 11.95.3)
    pop ax
.vis:
    test word [bx+W_FLAGS], 2   ; visible?
    jz .hidden
    clc
    ret
.hidden:
    stc
    ret

; -----------------------------------------------------------------------------
; wm_win_over - does window DI, drop shadow included, land on window BX?
; in:  DI = the covering window, BX = the covered one (neither is tested for
;      visibility: both callers have their own reason to ask)
; out: CF = 0 = the two rects overlap; CF = 1 = they do not
; clobbers: nothing else (flags)
;
; DI's rect is (x, y, w+1, h+1) - the drop shadow is on row y+h and column
; x+w, so "not overlapping" really means none of its pixels sit over ours.
;
; Lifted out of wm_obscured whole, because wm_su_precover (SPEC.md 11.96.16)
; asks the same question about a window that is not in the z-order's answer
; yet: it is the ONE window whose pixels are not on the glass, which is why
; the pass runs before its visible bit goes on. Two copies of this arithmetic
; would be two opinions about what "covers" means, and the shadow is exactly
; the sort of detail one of them would lose.
; -----------------------------------------------------------------------------
wm_win_over:
    push ax
    push dx
    mov ax, [di+W_X]            ; above.x <= our right edge?
    mov dx, [bx+W_X]
    add dx, [bx+W_W]
    cmp ax, dx
    jge .no
    mov ax, [di+W_X]            ; above right edge + shadow >= our left?
    add ax, [di+W_W]
    cmp ax, [bx+W_X]
    jl .no
    mov ax, [di+W_Y]            ; above.y <= our bottom edge?
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    cmp ax, dx
    jge .no
    mov ax, [di+W_Y]            ; above bottom edge + shadow >= our top?
    add ax, [di+W_H]
    cmp ax, [bx+W_Y]
    jl .no
    pop dx                      ; a pop touches no flag, so the clc/stc below
    pop ax                      ; is what the caller reads
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wm_obscured - is anything covering this window? (SPEC.md 11.3.1)
; in:  BX = window ptr
; out: CF = 1 if BX is not visible at all, or a visible window later in
;      wm_zord overlaps its frame rect
;
; The window above is tested with its drop shadow included - rect
; (x, y, w+1, h+1) - so an "unobscured" answer really means none of its
; pixels, shadow included, sit over ours. Only trustworthy while the caller
; holds the gfx lock (the UI task mutates wm_zord/rects under it).
;
; THE VISIBLE TEST IS PART OF THE ANSWER, and for a long time it was not:
; this asked only "is anything ON TOP of me", so a HIDDEN window - still in
; wm_zord, with nothing above it - came back CLEAR and its background painter
; drew onto the bare desktop. Every correct caller carried its own
; `test [W_FLAGS], 2` on the line above the call (wm_front, cp_tick, and six
; package workers), and nine copies of a test is the shape that says the
; routine owes the answer. Note Pad's worker was the tenth and did not: closing
; it left a scroll bar drawn on the desktop, in the ~165ms between the hide and
; the worker's next OSAPI_TASK_ALIVE. The error is one-sided - no caller wants
; "draw the lot" about a window with no pixels on screen - so the safe value is
; the one a forgotten call site inherits (wm_dmg_dk's rule, SPEC.md 11.91).
; -----------------------------------------------------------------------------
wm_obscured:
    test word [bx+W_FLAGS], 2   ; on screen at all? Everything a hidden window
    jz .gone                    ; would draw lands on somebody else's window or
                                ; on the desktop, and nothing ever erases it
    push ax
    push cx
    push dx
    push si
    push di
    mov ax, bx                  ; AL = our index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    jcxz .clear
.find:
    cmp [si], al
    je .found
    inc si
    loop .find
    jmp .clear                  ; not in z-order (cannot happen)
.found:
    inc si                      ; first entry above us
    dec cx
.scan:
    jcxz .clear
    dec cx
    mov al, [si]
    inc si
    push bx
    call wm_idx2ptr
    mov di, bx                  ; DI = window above
    pop bx
    test word [di+W_FLAGS], 2
    jz .scan
    call wm_win_over            ; does DI land on BX? (SPEC.md 11.96.16)
    jc .scan
    stc                         ; overlap: we are obscured
    jmp .out
.clear:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret
.gone:                          ; above the pushes, so nothing to unwind
    stc
    ret

; =============================================================================
; The clip region (SPEC.md 11.3) - what a background task may draw
;
; wm_obscured above answers a boolean, and every background painter used it
; as a veto: one covered pixel and the whole frame was skipped, because the
; gfx_* primitives draw in absolute screen coordinates and would otherwise
; paint straight over the window on top. The region replaces the veto: it is
; the window's content rect with every visible window above it subtracted,
; and while it is armed the six clipped primitives (SPEC.md 5/6/25) draw
; only inside it.
;
; Three properties are binding:
;   - It is built and consumed inside ONE gfx lock hold. wm_zord and the
;     window rects are mutated only under the lock, so a region built at
;     wm_clip_set is valid until gfx_unlock - and stale the moment after,
;     which is why gfx_unlock clears it rather than trusting callers.
;   - Overflow degrades to "skip the frame" (CF = 1), which is exactly the
;     old wm_obscured behaviour and therefore cannot regress anything.
;   - Nothing here needs an interrupt guard, for the same reason vga12's
;     rect scratch does not: the mouse ISR never draws while the lock is set.
;
; There is no wm_obscured fast path in wm_clip_set, deliberately: the region
; build IS wm_obscured's z-order walk plus a rect subtraction that does
; nothing when nothing overlaps, so a pre-test would only walk it twice.
; =============================================================================

; WM_CLIP_MAX and the WCR_* rect offsets are in kernel.asm: vga12.inc,
; font.inc and icons.inc read this list and NASM sees them first.

wm_dmg_x1   dw 0                ; the damage rect wm_paint_dmg works from,
wm_dmg_y1   dw 0                ; inclusive and already clamped to the screen
wm_dmg_x2   dw 0
wm_dmg_y2   dw 0
wm_dmg_mask dw 0                ; bit n = window n must be redrawn
wm_dmg_shad dw 0                ; bit n = window n owes its DROP SHADOW and
                                ; nothing else (SPEC.md 11.91.4). Never both:
                                ; a window in wm_dmg_mask is redrawn whole and
                                ; the shadow comes with it
wm_dmg_zn   db 0                ; bit n = drive zone n must be redrawn
wm_dmg_fs   db 0                ; 1 = a fullscreen window is over the chrome,
                                ; so this pass draws windows and nothing else
wm_dmg_dk   db 0                ; 1 = the dock strip really was drawn, so a
                                ; window sitting on it owes a redraw. A
                                ; one-shot argument to wm_dmg_wins, which
                                ; clears it on the way out
wm_dmg_j    dw 0                ; the marking pass's inner index

; --- the rect a moved window is LEAVING (SPEC.md 11.91.2) -------------------
; A one-shot argument to wm_dmg_wins, like wm_dmg_dk, and cleared by it for
; the same reason: a call site that forgets inherits 0, which marks against
; the whole damage rect as this pass always did, and a wasted repaint is the
; safe error.
wm_dmg_stwin dw 0               ; the window that moved, 0 = not armed
wm_dmg_rzwin dw 0               ; SPEC.md 11.90.3: ...and the one ui_grow just
                                ; SHRANK without moving, which owes its content
                                ; nothing. A one-shot across wm_paint_dmg
wm_dmg_st1  dw 0                ; ...and where it WAS, shadow included
wm_dmg_sy1  dw 0
wm_dmg_st2  dw 0
wm_dmg_sy2  dw 0
wm_hide_r   times 4 dw 0        ; wm_hide/wm_destroy: the vacated rect,
                                ; banked across the record teardown
wm_dg_r     times 4 dw 0        ; wm_dmg_gray's rect, banked across the
                                ; region build that eats its registers

wm_clipwin  dw 0                ; the window wm_clip_set was given, banked
                                ; because BX does not survive the occlusion
                                ; walk. It was the CURFIX knob's alone - the
                                ; pre-7.1.4.2 cur_lazyck tested the window's
                                ; FRAME, and the region test wants no window at
                                ; all - and 11.96.18's drop, which happens
                                ; after that walk, needs the same word in every
                                ; build
wm_clip_n   dw 0                ; rects in wm_clip_tab; 0 = disarmed.
                                ; Initialized data, NOT .bss: every gfx_fill
                                ; reads it, including the machine's first,
                                ; and -f bin zeroes nothing.
wm_clip_ofr db 0                ; wm_clip_occl subtracts by the FRAME rect and
                                ; not the occupied one (SPEC.md 11.96.15).
                                ; Written by both entry points before either
                                ; reads it, so its value here is documentation
wm_dmg_cullq db 0               ; SPEC.md 11.3.3: wm_dmg_wins ASKS for the cull
wm_dmg_cull db 0                ; ...and wm_draw_win turns it on around W_PAINT
                                ; alone, which is what keeps wm_su_try's own
                                ; region arming out of it. Two bytes and not
                                ; one: the request outlives the state
wm_clip_r0  db 0                ; wm_clip_rows' answer (SPEC.md 11.3.2): the
wm_clip_rn  db 8                ; first drawable row of the cell it was last
                                ; asked about, and how many from there. Valid
                                ; only immediately after that call - the glyph
                                ; renderers write them on their unclipped path
                                ; too, so these are "the last row answer" and
                                ; never a standing description of anything.
                                ; .text for wm_clip_n's reason: font_char reads
                                ; them before any code has written them.
%if wm_clip_rn - wm_clip_r0 != 1
  %error "wm_clip_r0/rn must stay adjacent: font_char sets both in one word store"
%endif

; -----------------------------------------------------------------------------
; wm_clip_set - arm clipping to a window's visible region
; in:  BX = window ptr; caller HOLDS the gfx lock
; out: CF = 1 the window is entirely invisible - nothing armed, skip the
;      frame; CF = 0 clipping is armed and dies at the next gfx_unlock
; clobbers: flags
;
; "ENTIRELY INVISIBLE" INCLUDES HIDDEN, and it did not (SPEC.md 11.3.1). The
; seed is the window's own rect and wm_clip_occl subtracts the windows ABOVE
; it, so a hidden window with nothing on top of it armed its full content rect
; and every clipped primitive then drew straight onto the desktop. The contract
; above already said otherwise, and files_poster_x read it exactly as written
; ("CF = 1: none of it is visible any more") with no visibility test of its
; own - so this was a promise the routine did not keep rather than a rule its
; callers were failing to learn. That caller is gone (SPEC.md 22.17) and the
; fix is not: the contract is what it always said, and the next caller to take
; it at its word gets the behaviour it promises.
;
; Asked FIRST, above wm_su_drop: a hidden window is about to draw nothing, so
; the raise cache wm_hide banked at the moment it went down (11.96.5) has to
; survive this call rather than being dropped by it.
; -----------------------------------------------------------------------------
wm_clip_set:
    test word [bx+W_FLAGS], 2
    jz .gone
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, [bx+W_FLAGS+1]      ; ...AND IT IS THIS WINDOW'S CLAIM (SPEC.md
    and al, WF_1BPP >> 8        ; 11.96.17.1). Here as well as in wm_pkgcall
    mov [gfx_mono1], al         ; because a background painter is a WORKER and
                                ; reaches no dispatcher at all - and this is
                                ; the door 11.3 already makes every one of
                                ; them come through. Not stacked: gfx_unlock
                                ; clears it, which is [gfx_dis]'s lifetime
    mov [wm_clipwin], bx        ; the window, because BX does not survive the
                                ; occlusion walk - .done needs it for the drop
                                ; below and, under CURFIX, for the frame test
    mov word [wm_clip_n], 0

    mov ax, [bx+W_X]            ; seed with the content rect - the same one
    mov dx, [bx+W_Y]            ; wm_draw_win white-fills before W_PAINT
    mov cx, ax
    add cx, [bx+W_W]
    dec cx
    mov si, dx
    add si, [bx+W_H]
    dec si
    test word [bx+W_FLAGS], WF_FULL
    jnz .seed                   ; fullscreen: the frame IS the content
    dec cx                      ; else x+1 .. x+w-2, y+TITLE_H .. y+h-2
    add dx, TITLE_H
    dec si
    call wm_bordr               ; ...or x+b .. x+w-1-b: with NO side borders an
    inc cx                      ; app may draw in both edge columns, which on a
    sub cx, ax                  ; maximized window is where its last CELL goes
    call wm_bord                ; (SPEC.md 11.95.3). The RIGHT edge first,
    add ax, [bx+W_X]            ; because AX is the seed's x and both answers
.seed:                          ; come back in AX
    cmp ax, cx
    jg .none                    ; degenerate content rect
    cmp dx, si
    jg .none
    call wm_clip_seed
    call wm_clip_occl           ; subtract every visible window above us
    jc .none                    ; the list overflowed: degrade to a skip
    cmp word [wm_clip_n], 0
    jne .done
.none:
    mov word [wm_clip_n], 0
    stc
    jmp short .out
.done:
    ; --- THE RAISE CACHE GOES HERE, AND NOT AT THE TOP (SPEC.md 11.96.18) ----
    ; This window is about to draw, so whatever the cache holds for it is now
    ; a picture of the past - every background painter comes through here
    ; (11.3 makes it arm a region or paint over the window on top of it), so
    ; one call covers every app at once with no rule for an app to remember.
    ; Per window (11.96.3): only the drawer's own.
    ;
    ; **AFTER THE OCCLUSION WALK**, which is the whole of 11.96.18. It ran at
    ; the top for as long as this existed, so a window that is visible but
    ; WHOLLY COVERED - precisely the state a save-under is for - destroyed its
    ; own cache and was then told, two calls later, that there was nothing to
    ; draw. Every app with a background painter did it on every poll, so the
    ; feature could not survive being switched on. Nothing between the two
    ; points draws: the seed and the walk read the record and write
    ; wm_clip_tab, and .none below arms nothing at all.
    cmp byte [wm_dmg_cull], 0
    jne .nodrop                 ; ...EXCEPT UNDER THE CULL (SPEC.md 11.3.3),
                                ; where wm_su_try has already run and either
                                ; restored or missed: 11.96.4 leaves that
                                ; decision to wm_su_bank at the end of the
                                ; draw, and a drop here would take it away
                                ; from a window that never moved
    push bx
    mov bx, [wm_clipwin]
    call wm_su_drop
    pop bx
.nodrop:

    ; The region is armed, and from here to gfx_unlock every clipped primitive
    ; is CONFINED to it - so if the cursor is outside THE REGION the deferred
    ; hide (SPEC.md 7.1.4) is not owed and the pointer stays on screen through
    ; the whole refresh. This is the one place that can prove that, and it is
    ; already on the path of all twelve background painters.
    ;
    ; The region and not the window: the fragments are the content rect minus
    ; the windows ON TOP of it, so a pointer over the window in FRONT of this
    ; one is unreachable while the frame this used to test says otherwise
    ; (SPEC.md 7.1.4.2).
    ;
    ; The FAILURE path above deliberately does not check: with no region armed
    ; every primitive falls to its own unconditional cur_unlazy, which is the
    ; safe default and exactly what happened before any of this.
    mov si, [wm_clipwin]
    call cur_lazyck
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.gone:                          ; above the pushes, so nothing to unwind - and
    mov word [wm_clip_n], 0     ; "nothing armed" is a store, not an assumption:
    stc                         ; a caller may arm twice inside one lock hold
    ret

; -----------------------------------------------------------------------------
; wm_clip_occl - subtract every visible window ABOVE BX from the armed list
; in:  BX = window ptr; the list holds at least one rect
; out: CF = 1 the list overflowed - nothing about it is valid. CF = 0 and
;      [wm_clip_n] = 0 means the seed rect is entirely covered.
; clobbers: flags
;
; The walk both callers need, and the reason they can disagree about what an
; empty list MEANS: wm_clip_set reads it as "invisible, skip the frame" and
; wm_covered as "covered, skip the redraw" - opposite readings of the same
; fact, and opposite degradations on overflow (skip vs. draw anyway).
; -----------------------------------------------------------------------------
; wm_clip_occlf - the same walk, subtracting each window above by the rect
; wm_draw_win REPAINTS (wm_clip_subf's frame) rather than the box it occupies.
;
; SPEC.md 11.96.15. Which of the two is right is decided by what the caller
; does with the answer, and the tree already knows it at wm_clip_subf: the
; drop shadow is an L starting at (+1,+1), so the occupied box holds two
; corners - (x+w, y) and (x, y+h) - that nothing ever paints.
;
; For OCCLUSION - "may I draw here?" - claiming them is conservative and free:
; it says covered about a pixel that is not, and a clipped primitive declines
; to draw one nobody needed. For a RESTORE it is the other direction and it
; LOSES A PIXEL: the one declined is one no window above will write, so the
; damage under it is never repaired and the artifact is permanent. 11.91.1
; found exactly this at the dither, one pixel at a time, and this is the same
; asymmetry in the second place the tree subtracts a window in order NOT to
; draw something.
; -----------------------------------------------------------------------------
wm_clip_occlf:
    mov byte [wm_clip_ofr], 1
    jmp short wm_clip_occl_r
wm_clip_occl:
    mov byte [wm_clip_ofr], 0
wm_clip_occl_r:
    push ax
    push bx
    push cx
    push si
    mov ax, bx                  ; AL = our index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    jcxz .done
.find:
    cmp [si], al
    je .found
    inc si
    loop .find
    jmp short .done             ; not in z-order (cannot happen)
.found:
    inc si                      ; first entry above us
    dec cx
.scan:
    jcxz .done
    dec cx
    mov al, [si]
    inc si
    call wm_idx2ptr             ; BX = window above (AX and CX preserved)
    test word [bx+W_FLAGS], 2
    jz .scan
    call wm_clip_subx           ; ...by frame or by occupied rect, per entry
    jc .over
    cmp word [wm_clip_n], 0
    jne .scan
.done:
    clc
    jmp short .out
.over:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_covered - is EVERY pixel of this window hidden by windows above it?
; in:  BX = window ptr (visible); caller HOLDS the gfx lock
; out: CF = 1 = wholly covered: drawing it would change no pixel on screen.
;      All registers preserved, and the clip list is left DISARMED either way.
;
; The same region arithmetic wm_clip_set does, seeded with the FRAME rect
; (drop shadow included) instead of the content rect, because a title bar
; peeking out from under another window is still a pixel this window owns.
;
; A back-to-front painter may skip a window this answers yes about: every
; pixel it would write is written again by a window later in wm_zord, whether
; that window is being redrawn in this pass or is merely still on screen. The
; consequence a package author can see is that **W_PAINT does not run on a
; wholly covered window**, so a paint proc must be a repaint and nothing else
; - anything it also does has to tolerate being skipped (SPEC.md 11.91).
;
; Overflow degrades the OTHER way from wm_clip_set: more than 16 fragments
; means "not covered", i.e. draw it. Skipping on a maybe would lose pixels.
; -----------------------------------------------------------------------------
wm_covered:
    push ax
    push cx
    push dx
    push si
    mov word [wm_clip_n], 0
    mov ax, [bx+W_X]
    mov dx, [bx+W_Y]
    mov cx, ax
    add cx, [bx+W_W]            ; x .. x+w and y .. y+h: the frame plus the
    mov si, dx                  ; 1px drop shadow, which wm_draw_win draws
    add si, [bx+W_H]
    test word [bx+W_FLAGS], WF_FULL
    jz .seed
    dec cx                      ; fullscreen has no shadow (SPEC.md 11.2)
    dec si
.seed:
    call wm_clip_seed
    call wm_clip_occl
    jc .no
    cmp word [wm_clip_n], 0
    jne .no
    mov word [wm_clip_n], 0
    stc
    jmp short .out
.no:
    mov word [wm_clip_n], 0
    clc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_seed - internal: arm the list with one rect and nothing subtracted
; in:  AX = x1, DX = y1, CX = x2, SI = y2 (inclusive, and non-degenerate -
;      every caller has already tested that in its own terms)
; out: [wm_clip_n] = 1; all registers preserved, flags included
;
; The three region builds below start here and then differ only in WHICH
; occluders they subtract, which is the whole of what separates them.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; wm_dmg_gray - the desktop dither, MINUS the windows about to cover it
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive, already clamped);
;      caller holds the gfx lock
; out: nothing (all registers preserved)
;
; **Ground a window is about to be drawn on does not need dithering first.**
; wm_paint_dmg repaints the damage rect and then draws every window that
; overlaps it - whole (SPEC.md 11.91) - so every pixel under one of those
; windows was dithered and then painted over. That is PERFORMANCE.md Part 1's
; double-draw, and on the two operations where the damage is mostly WINDOW it
; is most of the cost: dragging a window (ui_drag passes the union of where it
; was and where it is, and the second half is covered by the window itself)
; and un-zooming one (SPEC.md 11.95.1).
;
; The region machinery already answers exactly this question. wm_clip_seed
; takes the rect, wm_dmg_occl subtracts every visible window shadow and all,
; and gfx_fill_gray is a CLIPPED primitive - so the fill is confined to what
; is genuinely being uncovered, in one call, with no new drawing code.
;
; Three things are load-bearing.
;
;   - The dither's phase is a function of ABSOLUTE x+y (see gfx_fill_gray), so
;     drawing it as fragments is pixel-identical to drawing it whole. A
;     pattern phased from the fill's own origin could not be split this way.
;   - Overflow degrades to dithering the whole of every BAND, which is the
;     opposite of wm_clip_set's degradation and for the opposite reason: a
;     fragment too many there means "skip, we might overdraw somebody", here it
;     means "draw it all, or we leave stale pixels behind". It was the whole
;     RECT until the bands became per display (SPEC.md 39.16.2.1); that rect is
;     now a bounding box which can span a seam, so the degradation re-seeds
;     rather than disarming.
;   - The hide is spent explicitly. GFXCLIP only calls cur_unlazy on its
;     UNCLIPPED branch (SPEC.md 7.1.4), trusting wm_clip_set's cur_lazyck to
;     have decided for an armed one - and there is no window here to decide
;     about. cur_unlazy is what the unclipped fill did before, so this keeps
;     the cursor behaviour exactly as it was.
; -----------------------------------------------------------------------------
wm_dmg_gray:
    push ax
    push bx
    push cx
    push dx
    push si
    call cur_unlazy              ; ...as the unclipped fill did (see above)
    mov [wm_dg_r + 0], ax       ; the DAMAGE, banked: the overflow path below
    mov [wm_dg_r + 2], bx       ; has to re-seed from it and the region build
    mov [wm_dg_r + 4], cx       ; eats every register
    mov [wm_dg_r + 6], dx
    call wm_dmg_bands           ; the seeds are each display's own band inside
    jc .out                     ; that rect (SPEC.md 39.16.2.1), and AX..DX
                                ; their bounding box - which the fill wants and
                                ; which stays in wm_db_b across the build
    call wm_dmg_occl            ; every visible window, shadow included
    jc .whole                   ; overflowed: dither the lot, as before
    cmp word [wm_clip_n], 0
    je .out                     ; wholly covered: not one pixel is revealed
    jmp short .fill
.whole:
    mov ax, [wm_dg_r + 0]       ; ...THE BANDS, not their bounding box, and not
    mov bx, [wm_dg_r + 2]       ; the damage: re-seeding from the box would
    mov cx, [wm_dg_r + 4]       ; dither columns of display 0 that the damage
    mov dx, [wm_dg_r + 6]       ; never covered, because the box spans the seam
    call wm_dmg_bands           ; and each display then clamps it back to its
                                ; own full width. It is idempotent from the
                                ; DAMAGE and is not from the box
.fill:
    mov ax, [wm_db_b + 0]       ; the bounding box of the seeds; the armed
    mov bx, [wm_db_b + 2]       ; region confines the fill inside it and
    mov cx, [wm_db_b + 4]       ; gfx_disp_run splits what is left per display
    mov dx, [wm_db_b + 6]
    call thm_desk               ; clipped to what is really being revealed
.out:
    mov word [wm_clip_n], 0     ; ours for one call and no longer
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_dmg_occl - subtract EVERY visible window from the armed list
; in:  the list holds at least one rect
; out: CF = 1 the list overflowed; CF = 0 and [wm_clip_n] = 0 means the seed
;      rect is entirely covered
; clobbers: flags
;
; wm_clip_occl's walk without the "above us" part, because there is no `us`:
; the seed is a damage rect rather than a window. Every visible window that
; overlaps it is marked by wm_dmg_wins and redrawn WHOLE, so every one of them
; is ground that does not need dithering; one that does not overlap subtracts
; nothing and costs a compare.
; -----------------------------------------------------------------------------
wm_dmg_occl:
    push ax
    push bx
    push cx
    push si
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    jcxz .done
.scan:
    mov al, [si]
    inc si
    call wm_idx2ptr             ; BX = the window (AX and CX preserved)
    test word [bx+W_FLAGS], 2
    jz .next
    call wm_clip_subf           ; the FRAME rect: what will be REPAINTED, not
    jc .over                    ; what is occupied - the shadow's two corners
    cmp word [wm_clip_n], 0     ; are in the second and in neither
    je .done                    ; nothing left: stop walking
.next:
    loop .scan
.done:
    clc
    jmp short .out
.over:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

wm_clip_seed:
    mov [wm_clip_tab+WCR_X1], ax
    mov [wm_clip_tab+WCR_Y1], dx
    mov [wm_clip_tab+WCR_X2], cx
    mov [wm_clip_tab+WCR_Y2], si
    mov word [wm_clip_n], 1
    ret

; -----------------------------------------------------------------------------
; wm_clip_rect - arm clipping to a bare screen rect's visible region
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive); caller HOLDS the gfx
;      lock
; out: CF = 1 the region needs more than WM_CLIP_MAX rects - nothing is armed
;          and the caller must fall back to something unconditional;
;      CF = 0 armed, and ZF = 1 means the rect is WHOLLY covered (the list is
;          empty, i.e. disarmed: every pixel of it belongs to some window).
;      All registers preserved; the region dies at the next gfx_unlock.
;
; wm_clip_set's arithmetic for something that is not a window. The DESKTOP is
; what needs it (SPEC.md 26.2): it is below every window, so there is no
; z-order position to find and no "above us" to walk from - every visible
; window is an occluder. That is the whole difference, and it is why this
; walks wm_wins directly instead of going through wm_clip_occl.
;
; The two degradations are wm_clip_set's, split apart because a caller can
; act on them differently: an overflow says nothing valid is known (repaint
; the world), while an empty list is a FACT (draw nothing at all).
; -----------------------------------------------------------------------------
wm_clip_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [wm_clip_n], 0
    cmp ax, cx
    jg .done                    ; degenerate rect: read as wholly covered
    cmp bx, dx
    jg .done
    mov si, dx                  ; wm_clip_seed's argument order, which is
    mov dx, bx                  ; wm_clip_set's and wm_covered's
    call wm_clip_seed

    mov bx, wm_wins             ; BX walks the table and the END is the bound,
.win:                           ; the old desk_zone_redraw idiom: wm_clip_sub
    test word [bx+W_FLAGS], 2   ; reads through BX and preserves it
    jz .next
    call wm_clip_sub            ; subtract it, shadow included. An emptied
    jc .over                    ; list needs no early exit - the subtractions
.next:                          ; that follow walk it zero times
    add bx, WIN_SIZE
    cmp bx, wm_wins + MAX_WIN*WIN_SIZE
    jb .win
.done:
    cmp word [wm_clip_n], 0     ; ZF = 1: wholly covered. clc leaves it alone,
    clc                         ; and so does every pop below
    jmp short .out
.over:
    mov word [wm_clip_n], 0
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; wm_clip_clear - disarm clipping
; in:  nothing
; out: nothing
; clobbers: flags
;
; gfx_unlock calls this, so a background painter never has to. It exists for
; the case of a task that wants to draw unclipped again inside the same lock
; hold - and it is what SPEC.md 11.3 rule 3 points at.
; -----------------------------------------------------------------------------
wm_clip_clear:
    mov word [wm_clip_n], 0
    ret

; -----------------------------------------------------------------------------
; wm_clip_sub - internal: subtract one window's occupied rect from the list
; in:  BX = the occluding window ptr; the list holds at least one rect
; out: CF = 1 the list overflowed (nothing about it is valid afterwards)
; clobbers: flags
;
; The occupied rect is (x, y) to (x+w, y+h) INCLUSIVE - the 1px drop shadow
; wm_draw_win puts at (+1,+1) is the caller's problem too, exactly as
; wm_obscured has always counted it. A fullscreen window has no shadow, but
; it covers the screen anyway and one row past the edge costs nothing:
; vga_rect_setup clips.
; -----------------------------------------------------------------------------
; wm_clip_subf - the FRAME rect instead: what wm_draw_win actually REPAINTS.
;
; The difference is two pixels and they matter to exactly one caller. The
; occupied rect above is the bounding box (x,y)..(x+w,y+h), so it includes the
; drop shadow's row AND column - but the shadow wm_draw_win draws is an **L**,
; starting at (+1,+1), so the top-right corner (x+w, y) and the bottom-left
; (x, y+h) are inside that box and are never painted by anything.
;
; For OCCLUSION that over-counting is harmless and conservative: it says
; "covered" about a pixel that is not, and a clipped primitive then declines to
; draw there. For SPEC.md 11.91.1's dither it is the wrong direction - "do not
; dither here, a window will cover it" about a pixel no window ever covers -
; and it leaves that pixel stale. It cost exactly one: a round trip that must
; come back byte for byte returned 1 differing pixel, at the restored window's
; top-right shadow corner.
;
; So the dither subtracts the frame only, and the shadow's L is dithered and
; then drawn over: a 1px double-draw down two edges, against a stale pixel.
; -----------------------------------------------------------------------------
; wm_clip_subx - whichever of the two wm_clip_occl's entry point asked for.
; One place decides, so the walk above cannot hold an opinion of its own.
; -----------------------------------------------------------------------------
wm_clip_subx:
    cmp byte [wm_clip_ofr], 0
    jne wm_clip_subf
    jmp short wm_clip_sub

wm_clip_subf:
    push ax
    mov ax, [bx+W_X]
    mov [wm_cl_ox1], ax
    add ax, [bx+W_W]
    dec ax
    mov [wm_cl_ox2], ax
    mov ax, [bx+W_Y]
    mov [wm_cl_oy1], ax
    add ax, [bx+W_H]
    dec ax
    mov [wm_cl_oy2], ax
    pop ax
    jmp short wm_clip_subr

wm_clip_sub:
    push ax
    mov ax, [bx+W_X]
    mov [wm_cl_ox1], ax
    add ax, [bx+W_W]
    mov [wm_cl_ox2], ax
    mov ax, [bx+W_Y]
    mov [wm_cl_oy1], ax
    add ax, [bx+W_H]
    mov [wm_cl_oy2], ax
    pop ax
wm_clip_subr:                   ; ...and the walk, from those four words
    push ax
    push si
    push di
    mov si, wm_clip_tab
    xor di, di                  ; DI = index of the rect under test
.rect:
    cmp di, [wm_clip_n]
    jae .ok
    mov ax, [si+WCR_X2]
    cmp ax, [wm_cl_ox1]
    jl .keep
    mov ax, [si+WCR_X1]
    cmp ax, [wm_cl_ox2]
    jg .keep
    mov ax, [si+WCR_Y2]
    cmp ax, [wm_cl_oy1]
    jl .keep
    mov ax, [si+WCR_Y1]
    cmp ax, [wm_cl_oy2]
    jg .keep
    call wm_clip_split          ; overlap: up to four fragments replace it
    jc .bad
    jmp short .rect             ; slot DI now holds the rect moved down into
                                ; it, so re-test the same index
.keep:
    inc di
    add si, WCR_SZ
    jmp short .rect
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_split - internal: replace one overlapped rect with its fragments
; in:  SI -> the rect (index DI), wm_cl_o* = the occluder, overlap proven
; out: CF = 1 the list is full
; clobbers: flags
;
; Removal is "move the last rect down into this slot and shorten the list",
; so the order of the list is not preserved - nothing depends on it. The
; fragments are appended at the end, where wm_clip_sub's own walk will reach
; them and find (by construction) that they do not touch the occluder.
; -----------------------------------------------------------------------------
wm_clip_split:
    push ax
    push bx
    mov ax, [si+WCR_X1]         ; the rect is about to be overwritten
    mov [wm_cl_rx1], ax
    mov ax, [si+WCR_Y1]
    mov [wm_cl_ry1], ax
    mov ax, [si+WCR_X2]
    mov [wm_cl_rx2], ax
    mov ax, [si+WCR_Y2]
    mov [wm_cl_ry2], ax

    mov ax, [wm_clip_n]         ; drop it: the last rect moves into its slot
    dec ax                      ; (a self-copy when it WAS the last)
    mov [wm_clip_n], ax
    call wm_clip_ptr            ; BX -> the last rect
    mov ax, [bx+WCR_X1]
    mov [si+WCR_X1], ax
    mov ax, [bx+WCR_Y1]
    mov [si+WCR_Y1], ax
    mov ax, [bx+WCR_X2]
    mov [si+WCR_X2], ax
    mov ax, [bx+WCR_Y2]
    mov [si+WCR_Y2], ax

    mov ax, [wm_cl_rx1]         ; above: full width, r.y1 .. o.y1-1
    mov [wm_cl_fx1], ax
    mov ax, [wm_cl_rx2]
    mov [wm_cl_fx2], ax
    mov ax, [wm_cl_ry1]
    mov [wm_cl_fy1], ax
    mov ax, [wm_cl_oy1]
    dec ax
    mov [wm_cl_fy2], ax
    call wm_clip_add
    jc .full
    mov ax, [wm_cl_oy2]         ; below: full width, o.y2+1 .. r.y2
    inc ax
    mov [wm_cl_fy1], ax
    mov ax, [wm_cl_ry2]
    mov [wm_cl_fy2], ax
    call wm_clip_add
    jc .full

    mov ax, [wm_cl_ry1]         ; the band the occluder shares with us
    cmp ax, [wm_cl_oy1]
    jge .my1
    mov ax, [wm_cl_oy1]
.my1:
    mov [wm_cl_fy1], ax
    mov ax, [wm_cl_ry2]
    cmp ax, [wm_cl_oy2]
    jle .my2
    mov ax, [wm_cl_oy2]
.my2:
    mov [wm_cl_fy2], ax
    mov ax, [wm_cl_rx1]         ; left of it
    mov [wm_cl_fx1], ax
    mov ax, [wm_cl_ox1]
    dec ax
    mov [wm_cl_fx2], ax
    call wm_clip_add
    jc .full
    mov ax, [wm_cl_ox2]         ; and right of it
    inc ax
    mov [wm_cl_fx1], ax
    mov ax, [wm_cl_rx2]
    mov [wm_cl_fx2], ax
    call wm_clip_add
    jc .full
    clc
    jmp short .out
.full:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_add - internal: append wm_cl_f* to the list if it is non-empty
; in:  wm_cl_fx1/fy1/fx2/fy2
; out: CF = 1 the list is full (nothing appended)
; clobbers: flags
; -----------------------------------------------------------------------------
wm_clip_add:
    push ax
    push bx
    mov ax, [wm_cl_fx1]
    cmp ax, [wm_cl_fx2]
    jg .empty
    mov ax, [wm_cl_fy1]
    cmp ax, [wm_cl_fy2]
    jg .empty
    mov ax, [wm_clip_n]
    cmp ax, WM_CLIP_MAX
    jae .full
    inc word [wm_clip_n]
    call wm_clip_ptr            ; BX -> the new slot
    mov ax, [wm_cl_fx1]
    mov [bx+WCR_X1], ax
    mov ax, [wm_cl_fy1]
    mov [bx+WCR_Y1], ax
    mov ax, [wm_cl_fx2]
    mov [bx+WCR_X2], ax
    mov ax, [wm_cl_fy2]
    mov [bx+WCR_Y2], ax
.empty:
    clc
    jmp short .out
.full:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_dw_cull - arm the CULL for one W_PAINT, if this pass asked for one
; in:  DI = the window being drawn; [wm_dmg_cullq] set by wm_dmg_wins
; out: nothing (every register and the flags preserved)
;
; SPEC.md 11.3.3. wm_dmg_wins redraws a marked window WHOLE and lets the ones
; above it paint over the part they own, which is correct and is SECONDS of
; visible rubbish on a 4.77MHz machine: measured on the Task Manager, a window
; dragged further over it made 118 primitive calls of which 117 landed inside
; the rect the mover was about to occupy, and 42,546 subpixels of its memory
; list were on the glass before the mover covered them.
;
; The region is the same one every background painter arms - the content rect
; minus the windows above - and what makes it safe HERE, where 11.97.1 says it
; is not, is that the cull rounds a glyph cell OUTWARD instead of dropping it.
; A cell the region cuts is painted whole; the pixels outside are somebody
; else's and are painted again in this same pass, a few instructions later.
; -----------------------------------------------------------------------------
wm_dw_cull:
    cmp byte [wm_dmg_cullq], 0
    je .out
    pushf
    push bx
    mov byte [wm_dmg_cull], 1   ; BEFORE the arm: wm_clip_set reads it, and
    mov bx, di                  ; a drop there would spend a cache wm_su_try
    call wm_clip_set            ; has already answered from (11.96.4)
    pop bx
    popf
.out:
    ret

; -----------------------------------------------------------------------------
; wm_clip_ptr - internal: clip-list index to rect pointer
; in:  AX = index
; out: BX = wm_clip_tab + index*WCR_SZ
; clobbers: flags
; -----------------------------------------------------------------------------
wm_clip_ptr:
    push cx
    mov bx, ax
    mov cl, 3                   ; WCR_SZ is 8, and it is meant to stay 8
    shl bx, cl
    add bx, wm_clip_tab
    pop cx
    ret

; -----------------------------------------------------------------------------
; wm_clip_test - may a whole rect be drawn?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; out: CF = 0 the rect lies wholly inside one clip fragment, or the clip is
;      disarmed; CF = 1 it does not - the caller must draw NOTHING
; clobbers: flags
;
; This is the answer for the two primitives that cannot draw a partial
; shape: font_char's 8x8 cell and ico_core's icon body (SPEC.md 6/25). Both
; already skip a glyph that would cross a SCREEN edge; a clip edge is the
; same decision one region in.
; -----------------------------------------------------------------------------
wm_clip_test:
    cmp word [wm_clip_n], 0
    je wm_clip_free             ; ...NOT a local label: the walk below is its
                                ; own symbol now, so this one would belong to
                                ; it rather than to the entry above
    cmp byte [wm_dmg_cull], 0
    je wm_clip_t1               ; SPEC.md 11.3.3: THE CULL ROUNDS OUTWARD, and
    xchg ax, cx                 ; containment with the corners exchanged IS
    xchg bx, dx                 ; overlap - the walk below is four compares
    call wm_clip_t1             ; that read x1 against the fragment's left and
    pushf                       ; x2 against its right, so swapping the pair
    xchg ax, cx                 ; asks the other question with no second copy
    xchg bx, dx                 ; of the arithmetic. Put back, because
    popf                        ; font_char takes its CX/DX from AX/BX after
    ret                         ; this returns
wm_clip_t1:
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; SPEC.md 39.14.4: inside a whole-shape hook
    je wm_clip_walk             ; the caller's rect has already been moved into
                                ; its display, and wm_clip_tab is VIRTUAL. One
                                ; rect translated here beats sixteen there,
                                ; and it is the only place the two spaces meet
    add ax, [vid_ox]
    add cx, [vid_ox]
    add bx, [vid_oy]
    add dx, [vid_oy]
    call wm_clip_walk
    pushf                       ; ...and back, because font_char takes its
    sub ax, [vid_ox]            ; CX/DX from AX/BX after this returns - and
    sub cx, [vid_ox]            ; `sub` sets the flags this routine answers in
    sub bx, [vid_oy]
    sub dx, [vid_oy]
    popf
    ret
%endif
wm_clip_walk:
    push si
    push di
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
.frag:
    cmp ax, [si+WCR_X1]
    jl .next
    cmp cx, [si+WCR_X2]
    jg .next
    cmp bx, [si+WCR_Y1]
    jl .next
    cmp dx, [si+WCR_Y2]
    jle .in
.next:
    add si, WCR_SZ
    dec di
    jnz .frag
    stc
    jmp short .out
.in:
    clc
.out:
    pop di
    pop si
    ret
wm_clip_free:
    clc
    ret

; -----------------------------------------------------------------------------
; wm_clip_rows - which ROWS of a cell may be drawn? (SPEC.md 11.3.2)
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive; at most 255 rows tall)
; out: CF = 0 and [wm_clip_r0] = the first drawable row as an offset from y1,
;      [wm_clip_rn] = how many rows from there; CF = 1 not one row may be
;      drawn - the caller must draw NOTHING, exactly as wm_clip_test says
; clobbers: flags (every register preserved)
;
; wm_clip_test asks "may this WHOLE shape be drawn", which is the only
; question a glyph could answer while its renderer drew eight rows or none.
; A window edge crossing a line of text horizontally cuts every cell in it,
; so that answer is no for the whole line and the visible slice of the text
; is left STALE - a running Timer with its digits half covered stopped at the
; second the covering window landed and stayed there (SPEC.md 14.4).
;
; A glyph cannot draw half a CELL, but it can draw whole ROWS of one, and a
; horizontal cut is exactly the case where the rows either side of the edge
; are each entirely in or entirely out. So this is the same walk with the
; y test relaxed into an intersection: the fragment must still cover the
; cell's FULL WIDTH - a vertical cut is refused here as it always was,
; because half a row of a cell is the thing the renderers cannot express -
; and what comes back is the tallest run of rows one fragment can offer.
;
; ONE fragment, not the union of several: the region is a rect LIST and two
; fragments can offer disjoint slices of the same cell, which is not a row
; range. Taking the best single fragment can only ever return FEWER rows than
; are strictly visible, so the answer is always safe to draw and never draws
; outside the region - which is the direction an error has to point.
; -----------------------------------------------------------------------------
wm_clip_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov byte [wm_clip_r0], 0
    cmp word [wm_clip_n], 0
    je .all                     ; disarmed: every row, the same answer
                                ; wm_clip_free gives for the whole shape
    cmp byte [wm_dmg_cull], 0
    je .norows                  ; SPEC.md 11.3.3: under the cull a cell that
    call wm_clip_test           ; overlaps at all is drawn WHOLE, so the row
    jc .none                    ; range is every row or none - the partial
    jmp short .all              ; answer this routine exists to give is what
.norows:                        ; the cull is defined not to need
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; SPEC.md 39.14.4, exactly as wm_clip_test:
    je .armed                   ; inside a whole-shape hook the caller's rect
    add ax, [vid_ox]            ; is in its display's space and wm_clip_tab is
    add cx, [vid_ox]            ; virtual. No translation BACK is needed here -
    add bx, [vid_oy]            ; every register is restored from the stack and
    add dx, [vid_oy]            ; a row OFFSET is the same in both spaces
.armed:
%endif
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
    xor bp, bp                  ; BP = the best row count found so far
.frag:
    cmp ax, [si+WCR_X1]         ; the cell's full width, or nothing: a
    jl .next                    ; part-width row is what cannot be drawn
    cmp cx, [si+WCR_X2]
    jg .next
    push ax
    push cx
    mov ax, bx                  ; AX = max(y1, frag y1)
    cmp ax, [si+WCR_Y1]
    jge .lo
    mov ax, [si+WCR_Y1]
.lo:
    mov cx, dx                  ; CX = min(y2, frag y2)
    cmp cx, [si+WCR_Y2]
    jle .hi
    mov cx, [si+WCR_Y2]
.hi:
    sub cx, ax
    jl .frag_no                 ; they do not overlap at all
    inc cx                      ; CX = rows this fragment offers
    cmp cx, bp
    jbe .frag_no
    mov bp, cx
    sub ax, bx                  ; ...and where they start, from y1
    mov [wm_clip_r0], al
.frag_no:
    pop cx
    pop ax
.next:
    add si, WCR_SZ
    dec di
    jnz .frag
    or bp, bp
    jz .none
    mov ax, bp                  ; a BYTE store: [wm_clip_rn] is a db, and
    mov [wm_clip_rn], al        ; `mov [wm_clip_rn], bp` writes a WORD - it
                                ; put BP's zero high byte over the first
                                ; instruction of wm_clip_set, which is the
                                ; next thing in .text. AX is dead here: it is
                                ; restored from the stack four lines down
    jmp short .ok
.all:
    mov ax, dx
    sub ax, bx
    inc ax
    mov [wm_clip_rn], al
.ok:
    clc                         ; POP leaves the flags alone, so the answer
    jmp short .out              ; is set before the epilogue rather than after
.none:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_ptr2idx - window record pointer to index (public, SPEC.md 11)
; in:  BX = window ptr (record-aligned)
; out: AL = window index, AH = 0
; clobbers: nothing else
; -----------------------------------------------------------------------------
wm_ptr2idx:
    push cx
    mov ax, bx
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index...
    xor ah, ah                  ; ...and AH IS THE REMAINDER, NOT ZERO. This
                                ; line used to be a comment saying "AH = 0: ptr
                                ; is record-aligned", which is a statement about
                                ; the CALLER. wm_su_slot doubles the whole of AX
                                ; to reach a word per window, so one unaligned
                                ; pointer put its slot 0x566 past the table and
                                ; wm_su_drop zeroed two bytes of the kernel's
                                ; own .text with it (SPEC.md 11.96.11.4). A
                                ; documented output that is only true when the
                                ; input is right is not a documented output
    cmp al, MAX_WIN             ; ...and the INDEX IS BOUNDED, because every
    jb .ok                      ; per-window table in this module is addressed
    mov al, MAX_WIN - 1         ; off it: wm_su_segs, wm_su_ext, the size
.ok:                            ; preferences. A pointer that is not a window
    pop cx                      ; then names the LAST window rather than a byte
    ret                         ; of .text, which is wrong and bounded instead
                                ; of wrong and unbounded

; -----------------------------------------------------------------------------
; wm_idx2ptr - window index to record pointer (module internal)
; in:  AL = window index
; out: BX = wm_wins + index*WIN_SIZE
; -----------------------------------------------------------------------------
wm_idx2ptr:
    push ax
    push cx
    mov ah, 0                   ; index * WIN_SIZE, spelled as a real 8-bit
    mov cl, WIN_SIZE            ; multiply rather than a sum of shifts: the
    mul cl                      ; stride has already changed twice (16 -> 18
    mov bx, ax                  ; -> 20, SPEC.md 11) and a shift idiom is a
    add bx, wm_wins             ; silent breakage every time it does
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_ownseg - which SEGMENT owns window slot AL (SPEC.md 11.96.3.1)
; in:  AL = a window slot
; out: CF = 1 that slot holds no live window, DX unspecified;
;      CF = 0 and DX = the owning segment - [W_SEG], or KERNEL_SEG when that
;      word is 0, which is what a window the KERNEL created carries
; clobbers: DX and the flags, nothing else
;
; SPEC.md 11.96.3 says a raise cache's owner tag is MEM_P_WSAVE plus the
; window's SLOT, and until now nothing outside this module could turn that
; slot back into anything - which is why the Task Manager's heap page could
; only ever print `WinSave` and not whose. This is the door for it.
;
; A SEGMENT and not the record pointer, on purpose: a package holding another
; window's record could pass it back through any wm_* cell, and a segment is
; the whole of what the two questions worth asking need. `Is that cache mine`
; is a compare against the caller's own DS, and `whose is it` is a match
; against the instance bases a claim walker already has - the Task Manager
; keeps exactly that in tm_ispt. The bss array of the same idea one line down
; (`wm_owner`, SPEC.md 29) answers in INSTANCE indices instead, and is not
; what is published here: an instance index means nothing to a caller that
; has not also snapshotted the instance table, and a segment means the same
; thing to everyone.
;
; The slot is bounded here and the USED bit tested, because both are the
; caller's word for it: a free record's W_SEG is a previous tenant's leftover
; (wm_destroy_seg says so) and would name a package that is no longer running.
; -----------------------------------------------------------------------------
wm_ownseg:
    push bx
    cmp al, MAX_WIN
    jae .no
    call wm_idx2ptr             ; BX = the record; AL is preserved
    test word [bx+W_FLAGS], 1
    jz .no
    mov dx, [bx+W_SEG]
    or dx, dx
    jnz .yes
    mov dx, KERNEL_SEG          ; 0 means the kernel made it (wm_seg_of's rule)
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; wm_tpen_bg / wm_tpen_ink - a title bar's ground / ink (SPEC.md 76.12)
; in:  DI = the window, BP = the frontmost one (wm_draw_title's own registers)
; out: [gfx_color]; clobbers flags
;
; The ACTIVE pair when this window has the focus and the chrome pair when it
; has not. In Bright and Dark the two pairs are equal, so this is one compare
; and the same pixels; under Color it is the whole feature - a blue bar with a
; white caption against a grey one with a black caption, which is what makes
; focus readable across a screen without reading any text.
;
; A TAIL CALL is legal here and both targets are NEAR: their `ret` returns to
; this routine's caller, and the frame has nothing to undo. It would not be
; legal to a cw_ shim, which ends in `retf` (SPEC.md 2.6).
%ifdef KERN_BIG
wm_tpen_bg:
    cmp di, bp
    je thm_tbg
    jmp thm_bg
wm_tpen_ink:
    cmp di, bp
    je thm_tink
    jmp thm_ink
%else                           ; no Color, so the two pairs are one pair and
wm_tpen_bg  equ thm_bg          ; the focus question has no answer to give
wm_tpen_ink equ thm_ink
%endif

; -----------------------------------------------------------------------------
; wm_draw_title - one window's title bar, in the form its rank calls for
; in:  DI = window ptr, BP = the frontmost visible window (or 0); gfx lock
;      held by the caller
; out: nothing (all registers preserved)
;
; Split out of wm_draw_win because it is the ONLY part of a window that
; changes when some OTHER window is raised: the pinstripes, the close box
; and the minimize box belong to the frontmost window alone (DI == BP), so
; the outgoing front window needs exactly this rect redrawn and nothing
; else. wm_show's fast path (SPEC.md 11.90) is the caller that cares; every
; other path reaches it through wm_draw_win and never notices.
; -----------------------------------------------------------------------------
%ifdef BANDCOMP
; -----------------------------------------------------------------------------
; wm_title_band - the .text half of SPEC.md 11.101's composed title bar
; in:  DI = the window, BP = the frontmost one; the gfx lock held
; out: CF = 0 the interior was drawn, CF = 1 it was not and the caller must
;      draw it the old way. SI is SPENT; every other register is preserved.
;
; EVERY CROSSING INTO .text IS HERE, which is the whole reason this half
; exists: wm_flush, thm_tbg/thm_tink, wm_strseg and font_width_x are all
; resident, and a .cold body near-calling one of them is SPEC.md 5.4.2.1's
; defect - tools/os88ovlchk.py caught exactly that on this file's first build.
; So the prologue resolves the four facts the picture needs, writes them where
; the body can read them, and far-calls it.
;
; It also draws the RAGGED EDGES. gfx_blit1 takes a multiple of 8 at both ends
; (SPEC.md 5.4.2) and a window's frame x is aligned only when it asked for
; WF_SNAP - which aligns its CONTENT, so W_X is 7 mod 8 when it did. The up to
; 7 pixels at either end are one thin fill each and cannot be composed without
; painting desktop.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; wm_tsend - the PINSTRIPE FRAGMENTS inside one ragged end (SPEC.md 11.101.1)
; in:  AX = the end's first column, CX = its last (both already known
;      non-empty), DI = the window, BP = the frontmost, BX/DX = the bar's
;      interior rows, and [gfx_color] the bar's GROUND
; out: nothing; every register preserved and the ground pen put back
;
; A ragged end is NOT just ground, which is what the first composed bar
; assumed: the stripes run from W_X+3 to W_X+W_W-4 and the byte-aligned span
; stops up to 7 pixels short of both, so filling the ends flat loses three or
; four pixels off the end of every stripe. Six dashes that stop before the bar
; does - small, and plainly visible beside a window drawn the old way.
;
; WHAT SURVIVES OUT THERE IS TWO THREE-PIXEL RUNS, not the whole stripe span.
; The primitive path draws each stripe full width and then paints the two
; boxes' break fills over it, so all that is left near either edge is
; W_X+3..W_X+5 and W_X+W_W-6..W_X+W_W-4. A fragment drawn without that
; knowledge puts a stripe pixel back inside the minimize box's break, which is
; how this was found the SECOND time.
;
; It is six more primitive calls at each end that holds one, and that is the
; honest price of a band which cannot reach the last byte - the clearest
; single statement of what a composer costs at a boundary it cannot cross.
; -----------------------------------------------------------------------------
wm_tsend:
    cmp di, bp
    jne .out                    ; an inactive bar has no stripes to lose
%ifdef KERN_BIG
    cmp byte [thm_strp], 0      ; ...and neither has a SOLID theme (76.12)
    je .out
%endif
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov bp, ax                  ; BP/SI = the end zone, banked while AX/CX
    mov si, cx                  ; carry each run in turn
    mov ax, [di+W_X]            ; the run left of the close box's break fill
    add ax, 3
    mov cx, ax
    add cx, 2
    call .run
    mov ax, [di+W_X]            ; ...and the one right of the minimize box's
    add ax, [di+W_W]
    sub ax, 6
    mov cx, ax
    add cx, 2
    call .run
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; .run - stripe run AX..CX, clipped to the end zone BP..SI and drawn if any of
;        it is left. AX and CX are spent; BX/DX/SI/BP are not.
.run:
    cmp ax, bp
    jge .rl
    mov ax, bp
.rl:
    cmp cx, si
    jle .rr
    mov cx, si
.rr:
    cmp ax, cx
    jg .rout                    ; this end holds none of this run
    push bx
    push dx
    push cx                     ; the run's right end, for gfx_hline's BX
    call thm_tink
    pop bx
    mov dx, [di+W_Y]
    add dx, 3
    mov cx, 6                   ; six stripes, rows y+3,5,7,9,11,13
.r1:
    call gfx_hline
    add dx, 2
    loop .r1
    call thm_tbg                ; ...and the ground back, for the next run
    pop dx
    pop bx
.rout:
    ret


wm_title_band:
    push ax
    push bx
    push cx
    push dx
    push es
    cmp word [bnd_seg], 0       ; no buffer, no composer (SPEC.md 5.9.2): band_
    je .no                      ; open_x would refuse anyway, but the two ragged
                                ; ends below would have been drawn first and
                                ; then drawn again by the fallback
    mov bx, di                  ; the bar's own edges, x+b .. x+w-1-b, and on
    call wm_bordr               ; a window that spans the screen b is 0 at BOTH
    mov cx, [di+W_X]            ; of them (SPEC.md 11.95.3)
    add cx, [di+W_W]
    dec cx
    sub cx, ax
    mov [wmt_hi], cx
    call wm_bord                ; ...the left one with BX still the window
    add ax, [di+W_X]
    mov [wmt_lo], ax
    add ax, 7                   ; ...and the aligned span inside them
    and ax, 0FFF8h
    mov [wmt_x0], ax
    inc cx
    and cx, 0FFF8h
    dec cx
    mov [wmt_x1], cx
    cmp ax, cx
    jg .no                      ; a bar with no whole byte in it at all

    call wm_tpen_bg             ; the pair, read back off the pen: a KERNEL
    mov ah, [gfx_color]         ; may do that (SPEC.md 6.6.5). FOCUS-RESOLVED,
    call wm_tpen_ink            ; through the same two names the fallback path
    mov al, [gfx_color]         ; uses (76.12) - this word is the band's paper
    mov [wmt_pair], ax          ; AL = ink, AH = paper. AND the ink of its
                                ; stripes, its boxes and its caption, so going
                                ; straight to thm_tbg/thm_tink here painted
                                ; every UNFOCUSED bar in the Color theme blue
                                ; with a white caption while the body's own
                                ; `cmp di, bp` correctly withheld the stripes
                                ; and the boxes. DI and BP are still this
                                ; routine's arguments here: nothing above
                                ; touches either, and wm_flush, wm_strseg and
                                ; the fills all preserve them. On kern_small
                                ; the two names are equ'd to the chrome pair
                                ; (76.13), so both builds read the same

    mov si, [di+W_TITLE]
    or si, si
    jz .noname
    call wm_strseg              ; ES = the OWNER's segment, and it stays that
    call font_width_x           ; way into the body, which letters through it
    mov [wmt_tw], ax
    mov cx, [di+W_W]
    sub cx, ax
    sar cx, 1                   ; SIGNED: a title wider than its window
    add cx, [di+W_X]
    mov [wmt_tx], cx
.noname:
    call wm_tpen_bg             ; the two ragged ends, in the bar's ground -
                                ; the SAME ground the band is about to be
                                ; blitted in, so it asks the same question
    mov bx, [di+W_Y]
    inc bx
    mov dx, [di+W_Y]
    add dx, TITLE_H-2
    mov ax, [wmt_lo]
    mov cx, [wmt_x0]
    dec cx
    cmp ax, cx
    jg .rright
    call gfx_fill
    call wm_tsend               ; ...and what the ground fill just flattened
.rright:
    mov ax, [wmt_x1]
    inc ax
    mov cx, [wmt_hi]
    cmp ax, cx
    jg .band
    call gfx_fill
    call wm_tsend
.band:
    call COLD_SEG:wm_title_band_x
    jmp short .out
.no:
    stc
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

wm_draw_title:
%ifdef SNAPAUDIT
    ; SPEC.md 11.94.2: THE CAPTION IS CHROME. The pen is centred in the title
    ; bar by the kernel and no application can influence it, so attributing it
    ; to whichever callback bracket happens to be open is wrong twice - it
    ; reported a window's own caption as the app's off-grid text, and it
    ; reported OTHER windows' captions the same way when a callback caused a
    ; repaint. Suspending the attribution here fixes both by construction,
    ; because it keys on who CHOSE the pen rather than on who is on the stack.
    ; No pushf: push, pop and mov write no flags, and the test below sets them.
    push ax
    mov ax, [snap_cur]
    mov [snap_tsave], ax
    mov word [snap_cur], 0
    pop ax
%endif
    test word [di+W_FLAGS], WF_FULL
    jnz .none                   ; a fullscreen window HAS no title bar
                                ; (SPEC.md 11.2), and drawing one anyway put
                                ; a full-width white strip with a centred
                                ; caption across the top of its content. It
                                ; never showed while wm_fast_ok sent every
                                ; fullscreen case to wm_paint_all instead;
                                ; the guard belongs here rather than at each
                                ; caller, because "no title bar" is a fact
                                ; about the window and not about the path
    push ax
    push bx
    push cx
    push dx
    push si
%ifdef BANDCOMP
    call wm_title_band          ; SPEC.md 11.101: the interior as ONE PICTURE,
    jnc .sep                    ; composed and blitted - the `BAND=1` path, a
%endif                          ; KNOB again since 5.9.6. Everything below is
                                ; what every OTHER build draws, kern_small
                                ; included, and what a BAND=1 kernel itself
                                ; falls back to: a bar with no whole byte in
                                ; it, or a machine whose heap refused the 2KB
                                ; claim
    call wm_tpen_bg          
    mov bx, di                  ; title bar interior, 16 white rows - reaching
    call wm_bordr               ; BOTH edge columns on a window with no side
    mov cx, [di+W_X]            ; borders, or a 1px strip of desktop shows
    add cx, [di+W_W]            ; through it (SPEC.md 11.95.3). The right edge
    dec cx                      ; first, because BX is still the window there
    sub cx, ax
    call wm_bord
    add ax, [di+W_X]
    mov bx, [di+W_Y]
    inc bx
    mov dx, [di+W_Y]
    add dx, TITLE_H-2
    call gfx_fill

    cmp di, bp                  ; pinstripes + close box: frontmost only
    jne .title
    call thm_tink               ; the stripes and the boxes are the ACTIVE
                                ; bar's, so they take its ink - which is the
                                ; same black both 1bpp themes already used
%ifdef KERN_BIG
    cmp byte [thm_strp], 0      ; ...and a SOLID bar has none at all (SPEC.md
    je .boxes                   ; 76.12): six gfx_hlines in the ground colour
                                ; would draw nothing for ~4.5 ms of the field
                                ; machine's time, so this is a skip and not a
%endif                          ; recolour. Only Color sets it, so only
                                ; kern_big can ever take the branch
    mov dx, [di+W_Y]            ; 6 stripes, rows y+3,5,7,9,11,13
    add dx, 3
    mov cx, 6
.stripe:
    mov ax, [di+W_X]            ; inset 3px from each side
    add ax, 3
    mov bx, [di+W_X]
    add bx, [di+W_W]
    sub bx, 4
    call gfx_hline
    add dx, 2
    loop .stripe

.boxes:
    call thm_tbg             
    mov ax, [di+W_X]            ; break the stripes 2px around the close box
    add ax, 6
    mov bx, [di+W_Y]
    add bx, 2
    mov cx, [di+W_X]
    add cx, 20
    mov dx, [di+W_Y]
    add dx, 16
    call gfx_fill

    call thm_tink             
    mov ax, [di+W_X]            ; close box: 11x11 black frame, white inside
    add ax, 8                   ; (interior is white from the break fill)
    mov bx, [di+W_Y]
    add bx, 4
    mov cx, [di+W_X]
    add cx, 18
    mov dx, [di+W_Y]
    add dx, 14
    call gfx_frame

    call thm_tbg             
    mov ax, [di+W_X]            ; break the stripes around the minimize box
    add ax, [di+W_W]            ; (mirror of the close-box break, SPEC.md 11)
    sub ax, 21
    mov bx, [di+W_Y]
    add bx, 2
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 7
    mov dx, [di+W_Y]
    add dx, 16
    call gfx_fill

    call thm_tink             
    mov ax, [di+W_X]            ; minimize box: 11x11 black frame at
    add ax, [di+W_W]            ; W_X+W_W-19 .. W_X+W_W-9, rows +4..+14
    sub ax, 19
    mov bx, [di+W_Y]
    add bx, 4
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 9
    mov dx, [di+W_Y]
    add dx, 14
    call gfx_frame

    mov ax, [di+W_X]            ; collapse glyph: black bar across row
    add ax, [di+W_W]            ; W_Y+9, cols W_X+W_W-17 .. W_X+W_W-11
    sub ax, 17
    mov bx, [di+W_X]
    add bx, [di+W_W]
    sub bx, 11
    mov dx, [di+W_Y]
    add dx, 9
    call gfx_hline

.title:
    mov si, [di+W_TITLE]        ; centered title on a 6px white gap
    test si, si
    jz .sep                     ; no title string: nothing more in the bar,
                                ; but the separator under it is still owed
    push es                     ; the title is the OWNER's string, so it is
    call wm_strseg              ; read through ES: the package's segment for
    call font_width_x           ; a package window, ours for everything else
    pop es
    mov cx, [di+W_W]
    sub cx, ax
    sar cx, 1
    add cx, [di+W_X]            ; CX = text left
%ifdef TITLESNAP
    ; docs/TEXT-PLAN.md 6.1: centre to the nearest CELL rather than to the
    ; exact pixel. A window title is the hottest text in the system - it
    ; redraws on every window operation - and centring puts its pen off the
    ; 8px grid seven times in eight, which costs SPEC.md 6.1's fast path and
    ; (since Stage 1) 2.8x on VGA. The title moves by at most 4 px, so this
    ; is a LOOK question and lives on a knob until somebody has looked.
    ;
    ; The +4 is what makes it NEAREST rather than leftward, and it is correct
    ; on a title wider than its window too: `sar cx, 1` is signed, so CX can
    ; be left of W_X, and (cx + 4) AND ~7 rounds a two's-complement negative
    ; the same way it rounds a positive one.
    ;
    ; The white gap below is computed FROM this pen, so it moves with the
    ; text and stays centred on it - the gap is not separately snapped, and
    ; must not be: it is 6px of margin either side of wherever the glyphs
    ; actually land.
    add cx, 4
    and cx, 0FFF8h
%endif
    push cx
    add ax, cx                  ; AX = gap right = text left + width + 5
    add ax, 5
    xchg ax, cx                 ; AX = text left, CX = gap right
    sub ax, 6                   ; AX = gap left
    mov bx, [di+W_Y]
    add bx, 2
    mov dx, [di+W_Y]
    add dx, 15
    call wm_tpen_bg          
    call gfx_fill               ; white gap (covers the stripe rows)
    call wm_tpen_ink          
    pop cx
    mov dx, [di+W_Y]
    add dx, 5                   ; 8px glyphs centered in the 16-row bar
    push es
    call wm_strseg
    call font_str_x
    pop es
.sep:
    ; The separator under the bar, drawn LAST and by both paths. It is row
    ; TITLE_H-1 and the interior stops at TITLE_H-2, so moving it off the
    ; front of this routine changes no pixel - and it reaches the ragged ends
    ; a band cannot (SPEC.md 11.95.2/11.101).
    call thm_ink
    mov bx, di                  ; SPEC.md 11.95.3: x+b .. x+w-1-b
    call wm_bordr
    mov cx, [di+W_X]
    add cx, [di+W_W]
    dec cx
    sub cx, ax
    call wm_bord
    add ax, [di+W_X]
    mov bx, cx                  ; ...and gfx_hline wants x2 in BX, which is
    mov dx, [di+W_Y]            ; why the window pointer had to be done with
    add dx, TITLE_H-1
    call gfx_hline
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
%ifdef KERN_BIG
    call wm_chrome_relit        ; SPEC.md 13.8.1: a box being HELD DOWN stays
                                ; down across a repaint, and a window that just
                                ; lost its boxes stops claiming one is lit. The
                                ; common tail, so both exits pay it - the
                                ; fullscreen early-out above included, which is
                                ; the case that has no title bar at all
%endif
%ifdef SNAPAUDIT
    push ax
    mov ax, [snap_tsave]
    mov [snap_cur], ax
    pop ax
%endif
    ret

%ifdef KERN_BIG
section .cold                   ; the two chrome-box helpers are COLD (SPEC.md
                                ; 2.6): they are a geometry table and a repaint
                                ; hook, neither on a path that runs per frame,
                                ; and .text's rung had 204 bytes left when this
                                ; feature arrived wanting 232. Their public
                                ; names are thunks in kernel.asm and the bodies
                                ; are _x, which is the file modules' shape.
                                ; wm_chrome_relit's GUARD stays resident, up in
                                ; the thunk, so the common case - nothing is
                                ; held down - is still a compare and a near ret
                                ; on every title draw in the system

%ifdef BANDCOMP
; =============================================================================
; wm_title_band_x - THE TITLE BAR, COMPOSED (SPEC.md 11.101)
;
; in:  DI = the window, BP = the frontmost one (DI == BP is "active"), and the
;      four words the .text prologue resolved for it - see wm_title_band, which
;      is where every crossing into .text lives so that this body has none
;      (SPEC.md 5.4.2.1). ES:SI = the title string, in the OWNER's segment.
; out: CF = 0 the bar's interior was drawn whole, CF = 1 nothing was and the
;      caller must draw it the old way
;
; The bar is ONE PICTURE - ground, pinstripes, both boxes, the collapse mark
; and the caption - composed into a band and blitted. It used to be up to
; fifteen primitive calls with the caption's own rows written FOUR times
; (the bar fill, the stripes across them, the white gap over those, and the
; glyphs), and the gap between the third and the fourth is SPEC.md 6.6's
; double-draw at the top of every window in the system.
;
; THE BAND IS REPLAYED, NOT SPLIT. The op list below is emitted once per chunk
; and every op clips itself, so a box or a glyph that straddles two chunks is
; composed into both without this routine knowing where the seams are.
;
; THE RAGGED EDGES ARE THE CALLER'S. gfx_blit1 takes a multiple of 8 at both
; ends (SPEC.md 5.4.2) and a window's frame x is only aligned when it asked for
; WF_SNAP - and that aligns its CONTENT, so W_X is 7 mod 8 when it does. The
; up-to-7px at either end is left to two thin gfx_fills up in the prologue,
; which is one call each and cannot be composed without painting desktop.
; =============================================================================
wm_title_band_x:
    push ax                     ; SI is NOT banked: the prologue's contract
    push bx                     ; says it is spent, and this body reads it as
    push cx                     ; the caption string all the way to the end
    push dx
    push di
    push bp
    mov ax, [wmt_pair]          ; the bar's two colours, BEFORE the first band
    call band_pen_x             ; opens: on 1bpp they decide which way up the
                                ; bits are stored (SPEC.md 5.9.3)
    mov ax, [wmt_x0]            ; the aligned span this routine may blit
    mov [wmt_cx], ax
.chunk:
    mov ax, [wmt_x1]
    sub ax, [wmt_cx]
    inc ax                      ; AX = pixels left to draw
    jle .done
    cmp ax, BAND_MAXW
    jbe .cw
    mov ax, BAND_MAXW
.cw:
    push ax
    mov cx, ax
    mov ax, [wmt_cx]
    mov bx, [di+W_Y]
    inc bx                      ; the interior starts one row under the frame
    mov dx, TITLE_H - 2         ; ...and is 16 of them
    call band_open_x
    pop ax
    jc .fail
    add [wmt_cx], ax            ; the next chunk starts after this one

    ; --- NO GROUND FILL. band_open_x clears the band to PAPER and paper is
    ; what the blit turns into the bar's ground, so the whole interior is
    ; already drawn the instant the band opens. It used to be filled again
    ; here, which is 16 rows a chunk of work to reach the state it was
    ; handed - PERFORMANCE.md Set 88.
    cmp di, bp                  ; pinstripes, boxes and the collapse mark are
    jne .caption                ; the ACTIVE bar's alone (SPEC.md 11)
    mov al, 1
    call band_ink_x
%ifdef KERN_BIG
    cmp byte [thm_strp], 0      ; ...and a SOLID bar has no stripes (76.12)
    je .boxes
%endif
    mov dx, [di+W_Y]            ; 6 stripes, rows y+3,5,7,9,11,13
    add dx, 3
    mov cx, 6
.stripe:
    mov ax, [di+W_X]
    add ax, 3                   ; inset 3px from each side
    mov bx, [di+W_X]
    add bx, [di+W_W]
    sub bx, 4
    call band_hline_x
    add dx, 2
    loop .stripe
.boxes:
    xor al, al                  ; break the stripes 2px around the close box
    call band_ink_x
    mov ax, [di+W_X]
    add ax, 6
    mov bx, [di+W_Y]
    add bx, 2
    mov cx, [di+W_X]
    add cx, 20
    mov dx, [di+W_Y]
    add dx, 16
    call band_fill_x
    mov al, 1
    call band_ink_x
    mov ax, [di+W_X]            ; close box: an 11x11 frame, paper inside
    add ax, 8
    mov bx, [di+W_Y]
    add bx, 4
    mov cx, [di+W_X]
    add cx, 18
    mov dx, [di+W_Y]
    add dx, 14
    call band_frame_x

    xor al, al                  ; ...and the mirror of both around the
    call band_ink_x             ; minimize box
    mov ax, [di+W_X]
    add ax, [di+W_W]
    sub ax, 21
    mov bx, [di+W_Y]
    add bx, 2
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 7
    mov dx, [di+W_Y]
    add dx, 16
    call band_fill_x
    mov al, 1
    call band_ink_x
    mov ax, [di+W_X]
    add ax, [di+W_W]
    sub ax, 19
    mov bx, [di+W_Y]
    add bx, 4
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 9
    mov dx, [di+W_Y]
    add dx, 14
    call band_frame_x
    mov ax, [di+W_X]            ; the collapse mark
    add ax, [di+W_W]
    sub ax, 17
    mov bx, [di+W_X]
    add bx, [di+W_W]
    sub bx, 11
    mov dx, [di+W_Y]
    add dx, 9
    call band_hline_x

.caption:
    or si, si
    jz .emit                    ; no title string: the bar is finished
    cmp di, bp
    jne .glyphs                 ; an INACTIVE bar has no stripes, so there is
                                ; nothing under the caption to break and the
                                ; run's own cells are the whole of its ground
    xor al, al                  ; the 6px white gap either side of the glyphs,
    call band_ink_x             ; which is what breaks the stripes for them
    mov ax, [wmt_tx]
    sub ax, 6
    mov bx, [di+W_Y]
    add bx, 2
    mov cx, [wmt_tx]
    add cx, [wmt_tw]
    add cx, 5
    mov dx, [di+W_Y]
    add dx, 15
    call band_fill_x
.glyphs:
    mov al, 1
    call band_ink_x
    mov cx, [wmt_tx]
    mov dx, [di+W_Y]
    add dx, 5                   ; 8px glyphs centred in the 16-row bar
    call band_run_x
.emit:
    call band_emit_x            ; the colours are band_pen_x's, up at the top
    jc .fail
    jmp .chunk
.done:
    clc
    jmp short .out
.fail:
    stc
.out:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; wmt_barrect - the bar's interior rect, which the ground fill and nothing
; else wants. AX/BX/CX/DX; DI = the window.
wmt_barrect:
    mov ax, [di+W_X]
    mov bx, [di+W_Y]
    inc bx
    mov cx, [di+W_X]
    add cx, [di+W_W]
    sub cx, 2
    mov dx, [di+W_Y]
    add dx, TITLE_H-2
    ret
%endif                          ; BANDCOMP

; -----------------------------------------------------------------------------
; wm_chrome_rect_x - the INTERIOR of a title-bar box (SPEC.md 13.8.1)
; in:  SI = window, AH = wm_hit's region (2 = close box, 3 = minimize box)
; out: AX, BX, CX, DX = x1, y1, x2, y2 inclusive; SI preserved
;
; The 11x11 boxes wm_draw_title lays down, inset by their own frame - so this
; is the 9x9 white middle and an inversion of it leaves the frame standing.
; ONE description of that geometry, which is SPEC.md 20.5.1's rect-pointer
; discipline reaching a control the shared painter does not own: the boxes are
; drawn inline up in wm_draw_title and hit-tested in wm_hit, and this is the
; third reader rather than a third copy of the arithmetic.
;
; THE REGION IS TESTED BEFORE AX IS LOADED, and that is not a style choice: AH
; is the argument and AX is about to be a coordinate, so the compare has to
; happen first. It sits between the cmp and the je because `mov` writes no
; flags - the 8086 idiom, and the alternative is a push and a pop.
; -----------------------------------------------------------------------------
wm_chrome_rect_x:
    mov bx, [si+W_Y]
    mov dx, bx
    add bx, 5                   ; rows W_Y+5 .. W_Y+13 for both boxes
    add dx, 13
    cmp ah, 3
    mov ax, [si+W_X]            ; (writes no flags: the cmp above still stands)
    je .min
    add ax, 9                   ; close box interior: W_X+9 .. W_X+17
    mov cx, ax
    add cx, 8
    ret
.min:
    add ax, [si+W_W]
    sub ax, 18                  ; minimize box: the close box mirrored
    mov cx, ax                  ; (W_X+W_W-18 .. W_X+W_W-10)
    add cx, 8
    ret

; -----------------------------------------------------------------------------
; wm_chrome_relit_x - re-apply a held chrome box's down state (SPEC.md 13.8.1)
; in:  DI = the window wm_draw_title just drew, BP = the frontmost window,
;      and [ui_armlit] is already known non-zero (the thunk tested it)
; out: nothing; all registers preserved
;
; [ui_armlit] is the authority and the glass has to agree with it, which is
; what lets ui_lit_set be a plain XOR of the difference. Two answers, and the
; second is the one that is easy to leave out:
;
;   - the armed window was drawn WITH its boxes (frontmost, not fullscreen):
;     put the inversion back, because the fill above has just erased it.
;   - it was drawn WITHOUT them: its boxes went with its pinstripes, so
;     nothing is lit any more and saying otherwise leaves the release XOR-ing
;     a square onto a title bar that has no box in it.
;
; A package worker calling OSAPI_WM_FRONT is the reachable way to lose the
; front position mid-gesture - the user cannot click anything, the button
; being down - which is exactly the kind of rare that is never debugged.
; -----------------------------------------------------------------------------
wm_chrome_relit_x:
    cmp di, [ui_armw]
    jne .out                    ; a different window: this draw says nothing
    test word [di+W_FLAGS], WF_FULL
    jnz .clear                  ; no title bar at all (SPEC.md 11.2)
    cmp di, bp
    jne .clear                  ; not frontmost: no boxes were drawn
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, di
    mov ah, [ui_armr]
    call wm_chrome_rect_x
    call KERNEL_SEG:cw_gfx_xor_fill ; the caller holds the gfx lock: every
    pop si                          ; wm_draw_title caller does
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret
.clear:
    mov byte [ui_armlit], 0
    ret

; -----------------------------------------------------------------------------
; The far entries (SPEC.md 2.6): kernel.asm's resident thunks far-call these,
; and these near-call the bodies above. gbz_gfx_blit1's shape exactly.
;
; THE LAYER IS NOT OPTIONAL AND LEAVING IT OUT DOES NOT FAIL TO ASSEMBLE. A
; cold body keeps an ordinary near `ret`, so a resident thunk far-calling one
; DIRECTLY pops the offset and leaves the segment on the stack - the `ret`
; returns to a computed address in whatever follows cold_end, which here was
; the FAT window. os88ovlchk.py does not see it: it refuses NEAR calls that
; cross a boundary, and this is a far call to a near-ret body, which is the
; same mistake from the other side. What it looks like from outside is a
; machine whose desktop still moves its mouse (mou_isr is an ISR) and whose
; UI task never comes back - so the press arms, the flag it set reads
; perfectly over the debugger, and nothing is ever drawn or released.
; -----------------------------------------------------------------------------
%ifdef BANDCOMP
%endif
wmz_wm_chrome_rect:     call wm_chrome_rect_x
                    retf
wmz_wm_chrome_relit:    call wm_chrome_relit_x
                    retf

section .text
%endif                          ; KERN_BIG

; -----------------------------------------------------------------------------
; wm_title_set - change a window's caption and redraw ONLY the caption
; in:  BX = window ptr, AX = the new W_TITLE offset, or 0 to leave W_TITLE
;      alone (the bytes it already points at changed in place); caller HOLDS
;      the gfx lock
; out: nothing (all registers preserved)
;
; The answer to "the title bar was drawn before the window knew what it wanted
; to be called". A caption changes on an EVENT - a folder was entered, a file
; was opened - and never on a paint, so the fix is not a hook that asks every
; window for its name on every frame: it is a call the owner makes at the
; moment it decides, and which draws that one strip and nothing else. Before
; this, the file manager's only way to correct its caption was a flag that
; escalated the next repaint to the whole FRAME, so stepping into a folder
; redrew the window's contents, its listing and everything overlapping it.
;
; The rect is x .. x+w-1, y .. y+TITLE_H-1, which is the strip wm_draw_title
; owns, and nothing outside it is touched: no W_PAINT, no cascade. The region
; arithmetic is wm_covered's (SPEC.md 11.3) seeded on that strip instead of
; the frame, so an occluding window's pixels stay its own.
;
; The granularity rule decides which of three ways it goes. The caption is a
; white gap fill with glyphs on top; a fill clips per pixel and a glyph per
; whole 8x8 cell, so a clip edge across the strip would erase the text and
; not put it back - blank, not stale. So the cheap path is taken only when
; the WHOLE strip is one fragment (nothing over it). Wholly covered draws
; nothing at all. Anything in between - or a region that overflowed - falls
; back to wm_paint_dmg over the strip, which is still 17 rows rather than a
; screen.
; -----------------------------------------------------------------------------
wm_title_set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    or ax, ax
    jz .keep
    mov [bx+W_TITLE], ax
.keep:
    mov di, bx                  ; DI = the window for the duration; BX stays
                                ; the window until wm_clip_occl has had it
    test word [di+W_FLAGS], 2
    jz .out                     ; hidden: no pixels on screen to correct
    test word [di+W_FLAGS], WF_FULL
    jnz .out                    ; fullscreen has no title bar (SPEC.md 11.2)

    mov word [wm_clip_n], 0
    mov ax, [di+W_X]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, [di+W_Y]
    mov si, dx
    add si, TITLE_H-1
    mov [wm_clip_tab+WCR_X1], ax
    mov [wm_clip_tab+WCR_Y1], dx
    mov [wm_clip_tab+WCR_X2], cx
    mov [wm_clip_tab+WCR_Y2], si
    mov word [wm_clip_n], 1
    call wm_clip_occl           ; subtract every visible window above us
    jc .dmg                     ; overflowed: nothing about the list is valid
    cmp word [wm_clip_n], 0
    je .clear                   ; wholly covered: not one pixel of it is ours

    mov ax, [di+W_X]            ; is the whole strip one fragment? wm_clip_test
    mov cx, ax                  ; reads an empty list as "disarmed, draw
    add cx, [di+W_W]            ; freely", which is why the covered case had
    dec cx                      ; to be answered above it and not here
    mov bx, [di+W_Y]
    mov dx, bx
    add dx, TITLE_H-1
    call wm_clip_test
    jc .dmg
    call wm_clip_clear          ; nothing is over it: the clip has no work to
    call wm_top                 ; do, and wm_draw_title stays inside the strip
    mov bp, bx                  ; BP = the frontmost visible window (or 0):
    mov bx, di                  ; the pinstripes and the two boxes are its
    call wm_draw_title          ; alone (SPEC.md 11.90)
    jmp short .out
.clear:
    call wm_clip_clear
    jmp short .out
.dmg:
    call wm_clip_clear
    mov ax, [di+W_X]
    mov bx, [di+W_Y]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, bx
    add dx, TITLE_H-1
    call wm_paint_dmg
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_draw_win - draw one window's frame and content (module internal)
; in:  BX = window ptr (visible), BP = frontmost visible window ptr (or 0)
; out: nothing (all registers preserved; caller holds the gfx lock)
;
; Exact frame per SPEC.md 11: 1px black outline; drop shadow offset (+1,+1)
; along the right and bottom edges; title bar rows W_Y+1..W_Y+TITLE_H-2
; white; frontmost window gets 6 black pinstripes inset 3px, an 11x11
; close box at W_X+8, rows W_Y+4..W_Y+14, and an 11x11 minimize box
; mirrored at W_X+W_W-19 with a collapse bar across row W_Y+9 - stripes
; broken by white 2px around each box and 6px around the centered black
; title; black separator at row W_Y+TITLE_H-1; content white-filled, then
; W_PAINT is called with SI=window - bracketed by task_cycles/inst_charge so
; the paint is billed to the window's instance, not to the painting task
; (SPEC.md 8.1/28).
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; wm_saveu - API 0x0340: BX = window, AL = 0 clear / non-0 set
; The window's promise that its content does not change while it is not
; drawing (SPEC.md 11.96.1). Preserves the flags for wm_snap's reason: an
; entry proc calls this after wm_create and the carry riding in is its own
; return value.
;
; **AL BIT 1 IS THE SECOND HALF OF THE PROMISE** (SPEC.md 11.96.17): "and
; every pixel of my content is colour 0 or colour 15", which is what lets the
; cache hold ONE plane instead of four. On the same call and not a slot of its
; own, because the claim means nothing without the save-under it exists to
; make affordable - and RE-STATED every call, so AL = 1 after AL = 3 withdraws
; it exactly as AL = 0 withdraws them both. Bit 0 is still the whole of
; "clear or set", so every historical caller passing 1 is untouched.
;
; IT DOES NOT DROP THE CACHE ON A DEPTH CHANGE, and must not learn to: a
; buffer taken one plane deep means something else the moment the claim goes,
; and 11.96.3 already settled where that is caught - the depth travels in the
; claim's HEADER with the rect it describes, so wm_su_ck invalidates by
; disagreeing. Binding on whoever builds the cache; there is nothing to
; invalidate yet.
; -----------------------------------------------------------------------------
wm_saveu:
    pushf
    push ax                     ; **BX CAME FROM A PACKAGE** (SPEC.md
    mov ax, bx                  ; 11.96.11.4) and both writes below go THROUGH
    sub ax, wm_wins             ; it - so it is a window record or we do
    cmp ax, MAX_WIN * WIN_SIZE  ; nothing. BELOW the table wraps high, so one
    pop ax                      ; unsigned compare catches both ends, and `pop`
    jae .done                   ; leaves the flags alone. Paint once passed its
                                ; screen's HEIGHT here and the kernel set a bit
                                ; at KERNEL_SEG:480, inside the API jump table
    or al, al
    jz .clear
    and word [bx+W_FLAGS], ~WF_1BPP & 0xFFFF    ; the depth claim is RE-STATED
    test al, 2                                   ; on every call, so a caller
    jz .set                                      ; that stops passing bit 1 has
    or word [bx+W_FLAGS], WF_1BPP                ; withdrawn it (SPEC.md
.set:                                            ; 11.96.17)
    or word [bx+W_FLAGS], WF_SAVEU
    popf
    ret
.clear:
    and word [bx+W_FLAGS], ~(WF_SAVEU | WF_1BPP) & 0xFFFF
    call wm_su_drop             ; ...and a promise withdrawn takes any cache
.done:                          ; made under it with it
    popf
    ret

; -----------------------------------------------------------------------------
; wm_band - API 0x03A8: "cache THIS BAND of my content and hand me the rest"
; in:  BX = window ptr, AL = edge (0 left, 1 right, 2 top, 3 bottom),
;      CX = the band's extent in pixels, measured in from that edge.
;      CX = 0 retires the band, and the cache goes back to the whole content.
; out: CF = 0 taken, CF = 1 REFUSED - and a refusal is not an error, it is the
;      answer a kernel that does not do this gives (REDRAWFULL, and any future
;      build that drops the feature). An app must READ it: the whole reason to
;      name a band is that a cache over the whole content is unaffordable, so
;      a caller that promises WF_SAVEU without checking has promised the thing
;      it was trying to avoid. Paint's content is 9KB banked on 1bpp and 150KB
;      grown on VGA; its palette column is 1KB and 13KB.
; clobbers: nothing but CF
;
; SPEC.md 11.96.11. The band is where a window keeps the furniture it redraws
; by DRAWING it - Paint's tool palette is 451 of the 604 primitive calls its
; repaint issues, 341 ms of 422 on a 4.77MHz 8088 - and the rest of the content
; is whatever it can put back cheaply itself. So the kernel banks the band and
; the app is told, through 11.90.2's damage rect, that it owes everything but.
;
; A BAND AND NOT A RECT, and that is what makes it small: on one edge, the part
; the app keeps is a RECTANGLE for any damage rect, so wm_damage subtracts it
; with four compares and no region arithmetic anywhere. A general kept rect
; leaves an L or a ring and needs the cache to become a fragment list; the band
; is the case every consumer here actually has, and 11.96.11.1 records what the
; general one would cost.
;
; ONE WORD PER WINDOW, edge in the top two bits, and 0 means no band - which is
; what every existing window has by construction rather than by initialisation.
; -----------------------------------------------------------------------------
wm_band:
%ifdef REDRAWFULL
    stc                         ; the reference build does not do this, and
    ret                         ; says so rather than half-doing it
%else
    push ax
    push bx
    push cx
    push si
    cmp al, 3
    ja .no
    cmp cx, 255
    ja .no                      ; A BYTE PER EDGE (SPEC.md 11.96.11.1): four of
                                ; them per window is 48 bytes of .text, and a
                                ; cached band wider than 255 pixels is not a
                                ; band. CX = 0 retires this edge
    or cx, cx
    jz .idx                     ; ...and a retirement needs no promise
    test word [bx+W_FLAGS], WF_OWNBG
    jz .no                      ; THE INTERLOCK, and it is 11.90.2's own: with
                                ; the flag clear the kernel white-fills the
                                ; WHOLE content before W_PAINT, so a band
                                ; restored in front of one is wiped by it.
                                ; Refusing here makes "a band implies the app
                                ; owns its background" a property of the build
                                ; rather than a rule for a reader to keep
.idx:
    push di                     ; wm_su_eptr answers about DI, and every other
    mov di, bx                  ; caller here already has the window there
    call wm_su_eptr             ; SI = &wm_su_ext[slot]
    pop di
    xor ah, ah                  ; AX = the edge, so SI names its byte
    add si, ax
    cmp cl, [si]
    je .ok                      ; unchanged: do NOT drop a live cache for it
    mov [si], cl
    call wm_su_drop             ; ...and changed, whatever is banked is laid
.ok:                            ; out for a different set of fragments
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; wm_ownbg - "I paint every pixel of my content myself" (SPEC.md 11.90.1)
; in:  BX = window ptr, AL = 0 clear / non-0 set
; out: nothing (all registers and the flags preserved)
;
; Withdrawing it needs no cleanup, unlike wm_saveu's: the flag changes what the
; NEXT paint does and leaves nothing behind. The promise is the whole contract
; and it is the app's to keep - a window that sets this and then leaves a pixel
; of its content unwritten shows whatever was there before, which after a move
; is another window's.
; -----------------------------------------------------------------------------
wm_ownbg:
    pushf
    or al, al
    jz .clear
    or word [bx+W_FLAGS], WF_OWNBG
    popf
    ret
.clear:
    and word [bx+W_FLAGS], ~WF_OWNBG & 0xFFFF
    popf
    ret

; -----------------------------------------------------------------------------
; wm_keeph - "my layout is fixed: hang me over the dock rather than shorten me"
;            (public; API 0x03E0, SPEC.md 11.93)
; in:  BX = window ptr, AL = 0 clear / non-0 set
; out: nothing - every register preserved, FLAGS INCLUDED
;
; wm_fit clamps a frame that will not fit the desktop band down to it, and for
; anything that reflows - a Disk window, Note Pad, the two that re-derive
; through 11.98 - that is exactly right. For a FIXED layout it is not a clamp
; but a loss: the app goes on drawing all of its content, so the rows below
; W_Y + W_H are still on the glass and simply belong to no window. wm_hit
; tests the record, so a click there falls through to the dock, and the effect
; is a board whose bottom rows can be seen and not played.
;
; So the height stops being the kernel's to reduce, and the floor becomes the
; DISPLAY's own bottom instead of the dock's top row. It is opt-in and must
; be: the kernel cannot tell a layout that can give a row up from one that
; cannot, and guessing it from "has no negotiator and no 11.98 handler" would
; make ADDING one silently re-truncate the window.
;
; IT RE-FITS FROM THE NATURAL BANK, because by the time an entry proc can call
; this wm_create has already clamped the window and the asked-for height only
; survives in the bank (39.11.2.1). That is also why this is not simply a
; template word: the copy in wm_create is EIGHT words and growing it would
; read one word past every existing package's template.
;
; The intended caller is an entry proc or a KD_INIT, before the window is
; shown, where moving it costs nothing and no repaint is owed. Calling it on a
; visible window is legal and takes effect at the next paint the caller
; performs - wm_snap's terms, and its FLAGS-preserving contract too, for
; wm_snap's reason: an entry proc's CF is the loader's return value.
;
; NO 11.98 NOTIFICATION, deliberately: that one reports a box that moved UNDER
; an application, and is called with the gfx lock held, which an entry proc
; does not hold. This is the application asking, and its own first W_PAINT is
; where it reads the answer.
; -----------------------------------------------------------------------------
wm_keeph:
    pushf
    or al, al
    jz .clear
    or word [bx+W_FLAGS], WF_KEEPH
.refit:
    call wm_apply_wh            ; the SIZE the TEMPLATE asked for, this
                                ; adapter's preference over it, and the whole
                                ; re-clamped under the flag as it now is
                                ; (SPEC.md 11.100) - the ONE sequence, so a
                                ; window that both declares a preference and
                                ; asks to keep its height cannot get a
                                ; different answer depending on which of the
                                ; two it called last. NOT wm_apply_size: this
                                ; is reachable from an 11.98 handler inside a
                                ; drag's own notification, and the origin
                                ; there belongs to the hand on the window
                                ; (SPEC.md 11.100.9)
    popf
    ret
.clear:
    and word [bx+W_FLAGS], ~WF_KEEPH & 0xFFFF
    jmp short .refit            ; both directions re-fit: withdrawing it has
                                ; to be able to put a shortened window back

; -----------------------------------------------------------------------------
; wm_damage - which rect does this paint owe? (SPEC.md 11.90.2)
; in:  BX = window ptr - your own, called from inside its W_PAINT
; out: CF = 1 = draw the WHOLE content, and AX/BX/CX/DX are that content rect;
;      CF = 0 = draw AX/BX/CX/DX and nothing else, absolute screen coordinates,
;      inclusive - and possibly EMPTY (x1 > x2), which means draw nothing at all.
;      Either answer leaves a rect in the four registers that is safe to draw:
;      vga_rect_setup reads an empty one as "nothing to do", so a caller that
;      never tests for it still cannot paint anything wrong.
;      Clobbers the four outputs only.
;
; §11.96.6's accumulated damage, handed to the application instead of being kept
; for the raise cache. There is no new bookkeeping behind it: wm_dmg_wins already
; arms the per-window rect before every wm_draw_win, and on a window with no
; cache wm_su_srect never spends it, so it is still there when W_PAINT runs.
;
; IT ANSWERS "WHOLE" WITHOUT WF_OWNBG, and that is the interlock rather than a
; documented rule: with the flag clear the kernel has ALREADY filled the content
; white, so a partial answer would leave the rest of the window blank. The two
; features are only correct together and this is where that is made true.
;
; Every other path answers "whole" by having armed nothing - a raise, a
; wm_paint_all, a fullscreen surface - and the one path that does arm cannot
; under-report, because a window that MOVED has its whole content inside the
; damage by construction (the rect is the union of where it was and where it is).
; -----------------------------------------------------------------------------
wm_damage:
    push si
    push di
    mov di, bx                  ; the window: wm_su_rect writes all four
    call wm_su_rect             ; AX/BX/CX/DX = its content rect
    xor si, si                  ; ...and SI says whether that rect has been
                                ; NARROWED by anything below, which is what
                                ; lets "no sub-rect named" still be an answer
    test word [di+W_FLAGS], WF_FULL
    jnz .whole                  ; ...AND WHOLE ON A FULLSCREEN WINDOW, whatever
                                ; WF_OWNBG says: wm_draw_win's 11.2 branch fills
                                ; the WHOLE FRAME white and has no opt-out, so a
                                ; partial answer there leaves the rest blank -
                                ; the interlock below, at the one geometry that
                                ; escapes it. Latent until 11.96.10, because
                                ; wm_dmg_wins was the only caller that armed a
                                ; rect and a fullscreen window is rarely marked;
                                ; wm_front can raise one
    test word [di+W_FLAGS], WF_OWNBG
    jz .whole
    cmp di, [wm_dmg_rzwin]
    jne .notrz                  ; SPEC.md 11.90.3: this window was SHRUNK with
    mov ax, 1                   ; its origin unmoved, so nothing painted over
    xor bx, bx                  ; what survived - and an EMPTY rect (x1 > x2)
    xor cx, cx                  ; is what 11.90.2 already documents as "draw
    xor dx, dx                  ; NOTHING". What MOVED is still the app's:
    clc                         ; only it knows its strip is anchored
    pop di
    pop si
    ret
.notrz:
%ifndef REDRAWFULL
    cmp byte [wm_su_bok], 0     ; THE BANDS CAME BACK OFF THE CACHE (SPEC.md
    je .noband                  ; 11.96.11), so the app owes the content INSET
    call wm_su_bget             ; by them - which is the whole point of naming
    jc .noband                  ; any. It is [wm_su_bok] and not the extents,
    call wm_su_orect            ; because on a cache MISS the app owes the
    inc si                      ; bands too and nothing else tells those apart
.noband:
%endif
    cmp word [wm_su_son], 0
    je .nosub                   ; nobody named one
    cmp ax, [wm_su_sx1]
    jge .x1
    mov ax, [wm_su_sx1]
.x1:
    cmp bx, [wm_su_sy1]
    jge .y1
    mov bx, [wm_su_sy1]
.y1:
    cmp cx, [wm_su_sx2]
    jle .x2
    mov cx, [wm_su_sx2]
.x2:
    cmp dx, [wm_su_sy2]
    jle .y2
    mov dx, [wm_su_sy2]
.y2:
    clc
    pop di
    pop si
    ret
.nosub:
    or si, si                   ; the band narrowed it, so this is a rect and
    jz .whole                   ; not "the whole content"
    clc
    pop di
    pop si
    ret
.whole:
    stc
    pop di
    pop si
    ret

; =============================================================================
; THE RAISE CACHE (SPEC.md 11.96) - a window raised without repainting it
;
; Raising an obscured window was measured at 1,026 ms on a 4.77MHz 8088, and
; 578 ms of that is W_PAINT lettering cells that were genuinely overwritten
; while the window was covered (docs/NOTEPAD-NOTES.md 5.3). There is no faster
; way to draw them; the only way under one typematic repeat is not to draw
; them at all. So the content is BANKED when the window is covered and put
; back when it is raised: ~10 ms of blit each way against 578 ms of glyphs.
;
; IT IS A PURGEABLE CLAIM (SPEC.md 50.6), which is what makes it affordable to
; take speculatively: MEM_P_WSAVE, given back the instant anything else needs
; the room, and mem_shed_one zeroes [wm_su_seg] on its way out so the test
; below is the notice. A machine with no room for it behaves exactly as it did
; before this existed.
;
; IT IS OPT-IN, THROUGH WF_SAVEU, and this comment used to say the opposite -
; "there is no opt-in and that is deliberate... so every window gets it". That
; was written before the flag existed and left behind when it arrived, which
; cost a reader the belief that no window opts in at all.
;
; The argument it made was that the kernel knows everything the decision needs
; and the app knows nothing extra. The second half is false, and the paragraph
; below is why: what makes the cache safe is a covered window not changing its
; pixels without the kernel finding out - and that only holds for a window
; that DRAWS. A window whose state advances while it is covered and which
; skips drawing BECAUSE it is covered never reaches wm_clip_set, so nothing
; drops the cache and the raise puts back pixels that stopped being true. Only
; the application knows which of those it is (SPEC.md 11.96.1).
;
; ONE CACHE PER WINDOW (SPEC.md 11.96.3), and it was one for the machine. The
; bound was worth having while every cache cost the same as every other; the
; PRIORITY LADDER (SPEC.md 50.6.4) retired that, because a cache at the
; trivial rank is given up by anything that wants the room - so bounding it
; at one block bought nothing and cost last-covered-wins, where covering a
; cheap window took the cache from an expensive one.
;
; The slot IS the window, so wm_su_win is gone: wm_su_segs is a word per
; window record and the owner tag carries the slot in its low byte, which is
; the only part of the memory manager that had to learn anything (mem_pg_own
; rows are ranges now). AND THE RECT LIVES IN THE CLAIM, in a four-word
; header the image starts after. That is not only cheaper than four more
; arrays - it is the better shape, because the rect travels with the pixels
; it describes and cannot get out of step with them.
;
; WHAT MAKES IT SAFE is that a covered window cannot change its pixels without
; the kernel finding out. SPEC.md 11.3: a background painter arms a clip
; region before it draws, because a covered window that draws without one
; paints over the window on top of it - so it is already broken if it does
; not. wm_clip_set therefore drops the cache, which covers every app at once
; with no rule for an app to remember. The other two are free: wm_su_try
; compares the banked RECT with the window's rect now, so a move, a resize, a
; refit and an adapter change all invalidate by disagreeing, and wm_destroy
; drops it because a reused slot would otherwise alias the pointer.
; =============================================================================
; --- the claim's own header, ahead of the image gfx_save writes -------------
WSU_X1  equ 0                   ; word: the CONTENT rect the pixels behind it
WSU_Y1  equ 2                   ; were taken over, COMPARED against the
WSU_X2  equ 4                   ; window's rect now rather than trusted (see
WSU_Y2  equ 6                   ; above)
WSU_EXT equ 8                   ; ...and the four clamped band extents the
                                ; FRAGMENT LAYOUT was built from (SPEC.md
                                ; 11.96.11.1): left, right, top, bottom, one
                                ; byte each. With the rect above they are the
                                ; whole of what that layout is a function of
%ifdef KERN_BIG
  %macro SU1ARM 0
    call wm_su_arm
  %endmacro
%else
  %macro SU1ARM 0               ; kern_small has no WF_1BPP path (SPEC.md
  %endmacro                     ; 11.96.17), so it pays nothing at all
%endif

WSU_PW  equ 12                  ; ...and HOW DEEP the buffer behind it is, in
                                ; planes (SPEC.md 11.96.17). A cache taken
                                ; under a two-colour claim is ONE plane, and
                                ; the same bytes mean something else the
                                ; moment the claim goes - so the depth travels
                                ; with the pixels it describes, exactly as
                                ; 11.96.3 has the rect do, and wm_su_ck
                                ; invalidates by disagreeing rather than
                                ; wm_saveu remembering to drop. It subsumes
                                ; the dual-display hazard too: a window banked
                                ; on a Hercules and checked on a VGA
                                ; disagrees here
WSU_IMG equ 14                  ; ...and the image starts here

wm_su_segs: times MAX_WIN dw 0  ; the banked content, one word per window
                                ; record, 0 = none. In .text with real
                                ; initialisers: -f bin zeroes nothing and
                                ; wm_su_try reads one before anything sets it
wm_su_ext: times MAX_WIN*4 db 0 ; ...and the four BAND extents each window
                                ; asked to have cached (SPEC.md 11.96.11):
                                ; left, right, top, bottom, in pixels, per
                                ; window slot. All four zero is "no bands",
                                ; which every window has by construction
                                ; rather than by initialisation. Same reason
                                ; for being in .text as the row above
wm_su_kex: times 4 db 0         ; ...those four CLAMPED to the content rect of
                                ; the operation in hand (wm_su_flay), which is
                                ; what everything downstream reads
wm_su_kany: db 0                ; ...and whether any of them is non-zero
wm_su_ftab: times 16 dw 0       ; the four fragment rects, x1/y1/x2/y2 each;
                                ; an empty one has x1 > x2 (SPEC.md 11.96.11.1)
wm_su_fn: dw 0                  ; ...which one the loop in hand is on
wm_su_fsz: dw 0                 ; ...and how many buffer bytes it occupies
wm_su_scrb: dw 0                ; where the fragments end and 11.96.2's edge
wm_su_scrsz: dw 0               ; scratch begins, and how big that is
%ifdef KERN_BIG
wm_su_pw: dw 4                  ; HOW DEEP THE DISPLAY THIS CACHE IS ON IS, in
                                ; planes, which is not [vid_planes_w] the
                                ; moment two displays can differ
                                ; (docs/DUAL-DISPLAY-VGA.md 4.3). Set by
                                ; wm_su_flay, which is the one place both the
                                ; take and the check establish the content
                                ; rect, and read by wm_su_bytes, wm_su_scrset,
                                ; wm_su_edge and wm_su_merge - the sizing, the
                                ; scratch, the restore stride and the merge.
                                ;
                                ; THE BUG IT CLOSES IS A HEAP OVERRUN, not a
                                ; wrong picture. gfx_save takes GFXDENTER and
                                ; so writes with the planes of the display the
                                ; rect is ON, while the claim was sized from
                                ; whichever display happened to be current -
                                ; the primary, since every hook restores it.
                                ; Primary Hercules with a window on the VGA is
                                ; a buffer sized x1 and a save that writes x4.
                                ; The other way round merely wastes.
                                ;
                                ; .text with a real initialiser for this file's
                                ; own reason: -f bin zeroes nothing.
%endif

; ...AND kern_small READS [vid_planes_w] DIRECTLY, because that build has ONE
; display by construction and the two words can never differ in it. Selecting
; the NAME here rather than keeping the word and feeding it is what makes the
; small build measure +0: the word, the store and the four instructions that
; bank CX/DX around it are not assembled at all. Written down because the
; plan (docs/DUAL-DISPLAY-VGA.md 6) calls that +0 non-negotiable and it is
; the cheapest regression test the whole feature has - and because the first
; version of this DID cost it 17 bytes, by writing [vid_planes_w] into a word
; nothing else could ever have put anything different in.
%ifdef KERN_BIG
%define WM_SU_PW wm_su_pw
%else
%define WM_SU_PW vid_planes_w
%endif
wm_su_fbase: dw 0               ; ...and where the fragment being restored
                                ; STARTS, which is what wm_su_edge patches
                                ; relative to. It was WSU_IMG, which is the
                                ; first fragment's base and nobody else's
wm_su_ex1: dw 0                 ; wm_su_edge's OWN four (SPEC.md 11.96.11.1):
wm_su_ey1: dw 0                 ; it used to borrow wm_su_sx1..sy2, and a cache
wm_su_ex2: dw 0                 ; of FRAGMENTS calls it once per fragment while
wm_su_ey2: dw 0                 ; the sub-rect is still owed to every one
wm_su_bok: db 0                 ; the band came back off the cache in the draw
                                ; in hand, so wm_damage owes the app the rest
                                ; and not the lot. Cleared at the top of every
                                ; wm_draw_win; in .text for its neighbours'
                                ; reason
wm_su_x1:  dw 0                 ; the rect the operation in hand is working
wm_su_y1:  dw 0                 ; on - written by wm_su_take before the claim
wm_su_x2:  dw 0                 ; that will hold it exists, and read back out
wm_su_y2:  dw 0                 ; of that claim's header by wm_su_ck

; --- and the rect the NEXT window drawn owes (SPEC.md 11.96.6) --------------
; A one-shot, wm_dmg_dk's rule: 0 means "the whole window", which is what a
; caller that never heard of this inherits.
%ifdef KERN_BIG
; --- WHICH DISPLAY A LANDING DECIDED ON (SPEC.md 39.16.4.2) -------------------
; wm_land_fit asks wm_disp_now ONCE and every step after it is answered from
; here, because the RESIZE it applies can stop the straddle being true: a
; 250x303 Arkanoid dropped over the seam takes the CGA's 208x156 and no longer
; reaches the CGA at all, so wm_kind_bank recorded the HERCULES against a CGA
; frame and the app's own OSAPI_WM_DISPLAY read the Hercules' 720x348 to lay
; out a 208-wide window. Two answers to one question, exactly as SPEC.md
; 39.16.4.1 already had to settle once - and this is the same defect one step
; further in, where the geometry moves between the two asks rather than the
; routine.
;
; The WINDOW is latched beside the answer so a handler that asks about some
; OTHER window during the notification gets the truth. .text and not .bss:
; wm_disp_now is asked from the first mouse press of the session, long before
; any landing, and 0 is "nothing armed" only because wm_wins is well above it.
wm_lf_win:  dw 0
wm_lf_disp: db 0
%endif

wm_dw_part: db 0                ; the draw in hand is PARTIAL (SPEC.md 11.96.9).
                                ; In .text with an initialiser: -f bin zeroes
                                ; nothing and wm_su_bank reads it on the first
                                ; window the machine ever draws
wm_su_pn:  dw 0                 ; SPEC.md 11.96.15: pieces of the sub-rect the
wm_su_pi:  dw 0                 ; restore is walking, and which one it is on -
wm_su_bx1: dw 0                 ; 0 pieces = the single rect below, unreduced.
wm_su_by1: dw 0                 ; The caller's rect is banked across the walk
wm_su_bx2: dw 0                 ; because wm_damage still owes it to the app on
wm_su_by2: dw 0                 ; a band restore (11.96.11)
wm_su_son: dw 0                 ; is a sub-rect named? (in .text: -f bin zeroes
wm_su_sx1: dw 0                 ; nothing, and wm_su_try reads this before
wm_su_sy1: dw 0                 ; anything in the machine has written it)
wm_su_sx2: dw 0
wm_su_sy2: dw 0

; ...and the DISPLAY CLIP, which is a SECOND named rect rather than an
; intersection into the one above (SPEC.md 11.96.14). The two say different
; things and have different owners: wm_su_s* is what a PASS PAINTED, named by
; wm_damage, which owes the app that same rect afterwards - narrowing it here
; would quietly change what the app is told to repaint. wm_su_v* is what is on
; the GLASS, which is nobody's argument but this one's. Same one-shot rule,
; cleared by whoever armed it.
wm_su_von: dw 0
wm_su_vx1: dw 0
wm_su_vy1: dw 0
wm_su_vx2: dw 0
wm_su_vy2: dw 0

; --- the DRAG cache (SPEC.md 11.96.12) --------------------------------------
; The window a drag has banked, and the ONLY window allowed to use the machine
; above without having promised WF_SAVEU. In .text with an initialiser for
; wm_su_son's reason: wm_su_ck reads it before anything has written it.
wm_dc_win: dw 0

; --- ...and what a RAISE owes, which is what was COVERED (SPEC.md 11.96.10) --
; Written by wm_cov_rect before wm_lift and spent by wm_raise into wm_su_sub
; above, because everything between the two - the bar, the dock, the outgoing
; window's chrome - may itself call wm_draw_win and would consume a one-shot
; armed any earlier.
wm_su_pcrp: dw 0                ; SPEC.md 11.96.16.2: a rect laid out like a
                                ; record's X/Y/W/H for wm_su_precover's
                                ; overlap test to use INSTEAD of DI's own,
                                ; because a resize has already written the new
                                ; one where wm_obscured needs the old. 0 = ask
                                ; about DI, which is every other caller
wm_cov_x1: dw 0                 ; the covered box, accumulated
wm_cov_y1: dw 0
wm_cov_x2: dw 0
wm_cov_y2: dw 0
wm_cov_c1: dw 0                 ; ...and our content rect, held across the walk
wm_cov_c2: dw 0                 ; so the fold can do one axis at a time
wm_cov_c3: dw 0
wm_cov_c4: dw 0

; -----------------------------------------------------------------------------
; wm_su_owed - the rect window SI owes in this pass (SPEC.md 11.96.6)
; in:  SI = window ptr; wm_dmg_x1..y2 = everything the pass has painted
; out: AX = x1, BX = y1, CX = x2, DX = y2; SI preserved
;
; The painted box, plus the dock strip for a window that reaches it. THE STRIP
; IS A PER-WINDOW QUESTION and not a term in the box, which is the same
; distinction the marking pass makes one screen up (`.mnodock`): the strip is
; full width, so folding it into the shared rect makes EVERY window's owed rect
; full width - measured, that turned a 27-byte-per-row band into a 41-byte one
; for a window nowhere near the dock. Same test as the marking's, so the two
; cannot come to disagree about which windows the strip damages.
; -----------------------------------------------------------------------------
wm_su_owed:
    mov ax, [wm_dmg_x1]
    mov bx, [wm_dmg_y1]
    mov cx, [wm_dmg_x2]
    mov dx, [wm_dmg_y2]
    cmp byte [wm_dmg_dk], 0
    je .out                     ; dock_paint put nothing on the strip
    push di
    mov di, [si+W_Y]
    add di, [si+W_H]
    cmp di, [vid_dock_y0]
    pop di
    jl .out                     ; ...or this window does not reach it
    xor ax, ax                  ; it does, and the strip is drawn UNDER windows,
    cmp cx, [vid_pwm1]          ; so this window owes the whole width of it -
    jge .noc                    ; as a WIDENING of the painted box, never a
    mov cx, [vid_pwm1]          ; replacement. The strip is the PRIMARY's
.noc:                           ; (39.14), so replacing would hand a window on
    cmp dx, [vid_phm1]          ; display 1 a rect disjoint from itself, and
    jge .nod                    ; wm_su_srect reads that as "nothing this pass
    mov dx, [vid_phm1]          ; painted reached the content" - which is
.nod:                           ; permanent, nothing repaints it after
    cmp bx, [vid_dock_y0]
    jle .out
    mov bx, [vid_dock_y0]
.out:
    ret

; -----------------------------------------------------------------------------
; wm_chrome_clip - the region a window's CHROME may draw in (SPEC.md 11.97)
; in:  DI = the window wm_draw_win is about to draw; gfx lock held
; out: nothing; every register and the flags preserved. The clip list is armed,
;      or left DISARMED - which means "draw the lot" and is what every caller
;      did before this existed.
;
; wm_draw_win writes the outline, the drop shadow and the title bar WHOLE,
; whether the raise cache hit or not (11.96.6), and a damage pass draws back to
; front. So a window below the mover puts its frame down across ground the mover
; is about to cover, and the mover - being frontmost - is drawn last: the window
; underneath shows its frame and drop shadow for a frame or two and then loses
; them again. Reported from the field, priced by PERFORMANCE.md Part 3.1's
; instrument, and it is 1px black lines on grey, which is the most visible thing
; a redraw can flash.
;
; It is wm_covered's arithmetic (11.3) seeded with the FRAME rect where
; wm_clip_set seeds with the CONTENT rect, and NOT wm_clip_set itself: that one
; drops the raise cache, which is exactly what this window is about to restore
; from.
;
; WHAT MAKES IT SOUND is 11.91's marking: a window that overlaps a marked window
; is marked too, transitively and upwards, so every visible window above this one
; that overlaps it is drawn LATER IN THIS SAME PASS. A pixel the region drops is
; therefore a pixel somebody above is about to write anyway - which is the
; identical argument that made drawing over it safe, read the other way round.
;
; wm_obscured first, because on the frontmost window - every wm_raise, every
; wm_show - the answer is "all of it", and an armed region costs gfx_clip_run a
; pass per primitive to arrive at the rect it was handed.
; -----------------------------------------------------------------------------
wm_chrome_clip:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [wm_clip_n], 0
    mov bx, di
    call wm_obscured
    jnc .out                    ; nothing above us overlaps: draw the lot
    call cur_unlazy              ; SPEND THE DEFERRED HIDE (7.1.4). An armed
                                ; region stops the clipped primitives taking it
                                ; themselves, and the cursor is not confined to
                                ; this window - so without this it is drawn over
                                ; and smeared at the next cur_move
    mov ax, [di+W_X]
    mov dx, [di+W_Y]
    mov cx, ax
    add cx, [di+W_W]            ; x .. x+w and y .. y+h: the frame plus its 1px
    mov si, dx                  ; drop shadow, which wm_draw_win draws
    add si, [di+W_H]
    test word [di+W_FLAGS], WF_FULL
    jz .seed
    dec cx                      ; fullscreen has no shadow (SPEC.md 11.2)
    dec si
.seed:
    call wm_clip_seed
    call wm_clip_occlf          ; BX = us; subtract every visible window above,
                                ; BY THE FRAME each of them REPAINTS and not by
                                ; the box it occupies (SPEC.md 11.97.2). The
                                ; paragraph at wm_clip_occlf says occlusion may
                                ; claim the two unpainted shadow corners because
                                ; "a clipped primitive declines to draw one
                                ; nobody needed" - and that is 11.97's own
                                ; licence, "a pixel the region drops is a pixel
                                ; somebody above is about to write anyway",
                                ; which holds INSIDE the upper window's frame
                                ; and is false at exactly those two corners:
                                ; nothing paints them, ever. So the seed here -
                                ; a LOWER window's chrome, drop shadow included
                                ; - lost its shadow pixel wherever an upper
                                ; window's (x, y+h) or (x+w, y) landed on it,
                                ; and nothing put it back
    jc .none                    ; overflowed: nothing about the list is valid
    cmp word [wm_clip_n], 0
    jne .out
.none:
    mov word [wm_clip_n], 0     ; OVERFLOW DEGRADES TO "DRAW IT", wm_covered's
                                ; way and not wm_clip_set's: a dropped pixel
                                ; here is one a background painter would have
                                ; skipped, and here it is one this pass owes
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_cov_rect - the box of this window's CONTENT that something is sitting on
; in:  BX = window ptr (visible), gfx lock held, asked BEFORE wm_lift while the
;      z-order still says what is on top
; out: wm_cov_x1..y2 = the box, inclusive - and EMPTY (x1 > x2) when nothing
;      above reaches the content, which is a real answer and not a failure.
;      All registers preserved.
;
; SPEC.md 11.96.10. A raise repaints what was COVERED, and that is the union of
; the windows above us intersected with our content - a bounding box of it here,
; the same shape wm_dmg_x1..y2 already accumulates.
;
; IT IS wm_obscured'S OWN ASSERTION AT A FINER GRAIN, not a new one. wm_front
; has always drawn only the title bar when nothing was on top, which says in so
; many words that an uncovered window's content on screen is already right; this
; asks the same question per region instead of per window, and the AL = 0 path
; is now the degenerate case of it (an empty box - nothing to put back).
;
; The box is the CONTENT's because that is all either consumer owns: the cache
; holds content and wm_damage answers a content rect, while wm_draw_win draws
; the outline, the shadow and the title bar whole either way.
;
; The union's bounding box is exact - the smallest rect holding every covered
; pixel - so this cannot under-report; the occluder's rect includes its drop
; shadow, for wm_obscured's reason.
; -----------------------------------------------------------------------------
wm_cov_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx                  ; DI = us, for the walk and for wm_su_rect
    call wm_su_rect             ; AX/BX/CX/DX = our content rect
    test word [di+W_FLAGS], WF_FULL
    jz .rect
    mov ax, [di+W_X]            ; ...except fullscreen, where the FRAME is the
    mov bx, [di+W_Y]            ; content and wm_su_rect insets a title bar that
    mov cx, ax                  ; is not there (SPEC.md 11.2). Not wm_win_rect:
    add cx, [di+W_W]            ; that takes SI, and the walk below is holding it
    dec cx
    mov dx, bx
    add dx, [di+W_H]
    dec dx
.rect:
    mov [wm_cov_c1], ax
    mov [wm_cov_c2], bx
    mov [wm_cov_c3], cx
    mov [wm_cov_c4], dx
    mov word [wm_cov_x1], 0x7FFF; seeded EMPTY, and left that way by a window
    mov word [wm_cov_y1], 0x7FFF; nothing overlaps - which wm_su_srect reads as
    mov word [wm_cov_x2], -1    ; "no pixel needs writing" and wm_damage as
    mov word [wm_cov_y2], -1    ; "draw nothing", both correct
    mov ax, di                  ; AL = our index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; (AH = 0: a window ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    jcxz .out
.find:
    cmp [si], al
    je .found
    inc si
    loop .find
    jmp short .out              ; not in the z-order (cannot happen)
.found:
    inc si                      ; first entry above us
    dec cx
.scan:
    jcxz .out
    dec cx
    mov al, [si]
    inc si
    call wm_idx2ptr             ; BX = the window above (AX and CX preserved)
    test word [bx+W_FLAGS], 2
    jz .scan
    call wm_cov_add
    jmp short .scan
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_cov_add - fold (window BX's frame INTERSECT wm_cov_c1..c4) into the box
; in:  BX = an occluder above us, visible; wm_cov_c1..c4 = our content rect
; out: nothing; AX/CX/SI/DI preserved (the walk above holds all four)
;
; The overlap is settled FIRST, by four compares, and only then is each edge
; clamped and folded - which is what lets the four axes be done one at a time
; with two registers instead of holding a whole intersection rect.
; -----------------------------------------------------------------------------
wm_cov_add:
    push ax
    push dx
    mov ax, [bx+W_X]
    cmp ax, [wm_cov_c3]
    jg .out                     ; entirely to our right
    mov ax, [bx+W_Y]
    cmp ax, [wm_cov_c4]
    jg .out                     ; entirely below us
    call wm_cov_ox2             ; AX = its right edge, drop shadow included
    cmp ax, [wm_cov_c1]
    jl .out                     ; entirely to our left
    mov dx, ax                  ; ...banked: the y test clobbers AX
    call wm_cov_oy2
    cmp ax, [wm_cov_c2]
    jl .out                     ; entirely above us
    cmp ax, [wm_cov_c4]         ; --- y2 = min(its bottom, our bottom)
    jle .y2
    mov ax, [wm_cov_c4]
.y2:
    cmp ax, [wm_cov_y2]
    jle .x2
    mov [wm_cov_y2], ax
.x2:
    cmp dx, [wm_cov_c3]         ; --- x2 = min(its right, our right)
    jle .x2f
    mov dx, [wm_cov_c3]
.x2f:
    cmp dx, [wm_cov_x2]
    jle .y1
    mov [wm_cov_x2], dx
.y1:
    mov ax, [bx+W_Y]            ; --- y1 = max(its top, our top)
    cmp ax, [wm_cov_c2]
    jge .y1f
    mov ax, [wm_cov_c2]
.y1f:
    cmp ax, [wm_cov_y1]
    jge .x1
    mov [wm_cov_y1], ax
.x1:
    mov ax, [bx+W_X]            ; --- x1 = max(its left, our left)
    cmp ax, [wm_cov_c1]
    jge .x1f
    mov ax, [wm_cov_c1]
.x1f:
    cmp ax, [wm_cov_x1]
    jge .out
    mov [wm_cov_x1], ax
.out:
    pop dx
    pop ax
    ret

; wm_cov_ox2 / wm_cov_oy2 - window BX's right and bottom edges in AX, drop
; shadow included: x+w and y+h, one less on a fullscreen window, which has no
; shadow (SPEC.md 11.2). wm_obscured tests the same two edges the same way.
wm_cov_ox2:
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    test word [bx+W_FLAGS], WF_FULL
    jz .out
    dec ax
.out:
    ret
wm_cov_oy2:
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    test word [bx+W_FLAGS], WF_FULL
    jz .out
    dec ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_su_sub - the next window drawn owes only this rect (SPEC.md 11.96.6)
; in:  AX = x1, BX = y1, CX = x2, DX = y2 - everything painted so far by the
;      pass that is about to draw it
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_su_sub:
    mov [wm_su_sx1], ax
    mov [wm_su_sy1], bx
    mov [wm_su_sx2], cx
    mov [wm_su_sy2], dx
    mov word [wm_su_son], 1
    ret

; -----------------------------------------------------------------------------
; wm_su_srect - which rect is wm_su_try about to put back?
; out: CF = 0 and AX/BX/CX/DX = the rect to hand gfx_restore, with gfx_sub_*
;      armed if it is a proper sub-rect of the banked one; CF = 1 = NOTHING
;      needs putting back, because nothing this pass painted reached the
;      content. Clobbers AX, BX, CX, DX.
;
; The named rect is intersected with the banked one rather than trusted: a
; caller names what its pass has PAINTED, which is a fact about the screen and
; says nothing about where this window is.
;
; A refusal from gfx_sub_arm falls back to the whole buffer, which is always
; correct and only dearer - so this cannot go wrong in the direction that loses
; pixels, only in the one that spends time.
; -----------------------------------------------------------------------------
; wm_su_vset - arm the DISPLAY CLIP for a cached rect (SPEC.md 11.96.14)
; in:  AX/BX/CX/DX = the rect the cache describes, in screen coordinates
; out: CF = 0 and wm_su_v* armed if any of it is on the glass; CF = 1 if none
;      of it is. Preserves every register.
;
; This is vid_span_one's question asked for an ANSWER instead of a verdict: it
; tested x2/y2 against the display's far edges and refused, and a drag is the
; one caller for which refusing is the wrong response - the pixels are banked
; and correct, and the only thing wrong with them is that some are headed off
; the screen. So the far edges CLAMP rather than refuse.
;
; The near edges still refuse, and that is vid_span_one's dead-zone test kept
; verbatim: vid_disp_of falls back to the PRIMARY for a point no display
; covers, so an origin outside its own display's bounds means the answer is a
; fallback rather than a fact, and clamping against the wrong display's far
; edge would put the restore on the wrong card. ui_drag clamps x >= 0 and
; y >= MBAR_H, so the overhang a drag can produce is always at x2/y2.
;
; ...AND A FAR EDGE MAY ONLY CLAMP AWAY WHAT IS NOT ANOTHER DISPLAY'S GLASS
; (SPEC.md 11.96.14.1). That is the whole distinction this routine turns on and
; the first version did not make it: "past this display's right edge" is two
; completely different facts on an extended desktop. Off the DESKTOP, there is
; nothing there to draw and clipping the replay is exactly right - which is the
; window-dragged-off-the-bottom case this routine was written for. Across a
; SEAM, the pixels being clipped away are on a monitor the user is looking at,
; and a clipped restore is silent: wm_su_try answers CF = 0, wm_draw_win reads
; that as "the content is already back" and skips both the white fill and
; W_PAINT, so nothing draws the other display's slice at all. Reported from the
; field as a window dragged across the seam appearing on the primary and not on
; the secondary, and coming right when dragged a SECOND time - the second drag's
; SOURCE straddles, so wm_su_take's vid_span_one refuses, no cache is taken and
; the window redraws in full.
;
; So the far edge asks vid_disp_find - the same question vid_disp_of asks,
; allowed to answer NO - about the first pixel it is about to discard. A display
; holds it: refuse, and the caller repaints. Nothing does: clamp, as before.
; One point answers for the whole cut edge because the displays are placed edge
; to edge in one of two arrangements (SPEC.md 39.19.2), so the cut runs along a
; seam or along the desktop's own boundary and never along both.
; -----------------------------------------------------------------------------
wm_su_vset:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
%ifdef KERN_BIG
    mov si, ax                  ; SI = x1; vid_disp_of ANSWERS in AL
    call vid_disp_of
    call vid_ctx_ptr            ; DI = the display holding (x1, y1)
    mov ax, [di+VID_CTX_VX]
    cmp si, ax
    jb .no                      ; the dead zone: see above
    cmp bx, [di+VID_CTX_VY]
    jb .no
    add ax, [di+VID_CTX_CW]
    dec ax                      ; AX = the last legal x on this display
    cmp cx, ax
    jle .xok
    push ax                     ; ...and what is one column past it? BX is
    inc ax                      ; still y1, which the near-edge test above has
    call vid_disp_find          ; already proved is a row THIS display has - so
    pop ax                      ; a hit is a seam and a miss is the desktop's
    jnc .no                     ; own edge (a pop touches no flag, so CF lives)
    mov cx, ax
.xok:
    mov ax, [di+VID_CTX_VY]
    add ax, [di+VID_CTX_CH]
    dec ax
    cmp dx, ax
    jle .yok
    push ax                     ; ...the same question one row further down,
    push bx                     ; at x1 - which is this display's by the same
    mov bx, ax                  ; test. vid_disp_find takes the point in AX/BX
    inc bx                      ; and answers in AL, so x has to be put there
    mov ax, si
    call vid_disp_find
    pop bx
    pop ax
    jnc .no
    mov dx, ax
.yok:
    mov ax, si                  ; x1 BACK: the bounds arithmetic above spent AX
%else
    or ax, ax                   ; the same two questions where there is only
    js .no                      ; ever one display
    or bx, bx
    js .no
    mov si, [vid_cw]
    dec si
    cmp cx, si
    jle .xok
    mov cx, si
.xok:
    mov si, [vid_ch]
    dec si
    cmp dx, si
    jle .yok
    mov dx, si
.yok:
%endif
    cmp ax, cx                  ; ...and an empty intersection is a refusal
    jg .no                      ; again, because there is nothing to put back
    cmp bx, dx
    jg .no
    mov [wm_su_vx1], ax
    mov [wm_su_vy1], bx
    mov [wm_su_vx2], cx
    mov [wm_su_vy2], dx
    mov word [wm_su_von], 1
    clc
    jmp short .out
.no:
    stc
.out:
    pop di                      ; a pop touches no flag, so CF survives
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
wm_su_srect:
    push si
    mov ax, [wm_su_x1]
    mov bx, [wm_su_y1]
    mov cx, [wm_su_x2]
    mov dx, [wm_su_y2]
    cmp word [wm_su_son], 0
    jne .named
    cmp word [wm_su_von], 0     ; ...the display clip ALONE is still a sub-rect
    jne .named                  ; (SPEC.md 11.96.14)
    jmp .whole
.named:
                                ; THE ONE-SHOT IS NOT SPENT HERE (SPEC.md
                                ; 11.96.11.1). It used to be, so a skipped
                                ; window could not pass its rect to the next -
                                ; but a cache of FRAGMENTS calls this once per
                                ; fragment, and wm_damage owes the app the same
                                ; rect afterwards. wm_draw_win clears it at its
                                ; exit and wm_dmg_wins at .wdone, which is the
                                ; same guarantee at the two sites that can skip
    cmp word [wm_su_son], 0     ; EACH named rect is intersected only if it is
    je .vis                     ; armed - the two are independent one-shots and
                                ; either may be the only one set
    cmp ax, [wm_su_sx1]
    jge .x1
    mov ax, [wm_su_sx1]
.x1:
    cmp bx, [wm_su_sy1]
    jge .y1
    mov bx, [wm_su_sy1]
.y1:
    cmp cx, [wm_su_sx2]
    jle .x2
    mov cx, [wm_su_sx2]
.x2:
    cmp dx, [wm_su_sy2]
    jle .y2
    mov dx, [wm_su_sy2]
.y2:
.vis:
    cmp word [wm_su_von], 0     ; ...and then what is actually ON THE GLASS
    je .isect                   ; (SPEC.md 11.96.14)
    cmp ax, [wm_su_vx1]
    jge .vx1
    mov ax, [wm_su_vx1]
.vx1:
    cmp bx, [wm_su_vy1]
    jge .vy1
    mov bx, [wm_su_vy1]
.vy1:
    cmp cx, [wm_su_vx2]
    jle .vx2
    mov cx, [wm_su_vx2]
.vx2:
    cmp dx, [wm_su_vy2]
    jle .vy2
    mov dx, [wm_su_vy2]
.vy2:
.isect:
    cmp ax, cx
    jg .none
    cmp bx, dx
    jg .none
    mov si, wm_su_x1            ; the four words the buffer was laid out for
    call gfx_sub_arm
    jc .whole
    clc
    pop si
    ret
.whole:
    mov ax, [wm_su_x1]
    mov bx, [wm_su_y1]
    mov cx, [wm_su_x2]
    mov dx, [wm_su_y2]
    clc
    pop si
    ret
.none:
    stc
    pop si
    ret

; wm_su_rect - DI = window -> AX/BX/CX/DX = its content rect, the same four
; numbers wm_draw_win's white fill uses
wm_su_rect:
    mov bx, di                  ; (BX is an OUTPUT here, so borrowing it for
    call wm_bordr               ; the question costs nothing - SPEC.md 11.95.3)
    mov cx, [di+W_X]
    add cx, [di+W_W]
    dec cx
    sub cx, ax
    call wm_bord
    add ax, [di+W_X]
    mov bx, [di+W_Y]
    add bx, TITLE_H
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    sub dx, 2
    ret

; --- SPEC.md 11.96.11.1: the cached region as up to FOUR FRAGMENTS ----------
;
; A window names an extent on each of the four edges (wm_band) and the kernel
; caches those bands: everything the app did NOT keep. Expressed that way the
; KEPT part is exactly the content inset by the four extents - a RECTANGLE for
; any combination of them - which is what lets wm_damage answer it with four
; moves and no region arithmetic. The cached part is up to four strips.
;
; TOP and BOTTOM are full width; LEFT and RIGHT take what is between them. They
; do not overlap, so nothing has to reason about the order they go back in.
;
; WITH NO EXTENTS AT ALL fragment 0 is the whole content and 1..3 are empty, so
; a window that never named a band takes exactly the path it always did.

; wm_su_eptr - SI = &wm_su_ext[DI's slot], four bytes: left, right, top, bottom
; clobbers: SI and flags
wm_su_eptr:
    push ax
    push bx
    mov bx, di
    call wm_ptr2idx             ; AL = the slot, AH = 0
    mov si, ax
    add si, si
    add si, si
    add si, wm_su_ext
    pop bx
    pop ax
    ret

; wm_su_flay - lay DI's cached region out (SPEC.md 11.96.11.1)
; in:  DI = window ptr
; out: wm_su_x1..y2 = its CONTENT rect, wm_su_kex[0..3] = the extents clamped
;      to fit it, wm_su_kany = are there any, wm_su_ftab = the four fragments
;      (an empty one has x1 > x2). Every register preserved.
;
; The clamp is left-then-right and top-then-bottom, each into what the one
; before it left, so two bands that together overrun the content cannot make a
; fragment that reaches outside it. Everything downstream reads the CLAMPED
; figures - the header included - so a resize that shrinks the window past a
; band invalidates the cache through the ordinary rect comparison.
%ifdef KERN_BIG
; wm_su_arm - tell the next gfx_save/gfx_restore how deep this cache is
; Preserves every register and the flags. SU1ARM is the call, and it assembles
; to NOTHING in kern_small: that build has no WF_1BPP path at all, so
; WM_SU_PW is [vid_planes_w] and a 1 there is a mono adapter, where
; vga_save_vram has already branched to the software renderer.
;
; ARMED PER CALL, because gfx_save and gfx_restore spend it. The window
; between the arm and the call belongs to the gfx lock - vga12.inc's own
; conventions say the mouse ISR never draws while it is held, which is the
; same guarantee vga_plane and vga_off have always run on.
wm_su_arm:
    pushf
    cmp word [WM_SU_PW], 1
    jne .out
    call gfx_1bpp_on
.out:
    popf
    ret

; wm_su_1bpp - may window DI's cache hold ONE plane? (SPEC.md 11.96.17)
; in:  DI = the window; wm_su_x1/x2 = the content rect just laid out
; out: CF = 0 yes; every register preserved
;
; TWO questions, and the second is not about the window at all.
;
; It must have DECLARED it. Four identical planes are one plane of content,
; and only the application knows whether its planes are identical.
;
; And the rect must be WHOLE BYTES. gfx_restore writes x1 rounded down and x2
; up, so a content ending mid-byte overhangs its own window, and 11.96.2's
; wm_su_edge repairs that overhang by reading the screen back - which on a VGA
; is somebody else's COLOUR, and one plane of buffer cannot carry it.
; Broadcast through Map Mask 0Fh it would come back as 15 or 0: a stripe of
; flattened neighbour down the side of the window, up to 6px left and 5px
; right. SPEC.md 11.94.5 snaps both edges so whole bytes are the ordinary
; answer, and the two windows they are not are the one that opted out
; (WF_NOSNAP) and the one that spans the screen, whose content is W_W-1 and
; whose width 11.95.2 will not let anyone touch.
wm_su_1bpp:
    test word [di+W_FLAGS], WF_1BPP
    jz .no
    cmp byte [wm_su_kany], 0
    jne .no                     ; ...AND IT MAY NOT HAVE NAMED A BAND. The
                                ; test below is about the CONTENT rect, and
                                ; 11.96.11.1's LEFT and RIGHT fragments are
                                ; sub-spans of it that can end mid-byte even
                                ; when it does not - so a banded window could
                                ; hand wm_su_edge a ragged fragment over a
                                ; one-plane buffer. Nothing wants both yet
                                ; (a band needs WF_OWNBG), and refusing is a
                                ; cache the ordinary depth still takes
    push ax
    mov ax, [wm_su_x1]
    test al, 7                  ; the content's FIRST byte is whole...
    jnz .popno
    mov ax, [wm_su_x2]
    and al, 7                   ; ...and its LAST one is: x2 must be the eighth
    cmp al, 7                   ; pixel of its byte, not the first of the next
    pop ax
    jne .no
    clc
    ret
.popno:
    pop ax
.no:
    stc
    ret
%endif

wm_su_flay:
    push ax
    push bx
    push cx
    push dx
    push si
    call wm_su_rect
    mov [wm_su_x1], ax
    mov [wm_su_y1], bx
    mov [wm_su_x2], cx
    mov [wm_su_y2], dx

%ifdef KERN_BIG
    push cx                     ; ...and HOW DEEP the display holding it is,
    push dx                     ; before anything sizes a buffer from it. Here
    call vid_disp_planes        ; because this is the one routine both the take
    mov [WM_SU_PW], cx          ; and the check reach the content rect through
    pop dx                      ; (AX/BX are still its top-left)
    pop cx
%endif                          ; kern_small: WM_SU_PW IS [vid_planes_w] and
                                ; there is nothing to establish

    call wm_su_eptr
    mov ax, cx
    sub ax, [wm_su_x1]
    inc ax                      ; AX = the content's width...
    mov bx, ax
    mov al, [si]
    xor ah, ah
    cmp ax, bx
    jbe .l
    mov ax, bx
.l:
    mov [wm_su_kex+0], al
    sub bx, ax                  ; ...and what the left band left of it
    mov al, [si+1]
    xor ah, ah
    cmp ax, bx
    jbe .r
    mov ax, bx
.r:
    mov [wm_su_kex+1], al
    mov ax, dx
    sub ax, [wm_su_y1]
    inc ax                      ; AX = the height, same again
    mov bx, ax
    mov al, [si+2]
    xor ah, ah
    cmp ax, bx
    jbe .t
    mov ax, bx
.t:
    mov [wm_su_kex+2], al
    sub bx, ax
    mov al, [si+3]
    xor ah, ah
    cmp ax, bx
    jbe .b
    mov ax, bx
.b:
    mov [wm_su_kex+3], al
    mov al, [wm_su_kex+0]
    or al, [wm_su_kex+1]
    or al, [wm_su_kex+2]
    or al, [wm_su_kex+3]
    mov [wm_su_kany], al
%ifdef KERN_BIG
    call wm_su_1bpp             ; ...OR ONE PLANE (SPEC.md 11.96.17), and HERE
    jc .deep                    ; rather than beside vid_disp_planes because
    mov word [WM_SU_PW], 1      ; the answer needs [wm_su_kany], which the
.deep:                          ; clamping above has only just settled
    mov al, [wm_su_kany]        ; (...and the branch below wants its flags)
    or al, al
%endif
    jnz .frags
    mov ax, [wm_su_x1]          ; no bands: fragment 0 is the whole content
    mov [wm_su_ftab+0], ax
    mov ax, [wm_su_y1]
    mov [wm_su_ftab+2], ax
    mov ax, [wm_su_x2]
    mov [wm_su_ftab+4], ax
    mov ax, [wm_su_y2]
    mov [wm_su_ftab+6], ax
    mov si, 1
    call wm_su_fnil
    mov si, 2
    call wm_su_fnil
    mov si, 3
    call wm_su_fnil
    jmp .out
.frags:
    ; --- 0 TOP: (x1, y1, x2, y1 + t - 1), full width -----------------------
    mov al, [wm_su_kex+2]
    xor ah, ah
    or ax, ax
    jz .notop
    add ax, [wm_su_y1]
    dec ax
    mov [wm_su_ftab+6], ax
    mov ax, [wm_su_x1]
    mov [wm_su_ftab+0], ax
    mov ax, [wm_su_y1]
    mov [wm_su_ftab+2], ax
    mov ax, [wm_su_x2]
    mov [wm_su_ftab+4], ax
    jmp short .bot
.notop:
    xor si, si
    call wm_su_fnil
.bot:
    ; --- 1 BOTTOM: (x1, y2 - b + 1, x2, y2), full width --------------------
    mov al, [wm_su_kex+3]
    xor ah, ah
    or ax, ax
    jz .nobot
    mov bx, [wm_su_y2]
    sub bx, ax
    inc bx
    mov [wm_su_ftab+10], bx
    mov ax, [wm_su_x1]
    mov [wm_su_ftab+8], ax
    mov ax, [wm_su_x2]
    mov [wm_su_ftab+12], ax
    mov ax, [wm_su_y2]
    mov [wm_su_ftab+14], ax
    jmp short .mid
.nobot:
    mov si, 1
    call wm_su_fnil
.mid:
    ; --- the rows LEFT and RIGHT share: y1 + t .. y2 - b -------------------
    mov al, [wm_su_kex+2]
    xor ah, ah
    add ax, [wm_su_y1]
    mov bx, ax                  ; BX = the middle band's first row
    mov al, [wm_su_kex+3]
    xor ah, ah
    mov cx, [wm_su_y2]
    sub cx, ax                  ; CX = ...and its last
    cmp bx, cx
    jle .side
    mov si, 2                   ; nothing between the strips: no sides
    call wm_su_fnil
    mov si, 3
    call wm_su_fnil
    jmp short .out
.side:
    ; --- 2 LEFT: (x1, mid1, x1 + l - 1, mid2) ------------------------------
    mov al, [wm_su_kex+0]
    xor ah, ah
    or ax, ax
    jz .noleft
    add ax, [wm_su_x1]
    dec ax
    mov [wm_su_ftab+20], ax
    mov ax, [wm_su_x1]
    mov [wm_su_ftab+16], ax
    mov [wm_su_ftab+18], bx
    mov [wm_su_ftab+22], cx
    jmp short .right
.noleft:
    mov si, 2
    call wm_su_fnil
.right:
    ; --- 3 RIGHT: (x2 - r + 1, mid1, x2, mid2) -----------------------------
    mov al, [wm_su_kex+1]
    xor ah, ah
    or ax, ax
    jz .noright
    mov dx, [wm_su_x2]
    sub dx, ax
    inc dx
    mov [wm_su_ftab+24], dx
    mov [wm_su_ftab+26], bx
    mov ax, [wm_su_x2]
    mov [wm_su_ftab+28], ax
    mov [wm_su_ftab+30], cx
    jmp short .out
.noright:
    mov si, 3
    call wm_su_fnil
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wm_su_fnil - mark fragment SI empty (x1 > x2, which every reader already
;              treats as nothing to do). Preserves every register.
wm_su_fnil:
    push ax
    push si
    add si, si
    add si, si
    add si, si
    mov ax, 1
    mov [si+wm_su_ftab+0], ax
    xor ax, ax
    mov [si+wm_su_ftab+2], ax
    mov [si+wm_su_ftab+4], ax
    mov [si+wm_su_ftab+6], ax
    pop si
    pop ax
    ret

; wm_su_frag - fragment [wm_su_fn] (0..3) of the laid-out region
; in:  [wm_su_fn] = 0..3, in memory rather than a register because every loop
;      over the fragments wants SI for gfx_restore's buffer; wm_su_ftab valid
; out: CF = 0 and AX/BX/CX/DX = it; CF = 1 = empty, and there is nothing to do
; clobbers: the four outputs and flags
wm_su_frag:
    push si
    mov si, [wm_su_fn]
    add si, si
    add si, si
    add si, si
    mov ax, [si+wm_su_ftab+0]
    mov bx, [si+wm_su_ftab+2]
    mov cx, [si+wm_su_ftab+4]
    mov dx, [si+wm_su_ftab+6]
    pop si
    cmp ax, cx
    jg .no
    cmp bx, dx
    jg .no
    clc
    ret
.no:
    stc
    ret

; wm_su_bytes - [wm_su_fsz] = the buffer bytes gfx_save writes for the rect in
;               AX/BX/CX/DX, which is what a later fragment's offset is past.
; out: CF = 1 = it does not fit a word; every register preserved
;
; The +3 is wm_su_kb's own: the byte span, plus the TWO EDGE COLUMNS 11.96.2's
; merge keeps its scratch in. Both this and wm_su_kb read it from here, so the
; claim's size and the offsets inside it cannot disagree.
wm_su_bytes:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, cx
    mov di, ax
    mov cl, 3
    shr si, cl
    shr di, cl
    sub si, di
    inc si                      ; span -> byte count. NOT +3: the two extra
                                ; columns wm_su_kb used to add are 11.96.2's
                                ; edge SCRATCH, which lives past the whole
                                ; image and is not part of any fragment
    mov ax, dx
    sub ax, bx
    inc ax
    mul si
    or dx, dx
    jnz .big
    mov si, [WM_SU_PW]          ; the display THIS CACHE IS ON (4.3), not the
    mul si                      ; one that happens to be current
    or dx, dx
    jnz .big
    mov [wm_su_fsz], ax
    clc
    jmp short .out
.big:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wm_su_scrset - where the fragments end and 11.96.2's edge scratch begins
; out: [wm_su_scrb] = that offset, [wm_su_scrsz] = the scratch's own size;
;      CF = 1 = it does not fit a word. Every register preserved.
;
; THE SCRATCH IS SHARED AND SITS PAST EVERY FRAGMENT (SPEC.md 11.96.11.1). Each
; edge merge wants two byte columns of working room and the merges are
; sequential - one fragment is wholly back on screen before the next is
; touched - so one area sized for the TALLEST fragment serves them all. It used
; to be computed inside wm_su_edge as "past this rect's image", which with one
; fragment was the same address and with four lands inside the next one's
; pixels. That is what a 605-pixel stripe down the middle of Paint's tool
; column turned out to be.
wm_su_scrset:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                  ; DI = the images together
    xor si, si                  ; SI = the tallest fragment's rows
    mov word [wm_su_fn], 0
.f:
    call wm_su_frag
    jc .fnext
    push si
    call wm_su_bytes
    pop si
    jc .big
    add di, [wm_su_fsz]
    jc .big
    sub dx, bx
    inc dx
    cmp dx, si
    jbe .fnext
    mov si, dx
.fnext:
    inc word [wm_su_fn]
    cmp word [wm_su_fn], 4
    jb .f
    add di, WSU_IMG
    jc .big
    mov [wm_su_scrb], di
    mov ax, si
    add ax, ax                  ; two byte columns...
    mov cx, [WM_SU_PW]
    mul cx                      ; ...x planes, of the tallest fragment
    or dx, dx
    jnz .big
    mov [wm_su_scrsz], ax
    clc
    jmp short .out
.big:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wm_su_bget - CF = 0 if DI named any band at all (SPEC.md 11.96.11)
; clobbers: flags only
wm_su_bget:
    push ax
    push si
    call wm_su_eptr
    mov al, [si]
    or al, [si+1]
    or al, [si+2]
    or al, [si+3]
    pop si
    or al, al
    pop ax
    jz .no
    clc
    ret
.no:
    stc
    ret

; wm_su_orect - the part of DI's content the APP still owes once the cached
;               bands have come back: the content INSET by the four extents,
;               and possibly EMPTY (x1 > x2), which is legal and means the
;               bands were the whole of it.
; in:  DI = window ptr
; out: AX/BX/CX/DX; clobbers those and flags
;
; It insets by wm_su_flay's CLAMPED extents, so the kept rect and the fragments
; are complementary by construction rather than by two arithmetics agreeing.
wm_su_orect:
    push si
    push bp                     ; BP as a plain scratch REGISTER, which is
    call wm_su_flay             ; legal - what SS != DS forbids is [bp+d]
    mov bx, [wm_su_y1]
    mov cx, [wm_su_x2]
    mov dx, [wm_su_y2]
    mov bp, [wm_su_x1]
    mov al, [wm_su_kex+0]
    xor ah, ah
    add bp, ax
    mov si, bp                  ; SI = the inset x1, banked over the rest
    mov bp, cx
    mov al, [wm_su_kex+1]
    xor ah, ah
    sub bp, ax
    mov cx, bp
    mov bp, bx
    mov al, [wm_su_kex+2]
    xor ah, ah
    add bp, ax
    mov bx, bp
    mov bp, dx
    mov al, [wm_su_kex+3]
    xor ah, ah
    sub bp, ax
    mov dx, bp
    mov ax, si
    pop bp
    pop si
    ret

; wm_su_slot - which cache belongs to a window
; in:  BX = window ptr
; out: SI = -> its wm_su_segs word, BX = its owner tag (the base plus the
;      slot, SPEC.md 11.96.3); clobbers nothing else (flags)
wm_su_slot:
    push ax
    call wm_ptr2idx             ; AL = slot, AH = 0 (and it IS 0 now)
    mov si, ax
    add si, ax                  ; a word per slot
    add si, wm_su_segs
    add ax, MEM_P_WSAVE         ; ...and the tag carries it too, so the shed
    mov bx, ax                  ; can find the word again (mem_pg_own)
    pop ax
    ret

; wm_su_drop_all - forget EVERY window's cache (SPEC.md 39.14.8.1)
; preserves every register and the flags
;
; A display change is the one event wm_su_ck cannot see. It compares the banked
; rect against the window's rect NOW, which catches a move, a resize and a
; refit - and a window small enough to fit both geometries does not move, while
; the pixels behind it were read off a different card, at a different stride,
; and possibly with a different number of planes. vid_setmode has just cleared
; the framebuffer as well, so there is nothing to keep.
wm_su_drop_all:
    pushf
    push ax
    push bx
    push cx
    xor cx, cx
.one:
    mov ax, cx
    call wm_idx2ptr             ; BX = the record (AL = the index)
    call wm_su_drop
    inc cx
    cmp cx, MAX_WIN
    jb .one
    pop cx
    pop bx
    pop ax
    popf
    ret

; wm_su_drop - forget window BX's cache and give the memory back
; preserves every register and the flags
wm_su_drop:
    pushf
    push ax
    push bx
    push dx
    push si
                                ; NO wm_win_ok HERE, and that is a size
                                ; decision rather than a judgement that it is
                                ; safe: the BOOT_MARK kernel has tens of bytes
                                ; of its 64KB segment left (docs/KERNEL-MEMORY.md),
                                ; so the check lives at the two API doors a
                                ; package can reach and every caller below them
                                ; passes wm_idx2ptr's own answer
    call wm_su_slot             ; SI = its word, BX = its tag
    mov dx, [si]
    or dx, dx
    jz .out
    mov word [si], 0
    call mem_free
.out:
    pop si
    pop dx
    pop bx
    pop ax
    popf
    ret

; wm_su_stale - forget window BX's cache, and refuse to take another until it
;               has been drawn whole (SPEC.md 11.96.16.1)
; preserves every register and the flags
;
; wm_su_drop was the whole of what fmv_store did, and "no cache" used to be
; indistinguishable from "please take one" because nothing but wm_draw_win
; ever took one - so the drop and the redraw could not get out of step. The
; moment a window can be banked from anywhere else (wm_su_precover), the
; difference matters: fmv_bcast replaces a SIBLING window's listing and
; deliberately does not repaint it, so its pixels are the folder as it was,
; and a cache taken from them is docs/FIELD-NOTES.md 4's stale listing with a
; new way in. The flag is that difference, said out loud.
wm_su_stale:
    pushf                       ; `or` writes flags and wm_su_drop banks its
    or word [bx+W_FLAGS], WF_STALE  ; OWN caller's, which would be these - so
    popf                        ; the restore has to happen on this side of it
    jmp wm_su_drop

; wm_su_ck - CF=0 if the cache describes window DI as it is right now, and
;            then AX = the claim's segment and wm_su_x1..y2 hold the rect it
;            was taken over. AX is scratch on CF=1.
wm_su_ck:
    push bx
    push cx
    push dx
    push si
    push es
    mov bx, di
    call wm_su_slot
    mov si, [si]                ; SI = the claim, 0 = no cache for this window
    or si, si
    jz .no
    call wm_dc_ok
    jc .no                      ; THE PROMISE IS TESTED AT THE RESTORE TOO, not
                                ; only at the take. wm_saveu_set drops the cache
                                ; when an app withdraws it, so this is belt and
                                ; braces there - but it is the only thing making
                                ; "clear the flag and the raise must draw" true
                                ; for a flag cleared any OTHER way, which is
                                ; exactly what tools/notepad/pixcheck.py does to
                                ; force a full repaint (SPEC.md 11.96.2)
    mov es, si                  ; the rect is the CLAIM's, not a fifth array:
    call wm_su_flay             ; it travels with the pixels it describes. It
                                ; is the CONTENT rect and the four clamped
                                ; extents (SPEC.md 11.96.11.1), because those
                                ; two together are what the fragment layout is
                                ; a function of - so a band that changed, or a
                                ; window that resized past one, disagrees here
                                ; with no new comparison anywhere
    mov ax, [wm_su_x1]
    cmp ax, [es:WSU_X1]
    jne .no
    mov ax, [wm_su_y1]
    cmp ax, [es:WSU_Y1]
    jne .no
    mov ax, [wm_su_x2]
    cmp ax, [es:WSU_X2]
    jne .no
    mov ax, [wm_su_y2]
    cmp ax, [es:WSU_Y2]
    jne .no
    mov ax, [wm_su_kex+0]
    cmp ax, [es:WSU_EXT+0]
    jne .no
    mov ax, [wm_su_kex+2]
    cmp ax, [es:WSU_EXT+2]
    jne .no
    mov ax, [WM_SU_PW]          ; ...and the DEPTH the bytes were laid out to
    cmp ax, [es:WSU_PW]         ; (SPEC.md 11.96.17): a two-colour claim
    jne .no                     ; withdrawn, or a window that has crossed to a
                                ; display of another depth, leaves a buffer
                                ; that means something else at the same stride
    mov ax, si                  ; ...and the segment, for wm_su_try
    clc
    jmp short .out
.no:
    stc
.out:
    pop es                      ; a pop touches no flag, so CF survives
    pop si
    pop dx
    pop cx
    pop bx
    ret

; wm_su_take - bank window DI's content, because it is about to be covered
; preserves every register; a refusal anywhere just leaves no cache
; -----------------------------------------------------------------------------
; The DRAG cache (SPEC.md 11.96.12) - a window MOVED puts its content back
; instead of drawing it again.
;
; It is not a second mechanism. A drag does not change the content, only where
; it goes, so this BANKS through wm_su_take at the old position and then moves
; the claim's header by the same delta - and wm_su_ck, which compares that
; header against the window's rect NOW, then agrees. Everything after that is
; the raise cache's own: the fragment layout, wm_su_edge's edge merge, the
; restore, the free.
;
; **EVERY window is eligible, and WF_SAVEU is not the reason.** 11.96.1's
; promise is about content changing while a window is merely COVERED, which is
; a question about arbitrary time; here the bank and the restore happen inside
; ONE gfx lock hold - ui_drag reaches .release holding it and does not unlock
; until after wm_paint_dmg - so no task, worker or otherwise, can run between
; them. (Workers are NOT suspended during the drag itself: ui_drag's tracking
; pass unlocks and yields on every tick. It does not matter, because the bank
; is taken after that loop has ended.) So the promise is waived for exactly
; the one window, through wm_dc_win, and never globally: a blanket waiver
; would let wm_su_bank take a RAISE cache for a window that has made no
; promise, which is 11.96.1's hazard with a new way in.
; -----------------------------------------------------------------------------

; wm_dc_ok - may window DI use the save-under machinery?
; out: CF = 0 yes; preserves every register
wm_dc_ok:
    test word [di+W_FLAGS], WF_SAVEU
    jnz .yes
    cmp di, [wm_dc_win]         ; a window record is never 0, so this cannot
    jne .no                     ; match the "nothing cached" state by accident
.yes:
    clc
    ret
.no:
    stc
    ret

; wm_dc_take - bank window BX's content before it moves, and relabel it
; in:  BX = the window, its W_X/W_Y ALREADY the new ones; ui_origx/y = the old
; out: nothing; preserves every register
wm_dc_take:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov di, bx
    call wm_su_rect             ; ...AND SOME OF IT ON SCREEN WHERE IT IS GOING
    call wm_su_vset             ; (SPEC.md 11.96.14). This asked for the WHOLE
    jc .out                     ; rect and refused, which cost the commonest
    mov word [wm_su_von], 0     ; drag there is: ui_drag clamps the TITLE BAR
                                ; onto the screen and not the window, so a tall
                                ; window dragged down hangs its bottom off, and
                                ; on Hercules Solitaire's 303 rows fill a
                                ; ~305-row band - its legal fully-on-screen y
                                ; was 18..20, so nearly every vertical drag
                                ; redrew in full. wm_su_try clips the restore
                                ; to the glass now, so the only thing worth
                                ; refusing here is a destination with NOTHING
                                ; of it visible. The arm is dropped again
                                ; immediately: this is a question, and the
                                ; answer that matters is asked at the restore
                                ; against the rect as it will be THEN
    mov ax, [di+W_X]
    sub ax, [ui_origx]
    test al, 7                  ; THE BYTE PHASE, and it is the whole of what
    jnz .out                    ; this cannot do: gfx_save lays the buffer out
                                ; from x1 rounded DOWN to a framebuffer byte,
                                ; so the pixels can be replayed at another x
                                ; only where the content sits at the same
                                ; offset inside its byte. Any dy is free -
                                ; rows are whole - so a vertical drag always
                                ; takes this path and a horizontal one takes it
                                ; once every eight pixels
%ifdef KERN_BIG
    mov ax, [di+W_X]            ; ...AND THE DEPTH IS THE SECOND THING IT
    mov bx, [di+W_Y]            ; CANNOT DO (docs/DUAL-DISPLAY-VGA.md 4.3.1).
    call vid_disp_planes        ; The byte phase above is this same argument
    mov si, cx                  ; about x; this is it about PLANES. gfx_save
    mov ax, [ui_origx]          ; lays the buffer out with the depth of the
    mov bx, [ui_origy]          ; display the pixels are ON, so a window banked
    call vid_disp_planes        ; on a Hercules is w*h/8 bytes of one plane and
    cmp cx, si                  ; replaying it onto a VGA writes four - the
    jne .out                    ; buffer then means something else where it
%endif                          ; lands, and mode 12h renders 1bpp bytes as
                                ; colour. Reported from the field on 86Box:
                                ; drag a Disk window off the Hercules onto the
                                ; VGA and its content arrives as magenta and
                                ; cyan noise inside a correct frame. Refusing
                                ; costs one full repaint on the one drag in a
                                ; session that crosses a depth change, and
                                ; Hercules+CGA can never reach it - both 1bpp,
                                ; so the two answers are always equal
    mov [wm_dc_win], di         ; ...the waiver, for this window alone
    mov cx, [di+W_X]            ; wm_su_take reads the geometry, and the pixels
    mov si, [di+W_Y]            ; are still where the window WAS - so put it
    mov ax, [ui_origx]          ; back for the length of the save
    mov [di+W_X], ax
    mov ax, [ui_origy]
    mov [di+W_Y], ax
    call wm_su_take
    mov [di+W_X], cx
    mov [di+W_Y], si
    mov bx, di
    call wm_su_slot             ; SI = the slot word, and a refusal anywhere in
    mov ax, [si]                ; wm_su_take just leaves it 0
    or ax, ax
    jz .drop
    mov es, ax
    mov ax, [di+W_X]            ; RELABEL: the header is the rect wm_su_ck
    sub ax, [ui_origx]          ; compares, so moving it by the drag's own
    add [es:WSU_X1], ax         ; delta is the whole trick. WSU_EXT is left
    add [es:WSU_X2], ax         ; alone because a drag does not resize, so the
    mov ax, [di+W_Y]            ; band extents it describes are unchanged
    sub ax, [ui_origy]
    add [es:WSU_Y1], ax
    add [es:WSU_Y2], ax
    jmp short .out
.drop:
    mov word [wm_dc_win], 0
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; wm_dc_done - the repaint is over: spend the waiver
; preserves every register and the flags
;
; A window that DOES promise keeps what was banked - it is an ordinary raise
; cache now, correctly labelled. One that does not must lose it here, or the
; next raise would put back a picture its worker has moved on from.
wm_dc_done:
    pushf
    push bx
    mov bx, [wm_dc_win]
    or bx, bx
    jz .out
    mov word [wm_dc_win], 0
    test word [bx+W_FLAGS], WF_SAVEU
    jnz .out
    call wm_su_drop
.out:
    pop bx
    popf
    ret

wm_su_take:
    KFZ 19                      ; ...taking one cache
    CURLOG 'P'
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call wm_dc_ok
    jc .out                     ; it made no promise, so its pixels may be
                                ; stale by the time it is raised (11.96.1)
    test word [di+W_FLAGS], WF_STALE
    jnz .out                    ; ...and the kernel has replaced its data
                                ; without redrawing it, so the pixels are a
                                ; picture of a folder that has moved on
                                ; (SPEC.md 11.96.16.1). Tested HERE and in no
                                ; other place, so every taker inherits it -
                                ; wm_su_bank's is free, wm_draw_win having
                                ; cleared the bit a moment earlier on exactly
                                ; the draws that earn it
    test word [di+W_FLAGS], 2   ; not visible: there are no pixels to bank
    jz .out
    test word [di+W_FLAGS], WF_FULL
    jnz .out                    ; a fullscreen window is not usefully covered
                                ; by what is being raised over it, and it is
                                ; the biggest block there is
                                ; THERE WAS A BLANKET `[vid_ndisp] > 1 -> .out`
                                ; HERE and it is deliberately gone (SPEC.md
                                ; 39.14.8). It was right when it was written -
                                ; gfx_save/gfx_restore carried no display hook,
                                ; so a bank taken while the other card was
                                ; current saved the wrong band of the wrong
                                ; monitor - and both halves of that reason have
                                ; since been removed: the two routines take
                                ; GFXDENTER now, and vid_span_one below refuses
                                ; exactly the rects that cannot be banked
                                ; instead of every rect on the second display.
                                ; Two trees fixed one bug, one bluntly and one
                                ; precisely, in different lines - so git merged
                                ; both and the blunt one won by being first.
                                ; tests/dispsave.py is what noticed.
    mov bx, di
    call wm_su_drop             ; whatever THIS window banked before: one
                                ; window, one claim, or the old one leaks
    call wm_su_flay             ; DI is still the window for this: the CONTENT
    mov ax, [wm_su_x1]          ; rect, the clamped extents and the fragment
    mov bx, [wm_su_y1]          ; layout, all three of which the header carries
    mov cx, [wm_su_x2]
    mov dx, [wm_su_y2]
    cmp ax, cx
    ja .out                     ; degenerate content: nothing to bank
    cmp bx, dx
    ja .out
%ifdef KERN_BIG
    call vid_span_one           ; WHOLLY INSIDE ONE DISPLAY or no cache, which
    jc .out                     ; is the screen test on a one-card machine and
                                ; a stronger one on two (SPEC.md 39.14.8).
                                ; [vid_cw] is the display LAST DRAWN ON
                                ; (39.14.3), not the screen, so the test this
                                ; replaces asked a question whose answer
                                ; depended on draw order
%else
    cmp cx, [vid_cw]
    jae .out                    ; WHOLLY ON SCREEN or no cache: vga_rect_setup
    cmp dx, [vid_ch]            ; CLIPS, and wm_su_edge's row count and stride
    jae .out                    ; are computed from the rect rather than read
                                ; back out of it, so a clipped one would index
                                ; the buffer wrongly. wm_fit keeps a window on
                                ; screen, so this refuses nothing in practice
%endif
    call wm_su_kb               ; AX = KB for every fragment together, the
    jc .out                     ; header included - or it does not fit one
                                ; segment at all
    mov bx, di
    call wm_su_slot             ; SI = its wm_su_segs word, BX = its own tag
    call mem_claim              ; DX = segment
    jc .out                     ; refused: no cache, and the raise repaints -
                                ; which is what it did before this existed
    mov es, dx
    push si                     ; the header first: the content rect and the
    mov si, wm_su_x1            ; four extents go WITH the pixels, so nothing
    xor di, di                  ; outside the claim has to be kept in step
    mov cx, 4
    cld
    rep movsw
    mov si, wm_su_kex
    mov cx, 2
    rep movsw
    mov ax, [WM_SU_PW]          ; ...AND THE DEPTH (SPEC.md 11.96.17), which
    mov [es:WSU_PW], ax         ; wm_su_flay has just decided and which the
    pop si                      ; bytes below are laid out to
    mov di, WSU_IMG             ; ...and the fragments start after it, in index
    mov word [wm_su_fn], 0      ; order and back to back - gfx_save walks DI
.frag:                          ; itself, and leaves it alone for an empty rect
    call wm_su_frag
    jc .fnext
    SU1ARM                      ; ...one plane or four (SPEC.md 11.96.17)
    call gfx_save
.fnext:
    inc word [wm_su_fn]
    cmp word [wm_su_fn], 4
    jb .frag
    mov ax, es
    mov [si], ax                ; PUBLISHED LAST, so a cache that was only
                                ; half taken can never be tried
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wm_su_kb - KB for the banked rect, menu_save_kb's arithmetic
; in: the four wm_su_* rect words
; out: CF=0, AX = KB (never 0); CF=1 = it cannot be banked at all
;
; The block is WSU_IMG + (bytes per row + 2) x rows x planes: the four-word
; header, the image gfx_save writes, and behind it wm_su_edge's two
; one-byte-wide screen columns, which are rows x planes each.
;
; IT REFUSES OVER 64KB rather than wrapping. gfx_save advances a 16-bit DI
; inside one segment, so a rect needing more than a segment cannot be banked
; however big the claim is - and the old arithmetic could not even ASK for the
; right size, because `mul` answers in DX:AX and the plane multiply threw DX
; away. A maximised window on VGA came to 135,432 bytes, which truncated to a
; 5KB claim that gfx_save then wrote 132KB into.
wm_su_kb:
    push bx
    push cx
    push dx
    push si
    push di
    call wm_su_scrset           ; the FRAGMENTS' images end at [wm_su_scrb] -
    jc .big                     ; header included - and 11.96.2's shared edge
    mov ax, [wm_su_scrb]        ; scratch takes [wm_su_scrsz] past that
    add ax, [wm_su_scrsz]
    jc .big
    cmp ax, 0xFC00
    ja .big                     ; ...nor could the rounding below survive it
    add ax, 1023
    mov cl, 10
    shr ax, cl
    or ax, ax
    jnz .out
    inc ax
.out:
    clc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.big:
    stc
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; wm_su_edge - make the banked buffer's two edge byte columns agree with the
;              screen OUTSIDE the window (SPEC.md 11.96.2)
; in:  ES = the claim; AX/BX/CX/DX = the rect gfx_restore is about to be handed
;      - the whole content rect, or 11.96.6's sub-rect of it; the four wm_su_*
;      words = the rect the buffer was SAVED over. Preserves every register.
;
; IT PATCHES THE BYTES THE RESTORE WILL WRITE, AND ONLY THOSE (SPEC.md 11.96.8).
; That used to be the whole banked rect because that was the whole restore, and
; it is the larger half of what a restore costs: 18.22 ms of a 47.86 ms restore
; on a 318x136 CGA Disk window, against the blit's 29.64. Two things fall out of
; asking about the rect rather than the buffer, and the second is the bigger:
; the rows walked are the SUB-RECT's, and an edge column the restore does not
; reach is not patched at all - a sub-rect whose left byte column is inside the
; banked rect has no overhang there to fix, because those pixels are the
; window's own content (SPEC.md 5.8's argument) and the cache already agrees
; with the screen about them.
;
; The MASKS stay the banked rect's, which is not an oversight: the byte being
; patched is the banked rect's edge byte - the restore only reaches it when the
; sub-rect's edge is in the same byte column - so the overhang to repair is that
; rect's overhang. The sub-rect's own mask would also be correct and no tighter,
; the difference between them being pixels the cache and the screen agree on.
;
; gfx_restore writes WHOLE BYTES - x1 rounded down and x2 up - so putting a
; content rect back paints up to 6 pixels to the left of the window's frame
; and up to 5 to the right of its drop shadow. For the menu save-under and the
; cursor that costs nothing, because nothing underneath them can change
; between the save and the restore; the raise cache is the one caller where
; something always has, since the window it was covered by has just drawn.
; Measured on CGA, after the ES:SI fix below: 6 pixels of the Disk window
; beside Note Pad, replaced with the copy taken before that window was raised.
;
; A masked write would be the other fix, and it would mean edge read-modify-
; writes in sw_xfer and a Bit Mask plus a latch load in vga_restore_vram - two
; primitives the cursor is on, to serve one caller. So the merge is done in
; RAM instead: read what the screen holds in those two byte columns NOW, and
; patch the bits OUTSIDE the rect into the buffer before restoring it. The
; write is then byte-for-byte what is already there wherever it overhangs.
;
; vga_rect_setup's own mask tables answer which bits those are, the one-byte
; rect included (x1 and x2 in the same byte: one column, one combined mask).
wm_su_edge:
    mov si, [wm_su_x1]          ; A WHOLE-BYTE RECT HAS NO OVERHANG, so there
    test si, 7                  ; is nothing outside it in either edge byte
    jnz .work                   ; and both masks come out FFh - which makes
    mov si, [wm_su_x2]          ; wm_su_merge write the buffer back over
    and si, 7                   ; itself, twice, after two gfx_saves that read
    cmp si, 7                   ; the screen for nothing. SPEC.md 11.96.8
    je .skip                    ; prices that at 18.22 ms of a 47.86 ms
.work:                          ; restore, and SPEC.md 11.94.5 snaps both
                                ; edges so it is now the ORDINARY case rather
                                ; than a lucky one. It is also what makes a
                                ; one-plane cache safe (11.96.17): the merge
                                ; is the only place a colour neighbour could
                                ; reach a 1bpp buffer
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [wm_su_ex1], ax         ; the rect being restored, banked: every
    mov [wm_su_ey1], bx         ; register is wanted below. FOUR WORDS OF ITS
    mov [wm_su_ex2], cx         ; OWN (SPEC.md 11.96.11.1) - it used to borrow
    mov [wm_su_ey2], dx         ; wm_su_sx1..sy2 on the reasoning that the
                                ; one-shot was already spent, and a CACHE OF
                                ; FRAGMENTS calls this once per fragment while
                                ; the sub-rect is still owed to every one of
                                ; them and to wm_damage after

    mov bx, [wm_su_x2]          ; BX = bpr, the buffer's stride: the BANKED
    mov cl, 3                   ; rect's bytes per row, whatever is restored
    shr bx, cl
    mov ax, [wm_su_x1]
    shr ax, cl
    sub bx, ax
    inc bx
    mov [wm_su_bpr], bx

    mov ax, [wm_su_ey2]         ; the restored rect's own rows, which is what
    sub ax, [wm_su_ey1]         ; every walk below is bounded by
    inc ax
    mov [wm_su_srow], ax
    mov cx, [WM_SU_PW]          ; ...x planes = the bytes gfx_save writes for
    mul cx                      ; one screen column, so the second column's
    mov [wm_su_nrow], ax        ; scratch starts that far past the first

    mov ax, [wm_su_y2]          ; the banked rect's rows, for the thing the
    sub ax, [wm_su_y1]          ; sub-rect cannot answer: how far a plane's
    inc ax                      ; unwalked tail is
    sub ax, [wm_su_srow]
    mul bx
    mov [wm_su_pext], ax        ; the rows of each plane this rect does not
                                ; cover, one whole buffer row each
    mov ax, [wm_su_scrb]        ; ...and the scratch is past EVERY FRAGMENT's
    mov [wm_su_scr], ax         ; image (SPEC.md 11.96.11.1), not just this
                                ; one's. Derived here it was the same address
                                ; while there was one fragment and lands inside
                                ; the next one's pixels when there are four

    mov ax, [wm_su_ey1]         ; where the restored rect's first row sits in
    sub ax, [wm_su_y1]          ; the buffer
    mul bx
    mov [wm_su_rofs], ax

    mov bx, [wm_su_x1]          ; the masks, from the rect primitives' tables
    and bx, 7
    mov al, [bx + vga_lmtab]
    mov [wm_su_lm], al
    mov bx, [wm_su_x2]
    and bx, 7
    mov al, [bx + vga_rmtab]
    mov [wm_su_rm], al

    ; --- which edge columns does the restore actually reach? -----------------
    ; The banked rect's left byte column is written only if the restored rect's
    ; left edge is in that same byte, and likewise on the right. Anywhere else
    ; the "overhang" is the window's own content, which this pass did not paint,
    ; so the cache and the screen already agree and there is nothing to merge.
    mov cl, 3
    mov ax, [wm_su_ex1]
    shr ax, cl
    mov bx, [wm_su_x1]
    shr bx, cl
    sub ax, bx
    mov [wm_su_dol], ax         ; 0 = the left column IS this rect's left byte
    mov ax, [wm_su_x2]
    shr ax, cl
    mov bx, [wm_su_ex2]
    shr bx, cl
    sub ax, bx
    mov [wm_su_dor], ax         ; 0 = ...and the right one is its right byte

    cmp word [wm_su_dol], 0     ; the LEFT byte column as it is on screen now,
    jne .noleft                 ; over the RESTORED rect's rows only
    mov ax, [wm_su_x1]
    mov bx, [wm_su_ey1]
    mov cx, ax                  ; x2 = x1: one byte wide, whole bytes copied
    mov dx, [wm_su_ey2]
    mov di, [wm_su_scr]
    SU1ARM                      ; the scratch is sized to this cache's depth,
    call gfx_save               ; so the read must be too - or gfx_save writes
.noleft:                        ; four planes into one plane's room
    cmp word [wm_su_dor], 0     ; ...and the right one
    jne .nomask
    mov ax, [wm_su_x2]
    mov bx, [wm_su_ey1]
    mov cx, ax
    mov dx, [wm_su_ey2]
    mov di, [wm_su_scr]
    add di, [wm_su_nrow]
    SU1ARM
    call gfx_save
.nomask:
    cmp word [wm_su_bpr], 1
    jne .two
    mov al, [wm_su_rm]          ; one byte column: it is both edges at once,
    and [wm_su_lm], al          ; so it keeps only the bits between them
.two:
    cmp word [wm_su_dol], 0
    jne .right
    mov si, [wm_su_scr]         ; left: buffer byte 0 of every restored row
    mov di, [wm_su_fbase]       ; ...of THIS FRAGMENT (SPEC.md 11.96.11.1),
    add di, [wm_su_rofs]        ; which is WSU_IMG only for the first one
    mov dl, [wm_su_lm]
    call wm_su_merge
.right:
    cmp word [wm_su_bpr], 1
    je .out                     ; ...and there is no second column to do
    cmp word [wm_su_dor], 0
    jne .out
    mov si, [wm_su_scr]
    add si, [wm_su_nrow]        ; right: buffer byte bpr-1 of every restored row
    mov di, [wm_su_bpr]
    dec di
    add di, [wm_su_fbase]
    add di, [wm_su_rofs]
    mov dl, [wm_su_rm]
    call wm_su_merge
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.skip:
    ret

; wm_su_merge - ES:DI = the buffer byte to patch in the restored rect's FIRST
;               row of plane 0, ES:SI = the screen column, DL = the bits to
;               KEEP from the buffer
;
; TWO LEVELS, and that is what bounding this to a sub-rect costs (SPEC.md
; 11.96.8). One flat walk of stride bpr was right while every restore covered
; the whole banked rect: the layout is plane-major and every plane holds the
; same rows, so all of them are contiguous at that stride. A sub-rect leaves a
; TAIL in each plane that the walk must step over - [wm_su_pext], the rows of
; the plane this rect does not cover - so the planes become an outer loop. The
; screen column is contiguous either way, because gfx_save wrote exactly the
; rows being merged.
;
; With no sub-rect armed [wm_su_pext] is 0 and [wm_su_srow] is every row, so
; this degenerates to the one flat walk it replaced.
wm_su_merge:
    push si
    push di
    push bp
    mov dh, dl
    not dh                      ; ...and the bits to take from the screen
    mov bx, [wm_su_bpr]
    mov bp, [WM_SU_PW]
.plane:
    mov cx, [wm_su_srow]
.row:
    mov al, [es:di]
    and al, dl
    mov ah, [es:si]
    and ah, dh
    or al, ah
    mov [es:di], al
    inc si
    add di, bx
    loop .row
    add di, [wm_su_pext]        ; past the rows of this plane the rect does not
    dec bp                      ; cover, and on to the next plane's copy of it
    jnz .plane
    pop bp
    pop di
    pop si
    ret

; wm_su_try - put window DI's content back from the cache
; out: CF = 0 the content is on screen and W_PAINT must NOT run; CF = 1 draw
;      it the ordinary way. Preserves every register.
; -----------------------------------------------------------------------------
; wm_su_occl - the named sub-rect MINUS every window above this one
; in:  DI = the window about to be restored; [wm_su_son]/[wm_su_s*] as armed
; out: [wm_su_pn] = the pieces in wm_clip_tab, 0 = restore the single rect.
;      Preserves every register.
;
; SPEC.md 11.96.15. This is 11.97's argument about the CHROME applied to the
; content: a pixel this window would put back under a window above it is a
; pixel that window is about to write anyway, because 11.91's marking is
; transitive and upwards and the draw loop runs back to front - so every
; visible window above this one that overlaps it is drawn LATER IN THIS SAME
; PASS. Dropping it is therefore not losing it.
;
; It matters because the damage rect for a DRAG is the union of where the
; window was and where it is, which for an eight-pixel move is very nearly the
; whole window - so every window under it was restoring almost all of its
; content to reveal an eight-pixel strip. Measured on the reported four-window
; cascade: the three background windows restored 122,739 pixels of a 2,880
; pixel reveal, and 2,134 after this.
;
; Both degradations are "restore the lot", which is what this did before it
; existed and can only cost time: an overflow past WM_CLIP_MAX, and an empty
; list (wm_covered has already skipped a wholly covered window, so that is
; unreachable rather than merely rare).
; -----------------------------------------------------------------------------
wm_su_occl:
    KFZ 21                      ; ...and the occluded part of it
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [wm_su_pn], 0
    mov word [wm_clip_n], 0
    cmp word [wm_su_son], 0
    je .out                     ; no sub-rect named: the whole content is owed
    mov ax, [wm_su_sx1]
    mov dx, [wm_su_sy1]
    mov cx, [wm_su_sx2]
    mov si, [wm_su_sy2]
    cmp ax, cx
    jg .none
    cmp dx, si
    jg .none
    call wm_clip_seed
    mov bx, di
    call wm_clip_occlf          ; BY THE FRAME AND NOT THE OCCUPIED BOX: the
    jc .none                    ; shadow's two corners are painted by nothing,
                                ; so claiming them stops this restore putting
                                ; back a pixel nothing above will write
    mov ax, [wm_clip_n]
    or ax, ax
    jz .none
    mov [wm_su_pn], ax
    jmp short .out
.none:
    mov word [wm_clip_n], 0
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wm_su_piece - name piece [wm_su_pi] as the sub-rect wm_su_srect intersects
; preserves every register
;
; ITS X EDGES ARE ROUNDED OUTWARD TO WHOLE BYTES, and that is what makes the
; pieces composable rather than a tidiness (SPEC.md 11.96.15.1). wm_su_edge
; patches the buffer's partial edge bytes with the screen as it is NOW, keeping
; only the bits the rect owns - so it OVERWRITES the cached content of the bits
; it does not own. One rect never noticed: those bits are outside the restore.
; Two pieces side by side in the same rows DO share an edge byte, and then the
; left one's merge replaces exactly the cached bits the right one is about to
; restore, which comes back as a stale sliver up to 8px wide down the join.
;
; Rounding out removes the case instead of sequencing it: an aligned edge owns
; its whole byte, so vga_lmtab/vga_rmtab hand wm_su_edge a full mask, the merge
; keeps everything and writes nothing back. Two pieces may then overlap by one
; byte column and both put the same cached bytes there, which is idempotent.
;
; What licenses reaching outward at all is that the piece is CLAMPED BACK to
; the caller's own rect (wm_su_bx1/bx2), which is what this restore put on
; screen whole before the reduction existed - so every piece is a subset of it
; and the worst case is the behaviour this replaced. The pixels reclaimed from
; an occluder belong to a window that overlaps ours and is therefore MARKED
; (11.91's transitive pass) and redrawn later in this same back-to-front walk.
wm_su_piece:
    push ax
    push si
    cmp word [wm_su_pn], 0
    je .out                     ; unreduced: the caller's own rect stands
    mov ax, [wm_su_pi]
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1                   ; x 8: four words a rect
    add si, wm_clip_tab
    mov ax, [si+WCR_X1]
    and ax, 0FFF8h              ; down to the byte, then back inside the rect
    cmp ax, [wm_su_bx1]         ; the caller handed us
    jge .x1
    mov ax, [wm_su_bx1]
.x1:
    mov [wm_su_sx1], ax
    mov ax, [si+WCR_Y1]
    mov [wm_su_sy1], ax
    mov ax, [si+WCR_X2]
    or ax, 7                    ; ...and up to the end of one
    cmp ax, [wm_su_bx2]
    jle .x2
    mov ax, [wm_su_bx2]
.x2:
    mov [wm_su_sx2], ax
    mov ax, [si+WCR_Y2]
    mov [wm_su_sy2], ax
    mov word [wm_su_son], 1
.out:
    pop si
    pop ax
    ret

wm_su_try:
    KFZ 20                      ; the reveal, answered from a cache (11.96.18)
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call wm_su_ck               ; ...which lays the fragments out and checks
    jc .no                      ; them against the claim's own header, and
                                ; answers AX = the claim
    mov es, ax
    mov ax, [wm_su_x1]          ; ...AND WHATEVER OF IT IS ON THE GLASS
    mov bx, [wm_su_y1]          ; (SPEC.md 11.96.14). This was vid_span_one and
    mov cx, [wm_su_x2]          ; a refusal, which is right for a rect whose
    mov dx, [wm_su_y2]          ; DISPLAY moved under it (39.14.8.1) and wrong
    call wm_su_vset             ; for a window DRAGGED off the bottom: the
    jc .no                      ; pixels are banked and correct and only some
                                ; of them are headed off the screen, so the
                                ; far edges clamp and the near ones still
                                ; refuse. An empty intersection is the old
                                ; answer, and a rect wholly on one display
                                ; arms a clip that changes nothing
    call wm_su_ck               ; (the clip spent AX, and the claim is what the
    jc .no                      ; restore below needs in ES)
    mov es, ax
    call wm_su_scrset           ; where 11.96.2's edge scratch lives, which is
    jc .no                      ; past EVERY fragment (SPEC.md 11.96.11.1)
    mov ax, [wm_su_sx1]         ; THE PIECES OF IT NOTHING ABOVE WILL COVER
    mov [wm_su_bx1], ax         ; (SPEC.md 11.96.15), and the caller's own rect
    mov ax, [wm_su_sy1]         ; banked across the walk: wm_damage still owes
    mov [wm_su_by1], ax         ; it to the app if the bands send us to W_PAINT
    mov ax, [wm_su_sx2]
    mov [wm_su_bx2], ax
    mov ax, [wm_su_sy2]
    mov [wm_su_by2], ax
%ifndef NOSUOCCL
    call wm_su_occl             ; (NOSUOCCL=1 skips only this, leaving the
%endif                          ; routine compiled: SPEC.md 11.96.15's A/B is
                                ; three bytes of kernel apart, so both sides
                                ; carry the same disk and the same free space
    mov di, WSU_IMG             ; DI = the running offset into it, the twin of
    mov word [wm_su_fn], 0      ; wm_su_take's - so the two walk the fragments
.frag:                          ; in one order and cannot disagree about where
    call wm_su_frag             ; any of them lives (SPEC.md 11.96.11.1)
    jc .fnext
    call wm_su_bytes            ; [wm_su_fsz] = this one's span in the buffer.
    jc .fnext                   ; It cannot overflow - wm_su_kb refused the
                                ; claim if it could - and skipping is the safe
                                ; answer if it somehow does
    mov [wm_su_fbase], di       ; ...and where it lives, for the edge merge
    mov [wm_su_x1], ax          ; THE FRAGMENT IS THE RECT THE BUFFER UNDER DI
    mov [wm_su_y1], bx          ; WAS LAID OUT FOR, which is what wm_su_srect
    mov [wm_su_x2], cx          ; intersects against and what wm_su_edge takes
    mov [wm_su_y2], dx          ; its stride and its masks from
    mov word [wm_su_pi], 0      ; ...ONCE PER PIECE (SPEC.md 11.96.15): the
.piece:                         ; buffer offset is the fragment's and does not
    call wm_su_piece            ; move, so DI is held across the whole inner
    call wm_su_srect            ; walk and advanced by wm_su_fsz after it
    jc .pnext                   ; only part of the buffer if a rect was named
    call wm_su_edge             ; the edge bytes overhang, so they are merged
                                ; with the screen as it is NOW before any of
                                ; this goes back (11.96.2) - AFTER the arm,
                                ; because it patches the bytes THIS restore
                                ; will write and no others (11.96.8). Its own
                                ; two gfx_save calls cannot disarm the
                                ; sub-rect: the disarm is gated on [sw_dir], so
                                ; a save never reaches it (SPEC.md 5.8)
    push di
    mov si, di                  ; GFX_RESTORE'S BUFFER IS ES:SI, NOT ES:DI -
    SU1ARM                      ; ...one plane or four (SPEC.md 11.96.17)
    call gfx_restore            ; the twin of gfx_save's, because one advances
    pop di                      ; a movsb source and the other its destination.
                                ; Handed DI, the restore read from the buffer
                                ; segment at the offset of the WINDOW RECORD -
                                ; tens of KB past a claim of a few, so the
                                ; content came back as whatever was there,
                                ; which is the reported blank window
.pnext:
    inc word [wm_su_pi]
    mov ax, [wm_su_pi]
    cmp ax, [wm_su_pn]
    jb .piece
.fskip:
    add di, [wm_su_fsz]         ; an EMPTY fragment occupies nothing, which is
.fnext:                         ; why the two exits differ
    inc word [wm_su_fn]
    cmp word [wm_su_fn], 4
    jb .frag
%ifndef REDRAWFULL
    mov byte [wm_su_bok], 1     ; the bands are on the glass again - including
%endif                          ; when nothing was put back, which says the
                                ; same thing (SPEC.md 11.96.11)
    clc                         ; THE CACHE IS NOT SPENT HERE. It used to be
    jmp short .out              ; dropped on the grounds that this window is
                                ; about to be the front one and will change
                                ; its own pixels - true of a raise and false
                                ; of a damage repaint, which redraws a window
                                ; that stays exactly where it was. wm_su_bank
                                ; below asks which of the two this is, and
                                ; drops it only for the first (SPEC.md 11.96.4)
.no:
    stc
.out:
    pushf                       ; CF IS THIS ROUTINE'S WHOLE ANSWER, and every
                                ; test below writes flags (SPEC.md 11.96.15).
                                ; Without this the `cmp` on the next line
                                ; cleared the `stc` above it, so EVERY cache
                                ; miss returned "the content is already on
                                ; screen" - wm_draw_win then skipped both the
                                ; white fill and W_PAINT, and every window in
                                ; the machine came up as bare chrome. The
                                ; hit path survived by luck, `cmp 0,0` leaving
                                ; the CF that `clc` had just set. wm_snap
                                ; banks its flags for the same reason (11.94)
    cmp word [wm_su_pn], 0      ; the caller's rect back, and the list disarmed:
    je .nopc                    ; wm_damage owes the app the rect it was HANDED
    push ax                     ; and not whichever piece the walk stopped on
    mov ax, [wm_su_bx1]
    mov [wm_su_sx1], ax
    mov ax, [wm_su_by1]
    mov [wm_su_sy1], ax
    mov ax, [wm_su_bx2]
    mov [wm_su_sx2], ax
    mov ax, [wm_su_by2]
    mov [wm_su_sy2], ax
    pop ax
    mov word [wm_su_pn], 0
    mov word [wm_clip_n], 0     ; ...AND ONLY IF WE ARMED IT. Unconditionally
                                ; here it also fires on the .no path above -
                                ; which is the ONLY path the REDRAWFULL
                                ; reference build ever takes, since nothing
                                ; banks there and wm_su_ck always refuses - so
                                ; a change that is meant to be invisible to
                                ; the reference was disarming a region the
                                ; reference had armed and never armed itself.
                                ; A one-shot is spent by its armer (11.96.14)
.nopc:
    mov word [wm_su_von], 0     ; THE DISPLAY CLIP DIES WITH THE RESTORE THAT
                                ; ARMED IT (SPEC.md 11.96.14), and it is spent
                                ; HERE rather than at wm_draw_win's exit like
                                ; wm_su_son because this routine is its only
                                ; armer - a one-shot whose arm and its spend
                                ; are in one routine cannot be leaked onto the
                                ; next window by a path that skips. Both exits,
                                ; because .no is reached AFTER the arm on the
                                ; second wm_su_ck. A `mov` touches no flag - it
                                ; is the `cmp` above that does, which is what
                                ; the pushf at .out is for
    popf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_draw_shadow - a window's drop shadow, the L at (+1,+1) (SPEC.md 11.91.4)
; in:  DI = the window; [gfx_color] = CBLACK; gfx lock held, region as the
;      caller left it
; out: nothing; clobbers AX, BX, DX, flags - wm_draw_win's own contract, which
;      is what lets this be lifted out of it unchanged
;
; Lifted out of wm_draw_win because it is separately OWED. 11.91.1's dither
; subtracts each window's FRAME and not its occupied box - deliberately, since
; the box holds two corners the shadow's L never covers - so the shadow is
; dithered over and put back by the window's own redraw. 11.91.2 then stopped
; redrawing a window whose ground was never uncovered, and those two are only
; jointly true while every window in the damage is redrawn. This is what a
; window that is NOT being redrawn owes: two drawing calls against a repaint.
; -----------------------------------------------------------------------------
wm_draw_shadow:
    mov ax, [di+W_X]            ; right edge: x+w, y+1..y+h
    add ax, [di+W_W]
    mov bx, [di+W_Y]
    inc bx
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    call gfx_vline
    mov bx, di                  ; bottom edge: y+h, x+1..x+w - and x..x+w with
    call wm_flush               ; no left border to sit under, or the corner
    mov ax, [di+W_X]            ; pixel is left unpainted (SPEC.md 11.95.2)
    jc .bot
    inc ax
.bot:
    mov bx, [di+W_X]
    add bx, [di+W_W]
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    call gfx_hline
    ret

wm_draw_win:
    KFZ 24                      ; one window's frame and content
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx                  ; DI = window ptr for the duration
%ifndef REDRAWFULL
    mov byte [wm_su_bok], 0     ; no band has been put back in THIS draw yet
    mov byte [wm_dw_part], 0    ; IS THIS DRAW PARTIAL? (SPEC.md 11.96.9) Read
    cmp word [wm_su_son], 0     ; HERE, because wm_su_srect spends the one-shot
    je .whole                   ; inside wm_su_try below and wm_su_bank needs to
    mov byte [wm_dw_part], 1    ; know about it afterwards
.whole:
%endif

    test word [di+W_FLAGS], WF_FULL
    jz .chrome                  ; fullscreen: no chrome at all - the whole
    mov byte [gfx_color], CWHITE; frame is content, white-filled, then
    mov ax, [di+W_X]            ; W_PAINT (SPEC.md 11.2)
    mov bx, [di+W_Y]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, bx
    add dx, [di+W_H]
    dec dx
    call gfx_fill
    jmp .paint

.chrome:
%ifndef REDRAWFULL
    call wm_chrome_clip         ; THE OUTLINE AND THE DROP SHADOW, and not where
%endif                          ; something above will cover them a moment from
                                ; now (SPEC.md 11.97). Both are gfx_fill, which
                                ; clips per PIXEL; the title bar below is not,
                                ; and 11.97.1 is why the region stops short of it
    call thm_ink                
    mov ax, [di+W_X]            ; 1px outline around the whole frame
    mov bx, [di+W_Y]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, bx
    add dx, [di+W_H]
    dec dx
    push bx                     ; ...or TWO SIDES of one, on a window that
    mov bx, di                  ; spans the screen and so has neither side
    call wm_flush               ; border (SPEC.md 11.95.3). push/pop write no
    pop bx                      ; flags, so the answer survives to the branch
    jc .frame2
    call gfx_frame
    jmp short .shadow
.frame2:
    ; gfx_frame is four gfx_fills - two hlines and two vlines - so leaving the
    ; SIDE ones out is drawing CALLS cheaper, not dearer (PERFORMANCE.md Part
    ; 2's ~756us of fixed cost per call, whatever it draws). A border may NOT
    ; be drawn and then covered by the fills below: every pixel of that column
    ; would be written twice, which is Part 1's double-draw flash and is
    ; plainly visible on the target machine.
    push si
    push di
    mov si, bx                  ; SI = y1, DI = y2 (gfx_frame's own idiom)
    mov di, dx
    mov bx, cx                  ; gfx_hline wants BX = x2, DX = y
    mov dx, si
    call gfx_hline              ; top, the full frame width
    mov dx, di
    call gfx_hline              ; bottom
%ifdef NOFLUSHR
    mov ax, cx                  ; ...and the RIGHT vline, which 11.95.3 drops
    mov bx, si                  ; along with the left one. This is its A/B, and
    mov dx, di                  ; the only place the two halves are drawn
    call gfx_vline              ; rather than measured
%endif
    pop di
    pop si
.shadow:
    call wm_draw_shadow         ; the L, and it is a routine of its own because
                                ; a window can owe its SHADOW and nothing else
                                ; (SPEC.md 11.91.4)

%ifndef REDRAWFULL
    mov word [wm_clip_n], 0     ; ...AND THE REGION DIES BEFORE THE TITLE BAR,
%endif                          ; not after it (SPEC.md 11.97.1). font_char
                                ; clips per whole CELL, so a glyph the region
                                ; cuts is dropped ENTIRELY - including the rows
                                ; of it this window can still see. Measured: a
                                ; window whose title bar was covered from its
                                ; 10th row down lost its caption's top 5 rows
                                ; as well. 11.3's granularity rule, in the one
                                ; place the plan for this said it could not bite
    call wm_draw_title          ; the title bar, active or not (BP decides)

    call wm_su_try              ; THE RAISE CACHE (SPEC.md 11.96): if the
    jc .content                 ; content was banked when this window was
                                ; covered, it is already back on screen - so
                                ; neither the white fill nor W_PAINT runs, and
                                ; the 578 ms of glyphs they cost is not spent.
                                ; The chrome above is drawn either way: it is
                                ; cheap, and a raise changes the pinstripes
%ifndef REDRAWFULL
    call wm_su_bget             ; ...UNLESS THE CACHE IS ONLY THE BANDS OF IT
    jnc .paint                  ; (SPEC.md 11.96.11), in which case what came
%endif                          ; back is the furniture and the app still owes
                                ; the rest - wm_damage hands it exactly that,
    jmp .growbox                ; and the white fill is skipped because a band
                                ; needs WF_OWNBG to have been granted at all
.content:
    test word [di+W_FLAGS], WF_OWNBG
    jnz .paint                  ; IT OWNS ITS BACKGROUND (SPEC.md 11.90.1): the
                                ; fill would be the first layer of a double
                                ; draw, and on a window whose W_PAINT takes
                                ; seconds it is that long as a white hole
    mov byte [gfx_color], CWHITE
    mov bx, di                  ; content area, filled white before W_PAINT -
    call wm_bordr               ; both edge columns where there are no side
    mov cx, [di+W_X]            ; borders (SPEC.md 11.95.3), or the fill is a
    add cx, [di+W_W]            ; column short of what the app may draw in
    dec cx
    sub cx, ax
    call wm_bord
    add ax, [di+W_X]
    mov bx, [di+W_Y]
    add bx, TITLE_H
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    sub dx, 2
    call gfx_fill

.paint:
    cmp word [di+W_PAINT], 0
    je .growbox
%ifndef REDRAWFULL
    call wm_dw_cull             ; SPEC.md 11.3.3: inside wm_dmg_wins, W_PAINT
%endif                          ; is confined to what nothing above will cover
    mov bx, di                  ; the paint is the owning instance's CPU
    call inst_win_owner         ; time, not the painting task's (SPEC.md 8.1)
    push di
    push word [snd_inst]        ; stamp the dispatched instance for sound
    call snd_disp_set           ; grants made inside the paint (SPEC 34.3)
    call task_cycles            ; DX:AX = start stamp
    push dx
    push ax
    mov si, bx                  ; SI = window ptr for the paint proc
    push bp
    mov bp, [bx+W_PAINT]
    call wm_pkgcall             ; near for a kernel window, far into the
    pop bp                      ; package's dispatcher otherwise (SPEC 20.1)
    mov bx, si                  ; (wm_pkgcall preserves both, but the paint
                                ; proc need not preserve BX)
    pop ax                      ; start stamp
    pop dx
    pop word [snd_inst]         ; the outer dispatch's stamp, back
    pop di                      ; owning instance record, 0 = unowned window
    or di, di
    jz .uncharged
    call inst_charge
.uncharged:
    mov di, bx                  ; DI = window ptr again (BX survived the
                                ; paint proc - SPEC.md 1 register discipline)
%ifndef REDRAWFULL
    mov byte [wm_dmg_cull], 0   ; ...and the cull dies with the paint it was
    mov word [wm_clip_n], 0     ; armed for: the grow box below and wm_su_bank
%endif                          ; are the kernel's own and clip like anything
                                ; else (SPEC.md 11.3.3)
.growbox:
    mov bx, di                  ; grow box AFTER W_PAINT (SPEC.md 11.1) -
    call wm_grow_paint          ; a no-op unless frontmost + WF_SIZABLE
    call wm_su_bank             ; ...and THIS is the moment its pixels are
                                ; both correct and finished with (SPEC.md
                                ; 11.96.4)
%ifndef REDRAWFULL
    mov word [wm_su_son], 0     ; THE ONE-SHOT DIES WITH THE DRAW IT WAS ARMED
                                ; FOR, at the one site every window in the system
                                ; is drawn through. wm_su_srect spends it, but
                                ; only when there is a cache to restore from: on
                                ; a MISS it is read by wm_damage and left set,
                                ; and would then be inherited by the next
                                ; wm_draw_win from anywhere - wm_paint_all, a
                                ; wm_show - as somebody else's rect. wm_dmg_wins
                                ; still clears it at .wdone for the case that
                                ; does not reach here at all, a window wm_covered
                                ; skipped (SPEC.md 11.96.6/11.96.10)
%endif
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_su_bank - after drawing window DI, decide what its raise cache should be
; in:  DI = the window just drawn (gfx lock held)
; out: nothing (all registers preserved)
;
; SPEC.md 11.96.4. The cache used to be taken in ONE place - wm_raise, from
; the window losing the front - and spent on the first restore, so it served
; the case it was written for (two windows alternating) and nothing else: a
; second drag across the same window repainted it, and a window that had been
; in the background a while had no cache at all.
;
; The question is the same wherever it is asked: has this window finished
; with the pixels it has just been drawn? The FRONT window has not - the next
; thing it does is change them - and every other window has, because nothing
; but a redraw can move them (11.96.1's promise). So the front window's cache
; is dropped and everybody else's is taken, at the one place every window in
; the system is drawn.
;
; It is affordable because a bank is a COPY and a repaint is a W_PAINT: about
; 5.7KB of framebuffer on a mono Disk window against 578 ms of glyphs
; (11.96). The claim is purgeable (50.6), so a machine with no room to spare
; declines and behaves exactly as it did before this existed.
; -----------------------------------------------------------------------------
wm_su_bank:
    push ax
    push bx
%ifdef REDRAWFULL
    jmp short .front            ; the reference build (SPEC.md 11.96.4): only
%endif                          ; wm_raise ever banks, and the first restore
                                ; spends it, which is where this started
    call wm_top                 ; BX = frontmost visible (0 if none)
    cmp bx, di
    je .front
    cmp byte [wm_dw_part], 0    ; A PARTIAL DRAW MAY NOT RE-BANK (SPEC.md
    je .take                    ; 11.96.9): the pass repaired part of this
                                ; window, so what it drew is no evidence about
                                ; the rest
    call wm_su_ck               ; ...but the cache it already has IS. A partial
    jnc .out                    ; draw changed nothing, so an existing one is
                                ; still exactly right and re-taking it is a
                                ; blit for nothing
    mov bx, di                  ; ...AND WITH NO CACHE AT ALL, ASK THE SCREEN
    call wm_obscured            ; (SPEC.md 11.96.9.1). "Was this draw partial"
    jc .out                     ; is a proxy for "is some of this window's
                                ; content somebody else's pixels", and it is
                                ; the wrong question once the window is out
                                ; from under whatever covered it: a drag that
                                ; uncovers a window repairs only the vacated
                                ; rect, so the draw is partial while the whole
                                ; content is now on the glass and correct.
                                ; wm_obscured asks that directly, and it
                                ; refuses 11.96.7's hazard too - a window
                                ; drawn earlier in this same pass with a
                                ; marked window above it still to come is
                                ; covered by construction
.take:
    call wm_su_take             ; covered, or minimized a moment from now:
    jmp short .out              ; its pixels stand until something draws it
.front:
    mov bx, di
    call wm_su_drop             ; the front window is about to change them
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_su_precover - bank what a window ABOUT TO APPEAR is going to cover
; in:  DI = the incoming window. Its rect is final, its visible bit is still
;      CLEAR and it has no pixels on the glass; caller holds the gfx lock
; out: nothing (all registers preserved)
;
; SPEC.md 11.96.16. wm_su_bank (11.96.4) takes a cache at the one place every
; window is DRAWN, which covers every window that has been drawn since it
; last lost its cache - and a window whose cache was dropped and which nobody
; has drawn since has none at all. wm_clip_set drops one on every background
; painter's frame (11.96) and fmv_store on every re-list (22.14), so that
; state is ordinary rather than exotic; what makes it expensive is a NEW
; window landing on such a window, because from that moment its content is
; somebody else's pixels and there is no longer anything to bank.
;
; wm_raise already banks ONE window for this reason - the outgoing front,
; which by definition has no cache (11.96.4 drops the front window's) - and
; that is the whole of what the case was ever given. Everything below it in
; the z-order got nothing.
;
; So the question is asked of every window the incoming rect lands on, and
; the moment is the last one at which it can be asked: BEFORE the visible bit
; goes on. That is not tidiness. wm_obscured is the test for "is any of this
; window's content already somebody else's pixels", 11.96.7's hazard, and the
; incoming window is in wm_zord from wm_create - so once it is visible it is
; the answer to that question about every window underneath it, and the pass
; would refuse them all. Running first is what makes the one window whose
; pixels are NOT on the glass invisible to the test.
;
; Two windows are skipped for reasons of their own. OURSELVES, and the
; frontmost window, which wm_raise banks itself a few instructions later -
; and must, because wm_draw_title and wm_grow_erase change its pixels first
; (11.1.1) and a cache taken here would hold a grow box it is about to lose.
; Re-taking it there is a drop and a ~10 ms blit for a picture we would have
; had to throw away anyway.
; -----------------------------------------------------------------------------
wm_su_precover:
    KFZ 18                      ; banking what the incoming window covers (11.96.16.2)
%ifndef REDRAWFULL
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    call wm_top                 ; BP = the frontmost visible window, or 0
    mov bp, bx
    mov si, wm_zord
    mov cl, [wm_zn]
    xor ch, ch
    jcxz .out
.scan:
    mov al, [si]
    inc si
    push cx
    push si
    call wm_idx2ptr             ; BX = the record this z-order slot names
    cmp bx, di
    je .next                    ; ourselves: we are what is doing the covering
    cmp bx, bp
    je .next                    ; wm_raise's own, above
    test word [bx+W_FLAGS], 2
    jz .next                    ; hidden: no pixels on the glass to bank
    test word [bx+W_FLAGS], WF_SAVEU
    jz .next                    ; made no promise - wm_su_take would refuse,
                                ; and asking here keeps the walk cheap on a
                                ; desktop full of windows that cannot use one
    push di                     ; ...AGAINST THE RECT WE ARE ABOUT TO HAVE,
    cmp word [wm_su_pcrp], 0    ; which for a RESIZE is not the one in our
    je .ownr                    ; record any more (SPEC.md 11.96.16.2). A
    mov di, [wm_su_pcrp]        ; window appearing has no such distinction and
.ownr:                          ; leaves this 0
    call wm_win_over            ; DI = us: do we land on it at all? A window
    pop di                      ; we do not touch keeps whatever it has and
    jc .next                    ; owes us no blit
    call wm_obscured            ; ...and is its content still its own? (11.96.7)
    jc .next                    ; Something already drawn over it would be
                                ; banked as this window's content
    push di
    mov di, bx
    call wm_su_ck               ; a cache it already has is still exactly right
    jnc .have                   ; - nothing has drawn since - so re-taking it
    call wm_su_take             ; is a blit for a picture we hold already
.have:
    pop di
.next:
    pop si
    pop cx
    loop .scan
.out:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    pop ax
%endif
    ret

; -----------------------------------------------------------------------------
; wm_grow_rect - DI = window -> AX,BX,CX,DX = its grow box, inclusive
; (W_X+W_W-14, W_Y+W_H-14) .. (W_X+W_W-2, W_Y+W_H-2)
; -----------------------------------------------------------------------------
wm_grow_rect:
    mov ax, [di+W_X]
    add ax, [di+W_W]
    sub ax, 14
    mov bx, [di+W_Y]
    add bx, [di+W_H]
    sub bx, 14
    mov cx, ax
    add cx, 12
    mov dx, bx
    add dx, 12
    ret

; -----------------------------------------------------------------------------
; wm_grow_erase - take the grow box off a window that has stopped being front
; in:  DI = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; SPEC.md 11.1.1. The grow box belongs to the frontmost window alone, exactly
; like the close and minimize boxes wm_draw_title takes away when a window
; loses the front - and nothing took it off. It survived only until something
; repainted that window, which used to be nearly every damage pass; SPEC.md
; 11.91.2 makes "nothing repaints it" the normal case and turns a transient
; artifact into a permanent one.
;
; White is the exact erase and not an approximation: wm_grow_paint's first act
; is to white-fill this same 13x13, so whatever the application had drawn
; there was already gone the moment the box appeared. This puts the window
; back to how it looked before the box, not to something new.
; -----------------------------------------------------------------------------
wm_grow_erase:
    test word [di+W_FLAGS], WF_SIZABLE
    jz .out
    test word [di+W_FLAGS], WF_FULL
    jnz .out                    ; wm_grow_paint's own two gates, and the
    test word [di+W_FLAGS], 2   ; visible bit: a hidden window has no pixels
    jz .out
    push ax
    push bx
    push cx
    push dx
    call wm_grow_rect
    call thm_bg                
    call gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; wm_grow_paint - draw a window's grow box if it deserves one (SPEC.md 11.1)
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Draws only when BX is the frontmost visible window with WF_SIZABLE set
; and WF_FULL clear - a no-op otherwise, so it is always safe to call.
; wm_draw_win calls it after W_PAINT, and a resizable window's
; self-initiated content repaint must end with it: the white-fill idiom
; (SPEC.md 22) erases the corner, and wm_hit reports AL=4 from geometry
; alone, so the box must never be left invisible-but-clickable.
; -----------------------------------------------------------------------------
wm_grow_paint:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, bx
    call wm_top                 ; BX = frontmost visible (0 if none)
    cmp di, bx
    jne .out
    mov ax, [di+W_FLAGS]
    and ax, WF_SIZABLE | WF_FULL
    cmp ax, WF_SIZABLE
    jne .out
    call wm_grow_rect           ; 13x13 white square, black frame
    call thm_bg                
    call gfx_fill
    call thm_ink                
    call gfx_frame
    add ax, 4                   ; the big square of the glyph: 8x8 frame
    add bx, 4                   ; at inset (+4,+4)..(+11,+11)
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call gfx_frame
    sub ax, 2                   ; the small square: 7x7 at inset (+2,+2)..
    sub bx, 2                   ; (+8,+8), white-filled (it overlaps the
    mov cx, ax                  ; big one's top-left), black-framed
    add cx, 6
    mov dx, bx
    add dx, 6
    call thm_bg                
    call gfx_fill
    call thm_ink                
    call gfx_frame
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret


%ifdef WM_ANIM
; =============================================================================
; The zoom outline (SPEC.md 11.99) - kern_big only
; =============================================================================
; A window that appears, goes away, or changes size shows an outline walking
; between where it was and where it is going. It is ui_drag's overlay (SPEC.md
; 13) doing a second job, and it needed no new primitive: vga_xor_rect_vram is
; self-inverting, so there is no save-under and no claim that can be refused;
; it clips to the screen, so an outline reaching off the edge is free rather
; than illegal; it bypasses the clip region by contract (11.3); it owes its own
; cur_unlazy (7.1.4); and gfx_xor_rect_d puts GFXDENTER above the [vid_mono]
; dispatch (39.14.6), so one call covers all three adapters.
;
; **THE TICK IS THE FRAME CLOCK, AND THAT IS MEASURED.** tests/gfxbench on a
; cycle-accurate 4.77MHz 8088 with a real CGA prices a vertical scan line at
; 27.8 us and a whole outline at ~1,760 + 56(h-2) us, so the three intermediate
; outlines of one animation, drawn AND erased, are 53 ms on CGA and 60 on
; Hercules - one to two ticks for the lot. The pacing is therefore the bulk of
; the wall time on every machine and not a cap for fast ones, which is the
; opposite of what PERFORMANCE.md Part 2's fill rows imply: read off those, a
; 1-pixel-wide column brackets between 46 and 182 us a scan line and the
; drawing would have been the clock. None of them is narrower than 8 pixels.
;
; WM_ANIM_N frames at a tick each is the duration, and only N-1 outlines are
; drawn: step 0 and step N are the window itself, before and after, and drawing
; those would be PERFORMANCE.md Part 1's double-draw flash with the animation
; as the flash.
;
; **kern_big only.** kern_small is the 128KB machine's kernel and stands at one
; 512-byte step of KERN_BUDGET; this is quality-of-life and does not get to
; spend it. Every entry point below is inside this %ifdef and every call site
; guards its own call, so the small build assembles with none of it.
; -----------------------------------------------------------------------------
WM_ANIM_N   equ 6                   ; frames; N-1 outlines are drawn
WM_AN_FRAME equ 32768               ; one frame, in PIT channel-0 counts: HALF
                                    ; a tick, 27.5 ms. The whole animation is
                                    ; ~137 ms of pacing (SPEC.md 11.99.4) - it
                                    ; was a whole tick a frame and a third of a
                                    ; second, and that reads as slow
WM_AN_SEED  equ 8                   ; half-size of the collapsed end (11.99.1)

; wm_an_ok - may this window animate? (module internal)
; in:  BX = the window
; out: CF = 1 yes; every register preserved
wm_an_ok:
    cmp byte [wm_animon], 0     ; THE USER'S CHOICE (SPEC.md 76.11), and it is
    je .no                      ; asked HERE because this is the one gate every
                                ; call site already asks - a per-site test is
                                ; a second opinion about whether the feature is
                                ; on, and 11.99's contract is that the screen
                                ; comes back exactly as it was found, which a
                                ; site that animates while another does not
                                ; cannot promise
    test word [bx+W_FLAGS], WF_NOANIM
    jnz .no                     ; the modal dialog and the notice window, which
                                ; come and go too often to be worth a third of
                                ; a second each (11.99.2)
    call wm_fs_vis              ; a fullscreen window owns the screen: an
    jc .no                      ; outline over it is drawing on somebody's
    stc                         ; borrowed surface
    ret
.no:
    clc
    ret

; wm_anim - walk an outline from wm_an_s to wm_an_d
; in:  both rects filled, inclusive; the caller holds the gfx lock
; out: nothing; every register preserved, and the screen exactly as found -
;      every outline drawn is XORed off again before this returns
wm_anim:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [wm_an_i], 1
.frame:
    call wm_an_deadline         ; ...sampled BEFORE the draw, so the drawing is
                                ; absorbed by the wait and not added to it
    call wm_an_lerp
    call wm_an_xor              ; on
    call wm_an_pace             ; ...held for a frame...
    call wm_an_xor              ; ...and off again: XOR is its own inverse, so
                                ; nothing underneath it was ever lost
    inc word [wm_an_i]
    cmp word [wm_an_i], WM_ANIM_N
    jb .frame
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wm_an_lerp - wm_an_c = s + (d - s) * i / N, four coordinates at a time
; N is not a power of two, so this is a real signed divide - and it cannot
; overflow: |d - s| <= 720 and i < N, so the quotient is at most 600.
wm_an_lerp:
    mov si, wm_an_s
    mov di, wm_an_c
    mov cx, 4
.c:
    mov ax, [si+8]              ; the destination coordinate...
    sub ax, [si]                ; ...as a SIGNED delta from the source
    imul word [wm_an_i]         ; DX:AX
    idiv word [wm_an_n]         ; AX
    add ax, [si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .c
    ret

; wm_an_xor - the outline, drawn or erased (one call does both)
wm_an_xor:
    mov ax, [wm_an_c]
    mov bx, [wm_an_c+2]
    mov cx, [wm_an_c+4]
    mov dx, [wm_an_c+6]
    jmp vga_xor_rect_vram

; wm_an_deadline - bank "now + WM_AN_FRAME" as this frame's deadline
; out: nothing; every register preserved
;
; Sampled at the TOP of the frame, before the outline is drawn, so the drawing
; is ABSORBED by the wait rather than added to it - which is what a tick wait
; did for free and what a naive "wait this long after drawing" would lose.
wm_an_deadline:
    push ax
    push dx
    call wm_an_pit
    add ax, WM_AN_FRAME
    adc dx, 0
    mov [wm_an_dl], ax
    mov [wm_an_dl+2], dx
    pop dx
    pop ax
    ret

; wm_an_pace - hold this frame until its deadline.
; ui_drag's .linger in shape and for its reason: the lock is HELD across the
; wait, because an XOR overlay is on the screen and a background painter
; drawing under it would leave the erase inverting pixels it never drew.
; task_yield inside a lock hold is what ui_drag already does there, and it is
; what keeps the sound worker and the scheduler alive through this.
;
; THE COMPARE IS `js`, NOT `jb` - two free-running counters, so the SIGN of
; the difference decides (SPEC.md 45.15's rule, and sch_isr's wake scan is the
; same idiom). A magnitude compare reads a wrapped clock as "hours to go".
wm_an_pace:
    push ax
    push dx
.w:
    call task_yield
    call wm_an_pit
    sub ax, [wm_an_dl]
    sbb dx, [wm_an_dl+2]
    js .w
    pop dx
    pop ax
    ret

; wm_an_pit - the scheduler's clock, read and not written (SPEC.md 34.1)
; out: DX:AX = [ticks]:phase, phase counting UP from 0 at the tick
; clobbers: AX, DX, flags
;
; sch_account's own latch, minus the accounting: channel 0 is NEVER written
; here, only latched, which 34.1 names as the non-destructive operation the
; speaker PWM pacer already performs. **The latch+read triple runs inside one
; pushf/cli ... popf window**, which is 34.1's binding atomicity rule: the
; 8254 ignores a second latch while a latched value is unread, so an
; interleave would feed one reader a stale byte.
;
; It does NOT re-derive sch_account's IRQ-pending coherence test, and does not
; need to: a deadline half a tick away is compared with `js`, so a DX that is
; one behind at the instant of a reload costs at most one extra pass round the
; loop and never a wrong answer. This clock is a pace, not an accounting.
;
; A fullscreen bracket divides this period (53.2.1's FSXF_FASTTICK), and the
; animation cannot run inside one - the UI task does not run there at all
; (53.1) - so no gate on [sch_fast] is needed or wanted.
wm_an_pit:
    push cx
    pushf
    cli
    mov al, 0x00                ; latch channel 0's count
    out 0x43, al
    in al, 0x40
    mov cl, al
    in al, 0x40
    mov ch, al
    mov dx, [ticks]
    popf
    neg cx                      ; phase = 65536 - count, counting up from the
    mov ax, cx                  ; tick
    pop cx
    ret

; wm_an_setw - fill the rect at DI from window SI
; clobbers: flags
wm_an_setw:
    push ax
    push bx
    push cx
    push dx
    call wm_win_rect            ; AX/BX/CX/DX = the frame rect, shadow included
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov [di+6], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wm_an_seed - collapse the rect at SI onto the CENTRE of the one at DI
; (SPEC.md 11.99.1). There is no universal source rect for a window appearing:
; a Mac zooms out of the icon that was double-clicked, and here a launch
; arrives from a menu item, a drive zone, a Disk window row or a package's own
; wm_create, none of which reaches wm_show. The window's own middle is the
; honest general answer and is what a system with no icon to hand does.
; clobbers: flags
wm_an_seed:
    push ax
    push bx
    mov ax, [di]                ; cx = (x1 + x2) / 2
    add ax, [di+4]
    sar ax, 1
    mov bx, ax
    sub ax, WM_AN_SEED
    mov [si], ax
    add bx, WM_AN_SEED
    mov [si+4], bx
    mov ax, [di+2]              ; cy = (y1 + y2) / 2
    add ax, [di+6]
    sar ax, 1
    mov bx, ax
    sub ax, WM_AN_SEED
    mov [si+2], ax
    add bx, WM_AN_SEED
    mov [si+6], bx
    pop bx
    pop ax
    ret

; wm_an_arm - the NEXT wm_show zooms out of this rect instead of a centre
; in:  AX/BX/CX/DX = the rect, inclusive
; out: nothing; every register preserved
;
; ONE-SHOT, and that is the whole of how "restored from the dock" reaches
; wm_show. It cannot be derived there: all four un-minimize paths clear
; I_FLAGS bit 0 themselves before they call wm_show (SPEC.md 11.99.3), so by
; the time the window is being shown the one fact that says where it is coming
; from has already been thrown away.
wm_an_arm:
    mov [wm_an_s], ax
    mov [wm_an_s+2], bx
    mov [wm_an_s+4], cx
    mov [wm_an_s+6], dx
    mov byte [wm_an_armed], 1
    ret

; wm_an_open - a window arriving (module internal; wm_show's, lock held)
; in:  BX = the window, its rect already final
wm_an_open:
    call wm_an_ok
    jnc .drop
    push si
    push di
    mov si, bx
    mov di, wm_an_d
    call wm_an_setw             ; destination = where it is about to appear
    cmp byte [wm_an_armed], 0
    jne .armed                  ; ...source already set by wm_an_arm
    mov di, wm_an_d
    mov si, wm_an_s
    call wm_an_seed             ; ...or its own centre
.armed:
    mov byte [wm_an_armed], 0
    call wm_anim
    pop di
    pop si
    ret
.drop:
    mov byte [wm_an_armed], 0   ; a refused animation still spends the arm, or
    ret                         ; the next window inherits somebody else's tile

wm_an_n     dw WM_ANIM_N        ; idiv takes a memory or register operand, not
                                ; an immediate - and .text, not .bss, because
                                ; it has a value
%endif

; --- SPEC.md 76.11: does the user want the zoom? ----------------------------
; OUTSIDE the %ifdef above, deliberately. The settings map names this byte in
; EVERY build, and a kernel with no animation in it still has to carry the
; choice across SYSTEM.CFG untouched rather than dropping it - a user who
; turns the zoom off, boots kern_small once and comes back should not find it
; back on. It is .text with a real initialiser because .bss is not zeroed
; (-f bin), and 1 is the default because the animation is what elendilon
; shipped.
wm_animon   db 1                ; 0 = Off, 1 = On

; -----------------------------------------------------------------------------
; wm.inc .bss - window table + z-order (wm_init zeroes all of it as one run,
; so these three must stay contiguous and in this order)
; -----------------------------------------------------------------------------
section .bss
wm_wins:    resb MAX_WIN * WIN_SIZE ; 12 windows x WIN_SIZE bytes
wm_zord:    resb MAX_WIN            ; window indices, [0] = backmost
wm_zn:      resb 1                  ; entries in wm_zord
%ifdef BANDCOMP
; SPEC.md 11.101: what wm_title_band resolves in .text for wm_title_band_x to
; read in .cold. A parameter BLOCK rather than registers because the body
; needs six words and the 8086 has not got six to spare with DI and BP already
; carrying the two windows.
wmt_lo:     resw 1                  ; the bar's own left and right edges...
wmt_hi:     resw 1
wmt_x0:     resw 1                  ; ...and the byte-aligned span inside them,
wmt_x1:     resw 1                  ;    which is all gfx_blit1 will take
wmt_cx:     resw 1                  ; the chunk cursor, walking x0 -> x1
wmt_pair:   resw 1                  ; AL = the bar's ink, AH = its paper
wmt_tx:     resw 1                  ; the caption's pen...
wmt_tw:     resw 1                  ; ...and its width in pixels
%endif
%ifdef KERN_BIG
wm_tarm:    resb 1                  ; SPEC.md 13.9: MAY any window have a timer
                                    ; armed? Set by every wm_timer arm, cleared
                                    ; by ui_timer_pass's walk when it finds
                                    ; none. STALE is one wasted walk; MISSING
                                    ; would be a timer that never fires, and
                                    ; only arming sets it, so the unsafe
                                    ; direction is unreachable. It is what
                                    ; keeps the cost at one compare per UI pass
                                    ; on a machine with nothing armed
%endif
wm_rz_ox:   resw 1              ; where a window was, banked across ONE
wm_rz_oy:   resw 1              ; geometry change so the repaint after it can
wm_rz_ow:   resw 1              ; ask what actually moved (SPEC.md 11.95.1).
wm_rz_oh:   resw 1              ; Not per-slot: it never outlives the call.
                                ; LAID OUT LIKE A RECORD'S X/Y/W/H, so
                                ; wm_rz_swap can exchange the two four words
                                ; at a time and wm_win_over can be handed
                                ; either as a DI (SPEC.md 11.96.16.2)
%if wm_rz_oy - wm_rz_ox != 2 || wm_rz_ow - wm_rz_ox != 4 || wm_rz_oh - wm_rz_ox != 6
  %error "wm_rz_o* must stay four adjacent words: wm_rz_swap walks them"
%endif
%if W_Y - W_X != 2 || W_W - W_X != 4 || W_H - W_X != 6
  %error "W_X..W_H must stay four adjacent words: wm_rz_swap walks them"
%endif
wm_zoomr:   resb MAX_WIN * ZR_SIZE  ; per slot: the USER state a zoomed window
                                    ; goes back to (SPEC.md 11.95). A side
                                    ; table, not four more words in the record,
                                    ; because WIN_SIZE is the stride every
                                    ; reader of a window pays
wm_natr:    resb MAX_WIN * NR_SIZE  ; per slot: the rect a window ASKED for, so
                                    ; an adapter change can put back what its
                                    ; clamp took (SPEC.md 39.11.2.1). Same
                                    ; stride and the same argument as the zoom
                                    ; bank above, and OUTSIDE wm_init's zero
                                    ; run like it - nothing reads a slot before
                                    ; wm_create has written one (see the header)
wm_pref:    resw MAX_WIN            ; per slot: the OFFSET, in the window's own
                                    ; segment, of its 12-byte per-adapter size
                                    ; table; 0 = no preference (SPEC.md
                                    ; 11.100.1). The offset and not a copy - 12
                                    ; bytes a slot against 2 - and read through
                                    ; W_SEG at use time exactly as W_TITLE is.
                                    ; Written by wm_create for every slot it
                                    ; takes, which is what makes a reused slot
                                    ; safe without a second clear in wm_destroy
wm_usrsz:   resb MAX_WIN            ; per slot: THE USER HAS SIZED THIS WINDOW
                                    ; (SPEC.md 11.100.5) - set by ui_grow and
                                    ; wm_zoom, cleared by wm_create, and the
                                    ; one thing that stops a preference
                                    ; overriding a size somebody chose by hand
wm_minw:    resw MAX_WIN            ; per slot: the smallest frame this window
wm_minh:    resw MAX_WIN            ; will accept, 0 = the system floor
                                    ; (WMIN_W/WMIN_H, SPEC.md 11.100.2)
%ifdef KERN_BIG
wm_pkind:   resb MAX_WIN            ; per slot: the adapter kind the window was
                                    ; last SIZED for, so 11.100.4's adoption can
                                    ; tell a drag onto another card from a drag
                                    ; inside one display. KERN_BIG only - a
                                    ; machine with one display never moves
%endif
wm_about:   resw MAX_WIN            ; per-slot About handler offset, 0 = none
                                    ; (SPEC.md 12.2); inside wm_init's zero
                                    ; run, so a fresh slot has no About
wm_onsz:    resw MAX_WIN            ; per-slot size-CHANGED handler offset, 0 =
                                    ; none (SPEC.md 11.98). wm_about's shape
                                    ; exactly, for wm_about's reason: a handler
                                    ; is per SLOT and a wider record is paid for
                                    ; by every reader of a window
wm_onwk:    resw MAX_WIN            ; per-slot WAKE handler offset, 0 = none
                                    ; (SPEC.md 74.1); wm_onsz's shape and reason
wm_oncl:    resw MAX_WIN            ; per-slot CLOSE negotiator offset, 0 = none
                                    ; (SPEC.md 75.1); wm_onwk's shape and reason
                                    ; a fourth time
wm_onrc:    resw MAX_WIN            ; per-slot RIGHT-CLICK handler offset, 0 =
                                    ; none (SPEC.md 13.11); the same shape a
                                    ; fifth time, and inside wm_init's zero run
                                    ; below for wm_oncl's reason: ui_rdown reads
                                    ; this word for EVERY frontmost window a
                                    ; right press lands on, handler or not
wm_clreq:   resw 1                  ; the window a package asked to have closed
                                    ; (SPEC.md 75.2), 0 = none. ONE, because the
                                    ; request is spent on the next UI pass and
                                    ; nothing can queue a second inside one
wm_clask:   resw 1                  ; the window whose negotiator is running
                                    ; RIGHT NOW, 0 = none: the re-entry fence
wm_wkq:     resb MAX_WIN            ; per slot: WKQ_NONE / WKQ_RING (a record is
                                    ; in the ring) / WKQ_EATEN (a drain took
                                    ; the record; wm_wake_redo owes one back)
wm_wkdef:   resb 1                  ; ...1 = at least one slot is WKQ_EATEN, so
                                    ; ui_task's per-pass check is ONE compare
wm_wk_rec:  resb EVQ_RECSIZE        ; the one wake record, filled under cli
wm_owner:   resb MAX_WIN            ; instance index owning each window slot
                                    ; (SPEC.md 29), 0xFF = unowned; NOT part
                                    ; of wm_init's zero run (fill is 0xFF)
wm_fs:      resw 1                  ; fullscreen window ptr, 0 = none
                                    ; (SPEC.md 11.2); zeroed by wm_init
wm_fs_save: resw 4                  ; its windowed x/y/w/h, restored on exit

; --- wm_su_edge's scratch (SPEC.md 11.96.2) ----------------------------------
; Derived from the banked rect at every restore and never read before it is
; written, so unlike the five wm_su_* words above these want no initialiser.
wm_su_nrow: resw 1                  ; scratch bytes per column = restored rows
                                    ; x planes, which is what gfx_save wrote
wm_su_bpr:  resw 1                  ; bytes per buffer row (the BANKED rect's)
wm_su_scr:  resw 1                  ; offset of the two screen columns
wm_su_lm:   resb 1                  ; bits of the left edge byte the rect owns
wm_su_rm:   resb 1                  ; ...and of the right
; --- and what bounds it to the rect being restored (SPEC.md 11.96.8) ---------
wm_su_srow: resw 1                  ; rows of THIS restore, per plane
wm_su_pext: resw 1                  ; ...and the rows of the plane it does not
                                    ; cover, which the walk steps over
wm_su_rofs: resw 1                  ; where its first row sits in the buffer
wm_su_dol:  resw 1                  ; 0 = the restore reaches the banked rect's
wm_su_dor:  resw 1                  ; left / right edge byte column

; --- the clip region (SPEC.md 11.3) ------------------------------------------
; Uninitialized on purpose: wm_clip_n lives in .text as a dw 0 and is the
; only thing that says how much of this is meaningful, so nothing here needs
; a value before wm_clip_set writes one.
wm_clip_tab: resw 4 * WM_CLIP_MAX   ; 16 rects x {x1, y1, x2, y2}, inclusive
wm_cl_ox1:  resw 1                  ; wm_clip_sub: the occluder's rect, its
wm_cl_oy1:  resw 1                  ; drop shadow included
wm_cl_ox2:  resw 1
wm_cl_oy2:  resw 1
wm_cl_rx1:  resw 1                  ; wm_clip_split: the rect being replaced
wm_cl_ry1:  resw 1
wm_cl_rx2:  resw 1
wm_cl_ry2:  resw 1
%ifdef WM_ANIM
wm_an_s:    resw 4                  ; SPEC.md 11.99: where the zoom starts...
wm_an_d:    resw 4                  ; ...where it ends...
wm_an_c:    resw 4                  ; ...and the interpolated rect between
wm_an_i:    resw 1                  ; which frame
wm_an_dl:   resw 2                  ; ...and when it is due to end
wm_an_armed: resb 1                 ; 1 = wm_an_s is a caller's, not a centre
%endif
wm_cl_fx1:  resw 1                  ; wm_clip_add: the fragment to append
wm_cl_fy1:  resw 1
wm_cl_fx2:  resw 1
wm_cl_fy2:  resw 1
section .text
