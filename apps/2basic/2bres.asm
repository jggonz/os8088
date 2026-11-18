; 2BRES - native OS8088 editor for 2BR3 named resource packs.
; Supports sprite banks, 1/4-bit images, TEXT, DATA and WORDS16.
; All file operations happen on the OS8088 UI task.
%include "os88api.inc"

    OS88_HEADER '2BRES', br_entry, 1, OS88_STACK_384 | OS88_HIGH | OS88_HIGHMEM
    OS88_ICON16
    ; Exactly 16 mask rows followed by 16 data rows (64 bytes total).
    dw 0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh
    dw 0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh,0FFFFh
    dw 0FFFFh,0FFFFh,08001h,0BF7Dh,0A145h,0A145h,0BF7Dh,08001h
    dw 0BFFDh,0A005h,0A005h,0BFFDh,08001h,0FFFFh,00000h,00000h
    OS88_ICON16_END

BR_RES_SIZE  equ 3176
BR_TEXT_CAP  equ 2048
BR_BMP_CAP   equ 1024
BR_COUNT     equ 99
BR_V3_CAP    equ 8192
BR_EXTRA_MAX equ 4
BR_EXTRA_UNIT equ 1024

%macro BR_JC 1
    jnc %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JNC 1
    jc %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JZ 1
    jnz %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JNZ 1
    jz %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JA 1
    jbe %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JAE 1
    jb %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JB 1
    jae %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JBE 1
    ja %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JNE 1
    je %%skip
    jmp %1
%%skip:
%endmacro
%macro BR_JE 1
    jne %%skip
    jmp %1
%%skip:
%endmacro

br_entry:
    push ds
    pop es
    ; CGA 200-line desks receive a compact 4-row editor, never a tall
    ; window whose drawing can escape onto the OS8088 desktop.
    mov word [br_page_step], 8
    call OSAPI_VIDEO
    ; The window stands on light grey in colour; on a 1bpp adapter grey is a
    ; dither (SPEC.md 39.4) that an opaque text run cannot put under its
    ; glyphs - the status line read as a black bar - so there it is white.
    cmp dh, 4
    jae .colour
    mov byte [br_ground], 15
.colour:
    cmp bx, 400                 ; the full editor is 374 rows: a 348-line
    jae .full                   ; Hercules or 350-line EGA cannot hold it
    mov byte [br_compact], 1
    mov word [br_page_step], 4
    mov word [br_template+6], 173
    mov word [br_template+2], 22
.full:
    mov si, br_template
    call OSAPI_WM_CREATE
    jc .out
    mov [br_window], bx
    OS88_REGION_MOVABLE             ; SPEC.md 66.6.1: no segment of our own cached
    mov si, br_menus
    call OSAPI_MENU_SET
    call br_new_project
    mov di, br_manifest
    mov si, br_s_default_manifest
    mov cx, 10
    rep movsb
.out:
    ret

 ; ---- Window commands: File / Resource / Help and toolbar -------------------
; The editor runs on the UI task, with the gfx lock supplied to callbacks.
br_onmenu:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cmp ah, 0
    BR_JE .file
    cmp ah, 1
    BR_JE .resource
    mov word [br_status], br_s_about
    jmp .refresh
.file:
    cmp al, 0
    BR_JE .new
    cmp al, 1
    BR_JE .open
    cmp al, 2
    BR_JE .save
    jmp .done
.resource:
    cmp al, 0
    BR_JE .sprite
    cmp al, 1
    BR_JE .sheet
    cmp al, 2
    BR_JE .img1
    cmp al, 3
    BR_JE .img4
    cmp al, 4
    BR_JE .text
    cmp al, 5
    BR_JE .data
    cmp al, 6
    BR_JE .words
    cmp al, 7
    BR_JE .remove
    cmp al, 8
    BR_JE .rename
    cmp al, 9
    BR_JE .edit
    jmp .done
.new:
    call br_new_project
    jmp .refresh
.open:
    mov al, 1
    jmp .pick
.save:
    mov al, 4
    jmp .pick
.sprite:
    mov al, 2
    jmp .pick
.sheet:
    mov al, 3
    jmp .pick
.img1:
    mov al, 8
    jmp .pick
.img4:
    mov al, 9
    jmp .pick
.text:
    mov al, 6
    jmp .pick
.data:
    mov al, 7
    jmp .pick
.words:
    mov al, 10
.pick:
    mov byte [br_palette], 0
    call br_choose_file
    BR_JNC .done                 ; the file picker paints its OWN modal window
    jmp .refresh
.remove:
    call br_remove_resource
    jmp .refresh
.rename:
    call br_begin_rename
    jmp .refresh
.edit:
    call br_begin_edit
.refresh:
    mov si, [br_window]
    call br_refresh
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A selected TEXT resource is directly editable by typing; ESC finishes.
; R starts a bounded 12-character resource rename, Enter commits, ESC cancels.
br_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cmp byte [br_edit_mode], 0
    BR_JNE .typing
    cmp al, 'a'
    BR_JB .case_done
    cmp al, 'z'
    BR_JA .case_done
    sub al, 32
.case_done:
    cmp al, 'N'
    BR_JE .new
    cmp al, 'O'
    BR_JE .open
    cmp al, 'A'
    BR_JE .palette
    cmp al, 'S'
    BR_JE .save
    cmp al, 'D'
    BR_JE .remove
    cmp al, 'R'
    BR_JE .rename
    cmp al, 'E'
    BR_JE .edit
    cmp ah, 48h
    BR_JE .up
    cmp ah, 50h
    BR_JE .down
    cmp ah, 4Bh
    BR_JE .prev
    cmp ah, 4Dh
    BR_JE .next
    jmp .done
.typing:
    call br_edit_key
    jmp .refresh
.new:
    call br_new_project
    jmp .refresh
.open:
    mov al, 1
    jmp .pick
.save:
    mov al, 4
.pick:
    call br_choose_file
    BR_JNC .done
    jmp .refresh
.palette:
    mov byte [br_edit_mode], 0
    mov byte [br_palette], 1
    jmp .refresh
.remove:
    call br_remove_resource
    jmp .refresh
.rename:
    call br_begin_rename
    jmp .refresh
.edit:
    call br_begin_edit
    jmp .refresh
.up:
    cmp word [br_asset_sel], 0
    BR_JE .done
    dec word [br_asset_sel]
    jmp .refresh
.down:
    mov ax, [br_asset_sel]
    cmp ax, [br_extra_count]
    BR_JAE .done
    inc word [br_asset_sel]
    jmp .refresh
.prev:
    cmp word [br_asset_sel], 0
    BR_JNE .done
    cmp word [br_selected], 0
    BR_JE .done
    dec word [br_selected]
    jmp .refresh
.next:
    cmp word [br_asset_sel], 0
    BR_JNE .done
    cmp word [br_selected], 98
    BR_JAE .done
    inc word [br_selected]
.refresh:
    mov si, [br_window]
    call br_refresh
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Absolute screen click => local content coords, computed by last W_PAINT.
; The Add palette is *inside* the detail pane and never overlaps a file dialog.
br_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    sub cx, [br_left]
    sub dx, [br_top]
    cmp dx, 10
    BR_JB .done
    cmp dx, 35
    BR_JA .below_bar
    cmp cx, 12
    BR_JB .done
    cmp cx, 86
    BR_JB .new
    cmp cx, 166
    BR_JB .open
    cmp cx, 246
    BR_JB .palette
    cmp cx, 348
    BR_JB .remove
    cmp cx, 520
    BR_JB .save
    jmp .done
.below_bar:
    cmp byte [br_compact], 1
    je .compact_click
    cmp byte [br_palette], 0
    BR_JNE .palette_click
    cmp dx, 84
    BR_JB .details_click
    cmp dx, 216
    BR_JAE .nav
    cmp cx, 20
    BR_JB .details_click
    cmp cx, 282
    BR_JAE .details_click
    sub dx, 84
    mov ax, dx
    xor dx, dx
    mov bx, 24
    div bx
    cmp ax, [br_extra_count]
    BR_JA .done
    mov [br_asset_sel], ax
    jmp .refresh
.details_click:
    cmp cx, 284
    BR_JB .done
    cmp dx, 112
    BR_JA .done
    call br_begin_rename
    jmp .refresh
.nav:
    cmp dx, 240
    BR_JB .done
    cmp dx, 271
    BR_JA .done
    cmp word [br_asset_sel], 0
    BR_JNE .done
    cmp cx, 20
    BR_JB .done
    cmp cx, 109
    BR_JB .prev
    cmp cx, 215
    BR_JB .next
    jmp .done
.palette_click:
    cmp cx, 296
    BR_JB .done
    cmp cx, 506
    BR_JAE .done
    cmp dx, 86
    BR_JB .done
    cmp dx, 268
    BR_JAE .done
    sub dx, 86
    mov ax, dx
    xor dx, dx
    mov bx, 25
    div bx
    cmp ax, 0
    BR_JE .sprite
    cmp ax, 1
    BR_JE .sheet
    cmp ax, 2
    BR_JE .img1
    cmp ax, 3
    BR_JE .img4
    cmp ax, 4
    BR_JE .text
    cmp ax, 5
    BR_JE .data
    cmp ax, 6
    BR_JE .words
    jmp .done
.compact_click:
    cmp byte [br_palette], 0
    je .compact_rows
    cmp dx, 59
    jb .done
    cmp dx, 139
    jae .done
    sub dx, 59
    mov ax, dx
    xor dx, dx
    mov bx, 19
    div bx
    cmp ax, 3
    ja .done
    cmp cx, 290
    jb .compact_action
    add ax, 4
.compact_action:
    cmp ax, 0
    je .sprite
    cmp ax, 1
    je .sheet
    cmp ax, 2
    je .img1
    cmp ax, 3
    je .img4
    cmp ax, 4
    je .text
    cmp ax, 5
    je .data
    cmp ax, 6
    je .words
    jmp .done
.compact_rows:
    cmp cx, 282
    jae .done
    cmp dx, 71
    jb .done
    cmp dx, 141
    jae .done
    sub dx, 71
    mov ax, dx
    xor dx, dx
    mov bx, 14
    div bx
    cmp ax, [br_extra_count]
    ja .done
    mov [br_asset_sel], ax
    jmp .refresh
.new:
    call br_new_project
    jmp .refresh
.open:
    mov al, 1
    jmp .pick
.save:
    mov al, 4
    jmp .pick
.palette:
    xor byte [br_palette], 1
    jmp .refresh
.remove:
    call br_remove_resource
    jmp .refresh
.sprite:
    mov al, 2
    jmp .pick
.sheet:
    mov al, 3
    jmp .pick
.img1:
    mov al, 8
    jmp .pick
.img4:
    mov al, 9
    jmp .pick
.text:
    mov al, 6
    jmp .pick
.data:
    mov al, 7
    jmp .pick
.words:
    mov al, 10
.pick:
    mov byte [br_palette], 0
    call br_choose_file
    BR_JNC .done              ; do not repaint the file selector
    jmp .refresh
.prev:
    cmp word [br_selected], 0
    BR_JE .done
    dec word [br_selected]
    jmp .refresh
.next:
    cmp word [br_selected], 98
    BR_JAE .done
    inc word [br_selected]
.refresh:
    mov si, [br_window]
    call br_refresh
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AL=1 OPEN RES, 2 ADD ICON, 3 ADD 64x64 SHEET, 4 SAVE RES, 5 MANIFEST.
br_choose_file:
    mov [br_dialog_mode], al
    mov bx, [br_window]
    mov di, br_onfile
    cmp al, 4
    BR_JE .save
    xor si, si
    xor al, al
    call OSAPI_FILE_DLG
    BR_JNC .done
    mov word [br_status], br_s_dialog_error
    stc
    ret
.save:
    mov si, br_dest
    mov al, FDLG_SAVE
    call OSAPI_FILE_DLG
    BR_JNC .done
    mov word [br_status], br_s_dialog_error
    stc
    ret
.done:
    clc                        ; accepted: do NOT paint over modal dialog
    ret

; File dialog completion: gfx lock held. ES:DI points to kernel filename;
; file API now resolves relative names in the chosen folder automatically.
br_onfile:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; copy kernel filename before any API call
    mov si, di
    mov di, br_filename
    mov cx, 12
.copy:
    mov al, [es:si]
    cmp al, 'a'
    BR_JB .case_ok
    cmp al, 'z'
    BR_JA .case_ok
    sub al, 32
.case_ok:
    mov [di], al
    or al, al
    BR_JZ .copied
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.copied:
    push ds
    pop es
    mov al, [br_dialog_mode]
    cmp al, 1
    BR_JE .open
    cmp al, 2
    BR_JE .icon
    cmp al, 3
    BR_JE .sheet
    cmp al, 4
    BR_JE .save
    cmp al, 5
    BR_JE .manifest
    cmp al, 6
    BR_JE .text
    cmp al, 7
    BR_JE .data
    cmp al, 8
    BR_JE .img1
    cmp al, 9
    BR_JE .img4
    cmp al, 10
    BR_JE .words
    jmp .paint
.open:
    call br_open_pack
    jmp .paint
.icon:
    mov byte [br_kind], 1
    call br_add_selected
    jmp .paint
.sheet:
    mov byte [br_kind], 16
    call br_add_selected
    jmp .paint
.save:
    call br_save_pack
    jmp .paint
.text:
    mov byte [br_kind], 4
    call br_add_blob
    jmp .paint
.data:
    mov byte [br_kind], 5
    call br_add_blob
    jmp .paint
.words:
    mov byte [br_kind], 6
    call br_add_blob
    jmp .paint
.img1:
    mov byte [br_kind], 1
    call br_add_image
    jmp .paint
.img4:
    mov byte [br_kind], 2
    call br_add_image
    jmp .paint
.manifest:
    mov si, br_filename
    mov di, br_manifest
    mov cx, 13
    rep movsb
    call br_pack
.paint:
    mov si, [br_window]
    call br_refresh
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_prev_page:
    cmp word [br_page], 0
    BR_JE .done
    mov ax, [br_page_step]
    sub [br_page], ax
    mov ax, [br_page]
    mov [br_selected], ax
.done:
    ret
br_next_page:
    cmp word [br_page], 96
    BR_JAE .done
    mov ax, [br_page_step]
    add [br_page], ax
    cmp word [br_page], 96
    BR_JBE .store
    mov word [br_page], 96
.store:
    mov ax, [br_page]
    mov [br_selected], ax
.done:
    ret

; New project never touches the disk. 2BR2 is an exact 8-byte header followed
; by 99*32 monochrome 16x16 sprite buffers; zero sprite is transparent.
br_new_project:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, br_res_data
    mov cx, BR_RES_SIZE
    xor al, al
    rep stosb
    mov word [br_res_data], 4232h
    mov word [br_res_data + 2], 3252h
    mov byte [br_res_data + 4], 16
    mov byte [br_res_data + 5], 16
    mov byte [br_res_data + 6], 99
    mov byte [br_res_data + 7], 1
    mov di, br_seen
    mov cx, BR_COUNT
    xor al, al
    rep stosb
    mov di, br_sources
    mov cx, BR_COUNT*13
    rep stosb
    mov di, br_types
    mov cx, BR_COUNT
    rep stosb
    mov di, br_dest
    mov si, br_s_default_res
    mov cx, 13
    rep movsb
    mov word [br_icons], 0
    mov word [br_bank_count], 0
    mov word [br_page], 0
    mov word [br_selected], 0
    mov word [br_status], br_s_idle
    mov word [br_extra_count], 0
    mov word [br_extra_selected], 0
    mov word [br_asset_sel], 0
    mov byte [br_palette], 0
    mov byte [br_edit_mode], 0
    mov si, br_s_default_bank
    mov di, br_bank_name
    mov cx, 13
    rep movsb
    pop es
    pop di
    pop cx
    pop ax
    ret

; Find a vacant slot at/after selection, wrapping to zero if necessary. For
; sheets find an aligned contiguous group of 16 slots (0,16,..,80).
br_find_free:
    push bx
    push cx
    mov cx, 99
    mov bx, [br_selected]
    cmp byte [br_kind], 16
    BR_JNE .loop
    and bx, 0FFF0h
    mov cx, 6
.sheet:
    cmp bx, 80
    BR_JBE .sheet_scan
    xor bx, bx
.sheet_scan:
    push bx
    mov si, bx
    mov dx, 16
.scan:
    cmp byte [br_seen + si], 0
    BR_JNE .busy
    inc si
    dec dx
    BR_JNZ .scan
    pop bx
    mov ax, bx
    clc
    jmp .done
.busy:
    pop bx
    add bx, 16
    dec cx
    BR_JNZ .sheet
    jmp .bad
.loop:
    cmp bx, 99
    BR_JB .check
    xor bx, bx
.check:
    cmp byte [br_seen + bx], 0
    BR_JE .good
    inc bx
    loop .loop
.bad:
    stc
    jmp .done
.good:
    mov ax, bx
    clc
.done:
    pop cx
    pop bx
    ret

; Existing image decoder performs all 1bpp dimension/palette validation.
br_add_selected:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_find_free
    BR_JC .full
    mov [br_index], ax
    call br_copy_bitmap
    BR_JC .bad
    ; Record each occupied slot's source to give user meaningful list rows.
    xor cx, cx
    mov cl, [br_kind]
    mov bx, [br_index]
.tags:
    mov byte [br_types+bx], 1
    cmp byte [br_kind], 1
    BR_JE .type_set
    mov byte [br_types+bx], 16
.type_set:
    mov ax, bx
    mov dx, 13
    mul dx
    mov di, br_sources
    add di, ax
    mov si, br_filename
    push cx
    mov cx, 13
    rep movsb
    pop cx
    inc bx
    loop .tags
    mov ax, [br_index]
    mov [br_selected], ax
    mov word [br_asset_sel], 0
    mov bx, [br_page_step]
    xor dx, dx
    div bx
    mul bx
    mov [br_page], ax
    mov word [br_status], br_s_added
    jmp .done
.full:
    mov word [br_status], br_s_full
    jmp .done
.bad:
    mov word [br_status], br_s_invalid_bmp
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Remove selected resource. A sheet removes its entire 16-icon import group.
br_delete_slot:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    push ds
    pop es
    mov bx, [br_selected]
    cmp byte [br_seen + bx], 0
    BR_JE .empty
    mov cx, 1
    cmp byte [br_types + bx], 16
    BR_JNE .erase
    and bx, 0FFF0h
    mov cx, 16
.erase:
    push cx
    mov byte [br_seen + bx], 0
    mov byte [br_types + bx], 0
    mov ax, bx
    mov dx, 13
    mul dx
    mov di, br_sources
    add di, ax
    mov cx, 13
    xor al, al
    rep stosb
    mov ax, bx
    mov dx, 32
    mul dx
    mov di, br_res_data + 8
    add di, ax
    mov cx, 32
    rep stosb
    dec word [br_icons]
    inc bx
    pop cx
    loop .erase
    mov word [br_status], br_s_removed
    jmp .done
.empty:
    mov word [br_status], br_s_empty
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_open_pack:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov si, br_filename
    mov bx, br_import_data
    mov cx, BR_V3_CAP
    xor dx, dx
    call OSAPI_FILE_READ
    BR_JC .bad
    test dx, dx
    BR_JNZ .bad
    mov [br_loaded_len], ax
    cmp word [br_import_data], 4232h
    BR_JNE .bad
    cmp word [br_import_data+2], 3252h
    BR_JE .legacy
    cmp word [br_import_data+2], 3352h
    BR_JNE .bad
    call br_validate_v3
    BR_JC .bad
    call br_import_v3
    BR_JC .bad
    jmp .success
.legacy:
    cmp ax, BR_RES_SIZE
    BR_JNE .bad
    cmp word [br_import_data+4], 1010h
    BR_JNE .bad
    cmp word [br_import_data+6], 0163h
    BR_JNE .bad
    call br_new_project
    mov si, br_import_data
    mov di, br_res_data
    mov cx, BR_RES_SIZE
    rep movsb
    mov word [br_bank_count], 99
    call br_mark_bank
.success:
    mov si, br_filename
    mov di, br_dest
    mov cx, 13
    rep movsb
    mov word [br_asset_sel], 0
    mov word [br_status], br_s_opened
    jmp .done
.bad:
    mov word [br_status], br_s_invalid_res
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Mark the first bank count as occupied, including intentionally blank icons.
br_mark_bank:
    push ax
    push bx
    push cx
    mov word [br_icons], 0
    xor bx, bx
    mov cx, [br_bank_count]
.loop:
    jcxz .end
    mov byte [br_seen+bx], 1
    mov byte [br_types+bx], 1
    inc word [br_icons]
    inc bx
    loop .loop
.end:
    pop cx
    pop bx
    pop ax
    ret

; Validate a supported resource file without modifying existing work. We support
; one 16x16 sprite bank and up to four other entries of <=1024 bytes each.
; 2BR3 general-purpose host packer supports larger resources; this small
; on-machine editor REFUSES rather than truncates anything it cannot edit.
br_validate_v3:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [br_loaded_len]
    cmp ax, 24
    jb .bad
    cmp byte [br_import_data+4], 3
    jne .bad
    cmp byte [br_import_data+5], 0
    jne .bad
    cmp word [br_import_data+8], 24
    jne .bad
    cmp byte [br_import_data+14], 24
    jne .bad
    cmp byte [br_import_data+15], 0
    jne .bad
    cmp ax, [br_import_data+12]
    jne .bad
    mov cx, [br_import_data+6]
    cmp cx, BR_EXTRA_MAX+1
    ja .bad
    mov ax, cx
    mov bx, 24
    mul bx
    add ax, 24
    cmp ax, [br_import_data+10]
    jne .bad
    cmp ax, [br_loaded_len]
    ja .bad
    ; checksum
    mov cx, [br_loaded_len]
    sub cx, 24
    mov si, br_import_data+24
    xor dx, dx
.sum:
    jcxz .sum_ok
    xor ax, ax
    mov al, [si]
    add dx, ax
    inc si
    loop .sum
.sum_ok:
    cmp dx, [br_import_data+16]
    jne .bad
    mov di, br_import_data+24
    mov cx, [br_import_data+6]
    xor bx, bx                 ; bank seen
    xor dx, dx                 ; extras seen
.check:
    test cx, cx
    BR_JZ .ok
    mov ax, [di+18]
    test al, 1
    jnz .bad
    cmp ax, [br_import_data+10]
    jb .bad
    add ax, [di+20]
    jc .bad
    cmp ax, [br_loaded_len]
    ja .bad
    cmp byte [di], 'A'
    jb .bad
    cmp byte [di], 'Z'
    ja .bad
    cmp byte [di+12], 3
    jne .extra
    test bx, bx
    jnz .bad
    cmp byte [di+13], 1
    jne .bad
    cmp word [di+14], 16
    jne .bad
    cmp word [di+16], 16
    jne .bad
    mov ax, [di+22]
    test ax, ax
    jz .bad
    cmp ax, 99
    ja .bad
    inc bx
    push cx
    mov cl, 5
    shl ax, cl
    pop cx
    cmp ax, [di+20]
    jne .bad
    jmp .next
.extra:
    cmp dx, BR_EXTRA_MAX
    jae .bad
    cmp word [di+20], BR_EXTRA_UNIT
    ja .bad
    mov al, [di+12]
    cmp al, 1
    je .img1
    cmp al, 2
    je .img4
    cmp al, 4
    je .plain
    cmp al, 5
    je .plain
    cmp al, 6
    je .words
    jmp .bad
.img1:
    cmp byte [di+13], 1
    jne .bad
    mov ax, [di+14]
    cmp ax, 16
    je .dim1
    cmp ax, 32
    jne .bad
.dim1:
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov si, [di+16]
    cmp si, 16
    je .len1
    cmp si, 32
    jne .bad
.len1:
    push dx
    mul si
    pop dx
    cmp ax, [di+20]
    jne .bad
    cmp word [di+22], 0
    jne .bad
    jmp .accept
.img4:
    cmp byte [di+13], 0
    jne .bad
    mov ax, [di+14]
    cmp ax, 16
    je .dim4
    cmp ax, 32
    jne .bad
.dim4:
    shr ax, 1
    mov si, [di+16]
    cmp si, 16
    je .len4
    cmp si, 32
    jne .bad
.len4:
    push dx
    mul si
    pop dx
    cmp ax, [di+20]
    jne .bad
    cmp word [di+22], 0
    jne .bad
    jmp .accept
.plain:
    cmp byte [di+13], 0
    jne .bad
    cmp word [di+14], 0
    jne .bad
    cmp word [di+16], 0
    jne .bad
    cmp word [di+22], 0
    jne .bad
    jmp .accept
.words:
    cmp byte [di+13], 0
    jne .bad
    cmp word [di+14], 0
    jne .bad
    cmp word [di+16], 0
    jne .bad
    mov ax, [di+20]
    test al, 1
    jne .bad
    shr ax, 1
    cmp ax, [di+22]
    jne .bad
.accept:
    inc dx
.next:
    add di, 24
    dec cx
    jmp .check
.ok:
    clc
    jmp .out
.bad:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Copy validated resource pack into the editable sprite and typed-entry model.
br_import_v3:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call br_new_project
    mov word [br_file_index], 0
.loop:
    mov bp, [br_file_index]
    cmp bp, [br_import_data+6]
    jae .good
    mov ax, bp
    mov bx, 24
    mul bx
    mov bp, br_import_data+24
    add bp, ax
    cmp byte [ds:bp+12], 3
    jne .extra
    ; Name from directory, set exactly 12 bytes and NUL.
    mov si, bp
    mov di, br_bank_name
    mov cx, 12
    rep movsb
    mov byte [di], 0
    mov ax, [ds:bp+22]
    mov [br_bank_count], ax
    mov si, br_import_data
    add si, [ds:bp+18]
    mov di, br_res_data+8
    mov cx, [ds:bp+20]
    rep movsb
    call br_mark_bank
    jmp .next
.extra:
    mov bx, [br_extra_count]
    mov ax, bx
    mov cx, 13
    mul cx
    mov di, br_extra_names
    add di, ax
    mov si, bp
    mov cx, 12
    rep movsb
    mov byte [di], 0
    mov al, [ds:bp+12]
    mov [br_extra_types+bx], al
    mov al, [ds:bp+13]
    mov [br_extra_flags+bx], al
    shl bx, 1
    mov ax, [ds:bp+14]
    mov [br_extra_widths+bx], ax
    mov ax, [ds:bp+16]
    mov [br_extra_heights+bx], ax
    mov ax, [ds:bp+20]
    mov [br_extra_lengths+bx], ax
    mov ax, [ds:bp+22]
    mov [br_extra_aux+bx], ax
    shr bx, 1
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov di, br_extra_data
    add di, ax
    mov si, br_import_data
    add si, [ds:bp+18]
    mov cx, [ds:bp+20]
    rep movsb
    inc word [br_extra_count]
.next:
    inc word [br_file_index]
    jmp .loop
.good:
    clc
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_save_pack:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov si, br_filename
.check:
    mov al, [si]
    or al, al
    BR_JZ .bad
    cmp al, '.'
    BR_JE .extension
    inc si
    jmp .check
.extension:
    cmp byte [si+1], 'R'
    BR_JNE .bad
    cmp byte [si+2], 'E'
    BR_JNE .bad
    cmp byte [si+3], 'S'
    BR_JNE .bad
    cmp byte [si+4], 0
    BR_JNE .bad
    call br_build_v3
    BR_JC .bad
    mov si, br_filename
    mov bx, br_import_data
    mov cx, [br_export_len]
    xor dx, dx
    call OSAPI_FILE_WRITE
    BR_JC .bad
    mov di, br_dest
    mov si, br_filename
    mov cx, 13
    rep movsb
    mov word [br_status], br_s_saved
    jmp .done
.bad:
    mov word [br_status], br_s_save_error
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Build the complete 2BR3 pack in staging. BSS sprite data stays untouched.
; Canonical order is bank first, then supplementary named TEXT/DATA resources.
br_build_v3:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov di, br_import_data
    mov cx, BR_V3_CAP
    xor al, al
    rep stosb
    ; Highest occupied index +1 maintains stable numeric PUTRES IDs.
    mov word [br_bank_count], 0
    mov bx, 99
.scan:
    dec bx
    cmp byte [br_seen+bx], 0
    je .scan_next
    inc bx
    mov [br_bank_count], bx
    jmp .scan_done
.scan_next:
    test bx, bx
    jnz .scan
.scan_done:
    mov cx, [br_extra_count]
    cmp cx, BR_EXTRA_MAX
    ja .bad
    cmp word [br_bank_count], 0
    je .no_bank
    inc cx
.no_bank:
    mov [br_file_index], cx       ; resource count
    mov ax, cx
    mov bx, 24
    mul bx
    add ax, 24
    mov [br_export_len], ax      ; current payload offset
    cmp ax, BR_V3_CAP
    ja .bad
    mov word [br_import_data], 4232h
    mov word [br_import_data+2], 3352h
    mov byte [br_import_data+4], 3
    mov [br_import_data+6], cx
    mov word [br_import_data+8], 24
    mov [br_import_data+10], ax
    mov byte [br_import_data+14], 24
    mov bp, br_import_data+24
    cmp word [br_bank_count], 0
    je .extras
    mov si, br_bank_name
    mov di, bp
    mov cx, 12
    rep movsb
    mov byte [ds:bp+12], 3
    mov byte [ds:bp+13], 1
    mov word [ds:bp+14], 16
    mov word [ds:bp+16], 16
    mov ax, [br_export_len]
    mov [ds:bp+18], ax
    mov ax, [br_bank_count]
    mov [ds:bp+22], ax
    mov cl, 5
    shl ax, cl
    mov [ds:bp+20], ax
    add ax, [br_export_len]
    cmp ax, BR_V3_CAP
    ja .bad
    mov [br_export_len], ax
    mov di, br_import_data
    add di, [ds:bp+18]
    mov si, br_res_data+8
    mov cx, [ds:bp+20]
    rep movsb
    add bp, 24
.extras:
    xor bx, bx
.extra:
    cmp bx, [br_extra_count]
    jae .finish
    mov ax, bx
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
    mov di, bp
    mov cx, 12
    rep movsb
    mov al, [br_extra_types+bx]
    mov [ds:bp+12], al
    mov al, [br_extra_flags+bx]
    mov [ds:bp+13], al
    mov di, bx
    shl di, 1
    mov ax, [br_extra_widths+di]
    mov [ds:bp+14], ax
    mov ax, [br_extra_heights+di]
    mov [ds:bp+16], ax
    mov ax, [br_extra_lengths+di]
    mov [ds:bp+20], ax
    mov ax, [br_extra_aux+di]
    mov [ds:bp+22], ax
    mov ax, [br_export_len]
    test al, 1
    jz .aligned
    inc ax
.aligned:
    mov [ds:bp+18], ax
    add ax, [ds:bp+20]
    cmp ax, BR_V3_CAP
    ja .bad
    mov [br_export_len], ax
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov si, br_extra_data
    add si, ax
    mov di, br_import_data
    add di, [ds:bp+18]
    mov cx, [ds:bp+20]
    rep movsb
    inc bx
    add bp, 24
    jmp .extra
.finish:
    mov ax, [br_export_len]
    mov [br_import_data+12], ax
    mov cx, ax
    sub cx, 24
    mov si, br_import_data+24
    xor bx, bx
.sum:
    jcxz .sum_done
    xor ax, ax
    mov al, [si]
    add bx, ax
    inc si
    loop .sum
.sum_done:
    mov [br_import_data+16], bx
    clc
    jmp .out
.bad:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Import up to 1024 bytes of a TEXT or DATA source as a named resource.
; The 8.3 filename stem becomes the 2BR3 name; no implicit truncation.
br_add_blob:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, [br_extra_count]
    cmp bx, BR_EXTRA_MAX
    jae .full
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov di, br_extra_data
    add di, ax
    push ds
    pop es
    mov si, br_filename
    push bx
    mov bx, di
    mov cx, BR_EXTRA_UNIT
    xor dx, dx
    call OSAPI_FILE_READ
    pop bx
    BR_JC .bad
    or dx, dx
    BR_JNZ .bad
    test ax, ax
    jz .bad
    mov [br_file_textlen], ax
    mov word [br_temp_word], 0
    cmp byte [br_kind], 6
    jne .raw
    call br_parse_words
    jc .words_bad
    mov [br_temp_word], cx
.raw:
    mov di, bx
    shl di, 1
    mov [br_extra_lengths+di], ax
    mov cx, [br_temp_word]
    mov [br_extra_aux+di], cx
    mov word [br_extra_widths+di], 0
    mov word [br_extra_heights+di], 0
    mov byte [br_extra_flags+bx], 0
    mov al, [br_kind]
    mov [br_extra_types+bx], al
    call br_register_extra
    jmp .done
.words_bad:
    mov word [br_status], br_s_words_bad
    jmp .done
.full:
    mov word [br_status], br_s_blob_full
    jmp .done
.bad:
    mov word [br_status], br_s_blob_bad
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_remove_blob:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, [br_extra_selected]
    cmp bx, [br_extra_count]
    jae .empty
    push ds
    pop es
    ; Compact entries and their fixed 1KB payloads, then lower count.
.shift:
    mov ax, bx
    inc ax
    cmp ax, [br_extra_count]
    jae .finish
    mov dx, ax
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
    mov ax, bx
    mul cx
    mov di, br_extra_names
    add di, ax
    mov cx, 13
    rep movsb
    mov al, [br_extra_types+bx+1]
    mov [br_extra_types+bx], al
    mov al, [br_extra_flags+bx+1]
    mov [br_extra_flags+bx], al
    mov di, bx
    shl di, 1
    mov si, di
    add si, 2
    mov ax, [br_extra_widths+si]
    mov [br_extra_widths+di], ax
    mov ax, [br_extra_heights+si]
    mov [br_extra_heights+di], ax
    mov ax, [br_extra_aux+si]
    mov [br_extra_aux+di], ax
    mov ax, [br_extra_lengths+si]
    mov [br_extra_lengths+di], ax
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov di, br_extra_data
    add di, ax
    mov si, di
    add si, BR_EXTRA_UNIT
    mov cx, BR_EXTRA_UNIT
    rep movsb
    inc bx
    jmp .shift
.finish:
    dec word [br_extra_count]
    cmp word [br_extra_count], 0
    je .clear_select
    mov ax, [br_extra_count]
    cmp [br_extra_selected], ax
    jb .keep_select
    dec ax
    mov [br_extra_selected], ax
    jmp .keep_select
.clear_select:
    mov word [br_extra_selected], 0
.keep_select:
    mov word [br_status], br_s_blob_removed
    jmp .out
.empty:
    mov word [br_status], br_s_empty
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
; Native UI resource selection, in-place renaming and CP437 text editing.
; Resource selection: 0 = sprite bank; 1..BR_EXTRA_MAX = named entry.
; -----------------------------------------------------------------------------
br_remove_resource:
    push ax
    push bx
    push cx
    push di
    push es
    cmp word [br_asset_sel], 0
    jne .extra
    push ds
    pop es
    mov di, br_res_data+8
    mov cx, 99*32
    xor al, al
    rep stosb
    mov di, br_seen
    mov cx, 99
    rep stosb
    mov di, br_types
    mov cx, 99
    rep stosb
    mov di, br_sources
    mov cx, 99*13
    rep stosb
    mov word [br_icons], 0
    mov word [br_bank_count], 0
    mov word [br_selected], 0
    mov word [br_status], br_s_bank_removed
    jmp .done
.extra:
    mov bx, [br_asset_sel]
    dec bx
    mov [br_extra_selected], bx
    call br_remove_blob
    mov ax, [br_extra_count]
    cmp [br_asset_sel], ax
    jbe .done
    mov [br_asset_sel], ax
.done:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

br_begin_rename:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, br_bank_name
    cmp word [br_asset_sel], 0
    je .copy
    mov ax, [br_asset_sel]
    dec ax
    cmp ax, [br_extra_count]
    jae .done
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
.copy:
    mov di, br_s_edit_name
    mov cx, 13
    rep movsb
    mov di, br_s_edit_name
    xor cx, cx
.count:
    cmp cx, 12
    jae .set
    cmp byte [di], 0
    je .set
    inc cx
    inc di
    jmp .count
.set:
    mov [br_edit_length], cx
    mov byte [br_edit_mode], 1
    mov word [br_status], br_s_rename
.done:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

br_begin_edit:
    push ax
    push bx
    mov ax, [br_asset_sel]
    or ax, ax
    jz .bad
    dec ax
    cmp ax, [br_extra_count]
    jae .bad
    mov bx, ax
    cmp byte [br_extra_types+bx], 4
    jne .bad
    mov byte [br_edit_mode], 2
    mov word [br_status], br_s_editing
    jmp .done
.bad:
    mov word [br_status], br_s_edit_only
.done:
    pop bx
    pop ax
    ret

; AL = ASCII key (0 for navigation). All output is state, no draw until caller.
br_edit_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp al, 27
    je .cancel
    cmp byte [br_edit_mode], 1
    jne .text
    cmp al, 13
    je .commit
    cmp al, 8
    je .rename_back
    cmp al, 'a'
    jb .rename_upper
    cmp al, 'z'
    ja .rename_upper
    sub al, 32
.rename_upper:
    cmp al, 'A'
    jb .ren_digit
    cmp al, 'Z'
    jbe .rename_char
.ren_digit:
    cmp al, '0'
    jb .ren_under
    cmp al, '9'
    jbe .rename_char
.ren_under:
    cmp al, '_'
    jne .done
.rename_char:
    cmp word [br_edit_length], 12
    jae .done
    mov di, br_s_edit_name
    add di, [br_edit_length]
    mov [di], al
    inc di
    mov byte [di], 0
    inc word [br_edit_length]
    jmp .done
.rename_back:
    cmp word [br_edit_length], 0
    je .done
    dec word [br_edit_length]
    mov di, br_s_edit_name
    add di, [br_edit_length]
    mov byte [di], 0
    jmp .done
.commit:
    cmp word [br_edit_length], 0
    je .name_bad
    mov al, [br_s_edit_name]
    cmp al, 'A'
    jb .name_bad
    cmp al, 'Z'
    ja .name_bad
    ; New names must be unique across sprite bank and all extras.
    mov si, br_s_edit_name
    mov di, br_bank_name
    mov cx, 12
    repe cmpsb
    jne .check_extras
    cmp word [br_asset_sel], 0
    jne .name_bad
    jmp .name_ok
.check_extras:
    xor bx, bx
.check_one:
    cmp bx, [br_extra_count]
    jae .name_ok
    mov ax, bx
    mov cx, 13
    mul cx
    mov di, br_extra_names
    add di, ax
    mov si, br_s_edit_name
    mov cx, 12
    repe cmpsb
    jne .check_next
    mov ax, bx
    inc ax
    cmp ax, [br_asset_sel]
    jne .name_bad
.check_next:
    inc bx
    jmp .check_one
.name_ok:
    push ds
    pop es
    mov di, br_bank_name
    cmp word [br_asset_sel], 0
    je .copy_name
    mov ax, [br_asset_sel]
    dec ax
    mov cx, 13
    mul cx
    mov di, br_extra_names
    add di, ax
.copy_name:
    mov si, br_s_edit_name
    mov cx, 13
    rep movsb
    mov byte [br_edit_mode], 0
    mov word [br_status], br_s_idle
    jmp .done
.name_bad:
    mov word [br_status], br_s_bad_name
    jmp .done
.text:
    ; Single-line text editing, CR inserts LF. All bytes stay within the 1KB
    ; resource capacity; ESC exits to avoid consuming command shortcuts.
    mov bx, [br_asset_sel]
    or bx, bx
    jz .cancel
    dec bx
    cmp bx, [br_extra_count]
    jae .cancel
    mov si, bx
    shl si, 1
    mov cx, [br_extra_lengths+si]
    cmp al, 8
    je .text_back
    cmp al, 13
    je .text_newline
    cmp al, 32
    jb .done
    cmp al, 126
    ja .done
    jmp .text_append
.text_newline:
    mov al, 10
.text_append:
    cmp cx, BR_EXTRA_UNIT
    jae .done
    push ax
    mov ax, bx
    mov dx, BR_EXTRA_UNIT
    mul dx
    mov di, br_extra_data
    add di, ax
    add di, cx
    pop ax
    mov [di], al
    inc cx
    mov [br_extra_lengths+si], cx
    jmp .done
.text_back:
    or cx, cx
    jz .done
    dec cx
    mov [br_extra_lengths+si], cx
    jmp .done
.cancel:
    mov byte [br_edit_mode], 0
    mov word [br_status], br_s_idle
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Named entries: register a filename stem without committing partial data.
; Inputs: BX = next extra slot, br_kind = 1 IMG1 / 2 IMG4 / 4 TEXT /
; 5 DATA / 6 WORDS16; descriptor length/aux filled by caller.
; Output CF=0 added; CF=1 error, old resource table remains live.
; -----------------------------------------------------------------------------
br_register_extra:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cmp bx, BR_EXTRA_MAX
    jae .full
    mov ax, bx
    mov cx, 13
    mul cx
    mov di, br_extra_names
    add di, ax
    push di
    mov cx, 13
    xor al, al
    rep stosb
    pop di
    mov si, br_filename
    mov cx, 12
.name:
    mov al, [si]
    test al, al
    jz .named
    cmp al, '.'
    je .named
    mov [di], al
    inc di
    inc si
    loop .name
.named:
    mov byte [di], 0
    ; Valid name starts A..Z and contains only A..Z, 0..9 or underscore.
    mov ax, [br_extra_count]
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
    mov al, [si]
    cmp al, 'A'
    jb .invalid
    cmp al, 'Z'
    ja .invalid
    mov di, si
.check_chars:
    mov al, [di]
    test al, al
    jz .names
    cmp al, 'A'
    jb .digit
    cmp al, 'Z'
    jbe .next_char
.digit:
    cmp al, '0'
    jb .under
    cmp al, '9'
    jbe .next_char
.under:
    cmp al, '_'
    jne .invalid
.next_char:
    inc di
    jmp .check_chars
.names:
    ; Reserve the bank name even while its sprite count is zero.
    mov di, br_bank_name
    mov cx, 12
    push si
    repe cmpsb
    pop si
    je .duplicate
.other_names:
    xor bx, bx
.each:
    cmp bx, [br_extra_count]
    jae .unique
    mov ax, bx
    mov cx, 13
    mul cx
    mov di, br_extra_names
    add di, ax
    mov cx, 12
    push si
    repe cmpsb
    pop si
    je .duplicate
    inc bx
    jmp .each
.unique:
    mov bx, [br_extra_count]
    mov al, [br_kind]
    mov [br_extra_types+bx], al
    inc word [br_extra_count]
    mov ax, [br_extra_count]
    mov [br_asset_sel], ax
    mov word [br_status], br_s_blob_added
    clc
    jmp .done
.duplicate:
    mov word [br_status], br_s_blob_exists
    stc
    jmp .done
.invalid:
    mov word [br_status], br_s_bad_name
    stc
    jmp .done
.full:
    mov word [br_status], br_s_blob_full
    stc
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Convert a TXT file of signed decimal integers to little-endian WORDS16.
; Input br_extra_data[extra_count], br_file_textlen bytes; outputs AX = bytes,
; CX = item count. Staging in br_bmp_data prevents partial table creation.
br_parse_words:
    push bx
    push dx
    push si
    push di
    push bp
    mov ax, [br_extra_count]
    mov bx, BR_EXTRA_UNIT
    mul bx
    mov si, br_extra_data
    add si, ax
    mov bx, si
    add bx, [br_file_textlen]
    mov di, br_bmp_data
    mov word [br_num_count], 0
.skip:
    cmp si, bx
    jae .finished
    mov al, [si]
    cmp al, ' '
    jbe .next
    cmp al, ','
    je .next
    jmp .token
.next:
    inc si
    jmp .skip
.token:
    mov word [br_num_sign], 0
    cmp al, '-'
    jne .plus
    mov word [br_num_sign], 1
    inc si
    jmp .start
.plus:
    cmp al, '+'
    jne .start
    inc si
.start:
    xor bp, bp
    xor dx, dx
.digit:
    cmp si, bx
    jae .endtoken
    mov al, [si]
    cmp al, '0'
    jb .endtoken
    cmp al, '9'
    ja .endtoken
    xor ah, ah
    sub al, '0'
    mov [br_temp_word], ax
    mov ax, bp
    push dx
    mov cx, 10
    mul cx
    pop cx
    test dx, dx
    jne .bad
    add ax, [br_temp_word]
    jc .bad
    cmp ax, 32768
    ja .bad
    mov bp, ax
    inc si
    inc cx
    mov dx, cx
    jmp .digit
.endtoken:
    test dx, dx
    jz .bad
    cmp si, bx
    jae .store
    mov al, [si]
    cmp al, ' '
    jbe .store
    cmp al, ','
    jne .bad
.store:
    cmp word [br_num_sign], 0
    jne .signed
    cmp bp, 32767
    ja .bad
.signed:
    mov ax, bp
    cmp word [br_num_sign], 0
    je .write
    neg ax
.write:
    cmp word [br_num_count], 512
    jae .bad
    mov [di], ax
    add di, 2
    inc word [br_num_count]
    jmp .skip
.finished:
    cmp word [br_num_count], 0
    je .bad
    ; Copy staged word buffer back into the chosen extra slot.
    mov ax, [br_num_count]
    shl ax, 1
    push ax
    mov ax, [br_extra_count]
    mov bx, BR_EXTRA_UNIT
    mul bx
    mov di, br_extra_data
    add di, ax
    mov si, br_bmp_data
    pop ax
    push ax
    mov cx, ax
    push es
    push ds
    pop es
    rep movsb
    pop es
    pop ax
    mov cx, [br_num_count]
    clc
    jmp .out
.bad:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    ret

; Import 16x16 or 32x32 uncompressed Windows BMP as named IMG1 / IMG4.
; Reads a *new* file into staging and checks entire header/pixels BEFORE
; touching the selected resource. 1bpp palette white/black is normalized.
br_add_image:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp word [br_extra_count], BR_EXTRA_MAX
    jae .full
    push ds
    pop es
    mov si, br_filename
    mov bx, br_import_data
    mov cx, BR_TEXT_CAP
    xor dx, dx
    call OSAPI_FILE_READ
    jc .bad
    test dx, dx
    jnz .bad
    mov [br_bmp_length], ax
    cmp ax, 62                 ; 16x16 IMG1 BMP is only 126 bytes
    jb .bad
    cmp word [br_import_data], 4D42h
    jne .bad
    cmp word [br_import_data+14], 40
    jne .bad
    cmp word [br_import_data+16], 0
    jne .bad
    cmp word [br_import_data+20], 0
    jne .bad
    cmp word [br_import_data+24], 0
    jne .bad
    cmp word [br_import_data+26], 1
    jne .bad
    cmp word [br_import_data+30], 0
    jne .bad
    cmp word [br_import_data+32], 0
    jne .bad
    mov ax, [br_import_data+18]
    cmp ax, 16
    je .wok
    cmp ax, 32
    jne .bad
.wok:
    mov [br_bmp_width], ax
    mov ax, [br_import_data+22]
    cmp ax, 16
    je .hok
    cmp ax, 32
    jne .bad
.hok:
    mov [br_bmp_height], ax
    mov ax, 1
    cmp byte [br_kind], 1
    je .bits
    mov ax, 4
.bits:
    cmp [br_import_data+28], ax
    jne .bad
    mov ax, [br_import_data+10]
    cmp byte [br_kind], 1
    je .offset1
    cmp ax, 118
    jb .bad
    jmp .offset
.offset1:
    cmp ax, 62
    jb .bad
.offset:
    mov [br_bmp_offset], ax
    mov ax, [br_bmp_width]
    cmp byte [br_kind], 1
    je .pitch1
    shl ax, 1                 ; 4bpp: width/2 (width * 2 / 4)
    shl ax, 1
.pitch1:
    shr ax, 1
    shr ax, 1
    shr ax, 1                 ; 1bpp: width/8 ; 4bpp: width/2
    mov [br_bmp_rowbytes], ax
    mov bx, ax
    add bx, 3
    and bx, 0FFFCh            ; DWORD row alignment
    mov [br_pitch], bx
    mov ax, [br_bmp_height]
    mul bx
    test dx, dx
    jnz .bad
    add ax, [br_bmp_offset]
    jc .bad
    cmp ax, [br_bmp_length]
    ja .bad
    mov byte [br_invert], 0
    cmp byte [br_kind], 1
    jne .copy
    mov al, [br_import_data+54]
    cmp al, [br_import_data+58]
    jae .copy
    mov byte [br_invert], 1
.copy:
    xor bp, bp
.rows:
    cmp bp, [br_bmp_height]
    jae .staged
    mov ax, [br_bmp_height]
    dec ax
    sub ax, bp
    mul word [br_pitch]
    add ax, [br_bmp_offset]
    mov si, br_import_data
    add si, ax
    mov ax, bp
    mul word [br_bmp_rowbytes]
    mov di, br_bmp_data
    add di, ax
    mov cx, [br_bmp_rowbytes]
    push di
    push cx
    rep movsb
    pop cx
    pop di
    cmp byte [br_invert], 1
    jne .nextrow
.invert:
    not byte [di]
    inc di
    loop .invert
.nextrow:
    inc bp
    jmp .rows
.staged:
    ; Select a currently unused extra slot, write descriptors only after
    ; format validation. Register last, so failed name checks add nothing.
    mov bx, [br_extra_count]
    mov ax, [br_bmp_height]
    mul word [br_bmp_rowbytes]
    mov di, bx
    shl di, 1
    mov [br_extra_lengths+di], ax
    mov word [br_extra_aux+di], 0
    mov ax, [br_bmp_width]
    mov [br_extra_widths+di], ax
    mov ax, [br_bmp_height]
    mov [br_extra_heights+di], ax
    mov byte [br_extra_flags+bx], 0
    cmp byte [br_kind], 1
    jne .name
    mov byte [br_extra_flags+bx], 1
.name:
    call br_register_extra
    jc .out
    mov bx, [br_extra_count]
    dec bx
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov di, br_extra_data
    add di, ax
    mov si, br_bmp_data
    mov bx, [br_extra_count]
    dec bx
    shl bx, 1
    mov cx, [br_extra_lengths+bx]
    rep movsb
    mov word [br_status], br_s_added
    jmp .out
.full:
    mov word [br_status], br_s_blob_full
    jmp .out
.bad:
    mov word [br_status], br_s_image_bad
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --------- Retro OS8088 window UI (all drawing while gfx lock is held) --------
; BR buttons are deliberately plain bevelled rectangles with accessible key
; equivalents; the list and preview use no controls or external GUI library.
; -----------------------------------------------------------------------------
; Native 2BR3 editor. Named resources at LEFT, details/preview at RIGHT.
; Normal clicks refresh only the two panel interiors, NOT the whole window.
; All coordinates are content-relative and clipped by WM_CLIP_SET.
; -----------------------------------------------------------------------------
br_refresh:
    mov byte [br_partial], 1
    call br_paint
    mov byte [br_partial], 0
    ret

br_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov bx, [br_window]
    call OSAPI_WM_CONTENT
    mov [br_left], ax
    mov [br_top], dx
    call OSAPI_WM_GEOM
    BR_JC .out
    call OSAPI_WM_CLIP_SET
    BR_JC .out
    cmp byte [br_compact], 1
    jne .wide_mode
    call br_paint_compact
    jmp .out
.wide_mode:
    cmp byte [br_partial], 1
    je .panels
    ; Only W_PAINT changes the entire window background (not mouse clicks).
    add cx, [br_left]
    dec cx
    add dx, [br_top]
    dec dx
    mov al, [br_ground]
    call OSAPI_SET_COLOR
    mov ax, [br_left]
    mov bx, [br_top]
    call OSAPI_GFX_FILL
    ; Five evenly aligned toolbar controls, all fully inside content width.
    mov si, br_s_new
    mov ax, 12
    mov bx, 85
    call br_button
    mov si, br_s_open
    mov ax, 92
    mov bx, 165
    call br_button
    mov si, br_s_add
    mov ax, 172
    mov bx, 245
    call br_button
    mov si, br_s_remove
    mov ax, 252
    mov bx, 347
    call br_button
    mov si, br_s_save
    mov ax, 352
    mov bx, 516
    call br_button
.panels:
    mov al, 15
    call OSAPI_SET_COLOR
    mov ax, [br_left]
    add ax, 14
    mov bx, [br_top]
    add bx, 50
    mov cx, [br_left]
    add cx, 281
    mov dx, [br_top]
    add dx, 279
    call OSAPI_GFX_FILL
    mov al, 15
    call OSAPI_SET_COLOR
    mov ax, [br_left]
    add ax, 289
    mov bx, [br_top]
    add bx, 50
    mov cx, [br_left]
    add cx, 516
    mov dx, [br_top]
    add dx, 279
    call OSAPI_GFX_FILL
    mov cx, [br_left]
    add cx, 23
    mov dx, [br_top]
    add dx, 57
    mov si, br_s_resources
    call br_text_black
    mov cx, [br_left]
    add cx, 23
    mov dx, [br_top]
    add dx, 73
    mov si, br_s_name_type
    call br_text_black
    xor bp, bp
.rows:
    cmp bp, [br_extra_count]
    ja .details
    ; Make one padded, terminator-safe row: 12-character name + resource type.
    mov si, br_bank_name
    mov al, 3
    or bp, bp
    jz .make
    mov bx, bp
    dec bx
    mov al, [br_extra_types+bx]
    mov ax, bx
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
    mov al, [br_extra_types+bx]
.make:
    push ax
    mov di, br_s_row_asset
    mov cx, 12
    call br_copy_pad
    mov byte [br_s_row_asset+12], ' '
    pop ax
    call br_kind_string         ; SI = type name, AL = type value
    mov di, br_s_row_asset+13
    mov cx, 9
    call br_copy_pad
    ; Selected row is one blue opaque text run (no full-window erase).
    mov cx, [br_left]
    add cx, 21
    mov dx, bp
    mov ax, 24
    mul dx
    mov dx, ax
    add dx, [br_top]
    add dx, 86
    mov al, 0
    mov ah, 15
    cmp bp, [br_asset_sel]
    jne .draw_row
    mov al, 15
    mov ah, 1
.draw_row:
    mov si, br_s_row_asset
    call OSAPI_FONT_RUN
    inc bp
    jmp .rows
.details:
    mov cx, [br_left]
    add cx, 298
    mov dx, [br_top]
    add dx, 57
    mov si, br_s_details
    call br_text_black
    cmp byte [br_palette], 0
    jne .add_palette
    mov bx, [br_asset_sel]
    test bx, bx
    jnz .extra_details
    mov si, br_bank_name
    mov al, 3
    jmp .show_name
.extra_details:
    dec bx
    cmp bx, [br_extra_count]
    jae .footer
    mov al, [br_extra_types+bx]
    push ax
    mov ax, bx
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
    pop ax
.show_name:
    push ax
    mov cx, [br_left]
    add cx, 298
    mov dx, [br_top]
    add dx, 80
    call br_text_black
    pop ax
    call br_kind_string
    mov cx, [br_left]
    add cx, 298
    mov dx, [br_top]
    add dx, 101
    call br_text_black
    mov cx, [br_left]
    add cx, 298
    mov dx, [br_top]
    add dx, 123
    mov si, br_s_size
    cmp word [br_asset_sel], 0
    jne .size_label
    mov si, br_s_sprite_count
.size_label:
    call br_text_black
    cmp word [br_asset_sel], 0
    jne .extra_value
    mov ax, [br_bank_count]
    jmp .length_display
.extra_value:
    mov bx, [br_asset_sel]
    dec bx
    shl bx, 1
    mov ax, [br_extra_lengths+bx]
.length_display:
    call br_decimal_5
    mov cx, [br_left]
    add cx, 354
    cmp word [br_asset_sel], 0
    jne .size_num
    add cx, 32
.size_num:
    mov dx, [br_top]
    add dx, 123
    mov si, br_s_num
    call br_text_black
    cmp word [br_asset_sel], 0
    jne .draw_data
    mov cx, [br_left]
    add cx, 298
    mov dx, [br_top]
    add dx, 145
    mov si, br_s_sp_index
    call br_text_black
    mov ax, [br_selected]
    call br_number_2
    mov cx, [br_left]
    add cx, 372
    mov dx, [br_top]
    add dx, 145
    mov si, br_digits
    call br_text_black
    mov bx, [br_selected]
    cmp byte [br_seen+bx], 0
    je .bank_end
    call br_draw_preview
.bank_end:
    mov si, br_s_prev
    mov ax, 20
    mov bx, 108
    mov word [br_button_y], 246
    call br_button_at
    mov si, br_s_next
    mov ax, 126
    mov bx, 215
    call br_button_at
    jmp .footer
.draw_data:
    mov bx, [br_asset_sel]
    dec bx
    mov al, [br_extra_types+bx]
    cmp al, 1
    je .image
    cmp al, 2
    je .image
    cmp al, 4
    je .text
    cmp al, 6
    je .words
    jmp .hex
.image:
    call br_draw_image_preview
    jmp .footer
.text:
    call br_draw_text_preview
    jmp .footer
.words:
    call br_draw_words_preview
    jmp .footer
.hex:
    call br_draw_hex_preview
    jmp .footer
.add_palette:
    mov cx, [br_left]
    add cx, 300
    mov dx, [br_top]
    add dx, 62
    mov si, br_s_add_header
    call br_text_black
    mov si, br_s_add_1
    mov bp, 0
.palette_loop:
    cmp bp, 7
    jae .footer
    cmp bp, 0
    jne .p1
    mov si, br_s_add_1
    jmp .ptext
.p1:
    cmp bp, 1
    jne .p2
    mov si, br_s_add_2
    jmp .ptext
.p2:
    cmp bp, 2
    jne .p3
    mov si, br_s_add_3
    jmp .ptext
.p3:
    cmp bp, 3
    jne .p4
    mov si, br_s_add_4
    jmp .ptext
.p4:
    cmp bp, 4
    jne .p5
    mov si, br_s_add_5
    jmp .ptext
.p5:
    cmp bp, 5
    jne .p6
    mov si, br_s_add_6
    jmp .ptext
.p6:
    mov si, br_s_add_7
.ptext:
    mov cx, [br_left]
    add cx, 300
    mov ax, bp
    mov dx, 25
    mul dx
    mov dx, ax
    add dx, [br_top]
    add dx, 89
    call br_text_black
    inc bp
    jmp .palette_loop
.footer:
    mov cx, [br_left]
    add cx, 17
    mov dx, [br_top]
    add dx, 293
    mov si, br_s_output
    call br_text_black
    ; Padded to the 12 an 8.3 name can be: a partial refresh does not
    ; refill the footer, so a shorter name must cover a longer one's tail
    ; (MIXED.RES over UNTITLED.RES read MIXED.RESRES).
    mov si, br_dest
    mov di, br_s_detail_line
    mov cx, 12
    call br_copy_pad
    mov cx, [br_left]
    add cx, 67
    mov dx, [br_top]
    add dx, 293
    mov si, br_s_detail_line
    call br_text_black
    mov cx, [br_left]
    add cx, 290
    mov dx, [br_top]
    add dx, 293
    mov si, br_s_hint
    call br_text_black
    mov di, br_s_detail_line
    mov cx, 60
    mov si, [br_status]
    cmp byte [br_edit_mode], 1
    jne .status_normal
    mov si, br_s_edit_name
.status_normal:
    call br_copy_pad
    mov cx, [br_left]
    add cx, 18
    mov dx, [br_top]
    add dx, 322
    mov si, br_s_detail_line
    mov al, 0
    mov ah, [br_ground]
    call OSAPI_FONT_RUN
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AL = 2BR3 entry type. SI becomes a durable NUL string.
br_kind_string:
    cmp al, 1
    jne .i4
    mov si, br_s_type_img1
    ret
.i4:
    cmp al, 2
    jne .bank
    mov si, br_s_type_img4
    ret
.bank:
    cmp al, 3
    jne .text
    mov si, br_s_type_bank
    ret
.text:
    cmp al, 4
    jne .data
    mov si, br_s_type_text
    ret
.data:
    cmp al, 5
    jne .words
    mov si, br_s_type_data
    ret
.words:
    cmp al, 6
    jne .other
    mov si, br_s_type_words
    ret
.other:
    mov si, br_s_type_unknown
    ret

; Unsigned decimal display, right-aligned five chars and trailing NUL.
; Input AX = <=8192, preserves all registers.
br_decimal_5:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, br_s_num+4
    mov byte [di+1], 0
    mov cx, 5
    mov bx, 10
.loop:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    loop .loop
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Selected TEXT resource, bounded 6 lines x 23 chars, CP437 display.
br_draw_text_preview:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov si, br_extra_data
    add si, ax
    mov di, bx
    shl di, 1
    mov bx, [br_extra_lengths+di]
    xor bp, bp
.line:
    cmp bp, 6
    jae .done
    mov di, br_s_text_line
    mov cx, 23
    mov al, ' '
.spaces:
    mov [di], al
    inc di
    loop .spaces
    mov byte [di], 0
    mov di, br_s_text_line
    mov cx, 23
.chars:
    test bx, bx
    jz .render
    mov al, [si]
    inc si
    dec bx
    cmp al, 13
    je .chars
    cmp al, 10
    je .render
    cmp al, 32
    jb .blank
    cmp al, 126
    jbe .put
.blank:
    mov al, '.'
.put:
    mov [di], al
    inc di
    loop .chars
.render:
    mov cx, [br_left]
    add cx, 298
    mov ax, bp
    mov dx, 17
    mul dx
    mov dx, ax
    add dx, [br_top]
    add dx, 153
    push si
    mov si, br_s_text_line
    call br_text_black
    pop si
    inc bp
    cmp bx, 0
    je .done
    jmp .line
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Hex formatting AL->two uppercase ASCII chars at [DI].
br_hex_byte:
    push ax
    push bx
    mov bl, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call br_hex_digit
    mov [di], al
    inc di
    mov al, bl
    and al, 15
    call br_hex_digit
    mov [di], al
    inc di
    pop bx
    pop ax
    ret
br_hex_digit:
    and al, 15
    cmp al, 9
    jbe .number
    add al, 'A'-10
    ret
.number:
    add al, '0'
    ret

; Generic byte listing for DATA, and a fallback for WORDS16.
br_draw_hex_preview:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov si, br_extra_data
    add si, ax
    mov di, bx
    shl di, 1
    mov bx, [br_extra_lengths+di]
    xor bp, bp
.line:
    cmp bp, 4
    jae .done
    mov di, br_s_text_line
    mov cx, 23
    mov al, ' '
    rep stosb
    mov byte [di], 0
    mov di, br_s_text_line
    mov cx, 7
.byte:
    or bx, bx
    jz .render
    mov al, [si]
    inc si
    dec bx
    call br_hex_byte
    mov byte [di], ' '
    inc di
    loop .byte
.render:
    push si
    mov cx, [br_left]
    add cx, 298
    mov ax, bp
    mov dx, 17
    mul dx
    mov dx, ax
    add dx, [br_top]
    add dx, 153
    mov si, br_s_text_line
    call br_text_black
    pop si
    inc bp
    or bx, bx
    jnz .line
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_draw_words_preview:
    ; A WORDS16 resource's size and value count are shown in the inspector,
    ; followed by its little-endian bytes; signed conversion is in the importer.
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, br_s_values
    mov cx, [br_left]
    add cx, 298
    mov dx, [br_top]
    add dx, 145
    call br_text_black
    mov bx, [br_asset_sel]
    dec bx
    shl bx, 1
    mov ax, [br_extra_aux+bx]
    call br_decimal_5
    mov cx, [br_left]
    add cx, 366
    mov dx, [br_top]
    add dx, 145
    mov si, br_s_num
    call br_text_black
    mov bx, [br_asset_sel]
    dec bx
    call br_draw_hex_preview
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Preview image uses native palette indexes; no unexpected conversion.
; IMG1 ink=0 and transparent white; IMG4 passes its source pixel indexes.
br_draw_image_preview:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, bx
    mov cx, BR_EXTRA_UNIT
    mul cx
    mov si, br_extra_data
    add si, ax
    mov di, bx
    shl di, 1
    mov ax, [br_extra_widths+di]
    mov [br_bmp_width], ax
    mov ax, [br_extra_heights+di]
    mov [br_bmp_height], ax
    mov al, [br_extra_types+bx]
    mov [br_kind], al
    mov ax, [br_bmp_width]
    cmp byte [br_kind], 1
    je .pitch1
    shr ax, 1
    jmp .pitch
.pitch1:
    shr ax, 1
    shr ax, 1
    shr ax, 1
.pitch:
    mov [br_bmp_rowbytes], ax
    xor bp, bp
.row:
    cmp bp, [br_bmp_height]
    jae .done
    mov ax, bp
    mul word [br_bmp_rowbytes]
    mov di, si
    add di, ax
    xor bx, bx
.pixel:
    cmp bx, [br_bmp_width]
    jae .nextrow
    mov ax, bx
    cmp byte [br_kind], 1
    je .mono
    shr ax, 1
    add di, ax
    mov al, [di]
    test bl, 1
    jnz .lower
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lower:
    and al, 15
    jmp .color
.mono:
    mov cx, bx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax
    mov al, [di]
    and cl, 7
    mov ah, 7
    sub ah, cl
    mov cl, ah
    shr al, cl
    and al, 1
    jz .skip
    mov al, 0
.color:
    call OSAPI_SET_COLOR
    ; Draw 16x16 at 6x or 32x32 at 3x inside 128x128 inspector.
    mov ax, bx
    mov cx, bp
    mov dx, 6
    cmp word [br_bmp_width], 16
    je .scale
    mov dx, 3
.scale:
    push dx
    mul dx
    add ax, [br_left]
    add ax, 354
    push ax
    mov ax, cx
    pop cx
    mul dx
    add ax, [br_top]
    add ax, 158
    mov dx, ax
    pop ax                 ; AX = scale (3 or 6)
    push bx
    mov bx, dx
    mov dx, bx
    add dx, ax
    dec dx
    mov ax, cx
    add cx, 5
    cmp word [br_bmp_width], 16
    je .filled
    sub cx, 3
.filled:
    call OSAPI_GFX_FILL
    pop bx
.skip:
    ; recompute base offset for each x (DI pointed within source row)
    mov ax, bp
    mul word [br_bmp_rowbytes]
    mov di, si
    add di, ax
    inc bx
    jmp .pixel
.nextrow:
    inc bp
    jmp .row
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; CGA 640x200: two compact columns, one-line inspector, touch-safe selection.
; Image preview and sprite arrows are available in the 350/480 line layout;
; CGA users can still create, inspect, name, import and save resources.
br_paint_compact:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [br_partial], 1
    je .panels
    mov al, [br_ground]
    call OSAPI_SET_COLOR
    mov ax, [br_left]
    mov bx, [br_top]
    mov cx, ax
    add cx, 532
    mov dx, bx
    add dx, 145
    call OSAPI_GFX_FILL
    mov si, br_s_new
    mov ax, 12
    mov bx, 85
    call br_button
    mov si, br_s_open
    mov ax, 92
    mov bx, 165
    call br_button
    mov si, br_s_add
    mov ax, 172
    mov bx, 245
    call br_button
    mov si, br_s_remove
    mov ax, 252
    mov bx, 347
    call br_button
    mov si, br_s_save
    mov ax, 352
    mov bx, 516
    call br_button
.panels:
    mov al, 15
    call OSAPI_SET_COLOR
    mov ax, [br_left]
    add ax, 14
    mov bx, [br_top]
    add bx, 45
    mov cx, [br_left]
    add cx, 281
    mov dx, [br_top]
    add dx, 137
    call OSAPI_GFX_FILL
    mov al, 15
    call OSAPI_SET_COLOR
    mov ax, [br_left]
    add ax, 289
    mov bx, [br_top]
    add bx, 45
    mov cx, [br_left]
    add cx, 516
    mov dx, [br_top]
    add dx, 137
    call OSAPI_GFX_FILL
    mov cx, [br_left]
    add cx, 22
    mov dx, [br_top]
    add dx, 53
    mov si, br_s_resources
    call br_text_black
    cmp byte [br_palette], 0
    jne .add_palette
    xor bp, bp
.rows:
    cmp bp, [br_extra_count]
    ja .details
    mov si, br_bank_name
    mov al, 3
    cmp bp, 0
    je .name
    mov bx, bp
    dec bx
    mov al, [br_extra_types+bx]
    push ax
    mov ax, bx
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
    pop ax
.name:
    push ax
    mov di, br_s_row_asset
    mov cx, 12
    call br_copy_pad
    mov byte [br_s_row_asset+12], ' '
    pop ax
    call br_kind_string
    mov di, br_s_row_asset+13
    mov cx, 9
    call br_copy_pad
    mov cx, [br_left]
    add cx, 22
    mov ax, bp
    mov dx, 14
    mul dx
    mov dx, ax
    add dx, [br_top]
    add dx, 71
    mov al, 0
    mov ah, 15
    cmp bp, [br_asset_sel]
    jne .draw
    mov al, 15
    mov ah, 1
.draw:
    mov si, br_s_row_asset
    call OSAPI_FONT_RUN
    inc bp
    jmp .rows
.details:
    mov cx, [br_left]
    add cx, 297
    mov dx, [br_top]
    add dx, 52
    mov si, br_s_details
    call br_text_black
    mov bx, [br_asset_sel]
    cmp bx, 0
    jne .extra
    mov si, br_bank_name
    mov al, 3
    jmp .selected
.extra:
    dec bx
    cmp bx, [br_extra_count]
    jae .footer
    mov al, [br_extra_types+bx]
    push ax
    mov ax, bx
    mov cx, 13
    mul cx
    mov si, br_extra_names
    add si, ax
    pop ax
.selected:
    push ax
    mov cx, [br_left]
    add cx, 297
    mov dx, [br_top]
    add dx, 74
    call br_text_black
    pop ax
    call br_kind_string
    mov cx, [br_left]
    add cx, 297
    mov dx, [br_top]
    add dx, 94
    call br_text_black
    mov ax, [br_bank_count]
    cmp word [br_asset_sel], 0
    je .size
    mov bx, [br_asset_sel]
    dec bx
    shl bx, 1
    mov ax, [br_extra_lengths+bx]
.size:
    call br_decimal_5
    mov cx, [br_left]
    add cx, 297
    mov dx, [br_top]
    add dx, 115
    mov si, br_s_size
    call br_text_black
    mov cx, [br_left]
    add cx, 354
    mov si, br_s_num
    call br_text_black
    jmp .footer
.add_palette:
    mov cx, [br_left]
    add cx, 297
    mov dx, [br_top]
    add dx, 53
    mov si, br_s_add_header
    call br_text_black
    mov si, br_s_add_1
    mov bp, 0
.options:
    cmp bp, 7
    jae .footer
    cmp bp, 0
    jne .c1
    mov si, br_s_add_1
    jmp .draw_opt
.c1:
    cmp bp, 1
    jne .c2
    mov si, br_s_add_2
    jmp .draw_opt
.c2:
    cmp bp, 2
    jne .c3
    mov si, br_s_add_3
    jmp .draw_opt
.c3:
    cmp bp, 3
    jne .c4
    mov si, br_s_add_4
    jmp .draw_opt
.c4:
    cmp bp, 4
    jne .c5
    mov si, br_s_add_5
    jmp .draw_opt
.c5:
    cmp bp, 5
    jne .c6
    mov si, br_s_add_6
    jmp .draw_opt
.c6:
    mov si, br_s_add_7
.draw_opt:
    mov cx, [br_left]
    add cx, 20
    mov ax, bp
    cmp ax, 4
    jb .col
    sub ax, 4
    add cx, 272
.col:
    mov dx, 19
    mul dx
    mov dx, ax
    add dx, [br_top]
    add dx, 61
    call br_text_black
    inc bp
    jmp .options
.footer:
    mov di, br_s_detail_line
    mov cx, 60
    mov si, [br_status]
    call br_copy_pad
    mov cx, [br_left]
    add cx, 18
    mov dx, [br_top]
    add dx, 140
    mov si, br_s_detail_line
    mov al, 0
    mov ah, [br_ground]
    call OSAPI_FONT_RUN
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; in AX,BX = local x1,x2; SI caption. Top toolbar y=12.
br_button:
    mov word [br_button_y], 12
br_button_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax
    mov cx, bx
    mov bx, [br_top]
    add bx, [br_button_y]
    mov dx, bx
    add dx, 22
    add ax, [br_left]
    add cx, [br_left]
    ; fill white without clobbering the rectangle coordinates
    push ax
    push bx
    push cx
    push dx
    mov al, 15
    call OSAPI_SET_COLOR
    pop dx
    pop cx
    pop bx
    pop ax
    push ax
    push bx
    push cx
    push dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    push ax
    push bx
    push cx
    push dx
    mov al, 0
    call OSAPI_SET_COLOR
    pop dx
    pop cx
    pop bx
    pop ax
    call OSAPI_GFX_FRAME
    mov cx, di
    add cx, [br_left]
    add cx, 8
    mov dx, [br_top]
    add dx, [br_button_y]
    add dx, 7
    call br_text_black
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_text_black:
    mov al, 0
    mov ah, 15
    call OSAPI_FONT_RUN
    ret

; Copy NUL string to fixed-width display buffer, padded with spaces.
; SI source, DI dest, CX max; NUL byte written at dest+CX.
br_copy_pad:
    push ax
    push cx
.loop:
    mov al, [si]
    or al, al
    BR_JZ .space
    inc si
    jmp .store
.space:
    mov al, ' '
.store:
    mov [di], al
    inc di
    loop .loop
    mov byte [di], 0
    pop cx
    pop ax
    ret

; AX in 0..98 => decimal ASCII 2 chars for selected slot
br_number_2:
    push ax
    push bx
    mov bl, 10
    div bl
    add ax, 3030h
    mov [br_digits], al
    mov [br_digits+1], ah
    mov byte [br_digits+2], 0
    pop bx
    pop ax
    ret

; 16x16 preview at 6x scale. Draw contiguous lit runs per row.
br_draw_preview:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, [br_selected]
    mov ax, bx
    mov dx, 32
    mul dx
    mov si, br_res_data + 8
    add si, ax
    mov al, 0
    call OSAPI_SET_COLOR
    xor bp, bp
.row:
    cmp bp, 16
    BR_JAE .done
    mov ah, [si]
    mov al, [si+1]
    mov di, ax
    mov cx, 16
    mov bx, 8000h
.pixel:
    test di, bx
    BR_JZ .skip
    ; x = (16-CX)*6 + 354 ; y = row*6 + 158 (inside detail panel)
    push bx
    push cx
    push di
    mov ax, 16
    sub ax, cx
    mov dx, 6
    mul dx
    add ax, [br_left]
    add ax, 354
    mov bx, bp
    mov cx, bx
    shl bx, 1
    shl cx, 1
    shl cx, 1
    add bx, cx
    add bx, [br_top]
    add bx, 158
    mov cx, ax
    add cx, 5
    mov dx, bx
    add dx, 5
    call OSAPI_GFX_FILL
    pop di
    pop cx
    pop bx
.skip:
    shr bx, 1
    loop .pixel
    add si, 2
    inc bp
    jmp .row
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
; br_pack - invoked only on UI task, not while running a graphics worker.
; All validation and sprite writes happen in our own zeroed BSS. File output
; occurs ONCE, after all input files validate. A bad manifest does not partly
; overwrite the previous .RES file.
; -----------------------------------------------------------------------------
br_pack:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov word [br_status], br_s_working
    mov word [br_icons], 0
    mov byte [br_have_output], 0
    mov di, br_res_data
    mov cx, BR_RES_SIZE
    xor al, al
    rep stosb
    mov word [br_res_data], 4232h       ; 2B
    mov word [br_res_data + 2], 3252h   ; R2
    mov byte [br_res_data + 4], 16
    mov byte [br_res_data + 5], 16
    mov byte [br_res_data + 6], 99
    mov byte [br_res_data + 7], 1
    mov di, br_seen
    mov cx, BR_COUNT
    xor al, al
    rep stosb
    mov di, br_types
    mov cx, BR_COUNT
    rep stosb
    mov di, br_sources
    mov cx, BR_COUNT*13
    rep stosb
    mov si, br_manifest
    mov bx, br_text_data
    mov cx, BR_TEXT_CAP - 1
    xor dx, dx
    call OSAPI_FILE_READ
    pushf
    push ds
    pop es
    popf
    BR_JC .read_error
    or dx, dx
    BR_JNZ .read_error
    mov [br_text_length], ax
    mov bx, ax
    mov byte [br_text_data + bx], 0
    mov bx, br_text_data
    mov cx, ax
.upper:
    or cx, cx
    jz .parse_start
    mov al, [bx]
    cmp al, 'a'
    jb .skip_upper
    cmp al, 'z'
    ja .skip_upper
    sub al, 32
    mov [bx], al
.skip_upper:
    inc bx
    dec cx
    jmp .upper
.parse_start:
    mov word [br_next], br_text_data
    mov ax, br_text_data
    add ax, [br_text_length]
    mov [br_text_end], ax
.parse_loop:
    mov si, [br_next]
    cmp si, [br_text_end]
    BR_JAE .complete
    mov di, si
.scan:
    mov al, [di]
    or al, al
    jz .line_end
    cmp al, 13
    je .line_end
    cmp al, 10
    je .line_end
    inc di
    jmp .scan
.line_end:
    mov byte [di], 0
    mov bx, di
    inc bx
.skip_line_endings:
    cmp bx, [br_text_end]
    BR_JAE .next_saved
    cmp byte [bx], 13
    je .skip_one
    cmp byte [bx], 10
    jne .next_saved
.skip_one:
    inc bx
    jmp .skip_line_endings
.next_saved:
    mov [br_next], bx
    call br_process_line
    BR_JC .bad_line
    jmp .parse_loop
.complete:
    cmp byte [br_have_output], 1
    BR_JNE .bad_line
    mov si, br_dest
    mov bx, br_res_data
    mov cx, BR_RES_SIZE
    xor dx, dx
    call OSAPI_FILE_WRITE
    BR_JC .write_error
    mov word [br_status], br_s_finished
    mov word [br_page], 0
    mov word [br_selected], 0
    jmp .out
.read_error:
    mov word [br_status], br_s_read_error
    jmp .out
.bad_line:
    mov word [br_status], br_s_syntax_error
    jmp .out
.write_error:
    mov word [br_status], br_s_write_error
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Parses one NUL-terminated uppercase line at SI.
; Commands: OUT <8.3.RES> | S <slot> <64x64.BMP> | I <slot> <16x16.BMP>
br_process_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_spaces
    cmp byte [si], 0
    BR_JE .empty
    cmp byte [si], '#'
    BR_JE .empty
    cmp byte [si], 'O'
    je .try_output
    cmp byte [si], 'S'
    je .sheet
    cmp byte [si], 'I'
    je .icon
    jmp .error
.try_output:
    cmp byte [si + 1], 'U'
    BR_JNE .error
    cmp byte [si + 2], 'T'
    BR_JNE .error
    add si, 3
    call br_spaces
    mov di, br_dest
    call br_filename_token
    BR_JC .error
    ; OUT must be a .RES, so a typo cannot clobber the source BMP/TXT.
    mov di, br_dest
.find_dot:
    mov al, [di]
    or al, al
    BR_JZ .error
    cmp al, '.'
    je .test_ext
    inc di
    jmp .find_dot
.test_ext:
    cmp byte [di + 1], 'R'
    BR_JNE .error
    cmp byte [di + 2], 'E'
    BR_JNE .error
    cmp byte [di + 3], 'S'
    BR_JNE .error
    cmp byte [di + 4], 0
    BR_JNE .error
    mov byte [br_have_output], 1
    jmp .empty
.sheet:
    mov byte [br_kind], 16
    inc si
    jmp .bitmap_line
.icon:
    mov byte [br_kind], 1
    inc si
.bitmap_line:
    cmp byte [si], ' '
    je .good_sep
    cmp byte [si], 9
    BR_JNE .error
.good_sep:
    call br_spaces
    call br_decimal
    BR_JC .error
    cmp ax, 98
    BR_JA .error
    mov [br_index], ax
    cmp byte [br_kind], 1
    je .fits
    cmp ax, 83
    BR_JA .error
.fits:
    call br_spaces
    mov di, br_filename
    call br_filename_token
    BR_JC .error
    call br_copy_bitmap
    BR_JC .error
.empty:
    clc
    jmp .out
.error:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Advance over spaces and tabs.
br_spaces:
    cmp byte [si], ' '
    je .skip
    cmp byte [si], 9
    jne .done
.skip:
    inc si
    jmp br_spaces
.done:
    ret

; Decimal 0..999 parsed at SI, with at least one digit; CF status.
br_decimal:
    push bx
    push cx
    push dx
    xor ax, ax
    xor cx, cx
.digit:
    mov bl, [si]
    cmp bl, '0'
    jb .finish
    cmp bl, '9'
    ja .finish
    sub bl, '0'
    xor bh, bh
    mov dx, 10
    mul dx
    add ax, bx
    cmp ax, 999
    BR_JA .error
    inc si
    inc cx
    jmp .digit
.finish:
    or cx, cx
    BR_JZ .error
    cmp byte [si], ' '
    je .good
    cmp byte [si], 9
    BR_JNE .error
.good:
    clc
    jmp .out
.error:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; Copy a bare DOS 8.3 filename from SI to DI (13-byte buffer).
br_filename_token:
    push ax
    push bx
    push cx
    mov cx, 12
    xor bx, bx
.loop:
    mov al, [si]
    or al, al
    jz .end
    cmp al, '#'
    je .end
    cmp al, ' '
    je .end
    cmp al, 9
    je .end
    cmp al, '\'
    je .bad
    cmp al, '/'
    je .bad
    cmp al, ':'
    je .bad
    cmp cx, 0
    je .bad
    mov [di], al
    inc si
    inc di
    dec cx
    inc bx
    jmp .loop
.end:
    or bx, bx
    jz .bad
    mov byte [di], 0
    clc
    jmp .out
.bad:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; Read one BMP, check size/format then map 16 or 1 sprite(s) into pack BSS.
; Only uncompressed bottom-up Windows 1bpp BMPs are accepted: 16x16/64x64.
; Visual white is transparent and black is foreground ink; detect palette reversal.
br_copy_bitmap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, br_filename
    mov bx, br_bmp_data
    mov cx, BR_BMP_CAP
    xor dx, dx
    call OSAPI_FILE_READ
    pushf
    push ds
    pop es
    popf
    BR_JC .bad
    or dx, dx
    BR_JNZ .bad
    mov [br_bmp_length], ax
    cmp ax, 126
    BR_JB .bad
    cmp word [br_bmp_data], 4D42h
    BR_JNE .bad
    cmp word [br_bmp_data + 14], 40
    BR_JNE .bad
    cmp word [br_bmp_data + 16], 0
    BR_JNE .bad
    cmp word [br_bmp_data + 20], 0
    BR_JNE .bad
    cmp word [br_bmp_data + 24], 0
    BR_JNE .bad
    cmp word [br_bmp_data + 26], 1
    BR_JNE .bad
    cmp word [br_bmp_data + 28], 1
    BR_JNE .bad
    cmp word [br_bmp_data + 30], 0
    BR_JNE .bad
    cmp word [br_bmp_data + 32], 0
    BR_JNE .bad
    mov ax, 16
    cmp byte [br_kind], 1
    je .size_known
    mov ax, 64
.size_known:
    cmp [br_bmp_data + 18], ax
    BR_JNE .bad
    cmp [br_bmp_data + 22], ax
    BR_JNE .bad
    cmp byte [br_kind], 1
    jne .sheet_pitch
    mov word [br_pitch], 4
    jmp .pitch_done
.sheet_pitch:
    mov word [br_pitch], 8
.pitch_done:
    mov bx, [br_pitch]
    mul bx
    add ax, [br_bmp_data + 10]
    cmp ax, [br_bmp_length]
    BR_JA .bad
    cmp word [br_bmp_data + 12], 0
    BR_JNE .bad
    mov ax, [br_bmp_data + 10]
    cmp ax, 62
    BR_JB .bad
    mov [br_data_offset], ax
    mov byte [br_invert], 0
    ; Monochrome BMP palettes can be white/black or black/white. Normalize
    ; to 2BR2 0=transparent white, 1=foreground dark regardless of index.
    mov al, [br_bmp_data + 54]
    cmp al, [br_bmp_data + 58]
    jae .normal_palette
    mov byte [br_invert], 1
.normal_palette:
    xor bp, bp
.sprite:
    xor bx, bx
    mov bl, [br_kind]
    cmp bp, bx
    BR_JAE .good
    mov bx, [br_index]
    add bx, bp
    cmp byte [br_seen + bx], 0
    BR_JNE .bad
    mov byte [br_seen + bx], 1
    inc word [br_icons]
    mov ax, bx
    mov bx, 32
    mul bx
    mov di, br_res_data + 8
    add di, ax
    xor ax, ax
    mov [br_col_offset], ax
    mov word [br_source_row], 15
    cmp byte [br_kind], 1
    je .rows
    mov ax, bp
    and ax, 3
    shl ax, 1
    mov [br_col_offset], ax
    mov ax, bp
    shr ax, 1
    shr ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1            ; (sprite / 4) * 16
    mov bx, 63
    sub bx, ax
    mov [br_source_row], bx
.rows:
    mov cx, 16
.row:
    mov ax, [br_source_row]
    mul word [br_pitch]
    add ax, [br_data_offset]
    add ax, [br_col_offset]
    mov si, br_bmp_data
    add si, ax
    mov ax, [si]
    cmp byte [br_invert], 0
    je .normal_pixel
    not ax
.normal_pixel:
    mov [di], ax
    add di, 2
    dec word [br_source_row]
    loop .row
    inc bp
    jmp .sprite
.good:
    clc
    jmp .out
.bad:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_template:
    dw 42, 32, 542, 374
    dw br_title, br_cls
; The window's class (SPEC.md 11.104): every handler it has.
    OS88_CLASS br_cls, 0, PAINT, br_paint, ONKEY, br_onkey, \
               ONCLICK, br_onclick
br_ground:               db 7          ; window ground: br_entry picks it
br_title:                db '2BRES B36',0
br_menu_name:            db '2BRES',0
br_menu_file:            db 'File',0
br_menu_edit:            db 'Resource',0
br_menu_help:            db 'Help',0
br_menu_new:             db 'New',0
br_menu_open:            db 'Open .RES...',0
br_menu_save:            db 'Save As...',0
br_menu_import:          db 'Import Manifest...',0
br_menu_addicon:         db 'Add 16x16 BMP...',0
br_menu_addsheet:        db 'Add 64x64 Sheet...',0
br_menu_remove:          db 'Remove Sprite',0
br_menu_addtext:         db 'Add Text...',0
br_menu_adddata:         db 'Add Binary...',0
br_menu_remove_data:     db 'Remove Named Resource',0
br_menu_prev:            db 'Rename...',0
br_menu_next:            db 'Edit Text',0
br_menu_about:           db 'About 2BRES',0
br_menu_img1:            db 'Add 1-bit Image...',0
br_menu_img4:            db 'Add 4-bit Image...',0
br_menu_words:           db 'Add WORDS16 Table...',0
; 2BRES application menu set: header {name,oncmd,count}, menu descriptors
; {title,items,count}. Items arrays are strings, no NASM-only macros used.
br_menus:
    dw br_menu_name, br_onmenu, 3
    dw br_menu_file, br_menu_file_items, 3
    dw br_menu_edit, br_menu_edit_items, 10
    dw br_menu_help, br_menu_help_items, 1
br_menu_file_items: dw br_menu_new, br_menu_open, br_menu_save
br_menu_edit_items: dw br_menu_addicon, br_menu_addsheet, br_menu_img1, br_menu_img4, br_menu_addtext, br_menu_adddata, br_menu_words, br_menu_remove_data, br_menu_prev, br_menu_next
br_menu_help_items: dw br_menu_about
br_s_new:                db 'NEW',0
br_s_open:               db 'OPEN',0
br_s_icon:               db 'ADD ICON',0
br_s_sheet:              db 'ADD SHEET',0
br_s_remove:             db 'REMOVE',0
br_s_save:               db 'SAVE AS',0
br_s_prev:               db '< PREV',0
br_s_next:               db 'NEXT >',0
br_s_list_header:        db 'SLOT T  SOURCE / BANK',0
br_s_row_line:           times 30 db 0
br_s_preview:            db 'PREVIEW  #',0
br_s_shortcuts:          db 'CLICK A SLOT TO SELECT',0
br_s_default_res:        db 'UNTITLED.RES',0
br_s_default_manifest:   db '2BRES.TXT',0
br_s_free:               db '(empty)',0
br_s_imported:           db '(from pack)',0
br_s_output:             db 'FILE:',0
br_s_idle:               db 'READY - ADD BMP ARTWORK OR OPEN A .RES',0
br_s_working:            db 'IMPORTING MANIFEST...',0
br_s_read_error:         db 'ERROR: CANNOT READ MANIFEST',0
br_s_syntax_error:       db 'ERROR: INVALID MANIFEST / BMP OR DUPLICATE SLOT',0
br_s_write_error:        db 'ERROR: CANNOT WRITE RESULT',0
br_s_finished:           db 'MANIFEST PACK WRITTEN',0
br_s_dialog_error:       db 'ERROR: FILE DIALOG BUSY',0
br_s_added:              db 'IMAGE ADDED - SAVE AS TO WRITE PACK',0
br_s_removed:            db 'SLOT REMOVED - SAVE TO KEEP CHANGES',0
br_s_empty:              db 'NOTHING IN SELECTED SLOT',0
br_s_full:               db 'NO FREE ROOM FOR THAT IMPORT',0
br_s_invalid_bmp:        db 'ERROR: PICK MONO 16X16 / 64X64 BMP',0
br_s_invalid_res:        db 'ERROR: UNSUPPORTED OR INVALID RES FILE',0
br_s_opened:             db 'PACK OPENED - ADD/REMOVE THEN SAVE AS',0
br_s_saved:              db 'RESOURCE PACK SAVED',0
br_s_save_error:         db 'ERROR: NAME MUST END IN .RES / WRITE FAILED',0
br_s_about:              db '2BRES B36 - NAMED 2BR3 RESOURCE EDITOR',0
br_s_count:              db 'SPRITES:',0
br_s_footer:             db '2BR3 - NAMED, CHECKSUMMED RESOURCES',0
br_s_default_bank:       db 'ICONS',0
                            times 13-6 db 0
br_s_blob_added:         db 'TEXT/DATA ADDED - SAVE AS TO WRITE',0
br_s_blob_full:          db 'MAX 4 DATA ENTRIES - USE HOST 2BR3 TOOL',0
br_s_blob_bad:           db 'ERROR: FILE MISSING OR BIGGER THAN 1KB',0
br_s_blob_removed:       db 'SELECTED TEXT/DATA ENTRY REMOVED',0
br_s_blob_exists:        db 'ERROR: RESOURCE NAME ALREADY USED',0
br_s_other_header:       db 'NAMED RESOURCES (TEXT/DATA)',0
br_s_extra_row:          times 24 db 0
br_s_add:                db 'ADD...',0
br_s_resources:          db 'RESOURCES',0
br_s_name_type:          db 'NAME         TYPE',0
br_s_details:            db 'DETAILS',0
br_s_type_bank:          db 'SPRITES1',0
br_s_type_img1:          db 'IMG1',0
br_s_type_img4:          db 'IMG4',0
br_s_type_text:          db 'TEXT',0
br_s_type_data:          db 'DATA',0
br_s_type_words:         db 'WORDS16',0
br_s_type_unknown:       db 'UNKNOWN',0
br_s_size:               db 'SIZE:',0
br_s_values:             db 'VALUES:',0
br_s_sprite_count:       db 'SPRITES:',0
br_s_sp_index:           db 'SPRITE:',0
br_s_add_header:         db 'ADD RESOURCE',0
br_s_add_1:              db 'Sprite to bank (1-bit)',0
br_s_add_2:              db 'Sheet of 16 sprites',0
br_s_add_3:              db '1-bit BMP image',0
br_s_add_4:              db '4-bit BMP image',0
br_s_add_5:              db 'Text file',0
br_s_add_6:              db 'Binary data file',0
br_s_add_7:              db '16-bit number table',0
br_s_hint:               db 'R RENAME   E EDIT TEXT',0
br_s_rename:             db 'RENAME: TYPE NAME; ENTER',0
br_s_editing:            db 'EDIT TEXT; ESC = DONE',0
br_s_bad_name:           db 'INVALID OR DUPLICATE NAME',0
br_s_edit_only:          db 'SELECT A TEXT RESOURCE',0
br_s_image_bad:          db 'ERROR: BMP MUST BE 1/4BPP 16X16 OR 32X32',0
br_s_words_bad:          db 'ERROR: INVALID NUMBER TABLE',0
br_s_resource_removed:   db 'NAMED RESOURCE REMOVED',0
br_s_bank_removed:       db 'SPRITE BANK CLEARED',0
br_s_row_asset:          times 32 db 0
br_s_detail_line:        times 64 db 0
br_s_text_line:          times 28 db 0
br_s_num:                times 6 db 0
br_s_edit_name:          times 13 db 0
br_s_hex:                times 7 db 0


; ---- Zeroed BSS layout; sprite buffer remains 3176 bytes ------------------
BR_O_WINDOW       equ 0
BR_O_LEFT         equ BR_O_WINDOW + 2
BR_O_TOP          equ BR_O_LEFT + 2
BR_O_STATUS       equ BR_O_TOP + 2
BR_O_ICONS        equ BR_O_STATUS + 2
BR_O_HAVE_OUTPUT  equ BR_O_ICONS + 2
BR_O_MANIFEST     equ BR_O_HAVE_OUTPUT + 1
BR_O_DEST         equ BR_O_MANIFEST + 13
BR_O_FILENAME     equ BR_O_DEST + 13
BR_O_DIGITS       equ BR_O_FILENAME + 13
BR_O_KIND         equ BR_O_DIGITS + 3
BR_O_INDEX        equ BR_O_KIND + 1
BR_O_NEXT         equ BR_O_INDEX + 2
BR_O_TEXT_END     equ BR_O_NEXT + 2
BR_O_TEXT_LENGTH  equ BR_O_TEXT_END + 2
BR_O_BMP_LENGTH   equ BR_O_TEXT_LENGTH + 2
BR_O_PITCH        equ BR_O_BMP_LENGTH + 2
BR_O_DATA_OFFSET  equ BR_O_PITCH + 2
BR_O_COL_OFFSET   equ BR_O_DATA_OFFSET + 2
BR_O_SOURCE_ROW   equ BR_O_COL_OFFSET + 2
BR_O_INVERT       equ BR_O_SOURCE_ROW + 2
BR_O_SEEN         equ BR_O_INVERT + 1
BR_O_TEXT         equ BR_O_SEEN + BR_COUNT
BR_O_BMP          equ BR_O_TEXT + BR_TEXT_CAP
BR_O_RES          equ BR_O_BMP + BR_BMP_CAP
BR_O_PAGE         equ BR_O_RES + BR_RES_SIZE
BR_O_SELECTED     equ BR_O_PAGE + 2
BR_O_DIALOG_MODE  equ BR_O_SELECTED + 2
BR_O_BUTTON_Y     equ BR_O_DIALOG_MODE + 1
BR_O_SOURCES      equ BR_O_BUTTON_Y + 2
BR_O_TYPES        equ BR_O_SOURCES + BR_COUNT*13
BR_O_IMPORT       equ BR_O_TYPES + BR_COUNT
BR_O_STEP         equ BR_O_IMPORT + BR_V3_CAP
BR_O_COMPACT      equ BR_O_STEP + 2
BR_O_BANKNAME       equ BR_O_COMPACT + 1
BR_O_EXTRA_COUNT    equ BR_O_BANKNAME + 13
BR_O_EXTRA_NAMES    equ BR_O_EXTRA_COUNT + 2
BR_O_EXTRA_TYPES    equ BR_O_EXTRA_NAMES + (BR_EXTRA_MAX*13)
BR_O_EXTRA_FLAGS    equ BR_O_EXTRA_TYPES + BR_EXTRA_MAX
BR_O_EXTRA_WIDTHS   equ BR_O_EXTRA_FLAGS + BR_EXTRA_MAX
BR_O_EXTRA_HEIGHTS  equ BR_O_EXTRA_WIDTHS + (BR_EXTRA_MAX*2)
BR_O_EXTRA_AUX      equ BR_O_EXTRA_HEIGHTS + (BR_EXTRA_MAX*2)
BR_O_EXTRA_LENGTHS  equ BR_O_EXTRA_AUX + (BR_EXTRA_MAX*2)
BR_O_EXTRA_DATA     equ BR_O_EXTRA_LENGTHS + (BR_EXTRA_MAX*2)
BR_O_EXPORT_LEN     equ BR_O_EXTRA_DATA + BR_EXTRA_MAX*BR_EXTRA_UNIT
BR_O_LOADED_LEN     equ BR_O_EXPORT_LEN + 2
BR_O_BANK_COUNT     equ BR_O_LOADED_LEN + 2
BR_O_FILE_INDEX     equ BR_O_BANK_COUNT + 2
BR_O_TEMP_WORD      equ BR_O_FILE_INDEX + 2
BR_O_PARTIAL        equ BR_O_TEMP_WORD + 2
BR_O_EXTRA_SEL      equ BR_O_PARTIAL + 1
BR_O_ASSET_SEL       equ BR_O_EXTRA_SEL + 2
BR_O_PALETTE         equ BR_O_ASSET_SEL + 2
BR_O_EDIT_MODE       equ BR_O_PALETTE + 1
BR_O_EDIT_LENGTH     equ BR_O_EDIT_MODE + 1
BR_O_FILE_TEXTLEN    equ BR_O_EDIT_LENGTH + 2
BR_O_BMP_ROWS        equ BR_O_FILE_TEXTLEN + 2
BR_O_BMP_ROWBYTES    equ BR_O_BMP_ROWS + 2
BR_O_BMP_WIDTH       equ BR_O_BMP_ROWBYTES + 2
BR_O_BMP_HEIGHT      equ BR_O_BMP_WIDTH + 2
BR_O_BMP_OFFSET      equ BR_O_BMP_HEIGHT + 2
BR_O_NUM_COUNT       equ BR_O_BMP_OFFSET + 2
BR_O_NUM_SIGN        equ BR_O_NUM_COUNT + 2
BR_BSS_TOTAL         equ BR_O_NUM_SIGN + 2

    OS88_BSS BR_BSS_TOTAL
    OS88_IMAGE_END
br_window        equ os88_image_end + BR_O_WINDOW
br_left          equ os88_image_end + BR_O_LEFT
br_top           equ os88_image_end + BR_O_TOP
br_status        equ os88_image_end + BR_O_STATUS
br_icons         equ os88_image_end + BR_O_ICONS
br_have_output   equ os88_image_end + BR_O_HAVE_OUTPUT
br_manifest      equ os88_image_end + BR_O_MANIFEST
br_dest          equ os88_image_end + BR_O_DEST
br_filename      equ os88_image_end + BR_O_FILENAME
br_digits        equ os88_image_end + BR_O_DIGITS
br_kind          equ os88_image_end + BR_O_KIND
br_index         equ os88_image_end + BR_O_INDEX
br_next          equ os88_image_end + BR_O_NEXT
br_text_end      equ os88_image_end + BR_O_TEXT_END
br_text_length   equ os88_image_end + BR_O_TEXT_LENGTH
br_bmp_length    equ os88_image_end + BR_O_BMP_LENGTH
br_pitch         equ os88_image_end + BR_O_PITCH
br_data_offset   equ os88_image_end + BR_O_DATA_OFFSET
br_col_offset    equ os88_image_end + BR_O_COL_OFFSET
br_source_row    equ os88_image_end + BR_O_SOURCE_ROW
br_invert        equ os88_image_end + BR_O_INVERT
br_seen          equ os88_image_end + BR_O_SEEN
br_text_data     equ os88_image_end + BR_O_TEXT
br_bmp_data      equ os88_image_end + BR_O_BMP
br_res_data      equ os88_image_end + BR_O_RES
br_page          equ os88_image_end + BR_O_PAGE
br_selected      equ os88_image_end + BR_O_SELECTED
br_dialog_mode   equ os88_image_end + BR_O_DIALOG_MODE
br_button_y      equ os88_image_end + BR_O_BUTTON_Y
br_sources       equ os88_image_end + BR_O_SOURCES
br_types         equ os88_image_end + BR_O_TYPES
br_import_data   equ os88_image_end + BR_O_IMPORT
br_page_step     equ os88_image_end + BR_O_STEP
br_compact       equ os88_image_end + BR_O_COMPACT
br_bank_name     equ os88_image_end + BR_O_BANKNAME
br_extra_count   equ os88_image_end + BR_O_EXTRA_COUNT
br_extra_names   equ os88_image_end + BR_O_EXTRA_NAMES
br_extra_types   equ os88_image_end + BR_O_EXTRA_TYPES
br_extra_flags   equ os88_image_end + BR_O_EXTRA_FLAGS
br_extra_widths  equ os88_image_end + BR_O_EXTRA_WIDTHS
br_extra_heights equ os88_image_end + BR_O_EXTRA_HEIGHTS
br_extra_aux     equ os88_image_end + BR_O_EXTRA_AUX
br_extra_lengths equ os88_image_end + BR_O_EXTRA_LENGTHS
br_extra_data    equ os88_image_end + BR_O_EXTRA_DATA
br_export_len    equ os88_image_end + BR_O_EXPORT_LEN
br_loaded_len    equ os88_image_end + BR_O_LOADED_LEN
br_bank_count    equ os88_image_end + BR_O_BANK_COUNT
br_file_index    equ os88_image_end + BR_O_FILE_INDEX
br_temp_word     equ os88_image_end + BR_O_TEMP_WORD
br_partial       equ os88_image_end + BR_O_PARTIAL
br_extra_selected equ os88_image_end + BR_O_EXTRA_SEL
br_asset_sel       equ os88_image_end + BR_O_ASSET_SEL
br_palette         equ os88_image_end + BR_O_PALETTE
br_edit_mode       equ os88_image_end + BR_O_EDIT_MODE
br_edit_length     equ os88_image_end + BR_O_EDIT_LENGTH
br_file_textlen    equ os88_image_end + BR_O_FILE_TEXTLEN
br_bmp_rows        equ os88_image_end + BR_O_BMP_ROWS
br_bmp_rowbytes    equ os88_image_end + BR_O_BMP_ROWBYTES
br_bmp_width       equ os88_image_end + BR_O_BMP_WIDTH
br_bmp_height      equ os88_image_end + BR_O_BMP_HEIGHT
br_bmp_offset      equ os88_image_end + BR_O_BMP_OFFSET
br_num_count       equ os88_image_end + BR_O_NUM_COUNT
br_num_sign        equ os88_image_end + BR_O_NUM_SIGN
