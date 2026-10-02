; =============================================================================
; os8088 - desk.inc (SPEC.md 26)
;
; The desktop's ITEMS: the volumes (detected here at boot), the service item a
; driver registers (SPEC.md 26.7) and, on kern_big, the shortcuts desksc.inc
; keeps (26.8) - all of them ZONES in one grid of CELLS (SPEC.md 26.9), drawn,
; hit, selected, highlighted, damaged and opened by the one set of routines
; below. A cell is DESK_CW wide and [desk_zstep] tall; cell 0 is the top of the
; rightmost column, [vid_desk_zx], and cells run down a column and then to the
; next column on the LEFT. [desk_zslot] says which cell each zone is in.
;
; The picture box is 32 rows (VGA, Hercules) or DESK_IH_S on the CGA, where
; the pixels are 2.4:1 tall and a drive is its 14-row picture (SPEC.md 26.4);
; [desk_zh1] is that box, SPEC.md 26.5's gap and the label band, minus one.
;
; desk_paint runs under the caller's gfx lock (from wm_paint_all, right
; after the desktop fill, before any window). desk_click runs with NO lock
; held and takes gfx_lock/gfx_unlock itself around the zones it redraws.
; =============================================================================

; The grid hangs off the RIGHT edge of the screen: [vid_desk_zx] = the band's
; right edge - DESK_ZXOFF is cell 0's left, set where the band is (viddet.inc,
; dock.inc, dockmod.inc), so a right-hand dock moves the grid with it.
;
; ONE CELL, PER KERNEL (SPEC.md 26.9). kern_small has drives and nothing else,
; so its cell is the drive zone it always was - 32 wide on a 44 pitch, 56 from
; the edge - and it draws exactly what it drew. kern_big's cell is a shortcut's
; caption wide (twelve glyphs, SPEC.md 26.8.2), and a drive's picture sits in
; the middle of it.
%ifdef KERN_SMALL
DESK_CW     equ 32              ; the cell's width
DESK_PX     equ 44              ; ...its pitch, leftwards (DESK_COLW)
DESK_ZXOFF  equ 56              ; ...cell 0's left, off the band's right edge
DESK_IH_S   equ 14              ; the CGA's picture box: a drive's
%else
DESK_CW     equ 96
DESK_PX     equ 102             ; 6 columns at 640 and 7 at Hercules's 720:
                                ; a widest caption's rect (96 + 2 x 2) still
                                ; clears its neighbour's by 2 (SPEC.md 26.9.1)
DESK_ZXOFF  equ 100             ; the caption's overhang and two clear px
DESK_IH_S   equ 16              ; ...and a shortcut's 16 rows on kern_big, so
                                ; a link's picture fits above its caption
%endif
DESK_PICX   equ (DESK_CW - DESK_ZW) / 2     ; a drive's picture in its cell

; --- the table's byte (SPEC.md 26.9) ------------------------------------------
DSL_SLOT    equ 0x3F            ; the cell...
DSL_PIN     equ 0x40            ; ...PLACED by the user (a shortcut always is)
DSL_GONE    equ 0x80            ; ...not shown. 0xFF is "nothing"

DESK_ZW     equ 32              ; hit zone width (zx .. zx+31) - the icon's
                                ; own width now. It was 48, because the label
                                ; was 'Disk A' and six 8px glyphs are exactly
                                ; 48; SPEC.md 26.4 made it 'A:' and the zone
                                ; is the picture again
DESK_ZOVER  equ 2               ; label overhang each side
DESK_ZY0    equ 32              ; first zone top
DESK_LBLH   equ 12              ; the label band: 2px gap + 8px text + 2px gap
DESK_ZGAP   equ 2               ; desktop between the icon's cell and the
                                ; label's white rect (SPEC.md 26.5). It was 0,
                                ; which read as a band under the icon while
                                ; the caption filled the zone and reads as
                                ; PART OF THE DISKETTE now that it hugs: the
                                ; floppy's last row is a solid black edge, so
                                ; the white rect touched it
DESK_ZGAP_S equ 1               ; ...and on the CGA, whose pixels are 2.4:1
                                ; tall - 26.4's argument about the icon
DESK_ZSTEP  equ 60              ; zone pitch with the 32-row icon...
DESK_ZSTEP_S equ 34             ; ...and with SPEC.md 26.4's 14-row one
DESK_COLW   equ 44              ; column-to-column pitch, leftwards: the zone
                                ; plus its overhang plus one clear glyph cell,
                                ; which is what stops two columns' captions
                                ; reading as one run of text
DESK_DBLT   equ 9               ; double-click window, ticks

; --- the ZONE INDICES (SPEC.md 26.9) ------------------------------------------
; 0 .. DVOL_MAX-1 are the volumes, DESK_SVZ the service item a DRIVER registers
; (SPEC.md 26.7), and DESK_SC0 .. DESK_NZ-1 the shortcuts (26.8). The volumes
; and the service item FLOW; a shortcut, and anything the user dragged, is
; PLACED.
;
; The service item's picture is the DRIVER's: the kernel holds a caption, a
; launch file, a package name, a class and a verb, and calls the driver back to
; draw the glyph. Most XTs have no network card and a machine without one must
; not pay for a feature it cannot use, so there is no glyph in this kernel and
; nothing here spells the word Wire. kern_small has no driver that would
; register one, so the species is kern_big's.
%ifndef KERN_SMALL
DESK_SVZ    equ DVOL_MAX        ; the service item's index
DESK_SC0    equ DVOL_MAX + 1    ; the first shortcut's
SC_MAX      equ 15              ; ...and how many: 8 + 1 + 15 = 24 items, the
                                ; cells of the smallest screens (SPEC.md 26.9)
DESK_NZ     equ DESK_SC0 + SC_MAX
SC_PICX     equ DESK_PICX + DESK_ZW - 16    ; a shortcut's picture: the RIGHT
                                ; half of the picture column, its badge one
                                ; clear column left of it (SPEC.md 26.8.3), so
                                ; the selection's column covers both

; --- the record a driver registers (SPEC.md 26.7) ----------------------------
; Read out of the driver's segment ONCE, at registration, into the .bss cells
; at the foot of this file. The alternative - keeping seg:off and reading the
; strings far at every use - needs a kernel buffer at each site anyway, because
; font_run and ld_run_name both take an SI relative to DS and SS is not DS
; here (SPEC.md 1): a caption staged on the stack is unreachable by either.
DSV_R_CAP   equ 0               ; 12: the caption, NUL (<= 11 chars)
DSV_R_FILE  equ 12              ; 13: the 8.3 file it launches, NUL
DSV_R_CLS   equ 25              ;  1: the driver's DRVC_* class
DSV_R_VERB  equ 26              ;  1: the verb the kernel calls to PAINT it
DSV_R_NAME  equ 27              ; 12: the package's header name, NUL - what
                                ;     makes the zone a singleton (SPEC.md 26.7)
DSV_R_CELL  equ 39              ;  1: the cell it would like, 0xFF = the
                                ;     first free one after the volumes
                                ;     (OSAPI_DESK_ITEM, SPEC.md 26.9)
DSV_R_SIZE  equ 40
DSV_CAPMAX  equ 12              ; the four field widths, named because the copy
DSV_FILEMAX equ 13              ; and the .bss cells must agree
DSV_NAMEMAX equ 12
%else
DESK_NZ     equ DVOL_MAX
%endif

; -----------------------------------------------------------------------------
; desk_init - count floppy drives from the int 11h equipment word: if bit 0
;             is set, drives = ((AX >> 6) & 3) + 1, else 0. Rows 0 and 1 get
;             a zone flag; anything past them gets a volume ROW of its own
;             (SPEC.md 18.98). Initializes all module state (.bss is not
;             auto-zeroed).
; ins:  none
; outs: none (all registers preserved)

section .ovlw                  ; the boot overlay (SPEC.md 2.5): counting the
                                ; volumes and laying their zones out happens
                                ; once, before the first paint. desk_reflow
                                ; places them, and it and the painters are
                                ; resident
; -----------------------------------------------------------------------------
desk_init:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    push ds                     ; THE TABLE starts as "nothing" in every
    pop es                      ; zone (SPEC.md 26.9): .bss is zero, and 0
    mov di, desk_zslot          ; is cell 0. Here, in the boot overlay, it
    mov cx, DESK_NZ             ; costs no resident byte
    mov al, 0xFF
    cld
    rep stosb
    pop es
    pop di
    int 0x11                    ; AX = BIOS equipment word
    test al, 0x01               ; bit 0: any floppy drives at all?
    jz .none
    mov cl, 6
    shr ax, cl
    and al, 3                   ; 0..3 = 1..4 drives - AL, so AH keeps the
    inc ax                      ; equipment word's high byte (nothing reads it)
    mov dh, al                  ; ...and no clamp: it was 2, because the two
                                ; on the 5.25" adapter's external connector
                                ; had no volume row to be given (SPEC.md
                                ; 18.98). The COUNT is all the machine can
                                ; tell us and all it needs to: drive selects
                                ; are ordered, so 3 means 0,1,2 and there is
                                ; no drive 3 without a drive 2 to answer for
                                ;
                                ; DH and not AH: dsk_flop_add below ANSWERS in
                                ; AX, so a count kept there survives exactly
                                ; one call - which registered drive 2, lost
                                ; the count to 0, and left drive 3 with no row
    jmp .flags
.none:
    OVLMARK 51                  ; 51: SPEC.md 57.5's equipment word says this
                                ; machine has NO FLOPPY DRIVES, so no volume
                                ; gets a zone and the desktop has no icons.
                                ; On an XT that claim is the SW1 DIP switches
                                ; read back through the 8255 - a statement
                                ; about what somebody SET, and on a machine
                                ; with a hard-disk controller's option ROM in
                                ; it, about what that ROM left behind
    xor dh, dh
.flags:
    mov [fdd_dbg_eqp], dh       ; what int 11h CLAIMED, banked before anything
                                ; contests it (SPEC.md 57.5)
%ifndef NO_FDDPROBE
    ; ...and on a 5150 that claim is the SW1 DIP switches read back through the
    ; 8255, which is a statement about what somebody SET. Ask the drive
    ; (SPEC.md 18.97). Only when the word claims the second drive - a machine
    ; claiming one is uncontested and pays this compare - and only ever about
    ; unit 1, because unit 0 is where the kernel just came from.
    ;
    ; **THE COUNT IS IN DH AND THIS BLOCK USED TO READ AH.** 18.98 moved it,
    ; because dsk_flop_add below answers in AX; the two changes met at the
    ; merge and git took both without a conflict. What that produced assembles
    ; and boots: the mask was `and ax, 3` then, so AH was 0, the registry
    ; published 0, the `cmp` never let the probe run at all, and the retire
    ; path wrote a count nothing read - a probe silently switched off and a
    ; freed row that could still be given a zone. Read DH, and both features
    ; are themselves again. The mask is `and al, 3` now, which is a byte
    ; shorter AND fails louder: AH carries the equipment word's serial/parallel
    ; counts, so anyone who reads it again gets nonsense rather than a quiet 0.
    cmp dh, 2
    jb .probed
    push ax
    mov al, 1                   ; UNIT 1 - and this line is 18.98's, because
                                ; the probe took no argument when it was
                                ; written and this call site set nothing. Made
                                ; to read AL, it inherited whatever was there:
                                ; the drive COUNT, so a two-drive machine
                                ; probed unit 2 and a four-drive one asked
                                ; about unit 4, whose DOR select is `4 & 3` -
                                ; unit 0, the drive the kernel just booted off
    call dsk_fdd_probe          ; preserves BX, CX, DX and ES, so the count
    mov cl, al                  ; is safe across it; the verdict comes out of
    pop ax                      ; AX's way because the probe answers in AL
    or cl, cl
    jnz .probed                 ; present, or not PROVEN absent - either way it
                                ; keeps its zone (18.97: the two errors are not
                                ; symmetric). Absent: the row goes - BUT ONLY
                                ; IF THE CLAIM WAS A DEFAULT (SPEC.md 18.97.2).
                                ;
                                ; 18.97 contests unit 1 because on a 5150 the
                                ; equipment word is the SW1 DIP SWITCHES: two
                                ; drives is the factory position, it is wrong
                                ; on most 5150s, and nobody ever looked. That
                                ; is a default worth disproving. An AT-class
                                ; machine takes the same count out of CMOS
                                ; SETUP, where it is a deliberate assertion by
                                ; whoever configured the machine - which is
                                ; 18.98.1's own reasoning for TRUSTING a claim
                                ; of three or four drives, and it applies here
                                ; with the same force.
                                ;
                                ; It has to, because the discriminator is known
                                ; to be wrong on real drives: 18.98.1's IBM
                                ; 4865 is present, powered, mounting and
                                ; reading under int 13h at the very moment it
                                ; answers TRK0 clear before AND after a
                                ; recalibrate - the exact signature that
                                ; retires a drive here. A probe that cannot
                                ; distinguish "no drive" from "a drive that
                                ; will not report TRK0" may only be let near a
                                ; claim that was nobody's decision.
    cmp byte [cpu_tier], CPU_8086   ; tier 0 = 8086/8088 = DIP switches. Read
    jne .keepb                      ; through DS: cpu_tier is .text with a real
                                    ; initialiser and cpu_detect ran at kmain
                                    ; long before desk_init (SPEC.md 41.1)
    call dsk_fdd_retire         ; **AND THE COUNT DOES NOT.** This was
                                ; `mov dh, 1` - correct while two was the most
                                ; a machine could claim, and wrong the moment
                                ; 18.98 made units 2 and 3 reachable: it
                                ; truncates the loop below, so a machine
                                ; claiming three or four drives with no unit 1
                                ; never asks about the external pair at all.
                                ; That is not a corner - it is one internal
                                ; drive plus a 4865 on the 37-pin connector,
                                ; which is the field machine with the thing
                                ; 18.98 was written for plugged into it.
                                ; .zloop below reads the ROW instead.
    jmp short .probed
.keepb:
    mov byte [fdd_dbg_u+FDU_VRD], 1 ; and the block does not get to say the row
                                    ; went when it stayed (SPEC.md 57.5): the
                                    ; probe wrote its own finding into VRD on
                                    ; the way out, and the DECISION is made
                                    ; here. `probe stop` still carries what the
                                    ; probe concluded, which is the row
                                    ; sysbench already tells the reader to read
                                    ; FIRST - so a field report says "the probe
                                    ; called it absent and the row was kept"
                                    ; in the two bytes it already had
.probed:
%endif
    ; A floppy the machine does not have gets no zone - but its VOLUME ROW
    ; stays live, because [disk_drive] may still name it and a mount of an
    ; absent drive is an ordinary failed mount (SPEC.md 18.3). That is still
    ; right for a drive nothing DISPROVED - a claim of one drive is no evidence
    ; about unit 1, and retiring a row on no evidence is what 18.97 forbids.
    xor cl, cl
.zloop:
    mov bl, cl
    call COLD_SEG:dkf_dsk_vol_slot ; FAR: window -> cold, no .text thunk. It clobbers
    xor al, al                  ; "BX (the output), flags", so AL is free
    cmp byte [bx+DV_KIND], DVK_FREE
    je .zskip                   ; the probe retired it - do not hand it back.
                                ; This is what lets the count above stay the
                                ; CLAIM: the zone is a fact about the ROW now,
                                ; and only the row is taken away
    cmp cl, dh
    jae .zskip
    inc ax
.zskip:
    mov [bx+DV_FLAGS], al       ; the value is BUILT and stored once - the
                                ; same byte at the same point in the loop
    inc cl
    cmp cl, 2
    jb .zloop

    ; --- and the EXTERNAL pair, which needs a ROW and not just a flag -------
    ; Units 2 and 3 have no row in dsk_vtab to turn on: rows 0 and 1 are the
    ; internal drives and everything above them is free for whoever asks first
    ; (SPEC.md 18.98). HERE, and not beside dsk_boot_from in kmain, precisely
    ; so that a hard-disk boot has already taken row 2 - which is what leaves
    ; the boot partition at C: and puts the externals after it.
    mov cl, 2
.eloop:
    cmp cl, dh
    jae .edone                  ; the machine does not claim this many drives,
                                ; so it gets no row and therefore no zone -
                                ; there is nothing here to hide afterwards
%ifndef NO_FDDPROBE
    ; ...and they are ASKED but not JUDGED (SPEC.md 18.98.1). The probe runs
    ; for its published state - 57.5's ST3/ST0 for this unit is the only way
    ; anybody can see what an external drive answered - and the ROW is made
    ; whatever it says.
    ;
    ; **The switches are contested for unit 1 and TRUSTED for units 2 and 3**,
    ; and the asymmetry is about what the setting MEANS. Two drives is the
    ; factory position on essentially every 5150 and is wrong on most of them,
    ; so unit 1's claim is a default worth disproving. Nobody sets SW1 to
    ; claim three or four by accident: that is a deliberate assertion about
    ; hardware somebody went and wired to the 37-pin connector, and 18.96.2's
    ; rule applies - trust the assertion. The costs are not symmetric either:
    ; a phantom C: is one wasted click, a hidden real drive is the drive the
    ; operator just installed.
    ;
    ; It is not hypothetical. The field 5150's 4865 answers ST3 = 22 with
    ; TRK0 clear before AND after a recalibrate - ST0 = 72, the 765's own
    ; Equipment Check - while DOS reads the same drive perfectly. Whatever
    ; that is, the probe cannot see past it, and 18.97's "every failure keeps
    ; the drive" is the rule that should have governed.
    mov al, cl
    call dsk_fdd_probe          ; preserves BX, CX, DX and ES: CL and the
%endif                          ; count in DH both survive it. AL is IGNORED
    mov al, cl
    OVWCALL dsk_flop_add_x      ; window -> window: both bodies are `.ovlw`,
                                ; so the far call names FAT_SEG itself and the
                                ; `.text` thunk it used to bounce through is
                                ; gone (kernel size pass 5, -6 resident)
.eskip:
    inc cl
    cmp cl, 4
    jb .eloop
.edone:

    ; --- which KIND of diskette each floppy drive takes (SPEC.md 26.4.1) ----
    ; A 5.25" drive gets the 5.25" picture and a 3.5" one keeps the one it has
    ; always had. Asked of the DRIVE through int 13h AH=08h - the question
    ; dskw_fmt_probe_x and DOS's own FORMAT ask - and never of the medium,
    ; which is not there at boot and would not settle it if it were: a 720KB
    ; disk goes in a 1.2MB drive. Every row, not just the zones shown, so a
    ; drive the probe kept stays right if a zone is given to it later.
    xor cl, cl
.kloop:
    mov bl, cl
    call COLD_SEG:dkf_dsk_vol_slot ; FAR: window -> cold; BX = the row
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .knext                  ; a driver's volume, or a free row
    mov dl, [bx+DV_UNIT]
    test dl, 0x80
    jnz .knext                  ; a BIOS HARD disk (a boot partition)
    push bx
    push cx
    push di
    push bp                     ; AH=08h spends BX, CX, DX, DI and ES, and
    push es                     ; some BIOSes BP as well
    mov ah, 0x08
    int 0x13
    mov al, DVF_525 | DVF_GUESS ; **A ROM THAT WILL NOT ANSWER IS PROBABLY A
    jc .kset                    ; 5.25" MACHINE** - dskw_fmt_probe_x's
                                ; inference and for its reason: AH=08h for
                                ; floppies came with the AT, and so did most
                                ; drives that are not a 5.25". Neither GLaBIOS
                                ; nor the 1982 IBM ROM answers it, and a 5150
                                ; is exactly the machine that should draw this
                                ; picture. PROBABLY, because an XT can be
                                ; fitted with a 3.5" drive and GLaBIOS still
                                ; refuses (measured, os8088_xt_vga_144) - so
                                ; the guess is MARKED and the first mount of
                                ; the drive corrects it from the medium
                                ; (desk_learn_x). The boot drive's first mount
                                ; is drv_boot's, before the first paint.
                                ;
                                ; Otherwise decided on the drive's own
                                ; GEOMETRY and not on BL's CMOS type, which an
                                ; XT-class ROM that answers at all may leave
                                ; as anything: 15 sectors is the 1.2MB drive,
                                ; 9 over 40 cylinders the 360KB one, and every
                                ; other answer - 9/80, 18/80, 36/80, or a zero
                                ; from a ROM with nothing to say - keeps the
                                ; 3.5" picture it had before this existed
    and cl, 0x3F                ; CL = sectors per track
    cmp cl, 15
    je .kset
    cmp cl, 9
    jne .k35
    cmp ch, 40                  ; CH = the last cylinder, low 8 bits
    jb .kset
.k35:
    xor al, al
.kset:
    pop es
    pop bp
    pop di
    pop cx
    pop bx
    or [bx+DV_FLAGS], al
.knext:
    inc cl
    cmp cl, DVOL_MAX
    jb .kloop

    call KERNEL_SEG:ovw_desk_rowcalc  ; FAR: overlay -> resident. The row
                                ; count is geometry, and geometry can change
                                ; after boot now (SPEC.md 39.11.2), so the
                                ; arithmetic lives in .text where vid_switch
                                ; can reach it too

    xor ax, ax                  ; AX is popped two lines down, so it is free
    mov [desk_clkt], ax
    dec ax
    mov [desk_sel], al          ; 0xFF = no selection
    pop dx
    pop cx
    pop bx
    pop ax
    retf                        ; FAR, and the shim that used to own this
                                ; return is deleted (SPEC.md 2.6.1, 2.5.2):
                                ; kmain names this body directly through
                                ; OVLGATE1 and nothing near-calls it

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. Drive zones and the
                                ; desktop dither: drawn on a mount, a volume
                                ; going away and a desktop click, never in a
                                ; loop. This module has no data at all, so
                                ; there is no toggle in it (rule 1)

; -----------------------------------------------------------------------------
; desk_rowcalc - the grid's shape for this screen, then a reflow (SPEC.md 26.9)
;
; in:       [vid_dock_y0], [vid_desk_zx], [vid_kind]
; out:      [desk_zh1], [desk_zstep], [desk_pico], [desk_rows], on kern_big
;           [desk_ncell] - and every item placed in it by desk_reflow
; clobbers: flags
;
; 7 on VGA, 5 on Hercules, 4 on CGA - and at least one, always: a screen with
; no room for a single drive icon is not one this OS boots. It was inline in
; desk_init, which is boot-overlay code and therefore dead by the time the
; Control Panel can change the adapter under it (SPEC.md 39.11.2), so it is
; resident now and desk_init far-calls it like any other overlay->text step.
; -----------------------------------------------------------------------------
desk_rowcalc_x:
    call kentc_di               ; KENT: push ax..di

    ; --- which ICON, and therefore how tall a zone is (SPEC.md 26.4) --------
    ; The CGA's pixels are 2.4:1 tall, so the 32-row diskette renders as a
    ; stretched column there and costs 60 rows of pitch for it. The 14-row
    ; pair is square on that adapter AND doubles the column: 144 usable rows
    ; over a 34-row pitch is 4 zones where 60 was 2.
    ;
    ; [vid_kind] and not [vid_h], because the question is the PIXEL ASPECT
    ; and that is a property of the adapter's mode rather than of how many
    ; rows it has. It is re-asked here rather than at boot because this
    ; routine is what vid_switch re-runs (SPEC.md 39.11.2), so a machine
    ; moved from its CGA to its Hercules gets the tall pair back for free.
    ; The icon height and SPEC.md 26.5's gap are not stored: [desk_zh1] is
    ; the only thing anything ever read them for, so the sum is the arm's
    ; constant and the two words are gone. It is MINUS ONE because every
    ; reader wants it that way. The gap is inside the zone rather than beside
    ; it, so the erase, the damage growth and the XOR highlight - all of which
    ; derive from [desk_zh1] - cover it for free, and [desk_zstep] does not
    ; move: a zone still fits its pitch (45 of 60, 26 of 34).
    ;
    ; The PICTURES are one pointer and not three: [desk_pico] names the
    ; height's row of desk_ico32/desk_ico14, and desk_draw_zone indexes it.
    ; Three stores of three immediates per arm were 30 bytes of this routine
    ; for what two constant tables and one store say in 10 (SPEC.md 26.4.1).
    mov ax, 32 + DESK_ZGAP + DESK_LBLH - 1
    mov bx, DESK_ZSTEP
    mov si, desk_ico32
%ifdef OS88_SHORTCUTS
    mov cx, 32 - 16             ; a link's 16-row picture, bottom-aligned on
%endif                          ; the drives' 32-row box...
    cmp byte [vid_kind], VID_CGA
    jne .set
    mov ax, DESK_IH_S + DESK_ZGAP_S + DESK_LBLH - 1
    mov bx, DESK_ZSTEP_S
    mov si, desk_ico14
%ifdef OS88_SHORTCUTS
    xor cx, cx                  ; ...and at the TOP of the CGA's DESK_IH_S =
%endif                          ; 16, which is the link's own height
.set:
    mov [desk_zh1], ax
    mov [desk_zstep], bx
    mov [desk_pico], si
%ifdef OS88_SHORTCUTS
    mov [desk_lky], cx
%endif

    ; ROWS: the first, and then every whole PITCH that fits below it. The
    ; last row needs its own [desk_zh1]+1 rows and not a pitch - it was
    ; (dock - DESK_ZY0) / pitch, which left Hercules a whole row of room
    ; above its dock unused (324: 4 rows where 5 fit, SPEC.md 26.9.1)
    neg ax                      ; AX = [desk_zh1] still, from .set
    add ax, [vid_dock_y0]
    sub ax, DESK_ZY0 + 1        ; AX = room below the first row
    xor dx, dx
    mov cx, bx                  ; BX still holds the pitch - nothing between
    div cx                      ; .set and here writes it
    inc ax                      ; ...and the first row itself
    mov [desk_rows], ax

%ifndef KERN_SMALL
    ; --- ...and how many CELLS that is (SPEC.md 26.9) ------------------------
    ; Columns from cell 0's left to the band's left edge, at DESK_PX each,
    ; capped at what a table byte can name. An item whose cell is past the new
    ; count loses it in the reflow - a placed one flows, a shortcut is placed
    ; anew - and everything packs round what is left.
    mov cx, ax                  ; CX = rows
    mov ax, [vid_desk_zx]
%ifdef DOCK_OPT
    sub ax, [vid_band_x0]       ; a left dock takes its columns
%endif
    xor dx, dx
    mov bx, DESK_PX
    div bx
    inc ax                      ; AX = columns
    mul cx
    cmp ax, DSL_SLOT            ; DSL_SLOT itself is "no cell" (desk_reflow)
    jbe .nc
    mov al, DSL_SLOT
.nc:
    mov [desk_ncell], al
%endif
    call desk_reflow
    jmp kretfc_di

; =============================================================================
; THE SLOT GRID (SPEC.md 26.9) - one grid, one table, one placement rule
; =============================================================================
;
; Every desktop item - a volume, the driver's service item, a shortcut - is a
; ZONE INDEX, and [desk_zslot] holds one byte per zone saying which CELL of one
; grid it sits in. That byte is the whole answer to "where is it and is it
; shown"; nothing walks the volume table to find a position any more.
;
;   DSL_SLOT  bits 0..5   the cell, 0..[desk_ncell]-1
;   DSL_PIN   bit 6       PLACED: the user put it there (a shortcut always is)
;   DSL_GONE  bit 7       not shown. On a PLACED item it is a remembered
;                         cell - an unmounted drive's, or a cell an
;                         OSAPI_DESK_ITEM caller asked for - which it gets
;                         back when it is live and the cell is free; on a
;                         FLOWING one it only lives inside desk_reflow,
;                         holding the old cell for the repaint
;   0xFF                  nothing
;
; Cells are numbered column-major from the TOP-RIGHT: down a column, then the
; next column to the LEFT - SPEC.md 26.1's flow - so "the first free cell" is
; where today's next drive lands, and with nothing placed every drive is
; exactly where it always was.

; -----------------------------------------------------------------------------
; desk_zs - zone AL's cell
; in:  AL = zone index
; out: CF = 0 shown, AL = its cell; CF = 1 not shown
; clobbers: AL, flags
; -----------------------------------------------------------------------------
desk_zs:
    push bx
    mov bl, al
    xor bh, bh
    mov al, [bx+desk_zslot]
    pop bx
    cmp al, DSL_GONE            ; GONE, and 0xFF, are "not shown"
    jae .no
    and al, DSL_SLOT            ; ...which also leaves CF = 0
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; desk_live - does zone BL want a cell? (SPEC.md 26.9)
; in:  BL = zone index
; out: ZF = 0 it does, ZF = 1 it does not
; clobbers: BX, flags
;
; The one question asked PER KIND, and only by the placement: a volume is live
; while its row carries the desktop flag (SPEC.md 26.1), the service item while
; a driver has one registered (26.7), a shortcut while its claim row is in use
; (26.8.1).
; -----------------------------------------------------------------------------
desk_live:
%ifndef KERN_SMALL
    cmp bl, DESK_SVZ
    je .svc
  %ifdef OS88_SHORTCUTS
    ja .link
  %endif
%endif
    call dsk_vol_slot_x
    test byte [bx+DV_FLAGS], 1
    ret
%ifndef KERN_SMALL
.svc:
    cmp word [desk_svc_seg], 0  ; ZF = 1: no driver has one
    ret
  %ifdef OS88_SHORTCUTS
.link:
    push ax
    push es
    mov al, bl
    call sc_row                 ; CF = 1: no claim, past it, or a free row
    sbb al, al                  ; 0xFF dead, 0 live...
    inc al                      ; ...0 dead (ZF = 1), 1 live (ZF = 0) - AL,
                                ; not AX: ZF off a word is AH's to spoil
    pop es                      ; and neither pop writes a flag
    pop ax
    ret
  %endif
%endif

%ifndef KERN_SMALL
; -----------------------------------------------------------------------------
; desk_owner - which zone is SHOWN in cell AL?
; in:  AL = a cell
; out: CF = 0 one is, AH = its zone; CF = 1 the cell is free
; clobbers: AH, flags
; -----------------------------------------------------------------------------
desk_owner:
    push bx
    push cx
    mov bx, desk_zslot
    mov cx, DESK_NZ
.o:
    mov ah, [bx]
    cmp ah, DSL_GONE
    jae .n
    and ah, DSL_SLOT
    cmp ah, al
    je .hit                     ; equal: CF = 0
.n:
    inc bx
    loop .o
    stc
    jmp short .out
.hit:
    sub bx, desk_zslot          ; never borrows: CF stays 0
    mov ah, bl
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; desk_free - the first cell nothing is shown in
; out: CF = 0 AL = the cell; CF = 1 every cell on this screen is taken
; clobbers: AX, flags
; -----------------------------------------------------------------------------
desk_free:
    xor ax, ax
.s:
    cmp al, [desk_ncell]
    cmc                         ; CF = 1 once past the last cell
    jc .ret
    call desk_owner             ; CF = 1 free...
    cmc                         ; ...so CF = 0 is the answer
    jnc .ret
    inc ax
    jmp short .s
.ret:
    ret

%endif

; -----------------------------------------------------------------------------
; desk_cmark - cell AL owes a repaint: post it (SPEC.md 26.9)
; in:  AL = a cell
; out: nothing (all registers preserved)
;
; A volume comes and goes with the gfx lock possibly HELD (a driver adds one
; from a Control Panel click), so nothing here draws: it sets the cell's bit
; and posts [desk_zdirty], and ui_task's idle pass hands the set to
; desk_zones_paint - SPEC.md 26.3's deferral, with CELLS where it had a
; high-water mark of ordinals.
; -----------------------------------------------------------------------------
desk_cmark:
    push ax
    push bx
    call desk_cbit
    or [bx+desk_cdirty], ah
    mov al, 1
    mov [desk_zdirty], al
    mov [ui_post], al           ; ...and SAY SO once (SPEC.md 13.13)
    pop bx
    pop ax
    ret

; desk_cbit - cell AL's bit in [desk_cdirty]: BX = the byte, AH = the mask.
; Clobbers AH, BX.
desk_cbit:
    push cx
    mov bl, al
    xor bh, bh
    mov cl, bl
    and cl, 7
    mov ah, 1
    shl ah, cl
    mov cl, 3
    shr bx, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; desk_reflow - pack every FLOWING item into the first free cells (SPEC.md 26.9)
; in:  the table, and each kind's liveness
; out: nothing (all registers preserved); every cell that changed is posted
;
; It knows NOTHING about what was added or removed - only which zones are live
; - so it is the whole of "a volume came", "a volume went", "the Wire came",
; "a shortcut was moved or removed" and "the screen changed shape". Two passes,
; and the order is the design:
;
;   1. every FLOWING item lets go of its cell (keeping it, under DSL_GONE, for
;      the repaint); a PLACED one that is no longer live goes from the screen
;      but REMEMBERS its cell; any item in a cell this screen has not got is
;      forgotten;
;   2. every live item without a cell, IN INDEX ORDER, takes one: a PLACED
;      one its remembered cell if that is free, else - and every flowing one
;      - the first free cell. Index order is volumes, then the service item,
;      then shortcuts, so the Wire is after the drives whichever driver loaded
;      first - today's order. A shortcut reaches this pass only when it has
;      no cell at all (the screen shrank under it), and is PLACED wherever it
;      lands.
;
; A WISH IS A REMEMBERED CELL. OSAPI_DESK_ITEM writes the cell its caller
; asked for as DSL_PIN | DSL_GONE | cell, and pass 2 gives it that cell if it
; can and the first free one if it cannot - so "the slot you ask for, or the
; first available" is this routine and nothing new. A wish of 0xFF is already
; 0xFF, which is "nothing", and simply flows.
;
; NOTHING HAPPENS UNTIL [desk_ready]. The boot reads SYSTEM.CFG - the placed
; cells - and then the drivers add their volumes one at a time; a pass run
; between the two would forget a placed hard disk because its volume had not
; arrived yet. kmain sets the byte once everything is in and runs the first
; pass itself.
; -----------------------------------------------------------------------------
%ifdef KERN_SMALL
; kern_small has nothing PLACED - no shortcut, no service item, no drag - so
; the two passes are one: every live volume, in index order, gets the next
; cell. That is SPEC.md 26.1's ordinal flow with the answer WRITTEN DOWN, and
; it needs neither desk_owner nor desk_free (nor [desk_ready]: there is no
; SYSTEM.CFG to wait for).
desk_reflow:
    call kentc_di
    xor di, di
    xor cx, cx                  ; CL = the next cell
.c:
    mov dl, [di+desk_zslot]
    mov bx, di
    call desk_live
    mov al, 0xFF
    jz .set
    mov al, cl
    inc cx
.set:
    mov [di+desk_zslot], al
    and al, DSL_SLOT            ; "nothing" is DSL_SLOT's cell, which no
    and dl, DSL_SLOT            ; screen has (desk_reflow below)
    cmp al, dl
    je .n
    call desk_cmark
    mov al, dl
    call desk_cmark
.n:
    inc di
    cmp di, DESK_NZ
    jb .c
    jmp kretc_di
%else
desk_reflow:
    cmp byte [desk_ready], 0
    je .ret
    call kentc_di
    xor di, di                  ; --- 1: let go -----------------------------
.a:
    mov al, [di+desk_zslot]
    cmp al, DSL_GONE
    jae .an                     ; nothing here, or a place remembered
    mov ah, al
    and ah, DSL_SLOT
    cmp ah, [desk_ncell]
    jae .forget                 ; a cell this screen has not got
    test al, DSL_PIN
    jz .rel                     ; flowing: let go, keep the cell for the paint
    mov bx, di
    call desk_live
    jz .pgone
    mov al, ah                  ; placed and live: it stays - unless an
    call desk_owner             ; EARLIER item is in that cell, which only a
    mov bx, di                  ; damaged SYSTEM.CFG can say: then it is a
    cmp ah, bl                  ; wish again, placed in pass 2 (desk_owner
    je .an                      ; answers the lowest zone, so never a later)
    jmp short .rel
.pgone:
    mov al, ah                  ; placed and gone: its cell goes bare and the
    call desk_cmark             ; item REMEMBERS it, to have back (pass 2)
.rel:
    or byte [di+desk_zslot], DSL_GONE
    jmp short .an
.forget:
    mov byte [di+desk_zslot], 0xFF
.an:
    inc di
    cmp di, DESK_NZ
    jb .a
    xor di, di                  ; --- 2: place, in index order ---------------
.c:
    mov dl, [di+desk_zslot]     ; DL = the old byte
    cmp dl, DSL_GONE
    jb .cn                      ; shown: placed, and staying
    mov bx, di
    call desk_live
    mov dh, dl                  ; dead: a PLACED one keeps what it remembers
    jz .cdead                   ; (0xFF has the bit too and stays 0xFF)...
    test dl, DSL_PIN            ; live and PLACED: its own cell back, if this
    jz .first                   ; screen has it and nothing else is in it -
    mov al, dl                  ; which is also what a wish is, the cell an
    and al, DSL_SLOT            ; OSAPI_DESK_ITEM caller asked for
    cmp al, [desk_ncell]
    jae .first
    call desk_owner
    jnc .first
    and dh, ~DSL_GONE & 0FFh
    jmp short .cset
.first:
    call desk_free              ; ...else the first free one, FLOWING from
    mov dh, 0xFF                ; here on - unless it is a shortcut, which is
    jc .cset                    ; placed wherever it lands
    mov dh, al
%ifdef OS88_SHORTCUTS
    cmp di, DESK_SC0
    jb .cset
    or dh, DSL_PIN
%endif
    jmp short .cset
.cdead:
    test dl, DSL_PIN
    jnz .cset
    mov dh, 0xFF                ; ...and a flowing one has nothing
.cset:
    mov [di+desk_zslot], dh
    mov al, DSL_SLOT            ; --- ...and post what moved. AL = the cell
    test dl, DSL_PIN            ; it WAS shown in - only a flowing one this
    jnz .m1                     ; pass let go of had one, and "none" is
    mov al, dl                  ; DSL_SLOT's cell, which no screen has
    and al, DSL_SLOT            ; (desk_ncell stops short of it)
.m1:
    mov ah, DSL_SLOT            ; AH = the cell it IS shown in
    cmp dh, DSL_GONE
    jae .m2
    mov ah, dh
    and ah, DSL_SLOT
.m2:
    cmp al, ah
    je .cn                      ; the same cell: nothing moved
    call desk_cmark
    mov al, ah
    call desk_cmark
.cn:
    inc di
    cmp di, DESK_NZ
    jb .c
    jmp kretc_di
.ret:
    ret

%endif


; -----------------------------------------------------------------------------
; desk_zones_paint - spend the posted cells (SPEC.md 26.9)
; in:  the gfx lock HELD; [desk_cdirty]
; out: nothing (all registers preserved)
;
; One wm_paint_dmg (SPEC.md 11.91) per cell that changed: the dither clipped to
; that cell, whatever is shown in it, and only the windows that overlap it. A
; mount at the end of a column is one cell. Nothing between two columns is
; ever painted, so SPEC.md 26.3's phantom column cannot come back. No window
; moved, so the pass is told it uncovered nothing and promotes nobody (SPEC.md
; 11.91.6) - on kern_big that leaves a window over the cell untouched, the
; cell being drawn only where it shows.
; -----------------------------------------------------------------------------
desk_zones_paint_x:
    call kentc_di
    xor si, si
.c:
    mov ax, si
    call desk_cbit
    test [bx+desk_cdirty], ah
    jz .n
    not ah
    and [bx+desk_cdirty], ah
    mov ax, si
    call desk_cell_rect
    mov word [wm_dmg_stwin], 1  ; NOTHING WAS UNCOVERED - no window moved
    mov word [wm_dmg_st1+4], 8000h ; (SPEC.md 11.91.6): 11.91.2's vacated
                                ; rect, armed EMPTY (x2 = -32768 meets no rect)
                                ; for a "moved window" at 1, which is none.
                                ; Every window over the cell is then owed only
                                ; what the clipped cell and the dither reached
                                ; - on BOTH kernels: where kern_small draws a
                                ; zone whole, [wm_dmg_zb] owes those windows
    mov byte [wm_dmg_npro], 1   ; ...and so nothing was PROMOTED: the front
                                ; window is the one it was. Both kernels
    call KERNEL_SEG:cw_wm_paint_dmg
.n:
    inc si
    cmp si, DSL_SLOT            ; ...and "no cell" is never painted
    jb .c
    jmp kretfc_di

; =============================================================================
; GEOMETRY - one cell, for every kind
; =============================================================================

; -----------------------------------------------------------------------------
; desk_cell_xy - cell AL's origin (SPEC.md 26.9)
; in:  AL = a cell
; out: AX = its left column, DX = its top row
; clobbers: AX, DX (the outputs), flags
; -----------------------------------------------------------------------------
desk_cell_xy:
    push bx
    push cx
    xor ah, ah
    xor dx, dx
    div word [desk_rows]        ; AX = column, DX = row within it
    mov cx, dx
    mov bx, DESK_PX
    mul bx                      ; AX = column * pitch (DX = 0)
    mov bx, [vid_desk_zx]
    sub bx, ax                  ; columns run LEFTWARDS from the right edge
    mov ax, cx
    mul word [desk_zstep]
    add ax, DESK_ZY0
    mov dx, ax
    mov ax, bx
    pop cx
    pop bx
    ret


; -----------------------------------------------------------------------------
; desk_zone_xy - a SHOWN zone's cell origin
; in:  AL = zone index
; out: CF = 0, AX = left, DX = top; CF = 1 not shown (AX, DX undefined)
; clobbers: AX, DX, flags
; -----------------------------------------------------------------------------
desk_zone_xy:
    call desk_zs
    jnc desk_cell_xy
    ret

; -----------------------------------------------------------------------------
; desk_zone_rect - one zone's DRAWN rect; desk_cell_rect the same for a cell
; in:  AL = a shown zone / a cell
; out: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; clobbers: the four outputs
;
; The cell plus the caption's overhang each side: what the refill erases, what
; the damage covers and what a click hits.
; -----------------------------------------------------------------------------
desk_zone_rect:
    call desk_zone_xy
    jmp short desk_rect_ax
desk_cell_rect:
    call desk_cell_xy
desk_rect_ax:                   ; ...and the arithmetic alone, for a caller
    mov bx, dx                  ; already holding the origin
    add dx, [desk_zh1]
    mov cx, ax
    add cx, DESK_CW + DESK_ZOVER - 1
    sub ax, DESK_ZOVER
    ret

; -----------------------------------------------------------------------------
; desk_zone_label - a zone's caption
; in:  AL = zone index
; out: ES:SI -> a NUL string
; clobbers: ES, SI (the outputs), flags
;
; A volume's row names itself or is derived as 'A:' (SPEC.md 26.4); the
; service item's was copied in at registration (26.7); a shortcut's is read
; IN PLACE, out of its claim row (26.8.1) - which is why the answer is a far
; pointer, and why every reader of it takes ES:SI.
; -----------------------------------------------------------------------------
desk_zone_label:
    push ax
    push bx
    push ds
    pop es
%ifndef KERN_SMALL
    cmp al, DESK_SVZ
    jb .vol
    mov si, desk_svc_cap
  %ifdef OS88_SHORTCUTS
    je .out
    call sc_row                 ; ES:BX = the record
    lea si, [bx+SC_R_CAP]
  %endif
    jmp short .out
.vol:
%endif
    mov bl, al
    call dsk_vol_slot_x
    lea si, [bx+DV_LBL]
    cmp byte [si], 0
    jne .out                    ; the row named itself
    mov si, desk_lbl_gen        ; the DRIVE LETTER, built in place (SPEC.md
    add al, 'A'                 ; 26.4) - 'A:', as every other surface in this
    mov [si], al                ; OS writes a drive
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_caprect - a zone's caption and the white rect that HUGS it (SPEC.md 26.4)
; in:  AL = a shown zone
; out: AX = x1, BX = y1, CX = x2, DX = y2 of the white rect; ES:SI -> the
;      caption. The pen is (AX + DESK_ZOVER, BX + 2).
; clobbers: AX, BX, CX, DX, SI, ES (the outputs), flags
;
; Measured at paint for every kind, centred in the cell. One routine draws it
; and one highlight inverts it, so the two cannot disagree about where it is.
; -----------------------------------------------------------------------------
desk_caprect:
    push bp
    push ax
    call desk_zone_label        ; ES:SI -> it
    mov bp, font_width_x
    call ct_cw_mem_disp         ; AX = its width
    mov bx, ax
    mov cx, DESK_CW
    sub cx, bx
    jns .fit
    xor cx, cx                  ; wider than the cell: pin it to the left and
    mov bx, DESK_CW             ; keep the rect inside what the refill erases
.fit:
    shr cx, 1                   ; CX = the pen's offset in the cell
    pop ax
    call desk_zone_xy           ; AX = cell left, DX = cell top
    add ax, cx                  ; AX = the pen
    lea cx, [bx-1]
    add cx, ax
    add cx, DESK_ZOVER          ; x2
    sub ax, DESK_ZOVER          ; x1
    add dx, [desk_zh1]          ; y2 is the cell's bottom...
    mov bx, dx
    sub bx, DESK_LBLH - 1       ; ...and y1 the label band's top
    pop bp
    ret

; =============================================================================
; DRAWING - one routine, three picture sources
; =============================================================================

; -----------------------------------------------------------------------------
; desk_paint - draw every shown zone. Called by wm_paint_all after the desktop
;              fill; caller holds the gfx lock. All registers preserved.
; desk_paint_mask - draw the zones desk_dmg_zones collected into [desk_dmgm];
;              AX (wm.inc's "any" word) is not read. Lock held. On kern_big
;              each is drawn only where the pass reveals it (desk_zone_clip,
;              SPEC.md 11.91.6); wm_paint_all's pass draws them whole, the
;              painter's algorithm putting every window back over them.
; -----------------------------------------------------------------------------
desk_paint_x:
    push ax
    mov ax, 0xFFFF
    mov [desk_dmgm], ax
    mov [desk_dmgm+2], ax
%ifdef KERN_BIG
    mov ax, desk_draw_zone      ; the routine each set bit is handed to
%endif
    jmp short desk_pm_body
desk_paint_mask_x:
    push ax
%ifdef KERN_BIG
    mov ax, desk_zone_clip
%endif
desk_pm_body:
    push bx
    push cx
    push dx
%ifdef KERN_BIG
    xchg ax, dx                 ; DX = it, for the whole walk
%endif
    xor cx, cx
.next:
    shl word [desk_dmgm], 1     ; zone CL's bit, top of the dword first:
    rcl word [desk_dmgm+2], 1   ; desk_dmg_zones rotated zone 0 in LAST
    jnc .skip
    mov al, cl
%ifdef KERN_BIG
    call dx                     ; both preserve every register
%else
    call desk_draw_zone         ; a zone not shown draws nothing
%endif
.skip:
    inc cx
    cmp cl, DESK_NZ
    jb .next
    jmp kretfc_dx

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; desk_zone_clip - draw a touched zone ONLY where this damage pass reveals it
;                  (SPEC.md 11.91.6)
; in:  AL = a zone desk_dmg_zones marked (so a SHOWN one); [wm_dmg_*] set;
;      the gfx lock held
; out: nothing (all registers preserved)
;
; The region is wm_zone_r's: zone AND damage minus every window's frame. Each
; fragment is drawn ALONE - copied into slot 0 with the count at 1 - because
; font_char and ico_clip are exact against ONE fragment and under-draw against
; several (11.3.2, 11.3.4). The fragments are disjoint, so no pixel is drawn
; twice, and copying over slot 0 loses nothing: it was drawn first. Nothing
; in desk_draw_zone writes wm_clip_tab.
;
; Overflow is the old answer: the zone whole, its box grown so every window
; over it is owed a repaint (11.91's per-window test).
; -----------------------------------------------------------------------------
desk_zone_clip:
    call kentc_di
    push ax
    call desk_zone_rect
    call KERNEL_SEG:cw_wm_zone_r
    pop ax
    jc .whole
    jz .out                     ; nothing of it revealed
    xchg ax, dx                 ; DL = the zone
    mov cx, [wm_clip_n]
    mov si, wm_clip_tab
    push es
    push ds
    pop es
.frag:
    mov di, wm_clip_tab         ; fragment k into slot 0...
    movsw
    movsw
    movsw
    movsw
    mov word [wm_clip_n], 1     ; ...and only it armed
    mov al, dl
    call desk_draw_zone
    loop .frag
    pop es
    call KERNEL_SEG:cw_wm_clip_clear
    jmp short .out
.whole:
    push ax
    call desk_zone_rect
    call KERNEL_SEG:cw_wm_dmg_zadd
    pop ax
    call desk_draw_zone
.out:
    jmp kretc_di
%endif

; -----------------------------------------------------------------------------
; desk_dmg_zones - which zones does the damage rect touch?
; in:  [wm_dmg_*] (wm.inc); [wm_dmg_zb] EMPTY; caller holds the gfx lock
; out: [desk_dmgm] = the zones, for desk_paint_mask. kern_small: a zone the
;      pass reveals NONE of is left out (SPEC.md 11.91.6), and [wm_dmg_zb] =
;      the box of the rest (a zone is redrawn whole, so a window over one has
;      to be marked too - per window, by wm_dmg_wins); on kern_big
;      desk_zone_clip makes both decisions at the draw. The DAMAGE is not
;      grown: folded in, it damaged every window the box reached rather than
;      the ones over a zone (SPEC.md 11.91). AX clobbered, everything else
;      preserved.
;
; THE MASK IS A DWORD now, zone z at bit 31-z: shortcuts and drives share one
; index space (SPEC.md 26.9) and it is 24 zones on kern_big. Rotated in from
; the LAST zone to the first, so nothing has to be zeroed - the low bits are
; never read.
; -----------------------------------------------------------------------------
%if DESK_NZ > 32
  %error "desk: the zone mask is ONE DWORD - a zone past bit 31 would silently stop being repainted by every damage-driven path"
%endif
desk_dmg_zones_x:
    push bx
    push cx
    push dx
    push di
    mov di, DESK_NZ
.zone:
    dec di
    mov ax, di
    call desk_zs
    cmc
    jnc .r                      ; not shown: shift in a 0
    call desk_cell_rect
    call KERNEL_SEG:cw_wm_dmg_hit   ; CF = 1: it touches the damage
%ifdef KERN_SMALL
    jnc .r
    push ax
    push bx
    push cx
    push dx
    push si
    call KERNEL_SEG:cw_wm_zone_r    ; ...but is any of it REVEALED?
    pop si                      ; (`pop` writes no flag)
    pop dx
    pop cx
    pop bx
    pop ax
    jc .grow                    ; overflowed: say yes
    jz .r                       ; wholly under frames: CF = 0, not drawn
.grow:
    call KERNEL_SEG:cw_wm_dmg_zadd  ; drawn whole: the zones' box grows to it
    stc
%endif
.r:
    rcr word [desk_dmgm+2], 1
    rcr word [desk_dmgm], 1
    or di, di
    jnz .zone
    pop di
    pop dx
    pop cx
    pop bx
    retf

; -----------------------------------------------------------------------------
; desk_click - desktop hit test, called by ui.inc when wm_hit found no window
;              under an EVT_MDOWN. NO lock held. An item: select it, and a
;              second click within DESK_DBLT ticks opens it; on kern_big a
;              press that then travels FM_DRAGMIN px MOVES it, whatever it
;              is (SPEC.md 26.9). Bare desktop: clear the selection.
; ins:  CX = x, DX = y. outs: none (all registers preserved)
; -----------------------------------------------------------------------------
desk_click_x:
    call kentc_bp
    call desk_hit               ; AL = the zone, or 0xFF and CF on a miss
    jc .sel
    call desk_select            ; CF = 1: the second click of a double
    jnc .drag
    call desk_open
    jmp short .out
.sel:
    call desk_select            ; a miss clears the selection
    jmp short .out
.drag:
%ifdef OS88_SHORTCUTS
    mov cx, di                  ; the press that selected it is the press that
    mov dx, si                  ; drags it: fm_drag's own four pixels, waited
    call ct_cw_gfx_lock         ; out with the lock free inside the wait
    call fm_dgwait              ; CF = 1: let go where it was pressed - a click
    jc .u
    mov al, [desk_sel]          ; past them the gesture is CTRL.DRV's, which
    mov ah, SC_OP_MOVE          ; has to load to write SYSTEM.CFG anyway
    call sc_modcall
.u:
    call ct_cw_gfx_unlock
%endif
.out:
    jmp kretfc_bp

; -----------------------------------------------------------------------------
; desk_hit - the zone whose drawn rect holds a point
; in:  CX, DX = the point
; out: CF = 0 AL = the zone; CF = 1 AL = 0xFF, none. DI, SI = the point
; clobbers: AX, BX, CX, DX, BP
; -----------------------------------------------------------------------------
desk_hit:
    mov di, cx
    mov si, dx
    mov bp, DESK_NZ
.n:
    dec bp
    js .miss
    mov ax, bp
    call desk_zone_xy
    jc .n                       ; not shown
    call desk_rect_ax
    cmp di, ax
    jb .n
    cmp di, cx
    ja .n
    cmp si, bx
    jb .n
    cmp dx, si                  ; the last compare leaves CF = 0 on a hit
    jb .n
    xchg ax, bp                 ; AL = the zone
    ret
.miss:
    stc
    xchg ax, bp                 ; AX = 0xFFFF
    ret

; -----------------------------------------------------------------------------
; desk_select - make AL the desktop's selection (0xFF = none); the ONE writer
;               of [desk_sel]
; in:  AL; NO lock held
; out: CF = 1 AL was ALREADY the selection and this is its second click
;      within DESK_DBLT ticks - a double-click; CF = 0 otherwise
; clobbers: AH, BX, flags
;
; The click's birth tick (EV_C) is stamped on every call, so a slow second
; click restarts the window. The store is INSIDE the lock hold, because
; desk_zone_redraw's partial case FLIPS the highlight rather than redrawing
; it (SPEC.md 26.2). On kern_big it also keeps Locator's File > Remove
; Shortcut current - live while a shortcut is the selection, greyed otherwise
; - so the menu and the right-click's popup, which IS that item, cannot
; disagree. Its greyed twin is ONE byte before it, so live is a carry added.
; -----------------------------------------------------------------------------
desk_select:
    mov bx, [ui_click_t]
    sub bx, [desk_clkt]         ; BX = ticks since the last click, wrap-safe
    add [desk_clkt], bx         ; ...and the stamp is now this click's
    mov ah, [desk_sel]
    cmp ah, al
    jne .chg
    cmp bx, DESK_DBLT           ; the same one: CF = 1 inside the window
    ret
.chg:
    call ct_cw_gfx_lock
    mov [desk_sel], al
%ifdef OS88_SHORTCUTS
    push ax
    sub al, DESK_SC0
    cmp al, SC_MAX              ; CF = 1: a shortcut
    mov ax, sc_s_remx
    adc ax, 0
    mov [menu_items_file+2], ax
    pop ax
%endif
    xchg al, ah
    call .one                   ; the old one loses it...
    xchg al, ah
    call .one                   ; ...and the new one gains it
    call ct_cw_gfx_unlock
    clc
.r:
    ret
.one:
    cmp al, 0xFF
    je .r
    jmp desk_zone_redraw

; -----------------------------------------------------------------------------
; desk_unsel_x - the FOCUS left the desktop: nothing on it stays selected
;                (SPEC.md 26.9.5)
; in:  nothing. out: nothing; every register and the flags preserved.
; FAR, from menu_activate - the one place every focus change passes (a click,
; a raise, a window that opens, the window under a closed one) - which draws
; nothing and takes no lock. So neither does this: it forgets the selection
; and POSTS the item's cell, and ui_task's desk_zones_paint repaints that
; cell whole without the highlight. A repaint and not desk_zone_redraw's
; flip: no XOR is outstanding, so there is no pairing to keep.
; -----------------------------------------------------------------------------
desk_unsel_x:
    pushf
    push ax
    mov al, 0xFF
    xchg al, [desk_sel]
    cmp al, 0xFF
    je .o                       ; nothing was selected
%ifdef OS88_SHORTCUTS
    mov word [menu_items_file+2], sc_s_remx ; ...so Remove Shortcut greys,
%endif                          ; desk_select's other half
    call desk_zs                ; AL = its cell; CF = 1 not shown
    jc .o
    call desk_cmark
.o:
    pop ax
    popf
    retf

; -----------------------------------------------------------------------------
; desk_open - a double-click, or Enter, on zone AL: deselect, then dispatch
; in:  AL = a zone; NO lock held. out: nothing; clobbers AX, BX, flags
; -----------------------------------------------------------------------------
desk_open:
    push ax
    mov al, 0xFF
    call desk_select
    pop ax
%ifdef OS88_SHORTCUTS
    cmp al, DESK_SC0
    jae sc_open                 ; a shortcut opens BY NAME (SPEC.md 26.8.5)
%endif
%ifndef KERN_SMALL
    cmp al, DESK_SVZ
    je .svc
%endif
    jmp files_open_drive_x      ; AL = drive; locks internally (SPEC.md 22)
%ifndef KERN_SMALL
.svc:
    call KERNEL_SEG:cw_ui_svc_open  ; front it, or run it out of SYSTEM\
    ret                             ; (SPEC.md 28.3.2)
%endif

; -----------------------------------------------------------------------------
; desk_zone_redraw - flip one zone's SELECTION outside wm_paint_all
; in:  AL = zone index, whose selection state has just changed; caller holds
;      the gfx lock
; out: nothing; preserves all registers
;
; The zone may be overlapped by a visible window and the gfx_* primitives clip
; to the SCREEN, so the region arithmetic (SPEC.md 11.3) decides, four ways:
;
;   overflow    - more than 16 fragments: the full repaint stays as last resort
;   covered     - not one pixel of the zone is on screen: draw NOTHING
;   clear air   - the whole rect is inside one fragment: redraw it unclipped
;   partial     - flip only the highlight; gfx_xor_fill clips per fragment
;
; The partial case is why the caller must have changed [desk_sel] under the
; same lock hold: XOR is exact only against pixels that still carry the
; previous state (SPEC.md 26.2). A zone that is not shown any more - its item
; went away while it was selected - has nothing on the glass to flip.
; -----------------------------------------------------------------------------
desk_zone_redraw:
    push ax
    push bx
    push cx
    push dx
    push ax
    call desk_zs
    pop ax
    push ax
    jc .nonep                   ; not shown: nothing to flip
    call desk_zone_rect
    call KERNEL_SEG:wm_clip_rect
    jc .full
    jz .nonep
    call ct_cw_wm_clip_test
    pop ax                      ; `pop` writes no flags: clip_test's CF crosses
    jc .part
    call KERNEL_SEG:cw_wm_clip_clear
    call desk_draw_zone
    jmp short .done
.part:
    call desk_zone_hilite
    jmp short .done
.full:
    pop ax
    call KERNEL_SEG:cw_wm_paint_all
    jmp short .done
.nonep:
    pop ax
.done:
    jmp kretc_dx

; -----------------------------------------------------------------------------
; desk_zone_hilite - XOR one zone's SELECTION (self-inverse: applies and
;                    removes). Caller holds the gfx lock; a clip region may be
;                    armed. In AL = zone; clobbers AX, BX, CX, DX.
;
; ONE SHAPE FOR EVERY KIND on kern_big: the picture column and the caption's
; rect. Inverting a whole 96-wide cell would read as a hole in the desktop
; (SPEC.md 26.8.2), and the picture column is where every kind's picture is.
; -----------------------------------------------------------------------------
desk_zone_hilite:
%ifdef OS88_SHORTCUTS
    push ax                     ; THE PICTURE COLUMN, the picture box and the
    call desk_zone_xy           ; gap under it - which holds a drive's picture,
    add ax, DESK_PICX           ; the Wire's, and a shortcut's with its badge
    mov bx, dx                  ; (both sit in it, desk_draw_zone) - then the
    mov cx, ax                  ; CAPTION, which may be wider than the column
    add cx, DESK_ZW - 1         ; and is the other half of what a selected
    add dx, [desk_zh1]          ; item looks like. The column stops where the
    sub dx, DESK_LBLH           ; label band starts, so the two never overlap
    call ct_cw_gfx_xor_fill     ; and no pixel is flipped twice
    pop ax
    push si
    push es
    call desk_caprect
    pop es
    pop si
%else
    call desk_zone_xy           ; kern_small: the drive's column, top to bottom
    mov bx, dx                  ; - picture, gap and label band - which is
    mov cx, ax                  ; what a drive always inverted. A cell there IS
    add cx, DESK_ZW - 1         ; the column, so this is the whole zone
    add dx, [desk_zh1]
%endif
    jmp ct_cw_gfx_xor_fill      ; the NEAR trampoline, so a tail jump is safe

; -----------------------------------------------------------------------------
; desk_draw_zone - repaint one zone from scratch: the desktop ground (its
;                  dither is pixel-parity, so it meets the surroundings), the
;                  picture, the caption, and the highlight when selected.
; ins:  AL = zone index; caller holds the gfx lock
; outs: none (all registers preserved; [gfx_color] restored)
;
; THREE PICTURE SOURCES and one of everything else (SPEC.md 26.9):
;   KERNEL  a volume: one of [desk_pico]'s run records (SPEC.md 25.7)
;   DRIVER  the service item: the driver paints it (26.7)
;   CACHE   a shortcut: its stored 16x16 body, and the badge beside it (26.8.3)
; -----------------------------------------------------------------------------
desk_draw_zone:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov ah, [gfx_color]         ; read ABOVE cw_thm_desk, which under
    push ax                     ; OS88_THEME writes [gfx_color] itself
    mov [desk_zdrv], al
    call desk_zone_xy
    jc .done                    ; not shown: nothing to draw
    mov [desk_zx], ax
    mov [desk_zy], dx
    call desk_rect_ax
    call KERNEL_SEG:cw_thm_desk ; the ground under the cell and the overhang
    mov cx, [desk_zx]
    mov dx, [desk_zy]
    mov bl, [desk_zdrv]
%ifdef OS88_SHORTCUTS
    cmp bl, DESK_SC0
    jae .link
%endif
%if DESK_PICX
    add cx, DESK_PICX           ; the 32-wide picture, centred in the cell
%endif
    mov si, [desk_pico]         ; [diskette, 5.25", hard disk] at this height
%ifndef KERN_SMALL
    cmp bl, DESK_SVZ
    je .svcico
%endif
    call dsk_vol_fixed_x        ; the MEDIUM, not the transport (SPEC.md 18.7.2)
    jc .icon
    or al, al
    jz .flop
    add si, byte 4
    jmp short .icon
%ifndef KERN_SMALL
.svcico:                        ; THE DRIVER DRAWS IT (SPEC.md 26.7): no glyph
    push ds                     ; in here, so a machine with no network card
    pop es                      ; carries none. drv_pkg_call refuses (CF = 1)
    mov bh, [desk_svc_cls]      ; when no such driver is published, so one
    mov bl, [desk_svc_vrb]      ; that went away draws nothing
    call KERNEL_SEG:cw_drv_pkg_call
    jmp short .label
%endif
%ifdef OS88_SHORTCUTS
.link:                          ; THE CACHE: the stored picture, drawn
    mov al, bl                  ; straight out of the claim - the record
    call sc_row                 ; carries an icon header in front of its body
    lea si, [bx+SC_R_HDR]       ; (SPEC.md 26.8.1), so the two ARE a record
    mov cx, [desk_zx]           ; for icon_draw_x, which reads ES:SI
    add cx, SC_PICX
    add dx, [desk_lky]          ; bottom-aligned on a drive's picture box, so
    mov bp, icon_draw_x         ; a row of mixed items lines up
    call ct_cw_mem_disp
    sub cx, 11                  ; ...and the badge BESIDE it (SPEC.md 26.8.3)
    add dx, 16 - 10
    mov si, sc_bdg
    call KERNEL_SEG:cw_icon_draw
    jmp short .label
%endif
.flop:
    call dsk_vol_slot_x         ; the KIND of diskette (SPEC.md 26.4.1):
    mov al, [bx+DV_FLAGS]       ; DVF_525 is 2, the table's second word
    and ax, DVF_525
    add si, ax
.icon:
    mov si, [si]
    call KERNEL_SEG:cw_icon_draw_ix
.label:
    mov al, [desk_zdrv]         ; --- the caption, in a white rect that hugs
    call desk_caprect           ; it (SPEC.md 26.4), drawn as one opaque run
    mov byte [gfx_color], CWHITE    ; (26.6) - out of ES:SI, which for a
    call ct_cw_gfx_fill         ; shortcut is its claim row
    mov cx, ax
    add cx, DESK_ZOVER
    lea dx, [bx+2]
    mov ax, (CWHITE << 8) | CBLACK
    mov bp, font_run_x
    call ct_cw_mem_disp
    mov al, [desk_sel]
    cmp al, [desk_zdrv]
    jne .done
    call desk_zone_hilite
.done:
    pop ax
    mov [gfx_color], ah
    pop es
    pop bp
    jmp kretc_si

; -----------------------------------------------------------------------------
; desk_learn_x - a floppy just mounted: settle its drive's KIND off the medium,
;                if the ROM would not say (SPEC.md 26.4.1)
; in:  [disk_drive], and [disk_spt]/[dsk_totsec] as dsk_bpb_check has just
;      filled them
; out: nothing (all registers preserved); a changed picture posts the repaint
;
; Only a row desk_init marked DVF_GUESS is touched, so a drive whose ROM
; answered AH=08h is never second-guessed by what happens to be in it - and
; that is also the whole of the "is it a floppy" test: desk_init marks BIOS
; floppy rows and nothing else, and every other writer of DV_FLAGS stores the
; byte whole, so a partition or a driver's volume can never carry the bit. The
; medium settles the drive only where the medium CAN: 15 sectors a track is
; the 1.2MB format and 720 sectors or fewer a 40-track one, and both exist
; only on 5.25" media; 720KB and up is 3.5". A 5.25" drive fed an 80-track
; 9-sector disk would read as 3.5" here - it is the one medium that fits
; both, and on a ROM this old it is by far the rarer drive.
; -----------------------------------------------------------------------------
desk_learn_x:
    push ax
    push bx
    mov bl, [disk_drive]
    call dsk_vol_row_x
    jc .out
    mov al, [bx+DV_FLAGS]
    test al, DVF_GUESS
    jz .out                     ; the drive said what it is
    cmp word [disk_spt], 15     ; AL becomes the bit that must FLIP: the
    je .is525                   ; row's own DVF_525, complemented where the
    cmp word [dsk_totsec], 720  ; medium says 5.25" - so it is 0 exactly when
    ja .flip                    ; the guess was right, and the `and` that
.is525:                         ; isolates it sets ZF on every path in
    xor al, DVF_525
.flip:
    and al, DVF_525
    jz .out                     ; the guess was right - nothing to repaint
    xor [bx+DV_FLAGS], al
    mov al, [disk_drive]        ; the picture changed: its CELL owes a repaint
    call desk_zs                ; (SPEC.md 26.9), if it is shown at all
    jc .out
    call desk_cmark
.out:
    pop bx
    pop ax
    ret

%ifndef KERN_SMALL
; -----------------------------------------------------------------------------
; osapi_desk_item - slot 0x03F8: put an ITEM on the desktop, or take one off
;                   (SPEC.md 26.9)
; in:  AL = 1 add / 0 remove; ES = the caller's segment (an X cell)
;      add, from a DRIVER:  ES:SI -> a DSV_R_SIZE service record (SPEC.md
;                           26.7), whose +39 is the cell it would like
;      add, from a PACKAGE: ES:SI -> an SC_REC link record (SPEC.md 26.8.1),
;                           whose +1 is the cell it would like
;      remove: a driver its own service item; a package the link in zone AH,
;               ES:SI -> the record it added, which the row must still match
;      0xFF as the cell is "the first free one". A package calls this from a
;      window callback - the UI task, the gfx lock held - and never from a
;      worker: a link is PERSISTED, which writes SYSTEM.CFG (SPEC.md 20.6 r7)
; out: CF = 0 done, AL = the item's zone (an add); CF = 1 refused
; clobbers: AX (the output), flags
;
; ONE DOOR, TWO CALLERS, decided by who is calling and not by a field, which
; is osapi_vol_fence's question: the caller's segment is a published driver's
; or it is not.
;
; A DRIVER gets the SERVICE item: ONE of them, transient - it goes with the
; driver - and drawn by the driver, because a picture in here is a picture
; every machine carries and most have no network card (SPEC.md 26.7). The
; strings are COPIED here at registration: font_run and ld_run_name take an
; SI relative to DS, and SS is not DS (SPEC.md 1), so each site would need a
; kernel buffer anyway. The cell it asks for is taken if the user has not
; placed the item somewhere already and the cell is free; else it flows.
;
; A PACKAGE gets a LINK - a shortcut in all but who made it: CTRL.DRV copies
; the caller's record into a row, distrusting every byte of it, places it in
; the cell it asked for or the first free one, and writes SYSTEM.CFG. That is
; the drag-out-of-a-Disk-window's own door, so it needs the system disk for
; exactly the reason a drag does (SPEC.md 26.8.7).
; -----------------------------------------------------------------------------
osapi_desk_item_x:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call osapi_vol_fence        ; ES = the caller's; CF=1 = not a driver
    jc .pkg
    or al, al
    jz .withdraw

    ; --- a driver adds its service item ------------------------------------
    cmp word [desk_svc_seg], 0
    jne .no                     ; ONE of it
    mov ax, es                  ; the driver's segment, kept across the copy
    push ds
    push ds
    pop es                      ; ES = KERNEL_SEG, the destination...
    mov ds, ax                  ; ...and DS = the driver's, the source
    mov di, desk_svc_rec
    mov cx, DSV_R_SIZE
    cld
    rep movsb
    pop ds                      ; DS = KERNEL_SEG again, and ES already is
    mov [desk_svc_seg], ax      ; ...which is what makes the item exist
    mov byte [desk_svc_cap + DSV_CAPMAX - 1], 0
                                ; THE CAPTION IS TERMINATED HERE and not
                                ; trusted to be: it is drawn on EVERY desktop
                                ; repaint, and without a NUL font_run letters
                                ; the rest of .bss across the screen for ever
    mov al, [desk_svc_cell]     ; THE CELL IT ASKED FOR, as a wish the
    cmp byte [desk_zslot + DESK_SVZ], 0xFF  ; reflow grants if it can - unless
    jne .flow                   ; the user placed it already (SYSTEM.CFG's
    or al, DSL_PIN | DSL_GONE   ; cell wins). 0xFF stays 0xFF: it flows
    mov [desk_zslot + DESK_SVZ], al
.flow:
    call desk_reflow
    mov al, DESK_SVZ
    jmp short .ok

    ; --- a driver withdraws it ---------------------------------------------
.withdraw:
    mov bx, es
    cmp bx, [desk_svc_seg]
    jne .no                     ; not yours, or there is none
    mov word [desk_svc_seg], 0
    call desk_reflow            ; its cell is posted, and what flows after it
    jmp short .ok               ; packs up into it

    ; --- a package: a LINK, CTRL.DRV's (SPEC.md 26.8.7) ----------------------
.pkg:
    mov dx, es                  ; DX:CX = the caller's record, which survives
    mov cx, si                  ; the module's load where ES:SI would not
    or al, al
    jz .pdel
    mov ax, (SC_OP_ADD << 8) | 0xFF ; AL = 0xFF unless the module answers a
    call sc_modcall             ; zone - and one that cannot be read answers
    cmp al, 0xFF                ; nothing, having said why
    je .no
    jmp short .ok
.pdel:
    cmp ah, DESK_SC0            ; a link's zone or nothing: and never 0,
    jb .no                      ; which is the module's "removed"
    mov al, ah                  ; the zone AND the record, DX:CX: CTRL.DRV
    mov ah, SC_OP_PDEL          ; removes the row only if it is still THIS
    call sc_modcall             ; link - a zone is reused, and renumbered at
    or al, al                   ; boot. AL = 0 removed; the zone back is a
    jnz .no                     ; refusal, or no disk to read the module from
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf
%else
; -----------------------------------------------------------------------------
; ...and kern_small's, which is the whole of it there. The CELL is in both
; kernels because a slot that exists in one build and not another is an ABI
; that depends on a knob (SPEC.md 20.8 rule 4, wm_noanim's precedent); no
; driver on that kernel would register a zone, so the body is the refusal.
; -----------------------------------------------------------------------------
osapi_desk_item_x:
    stc
    retf
%endif

section .text                   ; DATA, so back to the kernel segment (SPEC.md
                                ; 2.6 rule 1). These labels carry NO COLON,
                                ; which is why a scan keyed on `name:` missed
                                ; them and moved the lot into .cold - where DS
                                ; still names KERNEL_SEG, so desk_pico read as
                                ; garbage and the comment below came true
desk_lbl_gen db "A:", 0         ; scratch for desk_zone_label's derived form:
                                ; a volume with no label of its own is named
                                ; from its index, in place

; --- what the ADAPTER decides (SPEC.md 26.4) ---------------------------------
; Set by desk_rowcalc, which is also what vid_switch re-runs, so these follow
; the machine being moved between its two cards. In .text with real
; initialisers and not .bss, for the reason every boot-reachable table here
; is: -f bin zeroes nothing, and a zero [desk_pico] draws the interrupt
; vector table as an icon.
desk_zh1    dw 32 + DESK_ZGAP + DESK_LBLH - 1   ; ...the zone's, MINUS ONE:
                                ; every reader wants the inclusive bottom
desk_zstep  dw DESK_ZSTEP       ; zone-to-zone pitch
desk_pico   dw desk_ico32       ; -> the pictures at that height, below
%ifdef OS88_SHORTCUTS
desk_lky    dw 32 - 16          ; a shortcut's picture row in its cell: 16 on
                                ; the 32-row box, 0 on the CGA (desk_rowcalc)
%endif

; --- the pictures, per height (SPEC.md 26.4.1) - constants, never written ----
; desk_draw_zone indexes a row with DVF_525 (2) for the 5.25" diskette and 4
; for a fixed volume, so the ORDER is load-bearing and asserted
desk_ico32  dw ico_disk32, ico_f525_32, ico_hdd32
desk_ico14  dw ico_disk14, ico_f525_14, ico_hdd14
%if DVF_525 != 2
  %error "desk_draw_zone indexes desk_ico32/14 with DVF_525 as a byte offset - it must be 2, the table's second word"
%endif

section .cold                   ; ...and back to cold code for the far entries

; =============================================================================
; Far entries (SPEC.md 2.6): the resident thunks in kernel.asm far-call these,
; and these near-call the bodies above.
; =============================================================================
; -----------------------------------------------------------------------------
section .bss
desk_rows    resw 1             ; zones that fit in one column above the dock
                                ; (SPEC.md 26.9.1) - 7 on VGA, 5 on
                                ; Hercules, 4 on CGA
%ifndef KERN_SMALL
desk_ready   resb 1             ; 0 until kmain has everything in: no reflow
desk_ncell   resb 1             ; cells on this screen, <= DSL_SLOT
%endif
desk_zdirty  resb 1             ; 1 = [desk_cdirty] holds cells that owe a
                                ; repaint (SPEC.md 26.9), posted
desk_cdirty  resb (DSL_SLOT + 1) / 8    ; ...one bit a cell
desk_zslot   resb DESK_NZ       ; THE TABLE: one byte a zone (SPEC.md 26.9).
                                ; desk_init fills it with 0xFF, "nothing"
desk_dmgm    resw 2             ; desk_dmg_zones -> desk_paint_mask, a dword
desk_sel     resb 1             ; selected ZONE index, 0xFF = none
desk_clkt    resw 1             ; click birth tick (EV_C) of the last zone click
desk_zy      resw 1             ; desk_draw_zone: current zone top
desk_zx      resw 1             ; ...and its left column (SPEC.md 26.1)
%ifndef KERN_SMALL
; --- the SERVICE zone a driver registered (SPEC.md 26.7) ---------------------
; The record's three strings are COPIED here at registration rather than read
; far at every use: font_run and ld_run_name both take an SI relative to DS,
; and SS is not DS on this system (SPEC.md 1), so a caption staged on the
; stack is unreachable by either and each site would need a kernel buffer of
; its own anyway. [desk_svc_seg] is the registering driver's segment and 0 is
; "no zone": one compare in desk_svflag is the whole of what a machine with no
; such driver pays at runtime.
desk_svc_seg  resw 1            ; the driver that registered, 0 = none
desk_svc_rec:                   ; ...and the record itself, IN THE RECORD'S
desk_svc_cap  resb DSV_CAPMAX   ; OWN ORDER, so the copy is one `rep movsb`
desk_svc_file resb DSV_FILEMAX  ; the 8.3 file it launches out of SYSTEM/
desk_svc_cls  resb 1            ; its DRVC_* class...
desk_svc_vrb  resb 1            ; ...and the verb that paints the icon
desk_svc_name resb DSV_NAMEMAX  ; the package's header name - the singleton
desk_svc_cell resb 1            ; the cell it asked for (SPEC.md 26.9)
%if ($ - desk_svc_rec) != DSV_R_SIZE
  %error "the .bss mirror and the registered record must be the same shape: osapi_desk_svc copies DSV_R_SIZE bytes into it with one rep movsb"
%endif
%endif
desk_zdrv    resb 1             ; desk_draw_zone: current ZONE index - a
                                ; BYTE, because DESK_NZ bounds it at 8 and
                                ; every reader wants AL or BL. The saved
                                ; [gfx_color] that used to sit here is on the
                                ; routine's own stack now
section .text
