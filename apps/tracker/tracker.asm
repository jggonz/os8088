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
; Six files, one package (SPEC.md 45):
;   tracker.asm  - header, icon, entry, callbacks, worker, stream plumbing
;   trkplay.inc  - MOD loader/validator, replayer, mixer (prefix mp_)
;   trkui.inc    - adapter-parameterized FT2 layout + all drawing (tui_)
;   trktxt.inc   - the same FT2 screen in 80x25 text (ttx_, SPEC.md 45.13)
;   trkwin.inc   - the WINDOWED face, ModPlug Player's (tw_, SPEC.md 45.21)
;   trklist.inc  - the playlist and its editor window (tpl_, SPEC.md 45.22)
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

    OS88_HEADER 'TRACKER', trk_entry, 3, OS88_STACK_256
                                ; THE WORKER'S STACK, declared
                                ; rather than defaulted (SPEC.md 8.7):
                                ; static 118 since the windowed
                                ; face (45.21) - its frame reaches
                                ; the button library's body - so
                                ; with the 64-byte interrupt floor
                                ; that is 182, and 256 gives 1.41x
                                ; where 192 gave 1.05

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

; --- the volume table's rows: 64 >> TRK_VSH (SPEC.md 45.4.1) -----------------
%ifndef TRK_VSH                     ; 2 SHIPS: 16 rows plus silence = 17
%define TRK_VSH 2                   ; levels, 4,096 bytes. 1 is 33 levels
%endif                              ; (8,192) and 0 is 65 (16,384), the old
                                    ; table. `make trkvol` builds those two,
                                    ; titled to say which, beside the default

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
TRK_VITEM   equ 24                  ; a composed View item: MENU_DIS +
                                    ; 'Fullscreen' + ' (5.5 kHz)' + NUL = 22
TRK_TITEM   equ 20                  ; ...and the View menu's SECOND composed
                                    ; item (SPEC.md 45.13.7): MENU_DIS + '* '
                                    ; + 'Text Screen' + ' (Xt)' + NUL = 20
TRK_RITEM_SH equ 5                  ; ...and the item stride is ONE constant,
TRK_RITEM   equ 1 << TRK_RITEM_SH   ; which the item walks add, rather than
                                    ; a second opinion about it. It was
                                    ; two constants and they disagreed: 24
                                    ; bytes declared, `shl di,1` three times =
                                    ; EIGHT written, so item 0 ran into item 1
                                    ; and the menu read `* 5.5 kH  11 kHz`.
                                    ; A composed Rate item is '* ' + '11 kHz'
                                    ; + ' (Windowed)' + NUL = 20 (SPEC.md
                                    ; 45.9.3). No MENU_DIS: the Rate menu
                                    ; lists only rows this mode can pick
; --- the ring is CHOSEN, not fixed (SPEC.md 45.18) ---------------------------
; It was a flat 16,384 and that is still what a machine with room gets. What
; it cost on a small one was the worst shape available: the module loaded,
; its title went up, and Play then said "Out of memory" because the grant
; would not fit what the module had left. So the size is picked from the free
; RAM that will remain AFTER the module is claimed, and the two numbers below
; are the whole policy.
TRK_RING    equ 16384               ; the FULL ring: 8 halves, and the only
                                    ; one that can hold TRK_PRE_FULL. Still a
                                    ; power of two in 4096..32768, which the
                                    ; ring-mode contract requires
TRK_RING_SM equ 8192                ; the SMALL ring: 4 halves. Not a
                                    ; compromise about steady state - the lead
                                    ; never drains below its top-up target on
                                    ; either (PERFORMANCE.md Set 21) - it is a
                                    ; compromise about the PRE-ROLL, which is
                                    ; what the ring has to be big enough to
                                    ; hold
; Both sizes are whole KB, so the driver's pool - sized to the first grant
; since SPEC.md 34.6.3 - is exactly the ring and not a byte more, and both are
; whole multiples of the driver's widest block, so the card can play the ring
; where it lies (SPEC.md 34.5.2) at every rate this app opens.
%if (TRK_RING % 4096) || (TRK_RING_SM % 4096)
  %error "a ring size must be whole 4KB blocks (SPEC.md 34.5.2)"
%endif
TRK_RING_HZ equ 8000                ; ...and at or below this RATE the small
                                    ; ring is taken on EVERY machine (SPEC.md
                                    ; 45.18.2). XT mode's 5.5 kHz mixes in
                                    ; pieces to the tick edge and never fills
                                    ; either ring - measured, the lead sits at
                                    ; 2-4KB on both - so the full one was 8KB
                                    ; of heap held for nothing. From 11 kHz up
                                    ; the ring IS filled and the full one is
                                    ; the cushion: at 11 kHz on an XT the small
                                    ; ring holds, but with one half to spare
TRK_ROOMYKB equ 64                  ; ...and the line between them. Above this
                                    ; much free RAM left over we take the full
                                    ; ring and the full cushion; at or below
                                    ; it we take the small one and ACCEPT the
                                    ; pre-roll hitch, because on a machine
                                    ; that tight the alternative is not a
                                    ; better cushion, it is no playback at all
TRK_HALF    equ 2048                ; the stream's fill unit, pinned (SPEC.md
                                    ; 34.5): fills are whole halves only
TRK_RATE    equ 11000               ; open rate request, Hz (kernel quantizes
                                    ; via the TC; mp_gen mixes to the same)
TRK_RATE_XT equ 5500                ; XT mode's rate (SPEC.md 45.9): halves
                                    ; the mixer's per-second sample budget
TRK_RATE_XT2 equ 11000              ; ...and XT mode's HIGH rate (SPEC.md
                                    ; 45.9.3), which is WINDOWED-ONLY and is
                                    ; not the default. PERFORMANCE.md Set 65
                                    ; measured both: windowed it delivers
                                    ; 100.2% of the music over 4.7 minutes,
                                    ; and on the 45.13 text screen it delivers
                                    ; 88.4% - a sixth of the song silently
                                    ; missing, which is not one of 45.8's
                                    ; honest degradations. So the surface is
                                    ; part of the CHOICE and trk_fs_enter
                                    ; refuses rather than quietly reverting
TRK_RATE22  equ 22050               ; Rate menu (SPEC.md 45.10): still the
                                    ; classic TC regime, any DSP
TRK_RATE33  equ 33075               ; ...and the one between (SPEC.md 45.10.1):
                                    ; 3/4 of 44.1, the rate a 286 can MIX -
                                    ; 91% of a 2 MIPS machine where 44.1 kHz
                                    ; cannot keep up at all. SB16: exact via
                                    ; 41h; SB Pro: TC 226 = 33,333 Hz
TRK_RATE44  equ 44100               ; the 34.5 wide-rate regime - DSP >= 4
                                    ; only; an older card refuses err 2
; The small ring's pre-roll is DERIVED by trk_ring_set - every half it has bar
; the one the feed ceiling keeps free - and comes out at THREE, which is 1.12 s
; at the XT rate against the 744 ms that produced the field hitch TRK_PREROLL
; was raised to fix. So a tight machine keeps 1.5x a figure already known to be
; too short, and that is the whole of what it gives up. The full ring's answer
; is 6 rather than 7 because the derivation is capped at TRK_PREROLL.
%if TRK_RING_SM / TRK_HALF - 1 < 3
  %error "the small ring cannot stage a usable pre-roll (SPEC.md 45.18)"
%endif
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
                                    ; The cost is six halves mixed on the UI
                                    ; task inside the Play handler, and this
                                    ; comment used to say "a tenth of a second
                                    ; on an XT". MEASURED on a cycle-accurate
                                    ; 5150 it is 0.8 SECONDS - 12,288 bytes is
                                    ; 2.23 s of audio at the XT rate and the
                                    ; mixer runs at about a third of real time
                                    ; (PERFORMANCE.md Set 20), so the estimate
                                    ; was out by 8x. It is the longest thing
                                    ; this app does with the gfx lock held, it
                                    ; is why playback LOOKS jerky before it
                                    ; looks smooth, and it is announced rather
                                    ; than shortened: the ring never starves
                                    ; (lead measured at 14,336+ of 16,384
                                    ; throughout), so a smaller pre-roll would
                                    ; move the cost onto the worker and back
                                    ; towards the hitch it was raised to fix
                                    ; (SPEC.md 45.17.2)
TRK_MAXFEED equ 6                   ; halves mixed per worker wake, at most -
                                    ; bounds the lock-free burst so a wake
                                    ; never mixes more than ~1.1s of audio
TRK_WINW    equ TW_W + 2            ; the windowed face's FRAME: content plus
TRK_WINH    equ TW_HFULL + TITLE_H + 2  ; the border and the title bar. CGA's
                                    ; band cannot hold it and wm_fit clamps
                                    ; it, which is what tw_track's COMPACT
                                    ; layout is for (SPEC.md 45.21.1)

; =============================================================================
; Entry (SPEC.md 20.2): create the splash window, register menus + About,
; prepare the replayer. No drawing, no spawn, no fullscreen - no lock here.
; =============================================================================
trk_entry:
    push si
    push di
    call mp_init                    ; first: mp_* may clobber, and the CF we
                                    ; owe the loader comes from wm_create
    mov cx, TRK_RING                ; the ring DEFAULTS to the full one, and
    call trk_ring_set               ; it has to be set here rather than left
                                    ; to trk_ring_pick: bss arrives zeroed
                                    ; (SPEC.md 20.2), a zero [trk_rmask] wraps
                                    ; every stage onto offset 0, and the load
                                    ; path that would have picked is skipped
                                    ; entirely when the dialog reports no size
                                    ; (a typed name). The grant walk then
                                    ; corrects it downward if the heap says so
    call OSAPI_CPU_INFO             ; AL = tier (SPEC.md 41.8); a tier-0
    mov byte [trk_rsel], 1          ; (a 286 or better opens at 22 kHz:
    or al, al                       ; SPEC.md 45.10.2 - half the machine
    jnz .cpu                        ; there, and every card takes it) - and
    mov byte [trk_rsel], 0          ; a tier-0 machine gets XT mode pre-armed
                                    ; with its menu item already relabeled
    mov byte [mp_xt], 1             ; (SPEC.md 45.9) - no table to rebuild,
    mov byte [trk_cpu0], 1          ; nothing is loaded yet - and the machine
    mov word [trk_mi_file + TRK_MI_XT], trk_s_xton  ; itself is remembered
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
    call OSAPI_WM_SNAP              ; (SPEC.md 11.94): EVERY adapter - it was
                                    ; mono-only and VGA turned out to gain
                                    ; more - and it is what lets every text
                                    ; run we draw take font_run's single-store
                                    ; path instead of the erase-and-letter
                                    ; fallback (on VGA, the fallback's own
                                    ; glyphs are the ones that come out
                                    ; aligned). wm_snap preserves FLAGS, so
                                    ; the loader's CF still survives to .out
    mov si, trk_pref                ; the two faces as a DECLARATION (SPEC.md
    call OSAPI_WM_PREFER            ; 11.100.1): a drag onto the other card of
                                    ; an extended desktop (11.100.4) takes the
                                    ; frame that card's face wants, and
                                    ; tw_track re-picks the layout off the
                                    ; content height it lands at. Flags kept
    push bx                         ; THE BUTTONS' GESTURE (SPEC.md 20.5.1.3):
    mov ax, bx                      ; press inverts, release fires, a slide
    mov bx, tw_btns                 ; off cancels, and the two slots it rides
    mov si, tw_onup                 ; on are the library's to install
    mov di, tw_ondrag
    mov dx, tw_onclick              ; ...a press on no button goes here
    call os88ui_btninit
    pop bx
    mov ax, tw_clickw               ; ...and the press reaches US first, so the
    call OSAPI_WM_ONCLICK           ; rects are re-read where the window is NOW
                                    ; before the library hit-tests them (a
                                    ; drag calls none of our handlers)
    mov byte [trk_rep], 2           ; Repeat: List - and the first song opened
                                    ; is the list, so it loops as it always
                                    ; did (45.21.3, 45.22.4)
    mov byte [mp_endstop], 1        ; ...once there is one: an empty list ends
    mov byte [tpl_cur], 0FFh        ; no list entry playing
    cmp byte [trk_cpu0], 0          ; the visualiser: the spectrum where there
    jne .viz                        ; are cycles for it, the needles where
    mov byte [tw_viz], TWV_SPEC     ; there are not (tw_vizfx forces those)
.viz:
    clc                             ; (wm_create's success, which the loader
                                    ; reads; the calls above write flags)
    call trk_menus_build            ; ...and this ends in MENU_SET. It has to
                                    ; run before the first paint: every item
                                    ; it owns is composed into BSS, which
                                    ; arrives ZEROED, so a set installed ahead
                                    ; of it carries EMPTY strings.
                                    ; It preserves FLAGS too, so the loader's
    mov si, trk_about               ; CF still survives to .out
    call OSAPI_ABOUT_SET
    mov ax, trk_onwake              ; SPEC.md 54.10: the kernel calls this once
    call OSAPI_WM_ONWAKE            ; our window is on the glass, and the launch
                                    ; module loads in front of it. BX is still
                                    ; the window and the slot preserves the
                                    ; flags the loader's CF rides in
    OS88_ALTENTER_ARM               ; SPEC.md 11.2.1.1: nothing tracks a
                                    ; scancode until something asks, and both
                                    ; halves of the chord ride on that map.
                                    ; It preserves the FLAGS, like every other
                                    ; call in this chain and for the same
                                    ; reason - the loader's CF is riding here
    OS88_REGION_MOVABLE             ; OUR REGION MOVES (SPEC.md 66.6.1), and
                                    ; it is what makes the what-if verb of
                                    ; below mean anything: a region born
                                    ; PINNED reads the same in both plans, so
                                    ; the what-if would answer "no better" for
                                    ; a heap the compactor could have emptied.
                                    ; Here rather than beside wm_create
                                    ; because OSAPI_MEM_MOVABLE writes CF and
                                    ; the loader's is riding in it - every
                                    ; other call in this chain preserves the
                                    ; flags on purpose
    clc                             ; ...so put the success back. We only
                                    ; reach this line past wm_create's jc
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
; entry proc holds none (SPEC.md 20.2). trk_onwake spends it - the first
; W_PAINT did until SPEC.md 54.10, and that paint is inside wm_show's repaint,
; so the read still happened before the desktop had finished being drawn.
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
; trk_onwake - W_ONWAKE: play the module we were launched on (SPEC.md 54.10)
; in:  SI = our window; the UI task, gfx lock NOT held
; out: nothing; no register need be preserved
;
; The kernel calls this once assoc_run has shown our window, so the read
; happens in front of a Tracker the user can see and SPEC.md 12.8's widget
; steps through it. It takes the lock for the burst SPEC.md 74.1 lets it state:
; trk_fdone is the file dialog's own completion proc and it claims, reads,
; reclaims the grant and repaints, all of which want the lock held. ONE hold:
; the posted work below and a load are the same burst.
;
; Two loads arrive here, and both hand a name to trk_fdone, the dialog's own
; completion proc, which copies it through ES:DI - so ES points at OUR segment
; and DI at our buffer, and every refusal, claim and message below it is the
; path a dialog open already takes:
;
; - OUR OWN POSTED COMPACTION HAS RUN (SPEC.md 66.4.3), so this wake is the
;   SAME load attempt continuing and not a new one - trk_fdone left everything
;   it found alone and asked for the room. Nothing to look up: the name is
;   already in OUR segment and the size is already banked, so trk_fdone's own
;   copy is onto itself and there is no OSAPI_FILE_GOTO_QM to do - the folder
;   never changed, this wake being one ui_task pass after the post. It
;   re-asks plain OSAPI_MEM_AVAIL rather than trusting the what-if, because
;   that is the whole rule: the what-if measured a state the machine has
;   since left, and the number to claim against is the one the wake reports.
;   If it STILL does not fit, [trk_cpq] is what stops the next trip posting
;   again - a second post would be a program spinning. trk_fdone clears it on
;   its way out, whichever way this goes.
; - the module we were LAUNCHED on (trk_arg), once. DX:CX = 0 is "no size",
;   which that path already handles for a typed name.
; -----------------------------------------------------------------------------
trk_onwake:
    call OSAPI_GFX_LOCK             ; a song ENDED while nobody touched us: the
    call trk_sover_ck               ; worker woke us to close it and walk the
    call tpl_run                    ; list (SPEC.md 45.22.1) - or the editor
    call tw_refresh                 ; posted work only WE may do (45.22.3).
                                    ; Cheap when it is neither
    mov di, trk_fname               ; the compaction's retry: our own banked
    mov cx, [trk_fsize]             ; name, and DX:CX the size the dialog
    mov dx, [trk_fsize_hi]          ; gave us
    cmp byte [trk_cpq], 0
    jne .load
    cmp byte [trk_argp], 0          ; not the module's wake, or the module is
    je .out                         ; already playing
    mov byte [trk_argp], 0          ; once, whatever happens below
    mov dx, [trk_argclus]
    mov bl, [trk_argdrv]
    call OSAPI_FILE_GOTO_QM         ; the folder it was opened from -
    jc .out                         ; QUIETLY, and the instance with us, so
                                    ; trk_fdone's by-name load resolves there.
                                    ; Nothing here lists the folder, so the
                                    ; remount's scan, sort and icon harvest
                                    ; were ~0.5 s of floppy for no reader
                                    ; (docs/plans/NAV-COST-PLAN.md, SPEC.md
                                    ; 19.9.1). CF=1 moved NOTHING: the load
                                    ; would look in Tracker's own folder, so
                                    ; refuse. Nothing is kept across it (QM's
                                    ; AX, and a redirected volume's CX, SI and
                                    ; DI - SPEC.md 62.9.1)
    mov di, trk_argnm
    xor cx, cx
    xor dx, dx                      ; no size: the typed-name path
.load:
    push ds
    pop es                          ; ES:DI = our own name
    xor al, al
    call trk_fdone
.out:
    call OSAPI_GFX_UNLOCK
    ret

; -----------------------------------------------------------------------------
; trk_paint - W_PAINT: layout init (once), worker hire (retried), full redraw
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; SI kept - the one register wm_draw_win reads back after a
;      W_PAINT (`mov bx, si`, SPEC.md 73's thunk note) - and the rest
;      clobbered, which a paint proc may
; -----------------------------------------------------------------------------
trk_paint:
    push si
                                    ; NO trk_argload HERE. The first paint
                                    ; used to spend it, and the first paint is
                                    ; INSIDE wm_show's repaint - so the module
                                    ; was read with the desktop half redrawn and
                                    ; this window's own content still blank.
                                    ; trk_onwake spends it after that whole pass
                                    ; has finished (SPEC.md 54.10)
    call trk_reap                   ; F00/watchdog leftovers close on any UI
                                    ; event (SPEC.md 45.2)
    cmp byte [tui_inited], 0
    jne .l1
    call tui_layout_init
.l1:
    call trk_hire                   ; idempotent; refusal is transient, so it
                                    ; is retried every paint, never latched
    cmp byte [trk_fs], 0
    jne .fs
    call tw_paint                   ; the windowed face (SPEC.md 45.21)
    jmp short .done
.fs:
    call tui_draw_all
.done:
    pop si
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
    mov al, 1                       ; PARK-SAFE (SPEC.md 66.5.4): this app
    call OSAPI_MEM_PARKSAFE         ; never holds a pointer derived from the
                                    ; module across a call that can yield -
                                    ; trk_render takes the gfx lock as its
                                    ; first instruction, before it addresses
                                    ; anything, and the fullscreen drain holds
                                    ; only a counter. Without this the worker
                                    ; can only park at OSAPI_TASK_ALIVE, which
                                    ; it cannot reach while blocked on a lock
                                    ; some other app's callback is holding -
                                    ; and the module then never moves
    mov ax, trk_worker
    mov bx, [trk_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [trk_hired], 1
    OS88_WORKER_RESTARTABLE trk_worker  ; ...and the region cannot move without
                                    ; this: the kernel wrote our segment into
                                    ; this worker's frame before its first
                                    ; instruction, so mem_frameless pins a
                                    ; region with an undeclared worker however
                                    ; it is declared (SPEC.md 66.6.2). What a
                                    ; restart costs is one pass of the loop;
                                    ; trk_worker's own head is where the only
                                    ; state that survives it is put right
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_onkey - W_ONKEY (SPEC.md 45): the whole player is drivable from here,
;             because fullscreen makes the menu bar unreachable. The keys
;             themselves are trk_key's, which BOTH surfaces share.
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; out: nothing; clobbers AX..DI and ES like any window callback (SPEC.md
;      12.2). tpl_run, the one package caller, keeps nothing across it
; -----------------------------------------------------------------------------
trk_onkey:
%ifdef TRKDBG
    inc word [tds_wkeys]            ; bench-only, and the WINDOWED half of
                                    ; tds_keys: one counter per route, because
                                    ; the bracket polls int 16h itself (53.1)
                                    ; and this one is dispatched by ui_task -
                                    ; so which of the two doubles an arrow is
                                    ; the whole question
%endif
    call trk_reap                   ; F00/watchdog leftovers close first
    call trk_abdismiss              ; any key takes the About panel down and
    jc .out                         ; is spent doing it (both keep AX)
    call trk_key
.out:
    jmp tw_refresh                  ; the face follows the key NOW, not at the
                                    ; worker's next frame (SPEC.md 56.12)
; -----------------------------------------------------------------------------
; trk_oncmd - AM_ONCMD (SPEC.md 12.2): File > Open..., View > Fullscreen.
; Reachable windowed only (fullscreen hides the bar), which is why every
; command also has a key - and why [trk_fs] is 0 on every path in here.
; in:  AL = item, AH = menu, SI = our window, BX = the set; gfx lock held
; out: nothing; clobbers AX..DI and ES (SPEC.md 12.2 lets it)
; -----------------------------------------------------------------------------
trk_oncmd:
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
    or bl, bl                       ; View > Fullscreen
    jz .vfull
    cmp bl, 1                       ; View > Text Screen (SPEC.md 45.13.7):
    jne .out                        ; the surface pick. No belt like .vfull's
    call trk_txt_toggle             ; below - the row greys because XT mode
    jmp short .out                  ; makes the pick INERT, not unavailable,
                                    ; and MENU_DIS is not dispatched anyway
.vfull:
    call trk_fs_ok                  ; belt: the row is MENU_DIS while this
    jc .out                         ; refuses and a greyed row is not
    call trk_fs_enter               ; dispatched, so this cannot fire - and
    jmp short .out                  ; the greying and the refusal share one
                                    ; predicate (SPEC.md 47 rule 5) rather
                                    ; than being two opinions
.file:
    mov byte [trk_addpl], 0
    or bl, bl                       ; File > Open...
    jz .fopen
    cmp bl, 1                       ; File > Add to PlayList... (45.22)
    je .fadd
    cmp bl, 2                       ; File > PlayList Editor
    je .fedit
    cmp bl, 3                       ; File > XT Mode (the relabeling item)
    jne .out
    call trk_xt_toggle
    jmp short .out
.fadd:
    inc byte [trk_addpl]
.fopen:
    call trk_do_open
    jmp short .out
.fedit:
    call tpl_toggle
    jmp short .out
.rate:
    mov al, bl                      ; Rate > 11/22/33/44 kHz (SPEC.md 45.10)
    call trk_rate_set
.out:
    jmp tw_refresh                  ; a menu command's effect on the face now
; -----------------------------------------------------------------------------
; trk_about - the OSAPI_ABOUT_SET handler: the STANDARD About card (os88ui.inc,
;             SPEC.md 20.5.1), drawn over the face and remembered in
;             [trk_abon]. While it is up the worker drops its frames under the
;             lock (trk_render), tw_refresh refuses, and the button record has
;             NO live buttons - so nothing, a press included, can draw through
;             it. ModPlug's card was painted over by its own worker; this is
;             the shape that cannot be. Audio keeps feeding throughout.
; in:  SI = our window ptr; gfx lock held
; -----------------------------------------------------------------------------
trk_about:
    push bx
    push si
    mov byte [trk_abon], 1
    call tw_rects                   ; BT_N = 0: the buttons are not live
    mov bx, [trk_win]
    mov si, tw_ablines
    call os88ui_about               ; CF = 1: not visible, and the next paint
    pop si                          ; puts the card up (the flag is set)
    pop bx
    ret
; -----------------------------------------------------------------------------
; trk_abdismiss - take the About card down if it is up
; out: CF=1 the key/click was spent doing it; preserves every register
; -----------------------------------------------------------------------------
trk_abdismiss:
    cmp byte [trk_abon], 0
    je .none
    mov byte [trk_abon], 0
    call tw_rects                   ; the buttons are live again...
    call tw_redraw                  ; ...and the face is drawn whole over the
    stc                             ; card (tw_redraw arms our clip)
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
; [trk_addpl] says which dialog this is: 0 Open (load and play), 1 the
; PlayList's Add... - the completion proc reads it back (SPEC.md 45.22)
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
; out: [trk_modseg] / [trk_capk] updated; clobbers AX, CX, DX (its one
;      caller, trk_fdone, keeps nothing across it)
;
; This exists for the path where the claim is sized BEFORE the file's size is
; known - SPEC.md 38.6 gave the completion proc a size and the ordinary load
; now claims exactly it, but a dialog with no size for the pick still falls
; back to min(largest run, 128KB), where a 5.6KB module would sit on 128KB of
; heap until the next load or teardown. One call gives the difference back.
; On the sized path it finds nothing to give and costs a compare.
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
    mov ax, [mp_bloblen_lo]
    mov dx, [mp_bloblen_hi]
    call trk_tokb                   ; round UP: the heap's granularity is KB,
                                    ; and a truncating divide would hand back
                                    ; the tail of the file with the tail of
                                    ; the claim. What was READ cannot exceed
                                    ; the capacity the claim was made at, and
                                    ; trk_fdone's ~64MB domain guard bounds
                                    ; that, so DX<<6 fits a word in there for
                                    ; the same reason it does at the claim
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
    ret

; trk_tokb - DX:AX bytes -> AX = KB, rounded UP; ZF from the answer. DX:AX
; must be under ~64MB (DX <= 1022), which both callers guarantee. Clobbers CL
; and DX. DX:AX >> 10 = (DX << 6) + (AX >> 10)
trk_tokb:
    add ax, 1023
    adc dx, 0
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    ret

; trk_tobytes - AX = KB -> DX:AX bytes (KB * 1024). Clobbers CL
trk_tobytes:
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    ret

; trk_modfree - give the module blob's claim back, if there is one, and
; forget it. Clobbers DX (OSAPI_MEM_FREE keeps the rest, SPEC.md 50.3)
trk_modfree:
    xor dx, dx
    xchg dx, [trk_modseg]           ; DX, not AX (SPEC.md 50.3)
    or dx, dx
    jz .none
    call OSAPI_MEM_FREE
.none:
    ret

; -----------------------------------------------------------------------------
; trk_fdone - the file-dialog completion proc (SPEC.md 38.6). UI task, gfx
;             lock held, ES:DI = the chosen name with ES = KERNEL_SEG, valid
;             for this call only - so it is copied out FIRST.
;
; The load path: stop playback, free the previous module grant, size a new
; grant from the size the dialog reported - or, when it had none, from
; OSAPI_MEM_AVAIL capped at 128KB (SPEC.md 45.3.1) - read the
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
    jmp tw_refresh                 ; the face: what the load changed, and no
                                    ; more (tw_refresh arms our clip)
.full:
    cmp byte [trk_tx], 0            ; a playlist advance INSIDE the bracket
    jne .text                       ; (SPEC.md 45.22.2) lands here too, and on
    jmp tui_draw_all               ; the text surface no kernel drawing slot
                                    ; may be used (45.13.3)
.text:
    jmp ttx_draw_all

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
    or di, di
    jz .no                          ; no dot at all
    cmp word [di], 'MO'
    jne .no
    cmp word [di+2], 'D'            ; 'D' and its NUL: CF = 0 when equal
    je .out
.no:
    stc
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_sizeof - the size of [trk_fname] in the CURRENT directory
;
; in:  [trk_fname] = a NUL-terminated 8.3 display name
; out: CF = 0 and DX:AX = its size in bytes; CF = 1 = no such file here.
;      Clobbers BX, CX, SI, DI and leaves ES = DS: trk_fdone, its one caller,
;      keeps nothing across it.
;
; The dialog reports a size (SPEC.md 38.6) and an ASSOCIATION launch does not -
; it hands over a name, a cluster and a drive (SPEC.md 54.5) - so a module
; double-clicked on the desktop reached trk_fdone as "size unknown" and took
; the speculative claim, which is capped. A 300KB module then gets a 128KB
; claim it does not fit, and OSAPI_FILE_READ REFUSES it: dskw_rbody compares
; the directory size against the capacity before any data I/O and answers
; FERR_BIG, which .rderr maps to the same 'File too big'. So this route was
; refusing the file too, by a different door. (It is NOT a short read - that
; was this comment's first draft and SPEC.md 45.3.1 keeps the correction.)
; OSAPI_FILE_FIND answers all 32 bits out of the directory, so both routes
; size the claim the same way and the cap is left to the one case that still
; cannot know (SPEC.md 45.3.1).
;
; It costs one directory walk, on a path that is about to read the whole file.
; -----------------------------------------------------------------------------
trk_sizeof:
    push ds
    pop es                          ; ES:DI = our own record buffer
    xor cx, cx                      ; ordinal 0 starts the walk
.next:
    mov di, trk_find
    call OSAPI_FILE_FIND            ; CF=1 AX=FERR_NOENT ends it; CX = the
    jc .no                          ; ordinal to ask for next
    cmp word [trk_find + 14], OSAPI_FT_DIR
    jae .next                       ; a folder or the synthesized '..'
    mov si, trk_fname
    mov di, trk_find                ; +0 is the same 8.3 display form
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
    mov ax, [trk_find + 18]         ; +18 = the size, all 32 bits
    mov dx, [trk_find + 20]
    clc
    jmp short .out
.no:
    stc
.out:
    ret

trk_fdone:
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
    inc byte [tw_gen]               ; the LCD's file name (tw_lkey)
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
    cmp byte [trk_addpl], 0         ; the PlayList's Add... (SPEC.md 45.22):
    je .load                        ; the name goes on the list and NOTHING
    mov byte [trk_addpl], 0         ; is read - the dialog left us in its
    call tpl_add                    ; folder, which is what the entry banks
    jmp .out
.load:
    mov word [trk_needk], 0         ; "unknown" has to be written, not assumed:
                                    ; this word survives the LAST load, so a
                                    ; file whose size nothing can answer would
                                    ; otherwise claim the previous module's
    mov ax, [trk_fsize]             ; DX:AX = the size, and it is genuinely
    mov dx, [trk_fsize+2]           ; 32-bit: a 116KB MOD has a high word of 1
    mov bx, ax
    or bx, dx
    jnz .havesz
    call trk_sizeof                 ; 0 = the dialog had no size for it (an
    jc .sizeok                      ; association launch, or a name typed for
    mov bx, ax                      ; something not in the listing), so ask
    or bx, dx                       ; the directory. Still nothing -> leave it
    jz .sizeok                      ; unknown and let the file API answer as
.havesz:                            ; it always did
    cmp dx, 1023                    ; the ONLY ceiling here is the domain of
    jae .toobig2                    ; the conversion below (SPEC.md 45.3.1):
                                    ; it composes DX<<6 into a word, so ~64MB
                                    ; is where the arithmetic stops being
                                    ; true. Everything under it is the HEAP's
                                    ; question, asked three lines down
    call trk_tokb                   ; AX = KB needed (DX <= 1022 above, so
                                    ; <= 1023 in there and DX<<6 still fits)
    mov [trk_needk], ax
    push ax
    call OSAPI_MEM_AVAIL            ; AX = LARGEST contiguous run in KB
    pop bx
    cmp ax, bx
    jae .sizeok
    call trk_cpq_try                ; ...and if it does not fit AS THE HEAP
    jc .nomem2                      ; STANDS, could a compaction that moved US
    jmp .outq                       ; TOO have funded it? (SPEC.md 66.4.3)
                                    ; CF=0 means one is POSTED and we must
                                    ; return having touched nothing - the wake
                                    ; re-enters here with the room made. CF=1
                                    ; is a refusal that has earned itself
.sizeok:

    ; --- PAST THIS LINE THE PLAYING MODULE IS GONE (SPEC.md 45.3.1.1) -------
    ; Everything above refuses without touching anything; everything below
    ; frees the old blob before it claims the new one, so a failure after this
    ; point leaves the machine with NO module - the one that was playing
    ; included. THE GATE ABOVE IS THE GUARANTEE, and it is the whole of it.
    ;
    ; That guarantee was broken and this is where it showed: fdlg_sizeof
    ; handed over the PACKED on-disk size (SPEC.md 20.14.3), so a compressed
    ; module sailed through the .nomem2 test on a third of its real figure,
    ; got here, freed a playing module and then failed the read with
    ; FERR_BIG. The screenshot was a Tracker playing Beverly Hills Cop turned
    ; into 'No module loaded' by a load that never happened.
    ;
    ; DO NOT "FIX" THAT BY MOVING THE FREE BELOW THE READ. It is here because
    ; the new claim comes out of the space the old blob is sitting in: on the
    ; 640KB machine this project is calibrated for, holding both is twice the
    ; peak and is what makes a large module unloadable rather than merely
    ; slow. The refusals that CANNOT be pre-checked - .nomem (the heap
    ; fragmented under a figure OSAPI_MEM_AVAIL had just answered), .noring,
    ; a genuine FERR_IO - are the residue, and they are the price of the
    ; single-copy peak rather than an oversight.
    call trk_play_stop              ; silence + close + DRAIN before the blob
                                    ; moves: trk_stream_close spins out the
                                    ; worker's in-flight feed pass, so no
                                    ; mp_mixch is mid-fetch from the old
                                    ; grant past this line ([trk_mixing])
    mov byte [mp_loaded], 0         ; the old blob is about to be freed: no
                                    ; reader may trust it past this line
    call trk_modfree
    mov ax, [trk_needk]             ; the size the DIALOG reported, rounded to
    or ax, ax                       ; KB: claim what the file needs and not
    jnz .sized                      ; the whole largest run (SPEC.md 38.6).
                                    ; 0 = no size was available (a typed
                                    ; name), so fall back to the old
                                    ; largest-run-capped-at-128KB shape
    call OSAPI_MEM_AVAIL            ; AX = LARGEST contiguous run in KB, and
    or ax, ax                       ; BX = the total (KB, not
    jz .nomem                       ; paragraphs - SPEC.md 50.3)
    cmp ax, 128                     ; cap the SPECULATIVE grant at 128KB. This
    jbe .sized                      ; is the original cap and it is the only
    mov ax, 128                     ; one with a reason left (SPEC.md 45.3.1):
                                    ; the size is unknown here, so the claim
                                    ; is a guess, and trk_ring_probe has to
                                    ; fund a ring out of whatever this leaves.
                                    ; It is a POLITENESS bound on an over-
                                    ; claim, never a verdict on a file - the
                                    ; refusals above no longer borrow it
.sized:
    mov [trk_capk], ax
    call OSAPI_MEM_CLAIM            ; AX = KB -> DX = base segment, CF=1
    jc .nomem                       ; refused (the answer moves register too)
    mov [trk_modseg], dx

    call trk_ring_probe             ; RESERVE the ring before the floppy turns
    jc .noring                      ; (SPEC.md 45.18): the module is claimed,
                                    ; so what is free now is what the grant
                                    ; will have to come out of - and this ASKS
                                    ; for it rather than estimating, because
                                    ; OSAPI_MEM_AVAIL cannot see through a
                                    ; PURGEABLE claim and a subtraction from
                                    ; it refuses modules that play perfectly

    mov ax, [trk_capk]              ; DX:CX = capacity in bytes = KB * 1024
    call trk_tobytes
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
    inc byte [tw_gen]               ; a new title either way (inc keeps CF)
    jc .lderr
    mov dx, [trk_modseg]        ; ONLY NOW is it movable (SPEC.md 66.2). Not
    mov ax, trk_reloc           ; at the claim, and the ordering is the whole
    call OSAPI_MEM_MOVABLE      ; safety argument rather than a tidiness:
                                ; OSAPI_FILE_READ above holds ES:BX into this
                                ; block across a call that itself CLAIMS
                                ; (SPEC.md 18.95's sector cache), and
                                ; trk_ring_probe before it is a claim outright.
                                ; Pinned until the module is parsed, both are
                                ; safe by construction; declared any earlier,
                                ; the block could move mid-read with the
                                ; kernel's own pointer stale on its stack.
                                ; dsk_xfer pins the destination for the same
                                ; reason (66.3 rule 5) - this ordering does not
                                ; NEED that guard, and having both is how a
                                ; rule survives the next author
    call OSAPI_SND_CAPS             ; NO CARD: the samples filtered for the
    test ax, SND_CAP_PCM_BG         ; speaker now, once, rather than per
    jnz .card                       ; output sample (SPEC.md 45.25)
    call tsp_outq                   ; ...unless the output is a COVOX, which
    cmp word [tsp_lpt], 0           ; plays bass: the module as the file has
    jne .lpt                        ; it (SPEC.md 45.25.5)
    call tsp_natural
    jmp short .card
.lpt:
    mov byte [tsp_pre], 0           ; nothing filtered (tests/lzmod.py)...
    mov byte [tsp_lvok], 0          ; ...and a new song, a new level - which
.card:                              ; is tsp_natural's first line
    mov byte [ttx_shok], 0          ; a NEW module can name the same pattern
                                    ; NUMBER with different rows in it, and
                                    ; that is the one thing SPEC.md 45.13.6's
                                    ; carried shadow cannot see - every other
                                    ; input to it is in the four-way test
    mov si, mp_title                ; the loaded title becomes the status line
    call tui_msg
    mov byte [trk_pause], 0         ; a NEW module is not paused in the old
    call tpl_note                   ; one's place - and it is on the list now
    mov byte [trk_cpq], 0           ; THE ATTEMPT IS OVER BEFORE THE PLAY: a
                                    ; windowed speaker play is a loop that
                                    ; returns only when the song stops, and a
                                    ; posted load's flag held up across it
                                    ; refuses the next load its compaction
    mov al, 0
    call trk_play                   ; a card, or the speaker (SPEC.md 45.25)
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
.noring:                            ; the module fits and the RING does not, so
    mov si, trk_s_nofit             ; the claim has to come back off the heap
    jmp .failfree                   ; before we say so (SPEC.md 45.18)
.rderr:
    cmp ax, FERR_BIG
    je .big
    cmp ax, FERR_NOENT
    je .noent
    mov si, trk_s_ioerr
    jmp .failfree
.big:
    mov si, trk_s_toobig
    cmp dx, [trk_capk]              ; THE READ SAYS HOW MANY KB IT NEEDS
    jbe .failfree                   ; (SPEC.md 20.14.6.3), and more than we
                                    ; claimed means our SIZE was wrong, not
                                    ; the file: a compressed module whose
                                    ; directory hint a foreign OS dropped, so
                                    ; FIND reported what it OCCUPIES. Claim
                                    ; again at the real size - ONCE, because
                                    ; that claim is DX and the next answer
                                    ; cannot be more than it
    push dx
    call trk_modfree                ; the short claim goes back first, so the
                                    ; heap test below sees the room it held
    pop ax                          ; AX = KB
    call trk_tobytes
    mov [trk_fsize], ax             ; KB -> bytes, banked where the dialog's
    mov [trk_fsize_hi], dx          ; figure lives - so a compaction this
    jmp .load                       ; retry posts (trk_cpqload) reloads with
                                    ; it and not with the one that was wrong.
                                    ; Nothing new is read off the disk: the
                                    ; refusal came before any data I/O, and
                                    ; the directory and the header sector it
                                    ; walked are what the retry walks again
.noent:
    mov si, trk_s_noent
    jmp .failfree
.lderr:
    mov si, ax                      ; mp_load's own verdict string
.failfree:
    call trk_modfree
.fail:
    call tui_msg
    call trk_repaint_done
.out:
    mov byte [trk_cpq], 0           ; THIS ATTEMPT IS OVER, whichever way it
.outq:                              ; went, so the next load may ask for a
                                    ; compaction of its own. The POSTED path
                                    ; jumps past it: its wake is this same
                                    ; attempt continuing, and the byte is what
                                    ; stops that trip asking twice
    ret

; -----------------------------------------------------------------------------
; trk_cpq_try - the one question a refusal owes the user (SPEC.md 66.4.3)
; in:  [trk_needk] = the KB this load needs, and plain OSAPI_MEM_AVAIL has
;      already said no
; out: CF = 0 a compaction is POSTED and the caller must RETURN with nothing
;      stopped and nothing freed; CF = 1 refuse for real.
;      Preserves every register but the flags
;
; Tracker's requirement is EXACT - this module or no module - and a claim that
; fails is destructive: mem_claim sheds every purgeable cache on its way down,
; so the read-ahead and the FAT windows go and cost seconds of int 13h to
; rebuild. A package with an exact requirement that guesses wrong therefore
; pays for its refusal twice, which is why this asks before it claims and why
; "Too big for free memory" only gets said once the answer is "not even if the
; machine emptied itself for me".
;
; ONE POST PER ATTEMPT. The wake's own OSAPI_MEM_AVAIL is the number to decide
; on; posting again because the first did not give us what the what-if hoped
; for is how a program spins, and [trk_cpq] is the byte that refuses to.
; -----------------------------------------------------------------------------
trk_cpq_try:
    push ax
    push bx
    push si
    cmp byte [trk_cpq], 0
    jne .no                         ; asked once already for this load
    mov ax, MEM_LVL_TOP             ; AH = MEMC_WHATIF, AL = the level
    call OSAPI_MEM_COMPACT          ; AX = the largest run there would be if
    cmp ax, [trk_needk]             ; our own region moved too - a MEASUREMENT
    jb .no                          ; of now, never a promise about later
    mov bx, [trk_win]
    mov ax, (MEMC_POST << 8) | MEM_LVL_TOP  ; an ordinary claim's rank: every
    call OSAPI_MEM_COMPACT          ; cache counts as free, because our claim
    jc .no                          ; would shed them all anyway (SPEC.md 50.6.4)
    mov byte [trk_cpq], 1
    mov si, trk_s_cpq               ; ...and SAY so: the pass is hundreds of
    call tui_msg                    ; milliseconds of rep movsw on the target
    pop si                          ; machine and this callback returns into
    pop bx                          ; it, so the sentence has to be on the
    pop ax                          ; glass before the freeze rather than after
    clc
    ret
.no:
    pop si
    pop bx
    pop ax
    stc
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
    push dx
    push di                         ; (the speaker's bracket takes a hook)
    cmp byte [trk_fs], 0
    jne .out                        ; the bracket blocks, so this is belt-only
%ifdef TTXFSANY
    jmp short .surf                 ; the refusal below is the PRODUCT rule,
%endif                              ; and it makes the thing it was decided
                                    ; from unmeasurable: with it in place
                                    ; os88rate.py --fullscreen at 11 kHz is
                                    ; silently a WINDOWED run, which is
                                    ; exactly how it first reported 100.3% on
                                    ; a text screen it never reached. So the
                                    ; bench can bypass it. Bench-only, and the
                                    ; shipped build is byte-identical
    call trk_fs_ok                  ; XT mode's HIGH rate is WINDOWED-ONLY
    jnc .surf                       ; (SPEC.md 45.9.3): the 45.13 text screen
                                    ; delivers 88.7% of the music at 11 kHz.
                                    ; View > Fullscreen is GREYED while this
                                    ; refuses - one predicate, so the menu and
                                    ; the refusal cannot come to differ - but
                                    ; the F key and the splash click have no
                                    ; greying and still arrive here, so this
                                    ; is where the message is said. It is at
                                    ; THIS routine and not at its three call
                                    ; sites so a fourth cannot forget it
    push si
    mov si, trk_s_fsxhi
    call tui_msg
    pop si
    jmp .out
.surf:
    mov byte [trk_fs], 1            ; BEFORE the call - see the header
    call trk_legend                 ; ...and the status legend is picked FOR a
                                    ; surface, so it changes with this byte

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
    call trk_txon                   ; THE TEXT SCREEN'S SHADOW IS CLAIMED HERE
    je .noshadow                    ; (SPEC.md 45.13.8), before the bracket
    mov ax, (TTX_SHBYTES + 1023) / 1024   ; and not in it: a refusal must
    call OSAPI_MEM_CLAIM            ; still leave a screen to fall back to,
    jc .noshadow                    ; and ttx_begin refusing on a zero
    mov [ttx_shseg], dx             ; [ttx_shseg] is that fall-back - the
    mov byte [ttx_shok], 0          ; graphics bracket. A NEW claim holds
.noshadow:                          ; nothing, so the shadow is rebuilt
    call trk_spkq                   ; NO CARD: a RATE bracket, whose door the
    jnc .cardfs                     ; speaker's play opens (SPEC.md 45.25) -
                                    ; its period the text screen's 54.6 Hz
    mov ax, trk_fsx_main            ; frame clock, which FSXW_FRAME then is,
    mov bx, [trk_win]               ; and no worker kept: this bracket's own
    mov cx, FSXF_RATE               ; loop mixes (tsp_poll). The caps are
    mov dx, TSP_FSDIV               ; asked FIRST: the call is free to spend
    mov di, tsp_hook                ; BX, and the fence reads it
    call OSAPI_FSX_RUN
    jnc .ran                        ; (refused - a kernel with no FSXF_RATE:
.cardfs:                            ; the viewer's bracket, as it always was)
    mov ax, trk_fsx_main
    mov bx, [trk_win]
%ifdef TTXNOFAST
    mov cx, FSXF_KEEPWORKER         ; PERFORMANCE.md Set 67's A/B: the sub-tick
%else                               ; alone, which costs three IRQ0s and three
    mov cx, FSXF_KEEPWORKER | FSXF_FASTTICK   ; sch_switch scans a tick where
%endif                              ; one would do. Bench-only
                                    ; the worker feeds through the freeze, and
                                    ; the quantum goes to 18 ms so its slot
                                    ; costs the scroll a third of a frame
                                    ; rather than a whole one (SPEC.md 53.2.1)
    call OSAPI_FSX_RUN              ; blocks until trk_fsx_main returns; the
                                    ; kernel then repaints the desktop whole
.ran:
    call tsp_leave                  ; (the bracket's end shut the door)
    mov byte [trk_fs], 0            ; back to the windowed splash
    xor dx, dx                      ; ...and the shadow goes back to the heap:
    xchg dx, [ttx_shseg]            ; nothing outside a text bracket reads it
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_fsx_main - the exclusive main (SPEC.md 53.1): SI = our window, ES =
;                KERNEL_SEG, DS = CS = ours, gfx lock held for the whole
;                session, every task but us and our feeder frozen.
;                Returns (near) on Esc; the kernel restores the desktop.
;
; The worker's drawing moved HERE: the bracket is the one drawer (lock held
; throughout), input is polled (no events are dispatched in a bracket), the
; unlocks, so that flush is the only one a buffered frame gets (SPEC.md
; 53.5/45.11). The worker keeps feeding audio and nothing else.
; out: nothing; clobbers AX..DI - a W_ONKEY's environment (SPEC.md 53.1),
;      and fsx_run banks all six around it (kent_di)
; -----------------------------------------------------------------------------
trk_fsx_main:
    call trk_txon                   ; the 80x25 TEXT screen (SPEC.md 45.13):
    je .gfx                         ; XT mode forces it, [trk_txw] asks for it
    call ttx_begin                  ; (45.13.7) - see trktxt.inc for the
    jc .gfx                         ; arithmetic. CF=1: refused, and nothing
                                    ; was changed - fsx_mode either
.txok:                              ; sets the mode or touches nothing at all
    mov byte [trk_tx], 1
    call ttx_draw_all
    call ttx_clkpick                ; the frame clock (SPEC.md 45.16/53.5.1)
    jmp short .loop
.gfx:
    call tui_draw_all               ; the whole FT2 screen
.loop:                              ; ONE loop for both surfaces: what differs
                                    ; is the frame and its clock, asked of
                                    ; [trk_tx] once a frame
    call tsp_poll                   ; no card: the speaker's producer (45.25)
    call trk_sover_ck               ; F00 / song end: close, walk the list
    call os88alt_edge               ; ...and Alt+Enter, which int 16h below
    jc .done                        ; cannot carry: no XT BIOS enqueues the
                                    ; combination (SPEC.md 9.7.1) and a
                                    ; bracket dispatches no events (53.1)
    mov ah, 1
    int 0x16                        ; this IS the UI task (SPEC.md 53.1)
    jz .draw
    xor ah, ah
    int 0x16
    cmp al, 27                      ; Esc, and F - the key that GOT you here
    je .done                        ; is the key that leaves (SPEC.md 45.17)
    cmp al, 'f'
    je .done
    cmp al, 'F'
    je .done
    call trk_fsx_key
.draw:
    cmp byte [trk_tx], 0
    jne .txdraw
    call tui_draw_dyn               ; the animated frame (no clip: we own the
                                    ; screen, nothing is on top)
    xor al, al                      ; one frame per tick - the worker's pace,
    call OSAPI_FSX_WAIT             ; the frame clock (SPEC.md 53.5)
    jmp .loop
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
    jmp .loop                       ; it cannot. The SPEC.md 53.5 present
                                    ; clause is dead either way by
                                    ; construction: fsx_mode refused a buffer
.done:
    mov byte [trk_tx], 0            ; before [trk_fs], so a message set on the
                                    ; way out reaches the windowed splash
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
    jmp trk_legend                  ; ...and the legend follows the surface

; -----------------------------------------------------------------------------
; trk_fsx_key - the bracket's keyboard (SPEC.md 53.1: input is polled, no
;               event is dispatched), under the held lock. It is trk_key:
;               the two surfaces share ONE keyboard and every key that means
;               something different in a bracket asks [trk_fs] or [trk_tx]
;               itself. Esc and F never arrive here - the bracket's loop
;               takes them first.
; trk_key     - the keys of both surfaces (SPEC.md 45, 45.21.7)
;   Enter      play song (or resume)  Space  pause / play
;   Home       play from the top      P      loop this pattern
;   Left/Right song position          Up/Dn  scroll rows (stopped only)
;   L          Load... (windowed; fullscreen says why)
;   F, Alt+Enter  fullscreen (trk_fs_enter refuses inside one)
;   X          XT mode (the text screen says why not)
;   V          the fullscreen surface (windowed only)    R  the rate
;   N / B      next / previous in the PlayList (|<< restarts a song with none)
;   E          the PlayList editor - windowed only: a window cannot open over
;              a bracket (SPEC.md 53.7), so the key says why, like L does
;   O          Repeat: Off / Song / List         H   Shuffle
;   + = -      master volume, a sixteenth of the range a press
;   1..4       channel mutes
; Letters are taken in either case.
; in:  AL = ascii, AH = scan
; out: nothing; clobbers AX..DI (both callers keep nothing across it)
; -----------------------------------------------------------------------------
trk_fsx_key:
%ifdef TRKDBG
    inc word [tds_keys]             ; bench-only (tests/trkscrl.inc): HOW MANY
                                    ; KEYS THE BRACKET ACTUALLY SAW. tests/
                                    ; trkscrl.py measured Up/Down moving the
                                    ; stopped view by TWO rows for one
                                    ; `sendkey`, and the two candidates -
                                    ; one event handled twice, or two events -
                                    ; are indistinguishable from outside.
                                    ; trk_dbg_key cannot answer it: that hook
                                    ; is on the ascii path and an arrow
                                    ; never reaches it
    or al, al                       ; ...and the bench's own keys, AHEAD of
    jz trk_key                      ; trk_key's: none of g j k n u b c is one
    cmp al, 'g'                     ; of its main keys, and N/B lowercase were
    je .grid                        ; the bench's before the list's already
    cmp al, 'G'
    je .grid
    call trk_dbg_key
    jc trk_key
    ret
.grid:
    call trk_dbg_area
    jmp tui_draw_pat
%endif
trk_key:
    mov bx, ax                      ; BL = ascii, kept for the mutes
    or al, al                       ; the keypad trap: '4'/'6' arrive with
    jnz .ascii                      ; arrow scan codes - ascii==0 first
    mov al, ah
    cmp al, 0x4B                    ; left arrow: previous song position
    je .prev
    cmp al, 0x4D                    ; right arrow: next
    je .next
    cmp al, 0x48                    ; up: scroll rows while stopped
    je .up
    cmp al, 0x50                    ; down
    je .down
    cmp al, 0x47                    ; Home: back to the top and play
    je .top
    cmp al, KEY_ALTENTER >> 8       ; Alt+Enter is the same door as F (SPEC.md
    je .fs                          ; 11.2.1.1): ascii 0, so it is a scan here
    ret
.prev:
    mov al, -1
    jmp short .pos
.next:
    mov al, 1
.pos:
    jmp mp_setpos                   ; the worker's change detection redraws
.up:
    mov ah, -1
    jmp short .vrow
.down:
    mov ah, 1
.vrow:
    cmp byte [mp_playing], 0
    jne .r
    mov al, [tui_vrow]
    add al, ah                      ; 0 - 1 and 63 + 1 both land above 63
    cmp al, 63
    ja .r
    mov [tui_vrow], al
.r:
    ret

.ascii:
    cmp al, 13                      ; Enter: play the song
    je .play
    cmp al, ' '                     ; Space: stop / play toggle
    je .space
    cmp al, 'A'
    jb .sym
    or al, 20h                      ; the letters, either case
.sym:
    cmp al, 'p'
    je .pat
    cmp al, 'l'
    je .load
    cmp al, 'f'
    je .fs
    cmp al, 'x'
    je .xt
    cmp al, 'r'
    je .rcyc
    cmp al, 'v'                     ; V: the fullscreen SURFACE (SPEC.md
    je .txtog                       ; 45.13.7). Free in every build - T is
                                    ; tlog_clk_key's in a TRKLOG one, and the
                                    ; mnemonic is not worth a silent collision
    cmp al, 'n'
    je .lnext
    cmp al, 'b'
    je .lprev
    cmp al, 'e'
    je .edit
    cmp al, 'o'
    je .rep
    cmp al, 'h'
    je .shuf
    cmp al, '+'
    je .vup
    cmp al, '='                     ; + unshifted, on a US keyboard
    je .vup
    cmp al, '-'
    je .vdn
%ifdef TRKLOG
    cmp al, 'd'
    je .diag
    cmp al, 'm'
    je .mark
    cmp al, 'w'
    je .wlog
    cmp byte [trk_fs], 0            ; the rest are the windowed keyboard's
    jne .mute                       ; alone, as they always were
    cmp al, 'y'
    je .synct
    cmp al, 't'
    je .clkt
    cmp al, 'k'
    je .xrate
%endif
.mute:
    mov al, bl                      ; 1..4: channel mute toggle; the worker's
    sub al, '1'                     ; scope redraw shows it next frame. Any
    jmp mp_mutetog                  ; other key wraps above 3 and is ignored

.play:
    jmp trk_play_go
.space:
    cmp byte [mp_playing], 0
    je .play
    call trk_play_stop
    jmp trk_transport
.top:
    xor al, al                      ; Home: play from the TOP - the restart
    jmp short .tplay                ; Enter used to be (SPEC.md 45.17)
.pat:
    mov al, 1
.tplay:
    jmp trk_play
.load:
    cmp byte [trk_fs], 0            ; Load is windowed: the Standard File
    jne .fsload                     ; dialog is unreachable in a bracket (its
    mov byte [trk_addpl], 0         ; completion comes through the parked
    jmp trk_do_open                 ; event ladder, SPEC.md 53.7)
.fsload:
    mov si, trk_s_fsload
    jmp short .say
.fs:
    jmp trk_fs_enter
.xt:
    cmp byte [trk_tx], 0            ; the text screen IS XT mode's surface
    jne .txxt
    jmp trk_xt_toggle
.txtog:
    cmp byte [trk_fs], 0            ; no stop, no rebuild - the pick is not
    jne .txxt                       ; something the mixer can see (45.13.7)
    jmp trk_txt_toggle
.edit:
    cmp byte [trk_fs], 0
    jne .txxt
    jmp tpl_toggle
.txxt:
    mov si, trk_s_txxt
.say:
    jmp tui_msg
.rcyc:
    jmp trk_rcyc
.lnext:
    jmp tpl_next
.lprev:
    jmp tpl_prev
.rep:
    jmp trk_rep_cycle
.shuf:
    xor byte [trk_shuf], 1
    ret
.vup:
    mov al, 1
    jmp short .vol
.vdn:
    mov al, -1
.vol:
    call trk_spkq                   ; no card: the speaker's level a step
    jnc .vcard                      ; (45.25.3)
    jmp tsp_lvstep
.vcard:
    shl al, 1                       ; the master a sixteenth of its range:
    shl al, 1                       ; +-4, and 0..64 + 4 cannot overflow a
    add al, [mp_master]             ; signed byte, so the sign is the floor
    jns .vpos
    xor al, al
.vpos:
    cmp al, 64
    jbe .vset
    mov al, 64
.vset:
    mov [mp_master], al
    ret
%ifdef TRKLOG
.diag:
    jmp tlog_key                    ; D: the log on/off (tests/trklog.inc)
.mark:
    jmp tlog_mark                   ; M: the listener heard something
.wlog:
    cmp byte [trk_fs], 0            ; W: TRKLOG.TXT on the current volume -
    jne .wref                       ; windowed, because a save is a file
    jmp tlog_save
.wref:
    mov si, tlog_s_fsw
    jmp short .say
.synct:
    jmp tlog_sync_key               ; Y: SPEC.md 45.15 off, to A/B it
.clkt:
    jmp tlog_clk_key                ; T: SPEC.md 45.16 off, likewise
.xrate:
    jmp tlog_rate_key               ; K: XT mode's sample rate, WITHOUT
                                    ; leaving XT mode (FIELD-NOTES.md 16)
%endif
; -----------------------------------------------------------------------------
; trk_ring_set - commit a ring size and everything derived from it
; in:  CX = ring bytes (TRK_RING or TRK_RING_SM)
; out: nothing; [trk_ring]/[trk_rmask]/[trk_preroll] set
; clobbers: nothing (flags)
;
; The ONE writer of the three, because they are one fact. The pre-roll is
; derived rather than tabled - every half the ring has bar the one the feed
; ceiling keeps free - and then capped at TRK_PREROLL, which is what stops a
; bigger ring buying a longer pre-roll nobody asked for. The cap is why the
; full ring's answer is 6 and not 7.
; -----------------------------------------------------------------------------
trk_ring_set:
    push ax
    push cx
    mov [trk_ring], cx
    mov ax, cx
    dec ax
    mov [trk_rmask], ax             ; ring - 1: a power of two, so this is the
    mov ax, cx                      ; wrap mask
    mov cl, 11                      ; halves = ring / TRK_HALF (2048)
    shr ax, cl
    dec ax                          ; ...bar the one the ceiling keeps free
    cmp ax, TRK_PREROLL
    jbe .cap
    mov ax, TRK_PREROLL
.cap:
    mov [trk_preroll], ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_ring_pick - choose the ring from the free RAM and the rate, NEVER refuse
; in:  nothing (asks OSAPI_MEM_AVAIL and trk_rate_pick itself)
; out: nothing; the ring committed
; clobbers: nothing (flags)
;
; SPEC.md 45.18's policy in two compares: at or below TRK_ROOMYKB of free RAM
; take the small ring and the pre-roll hitch with it - a question about
; POLITENESS rather than fit, 16KB out of a 20KB run being most of a small
; machine's heap - and at or below TRK_RING_HZ take it too, because a stream
; that slow never fills the full one (SPEC.md 45.18.2). Called at load (the
; probe) and again at every Play, since R moves the rate in between.
;
; **It cannot refuse, and that is deliberate.** OSAPI_MEM_AVAIL walks the
; claim map with mem_run, which counts a PURGEABLE claim (SPEC.md 50.4's
; 0xFExx tags - the directory read-ahead window is 63KB of one) as blocking,
; while an actual mem_claim will purge to satisfy a request. So the number
; here can be far smaller than what a claim would really get: measured on a
; 256KB machine it answered 21.5KB while the old code went on to fund an 18KB
; module AND a 16KB ring out of the same heap. Estimating downward from it
; and refusing turned a module that played perfectly into "Too big for free
; memory" - so this only ever biases the CHOICE, and trk_ring_probe does the
; refusing by asking.
; -----------------------------------------------------------------------------
trk_ring_pick:
    push ax
    push bx
    push cx
    call OSAPI_MEM_AVAIL            ; AX = largest run KB (BX = total)
    mov cx, TRK_RING_SM
    cmp ax, TRK_ROOMYKB
    jbe .set
    call trk_rate_pick              ; AX = the rate the next Play opens at:
    cmp ax, TRK_RING_HZ             ; a slow stream never fills the full ring
    jbe .set                        ; (SPEC.md 45.18.2)
    mov cx, TRK_RING
.set:
    call trk_ring_set
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_ring_probe - reserve the ring BEFORE the floppy turns
; in:  the module claim already taken (so the heap is as Play will find it)
; out: CF = 0 a ring of [trk_ring] can be had; CF = 1 nothing can, and the
;      caller must free the module and refuse the load
; clobbers: nothing (flags via CF)
;
; The original defect this whole section exists for: the module was claimed
; and read - seconds of floppy on the target machine - and Play then said
; "Out of memory" over a title that was already on screen. The expensive half
; succeeded and the cheap half failed.
;
; So the ring is asked for HERE, between the module's claim and its READ, and
; given straight back. Asking is the only honest test (SPEC.md 50.3's rule
; for mem_claim_dma, for the same reason): a purgeable claim makes every
; estimate pessimistic, and only the allocator knows what it would do. The
; grant is not KEPT because a stopped Tracker deliberately holds no pool
; (SPEC.md 34.6) - and it need not be, since trk_stream_open walks the tiers
; again and the heap is free to move in between either way.
; -----------------------------------------------------------------------------
trk_ring_probe:
    push ax
    push cx
    push si
    cmp byte [trk_ghave], 0         ; A GRANT WE STILL HOLD IS A RING: the one
    jne .none                       ; trk_stream_close could not give back (its
                                    ; free refused, so the latch stayed) and
                                    ; which trk_play reuses. Asking for a SECOND
                                    ; is refused by the driver, and that refusal
                                    ; read as "Too big for free memory" on the
                                    ; first playlist advance after a song ENDED
                                    ; (SPEC.md 45.22.1)
    call OSAPI_SND_CAPS             ; AX = merged caps word
    test ax, SND_CAP_PCM_BG
    jz .none                        ; NO BACKGROUND PCM SINK: this machine is
                                    ; a viewer (trk_play's own gate), so there
                                    ; is no ring to reserve and a load must
                                    ; never be refused for the want of one -
                                    ; without this the whole no-Sound-Blaster
                                    ; case stops loading modules at all
    call trk_grant_walk             ; the policy choice, from what is free now
    jc .no                          ; even the small ring is unfundable
    mov ax, (1 << 8) | 7
                                    ; sub-op 1 = free: we only wanted to know
    call OSAPI_SND_STREAM
.none:
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop si
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; trk_restart - a stream at AX Hz starts, the card's (trk_play) or the
;               speaker's (tsp_start): the replayer from [trk_pmode], and
;               every counter the face and the stamps run off back to 0.
;               Clobbers AX and DX
; -----------------------------------------------------------------------------
trk_restart:
    call tw_elscale                 ; a resume at another rate keeps its
    mov [mp_mixrate], ax            ; clock, in the new rate's bytes
    mov al, [trk_pmode]
    call tw_newstream               ; the face's clock: restarted with a song,
    call mp_start                   ; kept across a resume (AL survives it)
    xor ax, ax
    mov [trk_total], ax
    mov [trk_consumed], ax          ; the stream's byte counters both restart
    mov [mp_stampbase], ax          ; here, so the stamp history does too -
    call mp_stclear                 ; seeded with row 0, which mp_start has
                                    ; already read but nothing has mixed yet
                                    ; (it keeps AX)
    mov [tui_play], ax
    mov [trk_mixed], ax             ; a restart drops a half in progress
    mov ax, [mp_mixrate]            ; bytes per system tick: rate / 18.2065,
    mov dx, 3600                    ; and 3600/65536 is that to 0.011% - so
    mul dx                          ; the product's HIGH word is the answer
    mov [tui_bpt], dx               ; and no division is needed at all
    ret

; -----------------------------------------------------------------------------
; trk_grant_walk - pick a ring (trk_ring_pick) and ask the driver's pool for
;                  it, dropping to the small ring once on a refusal
; out: CF = 0 granted: SI = the grant's SND_SEG offset, and trk_ring_set has
;      been told what we ACTUALLY got, so the mask and pre-roll follow it and
;      not the pick; CF = 1 even the small ring is unfundable. Clobbers AX, CX
;
; The pick was made from what was free when asked, and the heap is free to
; move between a load and a Play - another app, a Disk window - so a refusal
; drops a tier and asks again rather than making the user close things and
; re-load. One retry: below the small ring there is nothing left to try.
; -----------------------------------------------------------------------------
trk_grant_walk:
    call trk_ring_pick
    mov cx, [trk_ring]
.try:
    mov ax, 7                       ; AL = verb 7, AH = sub-op 0 = alloc
    call OSAPI_SND_STREAM           ; out AX = 0 ok, SI = grant offset
    or ax, ax
    jz .got
    cmp cx, TRK_RING_SM
    jbe .no
    mov cx, TRK_RING_SM
    jmp short .try
.got:
    call trk_ring_set
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; trk_play - start playback
; in:  AL = 0 play song from the top / 1 loop the current pattern
;      gfx lock held (key handler, menu, completion proc)
; Refusals are status-line messages, never aborts: no module, no PCM_BG sink
; (the no-SB machine keeps the viewer), no staging memory.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; trk_play_go - Enter, and Space out of a stop: RESUME (SPEC.md 45.17)
;
; trk_play_stop parks the replayer at the row the LISTENER heard, so a stop IS
; a pause and the only thing missing was a play that honoured it. The one case
; that must not resume is a song that RAN OUT: the parked position is then the
; end, and resuming there ends again on the spot. [trk_ended] is that latch -
; set by the worker at F00 or the watchdog, cleared by trk_play as it opens the
; next stream, so it says exactly "the last thing that stopped us was the end".
; -----------------------------------------------------------------------------
trk_play_go:
    mov al, 2
    cmp byte [trk_ended], 0
    je .go
    xor al, al
.go:
    jmp trk_play

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
    jz .spk                         ; no card: the SPEAKER (SPEC.md 45.25)
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
    call trk_grant_walk             ; picked AGAIN: the rate may have moved
    jc .nogrant                     ; since the load (R), and the ring is
                                    ; sized by it (SPEC.md 45.18.2)
    mov [trk_grant], si
    mov byte [trk_ghave], 1
.granted:
    call trk_rate_pick              ; AX = the rate this mode asks for
    call trk_restart
%ifdef TRKLOG
    call tlog_stream                ; a new stream: the log's clocks restart
%endif
    mov byte [tui_noppos], 0        ; ...and tui_playpos asks the card again
                                    ; (SPEC.md 45.15.1, 34.5.1)
    mov si, trk_s_buffer            ; ...and SAY SO, because the loop below is
    call tui_msg                    ; the longest thing this app ever does with
    call trk_say                    ; the gfx lock held (SPEC.md 45.17.2)
    mov cx, [trk_preroll]           ; stage the cushion before the open, so
.pre:                               ; the stream starts [trk_preroll] halves
    call trk_mix_stage              ; ahead of the DSP instead of two
    loop .pre                       ; (trk_mix_stage keeps CX)
.preok:
    mov ax, SND_OPENF_RING << 8     ; verb 0: open-out, ring mode - the flag
                                    ; rides AH (verb 0 carries no handle,
                                    ; SPEC.md 20.3; DX stays the plain rate)
    mov si, [trk_grant]
    mov cx, [trk_total]             ; initial valid total = the pre-roll
    mov dx, [mp_mixrate]            ; the mode's rate, set above
    call OSAPI_SND_STREAM           ; out AL = 0 with AH = handle, else err
    or al, al
    jnz .ofail
    mov [trk_hand], ah
    mov byte [trk_ended], 0         ; re-arm BEFORE publishing: the worker
    mov byte [trk_pause], 0
    mov byte [trk_sopen], 1         ; keys every pass on trk_sopen, and a
                                    ; pass must never see the new stream
                                    ; through the old session's flags
    call trk_transport              ; the one success path; every exit below
    jmp short .out                  ; is a refusal with its own message
.spk:
    call tsp_play
    jmp short .out
.noload:
    mov si, trk_s_noload
    jmp short .say
.nogrant:
    mov si, trk_s_nomem
    jmp short .say
.ofail:
    push ax                         ; mp_stop ZEROES AX, and the test below is
    call mp_stop                    ; the driver's answer: every refusal read
    pop ax                          ; 'Sound open failed', err 2 included
    mov si, trk_s_snderr
    cmp ax, 2                       ; err 2 = rate refused: a 33/44 kHz pick
    jne .of8                        ; on a pre-3.x DSP (SPEC.md 45.10) - the
    mov si, trk_s_norate            ; menu greys those now, so this is a belt
    jmp short .ofmsg
.of8:
    cmp ax, 8                       ; err 8 = no page-safe 8KB for the
    jne .ofmsg                      ; double buffer, claimed per stream now
    mov si, trk_s_nomem             ; (SPEC.md 34.5.2): a memory answer
.ofmsg:
.say:
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
; keeps CX (trk_play's pre-roll counts in it) and clobbers the rest
; -----------------------------------------------------------------------------
trk_mix_stage:
    push cx
    mov ax, [trk_total]             ; where mp_outbuf[0] lands in the stream:
    mov [mp_stampbase], ax          ; the replayer stamps each row against it
    mov cx, TRK_HALF                ; (SPEC.md 45.15)
    mov word [trk_mixed], 0         ; a whole half: no piece is in progress
    call mp_gen                     ; renders into mp_outbuf, advances the
                                    ; replayer; clobbers freely (mp_* rule)
    call trk_stage
    pop cx
    ret

; trk_stage - stage mp_outbuf, a WHOLE half, at the ring position [trk_total]
; names and advance [trk_total]; [trk_mixed] back to 0. Preserves all.
trk_stage:
    push ax
    push cx
    push si
    push di
    mov word [trk_mixed], 0
    mov di, [trk_total]
    and di, [trk_rmask]
    add di, [trk_grant]             ; physical grant offset of stream byte n
    mov si, mp_outbuf
    mov cx, TRK_HALF
    mov al, 6                       ; verb 6: stage (kernel copies from our
    call OSAPI_SND_STREAM           ; segment; worker-safe since the amendment)
    add word [trk_total], TRK_HALF  ; free-running 16-bit, mod 65536
    pop di
    pop si
    pop cx
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
    mov ax, (1 << 8) | 7
                                    ; sub-op 1 = free
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
    cmp byte [trk_ended], 0         ; ONLY a stream the worker has seen DRAIN:
    je .out                         ; the song's tail is in the ring and the
                                    ; card plays it first. This used to close
                                    ; the moment the replayer stopped too, and
                                    ; with a playlist that cut the last seconds
                                    ; off every song (SPEC.md 45.22.1)
    call trk_stream_close
    mov byte [trk_sover], 1         ; ...and the song is OVER: what happens next
.out:                               ; is trk_song_over's, run where a load may
    ret                             ; happen - a wake or the bracket, never a
                                    ; paint (SPEC.md 54.10)

; -----------------------------------------------------------------------------
; trk_sover_ck - spend a song-over (lock held, UI task, not a paint). The
; windowed half is trk_onwake's, woken by the worker the moment it latches
; the end; the fullscreen half is the bracket's own loop, where no event is
; dispatched at all (SPEC.md 53.1). preserves all
; -----------------------------------------------------------------------------
trk_sover_ck:
    call trk_reap
    cmp byte [trk_sover], 0
    je .out
    mov byte [trk_sover], 0
    call trk_song_over
.out:
    ret

; -----------------------------------------------------------------------------
; trk_stop - the transport's Stop: a PAUSE that forgets where it was, so Play
; starts the song again from the top. [trk_ended] is exactly that fact
; (trk_play_go's own latch), and the face's Stop lamp reads it. lock held.
; -----------------------------------------------------------------------------
trk_stop:
    push ax
    call trk_play_stop
    mov byte [trk_pause], 0
    mov byte [trk_ended], 1
    xor al, al
    mov [mp_songpos], al
    mov [mp_row], al
    call mp_setposn                 ; (the pattern with it)
    call tw_newstream               ; ...and the clock
    call trk_transport
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_rate_pick - AX = the rate the next Play asks for: XT mode's own, or the
; Rate menu's pick (SPEC.md 45.9, 45.10). trk_play's choice, factored so the
; face can SAY it before anything plays. Preserves the rest.
; -----------------------------------------------------------------------------
trk_rate_pick:
    push bx
    cmp byte [mp_xt], 0             ; XT mode overrides the Rate menu with
    je .rsel                        ; its own rate (SPEC.md 45.9/45.10)
%ifdef TRKLOG
    mov ax, [tlog_xrate]            ; ...which K can move WITHOUT leaving XT
    jmp short .out                  ; mode, so the rate can be swept with the
%else                               ; surface held still (docs/FIELD-NOTES.md
    mov ax, TRK_RATE_XT             ; 16). Bench-only, and it OUTRANKS the
    cmp byte [trk_xhi], 0           ; user's pick below: a sweep that a
    je .out                         ; setting could veto is not a sweep
    mov ax, TRK_RATE_XT2            ; 45.9.3's windowed-only high rate
    jmp short .out
%endif
.rsel:
    mov bl, [trk_rsel]              ; the Rate menu's pick: 0/1/2
    xor bh, bh
    shl bx, 1
    mov ax, [trk_rates + bx]
.out:
    pop bx
    ret

; trk_rcyc - R, the Rate option button: cycle whatever the Rate MENU lists in
; this mode - 11/22/44 outside XT mode and 5.5/11 inside it (SPEC.md
; 45.9.3/45.10). One routine, so the key, the button and the menu agree.
trk_rcyc:
    push ax
    push cx
    call trk_rsel_get
    mov al, ah
    inc al
    call trk_rcount
    cmp al, cl
    jb .set
    xor al, al
.set:
    call trk_rate_set
    pop cx
    pop ax
    ret

; trk_rep_cycle - O, the Repeat button: Off -> Song -> List
trk_rep_cycle:
    push ax
    mov al, [trk_rep]
    inc al
    cmp al, 3
    jb .set
    xor al, al
.set:
    mov [trk_rep], al
    pop ax                          ; ...and the loop rule follows it

; trk_endstop_upd - does the song playing END at its order wrap, or does the
; replayer loop it by itself ([mp_endstop] = 0, SPEC.md 45.21.3)? It loops
; under Repeat Song, and under Repeat List when the list is ONE entry and that
; entry is what is playing (45.22.4): going round a one-song list is looping
; the song, and doing it in the replayer is seamless where a restart would be
; a gap and a pre-roll. Asked on every Repeat change and every list change
; (tpl_refresh), so adding a second song mid-play lets the first END. The
; worker reads the byte; one store is atomic. Preserves all but the flags.
trk_endstop_upd:
    push ax
    xor al, al
    cmp byte [trk_rep], 1
    je .set
    cmp byte [trk_rep], 2
    jne .ends
    cmp byte [tpl_n], 1
    jne .ends
    cmp byte [tpl_cur], 0
    je .set
.ends:
    inc ax
.set:
    mov [mp_endstop], al
    pop ax
    ret


trk_play_stop:
    call tsp_leave                  ; the speaker's door shut, CONS exact...
    cmp byte [tsp_run], 0
    je .park
    push es                         ; ...and the stream's counters the ring's,
    mov es, [tsp_rseg]              ; so the speaker's play is asked like a
    mov ax, [es:TSP_RL]             ; card's: tsp_run still says it is one
    mov [trk_total], ax             ; (45.25), or tui_sync quotes the MIXER
    mov ax, [es:TSP_RL+2]
    mov [trk_consumed], ax
    pop es
.park:
    mov byte [trk_pause], 1         ; a stop PARKS (SPEC.md 45.17): Play resumes
    call tui_sync                   ; where the LISTENER is, asked while the
                                    ; stream can still answer (SPEC.md 45.15)
    call tsp_halt                   ; ...and only then the speaker's play over
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
; trk_say - put [tui_msgp] on the glass NOW, on whichever surface is up
;
; tui_msg only records; the pixels are the next frame's, and the next frame
; belongs to the worker - which cannot run while this callback holds the gfx
; lock. So a message set immediately before a long lock-held stretch would
; appear when the stretch ENDED, which is the one moment it is no useless.
; -----------------------------------------------------------------------------
trk_say:
    cmp byte [trk_tx], 0
    jne .tx
    jmp tui_msg_draw               ; windowed splash or the graphics FT2 frame
.tx:
    jmp ttx_status                 ; the XT text screen owns the adapter, so
                                    ; no kernel drawing slot may be used here

; -----------------------------------------------------------------------------
; trk_legend - the surface changed under a legend that was chosen FOR a
;              surface. Re-run trk_transport, but only if what is on the
;              status line IS one of its four strings: a real message ("Rate:
;              22 kHz", a refusal) belongs to the user and must not be eaten
;              by pressing F. Preserves everything.
trk_legend:
    push si
    mov si, [tui_msgp]
    cmp si, trk_s_stopd
    je .re
    cmp si, trk_s_paused
    je .re
    cmp si, trk_s_playing
    je .re
    cmp si, trk_s_stopdf
    je .re
    cmp si, trk_s_playingf
    jne .out
.re:
    call trk_transport
.out:
    pop si
    ret

trk_transport:
    push ax
    push si
    cmp byte [mp_playing], 0
    jne .playing
    mov al, [mp_row]
    mov [tui_vrow], al
    mov si, trk_s_paused            ; a PAUSE says so (45.21): Play resumes,
    cmp byte [trk_pause], 0         ; where a stop starts the song again
    jne .msg
    mov si, trk_s_stopd             ; ...and the FULLSCREEN twin of each, which
    cmp byte [trk_fs], 0            ; is where the legend is actually read from
    je .msg                         ; most of the time: [tui_msgp] is non-zero
    mov si, trk_s_stopdf            ; from the first transport change on, so
    jmp short .msg                  ; tui_s_hint is the FIRST paint's legend and
.playing:                           ; these two are every one after it. L is off
    mov si, trk_s_playing           ; both fullscreen twins (SPEC.md 45.17)
    cmp byte [trk_fs], 0
    je .msg
    mov si, trk_s_playingf
.msg:
    call tui_msg
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_rate_set - pick the Rate menu's sample rate (AL = 0/1/2 = 11/22/44
; kHz; SPEC.md 45.10). UI context, lock held (key R or the Rate menu).
; Playing stops first, like the XT toggle; the pick lands at the next Play.
; The active item becomes its own MENU_DIS twin (the radio idiom) and
; MENU_SET re-runs - the kernel holds a COPY of the set (SPEC.md 12.2).
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; trk_menus_build - compose every item this set owns and install it ONCE
;                 kernel (SPEC.md 45.17.1). Preserves every register.
;
; TWO MARKS THAT MEAN TWO THINGS, which is the whole point of composing these
; rather than pointing at fixed strings. The active pick used to BE a MENU_DIS
; twin - the radio idiom - and MENU_DIS is SPEC.md 47's "you cannot have this",
; so the field read `11 kHz` greyed and reported the rate menu as disabled. It
; was selected. So the selection is a '*' in the text, and greying goes back to
; meaning unavailable:
;
;   `* 11 kHz`          selected, live
;   `  22 kHz`          not selected, live
;   `  11 kHz (Xt)`     greyed, because XT MODE OVERRIDES THE RATE - trk_play
;                       takes TRK_RATE_XT whenever [mp_xt], so every pick here
;                       is inert until XT mode goes. That is a FACT and not a
;                       guess (rule 3), the whole control is greyed rather than
;                       the caption alone (rule 2), and the suffix says why
;                       (rule 7)
;
;
; 33 and 44 kHz are not rows at all on a card without SND_CAP_PCM_HI (DSP
; < 3.00, SPEC.md 45.10.1): trk_rcount's usable count IS the item count. They
; were live on every machine while no bit said so, and an SB 2.0 took the pick
; and refused it at Play - which a clobbered AX then reported as 'Sound open
; failed'.
; -----------------------------------------------------------------------------
trk_menus_build:
    pushf                           ; FLAGS, not just registers: the entry proc
    push ax                         ; calls this between `jc .out` and its own
    push bx                         ; ret, and the loader reads that CF
    push cx
    push si
    push di

    ; --- View > Fullscreen: greyed while the rate cannot use it -------------
    mov di, trk_viewitem
    call trk_fs_ok                  ; CF = 1: 11 kHz is picked, so the text
    jnc .vlive                      ; screen is refused (SPEC.md 45.9.3)
    mov byte [di], MENU_DIS         ; ...so do not OFFER it. A row that is
    inc di                          ; live and then says no is rule 4's
.vlive:                             ; "looks available and is not"
    mov si, trk_s_fullm
    call trk_scpy
    call trk_fs_ok
    jnc .vterm
    mov si, trk_s_fswin             ; rule 7: name what brings it back
    call trk_scpy
.vterm:
    mov byte [di], 0

    ; --- View > Text Screen: the surface pick (SPEC.md 45.13.7) -------------
    ; The Rate menu's idiom below, one for one: '*' in the TEXT means selected
    ; so that greying goes back to meaning unavailable, and the '(Xt)' suffix
    ; names the other control that makes this one inert (SPEC.md 47 rule 7).
    ; It is a FACT and not a guess (rule 3) - trk_txon's first term is already
    ; true, so every press here really is without effect until XT mode goes.
    ;
    ; No adapter test and no CPU test: FSXM_TEXT80 is bit 0 of all three
    ; capstab rows (SPEC.md 53.8) and the text screen is the CHEAPER surface
    ; on every machine (45.13.1), so either gate would be greying a guess.
    mov di, trk_textitem
    cmp byte [mp_xt], 0
    je .tlive
    mov byte [di], MENU_DIS
    inc di
.tlive:
    mov al, ' '
    call trk_txon                   ; the '*' shows the EFFECTIVE surface, so
    je .tmark                       ; in XT mode it is set whatever the pick
    mov al, '*'                     ; is - which is what the greying explains
.tmark:
    mov [di], al
    inc di
    mov byte [di], ' '
    inc di
    mov si, trk_s_textm
    call trk_scpy
    cmp byte [mp_xt], 0
    je .tterm
    mov si, trk_s_xsfx
    call trk_scpy
.tterm:
    mov byte [di], 0

    ; --- Rate: NO CARD, the speaker's (SPEC.md 45.25.3): Auto, then every
    ; rung it will step to on this machine, each with its predicted load
    ; once the bench has run ---
    call trk_spkq
    jnc .rcard
    call trk_spkrate_menu
    jmp .rdone
.rcard:
    ; --- Rate: XT mode's TWO, or the other mode's FOUR - or TWO -----------
    call trk_rcount                 ; CL = the rows this mode AND this card
    xor ch, ch                      ; can play: a card without SND_CAP_PCM_HI
                                    ; gets no 33/44 rows at all (SPEC.md
                                    ; 45.10.1). Not greyed - a sound card is
                                    ; not swapped without a reboot, so a row
                                    ; that can never be picked is only noise.
                                    ; The counts differ, so AMENU_NITEM is
                                    ; written rather than assembled
    mov [trk_e_rate + AMENU_NITEM], cx
    xor bx, bx                      ; BX = the item index
    mov di, trk_ritem0              ; ...and DI its item, a TRK_RITEM stride
.item:
    push di                         ; the item's start, for the stride below
    mov al, ' '
    call trk_rsel_get               ; AH = the live pick for THIS mode (it
    cmp bl, ah                      ; reads no BX)
    jne .pad
    mov al, '*'
.pad:
    mov [di], al
    inc di
    mov byte [di], ' '
    inc di
    push bx
    shl bx, 1
    mov si, [trk_rname + bx]
    cmp byte [mp_xt], 0
    je .name
    mov si, [trk_xrname + bx]
.name:
    call trk_scpy
    pop bx
    cmp byte [mp_xt], 0             ; the 11 kHz row - and ONLY in XT mode -
    je .term                        ; carries the trade in its own label
    cmp bl, 1
    jne .term
    mov si, trk_s_xwin
    call trk_scpy
.term:
    mov byte [di], 0
    pop di
    add di, TRK_RITEM
    inc bx
    cmp bx, cx
    jb .item

.rdone:
    mov bx, [trk_win]               ; ONE MENU_SET for the whole set, which is
    mov si, trk_menus               ; why this is one routine: two that each
    call OSAPI_MENU_SET             ; ended in it meant the last one called
    pop di                          ; decided what reached the bar
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; trk_fs_ok - may the app enter its fullscreen surface? CF = 1 if not.
;
; ONE predicate for the greying, the key's refusal and trk_fs_enter's belt
; (SPEC.md 47 rule 5). Preserves everything but the flags.
; -----------------------------------------------------------------------------
trk_fs_ok:
    cmp byte [mp_xt], 0             ; XT mode's high rate cannot hold the
    je .ok                          ; 45.13 text screen (PERFORMANCE.md
    cmp byte [trk_xhi], 0           ; Sets 65-67), so it is not offered
    je .ok
    stc
    ret
.ok:
    clc
    ret

; -----------------------------------------------------------------------------
; trk_txon - is the fullscreen surface the SPEC.md 45.13 TEXT screen?
;            out: ZF=0 (jne) yes, ZF=1 (je) no. Preserves every register.
;
; ONE predicate, two readers (SPEC.md 45.13.7, 47 rule 5): trk_fsx_main's
; branch and trk_menus_build's '*'. So the surface the bracket actually takes
; and the surface the menu claims cannot come to differ.
;
; XT mode FORCES the surface and [trk_txw] ASKS for it, and NEITHER TERM READS
; THE OTHER - that is the point of the section rather than an implementation
; detail. Turning XT mode off leaves a picked surface exactly where the user
; put it, at 22 or 44 kHz; and a tier-0 machine, pre-armed into XT mode at
; entry (45.9), still lands on the text screen with [trk_txw] never written.
;
; Not called by the bracket's X and V guards, which ask [trk_tx] - "where the
; app already IS" - and SPEC.md 45.13.3 is why that is the right question
; there and this is the wrong one.
; -----------------------------------------------------------------------------
trk_txon:
    cmp byte [mp_xt], 0
    jne .yes                        ; ZF=0 already: XT mode forces it
    cmp byte [trk_txw], 0           ; ...otherwise the pick IS the answer
.yes:
    ret

; -----------------------------------------------------------------------------
; trk_txt_toggle - flip the fullscreen surface pick (SPEC.md 45.13.7). UI
; context, lock held (key V or View > Text Screen). Preserves every register.
;
; NO playback stop, NO stream close and NO table rebuild, which is what makes
; this a different shape from trk_xt_toggle sitting next to it: the pick
; decides which bracket F enters and NOTHING the mixer can see, so there is
; no rate to fix at stream-open time and the music does not even pause.
;
; It moves the byte even while XT mode forces the surface and the row is
; MENU_DIS. The pick is the user's; XT mode is only standing on top of it, and
; a toggle that silently refused would leave the '*' disagreeing with what the
; user just pressed the moment XT mode came off.
;
; The repaint is the menu and nothing else. The windowed splash carries no
; pixel that depends on this - its hint line is trk_fs_ok's business (45.9.3),
; which [trk_txw] does not move - so tui_draw_all here would be PERFORMANCE.md
; rule 1's full repaint bought for identical pixels.
; -----------------------------------------------------------------------------
trk_txt_toggle:
    push ax
    push si
    push di                         ; tui_msg_draw preserves AX/BX/CX/DX/SI and
                                    ; NOT DI, and this is a public routine
    mov al, [trk_txw]
    xor al, 1
    mov [trk_txw], al
    call trk_menus_build            ; the '*' moves - one builder, one
                                    ; MENU_SET (SPEC.md 45.17.1)
    mov si, trk_s_txmg
    cmp byte [trk_txw], 0
    je .say
    mov si, trk_s_txmt
.say:
    call tui_msg
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; trk_rsel_get / trk_rsel_put - the rate pick for whichever mode is on
;
; Two bytes rather than one, so each mode REMEMBERS its own choice across a
; toggle: [trk_rsel] is the Rate menu's 11/22/44 and [trk_xhi] is XT mode's
; 5.5/11. out AH = the pick; put takes it in AL.
; -----------------------------------------------------------------------------
trk_rsel_get:
    call trk_spkq                   ; no card: the speaker's pick (45.25.3)
    jnc .card
    mov ah, [tsp_rsel]
    ret
.card:
    mov ah, [trk_rsel]
    cmp byte [mp_xt], 0
    je .out
    mov ah, [trk_xhi]
.out:
    ret

trk_rsel_put:
    call trk_spkq
    jnc .card
    mov [tsp_rsel], al
    ret
.card:
    cmp byte [mp_xt], 0
    je .plain
    mov [trk_xhi], al
    ret
.plain:
    mov [trk_rsel], al
    ret

; -----------------------------------------------------------------------------
; trk_rcount - how many Rate rows are USABLE in this mode. out CL.
;
; XT mode's two, or the other mode's four - but only the first TWO (11/22) on a
; card without SND_CAP_PCM_HI, since 33 and 44 kHz need DSP >= 3.00 (SPEC.md
; 45.10.1). The Rate menu's item count, R, the rate button and trk_rate_set's
; bound are all this one answer (SPEC.md 47 rule 5).
; -----------------------------------------------------------------------------
trk_rcount:
    call trk_spkq                   ; no card: Auto and the machine's rungs
    jnc .card
    push si
    call tsp_rates
    mov cl, 1
.sr:
    cmp word [si], 0
    je .sre
    inc cx
    add si, 2
    jmp short .sr
.sre:
    pop si
    ret
.card:
    mov cl, 2
    cmp byte [mp_xt], 0
    jne .out
    call trk_hirate
    jnc .out
    mov cl, 4
.out:
    ret

; trk_hirate - CF = 1 the sound driver takes rates above 22,222 Hz (SND_CAP_
; PCM_HI: an SB Pro or SB16). Asked LIVE, not latched: a driver can be mounted
; or its DSP tier switched off mid-session. Preserves every register.
trk_hirate:
    push ax
    push bx
    push dx
    call OSAPI_SND_CAPS             ; AX = caps (BL/DX: the route, unused)
    test ax, SND_CAP_PCM_HI
    pop dx
    pop bx
    pop ax
    jz .no
    stc
    ret
.no:
    clc
    ret

; trk_spkq - CF = 1 when there is NO CARD: Tracker plays through the PC speaker
; and picks the speaker's rate itself (SPEC.md 45.25), so the Rate menu, the
; rate button and R have nothing to set. Asked LIVE, as trk_hirate is.
; Preserves every register.
trk_spkq:
    push ax
    push bx
    push dx
    call OSAPI_SND_CAPS
    test ax, SND_CAP_PCM_BG
    pop dx
    pop bx
    pop ax
    jnz .card
    stc
    ret
.card:
    clc
    ret

; -----------------------------------------------------------------------------
; trk_rstop - a rate change's stop, the card's and the speaker's: a playing
; song paused where the listener is, with the renderer told the stop has
; been SEEN - or its next frame notices it and repaints `Paused` over the
; rate's message (SPEC.md 45.10.1). Nothing when stopped. And then a drained
; ring left open by the F00/stop paths closes, before the rate or the mode
; flips (the speaker never has one open). XT mode's toggle is the same stop.
; Clobbers AL (trk_play_stop), which no caller reads after it
; -----------------------------------------------------------------------------
trk_rstop:
    cmp byte [mp_playing], 0
    je .idle
    call trk_play_stop              ; drains the worker (SPEC.md 45.2)
    call trk_transport              ; ...and the stop's legend and parked row
    cmp byte [trk_fs], 0            ; NOW, with the renderer told it has been
    je .seen                        ; seen - or its next frame notices the stop
    cmp byte [trk_cpu0], 0          ; and repaints `Paused` over the rate
    jne .idle                       ; message below, which is where 44 kHz's
.seen:                              ; note lives (SPEC.md 45.10.1). A tier-0
    mov byte [tui_lplay], 0         ; FULLSCREEN stop also reshapes the pattern
.idle:                              ; view, so there the renderer keeps it
    cmp byte [trk_sopen], 0
    je .shut
    call trk_stream_close
.shut:
    ret

; -----------------------------------------------------------------------------
; trk_spkrate_menu - the Rate menu with NO CARD (SPEC.md 45.25.3): Auto, then
; each rung the speaker will step to on this machine, '*' on the pick and,
; once the bench has run, each rung's predicted load - so the user picks a
; rate knowing what it asks of the machine. Called by trk_menus_build, which
; banks AX, BX, CX, SI and DI around it: this keeps ES alone.
; -----------------------------------------------------------------------------
trk_spkrate_menu:
    push es
    push ds
    pop es                          ; tsp_mnum writes at ES:DI
    cld
    call trk_rcount                 ; CL = Auto + the rungs
    xor ch, ch
    mov [trk_e_rate + AMENU_NITEM], cx
    xor bx, bx
    mov di, trk_ritem0
.item:
    push di
    mov al, ' '
    cmp bl, [tsp_rsel]
    jne .p
    mov al, '*'
.p:
    stosb
    mov al, ' '
    stosb
    mov si, tsp_s_rauto
    or bx, bx
    jz .name
    call tsp_rates                  ; SI -> the rungs
    push bx
    dec bx
    shl bx, 1
    mov ax, [si+bx]
    pop bx
    call tsp_rstr                   ; SI = its name
    call trk_scpy
    cmp byte [tsp_cal], 0           ; benched: the load it would put on the
    je .term                        ; machine
    call tsp_predict                ; AX = %
    push ax
    mov al, ' '
    stosb
    stosb
    pop ax
    call tsp_mnum
    mov al, '%'
    stosb
    jmp short .term
.name:
    call trk_scpy
.term:
    mov byte [di], 0
    pop di
    add di, TRK_RITEM
    inc bx
    cmp bx, cx
    jb .item
    pop es
    ret

; trk_spkrate_say - `Rate: 5512 Hz at the next Play` (or Auto). Keeps ES; trk_rate_set
; banks the rest
trk_spkrate_say:
    push es
    mov si, tsp_s_rauto
    mov bl, [tsp_rsel]
    or bl, bl
    jz .n
    call tsp_rates
    dec bl
    xor bh, bh
    shl bx, 1
    mov ax, [si+bx]
    call tsp_rstr
.n:
    mov bx, trk_s_rspkp
    call trk_rcomp
    call tsp_mend
    pop es
    ret

; DS:SI (asciiz, terminator dropped) -> [DI], DI left past it. AL, SI spent.
; trk_rcomp - `Rate: ` + SI + BX, composed at tsp_msg (the speaker's own
; message buffer and composer, SPEC.md 45.25.3) and NOT terminated: DI is
; left at the end for the caller's NUL. ES = DS on the way out; clobbers SI
trk_rcomp:
    call tsp_mbeg
    push si
    mov si, trk_s_rspkm
    call tsp_mcat
    pop si
    call tsp_mcat
    mov si, bx
    jmp tsp_mcat

trk_scpy:
    mov al, [si]
    inc si
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short trk_scpy
.out:
    ret

trk_rate_set:
    push ax
    push bx
    push cx
    push si
    push di
    call trk_spkq                   ; NO CARD: the speaker's rate (45.25.3):
    jnc .card                       ; Auto or a rung, taken at the next play
    call trk_rcount
    cmp al, cl
    jae .done
    mov [tsp_rsel], al
    call trk_rstop                  ; PLAYING: paused first, as a card's
                                    ; change is, so the face says what R did
                                    ; and Play takes the new rate from where
                                    ; the listener is
    mov byte [tsp_rung], 0          ; Auto starts from the top again
    mov byte [tsp_force], 0
    call trk_menus_build
    call trk_spkrate_say
    jmp .done
.card:
    call trk_rcount                 ; CL = rows in THIS mode: 3 outside XT
    cmp al, cl                      ; mode, 2 inside it. A pick past the end
    jae .done                       ; cannot happen from the menu and can from
                                    ; a future key, so it is refused here
    cmp byte [trk_tx], 0            ; the SPEC.md 45.13 text surface: a rate
    je .ok                          ; change needs the stream reopened, which
    mov si, trk_s_txxt              ; is windowed-only - the same 'windowed
    call tui_msg                    ; first' the X key says, and for the same
    jmp short .done                 ; reason
.ok:
    push ax
    call trk_rsel_get               ; AH = the live pick for this mode
    cmp al, ah
    pop ax
    je .done                        ; same pick: nothing to do at all
    call trk_rsel_put
%ifdef TRKLOG
    cmp byte [mp_xt], 0             ; the bench sweep OUTRANKS trk_xhi in
    je .idle0                       ; trk_play, so it is moved to agree with
    call tlog_xrate_set             ; it - otherwise R relabels the menu while
.idle0:                             ; K's rate keeps playing, and a bench
%endif                              ; build that lies about its own state is
                                    ; the one thing a bench build may not do.
                                    ; It lived in trk_xrate_tog until that
                                    ; routine folded into this one
    call trk_rstop                  ; ...and a drained ring closes
    call trk_menus_build            ; the '*' moves, and in XT mode View >
                                    ; Fullscreen greys or un-greys with it -
                                    ; ONE builder, so they cannot disagree
    cmp byte [mp_xt], 0             ; ...and the SPLASH carries the same fact
    je .msg                         ; in its hint line, which is pixels rather
    call tui_draw_all               ; than a menu string and so does not
                                    ; follow a rebuild. Outside XT mode
                                    ; nothing on the splash is rate-dependent,
                                    ; so that path repaints exactly as little
                                    ; as it did before
.msg:
    call trk_rsel_get
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov si, [trk_rname + bx]        ; `Rate: <the pick> <what it costs>`,
    mov ax, [trk_rsfx + bx]         ; composed (trk_rcomp)
    cmp byte [mp_xt], 0
    je .say
    mov si, [trk_xrname + bx]
    mov ax, [trk_xrsfx + bx]
.say:
    xchg ax, bx                     ; BX = the suffix
    push es
    call trk_rcomp
    mov byte [di], 0
    mov si, tsp_msg
    call tui_msg                    ; ONE draw (tsp_mend would add trk_say's)
    pop es
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
    call trk_rstop                  ; a rate change's stop and close
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
    mov [trk_mi_file + TRK_MI_XT], si
    call trk_menus_build            ; the Rate menu becomes XT mode's TWO rows
                                    ; or the other mode's three, and View >
                                    ; Fullscreen greys with them (SPEC.md
                                    ; 45.9.3) - one builder, one MENU_SET
    mov si, trk_s_xtmoff
    cmp byte [mp_xt], 0
    je .msg
    mov si, trk_s_xtmon
.msg:
    cmp byte [trk_fs], 0            ; in the bracket the whole frame follows
    jne .card                       ; the mode - the pattern view's SHAPE does
    cmp byte [trk_xhi], 0           ; (SPEC.md 45.9.1). Windowed, the card's
    je .say                         ; only mode-dependent pixel is the hint,
.card:                              ; and trk_fs_ok moves with mp_xt ONLY when
    mov [tui_msgp], si              ; the high rate is picked - at 5.5 kHz the
    inc byte [tw_gen]               ; (tui_msg's bump, for the direct store)
    call tui_draw_all               ; repaint would be identical pixels. Set
    jmp short .out                  ; the message first and the card letters
.say:                               ; that line once (PERFORMANCE.md rule 2)
    call tui_msg
.out:
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
TRK_PIECE   equ 256                 ; a feed piece while the ring is deep
                                    ; (SPEC.md 45.16.7): ~25 ms of 8088
TRK_LOW     equ 2 * TRK_HALF        ; ...and under THIS lead, whole halves at
                                    ; once: 745 ms of cushion at the XT rate
                                    ; against ~200 ms to mix a half. At
                                    ; TRK_DEEP the ring dipped there every few
                                    ; seconds in steady state, and each dip was
                                    ; a 150-185 ms frame gap for nothing

trk_worker:
    mov byte [trk_inrend], 0        ; THE RESTART LANDS HERE (SPEC.md 66.6.2)
                                    ; and this is the one byte it can leave
                                    ; stuck. Tracker is PARK-SAFE, so the
                                    ; kernel may park this worker blocked in
                                    ; OSAPI_GFX_LOCK as well as at
                                    ; OSAPI_TASK_ALIVE - and the only lock it
                                    ; ever blocks on is trk_render's, taken
                                    ; with this flag already 1 and nothing
                                    ; drawn yet. Restarted there the frame is
                                    ; lost, which is what a restart costs, and
                                    ; the flag would stay set for ever with
                                    ; trk_fs_enter's drain waiting on a worker
                                    ; that is no longer inside trk_render.
                                    ; [trk_mixing] needs no such line: trk_feed
                                    ; blocks on nothing, so neither park point
                                    ; is ever inside a feed pass
.loop:
%ifdef TRKLOG
    call tlog_wake                  ; WK: how often this got the CPU, which is
%endif                              ; what tells a STARVED worker from an idle
                                    ; one - FD reads 0 for both
    mov bx, [trk_win]
    call OSAPI_TASK_ALIVE           ; lock NOT held here (rule 4)
    call OSAPI_GET_TICKS            ; A TICK WENT BY during the last pass (the
    cmp ax, [trk_wtick]             ; feed mixes up to the edge, SPEC.md
    jne .late                       ; 45.16.7): its frame is owed NOW - a
    mov ax, 1                       ; sleep here would wait out a whole
    call OSAPI_TASK_SLEEP           ; second tick and draw every other one.
    call OSAPI_GET_TICKS            ; Otherwise ~18 wakes a second, one frame
.late:                              ; a tick
    mov [trk_wtick], ax             ; ...and this is the tick the pass is for
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
                                    ; UI-side close - mp_stop already ran
                                    ; (the effect itself), so only the
                                    ; latch is left. But first the TAIL
                                    ; (SPEC.md 45.22.1): the half the pieces
    xor bx, bx                      ; had begun, and the driver's own block.
    cmp word [trk_mixed], 0         ; BX = something staged this pass
    je .pad
    push dx                         ; (mp_gen clobbers freely: mp_* rule)
    mov cx, TRK_HALF                ; finish the half in progress: mp_genc
    sub cx, [trk_mixed]             ; pads 80h now the replayer has stopped,
    call mp_genc                    ; and its room was checked when it began
    call trk_stage                  ; (consumed only grows)
    pop dx
    mov bx, 1
.pad:
    cmp word [mp_mixrate], 22222    ; above 22,222 Hz the driver's block is
    jbe .sfeed                      ; 4KB (SPEC.md 34.5), and a total at an
    test word [trk_total], TRK_HALF ; ODD 2KB never drains: the card stops
    jz .sfeed                       ; a block short of it. One more half of
    mov ax, [trk_total]             ; silence, when the ring has room - and
    sub ax, dx                      ; when it has not, the bit is still set
    mov cx, [trk_ring]              ; and the next pass asks again
    sub cx, TRK_HALF
    cmp ax, cx
    ja .sfeed
    push dx
    mov cx, TRK_HALF
    call mp_gen                     ; mp_playing = 0: pure silence
    call trk_stage
    pop dx
    mov bx, 1
.sfeed:
    or bx, bx
    jz .sdrn
    mov cx, [trk_total]             ; verb 1: feed what was staged
    mov al, 1
    mov ah, [trk_hand]
    push dx
    call OSAPI_SND_STREAM
    pop dx
    jmp .out                        ; the drain is the next pass's question
.sdrn:
    cmp dx, [trk_total]
    jne .out
    mov byte [trk_ended], 1
    call trk_wake                   ; ...and the UI task is TOLD (45.22.1)
    jmp .out
.go:
    mov byte [trk_halves], 0
    mov byte [trk_whole], 0         ; THE XT AT 11 kHz MIXES WHOLE HALVES
    cmp byte [mp_xt], 0             ; (SPEC.md 45.16.7.1). There a half is
    je .fill                        ; 186 ms of music and nearly as much 8088,
    cmp word [mp_mixrate], 11000    ; so the pieces' frame a tick is paid for
    jb .fill                        ; out of the cushion: a busy passage
    mov byte [trk_whole], 1         ; drains it and it never refills
.fill:
    cmp byte [trk_sopen], 0         ; a UI close mid-pass ends the burst
    je .out                         ; (bounds trk_stream_close's drain wait)
    cmp byte [mp_playing], 0        ; F00 mid-burst stops the mixer
    je .out
    cmp byte [trk_halves], TRK_MAXFEED
    jae .out
    mov ax, [trk_total]
    sub ax, dx                      ; AX = lead, 0..[trk_ring]
    mov bx, [trk_ring]              ; the feed ceiling is the ring less one
    sub bx, TRK_HALF                ; half, and the ring is chosen now
    cmp ax, bx                      ; (SPEC.md 45.18)
    ja .out                         ; no room for a whole half
    ; --- A HALF IN PIECES (SPEC.md 45.16.7) ---------------------------------
    ; The card takes whole halves and nothing said they had to be MIXED
    ; whole. One was, per pass, and at XT mode's rate a half is 372 ms of
    ; music and ~200 ms of 8088 - the worker drew nothing for that long, so
    ; the windowed meters ran at 55 ms, 55, 55, 275 (measured). Now a pass
    ; mixes TRK_PIECE at a time into mp_outbuf where the last piece stopped,
    ; and stages and feeds the half when it is whole - and PIECES GO UNTIL
    ; THE TICK EDGE rather than to a byte count: a count that overran the
    ; tick made the worker sleep through the next one and draw every other
    ; (measured, 110 ms median), where stopping at the edge spends exactly
    ; the time the frame left. A ring that has run LOW (under TRK_LOW)
    ; finishes the half at once, so the cushion is never traded for a frame.
    mov cx, TRK_HALF
    sub cx, [trk_mixed]             ; CX = what the half still needs
    cmp byte [trk_whole], 0
    jne .piece                      ; the XT at 11 kHz: all of it, always
    cmp ax, TRK_LOW
    jb .piece                       ; low: all of it, now
    cmp cx, TRK_PIECE
    jbe .piece
    mov cx, TRK_PIECE
.piece:
    push dx
    push cx                         ; mp_gen clobbers freely (mp_* rule)
    cmp word [trk_mixed], 0
    jne .more
    mov ax, [trk_total]             ; the half's first piece: where
    mov [mp_stampbase], ax          ; mp_outbuf[0] lands (SPEC.md 45.15)
    call mp_gen
    jmp short .mixed
.more:
    call mp_genc
.mixed:
    pop cx
    add [trk_mixed], cx
    cmp word [trk_mixed], TRK_HALF
    jb .part
    call trk_stage                  ; whole: stage + total += 2048
    mov cx, [trk_total]             ; verb 1: feed - new total valid length
    mov al, 1
    mov ah, [trk_hand]
    call OSAPI_SND_STREAM
    pop dx
    or ax, ax
    jnz .out                        ; refused feed: try again next wake
    inc byte [trk_halves]
    jmp short .next
.part:
    pop dx
.next:
    cmp byte [trk_whole], 0         ; ...and it fills the ring before it
    jne .fill                       ; draws, which is what it always did
    mov ax, [trk_total]             ; low: go round and fill regardless
    sub ax, dx
    cmp ax, TRK_LOW
    jb .fill
    call OSAPI_GET_TICKS            ; deep: pieces while THIS tick lasts, and
    cmp ax, [trk_wtick]             ; the next one's frame goes first
    je .fill
    jmp .out
.dead:
    call mp_stop                    ; watchdog-ended streams never resume
    mov byte [trk_ended], 1         ; (SPEC.md 34.5); trk_reap or the next
    call trk_wake                   ; Play closes it on the UI task
.out:
%ifdef TRKLOG
    call tlog_wend                  ; ...and the span closes here
%endif
    mov byte [trk_mixing], 0        ; the drain gate reopens LAST
    ret

; trk_wake - post our window a wake, from the worker (OSAPI_WM_WAKE is
; worker-safe, and at most one is ever queued). preserves all
trk_wake:
    push bx
    mov bx, [trk_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; -----------------------------------------------------------------------------
; trk_render - the worker's one lock hold (SPEC.md 20.6 rules 3/5): geometry
;              re-check, clip armed, [trk_abon] honoured under the lock so
;              the About panel is never painted over, then the dynamic-only
;              redraw.
; out: nothing; clobbers AX..DI (the worker keeps nothing across it)
; -----------------------------------------------------------------------------
trk_render:
    call tw_want                    ; a frame that would draw nothing does not
    jnc .none                       ; take the lock either (tw_want)
    call OSAPI_GFX_LOCK
    call trk_frame
    call OSAPI_GFX_UNLOCK           ; also clears the clip
.none:
    ret

; -----------------------------------------------------------------------------
; trk_frame - the frame itself, with the gfx lock HELD: the worker's
;             (trk_render) and the speaker bracket's (tsp_frame). Clobbers
;             AX..DI: neither caller keeps anything across it
; -----------------------------------------------------------------------------
trk_frame:
    mov bx, [trk_win]
    call OSAPI_WM_CLIP_SET          ; CF=1: hidden or fully covered - skip the
    jc .book                        ; frame. The region asks W_FLAGS itself
                                    ; (SPEC.md 11.3.1), so a WM_GEOM ahead of
                                    ; it was a second far call a frame for
                                    ; the same answer
    cmp byte [trk_abon], 0          ; checked HERE, under the lock, after the
    jne .book                       ; clip: the [ark_abon] rule verbatim
    mov byte [tw_inframe], 1        ; the frame's own clip is armed: a message
    call tui_draw_dyn               ; set inside it is drawn BY it, and nothing
    mov byte [tw_inframe], 0        ; may clear the clip (tw_refresh)
    jmp short .out
.book:                              ; ...a frame not drawn is still BOOKED:
    call tw_book                    ; the position, the decay, the clock
.out:                               ; (SPEC.md 45.21.9)
    ret

; =============================================================================
; Data
; =============================================================================

; --- window template (SPEC.md 11: 16 bytes, 8 words); x/y set by trk_entry ----
trk_tpl:
    dw 0, 0, TRK_WINW, TRK_WINH
    dw trk_ttl, trk_paint, trk_onkey, tw_clickw

; --- the frame per adapter (SPEC.md 11.100.1): VGA, Hercules, CGA ------------
    OS88_PREFER trk_pref, TRK_WINW, TRK_WINH,  TRK_WINW, TRK_WINH, \
                          TRK_WINW, TW_HCOMP + TITLE_H + 2

; --- app menu set (SPEC.md 12.2) -----------------------------------------------
    OS88_MENUSET trk_menus, trk_m_name, trk_oncmd
        OS88_MENU trk_m_file, trk_mi_file, 4
trk_e_view:
        OS88_MENU trk_m_view, trk_mi_view, 2    ; Fullscreen + Text Screen
                                        ; (SPEC.md 45.13.7). A fixed count,
                                        ; unlike trk_e_rate's below: both
                                        ; rows exist in every mode and it is
                                        ; the GREYING that moves
trk_e_rate:                             ; ...labelled because its AMENU_NITEM
        OS88_MENU trk_m_rate, trk_mi_rate, 5    ; is written at RUN TIME: the
                                        ; Rate menu is TWO items in XT mode
                                        ; and four outside it (SPEC.md
                                        ; 45.9.3). The set is the package's
                                        ; own image and the image is writable,
                                        ; which trk_xt_toggle's relabel has
                                        ; always relied on
    OS88_MENUSET_END trk_menus

trk_m_name:  db 'Tracker', 0
trk_m_file:  db 'File', 0
trk_mi_file: dw trk_s_open, trk_s_addl, trk_s_edit, trk_s_xtoff
TRK_MI_XT   equ 3 * 2                   ; item 3 repointed by trk_xt_toggle
                                        ; (the sol_dealmenu relabel idiom -
                                        ; invisible until MENU_SET re-runs)
trk_s_open:  db 'Open...', 0
trk_s_addl:  db 'Add to PlayList...', 0
trk_s_edit:  db 'PlayList Editor', 0
trk_s_xtoff: db 'XT Mode: Off', 0
trk_s_xton:  db 'XT Mode: On', 0
trk_s_fsxhi: db '11 kHz is windowed: R picks 5.5', 0
trk_m_view:  db 'View', 0
trk_mi_view: dw trk_viewitem, trk_textitem  ; AMENU_ITEMS is an ARRAY of pointers,
                                        ; never the string itself - pointed at
                                        ; the composed buffer directly, the bar
                                        ; reads its first two characters as a
                                        ; pointer and draws whatever they name
trk_s_fullm: db 'Fullscreen', 0
trk_s_textm: db 'Text Screen', 0        ; SPEC.md 45.13.7's surface pick
trk_s_xsfx:  db ' (Xt)', 0              ; ...greyed with the Rate menu's own
                                        ; suffix, because it is the same fact:
                                        ; XT mode overrides the row below it
trk_s_txmt:  db 'Full screen: text', 0
trk_s_txmg:  db 'Full screen: graphics', 0

; TRK_TITEM against the strings it was sized from, rather than a second
; opinion about them (the TRK_RITEM lesson: two constants that disagreed put
; `* 5.5 kH  11 kHz` on the bar). Worst case is the greyed row - MENU_DIS +
; '* ' + the caption + the suffix + NUL - and it fits EXACTLY, so a caption
; edit that does not move the constant fails the build instead of running off
; the end of the buffer into trk_viewitem.
%if 1 + 2 + (trk_s_xsfx - trk_s_textm - 1) + (trk_s_txmt - trk_s_xsfx - 1) + 1 > TRK_TITEM
  %error "the composed View > Text Screen row no longer fits TRK_TITEM"
%endif
trk_s_fswin: db ' (5.5 kHz)', 0         ; SPEC.md 47 rule 7: the greyed row
                                        ; says what would bring it back, which
                                        ; is the OTHER control's setting
trk_m_rate:  db 'Rate', 0
trk_mi_rate: dw trk_ritem0, trk_ritem1, trk_ritem2, trk_ritem3, trk_ritem4
                                        ; COMPOSED, by
                                        ; trk_rate_menu (SPEC.md 45.17.1)
trk_s_r11 equ tw_s_voff + 13        ; '11 kHz', the tail of tw_s_voff's bytes
trk_s_r22:   db '22 kHz', 0
trk_s_r33:   db '33 kHz', 0
trk_s_r44:   db '44 kHz', 0
trk_rname:   dw trk_s_r11, trk_s_r22, trk_s_r33, trk_s_r44
trk_rates:   dw TRK_RATE, TRK_RATE22, TRK_RATE33, TRK_RATE44
trk_rsfx:    dw trk_s_play, trk_s_play, trk_s_play, trk_s_smooth
                                      ; each pick's message is `Rate: ` +
                                      ; its name + this (trk_rcomp)
trk_s_play equ trk_s_xtmon + 10     ; ' - Enter plays', the tail of trk_s_xtmon's bytes
trk_s_smooth: db ' - smooth at 16 MHz+  Enter plays', 0
                                      ; SPEC.md 45.10.1: a 12 MHz 286 cannot
                                      ; MIX it, a 16 MHz one can - a fact about
                                      ; the CPU that the tier cannot tell apart
; ...and XT mode's own two, which REPLACE the three above while it is on
; rather than greying beside them (SPEC.md 45.9.3): 22 and 44 kHz are not
; choices a tier-0 machine can make, so a menu that lists them is a menu
; three fifths of which is unreachable. `(Windowed)` is rule 7 again - the
; trade is the whole feature, so the row that carries it says so.
trk_s_x55:   db '5.5 kHz', 0
trk_s_xwin:  db ' (Windowed)', 0
trk_xrname:  dw trk_s_x55, trk_s_r11
trk_xrsfx:   dw trk_s_play, trk_s_xwo
trk_s_xwo:   db ' - windowed only', 0
trk_s_rspkm: db 'Rate: ', 0             ; ...and the speaker's, 45.25.3
trk_s_rspkp: db ' at the next Play', 0

%if TRK_VSH == 1
trk_ttl:     db 'Tracker 33', 0     ; the listening builds say which they are
%elif TRK_VSH == 0
trk_ttl:     db 'Tracker 65', 0
%else
trk_ttl equ trk_m_name             ; 'Tracker', trk_m_name's own bytes
%endif

; --- status-line strings -------------------------------------------------------
trk_s_stopd:  db 'Stopped  ENTER play  HOME top  L load', 0
trk_s_playing: db 'Playing  SPACE pause  HOME top  L load', 0
trk_s_paused: db 'Paused  ENTER resumes', 0 ; short enough for either surface
; The fullscreen twins are SHORTER because that field is: TL_STW is 284px on
; the compact (CGA) layout = 35 cells, against the windowed splash's 52. The
; first version was 45 and truncated to `... HOME top  L lo`, which is how a
; legend ends up advertising a key it was written to stop advertising.
trk_s_stopdf: db 'Stopped  ENTER play  F/ESC exits', 0
trk_s_playingf: db 'Playing  SPACE pause  F/ESC exits', 0
trk_s_fsload: db 'Load is windowed: F or Esc first', 0
trk_s_notmod: db 'Not a .MOD file', 0
trk_s_nofit:  db 'Too big for free memory', 0
trk_s_cpq:    db 'Making room...', 0
trk_s_noload: db 'No module loaded - L loads one', 0
trk_s_nosb:   db 'No Sound Blaster: viewer only', 0
; A STRING THAT NAMES THE SPEAKER has its COVOX twin's address in the word
; just before it, which is where tsp_lptsi looks (SPEC.md 45.25.5)
              dw trk_s_cvx1
trk_s_spk1:   db 'Speaker: needs ', 0
trk_s_spk2:   db '% of this PC - Play again to try', 0
              dw trk_s_cvx3
trk_s_spk3:   db 'Spk ', 0         ; (short: the compact face's strip holds
                                   ; the legend's 38 and no more)
trk_s_spkflt: db 'Filtering the samples for the speaker...', 0
trk_s_spk4:   db ' Hz, ', 0
trk_s_spk5:   db '% cpu', 0
             dw trk_s_cvxl
trk_s_lev1:  db 'Spk level ', 0
trk_s_lev2:  db ' of 10 (+/-)', 0
trk_s_spk6:   db ' (CARRIER WHINES!)', 0
trk_s_spkbusy: db 'The PC speaker is busy', 0
trk_s_cvx1:   db 'Covox: needs ', 0
trk_s_cvx3:   db 'Covox ', 0
trk_s_cvxl:   db 'Covox level ', 0
trk_s_nomem:  db 'Out of memory', 0
trk_s_toobig: db 'File too big', 0
trk_s_noent:  db 'File not found', 0
trk_s_ioerr:  db 'Disk error', 0
trk_s_snderr: db 'Sound open failed', 0
trk_s_xtmon:  db 'XT mode on - Enter plays', 0
trk_s_xtmoff: db 'XT mode off - Enter plays', 0
trk_s_norate: db 'That rate needs an SB Pro or SB16', 0
trk_s_buffer: db 'Buffering...', 0
trk_s_txxt:   db 'Windowed only: Esc first', 0
                                        ; THREE keys share this and sharing it
                                        ; is what made it accurate (SPEC.md
                                        ; 45.13.3). It read 'XT off is
                                        ; windowed', which was already wrong
                                        ; for the R key that shared it - a
                                        ; rate change is not an XT toggle -
                                        ; and 45.13.7 would have made it wrong
                                        ; for X too, since a picked surface
                                        ; can hold the bracket with [mp_xt]
                                        ; clear and X then means ON. True for
                                        ; X, R and V now, in both directions,
                                        ; and five bytes shorter than the
                                        ; sentence that was true for one

; =============================================================================
; The rest of the package: the replayer, and the two renderers of one screen -
; trkui.inc in pixels, trktxt.inc in character cells (SPEC.md 45.13, which is
; what XT mode's fullscreen is)
; =============================================================================
%include "trkplay.inc"

; -----------------------------------------------------------------------------
; trk_reloc - THE HEAP COMPACTOR MOVED THE MODULE (SPEC.md 66.2/45)
; in:  BX = the base segment it WAS at, DX = the base it is at NOW.
;      DS = CS = ours, ES = KERNEL_SEG. The bytes have already moved.
; out: nothing; every register preserved
;
; A 116KB module is the largest single claim this OS ever hands out, and it is
; the reason SPEC.md 66's relocation is a CALLBACK rather than the kernel
; poking a word. [trk_modseg] is one word and rewriting it relocates NOTHING:
; the replayer never reads it again after load. What it reads is
;
;   [mp_blobseg]         trkplay's own copy of the base
;   mp_smptab[31].MS_SEG A NORMALIZED BASE SEGMENT PER SAMPLE - "each sample
;                        gets a base seg = blobseg + (start >> 4)", which is
;                        how a blob bigger than a segment is addressed at all
;   mp_chans[4].MP_SEG   ...and the segment of the sample each channel is
;                        PLAYING, which the mixer walks every chunk
;
; so thirty-six words, all of them blobseg plus a constant, all fixed by
; adding the delta.
;
; IT IS PLACED AFTER trkplay.inc AND NOT BESIDE ITS CALLERS, because MS_SZ,
; MP_CHSZ and the bss labels are that file's and this is the first point they
; all exist.
;
; WHY IT IS SAFE TO MOVE AT ALL, given a mixer that reads those words with no
; lock: the worker is PARKED (SPEC.md 66.5). It calls OSAPI_TASK_ALIVE at the
; top of every pass and sleeps a tick, so it reaches the park well inside
; INST_PARKW, and it parks BEFORE trk_feed and trk_render - the two things
; that touch any of this.
;
; It claims nothing, frees nothing, yields nothing and draws nothing (66.3
; rule 3), and it refuses to touch the replayer's tables unless they actually
; describe THIS buffer: between the claim and mp_load they still describe the
; last module, and shifting them then would invent addresses out of a blob
; that has been freed.
; -----------------------------------------------------------------------------
trk_reloc:
    cmp bx, [tw_scseg]              ; the scope's band (SPEC.md 45.21.5.1):
    jne .blob                       ; one word, read only under the gfx lock
    mov [tw_scseg], dx              ; by a worker that is parked whenever it
    ret                             ; can move
.blob:
    push ax
    push cx
    push si
    cmp bx, [trk_modseg]
    jne .out
    mov [trk_modseg], dx
    mov ax, dx
    sub ax, bx                  ; AX = the paragraph delta
    cmp bx, [mp_blobseg]
    jne .out                    ; the replayer is looking at something else
    add [mp_blobseg], ax
    mov si, mp_smptab
    mov cx, 31
.smp:
    add [si+MS_SEG], ax
    add si, MS_SZ
    loop .smp
    mov si, mp_chans
    mov cx, 4
.ch:
    cmp word [si+MP_SEG], 0     ; 0 = this channel is playing NOTHING, and it
    je .chnext                  ; is not a base to be adjusted - adding the
    add [si+MP_SEG], ax         ; delta to it invents an address out of
.chnext:                        ; nowhere. Found by tests/trackmove.py on a
    add si, MP_CHSZ             ; machine with no sound card, where all four
    loop .ch                    ; channels sit at 0 and every one of them came
                                ; out pointing outside the module
.out:
    pop si
    pop cx
    pop ax
    ret

%include "trkui.inc"
%include "trktxt.inc"
%include "trkwin.inc"
%include "trklist.inc"
%include "os88spk.inc"              ; the speaker's ring player (SPEC.md 34.11)
%include "os88spkfx.inc"            ; ...and its shaper (34.11.9)
%include "trkspk.inc"               ; ...played with no card (45.25)
%ifdef TRKLOG
%include "trklog.inc"               ; tests/ - the bench build only, and the
%endif                              ; only thing -DTRKLOG adds beyond hooks
%ifdef TRKDBG
%include "trkscrl.inc"              ; ...and the same shape for SPEC.md
%endif                              ; 45.12.2's scroll gate

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
    TRKB trk_hired                  ; the worker exists
    TRKB trk_addpl                  ; the open dialog is the PlayList's Add...
    TRKB trk_pause                  ; stopped WHERE THE LISTENER WAS: Play
                                    ; resumes (the face's Pause lamp)
    TRKB trk_rep                    ; Repeat: 0 Off, 1 Song, 2 List
    TRKB trk_shuf                   ; Shuffle
    TRKB trk_sover                  ; a song ended and its stream is closed:
                                    ; trk_song_over is owed
    TRKB trk_abon                   ; the About panel is up; worker frames drop
    TRKB trk_pmode                  ; 0 = song, 1 = pattern loop, 2 = resume
    TRKB trk_xhi                      ; XT mode's rate: 0 = 5,500 (the default,
                                      ; and bss arrives zeroed so it is the
                                      ; default by construction), 1 = 11,000
    TRKB trk_txw                      ; the FULLSCREEN SURFACE pick (SPEC.md
                                      ; 45.13.7): 0 = the 45.6 graphics
                                      ; screen, 1 = the 45.13 text one. bss
                                      ; arrives zeroed, so the default is the
                                      ; default by construction - trk_xhi's
                                      ; idiom just above - and NOTHING seeds
                                      ; or clears this byte. trk_xt_toggle in
                                      ; particular does not: a pick that XT
                                      ; mode reset would be the
                                      ; setting-that-undoes-itself 45.9.3
                                      ; refused to ship. XT mode FORCES the
                                      ; surface without owning it, which is
                                      ; the whole of trk_txon
    TRKBUF trk_textitem, TRK_TITEM    ; ...its composed row, greyed while XT
                                      ; mode forces the surface anyway
    TRKBUF trk_viewitem, TRK_VITEM    ; ...and the View item, which greys
    TRKBUF trk_ritem0, TRK_RITEM      ; the three composed Rate items
    TRKBUF trk_ritem1, TRK_RITEM      ; (SPEC.md 45.17.1) - in the PACKAGE's own
    TRKBUF trk_ritem2, TRK_RITEM      ; segment, which is where a menu string
    TRKBUF trk_ritem3, TRK_RITEM      ; (33 kHz made it four, SPEC.md 45.10.1)
    TRKBUF trk_ritem4, TRK_RITEM      ; (the speaker's Auto and a 286's four
                                      ; rungs made it five, 45.25.3)
                                    ; has to live (SPEC.md 12.2's MB_SEG)
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
    TRKB trk_cpq                    ; 1 = we posted OSAPI_MEM_COMPACT for
                                    ; the load in trk_fname and the wake owes
                                    ; us the retry (SPEC.md 66.4.3). It is one
                                    ; byte doing two jobs and they are the same
                                    ; job: it tells trk_onwake which wake this
                                    ; is, and it tells trk_cpq_try that this
                                    ; attempt has had its one question
    TRKBUF trk_argnm, 13            ; a module handed to us at launch (54.5)
    TRKBUF trk_argp, 1              ; ...1 = the first paint owes the load
    TRKBUF trk_argdrv, 1            ; ...and where it lives
    TRKBUF trk_argclus, 2
    TRKBUF trk_fname, 13            ; the chosen 8.3 name, copied out of the
                                    ; kernel's buffer during the completion call
    TRKBUF trk_find, OSAPI_FIND_SZ  ; trk_sizeof's directory record, for the
                                    ; loads that arrive with no size (45.3.1)

; --- the ring stream (SPEC.md 34.5 ring mode) ----------------------------------
    TRKB trk_ghave                  ; the pool grant exists
    TRKW trk_grant                  ; ...its SND_SEG offset
    TRKW trk_ring                   ; ...and how big it is: TRK_RING or
                                    ; TRK_RING_SM, chosen by trk_ring_set from
                                    ; the free RAM left after the module
                                    ; (SPEC.md 45.18). These three move
                                    ; together and NOTHING may set one alone -
                                    ; a mask that disagrees with its ring
                                    ; wraps the stage into the middle of the
                                    ; grant and plays the seam forever
    TRKW trk_rmask                  ; ...ring - 1, the stage's wrap
    TRKW trk_preroll                ; ...and the halves staged before the open
    TRKB trk_sopen                  ; a stream is open
    TRKB trk_hand                   ; ...its handle
    TRKB trk_ended                  ; watchdog/F00-ended: stop feeding, close
                                    ; on the next UI event (trk_reap)
    TRKW trk_total                  ; free-running total bytes mixed (mod 64K)
    TRKW trk_consumed               ; ...and what the CARD has played of it,
                                    ; polled by the worker once a tick and
                                    ; read by every frame (SPEC.md 45.15)
    TRKB trk_halves                 ; halves fed this wake (bounds the burst)
    TRKW trk_mixed                  ; bytes of the half in mp_outbuf so far
    TRKW trk_wtick                  ; the tick the worker's pass is for
    TRKB trk_whole                  ; this feed pass mixes whole halves and
                                    ; fills the ring (SPEC.md 45.16.7.1)
    TRKB trk_rsel                   ; the Rate menu's pick (SPEC.md 45.10):
                                    ; 0/1/2 = 11/22/44 kHz; bss zeroes to
                                    ; the 11 kHz default
    TRKB trk_mixing                 ; the worker is inside a trk_feed pass -
                                    ; trk_stream_close drains it before any
                                    ; UI-task touch of mp_* state or the blob

%include "os88alt.inc"              ; SPEC.md 11.2.1.1's edge, for the brackets
%define OS88UI_ABOUT                ; the standard About card (SPEC.md 20.5.1)
%define OS88UI_BIMG                 ; ...and buttons with PICTURES, drawn with
%define OS88UI_NOGLYPH              ; no pixel written twice (SPEC.md 13.8.9),
%include "os88ui.inc"               ; and no check box or radio at all

    OS88_BSS TRK_BSS
    OS88_IMAGE_END
