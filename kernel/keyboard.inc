; Optional international input, SPEC.md 9.16. No translator/table/state is
; resident: the core contains dispatch, activation and instance teardown.
%ifdef KERN_MEGA
%if MOD_HIOK & (1 << MOD_KEYBOARD)
  %error "keyboard IRQ/reflection entry must remain conventional"
%endif
%endif
KL_NENT equ 2
KL_SCAN equ 0
KL_CMD equ 4
KBDV_QUERY equ 0
KBDV_SET equ 1
KBDV_HOLD equ 2
KBDV_RELEASE equ 3
KBDV_CANCEL equ 4
KBDV_RAW equ 5
KBDL_BIOS equ 0
KBDL_US equ 1
KBDL_ES equ 2
KBDL_LA equ 3
KBDL_FR equ 4

section .text
; The public cell stamps its owner. Built-in glyph holds use the PRIVATE
; control entry with DH=FF; a package cannot claim that shared hold.
osapi_keyboard:
    push dx
    call inst_caller
    cmp al, KBDV_HOLD
    jb .call
    cmp al, KBDV_RELEASE
    jbe .owner
    cmp al, KBDV_RAW
    jne .call
.owner:
    cmp dh, INST_MAX
    jb .call
    mov ax, 1
    stc
    jmp short .out
.call:
    call COLD_SEG:kl_control_x
.out:
    pop dx
    ret

; No load, including from a painter. The unloaded entry's refusal means
; BIOS default. AX only; FLAGS are outputs, all other registers kept.
kl_query:
    mov al, KBDV_QUERY
    call far [KLFP+KL_CMD]
    jnc .out
    xor ax, ax
.out:
    ret

; Instance teardown already has AL=owner; preserve it for later releases.
kl_release_inst:
    push ax
    push dx
    mov dh, al
    mov al, KBDV_RELEASE
    call COLD_SEG:kl_control_x
    pop dx
    pop ax
    ret

%ifdef KERN_MEGA
; A conventional module has both a protected selector and this real alias.
; Only mega_r15 (already in real mode) uses it; disarmed before the drop.
kl_rfp: dw 0,0
%endif

section .cold
; Same AL/AH contract as OSAPI_KEYBOARD; private HOLD/RELEASE DH=FF is
; Control Panel's own glyph client. No state is kept in the resident core.
kl_control_x:
    push bx
    push cx
    push dx
    push si
    mov si, ax                 ; verb survives module status return
    cmp al, KBDV_RAW
    je .raw
    or al, al
    jz .which                  ; read-only QUERY is safe on every task
    cmp byte [sch_cur], 0
    jne .context
    cmp al, KBDV_CANCEL
    ja .bad
    cmp al, KBDV_SET
    jne .which
    cmp ah, KBDL_FR
    ja .bad
.which:
    cmp al, KBDV_SET
    jne .hold
    or ah, ah
    jnz .load
.hold:
    cmp al, KBDV_HOLD
    je .load
    cmp word [mod_r_keyboard], 0
    je .zero
    jmp short .call
.load:
    push ax
    mov al, MOD_KEYBOARD
    call mod_need
    pop ax
    jc .resource
%ifdef KERN_MEGA
    push ax
    mov dx, [mod_r_keyboard]
    mov bl, SEG_REAL
    call COLD_SEG:osapi_seg
    ; SEG_REAL cannot refuse this valid conventional module claim.
    mov word [kl_rfp], klm_scan
    mov [kl_rfp+2], dx
    pop ax
%endif
.call:
    cmp al, KBDV_SET
    jne .invoke
    or ah, ah
    jz .invoke
    push ax
    xor al, al
    call KERNEL_SEG:rmk_8       ; existing KEY_DOWN cell arms the raw observer
    pop ax
.invoke:
    call far [KLFP+KL_CMD]
    jc .out
    or ax, ax
    jnz .out                   ; explicit layout and/or glyph hold
    mov bx, si
    cmp bl, KBDV_SET
    je .drop
    cmp bl, KBDV_RELEASE
    jne .out                   ; QUERY/CANCEL cannot drop an in-flight load
.drop:
    pushf
    cli                        ; no IRQ sees disarmed slots with a live row
%ifdef KERN_MEGA
    mov word [kl_rfp+2], 0
%endif
    push ax
    mov al, MOD_KEYBOARD
    call mod_drop
    pop ax
    popf
    jmp short .out
.raw:
    cmp dh, INST_MAX
    jae .bad
    mov bl, dh
    xor bh, bh
    mov cl, 5
    shl bx, cl                 ; I_RECSZ=32, existing instance flags byte
    and byte [bx+inst_tab+I_FLAGS], ~IF_RAWKBD & 0xff
    or ah, ah
    jz .query
    or byte [bx+inst_tab+I_FLAGS], IF_RAWKBD
.query:
    mov al, KBDV_QUERY
    jmp .which
.resource:
    mov ax, 2
    stc
    jmp short .out
.context:
    mov ax, 3
    stc
    jmp short .out
.bad:
    mov ax, 1
    stc
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    retf

%ifdef KERN_MEGA
; Boot activation is after mega's font-pointer/cache promotion.
kl_boot_x:
    mov ah, [drv_cfg+CFG_KL]
    or ah, ah
    jz .out
    mov al, KBDV_SET
    call COLD_SEG:kl_control_x
.out:
    retf
%endif

section .modq
mods_kl_header:
    dw 0x384f
    db MOD_VER, MOD_KEYBOARD
    dw BUILD_NUM, MOD_STAMP
    dw MODQ_SIZE, KL_NENT
    dw 0                       ; no kept cache: activation fills the font image
    dw klm_scan, klm_control

; ES = this image's writable alias. SMSW is only in the mega build, whose
; CPU is >=286, and distinguishes native PM from the real reflection's IRQ.
%macro KLM_DATA 0
    mov bx, cs
%ifdef KERN_MEGA
    cpu 286
    smsw ax
    cpu 8086
    test al, 1
    jz %%real
    add bx, 8
%%real:
%endif
    mov es, bx
%endmacro

%macro KLM_BDA 0
    mov bx, 0x40
%ifdef KERN_MEGA
    cpu 286
    smsw ax
    cpu 8086
    test al, 1
    jz %%real
    mov bx, MEGA_BDA
%%real:
%endif
    mov es, bx
%endmacro

; Control: AX inputs, DH stamped owner. Every other register preserved.
klm_control:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov di, ax
    KLM_DATA
    mov ax, di
    cmp al, KBDV_QUERY
    je klm_status
    cmp al, KBDV_SET
    je klm_set
    cmp al, KBDV_HOLD
    je klm_hold
    cmp al, KBDV_RELEASE
    je klm_release
    cmp al, KBDV_CANCEL
    jne klm_bad
    mov byte [es:klm_dead], 0
    jmp klm_status
klm_set:
    cmp ah, KBDL_FR
    ja klm_bad
    or ah, ah
    jz .publish
    call klf_enable
    jc klm_bad
    cmp byte [es:klm_layout], 0
    jne .publish
    push ax
    call klm_seed
    pop ax
.publish:
    pushf
    cli
    mov [es:klm_layout], ah
    mov byte [es:klm_dead], 0
    popf
    jmp klm_status
klm_hold:
    cmp dh, 0xff
    je .built
    cmp dh, INST_MAX
    jae klm_bad
    call klf_enable
    jc klm_bad
    mov cl, dh
    mov bx, 1
    shl bx, cl
    or [es:klm_clients], bx
    jmp short klm_status
.built:
    call klf_enable
    jc klm_bad
    mov byte [es:klm_builtin], 1
    jmp short klm_status
klm_release:
    cmp dh, 0xff
    je .built
    cmp dh, INST_MAX
    jae klm_bad
    mov cl, dh
    mov bx, 1
    shl bx, cl
    not bx
    and [es:klm_clients], bx
    jmp short klm_status
.built:
    mov byte [es:klm_builtin], 0
klm_status:
    mov al, [es:klm_layout]
    xor ah, ah
    or al, al
    jnz .held
    cmp word [es:klm_clients], 0
    jne .held
    cmp byte [es:klm_builtin], 0
    jne .held
    mov bx, di
    cmp bl, KBDV_QUERY
    je .done
    cmp bl, KBDV_CANCEL
    je .done
    call klf_disable
    jmp short .done
.held:
    inc ah
.done:
    clc
    jmp short klm_return
klm_bad:
    mov ax, 1
    stc
klm_return:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; Seed a newly selected layout from the BIOS's lock/modifier snapshot.
klm_seed:
    push es
    KLM_BDA
    mov al, [es:0x17]
    and al, 0x4f               ; BDA left/right Shift, Ctrl/Alt/Caps
    mov ah, [es:0x96]
    mov dl, [es:0x18]
    test ah, 0x10              ; enhanced keyboard: right Ctrl/Alt snapshot
    jz .mods
    test al, 4
    jz .alt
    test ah, 4
    jz .alt
    or al, 32
    test dl, 1
    jnz .alt
    and al, 0xfb
.alt:
    test al, 8
    jz .mods
    test ah, 8
    jz .mods
    or al, 16
    test dl, 2
    jnz .mods
    and al, 0xf7
.mods:
    and dl, 0x40               ; Caps held already: typematic is not an edge
    and ah, 2
    shr ah, 1
    pop es
    mov [es:klm_mods], al
    mov [es:klm_prefix], ah
    mov byte [es:klm_pause], 0
    mov [es:klm_capsdown], dl
    mov byte [es:klm_focus], 0xff
    ret

; Raw set-1 scan, after kbd_track. CF=1 pass, CF=0 consumed; every
; register preserved. No kernel calls, I/O, allocation or unbounded work.
klm_scan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov di, ax
    KLM_DATA
    cmp byte [es:klm_layout], 0
    je klm_pass
    call klm_focus_owner
    mov ax, di
    cmp byte [es:klm_pause], 0
    je .prefix
    dec byte [es:klm_pause]
    jmp klm_pass
.prefix:
    cmp al, 0xe1
    jne .e0
    mov byte [es:klm_pause], 5
    jmp klm_pass
.e0:
    cmp al, 0xe0
    jne .key
    mov byte [es:klm_prefix], 1
    jmp klm_pass
.key:
    xor ah, ah
    mov bp, ax
    and bp, 0x7f
    xor cx, cx
    xchg cl, [es:klm_prefix]
    mov bx, bp
    cmp bl, 0x38
    je .alt
    cmp bl, 0x1d
    je .ctrl
    or cl, cl
    jnz klm_pass              ; grey navigation, fake shifts, keypad / and Enter
    mov cl, 2                 ; match BDA: left Shift bit1, right bit0
    cmp bl, 0x2a
    je .modifier
    mov cl, 1
    cmp bl, 0x36
    je .modifier
    cmp bl, 0x3a
    je .caps
    test al, 0x80
    jnz klm_pass
    cmp byte [es:klm_foreign], 0
    jne klm_pass
    cmp bl, 0x0e
    jne .printable
    cmp byte [es:klm_dead], 0
    je klm_pass
    mov byte [es:klm_dead], 0 ; Backspace cancels the dead accent alone
    jmp klm_eaten
.printable:
    cmp bl, 0x39
    je klm_translate
    cmp bl, 0x56
    je klm_translate
    cmp bl, 0x02
    jb .special
    cmp bl, 0x0d
    jbe klm_translate
    cmp bl, 0x10
    jb .special
    cmp bl, 0x1b
    jbe klm_translate
    cmp bl, 0x1e
    jb .special
    cmp bl, 0x29
    jbe klm_translate
    cmp bl, 0x2b
    jb .special
    cmp bl, 0x35
    jbe klm_translate
.special:
    mov byte [es:klm_dead], 0
    jmp klm_pass
.alt:
    mov ch, 8
    or cl, cl
    jz .modch                  ; CL=0 plain left Alt
    mov ch, 16
    jmp short .modch
.ctrl:
    mov ch, 4
    or cl, cl
    jz .modch
    mov ch, 32
.modch:
    mov cl, ch
.modifier:
    test al, 0x80
    jz .down
    not cl
    and [es:klm_mods], cl
    jmp klm_pass
.down:
    or [es:klm_mods], cl
    jmp klm_pass
.caps:
    test al, 0x80
    jz .capdown
    mov byte [es:klm_capsdown], 0
    jmp klm_pass
.capdown:
    cmp byte [es:klm_capsdown], 0
    jne klm_pass
    mov byte [es:klm_capsdown], 1
    xor byte [es:klm_mods], 0x40
    jmp klm_pass

; Read only bounded existing UI/instance tables; no WM call from IRQ.
; Modifier state keeps tracking while a foreign owner receives BIOS input.
klm_focus_owner:
    xor cx, cx
    mov cl, [wm_zn]
    cmp cl, MAX_WIN
    jbe .bounded
    mov cl, MAX_WIN
.bounded:
    mov si, cx
.next:
    or si, si
    jz .none
    dec si
    mov al, [wm_zord+si]
    cmp al, MAX_WIN
    jae .next
    mov dl, al
    mov bl, WIN_SIZE
    mul bl
    mov bx, ax
    mov ax, [wm_wins+bx+W_FLAGS]
    and ax, WF_DESK | 2
    cmp ax, 2
    jne .next
    xor bx, bx
    mov bl, dl
    mov al, [wm_owner+bx]
    jmp short .owner
.none:
    mov al, 0xff
.owner:
    cmp al, [es:klm_focus]
    je .same
    mov [es:klm_focus], al
    mov byte [es:klm_dead], 0
.same:
    xor dx, dx
    cmp al, INST_MAX
    jae .out
    xor ah, ah
    mov bx, ax
    mov cl, 5
    shl bx, cl
    test byte [inst_tab+bx+I_FLAGS], IF_RAWKBD
    jz .out
    inc dl
.out:
    cmp dl, [es:klm_foreign]
    je .done
    mov [es:klm_foreign], dl
    mov byte [es:klm_dead], 0
    or dl, dl
    jnz .done
    call klm_seed              ; BIOS may have owned IRQs in a foreign session
.done:
    ret

klm_translate:
    xor ax, ax
    mov al, [es:klm_layout]
    dec ax
    add ax, ax
    mov bx, ax
    mov si, [cs:klm_maps+bx]
    mov bx, bp
    add bx, bx
    mov dl, [es:klm_mods]
    test dl, 16
    jnz .altgr
    mov dh, dl
    and dh, 0x2c               ; either Ctrl plus left Alt also selects AltGr
    test dh, 8
    jz .noaltgr
    and dh, 0x24
    jnz .altgr
.noaltgr:
    test dl, 8
    jnz klm_pass               ; ordinary left-Alt BIOS shortcuts
    test dl, 0x24
    jnz .control
    xor dx, dx
    test byte [es:klm_mods], 3
    jz .shifted
    inc dl
.shifted:
    test byte [es:klm_mods], 0x40
    jz .plane
    push bx
    mov bx, bp
    mov cl, 3
    shr bx, cl
    mov al, [cs:si+bx+KLM_MAP_BYTES*3]
    mov bx, bp
    and bl, 7
    mov cl, bl
    mov ah, 1
    shl ah, cl
    test al, ah
    pop bx
    jz .plane
    xor dl, 1
.plane:
    or dl, dl
    jz .lookup
    add si, KLM_MAP_BYTES
.lookup:
    mov dx, [cs:si+bx]
    jmp short klm_character
.altgr:
    cmp byte [es:klm_layout], KBDL_US
    je klm_pass
    test dl, 3                 ; no Shift+AltGr column in these legacy layouts
    jnz klm_eaten
    add si, KLM_MAP_BYTES*2
    jmp short .lookup
.control:
    mov ax, [cs:si+bx]
    cmp al, 'a'
    jb .punct
    cmp al, 'z'
    ja .punct
    sub al, 0x60
    jmp klm_single
.punct:
    cmp al, '['
    jb .other
    cmp al, ']'
    ja .other
    sub al, 0x40
    jmp klm_single
.other:
    cmp al, '6'
    jne .minus
    mov al, 0x1e
    jmp klm_single
.minus:
    cmp al, '-'
    jne klm_pass
    mov al, 0x1f
    jmp klm_single

klm_character:
    or dl, dl
    jz klm_eaten               ; unmappable character (for example Euro)
    mov al, [es:klm_dead]
    test dh, 1
    jz .ordinary
    or al, al
    jz .wait
    cmp al, dl
    je .literal
    mov [es:klm_dead], dl     ; old literal emitted, new accent stays pending
    jmp klm_single
.wait:
    mov [es:klm_dead], dl
    jmp klm_eaten
.literal:
    mov byte [es:klm_dead], 0
    jmp klm_single
.ordinary:
    or al, al
    jz .plain
    mov byte [es:klm_dead], 0
    cmp dl, ' '
    je klm_single
    mov si, klm_compose
    mov cx, KLM_COMPOSE_N
.find:
    cmp al, [cs:si]
    jne .next
    cmp dl, [cs:si+1]
    jne .next
    mov al, [cs:si+2]
    jmp klm_single
.next:
    add si, 3
    loop .find
    mov cx, bp
    mov ch, cl                 ; CX = second word, physical scan and character
    mov cl, dl
    mov dx, bp
    mov ah, dl
    jmp short klm_store
.plain:
    mov al, dl
klm_single:
    xor cx, cx
    mov dx, bp
    mov ah, dl
klm_store:
    push ax                    ; first word while BDA alias macro uses AX
    KLM_BDA
    pop ax
    mov bx, [es:0x1c]
    cmp bx, KBD_BUFB
    jb klm_eaten
    cmp bx, KBD_BUFE
    jae klm_eaten
    test bl, 1
    jnz klm_eaten
    mov si, bx
    call klm_next
    cmp si, [es:0x1a]
    je klm_eaten
    mov dx, si
    jcxz .publish
    call klm_next
    cmp si, [es:0x1a]
    je klm_eaten               ; reserve both before writing either
.publish:
    mov [es:bx], ax
    jcxz .tail
    mov bx, dx
    mov [es:bx], cx
.tail:
    mov [es:0x1c], si          ; publish completed word(s) last
klm_eaten:
    clc
    jmp short klm_scanret
klm_pass:
    stc
klm_scanret:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf
klm_next:
    inc si
    inc si
    cmp si, KBD_BUFE
    jb .out
    mov si, KBD_BUFB
.out:
    ret

%include "keymaps.inc"
%include "keyfont.inc"
section .modq
MODQ_IMG equ $ - $$

section .modqb
klm_layout:   resb 1
klm_mods:     resb 1
klm_prefix:   resb 1
klm_pause:    resb 1
klm_capsdown: resb 1
klm_dead:     resb 1
klm_focus:    resb 1
klm_foreign:  resb 1
klm_clients:  resw 1
klm_builtin:  resb 1
section .text
