; =============================================================================
; os8088 - HDD.DRV / inst.inc
;
; Install (SPEC.md 52.10.4): make a partition into a system volume the machine
; can boot from, by putting both floppies onto it and marking it bootable.
;
;   Install to        Size   State
;   Slot 1            31M    Ready          <- big enough, and formattable
;   Slot 2             -     Too Small
;   Slot 3            50M    Unusable       <- foreign, or over the ceiling
;   Slot 4             -     No Room
;   [Install] [Close]
;   Insert the APPS disk in A:, then Install
;
; **IT FORMATS THE TARGET, AND THAT IS NOT A CONVENIENCE.** boot/boothd.asm
; reads KERNEL.SYS as a flat run from the start of the data area rather than
; walking its cluster chain - which is what makes it fit in 512 bytes
; (SPEC.md 19.3/52.10.2) - so the kernel has to be the FIRST thing on the
; volume and contiguous from cluster 2. Formatting guarantees a free cluster
; 2; writing the kernel before anything else puts it there.
;
; **THE COPY IS IN STAGES, DRIVEN BY REPEAT CLICKS**, because the machine this
; is for has ONE floppy drive (docs/FIELD-MACHINES.md) and a driver has no
; modal dialog to ask in - tool.inc's Delete uses the same idiom, the caption
; asking and the second click answering:
;
;   idle     -> Install arms:  'Erases slot N - Install again to confirm'
;   armed    -> format, then copy the SYSTEM disk (the one in A: now)
;               'Copied - insert APPS disk, then Install'
;   sysdone  -> copy that disk, write the VBR, write the MBR
;               'Done - restart with no floppy'
;
; Taking the system disk out mid-install is safe: the kernel is in RAM and the
; only thing that reaches for A: afterwards is this copy. A Control Panel
; close would try to write SYSTEM.CFG and be refused, because SPEC.md 51.5.1
; looks for KERNEL.SYS before writing and the apps disk has none.
;
; **THE MBR IS LAST AND IT IS THE COMMIT.** Every earlier step leaves a disk
; that is merely not bootable yet - the same discipline the formatter follows
; in writing its boot sector last (SPEC.md 52.3). An install interrupted
; anywhere leaves a machine that still boots from its floppy.
;
; A driver has no instance, so it has no folder of its own and uses the
; machine's (SPEC.md 19.2.1 - inst_vol_enter says so in its own comment):
; every file call here acts on the global current volume, and OSAPI_FILE_GOTO
; is how it moves. The volume is banked and restored around each stage,
; SPEC.md 51.5.2's rule, learned when a Control Panel click silently moved
; the user to A:.
; =============================================================================

HIW_W        equ 300
HIW_H        equ 150
HIW_LX       equ 4
HIW_HY       equ 6
HIW_R0Y      equ 24
HIW_ROWH     equ 12
HIW_BY       equ 96
HIW_BH       equ 16
HIW_CAPY     equ 120
HIW_B0X      equ 4              ; 'Install'
HIW_B1X      equ 84             ; 'Close'
HIW_BW0      equ 72
HIW_BW1      equ 56

; What a slot is as an install TARGET, which is not tool.inc's HTS_* question.
; That one asks what is there; this asks whether an install would work, and
; the two differ: a slot with a perfectly good volume on it is a fine target
; (it gets reformatted), and a slot too small is not, however healthy it is.
HIS_OK       equ 0
HIS_SMALL    equ 1
HIS_ALIEN    equ 2
HIS_NONE     equ 3

; Both floppies are 720KB between them, and a system volume wants room to work
; in afterwards. Below this an install would fit and leave nothing over, so it
; is refused with a reason rather than allowed to produce a full disk.
HIW_MINSEC   equ 4096           ; 2MB in sectors

HII_IDLE     equ 0
HII_ARMED    equ 1
HII_SYSDONE  equ 2
HII_DONE     equ 3

HIW_CHUNKKB  equ 32             ; the copy buffer, KB. A cluster multiple on
                                ; every geometry here (SPEC.md 18.4.4's rule),
                                ; and small enough that a 128KB machine can
                                ; still find it
HIW_KMAXKB   equ 96             ; ...and the ceiling on KERNEL.SYS, which is
                                ; written WHOLE (18.4.4: append refuses a
                                ; protected file, and one write is also the
                                ; strongest guarantee of contiguity)

hd_itpl:    dw 70, 70, HIW_W, HIW_H, hd_s_ititle, hd_iw_paint, 0, hd_iw_click

; -----------------------------------------------------------------------------
; hd_iw_open - put the installer up (module internal)
; in:  the gfx lock is HELD (a Control Panel click)
; -----------------------------------------------------------------------------
hd_iw_open:
    push ax
    push bx
    push si
    mov byte [hd_iw_st], HII_IDLE
    mov word [hd_imsg], hd_s_ipick
    mov byte [hd_iautod], 0xFF  ; a fresh run asks the machine again
    call hd_iw_scan             ; a fresh table every time, tool.inc's reason:
                                ; the disk can be swapped between two clicks
    cmp word [hd_iwin], 0
    jne .show
    mov si, hd_itpl
    call OSAPI_WM_CREATE
    jc .out
    mov [hd_iwin], bx
.show:
    mov bx, [hd_iwin]
    call OSAPI_WM_SHOW
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_shut - take it down. DETACH's too: the kernel frees this image the
; moment detach returns, so a window of ours left on screen is a paint through
; freed memory. The unowned species (SPEC.md 38.1), so hiding IS closing.
; -----------------------------------------------------------------------------
hd_iw_shut:
    push bx
    mov bx, [hd_iwin]
    or bx, bx
    jz .out
    call OSAPI_WM_HIDE
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_iw_repaint - the window, when the caption has changed (module internal)
; -----------------------------------------------------------------------------
hd_iw_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [hd_iwin]
    or bx, bx
    jz .out
    call hd_win_erase           ; the CONTENT, and only the content - tool.inc's
    mov si, [hd_iwin]           ; ...and the window in SI, which is where
    call hd_iw_paint            ; W_PAINT's contract puts it. The reason this
                                ; is not WM_FRONT is
                                ; SPEC.md 11.90: raising a window that is
                                ; ALREADY frontmost repaints nothing at all, so
                                ; a changed caption never reached the glass
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_scan - what each slot would be as a target (module internal)
; out: hd_istate[0..3] = HIS_*, hd_isecs[0..3] = its extent in sectors
; -----------------------------------------------------------------------------
hd_iw_scan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_load           ; the table, or a blank one if unreadable
    xor cl, cl
.slot:
    mov bl, cl
    xor bh, bh
    mov byte [bx+hd_istate], HIS_NONE
    shl bx, 1
    mov word [bx+hd_isecs], 0

    mov al, cl
    call hd_part_ent            ; SI -> the entry
    mov al, [si+HP_TYPE]
    or al, al
    jz .extent                  ; empty: is there room to make one?
    call hd_fmt_isfat           ; a type this kernel could mount
    jc .alien
    mov ax, [si+HP_SECS+2]
    or ax, ax
    jnz .alien                  ; past 65,535 sectors: the volume ceiling
    mov ax, [si+HP_SECS]
    jmp short .sized
.extent:
    push cx
    mov al, [hd_sel]
    call hd_dev_row
    mov al, cl
    call hd_slot_extent         ; -> [hd_fbase]/[hd_fsecs] (SPEC.md 52.2.1)
    pop cx
    jc .next                    ; no room on the disk for a new one
    mov ax, [hd_fsecs]
.sized:
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov [bx+hd_isecs], ax
    mov bl, cl
    xor bh, bh
    cmp ax, HIW_MINSEC
    jb .small
    mov byte [bx+hd_istate], HIS_OK
    jmp short .next
.small:
    mov byte [bx+hd_istate], HIS_SMALL
    jmp short .next
.alien:
    mov bl, cl
    xor bh, bh
    mov byte [bx+hd_istate], HIS_ALIEN
.next:
    inc cl
    cmp cl, 4
    jb .slot

    ; The selection follows what is usable, so the first click lands on a
    ; target that would work rather than on whatever slot 0 happens to be.
    mov bl, [hd_isel]
    xor bh, bh
    cmp byte [bx+hd_istate], HIS_OK
    je .out
    xor cl, cl
.pick:
    mov bl, cl
    xor bh, bh
    cmp byte [bx+hd_istate], HIS_OK
    jne .pnext
    mov [hd_isel], cl
    jmp short .out
.pnext:
    inc cl
    cmp cl, 4
    jb .pick
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_paint - W_PAINT.  in: BX = the window, ES = KERNEL_SEG (SPEC.md 20.3)
; -----------------------------------------------------------------------------
hd_iw_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si                  ; W_PAINT hands the window in SI, and
    call OSAPI_WM_CONTENT       ; OSAPI_WM_CONTENT wants it in BX
    mov di, ax                  ; AX = content left, DX = top - the KERNEL's,
    mov bp, dx                  ; never the template's: wm_fit may have clamped
                                ; it. AX and NOT CX, which is what this read
                                ; first and is a whole content drawn a window's
                                ; width to the right of where it belongs

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, hd_s_ihead
    mov cx, di
    add cx, HIW_LX
    mov dx, bp
    add dx, HIW_HY
    call OSAPI_FONT_STR

    xor cl, cl
.row:
    push cx
    mov [hd_irow], cl
    call hd_iw_rowtext          ; -> hd_line
    mov al, [hd_irow]
    mov ah, HIW_ROWH
    mul ah
    add ax, HIW_R0Y
    mov dx, bp
    add dx, ax
    mov al, [hd_irow]
    cmp al, [hd_isel]
    jne .plain
    push dx                     ; the selected row is a black bar - page.inc's
    mov al, CBLACK              ; and tool.inc's, so a selection reads the same
    call OSAPI_SET_COLOR        ; everywhere in this driver
    mov ax, di
    add ax, HIW_LX - 1
    pop dx
    push dx
    mov bx, dx
    dec bx
    mov cx, di
    add cx, HIW_W - 12
    mov dx, bx
    add dx, 9
    call OSAPI_GFX_FILL
    pop dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    jmp short .text
.plain:
    mov al, CBLACK
    call OSAPI_SET_COLOR
.text:
    mov si, hd_line
    mov cx, di
    add cx, HIW_LX
    call OSAPI_FONT_STR
    pop cx
    inc cl
    cmp cl, 4
    jb .row

    mov si, hd_s_iinst
    mov ax, HIW_B0X
    mov bx, HIW_BW0
    xor cl, cl
    call hd_iw_button
    mov si, hd_s_close
    mov ax, HIW_B1X
    mov bx, HIW_BW1
    mov cl, 1
    call hd_iw_button

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, [hd_imsg]
    mov cx, di
    add cx, HIW_LX
    mov dx, bp
    add dx, HIW_CAPY
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
; hd_iw_button - one framed button (module internal)
; in:  SI = label, AX = x, BX = width, CL = 0 Install / 1 Close,
;      DI = pane left, BP = pane top
;
; The pen comes from OSAPI_GFX_PEN, so a disabled button carries [gfx_dis]
; as well as CDGRAY and its LABEL dithers on the two 1bpp adapters instead of
; coming out solid black inside a dotted frame (SPEC.md 47 rule 1). Close is
; never disabled: there is always a way out of a window.
; -----------------------------------------------------------------------------
hd_iw_button:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [hd_ibx], ax
    mov [hd_ibw], bx
    mov [hd_ibl], si
    or cl, cl
    jnz .live
    call hd_inst_ok             ; the ONE predicate, shared by the greying,
    jmp short .pen              ; the refusal and the explanation (47 rule 2)
.live:
    clc
.pen:
    call OSAPI_GFX_PEN          ; CF IS the argument (SPEC.md 47 rule 1)
    mov ax, di
    add ax, [hd_ibx]
    mov bx, bp
    add bx, HIW_BY
    mov cx, ax
    add cx, [hd_ibw]
    dec cx
    mov dx, bx
    add dx, HIW_BH - 1
    call OSAPI_GFX_FRAME
    mov si, [hd_ibl]
    call OSAPI_FONT_WIDTH       ; AX = its pixels, for centring
    mov cx, [hd_ibw]
    sub cx, ax
    shr cx, 1
    add cx, di
    add cx, [hd_ibx]
    mov dx, bp
    add dx, HIW_BY + 4
    mov si, [hd_ibl]
    call OSAPI_FONT_STR
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_ok - may Install be clicked? (module internal)
; out: CF = 0 yes / 1 no.  SPEC.md 47 rule 2's single predicate.
; -----------------------------------------------------------------------------
hd_inst_ok:
    push bx
    cmp byte [hd_iw_st], HII_DONE
    je .no                      ; finished: nothing left to do in here
    mov bl, [hd_isel]
    xor bh, bh
    cmp byte [bx+hd_istate], HIS_OK
    jne .no
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_iw_rowtext - one row into hd_line (module internal)
; -----------------------------------------------------------------------------
hd_iw_rowtext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, hd_line
    mov [hd_lineptr], di
    mov si, hd_s_islot
    call hd_scat
    mov di, [hd_lineptr]
    mov al, [hd_irow]
    xor ah, ah
    inc ax
    call hd_utoa
    mov [hd_lineptr], di
    mov si, hd_s_igap
    call hd_scat

    mov bl, [hd_irow]
    xor bh, bh
    shl bx, 1
    mov ax, [bx+hd_isecs]
    or ax, ax
    jz .nosize
    call hd_iw_size
    jmp short .state
.nosize:
    mov di, [hd_lineptr]
    mov si, hd_s_idash
    call hd_scat
.state:
    mov di, [hd_lineptr]
    mov si, hd_s_igap
    call hd_scat
    mov bl, [hd_irow]
    xor bh, bh
    mov bl, [bx+hd_istate]
    xor bh, bh
    shl bx, 1
    mov si, [bx+hd_itab]
    mov di, [hd_lineptr]
    call hd_scat
    mov di, [hd_lineptr]
    mov byte [di], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_size - AX sectors as '31M' or '970K' at [hd_lineptr] (module internal)
;
; KB as well as MB, because SPEC.md 52.2.1's alignment gaps are real
; partitions: a disk repartitioned by a modern tool leaves ~1,985 sectors at
; the front, and a size column reading '0M' looks like a broken tool rather
; than a small volume.
; -----------------------------------------------------------------------------
hd_iw_size:
    push ax
    push bx
    push dx
    push si
    mov di, [hd_lineptr]
    cmp ax, 2048
    jae .mb
    shr ax, 1                   ; sectors / 2 = KB
    call hd_utoa
    mov [hd_lineptr], di
    mov si, hd_s_ik
    call hd_scat
    jmp short .out
.mb:
    xor dx, dx
    mov bx, 2048
    div bx
    call hd_utoa
    mov [hd_lineptr], di
    mov si, hd_s_mb
    call hd_scat
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_click - W_ONCLICK.  in: BX = window, CX = x, DX = y, content-relative
; -----------------------------------------------------------------------------
hd_iw_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si                  ; W_ONCLICK's CX/DX are SCREEN coordinates and
    mov si, dx                  ; the window is in SI - tool.inc's conversion,
    call OSAPI_WM_CONTENT       ; and the reason this file had none was an
    mov di, ax                  ; assumption that they arrive content-relative.
    mov bp, dx                  ; They do not, and every y then compared past
    sub cx, di                  ; the button row, so nothing was ever hit
    mov dx, si
    sub dx, bp
    mov [hd_iclx], cx
    mov [hd_icly], dx

    cmp dx, HIW_BY - 2
    jl .rows
    cmp dx, HIW_BY + HIW_BH
    jge .done
    mov cx, [hd_iclx]
    cmp cx, HIW_B0X
    jl .done
    cmp cx, HIW_B0X + HIW_BW0
    jl .install
    cmp cx, HIW_B1X
    jl .done
    cmp cx, HIW_B1X + HIW_BW1
    jge .done
    call hd_iw_shut
    jmp short .done
.install:
    call hd_inst_ok
    jc .refused
    call hd_inst_step
    jmp short .repaint
.rows:
    mov ax, [hd_icly]
    sub ax, HIW_R0Y - 2
    js .done
    mov bl, HIW_ROWH
    div bl                      ; AL = the row
    cmp al, 4
    jae .done
    cmp al, [hd_isel]
    je .done
    mov [hd_isel], al
    mov byte [hd_iw_st], HII_IDLE   ; picking another row disarms, tool.inc's
    mov word [hd_imsg], hd_s_ipick  ; rule: anything but the same button again
    jmp short .repaint
.refused:
    ; A greyed control explains itself (SPEC.md 47 rule 5), so this says
    ; nothing new - it makes sure the reason is on screen.
    mov word [hd_imsg], hd_s_ibad
.repaint:
    call hd_iw_repaint
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
; hd_inst_step - one click of the stage machine (module internal)
; -----------------------------------------------------------------------------
hd_inst_step:
    push ax
    cmp byte [hd_iw_st], HII_ARMED
    je .go
    cmp byte [hd_iw_st], HII_SYSDONE
    je .apps
    mov byte [hd_iw_st], HII_ARMED  ; ask. There is no dialog to ask in, and a
    mov word [hd_imsg], hd_s_iarm   ; second click on the same button is the
    jmp short .out                  ; cheapest confirm that cannot be mistaken
                                    ; for the first (SPEC.md 52.2.3)
.go:
    call hd_inst_sys
    jc .failed
    call hd_iapps_find          ; ...and do not ask for a disk that is already
    jc .swap                    ; in the machine (SPEC.md 52.10.5). Falling
    mov [hd_iautod], al         ; straight into .apps is what makes the whole
                                ; install one confirm on a two-drive machine
    jmp short .apps
.swap:
    mov byte [hd_iw_st], HII_SYSDONE
    mov word [hd_imsg], hd_s_iswap
    jmp short .out
.apps:
    call hd_inst_apps
    jc .failed
    mov byte [hd_iw_st], HII_DONE
    mov word [hd_imsg], hd_s_idone
    jmp short .out
.failed:
    mov byte [hd_iw_st], HII_IDLE   ; [hd_imsg] already names what went wrong
.out:
    pop ax
    ret

; =============================================================================
; The copy itself
; =============================================================================

; -----------------------------------------------------------------------------
; hd_inst_sys - format the slot, then copy the disk that is in A: now
; out: CF=1 with [hd_imsg] naming what refused
; -----------------------------------------------------------------------------
hd_inst_sys:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call hd_ibuf_get            ; the buffer FIRST: a format we cannot follow
    jc .nomem                   ; with a copy is a wiped partition
    mov word [hd_imsg], hd_s_iwork
    call hd_iw_repaint          ; say so BEFORE the writes - a 32MB format is
                                ; hundreds of sectors and the user should not
                                ; watch a frozen window wonder
    call hd_ivol_bank           ; SPEC.md 51.5.2, whatever happens below
    call hd_inst_fmt
    jc .failed
    call hd_inst_mount
    jc .failed
    mov si, hd_s_kernel         ; KERNEL.SYS FIRST and alone, before the walk:
    call hd_icopy_one           ; SPEC.md 52.10.2 needs it at cluster 2
    jc .failed
    call hd_icopy_tree
    jc .failed
    call hd_ivol_back
    call hd_ibuf_put
    clc
    jmp short .out
.nomem:
    mov word [hd_imsg], hd_s_inomem
    stc
    jmp short .out
.failed:
    call hd_ivol_back
    call hd_ibuf_put
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_apps - copy the swapped disk, then commit (module internal)
; -----------------------------------------------------------------------------
hd_inst_apps:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call hd_ibuf_get
    jc .nomem
    mov word [hd_imsg], hd_s_iwork
    call hd_iw_repaint
    call hd_ivol_bank           ; ...which set hd_isrcdrv to the user's volume;
    mov al, [hd_iautod]         ; an auto-detected apps disk overrides THAT one
    cmp al, 0xFF                ; and never hd_ivdrv, or the restore below puts
    je .src                     ; the user on a drive they were never on
    mov [hd_isrcdrv], al
    mov byte [hd_iautod], 0xFF  ; spent: a second install asks afresh
.src:
    call hd_icopy_tree
    jc .failed
    call hd_ivol_back
    call hd_inst_vbr            ; the volume boot record...
    jc .failed2
    call hd_inst_mbr            ; ...and the MBR LAST: it is the commit
    jc .failed2
    call hd_ibuf_put
    clc
    jmp short .out
.nomem:
    mov word [hd_imsg], hd_s_inomem
    stc
    jmp short .out
.failed:
    call hd_ivol_back
.failed2:
    call hd_ibuf_put
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ibuf_get / hd_ibuf_put - the copy buffer (module internal)
;
; A heap claim rather than bss, for SPEC.md 50's reason: it is HIW_KMAXKB at
; its largest and the driver's image would carry that on every machine, hard
; disk or not. Refusal is a normal path - the caller says so and stops.
; -----------------------------------------------------------------------------
hd_ibuf_get:
    push ax
    push cx
    push dx
    mov ax, HIW_KMAXKB          ; THE SIZE GOES IN AX AND THE SEGMENT COMES
    call OSAPI_MEM_CLAIM        ; BACK IN DX (SPEC.md 50.3). This asked in CX
    jnc .got                    ; and read the answer out of AX, so the buffer
                                ; segment was whatever AX happened to hold -
                                ; 0x0047 on the machine that found it, which
                                ; is BELOW KERNEL_SEG. Reading 76KB of
                                ; KERNEL.SYS there writes over the interrupt
                                ; vector table, the BIOS data area and the
                                ; bottom of the kernel, which is why this
                                ; presented first as a crash to CS=0000:IP=0000
                                ; and then as an endless loop through damaged
                                ; kernel code. A claim can never be below the
                                ; heap base, so the value alone was the tell
    mov ax, HIW_CHUNKKB         ; not enough for the kernel in one piece: take
    call OSAPI_MEM_CLAIM        ; the chunk size and let hd_icopy_one refuse
    jc .no                      ; the one file that needs more
    mov word [hd_ibufkb], HIW_CHUNKKB
    jmp short .have
.got:
    mov word [hd_ibufkb], HIW_KMAXKB
.have:
    mov [hd_ibufseg], dx
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    mov word [hd_ibufseg], 0
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_ibufbytes - the buffer's capacity in BYTES, 32-bit (module internal)
; out: DX:AX; every other register preserved
;
; A routine rather than a shift because HIW_KMAXKB * 1024 does not fit a word,
; and the 16-bit version of this arithmetic is what made the installer refuse
; KERNEL.SYS outright.
; -----------------------------------------------------------------------------
hd_ibufbytes:
    push cx
    mov ax, [hd_ibufkb]
    mov cx, 1024
    mul cx                      ; DX:AX = KB * 1024
    pop cx
    ret

hd_ibuf_put:
    push ax
    push dx
    mov dx, [hd_ibufseg]        ; DX here too, for the same reason
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [hd_ibufseg], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ivol_bank / hd_ivol_back - the user's volume (SPEC.md 51.5.2)
; BOTH PRESERVE THE FLAGS: each caller's result is in CF and a failed restore
; must not read as a failed install.
; -----------------------------------------------------------------------------
hd_ivol_bank:
    pushf
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE        ; DX = the directory, BL = the drive
    mov [hd_ivcwd], dx
    mov [hd_ivdrv], bl
    mov [hd_isrcdrv], bl        ; the copy reads from there too, unless the
                                ; caller overrides it after this returns
    pop dx
    pop bx
    pop ax
    popf
    ret

hd_ivol_back:
    pushf
    push ax
    push bx
    push dx
    mov dx, [hd_ivcwd]
    mov bl, [hd_ivdrv]
    call OSAPI_FILE_GOTO        ; a failed restore is SWALLOWED by the popf,
    pop dx                      ; deliberately - drv_vol_back's rule
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; hd_iapps_find - is the apps disk already in the OTHER floppy drive?
; in:  the volume is wherever hd_inst_sys left it (the user's, restored)
; out: CF = 0 and AL = that drive; CF = 1 = ask the user to swap
; clobbers: AX, flags. The volume is put back on EVERY path.
;
; A one-drive machine has to be asked to swap disks and a two-drive machine
; usually does not, because the disk is already sitting in B:. int 11h is the
; only honest source for how many drives there are - bits 7:6 are the count
; less one, and the kernel's volume table is no help at all, since A: and B:
; are unconditional rows in it and on a one-drive PC the BIOS aliases unit 1
; onto the same physical drive.
;
; The test for the disk itself is an APPS FOLDER in its root, which is what
; makes it the apps disk (SPEC.md 19.2) and what the system disk does not
; have. Nothing here is a guess: if the folder is not found - a renamed disk,
; a data disk, no disk at all - this refuses and the swap prompt happens
; exactly as it did before.
; -----------------------------------------------------------------------------
hd_iapps_find:
    push bx
    push cx
    push dx
    push si
    push di
    int 0x11                    ; the equipment word
    test al, 1                  ; ...any floppy drive at all
    jz .no
    mov cl, 6
    shr al, cl                  ; bits 7:6 = drives - 1
    or al, al
    jz .no                      ; one drive: there is nowhere else to look
    mov bl, [hd_ivdrv]          ; the drive stage 1 read from
    cmp bl, 2
    jae .no                     ; not a floppy at all: "the other one" has no
    xor bl, 1                   ; meaning, so ask
    push bx
    xor dx, dx                  ; its ROOT
    call OSAPI_FILE_GOTO
    pop bx
    jc .no                      ; no disk in it, or one we cannot read
    xor cx, cx
.scan:
    push es
    push cs
    pop es
    mov di, hd_ifind            ; NOT hd_ient: the tree walk owns that one and
    call OSAPI_FILE_FIND        ; this runs between two of its passes
    pop es
    jc .no
    cmp word [hd_ifind+14], 2   ; a folder, and not the synthesized '..' (3)
    jne .scan
    mov si, hd_ifind
    mov di, hd_s_iapps
    call hd_iname_same
    jnc .scan
    call hd_ivol_back           ; found it - and the volume goes back either
    mov al, bl                  ; way, because hd_inst_apps banks afresh
    clc
    jmp short .out
.no:
    call hd_ivol_back
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_isrc / hd_isrc_sub / hd_idst / hd_idst_sub - stand somewhere (internal)
;
; A driver has no folder of its own and uses the machine's (SPEC.md 19.2.1),
; so these move the GLOBAL volume and every file call afterwards resolves
; there. Each is a REMOUNT, which is why the copy is structured to make as
; few of them as it can - one per file, not one per chunk.
; -----------------------------------------------------------------------------
hd_isrc:
    push ax
    push bx
    push dx
    mov bl, [hd_isrcdrv]        ; the source is where the user was: A: on a
    xor dx, dx                  ; one-floppy machine, at its ROOT
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

hd_isrc_sub:
    push ax
    push bx
    push dx
    mov bl, [hd_isrcdrv]
    mov dx, [hd_isubc]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

hd_idst:
    push ax
    push bx
    push dx
    mov bl, [hd_idrv]
    xor dx, dx
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

hd_idst_sub:
    push ax
    push bx
    push dx
    mov bl, [hd_idrv]
    mov dx, [hd_idsubc]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_icopy_tree - the current source volume's root, and one folder level
;
; Root files, then one level of folders, which is what os88disk.py builds and
; what both shipped disks are (SPEC.md 19.2). Deeper nesting is NOT walked and
; that is a stated limit rather than an oversight: this runs on the UI task's
; stack and open recursion is how you overrun one.
; -----------------------------------------------------------------------------
hd_icopy_tree:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor cx, cx
    mov [hd_iord], cx
.entry:
    call hd_isrc                ; back to the source root: the copy moved us
    jc .fail
    mov cx, [hd_iord]
    push es
    push cs
    pop es
    mov di, hd_ient
    call OSAPI_FILE_FIND        ; the walk is stateless and this is all of its
    pop es                      ; state (SPEC.md 19.7.1)
    jc .done                    ; FERR_NOENT: the end of the directory
    mov [hd_iord], cx
    mov ax, [hd_ient+14]
    cmp ax, 3
    je .entry                   ; the synthesized '..' is not ours to copy
    cmp ax, 2
    je .folder
    mov si, hd_ient             ; ...and NEITHER IS KERNEL.SYS, which
    mov di, hd_s_kernel         ; hd_inst_sys has already written FIRST so
    call hd_iname_same          ; that it lands on cluster 2 (SPEC.md
    jc .entry                   ; 52.10.2). Copying it again is a REPLACE: it
                                ; allocates a fresh chain at the end of the
                                ; volume and frees the one at the front, so
                                ; the kernel ended up at cluster 40 with
                                ; MEDIA sitting on cluster 2 - a volume that
                                ; is perfectly good and cannot be booted,
                                ; because the boot sector reads a flat run
                                ; from the start of the data area rather than
                                ; walking a chain
    mov si, hd_ient
    call hd_icopy_one
    jc .fail
    jmp short .entry
.folder:
    mov ax, [hd_ient+16]
    mov [hd_isubc], ax          ; its cluster on the SOURCE, for the descent
    call hd_idst
    jc .fail
    mov si, hd_ient
    call OSAPI_FILE_MKDIR       ; make it on the destination (SPEC.md 18.5.1)
    jc .mkchk
.mkok:
    call hd_icopy_sub
    jc .fail
    jmp short .entry
.mkchk:
    cmp ax, FERR_EXIST          ; a re-install finds its own folders: that is
    je .mkok                    ; not a failure, it is the second time
    jmp short .fail
.done:
    clc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ierr
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_icopy_sub - the FILES of one folder (module internal)
; in:  [hd_isubc] = its cluster on the source, hd_ient = its entry
; -----------------------------------------------------------------------------
hd_icopy_sub:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, hd_ient             ; bank its name in hd_iname2, NOT hd_iname:
    mov di, hd_iname2           ; hd_icopy_one overwrites that one with each
    mov cx, 13                  ; FILE's name, and hd_idst_findsub needs the
    call hd_iname_copy          ; FOLDER's to find it on the destination
    xor cx, cx
    mov [hd_isord], cx
.entry:
    call hd_isrc_sub
    jc .fail
    mov cx, [hd_isord]
    push es
    push cs
    pop es
    mov di, hd_ient
    call OSAPI_FILE_FIND
    pop es
    jc .done
    mov [hd_isord], cx
    mov ax, [hd_ient+14]
    cmp ax, 1
    jne .entry                  ; FILES only one level down - see the header
    mov byte [hd_iinsub], 1
    mov si, hd_ient
    call hd_icopy_one
    mov byte [hd_iinsub], 0
    jc .fail
    jmp short .entry
.done:
    clc
    jmp short .out
.fail:
    mov byte [hd_iinsub], 0
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_icopy_one - copy ONE file, source folder current on entry
; in:  SI -> its NUL name (hd_ient's, or a literal for KERNEL.SYS)
; out: CF=1 and [hd_imsg] set
;
; Two shapes, and the split is SPEC.md 18.4.4's: a file that fits the buffer
; is one read and one write, and a bigger one is a run of chunked takes. The
; chunk is the BUFFER, which is a whole number of KB and therefore of clusters
; on every geometry this kernel mounts - the rule that has to hold at BOTH
; ends, since the destination's cluster size need not equal the source's.
;
; A SYSTEM file goes through OSAPI_FILE_WRITE_SYS (SPEC.md 19.6.1) so it lands
; hidden and system exactly as it is on the floppy. That path cannot chunk -
; append refuses a protected file - so a system file has to fit the buffer,
; which is why hd_ibuf_get asks for HIW_KMAXKB first and KERNEL.SYS is the one
; file that could ever need it.
; -----------------------------------------------------------------------------
hd_icopy_one:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, hd_iname            ; bank the name: every volume switch below
    mov cx, 13                  ; runs through code that rewrites hd_ient
    call hd_iname_copy
    call hd_isrc_here           ; ...and STAND IN THE SOURCE before asking about
    jc .fail                    ; it. The tree walk arrives here already there,
                                ; but hd_inst_sys's KERNEL.SYS call arrives
                                ; straight off the mount with the DESTINATION
                                ; current - where the file it is looking for is
                                ; exactly what does not exist yet
    xor ax, ax
    mov [hd_ioff], ax
    mov [hd_ioff+2], ax
    mov [hd_isys], al

    mov si, hd_iname            ; how big is it, and is it a system file?
    call hd_istat
    jc .fail

    ; Does it fit the buffer whole? A 32-BIT compare, and it has to be: the
    ; buffer is up to HIW_KMAXKB and `kb << 10` overflows a word at 64KB, so
    ; the 16-bit version made every file of 64KB or more take the chunked path
    ; - including KERNEL.SYS, which is a system file and cannot be appended to
    ; (SPEC.md 18.4.4). It refused the one file the whole feature exists for.
    call hd_ibufbytes           ; DX:AX = the buffer's capacity
    cmp dx, [hd_isize+2]
    ja .whole
    jb .chunked
    cmp ax, [hd_isize]
    jb .chunked
.whole:

    ; --- the whole-file shape ------------------------------------------------
    call hd_iread_whole
    jc .fail
    call hd_idst_here
    jc .fail
    call hd_iwrite_whole
    jc .fail
    jmp short .ok

    ; --- and the chunked one (SPEC.md 18.4.4) --------------------------------
.chunked:
    cmp byte [hd_isys], 0
    jne .toobig                 ; a system file cannot be appended to
.chunk:
    call hd_isrc_here
    jc .fail
    call hd_iread_chunk         ; AX = bytes taken, 0 = the file is finished
    jc .fail
    or ax, ax
    jz .ok
    mov [hd_itake], ax
    call hd_idst_here
    jc .fail
    call hd_iwrite_chunk
    jc .fail
    mov ax, [hd_itake]          ; the offset advances by what was TAKEN, and
    add [hd_ioff], ax           ; the next read starts exactly there
    adc word [hd_ioff+2], 0
    jmp short .chunk
.ok:
    clc
    jmp short .out
.toobig:
    mov word [hd_imsg], hd_s_ibig
    stc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ierr
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_istat - the named file's size and species (module internal)
; in:  SI = the name, source folder current
; out: CF=0 with [hd_isize] (32-bit) and [hd_isys]; CF=1 = it is not there
;
; By FIND rather than by a stat call, because there is no stat slot and
; because FIND is what already answers both questions: the 32-bit size (the
; listing's is clamped to a word) and the attribute that says whether this is
; a system file (SPEC.md 19.7.1).
; -----------------------------------------------------------------------------
hd_istat:
    push bx
    push cx
    push dx
    push si
    push di
    mov [hd_iwant], si
    xor cx, cx
.next:
    push es
    push cs
    pop es
    mov di, hd_ifind
    call OSAPI_FILE_FIND
    pop es
    jc .no
    push si
    push di
    mov si, [hd_iwant]
    mov di, hd_ifind
    call hd_iname_same
    pop di
    pop si
    jnc .next
    mov ax, [hd_ifind+18]
    mov [hd_isize], ax
    mov ax, [hd_ifind+20]
    mov [hd_isize+2], ax
    mov al, [hd_ifind+13]
    and al, 0x06                ; hidden|system: SPEC.md 19.6's species
    mov [hd_isys], al
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_iread_whole / hd_iwrite_whole - the small-file shape (module internal)
; -----------------------------------------------------------------------------
hd_iread_whole:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [hd_ibufseg]
    xor bx, bx
    mov si, hd_iname
    call hd_ibufbytes           ; DX:AX = the capacity, 32-bit
    mov cx, ax                  ; ...and DX:CX is what the slot takes
    call OSAPI_FILE_READ
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_iwrite_whole:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [hd_ibufseg]
    xor bx, bx
    mov si, hd_iname
    mov cx, [hd_isize]
    mov dx, [hd_isize+2]
    cmp byte [hd_isys], 0
    jne .sys
    call OSAPI_FILE_WRITE
    jmp short .out
.sys:
    call OSAPI_FILE_WRITE_SYS   ; hidden + system, as it is on the floppy
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iread_chunk - one take at [hd_ioff] (module internal)
; out: CF=0 with AX = bytes delivered, 0 = finished
; -----------------------------------------------------------------------------
hd_iread_chunk:
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [hd_ibufseg]
    xor bx, bx
    mov si, hd_iname
    call hd_ibufbytes           ; DX:AX = the capacity...
    mov cx, ax                  ; ...and CX is what READ_AT takes, a cluster
                                ; multiple by construction (SPEC.md 18.4.4).
                                ; The chunk path only runs for buffers under
                                ; 64KB, so the high half is 0 here by the same
                                ; construction that makes HIW_CHUNKKB 32
    mov ax, [hd_ioff]
    mov dx, [hd_ioff+2]
    call OSAPI_FILE_READ_AT     ; out DX:AX = the bytes, 0 at the end
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_iwrite_chunk - the first take CREATES, every later one APPENDS
; -----------------------------------------------------------------------------
hd_iwrite_chunk:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [hd_ibufseg]
    xor bx, bx
    mov si, hd_iname
    mov cx, [hd_itake]
    mov ax, [hd_ioff]
    or ax, [hd_ioff+2]
    jnz .append
    xor dx, dx
    call OSAPI_FILE_WRITE       ; offset 0: create (or replace, on a re-run)
    jmp short .out
.append:
    call OSAPI_FILE_APPEND      ; every later chunk, SPEC.md 18.4.4
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_isrc_here / hd_idst_here - the folder this file lives in (module internal)
; A root file goes to the root; a file inside a folder goes to that folder on
; both volumes, which [hd_iinsub] is what distinguishes.
; -----------------------------------------------------------------------------
hd_isrc_here:
    cmp byte [hd_iinsub], 0
    jne hd_isrc_sub
    jmp hd_isrc

hd_idst_here:
    cmp byte [hd_iinsub], 0
    jne hd_idst_findsub
    jmp hd_idst

; The destination folder's cluster is not known until it has been made, and a
; re-install finds one that already exists - so it is LOOKED UP by name rather
; than remembered from the mkdir, which would have to distinguish those cases.
hd_idst_findsub:
    push ax
    push cx
    push dx
    push si
    push di
    call hd_idst
    jc .out
    xor cx, cx
.next:
    push es
    push cs
    pop es
    mov di, hd_ifind
    call OSAPI_FILE_FIND
    pop es
    jc .out                     ; CF=1: the folder is not there, which is a
    mov si, hd_iname2           ; real failure and the caller reports it
    push di
    mov di, hd_ifind
    call hd_iname_same
    pop di
    jnc .next
    mov ax, [hd_ifind+16]
    mov [hd_idsubc], ax
    call hd_idst_sub
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iname_copy - CX bytes from SI to DI (module internal, NUL-safe)
; hd_iname_same - CF=1 when the NUL strings at SI and DI match
; -----------------------------------------------------------------------------
hd_iname_copy:
    push ax
    push cx
    push si
    push di
.next:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .done
    loop .next
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

hd_iname_same:
    push ax
    push si
    push di
.next:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .next
.yes:
    pop di
    pop si
    pop ax
    stc
    ret
.no:
    pop di
    pop si
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_inst_fmt - partition and format the selected slot (module internal)
; The formatter's own path (SPEC.md 52.3), reached exactly as tool.inc's
; Format reaches it: the entry first, then the volume, boot sector last.
; -----------------------------------------------------------------------------
hd_inst_fmt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_sel]
    call hd_dev_row
    mov al, [hd_isel]
    call hd_slot_extent
    jc .fail
    call hd_fmt_plan
    jc .fail
    mov al, [hd_isel]
    call hd_part_ent
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
    mov byte [si+HP_STATUS], 0  ; NOT active yet: the MBR is the commit and it
                                ; is written at the very end (SPEC.md 52.10.4)
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_chs
    call hd_part_write
    jc .fail
    mov al, [hd_sel]
    call hd_dev_row
    call hd_fmt_run
    jc .fail
    clc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ifmt
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_mount - mount what we just made, and remember its volume (internal)
; out: [hd_idrv] = the kernel's volume index for the destination
; -----------------------------------------------------------------------------
hd_inst_mount:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_isel]
    call hd_tw_unmount_slot     ; a stale volume on this slot names a FAT the
                                ; format has just replaced
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_load           ; the table as the format left it
    mov al, [hd_isel]
    mov [hd_pslot], al          ; hd_mount_one reads the SLOT from here and
    call hd_part_ent            ; the entry from SI - it answers no volume
    call hd_mount_one           ; index, so the row is looked up below

    ; Which kernel volume did it become? hd_mount_one publishes that in the
    ; hd_vols row it filled, and the row is found the way hd_mounted finds
    ; one - by device - narrowed by the partition slot, because this device
    ; may have several volumes on it and only one of them is ours.
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, [hd_sel]
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, [hd_isel]
    je .found
.next:
    add bx, HDV_SIZE
    loop .scan
    mov word [hd_imsg], hd_s_imnt
    stc
    jmp short .out
.found:
    mov al, [bx+HDV_VOL]
    mov [hd_idrv], al
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_vbr - the volume boot record (SPEC.md 52.10.2)
;
; boot/boothd.asm's 512 bytes with THIS volume's BPB written over the first
; 62, which is os88disk.py's split done in the driver: bytes 0..2 and 62..511
; are the sector's own code and are not ours to touch.
;
; The kernel's sector count goes into the word at BOOTHD_KSECS. That is the
; one patch site and it is pinned at a documented offset precisely so this
; code can find it without knowing anything else about the sector's layout.
; -----------------------------------------------------------------------------
hd_inst_vbr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov si, hd_bootvbr          ; our template -> the sector buffer
    mov di, hd_sec0
    mov cx, 512
    push ds
    pop es
    cld
    rep movsb

    ; --- the BPB, from the volume we just formatted -------------------------
    mov word [hd_sec0+HF_B_BPS], 512
    mov ax, [hd_fspc]
    mov [hd_sec0+HF_B_SPC], al
    mov word [hd_sec0+HF_B_RSVD], HF_RSVD
    mov byte [hd_sec0+HF_B_NFATS], HF_NFATS
    mov word [hd_sec0+HF_B_ROOTN], HF_ROOTENT
    mov ax, [hd_fsecs]
    mov [hd_sec0+HF_B_TOT16], ax
    mov byte [hd_sec0+HF_B_MEDIA], HF_MEDIA
    mov ax, [hd_ffatsz]
    mov [hd_sec0+HF_B_FATSZ], ax
    mov al, [hd_sel]
    call hd_dev_row
    mov ax, [di+HDD_SPT]
    mov [hd_sec0+HF_B_SPT], ax
    mov ax, [di+HDD_HEADS]
    mov [hd_sec0+HF_B_HEADS], ax
    mov ax, [hd_fbase]              ; BPB_HiddSec: the partition base, and
    mov [hd_sec0+HF_B_HIDD], ax     ; what the sector adds to every LBA
    mov ax, [hd_fbase+2]
    mov [hd_sec0+HF_B_HIDD+2], ax
    mov byte [hd_sec0+HF_B_DRVNUM], 0x80
    mov byte [hd_sec0+HF_B_BOOTSIG], 0x29

    ; --- and the kernel's length, at the pinned patch site -------------------
    call hd_iksecs                  ; AX = sectors of KERNEL.SYS
    jc .fail
    mov [hd_sec0+BOOTHD_KSECS], ax

    mov al, [hd_sel]
    call hd_dev_row
    mov byte [hd_rawop], 1          ; write
    mov ax, [hd_fbase]              ; volume LBA 0, absolute
    mov dx, [hd_fbase+2]
    mov bx, hd_sec0
    mov cx, 1
    push ds
    pop es
    call hd_raw
    jc .fail
    clc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ierr
    stc
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
; hd_iksecs - how many sectors KERNEL.SYS occupies on the DESTINATION
; out: CF=0 and AX = the count
;
; Asked of the destination rather than remembered from the copy, because that
; is the volume the boot sector will read it from and the question is about
; the file that is actually there - a re-install over a shorter kernel would
; otherwise patch the previous one's length.
; -----------------------------------------------------------------------------
hd_iksecs:
    push bx
    push cx
    push dx
    push si
    call hd_ivol_bank
    call hd_idst
    jc .fail
    mov si, hd_s_kernel
    call hd_istat
    jc .fail
    mov ax, [hd_isize]
    mov dx, [hd_isize+2]
    add ax, 511                     ; ceil(size / 512), in 32 bits: a kernel
    adc dx, 0                       ; near 64KB would carry out of a word
    mov cl, 9
.shr:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .shr
    call hd_ivol_back
    clc
    jmp short .out
.fail:
    call hd_ivol_back
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_inst_mbr - the boot code and the active flag, LAST (SPEC.md 52.10.4)
;
; This is the commit. Everything before it leaves a disk that is merely not
; bootable yet; this one sector is what makes the machine start from it, so
; an install interrupted anywhere earlier still boots from its floppy.
;
; The table is PRESERVED - only bytes 0..445 and the four status bytes change
; - because the other three entries may be somebody else's partitions.
; -----------------------------------------------------------------------------
hd_inst_mbr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_load           ; re-read: the format wrote the table and this
    jc .fail                    ; must not put a stale copy back

    mov si, hd_bootstub         ; the chain-loader over the boot code only
    mov di, hd_mbr
    mov cx, HD_MBRCODE
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es

    xor cl, cl                  ; exactly one entry is active
.flag:
    mov al, cl
    call hd_part_ent
    mov byte [si+HP_STATUS], 0
    cmp cl, [hd_isel]
    jne .fnext
    mov byte [si+HP_STATUS], 0x80
.fnext:
    inc cl
    cmp cl, 4
    jb .flag

    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_write
    jc .fail
    clc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ierr
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Strings and state
;
; EVERY ONE OF THESE FITS ITS BOX. The window is 300px wide, so a caption is
; at most 36 characters at HIW_LX - page.inc's rule, and for its reason:
; font_str clips to the SCREEN and not to the window (SPEC.md 5), so a long
; one paints over the border and out the other side.
; =============================================================================
hd_s_ititle:  db 'Install', 0
hd_s_ihead:   db 'Install to        Size   State', 0
hd_s_iinst:   db 'Install', 0
hd_s_islot:   db 'Slot ', 0
hd_s_igap:    db '    ', 0
hd_s_idash:   db '   -', 0
hd_s_ik:      db 'K', 0
hd_s_kernel:  db 'KERNEL.SYS', 0
hd_s_iapps:   db 'APPS', 0      ; what makes a disk THE APPS DISK (52.10.5)

hd_s_iready:  db 'Ready', 0
hd_s_ismall:  db 'Too Small', 0
hd_s_ialien:  db 'Unusable', 0
hd_s_inone:   db 'No Room', 0
hd_itab:      dw hd_s_iready, hd_s_ismall, hd_s_ialien, hd_s_inone

hd_s_ipick:   db 'Pick a partition, then Install', 0
hd_s_iarm:    db 'Erases it - Install again to confirm', 0
hd_s_iwork:   db 'Copying - this takes a few minutes', 0
hd_s_iswap:   db 'Now insert the APPS disk, then Install', 0
hd_s_idone:   db 'Done - restart with no floppy in A:', 0
hd_s_ibad:    db 'That partition cannot be used', 0
hd_s_inomem:  db 'Not enough memory to copy', 0
hd_s_ifmt:    db 'Could not format that partition', 0
hd_s_imnt:    db 'Formatted, but it would not mount', 0
hd_s_ierr:    db 'Disk error - the install stopped', 0
hd_s_ibig:    db 'A file is too big for this machine', 0

hd_iwin:      dw 0              ; .text with a real initialiser: -f bin zeroes
                                ; nothing, and a garbage window pointer here
                                ; is a paint through whatever it names
hd_iw_st:     db 0
hd_isel:      db 0
hd_imsg:      dw hd_s_ipick
hd_istate:    times 4 db 0
hd_isecs:     times 4 dw 0
hd_irow:      db 0
hd_iclx:      dw 0
hd_icly:      dw 0
hd_ibx:       dw 0
hd_ibw:       dw 0
hd_ibl:       dw 0
hd_ibufseg:   dw 0
hd_ibufkb:    dw 0
hd_idrv:      db 0              ; the destination's kernel volume index
hd_ivdrv:     db 0              ; ...and the user's, banked
hd_ivcwd:     dw 0
hd_isrcdrv:   db 0              ; the drive the COPY reads from, which is the
                                ; banked one on a one-drive machine and the
                                ; other floppy when hd_iapps_find has found the
                                ; apps disk already in it (SPEC.md 52.10.5).
                                ; Separate from hd_ivdrv because that one is
                                ; what hd_ivol_back puts the user on, and the
                                ; two must not be the same word
hd_iautod:    db 0xFF           ; the auto-detected apps drive, 0xFF = none.
                                ; .text with a real initialiser, hd_iwin's
                                ; reason: -f bin zeroes nothing and 0 here is
                                ; drive A:
hd_iord:      dw 0              ; the root walk's ordinal (SPEC.md 19.7.1)
hd_isord:     dw 0              ; ...and the sub-folder walk's
hd_isubc:     dw 0              ; the source folder's cluster
hd_idsubc:    dw 0              ; ...and the destination folder's
hd_iinsub:    db 0              ; 1 = the file being copied is in a folder
hd_ient:      times OSAPI_FIND_SZ db 0
hd_ifind:     times OSAPI_FIND_SZ db 0
hd_iname:     times 14 db 0
hd_iname2:    times 14 db 0
hd_iwant:     dw 0
hd_isize:     dd 0
hd_isys:      db 0
hd_ioff:      dd 0
hd_itake:     dw 0
