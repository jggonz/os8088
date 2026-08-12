; =============================================================================
; os8088 - vidsel.inc
;
; Which adapters this machine HAS, and switching between them while it runs
; (SPEC.md 39.11). viddet.inc answers "which one am I driving"; this answers
; "which ones could I drive", which is a different question and has more than
; one answer on the machine this project is calibrated against - an IBM 5150
; with a Hercules AND a CGA card in it, each on its own monitor
; (docs/FIELD-MACHINES.md).
;
; SEPARATE FROM viddet.inc ON PURPOSE, and the reason is mechanical rather
; than editorial: viddet.inc is included BEFORE splash.inc, so every byte
; added to it pushes splash.inc further down the image, and the whole splash
; module has to land inside the first SPL_RESIDENT sectors (SPEC.md 15). That
; assertion had 234 bytes of room when this was written. Nothing here is
; reachable from the splash - the availability probe runs from kmain and the
; switch from the Control Panel - so it goes after it and costs that budget
; nothing.
;
; The probe WRITES to the framebuffer it is testing, so it must never be
; pointed at the adapter currently on screen. It is not: the running adapter
; is marked available by definition and skipped (see vid_probe_avail).
; =============================================================================

; [vid_avail] is a bitmap, one bit per VID_* kind, so bit n = 1 << VID_*.
VID_A_VGA   equ 1 << VID_VGA
VID_A_HERC  equ 1 << VID_HERC
VID_A_CGA   equ 1 << VID_CGA

; SPEC.md 39.19.2's placements. TWO and not four, because the primary is
; always at the virtual origin, so "which monitor is on the left" is answered
; by which CARD is primary - the choice directly above this one on the same
; Control Panel page. All four physical arrangements are reachable; what is
; given up is the menu bar being on the right-hand monitor, and buying that
; back costs the primary's origin added at every chrome site in the machine.
VID_L_RIGHT equ 0
VID_L_BELOW equ 1

; .text and not .bss, like everything else that has to be right before the
; kernel has zeroed anything: -f bin zeroes nothing at all.
vid_avail   db 0                ; which adapters answered (VID_A_*). Probed
                                ; ONCE, at boot, and cached - vid_switch moves
                                ; [vid_kind], so re-deriving it later would
                                ; ask the question of the wrong machine

; -----------------------------------------------------------------------------
; The 'VD' block of SPEC.md 57.4 - nine words, no .bss, nothing copied.
;
; UNCONDITIONAL, for the mouse and clock blocks' reason: a dual-display
; arrangement is a question about two real cards on two real monitors, so it
; has to be answerable in the build the field machine is actually sent - and
; it is the one part of SPEC.md 39 no emulator can settle, because what an
; emulator cannot show is a monitor that is not plugged in.
;
; Rule 6 throughout - every word points at state that already exists. The
; second one covers SIX bytes with one pointer, which is why vid_dmode and
; vid_dlay were put immediately after vid_oy rather than beside vid_avail.
; -----------------------------------------------------------------------------
vid_dbg_blk:
    dw DBG_TAG_VIDEO            ; +0  'VD', and the block's own first word
    dw vid_kind                 ; +2  db kind, db mono, db planes (viddet.inc)
    dw vid_avail                ; +4  db: which adapters answered
%ifdef KERN_BIG
    dw vid_ndisp                ; +6  db ndisp, db cur, dw ox, dw oy,
                                ;     db dmode, db dlay
    dw cur_disp                 ; +8  db cur_disp, db cur_dprev
    dw vid_ctx                  ; +10 the records, VID_CTX_SZ apart
    dw VID_CTX_SZ               ; +12
%else
    dw 0                        ; +6  a kern_small kernel is single-display by
    dw 0                        ; +8  CONSTRUCTION, not by setting - so the
    dw 0                        ; +10 reader has nothing to report rather than
    dw 0                        ; +12 a one taken from a byte that is not there
%endif
    dw vid_w                    ; +14 dw w, dw h - the DESKTOP, so the union
    dw vid_pw                   ; +16 dw pw, dw ph - the CHROME's extent, the
                                ;     primary's. The PAIR is the assertion
                                ;     (SPEC.md 39.16): equal with two displays
                                ;     means vid_desk_union never ran, unequal
                                ;     with one means nothing put it back

; -----------------------------------------------------------------------------
; vid_memchk - is there display RAM at this segment? (SPEC.md 39.11.1)
;
; in:       AX = segment to test
; out:      CF = 0 RAM answers there, CF = 1 nothing does
; clobbers: flags
;
; WRITE TWO DIFFERENT VALUES TO TWO DIFFERENT OFFSETS, THEN READ THE FIRST
; BACK. The obvious test - write a byte, read it back - passes on an empty
; bus, because ISA is capacitive and a read with nothing driving it returns
; whatever crossed the bus last, which is the byte just written. Writing 0xAA
; to a second address in between is what makes the read of 0x55 mean
; something: a floating bus answers 0xAA (or 0xFF), and only real memory
; answers 0x55.
;
; THE SECOND OFFSET IS 0x1000, AND THAT IS ALSO THE HERCULES-VERSUS-MDA TEST
; SPEC.md 39.1 RECORDED AS A SCOPE CUT. A plain MDA has 4KB at B000 and
; aliases it upwards, so its 0x1000 IS its 0x0000 - the 0xAA lands on top of
; the 0x55 and the test fails, which is the right answer: MDA is text-only
; and has no 720x348 mode to offer. A Hercules has 32KB in page 0, a CGA
; 16KB at B800, and both keep the two offsets apart. So the one probe that
; rejects an empty bus rejects a text-only card for free, and nothing here
; needs the 3BAh sync-toggle timing loop.
;
; The bytes are put back either way. On a machine where this lands on a live
; MDA text page that is two characters flickering once, during boot.
; -----------------------------------------------------------------------------
vid_memchk:
    push ax
    push bx
    push dx
    push es
    mov es, ax
    mov dh, [es:0x0000]         ; bank both bytes before trampling them
    mov dl, [es:0x1000]
    mov bl, 0x55
    mov [es:0x0000], bl
    mov byte [es:0x1000], 0xAA  ; a DIFFERENT value at a DIFFERENT address
    cmp [es:0x0000], bl         ; ...so this read cannot be the bus echoing
    mov [es:0x0000], dh         ; restore before branching on the answer:
    mov [es:0x1000], dl         ; `mov` does not touch flags, and there is
    je .yes                     ; then one path out rather than two
    stc
    jmp short .out
.yes:
    clc
.out:
    pop es
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_probe_avail - fill [vid_avail] (SPEC.md 39.11.1)
;
; in:       [vid_kind], and the machine already in that adapter's mode
; out:      [vid_avail]
; clobbers: flags
;
; CALLED FROM kmain, AFTER THE MODE IS SET, and both halves of that matter.
; A VGA in mode 12h decodes A0000 only - the Graphics Controller's memory map
; is 64KB at A000 - so B0000 and B8000 are free for a second card to answer
; at, and a positive there is a real one. Probed before the mode set, on a
; machine whose BIOS came up in mono text, the VGA answers at B0000 as
; ITSELF and reports a Hercules that is not there.
;
; The running adapter is marked available without being probed, because the
; probe writes and that is the screen.
;
; VGA IS NOT PROBED AT ALL, because vid_detect already answered it: steps 1
; and 2 of SPEC.md 39.1 run before the equipment word, so [vid_kind] is
; VID_VGA on every machine with a VGA or EGA BIOS. And such a machine can
; always do CGA as well - mode 6 is a standard BIOS mode on every VGA and
; EGA ever made, and it is the same byte-exact 640x200 framebuffer this
; kernel's CGA path already drives (SPEC.md 39.9's VIDEO=cga has relied on
; that for as long as it has existed).
; -----------------------------------------------------------------------------
vid_probe_avail:
    push ax
    push cx
    mov cl, [vid_kind]          ; the adapter we are RUNNING on: available by
    mov al, 1                   ; definition, and the one we must not write to
    shl al, cl
    mov [vid_avail], al

    cmp byte [vid_kind], VID_VGA    ; ...and a VGA/EGA does mode 6 too
    jne .herc
    or byte [vid_avail], VID_A_CGA
.herc:
    cmp byte [vid_kind], VID_HERC
    je .cga                     ; already marked, and its RAM is the screen
    mov ax, VID_HERC_SEG
    call vid_memchk
    jc .cga
    or byte [vid_avail], VID_A_HERC
.cga:
    test byte [vid_avail], VID_A_CGA
    jnz .out                    ; a VGA answered for it, or it is the screen
    mov ax, 0xB800
    call vid_memchk
    jc .out
    cmp byte [vid_kind], VID_HERC    ; ...and on a mono machine it has to be a
    jne .yes                         ; SECOND CARD and not the first one again
    call vid_cga_alias
    jc .out
.yes:
    or byte [vid_avail], VID_A_CGA
.out:
    ; ...and one adapter is not a CHOICE, so the Control Panel does not offer
    ; a page about it (SPEC.md 31.10.1). Here because this is the only routine
    ; that can answer the question and it answers it once - [vid_avail] is
    ; fixed for the life of the machine, so both bytes below are written once.
    mov al, [vid_avail]
    dec al
    and al, [vid_avail]         ; x & (x-1) == 0: exactly one bit set
    jnz .done
    dec byte [cp_nst]           ; one row fewer on screen...
    or byte [cp_hide], 1 << CP_IVID     ; ...and WHICH one. The count alone
                                ; could only shorten the list, which is why
                                ; this page had to be the last row in
                                ; cp_items; the bit says which record is
                                ; missing, so cp_v2r can skip it wherever it
                                ; sits and the ordering is free
.done:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_cga_alias - is B800 just the mono card's own second page?
;
; in:       nothing (the machine is running on the mono card)
; out:      CF = 1 it is the same memory - there is no CGA
;           CF = 0 they are two different cards
; clobbers: flags
;
; MEMORY ANSWERING AT B800 IS NOT A CGA, and a Hercules is exactly what it can
; be instead. The card has 64KB in two 32KB pages, page 1 being B8000 - which
; is where a CGA in the same machine lives, and why vid_setmode deliberately
; leaves 3BFh's page bit CLEAR (§39.6): we only ever address page 0, and
; enabling the second decode would put two cards on the bus for one address.
;
; A card that honours that bit does not answer at B800 and vid_memchk has
; already rejected it. Not every card does, so this is the backstop: ONE CARD
; WEARING TWO ADDRESSES answers at both apertures out of the same memory, and
; two cards do not. Write a different value through each segment and read the
; first back - vid_memchk's own idiom, one aperture apart instead of one
; offset apart.
;
; THE TEST THIS REPLACES COMPARED AN ADDRESS WITH ITSELF. It wrote at
; B000:8000 and looked for the byte at B800:0000, and on an 8086 those are the
; same linear address - 0xB8000 either way, whichever segment reaches it. So
; it answered "same" on every machine where anything at all decodes B800,
; including a machine with a perfectly good CGA in it, and vid_probe_avail
; then threw that CGA away. It is reached only when the HERCULES is the
; primary, which is why it survived: the direction that was testable on the
; old emulator was CGA-primary, where this routine never runs. Found by
; tests/dualcheck.py on a machine with two provably independent cards -
; vid_avail read 0x02 (Hercules alone) where it should read 0x06.
;
; Offset 0x7F00 is inside the Hercules' page 0 and PAST THE LAST DISPLAYED
; BYTE: four banks of 87 rows x 90 bytes end at 0x7796, and page 0 runs to
; 0x7FFF. So the probe never flickers the screen it is running on. On the CGA
; side that offset is past its 16KB and reads back through whatever mirroring
; the card does, which is fine - the question is only whether the two answers
; come from one memory, and both bytes are restored either way.
;
; What it still cannot see: a card that ignores the page bit AND presents a
; true 64KB, where B000:7F00 and B800:7F00 are genuinely different bytes of
; one device. That reports two cards. The alternative - probing the size at
; B800, since a CGA's 16KB mirrors where a Hercules' 32KB page does not - was
; considered and REJECTED: it trades a card nobody has built for a CGA that
; does not mirror, which is the failure this routine was just fixed for.
; -----------------------------------------------------------------------------
vid_cga_alias:
    push ax
    push bx
    push cx
    push es
    mov ax, VID_HERC_SEG
    mov cx, 0xB800
    mov es, ax
    mov bh, [es:0x7F00]         ; bank both bytes BEFORE either is written -
    mov es, cx                  ; aliased, they are the same byte and the two
    mov bl, [es:0x7F00]         ; restores below simply agree
    mov es, ax
    mov byte [es:0x7F00], 0x3C  ; two values neither vid_memchk pattern leaves,
    mov es, cx                  ; and different from each other: if the second
    mov byte [es:0x7F00], 0xC3  ; landed on the first, the first is gone
    mov es, ax
    cmp byte [es:0x7F00], 0x3C  ; still ours? then they are two memories
    mov [es:0x7F00], bh         ; put both back - `mov` touches no flag, so the
    mov es, cx                  ; compare above still decides
    mov [es:0x7F00], bl
    jne .same
    clc
    jmp short .o
.same:
    stc
.o:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_avail_test - may this machine be driven as AL?
; in:       AL = a VID_* kind
; out:      CF = 0 yes, CF = 1 no
; clobbers: flags
; -----------------------------------------------------------------------------
vid_avail_test:
    push cx
    push bx
    cmp al, VID_CGA             ; a byte from SYSTEM.CFG written by a kernel
    ja .no                      ; that knew a fourth adapter
    mov cl, al
    mov bl, 1
    shl bl, cl
    test [vid_avail], bl
    pop bx
    pop cx
    jz .noflag
    clc
    ret
.noflag:
    stc
    ret
.no:
    pop bx
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; vid_equip - tell the BIOS which KIND of display this machine now has
;
; in:       [vid_kind]
; out:      -
; clobbers: flags
;
; The counterpart to viddet.inc's vid_cga_equip, and it exists because that
; one is a ONE-WAY door: it flips 40:10's bits 5:4 from mono to colour so an
; XT BIOS's equipment-driven mode set can reach the CGA, and nothing ever put
; them back. That was correct while the adapter was chosen once at boot. It
; is not correct once the user can switch: on the 5150 with both cards,
; Hercules -> CGA -> Hercules left the flag saying colour, and CMD_REBOOT's
; vid_text (int 10h AX=0007h) would then have programmed the CGA instead of
; the MDA - a machine that reboots to a dead mono monitor.
;
; Only vid_switch calls it, so the boot path is untouched: an auto-detected
; machine still sets exactly the bits its BIOS already had.
; -----------------------------------------------------------------------------
vid_equip:
    push ax
    push bx
    push ds
    mov bl, 0x20                ; 10b = 80x25 colour
    cmp byte [vid_kind], VID_HERC   ; ...read while DS is still the kernel's
    jne .go
    mov bl, 0x30                ; 11b = monochrome at B000
.go:
    mov ax, 0x0040              ; the BIOS data area
    mov ds, ax
    mov al, [0x0010]
    and al, 0xCF
    or al, bl
    mov [0x0010], al
    pop ds
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; spl_reset - put the loading screen back after a mode switch (SPEC.md 39.11.3)
;
; in:       nothing
; out:      nothing (all registers preserved)
; clobbers: -
;
; vid_setmode clears the framebuffer, so a switch taken from drv_boot - while
; the boot is still reading sectors and ticking the bar - wipes the loading
; screen. This redraws it: the chrome from spl_rechrome, which is spl_chrome
; without the adapter re-probe that would undo the switch, and then one
; ordinary frame from the progress the bar had already reached.
;
; It lives here rather than in splash.inc because that module has to fit
; inside the image's first SPL_RESIDENT sectors and this is not reachable
; from the splash - the label it enters at costs that budget nothing.
;
; [spl_live] is the guard, and it is what makes this safe to call from
; anywhere: after spl_finish the splash owns nothing and drawing a dialog
; over the desktop would be a bug. The Control Panel's switch therefore
; never calls it and would be harmless if it did.
; -----------------------------------------------------------------------------
spl_reset:
    cmp byte [spl_live], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call spl_rechrome
    call spl_paint
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; vid_blank - dark the card the machine is LEAVING (SPEC.md 39.11.4)
;
; in:       AL = the kind being switched TO; [vid_kind] = the one being left
; out:      -
; clobbers: flags
;
; On a two-monitor 5150 a switch otherwise leaves the outgoing tube lit with a
; frozen desktop on it - harmless, since the live screen is the one whose
; cursor moves, and still the wrong thing to show somebody. Stopping the video
; signal costs two OUTs and the card keeps its sync, exactly as vid_setmode's
; own blank-first sequence does (SPEC.md 39.6).
;
; ONLY WHEN THE TWO KINDS ARE TWO CARDS, which here means exactly one of them
; is the mono one. That test is not fussiness: on a VGA machine the CGA row IS
; the VGA doing mode 6 (SPEC.md 39.11.1), so "blank the outgoing card" would
; blank the very card the next three lines programme, and the machine would
; come back on a dark screen.
;
; Nothing has to be un-blanked: vid_setmode re-enables whichever card it
; programmes, on both paths.
; -----------------------------------------------------------------------------
vid_blank:
    push ax
    push dx
    cmp al, VID_HERC
    je .tomono
    cmp byte [vid_kind], VID_HERC
    jne .out                    ; colour -> colour: one card, nothing to do
    mov dx, 0x3B8               ; leaving the Hercules: video off (bit 3), and
    xor al, al                  ; 3BFh back to "graphics not allowed", which is
    out dx, al                  ; also the page-1 decode locked out again
    mov dx, 0x3BF
    out dx, al
    jmp short .out
.tomono:
    cmp byte [vid_kind], VID_HERC
    je .out                     ; already there; vid_switch caught this anyway
    mov dx, 0x3D8               ; leaving a CGA: mode control, video bit clear.
    xor al, al                  ; A VGA does not implement 3D8h at all, so a
    out dx, al                  ; VGA-plus-Hercules machine keeps its VGA lit -
.out:                           ; accepted, that pairing being one nobody built
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_switch - drive a different adapter, now (SPEC.md 39.11.2)
;
; in:       AL = the VID_* kind to switch to
; out:      CF = 0 the machine is now on AL, CF = 1 refused (not available)
; clobbers: flags
;
; THE CALLER OWES THE REPAINT, and deliberately: vid_setmode clears the
; framebuffer, so what is on screen when this returns is a black rectangle of
; the new geometry. Its two callers want different things put back - the
; Control Panel posts [cp_dirty] and lets ui_task's step 3 do the
; wm_paint_all outside the lock (SPEC.md 13/31.2), and drv_boot redraws the
; SPLASH, because at that point the desktop does not exist yet (SPEC.md 15.3).
; Doing it here would make one of them wrong.
;
; Order is binding, and every step is somebody else's invariant:
;
;   1. Spend the deferred cursor hide (SPEC.md 7.1.4) while the old geometry
;      still describes the save-under. gfx_lock only PROMISES the hide, so
;      under the Control Panel's lock the arrow is typically still on screen
;      with 24 bytes of background banked against the old stride. Change the
;      geometry first and gfx_unlock restores those bytes through the new
;      addressing, which smears the arrow across the screen permanently. It
;      is a no-op on the boot path, where no promise is outstanding.
;
;   2. vid_apply republishes all nine live words and everything derived from
;      them, and homes the mouse into the new screen - which is load-bearing
;      rather than tidy, for the reason in SPEC.md 39.2.
;
;   3. desk_rowcalc, because how many drive zones fit in a column is computed
;      from [vid_dock_y0] and is 7 on VGA, 4 on Hercules and 2 on CGA - and
;      wm_refit, because every window's frame was clamped onto the OLD screen
;      and a 640x480 window is off the bottom of a 640x200 one.
; -----------------------------------------------------------------------------
vid_switch:
    cmp al, [vid_kind]
    je .already
    call vid_avail_test
    jc .refuse

    call cur_unlazy             ; 1. the cursor's save-under is the old
    call vid_blank              ; geometry's - and so is the tube we are
    call wm_su_drop_all         ; walking away from, and so is every raise
                                ; cache (SPEC.md 39.14.8.1): wm_su_ck compares
                                ; RECTS, and a window small enough to fit both
                                ; geometries does not move while the pixels
                                ; behind it change card, stride and possibly
                                ; plane count

    mov [vid_kind], al          ; 2. republish the geometry
    call vid_equip
    call vid_apply
    call vid_setmode

%ifdef KERN_BIG
    call vid_disp_init          ; 3. ...and the OTHER card follows the switch:
                                ; the records, the chrome's extent and the
                                ; desktop union all describe the arrangement
                                ; before it until this re-runs (SPEC.md
                                ; 39.13/39.16). Before desk_rowcalc and
                                ; wm_refit, which read what it publishes
%endif
    call desk_rowcalc           ; 4. the two things vid_apply does not own
    call wm_refit
    call menu_force             ; ...and the two the CHROME does not: setmode
    call dock_force             ; cleared the framebuffer, so the bar's logo
                                ; and rule and the whole dock strip are gone,
                                ; and both modules keep a record of what they
                                ; last put on the glass (SPEC.md 12.9/30.3).
                                ; The caller's repaint is what spends these -
                                ; it used to inherit them from wm_paint_all,
                                ; which no longer forces either, because
                                ; nothing IN wm_paint_all destroys them
.already:
    clc
    ret
.refuse:
    stc
    ret

; =============================================================================
; The per-display context (SPEC.md 39.12) - kern_big only
;
; STEP 1 of docs/DUAL-DISPLAY-PLAN.md 11: the state and the swap, with
; [vid_ndisp] = 1 and nothing on a drawing path calling any of it. So the
; screen is byte-identical to the build before it, on all three adapters, by
; construction rather than by inspection - which is the gate this step is
; allowed to land against.
;
; WHAT A DISPLAY OWNS is the eighteen contiguous words `vid_seg` through
; `vid_rend`: the nine live geometry words and the nine derived ones the
; RENDERER reads (viddet.inc). What it does not own starts at `vid_dock_y0` -
; the dock row, the clock cell, the drive column - because those describe the
; DESKTOP and must not change because a primitive happened to be drawing on
; the other monitor (DUAL-DISPLAY-PLAN 5).
;
; COPY, DO NOT INDIRECT, and that is the whole reason this is a swap rather
; than a pointer. gfx_rowbase and gfx_nextrow read their parameters as
; ABSOLUTE operands through CS, and gfx_nextrow's contract is DI and flags and
; NOTHING ELSE - several callers are inner loops with no spare register and one
; is inside IRQ4. There is no register to index a per-display block through, so
; the active display's numbers are copied into the words the renderer already
; reads. It is vid_apply's own idiom, and 36 bytes of rep movsw against a
; 756 us per-call floor is under a tenth of one primitive - paid only when the
; display actually changes, which spatial coherence makes rare.
; =============================================================================

; -----------------------------------------------------------------------------
; vid_blank_kind / vid_unblank_kind - dark a card and bring it back
;                                     (SPEC.md 39.18.1, 64.3)
;
; in:       AL = the VID_* kind whose card to blank/unblank
; out:      -
; clobbers: flags
;
; vid_blank (39.11.4) is written for a SWITCH - it takes the kind being moved
; TO and reads [vid_kind] for the one being left. An fsx bracket blanks a card
; that nobody is switching away from, so it needs the question asked directly,
; and SPEC.md 64's idle blanker asks it of every card the machine has.
;
; OUTSIDE THE DUAL-DISPLAY FENCE BELOW, which is where these two used to live.
; They were reachable only from vid_fsx_enter/vid_fsx_unblank, so kern_big was
; the only build that had a way to dark a card at all - and the idle blanker is
; not a dual-display feature. A 128KB machine has a monitor to protect too, and
; on the class of hardware kern_small exists for it is the older monitor.
;
; ALL THREE ADAPTERS, and the VGA arm is the one that had to be written. Both
; bodies used to test only for Hercules and send everything else to the CGA
; branch, which writes 3D8h - a register a VGA does not implement, so the OUT
; was swallowed by the bus and did nothing at all. vid_blank's own header
; records the resulting hole as accepted ("a VGA-plus-Hercules machine keeps
; its VGA lit"), which is affordable for a pairing nobody built and is not
; affordable for a blanker: silently declining to blank on one adapter in three
; is exactly the class of failure this tree keeps paying for. So the VGA gets
; its own arm - Sequencer register 1 (Clocking Mode) bit 5, Screen Off.
;
; All three gate the video SIGNAL and nothing else: the CRTC keeps running and
; both syncs keep coming out, so the tube stays locked, the monitor is never
; asked to re-acquire, and what the card holds in memory is untouched. That is
; the property the whole feature rests on - vid_setmode's own blank-first
; sequence (39.6) says the same thing from the other end - and it is why
; blanking may be done to a card that a background task is drawing on.
;
; THE HERCULES ARM WRITES 0x02 AND NOT 0, WHICH IS A FIX. Zero clears bit 3,
; the video enable - and it clears bit 1, the GRAPHICS bit, with it, so the
; card was not being blanked at all: it was being put into MDA TEXT mode, with
; a 6845 still carrying 720x348 timings and the graphics framebuffer under it
; being drawn as character cells. On real iron bit 3 is down so nothing of that
; reaches the tube and it merely looks like a blank; on a machine whose mono
; card models the mode bit and not the enable bit it is plainly visible, and
; that is how it was found (MartyPC: 136,377 lit pixels before, 138,046 after
; a "blank"). 0x02 is what vid_setmode itself writes to blank the card across
; its own mode change - graphics, page 0, video off - so the two agree now, and
; a blank changes exactly one bit and leaves the mode alone. 3BFh is left
; alone for the same reason: the old pair also locked graphics back out, which
; is a second piece of state for the unblank to put back and buys nothing when
; the video signal is already gated.
;
; UNBLANKING IS NOT vid_setmode. Re-running the mode set on a card whose mode
; never changed buys a 32KB clear nobody asked for and a 6845 sync transient
; somebody might see. The inverses are exact instead: the Hercules' two are
; the two vid_setmode itself writes (3BFh graphics allowed, then 3B8h graphics
; + page 0 + video), the CGA's comes out of the BIOS's own shadow of 3D8h
; at 40:65h - which is write-only on the card, so there is nothing to read
; back and the ROM's record is the only honest source - and the VGA's is the
; same SR01 with bit 5 put back down.
; -----------------------------------------------------------------------------
vid_blank_kind:
    push ax
    push dx
    push es
    cmp al, VID_HERC
    je .mono
    cmp al, VID_VGA
    je .vga
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x0065]         ; the BIOS's shadow of 3D8h...
    and al, ~8                  ; ...minus the video-enable bit
    mov dx, 0x3D8
    out dx, al
    jmp short .out
.mono:
    mov dx, 0x3B8               ; graphics + page 0, video OFF (bit 3 clear) -
    mov al, 0x02                ; byte for byte the value vid_setmode's own
    out dx, al                  ; blank-first sequence writes (SPEC.md 39.6)
    jmp short .out
.vga:
    mov al, 0x20                ; SR01 bit 5 up: Screen Off
    call vid_seq1_blank
.out:
    pop es
    pop dx
    pop ax
    ret

vid_unblank_kind:
    push ax
    push dx
    push es
    cmp al, VID_HERC
    je .mono
    cmp al, VID_VGA
    je .vga
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x0065]         ; ...exactly what the ROM's mode set left
    mov dx, 0x3D8
    out dx, al
    jmp short .out
.mono:
    mov dx, 0x3BF               ; the order vid_setmode uses: allow graphics
    mov al, 1                   ; first, then turn the video on
    out dx, al
    mov dx, 0x3B8
    mov al, 0x0A
    out dx, al
    jmp short .out
.vga:
    xor al, al                  ; SR01 bit 5 down again
    call vid_seq1_blank
.out:
    pop es
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_seq1_blank - internal: write bit 5 of the VGA's Sequencer register 1
;
; in:       AL = 0x20 to dark the screen, 0 to light it
; out:      -
; clobbers: flags (AL is consumed)
;
; IT RUNS WITH IF=0, AND THAT IS THE ONLY INTERESTING THING ABOUT IT. SR01's
; other bits are the dot clock and the character width; they belong to whatever
; mode the card is in and cannot be guessed, so the register has to be READ -
; which makes this an index write, a read and a data write, three instructions
; with two gaps in them.
;
; Every other Sequencer access in the kernel is a single `out dx, ax`, index
; and data in one instruction, which is atomic by construction and needs
; nothing. This one cannot be, and the gap is reachable: vga12.inc's plane
; loops leave the Sequencer INDEX at 2 (Map Mask) for the whole of a plane's
; rep movsb, and every drawing task can be pre-empted mid-row. A switch landing
; between the index write and the read below would read the Map Mask and then
; write the screen-off bit INTO it - a card blanked by having one plane's write
; mask corrupted, which is neither of the two things this routine can be asked
; for. pushf/cli ... popf, never cli/sti (SPEC.md 1).
;
; Leaving the index at 1 afterwards costs nothing, for the same reason: every
; reader re-selects with its own word OUT.
; -----------------------------------------------------------------------------
vid_seq1_blank:
    push ax
    push bx
    push dx
    mov bl, al                  ; the wanted state of bit 5
    pushf
    cli
    mov dx, VGA_SEQ
    mov al, 1                   ; SR01, Clocking Mode
    out dx, al
    inc dx
    in al, dx
    and al, ~0x20               ; ...with bit 5 replaced and the dot clock,
    or al, bl                   ; the character width and the rest untouched
    out dx, al
    popf
    pop dx
    pop bx
    pop ax
    ret

%ifdef KERN_BIG

VID_NDISP_MAX equ 2             ; the machine this is for holds two cards
VID_CTX_W    equ 18             ; words copied to and from the live block
VID_CTX_CW   equ 14             ; ...within the run, the display's own extent -
VID_CTX_CH   equ 16             ; word 7 and word 8, vid_cw/vid_ch (39.2.1)
VID_CTX_VX   equ VID_CTX_W*2    ; ...then the display's origin in the VIRTUAL
VID_CTX_VY   equ VID_CTX_W*2+2  ; desktop, which is NOT copied anywhere
VID_CTX_KIND equ VID_CTX_W*2+4  ; ...and which ADAPTER it is, because SPEC.md
                                ; 39.18 has to put [vid_kind] where the live
                                ; geometry is and a segment is not a kind
VID_CTX_SZ   equ VID_CTX_W*2+6

; ...and those two offsets are a SECOND opinion about viddet.inc's column
; order, so they are asserted rather than trusted, exactly as the run's length
; is above. vid_tab's rows and the live block agree by construction; this file
; is the only place that indexes into the middle of the copy.
%if vid_cw - vid_seg != VID_CTX_CW || vid_ch - vid_seg != VID_CTX_CH
%error "vid_cw/vid_ch are not at VID_CTX_CW/CH in the run - see viddet.inc"
%endif

; The live run this copies must be the run viddet.inc lays out, and a reorder
; there would be silent here - so it is asserted rather than trusted.
%if vid_chm8 - vid_seg != (VID_CTX_W-1)*2
%error "vid_seg..vid_chm8 is not VID_CTX_W contiguous words - see viddet.inc"
%endif

; .text with REAL INITIALISERS, not .bss, and for the reason vid_avail above
; is: -f bin zeroes nothing at all, so a byte in .bss holds whatever the
; machine left there until something writes it. These two are read before
; vid_ctx_init runs on any path that ever grows one - and a garbage [vid_ndisp]
; is a loop bound, while a garbage [vid_cur] is a display index that indexes
; past the array. cur_sptr and dsk_fatwc are the two this tree has already been
; caught by.
vid_ndisp   db 1                ; displays the kernel has brought up. 1 for
                                ; ever on a machine with one card
vid_cur     db 0                ; which display's geometry is in the live block
vid_ox      dw 0                ; ...and its ORIGIN in the virtual desktop,
vid_oy      dw 0                ; which is what every 39.14 hook subtracts.
                                ; OUTSIDE the eighteen-word run on purpose:
                                ; that run is asserted contiguous and is what
                                ; the RENDERER reads, and an origin is not
                                ; renderer state. Published by vid_ctx_act

; SPEC.md 39.19's two bytes - the WHOLE of what the user gets to decide about
; the arrangement, and the only thing vid_disp_init reads. Everything 39.13 to
; 39.18 built is downstream of them and none of it knows they exist.
;
; They sit here, immediately after vid_oy, so that SPEC.md 57.4's 'VD' block
; can name ndisp/cur/ox/oy/dmode/dlay with ONE pointer - rule 6's "publish a
; pointer to state that already exists" taken at the point where it costs a
; layout decision rather than a copy.
;
; THE DEFAULT IS SINGLE, and 39.19.1 is why in full: the kernel can detect a
; second CARD and nothing it can do detects a second MONITOR, so extending by
; default puts half the desktop - and the pointer that would reach the control
; to undo it - on glass that may not exist.
vid_dmode   db 0                ; 0 = Single, 1 = Extend
vid_dlay    db VID_L_RIGHT      ; where display 1 sits when extended

section .bss
; The records themselves are scratch - written by vid_ctx_init before anything
; reads them - so they cost the image nothing and live here.
vid_ctx     resb VID_NDISP_MAX * VID_CTX_SZ
section .text

; -----------------------------------------------------------------------------
; vid_ctx_ptr - internal: DI = the record for display AL
; in:       AL = display index (0..VID_NDISP_MAX-1)
; out:      DI = record address
; clobbers: DI, flags. AX is preserved, because every caller still needs it.
; -----------------------------------------------------------------------------
vid_ctx_ptr:
    push ax
    push dx
    mov dl, VID_CTX_SZ
    mul dl                      ; AX = index * 40 (not a power of two)
    mov di, vid_ctx
    add di, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_ctx_capture - bank the live geometry into display AL's record
;
; in:       AL = display index
; out:      -
; clobbers: flags
;
; CALLED AFTER ANYTHING THAT CHANGES THE LIVE GEOMETRY OUTSIDE A SWAP. That
; is vid_apply - a mode or adapter change - and, since SPEC.md 32 went, only
; vid_apply: sw_set used to write [vid_rseg] from inside this run and was the
; one other entry on the list. Miss one and the display's record describes a
; geometry that has moved on - which shows up as drawing through the wrong
; segment the next time that display is activated, a long way from the cause.
; -----------------------------------------------------------------------------
vid_ctx_capture:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov cx, KERNEL_SEG          ; both ends are ours; the caller's ES is not
    mov ds, cx
    mov es, cx
    call vid_ctx_ptr            ; DI = the record
    push di
    mov si, vid_seg
    mov cx, VID_CTX_W
    cld
    rep movsw
    pop di
    mov cl, [vid_kind]          ; ...and the adapter this geometry IS. Here
    mov [di+VID_CTX_KIND], cl   ; rather than in the run, because the run is
                                ; what the RENDERER reads and a kind is not
                                ; renderer state (SPEC.md 39.12)
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_ctx_act - make display AL the one the renderer draws on
;
; in:       AL = display index
; out:      [vid_cur] = AL; the live block holds that display's geometry
; clobbers: flags
;
; A NO-OP WHEN IT IS ALREADY CURRENT, which is the case that decides whether
; this is affordable: drawing is spatially coherent - a window's W_PAINT is a
; burst of calls in one place, a font_run row is one call - so the steady state
; is two compares against a 756 us floor. Only a rect that genuinely crosses to
; the other monitor pays the copy.
; -----------------------------------------------------------------------------
vid_ctx_act:
    cmp al, [vid_cur]
    je .out
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov cx, KERNEL_SEG
    mov ds, cx
    mov es, cx
    call vid_ctx_ptr
    mov si, di                  ; ...the record is the SOURCE this way round
    mov di, vid_seg
    mov cx, VID_CTX_W
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    mov [vid_cur], al
    push di                     ; ...and republish the origin every hook reads
    push ax
    call vid_ctx_ptr
    mov ax, [di+VID_CTX_VX]
    mov [vid_ox], ax
    mov ax, [di+VID_CTX_VY]
    mov [vid_oy], ax
    pop ax
    pop di
.out:
    ret

; -----------------------------------------------------------------------------
; vid_ctx_init - one display, at the origin, holding what is on screen now
;
; in:       the live geometry, already published by vid_apply
; out:      [vid_ndisp] = 1, [vid_cur] = 0, display 0 captured at (0,0)
; clobbers: flags
;
; Called from kmain beside vid_probe_avail, and from vid_switch, and NOT from
; vid_apply - which is the trap this module already carries a scar from.
; vid_apply runs from the SPLASH, on its first tick, while the rest of the
; kernel is still coming off the floppy (SPEC.md 15.3), so a call from there
; into this file executes whatever has not been loaded yet: black screen, no
; clue. viddet.inc's header says so about cur_unlazy for the same reason.
; -----------------------------------------------------------------------------
vid_ctx_init:
    push ax
    push cx
    push di
    push es
    mov cx, KERNEL_SEG
    mov es, cx
    mov byte [vid_ndisp], 1
    mov byte [vid_cur], 0
    xor al, al
    call vid_ctx_ptr
    xor ax, ax                  ; display 0 sits at the virtual origin
    mov [es:di+VID_CTX_VX], ax
    mov [es:di+VID_CTX_VY], ax
    mov [vid_ox], ax            ; ...and it is current, so vid_ctx_act - which
    mov [vid_oy], ax            ; is what normally publishes these - never runs
    xor al, al
    call vid_ctx_capture
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_fsx_enter - one display, and the others dark (SPEC.md 39.18)
;
; in:       AL = the display the bracket runs on
; out:      the machine describes itself as having ONE display, that one, at
;           the virtual origin; every other card is blanked
; clobbers: flags
;
; INSIDE A BRACKET THE MACHINE IS SINGLE-DISPLAY AGAIN, and that is the whole
; design: every SPEC.md 53 contract - fsx_mode, the FSI block, fsx_wait,
; FSXF_KEEPWORKER, FSXF_FASTTICK - is then exactly what it is today, and no
; fsx app needs a line.
;
; [vid_kind] MOVES TO THAT DISPLAY'S ADAPTER, which is what makes it work
; rather than merely look right: fsx_caps, fsx_mode and vid_setmode all read
; it, and vid_apply then republishes the live block, the desktop words and
; 39.16's chrome extent from vid_tab - one call, no second opinion.
;
; The others are BLANKED because nothing can maintain them: the kernel does
; not run inside the bracket (53.1), so what a live card would show is a
; frozen desktop. 39.11.4's argument, unchanged.
; -----------------------------------------------------------------------------
vid_fsx_enter:
    cmp byte [vid_ndisp], 1
    jbe .out                    ; one display: nothing here applies
    push ax
    push cx
    push di
    mov [fsx_vdisp], al
    mov cl, [vid_kind]
    mov [fsx_vkind], cl         ; the DESKTOP's adapter, to come back to
    mov cl, [vid_ndisp]
    mov [fsx_vndisp], cl

    xor ch, ch                  ; ...dark every card but the bracket's
.blank:
    cmp ch, al
    je .bnext
    push ax
    mov al, ch
    call vid_ctx_ptr
    mov al, [di+VID_CTX_KIND]
    call vid_blank_kind
    pop ax
.bnext:
    inc ch
    cmp ch, cl
    jb .blank

    call vid_ctx_ptr            ; ...and become that one
    mov cl, [di+VID_CTX_KIND]
    mov [vid_kind], cl
    call vid_apply              ; the live block, the desktop words and the
                                ; chrome, all of it that display's
    mov byte [vid_ndisp], 1
    mov byte [vid_cur], 0       ; consistent with the lie: one display, and
    mov word [vid_ox], 0        ; it is at the virtual origin
    mov word [vid_oy], 0
    pop di
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; vid_fsx_leave - the desktop's geometry back (SPEC.md 39.18)
; in:       nothing
; out:      the union, the primary's chrome, the records unchanged
; clobbers: flags
;
; The records were never written inside the bracket, so this is vid_apply on
; the desktop's own adapter plus 39.16's union - and NOT an unblank, which
; owes the repaint first (39.18.1).
; -----------------------------------------------------------------------------
vid_fsx_leave:
    cmp byte [fsx_vndisp], 1
    jbe .out
    push ax
    mov al, [fsx_vkind]
    mov [vid_kind], al
    call vid_apply
    mov al, [fsx_vndisp]
    mov [vid_ndisp], al
    mov byte [vid_cur], 0
    call vid_desk_union
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; vid_fsx_unblank - the dark monitors back, AFTER the repaint (SPEC.md 39.18.1)
; in:       nothing
; out:      every card lit again; [fsx_vndisp] spent
; clobbers: flags
;
; AFTER wm_paint_all AND NOT BEFORE IT. Blanking gates the video SIGNAL and
; not memory, so a blanked card's framebuffer still holds the desktop as it
; was when the bracket started - minutes stale, on a monitor the user has not
; been looking at. Unblanking first shows that frame and then paints over it;
; repainting first means the card is only ever shown what is current.
; -----------------------------------------------------------------------------
vid_fsx_unblank:
    cmp byte [fsx_vndisp], 1
    jbe .out
    push ax
    push cx
    push di
    mov cl, [fsx_vndisp]
    mov byte [fsx_vndisp], 1    ; one-shot: a bracket that set no mode and a
    xor ch, ch                  ; second restore must not write these ports
.d:
    cmp ch, [fsx_vdisp]
    je .next
    mov al, ch
    call vid_ctx_ptr
    mov al, [di+VID_CTX_KIND]
    call vid_unblank_kind
.next:
    inc ch
    cmp ch, cl
    jb .d
    pop di
    pop cx
    pop ax
.out:
    ret

fsx_vdisp   db 0                ; the display a bracket is running on...
fsx_vkind   db 0                ; ...the DESKTOP's adapter, banked across it
fsx_vndisp  db 1                ; ...and how many displays there were. 1 means
                                ; "no bracket state to undo", which is the
                                ; boot value and what vid_fsx_unblank leaves

; -----------------------------------------------------------------------------
; vid_disp_dither - the desktop on every display but the primary
;                   (SPEC.md 39.14.4)
;
; in:       nothing; the caller holds the gfx lock
; out:      nothing
; clobbers: flags
;
; wm_paint_all draws display 0's band itself - menu bar to dock, the call that
; has always been there - and this is the rest. Each is ONE gfx_fill_gray in
; VIRTUAL coordinates, and GFXDISP is what puts it on the right card; nothing
; here knows which card that is, or that there is more than one way to be a
; framebuffer.
;
; A secondary display is dithered WHOLE, because SPEC.md 39.14's chrome - the
; menu bar, the dock, the drive column - lives on the primary alone.
;
; This is the only caller in the kernel that draws in virtual coordinates.
; Everything else is inside the chrome or inside a window, and both are on
; display 0, where virtual and local are the same numbers (39.14.4).
; -----------------------------------------------------------------------------
vid_disp_dither:
    cmp byte [vid_ndisp], 1
    jbe .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, 1                   ; display 0 is wm_paint_all's own
.d:
    mov ax, si
    call vid_ctx_ptr
    mov ax, [di+VID_CTX_VX]
    mov cx, ax
    add cx, [di+VID_CTX_CW]
    dec cx
    mov bx, [di+VID_CTX_VY]
    mov dx, bx
    add dx, [di+VID_CTX_CH]
    dec dx
    call gfx_fill_gray
    inc si
    mov al, [vid_ndisp]
    mov ah, 0
    cmp si, ax
    jb .d
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; vid_disp_of - which display holds the virtual point (AX, BX)? (SPEC.md 39.14)
;
; in:       AX = x, BX = y, in VIRTUAL desktop coordinates
; out:      AL = display index
; clobbers: AX, flags
;
; IT NEVER FAILS, and that is what keeps every hook in 39.14.2 free of a CF to
; test: the virtual desktop is not a rectangle (39.2.1), so a point can fall in
; the DEAD ZONE between two displays - and the honest answer there is the
; PRIMARY, where the primitive's own edge clip discards it for nothing. A
; refusal would have to be handled at seven call sites, six of which would
; handle it by discarding the shape anyway.
; -----------------------------------------------------------------------------
vid_disp_of:
    call vid_disp_find
    jnc .out
    xor al, al                  ; nowhere: the dead zone, and the primary is
.out:                           ; where a shape goes to be clipped away
    ret

; -----------------------------------------------------------------------------
; vid_disp_find - the same question, allowed to answer NO
; in:       AX = x, BX = y, virtual
; out:      CF = 0 and AL = the display holding it; CF = 1 = no display does
; clobbers: AX, flags
;
; Separate from vid_disp_of because the two callers want opposite things from
; a miss. A drawing hook wants somewhere to send a shape that will then be
; clipped away; mou_clamp (SPEC.md 39.15.4) wants to KNOW, because "inside no
; display" is exactly the case it exists to correct.
; -----------------------------------------------------------------------------
vid_disp_find:
    push bx
    push cx
    push dx
    push di
    xor cl, cl                  ; CL = the display being tested, and DI walks
    mov di, vid_ctx             ; the records rather than vid_ctx_ptr deriving
.next:                          ; them - because THAT takes its index in AL,
                                ; and AL is the low byte of the x being tested.
                                ; It cost a real bug: `mov al, cl` zeroed the
                                ; low byte of x, so a point at 1300 was tested
                                ; as 1280 and one at 660 as 640+1 - which lands
                                ; on the right display nearly always, and moved
                                ; a pointer stepping across the seam onto the
                                ; wrong side of it. Walking the array needs no
                                ; register at all
    mov dx, [di+VID_CTX_VX]
    cmp ax, dx
    jb .no                      ; left of it
    add dx, [di+VID_CTX_CW]
    cmp ax, dx
    jae .no                     ; ...or right of its last column
    mov dx, [di+VID_CTX_VY]
    cmp bx, dx
    jb .no
    add dx, [di+VID_CTX_CH]
    cmp bx, dx
    jb .hit
.no:
    add di, VID_CTX_SZ
    inc cl
    cmp cl, [vid_ndisp]
    jb .next
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret
.hit:
    mov al, cl
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; vid_pt_local - a virtual point in the display holding it (SPEC.md 39.15)
; in:       AX = x, BX = y, virtual
; out:      CL = the display, AX/BX its own coordinates
; clobbers: AX, BX, CL, flags
; -----------------------------------------------------------------------------
vid_pt_local:
    push dx
    push di
    push ax                     ; vid_disp_of ANSWERS in AL, so x is banked
    call vid_disp_of
    mov cl, al
    call vid_ctx_ptr            ; DI = its record; AX preserved
    pop ax
    sub ax, [di+VID_CTX_VX]
    sub bx, [di+VID_CTX_VY]
    pop di
    pop dx
    ret

; -----------------------------------------------------------------------------
; vid_pt_clamp - the nearest virtual point inside display CL (SPEC.md 39.15.4)
; in:       AX = x, BX = y virtual, CL = the display to clamp into
; out:      AX/BX inside it
; clobbers: AX, BX, flags
; -----------------------------------------------------------------------------
vid_pt_clamp:
    push cx
    push dx
    push di
    push ax
    mov al, cl
    call vid_ctx_ptr
    pop ax
    mov dx, [di+VID_CTX_VX]     ; x into [vx, vx + cw - 1]
    cmp ax, dx
    jge .xlo
    mov ax, dx
.xlo:
    add dx, [di+VID_CTX_CW]
    dec dx
    cmp ax, dx
    jle .xok
    mov ax, dx
.xok:
    mov dx, [di+VID_CTX_VY]
    cmp bx, dx
    jge .ylo
    mov bx, dx
.ylo:
    add dx, [di+VID_CTX_CH]
    dec dx
    cmp bx, dx
    jle .yok
    mov bx, dx
.yok:
    pop di
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; vid_dual_ok - can this machine have an extended desktop? (SPEC.md 39.19)
;
; in:       nothing
; out:      CF = 0 yes, CF = 1 no
; clobbers: flags
;
; The SPEC.md 31.3 idiom - one predicate read by vid_disp_init, by whether
; SPEC.md 31.10.2's row is drawn at all, and by that row's click refusal - so
; none of the three can disagree about what this machine can do. There is no
; GREYING in that list on purpose: a machine with no choice is not shown the
; control (31.10.1's answer), which is why this never has to explain itself.
; -----------------------------------------------------------------------------
vid_dual_ok:
    push ax
    mov al, [vid_avail]
    cmp al, VID_A_HERC | VID_A_CGA
    pop ax                      ; POP does not touch flags
    je .yes
    stc
    ret
.yes:
    clc
    ret

; -----------------------------------------------------------------------------
; vid_disp_init - arrange the displays the way SPEC.md 39.19's two bytes say
;
; in:       [vid_avail], [vid_dmode], [vid_dlay], and the machine running on
;           [vid_kind] with vid_apply having published it
; out:      EXTEND on a two-card machine: both cards scanning our raster, the
;           second one black, [vid_ndisp] = 2, [vid_cur] = 0, display 1 banked
;           and placed. SINGLE: the other card dark, [vid_ndisp] = 1, the
;           desktop back to the primary alone. A machine vid_dual_ok refuses:
;           untouched, [vid_ndisp] stays 1
; clobbers: flags
;
; IT IS RE-RUNNABLE AND THAT IS THE POINT (SPEC.md 39.19). It was a boot-time
; one-shot; it is now the ONLY routine in the kernel that writes a vid_ctx
; origin or [vid_ndisp], so the Control Panel changing its mind, vid_switch
; moving the primary and drv_boot applying SYSTEM.CFG are all one call. The
; caller owes the repaint either way - the geometry it was drawn against has
; just moved.
;
; TWO DISPLAYS IS [vid_avail] == HERC|CGA EXACTLY, and an equality rather than
; a bit count for a reason per value. VGA|CGA is ONE CARD - vid_probe_avail
; sets the CGA bit on every VGA/EGA without probing, mode 6 being a standard
; BIOS mode there - so a bit count would report two displays on the commonest
; machine in the tree and programme the VGA twice. VGA|HERC|CGA genuinely IS
; two cards and is deliberately not taken: the bitmap cannot say whether the
; CGA bit is that VGA's mode 6 or a third card, and SPEC.md 39.11.4 already
; accepts that pairing as one nobody built.
;
; THE SECONDARY IS BROUGHT UP BY MAKING IT MOMENTARILY CURRENT. vid_apply is
; the only routine that turns a vid_tab row into the eighteen words a display
; owns plus everything derived from them, and it writes the LIVE block - so
; this sets [vid_kind], calls it, and banks the answer. vid_setmode then
; programmes the right card by construction rather than by argument: it reads
; [vid_kind] and [vid_seg], and both already describe the secondary.
;
; THE WAY BACK IS vid_apply AND NOT vid_ctx_act, which is the design working
; rather than a shortcut. A swap carries the smallest thing that is genuinely
; per-display, because it has to be cheap enough to sit under a drawing
; primitive (SPEC.md 39.12); vid_apply on the way IN also rewrote every desktop
; word - the dock row, the clock cell, the drive column, the pull-down cap -
; and only vid_apply puts those back.
;
; AND THE PRIMARY'S CARD IS NEVER RE-PROGRAMMED. There is no vid_setmode on the
; way back: the primary is already in its graphics mode with the loading screen
; on it (SPEC.md 15.3), and a second mode set would clear it. "Primary
; unchanged" is this step's literal gate and it holds by construction.
;
; SINGLE IS NOT "DO NOTHING", which is the half that arrived with 39.19. The
; other card is DARKED - it is a real card on a real monitor and what it would
; otherwise show is whatever DOS left there - and the desktop, [vid_ndisp] and
; the pointer all have to come home, because this path is reached by a user
; turning Extend OFF as well as by a machine booting without it.
; -----------------------------------------------------------------------------
vid_disp_init:
    push ax
    push di
    call vid_dual_ok
    jc .out                     ; one display - or a pairing we do not take

    mov ah, [vid_kind]          ; the other kind is the one this machine has
    mov al, VID_CGA             ; and is not running on
    cmp ah, VID_HERC
    je .other
    mov al, VID_HERC
.other:
    cmp byte [vid_dmode], 0
    jne .extend

    ; --- SINGLE (SPEC.md 39.19) ---------------------------------------------
    call vid_blank_kind         ; AL is still the other kind: dark its monitor
    mov byte [vid_ndisp], 1     ; ...before anything can compute a union over
    mov byte [vid_cur], 0       ; a display that is no longer there
    mov [vid_kind], ah          ; (a no-op unless a caller moved it)
    call vid_apply              ; republishes the chrome extent AND re-homes
    xor al, al                  ; the pointer onto display 0 - which is what
    call vid_ctx_capture        ; a shrinking desktop owes it
    call vid_desk_union         ; ...and BANK BEFORE UNION, the extend path's
    jmp short .out              ; order: the union reads display 0's record,
                                ; which is stale until the capture when a
                                ; vid_switch has just moved the primary

.extend:
    mov [vid_kind], al
    call vid_apply              ; the live block is the SECONDARY's now...
    call vid_setmode            ; ...which is what points this at the right
                                ; card, and what makes its 32KB/16KB clear
                                ; land on the right memory
    mov al, 1
    call vid_ctx_capture        ; bank it as display 1

    mov [vid_kind], ah          ; ...and back. No vid_setmode: the primary is
    call vid_apply              ; already in its mode with the splash on it
    call vid_equip              ; 40:10 back to the primary's KIND, because
                                ; vid_setmode's CGA path flipped it to colour
                                ; and nothing else ever would (SPEC.md 39.11.2)
    xor al, al
    call vid_ctx_capture        ; a no-op today - vid_apply has just republished
                                ; what vid_ctx_init banked - and 39.12's rule
                                ; followed mechanically rather than reasoned
                                ; about one instance at a time

    mov al, 1                   ; --- and WHERE display 1 sits (SPEC.md 39.19.2)
    call vid_ctx_ptr            ; The primary is at the origin either way, so
    xor ax, ax                  ; one of the two words is 0 and the other is
    mov [di+VID_CTX_VX], ax     ; the primary's own extent - which vid_apply
    mov [di+VID_CTX_VY], ax     ; has just republished into the live block
    cmp byte [vid_dlay], VID_L_BELOW
    je .below
    mov ax, [vid_cw]
    mov [di+VID_CTX_VX], ax
    mov word [di+VID_CTX_VY], MBAR_H  ; ...AND ITS TOP ROW IS THE DESKTOP'S,
                                      ; not the screen's (SPEC.md 39.19.3).
                                      ; The second monitor carries no menu
                                      ; bar, so aligning its row 0 with the
                                      ; primary's row 0 spends the first
                                      ; MBAR_H of it on a band no straddling
                                      ; window may enter - 20 of a CGA's 200
                                      ; rows, 10%. Aligning it with the
                                      ; DESKTOP BAND instead gives those rows
                                      ; back and costs nothing: the dead zone
                                      ; moves from below the card to above it
                                      ; and does not grow, mou_clamp is
                                      ; written against the union rather than
                                      ; against a rectangle (39.15.4), and
                                      ; ui_ylow already reads this very word
    jmp short .placed
.below:
    mov ax, [vid_ch]
    mov [di+VID_CTX_VY], ax
.placed:

    mov byte [vid_cur], 0       ; the live block is display 0's again
    mov byte [vid_ndisp], 2
    call vid_desk_union         ; ...and the DESKTOP is both of them now
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; vid_span_one - is this whole rect inside ONE display? (SPEC.md 39.14.6)
;
; in:       AX = x1, BX = y1, CX = x2, DX = y2, all VIRTUAL and inclusive
; out:      CF = 0 the rect lies wholly inside one display, CF = 1 it does not
;           (it crosses a seam, or reaches into the dead zone)
; clobbers: flags
;
; The question a WHOLE-SHAPE primitive has to ask before it treats itself as
; one shape. font_char never asks it - an 8x8 cell that crossed would be four
; pixels on the wrong card and the rule that a cell goes whole is worth more
; than that - but a RUN is up to a window wide and a BLIT up to a canvas, and
; for those the answer changes what they do.
;
; Resolving the top-left and testing the far corner against THAT display is
; the whole of it: displays do not overlap (SPEC.md 39.19.2 places them edge
; to edge), so a rect inside one of them is inside the one holding its origin.
; -----------------------------------------------------------------------------
vid_span_one:
    push ax
    push bx
    push si
    push di
    mov si, ax                  ; SI = x1; vid_disp_of ANSWERS in AL
    call vid_disp_of            ; AL = the display holding (x1, y1)...
    call vid_ctx_ptr            ; DI = its record
    mov ax, [di+VID_CTX_VX]
    cmp si, ax
    jb .no                      ; ...and these two are what catch the DEAD
    cmp bx, [di+VID_CTX_VY]     ; ZONE: vid_disp_of falls back to the primary
    jb .no                      ; for a point no display covers, and the
    add ax, [di+VID_CTX_CW]     ; origin then fails its own display's bounds
    cmp cx, ax
    jae .no                     ; x2 past this display's right edge
    mov ax, [di+VID_CTX_VY]
    add ax, [di+VID_CTX_CH]
    cmp dx, ax
    jae .no                     ; ...or y2 past its bottom
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; vid_disp_relayout - vid_disp_init, plus the two things a moved desktop owes
;                     the rest of the machine (SPEC.md 39.19)
;
; in:       [vid_dmode] / [vid_dlay], already set
; out:      the displays arranged, the drive column recounted and every window
;           clamped back onto a screen that exists
; clobbers: flags
;
; vid_switch does these three in this order as its steps 4 and 5, and did so
; before this existed; what needed a routine is the OTHER caller. Turning
; Extend off shrinks the desktop, and a window left on the display that has
; gone is a window off the right-hand edge of the screen with its title bar
; unreachable - the same failure wm_refit was written for when a 640x480
; machine switched to CGA (SPEC.md 39.11.2). desk_rowcalc is a no-op when only
; the arrangement moved, and is here so the pair cannot be split up.
;
; NOT called from kmain's vid_disp_init: wm_wins is .bss, -f bin zeroes
; nothing, and wm_init has not run yet - so wm_refit there would clamp records
; whose used bit is whatever the machine left in memory.
; -----------------------------------------------------------------------------
vid_disp_relayout:
    call vid_disp_init
    call desk_rowcalc
    call wm_refit
    ret

; -----------------------------------------------------------------------------
; vid_desk_union - the desktop spans every display (SPEC.md 39.16)
;
; in:       vid_ctx, [vid_ndisp], and vid_apply having just published the
;           PRIMARY's geometry
; out:      [vid_w]/[vid_h]/[vid_wm1]/[vid_hm1] cover the union
; clobbers: flags
;
; FOUR WORDS AND NOTHING ELSE, which is the whole reason 39.16 is cheap.
; vid_apply computed every derived CHROME word - the dock row, the clock cell,
; the drive column, the pull-down cap, and 39.16's own vid_pw/vid_ph pair -
; from the display it was applying, and vid_disp_init applies the primary
; LAST. So growing the desktop past them here leaves the strips measuring
; themselves against the monitor they are drawn on, with no special case
; anywhere.
;
; The union's ORIGIN is (0, 0) by construction: display 0 sits there and every
; other display is placed to its right, so a bounding box needs only the far
; corner.
; -----------------------------------------------------------------------------
vid_desk_union:
    push ax
    push cx
    push dx
    push si
    push di
    xor cx, cx                  ; CX/DX = the far corner, exclusive
    xor dx, dx
    xor si, si
.d:
    mov ax, si
    call vid_ctx_ptr
    mov ax, [di+VID_CTX_VX]
    add ax, [di+VID_CTX_CW]
    cmp ax, cx
    jle .w
    mov cx, ax
.w:
    mov ax, [di+VID_CTX_VY]
    add ax, [di+VID_CTX_CH]
    cmp ax, dx
    jle .h
    mov dx, ax
.h:
    inc si
    mov al, [vid_ndisp]
    mov ah, 0
    cmp si, ax
    jb .d
    mov [vid_w], cx
    dec cx
    mov [vid_wm1], cx
    mov [vid_h], dx
    dec dx
    mov [vid_hm1], dx
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

%endif  ; KERN_BIG
