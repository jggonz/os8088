; =============================================================================
; os8088 - HDD.DRV / tool.inc
;
; The disk tool (SPEC.md 52.2) - ONE window where there were two.
;
; Partitioning and formatting were a Partition window (New / Delete / Write)
; and a Format window (pick a slot, Format), and between them the user had to
; know that a partition table entry is not a volume, that New only edits RAM,
; that Write is the commit, and that the formatter's slot list is the same four
; slots seen through a different window. None of that is a thing anybody wants
; to know about their disk. What they want is four slots and, for each one,
; either what is in it or a button that puts a usable volume there.
;
; So there is one window, one row per slot, and two buttons that act on it:
;
;   Slot  Size   State
;   1     31M    FAT16                 <- a volume; Mount will find it
;   2      -     Not Formatted         <- free, or a region with no volume in
;   3     50M    Unmountable              it: Format makes one
;   4      -     Not Formatted         <- foreign, or over the ceiling: Format
;   [Format] [Delete] [Close]             reclaims the first 32MB of it
;
; **Not Formatted covers unpartitioned AND partitioned-but-empty**, and that is
; deliberate: the difference is the tool's business and not the user's. Both
; mean "there is no volume here and Format will make one", and a table entry
; with nothing behind it is exactly what a format interrupted half way leaves
; (SPEC.md 52.3 writes the boot sector last).
;
; **Delete gives a slot back** (SPEC.md 52.2.4), which Format cannot: Format
; reuses a slot in place, so a table filled to four primaries had no way back
; except another machine's FDISK. It clears the entry and nothing else - the
; volume's data is still on the disk - unmounts anything mounted on it first,
; and is the one control here that greys, on a slot with nothing in it.
;
; **A destructive button confirms first**, in the caption, by asking for the
; click again. There is no dialog to put it in - a driver has no notice window
; and no modal - and a second click on the same button is the cheapest confirm
; that cannot be mistaken for the first one. Anything else (picking another
; row, the other button, closing) disarms it.
;
; **No ROW is greyed, and an Unmountable one least of all** (SPEC.md 52.2.2) -
; which is not in tension with Delete greying, it is the distinction: a row is
; not a control and a button is.
; It was, and it was wrong twice. SPEC.md 47 rule 1: the disabled pen says
; DISABLED, and this row is selectable and its Format button works on it - rule
; 4's own failure, "looks unavailable and works". And it did not even show,
; because the row was greyed with a bare CDGRAY: a glyph takes the 50% dither
; only when [gfx_dis] is set, and a colour on its own rounds to black, so on
; CGA the greyed row was pixel-identical to the live one under it. The State
; column is the whole signal, and it reads the same on all three adapters.
;
; The BUTTONS do take the flag, through OSAPI_GFX_PEN - a button is a control
; and a row is not, which is the whole of the distinction.
;
; The window is the unowned species (SPEC.md 38.1) - no instance, so its close
; box reduces to wm_hide and there is no teardown path to write. What there IS
; is the rule that detach must leave none on screen, because the kernel frees
; this image the moment detach returns.
; =============================================================================

HTW_W        equ 300
HTW_H        equ 150
HTW_LX       equ 4
HTW_HY       equ 6              ; the header row
HTW_R0Y      equ 22             ; the first slot row, content-relative
HTW_ROWH     equ 12
HTW_BY       equ 96             ; the button row
HTW_BH       equ 16
HTW_CAPY     equ 120
HTW_B0X      equ 4              ; 'Format'
HTW_B1X      equ 76             ; 'Delete'
HTW_B2X      equ 148            ; 'Close'
HTW_BW0      equ 64
HTW_BW1      equ 64
HTW_BW2      equ 56

HTA_DEL      equ 0x10           ; [hd_tarm]: the armed slot is the low nibble,
                                ; 1-based so that 0 is "nothing armed", and
                                ; this bit says WHICH button armed it. One byte
                                ; for both, so they cannot arm each other and
                                ; there is no pair of words that has to agree
                                ; (SPEC.md 52.2.3)
HTA_FMT      equ 0              ; hd_tw_confirm/hd_tw_arm's action in AL
HTA_DELETE   equ 1

HTS_FREE     equ 0              ; nothing in this slot
HTS_RAW      equ 1              ; a FAT-typed extent with no volume in it
HTS_VOL      equ 2              ; a volume
HTS_ALIEN    equ 3              ; foreign type, or over the 65,535-sector
                                ; ceiling (SPEC.md 18.7) - either way this
                                ; kernel will never mount it

hd_ttpl:    dw 90, 90, HTW_W, HTW_H, hd_s_ttitle, hd_tw_paint, 0, hd_tw_click
hd_s_ttitle: db 'Format', 0

; -----------------------------------------------------------------------------
; hd_tw_open - put the tool up (module internal)
; in:  nothing; the gfx lock is HELD (a Control Panel click)
; out: nothing
;
; A fresh table every time it opens, because the disk can be swapped between
; two clicks - and the states with it, which is up to four sector reads and
; the only I/O this window does that is not a write.
; -----------------------------------------------------------------------------
hd_tw_open:
    push ax
    push bx
    push si
    mov al, [hd_sel]
    call hd_dev_row
    push di
    call hd_part_load
    pop di
    call hd_tw_scan
    mov byte [hd_tsel], 0
    mov byte [hd_tarm], 0
    mov word [hd_tmsg], hd_s_tready
    mov bx, [hd_twin]
    or bx, bx
    jnz .show
    mov si, hd_ttpl
    call OSAPI_WM_CREATE
    jc .out
    mov [hd_twin], bx
.show:
    call OSAPI_WM_SHOW
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_scan - work out all four slots' states (module internal)
; in:  DI = the device row; hd_mbr holds the table
; out: hd_tstate[0..3] filled
; clobbers: flags
;
; The order of the tests is the order of the certainties: over the ceiling and
; a type we do not mount are facts about the TABLE, so neither costs a read;
; only a slot that could carry a volume is asked whether it does.
; -----------------------------------------------------------------------------
hd_tw_scan:
    push ax
    push bx
    push si
    xor bl, bl
.slot:
    mov al, bl
    call hd_part_ent
    mov al, HTS_FREE
    call hd_part_used
    jc .store
    mov al, HTS_ALIEN
    cmp word [si+HP_SECS+2], 0
    jne .store                  ; over 65,535 sectors (SPEC.md 18.7)
    call hd_part_isfat
    jc .store                   ; a type this driver does not mount
    mov al, HTS_VOL
    call hd_fmt_isfat
    jnc .store
    mov al, HTS_RAW
.store:
    push bx
    xor bh, bh
    mov [bx+hd_tstate], al
    pop bx
    inc bl
    cmp bl, 4
    jb .slot
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_paint - W_PAINT (content already white-filled, lock held)
; in:  SI = the window
; out: nothing
; -----------------------------------------------------------------------------
hd_tw_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov di, ax
    mov bp, dx

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, hd_s_thdr
    mov cx, di
    add cx, HTW_LX
    mov dx, bp
    add dx, HTW_HY
    call OSAPI_FONT_STR

    xor cl, cl
.row:
    push cx
    mov al, cl
    call hd_tw_rowtext          ; hd_line = '1     31M   FAT16'
    pop cx
    push cx
    mov ax, cx
    mov ah, HTW_ROWH
    mul ah
    add ax, HTW_R0Y
    mov dx, bp
    add dx, ax                  ; DX = the row's text y
    push dx
    mov al, cl
    cmp al, [hd_tsel]
    jne .plain
    pop dx                      ; the selected slot: a black bar under it
    push dx
    push cx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    add ax, HTW_LX - 2
    mov bx, dx
    dec bx
    mov cx, di
    add cx, HTW_W - 14
    mov dx, bx
    add dx, 9
    call OSAPI_GFX_FILL
    pop cx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    jmp short .text
.plain:
    mov al, CBLACK              ; every row in the SAME pen: the State column
    call OSAPI_SET_COLOR        ; is what says what a slot is (SPEC.md 52.2.2)
.text:
    pop dx
    mov si, hd_line
    mov cx, di
    add cx, HTW_LX
    call OSAPI_FONT_STR
    pop cx
    inc cl
    cmp cl, 4
    jb .row

    mov al, CBLACK              ; the pen for the buttons and the caption:
    call OSAPI_SET_COLOR        ; hd_tw_button cannot set it (AX is the x)
    mov si, hd_s_tfmt
    mov ax, HTW_B0X
    mov bx, HTW_BW0
    call hd_tw_button
    call hd_tw_delok            ; the ONE predicate (SPEC.md 47 rule 2): it
    call OSAPI_GFX_PEN          ; greys the button here and refuses the click
                                ; in hd_tw_delete, so a greyed Delete can never
                                ; be one that would have worked. CF is what
                                ; both of them answer in, so the pen takes it
                                ; with no test in between - and it carries
                                ; [gfx_dis], so the label dithers on mono
                                ; along with the frame (SPEC.md 47 rule 3)
    mov si, hd_s_tdel
    mov ax, HTW_B1X
    mov bx, HTW_BW1
    call hd_tw_button
    clc                         ; ...and back to the live pen for the rest:
    call OSAPI_GFX_PEN          ; the flag would dither Close and the caption
    mov si, hd_s_close
    mov ax, HTW_B2X
    mov bx, HTW_BW2
    call hd_tw_button

    mov si, [hd_tmsg]
    mov cx, di
    add cx, HTW_LX
    mov dx, bp
    add dx, HTW_CAPY
    call OSAPI_FONT_STR

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_button - one framed, labelled button (module internal)
; in:  AX = x, BX = width (both content-relative), SI = the label,
;      DI/BP = the content origin
; out: nothing (all registers preserved)
;
; Format is live on every row there is - including an unmountable one, which is
; the only way to get that space back - and the caption is what explains a
; refusal (SPEC.md 47 rule 3: the only honest test for "is there room" is
; working out the extent, which is what the click does). Delete is the one
; button here with a predicate, and it is a fact rather than a guess: an empty
; slot has nothing to give back (SPEC.md 52.2.4).
;
; **The pen is the CALLER's**, because AX is the x here and OSAPI_SET_COLOR
; took its colour in AL: setting it inside drew both buttons at the same place,
; one on top of the other. It is also what lets the caller grey one button and
; not the others without this routine knowing any predicate - and what makes
; the frame and the label share one pen, so on mono they dither together.
; -----------------------------------------------------------------------------
hd_tw_button:
    push ax
    push bx
    push cx
    push dx
    push si
    add ax, di                  ; AX = x1
    mov cx, ax
    add cx, bx
    dec cx                      ; CX = x2
    mov bx, bp
    add bx, HTW_BY              ; BX = y1
    mov dx, bx
    add dx, HTW_BH - 1          ; DX = y2
    push ax
    push bx
    push cx
    push dx
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    add ax, 4                   ; the label, 4px in and centred vertically
    mov cx, ax
    mov dx, bx
    add dx, 4
    call OSAPI_FONT_STR
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_rowtext - one slot's line, into hd_line (module internal)
; in:  AL = slot
; out: hd_line holds a NUL string
; clobbers: flags
;
; 'n' + the size + the state, and the size is blank for a slot with no extent
; rather than a number nothing has promised: two free slots would otherwise
; both advertise the same megabytes and only one of them can have them.
; -----------------------------------------------------------------------------
hd_tw_rowtext:
    push ax
    push bx
    push si
    push di
    mov [hd_rowslot], al
    mov di, hd_line
    add al, '1'
    mov [di], al
    inc di
    mov si, hd_s_four
    call hd_scat

    mov bl, [hd_rowslot]
    xor bh, bh
    mov al, [bx+hd_tstate]
    cmp al, HTS_FREE
    jne .size
    mov si, hd_s_nosize         ; five spaces: nothing is claimed here
    call hd_scat
    jmp short .state
.size:
    mov al, [hd_rowslot]
    call hd_part_ent
    call hd_tw_size
.state:
    mov si, hd_s_two
    call hd_scat
    mov bl, [hd_rowslot]
    xor bh, bh
    mov al, [bx+hd_tstate]
    cmp al, HTS_ALIEN
    je .alien
    cmp al, HTS_VOL
    je .vol
    mov si, hd_s_traw           ; free and partitioned-but-empty read the same
    call hd_scat                ; on purpose: both mean Format makes a volume
    jmp short .done
.alien:
    mov si, hd_s_talien
    call hd_scat
    jmp short .done
.vol:
    mov al, [hd_rowslot]        ; the TYPE byte's name: what the table says,
    call hd_part_ent            ; and what this driver wrote into it
    cmp byte [si+HP_TYPE], HPT_FAT12
    mov si, hd_s_f16name        ; (no flags touched between the two)
    jne .tname
    mov si, hd_s_f12name
.tname:
    call hd_scat
.done:
    mov byte [di], 0
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_size - one entry's size, in the unit that does not read as zero
; in:  SI = the entry, DI = the string cursor
; out: DI advanced past the digits and the unit
; clobbers: DI (the output), flags
;
; **Under a megabyte it is KB**, and that is not decoration. A partition table
; another machine wrote leaves ALIGNMENT HOLES - a modern tool 1MB-aligns its
; partitions while this one starts at LBA = spt, the end of the MBR's own
; track - and the 1,985 sectors in between are real, unclaimed, and exactly
; what SPEC.md 52.2.1's scan is for. Formatted, that hole is a working 970KB
; FAT12 volume; in whole MB it read '0M', which looks like a bug in the tool
; rather than a small disk, and was reported as one.
; -----------------------------------------------------------------------------
hd_tw_size:
    push ax
    push si
    mov ax, [si+HP_SECS]
    cmp word [si+HP_SECS+2], 0
    jne .mb                     ; more sectors than a word holds: MB, plainly
    cmp ax, 2048
    jae .mb
    shr ax, 1                   ; 512-byte sectors, so KB is a halving
    call hd_utoa4
    mov si, hd_s_kb
    jmp short .unit
.mb:
    call hd_part_mb
    call hd_utoa4
    mov si, hd_s_mb
.unit:
    call hd_scat
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_click - W_ONCLICK (absolute screen coords, lock held)
; in:  CX = x, DX = y, SI = the window
; out: nothing
; -----------------------------------------------------------------------------
hd_tw_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    mov si, dx
    call OSAPI_WM_CONTENT
    mov di, ax
    mov bp, dx
    sub cx, di                  ; CX = content-relative x
    mov dx, si
    sub dx, bp                  ; DX = content-relative y

    cmp dx, HTW_BY
    jge .buttons

    mov ax, dx                  ; a slot row?
    sub ax, HTW_R0Y
    jl .done
    mov bl, HTW_ROWH
    div bl                      ; AL = the row, AH = the remainder
    cmp al, 4
    jae .done
    mov [hd_tsel], al
    mov byte [hd_tarm], 0       ; a different row is a different question
    mov word [hd_tmsg], hd_s_tready
    jmp short .repaint

.buttons:
    cmp dx, HTW_BY + HTW_BH
    jge .done
    mov ax, cx
    cmp ax, HTW_B0X
    jl .done
    cmp ax, HTW_B0X + HTW_BW0
    jl .format
    cmp ax, HTW_B1X
    jl .done
    cmp ax, HTW_B1X + HTW_BW1
    jl .delete
    cmp ax, HTW_B2X
    jl .done
    cmp ax, HTW_B2X + HTW_BW2
    jge .done
    mov byte [hd_tarm], 0
    mov bx, [hd_twin]
    call OSAPI_WM_HIDE
    jmp short .done

.delete:
    call hd_tw_delete
    jmp short .repaint
.format:
    call hd_tw_format
.repaint:
    call hd_tw_repaint
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_format - partition and format the selected slot (module internal)
; in:  [hd_tsel]; the gfx lock is HELD
; out: [hd_tmsg] says what happened; the states are re-scanned
; clobbers: flags
;
; **The table entry goes down BEFORE the volume**, and that is the same
; argument as the boot sector going last inside the format (SPEC.md 52.3):
; every way this can be interrupted has to leave a state the tool can describe
; and re-offer. Entry first, volume second means an interruption leaves 'Not
; Formatted' - a claimed region with nothing in it - which is exactly what the
; button is for. The other order leaves a volume nothing can see and an entry
; still pointing at data that has just been overwritten.
; -----------------------------------------------------------------------------
hd_tw_format:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bl, [hd_tsel]
    xor bh, bh
    cmp byte [bx+hd_tstate], HTS_FREE
    je .go                      ; nothing here to lose

    mov al, HTA_FMT
    call hd_tw_confirm
    jnc .go                     ; asked and answered
    jmp .out                    ; ...otherwise the caption is asking, and .out
                                ; is NEAR: the format below is well past 127
                                ; bytes and a `jc` cannot reach it
.go:
    mov byte [hd_tarm], 0
    mov al, [hd_sel]
    call hd_dev_row
    mov al, [hd_tsel]
    call hd_slot_extent         ; -> [hd_fbase] / [hd_fsecs] (SPEC.md 52.2.1)
    jc .noroom
    call hd_fmt_plan            ; -> the cluster size and the FAT type
    jc .nofit

    mov word [hd_tmsg], hd_s_tdoing
    call hd_tw_repaint          ; say so BEFORE the writes: a 32MB format is
                                ; hundreds of sectors and the user should not
                                ; watch a frozen window wonder

    mov al, [hd_tsel]           ; the entry: the extent, the type the plan
    call hd_part_ent            ; chose, and the CHS halves DOS reads
    mov ax, [hd_fbase]
    mov [si+HP_LBA], ax
    mov ax, [hd_fbase+2]
    mov [si+HP_LBA+2], ax
    mov ax, [hd_fsecs]
    mov [si+HP_SECS], ax
    mov word [si+HP_SECS+2], 0
    mov byte [si+HP_TYPE], HPT_FAT16S
    cmp byte [hd_ftype], 0
    jne .type
    mov byte [si+HP_TYPE], HPT_FAT12
.type:
    mov byte [si+HP_STATUS], 0
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_chs

    call hd_part_write          ; one sector, and it is the first commit
    jc .werr
    mov al, [hd_sel]
    call hd_dev_row
    call hd_fmt_run             ; ...and the volume, boot sector last
    jc .ferr
    mov word [hd_tmsg], hd_s_tdone
    jmp short .rescan
.werr:
    mov word [hd_tmsg], hd_s_twerr
    jmp short .rescan
.ferr:
    mov word [hd_tmsg], hd_s_tferr
    jmp short .rescan
.noroom:
    mov word [hd_tmsg], hd_s_tnoroom
    jmp short .out
.nofit:
    mov word [hd_tmsg], hd_s_tnofit
    jmp short .out
.rescan:
    mov al, [hd_sel]            ; the states again, which re-reads the boot
    call hd_dev_row             ; sector we just wrote: the row is what the
    call hd_tw_scan             ; DISK says and not what we hoped
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_delete - clear the selected slot's table entry (SPEC.md 52.2.4)
; in:  [hd_tsel]; the gfx lock is HELD
; out: [hd_tmsg] says what happened; the states are re-scanned
; clobbers: flags
;
; Sixteen zeroed bytes and the one sector write Format commits with - the
; volume's data is not touched, and an entry re-created with the same base and
; length brings it back. What the space becomes is free space, which
; hd_slot_extent's scan will hand to ANY slot and not only this one.
;
; **The volume goes before the table.** A driver-backed volume's base LBA lives
; in hd_vols and the kernel never reads the table again (SPEC.md 18.7), so one
; left mounted here goes on reading and writing a region the table has just
; called free - and the very next Format lays a new volume straight through it.
; -----------------------------------------------------------------------------
hd_tw_delete:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    call hd_tw_delok            ; the same predicate that greyed the button:
    jc .empty                   ; the refusal only puts the reason back on
                                ; screen (SPEC.md 47 rule 5)
    mov al, HTA_DELETE
    call hd_tw_confirm
    jc .out                     ; the caption is asking

    mov al, [hd_tsel]
    call hd_tw_unmount_slot     ; -> [hd_tdrop]
    cmp byte [hd_tdrop], 0
    je .table
    call hd_cfg_mark            ; the set of mounted drives is what the next
                                ; boot reads, and it just changed: STAGED, like
                                ; every other setting in the machine, and
                                ; written when the panel closes (SPEC.md 31.8)
    mov word [hd_msg], hd_s_pick    ; ...and the PAGE's caption records what the
                                    ; last PAGE click did, so it is sitting
                                    ; there saying 'Mounted 1 volume' about a
                                    ; volume this click has just taken away. The
                                    ; neutral line claims nothing and so cannot
                                    ; be wrong; closing the tool repaints the
                                    ; panel, which is where it shows
.table:
    mov al, [hd_tsel]
    call hd_part_ent            ; SI = the entry
    push es
    push ds
    pop es
    cld
    mov di, si
    mov cx, HP_ESZ
    xor al, al
    rep stosb                   ; a zero type and a zero length is exactly what
    pop es                      ; hd_part_used calls free

    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_write          ; one sector, and it is the commit
    jc .werr
    mov word [hd_tmsg], hd_s_tdeleted
    cmp byte [hd_tdrop], 0
    je .rescan
    mov word [hd_tmsg], hd_s_tdelunm    ; a drive icon leaving the desktop with
    jmp short .rescan                   ; no explanation is worse than the pause
.werr:
    mov al, [hd_sel]            ; **the table goes back**, which Format's own
    call hd_dev_row             ; failure path deliberately does not do: RAM
    call hd_part_load           ; claiming a region the disk does not is
    mov word [hd_tmsg], hd_s_twerr  ; conservative, and RAM calling a LIVE
                                    ; partition free is the one state the next
                                    ; Format destroys something from
.rescan:
    mov al, [hd_sel]            ; the states again, off the disk rather than
    call hd_dev_row             ; off what we hoped we wrote
    call hd_tw_scan
    jmp short .out
.empty:
    mov word [hd_tmsg], hd_s_tdempty
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_delok - may Delete be clicked? (module internal)
; in:  [hd_tsel], hd_tstate
; out: CF = 0 yes; CF = 1 = the slot is empty and there is nothing to give back
; clobbers: flags
;
; A FACT and never a guess (SPEC.md 47 rule 4): the state it reads is the one
; already printed in the row's State column. ONE predicate, shared by the
; greying in hd_tw_paint and the refusal in hd_tw_delete, so a greyed Delete
; can never be one that would have worked.
; -----------------------------------------------------------------------------
hd_tw_delok:
    push ax
    push bx
    mov bl, [hd_tsel]
    xor bh, bh
    mov al, [bx+hd_tstate]
    cmp al, HTS_FREE
    je .no
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_tw_unmount_slot - drop every volume mounted on one partition (internal)
; in:  AL = the slot, [hd_sel] = the device; the gfx lock is HELD
; out: [hd_tdrop] = 1 if anything was unmounted, 0 if not
; clobbers: flags
;
; EVERY row and not the first, for hd_unmount's own reason: one partition is
; one volume today, and a scan that stops early is the kind of thing that
; stops being true quietly.
; -----------------------------------------------------------------------------
hd_tw_unmount_slot:
    push ax
    push bx
    push cx
    push dx
    mov dl, al                  ; DL = the slot: AL is the comparison's
    mov byte [hd_tdrop], 0
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, [hd_sel]
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, dl
    jne .next
    call hd_unmount_row
    mov byte [hd_tdrop], 1
.next:
    add bx, HDV_SIZE
    loop .scan
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_confirm - the ask-again gate both destructive buttons share (internal)
; in:  AL = HTA_FMT / HTA_DELETE
; out: CF = 0 = asked and answered, go ahead; CF = 1 = the question is now in
;      the caption
; clobbers: flags
;
; ONE BYTE holds the armed action - the slot in the low nibble, 1-based so that
; 0 is "nothing armed", and HTA_DEL for which button (SPEC.md 52.2.3). So the
; two buttons cannot arm each other: arming Format and then clicking Delete
; builds a different byte, which asks Delete's question rather than acting on
; Format's answer. A pair of words that had to agree was the obvious shape and
; the wrong one.
; -----------------------------------------------------------------------------
hd_tw_confirm:
    push ax
    push bx
    mov bl, al                  ; BL = the action
    mov al, [hd_tsel]
    inc al
    cmp bl, HTA_FMT
    je .want
    or al, HTA_DEL
.want:
    cmp al, [hd_tarm]
    je .go                      ; asked and answered
    mov [hd_tarm], al           ; ...otherwise ask, and it is the CAPTION that
    mov al, bl                  ; asks: a driver has no dialog to put it in
    call hd_tw_arm
    pop bx
    pop ax
    stc
    ret
.go:
    mov byte [hd_tarm], 0
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_tw_arm - the confirm question, into hd_ask (module internal)
; in:  AL = HTA_FMT / HTA_DELETE, [hd_tsel]
; out: [hd_tmsg] points at the built line
; clobbers: flags
;
; hd_line is the row painter's buffer too, so this has to be its own: the
; caption survives a repaint and a row does not.
;
; The button names ITSELF in the question, both halves of it: 'Click Format
; again' under a Delete that is armed would be an instruction that erases the
; partition it is standing next to.
; -----------------------------------------------------------------------------
hd_tw_arm:
    push ax
    push bx
    push si
    push di
    mov bl, al
    mov di, hd_ask
    mov si, hd_s_task0
    cmp bl, HTA_FMT
    je .verb
    mov si, hd_s_tdsk0
.verb:
    call hd_scat
    mov al, [hd_tsel]
    inc al
    xor ah, ah
    call hd_utoa
    mov si, hd_s_task1
    cmp bl, HTA_FMT
    je .again
    mov si, hd_s_tdsk1
.again:
    call hd_scat
    mov byte [di], 0
    mov word [hd_tmsg], hd_ask
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_tw_repaint - redraw the tool's content (module internal)
; in:  nothing; the lock is held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hd_tw_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [hd_twin]
    or bx, bx
    jz .out
    call hd_win_erase           ; the CONTENT, and only the content
    mov si, [hd_twin]
    call hd_tw_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_s_kb:      db 'K', 0
hd_s_thdr:    db 'Slot  Size   State', 0
hd_s_four:    db '     ', 0     ; slot digit -> size column
hd_s_nosize:  db '     ', 0     ; ...and the size column when nothing claims it
hd_s_traw:    db 'Not Formatted', 0
hd_s_talien:  db 'Unmountable', 0
hd_s_tfmt:    db 'Format', 0
hd_s_tdel:    db 'Delete', 0
hd_s_tready:  db 'Pick a slot, then a button', 0
hd_s_tdoing:  db 'Writing...', 0
hd_s_tdone:   db 'Formatted - now Mount it', 0
hd_s_twerr:   db 'Could not write the table', 0
hd_s_tferr:   db 'Could not write the volume', 0
hd_s_tnoroom: db 'No room left on this drive', 0
hd_s_tnofit:  db 'No FAT layout fits that space', 0
hd_s_task0:   db 'Erase slot ', 0
hd_s_task1:   db '? Click Format again', 0
hd_s_tdsk0:   db 'Delete slot ', 0
hd_s_tdsk1:   db '? Click Delete again', 0
hd_s_tdeleted: db 'Slot deleted', 0
hd_s_tdelunm: db 'Deleted - volume unmounted', 0
hd_s_tdempty: db 'Nothing in that slot', 0
