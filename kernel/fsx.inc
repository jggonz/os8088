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
FSXF_RATE       equ 4           ; ...bit 2 (SPEC.md 53.2.2, kern_big): IRQ0 at
                                ; DX, a PIT divisor, with a hook at DI called
                                ; on every one. [ticks] stays exact by an
                                ; accumulator
FSX_RATE_MIN    equ 2048        ; the fastest divisor it takes: 582 Hz
FSXF_REAL       equ 8           ; ...bit 3 (SPEC.md 53.2.3): the bracket in REAL
                                ; mode on kern_mega - every other kernel takes
                                ; the bit and strips it at the door
; ...and its ANSWER, in SI on the way out (SPEC.md 53.2.3; mirrored in
; apps/os88api.inc): real mode, or why the bracket ran in protected mode.
; One test each at the door, so each lifts by itself - tested NOT in the
; table's order but KEEP; the list's VOL, FRAG or DRV; ROOM (the copy, then
; the service area); the drivers' DROOM, or DRV for a SOUND.DRV word above
; 1MB; EXTD.DRV's DROOM (fsx_rprepf)
FSXR_REAL       equ 0
FSXR_KEEP       equ 1               ; FSXF_KEEPWORKER
FSXR_SVC        equ 2               ; (RETIRED by W7 - never answered: a
FSXR_EXTD       equ 3               ; driver's worker parks, EXTD.DRV runs
FSXR_SERIAL     equ 4               ; low, a lease is served; the numbers stay)
FSXR_ROOM       equ 5               ; no conventional room for the copy, or
                                    ; for the service area (W3)
FSXR_VOL        equ 6               ; a listed stream real mode cannot read
FSXR_FRAG       equ 7               ; ...more runs or files than the list holds
FSXR_DRV        equ 8               ; a listed driver class not carried low
FSXR_DROOM      equ 9               ; no conventional room for a driver's copy
; ...and FSXF_REAL's LIST (SPEC.md 53.2.3, W3; mirrored in apps/os88api.inc):
; the caller's BP, entries naming what the bracket will read
FSXL_END        equ 0
FSXL_FILE       equ 1               ; db FSXL_FILE, a NUL 8.3 name
FSXL_DRV        equ 2               ; db FSXL_DRV, a DRVC_* class
FSXL_NMAX       equ 4               ; ...files a list may name
FSXL_RUNS       equ 224             ; ...runs, all of them together
SCH_RATE        equ FSXF_RATE + 1 ; [sch_fast]'s value while FSXF_RATE is armed:
                                ; 5, outside sch_fast_on's 2..4 and not 0.
                                ; fsx_run DERIVES it, and FSX_SUBTICK, from
                                ; CX as (CL & 6) + 1 - the %if below
%ifdef KERN_BIG
%if (sch_rdiv - sch_fdivs) != (SCH_RATE - 2) * 2
  %error "sch_fast_on reads SCH_RATE's divisor as sch_fdivs' row N: sch_rdiv must follow the table directly"
%endif
%endif
%if FSX_SUBTICK != FSXF_FASTTICK + 1
  %error "fsx_run derives sch_fast_on's AL as (CL & 6) + 1: FSX_SUBTICK must be FSXF_FASTTICK + 1"
%endif

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
%ifdef KERN_BIG
    cmp bx, 16                  ; BX = 1..15 ASKS ABOUT DISPLAY BX-1 (SPEC.md
    jae .ask                    ; 53.1.2) - no window pointer is that small -
    cmp [vid_ndisp], bl         ; and CF = 1 is "BX-1 is past the last one":
    jb .out                     ; the refusal's own carry, out through the one
                                ; `pop bx`. BX = 0 (the frontmost) compares
                                ; below nothing and asks
%else
    lea ax, [bx-2]              ; ONE display: BX = 1 is the primary, which
    cmp ax, 14                  ; this answers whatever BX is, and 2..15 are
    jb .out                     ; displays there are not (SPEC.md 53.1.2) -
%endif                          ; CF = 1, the refusal, out through the `pop`
.ask:
    mov dl, [vid_kind]
%ifdef KERN_BIG                 ; ...of the display the ASKED-ABOUT window is
                                ; on (SPEC.md 39.18.1)
    pushf                       ; IF = 0 across the module (SPEC.md 39.19.6.1):
    cli                         ; a worker may ask this WITHOUT the lock, and
                                ; a switch inside EXTD.DRV could let the panel
                                ; drop the image under it. A few hundred cycles
    mov ah, EXTF_CAPS           ; AX is an output, so AH is free for the
    EXTCALL EXT_FSX             ; selector. BX = 0 is the frontmost window,
    popf                        ; and the primary if nothing is visible; the
%endif                          ; CENTRE, then the origin (SPEC.md 39.17) -
                                ; EXTD.DRV's ext_fsx_caps. UNGATED: an app
                                ; asks this once to grey a menu, and one
                                ; display keeps DL = [vid_kind] (39.19.6)
%ifdef VID_EXT
    cmp dl, VID_VESA            ; a VESA display answers VID_VGA (SPEC.md
    jne .kind                   ; 120.5): the kind that names the BIOS modes
    mov dl, VID_VGA             ; a bracket may set, which is what every
.kind:                          ; package asks this for - Gorillas took kind 4
%endif                          ; for "neither VGA nor CGA" and drew planar
                                ; onto the packed desktop. The depth is
                                ; OSAPI_VIDEO's and OSAPI_WM_DISPLAY's DH
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov ax, [bx+fsx_capstab]
    clc
.out:
    pop bx                      ; `pop` writes no flag: either answer's CF
    ret

fsx_capstab:                    ; indexed by VID_* (VGA 0, HERC 1, CGA 2, EGA 3)
    dw 0x01EF                   ; VGA:  0,1,2,3 + 5,6,7,8
    dw 0x0011                   ; HERC: 0 (mode 7 text) + 4 (its own graphics)
    dw 0x000F                   ; CGA:  0,1,2,3
%ifdef GFX_VGA                  ; the EGA row is kern_big's alone
    dw 0x000F                   ; EGA:  0,1,2,3 - the CGA-compatible modes only,
                               ; which an EGA BIOS all has. Its own mode 10h
                               ; has no FSXM id, and mode 0Dh (native, planar)
                               ; is a safe later addition, not pass 1
                               ; (SPEC.md 39.24, docs/plans/completed/EGA-PLAN.md 7)
%endif
%ifdef VID_EXT
    dw 0x01EF                   ; VESA: VGA's row (SPEC.md 120.5) - it is the
                               ; same card, every BIOS mode in it is legal
                               ; from a VBE mode, and fsx_leave's vid_setmode
                               ; takes the VESA arm back. WITHOUT THIS ROW the
                               ; table was indexed past its end by kind 4
%endif

; -----------------------------------------------------------------------------
; fsx_run - the bracket (SPEC.md 53.1, slot 0x0235 via X stub)
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
fsx_rfail:                      ; THE REFUSAL, ABOVE THE ENTRY. Nine `j..`
    stc                         ; below were out of short reach as the 5-byte
    jmp kret_di                 ; inverted-jump-plus-near-jmp form; from here
                                ; they are 2 bytes each, backward. It was a
                                ; `jmp short .tests` trampoline INSIDE fsx_run
                                ; (`stc / jmp .out`), whose own jump went with
                                ; it (kernel size pass 17, -2) - and it is a
                                ; GLOBAL label, so no other routine's `.fail`
                                ; can be retargeted by it
fsx_run:
    call kent_di                ; = push ax/bx/cx/dx/si/di
%ifdef KERN_MEGA
    mov si, cx                  ; the flags as ASKED, FSXF_REAL among them:
%endif                          ; SI is nobody's until the release loop below
    and cl, ~FSXF_REAL & 0xFF   ; EVERY kernel takes FSXF_REAL (SPEC.md 53.2.3)
                                ; and strips it here, so the tests below see
                                ; the flags they always saw: kern_big and
                                ; kern_small are in real mode already, and
                                ; kern_mega acts on SI's copy after the fence
%ifdef KERN_BIG
    cmp cl, FSXF_FASTTICK + FSXF_RATE ; 6: CL >= 8 is an undefined bit, and 6
    jae fsx_rfail                   ; and 7 are FASTTICK WITH RATE, which refuses
    cmp [vid_ndisp], ch         ; too - one channel 0 (53.2.2). CH = 0 the
    jb fsx_rfail                    ; window's display, n display n-1 (53.1.2)
%else
    cmp cx, FSXF_KEEPWORKER + FSXF_FASTTICK + 1 ; (CH: any display past the
    jae fsx_rfail                   ; window's refuses, CX being past the flags)
%endif
                                ; undefined flag bits refuse, so the word
                                ; stays extensible (SPEC.md 53.1). "any bit
                                ; above the defined ones is set" and "CX >=
                                ; 2^n" are the same statement ONLY BECAUSE THE
                                ; DEFINED BITS ARE THE LOW n: define bit 4
                                ; without bit 3 and this must go back to a
                                ; mask test
    cmp word [sch_cur], 0xFF00  ; the UI task alone may enter - a worker's
    jne fsx_rfail                   ; bracket would freeze the UI task mid-frame -
                                ; and one bracket at a time: [sch_cur] = 0 and
                                ; [fsx_task] = 0xFF, ONE word (adjacent, the
                                ; %if at fsx_mine)
%ifdef KERN_BIG
    mov [fsx_wdisp], ch         ; THE DISPLAY ASKED FOR, banked - no bracket is
%endif                          ; up, so nothing reads it. CH may stay: only CL
                                ; is read until the release loop loads all of CX
%ifdef KERN_BIG
    test cl, FSXF_RATE          ; SPEC.md 53.2.2: DX a divisor, DI a hook
    jz .norate                  ; (never with FASTTICK: refused above)
    cmp dx, FSX_RATE_MIN
    jb fsx_rfail
    mov [sch_rdiv], dx          ; banked NOW, because the fence below spends
    mov [sch_rbp], di           ; DX and DI. Inert until [sch_fast] reads
    mov [sch_rwin], bx          ; SCH_RATE, which only the arm below writes -
    xor dx, dx                  ; and no bracket is up (tested above), so no
    mov [sch_racc], dx          ; live rate is being overwritten. The window
    mov [sch_rpend], dx         ; is the fence's to prove, below, and the ISR
.norate:                        ; calls through its record as wm_pkgcall
                                ; does. The accumulator from 0 and nothing
                                ; owed: .bss is NOT zeroed (SPEC.md 12.8.5.1).
                                ; Nothing in flight is [sch_rbusy] = 0, which
                                ; sched_init stores and every hook call
                                ; balances
%endif
    call cur_unlazy              ; the bracket owns the machine and the held
                                ; lock is what keeps the arrow off it - which
                                ; a DEFERRED hide would not (SPEC.md 7.1.4).
                                ; A same-mode bracket (SPEC.md 53.7) never
                                ; reaches vid_setmode, so this is the only
                                ; place that catches it, and the double cursor
                                ; SPEC.md 48.13 watches for is the symptom
    cmp word [fdlg_win], 0      ; not under the modal file dialog (SPEC.md 38)
    jne fsx_rfail
    call inst_pkg_fence         ; the ownership fence (SPEC.md 53.1), which is
    jc fsx_rfail                    ; inst_pkg_spawn's and now shared with it: BX
                                ; bounded, a live package instance whose
                                ; segment is the caller's own DS, and AX an
                                ; entry inside its image+bss.  DI = the record
%ifdef KERN_BIG
    test cl, FSXF_RATE
    jz .nohook
    pop dx                      ; the hook - the caller's DI, which is the
    push dx                     ; LAST of the six pushes at the door - fenced
    cmp dx, [di+I_SIZE]         ; as the entry is: inside the SAME instance's
    jae fsx_rfail                   ; image+bss (DX is the fence's scratch, dead
.nohook:                        ; here)
%endif
%ifdef KERN_MEGA
    cmp byte [ss:mega_pm], 0    ; THE REAL-MODE BRACKET (SPEC.md 53.2.3): its
    je .nreal                   ; refusals and its claim, the LAST test at the
    call MEGA_SEL_CODE:fsx_rprepf ; door, so nothing below can refuse after the
.nreal:                         ; claim is taken - when SI asks for it, which
                                ; fsx_rprepf tests. In real mode already (a
                                ; machine kern_mega could not switch) the
                                ; bit is ignored, as on every other kernel.
                                ; It never REFUSES (53.2.3): [fsx_rwhy] is
                                ; FSXR_REAL and the bracket real, or the
                                ; reason and the bracket goes on as today's
%endif
    call fpg_finish             ; **NO KERNEL CHROME SURVIVES INTO A BRACKET**
                                ; (SPEC.md 12.8.5.2). fpg_arm refuses once
                                ; [fsx_task] is set, but a read earlier in this
                                ; lock hold may have armed it, and a SAME-MODE
                                ; bracket never reaches fsx_mode's own payoff.
                                ; It tests [fpg_on] itself - gfx_unlock calls
                                ; it on every unlock - and it is after every
                                ; refusal, so a refused call changes nothing

%ifdef DOCK_OPT
    call dock_drop              ; an OPEN dock's hole would clip the app out
                                ; of its own raster (SPEC.md 30.6.1), and
                                ; fsx_restore repaints the whole screen anyway
%endif
    mov dx, 0xFF00              ; DH = the kept worker's slot, or none; DL =
                                ; task 0, checked above. 0xFF00 AND NOT
                                ; 0x00FF: the store below is little-endian, so
                                ; DL lands in fsx_task and DH in fsx_worker -
                                ; the other way round arms the freeze with
                                ; fsx_task = 0xFF, which IS the no-bracket
                                ; sentinel, and every fsx slot then refuses
                                ; for the rest of the session.
                                ; NO INSTANCE INDEX:
                                ; the release walk below exempts the caller by
                                ; comparing RECORDS (cmp si, di) rather than
                                ; indices, so the six instructions that
                                ; derived one - inst_idx's own arithmetic,
                                ; hand-rolled a seventh time - are gone
    test cl, FSXF_KEEPWORKER
    jz .nokeep
    mov dh, [di+I_TASK]         ; 0xFF when the instance has no worker -
.nokeep:                        ; keeping nothing is not an error

    mov [fsx_task], dx          ; arm the freeze (SPEC.md 53.2): both bytes
%ifdef KERN_MEGA
    cmp byte [fsx_rmode], 0     ; ...and a real-mode bracket's REGION COPY
    je .nrcp                    ; DOWN, here and nowhere earlier: frozen, no
                                ; other task can write the region, and with
                                ; the rate not yet armed no hook can either -
    call MEGA_SEL_CODE:fsx_rcopydf ; at IF = 1 (fsx_rcopyf's AL = 0: down), so
.nrcp:                          ; no period is lost to a 63KB copy (53.2.3)
%endif
%ifdef KBD_DIAG
    push ax                     ; KBDDIAG=1 (SPEC.md 9.8.1): the bracket's
    mov al, 'E'                 ; edge, with its flags, AFTER the arm so the
    mov ah, cl                  ; record says it is inside
    call kbg_mark
    pop ax
%endif
                                ; or neither, in ONE instruction - DL into
                                ; fsx_task and DH into fsx_worker (little-
                                ; endian, adjacent - the %if at fsx_mine). An
                                ; 8086 takes an interrupt only BETWEEN
                                ; instructions, so sch_switch, reading both
                                ; from IRQ0, cannot observe a half-written
                                ; pair and no pushf/cli window is owed (size
                                ; pass 6: it was one, -3)
                                ; No foreign mode yet: [fsx_cur] is 0xFF
                                ; already - seeded so, written by fsx_mode
                                ; alone and put back by fsx_restore, the one
                                ; way out of every bracket
    push ax                     ; ...and the finer quantum, AFTER the arm so
    mov al, cl                  ; the task set it applies to is already the
    and al, FSXF_FASTTICK + FSXF_RATE ; frozen one (SPEC.md 53.2.1) - or the
    inc ax                      ; caller's own rate (53.2.2), banked above.
    call sch_fast_on            ; AL = (CL & 6) + 1: 1 for neither, which
                                ; sch_fast_on refuses as a no-op (N < 2), 3 =
                                ; FSX_SUBTICK for FASTTICK, 5 = SCH_RATE for
                                ; RATE - both at once was refused above, and
                                ; on kern_small bit 2 is. AX is the app's
                                ; entry offset, still owed to wm_pkgcall far
                                ; below; the argument register is AL - and
                                ; the bank STAYS until the display is asked
                                ; below: the release loop wants AL too

    ; silence what the freeze strands (SPEC.md 53.3) - AFTER the arm, so no
    ; foreign task can take a fresh grant between the two. snd_release_inst
    ; is the SPEC.md 29.4 teardown routine: DSV_RELINST first, then the
    ; clip, then the tone channel, then the generation bump that makes the
    ; thaw safe. The caller's own grants stay - it keeps running, which is
    ; what carries a FSXF_KEEPWORKER stream in.
    mov si, inst_tab
%ifdef KERN_BIG
    mov cx, INST_MAX            ; ALL OF CX: CH is still the door's display,
%else                           ; banked above (kern_big). CL alone on
    mov cl, INST_MAX            ; kern_small: CH is 0, because the door refused
%endif                          ; every CX past the flags, and nothing since
                                ; has written CX (the fence's clobbers are DX
                                ; and DI)
    xor al, al
.rel:
    cmp byte [si+I_STATE], 0
    je .relnext
    cmp si, di                  ; the caller's own grants stay - by RECORD,
    je .relnext                 ; which is the same test the index was
    call snd_release_inst
.relnext:
    inc al
    add si, I_RECSZ
    loop .rel

%ifdef KERN_BIG
    dec byte [fsx_wdisp]        ; the door's CH: n is display n-1 (SPEC.md
    jns .disp                   ; 53.1.2), and 0 - every caller before it -
                                ; wraps to FFh: the WINDOW's display
    call wm_disp_of             ; WHICH DISPLAY this bracket is on - banked,
    mov [fsx_wdisp], al         ; not acted on (SPEC.md 39.18.3). BX is still
.disp:                          ; the window
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
    pop ax                      ; the app's entry, owed below - banked since
                                ; the quantum above

    call wm_clip_clear          ; THE WHOLE SCREEN IS THE APP'S, so no window's
                                ; clip region may survive into it (SPEC.md
                                ; 53.1.1). A region dies at the caller's next
                                ; gfx_unlock and the bracket runs INSIDE that
                                ; same hold, so a package that armed one to
                                ; draw in its click handler - which is what
                                ; SPEC.md 11.3 tells it to do - and then
                                ; entered a bracket handed us its own content
                                ; rect as the clip for everything: the app's
                                ; own drawing, and then fsx_restore's repaint
                                ; of the DESKTOP. CLEAR SKIES' Fly button was
                                ; exactly that, and the desktop came back with
                                ; no background, no menu bar and no dock,
                                ; while its windows drew correctly because
                                ; wm_paint arms a region per window
    push bp                     ; the app owns the machine from here to its
    mov si, bx                  ; ret: wm_pkgcall's contract, record in SI,
    mov bp, ax                  ; offset in BP (SPEC.md 53.1)
%ifdef KERN_MEGA
    call fsx_rcall              ; ...or the same call in REAL MODE (SPEC.md
%else                           ; 53.2.3), which fsx_rcall decides
    call wm_pkgcall
%endif
    pop bp
.restore:                       ; FSX_RESTORE - one path home, SPEC.md 53.6,
                                ; ordered (its header, below, is the order)
%ifdef KBD_DIAG
    push ax                     ; KBDDIAG=1 (SPEC.md 9.8.1): the other edge,
    mov ax, 'L'                 ; BEFORE the drain below empties the BIOS
    call kbg_mark               ; buffer - a head short of the tail here is a
    pop ax                      ; key int 16h never handed the app
%endif
%ifdef KERN_BIG
    call spk_off                ; a speaker ring the app left open (SPEC.md
                                ; 34.11): channel 2 idle and channel 0 at the
                                ; bracket's rate, before the rate itself goes.
                                ; A REAL-MODE bracket's is closed already, in
                                ; fsx_rcall, whose rate goes off before this
%endif
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
    mov byte [kbd_ae], 0        ; ...and the Alt+Enter latch with it (SPEC.md
                                ; 9.7.1), for the same reason the drain above
                                ; exists: the keystroke that ENDED an exclusive
                                ; app must not also reach the window underneath
                                ; it. A box that leaves full screen on this key
                                ; would otherwise be handed it again the moment
                                ; ui_task ran, and go straight back in
    mov di, fsx_evtmp           ; drain the event queue into OUR OWN buffer -
    call evq_drain              ; the loop is shared, the buffer is not
                                ; (SPEC.md 74.1.1)
    mov ax, 0xFFFF              ; disarm both, one word store (adjacent -
    mov [fsx_task], ax          ; the %if at fsx_mine; atomic as fsx_run's
                                ; arm is) - sleepers marked ready during the
                                ; bracket run from the next tick
    mov [fsx_cur], al           ; ...and no foreign mode
%ifdef KERN_BIG                 ; (AX is free from the drain above to the
    mov ah, EXTF_LEAVE          ; pop at the foot: nothing here reads it)
    EXTCALL EXT_FSX             ; the DESKTOP's shape back - the union, the
                                ; primary's chrome - before anything draws on
                                ; it (SPEC.md 39.18). EXTD.DRV's: with no image
                                ; there was never a second display to restore
%endif
    call blk_relit              ; the bar and the strip really WERE overdrawn -
                                ; an exclusive app owned the whole screen
                                ; (SPEC.md 12.9/30.3), so the logo, the rule
                                ; and every tile are owed. wm_paint_all does
                                ; not force either any more, because nothing
                                ; inside it destroys them; this is a caller
                                ; that genuinely did
%ifdef KERN_BIG
    mov ah, EXTF_UNBLANK        ; ...and only NOW the dark monitors, because a
    EXTCALL EXT_FSX             ; blanked card still holds the frame it was
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
%ifdef KERN_MEGA
    mov al, 0xFF                ; FSXF_REAL's ANSWER in the caller's SI
    xchg al, [fsx_rwhy]         ; (SPEC.md 53.2.3), and the byte at rest
    cmp al, 0xFF                ; again: FFh = not asked here, SI kept
    je .nwhy
    cbw                         ; (FSXR_* < 80h: AH = 0)
    pop di                      ; kent_di's frame: [DI][SI]...
    pop dx                      ; the caller's SI, answered instead (DX is
    push ax                     ; kret_di's to pop back)
    push di
.nwhy:
%endif
    clc
.out:
    jmp kret_di
fsx_mfail:                      ; fsx_mode's refusal, HERE rather than behind a
    stc                         ; `jmp short .tests` trampoline inside it: its
    jmp short fsx_run.out       ; tests reach this backward in two bytes, and
                                ; fsx_run's own exit is the kret_di it wants
                                ; (kernel size pass 17, -3)

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
; (THE CODE IS fsx_run's .restore, its one caller, since size pass 9: AX
; and DI are fsx_run's to restore at kret_di, so the body banks nothing.)

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
    call kent_di                ; = push ax/bx/cx/dx/si/di
    call fsx_mine               ; a bracket, and ours (SPEC.md 53.1)
    jne fsx_mfail
    cmp al, FSXM_COUNT
    jae fsx_mfail
    mov bl, [vid_kind]          ; the caps bit - the same predicate the
%ifdef KERN_BIG                 ; app's greying used (SPEC.md 47), and for
    mov ah, EXTF_KIND           ; THE DISPLAY THIS BRACKET IS ON rather than
    EXTCALL EXT_FSX             ; the primary (SPEC.md 39.18.3/39.18.2). Once
                                ; the collapse below has run there is only one
                                ; display left and [vid_kind] IS that one, so a
                                ; second fsx_mode call keeps BL - as does one
                                ; display, mod_gone's answer. AH is free: the
                                ; id is AL, and AX is restored at .out
%endif
    xor bh, bh
    shl bx, 1
    mov bx, [bx+fsx_capstab]    ; the mask itself, in BX...
    mov cl, al
    shr bx, cl                  ; ...the id's bit down to bit 0...
    shr bx, 1                   ; ...and into CF. TWO shifts, not one variable
    jnc fsx_mfail                   ; one: `shr bx, cl` with CL = 0 leaves the
                                ; flags alone on an 8086, so the fixed shift
                                ; is what actually sets CF. Do not fold them

                                ; **NO KERNEL CHROME SURVIVES INTO A FOREIGN
                                ; MODE** (SPEC.md 12.8.5) - and nothing has to
                                ; be done about it HERE any more: fsx_run paid
                                ; the progress widget off at the bracket's
                                ; door (12.8.5.2) and fpg_arm refuses inside
                                ; any bracket, so [fpg_on] is 0 on every path
                                ; into this routine. The fpg_finish that stood
                                ; here was dead from the day the door's went in

%ifdef KERN_BIG
    push ax                     ; ...and ONLY NOW is the machine one display
    mov al, [fsx_wdisp]         ; with every other card dark (SPEC.md 39.18):
    mov ah, EXTF_ENTER          ; a mode is about to be set, which makes every
    EXTCALL EXT_FSX
    pop ax                      ; other display's geometry meaningless. After
%endif                          ; the caps test, so a REFUSED mode leaves the
                                ; desktop exactly as it found it; idempotent,
                                ; because vid_fsx_enter no-ops at ndisp <= 1

    mov ch, al                  ; CH = the id, live across the mode set
    cmp al, FSXM_TEXT80
    je .text
    cmp al, FSXM_HERC
    je .herc
%ifdef GFX_VGA
    cmp al, FSXM_MODEX          ; no VGA in the build = fsx_capstab's VGA row
    je .modex                   ; is unreachable ([vid_kind] can never be
%endif                          ; VID_VGA), so this id never arrives
    mov bx, fsx_bmodes - 1      ; the plain BIOS rows, indexed FROM 1: ids 0
                                ; and 4 branch away above the xlatb on every
                                ; build, so id 0's row was never read
    xlatb                       ; AL = the BIOS mode number
    call fsx_setbios
    jmp short .fill
.text:
    call vid_text               ; mode 3, or 3BFh<-0 + mode 7 on Hercules
    jmp short .fill
.herc:
    call vid_setmode            ; the 6845 body + the load-bearing 32KB clear
%ifdef GFX_VGA                  ; (without a VGA .fill is the next instruction,
    jmp short .fill             ; and a jump onto it is two dead bytes)
.modex:
    mov al, 0x13
    call fsx_setbios
    push cx                     ; FSX_MODEX, inlined: unchain + retime to
    push di                     ; 320x240 (SPEC.md 53.4). CH is the id, ES:DI
    push es                     ; the buffer .fill writes; vid_wipe spends all three
    mov dx, 0x3C4
    mov ax, 0x0604              ; chain4 off, odd/even off
    out dx, ax
    mov ax, 0x0100              ; sync reset while the clock changes
    out dx, ax
    mov dl, 0xC2                ; DH is 3 from here to the wipe: every port
    mov al, 0xE3                ; below is 3xxh, so each move is of DL alone
    out dx, al                  ; (25MHz, 60Hz, 480-line sync polarity)
    mov dl, 0xC4
    mov ax, 0x0300              ; run again
    out dx, ax
    mov dl, 0xD4
    mov si, fsx_xcrtc           ; ...and the CRTC: the table's FIRST word
    mov cx, 11                  ; unprotects CR0-7 (kernel size pass 17, -8:
                                ; it was `in`, `and al, 7Fh`, `out` of CR11;
                                ; the table writes CR11 = ACh four words on,
                                ; so what the low bits hold until then is
                                ; mode 13h's own 0Eh rather than whatever the
                                ; ROM left - and DF = 0 is SPEC.md 1's
                                ; promise on entry, so the `cld` went)
.crtc:
    lodsw
    out dx, ax
    loop .crtc
    mov dl, 0xC4                ; (DX was 3D4h: the CRTC loop moves no DX)
    mov ax, 0x0F02              ; map mask: all four planes...
    out dx, ax
    VSEG ax, 0xA000             ; ...and clear the whole 256KB. vid_wipe is
    mov cx, 0x8000              ; this body, factored (viddet.inc): both ends
    call vid_wipe               ; are .text in KERNEL_SEG so the call is near,
                                ; the four registers it clobbers - AX, CX, DI
                                ; and ES - are all pushed at this routine's
                                ; own entry, and zero is the right fill
    pop es
    pop di
    pop cx
%endif
.fill:
    mov al, FSXR_SIZE           ; DS:SI = the id's row, ES:DI = the buffer;
    mul ch                      ; the row IS bytes 0..13 of the block
    add ax, fsx_mtab
    xchg si, ax
    push cx                     ; CH is the id and rep movsw eats CX whole
                                ; (no `cld`: DF = 0 on entry, SPEC.md 1, and
                                ; every call above is the kernel's or an
                                ; int 10h, whose iret puts DF back - kernel
                                ; size pass 17)
    mov cx, FSXR_SIZE / 2
    rep movsw
    pop cx
    mov al, ch                  ; FSI_MODE and FSI_RSVD, ONE word: the id,
    cbw                         ; and 0 above it (ids are < 128), stored
    stosw                       ; where the row ENDED. AX is free, restored
                                ; at kret_di - and so is DI, which is why it
                                ; is not banked: from here it is the END of
                                ; the block and the fixups below address it
                                ; backwards (FSI_x - FSI_SIZE)
%if FSI_RSVD != FSI_MODE + 1 || FSI_SIZE != FSI_MODE + 2
  %error "fsx_mode stosw's FSI_MODE and FSI_RSVD as one word and leaves DI at FSI_SIZE"
%endif
    ; the three per-adapter fixups (SPEC.md 53.4)
    cmp ch, FSXM_HERC
    jne .f1
    mov ax, [vid_seg]           ; NOT a constant: a HERCSEG= test build
    mov [es:di+FSI_SEG-FSI_SIZE], ax ; (SPEC.md 39.9) flows through to the app,
    jmp short .f3               ; or hercshot could never see an fsx frame
.f1:
    or ch, ch                   ; FSXM_TEXT80 is 0, so the compare is a test
    jne .f3
    mov al, [vid_kind]          ; read ONCE, accumulator form - AL is spent
    cmp al, VID_HERC            ; here (the mul's product went into SI four
    jne .f2                     ; instructions above) and the caller's AX is
                                ; on the stack - and so is AH, below
%ifdef KERN_MEGA
    VSEG ax, 0xB000             ; kern_mega: the row holds B800h's SELECTOR
    mov [es:di+FSI_SEG-FSI_SIZE], ax ; after the switch, so B000h's whole
%else                           ; word (KERN-MEGA-MEMORY-PLAN MC7.4)
    mov byte [es:di+FSI_SEG+1-FSI_SIZE], 0xB0 ; B800h -> B000h: the
                                ; FSXM_TEXT80 row's segment has a zero low
                                ; byte, so its HIGH byte is the whole edit
%endif
    mov ah, 1                   ; the PAGE COUNT through AH, one register store
    jmp short .fp               ; for both arms where each carried a five-byte
.f2:                            ; immediate one (kernel size pass 18, -2)
    cmp al, VID_CGA
    jne .f3
    mov ah, 4
.fp:
    mov [es:di+FSI_PAGES-FSI_SIZE], ah
.f3:
    mov [fsx_cur], ch
    clc
.out:
    jmp kret_di

; -----------------------------------------------------------------------------
; fsx_surf - the rect this bracket owns (SPEC.md 53.7.1, slot 0x0304)
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
    call fsx_mine               ; a bracket, and ours - and it preserves AX,
    stc                         ; so the bank the refusal path needed is gone
    jne .out                    ; (stc writes CF alone: ZF is still fsx_mine's)
%ifdef VID_EXT
    cmp byte [vid_kind], VID_VESA   ; the DESKTOP's own surface on a VESA
    jne .surf                   ; display is packed pixels (SPEC.md 120.5), and
    cmp byte [fsx_cur], 0xFF    ; a same-mode bracket's caller writes planar
    stc                         ; VGA: refused, CF = 1, unless a foreign mode
    je .out                     ; is up - then the surface is that mode's
.surf:
%endif
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
    clc
.out:
    pop di
    ret

; -----------------------------------------------------------------------------
; fsx_setbios - int 10h AH=00h with everything banked. A mode set is rare
; and some BIOSes scribble more than AX, so the insurance is cheap where it
; matters and paid nowhere hot.
; in: AL = BIOS mode number
; -----------------------------------------------------------------------------
fsx_setbios:
    call kent_bp                ; = push ax/bx/cx/dx/si/di/bp, banked only so
                                ; the exit is kret_es's: AX comes back too,
                                ; which nobody reads
    push es
    call vid_cga_equip          ; THE THIRD SITE OF SPEC.md 39.19.4's defect,
                                ; and the same fix: this routine is reached
                                ; only with a COLOUR [vid_kind] (fsx_capstab
                                ; gives a Hercules display ids 0 and 4 alone,
                                ; and both branch away above the xlatb), so on
                                ; a MONO-primary machine with the bracket on a
                                ; colour second display 40:10 still says mono
                                ; and the ROM would force mode 7 and the 3B4h
                                ; CRTC - retiming the PRIMARY over its own
                                ; graphics framebuffer and leaving this card
                                ; untouched. TANK's FSXM_CGA320 (SPEC.md 85.3)
                                ; is the shipped consumer; vid_fsx_leave puts
                                ; the flag back (SPEC.md 39.19.4.1)
    xor ah, ah
    int 0x10
    jmp kret_es
%ifdef GFX_VGA

; -----------------------------------------------------------------------------
; fsx_modex - Mode X: 13h unchained and retimed to 320x240 (SPEC.md 53.4)
;
; The canonical sequence: chain4 off through a sequencer sync reset, the
; 25MHz dot clock and 480-scan polarity on the misc output, the CRTC
; unprotected and retimed (240 doubled lines), dword mode off, byte mode
; on, and all four planes cleared through map mask 0Fh - the BIOS cleared
; only the 64KB the chained mode could see.
; -----------------------------------------------------------------------------
; (THE CODE IS fsx_mode's .modex since size pass 9, its one caller. fsx_mode
; restores AX, DX and SI at kret_di; CX (the id in CH), DI and ES (the
; caller's buffer) are banked around it because .fill reads them.)

fsx_xcrtc:                      ; Mode X CRTC retiming, index in the low byte
    dw 0x0E11                   ; CR11 bit 7 clear: CR0-7 writable
    dw 0x0D06, 0x3E07, 0x4109, 0xEA10, 0xAC11
    dw 0xDF12, 0x0014, 0xE715, 0x0616, 0xE317
%endif                          ; ...end of Mode X, VGA-only by definition

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
    call fsx_mine               ; a bracket, and ours (SPEC.md 53.1)
    jne .fail
    cmp al, FSXW_FRAME          ; the argument is checked BEFORE the present,
    ja .fail                    ; so an unknown clock never flushes and then
    je .frame                   ; refuses (SPEC.md 53.1's all-or-nothing).
                                ; ONE compare: `ja` reads flags and writes
                                ; none, so ZF is still the compare's here
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
%ifdef KERN_BIG
    cmp word [spk_seg], 0       ; ...EXCEPT under a sample ISR (SPEC.md
    je .fy                      ; 34.11): a yield spends its switch at IF = 0,
.fh:                            ; and a loop of them loses the samples that
    hlt                         ; land there. hlt waits with IF = 1, and the
    jmp short .fc               ; rate period's own entry still switches
.fy:
 %ifdef KERN_MEGA
    cmp byte [fsx_rmode], 0     ; ...and in a REAL-MODE bracket (SPEC.md
    jne .fh                     ; 53.2.3), which has no scheduler to yield to:
 %endif                         ; fsx_rtick counts [sch_subs] all the same
%endif
    call task_yield             ; so the time we do not use is the worker's
.fc:
    cmp bx, [sch_subs]
    je .fw
    jmp short .ok
.vsync:
    mov dx, 0x3DA               ; the port by the CURRENT mode: on a Hercules
    cmp byte [vid_kind], VID_HERC ; machine every settable mode is the 3BAh
    jne .pa                     ; family, on VGA/CGA every one is 3DAh - so
    mov dl, 0xBA                ; the adapter kind IS the mode's family (DH
                                ; is 3 from the move above)
.pa:
    mov bx, [ticks]             ; phase A: wait until OUT of retrace, so a
.paw:                           ; call landing mid-retrace syncs to the NEXT
    call fsx_insync             ; one. fsx_insync owns the timeout (its
    jz .ok                      ; header): ZF = 1 is a dead port, timed out,
    jnc .paw                    ; return anyway. CF = 0 is IN retrace
.pb:                            ; phase B: wait until retrace begins - and a
    call fsx_insync             ; timeout here is ZF = 1 with CF = 0, which
    jc .pb                      ; falls out of this loop by itself
.ok:
    CLC_OR_STC .fail            ; CF = 0 waited, or (.fail) CF = 1 refused
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
    call fsx_mine               ; a bracket, and ours (SPEC.md 53.1)
    jne fsx_wait.fail
    mov ah, [fsx_cur]
    cmp ah, FSXM_HERC
    je .herc
    cmp ah, FSXM_MODEX
    jne fsx_wait.fail
    cmp al, 3                       ; Mode X: three pages of 19,200 bytes,
    jae fsx_wait.fail                       ; and 3 x 19,200 still fits a segment
%if 19200 % 256 || 2 * (19200 / 256) > 255
  %error "fsx_page writes Mode X's start address as an 8-bit page product in the HIGH byte and 0 in the low"
%endif
    mov ah, 19200 / 256             ; 19,200 is 4B00h: the start address's
    mul ah                          ; LOW byte is 0 on every page, so the high
    mov ah, al                      ; one is the page x 4Bh, an 8-bit product
    mov al, 0x0C                    ; (at most 96h) - one byte mul, no DX
    mov dx, 0x3D4
    out dx, ax                      ; start address high...
    mov ax, 0x000D                  ; ...and low, always 0
    out dx, ax
    jmp short fsx_wait.vsync
.herc:
    cmp al, 2
    jae fsx_wait.fail
    mov bx, 0x030A                  ; graphics + video, both lone-Hercules pages
%ifdef KERN_BIG
    call vid_dual_ok                ; **AND THE SECOND PAGE IS REFUSED ON A
    jc .single                      ; (AX survives it, vid_dual_ok's contract)
    or al, al
    jnz fsx_wait.fail               ; MACHINE THAT HAS A COLOUR CARD TOO**
    mov bh, 1                       ; dual-card page 0 must not decode B8000h,
.single:                            ; and AL = 0 takes the `jz .h0` below
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
    or al, al
    jz .h0
    or bl, 0x80                     ; ...and page 1, whose decode BH = 3
.h0:                                ; already allows (kernel size pass 13, -6)
    mov dx, 0x3BF                   ; ALLOW the second page: bit 0 graphics,
    mov al, bh                      ; retain both pages on a lone Hercules
    out dx, al                      ; dual-card page 0 retains only its own RAM
    mov dl, 0xB8                    ; and is the state the desktop comes back
    mov al, bl                      ; to (3B8h: DH is still 3)
    out dx, al
                                    ; the latch happens at the retrace, so the
    jmp short fsx_wait.vsync        ; page is only really shown after one -
                                    ; and fsx_wait's own timeout means a dead
                                    ; status port cannot hang this. A TAIL
                                    ; JUMP into its retrace wait: the frame is
                                    ; ours to the word (AX, BX, DX pushed in
                                    ; that order), its bracket test is the one
                                    ; passed above, and its exit is CF = 0 and
                                    ; the same three pops. So are the
                                    ; refusals', at fsx_wait.fail

%if (fsx_task - sch_cur) != 1
  %error "fsx_mine reads [sch_cur] and [fsx_task] as ONE word: they must stay adjacent in sched.inc's .bss, sch_cur first"
%endif
; -----------------------------------------------------------------------------
; fsx_mine - is a bracket up, and is it THIS task's? (SPEC.md 53.1)
;
; in:       nothing
; out:      ZF = 1 iff a bracket is armed and [sch_cur] owns it
; clobbers: flags. AX IS PRESERVED, and that is a difference from the four
;           guards this replaces: each of them left AH = [fsx_task] behind.
;           AH is dead at all four sites, checked one by one - fsx_mode
;           overwrites it with its only `mul` (AX = the product), fsx_surf
;           did `pop ax / xor ax,ax`, fsx_wait never reads AH, and fsx_page
;           reloads it with [fsx_cur]. AL is the argument at three of them and
;           survives either way.
;
; IT REFUSES ON THE RANGE, not on 0xFF. The four copies this replaces compared
; [fsx_task] against 0xFF and then against [sch_cur], and collapsing that to
; the equality alone rests on [sch_cur] never being 0xFF - true today, and
; exactly the kind of thing a later change breaks in silence, because
; 0xFF-as-none is this kernel's house idiom (sch_idleslot, fsx_task,
; fsx_worker, T_INST all use it). If a "nothing running" sentinel ever landed
; in sch_cur, the equality would MATCH with no bracket up and fsx_mode would
; set a video mode for nobody. Refusing anything that is not a slot costs six
; bytes and is immune to any sentinel, in either byte.
; -----------------------------------------------------------------------------
fsx_mine:
    push ax
    mov ax, [sch_cur]           ; AL = sch_cur, AH = fsx_task - adjacent, see
    cmp ah, MAX_TASKS           ; the %if above
    jae .no                     ; not a slot: no bracket, whatever the value
    cmp al, ah
    pop ax                      ; pop leaves the flags alone
    ret
.no:
    or ah, ah                   ; AH >= MAX_TASKS, so AH != 0 and ZF = 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; fsx_insync - one poll of fsx_wait's retrace spin, timeout included
;
; in:       DX = the status port, BX = the [ticks] the wait began at
; out:      ZF = 1 (and CF = 0): three ticks have passed - a dead port, and
;           the wait returns anyway (SPEC.md 37.90's rule). Otherwise ZF = 0
;           and CF = 1 when the adapter is NOT in vertical retrace, CF = 0
;           when it is.
; clobbers: AX - both callers are fsx_wait's .paw and .pb, which bank it
;
; VGA/CGA (3DAh): bit 3 set. Hercules (3BAh): bit 7 LOW while vertical sync
; is active - the polarity is inverted there.
;
; THE TIMEOUT LIVES HERE (size pass 10, -9 bytes): both phases spelled the
; same three-instruction [ticks] delta out, phase A after its poll and phase
; B before it. Checking before every read changes no outcome - a timed-out
; phase A went to phase B, whose first act was that same check - and the
; poll lost its push/pop of AX, ~27 clocks off every iteration.
; -----------------------------------------------------------------------------
fsx_insync:
    mov ax, [ticks]
    sub ax, bx
    cmp ax, 3
    jae .tmo
    in al, dx
    mov ah, 8                   ; VGA/CGA (3DAh): bit 3, active HIGH
    cmp dl, 0xBA
    jne .go
    mov ah, 0x80                ; Hercules (3BAh): bit 7, active LOW - so
    not al                      ; invert the byte and ONE test serves both
.go:
    and al, ah                  ; 0, or the bit alone (8 or 80h)
    sub al, 1                   ; CF = "that bit was CLEAR", and ZF = 0
    ret                         ; either way: 0 - 1 is FFh, the bit less one
                                ; is 7 or 7Fh. The callers read both flags
.tmo:
    xor ax, ax                  ; ZF = 1, CF = 0
    ret                         ; (The retrace test without the timeout:
                                ; ~164 -> ~142 clocks on the VGA in-retrace
                                ; path at size pass 6, the `in` at 41.5
                                ; dominating.)
                                ; SPEC.md 53.5.1 measured fsx_insync plus
                                ; fsx_wait at 28.8% of a 4.77 MHz 8088 with
                                ; Tracker pacing on it

; --- the mode rows: bytes 0..13 of the FSI block, verbatim (SPEC.md 53.4) ----
FSXR_SIZE equ 14
%if FSXR_SIZE != FSI_MODE
  %error "fsx_mode's rep movsw copies a row of FSXR_SIZE bytes and its stosw lands on FSI_MODE: they must be equal"
%endif

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

fsx_bmodes:                     ; the plain-BIOS rows' mode numbers, INDEXED
    db 0x01, 0x04, 0x06        ; FROM 1 (see the `- 1` at the xlatb): id 0
    db 0x00, 0x0D, 0x13, 0x12  ; dispatches to .text before it and never read
    db 0x13                    ; its row. Id 4's hole stays, because the index
                                ; has to stay linear; and SO DOES ID 8'S ROW,
                                ; deliberately - it is dead only while the
                                ; %ifdef GFX_VGA dispatch above catches Mode X,
                                ; which a kern_small build compiles out, and
                                ; the argument that it still cannot arrive
                                ; there (fsx_capstab's VGA row is unreachable
                                ; because [vid_kind] can never be VID_VGA) is
                                ; sound but crosses two files. One byte is not
                                ; worth making a wrong BIOS mode the failure

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

%ifdef KERN_MEGA
; =============================================================================
; THE REAL-MODE BRACKET (SPEC.md 53.2.3, docs/plans/MEGA-VIDEO-REALMODE-PLAN.md
; W1): FSXF_REAL on kern_mega in protected mode. fsx_run's door takes the
; claim (fsx_rprepf), the freeze's arm copies the region down (fsx_rcopyf),
; and its entry call is fsx_rcall - out of protected mode once, the package's
; proc in real mode with fsx_rtick on IRQ0, and back once.
;
; WHERE IT LIVES, and why it is not a module (the plan's W1 decision): every
; instruction real mode runs is here in `.text`, the one part of kern_mega
; that is conventional memory in both modes - `.cold`, `.mega`, the drivers
; and the modules are above 1MB once the switch is done. The protected-mode
; halves are `.mega`'s. A module read at every bracket would be a disk read
; from the SYSTEM disk at every Play, which on a one-floppy machine is a swap
; (ONDEMAND-PLAN 1's test fails), and W1's real-mode half is smaller than a
; module's own row, thunks and load arm. W2's API table is the part that
; might want one; it is W2's to decide.
; =============================================================================

fsx_rdisp   dw PKG_DISP         ; THE ENTRY's far pointer: the dispatcher of
fsx_rseg    dw 0                ; ...the REAL paragraph the region runs at -
                                ; its copy, or its own when conventional.
                                ; fsx_rtick calls the hook through it too
fsx_rwin    dw 0                ; the window (SI) and the entry (BP), banked
fsx_rent    dw 0                ; across the drop: a 286's reset keeps no
                                ; register
fsx_rmode   db 0                ; 1 = this bracket is a real-mode one. `.text`
                                ; and not `.bss`: it is read on every bracket
                                ; and must rest at 0 (SPEC.md 12.8.5.1)
fsx_rwhy    db 0xFF             ; ...FSXR_*, the door's answer to FSXF_REAL,
                                ; for the caller's SI at the foot; FFh at rest
                                ; (not asked, or a kernel in real mode)

; fsx_rgone - mod_gone's answer, in REAL mode: EXTD.DRV's entry slots while a
; real-mode bracket runs. Unloaded, every slot names `.cold`'s mod_gone through
; a protected-mode selector, and fsx_mode, wm_content and fsx_caps far-call
; one whatever the machine (EXTCALL EXT_FSX: CF = 1 is the one-display
; answer). In real mode that selector is a wild segment, so fsx_rdownf points
; the block here and fsx_rupf puts it back (MEGA-VIDEO-REALMODE-PLAN W1)
fsx_rgone:
    stc
    retf

; -----------------------------------------------------------------------------
; fsx_rcall - wm_pkgcall's call, in real mode when [fsx_rmode] = 1 (and
;      wm_pkgcall itself, jumped to, when it is 0)
; in:  PM, SI = the window, BP = the entry, DS = KDATA; the bracket armed and
;      [fsx_rmode] = 1 (the claim taken, the region copied down)
; out: the entry proc has run; protected mode again, every word, mask and
;      vector back, the region copied back up and the claim freed. DS and ES
;      and FLAGS as they came; every other register may have changed, as
;      across wm_pkgcall
; -----------------------------------------------------------------------------
cpu 286
fsx_rcall:
    cmp byte [fsx_rmode], 0     ; a protected-mode bracket: wm_pkgcall's own
    jne .real                   ; call, tail-jumped (kernel size pass 16:
    jmp wm_pkgcall              ; the test was fsx_run's)
.real:
    pushf
    push ds
    push es
    cli                         ; ...to the entry proc's own sti below
    call MEGA_SEL_CODE:fsx_rdownf ; the masks, IVT 20h, the words real
                                ; mode reads, SI and BP banked, NMI off and
                                ; [mega_drop] = 2 (kernel size pass 16: those
                                ; were here, and are protected-mode stores)
    push ds
    pop es                      ; ES writable across the switch (a 386 keeps
                                ; a segment's ATTRIBUTES: mega_svc_lo's note)
    jmp mega_down               ; (CR0 on a 386, the reset on a 286)
fsx_rland:                      ; REAL MODE, IF = 0, the stack as it was
    ; STKBALANCE-OK: mega_down's landing, never called - reached by a jump from mega_rland, after mega_r286 put a 286's stack back from [mega_rss]; it ends at fsx_rcall's own ret, on the frame fsx_rcall built before the drop
    mov ax, KERNEL_SEG
    mov ds, ax
    mov es, ax
    mov byte [mega_drop], 0     ; the next reset is a reflection's again
    mov [k_dsel], ax            ; KDS in real mode: the mouse ISR's, the cells'
    mov ax, ss                  ; LOW_SEG: the selector IS the segment
    mov ss, ax
    lidt [mega_rmidtr]
    mov al, 0x0D                ; NMI on (index 0Dh, read-only), once the IVT
    out 0x70, al                ; is the table again
    mov si, [fsx_rwin]
    mov bp, [fsx_rent]
    push ds
    mov ds, [fsx_rseg]          ; DS = CS = the real paragraph, ES = KERNEL_SEG,
    sti                         ; SI = the window: wm_pkgcall's environment
    call far [cs:fsx_rdisp]
    cli
    pop ds
    call mega_a20               ; A20 PROVED OPEN before the GDT is read: a
                                ; BIOS the package called may have shut it
    call mega_up                ; protected mode: DS = ES = KDATA
    call sch_fast_off           ; the rate OFF first: no hook may run in
                                ; protected mode on the window's copy, which
                                ; is stale until the copy back (fsx_restore
                                ; calls it again, a no-op then)
    call MEGA_SEL_CODE:fsx_rupf ; ...and everything fsx_rdownf did, undone
    pop es
    pop ds
    popf
    ret

; -----------------------------------------------------------------------------
; fsx_rtick - IRQ0 in the real-mode bracket (IVT 20h, the 8259 at its
; protected-mode base): sch_isr's three arms with no scheduler. Every entry
; counts [sch_subs]; a TICK chains the ROM's own handler (40:6C, the floppy
; motor, the EOI) on sch_chstack as sch_isr does (SPEC.md 8.5, 8.5.1) and
; bumps [ticks]; a period that is no tick sends its own EOI; under FSXF_RATE
; the hook runs after either, under sch_rhook's skip rules - the previous call
; still out, the ROM's chain under us, or SS not LOW_SEG (a ROM service's
; private stack) - at IF = 1 under the speaker's sample ISR, as sch_rhook. A
; tick runs SOUND.DRV's DSV_TICK through its low copy (W4: rms_stick, which
; answers at once when the list carried no sound driver); snd_tick's tone
; expiry does not run.
; -----------------------------------------------------------------------------
fsx_rtick:
    pusha
    push ds
    push es
    mov ax, KERNEL_SEG
    mov ds, ax
    inc word [sch_subs]         ; fsx_wait's frame clock (SPEC.md 53.5.1)
    mov al, [sch_fast]
    cmp al, SCH_RATE
    je .rate
    or al, al
    jz .tick                    ; no sub-tick: every entry is a tick
    dec byte [sch_fcnt]         ; FSXF_FASTTICK: every Nth
    jnz .eoi
    mov [sch_fcnt], al
.tick:
%ifndef NO_CHAINPRIV
    mov bx, sp                  ; THE ROM's CHAIN ON sch_chstack, as sch_isr's
    cmp byte [sch_chbusy], 0    ; (SPEC.md 8.5): SS is LOW_SEG here too, so the
    jne .tskip                  ; swap is SP alone. Nested (8.5.1) - a sample
    mov ax, ss                  ; ISR chaining us inside the ROM's sti - it
    cmp ax, LOW_SEG             ; skips the chain and the driver's tick, which
    jne .tplain                 ; do not fit there, and the hook refuses on
    inc byte [sch_chbusy]       ; the same flag. A ROM service's own stack
    mov sp, sch_chstack + SCH_CHSTK ; chains in place. The task's SP is in BX
.tplain:                        ; (an ISR, the ROM's keeps every register):
%endif                          ; one call for both arms, and on the plain
    pushf                       ; one the restore and the clear below are
    call far [mega_old08]       ; no-ops - [sch_chbusy] was 0 there (kernel
%ifndef NO_CHAINPRIV            ; size pass 16, -9)
    mov sp, bx
    mov byte [sch_chbusy], 0
%endif
    inc word [ticks]
    mov ax, LOW_SEG             ; the stream's watchdog (W4): fsx_rstko is
    mov es, ax                  ; the service area's rms_stick, beside
    call far [es:fsx_rstko]     ; [fsx_rmsseg], its paragraph. DS kept
    cmp byte [sch_fast], SCH_RATE
    je .hook                    ; a rate entry that carried is a period too
    jmp short .out
.rate:
    inc word [sch_rpend]        ; a period, owed to the hook
    mov ax, [sch_rdiv]
    add [sch_racc], ax          ; the accumulator's carry is a TICK, so
    jc .tick                    ; [ticks] and 40:6C stay exact (53.2.2)
    mov al, 0x20
    out 0x20, al
.hook:
    cmp word [spk_seg], 0       ; UNDER A SAMPLE ISR the hook runs at IF = 1
    je .if0                     ; (sch_rhook's rule): the next IRQ0 is a
    sti                         ; sample, and one held here is lost (W4)
.if0:
%ifndef NO_CHAINPRIV
    cmp word [sch_chbusy], 0    ; the last call is still out (it sti'd), or
%else                           ; the ROM's chain is under us on sch_chstack
    cmp byte [sch_rbusy], 0     ; ([sch_chbusy] and [sch_rbusy] as ONE word,
%endif                          ; sch_rhook's test and its %error)
    jne .out
    mov ax, ss
    cmp ax, LOW_SEG             ; a ROM service on a stack of its own
    jne .out
    inc byte [sch_rbusy]
    xor ax, ax
    xchg ax, [sch_rpend]        ; AX = the periods owed, and none after
    mov si, [fsx_rwin]
    mov bp, [sch_rbp]
    push ds
    pop es                      ; ES = KERNEL_SEG
    push ds
    mov ds, [fsx_rseg]
    call far [cs:fsx_rdisp]
    pop ds
    cli                         ; the hook may have sti'd
    dec byte [sch_rbusy]
    jmp short .out
%ifndef NO_CHAINPRIV
.tskip:                         ; a NESTED tick: no chain, no driver tick
    inc word [sch_chskip]
    inc word [ticks]            ; ...and falls into its own EOI
%endif
.eoi:
    mov al, 0x20
    out 0x20, al
.out:
    pop es
    pop ds
    popa
    iret
cpu 8086

section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
cpu 286
; -----------------------------------------------------------------------------
; fsx_rprepf - FAR, fsx_run's door: the real-mode bracket's REASONS and its
; claims. in: PM, DS = KDATA, DI = the instance record, SI = the flags as
; asked, and fsx_run's frame (the caller's DS:BP is the FSXL_ list, BP = 0
; none: SPEC.md 53.2.3). out: [fsx_rwhy] =
; FSXR_REAL, [fsx_rmode] = 1, [fsx_rseg] the paragraph the entry will run at
; and - when the region is above 1MB - a conventional claim for its copy, both
; linears and the length banked; the SERVICE AREA (W3) claimed and filled -
; the twins' code, the list's extent rows and runs - and each low driver's
; claim; or [fsx_rwhy] = the first FSXR_* reason that holds and NOTHING
; taken: the bracket goes on in protected mode (SPEC.md 53.2.3 - a fallback,
; never a refusal, for a state the user is in). ONE TEST A REASON, so lifting
; one (MEGA-VIDEO-REALMODE-PLAN's owed list) deletes its lines and nothing
; else. Every register kept
; -----------------------------------------------------------------------------
fsx_rprepf:
    test si, FSXF_REAL          ; not asked: nothing (the test was fsx_run's,
    jnz .ask                    ; in `.text`: kernel size pass 16)
    retf
.ask:
    push ax                     ; AL = the reason, as each test is reached
    mov al, FSXR_KEEP
    test si, FSXF_KEEPWORKER    ; one task runs in real mode: nothing to keep
    jnz .out
                                ; (FSXR_SVC, FSXR_EXTD and FSXR_SERIAL were
                                ; tests here until W7: a driver's worker is
                                ; parked, EXTD.DRV runs from a low copy and a
                                ; lease is serviced - fsx_rextf, fsx_rserdn)
    pusha
    xor ax, ax                  ; (`.lowbss` is not zeroed: .no reads these)
    mov [ss:fsx_rlow], ax
    mov [ss:fsx_rsvc], ax
    call fsx_rmvck              ; AL = 0: the caller's movable claims' bases,
                                ; before the first claim the door makes
    mov bp, sp                  ; THE LIST (W3), the caller's DS:BP - under
    mov es, [bp+38]             ; pusha, our AX and far return, kent_di's six,
    mov bp, [bp+40]             ; the X stub's return and its ES - walked in
    or bp, bp                   ; the caller's own folder into the park's
    jz .nl                      ; scratch, FSXR_VOL, _FRAG or _DRV in AL. No
    push bp                     ; list, no remount: the machine stays where
    mov bp, inst_vol_enter      ; it stands
    call KERNEL_SEG:mega_tcall
    pop bp
.nl:
    call fsx_rlist
    jc .why
    popa                        ; (AL the reason again, for every exit below)
    mov al, FSXR_ROOM
    pusha
    mov bx, di
    sub bx, inst_tab
    mov cl, 5
    shr bx, cl                  ; BX = the slot: every claim is the
    mov [ss:fsx_rown], bx       ; instance's, so a teardown frees what an exit
                                ; did not
    mov ax, [di+I_SPTR]
    call mega_selbase           ; DX:AX = the region's base
    jc .no
    cmp dx, 0x10
    jb .para                    ; conventional: it runs where it is
    mov [ss:fsx_rhi], ax        ; ABOVE 1MB (OS88_HIGH): a copy, low
    mov [ss:fsx_rhi+2], dx
    mov ax, [di+I_SIZE]         ; image + bss, FFFFh for a whole 64KB
    mov cx, ax
    inc cx
    and cx, -2                  ; the copy moves words (mega_imghif's sum): CX,
                                ; not CL - a region of 7FFh bytes made CL 0
                                ; and the copy 64KB, over the next claim
    jnz .len
    mov cx, 0xFFFE              ; a 64KB region: mega_lincopy's ceiling
.len:
    mov [ss:fsx_rlen], cx
    call fsx_rlowclaim          ; AX bytes -> DX = a conventional claim's
    jc .no                      ; selector, AX:CX its base
    mov [ss:fsx_rlow], dx
    mov [ss:fsx_rlo], cx
    mov [ss:fsx_rlo+2], ax
    xchg ax, dx
    xchg ax, cx                 ; DX:AX = the copy's base
.para:
    shr ax, 4                   ; DX:AX (< 1MB, paragraph-aligned) -> the
    shl dx, 12                  ; paragraph
    or ax, dx
    mov [fsx_rseg], ax
    call fsx_rsvcf              ; THE SERVICE AREA (W3), FSXR_ROOM's too
    jc .no
    call fsx_rdrvf              ; ...and the drivers' low copies: AL =
    jc .why                     ; FSXR_DROOM or (W4) FSXR_DRV
    call fsx_rextf              ; ...and EXTD.DRV's (W7): AL = FSXR_DROOM
    jc .why
    mov al, 1                   ; ...and NOTHING OF THE CALLER'S MOVED under
    call fsx_rmvck              ; them: os88real_prep banked its real
    mov al, FSXR_ROOM           ; segments at the door, and no proc is told
    jc .why                     ; of a protected-mode move (53.2.3)
    mov byte [fsx_rmode], 1
    popa
    mov al, FSXR_REAL
    jmp short .out
.why:
    mov bp, sp
    mov [bp+14], al             ; the reason into pusha's AX
.no:
    call fsx_rfree              ; whatever was taken goes back
    popa
.out:
    mov [fsx_rwhy], al
    pop ax
    retf

; fsx_rmvck - THE BANK'S GUARD (SPEC.md 53.2.3): the base of every MOVABLE
; claim the instance DI owns, in mem_tab's order, into the park's scratch at
; FSX_RMV (AL = 0, its slot into [fsx_rown]), or compared with it (AL = 1,
; that slot's: CF = 1 one moved, went or came - or there were more than the
; snapshot holds, which cannot be vouched for and so counts as a move). The
; door's own claims may compact, and a package's os88real_prep bank is a
; snapshot of real segments no protected-mode move tells anyone about - so a
; move falls the bracket back to protected mode rather than handing real mode
; a stale segment. Every register kept
FSX_RMV     equ FSX_RSV + 4         ; dw 2 x the count; the bases after it
FSX_RMVN    equ (MPS_SIZE - FSX_RMV - 2) / 2 ; ...as many as the scratch holds
%if FSX_RMVN < 8
 %error "fsx_rmvck's snapshot is the park's scratch past the list: MPS_SIZE leaves it room for fewer than 8 claims"
%endif
fsx_rmvck:
    pusha
    push es
    mov bx, [ss:fsx_rown]       ; AL = 1: the slot the snapshot banked
    or al, al
    jnz .sl
    mov bx, di                  ; AL = 0: DI's slot (fsx_rprepf's own
    sub bx, inst_tab            ; derivation, which stores it there again)
    mov cl, 5
    shr bx, cl
    mov [ss:fsx_rown], bx
.sl:
    mov dx, MEGA_SEL_PSCR
    mov es, dx
    mov di, FSX_RMV + 2
    mov si, mem_tab
    mov cx, MEM_MAX
.l:
    cmp [ss:si+MC_OWN], bx      ; the caller's...
    jne .n
    cmp word [ss:si+MC_RLOC], 0 ; ...and movable (a pinned one cannot move)
    je .n
    cmp di, FSX_RMV + 2 + 2 * FSX_RMVN
    jae .bad
    mov dx, [ss:si+MC_SEG]
    or al, al
    jnz .c
    mov [es:di], dx
    jmp short .a
.c:
    cmp [es:di], dx
    jne .bad
.a:
    inc di
    inc di
.n:
    add si, MC_SIZE
    loop .l
    sub di, FSX_RMV + 2         ; DI = 2 x the count
    or al, al
    jnz .cc
    mov [es:FSX_RMV], di
    jmp short .o                ; (CF = 0 from the sub)
.cc:
    cmp [es:FSX_RMV], di
    je .o                       ; (CF = 0: equal)
.bad:
    or al, al                   ; a snapshot that overflowed is one the
    jnz .st                     ; compare must fail: an odd count is none
    mov word [es:FSX_RMV], 1    ; it can reach
.st:
    stc
.o:
    pop es
    popa
    ret

; fsx_rlowclaim - AX = bytes -> CF = 0 and DX = a conventional claim of that
; many (whole KB) owned by [ss:fsx_rown], AX:CX = its linear base (below
; 1MB); CF = 1 none, nothing taken. Clobbers AX, CX, DX
fsx_rlowclaim:
    push bx
    add ax, 1023                ; ...in whole KB, the claim's unit
    mov al, ah
    rcr al, 1                   ; (the add's carry is the 64th KB)
    shr al, 1
    xor ah, ah
    mov bx, [ss:fsx_rown]
    call COLD_SEG:mmf_mem_claim ; DX = its data selector
    jc .o
    mov ax, dx
    push dx
    call mega_selbase           ; DX:AX = its base
    mov cx, ax
    xchg ax, dx                 ; AX:CX
    pop dx
    jc .fr
    cmp ax, 0x10
    cmc
    jnc .o                      ; below 1MB: the answer
.fr:
    mov bx, [ss:fsx_rown]       ; (a base it cannot use: back)
    call COLD_SEG:mmf_mem_free
    stc
.o:
    pop bx
    ret

; fsx_rfree - every claim fsx_rprepf may have taken - the region's copy, the
; service area, the drivers' copies (listed in the service area) - freed and
; forgotten. PM, DS = KDATA. Every register kept
fsx_rfree:
    pusha
    push es
    mov bx, [ss:fsx_rown]
    xor dx, dx
    xchg dx, [ss:fsx_rlow]
    call .f
    mov ax, [ss:fsx_rsvc]
    or ax, ax
    jz .nos
    mov es, ax
    mov si, rms_drv - rms_lo
    mov cx, RMD_N
.d:
    xor dx, dx
    xchg dx, [es:si+RMD_CL]
    call .f
    add si, RMD_SIZE
    loop .d
    xor dx, dx
    xchg dx, [ss:fsx_rsvc]
    call .f
.nos:
    pop es
    popa
    ret
.f:
    or dx, dx
    jz .fo
    call COLD_SEG:mmf_mem_free
.fo:
    ret

; -----------------------------------------------------------------------------
; fsx_rlist - THE LIST (SPEC.md 53.2.3, W3): ES:BP = the caller's FSXL_
; entries, BP = 0 none. Each FSXL_FILE's name goes into a stream row of the
; park's scratch (MEGA_SEL_PSCR, FSX_RSR + n x RMS_SSZ, upper-cased) and its
; file is walked into runs after FSX_RSN (fsx_rwalkf); each FSXL_DRV's class
; into the scratch's mask. out CF = 0, or CF = 1 and AL = FSXR_VOL / FSXR_FRAG
; / FSXR_DRV. PM, DS = KDATA. Clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
fsx_rlist:
    push es
    push es
    mov ax, MEGA_SEL_PSCR       ; the scratch's counters, zero
    mov ds, ax
    xor ax, ax
    mov [FSX_RSV+FSV_NSTR], ax  ; (the stream count and the class mask)
    mov [FSX_RSV+FSV_NRUN], ax
    pop ds                      ; DS:SI = the list
    cld
    mov si, bp
    or si, si
    jz .ok
.e:
    lodsb
    cmp al, FSXL_DRV
    je .drv
    cmp al, FSXL_FILE
    jne .ok                     ; FSXL_END, or anything else, ends it
    mov ax, MEGA_SEL_PSCR
    mov es, ax
    mov al, [es:FSX_RSV+FSV_NSTR]
    cmp al, FSXL_NMAX
    mov ah, FSXR_FRAG           ; (a row too many is the extent list's room)
    jae .no
    mov ah, RMS_SSZ
    mul ah
    add ax, FSX_RSR
    mov di, ax                  ; ES:DI = the row
    mov bx, di                  ; (its start, for the walk)
    mov cx, 12                  ; the name: 12 bytes at most, and a NUL
.n:
    lodsb
    cmp al, 'a'
    jb .u
    cmp al, 'z'
    ja .u
    sub al, 0x20                ; upper case, as the twin compares it
.u:
    stosb
    or al, al
    jz .nd
    loop .n
    mov byte [es:di], 0         ; (a 13th byte: NUL, and the rest skipped)
.sk:
    lodsb
    or al, al
    jnz .sk
.nd:
    push si                     ; the list, on past the name
    push ds
    push es
    pop ds                      ; DS:SI = the row's name ->
    mov si, bx
    mov ax, MEGA_SEL_KDATA
    mov es, ax
    mov di, api_name            ; ...KERNEL:api_name, for the stat
    mov cx, 13
    rep movsb
    mov ds, ax                  ; DS = KDATA
    mov di, bx
    call COLD_SEG:fsx_rwalkf    ; AL = 0, or the reason
    mov bx, MEGA_SEL_PSCR
    mov ds, bx
    inc byte [FSX_RSV+FSV_NSTR] ; (one row more: `inc` of memory, CF kept)
    pop ds
    pop si
    or al, al
    jz .e
    mov ah, al
    jmp short .no
.drv:
    lodsb                       ; the class
    mov ah, 1
    cmp al, DRVC_DISK
    je .dm
    mov ah, 2
    cmp al, DRVC_EMS
    je .dm
    mov ah, 4
    cmp al, DRVC_SOUND          ; (W4)
    je .dm
    mov ah, FSXR_DRV            ; any other: not carried
    jmp short .no
.dm:
    push ds
    mov bx, MEGA_SEL_PSCR
    mov ds, bx
    or [FSX_RSV+FSV_DMSK], ah
    pop ds
    jmp .e
.no:
    mov al, ah
    stc
    jmp short .x
.ok:
    clc
.x:
    mov bx, MEGA_SEL_KDATA      ; (mov leaves CF)
    mov ds, bx
    pop es
    ret

; -----------------------------------------------------------------------------
; fsx_rsvcf - THE SERVICE AREA (W3): a conventional claim holding the real-
; mode twins' code and data (rms_lo..rms_hi, copied out of `.mega`), then the
; scratch's stream rows and runs at rms_strm, and the paragraph that runs
; them in [ss:fsx_rmsseg] - the twins' offsets being `.mega`'s own, so
; CS x 16 = the claim's base - rms_lo. CF = 1: no room (FSXR_ROOM), nothing
; taken. PM, DS = KDATA. Clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
fsx_rsvcf:
    push es
    push bp
    mov ax, MEGA_SEL_PSCR
    mov es, ax
    mov ax, [es:FSX_RSV+FSV_NRUN]
    mov cx, RMR_SIZE
    mul cx
    add ax, rms_runs - rms_lo   ; the claim's bytes: code, rows, runs...
    push ax
    xor bp, bp
    call fsx_rxrows             ; ...and the bracket's pool rows (CX bytes)
    pop ax
    add ax, cx
    xor dx, dx
    cmp word [ser_row], 0xFFFF  ; ...and a SERIAL.DRV lease's ring after
    je .nser                    ; them (W7, fsx_rserdn), at an even offset
    push ds                     ; (the runs are words) - unless the lease is
    mov ds, [mod_tab + MOD_SER * MODR_SIZE + MODR_SEG] ; THIS package's: its
    mov bx, [SRL_O_SEG]         ; ring is in the region, which runs where
    pop ds                      ; [fsx_rseg] says, and a second copy would
    inc dx                      ; be overwritten by the region's at the
    cmp bx, [di+I_SPTR]         ; return. DX = 1 says so (no ring is at an
    je .nser                    ; odd offset)
    mov dx, ax
    add ax, SER_SIZE
.nser:
    mov [ss:fsx_rserb], dx
    xor dx, dx                  ; ...and THE GLYPH TABLE'S COPY last, when it
    push ax                     ; is above 1MB (MEM_K_FONT, KERN-MEGA-PLAN
    mov ax, [font_seg]          ; 14.120/14.131): no real segment reaches it
    call MEGA_SEL_CODE:mega_ishif ; (MEGA_ISHI less its real-mode test:
    pop ax                      ; `.mega` runs in protected mode alone)
    jc .nfnt
    inc ax                      ; there, so the bracket reads it here
    and al, 0xFE                ; (at an even offset: rep movsw)
    mov dx, ax
    add ax, FONT_BYTES
.nfnt:
    mov [ss:fsx_rfont], dx
    push ax
    call fsx_rlowclaim          ; DX = the selector, AX:CX its base
    pop si
    jc .o
    mov [ss:fsx_rsvc], dx
    sub cx, rms_lo              ; the paragraph CS x 16 = base - rms_lo
    sbb ax, 0                   ; (rms_lo is a multiple of 16 and far below
    shr cx, 4                   ; any heap base)
    shl ax, 12
    or cx, ax
    mov [ss:fsx_rmsseg], cx
    push ds
    mov es, dx
    xor di, di
    mov si, rms_lo              ; the code, out of `.mega` (its CS is a
    mov cx, rms_strm - rms_lo   ; READABLE code selector)
    mov ax, MEGA_SEL_CODE
    mov ds, ax
    cld
    rep movsb
    mov ax, MEGA_SEL_PSCR       ; ...the rows and the runs after it, and the
    mov ds, ax                  ; class mask and the stream count into its
    xor si, si                  ; data
    mov cx, [FSX_RSV+FSV_NRUN]
    mov ax, RMR_SIZE
    mul cx
    add ax, FSX_RSN
    xchg ax, cx
    rep movsb
    mov ax, [FSX_RSV+FSV_NSTR]  ; AL the count, AH the mask
    pop ds
    mov [es:rms_nstr - rms_lo], ax
    inc bp                      ; ...the pool rows after the runs, where DI
    call fsx_rxrows             ; is, for rms_xchk
    cmp word [xm_kb], 0         ; ...and OSAPI_XMEM_CAPS's answer, for
    je .nxc                     ; rms_xcaps
    call mx_caps
    mov [es:rms_xkb - rms_lo], ax
    mov [es:rms_xbl - rms_lo], bl
.nxc:
    mov al, [ss:fsx_rown]
    mov [es:rms_slot - rms_lo], al
    mov al, [cpu_tier]
    mov [es:rms_tier - rms_lo], al
    mov di, [ss:fsx_rfont]      ; the glyph table's copy, out of the pool
    or di, di
    jz .nf
    push ds
    mov si, [font_base]
    mov ds, [font_seg]
    mov cx, FONT_BYTES / 2
    rep movsw
    pop ds
.nf:
    clc
.o:
    pop bp
    pop es
    ret

; fsx_rxrows - THE BRACKET'S POOL ROWS (kern_mega size pass 2): mx_tab is in
; the tables' tail above 1MB, which real mode cannot read, so rms_xchk reads
; a copy of what it needs - {MX_AUX, MX_KB, MX_OFF} of every live entry the
; bracket's instance owns ([ss:fsx_rown]), in the table's order. BP = 0:
; CX = their bytes only; else ES:DI = where they go in the service area ->
; written there, rms_xrow and rms_xend aimed at them, DI past them. PM,
; DS = KDATA. Clobbers AX, BX, CX, SI
RMX_AUX     equ 0
RMX_KB      equ 2
RMX_OFF     equ 4
RMX_SIZE    equ 6
fsx_rxrows:
    or bp, bp
    jz .c
    lea ax, [di + rms_lo]       ; (the twins' offsets: CS x 16 is the
    mov [es:rms_xrow - rms_lo], ax ; claim's base less rms_lo)
.c:
    xor cx, cx
    mov bl, [ss:fsx_rown]
    push ds
    push word MEGA_SEL_MXT
    pop ds
    mov si, mx_tab
.l:
    cmp [si+MX_OWN], bl
    jne .n
    cmp word [si+MX_KB], 0
    je .n
    add cx, RMX_SIZE
    or bp, bp
    jz .n
    mov ax, [si+MX_AUX]
    stosw
    mov ax, [si+MX_KB]
    stosw
    mov ax, [si+MX_OFF]
    stosw
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .l
    pop ds
    or bp, bp
    jz .o
    lea ax, [di + rms_lo]
    mov [es:rms_xend - rms_lo], ax
.o:
    ret

; -----------------------------------------------------------------------------
; fsx_rdrvf - THE LOW DRIVERS (W3, W4): each class the list asked for (rms_dmsk:
; DRVC_DISK, DRVC_EMS, DRVC_SOUND) whose driver is loaded gets a row in the
; service area - its DSV_BLK and DSV_PKGCALL (SOUND: its DSV_TICK and
; DSV_STREAM too), and the REAL paragraph it runs at: its own when its image
; is conventional, else a conventional claim's, the image and bss copied down
; and back up (fsx_rcopyf; SOUND's at the drop and the return, fsx_rsndf).
; SOUND's paragraph must be a multiple of 20h - SBL_DSCS derives DS = CS
; only there - and its SNDV_RMODE words must each have a real segment.
; CF = 1: AL = FSXR_DROOM no room, FSXR_DRV a word with no segment. PM, DS =
; KDATA. Clobbers AX, BX, CX, DX, SI, DI, BP
; -----------------------------------------------------------------------------
fsx_rdrvf:
    push es
    mov es, [ss:fsx_rsvc]
    mov bp, rms_drv - rms_lo    ; ES:BP = the row
    mov bh, 1                   ; ...its class's mask bit
.c:
    mov bl, DRVC_DISK
    cmp bh, 1
    je .k
    mov bl, DRVC_EMS
    cmp bh, 2
    je .k
    mov bl, DRVC_SOUND
.k:
    test [es:rms_dmsk - rms_lo], bh
    jz .nx
    mov al, bl
    cbw
    dec ax
    shl ax, 2
    xchg ax, si
    mov dx, [drv_fptr+si+2]     ; the driver's data selector, 0 = not loaded
    or dx, dx
    jz .nx
    mov [es:bp+RMD_CLS], bl
    mov [es:bp+RMD_SEL], dx
    mov word [es:bp+RMD_DSP], PKG_DISP
    mov ax, [drv_fptr7]         ; DRVC_EMS: its door in drv_fptr7's offset
    cmp bl, DRVC_EMS            ; half, and no service table
    je .pk
    push dx
    mov al, bl
    call COLD_SEG:drvf_drv_cls_svc  ; DI = the class's table
    pop dx
    jc .nx
    mov ax, [di+DSV_BLK]
    mov [es:bp+RMD_BLK], ax
    mov ax, [di+DSV_TICK]       ; (a disk driver's are 0: never read)
    mov [es:bp+RMD_TICK], ax
    mov ax, [di+DSV_STREAM]
    mov [es:bp+RMD_STR], ax
    mov ax, [di+DSV_PKGCALL]
.pk:
    mov [es:bp+RMD_PKG], ax
    push ds
    mov ds, dx
    mov cl, [DRV_H_BSSP]        ; the bytes it runs in: image + bss
    xor ch, ch
    shl cx, 4
    add cx, [DRV_H_IMG]
    pop ds
    call fsx_rdlow              ; AX = its paragraph, the copy's or its own
    jc .room
    or ax, ax
    jz .nx                      ; (a selector with no base: not carried)
    cmp bl, DRVC_SOUND
    jne .nx
    test al, 0x1F               ; SBL_DSCS: DS = CS only on a 512-byte
    jnz .room                   ; boundary (W4)
    call fsx_rsndq              ; ...and its words (CF = 1: AL = FSXR_DRV)
    jc .o
.nx:
    add bp, RMD_SIZE
    shl bh, 1
    cmp bh, 8
    jb .c
    clc
.o:
    pop es
    ret
.room:
    mov al, FSXR_DROOM
    stc
    jmp short .o

; fsx_rextf - EXTD.DRV LOW (W7, SPEC.md 53.2.3): a loaded extended desktop's
; module gets the service area's RMD_EXT row - its whole claim (LSL: the
; image and its tail), copied down and back up as a driver's is - and the
; drop aims EXFP's segments at RMD_SEG. Nothing when it is not loaded (the
; row stays zero, and the drop's fsx_rgone stands). CF = 1 and AL =
; FSXR_DROOM: no room for the copy. PM, DS = KDATA. Clobbers AX, CX, DX, BP
fsx_rextf:
    mov dx, [mod_tab + MOD_EXT * MODR_SIZE + MODR_SEG]
    or dx, dx                   ; its DATA selector (mega_mods_fix), 0 = not
    jz .o                       ; loaded: CF = 0 from the `or`
    push es
    mov es, [ss:fsx_rsvc]
    mov bp, rms_drv - rms_lo + RMD_EXT
    mov [es:bp+RMD_SEL], dx
    lsl cx, dx                  ; the claim's last byte (a module is far
    jnz .x                      ; under 64KB) - or no descriptor: no image
    inc cx                      ; (CF = 0 from the `or`)...
    inc cx
    and cx, -2                  ; ...and its bytes, even
    call fsx_rdlow
    mov al, FSXR_DROOM
.x:
    pop es
.o:
    ret

; fsx_rdlow - a low copy's row: DX = the image's data selector, CX = its bytes
; (even), ES:BP = the row. Conventional already: RMD_SEG its own paragraph.
; Above 1MB: a conventional claim, RMD_CL/HI/LO/LEN for fsx_rcprow, and
; RMD_SEG the claim's paragraph. out CF = 0, AX = RMD_SEG (0 if the selector
; has no base, and the row then unset); CF = 1 no room. Clobbers CX, DX
fsx_rdlow:
    mov ax, dx
    push cx
    call mega_selbase           ; DX:AX = its base
    pop cx
    jc .none
    cmp dx, 0x10
    jb .para                    ; conventional: it runs where it is
    mov [es:bp+RMD_HI], ax
    mov [es:bp+RMD_HI+2], dx
    mov [es:bp+RMD_LEN], cx
    xchg ax, cx                 ; AX = bytes
    push bx
    call fsx_rlowclaim          ; DX = the claim, AX:CX its base
    pop bx
    jc .o
    mov [es:bp+RMD_CL], dx
    mov [es:bp+RMD_LO], cx
    mov [es:bp+RMD_LO+2], ax
    xchg ax, dx
    xchg ax, cx                 ; DX:AX = the copy's base
.para:
    shr ax, 4
    shl dx, 12
    or ax, dx
    mov [es:bp+RMD_SEG], ax
.o:
    ret
.none:
    xor ax, ax                  ; (CF = 0)
    ret

; fsx_rsndq - the sound row's SNDV_RMODE list (W4), asked of the driver
; (AL = 2, THE DOOR: the list alone since SPEC.md 34.5.7 made a Sound
; Blaster's IRQ discovery its attach's - the door ran it here while it was
; deferred to the first stream) into the service area,
; and each word asked whether it HAS a real
; segment - a selector above 1MB has none, and a stream that is not handed
; one cannot play in the bracket. ES = the service area, ES:BP the row. CF =
; 1 and AL = FSXR_DRV a word with none, or a list longer than RMS_SNMAX.
; Clobbers AX, CX, SI, DI
fsx_rsndq:
    push bx
    push dx
    push bp
    push es
    push ds
    pop es                      ; ES = KDATA: the verb is the kernel's alone
    mov bx, DRVC_SOUND * 256 + SNDV_RMODE
    mov al, 2
    mov bp, drv_pkg_call_x
    call KERNEL_SEG:mega_tcall  ; SI = the list in its segment, CX = count
    pop es
    pop bp
    jc .no
    cmp cx, RMS_SNMAX
    ja .no
    mov [es:rms_sn - rms_lo], cx
    mov di, rms_soff - rms_lo
    cld
    mov dx, [es:bp+RMD_SEL]
.w:
    jcxz .ok
    push ds
    mov ds, dx
    lodsw                       ; the word's offset
    mov bx, ax
    mov ax, [bx]                ; ...and what it holds now
    pop ds
    mov [es:di], bx
    inc di
    inc di
    call COLD_SEG:mega_s2segc ; AX = its segment, FFFFh none
    cmp ax, 0xFFFF
    je .no
    dec cx
    jmp short .w
.ok:
    clc
    jmp short .o
.no:
    mov al, FSXR_DRV
    stc
.o:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fsx_rcopyf - FAR: AL = 0 the region DOWN into its copy, and each low
; driver's image into its, 1 all of them back UP. Nothing for one that runs
; where it is. PM, DS = KDATA, any IF (fsx_run's copy runs at IF = 1, under
; sch_lock as every mega_lincopy does). Every register kept. fsx_rcopydf is
; the AL = 0 call with AX kept, for fsx_run (kern_mega size pass 2)
; -----------------------------------------------------------------------------
fsx_rcopydf:
    pusha
    xor ax, ax
    jmp short fsx_rcopyf.go
fsx_rcopyf:
    pusha
.go:
    push es
    push ss
    pop es
    cmp word [ss:fsx_rlow], 0
    je .nr
    mov si, fsx_rhi
    mov di, fsx_rlo
    mov cx, [ss:fsx_rlen]
    call fsx_rcp1
.nr:
    mov bx, [ss:fsx_rsvc]
    or bx, bx
    jz .o
    mov es, bx
    mov bx, rms_drv - rms_lo
    mov dx, RMD_NA              ; (not SOUND's: fsx_rsndf, at IF = 0)
.d:
    call fsx_rcprow
.dn:
    add bx, RMD_SIZE
    dec dx
    jnz .d
    add bx, RMD_EXT - RMD_NA * RMD_SIZE ; ...and EXTD.DRV's (W7), with the
    call fsx_rcprow             ; region's: nothing writes it in between
.o:
    pop es
    popa
    retf
; fsx_rcprow - ES:BX = a driver's row: its image copied, AL the way (0
; down), when it has a copy. Clobbers CX, SI, DI
fsx_rcprow:
    cmp word [es:bx+RMD_CL], 0
    je .o
    lea si, [bx+RMD_HI]
    lea di, [bx+RMD_LO]
    mov cx, [es:bx+RMD_LEN]
    call fsx_rcp1
.o:
    ret
fsx_rcp1:                       ; ES:SI the high linear, ES:DI the low, CX
    or al, al                   ; bytes, AL the way
    jz .dw
    xchg si, di
.dw:
    pusha                       ; (mega_lincopy spends AX..DI: BX and DX are
    mov ax, [es:si]             ; the walk's)
    mov [mega_xsrc], ax
    mov ax, [es:si+2]
    mov [mega_xsrc+2], ax
    mov ax, [es:di]
    mov [mega_xdst], ax
    mov ax, [es:di+2]
    mov [mega_xdst+2], ax
    inc byte [sch_lock]
    call mx_lcopy               ; (`.mega` too: near, not the door)
    dec byte [sch_lock]
    popa
    ret

; -----------------------------------------------------------------------------
; fsx_rdownf - FAR, the last of protected mode before the drop (IF = 0, DS =
; KDATA): the 8259's masks a service reflection's, IVT 20h the real-mode
; tick, and every word real mode will read as a segment made one again -
; VSEG's four and the video and font layer's (banked, mega_vwalk), [k_seg0]
; and CPU_F_PM (SPEC.md 53.2.3 step 3). Every register kept
; -----------------------------------------------------------------------------
%define FSX_RKEEPN (fsx_rkeep_end - fsx_rkeep) ; fsx_rkeep's bytes
%define FSX_RKMN (fsx_rkeepm_end - fsx_rkeepm) ; ...and fsx_rkeepm's
fsx_rdownf:
    mov [fsx_rwin], si          ; fsx_rcall's window and entry, banked
    mov [fsx_rent], bp          ; across the drop
    pusha
    push es
    mov [mega_pss], ss          ; the stack mega_up puts back (fsx_rland)
    in al, 0x21                 ; the masks the return puts back, and what
    mov [mega_imr1], al         ; mega_r286 compares a 286's reset against
    in al, 0xA1
    mov [mega_imr2], al
    mov byte [mega_irqr], 0     ; (a service's masks, not an IRQ's)
    mov bx, 0x3FB8
    call MEGA_SEL_CODE:mega_rmaskf.nv ; BX = the lines to shut, [mega_rmask]
    xor ax, ax                  ; ...and mega_vrm's two words, if it made
    xchg ax, [mega_vsw]         ; them segments for the pointer: put back,
    or ax, ax                   ; since the walk below banks and converts
    jz .nvs                     ; all of them
    mov [vid_seg], ax
    mov ax, [mega_vsw+2]
    mov [k_vsa], ax
.nvs:
    call fsx_rsnddn             ; SOUND.DRV low, its lines open (W4) -
    call fsx_rserdn             ; ...a SERIAL.DRV lease served, its line
                                ; open (W7) -
    mov [mega_rmask], bx        ; and THE MASKS mega_r286 COMPARES A 286'S
                                ; RESET AGAINST with them: left at the lines
                                ; mega_rmaskf said, the compare missed, the
                                ; 8259 was re-initialised and the card's line
                                ; shut for the bracket (vm/286-hma, W4)
    mov word [ss:fsx_rstko], rms_stick ; ...and the tick's call to its DSV_TICK
    mov al, [mega_imr1]
    or al, bl
    out 0x21, al
    mov al, [mega_imr2]
    or al, bh
    out 0xA1, al
    mov ax, MEGA_SEL_LOW0       ; IRQ0 in real mode: the PIC stays at 20h, so
    mov es, ax                  ; it is IVT 20h's - fsx_rupf's forced sync
    mov word [es:MEGA_PIC_BASE*4], fsx_rtick   ; puts the ROM's back
    mov word [es:MEGA_PIC_BASE*4+2], KERNEL_SEG
    mov ax, [EXFP]              ; EXTD.DRV's slots: unloaded, every one
    mov [ss:fsx_rexfp], ax      ; mod_gone's, and fsx_rgone's for the
    mov ax, [EXFP+2]            ; bracket; LOADED (W7), every segment its
    mov [ss:fsx_rexfp+2], ax    ; low copy's paragraph, the offsets kept
    mov es, [ss:fsx_rsvc]       ; (fsx_rexfill). Slot 0 is the far pointer
    mov dx, [es:rms_drv - rms_lo + RMD_EXT + RMD_SEG] ; banked: one segment
    or dx, dx                   ; for all of them either way
    jnz .exl
    mov dx, KERNEL_SEG
.exl:
    mov ax, fsx_rgone
    call fsx_rexfill
    mov word [k_seg0], 0        ; low memory is segment 0 again (MEGA_SEG0)
    and byte [cpu_feat], ~CPU_F_PM & 0xFF ; OSAPI_CPU_INFO answers real mode
    mov si, fsx_rbank
    mov ax, fsx_rvdn
    call mega_vwalk
    mov ax, [ss:fsx_rfont]      ; THE GLYPH TABLE above 1MB (fsx_rvdn left
    or ax, ax                   ; its selector, which real mode cannot use):
    jz .fnt                     ; the service area's copy instead, in the
    add ax, rms_lo              ; twins' own paragraph (KERN-MEGA-PLAN
    xchg ax, [font_base]        ; 14.131). [font_seg] goes back off the
    mov [ss:fsx_rfbase], ax     ; bank at the return, [font_base] off this
    mov ax, [ss:fsx_rmsseg]
    mov [font_seg], ax
.fnt:
%ifdef VID_EXT
    mov ax, [vx_ep]             ; the VESA desktop's hook: VESA.DRV's code
    mov [ss:fsx_rvxep], ax      ; half above 1MB, a selector real mode cannot
    mov ax, [vx_ep+2]           ; follow - vx_null's `.text` answer for the
    mov [ss:fsx_rvxep+2], ax    ; bracket (it draws nothing; no cell the
    mov word [vx_ep], vx_null   ; table keeps reaches the hook, so this is the
    mov word [vx_ep+2], KERNEL_SEG ; far slot's insurance, W1's EXTD lesson)
%endif
    ; THE TABLE (SPEC.md 53.2.3): banked whole in the park's scratch, then
    ; every byte `stc`, a `retf` before each kept run and at the end, the
    ; kept runs copied back and each twin a far jump - fsx_rkeep's list,
    ; walked DOWN, so a `retf` written into a kept run's last byte by the run
    ; above it is overwritten when that run is copied back
    mov ax, MEGA_SEL_PSCR
    mov es, ax
    mov si, osapi_table
    xor di, di
    mov cx, OSAPI_TABLE_LEN
    rep movsb
    push ds
    pop es                      ; ES = KDATA: the table's writable alias
    mov di, osapi_table
    mov cx, OSAPI_TABLE_LEN
    mov al, 0xF9                ; stc
    rep stosb
    mov byte [osapi_table_end - 1], 0xCB ; retf
    mov bx, FSX_RKMN            ; THE SERVICE AREA'S TWINS (W3) FIRST: a
.km:                            ; `retf` before each lands in the cell above
    sub bx, 4                   ; it, and if that is a kept run's last byte,
    mov di, [cs:fsx_rkeepm+bx]  ; the copy-back below puts it right
    mov byte [di-1], 0xCB
    mov byte [di], 0xEA         ; `jmp far [fsx_rmsseg]:twin`
    mov ax, [cs:fsx_rkeepm+bx+2]
    mov [di+1], ax
    mov ax, [ss:fsx_rmsseg]
    mov [di+3], ax              ; (no `or bx, bx`: every instruction since
    jnz .km                     ; the `sub` is a `mov` - kernel size pass 17)
    mov bx, FSX_RKEEPN          ; the list's bytes, walked down
.keep:
    sub bx, 6                   ; (BX = the row's offset in the list)
    mov di, [cs:fsx_rkeep+bx]   ; the run's first cell
    mov byte [di-1], 0xCB       ; ...the slide above it stops here
    mov cx, [cs:fsx_rkeep+bx+2] ; its bytes, from the bank
    mov si, di
    sub si, osapi_table
    push ds
    mov ax, MEGA_SEL_PSCR
    mov ds, ax
    rep movsb
    pop ds
    mov ax, [cs:fsx_rkeep+bx+4] ; a TWIN: `jmp far KERNEL_SEG:twin` over the
    or ax, ax                   ; cell's first five
    jz .kn
    mov di, [cs:fsx_rkeep+bx]
    mov byte [di], 0xEA
    mov [di+1], ax
    mov word [di+3], KERNEL_SEG
.kn:
    or bx, bx
    jnz .keep
    mov al, 0x8D                ; NMI off across the switch, LAST
    out 0x70, al
    mov byte [mega_drop], 2     ; ...landing at fsx_rland
    pop es
    popa
    retf

; -----------------------------------------------------------------------------
; fsx_rsnddn - SOUND.DRV INTO THE BRACKET (W4, SPEC.md 53.2.3), at the drop:
; IF = 0, PM, DS = KDATA. The driver told (SNDV_RMODE, AL = 1); each word of
; its list banked and made the real segment with its base; its image copied
; down (the card plays on: a block IRQ between a copy at IF = 1 and the drop
; would be counted into the image that stops running); and every IVT cell
; 08h-0Fh / 70h-77h naming its image banked and pointed at the copy - a
; master line's twin at 20h + n too, where the 8259 delivers it - its line
; taken out of BX, the lines the drop shuts. Nothing with no sound row.
; in/out BX = the lines to shut (mega_rmaskf's). Every other register kept
; -----------------------------------------------------------------------------
fsx_rsnddn:
    pusha
    push es
    mov es, [ss:fsx_rsvc]
    mov bp, rms_drv - rms_lo + RMD_SND
    cmp byte [es:bp+RMD_CLS], 0
    je .o
    mov al, 1
    call fsx_rsndv              ; the 8237 rule, real mode's
    mov cx, [es:rms_sn - rms_lo]
    xor si, si
.w:
    jcxz .wd
    mov di, [es:rms_soff - rms_lo + si]
    push ds
    mov ds, [es:bp+RMD_SEL]
    mov ax, [di]
    mov [es:rms_sbank - rms_lo + si], ax
    call COLD_SEG:mega_s2segc ; AX = its segment (the door saw one)
    inc ax
    jz .ws                      ; FFFFh: none - 0, which the return renames
    dec ax                      ; back from the bank like any other
.ws:
    mov [di], ax
    pop ds
    mov [es:rms_sconv - rms_lo + si], ax
    inc si
    inc si
    dec cx
    jmp short .w
.wd:
    xor al, al
    xchg bx, bp
    call fsx_rcprow             ; the image down (a conventional one: none)
    xchg bx, bp
    push ds
    mov ax, MEGA_SEL_LOW0
    mov ds, ax
    mov dx, [es:bp+RMD_SEL]
    sub dx, 8                   ; the CODE half: what its vectors name
    mov di, rms_svec - rms_lo
    mov si, 0x08 * 4
    mov cx, 1                   ; the cell's 8259 bit, master then slave
.v:
    cmp [si+2], dx
    jne .vn
    cmp di, rms_svec - rms_lo + RMV_N * 6
    jae .vn
    mov [es:di], si
    mov ax, [si]
    mov [es:di+2], ax
    mov [es:di+4], dx
    add di, 6
    mov ax, [es:bp+RMD_SEG]
    mov [si+2], ax
    cmp si, 0x10 * 4
    jae .op
    push word [si]              ; a master line: IRQ n is vector 20h + n here
    pop word [si + (MEGA_PIC_BASE - 8) * 4]
    mov [si + (MEGA_PIC_BASE - 8) * 4 + 2], ax
.op:
    mov ax, cx
    cmp si, 0x0A * 4            ; IRQ 2 on an AT is the slave's IRQ 9, whose
    jne .o9                     ; ROM handler raises int 0Ah: open both
    or ah, 2
.o9:
    or [es:rms_sline - rms_lo], ax
    not ax
    push bp
    mov bp, sp
    and [ss:bp+14], ax          ; pusha's BX, under BP, DS and ES
    pop bp
.vn:
    add si, 4
    shl cx, 1
    cmp si, 0x10 * 4
    jne .vs
    mov si, 0x70 * 4
.vs:
    cmp si, 0x78 * 4
    jb .v
    pop ds
.o:
    pop es
    popa
    ret

; fsx_rsndv - SNDV_RMODE with AL = the way (1 into the bracket, 0 out), to
; the sound row's driver at its protected-mode image. ES:BP = the row, DS =
; KDATA. Clobbers AX, BX, CX, SI, DI
fsx_rsndv:
    push bp
    push es
    push ds
    pop es                      ; ES = KDATA: the verb is the kernel's alone
    mov bx, DRVC_SOUND * 256 + SNDV_RMODE
    mov bp, drv_pkg_call_x
    call KERNEL_SEG:mega_tcall
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; fsx_rsndup - ...AND OUT, at the return: IF = 0, PM, DS = KDATA, before the
; forced sync. The IVT cells put back; the image copied up; each word that
; still holds what the drop wrote given its selector back, and a word the
; bracket WROTE - a stream opened inside stores real segments - the selector
; of the claim based at that paragraph (mega_p2d), 0 when none is; the driver
; told (AL = 0). So no real-mode segment outlives the bracket and a stream
; opened in either mode plays on in the other. Every register kept
; -----------------------------------------------------------------------------
fsx_rsndup:
    pusha
    push es
    mov es, [ss:fsx_rsvc]
    mov bp, rms_drv - rms_lo + RMD_SND
    cmp byte [es:bp+RMD_CLS], 0
    je .o
    push ds
    mov ax, MEGA_SEL_LOW0
    mov ds, ax
    mov di, rms_svec - rms_lo
    mov cx, RMV_N
.v:
    mov si, [es:di]
    or si, si
    jz .vn
    mov ax, [es:di+2]
    mov [si], ax
    mov ax, [es:di+4]
    mov [si+2], ax
.vn:
    add di, 6
    loop .v
    pop ds
    mov al, 1
    xchg bx, bp
    call fsx_rcprow             ; the image up
    xchg bx, bp
    mov cx, [es:rms_sn - rms_lo]
    xor si, si
.w:
    jcxz .wd
    mov di, [es:rms_soff - rms_lo + si]
    push ds
    mov ds, [es:bp+RMD_SEL]
    mov dx, [di]
    mov ax, [es:rms_sbank - rms_lo + si]
    cmp dx, [es:rms_sconv - rms_lo + si]
    je .wr                      ; untouched: its selector back
    xor ax, ax
    or dx, dx
    jz .wr
    call mega_p2d               ; DX = the selector of the claim based there
    jc .wr
    xchg ax, dx
.wr:
    mov [di], ax
    pop ds
    inc si
    inc si
    dec cx
    jmp short .w
.wd:
    xor al, al
    call fsx_rsndv              ; the 8237 rule, protected mode's
.o:
    pop es
    popa
    ret

; fsx_rkeep - THE REAL-MODE TABLE'S LIST (SPEC.md 53.2.3,
; MEGA-VIDEO-REALMODE-PLAN 7.2): {dw first cell, dw bytes kept, dw twin}, in
; table order, every run a pair of labels in the table itself (kernel.asm's
; rmk_*) so a run is exactly its cells. Bytes 0 and a twin: the cell is a far
; jump to a `.text` body. Everything not listed answers CF = 1
fsx_rkeep:
    dw rmk_1, rmk_1e - rmk_1, 0     ; OSAPI_WM_CONTENT
    dw rmk_2, rmk_2e - rmk_2, 0     ; OSAPI_GET_TICKS, _SET_COLOR, _MOUSE
    dw rmk_3, rmk_3e - rmk_3, 0     ; OSAPI_SND_CAPS
    dw rmk_4, rmk_4e - rmk_4, 0     ; OSAPI_VIDEO
    dw rmk_5, rmk_5e - rmk_5, 0     ; OSAPI_CPU_INFO
    dw rmk_6, rmk_6e - rmk_6, 0     ; OSAPI_FONT_GLYPHS
    dw rmk_7, rmk_7e - rmk_7, 0     ; OSAPI_FSX_MODE, _FSX_WAIT
    dw rmk_8, rmk_8e - rmk_8, 0     ; OSAPI_KEY_DOWN, _FSX_SURF
    dw rmk_9, rmk_9e - rmk_9, 0     ; OSAPI_WM_WAKE
    dw rmk_10, rmk_10e - rmk_10, 0  ; OSAPI_FSX_PAGE
    dw rmk_seg, 0, osapi_segr       ; OSAPI_SEG: the twin
fsx_rkeep_end:
; fsx_rkeepm - ...and the service area's (W3): {dw the cell, dw the twin in
; `.mega`'s numbering}, a far jump to [fsx_rmsseg]:twin - the claim the twins
; were copied into (fsx_rsvcf). READ_AT, XMEM_ALLOC and XMEM_FREE are not
; here: they answer CF = 1 (SPEC.md 53.2.3)
fsx_rkeepm:
    dw apic_xm_caps, rms_xcaps          ; OSAPI_XMEM_CAPS
    dw apic_xm_copy, rms_xcopy          ; OSAPI_XMEM_COPY
    dw apic_drv_pkg_call, rms_drvx      ; OSAPI_DRV_CALL (ES = your DS)
    dw apic_drv_call_at, rms_drva       ; OSAPI_DRV_CALL_AT (ES as it is)
    dw rmk_rseq, rms_rseq               ; OSAPI_FILE_READ_SEQ
    dw rmk_sstr, rms_sstr               ; OSAPI_SND_STREAM (W4)
    dw rmk_spk, rms_spk                 ; OSAPI_FSX_SPK (W4)
fsx_rkeepm_end:
%if OSAPI_TABLE_LEN > MPS_SIZE
 %error "the real-mode bracket banks the API table in the park's scratch: MPS_SIZE must hold OSAPI_TABLE_LEN"
%endif

; fsx_rvdn - mega_vwalk's step down: [DI] banked at [SI], SI on, and an LDT
; selector of the allocator's (mega_useg's over a frame buffer or a ROM,
; mega_p2d's over a claim) made the real segment with its base - the test
; mega_ivt_sync's .fixrow makes. Anything else stays: a segment vfixall left
; alone, a 0. A base above 1MB has no segment (FFFFh) and stays a selector,
; which no frame buffer and no glyph table of a real-mode bracket is
fsx_rvdn:
    mov ax, [di]
    mov [ss:si], ax             ; (the bank is `.lowbss`: SS)
    inc si
    inc si
    mov dx, ax
    and dl, 7
    cmp dl, 4
    jne .k
    cmp ax, MEGA_LDT_FREE0 * 8
    jb .k
    call COLD_SEG:mega_s2segc
    cmp ax, 0xFFFF
    je .k
    mov [di], ax
.k:
    ret

; fsx_rexfill - ZF = 1: every EXFP slot := DX:AX; ZF = 0 (W7, EXTD.DRV's low
; copy): every slot's SEGMENT := DX and its offset kept. The drop decides by
; the row's RMD_SEG and the return by what the drop wrote (KERNEL_SEG is
; fsx_rgone's), so the two ends agree whatever the module row says.
; Clobbers CX, DI; no flag is written in the loop
fsx_rexfill:
    mov di, EXFP
    mov cx, EXT_NENT
.l:
    jnz .s
    mov [di], ax
.s:
    mov [di+2], dx
    lea di, [di+4]
    loop .l
    ret

; -----------------------------------------------------------------------------
; fsx_rserdn - A SERIAL.DRV LEASE INTO THE BRACKET (W7, SPEC.md 53.2.3), at the
; drop: IF = 0, PM, DS = KDATA. The pointer's ISR far-calls the module for
; the leased UART (mou_isr_ser): SERFP's segments made the module's paragraph
; (its claim is conventional - the %error), the OWNER's ring (in its region,
; above 1MB or not) copied into the service area and srl_seg:srl_buf aimed at
; the copy, and the leased line left open when its real-mode vector is the
; kernel's - mega_rmaskf's own test for the mouse's. Nothing with no lease.
; THE BRACKET PACKAGE'S OWN lease (fsx_rserb = 1) rides the region's copy and
; not a copy of its own, so bytes the protected-mode ISR writes to the high
; ring between the arm's copy down and this drop, or between fsx_rserup and
; the copy up passing the ring, are lost: a known limit, no shipped caller
; holds one (SPEC.md 53.2.3's serial lease paragraph).
; in/out BX = the lines to shut. Every other register kept
; -----------------------------------------------------------------------------
%if MOD_HIOK & (1 << MOD_SER)
 %error "fsx_rserdn calls SERIAL.DRV at its own paragraph: its claim must stay conventional"
%endif
fsx_rserdn:
    cmp word [ss:fsx_rserb], 0
    je .o
    push ax
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov dx, [SERFP+2]           ; its code selector, banked
    mov [ss:fsx_rsfp], dx
    mov ax, [mod_tab + MOD_SER * MODR_SIZE + MODR_SEG] ; its data selector
    push ax
    call mega_selbase           ; DX:AX = its base, below 1MB
    shr ax, 4
    shl dx, 12
    or dx, ax                   ; DX = its paragraph
    mov di, SERFP + 2
    mov cx, SER_NENT
.s:
    mov [di], dx
    add di, 4
    loop .s
    mov ax, [fsx_rseg]          ; (the region's paragraph, for its own)
    mov dx, [ss:fsx_rmsseg]     ; the copy, in the twins' own segment
    mov di, [ss:fsx_rserb]
    pop ds                      ; DS = the module's data
    mov si, [SRL_O_BUF]
    mov cx, [SRL_O_SEG]
    mov [ss:fsx_rsrbf], si      ; the owner's ring, banked...
    mov [ss:fsx_rsrsg], cx
    cmp di, 1
    jne .cp
    mov [SRL_O_SEG], ax         ; ...THIS package's: the region's paragraph,
    mov ah, [SRL_O_MASK]        ; the offset as it was
    push ax
    jmp short .line
.cp:
    mov [SRL_O_SEG], dx         ; ...another's: the copy named in its place
    lea dx, [di + rms_lo]
    mov [SRL_O_BUF], dx
    mov ah, [SRL_O_MASK]        ; its line's PIC bit, IRQ3 or IRQ4
    push ax
    mov es, [ss:fsx_rsvc]
    mov ds, cx
    mov cx, SER_SIZE
    cld
    rep movsb                   ; the owner's ring into the copy
.line:
    mov al, MEGA_PIC_BASE - 1
.b:
    inc ax                      ; (the bit rides off AH's bottom, the count
    shr ah, 1                   ; up AL's: the line is under 8)
    jnc .b                      ; AL = the line's real-mode vector
    mov ah, 4
    mul ah
    xchg ax, si
    pop ax
    mov cx, MEGA_SEL_LOW0
    mov ds, cx
    cmp word [si+2], KERNEL_SEG
    jne .shut                   ; not the kernel's: it stays shut
    not ah
    and bl, ah                  ; ...open
.shut:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.o:
    ret

; fsx_rserup - ...and back, at the return (IF = 0, PM, DS = KDATA): the ring
; copied over the owner's, srl_seg and srl_buf the owner's again, SERFP's
; code selector. Every register kept
fsx_rserup:
    cmp word [ss:fsx_rserb], 0
    je .o
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov ax, [ss:fsx_rsfp]
    mov di, SERFP + 2
    mov cx, SER_NENT
.s:
    mov [di], ax
    add di, 4
    loop .s
    mov es, [mod_tab + MOD_SER * MODR_SIZE + MODR_SEG]
    mov ax, [ss:fsx_rsrsg]
    mov di, [ss:fsx_rsrbf]
    mov [es:SRL_O_SEG], ax
    mov [es:SRL_O_BUF], di
    mov si, [ss:fsx_rserb]
    cmp si, 1
    je .own                     ; THIS package's: the region's copy-up has it
    mov es, ax                  ; ES:DI = the owner's ring
    mov ds, [ss:fsx_rsvc]       ; DS:SI = the copy
    mov cx, SER_SIZE
    cld
    rep movsb
.own:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
.o:
    ret

; fsx_rvup - ...and its step back: the bank's word into [DI]
fsx_rvup:
    mov ax, [ss:si]
    inc si
    inc si
    mov [di], ax
    ret

; -----------------------------------------------------------------------------
; fsx_rupf - FAR, the first of protected mode after the return (IF = 0, DS =
; KDATA, the rate already off): fsx_rdownf undone in reverse, then the region
; copied back UP with interrupts on and its claim freed. Every register kept
; -----------------------------------------------------------------------------
fsx_rupf:
    pusha
    push es
    cmp word [spk_seg], 0       ; A SPEAKER RING LEFT OPEN (SPEC.md 34.11) is
    je .nspk                    ; closed HERE, before fsx_restore's spk_off -
    in al, 0x61                 ; the twin's close (rms_spk) less its two
    and al, 0xFC                ; writes that cannot matter: IVT 20h is put
    out 0x61, al                ; back by the forced sync below, and channel
    mov al, 0xB6                ; 0 by sch_fast_off before this call. spk_off
    out 0x43, al                ; used to close it AFTER that, reloading
    xor ax, ax                  ; channel 0 with [spk_rdsv] - the bracket's
    mov [spk_seg], ax           ; divisor - once [sch_fast] was already 0, so
    mov [snd_ch2mode], ax       ; the desktop ran at the bracket's rate with
.nspk:                          ; every IRQ0 a tick. Channel 2 idle, IF = 0.
                                ; It was in fsx_rland's `.text`, before
                                ; sch_fast_off - which writes channel 0 and
                                ; reads neither word, so the order between
                                ; the two is free (kernel size pass 16)
    call fsx_rsndup             ; SOUND.DRV back up, its cells first (W4)
    not word [mega_ivtsh]       ; THE FORCED SYNC (mega_dwake's): the gates
    call mega_ivt_sync          ; rebuilt from the IVT whatever the shadow
                                ; says, and IVT 20h-27h written back as the
                                ; copy of 08h-0Fh - which is what takes the
                                ; real-mode tick off IRQ0's real-mode vector
                                ; before the next reflection could take it
    push ds                     ; THE TABLE back, whole, from the bank -
    pop es                      ; before IF = 1, so no protected-mode caller
    mov di, osapi_table         ; ever meets the real-mode one
    xor si, si
    mov cx, OSAPI_TABLE_LEN
    push ds
    mov ax, MEGA_SEL_PSCR
    mov ds, ax
    rep movsb
    pop ds
%ifdef VID_EXT
    mov ax, [ss:fsx_rvxep]      ; ...and the VESA hook
    mov [vx_ep], ax
    mov ax, [ss:fsx_rvxep+2]
    mov [vx_ep+2], ax
%endif
    mov al, [mega_imr1]
    out 0x21, al
    mov al, [mega_imr2]
    out 0xA1, al
    mov word [k_seg0], MEGA_SEL_LOW0
    or byte [cpu_feat], CPU_F_PM
    mov word [spk_rdsv], 0      ; a speaker the twin left open (rms_spk):
                                ; fsx_restore's spk_off closes it AFTER the
                                ; rate went off at the return, so channel 0
                                ; goes back to 65536, not to the rate (34.11.1)
    call fsx_rserup             ; a lease's ring, words and slots back (W7)
    mov ax, [ss:fsx_rexfp]      ; EXTD.DRV's slots back: mod_gone's, or the
    mov dx, [ss:fsx_rexfp+2]    ; image's code selector (W7)
    cmp word [EXFP+2], KERNEL_SEG ; (ZF: the drop's fsx_rgone, the whole slot)
    call fsx_rexfill
    mov si, fsx_rbank
    mov ax, fsx_rvup
    call mega_vwalk
    cmp word [ss:fsx_rfont], 0  ; the glyph table's offset back (the drop's
    je .fnt                     ; copy: KERN-MEGA-PLAN 14.131)
    mov ax, [ss:fsx_rfbase]
    mov [font_base], ax
.fnt:
    mov byte [fsx_rmode], 0
    sti                         ; the copy back at IF = 1: no other task runs
    mov al, 1                   ; (frozen) and the rate is off, so nothing
    call MEGA_SEL_CODE:fsx_rcopyf ; in protected mode writes the window's
                                ; copy meanwhile - nor a low driver's image
    call fsx_rfree              ; ...and every claim back: the copies', the
    pop es                      ; service area's
    popa
    retf
cpu 8086

; =============================================================================
; THE SERVICE AREA (SPEC.md 53.2.3, MEGA-VIDEO-REALMODE-PLAN W3): what a
; real-mode bracket reads files, extended memory and the low drivers with.
;
; THE LIST'S EXTENTS. A stream the caller names (FSXL_FILE) is walked at the
; door, while protected mode still has the file layer, into a row of the
; park's scratch (MEGA_SEL_PSCR - free at the door: a trip, a hibernate and a
; bracket exclude each other, and the table is banked there only at the drop)
; and its RUNS after the rows: absolute sectors of a BIOS unit, or a DRVC_DISK
; volume's own. fsx_rsvcf then copies rows and runs into the service area
; behind the twins' code, so the claim is exactly as big as the file is
; fragmented.
; =============================================================================
RMS_NAME    equ 0               ; db[13]: the name, upper case, NUL
RMS_ERR     equ 13              ; db: 0, or the FERR_* the twin answers
RMS_SIZE    equ 14              ; dd: the file's size
RMS_RUN     equ 18              ; dw: its first run's index
RMS_NRUN    equ 20              ; dw: ...and how many
RMS_KIND    equ 22              ; db: DVK_BIOS 0, DVK_DRV 1
RMS_UNIT    equ 23              ; db: the int 13h drive, or the driver's handle
RMS_SPT     equ 24              ; dw: a BIOS unit's sectors a track...
RMS_HEADS   equ 26              ; dw: ...and heads
RMS_CYL     equ 28              ; db: 1 = a call may run to the CYLINDER's end
                                ; (SPEC.md 18.91.1, 18.91.5), 0 the track's
RMS_SSZ     equ 32
RMR_LBA     equ 0               ; a run: dd its first sector...
RMR_CNT     equ 4               ; ...dw its sectors
RMR_SIZE    equ 6
FSX_RSR     equ 0               ; the scratch: the rows...
FSX_RSN     equ FSXL_NMAX * RMS_SSZ ; ...the runs
FSX_RSV     equ FSX_RSN + FSXL_RUNS * RMR_SIZE ; ...and its counters:
FSV_NSTR    equ 0               ; db: rows written
FSV_DMSK    equ 1               ; db: the classes asked (1 DISK, 2 EMS)
FSV_NRUN    equ 2               ; dw: runs written
%if FSX_RSV + 4 > MPS_SIZE
  %error "the extent list's scratch is the park's: MPS_SIZE must hold FSXL_NMAX rows and FSXL_RUNS runs"
%endif
RMD_CLS     equ 0               ; a LOW DRIVER's row: db its class (0 none)
RMD_DSP     equ 2               ; dw PKG_DISP \ the far pointer its calls go
RMD_SEG     equ 4               ; dw its paragraph / through
RMD_BLK     equ 6               ; dw its DSV_BLK
RMD_PKG     equ 8               ; dw its DSV_PKGCALL
RMD_CL      equ 10              ; (protected mode's) dw the copy's claim, 0 =
RMD_HI      equ 12              ; it runs where it is; dd its image's base
RMD_LO      equ 16              ; ...dd the copy's
RMD_LEN     equ 20              ; ...dw the bytes, even
RMD_TICK    equ 22              ; DRVC_SOUND (W4): dw its DSV_TICK...
RMD_STR     equ 24              ; ...dw its DSV_STREAM...
RMD_SEL     equ 26              ; ...dw (protected mode's) its data selector
RMD_SIZE    equ 28
RMD_N       equ 4               ; DRVC_DISK's row, DRVC_EMS's, DRVC_SOUND's,
                                ; EXTD.DRV's (W7)
RMD_NA      equ 2               ; ...the first RMD_NA copied at the freeze's
                                ; arm; SOUND's at the drop, at IF = 0 (W4)
RMD_SND     equ 2 * RMD_SIZE    ; ...the sound row's offset in rms_drv
RMD_EXT     equ 3 * RMD_SIZE    ; ...and EXTD.DRV's (W7: no class, no verb -
                                ; RMD_CLS 0 matches no call - an image the
                                ; EXFP slots are aimed at, fsx_rextf)
RMS_SNMAX   equ 4               ; SNDV_RMODE's words the kernel renames
SNDV_RMODE  equ 13              ; drivers/sound/sndpkg.inc, held equal by
                                ; tests/unit/t_mirror.py
RMV_N       equ 2               ; the IVT cells naming SOUND.DRV it redirects
%if RMS_NAME + 13 > RMS_ERR || FSV_DMSK != FSV_NSTR + 1
  %error "a stream row's name is 13 bytes; fsx_rsvcf moves FSV_NSTR and FSV_DMSK as one word"
%endif

section .cold
cpu 286
; -----------------------------------------------------------------------------
; fsx_rwalkf - FAR, one FSXL_FILE: the name at api_name stat'ed where
; inst_vol_enter stood the machine (the caller's folder), and its chain walked
; into runs in the park's scratch - DI = its row there, its name already in
; it. out AL = 0 (with the row's RMS_ERR the stat's or the chain's FERR_*,
; which the twin answers - what the file layer would say), or AL = FSXR_VOL
; (a redirected or file-driver volume, a mounted image, or a sector past
; CHS's reach) / FSXR_FRAG (the runs are full). PM, DS = KDATA. Clobbers AX,
; BX, CX, DX, SI
; -----------------------------------------------------------------------------
FSW_REM     equ 0               ; dd: sectors still to walk
FSW_BASE    equ 4               ; dd: the partition's base (a BIOS unit's)
FSW_MAX     equ 8               ; dd: the sectors CHS reaches
FSW_LBA     equ 12              ; dd: this cluster's first sector
FSW_LAST    equ 16              ; dw: this stream's last run, 0 = none yet
FSW_SIZE    equ 18
fsx_rwalkf:
    push es
    push bp
    sub sp, FSW_SIZE
    mov bp, sp                  ; (SS: the locals)
    mov si, api_name
    call dskw_stat_x            ; BX = the first cluster, DX:CX = the size
    mov si, MEGA_SEL_PSCR
    mov es, si
    jnc .found
    mov [es:di+RMS_ERR], al     ; (every FERR_* fits a byte)
    jmp .ok
.found:
    mov byte [es:di+RMS_ERR], 0
    mov [es:di+RMS_SIZE], cx
    mov [es:di+RMS_SIZE+2], dx
    add cx, 511                 ; its sectors, (size + 511) / 512
    adc dx, 0
    mov ax, cx
    shr ax, 9
    mov si, dx
    shl si, 7
    or ax, si
    mov [bp+FSW_REM], ax
    shr dx, 9
    mov [bp+FSW_REM+2], dx
    xor ax, ax
    mov [bp+FSW_BASE], ax
    mov [bp+FSW_BASE+2], ax
    mov [bp+FSW_LAST], ax
    dec ax
    mov [bp+FSW_MAX], ax        ; a driver's volume: its own bound
    mov [bp+FSW_MAX+2], ax
    mov si, bx                  ; SI = the cluster from here on
    mov bl, [disk_drive]
    call dsk_vol_row_x          ; BX = the volume's row
    mov al, FSXR_VOL
    jc .no
    mov cx, [bx+DV_KIND]        ; CL = the kind, CH = the unit
    mov [es:di+RMS_KIND], cl
    mov [es:di+RMS_UNIT], ch
    cmp cl, DVK_DRV
    jne .bios
    cmp byte [bx+DV_CLASS], DRVC_DISK
    jne .no                     ; (a mounted image, a file class: AL VOL)
    or byte [es:FSX_RSV+FSV_DMSK], 1 ; ...whose driver goes low
    jmp short .walk
.bios:
    cmp cl, DVK_BIOS
    jne .no                     ; a REDIRECTED volume has no sectors at all
    mov ax, [disk_spt]
    mov [es:di+RMS_SPT], ax
    mov dx, [disk_heads]
    mov [es:di+RMS_HEADS], dx
    mul dx                      ; AX = a cylinder's sectors
    mov dx, 80                  ; dsk_geom_pick's bound: a floppy's 80
    test ch, 0x80               ; cylinders, CHS's 1024 on a fixed disk
    jz .fl
    mov dx, 1024
.fl:
    mul dx
    mov [bp+FSW_MAX], ax        ; DX:AX = the sectors CHS can name
    mov [bp+FSW_MAX+2], dx
    xor al, al                  ; THE RUN'S BOUND, as dsk_xfer's .runcmp:
%ifndef TRACK_RUN               ; a fixed disk's reads to the cylinder
    test ch, 0x80               ; (DSK_HCYL), a floppy's where the boot's
    jz .flc                     ; canary saw the FDC carry one ([dsk_cylrun])
 %ifdef DSK_HCYL
  %ifdef DSK_HCYLPROBE
    mov al, [dsk_hcyl]
  %else
    mov al, 1
  %endif
 %endif
    jmp short .cy
.flc:
    mov al, [dsk_cylrun]
.cy:
%endif
    mov [es:di+RMS_CYL], al
    mov bl, [disk_drive]        ; ...and the partition's base, made absolute
    xor bh, bh                  ; here (dsk_geom_pick's dsk_vbase)
    shl bx, 2
    mov ax, [dsk_vbase+bx]
    mov [bp+FSW_BASE], ax
    mov ax, [dsk_vbase+bx+2]
    mov [bp+FSW_BASE+2], ax
.walk:
    mov ax, [es:FSX_RSV+FSV_NRUN]
    mov [es:di+RMS_RUN], ax
    and word [es:di+RMS_NRUN], 0 ; (`and`: a byte under `mov`, the `or`
.lp:                            ; below writes the flags - kernel size pass 16)
    mov ax, [bp+FSW_REM]
    or ax, [bp+FSW_REM+2]
    jz .ok
    mov ax, si
    call dsk_clus2lba_x         ; AX = its first sector, [dsk_c2hi] above
    jc .eio
    mov dx, [dsk_c2hi]
    add ax, [bp+FSW_BASE]
    adc dx, [bp+FSW_BASE+2]
    mov [bp+FSW_LBA], ax
    mov [bp+FSW_LBA+2], dx
    mov cl, [dsk_spc]           ; take: the cluster, or the file's last sectors
    xor ch, ch
    cmp word [bp+FSW_REM+2], 0
    jne .tk
    cmp cx, [bp+FSW_REM]
    jbe .tk
    mov cx, [bp+FSW_REM]
.tk:
    add ax, cx                  ; ...whose end CHS must reach
    adc dx, 0
    cmp dx, [bp+FSW_MAX+2]
    jne .rc
    cmp ax, [bp+FSW_MAX]
.rc:
    mov al, FSXR_VOL
    ja .no
    mov bx, [bp+FSW_LAST]       ; THE SAME RUN, when it starts where the last
    or bx, bx                   ; one ends (and has room)
    jz .new
    mov ax, [es:bx+RMR_LBA]
    mov dx, [es:bx+RMR_LBA+2]
    add ax, [es:bx+RMR_CNT]
    adc dx, 0
    cmp ax, [bp+FSW_LBA]
    jne .new
    cmp dx, [bp+FSW_LBA+2]
    jne .new
    cmp word [es:bx+RMR_CNT], 0xFF00
    jae .new
    add [es:bx+RMR_CNT], cx
    jmp short .adv
.new:
    mov ax, [es:FSX_RSV+FSV_NRUN]
    cmp ax, FSXL_RUNS
    jae .frag
    inc word [es:FSX_RSV+FSV_NRUN]
    inc word [es:di+RMS_NRUN]
    imul bx, ax, RMR_SIZE
    add bx, FSX_RSN
    mov [bp+FSW_LAST], bx
    mov ax, [bp+FSW_LBA]
    mov [es:bx+RMR_LBA], ax
    mov ax, [bp+FSW_LBA+2]
    mov [es:bx+RMR_LBA+2], ax
    mov [es:bx+RMR_CNT], cx
.adv:
    sub [bp+FSW_REM], cx
    sbb word [bp+FSW_REM+2], 0
    mov ax, [bp+FSW_REM]
    or ax, [bp+FSW_REM+2]
    jz .ok
    mov ax, si
    call dsk_next_clus_x        ; the next link
    jc .eio
    mov si, ax
    jmp .lp
.frag:
    mov al, FSXR_FRAG
    jmp short .no
.eio:
    mov byte [es:di+RMS_ERR], FERR_IO ; a chain the file layer would refuse
.ok:
    xor al, al
.no:
    add sp, FSW_SIZE
    pop bp
    pop es
    retf
cpu 8086

section .mega
cpu 286
; =============================================================================
; THE REAL-MODE TWINS (SPEC.md 53.2.3, W3): assembled into `.mega` and never
; run there. fsx_rsvcf copies rms_lo..rms_strm into the bracket's SERVICE
; AREA, a conventional claim, with the extent rows and runs behind it, and the
; table's cells far-jump to [fsx_rmsseg]:twin - a CS whose x 16 is the claim's
; base less rms_lo, so every label below is the twin's own offset there. Their
; data is in the same copy, read through CS: and written with DS = CS; the
; kernel's through KERNEL_SEG (a real segment in the bracket), and the pool's
; table as fsx_rsvcf copied what of it they need (mx_tab itself is above 1MB,
; in the tables' tail). WHY NOT RESIDENT `.text`: these are ~1.6 KB that a
; machine needs only inside a real-mode bracket, and `.text` is conventional
; memory on every kern_mega machine for ever; a module would be a system-disk
; read at every Play (W1's decision 1). Copied out of `.mega` they cost the
; bracket a claim and no disk.
; =============================================================================
; ovlchk: real mode - every line from here to rms_hi runs below 1MB, in real
; mode, at [fsx_rmsseg]: `push cs` there is its own segment (os88ovlchk)
align 16
rms_lo:
rms_nstr    db 0                ; the stream rows (fsx_rsvcf), and...
rms_dmsk    db 0                ; ...the classes the list asked
rms_slot    db 0                ; the bracket's instance: XMEM's owner
rms_tier    db 0                ; [cpu_tier]
rms_la      db 0                ; 1: la_capture has run in this bracket
            db 0
rms_drv:    times RMD_N * RMD_SIZE db 0 ; the low drivers (fsx_rdrvf)
rms_sn      dw 0                ; SOUND.DRV (W4): SNDV_RMODE's words, how many
rms_soff    times RMS_SNMAX dw 0 ; ...their offsets in its segment
rms_sbank   times RMS_SNMAX dw 0 ; ...the selectors they held at the drop
rms_sconv   times RMS_SNMAX dw 0 ; ...and the segments the drop made them
rms_svec    times RMV_N * 3 dw 0 ; the IVT cells naming it: {the cell's
                                ; offset (0 none), its offset, its selector}
rms_sline   dw 0                ; ...and the 8259 lines they are (slave high)
rms_xkb     dw 0                ; mx_caps's answer at the drop (rms_xcaps)
rms_xbl     db 0
            db 0
rms_xrow    dw 0                ; the bracket's own pool blocks, copied at
rms_xend    dw 0                ; the drop (fsx_rxrows): first row, past last
rms_src     dd 0                ; a copy's two linear ends
rms_dst     dd 0
rms_rrow    dw 0                ; READ_SEQ: the stream's row
rms_rbuf    dw 0                ; ...the buffer's paragraph, advanced...
rms_rofs    dw 0                ; ...and its offset, 0..15
rms_rn      dw 0                ; ...the bytes it delivers
rms_rcap    dw 0                ; ...its capacity
rms_rsec    dw 0                ; ...the sectors still to read
rms_xl      dd 0                ; the transport: the next sector,
rms_xn      dw 0                ; ...the sectors left,
rms_xrun    dw 0                ; ...this call's,
rms_xcx     dw 0                ; ...its int 13h CX,
rms_xdh     db 0                ; ...its head,
rms_xbn     db 0                ; ...1 = through dsk_secbuf (a 64KB page
                                ; boundary inside the sector)
%define LA_NOPROBE              ; a 286 IS tier 1 (FLAGS' top nibble), and
%include "os88la.inc"           ; kern_mega runs on nothing older
cpu 286
%define UB_DS
%define UB_ES
%include "os88ub.inc"
cpu 286
    LA_DATA
    UB_DATA

; rms_ncmp - DS:SI the caller's NUL name against the row at CS:BP: ZF = 1
; the same, upper case either way. Clobbers AX
rms_ncmp:
    push si
    push bp
.l:
    lodsb
    cmp al, 'a'
    jb .u
    cmp al, 'z'
    ja .u
    sub al, 0x20
.u:
    cmp al, [cs:bp]
    jne .o
    inc bp
    or al, al
    jnz .l
.o:
    pop bp
    pop si
    ret

; -----------------------------------------------------------------------------
; rms_rseq - OSAPI_FILE_READ_SEQ's TWIN: READ_SEQ's contract (DS:SI the name,
; DX:BX the buffer, CX its capacity, ES:DI the cursor), served from the
; extent list. The offset is the cursor's FSEQ_OFF, whole sectors (READ_AT's
; FERR_NAME otherwise, as is a capacity that is not), and the cursor comes
; back advanced with every byte but FSEQ_OFF zero - a SEEK, so the next
; protected-mode call re-seeds from the name. A name the list did not carry
; answers FERR_NAME; one whose stat or chain failed at the door, that FERR_*
; -----------------------------------------------------------------------------
rms_rseq:
    push bx
    push cx
    push si
    push di
    push bp
    push ds
    push es
    cld
    mov bp, rms_strm
    push cx
    mov cl, [cs:rms_nstr]
    xor ch, ch
    jcxz .nf
.f:
    call rms_ncmp
    je .got
    add bp, RMS_SSZ
    loop .f
.nf:
    pop cx
    mov ax, FERR_NAME
    jmp .fail
.got:
    pop cx
    push cs
    pop ds                      ; DS = ours: the name is spent
    mov [rms_rrow], bp
    mov al, [ds:bp+RMS_ERR]
    cbw
    or ax, ax
    jnz .fail
    mov al, FERR_NAME
    test cx, 511                ; (0 too: a capacity is whole sectors)
    jnz .fail
    or cx, cx
    jz .fail
    mov [rms_rcap], cx
    mov si, bx                  ; the buffer, normalized: offset 0..15
    and si, 15
    mov [rms_rofs], si
    shr bx, 4
    add dx, bx
    mov [rms_rbuf], dx
    mov ax, [es:di+FSEQ_OFF]
    mov dx, [es:di+FSEQ_OFF+2]
    mov bx, [ds:bp+RMS_SIZE]
    mov si, [ds:bp+RMS_SIZE+2]
    sub bx, ax
    sbb si, dx                  ; SI:BX = what is left of the file
    jc .end                     ; (at or past the end: nothing, whatever
    mov cx, bx                  ; the offset - READ_AT's order)
    or cx, si
    jz .end
    mov cx, [rms_rcap]
    test ax, 511
    jz .al
    mov ax, FERR_NAME
    jmp .fail
.al:
    or si, si
    jnz .cap
    cmp bx, cx
    jae .cap
    mov cx, bx                  ; ...its tail
.cap:
    or cx, cx
    jz .end
    mov [rms_rn], cx
    add cx, 511
    rcr cx, 1
    shr cx, 8                   ; the sectors
    mov [rms_rsec], cx
    mov si, dx                  ; SI:AX = the first one, in the file
    shr si, 9
    shr ax, 9
    shl dx, 7
    or ax, dx
    imul bx, [ds:bp+RMS_RUN], RMR_SIZE
    add bx, rms_runs            ; BX = the stream's first run
    mov cx, [ds:bp+RMS_NRUN]
.r:
    jcxz .eio
    or si, si
    jnz .rs
    cmp ax, [bx+RMR_CNT]
    jb .rin
.rs:
    sub ax, [bx+RMR_CNT]
    sbb si, 0
    add bx, RMR_SIZE
    dec cx
    jmp short .r
.rin:
    mov si, [bx+RMR_CNT]
    sub si, ax                  ; SI = what this run has left
    xor dx, dx
    add ax, [bx+RMR_LBA]
    adc dx, [bx+RMR_LBA+2]      ; DX:AX = its sector
.rd:
    push cx
    mov cx, [rms_rsec]
    cmp cx, si
    jbe .tk
    mov cx, si
.tk:
    call rms_xfer               ; CX sectors at DX:AX into the buffer
    jc .eio1
    sub [rms_rsec], cx
    sub si, cx
    add ax, cx
    adc dx, 0
    pop cx
    cmp word [rms_rsec], 0
    je .done
    or si, si
    jnz .rd
    add bx, RMR_SIZE            ; the next run
    dec cx
    jz .eio
    mov ax, [bx+RMR_LBA]
    mov dx, [bx+RMR_LBA+2]
    mov si, [bx+RMR_CNT]
    jmp short .rd
.done:
    mov ax, [rms_rn]
    add [es:di+FSEQ_OFF], ax
    adc word [es:di+FSEQ_OFF+2], 0
    push ax
    xor ax, ax
    mov cx, FSEQ_OFF / 2        ; the rest of it zero: a seek
    rep stosw
    pop ax
    xor dx, dx
    jmp short .out              ; (CF = 0 from the `xor`)
.end:
    xor ax, ax                  ; at or past the end: nothing, CF = 0
    cwd
    jmp short .out
.eio1:
    pop cx
.eio:
    mov ax, FERR_IO
.fail:
    stc
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    retf

; rms_xfer - CX sectors (>= 1) at DX:AX of the stream [rms_rrow] into the
; buffer at [rms_rbuf]:[rms_rofs], which it advances. DS = CS. CF = 1 the
; read failed. Every register kept
rms_xfer:
    ; STKBALANCE-OK: a DRVC_DISK volume's DSV_BLK is entered by a CONSTRUCTED far call (push cs / push .back / push seg / push PKG_DISP / retf) whose `retf` the driver's dispatcher answers at .back - drv_pkg_disp's shape, in real mode
    pusha
    push es
    mov bp, [rms_rrow]
    mov [rms_xl], ax
    mov [rms_xl+2], dx
    mov [rms_xn], cx
    cmp byte [ds:bp+RMS_KIND], 0
    je .n
    mov ax, [rms_drv+RMD_SEG]   ; ---- a DRVC_DISK volume: ITS DSV_BLK, at
    or ax, ax                   ; its low copy (the driver splits the run)
    jz .fail
    push cs
    push word .back
    push ax
    push word PKG_DISP
    mov ds, ax                  ; DS = the driver's
    mov si, [cs:rms_xl]         ; DI:SI = the sector...
    mov di, [cs:rms_xl+2]
    mov cx, [cs:rms_xn]         ; ...CX the count...
    mov bx, [cs:rms_rofs]       ; ...DX:BX the buffer
    mov dx, [cs:rms_rbuf]
    mov ah, [cs:bp+RMS_UNIT]    ; AH = its handle, AL = 0 read
    xor al, al
    mov bp, [cs:rms_drv+RMD_BLK]
    push word KERNEL_SEG
    pop es
    retf                        ; (its dispatcher: `call bp / retf`)
.back:
    push cs
    pop ds
    jc .fail
    mov cx, [rms_xn]
    shl cx, 5
    add [rms_rbuf], cx
    jmp .ok
.n:                             ; ---- a BIOS unit: CHS, dsk_xfer's bounds
    mov si, [rms_xn]
    or si, si
    jz .ok
    mov ax, [ds:bp+RMS_SPT]
    mul word [ds:bp+RMS_HEADS]
    mov cx, ax                  ; a cylinder's sectors
    mov ax, [rms_xl]
    mov dx, [rms_xl+2]
    div cx                      ; AX = the cylinder (the door proved < 1024)
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div word [ds:bp+RMS_SPT]    ; AX = the head, DX = the sector - 1
    mov [rms_xdh], al
    mov cx, [ds:bp+RMS_SPT]
    sub cx, dx                  ; the TRACK's end...
    cmp byte [ds:bp+RMS_CYL], 0
    je .tr
    mov cx, [ds:bp+RMS_HEADS]   ; ...or the CYLINDER's: (heads - head) x spt
    sub cx, ax                  ; less what is behind the head
    mov ax, [ds:bp+RMS_SPT]
    push dx
    mul cx
    pop dx
    sub ax, dx
    mov cx, ax
.tr:
    cmp cx, si
    jbe .t1
    mov cx, si
.t1:
    xchg bh, bl                 ; CH = the cylinder's low 8 bits, CL bits 6-7
    ror bl, 1                   ; its 8-9, bits 0-5 the sector
    ror bl, 1
    inc dx
    or bl, dl
    mov [rms_xcx], bx
    mov byte [rms_xbn], 0
    mov ax, [rms_rbuf]          ; THE 64KB PAGE (dsk_runcap): what is left of
    shl ax, 4                   ; it under the buffer, in whole sectors
    add ax, [rms_rofs]
    neg ax
    jz .pg                      ; (a page's start: all of it)
    shr ax, 9
    jnz .pc
    inc byte [rms_xbn]          ; not one whole sector: through dsk_secbuf
    mov cx, 1
    jmp short .pg
.pc:
    cmp cx, ax
    jbe .pg
    mov cx, ax
.pg:
    mov [rms_xrun], cx
    mov al, [ds:bp+RMS_UNIT]
    cmp al, 0x80
    jae .att0
    mov al, [ds:bp+RMS_SPT]     ; a floppy: the volume's EOT (SPEC.md 18.92)
    push ds
    push word KERNEL_SEG
    pop ds
    mov [dsk_dpt+4], al
    pop ds
.att0:
    mov di, 3                   ; three tries a run...
.att:
    cmp byte [rms_xbn], 0
    je .bb
    push word LOW_SEG
    pop es
    mov bx, dsk_secbuf
    jmp short .bs
.bb:
    mov es, [rms_rbuf]
    mov bx, [rms_rofs]
.bs:
    mov cx, [rms_xcx]
    mov dh, [rms_xdh]
    mov dl, [ds:bp+RMS_UNIT]
    mov al, [rms_xrun]
    mov ah, 2
    int 0x13
    jnc .did
    xor ah, ah                  ; ...a reset between them...
    mov dl, [ds:bp+RMS_UNIT]
    int 0x13
    dec di
    jnz .att
    cmp word [rms_xrun], 1      ; ...and then a sector at a time (18.91)
    jbe .fail
    mov word [rms_xrun], 1
    mov di, 3
    jmp short .att
.did:
    cmp byte [rms_xbn], 0
    je .adv
    push ds                     ; the bounced sector out to the buffer
    push word LOW_SEG
    pop ds
    mov si, dsk_secbuf
    mov es, [cs:rms_rbuf]
    mov di, [cs:rms_rofs]
    mov cx, 256
    cld
    rep movsw
    pop ds
.adv:
    mov cx, [rms_xrun]
    add [rms_xl], cx
    adc word [rms_xl+2], 0
    sub [rms_xn], cx
    shl cx, 5
    add [rms_rbuf], cx
    jmp .n
.ok:
    clc
    jmp short .x
.fail:
    stc
.x:
    pop es
    popa
    ret

; -----------------------------------------------------------------------------
; rms_xcaps - OSAPI_XMEM_CAPS's TWIN: mx_caps's answer, which fsx_rsvcf took
; in protected mode (rms_xkb, rms_xbl) - mx_tab is above 1MB, and no code
; changes it in a real-mode bracket. out AX = KB free, DX:CX the pool's base,
; BL free entries
; -----------------------------------------------------------------------------
rms_xcaps:
    push ds
    push word KERNEL_SEG
    pop ds
    mov ax, [xm_kb]
    or ax, ax
    jnz .p
    cwd                         ; no store: kern_mega's own no-store answer
    xor cx, cx
    mov bl, XM_MAX_BLKS
    jmp short .o
.p:
    mov ax, [cs:rms_xkb]        ; mx_caps's answer, taken by fsx_rsvcf in
    mov bl, [cs:rms_xbl]        ; protected mode: the table is above 1MB
    mov dx, MEGA_XPOOL >> 16    ; now (the tables' tail, kern_mega size
    xor cx, cx                  ; pass 2) and no code changes it in a
.o:                             ; real-mode bracket. (CF = 0)
    pop ds
    retf

; -----------------------------------------------------------------------------
; rms_xcopy - OSAPI_XMEM_COPY's TWIN: ES:SI the conventional end, DX:AX the
; extended end - a TOKEN and an offset (SPEC.md 50.6.10), translated by
; the pool's rows (fsx_rxrows) to where the block is NOW, which nothing
; changes in a real-mode
; bracket - CX bytes (even, <= 32768), DI 0 out / 1 in. Moved by LOADALL on a
; 286 and by a loaded base on a 386, in kern_big's XMEM.DRV's pieces (16 KB /
; 2 KB, interrupts open between them), with A20 proved first. out CF = 0;
; CF = 1 AX = 1 no store / 2 not a block of yours / 3 bad length. Every other
; register kept
; -----------------------------------------------------------------------------
rms_xcopy:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    push cs
    pop ds
    mov bp, 3
    jcxz .e
    test cl, 1
    jnz .e
    cmp cx, 32768
    ja .e
    mov bp, 1
    push es
    push word KERNEL_SEG
    pop es
    cmp word [es:xm_kb], 0
    pop es
    je .e
    mov bp, 2
    mov bl, [rms_slot]
    call rms_xchk               ; DX:AX = where those bytes are now
    jc .e
    push dx
    push ax
    mov ax, es                  ; the conventional end, linear
    mov dx, 16
    mul dx
    add ax, si
    adc dx, 0
    mov bp, cx                  ; BP = the bytes
    or di, di
    jnz .in
    mov [rms_src], ax
    mov [rms_src+2], dx
    pop word [rms_dst]
    pop word [rms_dst+2]
    jmp short .go
.in:
    mov [rms_dst], ax
    mov [rms_dst+2], dx
    pop word [rms_src]
    pop word [rms_src+2]
.go:
    call KERNEL_SEG:mega_a20f   ; A20 proved open (a BIOS may have shut it)
    cmp byte [rms_tier], CPU_386
    jae .u
    call rms_lcopy
    jmp short .ok
.u:
    call rms_ucopy
.ok:
    xor ax, ax                  ; (CF = 0)
    jmp short .o
.e:
    mov ax, bp
    stc
.o:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; rms_xchk - mx_chk in real mode: DX:AX = a name, CX = bytes, BL = the owner
; -> CF = 0 inside one of its blocks and DX:AX = where those bytes are now;
; CF = 1 not. Clobbers nothing else
rms_xchk:
    push cx
    push si
    push di
    push ax
    push dx
    mov di, ax                  ; DI = the KB the range spans...
    and di, 0x03FF
    add di, cx
    add di, 1023
    shr di, 10
    sub dx, MEGA_XPOOL >> 16    ; ...AX = its first, from the pool's base
    jc .no
    cmp dh, 0x04
    jae .no
    shr ax, 10
    shl dx, 6
    or ax, dx
    add di, ax                  ; ...DI its end
    jc .no
    mov si, [rms_xrow]          ; the bracket's own blocks, as fsx_rsvcf
.scan:                          ; copied them: {MX_AUX, MX_KB, MX_OFF} of
    cmp si, [rms_xend]          ; every live entry BL ([rms_slot]) owns, in
    jae .no                     ; the table's order (RMX_*)
    mov cx, [si+RMX_AUX]        ; a package's block is named by its TOKEN
    cmp cx, ax
    ja .next
    add cx, [si+RMX_KB]
    cmp cx, di
    jae .found
.next:
    add si, RMX_SIZE
    jmp short .scan
.no:
    pop dx
    pop ax
    stc
    jmp short .o
.found:
    pop dx
    pop ax
    mov cx, [si+RMX_AUX]        ; KB the name is above the block, signed
    sub cx, [si+RMX_OFF]
    mov di, cx
    shl cx, 10
    sar di, 6                   ; DI:CX = bytes
    sub ax, cx
    sbb dx, di                  ; (CF: a block moved UP borrows - no answer)
    clc
.o:
    pop di
    pop si
    pop cx
    ret

; rms_ucopy - XMEM.DRV's tier-2 transport (xm_ucopy): [rms_src] -> [rms_dst],
; BP bytes (even), 2 KB a chunk with DS's and ES's bases loaded (ub_set) and
; interrupts off for each. DS = CS. Clobbers AX, BX, CX, DX, SI, DI, BP and
; the high halves
rms_ucopy:
.chunk:
    mov cx, bp
    jcxz .fin
    cmp cx, 2048
    jbe .have
    mov cx, 2048
.have:
    sub bp, cx
    mov dx, cx
    pushf
    cli
    push ds
    push es
cpu 386
    mov eax, [rms_src]
    mov ebx, [rms_dst]
    movzx ecx, cx
    add [rms_src], ecx
    add [rms_dst], ecx
    call ub_set                 ; DS = the source, ES = the destination
    xor si, si
    xor di, di
    mov cx, dx
    shr cx, 2
    cld
    rep movsd
    test dl, 2
    jz .cd
    movsw
.cd:
cpu 286
    pop es
    pop ds
    popf
    jmp short .chunk
.fin:
    ret

; rms_lcopy - XMEM.DRV's tier-1 transport by LOADALL (xm_lcopy, SPEC.md
; 41.13): 16 KB a borrow (XM_LPIECE's measured trade), interrupts off for
; each. DS = CS. Clobbers AX, BX, CX, DX, SI, DI, BP
rms_lcopy:
    cmp byte [rms_la], 0
    jne .piece
    call la_capture             ; the MSW, IDTR and GDTR of THIS bracket
    mov byte [rms_la], 1
.piece:
    mov cx, bp
    jcxz .fin
    cmp cx, 16384
    jbe .have
    mov cx, 16384
.have:
    sub bp, cx
    mov ax, [rms_src]
    mov dx, [rms_src+2]
    mov di, la_dsc
    mov bx, la_dsv
    call rms_lend
    push ax
    mov ax, [rms_dst]
    mov dx, [rms_dst+2]
    mov di, la_esc
    mov bx, la_esv
    call rms_lend
    xchg di, ax
    pop si
    mov ax, [la_dsv]            ; the extended end shows the conventional
    or ax, [la_esv]             ; end's selector
    mov [la_dsv], ax
    mov [la_esv], ax
    push cx
    shr cx, 1
    pushf
    cli
    call la_borrow
    rep movsw
    call la_restore             ; DS = ES = ours again
    popf
    pop cx
    add [rms_src], cx
    adc word [rms_src+2], 0
    add [rms_dst], cx
    adc word [rms_dst+2], 0
    jmp short .piece
.fin:
    ret

; rms_lend - xm_lend: one end of a piece, DX:AX its linear address, DI its
; cache dword, BX its visible selector word. Clobbers AX, DX
rms_lend:
    cmp dx, 0x0010
    jae .ext
    push cx
    push ax
    and ax, 0xFFF0
    mov [di], ax
    mov [di+2], dx
    shr ax, 4
    shl dx, 12
    or ax, dx
    mov [bx], ax
    pop ax
    and ax, 0x000F
    pop cx
    ret
.ext:
    mov [di], ax
    mov [di+2], dx
    mov word [bx], 0
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; rms_drvx - OSAPI_DRV_CALL's TWIN (an X cell: ES = your DS); rms_drv -
; OSAPI_DRV_CALL_AT's (ES as you set it). BH = the class: a driver the door
; carried low (rms_drv's rows) is called at its real paragraph, through its
; DSV_PKGCALL, with DRVC_EMS handed your slot in BH as drv_pkg_call_x hands
; it; any other class answers CF = 1 AX = 0, as for no driver. BP, DS, ES
; and DI come back yours
; -----------------------------------------------------------------------------
rms_drvx:
    push es
    push ds
    pop es
    call rms_drvb
    pop es
    retf
rms_drva:
    call rms_drvb
    retf
rms_drvb:
    ; STKBALANCE-OK: the driver is entered by a CONSTRUCTED far call (four pushes and a `retf`) that its dispatcher returns from at .back, where the three banked words come off - drv_pkg_disp's shape, in real mode
    push bp
    push ds
    push di
    mov di, rms_drv
    mov bp, RMD_N
.f:
    cmp [cs:di+RMD_CLS], bh
    je .got
    add di, RMD_SIZE
    dec bp
    jnz .f
.none:
    pop di
    pop ds
    pop bp
    xor ax, ax
    stc
    ret
.got:
    mov bp, [cs:di+RMD_PKG]
    or bp, bp
    jz .none
    cmp bh, DRVC_EMS
    jne .c
    mov bh, [cs:rms_slot]       ; EMS keys its pages on the CALLER (107.1)
.c:
    push cs
    push word .back
    push word [cs:di+RMD_SEG]
    push word PKG_DISP
    mov ds, [cs:di+RMD_SEG]
    mov di, sp
    mov di, [ss:di+8]           ; the caller's DI again: an argument
    retf
.back:
    pop di
    pop ds
    pop bp
    ret

; -----------------------------------------------------------------------------
; THE SOUND DRIVER'S TWINS (W4, SPEC.md 53.2.3): its row is rms_drv's third,
; written by fsx_rdrvf when the list carried DRVC_SOUND, and all zero when it
; did not - so every one of these asks its row and nothing else.
; -----------------------------------------------------------------------------
; rms_stick - FAR, from fsx_rtick's tick (IF = 0, DS = KERNEL_SEG): snd_tick's
; DSV_TICK half, through the low copy - DS = it, ES = KERNEL_SEG, as
; drv_svc_call hands it. DS kept; every other register spent (fsx_rtick
; banked them)
rms_stick:
    mov di, rms_drv + RMD_SND
    mov bp, [cs:di+RMD_TICK]
    or bp, bp
    jz .o
    push ds
    pop es
    call .t
.o:
    retf
.t:
    push bp
    push ds
    push di
    jmp rms_drvb.c

; rms_sstr - OSAPI_SND_STREAM's TWIN (an X cell: the twin is entered with the
; caller's DS): osapi_snd_stream's registers - BX the caller's segment for
; verbs 5 and 6 and the RATE for 0 and 4, DH the bracket's instance, DX the
; answer on verbs 3 and 9 and kept on every other - to the low copy's
; DSV_STREAM. No sound row: AX = 4, CF = 1, "no streaming sink"
rms_sstr:
    push bx
    push bp
    push es
    mov bx, ds                  ; the caller's segment (verbs 5 and 6)
    cmp al, 3
    je .st
    cmp al, 9                   ; (verb 9 answers in DX as verb 3 does)
    je .st
    push dx
    cmp al, 0
    je .rate
    cmp al, 4
    jne .go
.rate:
    mov bx, dx                  ; the open verbs: BX the rate, DH the slot
.go:
    call .svc
    pop dx
.pops:
    pop es
    pop bp
    pop bx
    retf
.st:
    call .svc                   ; DX is the answer: nothing banks it
    jmp short .pops
.svc:
    mov dh, [cs:rms_slot]
    mov bp, KERNEL_SEG
    mov es, bp
    push bp
    push ds
    push di
    mov di, rms_drv + RMD_SND
    mov bp, [cs:di+RMD_STR]
    or bp, bp
    jnz rms_drvb.c
    pop di
    pop ds
    pop bp
    mov ax, 4                   ; no streaming sink, in the stream verbs'
    stc                         ; own vocabulary
    ret

; rms_spk - OSAPI_FSX_SPK's TWIN: AL = 2 OPEN WITH THE VECTOR - osapi_fsx_spk's
; arbitration and trim, then IVT 20h (IRQ0 here: the 8259 at its protected-
; mode base) := BX:SI, CX:DX = the chain, the real-mode tick. AL = 1 CLOSE,
; spk_off's: IVT 20h the tick, channel 0 at the bracket's rate, channel 2
; idle. AL = 0 - the caller writing IVT 08h - REFUSED, AX = 1, as protected
; mode refuses it; a tone sounding refuses AX = 3 (its silencing may reach
; the card's driver through `.cold`). IF = 0 by the caller, as the door
; demands. Out: AX = K; BX, DX spent; CF
rms_spk:
    push ds
    push ax
    mov ax, KERNEL_SEG
    mov ds, ax
    pop ax
    cmp al, 1
    je .close
    cmp al, 2
    mov ax, 1                   ; SPK_E_NOTRATE: AL = 0 here, or no rate
    jne .err
    cmp byte [sch_fast], SCH_RATE
    jne .err
    mov al, 3                   ; SPK_E_BUSY
    cmp byte [snd_pcm_busy], 0
    jne .err
    cmp byte [snd_town_act], 0
    jne .err
    mov [spk_seg], bx           ; the open flag, and the ISR's segment
    mov word [snd_ch2mode], 0x0102 ; channel 2 the ISR's (mode 2, busy 1)
    mov [spk_n], dx
    mov al, 0x90                ; spk_pwm_on: ch2 mode 0, lobyte...
    out 0x43, al
    in al, 0x61                 ; ...gate and speaker on, the rest kept
    or al, 0x03
    out 0x61, al
    mov ax, [sch_rdiv]          ; the bracket's divisor, banked for the close
    mov [spk_rdsv], ax
    push bx
    mov bx, dx
    xor dx, dx
    div bx                      ; AX = K, DX = the remainder: the period is
    sub [sch_rdiv], dx          ; K x N exactly
    pop bx
    push es
    xor dx, dx
    mov es, dx
    mov [es:MEGA_PIC_BASE*4], si ; IRQ0's vector, in real mode
    mov [es:MEGA_PIC_BASE*4+2], bx
    pop es
    mov cx, KERNEL_SEG          ; ...and the chain: the real-mode tick
    mov dx, fsx_rtick
    clc
    pop ds
    retf
.err:
    stc
    pop ds
    retf
.close:
    cmp word [spk_seg], 1       ; CF = 1: nothing open
    jb .cd
    pushf
    cli
    push es
    xor ax, ax
    mov [spk_seg], ax
    mov es, ax
    mov word [es:MEGA_PIC_BASE*4], fsx_rtick
    mov word [es:MEGA_PIC_BASE*4+2], KERNEL_SEG
    pop es
    mov ax, [spk_rdsv]          ; channel 0 at the bracket's rate (sch_pit0)
    mov [sch_rdiv], ax
    push ax
    mov al, 0x34
    out 0x43, al
    pop ax
    out 0x40, al
    mov al, ah
    out 0x40, al
    in al, 0x61                 ; ...and channel 2 idle (spk_pcm_idle's ports
    and al, 0xFC                ; and record: no stream verb 8, which is a
    out 0x61, al                ; call into the driver)
    mov al, 0xB6
    out 0x43, al
    mov word [snd_ch2mode], 0
    popf
.cd:
    pop ds
    retf
align 16
rms_hi:
; ovlchk: end real mode
rms_strm    equ rms_hi          ; the copy's stream rows (FSX_RSR's)...
rms_runs    equ rms_strm + FSX_RSN ; ...and runs
cpu 8086


section .lowbss                 ; PROTECTED-MODE STATE, so out of the kernel
                                ; segment (tests/megaseg.txt): only `.mega`
                                ; reads it, through SS
; the bank fsx_rvdn fills and fsx_rvup empties: mega_vwalk's words, in its
; order - VSEG's four, vid_tab's column, the live block's three, each
; context's three, the full-screen rows, the glyph table (and the band)
%ifdef BANDCOMP
FSX_RBN     equ 4 + (vid_tab_end - vid_tab) / VID_REC + 3 + 3 * VID_NDISP_MAX + FSXM_COUNT + 2
%else
FSX_RBN     equ 4 + (vid_tab_end - vid_tab) / VID_REC + 3 + 3 * VID_NDISP_MAX + FSXM_COUNT + 1
%endif
fsx_rbank   resw FSX_RBN
fsx_rhi     resw 2              ; the region's linear base, above 1MB
fsx_rlo     resw 2              ; ...its copy's, below
fsx_rlen    resw 1              ; ...the bytes copied, even
fsx_rlow    resw 1              ; ...the copy's claim (its data selector), 0
                                ; = none: the region runs where it is
fsx_rown    resw 1              ; ...and its owner, the instance's slot
fsx_rexfp   resw 2              ; EXTD.DRV's slots' far pointer, mod_gone's
fsx_rsvc    resw 1              ; the SERVICE AREA's claim (W3), 0 = none -
fsx_rstko   resw 1              ; ...rms_stick, the tick's far call (W4)...
fsx_rmsseg  resw 1              ; ...and the paragraph its twins run at
%if fsx_rmsseg - fsx_rstko != 2
 %error "fsx_rtick's far call is the dword fsx_rstko:fsx_rmsseg"
%endif
%ifdef VID_EXT
fsx_rvxep   resw 2              ; ...and [vx_ep], the VESA desktop's hook
%endif
fsx_rserb   resw 1              ; a SERIAL.DRV lease's ring copy (W7): its
                                ; offset in the service area's claim, 0 = no
                                ; lease (fsx_rsvcf)
fsx_rfont   resw 1              ; the GLYPH TABLE's copy in the service area
                                ; (KERN-MEGA-PLAN 14.131): its offset in the
                                ; claim, 0 = none - the table is not above
                                ; 1MB (fsx_rsvcf)
fsx_rfbase  resw 1              ; ...and [font_base], banked at the drop
fsx_rsfp    resw 1              ; ...SERFP's code selector, banked
fsx_rsrsg   resw 1              ; ...and the module's srl_seg and srl_buf -
fsx_rsrbf   resw 1              ; the OWNER's ring - banked
section .text
%endif
