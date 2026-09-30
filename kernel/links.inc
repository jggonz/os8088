%ifdef KERN_BIG
; =============================================================================
; Desktop shortcuts (SPEC.md 26.8). Paint and open work without the system
; disk. Behavior lives in DESKTOP.DRV; resident code only dispatches.
; DS is the kernel; the lazily allocated record store is reached through ES.
; =============================================================================
DL_MAX      equ 16
DL_REC      equ 256
DL_HDR      equ 512
DL_CAP      equ DL_HDR+DL_MAX*DL_REC
DL_KB       equ (DL_CAP+512)/1024
DL_MOD_KB   equ 4
DL_X        equ 0
DL_Y        equ 2
DL_DRV      equ 4
DL_TYPE     equ 5
DL_SIG      equ 6
DL_PATH     equ 8
DL_PATHMAX  equ 128
DL_ICON     equ 136
DL_W        equ 112
DL_H        equ 44
DL_HCGA     equ 28
DL_NENT     equ 11
%ifdef KERN_EMU
DL_GLUE_SEG equ COLD_SEG
%else
DL_GLUE_SEG equ KERNEL_SEG
%endif

section .text
; Pending state is initialized, not dependent on uninitialized bss.
dl_seg:     dw 0
dl_count:   db 0
dl_sel:     db 0xFF
dl_dirty:   db 0
dl_save:    db 0
dl_open:    db 0
dl_cfgname: db 'DESKTOP.CFG',0
dl_sysname equ drv_sysname
dl_sysmissing: db 'Insert the system disk',0

section .text
%ifdef KERN_EMU
section .cold                   ; emu reuses its existing cold-rung slack
%endif
dlf_dl_damage_x:
    cmp byte [dl_count], 0
    je .done
    call far [DLFP+3*4]
.done:
    retf
dlf_dl_paint_damage_x:
    cmp byte [dl_count], 0
    je .done
    call far [DLFP+4*4]
.done:
    retf

section .text
%ifdef KERN_EMU
section .cold                   ; emu reuses its existing cold-rung slack
%endif
dlf_dl_drain_x:
    cmp word [dl_save], 0        ; save/open are adjacent bytes
    jz .done
    call far [DLFP+8*4]
.done:
    retf


; UI-only consumers already share dsk_ent/dsk_ico for names and pictures.
dl_name     equ dsk_ent
; Opening yields during I/O: use the UI loader's name, never paint staging.
dl_openname equ ld_lname
section .cold
; The UI prepares the image after the first frame while the boot disk is present.
; This lets first links from a swapped-in apps floppy work without a swap.
; An empty desktop skips every paint/click dispatch and claims no records.
section .text
%ifdef KERN_EMU
section .cold                   ; emu reuses its existing cold-rung slack
%endif
dlf_dl_paint_x:
    cmp byte [dl_count], 0
    je .done
    call far [DLFP+2*4]
.done:
    retf
dlf_dl_click_x:
    cmp byte [dl_count], 0
    je .miss
    call far [DLFP+5*4]
    retf
.miss:
    clc
    retf
section .text
%ifdef KERN_EMU
section .cold                   ; emu reuses its existing cold-rung slack
%endif
dlf_dl_drop_x:
    push ax
    mov al, MOD_DESK
    call COLD_SEG:dlf_mod_need
    pop ax
    jc .missing
    call far [DLFP+6*4]
    retf
.missing:
    call DL_GLUE_SEG:dlf_dl_missing_x
    retf
section .text
%ifdef KERN_EMU
section .cold                   ; emu reuses its existing cold-rung slack
%endif
dlf_dl_command_x:
    jmp far [DLFP+7*4]           ; unloaded slots already point at mod_gone

section .text
%ifdef KERN_EMU
section .cold                   ; emu reuses its existing cold-rung slack
%endif
dlf_dl_missing_x:
    push ds
    pop es                      ; only the drop path calls this; all registers dead
    mov si, dl_sysmissing
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    retf

section .cold
%macro DLSHIM 1
dlf_%1:
    call %1
    retf
%endmacro
DLSHIM mod_need
DLSHIM desk_zone_redraw
DLSHIM fm_choose
DLSHIM fm_stage_name
DLSHIM fmv_get_icon
DLSHIM ld_pkg_start_x
DLSHIM ld_say_status
%ifdef OS88_ASSOC
DLSHIM assoc_post_x
%endif

; The title shares the menu string; only the window template is resident.
section .text
dl_info_title equ menu_s_linkinfo
dl_info_tpl: dw 120, 60, 400, 108, dl_info_title, dlf_dl_info_paint, 0, 0
section .cold
dlf_dl_info_paint:
    call far [DLFP+9*4]
    ret

section .modt
dlt_header:
    dw 0x384F
    db MOD_VER, MOD_DESK
    dw BUILD_NUM, MOD_STAMP
    dw MODT_SIZE, DL_NENT
    dw dlt_load, dlt_save
    dw dlt_entry_dlt_paint, dlt_entry_dlt_damage, dlt_entry_dlt_paint_damage
    dw dlt_entry_dlt_click, dlt_entry_dlt_drop, dlt_entry_dlt_command, dlt_entry_dlt_drain
    dw dlt_entry_dl_info_paint, dlt_entry_dl_key

dl_stage: times DL_ICON db 0

dl_msgfull: db 'Desktop is full',0
dl_msgpath: db 'Shortcut path too long',0
dl_msgdisk: db 'Target disk unavailable',0
dl_msggone: db 'Shortcut target missing',0
dl_msgswap: db 'Insert the shortcut target disk',0
dl_msgmem:  db 'No memory for shortcuts',0
dl_msgsave: db 'Desktop save failed',0
dl_msgsys:  db 'Insert the system disk',0
dl_msgcfg:  db 'Invalid DESKTOP.CFG',0
; Built-in folder and document pictures in the existing masked-record format.
dl_folder:
    db 1,16
    times 16 dw 0xFFFF
    dw 0,0x7C00,0x8200,0x81FC,0x8002,0xBFF2,0xA002,0xA002
    dw 0xA002,0xA002,0xA002,0xA002,0xA002,0xBFFE,0,0
dl_document:
    db 1,16
    times 16 dw 0xFFFF
    dw 0,0x7FE0,0x4030,0x4028,0x403C,0x4004,0x5FC4,0x4004
    dw 0x5FC4,0x4004,0x5FC4,0x4004,0x4004,0x7FFC,0,0
dl_badge:
    db 1,8
    times 8 dw 0xFF00
    dw 0xFF00,0x8100,0xBD00,0x8D00,0x9500,0xA100,0x8100,0xFF00

dl_index:   db 0
dl_clock:   dw 0
dl_height:  dw 0
dl_pic:     dw 0
dl_right:   dw 0
dl_bottom:  dw 0
dl_px:      dw 0
dl_py:      dw 0
dl_oldx:    dw 0
dl_oldy:    dw 0
dl_pressx:  dw 0
dl_pressy:  dw 0
dl_changed: db 0
dl_existing: db 0

; Claim 5KB only for an actual configuration or first shortcut. AX/CF outputs.
dl_claim:
    cmp word [dl_seg], 0
    jne .ok
    push bx
    push dx
    push di
    push cx
    push es
    mov ax, DL_KB
    mov bx, MEM_K_DESK
    call COLD_SEG:mmf_mem_claim
    jc .out
    mov [dl_seg], dx
    mov es, dx
    xor di, di
    xor ax, ax
    mov cx, DL_KB*512
    cld
    rep stosw
.okpop:
    clc
.out:
    pop es
    pop cx
    pop di
    pop dx
    pop bx
    ret
.ok:
    clc
    ret

dl_release:
    push ax
    push bx
    push dx
    mov dx, [dl_seg]
    mov bx, MEM_K_DESK
    call COLD_SEG:mmf_mem_free
    mov word [dl_seg], 0
    pop dx
    pop bx
    pop ax
    ret

; AL = record index -> stage. All registers preserved, no I/O.
dl_read:
    push ax
    push cx
    push si
    push di
    push es
    xor ah, ah
    mov si, ax
    mov cl, 8
    shl si, cl
    add si, DL_HDR
    mov es, [dl_seg]
    mov di, dl_stage
    mov cx, DL_ICON/2
.copy:
    mov ax, [es:si]
    mov [cs:di], ax
    add si, 2
    add di, 2
    loop .copy
    mov di, dsk_ico
    mov cx, 32
.icon:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .icon
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; AL = record index <- stage. All registers preserved.
dl_write:
    push ax
    push cx
    push si
    push di
    push es
    xor ah, ah
    mov di, ax
    mov cl, 8
    shl di, cl
    add di, DL_HDR
    mov es, [dl_seg]
    mov si, dl_stage
    mov cx, DL_ICON/2
    cld
    push ds
    push cs
    pop ds
    rep movsw
    pop ds
    mov si, dsk_ico
    mov cx, 32
    rep movsw
    xor ax, ax
    mov cx, (DL_REC-DL_ICON-64)/2
    rep stosw
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; Runtime limits reserve every possible drive column, including the service.
; Clobbers AX/BX/CX/DX; stage's position is clamped, never screen constants.
dl_rect:
    mov ax, DL_H
    mov bx, 32
    cmp byte [vid_kind], VID_CGA
    jne .height
    mov ax, DL_HCGA
    mov bx, 16
.height:
    mov [cs:dl_height], ax
    mov [cs:dl_pic], bx
    mov bx, [vid_dock_y0]
    sub bx, ax
    mov [cs:dl_bottom], bx
    mov ax, DESK_NZ-1
    xor dx, dx
    div word [desk_rows]
    mov bx, DESK_COLW
    mul bx
    mov bx, [vid_desk_zx]
    sub bx, ax
    sub bx, DL_W+8
    mov [cs:dl_right], bx
    mov ax, [cs:dl_stage+DL_X]
    or ax, ax
    jns .xpos
    xor ax, ax
.xpos:
    cmp ax, bx
    jle .xok
    mov ax, bx
.xok:
    mov bx, [cs:dl_stage+DL_Y]
    cmp bx, MBAR_H+4
    jge .ypos
    mov bx, MBAR_H+4
.ypos:
    cmp bx, [cs:dl_bottom]
    jle .yok
    mov bx, [cs:dl_bottom]
.yok:
    mov [cs:dl_stage+DL_X], ax
    mov [cs:dl_stage+DL_Y], bx
    mov cx, ax
    add cx, DL_W-1
    mov dx, bx
    add dx, [cs:dl_height]
    dec dx
    ret

; This is only caption derivation: the full persisted path stays untouched.
; Out SI = NUL-terminated basename without extension. Clobbers AX/DI.
dl_names:
    mov si, dl_stage+DL_PATH
    mov di, si
.scan:
    mov al, [cs:si]
    inc si
    cmp al, '\'
    jne .next
    mov di, si
.next:
    or al, al
    jnz .scan
    mov si, di
    mov di, dl_name
.copy:
    mov al, [cs:si]
    inc si
    cmp al, '.'
    je .end
    or al, al
    jz .end
    mov [di], al
    inc di
    jmp short .copy
.end:
    mov byte [di], 0
    mov si, dl_name
    ret

; Gfx lock held. No erase: wm composition has already drawn the desktop.
dl_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call dl_rect
    mov [cs:dl_px], ax
    mov [cs:dl_py], bx
    add ax, (DL_W-32)/2
    cmp word [cs:dl_pic], 16
    jne .tile
    add ax, 8
.tile:
    mov cx, ax
    add cx, [cs:dl_pic]
    dec cx
    mov dx, bx
    add dx, [cs:dl_pic]
    dec dx
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill
    mov byte [gfx_color], CBLACK
    call KERNEL_SEG:cw_gfx_frame
    mov cx, [cs:dl_px]
    add cx, (DL_W-16)/2
    mov dx, [cs:dl_py]
    cmp word [cs:dl_pic], 16
    je .body
    add dx, 8
.body:
    mov si, dsk_ico
    call dl_icon_blank
    jz .generic
    call KERNEL_SEG:cw_icon_draw16
    jmp short .badge
.generic:
    mov si, dl_document
    cmp byte [cs:dl_stage+DL_TYPE], 2
    je .folder
    cmp byte [cs:dl_stage+DL_TYPE], 1
    jne .glyph
    mov si, ico_app16
    call KERNEL_SEG:cw_icon_draw
    jmp short .badge
.folder:
    mov si, dl_folder
.glyph:
    push cs
    pop es
    call KERNEL_SEG:dlf_icon_draw_x
.badge:
    mov cx, [cs:dl_px]
    add cx, (DL_W-32)/2-2
    mov dx, [cs:dl_py]
    add dx, [cs:dl_pic]
    sub dx, 7
    mov si, dl_badge
    push cs
    pop es
    call KERNEL_SEG:dlf_icon_draw_x
    call dl_names
    mov dx, [cs:dl_py]
    add dx, [cs:dl_pic]
    add dx, 2
    call dl_label
    mov al, [cs:dl_index]
    cmp al, [dl_sel]
    jne .done
    call dl_rect
    call KERNEL_SEG:cw_gfx_xor_fill
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; SI = caption, DX = y. Opaque text, centered; labels fit DL_W.
dl_label:
    push ax
    push bx
    push cx
    call KERNEL_SEG:font_width
    mov cx, DL_W
    sub cx, ax
    sar cx, 1
    add cx, [cs:dl_px]
    mov ax, (CDGRAY << 8) | CWHITE
    call KERNEL_SEG:cw_font_run
    pop cx
    pop bx
    pop ax
    ret

; All painting entries preserve every register, caller holds the gfx lock.
dlt_paint:
    push ax
    xor al, al
.next:
    cmp al, [dl_count]
    jae .done
    mov [cs:dl_index], al
    call dl_read
    call dl_draw
    inc al
    jmp short .next
.done:
    pop ax
    ret

; Stage rectangle intersects wm_dmg_*? Out CF=0 yes, CF=1 no.
dl_intersects:
    call dl_rect
    cmp ax, [wm_dmg_x2]
    jg .no
    cmp bx, [wm_dmg_y2]
    jg .no
    cmp cx, [wm_dmg_x1]
    jl .no
    cmp dx, [wm_dmg_y1]
    jl .no
    clc
    ret
.no:
    stc
    ret

; Grow to whole touched cells; repeat because cells can overlap. Registers
; preserved. A changed union disarms the WM's stationary-window shortcut.
dlt_damage:
    call dl_enter
.again:
    ; Drive-zone growth can expose another link, and vice versa. Close both
    ; geometries before the WM clears or paints any part of the union.
    call COLD_SEG:desk_dmg_zones_x
    mov byte [cs:dl_changed], 0
    xor di, di
.next:
    mov ax, di
    cmp al, [dl_count]
    jae .pass
    call dl_read
    call dl_intersects
    jc .skip
    cmp ax, [wm_dmg_x1]
    jge .x1
    mov [wm_dmg_x1], ax
    inc byte [cs:dl_changed]
.x1:
    cmp bx, [wm_dmg_y1]
    jge .y1
    mov [wm_dmg_y1], bx
    inc byte [cs:dl_changed]
.y1:
    cmp cx, [wm_dmg_x2]
    jle .x2
    mov [wm_dmg_x2], cx
    inc byte [cs:dl_changed]
.x2:
    cmp dx, [wm_dmg_y2]
    jle .skip
    mov [wm_dmg_y2], dx
    inc byte [cs:dl_changed]
.skip:
    inc di
    jmp short .next
.pass:
    cmp byte [cs:dl_changed], 0
    je .done
    mov word [wm_dmg_stwin], 0
    jmp short .again
.done:
    jmp dl_return

dlt_paint_damage:
    call dl_enter
    xor di, di
.next:
    mov ax, di
    cmp al, [dl_count]
    jae .done
    mov [cs:dl_index], al
    call dl_read
    call dl_intersects
    jc .skip
    call dl_draw
.skip:
    inc di
    jmp short .next
.done:
    jmp dl_return

; AL = cell index, lock held. Paint only this damage and windows over it.
dl_repaint:
    push ax
    push bx
    push cx
    push dx
    call dl_read
    call dl_rect
    call KERNEL_SEG:cw_wm_paint_dmg
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Return selected record from hit testing. CX/DX input, AL index, CF=0 hit.
dl_hit:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, cx
    mov di, dx
    mov al, [dl_count]
.next:
    or al, al
    jz .miss
    dec al
    push ax
    call dl_read
    call dl_rect
    cmp si, ax
    jl .skip
    cmp si, cx
    jg .skip
    cmp di, bx
    jl .skip
    cmp di, dx
    jg .skip
    pop ax
    clc
    jmp short .done
.skip:
    pop ax
    jmp short .next
.miss:
    stc
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; AL = index/FF selection, lock held. Preserve all registers.
dl_select:
    push ax
    push bx
    cmp al, [dl_sel]
    je .done
    mov bl, [dl_sel]
    mov [dl_sel], al
    cmp bl, 0xFF
    je .new
    push ax
    mov al, bl
    call dl_repaint
    pop ax
.new:
    cmp al, 0xFF
    je .done
    call dl_repaint
.done:
    pop bx
    pop ax
    ret

; Desktop press, no gfx lock. CF=1 consumed, else normal drive handling.
dlt_click:
    call dl_enter
    call dl_hit
    jc .miss
    mov [cs:dl_index], al
    call KERNEL_SEG:cw_gfx_lock
    ; Selecting a shortcut withdraws the volume selection too.
    push ax
    mov al, [desk_sel]
    cmp al, 0xFF
    je .selected
    mov byte [desk_sel], 0xFF
    call COLD_SEG:dlf_desk_zone_redraw
.selected:
    pop ax
    cmp al, [dl_sel]
    jne .single
    mov bx, [ui_click_t]
    sub bx, [cs:dl_clock]
    cmp bx, DESK_DBLT
    jae .single
    inc al
    mov [dl_open], al
    mov byte [ui_post], 1
    call KERNEL_SEG:cw_gfx_unlock
    stc
    jmp short .out
.single:
    mov bx, [ui_click_t]
    mov [cs:dl_clock], bx
    call dl_select
    call KERNEL_SEG:cw_gfx_unlock
    call dl_drag
    stc
    jmp short .out
.miss:
    call KERNEL_SEG:cw_gfx_lock
    mov al, 0xFF
    call dl_select
    call KERNEL_SEG:cw_gfx_unlock
    clc
.out:
    jmp dl_return

; Reposition tracker. VRAM-direct XOR is erased before every yield with the
; lock released. Uses the same queue-drain/level fallback as fm_drag.
dl_drag:
    cmp byte [mou_ptr], 0
    je .done                    ; keyboard mouse cannot move during a click
    mov [cs:dl_pressx], cx
    mov [cs:dl_pressy], dx
.wait:
    mov di, ui_ev
    call KERNEL_SEG:cw_evq_mup
    jnc .done
    test byte [mouse_btn], 1
    jz .done
    mov ax, [mouse_x]
    sub ax, [cs:dl_pressx]
    cmp ax, FM_DRAGMIN
    jge .begin
    cmp ax, -FM_DRAGMIN
    jle .begin
    mov ax, [mouse_y]
    sub ax, [cs:dl_pressy]
    cmp ax, FM_DRAGMIN
    jge .begin
    cmp ax, -FM_DRAGMIN
    jle .begin
    call KERNEL_SEG:cw_task_yield
    jmp short .wait
.begin:
    call KERNEL_SEG:cw_gfx_lock
    mov al, [dl_sel]
    call dl_read
    call dl_rect
    mov [cs:dl_oldx], ax
    mov [cs:dl_oldy], bx
    call dl_outline
.track:
    mov di, ui_ev
    call KERNEL_SEG:cw_evq_mup
    jnc .drop
    test byte [mouse_btn], 1
    jz .drop
    call dl_outline
    call KERNEL_SEG:cw_gfx_unlock
    call KERNEL_SEG:cw_task_yield
    call KERNEL_SEG:cw_gfx_lock
    call dl_move
    call dl_outline
    mov ax, [ticks]
.linger:
    call KERNEL_SEG:cw_task_yield
    cmp ax, [ticks]
    je .linger
    jmp short .track
.drop:
    call dl_outline
    call dl_move
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    call KERNEL_SEG:cw_wm_hit
    or bx, bx
    jnz .cancel
    mov ax, [cs:dl_stage+DL_X]
    mov bx, [cs:dl_stage+DL_Y]
    mov [cs:dl_px], ax
    mov [cs:dl_py], bx
    mov al, [dl_sel]
    call dl_write
    mov ax, [cs:dl_oldx]
    mov bx, [cs:dl_oldy]
    mov cx, ax
    add cx, DL_W-1
    mov dx, bx
    add dx, [cs:dl_height]
    dec dx
    call KERNEL_SEG:cw_wm_paint_dmg
    mov al, [dl_sel]
    call dl_repaint
    call dl_owe
.cancel:
    call KERNEL_SEG:cw_gfx_unlock
.done:
    ret

dl_move:
    mov al, [dl_sel]
    call dl_read
    mov ax, [mouse_x]
    sub ax, [cs:dl_pressx]
    add ax, [cs:dl_oldx]
    mov [cs:dl_stage+DL_X], ax
    mov ax, [mouse_y]
    sub ax, [cs:dl_pressy]
    add ax, [cs:dl_oldy]
    mov [cs:dl_stage+DL_Y], ax
    call dl_rect
    ret

dl_outline:
    call dl_rect
    call KERNEL_SEG:cw_vga_xor_rect_vram
    ret

; AX = source ordinal, DI = its Disk state, CX/DX = bare-desktop drop.
; Lock held by fm_drag. No clipboard calls, no target writes.
dlt_drop:
    call dl_enter
    mov byte [cs:dl_existing], 0
    push es
    mov [cs:dl_pressx], cx
    mov [cs:dl_pressy], dx
    cmp dx, MBAR_H+4
    jl .done
    cmp dx, [vid_dock_y0]
    jge .done
    cmp cx, [vid_desk_zx]
    jge .done
    push ax
    call COLD_SEG:dlf_fm_stage_name
    call COLD_SEG:dlf_fmv_get_icon
    mov ax, [fm_ftype]
    mov [cs:dl_stage+DL_TYPE], al
    mov di, [fm_vp]
    mov dl, [di+FS_DRV]
    mov [cs:dl_stage+DL_DRV], dl
    mov ax, [di+FS_CWD]
    call COLD_SEG:dkf_dsk_chdir_q
    pop ax
    jc .disk
    mov ax, [dsk_sigcur]
    cmp byte [dsk_vkind], DVK_FILE
    jne .sig
    xor ax, ax
.sig:
    mov [cs:dl_stage+DL_SIG], ax
    push cs
    pop es
    mov di, dl_stage+DL_PATH
    mov cx, DL_PATHMAX-13
    push bp
    call COLD_SEG:dsk_path_x
    pop bp
    jc .path
    add di, cx
    cmp byte [es:di-1], '\'
    je .append
    mov byte [es:di], '\'
    inc di
.append:
    mov si, fm_name
.name:
    mov al, [si]
    inc si
    mov [es:di], al
    inc di
    or al, al
    jnz .name
    call dl_claim
    jc .memory
    ; Same target means reposition its existing record, not duplicate it.
    xor bx, bx
.search:
    cmp bl, [dl_count]
    jae .new
    mov es, [dl_seg]
    mov di, bx
    mov cl, 8
    shl di, cl
    add di, DL_HDR
    mov al, [cs:dl_stage+DL_DRV]
    cmp al, [es:di+DL_DRV]
    jne .next
    mov ax, [cs:dl_stage+DL_SIG]
    cmp ax, [es:di+DL_SIG]
    jne .next
    add di, DL_PATH
    mov si, dl_stage+DL_PATH
    mov cx, DL_PATHMAX
.compare:
    mov al, [cs:si]
    inc si
    cmp al, [es:di]
    jne .next
    inc di
    or al, al
    jz .existing
    loop .compare
.next:
    inc bx
    jmp short .search
.new:
    cmp bl, DL_MAX
    jae .full
    inc byte [dl_count]
    jmp short .place
.existing:
    mov byte [cs:dl_existing], 1
    ; Keep the new path/picture through composition's staging reads.
    mov es, [dl_seg]
    mov di, DL_CAP
    mov si, dl_stage
    mov cx, DL_ICON/2
    cld
    push ds
    push cs
    pop ds
    rep movsw
    pop ds
    mov si, dsk_ico
    mov cx, 32
    rep movsw
    mov al, bl
    push bx
    call dl_read
    call dl_rect
    mov [cs:dl_oldx], ax
    mov [cs:dl_oldy], bx
    pop bx
    mov es, [dl_seg]
    mov si, DL_CAP
    mov di, dl_stage
    mov cx, DL_ICON/2
.restore:
    mov ax, [es:si]
    mov [cs:di], ax
    add si, 2
    add di, 2
    loop .restore
    mov di, dsk_ico
    mov cx, 32
.restoreicon:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .restoreicon
.place:
    mov ax, [cs:dl_pressx]
    sub ax, DL_W/2
    mov [cs:dl_stage+DL_X], ax
    mov ax, [cs:dl_pressy]
    sub ax, 8
    mov [cs:dl_stage+DL_Y], ax
    push bx
    call dl_rect
    pop bx
    mov al, bl
    call dl_write
    cmp byte [cs:dl_existing], 0
    je .select
    push ax
    mov ax, [cs:dl_oldx]
    mov bx, [cs:dl_oldy]
    mov cx, ax
    add cx, DL_W-1
    mov dx, bx
    add dx, [cs:dl_height]
    dec dx
    call KERNEL_SEG:cw_wm_paint_dmg
    pop ax
.select:
    ; Selection redraw can stage another record, so it follows the write.
    call dl_select
    call dl_repaint
    call dl_owe
    jmp short .done
.full:
    mov si, dl_msgfull
    jmp short .say
.path:
    mov si, dl_msgpath
    jmp short .say
.disk:
    mov si, dl_msgdisk
    jmp short .say
.memory:
    mov si, dl_msgmem
.say:
    call dl_toast
.done:
    pop es
    jmp dl_return

dl_owe:
    mov byte [dl_dirty], 1
    mov byte [dl_save], 1
    mov byte [ui_post], 1
    ret

; AL = File menu item (1 open, 2 remove, 3 retry save). No lock held.
dlt_command:
    cmp al, 3
    jne .selected
    mov byte [dl_save], 1
    mov byte [ui_post], 1
    ret
.selected:
    cmp byte [dl_sel], 0xFF
    je .done
    cmp al, 4
    je dl_info_open
    cmp al, 1
    jne .remove
    mov al, [dl_sel]
    inc al
    mov [dl_open], al
    mov byte [ui_post], 1
    ret
.remove:
    cmp al, 2
    jne .done
    call dl_enter
    push es
    call KERNEL_SEG:cw_gfx_lock
    mov al, [dl_sel]
    call dl_read
    call dl_rect
    mov [cs:dl_oldx], ax
    mov [cs:dl_oldy], bx
    mov byte [dl_open], 0      ; queued indices cannot survive compaction
    ; Compact records inside the store, then repaint the removed cell.
    mov al, [dl_sel]
    xor ah, ah
    mov di, ax
    mov cl, 8
    shl di, cl
    add di, DL_HDR
    mov si, di
    add si, DL_REC
    mov cl, [dl_count]
    xor ch, ch
    dec cx
    sub cx, ax
    mov ax, DL_REC
    mul cx
    mov cx, ax
    mov es, [dl_seg]
.shift:
    jcxz .shifted
    mov al, [es:si]
    mov [es:di], al
    inc si
    inc di
    loop .shift
.shifted:
    dec byte [dl_count]
    mov byte [dl_sel], 0xFF
    mov ax, [cs:dl_oldx]
    mov bx, [cs:dl_oldy]
    mov cx, ax
    add cx, DL_W-1
    mov dx, bx
    add dx, [cs:dl_height]
    dec dx
    call KERNEL_SEG:cw_wm_paint_dmg
    call dl_owe
    call KERNEL_SEG:cw_gfx_unlock
    pop es
    jmp dl_return
.done:
    ret

; Gfx lock not required. Text belongs to the retained module.
dl_toast:
    push cx
    push es
    push cs
    pop es
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    pop es
    pop cx
    ret

; Pending operations, UI task with no gfx lock.
dlt_drain:
    call dl_enter
    push es
    xor al, al
    xchg al, [dl_open]
    or al, al
    jz .save
    dec al
    cmp al, [dl_count]
    jae .save
    ; Paint can run in another task during filesystem I/O. Snapshot the
    ; locator into the claim's UI-only scratch while composition is excluded.
    call KERNEL_SEG:cw_gfx_lock
    call dl_read
    mov es, [dl_seg]
    mov si, dl_stage
    mov di, DL_CAP
    mov cx, DL_ICON/2
    cld
    push ds
    push cs
    pop ds
    rep movsw
    pop ds
    call KERNEL_SEG:cw_gfx_unlock
    call dl_activate
.save:
    xor al, al
    xchg al, [dl_save]
    test al, 1                   ; bit 1 watches the Get Info window, not a save
    jz .done
    cmp byte [dl_dirty], 0
    je .done
    mov al, 1
    call dlt_save_near
    jnc .saved
    mov si, dl_msgsave
    cmp ax, FERR_NOENT
    je .system
    cmp ax, FERR_NODISK
    jne .say
.system:
    mov si, dl_msgsys
.say:
    call dl_toast
    jmp short .done
.saved:
    cmp byte [dl_count], 0
    jne .clean
    call dl_release
.clean:
    mov byte [dl_dirty], 0
.done:
    call dl_info_reap
    pop es
    jmp dl_return

; Walk each 8.3 component afresh; never persist a cluster address. Leaves
; the mounted volume at the parent and SI -> dl_name, BX = leaf cluster.
dl_resolve:
    mov es, [dl_seg]
    mov dl, [es:DL_CAP+DL_DRV]
    xor ax, ax
    call COLD_SEG:dkf_dsk_chdir_q
    jc .fail
    mov ax, [es:DL_CAP+DL_SIG]
    or ax, ax
    jz .walk
    cmp ax, [dsk_sigcur]
    jne .swap
.walk:
    mov si, DL_CAP+DL_PATH+1
.component:
    mov di, dl_openname
    mov cx, 13
.take:
    mov al, [es:si]
    inc si
    or al, al
    jz .last
    cmp al, '\'
    je .folder
    mov [di], al
    inc di
    loop .take
    jmp short .fail
.folder:
    mov byte [di], 0
    push es
    push si
    mov si, dl_openname
    call COLD_SEG:dwf_dskw_stat
    pop si
    pop es
    jc .fail
    test al, 0x10
    jz .fail
    mov ax, bx
    mov dl, [disk_drive]
    call COLD_SEG:dkf_dsk_chdir_q
    jc .fail
    jmp short .component
.last:
    mov byte [di], 0
    mov si, dl_openname
    call COLD_SEG:dwf_dskw_stat
    ret
.swap:
    mov si, dl_msgswap
    stc
    ret
.fail:
    mov si, dl_msggone
    stc
    ret

; Open without the gfx lock. Folder windows use existing view reuse/caps;
; packages and documents use the existing loader and association dispatcher.
dl_activate:
    call dl_resolve
    jnc .target
    ; A leaf stat failure leaves SI at the name; use a useful error message.
    cmp si, dl_openname
    jne .error
    mov si, dl_msggone
.error:
    call dl_toast
    ret
.target:
    test al, 0x10
    jnz .folder
    mov word [ld_pwin], 0
%ifdef OS88_ASSOC
    mov es, [dl_seg]
    cmp byte [es:DL_CAP+DL_TYPE], 0
    jne .program
    xor di, di
    call COLD_SEG:dlf_assoc_post_x
    jc .program
    call COLD_SEG:assoc_run_x
    ret
%endif
.program:
    xor cx, cx
    xor dx, dx
    xor ax, ax
    mov es, ax
    call COLD_SEG:dlf_ld_pkg_start_x
    call COLD_SEG:dlf_ld_say_status
    ret
.folder:
    mov dx, bx
    mov al, [disk_drive]
    call COLD_SEG:dlf_fm_choose
    ret

dl_info_win: dw 0
dl_info_type: db 0
dl_info_target: times DL_PATHMAX+2 db 0
dl_info_kinds: dw dl_info_doc, dl_info_app, dl_info_folder
dl_info_doc: db 'Document shortcut',0
dl_info_app: db 'Application shortcut',0
dl_info_folder: db 'Folder shortcut',0

; Called by Locator with the gfx lock free. Snapshot once; no disk access.
dl_info_open:
    call dl_enter
    push es
    call KERNEL_SEG:cw_gfx_lock
    mov al, [dl_sel]
    call dl_read
    mov al, [cs:dl_stage+DL_TYPE]
    mov [cs:dl_info_type], al
    cld
    push cs
    pop es
    mov al, [cs:dl_stage+DL_DRV]
    add al, 'A'
    mov di, dl_info_target
    stosb
    mov al, ':'
    stosb
    mov si, dl_stage+DL_PATH
    mov cx, DL_PATHMAX
    push ds
    push cs
    pop ds
    rep movsb
    pop ds
    mov bx, [cs:dl_info_win]
    or bx, bx
    jz .create
    test byte [bx+W_FLAGS], 1
    jz .create
    cmp word [bx+W_PAINT], dlf_dl_info_paint
    je .show
.create:
    mov si, dl_info_tpl
    call KERNEL_SEG:apic_wm_create ; DS/ES kernel template -> kernel window
    jc .full
    mov [cs:dl_info_win], bx
.show:
    or byte [dl_save], 2          ; reuse the drain gate until the window closes
    call KERNEL_SEG:cw_wm_show
    jmp short .out
.full:
    call KERNEL_SEG:cw_snd_beep
.out:
    call KERNEL_SEG:cw_gfx_unlock
    pop es
    jmp dl_return

; Unowned kernel windows close by hiding. Reclaim the slot in the next UI
; drain, like the file dialog, after its callback stack has unwound.
dl_info_reap:
    mov bx, [cs:dl_info_win]
    or bx, bx
    jz .done
    call KERNEL_SEG:cw_gfx_lock
    test byte [bx+W_FLAGS], 2
    jnz .watch
    call KERNEL_SEG:apic_wm_destroy
    mov word [cs:dl_info_win], 0
    jmp short .unlock
.watch:
    or byte [dl_save], 2
.unlock:
    call KERNEL_SEG:cw_gfx_unlock
.done:
    ret

; W_PAINT: cached snapshot only, all registers preserved. Three path rows
; fit even on CGA; the fixed 8px font puts 48 characters in 384 pixels.
dl_info_paint:
    call dl_enter
    push es
    mov cx, [si+W_X]
    add cx, 8
    mov dx, [si+W_Y]
    add dx, TITLE_H+8
    xor bx, bx
    mov bl, [cs:dl_info_type]
    shl bx, 1
    mov si, [cs:dl_info_kinds+bx]
    call .line
    add dx, 16
    mov si, dl_info_target
.row:
    mov di, si
    mov bx, 48
.scan:
    cmp byte [cs:di], 0
    je .chunk
    inc di
    dec bx
    jnz .scan
.chunk:
    xor al, al
    xchg al, [cs:di]
    push ax
    call .line
    pop ax
    mov [cs:di], al
    or al, al
    jz .done
    mov si, di
    add dx, 12
    jmp short .row
.done:
    pop es
    jmp dl_return
.line:
    push ds
    push cs
    pop ds                      ; the public X cell reads module text
    mov ax, (CWHITE << 8) | CBLACK
    call KERNEL_SEG:apic_font_run
    pop ds
    ret

dl_key:
    cmp word [menu_win], 0
    jne .miss
    cmp al, 13
    je .open
    cmp ah, 0x53
    jne .miss
    mov al, 2
    jmp short .command
.open:
    mov al, 1
.command:
    call dlt_command
    stc
    ret
.miss:
    clc
    ret

; A small local scan saves a permanent cold-side trampoline.
dl_icon_blank:
    push ax
    push cx
    push si
    xor ax, ax
    mov cx, 32
.orw:
    or ax, [si]
    inc si
    inc si
    loop .orw
    or ax, ax
    pop si
    pop cx
    pop ax
    ret

; Module-local register ladders: same layout as kentc_di/kretc_di.
dl_enter:
    ; STKBALANCE-NET: +6 - banks AX..DI on the caller's stack
    push bx
    push cx
    push dx
    push si
    push di
    mov di, sp
    push word [ss:di+10]
    mov [ss:di+10], ax
    mov di, [ss:di]
    ret
dl_return:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%macro DLENTRY 1
dlt_entry_%1:
    call %1
    retf
%endmacro
DLENTRY dlt_paint
DLENTRY dlt_damage
DLENTRY dlt_paint_damage
DLENTRY dlt_click
DLENTRY dlt_drop
DLENTRY dlt_command
DLENTRY dlt_drain
DLENTRY dl_info_paint
DLENTRY dl_key

section .text
%endif
