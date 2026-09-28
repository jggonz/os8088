; =============================================================================
; os8088 - tests/vidbench/vidbench.asm
;
; VIDBENCH: wave 0 (a) and (d) of docs/plans/VIDEO-PLAN.md - what a video
; frame COSTS, decoded three ways on the adapter it is booted on.
;
;   python3 tests/vidbench.py --samples DIR [--machine os8088_5150_herc]
;
; The frames are REAL ones: tools/os88vid.py benchdat picks the heaviest,
; the busiest, a p95 and a median frame out of each XDC stream it is given
; and writes each one twice into VIDBENCH.DAT - XDC's own packet, which is a
; program the bench far-calls exactly as XDC_PLAY.PAS does, and the same
; frame in the plan's operand format (eight skip-coded lists, section 2.2),
; which apps/video/vdec.inc decodes. The streams are the owner's and are
; never committed, so the data file is built by the driver from a path.
;
; PER FRAME, THREE ROWS, all in the bracket in the mode the player will use
; (CGA 640x200, Hercules 720x348, VGA 640x480):
;
;   XDC scr    XDC's program writing the screen (its `mov ax,B800` patched to
;              the surface's segment, so on Hercules and VGA it writes the
;              same bytes into that card's memory - the timing XDC would get
;              there, not a picture)
;   nat scr    vd_native writing the screen - the player's own decoder
;   nat ram    vd_native into a 16 KB RAM canvas: the card's wait states
;              taken out, so nat scr - nat ram IS the card's cost - the
;              field question on a Hercules, which MartyPC charges exactly
;              as it charges a CGA
;
; Wave 0 had two more, and they came out when the report TRUNCATED at 8,000
; bytes on the field disk's 31 frames: shd+copy (its answer - 3-5x a native
; decode at full screen - is in docs/reports/VIDEO-W0-2026-09-25.md and did
; not need re-asking) and XDC ram (XDC's own wait states, which are not the
; question). After the frames, the WORST nat scr and XDC scr frame is printed
; as a share of a 30 fps and a 23.976 fps period, which is the number the
; player's CPU budget is written in (VIDEO-PLAN 3.2). The report is SAVED to
; VIDBENCH.TXT beside the bench when the run ends.
;
; ...and four raw rows first: 8,000 bytes by rep movsb and by rep movsw, to
; the screen and to RAM, which is wave 0 (d).
;
; THE PICTURE IS CHECKED BEFORE ANYTHING IS TIMED. Each frame is applied to a
; BLACK RAM canvas two ways - XDC's program and vd_native - and each canvas is
; summed with the host's checksum
; (os88vid.py checksum(), vb_sum here). The report says OK or BAD per frame
; and per path, and the harness fails on BAD: a fast decoder that draws the
; wrong picture is not a result.
;
; Each row is benchlib's method P: VB_N iterations of the body with
; interrupts off inside each, PIT-timed, which MartyPC counts to the cycle.
; A frame is at most ~100,000 cycles (21 ms), under benchlib's 33 ms
; suspicion line.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VIDBENCH', vb_entry

VB_N        equ 4                   ; iterations of a frame row
VB_NRAW     equ 8                   ; ...of a raw row
VB_MAXF     equ 32                  ; frames the data file may carry
VB_NKIND    equ 3                   ; rows a frame
VB_NRES     equ 8 + VB_MAXF * VB_NKIND ; result rows the harness reads
VB_DATKB    equ 192                 ; the data file's claim
VB_RAWB     equ 8000                ; bytes a raw row moves: one CGA bank
VB_DIR      equ 8                   ; the directory's offset in the file
VB_DIRSZ    equ 32                  ; ...and an entry's size

; -----------------------------------------------------------------------------
; vb_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
vb_entry:
    push si
    call vb_hint
    mov si, vb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [vb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
.out:
    pop si
    ret

vb_hint:
    push si
    call bl_blank
    mov si, vb_s_title
    call bl_sline
    call bl_head
    mov si, vb_s_hint
    call bl_sline
    pop si
    ret

vb_paint:
    call bl_paint
    ret

vb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [vb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call vb_run
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [vb_win], si
    call vb_run
    call bl_paint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the bodies. Every one preserves DS, ES and BP.
; =============================================================================

; XDC's program, far-called at [vb_xptr] exactly as XDC_PLAY.PAS does
vb_b_xdc:
    push ds
    push es
    push bp
    call far [vb_xptr]
    pop bp
    pop es
    pop ds
    ret

; vd_native: DS:0 = the lists, ES = [vb_tseg], BP = 0
vb_b_nat:
    push ds
    push es
    push bp
    mov es, [vb_tseg]
    xor bp, bp
    xor si, si
    mov ds, [vb_lseg]
    call vd_native
    pop bp
    pop es
    pop ds
    ret

; VB_RAWB bytes from the data file to [vb_tseg]:0
vb_b_mb:
    push ds
    push es
    mov es, [vb_tseg]
    mov ds, [vb_dseg]
    xor si, si
    xor di, di
    mov cx, VB_RAWB
    cld
    rep movsb
    pop es
    pop ds
    ret

vb_b_mw:
    push ds
    push es
    mov es, [vb_tseg]
    mov ds, [vb_dseg]
    xor si, si
    xor di, di
    mov cx, VB_RAWB / 2
    cld
    rep movsw
    pop es
    pop ds
    ret

; =============================================================================
; helpers
; =============================================================================

; vb_patch - AX = the segment XDC's program should write: its `mov ax,B800`
;            immediate, at offset 7 of the packet
vb_patch:
    push es
    mov es, [vb_xptr + 2]
    mov [es:7], ax
    pop es
    ret

; vb_frame - BX = frame index: point vb_xptr and vb_lseg at it and copy its
;            label into vb_lbl
vb_frame:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    mov ax, VB_DIRSZ
    mul bx
    add ax, VB_DIR
    mov si, ax                      ; SI = the directory entry, in the file
    mov bx, [vb_dseg]
    push ds
    pop es                          ; ES = ours
    mov ds, bx                      ; DS = the file
    mov ax, [si + 12]
    add ax, bx
    mov [es:vb_xptr + 2], ax
    mov word [es:vb_xptr], 0
    mov ax, [si + 16]
    add ax, bx
    mov [es:vb_lseg], ax
    mov ax, [si + 20]
    mov [es:vb_want], ax
    mov ax, [si + 30]
    mov [es:vb_y0], al
    mov [es:vb_y1], ah
    mov di, vb_lbl
    mov cx, 12
.cp:
    lodsb
    or al, al
    jz .end
    stosb
    loop .cp
.end:
    mov [es:vb_lblend], di
    xor al, al
    stosb
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; vb_label - SI = a suffix: vb_lbl becomes the frame's label and the suffix
; out: SI = vb_lbl
vb_label:
    push ax
    push di
    push es
    push ds
    pop es
    mov di, [vb_lblend]
.cp:
    lodsb
    stosb
    or al, al
    jnz .cp
    pop es
    pop di
    pop ax
    mov si, vb_lbl
    ret

; vb_clear - zero the RAM canvas
vb_clear:
    push ax
    push cx
    push di
    push es
    mov es, [vb_rseg]
    xor di, di
    xor ax, ax
    mov cx, 8192
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; vb_sum - AX = the RAM canvas's checksum: s = rol(s + word, 1), the host's
vb_sum:
    push cx
    push dx
    push si
    push ds
    mov ds, [vb_rseg]
    xor si, si
    xor dx, dx
    mov cx, 8192
    cld
.l:
    lodsw
    add dx, ax
    rol dx, 1
    loop .l
    mov ax, dx
    pop ds
    pop si
    pop dx
    pop cx
    ret

; vb_bank - BX = the result row: bank [bl_lastus] and its flag
vb_bank:
    push ax
    push bx
    push dx
    push si
    mov si, bx
    shl si, 1
    shl si, 1
    mov ax, [bl_lastus]
    mov dx, [bl_lastus + 2]
    mov [vb_res + si], ax
    mov [vb_res + si + 2], dx
    mov al, [bl_lscr + BL_C_FLAG]
    or al, al
    jnz .flag
    mov al, ' '
.flag:
    mov [vb_resf + bx], al
    pop si
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; vb_load - claim, and read VIDBENCH.DAT. CF = could not
; =============================================================================
vb_load:
    cmp word [vb_dseg], 0
    jne .ok
    mov ax, VB_DATKB
    call OSAPI_MEM_CLAIM
    jc .err
    mov [vb_dseg], dx
    mov ax, 16
    call OSAPI_MEM_CLAIM
    jc .err
    mov [vb_rseg], dx
    push es
    mov es, [vb_dseg]
    xor bx, bx
    mov si, vb_f_dat
    mov dx, VB_DATKB / 64           ; DX:CX = the capacity, VB_DATKB KB
    xor cx, cx
    call OSAPI_FILE_READ
    pop es
    jc .err
    push es
    mov es, [vb_dseg]
    cmp word [es:0], 'VB'
    jne .bad
    cmp word [es:2], 'D1'
    jne .bad
    mov ax, [es:4]
    pop es
    or ax, ax
    jz .err
    cmp ax, VB_MAXF
    ja .err
    mov [vb_nf], ax
.ok:
    clc
    ret
.bad:
    pop es
.err:
    stc
    ret

; =============================================================================
; vb_checks - every frame, three ways, onto a black RAM canvas
; =============================================================================
vb_checks:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, vb_s_hdrc
    call bl_sline
    ; THE FRAME INDEX AND THE BITS LIVE IN MEMORY: the bodies clobber BX
    ; and DX (the decoders' Duff index and list counts), which is their
    ; contract - a harness holding either in a register across one reads
    ; every picture as wrong
    mov word [vb_fi], 0
.f:
    mov bx, [vb_fi]
    call vb_frame
    mov byte [vb_bits], 0           ; 1 XDC, 2 native
    ; --- XDC's own program
    call vb_clear
    mov ax, [vb_rseg]
    call vb_patch
    call vb_b_xdc
    call vb_sum
    cmp ax, [vb_want]
    jne .n1
    or byte [vb_bits], 1
.n1:
    ; --- vd_native
    call vb_clear
    mov ax, [vb_rseg]
    mov [vb_tseg], ax
    call vb_b_nat
    call vb_sum
    cmp ax, [vb_want]
    jne .n2
    or byte [vb_bits], 2
.n2:
    mov bx, [vb_fi]
    mov al, [vb_bits]
    mov [vb_chk + bx], al
    mov si, vb_x_chk
    call vb_label
    mov di, vb_s_ok
    cmp byte [vb_bits], 3
    je .say
    mov di, vb_s_bad
.say:
    call bl_kvs
    inc word [vb_fi]
    mov bx, [vb_fi]
    cmp bx, [vb_nf]
    jb .f
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; vb_fsx - OSAPI_FSX_RUN's proc: the mode the player takes, then every row
; =============================================================================
vb_fsx:
    push ds
    pop es
    mov al, [vb_fsxm]
    mov di, vb_fsi
    call OSAPI_FSX_MODE
    jc .out
    mov byte [vb_inmode], 1
    mov ax, [vb_fsi + FSI_SEG]
    mov [vb_sseg], ax
    cmp byte [vb_fsxm], FSXM_VGA12
    jne .nomask
    mov dx, 0x3C4                   ; every plane: a byte store is 8 pixels
    mov ax, 0x0F02                  ; in all four, which is MONO1 on mode 12h
    out dx, ax
.nomask:
    call bl_baseline

    ; --- (d) raw stores ---------------------------------------------------
    mov si, vb_s_hdrr
    call bl_sline
    mov word [bl_n], VB_NRAW
    mov ax, [vb_sseg]
    mov [vb_tseg], ax
    mov word [bl_body], vb_b_mb
    mov si, vb_r_mbs
    xor al, al
    call bl_run
    mov bx, 0
    call vb_bank
    mov word [bl_body], vb_b_mw
    mov si, vb_r_mws
    xor al, al
    call bl_run
    mov bx, 1
    call vb_bank
    mov ax, [vb_rseg]
    mov [vb_tseg], ax
    mov word [bl_body], vb_b_mb
    mov si, vb_r_mbr
    xor al, al
    call bl_run
    mov bx, 2
    call vb_bank
    mov word [bl_body], vb_b_mw
    mov si, vb_r_mwr
    xor al, al
    call bl_run
    mov bx, 3
    call vb_bank

    ; --- (a) every frame, three ways ------------------------------------------
    mov si, vb_s_hdrf
    call bl_sline
    mov word [bl_n], VB_N
    xor bx, bx
.f:
    call vb_frame
    mov ax, bx
    mov cx, VB_NKIND
    mul cx
    add ax, 8
    mov [vb_ridx], ax
    push bx
    ; XDC's program, to the screen
    mov ax, [vb_sseg]
    call vb_patch
    mov word [bl_body], vb_b_xdc
    mov si, vb_x_xs
    call vb_label
    xor al, al
    call bl_run
    mov bx, [vb_ridx]
    call vb_bank
    mov di, vb_wx                   ; the worst XDC frame so far
    call vb_worst
    ; native, to the screen
    mov ax, [vb_sseg]
    mov [vb_tseg], ax
    mov word [bl_body], vb_b_nat
    mov si, vb_x_ns
    call vb_label
    xor al, al
    call bl_run
    mov bx, [vb_ridx]
    inc bx
    call vb_bank
    mov di, vb_wn                   ; ...and the worst of ours
    call vb_worst
    ; native, to RAM
    mov ax, [vb_rseg]
    mov [vb_tseg], ax
    mov word [bl_body], vb_b_nat
    mov si, vb_x_nr
    call vb_label
    xor al, al
    call bl_run
    mov bx, [vb_ridx]
    add bx, 2
    call vb_bank
    pop bx
    inc bx
    cmp bx, [vb_nf]
    jb .f
.out:
    ret

; vb_worst - DI = a 6-byte record (hundredths of a us, dword; the frame):
;            keep [bl_lastus] in it if it is the largest yet. BX = the frame
vb_worst:
    push ax
    push dx
    mov ax, [bl_lastus]
    mov dx, [bl_lastus + 2]
    cmp dx, [di + 2]
    jb .no
    ja .yes
    cmp ax, [di]
    jbe .no
.yes:
    mov [di], ax
    mov [di + 2], dx
    mov [di + 4], bx
.no:
    pop dx
    pop ax
    ret

; vb_share - SI = the label, DI = a vb_worst record: the frame's cost as a
;            share of a 30 fps and a 23.976 fps period, in tenths of a percent
;            (hundredths of a us / 3,333.3 and / 4,170.8)
vb_share:
    push ax
    push cx
    push dx
    mov ax, [di]
    mov dx, [di + 2]
    mov cx, 3333
    div cx
    xor dx, dx
    mov cx, 9
    call bl_kv
    mov ax, [di]
    mov dx, [di + 2]
    mov cx, 4171
    div cx
    xor dx, dx
    mov cx, 9
    push si
    mov si, vb_r_24
    call bl_kv
    pop si
    pop dx
    pop cx
    pop ax
    ret

; vb_adapter - the mode the player would take for a CGA-layout file here:
;              the BIOS's mode 6 on a CGA, an EGA or a VGA (native), and
;              Hercules' own mode on a Hercules (a FOREIGN file - its rows
;              are the shd+copy row's)
vb_adapter:
    push ax
    push bx
    push dx
    mov bx, [vb_win]
    call OSAPI_FSX_CAPS             ; AX = the mask, DL = the kind
    mov [vb_vkind], dl
    mov byte [vb_fsxm], FSXM_CGA640
    cmp dl, VID_HERC
    jne .nh
    mov byte [vb_fsxm], FSXM_HERC
.nh:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; vb_run - load, check, then the bracket
; =============================================================================
vb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [vb_done], 0
    mov word [bl_nrow], 0
    push es                         ; the worst frames, from nothing
    push ds
    pop es
    mov di, vb_wx
    xor ax, ax
    mov cx, 6
    cld
    rep stosw
    pop es
    mov si, vb_s_title
    call bl_sline
    call vb_load
    jnc .loaded
    mov si, vb_s_nodat
    call bl_sline
    mov word [vb_done], 0xFFFF      ; the harness reads this as "no data"
    jmp .end
.loaded:
    call vb_checks
    call vb_adapter
    mov byte [vb_inmode], 0
    mov si, vb_s_hdrb
    call bl_sline
    mov ax, vb_fsx
    mov bx, [vb_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    jnc .ran
    mov si, vb_s_refused
    call bl_sline
.ran:
    cmp byte [vb_inmode], 0
    jne .modeok
    mov si, vb_s_nomode
    call bl_sline
.modeok:
    mov si, vb_r_mode
    mov al, [vb_fsxm]
    xor ah, ah
    xor dx, dx
    mov cx, 9
    call bl_kv
    mov si, vb_r_kind
    mov al, [vb_vkind]
    xor ah, ah
    xor dx, dx
    call bl_kv
    mov si, vb_s_hdrw               ; the worst frames, as the player's budget
    call bl_sline                   ; is written (VIDEO-PLAN 3.2)
    mov si, vb_r_wx
    mov di, vb_wx
    call vb_share
    mov si, vb_r_wn
    mov di, vb_wn
    call vb_share
    call bl_operator
    mov si, vb_f_txt                ; ...and the report, to a file beside the
    call bl_save                    ; bench (benchlib's rule for every bench)
    inc word [vb_done]
.end:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%define BL_ARENA_BYTES 12000        ; ~150 lines. 8,000 TRUNCATED the field
                                    ; disk's 31 frames at five rows a frame
%include "benchlib.inc"
%include "video/vdec.inc"

; =============================================================================
; data
; =============================================================================
vb_tpl:
    dw 7, 22, 632, 448
    dw vb_ttl, vb_paint, vb_onkey, vb_onclick

vb_ttl:       db 'Video Bench', 0
vb_f_dat:     db 'VIDBENCH.DAT', 0
vb_f_txt:     db 'VIDBENCH.TXT', 0

vb_s_title:   db 'VIDBENCH - a video frame, decoded three ways (VIDEO-PLAN W0)', 0
vb_s_hint:    db 'Click, or press R, to run. The rows go fullscreen.', 0
vb_s_nodat:   db 'NO VIDBENCH.DAT beside the bench, or it is not a VBD1 file', 0
vb_s_hdrc:    db '-- the picture: each frame onto black, three ways --', 0
vb_s_hdrb:    db '-- the bracket: the mode the player takes --', 0
vb_s_hdrr:    db '-- (d) raw: 8000 bytes, rep movsb / rep movsw --', 0
vb_s_hdrf:    db '-- (a) per frame: XDC and ours to the screen, ours to RAM --', 0
vb_s_hdrw:    db '-- the worst frame, per mille of a frame period --', 0
vb_r_wx:      db 'XDC scr, 30 fps', 0
vb_r_wn:      db 'nat scr, 30 fps', 0
vb_r_24:      db '  ...at 23.976 fps', 0
vb_s_refused: db 'BRACKET REFUSED', 0
vb_s_nomode:  db 'MODE REFUSED', 0
vb_s_ok:      db 'OK', 0
vb_s_bad:     db 'BAD', 0

vb_r_mbs:     db 'movsb 8000 to screen', 0
vb_r_mws:     db 'movsw 8000 to screen', 0
vb_r_mbr:     db 'movsb 8000 to RAM', 0
vb_r_mwr:     db 'movsw 8000 to RAM', 0
vb_r_mode:    db 'bracket mode (FSXM)', 0
vb_r_kind:    db 'adapter kind (VID)', 0

vb_x_chk:     db ' check', 0
vb_x_xs:      db ' XDC scr', 0
vb_x_ns:      db ' nat scr', 0
vb_x_nr:      db ' nat ram', 0

vb_win:       dw 0
vb_dseg:      dw 0                  ; the data file's claim
vb_rseg:      dw 0                  ; the 16 KB RAM canvas
vb_sseg:      dw 0                  ; the screen, from the bracket's FSI
vb_tseg:      dw 0                  ; a row's target
vb_lseg:      dw 0                  ; the current frame's lists
vb_xptr:      dw 0, 0               ; ...and its XDC packet, far
vb_want:      dw 0                  ; ...and its checksum on black
vb_nf:        dw 0
vb_ridx:      dw 0
vb_fi:        dw 0
vb_bits:      db 0
              db 0
vb_lblend:    dw 0
vb_done:      dw 0                  ; runs completed; 0xFFFF = no data file
vb_fsxm:      db 0
vb_y0:        db 0                  ; the frame's dirty band of screen rows
vb_y1:        db 0
vb_vkind:     db 0
vb_inmode:    db 0
vb_chk:       times VB_MAXF db 0    ; per frame: 1 XDC, 2 native
vb_res:       times VB_NRES dd 0    ; hundredths of a us per iteration
vb_resf:      times VB_NRES db 0    ; ' ' ok, 't' method T, '!' suspect
vb_lbl:       times 32 db 0
vb_wx:        dw 0, 0, 0            ; the worst XDC scr frame: cost, frame
vb_wn:        dw 0, 0, 0            ; ...and the worst nat scr frame

VB_BSS_OWN  equ 512                 ; the FSI block; benchlib's base must be
                                    ; 512-aligned (bl_save)
    OS88_BSS VB_BSS_OWN + BL_BSS_SIZE
    align 512
    OS88_IMAGE_END

vb_fsi      equ os88_image_end + 0

    BL_BSS os88_image_end + VB_BSS_OWN
