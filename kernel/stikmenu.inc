; =============================================================================
; os8088 - stikmenu.inc
;
; THE STICKY APPS MENU (SPEC.md 110.12): Locator's bar cell that lists every
; Sticky App it can find - each by the CARD its package carries in its clear
; prefix (header flags bit 7: a name, a description and a 32x32 picture) -
; says which are running, and starts, stops, hides, shows and arranges them.
; Prefix `stm_` for the resident half, `stm_`/`smn_` in the image.
;
; THE SHAPE IS DOCK.DRV'S LESSON (SPEC.md 2.8). What is resident is the bar
; cell (menu.inc), menu_track's three-instruction test for it, one CMD_* and
; the two cold thunks below. Everything that touches a disk, draws or decides
; is STIKMENU.DRV, loaded when the cell is pressed and dropped when the menu
; is done with - so a machine that never opens the menu pays the thunks and a
; title, and one that does pays nothing once it has closed.
;
; WHY IT QUALIFIES (docs/plans/completed/ONDEMAND-PLAN.md 1): the menu's
; whole job is to read STIKAPPS\ off the system disk, so the disk is required
; already, and the load is inside the gesture that needs it.
;
; TWO HALVES, BECAUSE A LAUNCH NEEDS THE LOCK FREE. menu_track runs with the
; gfx lock held (ui_lcall), and the loader takes the lock itself (SPEC.md
; 21.5): a launch from the tracker would hang the machine. So the TRACK entry
; scans, draws, follows the pointer and puts the screen back; a pick is
; recorded in the image and POSTED as CMD_STIK (ui_post_cmd, SPEC.md 13), and
; ui_cmd's ACT entry - UI task, nothing held - does it and drops the image.
; Nothing picked: the track thunk drops it at once.
;
; DS = KERNEL_SEG on every entry, so the kernel's data is plain references
; and the image's own is `[cs:...]` (SPEC.md 2.8.6). Kernel code is reached
; through resident far entries, `wm_cbd` for a cold near body, and SMK - this
; image's copy of extmod.inc's EXK - for a .text near one.
; =============================================================================

; --- the card, as this menu reads it (SPEC.md 110.12). tools/os88pkg.py's
; STIK_* are the same numbers and tests/unit/t_stikcard.py holds them equal.
STM_C_NAME    equ 4             ; the name, NUL-padded...
STM_C_NAMEMAX equ 24            ; ...in a field this long
STM_C_DESC    equ 28            ; the description...
STM_C_DESCMAX equ 36            ; ...likewise
STM_C_PIC     equ 64            ; the 32x32 picture, 4 bytes a row
STM_C_SIZE    equ 192

%ifdef KERN_BIG
SMN_NENT    equ 2               ; STIKMENU.DRV's mod_fp entries, in SMNFP:
SMN_TRACK   equ 0               ;   the drop: scan, draw, track, post
SMN_ACT     equ 4               ;   the posted pick, with nothing held

section .cold
; -----------------------------------------------------------------------------
; stm_track_x - the Sticky Apps cell was pressed (SPEC.md 110.12), far
; in:  BX = the cell's left x; the gfx lock held (menu_track), its title lit
; out: AX = 0FFFFh - nothing for ui_dispatch, on every path: a pick is POSTED
; clobbers: BX, CX, DX, SI, DI (menu_track banked all five)
;
; The image is held past the return only when the track posted a pick, for
; stm_act_x to spend; a refused load says why (hbf_nodisk: `Needs Sys Disk
; A:`, the Control Panel's own sentence) and drops nothing it does not hold.
; -----------------------------------------------------------------------------
stm_track_x:
    mov al, MOD_STIKM
    call mod_need               ; inside the lock: sc_modcall's precedent
    jc .nodisk
    call far [SMNFP + SMN_TRACK] ; CF = 0 a pick is posted: keep the image
    jnc .out
    mov al, MOD_STIKM
    call mod_drop
.out:
    mov ax, 0xFFFF
    retf
.nodisk:
    call hbf_nodisk
    jmp short .out

; -----------------------------------------------------------------------------
; stm_act_x - ui_cmd's CMD_STIK: do the posted pick, then drop the image
; in:  nothing; UI task, no lock held. out: nothing; AX preserved (ui_cmd
;      clobbers it anyway, but the far call below is the image's to keep)
; With no image held - the post outlived it, which nothing here does - the
; slot is mod_gone's refusal and the drop finds no claim.
; -----------------------------------------------------------------------------
stm_act_x:
    push ax
    call far [SMNFP + SMN_ACT]
    mov al, MOD_STIKM
    call mod_drop
    pop ax
    retf
section .text

; =============================================================================
; STIKMENU.DRV - the module image (SPEC.md 110.12)
; =============================================================================
section .modn
modn_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER, MOD_STIKM       ; MOD_H_VER, MOD_H_ID
    dw BUILD_NUM, MOD_STAMP     ; MOD_H_BUILD, MOD_H_STAMP (SPEC.md 2.8.2)
    dw modn_end, SMN_NENT       ; MOD_H_IMG, MOD_H_NENT
    dw smn_track, smn_act

; SMK name - far-call the KERNEL routine `name` (a near proc in .text) from
; this image: winmod.inc's WNK, whose wnk_go lives in WIN95.DRV's image and
; so cannot be shared.
%macro SMK 1
    call smk_go
    dw %1
%endmacro

; --- what the menu holds, and its geometry ------------------------------------
STM_MAX     equ 64              ; Sticky Apps listed at most, PAGED: the
                                ; panel shows what the screen holds and a
                                ; pager row turns the page (SPEC.md 110.12)
STM_PGMAX   equ 8               ; App slots on one page, at most
STE_VOL     equ 0               ; byte: the volume it was found on
STE_RUN     equ 1               ; byte: 1 = an instance of it is running
STE_FILE    equ 2               ; 13: its 8.3 name in the STIKAPPS folder
STE_HNAME   equ 15              ; 16: its header's program name (+16), which is
                                ; what an instance record carries (I_NAME)
STE_NAME    equ 31              ; 24: its card's name...
STE_DESC    equ 55              ; 36: ...and description - the TEXT of the
                                ; card. The PICTURE is read again for the page
                                ; on show (stm_pics), so an entry is 96 bytes
                                ; and not 224, and 64 of them fit the image
STE_CLUS    equ 91              ; word: its first cluster, for that re-read
STE_SIZE    equ 96
STM_SLMAX   equ 18              ; head, rule, 8 apps, pager, rule, 3, rule, hint
SL_KIND     equ 0               ; byte: SK_*
SL_PARAM    equ 1               ; byte: the entry, or the IT_* item
SL_FLAGS    equ 2               ; byte: SF_*
SL_H        equ 3               ; byte: rows
SL_Y        equ 4               ; word: screen y of its first row
SL_SIZE     equ 6
SK_HEAD     equ 0
SK_RULE     equ 1
SK_APP      equ 2
SK_ITEM     equ 3
SK_HINT     equ 4
SK_NONE     equ 5
SK_PAGE     equ 6               ; the pager: an ITEM's height
SF_SEL      equ 1               ; a pick: highlighted under the pointer
SF_DIS      equ 2               ; greyed
IT_ARR      equ 0               ; Arrange Sticky Apps...
IT_HIDE     equ 1               ; Hide All / Show All Sticky Apps
IT_MORE     equ 2               ; More Sticky Apps...
AK_NONE     equ 0               ; [stm_ak], the posted pick
AK_LAUNCH   equ 1
AK_CLOSE    equ 2
AK_HIDE     equ 3
AK_SHOW     equ 4
AK_ARR      equ 5
AK_MORE     equ 6
STY_BOLD    equ 1               ; stm_text's DL
STY_GREY    equ 2
STM_STRMAX  equ 52              ; the widest row, in bytes: TALL's 8 + 35 + 9
STM_SLABMAX equ 56              ; ...and the tallest slab, TALL's App
STM_SAVEMAX equ 0xF000          ; the save-under's ceiling, bytes: inside one
                                ; segment, so gfx_save cannot wrap
; A LAYOUT, LY_SIZE bytes, picked by the primary's height as the widget's own
; are (SPEC.md 110.11): TALL at 400 rows or more (VGA) - the owner's mockup's
; air: a 56-row App, 18-row items, two-cell margins - MID at 300-399
; (Hercules, EGA) and COMPACT below that (CGA). Every row is relative to its
; own slab's first; every column is a BYTE of the band.
LY_HT       equ 0               ; 6: the slab heights, by SK_*
LY_HDR1     equ 6               ; the header's first line
LY_HDR2     equ 7               ; ...and its second
LY_ITEM     equ 8               ; an item's line
LY_NONE     equ 9               ; 'No Sticky Apps found'
LY_PIC      equ 10              ; the picture's first row
LY_NAME     equ 11              ; the name
LY_DESC     equ 12              ; the description
LY_BTXT     equ 13              ; 'On' / 'Add'
LY_BTOP     equ 14              ; the Add box's top row
LY_BH       equ 15              ; ...and its rows
LY_HINT     equ 16              ; the hint's line
LY_PICX     equ 17              ; the picture's column (byte)
LY_TXTX     equ 18              ; the name's and the description's
LY_HDRX     equ 19              ; the header's, the hint's and NONE's
LY_ITEMX    equ 20              ; the three items'
LY_RFURN    equ 21              ; bytes right of the text: gap, On/Add, frame
LY_SIZE     equ 22

; -----------------------------------------------------------------------------
; smn_track - the drop (SMN_TRACK), far: DS = KERNEL_SEG, the gfx lock held,
;             BX = the bar cell's left x
; out: CF = 0 a pick is recorded and CMD_STIK posted; CF = 1 nothing picked.
;      Every register preserved.
; -----------------------------------------------------------------------------
smn_track:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [cs:stm_cellx], bx
    mov byte [cs:stm_ak], AK_NONE
    call stm_scan               ; STIKAPPS\ on the boot volume and the fixed
    call stm_census             ; disks; then who is running
    call stm_layout
    call stm_loadpics           ; the first page's pictures
    call stm_open               ; the save-under, the dock's hole, the pens
    call stm_drawall
    mov byte [cs:stm_live], 1   ; the panel is on the glass (a row's witness:
    call stm_loop               ; AX = the slab picked, 0FFFFh none
    mov byte [cs:stm_live], 0   ; tests/stikmenu.py)
    push ax
    call stm_shut               ; the screen back, whatever was picked
    pop ax
    call stm_pick               ; CF = 0 posted
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; stm_scan - every Sticky App this machine can reach, into stm_ents
; The BOOT volume first - its copy is the one a widget row names - then every
; other FIXED disk: hb_pick1's test (dsk_vol_fixed_x AL = 1, not a redirected
; FILE volume) less a mounted disk IMAGE, which is a floppy's contents. A
; name already listed is skipped, so the boot volume's copy wins. The
; machine's own place is banked round the whole walk (drv_vol_bank).
; clobbers: AX, BX, CX, DX, SI, DI, BP, ES
; -----------------------------------------------------------------------------
stm_scan:
    mov byte [cs:stm_n], 0
    call COLD_SEG:drvf_drv_vol_bank
    mov al, [dsk_bootvol]
    call stm_scanvol
    xor cx, cx
.vol:
    cmp cl, [dsk_bootvol]
    je .next
    push cx
    call stm_isfixed
    pop cx
    jc .next
    push cx
    mov al, cl
    call stm_scanvol
    pop cx
.next:
    inc cx
    cmp cl, DVOL_MAX
    jb .vol
    call COLD_SEG:drvf_drv_vol_back
    ret

; stm_isfixed - CL = a volume: CF = 0 when it is a fixed disk to search.
; clobbers AX, BX
stm_isfixed:
    mov bl, cl
    call COLD_SEG:dskf_dsk_vol_row
    jc .r                       ; a free row: no BIOS retries on nothing
    cmp byte [bx+DV_CLASS], DRVC_IMG
    je .no
    mov bl, cl
    call COLD_SEG:dkf_dsk_vol_fixed
    jc .r
    cmp al, 1
    jne .no
    cmp ah, DVK_FILE
    je .no
    clc
.r:
    ret
.no:
    stc
    ret

; stm_scanvol - AL = a volume: its STIKAPPS\, entry by entry.
; clobbers AX, BX, CX, DX, SI, DI, BP, ES
stm_scanvol:
    mov [cs:stm_vol], al
    call stm_gohome             ; CF = 0 standing in its STIKAPPS folder
    jc .ret
    xor cx, cx                  ; the ordinal: dsk_find_x is stateless, so the
.f:                             ; peeks between the calls cannot disturb it
    cmp byte [cs:stm_n], STM_MAX
    jae .ret
    push cs
    pop es
    mov di, stm_find
    xor al, al
    call COLD_SEG:dsk_find_x
    jc .ret                     ; the end, or a read that failed
    push cx
    call stm_take
    pop cx
    jmp short .f
.ret:
    ret

; stm_gohome - stand in STIKAPPS\ at the root of volume [stm_vol]: its root
; quietly, the folder by name, then the folder (sc_open's walk, one step).
; out: CF = 0 standing there, CF = 1 no such volume or folder.
; clobbers AX, BX, CX, DX, SI
stm_gohome:
    mov al, [cs:stm_vol]
    xor bx, bx
    call COLD_SEG:dskf_dsk_goto
    jc .r
    mov si, stm_s_dir
    call stm_kname              ; 'STIKAPPS' into the kernel's name scratch
    call COLD_SEG:dwf_dskw_stat ; AL = attribute, BX = its cluster
    jc .r
    test al, 0x10
    jz .no                      ; a FILE of that name is not the folder
    or bx, bx
    jz .no
    mov al, [cs:stm_vol]
    call COLD_SEG:dskf_dsk_goto
.r:
    ret
.no:
    stc
    ret

; stm_take - stm_find holds one entry: list it if it is a .O88 whose clear
; prefix carries a valid card and whose name is not listed yet.
; clobbers AX, BX, CX, DX, SI, DI, BP, ES
stm_take:
    test byte [cs:stm_find+13], 0x10
    jnz .no                     ; a folder
    mov si, stm_find
.dot:
    mov al, [cs:si]
    or al, al
    jz .no
    inc si
    cmp al, '.'
    jne .dot
    cmp word [cs:si], 'O8'
    jne .no
    cmp word [cs:si+2], '8'     ; '8', then the NUL
    jne .no
    call stm_dup
    jc .no
    mov ax, [cs:stm_find+16]    ; its first cluster
    mov cx, 512                 ; the first sector: the clear prefix is in it
    mov bp, dsk_peek_x          ; (SPEC.md 20.13.1), compressed or not
    call COLD_SEG:wm_cbd        ; ES:BX = the bytes, AX = how many came
    jc .no
    cmp word [es:bx], 0x384F    ; 'O8'
    jne .no
    mov dl, [es:bx+3]
    test dl, LD_HF_STIK
    jz .no                      ; no card: not listed (SPEC.md 110.12)
    mov si, 32                  ; the card follows the rest of the prefix
    test dl, 1
    jz .ni
    add si, 64
.ni:
    test dl, 2
    jz .na
    add si, 16
.na:
    test dl, LD_HF_GLYPH
    jz .ng
    add si, 16
.ng:
    lea cx, [si+STM_C_SIZE]
    cmp ax, cx
    jb .no                      ; a file shorter than its own card
    add si, bx                  ; ES:SI = the card
    call stm_cardok
    jc .no
    mov al, [cs:stm_n]
    call stm_ent                ; CS:DI = the next entry
    mov al, [cs:stm_vol]
    mov [cs:di+STE_VOL], al
    mov byte [cs:di+STE_RUN], 0
    mov ax, [cs:stm_find+16]
    mov [cs:di+STE_CLUS], ax
    push si
    push di
    push es                     ; the 8.3 name: image to image
    push cs
    pop es
    mov si, stm_find
    add di, STE_FILE
    mov cx, 13
    call stm_cpy
    pop es
    pop di
    push di
    lea si, [bx+16]             ; the program name, as the loader copies it
    add di, STE_HNAME           ; to I_NAME: at most 15, and a NUL
    mov cx, 15
    call stm_cpy
    mov byte [cs:di], 0
    pop di
    pop si
    push si
    push di
    add si, STM_C_NAME          ; the name and the description, which
    add di, STE_NAME            ; follow one another in the card as here
    mov cx, STM_C_NAMEMAX + STM_C_DESCMAX
    call stm_cpy
    pop di
    pop si
    inc byte [cs:stm_n]
.no:
    ret

; stm_cpy - CX bytes from ES:SI to CS:DI; SI, DI past them. clobbers AL, CX
stm_cpy:
    mov al, [es:si]
    mov [cs:di], al
    inc si
    inc di
    loop stm_cpy
    ret

; stm_dup - CF = 1 when stm_find's name is already listed. clobbers AX, CX,
; SI, DI
stm_dup:
    mov cl, [cs:stm_n]
    mov ch, 0
    jcxz .new
    xor al, al
.e:
    push ax
    call stm_ent
    pop ax
    add di, STE_FILE
    mov si, stm_find
.c:
    mov ah, [cs:si]
    cmp ah, [cs:di]
    jne .ne
    or ah, ah
    jz .same
    inc si
    inc di
    jmp short .c
.same:
    stc
    ret
.ne:
    inc al
    loop .e
.new:
    clc
    ret

; stm_cardok - ES:SI = a card: CF = 0 when the menu may letter it - 'SA',
; version 1, 0; a name of 1..23 printable bytes and a description of 0..35,
; each NUL-terminated inside its field. tools/os88pkg.py's stik_card_problem
; asks the same and more. preserves every register but the flags
stm_cardok:
    push ax
    push cx
    push di
    cmp word [es:si], 'SA'
    jne .bad
    cmp word [es:si+2], 1
    jne .bad
    lea di, [si+STM_C_NAME]
    mov cx, STM_C_NAMEMAX
    call stm_fieldok
    jc .out
    or ax, ax
    jz .bad                     ; an EMPTY name lists as nothing at all
    lea di, [si+STM_C_DESC]
    mov cx, STM_C_DESCMAX
    call stm_fieldok
    jmp short .out
.bad:
    stc
.out:
    pop di
    pop cx
    pop ax
    ret

; stm_fieldok - ES:DI = a field of CX bytes: CF = 0 and AX = its text's
; length when it is printable up to a NUL inside it. clobbers AX, CX, DI
stm_fieldok:
    xor ax, ax
.c:
    cmp byte [es:di], 0
    je .ok
    cmp byte [es:di], 0x20
    jb .bad
    cmp byte [es:di], 0x7E
    ja .bad
    inc di
    inc ax
    loop .c
.bad:
    stc
    ret
.ok:
    clc
    ret

; stm_ent - AL = an entry's index -> DI = its offset in the image. clobbers AX
stm_ent:
    mov ah, STE_SIZE
    mul ah
    add ax, stm_ents
    xchg di, ax
    ret

; stm_kname - copy the image's NUL string at SI into the kernel's 13-byte name
; scratch (assoc_doc, sc_open's) and leave SI on it: dskw_stat and the loader
; read their name through DS. clobbers SI
stm_kname:
    push ax
    push di
    mov di, assoc_doc
.c:
    mov al, [cs:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .c
    mov si, assoc_doc
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; stm_census - which entries are running, how many, and the desk windows
; An entry is RUNNING when a live package instance's I_NAME - its header's
; program name, the loader's copy - is the entry's: os88stik.inc's singleton
; test (stik_single), from the kernel's side. The desk windows are counted
; for Hide All, Show All and Arrange: used and WF_DESK, visible or not - a
; hidden one is Hide All's, since nothing else leaves a desk window hidden at
; a moment this menu can run (SPEC.md 110.12).
; clobbers: AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
stm_census:
    mov byte [cs:stm_nrun], 0
    xor dx, dx                  ; DL = the entry
.e:
    cmp dl, [cs:stm_n]
    jae .wins
    mov al, dl
    call stm_ent
    call stm_find_inst          ; CF = 0: BX = its instance record
    jc .n
    mov byte [cs:di+STE_RUN], 1
    inc byte [cs:stm_nrun]
.n:
    inc dx
    jmp short .e
.wins:
    xor cx, cx                  ; CL = desk windows, CH = visible ones
    mov bx, wm_wins
    mov dx, MAX_WIN
.w:
    call stm_isdesk
    jc .wn
    inc cl
    test byte [bx+W_FLAGS], 2
    jz .wn
    inc ch
.wn:
    add bx, WIN_SIZE
    dec dx
    jnz .w
    mov [cs:stm_ndesk], cx      ; stm_ndesk, then stm_nvis
    ret

; stm_isdesk - BX = a window slot: CF = 0 when it is a live desk window
stm_isdesk:
    test byte [bx+W_FLAGS], 1
    jz .no
    test byte [bx+W_FLAGS+1], WF_DESK >> 8
    jz .no
    clc
    ret
.no:
    stc
    ret

; stm_find_inst - DI = an entry: CF = 0 and BX = the first live package
; instance whose I_NAME is its program name; CF = 1 none. clobbers AX, BX, SI
stm_find_inst:
    push cx
    mov bx, inst_tab
    mov cx, INST_MAX
.i:
    cmp byte [bx+I_STATE], 1
    jne .nx
    test byte [bx+I_KIND], KIND_PKG
    jz .nx
    push di
    lea si, [bx+I_NAME]
    add di, STE_HNAME
.c:
    mov al, [si]
    cmp al, [cs:di]
    jne .nxp
    inc si
    inc di
    or al, al
    jnz .c
    pop di
    pop cx
    clc
    ret
.nxp:
    pop di
.nx:
    add bx, I_RECSZ
    loop .i
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; stm_layout - the panel's width, its slabs and where it hangs
; TALL on a primary of 300 rows or more, COMPACT on CGA's 200 (the picture is
; halved vertically there, which on CGA's tall pixels is about square). The
; width is the widest name or description in BYTES - every pen here is on the
; byte grid, so every slab is composed whole and blitted once - and the panel
; hangs from the bar under the title, shifted left if it would leave the
; primary. As many apps are listed as fit the screen AND a save-under of one
; segment; the rest are not shown.
; clobbers: AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
stm_layout:
    mov si, stm_lay_tall
    cmp word [vid_ph], 400
    jae .t
    mov si, stm_lay_mid
    cmp word [vid_ph], 300
    jae .t
    mov si, stm_lay_comp
.t:
    mov [cs:stm_ly], si
    ; --- the width: 14 bytes of furniture round the widest text ---------------
    xor dx, dx                  ; DL = the widest name/description, in cells
    xor al, al
.w:
    cmp al, [cs:stm_n]
    jae .wd
    push ax
    call stm_ent
    lea si, [di+STE_NAME]
    call stm_len
    lea si, [di+STE_DESC]
    call stm_len
    pop ax
    inc al
    jmp short .w
.wd:
    mov si, [cs:stm_ly]         ; the widest text, the furniture either side
    add dl, [cs:si+LY_TXTX]     ; of it - the margin, the picture and the gap
    add dl, [cs:si+LY_RFURN]    ; left of it, On/Add and the frame right
    mov al, [cs:si+LY_HDRX]
    add al, 31                  ; ...and never narrower than the hint (29
    cmp dl, al                  ; cells) with the header's margin either side
    jae .wok
    mov dl, al
.wok:
    mov [cs:stm_stride], dl
    mov dh, 0
    mov [cs:stm_stridew], dx
    mov cl, 3
    shl dx, cl
    mov [cs:stm_w], dx
    ; --- x: under the title, on the byte grid, inside the primary --------------
    mov ax, [cs:stm_cellx]
    and al, 0xF8
    mov bx, ax
    add bx, dx                  ; one past the panel...
    inc bx                      ; ...and its shadow column
    cmp bx, [vid_pw]
    jbe .xok
    mov ax, [vid_pw]
    sub ax, dx
    dec ax
    jns .xpos
    xor ax, ax
.xpos:
    and al, 0xF8
.xok:
    mov [cs:stm_x1], ax
    add ax, dx
    dec ax
    mov [cs:stm_x2], ax
    ; --- how many apps fit: the screen, and one segment of save-under -------
    mov si, [cs:stm_ly]
    xor bx, bx                  ; BX = every row but the apps'
    mov bl, [cs:si+LY_HT+SK_HEAD]
    xor ax, ax
    mov al, [cs:si+LY_HT+SK_RULE]
    add bx, ax
    add bx, ax
    add bx, ax                  ; three rules
    mov al, [cs:si+LY_HT+SK_ITEM]
    add bx, ax
    add bx, ax
    add bx, ax                  ; three items
    mov al, [cs:si+LY_HT+SK_HINT]
    add bx, ax
    mov ax, [vid_ph]
    sub ax, MBAR_H + 1          ; the shadow's row
    xchg cx, ax                 ; CX = the rows the screen allows (a panel
                                ; too big for one segment of save-under is
                                ; drawn anyway and REPAINTED on close -
                                ; stm_open's refusal, menu_drop's own path)
    sub cx, bx                  ; rows left for apps
    jnc .fit
    xor cx, cx
.fit:
    xchg ax, cx                 ; AX = those rows
    mov cl, [cs:si+LY_HT+SK_APP]
    push ax
    div cl                      ; AL = apps that fit with NO pager
    mov ah, [cs:stm_n]
    cmp ah, al
    pop bx
    ja .page                    ; more than fit: a pager, and a page
    cmp ah, STM_PGMAX
    ja .page
    mov [cs:stm_shown], ah      ; all of them, and no pager
    mov byte [cs:stm_paged], 0
    jmp short .nok
.page:
    xchg ax, bx                 ; the rows again, less the pager's
    xor bx, bx
    mov bl, [cs:si+LY_HT+SK_ITEM]
    sub ax, bx
    jnc .pfit
    xor ax, ax
.pfit:
    div cl                      ; AL = App slots on a page
    cmp al, STM_PGMAX
    jbe .pmax
    mov al, STM_PGMAX
.pmax:
    or al, al
    jnz .pone
    inc ax                      ; never a page of none
.pone:
    mov [cs:stm_shown], al
    mov byte [cs:stm_paged], 1
.nok:
    mov byte [cs:stm_top], 0
    ; --- the slabs, top down --------------------------------------------------
    mov di, stm_sl
    mov dx, MBAR_H              ; DX = the next slab's y
    mov [cs:stm_y1], dx
    mov ax, SK_HEAD
    call stm_addsl
    mov ax, SK_RULE
    call stm_addsl
    xor cx, cx
.ap:
    cmp cl, [cs:stm_shown]
    jae .apd
    mov al, SK_APP
    mov ah, cl
    mov byte [cs:stm_sflag], SF_SEL
    call stm_addsl
    inc cx
    jmp short .ap
.apd:
    cmp byte [cs:stm_paged], 0
    je .nopg
    mov byte [cs:stm_sflag], SF_SEL
    mov ax, SK_PAGE
    call stm_addsl
.nopg:
    cmp byte [cs:stm_n], 0
    jne .some
    mov byte [cs:stm_sflag], SF_DIS
    mov ax, SK_NONE
    call stm_addsl
.some:
    mov ax, SK_RULE
    call stm_addsl
    mov al, SF_SEL
    cmp byte [cs:stm_ndesk], 0
    jne .arr
    mov al, SF_DIS              ; nothing to arrange or hide
.arr:
    mov [cs:stm_iflag], al
    mov [cs:stm_sflag], al
    mov ax, SK_ITEM | (IT_ARR << 8)
    call stm_addsl
    mov al, [cs:stm_iflag]
    mov [cs:stm_sflag], al
    mov ax, SK_ITEM | (IT_HIDE << 8)
    call stm_addsl
    mov byte [cs:stm_sflag], SF_SEL
    mov ax, SK_ITEM | (IT_MORE << 8)
    call stm_addsl
    mov ax, SK_RULE
    call stm_addsl
    mov ax, SK_HINT
    call stm_addsl
    dec dx
    mov [cs:stm_y2], dx
    xchg ax, di
    sub ax, stm_sl
    mov cl, SL_SIZE
    div cl
    mov [cs:stm_nsl], al
    ret

; stm_len - SI = an image string: DL = max(DL, its length). clobbers AH, SI
stm_len:
    xor ah, ah
.c:
    cmp byte [cs:si], 0
    je .d
    inc si
    inc ah
    jmp short .c
.d:
    cmp ah, dl
    jbe .r
    mov dl, ah
.r:
    ret

; stm_addsl - AL = SK_*, AH = its param, [stm_sflag] its flags (cleared
; here), DI = the slab, DX = its y: write it, DI and DX past it. clobbers AX, BX
stm_addsl:
    mov [cs:di+SL_KIND], ax     ; SL_KIND, then SL_PARAM
    mov bl, 0
    xchg bl, [cs:stm_sflag]
    mov [cs:di+SL_FLAGS], bl
    mov bx, [cs:stm_ly]
    mov ah, 0
    cmp al, SK_PAGE
    jne .ht
    mov al, SK_ITEM             ; the pager is an item's height
.ht:
    add bx, ax
    mov al, [cs:bx+LY_HT]
    mov [cs:di+SL_H], al
    mov [cs:di+SL_Y], dx
    add dx, ax
    add di, SL_SIZE
    ret

; -----------------------------------------------------------------------------
; stm_open - claim and fill the save-under, put the open dock's hole away and
; set the blit's pens (SPEC.md 12.4's discipline, menu_drop's to the line)
; The save-under IS menu_drop's: [menu_sseg], MEM_P_MSAVE and menu_reloc, so
; the purge table and the compactor know it by the same word. A refused claim
; leaves it 0 and stm_shut repaints instead. The rect is the panel and its
; shadow, x1..x2+1 by y1..y2+1.
; clobbers: AX, BX, CX, DX, DI, ES
; -----------------------------------------------------------------------------
stm_open:
    mov word [menu_sseg], 0
    mov word [cs:stm_sseg2], 0
    call stm_rect               ; AX..DX
    xchg ax, cx                 ; AX = x2+1, CX = x1
    push cx
    mov cl, 3
    shr ax, cl
    pop bx
    shr bx, cl
    sub ax, bx
    add ax, 2                   ; span -> count, and the misalignment byte
    mul word [vid_planes_w]     ; AX = the bytes ONE ROW saves, every plane
    mov [cs:stm_rowb], ax
    xchg bx, ax
    mov ax, STM_SAVEMAX
    xor dx, dx
    div bx                      ; AX = the rows one segment holds
    mov cx, [cs:stm_y2]
    sub cx, [cs:stm_y1]
    add cx, 2                   ; CX = the rows, the shadow's included
    mov [cs:stm_rows], cx
    cmp cx, ax
    ja .two
    xchg ax, cx                 ; one segment holds the lot
.two:
    mov [cs:stm_cut], ax        ; the rows the FIRST part saves
    ; --- TWO PARTS when one segment cannot hold the panel - a paged VGA
    ; panel can be 440 rows, ~80 KB - so the close RESTORES rather than
    ; repainting what it covered. The first part is menu_drop's own claim
    ; ([menu_sseg], MEM_P_MSAVE, movable), the second the module's
    ; (MEM_K_MOD, pinned, freed with it); either refused, neither is kept
    mul word [cs:stm_rowb]
    call stm_kb
    mov bx, MEM_P_MSAVE         ; TRIVIAL-purgeable (SPEC.md 12.4.2)
    call COLD_SEG:mmf_mem_claim
    jc .nosave
    mov [menu_sseg], dx
    mov ax, menu_reloc          ; MOVABLE, as menu_drop's is (SPEC.md 66.5.6)
    mov bx, MEM_P_MSAVE
    call COLD_SEG:mmf_mem_movable
    mov ax, [cs:stm_rows]
    sub ax, [cs:stm_cut]
    jz .save
    mul word [cs:stm_rowb]
    or dx, dx
    jnz .drop1                  ; (a screen is never three segments tall)
    call stm_kb
    mov bx, MEM_K_MOD
    call COLD_SEG:mmf_mem_claim
    jc .drop1
    mov [cs:stm_sseg2], dx
.save:
    mov es, [menu_sseg]
    xor di, di
    call stm_rect1
    SMK gfx_save
    cmp word [cs:stm_sseg2], 0
    je .nosave
    mov es, [cs:stm_sseg2]
    xor di, di
    call stm_rect2
    SMK gfx_save
    jmp short .nosave
.drop1:
    xor dx, dx
    xchg dx, [menu_sseg]
    mov bx, MEM_P_MSAVE
    call COLD_SEG:mmf_mem_free  ; no save-under at all: the close repaints
.nosave:
    xor ax, ax                  ; a menu over an open dock draws whole
    xchg ax, [gfx_hole]         ; (SPEC.md 30.6.1)
    mov [cs:stm_hole], ax
    xor ax, ax
    xchg ax, [wm_clip_n]
    mov [cs:stm_clipn], ax
    ; --- the pens: the band is composed 1 = INK, and the blit's SET bit is
    ; whatever the pen says (SPEC.md 5.4.2.4). Bright and Windows keep the
    ; default pen - set = white, the fast path - and invert the band; every
    ; other theme gets the pair on VGA; and on a 1bpp adapter, where no pen
    ; is asked, the band is inverted when the theme's ink is a dark colour.
    mov ax, [gfx_b1ink]         ; gfx_b1ink, then gfx_b1pap
    mov [cs:stm_pens], ax
    mov byte [cs:stm_pol], 0
    mov al, [thm_inkc]
    cmp byte [vid_planes], 1
    je .mono
    cmp al, CBLACK
    jne .pens
    cmp byte [thm_bgc], CWHITE
    jne .pens
    mov byte [cs:stm_pol], 0xFF
    ret
.pens:
    mov ah, [thm_bgc]
    mov [gfx_b1ink], ax         ; ink on a set bit, the ground on a clear one
    ret
.mono:
    cmp al, CLGRAY
    jae .r                      ; a light ink lights the pixel: set = ink
    mov byte [cs:stm_pol], 0xFF
.r:
    ret

; stm_kb - AX bytes -> AX whole KB, never 0 (mem_bytes_kb's)
stm_kb:
    add ax, 1023
    jnc .ok
    mov ax, 0xFFFF              ; (STM_SAVEMAX keeps every part under this)
.ok:
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    or ax, ax
    jnz .r
    inc ax
.r:
    ret

; stm_rect1 / stm_rect2 - AX..DX = the save-under's first part (stm_cut
; rows from the top) and its second (the rest, to the shadow's row)
stm_rect1:
    call stm_rect
    mov dx, bx
    add dx, [cs:stm_cut]
    dec dx
    ret
stm_rect2:
    call stm_rect
    add bx, [cs:stm_cut]
    ret

; stm_rect - AX..DX = the panel and its shadow
stm_rect:
    mov ax, [cs:stm_x1]
    mov bx, [cs:stm_y1]
    mov cx, [cs:stm_x2]
    inc cx
    mov dx, [cs:stm_y2]
    inc dx
    ret

; -----------------------------------------------------------------------------
; stm_shut - the pens, the dock's hole and the screen back; the save-under
; freed (menu_drop's .up, with its [menu_bdirty] reasoning: a REPAINT must not
; land between the title's two XORs). clobbers: AX, BX, CX, DX, SI, ES
; -----------------------------------------------------------------------------
stm_shut:
    mov ax, [cs:stm_pens]
    mov [gfx_b1ink], ax
    mov ax, [cs:stm_hole]
    mov [gfx_hole], ax
    mov ax, [cs:stm_clipn]
    mov [wm_clip_n], ax
    mov ax, [menu_sseg]         ; the first part gone (shed while the menu
    or ax, ax                   ; was down) or a second part owed and not
    jz .repaint                 ; held: the whole rect repaints
    mov cx, [cs:stm_rows]
    cmp cx, [cs:stm_cut]
    jbe .one
    cmp word [cs:stm_sseg2], 0
    je .repaint
.one:
    mov es, ax
    xor si, si
    call stm_rect1
    SMK gfx_restore
    mov ax, [cs:stm_sseg2]
    or ax, ax
    jz .free
    mov es, ax
    xor si, si
    call stm_rect2
    SMK gfx_restore
    jmp short .free
.repaint:
    mov byte [menu_bdirty], 0
    call stm_rect
    call KERNEL_SEG:cw_wm_paint_dmg
.free:
    xor dx, dx
    xchg dx, [menu_sseg]
    or dx, dx
    jz .free2
    mov bx, MEM_P_MSAVE
    call COLD_SEG:mmf_mem_free
.free2:
    xor dx, dx
    xchg dx, [cs:stm_sseg2]
    or dx, dx
    jz .nofree
    mov bx, MEM_K_MOD
    call COLD_SEG:mmf_mem_free
.nofree:
    mov byte [menu_bdirty], 1
    ret

; -----------------------------------------------------------------------------
; stm_drawall - every slab, then the shadow: one blit a slab, two fills
; clobbers: AX, BX, CX, DX
; -----------------------------------------------------------------------------
stm_drawall:
    xor ax, ax
.s:
    cmp al, [cs:stm_nsl]
    jae .shadow
    call stm_slab               ; AH = 0: not highlighted
    inc al
    jmp short .s
.shadow:
    mov al, [thm_inkc]          ; a window's L, one pixel down and right
    mov [gfx_color], al         ; (wm_draw_shadow's)
    mov ax, [cs:stm_x2]
    inc ax
    mov cx, ax
    mov bx, [cs:stm_y1]
    inc bx
    mov dx, [cs:stm_y2]
    inc dx
    call KERNEL_SEG:cw_gfx_fill
    mov ax, [cs:stm_x1]
    inc ax
    mov bx, dx
    call KERNEL_SEG:cw_gfx_fill
    ret

; -----------------------------------------------------------------------------
; stm_slab - compose slab AL into the band and blit it: AH = 1 highlighted
; Every pixel of the slab is written ONCE, by one gfx_blit1 (SPEC.md 5.4.2):
; the band is composed 1 = ink with the frame's two columns in it, inverted
; inside the frame when highlighted, then put in the pen's polarity.
; preserves every register
; -----------------------------------------------------------------------------
stm_slab:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [cs:stm_hi], ah
    mov ah, SL_SIZE
    mul ah
    add ax, stm_sl
    xchg bx, ax                 ; CS:BX = the slab
    push cs
    pop es                      ; ES = the image, for every stosb below
    mov al, [cs:bx+SL_H]
    mul byte [cs:stm_stride]
    xchg cx, ax                 ; CX = the band's bytes
    mov [cs:stm_bn], cx
    mov di, stm_band
    xor al, al
    cld
    rep stosb
    mov si, [cs:stm_ly]         ; CS:SI = the layout, for every composer
    mov al, [cs:bx+SL_KIND]
    cmp al, SK_HEAD
    jne .k1
    call stm_c_head
    jmp short .framed
.k1:
    cmp al, SK_RULE
    jne .k2
    call stm_c_rule
    jmp short .framed
.k2:
    cmp al, SK_APP
    jne .k3
    call stm_c_app
    jmp short .framed
.k3:
    cmp al, SK_ITEM
    jne .k4
    call stm_c_item
    jmp short .framed
.k4:
    cmp al, SK_HINT
    jne .k5
    call stm_c_hint
    jmp short .framed
.k5:
    cmp al, SK_PAGE
    jne .k6
    call stm_c_page
    jmp short .framed
.k6:
    call stm_c_none
.framed:
    call stm_frame              ; the frame's two columns, every row
    cmp byte [cs:stm_hi], 0
    je .pol
    mov cx, [cs:stm_bn]         ; highlighted: invert INSIDE the frame
    mov di, stm_band
.inv:
    not byte [cs:di]
    inc di
    loop .inv
    call stm_frame
.pol:
    push word [gfx_b1ink]       ; (gfx_b1ink, gfx_b1pap)
    mov al, [cs:stm_pol]
    test byte [cs:bx+SL_FLAGS], SF_DIS
    jz .live
    cmp byte [vid_planes], 1
    je .live
    mov al, [thm_disc]          ; GREYED on a planar adapter: the theme's
    mov ah, [thm_bgc]           ; disabled ink on its ground, the pen's own
    mov [gfx_b1ink], ax         ; polarity - the grey a dropped menu's
    xor al, al                  ; MENU_DIS item wears (SPEC.md 76.6)
.live:
    or al, al
    jz .blit
    mov cx, [cs:stm_bn]
    mov di, stm_band
.pl:
    xor [cs:di], al
    inc di
    loop .pl
.blit:
    mov dl, [cs:bx+SL_H]
    mov dh, 0
    mov bx, [cs:bx+SL_Y]
    mov ax, [cs:stm_x1]
    mov cx, [cs:stm_w]
    mov bp, [cs:stm_stridew]
    mov si, stm_band            ; ES:SI = the band, ES = CS above
    call COLD_SEG:gbz_gfx_blit1
    pop word [gfx_b1ink]
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stm_frame - CS:BX = the slab: its left and right frame columns, ink, every
; row of the band. clobbers AX, CX, SI, DI
stm_frame:
    mov cl, [cs:bx+SL_H]
    mov ch, 0
    mov di, stm_band
    mov si, [cs:stm_stridew]
.r:
    or byte [cs:di], 0x80
    add di, si
    or byte [cs:di-1], 0x01
    loop .r
    ret

; --- the composers: CS:BX = the slab, CS:SI = the layout, ES = CS, the band
; clear. Each may clobber AX, CX, DX, SI, DI, BP ----------------------------
stm_c_head:
    xor al, al                  ; the frame's top row
    call stm_hrule0
    mov al, [cs:si+LY_HDR1]
    mov ah, [cs:si+LY_HDRX]
    call stm_at
    push si
    mov si, stm_s_keep
    xor dl, dl
    call stm_text
    pop si
    mov al, [cs:si+LY_HDR2]
    mov ah, [cs:si+LY_HDRX]
    call stm_at
    call stm_count              ; SI = 'N Sticky Apps active'
    xor dl, dl
    jmp stm_text

stm_c_rule:
    mov al, [cs:bx+SL_H]
    shr al, 1                   ; the middle row, inset one cell each side
    mov ah, 1
    call stm_at
    mov cx, [cs:stm_stridew]
    dec cx
    dec cx
    mov al, 0xFF
    rep stosb
    ret

stm_c_hint:
    mov al, [cs:bx+SL_H]
    dec ax                      ; the frame's bottom row
    call stm_hrule0
    mov al, [cs:si+LY_HINT]
    mov ah, [cs:si+LY_HDRX]
    call stm_at
    mov si, stm_s_hint
    xor dl, dl
    jmp stm_text

stm_c_none:
    mov al, [cs:si+LY_NONE]
    mov ah, [cs:si+LY_HDRX]
    call stm_at
    mov si, stm_s_none
    jmp stm_greytext

; the pager: 'Next page (P of N)', or 'First page (N of N)' on the last
stm_c_page:
    mov al, [cs:si+LY_ITEM]
    mov ah, [cs:si+LY_ITEMX]
    call stm_at
    push di
    mov di, stm_cnt
    mov si, stm_s_next
    mov al, [cs:stm_top]        ; this page's first entry...
    add al, [cs:stm_shown]
    cmp al, [cs:stm_n]          ; ...and is there one past the page?
    jb .nx
    mov si, stm_s_first
.nx:
    call stm_cat
    mov al, [cs:stm_top]        ; the page number: top / page + 1
    mov ah, 0
    div byte [cs:stm_shown]
    inc ax
    call stm_num
    mov si, stm_s_of
    call stm_cat
    mov al, [cs:stm_n]          ; the pages: ceil(n / page)
    add al, [cs:stm_shown]
    dec ax
    mov ah, 0
    div byte [cs:stm_shown]
    call stm_num
    mov byte [cs:di], ')'
    mov byte [cs:di+1], 0
    pop di
    mov si, stm_cnt
    xor dl, dl
    jmp stm_text

; stm_num - AL (0..99) as one or two digits at CS:DI; DI past them.
; clobbers AX
stm_num:
    mov ah, 0
    push cx
    mov cl, 10
    div cl                      ; AL tens, AH units
    pop cx
    or al, al
    jz .u
    add al, '0'
    mov [cs:di], al
    inc di
.u:
    add ah, '0'
    mov [cs:di], ah
    inc di
    ret

stm_c_item:
    mov al, [cs:si+LY_ITEM]
    mov ah, [cs:si+LY_ITEMX]
    call stm_at
    mov al, [cs:bx+SL_PARAM]
    mov si, stm_s_arr
    cmp al, IT_ARR
    je .t
    mov si, stm_s_more
    cmp al, IT_MORE
    je .t
    mov si, stm_s_hide
    cmp byte [cs:stm_nvis], 0
    jne .t
    cmp byte [cs:stm_ndesk], 0
    je .t                       ; nothing at all: Hide, greyed
    mov si, stm_s_show          ; every widget is hidden: SHOW them
.t:
    xor dl, dl
    test byte [cs:bx+SL_FLAGS], SF_DIS
    jz .live
    jmp stm_greytext
.live:
    jmp stm_text

; stm_greytext - stm_text, GREYED: a checkerboard on a 1bpp adapter, where
; that is what grey is (SPEC.md 47); plain on a planar one, whose slab is
; blitted in the theme's DISABLED pen instead (stm_slab)
stm_greytext:
    xor dl, dl
    cmp byte [vid_planes], 1
    jne .pen
    mov dl, STY_GREY
.pen:
    jmp stm_text

; --- an app's row: its picture, its name (bold), its description, and On or
; Add at the right ---------------------------------------------------------------
stm_c_app:
    mov al, [cs:bx+SL_PARAM]    ; the slot...
    mov [cs:stm_slot], al
    add al, [cs:stm_top]        ; ...and the entry the page puts in it
    cmp al, [cs:stm_n]
    jb .ent
    ret                         ; past the last App: an empty slot, the frame
.ent:                           ; alone, so a short last page keeps its size
    call stm_ent                ; CS:DI = the entry
    mov bp, di
    ; the picture: 32 rows, or (COMPACT) 16 of row pairs ORed
    mov al, [cs:si+LY_PIC]
    mov ah, [cs:si+LY_PICX]
    call stm_at
    xor dx, dx                  ; DL = 1 for pairs
    cmp si, stm_lay_comp
    jne .full
    inc dx
.full:
    mov al, [cs:stm_slot]       ; the slot's picture, read for this page
    mov ah, 128                 ; (stm_loadpics)
    mul ah
    add ax, stm_pics
    xchg si, ax
    mov dh, 32                  ; source rows left
.pic:
    mov cx, 4
.pb:
    mov al, [cs:si]
    or dl, dl
    jz .p1
    or al, [cs:si+4]
.p1:
    or [cs:di], al
    inc si
    inc di
    loop .pb
    sub di, 4
    add di, [cs:stm_stridew]
    dec dh
    or dl, dl
    jz .p2
    add si, 4
    dec dh
.p2:
    or dh, dh
    jnz .pic
    ; the name (bold) and the description, at byte 6
    mov si, [cs:stm_ly]
    mov al, [cs:si+LY_NAME]
    mov ah, [cs:si+LY_TXTX]
    call stm_at
    push si
    lea si, [bp+STE_NAME]
    mov dl, STY_BOLD
    call stm_text
    pop si
    mov al, [cs:si+LY_DESC]
    mov ah, [cs:si+LY_TXTX]
    call stm_at
    push si
    lea si, [bp+STE_DESC]
    xor dl, dl
    call stm_text
    pop si
    ; the right column: a tick and 'On' when it runs, else an Add button
    mov al, [cs:si+LY_BTXT]
    mov ah, [cs:stm_stride]
    sub ah, 6
    call stm_at
    cmp byte [cs:bp+STE_RUN], 0
    je .add
    push di
    mov si, stm_tick            ; the tick: 8x8, 2-px strokes, which a 1bpp
    mov cx, 8                   ; adapter keeps (SPEC.md 13.16.2.1)
.tk:
    mov al, [cs:si]
    or [cs:di], al
    inc si
    add di, [cs:stm_stridew]
    loop .tk
    pop di
    inc di                      ; a cell's gap, then 'On'
    inc di
    mov si, stm_s_on
    mov dl, STY_BOLD
    jmp stm_text
.add:
    push si
    push di
    mov si, stm_s_add
    xor dl, dl
    call stm_text
    pop di
    pop si
    ; the box: 5 bytes from the byte before the text, LY_BTOP down, LY_BH
    ; rows, its corners open
    mov al, [cs:si+LY_BTXT]
    sub al, [cs:si+LY_BTOP]
    mov ah, 0
    mul word [cs:stm_stridew]
    sub di, ax
    dec di
    mov dl, [cs:si+LY_BH]
    mov dh, 0
    ; (stm_box falls through)

; stm_box - DI = the top-left byte of a 5-byte-wide box, DX = its rows: the
; top and bottom rows from x+1 to x+38 (corners open, so it reads rounded at
; this size), the two sides between. clobbers AX, DX, DI
stm_box:
    mov byte [cs:di], 0x7F
    mov word [cs:di+1], 0xFFFF
    mov word [cs:di+3], 0xFEFF
    dec dx
    dec dx
.s:
    add di, [cs:stm_stridew]
    or byte [cs:di], 0x80
    or byte [cs:di+4], 0x01
    dec dx
    jnz .s
    add di, [cs:stm_stridew]
    mov byte [cs:di], 0x7F
    mov word [cs:di+1], 0xFFFF
    mov word [cs:di+3], 0xFEFF
    ret

; stm_at - DI = the band offset of row AL, byte AH. clobbers AX
stm_at:
    push dx
    push ax
    mov ah, 0
    mul word [cs:stm_stridew]
    pop dx
    mov dl, dh
    mov dh, 0
    add ax, dx
    add ax, stm_band
    xchg di, ax
    pop dx
    ret

; stm_hrule0 - row AL of the band, every byte ink: the frame's top or bottom.
; ES = CS. clobbers AX, CX, DI
stm_hrule0:
    mov ah, 0
    call stm_at
    mov cx, [cs:stm_stridew]
    mov al, 0xFF
    rep stosb
    ret

; -----------------------------------------------------------------------------
; stm_text - letter the image's NUL string at SI into the band at DI (the
; byte column of its top row), one 8x8 cell a byte: the ROM glyphs off
; [font_seg]:[font_base] ORed in, BOLD (DL bit 0) as each row ORed with itself
; one pixel right, GREY (DL bit 1) as a checkerboard - which is a 1bpp
; adapter's grey anyway (SPEC.md 47). It stops short of the frame's byte.
; clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
stm_text:
    push bx
    push es
    mov es, [font_seg]
    mov [cs:stm_sty], dl
    mov ax, di                  ; how many cells to the frame's byte: the
    sub ax, stm_band            ; column, off the offset
    xor dx, dx
    div word [cs:stm_stridew]
    mov cx, [cs:stm_stridew]
    dec cx
    sub cx, dx                  ; CX = the cells left
    jbe .out
.ch:
    mov al, [cs:si]
    or al, al
    jz .out
    inc si
    sub al, 32
    cmp al, 126 - 32
    jbe .gl
    xor al, al                  ; unprintable: a space
.gl:
    mov ah, 0
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [font_base]
    xchg bx, ax                 ; ES:BX = the glyph's eight rows
    push cx
    push di
    mov dh, 0xFF                ; DH = this row's mask: all, or the
    test byte [cs:stm_sty], STY_GREY ; checkerboard's phase
    jz .m
    mov dh, 0xAA
.m:
    mov cx, 8
.r:
    mov al, [es:bx]
    test byte [cs:stm_sty], STY_BOLD
    jz .nb
    mov ah, al
    shr ah, 1
    or al, ah
.nb:
    and al, dh
    or [cs:di], al
    test byte [cs:stm_sty], STY_GREY
    jz .ng
    not dh                      ; 0xAA <-> 0x55
.ng:
    inc bx
    add di, [cs:stm_stridew]
    loop .r
    pop di
    pop cx
    inc di
    loop .ch
.out:
    pop es
    pop bx
    ret

; stm_count - SI = the header's second line, 'N Sticky App(s) active' or
; 'No Sticky Apps active', built in stm_cnt. clobbers AX, SI
stm_count:
    mov al, [cs:stm_nrun]
    mov si, stm_s_none_act
    or al, al
    jz .r
    push di
    mov di, stm_cnt
    call stm_num                ; STM_MAX is 64: one or two digits
    mov si, stm_s_app
    call stm_cat
    cmp byte [cs:stm_nrun], 1
    je .one
    mov byte [cs:di], 's'
    inc di
.one:
    mov si, stm_s_act
    call stm_cat
    mov byte [cs:di], 0
    pop di
    mov si, stm_cnt
.r:
    ret

; stm_cat - append the image's string at SI to DI (no NUL). clobbers AL, SI
stm_cat:
    mov al, [cs:si]
    or al, al
    jz .r
    mov [cs:di], al
    inc si
    inc di
    jmp short stm_cat
.r:
    ret

; -----------------------------------------------------------------------------
; stm_loop - follow the pointer: AX = the slab released on, 0FFFFh none
; menu_drop's poll (SPEC.md 12.4): the button, one torn-free reading of the
; pointer, a highlight moved by recomposing the two slabs, the keyboard
; mouse, task_yield. THREE DIFFERENCES:
;   - THE ARROW STAYS ON THE GLASS. The mouse ISR draws it only while the
;     lock is free, and this loop holds the lock, so it is drawn HERE: shown
;     once the panel is up, and on every move hidden, the highlight moved
;     under it, and shown again where the pointer now is - WIN95.DRV's
;     click-to-open menu (wn_track), the one other tracker that does it.
;     It is hidden again before the restore, so menu_track's gfx_unlock
;     puts it back the way it always has.
;   - A press RELEASED while the disk was being read - on a floppy the
;     commonest way this menu comes up - leaves the panel up for a click,
;     rather than closing it before it was seen.
;   - ...and that click's press and release are DRAINED from the event ring
;     when the menu closes (wn's evq_drain), or they would reach whatever the
;     panel covered once it is put back.
; clobbers: AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
stm_loop:
    mov word [cs:stm_sel], 0xFFFF
    mov word [cs:stm_lastx], 0xFFFF ; nowhere: the first pass tracks
    xor ax, ax
    mov [cs:stm_stk], al
    mov [cs:stm_stky], al
    test byte [mouse_btn], 1
    jnz .arrow
    inc byte [cs:stm_stk]       ; released already: wait for the click
    inc byte [cs:stm_stky]      ; ...and drain it when the menu closes
.arrow:
    SMK cur_unlazy              ; any deferred hide spent, then the arrow
    SMK cursor_show             ; drawn where the pointer is
.poll:
    test byte [mouse_btn], 1
    jnz .down
    cmp byte [cs:stm_stk], 1
    je .track                   ; still waiting: the hover is shown
    jmp short .up
.down:
    mov byte [cs:stm_stk], 0    ; a press: its release ends the menu
.track:
    mov cx, [mouse_x]           ; only a MOVE redraws anything (the hit
    mov dx, [mouse_y]           ; test below reads its own torn-free pair)
    cmp cx, [cs:stm_lastx]
    jne .moved
    cmp dx, [cs:stm_lasty]
    je .same
.moved:
    mov [cs:stm_lastx], cx
    mov [cs:stm_lasty], dx
    SMK cursor_hide             ; the arrow off before any slab is blitted
    call stm_hit
    cmp ax, [cs:stm_sel]
    je .shown
    xchg ax, [cs:stm_sel]
    cmp ax, 0xFFFF
    je .noold
    mov ah, 0
    call stm_slab
.noold:
    mov ax, [cs:stm_sel]
    cmp ax, 0xFFFF
    je .shown
    mov ah, 1
    call stm_slab
.shown:
    SMK cursor_show             ; ...and back on, where the pointer is now
.same:
    SMK kbm_pollm               ; the keyboard mouse's button (SPEC.md 9.6)
    call KERNEL_SEG:cw_task_yield
    jmp short .poll
.up:
    call stm_ispage             ; a release on the PAGER turns the page and
    jc .close                   ; keeps the panel up for the next click
    SMK cursor_hide
    call stm_turn
    SMK cursor_show
    mov byte [cs:stm_stk], 1    ; ...a click away, and drained like the
    mov byte [cs:stm_stky], 1   ; stay-up click when the menu closes
    jmp .poll
.close:
    SMK cursor_hide             ; down again: the restore and gfx_unlock do
    cmp byte [cs:stm_stky], 0   ; what they always did
    je .ret
    push di
    mov di, ui_ev
    SMK evq_drain
    pop di
.ret:
    mov ax, [cs:stm_sel]
    ret

; stm_ispage - CF = 0 when [stm_sel] is the pager. clobbers AX
stm_ispage:
    mov ax, [cs:stm_sel]
    cmp ax, 0xFFFF
    je .no
    push bx
    mov bl, SL_SIZE
    mul bl
    xchg bx, ax
    cmp byte [cs:bx+stm_sl+SL_KIND], SK_PAGE
    pop bx
    je .yes
.no:
    stc
    ret
.yes:
    clc
    ret

; stm_turn - the next page (the first after the last): its pictures read,
; every App slot and the pager recomposed - the pager highlighted, the
; pointer still on it. The panel keeps its size, so nothing else is drawn.
; clobbers AX, BX, CX, DX, SI
stm_turn:
    mov al, [cs:stm_top]
    add al, [cs:stm_shown]
    cmp al, [cs:stm_n]
    jb .set
    xor al, al
.set:
    mov [cs:stm_top], al
    call stm_loadpics
    xor ax, ax
.s:
    cmp al, [cs:stm_nsl]
    jae .r
    push ax
    mov bl, SL_SIZE
    mul bl
    xchg bx, ax
    mov cl, [cs:bx+stm_sl+SL_KIND]
    pop ax
    cmp cl, SK_APP
    je .draw
    cmp cl, SK_PAGE
    jne .n
    mov ah, 1                   ; the pager, lit: it is under the pointer
.draw:
    call stm_slab
    mov ah, 0
.n:
    inc al
    jmp short .s
.r:
    ret

; -----------------------------------------------------------------------------
; stm_loadpics - the page's pictures, read again out of each file's first
; sector into stm_pics (an entry keeps its card's TEXT only, STE_*): each
; App's volume quietly, its first cluster, the card where its flags say.
; A sector that no longer holds a card leaves the slot's picture blank.
; On a floppy the scan's own reads are still in the track cache (SPEC.md
; 18.95), so the first page costs nothing more; a turn reads its page.
; Ends in stm_thaw: the freeze is over once the pictures are in.
; clobbers AX, BX, CX, DX, SI, DI, BP, ES
; -----------------------------------------------------------------------------
stm_loadpics:
    push cs
    pop es
    mov di, stm_pics            ; the page's slots blank first
    mov cx, STM_PGMAX * 128 / 2
    xor ax, ax
    cld
    rep stosw
    call COLD_SEG:drvf_drv_vol_bank
    xor cx, cx                  ; CL = the slot
.s:
    cmp cl, [cs:stm_shown]
    jae .donej
    mov al, cl
    add al, [cs:stm_top]
    cmp al, [cs:stm_n]
    jb .go
.donej:
    jmp .done
.go:
    push cx
    call stm_ent                ; CS:DI = the entry
    mov al, [cs:di+STE_VOL]
    xor bx, bx
    call COLD_SEG:dskf_dsk_goto ; its volume (a word when it is current)
    jc .next
    mov ax, [cs:di+STE_CLUS]
    mov cx, 512
    mov bp, dsk_peek_x
    call COLD_SEG:wm_cbd        ; ES:BX = the first sector, AX = its bytes
    jc .next
    cmp word [es:bx], 0x384F
    jne .next
    mov dl, [es:bx+3]
    test dl, LD_HF_STIK
    jz .next
    mov si, 32 + STM_C_PIC      ; the picture, behind the prefix's blocks
    test dl, 1
    jz .ni
    add si, 64
.ni:
    test dl, 2
    jz .na
    add si, 16
.na:
    test dl, LD_HF_GLYPH
    jz .ng
    add si, 16
.ng:
    lea cx, [si+128]
    cmp ax, cx
    jb .next
    add si, bx                  ; ES:SI = the picture
    pop cx
    push cx
    mov al, cl
    mov ah, 128
    mul ah
    add ax, stm_pics
    xchg di, ax                 ; CS:DI = the slot's
    mov cx, 128
    call stm_cpy
.next:
    pop cx
    inc cx
    jmp .s
.done:
    call COLD_SEG:drvf_drv_vol_back
                                ; ...and FALLS INTO stm_thaw: the disk work is
                                ; over and the panel is about to be the user's

; -----------------------------------------------------------------------------
; stm_thaw - THE FREEZE IS OVER, though the hold is not (SPEC.md 12.8.3.3):
; the progress widget off the bar and the busy clock off the pointer, which
; is gfx_unlock's two steps taken early. menu_track's hold does not end when
; the disk work does - the panel is tracked inside it, for as long as the
; user takes - so without this the widget sat on the bar and the clock
; stayed the pointer's picture until the panel closed. Reached at the end of
; every stm_loadpics, so after the drop's load and scan AND after a page
; turn's reads, both of them with the pointer OFF the glass or owed a hide
; (cur_shape_set's precondition): the drop's is gfx_lock's promise and the
; turn's stm_loop's own cursor_hide. ZERO resident bytes: both kernel steps
; are reached through SMK, and gfx_unlock's copy is untouched.
; clobbers AX
; -----------------------------------------------------------------------------
stm_thaw:
    SMK fpg_finish              ; the widget down and its cells given back
    cmp byte [cur_shape], CUR_BUSYSH
    jne .r                      ; no clock worn: nothing to take off
    mov al, [cur_shprev]        ; the picture the clock replaced (SPEC.md
    SMK cur_shape_set           ; 7.5.2), as gfx_unlock puts it back
.r:
    ret

; stm_hit - AX = the selectable slab under the pointer, 0FFFFh none; the
; pointer read once under cli (SPEC.md 12.4.1). clobbers BX, CX, DX, SI
stm_hit:
    pushf
    cli
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    popf
    mov ax, 0xFFFF
    mov bx, [cs:stm_x1]
    cmp cx, bx
    jbe .r                      ; the frame is not an item
    mov bx, [cs:stm_x2]
    cmp cx, bx
    jae .r
    mov si, stm_sl
    xor bx, bx
.s:
    cmp bl, [cs:stm_nsl]
    jae .r
    test byte [cs:si+SL_FLAGS], SF_SEL
    jz .n
    cmp dx, [cs:si+SL_Y]
    jb .n
    mov cl, [cs:si+SL_H]
    mov ch, 0
    add cx, [cs:si+SL_Y]
    cmp dx, cx
    jae .n
    cmp byte [cs:si+SL_KIND], SK_APP
    jne .hit
    mov cl, [cs:si+SL_PARAM]
    add cl, [cs:stm_top]
    cmp cl, [cs:stm_n]
    jae .r                      ; a short last page's empty slot: nothing
.hit:
    xchg ax, bx
    ret
.n:
    add si, SL_SIZE
    inc bx
    jmp short .s
.r:
    ret

; stm_pick - AX = the slab picked: record the action and post CMD_STIK.
; out: CF = 0 posted, CF = 1 nothing. clobbers AX, BX, DI
stm_pick:
    cmp ax, 0xFFFF
    je .none
    mov bl, SL_SIZE
    mul bl
    xchg bx, ax
    add bx, stm_sl
    mov al, [cs:bx+SL_PARAM]
    cmp byte [cs:bx+SL_KIND], SK_APP
    jne .item
    add al, [cs:stm_top]        ; the slot's entry on this page
    mov [cs:stm_ai], al
    call stm_ent
    mov ah, AK_LAUNCH           ; a row that runs is a toggle: picking it
    cmp byte [cs:di+STE_RUN], 0 ; turns it OFF (SPEC.md 110.12)
    je .set
    mov ah, AK_CLOSE
    jmp short .set
.item:
    mov ah, AK_ARR
    cmp al, IT_ARR
    je .set
    mov ah, AK_MORE
    cmp al, IT_MORE
    je .set
    mov ah, AK_HIDE
    cmp byte [cs:stm_nvis], 0
    jne .set
    mov ah, AK_SHOW
.set:
    mov [cs:stm_ak], ah
    mov ax, CMD_STIK
    call KERNEL_SEG:ui_post_cmd ; ui_task spends it with nothing held
    clc
    ret
.none:
    stc
    ret

; =============================================================================
; smn_act - the posted pick (SMN_ACT), far: UI task, no lock held, DS =
;           KERNEL_SEG. preserves every register
; =============================================================================
smn_act:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, [cs:stm_ai]
    call stm_ent                ; CS:DI = the entry, for the two that want one
    mov al, [cs:stm_ak]
    mov byte [cs:stm_ak], AK_NONE
    cmp al, AK_LAUNCH
    jne .a1
    call stm_a_launch
    jmp short .out
.a1:
    cmp al, AK_CLOSE
    jne .a2
    call stm_a_close
    jmp short .out
.a2:
    cmp al, AK_MORE
    jne .a3
    call stm_a_more
    jmp short .out
.a3:
    cmp al, AK_ARR
    jne .a4
    call stm_a_arr
    jmp short .out
.a4:
    cmp al, AK_HIDE
    je .vis
    cmp al, AK_SHOW
    jne .out
.vis:
    call stm_a_vis
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; --- Add: STIKAPPS\NAME.O88 off the volume it was found on - sc_open's walk
; and its loader call; a refusal says its own sentence (ld_say_status).
; DI = the entry. clobbers AX, BX, CX, DX, SI, BP, ES ---------------------------
stm_a_launch:
    call COLD_SEG:drvf_drv_vol_bank
    mov al, [cs:di+STE_VOL]
    mov [cs:stm_vol], al
    call stm_gohome
    jc .miss
    lea si, [di+STE_FILE]
    call stm_kname
    xor cx, cx                  ; DX:CX = 0: read the file
    xor dx, dx
    mov es, cx                  ; ES = 0: no document
    call COLD_SEG:ldf_ld_pkg_start
    jnc .back
    mov bp, ld_say_status       ; AL = its LD_*
    call COLD_SEG:wm_cbd
    jmp short .back
.miss:
    call KERNEL_SEG:cw_snd_beep ; the disk went away between the drop and now
.back:
    call COLD_SEG:drvf_drv_vol_back
    ret

; --- On: close it as the user does - app_close_win under the lock, so its
; own negotiator (stik_close) removes its row and saves (SPEC.md 110.5). Not
; OSAPI_WM_CLOSE, whose pass clears the negotiator first. DI = the entry ---
stm_a_close:
    call stm_find_inst
    jc .r
    mov bx, [bx+I_WIN]
    or bx, bx
    jz .r
    call KERNEL_SEG:cw_gfx_lock
    call KERNEL_SEG:cw_app_close_win
    call KERNEL_SEG:cw_gfx_unlock
.r:
    ret

; --- More Sticky Apps...: The Wire, as the desktop's service zone opens it.
; With no driver registered there are no strings, so this names the package
; itself - The Wire opens and explains itself without a card (SPEC.md 92.9) --
stm_a_more:
    cmp word [desk_svc_seg], 0
    jne .go
    push ds
    pop es                      ; ES = KERNEL_SEG, the destination
    push cs
    pop ds                      ; DS = the image, the source
    mov si, stm_s_wiref
    mov di, desk_svc_file
    mov cx, DSV_FILEMAX
    cld
    rep movsb
    mov si, stm_s_wiren
    mov di, desk_svc_name
    mov cx, DSV_NAMEMAX
    rep movsb
    push es
    pop ds                      ; KERNEL_SEG again
.go:
    call KERNEL_SEG:cw_ui_svc_open
    ret

; --- Hide All / Show All: every desk window, wm_hide or wm_show (DESK.DRV's
; dkd_show for a desk window); nothing closed, nothing written. AL = AK_HIDE
; or AK_SHOW ------------------------------------------------------------------
stm_a_vis:
    mov dl, al
    call KERNEL_SEG:cw_gfx_lock
    mov bx, wm_wins
    mov cx, MAX_WIN
.w:
    call stm_isdesk
    jc .n
    test byte [bx+W_FLAGS], 2
    jz .hidden
    cmp dl, AK_HIDE
    jne .n
    SMK wm_hide
    jmp short .n
.hidden:
    cmp dl, AK_SHOW
    jne .n
    SMK wm_show
.n:
    add bx, WIN_SIZE
    loop .w
    call KERNEL_SEG:cw_gfx_unlock
    ret

; --- Arrange: a column down the right of the desktop band, clear of the drive
; zones' column, wrapping left; each widget hidden, placed and shown (there
; is no move cell - stik_refit's shape, SPEC.md 110.11). W_X stays 7 mod 8 so
; the face is on the byte grid, and the widget's own poll saves the place ----
STM_ARR_R   equ 64              ; the drive zones' column and a gap
STM_ARR_G   equ 8               ; between two widgets
STM_ARR_T   equ MBAR_H + 4      ; the column's top
stm_a_arr:
    call KERNEL_SEG:cw_gfx_lock
    mov ax, [vid_band_xe]
    sub ax, STM_ARR_R
    mov [cs:stm_ax], ax         ; this column's right edge
    mov [cs:stm_axl], ax        ; ...and its leftmost frame so far
    mov word [cs:stm_ay], STM_ARR_T
    mov bx, wm_wins
    mov cx, MAX_WIN
.w:
    call stm_isdesk
    jc .n
    mov dx, [cs:stm_ay]
    add dx, [bx+W_H]
    cmp dx, [vid_dock_y0]
    jb .fits
    cmp word [cs:stm_ay], STM_ARR_T
    je .fits                    ; taller than the band: first in a column
    mov ax, [cs:stm_axl]        ; a new column, left of this one
    sub ax, STM_ARR_G
    mov [cs:stm_ax], ax
    mov [cs:stm_axl], ax
    mov word [cs:stm_ay], STM_ARR_T
.fits:
    mov ax, [cs:stm_ax]
    sub ax, [bx+W_W]
    inc ax
    and al, 0xF8
    dec ax                      ; W_X = 7 mod 8: the face on the grid
    cmp ax, 7
    jge .xok
    mov ax, 7
.xok:
    cmp ax, [cs:stm_axl]
    jge .lft
    mov [cs:stm_axl], ax
.lft:
    mov dx, [cs:stm_ay]
    mov si, [bx+W_FLAGS]
    test si, 2
    jz .place
    SMK wm_hide
.place:
    mov [bx+W_X], ax
    mov [bx+W_Y], dx
    mov [bx+W_NATR+NR_X], ax    ; ...and banked, for an adapter change
    mov [bx+W_NATR+NR_Y], dx    ; (wm_nat_bank_xy's effect)
    test si, 2
    jz .next
    SMK wm_show
.next:
    add dx, [bx+W_H]
    add dx, STM_ARR_G
    mov [cs:stm_ay], dx
.n:
    add bx, WIN_SIZE
    dec cx
    jz .done
    jmp .w                      ; (too far for `loop`)
.done:
    call KERNEL_SEG:cw_gfx_unlock
    ret

; -----------------------------------------------------------------------------
; smk_go - the far call SMK names (winmod.inc's wnk_go, to the instruction):
; the call left the address of `dw target` on the stack, and this turns it
; into a far return to just past it with cw_kretf's near return on top, then
; RETFs into KERNEL_SEG:target. Every register and the flags preserved.
; -----------------------------------------------------------------------------
smk_go:                         ; ovlchk: builds its own far frame
    push ax                     ; STKBALANCE-OK: a far frame built by hand, its
    push ax                     ; four slots spent by the RETF below and by the
    push ds                     ; target's own return through cw_kretf. The
    push ax                     ; segment slot is PUSHED: DS is KERNEL_SEG
    push bp                     ; throughout the image
    mov bp, sp
    push bx
    mov bx, [bp+10]
    mov [bp+10], cs
    mov word [bp+6], cw_kretf
    lea bx, [bx+2]
    mov [bp+8], bx
    mov bx, [cs:bx-2]
    mov [bp+2], bx
    pop bx
    pop bp
    retf

; --- the image's constants ----------------------------------------------------
; The three LAYOUTS (LY_*): the slab heights by SK_* (HEAD, RULE, APP, ITEM,
; HINT, NONE), the rows - each from its own slab's top - and the columns, in
; bytes. TALL is the owner's mockup at VGA's 480: a 56-row App with the
; picture and the name-and-description block both centred on its middle, the
; name and the description 18 rows apart, 18-row items, 11-row rules and two
; cells of margin; MID is the 40-row App that fits Hercules' and EGA's 348
; and 350; COMPACT is CGA's 20-row App with the picture in row pairs.
stm_lay_tall:                   ; VGA: the mockup's proportions
    db 34, 11, 56, 18, 20, 24   ; LY_HT: HEAD RULE APP ITEM HINT NONE
    db 9, 22, 5, 8              ; HDR1 HDR2 ITEM NONE
    db 12, 15, 33               ; PIC NAME DESC: the picture 12..43 and the
                                ; text block 15..40 both centred on 27.5
    db 24, 19, 18               ; BTXT BTOP BH: the box 19..36, its word 24
    db 6                        ; HINT
    db 2, 8, 2, 3, 9            ; PICX TXTX HDRX ITEMX RFURN
stm_lay_mid:                    ; Hercules, EGA
    db 26, 7, 40, 12, 13, 16
    db 4, 15, 2, 4
    db 4, 9, 23
    db 16, 11, 18
    db 2
    db 1, 6, 1, 2, 8
stm_lay_comp:                   ; CGA: the picture in row pairs
    db 20, 3, 20, 10, 11, 12
    db 2, 11, 1, 2
    db 2, 2, 11
    db 6, 2, 16
    db 1
    db 1, 6, 1, 2, 8
%if stm_lay_mid - stm_lay_tall != LY_SIZE || stm_lay_comp - stm_lay_mid != LY_SIZE || ($ - stm_lay_comp) != LY_SIZE
  %error "a layout is LY_SIZE bytes, LY_* in order"
%endif

stm_tick:   db 0x00, 0x03, 0x06, 0x8C, 0xD8, 0x70, 0x20, 0x00
stm_s_dir:  db 'STIKAPPS', 0
stm_s_keep: db 'Keep tools on your desktop', 0
stm_s_none_act: db 'No Sticky Apps active', 0
stm_s_app:  db ' Sticky App', 0
stm_s_act:  db ' active', 0
stm_s_none: db 'No Sticky Apps found', 0
stm_s_arr:  db 'Arrange Sticky Apps...', 0
stm_s_hide: db 'Hide All Sticky Apps', 0
stm_s_show: db 'Show All Sticky Apps', 0
stm_s_more: db 'More Sticky Apps...', 0
stm_s_hint: db 'Drag a Sticky App to move it.', 0
stm_s_on:   db 'On', 0
stm_s_next: db 'Next page (', 0
stm_s_first: db 'First page (', 0
stm_s_of:   db ' of ', 0
stm_s_add:  db 'Add', 0
stm_s_wiref: db 'THEWIRE.O88', 0     ; DSV_FILEMAX bytes, with the padding
            times DSV_FILEMAX - ($ - stm_s_wiref) db 0
stm_s_wiren: db 'The Wire', 0        ; DSV_NAMEMAX bytes
            times DSV_NAMEMAX - ($ - stm_s_wiren) db 0

; --- the image's scratch: ZEROS on the disk, which the CZ wrap packs to a few
; bytes, and zeros again at every load - a fresh claim per drop ---------------
stm_cellx:  dw 0
stm_x1:     dw 0
stm_x2:     dw 0
stm_y1:     dw 0
stm_y2:     dw 0
stm_w:      dw 0
stm_stridew: dw 0
stm_stride: db 0
stm_ly:     dw 0
stm_n:      db 0
stm_shown:  db 0
stm_nrun:   db 0
stm_ndesk:  db 0                ; ...and stm_nvis, the word stm_census writes
stm_nvis:   db 0
stm_nsl:    db 0
stm_sflag:  db 0
stm_iflag:  db 0
stm_vol:    db 0
stm_hi:     db 0
stm_stk:    db 0
stm_stky:   db 0                ; the stay-up click happened: drain it
stm_lastx:  dw 0                ; where the arrow was last drawn
stm_lasty:  dw 0
stm_pol:    db 0
stm_sty:    db 0
stm_ak:     db 0
stm_ai:     db 0
stm_top:    db 0                ; the first entry on show
stm_paged:  db 0                ; 1 = more than one page: a pager row
stm_live:   db 0                ; 1 while the panel is on the glass
stm_sseg2:  dw 0                ; the save-under's second part, 0 = none
stm_rowb:   dw 0                ; the bytes one row of it saves
stm_rows:   dw 0                ; its rows, the shadow's included
stm_cut:    dw 0                ; ...and how many the first part holds
stm_slot:   db 0                ; stm_c_app's slot
stm_sel:    dw 0
stm_bn:     dw 0
stm_pens:   dw 0
stm_hole:   dw 0
stm_clipn:  dw 0
stm_ax:     dw 0
stm_axl:    dw 0
stm_ay:     dw 0
stm_cnt:    times 24 db 0
stm_find:   times DSK_FIND_SZ db 0
stm_sl:     times STM_SLMAX * SL_SIZE db 0
stm_ents:   times STM_MAX * STE_SIZE db 0
stm_pics:   times STM_PGMAX * 128 db 0  ; the page's pictures (stm_loadpics)
stm_band:   times STM_STRMAX * STM_SLABMAX db 0
%if stm_nvis - stm_ndesk != 1
  %error "stm_census writes stm_ndesk and stm_nvis as one word"
%endif
%if (gfx_b1pap - gfx_b1ink) != 1
  %error "stm_open moves gfx_b1ink and gfx_b1pap as one word"
%endif
section .text
%endif                          ; KERN_BIG
