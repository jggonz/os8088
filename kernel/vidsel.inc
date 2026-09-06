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
VID_A_EGA   equ 1 << VID_EGA     ; bit 3. An EGA machine is EGA + CGA (mode 6),
                                 ; never part of an extended desktop (39.24)

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
vid_hprobe  db 0                ; ...and HOW the mono card answered, because
                                ; "not detected" is three different faults and
                                ; the field cannot tell them apart from a
                                ; screenshot (SPEC.md 39.11.1.1). 0 = not
                                ; probed (it IS the running adapter), 1 = the
                                ; plain memory probe, 2 = only after the config
                                ; switch was written, 3 = refused both. It sits
                                ; here so 57.4's +4 pointer covers it with no
                                ; new word in the block

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
    dw vid_avail                ; +4  db avail, db hprobe (how the mono card
                                ;     answered - SPEC.md 39.11.1.1)
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

; =============================================================================
; THE ADAPTER PROBE TRIO, IN THE BOOT OVERLAY (SPEC.md 2.5.2,
; docs/LAST-DROP-BYTES.md row 2) - 262 contiguous bytes and the cleanest body
; in the tree to move: ONE inbound edge in the whole repository (kmain:4211),
; ZERO outbound, and no data and no `section` line anywhere in the span.
; vid_probe_avail calls vid_memchk three times and vid_cga_alias once and calls
; nothing else at all, so the two below it stay near-called and keep their near
; `ret`; only vid_probe_avail is entered from outside the blob and only it ends
; in `retf`.
;
; What this buys is 256 bytes of the 64KB segment - the scarce guard - that
; stop existing when kmain gives the blob back.
; =============================================================================
section .ovlw

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
; NEITHER PLANAR KIND IS PROBED AT ALL, because vid_detect already answered
; it: steps 1 and 2 of SPEC.md 39.1 run before the equipment word, so
; [vid_kind] is VID_VGA or VID_EGA on every machine with a VGA or EGA BIOS -
; which is what [vid_mono] = 0 above is asking, rather than the kind. And such
; a machine can always do CGA as well - mode 6 is a standard BIOS mode on
; every VGA and
; EGA ever made, and it is the same byte-exact 640x200 framebuffer this
; kernel's CGA path already drives (SPEC.md 39.9's VIDEO=cga has relied on
; that for as long as it has existed).
; -----------------------------------------------------------------------------
vid_probe_avail:
    push ax
    push cx
    push dx                     ; ...DX because the Hercules retry below
                                ; addresses the config switch
    mov cl, [vid_kind]          ; the adapter we are RUNNING on: available by
    mov al, 1                   ; definition, and the one we must not write to
    shl al, cl
    mov [vid_avail], al

    cmp byte [vid_mono], 0          ; ...and a VGA OR EGA does mode 6 too - it
    jne .herc                      ; is a standard BIOS mode on every one of
    or byte [vid_avail], VID_A_CGA  ; them, so the CGA bit is set unprobed
.herc:
    cmp byte [vid_kind], VID_HERC
    je .cga                     ; already marked, and its RAM is the screen
    mov ax, VID_HERC_SEG
    call vid_memchk
    mov byte [vid_hprobe], 1    ; ...and RECORD which answer this was, or the
    jnc .hyes                   ; field can only report that the card is
                                ; missing (SPEC.md 39.11.1.1)
    mov byte [vid_hprobe], 2
                                ; --- AND AGAIN WITH GRAPHICS ALLOWED --------
                                ; (SPEC.md 39.11.1.1). A Hercules whose config
                                ; switch has never been written is an MDA: 4KB
                                ; at B000, aliased upward, so the probe's 0xAA
                                ; at 0x1000 lands on its own 0x55 and the card
                                ; is rejected as the text-only part that
                                ; signature means. That is the RIGHT answer for
                                ; an MDA and the wrong one for this card, and
                                ; the two are indistinguishable until the
                                ; switch is written - which is the one thing
                                ; that can only be true of a Hercules, an MDA
                                ; having no 3BFh at all.
    mov dx, 0x3BF               ; bit 0 ONLY: graphics allowed, page 1 left
    mov al, 1                   ; disabled, because B8000 is where a CGA lives
    out dx, al                  ; and vid_setmode leaves it alone for the same
    mov ax, VID_HERC_SEG        ; reason (SPEC.md 39.6)
    call vid_memchk             ; ...preserves DX, so the switch is still
    jnc .hyes                   ; addressed below
    mov byte [vid_hprobe], 3
    xor al, al                  ; nothing answered: put the switch back as it
    out dx, al                  ; was found rather than leaving a card nobody
    jmp short .cga              ; has proved is there in a state nobody chose
.hyes:
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
    pop dx
    pop cx
    pop ax
    retf                        ; FAR: the trio's only entry from outside the
                                ; blob, reached by OVLGATE1 from kmain. The two
                                ; bodies around it are near-called from in here
                                ; and keep their near `ret`

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
section .text                   ; ...and the overlay's three end here

; -----------------------------------------------------------------------------
; vid_avail_test - may this machine be driven as AL?
; in:       AL = a VID_* kind
; out:      CF = 0 yes, CF = 1 no
; clobbers: flags
; -----------------------------------------------------------------------------
vid_avail_test:
    push ax
    push cx
    cmp al, VID_KIND_MAX + 1    ; THE RANGE CHECK ANSWERS IN CF ITSELF, and it
    cmc                         ; must: a byte from SYSTEM.CFG written by a
    jc .out                     ; kernel that knew a fifth adapter would
                                ; otherwise reach the ror below, and the 8086
                                ; does not mask a rotate count - kind 8 would
                                ; rotate by 9 and answer for kind 0. cmp sets
                                ; CF = 1 for AL < 4, i.e. IN range, so one cmc
                                ; makes CF = 1 mean "refuse" and the jump
                                ; carries it straight out (jc and pop touch no
                                ; flag). Setting CF before the compare would
                                ; not work - cmp overwrites it.
                                ; **THE BOUND IS VID_KIND_MAX AND NOTHING
                                ; HERE NAMES A KIND**, which is why admitting
                                ; VID_EGA (SPEC.md 39.24) was one edit in
                                ; viddet.inc and none in this routine
    mov cl, al
    inc cl                      ; ROR by n leaves bit n-1 of the original in
    mov al, [vid_avail]         ; CF, so n = kind+1 puts bit KIND there - which
    ror al, cl                  ; is what `test [vid_avail], 1 << kind` was.
    cmc                         ; CL is 1..4, never 0. Then invert to this
.out:                           ; routine's published polarity: CF = 0 is yes
    pop cx
    pop ax                      ; pop touches no flag
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
; TWO CALLERS, AND THE SECOND ONE RUNS AT BOOT. vid_switch is the obvious one;
; vid_disp_init calls it too, at the foot of its two-card arrangement, and
; kmain calls vid_disp_init. So on a VGA+Hercules machine booting with Extend
; on, this writes 40:10 bits 5:4 where vid_cga_equip deliberately leaves them
; alone - a claim in the BDA that the boot path was said not to make.
;
; The line above used to say "Only vid_switch calls it, so the boot path is
; untouched". vid_ctx_init a few hundred lines down carries a scar of exactly
; this shape with the polarity the other way round; this one DENIED a caller
; that exists, which is the harder half to notice, because the code reads
; consistent with the comment right up until you grep.
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
; vid_mono_text - put the REAL mono card into 80x25 text, when int 10h cannot
;                 (SPEC.md 39.20)
;
; in:       [vid_kind] is VID_HERC and the caller has already blanked 3B8h and
;           written 3BFh = 0
; out:      CF = 1 this card is now in text and the caller must NOT call
;                  int 10h; CF = 0 not this machine's problem, use the ROM
; clobbers: flags
;
; The one case is A VGA IN THE MACHINE. Everything else - a lone MDA, a lone
; Hercules, Hercules+CGA - has an int 10h owned by the motherboard ROM, whose
; mode 7 IS this card and whose mode set is equipment-driven, so vid_equip
; above already names it and this returns CF = 0 having touched nothing. That
; is why the shipping pairing cannot change behaviour here.
;
; IT IS A PRE-EXISTING BUG AND NOT THIS BRANCH'S, which is worth stating
; because it sits in a file the extended desktop is being built in. Reaching
; it needs no extended desktop at all: a VGA and a Hercules in one machine,
; the Display page (SPEC.md 31.10) used to make the Hercules primary, and then
; a reboot - all of which the shipped kernel does today. vid_dual_ok's refusal
; gates the DESKTOP, never vid_switch.
;
; What it does NOT do is update 40:49/40:63. Those describe the card int 10h
; drives, and on this machine that is the VGA - saying "mode 7 at 3B4" there
; would point the ROM at a card it has never heard of. vid_reboot below is
; where the other card is given something the BIOS can honestly print on.
; -----------------------------------------------------------------------------
vid_mono_text:
%ifndef GFX_VGA                 ; NO VGA IN THE BUILD, so [vid_avail]'s VGA
    clc                         ; bit can never be set (see vid_probe_avail:
    ret                         ; its only writer of that bit is `shl al, cl`
%else                           ; on [vid_kind], vid_detect's VGA/EGA probes
                                ; are themselves %ifdef GFX_VGA, VIDEO=vga is
                                ; a %error there (viddet.inc 51), and every
                                ; other [vid_kind] writer is inside
                                ; %ifdef KERN_BIG). The whole body below is
                                ; unreachable, and this IS the answer it gives.
    test byte [vid_avail], VID_A_VGA
    jnz .mine
    clc                         ; the ROM's mode 7 is this card: let it
    ret
.mine:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov si, vid_6845t           ; MDA 80x25, cursor off (viddet.inc)
    call vid_6845_prog
    mov ax, VID_HERC_SEG        ; and clear the page, which the ROM's mode set
    mov es, ax                  ; would have done: what is in there is a
    xor di, di                  ; 720x348 graphics desktop, and read as
    mov ax, 0x0720              ; characters it is noise. Space on attribute 7
    mov cx, 2000
    cld
    rep stosw
    mov dx, 0x3B8               ; high-res text, video ON, blink on - IBM's own
    mov al, 0x29                ; mode 7 value, and the last write for the same
    out dx, al                  ; reason vid_setmode's is (39.6): onto a page
    pop es                      ; that is already blank
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; vid_reboot - the text mode CMD_REBOOT wants, on every card (SPEC.md 39.20.1)
;
; in:       -
; out:      -
; clobbers: flags
;
; vid_text names the PRIMARY's card. int 19h is a bootstrap and not a POST -
; it re-reads the boot sector and jumps, resetting no video at all - so what
; prints next comes out of int 10h, and on a machine with a VGA in it int 10h
; is the VGA's whatever os8088 was using. Leaving that card in mode 12h means
; the next boot's messages are rendered as graphics over a stale desktop.
;
; So the OTHER card gets a mode too, and the equipment flag goes with it:
; vid_cga_equip is exactly the same manoeuvre viddet.inc already makes to
; reach a second card, DOS's own MODE CO80. The VGA's mode 3 cannot disturb
; the mono card - different aperture, different CRTC - and it runs AFTER
; vid_text on purpose, so that if some ROM does something surprising the last
; writes to the mono card are still ours.
;
; fsx (FSXM_TEXT80) deliberately calls vid_text and NOT this: inside a bracket
; the other display has been darked (39.18) and a mode set would light it with
; an empty text screen, which is the one thing that path exists to prevent.
; -----------------------------------------------------------------------------
vid_reboot:
    push ax
    call vid_text
%ifdef GFX_VGA                  ; without a VGA in the build the tail cannot
                                ; run: [vid_avail]'s VGA bit is never set, so
                                ; the test below always jumps - vid_text alone
                                ; is already the whole of this routine there
    cmp byte [vid_kind], VID_HERC   ; the mono card is the ONLY primary int 10h
    jne .out                    ; does not drive - and on a VGA machine the CGA
                                ; row IS the VGA doing mode 6, so vid_text has
                                ; already given that case its mode 3
    test byte [vid_avail], VID_A_VGA
    jz .out                     ; no VGA: int 10h drove the primary already
    call vid_cga_equip          ; 40:10 -> colour, or the ROM's mode set can
    mov ax, 0x0003              ; refuse on a machine flagged mono
    int 0x10
.out:
%endif
    pop ax
    ret

; =============================================================================
; THE BOOT STATUS LINE (SPEC.md 15.6) - ENTIRELY IN THE BOOT OVERLAY
;
; The bar says HOW FAR; this says WHAT. From kmain onward the remaining
; seconds of a boot are four distinguishable pieces of floppy work - the mount
; of A:, the read of SYSTEM.CFG, and one .DRV per driver the settings ask for
; - and on the field machine that is seconds of drive noise with nothing on
; the screen but a bar creeping through SPEC.md 15.3's SPL_POST notches. It
; reads as a hang. A line under the bar naming the file being read turns it
; into a machine that is visibly working.
;
; **EVERY BYTE OF THE CODE, THE STRINGS AND THE BUFFER IS IN `.ovl`**, which
; is what makes the feature free: the overlay rides stage 2's blob (SPEC.md
; 2.9.6), which stage 1 reads before it jumps and kmain gives back after
; spl_finish - so this is drawing code that exists for exactly as long as there
; is a loading screen to draw on, and then stops existing. The resident cost is
; 4 bytes (ovw_font_run_x) plus the two OVLCALL sites.
;
; **That is only possible since SPEC.md 2.5.1.** The overlay used to BE the FAT
; window, so the first mount ate it - and drv_boot's own mount is the very
; thing the first line here announces, so every driver message after it would
; have been a far call into somebody else's FAT. It outlives the mount now,
; which is exactly the lifetime this needs.
;
; Five things hold it up.
;
; **It is drawn when it CHANGES, and never on a tick.** spl_paint runs once
; per sector transferred (dsk_xfer, SPEC.md 15.3) and a 32-cell font_run is
; tens of milliseconds on a 4.77MHz machine against a sector's ~24 ms, so a
; line redrawn per frame would cost more than the load it is reporting on.
; spl_mline is therefore the composition buffer AND the record of what is on
; the glass - SPEC.md 12.9's menu_bcell discipline in miniature - so composing
; IS the diff. The whole boot draws it three times.
;
; **It is ONE opaque font_run, space-padded to SPL_MSG_CELLS.** The padding is
; the erase (SPEC.md 27.2), so a shorter message wipes a longer one in the
; same pass that letters it; there is no fill anywhere in this module and the
; line is never momentarily blank. It also puts SPEC.md 11.3's granularity
; rule out of reach - there is no erase-and-letter pair here to disagree.
;
; **The pen is byte-aligned**, floored to a cell by spl_mgeom, so both mono
; adapters take font_run's single-store path (SPEC.md 6.1). That is what makes
; a 32-cell line affordable on the machine it exists for - and the line draws
; on ALL THREE adapters, which the rest of the loading screen does not: 15's
; dialog, percentage and spinner are BIOS teletype and mode-12h chrome, and
; the bar was the only thing a Hercules or a CGA ever saw.
;
; **The overlay may not reach its own data through DS** (SPEC.md 2.5), so the
; buffer and the three strings below are read `cs:` - and the string the
; DRIVER supplies is a .text one read through DS, which is why there are two
; walkers (spl_mcs / spl_mds) rather than one.
;
; **No entry takes a pointer across the boundary.** A caller in .cold naming
; a label in this section would assemble and would be a cross-address-space
; reference os88ovlchk cannot see, so the two literal lines are zero-argument
; entries of their own and the only pointer that ever crosses is DS:SI, which
; is a kernel string by contract.
;
; The gfx lock is not held, exactly as spl_paint does not hold it: nothing
; else in the machine draws yet. The UI task does not exist, the cursor is not
; shown until after spl_finish, and [spl_live] refuses spl_mdraw the moment
; the desktop owns the screen.
; =============================================================================
section .ovl

SPL_MSG_CELLS equ 32            ; the reserved width, in 8px cells. 32 because
                                ; the longest line the tree can compose is
                                ; 'Loading Driver 1/5 (Hard Drive)' = 31, and
                                ; 32 cells is 256px - byte-aligned arithmetic
                                ; on every adapter this runs on
SPL_MSG_Y     equ 288           ; VGA: under the bar trough (rows 262..281) and
                                ; above spl_pct's text row 19 (y 304). Mono
                                ; computes its own from the live geometry

; -----------------------------------------------------------------------------
; The three drv_boot reaches, and the one spl_reset does. Every one of them
; preserves every register and the flags.
;
; **THE FIRST THREE ARE NEAR NOW** (SPEC.md 2.5.2): drv_boot_x moved into this
; same section, and it is their only caller, so what used to be a 20-byte
; OVLCALL through spl_fp is three bytes of near call in one address space and
; the `retf` they answered it with is a `ret`. That collapse is most of why
; docs/LAST-DROP-BYTES.md row 1 is the only row in the register that returns
; more than it spends. ovl_spl_redraw below keeps its `retf` - spl_reset is in
; `.boot2` and reaches it through OVLCALL still.
; -----------------------------------------------------------------------------
ovl_spl_msg_cfg:                ; 'Loading SYSTEM.CFG'
    push si
    mov si, spl_s_cfg
    call spl_msg_cs
    pop si
    ret

ovl_spl_msg_boot:               ; 'Starting os8088'
    push si
    mov si, spl_s_boot
    call spl_msg_cs
    pop si
    ret

; ...and the two kmain reaches, for the phases that are NOT disk and are the
; longest things on a hard-disk boot's bar (SPEC.md 15.6.4). Each is composed
; AFTER the notch that precedes it, never before: spl_mdraw refuses while
; [spl_live] is 0 and spl_mline is the record of what is on the glass, so a
; line composed before the screen exists is composed, never drawn, and never
; redrawn.
ovl_spl_msg_mouse:              ; 'Looking for Mouse' - SPEC.md 9.4.1's
    push si                     ; identify window: 596 ms with a mouse on the
    mov si, spl_s_mouse         ; other end and 1,208 ms without, measured at
    call spl_msg_cs             ; 1.23 s on the hard-disk boot this names
    pop si
    retf

ovl_spl_msg_fdd:                ; 'Looking for Drives' - SPEC.md 18.97's TRACK
    push si                     ; 0 question, FDD_MOTORW + FDD_SEEKW = 32 ticks
    mov si, spl_s_fdd           ; whenever the answer is heading for absent,
    call spl_msg_cs             ; measured at 1.80 s against 1,758 ms predicted
    pop si
    retf

ovl_spl_msg_drv:                ; AL = ordinal, AH = total, DS:SI = DRVR_TITLE
    call spl_msg_drv            ; ...and near too - see above
    ret

ovl_spl_redraw:                 ; put the composed line back after a mode set
    call spl_mdraw
    retf

; -----------------------------------------------------------------------------
; spl_msg_cs - compose a whole literal line, from a string in THIS section
; in:  CS:SI -> ASCIIZ
; -----------------------------------------------------------------------------
spl_msg_cs:
    pushf
    push ax
    push si
    push di
    call spl_mbegin
    call spl_mcs
    call spl_mend
    pop di
    pop si
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; spl_msg_drv - 'Loading Driver n/N (Title)'
; in:  AL = the 1-based ordinal, AH = how many are being loaded,
;      DS:SI -> the row's DRVR_TITLE string
; -----------------------------------------------------------------------------
spl_msg_drv:
    pushf
    push ax
    push bx
    push si
    push di
    mov bx, ax                  ; BL = ordinal, BH = total - AL is the cell
    call spl_mbegin
    push si
    mov si, spl_s_drv
    call spl_mcs
    pop si
    mov al, bl
    call spl_mnum
    mov al, '/'
    call spl_mput
    mov al, bh
    call spl_mnum
    mov al, ' '
    call spl_mput
    mov al, '('
    call spl_mput
    call spl_mds                ; the title, through DS - it is a .text string
    mov al, ')'
    call spl_mput
    call spl_mend
    pop di
    pop si
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; The composer. DI walks spl_mline and every store goes through spl_mput,
; which compares before it writes: at spl_mend [spl_mdirty] says whether one
; single cell differs from what is on the glass. The BOUND lives in spl_mput
; rather than in its callers, so a line that overruns truncates at the
; reserved width instead of writing past the buffer, and nothing above has to
; carry a length.
; -----------------------------------------------------------------------------
spl_mbegin:
    mov di, spl_mline
    mov byte [cs:spl_mdirty], 0
    ret

spl_mput:                       ; AL -> the line at DI
    cmp di, spl_mline + SPL_MSG_CELLS
    jae .out                    ; full: swallow the rest of the line
    cmp al, [cs:di]
    je .adv
    mov [cs:di], al
    mov byte [cs:spl_mdirty], 1
.adv:
    inc di
.out:
    ret

spl_mcs:                        ; CS:SI ASCIIZ -> the line (one of ours)
    push ax
.next:
    mov al, [cs:si]
    inc si
    or al, al
    jz .out
    call spl_mput
    jmp short .next
.out:
    pop ax
    ret

spl_mds:                        ; DS:SI ASCIIZ -> the line (a kernel string)
    push ax
.next:
    lodsb
    or al, al
    jz .out
    call spl_mput
    jmp short .next
.out:
    pop ax
    ret

spl_mnum:                       ; AL = 0..99 -> the line, no leading zero
    push ax
    push dx
    aam                         ; AH = AL/10, AL = AL%10 - the same conversion
    mov dl, al                  ; clk_put2 (clock.inc) does, in two bytes and
    mov al, ah                  ; with no divisor register to bank. 0..99 IS
                                ; aam's domain, and this routine's input is
                                ; documented as exactly that
    or al, al
    jz .units
    add al, '0'
    call spl_mput
.units:
    mov al, dl
    add al, '0'
    call spl_mput
    pop dx
    pop ax
    ret

spl_mend:
    mov al, ' '                 ; the padding IS the erase - see the header
.pad:
    cmp di, spl_mline + SPL_MSG_CELLS
    jae .padded
    call spl_mput
    jmp short .pad
.padded:
    cmp byte [cs:spl_mdirty], 0
    je .out                     ; the same line as last time: no pixels at all
    call spl_mdraw
.out:
    ret

; -----------------------------------------------------------------------------
; spl_mdraw - put spl_mline on the glass, wherever the screen currently is
; out: nothing (every register and the flags preserved)
;
; [spl_live] is the guard, spl_reset's exactly: after spl_finish the splash
; owns nothing and a status line over the desktop would be a bug. It is a
; .text byte, so DS reaches it (SPEC.md 2.5) while the line beside it needs
; ES:CS - which is why this calls font_run_x and not font_run.
; -----------------------------------------------------------------------------
spl_mdraw:
    pushf
    cmp byte [spl_live], 0
    je .out
    push ax
    push cx
    push dx
    push si
    push es
    inc byte [spl_busy]         ; SPEC.md 15.3.8.3's lock: font_run programs the
                                ; write mode and the Map Mask, and an animation
                                ; frame arriving from IRQ0 in the middle of that
                                ; draws its box through somebody else's state
    call spl_mgeom              ; CX = x, DX = y
    mov ax, cs                  ; the line lives in THIS section, and
    mov es, ax                  ; font_run_x is the ES:SI entry
    mov si, spl_mline
    mov ax, (CBLACK << 8) | CWHITE
    call KERNEL_SEG:ovw_font_run_x  ; ONE opaque run: ink white, ground black,
                                ; which is the loading screen on all three
                                ; adapters whatever the theme says about a
                                ; desktop that has not been drawn yet
    dec byte [spl_busy]
    pop es
    pop si
    pop dx
    pop cx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; spl_mgeom - where the line goes on THIS screen
; out: CX = x (byte-aligned), DX = y
; clobbers: cx, dx, flags
;
; Read live, never banked: drv_boot can switch the adapter out from under a
; line that is already up (SPEC.md 39.11.3), and spl_reset then redraws it
; through here at the new geometry.
; -----------------------------------------------------------------------------
spl_mgeom:
    mov cx, [vid_cw]            ; the ACTIVE display's extent, as the bar uses
    sub cx, SPL_MSG_CELLS * 8
    shr cx, 1
    and cl, 0xF8                ; ...floored to a cell, or the mono adapters
                                ; lose font_run's single-store path (SPEC.md
                                ; 6.1) for a line 32 cells wide
    mov dx, [cs:spl_l_msg]      ; ...and the ROW that splash.inc banked when it
    ret                         ; laid the screen out (SPEC.md 15.3.5.1). Both
                                ; halves of the blob are one segment, so this
                                ; is a `cs:` away - and it is a BACKWARD
                                ; reference, splash.inc being %included above
                                ; this file, which is why the CELL COUNT below
                                ; is still a second copy with an assertion on
                                ; it and this is not

spl_s_drv:  db 'Loading Driver ', 0
spl_s_cfg:  db 'Loading SYSTEM.CFG', 0
spl_s_mouse: db 'Looking for Mouse', 0      ; 17 and 18 cells - both inside
spl_s_fdd:  db 'Looking for Drives', 0      ; SPL_MSG_CELLS, so neither is
                                            ; truncated by spl_mput's bound
spl_s_boot: db 'Starting os8088', 0

spl_mdirty: db 0                ; ...and the line itself. Both `cs:`, both in
; ...and the two constants splash.inc had to carry a second copy of, because
; `.boot2` is %included ABOVE this file and stage 2 draws into the same field
; (SPEC.md 15.6.5). Asserted rather than commented: a geometry that must agree
; and is written twice is what a build check is for.
%if SPL_KCELLS != SPL_MSG_CELLS
%error "splash.inc's SPL_KCELLS and vidsel.inc's SPL_MSG_CELLS have parted"
%endif
%if SPL_KY != SPL_MSG_Y
%error "splash.inc's SPL_KY and vidsel.inc's SPL_MSG_Y have parted"
%endif

spl_mline:  times SPL_MSG_CELLS + 1 db 0   ; the overlay, both gone with it.
                                ; The trailing NUL is never written by anything
                                ; and is what terminates the run
section .text

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
;
; IT IS THE TEST AND NOTHING ELSE NOW - vid_blank_kind does the darking
; (39.18.1), and this routine had its own copy of it for as long as that one
; was behind the dual-display fence. Two defects came with the copy and both
; were live, which is what a second implementation of one operation costs:
;
;   * THE HERCULES ARM WROTE 3B8h = 0, which clears bit 1 - the GRAPHICS bit -
;     along with bit 3, so the card was not blanked but put into MDA TEXT mode
;     with a 6845 still carrying 720x348 timings. vid_blank_kind's header
;     records the fix (0x02, graphics + page 0 + video off) and the measurement
;     that found it; the fix was never brought back here.
;   * THE COLOUR ARM WROTE 3D8h UNCONDITIONALLY, a register a VGA does not
;     implement, so the OUT was swallowed by the bus. That was accepted while
;     "a VGA beside a Hercules" was believed to be a pairing nobody built - it
;     is in fact the one the period built most (docs/DUAL-DISPLAY-VGA.md 1.3) -
;     and on such a machine switching to the Hercules left the VGA lit with a
;     frozen desktop on it. vid_blank_kind has a real VGA arm (SR01 bit 5).
;
; So the question this answers is "are these two kinds two CARDS", and the
; answer to "how is a card darked" belongs in one place.
;
; THE KINDS ARE A CLOSED SET AND THIS TEST RESTS ON IT. "Exactly one of them
; is the mono one" is decided by comparing against VID_HERC BY NAME, here and
; in vid_blank_kind and vid_unblank_kind (39.18.1) - so a NEW kind is silently
; colour to all three, and vid_avail_test would admit it on the strength of one
; `cmp al, VID_KIND_MAX`.
;
; **THE BIT-0 FORM IS GONE, AND VID_EGA IS WHY (SPEC.md 39.24).** This read
; `xor al, ah / test al, 1`, on the fact that VID_VGA=0, VID_HERC=1, VID_CGA=2
; made bit 0 of a kind exactly "is it the mono card" - so f(a) XOR f(b) was one
; xor and one test. VID_EGA is 3. It is ODD and it is COLOUR, and no single bit
; separates {VID_HERC} from {VID_VGA, VID_CGA, VID_EGA}, so the trick does not
; extend - it inverts, in both directions at once. An EGA -> CGA switch would
; have read "exactly one is mono" and darked the outgoing card, which on an EGA
; IS the card the next three lines programme (39.11.1: the CGA row is that
; planar card doing mode 6) - the dark screen this routine's own header warns
; about. An EGA -> Hercules switch, two real cards, would have read "neither"
; and darked nothing. The %if below caught it at assembly time when
; VID_KIND_MAX rose, which is what it was put there for.
;
; The explicit compares cost 7 bytes over the xor. They are not optional and
; they are not a place to be clever again: the predicate is "is this kind the
; mono card", the mono kinds are a SET, and a set of one is the only thing a
; single bit can ever encode.
; -----------------------------------------------------------------------------
%if VID_KIND_MAX != VID_EGA
  %error "vid_blank, vid_blank_kind and vid_unblank_kind decide WHICH CARD by comparing against VID_HERC (and VID_VGA/VID_EGA) BY NAME and treating everything else as the CGA (SPEC.md 39.18.1), and vid_avail_test's `cmp al, VID_KIND_MAX` is the range that rests on it. A FIFTH kind is silently a CGA to all three. Give it an arm in each of them before raising VID_KIND_MAX"
%endif
vid_blank:
    push ax
    mov ah, [vid_kind]          ; the kind being LEFT
    cmp al, ah                  ; the same card either way - nothing to dark,
    je .out                     ; and this is also what stops mono -> mono
                                ; reaching the compares below as "one of them"
    cmp al, VID_HERC            ; two different kinds: dark the outgoing card
    je .dark                    ; only when EXACTLY ONE of them is the mono
    cmp ah, VID_HERC            ; one. Neither being mono means one card in two
    jne .out                    ; modes (39.11.1) and darking it is the bug
.dark:
    mov al, ah                  ; dark the card being LEFT
    call vid_blank_kind
.out:
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

    call cur_unlazy              ; 1. the cursor's save-under is the old
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
%ifdef KERN_BIG
    push ax                     ; ...and the THEME, which can be a fact about
    mov al, [thm_kind]          ; the adapter: SPEC.md 76.12's Color theme is
    call thm_set                ; VGA's alone, and this page can move a running
    pop ax                      ; machine onto a card with no colour in it.
                                ; thm_set owns the refusal, so re-asserting the
                                ; kind is the whole of it - Bright and Dark
                                ; re-resolve to what they already were, and
                                ; Color falls back rather than reducing to a
                                ; dither nobody chose (SPEC.md 76.12.1). It
                                ; also re-forces the two painters, which the
                                ; pair below wanted anyway
%endif
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
    retf
.refuse:
    stc
    retf

; =============================================================================
; The per-display context (SPEC.md 39.12) - kern_big only
;
; STEP 1 of docs/DUAL-DISPLAY-PLAN.md 11: the state and the swap, with
; [vid_ndisp] = 1 and nothing on a drawing path calling any of it. So the
; screen is byte-identical to the build before it, on all three adapters, by
; construction rather than by inspection - which is the gate this step is
; allowed to land against.
;
; WHAT A DISPLAY OWNS is the sixteen contiguous words `vid_seg` through
; `vid_tseg`: the nine live geometry words, the six derived ones the RENDERER
; reads, and font_run's composed-row target (viddet.inc). What it does NOT own
; starts at `vid_dock_y0` -
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
    push dx                     ; DX FIRST, and the bit goes STRAIGHT into DH:
    xor dh, dh                  ; AH IS LIVE ACROSS THIS CALL at
    jmp short vid_bk            ; vid_disp_init's SINGLE arm, which banks the
                                ; primary's kind there and stores it back
                                ; three instructions later. Writing AH here
                                ; and pushing afterwards puts VID_VGA in
                                ; [vid_kind] on a machine with no VGA and
                                ; boots it to a dead monitor
vid_unblank_kind:
    push dx
    mov dh, 8                   ; 0 dark, 8 lit - and ONE BODY: the two differ
                                ; in this byte and in nothing else, and the
                                ; three pushes, the two-compare dispatch, the
                                ; CGA preamble and the epilogue were
                                ; duplicated exactly, which this file's own
                                ; header says should not be
vid_bk:
    push ax
    push es                     ; DH survives the CGA arm's `mov ax, 0x0040`,
                                ; which is why the bit lives there. NOTHING
                                ; BELOW MAY WRITE DH BEFORE THE ARM THAT READS
                                ; IT - the mono arm's `mov dx, 0x3BF` does,
                                ; which is why it tests DH first
    cmp al, VID_HERC
    je .mono
%ifdef GFX_VGA                  ; no planar adapter in the build: [vid_kind]
    cmp al, VID_EGA             ; is never VID_VGA or VID_EGA and vid_disp_set
    je .ega                     ; is inside %ifdef KERN_BIG, so no caller can
    cmp al, VID_VGA             ; pass either. EGA has NEITHER of the two
    je .vga                     ; enable bits - SR01 bit 5 and the Attribute
%endif                          ; Controller's PAS are both VGA additions -
    mov ax, 0x0040              ; and it does not decode 3D8h either: its
                                ; blank is its PALETTE (vid_ac_pal below)
    mov es, ax
    mov al, [es:0x0065]         ; the BIOS's shadow of 3D8h...
    and al, ~8                  ; ...minus the video-enable bit...
    or al, dh                   ; ...plus the one this direction wants. The
                                ; unblank FORCES the bit where it used to
                                ; write the shadow unchanged: identical on
                                ; every machine whose ROM mode set left the
                                ; bit up, and where they differ the old form
                                ; left the screen dark for ever (SPEC.md
                                ; 39.18.1)
    mov dx, 0x3D8
    out dx, al
    jmp short .out
.mono:
    or dh, dh                   ; the two directions are NOT symmetric here
    mov al, 0x02                ; and the merge keeps both exactly: `mov`
    jz .m8                      ; touches no flag, so blanking writes 3B8h
                                ; alone - graphics + page 0, video OFF, byte
                                ; for byte what vid_setmode's blank-first
                                ; sequence writes (SPEC.md 39.6) - and leaves
                                ; 3BFh alone, because the graphics LOCK is a
                                ; second piece of state the unblank would then
                                ; owe and the gated signal makes it pointless
    mov dx, 0x3BF               ; unblanking: allow graphics FIRST (the order
    mov al, 1                   ; vid_setmode uses), then video on
    out dx, al
    mov al, 0x0A
.m8:
    mov dx, 0x3B8
    out dx, al
    jmp short .out
%ifdef GFX_VGA
.ega:
    mov al, dh                  ; EGA HAS NEITHER ENABLE BIT and its blank is
    call vid_ac_pal             ; its palette (SPEC.md 39.24). vid_ac_pal reads
    jmp short .out              ; its argument as ZERO / NONZERO, so this
                                ; body's own direction byte - 0 dark, 8 lit -
                                ; is already the argument and needs no
                                ; translation into main's 0/1
.vga:
    mov al, dh                  ; SR01 bit 5 is Screen OFF, so the polarity
    xor al, 8                   ; is inverted first: dark (DH=0) -> 8 -> 0x20,
    shl al, 1                   ; lit (DH=8) -> 0 -> 0. shl by 1 is the
    shl al, 1                   ; 8086-legal form
    call vid_seq1_blank
%endif
.out:
    pop es
    pop ax
    pop dx
    ret

%ifdef GFX_VGA                  ; its only two callers are the .vga arms above,
                                ; both gated for the same reason
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
%endif                          ; ...end of vid_seq1_blank

; -----------------------------------------------------------------------------
; vid_ac_pal - internal: the EGA's blank, which is its PALETTE
;
; in:       AL = 0 to dark the screen (all sixteen Attribute Controller
;           palette registers to black), nonzero to put mode 10h's own
;           sixteen back
; out:      -
; clobbers: flags (AL is consumed)
;
; AN EGA HAS NEITHER ENABLE BIT. Sequencer register 1 bit 5 (Screen Off) and
; the Attribute Controller index's bit 5 (Palette Address Source) are BOTH
; VGA additions - on the IBM EGA, SR01 is the dot clock and the character
; width with bits 5-7 reserved, and the AC index is bits 4-0 with 7-5 unused.
; Writing either on an EGA blanks nothing: the bit lands in a reserved field
; and the tube stays lit. So the card's video signal cannot be gated at all,
; and what CAN be done is to make every colour it is showing black.
;
; That keeps the property the whole feature rests on and that the other three
; arms have (see vid_blank's header): the CRTC is untouched, both syncs keep
; coming out, the tube never re-acquires, and what the card holds in memory is
; not disturbed - so a background task may go on drawing into a blanked EGA
; exactly as it may into a blanked VGA.
;
; THE FLIP-FLOP IS RESET PER PAIR, not once in front of sixteen. 3C0h is one
; port for both the index and the data and which one a write lands in is a
; LATCH, not a state re-selected by each access the way the Sequencer's index
; is; sixteen unguarded pairs are sixteen chances to be one out, and being one
; out writes a palette entry where an index belongs. The routine also leaves
; the latch in the INDEX phase, because the phase outlives the call and
; fsx.inc hands a package the raw hardware between these two routines.
;
; EVERY INDEX WRITE CARRIES BIT 5, and leaving it out is a black screen that
; never comes back. On the EGA bits 7-5 of the index are unused and it is a
; don't-care - but this same code runs on a VGA under `make test VIDEO=ega`
; (SPEC.md 39.9), and there bit 5 IS Palette Address Source: an index written
; with it clear means "the host is loading the palette", which DISABLES video
; output. Sixteen index writes of a bare 0..15 therefore blank the screen as a
; side effect of addressing it, and the restoring pass blanks it again while
; putting the colours back - the palette ends up right and the tube stays
; dark. Found on the glass, which is the only place it shows: the blank looks
; correct and it is the unblank that never returns.
;
; IT RUNS WITH IF=0, for vid_seq1_blank's reason one screen up: the read and
; the write are a pair with a reachable gap, and this one is thirty-two
; accesses of it, off ui_task's idle blanker, pre-emptible throughout.
; -----------------------------------------------------------------------------
vid_ac_pal:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, al                  ; 0 = black, nonzero = the defaults
    mov si, vid_ac_dflt
    xor cl, cl                  ; CL = the palette register, 0..15
    pushf
    cli
.n:
    mov dx, 0x3DA               ; Input Status 1: latch to INDEX...
    in al, dx
    mov dx, 0x3C0
    mov al, cl                  ; ...the register, WITH BIT 5 SET...
    or al, 0x20                 ; (see below)
    out dx, al
    xor al, al
    or bl, bl
    jz .wr
    mov al, [si]
.wr:
    out dx, al                  ; ...and what it is to show
    inc si
    inc cl
    cmp cl, 16
    jb .n
    mov dx, 0x3DA               ; leave the latch where every reader assumes
    in al, dx                   ; it, INDEX, and not mid-pair
    popf
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Mode 10h's sixteen, which is what the BIOS mode set left and therefore what
; an unblank owes: the standard EGA/VGA 16-colour default, with 6 mapped to
; 14h so that colour 6 is BROWN rather than dark yellow, and 8..15 in the
; second intensity group at 38h..3Fh. The kernel never programs these itself
; (SPEC.md 39.4 works in the sixteen indices, not in the palette behind
; them), so the BIOS default IS the live palette and restoring it is exact.
vid_ac_dflt:
    db 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07
    db 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F

%ifdef KERN_BIG

VID_NDISP_MAX equ 2             ; the machine this is for holds two cards
%if VID_NDISP_MAX != 2
  %error "the extended desktop is written for TWO displays and no more (SPEC.md 39.12): vid_ctx is sized from this, vid_disp_set stores the count as a literal 2, and wm.inc's snapshot band appends 'at most VID_NDISP_MAX' clip rects so WM_CLIP_MAX's full case is unreachable rather than handled. A third display needs each of those looked at, not this number raised"
%endif
VID_CTX_W    equ 16             ; words copied to and from the live block
VID_CTX_CW   equ 14             ; ...within the run, the display's own extent -
VID_CTX_CH   equ 16             ; word 7 and word 8, vid_cw/vid_ch (39.2.1)
VID_CTX_VX   equ VID_CTX_W*2    ; ...then the display's origin in the VIRTUAL
VID_CTX_VY   equ VID_CTX_W*2+2  ; desktop, which is NOT copied anywhere
VID_CTX_KIND equ VID_CTX_W*2+4  ; ...and which ADAPTER it is, because SPEC.md
                                ; 39.18 has to put [vid_kind] where the live
                                ; geometry is and a segment is not a kind
VID_CTX_SZ   equ VID_CTX_W*2+5

; ...and those two offsets are a SECOND opinion about viddet.inc's column
; order, so they are asserted rather than trusted, exactly as the run's length
; is above. vid_tab's rows and the live block agree by construction; this file
; is the only place that indexes into the middle of the copy.
%if vid_cw - vid_seg != VID_CTX_CW || vid_ch - vid_seg != VID_CTX_CH
%error "vid_cw/vid_ch are not at VID_CTX_CW/CH in the run - see viddet.inc"
%endif

; The live run this copies must be the run viddet.inc lays out, and a reorder
; there would be silent here - so it is asserted rather than trusted.
%if vid_tseg - vid_seg != (VID_CTX_W-1)*2
%error "vid_seg..vid_tseg is not VID_CTX_W contiguous words - see viddet.inc"
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
                                ; OUTSIDE the sixteen-word run on purpose:
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
; vid_kind_of - internal: WHICH ADAPTER IS DISPLAY N? (SPEC.md 39.18)
; in:       AL = display index (vid_ctx_ptr's convention)
; out:      AL = its VID_* kind
; clobbers: AX (an output) and the flags.  DI IS BANKED, which is the whole
;           difference from `call vid_ctx_ptr / mov al, [di+VID_CTX_KIND]`:
;           eight sites wrote that pair out and five of them had to bank DI
;           round it themselves.
;
; Inside this file's `%ifdef KERN_BIG` because vid_ctx_ptr is - on kern_small
; there is one display and [vid_kind] IS the answer, which is what every one
; of the eight sites' `%else` arm already says.
; -----------------------------------------------------------------------------
vid_kind_of:
    push di
    call vid_ctx_ptr
    mov al, [di+VID_CTX_KIND]
    pop di
    ret

; -----------------------------------------------------------------------------
; vid_ctx_rect - internal: a display's INCLUSIVE virtual rect (SPEC.md 39.14)
; in:       AL = display index (vid_ctx_ptr's own convention)
; out:      AX/BX = the top-left, CX/DX = the bottom-right, all VIRTUAL and
;           both ends inclusive; DI = the context record
; clobbers: the above, and the flags
;
; Written out twice before this existed - vid_disp_desk below and wm.inc's
; wm_dmg_bands, the same nine instructions in the same order, found as one
; hash.  Both are inside this file's `%ifdef KERN_BIG`, which is where the
; whole multi-display path lives, so this must be too: on kern_small the
; second display does not exist and neither does vid_ctx_ptr.
; -----------------------------------------------------------------------------
vid_ctx_rect:
    call vid_ctx_ptr            ; DI = the record (AX preserved)
    mov ax, [di+VID_CTX_VX]
    mov cx, ax
    add cx, [di+VID_CTX_CW]
    dec cx                      ; inclusive: a display CW wide ends at CW-1
    mov bx, [di+VID_CTX_VY]
    mov dx, bx
    add dx, [di+VID_CTX_CH]
    dec dx
    ret

; -----------------------------------------------------------------------------
; vid_ctx_ptr - internal: DI = the record for display AL
; in:       AL = display index (0..VID_NDISP_MAX-1)
; out:      DI = record address
; clobbers: DI, flags. AX is preserved, because every caller still needs it.
; -----------------------------------------------------------------------------
vid_ctx_ptr:
    mov di, vid_ctx             ; TWO displays (the %if at VID_NDISP_MAX), so
    or al, al                   ; the multiply is a branch: the index is 0 or 1
    jz .out                     ; by construction and every caller passes a
    add di, VID_CTX_SZ          ; literal or a value already bounded by
.out:                           ; [vid_ndisp]. That deletes `mul dl` - 70-77
    ret                         ; clocks on an 8088 - plus two pushes and two
                                ; pops, ~100 clocks, from a routine the mouse
                                ; ISR reaches through cur_move_multi and every
                                ; font_run reaches through vid_span_one. AX and
                                ; DX are still untouched, so the contract is
                                ; unchanged

; -----------------------------------------------------------------------------
; vid_ctx_pre - internal: the set-up vid_ctx_capture and vid_ctx_act share
;
; in:       AL = display index
; out:      DS = ES = KERNEL_SEG, DI = the record, CX = VID_CTX_W, DF clear
; clobbers: CX, DI, DS, ES, flags
;
; In .text beside its two callers on purpose: a near call across a section
; boundary assembles and runs wrong, and both callers are here.
; -----------------------------------------------------------------------------
vid_ctx_pre:
    mov cx, KERNEL_SEG          ; both ends are ours; the caller's ES is not
    mov ds, cx
    mov es, cx
    call vid_ctx_ptr            ; DI = the record. It reads no memory, so
    mov cx, VID_CTX_W           ; calling it after DS is loaded is what both
    cld                         ; callers did anyway
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
    call vid_ctx_pre            ; DS = ES = KERNEL_SEG, DI = the record,
    push di                     ; CX = VID_CTX_W, DF clear
    mov si, vid_seg
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
    call vid_ctx_pre            ; DS = ES = KERNEL_SEG, DI = the record,
    mov si, di                  ; CX = VID_CTX_W, DF clear - and the record is
    mov di, vid_seg             ; the SOURCE this way round
    rep movsw
    sub si, VID_CTX_W*2         ; ...and rep movsw left SI exactly that far
    mov di, si                  ; PAST the record it read, so the pointer is
                                ; recovered rather than derived a second time
    mov [vid_cur], al           ; ...published here, because AL is about to
                                ; become the record's KIND below and nothing
                                ; between reads [vid_cur]
                                ; ...and republish the origin every hook reads
    mov ax, [di+VID_CTX_VX]
    mov [vid_ox], ax
    mov ax, [di+VID_CTX_VY]
    mov [vid_oy], ax
    mov al, [di+VID_CTX_KIND]   ; ...and HOW DEEP this display is, which is not
    call vid_depth_set          ; in the sixteen-word run and is what every
                                ; primitive dispatches on. Two 1bpp displays
                                ; agree about it and a VGA beside a Hercules
                                ; does not - see vid_depth_set for what the
                                ; omission measured as

    ; ...and SPEC.md 39.3.1's ROW TABLE, which is the same omission again and
    ; the one the run can never fix: [vid_rowmax] is in .text and vid_rowtab is
    ; a single array in .lowbss, so no rep movsw reaches either - while
    ; vid_stride, vid_bmask and vid_bshift, the three inputs the table baked
    ; in, are all INSIDE the run and have just changed. vid_dual_ok takes only
    ; a mono+colour pairing, so the two displays never agree about a row: at
    ; y=5 Hercules answers 0x205A and CGA 0x20A0, and every row but 0 and 1
    ; differs. VID_ROWTAB is 348 - every row of both 1bpp adapters - so the
    ; "row past the end" miss path does not rescue it either, and the secondary
    ; would be drawn through the PRIMARY's addressing: shredded, not
    ; misaligned.
    ;
    ; Rebuilding it here is 23.6 ms (SPEC.md 39.3.1) under a drawing primitive
    ; whose floor is 756 us, and under the CURSOR. So the table is switched OFF
    ; instead: the display it was built for keeps the fast path and the other
    ; one computes, which is what every row cost before the table existed.
    ;
    ; [vid_kind] IS the table's kind, by construction and not by a stamp:
    ; vid_apply is the only routine that builds the table and it builds for
    ; [vid_kind], and this routine deliberately does not move [vid_kind] - the
    ; record's kind goes to vid_depth_set and nowhere else. AL still holds it.
    xor di, di                  ; the record is finished with
    cmp al, [vid_kind]
    jne .norow
    mov di, VID_ROWTAB
.norow:
    mov [vid_rowmax], di
    pop es                      ; the pops are HERE, not above the origin
    pop ds                      ; block: DS is KERNEL_SEG either way (every
    pop di                      ; caller of this routine already has it, or
    pop si                      ; the [vid_ox]/[vid_oy]/[vid_rowmax] stores
    pop cx                      ; would never have worked), and vid_depth_set
    pop ax                      ; uses no segment register and no SI
.out:
    ret

; -----------------------------------------------------------------------------
; vid_ctx_init - one display, at the origin, holding what is on screen now
;
; in:       the live geometry, already published by vid_apply
; out:      [vid_ndisp] = 1, [vid_cur] = 0, display 0 captured at (0,0)
; clobbers: flags
;
; Called from kmain beside vid_probe_avail and NOT from vid_apply - which is the
; trap this module already carries a scar from. vid_apply runs from the SPLASH,
; on its first tick, while the rest of the kernel is still coming off the floppy
; (SPEC.md 15.3), so a call from there into this file executes whatever has not
; been loaded yet: black screen, no clue. viddet.inc's header says so about
; cur_unlazy for the same reason.
;
; **THE LINE ABOVE USED TO SAY "and from vid_switch" AND IT WAS NOT TRUE** -
; vid_switch calls vid_avail_test, cur_unlazy, vid_blank, wm_su_drop_all,
; vid_equip, vid_apply, vid_setmode, vid_disp_init and desk_rowcalc, and none of
; them is this. kmain:4203 is the only edge in the repository, which is what
; puts this body in the BOOT OVERLAY (SPEC.md 2.5.2,
; docs/LAST-DROP-BYTES.md row 13) - and a stale comment naming a runtime caller
; is exactly how a later session would add the one that breaks it silently.
;
; Its two outbound calls go through cw_ shims, which is `.ovl`'s discipline
; unchanged from `.cold`'s: CS is the blob's, DS is still KERNEL_SEG, so the
; body reads [vid_ndisp] and [vid_ox] exactly as it did.
; -----------------------------------------------------------------------------
section .ovlw
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
    call KERNEL_SEG:cw_vid_ctx_ptr
    xor ax, ax                  ; display 0 sits at the virtual origin
    mov [es:di+VID_CTX_VX], ax
    mov [es:di+VID_CTX_VY], ax
    mov [vid_ox], ax            ; ...and it is current, so vid_ctx_act - which
    mov [vid_oy], ax            ; is what normally publishes these - never runs
    xor al, al
    call KERNEL_SEG:cw_vid_ctx_capture
    pop es
    pop di
    pop cx
    pop ax
    retf                        ; FAR: kmain reaches this through OVLGATE1
section .text

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
    call vid_kind_of
    call vid_blank_kind
    pop ax
.bnext:
    inc ch
    cmp ch, cl
    jb .blank

    call vid_kind_of            ; ...and become that one
    mov [vid_kind], al
    call vid_apply              ; the live block, the desktop words and the
                                ; chrome, all of it that display's
    xor ax, ax                  ; consistent with the lie: one display, and
    mov [vid_cur], al           ; it is at the virtual origin. Through AX: the
    mov [vid_ox], ax            ; accumulator forms are 3 bytes where
    mov [vid_oy], ax            ; `mov byte [m],imm8` is 5 and
    inc ax                      ; `mov word [m],imm16` is 6. AL is spent - its
    mov [vid_ndisp], al         ; last reader was the vid_kind_of above and the
    pop di                      ; caller's AX is on the stack
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
    call vid_kind_of
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
; vid_disp_desk - the desktop on every display but the primary
;                 (SPEC.md 39.14.4)
;
; in:       nothing; the caller holds the gfx lock
; out:      nothing
; clobbers: flags
;
; wm_paint_all draws display 0's band itself - menu bar to dock, the call that
; has always been there - and this is the rest. Each is ONE ground fill in
; VIRTUAL coordinates, and GFXDISP is what puts it on the right card; nothing
; here knows which card that is, or that there is more than one way to be a
; framebuffer.
;
; A secondary display gets its ground WHOLE, because SPEC.md 39.14's chrome -
; the menu bar, the dock, the drive column - lives on the primary alone.
;
; **IT IS thm_desk AND NOT gfx_fill_gray, AND THE NAME IS WHY IT WAS NOT**
; (SPEC.md 76.2.1). This routine was called vid_disp_dither, SPEC.md 76.1's own
; list of the four themed desktop sites NAMES it, and 76.2 says thm_desk
; replaced gfx_fill_gray at all of them - and this one kept the dither, so
; switching to Dark left the second monitor in Bright's ground for ever. A
; routine whose name says DITHER does not read as a themed site to whoever is
; converting them.
;
; This is the only caller in the kernel that draws in virtual coordinates.
; Everything else is inside the chrome or inside a window, and both are on
; display 0, where virtual and local are the same numbers (39.14.4).
; -----------------------------------------------------------------------------
vid_disp_desk:
    cmp byte [vid_ndisp], 1
    jbe .out
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, 1                   ; NOT A LOOP: VID_NDISP_MAX is 2 (the %if at
                                ; its equ), the guard above has already
                                ; returned for [vid_ndisp] <= 1, and display 0
                                ; is wm_paint_all's own - so the general loop
                                ; ran exactly once by construction
    call vid_ctx_rect           ; AX/BX/CX/DX = its inclusive virtual rect
    call thm_desk               ; the THEME's ground (SPEC.md 76.2), not a
                                ; hard-coded dither
    pop di
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
    stc                         ; the MISS pays the jump, and that is the
    jmp short .out              ; dead zone - which is what mou_clamp exists
.hit:                           ; to correct, so it is once per ISR pass with
    mov al, cl                  ; the pointer between monitors: 15 clocks
    clc                         ; against a ~4,500-clock pass
.out:
    pop di                      ; pop touches no flag, so the CF set above
    pop dx                      ; survives all four
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; vid_disp_planes - how deep is the display holding the virtual point (AX, BX)?
;
; in:       AX = x, BX = y, in VIRTUAL desktop coordinates
; out:      CX = 1 or 4
; clobbers: CX, flags
;
; SPEC.md 11.96's raise cache is the one consumer, and what it needs is not
; "how deep is the screen" but "how deep is the card these pixels are ON".
; Those are the same question on every machine with one display and on both
; displays of a Hercules+CGA pair; a VGA beside a Hercules is where they part
; (docs/DUAL-DISPLAY-VGA.md 4.3).
;
; It DERIVES rather than being told, and that is deliberate: the cache already
; carries the rect its pixels were taken over, in the claim's own header, so
; the display is a function of something the buffer already knows and storing
; a plane count beside it would be a second opinion that can go stale. The
; header's own comment makes the argument for the rect - "it travels with the
; pixels it describes and cannot get out of step with them" - and this is that
; sentence applied one field further.
; -----------------------------------------------------------------------------
vid_disp_planes:
    push ax
    push di
    call vid_disp_of            ; AL = the display holding it...
    call vid_kind_of            ; ...and AL = that display's adapter
    mov cx, 4
    cmp al, VID_VGA
    je .out
    mov cx, 1
.out:
    pop di
    pop ax
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
;
; A MONO CARD AND A COLOUR CARD, which is a sentence about cards where this
; used to be an equality against HERC|CGA exactly. The equality was right about
; every value it refused but one, and the one it got wrong is the pairing the
; period built most of (docs/DUAL-DISPLAY-VGA.md):
;
;   HERC|CGA        a Hercules and a real CGA          two - as before
;   VGA|HERC|CGA    a VGA and a real Hercules          two - NEW
;   VGA|CGA         ONE card: a VGA that can also do
;                   mode 6                             one
;   one bit         the ordinary machine               one
;
; WHAT MAKES THE THIRD ROW SEPARABLE FROM THE SECOND IS THAT ONE BIT IS
; EVIDENCE AND THE OTHER IS NOT. The HERC bit is only ever set by a probe that
; found memory at B0000 which the running card demonstrably is not
; (vid_memchk, and a VGA in mode 12h decodes A0000 alone), or by the machine
; running on it. The CGA bit is SYNTHETIC on any VGA - vid_probe_avail sets it
; without probing anything, because mode 6 is a standard BIOS mode there. So
; "is there a second card" is exactly "is the HERC bit set", and the colour
; half is then whichever colour card the machine has.
;
; SPEC.md 39.13 refused VGA|HERC|CGA on the grounds that the bitmap cannot say
; whether the CGA bit is that VGA's mode 6 or a third card. That is true and it
; does not decide anything: either way the machine has a colour card and a mono
; card, and pairing the two is right. What it does mean is that a machine with
; a VGA, a Hercules AND a real CGA is indistinguishable from one without the
; CGA, and gets the VGA - the third card is never programmed. Accepted, and the
; alternative is probing B8000 for a card distinct from the VGA's own mode 6,
; which is vid_cga_alias's problem again on a machine nobody has.
; -----------------------------------------------------------------------------
; **THE TWO EXITS ARE NOT MERGEABLE AND THE ONE BYTE IS NOT AVAILABLE.** This
; read `stc` / `test` / `jz .out` / `test` / `jz .out` / `clc` / `.out: ret`
; for one commit, to share the `ret` - and `test` CLEARS CF (Intel SDM: "the
; OF and CF flags are set to 0"), so the hoisted `stc` was dead and EVERY
; refusal returned CF = 0, the answer for `yes, this machine can extend`. It
; assembles, it is one byte smaller, and it inverts the routine on exactly the
; machines it exists to refuse: the ordinary single-card one, where the
; Control Panel then OFFERS the desktop radios that SPEC.md 47 says it must
; grey, fsx hands out a second page that is not there, and vid_disp_init takes
; the extend arm into an empty socket. Restored to two exits deliberately.
vid_dual_ok:
    cmp byte [vid_kind], VID_EGA    ; EGA is a single-display adapter here
    je .no                          ; (SPEC.md 39.24: the per-display context
                                    ; is not in scope for it). A machine that
                                    ; booted EGA and holds a Hercules too
                                    ; still gets one desktop; switching the
                                    ; primary to CGA re-opens the HERC|CGA
                                    ; case, which this then allows as before
    test byte [vid_avail], VID_A_HERC
    jz .no                      ; nothing to extend ONTO...
    test byte [vid_avail], VID_A_VGA | VID_A_CGA
    jz .no                      ; ...or nothing to extend FROM
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; vid_disp_other - which adapter is the display that is NOT the primary?
;
; in:       [vid_kind] = the primary, [vid_avail]
; out:      AL = a VID_* kind
; clobbers: AL, flags
;
; vid_dual_ok's other half, and it is a routine rather than four lines inside
; vid_disp_init because the two have to agree: a predicate that says "yes, two
; displays" and a derivation that then names a card the machine does not have
; is a mode set into an empty socket.
;
; It reads as "the other one" and the VGA is why it cannot be written that way.
; With a Hercules and a CGA the two kinds are a pair and flipping between them
; is the whole rule, which is what this used to be. On a VGA machine the CGA
; row is that VGA doing mode 6 (SPEC.md 39.11.1), so flipping away from the
; mono card lands on a kind that is not a second card at all - it is the same
; silicon in another mode, and vid_disp_init would then programme one card
; twice and place two displays on one monitor.
;
; So the mono card is the pivot: away from it the answer is always the mono
; card, and towards it the answer is the machine's colour card, which is the
; VGA when there is one.
; -----------------------------------------------------------------------------
vid_disp_other:
    mov al, VID_HERC            ; on a colour card: the other one is the mono
    cmp byte [vid_kind], VID_HERC   ; card, the only second card 39.13 admits
    jne .out
    mov al, VID_CGA             ; on the mono card: the colour one - and a VGA
    test byte [vid_avail], VID_A_VGA    ; wears the CGA bit as well, so the VGA
    jz .out                             ; bit has to be asked about FIRST
    mov al, VID_VGA
.out:
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
;           untouched if nothing was live, and COLLAPSED to single if
;           something was - see the refusal arm (SPEC.md 39.24)
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
; the only routine that turns a vid_tab row into the sixteen words a display
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
    jnc .paired

    ; --- REFUSED, and that used to be the end of it ------------------------
    ; [vid_ndisp] was 1 here BY CONSTRUCTION: vid_dual_ok read nothing but
    ; [vid_avail], which is fixed for the life of the machine (see the top of
    ; this file), so a refusal could never follow a non-refusal and returning
    ; was safe. VID_EGA made the predicate depend on [vid_kind] as well
    ; (SPEC.md 39.24), and vid_switch MOVES that - so an Extend can be LIVE
    ; when the answer turns to no. Returning then leaves [vid_ndisp] = 2 and
    ; display 0's vid_ctx describing the card we have just switched away
    ; from, which is a desktop drawn into the wrong segment at the wrong
    ; stride. A refusal has to bring the arrangement DOWN.
    cmp byte [vid_ndisp], 1
    je .jout                    ; nothing live: unchanged, and the boot path
    mov ah, [vid_kind]          ; ...otherwise collapse it through the arm
    call vid_disp_other         ; that already knows how. [vid_dmode] is kept:
    jmp short .single           ; the arrangement is impossible on THIS
                                ; primary, not forgotten
.paired:
    mov ah, [vid_kind]          ; the primary, banked across the two applies
    call vid_disp_other         ; ...and AL = the other display's adapter, which
                                ; is NOT simply the other of two kinds - see
                                ; there, and see what a VGA's CGA bit means
    cmp byte [vid_dmode], 0
    jne .extend

    ; --- SINGLE (SPEC.md 39.19) ---------------------------------------------
.single:
    call vid_blank_kind         ; AL is still the other kind: dark its monitor
    mov byte [vid_ndisp], 1     ; ...before anything can compute a union over
    mov byte [vid_cur], 0       ; a display that is no longer there
    mov [vid_kind], ah          ; (a no-op unless a caller moved it)
    call vid_apply              ; republishes the chrome extent AND re-homes
    xor al, al                  ; the pointer onto display 0 - which is what
    call vid_ctx_capture        ; a shrinking desktop owes it
    call vid_desk_union         ; ...and BANK BEFORE UNION, the extend path's
.jout:
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
                                ; one of the two words is 0 and the other is
                                ; the primary's own extent - which vid_apply
                                ; has just republished into the live block.
                                ; Each arm writes BOTH words, so there is no
                                ; pre-zero: the RIGHT arm overwrote it in both
    cmp byte [vid_dlay], VID_L_BELOW
    je .below
    mov ax, [vid_cw]
    mov [di+VID_CTX_VX], ax
    mov ax, MBAR_H                    ; ...AND ITS TOP ROW IS THE DESKTOP'S,
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
    mov word [di+VID_CTX_VX], 0
    mov ax, [vid_ch]
.placed:
    mov [di+VID_CTX_VY], ax

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
    clc
.out:
    pop di                      ; pop touches no flag, so the CF set above
    pop si                      ; survives all four
    pop bx
    pop ax
    ret
.no:                            ; BELOW the epilogue on purpose: the common
    stc                         ; answer - a rect that fits one display - is a
    jmp short .out              ; straight fall-through and pays nothing, and
                                ; this is font_run's and gfx_blit4's path

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
    retf

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
    cbw                         ; 1 byte where `mov ah,0` is 2: [vid_ndisp] is
    cmp si, ax                  ; 1 or 2, bounded by VID_NDISP_MAX, so the sign
    jb .d                       ; extension cannot bite
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
