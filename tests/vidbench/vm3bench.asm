; =============================================================================
; os8088 - tests/vidbench/vm3bench.asm
;
; VM3BENCH: what a WRITE MODE 3 store costs on the VGA it is booted on, beside
; the stores a 16-colour video record makes today (docs/plans/
; VIDEO-PALETTE-PLAN.md 10.6). The question it answers is one number the
; encoder cannot be told by arithmetic: a 16-colour record writes a byte as one
; store per GROUP of planes (SPEC.md 98.1.3.2), and a byte the player has
; written on some planes and not yet the rest shows a colour neither frame has
; whenever a refresh passes it. Write mode 3 writes one colour to the masked
; pixels of a byte on all four planes in ONE store - but it takes the other
; pixels from the latches, so each store must READ its byte first, and what an
; ISA read of a VGA costs on a 286 with wait states is a measurement.
;
; Every row moves 2,000 bytes onto the mode 12h screen from a RAM buffer of
; pixel masks, and reports the time per 2,000:
;
;   m0 rep movsb      write mode 0, all planes: today's slice, a byte a store
;   m0 lodsb/stosb    ...the same a byte at a time, a loop: what an entry's
;                     store costs without the block move
;   m3 rep movsb      write mode 3 with no read first: right only for a byte
;                     whose every pixel takes the colour (mask FFh) - a run
;   m3 lodsb/and      write mode 3, read mode 1 and Color Don't Care 0, so a
;                     read answers FFh: AND [ES:DI], AL loads the latches and
;                     stores the mask in one instruction
;   m3 lodsb/rd/stosb write mode 3, a MOV read then a store: the other way
;   out set/reset     OUT DX, AX to 3CEh index 0, 2,000 times: a colour's
;                     switch, once a colour list
;   read 12h rep movsb 2,000 bytes read from the screen: an ISA read alone
;
; THE PICTURE IS CHECKED FIRST: a known byte on all four planes, a mask of one
; colour written over it each way, and every plane read back. A card or an
; emulator that does not do write mode 3 says BAD here (MartyPC's VGA logs an
; invalid write mode and drops the store), and its times are not a result.
;
; Each row is benchlib's method P. The report is saved to VM3BENCH.TXT.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VM3BENCH', vm_entry

VM_N        equ 8                   ; iterations of a row
VM_B        equ 2000                ; bytes a row moves: under the PIT's 33 ms
                                    ; suspicion line on a 4.77 MHz 8088 too
VM_COLOUR   equ 9                   ; the set/reset colour the rows write

vm_entry:
    push si
    call bl_blank
    mov si, vm_s_title
    call bl_sline
    mov si, vm_s_hint
    call bl_sline
    mov si, vm_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [vm_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
.out:
    pop si
    ret

vm_paint:
    call bl_paint
    ret

vm_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [vm_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call vm_run
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vm_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [vm_win], si
    call vm_run
    call bl_paint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the VGA's registers
; =============================================================================

; vm_gc - AL = index, AH = value, to the Graphics Controller. Keeps every
;         register
vm_gc:
    push dx
    mov dx, 0x3CE
    out dx, ax
    pop dx
    ret

; vm_mode0 - mode 12h's own: write mode 0, read mode 0, all planes, no
;            set/reset, every bit, Color Don't Care all four
vm_mode0:
    push ax
    push dx
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    mov ax, 0x0005
    call vm_gc
    mov ax, 0x0000
    call vm_gc
    mov ax, 0x0001
    call vm_gc
    mov ax, 0x0003
    call vm_gc
    mov ax, 0x0F07
    call vm_gc
    mov ax, 0xFF08
    call vm_gc
    pop dx
    pop ax
    ret

; vm_mode3 - write mode 3, read mode 1 with Color Don't Care 0 (a read is
;            FFh, and loads the latches), set/reset = AL
vm_mode3:
    push ax
    mov ah, al
    xor al, al                      ; index 0: set/reset
    call vm_gc
    mov ax, 0x0007                  ; Color Don't Care: none
    call vm_gc
    mov ax, 0x0B05                  ; read mode 1, write mode 3
    call vm_gc
    mov ax, 0xFF08
    call vm_gc
    pop ax
    ret

; =============================================================================
; the bodies. Each preserves DS, ES and BP. DS:0 = the masks, ES:0 = screen
; =============================================================================
vm_b_movsb:
    push ds
    push es
    mov es, [vm_sseg]
    mov ds, [vm_dseg]
    xor si, si
    xor di, di
    mov cx, VM_B
    cld
    rep movsb
    pop es
    pop ds
    ret

vm_b_loop:
    push ds
    push es
    mov es, [vm_sseg]
    mov ds, [vm_dseg]
    xor si, si
    xor di, di
    mov cx, VM_B
    cld
.l: lodsb
    stosb
    loop .l
    pop es
    pop ds
    ret

vm_b_and:
    push ds
    push es
    mov es, [vm_sseg]
    mov ds, [vm_dseg]
    xor si, si
    xor di, di
    mov cx, VM_B
    cld
.l: lodsb
    and [es:di], al
    inc di
    loop .l
    pop es
    pop ds
    ret

vm_b_rdst:
    push ds
    push es
    mov es, [vm_sseg]
    mov ds, [vm_dseg]
    xor si, si
    xor di, di
    mov cx, VM_B
    cld
.l: lodsb
    mov ah, [es:di]
    stosb
    loop .l
    pop es
    pop ds
    ret

vm_b_out:
    mov dx, 0x3CE
    mov cx, VM_B
    mov ax, (VM_COLOUR << 8) | 0
.l: out dx, ax
    loop .l
    ret

vm_b_read:
    push ds
    push es
    mov es, [vm_dseg]
    mov ds, [vm_sseg]
    xor si, si
    xor di, di
    mov cx, VM_B
    cld
    rep movsb
    pop es
    pop ds
    ret

; =============================================================================
; vm_check - write mode 3 against the planes, both store shapes. Leaves the
;            result in [vm_ok]: bit 0 AND, bit 1 read+store
; =============================================================================
VM_CK_MASK  equ 0xA5                ; the pixels written
VM_CK_P0    equ 0x0F                ; ...over these, plane by plane
VM_CK_P1    equ 0x33
VM_CK_P2    equ 0x55
VM_CK_P3    equ 0xF0

vm_check:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov es, [vm_sseg]
    mov byte [vm_ok], 0
    ; the known byte, planes 0..3, at offsets 0 and 1
    call vm_mode0
    mov dx, 0x3C4
    mov bx, vm_ck_planes
    mov ah, 1
.pl:
    mov al, 2
    out dx, ax                      ; one plane
    mov al, [bx]
    mov [es:0], al
    mov [es:1], al
    inc bx
    shl ah, 1
    cmp ah, 0x10
    jb .pl
    mov ax, 0x0F02
    out dx, ax
    ; offset 0: AND [ES:DI], AL
    mov al, VM_COLOUR
    call vm_mode3
    mov al, VM_CK_MASK
    xor di, di
    and [es:di], al
    ; offset 1: a read, then a store
    mov di, 1
    mov ah, [es:di]
    mov [es:di], al
    call vm_mode0
    ; read every plane back, read mode 0 and Read Map Select
    xor di, di
    mov bl, 1
.off:
    mov bh, 1                       ; all four planes right so far
    xor cx, cx                      ; CL = the plane
.rp:
    mov al, 4
    mov ah, cl
    call vm_gc
    mov al, [es:di]
    ; want = (old AND NOT mask) OR (mask if the colour has this plane's bit)
    push bx
    mov bx, vm_ck_planes
    add bx, cx
    mov ah, [bx]
    pop bx
    and ah, ~VM_CK_MASK & 0xFF
    mov dl, VM_COLOUR
    shr dl, cl
    test dl, 1
    jz .nb
    or ah, VM_CK_MASK
.nb:
    cmp al, ah
    je .same
    xor bh, bh
.same:
    inc cl
    cmp cl, 4
    jb .rp
    or bh, bh
    jz .bad
    or [vm_ok], bl
.bad:
    inc di
    shl bl, 1
    cmp di, 2
    jb .off
    mov ax, 0x0004
    call vm_gc
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; vm_fsx - OSAPI_FSX_RUN's proc: mode 12h, the check, then every row
; =============================================================================
vm_fsx:
    push ds
    pop es
    mov al, FSXM_VGA12
    mov di, vm_fsi
    call OSAPI_FSX_MODE
    jc .out
    mov byte [vm_inmode], 1
    mov ax, [vm_fsi + FSI_SEG]
    mov [vm_sseg], ax
    call vm_check
    call vm_mode0
    call bl_baseline
    mov word [bl_n], VM_N
    mov si, vm_s_hdr
    call bl_sline
    call bl_head
    ; --- write mode 0, today's stores
    mov word [bl_body], vm_b_movsb
    mov si, vm_r_m0b
    xor al, al
    call bl_run
    mov word [bl_body], vm_b_loop
    mov si, vm_r_m0l
    xor al, al
    call bl_run
    ; --- write mode 3
    mov al, VM_COLOUR
    call vm_mode3
    mov word [bl_body], vm_b_movsb
    mov si, vm_r_m3b
    xor al, al
    call bl_run
    mov word [bl_body], vm_b_and
    mov si, vm_r_m3a
    xor al, al
    call bl_run
    mov word [bl_body], vm_b_rdst
    mov si, vm_r_m3r
    xor al, al
    call bl_run
    call vm_mode0
    ; --- the OUT a colour list pays, and a read alone
    mov word [bl_body], vm_b_out
    mov si, vm_r_out
    xor al, al
    call bl_run
    mov ax, 0x0005                  ; (set/reset back to 0)
    call vm_gc
    xor ax, ax
    call vm_gc
    mov word [bl_body], vm_b_read
    mov si, vm_r_rd
    xor al, al
    call bl_run
    call vm_mode0
.out:
    clc
    ret

; =============================================================================
; vm_run - the masks, then the bracket
; =============================================================================
vm_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [bl_nrow], 0
    mov si, vm_s_title
    call bl_sline
    cmp word [vm_dseg], 0
    jne .have
    mov ax, 8                       ; the masks (VM_B of 8 KB)
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [vm_dseg], dx
.have:
    push es                         ; the masks: a fixed LCG, so every run
    mov es, [vm_dseg]               ; and every machine stores the same bytes
    xor di, di
    mov ax, 12345
    mov cx, VM_B
    cld
.fill:
    mov bx, 25173
    mul bx
    add ax, 13849
    mov [es:di], ah
    inc di
    loop .fill
    pop es
    mov bx, [vm_win]
    call OSAPI_FSX_CAPS             ; DL = the adapter
    cmp dl, VID_VGA
    je .vga
    cmp dl, VID_EGA
    je .vga
    mov si, vm_s_novga
    call bl_sline
    jmp .end
.vga:
    mov byte [vm_inmode], 0
    mov ax, vm_fsx
    mov bx, [vm_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    jnc .ran
    mov si, vm_s_refused
    call bl_sline
.ran:
    cmp byte [vm_inmode], 0
    jne .modeok
    mov si, vm_s_nomode
    call bl_sline
    jmp .save
.modeok:
    mov si, vm_r_cka
    mov di, vm_s_ok
    test byte [vm_ok], 1
    jnz .a
    mov di, vm_s_bad
.a: call bl_kvs
    mov si, vm_r_ckr
    mov di, vm_s_ok
    test byte [vm_ok], 2
    jnz .r
    mov di, vm_s_bad
.r: call bl_kvs
.save:
    call bl_operator
    mov si, vm_f_txt
    call bl_save
    jmp .end
.nomem:
    mov si, vm_s_nomem
    call bl_sline
.end:
    inc word [vm_done]              ; (a harness waits on this)
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%define BL_ARENA_BYTES 4000
%define BL_NUMBERED                 ; a field run saves NAME1, NAME2... (benchlib)
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================
vm_tpl:
    dw 7, 22, 632, 300
    dw vm_ttl, vm_paint, vm_onkey, vm_onclick

vm_ttl:       db 'Write Mode 3 Bench', 0
vm_f_txt:     db 'VM3BENCH.TXT', 0
vm_s_title:   db 'VM3BENCH - a VGA write mode 3 store against today', 0
vm_s_hint:    db 'Click, or press R, to run. The rows go fullscreen.', 0
vm_s_hdr:     db '-- 2000 bytes onto mode 12h, a row --', 0
vm_s_novga:   db 'NO VGA: write mode 3 is a VGA register', 0
vm_s_nomem:   db 'NO MEMORY for the 8 KB of masks', 0
vm_s_refused: db 'BRACKET REFUSED', 0
vm_s_nomode:  db 'MODE 12h REFUSED', 0
vm_s_ok:      db 'OK', 0
vm_s_bad:     db 'BAD - times are not a result', 0
vm_r_m0b:     db 'm0 rep movsb', 0
vm_r_m0l:     db 'm0 lodsb/stosb', 0
vm_r_m3b:     db 'm3 rep movsb (no read)', 0
vm_r_m3a:     db 'm3 lodsb/and [di]', 0
vm_r_m3r:     db 'm3 lodsb/rd/stosb', 0
vm_r_out:     db 'out set/reset', 0
vm_r_rd:      db 'read 12h rep movsb', 0
vm_r_cka:     db 'check mode 3 AND', 0
vm_r_ckr:     db 'check mode 3 rd+stosb', 0
vm_ck_planes: db VM_CK_P0, VM_CK_P1, VM_CK_P2, VM_CK_P3

vm_win:       dw 0
vm_dseg:      dw 0
vm_sseg:      dw 0
vm_inmode:    db 0
vm_ok:        db 0
vm_done:      dw 0                  ; runs completed

VM_BSS_OWN  equ 512                 ; the FSI block; benchlib's base must be
                                    ; 512-aligned (bl_save)
    OS88_BSS VM_BSS_OWN + BL_BSS_SIZE
    align 512
    OS88_IMAGE_END

vm_fsi      equ os88_image_end + 0

    BL_BSS os88_image_end + VM_BSS_OWN
