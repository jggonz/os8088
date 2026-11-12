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
; title, and one that does pays nothing once it has closed but a PURGEABLE
; claim: the drop keeps the image for the next press (SPEC.md 2.8.3), and any
; claim that needs the room takes it.
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
    dw modn_end - stm_cellx     ; MOD_H_SCR: the scratch, from stm_cellx
                                ; to the end, is all it writes - KEPT at its
                                ; drop (SPEC.md 2.8.3)
    dw smn_track, smn_act

; SMK name - far-call the KERNEL routine `name` (a near proc in .text) from
; this image: winmod.inc's WNK, whose wnk_go lives in WIN95.DRV's image and
; so cannot be shared.
%macro SMK 1
    call smk_go
    dw %1
%endmacro

; --- what the menu holds, and its geometry ------------------------------------
STM_MAX     equ 8               ; Sticky Apps listed at most, PAGED: the
                                ; panel shows what the screen holds and a
                                ; pager row turns the page (SPEC.md 110.12).
                                ; The tree ships three; raising it costs
                                ; STE_SIZE bytes of the image apiece. One
                                ; past it sets stm_over, and the hint row
                                ; says so instead of its hint
STM_PGMAX   equ 8               ; App slots on one page, at most
STE_VOL     equ 0               ; byte: the volume it was found on
STE_RUN     equ 1               ; byte: 1 = an instance of it is running
STE_FILE    equ 2               ; 13: its 8.3 name in the STIKAPPS folder
STE_HNAME   equ 15              ; 16: its header's program name (+16), which is
                                ; what an instance record carries (I_NAME)
STE_NAME    equ 31              ; 24: its card's name...
STE_DESC    equ 55              ; 36: ...and description...
STE_PIC     equ 91              ; 128: ...and its picture: the card from +4
                                ; on, in the card's own order, so the scan
                                ; takes all three in ONE copy and nothing is
                                ; read off the disk again
STE_SIZE    equ 220
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
STM_STRMAX  equ 49              ; the widest row, in bytes: MID's 6 + 35 + 8
STM_SLABMAX equ 40              ; ...and the tallest slab, MID's App
STM_SAVEMAX equ 0xF000          ; the save-under's ceiling, bytes: inside one
                                ; segment, so gfx_save cannot wrap
; A LAYOUT, LY_SIZE bytes, picked by the primary's height: MID at 300 rows
; or more (VGA, EGA, Hercules) and COMPACT below that (CGA). Every row is
; relative to its own slab's first; every column is a BYTE of the band.
LY_HT       equ 0               ; 6: the slab heights, by SK_*
LY_REST     equ 6               ; every row but the Apps': HEAD, 3 RULEs,
                                ; 3 ITEMs, HINT (STM_LY works it out)
LY_HDR1     equ 7               ; the header's first line
LY_HDR2     equ 8               ; ...and its second
LY_ITEM     equ 9               ; an item's line
LY_NONE     equ 10              ; 'No Sticky Apps found'
LY_PIC      equ 11              ; the picture's first row
LY_NAME     equ 12              ; the name
LY_DESC     equ 13              ; the description
LY_BTXT     equ 14              ; 'On' / 'Add'
LY_BTOP     equ 15              ; the Add box's top row
LY_BH       equ 16              ; ...and its rows
LY_HINT     equ 17              ; the hint's line
LY_PICX     equ 18              ; the picture's column (byte)
LY_TXTX     equ 19              ; the name's and the description's
LY_HDRX     equ 20              ; the header's, the hint's and NONE's
LY_ITEMX    equ 21              ; the three items'
LY_RFURN    equ 22              ; bytes right of the text: gap, On/Add, frame
LY_SIZE     equ 23

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
    CSDO_ON
    mov [CSEG stm_cellx], bx
    mov byte [CSEG stm_ak], AK_NONE
    CSDO_OFF
    call stm_scan               ; STIKAPPS\ on the boot volume and the fixed
    call stm_census             ; disks; then who is running
    call stm_layout
    call stm_thaw               ; the disk work is over
    call stm_open               ; the save-under, the dock's hole, the pens
    call stm_drawall
    CSDO mov byte [CSEG stm_live], 1 ; the panel is on the glass (a row's witness:
    call stm_loop               ; AX = the slab picked, 0FFFFh none
    CSDO mov byte [CSEG stm_live], 0 ; tests/stikmenu.py)
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
%ifdef MOD_CACHE
    ; THE KEPT SCAN (SPEC.md 110.12.2): the list a scan left is still the
    ; answer while the OS has written NOTHING to any volume since - every
    ; byte of [dsk_wgen] (fsck.inc, bumped by disk_write_x) as it was then.
    ; It lives below stm_cellx, so the drop that keeps the image keeps it
    ; and a load - first, or after a shed - starts with stm_kok = 0 from the
    ; file. No disk is touched here: a different floppy in the drive is the
    ; ACTION's to find (stm_a_launch re-walks and refuses)
    call stm_kgw                ; ES:DI = ours, DS:SI = the kernel's
    cmp byte [cs:stm_kok], 0
    je .scan
    repe cmpsb
    je .kept                    ; nothing written: the list stands
.scan:
%endif
    CSDO mov byte [CSEG stm_n], 0
    CSDO mov byte [CSEG stm_over], 0
    call COLD_SEG:drvf_drv_vol_bank
    call COLD_SEG:dkf_dsk_batch_begin ; ONE BPB VALIDATION PER VOLUME for
                                ; the walk (SPEC.md 18.9.3, 110.12.2): the
                                ; panel is dropped inside menu_track's lock,
                                ; so nothing can reach the drive, and the
                                ; unlock that ends the track ends the batch.
                                ; Without it a machine with a FIXED disk paid
                                ; a second boot-sector read every press,
                                ; coming back to the floppy from C:\STIKAPPS
                                ; (drv_vol_back's mount) - and from wherever
                                ; the cards had moved the head to
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
%ifdef MOD_CACHE
    call stm_kgw                ; ...and the generations it was taken under
    rep movsb
    CSDO mov byte [CSEG stm_kok], 1
.kept:
%endif
    ret

%ifdef MOD_CACHE
; stm_kgw - ES:DI = stm_kgen, DS:SI = [dsk_wgen], CX = DVOL_MAX, DF = 0: the
; kept scan's generations and the kernel's, for one string instruction
; clobbers: CX, SI, DI, ES
stm_kgw:
    PUSH_CSDATA
    pop es
    mov si, dsk_wgen
    mov di, stm_kgen
    mov cx, DVOL_MAX
    cld
    ret
%endif

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
    CSDO mov [CSEG stm_vol], al
    call stm_gohome             ; CF = 0 standing in its STIKAPPS folder
    jc .ret
    xor cx, cx                  ; the TOKEN (SPEC.md 19.7.1.1): held here and
.f:                             ; not in the kernel, so the peeks between the
    cmp byte [cs:stm_over], 0   ; calls cannot disturb it
    jne .ret                    ; one App past STM_MAX: no need to look on
    PUSH_CSDATA
    pop es
    mov di, stm_find
    mov ax, 0x0100              ; AH = 1 the token walk, AL = 0 no system
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
    cmp word [es:si], 'SA'      ; the magic, version 1, and a name: the
    jne .no                     ; rest is made SAFE rather than checked -
    cmp word [es:si+2], 1       ; both fields are NUL-ended below whatever
    jne .no                     ; the file says, and stm_text letters any
    cmp byte [es:si+STM_C_NAME], 0 ; unprintable byte as a space.
    je .no                      ; tools/os88pkg.py checks the rest at build
    cmp byte [cs:stm_n], STM_MAX
    jb .room
    CSDO mov byte [CSEG stm_over], 1 ; a real App with no room: left out,
    ret                         ; and the hint row says so
.room:
    mov al, [cs:stm_n]
    call stm_ent                ; CS:DI = the next entry
    mov al, [cs:stm_vol]
    CSDO mov [CSEG di+STE_VOL], al ; (STE_RUN is stm_census's)
    push si
    push di
    push es                     ; the 8.3 name: image to image
    PUSH_CSDATA
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
    CSDO mov byte [CSEG di], 0
    pop di
    pop si
    add si, STM_C_NAME          ; the name, the description and the
    add di, STE_NAME            ; picture, which follow one another in the
    mov cx, STM_C_SIZE - STM_C_NAME ; card as here
    call stm_cpy
    CSDO_ON
    mov byte [CSEG di-STM_C_SIZE+STM_C_DESC-1], 0 ; the name NUL-ended...
    mov byte [CSEG di-STM_C_SIZE+STM_C_PIC-1], 0  ; ...and the description
    inc byte [CSEG stm_n]
    CSDO_OFF
.no:
    ret

; stm_cpy - CX bytes from ES:SI to CS:DI; SI, DI past them. clobbers AL, CX
stm_cpy:
    CSDO_ON                     ; (DS = the image for the whole copy)
.c:
    mov al, [es:si]
    mov [CSEG di], al
    inc si
    inc di
    loop .c
    CSDO_OFF
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
    CSDO mov byte [CSEG stm_nrun], 0
    xor dx, dx                  ; DL = the entry
.e:
    cmp dl, [cs:stm_n]
    jae .wins
    mov al, dl
    call stm_ent
    CSDO mov byte [CSEG di+STE_RUN], 0 ; a KEPT entry says what it said last
    call stm_find_inst          ; CF = 0: BX = its instance record
    jc .n
    CSDO_ON
    mov byte [CSEG di+STE_RUN], 1
    inc byte [CSEG stm_nrun]
    CSDO_OFF
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
    CSDO mov [CSEG stm_ndesk], cx ; stm_ndesk, then stm_nvis
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
; MID on a primary of 300 rows or more, COMPACT on CGA's 200 (the picture is
; halved vertically there, which on CGA's tall pixels is about square). The
; width is the widest name or description in BYTES - every pen here is on the
; byte grid, so every slab is composed whole and blitted once - and the panel
; hangs from the bar under the title, shifted left if it would leave the
; primary. As many apps are shown at once as fit the screen AND a save-under
; of one segment; when that is fewer than are listed, a pager turns the page.
; clobbers: AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
stm_layout:
    mov si, stm_lay_mid
    cmp word [vid_ph], 300
    jae .t
    mov si, stm_lay_comp
.t:
    CSDO mov [CSEG stm_ly], si
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
    CSDO_ON
    mov [CSEG stm_stride], dl
    mov dh, 0
    mov [CSEG stm_stridew], dx
    mov cl, 3
    shl dx, cl
    mov [CSEG stm_w], dx
    CSDO_OFF
    push dx
    mov ax, [cs:stm_stridew]    ; the save-under's row: the panel's bytes,
    inc ax                      ; the shadow's and the misalignment byte,
    inc ax                      ; every plane
    mul word [vid_planes_w]
    CSDO mov [CSEG stm_rowb], ax
    pop dx
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
    CSDO_ON
    mov [CSEG stm_x1], ax
    add ax, dx
    dec ax
    mov [CSEG stm_x2], ax
    CSDO_OFF
    ; --- how many apps fit: the screen, and one segment of save-under -------
    mov si, [cs:stm_ly]
    xor bx, bx                  ; BX = every row but the apps'
    mov bl, [cs:si+LY_REST]
    mov ax, STM_SAVEMAX         ; the rows ONE segment of save-under holds,
    xor dx, dx                  ; less the shadow's...
    div word [cs:stm_rowb]
    dec ax
    mov cx, [vid_ph]
    sub cx, MBAR_H + 1          ; ...or the rows the screen allows, the
    cmp cx, ax                  ; shadow's taken: the fewer. So the panel
    jbe .scr                    ; is always saved in one part
    xchg cx, ax
.scr:
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
    CSDO_ON
    mov [CSEG stm_shown], ah    ; all of them, and no pager
    mov byte [CSEG stm_paged], 0
    CSDO_OFF
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
    CSDO_ON
    mov [CSEG stm_shown], al
    mov byte [CSEG stm_paged], 1
    CSDO_OFF
.nok:
    CSDO_ON
    mov byte [CSEG stm_top], 0
    ; --- the slabs, top down --------------------------------------------------
    mov di, stm_sl
    mov dx, MBAR_H              ; DX = the next slab's y
    mov [CSEG stm_y1], dx
    CSDO_OFF
    xor cx, cx                  ; CH = the next slab's flags: none...
    mov ax, SK_HEAD
    call stm_addsl              ; (which leaves AH = 0)
    mov al, SK_RULE
    call stm_addsl
    mov ch, SF_SEL              ; ...an App a pick, CL the slot
.ap:
    cmp cl, [cs:stm_shown]
    jae .apd
    mov al, SK_APP
    mov ah, cl
    call stm_addsl
    inc cx
    jmp short .ap
.apd:
    cmp byte [cs:stm_paged], 0
    je .nopg
    mov al, SK_PAGE             ; the pager a pick too
    call stm_addsl
.nopg:
    cmp byte [cs:stm_n], 0
    jne .some
    mov ch, SF_DIS
    mov al, SK_NONE
    call stm_addsl
.some:
    mov ch, 0
    mov al, SK_RULE
    call stm_addsl
    mov ch, SF_SEL
    cmp byte [cs:stm_ndesk], 0
    jne .arr
    mov ch, SF_DIS              ; nothing to arrange or hide
.arr:
    mov ax, SK_ITEM | (IT_ARR << 8)
    call stm_addsl
    mov ax, SK_ITEM | (IT_HIDE << 8)
    call stm_addsl
    mov ch, SF_SEL
    mov ax, SK_ITEM | (IT_MORE << 8)
    call stm_addsl
    mov ch, 0
    mov al, SK_RULE
    call stm_addsl
    mov al, SK_HINT
    call stm_addsl
    dec dx
    CSDO_ON
    mov [CSEG stm_y2], dx
    xchg ax, di
    sub ax, stm_sl
    mov cl, SL_SIZE
    div cl
    mov [CSEG stm_nsl], al
    CSDO_OFF
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

; stm_addsl - AL = SK_*, AH = its param, CH its SF_* flags, DI = the slab,
; DX = its y: write it, DI and DX past it; AH = 0 on the way out.
; clobbers AX, BX
stm_addsl:
    CSDO_ON
    mov [CSEG di+SL_KIND], ax   ; SL_KIND, then SL_PARAM
    mov [CSEG di+SL_FLAGS], ch
    CSDO_OFF
    mov bx, [cs:stm_ly]
    mov ah, 0
    cmp al, SK_PAGE
    jne .ht
    mov al, SK_ITEM             ; the pager is an item's height
.ht:
    add bx, ax
    mov al, [cs:bx+LY_HT]
    CSDO_ON
    mov [CSEG di+SL_H], al
    mov [CSEG di+SL_Y], dx
    CSDO_OFF
    add dx, ax
    add di, SL_SIZE
    ret

; -----------------------------------------------------------------------------
; stm_open - claim and fill the save-under, put the open dock's hole away and
; set the blit's pens (SPEC.md 12.4's discipline, menu_drop's to the line)
; The save-under IS menu_drop's: [menu_sseg], MEM_P_MSAVE and menu_reloc, so
; the purge table and the compactor know it by the same word. A refused claim
; leaves it 0 and stm_shut repaints instead. The rect is the panel and its
; shadow, x1..x2+1 by y1..y2+1, and stm_layout keeps it inside ONE segment.
; clobbers: AX, BX, CX, DX, DI, ES
; -----------------------------------------------------------------------------
stm_open:
    mov word [menu_sseg], 0
    mov ax, [cs:stm_y2]
    sub ax, [cs:stm_y1]
    add ax, 2                   ; the rows, the shadow's included
    mul word [cs:stm_rowb]      ; ...at most STM_SAVEMAX (stm_layout)
    SMK mem_bytes_kb
    mov bx, MEM_P_MSAVE         ; TRIVIAL-purgeable (SPEC.md 12.4.2)
    call COLD_SEG:mmf_mem_claim
    jc .nosave
    mov [menu_sseg], dx
    mov ax, menu_reloc          ; MOVABLE, as menu_drop's is (SPEC.md 66.5.6)
    mov bx, MEM_P_MSAVE
    call COLD_SEG:mmf_mem_movable
    mov es, [menu_sseg]
    xor di, di
    call stm_rect
    SMK gfx_save
.nosave:
    xor ax, ax                  ; a menu over an open dock draws whole
    xchg ax, [gfx_hole]         ; (SPEC.md 30.6.1)
    CSDO mov [CSEG stm_hole], ax
    xor ax, ax
    xchg ax, [wm_clip_n]
    CSDO mov [CSEG stm_clipn], ax
    ; --- the pens: the band is composed 1 = INK, and the blit's SET bit is
    ; whatever the pen says (SPEC.md 5.4.2.4). Bright and Windows keep the
    ; default pen - set = white, the fast path - and invert the band; every
    ; other theme gets the pair on VGA; and on a 1bpp adapter, where no pen
    ; is asked, the band is inverted when the theme's ink is a dark colour.
    mov ax, [gfx_b1ink]         ; gfx_b1ink, then gfx_b1pap
    CSDO_ON
    mov [CSEG stm_pens], ax
    mov byte [CSEG stm_pol], 0
    CSDO_OFF
    mov al, [thm_inkc]
    cmp byte [vid_planes], 1
    je .mono
    cmp al, CBLACK
    jne .pens
    cmp byte [thm_bgc], CWHITE
    jne .pens
    CSDO mov byte [CSEG stm_pol], 0xFF
    ret
.pens:
    mov ah, [thm_bgc]
    mov [gfx_b1ink], ax         ; ink on a set bit, the ground on a clear one
    ret
.mono:
    cmp al, CLGRAY
    jae .r                      ; a light ink lights the pixel: set = ink
    CSDO mov byte [CSEG stm_pol], 0xFF
.r:
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
    mov ax, [menu_sseg]         ; shed while the menu was down: the
    or ax, ax                   ; whole rect repaints
    jz .repaint
    mov es, ax
    xor si, si
    call stm_rect
    SMK gfx_restore
    xor dx, dx
    xchg dx, [menu_sseg]
    mov bx, MEM_P_MSAVE
    call COLD_SEG:mmf_mem_free
    jmp short .done
.repaint:
    mov byte [menu_bdirty], 0
    call stm_rect
    KDOOR wm_paint_dmg
.done:
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
    CSDO mov [CSEG stm_hi], ah
    mov ah, SL_SIZE
    mul ah
    add ax, stm_sl
    xchg bx, ax                 ; CS:BX = the slab
    PUSH_CSDATA
    pop es                      ; ES = the image, for every stosb below
    mov al, [cs:bx+SL_H]
    mul byte [cs:stm_stride]
    xchg cx, ax                 ; CX = the band's bytes
    CSDO mov [CSEG stm_bn], cx
    mov di, stm_band
    xor al, al
    cld
    rep stosb
    mov si, [cs:stm_ly]         ; CS:SI = the layout, for every composer
    mov al, [cs:bx+SL_KIND]
    cbw
    shl ax, 1
    xchg di, ax
    call [cs:di+stm_ctab]       ; the slab's composer, by SK_*
.framed:
    call stm_frame              ; the frame's two columns, every row
    cmp byte [cs:stm_hi], 0
    je .pol
    mov cx, [cs:stm_bn]         ; highlighted: invert INSIDE the frame
    mov di, stm_band
    CSDO_ON
.inv:
    not byte [CSEG di]
    inc di
    loop .inv
    CSDO_OFF
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
    CSDO_ON
.pl:
    xor [CSEG di], al
    inc di
    loop .pl
    CSDO_OFF
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

stm_ctab:   dw stm_c_head, stm_c_rule, stm_c_app, stm_c_item, stm_c_hint
            dw stm_c_none, stm_c_page
%if SK_HEAD != 0 || SK_RULE != 1 || SK_APP != 2 || SK_ITEM != 3 || SK_HINT != 4 || SK_NONE != 5 || SK_PAGE != 6
  %error "stm_ctab is in SK_* order"
%endif

; stm_frame - CS:BX = the slab: its left and right frame columns, ink, every
; row of the band. clobbers AX, CX, SI, DI
stm_frame:
    mov cl, [cs:bx+SL_H]
    mov ch, 0
    mov di, stm_band
    mov si, [cs:stm_stridew]
    CSDO_ON
.r:
    or byte [CSEG di], 0x80
    add di, si
    or byte [CSEG di-1], 0x01
    loop .r
    CSDO_OFF
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
    cmp byte [cs:stm_over], 0
    je .h
    mov si, stm_s_over          ; Apps were left out: say that instead
.h:
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
    CSDO_ON
    mov byte [CSEG di], ')'
    mov byte [CSEG di+1], 0
    CSDO_OFF
    pop di
    mov si, stm_cnt
    xor dl, dl
    jmp stm_text

; stm_num - AL (0..99) as one or two digits at CS:DI; DI past them.
; clobbers AX
stm_num:
    CSDO_ON
    mov ah, 0
    push cx
    mov cl, 10
    div cl                      ; AL tens, AH units
    pop cx
    or al, al
    jz .u
    add al, '0'
    mov [CSEG di], al
    inc di
.u:
    add ah, '0'
    mov [CSEG di], ah
    inc di
    CSDO_OFF
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
    lea si, [bp+STE_PIC]        ; the entry's picture
    mov dh, 32                  ; source rows left
    CSDO_ON                     ; (DS = the image for the whole picture)
.pic:
    mov cx, 4
.pb:
    mov al, [cs:si]
    or dl, dl
    jz .p1
    or al, [cs:si+4]
.p1:
    or [CSEG di], al
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
    CSDO_OFF
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
    CSDO_ON
.tk:
    mov al, [cs:si]
    or [CSEG di], al
    inc si
    add di, [cs:stm_stridew]
    loop .tk
    CSDO_OFF
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
    CSDO_ON
    mov byte [CSEG di], 0x7F
    mov word [CSEG di+1], 0xFFFF
    mov word [CSEG di+3], 0xFEFF
    dec dx
    dec dx
.s:
    add di, [cs:stm_stridew]
    or byte [CSEG di], 0x80
    or byte [CSEG di+4], 0x01
    dec dx
    jnz .s
    add di, [cs:stm_stridew]
    mov byte [CSEG di], 0x7F
    mov word [CSEG di+1], 0xFFFF
    mov word [CSEG di+3], 0xFEFF
    CSDO_OFF
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
    CSDO mov [CSEG stm_sty], dl
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
    CSDO_ON                     ; (DS = the image for the glyph's 8 rows)
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
    or [CSEG di], al
    test byte [cs:stm_sty], STY_GREY
    jz .ng
    not dh                      ; 0xAA <-> 0x55
.ng:
    inc bx
    add di, [cs:stm_stridew]
    loop .r
    CSDO_OFF
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
    call stm_num                ; STM_MAX is 8: one digit (stm_num has two)
    mov si, stm_s_app
    call stm_cat
    cmp byte [cs:stm_nrun], 1
    je .one
    CSDO mov byte [CSEG di], 's'
    inc di
.one:
    mov si, stm_s_act
    call stm_cat
    CSDO mov byte [CSEG di], 0
    pop di
    mov si, stm_cnt
.r:
    ret

; stm_cat - append the image's string at SI to DI (no NUL). clobbers AL, SI
stm_cat:
    CSDO_ON
.c:
    mov al, [cs:si]
    or al, al
    jz .r
    mov [CSEG di], al
    inc si
    inc di
    jmp short .c
.r:
    CSDO_OFF
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
    CSDO_ON
    mov word [CSEG stm_sel], 0xFFFF
    mov word [CSEG stm_lastx], 0xFFFF ; nowhere: the first pass tracks
    xor ax, ax
    mov [CSEG stm_stk], al
    mov [CSEG stm_stky], al
    CSDO_OFF
    test byte [mouse_btn], 1
    jnz .arrow
    CSDO_ON
    inc byte [CSEG stm_stk]     ; released already: wait for the click
    inc byte [CSEG stm_stky]    ; ...and drain it when the menu closes
    CSDO_OFF
.arrow:
    SMK cur_unlazy              ; any deferred hide spent, then the arrow
    SMK cursor_show             ; drawn where the pointer is
.poll:
    test byte [mouse_btn], 1
    jnz .down
    cmp byte [cs:stm_stk], 1
    je .track                   ; still waiting: the hover is shown
    jmp .up
.down:
    CSDO mov byte [CSEG stm_stk], 0 ; a press: its release ends the menu
.track:
    mov cx, [mouse_x]           ; only a MOVE redraws anything (the hit
    mov dx, [mouse_y]           ; test below reads its own torn-free pair)
    cmp cx, [cs:stm_lastx]
    jne .moved
    cmp dx, [cs:stm_lasty]
    je .same
.moved:
    CSDO_ON
    mov [CSEG stm_lastx], cx
    mov [CSEG stm_lasty], dx
    CSDO_OFF
    SMK cursor_hide             ; the arrow off before any slab is blitted
    call stm_hit
    cmp ax, [cs:stm_sel]
    je .shown
    CSDO xchg ax, [CSEG stm_sel]
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
    jmp .poll
.up:
    call stm_ispage             ; a release on the PAGER turns the page and
    jc .close                   ; keeps the panel up for the next click
    SMK cursor_hide
    call stm_turn
    SMK cursor_show
    CSDO_ON
    mov byte [CSEG stm_stk], 1  ; ...a click away, and drained like the
    mov byte [CSEG stm_stky], 1 ; stay-up click when the menu closes
    CSDO_OFF
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
    CSDO mov [CSEG stm_top], al
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
; stm_thaw - THE FREEZE IS OVER, though the hold is not (SPEC.md 12.8.3.3):
; the progress widget off the bar and the busy clock off the pointer, which
; is gfx_unlock's two steps taken early. menu_track's hold does not end when
; the disk work does - the panel is tracked inside it, for as long as the
; user takes - so without this the widget sat on the bar and the clock
; stayed the pointer's picture until the panel closed. Reached at the end of
; the drop's load and scan, with the pointer OFF the glass or owed a hide
; (cur_shape_set's precondition) - gfx_lock's promise. A page turn reads
; nothing: every picture came in with the scan. ZERO resident bytes: both kernel steps
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
    cmp cx, [cs:stm_x1]
    jbe .r                      ; the frame is not an item
    cmp cx, [cs:stm_x2]
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
    CSDO mov [CSEG stm_ai], al
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
    CSDO mov [CSEG stm_ak], ah
    mov ax, CMD_STIK            ; ui_task spends it with nothing held: what
    mov [ui_pcmd], ax           ; ui_post_cmd did, written out here - this was
    inc byte [ui_post]          ; its one caller (kernel size pass 14). DS =
                                ; KERNEL_SEG, and ui_post's one bump is the
                                ; SAY SO of SPEC.md 13.13
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
    CSDO mov byte [CSEG stm_ak], AK_NONE
    cbw
    mov si, ax                  ; (AL stays the action, for stm_a_vis)
    shl si, 1
    jz .out                     ; AK_NONE: nothing posted
    call [cs:si+stm_atab-2]
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

stm_atab:   dw stm_a_launch, stm_a_close, stm_a_vis, stm_a_vis, stm_a_arr
            dw stm_a_more
%if AK_LAUNCH != 1 || AK_CLOSE != 2 || AK_HIDE != 3 || AK_SHOW != 4 || AK_ARR != 5 || AK_MORE != 6
  %error "stm_atab is in AK_* order"
%endif

; --- Add: STIKAPPS\NAME.O88 off the volume it was found on - sc_open's walk
; and its loader call; a refusal says its own sentence (ld_say_status).
; DI = the entry. clobbers AX, BX, CX, DX, SI, BP, ES ---------------------------
stm_a_launch:
    call COLD_SEG:drvf_drv_vol_bank
    mov al, [cs:di+STE_VOL]
    CSDO mov [CSEG stm_vol], al
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
    KDOOR snd_beep              ; the disk went away between the drop and now
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
    KDOOR app_close_win
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
    PUSH_CSDATA
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
    CSDO_ON
    mov [CSEG stm_ax], ax       ; this column's right edge
    mov [CSEG stm_axl], ax      ; ...and its leftmost frame so far
    mov word [CSEG stm_ay], STM_ARR_T
    CSDO_OFF
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
    CSDO_ON
    mov [CSEG stm_ax], ax
    mov [CSEG stm_axl], ax
    mov word [CSEG stm_ay], STM_ARR_T
    CSDO_OFF
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
    CSDO mov [CSEG stm_axl], ax
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
    CSDO mov [CSEG stm_ay], dx
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
    PUSH_KCODE                  ; target's own return through cw_kretf. The
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
; The two LAYOUTS (LY_*): the slab heights by SK_* (HEAD, RULE, APP, ITEM,
; HINT, NONE), the rows - each from its own slab's top - and the columns, in
; bytes. MID is the 40-row App with the whole 32-row picture, for every
; primary of 300 rows or more (VGA, EGA, Hercules); COMPACT is CGA's 20-row
; App with the picture in row pairs, which on CGA's tall pixels is about
; square. (A third, TALL, gave VGA the mockup's air - a 56-row App - for 22
; bytes of table, 832 of band and a save-under that took two segments.)
; STM_LY head, rule, app, item, hint, none - LY_HT's six, then LY_REST
%macro STM_LY 6
    db %1, %2, %3, %4, %5, %6, %1 + 3 * (%2) + 3 * (%4) + %5
%endmacro
stm_lay_mid:                    ; VGA, EGA, Hercules
    STM_LY 26, 7, 40, 12, 13, 16
    db 4, 15, 2, 4
    db 4, 9, 23
    db 16, 11, 18
    db 2
    db 1, 6, 1, 2, 8
stm_lay_comp:                   ; CGA: the picture in row pairs
    STM_LY 20, 3, 20, 10, 11, 12
    db 2, 11, 1, 2
    db 2, 2, 11
    db 6, 2, 16
    db 1
    db 1, 6, 1, 2, 8
%if stm_lay_comp - stm_lay_mid != LY_SIZE || ($ - stm_lay_comp) != LY_SIZE
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
stm_s_over: db 'Only the first ', '0' + STM_MAX, ' are listed.', 0
%if STM_MAX > 9
  %error "stm_s_over spells STM_MAX as one digit"
%endif
stm_s_on:   db 'On', 0
stm_s_next: db 'Next page (', 0
stm_s_first: db 'First page (', 0
stm_s_of:   db ' of ', 0
stm_s_add:  db 'Add', 0
stm_s_wiref: db 'THEWIRE.O88', 0     ; DSV_FILEMAX bytes, with the padding
            times DSV_FILEMAX - ($ - stm_s_wiref) db 0
stm_s_wiren: db 'The Wire', 0        ; DSV_NAMEMAX bytes
            times DSV_NAMEMAX - ($ - stm_s_wiren) db 0

; --- THE KEPT SCAN (SPEC.md 110.12.2): zeros on the disk, and NOT in the
; scratch tail, so a drop that keeps the image keeps them and the next press
; lists from them with no disk I/O. Written only by stm_scan (and STE_RUN by
; stm_census, every press); a load - the first, or one after a shed - brings
; stm_kok in as 0, and the scan runs again
%ifdef MOD_CACHE                ; (kern_small frees the image at every drop)
stm_kok:    db 0                ; 1 = the three below are a scan's...
stm_kgen:   times DVOL_MAX db 0 ; ...taken at these [dsk_wgen]
%endif
stm_n:      db 0
stm_over:   db 0                ; 1 = an App past STM_MAX was left out
stm_ents:   times STM_MAX * STE_SIZE db 0

; --- the image's scratch: ZEROS on the disk, which the CZ wrap packs to a few
; bytes, and zeros again at every use - read in, or re-zeroed by the drop that
; KEEPS the image (MOD_H_SCR above, SPEC.md 2.8.3). EVERY byte this module
; writes in its own image is from here to the end but THE KEPT SCAN above; a
; write below stm_cellx survives into the next press ------------------------
stm_cellx:  dw 0
stm_x1:     dw 0
stm_x2:     dw 0
stm_y1:     dw 0
stm_y2:     dw 0
stm_w:      dw 0
stm_stridew: dw 0
stm_stride: db 0
stm_ly:     dw 0
stm_shown:  db 0
stm_nrun:   db 0
stm_ndesk:  db 0                ; ...and stm_nvis, the word stm_census writes
stm_nvis:   db 0
stm_nsl:    db 0
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
stm_rowb:   dw 0                ; the bytes one row of the save-under holds
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
stm_band:   times STM_STRMAX * STM_SLABMAX db 0
%if STE_DESC - STE_NAME != STM_C_DESC - STM_C_NAME || STE_PIC - STE_NAME != STM_C_PIC - STM_C_NAME || STE_PIC + 128 > STE_SIZE
  %error "an entry's name, description and picture are the card's, in one copy"
%endif
%if STE_SIZE > 255 || STM_MAX * STE_SIZE > 0xFFFF
  %error "stm_ent multiplies STE_SIZE by a byte"
%endif
%if stm_nvis - stm_ndesk != 1
  %error "stm_census writes stm_ndesk and stm_nvis as one word"
%endif
%if (gfx_b1pap - gfx_b1ink) != 1
  %error "stm_open moves gfx_b1ink and gfx_b1pap as one word"
%endif
section .text
%endif                          ; KERN_BIG
