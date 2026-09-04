; =============================================================================
; os8088 - vga12.inc
;
; VGA mode 12h (640x480x16, planar) primitives, region save/restore, and the
; global drawing mutex. Everything here programs the Graphics Controller
; (index 0x3CE / data 0x3CF) and Sequencer (0x3C4/0x3C5) directly - both
; byte-identical on an EGA, which runs this same code in mode 10h at 350
; rows ([vid_mono] = 0, SPEC.md 39.24). The width and stride are 640/80 on
; both; only the two bottom-edge clips here read the live [vid_ch].
;
; Conventions (SPEC.md 1, 5, 7):
;   - Solid fills use Set/Reset (GC0 = color, GC1 = 0Fh) with the Bit Mask
;     (GC8) for partial edge bytes; a read of the target byte loads the
;     latches before every masked write.
;   - XOR ops use ALU function = XOR (GC3 = 18h) with Set/Reset color 0Fh,
;     and EVERY target byte is read immediately before it is written -
;     including full interior bytes - because the ALU XORs against the latch
;     contents. rep stosb is only used for the interior of replace-mode
;     fills, where the latches are irrelevant.
;   - Every routine clips to 0..639 / 0..479 and leaves the GC/Sequencer in
;     the default state on return: Set/Reset enable = 0, Bit Mask = FF,
;     function = replace, rotate 0, Map Mask = 0Fh, Read Map = 0, mode 0.
;   - Drawing routines run with the gfx lock held (SPEC.md 1.6), so the
;     module-private scratch variables below need no interrupt guard: the
;     mouse ISR never draws while the lock is set.
; =============================================================================

VGA_GC      equ 0x3CE           ; Graphics Controller index port (data +1)
VGA_SEQ     equ 0x3C4           ; Sequencer index port (data +1)

gfx_mono1       dw 0            ; THE WINDOW BEING DRAWN HAS DECLARED ITS
                                ; CONTENT TWO COLOURS (SPEC.md 11.96.17.1), so
                                ; font_ink takes its mono path here as well -
                                ; reducing the ink to 00h/FFh, which the glyph
                                ; loop turns into colour 0 and 15 across all
                                ; four planes, and stippling a disabled one
                                ; instead of leaving it solid CDGRAY. Set by
                                ; wm_pkgcall (stacked, so a repaint pass that
                                ; calls several windows' callbacks answers
                                ; WHOSE) and by wm_clip_set (for a background
                                ; painter, which reaches no dispatcher);
                                ; cleared by gfx_unlock, which is [gfx_dis]'s
                                ; lifetime and for its reason. .text with a
                                ; real initialiser: -f bin zeroes nothing.
                                ; A WORD, and only its low byte is ever
                                ; written: wm_pkgcall banks it with `push
                                ; word`, which would read - and its `pop
                                ; word` would WRITE - the neighbouring byte
                                ; if this were a `db`

; --- gfx_blit1's PEN (SPEC.md 5.4.2.2) --------------------------------------
; A SET bit in a band takes the ink, a CLEAR bit the paper. The default pair
; IS the resting framebuffer state - Map Mask 0Fh with Set/Reset disabled puts
; one CPU byte in all four planes, so a set bit is colour 15 and a clear one 0
; - which is why the default costs nothing and is what every caller written
; before this got. gfx_unlock puts it back, exactly as it does [gfx_dis]:
; a pen is valid for one lock hold and no longer.
;
; ON A 1bpp ADAPTER IT IS NOT READ AT ALL. A band there already means lit and
; unlit, which is the whole of what one plane can say, so the pen has nothing
; to add and is ignored rather than refused - a refusal would blank the shape
; on two adapters of three for a caller who set a pen the third one wanted.
;
; ...UNLESS THE CALLER SAYS ITS BAND IS NOT IN SCREEN POLARITY (SPEC.md
; 5.9.3.1). band.inc composes ink-as-set-bits or paper-as-set-bits, whichever
; the display it was composed for wants, and on an EXTENDED DESKTOP the band
; meets both a 1bpp display and a colour one - so one storage cannot be right
; for both and the storage becomes a DECLARATION instead. The 1bpp arm then
; answers the one question a single plane CAN be asked: which way up the bits
; are. It is 0 for every other caller and dies with the pen, so the paragraph
; above is untouched for a package.
gfx_b1ink       db CWHITE
gfx_b1pap       db CBLACK
gfx_b1adp       db 0            ; 0 = the band is in SCREEN polarity, 1 = a set
                                ;     bit is the pen's ink, 2 = a set bit is its
                                ;     paper and the emit may turn the band over
                                ;     to suit the display (SPEC.md 5.9.3.1)

; --- THEME (SPEC.md 76) -----------------------------------------------------
; [thm_kind] is the SETTING; the eight bytes below are what it MEANS, copied
; whole from thm_tab at the switch so every chrome site is a load and never a
; branch. The content of a window is not themed in any of them - only the
; desktop, the menu bar, the dock and a window's own chrome.
;
; It was TWO bytes while there were two themes, and Color is what made the
; distinctions real: a title bar that is blue when focused and grey when not
; needs a second pair, and a desktop that is neither a dither nor black needs
; a colour rather than a flag. THE LIVE BLOCK AND THE TABLE ROW ARE THE SAME
; EIGHT BYTES IN THE SAME ORDER, so the switch is one `rep movsb` and there is
; no per-field copy to fall out of step with the table.
THM_BRIGHT  equ 0
THM_DARK    equ 1
THM_COLOR   equ 2
%ifdef KERN_BIG
THM_NKIND   equ 3
%else
THM_NKIND   equ 2               ; NO COLOR ON kern_small (SPEC.md 76.12.3):
%endif                          ; the 128KB machine's kernel does not get to
                                ; spend a step on a theme it would have to
                                ; reduce to Bright anyway
THM_DITHER  equ 0xFF            ; [thm_deskc]: lay SPEC.md 26's 50% dither
                                ; rather than a solid colour

%ifdef THEMEDARK                ; make THEMEDARK=1 (Dark) or =2 (Color): the
thm_kind        db THEMEDARK    ; A/B, and the only way to see a theme before
%else                           ; the Control Panel has been clicked
thm_kind        db THM_BRIGHT
%endif
                                ; [thm_kind] IS IN BOTH BUILDS even though
                                ; kern_small has no theme: SYSTEM.CFG's 'TH'
                                ; key is two bytes on both, so a user's choice
                                ; round-trips untouched through a small kernel
                                ; instead of being reset by booting one
                                ; (SPEC.md 76.13)
%ifdef OS88_THEME
; ...and the live block. ADJACENT AND IN THIS ORDER: thm_pair reads the first
; two as one word, and thm_set copies the row over the lot.
thm_bgc         db CWHITE       ; chrome background: the field a caption sits on
thm_inkc        db CBLACK       ; chrome ink: the caption, the rules, the boxes
thm_disc        db CDGRAY       ; ...and DISABLED chrome ink (SPEC.md 76.6)
thm_tbgc        db CWHITE       ; the ACTIVE title bar's ground...
thm_tinkc       db CBLACK       ; ...and its ink. Equal to the pair above in
                                ; both 1bpp themes, which is what keeps them
                                ; byte-identical to the kernel that had one
thm_deskc       db THM_DITHER   ; the desktop's ground, or THM_DITHER
thm_strp        db 1            ; 1 = the active title carries SPEC.md 11's
                                ; pinstripes; 0 = it is a solid bar
thm_pad         db 0            ; the row is 8 so the index is a shift

; bg      ink     dis     tbg     tink    desk        stripes pad
thm_tab:
    db CWHITE, CBLACK, CDGRAY, CWHITE, CBLACK, THM_DITHER, 1, 0   ; Bright
    db CBLACK, CWHITE, CLGRAY, CBLACK, CWHITE, CBLACK,     1, 0   ; Dark
%ifdef KERN_BIG
    db CLGRAY, CBLACK, CDGRAY, CBLUE,  CWHITE, CTEAL,      0, 0   ; Color
%endif

%endif

gfx_color       db CBLACK       ; current drawing color for every solid op.
                                ; IT LIVES HERE, not up beside gfx_mono1,
                                ; because the pen setters write it and the
                                ; two bytes below it TOGETHER - see the
                                ; adjacency guard under gfx_disink
gfx_dis         db 0            ; 1 = what is being drawn is a DISABLED
                                ; control (SPEC.md 47). It is a FLAG and not
                                ; a colour because the renderer has to do
                                ; something a colour cannot express: on a
                                ; mono adapter font_ink masks the glyph to a
                                ; checkerboard, which is the only way a
                                ; disabled STRING is distinguishable from a
                                ; live one at 1bpp. Keying that off CDGRAY
                                ; instead would catch every other use of the
                                ; colour - Minesweeper's digit 8 came out
                                ; looking like a menu item you cannot pick -
                                ; and would make "never use this colour for
                                ; anything else" a rule the tree has to keep
                                ; by hand. Set it through gfx_pen_* and it
                                ; cannot disagree with gfx_color.
                                ; Initialized data, NOT .bss (nothing zeroes
                                ; .bss), and gfx_unlock clears it for the
                                ; reason it clears the clip region: it is
                                ; valid for exactly one lock hold.
gfx_disink      db CBLACK       ; ...and WHICH DIRECTION a disabled GLYPH
                                ; stipples in on a 1bpp adapter (SPEC.md
                                ; 76.6.1). The flag above buys the
                                ; checkerboard; this says what it is laid on,
                                ; and it is the setting pen's own LIVE ink -
                                ; black for the content pair, [thm_inkc] for
                                ; the chrome's. Without it font_ink's
                                ; reduction sent every disabled pen to black
                                ; (they are all middle greys, and a grey is
                                ; not 0xFF), so a greyed caption and a
                                ; MENU_DIS separator were a black stipple on
                                ; Dark's black chrome - drawn, correct in the
                                ; framebuffer, and invisible.
                                ;
                                ; Initialized data and NOT .bss, for
                                ; [gfx_dis]'s reason one line up: nothing
                                ; zeroes .bss, and CBLACK is the answer for
                                ; every caller that never touches it.
%if gfx_dis - gfx_color != 1 || gfx_disink - gfx_dis != 1
  %error "gfx_color/gfx_dis/gfx_disink must stay adjacent: gfx_pen_live and gfx_pen_dis each set a PAIR of them in one word store"
%endif
; THE EDGE MASKS, AND THE GC INDEX THAT ALWAYS GOES WITH THEM. Every site
; that uses one does `mov ah,[mask] / mov al,8 / out dx,ax` - the Bit Mask
; register's index is 8 and never anything else - so the literal lives in
; front of the mask and the pair is ONE word load. That is 3 bytes and 4
; clocks a site, in the hot fills, and it is CLAUDE.md's prefetch-floor case:
; `mov ax,[m]` is the 3-byte accumulator form.
;
; In .text rather than .bss for the same reason gfx_dis is: -f bin initialises
; .text, so the two 8s are real bytes in the image. gfx_rect_setup writes both
; masks as BYTES and softgfx.inc reads them as bytes; nothing addresses the
; pair from the other end.
;
; THE MASKS ARE SHARED AND THEIR PACKING IS VGA'S, which is why they carry a
; gfx_ name and live in the VGA file rather than beside the body that writes
; them. gfx_rect_setup moved to softgfx.inc because nothing in it touches this
; card (see there); these four bytes did not follow it, because splitting the
; pair to move two of them costs every VGA fill 3 bytes and 4 clocks a site -
; the whole point of the interleave - and a name in the right file is not
; worth that. The 8s are the only VGA-specific part and they are two bytes;
; docs/MONO-RECLAIM-PLAN.md 1.1 carries the trade.
vga_m8l         db 8
gfx_lmsk        db 0            ; rect: left edge bit mask
vga_m8r         db 8
gfx_rmsk        db 0            ; rect: right edge bit mask
%if gfx_lmsk - vga_m8l != 1 || gfx_rmsk - vga_m8r != 1
  %error "vga_m8l/gfx_lmsk and vga_m8r/gfx_rmsk must stay adjacent: the fills load each pair as one word"
%endif
gfx_pat         dw 0            ; gfx_fill_pat: near ptr to 8 row bytes,
                                ; set by the caller before every call
gfx_lock_flag   db 0            ; drawing mutex (SPEC.md 7); **strictly 0 or 1
                                ; and never a count** - toast_now reads this
                                ; byte and the next AS ONE WORD (`cmp word
                                ; [gfx_lock_flag], 1` = held, and held by task
                                ; 0), and that test dies SILENTLY if this ever
                                ; becomes a depth. Four writers in the tree and
                                ; no fifth: :5303 (1), :5384 (0), sched.inc
                                ; :1683 (0) and :1699 (the value banked at
                                ; :1677).
                                ; Initialized data and NOT .bss - not because
                                ; .bss is unzeroed (it arrives zeroed, SPEC.md
                                ; 2.6) but because gfx_lock_own below cannot
                                ; live there at all, and the two must stay
                                ; together.
gfx_lock_own    db 0xFF         ; ...and WHICH task holds it ([sch_cur] at the
                                ; take, 0xFF = nobody). toast_now's whole test
                                ; (SPEC.md 59.4): the flag alone says somebody
                                ; holds it, never that the CALLER does.
                                ; **KEEP IT IMMEDIATELY AFTER THE FLAG**, which
                                ; is what makes that test one word read.
%if gfx_lock_own - gfx_lock_flag != 1
  %error "toast_now reads gfx_lock_flag and gfx_lock_own as ONE WORD (`cmp word [gfx_lock_flag], 1` = held by task 0). Separate them and it silently stops firing."
%endif
gfx_lock_want   db 0            ; SPEC.md 7.3: SOMEBODY IS BLOCKED ON IT. Set by
                                ; a waiter on its way to task_yield, spent by
                                ; the last owner's next acquire. Initialized
                                ; data for gfx_lock_flag's reason.
gfx_lock_last   db 0xFF         ; ...and who took it last, which gfx_lock_own
                                ; cannot say: gfx_unlock stamps 0xFF over it,
                                ; and 7.3's whole question is about a task
                                ; that has ALREADY let go.

; -----------------------------------------------------------------------------
; GFXAUDIT (make GFXAUDIT=1) - WHO DRAWS WITH THE LOCK FREE?
;
; SPEC.md 7 says two things that only hold together: "every task-level drawing
; burst is wrapped in gfx_lock/gfx_unlock", and "the mouse ISR moves the cursor
; when the lock is free AND cur_level >= 0". So a primitive that runs with
; [gfx_lock_flag] clear is drawing WHILE IRQ4 may be drawing the arrow - the
; two share the VGA's registers, gfx_rect_setup's statics and the framebuffer,
; and cur_draw's own header says it wants "default GC state on entry".
;
; This counts those calls and remembers WHERE THEY CAME FROM: the return
; address at the audited entry is the drawing call site itself, because every
; hook below sits above the first push of a public routine. Read it with
; tools/os88sym.py and os88marty read.
;
; It is a DIAGNOSTIC and never ships - it costs a call, a compare and a return
; at every primitive in the machine.
; -----------------------------------------------------------------------------
%ifdef GFXAUDIT
GFXAUD_N    equ 12              ; distinct call sites remembered

gfx_aud_tot times 2 db 0        ; unlocked primitive calls, all sites
gfx_aud_ra  times GFXAUD_N*2 db 0   ; ...each site's return address
gfx_aud_cnt times GFXAUD_N*2 db 0   ; ...and how many from it
gfx_aud_dep db 0                ; rect primitives IN FLIGHT right now
gfx_aud_mv  times 2 db 0        ; ISR cursor MOVES, the control for the row
                                ; below: a zero collision count means nothing
                                ; unless the arrow was being drawn at all
gfx_aud_gate times 2 db 0       ; ISR draws with the progress widget UP, which
                                ; SPEC.md 12.8.4 makes unreachable: THE GATE
gfx_aud_bank times 2 db 0       ; ...and screen READS taken with the arrow still
                                ; ON THE GLASS, which is SPEC.md 11.101.2's:
                                ; every gfx_save is a save-under, and one that
                                ; banks the cursor stores it as somebody's
                                ; window content for the rest of the session
gfx_aud_def times 2 db 0        ; ...and the deferrals that make a zero above
                                ; mean something
gfx_aud_race times 2 db 0       ; ...and the times IRQ4 drew the arrow inside
                                ; one with the lock free, which is the
                                ; COLLISION rather than the exposure

gfx_aud:                        ; the BRACKETED hook: GFXCLIP's, which has a
    inc byte [gfx_aud_dep]      ; gfx_aud_end to match it, so the depth is a
                                ; real count of rect primitives in flight -
                                ; whoever holds the lock, because the ISR's own
                                ; test is the flag and this is the other half
                                ; of the question
gfx_aud_site:                   ; ...and the bare one, for the whole-shape
    pushf                       ; entries that have no end to pair with: it
    push ax                     ; counts the site and touches no depth, or the
    push bx                     ; leak would read as a permanent collision
    push cx
    push si
    cmp byte [gfx_lock_flag], 0
    jne .out                    ; held by SOMEBODY is this audit's whole bar:
                                ; the ISR's test is the same flag, so a held
                                ; lock is a cursor that cannot move
    mov bx, sp                  ; SS is LOW_SEG (SPEC.md 1), so this needs the
    mov ax, [ss:bx+12]          ; override: 5 words banked, then our own return
    inc word [gfx_aud_tot]      ; address, then the call site's
    mov si, gfx_aud_ra
    mov cx, GFXAUD_N
.scan:
    cmp [si], ax
    je .hit
    cmp word [si], 0
    je .new
    add si, 2
    loop .scan
    jmp short .out              ; table full: the total still counts it
.new:
    mov [si], ax
.hit:
    sub si, gfx_aud_ra
    add si, gfx_aud_cnt
    inc word [si]
.out:
    pop si
    pop cx
    pop bx
    pop ax
    popf
    ret

gfx_aud_end:                    ; the primitive's answer is in the flags on
    pushf                       ; this path (gfx_scroll's CF), so they survive
    dec byte [gfx_aud_dep]
    popf
    ret

%macro GFXAUDIT_HOOK 0
    call gfx_aud_site
%endmacro
%else
%macro GFXAUDIT_HOOK 0
%endmacro
%endif

; CURLOG - one cursor event into mouse.inc's ring (GFXAUDIT only). DEFINED HERE
; and not beside the ring itself, because vga12.inc is %included BEFORE
; mouse.inc and a macro has to exist before it is used - the label it calls
; does not.
%macro CURLOG 1
%ifdef GFXAUDIT
    push ax
    push si
    mov al, %1
    call cur_logput
    pop si
    pop ax
%endif
%endmacro

; -----------------------------------------------------------------------------
; GFXCLIP - the clip hook, at a public rect entry (SPEC.md 11.3)
;
; Placed at the PUBLIC entry, above the [vid_mono] dispatch, so one hook covers
; the VRAM path, VGA and both mono adapters at once - on a 1bpp adapter the
; software renderer IS the direct path (SPEC.md 39.5), and a hook below the
; dispatch would work on VGA and silently do nothing on Hercules.
;
; Disarmed (the whole repaint path) it costs one compare and a taken branch;
; nothing else changes. Armed, gfx_clip_run re-enters the raw body once per
; visible fragment.
; -----------------------------------------------------------------------------
%macro GFXCLIP 1
%ifdef GFXAUDIT
    call gfx_aud                ; make GFXAUDIT=1: is the lock even held, and
    cmp word [wm_clip_n], 0     ; is IRQ4 inside this call? The tail jump below
    jne %%aclipped              ; becomes a CALL so the body has an end to
    CURUNLAZY                   ; count from - one stack word deeper than the
    call %1                     ; kernel that ships, which is what a diagnostic
    jmp gfx_aud_end             ; build is allowed to be
%%aclipped:
    push di
    mov di, %1
    call gfx_clip_run
    pop di
    jmp gfx_aud_end
%else
    cmp word [wm_clip_n], 0     ; clipped background draw? (SPEC.md 11.3)
    jne %%clipped
    CURUNLAZY                   ; UNCLIPPED: this may write any pixel on the
    jmp %1                      ; screen, so a deferred hide is owed (7.1.4)
%%clipped:
    push di
    mov di, %1
    call gfx_clip_run
    pop di
    ret
%endif
%endmacro

; -----------------------------------------------------------------------------
; GFXDISP - the display hook, immediately BELOW a rect entry's GFXCLIP
;           (SPEC.md 39.14.1)
;
;   gfx_fill:        GFXCLIP gfx_fill_d       ; clip, in VIRTUAL space
;   gfx_fill_d:      GFXDISP gfx_fill_raw     ; split by display, then translate
;   gfx_fill_raw:    ...
;
; BELOW GFXCLIP AND NOT ABOVE IT, which is the correctness argument rather
; than a preference: wm_clip_tab holds VIRTUAL rects, so the clip has to be
; applied before anything is translated. Above, gfx_clip_run would be handed
; display-local coordinates to compare against virtual fragments - which is
; right on display 0, where the two are the same numbers, and silently wrong
; everywhere else. The dual-display plan said "above"; it was written before
; the clip's coordinate space had been thought about.
;
; ONE DISPLAY COSTS A COMPARE AND A TAKEN BRANCH, then falls straight through
; into the body - the bargain [vid_mono] and [wm_clip_n] already make. On
; kern_small it expands to nothing at all.
;
; [gfx_dnest] is what lets this compose with 39.14.2's whole-shape hooks: a
; font_run that has already translated falls back to gfx_fill + font_str, and
; a translated caller reaching a hooked callee would translate twice.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; NOVGA_ARM - the no-VGA arm of an entry whose VGA body is FALLEN INTO
;
; SPEC.md 39.27.5. Four entries in this file are reached by fall-through from a
; GFXDISP or a plain label above them, and GFXDISP EXPANDS TO NOTHING on
; kern_small - so on that build the entry lands on whatever the assembler put
; next. An `equ` to the software twin is the obvious way to write the no-VGA
; arm and it is WRONG for exactly that reason: an `equ` emits no code, so the
; fall-through goes past it into the following routine. It assembles, it boots,
; and gfx_fill_gray runs osapi_gfx_fill_pat's body.
;
; So the arm is a LABEL AND A JUMP, and the %if is what stops the next person
; turning it back into an `equ`: an arm that emits nothing fails the build.
; -----------------------------------------------------------------------------
%macro NOVGA_ARM 2
%1:
    jmp %2
%%armend:
 %if %%armend - %1 < 2
  %error "the no-VGA arm of %1 emits no code - the entry above FALLS THROUGH to it (SPEC.md 39.27.5)"
 %endif
%endmacro

%macro GFXDISP 1
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe %%one                   ; one display: the rect is already local
    cmp byte [gfx_dnest], 0
    jne %%one                   ; ...or an outer hook already translated it
    push di
    mov di, %1
    call gfx_disp_run
    pop di
    ret
%%one:
%endif
%endmacro

%ifdef KERN_BIG
; .text with real initialisers: -f bin zeroes nothing, and every drawing call
; in the machine reads these - including the first, before anything has run.
gfx_dnest   db 0                ; whole-shape hooks in effect (SPEC.md 39.14.2)
%ifndef NOBLITCUT
gfx_blit_fd db 0                ; SPEC.md 39.14.7.2: 0 = gfx_blit4 resolves its
                                ; own display, d+1 = this call is one HALF of a
                                ; cut and the display is NAMED.
                                ;
                                ; .text WITH A REAL INITIALISER, not .bss, for
                                ; vid_ndisp's reason two files along: -f bin
                                ; zeroes nothing, and this is the one byte of
                                ; gfx_blit4's block that is READ BEFORE IT IS
                                ; WRITTEN - the gate sits above every store in
                                ; the routine. In .bss it holds whatever the
                                ; machine left there until the first blit, and
                                ; a stale non-zero is a display INDEX handed
                                ; to vid_ctx_act that indexes past a two-record
                                ; array. gfx_blit_hk and gfx_blit_fst may stay
                                ; in .bss because each call writes them first
%endif
gfx_dr      times 4 dw 0        ; gfx_disp_run's banked virtual rect
gfx_dfn     dw 0                ; ...and the raw body it is re-entering

; -----------------------------------------------------------------------------
; gfx_disp_run - internal: run a rect primitive once per DISPLAY it touches
; in:       AX = x1, BX = y1, CX = x2, DX = y2 in VIRTUAL coordinates,
;           DI = near ptr to the raw body (the entry below that primitive's
;           GFXDISP); more than one display exists and no hook is nested
; out:      -
; clobbers: flags
;
; gfx_clip_run's shape, with a GEOMETRY attached to each rect instead of only
; a rect: intersect with the display, translate the survivor into the
; display's own coordinates, activate it, draw. A fragment landing in no
; display is drawn by nobody and there is no test for it anywhere - which is
; the whole reason the DEAD ZONE (SPEC.md 39.2.1) costs nothing.
;
; The state is module scratch for gfx_clip_run's reason: the raw bodies
; preserve everything, there is nothing left to spare, and this only ever runs
; with the gfx lock held.
;
; IT DOES NOT RESTORE THE DISPLAY (SPEC.md 39.14.3): the last one drawn on
; stays current until gfx_unlock, so a window's repaint pays ONE swap and not
; two per primitive. That is safe because the cursor brackets itself (39.15.2)
; - cur_unlazy runs at the top of GFXCLIP, above all of this, and would
; otherwise erase the arrow through whatever display happened to be live.
; -----------------------------------------------------------------------------
gfx_disp_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [gfx_dr+0], ax
    mov [gfx_dr+2], bx
    mov [gfx_dr+4], cx
    mov [gfx_dr+6], dx
    mov [gfx_dfn], di
    xor si, si                  ; SI = the display being drawn
.disp:
    mov ax, si
    call vid_ctx_ptr            ; DI = its record; AX preserved
    mov ax, [di+VID_CTX_VX]     ; x1 = max(rect's, this display's)
    cmp ax, [gfx_dr+0]
    jge .x1
    mov ax, [gfx_dr+0]
.x1:
    mov cx, [di+VID_CTX_VX]     ; x2 = min(rect's, its last column)
    add cx, [di+VID_CTX_CW]
    dec cx
    cmp cx, [gfx_dr+4]
    jle .x2
    mov cx, [gfx_dr+4]
.x2:
    cmp ax, cx
    jg .next                    ; no columns here - and if no display claims
                                ; the fragment, nobody draws it (39.2.1)
    mov bx, [di+VID_CTX_VY]
    cmp bx, [gfx_dr+2]
    jge .y1
    mov bx, [gfx_dr+2]
.y1:
    mov dx, [di+VID_CTX_VY]
    add dx, [di+VID_CTX_CH]
    dec dx
    cmp dx, [gfx_dr+6]
    jle .y2
    mov dx, [gfx_dr+6]
.y2:
    cmp bx, dx
    jg .next
    push si                     ; ...into the display's own coordinates, while
    mov si, [di+VID_CTX_VX]     ; the record is still in hand: vid_ctx_act is
    sub ax, si                  ; about to move the live words under us
    sub cx, si
    mov si, [di+VID_CTX_VY]
    sub bx, si
    sub dx, si
    pop si
    push ax                     ; AL is the display's, x1 is the rect's
    mov ax, si
    call vid_ctx_act
    pop ax
    call word [gfx_dfn]         ; the body - it preserves every register
.next:
    inc si
    mov al, [vid_ndisp]
    mov ah, 0
    cmp si, ax
    jb .disp                    ; ...and the LAST display drawn on stays
    pop di                      ; current; gfx_unlock is what puts 0 back
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_disp_enter / gfx_disp_leave - the WHOLE-SHAPE hook (SPEC.md 39.14.2)
;
; in:       AX = x, BX = y - the shape's ANCHOR, in virtual coordinates
; out:      that display is current; AX/BX are its own coordinates;
;           [vid_ox]/[vid_oy] is the origin subtracted, for a caller with more
;           coordinates of its own to move
; clobbers: flags (the GFXDLEAVE macro banks them, because a leave sits at the
;           end of routines whose answer is in CF).
;
; For font_char, font_run, ico_core, gfx_scroll, gfx_save/restore and
; gfx_line, none of which can be cut into rect fragments: they clip per whole
; cell, per whole shape or per pixel. So the shape goes ENTIRELY onto the
; display holding its anchor and is cut at that display's edge exactly as it
; is cut at a screen edge today - 39.14.2's decision, and 39.14.3's rule for
; the cursor applied to everything else with the same shape.
;
; THE NESTING COUNT IS WHAT MAKES IT COMPOSE. font_run's fallback path is
; gfx_fill + font_str and gfx_line's axis-aligned cases are gfx_hline/vline:
; a translated caller reaching a hooked callee would translate twice. It is a
; COUNT and not a flag because gfx_disp_leave has to know which call owes the
; restore.
; -----------------------------------------------------------------------------
gfx_disp_enter:
    inc byte [gfx_dnest]
    cmp byte [gfx_dnest], 1
    jne .out                    ; nested: an outer hook owns the translation
    push ax                     ; vid_disp_of ANSWERS in AL, so x is banked
    call vid_disp_of            ; AL = the display holding (AX, BX); it cannot
    jmp short gfx_dent_act      ; fail - the dead zone answers with the primary
.out:
    ret

; ...and the same enter with the display NAMED rather than resolved, for
; SPEC.md 39.14.7.2's cut. DL = the display index.
;
; IT IS NOT gfx_disp_enter WITH A DIFFERENT ANCHOR, and that is the whole
; reason it exists: vid_disp_of answers with the PRIMARY for a point no
; display covers (39.2.1), so a half whose top-left corner sits in the dead
; zone - above the second monitor's first row, which on the RIGHT layout is
; every y under MBAR_H (39.19.3) - would be entered on display 0, drawn at an
; x that display does not have, and clipped away entire. The rows the second
; display DOES hold would go with it. The cut already knows which display each
; half belongs to, because it computed the seam from that display's record.
gfx_disp_enter_n:
    inc byte [gfx_dnest]
    cmp byte [gfx_dnest], 1
    jne gfx_dent_out            ; nested: an outer hook owns the translation
    push ax
    mov al, dl
gfx_dent_act:
    call vid_ctx_act
    pop ax
    sub ax, [vid_ox]
    sub bx, [vid_oy]
gfx_dent_out:
    ret

gfx_disp_enter_cd:
    push ax                     ; the same, with the anchor in CX/DX - and
    push bx                     ; nested it writes back what it was handed,
    mov ax, cx                  ; because gfx_disp_enter left AX/BX alone
    mov bx, dx
    call gfx_disp_enter
    mov cx, ax
    mov dx, bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_denter / gfx_denter_rect / gfx_dleave - what GFXDENTER, GFXDORG and
; GFXDLEAVE used to expand INTO at every site (SPEC.md 39.14.2)
;
; The three macros were 10, 15 and 13 bytes at 7, 5 and 11 sites. Every
; GFXDENTER/GFXDORG site is a WHOLE-SHAPE primitive - gfx_line, gfx_blit4,
; gfx_blitp, gfx_xor_rect, gfx_save, gfx_restore, gfx_scroll - whose own
; arrival is hundreds of microseconds, so a near call+ret at 43 clocks is
; under half a percent of any of them. font_char's and font_run's leaves stay
; INLINE, for the reason the macro banner gives: a glyph cell is ~1ms and 11us
; is 1.2% of every character in the system.
;
; gfx_denter_rect does NOT call gfx_denter - it inlines the two instructions.
; That is +7 bytes once and removes a nested call from all five rect sites,
; which is the whole of what the conversion would otherwise have cost them.
; The one-display skip is shared: on a machine with one card [gfx_dnest] never
; leaves 0, so GFXDORG's own `cmp/jne` would have skipped anyway.
; -----------------------------------------------------------------------------
gfx_denter:
    cmp byte [vid_ndisp], 1
    jbe .one
    jmp gfx_disp_enter          ; a tail jump: it returns to OUR caller
.one:
    ret

gfx_denter_rect:
    cmp byte [vid_ndisp], 1
    jbe .one
    call gfx_disp_enter
    cmp byte [gfx_dnest], 1     ; gated on the nest count being exactly 1, not
    jne .one                    ; on non-zero: nested, an OUTER hook did the
    sub cx, [vid_ox]            ; translation and [vid_ox] is that hook's
    sub dx, [vid_oy]            ; origin, so subtracting it again would move
.one:                           ; the shape twice
    ret

gfx_dleave:
    pushf                       ; the FLAGS, because more than one of these
    cmp byte [vid_ndisp], 1     ; routines answers in CF and a leave is the
    jbe .one                    ; last thing before its `ret` - and `dec`
    dec byte [gfx_dnest]        ; sets them
.one:
    popf
    ret

%endif

; GFXDENTER / GFXDLEAVE - the two above, with their one-display test inlined
; so a single-card machine pays a compare rather than a near call. font_char
; is the reason: SPEC.md 6.1.1 prices a glyph cell at ~1ms on the target and
; a call/ret at ~11us, which is 1.2% of every character in the system for a
; branch that is never taken on any machine but one.
%macro GFXDENTER 0
%ifdef KERN_BIG
    call gfx_denter
%endif
%endmacro

; ...and the RECT spelling: the enter above plus what GFXDORG used to be, for
; a routine holding a rect rather than a point. The enter translated the
; anchor; this translates the far corner in CX/DX. Every GFXDORG site in the
; tree was `GFXDENTER` / `GFXDORG cx, dx` on consecutive lines, so the pair is
; one call and GFXDORG no longer exists on its own.
%macro GFXDENTERR 0
%ifdef KERN_BIG
    call gfx_denter_rect
%endif
%endmacro

; ...and the CX/DX spelling, for font_char and font_run_x - where AL is the
; character and AX cannot be touched - and for the icon wrappers.
%macro GFXDENTERCD 0
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe %%one
    call gfx_disp_enter_cd
%%one:
%endif
%endmacro

; ...and GFXDLEAVE is only the count coming down, because NOTHING RESTORES
; THE DISPLAY any more (SPEC.md 39.14.3): the last one drawn on stays current
; until gfx_unlock, so a burst of primitives in one place pays one swap
; instead of one per call. It banks the FLAGS, because more than one of these
; routines answers in CF and a leave is the last thing before its `ret`, and
; `dec` sets them.
%macro GFXDLEAVE 0
%ifdef KERN_BIG
    call gfx_dleave
%endif
%endmacro

; ...and the INLINE spelling, for font_char and font_run_x alone. The banner
; above is the reason: a glyph cell is ~1ms on the target and a call/ret is
; ~11us, which is 1.2% of every character in the system for a branch no
; single-card machine ever takes.
%macro GFXDLEAVEI 0
%ifdef KERN_BIG
    pushf
    cmp byte [vid_ndisp], 1
    jbe %%one
    dec byte [gfx_dnest]
%%one:
    popf
%endif
%endmacro

; -----------------------------------------------------------------------------
; gfx_rect_isect - AX/BX/CX/DX intersected with the rect at DS:SI
; in:       AX = x1, BX = y1, CX = x2, DX = y2 (inclusive); SI = a four-word
;           block laid out x1, y1, x2, y2 - the WCR_* order, which every rect
;           in the window manager and every clip rect here already uses
; out:      the same four registers, clipped. Flags are `cmp dx, [si+6]`'s,
;           which is what the open-coded copies left too
; clobbers: nothing else
;
; SIGNED compares, and they have to be: wm_paint_dmg's clamp takes screen
; coordinates negative.
;
; **THE RECT IS ADDRESSED THROUGH DS**, so a rect this serves may never move
; to `.lowbss`. All of them are `.text` or `.bss` today.
;
; It is HERE, in the lowest layer, rather than in wm.inc: this file is
; %included first, so the five sites up there are backward references and a
; gfx_* primitive is not left forward near-calling a wm_* helper. Eight blocks
; of these four compares existed - five intersections and, with the store form
; below, three unions - and every one of them already did `mov si, <rect>`
; first, which is why sharing this pays: the marshalling does not grow.
; -----------------------------------------------------------------------------
gfx_rect_isect:
    cmp ax, [si]
    jge .a
    mov ax, [si]
.a:
    cmp bx, [si+2]
    jge .b
    mov bx, [si+2]
.b:
    cmp cx, [si+4]
    jle .c
    mov cx, [si+4]
.c:
    cmp dx, [si+6]
    jle .d
    mov dx, [si+6]
.d:
    ret

; -----------------------------------------------------------------------------
; gfx_rect_hit - does AX/BX/CX/DX OVERLAP the rect at DS:SI?
; in:       AX = x1, BX = y1, CX = x2, DX = y2 (inclusive); SI = the same
;           four-word WCR_* block gfx_rect_isect takes
; out:      CF = 1 they overlap, CF = 0 they do not
; clobbers: nothing at all - not even the four inputs
;
; gfx_rect_isect's missing sibling. gfx_rect_isectcf answers the same question
; but CLIPS on the way, so using it here would cost four pushes and four pops
; and lose more than the compares are worth; this one only compares.
;
; SIGNED, for gfx_rect_isect's reason: wm_paint_dmg's clamp takes screen
; coordinates negative.
; -----------------------------------------------------------------------------
gfx_rect_hit:
    cmp ax, [si+4]
    jg .no
    cmp cx, [si]
    jl .no
    cmp bx, [si+6]
    jg .no
    cmp dx, [si+2]
    jl .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; gfx_rect_isectcf - gfx_rect_isect, and CF = 1 if the answer is EMPTY
; in/out:   as above, plus CF = 1 -> x1 > x2 or y1 > y2, i.e. nothing left
; clobbers: nothing else
; -----------------------------------------------------------------------------
gfx_rect_isectcf:
    call gfx_rect_isect
    cmp ax, cx
    jg .no
    cmp bx, dx
    jg .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; gfx_clip_run - internal: run a rect primitive once per visible fragment
; in:       AX = x1, BX = y1, CX = x2, DX = y2, DI = near ptr to the raw body
;           (the entry just below that primitive's GFXCLIP); clip is armed
; out:      -
; clobbers: flags
;
; The loop's state lives in module scratch rather than registers because the
; raw bodies preserve everything and there is nothing left to spare anyway.
; That is safe for the same reason vga12's rect scratch is: this only ever
; runs with the gfx lock held, and no clipped primitive calls another.
; -----------------------------------------------------------------------------
gfx_clip_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; SPEC.md 39.14.4: a nested call from inside a
    je .virt                    ; whole-shape hook arrives in DISPLAY
    add ax, [vid_ox]            ; coordinates, and wm_clip_tab is virtual. The
    add cx, [vid_ox]            ; fragments go back below, one subtraction
    add bx, [vid_oy]            ; each, because the raw body wants them local
    add dx, [vid_oy]
.virt:
%endif
    mov [gfx_cl_x1], ax
    mov [gfx_cl_y1], bx
    mov [gfx_cl_x2], cx
    mov [gfx_cl_y2], dx
    mov [gfx_cl_fn], di
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
.frag:
    mov ax, [si+WCR_X1]         ; the fragment, clipped to the primitive's own
    mov bx, [si+WCR_Y1]         ; rect - and gfx_cl_x1..y2 is laid out in the
    mov cx, [si+WCR_X2]         ; same WCR order, so the helper reads it as
    mov dx, [si+WCR_Y2]         ; one block
    push si
    mov si, gfx_cl_x1
    call gfx_rect_isectcf
    pop si
    jc .next                    ; nothing of this fragment is inside
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; ...and the fragment back into the display the
    je .lv                      ; body is going to draw on. The loop re-reads
    sub ax, [vid_ox]            ; gfx_cl_* below, which stayed virtual, so
    sub cx, [vid_ox]            ; there is nothing to undo
    sub bx, [vid_oy]
    sub dx, [vid_oy]
.lv:
%endif
    call word [gfx_cl_fn]
.next:
    add si, WCR_SZ
    dec di
    jnz .frag
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef GFX_VGA
; -----------------------------------------------------------------------------
; vga_set_color - internal: arm Set/Reset with [gfx_color] on all planes
; in:       -
; out:      GC0 = [gfx_color], GC1 = 0Fh
; clobbers: flags only (all registers preserved)
; -----------------------------------------------------------------------------
vga_set_color:
    push ax
    push dx
    mov dx, VGA_GC
    mov ah, [gfx_color]
    xor al, al                  ; GC0: Set/Reset value
    out dx, ax
    mov ax, 0x0F01              ; GC1: enable Set/Reset on all planes
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_set_xor - internal: arm Set/Reset 0Fh with ALU function = XOR
; in:       -
; out:      GC0 = 0Fh, GC1 = 0Fh, GC3 = 18h
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_set_xor:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0F00              ; GC0: Set/Reset value = 0Fh
    out dx, ax
    mov ax, 0x0F01              ; GC1: enable on all planes
    out dx, ax
    mov ax, 0x1803              ; GC3: rotate 0, function = XOR
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_gc_reset - internal: restore the GC default state (SPEC.md 1.7)
; in:       -
; out:      GC1 = 0, GC3 = 0, GC8 = FF
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_gc_reset:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0001              ; GC1: Set/Reset enable off
    out dx, ax
    mov ax, 0x0003              ; GC3: rotate 0, function = replace
    out dx, ax
    mov ax, 0xFF08              ; GC8: Bit Mask = all bits
    out dx, ax
    pop dx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; SPEC.md 5.4.1's one-shot: the pair tables are derived from gfx_inktab, so
; they are built at run time rather than assembled - and the flag lives HERE,
; in .text with a real initialiser, because -f bin zeroes no .bss and this is
; read by the first blit the machine ever draws.
gfx_pairbuilt db 0

; ...and the planar decoder's own (SPEC.md 5.4.1.3), for the same reason: the
; 512-byte pair-to-plane table is DERIVED and lives in .lowbss, which -f bin
; leaves as whatever the last boot wrote there.
%ifdef GFX_PLANE
vga_p4built db 0
%endif

; -----------------------------------------------------------------------------
; gfx_pixel - plot one pixel in [gfx_color]
; in:       CX = x, DX = y
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_pixel:
    push ax
    push bx
    mov ax, cx                  ; a pixel is a 1x1 solid rect (CX/DX already
    mov bx, dx                  ; hold x2/y2); gfx_fill clips and back-
    call gfx_fill               ; buffer-dispatches itself
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_hline - horizontal line in [gfx_color], inclusive endpoints
; in:       AX = x1, BX = x2, DX = y  (x1 <= x2)
; out:      -
; clobbers: flags
;
; A 1-row solid rect: gfx_fill clips and adapter-dispatches itself.
; (The former dedicated VRAM loop was retired for the code budget -
; gfx_fill produces identical pixels for one row.)
; -----------------------------------------------------------------------------
gfx_hline:
    push bx
    push cx
    mov cx, bx                  ; rect (x1,y)-(x2,y)
    mov bx, dx
    call gfx_fill
    pop cx
    pop bx
    ret


; =============================================================================
; SPEC.md 5.6.7 - the RESUMABLE walk
;
; gfx_line answers "draw this whole line". These two answer "draw the next N
; pixels of this line", and the difference is what lets a caller ERASE
; exactly what it drew.
;
; The problem they solve: a missile trail is DRAWN a couple of pixels a frame
; and ERASED as one long line, and Bresenham over the whole line does not
; visit the union of the per-frame segments - so 5.6.5 dilated the erase by a
; pixel either side and hoped. That is an approximation in BOTH directions:
; it can still miss (leaving the dashes 48's first note is about) and it
; erases pixels the trail never drew, out of whatever was underneath. It also
; costs three walks, measured at 500us a trail pixel on a 4.77MHz machine
; (PERFORMANCE.md Set 4) - the largest remaining item in that game.
;
; A caller that carries the walk's own state has neither problem. The draw
; advances it; the erase replays it from a saved copy. Same recurrence, same
; pixels, one walk, and the caller may stop and resume anywhere - which is
; also what lets an erase be SPREAD over frames instead of landing whole.
;
; THE WALK RUNS IN THE CALLER'S DIRECTION. gfx_line normalises downward so
; its pixel set is a property of the endpoint PAIR (5.6.2); this cannot,
; because the caller is drawing outward from a fixed point and needs step 1
; to be next to it. Order therefore matters here and gfx_prevrow exists for
; it. Two calls with the same endpoints agree by construction, which is all
; a draw/erase pair needs.
; =============================================================================

; -----------------------------------------------------------------------------
; gfx_linit - set a walk state up for the line (x1,y1)-(x2,y2)
; in:  AX = x1, BX = y1, CX = x2, DX = y2; ES:DI = a GLS_SZ-byte block
; out: the block; preserves every register
; -----------------------------------------------------------------------------
gfx_linit:
    push ax
    push bx
    push cx
    push dx
    mov [es:di + GLS_X], ax
    mov [es:di + GLS_Y], bx
    mov word [es:di + GLS_SX], 1
    sub cx, ax
    jns .dxok
    neg cx
    mov word [es:di + GLS_SX], -1
.dxok:
    mov [es:di + GLS_DX], cx
    mov word [es:di + GLS_SY], 1
    sub dx, bx
    jns .dyok
    neg dx
    mov word [es:di + GLS_SY], -1
.dyok:
    mov [es:di + GLS_DY], dx
    sub cx, dx                  ; err = dx - dy, exactly gfx_line's
    mov [es:di + GLS_ERR], cx
    jmp kret_dx

; -----------------------------------------------------------------------------
; gfx_lstep - draw the next CX pixels of the walk in [gfx_color]
; in:  ES:DI = the block, CX = pixels (0 legal, does nothing)
; out: the block advanced; preserves every register; caller holds the gfx lock
;
; The block is STAGED into kernel scratch and written back, rather than being
; addressed through ES throughout - 20.8 rule 3, and here it is forced as
; well as preferred: the mono walk needs ES for the framebuffer.
;
; Each call plots the CURRENT point first and advances after, so N then M
; pixels are exactly the N+M one call would have drawn. That property is the
; whole contract.
; -----------------------------------------------------------------------------
gfx_lstep:
    CURUNLAZY                   ; ...and so does the SCALAR walk. gfx_lstepv had
                                ; this and the twin below it did not, over the
                                ; same gfx_ls_one body: on mono the walk
                                ; read-modify-writes the framebuffer directly,
                                ; so with the region disarmed there is nothing
                                ; else in the path that could spend gfx_lock's
                                ; promise - the line draws over an arrow still
                                ; on screen and gfx_unlock then paints the
                                ; stale save-under back over it (SPEC.md 7.1.4)
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    or cx, cx
    jz .out
    call gfx_ls_ink
    call gfx_ls_one
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_lstepv - step MANY walks in one call (SPEC.md 5.6.8)
; in:  ES:DI = an array of CX descriptors, each `dw block_offset, pixels`,
;      the blocks in ES too; CX = 0 legal, does nothing
; out: every block advanced; preserves every register; caller holds the lock
;
; Identical pixels to CX separate gfx_lstep calls - it IS those calls, with
; the arriving done once. That is the whole point: a walk step draws two or
; three pixels and PERFORMANCE.md Part 2 prices the arriving at ~756us, so a
; caller with eight live lines was paying eight floors a frame to move about
; twenty pixels. A descriptor with a zero count is legal and is skipped, so a
; caller may build the array once and vary what it asks for.
; -----------------------------------------------------------------------------
gfx_lstepv:
    CURUNLAZY                   ; ...and so does the resumable walk
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    jcxz .out
    call gfx_ls_ink             ; the pen is the caller's, and one for all
    mov si, di                  ; SI = the descriptor being read
.each:
    mov di, [es:si]             ; the block...
    mov ax, [es:si+2]           ; ...and how far to take it
    or ax, ax
    jz .next
    push cx
    push si
    mov cx, ax
    call gfx_ls_one
    pop si
    pop cx
.next:
    add si, 4
    loop .each
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gfx_ls_ink - resolve [gfx_color] to the 1bpp class once (00 / FF / 01)
; clobbers AX
gfx_ls_ink:
    mov al, [gfx_color]
    call gfx_ink
    mov [gfx_ln_ink], al
    ret

; gfx_ls_one - stage the block at ES:DI in, walk CX pixels, stage it back
; in:  ES:DI = the caller's block, CX = pixels (non-zero), [gfx_ln_ink] set
; clobbers AX/BX/DX/SI/DI/BP/ES; CX is spent
gfx_ls_one:
    mov [gfx_ls_cseg], es       ; --- stage the caller's block in. Its SEGMENT
    mov [gfx_ls_coff], di       ; has to be remembered: the walk needs ES for
    push cx                     ; the framebuffer, so it cannot stay there
    push si
    push ds
    pop ax                      ; AX = KERNEL_SEG
    mov si, di
    push es
    pop ds                      ; DS:SI = the caller's block
    mov es, ax
    mov di, gfx_ls_x            ; ES:DI = ours
    mov cx, GLS_SZ / 2
    cld
    rep movsw
    mov ds, ax                  ; DS = KERNEL_SEG again
    pop si
    pop cx

%ifdef KERN_BIG
    ; --- SPEC.md 39.14.5: the walk goes onto the display holding its CURRENT
    ; point, and the block is handed back in VIRTUAL coordinates.
    ;
    ; It is the staged copy that is translated, never the caller's: SPEC.md
    ; 48.14 has Missile Command computing with the block's x and y itself, and
    ; 5.6.7's replay-to-erase depends on the state meaning the same thing on
    ; every call. A block left display-local would compound the translation on
    ; the next step and put the caller's arithmetic in a space nothing else
    ; uses. This is the whole of what was missing: the walk wrote the primary's
    ; framebuffer at virtual coordinates, so a trail on the second display drew
    ; at the wrong offset on the FIRST one and stopped dead at its edge.
    cmp byte [vid_ndisp], 1
    jbe .nodisp
    push ax
    push bx
    mov ax, [gfx_ls_x]
    mov bx, [gfx_ls_y]
    call gfx_disp_enter         ; ...and it cannot fail: the dead zone answers
    mov [gfx_ls_x], ax          ; with the primary
    mov [gfx_ls_y], bx
    pop bx
    pop ax
.nodisp:
%endif
    cmp byte [vid_mono], 0         ; 5.6.4's split: mono gets the per-pixel walk,
    je .slow                    ; VGA is the fast machine and gets gfx_pixel
    cmp byte [vid_planes], 1
    jne .slow
    call gfx_lstep_mono
    jmp short .back
.slow:
    call gfx_lstep_slow
.back:
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1     ; ...and back into virtual space before the
    jbe .nodisp2                ; caller ever sees it
    push ax
    mov ax, [vid_ox]
    add [gfx_ls_x], ax
    mov ax, [vid_oy]
    add [gfx_ls_y], ax
    pop ax
    dec byte [gfx_dnest]
.nodisp2:
%endif
    mov si, gfx_ls_x            ; --- and back out, to where it came from
    mov es, [gfx_ls_cseg]
    mov di, [gfx_ls_coff]
    mov cx, GLS_SZ / 2
    cld
    rep movsw
    ret

; gfx_lstep_slow - one gfx_pixel a step. VGA's planar path, and any adapter
; whose renderer is not the 1bpp one; CX = steps.
gfx_lstep_slow:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, cx                  ; the count moves out of CX: gfx_pixel takes
.px:                            ; its x THERE
    mov cx, [gfx_ls_x]
    mov dx, [gfx_ls_y]
    call gfx_pixel              ; clips to the region and the screen itself
    call gfx_ls_adv
    dec si
    jnz .px
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gfx_ls_adv - one Bresenham step of the staged state. Clobbers AX only.
gfx_ls_adv:
    push ax
    mov ax, [gfx_ls_err]
    add ax, ax                  ; e2 = 2*err
    mov [gfx_ls_e2], ax
    mov ax, [gfx_ls_dy]
    neg ax
    cmp [gfx_ls_e2], ax         ; e2 > -dy: step x
    jle .noxs
    mov ax, [gfx_ls_err]
    sub ax, [gfx_ls_dy]
    mov [gfx_ls_err], ax
    mov ax, [gfx_ls_x]
    add ax, [gfx_ls_sx]
    mov [gfx_ls_x], ax
.noxs:
    mov ax, [gfx_ls_e2]
    cmp ax, [gfx_ls_dx]         ; e2 < dx: step y
    jge .noys
    mov ax, [gfx_ls_err]
    add ax, [gfx_ls_dx]
    mov [gfx_ls_err], ax
    mov ax, [gfx_ls_y]
    add ax, [gfx_ls_sy]
    mov [gfx_ls_y], ax
.noys:
    pop ax
    ret


; gfx_lstep_mono - the 1bpp per-pixel walk, CX steps. Registers: SI = x,
; DX = y, BP = err, DI = framebuffer offset, BL = the bit mask, ES = the
; framebuffer. dx/dy/sx/sy stay in memory, as they do in gfx_line_mono.
gfx_lstep_mono:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [vid_rseg]          ; [gfx_ln_ink] is the caller's - gfx_ls_ink
    mov si, [gfx_ls_x]
    mov dx, [gfx_ls_y]
    mov bp, [gfx_ls_err]
    call gfx_ls_box             ; the rect this walk starts inside - AFTER
    call gfx_ls_addr            ; SI/DX are loaded, because it reads them
.px:
    cmp si, [gfx_ls_bx1]        ; still in the resolved rect?
    jl .miss
    cmp si, [gfx_ls_bx2]
    jg .miss
    cmp dx, [gfx_ls_by1]
    jl .miss
    cmp dx, [gfx_ls_by2]
    jle .draw
.miss:
    call gfx_ls_box             ; ...no: find the one that holds it, if any
    cmp si, [gfx_ls_bx1]
    jl .step
    cmp si, [gfx_ls_bx2]
    jg .step
    cmp dx, [gfx_ls_by1]
    jl .step
    cmp dx, [gfx_ls_by2]
    jg .step
.draw:
    mov al, [gfx_ln_ink]
    cmp al, 1                   ; the dither class: lit where (x+y) is even
    jne .solid
    mov ax, si
    xor ax, dx
    and al, 1
    dec al
.solid:
    and al, bl
    mov ah, bl
    not ah
    and ah, [es:di]
    or  ah, al
    mov [es:di], ah
.step:
    mov ax, bp
    add ax, ax
    mov [gfx_ls_e2], ax
    mov ax, [gfx_ls_dy]
    neg ax
    cmp [gfx_ls_e2], ax         ; e2 > -dy: step x
    jle .noxs
    sub bp, [gfx_ls_dy]
    cmp word [gfx_ls_sx], 0
    jl .xleft
    inc si
    shr bl, 1
    jnz .noxs
    mov bl, 0x80
    inc di
    jmp short .noxs
.xleft:
    dec si
    shl bl, 1
    jnz .noxs
    mov bl, 0x01
    dec di
.noxs:
    mov ax, [gfx_ls_e2]
    cmp ax, [gfx_ls_dx]         ; e2 < dx: step y, EITHER WAY (5.6.7)
    jge .noys
    add bp, [gfx_ls_dx]
    cmp word [gfx_ls_sy], 0
    jl .yup
    inc dx
    call gfx_nextrow
    jmp short .noys
.yup:
    dec dx
    call gfx_prevrow
.noys:
    dec cx
    jnz .px
    mov [gfx_ls_x], si          ; hand the advanced state back
    mov [gfx_ls_y], dx
    mov [gfx_ls_err], bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gfx_ls_addr - DI = the framebuffer byte for (SI,DX), BL = its bit
; clobbers AX/CX, and DI/BL are outputs
gfx_ls_addr:
    push cx                     ; CX is the caller's STEP COUNT
    push dx
    mov ax, dx
    call gfx_rowbase            ; clobbers AX/CX/DX, preserves BX
    mov di, ax
    mov ax, si
    mov cl, 3
    sar ax, cl                  ; ARITHMETIC: a walk entering the screen from
    add di, ax                  ; the LEFT has x < 0 here, and `shr` puts that
                                ; 8,190 bytes along the row instead of one
                                ; byte before it. The address is not drawn
                                ; through until x reaches the box, and this is
                                ; what makes it right when it does - the bit
                                ; mask below already was, x & 7 being the
                                ; correct bit for any x
    mov cx, si
    and cl, 7
    mov bl, 0x80
    shr bl, cl
    pop dx
    pop cx
    ret

; gfx_ls_box - the clip rect containing (SI,DX), into gfx_ls_bx1..by2. With
; the region disarmed that is the screen; with no rect containing the point
; it is an EMPTY box, so the caller skips the pixel and re-resolves at the
; next one. Preserves every register.
gfx_ls_box:
    push ax
    push bx
    push cx
    push di
    cmp word [wm_clip_n], 0
    jne .armed
    xor ax, ax
    mov [gfx_ls_bx1], ax
    mov [gfx_ls_by1], ax
    mov ax, [vid_cwm1]
    mov [gfx_ls_bx2], ax
    mov ax, [vid_chm1]
    mov [gfx_ls_by2], ax
    jmp .out                    ; NOT `jmp short`: 39.14.9's virtual-space
                                ; scan put the armed branch out of reach
.armed:
%ifdef KERN_BIG
    ; SPEC.md 39.14.9: THE POINT IS DISPLAY-LOCAL HERE AND wm_clip_tab IS
    ; VIRTUAL. gfx_ls_one resolved the walk's display and translated before
    ; the walk ran, so the scan has to be done in virtual space and the rect
    ; it finds has to come back translated - which is 39.14.1's "below
    ; GFXCLIP and not above it" in the one place the hook could not be put
    ; below it. Untranslated, display 0 works (virtual IS local there) and
    ; every other display matches NO rect, takes the empty box below, and
    ; draws not one pixel while the walk advances normally.
    ; Both terms are 0 on display 0, so a one-display machine pays two adds.
    mov ax, si
    add ax, [vid_ox]
    mov [gfx_ls_vx], ax
    mov ax, dx
    add ax, [vid_oy]
    mov [gfx_ls_vy], ax
%else
    mov [gfx_ls_vx], si
    mov [gfx_ls_vy], dx
%endif
    mov bx, wm_clip_tab
    mov cx, [wm_clip_n]
.scan:
    mov ax, [bx + WCR_X1]
    cmp [gfx_ls_vx], ax
    jl .next
    mov ax, [bx + WCR_X2]
    cmp [gfx_ls_vx], ax
    jg .next
    mov ax, [bx + WCR_Y1]
    cmp [gfx_ls_vy], ax
    jl .next
    mov ax, [bx + WCR_Y2]
    cmp [gfx_ls_vy], ax
    jg .next
    ; ...and the survivor comes back in the DISPLAY's coordinates, clamped to
    ; the display: a clip rect may span the seam, and a box reaching past this
    ; card's last column would have gfx_lstep_mono addressing off the end of
    ; its own framebuffer.
    mov ax, [bx + WCR_X1]
    call gfx_ls_lx
    mov [gfx_ls_bx1], ax
    mov ax, [bx + WCR_Y1]
    call gfx_ls_ly
    mov [gfx_ls_by1], ax
    mov ax, [bx + WCR_X2]
    call gfx_ls_lx
    cmp ax, [vid_cwm1]
    jle .x2ok
    mov ax, [vid_cwm1]
.x2ok:
    mov [gfx_ls_bx2], ax
    mov ax, [bx + WCR_Y2]
    call gfx_ls_ly
    cmp ax, [vid_chm1]
    jle .y2ok
    mov ax, [vid_chm1]
.y2ok:
    mov [gfx_ls_by2], ax
    jmp short .out
.next:
    add bx, WCR_SZ
    loop .scan
    mov word [gfx_ls_bx1], 1    ; nothing holds it: an empty box, so every
    mov word [gfx_ls_bx2], 0    ; later pixel re-resolves rather than
    mov word [gfx_ls_by1], 1    ; inheriting a stale rect
    mov word [gfx_ls_by2], 0
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; gfx_ls_lx / gfx_ls_ly - one virtual coordinate into the ACTIVE display's,
; floored at 0. AX in, AX out; flags clobbered. Two names rather than one with
; an argument because each has exactly one origin to subtract and the caller
; is a straight run of four.
gfx_ls_lx:
%ifdef KERN_BIG
    sub ax, [vid_ox]
    jns .ok
    xor ax, ax
.ok:
%endif
    ret

gfx_ls_ly:
%ifdef KERN_BIG
    sub ax, [vid_oy]
    jns .ok
    xor ax, ax
.ok:
%endif
    ret


; -----------------------------------------------------------------------------
%ifdef KERN_BIG
; SPEC.md 5.6.4.1's three facts, in BH: which axis is the major one, whether
; the ink sets bits or clears them, and which way x runs. They pick the walk.
LF_STEEP    equ 1
LF_WHITE    equ 2
LF_XPOS     equ 4

; ...and the length below which the walk is not worth arriving at. The setup
; is ~1,700 cycles against the general walk's ~600, and a pixel is 152 against
; 723 (PERFORMANCE.md Set 69), so the two cross at about three pixels. Under
; it the general walk is CHEAPER and keeps the line, which is what stops this
; being a win on average and a regression on the short case.
LF_MINMAJ   equ 4

; GFX_LFWALK - one drawn run of SPEC.md 5.6.4.1's walk, and the whole reason
; there are eight of them: on an 8088 the three things that vary here are an
; OPCODE, a BRANCH CONDITION and an inc/dec, none of which a register can hold.
;
;   %1  1 = y-major (steep), 0 = x-major (shallow)
;   %2  1 = x increasing, 0 = x decreasing
;   %3  1 = white ink, 0 = black
;
; registers, on entry and throughout:
;   AX = d                  BL = the bit mask, INVERTED when the ink is black
;   DX = 2 * major delta    DI = the framebuffer byte
;   SI = 2 * minor delta    BP = vid_rowadd
;   CX = pixels to draw     ES = the framebuffer
;
; THE MASK ROTATES, IT DOES NOT SHIFT, and that is what lets one shape serve
; the inverted mask too: `ror`/`rol` put the leaving bit in CF, so the byte
; boundary is a carry rather than a zero. Going right the plain mask leaves a
; 1 exactly at 0x01 and the inverted one leaves a 0 exactly at 0xFE - the same
; test, read the other way round, which is the only thing %3 changes besides
; `or` against `and`.
; -----------------------------------------------------------------------------
%macro GFX_LFWALK 3
  %if %3
    %define LFOP or
    %define LFNW jnc                ; plain mask: the boundary IS the carry
  %else
    %define LFOP and
    %define LFNW jc                 ; inverted mask: the boundary is its absence
  %endif
  %if %2
    %define LFROT ror
    %define LFADJ inc di
  %else
    %define LFROT rol
    %define LFADJ dec di
  %endif
  %if %1
%%px:
    LFOP [es:di], bl
    or ax, ax                       ; d > 0: the minor axis steps
    jg %%xs
%%noxs:
    add ax, si                      ; d += 2*minor, and one row down - the
    add di, bp                      ; major axis steps every pixel, untested
    test di, [cs:vid_wrapbit]       ; gfx_nextrow, inlined
    jnz %%wrap
%%back:
    loop %%px
    clc
    ret
%%xs:
    sub ax, dx
    LFROT bl, 1
    LFNW %%noxs
    LFADJ
    jmp short %%noxs
%%wrap:
    add di, [cs:vid_wrapfix]
    jmp short %%back
  %else
%%px:
    LFOP [es:di], bl
    or ax, ax
    jg %%ys
%%noys:
    add ax, si
    LFROT bl, 1
    LFNW %%back
    LFADJ
%%back:
    loop %%px
    clc
    ret
%%ys:
    sub ax, dx
    add di, bp
    test di, [cs:vid_wrapbit]
    jnz %%wrap
    jmp short %%noys
%%wrap:
    add di, [cs:vid_wrapfix]
    jmp short %%noys
  %endif
  %undef LFOP
  %undef LFNW
  %undef LFROT
  %undef LFADJ
%endmacro
%endif


; -----------------------------------------------------------------------------
; gfx_line - an arbitrary-angle line in [gfx_color] (SPEC.md 5.6)
;
; in:       AX = x1, BX = y1, CX = x2, DX = y2, inclusive, absolute screen
;           coordinates. SI = 0 thin / 1 dilated (SPEC.md 5.6.5). Pen in
;           [gfx_color]; caller holds the gfx lock.
; out:      -
; clobbers: flags
;
; It does NOT replace the area primitives and defers to them: a fill writes
; eight pixels a store on 1bpp where a walk writes one, so a long axis-
; aligned run is gfx_fill's job whatever the angle of the argument (SPEC.md
; 5.6.1, crossover ~27px). The two compares below are what let a caller hand
; this routine any pair of points, degenerate ones included.
;
; The walk always runs DOWNWARD (SPEC.md 5.6.2). Bresenham is not symmetric
; under endpoint swap, so a line drawn as (A,B) and erased as (B,A) leaves a
; scatter behind; normalising makes the pixel set a property of the pair. It
; is also what lets the mono loop step rows with gfx_nextrow, which only goes
; down, instead of a gfx_rowbase per pixel.
; -----------------------------------------------------------------------------
gfx_line:
    call cur_unlazy              ; walks pixels, not clipped rects: 7.1.4
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                     ; 5.6.6's three-column walk uses it as a plain
    push es                     ; register - never as an address base
    GFXDENTERR                  ; the walk goes onto the display holding the
                                ; FIRST endpoint and is cut at that display's
                                ; edge, exactly as it is cut at a screen edge
                                ; (SPEC.md 39.14.2). Above the axis-aligned
                                ; dispatch, so .hline/.vline reach gfx_fill
                                ; with LOCAL coordinates and the nesting count
                                ; stops it translating them again
    cmp bx, dx                  ; an axis-aligned pair belongs in the AREA
    je .hline                   ; primitive, whoever asked for a line - and
    cmp ax, cx                  ; it needs no dilation either, its segments
    je .vline                   ; being on the same lattice as the whole line
    cmp bx, dx                  ; SIGNED: y1 <= y2, or swap both ends
    jle .ord
    xchg ax, cx
    xchg bx, dx
.ord:
    mov [gfx_ln_x1], ax
    mov [gfx_ln_y1], bx
    mov [gfx_ln_x2], cx
    mov [gfx_ln_y2], dx
    sub dx, bx                  ; dy = y2 - y1, and cannot be negative now
    mov [gfx_ln_dy], dx
    mov word [gfx_ln_sx], 1
    sub cx, ax                  ; dx = |x2 - x1|, sx = its sign
    jns .dxok
    neg cx
    mov word [gfx_ln_sx], -1
.dxok:
    mov [gfx_ln_dx], cx
    sub cx, dx                  ; err = dx - dy
    mov [gfx_ln_err], cx

    mov word [gfx_ln_fdx], 0    ; SPEC.md 5.6.5: SI = 1 dilates by a pixel
    mov word [gfx_ln_fdy], 0    ; either side of the MINOR axis - three passes
    mov word [gfx_ln_np], 1     ; at the same dx/dy/err, so only the start
    mov byte [gfx_ln_wide], 0   ; point moves
    or si, si
    jz .passes
    mov word [gfx_ln_np], 3
    mov ax, [gfx_ln_dx]
    cmp ax, [gfx_ln_dy]
    jb .fatx
    mov word [gfx_ln_fdy], 1    ; shallow: the minor axis is y
    dec word [gfx_ln_y1]
    dec word [gfx_ln_y2]
    jmp short .passes
.fatx:
    mov word [gfx_ln_fdx], 1    ; steep: the minor axis is x
    dec word [gfx_ln_x1]
    dec word [gfx_ln_x2]

    ; SPEC.md 5.6.6: on 1bpp, a STEEP dilation is three horizontally adjacent
    ; pixels a step - the same row, usually the same BYTE - and the three
    ; passes share dx, dy and err, so pass k visits exactly (x_i + k-1, y_i).
    ; One walk with a three-bit mask is therefore the identical pixel set at a
    ; third of the Bresenham. Solid ink only: the dither class alternates with
    ; x, so the neighbours never carry the centre's bit.
%ifdef GFX_VGA
    cmp byte [vid_mono], 0         ; the same two tests gfx_line_raw makes -
    je .passes                  ; and gated the same way: without a VGA in
    cmp byte [vid_planes], 1    ; the build both are constants
    jne .passes
%endif
    mov al, [gfx_color]
    call gfx_ink
    cmp al, 1
    je .passes
%ifdef KERN_BIG
    call gfx_lf_wide3           ; SPEC.md 5.6.6.1: ...and on this kernel the
    jnc .passes                 ; wide walk is the FALLBACK, not the win
%endif
    mov byte [gfx_ln_wide], 1
    mov word [gfx_ln_np], 1
    inc word [gfx_ln_x1]        ; undo the shift: the mask spans both sides
    inc word [gfx_ln_x2]
.passes:
    cmp word [wm_clip_n], 0     ; SPEC.md 5.6.3: one pass per clip rect, the
    jne .clipped                ; gfx_clip_run idiom. Clipping the ENDPOINTS
    xor ax, ax                  ; would move the lattice and the erase would
    mov [gfx_ln_cx1], ax        ; then miss; disarmed, one pass at the screen
    mov [gfx_ln_cy1], ax        ; edge, which is also where this gets its
    mov ax, [vid_cwm1]          ; screen clipping
    mov [gfx_ln_cx2], ax
    mov ax, [vid_chm1]
    mov [gfx_ln_cy2], ax
    call gfx_line_raw
    jmp short .nextpass
.clipped:
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
.frag:
    mov ax, [si+WCR_X1]         ; the rects are disjoint, so no pixel of the
    mov bx, [si+WCR_Y1]         ; line is ever written twice
    mov cx, [si+WCR_X2]
    mov dx, [si+WCR_Y2]
%ifdef KERN_BIG
    cmp byte [gfx_dnest], 0     ; the fragments are VIRTUAL and the endpoints
    je .fv                      ; went display-local at GFXDORG, so each
    sub ax, [vid_ox]            ; fragment follows them onto the display -
    sub cx, [vid_ox]            ; gfx_clip_run's own idiom, which this loop
    sub bx, [vid_oy]            ; replicated WITHOUT the translation: a local
    sub dx, [vid_oy]            ; line clipped against virtual rects meets
.fv:                            ; none of them on display 1 and vanishes
%endif
    mov [gfx_ln_cx1], ax
    mov [gfx_ln_cy1], bx
    mov [gfx_ln_cx2], cx
    mov [gfx_ln_cy2], dx
    push si
    push di
    call gfx_line_raw
    pop di
    pop si
    add si, WCR_SZ
    dec di
    jnz .frag
.nextpass:
    dec word [gfx_ln_np]
    jz .out
    mov ax, [gfx_ln_fdx]        ; the next parallel pass, one pixel across
    add [gfx_ln_x1], ax
    add [gfx_ln_x2], ax
    mov ax, [gfx_ln_fdy]
    add [gfx_ln_y1], ax
    add [gfx_ln_y2], ax
    jmp .passes
.out:
    GFXDLEAVE
    jmp kret_es
.hline:
    cmp ax, cx                  ; gfx_hline wants x1 <= x2 in AX/BX, y in DX
    jle .hord
    xchg ax, cx
.hord:
    mov bx, cx
    call gfx_hline
    jmp short .out
.vline:
    cmp bx, dx                  ; gfx_vline wants x in AX, y1/y2 in BX/DX
    jle .vord
    xchg bx, dx
.vord:
    call gfx_vline
    jmp short .out

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; gfx_lf_wide3 - would SPEC.md 5.6.4.1's walk take all THREE passes of this
;                dilated steep line?
;
; in:       the staged line, with x1/x2 already shifted one column LEFT for
;           pass 0, and 5.6.6's ink and adapter tests already passed
; out:      CF clear = yes, so 5.6.6's wide walk is not wanted
;           CF set   = not sure; take the wide walk
; clobbers: AX, BX, flags
;
; SPEC.md 5.6.6.1. 5.6.6's wide walk was an optimisation against 5.6.4's
; general loop and it still is - but three of 5.6.4.1's passes beat it, and
; not slightly: on a 193-pixel trail erase, the shape SPEC.md 48 actually
; draws, the wide walk is 40.5 ms and three fast passes are 20.3. So the wide
; walk is now what happens when the fast walk would REFUSE a pass, and the
; only question here is which.
;
; IT IS CONSERVATIVE IN THE SAFE DIRECTION. Both answers draw the identical
; pixels - 5.6.6's own argument is that its walk IS those three passes - so a
; wrong guess costs time and never a pixel, and "not sure" is spelled CF set.
; That is why a fragmented region and a second display are declined rather
; than reasoned about: the fast walk's box is a clip rect translated per
; fragment, and reproducing that rule here would be keeping a copy of
; 5.6.4.1's eligibility in a second place, which is the thing that drifts.
; -----------------------------------------------------------------------------
gfx_lf_wide3:
    cmp word [gfx_ln_dy], LF_MINMAJ ; too short to pay for the setup, three
    jl .no                          ; times over
    cmp byte [vid_ndisp], 1         ; 39.14: a fragment on a second display is
    ja .no                          ; translated, and this is not
    mov ax, [gfx_ln_x1]             ; the span all three passes together cover
    mov bx, [gfx_ln_x2]
    cmp ax, bx
    jle .ord
    xchg ax, bx
.ord:
    inc bx                      ; ...pass 2 being two columns right of 0
    inc bx
    cmp word [wm_clip_n], 0
    jne .armed
    or ax, ax                       ; disarmed: the box is the screen
    js .no
    cmp bx, [vid_cwm1]
    jg .no
    clc
    ret
.armed:
    cmp word [wm_clip_n], 1         ; one rect: the box is that rect, and the
    jne .no                         ; whole line meets it or none of it does
    cmp ax, [wm_clip_tab + WCR_X1]
    jl .no
    cmp bx, [wm_clip_tab + WCR_X2]
    jg .no
    clc
    ret
.no:
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_line_raw - internal: one pass over the line, drawing only inside the
;                box in gfx_ln_c*. The Bresenham state is already staged.
; clobbers: everything but the segment registers
gfx_line_raw:
%ifdef GFX_VGA
    cmp byte [vid_mono], 0         ; SPEC.md 5.6.4: mono IS the 4.77MHz machine
    je gfx_line_runs            ; and gets the per-pixel loop; VGA is the fast
    cmp byte [vid_planes], 1    ; one and gets major-axis runs, still fewer
    jne gfx_line_runs           ; calls than the app-side horizontal-only
%endif                          ; coalescing this replaces. Both tests are
                                ; constants without a VGA in the build - a
                                ; 1bpp adapter is the only kind there is
%ifdef KERN_BIG
    call gfx_line_fast          ; SPEC.md 5.6.4.1 - the same pixels, 4.9x, when
    jc gfx_line_mono            ; the line's shape lets the box be spent up
    ret                         ; front. CF set = it refused; walk it the long
                                ; way, which is what stays below. NOT ON
                                ; kern_small (5.6.4.4): it is 686 bytes of
                                ; .text and that machine is short of the thing
                                ; it would be spent from
%else
    jmp gfx_line_mono           ; 5.6.4.4: the general walk, and NOT a fall
                                ; through - gfx_lm_pre sits between this entry
                                ; and it. With both gates above closed the
                                ; entry fell into gfx_lm_pre, which steps the
                                ; walk to the box's top edge and RETURNS, so
                                ; every gfx_line on kern_small drew nothing at
                                ; all: 0 lit pixels for a (100,66)-(180,94)
                                ; against 81 on kern_big, measured through
                                ; the debugger on a CGA 5150
%endif
; -----------------------------------------------------------------------------
; gfx_lm_pre - advance the walk to the box's top edge, drawing nothing
;
; in:       SI = x, DX = y, CX = err       out: the same, at y >= [gfx_ln_cy1]
;                                               (or at y2, if the line ends
;                                               above the box)
; clobbers: AX, flags
;
; SPEC.md 5.6.4.3. THE FRAMEBUFFER HAS NO ROW ABOVE ZERO and, on a banked
; adapter, not even the arithmetic for one: `gfx_rowbase(-5)` is not row 0
; minus a stride, it is bank 3 of a 16-bit product that wrapped, and stepping
; five rows down from it does not arrive at row 0 - measured, it arrives 90
; bytes past it, so a line entering the screen from the top drew four rows low
; on Hercules with nothing to say so. The walk skipped those pixels correctly
; and addressed the rest wrongly.
;
; So the fix is not in the addressing, it is here: every pixel above the box
; was going to be skipped anyway, and stepping the state without an address is
; what the walk should have done from the start. It costs the general path one
; call, and a line that starts inside the box - almost all of them - returns
; on the first compare.
; -----------------------------------------------------------------------------
gfx_lm_pre:
    cmp dx, [gfx_ln_cy1]
    jge .out
    cmp dx, [gfx_ln_y2]         ; ...and never past the end of the line
    jge .out
    mov ax, cx                  ; exactly .pixel's step, without the pixel
    add ax, ax
    mov [gfx_ln_e2], ax
    mov ax, [gfx_ln_dy]
    neg ax
    cmp [gfx_ln_e2], ax
    jle .noxs
    sub cx, [gfx_ln_dy]
    add si, [gfx_ln_sx]
.noxs:
    mov ax, [gfx_ln_e2]
    cmp ax, [gfx_ln_dx]
    jge gfx_lm_pre
    add cx, [gfx_ln_dx]
    inc dx
    jmp short gfx_lm_pre
.out:
    ret

; --- the 1bpp per-pixel walk - the GENERAL case (SPEC.md 5.6.4) --------------
gfx_line_mono:
    mov al, [gfx_color]
    call gfx_ink                ; AL = 00 black / FF white / 01 the dither
    mov [gfx_ln_ink], al
    mov es, [vid_rseg]
    mov si, [gfx_ln_x1]         ; SI = x, DX = y, CX = err
    mov dx, [gfx_ln_y1]
    mov cx, [gfx_ln_err]
    call gfx_lm_pre             ; ...to the box's top edge, if it starts above
    call gfx_ls_addr            ; DI = the byte, BL = its bit
.pixel:
    cmp dx, [gfx_ln_cy2]        ; y only ever grows, so past the box's bottom
    jg .done                    ; there is nothing left to draw
    cmp dx, [gfx_ln_cy1]
    jl .step
    cmp byte [gfx_ln_wide], 0   ; SPEC.md 5.6.6
    jne .wide
    cmp si, [gfx_ln_cx1]
    jl .step
    cmp si, [gfx_ln_cx2]
    jg .step
    mov al, [gfx_ln_ink]
    cmp al, 1                   ; the dither class: lit where (x+y) is even
    jne .solid
    mov ax, si
    xor ax, dx
    and al, 1
    dec al                      ; 0 (even) -> FF lit, 1 -> 00
.solid:
    and al, bl                  ; AL = the bit to set, 0 if this ink clears it
    mov ah, bl
    not ah
    and ah, [es:di]             ; one read and one write, not two RMW
    or  ah, al
    mov [es:di], ah
.step:
    cmp si, [gfx_ln_x2]
    jne .go
    cmp dx, [gfx_ln_y2]
    je .done
.go:
    mov ax, cx                  ; e2 = 2 * err; BOTH tests read the same e2,
    add ax, ax                  ; which is what makes it one Bresenham
    mov [gfx_ln_e2], ax
    mov ax, [gfx_ln_dy]
    neg ax
    cmp [gfx_ln_e2], ax         ; e2 > -dy: step x
    jle .noxs
    sub cx, [gfx_ln_dy]
    cmp word [gfx_ln_sx], 0
    jl .xleft
    inc si
    shr bl, 1
    jnz .noxs
    mov bl, 0x80
    inc di
    jmp short .noxs
.xleft:
    dec si
    shl bl, 1
    jnz .noxs
    mov bl, 0x01
    dec di
.noxs:
    mov ax, [gfx_ln_e2]
    cmp ax, [gfx_ln_dx]         ; e2 < dx: step y
    jge .pixel
    add cx, [gfx_ln_dx]
    inc dx
    call gfx_nextrow            ; DI and flags only - CX/SI/DX/BX survive
    jmp .pixel
.done:
    ret

; --- SPEC.md 5.6.6: the three columns x-1, x, x+1 of one row, in one visit ---
; Each column is clipped on its own, exactly as its own pass would have been.
; At most ONE of them can fall in a neighbouring byte, because the centre bit
; cannot be both 0x80 and 0x01 - so BH carries that bit and BP its direction.
.wide:
    xor ah, ah                  ; AH = the bits of [es:di] this row owes
    mov bh, 0                   ; BH = a bit in the neighbouring byte, 0 = none
    cmp si, [gfx_ln_cx1]
    jl .wleft
    cmp si, [gfx_ln_cx2]
    jg .wleft
    mov ah, bl
.wleft:
    dec si
    cmp si, [gfx_ln_cx1]
    jl .wright
    mov al, bl
    shl al, 1                   ; 0x80 shifts OUT: the column is in [es:di-1]
    jnz .wlin
    mov bh, 0x01
    mov bp, -1
    jmp short .wright
.wlin:
    or ah, al
.wright:
    inc si
    inc si
    cmp si, [gfx_ln_cx2]
    jg .wmid
    mov al, bl
    shr al, 1                   ; 0x01 shifts OUT: the column is in [es:di+1]
    jnz .wrin
    mov bh, 0x80
    mov bp, 1
    jmp short .wmid
.wrin:
    or ah, al
.wmid:
    dec si                      ; SI back to the centre column
    or ah, ah
    jz .wnb
    mov al, [gfx_ln_ink]        ; solid by construction: 00 or FF
    and al, ah
    not ah
    and ah, [es:di]
    or  ah, al
    mov [es:di], ah
.wnb:
    or bh, bh
    jz .wout
    add di, bp
    mov al, [gfx_ln_ink]
    and al, bh
    not bh
    and bh, [es:di]
    or  bh, al
    mov [es:di], bh
    sub di, bp
.wout:
    jmp .step

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; SPEC.md 5.6.4.1 - the 1bpp walk with its state in REGISTERS
;
; PERFORMANCE.md Set 69 priced the general walk above at 723 cycles a pixel and
; said where every one of them goes: 170 in the per-pixel clip test, ~270 in a
; Bresenham whose every term is a word in .bss, ~134 in `call gfx_nextrow`, and
; 38 in the pixel. Forty instructions a pixel at 17.7 cycles each - and 17.7 is
; what a DIRECT MEMORY OPERAND costs on an 8088, not a slow instruction set.
;
; This is the same rasterisation with none of that. Three things buy it and
; they pay for each other:
;
;   SPLIT BY OCTANT. The major axis then steps EVERY pixel, so it needs no test
;   at all, and the minor axis's whole decision is the sign of one register:
;
;       steep:   d = 2dx - dy;  plot; if d>0 {x+=sx; d-=2dy}; d+=2dx; y++
;       shallow: d = 2dy - dx;  plot; if d>0 {y++;   d-=2dx}; d+=2dy; x+=sx
;
;   That is not an approximation of gfx_line_mono, it IS gfx_line_mono with the
;   invariants folded in - tools/os88linecost.py --model checks the two lay the
;   identical pixel set over 9,480 endpoint pairs, and tests/linetest is the
;   gate on the glass.
;
;   SPEND THE BOX AS AN INTERVAL. 5.6.3 refuses to clip because clipping the
;   ENDPOINTS moves the lattice; this moves neither endpoint. The pixels of a
;   monotone line inside an axis-aligned rect are a CONTIGUOUS range of major-
;   axis steps, so the box becomes a first and a count, computed once. The
;   minor axis is handled by refusing outright (below) rather than by testing.
;
;   INLINE THE ROW STEP. `add di,bp` and one test, against a call and a ret.
;
;   x AND y ARE THEN NOT TRACKED AT ALL - DI and BL carry the position - which
;   is what frees the two registers the interval made spare. The three are one
;   change; keeping the per-pixel test would put dx/dy back in .bss and the
;   modelled result is ~2.1x rather than 4.9x.
;
; WHAT IT REFUSES, and every refusal falls through to the walk above rather
; than drawing anything wrong:
;
;   - 5.6.6's dilated STEEP line, which is a three-column mask walk and a
;     different shape entirely;
;   - 39.4's DITHER ink, which is a per-pixel decision by definition;
;   - a line whose MINOR extent leaves the box. The major axis is clipped
;     exactly, the minor one is all-or-nothing, and that is the whole of the
;     compromise: a line crossing a clip rect's left or right edge (steep) or
;     its top or bottom (shallow) is walked the old way and costs what it
;     always did. Nothing is ever slower than before.
;
; in:       the staged Bresenham gfx_line_raw hands gfx_line_mono - x1/y1,
;           x2/y2, dx, dy, sx - and this pass's box in gfx_ln_c*
; out:      CF clear = drawn (which includes "no pixel of it is in this box").
;           CF set   = refused; the caller walks it.
; clobbers: everything but the segment registers
; -----------------------------------------------------------------------------
gfx_line_fast:
    cmp byte [gfx_ln_wide], 0   ; 5.6.6's three-column walk, not this shape
    jne .refuse
    ; ONE BYTE CARRIES THE THREE FACTS THE LOOPS ARE PICKED BY: which axis is
    ; the major one, whether the ink sets bits or clears them, and which way x
    ; runs. It is settled once here, in .bss, because the setup below needs
    ; every register it has - and it is read into BH for the walk, which is
    ; the one register no loop touches.
    xor bx, bx                  ; ...built in BL, where a bit is `or bl,imm` at
    mov al, [gfx_color]         ; four clocks rather than a read-modify-write
    call gfx_ink                ; on .bss at twenty-two. gfx_ink keeps BX
    cmp al, 1
    je .refuse                  ; the dither is a per-pixel decision (39.4)
    or al, al
    jz .blk
    or bl, LF_WHITE
.blk:
    cmp word [gfx_ln_sx], 0
    jl .xneg
    or bl, LF_XPOS
.xneg:
    mov ax, [gfx_ln_dx]
    cmp ax, [gfx_ln_dy]
    jae .shal

    ; --- y-major: the minor extent is the line's whole x span ---------------
    or bl, LF_STEEP
    mov ax, [gfx_ln_x1]
    mov cx, [gfx_ln_x2]         ; CX and not BX: BL is still the flags
    test bl, LF_XPOS
    jnz .xord
    xchg ax, cx                 ; AX = leftmost, CX = rightmost
.xord:
    cmp ax, [gfx_ln_cx1]
    jl .refuse
    cmp cx, [gfx_ln_cx2]
    jg .refuse
    mov [gfx_ln_flg], bl        ; ...and .span wants BX now
    mov ax, [gfx_ln_cy1]        ; the box, as a range of STEPS: step i is at
    sub ax, [gfx_ln_y1]         ; y1 + i, and 5.6.2 says i only ever grows
    mov bx, [gfx_ln_cy2]
    sub bx, [gfx_ln_y1]
    mov cx, [gfx_ln_dx]         ; minor delta
    mov dx, [gfx_ln_dy]         ; major delta
    jmp short .span
.refuse:
    stc
    ret

    ; --- x-major: ...and the minor extent is the whole y span, which 5.6.2
    ; has already ordered for us --------------------------------------------
.shal:
    mov ax, [gfx_ln_y1]
    cmp ax, [gfx_ln_cy1]
    jl .refuse
    mov ax, [gfx_ln_y2]
    cmp ax, [gfx_ln_cy2]
    jg .refuse
    mov [gfx_ln_flg], bl        ; ...and .span wants BX now
    mov ax, [gfx_ln_cx1]        ; step i is at x1 + sx*i, so walking LEFT the
    sub ax, [gfx_ln_x1]         ; box's interval is the same one mirrored
    mov bx, [gfx_ln_cx2]
    sub bx, [gfx_ln_x1]
    test byte [gfx_ln_flg], LF_XPOS
    jnz .sord
    neg ax
    neg bx
    xchg ax, bx
.sord:
    mov cx, [gfx_ln_dy]
    mov dx, [gfx_ln_dx]

    ; --- the step interval, and the staging both octants share --------------
    ; AX = lo, BX = hi, CX = minor delta, DX = major delta
.span:
    cmp dx, LF_MINMAJ           ; a line too short to pay for the setup goes
    jl .refuse                  ; the old way (5.6.4.2)
    or ax, ax                   ; first = max(0, lo)
    jns .lo
    xor ax, ax
.lo:
    cmp bx, dx                  ; last = min(major, hi)
    jle .hi
    mov bx, dx
.hi:
    mov [gfx_ln_first], ax
    sub bx, ax
    jl .empty                   ; no step of this line is inside this box
    inc bx
    mov [gfx_ln_cnt], bx
    mov si, cx                  ; the staged walk, into the registers the skip
    add si, si                  ; below wants it in - SI = 2 * minor...
    mov [gfx_ln_add2], si
    mov cx, ax                  ; ...CX = the steps to skip...
    mov ax, si
    sub ax, dx                  ; ...AX = d = 2*minor - major...
    add dx, dx                  ; ...DX = 2 * major
    mov [gfx_ln_sub2], dx

    ; --- the steps in front of the box, advanced but not drawn --------------
    ; The state and nothing else: no framebuffer, no mask, no address - so one
    ; loop serves both octants and both inks, at ~35 cycles a skipped step
    ; against the 723 the general walk spends testing each one.
    xor di, di                  ; DI = minor steps taken while skipping
    jcxz .at
.skip:
    or ax, ax
    jle .sn
    sub ax, dx
    inc di
.sn:
    add ax, si
    loop .skip
.at:
    mov [gfx_ln_d], ax          ; ...and gfx_ls_addr is about to take AX

    ; --- the entry pixel ----------------------------------------------------
    mov bh, [gfx_ln_flg]        ; BX is free from here, and BH survives
    mov si, [gfx_ln_x1]         ; gfx_ls_addr, which writes only BL
    mov dx, [gfx_ln_y1]
    mov ax, di
    test bh, LF_STEEP
    jz .esh
    add dx, [gfx_ln_first]      ; steep: y by the whole skip, x by the minor
    jmp short .ex               ; steps the skip actually took
.esh:
    add dx, ax                  ; shallow: the other way round
    mov ax, [gfx_ln_first]
.ex:
    test bh, LF_XPOS
    jnz .exp
    neg ax
.exp:
    add si, ax
    call gfx_ls_addr            ; DI = the byte, BL = the bit
    test bh, LF_WHITE
    jnz .ld
    not bl                      ; black CLEARS, so it carries the mask
.ld:                            ; inverted and plots with `and`
    mov es, [vid_rseg]
    mov bp, [vid_rowadd]
    mov ax, [gfx_ln_d]
    mov dx, [gfx_ln_sub2]
    mov si, [gfx_ln_add2]
    mov cx, [gfx_ln_cnt]
    test bh, LF_STEEP
    jz .lshal
    test bh, LF_XPOS
    jz .lsleft
    test bh, LF_WHITE
    jz .lsrb
    GFX_LFWALK 1, 1, 1
.lsrb:
    GFX_LFWALK 1, 1, 0
.lsleft:
    test bh, LF_WHITE
    jz .lslb
    GFX_LFWALK 1, 0, 1
.lslb:
    GFX_LFWALK 1, 0, 0
.lshal:
    test bh, LF_XPOS
    jz .lhleft
    test bh, LF_WHITE
    jz .lhrb
    GFX_LFWALK 0, 1, 1
.lhrb:
    GFX_LFWALK 0, 1, 0
.lhleft:
    test bh, LF_WHITE
    jz .lhlb
    GFX_LFWALK 0, 0, 1
.lhlb:
    GFX_LFWALK 0, 0, 0
.empty:
    clc
    ret
%endif
%ifdef GFX_VGA


; --- major-axis runs, one gfx_fill_raw each ----------------------------------
; gfx_fill_raw and not gfx_fill: the region has already been spent above, and
; re-entering GFXCLIP per run would clip the same line twice.
gfx_line_runs:
    mov si, [gfx_ln_x1]
    mov dx, [gfx_ln_y1]
    mov cx, [gfx_ln_err]
    mov ax, [gfx_ln_dx]         ; the major axis: x if dx >= dy, else y
    cmp ax, [gfx_ln_dy]
    jae .majx
    mov byte [gfx_ln_majx], 0
    mov [gfx_ln_rs], dx         ; a vertical run starts at this y
    jmp short .step
.majx:
    mov byte [gfx_ln_majx], 1
    mov [gfx_ln_rs], si         ; ...a horizontal one at this x
.step:
    cmp si, [gfx_ln_x2]
    jne .go
    cmp dx, [gfx_ln_y2]
    je .last
.go:
    mov ax, cx
    add ax, ax
    mov [gfx_ln_e2], ax
    mov ax, [gfx_ln_dy]
    neg ax
    cmp [gfx_ln_e2], ax
    jle .noxs
    sub cx, [gfx_ln_dy]
    cmp byte [gfx_ln_majx], 0   ; a MINOR-axis step ends the open run
    jne .xok
    call gfx_line_flush
.xok:
    add si, [gfx_ln_sx]
    cmp byte [gfx_ln_majx], 0
    jne .noxs
    mov [gfx_ln_rs], dx         ; the next vertical run starts here
.noxs:
    mov ax, [gfx_ln_e2]
    cmp ax, [gfx_ln_dx]
    jge .step
    add cx, [gfx_ln_dx]
    cmp byte [gfx_ln_majx], 0
    je .yok
    call gfx_line_flush
.yok:
    inc dx
    cmp byte [gfx_ln_majx], 0
    je .step
    mov [gfx_ln_rs], si
    jmp short .step
.last:                          ; ...AND FALLS THROUGH into gfx_line_flush,
                                ; whose ret is gfx_line's.  It was `call
                                ; gfx_line_flush / ret`, four bytes to reach a
                                ; label six lines down.  The %endif below emits
                                ; nothing, and gfx_line_flush is defined
                                ; OUTSIDE it - so NOTHING MAY BE APPENDED TO
                                ; THIS ARM, and nothing may go between the
                                ; %endif and gfx_line_flush's label
%endif

; gfx_line_flush - internal: the open run, clipped to the box, as one rect.
; in:  SI = x, DX = y (the run's far end), [gfx_ln_rs] its near end
; clobbers: ax, bx, di, flags. SI/CX/DX are the walk's and are preserved.
gfx_line_flush:
    push cx
    push dx
    push si
    cmp byte [gfx_ln_majx], 0
    je .vert
    mov ax, [gfx_ln_rs]         ; horizontal: (rs..x, y..y)
    mov cx, si
    cmp ax, cx
    jle .hord
    xchg ax, cx
.hord:
    mov bx, dx
    jmp short .clip
.vert:
    mov ax, si                  ; vertical: (x..x, rs..y)
    mov cx, si
    mov bx, [gfx_ln_rs]
.clip:
    cmp ax, [gfx_ln_cx1]
    jge .c1
    mov ax, [gfx_ln_cx1]
.c1:
    cmp cx, [gfx_ln_cx2]
    jle .c2
    mov cx, [gfx_ln_cx2]
.c2:
    cmp ax, cx
    jg .out
    cmp bx, [gfx_ln_cy1]
    jge .c3
    mov bx, [gfx_ln_cy1]
.c3:
    cmp dx, [gfx_ln_cy2]
    jle .c4
    mov dx, [gfx_ln_cy2]
.c4:
    cmp bx, dx
    jg .out
    call gfx_fill_raw
.out:
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; gfx_blit4 - draw a block of packed 4bpp pixels (SPEC.md 5.4/42)
;
; in:       ES:SI = source, packed 4 bits per pixel, 2 pixels per byte, the
;                   HIGH nibble leftmost - the layout apps/paint's canvas
;                   already uses, and the one a 16-colour BMP row uses
;           BP    = source stride in bytes (one row to the next)
;           AX    = destination x, BX = destination y (screen coords)
;           CX    = width in pixels (> 0), DX = height in rows (> 0)
; out:      nothing; every register preserved. [gfx_color] is left holding
;           the last colour drawn.
; clobbers: nothing else (flags)
;
; What this is, and what it deliberately is not. It coalesces runs of equal
; pixels and emits one gfx_fill per run - the same thing apps/paint's pt_blit
; did in the package, and therefore the same pixels - but from INSIDE the
; kernel. That is the whole win and it is a large one: since packages own a
; segment (SPEC.md 20.1) every gfx call from one is a FAR call, so a canvas
; repaint that cost N far calls plus N run scans now costs ONE call and a
; near loop. It also means the primitive works on all three adapters, in
; and inside a clip region, without one line of adapter-specific code -
; because gfx_fill already does.
;
; **The run scan compares BYTES, not pixels, and that is the whole speed of
; the SCAN.** A run of colour c is a run of bytes equal to c|c<<4, so
; `repe scasb` walks it two pixels at a time at about 15 clocks a byte -
; roughly seven and a half clocks a pixel. The odd ends are handled by hand:
; the first pixel when the run starts on a low nibble, the last when it ends
; on a high one.
;
; The first version of this routine decoded every pixel individually instead
; - read the byte, test the parity, `shr al, cl` by four, compare, branch -
; and that is 75 to 90 clocks a pixel on an 8086, because a 4-bit shift by CL
; alone is 24 of them. It kept the SHAPE of the optimisation (one call per
; run) and threw away the optimisation. Those two cycle counts are still
; WRITTEN DOWN rather than measured, and the "quarter of a second to over
; two" figure this comment used to give for a 448x280 repaint was derived
; from them; the ratio is the point and the absolute numbers are not
; measurements. What HAS since been measured on the target machine is more
; important than either, and it is below.
;
; **What a blit actually costs, measured on a 4.77MHz 8088 (PERFORMANCE.md
; Part 9): `runs x 0.5 ms`.** Each coalesced run is one gfx_hline, and a
; drawing call on this machine has a ~756us FIXED cost - far call, lock, clip
; test, dispatch, gfx_rowbase - before it draws a pixel. Three routes agree:
; a 64-run solid blit is 441us a call, a 1,024-run striped one 548us, and
; GFX_HLINE 8px through the API 765us. So the pixel count barely enters the
; price at all and **how FLAT the source is decides it**: the same 4,096
; pixels cost 28.2 ms solid and 561 ms in 4-pixel stripes, a 20x spread with
; no pixel of difference. A caller that wants a blit to be cheap makes its
; art flat; a caller that must draw detail should expect ~0.5 ms per colour
; change per row, and budget accordingly.
;
; A plane-parallel VGA fast path would attack exactly that, since it costs
; one write per 8 pixels regardless of flatness - see below.
;
; A plane-parallel VGA fast path (build a byte-per-8-pixels mask per plane
; and write it through the Map Mask) would beat this on detailed pictures and
; lose to it on flat ones, and it would need its own 1bpp
; twins. It is a worthwhile future optimisation and it changes no caller;
; docs/PAINT-NOTES.md records why it was not the first one.
;
; Colour reduction on a 1bpp adapter happens in gfx_fill, per run (SPEC.md
; 39.4), so a 16-colour picture reads as black/dither/white there exactly as
; the rest of the UI does.
;
; ...and a DISPLAY SPLIT is THREE cases now, not one, and the paragraph that
; used to sit here got its own cost wrong twice (SPEC.md 39.14.7.1):
;
;   - the block FITS one display -> the whole-shape hook, and 5.4.1's fast
;     path exactly as a one-card machine has it (39.14.7.1);
;   - it STRADDLES and the seam can be cut -> two halves, each hooked to its
;     own display and each on its own fast path (39.14.7.2). Measured at
;     44.56x against the case below, PERFORMANCE.md Set 112;
;   - it straddles and the cut is refused - an ODD number of pixels left of
;     the seam, which no source pointer can express two pixels to a byte -
;     -> the destination stays VIRTUAL and each run splits itself through
;     gfx_fill's own GFXDISP, which is 39.14.7's mechanism and is what
;     NOBLITCUT=1 restores for all three.
;
; The last of those is not "a slower way of writing a run": it is 5.4.1's
; fast path given up on the WHOLE block, every run of it paying a gfx_fill's
; own arrival. That is why the first two exist. On every one-card machine
; none of it costs anything at all, because the gate short-circuits on
; [vid_ndisp] two instructions in.
; -----------------------------------------------------------------------------
gfx_blit4:
    KFZ 25                      ; a packed-4bpp block (SPEC.md 5.4)
    call cur_unlazy              ; off the clip list (SPEC.md 11.3): 7.1.4
                                ; ...but NOT off the display split, and there
                                ; is deliberately no GFXDENTER here: A RUN
                                ; CARRIES NO SOURCE (SPEC.md 39.14.7). The
                                ; destination stays VIRTUAL the whole way
                                ; down, and each coalesced run is one solid
                                ; gfx_hline that splits itself through
                                ; gfx_fill's own GFXDISP - so a straddling
                                ; block needs no source arithmetic at all.
                                ; Nested (gfx_dnest != 0) the runs take
                                ; GFXDISP's local path, which is what an
                                ; outer whole-shape hook wants.
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov byte [gfx_blit_fst], 0  ; BEFORE the two guards below, or a call that
                                ; draws nothing inherits the last one's answer
                                ; and .out resets a VGA that was never armed
%ifdef GFX_PLANE
    mov byte [gfx_blit_pok], 0  ; ...and the same for SPEC.md 5.4.1.3's, which
                                ; is armed ONLY down the .vga branch and read
                                ; on every run of every path. A clipped blit
                                ; goes straight to .slow and never passes the
                                ; arming, so without this it inherits the last
                                ; call's YES and hands a row to a decoder that
                                ; does not clip - which is the picture drawn
                                ; over the window on top of it
%endif
    or cx, cx
    jz .out
    or dx, dx
    jz .out

%ifdef KERN_BIG
    ; --- THE WHOLE-SHAPE HOOK, TAKEN WHEN THE BLOCK FITS ONE DISPLAY -------
    ; SPEC.md 39.14.7.1. This is the gate 39.14.7 considered and declined,
    ; and the field priced what declining it cost: a two-display machine sent
    ; EVERY blit down .slow, which is not "per-run gfx_disp_run instead of
    ; two compares" - it is giving up 5.4.1's fast path altogether, so each
    ; coalesced run became a full gfx_fill at its own ~756us arrival instead
    ; of a direct write into the framebuffer.
    ;
    ; Only AX/BX need translating: CX and DX are a WIDTH and a HEIGHT, which
    ; no display origin can move. That is why there is no GFXDORG here and
    ; why this costs one macro rather than the "second path through the
    ; routine" 39.14.7 weighed.
    ;
    ; ...AND THE STRADDLING CASE IS NO LONGER "the one that was already
    ; there", which is what this comment used to say and what 39.14.7.2 is
    ; about: it goes to .cut, which cuts the block at the seam and runs this
    ; routine once per half. Only a cut the seam REFUSES - an odd number of
    ; pixels left of it - falls through to the path that was already there.
    mov byte [gfx_blit_hk], 0
    cmp byte [vid_ndisp], 1
    jbe .hkdone                 ; one display: nothing to decide, and this is
                                ; every machine that was already fast
    cmp byte [gfx_dnest], 0
    jne .hkdone                 ; nested: an outer whole-shape hook owns the
                                ; translation and .slow is what it wants
%ifndef NOBLITCUT
    cmp byte [gfx_blit_fd], 0   ; SPEC.md 39.14.7.2: this call is one HALF of a
    jne .hkforce                ; cut, and its display is NAMED
%endif
    push cx
    push dx
    add cx, ax                  ; vid_span_one takes an INCLUSIVE rect and
    dec cx                      ; this routine takes a width and a height
    add dx, bx
    dec dx
    call vid_span_one
    pop dx
    pop cx
%ifdef NOBLITCUT
    jc .hkdone                  ; STRADDLING: leave the destination virtual
                                ; and let each run split itself, which is
                                ; 39.14.7's mechanism and still correct
%else
    jc .cut                     ; STRADDLING: cut it at the seam if it can be
%endif                          ; cut, and only then fall back to 39.14.7's
    mov byte [gfx_blit_hk], 1
    GFXDENTER                   ; ...one display, so the block is local now
.hkdone:
%endif

    mov [gfx_blit_w], cx        ; lock-held scratch, like vga12's rect block:
    mov [gfx_blit_x], ax        ; every drawing entry runs under the gfx lock
    mov [gfx_blit_h], dx        ; (SPEC.md 1 rule 6), so one copy serves
    mov [gfx_blit_sv], si       ; - and AX is the TRANSLATED x by here

    ; --- may a run go straight into the framebuffer? (SPEC.md 5.4.1) -------
    cmp word [wm_clip_n], 0
    je .clipfree
    ; SPEC.md 5.4.1.4 - A REGION IS ARMED, BUT IS IT IN THE WAY? If the whole
    ; block lies inside ONE fragment there is nothing here to clip against and
    ; the fast path writes exactly the pixels the slow one would. 11.3.3's cull
    ; arms a region around every W_PAINT, so without this a Paint canvas gave up
    ; 5.4.1 on EVERY damage repaint and paid a whole gfx_fill per coalesced run:
    ; OS8088.GIF is one 466x110 blit and it measured 19.97 SECONDS on a 4.77MHz
    ; 8088 with a Hercules, 63's dither being a run a pixel
    push ax
    push bx
    push cx
    push dx
    add cx, ax                  ; wm_clip_t1 takes an INCLUSIVE rect where this
    dec cx                      ; routine takes a width and a height
    add dx, bx
    dec dx
    call wm_clip_t1             ; ...and NOT wm_clip_test, whose outward
    pop dx                      ; rounding under the cull is font_char's
    pop cx                      ; question and not this one (11.3.3)
    pop bx
    pop ax
    jc .slow                    ; it crosses a fragment edge: each run has to
                                ; clip itself against the region, which only
                                ; gfx_fill does
.clipfree:
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .dsp1                   ; one display: the fast path as it always was
    cmp byte [gfx_blit_hk], 0   ; two, and we took the hook above: the block
    je .slow                    ; is display-local and one display is current,
                                ; so the fast path is exactly right. Without
                                ; it - straddling, or nested by somebody else
                                ; - a run splits itself through gfx_fill's own
                                ; GFXDISP (SPEC.md 39.14.7)
.dsp1:
%endif
%ifdef GFX_VGA
    cmp byte [vid_mono], 0
    je .vga                     ; (no test at all without a VGA in the build:
%endif                          ; [vid_mono] is 1 by construction there)
    or ax, ax                   ; 1 = THE ROW DECODER (SPEC.md 5.4.1), which
    js .slow                    ; does not clip in x: a left clip advances the
    add ax, cx                  ; source by a pixel count that may be ODD, and
    cmp ax, [vid_cw]            ; that shifts the nibble phase of every pair
    ja .slow                    ; after it. Every caller clips its own blit
    mov byte [gfx_blit_fst], 1
    cmp byte [gfx_pairbuilt], 0
    jne .slow
    call sw_pairbuild           ; once a boot, from gfx_inktab
    jmp short .slow
%ifdef GFX_VGA
.vga:
    mov byte [gfx_blit_fst], 2  ; 2 = the planar SPAN writer, whose Enable
    call vga_sr_on              ; Set/Reset is armed ONCE here and cleared at
                                ; .out: only the colour and the bit mask
                                ; change per run
%ifdef GFX_PLANE
    ; --- ...and MAY A ROW GO PLANAR INSTEAD? (SPEC.md 5.4.1.3) -------------
    ; The run writer is priced per RUN and the row decoder per PIXEL, so the
    ; two are asked per row and this is what the asking needs: a threshold,
    ; and a promise that the block is on screen in x. The decoder does not
    ; clip - it owns whole byte columns - where a run clips itself in
    ; vga_blit_span, so a block hanging off either edge keeps the run path
    ; entire rather than growing a second clipper.
    or ax, ax
    js .pdone                   ; (AX is the TRANSLATED x by here). The flag is
                                ; already 0 from the entry above, so every
                                ; refusal here is a fallthrough
    mov dx, ax
    add dx, cx
    cmp dx, [vid_cw]
    ja .pdone
    cmp cx, 64
    jb .pdone                   ; a narrow block is all edge: four Map Masks
                                ; and a two-byte `rep movsb` cannot beat a
                                ; run, and 64 is where the threshold below
                                ; still has two runs of room in it
    push cx
    mov dx, cx
    mov cl, 5
    shr dx, cl                  ; W/32: the runs a row may spend before the
    pop cx                      ; rest of it goes to the decoder, and the
    mov [gfx_blit_thr], dx      ; length the FIRST run must reach to stay
    mov byte [gfx_blit_pok], 1
    cmp byte [vga_p4built], 0
    jne .pdone
    call vga_p4build            ; once a boot, like sw_pairbuild
.pdone:
%endif
%endif                          ; ...end of gfx_blit4's VGA arm
.slow:
.row:
    mov si, [gfx_blit_sv]       ; ES:SI = this row's first byte
    cmp byte [gfx_blit_fst], 0
    je .rowd
    cmp bx, [vid_ch]            ; UNSIGNED, so a negative y is caught too: the
    jae .jnextrow               ; row is off screen and nothing below it clips
    push ax                     ; any more
    mov ax, bx
    call gfx_rowbase            ; once a ROW, where gfx_fill did it once a RUN.
                                ; BEFORE the `xor dx, dx` below and not after:
                                ; gfx_rowbase ends in a `mul`, so it WRITES DX -
                                ; zero for every geometry this kernel has, which
                                ; is exactly the kind of luck a row cursor
                                ; should not be resting on
    mov [gfx_blit_ro], ax
    mov ax, bx                  ; ...and this row's pair table: the two pixels
    add ax, [gfx_blit_x]        ; of a pair are always opposite parities, so
    mov dx, gfx_pairtab0        ; (x + y) & 1 picks the table and the dither
    test al, 1                  ; then costs the loop nothing (SPEC.md 39.4)
    jz .par
    mov dx, gfx_pairtab1
.par:
    mov [gfx_blit_tb], dx
    pop ax
    cmp byte [gfx_blit_fst], 1
    jne .rowd
    call sw_blit_row            ; THE WHOLE ROW, no run scan at all
.jnextrow:
    jmp .nextrow                ; (near: the scan below is longer than 127)
.rowd:
    xor dx, dx                  ; DX = the pixel the next run starts at
%ifdef GFX_PLANE
    mov [gfx_blit_rn], dx       ; ...and no run of this row is priced yet
%endif
.run:
    ; --- the run's colour, and the byte a matching PAIR would hold ---------
    mov di, dx
    shr di, 1
    add di, si
    mov al, [es:di]
    test dl, 1
    jnz .lo                     ; an even pixel is the HIGH nibble
    mov cl, 4
    shr al, cl
.lo:
    and al, 0Fh
    mov [gfx_blit_c], al
    mov ah, al
    mov cl, 4
    shl ah, cl
    or ah, al
    mov [gfx_blit_pb], ah

    ; --- walk it two pixels at a time -------------------------------------
    mov cx, dx
    test cl, 1
    jz .aligned
    inc cx                      ; an odd start: the pair scan begins at the
.aligned:                       ; next pixel, and this one is already in
    mov [gfx_blit_e], cx
    mov ax, [gfx_blit_w]
    sub ax, cx
    jbe .tail                   ; nothing left to scan in pairs
    shr ax, 1                   ; whole pairs remaining in the row
    jz .tail
    mov di, cx
    shr di, 1
    add di, si                  ; ES:DI = the first pair byte
    mov [gfx_blit_s0], di       ; SI belongs to the row, so the scan's origin
    mov cx, ax                  ; is parked in memory
    mov al, [gfx_blit_pb]
    cld
    repe scasb
    mov ax, di                  ; `mov` leaves scasb's ZF alone, and the
    jne .mism                   ; branch has to read it before anything else
    sub ax, [gfx_blit_s0]       ; does: every byte matched
    jmp short .have
.mism:
    dec ax                      ; the mismatch sits at DI-1, so it is not
    sub ax, [gfx_blit_s0]       ; part of the run
.have:
    add ax, ax                  ; bytes matched -> pixels
    add [gfx_blit_e], ax
.tail:
    mov cx, [gfx_blit_e]        ; the odd final nibble, by hand
    cmp cx, [gfx_blit_w]
    jae .emit
    mov di, cx
    shr di, 1
    add di, si
    mov al, [es:di]
    test cl, 1
    jnz .tlo
    mov cl, 4
    shr al, cl
.tlo:
    and al, 0Fh
    cmp al, [gfx_blit_c]
    jne .emit
    inc word [gfx_blit_e]       ; it matches too
.emit:
%ifdef GFX_PLANE
    ; --- IS THIS ROW STILL WORTH A RUN? (SPEC.md 5.4.1.3) ------------------
    ; Two questions and ONE threshold, read twice. The first run's LENGTH is
    ; the probe: a row that opens with a long run is flat and the span writer
    ; is ~16x cheaper on it, and a row that opens on a single pixel is a
    ; dither and every run after it will be one too. Then the run COUNT is a
    ; budget, because a row can be both - flat down its left half and noisy
    ; across the rest - and the scan has already said so without one extra
    ; compare.
    ;
    ; The length is computed for the FIRST run only. On every run after it the
    ; count answers, and this is the hottest loop in the primitive: what a
    ; flat row pays for the hybrid is these five instructions, and it is 5%
    ; (Set 107).
    cmp byte [gfx_blit_pok], 0
    je .emitrun
    mov di, [gfx_blit_rn]
    or di, di
    jnz .ebudget
    mov ax, [gfx_blit_e]
    sub ax, dx                  ; the first run's length, against the same
    cmp ax, [gfx_blit_thr]      ; threshold the count is measured by
    jb .eplanar                 ; it opens on detail: the row is the decoder's
.emitrun:
    inc word [gfx_blit_rn]
%endif
    mov di, [gfx_blit_x]
    add di, dx                  ; DI = the run's first x, on screen
    mov ah, [gfx_blit_c]
    mov dx, [gfx_blit_e]        ; DX = one past its last pixel, row-relative
%ifdef GFX_VGA
    cmp byte [gfx_blit_fst], 2  ; (which is where the next run starts) 2 = the
    je .emitv                   ; planar span writer; 0 = gfx_fill as it always
%endif                          ; was. 1 never gets here - the decoder above
    call gfx_blit_run           ; draws the whole row and skips the scan.
%ifdef GFX_VGA                  ; Without a VGA, 2 is unreachable and the fork
    jmp short .emitd            ; is not a branch, it is the only arm
.emitv:
    call vga_blit_span
%endif
.emitd:
    cmp dx, [gfx_blit_w]
    jb .run
%ifdef GFX_PLANE
    jmp short .nextrow

    ; --- ...and the handover, out of line because a flat row never takes it
    ; The point is rounded DOWN to an even pixel: the decoder takes source
    ; PAIRS, and the pixel that redraws is redrawn in the colour it holds.
.ebudget:
    cmp di, [gfx_blit_thr]
    jb .emitrun                 ; the row has spent its runs: the rest of it
.eplanar:                       ; is the decoder's, from this one
    mov ax, dx
    and al, 0xFE
    mov di, ax
    shr di, 1
    add di, si                  ; ES:DI = that pixel's source byte
    mov cx, [gfx_blit_w]
    sub cx, ax
    add ax, [gfx_blit_x]        ; AX = its x, CX = the pixels left in the row
    push si
    mov si, di
    call vga_blit_prow
    pop si
%endif
.nextrow:
    mov ax, [gfx_blit_sv]       ; next source row
    add ax, bp
    mov [gfx_blit_sv], ax
    inc bx                      ; next destination row
    dec word [gfx_blit_h]
    jnz .row
.out:
%ifdef GFX_VGA
    cmp byte [gfx_blit_fst], 2  ; the planar writer left Enable Set/Reset on
    jne .done                   ; for the whole call (SPEC.md 5.4.1)
    call vga_gc_reset
%endif
.done:
%ifdef KERN_BIG
    cmp byte [gfx_blit_hk], 0   ; ...and the nest comes down only if this call
    je .nohk                    ; put it up: the two guards above can reach
    GFXDLEAVE                   ; .out without ever having taken the hook
.nohk:
%endif
.pops:                          ; ...and the CUT returns here, having had its
    jmp kret_bp

%ifdef KERN_BIG
%ifndef NOBLITCUT
; -----------------------------------------------------------------------------
; ...and the CUT (SPEC.md 39.14.7.2), out of line because no machine with one
; card and no blit that fits one display ever reaches it.
;
; 39.14.7.1's gate is binary: a block that fits one display keeps 5.4.1's fast
; path and a block that straddles gives it up ENTIRELY - every coalesced run
; becomes a gfx_fill at its own arrival, which is the 4.17x Set 63 measured. A
; straddle is not, though, a reason to give up the fast path on the pixels that
; do NOT straddle: displays are placed edge to edge (39.19.2), so there is
; exactly ONE seam, and either side of it is a block that fits one display.
;
; What the seam costs is a source pointer, and that is the whole feasibility
; question. A run carries no source (39.14.7) and a SUB-RECT does, so the
; second half has to start `n` pixels along - and the source is packed two
; pixels to a byte, high nibble leftmost, so it can only do that when `n` is
; EVEN. It is: the seam is the primary's own width, 640 or 720, and a window's
; content origin is a multiple of 8 (11.94). An odd cut is refused rather than
; approximated, and the refusal is 39.14.7's path, unchanged.
;
; The half that does not cross is the same case one axis along, and it is
; taken too: vid_span_one refuses a block that fits one display in x and hangs
; into the DEAD ZONE in y (39.2.1), and those rows are drawn by nobody. So a
; block that does not reach the seam at all is one forced pass rather than
; two, and the row loop's own `cmp bx, [vid_ch]` is what skips the rows that
; are not there.
;
; in:  AX = x, BX = y, CX = w, DX = h (VIRTUAL), ES:SI = source, BP = stride,
;      [gfx_blit_fd] = 0, no hook taken and no nest put up
; -----------------------------------------------------------------------------
.cut:
    push ax                     ; vid_ctx_ptr answers in DI and DI is the
    mov al, 1                   ; caller's, banked at entry. Display 1 is at
    call vid_ctx_ptr            ; (VX, MBAR_H) on the RIGHT layout and (0, VY)
    mov ax, [di+VID_CTX_VX]     ; on BELOW, so a NON-ZERO VX names the axis
    or ax, ax                   ; (39.19.2) and no mode byte has to be read
    jz .cutv
    mov [gfx_blit_sm], ax
    mov di, ax                  ; ...and in DI, where the arithmetic wants it
    pop ax

    ; --- RIGHT: the seam is a COLUMN, and a column carries a nibble phase --
    sub di, ax                  ; DI = pixels of this block left of the seam
    jle .cutd1                  ; none: display 1 owns the whole of it
    cmp di, cx
    jge .cutd0                  ; all of them: display 0 does
    test di, 1
    jnz .hkdone                 ; AN ODD CUT (see above): the second half's
                                ; source would start on a LOW nibble, which no
                                ; path below can take. 39.14.7's mechanism is
                                ; still correct and is what happens
    mov [gfx_blit_n1], di
    push cx
    mov cx, di                  ; --- the left half, on display 0
    mov byte [gfx_blit_fd], 1
    call gfx_blit4
    pop cx
    mov di, [gfx_blit_n1]
    sub cx, di                  ; --- and the right half, on display 1,
    mov ax, [gfx_blit_sm]       ; whose source is n/2 BYTES along
    shr di, 1
    add si, di
    jmp short .cutd1            ; ...which is the second half's whole story

    ; --- BELOW: the seam is a ROW, and a row carries no parity at all ------
    ; The source advance is a whole number of STRIDES, so there is no even-cut
    ; test here and nothing to refuse. BP is SIGNED - Paint stores its canvas
    ; bottom row first and hands over a negative stride (42.9) - and `mul`
    ; leaves the same low sixteen bits as `imul` would, which is all that is
    ; being added to SI.
.cutv:
    mov ax, [di+VID_CTX_VY]
    mov [gfx_blit_sm], ax
    mov di, ax
    pop ax
    sub di, bx                  ; DI = rows of this block above the seam
    jle .cutd1
    cmp di, dx
    jge .cutd0
    mov [gfx_blit_n1], di
    push dx
    mov dx, di                  ; --- the top half, on display 0
    mov byte [gfx_blit_fd], 1
    call gfx_blit4
    pop dx
    mov di, [gfx_blit_n1]
    sub dx, di                  ; --- and the bottom half, on display 1,
    mov bx, [gfx_blit_sm]       ; whose source is n STRIDES along
    push ax
    push dx
    mov ax, di
    mul bp
    add si, ax
    pop dx
    pop ax
    jmp short .cutd1
.cutd0:
    mov byte [gfx_blit_fd], 1   ; one pass, on display 0: it does not reach the
    jmp short .cut1             ; seam and is in the dead zone in the other axis
.cutd1:
    mov byte [gfx_blit_fd], 2   ; ...or on display 1, for the same reason
.cut1:                          ; ...and the two-pass cases arrive at .cutd1
    call gfx_blit4              ; for their SECOND half, which is the same
    mov byte [gfx_blit_fd], 0   ; three instructions with the same display
    jmp .pops

.hkforce:                       ; THIS call is one half: its display is named
    mov byte [gfx_blit_hk], 1   ; and its teardown is its own
    push dx
    mov dl, [gfx_blit_fd]
    dec dl
    call gfx_disp_enter_n
    pop dx
    jmp .hkdone
%endif
%endif
%ifdef GFX_VGA

; -----------------------------------------------------------------------------
; vga_sr_on - internal: Enable Set/Reset on all four planes, and nothing else
;
; gfx_blit4's planar fast path arms this ONCE for the whole call and clears it
; with vga_gc_reset at the end (SPEC.md 5.4.1). vga_set_color writes GC0 and
; GC1 together, which is right for a primitive that arrives once and is a
; wasted `out` per RUN here - the colour is the only half that changes.
; -----------------------------------------------------------------------------
vga_sr_on:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0F01              ; GC1: enable Set/Reset on all planes
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_blit_span - gfx_blit_run's direct twin on VGA (SPEC.md 5.4.1)
; in:  DI = the run's first x, DX = one past its last pixel (row-relative),
;      AH = its colour, [gfx_blit_x] = the block's left edge, [gfx_blit_ro] =
;      the row's base byte, GC1 already armed by vga_sr_on, ES = the CALLER's
;      source segment, DF clear
; out: nothing; every register preserved, ES included
;
; sw_blit_span's planar half, and the same argument: gfx_hline is a gfx_fill and
; a gfx_fill is SPEC.md 5.7's per-call floor before a byte is written, per RUN.
;
; What a run costs here instead is TWO to FOUR `out`s and as many writes. The
; four planes take their data from Set/Reset rather than from the CPU byte, so
; a run is one colour write (GC0) and then the same masked-byte shape the 1bpp
; writer has - which is why the interior can be a `rep stosb` of a byte whose
; VALUE is irrelevant, exactly as gfx_fill's own interior is.
;
; Enable Set/Reset (GC1) is NOT written here: it does not change between runs,
; and hoisting it is the difference between two `out`s a run and three.
; -----------------------------------------------------------------------------
vga_blit_span:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, ah                  ; the documented contract: gfx_blit4 leaves
    mov [gfx_color], al         ; [gfx_color] holding the last colour drawn
    mov ax, di                  ; AX = x1
    mov cx, [gfx_blit_x]
    add cx, dx
    dec cx                      ; CX = x2, the last pixel drawn
    or ax, ax                   ; x1 < 0 -> 0
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp cx, [vid_cw]            ; x2 past the last column -> the last column
    jl .x2ok
    mov cx, [vid_cwm1]
.x2ok:
    cmp cx, ax
    jl .out                     ; entirely off the screen (or an empty run)
    mov si, ax                  ; BH = left edge mask, BL = right, by table
    and si, 7
    mov bh, [si + gfx_lmtab]
    mov si, cx
    and si, 7
    mov bl, [si + gfx_rmtab]
    shr cx, 1                   ; CX = right byte column
    shr cx, 1
    shr cx, 1
    shr ax, 1                   ; AX = left byte column
    shr ax, 1
    shr ax, 1
    mov di, [gfx_blit_ro]
    add di, ax                  ; DI = the run's first byte
    sub cx, ax
    mov si, cx                  ; SI = byte span; 0 = inside one byte
    mov dx, VGA_GC
    mov ah, [gfx_color]
    xor al, al                  ; GC0: Set/Reset = this run's colour
    out dx, ax
    mov ax, VGA_SEG
    mov es, ax                  ; ...and only NOW is ES not the source
    or si, si
    jnz .multi
    mov ah, bh                  ; one byte: the two edges are one mask
    and ah, bl
    mov al, 8
    out dx, ax
    mov al, [es:di]             ; the read loads the latches; the write is what
    mov [es:di], al             ; the ALU and the bit mask act on
    jmp short .out
.multi:
    mov ah, bh                  ; --- the left edge byte
    mov al, 8
    out dx, ax
    mov al, [es:di]
    mov [es:di], al
    inc di
    dec si
    jz .right
    mov ax, 0xFF08              ; --- whole bytes: latches irrelevant, and the
    out dx, ax                  ; CPU byte with them - Set/Reset supplies all
    mov cx, si                  ; four planes, so AL's value does not matter
    rep stosb
.right:
    mov ah, bl                  ; --- and the right edge byte
    mov al, 8
    out dx, ax
    mov al, [es:di]
    mov [es:di], al
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                          ; ...end of the VGA span writer

%ifdef GFX_PLANE
; -----------------------------------------------------------------------------
; THE PLANAR ROW DECODER (SPEC.md 5.4.1.3) - packed 4bpp into four VGA planes
;
; vga_blit_span above is the run path and it is the RIGHT one for flat art: a
; run of any length is two `out`s and a masked write, so a row that is one
; colour costs one of them. What it cannot do is detail. A run is ~1,800
; cycles on a 4.77MHz 8088 whatever it covers (PERFORMANCE.md Set 107), so a
; row of 466 alternating pixels is 466 of them - and os8088.gif, whose
; background is SPEC.md 63's 50% dither, is exactly that picture: 18,978 runs
; over 51,260 pixels, 34.1M cycles, SEVEN SECONDS for one canvas.
;
; This is the other half, and it is what SPEC.md 5.4's banner has called "a
; worthwhile future optimisation" since the primitive was written. It costs
; the SAME whatever is in the row - one write per plane per eight pixels -
; because it never asks what colour anything is. The 1bpp adapters have had
; their version of this since 5.4.1 (sw_blit_row, softgfx.inc); VGA was the
; odd one out, and only because four planes are four times the transpose.
;
; **THE TRANSPOSE IS THE WHOLE COST AND A TABLE IS WHAT MAKES IT AFFORDABLE.**
; A source byte is two pixels, so it contributes exactly two bits to each of
; the four planes - and which two depends on nothing but the byte. vga_p4tab
; answers all four at once: entry b holds, in nibble k, plane k's pair of
; bits. Two entries shifted together fill four pixels of each nibble, two
; more fill the other four, and the eight bits of one plane's byte are then
; one nibble of each half - which `rol reg, 4` and two masks assemble. Twenty
; instructions per eight pixels, no branch, no colour compare, ~65 cycles a
; pixel against the run path's ~1,800 a RUN.
;
; **WHY THERE IS A HYBRID HERE AND NOT ON 1bpp**, which is the question
; softgfx.inc's banner answers the other way. The crossover is where a row's
; runs cost what its pixels do: on 1bpp that is 1.84 runs a row (Set 43) and
; nothing anyone waits on is below it, so that decoder took the row outright.
; Here the transpose is four planes deep and the run is a direct write rather
; than a gfx_hline, and the two moves land it at ~16 runs a row - which is
; above Paint's BLANK canvas at one run a row, above ArtfulType's line of
; text, above Solitaire's card backs. Taking the row outright would cost a
; blank 448x280 canvas 1.66 s where it costs 111 ms. So the row is priced
; rather than assumed, and gfx_blit4's scanner does the pricing for free: the
; first run's LENGTH is the probe, and a row that starts flat and turns noisy
; is caught by a run BUDGET that hands the rest of the row over here.
;
; **THE SOURCE POINTER IS BIASED BACKWARDS, ON PURPOSE.** The destination
; byte grid is fixed by x and the source pair grid by the pixels, and the two
; align only when x is even. Rather than a second inner loop for the other
; case, the decode starts `p >> 1` bytes EARLIER than the first pixel and the
; leading junk is masked off by the same left-edge mask the block needed
; anyway - so one loop serves every phase. The bytes it reads before the row
; are inside the caller's segment and their pixels never reach the screen.
; When x is ODD that leaves the whole row one bit to the left, and a `rcr`
; chain over the four plane rows puts it back: 9 bytes of code for what a
; straddling-pair variant would have cost 195, and ~39% of a row - which is
; the one number here worth improving and 5.4.1.1 says how.
;
; THREE SEGMENTS, exactly as sw_blit_row has: the source is the CALLER's (ES
; on entry and left there through the decode), the table and the four plane
; rows are .lowbss - so DS becomes LOW_SEG and nothing needs an override -
; and the framebuffer is ES for the write pass alone. Nothing may read a
; [gfx_blit_*] word after DS moves.
; -----------------------------------------------------------------------------
VGA_PB      equ 84              ; bytes per plane row: 640/8 = 80, and four
                                ; spare so a block ending on a byte boundary
                                ; can still decode the group after it

; -----------------------------------------------------------------------------
; vga_p4build - fill vga_p4tab from nothing but arithmetic (SPEC.md 5.4.1.3)
; out: vga_p4tab built, [vga_p4built] = 1; every register preserved
;
; Once a boot, ~30,000 clocks, behind a flag in .text for gfx_pairbuilt's
; reason. Entry b, nibble k = (bit k of b's HIGH nibble) << 1 | (bit k of its
; low one): the high nibble is the LEFT pixel and takes the more significant
; of the pair, which is what makes `(T[b0] << 2) | T[b1]` come out in reading
; order.
; -----------------------------------------------------------------------------
vga_p4build:
    push ax
    push bx
    push cx
    push dx
    push si
    push es                     ; ...and ES, banked to the buffers' own rung
    mov ax, VGABUF_SEG          ; for the whole build (SPEC.md 39.22). It used
    mov es, ax                  ; to be SS, which reached them while they were
                                ; .lowbss; the contract that every register is
                                ; preserved is why this pushes rather than
                                ; leaving ES where it lands
    xor bx, bx
.byte:
    mov cl, 4
    mov al, bl
    shr al, cl                  ; AL = the left pixel
    mov ah, bl
    and ah, 15                  ; AH = the right one
    xor dx, dx
    mov ch, 8                   ; plane 3 first, so the shifts carry it up
.pl:
    shl dx, cl                  ; CL is 4 from the nibble split above
    test al, ch
    jz .l0
    or dl, 2
.l0:
    test ah, ch
    jz .l1
    or dl, 1
.l1:
    shr ch, 1
    jnz .pl
    mov si, bx
    shl si, 1
    mov [es:si + vga_p4tab], dx ; .vgabuf, so NOT through SS any more
                                ; (SPEC.md 39.22): ES is banked to VGABUF_SEG
                                ; for the build and put back, which is cheaper
                                ; than a segment load a byte
    inc bx
    cmp bx, 256
    jb .byte
    mov byte [vga_p4built], 1
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_blit_prow - the rest of ONE ROW, transposed into the four planes
; in:  ES:SI = the source byte holding the first pixel - an EVEN pixel index
;              inside the row, so it is that byte's HIGH nibble
;      AX    = that pixel's destination x, on screen (gfx_blit4 has proved the
;              whole block is, which is why this does not clip in x)
;      CX    = pixels to draw (> 0), BX = ignored
;      [gfx_blit_ro] = the row's first framebuffer byte
; out: nothing; every register preserved, DS and ES included. Leaves Enable
;      Set/Reset and the Map Mask as vga_sr_on left them, so a following run
;      needs no re-arming.
; -----------------------------------------------------------------------------
vga_blit_prow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    mov bx, ax                  ; --- the two edge masks and the byte span
    add bx, cx
    dec bx                      ; BX = the last pixel's x
    mov di, bx                  ; ...and CX is spent from here, so it can
    mov cl, 3                   ; carry the shift count for both columns
    shr di, cl                  ; DI = its byte column
    and bx, 7
    mov bl, [bx + gfx_rmtab]
    mov [ss:vga_pr_rm], bl
    mov bx, ax
    and bx, 7
    mov bp, bx                  ; BP = p, the destination phase
    mov bl, [bx + gfx_lmtab]
    mov [ss:vga_pr_lm], bl

    mov dx, ax
    shr dx, cl                  ; DX = the first byte's column
    sub di, dx
    inc di
    mov [ss:vga_pr_n], di       ; DI = bytes across
    add dx, [gfx_blit_ro]
    mov [ss:vga_pr_o], dx       ; ...starting here in the framebuffer

    mov bx, bp
    shr bx, 1
    sub si, bx                  ; the decode starts p >> 1 bytes EARLY
    and bp, 1
    mov [ss:vga_pr_sh], bp      ; an odd x is one bit left until the rcr pass

    call vga_gc_reset           ; the CPU byte supplies the plane here, so
                                ; Set/Reset comes OFF for the whole of this
                                ; row - and back on at the end, because the
                                ; next row may be a run's again

    mov ax, VGABUF_SEG          ; ovlchk: DS = VGABUF_SEG
    mov ds, ax                  ; DS = the table and the four plane rows; ES is
                                ; still the caller's source. THE BARE
                                ; REFERENCE IS THE .vgabuf ONE FROM HERE TO
                                ; THE `pop ds` BELOW, and every `.lowbss` word
                                ; in between needs `ss:` - which is the whole
                                ; hazard of SPEC.md 39.22 and cost three
                                ; overrides that used to be free
    mov bp, di                  ; BP = groups to decode
    mov di, vga_pbuf
    mov cl, 4                   ; the split's rotate count, for the whole loop
.g:
    mov bl, [es:si]             ; --- four source bytes, eight pixels, four
    xor bh, bh                  ;     plane bytes, and not one compare
    shl bx, 1
    mov ax, [bx + vga_p4tab]
    mov bl, [es:si+1]
    xor bh, bh
    shl bx, 1
    shl ax, 1
    shl ax, 1
    or ax, [bx + vga_p4tab]     ; AX = pixels 0..3, a nibble to the plane
    mov bl, [es:si+2]
    xor bh, bh
    shl bx, 1
    mov dx, [bx + vga_p4tab]
    mov bl, [es:si+3]
    xor bh, bh
    shl bx, 1
    shl dx, 1
    shl dx, 1
    or dx, [bx + vga_p4tab]     ; DX = pixels 4..7
    add si, 4

    mov ch, al                  ; --- plane 0 is A's nibble 0 over B's, and
    rol al, cl                  ;     plane 1 the same one nibble up
    and al, 0xF0
    mov bl, dl
    and bl, 0x0F
    or al, bl                   ; AL = plane 0
    rol dl, cl
    and dl, 0x0F
    and ch, 0xF0
    or dl, ch                   ; DL = plane 1
    mov ch, ah
    rol ah, cl
    and ah, 0xF0
    mov bl, dh
    and bl, 0x0F
    or ah, bl                   ; AH = plane 2
    rol dh, cl
    and dh, 0x0F
    and ch, 0xF0
    or dh, ch                   ; DH = plane 3

    mov [di], al
    mov [di + VGA_PB], dl
    mov [di + VGA_PB*2], ah
    mov [di + VGA_PB*3], dh
    inc di
    dec bp
    jnz .g

    cmp word [ss:vga_pr_sh], 0  ; --- an odd x, put back one bit right. `inc`
    je .noshift                 ;     and `loop` both leave CF alone, so the
    mov si, vga_pbuf            ;     chain runs the length of a plane row
    mov bp, 4
.shp:
    mov cx, [ss:vga_pr_n]
    mov di, si
    clc
.shb:
    mov al, [di]
    rcr al, 1
    mov [di], al
    inc di
    loop .shb
    add si, VGA_PB
    dec bp
    jnz .shp
.noshift:

    mov word [ss:vga_pr_ps], VGA_PB
    mov bp, vga_pbuf
    call vga_prow_emit          ; --- and out, four Map Masks and four rows

    mov dx, VGA_SEQ             ; --- and hand the card back as it was found
    mov ax, 0x0F02
    out dx, ax
    call vga_sr_on
    pop es
    pop ds                      ; ovlchk: DS restored
    jmp kret_bp
%endif

; -----------------------------------------------------------------------------
; gfx_blitp - a block that is ALREADY FRAMEBUFFER BYTES, copied to the screen
;             (SPEC.md 5.4.3)
;
; in:       ES:SI = plane 0's first row of the block. The other three planes
;                   follow at DI-byte intervals from it
;           DI    = that plane step (plane 0 to plane 1, and so on)
;           BP    = the row stride WITHIN a plane
;           AX    = destination x - A MULTIPLE OF 8 - BX = destination y
;           CX    = width in pixels (> 0), DX = height in rows (> 0)
; out:      CF = 0 drawn; CF = 1 refused and nothing was drawn. Every register
;           preserved, DS and ES included.
;
; THE OTHER END OF SPEC.md 5.4. gfx_blit4 takes PIXELS and works out what the
; card wants; this takes what the card wants and does nothing but put it
; there. It is gfx_blit1 (5.4.2) with four planes instead of one, and it is
; gfx_restore (5.8) without the rect having had to come off this screen
; first - which is the whole of what it adds, because a canvas is not a
; save-under and nothing ever saved it.
;
; **WHAT IT COSTS IS THE BYTES AND NOTHING ELSE**: one `rep movsb` per plane
; per row, plus a masked byte where the block's right edge lands mid-byte.
; That is the same traffic gfx_scroll and gfx_restore move for the same area,
; and it is the floor a 16-colour blit has on this card - four planes of one
; bit each is what 4bpp IS here.
;
; What it does NOT do is a caller's arithmetic. It refuses, rather than
; approximating, in five cases:
;
;   - a 1bpp adapter (39.5). Four planes is not a thing a Hercules has, and a
;     block built for one means nothing there. A caller that wants both keeps
;     its pixels in 4bpp and calls gfx_blit4, which is what the 1bpp decoder
;     (5.4.1.1) is for and is already flat in run density.
;   - an x off the byte grid. The whole point is that no bit moves; an
;     unaligned x means every byte of every row shifts, which is a pass over
;     the data and is exactly the cost this exists to remove. 5.4.3 is where
;     a caller is told to align instead - and for a WINDOW that is free,
;     because 11.94 already puts the content origin on a multiple of 8.
;   - a block off either side of the screen in x, for 5.4.1's reason: a left
;     clip is a shift.
;   - an armed clip region (11.3), which each byte would have to test itself
;     against - and only gfx_fill does that.
;   - a block straddling two displays (39.14.7). One display it can take,
;     through the same whole-shape hook gfx_blit4 uses.
;
; A refusal is not an error and a caller must READ it: it means "draw this
; the way you did before", and the way you did before still works.
;
; Rows off the screen in y are SKIPPED rather than refused - that costs one
; compare a row and no caller has to clamp.
; -----------------------------------------------------------------------------
gfx_blitp:
%ifndef GFX_PLANE
    stc                         ; A SLOT THAT EXISTS IN ONE BUILD AND NOT
    ret                         ; ANOTHER IS WORSE THAN A REFUSAL (the debug
                                ; registry's banner in kernel.asm says why), so
                                ; the cell is here in every kernel and kern_small
                                ; answers it the way a 1bpp adapter does: no,
                                ; draw it the way you did before. Six bytes.
%else
    call cur_unlazy             ; off the clip list (SPEC.md 11.3): 7.1.4
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    mov byte [gfx_bp_hk], 0     ; **BEFORE THE GUARDS, NOT AFTER THEM.** This
                                ; word is what the teardown reads to decide
                                ; whether to bring the display nest back down,
                                ; and every refusal below reaches that teardown
                                ; - so cleared here it said "no hook" and
                                ; cleared six lines further on it said whatever
                                ; the LAST call had said. One call that took
                                ; the hook, then one that refuses on its first
                                ; line, and GFXDLEAVE ran for a hook this call
                                ; never took: [gfx_dnest] goes to 255, every
                                ; primitive after it believes it is nested, and
                                ; gfx_blit4 then treats a VIRTUAL x as a
                                ; display-local one and clips the whole canvas
                                ; away. What that looked like was a Paint
                                ; window dragged from a VGA straight onto a
                                ; mono display - no straddle on the way - with
                                ; a canvas that never drew, while the same trip
                                ; VIA a straddle was fine, because the straddle
                                ; had already converted and gfx_blitp was not
                                ; called again. gfx_blit4's own [gfx_blit_fst]
                                ; carries this warning twenty lines up; this
                                ; routine was written from it and put the line
                                ; in the wrong place
    cmp word [wm_clip_n], 0     ; ...and NOT the depth: SPEC.md 5.4.3.4 moves
    je .clipok                  ; that below the display hook
    cmp byte [wm_dmg_cull], 0   ; SPEC.md 5.4.3.3: THE CULL IS NOT A CLIP
    je .no                      ; REGION. A plane byte carries eight x's, so a
                                ; real region has to be refused - but 11.3.3's
                                ; cull is armed for one W_PAINT in a pass that
                                ; redraws everything above afterwards, so
                                ; ignoring it paints redundantly and not
                                ; wrongly. Refusing on it cost Paint its four
                                ; planes for the session, because 42.13.1
                                ; reads a refusal as a fact about the MACHINE
.clipok:
    test al, 7
    jnz .no                     ; x off the byte grid
    or cx, cx
    jz .no
    or dx, dx
    jz .no
    or ax, ax
    js .no
%ifdef KERN_BIG
    cmp byte [vid_ndisp], 1
    jbe .hkdone                 ; one display: every machine that was already
    cmp byte [gfx_dnest], 0     ; right
    jne .no                     ; nested: an outer hook owns the translation
    push cx
    push dx
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call vid_span_one           ; SPEC.md 39.14.7.1, gfx_blit4's own gate
    pop dx
    pop cx
    jc .no                      ; STRADDLING: a plane byte carries no x, so
                                ; there is nothing here to split
    mov byte [gfx_bp_hk], 1
    GFXDENTER
.hkdone:
%endif
    ; --- ...AND ONLY NOW, HOW DEEP IS IT? (SPEC.md 5.4.3.4) ------------------
    ; BELOW the hook and not above it, which is 39.14.8's rule applied to a
    ; REFUSAL rather than to a write: [vid_mono] describes the display LAST
    ; DRAWN ON (39.14.3), and the rect handed in is virtual, so asked up there
    ; this routine answers a question about whichever card the previous
    ; primitive happened to touch. gfx_blit4 has always tested it here - below
    ; its own hook, at .dsp1 - and the two are the same routine's two ends.
    ;
    ; Both directions are wrong and neither is what a caller would guess: a
    ; VGA block entered while the mono card is current is REFUSED and Paint
    ; converts a canvas that could have kept its planes (42.13.1 prices that
    ; at 1.2 s each way), and a MONO block entered while the VGA is current
    ; PASSES - so 5.4.3.2's probe answers "yes, planes" for a 1bpp card and
    ; the caller converts to four planes it can never draw.
    ;
    ; IT IS LATENT AND IS RECORDED AS LATENT, which is the whole of what is
    ; claimed here. Measured on a VGA+MDA XT with the desktop extended, both
    ; primaries, Paint dragged wholly onto the second display: [vid_cur] was
    ; ALREADY the block's own display at entry both times - 1 with
    ; [vid_mono]=0 on the VGA and 1 with [vid_mono]=1 on the MDA - because a
    ; window's frame is drawn on its own display earlier in the same lock
    ; hold, and gfx_unlock is what bounds that to one hold. So the guard read
    ; the right card by ACCIDENT OF DRAW ORDER, and the menu save-under in
    ; 39.14.8 was safe by exactly that kind of accident until it was not.
    ; This is the same fix: safe by mechanism, and it costs code motion.
    ;
    ; OUTSIDE the %endif rather than inside it: GFX_PLANE implies KERN_BIG
    ; today, so both spellings assemble, and this one stays right if that ever
    ; stops being true.
    cmp byte [vid_mono], 0
    jne .no
    ; --- the geometry, once for the whole block. THE SHIFTS ARE SINGLE-BIT
    ; and that is not style: `mov cl, 3` and a `pop cx` in the same block is a
    ; count destroyed by the value it was going to shift, and the first
    ; version of this did exactly that - `shr cx, cl` with CL holding x's low
    ; byte, which made the block one byte too wide and every row of it read
    ; one byte into the next.
    push ax                     ; x
    add ax, cx
    dec ax                      ; AX = the last pixel's x
    cmp ax, [vid_cw]
    jae .nopop                  ; ...off the right edge (and a wrap with it)
    or di, di                   ; **THE PROBE** (SPEC.md 5.4.3.2): bit 15 IS the
    js .probe                   ; sign bit, so the test is two bytes. Every guard
                                ; has now run and nothing has been written, so
                                ; this is the answer without the drawing. DI is
                                ; a row's worth of bytes, so bit 15 in it is a
                                ; plane step no caller can have; ES:SI and BP
                                ; are not read at all on this path, which is
                                ; the point - the caller asking has no planes
                                ; yet. It is one question Paint cannot answer
                                ; for itself: OSAPI_WM_DISPLAY names a display
                                ; and says nothing about a straddle, and
                                ; 39.16.4.2 resolves one by usable BAND rather
                                ; than by colour, so a window across a 640-wide
                                ; VGA and a 720-wide Hercules reads as the VGA
    push ax
    and ax, 7
    xchg ax, bx
    mov bl, [bx + gfx_rmtab]
    xchg ax, bx
    mov [ss:vga_pr_rm], al
    pop ax
    shr ax, 1
    shr ax, 1
    shr ax, 1                   ; AX = the last byte column
    pop cx                      ; CX = x
    shr cx, 1
    shr cx, 1
    shr cx, 1                   ; CX = the first byte column, for every row
    sub ax, cx
    inc ax
    mov [ss:vga_pr_n], ax       ; bytes across
    mov byte [ss:vga_pr_lm], 0xFF   ; x is aligned, so there is no left edge
    mov [ss:vga_pr_ps], di
    mov [ss:vga_pr_rs], bp
    mov bp, es
    mov [ss:vga_pr_sg], bp      ; the caller's segment, banked: DS has to be
                                ; the kernel's for gfx_rowbase and the planes'
                                ; for the copy, once a row each
    call vga_gc_reset           ; Set/Reset off, bit mask open: the CPU byte
                                ; IS the data here
.row:
    cmp bx, [vid_ch]
    jae .next                   ; UNSIGNED, so a negative y is caught too
    push ax
    push cx                     ; **gfx_rowbase HAS TWO PATHS AND ONE OF THEM
    push dx                     ; WRITES CL AND DX** (SPEC.md 5.4.3.1): below
    mov ax, bx                  ; [vid_rowmax] it is a table lookup that touches
    call gfx_rowbase            ; nothing, and above it gfx_rowbase_calc takes
    pop dx                      ; its shift count in CL and ends in a `mul`.
    pop cx                      ; This loop keeps its byte column in CX and its
    add ax, cx                  ; row count in DX, so without these two pushes
    mov [ss:vga_pr_o], ax       ; it is correct for the top 348 rows of a
    pop ax                      ; 480-row screen and runs away below them -
                                ; 65,000 iterations at a byte column that is
                                ; now [vid_bshift]. Reported from the field as
                                ; a Paint window dragged low, and gfx_blit4
                                ; does not have it because it keeps its row
                                ; cursor in memory and says why
    mov bp, si
    push bx                     ; THE EMITTER KEEPS NOTHING BUT BP AND DS, and
    push cx                     ; where it came from that was free - it was the
    push dx                     ; last thing vga_blit_prow did. Here it is
    push si                     ; inside a loop whose y, byte column, row count
    push ds                     ; and source pointer all live in what it
    mov ds, [ss:vga_pr_sg]      ; clobbers, and the first version of this
    call vga_prow_emit          ; drew 470 rows of stripes instead of 64
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
.next:
    add si, [ss:vga_pr_rs]
    inc bx
    dec dx
    jnz .row

    mov dx, VGA_SEQ             ; the card back as every other primitive
    mov ax, 0x0F02              ; expects to find it (Map Mask all four,
    out dx, ax                  ; Set/Reset off, bit mask open)
    call vga_gc_reset
    clc
    jmp short .out
.probe:
    pop ax                      ; the x, which .nopop pops for the same reason:
                                ; the guard above is INSIDE the geometry's own
                                ; push and one is outstanding here
    clc                         ; ...and the hook, if this call took one, comes
    jmp short .out              ; down in the teardown exactly as it would have
                                ; done after a real block
.nopop:
    pop ax
.no:
    stc
.out:
%ifdef KERN_BIG
    pushf                       ; **THE ANSWER, CARRIED ACROSS A COMPARE THAT
                                ; WRITES CF.** This routine's whole output is
                                ; the carry, and the teardown below opens with
                                ; a `cmp` - so the `stc` above lived exactly
                                ; two instructions and every refusal returned
                                ; "drawn". The success path survived it by
                                ; luck, `cmp 0, 0` clearing CF being the same
                                ; answer `clc` had just given.
                                ;
                                ; It was invisible for a release because no
                                ; refusal is reachable on a one-display VGA:
                                ; the adapter is not 1bpp, 42.13 aligns the x,
                                ; and there is no seam. The EXTENDED DESKTOP
                                ; has all three (SPEC.md 39.14.7.1), and what
                                ; it looked like there was a canvas that drew
                                ; nothing at all on the far monitor - Paint
                                ; was told it had been drawn, so 42.13.1's
                                ; fallback never ran.
    cmp byte [gfx_bp_hk], 0     ; ...and the nest comes down only if this call
    je .nohk                    ; put it up. GFXDLEAVE keeps the flags itself
    GFXDLEAVE
.nohk:
    popf
%endif
    pop es
    pop ds
    jmp kret_bp
%endif

%ifdef GFX_PLANE
; -----------------------------------------------------------------------------
; vga_prow_emit - ONE ROW's four plane bytes into the framebuffer
; in:  DS:BP = plane 0's bytes for this row, [vga_pr_ps] = the step from one
;      plane's bytes to the next, and the row's geometry in [vga_pr_lm] /
;      [vga_pr_rm] / [vga_pr_n] / [vga_pr_o]
; out: ES = VGA_SEG; AX, BX, CX, DX, SI, DI ALL CLOBBERED, BP and DS preserved
;      - and a caller that loops over rows has to bank its own, which is the
;      one thing about this contract that has already cost a session
;
; Split out of vga_blit_prow when SPEC.md 5.4.3 arrived, because it is the
; whole of what that primitive does and none of what the decoder does. **Its
; geometry is read `ss:` and that is the point**: it is .lowbss either way, SS
; IS LOW_SEG for all kernel code (SPEC.md 1), and reading it through SS leaves
; DS free to be whoever owns the PLANES - the decoder's own buffers on one
; caller and a package's canvas on the other. Five prefix bytes buys the
; primitive its whole body.
;
; Set/Reset is OFF and the bit mask is the caller's business: both are armed
; once per CALL above rather than per row.
; -----------------------------------------------------------------------------
vga_prow_emit:
    push bp
    cld                         ; `lodsb`, `stosb` and `rep movsb` all read DF
                                ; and NEITHER caller owes it: gfx_blit4 clears
                                ; it for its own `repe scasb`, which is luck
                                ; rather than a contract, and gfx_blitp did not
                                ; clear it at all
    mov ax, VGA_SEG
    mov es, ax
    mov bh, 1                   ; BH = the plane's Map Mask bit
.pl:
    mov dx, VGA_SEQ
    mov al, 2
    mov ah, bh
    out dx, ax                  ; SEQ2: Map Mask = this plane alone
    mov si, bp
    mov di, [ss:vga_pr_o]
    mov cx, [ss:vga_pr_n]
    mov dx, VGA_GC
    mov ah, [ss:vga_pr_lm]
    cmp cx, 1
    ja .pf
    and ah, [ss:vga_pr_rm]      ; one byte across: the edges are one mask
.pf:
    ; --- BOTH EDGES WHOLE? then there are no edges (SPEC.md 5.4.3)
    ; A masked byte is a port write, a VRAM READ to load the latches and a
    ; VRAM write, and this path spends two of them per plane per row for
    ; bytes that are entirely the caller's anyway. Measured, that masking is
    ; ~124 us of the ~140 a narrow row costs (PERFORMANCE.md Set 108), so a
    ; caller that aligns its left edge - which is 11.94's free for a window -
    ; and keeps its width on the byte grid gets one `out` and one `rep movsb`
    ; for the whole row instead of three and three.
    cmp ah, 0xFF
    jne .pedge
    cmp byte [ss:vga_pr_rm], 0xFF
    jne .pedge
    mov ax, 0xFF08
    out dx, ax
    rep movsb
    jmp short .pnext
.pedge:
    mov al, 8
    out dx, ax
    lodsb
    mov ah, [es:di]             ; the read loads the latches; the write is what
    stosb                       ; the bit mask acts on
    dec cx
    jz .pnext
    dec cx
    jz .plast
    mov ax, 0xFF08              ; the interior: every bit ours, so the latches
    out dx, ax                  ; and the read with them are not needed
    rep movsb
.plast:
    mov ah, [ss:vga_pr_rm]
    mov al, 8
    out dx, ax
    lodsb
    mov ah, [es:di]
    stosb
.pnext:
    add bp, [ss:vga_pr_ps]
    shl bh, 1
    cmp bh, 0x10
    jb .pl
    pop bp
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_blit_run - emit one coalesced run (module-internal to gfx_blit4)
; in:  DI = run start x, DX = one past the run's last pixel (row-relative),
;      AH = colour, BX = y, [gfx_blit_x] = the block's left edge
; out: nothing (AX/BX/CX/DX/SI/DI/BP preserved)
; -----------------------------------------------------------------------------
gfx_blit_run:
    push ax
    push bx
    push cx
    push dx
    mov al, ah
    mov [gfx_color], al
    mov ax, di                  ; x1 = the run's start
    mov cx, [gfx_blit_x]
    add cx, dx
    dec cx                      ; x2 = the last pixel drawn
    cmp ax, cx
    ja .out                     ; empty run (cannot happen; cheap to guard)
    mov dx, bx                  ; gfx_hline wants y in DX
    mov bx, cx
    call gfx_hline              ; clips, dispatches and reduces for us
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret



; -----------------------------------------------------------------------------
; gfx_vline - vertical line in [gfx_color], inclusive endpoints
; in:       AX = x, BX = y1, DX = y2  (y1 <= y2)
; out:      -
; clobbers: flags
;
; A 1-column solid rect via gfx_fill (which clips and adapter-
; dispatches itself). vga_vline_core used to live above this for
; gfx_xor_rect's verticals; gfx_xor_strips draws them as fills now.
; -----------------------------------------------------------------------------
gfx_vline:
    push cx
    mov cx, ax                  ; rect (x,y1)-(x,y2)
    call gfx_fill
    pop cx
    ret

; -----------------------------------------------------------------------------
; gfx_fill - solid rectangle in [gfx_color], inclusive corners
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_fill:
    GFXCLIP gfx_fill_d
gfx_fill_d:
    GFXDISP gfx_fill_raw
gfx_fill_raw:
%ifndef GFX_VGA
    jmp sw_fill                 ; NO VGA IN THIS BUILD: the software renderer is
                                ; not a branch, it is the only renderer there is.
                                ; The `jmp` and not an `equ` because the FALL
                                ; THROUGH below is what a VGA build takes, and
                                ; there is nothing here to fall into any more
%else
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_fill                 ; the renderer - and the flag store is BELOW
    mov byte [vga_xorm], 0      ; this jump, so the 1bpp arm is byte-identical

; -----------------------------------------------------------------------------
; vga_solid_rect - the VGA rect body, for gfx_fill_raw AND gfx_xor_fill_raw
;
; in:  AX/BX/CX/DX = the rect; [vga_xorm] = 0 replace, 1 XOR
;
; The two differed in exactly two places: which routine arms the GC, and
; whether the interior is `rep stosb` or a byte-at-a-time read-then-write
; (the XOR ALU needs fresh latches per byte). The seven pushes, gfx_rect_setup,
; the `jc`, the ES/DX setup, BOTH masked edge column loops, the span tests, the
; 0FF08h interior mask, vga_gc_reset and the seven pops were the same 138 and
; 155 bytes twice.
;
; THE EDGE LOOPS SHARE VERBATIM, which is the fact that makes this a merge and
; not a rewrite: with Set/Reset enabled on all four planes the CPU byte is
; IGNORED, so `mov al,[es:di] / mov [es:di],al` means the same thing in both
; modes - a latch load and a masked write-back.
;
; PERFORMANCE (SPEC.md 5.7.1, PERFORMANCE.md Set 93.1): this adds one flag
; store and two compares to the VGA arm of the most-called drawing routine in
; the system - about 50 clocks against a 2,405-cycle floor, +2.1%, PREDICTED.
; The 1bpp arm pays nothing.
; -----------------------------------------------------------------------------
vga_solid_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call gfx_rect_setup
    jc .done
    cmp byte [vga_xorm], 0
    jne .armx
    call vga_set_color
    jmp short .armed
.armx:
    call vga_set_xor
.armed:
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ax, [vga_m8l]           ; left edge column (whole rect if 1 byte
    out dx, ax                  ; wide): AL = 8, AH = the mask, one word
    mov di, [gfx_roff]
    mov si, [gfx_rrows]
.lcol:
    mov al, [es:di]             ; load latches
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov bx, [gfx_rspan]
    or bx, bx
    jz .reset
    mov ax, [vga_m8r]           ; right edge column
    out dx, ax
    mov di, [gfx_roff]
    add di, bx
    mov si, [gfx_rrows]
.rcol:
    mov al, [es:di]
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec bx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [gfx_roff]
    inc di
    mov si, [gfx_rrows]
    mov dx, ROW_BYTES           ; DX = the row step LESS what the rep advances
    sub dx, bx                  ; DI by, so one add ends the row (SPEC.md 5.7.1)
    cmp byte [vga_xorm], 0
    jne .xrow
.irow:
    mov cx, bx
    rep stosb
    add di, dx
    dec si
    jnz .irow
    jmp short .reset
.xrow:                          ; XOR: every interior byte read before write,
    mov cx, bx                  ; because the ALU works off the latches
.xbyte:
    mov al, [es:di]
    stosb
    loop .xbyte
    add di, dx
    dec si
    jnz .xrow
.reset:
    call vga_gc_reset
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_frame - 1px outline rectangle in [gfx_color]
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_frame:
    push bp                     ; the SAME four strips gfx_xor_rect draws, and
    mov bp, gfx_fill            ; the same pixels this drew with two hlines and
    call gfx_xor_strips         ; two vlines: the strips' top and bottom are
    pop bp                      ; full width, so they cover the corners the
    ret                         ; verticals used to draw twice. Four calls
                                ; either way, and gfx_fill is one wrapper
                                ; frame shallower than gfx_hline/gfx_vline

; =============================================================================
; SPEC.md 5.10 - gfx_spans: a RUN OF SCAN LINES, one call
;
; Every rect primitive above pays its fixed cost per RECT, and SPEC.md 5.7 is
; the account of why that cost is what it is. Measured on the 8088 for an 8x8
; brush dab on a Hercules, gfx_fill entry to sw_rect.done, ISR-free intervals
; only:
;
;   the wrapper (clip test, display test, [vid_mono], the push set)     381
;   gfx_rect_setup                                                    1,236
;   sw_plane_op - the rows themselves                                 3,197
;                                                                     -----
;                                                                     5,305 cyc
;
; So BATCHING ALONE BUYS ALMOST NOTHING: a primitive that loops the existing
; per-rect machinery hoists 381 cycles of 5,305, which is 7%, and that is
; PERFORMANCE.md Set 88's trap exactly - removing calls without beating them.
; What it has to beat is the 1,236 and the row loop, and the only way is to
; stop DRAWING THE SAME ROW REPEATEDLY. A 45-degree brush chord is 8 dabs of
; 8x8 = 64 row-writes for a shape that is 15 rows; emitted as its 15 spans
; with one setup between them it is 15.
;
; Hence the shape of this call, and each part of it is load-bearing:
;
;   CONSECUTIVE ROWS. Row n is y+n, so the framebuffer base steps by
;   gfx_nextrow's three instructions instead of gfx_rowbase's lookup, and the
;   y clip is done ONCE for the whole run rather than per row.
;
;   ONE INTERVAL A ROW. x1 > x2 is an empty row, which costs the step and
;   nothing else. A caller with two intervals on one row makes two calls.
;
;   IT REFUSES. An armed clip region (SPEC.md 11.3) and a second display
;   (39.14) both want the run re-cut per fragment, which is the whole of
;   gfx_clip_run and gfx_disp_run again; kern_small has no body at all. All
;   three answer CF = 1 having drawn NOTHING, and the caller falls back to a
;   gfx_fill a row - gfx_blitp's contract (5.4.3) and for its reason.
;
; It is not Paint's primitive. A span list per consecutive scan line is what
; every polygon rasteriser produces, and this is the call that draws one.
; -----------------------------------------------------------------------------
; gfx_spans - fill CX consecutive scan lines from y, one x interval each
; in:       AX = y of the FIRST span
;           ES:SI = CX records of two words each: x1 then x2, both INCLUSIVE
;           CX = rows; [gfx_color] = the ink; the gfx lock held
; out:      CF = 0 drawn. CF = 1 REFUSED, and nothing was drawn
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_spans:
%ifndef KERN_BIG
    stc                         ; kern_small carries the slot and no body,
    ret                         ; gfx_blit1's precedent (SPEC.md 5.4.2)
%else
    cmp word [wm_clip_n], 0     ; an armed region, or...
    jne .no
    cmp byte [vid_ndisp], 1     ; ...a second display: both re-cut the run,
    ja .no                      ; and the caller's gfx_fill loop does it right
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [gfx_sp_seg], es
    mov [gfx_sp_si], si
    mov [gfx_sp_n], cx
    jcxz .nil                   ; the three early exits go through .nil: every
    or ax, ax                   ; conditional jump on an 8086 is rel8 and
    jns .top                    ; .done is past the row loops
    mov bx, ax                  ; y < 0: drop that many LEADING spans, which
    neg bx                      ; is the whole of this run's top clip
    cmp bx, cx
    jae .nil
    sub [gfx_sp_n], bx
    shl bx, 1
    shl bx, 1                   ; four bytes a record
    add [gfx_sp_si], bx
    xor ax, ax
.top:
    cmp ax, [vid_ch]
    jb .rowsl
.nil:
    jmp .done
.rowsl:
    mov bx, [vid_ch]            ; ...and the bottom clip is a row COUNT, once
    sub bx, ax
    cmp bx, [gfx_sp_n]
    jae .rows
    mov [gfx_sp_n], bx
.rows:
    mov [gfx_sp_y], ax
    call gfx_rowbase            ; the ONE row base this call computes
    mov [gfx_sp_row], ax
    CURUNLAZY                   ; unclipped, so this may write any pixel on
%ifndef GFX_VGA
    jmp sw_spans                ; no VGA: softgfx.inc's row loop is the loop
%else
    cmp byte [vid_mono], 0      ; the screen and a hide is owed (7.1.4)
    jne sw_spans                ; 1bpp: softgfx.inc's row loop, then .done
.vga:
    call vga_set_color          ; THE LIST'S, not the row's - four OUTs and a
    mov ax, VGA_SEG             ; GC reset hoisted out of every span but one
    mov [gfx_sp_fb], ax         ; gfx_sp_next puts ES back to this
    mov es, ax
.vrow:
    call gfx_sp_next            ; AH/AL = the masks, BX = span, DI = the byte
    jc .vnext
    mov cl, al                  ; bank the right mask: AL is about to be 8
    mov dx, VGA_GC
    mov al, 8
    out dx, ax                  ; GC8 = left edge (the row if 1 byte wide)
    mov al, [es:di]             ; load latches
    mov [es:di], al
    or bx, bx
    jz .vnext
    mov ah, cl                  ; right edge column
    mov al, 8
    out dx, ax
    add di, bx
    mov al, [es:di]
    mov [es:di], al
    dec bx                      ; interior bytes
    jz .vnext
    mov ax, 0xFF08              ; full bytes: latches irrelevant, and Set/Reset
    out dx, ax                  ; is armed so the CPU byte is ignored
    sub di, bx                  ; DI is on the right edge: step back over the
    mov cx, bx                  ; interior rather than reload the left byte
    rep stosb
.vnext:
    dec word [gfx_sp_n]
    jnz .vrow
    call vga_gc_reset
%endif
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; gfx_sp_next - internal: the next span record, ready to write
; in:       [gfx_sp_seg]:[gfx_sp_si] = the list, [gfx_sp_row] = the row it names
; out:      CF = 1 the row is EMPTY; otherwise AH = the left edge's mask, AL =
;           the right edge's, BX = right byte - left byte, DI = the framebuffer
;           offset of the left byte. ES = the framebuffer either way, and the
;           row is stepped either way.
; clobbers: cx, dx, si, bp, flags
;
; This is gfx_rect_setup minus the two things a RUN already knows - the row is
; one row, and its base is the last one stepped rather than gfx_rowbase's
; lookup and multiply - and minus the scratch. **IT ANSWERS IN REGISTERS AND
; THAT IS THE POINT**: writing [gfx_lmsk]/[gfx_rmsk]/[gfx_rspan]/[gfx_roff] for a
; row writer three instructions away to read back is four stores and six loads
; a span, and on an 8088 a word through memory costs what a masked framebuffer
; byte costs. Both writers take the same four registers, so neither carries a
; second copy of the clip or the mask lookups.
;
; The three `shr` are not a missed `mov cl, 3`: a variable shift is 8 clocks
; plus 4 a bit and needs CX freed around it, and CX is carrying both masks
; here. Three of them are six bytes and six clocks (SPEC.md 5.7 rule 3).
; -----------------------------------------------------------------------------
gfx_sp_next:
    mov bp, [gfx_sp_row]        ; BP = THIS row's base; the word steps to the
    mov ax, bp                  ; next, gfx_nextrow inlined (SPEC.md 5.7 r6)
    add ax, [cs:vid_rowadd]
    test ax, [cs:vid_wrapbit]
    jz .nw
    add ax, [cs:vid_wrapfix]
.nw:
    mov [gfx_sp_row], ax
    mov es, [gfx_sp_seg]
    mov si, [gfx_sp_si]
    add word [gfx_sp_si], 4
    mov ax, [es:si]             ; x1
    mov dx, [es:si+2]           ; x2
    mov es, [gfx_sp_fb]         ; ...and ES BACK TO THE FRAMEBUFFER. The list
                                ; is the CALLER's segment (SPEC.md 20.3's X
                                ; cell) and the row loops write through ES, so
                                ; leaving it here writes the picture into the
                                ; package's own image - which reads as memory
                                ; corruption and not as a wrong segment
                                ; register, because the bytes really are ours
                                ; (SPEC.md 77.10's shape, one layer down)
    or ax, ax                   ; x1 < 0 -> 0
    jns .x1
    xor ax, ax
.x1:
    cmp dx, [vid_cw]            ; x2 past the last column -> the last column
    jl .x2
    mov dx, [vid_cwm1]
.x2:
    cmp dx, ax
    jl .empty
    mov bx, ax                  ; left mask = FF >> (x1 & 7)
    and bx, 7
    mov ch, [bx + gfx_lmtab]
    mov bx, dx                  ; right mask = FF << (7 - (x2 & 7))
    and bx, 7
    mov cl, [bx + gfx_rmtab]
    mov bx, dx
    shr bx, 1
    shr bx, 1
    shr bx, 1                   ; BX = right byte column
    shr ax, 1
    shr ax, 1
    shr ax, 1                   ; AX = left byte column
    sub bx, ax
    jnz .wide
    and ch, cl                  ; one byte holds both edges
.wide:
    add ax, bp
    mov di, ax                  ; DI = the left byte
    mov ax, cx                  ; AH = left mask, AL = right mask
    clc
    ret
.empty:
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_fill_gray - 50% dither: (x+y) even = white, odd = black (desktop)
;
; Ignores [gfx_color]. Bytes start on even x, so the pattern byte depends
; only on row parity: 0AAh on even rows, 55h on odd. With Set/Reset off and
; Map Mask 0Fh the CPU byte lands in all four planes, giving color 15/0
; per bit directly.
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
;
; THE VGA BODY BELOW IS DELIBERATELY ITS OWN, and it looks like 139 bytes of
; duplication because it nearly is: gfx_fill_pat_raw's VGA body is this one
; with the row byte read out of a table where this toggles `not bh`, so with
; the eight bytes AA 55 AA 55 AA 55 AA 55 they are the same function. That
; merge was taken and MEASURED: gfxbench GFX_FILL_GRAY 64x64 on MartyPC VGA
; went 4,266.31 -> 5,685.53 us, +33.27% for 139 bytes, and it was reverted.
; UI_GRAY is os88ui_sbar's trough, so the bill lands on every scrollbar in
; every package and on every C package through os88thunk.asm. Do not re-derive
; it: docs/LAST-DROP-BYTES.md 7.6 carries the numbers and the flip condition.
; -----------------------------------------------------------------------------
gfx_fill_gray:
    GFXCLIP gfx_fill_gray_d
gfx_fill_gray_d:
    GFXDISP gfx_fill_gray_raw
%ifndef GFX_VGA
    NOVGA_ARM gfx_fill_gray_raw, sw_fill_gray
%else                           ; renderer is not a branch - it is the
gfx_fill_gray_raw:                              ; only renderer gfx_fill_gray has
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_fill_gray
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call gfx_rect_setup
    jc .done
    mov bl, 0xAA                ; pattern for the top clipped row
    test byte [gfx_ry1], 1
    jz .parity
    mov bl, 0x55
.parity:
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ax, [vga_m8l]           ; left edge column: AL = 8, AH = the mask
    out dx, ax
    mov di, [gfx_roff]
    mov si, [gfx_rrows]
    mov bh, bl
.lcol:
    mov al, [es:di]             ; load latches for the masked write
    mov [es:di], bh
    not bh                      ; AA <-> 55 each row
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov cx, [gfx_rspan]
    or cx, cx
    jz .reset
    mov ax, [vga_m8r]           ; right edge column
    out dx, ax
    mov di, [gfx_roff]
    add di, cx
    mov si, [gfx_rrows]
    mov bh, bl
.rcol:
    mov al, [es:di]
    mov [es:di], bh
    not bh
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec cx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [gfx_roff]
    inc di
    mov si, [gfx_rrows]
    mov ah, bl                  ; the row pattern moves to AH so BX can carry
    mov bx, cx                  ; the count the rep destroys, and DX the row
    mov dx, ROW_BYTES           ; step less that count (SPEC.md 5.7.1)
    sub dx, bx
.irow:
    mov al, ah
    mov cx, bx
    rep stosb
    add di, dx
    not ah
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_fill_pat - 8x8 dither pattern fill, screen-aligned (SPEC.md 5)
;
; Row y takes the byte [gfx_pat][y & 7]; bit set = white (15), clear =
; black (0), bit 7 the leftmost pixel of each screen byte. Ignores
; [gfx_color] and, like gfx_fill_gray, writes only colors 0/15, so it
; is a pattern rather than a solid colour. Same masked-edge scheme as
; gfx_fill_gray, with the row byte fetched from the table instead of the
; AA/55 toggle. The 8 pattern bytes are staged into gfx_patbuf first:
; [bp+bx] is not an 8086 addressing mode, but [gfx_patbuf+bx] is (and the
; bb twin shares the staging).
; in:       AX = x1, BX = y1, CX = x2, DX = y2; [gfx_pat] = pattern ptr
; out:      -
; clobbers: flags
;
; Clipped like the other rect fills, and it re-enters per fragment for free
; because the pattern is SCREEN-aligned: the row byte is [gfx_ry1] & 7 off a
; table staged from [gfx_pat] on every call, so a fragment starting at any y
; picks the same byte the whole rect would have. It was the one rect
; primitive without the hook, and the Task Manager's memory map is drawn
; almost entirely out of it - the claim bands, the buffer texture and each
; package's region pattern - so a map redrawn behind another window painted
; its full width straight across whatever was on top. The kernel's own band
; stopped at the right place, because gfx_fill_gray had the hook, which is
; what made it look like the map was drawing the wrong SHAPE rather than in
; the wrong PLACE.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; osapi_gfx_fill_pat - API slot 0x02B0's X body (SPEC.md 20.9)
; in:  AX/BX/CX/DX = the rect, ES:SI = the caller's 8 pattern bytes
; out: nothing; SI preserved
;
; The eight bytes are staged into the kernel's own segment first, the
; dsk_get_dir idiom (SPEC.md 2.1), because gfx_pat_stage reads [gfx_pat]
; through DS and DS is KERNEL_SEG by the time it runs. [gfx_pat] is a NEAR
; pointer and always was; giving it a segment would put one on every kernel
; caller of a primitive that is drawn per fragment.
; -----------------------------------------------------------------------------
osapi_gfx_fill_pat:
    push ax
    push cx
    push si
    push di
    mov di, api_pat
    mov cx, 8
.copy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .copy
    mov word [gfx_pat], api_pat
    pop di
    pop si
    pop cx
    pop ax
                                ; ...AND FALLS THROUGH into gfx_fill_pat, whose
                                ; ret is this routine's.  It was `call
                                ; gfx_fill_pat / ret`, four bytes to reach the
                                ; label on the next line.  NOTHING MAY BE
                                ; INSERTED BETWEEN THESE TWO LABELS
gfx_fill_pat:
    GFXCLIP gfx_fill_pat_d
gfx_fill_pat_d:
    GFXDISP gfx_fill_pat_raw
%ifndef GFX_VGA
    NOVGA_ARM gfx_fill_pat_raw, sw_fill_pat
%else                           ; renderer is not a branch - it is the
gfx_fill_pat_raw:                              ; only renderer gfx_fill_pat has
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_fill_pat
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call gfx_rect_setup
    jc .done
    call gfx_pat_stage          ; gfx_patbuf <- the 8 bytes at [gfx_pat]
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ax, [vga_m8l]           ; left edge column: AL = 8, AH = the mask
    out dx, ax
    mov di, [gfx_roff]
    mov si, [gfx_rrows]
    mov bx, [gfx_ry1]
    and bx, 7
.lcol:
    mov ah, [gfx_patbuf+bx]     ; row byte first: the masked write must
    mov al, [es:di]             ; follow the latch load immediately
    mov [es:di], ah
    inc bx
    and bx, 7
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov cx, [gfx_rspan]
    or cx, cx
    jz .reset
    mov ax, [vga_m8r]           ; right edge column
    out dx, ax
    mov di, [gfx_roff]
    add di, cx
    mov si, [gfx_rrows]
    mov bx, [gfx_ry1]
    and bx, 7
.rcol:
    mov ah, [gfx_patbuf+bx]
    mov al, [es:di]
    mov [es:di], ah
    inc bx
    and bx, 7
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec cx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [gfx_roff]
    inc di
    mov si, [gfx_rrows]
    mov dx, cx                  ; DX = the count, which the rep destroys; BX is
    mov bx, [gfx_ry1]            ; the pattern row and BP is not ours to spend,
    and bx, 7                   ; so the row start is unwound (SPEC.md 5.7.1)
.irow:
    mov al, [gfx_patbuf+bx]
    mov cx, dx
    rep stosb
    sub di, dx
    add di, ROW_BYTES
    inc bx
    and bx, 7
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_xor_rect - 1px outline rectangle, XOR 0Fh (drag outline)
;
; Self-inverting: drawing it twice restores the screen. Each edge pixel is
; touched exactly once - the verticals skip the corner rows, and degenerate
; rects (x1=x2 and/or y1=y2) collapse to a single line/pixel rather than
; XORing anything twice.
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_xor_rect:
    cmp word [wm_clip_n], 0     ; clipped background draw? (SPEC.md 11.3)
    jne gfx_xor_rect_clip       ; an OUTLINE is not the intersection of its
                                ; bounding rect with anything - clipping it
                                ; that way would draw edges through the
                                ; middle of it, so it decomposes first
                                ; (...and each strip meets GFXCLIP, whose
                                ; clipped branch owes no hide: wm_clip_set
                                ; already asked cur_lazyck - 7.1.4)
    call cur_unlazy              ; UNCLIPPED: 7.1.4/7.1.5
    jmp short gfx_xor_rect_d

vga_xor_rect_vram:
    call cur_unlazy              ; the drag outline bypasses the clip
                                ; (SPEC.md 11.3): 7.1.4. VRAM body under its
                                ; own name, which the transient overlays call
                                ; direct - so it owes the hide on its own
                                ; account. Reached from above the flag is
                                ; already spent and this is three instructions
                                ; (cur_unlazy is one-way within a hold)

; SPEC.md 39.14.6: THE DISPLAY HOOK GOES ABOVE THE [vid_mono] DISPATCH, and
; the reason is the same one SPEC.md 11.3 gives for the clip hook - except
; that here getting it the wrong way round does not merely fail to draw.
; `[vid_mono]` is not a property of the MACHINE on a two-card desktop, it is a
; property of the display the renderer is pointed at, and only GFXDENTER
; points it. Tested first, it answers about whichever display the LAST
; primitive happened to leave current (39.14.3 deliberately does not restore
; one), so a rect on the VGA dispatched while the Hercules was still active
; chose the SOFTWARE renderer - and then GFXDENTER switched the live block to
; the VGA underneath it, where [vid_rseg] is 0. sw_col then wrote through
; ES:DI = 0960:8FC1, which is COLD_SEG:45C1: the file manager's own code.
; Measured, one click on a Disk window with a straddling window over it left
; 34 differing runs in .cold at a stride of 80 - a scan line - and what it
; overwrote next ran: a `call far` became `cbw`, DS went to 0, fm_layout's
; stores landed 0x600 low across ui_rebootq, and the machine rebooted or
; froze (docs/DUAL-DISPLAY-VGA.md 8(11), docs/FIELD-NOTES.md).
;
; It is whole-shape rather than split, for gfx_blit4's reason: an outline is
; not the intersection of its bounding rect with anything (see the clipped
; path below), and the four strips it decomposes into - either renderer's -
; are already inside the nest by the time they are drawn. The frame is OUT
; HERE, where gfx_line and gfx_scroll keep theirs: the hook translates AX/BX
; and GFXDORG CX/DX and nothing puts them back (39.14.2), so without it a
; primitive whose contract is `out: -` answers DISPLAY-LOCAL coordinates.
gfx_xor_rect_d:
    push ax
    push bx
    push cx
    push dx
    push bp
    mov bp, gfx_xor_fill_raw    ; already inside the display hook and past the
                                ; clip, so the strips take the RAW entry
    GFXDENTERR
    call gfx_xor_strips
    GFXDLEAVE
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_xor_strips - AN OUTLINE AS FOUR FILLED STRIPS, once (SPEC.md 5)
; in:       AX = x1, BX = y1, CX = x2, DX = y2; BP = the FILL to draw each
;           strip with
; out:      -
; clobbers: flags. Every register including BP comes back the caller's.
;
; This decomposition was written out THREE times - here, in gfx_xor_rect_raw
; (VGA, through hand-rolled vga_xor_hline and vga_vline_core, all three now
; deleted), and in softgfx.inc's sw_xor_rect (deleted with them). gfx_frame was a FOURTH of the same rectangle with
; overlapping corners, which in replace mode draws the identical pixels: the
; top and bottom strips are full width, so they cover the corners the
; verticals used to draw twice. The degenerate cases are the same three tests
; in all four.
;
; Each pixel is touched exactly ONCE, which is what keeps the XOR forms
; self-inverting.
;
; BP CARRIES THE FILL, which is how gfx_line already uses it in this file. It
; survives every callee: gfx_fill and gfx_xor_fill document "clobbers: flags";
; vga_solid_rect names no BP at all; gfx_clip_run, gfx_disp_run,
; gfx_rect_setup, vga_set_color, vga_set_xor and vga_gc_reset name none
; either; sw_rect brackets push bp/pop bp around sw_plane_op, whose own header
; declares it clobbers BP; and cur_unlazy's cursor_hide pushes it.
; gfx_fill_pat_raw's "BP is not ours to spend" is the same invariant from the
; other side.
;
; The three heads: gfx_xor_rect_d passes gfx_xor_fill_raw (already hooked and
; past the clip), gfx_xor_rect's clipped arm passes the PUBLIC gfx_xor_fill so
; each strip meets GFXCLIP itself (SPEC.md 11.3 - an OUTLINE is not the
; intersection of its bounding rect with anything), and gfx_frame passes
; gfx_fill.
; -----------------------------------------------------------------------------
gfx_xor_rect_clip:              ; the clipped head: each strip meets GFXCLIP
    push bp
    mov bp, gfx_xor_fill
    call gfx_xor_strips
    pop bp
    ret

gfx_xor_strips:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx                  ; SI = y1, DI = y2
    mov di, dx
    mov dx, si
    call bp                     ; top row
    cmp di, si
    je .out                     ; y1 = y2: one line, done
    mov bx, di
    mov dx, di
    call bp                     ; bottom row
    mov bx, si                  ; verticals span y1+1 .. y2-1
    inc bx
    mov dx, di
    dec dx
    cmp dx, bx
    jl .out                     ; only two rows: no vertical part
    push cx
    mov cx, ax
    call bp                     ; left column
    pop cx
    cmp cx, ax
    je .out                     ; x1 = x2: one column, done
    mov ax, cx
    call bp                     ; right column
.out:
    jmp kret_di

; -----------------------------------------------------------------------------
; gfx_xor_fill - filled rectangle, XOR 0Fh (menu highlight)
;
; Every byte read-before-write; no rep stosb anywhere (SPEC.md 5).
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_xor_fill:
    GFXCLIP gfx_xor_fill_d
gfx_xor_fill_d:
    GFXDISP gfx_xor_fill_raw
gfx_xor_fill_raw:
%ifndef GFX_VGA
    jmp sw_xor_fill
vga_xor_fill_vram:              ; ...and this one keeps its cur_unlazy: it is
    call cur_unlazy             ; entered DIRECTLY by menu.inc and kernel.asm's
    jmp sw_xor_fill             ; thunk, which have not hidden the arrow (7.1.4)
%else
    cmp byte [vid_mono], 0      ; 1bpp? then the software renderer IS
    jne sw_xor_fill
vga_xor_fill_vram:
    call cur_unlazy              ; ...and the menu highlights with it. VRAM
                                ; body under its own name (see
                                ; vga_xor_rect_vram). The re-test below is
                                ; NOT redundant: this label is entered
                                ; directly by menu.inc and by kernel.asm's
                                ; thunk, so it owes its own adapter check
    cmp byte [vid_mono], 0
    jne sw_xor_fill
    mov byte [vga_xorm], 1      ; ...and the body is gfx_fill_raw's, which is
    jmp vga_solid_rect          ; the same 155 bytes with two differences
%endif

; -----------------------------------------------------------------------------
; gfx_save - copy a screen region to a buffer, plane by plane
;
; x1 is rounded down and x2 up to byte boundaries internally. Buffer layout:
; all plane-0 rows, then plane 1, plane 2, plane 3 (rows top to bottom,
; ((x2/8)-(x1/8)+1) bytes each). Uses Read Map Select (GC4) per plane.
; in:       AX = x1, BX = y1, CX = x2, DX = y2, ES:DI = buffer
; out:      DI advanced past the data (unchanged if the clipped rect is
;           empty); ES preserved
; clobbers: flags
;
; Dispatches to sw_save on a 1bpp adapter (SPEC.md 39.5). vga_save_vram is
; the VRAM body under its own name: the mouse cursor's save-under calls it
; directly, without cur_unlazy.
; -----------------------------------------------------------------------------
gfx_save:
    GFXAUDIT_HOOK
    call cur_unlazy              ; the menu save-under READS the screen, so
                                ; the arrow must not be in what it banks
%ifdef GFXAUDIT
    cmp byte [cur_level], 0     ; ...AND THAT IS AN INVARIANT, not a hope
    jl gfx_bank_ok              ; (SPEC.md 11.101.2): the line above only
    inc word [gfx_aud_bank]     ; spends a hide a LOCK HOLD promised, so an
gfx_bank_ok:                    ; unlocked caller banks the arrow and nothing
%endif                          ; says so until the cache is used
    push ax                     ; the rect is banked around the hook, which
    push bx                     ; translates it and never puts it back - DI is
    push cx                     ; NOT, because DI advancing is this routine's
    push dx                     ; documented answer
    GFXDENTERR                  ; ...and it reads it off ONE CARD (SPEC.md
                                ; 39.14.8): a save is pixels, and pixels live
    call vga_save_vram          ; on a card rather than in a coordinate space,
    GFXDLEAVE                   ; so this is whole-shape like gfx_scroll and
    call gfx_1bpp_off           ; ...and a ONE-PLANE arm is spent (11.96.17)
    pop dx                      ; NOT split like a blit's runs. The caller owes
    pop cx
    pop bx
    pop ax
    ret
                                ; the straddle test - wm_su_take asks
                                ; vid_span_one - because a cut save and a cut
                                ; restore agree with each other and leave the
                                ; rest of the rect stale.
                                ;
                                ; BELOW the hook and reached directly by the
                                ; CURSOR out of IRQ4, which must not take it:
                                ; the arrow brackets itself (39.15.2), and a
                                ; hook here would translate a rect that
                                ; cur_geom has already resolved.
%ifndef GFX_VGA
    NOVGA_ARM vga_save_vram, sw_save
%else                           ; renderer is not a branch - it is the
vga_save_vram:                              ; only renderer gfx_save has
    cmp byte [vid_mono], 0      ; the cursor's save-under, from inside IRQ4:
    jne sw_save                 ; on mono the framebuffer IS the renderer's
                                ; target, so one "plane" of sw_xfer is it
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    call gfx_rect_setup
    jc .done
    mov byte [vga_plane], 0
.plane:
    mov dx, VGA_GC
    mov ah, [vga_plane]
    mov al, 4                   ; GC4: Read Map Select
    out dx, ax
    mov si, [gfx_roff]
    mov dx, [gfx_rrows]
    mov bp, [gfx_rspan]
    inc bp                      ; bp = bytes per row
    push ds
    mov ax, VGA_SEG
    mov ds, ax                  ; movsb source = VRAM
.row:
    mov cx, bp
    rep movsb                   ; buffer DI advances contiguously
    add si, ROW_BYTES
    sub si, bp
    dec dx
    jnz .row
    pop ds
    inc byte [vga_plane]
    mov al, [vga_pn]            ; ...as many planes as the caller armed
    cmp [vga_plane], al
    jb .plane
    mov dx, VGA_GC
    mov ax, 0x0004              ; GC4 back to plane 0 (default)
    out dx, ax
.done:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; --- the SUB-RECT restore (SPEC.md 5.8) --------------------------------------
;
; Three words that make gfx_restore put back PART of a buffer instead of all of
; it. They are in .text with real initialisers, not .bss: `-f bin` zeroes
; nothing, and the mouse cursor reaches vga_restore_vram on the machine's first
; pointer move, long before anything arms these (the cur_sptr trap, SPEC.md
; 7.1.2).
;
; ALL THREE ARE 0 AT REST, which is what makes the whole feature additive: the
; two restore bodies read them unconditionally and a zero skip is the walk they
; had, so the cursor's own path and the menu save-under are byte-identical.
; --- SPEC.md 11.96.17's ONE-PLANE save and restore --------------------------
;
; A window whose content is only colour 0 and colour 15 has four IDENTICAL
; planes, so one plane is the whole of it. Reading it back is one pass of GC4
; = 0 instead of four; putting it back is one pass with SEQ2 Map Mask = 0Fh,
; because with Set/Reset disabled that lands one CPU byte in all four planes -
; the resting framebuffer state gfx_blit1's default pen already ships on
; (SPEC.md 5.4.2.2). A quarter of the buffer and a quarter of the writes.
;
; 4 and 1 AT REST, which is gfx_sub_*'s discipline below and for its reason:
; the two bodies read them unconditionally and the resting pair IS the walk
; they had, so the cursor's own path is byte-identical. gfx_save and
; gfx_restore put them back, so an arm is good for exactly ONE call.
;
; THAT IS NOT WHAT KEEPS IT OFF THE POINTER, and this comment said it was.
; cur_saveu and cur_restoreu reach vga_save_vram and vga_restore_vram DIRECTLY
; out of IRQ4 - and gfx_save and gfx_restore call cur_unlazy at their TOP,
; above their own disarm, so the arrow was hidden and put back with a live
; one-plane arm every time a window that promised OSAPI_SAVEU_1BPP was banked.
; The pair is banked and cleared by those two routines now, at the one point
; both cursor paths pass through (SPEC.md 11.96.17).
;
; The restore's loop needs no terminator of its own: seeded with 0Fh it does
; one pass and `add al, al` gives 1Eh, which fails `cmp al, 0x10`.
vga_pn      db 4                ; planes a SAVE walks: 4, or 1 for one plane
vga_pm      db 1                ; ...and the Map Mask a RESTORE starts on: the
                                ; single-plane bit 1, or 0Fh to broadcast

; gfx_1bpp_on / gfx_1bpp_off - arm the next gfx_save/gfx_restore for ONE plane
; Preserves every register and the flags.
gfx_1bpp_on:
    mov byte [vga_pn], 1
    mov byte [vga_pm], 0x0F
    ret
gfx_1bpp_off:
    mov byte [vga_pn], 4
    mov byte [vga_pm], 1
    ret

gfx_sub_st  dw 0                ; buffer offset of the sub-rect's first byte
gfx_sub_rs  dw 0                ; ...bytes to step the BUFFER past each row
gfx_sub_ps  dw 0                ; ...and past each plane

; -----------------------------------------------------------------------------
; gfx_sub_arm - arm the next gfx_restore to put back only part of its buffer
; in:  AX = x1, BX = y1, CX = x2, DX = y2 - the sub-rect wanted; SI -> the four
;      words (x1, y1, x2, y2) the buffer was SAVED over
; out: CF = 0, armed; CF = 1 = refused and nothing armed, so the caller's
;      gfx_restore puts the whole buffer back - which is always correct, only
;      dearer. Preserves every register.
;
; gfx_save's buffer is plane-major, `full_row` bytes per row, x1 rounded down
; and x2 up (SPEC.md 5), so a sub-rect is three numbers: where its first byte
; is, how far past the end of each row its next row starts, and the same past
; each plane. The SCREEN side needs nothing at all - gfx_rect_setup recomputes
; the geometry from whatever rect it is handed, so the caller passes the
; sub-rect and it lands in the right place.
;
; IT REFUSES RATHER THAN CLAMPS a sub-rect that is not inside the saved one:
; clamping would silently restore something the caller did not ask for, and
; every refusal has a correct fallback one branch away.
; -----------------------------------------------------------------------------
gfx_sub_arm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp ax, [si]                ; inside the saved rect on all four sides, or
    jl .no                      ; the arithmetic below indexes outside the
    cmp bx, [si+2]              ; buffer - which is a claim of a few KB, so it
    jl .no                      ; would be somebody else's memory
    cmp cx, [si+4]
    jg .no
    cmp dx, [si+6]
    jg .no
    cmp ax, cx                  ; ...and not degenerate
    jg .no
    cmp bx, dx
    jg .no

    ; The three words double as the scratch this arithmetic needs, so it costs
    ; no .bss: every register is wanted, CL has to be the shift count (an 8086
    ; shifts by 1 or by CL and nothing else), and both `mul`s eat DX.
    mov [gfx_sub_st], ax        ; S.x1
    mov [gfx_sub_rs], cx        ; S.x2
    mov [gfx_sub_ps], dx        ; S.y2
    push bx                     ; S.y1
    mov cl, 3

    mov ax, [si+4]              ; DI = full_row, the buffer's bytes per row;
    shr ax, cl                  ; BX = the saved rect's own left byte column
    mov bx, [si]
    shr bx, cl
    mov di, ax
    sub di, bx
    inc di

    mov ax, [gfx_sub_rs]        ; BP = sub_row, the bytes to move per row
    shr ax, cl
    mov bp, ax
    mov ax, [gfx_sub_st]
    shr ax, cl
    sub bp, ax
    inc bp
    sub ax, bx                  ; AX = the sub-rect's byte column within a
                                ; buffer row

    pop bx                      ; CX = sub_rows, BX = the sub-rect's first row
    mov cx, [gfx_sub_ps]        ; as an offset into the buffer. (CL was the
    sub cx, bx                  ; shift count and every shift is done.)
    inc cx
    sub bx, [si+2]

    push ax                     ; start = that row x full_row + that column.
    mov ax, bx                  ; No overflow to worry about: wm_su_kb has
    mul di                      ; already refused a buffer bigger than one
    pop bx                      ; segment, so every offset in it fits a word
    add ax, bx
    mov [gfx_sub_st], ax

    mov ax, di                  ; row_skip: what the row loop must step past
    sub ax, bp                  ; after each rep movsb
    mov [gfx_sub_rs], ax

    mov ax, [si+6]              ; plane_skip: the saved rows this rect does not
    sub ax, [si+2]              ; cover, one whole buffer row each
    inc ax
    sub ax, cx
    mul di
    mov [gfx_sub_ps], ax
    clc
    jmp short .out
.no:
    stc
.out:
    jmp kret_bp

; -----------------------------------------------------------------------------
; gfx_sub_off - back to restoring whole buffers
; Called by both restore bodies on their way out, so a caller CANNOT leave one
; armed: the safe value is the one a forgetful caller inherits, which is
; wm_dmg_dk's rule (SPEC.md 11.91). Preserves every register; clobbers flags,
; which both callers have already finished with.
; -----------------------------------------------------------------------------
gfx_sub_off:
    mov word [gfx_sub_st], 0
    mov word [gfx_sub_rs], 0
    mov word [gfx_sub_ps], 0
    ret

; -----------------------------------------------------------------------------
; gfx_restore - write a gfx_save buffer back to the screen
;
; Same rounding and buffer layout as gfx_save. Writes through the Sequencer
; Map Mask one plane at a time with Set/Reset off and Bit Mask FF, so the
; CPU bytes land verbatim and the latches are irrelevant.
; in:       AX = x1, BX = y1, CX = x2, DX = y2, ES:SI = buffer
; out:      SI advanced past the data (unchanged if the clipped rect is
;           empty); ES preserved
; clobbers: flags
;
; Dispatches to sw_restore on a 1bpp adapter (SPEC.md 39.5);
; vga_restore_vram is the VRAM body for the cursor path (see gfx_save).
; -----------------------------------------------------------------------------
gfx_restore:
    GFXAUDIT_HOOK
    call cur_unlazy              ; ...and writes it back, unclipped
    push ax                     ; same frame as gfx_save's, and SI is outside
    push bx                     ; it for the same reason: SI advancing is what
    push cx                     ; this routine answers
    push dx
    GFXDENTERR                  ; ...onto the card it came off (SPEC.md
                                ; 39.14.8) - gfx_save's twin, and it has to be
    call vga_restore_vram       ; the same hook or a rect banked on one display
    GFXDLEAVE                   ; could be put back on the other
    call gfx_1bpp_off           ; ...and a ONE-PLANE arm is spent (11.96.17)
    jmp kret_dx
%ifndef GFX_VGA
    NOVGA_ARM vga_restore_vram, sw_restore
%else                           ; renderer is not a branch - it is the
vga_restore_vram:                              ; only renderer gfx_restore has
    cmp byte [vid_mono], 0      ; see vga_save_vram
    jne sw_restore
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    call gfx_rect_setup
    jc .done
    add si, [gfx_sub_st]        ; the SUB-RECT restore (SPEC.md 5.8), and 0 at
                                ; rest: DS is still KERNEL_SEG out here, so
                                ; this one needs no override
    mov al, [vga_pm]            ; plane mask bit, 1/2/4/8 - or 0Fh, which is
    mov [vga_plane], al         ; ONE pass into all four (SPEC.md 11.96.17)
.plane:
    mov dx, VGA_SEQ
    mov ah, [vga_plane]
    mov al, 2                   ; SEQ2: Map Mask = this plane only
    out dx, ax
    mov di, [gfx_roff]
    mov dx, [gfx_rrows]
    mov bp, [gfx_rspan]
    inc bp                      ; bp = bytes per row
    push ds
    push es
    push es
    pop ds                      ; movsb source = caller's buffer
    mov ax, VGA_SEG
    mov es, ax                  ; movsb dest = VRAM
.row:
    mov cx, bp
    rep movsb
    add si, [cs:gfx_sub_rs]     ; CS: IS MANDATORY - DS is the BUFFER in here
    add di, ROW_BYTES
    sub di, bp
    dec dx
    jnz .row
    pop es
    pop ds
    add si, [gfx_sub_ps]        ; ...and past the rows this rect does not cover
                                ; (DS is KERNEL_SEG again by here)
    mov al, [vga_plane]
    add al, al                  ; next plane bit
    mov [vga_plane], al
    cmp al, 0x10
    jb .plane
    mov dx, VGA_SEQ
    mov ax, 0x0F02              ; SEQ2: Map Mask back to all planes
    out dx, ax
.done:
    call gfx_sub_off            ; a sub-rect is ONE restore (SPEC.md 5.8)
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; gfx_pen_live / gfx_pen_dis / gfx_pen_cf - the enabled and disabled pens
;
; in:       gfx_pen_cf only: CF = 0 live, CF = 1 disabled - which is the shape
;           EVERY greying predicate in the tree already answers in, so a call
;           site is `call <ok-test>` then `call gfx_pen_cf` and nothing else
; out:      nothing
; clobbers: nothing (flags)
;
; SPEC.md 47 makes a disabled control two things at once - the CDGRAY pen and
; the [gfx_dis] flag - and these exist so no call site can set one without the
; other. That is not hypothetical tidiness: the bug this whole section came
; out of was a row whose LABEL was greyed and whose GLYPH was not, and "two
; halves of one control disagreeing" is the failure mode the flag would
; otherwise invite a second time.
;
; gfx_pen_live returns the pen to CBLACK, which is what every site in the tree
; wants back. A site that needs some other colour afterwards sets it itself -
; but it must still come through here, or the flag stays armed and the next
; string on this lock hold draws dithered.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; thm_bg / thm_ink - set the pen to the chrome's background / ink (SPEC.md 76)
; in:  -   out: [gfx_color]   clobbers: nothing (flags preserved)
;
; A near call is THREE bytes where `mov byte [gfx_color], CWHITE` is five, so
; the substitution pays for these two routines several times over. Flags are
; preserved because several chrome sites set the pen between a CF-setting call
; and the branch that reads it (wm_draw_title's wm_flush, SPEC.md 11.95.2).
; -----------------------------------------------------------------------------
%ifndef OS88_THEME              ; NO THEME HERE (SPEC.md 76.13): the pens keep
thm_bg:                         ; their names and become CONSTANTS, so every
    mov byte [gfx_color], CWHITE; chrome site in the kernel is untouched - and
    ret                         ; a 3-byte call is still shorter than the
thm_ink:                        ; 5-byte immediate store it replaced, so the
    mov byte [gfx_color], CBLACK; small build comes out AHEAD of the tree that
    ret                         ; never had a theme. Flags survive: neither
%else                           ; instruction writes any
thm_bg:
    pushf
    push ax
    mov al, [thm_bgc]
    mov [gfx_color], al
    pop ax
    popf
    ret
thm_ink:
    pushf
    push ax
    mov al, [thm_inkc]
    mov [gfx_color], al
    pop ax
    popf
    ret

; ...and the ACTIVE TITLE BAR's pair (SPEC.md 76.12). Equal to the two above in
; both 1bpp themes, so a build that only ever had one pair draws the same
; pixels; under Color they are white-on-blue, which is the whole of what makes
; focus visible at a glance.
thm_tbg:
    pushf
    push ax
    mov al, [thm_tbgc]
    mov [gfx_color], al
    pop ax
    popf
    ret
thm_tink:
    pushf
    push ax
    mov al, [thm_tinkc]
    mov [gfx_color], al
    pop ax
    popf
    ret
%endif

%ifndef OS88_THEME              ; ...and with no theme the ACTIVE pair IS the
thm_tbg     equ thm_bg          ; chrome pair, aliased rather than %ifdef'd at
thm_tink    equ thm_ink         ; each of wm_draw_title's six sites - which is
%endif                          ; where a divergence would rot (SPEC.md 76.13)

; thm_pair - AL = chrome ink, AH = chrome background, for font_run
; thm_rpair - the INVERSE pair (SPEC.md 59's toast strip)
; The three theme bytes are declared ADJACENT for this: [thm_bgc] read as a
; WORD is already the inverse pair, so thm_rpair is one instruction and
; thm_pair is that plus an xchg.
%ifndef OS88_THEME
thm_pair:
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = background
    ret
thm_rpair:
    mov ax, (CBLACK << 8) | CWHITE
    ret
%else
thm_pair:
    mov ax, [thm_bgc]
    xchg al, ah
    ret
thm_rpair:
    mov ax, [thm_bgc]
    ret
%endif

; thm_desk - lay the desktop's ground over AX,BX..CX,DX (SPEC.md 76.2)
; Bright is SPEC.md 26's 50% dither; Dark is solid black. gfx_fill_gray is a
; CLIPPED primitive and so is gfx_fill, so wm_dmg_gray's region arithmetic is
; unchanged either way.
%ifndef OS88_THEME
thm_desk:
    jmp gfx_fill_gray           ; SPEC.md 26's dither, and the only ground
%else                           ; this build has
thm_desk:
    push ax
    mov al, [thm_deskc]
    cmp al, THM_DITHER
    je .dither
    mov [gfx_color], al
    pop ax
    jmp gfx_fill
.dither:
    pop ax
    jmp gfx_fill_gray
%endif

; thm_set - adopt AL (0/1) as the theme and resolve the two pens
%ifdef OS88_THEME
thm_set:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp al, THM_NKIND           ; a byte from a future version, or from a
    jb .known                   ; settings disk this kernel does not know
    xor al, al
.known:
%ifdef KERN_BIG
    cmp al, THM_COLOR           ; COLOUR IS A FACT ABOUT THE ADAPTER, and it is
    jne .ok                     ; refused HERE rather than at the two callers
    cmp byte [vid_kind], VID_VGA; (SPEC.md 76.12.1). The test is the DEPTH of
    je .ok                      ; the PRIMARY: VID_VGA and VID_EGA are the two
    cmp byte [vid_kind], VID_EGA; 4-plane kinds, HERC and CGA are 1bpp. It was
    je .ok                      ; `cmp [vid_kind], VID_VGA` while VGA was the
    xor al, al                  ; only colour adapter - a proxy VID_EGA broke -
.ok:                            ; and it is NOT [vid_mono]: on a two-card
%endif                          ; desktop that byte names the display LAST
                                ; DRAWN ON (SPEC.md 39.14.3, 5.4.2.4), not the
                                ; machine, and cp_thm_colgrey reads it from
                                ; the Control Panel's own paint, so spelt that
                                ; way a panel dragged onto the Hercules greyed
                                ; Color on a VGA machine. So a settings disk
                                ; carried to a Hercules machine, and SPEC.md
                                ; 39.11's Display page moving this one onto a
                                ; card with no colour in it, both land on the
                                ; same refusal and both fall back to Bright.
                                ; On kern_small THM_NKIND is 2, so the range
                                ; test above has already clamped it
    mov [thm_kind], al
    mov bl, al                  ; the row: index * 8, the row being padded to
    xor bh, bh                  ; 8 for exactly this
    mov cl, 3
    shl bx, cl
    add bx, thm_tab
    push ds                     ; ONE copy, so the live block cannot fall out
    pop es                      ; of step with the table field by field
    mov si, bx
    mov di, thm_bgc
    mov cx, 8
    cld
    rep movsb
    call menu_force             ; THE TWO DIFFING PAINTERS CANNOT SEE THIS
    call dock_force             ; (SPEC.md 76.7). menu_bcell is a record of
    pop es                      ; CHARACTERS and a dock tile's key is its icon
    pop di                      ; plus its marks - a theme change moves
    pop si                      ; NEITHER, so composing IS the diff finds no
    pop cx                      ; difference and the bar and the strip keep
    pop bx                      ; the colours they had. wm_paint_all does not
    pop ax                      ; cover it: it calls both painters and both
    ret                         ; are gated

%endif

; THESE TWO ARE CONTENT PENS AND THE THEME DOES NOT REACH THEM (SPEC.md 76.6).
; Every caller but one draws inside a WINDOW - the Control Panel's pages, the
; Standard File dialog, os88ui.inc's shared controls, and every package
; through OSAPI_GFX_PEN - and a window's content is white in both themes. The
; one caller that is chrome is the pull-down menu, and it has its own pair
; below rather than bending these: themed here, the Control Panel's radios
; and every package's greyed caption go white on white and vanish.
; -----------------------------------------------------------------------------
; gfx_blit1_pen - set the pen the NEXT gfx_blit1 emits with (SPEC.md 5.4.2.2)
; in:       AL = ink (what a SET band bit becomes), AH = paper (a CLEAR one)
; out:      nothing
; clobbers: nothing (flags)
;
; Not an argument to gfx_blit1 because that call has no register left - every
; one of the nine carries geometry - and not a persistent setting because
; gfx_unlock clears it. A caller sets it inside the lock hold it draws in,
; which is the same lifetime gfx_pen_cf publishes for [gfx_dis].
; -----------------------------------------------------------------------------
gfx_blit1_pen:
    mov [gfx_b1ink], al
    mov [gfx_b1pap], ah
    mov byte [gfx_b1adp], 0     ; ...and a PACKAGE's band is in screen polarity
    ret                         ; by SPEC.md 5.4.2.2: only band.inc's composer
                                ; sets that flag, and only for its own emit

gfx_pen_cf:
    jc gfx_pen_dis
gfx_pen_live:
    mov word [gfx_color], CBLACK | (0 << 8)   ; ...and [gfx_dis] with it
    ret
gfx_pen_dis:
    mov byte [gfx_color], CDGRAY
    cmp byte [gfx_mono1], 0         ; ...AND CDGRAY IS COLOUR 8, which a
    je .grey                        ; one-plane cache cannot carry - so for a
    mov byte [gfx_color], CBLACK    ; window that declared two colours the pen
.grey:                              ; rounds the way a 1bpp adapter's already
                                    ; does (SPEC.md 11.96.17.1). It reaches
                                    ; the FRAME and every solid fill a greyed
                                    ; control draws.
                                    ;
                                    ; THIS WAS WITHDRAWN ONCE AND IS BACK. On
                                    ; its own it made a greyed caption
                                    ; pixel-identical to a live one, because
                                    ; font_char sends a VGA to its .vram path
                                    ; where there is no dither to carry 47
                                    ; rule 1's signal. What changed is
                                    ; os88ui_btn: its caption is an opaque RUN
                                    ; now wherever the interior is filled, and
                                    ; font_run lays the checkerboard on every
                                    ; adapter. The stipple carries the signal,
                                    ; so the colour no longer has to
    mov word [gfx_dis], 1 | (CBLACK << 8)   ; ...and [gfx_disink] with it:
    ret                             ; content is white in both themes, so a
                                    ; greyed caption stipples in black

; ...and the CHROME's pair, for menu_drop's items (SPEC.md 76.6). Same shape,
; same [gfx_dis] contract - only the two colours come from the theme, so on
; Bright they are byte-for-byte what the pair above does - which is why the
; unthemed build simply ALIASES them to it (SPEC.md 76.13).
%ifndef OS88_THEME
thm_pen_live    equ gfx_pen_live
thm_pen_dis     equ gfx_pen_dis
%else
thm_pen_live:
    push ax
    mov al, [thm_inkc]
    mov [gfx_color], al
    pop ax
    mov byte [gfx_dis], 0
    ret
thm_pen_dis:
    push ax
    mov al, [thm_disc]          ; CDGRAY on white is a legible grey and on
    mov [gfx_color], al         ; black is very nearly nothing, so Dark takes
    mov al, [thm_inkc]          ; CLGRAY. That is the VGA answer; on 1bpp the
    mov [gfx_disink], al        ; flag buys a checkerboard and [gfx_disink]
    pop ax                      ; says what it is laid ON - this surface's own
    mov byte [gfx_dis], 1       ; live ink, so black on Bright and white on
    ret                         ; Dark (SPEC.md 76.6.1)
%endif

; -----------------------------------------------------------------------------
; gfx_lock - acquire the drawing mutex, then hide the cursor (SPEC.md 7)
;
; Spins with task_yield while another task holds it, so background tasks
; keep running. Task-level only - never call from an ISR. Non-reentrant:
; re-acquiring from the holder deadlocks.
; in:       -
; out:      -
; clobbers: flags (IF = 1 on return)
; -----------------------------------------------------------------------------

gfx_lock:
.retry:
    cli
    cmp byte [gfx_lock_flag], 0
    je .free
    mov byte [gfx_lock_want], 1 ; SPEC.md 7.3: say so BEFORE the sti, inside
                                ; the cli window that read the flag - a store
                                ; after it can land behind the very unlock it
                                ; was meant to be seen by
.block:
    sti
%ifndef NOPARKLK
    call inst_park_lk           ; BLOCKED HERE HOLDS NOTHING, if the package
%endif                          ; said so (SPEC.md 66.5.4) - which is what
    call task_yield             ; lets the compactor reach a DRAWING worker's
%ifndef NOPARKLK                ; claims at all. make HEAPPARKLK=0 is the A/B
    call inst_park_unlk
%endif
    jmp .retry

.free:                          ; the flag is clear - but SPEC.md 7.3 asks one
    push ax                     ; more question first. The save and the
    mov al, [sch_cur]           ; [sch_cur] read are .take's, made one step
    cmp byte [gfx_lock_want], 0 ; early and shared, so the fast path pays a
    je .take                    ; compare and a branch and nothing else
    cmp al, [gfx_lock_last]     ; somebody IS blocked. Am I the task that just
    jne .take                   ; let go? Then taking it again here IS the
    pop ax                      ; starvation: a worker that unlocks and
    mov byte [gfx_lock_want], 0 ; re-locks inside one slice wins every race
    jmp short .block            ; against a waiter that has to be SCHEDULED
                                ; before it can even look. The debt is one
                                ; scheduling window and it is spent HERE, so
                                ; the same task cannot starve a waiter twice
                                ; running
.take:
    mov byte [gfx_lock_flag], 1 ; AX is saved and AL = [sch_cur] already
    mov [gfx_lock_own], al      ; the owner is stamped inside the cli window,
    mov [gfx_lock_last], al     ; so flag and owner can never disagree - and
    pop ax                      ; this second one outlives the hold (SPEC 7.3)
    sti
    mov byte [cur_lazy], 1      ; PROMISE the hide (SPEC.md 7.1.4) - the ISR
    CURLOG 'L'
    ret                         ; will not move the arrow while the flag is
                                ; set, so it is frozen where it is until
                                ; something is about to draw over it

; -----------------------------------------------------------------------------
; gfx_unlock - show the cursor, then release the mutex (SPEC.md 7)
;
; Order is binding: cursor_show redraws at the latest mouse position while we
; still own the screen; only then may the ISR draw again.
;
; The clip region dies here too (SPEC.md 11.3 rule 1). Making it die with
; the lock hold is what stops a leaked region from silently truncating the
; next painter - and it is what keeps the drag outline and the menu
; highlights unclipped without any of them knowing the region exists. The
; DISPLAY CONTEXT dies with it, for the same reason (SPEC.md 39.14.3).
; in:       -
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_unlock:
    CURLOG 'U'
    mov word [wm_clip_n], 0
    mov byte [gfx_b1ink], CWHITE ; ...and so does gfx_blit1's (SPEC.md 5.4.2.2),
    mov byte [gfx_b1pap], CBLACK ; for the same reason one line down - the
    mov byte [gfx_b1adp], 0      ; composer's polarity declaration with it
                                 ; (SPEC.md 5.9.3.1): it is part of the pen and
                                 ; has exactly the pen's lifetime
    mov byte [gfx_mono1], 0     ; ...and so does whose content this was
                                ; (SPEC.md 11.96.17.1)
    mov byte [gfx_dis], 0       ; the disabled pen dies with the lock, for the
                                ; clip region's reason (SPEC.md 11.3/47): a
                                ; site that forgets gfx_pen_live cannot leak
                                ; dithered text into whatever draws next
%ifdef KERN_BIG
    push ax                     ; ...and so does the DISPLAY CONTEXT (SPEC.md
    xor al, al                  ; 39.14.3). Nothing in 39.14 restores it any
    call vid_ctx_act            ; more - the last display drawn on stays
    pop ax                      ; current, so a repaint pays one swap instead
                                ; of two per primitive - and this is what
                                ; bounds that to one lock hold, exactly as the
                                ; two stores above bound theirs. It is a
                                ; compare and a branch when nothing moved
%endif
    call fpg_finish             ; ANY UNLOCKING OF THE USER INTERFACE TAKES
                                ; SPEC.md 12.8'S PROGRESS WIDGET DOWN
                                ; (SPEC.md 12.8.3), and it is dsk_batch's
                                ; argument below in the other direction: the
                                ; freeze the widget reports IS this hold, so
                                ; the hold is its extent and no caller can
                                ; leave it on the bar - not by forgetting a
                                ; bracket, not by returning early, not by a
                                ; file operation that failed.
                                ;
                                ; HERE, and not below with the batch: it
                                ; DRAWS, so it has to run while we still own
                                ; the screen and the arrow is still down - and
                                ; after the clip region above, or the fills
                                ; that give the bed back would be cut by
                                ; whatever region the hold happened to leave
                                ; armed. It preserves the flags, which this
                                ; routine's contract requires, and is a
                                ; compare and a return on the unlocks that saw
                                ; no file work, which is nearly all of them
    cmp byte [cur_lazy], 0      ; was the hide ever spent? (SPEC.md 7.1.4)
    jne .never                  ; no: the arrow never left the screen, so
    call cursor_show            ; showing it would bank itself as background
    jmp short .rel
.never:
    CURLOG 'e'                  ; the promise SURVIVED: no show is owed
    call cur_lazyend            ; ...but the ISR could not move it all hold
.rel:
    mov byte [gfx_lock_own], 0xFF   ; owner first: a task switch between the
                                    ; two stores must not show flag-free with
                                    ; a stale owner still named
    mov byte [gfx_lock_flag], 0
    mov byte [dsk_batch], 0     ; ANY UNLOCKING OF THE USER INTERFACE ENDS A
                                ; BATCH (SPEC.md 18.9.3). One store, and it is
                                ; the whole safety argument: a caller cannot
                                ; leave a bracket open across a moment when the
                                ; user could reach the drive, because every way
                                ; out of a locked run of kernel code - return,
                                ; failure, a question, a swap prompt - comes
                                ; through here. mov leaves the flags alone,
                                ; which this routine's contract requires
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; gfx_scroll - vertical scroll blit (SPEC.md 5.5; API slot 0x01E0)
;
; in:       AX = x1, BX = y1, CX = x2, DX = y2 (inclusive, absolute screen
;           coords), SI = dy SIGNED: positive moves the rect's content UP dy
;           rows, negative down. Caller holds the gfx lock - which is what
;           makes the blit cursor-safe: the cursor is hidden under it.
; out:      CF=0 done; the |dy| vacated rows keep UNSPECIFIED content and
;           the caller repaints them. CF=1 refused, nothing moved: x1/(x2+1)
;           not multiples of 8, empty/inverted rect, |dy| = 0 or >= height,
;           rect off the live screen, or an armed clip region that does not
;           contain the whole rect (whole-shape clipping, the font_char/
;           ico_core class - a blit cannot be cut per pixel; the caller
;           falls back to a full repaint, which clips per strip).
; clobbers: nothing (registers preserved; CF is the answer)
;
; Two backends, one contract (SPEC.md 39):
;   VGA direct   - GC write mode 1: each movsb moves 8 pixels across all
;                  four planes through the latches; mode restored after
;                  (SPEC.md 1 rule 7).
;   mono         - per-row rep movsb through gfx_rowbase, immune to the
;                  banked layouts by construction.
; -----------------------------------------------------------------------------
gfx_scroll:
    GFXAUDIT_HOOK
    call cur_unlazy              ; off the clip list (SPEC.md 11.3): 7.1.4
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    GFXDENTERR                  ; a blit cannot take a sub-rect (SPEC.md 11.3),
                                ; so it goes onto the display holding its top
                                ; left - and the two edge tests below then
                                ; REFUSE a rect that reaches past that display,
                                ; which is 39.14.2's straddle rule for free
    test ax, 7                  ; byte-column granularity on every adapter
    jnz .no
    mov di, cx
    inc di
    test di, 7
    jnz .no
    cmp ax, cx
    ja .no
    cmp bx, dx
    ja .no
    cmp cx, [vid_cw]
    jae .no                     ; also catches a "negative" x1 via x1 <= x2
    cmp dx, [vid_ch]
    jae .no
    or si, si
    jz .no
    mov di, si                  ; DI = |dy|
    jns .pos
    neg di
.pos:
    push dx
    sub dx, bx
    inc dx                      ; DX = height
    cmp di, dx
    pop dx
    jae .no
    call wm_clip_test           ; whole-shape clip (SPEC.md 11.3): CF=1 =
    jc .no                      ; armed and not wholly inside one fragment
    mov [vgas_sdy], si
    push cx
    mov cl, 3
    shr ax, cl
    mov [vgas_col], ax           ; left byte column
    pop ax                      ; AX = x2
    inc ax
    mov cl, 3
    shr ax, cl
    sub ax, [vgas_col]
    mov [vgas_nb], ax            ; bytes per row
    mov ax, dx
    sub ax, bx
    inc ax
    sub ax, di
    mov [vgas_rows], ax          ; rows to copy = height - |dy|
    cmp word [vgas_sdy], 0
    jl .down
    mov [vgas_ydst], bx          ; up: dest rows y1..y2-|dy|, walked top-down
    mov word [vgas_step], 1      ; (source below destination - forward-safe)
    jmp short .disp
.down:
    mov [vgas_ydst], dx          ; down: dest rows y1+|dy|..y2, walked
    mov word [vgas_step], -1     ; bottom-up (source above destination)
.disp:
%ifdef GFX_VGA
    cmp byte [vid_planes], 1
    je .mono
    mov dx, 0x03CE              ; VRAM, via the latches
    mov ax, 0x0105              ; GC5: write mode 1 - a movsb read fills the
    out dx, ax                  ; latches, the write stores all four planes
    mov dx, VGA_SEG
    call vgas_lincopy
    mov dx, 0x03CE
    mov ax, 0x0005              ; write mode back to 0 (SPEC.md 1 rule 7)
    out dx, ax
    jmp short .ok
%endif
.mono:
    call vgas_bankcopy           ; the framebuffer IS [vid_rseg] (SPEC.md 39)
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    GFXDLEAVE                   ; ...and it banks CF, which is this routine's
    jmp kret_di
%ifdef GFX_VGA

; -----------------------------------------------------------------------------
; vgas_lincopy - scroll one LINEAR segment (the VGA stride): a buffer plane,
; or VRAM itself in write mode 1
; in:       DX = target segment; the vgas_* geometry
; clobbers: AX/BX/CX/DX/SI/DI, flags (gfx_scroll banked the caller's)
; -----------------------------------------------------------------------------
vgas_lincopy:
    push ds
    mov es, dx
%ifdef SCROLL_ROWBASE           ; the reference form: two muls a row (SPEC 5.5.1)
    mov ax, [vgas_rows]
    mov [vgas_left], ax
    mov bx, [vgas_ydst]
.r:
    mov ax, bx
    add ax, [vgas_sdy]
    mul word [vid_stride]       ; AX = src row * stride (DX:AX, DX = 0)
    add ax, [vgas_col]
    mov si, ax
    mov ax, bx
    mul word [vid_stride]
    add ax, [vgas_col]
    mov di, ax
    mov cx, [vgas_nb]
    push es
    pop ds                      ; DS = ES = the target for the movs
    cld
    rep movsb
    push cs
    pop ds
    add bx, [vgas_step]
    dec word [vgas_left]
    jnz .r
%else
    ; --- SPEC.md 5.5.1: the row address ADVANCES, so derive it once ----------
    ; A linear framebuffer has one bank, so rowbase(y) is y * stride and the
    ; two ends of the copy are a CONSTANT dy * stride apart - for any dy, with
    ; no alignment precondition anywhere. Both products are taken here instead
    ; of twice a row.
    mov ax, [vgas_rows]
    mov [vgas_left], ax
    mov ax, [vgas_ydst]
    mul word [vid_stride]
    add ax, [vgas_col]
    mov di, ax                  ; DI = the destination cursor
    mov ax, [vgas_sdy]          ; the delta, ONCE. `mul` is unsigned and sdy is
    mul word [vid_stride]       ; signed, and that is fine: only the low word
    mov dx, ax                  ; is used, and a product mod 65536 does not
                                ; care which sign the operand was read as -
                                ; the same reason DI itself may wrap
    mov bx, [vid_stride]        ; BX = the per-row step, in the walk's own
    cmp word [vgas_step], 0     ; direction (down: bottom-up, so backwards)
    jg .fwd
    neg bx
.fwd:
    sub bx, [vgas_nb]           ; ...LESS what the rep movsb will already have
                                ; added to DI. SPEC.md 5.7.1's rule, and the
                                ; A/B caught its absence: a string op advances
                                ; its pointer, and the reference form never had
                                ; to care because it recomputed DI every row.
                                ; Folded into the increment, so it is free
    cld                         ; nothing in the loop touches DF
.r:
    mov si, di
    add si, dx
    mov cx, [vgas_nb]
    push es
    pop ds                      ; DS = ES = the target for the movs
    rep movsb
    push cs
    pop ds
    add di, bx
    dec word [vgas_left]
    jnz .r
%endif
    pop ds
    ret
%endif

; -----------------------------------------------------------------------------
; vgas_bankcopy - the mono twin: per-row copies through gfx_rowbase, which
; absorbs the Hercules/CGA bank interleave (SPEC.md 39.2)
; in:       the vgas_* geometry; target = [vid_rseg]
; clobbers: AX/BX/CX/DX/SI/DI, flags
; -----------------------------------------------------------------------------
vgas_bankcopy:
    push ds
    mov es, [vid_rseg]
    mov ax, [vgas_rows]
    mov [vgas_left], ax
    mov bx, [vgas_ydst]
%ifndef SCROLL_ROWBASE
    ; --- SPEC.md 5.5.1: a whole number of BANKS makes the delta constant -----
    ; rowbase(y) is banktab[y & bmask] + (y >> bshift) * stride, so the two ends
    ; of the copy are a constant apart exactly when dy leaves the bank field
    ; alone - dy a multiple of 2 on CGA and of 4 on Hercules. Then the quotients
    ; differ by dy >> bshift and nothing else, and the destination can walk with
    ; gfx_nextrow instead of being recomputed. The test costs two instructions
    ; on the path that cannot use it.
    mov ax, [vgas_sdy]
    and ax, [vid_bmask]         ; a negative dy is fine: two's complement keeps
    jnz .gen                    ; the low bits' residue
    mov ax, bx
    call gfx_rowbase            ; the destination base, ONCE
    add ax, [vgas_col]
    mov di, ax
    mov ax, [vgas_sdy]          ; delta = (dy >> bshift) * stride, signed - SAR
    mov cl, [vid_bshift]        ; because dy may be negative and the shift has
    sar ax, cl                  ; to floor; exact, because dy is a multiple of
    mul word [vid_stride]       ; the bank count (mul's low word again)
    mov dx, ax
    cld
    cmp word [vgas_step], 0
    jl .rdn
.rup:                           ; up: destination walks DOWN the screen
    mov si, di
    add si, dx
    mov cx, [vgas_nb]
    push es
    pop ds
    rep movsb
    push cs
    pop ds
    sub di, [vgas_nb]           ; back to the row's own byte: rep movsb left DI
                                ; nb past it, and the advance below is
                                ; column-preserving, so it has to start from
                                ; the column it is preserving (SPEC.md 5.7.1)
    call gfx_nextrow            ; DI and flags only, and CS-relative reads, so
    dec word [vgas_left]        ; it is safe whatever DS was left at
    jnz .rup
    jmp short .out
.rdn:                           ; down: bottom-up, so the mirror
    mov si, di
    add si, dx
    mov cx, [vgas_nb]
    push es
    pop ds
    rep movsb
    push cs
    pop ds
    sub di, [vgas_nb]           ; back to the row's own byte: rep movsb left DI
                                ; nb past it, and the advance below is
                                ; column-preserving, so it has to start from
                                ; the column it is preserving (SPEC.md 5.7.1)
    call gfx_prevrow
    dec word [vgas_left]
    jnz .rdn
    jmp short .out
%endif
.gen:                           ; the general form: one rowbase per END per row
.r:
    mov ax, bx
    add ax, [vgas_sdy]
    call gfx_rowbase            ; AX = the src row's start (CS-relative
    add ax, [vgas_col]           ; parameter reads - DS-safe, SPEC.md 39.2)
    mov si, ax
    mov ax, bx
    call gfx_rowbase
    add ax, [vgas_col]
    mov di, ax
    mov cx, [vgas_nb]
    push es
    pop ds
    cld
    rep movsb
    push cs
    pop ds
    add bx, [vgas_step]
    dec word [vgas_left]
    jnz .r
.out:
    pop ds
    ret

section .bss

api_pat:     resb 8             ; osapi_gfx_fill_pat's staging slot: a
                                ; package's eight pattern bytes, copied into
                                ; the kernel's segment because gfx_pat_stage
                                ; reads them through DS

gfx_blit_x:  resw 1             ; gfx_blit4's row cursor (SPEC.md 5.4):
gfx_blit_w:  resw 1             ; destination left, width, rows left and the
gfx_blit_h:  resw 1             ; current source row's base. Lock-held
gfx_blit_sv: resw 1             ; scratch, like the rect block below.
gfx_blit_e:  resw 1             ; ...and the run scanner's state: one past the
gfx_blit_s0: resw 1             ; run's last pixel, where `repe scasb` began,
gfx_blit_c:  resb 1             ; the run's colour, and the byte a matching
gfx_blit_pb: resb 1             ; PAIR of that colour holds (c | c<<4)
gfx_blit_ro: resw 1             ; SPEC.md 5.4.1's direct paths: this row's base
gfx_blit_tb: resw 1             ; byte, its pair table, and which path is armed
gfx_blit_fst:resb 1             ; (0 = gfx_fill, 1 = the decoder, 2 = VGA spans)
%ifdef KERN_BIG
gfx_blit_hk: resb 1             ; did THIS call take the whole-shape display
                                ; hook? (SPEC.md 39.14.7.1) - .out must not
                                ; bring a nest down that it never put up, and
                                ; the two early guards can reach it
gfx_blit_sm: resw 1             ; SPEC.md 39.14.7.2's CUT: the seam, and the
gfx_blit_n1: resw 1             ; half left of it, banked across the first of
                                ; the two calls. Both are written before they
                                ; are read; [gfx_blit_fd] is NOT, and is in
                                ; .text with the rest of that kind
%endif
%ifdef GFX_PLANE
gfx_blit_rn: resw 1             ; SPEC.md 5.4.1.3's pricing: runs emitted in
gfx_blit_thr:resw 1             ; this row, and the count that ends the run
gfx_blit_pok:resb 1             ; path - which is also the first run's minimum
                                ; length. 0 = this call cannot go planar at all
gfx_bp_hk:   resb 1             ; ...and did THIS gfx_blitp take the whole-shape
                                ; display hook? (SPEC.md 5.4.3) - the exit must
                                ; not bring a nest down that it never put up,
                                ; and every refusal above reaches that exit
%endif

vga_plane       resb 1          ; save/restore plane counter / mask bit
vga_xorm        resb 1          ; vga_solid_rect: 0 = replace, 1 = XOR. The
                                ; ONE difference between gfx_fill_raw's VGA
                                ; body and gfx_xor_fill_raw's, and both heads
                                ; write it before the body reads it, so .bss's
                                ; guaranteed zero is not being relied on
%ifdef KERN_BIG
gfx_sp_seg      resw 1          ; SPEC.md 5.10 - gfx_spans' walk: the CALLER's
gfx_sp_si       resw 1          ; segment and cursor into the span list, the
gfx_sp_n        resw 1          ; rows left, and the row the next one names.
gfx_sp_row      resw 1          ; It is memory and not registers because
gfx_sp_y        resw 1          ; the row writers keep nothing across a call
gfx_sp_fb       resw 1          ; ...and the framebuffer ES is put back to
%endif
gfx_cl_x1       resw 1          ; gfx_clip_run: the primitive's own rect,
gfx_cl_y1       resw 1          ; held while the raw body runs per fragment
gfx_cl_x2       resw 1
gfx_cl_y2       resw 1
gfx_cl_fn       resw 1          ; ...and the raw body's near pointer

gfx_ln_x1       resw 1          ; gfx_line (SPEC.md 5.6): the normalised
gfx_ln_y1       resw 1          ; endpoints - y1 <= y2 always, so the walk
gfx_ln_x2       resw 1          ; runs downward and the pixel set is a
gfx_ln_y2       resw 1          ; property of the PAIR, not of its order
gfx_ln_dx       resw 1          ; |x2-x1|, and y2-y1 which cannot be negative
gfx_ln_dy       resw 1
gfx_ln_sx       resw 1          ; +1 / -1
gfx_ln_err      resw 1          ; the Bresenham error term at the start
gfx_ln_e2       resw 1          ; ...and 2*err inside the loop
gfx_ln_cx1      resw 1          ; the box this pass draws inside - a clip
gfx_ln_cy1      resw 1          ; rect, or the screen when the region is
gfx_ln_cx2      resw 1          ; disarmed (5.6.3)
gfx_ln_cy2      resw 1
gfx_ln_rs       resw 1          ; runs path: the open run's near end, on the
                                ; major axis
gfx_ln_np       resw 1          ; passes still owed: 1 thin, 3 fat (5.6.5)
gfx_ln_fdx      resw 1          ; ...and the step between them, which is one
gfx_ln_fdy      resw 1          ; pixel across the MINOR axis
%ifdef KERN_BIG
gfx_ln_d        resw 1          ; SPEC.md 5.6.4.1's register walk, staged
gfx_ln_sub2     resw 1          ; across gfx_ls_addr: 2*major and 2*minor, the
gfx_ln_add2     resw 1          ; step interval's start, and how many pixels of
gfx_ln_first    resw 1          ; it are inside this pass's box
gfx_ln_cnt      resw 1
gfx_ln_flg      resb 1          ; ...and LF_STEEP / LF_WHITE / LF_XPOS, which
                                ; pick which of 5.6.4.2's eight walks runs
%endif
gfx_ln_ink      resb 1          ; mono path: gfx_ink's 00 / FF / 01
gfx_ln_wide     resb 1          ; 1 = the 1bpp three-column walk (5.6.6)
gfx_ln_majx     resb 1          ; runs path: 1 = the major axis is x
gfx_ls_x        resw 1          ; 5.6.7's staged walk state - the caller's
gfx_ls_y        resw 1          ; block, copied in and back out (20.8 rule 3,
gfx_ls_err      resw 1          ; and forced here as well as preferred: the
gfx_ls_dx       resw 1          ; mono walk needs ES for the framebuffer)
gfx_ls_dy       resw 1
gfx_ls_sx       resw 1
gfx_ls_sy       resw 1
gfx_ls_e2       resw 1
gfx_ls_bx1      resw 1          ; the clip rect the walk is inside, re-resolved
gfx_ls_by1      resw 1          ; only when a pixel steps out of it - a step is
gfx_ls_bx2      resw 1          ; a handful of pixels, so this is one list walk
gfx_ls_by2      resw 1          ; per CALL in every case that matters
gfx_ls_vx       resw 1          ; ...and the point in VIRTUAL space, which is
gfx_ls_vy       resw 1          ; what wm_clip_tab is in (SPEC.md 39.14.9)
gfx_ls_cseg     resw 1          ; ...and where the caller's block came from,
gfx_ls_coff     resw 1          ; because ES cannot hold it across the walk

vgas_col         resw 1          ; gfx_scroll (SPEC.md 5.5): left byte column
vgas_nb          resw 1          ;   bytes per row
; --- gfx_blit1's PEN, resolved once a band (SPEC.md 5.4.2.2) ----------------
; KERNEL_SEG's .bss, not COLD_SEG's: gfx_blit1_x reads all four while DS is
; still ours, above the point the emit spends it on the caller's band.
gfx_b1act        resb 1          ; 1 = the pen is not the default, so the two
                                 ;     GC writes below are owed - and owed
                                 ;     back, which is why this is a flag and
                                 ;     not just a pair of values
gfx_b1inv        resb 1          ; 1 = the varying planes want ~band. Rides to
                                 ;     the emit in DL; see there
gfx_b1g0         resw 1          ; the GC0 write: Set/Reset = the constant
gfx_b1g1         resw 1          ; planes' colour, and GC1 = which they are
%if gfx_b1inv - gfx_b1act != 1
  %error "gfx_b1act/gfx_b1inv must stay adjacent: gfx_blit1_x clears both in one word store"
%endif
vgas_sdy         resw 1          ;   signed dy (positive = content up)
vgas_ydst        resw 1          ;   first destination row
vgas_step        resw 1          ;   +1 (up: top-down) / -1 (down: bottom-up)
vgas_rows        resw 1          ;   rows to copy = height - |dy|
vgas_left        resw 1          ;   per-pass row countdown

; -----------------------------------------------------------------------------
section .cold                   ; gfx_blit1's body, and its far entry below it
%ifdef KERN_BIG
; =============================================================================
; gfx_blit1_x - a composed 1bpp band, straight into the framebuffer (SPEC.md
;               5.4.2). kern_big only; kern_small's thunk is stc/retf.
;
; in:       ES:SI = the band, BP = its stride in bytes per row, AX = x (a
;           multiple of 8, SIGNED), BX = y (SIGNED), CX = width in pixels (a
;           multiple of 8), DX = rows (1..255); the gfx lock held
; out:      CF = 0 drawn or clipped exactly, CF = 1 refused and nothing drawn
; clobbers: NOTHING - every register, ES and BP included
;
; The whole routine is the arriving. The caller has already decided what every
; bit is, so there is no colour to reduce, no plane to select and no glyph to
; look up: what is left is a rep movsb a row. SPEC.md 5.4.2 is the contract and
; the reasoning; this is the order of operations it pins, in the order it pins
; them - refuse, resolve the display, clip, resolve the PEN, emit. The pen is
; LAST because it is a question about the display and not about the arguments
; (SPEC.md 5.4.2.4), and at the door no display has been resolved yet.
;
; The band step, the row step and the row count are pushed and read BP-relative
; because BP addresses SS (SPEC.md 1) and DS spends the emit pointed at the
; CALLER's band. This body's CS is COLD_SEG, so sw_xfer's [cs:vid_rowadd] idiom
; does not transfer either - the frame is the way to reach a geometry word once
; DS has moved.
;
; THAT RULE COVERS THE EXIT TOO, and it did not: the nest count came down at
; `.done`, which is ABOVE `pop ds`, so `cmp byte [vid_ndisp]` and `dec byte
; [gfx_dnest]` both addressed the CALLER's segment. The read decided the write
; - a package whose image happens to hold anything above 1 at vid_ndisp's
; offset fell through the guard - and the write then decremented one byte of
; that package's own IMAGE, once per blit. Nothing in the kernel or the package
; is checking, so it is silent until the rotted byte is CODE the package
; executes: Word (SPEC.md 68.13) hit it at 0x1698, the head of wd_rflush's
; leading-gap fill, which only a face taller than the 8x8 cell ever runs.
; SPEC.md 5.4.2.1 is the account.
; =============================================================================
gfx_blit1_x:
    test ax, 7                  ; the two argument refusals, and they are the
    jnz .refuse                 ; ONLY two: an x or a width off the byte grid
    test cx, 7                  ; is a caller error, not a shape to clip
    jnz .refuse
    or cx, cx
    jz .refuse
    or dx, dx
    jz .refuse
    cmp dx, 255                 ; ...and a band taller than wm_clip_rows' own
    ja .refuse                  ; byte answer can express (SPEC.md 11.3.2)

    mov word [gfx_b1act], 0     ; SPEC.md 5.4.2.2's two flags, and BOTH in one
                                ; store - they are adjacent by assertion below,
                                ; and this is the path EVERY band takes
                                ; (SPEC.md 6.1.10's own trick on wm_clip_r0: 5
                                ; bytes against 10, and an 8088 pays 4.34
                                ; clocks per instruction BYTE). What they will
                                ; SAY is .pen's answer, a long way below: the
                                ; pen is resolved on the display this band is
                                ; going to and not at the door (SPEC.md
                                ; 5.4.2.4). Cleared here rather than there so
                                ; that a band clipped away entirely - which
                                ; never reaches .pen - cannot leave .done
                                ; tearing down ports nobody armed

    cmp byte [cur_lazy], 0          ; this may write any pixel on the screen,
    je .lazyok                      ; so the deferred hide is owed (SPEC.md
    call KERNEL_SEG:cw_cur_unlazy   ; 7.1.4) - and owed HERE, above the two
.lazyok:                            ; questions below, so nothing that answers
                                    ; them can be read stale afterwards.
                                    ; CURUNLAZY's test, written out: this body
                                    ; is .cold and the macro's `call` is near

    ; --- resolve the display BEFORE anything meets a screen extent (39.14.8)
    cmp byte [vid_ndisp], 1
    jbe .spanok
    cmp byte [gfx_dnest], 0
    jne .spanok                 ; an outer whole-shape hook already chose one
    push cx
    push dx
    add cx, ax
    dec cx                      ; CX = x2, DX = y2, both still VIRTUAL
    add dx, bx
    dec dx
    call KERNEL_SEG:cw_vid_span_one
    pop dx
    pop cx
    jc .percol                  ; it crosses a seam: per band column, each
.spanok:                        ; resolving its own display (SPEC.md 39.14.6)

    ; --- ...and the clip region, in the same VIRTUAL space wm_clip_tab is in
    cmp word [wm_clip_n], 0
    je .clipok
    push cx
    push dx
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call KERNEL_SEG:cw_wm_clip_rows
    pop dx
    pop cx
    jc .percol                  ; a VERTICAL cut: wm_clip_rows refuses a
.clipok:                        ; part-width row, so the columns ask singly

    push es                     ; from here the caller's registers are banked
    push ds                     ; and the body is free with all nine
    push bp
    push si
    push di
    push dx
    push cx
    push bx
    push ax

    cmp word [wm_clip_n], 0
    je .rowsall
    push ax                     ; x, which this block spends AX to compute
    mov al, [wm_clip_r0]        ; skip the rows the region cut off the top:
    xor ah, ah                  ; a row OFFSET is the same number in either
    or ax, ax                   ; coordinate space, which is why this may be
    jz .r0z                     ; applied before the translation below
    add bx, ax
    push dx
    mul bp                      ; ...and the band skips r0 whole strides
    add si, ax
    pop dx
.r0z:
    mov al, [wm_clip_rn]
    xor ah, ah
    mov dx, ax                  ; DX = the rows one fragment can offer
    pop ax
.rowsall:

    cmp byte [vid_ndisp], 1
    jbe .noenter
    call KERNEL_SEG:cw_gfx_disp_enter   ; AX/BX become this display's own
.noenter:

    ; --- clip to THAT display's extent. A negative x or y is the ordinary
    ; case here and not an error: a window dragged half off the left edge has
    ; its content at one, and whole 8-pixel columns simply drop.
    or ax, ax
    jns .xleft
    add cx, ax                  ; CX loses the columns left of the screen
    jle .none
    push ax
    neg ax
    shr ax, 1                   ; three single shifts: 6 clocks against the
    shr ax, 1                   ; 20 an 8088 charges for a shift by CL
    shr ax, 1
    add si, ax                  ; ...and the band starts that many bytes in
    pop ax
    xor ax, ax
.xleft:
    cmp ax, [vid_cw]
    jae .none
    mov di, ax
    add di, cx
    cmp di, [vid_cw]            ; vid_cw is 640 or 720, both multiples of 8,
    jbe .xright                 ; so the truncation stays on the byte grid
    mov cx, [vid_cw]
    sub cx, ax
.xright:
    or bx, bx
    jns .ytop
    add dx, bx
    jle .none
    push ax
    mov ax, bx
    neg ax
    push dx
    mul bp                      ; the band skips |y| whole strides
    add si, ax
    pop dx
    pop ax
    xor bx, bx
.ytop:
    cmp bx, [vid_ch]
    jae .none
    mov di, bx
    add di, dx
    cmp di, [vid_ch]
    jbe .ybot
    mov dx, [vid_ch]
    sub dx, bx
.ybot:

    call .pen                   ; SPEC.md 5.4.2.4: THE PEN, now that there IS a
    jc .penref                  ; display under this band - [vid_mono],
                                ; [vid_seg] and [vid_cw] all mean something
                                ; here and none of them did at the door

    ; --- the frame the row loop reads, and then DS leaves KERNEL_SEG --------
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1                   ; DI = bytes per row
    push dx                     ; [bp+10] rows remaining
    push di                     ; [bp+8]  bytes per row
    mov cx, bp
    sub cx, di
    push cx                     ; [bp+6]  the band's step, less what rep moved
    push word [vid_rowadd]      ; [bp+4]  gfx_nextrow, inlined (Part 9 Set 3)
    push word [vid_wrapbit]     ; [bp+2]
    push word [vid_wrapfix]     ; [bp+0]
    mov bp, sp

    mov cx, ax                  ; AX is still x, and x is a multiple of 8
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov ax, bx
    push cx
    call KERNEL_SEG:cw_gfx_rowbase  ; AX = rowbase(y); it clobbers CX and DX
    pop cx
    add ax, cx
    mov di, ax                  ; DI = the band's first framebuffer byte
    cmp byte [gfx_b1act], 0     ; SPEC.md 5.4.2.2's pen: the ports are the
    je .penres                  ; BAND's, issued once, and DS is still ours
    mov dx, VGA_GC              ; here - the emit below spends it on the
    mov ax, [gfx_b1g0]          ; caller's band
    out dx, ax                  ; GC0 Set/Reset = the constant planes' colour
    mov ax, [gfx_b1g1]
    out dx, ax                  ; GC1 Enable Set/Reset = C
.penres:
    mov ax, [vid_seg]           ; the UNIFORM cell, and NOT vid_rseg, which is
    mov bx, es                  ; 0 on VGA (viddet.inc:115) - and already the
    mov dl, [gfx_b1inv]         ; ...and WHICH LOOP, read while DS is still
                                ; ours. `cs:` does not reach it - this body's
                                ; CS is COLD_SEG and the flag is KERNEL_SEG's
                                ; - and the frame is full, so it rides in DL,
                                ; which the frame banked at [bp+10] and
                                ; neither emit loop touches
    mov es, ax                  ; resolved display's, the enter above having
    mov ds, bx                  ; run first (SPEC.md 39.12)
    cld
    or dl, dl
    jnz .rowi
.row:
    mov cx, [bp+8]
    push di
    shr cx, 1                   ; WORDS: `rep movsw` is 12.5 clocks a byte on
    rep movsw                   ; an 8088 against `rep movsb`'s 17, and the
    jnc .even                   ; carry out of the shift is the odd byte. A
    movsb                       ; string op writes no flag and neither does
.even:                          ; `rep`, so it survives the move (SPEC.md
    pop di                      ; 5.4.2). No segment override on a rep: an
    add si, [bp+6]              ; 8086 loses the prefix to an interrupt (5.7)
    add di, [bp+4]
    test di, [bp+2]
    jz .nowrap
    add di, [bp+0]
.nowrap:
    dec word [bp+10]
    jnz .row
    jmp short .emitted

    ; --- ...and the same rows COMPLEMENTED (SPEC.md 5.4.2.2) ----------------
    ; The pen's varying planes want ~band rather than band - black ink on white
    ; paper is the case, and it is the commonest one text has. `rep movsw`
    ; cannot invert, so this is the hand loop: 34 clocks a word against the
    ; rep's 25. It is a SECOND loop and not a mask folded into the first,
    ; because folding it would charge every existing caller 36% for a case
    ; none of them uses - Word, cword and os88type all emit with the default
    ; pen (SPEC.md 6.3).
.rowi:
    mov cx, [bp+8]
    push di
    shr cx, 1                   ; CF = an ODD bytes-per-row, and it survives
    jcxz .iodd                  ; everything below: jcxz, lodsw, stosw, loop
.iword:                         ; and NOT all leave the flags alone
    lodsw
    not ax
    stosw
    loop .iword
.iodd:
    ; The trailing byte of an odd row, done LAST. It used to be done first,
    ; with SI and DI saved around it and RESTORED - and restoring SI is a bug
    ; (SPEC.md 5.4.2.3): the word loop then advanced SI by only bytes-1, so
    ; `add si, [bp+6]` left it one byte short and EVERY LATER ROW read the
    ; band one byte early. It is latent unless a caller emits an odd
    ; bytes-per-row band through the COMPLEMENTED pen, which is black ink on
    ; white paper - and until band.inc's chunked title bar (SPEC.md 11.96)
    ; every band in the tree was a whole number of words wide.
    jnc .idone
    lodsb
    not al
    stosb
.idone:
    pop di
    add si, [bp+6]
    add di, [bp+4]
    test di, [bp+2]
    jz .inowrap
    add di, [bp+0]
.inowrap:
    dec word [bp+10]
    jnz .rowi
.emitted:
    mov sp, bp
    add sp, 12
.done:
    pop ax
    pop bx
    pop cx
    pop dx
    pop di
    pop si
    pop bp
    pop ds                      ; DS IS KERNEL_SEG AGAIN, AND NOT ONE BYTE
    pop es                      ; SOONER: the emit above spends it on the
                                ; CALLER's band, so the two bytes below have to
                                ; wait for this pop. They did not, and both of
                                ; them landed in the CALLER'S IMAGE - see below.
    cmp byte [gfx_b1act], 0     ; SPEC.md 5.4.2.2: vga12.inc's standing contract
    je .nopen                   ; is that every routine leaves Enable Set/Reset
    push ax                     ; = 0. GC0 is not part of that resting state,
    push dx                     ; and the Bit Mask and Map Mask were never
    mov dx, VGA_GC              ; touched, so the teardown is one `out`
    mov ax, 0x0001
    out dx, ax
    pop dx
    pop ax
    mov byte [gfx_b1act], 0
.nopen:
    cmp byte [vid_ndisp], 1
    jbe .noleave
    dec byte [gfx_dnest]        ; ...and NOTHING restores the display
.noleave:                       ; (SPEC.md 39.14.3)
    clc
    ret
.none:                          ; clipped away entirely: drawn, and nothing
    jmp .done                   ; drawn, which is CF = 0 either way

    ; --- per 8-pixel band column, untranslated (SPEC.md 5.4.2 step 4) -------
    ; Each column re-enters this routine and resolves its own display and its
    ; own row range, which is font_run's per-cell answer one granularity
    ; coarser. A column that is refused a second time is a column that cannot
    ; be drawn - it is skipped, and the caller never letters the row twice.
.percol:
    cmp cx, 8
    jbe .okquiet                ; already one column: this is that skip
    push ax
    push cx
    push si
    push di
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1                   ; DI = columns to place
    mov cx, 8
.pcol:
    push ax
    push cx
    push si
    push di
    call gfx_blit1_x
    pop di
    pop si
    pop cx
    pop ax
    add ax, 8
    inc si
    dec di
    jnz .pcol
    pop di
    pop si
    pop cx
    pop ax
.okquiet:
    clc
    ret

    ; --- SPEC.md 5.4.2.2: THE PEN, asked ON THE RESOLVED DISPLAY -----------
    ; A set band bit takes the ink and a clear one the paper. Which planes VARY
    ; across the band and which are CONSTANT is a question the BAND answers
    ; once, not the byte - SPEC.md 6.1.10's grouping, one level up from a glyph
    ; row:
    ;     A = ink & ~paper   the planes that want the band byte as it stands
    ;     B = ~ink & paper   ...and the planes that want its complement
    ;     C = ~(ink ^ paper) the planes both agree on - Set/Reset's job
    ; With GC1 = C, GC0 = ink and the Bit Mask left at FF, one CPU byte gives C
    ; its constant and hands A|B the data, and the Bit Mask being FF is what
    ; removes the latch read. A or B empty is one pass; BOTH non-empty needs a
    ; Map Mask split and is the fourth refusal.
    ;
    ; IT USED TO SIT AT THE TOP, beside the argument refusals, and SPEC.md
    ; 5.4.2.4 is what that cost: `[vid_mono]` is a fact about a DISPLAY on a
    ; two-card desktop and not about the machine (SPEC.md 39.14.6), so up there
    ; it was answered off whichever display was left current by whatever drew
    ; last. A straddling band re-enters this routine PER 8-PIXEL COLUMN, each
    ; column asking again - so the first column on the far monitor inherited
    ; the near one's answer and was right by accident, and every column after
    ; it was inverted. That is a title bar drawn half one way up and half the
    ; other, splitting one byte INTO the second screen rather than at the seam.
    ;
    ; in:  the display resolved and current; DS = KERNEL_SEG
    ; out: CF = 0 resolved into [gfx_b1act]/[gfx_b1inv]/[gfx_b1g0]/[gfx_b1g1],
    ;      CF = 1 the fourth refusal. AX and BX preserved.
.pen:
    push ax
    push bx
    cmp byte [vid_mono], 0
    je .pvga

    ; --- 1bpp: a set bit is a LIT pixel and a pen has no colour to add
    ; (SPEC.md 5.4.2.2) - but WHICH WAY UP the band is stored is the composer's
    ; answer, and on an extended desktop it cannot be the machine's (SPEC.md
    ; 5.9.3.1). band.inc declares it in [gfx_b1adp]: 1 = a set bit is the ink,
    ; 2 = a set bit is the paper, 0 = the band is in SCREEN polarity already,
    ; which is every package and a compare and a fallthrough for them.
    ;
    ; The two lines below are band_pen_x's OWN decision, moved to where the
    ; display is known, and they reproduce it exactly: it stores paper-as-set
    ; when - and only when - the ink is not the lit colour, so a band that
    ; landed on the depth it was composed for wants no turn at all. That is
    ; what makes this cost a one-card machine nothing: `d XOR pol` is 0 on
    ; every band such a machine composes, mono and VGA alike.
    cmp byte [gfx_b1adp], 0
    je .pdone
    mov bl, [gfx_b1ink]         ; gfx_inktab (SPEC.md 39.4): FFh = this colour
    and bl, 15                  ; lights the pixel; 00h and the 01h dither class
    xor bh, bh                  ; are both UNLIT to a band, which cannot dither
    mov bl, [bx + gfx_inktab]
    cmp bl, 0FFh
    mov bl, 0                   ; BL = "the ink is DARK" - and a `mov` writes no
    je .ppol                    ; flag, so the compare above still stands here
    mov bl, 1
.ppol:
    cmp byte [gfx_b1adp], 2     ; ...against the way up the band is STORED: a
    jne .pturn                  ; band holding paper-as-set is already the
    xor bl, 1                   ; answer to a dark ink
.pturn:
    or bl, bl
    jz .pdone                   ; the set bits are the lit ones: it stands
    mov byte [gfx_b1inv], 1     ; ...and when they are not, the emit turns it
    jmp short .pdone            ; over - the complemented loop and nothing else

.pvga:
    mov al, [gfx_b1ink]
    mov ah, [gfx_b1pap]
    cmp byte [gfx_b1adp], 2     ; the band holds PAPER-as-set (SPEC.md 5.9.3.1),
    jne .pv0                    ; and the grouping below is phrased in terms of
    xchg al, ah                 ; what a SET bit means - so for that band the
.pv0:                           ; pair that describes it is the other way round
    and ax, 0x0F0F
    cmp ax, (CBLACK << 8) | CWHITE
    je .pdone                   ; the default IS the resting state: no ports
    mov bl, al
    xor bl, ah                  ; BL = the planes the two differ on
    mov bh, al
    and bh, bl                  ; BH = A
    and ah, bl                  ; AH = B
    or bh, bh
    jz .peninv                  ; A empty: every varying plane wants ~band
    or ah, ah
    jnz .penno                  ; both: a Map Mask split, which this does not do
    jmp short .penset
.peninv:
    mov byte [gfx_b1inv], 1
.penset:
    mov byte [gfx_b1act], 1
    not bl
    and bl, 0x0F                ; BL = C, the planes Set/Reset answers for
    mov ah, al                  ; ...with the INK's bits: on those planes the
    mov al, 0                   ; two colours agree, so either would do
    mov [gfx_b1g0], ax          ; GC0 = Set/Reset value
    mov ah, bl
    mov al, 1
    mov [gfx_b1g1], ax          ; GC1 = Enable Set/Reset = C
.pdone:
    pop bx
    pop ax
    clc                         ; the pops write no flag, so both answers say
    ret                         ; so themselves
.penno:                         ; SPEC.md 5.4.2.2's fourth refusal: a pen
    pop bx                      ; whose two colours share no plane in EITHER
    pop ax                      ; direction. CDGRAY on CLGRAY - 8 against 7 -
    stc                         ; is the only such pair this system sets, and
    ret                         ; it belongs to text rather than to a band

    ; --- ...and that refusal now arrives from BELOW the banking pushes, which
    ; is what asking the pen on the display costs: the nine registers and the
    ; nest count the enter took both have to come back before the `stc`.
    ; Nothing was drawn and no port was armed - [gfx_b1act] is only ever set on
    ; the path that does not refuse - so there is nothing else to undo.
.penref:
    pop ax
    pop bx
    pop cx
    pop dx
    pop di
    pop si
    pop bp
    pop ds                      ; KERNEL_SEG again, and the two reads below
    pop es                      ; wait for it (SPEC.md 5.4.2.1)
    cmp byte [vid_ndisp], 1
    jbe .refuse
    dec byte [gfx_dnest]        ; ...and NOTHING restores the display (39.14.3)
.refuse:
    stc
    ret

; -----------------------------------------------------------------------------
; The far entry (SPEC.md 2.6): kernel.asm's resident thunk far-calls this, and
; this near-calls the body above.
; -----------------------------------------------------------------------------
gbz_gfx_blit1:        call gfx_blit1_x
                  retf
%endif

section .lowbss                 ; LOW_SEG (SPEC.md 2.1), and not .bss for
                                ; vid_rowtab's reason: 848 bytes in the image
                                ; rung would cross a step AND spend
                                ; KERN_CODE_MAX, which is the guard nobody can
                                ; raise. Here it costs neither - the low rung
                                ; had 1,258 bytes standing - and it costs
                                ; nothing to REACH either, because the decoder
                                ; runs with DS = LOW_SEG and reads both with no
                                ; override at all (SPEC.md 5.4.1.3)
gfx_pairtab0 resb 256           ; SPEC.md 5.4.1.1's pair tables, built once from
gfx_pairtab1 resb 256           ; gfx_inktab by sw_pairbuild - and here rather
                                ; than in .bss for 39.3.1's reason, which came
                                ; due when 5.4.1.3 below spent the segment: 512
                                ; bytes is the largest pair of objects the
                                ; kernel's own 64KB was carrying for this
                                ; primitive, they are read `ss xlat` where they
                                ; were read `cs xlat` - the same one prefix
                                ; byte, sw_blit_row having moved DS already -
                                ; and the move is KERN_BUDGET-NEUTRAL: the
                                ; image rung gives back exactly the step the
                                ; low rung takes
%ifdef GFX_PLANE
section .vgabuf                 ; ...AND THESE TWO ARE NOT .lowbss's any more
                                ; (SPEC.md 39.22): they are the only bytes in
                                ; the kernel a machine with no VGA has no use
                                ; for, and `.vgabuf` is a rung of its own at
                                ; the TOP of the kernel so that mem_init can
                                ; put the heap floor under them. Reached
                                ; through VGABUF_SEG, which the decoder banks
                                ; into DS exactly where it used to bank
                                ; LOW_SEG - one word, because the table was
                                ; already read through a segment register.
vga_p4tab   resw 256            ; source byte -> a nibble of plane bits each
vga_pbuf    resb VGA_PB*4       ; one plane row apiece, filled then blitted
section .lowbss                 ; ...and the SCRATCH stays here, because
                                ; vga_prow_emit reads it through SS while DS
                                ; is the caller's planes
vga_pr_lm   resb 1              ; the row's edge masks, byte span, first
vga_pr_rm   resb 1              ; framebuffer byte and odd-x flag - banked
vga_pr_n    resw 1              ; HERE and not in .bss because DS is NEVER
vga_pr_o    resw 1              ; KERNEL_SEG from the decode onwards - it is
vga_pr_sh   resw 1              ; VGABUF_SEG in the decoder and the caller's
                                ; planes in gfx_blitp - so a [gfx_blit_*] word
                                ; is unreachable from either and SS reaches
                                ; these from both. Three of them are read
                                ; `ss:` INSIDE the decoder's bank, which is
                                ; the hazard SPEC.md 39.22 introduced
vga_pr_rs   resw 1              ; gfx_blitp's row stride and the caller's
vga_pr_sg   resw 1              ; segment, banked because DS is the kernel's
                                ; for gfx_rowbase and the planes' for the copy
vga_pr_ps   resw 1              ; ...and the step from one plane's row bytes to
                                ; the next, which is the decoder's buffer
                                ; spacing on one caller and a canvas's on the
                                ; other (SPEC.md 5.4.3)
%endif

section .text
