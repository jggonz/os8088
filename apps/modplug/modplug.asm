; =============================================================================
; os8088 - apps/modplug/modplug.asm
;
; ModPlug Player, the fourteenth software package (SPEC.md 56): a port of
; ModPlug Player V2's LOOK AND FEEL - the skinned player window, the Setup
; window and the PlayList editor - onto the os8088 window manager, playing
; ProTracker modules through the SPEC.md 34 sound layer.
;
; What was ported and what was not, because the difference is the whole
; design. ModPlugPlayer (github.com/ModPlugPlayer, GPLv3) is a Qt6
; application: its interface is nine .ui files, and its PLAYBACK is
; libopenmpt, with PortAudio underneath and KissFFT/FFTW driving the
; spectrum analyser. None of those four libraries is a thing a 4.77 MHz 8086
; runs. So:
;
;   - The INTERFACE is ported, closely and deliberately: the bevelled body,
;     the green LCD panel, the LED transport row, the time scrubber, the
;     volume slider, the option-button grid, the three visualisers, and both
;     secondary windows including Setup's left-hand page list. Where a
;     control exists in ModPlugPlayer and cannot work here, it is DRAWN and
;     greyed with its reason in the label (SPEC.md 47's rule 5) rather than
;     quietly dropped - the shape of the app is the thing being ported.
;   - The REPLAYER is this tree's own (mppmix.inc, and see its banner for the
;     provenance): an independent copy of the proven 8086 ProTracker engine,
;     extended with the four DSP stages ModPlugPlayer's Setup pages actually
;     expose and that this hardware can actually run.
;   - The SPECTRUM ANALYSER is not an FFT and does not claim to be. mppui.inc
;     projects the four channels onto bands by period; SPEC.md 56.6 is the
;     honest statement of what that is and is not.
;
; Three windows, one instance. The player window is the BOUND one (the entry
; proc returns it), so its close box tears the instance down. Setup and
; PlayList are created later and never bound, which means wm_owner never
; names them - so their close and minimize boxes both reduce to a plain
; wm_hide, exactly like the kernel's own Standard File dialog (SPEC.md 38),
; and reopening is just wm_show. Teardown sweeps them by the W_SEG creator
; stamp (SPEC.md 20.2); this file has no close-path code at all.
;
; The audio half is the Tracker's architecture (SPEC.md 45.2) because it is
; the architecture the sound layer was built for: the UI task opens, closes
; and pre-mixes; the worker mixes and feeds a ring stream; and every close
; DRAINS the worker's in-flight pass ([mpp_mixing]) before the module blob
; can move. mpm_gen is not reentrant, and that drain is the only thing
; standing between a mid-fetch mixer and a freed heap claim.
;
; Window procs run with the gfx lock held and preserve all registers. Every
; callback is a NEAR proc with a near `ret` - the kernel reaches us through
; our own header dispatcher - so there is no `retf` anywhere in this package.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MODPLUG', mpp_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; The player's face: a bevelled box with an LCD band across the top and a
; row of transport buttons under it. The mask is the silhouette dilated 1px.
;
;   data                          mask
    OS88_ICON16
    dw 0000000000000000b        ; mask row 0
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0000000000000000b

    dw 0000000000000000b        ; data row 0
    dw 0011111111111100b        ; body top
    dw 0010000000000100b
    dw 0010111111101000b        ; LCD band
    dw 0010100000101000b
    dw 0010101110101000b
    dw 0010100000101000b
    dw 0010111111101000b
    dw 0010000000000100b
    dw 0010110110110100b        ; transport buttons
    dw 0010100100100100b
    dw 0010110110110100b
    dw 0010000000000100b
    dw 0011111111111100b
    dw 0000000000000000b
    dw 0000000000000000b
    OS88_ICON16_END

; --- the package-wide bss macros (the Arkanoid %assign pattern) ----------------
; Pinned interface: defined HERE, at the top, before any %include, so all
; four files declare bss through the same accumulator. MPPB = byte,
; MPPW = word, MPPBUF = named buffer.
%assign MPP_BSS 0
%macro MPPB 1
%1 equ os88_image_end + MPP_BSS
%assign MPP_BSS MPP_BSS + 1
%endmacro
%macro MPPW 1
%1 equ os88_image_end + MPP_BSS
%assign MPP_BSS MPP_BSS + 2
%endmacro
%macro MPPBUF 2
%1 equ os88_image_end + MPP_BSS
%assign MPP_BSS MPP_BSS + (%2)
%endmacro

; --- the stream (SPEC.md 34.5 ring mode; the Tracker's numbers) ----------------
MPP_RING    equ 16384               ; ring grant, bytes: a power of two in
                                    ; 4096..32768 per the ring-mode contract
MPP_RMASK   equ MPP_RING - 1
MPP_HALF    equ 2048                ; the fill unit, pinned: fills are whole
                                    ; halves, and 16384 is a multiple of it,
                                    ; so a half never crosses the ring seam
MPP_RATE    equ 11000               ; Setup > Audio > Frequency, the default
MPP_RATE22  equ 22050
MPP_RATE44  equ 44100               ; the SPEC.md 34.5 wide-rate regime: a
                                    ; DSP 4.x card, and an honest refusal on
                                    ; anything older
MPP_RATE_XT equ 5500                ; XT mode's own rate (SPEC.md 56.7)
MPP_MAXFEED equ 6                   ; halves per worker wake: bounds the
                                    ; lock-free burst at ~1.1 s of mixing

; --- the player window ---------------------------------------------------------
; ModPlug Player V2's own window is 417x226. The content width is kept; the
; height is the one thing that cannot be: CGA's desktop band is 156 rows
; (SPEC.md 39), so the full layout does not fit and mppui.inc has a compact
; one. mpp_entry picks between them from OSAPI_VIDEO - see MPPU_* there.
MPP_CW      equ 416                 ; content width, both layouts
MPP_CHF     equ 196                 ; content height, full (VGA / Hercules)
MPP_CHC     equ 132                 ; ...compact (CGA)
MPP_FW      equ MPP_CW + 2          ; frame = content + the 1px borders...
MPP_FHF     equ MPP_CHF + TITLE_H + 1   ; ...and the title bar
MPP_FHC     equ MPP_CHC + TITLE_H + 1

; --- the playlist (SPEC.md 56.8) -----------------------------------------------
MPP_PLMAX   equ 16                  ; entries; 13 bytes each, all in bss
MPP_PLENT   equ 13                  ; an 8.3 name and its NUL

; --- control ids, as mppu_hit answers them (SPEC.md 56.3) ----------------------
; Up here with the other constants rather than beside the tables they index,
; because mpp_onclick compares against them and a forward-referenced equ in an
; instruction operand is exactly the shape NASM will not always fold.
MPPC_TRANS_N equ 9                  ; 0..8 = the transport row
MPPC_OPT0    equ 16                 ; the option-button block: +0 .. +11
MPPC_SCRUB   equ 40                 ; the time scrubber (BX = a song position)
MPPC_VOL     equ 41                 ; the volume slider (BX = 0..64)
MPPC_SCOPE   equ 42                 ; a VU cell (BX = the channel to mute)
MPPO_COUNT   equ 12                 ; option buttons, full layout
MPPO_COMPACT equ 6                  ; ...and the ones the compact layout shows:
                                    ; the first six, the three visualiser
                                    ; toggles being on the View menu and on G

; --- repeat modes and visualisers ---------------------------------------------
MPPR_OFF      equ 0
MPPR_SONG     equ 1
MPPR_LIST     equ 2
MPPV_SPECTRUM equ 0
MPPV_SCOPE    equ 1
MPPV_VU       equ 2

; =============================================================================
; Entry (SPEC.md 20.2): pick the layout, create the player window, register
; the menus and About. No drawing, no spawn, no lock here.
; =============================================================================
mpp_entry:
    push si
    push di
    call mpm_init                   ; first: mpm_* may clobber freely, and the
                                    ; CF we owe the loader comes from wm_create

    call OSAPI_CPU_INFO             ; AL = tier (SPEC.md 41.8). A tier-0
    or al, al                       ; machine gets XT mode pre-armed with its
    jnz .cpu                        ; menu item already relabeled: the machine
    mov byte [mpp_cpu0], 1          ; this mode exists for should not have to
    mov al, 1                       ; go and find the toggle (SPEC.md 56.7)
    call mpm_setxt                  ; ...through the setter, so the DSP tail is
    mov word [mpp_mi_play + 14], mpp_s_xton  ; forced off with it (item 7 of a
.cpu:                               ; dw array is at +14, not +12)
    call OSAPI_GET_TICKS            ; seed the shuffle: the tick count at the
    call OSAPI_SRAND                ; moment a user launched the app is as
                                    ; good an entropy source as this machine
                                    ; has, and a fixed seed would make every
                                    ; session shuffle to the same order
    call OSAPI_VIDEO                ; AX = w, BX = h, CX = first dock row
    mov [mpp_scrw], ax
    mov [mpp_docky], cx
    mov ax, cx                      ; the desktop band this machine has
    sub ax, MBAR_H
    mov word [mpp_tpl + WT_H], MPP_FHF
    mov byte [mpp_compact], 0
    cmp ax, MPP_FHF                 ; does the full layout fit in it?
    jae .fits
    mov word [mpp_tpl + WT_H], MPP_FHC
    mov byte [mpp_compact], 1       ; CGA: the compact layout (SPEC.md 56.4)
.fits:
    mov ax, [mpp_scrw]              ; centre the frame on the screen...
    sub ax, MPP_FW
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [mpp_tpl + WT_X], ax
    mov ax, [mpp_docky]             ; ...and in the desktop band; wm_fit
    sub ax, MBAR_H                  ; clamps whatever does not fit
    sub ax, [mpp_tpl + WT_H]
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, MBAR_H
    mov [mpp_tpl + WT_Y], ax

    mov si, mpp_tpl
    call OSAPI_WM_CREATE            ; out BX = win ptr, CF on full
    jc .out
    mov [mpp_win], bx
    mov si, mpp_menus
    call OSAPI_MENU_SET             ; preserves registers AND flags, so the
    mov si, mpp_about               ; loader's CF survives to the ret
    call OSAPI_ABOUT_SET

    ; --- ...and the two layouts become a DECLARATION (SPEC.md 11.100.1) ------
    ; The band test above still decides the size this window OPENS at, and the
    ; three entries below say the same thing, so nothing about a launch moves.
    ; What they add is every LATER moment the adapter can change under us -
    ; the Display page's Activate Mode, and a drag onto the other card on an
    ; extended desktop (11.100.4) - where before this the face stayed the size
    ; it was born and [mpp_compact] stayed whatever it was decided to be.
    mov bx, [mpp_win]
    mov si, mpp_pref
    call OSAPI_WM_PREFER            ; preserves the flags, like the two above
    mov ax, mpp_onresize
    call OSAPI_WM_ONRESIZE          ; ...and tell us, so the flag can follow
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; The two layouts, as the three sizes they are (SPEC.md 11.100.1). The heights
; are the ones mpp_entry's band test picks on each adapter, which is why
; declaring them changes nothing about how the window opens.
; -----------------------------------------------------------------------------
    OS88_PREFER mpp_pref, MPP_FW, MPP_FHF,  MPP_FW, MPP_FHF,  MPP_FW, MPP_FHC

; -----------------------------------------------------------------------------
; mpp_onresize - the box moved under us, so re-pick the layout (SPEC.md 11.98)
; in:  the gfx lock is HELD and this MUST NOT DRAW - it decides, and the paint
;      that follows reads what it decided
; out: nothing; every register preserved
;
; [mpp_compact] was set once at launch from the desktop band, and it is what
; mppu_layout copies a whole coordinate table out of. An adapter change or a
; drag onto the other card moves the box and left that byte describing the
; card the window used to be on: the compact face drawn into a full-size frame,
; or worse the full face into a 151-row one, where the bottom of the spectrum
; pane is outside the window and the gfx primitives clip to the SCREEN.
;
; It reads the CONTENT height rather than the adapter, because that is what the
; layout is a function of - the same number mppu_layout itself asks for.
; -----------------------------------------------------------------------------
mpp_onresize:
    push ax
    push bx
    push cx
    push dx
    mov bx, [mpp_win]
    call OSAPI_WM_GEOM              ; out CX = content w, DX = content h
    xor al, al
    cmp dx, MPP_CHF
    jae .set
    mov al, 1                       ; not tall enough for the full face
.set:
    cmp al, [mpp_compact]
    je .out                         ; the same answer: nothing to redraw for
    mov [mpp_compact], al
    mov byte [mppu_ok], 0           ; the coordinate table is the OTHER
                                    ; layout's, so mppu_layout must copy the
                                    ; new one in rather than keep its origin
                                    ; check - and MPPI_BODY, because every
                                    ; element on the face has moved
    mov ax, MPPI_BODY
    call mppu_inval
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The player window's callbacks
; =============================================================================

; =============================================================================
; MPPDBG - the breadcrumb (build with `make modplugdbg`, ships nowhere)
;
; A reported hard freeze on the 'L' key that no emulator here reproduces: it
; needs a hard disk MOUNTED, ModPlug rather than any other loader, and a
; pristine post-boot heap (File > Open first, and 'L' stops freezing). So the
; instrument has to run on the REPORTER's machine, survive a machine that has
; stopped, and need no tooling at the other end - which means the screen.
;
; MPPDBG n paints a black bar n cells long on the bare desktop at the LEFT
; edge, under the menu bar. It is drawn from x = 0 every time, so a longer bar
; simply overwrites a shorter one and the picture is always "the highest step
; reached". Read it by counting 8-pixel blocks in a photograph.
;
; The band is chosen so that nothing on this path can erase it: ModPlug's own
; frame starts at x = 111 on a 640px screen and the Standard File dialog at
; x = 87, so x = 0..79 is desktop that neither covers, and the row is below
; MBAR_H so the menu bar cannot reach it either.
;
; It draws through OSAPI_GFX_FILL, which is legal here for the reason the
; whole path is: every one of these sites runs inside a window callback with
; the gfx lock already held (SPEC.md 20.3 rule 7).
; =============================================================================
MPPDBG_Y    equ MBAR_H + 4          ; the first desktop row nothing here uses
MPPDBG_H    equ 8

%macro MPPDBG 1
%ifdef MPPDEBUG
    push ax
    push bx
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    xor ax, ax                      ; x1 = 0: always from the left edge, so a
    mov bx, MPPDBG_Y                ; later step simply covers an earlier one
    mov cx, (%1) * 8 - 1
    mov dx, MPPDBG_Y + MPPDBG_H - 1
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
%endif
%endmacro

; -----------------------------------------------------------------------------
; mpp_paint - W_PAINT: layout (once), worker hire (retried), full redraw
; in:  SI = window ptr; caller holds the gfx lock
; -----------------------------------------------------------------------------
mpp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mpp_reap                   ; watchdog/F00 leftovers close on any UI
    call mppu_layout                ; event (SPEC.md 56.2)
    call mpp_hire                   ; idempotent; a refusal is transient, so
                                    ; it is retried every paint, never latched
    mov ax, MPPI_ALL                ; the kernel ordered this one: our content
    call mppu_inval                 ; is gone, so nothing recorded describes it
    call mppu_refresh
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_hire - spawn the worker, once (the ark_hire shape)
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
; -----------------------------------------------------------------------------
mpp_hire:
    push ax
    push bx
    cmp byte [mpp_hired], 0
    jne .out
    mov al, 1                       ; PARK-SAFE (SPEC.md 66.5.4): the worker's
    call OSAPI_MEM_PARKSAFE         ; ONE lock site is mpp_render, which runs
                                    ; AFTER mpp_feed has returned - so no
                                    ; mixer pointer is live across it, and
                                    ; what mppu_frame draws from is bss
                                    ; (mpm_chans, mpp_lcdname) rather than the
                                    ; module. A worker suspended INSIDE a feed
                                    ; pass is a different matter and is not
                                    ; this: [mpp_mixing] and the ALIVE park
                                    ; already keep a compaction out of one
    mov ax, mpp_worker
    mov bx, [mpp_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [mpp_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_onkey - W_ONKEY. The transport row is Winamp's and ModPlug Player's own
;             Z X C V B, which is what a user of either already has in their
;             fingers; everything else is the initial of the thing it does.
;
;   Z prev   X play   C pause   V stop   B next
;   L Load...        P PlayList Editor   E Setup...
;   R repeat         H shuffle           T XT mode      G cycle visualiser
;   1..4 channel mute
;   Left/Right seek by song position     Up/Down master volume
;
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
mpp_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax                      ; BL = ascii, BH = scan
    MPPDBG 1                        ; 1: W_ONKEY was dispatched at all
    call mpp_reap
    MPPDBG 2                        ; 2: mpp_reap returned (the .drain spin in
                                    ;    mpp_stream_close is the one unbounded
                                    ;    wait in this package)
    call mpp_abdismiss              ; any key takes the About panel down and
    jc .done                        ; is spent doing it
    MPPDBG 3                        ; 3: past the About dismissal
    or bl, bl                       ; the SPEC.md 44.2 keypad gate: the numeric
    jnz .ascii                      ; keypad sends digits with ARROW scan
                                    ; codes, so ascii is tested before any
                                    ; scan code is trusted
    cmp bh, 0x4B
    je .seekb
    cmp bh, 0x4D
    je .seekf
    cmp bh, 0x48
    je .volup
    cmp bh, 0x50
    je .voldn
    jmp .done
.seekb:
    mov al, -1
    call mpm_setpos
    jmp .redraw
.seekf:
    mov al, 1
    call mpm_setpos
    jmp .redraw
.volup:
    mov al, [mpm_master]
    add al, 8
    cmp al, 64
    jbe .volset
    mov al, 64
    jmp .volset
.voldn:
    mov al, [mpm_master]
    sub al, 8
    jnc .volset
    xor al, al
.volset:
    mov [mpm_master], al
    jmp .redraw

.ascii:
    cmp bl, 'A'                     ; one case-fold instead of a pair of
    jb .k                           ; compares per letter
    cmp bl, 'Z'
    ja .k
    or bl, 0x20
.k:
    cmp bl, 'z'
    je .prev
    cmp bl, 'x'
    je .play
    cmp bl, 'c'
    je .pause
    cmp bl, 'v'
    je .stop
    cmp bl, 'b'
    je .next
    cmp bl, 'l'
    je .open
    cmp bl, 'p'
    je .plist
    cmp bl, 'e'
    je .setup
    cmp bl, 'r'
    je .rep
    cmp bl, 'h'
    je .shuf
    cmp bl, 't'
    je .xt
    cmp bl, 'g'
    je .viz
    cmp bl, ' '
    je .space
    cmp bl, 13
    je .play
    cmp bl, '1'
    jb .done
    cmp bl, '4'
    ja .done
    mov al, bl                      ; 1..4: channel mute, the scope cells' own
    sub al, '1'                     ; click by another name
    call mpm_mutetog
    jmp .redraw
.space:
    cmp byte [mpm_playing], 0
    je .play
    jmp .pause
.prev:
    call mpp_pl_prev
    jmp .redraw
.next:
    call mpp_pl_next
    jmp .redraw
.play:
    call mpp_play
    jmp .redraw
.pause:
    call mpp_pause
    jmp .redraw
.stop:
    call mpp_stop
    jmp .redraw
.open:
    MPPDBG 4                        ; 4: 'l' decoded and dispatched to Open
    call mpp_do_open
    MPPDBG 9                        ; 9: the whole thing came back
    jmp .done
.plist:
    call mppl_toggle
    jmp .done
.setup:
    call mpps_toggle
    jmp .done
.rep:
    mov al, [mpp_repeat]            ; Off -> Song -> List -> Off
    inc al
    cmp al, 3
    jb .repset
    xor al, al
.repset:
    mov [mpp_repeat], al
    jmp .redraw
.shuf:
    xor byte [mpp_shuffle], 1
    jmp .redraw
.xt:
    call mpp_xt_toggle
    jmp .done
.viz:
    mov al, [mpp_viz]
    inc al
    cmp al, 3
    jb .vizset
    xor al, al
.vizset:
    mov [mpp_viz], al
    call mppu_viz_reset
.redraw:
    call mppu_refresh
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_onclick - W_ONCLICK: everything on the face is clickable.
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
mpp_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mpp_reap
    call mpp_abdismiss              ; a click takes the About panel down too
    jc .done
    call mppu_hit                   ; CF=1 nothing; else AL = control id, and
    jc .done                        ; for the two sliders BX = the new value
    cmp al, MPPC_TRANS_N            ; 0..8: the transport row
    jb .trans
    cmp al, MPPC_SCRUB
    je .scrub
    cmp al, MPPC_VOL
    je .vol
    cmp al, MPPC_SCOPE
    je .scope
    sub al, MPPC_OPT0               ; else an option button
    call mpp_option
    jmp .redraw
.trans:
    call mpp_transport_btn
    jmp .redraw
.scrub:
    mov al, bl                      ; the scrubber answers an ABSOLUTE order
    call mpm_setposn                ; position, not a delta
    jmp .redraw
.vol:
    mov [mpm_master], bl
    jmp .redraw
.scope:
    mov al, bl                      ; a click in a VU cell mutes that channel
    call mpm_mutetog
.redraw:
    call mppu_refresh
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_transport_btn - one of the nine LED buttons
; in:  AL = 0..8 (Open Prev Rew Play Pause Stop FFwd Next Setup); lock held
; -----------------------------------------------------------------------------
mpp_transport_btn:
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    call [mpp_transtab + bx]        ; a jump table, so the buttons and the
    pop bx                          ; Playback menu cannot drift apart
    ret

mpp_t_open:
    call mpp_do_open
    ret
mpp_t_rew:
    mov al, -1
    call mpm_setpos
    ret
mpp_t_ffwd:
    mov al, 1
    call mpm_setpos
    ret
mpp_t_setup:
    call mpps_toggle
    ret

; -----------------------------------------------------------------------------
; mpp_option - one of the option buttons (SPEC.md 56.3)
; in:  AL = the option index; gfx lock held
; -----------------------------------------------------------------------------
mpp_option:
    push ax
    push bx
    push si
    cmp al, MPPO_COUNT
    jae .out
    mov bl, al
    xor bh, bh
    shl bx, 1
    call [mpp_opttab + bx]
.out:
    pop si
    pop bx
    pop ax
    ret

mpp_o_repeat:
    mov al, [mpp_repeat]
    inc al
    cmp al, 3
    jb .set
    xor al, al
.set:
    mov [mpp_repeat], al
    ret
mpp_o_shuffle:
    xor byte [mpp_shuffle], 1
    ret
mpp_o_plist:
    call mppl_toggle
    ret
mpp_o_amiga:
    cmp byte [mpm_xt], 0            ; XT mode owns the DSP tail: a refused
    jne .no                         ; click on a greyed control says nothing
    mov al, [mpm_amiga]             ; more than the greying already did
    inc al                          ; (SPEC.md 47 rule 6)
    cmp al, 3
    jb .set
    xor al, al
.set:
    mov [mpm_amiga], al
    mov word [mpm_flt], 0           ; a filter that changes pole starts from
.no:                                ; silence, not from the old pole's state
    ret
mpp_o_dither:
    cmp byte [mpm_xt], 0
    jne .no
    xor byte [mpm_dither], 1
.no:
    ret
mpp_o_xt:
    call mpp_xt_toggle
    ret
mpp_o_scope:
    mov byte [mpp_viz], MPPV_SCOPE
    call mppu_viz_reset
    ret
mpp_o_spectrum:
    mov byte [mpp_viz], MPPV_SPECTRUM
    call mppu_viz_reset
    ret
mpp_o_vu:
    mov byte [mpp_viz], MPPV_VU
    call mppu_viz_reset
    ret
mpp_o_info:
    xor byte [mpp_info], 1          ; the LCD's third line switches between
    ret                             ; the transport readouts and the module's
                                    ; sample list (SPEC.md 56.3)
mpp_o_setup:
    call mpps_toggle
    ret
mpp_o_about:
    call mpp_about
    ret

; -----------------------------------------------------------------------------
; mpp_oncmd - AM_ONCMD (SPEC.md 12.2). Every command here is also a key and
;             most are also a button; the menu is the discoverable copy.
; in:  AL = item, AH = menu, SI = our window, BX = the set; gfx lock held
; -----------------------------------------------------------------------------
mpp_oncmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax                      ; BL = item, BH = menu
    call mpp_reap
    call mpp_abdismiss
    jc .done
    or bh, bh
    je .file
    cmp bh, 1
    je .play
    cmp bh, 2
    je .view
    jmp .done

.file:                              ; File: Open..., PlayList Editor, Setup...
    or bl, bl
    je .f_open
    cmp bl, 1
    je .f_plist
    call mpps_toggle
    jmp .done
.f_open:
    call mpp_do_open
    jmp .done
.f_plist:
    call mppl_toggle
    jmp .done

.play:                              ; Playback: Play Pause Stop Prev Next
    or bl, bl                       ;           Repeat Shuffle XT Mode
    je .p_play
    cmp bl, 1
    je .p_pause
    cmp bl, 2
    je .p_stop
    cmp bl, 3
    je .p_prev
    cmp bl, 4
    je .p_next
    cmp bl, 5
    je .p_rep
    cmp bl, 6
    je .p_shuf
    call mpp_xt_toggle
    jmp .done
.p_play:
    call mpp_play
    jmp .redraw
.p_pause:
    call mpp_pause
    jmp .redraw
.p_stop:
    call mpp_stop
    jmp .redraw
.p_prev:
    call mpp_pl_prev
    jmp .redraw
.p_next:
    call mpp_pl_next
    jmp .redraw
.p_rep:
    call mpp_o_repeat
    jmp .redraw
.p_shuf:
    call mpp_o_shuffle
    jmp .redraw

.view:                              ; View: the three visualisers
    mov [mpp_viz], bl
    call mppu_viz_reset
.redraw:
    call mppu_refresh
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_about / mpp_abdismiss - the OSAPI_ABOUT_SET panel, the [ark_abon]
; pattern: the worker checks the flag under the lock right after the clip is
; armed and drops the whole frame while it is set. Only the DRAWING pauses -
; the audio feed keeps running, because a dropped frame should not stop music.
; -----------------------------------------------------------------------------
mpp_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [mpp_abon], 1
    mov ax, MPPI_ALL                ; the panel is drawn over the whole face,
    call mppu_inval                 ; so the whole face is what it destroys
    call mppu_refresh              ; draws the panel last, while the flag is up
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mpp_abdismiss:
    cmp byte [mpp_abon], 0
    je .none
    mov byte [mpp_abon], 0
    push ax
    mov ax, MPPI_ALL                ; ...and taking it down is what has to put
    call mppu_inval                 ; every one of those pixels back
    pop ax
    call mppu_refresh
    stc
    ret
.none:
    clc
    ret

; =============================================================================
; Loading (SPEC.md 56.2) - the Standard File dialog, and the completion proc
; =============================================================================

; -----------------------------------------------------------------------------
; mpp_do_open - put up the Standard File dialog (Open mode)
; in:  gfx lock held; preserves all registers. A refusal (one already up) is
; ignored on purpose: the dialog that is up IS the answer.
; -----------------------------------------------------------------------------
mpp_do_open:
    push ax
    push bx
    push si
    push di
    MPPDBG 5                        ; 5: inside mpp_do_open
    mov byte [mpp_addpl], 0         ; a plain Open replaces what is playing
    call mpp_pl_walk_reset
    MPPDBG 6                        ; 6: past the playlist reset
    mov al, FDLG_OPEN
    mov bx, [mpp_win]
    mov di, mpp_fdone
    xor si, si
    MPPDBG 7                        ; 7: about to cross into the KERNEL. A bar
                                    ;    that stops here is the kernel's dialog
                                    ;    open; one that stops earlier is ours
    call OSAPI_FILE_DLG
    MPPDBG 8                        ; 8: OSAPI_FILE_DLG returned
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_fdone - the file-dialog completion proc (SPEC.md 38.6). UI task, gfx
;             lock held, ES:DI = the chosen name with ES = KERNEL_SEG, valid
;             for THIS CALL ONLY - so it is copied out first, before anything.
;             DX:CX = that file's SIZE, which is banked here for the same
;             reason and for a better one (see mpp_load_name).
;
; Two callers with one completion proc, told apart by [mpp_addpl]: the
; player's Open loads and plays, the PlayList editor's Add... appends and
; does not disturb what is playing.
; -----------------------------------------------------------------------------
mpp_fdone:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mpp_fszl], cx              ; the size, before anything can clobber it.
    mov [mpp_fszh], dx              ; 0 = this listing had no such file
    mov si, di                      ; the kernel's buffer dies when this call
    mov di, mpp_fname               ; returns: copy it out NOW
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [mpp_fname + 12], 0
    push ds
    pop es                          ; ES = DS again (the callback default)
    cmp byte [mpp_addpl], 0
    je .load
    mov si, mpp_fname               ; PlayList > Add...: append and repaint
    call mpp_pl_add                 ; both windows, nothing else
    call mppl_redraw
    call mppu_refresh
    xor ax, ax                      ; nothing consumed the banked size on this
    mov [mpp_fszl], ax              ; branch, and a later playlist advance must
    mov [mpp_fszh], ax              ; not inherit some other file's
    jmp .out
.load:
    call mpp_load_name              ; the shared load, used by Open and by
    call mppu_refresh              ; every playlist advance
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_sizeof - the size of [mpp_fname] in the CURRENT directory
;
; in:  [mpp_fname] = a NUL-terminated 8.3 display name
; out: CF = 0 and DX:CX = its size in bytes (mpp_load_name's own convention),
;      CF = 1 = no such file here. Clobbers AX; preserves BX, SI, DI, ES.
;
; The dialog reports a size (SPEC.md 38.6) and a PLAYLIST ADVANCE does not -
; mpp_pl_play calls mpp_load_name with the banked figure already read-and-
; cleared - so every track after the first arrived as "size unknown" and took
; the speculative claim, which is capped. A module past that cap then gets a
; claim it does not fit, and OSAPI_FILE_READ REFUSES it (dskw_rbody checks the
; directory size against the capacity before any data I/O), so an oversized
; track STOPPED the list with 'File too big'. OSAPI_FILE_FIND answers all 32
; bits out of the directory, so the dialog and the playlist size the claim the
; same way and the cap is left to the one case that still cannot know.
;
; It costs one directory walk, on a path that is about to read the whole file.
; -----------------------------------------------------------------------------
mpp_sizeof:
    push bx
    push si
    push di
    push es
    push ds
    pop es                          ; ES:DI = our own record buffer
    xor cx, cx                      ; ordinal 0 starts the walk
.next:
    mov di, mpp_find
    call OSAPI_FILE_FIND            ; CF=1 AX=FERR_NOENT ends it; CX = the
    jc .no                          ; ordinal to ask for next
    cmp word [mpp_find + 14], OSAPI_FT_DIR
    jae .next                       ; a folder or the synthesized '..'
    mov si, mpp_fname
    mov di, mpp_find                ; +0 is the same 8.3 display form
.chr:
    mov al, [si]
    cmp al, [di]
    jne .next
    or al, al
    jz .hit
    inc si
    inc di
    jmp short .chr
.hit:
    mov cx, [mpp_find + 18]         ; +18 = the size, all 32 bits
    mov dx, [mpp_find + 20]
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; mpp_load_name - load [mpp_fname] and start it
; in:  gfx lock held; out: CF=1 on any failure (the message is already set)
;
; Stop playback, free the previous module claim, size a new one from the
; file's real size - the dialog's, or mpp_sizeof's for a playlist advance, and
; only a name in no directory falls back to OSAPI_MEM_AVAIL capped at 128KB
; (SPEC.md 56.14) - read the WHOLE file in one
; OSAPI_FILE_READ - the destination advances by SEGMENT (SPEC.md 18.4.1),
; which is the only reason a 116KB module fits in one call - then trim the
; claim to what the file needed and let mpm_load validate it.
; -----------------------------------------------------------------------------
mpp_load_name:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mpp_play_stop              ; silence + close + DRAIN before the blob
                                    ; moves: mpp_stream_close spins out the
                                    ; worker's in-flight feed pass, so no
                                    ; mpm_mixch is mid-fetch from the old
                                    ; claim past this line ([mpp_mixing])
    mov byte [mpm_loaded], 0        ; the old blob is about to be freed: no
                                    ; reader may trust it past this line
    cmp word [mpp_modseg], 0
    je .alloc
    mov dx, [mpp_modseg]            ; DX, not AX (SPEC.md 50.3)
    call OSAPI_MEM_FREE
    mov word [mpp_modseg], 0
.alloc:
    mov cx, [mpp_fszl]              ; READ-AND-CLEAR the size the dialog gave
    mov dx, [mpp_fszh]              ; us: the next load may be a playlist
    xor ax, ax                      ; advance, which had no dialog and must
    mov [mpp_fszl], ax              ; not inherit this file's figure
    mov [mpp_fszh], ax
    mov ax, cx
    or ax, dx
    jnz .havesz
    call mpp_sizeof                 ; 0 = no dialog size, which here means a
    jc .blind                       ; PLAYLIST ADVANCE: ask the directory
    mov ax, cx                      ; (SPEC.md 56.14). Still nothing -> the
    or ax, dx                       ; old speculative claim
    jz .blind
.havesz:
    ; The size is known, so REFUSE BEFORE THE READ rather than after it. On the
    ; 5150 a 116KB module is seconds of motor (PERFORMANCE.md), and a machine
    ; that reads all of it to then say 'File too big' looks exactly like one
    ; that is working. The figure is free - the dialog reads it out of the
    ; mount snapshot already in RAM - and it also lets the claim be EXACTLY
    ; what the file needs instead of the largest run in the heap, which is
    ; what mpp_trim below was invented to undo.
    cmp dx, 1023                    ; the ONLY ceiling here is the domain of
    jae .toobig                     ; the conversion below (SPEC.md 56.14): it
                                    ; composes DX<<6 into a word, so ~64MB is
                                    ; where the arithmetic stops being true.
                                    ; Everything under it is the HEAP's
                                    ; question, asked eight lines down
    add cx, 1023                    ; KB = ceil(bytes / 1024) across 32 bits:
    adc dx, 0                       ; the +1023 can carry out of CX, and DX is
    mov ax, cx                      ; at most 1023 after it, so the DX<<6
    mov cl, 10                      ; below still lands inside one word
    shr ax, cl                      ; (CX is dead the moment AX has it)
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; ...and it cannot come out 0: the smallest
                                    ; size that reaches here is 1 byte, and a
                                    ; size of 0 means UNKNOWN and went .blind
    mov cx, ax
    call OSAPI_MEM_AVAIL            ; AX = the LARGEST contiguous run, in KB
    cmp cx, ax
    ja .toobig                      ; it will not fit this heap: say so NOW,
    mov ax, cx                      ; with the drive still stopped
    jmp short .sized

.blind:
    call OSAPI_MEM_AVAIL            ; AX = the LARGEST contiguous run, in KB
    or ax, ax
    jz .nomem
    cmp ax, 128                     ; cap the SPECULATIVE claim at 128KB. This
    jbe .sized                      ; is the original cap and the only one with
    mov ax, 128                     ; a reason left (SPEC.md 56.14): the size
                                    ; is unknown here, so the claim is a guess
                                    ; and mpp_trim gives the surplus back. It
                                    ; is a POLITENESS bound on an over-claim,
                                    ; never a verdict on a file - the refusal
                                    ; above no longer borrows it
.sized:
    mov [mpp_capk], ax
    call OSAPI_MEM_CLAIM            ; AX = KB -> DX = base segment, CF on refusal
    jc .nomem
    mov [mpp_modseg], dx

    mov ax, [mpp_capk]              ; DX:CX = capacity in BYTES = KB * 1024
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    mov cx, ax
    mov es, [mpp_modseg]
    xor bx, bx
    mov si, mpp_fname
    call OSAPI_FILE_READ            ; out CF=0, DX:AX = bytes read
    push ds
    pop es
    jc .rderr
    mov [mpm_bloblen_lo], ax
    mov [mpm_bloblen_hi], dx
    call mpp_trim
    mov ax, [mpp_modseg]            ; AFTER the trim, so a claim that somehow
    mov [mpm_blobseg], ax           ; moved is still the one mpm_load indexes
    call mpm_load                   ; CF=1, AX = offset of a NUL error string
    jc .lderr
    mov dx, [mpp_modseg]        ; ONLY NOW is it movable (SPEC.md 66.2/66.5.8),
    mov ax, mpp_reloc           ; and the ordering is the safety argument
    call OSAPI_MEM_MOVABLE      ; rather than a tidiness: OSAPI_FILE_READ above
                                ; holds ES:BX into this block across a call
                                ; that itself CLAIMS (SPEC.md 18.95's sector
                                ; cache). Unlike the two editors (66.5.7.1)
                                ; this needs no pin/unpin pair, because a
                                ; reload FREES the claim and takes a fresh one
                                ; - and a fresh claim starts undeclared
    mov si, mpp_fname               ; the LCD's first line is the module's own
    mov di, mpp_lcdname             ; title, and its second the file name
    call mpp_strcpy12
    call mppu_viz_reset
    call mppu_clock_reset           ; a new module starts the clock at 00:00
    call mpp_play                   ; caps-gated: no-SB machines stay a viewer
    clc
    jmp .out

.nomem:
    mov si, mpp_s_nomem
    jmp .fail
.toobig:
    mov si, mpp_s_toobig            ; refused on the SIZE, before any claim was
    jmp .fail                       ; made - so there is nothing to free, and
                                    ; nothing is playing that this interrupted
.rderr:
    cmp ax, FERR_BIG
    je .big
    cmp ax, FERR_NOENT
    je .noent
    mov si, mpp_s_ioerr
    jmp .failfree
.big:
    mov si, mpp_s_toobig
    jmp .failfree
.noent:
    mov si, mpp_s_noent
    jmp .failfree
.lderr:
    mov si, ax                      ; mpm_load's own verdict string
.failfree:
    push si
    mov dx, [mpp_modseg]
    or dx, dx
    jz .npop
    call OSAPI_MEM_FREE
    mov word [mpp_modseg], 0
.npop:
    pop si
.fail:
    call mppu_msg
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
; mpp_trim - hand back the part of the claim the file did not need
; in:  [mpp_modseg], [mpm_bloblen_*]; preserves every register
;
; This exists for the loads whose size is NOT known up front, where the claim
; is the largest run in the heap and a 5.6KB module would otherwise sit on
; 128KB of it. That used to be every playlist advance; since SPEC.md 56.14's
; mpp_sizeof it is only a name the directory cannot answer for. A load whose
; size IS known claims the file's exact size, so the trim finds nothing to
; give back and costs one compare; it is kept rather than being made
; conditional because the two paths must not disagree about who owns the tail
; of a claim.
;
; OSAPI_MEM_REGROW shrinks IN PLACE (SPEC.md 50.3.1): the record's length
; changes and nothing moves. Called before mpm_load, so no sample pointer
; exists yet to be invalidated even on the impossible path where a shrink
; relocated.
; -----------------------------------------------------------------------------
mpp_trim:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mpm_bloblen_lo]
    mov dx, [mpm_bloblen_hi]
    add ax, 1023                    ; round UP: the heap's granularity is KB,
    adc dx, 0                       ; and a truncating divide would hand back
    mov cl, 10                      ; the tail of the file with the tail of
    shr ax, cl                      ; the claim
    mov bx, dx
    mov cl, 6
    shl bx, cl
    or ax, bx
    jnz .go
    inc ax                          ; 0 KB is a refusal, not a claim
.go:
    cmp ax, [mpp_capk]
    jae .out                        ; the file filled it: nothing to give back
    mov dx, [mpp_modseg]
    call OSAPI_MEM_REGROW
    jc .out                         ; refused: keep the oversized claim, which
    mov [mpp_modseg], dx            ; costs heap and breaks nothing
    mov [mpp_capk], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_reloc - the compactor moved the module (SPEC.md 66.5.8)
; in:  BX = the base it WAS at, DX = the base it is at NOW, DS = CS = ours
; out: nothing; preserves every register
;
; 36 WORDS, AND IT IS NOT trk_reloc. SPEC.md 56.1 is the whole reason this
; file exists in the shape it does: ModPlug's replayer is an INDEPENDENT copy
; of Tracker's, so a fix in one is not a fix in the other - and the layouts
; really do differ (MPS_SZ is 12 against trkplay's MS_SZ, MPM_CHSZ 40), which
; is what would make a copied-and-renamed proc walk the tables with the wrong
; stride and produce plausible garbage rather than an error.
;
; Everything else is trk_reloc's argument verbatim, including the trap it
; sprang: a channel's MPM_SEG of 0 means THIS CHANNEL IS PLAYING NOTHING and
; is not a base to be adjusted. Adding the delta to it invents an address out
; of nowhere, which on a machine with no Sound Blaster is all four of them.
; -----------------------------------------------------------------------------
mpp_reloc:
    push ax
    push cx
    push si
    cmp bx, [mpp_modseg]
    jne .out
    mov [mpp_modseg], dx
    mov ax, dx
    sub ax, bx                      ; AX = the paragraph delta
    cmp bx, [mpm_blobseg]
    jne .out                        ; the replayer is looking at something else
    add [mpm_blobseg], ax
    mov si, mpm_smptab
    mov cx, 31
.smp:
    add [si+MPS_SEG], ax
    add si, MPS_SZ
    loop .smp
    mov si, mpm_chans
    mov cx, 4
.ch:
    cmp word [si+MPM_SEG], 0        ; 0 = playing nothing (see above)
    je .chnext
    add [si+MPM_SEG], ax
.chnext:
    add si, MPM_CHSZ
    loop .ch
.out:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; The transport (SPEC.md 56.2)
; =============================================================================

; -----------------------------------------------------------------------------
; mpp_play - start (or resume) playback
; in:  gfx lock held. Refusals are status-line messages, never aborts: no
;      module, no PCM_BG sink (the no-SB machine keeps the visualiser and the
;      whole interface), no staging memory.
; -----------------------------------------------------------------------------
mpp_play:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [mpm_loaded], 0
    je .noload
    cmp byte [mpp_paused], 0        ; resuming from Pause keeps the position:
    je .fresh                       ; the stream never closed, the replayer
    mov byte [mpp_paused], 0        ; was only told to stop advancing
    mov byte [mpm_playing], 1
    call mpp_msg_transport
    jmp .out
.fresh:
    call OSAPI_SND_CAPS             ; AX = the merged caps word
    test ax, SND_CAP_PCM_BG
    jz .nosb
    call mpp_stream_close           ; restart = close + reopen; the close's
                                    ; drain parks the worker, which is what
                                    ; makes the mpm_start reset and the two
                                    ; UI-task pre-rolls below safe against a
                                    ; mid-mix feed pass
    cmp byte [mpp_ghave], 0         ; the ring grant: allocated once, force-
    jne .granted                    ; freed by teardown (SPEC.md 34.3)
    mov al, 7
    mov ah, 0                       ; sub-op 0 = alloc
    mov cx, MPP_RING
    call OSAPI_SND_STREAM           ; out AX = 0 ok, SI = grant offset
    or ax, ax
    jnz .nogrant
    mov [mpp_grant], si
    mov byte [mpp_ghave], 1
.granted:
    call mpp_rate_arm               ; [mpm_mixrate] = the mode's own rate
    mov al, 0
    call mpm_start
    mov word [mpp_total], 0
    call mpp_mix_stage              ; pre-mix two halves at ring offsets 0 and
    call mpp_mix_stage              ; 2048 - the open's initial valid total
    cmp word [mpm_mixrate], 22222   ; the wide regime plays 4KB kernel halves
    jbe .preok                      ; (SPEC.md 34.5): pre-roll two more, so
    call mpp_mix_stage              ; the open covers both wide halves
    call mpp_mix_stage
.preok:
    mov al, 0                       ; verb 0: open-out, ring mode - the flag
    mov ah, SND_OPENF_RING          ; rides AH (verb 0 carries no handle)
    mov si, [mpp_grant]
    mov cx, [mpp_total]             ; initial valid total = the pre-roll
    mov dx, [mpm_mixrate]
    call OSAPI_SND_STREAM           ; out AL = 0 with AH = handle, else err
    or al, al
    jnz .ofail
    mov [mpp_hand], ah
    mov byte [mpp_ended], 0         ; re-arm BEFORE publishing: the worker keys
    mov byte [mpp_paused], 0        ; every pass on mpp_sopen, and a pass must
    mov byte [mpp_sopen], 1         ; never see the new stream through the old
    call mpp_msg_transport          ; session's flags
    jmp .out
.ofail:
    call mpm_stop
    mov si, mpp_s_snderr
    cmp ax, 2                       ; err 2 = rate refused: the 44 kHz pick on
    jne .ofmsg                      ; a pre-4.x DSP (SPEC.md 56.5)
    mov si, mpp_s_norate
.ofmsg:
    call mppu_msg
    jmp .out
.noload:
    mov si, mpp_s_noload
    call mppu_msg
    jmp .out
.nosb:
    mov si, mpp_s_nosb
    call mppu_msg
    jmp .out
.nogrant:
    mov si, mpp_s_nomem
    call mppu_msg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_rate_arm - [mpm_mixrate] = the rate the CURRENT mode will ask for
; preserves all registers
;
; Called by mpp_play, and also by the XT toggle and the Setup page's Frequency
; radios - which is the point. The LCD reads this word, and a rate that only
; became true at the next Play meant the panel said '11.0 kHz' for as long as
; XT mode was on and stopped. The kernel may still quantise the request
; through the time constant (SPEC.md 34.5); this is what was ASKED for, which
; is the honest thing to show before there is an answer.
; -----------------------------------------------------------------------------
mpp_rate_arm:
    push ax
    push bx
    cmp byte [mpm_xt], 0            ; XT mode overrides Setup > Audio with its
    je .sel                         ; own 5,500 Hz (SPEC.md 56.7)
    mov ax, MPP_RATE_XT
    jmp .set
.sel:
    mov bl, [mpp_rsel]
    xor bh, bh
    shl bx, 1
    mov ax, [mpp_rates + bx]
.set:
    mov [mpm_mixrate], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_pause - ModPlug Player's Pause, and it is NOT Stop with a different name
;
; The stream stays open and the ring stays where it is; only the replayer
; stops advancing, so the worker's next pass mixes silence into the ring and
; the DSP plays it. Resuming is one flag. Stop, by contrast, closes.
; in:  gfx lock held
; -----------------------------------------------------------------------------
mpp_pause:
    push si
    cmp byte [mpm_loaded], 0
    je .out
    cmp byte [mpp_sopen], 0         ; nothing open: Pause has nothing to hold,
    je .out                         ; so it is not an error, it is a no-op
    cmp byte [mpp_paused], 0
    jne .resume
    mov byte [mpp_paused], 1
    mov byte [mpm_playing], 0
    jmp .msg
.resume:
    mov byte [mpp_paused], 0
    mov byte [mpm_playing], 1
.msg:
    call mpp_msg_transport
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; mpp_stop - close the stream and park the replayer at the top
; -----------------------------------------------------------------------------
mpp_stop:
    push si
    call mpp_play_stop
    mov byte [mpp_paused], 0
    call mpp_msg_transport
    pop si
    ret

; -----------------------------------------------------------------------------
; mpp_mix_stage - mix one half and stage it at the ring position [mpp_total]
;                 names, then advance that counter. Used by the UI pre-roll
;                 and by the worker's feed loop: MPP_RING is a multiple of
;                 MPP_HALF, so a half never crosses the ring seam and the copy
;                 needs no split (the SPEC.md 34.5 ring rule).
; preserves all registers
; -----------------------------------------------------------------------------
mpp_mix_stage:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, MPP_HALF
    call mpm_gen                    ; renders into mpm_outbuf and advances the
                                    ; replayer; clobbers freely (the mpm_ rule)
    call mppu_viz_feed              ; the oscilloscope's source is this buffer,
                                    ; and this is the only moment it is fresh
    mov di, [mpp_total]
    and di, MPP_RMASK
    add di, [mpp_grant]             ; the physical grant offset of stream byte n
    mov si, mpm_outbuf
    mov cx, MPP_HALF
    mov al, 6                       ; verb 6: stage (the kernel copies from our
    call OSAPI_SND_STREAM           ; segment; worker-safe since the amendment)
    add word [mpp_total], MPP_HALF  ; free-running 16-bit, mod 65536
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_stream_close / mpp_play_stop / mpp_reap
; UI context only (verb 2). The close box needs none of them: teardown
; force-frees the stream, the pool grant and the heap claim (34.3/50.2).
; -----------------------------------------------------------------------------
mpp_stream_close:
    push ax
    cmp byte [mpp_sopen], 0
    je .drain
    mov al, 2                       ; verb 2: close (fine on an ended stream -
    mov ah, [mpp_hand]              ; stale is an answer, not an error)
    call OSAPI_SND_STREAM
    mov byte [mpp_sopen], 0
.drain:
    cmp byte [mpp_mixing], 0        ; park the worker (SPEC.md 56.2): a feed
    jne .drain                      ; pass already past its mpp_sopen guard
                                    ; runs to completion first. Deadlock-free
                                    ; (mpp_feed never takes the gfx lock this
                                    ; task holds; pre-emption keeps the worker
                                    ; running) and bounded (.fill re-checks
                                    ; mpp_sopen per half). After this line the
                                    ; worker cannot be inside mpm_gen, so the
                                    ; caller may reset the replayer, pre-roll
                                    ; on the UI task, or free the module blob.
    pop ax
    ret

mpp_play_stop:
    call mpp_stream_close
    call mpm_stop
    ret

; mpp_reap - close a stopped-but-open stream from UI context. A watchdog end
; (or an F00 in the module) stops playback on the WORKER, which cannot close -
; verb 2 is UI-callback-only - so it only flags. Every UI callback runs this
; first, so the machine's single stream record is held no longer than this
; app's next paint, key, click or menu command. It also drives the playlist:
; a song that simply ENDED is what Repeat and the auto-advance hang off.
mpp_reap:
    push ax
    cmp byte [mpp_sopen], 0
    je .out
    cmp byte [mpp_ended], 0
    jne .close
    cmp byte [mpp_paused], 0        ; Pause stops the replayer on purpose and
    jne .out                        ; must not read as a finished song
    cmp byte [mpm_playing], 0
    jne .out
.close:
    call mpp_stream_close
    mov byte [mpp_ended], 0
    call mpp_song_over              ; ...and only then, the playlist rules
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_song_over - the module reached its end (or an F00 stopped it)
; in:  gfx lock held; the stream is already closed
;
; ModPlug Player's Repeat is three-valued and Shuffle sits on top of it, so
; this is the one place all four outcomes are decided: repeat the song,
; advance the list (shuffled or not), stop at the end of the list, or stop.
; -----------------------------------------------------------------------------
mpp_song_over:
    push ax
    cmp byte [mpp_repeat], MPPR_SONG
    je .again
    cmp byte [mpp_plcount], 0       ; no list: nothing to advance to
    je .halt
    call mpp_pl_advance             ; CF=1: the list ended and Repeat is Off
    jc .halt
    jmp .out
.again:
    call mpp_play
    jmp .out
.halt:
    call mpp_msg_transport
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_msg_transport - the status line follows the transport, from wherever it
;                     changed: the state AND the key that leaves it
; in:  gfx lock held; preserves all registers
; -----------------------------------------------------------------------------
mpp_msg_transport:
    push si
    mov si, mpp_s_stopped
    cmp byte [mpp_paused], 0
    je .p
    mov si, mpp_s_paused
    jmp .set
.p:
    cmp byte [mpm_playing], 0
    je .set
    mov si, mpp_s_playing
.set:
    call mppu_msg
    pop si
    ret

; -----------------------------------------------------------------------------
; mpp_xt_toggle - XT mode (SPEC.md 56.7), from the key, the menu, the option
;                 button and the Setup window's own page
; in:  gfx lock held; preserves all registers
;
; Toggling while playing STOPS first (through the SPEC.md 56.2 drain): the
; mode change is a volume-table rebuild plus a rate change, never a
; mid-stream switch. The menu item is relabeled the SPEC.md 12.2 way - the
; string is repointed and OSAPI_MENU_SET re-called (the Solitaire Deal-menu
; idiom), which is invisible until that second call.
; -----------------------------------------------------------------------------
mpp_xt_toggle:
    push ax
    push bx
    push si
    call mpp_play_stop
    mov byte [mpp_paused], 0
    mov al, [mpm_xt]
    xor al, 1
    call mpm_setxt                  ; ...which also forces the DSP tail off
    call mpp_rate_arm               ; ...and the LCD's rate follows the mode
    mov si, mpp_s_xtoff
    mov ax, mpp_s_xtmoff
    cmp byte [mpm_xt], 0
    je .lab
    mov si, mpp_s_xton
    mov ax, mpp_s_xtmon
.lab:
    mov [mpp_mi_play + 14], si      ; Playback item 7 relabels...
    mov bx, [mpp_win]
    mov si, mpp_menus
    call OSAPI_MENU_SET             ; ...and only this makes it visible
    mov si, ax
    call mppu_msg
    call mppu_refresh
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_rate_set - Setup > Audio > Frequency
; in:  AL = 0/1/2 (11/22/44 kHz); gfx lock held
; A rate change while playing stops first, exactly like the XT toggle.
; -----------------------------------------------------------------------------
mpp_rate_set:
    push ax
    cmp al, 3
    jae .out
    cmp al, [mpp_rsel]
    je .out
    push ax
    call mpp_play_stop
    mov byte [mpp_paused], 0
    pop ax
    mov [mpp_rsel], al
    call mpp_rate_arm
.out:
    pop ax
    ret

; =============================================================================
; The playlist (SPEC.md 56.8). The store lives here rather than in
; mpplist.inc because the transport reads it and the editor only edits it.
; =============================================================================

; mpp_pl_add - append [SI] (a NUL 8.3 name) to the list; CF=1 if it is full
mpp_pl_add:
    push ax
    push cx
    push di
    mov al, [mpp_plcount]
    cmp al, MPP_PLMAX
    jae .full
    mov cl, MPP_PLENT
    mul cl                          ; AX = index * 13 (AL * CL, no imm mul)
    mov di, mpp_plist
    add di, ax
    call mpp_strcpy12
    inc byte [mpp_plcount]
    clc
    jmp .out
.full:
    stc
.out:
    pop di
    pop cx
    pop ax
    ret

; mpp_pl_ptr - DI = the entry for index AL; CF=1 if the index is past the end
mpp_pl_ptr:
    push ax
    push cx
    cmp al, [mpp_plcount]
    jae .bad
    mov cl, MPP_PLENT
    mul cl
    mov di, mpp_plist
    add di, ax
    clc
    jmp .out
.bad:
    stc
.out:
    pop cx
    pop ax
    ret

; mpp_pl_remove - drop entry AL, closing the gap
mpp_pl_remove:
    push ax
    push bx
    push cx
    push si
    push di
    cmp al, [mpp_plcount]
    jae .out
    mov bl, al
    call mpp_pl_ptr
    jc .out
    mov si, di
    add si, MPP_PLENT
    mov al, [mpp_plcount]
    dec al
    sub al, bl                      ; entries after the removed one
    jz .last
    mov ah, MPP_PLENT
    mul ah                          ; AX = bytes to slide down
    mov cx, ax
    push ds
    pop es
    cld
    rep movsb
.last:
    dec byte [mpp_plcount]
    mov al, [mpp_plcur]             ; keep the cursor inside the shortened list
    cmp al, [mpp_plcount]
    jb .out
    mov al, [mpp_plcount]
    or al, al
    jz .zero
    dec al
.zero:
    mov [mpp_plcur], al
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; mpp_pl_play - load and play entry AL; CF=1 if it would not load
mpp_pl_play:
    push si
    push di
    call mpp_pl_ptr
    jc .bad
    mov [mpp_plcur], al
    mov si, di
    mov di, mpp_fname
    call mpp_strcpy12
    call mpp_load_name
    jmp .out
.bad:
    stc
.out:
    pop di
    pop si
    ret

; mpp_pl_next / mpp_pl_prev - the transport's |<< and >>| buttons. With no
; list they are not errors: they seek to the start and the end of the module,
; which is what a one-song "list" can mean.
mpp_pl_next:
    push ax
    call mpp_pl_walk_reset          ; a button press is a deliberate start
    cmp byte [mpp_plcount], 0
    je .seek
    call mpp_pl_pick_next
    call mpp_pl_play
    jmp .out
.seek:
    mov al, 1
    call mpm_setpos
.out:
    pop ax
    ret

mpp_pl_prev:
    push ax
    call mpp_pl_walk_reset
    cmp byte [mpp_plcount], 0
    je .seek
    mov al, [mpp_plcur]
    or al, al
    jnz .back
    mov al, [mpp_plcount]
.back:
    dec al
    call mpp_pl_play
    jmp .out
.seek:
    mov al, -1
    call mpm_setpos
.out:
    pop ax
    ret

; mpp_pl_pick_next - AL = the index to play after the current one. Shuffle
; draws a random entry (and rejects the current one while the list has more
; than one, so "shuffle" never means "play this again"); otherwise it wraps.
mpp_pl_pick_next:
    push bx
    push cx
    cmp byte [mpp_shuffle], 0
    je .seq
    cmp byte [mpp_plcount], 1
    jbe .seq
    mov cx, 8                       ; bounded: a draw that keeps landing on
.draw:                              ; the current entry gives up and steps
    call OSAPI_RAND
    xor dx, dx
    mov bl, [mpp_plcount]
    xor bh, bh
    div bx                          ; DX = AX mod count
    mov al, dl
    cmp al, [mpp_plcur]
    jne .got
    loop .draw
.seq:
    mov al, [mpp_plcur]
    inc al
    cmp al, [mpp_plcount]
    jb .got
    xor al, al
.got:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mpp_pl_advance - the end-of-song advance
; out: CF=1 means "the walk is over and Repeat is not List" - the one case
;      that stops rather than plays. Preserves every register.
;
; The end of a SHUFFLED list is the awkward case, and a played-count is what
; answers it. Sequentially, "the walk is over" is just plcur reaching the end;
; shuffled, there is no end - a random draw goes on forever - so Repeat: Off
; would never stop, which is not what either ModPlug Player or a user means by
; it. [mpp_plplayed] counts songs since the last DELIBERATE start (Open, the
; Prev/Next buttons, a double-click in the editor - all of which call
; mpp_pl_walk_reset), and one full list's worth of them ends the walk in both
; modes. It is a count and not a played-set because 16 entries of set would
; cost more state than the list itself, and the visible difference is only
; that a shuffle may repeat a song before the pass is out.
; -----------------------------------------------------------------------------
mpp_pl_advance:
    push ax
    mov al, [mpp_plplayed]
    inc al
    mov [mpp_plplayed], al
    cmp byte [mpp_repeat], MPPR_LIST
    je .go                          ; Repeat: List never ends
    cmp al, [mpp_plcount]
    jae .end                        ; one full pass, and Repeat is not List
.go:
    cmp byte [mpp_shuffle], 0
    jne .sh
    mov al, [mpp_plcur]             ; sequential, wrapping - the bound above
    inc al                          ; is what stops it, not the wrap
    cmp al, [mpp_plcount]
    jb .play
    xor al, al
    jmp .play
.sh:
    call mpp_pl_pick_next
.play:
    call mpp_pl_play
    pop ax
    clc
    ret
.end:
    pop ax
    stc
    ret

; mpp_pl_walk_reset - a deliberate start begins a new pass over the list
mpp_pl_walk_reset:
    mov byte [mpp_plplayed], 0
    ret

; mpp_pl_shuffle_list - PlayList > Shuffle List: a Fisher-Yates pass over the
; entries themselves, which is a different thing from the Shuffle option (that
; one picks at play time and leaves the list alone).
mpp_pl_shuffle_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, [mpp_plcount]
    cmp cl, 2
    jb .out
    xor ch, ch
.loop:
    dec cx                          ; i from count-1 down to 1
    jz .out
    call OSAPI_RAND
    xor dx, dx
    mov bx, cx
    inc bx
    div bx                          ; DX = a random j in 0..i
    mov al, cl
    call mpp_pl_ptr
    mov si, di                      ; SI = entry i
    mov al, dl
    call mpp_pl_ptr
    push cx                         ; DI = entry j; swap the 13 bytes
    mov cx, MPP_PLENT
.sw:
    mov al, [si]
    mov ah, [di]
    mov [si], ah
    mov [di], al
    inc si
    inc di
    loop .sw
    pop cx
    jmp .loop
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_strcpy12 - copy a NUL 8.3 name, DS:SI -> DS:DI, at most 12 bytes + NUL
; preserves all registers
; -----------------------------------------------------------------------------
mpp_strcpy12:
    push ax
    push cx
    push si
    push di
    mov cx, 12
.c:
    mov al, [si]
    mov [di], al
    or al, al
    jz .z
    inc si
    inc di
    loop .c
    mov byte [di], 0
    jmp .out
.z:
    ; the NUL is already stored
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; The worker (SPEC.md 20.6): ALIVE -> SLEEP 1 -> feed audio lock-free -> one
; short lock hold to draw. Never returns; OSAPI_TASK_ALIVE is where it dies
; when the close box is clicked.
; =============================================================================
mpp_worker:
.loop:
    mov bx, [mpp_win]
    call OSAPI_TASK_ALIVE           ; the lock is NOT held here (rule 4)
    mov ax, 1
    call OSAPI_TASK_SLEEP           ; ~18 wakes a second
    call mppu_clock                 ; the elapsed-time readout, counted on the
                                    ; wake rather than on the frame: a covered
                                    ; window skips frames and the clock must
                                    ; not stop with the picture
    call mpp_feed                   ; audio first: it must not starve behind a
    call mpp_render                 ; slow frame
    jmp .loop

; -----------------------------------------------------------------------------
; mpp_feed - keep the ring ahead of the DSP. Lock-free, worker context: verbs
;            3 (status), 6 (stage) and 1 (feed) are any-task under the amended
;            SPEC.md 20.3; verbs 0/2 are not, so an ENDED stream is only
;            FLAGGED here and closed later on the UI task (mpp_reap).
;
; [mpp_mixing] brackets the WHOLE pass, set before the entry guards and
; cleared last (SPEC.md 56.2): mpp_stream_close drains it, which is what makes
; the UI task's replayer resets, pre-rolls and blob frees safe against a
; worker suspended anywhere in here - mpm_gen is not reentrant. A pass that
; entered between a close and a reopen sees mpp_sopen = 0 and falls straight
; out without touching mpm_ state.
;
; lead = total - consumed, both free-running 16-bit counters, so the
; subtraction is exact across wrap. The consumed count goes stale across the
; loop, which errs the safe way: it only grows, so a stale value over-reports
; fullness and never over-feeds.
; -----------------------------------------------------------------------------
mpp_feed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [mpp_mixing], 1        ; FIRST, before the guards: a pass past its
                                    ; guards must never be invisible to the
                                    ; drain in mpp_stream_close
    cmp byte [mpp_sopen], 0
    je .out
    cmp byte [mpp_ended], 0
    jne .out
    mov al, 3                       ; verb 3: status - AX = state, DX =
    mov ah, [mpp_hand]              ; consumed (free-running in ring mode)
    call OSAPI_SND_STREAM
    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
    cmp byte [mpp_paused], 0        ; Pause holds the stream open and mixes
    jne .go                         ; silence into it - that is the whole
                                    ; difference between Pause and Stop
    cmp byte [mpm_playing], 0        ; the module stopped itself (end, or an
    jne .go                         ; F00): wait for the ring to drain so the
    cmp dx, [mpp_total]             ; last row is actually heard, then flag
    jne .out                        ; for the UI-side close
    mov byte [mpp_ended], 1
    jmp .out
.go:
    mov byte [mpp_halves], 0
.fill:
    cmp byte [mpp_sopen], 0         ; a UI close mid-pass ends the burst, which
    je .out                         ; is what bounds the drain wait
    cmp byte [mpp_halves], MPP_MAXFEED
    jae .out
    mov ax, [mpp_total]
    sub ax, dx                      ; AX = lead, 0..MPP_RING
    cmp ax, MPP_RING - MPP_HALF
    ja .out                         ; no room for a whole half
    push dx
    call mpp_mix_stage              ; mix + stage + total += HALF
    mov cx, [mpp_total]             ; verb 1: feed - the new valid total
    mov al, 1
    mov ah, [mpp_hand]
    call OSAPI_SND_STREAM
    pop dx
    or ax, ax
    jnz .out                        ; a refused feed: try again next wake
    inc byte [mpp_halves]
    jmp .fill
.dead:
    call mpm_stop                   ; watchdog-ended streams never resume
    mov byte [mpp_ended], 1         ; (SPEC.md 34.5); mpp_reap closes it on
                                    ; the UI task
.out:
    mov byte [mpp_mixing], 0        ; the drain gate reopens LAST
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mpp_render - the worker's one lock hold (SPEC.md 20.6 rules 3/5): geometry
;              re-check, clip armed, [mpp_abon] honoured UNDER the lock so the
;              About panel is never painted over, then the dynamic-only redraw.
; -----------------------------------------------------------------------------
mpp_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_GFX_LOCK
    mov bx, [mpp_win]
    call OSAPI_WM_GEOM              ; CF=1: hidden - draw nothing
    jc .unlock
    call OSAPI_WM_CLIP_SET          ; CF=1: fully covered - skip the frame
    jc .unlock
    cmp byte [mpp_abon], 0          ; checked HERE, under the lock, after the
    jne .unlock                     ; clip: the [ark_abon] rule verbatim
    call mppu_frame
.unlock:
    call OSAPI_GFX_UNLOCK           ; also clears the clip
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Data
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words); x/y/h set by mpp_entry --
mpp_tpl:
    dw 0, 0, MPP_FW, MPP_FHF
    dw mpp_ttl, mpp_paint, mpp_onkey, mpp_onclick

; --- the transport row's jump table -------------------------------------------
; Nine buttons in ModPlug Player's own order, and the four that are one line
; each live above; the rest are the transport proper.
mpp_transtab:
    dw mpp_t_open, mpp_pl_prev, mpp_t_rew, mpp_play, mpp_pause
    dw mpp_stop, mpp_t_ffwd, mpp_pl_next, mpp_t_setup
; --- the option buttons (SPEC.md 56.3) -----------------------------------------
mpp_opttab:
    dw mpp_o_repeat, mpp_o_shuffle, mpp_o_plist
    dw mpp_o_amiga,  mpp_o_dither,  mpp_o_xt
    dw mpp_o_scope,  mpp_o_spectrum, mpp_o_vu
    dw mpp_o_info,   mpp_o_setup,   mpp_o_about

; the labels, in the same order; the LED beside each says whether it is ON
mpp_optlab:
    dw mpp_ol_rep, mpp_ol_shuf, mpp_ol_list
    dw mpp_ol_ami, mpp_ol_dith, mpp_ol_xt
    dw mpp_ol_scope, mpp_ol_spec, mpp_ol_vu
    dw mpp_ol_info, mpp_ol_setup, mpp_ol_about
mpp_ol_rep:   db 'Repeat', 0
mpp_ol_shuf:  db 'Shuffle', 0
mpp_ol_list:  db 'PlayList', 0
mpp_ol_ami:   db 'Amiga', 0
mpp_ol_dith:  db 'Dither', 0
mpp_ol_xt:    db 'XT', 0
mpp_ol_scope: db 'Scope', 0
mpp_ol_spec:  db 'Spectrum', 0
mpp_ol_vu:    db 'VU', 0
mpp_ol_info:  db 'Info', 0
mpp_ol_setup: db 'Setup', 0
mpp_ol_about: db 'About', 0

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
    OS88_MENUSET mpp_menus, mpp_m_name, mpp_oncmd
        OS88_MENU mpp_m_file, mpp_mi_file, 3
        OS88_MENU mpp_m_play, mpp_mi_play, 8
        OS88_MENU mpp_m_view, mpp_mi_view, 3
    OS88_MENUSET_END mpp_menus

mpp_m_name:  db 'ModPlug', 0
mpp_m_file:  db 'File', 0
mpp_mi_file: dw mpp_s_open, mpp_s_plist, mpp_s_setup
mpp_s_open:  db 'Open...', 0
mpp_s_plist: db 'PlayList Editor', 0
mpp_s_setup: db 'Setup...', 0
mpp_m_play:  db 'Playback', 0
mpp_mi_play: dw mpp_s_play, mpp_s_pause, mpp_s_stopi, mpp_s_previ
             dw mpp_s_nexti, mpp_s_repi, mpp_s_shufi, mpp_s_xtoff
                                        ; item 7 is repointed by
                                        ; mpp_xt_toggle (the sol_dealmenu
                                        ; relabel idiom - invisible until
                                        ; OSAPI_MENU_SET runs again)
mpp_s_play:  db 'Play', 0
mpp_s_pause: db 'Pause', 0
mpp_s_stopi: db 'Stop', 0
mpp_s_previ: db 'Previous Song', 0
mpp_s_nexti: db 'Next Song', 0
mpp_s_repi:  db 'Repeat', 0
mpp_s_shufi: db 'Shuffle', 0
mpp_s_xtoff: db 'XT Mode: Off', 0
mpp_s_xton:  db 'XT Mode: On', 0
mpp_m_view:  db 'View', 0
mpp_mi_view: dw mpp_s_vspec, mpp_s_vscope, mpp_s_vvu
mpp_s_vspec:  db 'Spectrum Analyzer', 0
mpp_s_vscope: db 'Oscilloscope', 0
mpp_s_vvu:    db 'VU Meter', 0

mpp_ttl:     db 'ModPlug Player', 0

; --- the sample-rate table (Setup > Audio > Frequency) -------------------------
mpp_rates:   dw MPP_RATE, MPP_RATE22, MPP_RATE44

; --- status-line strings -------------------------------------------------------
mpp_s_stopped: db 'Stopped   X play  L load  E setup', 0
mpp_s_playing: db 'Playing   C pause  V stop  B next', 0
mpp_s_paused:  db 'Paused    X resume  V stop', 0
mpp_s_noload:  db 'No module loaded - L loads one', 0
mpp_s_nosb:    db 'No Sound Blaster: the interface only', 0
mpp_s_nomem:   db 'Out of memory', 0
mpp_s_toobig:  db 'File too big', 0
mpp_s_noent:   db 'File not found', 0
mpp_s_ioerr:   db 'Disk error', 0
mpp_s_snderr:  db 'Sound open failed', 0
mpp_s_norate:  db '44 kHz needs a DSP 4.x card', 0
mpp_s_xtmon:   db 'XT mode on - the DSP tail is off', 0
mpp_s_xtmoff:  db 'XT mode off', 0
mpp_s_hint:    db 'Z X C V B transport  L load  E setup  P list', 0

; --- the LCD panel's own strings (mppui.inc) -----------------------------------
mpp_s_appname:  db 'ModPlug Player', 0
mpp_s_untitled: db '(untitled module)', 0
mpp_s_nofile:   db '- no module -', 0
mpp_s_dashes:   db '---', 0
mpp_s_khz:      db ' kHz 8 Bit Mono', 0
mpp_s_fxt:      db ' XT', 0         ; the flags, each drawn only when it is on
mpp_s_flin:     db ' LIN', 0
mpp_s_fdith:    db ' DTH', 0
mpp_s_fa500:    db ' A500', 0
mpp_s_fa1200:   db ' A1200', 0
mpp_s_fcrush:   db ' CRUSH', 0
mpp_s_pos:      db 'Pos ', 0
mpp_s_pat:      db '  Pat ', 0
mpp_s_row:      db '  Row ', 0
mpp_s_spd:      db '  Spd ', 0
mpp_s_bpm:      db '  BPM ', 0

; --- the About card (OSAPI_ABOUT_SET) ------------------------------------------
; Credit where the design came from, and an honest line about what could not
; come with it - a user who reads this should not be left thinking libopenmpt
; is in here somewhere.
mpp_about_lines:
    dw mpp_ab1, mpp_ab2, mpp_ab3, mpp_ab4, mpp_ab5, mpp_ab6, mpp_ab7, 0
mpp_ab1: db 'ModPlug Player for os8088', 0
mpp_ab2: db 0
mpp_ab3: db 'Interface ported from ModPlug Player V2', 0
mpp_ab4: db 'by Volkan Orhan - modplugplayer.org, GPLv3', 0
mpp_ab5: db 0
mpp_ab6: db 'Replayer: 4-channel ProTracker, 8-bit mono.', 0
mpp_ab7: db 'Not libopenmpt - see SPEC.md 56.1.', 0

; =============================================================================
; The other three quarters of the package
; =============================================================================
%include "mppmix.inc"
%include "mppui.inc"
%include "mppset.inc"
%include "mpplist.inc"

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes MPP_BSS bytes after the image; every
; name is an equ offset from os88_image_end via the MPPB/MPPW/MPPBUF macros
; defined at the top of this file)
; =============================================================================

    MPPW mpp_win                    ; the bound window (opaque handle)
    MPPB mpp_hired                  ; the worker exists
    MPPB mpp_abon                   ; the About panel is up; worker frames drop
    MPPB mpp_compact                ; the compact layout (SPEC.md 56.4)
    MPPW mpp_scrw                   ; the screen this machine actually has
    MPPW mpp_docky                  ; ...and its first dock row
    MPPB mpp_cpu0                   ; the MACHINE is a tier-0 8086/8088
                                    ; (SPEC.md 41.8), latched at entry. NOT
                                    ; [mpm_xt], which is a user-toggleable
                                    ; playback mode and true on a 386 the
                                    ; moment someone presses T - this is the
                                    ; one that decides what may be ANIMATED

; --- the module blob (a heap claim, SPEC.md 50) -------------------------------
    MPPW mpp_modseg                 ; claim base segment, 0 = none
    MPPW mpp_capk                   ; ...its size in KB
    MPPBUF mpp_fname, 13            ; the chosen 8.3 name, copied out of the
                                    ; kernel's buffer during the completion call
    MPPBUF mpp_find, OSAPI_FIND_SZ  ; mpp_sizeof's directory record, for the
                                    ; loads that arrive with no size (56.14)
    MPPW mpp_fszl                   ; ...and that file's size, banked the same
    MPPW mpp_fszh                   ; way. 0 = unknown, which is every load
                                    ; that did not come from the dialog - a
                                    ; playlist advance. READ-AND-CLEAR in
                                    ; mpp_load_name, so one can never inherit
                                    ; the other's figure
    MPPBUF mpp_lcdname, 13          ; ...and the copy the LCD panel shows
    MPPB mpp_addpl                  ; the file dialog is serving PlayList > Add

; --- the ring stream (SPEC.md 34.5 ring mode) ----------------------------------
    MPPB mpp_ghave                  ; the pool grant exists
    MPPW mpp_grant                  ; ...its SND_SEG offset
    MPPB mpp_sopen                  ; a stream is open
    MPPB mpp_hand                   ; ...its handle
    MPPB mpp_ended                  ; watchdog/self-ended: stop feeding, close
                                    ; on the next UI event (mpp_reap)
    MPPW mpp_total                  ; free-running total bytes mixed (mod 64K)
    MPPB mpp_halves                 ; halves fed this wake (bounds the burst)
    MPPB mpp_mixing                 ; the worker is inside a feed pass -
                                    ; mpp_stream_close drains it before any
                                    ; UI-task touch of mpm_ state or the blob
    MPPB mpp_paused                 ; Pause: the stream stays OPEN and the
                                    ; replayer stops. Not Stop (SPEC.md 56.2)

; --- the player's own settings -------------------------------------------------
    MPPB mpp_rsel                   ; Setup > Audio > Frequency: 0/1/2 =
                                    ; 11/22/44 kHz; bss zeroes to the default
    MPPB mpp_repeat                 ; MPPR_OFF / _SONG / _LIST
    MPPB mpp_shuffle                ; the option button, not the list operation
    MPPB mpp_viz                    ; MPPV_SPECTRUM / _SCOPE / _VU
    MPPB mpp_info                   ; the LCD's third line shows the sample list

; --- the playlist (SPEC.md 56.8) -----------------------------------------------
    MPPB mpp_plcount                ; entries in use, 0..MPP_PLMAX
    MPPB mpp_plcur                  ; the one playing (or last played)
    MPPB mpp_plplayed               ; songs since the last deliberate start -
                                    ; what ends a SHUFFLED walk (mpp_pl_advance)
    MPPB mpp_plsel                  ; the editor's selected row
    MPPB mpp_pltop                  ; ...and its first visible row
    MPPBUF mpp_plist, MPP_PLMAX * MPP_PLENT

    OS88_BSS MPP_BSS
    OS88_IMAGE_END
