; =============================================================================
; os8088 - tests/megarmb/megarmbx.asm
;
; MEGARMBX, a TEST instrument (nothing under tests/ ships): what a REAL-MODE
; BRACKET reads (SPEC.md 53.2.3, docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W3) -
; OSAPI_FILE_READ_SEQ out of the extent list the door walked, OSAPI_XMEM_COPY
; by LOADALL (a 286) or a loaded base (a 386), OSAPI_DRV_CALL into a driver
; the door carried below 1MB. OS88_HIGH | OS88_HIGHMEM, so on kern_mega its
; region is above 1MB and the bracket runs a COPY of it (W1).
;
; Its window's timer starts it once. Every leg calls OSAPI_FSX_RUN with SI =
; FSXR_REAL, BP = the leg's FSXL_ list and the entry mx_main, and records CF,
; the SI that came back and OSAPI_CPU_INFO's AH inside. A READ leg streams its
; file through OSAPI_FILE_READ_SEQ in 32 KB calls into a conventional claim,
; checks every dword against its offset plus the file's salt (salt << 24 -
; tests/vidrmb.py writes them so), copies each chunk out to an
; OSAPI_XMEM_ALLOC block at the same offset, and at the end copies the whole
; file back in from extended memory and checks it again:
;
;   C  RMBXC.DAT in its own folder (Bochs: C:\RUN - a BIOS volume, or with
;      HDD.DRV a DRVC_DISK one through the driver's LOW COPY; 86Box: B:\)
;   B  RMBXB.DAT in B:\ (a BIOS floppy)
;   F  FRAGA.DAT in B:\ - FSXL_RUNS runs exactly, one sector each: real mode
;   G  FRAGB.DAT in B:\ - one run more: FSXR_FRAG, the same reads in
;      protected mode, the bytes the same
;   D  RMBXB.DAT with FSXL_DRV DRVC_NET: FSXR_DRV, protected mode (W4
;      carries DRVC_SOUND, so the class no bracket carries is the NIC's)
;   A  HDD.DRV's read STRAIGHT INTO XMS (HDV_ARM, SPEC.md 52.1.4) through the
;      OSAPI_DRV_CALL twin: RMBXC.DAT's first 32 KB armed to the XMS block's
;      CURRENT base (OSAPI_XMEM_SEL's window, OSAPI_SEG's SEG_LIN), read,
;      disarmed, and the block checked through the XMEM_COPY twin
;   N  inside a real-mode bracket: a name the list did not carry (FERR_NAME),
;      a listed file that is not there (FERR_NOENT), OSAPI_FILE_READ_AT and
;      OSAPI_XMEM_ALLOC refused, OSAPI_XMEM_CAPS answering what protected
;      mode answered
;
; THE RESULT BLOCK is the bss (mx_res), read by tests/vidrmb.py off the
; package's own memory (86Box has no port E9); every leg also prints
;      MEGARMBX: <leg> cf C why WW feat FF err EEEE bytes BBBBBBBB bad XXXX
;                xbad XXXX n NNNN x XXXX XXXX XXXX XXXX
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARMBX', mx_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM

MX_WAIT     equ 36                  ; the timer: two seconds after launch
MX_CHUNK    equ 32768
MX_XKB      equ 256                 ; the XMS block: every file fits
LEG_SZ      equ 24
L_CF        equ 0                   ; db: FFh = OSAPI_FSX_RUN refused
L_WHY       equ 1                   ; db: SI's answer
L_FEAT      equ 2                   ; db: OSAPI_CPU_INFO's AH inside
L_RAN       equ 3                   ; db: 1 = the entry ran
L_ERR       equ 4                   ; dw: the first failure's AX (0 none)
L_BYTES     equ 6                   ; dd: the bytes READ_SEQ delivered
L_BAD       equ 10                  ; dw: dwords wrong in the buffer
L_XBAD      equ 12                  ; dw: ...and read back from XMS
L_N         equ 14                  ; dw: READ_SEQ calls that delivered
L_X1        equ 16                  ; dw x 4: the leg's own (A, N)
L_X2        equ 18
L_X3        equ 20
L_X4        equ 22

mx_entry:
    push si
    mov si, mx_s_start
    call mx_say
    call mx_nl
    mov si, mx_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [mx_win], bx
    mov ax, mx_tick
    call OSAPI_WM_ONTIMER
    mov ax, MX_WAIT
    call OSAPI_WM_TIMER
    clc
.out:
    pop si
    ret

mx_paint:
    ret

; the timer: a window callback under the gfx lock - the bracket's context
mx_tick:
    cmp byte [mx_ran], 0
    je .go
    ret
.go:
    mov byte [mx_ran], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call OSAPI_FILE_HERE            ; where we were launched: DX, BL
    mov [mx_home], dx
    mov [mx_hdrv], bl
    mov ax, MX_CHUNK / 1024         ; the buffer: CONVENTIONAL (the DMA door,
    xor cx, cx                      ; CX = 0, for an OS88_HIGHMEM package)
    call OSAPI_MEM_CLAIM_DMA
    jc .nomem
    mov [mx_buf], dx
    mov ax, MX_XKB * 1024 & 0xFFFF  ; the XMS block
    mov dx, MX_XKB * 1024 >> 16
    call OSAPI_XMEM_ALLOC           ; DX:AX = its TOKEN
    jc .nomem
    mov [mx_xtok], ax
    mov [mx_xtok+2], dx
    xor cx, cx                      ; its CURRENT base, for HDV_ARM: a window
    call OSAPI_XMEM_SEL             ; over it and that window's linear base
    jc .nosel
    mov dx, ax
    mov bl, SEG_LIN
    call OSAPI_SEG
    jc .nosel
    mov [mx_xphys], ax
    mov [mx_xphys+2], dx
.nosel:
    call OSAPI_XMEM_CAPS
    mov [mx_capso], ax
    mov si, mx_list
    mov di, mx_list_bank
    call os88real_prep              ; the buffer's real segment, banked
    ; --- C: its own folder ------------------------------------------------
    mov al, 'C'
    mov bx, mx_lc
    mov word [mx_name], mx_n_c
    mov byte [mx_salt], 0x11
    call mx_leg
    ; --- B, F, G, D: B:\ ----------------------------------------------------
    xor dx, dx
    mov bl, 1
    call OSAPI_FILE_GOTO
    sbb dx, dx                      ; (on E9: CF, AX)
    mov si, mx_s_goto
    call mx_say
    xchg ax, dx
    call mx_hex4
    xchg ax, dx
    call mx_hex4
    call OSAPI_FILE_HERE
    mov al, ' '
    call mx_out1
    mov al, bl
    call mx_hex2
    mov ax, dx
    call mx_hex4
    call mx_nl
    mov al, 'B'
    mov bx, mx_lb
    mov word [mx_name], mx_n_b
    mov byte [mx_salt], 0x22
    call mx_leg
    mov al, 'F'
    mov bx, mx_lf
    mov word [mx_name], mx_n_f
    mov byte [mx_salt], 0x33
    call mx_leg
    mov al, 'G'
    mov bx, mx_lg
    mov word [mx_name], mx_n_g
    mov byte [mx_salt], 0x44
    call mx_leg
    mov al, 'D'
    mov bx, mx_ld
    mov word [mx_name], mx_n_b
    mov byte [mx_salt], 0x22
    call mx_leg
    mov dx, [mx_home]               ; ...and home
    mov bl, [mx_hdrv]
    call OSAPI_FILE_GOTO
    ; --- A: HDV_ARM through the twin --------------------------------------
    mov al, 'A'
    mov bx, mx_la
    mov word [mx_name], mx_n_c
    mov byte [mx_salt], 0x11
    mov word [mx_body], mx_arm
    call mx_leg0
    ; --- N: what the table answers ---------------------------------------
    mov al, 'N'
    mov bx, mx_lc
    mov word [mx_body], mx_nrm
    call mx_leg0
.nomem:
    mov si, mx_s_done
    call mx_say
    call mx_nl
    mov byte [mx_fin], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mx_leg - AL = the leg's letter, BX = its FSXL_ list, [mx_name] and
; [mx_salt] its file: the READ leg. mx_leg0 - ...with the body at [mx_body]
mx_leg:
    mov word [mx_body], mx_rd
mx_leg0:
    push ax
    sub al, 'A'                     ; the leg's record: by its letter
    mov ah, 0
    mov si, ax
    mov al, [mx_legix+si]
    mov ah, LEG_SZ
    mul ah
    add ax, mx_legs
    mov di, ax
    mov [mx_cur], di
    push di
    push ds
    pop es
    xor ax, ax                      ; the record clean
    mov cx, LEG_SZ / 2
    cld
    rep stosw
    pop di
    mov bp, bx                      ; BP = THE LIST
    mov bx, [mx_win]
    mov ax, mx_main
    mov cx, FSXF_REAL
    xor si, si                      ; FSXR_REAL in
    call OSAPI_FSX_RUN
    sbb dl, dl
    mov di, [mx_cur]
    mov [di+L_CF], dl
    mov ax, si
    mov [di+L_WHY], al
    cmp word [mx_body], mx_rd       ; A READ leg: its XMS once more, in
    jne .np                         ; PROTECTED mode - the kernel's own token
    call mx_back                    ; translation against the twin's
    mov di, [mx_cur]
    jnc .pb
    mov cx, 0xFFFF
.pb:
    mov [di+L_X1], cx
.np:
    pop ax
    jmp mx_rep

; THE ENTRY, in whichever bracket the kernel gave
mx_main:
    mov di, [mx_cur]
    call OSAPI_CPU_INFO
    mov [di+L_FEAT], ah
    mov byte [di+L_RAN], 1
    mov si, mx_list
    mov di, mx_list_bank
    call os88real_swap              ; the buffer word REAL, in a real bracket
    call [mx_body]
    mov si, mx_list
    mov di, mx_list_bank
    call os88real_swap
    ret

; mx_rd - the READ: [mx_name], 32 KB a call into [mx_buf], each chunk checked
; and copied out to XMS at its offset; then all of it back in and checked
mx_rd:
    call mx_cur0
    xor ax, ax
    mov [mx_off], ax
    mov [mx_off+2], ax
.lp:
    call mx_seq                     ; AX = bytes, CF = refused
    jc .err
    or ax, ax
    jz .back
    mov [mx_got], ax
    mov di, [mx_cur]
    inc word [di+L_N]
    add [di+L_BYTES], ax
    adc word [di+L_BYTES+2], 0
    call mx_chk                     ; the buffer's AX bytes at [mx_off]
    add [di+L_BAD], cx
    mov cx, [mx_got]
    inc cx
    and cl, 0xFE
    xor di, di                      ; out to XMS, at the same offset
    call mx_xcp
    jc .xerr
    mov ax, [mx_got]
    add [mx_off], ax
    adc word [mx_off+2], 0
    jmp short .lp
.back:                              ; ...and every byte back in from XMS
    call mx_back
    jc .xerr
    mov di, [mx_cur]
    mov [di+L_XBAD], cx
    ret
.xerr:
    or ax, 0x8000                   ; an XMEM_COPY failure: AX's code, marked
.err:
    mov di, [mx_cur]
    cmp word [di+L_ERR], 0
    jne .out
    or ax, ax
    jnz .e1
    dec ax
.e1:
    mov [di+L_ERR], ax
.out:
    ret

; mx_back - the leg's L_BYTES back in from the XMS block, 32 KB at a time,
; checked: CX = the dwords wrong; CF = 1 and AX an XMEM_COPY refused
mx_back:
    xor ax, ax
    mov [mx_xb], ax
    mov di, [mx_cur]
    mov ax, [di+L_BYTES]
    mov dx, [di+L_BYTES+2]
    mov [mx_end], ax
    mov [mx_end+2], dx
    xor ax, ax
    mov [mx_off], ax
    mov [mx_off+2], ax
.bl:
    mov ax, [mx_end]
    mov dx, [mx_end+2]
    sub ax, [mx_off]
    sbb dx, [mx_off+2]
    mov cx, ax
    or ax, dx
    jz .out
    or dx, dx
    jnz .full
    cmp cx, MX_CHUNK
    jbe .have
.full:
    mov cx, MX_CHUNK
.have:
    mov [mx_got], cx
    inc cx
    and cl, 0xFE
    mov di, 1                       ; in from XMS
    call mx_xcp
    jc .xerr
    mov ax, [mx_got]
    call mx_chk
    add [mx_xb], cx
    mov ax, [mx_got]
    add [mx_off], ax
    adc word [mx_off+2], 0
    jmp short .bl
.out:
    mov cx, [mx_xb]
    clc
.x:
    ret
.xerr:
    stc
    ret

; mx_seq - one READ_SEQ of [mx_name] into [mx_buf]: out CF, AX = bytes
mx_seq:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, [mx_name]
    mov dx, [mx_buf]
    xor bx, bx
    mov cx, MX_CHUNK
    push ds
    pop es
    mov di, mx_rcur
    call OSAPI_FILE_READ_SEQ
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; mx_cur0 - the cursor at offset 0
mx_cur0:
    push di
    push es
    push ds
    pop es
    mov di, mx_rcur
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    pop es
    pop di
    ret

; mx_xcp - CX bytes between [mx_buf]:0 and the XMS block at [mx_off], DI = 0
; out / 1 in. out CF, AX
mx_xcp:
    push bx
    push dx
    push si
    push es
    mov es, [mx_buf]
    xor si, si
    mov ax, [mx_xtok]
    mov dx, [mx_xtok+2]
    add ax, [mx_off]
    adc dx, [mx_off+2]
    call OSAPI_XMEM_COPY
    pop es
    pop si
    pop dx
    pop bx
    ret

; mx_chk - AX bytes of [mx_buf] against the file at [mx_off]: CX = the
; dwords that are not offset + (salt << 24)
mx_chk:
    push ax
    push bx
    push dx
    push si
    push es
    mov es, [mx_buf]
    xor si, si
    mov cx, ax
    shr cx, 1
    shr cx, 1
    jcxz .z
    mov ax, [mx_off]                ; the dword's offset in the file
    mov dx, [mx_off+2]
    mov bh, [mx_salt]
    push bp
    xor bp, bp                      ; BP = the count of bad ones
.l:
    cmp [es:si], ax
    jne .bad
    mov bl, dh
    add bl, bh
    cmp [es:si+2], dl
    jne .bad
    cmp [es:si+3], bl
    je .ok
.bad:
    inc bp
.ok:
    add si, 4
    add ax, 4
    adc dx, 0
    loop .l
    mov cx, bp
    pop bp
.z:
    pop es
    pop si
    pop dx
    pop bx
    pop ax
    ret

; mx_arm - A's body: HDD.DRV armed to the XMS block's current base, the first
; chunk read through it, disarmed - and the block's first chunk checked
mx_arm:
    call mx_cur0
    push es                         ; the block's first chunk ZERO first - C
    mov es, [mx_buf]                ; left the file there - so only what the
    xor di, di                      ; armed read sends can make it right
    mov cx, MX_CHUNK / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    mov [mx_off], ax
    mov [mx_off+2], ax
    mov cx, MX_CHUNK
    xor di, di
    call mx_xcp
    mov bx, DRVC_DISK << 8 | HDV_ARM
    mov dx, [mx_buf]
    mov cx, MX_CHUNK
    mov si, [mx_xphys]
    mov di, [mx_xphys+2]
    call OSAPI_DRV_CALL
    mov di, [mx_cur]
    jnc .armed
    or ax, 0x4000                   ; (0 = no such driver here)
    mov [di+L_X1], ax
    ret
.armed:
    mov word [di+L_X1], 0x1111
    call mx_seq
    mov di, [mx_cur]
    jc .rerr
    mov [di+L_BYTES], ax
    inc word [di+L_N]
    jmp short .dis
.rerr:
    mov [di+L_ERR], ax
.dis:
    mov bx, DRVC_DISK << 8 | HDV_DISARM
    mov di, mx_map
    call OSAPI_DRV_CALL             ; ES:DI = the map (ES = our DS: an X cell)
    mov di, [mx_cur]
    mov ax, [mx_map]
    mov [di+L_X3], ax
    mov ax, 0xFFFF
    and ax, [mx_map]
    and ax, [mx_map+2]
    and ax, [mx_map+4]
    and ax, [mx_map+6]
    mov [di+L_X4], ax
    xor ax, ax                      ; the block's first chunk, back in
    mov [mx_off], ax
    mov [mx_off+2], ax
    mov byte [mx_salt], 0x11
    mov cx, MX_CHUNK
    mov di, 1
    call mx_xcp
    mov di, [mx_cur]
    jc .xe
    mov ax, MX_CHUNK
    call mx_chk
    mov [di+L_XBAD], cx
    ret
.xe:
    or ax, 0x8000
    mov [di+L_X2], ax
    ret

; mx_nrm - N's body: the answers of the table's twins and refusals
mx_nrm:
    call mx_cur0
    mov word [mx_name], mx_n_no     ; not in the list: FERR_NAME
    call mx_seq
    sbb dx, dx
    mov di, [mx_cur]
    and ax, dx
    mov [di+L_X1], ax
    call mx_cur0
    mov word [mx_name], mx_n_miss   ; listed, not there: FERR_NOENT
    call mx_seq
    sbb dx, dx
    mov di, [mx_cur]
    and ax, dx
    mov [di+L_X2], ax
    mov si, mx_n_c                  ; READ_AT: refused
    mov es, [mx_buf]
    xor bx, bx
    mov cx, 512
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_READ_AT
    sbb ax, ax
    mov di, [mx_cur]
    mov [di+L_X3], ax
    mov ax, 1024                    ; XMEM_ALLOC: refused
    xor dx, dx
    call OSAPI_XMEM_ALLOC
    sbb ax, ax
    mov di, [mx_cur]
    mov [di+L_X4], ax
    call OSAPI_XMEM_CAPS            ; ...and CAPS answers
    mov [mx_capsi], ax
    mov word [mx_off], MX_CHUNK     ; ...and the COPY twin reads what D wrote
    mov word [mx_off+2], 0          ; in PROTECTED mode: RMBXB.DAT's second
    mov byte [mx_salt], 0x22        ; 32 KB, at its own offset
    mov cx, MX_CHUNK
    mov di, 1
    call mx_xcp
    mov di, [mx_cur]
    mov cx, 0xFFFF
    jc .nx
    mov ax, MX_CHUNK
    call mx_chk
.nx:
    mov [di+L_XBAD], cx
    ret

; mx_rep - AL = the leg's letter: its line on E9
mx_rep:
    mov di, [mx_cur]
    mov si, mx_name0
    call mx_puts
    call mx_out1
    mov si, mx_s_cf
    call mx_puts
    mov al, [di+L_CF]
    call mx_hex2
    mov si, mx_s_why
    call mx_puts
    mov al, [di+L_WHY]
    call mx_hex2
    mov si, mx_s_feat
    call mx_puts
    mov al, [di+L_FEAT]
    call mx_hex2
    mov si, mx_s_err
    call mx_puts
    mov ax, [di+L_ERR]
    call mx_hex4
    mov si, mx_s_bytes
    call mx_puts
    mov ax, [di+L_BYTES+2]
    call mx_hex4
    mov ax, [di+L_BYTES]
    call mx_hex4
    mov si, mx_s_bad
    call mx_puts
    mov ax, [di+L_BAD]
    call mx_hex4
    mov si, mx_s_xbad
    call mx_puts
    mov ax, [di+L_XBAD]
    call mx_hex4
    mov si, mx_s_n
    call mx_puts
    mov ax, [di+L_N]
    call mx_hex4
    mov si, mx_s_x
    call mx_puts
    mov ax, [di+L_X1]
    call mx_hex4
    mov al, ' '
    call mx_out1
    mov ax, [di+L_X2]
    call mx_hex4
    mov al, ' '
    call mx_out1
    mov ax, [di+L_X3]
    call mx_hex4
    mov al, ' '
    call mx_out1
    mov ax, [di+L_X4]
    call mx_hex4
    mov si, mx_s_caps
    call mx_puts
    mov ax, [mx_capso]
    call mx_hex4
    mov al, ' '
    call mx_out1
    mov ax, [mx_capsi]
    call mx_hex4
    jmp mx_nl

; --- E9 ----------------------------------------------------------------------
mx_say:
    push si
    mov si, mx_name0
    call mx_puts
    pop si
mx_puts:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mx_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mx_out1:
    out 0xE9, al
    ret
mx_hex4:
    push ax
    mov al, ah
    call mx_hex2
    pop ax
mx_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

%include "os88real.inc"

    OS88REAL_LIST mx_list, mx_buf   ; the buffer's claim word

mx_buf      dw 0
; THE LISTS (SPEC.md 53.2.3): what each leg's bracket will read
mx_lc:      db FSXL_FILE, 'RMBXC.DAT', 0, FSXL_FILE, 'MISSING.DAT', 0, FSXL_END
mx_lb:      db FSXL_FILE, 'RMBXB.DAT', 0, FSXL_END
mx_lf:      db FSXL_FILE, 'FRAGA.DAT', 0, FSXL_END
mx_lg:      db FSXL_FILE, 'FRAGB.DAT', 0, FSXL_END
mx_ld:      db FSXL_FILE, 'RMBXB.DAT', 0, FSXL_DRV, DRVC_NET, FSXL_END
mx_la:      db FSXL_FILE, 'RMBXC.DAT', 0, FSXL_DRV, DRVC_DISK, FSXL_END
mx_n_c:     db 'RMBXC.DAT', 0
mx_n_b:     db 'RMBXB.DAT', 0
mx_n_f:     db 'FRAGA.DAT', 0
mx_n_g:     db 'FRAGB.DAT', 0
mx_n_no:    db 'NOPE.DAT', 0
mx_n_miss:  db 'missing.dat', 0     ; (lower case: the twin compares upper)
; the legs' records, by letter: A B C D . F G . . . . . . N
mx_legix:   db 5, 1, 0, 4, 0, 2, 3, 0, 0, 0, 0, 0, 0, 6
mx_tpl:
    dw 8, 300, 200, 40
    dw mx_ttl, mx_paint, 0, 0
mx_ttl:     db 'MegaRMB reads', 0
mx_name0:   db 'MEGARMBX: ', 0
mx_s_start: db 'start', 0
mx_s_done:  db 'done', 0
mx_s_cf:    db ' cf ', 0
mx_s_why:   db ' why ', 0
mx_s_feat:  db ' feat ', 0
mx_s_err:   db ' err ', 0
mx_s_bytes: db ' bytes ', 0
mx_s_bad:   db ' bad ', 0
mx_s_xbad:  db ' xbad ', 0
mx_s_n:     db ' n ', 0
mx_s_x:     db ' x ', 0
mx_s_caps:  db ' caps ', 0
mx_s_goto:  db 'goto B: ', 0

; THE RESULT BLOCK, in the bss (tests/vidrmb.py reads it: its offset is
; os88_image_end, the .bin header's image size)
    OS88_BSS MX_RESN
    OS88_IMAGE_END

mx_res      equ os88_image_end
mx_fin      equ mx_res + 0          ; byte: 1 = every leg ran
mx_ran      equ mx_res + 1
mx_win      equ mx_res + 2
mx_capso    equ mx_res + 4          ; OSAPI_XMEM_CAPS's AX outside...
mx_capsi    equ mx_res + 6          ; ...and inside N
mx_xtok     equ mx_res + 8          ; dd: the XMS block's token
mx_xphys    equ mx_res + 12         ; dd: ...its base, at the start
mx_legs     equ mx_res + 16         ; C B F G D A N: LEG_SZ each
mx_cur      equ mx_legs + 7 * LEG_SZ
mx_body     equ mx_cur + 2
mx_name     equ mx_body + 2
mx_salt     equ mx_name + 2
mx_home     equ mx_salt + 2
mx_hdrv     equ mx_home + 2
mx_off      equ mx_hdrv + 2         ; dd
mx_end      equ mx_off + 4          ; dd
mx_map      equ mx_end + 4          ; 8 bytes: HDV_DISARM's
mx_rcur     equ mx_map + 8          ; FSEQ_SIZE: the READ_SEQ cursor
mx_got      equ mx_rcur + FSEQ_SIZE ; this chunk's bytes
mx_xb       equ mx_got + 2          ; mx_back's count
MX_RESN     equ mx_xb + 2 - mx_res
