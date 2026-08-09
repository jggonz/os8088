; =============================================================================
; os8088 - apps/tracker/tracker.asm
;
; Tracker, the tenth software package (SPEC.md 45). A FastTracker II-styled
; 4-channel ProTracker MOD player: launches windowed with a splash card, any
; key or click enters FULLSCREEN (wm_fullscreen's first shipped package
; client, SPEC.md 11.2), Esc returns. Modules load through the Standard File
; dialog into an arena grant via OSAPI_FILE_READ (whose destination walks by
; SEGMENT, SPEC.md 18.4.1, which is what lets a file >= 64KB land in one
; call at all), and play through a ring-mode Sound Blaster background
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

    OS88_HEADER 'TRACKER', trk_entry, 3

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

; --- the extensions this program opens (SPEC.md 54.6, flags bit 1) ------------
; The kernel already ships MOD -> TRACKER as a build-time default, so on the
; apps disk this declares what was true anyway. It earns its 16 bytes on
; EVERY OTHER disk: a declaration carries extensions only and the stem comes
; from the file it was harvested out of, so the TRKLOG build (SPEC.md 45.14),
; which ships as TRKLOG.O88 beside the module it is there to log, claims .MOD
; for ITSELF the moment its folder is browsed. Without it that disk hits the
; default, goes looking for a TRACKER.O88 that is not on it, and the module
; cannot be opened at all - which is exactly how this was found.
    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'MOD'
    OS88_ASSOC16_END

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
TRK_PREROLL equ 6                   ; ring halves staged before the stream is
                                    ; opened - the cushion the worker's first
                                    ; refill has to arrive within. It was TWO
                                    ; (four above 22 kHz, so that a wide 4KB
                                    ; kernel half was covered), which at the
                                    ; XT rate is 744ms and at 22 kHz is 186ms,
                                    ; and the field reports a hitch landing
                                    ; almost exactly there on the first play
                                    ; after a load - the moment the pre-roll
                                    ; runs out and the worker carries the
                                    ; stream alone for the first time
                                    ; (docs/FIELD-NOTES.md). Six is still
                                    ; under the feed's own ceiling of
                                    ; TRK_RING - TRK_HALF (seven halves), so
                                    ; the first wake can top up rather than
                                    ; finding the ring already full, and it
                                    ; stays EVEN, which is what the old
                                    ; wide-rate special case was for: above
                                    ; 22 kHz the kernel plays 4KB halves and
                                    ; an odd pre-roll covers one of them.
                                    ; The cost is four more halves mixed on
                                    ; the UI task inside the Play handler -
                                    ; a tenth of a second on an XT, paid
                                    ; where the user is already waiting
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
    mov byte [trk_cpu0], 1          ; nothing is loaded yet - and the machine
    mov word [trk_mi_file + 2], trk_s_xton  ; itself is remembered (45.9.1)
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
    mov al, 1                       ; keep our CONTENT ORIGIN 8-aligned
    call OSAPI_WM_SNAP              ; (SPEC.md 11.94): mono-only, a no-op on
                                    ; VGA, and it is what lets every text run
                                    ; we draw take font_run's single-store
                                    ; path instead of the erase-and-letter
                                    ; fallback. wm_snap preserves FLAGS, so
                                    ; the loader's CF still survives to .out
    mov si, trk_menus
    call OSAPI_MENU_SET             ; preserves registers AND flags, so the
    mov si, trk_about               ; loader's CF survives to the ret
    call OSAPI_ABOUT_SET
    call trk_arg                    ; were we launched to play a module?
.out:
    pop di
    pop si
    ret
; =============================================================================
; The UI task's half: paint, keys, clicks, menu, About, file dialog
; =============================================================================

; -----------------------------------------------------------------------------
; trk_arg - accept a module handed to us at launch (SPEC.md 54.5)
; in:  nothing; called from trk_entry once the window exists
; out: nothing; preserves all registers AND the flags - the loader's CF is
;      still riding in them
;
; It RECORDS and does not load: trk_fdone shows a message, frees and reclaims
; the module grant and repaints, all of which want the gfx lock HELD, and an
; entry proc holds none (SPEC.md 20.2). The first W_PAINT spends it.
; -----------------------------------------------------------------------------
trk_arg:
    pushf
    push ax
    push bx
    push cx
    push si
    push di
    push es
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the usual case
    jc .out
    mov [trk_argclus], dx
    mov [trk_argdrv], bl
    mov ax, KERNEL_SEG              ; the name is a KERNEL pointer, so ES is
    mov es, ax                      ; loaded rather than trusted
    mov di, trk_argnm
    mov cx, 12
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .copy
.named:
    mov byte [trk_argnm+12], 0
    push ds
    pop es                          ; ES = DS again, the callback default
    mov byte [trk_argp], 1
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; trk_argload - spend it, from the first paint (SPEC.md 54.5)
; in:  the gfx lock HELD, the window visible
; out: nothing; preserves all registers
;
; It hands the name to trk_fdone, the dialog's own completion proc, which
; copies it through ES:DI - so ES points at OUR segment and DI at our buffer,
; and every refusal, claim and message below it is the path a dialog open
; already takes. DX:CX = 0 is "no size", which that path already handles for
; a typed name.
; -----------------------------------------------------------------------------
trk_argload:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [trk_argp], 0          ; once, whatever happens below
    mov dx, [trk_argclus]
    mov bl, [trk_argdrv]
    call OSAPI_FILE_GOTO            ; the folder it was opened from
    jc .out
    push ds
    pop es
    mov di, trk_argnm
    xor cx, cx
    xor dx, dx                      ; no size: the typed-name path
    xor al, al
    call trk_fdone
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    cmp byte [trk_argp], 0          ; a module handed to us at launch
    je .noarg                       ; (SPEC.md 54.5): here the gfx lock is
    call trk_argload                ; held and the window is placed, which
.noarg:                             ; the entry proc could promise neither
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
                                    ; every key drives the player from the
                                    ; first keystroke - fullscreen is F or a
                                    ; click, deliberately NOT "any key",
                                    ; because Load/Play/mute all have to work
                                    ; on the splash BEFORE ever going
                                    ; fullscreen (a lesson from the field:
                                    ; any-key-enters made loading first
                                    ; impossible)
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
%ifdef TRKLOG
    cmp bl, 'd'
    je .diag
    cmp bl, 'D'
    je .diag
    cmp bl, 'w'
    je .wlog
    cmp bl, 'W'
    je .wlog
    cmp bl, 'm'
    je .mark
    cmp bl, 'M'
    je .mark
    cmp bl, 'y'
    je .synct
    cmp bl, 'Y'
    je .synct
    cmp bl, 't'
    je .clkt
    cmp bl, 'T'
    je .clkt
    cmp bl, 'e'
    je .event
    cmp bl, 'E'
    je .event
    cmp bl, 'k'
    je .xrate
    cmp bl, 'K'
    je .xrate
%endif
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
%ifdef TRKLOG
.diag:
    call tlog_key                   ; D: the log on/off (tests/trklog.inc)
    jmp .out
.wlog:
    call tlog_save                  ; W: TRKLOG.TXT on the current volume
    jmp .out
.mark:
    call tlog_mark                  ; M: the listener heard something
    jmp .out
.synct:
    call tlog_sync_key              ; Y: SPEC.md 45.15 off, to A/B it
    jmp .out
.clkt:
    call tlog_clk_key               ; T: SPEC.md 45.16 off, likewise
    jmp .out
.event:
    call tlog_even_key              ; E: the windowed readout on an EVEN
    jmp .out                        ; two-frame grid (SPEC.md 45.16.3)
.xrate:
    call tlog_rate_key              ; K: XT mode's sample rate, WITHOUT
    jmp .out                        ; leaving XT mode (FIELD-NOTES.md 16)
%endif
.play:
    mov al, 0
    call trk_play
    jmp .out
.space:
    cmp byte [mp_playing], 0
    je .play
    call trk_play_stop
    call trk_transport
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
; whole file with OSAPI_FILE_READ (ES:BX = the grant, DX:CX = its capacity in
; bytes - the read walks its destination by SEGMENT, SPEC.md 18.4.1, which is
; the only reason a 116KB module fits in one call at all), then
; mp_load validates and builds tables. Any failure frees the grant and puts
; its verdict on the status line. Success starts playback and repaints -
; which under WF_FULL also covers the menu-bar strip fdlg_close painted.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; trk_repaint_done - the SPEC.md 38.6 completion repaint, sized to what a
;                    load can actually have changed
;
; in:  gfx lock held (completion-proc context); preserves all registers
;
; Windowed, this used to be tui_draw_all: a full content fill and every line
; of the splash card re-lettered, six erase-and-letter pairs, on a machine
; where a glyph cell is about a millisecond (PERFORMANCE.md). That is a
; couple of hundred milliseconds of static text redrawn to change two lines -
; and it is paid at exactly the wrong moment, one pre-roll away from the
; worker's first refill (docs/FIELD-NOTES.md). The splash is STATIC except
; for the module title and the status line, and W_PAINT has just drawn the
; card clean underneath the destroyed dialog (SPEC.md 38.6's teardown-then-
; callback order), so those two lines are the whole debt.
;
; Fullscreen still takes the full path: that surface is being rewritten as a
; text mode elsewhere, so there is nothing here worth optimising into.
; -----------------------------------------------------------------------------
trk_repaint_done:
    cmp byte [trk_fs], 0
    jne .full
    call tui_win_lines
    ret
.full:
    call tui_draw_all
    ret

; -----------------------------------------------------------------------------
; trk_is_mod - does [trk_fname] end in .MOD? (SPEC.md 38.6)
; out: CF = 0 yes, CF = 1 no. All registers preserved.
;
; The mount's display names are upper-case 8.3 (SPEC.md 19), so this needs no
; case folding. It is a NAME test and not a content one on purpose: the point
; is to refuse before the file is read, and mp_load still has the last word on
; anything that gets past it - a .MOD that is not one is still caught, just
; not for free.
; -----------------------------------------------------------------------------
trk_is_mod:
    push ax
    push si
    push di
    xor di, di                      ; DI = the char after the last dot, 0 none
    mov si, trk_fname
.find:
    lodsb
    or al, al
    jz .end
    cmp al, '.'
    jne .find
    mov di, si                      ; SI already points past the dot
    jmp short .find
.end:
    mov si, di
    or si, si
    jz .no                          ; no dot at all
    cmp byte [si], 'M'
    jne .no
    cmp byte [si+1], 'O'
    jne .no
    cmp byte [si+2], 'D'
    jne .no
    cmp byte [si+3], 0
    jne .no
    pop di
    pop si
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop ax
    stc
    ret

trk_fdone:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [trk_fsize], cx             ; DX:CX = the file's size (SPEC.md 38.6),
    mov [trk_fsize+2], dx           ; banked FIRST: the name copy below uses
                                    ; CX as its counter and would eat it
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

    ; --- refuse what we cannot load, BEFORE any disk I/O (SPEC.md 38.6) ------
    ; Both tests below used to happen after the whole file had been read: the
    ; extension one as mp_load's verdict, the size one as dskw_read's
    ; FERR_BIG. Reading 116KB off a 360KB floppy to be told "not a MOD" is
    ; ten seconds of motor on the target machine, and the user cannot tell a
    ; failing load from a working one until it ends. The dialog now hands us
    ; the name AND the size, so both answers are free.
    call trk_is_mod
    jc .notmod
    mov ax, [trk_fsize]             ; DX:AX = the size, and it is genuinely
    mov dx, [trk_fsize+2]           ; 32-bit: a 116KB MOD has a high word of
    cmp dx, 2                       ; 1, which an "any high word is too big"
    jae .toobig2                    ; test rejects. >= 128KB is the real cap
    mov bx, ax
    or bx, dx
    jz .sizeok                      ; 0 = the dialog had no size for it (a
                                    ; typed name): fall through and let the
                                    ; file API answer as it always did
    add ax, 1023                    ; bytes -> KB, rounded up, across 32 bits
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                       ; AX = KB needed
    mov [trk_needk], ax
    cmp ax, 128                     ; the same cap the claim used to apply
    ja .toobig2
    push ax
    call OSAPI_MEM_AVAIL            ; AX = LARGEST contiguous run in KB
    pop bx
    cmp ax, bx
    jb .nomem2                      ; not fundable: say so and touch nothing
.sizeok:

    call trk_play_stop              ; silence + close + DRAIN before the blob
                                    ; moves: trk_stream_close spins out the
                                    ; worker's in-flight feed pass, so no
                                    ; mp_mixch is mid-fetch from the old
                                    ; grant past this line ([trk_mixing])
    mov byte [mp_loaded], 0         ; the old blob is about to be freed: no
                                    ; reader may trust it past this line
    cmp word [trk_modseg], 0
    je .alloc
    mov dx, [trk_modseg]            ; DX, not AX (SPEC.md 50.3)
    call OSAPI_MEM_FREE
    mov word [trk_modseg], 0
.alloc:
    mov ax, [trk_needk]             ; the size the DIALOG reported, rounded to
    or ax, ax                       ; KB: claim what the file needs and not
    jnz .sized                      ; the whole largest run (SPEC.md 38.6).
                                    ; 0 = no size was available (a typed
                                    ; name), so fall back to the old
                                    ; largest-run-capped-at-128KB shape
    call OSAPI_MEM_AVAIL            ; AX = LARGEST contiguous run in KB, and
    or ax, ax                       ; BX = the total (KB, not
    jz .nomem                       ; paragraphs - SPEC.md 50.3)
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
    xor bx, bx                      ; ES:BX = the grant, at its first byte
    mov si, trk_fname
    call OSAPI_FILE_READ            ; out CF=0, DX:AX = bytes read
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
    call trk_repaint_done           ; the mandatory completion repaint - two
    jmp .out                        ; LINES when windowed, not the whole card

.nomem:
    mov si, trk_s_nomem
    jmp .fail
.notmod:                            ; the three early refusals: nothing was
    mov si, trk_s_notmod            ; stopped, nothing was freed and the disk
    jmp .fail                       ; was never touched, so whatever is
.toobig2:                           ; playing keeps playing
    mov si, trk_s_toobig
    jmp .fail
.nomem2:
    mov si, trk_s_nofit
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
    mov dx, [trk_modseg]            ; DX, not AX (SPEC.md 50.3)
    or dx, dx
    jz .npop
    call OSAPI_MEM_FREE
    mov word [trk_modseg], 0
.npop:
    pop si
.fail:
    call tui_msg
    call trk_repaint_done
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; =============================================================================
; Fullscreen is the fsx exclusive surface (SPEC.md 53) - entered from
; W_ONKEY / W_ONCLICK / AM_ONCMD, all of which hold the gfx lock, which
; OSAPI_FSX_RUN requires. Tracker is the FSXF_KEEPWORKER reference consumer:
; the machine is ours, every other task frozen, but our audio-feeding worker
; keeps running so the Sound Blaster stream never underruns while we own the
; screen (SPEC.md 53.2). We do NOT switch video mode - the FT2 screen wants
; the desktop's resolution, so this is "exclusive but same mode" (SPEC.md
; 53.7) and the drawing slots stay legal throughout.
;
; [trk_fs] is set BEFORE OSAPI_FSX_RUN, not inside trk_fsx_main: a tick can
; preempt task 0 between the freeze arming and our main, and the worker must
; already see [trk_fs]=1 at its render check (above) or it enters trk_render,
; parks on our soon-held lock and starves the ring for the whole session.
; =============================================================================
trk_fs_enter:
    push ax
    push bx
    push cx
    cmp byte [trk_fs], 0
    jne .out                        ; the bracket blocks, so this is belt-only
    mov byte [trk_fs], 1            ; BEFORE the call - see the header

                                    ; ...and then DRAIN THE WORKER OUT OF
                                    ; trk_render, because the flag alone only
                                    ; closes the door for a worker that has not
                                    ; yet reached its test. One already INSIDE
                                    ; trk_render is parked in gfx_lock behind
                                    ; the lock this callback holds, and the
                                    ; bracket below holds it for the whole
                                    ; session - so it never reaches trk_feed,
                                    ; the ring drains its cushion and the music
                                    ; stops for as long as the user stays
                                    ; fullscreen. The menu makes it
                                    ; near-deterministic: a pull-down holds the
                                    ; lock for ~18 worker wakes and ui_dispatch
                                    ; retakes it a few instructions later.
                                    ;
                                    ; BEFORE the arguments below, not after:
                                    ; the unlock/yield/lock trio is three far
                                    ; calls and AX, BX and CX are all live once
                                    ; they are loaded.
    cmp byte [trk_inrend], 0
    je .run
    mov cx, 8                       ; BOUNDED: a worker wedged for some other
.drain:                             ; reason must not cost the keypress the
    call OSAPI_GFX_UNLOCK           ; machine
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    cmp byte [trk_inrend], 0
    je .run
    loop .drain
.run:
    mov ax, trk_fsx_main
    mov bx, [trk_win]
    mov cx, FSXF_KEEPWORKER | FSXF_FASTTICK
                                    ; the worker feeds through the freeze, and
                                    ; the quantum goes to 18 ms so its slot
                                    ; costs the scroll a third of a frame
                                    ; rather than a whole one (SPEC.md 53.2.1)
    call OSAPI_FSX_RUN              ; blocks until trk_fsx_main returns; the
                                    ; kernel then repaints the desktop whole
    mov byte [trk_fs], 0            ; back to the windowed splash
    cmp byte [trk_txbb], 0          ; ...and the user's own back-buffer setting
    je .out                         ; if the text screen (SPEC.md 45.13) had to
    mov byte [trk_txbb], 0          ; turn it off to get a foreign mode set.
    mov al, 1                       ; AFTER the run, not inside it: the desktop
    call OSAPI_GFX_DBUF             ; mode and its pixels are both back, so
                                    ; bb_set's seed-from-VRAM reads the screen
                                    ; the user is actually looking at
.out:
    pop cx
    pop bx
    pop ax
    ret

; trk_fs_exit - windowed callers still name this (the .esc / Fullscreen-toggle
; branches), but the bracket now owns the exit: Esc is POLLED inside
; trk_fsx_main (the event ladder is parked, SPEC.md 53.1), which returns and
; unwinds trk_fs_enter. So while [trk_fs]=1 the ladder cannot run to call
; this, and once it is 0 there is nothing to do - a clean guard either way.
trk_fs_exit:
    ret

; -----------------------------------------------------------------------------
; trk_fsx_main - the exclusive main (SPEC.md 53.1): SI = our window, ES =
;                KERNEL_SEG, DS = CS = ours, gfx lock held for the whole
;                session, every task but us and our feeder frozen.
;                Returns (near) on Esc; the kernel restores the desktop.
;
; The worker's drawing moved HERE: the bracket is the one drawer (lock held
; throughout), input is polled (no events are dispatched in a bracket), the
; Smooth back buffer is presented through OSAPI_FSX_WAIT - the bracket never
; unlocks, so that flush is the only one a buffered frame gets (SPEC.md
; 53.5/45.11). The worker keeps feeding audio and nothing else.
; -----------------------------------------------------------------------------
trk_fsx_main:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [mp_xt], 0             ; XT mode's fullscreen is an 80x25 TEXT
    je .gfx                         ; screen (SPEC.md 45.13) - see trktxt.inc
    xor al, al                      ; for the arithmetic. fsx_mode refuses
    call OSAPI_GFX_DBUF             ; while a back buffer is armed (it
    jc .txmode                      ; describes DESKTOP geometry), so park the
    or al, al                       ; user's setting first and note the debt;
    jz .txmode                      ; trk_fs_enter pays it after the restore
    mov byte [trk_txbb], 1
.txmode:
    call ttx_begin                  ; CF=1: refused, and nothing was changed -
    jnc .txok                       ; fsx_mode either sets the mode or touches
    cmp byte [trk_txbb], 0          ; nothing at all. Put the buffer back
    je .gfx                         ; before the graphics bracket takes over,
    mov byte [trk_txbb], 0          ; or Smooth's own banking and ours would
    mov al, 1                       ; both be holding the user's setting
    call OSAPI_GFX_DBUF
    jmp short .gfx
.txok:
    mov byte [trk_tx], 1
    call ttx_draw_all
    call ttx_clkpick                ; the frame clock (SPEC.md 45.16/53.5.1)
.txloop:
    call trk_reap                   ; F00 / watchdog stream cleanup, UI ctx
    mov ah, 1
    int 0x16                        ; this IS the UI task (SPEC.md 53.1)
    jz .txdraw
    xor ah, ah
    int 0x16
    cmp al, 27
    je .txdone
    call trk_fsx_key
.txdraw:
    call ttx_draw_dyn
    mov al, [ttx_clk]               ; retrace-paced where the adapter can be
%ifdef TRKLOG
    call tlog_wtstart               ; the loop's OTHER half, timed: FX covers
%endif                              ; the frame and DX the feed pass, and a
    call OSAPI_FSX_WAIT             ; (SPEC.md 45.16), one frame per tick where
%ifdef TRKLOG                       ; field gap had both healthy - so the time
    call tlog_wtend                 ; went here or nowhere either task can see
%endif
    jmp .txloop                     ; it cannot. The SPEC.md 53.5 present
                                    ; clause is dead either way by
                                    ; construction: fsx_mode refused a buffer
.txdone:
    mov byte [trk_tx], 0            ; before [trk_fs], so a message set on the
    mov byte [trk_fs], 0            ; way out reaches the windowed splash
    jmp .out
.gfx:
    cmp byte [trk_smooth], 0        ; Smooth (SPEC.md 45.11): arm the 32 back
    je .drawall                     ; buffer, and OSAPI_FSX_WAIT presents it
    cmp byte [trk_bbheld], 0        ; (already borrowed if smooth was toggled
    jne .drawall                    ; on before entry - it never is, but the
    mov al, 1                       ; guard keeps this and trk_smooth_toggle
    call OSAPI_GFX_DBUF             ; agreeing on one [trk_bbheld])
    jc .drawall                     ; mono / no RAM: draw straight to VRAM
    mov [trk_bbprev], al
    mov byte [trk_bbheld], 1
.drawall:
    call tui_draw_all               ; the whole FT2 screen, into bb or VRAM
.loop:
    call trk_reap                   ; F00 / watchdog stream cleanup, UI ctx
    mov ah, 1                       ; poll the keyboard - this IS the UI task
    int 0x16                        ; (SPEC.md 53.1), so int 16h is legal
    jz .draw
    xor ah, ah
    int 0x16
    cmp al, 27                      ; Esc leaves, exactly as it left 11.2
    je .done
    call trk_fsx_key
.draw:
    call tui_draw_dyn               ; the animated frame (no clip: we own the
                                    ; screen, nothing is on top)
    xor al, al                      ; one frame per tick - the worker's pace,
    call OSAPI_FSX_WAIT             ; and the back-buffer present with it
    jmp .loop
.done:
    mov byte [trk_fs], 0            ; BEFORE the return: fsx_restore repaints
                                    ; the desktop under the still-held lock
                                    ; (SPEC.md 53.6), and that repaint calls
                                    ; OUR W_PAINT - with [trk_fs] still 1 it
                                    ; would redraw the fullscreen FT2 frame
                                    ; over the desktop and nothing would come
                                    ; back. Clearing it here makes the paint
                                    ; windowed. (The worker may park one frame
                                    ; on our held lock now, harmless - we are
                                    ; leaving; trk_fs_enter's clear covers the
                                    ; fsx_run-refused path where we never ran)
    cmp byte [trk_bbheld], 0        ; hand the buffer back BEFORE the return,
    je .out                         ; so the kernel's desktop repaint takes
    mov al, [trk_bbprev]            ; the mode the user actually chose
    call OSAPI_GFX_DBUF
    mov byte [trk_bbheld], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_fsx_key - the bracket's keyboard: the fullscreen-relevant subset of
; trk_onkey, called under the held lock. Load is absent on purpose - the
; Standard File dialog is unreachable in a bracket (its completion comes
; through the parked event ladder, SPEC.md 53.7), and Tracker's Open lives in
; the windowed splash. in: AL = ascii, AH = scan.
; -----------------------------------------------------------------------------
trk_fsx_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    or al, al                       ; the keypad trap again: arrows arrive
    jnz .ascii                      ; ascii 0 with a scan code
    cmp ah, 0x4B
    je .prev
    cmp ah, 0x4D
    je .next
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    jmp .out
.ascii:
    cmp al, 13                      ; Enter: play the song
    je .play
    cmp al, ' '                     ; Space: stop / play toggle
    je .space
    cmp al, 'p'
    je .pat
    cmp al, 'P'
    je .pat
    cmp al, 'l'                     ; Load cannot happen here - the SPEC.md 38
    je .load                        ; dialog's answer comes through the parked
    cmp al, 'L'                     ; event ladder (SPEC.md 53.7) - so the key
    je .load                        ; says WHY not (SPEC.md 47), not nothing
    cmp al, 'x'
    je .xt
    cmp al, 'X'
    je .xt
    cmp al, 'r'
    je .rcyc
    cmp al, 'R'
    je .rcyc
    cmp al, 's'
    je .smooth
    cmp al, 'S'
    je .smooth
%ifdef TRKLOG
    cmp al, 'd'
    je .diag
    cmp al, 'D'
    je .diag
    cmp al, 'w'
    je .wref
    cmp al, 'W'
    je .wref
    cmp al, 'm'
    je .mark
    cmp al, 'M'
    je .mark
%endif
    cmp al, '1'
    jb .out
    cmp al, '4'
    ja .out
    sub al, '1'                     ; 1..4: channel mute toggle
    call mp_mutetog
    jmp .out
%ifdef TRKLOG
.diag:
    call tlog_key
    jmp .out
.mark:
    call tlog_mark                  ; M: stamp the tick the LISTENER heard
    jmp .out                        ; something - the one input here that is
.wref:                              ; not a measurement
    push si                         ; W is windowed-only for the same reason
    mov si, tlog_s_fsw              ; L is: the file slots are UI-callback-only
    call tui_msg                    ; and a bracket owns the machine until it
    pop si                          ; returns (SPEC.md 53.7). Say why, do not
    jmp .out                        ; do nothing (SPEC.md 47)
%endif
.load:
    push si
    mov si, trk_s_fsload
    call tui_msg
    pop si
    jmp .out
.prev:
    mov al, -1
    call mp_setpos
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
.play:
    mov al, 0
    call trk_play
    jmp .out
.space:
    cmp byte [mp_playing], 0
    je .play
    call trk_play_stop
    call trk_transport
    jmp .out
.pat:
    mov al, 1
    call trk_play
    jmp .out
.xt:
    cmp byte [trk_tx], 0            ; the XT text screen (SPEC.md 45.13) IS
    jne .txxt                       ; what XT mode's fullscreen is, so turning
    call trk_xt_toggle              ; the mode off from inside it would leave
    jmp .out                        ; the app on a surface it no longer wants.
                                    ; Say why not (SPEC.md 47), like L does
.rcyc:
    mov al, [trk_rsel]              ; R cycles 11 -> 22 -> 44 -> 11
    inc al
    cmp al, 3
    jb .rset
    xor al, al
.rset:
    call trk_rate_set
    jmp .out
.smooth:
    cmp byte [trk_tx], 0            ; Smooth is the SPEC.md 32 back buffer,
    jne .txsm                       ; which describes the DESKTOP's geometry -
    call trk_smooth_toggle          ; there is nothing to double in a text mode
    jmp .out                        ; and fsx_mode refused it on the way in.
                                    ; Graphics: it arms / disarms live, and
                                    ; OSAPI_FSX_WAIT's present follows [bb_dbl]
.txxt:
    mov si, trk_s_txxt
    call tui_msg
    jmp short .out
.txsm:
    mov si, trk_s_txsm
    call tui_msg
.out:
    pop di
    pop si
    pop dx
    pop cx
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
    cmp byte [trk_ghave], 0         ; the 16KB ring grant, taken per PLAY and
    jne .granted                    ; given back by trk_stream_close so the
                                    ; driver can release its staging pool
                                    ; while we are stopped (SPEC.md 34.6);
                                    ; teardown still force-frees it (34.3) if
                                    ; a close never ran
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
%ifdef TRKLOG
    mov ax, [tlog_xrate]            ; ...which K can move WITHOUT leaving XT
%else                               ; mode, so the rate can be swept with the
    mov ax, TRK_RATE_XT             ; surface held still (docs/FIELD-NOTES.md
%endif                              ; 16). Bench-only; the shipped build is
    jmp .rate                       ; the constant it always was
.rsel:
    mov bl, [trk_rsel]              ; the Rate menu's pick: 0/1/2
    xor bh, bh
    shl bx, 1
    mov ax, [trk_rates + bx]
.rate:
    mov [mp_mixrate], ax
    mov al, [trk_pmode]
    call mp_start
%ifdef TRKLOG
    call tlog_stream                ; a new stream: the log's clocks restart
%endif
    mov word [trk_total], 0
    mov word [trk_consumed], 0      ; the stream's byte counters both restart
    mov word [mp_stampbase], 0      ; here, so the stamp history does too -
    call mp_stclear                 ; seeded with row 0, which mp_start has
                                    ; already read but nothing has mixed yet
    mov word [tui_lcons], 0         ; ...and so does tui_playpos's estimate
    mov word [tui_play], 0          ; (SPEC.md 45.15.1), which is anchored on
    mov word [tui_pcon], 0          ; those same counters - the phase loop's
                                    ; last-measured report included, or the
                                    ; first edge of the new stream is compared
                                    ; against the old one's (SPEC.md 45.15.3)
    call OSAPI_GET_TICKS
    mov [tui_ct0], ax
    mov ax, [mp_mixrate]            ; bytes per system tick: rate / 18.2065,
    mov dx, 3600                    ; and 3600/65536 is that to 0.011% - so
    mul dx                          ; the product's HIGH word is the answer
    mov [tui_bpt], dx               ; and no division is needed at all
    mov [tui_bpf], dx               ; the sub-tick divider starts at one frame
    mov word [tui_sub], 0           ; a tick (SPEC.md 45.15.2), which IS the
    mov byte [tui_fpt], 1           ; old per-tick staircase - the first tick
    mov byte [tui_fcnt], 0          ; measured replaces it
    mov cx, TRK_PREROLL             ; stage the cushion before the open, so
.pre:                               ; the stream starts TRK_PREROLL halves
    call trk_mix_stage              ; ahead of the DSP instead of two
    loop .pre                       ; (trk_mix_stage preserves everything)
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
    call trk_transport              ; the one success path; every exit below
    jmp .out                        ; is a refusal with its own message
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
    mov ax, [trk_total]             ; where mp_outbuf[0] lands in the stream:
    mov [mp_stampbase], ax          ; the replayer stamps each row against it
    mov cx, TRK_HALF                ; (SPEC.md 45.15)
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

    ; ...and give the 16KB ring grant back, which is what lets the DRIVER
    ; release its 20KB staging pool (SPEC.md 34.6): the pool is a LATE heap
    ; claim - taken on the first grant, after this app already holds its
    ; module buffer - so a grant held across a whole session parks 20KB in
    ; the MIDDLE of the heap and splits it (SPEC.md 50.3's warning, and the
    ; refusal docs/FIELD-NOTES.md records). The grant used to be allocated
    ; once and left to teardown; stopped is exactly when nobody needs it.
    ;
    ; This is the sanctioned free point, not merely a convenient one. The
    ; SDK's one author rule is "never verb-7-free a grant your worker may be
    ; mid-stage into - only after your own handshake says it is parked", and
    ; the drain above IS that handshake; the stream is closed first, so the
    ; driver's own live-stream-overlap refusal cannot fire either.
    cmp byte [trk_ghave], 0
    je .nogr
    push si
    mov si, [trk_grant]             ; SI = the grant's offset (sbl_grant_find)
    mov al, 7
    mov ah, 1                       ; sub-op 1 = free
    call OSAPI_SND_STREAM
    pop si
    or ax, ax
    jnz .nogr                       ; refused: keep the latch, so the next
    mov byte [trk_ghave], 0         ; play reuses the grant we still hold
.nogr:
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
    call tui_sync                   ; where the LISTENER is, asked while the
                                    ; stream can still answer (SPEC.md 45.15)
    call trk_stream_close
    call mp_stop
    mov al, [tui_apos]              ; ...and park the replayer there rather
    mov [mp_songpos], al            ; than where the mixer got to, which is
    mov al, [tui_apat]              ; up to three seconds of music further on.
    mov [mp_pattern], al            ; trk_stream_close has drained the worker,
    mov al, [tui_arow]              ; so nothing is inside mp_gen; trk_transport
    mov [mp_row], al                ; then parks the view row on top of this
    ret

; -----------------------------------------------------------------------------
; trk_transport - the status legend and the stopped view row follow the
;                 transport, from wherever it changed
; in:  [mp_playing]; gfx lock held (every caller is a UI callback or the
;      worker's own locked frame)
; out: nothing (all registers preserved)
;
; TWO things were missing and both were the same omission: nothing said what
; the transport was doing. trk_play set no message at all on success, so the
; line kept whatever the load had put there; 'Stopped' was set once and stuck,
; with no key legend after it; and because [tui_msgp] is non-zero from the
; first load onward, tui_s_hint - the only thing that ever named the keys -
; never came back. So the line now carries the state AND the keys for it, in
; both directions.
;
; Stopping also parks the view where the music GOT TO. tui_viewrow reads
; [tui_vrow] while stopped, and that was the Up/Down scroll row, untouched
; since before playback started - so stopping jumped the pattern back to
; where you pressed play. mp_stop leaves [mp_row] alone, which is what makes
; this a copy rather than a snapshot taken earlier.
; -----------------------------------------------------------------------------
trk_transport:
    push ax
    push si
    cmp byte [mp_playing], 0
    jne .playing
    mov al, [mp_row]
    mov [tui_vrow], al
    mov si, trk_s_stopd
    jmp short .msg
.playing:
    mov si, trk_s_playing
.msg:
    call tui_msg
    pop si
    pop ax
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
;
; WHICH COMES FIRST IS THE WINDOWED FRAME'S PACING (SPEC.md 45.16.2). This
; worker both mixes and draws when windowed, so a feed pass that mixes a
; 2,048-byte half - about three ticks at XT mode's rate - is three frames not
; drawn, and the row change that was due in them arrives one to three ticks
; late. Measured against CLICK.MOD's 125 ms row: the worker is inside
; trk_feed on 13% of samples but on 82% of the row gaps that ran to 200 ms.
;
; So when the ring is DEEP the frame goes first and lands on the tick edge,
; and the mix happens behind it. "Audio first" is not being traded away - it
; is being applied where it means something: trk_deep gates on the lead, so
; the moment the ring is anything less than half full this is exactly the
; loop it always was. What makes that safe is that the windowed ring measures
; 7 to 8 halves of 8, and a windowed frame is a partial redraw of a few
; readouts, so the mixer loses tens of milliseconds out of a 2.6-second
; cushion.
; =============================================================================
TRK_DEEP    equ 4 * TRK_HALF        ; half the ring: draw first above this

trk_worker:
.loop:
%ifdef TRKLOG
    call tlog_wake                  ; WK: how often this got the CPU, which is
%endif                              ; what tells a STARVED worker from an idle
                                    ; one - FD reads 0 for both
    mov bx, [trk_win]
    call OSAPI_TASK_ALIVE           ; lock NOT held here (rule 4)
    mov ax, 1
    call OSAPI_TASK_SLEEP           ; ~18 wakes a second
    mov byte [trk_drew], 0
    cmp byte [trk_fs], 0            ; on the fsx surface (SPEC.md 53.2) the
    jne .feed                       ; BRACKET draws and this worker is the
                                    ; kept feeder - it must NOT take the gfx
                                    ; lock, or it parks on the bracket's hold
                                    ; and the ring starves. It keeps feeding.
    call trk_deep
    jnc .feed                       ; ring tight: audio first, as it always was
    mov byte [trk_inrend], 1        ; ...and say so, so the fsx bracket can
    call trk_render                 ; wait for us rather than lock us out
    mov byte [trk_inrend], 0
    mov byte [trk_drew], 1
.feed:
    call trk_feed
    cmp byte [trk_fs], 0            ; re-tested: F can be pressed inside the
    jne .loop                       ; frame we just drew
    cmp byte [trk_drew], 0
    jne .loop
    mov byte [trk_inrend], 1
    call trk_render                 ; ...and the tight-ring path still draws,
    mov byte [trk_inrend], 0
    jmp .loop                       ; just behind the mix ([trk_abon] drops
                                    ; the frame inside trk_render, under the
                                    ; lock)

; -----------------------------------------------------------------------------
; trk_deep - CF = 1 when the ring has enough in it that a frame may go first
;            (SPEC.md 45.16.2). Preserves every register.
;
; The lead is total - consumed, both free-running, so the subtraction is exact
; across the wrap. [trk_consumed] is at most one wake old and only ever grows,
; so a stale read UNDER-states the lead by a tick's worth - 302 bytes against
; an 8,192-byte threshold - which errs towards feeding first.
;
; A stream that is not open has no ring to starve, so drawing first is free.
; -----------------------------------------------------------------------------
trk_deep:
    push ax
    cmp byte [trk_sopen], 0
    je .yes
    mov ax, [trk_total]
    sub ax, [trk_consumed]
    cmp ax, TRK_DEEP
    cmc                             ; CF = 1 when the lead is AT or above it
    pop ax                          ; (pop leaves the flags alone)
    ret
.yes:
    pop ax
    stc
    ret

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
%ifdef TRKLOG
    call tlog_wstart                ; this pass's span starts here, and ENDS
%endif                              ; at .out - a pass that mixes TRK_MAXFEED
                                    ; halves is the thing DX exists to price
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
    mov [trk_consumed], dx          ; publish it for the display (SPEC.md
                                    ; 45.15): the worker already asks once a
                                    ; tick, so nothing else has to ask at all
    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
%ifdef TRKLOG
    call tlog_feed                  ; AX = stream state, DX = consumed - the
%endif                              ; whole per-tick record but the drawing
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
%ifdef TRKLOG
    call tlog_wend                  ; ...and the span closes here
%endif
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
trk_s_stopd:  db 'Stopped  ENTER play  L load', 0
trk_s_playing: db 'Playing  SPACE stop  L load', 0
trk_s_fsload: db 'Load is windowed: Esc first', 0
trk_s_notmod: db 'Not a .MOD file', 0
trk_s_nofit:  db 'Too big for free memory', 0
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
trk_s_txxt:   db 'XT off is windowed: Esc first', 0
trk_s_txsm:   db 'Smooth is a graphics mode only', 0

; =============================================================================
; The rest of the package: the replayer, and the two renderers of one screen -
; trkui.inc in pixels, trktxt.inc in character cells (SPEC.md 45.13, which is
; what XT mode's fullscreen is)
; =============================================================================
%include "trkplay.inc"
%include "trkui.inc"
%include "trktxt.inc"
%ifdef TRKLOG
%include "trklog.inc"               ; tests/ - the bench build only, and the
%endif                              ; only thing -DTRKLOG adds beyond hooks

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes TRK_BSS bytes after the image; every
; name is an equ offset from os88_image_end via the TRKB/TRKW/TRKBUF macros
; defined at the top of this file)
; =============================================================================

    TRKW trk_win                    ; our window ptr (opaque handle)
    TRKB trk_fs                     ; 1 = fullscreen active (read by trkui)
    TRKB trk_drew                   ; this wake already drew, before the mix
    TRKB trk_inrend                 ; the worker is INSIDE trk_render, which
                                    ; means it is either holding the gfx lock
                                    ; or parked waiting for it. Setting
                                    ; [trk_fs] cannot recall a worker that is
                                    ; already past its own test, so the bracket
                                    ; drains this before it takes the machine
                                    ; (SPEC.md 45.16.2) - so the tail does not
                                    ; draw the same frame twice
    TRKB trk_tx                     ; 1 = that fullscreen is the XT TEXT screen
                                    ; (SPEC.md 45.13) and the adapter is in a
                                    ; foreign mode: every kernel drawing slot
                                    ; is off-limits until the bracket returns
                                    ; (SPEC.md 53.1). Implies [trk_fs]
    TRKB trk_txbb                   ; ...and we turned the user's back buffer
                                    ; off to get that mode set, so we owe them
                                    ; one re-arm after the restore
    TRKB trk_hired                  ; the worker exists
    TRKB trk_abon                   ; the About panel is up; worker frames drop
    TRKB trk_pmode                  ; 0 = song, 1 = pattern loop
    TRKB trk_cpu0                   ; the MACHINE is a tier-0 8086/8088
                                    ; (SPEC.md 41.8), latched at entry. NOT
                                    ; [mp_xt], which is a user-toggleable
                                    ; playback mode and true on a 386 the
                                    ; moment someone presses X - this is the
                                    ; one that decides whether the pattern
                                    ; view can be animated at all (45.9.1)

; --- the module blob (a heap claim, SPEC.md 50) -------------------------------
    TRKW trk_modseg                 ; grant base segment, 0 = none
    TRKW trk_fsize                  ; the chosen file's size, from the dialog
    TRKW trk_fsize_hi               ; (SPEC.md 38.6); 0 = it had none
    TRKW trk_needk                  ; ...as KB, rounded up; 0 = unknown
    TRKW trk_capk                   ; its size in KB
    TRKBUF trk_argnm, 13            ; a module handed to us at launch (54.5)
    TRKBUF trk_argp, 1              ; ...1 = the first paint owes the load
    TRKBUF trk_argdrv, 1            ; ...and where it lives
    TRKBUF trk_argclus, 2
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
    TRKW trk_consumed               ; ...and what the CARD has played of it,
                                    ; polled by the worker once a tick and
                                    ; read by every frame (SPEC.md 45.15)
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
