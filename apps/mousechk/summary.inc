; =============================================================================
; os8088 - apps/mousechk/summary.inc - the Summary section (SPEC.md 126.5)
;
; The top of the list: which pointer the kernel is using NOW, its live
; position and buttons, the kernel, the CPU and the display, and ONE verdict a
; user can act on. The detail is the other sections'; this one only names what
; is happening. sum_snap reads the kernel's 'MO' block once per rebuild into
; the sum_* statics below, and other.inc reads the same statics (sum_rows runs
; first), so the two sections cannot disagree.
;
; [mc_kok] = 0 (another kernel build, SPEC.md 126.4): nothing is DECODED. The
; pointer row shows the raw bytes and the verdict says so (SPEC.md 47: grey a
; fact, never a guess).
;
; ES = KERNEL_SEG, DS = the package, on entry to both procs.
; =============================================================================

; the verdict, in the order sum_rows chooses it
SUM_V_RAW   equ 0               ; cannot decode this kernel's record
SUM_V_NONE  equ 1               ; the kernel publishes no 'MO' block
SUM_V_OK    equ 2               ; a pointer has spoken
SUM_V_WAIT  equ 3               ; a PS/2 mouse answered, nothing moved yet
SUM_V_NO    equ 4               ; no pointer
SUM_V_SWAIT equ 5               ; a serial port answered the identify, no packet yet

sum_init:
    ret

; -----------------------------------------------------------------------------
; sum_snap - bank the kernel's mouse record (DECODED only when [mc_kok])
; out: sum_ok = the record is read AND decodable; sum_v = the verdict index
; -----------------------------------------------------------------------------
sum_snap:
    xor ax, ax
    mov [sum_ok], al
    mov [sum_seen], al
    mov [sum_port], al
    mov [sum_p2], al
    mov [sum_st], al
    mov [sum_sid], al
    mov [sum_x], ax
    mov [sum_y], ax
    mov [sum_btn], al
    mov byte [sum_v], SUM_V_NONE
    mov ax, DBG_TAG_MOUSE
    call mc_dbgfind
    jc .cpu                         ; this kernel publishes none
    mov si, [es:bx+MOB_SPAN]
    mov al, [es:si+MOS_PORT]
    mov [sum_port], al
    mov al, [es:si+MOS_SEEN]
    mov [sum_seen], al
    ; which serial port answered mouse_init's identify window like a mouse
    ; ([mou_ident], per port, stride 2: SPEC.md 9.4.1). COM1 first
    mov al, '1'
    cmp byte [es:si+MOS_IDENT], 0
    jne .sid
    mov al, '2'
    cmp byte [es:si+MOS_IDENT+2], 0
    jne .sid
    xor al, al
.sid:
    mov [sum_sid], al
    mov si, [es:bx+MOB_CUR]
    mov ax, [es:si+MOC_X]
    mov [sum_x], ax
    mov ax, [es:si+MOC_Y]
    mov [sum_y], ax
    mov al, [es:si+MOC_BTN]
    mov [sum_btn], al
    mov si, [es:bx+MOB_P2]
    mov al, [es:si+MOP_P2]
    mov [sum_p2], al
    mov al, [es:si+MOP_ST]
    mov [sum_st], al
    mov byte [sum_v], SUM_V_RAW
    cmp byte [mc_kok], 0
    je .cpu                         ; raw: never decoded
    mov byte [sum_ok], 1
    mov byte [sum_v], SUM_V_NO
    cmp byte [sum_sid], 0
    je .p2w
    mov byte [sum_v], SUM_V_SWAIT   ; a serial mouse answered: not "no mouse"
.p2w:
    cmp byte [sum_p2], 0            ; ...and a live PS/2 one is named first
    je .chk
    cmp byte [sum_st], 9            ; 9 = the mouse is live (SPEC.md 9.4.4)
    jne .chk
    mov byte [sum_v], SUM_V_WAIT
.chk:
    cmp byte [sum_seen], 0
    je .cpu
    mov byte [sum_v], SUM_V_OK
.cpu:
    call OSAPI_CPU_INFO
    mov [sum_cpu], al
    ; CPU_F_PM: kern_mega (SPEC.md 20.18). DISPLAY ONLY - the Kernel row and
    ; the VMware section's "not kern_emu" - and registered as such in
    ; tests/megapkg.txt, the marker census's rule: a site is named, never
    ; hidden behind a literal
    and ah, CPU_F_PM
    mov [sum_pm], ah
    ret

sum_rows:
    call sum_snap
    mov si, sum_s_head
    mov al, LP_INFO
    call mc_head
    ; --- the pointer in use ----------------------------------------------
    mov si, sum_s_ptr
    call mc_row
    mov al, [sum_v]
    cmp al, SUM_V_OK
    je .name
    cmp al, SUM_V_WAIT
    je .wait
    cmp al, SUM_V_NO
    je .none
    cmp al, SUM_V_SWAIT
    je .swait
    cmp al, SUM_V_RAW
    jne .nomo
    mov si, sum_s_raw               ; "raw port 04 seen 01 p2 01 st 09"
    call mc_put
    mov al, [sum_port]
    call mc_hex8
    mov si, sum_s_rseen
    call mc_put
    mov al, [sum_seen]
    call mc_hex8
    mov si, sum_s_rp2
    call mc_put
    mov al, [sum_p2]
    call mc_hex8
    mov si, sum_s_rst
    call mc_put
    mov al, [sum_st]
    call mc_hex8
    jmp short .pos
.swait:
    mov si, sum_s_com               ; "COM1 mouse found; no movement seen yet"
    call mc_put
    mov al, [sum_sid]
    call mc_putc
    mov si, sum_s_swait
    jmp short .put
.nomo:
    mov si, sum_s_nomo
    jmp short .put
.none:
    mov si, sum_s_none
    jmp short .put
.wait:
    mov si, sum_s_wait
    jmp short .put
.name:
    mov al, [sum_port]
    mov si, sum_s_unk
    cmp al, 8
    ja .put
    test al, 1
    jnz .put
    mov bl, al
    xor bh, bh
    mov si, [sum_pn+bx]
.put:
    call mc_put
    ; --- the live position -----------------------------------------------
.pos:
    mov si, sum_s_pos
    call mc_row
    cmp byte [sum_ok], 0
    je .na
    mov si, sum_s_x
    call mc_put
    mov ax, [sum_x]
    call mc_dec
    mov si, sum_s_y
    call mc_put
    mov ax, [sum_y]
    call mc_dec
    mov si, sum_s_btn
    call mc_put
    mov al, 'L'
    test byte [sum_btn], 1
    jnz .l
    mov al, '-'
.l:
    call mc_putc
    mov al, 'R'
    test byte [sum_btn], 2
    jnz .r
    mov al, '-'
.r:
    call mc_putc
    jmp short .kern
.na:
    mov si, sum_s_na
    call mc_put
    ; --- the machine -----------------------------------------------------
.kern:
    mov si, sum_s_kern
    call mc_row
    mov si, sum_s_kmega
    cmp byte [sum_pm], 0
    jne .kp
    mov si, sum_s_kbig              ; the 'ID' words match and it is real mode:
    cmp byte [mc_kok], 0            ; the build this package was MADE against,
    jne .kp                         ; which the shipped recipe takes from
    mov si, sum_s_kreal             ; kern_big's own map (SPEC.md 126.5)
.kp:
    call mc_put
    mov si, sum_s_id
    call mc_row
    mov ax, [mc_kid]
    call mc_hex16
    mov al, ' '
    call mc_putc
    mov ax, [mc_kid+2]
    call mc_hex16
    mov al, ' '
    call mc_putc
    mov ax, [mc_kid+4]
    call mc_hex16
    mov si, sum_s_idok
    cmp byte [mc_kok], 0
    jne .ip
    mov si, sum_s_idbad
.ip:
    call mc_put
    mov si, sum_s_cpu
    call mc_row
    mov al, [sum_cpu]
    cmp al, 2
    jbe .c1
    mov al, 2
.c1:
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [sum_cn+bx]
    call mc_put
    cmp byte [sum_pm], 0
    je .disp
    mov si, sum_s_pm
    call mc_put
.disp:
    mov si, sum_s_disp
    call mc_row
    call OSAPI_VIDEO                ; AX x BX, DL = adapter, DH = bits a pixel
    push bx
    push ax
    push dx
    mov ax, KERNEL_SEG
    mov es, ax                      ; (a far call: ES is ours again)
    pop dx
    mov bl, dl
    cmp bl, 4
    jbe .d1
    mov bl, 5
.d1:
    xor bh, bh
    shl bx, 1
    mov si, [sum_dn+bx]
    call mc_put
    mov al, ' '
    call mc_putc
    pop ax
    call mc_dec                     ; the width
    mov al, 'x'
    call mc_putc
    pop ax
    call mc_dec                     ; the height
    mov si, sum_s_bpp
    call mc_put
    mov al, dh
    xor ah, ah
    call mc_dec
    mov si, sum_s_bpp2
    call mc_put
    ; --- the verdict: ONE plain line (two when there is a way out) ------------
    mov al, [sum_v]
    cmp al, SUM_V_OK
    je .vok
    cmp al, SUM_V_WAIT
    je .vwait
    cmp al, SUM_V_NO
    je .vno
    cmp al, SUM_V_SWAIT
    je .vswait
    mov si, sum_s_vraw
    cmp al, SUM_V_RAW
    je .vl
    mov si, sum_s_vnomo
.vl:
    call mc_line
    mov al, LP_WARN
    jmp mc_lamp
.vok:
    mov si, sum_s_vok
    call mc_line
    mov al, [sum_port]
    mov si, sum_s_unk
    cmp al, 8
    ja .vp
    test al, 1
    jnz .vp
    mov bl, al
    xor bh, bh
    mov si, [sum_pn+bx]
.vp:
    call mc_put
    mov al, LP_OK
    jmp mc_lamp
.vwait:
    mov si, sum_s_vwait
    call mc_line
.vk:
    mov si, sum_s_kbd
    call mc_line
    mov al, LP_WARN
    jmp mc_lamp
.vswait:
    mov si, sum_s_vsw1              ; "A serial mouse answered on COM1. ..."
    call mc_line
    mov al, [sum_sid]
    call mc_putc
    mov si, sum_s_vsw2
    call mc_put
    jmp short .vk
.vno:
    mov si, sum_s_vno
    call mc_line
    mov si, sum_s_kbd
    call mc_line
    mov al, [sum_st]                ; where the PS/2 handshake stopped, if it did
    cmp al, 9
    jae .vb
    or al, al
    jz .vb
    mov si, sum_s_vst
    call mc_line
    xor ah, ah
    call mc_dec
.vb:
    mov al, LP_BAD
    jmp mc_lamp

; --- the tables, then the strings ---------------------------------------------
sum_pn: dw sum_n0, sum_n2, sum_n4, sum_n6, sum_n8   ; by [mou_port] (0,2,4,6,8: a byte offset)
sum_cn: dw sum_c0, sum_c1, sum_c2
sum_dn: dw sum_d0, sum_d1, sum_d2, sum_d3, sum_d4, sum_d5

sum_s_head: db 'Summary', 0
sum_s_ptr:  db 'Pointer in use', 0
sum_s_pos:  db 'Position', 0
sum_s_kern: db 'Kernel', 0
sum_s_id:   db 'Kernel id', 0
sum_s_cpu:  db 'CPU', 0
sum_s_disp: db 'Display', 0
sum_n0: db 'COM1 serial mouse', 0
sum_n2: db 'COM2 serial mouse', 0
sum_n4: db 'PS/2 mouse', 0
sum_n6: db 'VMware absolute pointer', 0
sum_n8: db 'USB mouse (USBMOUSE.DRV)', 0
sum_s_unk:  db 'unknown source (row not recognised)', 0
sum_s_none: db 'none yet - the keyboard mouse is on', 0
sum_s_wait: db 'PS/2 mouse found; no movement seen yet', 0
sum_s_com:  db 'COM', 0
sum_s_swait equ sum_s_wait + 4   ; ' mouse found; no movement seen yet' (shared tail)
sum_s_nomo: db 'unknown (no mouse record)', 0
sum_s_raw:  db 'raw: port ', 0
sum_s_rseen: db ' seen ', 0
sum_s_rp2:  db ' p2 ', 0
sum_s_rst:  db ' st ', 0
sum_s_x:    db 'x ', 0
sum_s_y:    db '  y ', 0
sum_s_btn:  db '  buttons ', 0
sum_s_na:   db 'unknown', 0
sum_s_kbig: db 'kern_big', 0
sum_s_kreal: db 'another real-mode build (not decoded)', 0
sum_s_kmega: db 'kern_mega', 0
sum_s_idok: db '  matches this package', 0
sum_s_idbad: db '  differs: raw', 0
sum_c0: db '8086/8088', 0
sum_c1: db '80286', 0
sum_c2: db '80386 or later', 0
sum_s_pm: db ', protected mode', 0
sum_d0: db 'VGA', 0
sum_d1: db 'Hercules', 0
sum_d2: db 'CGA', 0
sum_d3: db 'EGA', 0
sum_d4: db 'VESA', 0
sum_d5: db 'display', 0
sum_s_bpp:  db ', ', 0
sum_s_bpp2: db ' bpp', 0
; the verdict lines. The gate asserts on these EXACT strings (SPEC.md 126.5)
sum_s_vok:   db 'Your mouse is working: ', 0
sum_s_vwait: db 'A PS/2 mouse answered. Move it to confirm.', 0
sum_s_vsw1:  db 'A serial mouse answered on COM', 0
sum_s_vsw2  equ sum_s_vwait + 21 ; '. Move it to confirm.' (shared tail)
sum_s_vno:   db 'No mouse found. See the sections below for why.', 0
sum_s_kbd:   db 'The keyboard mouse is on: arrows move, keypad 5 clicks.', 0
sum_s_vst:   db 'The PS/2 handshake stopped at step ', 0
sum_s_vraw:  db 'Cannot decode the mouse record: a different kernel build.', 0
sum_s_vnomo: db 'This kernel publishes no mouse record to read.', 0

; --- statics (the image is writable, SPEC.md 20; other.inc reads them) ---------
sum_ok:   db 0
sum_v:    db 0
sum_seen: db 0
sum_port: db 0
sum_p2:   db 0
sum_st:   db 0
sum_sid:  db 0                  ; '1'/'2': that COM port answered the identify
sum_btn:  db 0
sum_x:    dw 0
sum_y:    dw 0
sum_cpu:  db 0                  ; OSAPI_CPU_INFO's tier
sum_pm:   db 0                  ; nonzero: protected mode (kern_mega)
