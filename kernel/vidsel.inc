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
; already rejected it. Not every card does - MartyPC's Hercules decodes the
; whole 64KB whatever the bit says, and reported a CGA on a machine that has
; only an MDA, whose Activate button would then have set a mode on a monitor
; that is not there. So: write a byte at B000:8000 and look for it at
; B800:0000. Same byte means one card wearing two addresses.
;
; It costs nothing on the machine this is for. On a real 5150 with both cards
; and the page bit clear, the write at B000:8000 is decoded by nobody and
; vanishes, B800 still holds what vid_memchk restored, the two differ, and the
; CGA is found - which is the answer that machine needs.
; -----------------------------------------------------------------------------
vid_cga_alias:
    push ax
    push bx
    push cx
    push es
    mov ax, VID_HERC_SEG
    mov cx, 0xB800
    mov es, ax
    mov bh, [es:0x8000]         ; bank both bytes BEFORE either is written -
    mov es, cx                  ; aliased, they are the same byte and the two
    mov bl, [es:0x0000]         ; restores below simply agree
    mov es, ax
    mov byte [es:0x8000], 0x3C  ; a value neither vid_memchk pattern leaves
    mov es, cx
    cmp byte [es:0x0000], 0x3C
    mov [es:0x0000], bl         ; put both back - `mov` touches no flag, so the
    mov es, ax                  ; compare above still decides
    mov [es:0x8000], bh
    je .same
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
