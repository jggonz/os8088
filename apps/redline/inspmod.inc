; =============================================================================
; apps/redline/inspmod.inc - the Inspect tab's MODULE (SPEC.md 103.7.2)
;
; A LAZY, COMPRESSED CODE PART of REDLINE.O88 (SPEC.md 20.12), assembled with
; the package so that every package symbol it names is the package's own
; address, and cut out of the image by tools/os88ovl.py at the image size
; the header records - Sheet's chart module's shape (82.16.12). Redline's
; segment had 3.7 KB free and this is ~13 KB of code and text, which is
; what the parts standard is for.
;
; THE CONVENTIONS, which are apps/pixel/pxpart.inc's rules for one part:
;   - CS = the module, reached by a far call through ri_mcall with AL = a
;     verb, and answering with retf: CF = 0 and AX, or CF = 1.
;   - DS = the MODULE'S OWN DATA segment, loaded from RIM_SELF, which the
;     package stamps before every call. Its strings, its state and the
;     lines it composes are all here, so the package's 64KB pays only for
;     the frame that draws them.
;   - ES = THE PACKAGE on entry. The module reads it ONCE, at the probe,
;     copying the inventory REDLINE already takes (rl_facts_start ..
;     rl_facts_end), the BIOS text, the CPU's name and what the resident
;     measured (ri_pf), and never touches the package again.
;   - It never speaks and never claims: no toast, no drawing, no file call,
;     no OSAPI_MEM_*. The package PINS it for the length of a call
;     (op_pin), because a CS on the stack is a segment no relocation proc
;     can put right.
; =============================================================================
section .modi vstart=0 align=1

RIM_SIG     equ 0                   ; dw 'RI'
RIM_SELF    equ 2                   ; dw the module's data segment (stamped)
RIM_ENTRY   equ 4                   ; dw the entry's offset
RIM_SIGVAL  equ 'RI'
RIM_PROBE   equ 0                   ; the verbs: probe everything,
RIM_COMPOSE equ 1                   ; ...compose page AH, out AX = lines,
RIM_TEXT    equ 2                   ; ...or as CR LF text in place at
                                    ; RIM_LINES, out AX = its bytes
RIM_LINES   equ ri_lines            ; what the package draws from
RIM_ATTR    equ ri_lattr
RIM_LBAR    equ ri_lbar

%define RF(x) ri_facts + ((x) - rl_facts_start)
%define PF(x) ri_pfc + ((x) - ri_pf)

rim_head:
    dw RIM_SIGVAL
    dw 0
    dw rim_entry

rim_entry:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    cld
    mov ds, [cs:RIM_SELF]
    mov [ri_pkg], es
    cmp al, RIM_PROBE
    jne .comp
    call rim_probe
    xor ax, ax
    jmp short .ok
.comp:
    cmp al, RIM_TEXT
    ja .bad
    cmp ah, RI_NPAGE
    jae .bad
    mov word [ri_mnl], 0
    mov bl, ah
    xor bh, bh
    shl bx, 1
    push ds
    pop es
    push ax
    call [ri_comps + bx]
    pop ax
    cmp al, RIM_TEXT
    mov ax, [ri_mnl]
    jne .ok
    call ri_totext                  ; AX = the text's bytes
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; ri_totext - the page just composed, turned into text IN PLACE at
; ri_lines: each line with its padding trimmed and CR LF, and a blank line
; after the page. A line's text is never longer than its slot (72 cells and
; two bytes against RI_LW), so the writer never passes the reader and no
; second buffer is needed. out AX = the bytes
ri_totext:
    push ds
    pop es
    mov di, ri_lines
    xor bx, bx
.line:
    cmp bx, [ri_mnl]
    jae .end
    mov ax, RI_LW
    mul bx
    add ax, ri_lines
    mov si, ax
    mov dx, di
.c:
    lodsb
    or al, al
    jz .eol
    stosb
    jmp short .c
.eol:
    cmp di, dx
    je .crlf
    cmp byte [di - 1], ' '
    jne .crlf
    dec di
    jmp short .eol
.crlf:
    mov ax, 0x0A0D
    stosw
    inc bx
    jmp short .line
.end:
    mov ax, 0x0A0D
    stosw
    xchg ax, di
    sub ax, ri_lines
    ret

ri_comps:   dw ri_c_sum, ri_c_cpu, ri_c_mem, ri_c_vid, ri_c_drv, ri_c_port
            dw ri_c_irq, ri_c_dev

; ri_facts_in - the package's facts into ours (ES = the package)
ri_facts_in:
    push ds
    push es
    push ds
    push es
    pop ds                          ; DS:SI the package's,
    pop es                          ; ES:DI ours
    mov si, rl_facts_start
    mov di, ri_facts
    mov cx, rl_facts_end - rl_facts_start
    rep movsb
    mov si, ri_pf
    mov di, ri_pfc
    mov cx, RI_PFSZ
    rep movsb
    mov si, cs_bt
    mov di, ri_bt
    mov cx, 2 * CS_BTW
    rep movsb
    mov si, [rl_cpuname]            ; the CPU's name is a package string
    mov di, ri_cpun
    mov cx, 31
.n:
    lodsb
    or al, al
    jz .z
    stosb
    loop .n
.z:
    xor al, al
    stosb
    pop es
    pop ds
    ret

; ri_win - AX = a physical paragraph, BX -> a window word: AX = a 64KB
; window over it (OSAPI_SEG's SEG_WIN, cs_win's): under kern_mega a
; paragraph past the kernel's own is no selector. Every other register kept
ri_win:
    or ax, ax
    jz .z
    push bx
    push cx
    push dx
    mov dx, ax
    mov ax, [bx]
    push bx
    xor cx, cx
    mov bl, SEG_WIN
    call OSAPI_SEG
    pop bx
    mov [bx], dx
    xchg ax, dx
    pop dx
    pop cx
    pop bx
.z:
    ret

ri_f000:                            ; AX = a window over F000h
    push bx
    mov ax, 0xF000
    mov bx, ri_fwin
    call ri_win
    pop bx
    ret

; =============================================================================
; Composing: RI_LINES slots of RI_LW bytes, each padded to RI_COLS so a line
; is drawn opaque across its width and needs no erase pass
; =============================================================================
; ri_lnew - AL = the kind: start the next line empty (a page past RI_LINES
; composes into the spare slot and is dropped)
ri_lnew:
    push ax
    push cx
    push dx                         ; (the mul)
    push di
    mov di, [ri_mnl]
    cmp di, RI_LINES
    jb .ok
    mov di, RI_LINES                ; the spare slot after the last
.ok:
    mov [ri_lattr + di], al
    mov ax, RI_LW
    mul di
    add ax, ri_lines
    mov [ri_lcur], ax
    add ax, RI_COLS
    mov [ri_lend], ax
    pop di
    pop dx
    pop cx
    pop ax
    ret

; ri_lfin - pad the line with spaces, close it and count it
ri_lfin:
    push ax
    push di
    mov di, [ri_lcur]
    mov al, ' '
.p:
    cmp di, [ri_lend]
    jae .z
    mov [di], al
    inc di
    jmp short .p
.z:
    mov byte [di], 0
    cmp word [ri_mnl], RI_LINES
    jae .full
    inc word [ri_mnl]
.full:
    pop di
    pop ax
    ret

ri_put:                             ; SI = NUL text, appended
    push ax
    push si
.c:
    lodsb
    or al, al
    jz .e
    call ri_ch
    jmp short .c
.e:
    pop si
    pop ax
    ret

ri_ch:                              ; AL = one character
    push di
    mov di, [ri_lcur]
    cmp di, [ri_lend]
    jae .e
    mov [di], al
    inc word [ri_lcur]
.e:
    pop di
    ret

ri_col:                             ; AL = a column: spaces up to it
    push ax
    push bx
    xor ah, ah
    mov bx, [ri_lend]
    sub bx, RI_COLS
    add bx, ax                      ; BX = that column's cell
.s:
    cmp [ri_lcur], bx
    jae .e
    mov al, ' '
    call ri_ch
    jmp short .s
.e:
    pop bx
    pop ax
    ret

ri_hex2:                            ; AL as two hex digits
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 0x0F
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .w
    add al, 7
.w:
    jmp ri_ch

ri_hex4:                            ; AX as four
    xchg al, ah
    call ri_hex2
    xchg al, ah
    jmp ri_hex2

ri_hexh:                            ; AX as four, and an 'h'
    call ri_hex4
    push ax
    mov al, 'h'
    call ri_ch
    pop ax
    ret

ri_dec:                             ; AX unsigned
    push dx
    xor dx, dx
    call ri_dec32
    pop dx
    ret

ri_dec32:                           ; DX:AX unsigned
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, 10
    xor cx, cx
.div:
    mov si, ax                      ; DX:AX / 10, high word first
    mov ax, dx
    xor dx, dx
    div bx
    xchg ax, si                     ; SI = the high quotient
    div bx
    push dx
    mov dx, si
    inc cx
    mov si, ax
    or si, dx
    jnz .div
.out:
    pop ax
    add al, '0'
    call ri_ch
    loop .out
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ri_dec1:                            ; AX = a value x 10: "12.3"
    push ax
    push dx
    push cx
    xor dx, dx
    mov cx, 10
    div cx
    call ri_dec
    mov al, '.'
    call ri_ch
    mov al, dl
    add al, '0'
    call ri_ch
    pop cx
    pop dx
    pop ax
    ret

ri_dec2:                            ; DX:AX = a value x 100: "4.77"
    push ax
    push bx
    push cx
    push dx
    mov cx, 100
    cmp dx, cx
    jae .out                        ; past 655 MHz: not a figure we print
    div cx
    mov bx, dx
    call ri_dec
    mov al, '.'
    call ri_ch
    mov ax, bx
    mov cl, 10
    div cl
    add ax, '00'
    call ri_ch
    mov al, ah
    call ri_ch
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_kv - SI = a label: a new text line, the label two cells in, and the
; write pointer at the value column
ri_kv:
    push ax
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    call ri_put
    mov al, RI_VALC
    call ri_col
    pop ax
    ret

; ri_kvs - SI = a label, DI = its value text: the whole line
ri_kvs:
    call ri_kv
    push si
    mov si, di
    call ri_put
    pop si
    jmp ri_lfin

; ri_kvn - SI = a label, AX = a number, DI = a unit (0 = none)
ri_kvn:
    call ri_kv
    call ri_dec
ri_unit:
    or di, di
    jz .fin
    push si
    mov si, di
    call ri_put
    pop si
.fin:
    jmp ri_lfin

; ri_head - SI = a header's text: a header line
ri_head:
    push ax
    mov al, RIA_HEAD
    call ri_lnew
    mov al, ' '
    call ri_ch
    call ri_put
    call ri_lfin
    pop ax
    ret

; ri_note - SI = a line of prose, two cells in
ri_note:
    push ax
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    call ri_put
    call ri_lfin
    pop ax
    ret

ri_gap:                             ; a blank line
    push ax
    mov al, RIA_TEXT
    call ri_lnew
    call ri_lfin
    pop ax
    ret

; ri_bar - SI = a label, AX = a part, DX = the whole (both KB): a bar line
; with "part KB" and the share. A whole of 0 draws an empty bar
ri_bar:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, ax
    mov cx, dx
    mov al, RIA_BAR
    call ri_lnew
    mov al, 2
    call ri_col
    call ri_put
    mov al, RI_BARC - 10
    call ri_col
    mov ax, bx
    call ri_dec
    mov si, ri_s_kb
    call ri_put
    mov al, RI_FIGC
    call ri_col
    xor ax, ax                      ; the share, per mille
    jcxz .set
    mov ax, bx
    cmp ax, cx
    jbe .mul
    mov ax, cx
.mul:
    mov dx, 1000
    mul dx
    div cx
.set:
    mov di, [ri_mnl]
    cmp di, RI_LINES
    jb .slot
    mov di, RI_LINES
.slot:
    shl di, 1
    mov [ri_lbar + di], ax
    push ax                         ; and in figures: " 45%"
    xor dx, dx
    mov cx, 10
    div cx
    mov cl, ' '
    cmp ax, 100
    jae .pct
    xchg ax, cx
    call ri_ch
    xchg ax, cx
    cmp ax, 10
    jae .pct
    xchg ax, cx
    call ri_ch
    xchg ax, cx
.pct:
    call ri_dec
    mov al, '%'
    call ri_ch
    pop ax
    call ri_lfin
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_yesno - CF = 0 'yes', 1 'no' after the label SI
ri_yn:
    mov di, ri_s_yes
    jnc ri_kvs
    mov di, ri_s_no
    jmp ri_kvs

ri_s_kb:    db ' KB', 0
ri_s_yes:   db 'yes', 0
ri_s_no:    db 'no', 0
ri_s_na:    db 'n/a', 0
ri_s_none:  db 'none', 0
ri_s_mhz:   db ' MHz', 0

; =============================================================================
; THE PROBE (SPEC.md 103.7.1). Read-only, in this order, and only what each
; tier and kernel allows: published slots first, then the BIOS data area and
; the ROM as MEMORY, then the few port reads an inventory needs. NO int 13h
; (SPEC.md 57.3 rule 5: the disk BIOS runs on the task stack the kernel's
; dsk_xfer is not holding the lock for), no debug registry, no CMOS (port
; 70h is the clock rung's), and under kern_mega nothing that would hand a
; real-mode segment back in a segment register.
; =============================================================================
RI_NROM     equ 12
RI_NDRV     equ 14                  ; drivers and modules listed

rim_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call ri_facts_in                ; the package's facts, copied whole
    call OSAPI_CPU_INFO
    mov [ri_cpu], ax
    call ri_p_rom
    call ri_p_cpu
    call ri_p_mem
    call ri_p_vid
    call ri_p_drv
    call ri_p_port
    call ri_p_irq
    call ri_p_dev
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_pm - CF = 1 when kern_mega runs in protected mode: segments are
; selectors, and the IVT and a BIOS's segment answers are not ours to read
ri_pm:
    test byte [ri_cpu + 1], CPU_F_PM
    jz .no
    stc
    ret
.no:
    clc
    ret

; --- the ROM BIOS: date, model, the int 15h C0h configuration table ----------
ri_p_rom:
    push es
    call ri_f000
    mov es, ax
    mov al, [es:0xFFFE]
    mov [ri_model], al
    mov si, 0xFFF5
    mov di, ri_bdate
    mov cx, 8
.d:
    mov al, [es:si]
    cmp al, ' '
    jb .bad
    cmp al, '~'
    jbe .ok
.bad:
    mov al, '?'
.ok:
    mov [di], al
    inc si
    inc di
    loop .d
    mov byte [di], 0
    pop es
    mov byte [ri_c0ok], 0
    call ri_pm                      ; int 15h C0h answers in ES:BX: a
    jc .out                         ; real-mode segment, never a selector
    push es
    push ds
    mov ah, 0xC0
    stc
    int 0x15
    pop ds
    jc .none
    or ah, ah
    jnz .none
    mov ax, [es:bx]                 ; the table's own length
    cmp ax, 5
    jb .none
    mov al, [es:bx + 2]
    mov [ri_c0], al                 ; model
    mov al, [es:bx + 3]
    mov [ri_c0 + 1], al             ; submodel
    mov al, [es:bx + 4]
    mov [ri_c0 + 2], al             ; BIOS revision
    mov al, [es:bx + 5]
    mov [ri_c0 + 3], al             ; feature byte 1
    mov byte [ri_c0ok], 1
.none:
    pop es
.out:
    ret

; --- the CPU: the coprocessor's kind, the MSW, a clock -------------------------
ri_p_cpu:
    mov byte [ri_fpu], 0
    mov byte [ri_mswok], 0
    test byte [ri_cpu + 1], CPU_F_X87
    jz .nofpu
    mov byte [ri_fpu], 1            ; an 8087 beside an 8088/8086
    cmp byte [ri_cpu], CPU_8086
    je .nofpu
    call ri_fputype
.nofpu:
    cmp byte [ri_cpu], CPU_8086
    je .out
cpu 286
    smsw ax                         ; unprivileged on every 286 and later
cpu 8086
    mov [ri_msw], ax
    mov byte [ri_mswok], 1
.out:
    ret

; ri_fputype - a 287 or a 387: 1/0 is +inf, and only a 387 tells +inf from
; -inf (the 287's default infinity is projective). The FPU's state is saved
; around it, so a program using the x87 finds it as it left it. 286+ only
cpu 386
ri_fputype:
    fnsave [ri_fsave]
    fninit
    fld1
    fldz
    fdivp st1, st0
    fld st0
    fchs
    fcompp
    fnstsw [ri_fsw]
    fwait
    frstor [ri_fsave]
    fwait
    mov byte [ri_fpu], 3
    test word [ri_fsw], 0x4000      ; C3: equal - projective, a 287
    jz .out
    mov byte [ri_fpu], 2
.out:
    ret
cpu 8086

; --- memory: the kernel's account, the pools, the upper memory map -------------
ri_p_mem:
    push es
    push ds
    pop es
    mov di, ri_kb
    call OSAPI_SYS_KB
    pop es
    call OSAPI_XMEM_CAPS
    mov [ri_xms], ax
    mov byte [ri_emsok], 0
    mov bx, DRVC_EMS << 8 | 0       ; EMSV_IDENT
    call OSAPI_DRV_CALL
    jc .noems
    cmp ax, 'EM'
    jne .noems
    mov bx, DRVC_EMS << 8 | 1       ; EMSV_CAPS
    call OSAPI_DRV_CALL
    jc .noems
    mov [ri_emsfree], ax
    mov [ri_emstot], cx
    mov [ri_emsfrm], dx
    mov byte [ri_emsok], 1
.noems:
    ; THE UPPER MEMORY MAP: option ROMs from C000h to F5FFh, each a 55AAh
    ; signature on a 2 KB boundary, its length in 512-byte units and a byte
    ; sum that should be zero. Read as memory, through one window
    mov word [ri_nroms], 0
    mov bx, 0xC000
.scan:
    cmp bx, 0xF600
    jae .done
    cmp word [ri_nroms], RI_NROM
    jae .done
    mov ax, bx
    push bx
    mov bx, ri_rwin
    call ri_win
    pop bx
    push es
    mov es, ax
    mov cx, 0x80                    ; the next 2 KB boundary
    cmp word [es:0], 0xAA55
    jne .next
    xor ax, ax
    mov al, [es:2]                  ; its length, in 512-byte blocks
    or al, al
    jz .next
    mov di, [ri_nroms]
    mov si, di
    shl di, 1
    add di, si
    shl di, 1                       ; 6 bytes a ROM: segment, KB, sum
    mov [ri_roms + di], bx
    push ax
    inc ax
    shr ax, 1
    mov [ri_roms + di + 2], ax      ; KB, rounded up
    pop ax
    mov word [ri_roms + di + 4], 0xFFFF ; not summed: past the window
    cmp al, 128
    jae .rec
    push ax
    mov cl, 9
    shl ax, cl
    mov cx, ax                      ; bytes
    xor si, si
    xor dl, dl
.sum:
    add dl, [es:si]
    inc si
    loop .sum
    xor dh, dh
    mov [ri_roms + di + 4], dx      ; 0 = the sum is right
    pop ax
.rec:
    inc word [ri_nroms]
    mov cl, 5
    shl ax, cl                      ; paragraphs, up to the next 2 KB
    add ax, 0x7F
    and ax, 0xFF80
    mov cx, ax
.next:
    pop es
    add bx, cx
    jc .done
    jmp .scan
.done:
    ret

; --- video: every display, its fullscreen modes, the EGA/VGA BIOS's answers ----
ri_p_vid:
    call OSAPI_VIDEO
    mov [ri_vw], ax
    mov [ri_vh], bx
    mov [ri_vdock], cx
    mov [ri_vkind], dx
    mov word [ri_ndisp], 0
    mov bx, 1                       ; FSX_CAPS BX = n asks about display n-1
.disp:
    cmp bx, 4
    ja .ega
    push bx
    call OSAPI_FSX_CAPS
    pop bx
    jc .ega
    mov di, bx
    dec di
    mov [ri_dkind + di], dl
    shl di, 1
    mov [ri_dmodes + di], ax
    inc word [ri_ndisp]
    inc bx
    jmp short .disp
.ega:
    mov byte [ri_dccok], 0
    mov byte [ri_egaok], 0
    mov al, [ri_vkind]
    cmp al, VID_VGA                 ; the EGA and VGA BIOS's two queries:
    je .ask                         ; registers in, registers out
    cmp al, VID_EGA
    je .ask
    cmp al, VID_VESA
    jne .out
.ask:
    push bp
    push ds
    push es
    mov ax, 0x1A00                  ; read display combination (VGA)
    int 0x10
    pop es
    pop ds
    pop bp
    cmp al, 0x1A
    jne .ega2
    mov [ri_dcc], bx
    mov byte [ri_dccok], 1
.ega2:
    push bp
    push ds
    push es
    mov ah, 0x12                    ; EGA information
    mov bx, 0xFF10
    int 0x10
    pop es
    pop ds
    pop bp
    cmp bl, 3
    ja .out
    mov [ri_egabx], bx
    mov [ri_egacx], cx
    mov byte [ri_egaok], 1
.out:
    ret

; --- drives: the BIOS's counts and every os8088 volume -------------------------
ri_p_drv:
    push es
    mov ax, 0x40
    mov es, ax
    mov al, [es:0x75]               ; fixed disks the BIOS counted
    mov [ri_nhd], al
    mov ax, [es:0x0E]               ; an AT BIOS's extended data area: the
    mov [ri_ebda], ax               ; word after LPT3's (0 on a PC/XT)
    mov al, [es:0x49]               ; the BIOS video mode the desktop left
    mov [ri_bmode], al
    pop es
    xor al, al
    mov ax, [RF(rl_equipment)]
    test al, 1
    mov byte [ri_nfd], 0
    jz .fd
    mov cl, 6
    shr al, cl
    inc al
    mov [ri_nfd], al
.fd:
    call OSAPI_VOL_SYS
    mov [ri_bootv], bl
    xor bx, bx
.vol:
    mov al, bl
    call OSAPI_VOL_KIND
    jnc .has
    mov ax, 0xFFFF
.has:
    mov di, bx
    shl di, 1
    mov [ri_vols + di], ax
    inc bx
    cmp bx, 26
    jb .vol
.out:
    ret

; --- ports: the BIOS's COM and LPT tables, each port read where it stands ------
ri_p_port:
    push es
    mov ax, 0x40
    mov es, ax
    xor bx, bx
.com:
    mov dx, [es:bx]
    mov di, bx
    shl di, 1
    shl di, 1                       ; 8 bytes a COM port
    mov [ri_com + di], dx
    or dx, dx
    jz .cnext
    call ri_uart
.cnext:
    add bx, 2
    cmp bx, 8
    jb .com
    xor bx, bx
.lpt:
    mov dx, [es:bx + 8]
    mov di, bx
    shl di, 1
    mov [ri_lpt + di], dx
    or dx, dx
    jz .lnext
    inc dx
    in al, dx                       ; the status port: read-only
    mov [ri_lpt + di + 2], al
.lnext:
    add bx, 2
    cmp bx, 6
    jb .lpt
    mov al, [es:0x96]               ; AT BIOS: bit 4 an enhanced keyboard
    mov [ri_kbd], al
    pop es
    mov dx, 0x201                   ; the game port reads FFh where nothing
    in al, dx                       ; answers; a card's idle one-shots are 0
    mov [ri_game], al
    ret

; ri_uart - DX = a UART's base, DI = its slot: the line settings (the
; divisor latch, read with interrupts held off and the LCR put straight
; back), the modem status, and the scratch register that a 16450 has and
; an 8250 has not - written and put back. Its FIFO is NOT probed: the FCR
; is write-only, and enabling it to find out would drop a byte the mouse
; or a serial lease is in the middle of
ri_uart:
    push ax
    push bx
    push dx
    mov bx, dx
    pushf
    cli
    add dx, 3
    in al, dx                       ; LCR
    mov [ri_com + di + 2], al
    mov ah, al
    or al, 0x80
    out dx, al                      ; DLAB: the divisor shows at base+0/1
    mov dx, bx
    in al, dx
    mov [ri_com + di + 4], al
    inc dx
    in al, dx
    mov [ri_com + di + 5], al
    mov dx, bx
    add dx, 3
    mov al, ah
    out dx, al                      ; LCR as it was
    popf
    mov dx, bx
    add dx, 6
    in al, dx                       ; MSR: CTS, DSR, RI, DCD
    mov [ri_com + di + 6], al
    inc dx                          ; the scratch register
    in al, dx
    mov ah, al
    mov al, 0x5A
    out dx, al
    jmp short $ + 2
    in al, dx
    cmp al, 0x5A
    jne .no
    mov al, 0xA5
    out dx, al
    jmp short $ + 2
    in al, dx
    cmp al, 0xA5
    jne .no
    mov al, 1
    jmp short .put
.no:
    xor al, al
.put:
    mov [ri_com + di + 7], al
    mov al, ah
    out dx, al                      ; ...put back
    pop dx
    pop bx
    pop ax
    ret

; --- interrupts: the PICs' masks and where each line's vector points ----------
ri_p_irq:
    in al, 0x21
    mov [ri_imr], al
    mov byte [ri_slave], 0          ; a second 8259 on an AT-class board
    cmp byte [ri_cpu], CPU_8086
    jne .slave
    cmp byte [ri_c0ok], 0
    je .imrs
    test byte [ri_c0 + 3], 0x40     ; C0h's feature byte: a slave 8259
    jz .imrs
.slave:
    mov byte [ri_slave], 1
    in al, 0xA1
    mov [ri_imr + 1], al
.imrs:
    mov byte [ri_ivtok], 0
    call ri_pm
    jc .out
    push es
    xor ax, ax
    mov es, ax
    mov si, 8 * 4                   ; IRQ 0-7: vectors 08h-0Fh
    xor di, di
    mov cx, 8
.m:
    mov ax, [es:si]
    mov [ri_ivt + di], ax
    mov ax, [es:si + 2]
    mov [ri_ivt + di + 2], ax
    add si, 4
    add di, 4
    loop .m
    mov si, 0x70 * 4                ; IRQ 8-15: vectors 70h-77h
    mov cx, 8
.s:
    mov ax, [es:si]
    mov [ri_ivt + di], ax
    mov ax, [es:si + 2]
    mov [ri_ivt + di + 2], ax
    add si, 4
    add di, 4
    loop .s
    pop es
    mov byte [ri_ivtok], 1
.out:
    ret

; --- devices: sound, the drivers' classes, every image, the programs ----------
ri_p_dev:
    call OSAPI_SND_CAPS
    mov [ri_scaps], ax
    mov [ri_sto], bl
    mov [ri_sdx], dx
    mov byte [ri_fmok], 0
    mov word [ri_mpu], 0
    mov word [ri_dac], 0
    mov byte [ri_mix], 0
    test dl, 2
    jz .drvs
    mov al, 4                       ; SND_FM_INFO
    call OSAPI_SND_FM
    jc .ident
    mov [ri_fm], ax
    mov byte [ri_fmok], 1
.ident:
    mov bx, DRVC_SOUND << 8 | 0     ; SNDV_IDENT
    call OSAPI_DRV_CALL
    jc .drvs
    cmp ax, 'SD'
    jne .drvs
    mov bx, DRVC_SOUND << 8 | 2     ; SNDV_MIDINFO
    call OSAPI_DRV_CALL
    jc .dac
    mov [ri_mpu], ax
.dac:
    test word [ri_scaps], 0x0100    ; SND_CAP_LPTDAC
    jz .mix
    mov bx, DRVC_SOUND << 8 | 6     ; SNDV_DACINFO
    call OSAPI_DRV_CALL
    jc .mix
    mov [ri_dac], ax
.mix:
    mov bx, DRVC_SOUND << 8 | 7     ; SNDV_MIXINFO
    call OSAPI_DRV_CALL
    jc .drvs
    mov [ri_mix], al
.drvs:
    xor di, di                      ; KB each class holds, FFFFh = none
    mov al, 1
.cls:
    push ax
    call OSAPI_DRV_CLASSK
    jnc .kb
    mov ax, 0xFFFF
.kb:
    mov [ri_drv + di], ax
    pop ax
    add di, 2
    inc al
    cmp al, 11
    jb .cls
    ; EVERY IMAGE: the claim heap's driver and module records, named by
    ; the kernel (kern_small names none: IMG_NAME refuses there)
    mov word [ri_ndrv], 0
    mov byte [ri_vmm], 0
    push es
    push ds
    pop es
    mov di, ri_claims
    mov cx, MEM_MAX
    call OSAPI_CLAIM_SNAPSHOT       ; CX = the records written
    mov si, ri_claims
.claim:
    cmp word [si + CLS_SEG], 0
    je .cnext
    mov ax, [si + CLS_OWN]
    mov dl, 0
    cmp ax, MEM_K_DRV
    je .img
    mov dl, 1
    cmp ax, MEM_K_MOD
    jne .cnext
.img:
    cmp word [ri_ndrv], RI_NDRV
    jae .cnext
    push cx
    push si
    push dx
    mov ax, [ri_ndrv]
    mov bx, 16
    mul bx
    add ax, ri_drvs
    mov di, ax                      ; 16 a record: 13 name, kind, KB
    mov ax, [si + CLS_SEG]
    mov bx, [si + CLS_PARA]
    push bx
    xor bx, bx
    call OSAPI_IMG_NAME             ; ES:DI = the name
    pop bx
    pop dx
    jc .unnamed
    mov [di + 13], dl
    mov cl, 6
    shr bx, cl
    mov [di + 14], bx               ; KB
    cmp word [di], 'VM'             ; VMMOUSE.DRV: the absolute pointer
    jne .counted
    cmp word [di + 2], 'MO'
    jne .counted
    mov byte [ri_vmm], 1
.counted:
    inc word [ri_ndrv]
.unnamed:
    pop si
    pop cx
.cnext:
    add si, CLS_RECSZ
    loop .claim
    mov di, ri_snap                 ; the running programs
    mov cx, INST_MAX
    call OSAPI_SYS_SNAPSHOT
    pop es
    call OSAPI_CLK_GET
    mov [ri_clk], cx
    mov [ri_clk + 2], dx
    mov [ri_clk + 4], bx
    mov [ri_clk + 6], ax
    call OSAPI_BOOT_TICKS
    mov [ri_boot], ax
    ret

; =============================================================================
; THE PAGES. Each composes from the probe's state alone: no I/O here
; =============================================================================
; ri_cpustr - SI = the CPU's name (CPUID's brand where it gave one)
ri_cpustr:
    mov si, RF(rl_brand)
.skip:
    cmp byte [si], ' '              ; brands are padded on the left
    jne .b
    inc si
    jmp short .skip
.b:
    cmp byte [si], 0
    jne .out
    mov si, ri_cpun
.out:
    ret

; ri_fpustr - SI = the coprocessor's description
ri_fpustr:
    push bx
    mov bl, [ri_fpu]
    xor bh, bh
    test word [RF(rl_features)], 1      ; CPUID's FPU bit: on the die
    jz .t
    cmp byte [RF(rl_leaf1)], 0
    je .t
    mov bl, 4
.t:
    shl bx, 1
    mov si, [ri_fpus + bx]
    pop bx
    ret

; ri_mhzput - the clock and where it came from, on the current line
ri_mhzput:
    mov ax, [PF(ri_pmhz)]
    mov dx, [PF(ri_pmhz) + 2]
    mov cx, ax
    or cx, dx
    jnz .have
    mov si, ri_s_clkunk
    jmp ri_put
.have:
    call ri_dec2
    mov si, ri_s_mhz
    call ri_put
    mov bl, [PF(ri_pmsrc)]
    xor bh, bh
    shl bx, 1
    mov si, [ri_clksrc + bx]
    jmp ri_put

; ri_vidname - AL = a VID_* kind -> SI = its name
ri_vidname:
    push bx
    mov si, ri_s_unk
    cmp al, 4
    ja .out
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [ri_vnames + bx]
.out:
    pop bx
    ret

; ri_kernstr - SI = which kernel this is, as far as a package can tell
ri_kernstr:
    mov si, ri_s_kmega
    test byte [ri_cpu + 1], CPU_F_PM
    jnz .out
    mov si, ri_s_ksmall
    test byte [ri_scaps], 0x08      ; SND_CAP_PCM_EXCL: kern_big's alone
    jz .out
    mov si, ri_s_kemu
    cmp byte [ri_vmm], 0
    jne .out
    mov si, ri_s_kbig
.out:
    ret

; ri_volcnt - AX = the volumes os8088 has
ri_volcnt:
    push bx
    push cx
    xor ax, ax
    xor bx, bx
    mov cx, 26
.v:
    cmp word [ri_vols + bx], 0xFFFF
    je .n
    inc ax
.n:
    add bx, 2
    loop .v
    pop cx
    pop bx
    ret

; ri_portcnt - BX = a table, CX = slots, DX = its stride -> AX = bases set
ri_portcnt:
    push bx
    push cx
    xor ax, ax
.p:
    cmp word [bx], 0
    je .n
    inc ax
.n:
    add bx, dx
    loop .p
    pop cx
    pop bx
    ret

; --- 1. Summary ----------------------------------------------------------------
ri_c_sum:
    mov si, ri_h_mach
    call ri_head
    mov si, ri_l_mach
    call ri_kv
    mov al, [ri_model]
    mov si, ri_models
.m:
    cmp byte [si], 0
    je .mother
    cmp al, [si]
    je .mhit
    add si, 3
    jmp short .m
.mhit:
    mov si, [si + 1]
    call ri_put
    jmp short .msub
.mother:
    mov si, ri_s_compat
    call ri_put
.msub:
    mov si, ri_s_mbyte
    call ri_put
    mov al, [ri_model]
    call ri_hex2
    cmp byte [ri_c0ok], 0
    je .mfin
    mov si, ri_s_sub
    call ri_put
    mov al, [ri_c0 + 1]
    call ri_hex2
.mfin:
    mov si, ri_s_hclose
    call ri_put
    call ri_lfin
    mov si, ri_l_bdate
    mov di, ri_bdate
    call ri_kvs
    cmp byte [ri_bt], 0
    je .cpu
    mov si, ri_l_bios
    mov di, ri_bt
    call ri_kvs
.cpu:
    mov si, ri_l_cpu
    call ri_kv
    call ri_cpustr
    call ri_put
    call ri_lfin
    mov si, ri_l_fpu
    call ri_kv
    call ri_fpustr
    call ri_put
    call ri_lfin
    mov si, ri_l_clock
    call ri_kv
    call ri_mhzput
    call ri_lfin
    call ri_gap
    mov si, ri_h_mem
    call ri_head
    mov si, ri_l_conv
    mov ax, [RF(rl_convkb)]
    mov di, ri_s_kb
    call ri_kvn
    cmp byte [RF(rl_extok)], 0
    je .free
    mov si, ri_l_ext
    mov ax, [RF(rl_extkb)]
    call ri_kvn
.free:
    mov si, ri_l_free
    call ri_kv
    mov ax, [RF(rl_free)]
    call ri_dec
    mov si, ri_s_kbfree
    call ri_put
    mov ax, [RF(rl_largest)]
    call ri_dec
    mov si, ri_s_kblarge
    call ri_put
    call ri_lfin
    call ri_gap
    mov si, ri_h_disp
    call ri_head
    mov si, ri_l_adapt
    call ri_kv
    call ri_vidput
    call ri_lfin
    mov si, ri_l_ndisp
    mov ax, [ri_ndisp]
    xor di, di
    call ri_kvn
    call ri_gap
    mov si, ri_h_stor
    call ri_head
    mov si, ri_l_nfd
    xor ax, ax
    mov al, [ri_nfd]
    call ri_kvn
    mov si, ri_l_nhd
    mov al, [ri_nhd]
    call ri_kvn
    mov si, ri_l_vols
    call ri_volcnt
    call ri_kvn
    mov si, ri_l_ncom
    mov bx, ri_com
    mov cx, 4
    mov dx, 8
    call ri_portcnt
    call ri_kvn
    mov si, ri_l_nlpt
    mov bx, ri_lpt
    mov cx, 3
    mov dx, 4
    call ri_portcnt
    call ri_kvn
    mov si, ri_l_game
    call ri_gameyn
    call ri_gap
    mov si, ri_h_os
    call ri_head
    mov si, ri_l_kern
    call ri_kernstr
    mov di, si
    mov si, ri_l_kern
    call ri_kvs
    call ri_bootline
    call ri_clockline
    mov si, ri_l_progs
    call ri_progcnt
    xor di, di
    jmp ri_kvn

ri_gameyn:                          ; SI = the label: the game port's line
    cmp byte [ri_game], 0xFF
    stc
    je .no
    clc
.no:
    jmp ri_yn

; ri_vidput - the primary display: "VGA, 640 x 480, 16 colours"
ri_vidput:
    mov al, [ri_vkind]
    call ri_vidname
    call ri_put
    mov si, ri_s_comma
    call ri_put
    mov ax, [ri_vw]
    call ri_dec
    mov si, ri_s_x
    call ri_put
    mov ax, [ri_vh]
    call ri_dec
    mov si, ri_s_comma
    call ri_put
    mov cl, [ri_vkind + 1]          ; 2 ^ bpp colours
    mov ax, 1
    xor dx, dx
.c:
    or cl, cl
    jz .p
    shl ax, 1
    rcl dx, 1
    dec cl
    jmp short .c
.p:
    call ri_dec32
    mov si, ri_s_colours
    jmp ri_put

ri_bootline:
    mov si, ri_l_boot
    call ri_kv
    mov ax, [ri_boot]
    cmp ax, 0xFFFF
    jne .t
    mov si, ri_s_unk
    call ri_put
    jmp ri_lfin
.t:
    mov cx, 100                     ; ticks x 100 / 182 = tenths of a second
    mul cx
    mov cx, 182
    div cx
    call ri_dec1
    mov si, ri_s_sec
    call ri_put
    jmp ri_lfin

ri_clockline:
    mov si, ri_l_date
    call ri_kv
    mov ax, [ri_clk]                ; CX = the year
    call ri_dec
    mov al, '-'
    call ri_ch
    mov al, [ri_clk + 3]            ; DH = the month
    call ri_d2
    mov al, '-'
    call ri_ch
    mov al, [ri_clk + 2]            ; DL = the day
    call ri_d2
    mov al, ' '
    call ri_ch
    mov al, [ri_clk + 5]            ; BH = the hour
    call ri_d2
    mov al, ':'
    call ri_ch
    mov al, [ri_clk + 4]            ; BL = the minute
    call ri_d2
    mov al, ':'
    call ri_ch
    mov al, [ri_clk + 7]            ; AH = the second
    call ri_d2
    mov si, ri_s_rtc
    test byte [ri_clk + 6], 1
    jnz .src
    mov si, ri_s_pitclk
.src:
    call ri_put
    jmp ri_lfin

ri_d2:                              ; AL = 0..99, two digits
    push ax
    xor ah, ah
    push cx
    mov cl, 10
    div cl
    add ax, '00'
    call ri_ch
    mov al, ah
    call ri_ch
    pop cx
    pop ax
    ret

; ri_progcnt - AX = the live instances in the snapshot
ri_progcnt:
    push bx
    push cx
    xor ax, ax
    mov bx, ri_snap + SS_INST
    mov cx, INST_MAX
.i:
    cmp byte [bx + SSI_STATE], 1
    jne .n
    inc ax
.n:
    add bx, SSI_RECSZ
    loop .i
    pop cx
    pop bx
    ret

; --- 2. CPU ----------------------------------------------------------------------
ri_c_cpu:
    mov si, ri_h_proc
    call ri_head
    mov si, ri_l_class
    mov di, ri_cpun
    call ri_kvs
    cmp byte [RF(rl_early)], 5
    ja .cpuid
    mov si, ri_l_bus                ; the early parts: bus and queue
    mov bl, [RF(rl_early)]
    xor bh, bh
    shl bx, 1
    mov di, [ri_buses + bx]
    call ri_kvs
.cpuid:
    cmp byte [RF(rl_cpuid)], 0
    je .clock
    mov si, ri_l_vendor
    mov di, RF(rl_vendor)
    call ri_kvs
    cmp byte [RF(rl_brand)], 0
    je .sig
    mov si, ri_l_brand
    call ri_cpustr
    mov di, si
    mov si, ri_l_brand
    call ri_kvs
.sig:
    cmp byte [RF(rl_leaf1)], 0
    je .clock
    mov si, ri_l_fms
    call ri_kv
    mov ax, [RF(rl_family)]
    call ri_dec
    mov al, '/'
    call ri_ch
    mov ax, [RF(rl_model)]
    call ri_dec
    mov al, '/'
    call ri_ch
    mov ax, [RF(rl_stepping)]
    call ri_dec
    mov si, ri_s_sigh
    call ri_put
    mov ax, [RF(rl_signature) + 2]
    call ri_hex4
    mov ax, [RF(rl_signature)]
    call ri_hexh
    mov al, ')'
    call ri_ch
    call ri_lfin
    mov si, ri_l_feat               ; CPUID EDX: the named bits, wrapped
    call ri_kv
    mov bx, ri_fnames
    mov dx, [RF(rl_features)]
    mov cx, 16
    call ri_flagbits
    mov dx, [RF(rl_features) + 2]
    mov cx, 16
    call ri_flagbits
    call ri_lfin
.clock:
    mov si, ri_l_clock
    call ri_kv
    call ri_mhzput
    call ri_lfin
    call ri_gap
    mov si, ri_h_mode
    call ri_head
    cmp byte [ri_mswok], 0
    je .flags
    mov si, ri_l_msw
    call ri_kv
    mov ax, [ri_msw]
    call ri_hexh
    mov si, ri_s_real
    test al, 1
    jz .pe
    mov si, ri_s_prot
.pe:
    call ri_put
    call ri_lfin
.flags:
    mov bl, CPU_F_A20
    mov si, ri_l_a20
    call ri_cpuf
    mov bl, CPU_F_HMA
    mov si, ri_l_hma
    call ri_cpuf
    mov bl, CPU_F_UNREAL
    mov si, ri_l_unreal
    call ri_cpuf
    mov bl, CPU_F_PM
    mov si, ri_l_pm
    call ri_cpuf
    call ri_gap
    mov si, ri_h_fpu
    call ri_head
    mov si, ri_l_fpu
    call ri_fpustr
    mov di, si
    mov si, ri_l_fpu
    call ri_kvs
    call ri_gap
    mov si, ri_n_cpu1
    call ri_note
    mov si, ri_n_cpu2
    call ri_note
    mov si, ri_n_cpu3
    jmp ri_note

ri_cpuf:                            ; BL = a CPU_F_* bit, SI = its label
    test [ri_cpu + 1], bl
    stc
    jz .no
    clc
.no:
    jmp ri_yn

; ri_flagbits - DX = 16 feature bits, BX -> their 4-byte names (a space
; for an unnamed bit): each set one written, a new line when this one fills
ri_flagbits:
.b:
    shr dx, 1
    jnc .next
    cmp byte [bx], ' '
    je .next
    push ax
    mov ax, [ri_lcur]
    add ax, 6
    cmp ax, [ri_lend]
    jbe .fits
    call ri_lfin                    ; the line is full: carry on beneath
    mov si, ri_s_empty
    call ri_kv
.fits:
    pop ax
    push cx
    mov si, bx
    mov cx, 4
.c:
    lodsb
    cmp al, ' '
    je .sp
    call ri_ch
.sp:
    loop .c
    mov al, ' '
    call ri_ch
    pop cx
.next:
    add bx, 4
    loop .b
    ret

; --- 3. Memory ------------------------------------------------------------------
ri_c_mem:
    mov si, ri_h_conv
    call ri_head
    mov si, ri_l_conv
    mov ax, [RF(rl_convkb)]
    mov di, ri_s_kb
    call ri_kvn
    mov dx, [RF(rl_convkb)]             ; every bar a share of it
    mov si, ri_l_kern2
    mov ax, [ri_kb + SK_KERN]
    call ri_bar
    mov si, ri_l_bufs
    mov ax, [ri_kb + SK_BUF]
    call ri_bar
    mov si, ri_l_heap
    mov ax, [ri_kb + SK_HEAP]
    call ri_bar
    mov si, ri_l_claim
    mov ax, [ri_kb + SK_CLAIM]
    call ri_bar
    mov si, ri_l_kclaim
    mov ax, [ri_kb + SK_KCLAIM]
    call ri_bar
    mov si, ri_l_osfree
    mov ax, [RF(rl_free)]
    call ri_bar
    mov si, ri_l_large
    mov ax, [RF(rl_largest)]
    call ri_bar
    call ri_gap
    mov si, ri_h_ext
    call ri_head
    cmp byte [ri_cpu], CPU_8086
    jne .ext
    mov si, ri_n_noext
    call ri_note
    jmp short .ems
.ext:
    mov si, ri_l_ext
    mov di, ri_s_na
    cmp byte [RF(rl_extok)], 0
    je .extna
    mov ax, [RF(rl_extkb)]
    mov di, ri_s_kb
    call ri_kvn
    jmp short .e820
.extna:
    call ri_kvs
.e820:
    cmp byte [RF(rl_mapok)], 0
    je .xms
    mov si, ri_l_e820
    mov ax, [RF(rl_ramkb)]
    mov dx, [RF(rl_ramkb) + 2]
    call ri_kv
    call ri_dec32
    mov si, ri_s_kb
    call ri_put
    call ri_lfin
.xms:
    mov si, ri_l_xms
    mov ax, [ri_xms]
    mov di, ri_s_kbfr
    call ri_kvn
    mov si, ri_l_hma
    mov bl, CPU_F_HMA
    call ri_cpuf
.ems:
    call ri_ebdaline
    mov si, ri_l_ems
    cmp byte [ri_emsok], 0
    jne .emsy
    mov di, ri_s_noems
    call ri_kvs
    jmp short .umb
.emsy:
    call ri_kv
    mov ax, [ri_emsfree]
    call ri_dec
    mov al, '/'
    call ri_ch
    mov ax, [ri_emstot]
    call ri_dec
    mov si, ri_s_pages
    call ri_put
    mov ax, [ri_emsfrm]
    call ri_hexh
    call ri_lfin
.umb:
    call ri_gap
    mov si, ri_h_umb
    call ri_head
    mov si, ri_t_umb
    call ri_note
    mov si, ri_s_vga                ; the adapter's own memory first
    mov ax, 0xA000
    mov cx, 128
    mov bl, [ri_vkind]
    cmp bl, VID_HERC
    jne .vcga
    mov ax, 0xB000
    mov cx, 64
    mov si, ri_s_hercmem
    jmp short .vmem
.vcga:
    cmp bl, VID_CGA
    jne .vmem
    mov ax, 0xB800
    mov cx, 16
    mov si, ri_s_cgamem
.vmem:
    call ri_umbline
    xor bx, bx
.rom:
    cmp bx, [ri_nroms]
    jae .bios
    push bx
    mov di, bx
    shl di, 1
    add di, bx
    shl di, 1
    mov ax, [ri_roms + di]
    mov cx, [ri_roms + di + 2]
    mov si, ri_s_optrom
    cmp ax, 0xC000
    jne .other
    cmp byte [ri_vkind], VID_VGA
    je .vbios
    cmp byte [ri_vkind], VID_EGA
    jne .other
.vbios:
    mov si, ri_s_vrom
.other:
    push word [ri_roms + di + 4]
    call ri_umbline.open
    pop ax
    mov si, ri_s_sumna
    cmp ax, 0xFFFF
    je .sum
    mov si, ri_s_sumok
    or ax, ax
    jz .sum
    mov si, ri_s_sumbad
.sum:
    call ri_put
    call ri_lfin
    pop bx
    inc bx
    jmp short .rom
.bios:
    mov si, ri_s_sysbios
    mov ax, 0xF000
    mov cx, 64
    jmp ri_umbline

; ri_ebdaline - an AT BIOS's extended data area, where it says it has one:
; a segment in 0040:000E between the top of conventional memory and A000h,
; and its size in KB from its own first byte
ri_ebdaline:
    mov ax, [ri_ebda]
    cmp ax, 0x8000
    jb .out
    cmp ax, 0xA000
    jae .out
    mov si, ri_l_ebda
    call ri_kv
    call ri_hexh
    mov si, ri_s_ebda2
    call ri_put
    jmp ri_lfin
.out:
    ret

; ri_equip - the BIOS equipment word (int 11h), decoded: what the BIOS
; was told at POST, beside what os8088 found
ri_equip:
    mov si, ri_h_equip
    call ri_head
    mov si, ri_l_eqw
    call ri_kv
    mov ax, [RF(rl_equipment)]
    call ri_hexh
    call ri_lfin
    mov si, ri_l_eqvid
    mov bx, [RF(rl_equipment)]
    mov cl, 4
    shr bx, cl
    and bx, 3
    shl bx, 1
    mov di, [ri_eqvids + bx]
    call ri_kvs
    mov si, ri_l_eqx87
    test byte [RF(rl_equipment)], 2
    stc
    jz .x
    clc
.x:
    call ri_yn
    mov si, ri_l_eqgame
    test byte [RF(rl_equipment) + 1], 0x10
    stc
    jz .g
    clc
.g:
    call ri_yn
    mov si, ri_l_bmode
    call ri_kv
    mov al, [ri_bmode]
    call ri_hex2
    mov al, 'h'
    call ri_ch
    jmp ri_lfin

; ri_umbline - AX = a segment, CX = KB, SI = what it is: "C000-C7FF  32 KB  x"
; (.open leaves the line open for a note)
ri_umbline:
    call .open
    jmp ri_lfin
.open:
    push ax
    push cx
    push dx
    push ax
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    pop ax
    call ri_hex4
    mov dx, ax
    mov al, '-'
    call ri_ch
    mov ax, cx
    push cx
    mov cl, 6
    shl ax, cl
    pop cx
    add ax, dx
    dec ax
    call ri_hex4
    mov al, 13
    call ri_col
    mov ax, cx
    call ri_dec
    mov al, ' '
    call ri_ch
    mov al, 'K'
    call ri_ch
    mov al, 'B'
    call ri_ch
    mov al, 22
    call ri_col
    call ri_put
    mov al, ' '
    call ri_ch
    pop dx
    pop cx
    pop ax
    ret

; --- 4. Video -------------------------------------------------------------------
ri_c_vid:
    mov si, ri_h_prim
    call ri_head
    mov si, ri_l_adapt
    call ri_kv
    call ri_vidput
    call ri_lfin
    mov si, ri_l_dock
    mov ax, [ri_vdock]
    xor di, di
    call ri_kvn
    mov si, ri_l_bmodes
    call ri_kv
    mov bl, [ri_vkind]
    xor bh, bh
    cmp bl, 4
    jbe .bm
    mov bl, 0
.bm:
    shl bx, 1
    mov si, [ri_bmodes + bx]
    call ri_put
    call ri_lfin
    call ri_gap
    xor bx, bx                      ; each display and its fullscreen modes
.disp:
    cmp bx, [ri_ndisp]
    jae .bios
    mov al, RIA_HEAD
    call ri_lnew
    mov al, ' '
    call ri_ch
    mov si, ri_s_dispn
    call ri_put
    mov ax, bx
    inc ax
    call ri_dec
    mov si, ri_s_colon
    call ri_put
    mov al, [ri_dkind + bx]
    call ri_vidname
    call ri_put
    call ri_lfin
    push bx
    shl bx, 1
    mov dx, [ri_dmodes + bx]
    xor cx, cx                      ; CX = the FSXM_* id
.mode:
    cmp cx, 9
    jae .mdone
    shr dx, 1
    jnc .mnext
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    mov si, cx
    shl si, 1
    mov si, [ri_fsxm + si]
    call ri_put
    call ri_lfin
.mnext:
    inc cx
    jmp short .mode
.mdone:
    pop bx
    inc bx
    jmp short .disp
.bios:
    call ri_gap
    mov si, ri_h_vbios
    call ri_head
    cmp byte [ri_dccok], 0
    jne .dcc
    cmp byte [ri_egaok], 0
    jne .ega
    mov si, ri_n_novb
    call ri_note
    jmp short .rom
.dcc:
    mov si, ri_l_dcca
    mov al, [ri_dcc]
    call ri_dccline
    mov si, ri_l_dccb
    mov al, [ri_dcc + 1]
    call ri_dccline
.ega:
    cmp byte [ri_egaok], 0
    je .rom
    mov si, ri_l_vram
    call ri_kv
    mov al, [ri_egabx]              ; BL = 0..3: 64 KB x (BL + 1)
    inc ax
    mov ah, 64
    mul ah
    call ri_dec
    mov si, ri_s_kb
    call ri_put
    mov si, ri_s_egacol
    cmp byte [ri_egabx + 1], 0
    je .ecol
    mov si, ri_s_egamono
.ecol:
    call ri_put
    call ri_lfin
    mov si, ri_l_esw
    call ri_kv
    mov al, [ri_egacx]              ; CL = the switch settings
    call ri_hex2
    mov al, 'h'
    call ri_ch
    call ri_lfin
.rom:
    cmp word [ri_nroms], 0
    je .out
    cmp word [ri_roms], 0xC000
    jne .out
    mov si, ri_l_vrom
    mov ax, [ri_roms + 2]
    mov di, ri_s_kbat
    call ri_kvn
.out:
    ret

; ri_dccline - SI = a label, AL = a display combination code
ri_dccline:
    call ri_kv
    push si
    mov si, ri_s_unk
    cmp al, 0x0C
    ja .p
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [ri_dccs + bx]
    pop bx
.p:
    call ri_put
    pop si
    jmp ri_lfin

; --- 5. Drives ------------------------------------------------------------------
ri_c_drv:
    mov si, ri_h_bdrv
    call ri_head
    xor ax, ax
    mov si, ri_l_nfd
    mov al, [ri_nfd]
    xor di, di
    call ri_kvn
    mov si, ri_l_nhd
    mov al, [ri_nhd]
    call ri_kvn
    call ri_gap
    mov si, ri_h_vols
    call ri_head
    mov si, ri_t_vols
    call ri_note
    xor bx, bx
.vol:
    cmp bx, 26
    jae .cur
    push bx
    shl bx, 1
    mov ax, [ri_vols + bx]
    pop bx
    cmp ax, 0xFFFF
    je .vnext
    push ax
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    mov al, bl
    add al, 'A'
    call ri_ch
    mov al, ':'
    call ri_ch
    mov al, 8
    call ri_col
    pop ax
    mov si, ri_s_remov
    or al, al
    jz .k
    mov si, ri_s_fixed
.k:
    call ri_put
    mov al, 22
    call ri_col
    mov si, ri_s_unk
    cmp ah, 2
    ja .t
    push bx
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov si, [ri_vts + bx]
    pop bx
.t:
    call ri_put
    cmp bl, [ri_bootv]
    jne .nb
    mov si, ri_s_boot
    call ri_put
.nb:
    cmp bl, [PF(ri_pcurv)]
    jne .nc
    mov si, ri_s_here
    call ri_put
.nc:
    call ri_lfin
.vnext:
    inc bx
    jmp .vol
.cur:
    cmp byte [PF(ri_pvsok)], 0
    je .drivers
    call ri_gap
    mov al, RIA_HEAD
    call ri_lnew
    mov al, ' '
    call ri_ch
    mov si, ri_s_vhere
    call ri_put
    mov al, [PF(ri_pcurv)]
    add al, 'A'
    call ri_ch
    mov al, ':'
    call ri_ch
    call ri_lfin
    mov si, ri_l_clus
    mov ax, [PF(ri_pvspc)]
    mul word [PF(ri_pvbps)]
    mov di, ri_s_bytes
    call ri_kvn
    mov ax, [PF(ri_pvtot)]               ; clusters -> KB: x sectors x bytes / 1024
    call ri_clkb
    mov bx, ax                      ; BX = total KB (saturating)
    mov si, ri_l_vtot
    mov di, ri_s_kb
    call ri_kvn
    mov ax, [PF(ri_pvtot)]
    sub ax, [PF(ri_pvfree)]
    call ri_clkb
    mov dx, bx
    mov si, ri_l_vused
    call ri_bar
    mov ax, [PF(ri_pvfree)]
    call ri_clkb
    mov si, ri_l_vfree
    call ri_bar
.drivers:
    call ri_gap
    mov si, ri_h_ddrv
    call ri_head
    mov si, ri_l_hdd
    mov bx, (DRVC_DISK - 1) * 2
    call ri_drvline
    mov si, ri_l_cd
    mov bx, (DRVC_CD - 1) * 2
    call ri_drvline
    mov si, ri_l_netv
    mov bx, (DRVC_FILE - 1) * 2
    call ri_drvline
    mov si, ri_l_ramd
    mov bx, (DRVC_FILE2 - 1) * 2
    call ri_drvline
    mov si, ri_l_http
    mov bx, (DRVC_HTTP - 1) * 2
    call ri_drvline
    call ri_gap
    mov si, ri_n_geom
    call ri_note
    mov si, ri_n_geom2
    jmp ri_note

; ri_clkb - AX = clusters -> AX = KB on the current volume (saturating)
ri_clkb:
    push cx
    push dx
    mul word [PF(ri_pvspc)]              ; sectors
    mov cx, [PF(ri_pvbps)]
    shr cx, 1                       ; half-KB units a sector...
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; ...512 bytes = 1
    or cx, cx
    jnz .m
    inc cx
.m:
    push dx
    mul cx
    pop cx
    shr dx, 1                       ; half-KB -> KB (DX:AX)
    rcr ax, 1
    or dx, cx
    jz .out
    mov ax, 0xFFFF
.out:
    pop dx
    pop cx
    ret

; ri_drvline - SI = a label, BX = its class's slot in ri_drv: loaded and KB
ri_drvline:
    mov ax, [ri_drv + bx]
    cmp ax, 0xFFFF
    jne .on
    mov di, ri_s_notld
    jmp ri_kvs
.on:
    call ri_kv
    push si
    mov si, ri_s_loaded
    call ri_put
    call ri_dec
    mov si, ri_s_kbp
    call ri_put
    pop si
    jmp ri_lfin

; --- 6. Ports -------------------------------------------------------------------
ri_c_port:
    mov si, ri_h_com
    call ri_head
    mov si, ri_t_com
    call ri_note
    xor bx, bx
.com:
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    mov si, ri_s_com
    call ri_put
    mov ax, bx
    inc ax
    call ri_dec
    mov al, 8
    call ri_col
    mov di, bx
    shl di, 1
    shl di, 1
    shl di, 1
    mov ax, [ri_com + di]
    or ax, ax
    jnz .cpresent
    mov si, ri_s_noport
    call ri_put
    jmp .cfin
.cpresent:
    call ri_hexh
    mov al, 15
    call ri_col
    mov si, ri_s_8250
    cmp byte [ri_com + di + 7], 0
    je .chip
    mov si, ri_s_16450
.chip:
    call ri_put
    mov al, 27
    call ri_col
    mov cx, [ri_com + di + 4]       ; the divisor: 115200 / it
    jcxz .nobaud
    mov ax, 0xC200
    mov dx, 1
    cmp cx, 2
    jb .nobaud
    div cx
    call ri_dec
    jmp short .lcr
.nobaud:
    mov si, ri_s_q
    call ri_put
.lcr:
    mov al, ' '
    call ri_ch
    mov ah, [ri_com + di + 2]       ; LCR: word length, parity, stop bits
    mov al, ah
    and al, 3
    add al, '5'
    call ri_ch
    mov al, ah
    mov cl, 3
    shr al, cl
    and al, 7
    push bx
    mov bx, ri_parity
    xlatb
    pop bx
    call ri_ch
    mov al, '1'
    test ah, 4
    jz .stop
    mov al, '2'
.stop:
    call ri_ch
    mov al, 42
    call ri_col
    mov ah, [ri_com + di + 6]       ; MSR: the lines the far end drives
    mov si, ri_s_cts
    test ah, 0x10
    jz .dsr
    call ri_put
.dsr:
    mov si, ri_s_dsr
    test ah, 0x20
    jz .dcd
    call ri_put
.dcd:
    mov si, ri_s_dcd
    test ah, 0x80
    jz .ri
    call ri_put
.ri:
    mov si, ri_s_ring
    test ah, 0x40
    jz .cfin
    call ri_put
.cfin:
    call ri_lfin
    inc bx
    cmp bx, 4
    jb .com
    call ri_gap
    mov si, ri_h_lpt
    call ri_head
    xor bx, bx
.lpt:
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    mov si, ri_s_lpt
    call ri_put
    mov ax, bx
    inc ax
    call ri_dec
    mov al, 8
    call ri_col
    mov di, bx
    shl di, 1
    shl di, 1
    mov ax, [ri_lpt + di]
    or ax, ax
    jnz .lpresent
    mov si, ri_s_noport
    call ri_put
    jmp short .lfin
.lpresent:
    call ri_hexh
    mov al, 15
    call ri_col
    mov ah, [ri_lpt + di + 2]       ; status: /BUSY, /ACK, PE, SLCT, /ERROR
    mov si, ri_s_busy
    test ah, 0x80
    jz .b
    mov si, ri_s_ready
.b:
    call ri_put
    mov si, ri_s_online
    test ah, 0x10
    jnz .o
    mov si, ri_s_offline
.o:
    call ri_put
    test ah, 0x20
    jz .e
    mov si, ri_s_paper
    call ri_put
.e:
    test ah, 0x08
    jnz .lfin
    mov si, ri_s_fault
    call ri_put
.lfin:
    call ri_lfin
    inc bx
    cmp bx, 3
    jb .lpt
    call ri_gap
    mov si, ri_h_oth
    call ri_head
    mov si, ri_l_game
    call ri_gameyn
    mov si, ri_l_kbd
    mov di, ri_s_kbxt
    cmp byte [ri_cpu], CPU_8086
    je .kb
    mov di, ri_s_kbatc
    test byte [ri_kbd], 0x10
    jz .kb
    mov di, ri_s_kbenh
.kb:
    call ri_kvs
    call ri_gap
    mov si, ri_n_fifo
    call ri_note
    mov si, ri_n_fifo2
    jmp ri_note

; --- 7. IRQ/DMA -----------------------------------------------------------------
ri_c_irq:
    mov si, ri_h_irq
    call ri_head
    mov si, ri_t_irq
    call ri_note
    xor bx, bx
.irq:
    cmp bx, 8
    jb .one
    cmp byte [ri_slave], 0
    je .dma
    cmp bx, 16
    jae .dma
.one:
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    cmp bx, 10
    jae .col
    mov al, 3
.col:
    call ri_col
    mov ax, bx
    call ri_dec
    mov al, 7
    call ri_col
    mov si, bx                      ; the standard use: the XT's and the AT's
    shl si, 1
    mov si, [ri_irqs + si]
    cmp byte [ri_slave], 0
    je .use
    cmp bx, 2
    jne .use5
    mov si, ri_i_casc
.use5:
    cmp bx, 5
    jne .use
    mov si, ri_i_lpt2
.use:
    call ri_put
    mov al, 32
    call ri_col
    mov cl, bl                      ; the mask: a set bit is masked off
    and cl, 7
    mov al, 1
    shl al, cl
    mov ah, [ri_imr]
    cmp bx, 8
    jb .m
    mov ah, [ri_imr + 1]
.m:
    mov si, ri_s_on
    test ah, al
    jz .mput
    mov si, ri_s_off
.mput:
    call ri_put
    mov al, 42
    call ri_col
    cmp byte [ri_ivtok], 0
    je .ifin
    mov di, bx
    shl di, 1
    shl di, 1
    mov dx, [ri_ivt + di + 2]
    mov ax, dx
    call ri_hex4
    mov al, ':'
    call ri_ch
    mov ax, [ri_ivt + di]
    call ri_hex4
    mov al, 53
    call ri_col
    mov si, ri_o_bios               ; who the vector belongs to, by where
    cmp dx, 0xF000                  ; it points
    jae .own
    mov si, ri_o_rom
    cmp dx, 0xC000
    jae .own
    mov si, ri_o_kern
    cmp dx, KERNEL_SEG
    je .own
    mov si, ri_o_low
    cmp dx, KERNEL_SEG
    jb .own
    mov si, ri_o_drv
.own:
    call ri_put
.ifin:
    call ri_lfin
    inc bx
    jmp .irq
.dma:
    cmp byte [ri_ivtok], 0
    jne .dhead
    mov si, ri_n_noivt
    call ri_note
.dhead:
    call ri_gap
    mov si, ri_h_dma
    call ri_head
    xor bx, bx
.ch:
    cmp bx, 4
    jb .chone
    cmp byte [ri_slave], 0
    je .dout
    cmp bx, 8
    jae .dout
.chone:
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 3
    call ri_col
    mov ax, bx
    call ri_dec
    mov al, 7
    call ri_col
    mov si, ri_d_8
    cmp bx, 4
    jb .w
    mov si, ri_d_16
.w:
    call ri_put
    mov al, 16
    call ri_col
    mov si, bx
    shl si, 1
    mov si, [ri_dmas + si]
    cmp byte [ri_slave], 0
    je .dput
    cmp bx, 0
    jne .d3
    mov si, ri_d_free
.d3:
    cmp bx, 3
    jne .dput
    mov si, ri_d_free
.dput:
    call ri_put
    call ri_lfin
    inc bx
    jmp short .ch
.dout:
    call ri_gap
    mov si, ri_n_dma
    call ri_note
    mov si, ri_n_dma2
    jmp ri_note

; --- 8. Devices -----------------------------------------------------------------
ri_c_dev:
    mov si, ri_h_snd
    call ri_head
    mov si, ri_l_spk
    clc
    call ri_yn
    mov si, ri_l_sdrv
    test byte [ri_sdx], 2
    stc
    jz .sd
    clc
.sd:
    call ri_yn
    mov si, ri_l_scaps              ; the capability word, named
    call ri_kv
    mov bx, ri_cnames
    mov dx, [ri_scaps]
    mov cx, 12
    call ri_flagbits
    call ri_lfin
    cmp byte [ri_fmok], 0
    je .mpu
    mov si, ri_l_fm
    call ri_kv
    mov si, ri_s_opl2
    cmp byte [ri_fm], 3
    jne .fmn
    mov si, ri_s_opl3
.fmn:
    call ri_put
    xor ax, ax
    mov al, [ri_fm + 1]
    call ri_dec
    mov si, ri_s_chans
    call ri_put
    call ri_lfin
.mpu:
    cmp word [ri_mpu], 0
    je .dac
    mov si, ri_l_mpu
    mov ax, [ri_mpu]
    call ri_kv
    call ri_hexh
    call ri_lfin
.dac:
    cmp word [ri_dac], 0
    je .mix
    mov si, ri_l_dac
    mov ax, [ri_dac]
    call ri_kv
    call ri_hexh
    call ri_lfin
.mix:
    cmp byte [ri_mix], 0
    je .ptr
    mov si, ri_l_mix
    call ri_kv
    mov bl, [ri_mix]
    xor bh, bh
    mov si, ri_s_unk
    cmp bl, 3
    ja .mx
    shl bx, 1
    mov si, [ri_mixs + bx]
.mx:
    call ri_put
    call ri_lfin
.ptr:
    call ri_gap
    mov si, ri_h_ptr
    call ri_head
    mov si, ri_l_mouse
    mov di, ri_s_musb
    cmp word [ri_drv + (DRVC_POINT - 1) * 2], 0xFFFF
    jne .mo
    mov di, ri_s_mvmw
    cmp byte [ri_vmm], 0
    jne .mo
    mov di, ri_s_mser
.mo:
    call ri_kvs
    mov si, ri_l_net
    mov bx, (DRVC_NET - 1) * 2
    call ri_drvline
    mov si, ri_l_emsd
    mov bx, (DRVC_EMS - 1) * 2
    call ri_drvline
    call ri_clockline
    call ri_gap
    call ri_equip
    call ri_gap
    mov si, ri_h_imgs
    call ri_head
    cmp word [ri_ndrv], 0
    jne .imgs
    mov si, ri_n_noimg
    call ri_note
    jmp short .progs
.imgs:
    xor bx, bx
.img:
    cmp bx, [ri_ndrv]
    jae .progs
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    mov ax, 16
    mul bx
    add ax, ri_drvs
    mov si, ax
    call ri_put
    mov al, 18
    call ri_col
    mov di, si
    mov si, ri_s_drv
    cmp byte [di + 13], 0
    je .ik
    mov si, ri_s_mod
.ik:
    call ri_put
    mov al, 30
    call ri_col
    mov ax, [di + 14]
    call ri_dec
    mov si, ri_s_kb
    call ri_put
    call ri_lfin
    inc bx
    jmp short .img
.progs:
    call ri_gap
    mov si, ri_h_progs
    call ri_head
    mov bx, ri_snap + SS_INST
    mov cx, INST_MAX
.p:
    cmp byte [bx + SSI_STATE], 1
    jne .pn
    mov al, RIA_TEXT
    call ri_lnew
    mov al, 2
    call ri_col
    lea si, [bx + SSI_NAME]
    mov byte [bx + SSI_NAME + 15], 0
    call ri_put
    mov al, 22
    call ri_col
    mov si, ri_s_pkg
    test byte [bx + SSI_KIND], KIND_PKG
    jnz .pk
    mov si, ri_s_builtin
.pk:
    call ri_put
    mov al, 34
    call ri_col
    mov ax, [bx + SSI_KB]
    call ri_dec
    mov si, ri_s_kb
    call ri_put
    call ri_lfin
.pn:
    add bx, SSI_RECSZ
    loop .p
    ret

; =============================================================================
; Text
; =============================================================================
ri_s_empty: db 0
ri_s_comma: db ', ', 0
ri_s_colon: db ': ', 0
ri_s_x:     db ' x ', 0
ri_s_q:     db '?', 0
ri_s_unk:   db 'unknown', 0
ri_s_colours: db ' colours', 0
ri_s_sec:   db ' s, power-on to the first desktop frame', 0
ri_s_kbfree: db ' KB free, largest run ', 0
ri_s_kblarge: db ' KB', 0
ri_s_kbfr:  db ' KB free in the pool', 0
ri_s_kbat:  db ' KB at C000h', 0
ri_s_kbp:   db ' KB)', 0
ri_s_bytes: db ' bytes', 0
ri_s_pages: db ' pages free (16 KB each), frame ', 0
ri_s_mbyte: db ' (model ', 0
ri_s_sub:   db 'h, submodel ', 0
ri_s_hclose: db 'h)', 0
ri_s_compat: db 'PC compatible', 0
ri_s_rtc:   db '  (RTC)', 0
ri_s_pitclk: db '  (no RTC: timer)', 0
ri_s_clkunk: db 'not measured for this processor family', 0

ri_h_mach:  db 'Machine', 0
ri_h_mem:   db 'Memory', 0
ri_h_disp:  db 'Display', 0
ri_h_stor:  db 'Storage and ports', 0
ri_h_os:    db 'Operating system', 0
ri_l_mach:  db 'Machine', 0
ri_l_bdate: db 'BIOS date', 0
ri_l_bios:  db 'BIOS', 0
ri_l_cpu:   db 'Processor', 0
ri_l_fpu:   db 'Coprocessor', 0
ri_l_clock: db 'Clock', 0
ri_l_conv:  db 'Conventional (BIOS)', 0
ri_l_ext:   db 'Extended (BIOS 88h)', 0
ri_l_free:  db 'os8088 heap', 0
ri_l_adapt: db 'Adapter', 0
ri_l_ndisp: db 'Displays', 0
ri_l_nfd:   db 'Floppy drives (BIOS)', 0
ri_l_nhd:   db 'Fixed disks (BIOS)', 0
ri_l_vols:  db 'os8088 volumes', 0
ri_l_ncom:  db 'Serial ports', 0
ri_l_nlpt:  db 'Parallel ports', 0
ri_l_game:  db 'Game port', 0
ri_l_kern:  db 'Kernel', 0
ri_l_boot:  db 'Boot time', 0
ri_l_date:  db 'Date and time', 0
ri_l_progs: db 'Programs running', 0
ri_s_kbig:  db 'kern_big (real mode)', 0
ri_s_ksmall: db 'kern_small (the 128 KB kernel)', 0
ri_s_kemu:  db 'kern_emu (kern_big + absolute pointer)', 0
ri_s_kmega: db 'kern_mega (286 protected mode)', 0

ri_models:  db 0xFF
            dw ri_m_ff
            db 0xFE
            dw ri_m_fe
            db 0xFD
            dw ri_m_fd
            db 0xFC
            dw ri_m_fc
            db 0xFB
            dw ri_m_fb
            db 0xFA
            dw ri_m_fa
            db 0xF9
            dw ri_m_f9
            db 0xF8
            dw ri_m_f8
            db 0
ri_m_ff:    db 'IBM PC', 0
ri_m_fe:    db 'IBM PC XT or Portable', 0
ri_m_fd:    db 'IBM PCjr', 0
ri_m_fc:    db 'IBM AT class', 0
ri_m_fb:    db 'IBM PC XT (1986)', 0
ri_m_fa:    db 'IBM PS/2 Model 30', 0
ri_m_f9:    db 'IBM PC Convertible', 0
ri_m_f8:    db 'IBM PS/2 Model 80', 0

ri_fpus:    dw ri_f_none, ri_f_8087, ri_f_287, ri_f_387, ri_f_on
ri_f_none:  db 'none', 0
ri_f_8087:  db '8087', 0
ri_f_287:   db '80287 (projective infinity)', 0
ri_f_387:   db '80387 or later (affine infinity)', 0
ri_f_on:    db 'on the processor (CPUID FPU)', 0
ri_clksrc:  dw ri_s_empty, ri_c_book, ri_c_tsc, ri_c_nom
ri_c_book:  db '  (MUL timed against book cycles)', 0
ri_c_tsc:   db '  (time-stamp counter)', 0
ri_c_nom:   db '  (CPUID nominal)', 0
ri_vnames:  dw ri_v_vga, ri_v_herc, ri_v_cga, ri_v_ega, ri_v_vesa
ri_v_vga:   db 'VGA', 0
ri_v_herc:  db 'Hercules', 0
ri_v_cga:   db 'CGA', 0
ri_v_ega:   db 'EGA', 0
ri_v_vesa:  db 'VESA', 0

; --- CPU ---
ri_h_proc:  db 'Processor', 0
ri_h_mode:  db 'Modes and memory features', 0
ri_h_fpu:   db 'Coprocessor', 0
ri_l_class: db 'Class', 0
ri_l_bus:   db 'Bus and queue', 0
ri_l_vendor: db 'Vendor', 0
ri_l_brand: db 'Name', 0
ri_l_fms:   db 'Family/model/stepping', 0
ri_s_sigh:  db '  (signature ', 0
ri_l_feat:  db 'Features (CPUID)', 0
ri_l_msw:   db 'Machine status word', 0
ri_s_real:  db '  real mode', 0
ri_s_prot:  db '  PROTECTED mode', 0
ri_l_a20:   db 'A20 line open', 0
ri_l_hma:   db 'HMA in use', 0
ri_l_unreal: db 'Unreal mode armed', 0
ri_l_pm:    db 'Protected-mode kernel', 0
ri_buses:   dw ri_b_88, ri_b_86, ri_b_v20, ri_b_v30, ri_b_188, ri_b_186
ri_b_88:    db '8-bit bus, 4-byte prefetch queue', 0
ri_b_86:    db '16-bit bus, 6-byte prefetch queue', 0
ri_b_v20:   db 'NEC: 8-bit bus, 80186 instructions, 8080 mode', 0
ri_b_v30:   db 'NEC: 16-bit bus, 80186 instructions, 8080 mode', 0
ri_b_188:   db '8-bit bus, integrated peripherals', 0
ri_b_186:   db '16-bit bus, integrated peripherals', 0
ri_n_cpu1:  db 'The class is told apart by behaviour: shift-count masking, the', 0
ri_n_cpu2:  db 'restart of an interrupted string op, the prefetch queue, then', 0
ri_n_cpu3:  db 'the FLAGS register and CPUID. Nothing is written to the CPU.', 0
ri_fnames:  db 'FPU ', 'VME ', 'DE  ', 'PSE ', 'TSC ', 'MSR ', 'PAE ', 'MCE '
            db 'CX8 ', 'APIC', '    ', 'SEP ', 'MTRR', 'PGE ', 'MCA ', 'CMOV'
            db 'PAT ', 'PS36', 'PSN ', 'CLFL', '    ', 'DS  ', 'ACPI', 'MMX '
            db 'FXSR', 'SSE ', 'SSE2', 'SS  ', 'HTT ', 'TM  ', '    ', 'PBE '
ri_cnames:  db 'TONE', 'FM  ', 'PCM ', 'EXCL', 'IN  ', 'HI  ', 'ADPC', 'EXT '
            db 'DAC ', 'MIX ', 'STER', 'P16 '

; --- Memory ---
ri_h_conv:  db 'Conventional memory', 0
ri_h_ext:   db 'Extended and expanded memory', 0
ri_h_umb:   db 'Upper memory map', 0
ri_l_kern2: db 'Kernel', 0
ri_l_bufs:  db 'FAT window and task stacks', 0
ri_l_heap:  db 'Claim heap', 0
ri_l_claim: db '  claimed', 0
ri_l_kclaim: db '    by the kernel', 0
ri_l_osfree: db '  free', 0
ri_l_large: db '  largest free run', 0
ri_n_noext: db 'An 8088/8086 addresses 1 MB: no extended memory.', 0
ri_l_e820:  db 'Usable RAM (BIOS E820)', 0
ri_l_xms:   db 'Extended pool', 0
ri_l_ems:   db 'Expanded (EMS)', 0
ri_l_ebda:  db 'Extended BIOS data', 0
ri_s_ebda2: db ' (the BIOS', 27h, 's own, below 640 KB)', 0
ri_h_equip: db 'What the BIOS was told at power-on', 0
ri_l_eqw:   db 'Equipment word', 0
ri_l_eqvid: db 'Initial video', 0
ri_eqvids:  dw ri_ev0, ri_ev1, ri_ev2, ri_ev3
ri_ev0:     db 'EGA/VGA (its own BIOS)', 0
ri_ev1:     db '40 x 25 colour', 0
ri_ev2:     db '80 x 25 colour', 0
ri_ev3:     db '80 x 25 monochrome', 0
ri_l_eqx87: db 'Coprocessor switch', 0
ri_l_eqgame: db 'Game port bit', 0
ri_l_bmode: db 'BIOS video mode now', 0
ri_s_noems: db 'no EMS driver loaded', 0
ri_t_umb:   db 'Range      Size     Contents', 0
ri_s_vga:   db 'Video memory (A000h-BFFFh)', 0
ri_s_hercmem: db 'Hercules video memory, two pages', 0
ri_s_cgamem: db 'CGA video memory', 0
ri_s_optrom: db 'Option ROM', 0
ri_s_vrom:  db 'Video BIOS ROM', 0
ri_s_sysbios: db 'System BIOS ROM', 0
ri_s_sumok: db '(checksum good)', 0
ri_s_sumbad: db '(CHECKSUM BAD)', 0
ri_s_sumna: db '', 0

; --- Video ---
ri_h_prim:  db 'Primary display', 0
ri_h_vbios: db 'Video BIOS', 0
ri_l_dock:  db 'Dock starts at row', 0
ri_l_bmodes: db 'BIOS modes', 0
ri_bmodes:  dw ri_bm_vga, ri_bm_herc, ri_bm_cga, ri_bm_ega, ri_bm_vga
ri_bm_vga:  db 'text 00h-03h, 07h; graphics 04h-06h, 0Dh-13h', 0
ri_bm_herc: db 'text 07h; 720 x 348 graphics (no BIOS mode)', 0
ri_bm_cga:  db 'text 00h-03h; graphics 04h-06h', 0
ri_bm_ega:  db 'text 00h-03h, 07h; graphics 04h-06h, 0Dh-10h', 0
ri_s_dispn: db 'Display ', 0
ri_fsxm:    dw ri_x0, ri_x1, ri_x2, ri_x3, ri_x4, ri_x5, ri_x6, ri_x7, ri_x8
ri_x0:      db 'Fullscreen 80 x 25 text', 0
ri_x1:      db 'Fullscreen 40 x 25 text', 0
ri_x2:      db 'Fullscreen 320 x 200, 4 colours (CGA)', 0
ri_x3:      db 'Fullscreen 640 x 200, 2 colours (CGA)', 0
ri_x4:      db 'Fullscreen 720 x 348, mono, 2 pages (Hercules)', 0
ri_x5:      db 'Fullscreen 320 x 200, 16 colours (mode 0Dh)', 0
ri_x6:      db 'Fullscreen 320 x 200, 256 colours (mode 13h)', 0
ri_x7:      db 'Fullscreen 640 x 480, 16 colours (mode 12h)', 0
ri_x8:      db 'Fullscreen 320 x 240, 256 colours, 3 pages (Mode X)', 0
ri_l_dcca:  db 'Active display', 0
ri_l_dccb:  db 'Alternate display', 0
ri_dccs:    dw ri_dc0, ri_dc1, ri_dc2, ri_s_unk, ri_dc4, ri_dc5, ri_dc6, ri_dc7
            dw ri_dc8, ri_s_unk, ri_s_unk, ri_dcmm, ri_dcmc
ri_dc0:     db 'none', 0
ri_dc1:     db 'MDA, monochrome', 0
ri_dc2:     db 'CGA, colour', 0
ri_dc4:     db 'EGA, colour', 0
ri_dc5:     db 'EGA, monochrome', 0
ri_dc6:     db 'PGA', 0
ri_dc7:     db 'VGA, monochrome', 0
ri_dc8:     db 'VGA, colour', 0
ri_dcmm:    db 'MCGA, monochrome', 0
ri_dcmc:    db 'MCGA, colour', 0
ri_l_vram:  db 'Video memory (BIOS)', 0
ri_s_egacol: db ', colour monitor', 0
ri_s_egamono: db ', monochrome monitor', 0
ri_l_esw:   db 'EGA switches', 0
ri_l_vrom:  db 'Video ROM', 0
ri_n_novb:  db 'This adapter', 27h, 's BIOS has no information calls.', 0

; --- Drives ---
ri_h_bdrv:  db 'Drives the BIOS reports', 0
ri_h_vols:  db 'os8088 volumes', 0
ri_h_ddrv:  db 'Storage drivers', 0
ri_t_vols:  db 'Drive Medium        Reached through', 0
ri_s_remov: db 'removable', 0
ri_s_fixed: db 'fixed', 0
ri_vts:     dw ri_vt0, ri_vt1, ri_vt2
ri_vt0:     db 'BIOS int 13h', 0
ri_vt1:     db 'a disk driver', 0
ri_vt2:     db 'a redirector', 0
ri_s_boot:  db '  [boot]', 0
ri_s_here:  db '  [current]', 0
ri_s_vhere: db 'Current volume ', 0
ri_l_clus:  db 'Cluster size', 0
ri_l_vtot:  db 'Capacity', 0
ri_l_vused: db 'Used', 0
ri_l_vfree: db 'Free', 0
ri_l_hdd:   db 'Hard disk (HDD.DRV)', 0
ri_l_cd:    db 'CD-ROM (CDROM.DRV)', 0
ri_l_netv:  db 'Parallel link (NET.DRV)', 0
ri_l_ramd:  db 'RAM disk', 0
ri_l_http:  db 'Web drive (HTTPFS.DRV)', 0
ri_s_notld: db 'not loaded', 0
ri_s_loaded: db 'loaded (', 0
ri_n_geom:  db 'Drive geometry is not read: a program may not call the disk', 0
ri_n_geom2: db 'BIOS beside the kernel (SPEC.md 57.3).', 0

; --- Ports ---
ri_h_com:   db 'Serial ports', 0
ri_h_lpt:   db 'Parallel ports', 0
ri_h_oth:   db 'Other', 0
ri_t_com:   db 'Port  Base   UART        Line         Signals', 0
ri_s_com:   db 'COM', 0
ri_s_lpt:   db 'LPT', 0
ri_s_noport: db 'not installed', 0
ri_s_8250:  db '8250', 0
ri_s_16450: db '16450+', 0
ri_parity:  db 'NONENMNS'           ; LCR bits 5-3: none, odd, even, mark, space
ri_s_cts:   db 'CTS ', 0
ri_s_dsr:   db 'DSR ', 0
ri_s_dcd:   db 'DCD ', 0
ri_s_ring:  db 'RI ', 0
ri_s_busy:  db 'busy, ', 0
ri_s_ready: db 'ready, ', 0
ri_s_online: db 'selected', 0
ri_s_offline: db 'offline', 0
ri_s_paper: db ', out of paper', 0
ri_s_fault: db ', fault', 0
ri_l_kbd:   db 'Keyboard', 0
ri_s_kbxt:  db 'XT (83-key, 8255 PPI)', 0
ri_s_kbatc: db 'AT (8042 controller)', 0
ri_s_kbenh: db 'Enhanced 101/102-key (8042 controller)', 0
ri_n_fifo:  db 'A 16550 FIFO is not probed: the FIFO register is write-only,', 0
ri_n_fifo2: db 'and setting it would drop a byte in flight on a port in use.', 0

; --- IRQ/DMA ---
ri_h_irq:   db 'Interrupt lines (8259)', 0
ri_t_irq:   db 'IRQ  Standard use              Mask      Vector     Handler', 0
ri_irqs:    dw ri_i0, ri_i1, ri_i2, ri_i3, ri_i4, ri_i5, ri_i6, ri_i7
            dw ri_i8, ri_i9, ri_i10, ri_i11, ri_i12, ri_i13, ri_i14, ri_i15
ri_i0:      db 'System timer', 0
ri_i1:      db 'Keyboard', 0
ri_i2:      db 'free (EGA retrace)', 0
ri_i_casc:  db 'Cascade to the second 8259', 0
ri_i3:      db 'COM2 / COM4', 0
ri_i4:      db 'COM1 / COM3', 0
ri_i5:      db 'Fixed disk (XT)', 0
ri_i_lpt2:  db 'LPT2, often a sound card', 0
ri_i6:      db 'Floppy controller', 0
ri_i7:      db 'LPT1', 0
ri_i8:      db 'Real-time clock', 0
ri_i9:      db 'Redirected IRQ 2', 0
ri_i10:     db 'free', 0
ri_i11:     db 'free', 0
ri_i12:     db 'PS/2 mouse', 0
ri_i13:     db 'Coprocessor', 0
ri_i14:     db 'Fixed disk (primary IDE)', 0
ri_i15:     db 'free (secondary IDE)', 0
ri_s_on:    db 'enabled', 0
ri_s_off:   db 'masked', 0
ri_o_bios:  db 'BIOS ROM', 0
ri_o_rom:   db 'option ROM', 0
ri_o_kern:  db 'os8088 kernel', 0
ri_o_low:   db 'low memory', 0
ri_o_drv:   db 'os8088 driver', 0
ri_n_noivt: db 'Vectors are not read in protected mode: the table is not the CPU', 27h, 's.', 0
ri_h_dma:   db 'DMA channels (8237)', 0
ri_d_8:     db '8-bit', 0
ri_d_16:    db '16-bit', 0
ri_dmas:    dw ri_dm0, ri_dm1, ri_dm2, ri_dm3, ri_dm4, ri_dm5, ri_dm6, ri_dm7
ri_dm0:     db 'Memory refresh (PC/XT)', 0
ri_dm1:     db 'free, often an 8-bit sound card', 0
ri_dm2:     db 'Floppy controller', 0
ri_dm3:     db 'Fixed disk (XT)', 0
ri_dm4:     db 'Cascade to the first 8237', 0
ri_dm5:     db 'free, often a 16-bit sound card', 0
ri_dm6:     db 'free', 0
ri_dm7:     db 'free', 0
ri_d_free:  db 'free', 0
ri_n_dma:   db 'Live DMA state is not read: the 8237', 27h, 's status register clears', 0
ri_n_dma2:  db 'as it is read, which a driver waiting on a transfer would miss.', 0

; --- Devices ---
ri_h_snd:   db 'Sound', 0
ri_h_ptr:   db 'Input, network and clock', 0
ri_h_imgs:  db 'Drivers and modules loaded', 0
ri_h_progs: db 'Programs running', 0
ri_l_spk:   db 'PC speaker', 0
ri_l_sdrv:  db 'Sound driver loaded', 0
ri_l_scaps: db 'Capabilities', 0
ri_l_fm:    db 'FM synthesis', 0
ri_s_opl2:  db 'OPL2, ', 0
ri_s_opl3:  db 'OPL3, ', 0
ri_s_chans: db ' channels', 0
ri_l_mpu:   db 'MPU-401 MIDI port', 0
ri_l_dac:   db 'Covox DAC port', 0
ri_l_mix:   db 'Mixer', 0
ri_mixs:    dw ri_s_unk, ri_mx1, ri_mx2, ri_mx3
ri_mx1:     db 'CT1335 (SB 2.0 CD)', 0
ri_mx2:     db 'CT1345 (SB Pro)', 0
ri_mx3:     db 'CT1745 (SB16)', 0
ri_l_mouse: db 'Pointer', 0
ri_s_musb:  db 'USB mouse (CH375, USBMOUSE.DRV)', 0
ri_s_mvmw:  db 'Absolute pointer (VMware, VMMOUSE.DRV)', 0
ri_s_mser:  db 'serial or PS/2 mouse', 0
ri_l_net:   db 'Ethernet (ETHER.DRV)', 0
ri_l_emsd:  db 'EMS (EMS.DRV)', 0
ri_n_noimg: db 'No driver images, or this kernel does not name them.', 0
ri_s_drv:   db 'driver', 0
ri_s_mod:   db 'module', 0
ri_s_pkg:   db 'program', 0
ri_s_builtin: db 'built in', 0

; =============================================================================
; THE MODULE'S STATE - zeros in the part, which compress to nothing on the
; disk (OP_COMP) and are the claim's own bytes in memory
; =============================================================================
; (no `align`: inside .modi it would raise the SECTION alignment and move the cut)
ri_pkg:     dw 0
ri_fwin:    dw 0
ri_rwin:    dw 0
ri_mnl:     dw 0
ri_lcur:    dw 0
ri_lend:    dw 0
ri_cpu:     dw 0
ri_fsw:     dw 0
ri_msw:     dw 0
ri_xms:     dw 0
ri_emsfree: dw 0
ri_emstot:  dw 0
ri_emsfrm:  dw 0
ri_nroms:   dw 0
ri_vw:      dw 0
ri_vh:      dw 0
ri_vdock:   dw 0
ri_vkind:   dw 0
ri_ndisp:   dw 0
ri_dcc:     dw 0
ri_egabx:   dw 0
ri_egacx:   dw 0
ri_imr:     dw 0
ri_scaps:   dw 0
ri_sdx:     dw 0
ri_fm:      dw 0
ri_mpu:     dw 0
ri_dac:     dw 0
ri_ndrv:    dw 0
ri_boot:    dw 0
ri_model:   db 0
ri_c0ok:    db 0
ri_fpu:     db 0
ri_mswok:   db 0
ri_emsok:   db 0
ri_dccok:   db 0
ri_egaok:   db 0
ri_nhd:     db 0
ri_nfd:     db 0
ri_bootv:   db 0
ri_kbd:     db 0
ri_game:    db 0
ri_slave:   db 0
ri_ivtok:   db 0
ri_sto:     db 0
ri_fmok:    db 0
ri_mix:     db 0
ri_vmm:     db 0
ri_bmode:   db 0
ri_ebda:    dw 0
; (no `align`: inside .modi it would raise the SECTION alignment and move the cut)
ri_c0:      times 4 db 0
ri_bdate:   times 9 db 0
ri_dkind:   times 4 db 0
; (no `align`: inside .modi it would raise the SECTION alignment and move the cut)
ri_dmodes:  times 4 dw 0
ri_clk:     times 4 dw 0
ri_vols:    times 26 dw 0
ri_com:     times 4 * 8 db 0
ri_lpt:     times 3 * 4 db 0
ri_ivt:     times 16 * 4 db 0
ri_drv:     times 10 dw 0
ri_roms:    times RI_NROM * 6 db 0
ri_kb:      times SYSKB_SIZE db 0
ri_claims:  times CLAIM_SNAPSHOT_SIZE db 0
ri_drvs:    times RI_NDRV * 16 db 0
ri_snap:    times SYS_SNAPSHOT_SIZE db 0
ri_fsave:   times 108 db 0
ri_pfc:     times RI_PFSZ db 0
ri_cpun:    times 32 db 0
ri_bt:      times 2 * CS_BTW db 0
ri_facts:   times rl_facts_end - rl_facts_start db 0
ri_lattr:   times RI_LINES + 1 db 0
; (no `align`: inside .modi it would raise the SECTION alignment and move the cut)
ri_lbar:    times RI_LINES + 1 dw 0
ri_lines:   times (RI_LINES + 1) * RI_LW db 0
ri_mend:
