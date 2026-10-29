; =============================================================================
; os8088 - apps/win16/w16menu.inc
;
; MENUS AND ACCELERATORS (SPEC.md 124.11). WIN16's own menu manager, shaped
; after Wine's user32 menu.c: a menu is a record in a USER heap of WIN16's
; (a fixed global block formatted as a local heap, w16mem.inc), an HMENU is
; that record's offset plus W16_HMB, popups nest, IDs are 16 bits.
;
; THE LOOK IS WINDOWS 3.0's, MEASURED (MENU.EXE on Windows 3.0, VGA): the bar
; is the System font's rows + 2 a line and a 1-pixel black line under it; a
; bar item is its text + 16 wide with the text 8 in; a popup is a 1-pixel
; black box with a 1-pixel COLOR_GRAYTEXT shadow right and below (black on
; a 1bpp display), its top on the bar's line, its client 15 + the widest label (+ 8 + the widest
; accelerator) + 14 wide; an item is rows + 2 high, a separator 7 with its
; line on the fourth; the check is 8 x 10 at (4, 3), a submenu's arrow 5 x 9
; at the client's right - 6, the mnemonic's underline on row ascent + 1.
; Each item is ONE band and ONE OSAPI_GFX_BLIT1, a bar line likewise.
;
; THE FLOW IS WINDOWS': a click on the bar is POSTED as WM_NCLBUTTONDOWN
; (HTMENU), F10 and Alt+letter as WM_SYSKEYDOWN / WM_SYSCHAR, and the
; program's DefWindowProc turns them into the menu loop - which here is the
; import WAITING (SPEC.md 124.2.1) while os8088's callbacks move the
; selection. WM_INITMENU and WM_INITMENUPOPUP are SENT from those callbacks
; through w16_resumecb (the "host send", SPEC.md 124.2.2), and a pick POSTS
; WM_COMMAND and lets DefWindowProc return.
;
; A popup stays inside the window's content, and what it covers is banked
; with OSAPI_GFX_SAVE into a global block and put back with OSAPI_GFX_REST.
; The lock is not held between callbacks, so a W_PAINT during the loop marks
; the banks stale and a stale bank is never written back: the area is made
; invalid instead and the program repaints it.
; =============================================================================

W16_MHEAPB  equ 8192            ; the USER heap
W16_HMB     equ 0x5000          ; HMENU = the record's offset + this, above
                                ;   the command IDs programs choose
                                ; (W16_MDEPTH, W16_MLAB, W16_MACC and MP_SIZE
                                ;   are win16.asm's: the bss is cut from them)

MH_MAGIC    equ 0               ; a menu
MH_ITEMS    equ 2               ; its items' block (an offset), 0 = none
MH_N        equ 4               ; byte: items
MH_CAP      equ 5               ; byte: what the block holds
MH_W        equ 6               ; laid out: the bar's width / a popup's
MH_H        equ 8               ;   client: the bar's height with its line
MH_AX       equ 10              ; a popup's accelerator column
MH_FL       equ 12              ; MHF_*
MH_SIZE     equ 14
MH_SIG      equ 0x554D          ; 'MU'
MHF_DIRTY   equ 1               ; changed since it was laid out

MI_FLAGS    equ 0               ; an item: MF_*
MI_ID       equ 2               ;   the command, or a popup's HMENU
MI_TEXT     equ 4               ;   its string's offset, 0 = none
MI_X        equ 6               ;   laid out (bar: content, popup: client)
MI_Y        equ 8
MI_W        equ 10
MI_H        equ 12
MI_SIZE     equ 14

MP_MENU     equ 0               ; an open popup (w16_mpl)
MP_X        equ 2               ;   its frame, content coordinates: the
MP_Y        equ 4               ;   border's top left and outer size, the
MP_W        equ 6               ;   shadow outside it
MP_H        equ 8
MP_SEL      equ 10              ; byte: the selected item, 0FFh none
MP_SAVED    equ 11              ; byte: 1 = what it covers is banked
MP_SH       equ 12              ; the bank's handle and segment
MP_SS       equ 14

MF_BYPOSITION equ 0x0400
MF_SEPARATOR  equ 0x0800
MF_GRAYED     equ 0x0001
MF_DISABLED   equ 0x0002
MF_BITMAP     equ 0x0004
MF_CHECKED    equ 0x0008
MF_POPUP      equ 0x0010
MF_MENUBARBREAK equ 0x0020
MF_MENUBREAK  equ 0x0040
MF_HILITE     equ 0x0080
MF_END        equ 0x0080        ; a template's last item (MF_HILITE's bit)
MF_OWNERDRAW  equ 0x0100
MF_APPEND     equ 0x0100        ; ChangeMenu's verbs
MF_DELETE     equ 0x0200
MF_CHANGE     equ 0x0080
MF_REMOVE     equ 0x1000
MF_HELP       equ 0x4000
MF_KEEP       equ 0x49FF        ; what an item keeps of the flags it is given
MF_NOPICK     equ 0x0813        ; MF_SEPARATOR | MF_GRAYED | MF_DISABLED |
                                ;   MF_POPUP, as a literal for t_bits
%if MF_NOPICK != (MF_SEPARATOR | MF_GRAYED | MF_DISABLED | MF_POPUP)
%error MF_NOPICK is not its members
%endif

RT_MENU       equ 4
RT_ACCEL      equ 9
WM_COMMAND    equ 0x0111
WM_INITMENU   equ 0x0116
WM_INITMENUPOPUP equ 0x0117
WM_NCLBUTTONDOWN equ 0x00A1
WM_SYSKEYUP   equ 0x0105
WM_SYSCHAR    equ 0x0106
HTMENU        equ 5
SC_KEYMENU    equ 0xF100
VK_F4         equ 0x73
VK_F10        equ 0x79

FVIRTKEY      equ 0x01          ; an ACCEL's fFlags
FSHIFT        equ 0x04
FCONTROL      equ 0x08
FALT          equ 0x10

; =============================================================================
; The USER heap and the records
; =============================================================================

; w16_mheap - ES = the USER heap's segment, made on first use. CF=1 no memory.
; Keeps the rest.
w16_mheap:
    push ax
    push dx
    mov ax, [w16_mseg]
    or ax, ax
    jnz .have
    push bx
    push cx
    mov ax, W16_MHEAPB
    xor dx, dx
    xor cl, cl                      ; fixed
    call w16_gnew                   ; AX = the handle, DX = the segment
    or ax, ax
    jz .fail
    mov [w16_mhh], ax
    mov [w16_mseg], dx
    mov ax, dx
    mov bx, 16
    mov dx, W16_MHEAPB
    call w16_linit
    pop cx
    pop bx
    mov ax, [w16_mseg]
.have:
    mov es, ax
    pop dx
    pop ax
    clc
    ret
.fail:
    pop cx
    pop bx
    pop dx
    pop ax
    stc
    ret

; w16_mget - AX = an HMENU -> ES = the USER heap, SI = its record. CF=1 it is
; not one. Keeps AX, BX, CX, DX, DI.
w16_mget:
    cmp word [w16_mseg], 0
    je .no
    mov es, [w16_mseg]
    push ax
    push di
    sub ax, W16_HMB
    call w16_lfind                  ; SI = the data
    pop di
    pop ax
    jc .no
    cmp word [es:si + MH_MAGIC], MH_SIG
    jne .no
    clc
    ret
.no:
    stc
    ret

; w16_mnew - a new, empty menu -> AX = its HMENU, 0 = no memory. ES = the
; USER heap. Keeps the rest.
w16_mnew:
    push cx
    push si
    call w16_mheap
    jc .no
    mov cx, MH_SIZE
    mov al, LMEM_ZEROINIT
    call w16_lnew
    or ax, ax
    jz .out
    mov si, ax
    mov word [es:si + MH_MAGIC], MH_SIG
    mov byte [es:si + MH_FL], MHF_DIRTY
    add ax, W16_HMB
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    pop cx
    ret

; w16_mitem - SI = a record, CX = an index -> DI = the item. Keeps the rest.
w16_mitem:
    push ax
    push dx
    mov ax, MI_SIZE
    mul cx
    add ax, [es:si + MH_ITEMS]
    mov di, ax
    pop dx
    pop ax
    ret

; w16_minsert - SI = a record, CX = a position (past the end appends) -> DI =
; a new zeroed item there, CX = its index. CF=1 no memory. Keeps AX, BX, DX.
w16_minsert:
    push ax
    push bx
    push dx
    mov al, [es:si + MH_N]
    xor ah, ah
    cmp cx, ax
    jbe .pos
    mov cx, ax
.pos:
    cmp al, [es:si + MH_CAP]
    jb .room
    cmp al, 240
    jae .fail
    push cx                         ; grow the block by eight items
    mov bl, [es:si + MH_CAP]
    add bl, 8
    mov al, MI_SIZE
    mul bl
    mov cx, ax
    mov al, LMEM_ZEROINIT
    call w16_lnew
    pop cx
    or ax, ax
    jz .fail
    mov [es:si + MH_CAP], bl
    push cx
    push si
    push di
    mov di, ax
    mov dx, [es:si + MH_ITEMS]
    mov [es:si + MH_ITEMS], ax
    mov al, MI_SIZE
    mul byte [es:si + MH_N]
    mov cx, ax
    mov si, dx
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
    mov ax, dx
    or ax, ax
    jz .nofree
    call w16_lfree
.nofree:
    pop di
    pop si
    pop cx
.room:
    mov al, [es:si + MH_N]          ; the items from CX on move up one
    xor ah, ah
    sub ax, cx
    mov dx, MI_SIZE
    mul dx
    inc byte [es:si + MH_N]
    or byte [es:si + MH_FL], MHF_DIRTY
    call w16_mitem                  ; DI = item CX
    push cx
    push si
    push di
    mov cx, ax
    jcxz .moved
    mov si, di
    add si, cx
    dec si
    mov di, si
    add di, MI_SIZE
    push ds
    push es
    pop ds
    std
    rep movsb
    cld
    pop ds
.moved:
    pop di
    pop si
    pop cx
    push cx
    push di
    mov cx, MI_SIZE / 2
    xor ax, ax
    rep stosw
    pop di
    pop cx
    clc
    jmp short .out
.fail:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; w16_mremove - SI = a record, CX = an index, AL = 1 to destroy a popup it
; names: take the item out. Keeps every register.
w16_mremove:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, al
    call w16_mitem
    mov ax, [es:di + MI_TEXT]
    or ax, ax
    jz .nt
    call w16_lfree
.nt:
    test byte [es:di + MI_FLAGS], MF_POPUP
    jz .shift
    or bl, bl
    jz .shift
    mov ax, [es:di + MI_ID]
    push si
    push di
    call w16_mdestroy
    pop di
    pop si
.shift:
    mov al, [es:si + MH_N]
    xor ah, ah
    dec ax
    sub ax, cx
    mov dx, MI_SIZE
    mul dx
    mov cx, ax
    push si
    mov si, di
    add si, MI_SIZE
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
    pop si
    dec byte [es:si + MH_N]
    or byte [es:si + MH_FL], MHF_DIRTY
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mdestroy - AX = an HMENU: it, its strings and its popups go. Keeps all.
w16_mdestroy:
    push ax
    push cx
    push si
    push di
    push es
    call w16_mget
    jc .out
    mov word [es:si + MH_MAGIC], 0  ; first, so a menu naming itself ends
.it:
    cmp byte [es:si + MH_N], 0
    je .free
    xor cx, cx
    mov al, 1
    call w16_mremove
    jmp short .it
.free:
    mov ax, [es:si + MH_ITEMS]
    or ax, ax
    jz .h
    call w16_lfree
.h:
    mov ax, si
    call w16_lfree
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16_msettext - DI = an item: its string becomes a copy of the far string
; [w16_mstr] (a NULL segment: none). CF=1 no memory. Keeps every register.
w16_msettext:
    push ax
    push cx
    push si
    push di
    mov ax, [es:di + MI_TEXT]
    or ax, ax
    jz .old
    call w16_lfree
    mov word [es:di + MI_TEXT], 0
.old:
    cmp word [w16_mstr + 2], 0
    je .ok
    push es
    les si, [w16_mstr]
    xor cx, cx
.len:
    cmp byte [es:si], 0
    je .lend
    inc si
    inc cx
    cmp cx, W16_MLAB + W16_MACC
    jb .len
.lend:
    pop es
    push cx
    inc cx
    xor al, al
    call w16_lnew
    pop cx
    or ax, ax
    jz .fail
    mov [es:di + MI_TEXT], ax
    mov di, ax
    push ds
    lds si, [w16_mstr]
    cld
    rep movsb
    pop ds
    mov byte [es:di], 0
.ok:
    clc
    jmp short .out
.fail:
    stc
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16_mput - DI = an item of the record SI: flags DX, ID BX, the string
; [w16_mstr]. A NULL string is a separator, as Wine's MENU_SetItemData has
; it. CF=1 no memory, or a popup that is not a menu. Keeps every register.
w16_mput:
    push ax
    push dx
    push si
    and dx, MF_KEEP
    test dx, MF_POPUP
    jz .np
    mov ax, bx
    push si
    push es
    call w16_mget
    pop es
    pop si
    jc .out
.np:
    test dx, MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR
    jz .str
    mov word [w16_mstr + 2], 0
    jmp short .set
.str:
    cmp word [w16_mstr + 2], 0
    jne .set
    or dx, MF_SEPARATOR
.set:
    mov [es:di + MI_FLAGS], dx
    mov [es:di + MI_ID], bx
    or byte [es:si + MH_FL], MHF_DIRTY
    call w16_msettext
.out:
    pop si
    pop dx
    pop ax
    ret

; w16_mappend - AX = an HMENU, DX = flags, BX = the ID, [w16_mstr] = the
; string: a new last item. CF=1 refused. Keeps every register.
w16_mappend:
    push cx
    push si
    push di
    call w16_mget
    jc .out
    mov cx, 0xFFFF
    call w16_minsert
    jc .out
    call w16_mput
.out:
    pop di
    pop si
    pop cx
    ret

; w16_mfindcmd - SI = a record, AX = a command -> SI = the record holding it
; (popups searched first, as Wine's MENU_FindItem does), CX = its index, DI =
; the item. CF=1 none. Keeps AX, BX, DX.
w16_mfindcmd:
    xor cx, cx
.l:
    cmp cl, [es:si + MH_N]
    jae .no
    call w16_mitem
    test byte [es:di + MI_FLAGS], MF_POPUP
    jz .plain
    push si
    push cx
    push ax
    mov ax, [es:di + MI_ID]
    call w16_mget
    pop ax
    jc .subno
    call w16_mfindcmd
    jc .subno
    add sp, 4
    clc
    ret
.subno:
    pop cx
    pop si
    call w16_mitem
.plain:
    cmp [es:di + MI_ID], ax
    je .hit
    inc cx
    jmp short .l
.hit:
    clc
    ret
.no:
    stc
    ret

; w16_mloc - AX = an HMENU, CX = an item, DX = flags (MF_BYPOSITION or by
; command) -> ES:DI = the item, SI = its record, CX = its index. CF=1 none.
; Keeps AX, BX, DX.
w16_mloc:
    call w16_mget
    jc .ret
    test dx, MF_BYPOSITION
    jz .cmd
    cmp cl, [es:si + MH_N]
    jae .no
    or ch, ch
    jnz .no
    call w16_mitem
    clc
.ret:
    ret
.no:
    stc
    ret
.cmd:
    push ax
    mov ax, cx
    call w16_mfindcmd
    pop ax
    ret

; w16_mprep - DI = an item: its label into w16_mlab ([w16_mlabn] characters,
; the '&'s gone and [w16_mmn] the one underlined, 0FFh none, [w16_mmch] it in
; upper case), the text after a tab into w16_macc ([w16_maccn]), and
; [w16_mhelp] = 1 for a '\b' item. Keeps every register.
w16_mprep:
    push ax
    push bx
    push cx
    push si
    push di
    mov byte [w16_mmn], 0xFF
    mov byte [w16_mmch], 0
    mov byte [w16_mhelp], 0
    xor cx, cx
    xor bx, bx
    mov si, [es:di + MI_TEXT]
    mov di, w16_mlab
    or si, si
    jz .done
    cmp byte [es:si], 8
    jne .l
    inc si
    mov byte [w16_mhelp], 1
.l:
    mov al, [es:si]
    inc si
    or al, al
    jz .done
    cmp al, 9
    je .acc
    cmp al, 7
    je .acc
    cmp al, '&'
    jne .put
    mov al, [es:si]
    inc si
    or al, al
    jz .done
    cmp al, '&'
    je .put
    cmp byte [w16_mmn], 0xFF
    jne .put
    mov [w16_mmn], cl
    mov ah, al
    cmp ah, 'a'
    jb .u
    cmp ah, 'z'
    ja .u
    sub ah, 'a' - 'A'
.u:
    mov [w16_mmch], ah
.put:
    cmp cx, W16_MLAB
    jae .l
    mov [di], al
    inc di
    inc cx
    jmp short .l
.acc:
    mov al, [es:si]
    inc si
    or al, al
    jz .done
    cmp bx, W16_MACC
    jae .acc
    mov [w16_macc + bx], al
    inc bx
    jmp short .acc
.done:
    mov byte [di], 0
    mov [w16_mlabn], cx
    mov [w16_maccn], bx
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Templates: LoadMenu, LoadMenuIndirect
; =============================================================================

; w16_mparse - AX = an HMENU: append the MENUITEMTEMPLATEs at
; [w16_mtps]:[w16_mtp] to it, down to the MF_END item, popups made and
; parsed in turn (Wine's MENU_ParseResource). CF=1 failed. Keeps all but AX.
w16_mparse:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cx, ax
.item:
    mov es, [w16_mtps]
    mov si, [w16_mtp]
    mov dx, [es:si]
    add si, 2
    xor bx, bx
    test dl, MF_POPUP
    jnz .str
    mov bx, [es:si]
    add si, 2
.str:
    mov [w16_mstr], si
    mov [w16_mstr + 2], es
.sk:
    mov al, [es:si]
    inc si
    or al, al
    jnz .sk
    mov [w16_mtp], si
    push dx                         ; MF_END, for after
    and dx, ~MF_END
    test dl, MF_POPUP
    jz .plain
    push cx
    push word [w16_mstr]
    push word [w16_mstr + 2]
    call w16_mnew
    mov bx, ax
    or ax, ax
    jz .nomem
    call w16_mparse
.nomem:
    pop word [w16_mstr + 2]
    pop word [w16_mstr]
    pop cx
    jc .failsub
    or bx, bx
    jz .fail
    jmp short .add
.failsub:
    mov ax, bx
    call w16_mdestroy
    jmp short .fail
.plain:
    les si, [w16_mstr]
    cmp byte [es:si], 0
    jne .add
    mov dx, MF_SEPARATOR
    mov word [w16_mstr + 2], 0
.add:
    mov ax, cx
    call w16_mappend
    pop dx
    jc .out
    test dl, MF_END
    jz .item
    clc
    jmp short .out
.fail:
    pop dx
    stc
.out:
    mov ax, cx
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_mtpl - a template at [w16_mtps]:[w16_mtp] (its header first) -> AX =
; the HMENU, 0 = refused. Keeps the rest.
w16_mtpl:
    push si
    push es
    mov es, [w16_mtps]
    mov si, [w16_mtp]
    mov ax, [es:si + 2]             ; MENUITEMTEMPLATEHEADER's offset
    add ax, 4
    add [w16_mtp], ax
    call w16_mnew
    or ax, ax
    jz .out
    call w16_mparse
    jnc .out
    call w16_mdestroy
    xor ax, ax
.out:
    pop es
    pop si
    ret

; w16_mload - DX:AX = a menu's name or MAKEINTRESOURCE in this program ->
; AX = the HMENU, 0 = none. Keeps the rest.
w16_mload:
    push bx
    push dx
    push si
    push di
    push es
    mov di, w16_rnamb
    call w16_rkey
    mov [w16_rnk], ax
    mov word [w16_rtk], 0x8000 | RT_MENU
    call w16_rfind
    or ax, ax
    jz .no
    mov bx, ax                      ; BX = the entry
    call w16_rload
    or ax, ax
    jz .no
    call w16_gfind
    jc .no
    cmp byte [w16_gdisc], 0
    jne .no
    mov ax, es
    inc ax
    mov [w16_mtps], ax
    mov word [w16_mtp], 0
    call w16_mtpl
    mov es, [w16_rtab]              ; LoadMenu frees the resource
    cmp word [es:bx + RN_USAGE], 0
    je .out
    dec word [es:bx + RN_USAGE]
    jmp short .out
.no:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    ret

; LoadMenu (USER.150) - args: +0 lpMenuName, +4 hInstance
w16u_loadmenu:
    call w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    jmp w16_mload

; LoadMenuIndirect (USER.220) - args: +0 lpMenuTemplate
w16u_loadmenuind:
    call w16_args
    mov ax, [es:bx]
    mov [w16_mtp], ax
    mov ax, [es:bx + 2]
    mov [w16_mtps], ax
    jmp w16_mtpl

; =============================================================================
; The menu API (USER.150-264, 410-415): Wine's user.exe16 shapes
; =============================================================================

; CreateMenu (USER.151), CreatePopupMenu (USER.415)
w16u_createmenu:
    jmp w16_mnew

; DestroyMenu (USER.152) - args: +0 hMenu
w16u_destroymenu:
    push si
    call w16_args
    mov ax, [es:bx]
    call w16_mget
    jc .no
    call w16_mdestroy
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop si
    ret

; w16_mitemargs - ES:BX -> the args: [w16_mstr] = the far item at +0, BX =
; the ID at +4, DX = the flags at +6. Keeps AX and CX.
w16_mitemargs:
    push ax
    mov ax, [es:bx]
    mov [w16_mstr], ax
    mov ax, [es:bx + 2]
    mov [w16_mstr + 2], ax
    mov dx, [es:bx + 6]
    mov bx, [es:bx + 4]
    pop ax
    ret

; AppendMenu (USER.411) - args: +0 lpNewItem, +4 wIDNewItem, +6 wFlags,
; +8 hMenu
w16u_appendmenu:
    call w16_args
    mov ax, [es:bx + 8]
    call w16_mitemargs
w16_mappend1:
    call w16_mappend
    mov ax, 0
    jc .out
    inc ax
.out:
    ret

; InsertMenu (USER.410) - args: +0 lpNewItem, +4 wIDNewItem, +6 wFlags,
; +8 nPosition, +10 hMenu. By command, the new item goes in front of that
; command's, in whichever popup it is; a position of -1 appends.
w16u_insertmenu:
    push si
    push di
    call w16_args
    mov cx, [es:bx + 8]
    mov ax, [es:bx + 10]
    call w16_mitemargs              ; BX = the ID, DX = the flags
w16_minsert1:
    push bx
    call w16_mget
    jc .fail
    test dx, MF_BYPOSITION
    jnz .at
    push cx
    push dx
    push ax
    mov ax, cx
    call w16_mfindcmd
    pop ax
    pop dx
    jnc .found
    pop cx
    cmp cx, 0xFFFF
    jne .fail
    call w16_mget
    jmp short .at
.found:
    add sp, 2
.at:
    pop bx
    call w16_minsert
    jc .no
    call w16_mput
    jc .no
    mov ax, 1
    jmp short .out
.fail:
    pop bx
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; ModifyMenu (USER.414) - args: as InsertMenu's
w16u_modifymenu:
    push si
    push di
    call w16_args
    mov cx, [es:bx + 8]
    mov ax, [es:bx + 10]
    call w16_mitemargs
w16_mmodify1:
    push bx
    call w16_mloc
    pop bx
    jc .no
    call w16_mput
    jc .no
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; DeleteMenu (USER.413), RemoveMenu (USER.412) - args: +0 wFlags,
; +2 nPosition, +4 hMenu. Delete destroys a popup the item names.
w16u_deletemenu:
    mov al, 1
    jmp short w16_mdel
w16u_removemenu:
    xor al, al
w16_mdel:
    push si
    push di
    push ax
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 2]
    mov ax, [es:bx + 4]
    call w16_mloc
    pop ax
    jc .no
    call w16_mremove
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; ChangeMenu (USER.153) - args: +0 wFlags, +2 wIDNewItem, +4 lpNewItem,
; +8 nPosition, +10 hMenu. Windows 2's verb, as Wine's ChangeMenu16 maps it.
w16u_changemenu:
    push si
    push di
    call w16_args
    mov dx, [es:bx]
    mov ax, [es:bx + 4]
    mov [w16_mstr], ax
    mov ax, [es:bx + 6]
    mov [w16_mstr + 2], ax
    mov cx, [es:bx + 8]
    mov ax, [es:bx + 10]
    mov bx, [es:bx + 2]
    test dx, MF_APPEND
    jz .notapp
    and dx, ~MF_APPEND
    pop di
    pop si
    jmp w16_mappend1
.notapp:
    test dx, MF_DELETE
    jz .notdel
    and dx, ~MF_DELETE
    push ax
    mov al, 1
    jmp short .del
.notdel:
    test dx, MF_CHANGE
    jz .notch
    and dx, ~MF_CHANGE
    jmp w16_mmodify1
.notch:
    test dx, MF_REMOVE
    jz .ins
    and dx, ~MF_REMOVE
    test dx, MF_BYPOSITION
    jnz .rbypos
    mov cx, bx
.rbypos:
    push ax
    xor al, al
.del:
    mov bl, al
    pop ax
    call w16_mloc
    jc .no
    mov al, bl
    call w16_mremove
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret
.ins:
    jmp w16_minsert1

; CheckMenuItem (USER.154) - args: +0 wCheck, +2 wIDCheckItem, +4 hMenu.
; Answers the old check, or -1.
w16u_checkmenu:
    push si
    push di
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 2]
    mov ax, [es:bx + 4]
    mov bx, dx
    call w16_mloc
    mov ax, -1
    jc .out
    mov ax, [es:di + MI_FLAGS]
    and ax, MF_CHECKED
    and word [es:di + MI_FLAGS], ~MF_CHECKED
    and bx, MF_CHECKED
    or [es:di + MI_FLAGS], bx
.out:
    pop di
    pop si
    ret

; EnableMenuItem (USER.155) - args: +0 wEnable, +2 wIDEnableItem, +4 hMenu.
; Answers the old state, or -1.
w16u_enablemenu:
    push si
    push di
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 2]
    mov ax, [es:bx + 4]
    mov bx, dx
    call w16_mloc
    mov ax, -1
    jc .out
    mov ax, [es:di + MI_FLAGS]
    and ax, MF_GRAYED | MF_DISABLED
    and word [es:di + MI_FLAGS], ~(MF_GRAYED | MF_DISABLED)
    and bx, MF_GRAYED | MF_DISABLED
    or [es:di + MI_FLAGS], bx
.out:
    pop di
    pop si
    ret

; GetMenuState (USER.250) - args: +0 wFlags, +2 wId, +4 hMenu. A popup
; answers its item count in the high byte (Wine's GetMenuState).
w16u_menustate:
    push si
    push di
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 2]
    mov ax, [es:bx + 4]
    call w16_mloc
    mov ax, -1
    jc .out
    mov ax, [es:di + MI_FLAGS]
    test al, MF_POPUP
    jz .out
    push ax
    mov ax, [es:di + MI_ID]
    call w16_mget
    pop ax
    jc .out
    mov ah, [es:si + MH_N]
.out:
    pop di
    pop si
    ret

; GetMenuItemCount (USER.263) - args: +0 hMenu
w16u_menucount:
    push si
    call w16_args
    mov ax, [es:bx]
    call w16_mget
    mov ax, -1
    jc .out
    mov al, [es:si + MH_N]
    xor ah, ah
.out:
    pop si
    ret

; GetMenuItemID (USER.264) - args: +0 nPos, +2 hMenu. A popup answers -1.
w16u_menuitemid:
    push si
    push di
    call w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 2]
    mov dx, MF_BYPOSITION
    call w16_mloc
    mov ax, -1
    jc .out
    test byte [es:di + MI_FLAGS], MF_POPUP
    jnz .out
    mov ax, [es:di + MI_ID]
.out:
    pop di
    pop si
    ret

; GetSubMenu (USER.159) - args: +0 nPos, +2 hMenu
w16u_getsubmenu:
    push si
    push di
    call w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 2]
    mov dx, MF_BYPOSITION
    call w16_mloc
    mov ax, 0
    jc .out
    test byte [es:di + MI_FLAGS], MF_POPUP
    jz .out
    mov ax, [es:di + MI_ID]
.out:
    pop di
    pop si
    ret

; GetMenuString (USER.161) - args: +0 wFlags, +2 nMaxCount, +4 lpString,
; +8 wIDItem, +10 hMenu. Answers the characters copied.
w16u_menustring:
    push si
    push di
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 8]
    mov ax, [es:bx + 10]
    call w16_mloc
    mov ax, 0
    jc .out
    mov si, [es:di + MI_TEXT]
    push es
    call w16_args
    mov cx, [es:bx + 2]
    les di, [es:bx + 4]
    pop ds                          ; DS = the USER heap, for the copy
    xor ax, ax
    jcxz .back
    dec cx
    or si, si
    jz .nul
.cp:
    jcxz .nul
    mov dl, [si]
    or dl, dl
    jz .nul
    mov [es:di], dl
    inc si
    inc di
    inc ax
    dec cx
    jmp short .cp
.nul:
    mov byte [es:di], 0
.back:
    push cs
    pop ds
.out:
    pop di
    pop si
    ret

; GetMenu (USER.157) - args: +0 hWnd
w16u_getmenu:
    call w16_args
    mov ax, [es:bx]
    call w16_hwnd0
    mov ax, 0
    jc .out
    mov ax, [bx + W16W_MENU]
.out:
    ret

; SetMenu (USER.158) - args: +0 hMenu, +2 hWnd. The bar comes or goes, the
; client moves under it, and the program hears WM_SIZE.
w16u_setmenu:
    push si
    call w16_args
    mov dx, [es:bx]
    mov ax, [es:bx + 2]
    call w16_hwnd2rec
    jc .no
    or dx, dx
    jz .set
    push ax
    mov ax, dx
    call w16_mget
    pop ax
    jc .no
.set:
    mov [bx + W16W_MENU], dx
    push ax
    push bx
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 1
    call w16_inval
    call w16_mbardraw
    pop bx
    pop ax
    call w16_sizemsg
    mov si, w16_cont_one
    call w16_sendwnd
    pop si
    ret
.no:
    xor ax, ax
    pop si
    ret

; DrawMenuBar (USER.160) - args: +0 hWnd
w16u_drawmenubar:
    call w16_mbardraw
    xor ax, ax
    ret

; GetSystemMenu (USER.156) - args: +0 bRevert, +2 hWnd. A real popup, the
; Windows 3.0 items, which a program may edit; os8088's frame never shows it.
w16u_sysmenu:
    push si
    push di
    call w16_args
    cmp word [es:bx], 0
    je .get
    mov ax, [w16_msys]
    or ax, ax
    jz .zero
    call w16_mdestroy
    mov word [w16_msys], 0
.zero:
    xor ax, ax
    jmp short .out
.get:
    mov ax, [w16_msys]
    or ax, ax
    jnz .out
    call w16_mnew
    or ax, ax
    jz .out
    mov [w16_msys], ax
    mov si, w16_systab
.it:
    mov bx, [cs:si]
    mov dx, [cs:si + 2]
    mov cx, [cs:si + 4]
    mov [w16_mstr], cx
    mov [w16_mstr + 2], cs
    or cx, cx
    jnz .s
    mov word [w16_mstr + 2], 0
.s:
    call w16_mappend
    add si, 6
    cmp si, w16_systab_end
    jb .it
.out:
    pop di
    pop si
    ret

w16_systab:                     ; ID, flags, text: Windows 3.0's system menu
    dw 0xF120, MF_GRAYED, w16_s_sysrest
    dw 0xF010, 0, w16_s_sysmove
    dw 0xF000, 0, w16_s_syssize
    dw 0xF020, 0, w16_s_sysmin
    dw 0xF030, 0, w16_s_sysmax
    dw 0, MF_SEPARATOR, 0
    dw 0xF060, 0, w16_s_sysclose
    dw 0, MF_SEPARATOR, 0
    dw 0xF130, 0, w16_s_sysswitch
w16_systab_end:
w16_s_sysrest:   db '&Restore', 0
w16_s_sysmove:   db '&Move', 0
w16_s_syssize:   db '&Size', 0
w16_s_sysmin:    db 'Mi&nimize', 0
w16_s_sysmax:    db 'Ma&ximize', 0
w16_s_sysclose:  db '&Close', 9, 'Alt+F4', 0
w16_s_sysswitch: db 'S&witch To...', 9, 'Ctrl+Esc', 0

; =============================================================================
; Layout
; =============================================================================

; w16_frows - AX = the System font's rows. Keeps the rest.
w16_frows:
    push es
    mov es, [w16_face]
    mov al, [es:W16F_ROWS]
    xor ah, ah
    pop es
    ret

; w16_mlaybar - SI = a bar's record, CX = the width it has: each item's
; place, a line for as many as fit, and MH_H = the lines and the line under
; them. Keeps every register.
w16_mlaybar:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [es:si + MH_W], cx
    mov [w16_mlw], cx
    call w16_frows
    add ax, 2
    mov [w16_mlh], ax
    xor bx, bx                      ; BX = x, DX = y
    xor dx, dx
    xor cx, cx
.it:
    cmp cl, [es:si + MH_N]
    jae .end
    call w16_mitem
    call w16_mprep
    push si
    push cx
    mov si, w16_mlab
    mov cx, [w16_mlabn]
    call w16_textw
    pop cx
    pop si
    add ax, 16                      ; AX = its width
    test word [es:di + MI_FLAGS], MF_MENUBARBREAK | MF_MENUBREAK
    jnz .brk
    push ax
    add ax, bx
    cmp ax, [w16_mlw]
    pop ax
    jbe .fit
.brk:
    or bx, bx
    jz .fit
    xor bx, bx
    add dx, [w16_mlh]
.fit:
    cmp byte [w16_mhelp], 0         ; a help item goes to the right
    je .place
    push ax
    neg ax
    add ax, [w16_mlw]
    cmp ax, bx
    jl .nh
    mov bx, ax
.nh:
    pop ax
.place:
    mov [es:di + MI_X], bx
    mov [es:di + MI_Y], dx
    mov [es:di + MI_W], ax
    add bx, ax
    mov ax, [w16_mlh]
    mov [es:di + MI_H], ax
    inc cx
    jmp short .it
.end:
    add dx, [w16_mlh]
    inc dx
    mov [es:si + MH_H], dx
    and byte [es:si + MH_FL], ~MHF_DIRTY
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mlaypop - SI = a popup's record: the client's size, the accelerator
; column and each item's row. Keeps every register.
w16_mlaypop:
    push ax
    push bx
    push cx
    push dx
    push di
    xor bx, bx                      ; BX = the widest label, DX = accelerator
    xor dx, dx
    xor cx, cx
.w:
    cmp cl, [es:si + MH_N]
    jae .wend
    call w16_mitem
    test word [es:di + MI_FLAGS], MF_SEPARATOR
    jnz .wn
    call w16_mprep
    push si
    push cx
    mov si, w16_mlab
    mov cx, [w16_mlabn]
    call w16_textw
    cmp ax, bx
    jbe .l
    mov bx, ax
.l:
    mov si, w16_macc
    mov cx, [w16_maccn]
    call w16_textw
    cmp ax, dx
    jbe .a
    mov dx, ax
.a:
    pop cx
    pop si
.wn:
    inc cx
    jmp short .w
.wend:
    lea ax, [bx + 15 + 8]
    mov [es:si + MH_AX], ax
    lea ax, [bx + 15 + 14]
    or dx, dx
    jz .nacc
    add ax, dx
    add ax, 8
.nacc:
    mov [es:si + MH_W], ax
    mov bx, ax
    call w16_frows
    add ax, 2
    mov [w16_mlh], ax
    xor dx, dx
    xor cx, cx
.y:
    cmp cl, [es:si + MH_N]
    jae .yend
    call w16_mitem
    mov word [es:di + MI_X], 0
    mov [es:di + MI_Y], dx
    mov [es:di + MI_W], bx
    mov ax, 7
    test word [es:di + MI_FLAGS], MF_SEPARATOR
    jnz .h
    mov ax, [w16_mlh]
.h:
    mov [es:di + MI_H], ax
    add dx, ax
    inc cx
    jmp short .y
.yend:
    mov [es:si + MH_H], dx
    and byte [es:si + MH_FL], ~MHF_DIRTY
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mbarh - AX = the bar's height in the top-level window's content, laid
; out for its width now; 0 = it has no menu. Keeps the rest.
w16_mbarh:
    push bx
    push cx
    push dx
    push si
    push es
    call w16_toprec
    mov ax, 0
    jc .out
    mov ax, [bx + W16W_MENU]
    or ax, ax
    jz .out
    call w16_mget
    mov ax, 0
    jc .out
    mov bx, [w16_win]
    call OSAPI_WM_GEOM              ; CX = the content's width
    mov ax, 0
    jc .out
    test byte [es:si + MH_FL], MHF_DIRTY
    jnz .lay
    cmp cx, [es:si + MH_W]
    je .have
.lay:
    call w16_mlaybar
.have:
    mov ax, [es:si + MH_H]
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; Drawing: a band an item (w16gdi.inc's band and composer)
; =============================================================================

; w16_mgon - the lock and the clip for drawing a menu, unless W_PAINT or an
; open DC already has them; [w16_mcx0]/[w16_mcy0] = the content's origin.
; CF=1: nothing of the window is visible, draw nothing (and no w16_mgoff).
w16_mgon:
    push ax
    push bx
    push dx
    mov byte [w16_mclip], 0
    mov byte [w16_mtook], 0
    cmp byte [w16_inwp], 0
    jne .org
    cmp byte [w16_locked], 0
    jne .clip
    cmp byte [w16_dctook], 0
    jne .clip
    cmp byte [w16_ltook], 0
    jne .clip
    call OSAPI_GFX_LOCK
    mov byte [w16_mtook], 1
.clip:
    cmp byte [w16_dcclip], 0
    jne .org
    mov bx, [w16_win]
    call OSAPI_WM_CLIP_SET
    jc .hide
    mov byte [w16_mclip], 1
.org:
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT
    mov [w16_mcx0], ax
    mov [w16_mcy0], dx
    pop dx
    pop bx
    pop ax
    clc
    ret
.hide:
    call w16_mgoff
    pop dx
    pop bx
    pop ax
    stc
    ret

; w16_mgoff - give back what w16_mgon took. Keeps every register.
w16_mgoff:
    cmp byte [w16_mclip], 0
    je .l
    mov byte [w16_mclip], 0
    call OSAPI_WM_CLIP_CLEAR
.l:
    cmp byte [w16_mtook], 0
    je .out
    mov byte [w16_mtook], 0
    call OSAPI_GFX_UNLOCK
.out:
    ret

; w16_mbopen - a band from screen x AX to CX (inclusive), DX rows, cleared.
; Keeps every register.
w16_mbopen:
    push ax
    push cx
    push dx
    push di
    push es
    mov [w16_rl], ax
    mov [w16_rr], cx
    mov [w16_mbh], dx
    and ax, 0xFFF8
    sub ax, 16
    mov [w16_tb0], ax
    mov ax, cx
    sub ax, [w16_tb0]
    add ax, 8 + 7
    mov cl, 3
    shr ax, cl
    mov [w16_tnb], ax
    push ds
    pop es
    mov di, w16_gbuf
    cld
.clr:
    push di
    mov cx, [w16_tnb]
    xor al, al
    rep stosb
    pop di
    add di, W16_BS
    dec dx
    jnz .clr
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; w16_mbstr - SI -> CX characters in our segment, composed into the band's
; top rows with the pen at screen x DX. Keeps every register.
w16_mbstr:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [w16_face]
    mov word [w16_r0], 0
    mov al, [es:W16F_ROWS]
    xor ah, ah
    dec ax
    mov [w16_r1], ax
    jcxz .out
.g:
    push cx
    mov al, [si]
    call w16_glyph
    mov cl, [es:W16F_ADV + bx]
    xor ch, ch
    mov ax, dx
    add ax, cx
    cmp ax, [w16_rl]
    jle .next
    cmp dx, [w16_rr]
    jg .stop
    call w16_compose
.next:
    add dx, cx
    inc si
    pop cx
    loop .g
    jmp short .out
.stop:
    pop cx
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mbhl - set the band's row DX from screen x AX to CX (inclusive, cut to
; the band). Keeps every register.
w16_mbhl:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp ax, [w16_rl]
    jge .a
    mov ax, [w16_rl]
.a:
    cmp cx, [w16_rr]
    jle .b
    mov cx, [w16_rr]
.b:
    cmp ax, cx
    jg .out
    push ax
    mov ax, W16_BS
    mul dx
    add ax, w16_gbuf
    mov di, ax
    pop ax
    sub ax, [w16_tb0]
    sub cx, [w16_tb0]
.px:
    mov bx, ax
    push cx
    mov cl, 3
    shr bx, cl
    mov cl, al
    and cl, 7
    mov ch, 0x80
    shr ch, cl
    or [di + bx], ch
    pop cx
    inc ax
    cmp ax, cx
    jle .px
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mbbits - SI -> CX rows of an 8-pixel-wide picture (bit 7 the left),
; ORed into the band from row DX at screen x AX. Keeps every register.
w16_mbbits:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    sub ax, [w16_tb0]
    mov bx, ax
    push cx
    mov cl, 3
    shr bx, cl
    pop cx
    push ax
    mov ax, W16_BS
    mul dx
    add ax, w16_gbuf
    add bx, ax
    pop ax
    and al, 7
    mov dl, al                      ; DL = the shift
.r:
    push cx
    mov ah, [si]
    xor al, al
    mov cl, dl
    shr ax, cl
    or [bx], ah
    or [bx + 1], al
    inc si
    add bx, W16_BS
    pop cx
    loop .r
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; w16_mbmask - over screen x AX to CX (inclusive) on every row of the band:
; BL = 0 keeps every other pixel (a grey item on a bar, and any on a 1bpp
; display), 1 inverts (the bar's selection). Keeps every register.
w16_mbmask:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    sub ax, [w16_tb0]
    sub cx, [w16_tb0]
    cmp ax, cx
    jg .out
    mov si, ax
    mov di, cx
    push cx
    mov cl, 3
    shr si, cl                      ; SI = the first byte, DI = the last
    shr di, cl
    mov cl, al
    and cl, 7
    mov bh, 0xFF
    shr bh, cl                      ; BH = the first byte's mask
    pop cx
    and cl, 7
    mov ch, 7
    sub ch, cl
    mov cl, ch
    mov ch, 0xFF
    shl ch, cl                      ; CH = the last byte's mask
    mov [w16_mbfb], si
    mov [w16_mblb], di
    mov dl, 0xAA                    ; DL = the grey's pattern, DH = the row
    xor dh, dh
.row:
    mov al, W16_BS
    mul dh
    add ax, w16_gbuf
    mov si, ax
    add si, [w16_mbfb]
    mov bp, si
    mov di, ax
    add di, [w16_mblb]
.b:
    mov ah, 0xFF
    cmp si, bp
    jne .nf
    and ah, bh
.nf:
    cmp si, di
    jne .nl
    and ah, ch
.nl:
    or bl, bl
    jz .grey
    xor [si], ah
    jmp short .nx
.grey:
    not ah
    or ah, dl
    and [si], ah
.nx:
    inc si
    cmp si, di
    jbe .b
    not dl
    inc dh
    mov al, dh
    xor ah, ah
    cmp ax, [w16_mbh]
    jb .row
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mlit - AL = a colour -> AL = 1 if a 1bpp display lights it. Keeps the
; rest.
w16_mlit:
    push bx
    mov bl, al
    and bx, 15
    mov al, [w16_monolit + bx]
    pop bx
    ret

; w16_mbflush - the band to the screen at row BX, in [w16_mink] on
; [w16_mpap]: ONE OSAPI_GFX_BLIT1. On a 1bpp display the band is made into
; lit and unlit first, as w16_txtdraw's is. Keeps every register.
w16_mbflush:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov word [w16_r0], 0
    mov ax, [w16_mbh]
    dec ax
    mov [w16_r1], ax
    cmp byte [w16_mono], 0
    je .pen
    mov al, [w16_mpap]
    call w16_mlit
    mov ah, al
    mov al, [w16_mink]
    call w16_mlit                   ; AL = the ink lit, AH = the paper
    or ah, ah
    jz .dark
    or al, al
    jnz .fill
    call w16_bandnot
    jmp short .pen
.fill:
    call w16_bandfill
    jmp short .pen
.dark:
    or al, al
    jnz .pen
    call w16_bandfill
    call w16_bandnot
.pen:
    mov al, [w16_mink]
    mov ah, [w16_mpap]
    call OSAPI_GFX_BLIT1_PEN
    push ds
    pop es
    mov si, w16_gbuf + 2
    mov bp, W16_BS
    mov ax, [w16_rl]
    mov cx, [w16_rr]
    sub cx, ax
    inc cx
    mov dx, [w16_mbh]
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_munder - the underline under the mnemonic of the label in w16_mlab
; whose pen is at screen x DX: on the band's row ascent + 1, the character's
; advance wide. Keeps every register.
w16_munder:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov cl, [w16_mmn]
    cmp cl, 0xFF
    je .out
    xor ch, ch
    mov si, w16_mlab
    call w16_textw
    add dx, ax                      ; DX = the character's x
    mov bl, [w16_mmn]
    xor bh, bh
    mov al, [w16_mlab + bx]
    mov es, [w16_face]
    call w16_glyph
    mov al, [es:W16F_ADV + bx]
    xor ah, ah
    mov cx, dx
    add cx, ax
    dec cx
    mov ax, dx
    mov dl, [es:W16F_ASC]
    xor dh, dh
    inc dx
    call w16_mbhl
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the bar ---------------------------------------------------------------------

; w16_mbardraw - the top-level window's menu bar, now: a band a line and the
; line under them. Keeps every register.
w16_mbardraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call w16_toprec
    jc .out
    mov ax, [bx + W16W_MENU]
    or ax, ax
    jz .out
    call w16_mbarh                  ; lays it out for the width it has
    or ax, ax
    jz .out
    mov ax, [bx + W16W_MENU]
    call w16_mget
    jc .out
    call w16_mgon
    jc .out
    call w16_frows
    add ax, 2
    mov [w16_mlh], ax
    xor dx, dx                      ; DX = the line's y
.line:
    mov ax, [es:si + MH_H]
    dec ax
    cmp dx, ax
    jae .last
    push dx
    mov ax, [w16_mcx0]
    mov cx, ax
    add cx, [es:si + MH_W]
    dec cx
    mov dx, [w16_mlh]
    call w16_mbopen
    pop dx
    xor cx, cx
.it:
    cmp cl, [es:si + MH_N]
    jae .flush
    call w16_mitem
    cmp [es:di + MI_Y], dx
    jne .in
    call w16_mbaritem
.in:
    inc cx
    jmp short .it
.flush:
    mov byte [w16_mink], CBLACK
    mov byte [w16_mpap], CWHITE
    mov bx, [w16_mcy0]
    add bx, dx
    call w16_mbflush
    add dx, [w16_mlh]
    jmp short .line
.last:
    mov al, CBLACK
    call w16_ink
    mov ax, [w16_mcx0]
    mov cx, ax
    add cx, [es:si + MH_W]
    dec cx
    mov bx, [w16_mcy0]
    add bx, dx
    mov dx, bx
    call OSAPI_GFX_FILL
    call w16_mgoff
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mbaritem - item CX (DI) of the bar into the open band: its label 8 in,
; the underline, a grey item's pixels halved, the selection inverted.
; Keeps every register.
w16_mbaritem:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, cx                      ; BX = the index
    call w16_mprep
    mov dx, [w16_mcx0]
    add dx, [es:di + MI_X]
    add dx, 8
    mov si, w16_mlab
    mov cx, [w16_mlabn]
    call w16_mbstr
    call w16_munder
    mov ax, [w16_mcx0]
    add ax, [es:di + MI_X]
    mov cx, ax
    add cx, [es:di + MI_W]
    dec cx
    push bx
    test word [es:di + MI_FLAGS], MF_GRAYED
    jz .ng
    xor bl, bl
    call w16_mbmask
.ng:
    pop bx
    cmp byte [w16_mtrk], 0
    je .out
    cmp bl, [w16_mbsel]
    jne .out
    mov bl, 1
    call w16_mbmask
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- popups ----------------------------------------------------------------------

; w16_mplev - AL = a level -> BX = its record. Keeps the rest.
w16_mplev:
    push ax
    mov bl, MP_SIZE
    mul bl
    add ax, w16_mpl
    mov bx, ax
    pop ax
    ret

; w16_mprect - BX = an open popup: [w16_mx0]..[w16_my1] = its frame on the
; screen, inclusive ([w16_mcx0]/[w16_mcy0] read). Keeps every register.
w16_mprect:
    push ax
    mov ax, [w16_mcx0]
    add ax, [bx + MP_X]
    mov [w16_mx0], ax
    add ax, [bx + MP_W]
    dec ax
    mov [w16_mx1], ax
    mov ax, [w16_mcy0]
    add ax, [bx + MP_Y]
    mov [w16_my0], ax
    add ax, [bx + MP_H]
    dec ax
    mov [w16_my1], ax
    pop ax
    ret

; w16_mpitem - item CX of the open popup BX (its record SI): one band and
; one blit - the check, the label 15 in, its underline, the accelerator, the
; submenu's arrow, a separator's line; the selection black, a grey item in
; COLOR_GRAYTEXT (on a 1bpp display, its pixels halved). Inside w16_mgon.
; Keeps every register.
w16_mpitem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call w16_mitem
    mov [w16_mpidx], cl
    mov al, [bx + MP_SEL]
    mov [w16_mpsel], al
    mov ax, [w16_mcx0]
    add ax, [bx + MP_X]
    inc ax
    mov [w16_mpcl], ax              ; the client's left, on the screen
    mov ax, [w16_mcy0]
    add ax, [bx + MP_Y]
    inc ax
    add ax, [es:di + MI_Y]
    mov [w16_mpct], ax              ; ...and the item's top
    mov ax, [w16_mpcl]
    mov cx, ax
    add cx, [es:si + MH_W]
    dec cx
    mov dx, [es:di + MI_H]
    call w16_mbopen
    mov byte [w16_mink], CBLACK
    mov byte [w16_mpap], CWHITE
    test word [es:di + MI_FLAGS], MF_SEPARATOR
    jz .text
    mov ax, [w16_rl]                ; the line, border to border
    mov cx, [w16_rr]
    mov dx, 3
    call w16_mbhl
    jmp .flush
.text:
    call w16_mprep
    mov dx, [w16_mpcl]
    add dx, 15
    push si
    mov si, w16_mlab
    mov cx, [w16_mlabn]
    call w16_mbstr
    pop si
    call w16_munder
    cmp word [w16_maccn], 0
    je .nacc
    mov dx, [w16_mpcl]
    add dx, [es:si + MH_AX]
    push si
    mov si, w16_macc
    mov cx, [w16_maccn]
    call w16_mbstr
    pop si
.nacc:
    test word [es:di + MI_FLAGS], MF_CHECKED
    jz .nchk
    push si
    mov si, w16_mchk
    mov cx, 10
    mov ax, [w16_mpcl]
    add ax, 4
    mov dx, 3
    call w16_mbbits
    pop si
.nchk:
    test word [es:di + MI_FLAGS], MF_POPUP
    jz .narr
    push si
    mov ax, [w16_mpcl]
    add ax, [es:si + MH_W]
    sub ax, 6
    mov si, w16_marr
    mov cx, 9
    mov dx, 4
    call w16_mbbits
    pop si
.narr:
    mov al, [w16_mpidx]
    cmp al, [w16_mpsel]
    jne .nsel
    mov byte [w16_mink], CWHITE
    mov byte [w16_mpap], CBLACK
.nsel:
    test word [es:di + MI_FLAGS], MF_GRAYED
    jz .flush
    cmp byte [w16_mono], 0
    je .cgrey
    mov ax, [w16_rl]
    mov cx, [w16_rr]
    xor bl, bl
    call w16_mbmask
    jmp short .flush
.cgrey:
    mov byte [w16_mink], CLGRAY
.flush:
    mov bx, [w16_mpct]
    call w16_mbflush
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mpopdraw - the open popup BX, whole: its frame, its shadow, its items.
; Keeps every register.
w16_mpopdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, [bx + MP_MENU]
    call w16_mget
    jc .out
    call w16_mgon
    jc .out
    call w16_mprect
    push bx
    mov al, CBLACK
    call w16_ink
    mov ax, [w16_mx0]
    mov bx, [w16_my0]
    mov cx, [w16_mx1]
    mov dx, [w16_my1]
    call OSAPI_GFX_FRAME
    mov al, CLGRAY                  ; the shadow: right, then below -
    cmp byte [w16_mono], 0          ; black on 1bpp, where a 1-pixel line
    je .shink                       ; of the dither reads as dots
    mov al, CBLACK
.shink:
    call w16_ink
    mov ax, [w16_mx1]
    inc ax
    mov cx, ax
    mov bx, [w16_my0]
    inc bx
    mov dx, [w16_my1]
    inc dx
    call OSAPI_GFX_FILL
    mov ax, [w16_mx0]
    inc ax
    mov cx, [w16_mx1]
    mov bx, [w16_my1]
    inc bx
    mov dx, bx
    call OSAPI_GFX_FILL
    pop bx
    xor cx, cx
.it:
    cmp cl, [es:si + MH_N]
    jae .done
    call w16_mpitem
    inc cx
    jmp short .it
.done:
    call w16_mgoff
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mpsave - bank what the open popup BX (its frame and shadow) is about to
; cover, into a global block: [bx + MP_SAVED] says whether it was. Inside
; w16_mgon. Keeps every register.
w16_mpsave:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [bx + MP_SAVED], 0
    mov [w16_mrec], bx
    call w16_mprect
    mov bx, [w16_win]
    call OSAPI_WM_DISPLAY           ; DH = the planes (bytes a column on VESA)
    mov al, dh
    xor ah, ah
    mov cx, [w16_my1]
    sub cx, [w16_my0]
    add cx, 2                       ; the rows, the shadow's included
    mul cx
    or dx, dx
    jnz .out
    mov bx, [w16_mx1]
    inc bx
    mov cl, 3
    shr bx, cl
    mov si, [w16_mx0]
    shr si, cl
    sub bx, si
    inc bx                          ; the byte columns
    mul bx
    or dx, dx
    jnz .out
    cmp ax, 0xFFF0
    ja .out
    xor cl, cl
    call w16_gnew                   ; AX = the handle, DX = the segment
    or ax, ax
    jz .out
    mov bx, [w16_mrec]
    mov [bx + MP_SH], ax
    mov [bx + MP_SS], dx
    mov es, dx
    xor di, di
    mov ax, [w16_mx0]
    mov bx, [w16_my0]
    mov cx, [w16_mx1]
    inc cx
    mov dx, [w16_my1]
    inc dx
    call OSAPI_GFX_SAVE
    mov bx, [w16_mrec]
    jc .drop
    mov byte [bx + MP_SAVED], 1
    jmp short .out
.drop:
    mov ax, [bx + MP_SH]
    call w16_gfreeh
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mpclose - take the open popup BX down: its bank written back, or - a
; stale bank, or none - the area made invalid and given the class brush, for
; the program to repaint. Keeps every register.
w16_mpclose:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov [w16_mrec], bx
    cmp byte [bx + MP_SAVED], 0
    je .inval
    cmp byte [w16_mstale], 0
    jne .inval
    call w16_mgon
    jc .free
    call w16_mprect
    mov es, [bx + MP_SS]
    xor si, si
    mov ax, [w16_mx0]
    mov bx, [w16_my0]
    mov cx, [w16_mx1]
    inc cx
    mov dx, [w16_my1]
    inc dx
    call OSAPI_GFX_REST
    call w16_mgoff
    jmp short .free
.inval:
    call w16_mpinval
.free:
    mov bx, [w16_mrec]
    cmp byte [bx + MP_SAVED], 0
    je .out
    mov byte [bx + MP_SAVED], 0
    mov ax, [bx + MP_SH]
    call w16_gfreeh
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mpinval - BX = an open popup: what its frame and shadow cover of the
; client becomes invalid, erased, and is filled with the class brush now.
; Keeps every register.
w16_mpinval:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx
    call w16_mbarh
    mov si, ax                      ; SI = the bar's height
    call w16_toprec                 ; BX = the window
    jc .out
    mov ax, [di + MP_X]
    mov cx, [di + MP_Y]
    sub cx, si
    mov dx, ax
    add dx, [di + MP_W]
    inc dx
    push si
    mov si, [di + MP_Y]
    add si, [di + MP_H]
    inc si
    pop di
    sub si, di
    mov byte [w16_erase], 1
    call w16_inval                  ; (it cuts nothing: the paint will)
    mov ax, [bx + W16W_HBR]
    call w16_brcol
    cmp al, W16_NOCOL
    je .out
    call w16_mgon
    jc .out
    call w16_ink
    mov bx, [w16_mrec]
    call w16_mprect
    mov ax, [w16_mx0]
    mov bx, [w16_my0]
    mov cx, [w16_mx1]
    inc cx
    mov dx, [w16_my1]
    inc dx
    mov si, [w16_mcy0]              ; the client's top: the bar stays
    add si, di
    cmp bx, si
    jge .t
    mov bx, si
.t:
    cmp bx, dx
    jg .off
    call OSAPI_GFX_FILL
.off:
    call w16_mgoff
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mcloseto - AL = a level: take every popup at it and deeper down,
; deepest first. Keeps every register.
w16_mcloseto:
    push ax
    push bx
.l:
    cmp [w16_mlev], al
    jbe .out
    dec byte [w16_mlev]
    push ax
    mov al, [w16_mlev]
    call w16_mplev
    call w16_mpclose
    pop ax
    jmp short .l
.out:
    pop bx
    pop ax
    ret

; w16_mpopopen - AL = a level, AH = 1 to select the first item: open the
; popup [w16_mnmenu] there, at [w16_mnx], [w16_mny] (content), kept inside
; the content - laid out, banked, drawn. Keeps every register.
w16_mpopopen:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp al, W16_MDEPTH
    jae .out
    call w16_mcloseto
    call w16_mplev
    mov [w16_mrec], bx
    mov cx, [w16_mnmenu]
    mov [bx + MP_MENU], cx
    mov byte [bx + MP_SEL], 0xFF
    mov byte [bx + MP_SAVED], 0
    push ax
    mov ax, cx
    call w16_mget
    pop ax
    jc .out
    call w16_mlaypop
    mov cx, [es:si + MH_W]
    add cx, 2
    mov [bx + MP_W], cx
    mov cx, [es:si + MH_H]
    add cx, 2
    mov [bx + MP_H], cx
    push ax
    mov bx, [w16_win]
    call OSAPI_WM_GEOM              ; CX/DX = the content's size
    mov bx, [w16_mrec]
    mov ax, [w16_mnx]
    sub cx, [bx + MP_W]
    dec cx                          ; the shadow's column must fit
    cmp ax, cx
    jle .x1
    mov ax, cx
.x1:
    or ax, ax
    jge .x2
    xor ax, ax
.x2:
    mov [bx + MP_X], ax
    mov ax, [w16_mny]
    sub dx, [bx + MP_H]
    dec dx
    cmp ax, dx
    jle .y1
    mov ax, dx
.y1:
    or ax, ax
    jge .y2
    xor ax, ax
.y2:
    mov [bx + MP_Y], ax
    pop ax
    or ah, ah
    jz .nosel
    push ax
    mov ax, 0x01FF                  ; from none, forward
    call w16_mnext
    mov [bx + MP_SEL], al
    pop ax
.nosel:
    call w16_mgon
    jc .draw
    call w16_mpsave
    call w16_mgoff
.draw:
    call w16_mpopdraw
    inc al
    mov [w16_mlev], al
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mnext - SI = a record, AL = an item (0FFh none), AH = 1 forward /
; 0FFh back -> AL = the next one that is not a separator, round the end;
; 0FFh if there is none. Keeps the rest.
w16_mnext:
    push cx
    push dx
    push di
    mov dl, [es:si + MH_N]
    or dl, dl
    jz .none
    mov cl, dl
    mov dh, al
    cmp al, 0xFF
    jne .l
    cmp ah, 0xFF
    jne .l
    mov dh, dl                      ; back from none: the last
.l:
    add dh, ah
    cmp dh, 0xFF
    jne .c1
    mov dh, dl
    dec dh
.c1:
    cmp dh, dl
    jb .c2
    xor dh, dh
.c2:
    push cx
    mov cl, dh
    xor ch, ch
    call w16_mitem
    pop cx
    test word [es:di + MI_FLAGS], MF_SEPARATOR
    jz .hit
    dec cl
    jnz .l
.none:
    mov al, 0xFF
    jmp short .out
.hit:
    mov al, dh
.out:
    pop di
    pop dx
    pop cx
    ret

; w16_mmnfind - SI = a record, AL = a character in upper case -> AL = the
; first item it is the mnemonic of, 0FFh none. Keeps the rest.
w16_mmnfind:
    push cx
    push di
    xor cx, cx
.l:
    cmp cl, [es:si + MH_N]
    jae .no
    call w16_mitem
    call w16_mprep
    cmp al, [w16_mmch]
    je .hit
    inc cx
    jmp short .l
.no:
    mov al, 0xFF
    jmp short .out
.hit:
    mov al, cl
.out:
    pop di
    pop cx
    ret

; w16_mupper - AL in upper case
w16_mupper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; --- the pictures (Windows 3.0's, read off its screen) ------------------------
w16_mchk:   db 0x01, 0x01, 0x03, 0x02, 0x06, 0xC4, 0x68, 0x28, 0x38, 0x10
w16_marr:   db 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xF0, 0xE0, 0xC0, 0x80

; =============================================================================
; The menu loop (SPEC.md 124.11): DefWindowProc waits, the callbacks move it
; =============================================================================

; w16_msendpop - AL = the level, AH = 1 to select its first item: SEND
; WM_INITMENUPOPUP for [w16_mnmenu] ([w16_mnpos] its place), and open it
; when the program has answered (w16_mt_popup). Only queues the call: the
; gate or w16_mhpost enters it. Keeps every register.
w16_msendpop:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov si, ax
    mov ax, [w16_mhwnd]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_INITMENUPOPUP
    mov ax, [w16_mnmenu]
    mov [w16_sm_wp], ax
    mov ax, [w16_mnpos]
    mov [w16_sm_lp], ax
    mov word [w16_sm_lp + 2], 0
    mov ax, [w16_mhwnd]
    call w16_hwnd2rec
    jc .out
    mov ax, si
    mov si, w16_mt_popup
    call w16_sendwnd
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; w16_mt_popup - WM_INITMENUPOPUP answered: CX = the level and AH's flag
w16_mt_popup:
    cmp byte [w16_mtrk], 0
    je w16_mt_tail
    mov ax, cx
    call w16_mpopopen
    ; fall into w16_mt_tail

; w16_mt_tail - how every continuation of the loop ends: a call it queued
; is entered, a loop still running keeps DefWindowProc waiting, and one
; that has ended lets it answer 0
w16_mt_tail:
    cmp byte [w16_cbgo], 0
    jne .ret
    cmp byte [w16_mtrk], 0
    je .done
    mov byte [w16_pend], 1
.ret:
    ret
.done:
    xor ax, ax
    xor dx, dx
    ret

; w16_mtbegin - DefWindowProc enters the loop: BX = the window, AX = its
; HWND, DL = 0 the mouse (pressed at [w16_mptx], [w16_mpty] on the screen) /
; 1 the keyboard, DH = the character (0: F10). Answers as a handler.
w16_mtbegin:
    cmp byte [w16_mtrk], 0
    jne .zero
    mov cx, [bx + W16W_MENU]
    or cx, cx
    jz .zero
    push ax
    mov ax, cx
    call w16_mget
    pop ax
    jc .zero
    mov [w16_mhbar], cx
    mov [w16_mhwnd], ax
    mov [w16_mkbd], dl
    mov [w16_mkch], dh
    mov byte [w16_mtrk], 1
    mov byte [w16_mbsel], 0xFF
    mov byte [w16_mlev], 0
    mov byte [w16_mstale], 0
    mov ax, [w16_idx]               ; the frame the loop waits in
    mov [w16_midx], ax
    mov ax, [w16_nargb]
    mov [w16_mnargb], ax
    mov ax, [w16_psp_]
    mov [w16_mpsp], ax
    mov ax, [w16_mhwnd]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_INITMENU
    mov [w16_sm_wp], cx
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    push si
    mov si, w16_mt_inited
    call w16_sendwnd
    pop si
    ret
.zero:
    xor ax, ax
    xor dx, dx
    ret

; w16_mt_inited - WM_INITMENU answered: select what the press or the key
; named
w16_mt_inited:
    cmp byte [w16_mtrk], 0
    je w16_mt_tail
    cmp byte [w16_mkbd], 0
    jne .kbd
    mov cx, [w16_mptx]
    mov dx, [w16_mpty]
    call w16_mrel
    call w16_mhit
    cmp al, 1
    jne .stop
    cmp bl, 0xFF
    je .stop
    mov al, bl
    mov ah, 1
    call w16_mbarsel
    jmp w16_mt_tail
.kbd:
    mov al, [w16_mkch]
    or al, al
    jnz .mn
    xor ax, ax                      ; F10: the first title, nothing open
    call w16_mbarsel
    jmp w16_mt_tail
.mn:
    call w16_mupper
    push ax
    mov ax, [w16_mhbar]
    call w16_mget
    pop ax
    jc .stop
    call w16_mmnfind
    cmp al, 0xFF
    je .stop
    call w16_mbarkey
    jmp w16_mt_tail
.stop:
    call w16_mstop
    jmp w16_mt_tail

; w16_mstop - the loop is over: every popup down, the bar unselected
w16_mstop:
    push ax
    xor al, al
    call w16_mcloseto
    mov byte [w16_mbsel], 0xFF
    mov byte [w16_mtrk], 0
    call w16_mbardraw
    pop ax
    ret

; w16_mpick - ES:DI = an item: one that can be chosen ends the loop and
; POSTS its WM_COMMAND. CF=1 it cannot. Keeps every register.
w16_mpick:
    test word [es:di + MI_FLAGS], MF_NOPICK
    jnz .no
    push ax
    mov ax, [es:di + MI_ID]
    call w16_mstop
    mov [w16_sm_wp], ax
    mov ax, [w16_mhwnd]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_COMMAND
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    call w16_post
    pop ax
    clc
    ret
.no:
    stc
    ret

; w16_mbarsel - AL = a title (0FFh none), AH = 1 open its popup, + 2 with
; its first item selected. Every popup goes first. Keeps every register.
w16_mbarsel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ax
    xor al, al
    call w16_mcloseto
    pop ax
    mov [w16_mbsel], al
    call w16_mbardraw
    test ah, 1
    jz .out
    cmp al, 0xFF
    je .out
    mov cl, al
    xor ch, ch
    push ax
    mov ax, [w16_mhbar]
    call w16_mget
    pop ax
    jc .out
    cmp cl, [es:si + MH_N]
    jae .out
    call w16_mitem
    test word [es:di + MI_FLAGS], MF_POPUP
    jz .out
    test word [es:di + MI_FLAGS], MF_GRAYED | MF_DISABLED
    jnz .out
    mov dx, [es:di + MI_ID]
    mov [w16_mnmenu], dx
    mov [w16_mnpos], cx
    mov dx, [es:di + MI_X]
    mov [w16_mnx], dx
    mov dx, [es:di + MI_Y]
    add dx, [es:di + MI_H]
    mov [w16_mny], dx
    shr ah, 1
    xor al, al
    call w16_msendpop
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mbarkey - AL = a title chosen from the keyboard: its popup opens with
; its first item selected, or a command title is picked
w16_mbarkey:
    push ax
    push cx
    push si
    push di
    push es
    mov cl, al
    xor ch, ch
    mov ah, 3
    call w16_mbarsel
    mov ax, [w16_mhbar]
    call w16_mget
    jc .out
    call w16_mitem
    call w16_mpick
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16_mpopsel - AL = a level, AH = its item (0FFh none), DL = 1 open it if
; it is a popup, + 2 with that one's first item selected. Keeps every
; register.
w16_mpopsel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call w16_mplev
    mov cl, [w16_mlev]
    dec cl
    cmp ah, [bx + MP_SEL]
    jne .change
    cmp cl, al                      ; the same item, its popup already open
    ja .out
    jmp short .sub
.change:
    push ax
    inc al
    call w16_mcloseto
    pop ax
    mov cl, [bx + MP_SEL]
    mov [bx + MP_SEL], ah
    push ax
    mov ax, [bx + MP_MENU]
    call w16_mget
    pop ax
    jc .out
    call w16_mgon
    jc .sub
    push cx
    cmp cl, 0xFF
    je .n1
    xor ch, ch
    call w16_mpitem
.n1:
    mov cl, ah
    cmp cl, 0xFF
    je .n2
    xor ch, ch
    call w16_mpitem
.n2:
    pop cx
    call w16_mgoff
.sub:
    test dl, 1
    jz .out
    cmp ah, 0xFF
    je .out
    push ax
    mov ax, [bx + MP_MENU]
    call w16_mget
    pop ax
    jc .out
    mov cl, ah
    xor ch, ch
    call w16_mitem
    test word [es:di + MI_FLAGS], MF_POPUP
    jz .out
    test word [es:di + MI_FLAGS], MF_GRAYED | MF_DISABLED
    jnz .out
    push ax
    mov ax, [es:di + MI_ID]
    mov [w16_mnmenu], ax
    mov [w16_mnpos], cx
    mov ax, [bx + MP_X]
    add ax, [bx + MP_W]
    sub ax, 8                       ; the parent's right border - 7
    mov [w16_mnx], ax
    mov ax, [bx + MP_Y]
    add ax, [es:di + MI_Y]          ; its top border at the item's top - 1
    mov [w16_mny], ax
    pop ax
    inc al
    mov ah, dl
    shr ah, 1
    call w16_msendpop
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mpopkey - AL = a level, AH = an item chosen from the keyboard: a popup
; opens with its first item selected, a command is picked
w16_mpopkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dl, 3
    call w16_mpopsel
    call w16_mplev
    push ax
    mov ax, [bx + MP_MENU]
    call w16_mget
    pop ax
    jc .out
    mov cl, ah
    xor ch, ch
    call w16_mitem
    call w16_mpick
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_mrel - CX, DX: a screen point -> the content's. Keeps the rest.
w16_mrel:
    push ax
    push bx
    push dx
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT
    mov bx, dx
    pop dx
    sub cx, ax
    sub dx, bx
    pop bx
    pop ax
    ret

; w16_mhit - CX, DX = a content point -> AL = 0 nothing / 1 the bar / 2 a
; popup, AH = its level, BL = the item there (0FFh none). Keeps CX, DX.
w16_mhit:
    push si
    push di
    push es
    mov al, [w16_mlev]
    mov [w16_mhl], al
.lv:
    cmp byte [w16_mhl], 0
    je .bar
    dec byte [w16_mhl]
    mov al, [w16_mhl]
    call w16_mplev
    mov ax, cx
    sub ax, [bx + MP_X]
    jl .lv
    cmp ax, [bx + MP_W]
    jge .lv
    dec ax
    mov [w16_mhx], ax               ; the popup's client point
    mov ax, dx
    sub ax, [bx + MP_Y]
    jl .lv
    cmp ax, [bx + MP_H]
    jge .lv
    dec ax
    mov [w16_mhy], ax
    mov ax, [bx + MP_MENU]
    call w16_mget
    mov bl, 0xFF
    jc .pop
    push cx
    xor cx, cx
.pi:
    cmp cl, [es:si + MH_N]
    jae .pin
    call w16_mitem
    mov ax, [w16_mhx]
    or ax, ax
    jl .pin
    cmp ax, [es:si + MH_W]
    jge .pin
    mov ax, [w16_mhy]
    sub ax, [es:di + MI_Y]
    jl .pnx
    cmp ax, [es:di + MI_H]
    jl .phit
.pnx:
    inc cx
    jmp short .pi
.phit:
    mov bl, cl
.pin:
    pop cx
.pop:
    mov al, 2
    mov ah, [w16_mhl]
    jmp short .out
.bar:
    mov ax, [w16_mhbar]
    call w16_mget
    jc .none
    or dx, dx
    jl .none
    mov ax, [es:si + MH_H]
    dec ax
    cmp dx, ax
    jge .none
    mov bl, 0xFF
    mov [w16_mhx], cx
    push cx
    xor cx, cx
.bi:
    cmp cl, [es:si + MH_N]
    jae .bin
    call w16_mitem
    mov ax, [w16_mhx]
    sub ax, [es:di + MI_X]
    jl .bnx
    cmp ax, [es:di + MI_W]
    jge .bnx
    mov ax, dx
    sub ax, [es:di + MI_Y]
    jl .bnx
    cmp ax, [es:di + MI_H]
    jl .bhit
.bnx:
    inc cx
    jmp short .bi
.bhit:
    mov bl, cl
.bin:
    pop cx
    mov al, 1
    jmp short .out
.none:
    xor al, al
    mov bl, 0xFF
.out:
    pop es
    pop di
    pop si
    ret

; w16_mhpre - is the program waiting in the loop's DefWindowProc? CF=1 not
; (the event is not the loop's). CF=0: the frame is current again, and the
; caller marks the callback's gfx lock as held ([w16_locked]) so drawing
; does not take it a second time; w16_mhpost clears the mark.
w16_mhpre:
    cmp byte [w16_mtrk], 0
    je .no
    cmp byte [w16_state], W16S_RUN
    jne .no
    push ax
    mov ax, [w16_psp_]
    cmp ax, [w16_mpsp]
    jne .no1
    mov ax, [w16_midx]
    mov [w16_idx], ax
    mov ax, [w16_mnargb]
    mov [w16_nargb], ax
    pop ax
    clc
    ret
.no1:
    pop ax
.no:
    stc
    ret

; w16_mhpost - after a callback moved the loop: enter the call it queued
; (w16_resumecb), or let DefWindowProc answer 0 if the loop has ended, and
; close up after a program that ended in either. The gfx lock is held.
w16_mhpost:
    mov byte [w16_locked], 1
    cmp byte [w16_cbgo], 0
    je .nosend
    call w16_resumecb
    jmp short .after
.nosend:
    cmp byte [w16_mtrk], 0
    jne .still
    xor ax, ax
    xor dx, dx
    call w16_resume
.after:
    call w16_after                  ; (the lock still marked held)
    mov byte [w16_locked], 0
    ret
.still:
    mov byte [w16_locked], 0
    ret

; --- the callbacks, while the loop runs: SI = our window, the lock held ------

; w16_mclick - a press: on a title it opens (or, open already, ends the
; loop), in a popup it selects, anywhere else it ends the loop
w16_mclick:
    push si
    push di
    call w16_mhpre
    jc .out
    mov byte [w16_locked], 1        ; the callback's lock
    mov byte [w16_meat], 1
    call w16_mrel
    call w16_mhit
    cmp al, 1
    je .bar
    cmp al, 2
    je .pop
    call w16_mstop
    jmp short .post
.bar:
    cmp bl, 0xFF
    je .post
    cmp bl, [w16_mbsel]
    jne .nb
    cmp byte [w16_mlev], 0
    je .nb
    call w16_mstop
    jmp short .post
.nb:
    mov al, bl
    mov ah, 1
    call w16_mbarsel
    jmp short .post
.pop:
    mov al, ah
    mov ah, bl
    mov dl, 1
    call w16_mpopsel
.post:
    call w16_mhpost
.out:
    pop di
    pop si
    ret

; w16_mdrag - the pointer moved with the button down: the selection follows
w16_mdrag:
    push si
    push di
    call w16_mhpre
    jc .out
    mov byte [w16_locked], 1        ; the callback's lock
    call w16_mrel
    call w16_mhit
    cmp al, 1
    je .bar
    cmp al, 2
    je .pop
    mov al, [w16_mlev]              ; outside: the deepest popup unselected
    or al, al
    jz .post
    dec al
    mov ah, 0xFF
    xor dl, dl
    call w16_mpopsel
    jmp short .post
.bar:
    cmp bl, 0xFF
    je .post
    cmp bl, [w16_mbsel]
    je .same
    mov al, bl
    mov ah, 1
    call w16_mbarsel
    jmp short .post
.same:
    cmp byte [w16_mlev], 0
    je .post
    xor al, al
    mov ah, 0xFF
    xor dl, dl
    call w16_mpopsel
    jmp short .post
.pop:
    mov al, ah
    mov ah, bl
    mov dl, 1
    call w16_mpopsel
.post:
    call w16_mhpost
.out:
    pop di
    pop si
    ret

; w16_mup - the release: on a command it picks, on a title or a popup's
; title the loop stays, outside it ends
w16_mup:
    push si
    push di
    mov byte [w16_meat], 0
    call w16_mhpre
    jc .out
    mov byte [w16_locked], 1        ; the callback's lock
    call w16_mrel
    call w16_mhit
    cmp al, 1
    je .bar
    cmp al, 2
    je .pop
    call w16_mstop
    jmp short .post
.bar:
    cmp bl, 0xFF
    je .post
    mov ax, [w16_mhbar]
    jmp short .item
.pop:
    cmp bl, 0xFF
    je .post
    mov al, ah
    push bx
    call w16_mplev
    mov ax, [bx + MP_MENU]
    pop bx
.item:
    call w16_mget
    jc .post
    mov cl, bl
    xor ch, ch
    call w16_mitem
    call w16_mpick
.post:
    call w16_mhpost
.out:
    pop di
    pop si
    ret

; w16_mkey - a key while the loop runs: AL = ASCII, AH = the scan code.
; Esc backs out a level, the arrows move, Enter chooses, a letter is a
; mnemonic, F10 ends the loop.
w16_mkey:
    push si
    push di
    call w16_mhpre
    jc .out
    mov byte [w16_locked], 1        ; the callback's lock
    mov [w16_mkch], al
    cmp ah, 0x01
    je .esc
    cmp ah, 0x44
    je .stop
    cmp ah, 0x4B
    je .left
    cmp ah, 0x4D
    je .right
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp ah, 0x1C
    je .enter
    or al, al
    jz .post
    call w16_mupper
    call w16_mmnkey
    jmp .post
.esc:
    mov al, [w16_mlev]
    or al, al
    jz .stop
    dec al
    call w16_mcloseto
    jmp .post
.stop:
    call w16_mstop
    jmp .post
.left:
    cmp byte [w16_mlev], 2
    jb .lb
    mov al, [w16_mlev]
    dec al
    call w16_mcloseto
    jmp .post
.lb:
    mov ah, 0xFF
    jmp short .barmove
.right:
    mov al, [w16_mlev]
    or al, al
    jz .rb
    dec al
    call w16_mplev
    mov ah, [bx + MP_SEL]
    cmp ah, 0xFF
    je .rb
    push ax
    mov ax, [bx + MP_MENU]
    call w16_mget
    pop ax
    jc .rb
    mov cl, ah
    xor ch, ch
    call w16_mitem
    test word [es:di + MI_FLAGS], MF_POPUP
    jz .rb
    mov dl, 3
    call w16_mpopsel
    jmp .post
.rb:
    mov ah, 1
.barmove:
    mov al, [w16_mbsel]
    push ax
    mov ax, [w16_mhbar]
    call w16_mget
    pop ax
    jc .post
    call w16_mnext
    xor ah, ah
    cmp byte [w16_mlev], 0
    je .bm
    mov ah, 3
.bm:
    call w16_mbarsel
    jmp short .post
.up:
    mov ah, 0xFF
    jmp short .vert
.down:
    mov ah, 1
.vert:
    cmp byte [w16_mlev], 0
    jne .vp
    mov al, [w16_mbsel]
    cmp al, 0xFF
    je .post
    mov ah, 3
    call w16_mbarsel
    jmp short .post
.vp:
    mov dl, ah
    mov al, [w16_mlev]
    dec al
    call w16_mplev
    push ax
    mov ax, [bx + MP_MENU]
    call w16_mget
    pop ax
    jc .post
    mov dh, al                      ; DH = the level
    mov al, [bx + MP_SEL]
    mov ah, dl
    call w16_mnext
    mov ah, al
    mov al, dh
    xor dl, dl
    call w16_mpopsel
    jmp short .post
.enter:
    mov al, [w16_mlev]
    or al, al
    jnz .ep
    mov al, [w16_mbsel]
    cmp al, 0xFF
    je .post
    call w16_mbarkey
    jmp short .post
.ep:
    dec al
    call w16_mplev
    mov ah, [bx + MP_SEL]
    cmp ah, 0xFF
    je .post
    call w16_mpopkey
.post:
    call w16_mhpost
.out:
    pop di
    pop si
    ret

; w16_mmnkey - AL = a letter, upper case: the deepest popup's item, or the
; bar's title, whose mnemonic it is
w16_mmnkey:
    mov ah, al
    mov al, [w16_mlev]
    or al, al
    jz .bar
    dec al
    push ax
    call w16_mplev
    mov ax, [bx + MP_MENU]
    call w16_mget
    pop ax
    jc .out
    mov dl, al
    mov al, ah
    call w16_mmnfind
    cmp al, 0xFF
    je .out
    mov ah, al
    mov al, dl
    jmp w16_mpopkey
.bar:
    push ax
    mov ax, [w16_mhbar]
    call w16_mget
    pop ax
    jc .out
    mov al, ah
    call w16_mmnfind
    cmp al, 0xFF
    je .out
    jmp w16_mbarkey
.out:
    ret

; w16_mrepaint - W_PAINT came while the loop runs (w16_ppaint): the banks are
; stale now, and the bar and every open popup are drawn again over the
; damage. Keeps every register.
w16_mrepaint:
    push ax
    push bx
    mov byte [w16_mstale], 1
    call w16_mbardraw
    xor al, al
.l:
    cmp al, [w16_mlev]
    jae .out
    call w16_mplev
    call w16_mpopdraw
    inc al
    jmp short .l
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Accelerators (USER.177, 178)
; =============================================================================

; LoadAccelerators (USER.177) - args: +0 lpTableName, +4 hInstance. The
; table stays loaded: its handle IS the HACCEL.
w16u_loadaccel:
    push di
    call w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    mov di, w16_rnamb
    call w16_rkey
    mov [w16_rnk], ax
    mov word [w16_rtk], 0x8000 | RT_ACCEL
    call w16_rfind
    or ax, ax
    jz .out
    call w16_rload
.out:
    pop di
    ret

; TranslateAccelerator (USER.178) - args: +0 lpMsg, +4 hAccel, +6 hWnd.
; A key-down is matched against the virtual-key entries with the shift
; state it was pressed with (w16_kmods), a character against the ASCII
; ones. A match sends WM_INITMENU, WM_INITMENUPOPUP if the command is in a
; popup, and - unless the item is grey or disabled - WM_COMMAND with 1 in
; lParam's high word: Wine's translate_accelerator, and Windows 3.0's
; counts (MENU.EXE: one of each a press). Answers 1 for a match.
w16u_transaccel:
    push si
    push di
    call w16_args
    mov ax, [es:bx + 6]
    mov [w16_ahwnd], ax
    mov ax, [es:bx + 4]
    mov [w16_aacc], ax
    les di, [es:bx]
    mov ax, [es:di + MSG_MSG]
    mov dx, [es:di + MSG_WP]
    mov cl, [es:di + MSG_LP + 2]    ; the scan code
    mov bl, 1                       ; BL = 1 a key, 0 a character
    cmp ax, WM_KEYDOWN
    je .kind
    cmp ax, WM_SYSKEYDOWN
    je .kind
    xor bl, bl
    cmp ax, WM_CHAR
    je .kind
    cmp ax, WM_SYSCHAR
    jne .zero
.kind:
    xor bh, bh                      ; BH = FSHIFT/FCONTROL/FALT as pressed
    cmp cl, W16_NSCAN
    jae .alt
    push bx
    mov bl, cl
    xor bh, bh
    mov al, [w16_kmods + bx]
    pop bx
    test al, 3
    jz .ns
    or bh, FSHIFT
.ns:
    test al, 4
    jz .nc
    or bh, FCONTROL
.nc:
    test al, 8
    jz .alt
    or bh, FALT
.alt:
    mov ax, [es:di + MSG_MSG]
    cmp ax, WM_SYSCHAR
    jne .tab
    or bh, FALT
.tab:
    mov ax, [w16_aacc]
    push bx
    call w16_gfind
    pop bx
    jc .zero
    cmp byte [w16_gdisc], 0
    jne .zero
    mov ax, es
    inc ax
    mov es, ax
    xor si, si
.e:
    mov al, [es:si]
    mov cx, [es:si + 1]
    cmp cx, dx
    jne .next
    test al, FVIRTKEY
    jz .ascii
    or bl, bl
    jz .next
    mov ah, al
    and ah, FSHIFT | FCONTROL | FALT
    cmp ah, bh
    je .hit
    jmp short .next
.ascii:
    or bl, bl
    jnz .next
    mov ah, al
    xor ah, bh
    test ah, FALT
    jz .hit
.next:
    test al, 0x80
    jnz .zero
    add si, 5
    jmp short .e
.zero:
    xor ax, ax
    jmp .out
.hit:
    mov ax, [es:si + 3]
    mov [w16_acmd], ax
    mov word [w16_asub], 0
    mov ax, [w16_ahwnd]
    call w16_hwnd2rec               ; BX = the window
    jc .zero
    mov [w16_arec], bx
    mov ax, [bx + W16W_MENU]
    mov [w16_ahm], ax
    or ax, ax
    jz .cmd
    call w16_mget
    jc .cmd
    mov ax, [w16_acmd]
    call w16_mfindcmd               ; SI = the record holding it
    jc .cmd
    lea ax, [si + W16_HMB]
    cmp ax, [w16_ahm]
    je .init
    mov [w16_asub], ax
    push ax                         ; ...and the popup's place in its parent
    mov ax, [w16_ahm]
    call w16_mget
    pop ax
    call w16_mfindcmd
    mov [w16_apos], cx
.init:
    mov ax, [w16_ahwnd]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_INITMENU
    mov ax, [w16_ahm]
    mov [w16_sm_wp], ax
    xor ax, ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    mov bx, [w16_arec]
    mov si, w16_ta_inited
    call w16_sendwnd
    jmp short .out
.cmd:
    call w16_ta_cmd
.out:
    pop di
    pop si
    ret

; w16_ta_inited - WM_INITMENU answered: WM_INITMENUPOPUP for its popup
w16_ta_inited:
    mov ax, [w16_asub]
    or ax, ax
    jz w16_ta_popped
    mov [w16_sm_wp], ax
    mov ax, [w16_ahwnd]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_INITMENUPOPUP
    mov ax, [w16_apos]
    mov [w16_sm_lp], ax
    mov word [w16_sm_lp + 2], 0
    mov bx, [w16_arec]
    mov si, w16_ta_popped
    jmp w16_sendwnd

; w16_ta_popped - both answered: the item's state now decides
w16_ta_popped:
    mov ax, [w16_ahm]
    mov cx, [w16_acmd]
    xor dx, dx
    call w16_mloc
    jc w16_ta_cmd
    test word [es:di + MI_FLAGS], MF_GRAYED | MF_DISABLED
    jz w16_ta_cmd
    mov ax, 1
    xor dx, dx
    ret

; w16_ta_cmd - send WM_COMMAND, from an accelerator
w16_ta_cmd:
    mov ax, [w16_ahwnd]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_COMMAND
    mov ax, [w16_acmd]
    mov [w16_sm_wp], ax
    mov word [w16_sm_lp], 0
    mov word [w16_sm_lp + 2], 1
    mov ax, [w16_ahwnd]
    call w16_hwnd2rec
    jc .one
    mov si, w16_cont_one
    jmp w16_sendwnd
.one:
    mov ax, 1
    xor dx, dx
    ret
