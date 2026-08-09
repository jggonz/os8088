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

; .text and not .bss, like everything else that has to be right before the
; kernel has zeroed anything: -f bin zeroes nothing at all.
vid_avail   db 0                ; which adapters answered (VID_A_*). Probed
                                ; ONCE, at boot, and cached - vid_switch moves
                                ; [vid_kind], so re-deriving it later would
                                ; ask the question of the wrong machine

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
    ; fixed for the life of the machine, so [cp_nst] is written once too.
    mov al, [vid_avail]
    dec al
    and al, [vid_avail]         ; x & (x-1) == 0: exactly one bit set
    jnz .done
    dec byte [cp_nst]           ; the Display item is LAST, so the list simply
.done:                          ; stops one row short of it (SPEC.md 31.10.1)
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
;   1. The back buffer belongs to the OLD adapter. Its 150KB claim is sized
;      for four 640x480 planes and its flush targets that framebuffer, so it
;      is dropped while the old geometry still describes it. bb_init below
;      then re-answers whether the NEW adapter can have one at all - on mono
;      it cannot (SPEC.md 39.5), and leaving [bb_avail] set would let the
;      Buffer page offer a buffer that bb_set would refuse.
;
;   2. Spend the deferred cursor hide (SPEC.md 7.1.4) while the old geometry
;      still describes the save-under. gfx_lock only PROMISES the hide, so
;      under the Control Panel's lock the arrow is typically still on screen
;      with 24 bytes of background banked against the old stride. Change the
;      geometry first and gfx_unlock restores those bytes through the new
;      addressing, which smears the arrow across the screen permanently. It
;      is a no-op on the boot path, where no promise is outstanding.
;
;   3. vid_apply republishes all nine live words and everything derived from
;      them, and homes the mouse into the new screen - which is load-bearing
;      rather than tidy, for the reason in SPEC.md 39.2.
;
;   4. desk_rowcalc, because how many drive zones fit in a column is computed
;      from [vid_dock_y0] and is 7 on VGA, 4 on Hercules and 2 on CGA - and
;      wm_refit, because every window's frame was clamped onto the OLD screen
;      and a 640x480 window is off the bottom of a 640x200 one.
; -----------------------------------------------------------------------------
vid_switch:
    cmp al, [vid_kind]
    je .already
    call vid_avail_test
    jc .refuse

    push ax
    xor al, al                  ; 1. the buffer is the old adapter's
    call bb_set
    pop ax
    call cur_unlazy             ; 2. ...and so is the cursor's save-under
    call vid_blank              ; ...and the tube we are walking away from

    mov [vid_kind], al          ; 3. republish the geometry
    call vid_equip
    call vid_apply
    call bb_init
    call vid_setmode

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
%ifdef KERN_BIG

VID_NDISP_MAX equ 2             ; the machine this is for holds two cards
VID_CTX_W    equ 18             ; words copied to and from the live block
VID_CTX_CW   equ 14             ; ...within the run, the display's own extent -
VID_CTX_CH   equ 16             ; word 7 and word 8, vid_cw/vid_ch (39.2.1)
VID_CTX_VX   equ VID_CTX_W*2    ; ...then the display's origin in the VIRTUAL
VID_CTX_VY   equ VID_CTX_W*2+2  ; desktop, which is NOT copied anywhere
VID_CTX_SZ   equ VID_CTX_W*2+4

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
; CALLED AFTER ANYTHING THAT CHANGES THE LIVE GEOMETRY OUTSIDE A SWAP, and the
; list is short but not empty: vid_apply (a mode or adapter change) and bb_set,
; which writes [vid_rseg] when a back buffer is armed or dropped and is inside
; this run. Miss one and the display's record describes a geometry that has
; moved on - which shows up as drawing through the wrong segment the next time
; that display is activated, a long way from the cause.
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
    mov si, vid_seg
    mov cx, VID_CTX_W
    cld
    rep movsw
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
    push bx
    push cx
    push dx
    push di
    xor cl, cl                  ; CL = the display being tested
.next:
    mov al, cl
    call vid_ctx_ptr            ; DI = its record
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
    inc cl
    cmp cl, [vid_ndisp]
    jb .next
    xor cl, cl                  ; nowhere: the dead zone, and the primary is
.hit:                           ; where a shape goes to be clipped away
    mov al, cl
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; vid_disp_init - programme the SECOND card, if this machine has one
;                 (SPEC.md 39.13)
;
; in:       [vid_avail], and the machine running on [vid_kind]
; out:      a two-card machine has both cards scanning our raster, the second
;           one black; [vid_ndisp] = 2, [vid_cur] = 0, display 1 banked.
;           Anything else: untouched, [vid_ndisp] stays 1
; clobbers: flags
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
; STILL OWED: vid_switch does not re-run this, so switching the primary leaves
; these records describing the arrangement before the switch - and vid_blank
; darks a card display 1 now owns. Inert while nothing reads either, and it
; stops being inert the moment a primitive activates a display.
; -----------------------------------------------------------------------------
vid_disp_init:
    push ax
    push di
    mov al, [vid_avail]
    cmp al, VID_A_HERC | VID_A_CGA
    jne .out                    ; one display - or a pairing we do not take

    mov ah, [vid_kind]          ; bank the primary; the other kind is the one
    mov al, VID_CGA             ; this machine has and is not running on
    cmp ah, VID_HERC
    je .other
    mov al, VID_HERC
.other:
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

    mov al, 1                   ; display 1 sits to the RIGHT of display 0.
    call vid_ctx_ptr            ; Nothing reads this yet; where the two really
    mov ax, [vid_cw]            ; sit is the Control Panel's question, and this
    mov [di+VID_CTX_VX], ax     ; is the arrangement its default will be
    xor ax, ax
    mov [di+VID_CTX_VY], ax

    mov byte [vid_cur], 0       ; the live block is display 0's again
    mov byte [vid_ndisp], 2
.out:
    pop di
    pop ax
    ret

%endif  ; KERN_BIG
