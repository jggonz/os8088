; =============================================================================
; os8088 - apps/tracker/tracker.asm
;
; Tracker, the tenth software package (SPEC.md 45). A FastTracker II-styled
; 4-channel ProTracker MOD player: launches windowed with a splash card, any
; key or click enters FULLSCREEN (wm_fullscreen's first shipped package
; client, SPEC.md 11.2), Esc returns. Modules load through the Standard File
; dialog into an arena grant via OSAPI_FILE_READBIG (files >= 64KB are why
; that slot exists), and play through a ring-mode Sound Blaster background
; stream fed by the package's worker task (the worker-safe stream verbs and
; ring mode of SPEC.md 34.5/20.3 exist for this app).
;
; Three files, one package (SPEC.md 45):
;   tracker.asm  - header, icon, entry, callbacks, worker, stream plumbing
;   trkplay.inc  - MOD loader/validator, replayer, mixer (prefix mp_)
;   trkui.inc    - adapter-parameterized FT2 layout + all drawing (tui_)
;
; The division of labour mirrors Arkanoid: the UI task only sets words and
; calls UI-context-only services (file dialog, stream open/close, MEM_*);
; the worker does everything periodic - it feeds the audio ring FIRST
; (lock-free: mp_gen + stage/feed, the newly worker-safe verbs 1/3/6), then
; takes the gfx lock for one short dynamic-redraw burst. ONE handshake
; serializes them (SPEC.md 45.2): [trk_mixing], the worker's feed-pass busy
; flag, drained by trk_stream_close before anything on the UI task resets
; the replayer, pre-rolls mp_gen, or frees the module blob - mp_gen is not
; reentrant, and every UI path that touches mp_* state sits behind a
; trk_stream_close.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'TRACKER', trk_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; Two beamed eighth notes over a square wave - the app in two glyphs. The mask
; is the silhouette dilated one pixel so it sits on a clean white underlay.
;
;   data                mask
;   ....##########..    ...############.
;   ....##########..    ...############.
;   ....##......##..    ...############.
;   ....##......##..    ...####....####.
;   ....##......##..    ...####....####.
;   ....##......##..    ...####....####.
;   ....##......##..    ...####....####.
;   ....##......##..    ...####....####.
;   ....##......##..    .######..######.
;   ..####....####..    ################
;   .######..######.    ################
;   .######..######.    ################
;   ..####....####..    ################
;   ................    ###############.
;   ####....####....    ################
;   ....####....####    ################
    OS88_ICON16
    dw 0x1FFE                       ; 16 mask rows (white underlay)
    dw 0x1FFE
    dw 0x1FFE
    dw 0x1E1E
    dw 0x1E1E
    dw 0x1E1E
    dw 0x1E1E
    dw 0x1E1E
    dw 0x7E7E
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFE
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0FFC                       ; 16 data rows (black pixels)
    dw 0x0FFC
    dw 0x0C0C
    dw 0x0C0C
    dw 0x0C0C
    dw 0x0C0C
    dw 0x0C0C
    dw 0x0C0C
    dw 0x0C0C
    dw 0x3C3C
    dw 0x7E7E
    dw 0x7E7E
    dw 0x3C3C
    dw 0x0000
    dw 0xF0F0
    dw 0x0F0F
    OS88_ICON16_END

; --- the package-wide bss macros (the Arkanoid %assign pattern) ----------------
; Pinned interface: defined HERE, at the top, before any %include of
; trkplay.inc / trkui.inc, so all three files declare bss through the same
; accumulator. TRKB = byte, TRKW = word, TRKBUF = named buffer.
%assign TRK_BSS 0
%macro TRKB 1
%1 equ os88_image_end + TRK_BSS
%assign TRK_BSS TRK_BSS + 1
%endmacro
%macro TRKW 1
%1 equ os88_image_end + TRK_BSS
%assign TRK_BSS TRK_BSS + 2
%endmacro
%macro TRKBUF 2
%1 equ os88_image_end + TRK_BSS
%assign TRK_BSS TRK_BSS + (%2)
%endmacro

; --- tuning --------------------------------------------------------------------
TRK_RING    equ 16384               ; ring grant size, bytes (power of two,
                                    ; 4096..32768 per the ring-mode contract);
                                    ; ~1.49s of audio at 11kHz
TRK_RMASK   equ TRK_RING - 1
TRK_HALF    equ 2048                ; the stream's fill unit, pinned (SPEC.md
                                    ; 34.5): fills are whole halves only
TRK_RATE    equ 11000               ; open rate request, Hz (kernel quantizes
                                    ; via the TC; mp_gen mixes to the same)
TRK_RATE_XT equ 5500                ; XT mode's rate (SPEC.md 45.9): halves
                                    ; the mixer's per-second sample budget
TRK_RATE22  equ 22050               ; Rate menu (SPEC.md 45.10): still the
                                    ; classic TC regime, any DSP
TRK_RATE44  equ 44100               ; the 34.5 wide-rate regime - DSP >= 4
                                    ; only; an older card refuses err 2
TRK_MAXFEED equ 6                   ; halves mixed per worker wake, at most -
                                    ; bounds the lock-free burst so a wake
                                    ; never mixes more than ~1.1s of audio
TRK_WINW    equ 420                 ; the windowed splash frame
TRK_WINH    equ 180

; =============================================================================
; Entry (SPEC.md 20.2): create the splash window, register menus + About,
; prepare the replayer. No drawing, no spawn, no fullscreen - no lock here.
; =============================================================================
trk_entry:
    push si
    push di
    call mp_init                    ; first: mp_* may clobber, and the CF we
                                    ; owe the loader comes from wm_create
    call OSAPI_CPU_INFO             ; AL = tier (SPEC.md 41.8); a tier-0
    or al, al                       ; machine gets XT mode pre-armed with
    jnz .cpu                        ; its menu item already relabeled
    mov byte [mp_xt], 1             ; (SPEC.md 45.9) - no table to rebuild,
    mov word [trk_mi_file + 2], trk_s_xton  ; nothing is loaded yet
.cpu:
    call OSAPI_VIDEO                ; AX = w, BX = h, CX = first dock row
    sub ax, TRK_WINW                ; centre the frame on the screen...
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [trk_tpl + WT_X], ax
    mov ax, cx                      ; ...and in the desktop band; wm_fit
    sub ax, MBAR_H                  ; clamps whatever does not fit (CGA)
    sub ax, TRK_WINH
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, MBAR_H
    mov [trk_tpl + WT_Y], ax

    mov si, trk_tpl
    call OSAPI_WM_CREATE            ; out BX = window ptr, CF on full
    jc .out
    mov [trk_win], bx
    mov si, trk_menus
    call OSAPI_MENU_SET             ; preserves registers AND flags, so the
    mov si, trk_about               ; loader's CF survives to the ret
    call OSAPI_ABOUT_SET
.out:
    pop di
    pop si
    ret
; =============================================================================
; The UI task's half: paint, keys, clicks, menu, About, file dialog
; =============================================================================

; -----------------------------------------------------------------------------
; trk_paint - W_PAINT: layout init (once), worker hire (retried), full redraw
; in:  SI = window ptr; caller holds the gfx lock
; -----------------------------------------------------------------------------
trk_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call trk_reap                   ; F00/watchdog leftovers close on any UI
                                    ; event (SPEC.md 45.2)
    cmp byte [tui_inited], 0
    jne .l1
    call tui_layout_init
.l1:
    call trk_hire                   ; idempotent; refusal is transient, so it
    call tui_draw_all               ; is retried every paint, never latched
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; trk_hire - spawn the worker, once (the ark_hire shape)
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it); preserves all registers
; -----------------------------------------------------------------------------
trk_hire:
    push ax
    push bx
    cmp byte [trk_hired], 0
    jne .out
    mov ax, trk_worker
    mov bx, [trk_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [trk_hired], 1
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_onkey - W_ONKEY (SPEC.md 45): the whole player is drivable from here,
;             because fullscreen makes the menu bar unreachable.
;   Enter      play song          Space  stop / play toggle
;   P          loop this pattern  L      Load... (Standard File dialog)
;   Left/Right song position      Up/Dn  scroll rows (stopped only)
;   1..4       channel mutes      F      fullscreen toggle
;   Esc        exit fullscreen (windowed: ignored)
; Windowed and never yet fullscreen-ed, ANY key enters fullscreen - the
; splash's promise - through the same trk_fs_enter every other path uses.
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
trk_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax                      ; BL = ascii, BH = scan
    call trk_reap                   ; F00/watchdog leftovers close first
    call trk_abdismiss              ; any key takes the About panel down and
    jc .out                         ; is spent doing it
    cmp byte [trk_fs], 0
    jne .keys
    cmp byte [trk_fsever], 0
    jne .keys
    call trk_fs_enter               ; the splash promise: any key, once
    jmp .out
.keys:
    or bl, bl                       ; the keypad trap: '4'/'6' arrive with
    jnz .ascii                      ; arrow scan codes - ascii==0 first
    cmp bh, 0x4B                    ; left arrow: previous song position
    je .prev
    cmp bh, 0x4D                    ; right arrow: next
    je .next
    cmp bh, 0x48                    ; up: scroll rows while stopped
    je .up
    cmp bh, 0x50                    ; down
    je .down
    jmp .out
.prev:
    mov al, -1
    call mp_setpos                  ; the worker's change detection redraws
    jmp .out
.next:
    mov al, 1
    call mp_setpos
    jmp .out
.up:
    cmp byte [mp_playing], 0
    jne .out
    cmp byte [tui_vrow], 0
    je .out
    dec byte [tui_vrow]
    jmp .out
.down:
    cmp byte [mp_playing], 0
    jne .out
    cmp byte [tui_vrow], 63
    jae .out
    inc byte [tui_vrow]
    jmp .out

.ascii:
    cmp bl, 27                      ; Esc: leave fullscreen (windowed: no-op)
    je .esc
    cmp bl, 13                      ; Enter: play the song
    je .play
    cmp bl, ' '                     ; Space: stop / play toggle
    je .space
    cmp bl, 'p'
    je .pat
    cmp bl, 'P'
    je .pat
    cmp bl, 'l'
    je .load
    cmp bl, 'L'
    je .load
    cmp bl, 'f'
    je .fstog
    cmp bl, 'F'
    je .fstog
    cmp bl, 'x'
    je .xt
    cmp bl, 'X'
    je .xt
    cmp bl, 'r'
    je .rcyc
    cmp bl, 'R'
    je .rcyc
    cmp bl, 's'
    je .smooth
    cmp bl, 'S'
    je .smooth
    cmp bl, '1'
    jb .out
    cmp bl, '4'
    ja .out
    mov al, bl                      ; 1..4: channel mute toggle; the worker's
    sub al, '1'                     ; scope redraw shows it next frame
    call mp_mutetog
    jmp .out
.esc:
    cmp byte [trk_fs], 0
    je .out
    call trk_fs_exit
    jmp .out
.xt:
    call trk_xt_toggle
    jmp .out
.rcyc:
    mov al, [trk_rsel]              ; R cycles 11 -> 22 -> 44 -> 11
    inc al                          ; (SPEC.md 45.10 - fullscreen reach)
    cmp al, 3
    jb .rset
    xor al, al
.rset:
    call trk_rate_set
    jmp .out
.smooth:
    call trk_smooth_toggle          ; S (SPEC.md 45.11 - fullscreen reach)
    jmp .out
.play:
    mov al, 0
    call trk_play
    jmp .out
.space:
    cmp byte [mp_playing], 0
    je .play
    call trk_play_stop
    mov si, trk_s_stop
    call tui_msg
    jmp .out
.pat:
    mov al, 1
    call trk_play
    jmp .out
.load:
    call trk_do_open
    jmp .out
.fstog:
    cmp byte [trk_fs], 0
    je .fenter
    call trk_fs_exit
    jmp .out
.fenter:
    call trk_fs_enter
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; trk_onclick - W_ONCLICK: windowed, a click enters fullscreen (the splash's
;               other promise); fullscreen, a click in a scope cell toggles
;               that channel's mute, exactly like its number key.
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
trk_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call trk_reap                   ; F00/watchdog leftovers close first
    call trk_abdismiss              ; a click takes the About panel down too
    jc .out
    cmp byte [trk_fs], 0
    jne .full
    call trk_fs_enter
    jmp .out
.full:
    call tui_scopehit               ; CX/DX screen coords -> CF=0, AL = 0..3
    jc .out
    call mp_mutetog
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; trk_oncmd - AM_ONCMD (SPEC.md 12.2): File > Open..., View > Fullscreen.
; Reachable windowed only (fullscreen hides the bar), which is why every
; command also has a key.
; in:  AL = item, AH = menu, SI = our window, BX = the set; gfx lock held
; -----------------------------------------------------------------------------
trk_oncmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax                      ; BL = item, BH = menu
    call trk_reap                   ; F00/watchdog leftovers close first
    call trk_abdismiss
    jc .out
    or bh, bh
    jz .file
    cmp bh, 2                       ; the Rate menu (SPEC.md 45.10)
    je .rate
    cmp bh, 1
    jne .out
    or bl, bl                       ; View > Fullscreen: toggle
    jz .vfull
    cmp bl, 1                       ; View > Smooth (SPEC.md 45.11)
    jne .out
    call trk_smooth_toggle
    jmp .out
.vfull:
    cmp byte [trk_fs], 0
    je .enter
    call trk_fs_exit
    jmp .out
.enter:
    call trk_fs_enter
    jmp .out
.file:
    or bl, bl                       ; File > Open...
    jz .fopen
    cmp bl, 1                       ; File > XT Mode (the relabeling item)
    jne .out
    call trk_xt_toggle
    jmp .out
.fopen:
    call trk_do_open
    jmp .out
.rate:
    mov al, bl                      ; Rate > 11/22/44 kHz (SPEC.md 45.10)
    call trk_rate_set
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; trk_about - the OSAPI_ABOUT_SET handler: panel-in-content, the [ark_abon]
;             pattern. The worker checks [trk_abon] under the lock, right
;             after the clip is armed, and drops the whole frame while it is
;             set - audio keeps feeding, only the drawing pauses.
; in:  SI = our window ptr; gfx lock held
; -----------------------------------------------------------------------------
trk_about:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [trk_abon], 1
    call tui_draw_all               ; draws the panel last while the flag is up
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; trk_abdismiss - take the About panel down if it is up
; out: CF=1 the key/click was spent doing it; preserves every register
; -----------------------------------------------------------------------------
trk_abdismiss:
    cmp byte [trk_abon], 0
    je .none
    mov byte [trk_abon], 0
    call tui_draw_all
    stc
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; trk_do_open - put up the Standard File dialog (Open mode)
; in:  gfx lock held (a key or menu handler); preserves all registers
; A refusal (one already up, no room) is silently ignored: the dialog that is
; up IS the answer. Cancel calls nothing, so no state is parked here - the
; worker's periodic top-band repaint erases the fdlg-cancel menu-bar strip
; (SPEC.md 38.6/45).
; -----------------------------------------------------------------------------
trk_do_open:
    push ax
    push bx
    push si
    push di
    mov al, FDLG_OPEN
    mov bx, [trk_win]
    mov di, trk_fdone
    xor si, si
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_trim - give back the part of the blob claim the file did not need
;
; in:  [trk_modseg] = the claim, [mp_bloblen_hi]:[mp_bloblen_lo] = bytes read
; out: [trk_modseg] / [trk_capk] updated; preserves every register
;
; The claim is sized BEFORE the file's size is known - the dialog's completion
; proc is handed a name, not a directory entry - so it is min(largest run,
; 128KB) and a 5.6KB module would sit on 128KB of heap until the next load or
; teardown. One call gives the difference back.
;
; This is a call and not a redesign because OSAPI_MEM_REGROW SHRINKS IN PLACE
; (SPEC.md 50.3.1): the record's length changes and nothing moves. Claim-copy-
; free could not do it - it needs both blocks at once, and may hand back a
; different base - which is why the tree this came from documented the
; over-claim as a thing it could not fix rather than a thing it had not.
;
; Called before mp_load, so no sample pointer exists yet to be invalidated
; even on the impossible path where a shrink relocated. mp_load bounds every
; read against [mp_bloblen_*] rather than the claim, so a claim trimmed to
; exactly those bytes cannot narrow what it may look at.
; -----------------------------------------------------------------------------
trk_trim:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mp_bloblen_lo]
    mov dx, [mp_bloblen_hi]
    add ax, 1023                    ; round UP: the heap's granularity is KB,
    adc dx, 0                       ; and a truncating divide would hand back
    mov cl, 10                      ; the tail of the file with the tail of
    shr ax, cl                      ; the claim
    mov bx, dx
    mov cl, 6
    shl bx, cl                      ; DX:AX >> 10 = (DX << 6) + (AX >> 10),
    or ax, bx                       ; and the 128KB cap keeps it under 129
    jnz .go
    inc ax                          ; 0 KB is a refusal, not a claim
.go:
    cmp ax, [trk_capk]
    jae .out                        ; the file filled it: nothing to give back
    mov dx, [trk_modseg]
    call OSAPI_MEM_REGROW
    jc .out                         ; refused: keep the oversized claim, which
    mov [trk_modseg], dx            ; costs heap and breaks nothing
    mov [trk_capk], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_fdone - the file-dialog completion proc (SPEC.md 38.6). UI task, gfx
;             lock held, ES:DI = the chosen name with ES = KERNEL_SEG, valid
;             for this call only - so it is copied out FIRST.
;
; The load path: stop playback, free the previous module grant, size a new
; grant from OSAPI_MEM_AVAIL (capped at 128KB), read the
; whole file with OSAPI_FILE_READBIG (DX:CX capacity in bytes - this is the
; slot that exists because real MODs exceed dskw_read's 64KB ceiling), then
; mp_load validates and builds tables. Any failure frees the grant and puts
; its verdict on the status line. Success starts playback and repaints -
; which under WF_FULL also covers the menu-bar strip fdlg_close painted.
; -----------------------------------------------------------------------------
trk_fdone:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, di                      ; copy the kernel's name buffer out NOW:
    mov di, trk_fname               ; it dies when this call returns
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [trk_fname + 12], 0
    push ds
    pop es                          ; ES = DS again (the callback default)

    call trk_play_stop              ; silence + close + DRAIN before the blob
                                    ; moves: trk_stream_close spins out the
                                    ; worker's in-flight feed pass, so no
                                    ; mp_mixch is mid-fetch from the old
                                    ; grant past this line ([trk_mixing])
    mov byte [mp_loaded], 0         ; the old blob is about to be freed: no
                                    ; reader may trust it past this line
    cmp word [trk_modseg], 0
    je .alloc
    mov dx, [trk_modseg]            ; DX, not AX (docs/PORTING.md 8)
    call OSAPI_MEM_FREE
    mov word [trk_modseg], 0
.alloc:
    call OSAPI_MEM_AVAIL            ; AX = LARGEST contiguous run in KB, and
    or ax, ax                       ; BX = the total (this fork counts KB, not
    jz .nomem                       ; paragraphs - docs/PORTING.md 8)
    cmp ax, 128                     ; cap the grant at 128KB - bigger than any
    jbe .sized                      ; sane 4-channel MOD
    mov ax, 128
.sized:
    mov [trk_capk], ax
    call OSAPI_MEM_CLAIM            ; AX = KB -> DX = base segment, CF=1
    jc .nomem                       ; refused (the answer moves register too)
    mov [trk_modseg], dx

    mov ax, [trk_capk]              ; DX:CX = capacity in bytes = KB * 1024
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    mov cx, ax
    mov es, [trk_modseg]
    mov si, trk_fname
    call OSAPI_FILE_READBIG         ; out CF=0, DX:AX = bytes read
    push ds
    pop es
    jc .rderr
    mov [mp_bloblen_lo], ax
    mov [mp_bloblen_hi], dx
    call trk_trim                   ; ...and hand back what it did not need
    mov ax, [trk_modseg]            ; AFTER the trim, so a claim that somehow
    mov [mp_blobseg], ax            ; moved is still the one mp_load indexes
    call mp_load                    ; CF=1, AX = offset of a NUL error string
    jc .lderr
    mov si, mp_title                ; the loaded title becomes the status line
    call tui_msg
    mov al, 0
    call trk_play                   ; caps-gated: no SB machine stays a viewer
    call tui_draw_all               ; the mandatory completion repaint
    jmp .out

.nomem:
    mov si, trk_s_nomem
    jmp .fail
.rderr:
    cmp ax, FERR_BIG
    je .big
    cmp ax, FERR_NOENT
    je .noent
    mov si, trk_s_ioerr
    jmp .failfree
.big:
    mov si, trk_s_toobig
    jmp .failfree
.noent:
    mov si, trk_s_noent
    jmp .failfree
.lderr:
    mov si, ax                      ; mp_load's own verdict string
.failfree:
    push si
    mov dx, [trk_modseg]            ; DX, not AX (docs/PORTING.md 8)
    or dx, dx
    jz .npop
    call OSAPI_MEM_FREE
    mov word [trk_modseg], 0
.npop:
    pop si
.fail:
    call tui_msg
    call tui_draw_all
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; =============================================================================
; Fullscreen (SPEC.md 11.2) - entered only from W_ONKEY / W_ONCLICK /
; AM_ONCMD, all of which hold the gfx lock, which OSAPI_FULLSCREEN requires.
; [trk_fs] is flipped BEFORE the call: entering fronts + repaints under the
; held lock, so the W_PAINT that runs inside the call must already see the
; fullscreen answer or it would draw the splash across the bare screen.
; =============================================================================
trk_fs_enter:
    push ax
    push bx
    cmp byte [trk_fs], 0
    jne .out
    mov byte [trk_fs], 1
    mov al, 1
    mov bx, [trk_win]
    call OSAPI_FULLSCREEN
    jnc .ok
    mov byte [trk_fs], 0            ; refused: someone else owns the screen
    jmp .out
.ok:
    mov byte [trk_fsever], 1
    cmp byte [trk_smooth], 0        ; Smooth (SPEC.md 45.11): borrow the 32
    je .draw                        ; back buffer for the fullscreen stay
    mov al, 1
    call OSAPI_GFX_DBUF             ; lock held (we are in a key/click/menu
    jc .draw                        ; callback); CF=1 = mono or small: draw
    mov [trk_bbprev], al            ; direct as before
    mov byte [trk_bbheld], 1
.draw:
    call tui_draw_all
.out:
    pop bx
    pop ax
    ret

trk_fs_exit:
    push ax
    push bx
    cmp byte [trk_fs], 0
    je .out
    cmp byte [trk_bbheld], 0        ; hand back the user's buffer state
    je .fs                          ; BEFORE the exit repaint, so the
    mov al, [trk_bbprev]            ; desktop redraw takes the mode the
    call OSAPI_GFX_DBUF             ; user actually chose (SPEC.md 45.11);
    mov byte [trk_bbheld], 0        ; a close-while-fullscreen never runs
                                    ; this - recorded in 45.11, not fenced
.fs:
    mov byte [trk_fs], 0
    mov al, 0
    mov bx, [trk_win]
    call OSAPI_FULLSCREEN           ; restores geometry + wm_paint_all, which
                                    ; re-enters trk_paint windowed
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Stream plumbing - UI-context only (verbs 0/2 and the first verb-7 alloc
; stay UI-callback-only under the amended SPEC.md 20.3; the worker gets
; feed/status/stage).
; =============================================================================

; -----------------------------------------------------------------------------
; trk_play - start playback
; in:  AL = 0 play song from the top / 1 loop the current pattern
;      gfx lock held (key handler, menu, completion proc)
; Refusals are status-line messages, never aborts: no module, no PCM_BG sink
; (the no-SB machine keeps the viewer), no staging memory.
; -----------------------------------------------------------------------------
trk_play:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [trk_pmode], al
    cmp byte [mp_loaded], 0
    je .noload
    call OSAPI_SND_CAPS             ; AX = merged caps word
    test ax, SND_CAP_PCM_BG
    jz .nosb
    call trk_stream_close           ; restart = close + reopen; the close's
                                    ; drain parks the worker, which is what
                                    ; makes the mp_start reset and the two
                                    ; UI-task mp_gen pre-rolls below safe
                                    ; against a mid-mix feed pass
    cmp byte [trk_ghave], 0         ; the 16KB ring grant, allocated once and
    jne .granted                    ; force-freed by teardown (SPEC.md 34.3)
    mov al, 7
    mov ah, 0                       ; sub-op 0 = alloc
    mov cx, TRK_RING
    call OSAPI_SND_STREAM           ; out AX = 0 ok, SI = grant offset
    or ax, ax
    jnz .nogrant
    mov [trk_grant], si
    mov byte [trk_ghave], 1
.granted:
    cmp byte [mp_xt], 0             ; XT mode overrides the Rate menu with
    je .rsel                        ; its own 5,500 Hz (SPEC.md 45.9/45.10)
    mov ax, TRK_RATE_XT
    jmp .rate
.rsel:
    mov bl, [trk_rsel]              ; the Rate menu's pick: 0/1/2
    xor bh, bh
    shl bx, 1
    mov ax, [trk_rates + bx]
.rate:
    mov [mp_mixrate], ax
    mov al, [trk_pmode]
    call mp_start
    mov word [trk_total], 0
    call trk_mix_stage              ; pre-mix two halves at ring offsets
    call trk_mix_stage              ; 0 and 2048: the open's initial CX
    cmp word [mp_mixrate], 22222    ; the wide regime plays 4KB kernel
    jbe .preok                      ; halves (SPEC.md 34.5): pre-roll two
    call trk_mix_stage              ; more so the open covers both wide
    call trk_mix_stage              ; halves, not one
.preok:
    mov al, 0                       ; verb 0: open-out, ring mode - the flag
    mov ah, SND_OPENF_RING          ; rides AH (verb 0 carries no handle,
                                    ; SPEC.md 20.3; DX stays the plain rate)
    mov si, [trk_grant]
    mov cx, [trk_total]             ; initial valid total = the pre-roll
    mov dx, [mp_mixrate]            ; the mode's rate, set above
    call OSAPI_SND_STREAM           ; out AL = 0 with AH = handle, else err
    or al, al
    jnz .ofail
    mov [trk_hand], ah
    mov byte [trk_ended], 0         ; re-arm BEFORE publishing: the worker
    mov byte [trk_sopen], 1         ; keys every pass on trk_sopen, and a
                                    ; pass must never see the new stream
                                    ; through the old session's flags
    jmp .out
.ofail:
    call mp_stop
    mov si, trk_s_snderr
    cmp ax, 2                       ; err 2 = rate refused: the 44 kHz pick
    jne .ofmsg                      ; on a pre-4.x DSP (SPEC.md 45.10)
    mov si, trk_s_norate
.ofmsg:
    call tui_msg
    jmp .out
.noload:
    mov si, trk_s_noload
    call tui_msg
    jmp .out
.nosb:
    mov si, trk_s_nosb
    call tui_msg
    jmp .out
.nogrant:
    mov si, trk_s_nomem
    call tui_msg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_mix_stage - mix one 2048-byte half and stage it at the ring position
;                 [trk_total] names, then advance [trk_total]. Used by the
;                 UI pre-roll above and by the worker's feed loop: 16384 is
;                 a multiple of 2048, so a half NEVER crosses the ring seam
;                 and the copy needs no split (SPEC.md 34.5 ring rule).
; preserves all registers
; -----------------------------------------------------------------------------
trk_mix_stage:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, TRK_HALF
    call mp_gen                     ; renders into mp_outbuf, advances the
                                    ; replayer; clobbers freely (mp_* rule)
    mov di, [trk_total]
    and di, TRK_RMASK
    add di, [trk_grant]             ; physical grant offset of stream byte n
    mov si, mp_outbuf
    mov cx, TRK_HALF
    mov al, 6                       ; verb 6: stage (kernel copies from our
    call OSAPI_SND_STREAM           ; segment; worker-safe since the amendment)
    add word [trk_total], TRK_HALF  ; free-running 16-bit, mod 65536
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_stream_close / trk_play_stop
; UI context only (verb 2). The close box needs neither: teardown force-frees
; the stream, the pool grant and the heap claim (SPEC.md 34.3/50.2).
; -----------------------------------------------------------------------------
trk_stream_close:
    push ax
    cmp byte [trk_sopen], 0
    je .drain
    mov al, 2                       ; verb 2: close (fine on an ended stream -
    mov ah, [trk_hand]              ; stale is an answer, not an error)
    call OSAPI_SND_STREAM
    mov byte [trk_sopen], 0
.drain:
    cmp byte [trk_mixing], 0        ; park the worker (SPEC.md 45.2): a feed
    jne .drain                      ; pass already past its trk_sopen guard
                                    ; runs to completion first. Deadlock-free
                                    ; (trk_feed never takes the gfx lock this
                                    ; task holds; pre-emption keeps the worker
                                    ; running) and bounded (.fill re-checks
                                    ; trk_sopen per half). After this line the
                                    ; worker cannot be inside mp_gen, so the
                                    ; caller may reset the replayer, pre-roll
                                    ; on the UI task, or free the module blob.
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_reap - close a stopped-but-open stream from UI context (SPEC.md 45.2).
; F00 (or a watchdog end) stops playback on the worker, which cannot close
; (verb 2 is UI-callback-only) - it only flags. Every UI callback runs this
; first, so the machine's single stream record is held no longer than the
; tracker's next paint, key, click or menu command.
; preserves all registers
; -----------------------------------------------------------------------------
trk_reap:
    cmp byte [trk_sopen], 0
    je .out
    cmp byte [trk_ended], 0
    jne .close
    cmp byte [mp_playing], 0        ; F00: the worker latches trk_ended once
    jne .out                        ; the ring drains, but close early if a
.close:                             ; callback lands first - the tail already
    call trk_stream_close           ; played or the user is acting anyway
.out:
    ret

trk_play_stop:
    call trk_stream_close
    call mp_stop
    ret

; -----------------------------------------------------------------------------
; trk_smooth_toggle - flip Smooth (SPEC.md 45.11). UI context, lock held
; (key S or View > Smooth). While fullscreen the change applies live:
; arming borrows the SPEC.md 32 back buffer (previous state banked for the
; exit hand-back), disarming returns the user's mode now. Windowed it just
; decides what the next fullscreen entry does.
; -----------------------------------------------------------------------------
trk_smooth_toggle:
    push ax
    push bx
    push si
    mov al, [trk_smooth]
    xor al, 1
    mov [trk_smooth], al
    cmp byte [trk_fs], 0
    je .menu
    or al, al
    jz .disarm
    cmp byte [trk_bbheld], 0        ; arm live (unless already borrowed)
    jne .menu
    mov al, 1
    call OSAPI_GFX_DBUF
    jc .menu                        ; mono/small: nothing to smooth
    mov [trk_bbprev], al
    mov byte [trk_bbheld], 1
    jmp .menu
.disarm:
    cmp byte [trk_bbheld], 0
    je .menu
    mov al, [trk_bbprev]
    call OSAPI_GFX_DBUF
    mov byte [trk_bbheld], 0
.menu:
    mov si, trk_s_smoff             ; relabel + MENU_SET (the kernel holds
    cmp byte [trk_smooth], 0        ; a COPY of the set, SPEC.md 12.2)
    je .lab
    mov si, trk_s_smon
.lab:
    mov [trk_mi_view + 2], si
    mov bx, [trk_win]
    mov si, trk_menus
    call OSAPI_MENU_SET
    mov si, trk_s_smmoff
    cmp byte [trk_smooth], 0
    je .msg
    mov si, trk_s_smmon
.msg:
    call tui_msg
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_rate_set - pick the Rate menu's sample rate (AL = 0/1/2 = 11/22/44
; kHz; SPEC.md 45.10). UI context, lock held (key R or the Rate menu).
; Playing stops first, like the XT toggle; the pick lands at the next Play.
; The active item becomes its own MENU_DIS twin (the radio idiom) and
; MENU_SET re-runs - the kernel holds a COPY of the set (SPEC.md 12.2).
; -----------------------------------------------------------------------------
trk_rate_set:
    push ax
    push bx
    push cx
    push si
    push di
    cmp al, [trk_rsel]
    je .done                        ; same pick: nothing to do
    mov [trk_rsel], al
    cmp byte [mp_playing], 0
    je .idle
    call trk_play_stop              ; drains the worker (SPEC.md 45.2)
.idle:
    cmp byte [trk_sopen], 0         ; a drained ring left open by F00/stop
    je .menu                        ; paths closes before the rate changes
    call trk_stream_close
.menu:
    xor si, si                      ; repoint the three items: the active
.mi:                                ; one gets its disabled twin
    mov bx, [trk_rplain + si]
    mov al, [trk_rsel]
    xor ah, ah
    shl ax, 1
    cmp ax, si
    jne .plain
    mov bx, [trk_rdis + si]
.plain:
    mov [trk_mi_rate + si], bx
    add si, 2
    cmp si, 6
    jb .mi
    mov bx, [trk_win]
    mov si, trk_menus
    call OSAPI_MENU_SET
    mov bl, [trk_rsel]
    xor bh, bh
    shl bx, 1
    mov si, [trk_rmsg + bx]
    call tui_msg
.done:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_xt_toggle - flip XT mode (SPEC.md 45.9). UI context, lock held (key X
; or File > XT Mode). Playing stops first - the mode is a table rebuild plus
; rate/mixer constants, never a mid-stream switch; the user presses Play
; again. The menu item is relabeled and MENU_SET re-called (the kernel holds
; a COPY of the set, SPEC.md 12.2 - a repoint alone changes nothing).
; -----------------------------------------------------------------------------
trk_xt_toggle:
    push ax
    push bx
    push cx
    push si
    push di
    cmp byte [mp_playing], 0
    je .idle
    call trk_play_stop              ; drains the worker (SPEC.md 45.2)
.idle:
    cmp byte [trk_sopen], 0         ; a drained ring left open by F00/stop
    je .flip                        ; paths closes now, before the rate flips
    call trk_stream_close
.flip:
    mov al, [mp_xt]
    xor al, 1
    call mp_setxt                   ; stores the mode + rebuilds the volume
                                    ; table when a module is loaded (the
                                    ; deliberate sub-second freeze, 45.9)
    mov si, trk_s_xtoff
    cmp byte [mp_xt], 0
    je .lab
    mov si, trk_s_xton
.lab:
    mov [trk_mi_file + 2], si
    mov bx, [trk_win]
    mov si, trk_menus
    call OSAPI_MENU_SET
    mov si, trk_s_xtmoff
    cmp byte [mp_xt], 0
    je .msg
    mov si, trk_s_xtmon
.msg:
    call tui_msg
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The worker (SPEC.md 20.6): ALIVE -> SLEEP 1 -> feed audio lock-free ->
; one short lock hold to draw. Never returns; OSAPI_TASK_ALIVE is where it
; dies when the close box is clicked.
; =============================================================================
trk_worker:
.loop:
    mov bx, [trk_win]
    call OSAPI_TASK_ALIVE           ; lock NOT held here (rule 4)
    mov ax, 1
    call OSAPI_TASK_SLEEP           ; ~18 wakes a second
    call trk_feed                   ; audio first - it must not starve behind
    call trk_render                 ; a slow frame ([trk_abon] drops the frame
    jmp .loop                       ; inside trk_render, under the lock)

; -----------------------------------------------------------------------------
; trk_feed - keep the ring ahead of the DSP. Lock-free, worker context: verbs
;            3 (status), 6 (stage) and 1 (feed) are any-task under the
;            amended SPEC.md 20.3; verbs 0/2 are not, so an ENDED stream is
;            only flagged here and closed later on the UI task (trk_reap).
;
; [trk_mixing] brackets the WHOLE pass, set before the entry guards and
; cleared last (SPEC.md 45.2): trk_stream_close drains it, which is what
; makes the UI task's replayer resets, mp_gen pre-rolls and blob frees safe
; against a worker suspended anywhere in here - mp_gen is not reentrant.
; A pass that entered between a close and a reopen sees trk_sopen = 0 and
; falls straight out without touching mp_* state.
;
; F00 stops the replayer (mp_playing = 0) but may not close from here: the
; pass keeps polling until consumed catches [trk_total] - the stop row's
; tail has then been heard - and latches trk_ended for trk_reap, so the
; machine's single stream record is not held for the rest of the session.
;
; lead = total - consumed, both free-running 16-bit counters, so the
; subtraction is exact across wrap. Feeding keeps lead <= RING - HALF, which
; implies the kernel's own bound (new_total - fed <= RL) because fed >=
; consumed - and stops after TRK_MAXFEED halves so one wake never mixes
; unboundedly. The lead is recomputed against the consumed count polled at
; entry; consumed only grows, so the stale answer under-reports room - the
; conservative side.
; -----------------------------------------------------------------------------
trk_feed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [trk_mixing], 1        ; FIRST, before the guards: a pass past
                                    ; its guards must never be invisible to
                                    ; trk_stream_close's drain
    cmp byte [trk_sopen], 0
    je .out
    cmp byte [trk_ended], 0
    jne .out
    mov al, 3                       ; verb 3: status - AX = state, DX =
    mov ah, [trk_hand]              ; consumed (free-running in ring mode)
    call OSAPI_SND_STREAM
    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
    cmp byte [mp_playing], 0        ; F00 stopped the mixer: wait for the
    jne .go                         ; ring to drain, then flag for the
    cmp dx, [trk_total]             ; UI-side close - mp_stop already ran
    jne .out                        ; (the effect itself), so only the
    mov byte [trk_ended], 1         ; latch is left
    jmp .out
.go:
    mov byte [trk_halves], 0
.fill:
    cmp byte [trk_sopen], 0         ; a UI close mid-pass ends the burst
    je .out                         ; (bounds trk_stream_close's drain wait)
    cmp byte [mp_playing], 0        ; F00 mid-burst stops the mixer
    je .out
    cmp byte [trk_halves], TRK_MAXFEED
    jae .out
    mov ax, [trk_total]
    sub ax, dx                      ; AX = lead, 0..TRK_RING
    cmp ax, TRK_RING - TRK_HALF
    ja .out                         ; no room for a whole half
    push dx
    call trk_mix_stage              ; mix + stage + total += 2048
    mov cx, [trk_total]             ; verb 1: feed - new total valid length
    mov al, 1
    mov ah, [trk_hand]
    call OSAPI_SND_STREAM
    pop dx
    or ax, ax
    jnz .out                        ; refused feed: try again next wake
    inc byte [trk_halves]
    jmp .fill
.dead:
    call mp_stop                    ; watchdog-ended streams never resume
    mov byte [trk_ended], 1         ; (SPEC.md 34.5); trk_reap or the next
                                    ; Play closes it on the UI task
.out:
    mov byte [trk_mixing], 0        ; the drain gate reopens LAST
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_render - the worker's one lock hold (SPEC.md 20.6 rules 3/5): geometry
;              re-check, clip armed, [trk_abon] honoured under the lock so
;              the About panel is never painted over, then the dynamic-only
;              redraw.
; -----------------------------------------------------------------------------
trk_render:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_GFX_LOCK
    mov bx, [trk_win]
    call OSAPI_WM_GEOM              ; CF=1: hidden - draw nothing
    jc .unlock
    call OSAPI_WM_CLIP_SET          ; CF=1: fully covered - skip the frame
    jc .unlock
    cmp byte [trk_abon], 0          ; checked HERE, under the lock, after the
    jne .unlock                     ; clip: the [ark_abon] rule verbatim
    call tui_draw_dyn
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

; --- window template (SPEC.md 11: 16 bytes, 8 words); x/y set by trk_entry ----
trk_tpl:
    dw 0, 0, TRK_WINW, TRK_WINH
    dw trk_ttl, trk_paint, trk_onkey, trk_onclick

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
    OS88_MENUSET trk_menus, trk_m_name, trk_oncmd
        OS88_MENU trk_m_file, trk_mi_file, 2
        OS88_MENU trk_m_view, trk_mi_view, 2
        OS88_MENU trk_m_rate, trk_mi_rate, 3
    OS88_MENUSET_END trk_menus

trk_m_name:  db 'Tracker', 0
trk_m_file:  db 'File', 0
trk_mi_file: dw trk_s_open, trk_s_xtoff ; item 1 repointed by trk_xt_toggle
                                        ; (the sol_dealmenu relabel idiom -
                                        ; invisible until MENU_SET re-runs)
trk_s_open:  db 'Open...', 0
trk_s_xtoff: db 'XT Mode: Off', 0
trk_s_xton:  db 'XT Mode: On', 0
trk_m_view:  db 'View', 0
trk_mi_view: dw trk_s_fullm, trk_s_smoff ; item 1 repointed by
                                        ; trk_smooth_toggle (SPEC.md 45.11)
trk_s_fullm: db 'Fullscreen', 0
trk_s_smon:  db 'Smooth: On', 0
trk_s_smoff: db 'Smooth: Off', 0
trk_m_rate:  db 'Rate', 0
trk_mi_rate: dw trk_s_r11d, trk_s_r22, trk_s_r44 ; the active pick is its
                                        ; own MENU_DIS twin - the radio
                                        ; idiom (SPEC.md 45.10); repointed
                                        ; by trk_rate_set + MENU_SET
trk_s_r11:   db '11 kHz', 0
trk_s_r11d:  db MENU_DIS, '11 kHz', 0
trk_s_r22:   db '22 kHz', 0
trk_s_r22d:  db MENU_DIS, '22 kHz', 0
trk_s_r44:   db '44 kHz', 0
trk_s_r44d:  db MENU_DIS, '44 kHz', 0
trk_rplain:  dw trk_s_r11, trk_s_r22, trk_s_r44
trk_rdis:    dw trk_s_r11d, trk_s_r22d, trk_s_r44d
trk_rates:   dw TRK_RATE, TRK_RATE22, TRK_RATE44
trk_rmsg:    dw trk_s_m11, trk_s_m22, trk_s_m44
trk_s_m11:   db 'Rate: 11 kHz - Enter plays', 0
trk_s_m22:   db 'Rate: 22 kHz - Enter plays', 0
trk_s_m44:   db 'Rate: 44 kHz - Enter plays', 0

trk_ttl:     db 'Tracker', 0

; --- status-line strings -------------------------------------------------------
trk_s_stop:   db 'Stopped', 0
trk_s_noload: db 'No module loaded - L loads one', 0
trk_s_nosb:   db 'No Sound Blaster: viewer only', 0
trk_s_nomem:  db 'Out of memory', 0
trk_s_toobig: db 'File too big', 0
trk_s_noent:  db 'File not found', 0
trk_s_ioerr:  db 'Disk error', 0
trk_s_snderr: db 'Sound open failed', 0
trk_s_xtmon:  db 'XT mode on - Enter plays', 0
trk_s_xtmoff: db 'XT mode off - Enter plays', 0
trk_s_norate: db '44 kHz needs a DSP 4.x card', 0
trk_s_smmon:  db 'Smooth on', 0
trk_s_smmoff: db 'Smooth off', 0

; =============================================================================
; The other two thirds of the package
; =============================================================================
%include "trkplay.inc"
%include "trkui.inc"

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes TRK_BSS bytes after the image; every
; name is an equ offset from os88_image_end via the TRKB/TRKW/TRKBUF macros
; defined at the top of this file)
; =============================================================================

    TRKW trk_win                    ; our window ptr (opaque handle)
    TRKB trk_fs                     ; 1 = fullscreen active (read by trkui)
    TRKB trk_fsever                 ; the splash promise has been kept once
    TRKB trk_hired                  ; the worker exists
    TRKB trk_abon                   ; the About panel is up; worker frames drop
    TRKB trk_pmode                  ; 0 = song, 1 = pattern loop

; --- the module blob (a heap claim, SPEC.md 50) -------------------------------
    TRKW trk_modseg                 ; grant base segment, 0 = none
    TRKW trk_capk                   ; its size in KB
    TRKBUF trk_fname, 13            ; the chosen 8.3 name, copied out of the
                                    ; kernel's buffer during the completion call

; --- the ring stream (SPEC.md 34.5 ring mode) ----------------------------------
    TRKB trk_ghave                  ; the 16KB pool grant exists
    TRKW trk_grant                  ; ...its SND_SEG offset
    TRKB trk_sopen                  ; a stream is open
    TRKB trk_hand                   ; ...its handle
    TRKB trk_ended                  ; watchdog/F00-ended: stop feeding, close
                                    ; on the next UI event (trk_reap)
    TRKW trk_total                  ; free-running total bytes mixed (mod 64K)
    TRKB trk_halves                 ; halves fed this wake (bounds the burst)
    TRKB trk_rsel                   ; the Rate menu's pick (SPEC.md 45.10):
                                    ; 0/1/2 = 11/22/44 kHz; bss zeroes to
                                    ; the 11 kHz default
    TRKB trk_smooth                 ; Smooth (SPEC.md 45.11) - bss zeroes:
                                    ; the default is OFF
    TRKB trk_bbprev                 ; ...the user's back-buffer state, banked
    TRKB trk_bbheld                 ; ...1 = we borrowed it (hand back at exit)
    TRKB trk_mixing                 ; the worker is inside a trk_feed pass -
                                    ; trk_stream_close drains it before any
                                    ; UI-task touch of mp_* state or the blob

    OS88_BSS TRK_BSS
    OS88_IMAGE_END
