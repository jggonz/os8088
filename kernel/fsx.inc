; =============================================================================
; fsx.inc - fullscreen exclusive (SPEC.md 53)
;
; The app borrows the machine: fsx_run is a BRACKET, not a latch - it is
; called from a window callback (UI task, gfx lock held) and does not return
; until the app's exclusive main does. While it runs, sch_switch passes only
; this task, its kept worker and TF_SERVICE workers (SPEC.md 53.2 - the
; whitelist lives in sched.inc, armed through [fsx_task]/[fsx_worker]); the
; held gfx lock keeps the mouse ISR off the screen; and the video mode is
; the app's to change through fsx_mode. The kernel never runs while the mode
; is foreign, because the kernel is parked on the stack underneath the app.
;
; The [vid_*] live block is never touched here: it keeps describing the
; DESKTOP mode, and the FSI info block is the app's only description of the
; foreign one. Restore is therefore vid_setmode + one wm_paint_all, and
; nothing kernel-side needs undoing.
; =============================================================================

; --- mode ids (SPEC.md 53.4; mirrored in apps/os88api.inc) -------------------
FSXM_TEXT80 equ 0               ; 80x25 text        VGA CGA HERC (vid_text)
FSXM_TEXT40 equ 1               ; 40x25 text        VGA CGA      (mode 01h)
FSXM_CGA320 equ 2               ; 320x200x4         VGA CGA      (mode 04h)
FSXM_CGA640 equ 3               ; 640x200x2         VGA CGA      (mode 06h)
FSXM_HERC   equ 4               ; 720x348 mono              HERC (vid_setmode)
FSXM_VGA0D  equ 5               ; 320x200x16 planar VGA          (mode 0Dh)
FSXM_VGA13  equ 6               ; 320x200x256       VGA          (mode 13h)
FSXM_VGA12  equ 7               ; 640x480x16        VGA          (mode 12h)
FSXM_MODEX  equ 8               ; 320x240x256       VGA  (13h + unchain/retime)
FSXM_COUNT  equ 9

FSXF_KEEPWORKER equ 1           ; fsx_run CX bit 0 (SPEC.md 53.2)
FSX_SUBTICK     equ 3           ; sub-ticks per tick when it is asked for
FSXF_FASTTICK   equ 2           ; ...bit 1 (SPEC.md 53.2.1): IRQ0 three times a
                                ; tick, so the round-robin quantum is 18 ms
                                ; instead of 55. [ticks] does not change rate

; --- fsx_wait's clock (SPEC.md 53.5; mirrored in apps/os88api.inc) -----------
FSXW_TICK   equ 0               ; the next [ticks] change, hlt between polls
FSXW_VSYNC  equ 1               ; vertical retrace, polled - SPINS (53.5.1)
FSXW_FRAME  equ 2               ; the next IRQ0: the sub-tick while FASTTICK is
                                ; armed, the tick otherwise - and it YIELDS,
                                ; so a kept worker gets the time (53.5.1)

; --- the info block fsx_mode fills (SPEC.md 53.4) ----------------------------
FSI_SEG    equ 0                ; word: framebuffer segment
FSI_W      equ 2                ; word: width  - pixels; COLUMNS in text
FSI_H      equ 4                ; word: height - pixels; ROWS in text
FSI_STRIDE equ 6                ; word: bytes per row (text: per char row)
FSI_FLAGS  equ 8                ; byte: FSIF_*
FSI_BPP    equ 9                ; byte: bits per pixel (text: 0)
FSI_BANKS  equ 10               ; byte: interleaved banks (1 = linear)
FSI_PAGES  equ 11               ; byte: display pages
FSI_BSTEP  equ 12               ; word: bank step bytes (0 when linear)
FSI_MODE   equ 14               ; byte: the FSXM_* id, echoed back
FSI_RSVD   equ 15               ; byte: 0
FSI_SIZE   equ 16

FSIF_TEXT   equ 1
FSIF_PLANAR equ 2
FSIF_BANKED equ 4

; -----------------------------------------------------------------------------
; fsx_caps - which FSXM ids a window's display can set (SPEC.md 53.8, 0x02C0)
;
; in:       BX = the window to ask ABOUT, or 0 for "whatever is frontmost"
; out:      AX = bitmask (bit n = FSXM id n settable), DL = the answering
;           display's VID_* kind; CF=0
; clobbers: nothing else. Any context, lock held or not (the osapi_video
;           precedent) - an app greys its mode menu with this per SPEC.md 47,
;           and fsx_mode refuses with the same bit, one predicate for both.
;
; IT TAKES THE WINDOW BECAUSE IT USED TO GUESS ONE (SPEC.md 39.18.2). The
; guess was wm_top, on the reasoning that an app greying its own mode menu is
; frontmost by construction - true when the menu is used and false when the
; question is asked, because a package's entry proc runs BEFORE its window
; exists and gets an answer about the window it was launched from. Missile
; Command launched off a Disk window on the Hercules read HERC's caps and
; greyed Mode X on a machine with a VGA in it.
; -----------------------------------------------------------------------------
fsx_caps:
    push bx
    mov dl, [vid_kind]
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1     ; ...of the display the ASKED-ABOUT window is
    jbe .kind                   ; on (SPEC.md 39.18.1). ONE DISPLAY covers a
                                ; mode-setting bracket too, which is what
                                ; collapsed the machine (39.18.3) and moved
                                ; [vid_kind] to the bracket's own card - so
                                ; there is no second test for that, and a
                                ; SAME-MODE bracket, which does not collapse,
                                ; correctly falls through and answers about
                                ; the window it was handed
    push ax
    or bx, bx                   ; BX = 0: no window to ask about - an entry
    jnz .win                    ; proc, or a caller that has none. Fall back
    call wm_top                 ; to the frontmost, and to the primary below
    or bx, bx                   ; if nothing is visible at all
    jz .notop
.win:
    push di
    call wm_disp_of             ; the CENTRE, then the origin, then the
    call vid_ctx_ptr            ; primary (SPEC.md 39.17) - so a straddling
    mov dl, [di+VID_CTX_KIND]   ; window belongs to the display holding more
    pop di                      ; of it, which is the same rule wm_fullscreen
.notop:                         ; and wm_zoom ask
    pop ax
.kind:
%endif
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov ax, [bx+fsx_capstab]
    pop bx
    clc
    ret

fsx_capstab:                    ; indexed by VID_* (VGA 0, HERC 1, CGA 2)
    dw 0x01EF                   ; VGA:  0,1,2,3 + 5,6,7,8
    dw 0x0011                   ; HERC: 0 (mode 7 text) + 4 (its own graphics)
    dw 0x000F                   ; CGA:  0,1,2,3

; -----------------------------------------------------------------------------
; fsx_run - the bracket (SPEC.md 53.1, slot 0x02C8 via X stub)
;
; in:       AX = near entry inside the caller's image, BX = the caller's own
;           window ptr, CX = flags (bit 0 = FSXF_KEEPWORKER, bit 1 =
;           FSXF_FASTTICK; others refuse);
;           ES = the CALLER's DS (the X stub) - the ownership fence reads it.
;           UI-task window-callback context, gfx lock held.
; out:      CF=1 refused, nothing changed. CF=0: the app's proc ran to its
;           ret and the desktop is restored (SPEC.md 53.6). Every register
;           preserved either way.
;
; The fence is inst_pkg_spawn's, verbatim: BX must be a live package
; instance's window whose segment is the caller's own, and AX must lie
; inside its image+bss. The entry is called through wm_pkgcall - SI = the
; window ptr, DS = CS = the package's segment, ES = KERNEL_SEG - an ordinary
; near proc with a near ret, exactly a W_ONKEY's environment.
; -----------------------------------------------------------------------------
fsx_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test cx, ~(FSXF_KEEPWORKER | FSXF_FASTTICK)
                                ; undefined flag bits refuse, so the word
    jnz .fail                   ; stays extensible (SPEC.md 53.1)
    cmp byte [sch_cur], 0       ; the UI task alone may enter - a worker's
    jne .fail                   ; bracket would freeze the UI task mid-frame
    cmp byte [fsx_task], 0xFF   ; one bracket at a time
    jne .fail
    call cur_unlazy              ; the bracket owns the machine and the held
                                ; lock is what keeps the arrow off it - which
                                ; a DEFERRED hide would not (SPEC.md 7.1.4).
                                ; A same-mode bracket (SPEC.md 53.7) never
                                ; reaches vid_setmode, so this is the only
                                ; place that catches it, and the double cursor
                                ; SPEC.md 48.13 watches for is the symptom
    cmp word [fdlg_win], 0      ; not under the modal file dialog (SPEC.md 38)
    jne .fail
    call inst_wchk              ; the ownership fence (SPEC.md 53.1): bound
    jc .fail                    ; the package's BX before any deref...
    call inst_of_win            ; DI = the record owning the window
    jc .fail
    cmp byte [di+I_STATE], 1    ; ...live only...
    jne .fail
    test byte [di+I_KIND], KIND_PKG
    jz .fail
    mov dx, es                  ; ...a package whose segment is the caller's
    cmp dx, [di+I_SPTR]         ; own DS (identity, not containment)...
    jne .fail
    cmp ax, [di+I_SIZE]         ; ...and an entry inside its image+bss
    jae .fail

    mov dx, di                  ; DL = the caller's instance index - the
    sub dx, inst_tab            ; release walk below exempts it
    push cx
    mov cl, 5
    shr dx, cl
    pop cx
    mov dh, 0xFF                ; DH = the kept worker's slot, or none
    test cl, FSXF_KEEPWORKER
    jz .nokeep
    mov dh, [di+I_TASK]         ; 0xFF when the instance has no worker -
.nokeep:                        ; keeping nothing is not an error

    pushf                       ; arm the freeze (SPEC.md 53.2) in one IF=0
    cli                         ; window: both bytes or neither
    mov [fsx_worker], dh
    mov byte [fsx_task], 0      ; task 0 - checked above
    popf
    mov byte [fsx_cur], 0xFF    ; no foreign mode yet
    test cl, FSXF_FASTTICK      ; ...and the finer quantum, AFTER the arm so
    jz .nofast                  ; the task set it applies to is already the
    push ax                     ; frozen one (SPEC.md 53.2.1). AX is the app's
    mov al, FSX_SUBTICK         ; entry offset, still owed to wm_pkgcall far
    call sch_fast_on            ; below - the argument register here is AL
    pop ax
.nofast:

    ; silence what the freeze strands (SPEC.md 53.3) - AFTER the arm, so no
    ; foreign task can take a fresh grant between the two. snd_release_inst
    ; is the SPEC.md 29.4 teardown routine: DSV_RELINST first, then the
    ; clip, then the tone channel, then the generation bump that makes the
    ; thaw safe. The caller's own grants stay - it keeps running, which is
    ; what carries a FSXF_KEEPWORKER stream in.
    push ax
    mov si, inst_tab
    mov cx, INST_MAX
    xor al, al
.rel:
    cmp byte [si+I_STATE], 0
    je .relnext
    cmp al, dl
    je .relnext
    call snd_release_inst
.relnext:
    inc al
    add si, I_RECSZ
    loop .rel
    pop ax

%ifdef KERN_BIG
    push ax                     ; WHICH DISPLAY this bracket is on - banked,
    call wm_disp_of             ; not acted on (SPEC.md 39.18.3). BX is still
    mov [fsx_wdisp], al         ; the window; AX is the app's entry, owed
    pop ax                      ; below.
                                ;
                                ; THE SINGLE-DISPLAY COLLAPSE MOVED TO
                                ; fsx_mode, because it is a consequence of
                                ; SETTING A MODE and not of taking the
                                ; machine. A same-mode bracket (SPEC.md 53.7)
                                ; promises that "the drawing slots stay legal
                                ; and the geometry is still the desktop's",
                                ; and collapsing here broke exactly that
                                ; promise on the secondary display.
%endif

    push bp                     ; the app owns the machine from here to its
    mov si, bx                  ; ret: wm_pkgcall's contract, record in SI,
    mov bp, ax                  ; offset in BP (SPEC.md 53.1)
    call wm_pkgcall
    pop bp
    call fsx_restore            ; SPEC.md 53.6, ordered
    clc
    jmp short .out
.fail:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fsx_restore - one path home (SPEC.md 53.6, order binding)
;
; 1. the desktop's mode back (skipped when no fsx_mode call happened - the
;    screen is already the desktop's mode); the BIOS mode set also restores
;    the default palette and the standard planar write state; 2. drain the
;    BIOS key buffer and the event queue - keys typed and clicks queued at
;    the game must not land in the desktop ladder (the SPEC.md 34.4
;    click-abort drain is the precedent); 3. disarm the freeze; 4. repaint
;    the desktop under the still-held lock. The callback epilogue's
;    gfx_unlock then flushes (if buffered) and re-shows the cursor with a
;    fresh save-under from the restored VRAM - SPEC.md 7's own contract.
;
; Step 4 is a wm_paint_all on every machine and every CPU tier, and SPEC.md
; 53.6.1 is the record of the snapshot that used to short-circuit it: a
; bracket takes real TIME, so the desktop behind it is live state (the bar's
; clock, the Timer, a Bounce, and the exclusive app's own window content)
; rather than an image to be put back. wm_paint_all asks every window what it
; looks like NOW, which is the only question with a correct answer here.
; -----------------------------------------------------------------------------
fsx_restore:
    push ax
    push di
    call sch_fast_off           ; the sub-tick goes back FIRST (SPEC.md 53.2.1):
                                ; everything below this runs on the ordinary
                                ; quantum, and it is a no-op if none was armed
    cmp byte [fsx_cur], 0xFF
    je .nomode
    call vid_setmode            ; idempotent; the Hercules body clears its
.nomode:                        ; 32KB, the BIOS resets the palette + state
.kd:
    mov ah, 1                   ; drain the BIOS key buffer
    int 0x16
    jz .kdone
    xor ah, ah
    int 0x16
    jmp short .kd
.kdone:
.ed:
    mov di, fsx_evtmp           ; drain the event queue
    call evq_pop
    jc .edone
    call wm_wake_eaten          ; ...but a package's wake is OWED, not dropped
    jmp short .ed               ; (SPEC.md 74.1.1)
.edone:
    pushf
    cli
    mov byte [fsx_task], 0xFF   ; disarm - sleepers marked ready during the
    mov byte [fsx_worker], 0xFF ; bracket run from the next tick
    popf
    mov byte [fsx_cur], 0xFF
%ifdef KERN_BIG
    call vid_fsx_leave          ; the DESKTOP's shape back - the union, the
                                ; primary's chrome - before anything draws on
                                ; it (SPEC.md 39.18)
%endif
    call menu_force             ; the bar and the strip really WERE overdrawn -
    call dock_force             ; an exclusive app owned the whole screen
    call wm_paint_all           ; (SPEC.md 12.9/30.3), so the logo, the rule
                                ; and every tile are owed. wm_paint_all does
                                ; not force either any more, because nothing
                                ; inside it destroys them; this is a caller
                                ; that genuinely did
%ifdef KERN_BIG
    call vid_fsx_unblank        ; ...and only NOW the dark monitors, because a
                                ; blanked card still holds the frame it was
                                ; blanked on and lighting it first shows that
                                ; one (SPEC.md 39.18.1)
%endif
    call blk_wake               ; the idle clock restarts here (SPEC.md 64.4).
                                ; The UI task does not run inside a bracket
                                ; (53.1), so nothing spends that clock while
                                ; one is up and nothing may - the app owns the
                                ; video mode, and blanking a card the kernel
                                ; is not driving is the one thing this feature
                                ; must never do. What that leaves is the
                                ; arithmetic on the way out: somebody plays a
                                ; game or a module for as long as they like,
                                ; and an hour of it comes back to a
                                ; [ticks] - [blk_t0] that has not only passed
                                ; BLK_IDLE but wrapped, so the very first pass
                                ; after this would dark the screen on a
                                ; machine somebody is sitting in front of.
                                ; The bracket WAS the user. CF is not read:
                                ; the screen cannot be dark here, because
                                ; entering a bracket took the input that lit it
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; fsx_mode - set a foreign mode and describe it (SPEC.md 53.4, 0x02D0)
;
; in:       AL = FSXM_* id, ES:DI = an FSI_SIZE buffer (ES is the caller's
;           own, the gfx_blit4 idiom). Bracket-only, exclusive task only.
; out:      CF=0 mode set, buffer filled. CF=1 refused: no bracket, wrong
;           task, id out of range, or not on this adapter (fsx_caps' own
;           bit). Every register preserved.
; -----------------------------------------------------------------------------
fsx_mode:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, [fsx_task]
    cmp ah, 0xFF
    je .fail
    cmp ah, [sch_cur]
    jne .fail
    cmp al, FSXM_COUNT
    jae .fail
    mov bl, [vid_kind]          ; the caps bit - the same predicate the
%ifdef KERN_BIG                 ; app's greying used (SPEC.md 47), and for
    cmp byte [vid_ndisp], 1     ; THE DISPLAY THIS BRACKET IS ON rather than
    jbe .kind                   ; the primary (SPEC.md 39.18.3/39.18.2). Once
    push ax                     ; the collapse below has run there is only one
    push di                     ; display left and [vid_kind] IS that one, so
    mov al, [fsx_wdisp]         ; a second fsx_mode call takes the short path
    call vid_ctx_ptr
    mov bl, [di+VID_CTX_KIND]
    pop di
    pop ax
.kind:
%endif
    xor bh, bh
    shl bx, 1
    mov dx, [bx+fsx_capstab]
    mov cl, al
    mov bx, 1
    shl bx, cl
    test dx, bx
    jz .fail

    cmp byte [fpg_on], 0        ; **NO KERNEL CHROME SURVIVES INTO A FOREIGN
    je .nofpg                   ; MODE** (SPEC.md 12.8.5). fpg_arm refuses to
    call fpg_finish             ; arm once [fsx_cur] is set, but a caller may
.nofpg:                         ; have armed the progress widget from a file
                                ; read EARLIER IN THE SAME LOCK HOLD - the hold
                                ; this bracket inherited from its callback - and
                                ; then fpg_step would draw into the app's raster
                                ; and fpg_finish would repair a menu bar that is
                                ; not on the screen. Its own teardown is the
                                ; right one and it is called HERE, before a
                                ; single mode register moves, so its erase, its
                                ; bar repaint and the cursor debt it may owe
                                ; (SPEC.md 12.8.4) all land on the desktop that
                                ; is still up. AFTER the caps test, so a
                                ; REFUSED mode still changes nothing at all.

%ifdef KERN_BIG
    push ax                     ; ...and ONLY NOW is the machine one display
    mov al, [fsx_wdisp]         ; with every other card dark (SPEC.md 39.18):
    call vid_fsx_enter          ; a mode is about to be set, which makes every
    pop ax                      ; other display's geometry meaningless. After
%endif                          ; the caps test, so a REFUSED mode leaves the
                                ; desktop exactly as it found it; idempotent,
                                ; because vid_fsx_enter no-ops at ndisp <= 1

    mov ch, al                  ; CH = the id, live across the mode set
    cmp al, FSXM_TEXT80
    je .text
    cmp al, FSXM_HERC
    je .herc
    cmp al, FSXM_MODEX
    je .modex
    mov bx, fsx_bmodes          ; the plain BIOS rows
    xlatb                       ; AL = the BIOS mode number
    call fsx_setbios
    jmp short .fill
.text:
    call vid_text               ; mode 3, or 3BFh<-0 + mode 7 on Hercules
    jmp short .fill
.herc:
    call vid_setmode            ; the 6845 body + the load-bearing 32KB clear
    jmp short .fill
.modex:
    mov al, 0x13
    call fsx_setbios
    call fsx_modex              ; unchain + retime to 320x240 (SPEC.md 53.4)
.fill:
    mov al, ch                  ; DS:SI = the id's row, ES:DI = the buffer;
    mov ah, FSXR_SIZE           ; the row IS bytes 0..13 of the block
    mul ah
    mov si, ax
    add si, fsx_mtab
    push cx                     ; CH is the id and rep movsw eats CX whole
    push di
    cld
    mov cx, 7
    rep movsw
    pop di
    pop cx
    mov [es:di+FSI_MODE], ch
    mov byte [es:di+FSI_RSVD], 0
    ; the three per-adapter fixups (SPEC.md 53.4)
    cmp ch, FSXM_HERC
    jne .f1
    mov ax, [vid_seg]           ; NOT a constant: a HERCSEG= test build
    mov [es:di+FSI_SEG], ax     ; (SPEC.md 39.9) flows through to the app,
    jmp short .f3               ; or hercshot could never see an fsx frame
.f1:
    cmp ch, FSXM_TEXT80
    jne .f3
    cmp byte [vid_kind], VID_HERC
    jne .f2
    mov word [es:di+FSI_SEG], 0xB000
    mov byte [es:di+FSI_PAGES], 1
    jmp short .f3
.f2:
    cmp byte [vid_kind], VID_CGA
    jne .f3
    mov byte [es:di+FSI_PAGES], 4
.f3:
    mov [fsx_cur], ch
    clc
    jmp short .out
.fail:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fsx_surf - the rect this bracket owns (SPEC.md 53.7.1, slot 0x03F8)
;
; in:       nothing. Bracket-only, exclusive task only.
; out:      CF=0 with AX = x, BX = y, CX = w, DX = h - the surface the app may
;           draw on, in the coordinates the DRAWING SLOTS take. CF=1 outside a
;           bracket or on the wrong task, with every register preserved.
;
; A SAME-MODE BRACKET (SPEC.md 53.7) DOES NOT COLLAPSE THE DESKTOP (39.18.3),
; so inside one the coordinate system is still the whole virtual desktop -
; and an app that reads "fullscreen" as (0,0) plus OSAPI_VIDEO's size then
; draws on the PRIMARY however far away its own window is. Measured on a
; cycle-accurate XT with a VGA and a Hercules extended right, Tracker's
; window wholly on the Hercules: pressing F took the VGA from 189,477 lit
; pixels to 95,619 - the FT2 screen, on the monitor Tracker is not on - while
; the Hercules kept the stale windowed desktop it had. Both come back on the
; way out, because SPEC.md 53.6's wm_paint_all repaints the world, so what
; this defect costs is the whole of the fullscreen session and nothing after
; it: "switching Tracker to full screen corrupts the other screen".
;
; Missile Command never had it because it stacks SPEC.md 11.2 on the bracket
; and wm_fullscreen sizes the window to THE DISPLAY IT IS ON (39.17.1) - so
; its geometry came out of the window record and was already right. This is
; that same answer for the two consumers that deliberately take no 11.2
; surface (SPEC.md 42.7's measured reason), handed over rather than derived.
;
; IT ANSWERS (0,0,w,h) ON EVERY ONE-DISPLAY MACHINE and after any fsx_mode
; call, because the collapse has then run and the machine really is one
; display at the virtual origin - which is exactly what those apps used to
; hard-code, so a single-display machine cannot see this change at all.
;
; The extent comes out of the DISPLAY'S RECORD and not out of [vid_cw]: the
; live block follows vid_ctx_act, which every straddling primitive moves, so
; a read of it answers about whichever display the last drawing call touched.
; -----------------------------------------------------------------------------
fsx_surf:
    push di
    push ax
    mov ah, [fsx_task]
    cmp ah, 0xFF
    je .fail
    cmp ah, [sch_cur]
    jne .fail
    pop ax
    xor ax, ax
    xor bx, bx
    mov cx, [vid_cw]
    mov dx, [vid_ch]
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .done
    mov al, [fsx_wdisp]
    call vid_ctx_ptr            ; DI = that display's record; AX preserved
    mov ax, [di+VID_CTX_VX]
    mov bx, [di+VID_CTX_VY]
    mov cx, [di+VID_CTX_CW]
    mov dx, [di+VID_CTX_CH]
.done:
%endif
    pop di
    clc
    ret
.fail:
    pop ax
    pop di
    stc
    ret

; -----------------------------------------------------------------------------
; fsx_setbios - int 10h AH=00h with everything banked. A mode set is rare
; and some BIOSes scribble more than AX, so the insurance is cheap where it
; matters and paid nowhere hot.
; in: AL = BIOS mode number
; -----------------------------------------------------------------------------
fsx_setbios:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor ah, ah
    int 0x10
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fsx_modex - Mode X: 13h unchained and retimed to 320x240 (SPEC.md 53.4)
;
; The canonical sequence: chain4 off through a sequencer sync reset, the
; 25MHz dot clock and 480-scan polarity on the misc output, the CRTC
; unprotected and retimed (240 doubled lines), dword mode off, byte mode
; on, and all four planes cleared through map mask 0Fh - the BIOS cleared
; only the 64KB the chained mode could see.
; -----------------------------------------------------------------------------
fsx_modex:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov dx, 0x3C4
    mov ax, 0x0604              ; chain4 off, odd/even off
    out dx, ax
    mov ax, 0x0100              ; sync reset while the clock changes
    out dx, ax
    mov dx, 0x3C2
    mov al, 0xE3                ; 25MHz, 60Hz, 480-line sync polarity
    out dx, al
    mov dx, 0x3C4
    mov ax, 0x0300              ; run again
    out dx, ax
    mov dx, 0x3D4
    mov al, 0x11                ; unprotect CRTC 0..7
    out dx, al
    inc dx
    in al, dx
    and al, 0x7F
    out dx, al
    dec dx
    mov si, fsx_xcrtc
    mov cx, 10
    cld
.crtc:
    lodsw
    out dx, ax
    loop .crtc
    mov dx, 0x3C4
    mov ax, 0x0F02              ; map mask: all four planes...
    out dx, ax
    mov ax, 0xA000              ; ...and clear the whole 256KB
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, 0x8000
    rep stosw
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

fsx_xcrtc:                      ; Mode X CRTC retiming, index in the low byte
    dw 0x0D06, 0x3E07, 0x4109, 0xEA10, 0xAC11
    dw 0xDF12, 0x0014, 0xE715, 0x0616, 0xE317

; -----------------------------------------------------------------------------
; fsx_wait - the frame clock, and the present (SPEC.md 53.5, 0x02D8)
;
; in:       AL = FSXW_TICK  (0) return at the next [ticks] change (hlt between
;                              polls);
;           AL = FSXW_VSYNC (1) return in vertical retrace;
;           AL = FSXW_FRAME (2) return at the next IRQ0 - the SUB-TICK while
;                              FSXF_FASTTICK is armed, the tick otherwise -
;                              handing the machine to the kept worker in the
;                              meantime (SPEC.md 53.5.1).
;           Bracket-only, exclusive task only.
; out:      CF=0 (a timed-out retrace wait too - timing degrades, it does
;           not fail); CF=1 outside a bracket or on an unknown AL. Every
;           register preserved.
;
; Every retrace poll is bounded by a [ticks] delta (SPEC.md 37.90's rule: the one
; way to hang is waiting forever for a bit that never changes on a machine
; where every read is 0FFh).
;
; WHY FSXW_FRAME EXISTS, AND WHY IT YIELDS (SPEC.md 53.5.1). The other two
; wait by BURNING the CPU, and under a round-robin scheduler a busy-wait is
; indistinguishable from work: the exclusive task takes its half of the
; machine whether it needs it or not. That is affordable for a bracket that
; is alone in the machine and it is not affordable for one with a
; FSXF_KEEPWORKER, because the whole promise of that flag is that the worker
; keeps running - and Tracker's worker is a MOD mixer that needs ~55% of a
; 4.77MHz 8088 to hold 5,500 Hz. Measured under a cycle-accurate 8088:
; fsx_insync + fsx_wait were 28.8% of the machine, the mixer got 35% where
; windowed it gets 47%, and the card was fed 4,808 bytes a second against
; the 5,533 it plays - so the ring's three-second cushion drained over the
; first half-minute and every second after that dropped audio.
;
; So this one does not poll a port at all. It waits on [sch_subs], which the
; PIT advances whether we are running or not, and between polls it calls
; task_yield - which hands the CPU to the next ready whitelisted task rather
; than to the next iteration of a loop. When a worker is ready that costs ONE
; context switch per frame and gives it everything else; when nothing is
; ready sch_switch resumes us and it degenerates to the spin the other two
; paths always were, which is exactly right, because with nothing else
; runnable there is nobody to starve.
;
; It is a frame clock rather than merely a cheaper wait: FSX_SUBTICK is 3, so
; while FSXF_FASTTICK is armed this returns 54.6 times a second - faster than
; a Hercules' 50 Hz retrace, within 10% of a CGA's 60, and evenly spaced by a
; crystal instead of by whatever the drawing loop managed last tick.
; -----------------------------------------------------------------------------
fsx_wait:
    push ax
    push bx
    push dx
    mov ah, [fsx_task]
    cmp ah, 0xFF
    je .fail
    cmp ah, [sch_cur]
    jne .fail
    cmp al, FSXW_FRAME          ; the argument is checked BEFORE the present,
    ja .fail                    ; so an unknown clock never flushes and then
                                ; refuses (SPEC.md 53.1's all-or-nothing)
    cmp al, FSXW_FRAME
    je .frame
    or al, al
    jnz .vsync
    mov bx, [ticks]             ; AL = 0: the next tick
.tick:
    hlt                         ; IF=1 in callback context - the tick wakes it
    cmp bx, [ticks]
    je .tick
    jmp short .ok
.frame:
    mov bx, [sch_subs]          ; AL = 2: the next IRQ0, however the divider
.fw:                            ; is set - and the wait itself is the yield,
    call task_yield             ; so the time we do not use is the worker's
    cmp bx, [sch_subs]
    je .fw
    jmp short .ok
.vsync:
    mov dx, 0x3DA               ; the port by the CURRENT mode: on a Hercules
    cmp byte [vid_kind], VID_HERC ; machine every settable mode is the 3BAh
    jne .pa                     ; family, on VGA/CGA every one is 3DAh - so
    mov dx, 0x3BA               ; the adapter kind IS the mode's family
.pa:
    mov bx, [ticks]             ; phase A: wait until OUT of retrace, so a
.paw:                           ; call landing mid-retrace syncs to the NEXT
    call fsx_insync
    jnc .pb
    mov ax, [ticks]
    sub ax, bx
    cmp ax, 3
    jb .paw
    jmp short .ok               ; dead port: timed out, return anyway
.pb:                            ; phase B: wait until retrace begins
    mov ax, [ticks]
    sub ax, bx
    cmp ax, 3
    jae .ok
    call fsx_insync
    jnc .pb
.ok:
    clc
    jmp short .out
.fail:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fsx_page - show one of the pages FSI_PAGES advertises (SPEC.md 53.10, slot
;            0x04E8)
;
; in:       AL = the page index. Bracket-only, exclusive task only, and only
;           after an fsx_mode call in a mode that HAS more than one page.
; out:      CF=0 and that page is on the glass - the write is latched at the
;           next vertical retrace and this waits for it, so the caller may
;           start drawing on another page the moment it returns. CF=1 refused,
;           with nothing written. Preserves every register.
;
; WHY THIS IS A SLOT AND NOT FOLKLORE. FSI_PAGES has been published since
; SPEC.md 53.4 and until now there was no supported way to act on it: the
; mechanism is different on every adapter that offers one - Mode X moves the
; CRTC start address in BYTES, a Hercules does not move it at all but flips
; bit 7 of 3B8h, and that bit does nothing until 3BFh bit 1 allows it, which
; vid_setmode deliberately leaves clear (SPEC.md 39.6). None of that is
; derivable from the info block, so every app that page-flips would rediscover
; it, and the two ways to get it wrong - writing the start address without
; waiting for the latch, and enabling the Hercules page without its
; configuration bit - both look like a card that has stopped working.
;
; It answers for the GRAPHICS modes that offer a page. Text pages are the
; BIOS's own 4KB convention rather than this table's stride x height, and no
; consumer here wants one; a text bracket that does can have this routine
; learn it, which is where that belongs.
; -----------------------------------------------------------------------------
fsx_page:
    push ax
    push bx
    push dx
    mov ah, [fsx_task]
    cmp ah, 0xFF
    je .fail
    cmp ah, [sch_cur]
    jne .fail
    mov ah, [fsx_cur]
    cmp ah, FSXM_HERC
    je .herc
    cmp ah, FSXM_MODEX
    jne .fail
    cmp al, 3                       ; Mode X: three pages of 19,200 bytes,
    jae .fail                       ; and 3 x 19,200 still fits a segment
    mov bl, al
    xor bh, bh
    mov ax, 19200
    mul bx                          ; DX:AX, and DX is 0 by construction
    mov bx, ax
    mov dx, 0x3D4
    mov al, 0x0C                    ; start address high...
    mov ah, bh
    out dx, ax
    mov al, 0x0D                    ; ...and low
    mov ah, bl
    out dx, ax
    jmp short .sync
.herc:
    cmp al, 2
    jae .fail
    mov bl, 0x0A                    ; graphics, video on...
    mov ah, 1                       ; ...and 3BFh bit 0 alone, which is what
    or al, al                       ; vid_setmode leaves (SPEC.md 39.6)
    jz .h0
%ifdef KERN_BIG
    call vid_dual_ok                ; **AND THE SECOND PAGE IS REFUSED ON A
    jnc .fail                       ; MACHINE THAT HAS A COLOUR CARD TOO**
%endif                              ; (SPEC.md 53.10). vid_setmode leaves 3BFh
                                    ; bit 1 clear for a reason it states at the
                                    ; write: that page is at B8000, where a CGA
                                    ; in the same machine lives, and turning
                                    ; the decode on puts two cards on the bus
                                    ; for the same addresses. A bracket does
                                    ; not make that safe, so the page is
                                    ; refused rather than the decode forced,
                                    ; and a lone-Hercules XT still gets it.
                                    ; NOT [vid_ndisp]: vid_fsx_enter has
                                    ; already forced that to 1 by the time any
                                    ; call can reach here
    or bl, 0x80                     ; ...and page 1
    mov ah, 3                       ; ...whose decode has to be allowed with it
.h0:
    mov dx, 0x3BF                   ; ALLOW the second page: bit 0 graphics,
    mov al, ah                      ; bit 1 the page - and bit 1 goes back off
    out dx, al                      ; for page 0, which needs no second decode
    mov dx, 0x3B8                   ; and is the state the desktop comes back
    mov al, bl                      ; to
    out dx, al
.sync:
    mov al, FSXW_VSYNC              ; the latch happens at the retrace, so the
    call fsx_wait                   ; page is only really shown after one -
    clc                             ; and fsx_wait's own timeout means a dead
    jmp short .out                  ; status port cannot hang this
.fail:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fsx_insync - CF = 1 when the adapter at DX is in vertical retrace.
; VGA/CGA (3DAh): bit 3 set. Hercules (3BAh): bit 7 LOW while vertical sync
; is active - the polarity is inverted there.
; -----------------------------------------------------------------------------
fsx_insync:
    push ax
    in al, dx
    cmp dl, 0xBA
    je .herc
    test al, 8
    jz .no
    jmp short .yes
.herc:
    test al, 0x80
    jnz .no
.yes:
    stc
    jmp short .done
.no:
    clc
.done:
    pop ax
    ret

; --- the mode rows: bytes 0..13 of the FSI block, verbatim (SPEC.md 53.4) ----
FSXR_SIZE equ 14

fsx_mtab:
    ;  seg     w    h    stride  flags        bpp banks pages  bstep
    dw 0xB800, 80,  25,  160
    db FSIF_TEXT, 0, 1, 8
    dw 0                        ; 0 TEXT80 (B000/pages fixed up per adapter)
    dw 0xB800, 40,  25,  80
    db FSIF_TEXT, 0, 1, 8
    dw 0                        ; 1 TEXT40
    dw 0xB800, 320, 200, 80
    db FSIF_BANKED, 2, 2, 1
    dw 0x2000                   ; 2 CGA320 (even/odd row interleave)
    dw 0xB800, 640, 200, 80
    db FSIF_BANKED, 1, 2, 1
    dw 0x2000                   ; 3 CGA640
    dw 0xB000, 720, 348, 90
    db FSIF_BANKED, 1, 4, 2
    dw 0x2000                   ; 4 HERC (seg <- [vid_seg] at fill)
    dw 0xA000, 320, 200, 40
    db FSIF_PLANAR, 4, 1, 8
    dw 0                        ; 5 VGA0D
    dw 0xA000, 320, 200, 320
    db 0, 8, 1, 1
    dw 0                        ; 6 VGA13 (linear, byte per pixel)
    dw 0xA000, 640, 480, 80
    db FSIF_PLANAR, 4, 1, 1
    dw 0                        ; 7 VGA12
    dw 0xA000, 320, 240, 80
    db FSIF_PLANAR, 8, 1, 3
    dw 0                        ; 8 MODEX (stride = bytes per PLANE row)

fsx_bmodes:                     ; the plain-BIOS rows' mode numbers; ids 0,
    db 0x00, 0x01, 0x04, 0x06  ; 4 and 8 dispatch before the xlatb and
    db 0x00, 0x0D, 0x13, 0x12  ; never read theirs
    db 0x13

; --- state -------------------------------------------------------------------
; **`.text`, SEEDED, AND NOT `.bss`** (SPEC.md 12.8.5.1). This byte used to be
; `.bss` on the note that fsx_run writes it before anything reads it - true of
; every reader it had, because every one of them was INSIDE a bracket, where
; fsx_run has already stored 0xFF.
;
; fpg_arm's refusal (SPEC.md 12.8.5) is the first reader outside one, and it
; runs on a machine that has never entered a bracket. `-f bin` emits nothing
; for `.bss` (SPEC.md 2.6) and the boot read lands the image's inter-section
; padding on these bytes, so the byte was 0 - which is FSXM_TEXT80, not
; "unswitched" - and the progress widget was refused for every file operation
; on the machine from boot until some fsx app had run and restored. An install
; is minutes of int 13h behind a held gfx lock with the widget as the only
; sign of life, so it was reported as a LOCK.
;
; One byte of `.text` against one given back from `.bss`, no boot-time store,
; and the value is right from the kernel's first instruction. api_name's rule
; and spl_fp's: a byte whose RESTING value is not zero cannot live in `.bss`
; here.
fsx_cur:   db 0xFF              ; the FSXM id on screen, 0xFF = unswitched

section .bss

fsx_evtmp: resb 8               ; fsx_restore's event-drain scratch
%ifdef KERN_BIG
fsx_wdisp: resb 1               ; the display the running bracket's window is
                                ; on (SPEC.md 39.18.3). Written by fsx_run
                                ; before the app's proc runs and read only by
                                ; fsx_mode, so it needs no boot-time init
                                ; either - and STILL CORRECT after the byte
                                ; above lost the same argument (SPEC.md
                                ; 12.8.5.1), because fsx_mode is legal only
                                ; inside the bracket. The claim is about the
                                ; SET OF READERS, so a new one reached from
                                ; desktop code falsifies it - and it is the
                                ; ONE fact fsx_run still establishes about
                                ; displays, the acting on it having moved to
                                ; the mode set
%endif

section .text
