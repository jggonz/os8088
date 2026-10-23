; MIXER.O88 — the optional hardware mixer (SPEC.md 34.15).
; The driver owns every port. The package owns only channel selection,
; level/balance gestures and a compact native window. Both faces share
; apps/os88mixui.inc with the quick-volume popup.
%include "os88api.inc"
%include "sndpkg.inc"
    OS88_HEADER 'Mixer', mx_entry, 1, OS88_HIGH | OS88_HIGHMEM ; kern_mega
                                   ; may run it above 1MB, claims and all: no
                                   ; arithmetic on its own CS or DS, no claim,
                                   ; and every port is SOUND.DRV's
                                   ; (KERN-MEGA-PLAN 14.52)
    OS88_ICON16                    ; three staggered mechanical faders
    times 16 dw 0FFFFh
    dw 0000h,1084h,1084h,1084h,7C84h,4484h,7C84h,1084h
    dw 1084h,11F0h,1110h,11F0h,1084h,109Fh,1091h,001Fh
    OS88_ICON16_END

mx_entry:
    mov si, mx_tpl
    call OSAPI_WM_CREATE
    jc mx_ret
    mov [mx_win], bx
    OS88_REGION_MOVABLE_NOKEEP mx_ret
    mov si, mx_about
    call OSAPI_ABOUT_SET
    mov si, mx_pref
    call OSAPI_WM_PREFER
    mov al, 1
    call OSAPI_WM_KEEPH
    mov ax, mx_click
    call OSAPI_WM_ONCLICK
    mov ax, mx_drag
    call OSAPI_WM_ONDRAG
    mov ax, mx_up
    call OSAPI_WM_ONMOUSEUP
    mov ax, mx_timer
    call OSAPI_WM_ONTIMER
    mov ax, 18
    call OSAPI_WM_TIMER
    clc
mx_ret:
    ret

mx_org:
    mov bx, [mx_win]
    call OSAPI_WM_CONTENT
    mov [mx_ox], ax
    mov [mx_oy], dx
    ret
mx_clip:
    call mx_org
    call OSAPI_WM_CLIP_SET
    ret

; Query actual hardware on every paint/gesture/timer; unloading the driver
; leaves an honest unavailable window. The channel mask is never guessed.
mx_fetch:
    mov word [mx_mask], 0
    mov bx, DRVC_SOUND*256 + SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .none
    cmp ax, SND_SIG
    jne .none
    mov bl, SNDV_MIXINFO
    call OSAPI_DRV_CALL
    jc .none
    mov [mx_mask], cx
    mov [mx_chip], al
    xor ah, ah
    mov cl, [mx_sel]
    mov ax, 1
    shl ax, cl
    test [mx_mask], ax
    jnz .get
    mov byte [mx_sel], 0
.get:
    mov al, [mx_sel]
    mov bl, SNDV_MIXGET
    call OSAPI_DRV_CALL
    jc .none
    mov al, [mx_sel]
    cmp al, [mx_source]
    jne .changed
    cmp dl, [mx_flags]
    jne .changed
    cmp cx, [mx_pair]
    je .same                   ; retain the smooth position for our last write
.changed:
    mov [mx_source], al
    mov [mx_pair], cx
    mov [mx_flags], dl
    test dl, MIX_STEREO
    jnz .stereo
    mov byte [mx_focus], 0
.stereo:
    mov al, cl
    cmp al, ch
    jae .level
    mov al, ch
.level:
    mov [mx_level], al
    test byte [mx_flags], MIX_TONE
    jz .volume
    mov [mx_level], cl
    mov [mx_balance], ch
    clc
    ret
.volume:
    ; Balance: center=50, left=0, right=100. Derive from the actual pair,
    ; so independent mixer clients and quantized hardware remain visible.
    mov dl, al
    mov al, 50
    or dl, dl
    jz .balance
    cmp cl, ch
    jb .right
    mov al, ch
    mov ah, 50
    mul ah
    div dl
    jmp short .balance
.right:
    mov al, cl
    mov ah, 50
    mul ah
    div dl
    mov ah, 100
    sub ah, al
    mov al, ah
.balance:
    mov [mx_balance], al
.same:
    clc
    ret
.none:
    mov word [mx_mask], 0
    mov byte [mx_source], 0FFh
    stc
    ret

mx_paint:
    push si
    call mx_org
    call mx_fetch
    mov byte [mx_force], 1
    call mx_face
    cmp byte [mx_abon], 0
    je .out
    mov bx, [mx_win]
    mov si, mx_ablines
    call os88ui_about_d
.out:
    pop si
    ret

mx_face:
    ; Header and the source/control divider are static, drawn on expose.
    mov si, mx_heading
    xor cx, cx
    xor dx, dx
    call mx_text
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [mx_ox]
    add ax, 111
    mov bx, [mx_oy]
    add bx, 12
    mov dx, bx
    add dx, 126
    call OSAPI_GFX_VLINE
    xor ax, ax
.row:
    push ax
    call mx_row
    pop ax
    inc ax
    cmp ax, MIX_NCHAN
    jb .row
    call mx_controls
    ret

mx_row:                        ; AX=channel row; preserve no caller scratch
    mov [mx_rownum], ax
    mov bx, ax
    shl bx, 1
    mov si, [mx_names+bx]
    mov bx, 13
    mul bx
    add ax, 14
    mov dx, ax
    mov cx, 8
    add cx, [mx_ox]
    add dx, [mx_oy]
    mov al, CBLACK
    mov ah, CWHITE
    mov bx, [mx_rownum]
    cmp bl, [mx_sel]
    jne .disabled
    xchg al, ah
.disabled:
    push ax
    mov ax, 1
    mov cl, bl
    shl ax, cl
    test ax, [mx_mask]
    pop ax
    jnz .draw
    stc
    call OSAPI_GFX_PEN
    mov al, CDGRAY
.draw:
    mov cx, [mx_ox]
    add cx, 8
    call OSAPI_FONT_RUN
    clc
    call OSAPI_GFX_PEN
    ret

mx_controls:
    cmp byte [mx_force], 0
    je .check
    mov byte [mx_drawlevel], 0FFh
    mov byte [mx_drawbalance], 0FFh
.check:
    cmp word [mx_mask], 0
    jne .live
    mov ax, [mx_ox]
    add ax, 120
    mov bx, [mx_oy]
    add bx, 14
    mov cx, ax
    add cx, 233
    mov dx, bx
    add dx, 124
    call mx_clear
    mov si, mx_missing
    mov cx, 128
    mov dx, 36
    call mx_text
    mov si, mx_load
    mov cx, 128
    mov dx, 52
    jmp mx_text
.live:
    mov al, [mx_level]
    cmp al, [mx_drawlevel]
    je .second
    mov [mx_drawlevel], al
    mov ax, [mx_ox]
    add ax, 136
    mov bx, [mx_oy]
    add bx, 18
    mov cl, [mx_level]
    mov si, mx_levellabel
    cmp byte [mx_focus], 0
    je .level
    mov si, mx_levelplain
.level:
    test byte [mx_flags], MIX_TONE
    jz .drawlevel
    mov si, mx_toneleft
.drawlevel:
    call mixui_draw
.second:
    test byte [mx_flags], MIX_STEREO
    jz .mono
    mov al, [mx_balance]
    cmp al, [mx_drawbalance]
    je .done
    mov [mx_drawbalance], al
    mov ax, [mx_ox]
    add ax, 136
    mov bx, [mx_oy]
    add bx, 78
    mov cl, [mx_balance]
    mov si, mx_balanceplain
    cmp byte [mx_focus], 0
    je .balance
    mov si, mx_balancelabel
.balance:
    test byte [mx_flags], MIX_TONE
    jz .drawbalance
    mov si, mx_toneright
.drawbalance:
    call mixui_draw
    mov si, mx_hint
    test byte [mx_flags], MIX_TONE
    jz .tonehint
    mov si, mx_tonehint
.tonehint:
    jmp short .hint
.mono:
    cmp byte [mx_force], 0
    je .done
    mov ax, [mx_ox]
    add ax, 128
    mov bx, [mx_oy]
    add bx, 78
    mov cx, ax
    add cx, 215
    mov dx, bx
    add dx, 47
    call mx_clear
    mov si, mx_monotext
    mov cx, 136
    mov dx, 94
    call mx_text
    mov si, mx_monohint
.hint:
    cmp byte [mx_force], 0
    je .done
    mov cx, 128
    mov dx, 130
    call mx_text
.done:
    mov byte [mx_force], 0
    ret

; Opaque text at content-relative CX/DX. Caller supplies a NUL string.
mx_clear:
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    ret
mx_text:
    add cx, [mx_ox]
    add dx, [mx_oy]
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

mx_click:
    push si
    cmp byte [mx_abon], 0
    je .hit
    call mx_abdismiss
    jmp .out
.hit:
    push cx
    push dx
    call mx_org
    pop dx
    pop cx
    sub cx, [mx_ox]
    sub dx, [mx_oy]
    cmp cx, 112
    jae .control
    sub dx, 14
    js .out
    mov ax, dx
    mov bl, 13
    div bl
    cmp al, MIX_NCHAN
    jae .out
    xor ah, ah
    mov cl, al
    mov dx, 1
    shl dx, cl
    test dx, [mx_mask]
    jz .out
    mov bl, [mx_sel]
    mov [mx_oldsel], bl
    mov [mx_sel], al
    mov byte [mx_focus], 0
    mov byte [mx_force], 1
    call mx_clip
    jc .out
    mov al, [mx_oldsel]
    xor ah, ah
    call mx_row
    mov al, [mx_sel]
    xor ah, ah
    call mx_row
    call mx_fetch
    call mx_controls
    jmp .out
.control:
    cmp cx, 128
    jb .out
    cmp cx, 320
    jae .out
    cmp dx, 18
    jb .out
    cmp dx, 66
    jb .level
    cmp dx, 78
    jb .out
    cmp dx, 126
    jae .out
    test byte [mx_flags], MIX_STEREO
    jz .out
    mov byte [mx_focus], 1
    jmp short .grab
.level:
    mov byte [mx_focus], 0
.grab:
    cmp word [mx_mask], 0
    je .out
    mov byte [mx_force], 1      ; focus labels belong to both cached bands
    mov byte [mx_dragging], 1
    mov ax, [mx_pair]
    mov [mx_baseline], ax
    mov ax, cx
    sub ax, 136
    mov cx, ax
    xor ax, ax
    call mixui_hit
    call mx_apply
.out:
    pop si
    ret

mx_drag:
    push si
    cmp byte [mx_dragging], 0
    je .out
    push cx
    call mx_org
    pop cx
    mov ax, [mx_ox]
    add ax, 136
    call mixui_hit
    call mx_apply
.out:
    pop si
    ret
mx_up:
    mov byte [mx_dragging], 0
    ret

; AL=desired level/balance. Keep its smooth position, and bank the actual
; readback separately so polling still sees writes from another instance.
mx_apply:
    mov [mx_wanted], al
    cmp word [mx_mask], 0
    je .out
    test byte [mx_flags], MIX_TONE
    jz .volume
    mov cx, [mx_pair]
    cmp byte [mx_focus], 0
    jne .toneright
    mov cl, al
    jmp short .set
.toneright:
    mov ch, al
    jmp short .set
.volume:
    cmp byte [mx_focus], 0
    jne .balance
    mov cx, [mx_baseline]
    call mixui_scale
    jmp short .set
.balance:
    mov dl, al
    mov cl, [mx_level]
    mov ch, cl
    cmp al, 50
    ja .right
    mul ch
    mov dl, 50
    div dl
    mov ch, al
    jmp short .set
.right:
    mov al, 100
    sub al, dl
    mul cl
    mov dl, 50
    div dl
    mov cl, al
.set:
    mov al, [mx_sel]
    mov bx, DRVC_SOUND*256 + SNDV_MIXSET
    call OSAPI_DRV_CALL
    jc .refresh
    call mx_fetch
    jc .paint
    mov al, [mx_wanted]
    cmp byte [mx_focus], 0
    jne .wantedright
    mov [mx_level], al
    jmp short .paint
.wantedright:
    mov [mx_balance], al
    jmp short .paint
.refresh:
    call mx_fetch
.paint:
    call mx_clip
    jc .out
    call mx_controls
.out:
    ret

mx_key:
    push si                    ; W_ONKEY owes the window pointer back
    call mx_key_do
    pop si
    ret
mx_key_do:
    cmp byte [mx_abon], 0
    jne mx_abdismiss
    push ax
    call mx_org
    pop ax
    cmp al, 9
    je .focus
    cmp ah, 48h
    je .prev
    cmp ah, 50h
    je .next
    cmp ah, 47h
    je .home
    cmp ah, 4Fh
    je .end
    cmp ah, 4Bh
    je .less
    cmp ah, 4Dh
    jne mx_ret
    call mx_keybase
    add al, 1
    cmp al, 100
    jbe .apply
.end:
    mov al, 100
    jmp short .apply
.home:
    xor al, al
    jmp short .apply
.less:
    call mx_keybase
    sub al, 1
    jnc .apply
    xor al, al
.apply:
    mov dx, [mx_pair]
    mov [mx_baseline], dx
    jmp mx_apply
.focus:
    test byte [mx_flags], MIX_STEREO
    jz mx_ret
    xor byte [mx_focus], 1
    mov byte [mx_force], 1
    call mx_clip
    jc mx_ret
    jmp mx_controls
.prev:
    mov dl, -1
    jmp short .select
.next:
    mov dl, 1
.select:
    mov al, [mx_sel]
    mov [mx_oldsel], al
.scan:
    add al, dl
    cmp al, MIX_NCHAN
    jae mx_ret
    mov cl, al
    mov bx, 1
    shl bx, cl
    test bx, [mx_mask]
    jz .scan
    mov [mx_sel], al
    mov byte [mx_focus], 0
    mov byte [mx_force], 1
    call mx_clip
    jc mx_ret
    mov al, [mx_oldsel]
    xor ah, ah
    call mx_row
    mov al, [mx_sel]
    xor ah, ah
    call mx_row
    call mx_fetch
    jmp mx_controls
mx_keybase:
    mov al, [mx_level]
    cmp byte [mx_focus], 0
    je .out
    mov al, [mx_balance]
.out:
    ret

mx_timer:
    push si
    mov ax, [mx_pair]
    mov [mx_oldpair], ax
    mov ax, [mx_mask]
    mov [mx_oldmask], ax
    call mx_fetch
    cmp byte [mx_abon], 0
    jne .arm
    cmp byte [mx_dragging], 0
    jne .arm
    mov ax, [mx_mask]
    cmp ax, [mx_oldmask]
    jne .all
    mov ax, [mx_pair]
    cmp ax, [mx_oldpair]
    je .arm
    call mx_clip
    jc .arm
    call mx_controls
    jmp short .arm
.all:
    mov byte [mx_force], 1
    call mx_clip
    jc .arm
    call mx_face
.arm:
    mov bx, [mx_win]
    mov ax, 18
    call OSAPI_WM_TIMER
    pop si
    ret

mx_about:
    mov byte [mx_abon], 1
    mov bx, [mx_win]
    mov si, mx_ablines
    jmp os88ui_about
mx_abdismiss:
    mov byte [mx_abon], 0
    call mx_clip
    jc .out
    mov ax, [mx_ox]
    mov bx, [mx_oy]
    mov cx, ax
    add cx, 357
    mov dx, bx
    add dx, 142
    call mx_clear
    mov byte [mx_force], 1
    call mx_face
.out:
    ret

mx_tpl:
    dw 136,40,360,162
    dw mx_title,mx_paint,mx_key,mx_click
mx_pref: dw 360,162, 360,162, 360,162
mx_title: db 'Mixer',0
mx_heading: db '  SOUND MIXER',0
mx_levellabel: db '> Level',0
mx_levelplain: db '  Level',0
mx_balancelabel: db '> Balance',0
mx_balanceplain: db '  Balance',0
mx_hint: db 'L     Tab: focus     R',0
mx_toneleft: db 'Left tone',0
mx_toneright: db 'Right tone',0
mx_tonehint: db 'Flat: 53%  Tab: focus ',0
mx_monohint: db '  Mono channel       ',0
mx_monotext: db 'Balance: mono',0
mx_missing: db 'Mixer unavailable',0
mx_load: db 'Load SOUND in Drivers',0
mx_names: dw mx_master,mx_pcm,mx_fm,mx_cd,mx_line,mx_mic,mx_spk,mx_bass,mx_treble
mx_master: db 'Master     ',0
mx_pcm: db 'PCM        ',0
mx_fm: db 'FM / MIDI  ',0
mx_cd: db 'CD         ',0
mx_line: db 'Line       ',0
mx_mic: db 'Mic        ',0
mx_spk: db 'PC Speaker ',0
mx_bass: db 'Bass       ',0
mx_treble: db 'Treble     ',0
mx_ablines:
    db 'Mixer',0,'Hardware source levels',0,'and stereo balance',0
    db 'Arrow keys: select / adjust',0,'Tab: level or balance',0,OS88UI_ABEND

mx_win: dw 0
mx_ox: dw 0
mx_oy: dw 0
mx_pair: dw 0
mx_baseline: dw 0
mx_mask: dw 0
mx_oldpair: dw 0
mx_oldmask: dw 0
mx_rownum: dw 0
mx_chip: db 0
mx_sel: db 0
mx_oldsel: db 0
mx_flags: db 0
mx_source: db 0FFh
mx_level: db 0
mx_balance: db 0
mx_focus: db 0
mx_dragging: db 0
mx_wanted: db 0
mx_abon: db 0
mx_force: db 0
mx_drawlevel: db 0FFh
mx_drawbalance: db 0FFh

%define MIXUI_BUFFER mx_band
%include "os88mixui.inc"
%define OS88UI_ABOUT
%define OS88UI_NOBTN
%define OS88UI_ABPACK
%include "os88ui.inc"

    OS88_BSS 1056
    OS88_IMAGE_END
mx_band equ os88_image_end
