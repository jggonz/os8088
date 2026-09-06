; Pac-Man for os8088 (SPEC.md 89). Native port of Roklan's 1982 Atari disk
; version. Source tables and sprites: assets.inc; provenance: README.md.
; One worker advances play under the graphics lock shared with UI callbacks.
; No private hardware access.
%include "os88api.inc"
    OS88_HEADER 'PACMAN', pm_entry, 1, OS88_STACK_256
    OS88_ICON16
%rep 2
    dw 0x07E0,0x1FF8,0x3FFC,0x7FFE,0x7FFC,0xFFF0,0xFFC0,0xFF00
    dw 0xFF00,0xFFC0,0xFFF0,0x7FFC,0x7FFE,0x3FFC,0x1FF8,0x07E0
%endrep
    OS88_ICON16_END

; Direction bits are the movement handler's: up/down/left/right.
PM_UP equ 1
PM_DOWN equ 2
PM_LEFT equ 4
PM_RIGHT equ 8
PM_PLAY equ 0
PM_READY equ 1
PM_DEAD equ 2
PM_LEVEL equ 3
PM_OVER equ 4
; Actors 0..3 ghosts, 4 player. Positions are Atari HPOS / VPOS.

; Render targets (89.3). PM_TGT_WIN is every kernel drawing slot - the window,
; the 11.2 surface and a SAME-MODE 53 bracket alike; the other two are the
; foreign framebuffers a bracket sets, which this package writes itself.
PM_TGT_WIN equ 0
PM_TGT_V13 equ 1                 ; FSXM_VGA13:  320x200x256, chunky, A000
PM_TGT_C4  equ 2                 ; FSXM_CGA320: 320x200x4, two banks, B800
PM_FS_TOP  equ 16                ; board top in either 320x200 foreign mode
PM_FS_MSG  equ 192               ; ...and the message row under it

; The status strip is four independent fields, so eating a dot letters the
; score and not the other 58 cells (89.3.3).
PM_SD_SCORE equ 1
PM_SD_LIVES equ 2
PM_SD_LEVEL equ 4
PM_SD_MSG   equ 8
PM_SD_ALL   equ 15

; One sprite pixel. Unrolled by its caller so the row costs no `loop`.
%macro PM_SPX 0
    shl al, 1
    jnc %%s
    mov [di], dl
%%s:
    inc di
%endmacro

pm_entry:
    call OSAPI_VIDEO
    cmp bx, 250
    jae .size
    mov word [pm_tpl + WT_H], 140
.size:
    call pm_new
    mov si, pm_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [pm_win], bx
    mov si, pm_menus
    call OSAPI_MENU_SET
    mov si, pm_about
    call OSAPI_ABOUT_SET
    mov si, pm_pref
    call OSAPI_WM_PREFER
    mov cx, 338
    mov dx, 140
    call OSAPI_WM_MINSIZE
    mov al, 1
    call OSAPI_WM_OWNBG
    clc
.out:
    ret

pm_new:
    mov word [pm_score], 0
    mov word [pm_score+2], 0
    mov word [pm_level], 0
    mov byte [pm_lives], 3
    mov byte [pm_bonus], 0
    mov byte [pm_pause], 0
    mov byte [pm_abon], 0
    call pm_board
    ret

pm_board:
    push es
    push ds
    pop es
    mov si, pm_maze_source
    mov di, pm_map
    mov cx, 880
    cld
    rep movsb
    pop es
    mov word [pm_eaten], 0
    mov word [pm_fruit], 0
    mov byte [pm_fruit_count], 0
    call pm_reset_actors
    mov byte [pm_full], 1
    ret

pm_reset_actors:
    mov si, pm_initial_x
    mov di, pm_x
    xor bx, bx
.copy:
    mov al, [si+bx]
    mov [di+bx], al
    mov al, [pm_initial_y+bx]
    mov [pm_y+bx], al
    mov byte [pm_dir+bx], PM_LEFT
    mov byte [pm_eyes+bx], 0
    inc bx
    cmp bx, 5
    jb .copy
    mov word [pm_release], 0
    mov bx, [pm_level]
    cmp bx, 3
    jbe .delay
    mov bx, 3
.delay:
    mov si, 1
.delays:
    xor ax, ax
    mov al, [pm_startv+bx]
    mov cx, 3                    ; 60Hz source delays -> approximately 18Hz
    xor dx, dx
    div cx
    mov [pm_release+si], al
    inc bx
    inc si
    cmp si, 4
    jb .delays
    mov byte [pm_want], PM_LEFT
    mov word [pm_fright], 0
    mov word [pm_chain], 200
    mov word [pm_phase_ticks], 127
    mov byte [pm_chase], 0
    mov byte [pm_mode], PM_READY
    mov word [pm_hold], 36
    mov byte [pm_status_dirty], PM_SD_ALL
    ret

; Public callbacks preserve the dispatcher registers. Internal routines use
; AX/BX/CX/DX/SI/DI/BP as scratch; all shared state is under the graphics lock.
%macro PM_CALLBACK 1
%1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call %1 %+ _body
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endmacro
PM_CALLBACK pm_key
PM_CALLBACK pm_command
PM_CALLBACK pm_about
PM_CALLBACK pm_dismiss
PM_CALLBACK pm_paint


pm_key_body:
    cmp byte [pm_abon], 0
    jne pm_dismiss
    cmp al, 27
    je .exitfs
    cmp al, 'f'
    je .fs
    cmp al, 'F'
    je .fs
    cmp al, 'n'
    je .new
    cmp al, 'N'
    je .new
    cmp al, 'p'
    je .pause
    cmp al, 'P'
    je .pause
    cmp al, ' '
    je .pause
    cmp ah, KSC_LEFT
    je .left
    cmp ah, KSC_RIGHT
    je .right
    cmp ah, KSC_UP
    je .up
    cmp ah, KSC_DOWN
    je .down
    or al, 20h
    cmp al, 'a'
    je .left
    cmp al, 'd'
    je .right
    cmp al, 'w'
    je .up
    cmp al, 's'
    je .down
    ret
.left:  mov byte [pm_want], PM_LEFT
    ret
.right: mov byte [pm_want], PM_RIGHT
    ret
.up:    mov byte [pm_want], PM_UP
    ret
.down:  mov byte [pm_want], PM_DOWN
    ret
.new:
    call pm_new
    jmp pm_paint
.pause:
    xor byte [pm_pause], 1
    or byte [pm_status_dirty], PM_SD_MSG
    jmp pm_redraw
.fs:
    cmp byte [pm_fs], 0
    jne .exitfs                  ; F leaves fullscreen as well as entering it,
    call pm_fs_enter             ; and Esc is the escape hatch (SPEC.md 11.2.1)
    ret
.exitfs:
    cmp byte [pm_fs], 0
    je .out
    xor al, al
    mov bx, [pm_win]
    call OSAPI_FULLSCREEN
    jc .out
    mov byte [pm_fs], 0
    call pm_paint
.out:
    ret

; ---------------------------------------------------------------------------
; pm_fs_enter - the 11.2 surface, and then the machine (SPEC.md 53)
; in:  gfx lock held (key or menu context); preserves all registers
;
; Missile Command's shape (SPEC.md 48), with one difference that is the whole
; point of it here: this bracket SETS A MODE where Missile's F does not. A
; 4.77MHz 8088 cannot letter this board through OSAPI_GFX_BLIT4 at 18.2 Hz -
; the desktop's 4bpp blit is priced per colour change and the maze is nothing
; but colour changes - so fullscreen drops to a mode whose pixels are BYTES
; and the band becomes a copy (89.3). The bracket is an optimisation and not
; the feature: a refusal leaves the game on the 11.2 surface it already has.
; ---------------------------------------------------------------------------
pm_fs_enter:
    push ax
    push bx
    push cx
    call pm_pick_mode
    cmp byte [pm_fsxm], 0FFh
    jne .bracket                 ; A MODE-SETTING BRACKET TAKES NO 11.2 SURFACE
                                 ; (SPEC.md 42.7's measured reason). That
                                 ; surface's own W_PAINT is a whole board
                                 ; through OSAPI_GFX_BLIT4 - 56,320 pixels,
                                 ; ~2.8 s on a 4.77MHz 8088 - and every one of
                                 ; them is thrown away by the mode set two
                                 ; calls later. The same-mode bracket below
                                 ; does want it: it draws through the window's
                                 ; own geometry and there is nothing else for
                                 ; the window to be.
    mov al, 1
    mov bx, [pm_win]
    call OSAPI_FULLSCREEN        ; fronts and repaints us, so the layout
    jc .out                      ; re-derives itself; no second paint here
    mov byte [pm_fs], 1
.bracket:
    mov ax, pm_fsx_main
    mov bx, [pm_win]
    xor cx, cx                   ; no KEEPWORKER: the worker freezes and the
    call OSAPI_FSX_RUN           ; bracket's own loop replaces it
    jnc .left
    cmp byte [pm_fs], 0
    jne .out                     ; refused with the 11.2 surface already up:
                                 ; stay on it, which is what F did before
    mov al, 1                    ; refused before we had any surface: give the
    mov bx, [pm_win]             ; user the one the bracket was an optimisation
    call OSAPI_FULLSCREEN        ; over
    jc .out
    mov byte [pm_fs], 1
    jmp short .out
.left:
    cmp byte [pm_fs], 0
    je .out                      ; we never took a surface: 53.6 has already
    mov byte [pm_fs], 0          ; repainted the desktop whole
    xor al, al
    mov bx, [pm_win]
    call OSAPI_FULLSCREEN
.out:
    pop cx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; pm_pick_mode - the lowest-resolution mode this display will give us
; out: [pm_fsxm] = an FSXM_* id, or 0FFh for a same-mode bracket
;
; ASKED ABOUT OUR WINDOW'S DISPLAY, not the primary (SPEC.md 39.18.2): on a
; two-monitor desktop the answer differs per card and the window moves. A
; Hercules offers nothing below its own 720x348, so it takes the same-mode
; bracket and keeps the picture it had - what it gains there is exclusivity.
; ---------------------------------------------------------------------------
pm_pick_mode:
    push ax
    push bx
    push dx
    mov byte [pm_fsxm], 0FFh
    mov bx, [pm_win]
    call OSAPI_FSX_CAPS
    test ax, 1 << FSXM_VGA13
    jz .cga
    mov byte [pm_fsxm], FSXM_VGA13
    jmp short .out
.cga:
    test ax, 1 << FSXM_CGA320
    jz .out
    mov byte [pm_fsxm], FSXM_CGA320
.out:
    pop dx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; pm_fsx_main - the exclusive main (SPEC.md 53.1)
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours, the gfx lock held for
;      the whole session and every other task frozen
; out: a near ret leaves the bracket; the kernel restores the desktop
;
; The worker's loop rewritten for a machine we own: no lock to take and give
; back, no clip region to arm, no events - keys are polled through int 16h,
; which is legal because this IS the UI task - and one frame per tick, the
; worker's own pace. After OSAPI_FSX_MODE every drawing slot is off limits
; (SPEC.md 53.7), so from there on the band blitters and the letterer below
; are the only things that write a pixel.
; ---------------------------------------------------------------------------
pm_fsx_main:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call OSAPI_FONT_GLYPHS       ; DX:SI = the kernel's 8x8 bitmaps, AL/AH the
    mov [pm_gtab], si            ; range they cover: how an app letters a
    mov [pm_gseg], dx            ; foreign mode without a second typeface
    mov [pm_gfirst], al
    mov [pm_glast], ah
    push ds
    pop es                       ; ES = ours, which is what fsx_mode wants
    mov al, [pm_fsxm]
    cmp al, 0FFh
    je .ready                    ; same-mode: the kernel's slots stay legal and
    mov di, pm_fsi               ; every path below is the one the desktop uses
    call OSAPI_FSX_MODE
    jc .home                     ; refused, and nothing was changed
    mov ax, [pm_fsi+FSI_SEG]
    mov [pm_fseg], ax
    mov ax, [pm_fsi+FSI_STRIDE]
    mov [pm_fstride], ax
    mov ax, [pm_fsi+FSI_BSTEP]
    mov [pm_fbstep], ax
    cmp byte [pm_fsi+FSI_MODE], FSXM_VGA13
    jne .c4
    mov byte [pm_tgt], PM_TGT_V13
    call pm_pal13
    jmp short .ready
.c4:
    mov byte [pm_tgt], PM_TGT_C4
    call pm_palc4
    call pm_c4build
.ready:
    mov byte [pm_full], 1
    mov byte [pm_status_dirty], PM_SD_ALL
    call pm_redraw
.loop:
.keys:
    mov ah, 1                    ; poll: no events are dispatched in a bracket
    int 0x16
    jz .nokey
    xor ah, ah
    int 0x16
    cmp al, 27
    je .done
    cmp al, 'f'
    je .done
    cmp al, 'F'
    je .done
    call pm_fsx_key
    jmp short .keys
.nokey:
    call pm_fsx_frame
    xor al, al                   ; FSXW_TICK: one frame a tick, and it halts
    call OSAPI_FSX_WAIT          ; between polls rather than spinning
    jmp .loop
.done:
    mov byte [pm_full], 1        ; the desktop's picture is owed a whole one
.home:
    mov byte [pm_tgt], PM_TGT_WIN
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; One frame inside the bracket. pm_frame's body without the two questions that
; cannot be true here: nothing else can be the top window, and the About box
; needs an event to appear.
pm_fsx_frame:
    cmp byte [pm_pause], 0
    jne .status
    cmp byte [pm_mode], PM_OVER
    je .status
    cmp byte [pm_mode], PM_PLAY
    je .move
    cmp word [pm_hold], 1
    jbe .move
    dec word [pm_hold]
    jmp short .status
.move:
    call pm_mark_actors
    call pm_step
    call pm_mark_actors
    call pm_redraw
    ret
.status:
    cmp byte [pm_status_dirty], 0
    je .out                      ; a pause message still has to reach the glass
    call pm_redraw
.out:
    ret

; The bracket's keyboard: the windowed bindings minus the ones its caller has
; already taken (F and Esc), and minus the ones that need a window.
pm_fsx_key:
    cmp al, 'n'
    je .win
    cmp al, 'N'
    je .win
    cmp al, 'p'
    je .win
    cmp al, 'P'
    je .win
    cmp al, ' '
    je .win
    cmp ah, KSC_LEFT
    je .win
    cmp ah, KSC_RIGHT
    je .win
    cmp ah, KSC_UP
    je .win
    cmp ah, KSC_DOWN
    je .win
    push ax
    or al, 20h
    cmp al, 'a'
    je .pop
    cmp al, 'd'
    je .pop
    cmp al, 'w'
    je .pop
    cmp al, 's'
    je .pop
    pop ax
    ret
.pop:
    pop ax
.win:
    jmp pm_key                   ; the windowed handler, register-preserving

pm_command_body:
    cmp al, 0
    jne .pause
    mov al, 'n'
    jmp pm_key
.pause:
    cmp al, 1
    jne .full
    mov al, 'p'
    jmp pm_key
.full:
    mov al, 'f'
    jmp pm_key

pm_about_body:
    mov byte [pm_pause], 1
    mov byte [pm_abon], 1
    mov bx, [pm_win]
    mov si, pm_ablines
    call os88ui_about
    ret
pm_dismiss_body:
    cmp byte [pm_abon], 0
    je .out
    mov byte [pm_abon], 0
    call pm_paint
.out:
    ret

pm_worker:
    call OSAPI_GET_TICKS
    mov [pm_due], ax
.loop:
    mov bx, [pm_win]
    call OSAPI_TASK_ALIVE            ; may terminate; never under the lock
    inc word [pm_due]
    call OSAPI_GET_TICKS
    mov bx, [pm_due]
    sub bx, ax
    jle .late
    mov ax, bx
    call OSAPI_TASK_SLEEP
    jmp short .frame
.late:
    mov [pm_due], ax                 ; no bursts of catch-up frames
.frame:
    call OSAPI_GFX_LOCK
    call pm_frame
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD            ; let a waiting UI handler acquire it
    jmp .loop

pm_frame:
    call OSAPI_WM_TOP
    cmp bx, [pm_win]
    jne .out
    cmp byte [pm_pause], 0
    jne .out
    cmp byte [pm_abon], 0
    jne .out
    cmp byte [pm_mode], PM_OVER
    je .out
    cmp byte [pm_mode], PM_PLAY
    je .move
    cmp word [pm_hold], 1
    jbe .move
    dec word [pm_hold]
    ret
.move:
    call pm_mark_actors
    call pm_step
    call pm_mark_actors
    call pm_redraw
.out:
    ret

; One native frame. State delays keep the ready/death/clear screen visible.
pm_step:
    inc word [pm_frames]
    cmp byte [pm_mode], PM_OVER
    je .out
    cmp byte [pm_mode], PM_PLAY
    je .play
    dec word [pm_hold]
    jnz .out
    cmp byte [pm_mode], PM_DEAD
    jne .level
    call pm_reset_actors
    ret
.level:
    cmp byte [pm_mode], PM_LEVEL
    jne .ready
    cmp word [pm_level], 65535
    je .board
    inc word [pm_level]
.board:
    call pm_board
    ret
.ready:
    mov byte [pm_mode], PM_PLAY
    or byte [pm_status_dirty], PM_SD_MSG
    ret
.play:
    cmp word [pm_fright], 0
    je .phase
    dec word [pm_fright]
    jmp short .fruit
.phase:
    dec word [pm_phase_ticks]
    jnz .fruit
    xor byte [pm_chase], 1
    mov word [pm_phase_ticks], 127
    cmp byte [pm_chase], 0
    je .reverse
    mov word [pm_phase_ticks], 364
.reverse:
    call pm_reverse
.fruit:
    cmp word [pm_fruit], 0
    je .player
    dec word [pm_fruit]
    jnz .player
    call pm_mark_fruit
.player:
    mov si, 4
    call pm_allowed
    test al, [pm_want]
    jz .old
    mov ah, [pm_want]
    mov [pm_dir+4], ah
.old:
    test al, [pm_dir+4]
    jz .eat
    call pm_move
.eat:
    call pm_eat
    call pm_collide
    cmp byte [pm_mode], PM_PLAY
    jne .out
    xor si, si
.ghost:
    call pm_ghost
    inc si
    cmp si, 4
    jb .ghost
    call pm_collide
.out:
    ret

; MAZHND: only junctions need a table lookup. Between junctions the actor
; continues on its lane. HTAB follows MONHND's left=4/right=8, despite
; MAZHND's introductory comment claiming the reverse (the top-left 0Ah
; junction must lead DOWN and RIGHT). The reachability test checks this.
; in SI actor; out AL legal direction mask, CF junction; other regs scratch.
pm_allowed:
    mov bl, [pm_x+si]
    mov bh, [pm_y+si]
    mov di, 9
.v:
    cmp bh, [pm_vtable+di]
    je .vfound
    dec di
    jns .v
    mov al, PM_UP|PM_DOWN
    clc
    ret
.vfound:
    mov dx, di
    mov di, 9
.h:
    cmp bl, [pm_htable+di]
    je .junction
    dec di
    jns .h
    mov al, PM_LEFT|PM_RIGHT
    clc
    ret
.junction:
    mov ax, dx
    mov cx, 10
    mul cx
    add di, ax
    mov al, [pm_junctions+di]
    ; Keep live ghosts out of the house: the two junctions above its gate
    ; prohibit UP. Eyes target the corridor center, then enter explicitly.
    cmp si, 4
    je .done
    cmp byte [pm_y+si], 100
    jne .done
    cmp byte [pm_x+si], 118
    je .gate
    cmp byte [pm_x+si], 130
    jne .done
.gate:
    and al, 0FEh
.done:
    stc
    ret

pm_move:
    mov al, [pm_dir+si]
    cmp al, PM_UP
    jne .down
    sub byte [pm_y+si], 2
    ret
.down:
    cmp al, PM_DOWN
    jne .left
    add byte [pm_y+si], 2
    ret
.left:
    cmp al, PM_LEFT
    jne .right
    dec byte [pm_x+si]
    cmp byte [pm_x+si], 48
    jae .out
    mov byte [pm_x+si], 200
    ret
.right:
    inc byte [pm_x+si]
    cmp byte [pm_x+si], 200
    jbe .out
    mov byte [pm_x+si], 48
.out:
    ret

pm_reverse:
    xor bx, bx
.loop:
    cmp byte [pm_release+bx], 0
    jne .next
    cmp byte [pm_eyes+bx], 0
    jne .next
    xor ax, ax
    mov al, [pm_dir+bx]
    mov di, ax
    mov al, [pm_opposite+di]
    mov [pm_dir+bx], al
.next:
    inc bx
    cmp bx, 4
    jb .loop
    ret

pm_ghost:
    cmp byte [pm_release+si], 0
    je .house
    dec byte [pm_release+si]
    ret
.house:
    cmp byte [pm_y+si], 100
    jb .normal
    cmp byte [pm_y+si], 116
    ja .normal
    cmp byte [pm_x+si], 114
    jb .normal
    cmp byte [pm_x+si], 134
    ja .normal
    ; Only a released house resident or eyes arriving at 124,100 may
    ; use this lane. Ordinary ghosts at y=100 stay in the corridor.
    cmp byte [pm_y+si], 100
    jne .leave
    cmp byte [pm_eyes+si], 0
    je .normal
    cmp byte [pm_x+si], 124
    jne .normal
    mov byte [pm_eyes+si], 0
    mov byte [pm_y+si], 116
    mov byte [pm_release+si], 18
    ret
.leave:
    mov byte [pm_dir+si], PM_RIGHT
    cmp byte [pm_x+si], 124
    jb .housemove
    mov byte [pm_dir+si], PM_LEFT
    ja .housemove
    mov byte [pm_dir+si], PM_UP
    ; The door is BETWEEN junctions. Turn into its horizontal corridor
    ; immediately at y=100; MAZHND's non-junction path cannot choose for us.
.housemove:
    call pm_move
    cmp byte [pm_y+si], 100
    jne .out
    mov byte [pm_dir+si], PM_LEFT
    ret
.normal:
    ; Frightened and tunnel ghosts run at half speed; eyes run every tick.
    cmp byte [pm_eyes+si], 0
    jne .choose
    cmp word [pm_fright], 0
    jne .slow
    cmp byte [pm_y+si], 116
    jne .choose
    cmp byte [pm_x+si], 82
    jb .slow
    cmp byte [pm_x+si], 166
    jbe .choose
.slow:
    test word [pm_frames], 1
    jnz .out
.choose:
    call pm_allowed
    jnc .move
    mov [pm_choices], al
    xor bx, bx
    mov bl, [pm_dir+si]
    mov al, [pm_opposite+bx]
    not al
    and al, [pm_choices]
    jz .all
    mov [pm_choices], al
.all:
    mov al, [pm_x+4]
    mov [pm_tx], al
    mov al, [pm_y+4]
    mov [pm_ty], al
    cmp byte [pm_eyes+si], 0
    je .corner
    mov byte [pm_tx], 124
    mov byte [pm_ty], 100
    jmp short .target
.corner:
    cmp byte [pm_chase], 0
    jne .target
    mov bx, si
    shl bx, 1
    mov al, [pm_homehv+bx]
    mov [pm_tx], al
    mov al, [pm_homehv+bx+1]
    mov [pm_ty], al
.target:
    mov word [pm_best], 0FFFFh
    mov byte [pm_candidate], 1
.try:
    mov al, [pm_candidate]
    test al, [pm_choices]
    jz .next
    ; Distance from the next step to the target. Vertical source pixels
    ; count half as much as horizontal color clocks.
    xor ax, ax
    xor bx, bx
    mov al, [pm_x+si]
    mov bl, [pm_y+si]
    cmp byte [pm_candidate], PM_UP
    jne .d
    sub bx, 2
.d:
    cmp byte [pm_candidate], PM_DOWN
    jne .l
    add bx, 2
.l:
    cmp byte [pm_candidate], PM_LEFT
    jne .r
    dec ax
.r:
    cmp byte [pm_candidate], PM_RIGHT
    jne .dist
    inc ax
.dist:
    xor dx, dx
    mov dl, [pm_tx]
    sub ax, dx
    jns .xabs
    neg ax
.xabs:
    mov dl, [pm_ty]
    sub bx, dx
    jns .yabs
    neg bx
.yabs:
    shr bx, 1
    add ax, bx
    cmp byte [pm_eyes+si], 0
    jne .best
    cmp word [pm_fright], 0
    je .best
    neg ax                          ; maximize distance during flight
    add ax, 512
.best:
    cmp ax, [pm_best]
    jae .next
    mov [pm_best], ax
    mov al, [pm_candidate]
    mov [pm_dir+si], al
.next:
    shl byte [pm_candidate], 1
    cmp byte [pm_candidate], 16
    jb .try
.move:
    call pm_move
.out:
    ret

pm_eat:
    ; Dot centers: h=46+4*column, v=36+8*row. Both axes must align.
    xor ax, ax
    mov al, [pm_x+4]
    sub ax, 46
    test al, 3
    jnz .fruit
    shr ax, 1
    shr ax, 1
    mov bx, ax
    xor ax, ax
    mov al, [pm_y+4]
    sub ax, 36
    test al, 7
    jnz .fruit
    mov cl, 3
    shr ax, cl
    mov cx, 40
    mul cx
    add bx, ax
    cmp bx, 880
    jae .fruit
    mov al, [pm_map+bx]
    cmp al, 1
    je .dot
    cmp al, 2
    jne .fruit
    mov byte [pm_map+bx], 0
    mov ax, 50
    call pm_addscore
    mov bx, [pm_level]
    cmp bx, 14
    jbe .blue
    mov bx, 14
.blue:
    xor ax, ax
    mov al, [pm_blutim+bx]
    ; FLITEC decrements once per 60Hz VBI: convert to 18Hz ticks.
    mov cx, 3
    mul cx
    mov cx, 10
    div cx
    mov [pm_fright], ax
    mov word [pm_chain], 200
    call pm_reverse
    mov ax, 900
    call pm_tone
    jmp short .count
.dot:
    mov byte [pm_map+bx], 0
    mov ax, 10
    call pm_addscore
    mov ax, 500
    test word [pm_eaten], 1
    jz .tone
    mov ax, 650
.tone:
    call pm_tone
.count:
    inc word [pm_eaten]
    cmp word [pm_eaten], 260
    jne .spawn
    mov byte [pm_mode], PM_LEVEL
    mov word [pm_hold], 36
    or byte [pm_status_dirty], PM_SD_MSG
    ret
.spawn:
    cmp word [pm_eaten], 80
    je .spawnfruit
    cmp word [pm_eaten], 160
    jne .fruit
.spawnfruit:
    inc byte [pm_fruit_count]
    mov word [pm_fruit], 182
    call pm_mark_fruit
.fruit:
    cmp word [pm_fruit], 0
    je .out
    cmp byte [pm_y+4], 132
    jne .out
    cmp byte [pm_x+4], 120
    jb .out
    cmp byte [pm_x+4], 128
    ja .out
    mov word [pm_fruit], 0
    call pm_mark_fruit
    mov bx, [pm_level]
    cmp bx, 12
    jbe .points
    mov bx, 12
.points:
    shl bx, 1
    mov ax, [pm_fruit_points+bx]
    call pm_addscore
    mov ax, 1200
    call pm_tone
.out:
    ret

pm_collide:
    xor si, si
.loop:
    cmp byte [pm_eyes+si], 0
    jne .next
    cmp byte [pm_release+si], 0
    jne .next
    mov al, [pm_x+si]
    sub al, [pm_x+4]
    jns .x
    neg al
.x:
    cmp al, 5
    jae .next
    mov al, [pm_y+si]
    sub al, [pm_y+4]
    jns .y
    neg al
.y:
    cmp al, 8
    jae .next
    cmp word [pm_fright], 0
    je .die
    mov byte [pm_eyes+si], 1
    mov ax, [pm_chain]
    call pm_addscore
    cmp word [pm_chain], 1600
    jae .sound
    shl word [pm_chain], 1
.sound:
    mov ax, 1400
    call pm_tone
.next:
    inc si
    cmp si, 4
    jb .loop
    ret
.die:
    dec byte [pm_lives]
    mov byte [pm_mode], PM_DEAD
    mov word [pm_hold], 27
    cmp byte [pm_lives], 0
    jne .dead
    mov byte [pm_mode], PM_OVER
.dead:
    or byte [pm_status_dirty], PM_SD_MSG | PM_SD_LIVES
    mov ax, 180
    call pm_tone
    ret

; Score is unsigned 32-bit. One bonus life at 10,000 (PSCORE).
pm_addscore:
    add [pm_score], ax
    adc word [pm_score+2], 0
    or byte [pm_status_dirty], PM_SD_SCORE
    cmp byte [pm_bonus], 0
    jne .out
    cmp word [pm_score+2], 0
    jne .bonus
    cmp word [pm_score], 10000
    jb .out
.bonus:
    mov byte [pm_bonus], 1
    inc byte [pm_lives]
    or byte [pm_status_dirty], PM_SD_LIVES
.out:
    ret
pm_tone:
    push cx
    push dx
    mov cx, 1
    mov dl, 40h
    call OSAPI_SND_TONE
    pop dx
    pop cx
    ret

; Rendering: each tile is 8x8 destination pixels, 4 bytes per source row.
; Dirty bands contain min/max tile columns. 40/0 denotes a clean band.
pm_mark_actors:
    xor si, si
.loop:
    xor ax, ax
    xor dx, dx
    mov al, [pm_x+si]
    mov dl, [pm_y+si]
    call pm_mark
    inc si
    cmp si, 5
    jb .loop
    ret
pm_mark_fruit:
    mov ax, 124
    mov dx, 132
    call pm_mark
    ret
; AX=HPOS, DX=VPOS. Actor covers center +/-4 clocks, +/-6 source rows.
pm_mark:
    cmp ax, 48
    jb .out
    cmp ax, 200
    ja .out
    cmp dx, 44
    jb .out
    cmp dx, 196
    ja .out
    sub ax, 48
    shr ax, 1
    shr ax, 1
    mov bx, ax
    add ax, 2
    cmp ax, 39
    jbe .right
    mov ax, 39
.right:
    sub dx, 38
    mov cl, 3
    shr dx, cl
    mov di, dx
    add dx, 2
    cmp dx, 21
    jbe .rows
    mov dx, 21
.rows:
    cmp bl, [pm_min+di]
    jae .max
    mov [pm_min+di], bl
.max:
    cmp al, [pm_max+di]
    jbe .next
    mov [pm_max+di], al
.next:
    inc di
    cmp di, dx
    jbe .rows
.out:
    ret

pm_track:
    cmp byte [pm_tgt], PM_TGT_WIN
    je .win
    mov byte [pm_bpp], 4         ; the foreign surface: 320x200 either way, the
    mov byte [pm_half], 0        ; board whole, and no window to read it off
    xor ax, ax
    mov [pm_cx], ax
    mov [pm_cy], ax
    mov [pm_ox], ax
    mov ax, [pm_fsi+FSI_W]
    mov [pm_cw], ax
    mov ax, [pm_fsi+FSI_H]
    mov [pm_ch], ax
    mov word [pm_oy], PM_FS_TOP
    ret
.win:
    mov bx, [pm_win]
    call OSAPI_WM_DISPLAY
    mov [pm_bpp], dh
    mov bx, [pm_win]
    call OSAPI_WM_CONTENT
    mov [pm_cx], ax
    mov [pm_cy], dx
    call OSAPI_WM_GEOM
    mov [pm_cw], cx
    mov [pm_ch], dx
    mov byte [pm_half], 0
    mov ax, 176
    cmp dx, 198
    jae .height
    mov byte [pm_half], 1
    mov ax, 88
.height:
    sub dx, ax
    shr dx, 1
    add dx, [pm_cy]
    mov [pm_oy], dx
    sub cx, 320
    shr cx, 1
    add cx, [pm_cx]
    and cx, 0FFF8h                 ; BLIT1 requires byte-aligned columns
    mov [pm_ox], cx
    ret

pm_paint_body:
    cmp byte [pm_hired], 0
    jne .draw
    mov ax, pm_worker
    mov bx, [pm_win]
    call OSAPI_TASK_SPAWN
    jc .draw                        ; next paint retries a full task table
    mov byte [pm_hired], 1
.draw:
    mov byte [pm_full], 1
    mov byte [pm_status_dirty], PM_SD_ALL
    call pm_redraw
    cmp byte [pm_abon], 0
    je .out
    mov bx, [pm_win]
    mov si, pm_ablines
    call os88ui_about
.out:
    ret

pm_redraw:
    push es
    cmp byte [pm_tgt], PM_TGT_WIN
    jne .own                     ; a foreign framebuffer is ours whole: there
    mov bx, [pm_win]             ; is no window above it to clip against, and
    call OSAPI_WM_CLIP_SET       ; wm_clip_set is a desktop question anyway
    jc .out
.own:
    call pm_track
    cmp byte [pm_full], 0
    je .compose
    mov byte [pm_full], 0
    mov byte [pm_score_prev], 0  ; the strip is gone with the rest of it
    cmp byte [pm_tgt], PM_TGT_WIN
    jne .marked                  ; the mode set already cleared, and every band
                                 ; below is about to be written again
    xor al, al
    call OSAPI_SET_COLOR
    mov ax, [pm_cx]
    mov bx, [pm_cy]
    mov cx, ax
    add cx, [pm_cw]
    dec cx
    mov dx, bx
    add dx, [pm_ch]
    dec dx
    call OSAPI_GFX_FILL
.marked:
    xor bx, bx
.all:
    mov byte [pm_min+bx], 0
    mov byte [pm_max+bx], 39
    inc bx
    cmp bx, 22
    jb .all
    mov byte [pm_status_dirty], PM_SD_ALL
.compose:
    push ds
    pop es
    cld
    xor bp, bp                       ; band index, NOT a pointer
.band:
    mov bx, bp
    xor ax, ax
    mov al, [pm_min+bx]
    cmp al, [pm_max+bx]
    ja .nextband
    mov [pm_col], ax
    shl bx, 1                    ; the band's two bases, once a band: both were
    mov ax, [pm_bandmap+bx]      ; a 16-bit MUL per TILE, and an 8088 charges
    mov [pm_mapb], ax            ; ~124 clocks for one (PERFORMANCE.md Part 2)
    mov ax, [pm_bandcv+bx]
    mov [pm_cvb], ax
.tile:
    mov bx, [pm_col]
    add bx, [pm_mapb]
    xor ax, ax
    mov al, [pm_map+bx]
    shl ax, 1
    mov bx, ax
    mov si, [pm_tile_offsets+bx]
    add si, pm_tiles
    mov di, [pm_col]
    shl di, 1
    shl di, 1
    add di, [pm_cvb]
    add di, pm_canvas
%rep 8                           ; unrolled: the row counter was 19 clocks a
    movsw                        ; row on a tile that copies four bytes
    movsw
    add di, 156
%endrep
    inc word [pm_col]
    mov bx, bp
    xor ax, ax
    mov al, [pm_max+bx]
    cmp [pm_col], ax
    jbe .tile
.nextband:
    inc bp
    cmp bp, 22
    jb .band
    ; Sprite composition touches RAM only. All actors are marked before
    ; each simulation step and after it; full paints mark every band.
    call pm_sprites
    xor bp, bp
.blit:
    mov di, bp
    xor ax, ax
    mov al, [pm_min+di]
    cmp al, [pm_max+di]
    ja .clean
    mov bx, bp
    shl bx, 1
    mov si, [pm_bandcv+bx]
    add si, pm_canvas
    mov bx, ax
    shl bx, 1
    shl bx, 1
    add si, bx
    xor cx, cx
    mov cl, [pm_max+di]
    sub cx, ax
    inc cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [pm_ox]
    mov bx, bp
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov dx, 8
    push bp
    mov bp, 160
    cmp byte [pm_half], 0
    je .draw
    shr bx, 1
    mov dx, 4
    mov bp, 320
.draw:
    add bx, [pm_oy]
    call pm_blit
    pop bp
.clean:
    mov byte [pm_min+di], 40
    mov byte [pm_max+di], 0
    inc bp
    cmp bp, 22
    jb .blit
    call pm_status
.out:
    pop es
    ret

; The mono backend receives final packed bits. Every canvas byte represents
; two identical pixels, so four table lookups compose eight output pixels.
; This avoids the software BLIT4 renderer's per-pixel color/clip work on XT.
pm_blit:
    cmp byte [pm_tgt], PM_TGT_V13
    je pm_blit13
    cmp byte [pm_tgt], PM_TGT_C4
    je pm_blitc4
    cmp byte [pm_bpp], 1
    jne .packed
    call pm_packmono
    push si
    push bp
    mov si, pm_monobuf
    mov bp, [pm_mcols]
    push ax
    mov ax, CWHITE
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    call OSAPI_GFX_BLIT1
    pop bp
    pop si
    jnc .out
.packed:                            ; kern_small / unsupported BLIT1 fallback
    call OSAPI_GFX_BLIT4
.out:
    ret

pm_packmono:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, cx
    shr ax, 1
    sub bp, ax                     ; source skip after one converted row
    mov ax, cx
    mov cl, 3
    shr ax, cl
    mov [pm_mcols], ax
    and bx, 1
    mov cl, 4
    shl bx, cl
    add bx, pm_mono_pairs
    mov di, pm_monobuf
.row:
    mov cx, [pm_mcols]
.byte:
    xor ah, ah
%rep 4
    lodsb
    and al, 15
    xlat
    shl ah, 1
    shl ah, 1
    or ah, al
%endrep
    mov al, ah
    stosb
    loop .byte
    add si, bp
    xor bx, 16                     ; switch checkerboard row polarity
    dec dx
    jnz .row
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; ===========================================================================
; THE FOREIGN-MODE BACKENDS (SPEC.md 89.3)
;
; Everything below runs only inside a SPEC.md 53 bracket that set a mode, and
; nothing above it changes. The canvas, the tile composer, the sprite composer
; and the dirty bands are the same on every path; what differs is the twelve
; instructions that put a band on the glass.
; ===========================================================================

; ---------------------------------------------------------------------------
; pm_blit13 - one band into 320x200x256 (FSXM_VGA13)
; in:  DS:SI = the band's first canvas byte, BP = the canvas stride, AX = x,
;      BX = y, CX = width in pixels, DX = rows. All registers preserved.
;
; A canvas byte is TWO IDENTICAL 4bpp pixels (89.2) and a mode 13h byte is one
; pixel, so the whole conversion is `mov ah, al` - and pm_pal13 has put colour
; c in DAC entry c|c<<4, which is exactly what a canvas byte holds. No
; transpose, no run scan, no clip test, no per-call arrival: ~18 clocks a
; pixel where the desktop's own OSAPI_GFX_BLIT4 measures ~235 on this art,
; because that primitive is priced per COLOUR CHANGE and a maze is nothing
; else (89.3.4).
; ---------------------------------------------------------------------------
pm_blit13:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [pm_bw], cx
    mov [pm_bh], dx
    mov [pm_bsk], bp
    mov [pm_bx], ax              ; x goes to MEMORY: the `mul` below writes DX
    mov ax, bx                   ; and a register would not survive it
    mov bx, 320
    mul bx
    add ax, [pm_bx]
    mov di, ax                   ; ES:DI = the first destination byte
    mov es, [pm_fseg]
    mov cx, [pm_bw]
    shr cx, 1                    ; canvas bytes a row
    mov ax, [pm_bsk]
    sub ax, cx
    mov [pm_bsk], ax
    shr cx, 1
    shr cx, 1                    ; ...in groups of four: a tile column is eight
    mov [pm_bc], cx              ; pixels, so this always divides (89.2)
    mov ax, 320
    sub ax, [pm_bw]
    mov [pm_bdk], ax
    cld
.row:
    mov cx, [pm_bc]
.grp:
%rep 4
    lodsb
    mov ah, al
    stosw
%endrep
    loop .grp
    add si, [pm_bsk]
    add di, [pm_bdk]
    dec word [pm_bh]
    jnz .row
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; pm_blitc4 - one band into 320x200x4 (FSXM_CGA320)
; Same contract as pm_blit13.
;
; Two canvas bytes are four screen pixels in one byte. pm_c4hi/pm_c4lo hold
; each byte's contribution already shifted into place, so packing is two
; lookups and an OR - and the CGA's two banks (FSI_BANKS = 2) mean a row
; alternates between +FSI_BSTEP and +FSI_STRIDE-FSI_BSTEP rather than adding a
; stride, which is why the row base is carried rather than computed.
; ---------------------------------------------------------------------------
pm_blitc4:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [pm_bw], cx
    mov [pm_bh], dx
    mov [pm_by], bx
    mov [pm_bsp], bp
    mov [pm_bx], ax
    mov ax, bx
    call pm_c4off                ; DI = that row's first byte
    mov ax, [pm_bx]
    shr ax, 1
    shr ax, 1
    add di, ax
    mov es, [pm_fseg]
    mov ax, [pm_bw]
    shr ax, 1
    shr ax, 1
    mov [pm_bc], ax              ; screen bytes a row
    mov ax, [pm_bsp]
    mov dx, [pm_bw]
    shr dx, 1
    sub ax, dx
    mov [pm_bsk], ax             ; canvas bytes to skip after one
    xor bh, bh
    cld
.row:
    mov cx, [pm_bc]
    mov [pm_bdi], di
.byte:
    mov bl, [si]
    mov al, [bx+pm_c4hi]
    mov bl, [si+1]
    or al, [bx+pm_c4lo]
    add si, 2
    stosb
    loop .byte
    add si, [pm_bsk]
    mov di, [pm_bdi]
    test byte [pm_by], 1
    jnz .odd
    add di, [pm_fbstep]          ; even row -> the odd bank, same line
    jmp short .next
.odd:
    add di, [pm_fstride]         ; odd row -> the even bank, one line down
    sub di, [pm_fbstep]
.next:
    inc word [pm_by]
    dec word [pm_bh]
    jnz .row
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX = a screen row -> DI = its first byte in the CGA's interleaved banks.
pm_c4off:
    push ax
    push dx
    mov dx, ax
    shr ax, 1
    push dx
    mov dx, [pm_fstride]
    mul dx
    mov di, ax
    pop dx
    test dl, 1
    jz .out
    add di, [pm_fbstep]
.out:
    pop dx
    pop ax
    ret

; Build the two packing tables. Every canvas byte is c|c<<4, so only sixteen
; entries carry meaning; indexing by the whole byte is what makes the inner
; loop two lookups with no shift in it.
pm_c4build:
    push ax
    push bx
    push cx
    xor bx, bx
.b:
    mov al, bl
    mov cl, 4
    shr al, cl
    push bx
    xor ah, ah
    mov bx, ax
    mov al, [bx+pm_c4]
    pop bx
    mov ah, al
    shl ah, 1
    shl ah, 1
    or al, ah                    ; the byte's two pixels, 2bpp
    mov [bx+pm_c4lo], al
    mov cl, 4
    shl al, cl
    mov [bx+pm_c4hi], al
    inc bx
    cmp bx, 256
    jb .b
    pop cx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; pm_pal13 - the desktop's sixteen colours into DAC entries 0x11*c
;
; This is what makes pm_blit13's `mov ah, al` legal: a canvas byte is c|c<<4,
; so if entry c|c<<4 holds colour c the byte IS the pixel. SPEC.md 53.7 hands
; the DAC to the app inside a foreign mode, and the exit mode set puts it back.
; ---------------------------------------------------------------------------
pm_pal13:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx
.c:
    mov dx, 0x3C8
    mov al, bl
    mov ah, bl
    mov cl, 4
    shl ah, cl
    or al, ah
    out dx, al
    inc dx
    mov si, bx
    add si, bx
    add si, bx
    add si, pm_dac
    mov al, [si]
    out dx, al
    mov al, [si+1]
    out dx, al
    mov al, [si+2]
    out dx, al
    inc bx
    cmp bx, 16
    jb .c
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; CGA palette 1 at high intensity - black, cyan, magenta, white - with a black
; border. The BIOS mode set leaves it on palette 0 (green/red/yellow), which is
; not a maze.
;
; BOTH WAYS ROUND, and the second is for the EGA. 3D9h is the CGA's colour
; select and an EGA does not decode it at all - that card keeps its palette in
; the attribute controller - so the direct write alone would leave an EGA on
; green and red. int 10h AH=0Bh is not a mode set (SPEC.md 53.7 forbids only
; those) and both BIOSes implement it, so the call is asked first and the port
; write, which carries the intensity bit a real CGA needs, lands over it on the
; card that has one.
pm_palc4:
    push ax
    push bx
    push dx
    mov ax, 0x0B01               ; BH=01 select palette, BL=01 palette 1
    mov bx, 0x0101
    int 0x10
    mov dx, 0x3D9
    mov al, 0x30                 ; palette 1, high intensity, black border
    out dx, al
    pop dx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; pm_fstext - a NUL-terminated string in opaque 8x8 cells, straight into the
;             foreign framebuffer
; in:  SI = the string, CX = x (a multiple of 8), DX = y. All registers kept.
;
; OSAPI_FONT_RUN is a drawing slot and renders DESKTOP geometry, so after
; OSAPI_FSX_MODE it is off limits (SPEC.md 53.7). What is legal is
; OSAPI_FONT_GLYPHS - the bitmaps themselves - which is the whole reason that
; slot exists, and it means the strip is the same typeface it always was.
; ---------------------------------------------------------------------------
pm_fstext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [pm_penx], cx
    mov [pm_peny], dx
    cld
.char:
    mov al, [si]
    inc si
    or al, al
    jz .done
    push si
    call pm_fsglyph
    call pm_fscell
    pop si
    add word [pm_penx], 8
    jmp short .char
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

; AL = a character -> its eight rows in pm_gbuf. Anything outside the table's
; range letters as a space, which is what a font run does with it too.
pm_fsglyph:
    push ax
    push bx
    push cx
    push di
    push es
    cmp al, [pm_gfirst]
    jb .space
    cmp al, [pm_glast]
    jbe .have
.space:
    mov al, ' '
.have:
    sub al, [pm_gfirst]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [pm_gtab]
    mov es, [pm_gseg]            ; the table is NOT in KERNEL_SEG (SPEC.md 20.8)
    mov di, pm_gbuf
    mov cx, 8
.copy:
    mov al, [es:bx]
    mov [di], al
    inc bx
    inc di
    loop .copy
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; pm_gbuf at [pm_penx],[pm_peny], in whichever foreign mode is up.
pm_fscell:
    cmp byte [pm_tgt], PM_TGT_C4
    je pm_fscellc4
    ; ...and fall through to the chunky one

pm_fscell13:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [pm_peny]
    mov dx, 320
    mul dx
    add ax, [pm_penx]
    mov di, ax
    mov es, [pm_fseg]
    mov si, pm_gbuf
    mov byte [pm_rowc], 8
    cld
.row:
    lodsb
    mov ah, al
    and ah, 0Fh
    mov cl, 4
    shr al, cl
    mov bl, al                   ; the high nibble is the left four pixels
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov dx, [bx+pm_nib13]
    mov [es:di], dx
    mov dx, [bx+pm_nib13+2]
    mov [es:di+2], dx
    mov bl, ah
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov dx, [bx+pm_nib13]
    mov [es:di+4], dx
    mov dx, [bx+pm_nib13+2]
    mov [es:di+6], dx
    add di, 320
    dec byte [pm_rowc]
    jnz .row
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pm_fscellc4:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [pm_peny]
    mov [pm_bdi], ax             ; the pen's row, carried down the cell
    mov si, pm_gbuf
    mov es, [pm_fseg]
    mov byte [pm_rowc], 8
    cld
.row:
    mov ax, [pm_bdi]
    call pm_c4off
    mov ax, [pm_penx]
    shr ax, 1
    shr ax, 1
    add di, ax
    lodsb
    mov ah, al
    and ah, 0Fh
    mov cl, 4
    shr al, cl
    mov bl, al
    xor bh, bh
    mov dl, [bx+pm_nibc4]
    mov [es:di], dl
    mov bl, ah
    xor bh, bh
    mov dl, [bx+pm_nibc4]
    mov [es:di+1], dl
    inc word [pm_bdi]
    dec byte [pm_rowc]
    jnz .row
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

pm_sprites:
    xor si, si
.actor:
    mov bx, si
    shl bx, 1
    mov ax, pm_pacdot
    cmp si, 4
    je .pac
    xor bx, bx
    mov bl, [pm_dir+si]
    shl bx, 1
    mov ax, [pm_ghost_shapes+bx]
    cmp word [pm_fright], 0
    je .eyes
    mov ax, pm_monsfl
.eyes:
    cmp byte [pm_eyes+si], 0
    je .shape
    mov ax, pm_monsey
    jmp short .shape
.pac:
    test word [pm_frames], 2
    jz .shape
    xor bx, bx
    mov bl, [pm_dir+4]
    shl bx, 1
    mov ax, [pm_pac_shapes+bx]
.shape:
    mov [pm_shape], ax
    mov al, [pm_colors+si]
    cmp si, 4
    je .color
    cmp word [pm_fright], 0
    je .color
    mov al, 7
    cmp word [pm_fright], 36
    ja .color
    test word [pm_frames], 4
    jz .color
    mov al, 15
.color:
    mov [pm_ink], al
    xor ax, ax
    xor dx, dx
    mov al, [pm_x+si]
    mov dl, [pm_y+si]
    push si
    call pm_sprite
    pop si
    inc si
    cmp si, 5
    jb .actor
    cmp word [pm_fruit], 0
    je .out
    mov word [pm_shape], pm_fruit_shape
    mov byte [pm_ink], 12
    mov ax, 124
    mov dx, 132
    call pm_sprite
.out:
    ret

; AX/DX center in Atari coordinates, pm_shape 8x10 source, doubled X.
; Entire sprite fits the canvas even at the wrap boundary (h=48..200).
pm_sprite:
    cmp ax, 48
    jb .out
    cmp ax, 200
    ja .out
    cmp dx, 44
    jb .out
    cmp dx, 196
    ja .out
    sub ax, 48
    mov bx, ax
    sub dx, 37
    mov ax, dx
    mov cx, 160
    mul cx
    mov di, pm_canvas
    add di, ax
    add di, bx
    mov si, [pm_shape]
    mov dl, [pm_ink]
    mov dh, dl
    mov cl, 4
    shl dl, cl
    or dl, dh
    mov dh, 10
.row:
    lodsb
%rep 8
    PM_SPX                       ; unrolled: `loop` is 17 clocks an 8088 spends
%endrep                          ; on nothing, eight times a sprite row
    add di, 152
    dec dh
    jnz .row
.out:
    ret

; ---------------------------------------------------------------------------
; pm_status - the strip, one field at a time (89.3.3)
;
; It used to be one dirty bit over four fields, so eating a dot lettered all
; 74 cells: ~67 ms on a 4.77MHz 8088 at ~0.9 ms a cell, more than a whole
; frame's budget, for two digits that moved. Each field now carries its own
; bit, and the score - the one that changes every few frames - is drawn from
; the first digit that actually differs, which for a 10-point dot is two.
; ---------------------------------------------------------------------------
pm_status:
    mov al, [pm_status_dirty]
    or al, al
    je .out
    mov [pm_sdbits], al
    mov byte [pm_status_dirty], 0
    mov ax, [pm_ox]              ; where the strip lives: inside the content on
    mov [pm_sx], ax              ; the desktop, at the screen's own edges in a
    mov dx, [pm_cy]              ; foreign mode
    add dx, 3
    mov bx, [pm_cy]
    add bx, [pm_ch]
    sub bx, 11
    cmp byte [pm_tgt], PM_TGT_WIN
    je .rows
    mov word [pm_sx], 0
    mov dx, 4
    mov bx, PM_FS_MSG
.rows:
    mov [pm_stop], dx
    mov [pm_sbot], bx

    test byte [pm_sdbits], PM_SD_SCORE
    jz .lives
    mov ax, [pm_score]
    mov dx, [pm_score+2]
    mov di, pm_score_text+6
    mov cx, 10
    call pm_decimal
    xor bx, bx                   ; the first cell that moved
.diff:
    mov al, [pm_score_text+bx]
    cmp al, [pm_score_prev+bx]
    jne .from
    or al, al
    jz .lives                    ; identical: nothing to letter
    inc bx
    jmp short .diff
.from:
    push bx
.copy:
    mov al, [pm_score_text+bx]
    mov [pm_score_prev+bx], al
    or al, al
    jz .copied
    inc bx
    jmp short .copy
.copied:
    pop bx
    mov cx, bx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [pm_sx]
    mov si, pm_score_text
    add si, bx
    mov dx, [pm_stop]
    call pm_run
.lives:
    test byte [pm_sdbits], PM_SD_LIVES
    jz .flevel
    mov al, [pm_lives]
    add al, '0'
    mov [pm_lives_text+6], al
    mov si, pm_lives_text
    mov cx, [pm_sx]
    add cx, 144
    mov dx, [pm_stop]
    call pm_run
.flevel:
    test byte [pm_sdbits], PM_SD_LEVEL
    jz .msg
    mov ax, [pm_level]
    inc ax
    xor dx, dx
    mov di, pm_level_text+6
    mov cx, 5
    call pm_decimal
    mov si, pm_level_text
    mov cx, [pm_sx]
    add cx, 232
    mov dx, [pm_stop]
    call pm_run
.msg:
    test byte [pm_sdbits], PM_SD_MSG
    jz .out
    mov bx, pm_s_play
    cmp byte [pm_mode], PM_READY
    jne .dead
    mov bx, pm_s_ready
.dead:
    cmp byte [pm_mode], PM_DEAD
    jne .level
    mov bx, pm_s_dead
.level:
    cmp byte [pm_mode], PM_LEVEL
    jne .over
    mov bx, pm_s_level
.over:
    cmp byte [pm_mode], PM_OVER
    jne .pause
    mov bx, pm_s_over
.pause:
    cmp byte [pm_pause], 0
    je .message
    mov bx, pm_s_pause
.message:
    cmp byte [pm_hired], 0
    jne .available
    mov bx, pm_s_wait
.available:
    mov si, bx
    mov cx, [pm_sx]
    mov dx, [pm_sbot]
    call pm_run
.out:
    ret

; One run of text, wherever this frame is going.
; in: SI = string, CX = x, DX = y. Preserves what its two backends preserve.
pm_run:
    cmp byte [pm_tgt], PM_TGT_WIN
    jne .foreign
    mov ax, CWHITE
    call OSAPI_FONT_RUN
    ret
.foreign:
    call pm_fstext
    ret

; DX:AX -> CX decimal digits at DS:DI; divide the high word first so no
; 8086 DIV overflow occurs above 655,359. Leading zeroes are intentional.
pm_decimal:
    push bp
    mov bp, cx
    add di, cx
    dec di
    mov bx, 10
.loop:
    push ax
    mov ax, dx
    xor dx, dx
    div bx
    mov si, ax
    pop ax
    div bx
    add dl, '0'
    mov [di], dl
    mov dx, si
    dec di
    dec bp
    jnz .loop
    pop bp
    ret

pm_tpl: dw 144, 32, 338, 222, pm_title, pm_paint, pm_key, pm_dismiss
    OS88_PREFER pm_pref, 338,222, 338,222, 338,140
    OS88_MENUSET pm_menus, pm_menu_name, pm_command
        OS88_MENU pm_game_name, pm_items, 3
    OS88_MENUSET_END pm_menus
pm_title: db 'Pac-Man',0
pm_menu_name: db 'Pac-Man',0
pm_game_name: db 'Game',0
pm_items: dw pm_new_text,pm_pause_text,pm_full_text
pm_new_text: db 'New Game',0
pm_pause_text: db 'Pause',0
pm_full_text: db 'Full Screen',0
pm_score_text: db 'SCORE 0000000000',0
pm_lives_text: db 'LIVES 3',0
pm_level_text: db 'LEVEL 00001',0
; Every footer is 40 cells, so an opaque run also removes the previous one.
pm_s_play: db 'ARROWS/WASD MOVE  P PAUSE  N NEW  F FULL',0
pm_s_wait: db 'NO TASK SLOT - CLOSE ANOTHER APP        ',0
pm_s_ready: db 'READY!                                  ',0
pm_s_dead: db 'CAUGHT!                                 ',0
pm_s_level: db 'MAZE CLEAR!                             ',0
pm_s_over: db 'GAME OVER - N FOR A NEW GAME            ',0
pm_s_pause: db 'PAUSED - P OR SPACE TO RESUME           ',0
pm_ablines: dw pm_ab1,pm_ab2,pm_ab3,pm_ab4,pm_ab5,0
pm_ab1: db 'Pac-Man for os8088',0
pm_ab2: db 'Roklan / Atari disk version, 1982',0
pm_ab3: db 'Original maze, sprites and scoring',0
pm_ab4: db 'Native 8086 adaptation',0
pm_ab5: db 'Arrows/WASD move. P pauses. N starts.',0
align 32, db 0

; A band's two bases, indexed by band (89.3.5): 22 words each, against a
; 16-bit MUL an 8088 charges ~124 clocks for.
pm_bandmap:
%assign pmb 0
%rep 22
    dw pmb * 40
%assign pmb pmb+1
%endrep
pm_bandcv:
%assign pmb 0
%rep 22
    dw pmb * 1280
%assign pmb pmb+1
%endrep

; The desktop's sixteen colours as 6-bit DAC triples, for FSXM_VGA13.
pm_dac:
    db  0, 0, 0,   0, 0,42,   0,42, 0,   0,42,42
    db 42, 0, 0,  42, 0,42,  42,21, 0,  42,42,42
    db 21,21,21,  21,21,63,  21,63,21,  21,63,63
    db 63,21,21,  63,21,63,  63,63,21,  63,63,63

; ...and as CGA palette 1 at high intensity - black, cyan, magenta, white.
; Walls (9) are cyan, dots and the player (14/15) white, the ghosts magenta;
; a frightened ghost (7) goes white, which is the flash it already has.
pm_c4:
    db 0,1,1,1,2,2,1,3,1,1,3,2,2,2,3,3

; Four pixels of a glyph nibble, one byte each, for FSXM_VGA13's chunky bytes
; (0xFF is colour 15 in the palette pm_pal13 lays down).
pm_nib13:
    db 0x00,0x00,0x00,0x00
    db 0x00,0x00,0x00,0xFF
    db 0x00,0x00,0xFF,0x00
    db 0x00,0x00,0xFF,0xFF
    db 0x00,0xFF,0x00,0x00
    db 0x00,0xFF,0x00,0xFF
    db 0x00,0xFF,0xFF,0x00
    db 0x00,0xFF,0xFF,0xFF
    db 0xFF,0x00,0x00,0x00
    db 0xFF,0x00,0x00,0xFF
    db 0xFF,0x00,0xFF,0x00
    db 0xFF,0x00,0xFF,0xFF
    db 0xFF,0xFF,0x00,0x00
    db 0xFF,0xFF,0x00,0xFF
    db 0xFF,0xFF,0xFF,0x00
    db 0xFF,0xFF,0xFF,0xFF

; ...and the same nibble as one 2bpp byte for FSXM_CGA320, ink 3.
pm_nibc4:
    db 0x00,0x03,0x0C,0x0F,0x30,0x33,0x3C,0x3F
    db 0xC0,0xC3,0xCC,0xCF,0xF0,0xF3,0xFC,0xFF

pm_mono_pairs: db 0,0,0,0,0,0,0,2,2,2,2,2,3,2,3,3
    db 0,0,0,0,0,0,0,1,1,1,1,1,3,1,3,3
pm_initial_x: db 124,124,116,132,122
pm_initial_y: db 100,116,116,116,164
pm_colors: db 12,13,11,14,14
pm_opposite: db 0,2,1,0,8,0,0,0,4
pm_ghost_shapes: dw pm_monslf,pm_monsup,pm_monsdn,pm_monslf,pm_monslf
    dw pm_monslf,pm_monslf,pm_monslf,pm_monsrt
pm_pac_shapes: dw pm_pacdot,pm_pactop,pm_pacbot,pm_pacdot,pm_paclft
    dw pm_pacdot,pm_pacdot,pm_pacdot,pm_pacrgt
pm_fruit_shape: db 0x08,0x10,0x30,0x7C,0xFE,0xFE,0xFE,0x7C,0x38,0
; Preserve the upstream distribution notice in the standalone package too.
pm_license:
    incbin "apps/pacman/LICENSE"
    db 0
%include "pacman/assets.inc"
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%include "os88ui.inc"

; BSS is zeroed by the package loader, not shipped on floppy.
%assign PM_BSS 0
%macro PM_VAR 2
    %1 equ os88_image_end + PM_BSS
    %assign PM_BSS PM_BSS + %2
%endmacro
    PM_VAR pm_win,2
    PM_VAR pm_hired,1
    PM_VAR pm_due,2
    PM_VAR pm_score,4
    PM_VAR pm_level,2
    PM_VAR pm_lives,1
    PM_VAR pm_bonus,1
    PM_VAR pm_pause,1
    PM_VAR pm_abon,1
    PM_VAR pm_fs,1
    PM_VAR pm_mode,1
    PM_VAR pm_hold,2
    PM_VAR pm_eaten,2
    PM_VAR pm_fruit,2
    PM_VAR pm_fruit_count,1
    PM_VAR pm_fright,2
    PM_VAR pm_chain,2
    PM_VAR pm_phase_ticks,2
    PM_VAR pm_chase,1
    PM_VAR pm_frames,2
    PM_VAR pm_x,5
    PM_VAR pm_y,5
    PM_VAR pm_dir,5
    PM_VAR pm_eyes,5
    PM_VAR pm_release,4
    PM_VAR pm_want,1
    PM_VAR pm_choices,1
    PM_VAR pm_candidate,1
    PM_VAR pm_best,2
    PM_VAR pm_tx,1
    PM_VAR pm_ty,1
    PM_VAR pm_full,1
    PM_VAR pm_status_dirty,1
    PM_VAR pm_half,1
    PM_VAR pm_bpp,1
    PM_VAR pm_mcols,2
    PM_VAR pm_monobuf,320
    PM_VAR pm_cx,2
    PM_VAR pm_cy,2
    PM_VAR pm_cw,2
    PM_VAR pm_ch,2
    PM_VAR pm_ox,2
    PM_VAR pm_oy,2
    PM_VAR pm_col,2
    PM_VAR pm_shape,2
    PM_VAR pm_ink,1
    PM_VAR pm_min,22
    PM_VAR pm_max,22
    PM_VAR pm_map,880
    PM_VAR pm_tgt,1
    PM_VAR pm_fsxm,1
    PM_VAR pm_fsi,FSI_SIZE
    PM_VAR pm_fseg,2
    PM_VAR pm_fstride,2
    PM_VAR pm_fbstep,2
    PM_VAR pm_gtab,2
    PM_VAR pm_gseg,2
    PM_VAR pm_gfirst,1
    PM_VAR pm_glast,1
    PM_VAR pm_gbuf,8
    PM_VAR pm_rowc,1
    PM_VAR pm_penx,2
    PM_VAR pm_peny,2
    PM_VAR pm_sdbits,1
    PM_VAR pm_sx,2
    PM_VAR pm_stop,2
    PM_VAR pm_sbot,2
    PM_VAR pm_score_prev,17
    PM_VAR pm_bw,2
    PM_VAR pm_bh,2
    PM_VAR pm_bc,2
    PM_VAR pm_bsk,2
    PM_VAR pm_bdk,2
    PM_VAR pm_bsp,2
    PM_VAR pm_bx,2
    PM_VAR pm_by,2
    PM_VAR pm_bdi,2
    PM_VAR pm_mapb,2
    PM_VAR pm_cvb,2
    PM_VAR pm_c4hi,256
    PM_VAR pm_c4lo,256
    PM_VAR pm_canvas,28160
    OS88_BSS PM_BSS
    OS88_IMAGE_END
