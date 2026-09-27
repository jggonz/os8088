; =============================================================================
; os8088 - tests/vidbench/viddisk.asm
;
; VIDDISK: what STREAMING a large file off the fixed disk costs, on the
; machine it runs on (docs/plans/VIDEO-PLAN.md wave 0 (b), and what waves 2
; and 3 added to the question).
;
;   python3 tests/viddisk.py [--machine os8088_5150_herc_hdd_sb_gla]
;
; THE STREAM is the first of STREAM.DAT, BADAPPLE.V88 or BAPPLE.V88 beside the
; bench that is at least 12 MB + 32 KB long - the emulator row makes a
; STREAM.DAT, the owner's field image carries BADAPPLE.V88, and the encoder's
; VGA disk BAPPLE.V88. With neither, the file rows
; say so and skip, and the int 13h rows still run. Every row is tick-timed
; (benchlib's method T: a disk call is tens of milliseconds and more).
;
;   READ_AT 32K @n MB     OSAPI_FILE_READ_AT at 0..12 MB. It re-walks the
;                         chain from the front every call (SPEC.md 18.4.4),
;                         so it GROWS with the offset: wave 0's slope.
;   READ_SEQ ...          OSAPI_FILE_READ_SEQ (SPEC.md 18.4.8): a SEEK's
;                         first call (one walk, timed alone), then 32 KB
;                         calls from where it stands, at 0 MB and at 12 MB -
;                         which should not differ - and 16 KB and 8 KB calls,
;                         which price a call's fixed part.
;   int 13h per 32 KB     the fixed disk's int 13h calls one 32 KB READ_SEQ
;                         makes at 12 MB, and how many land under cylinder 16
;                         (the FAT and the root). A run split at every track
;                         shows here as calls; a chain being re-read, as low
;                         ones.
;   ceiling at n%         THE SILENT PLAYER'S DISK (SPEC.md 98.3): inside an
;                         FSXF_RATE bracket at 30 Hz, a hook that holds n% of
;                         every period with interrupts ON - as the player's
;                         decode does - while the foreground streams 32 KB
;                         READ_SEQ calls for 5 s. KB/s. And one 50% row with
;                         interrupts OFF, which is what the difference is on
;                         a DMA controller: its completion interrupt waits.
;   int13 track / sector  the ROM's own int 13h on unit 80h, a whole track and
;                         one sector: the controller's ceiling.
;
; READ-ONLY. It writes nothing but VIDDISK.TXT, its report, beside itself.
; It calls int 13h itself because it is a bench; the player never does.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VIDDISK', vk_entry

VK_NRES     equ 20
VK_BUFKB    equ 40                  ; 32 KB chunks, and a whole track
VK_CHUNK    equ 32768
VK_DIV      equ 39773               ; 30.0 Hz
VK_CEILT    equ 91                  ; ticks a ceiling row streams: 5 s
VK_MB12     equ 12 * 16             ; 12 MB, as a high word

vk_entry:
    push si
    call bl_blank
    mov si, vk_s_title
    call bl_sline
    call bl_head
    mov si, vk_s_hint
    call bl_sline
    mov si, vk_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [vk_win], bx
    clc
.out:
    pop si
    ret

vk_paint:
    call bl_paint
    ret

vk_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call vk_run
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vk_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call vk_run
    call bl_paint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the bodies ---------------------------------------------------------------

; READ_AT 32 KB at [vk_off] (a dword, a cluster multiple)
vk_b_rat:
    push es
    mov es, [vk_buf]
    xor bx, bx
    mov si, [vk_fname]
    mov cx, VK_CHUNK
    mov ax, [vk_off]
    mov dx, [vk_off + 2]
    call OSAPI_FILE_READ_AT
    jnc .ok
    inc word [vk_err]
.ok:
    mov [vk_got], ax
    pop es
    ret

; READ_SEQ of [vk_cap] bytes from where the cursor stands
vk_b_seq:
    push es
    push ds
    pop es
    mov dx, [vk_buf]
    xor bx, bx
    mov cx, [vk_cap]
    mov di, vk_cur
    mov si, [vk_fname]
    call OSAPI_FILE_READ_SEQ
    jnc .ok
    inc word [vk_err]
    xor ax, ax
.ok:
    mov [vk_got], ax
    pop es
    ret

; vk_seek - DX = an offset's high word (the low is 0): zero the cursor there
vk_seek:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, vk_cur
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    mov [vk_cur + FSEQ_OFF + 2], dx
    pop es
    pop di
    pop cx
    pop ax
    ret

; int 13h, AL = [vk_nsec] sectors at the next track (head 0..H-1, cyl 1..)
vk_b_i13:
    push es
    mov es, [vk_buf]
    xor bx, bx
    mov al, [vk_nsec]
    mov ah, 2
    mov ch, [vk_cyl]
    mov cl, [vk_cylhi]              ; bits 6-7 = cylinder 8-9, sector 1
    or cl, 1
    mov dh, [vk_head]
    mov dl, 0x80
    int 0x13
    jnc .ok
    inc word [vk_err]
.ok:
    mov al, [vk_head]               ; the next track: head + 1, and the
    inc al                          ; cylinder after the last head
    cmp al, [vk_heads]
    jb .h
    xor al, al
    add byte [vk_cyl], 1
    jnc .h
    add byte [vk_cylhi], 0x40
.h:
    mov [vk_head], al
    pop es
    ret

; vk_bank - BX = the result row: [bl_lastus] into it
vk_bank:
    push ax
    push dx
    push si
    mov si, bx
    shl si, 1
    shl si, 1
    mov ax, [bl_lastus]
    mov dx, [bl_lastus + 2]
    mov [vk_res + si], ax
    mov [vk_res + si + 2], dx
    pop si
    pop dx
    pop ax
    ret

; vk_bankv - BX = the result row, DX:AX = a value to keep there
vk_bankv:
    push si
    mov si, bx
    shl si, 1
    shl si, 1
    mov [vk_res + si], ax
    mov [vk_res + si + 2], dx
    pop si
    ret

; vk_row - one timed row: SI = label, [bl_body], [bl_n], BX = the result row
vk_row:
    push ax
    mov al, 1                       ; method T
    call bl_run
    call vk_bank
    pop ax
    ret

; vk_ratrow - AX = the offset in MB, SI = the label, BX = the result row
vk_ratrow:
    push ax
    push bx
    push dx
    mov dx, 16                      ; MB -> the high word: n * 16 * 65,536
    mul dx
    mov [vk_off + 2], ax
    mov word [vk_off], 0
    push si
    call vk_b_rat                   ; once to warm whatever warms
    pop si
    mov word [bl_body], vk_b_rat
    pop dx
    pop bx
    call vk_row
    pop ax
    ret

; --- the stream: the first candidate that is 12 MB + 32 KB long ---------------
vk_find:
    mov word [vk_off], 0
    mov word [vk_off + 2], VK_MB12
    mov si, vk_f_names
.try:
    cmp byte [si], 0
    je .none
    mov [vk_fname], si
    push si
    mov word [vk_err], 0
    call vk_b_rat                   ; 32 KB at 12 MB: long enough?
    pop si
    cmp word [vk_err], 0
    jne .next
    cmp word [vk_got], VK_CHUNK
    je .found
.next:
    lodsb                           ; past this name's NUL
    or al, al
    jnz .next
    jmp short .try
.none:
    stc
    ret
.found:
    mov word [vk_err], 0
    clc
    ret

; --- the int 13h counter, on unit 80h ------------------------------------------
vk_i13on:
    push ax
    push es
    xor ax, ax
    mov es, ax
    mov [vk_i13n], ax
    mov [vk_i13lo], ax
    pushf
    cli
    mov ax, [es:0x13 * 4]
    mov [vk_i13old], ax
    mov ax, [es:0x13 * 4 + 2]
    mov [vk_i13old + 2], ax
    mov word [es:0x13 * 4], vk_i13
    mov [es:0x13 * 4 + 2], cs
    popf
    pop es
    pop ax
    ret
vk_i13off:
    push ax
    push es
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [vk_i13old]
    mov [es:0x13 * 4], ax
    mov ax, [vk_i13old + 2]
    mov [es:0x13 * 4 + 2], ax
    popf
    pop es
    pop ax
    ret
vk_i13:
    cmp dl, 0x80
    jne .chain
    inc word [cs:vk_i13n]
    test cl, 0xC0
    jnz .chain
    cmp ch, 16
    jae .chain
    inc word [cs:vk_i13lo]
.chain:
    jmp far [cs:vk_i13old]

; --- the silent player's ceiling (SPEC.md 98.3) --------------------------------
; vk_hook - FSXF_RATE's hook: hold [vk_burnc] PIT counts of the period, with
; interrupts on unless [vk_burncli]. Channel 0 counts DOWN from VK_DIV in mode
; 2, so the hook spins until the count is below VK_DIV - [vk_burnc]: exact on
; any CPU, and a hook that arrives late burns less rather than overrunning
vk_hook:
    mov cx, [vk_burnc]
    jcxz .out
    mov bx, VK_DIV
    sub bx, cx                      ; BX = the count to spin down to
    cmp byte [vk_burncli], 0
    jne .spin
    sti
.spin:
    mov al, 0x00                    ; latch channel 0
    pushf
    cli
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    popf
    xchg al, ah
    cmp ax, bx
    ja .spin
    cli
.out:
    ret

; vk_ceil - BX = the result row, AX = percent held, CL = 1 with interrupts off
vk_ceil:
    push ax
    push bx
    push cx
    push dx
    mov [vk_burncli], cl
    mov cx, VK_DIV / 100
    mul cx                          ; AX = the counts held
    mov [vk_burnc], ax
    mov [vk_ceilrow], bx
    add word [vk_ceilmb], 1         ; a fresh MB each row: nothing cached
    mov bx, [vk_win]
    mov ax, vk_ceilmain
    mov cx, FSXF_RATE
    mov dx, VK_DIV
    mov di, vk_hook
    call OSAPI_FSX_RUN
    jnc .ran
    inc word [vk_err]
.ran:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the bracket's foreground: SI = window, DS = ours. A same-mode bracket: the
; screen is left as it is and the rows are printed when it returns
vk_ceilmain:
    mov dx, [vk_ceilmb]
    shl dx, 1                       ; row n starts at 2n MB: a MB is 16 in
    shl dx, 1                       ; the high word, so 2 MB is 32
    shl dx, 1
    shl dx, 1
    shl dx, 1
    call vk_seek
    mov word [vk_cap], VK_CHUNK
    call vk_b_seq                   ; the seek's walk, outside the timing
    xor ax, ax
    mov [vk_cbytes], ax
    mov [vk_cbytes + 2], ax
    call OSAPI_GET_TICKS
    mov [vk_ct0], ax
.l:
    call vk_b_seq
    add [vk_cbytes], ax
    adc word [vk_cbytes + 2], 0
    or ax, ax
    jz .done                        ; the end of the stream
    call OSAPI_GET_TICKS
    sub ax, [vk_ct0]
    cmp ax, VK_CEILT
    jb .l
.done:
    call OSAPI_GET_TICKS
    sub ax, [vk_ct0]
    mov [vk_cticks], ax
    ; tenths of KB/s = bytes / ticks x 18.2065 x 10 / 1,024 - and bytes a
    ; tick x 182 stays inside 32 bits for any disk this side of 23 MB/s
    mov ax, [vk_cbytes]
    mov dx, [vk_cbytes + 2]
    mov cx, [vk_cticks]
    or cx, cx
    jz .zero
    call vk_div32                   ; DX:AX = bytes a tick
    mov cx, 182
    call vk_mul32                   ; x 18.2 ticks a second, x 10 for tenths
    mov cx, 1024
    call vk_div32                   ; tenths of KB/s
    jmp short .bank
.zero:
    xor ax, ax
    xor dx, dx
.bank:
    mov bx, [vk_ceilrow]
    call vk_bankv
    ret

; vk_div32 - DX:AX /= CX (32 by 16, a 32-bit quotient)
vk_div32:
    push bx
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div cx
    xchg ax, bx
    div cx
    mov dx, bx
    pop bx
    ret

; vk_mul32 - DX:AX *= CX, the product fitting 32 bits
vk_mul32:
    push bx
    mov bx, dx
    mul cx
    push dx
    push ax
    mov ax, bx
    mul cx
    pop bx
    pop dx
    add dx, ax
    mov ax, bx
    pop bx
    ret

; vk_kv - SI = label, BX = a result row: its value, as it is, into the report
vk_kv:
    push ax
    push cx
    push dx
    push si
    push bx
    shl bx, 1
    shl bx, 1
    mov ax, [vk_res + bx]
    mov dx, [vk_res + bx + 2]
    pop bx
    mov cx, 9
    call bl_kv
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
vk_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [vk_done], 0
    mov word [vk_err], 0
    mov word [bl_nrow], 0
    mov word [vk_ceilmb], 0
    mov si, vk_s_title
    call bl_sline
    cmp word [vk_buf], 0
    jne .have
    mov ax, VK_BUFKB                ; DMA-safe, all of it: a whole track is
    mov cx, VK_BUFKB                ; read into it, and on an XT controller
    call OSAPI_MEM_CLAIM_DMA        ; one crossing a 64 KB page is error 09h
    jc .fail                        ; every call (the 286's first run)
    mov [vk_buf], dx
.have:
    ; --- the fixed disk's geometry, off the ROM
    mov ah, 8
    mov dl, 0x80
    push es
    int 0x13
    pop es
    jc .fail
    mov al, cl
    and al, 0x3F
    mov [vk_spt], al
    inc dh
    mov [vk_heads], dh
    mov si, vk_r_spt
    mov al, [vk_spt]
    xor ah, ah
    xor dx, dx
    mov cx, 9
    call bl_kv
    mov si, vk_r_heads
    mov al, [vk_heads]
    xor ah, ah
    xor dx, dx
    call bl_kv

    call vk_find
    jnc .stream
    mov si, vk_s_nostr
    call bl_sline
    jmp .ctl
.stream:
    mov si, vk_s_using
    mov di, [vk_fname]
    call bl_kvs

    ; --- READ_AT at growing offsets ---------------------------------------------
    mov si, vk_s_hdra
    call bl_sline
    mov word [bl_n], 3
    xor ax, ax
    mov si, vk_r_r0
    xor bx, bx
    call vk_ratrow
    mov ax, 3
    mov si, vk_r_r3
    mov bx, 1
    call vk_ratrow
    mov ax, 6
    mov si, vk_r_r6
    mov bx, 2
    call vk_ratrow
    mov ax, 9
    mov si, vk_r_r9
    mov bx, 3
    call vk_ratrow
    mov ax, 12
    mov si, vk_r_r12
    mov bx, 4
    call vk_ratrow

    ; --- READ_SEQ: a seek, then from where it stands --------------------------------
    mov si, vk_s_hdrs
    call bl_sline
    mov word [bl_body], vk_b_seq
    mov word [vk_cap], VK_CHUNK
    xor dx, dx
    call vk_seek
    mov word [bl_n], 1
    mov si, vk_r_s0
    mov bx, 7
    call vk_row                     ; the seek to 0: its first call
    mov word [bl_n], 8
    mov si, vk_r_q0
    mov bx, 8
    call vk_row
    mov dx, VK_MB12
    call vk_seek
    mov word [bl_n], 1
    mov si, vk_r_s12
    mov bx, 9
    call vk_row                     ; the seek to 12 MB: ONE walk
    mov word [bl_n], 8
    call vk_i13on
    mov si, vk_r_q12
    mov bx, 10
    call vk_row
    call vk_i13off
    mov ax, [vk_i13n]
    mov dx, [vk_i13lo]
    mov bx, 13
    call vk_bankv                   ; calls, and the low ones, for 8 x 32 KB
    mov word [vk_cap], 16384
    mov si, vk_r_q16
    mov bx, 11
    call vk_row
    mov word [vk_cap], 8192
    mov si, vk_r_q8
    mov bx, 12
    call vk_row
    mov si, vk_r_i13n
    mov ax, [vk_i13n]
    xor dx, dx
    mov cx, 9
    call bl_kv
    mov si, vk_r_i13lo
    mov ax, [vk_i13lo]
    call bl_kv

    ; --- the silent player's ceiling: a 30 Hz hook holding n% --------------------------
    mov si, vk_s_hdrc
    call bl_sline
    mov si, vk_s_hdrc2
    call bl_sline
    xor ax, ax
    xor cl, cl
    mov bx, 14
    call vk_ceil
    mov ax, 25
    mov bx, 15
    call vk_ceil
    mov ax, 50
    mov bx, 16
    call vk_ceil
    mov ax, 75
    mov bx, 17
    call vk_ceil
    mov ax, 50
    mov cl, 1
    mov bx, 18
    call vk_ceil
    mov si, vk_r_c0
    mov bx, 14
    call vk_kv
    mov si, vk_r_c25
    inc bx
    call vk_kv
    mov si, vk_r_c50
    inc bx
    call vk_kv
    mov si, vk_r_c75
    inc bx
    call vk_kv
    mov si, vk_r_c50i
    inc bx
    call vk_kv

    ; --- the controller: whole tracks, then single sectors --------------------
.ctl:
    mov si, vk_s_hdri
    call bl_sline
    mov byte [vk_cyl], 1
    mov byte [vk_cylhi], 0
    mov byte [vk_head], 0
    mov al, [vk_spt]
    mov [vk_nsec], al
    mov word [bl_n], 40
    mov word [bl_body], vk_b_i13
    mov si, vk_r_trk
    mov bx, 5
    call vk_row
    mov byte [vk_nsec], 1
    mov word [bl_n], 60
    mov si, vk_r_sec
    mov bx, 6
    call vk_row

    mov si, vk_r_err
    mov ax, [vk_err]
    xor dx, dx
    mov cx, 9
    call bl_kv
    call bl_operator
    mov si, vk_f_txt                ; the report, beside the bench
    call bl_save
    inc word [vk_done]
    jmp short .end
.fail:
    mov si, vk_s_fail
    call bl_sline
    mov word [vk_done], 0xFFFF
.end:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%define BL_ARENA_BYTES 5000
%include "benchlib.inc"

vk_tpl:
    dw 7, 22, 632, 300
    dw vk_ttl, vk_paint, vk_onkey, vk_onclick

vk_ttl:       db 'Video Disk Bench', 0
vk_f_names:   db 'STREAM.DAT', 0, 'BADAPPLE.V88', 0, 'BAPPLE.V88', 0, 0
vk_f_txt:     db 'VIDDISK.TXT', 0
vk_s_title:   db 'VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)', 0
vk_s_hint:    db 'Click, or press R, to run. It only reads, and saves VIDDISK.TXT.', 0
vk_s_nostr:   db 'No STREAM.DAT or (BAD)APPLE.V88 of 12 MB here: file rows skipped', 0
vk_s_using:   db 'the stream', 0
vk_s_hdra:    db '-- READ_AT 32 KB, by offset (it re-walks the chain) --', 0
vk_s_hdrs:    db '-- READ_SEQ: a seek is one walk, then from where it stands --', 0
vk_s_hdrc:    db '-- the silent player: 32 KB READ_SEQ, 30 Hz hook holding n% --', 0
vk_s_hdrc2:   db '   (KB/s x 10; the hook has interrupts ON unless it says off)', 0
vk_s_hdri:    db '-- int 13h on unit 80h: a whole track, one sector --', 0
vk_s_fail:    db 'NO CLAIM, OR NO FIXED DISK ANSWERED', 0
vk_r_r0:      db 'READ_AT 32K @0 MB', 0
vk_r_r3:      db 'READ_AT 32K @3 MB', 0
vk_r_r6:      db 'READ_AT 32K @6 MB', 0
vk_r_r9:      db 'READ_AT 32K @9 MB', 0
vk_r_r12:     db 'READ_AT 32K @12 MB', 0
vk_r_s0:      db 'READ_SEQ seek 0, 1st', 0
vk_r_q0:      db 'READ_SEQ 32K @0 MB', 0
vk_r_s12:     db 'READ_SEQ seek 12MB 1st', 0
vk_r_q12:     db 'READ_SEQ 32K @12 MB', 0
vk_r_q16:     db 'READ_SEQ 16K @12 MB', 0
vk_r_q8:      db 'READ_SEQ 8K @12 MB', 0
vk_r_i13n:    db 'int13 calls, 8 x 32K', 0
vk_r_i13lo:   db '...under cylinder 16', 0
vk_r_c0:      db 'ceiling, hook 0%', 0
vk_r_c25:     db 'ceiling, hook 25%', 0
vk_r_c50:     db 'ceiling, hook 50%', 0
vk_r_c75:     db 'ceiling, hook 75%', 0
vk_r_c50i:    db 'ceiling, 50% ints off', 0
vk_r_trk:     db 'int13 one track', 0
vk_r_sec:     db 'int13 one sector', 0
vk_r_spt:     db 'sectors per track', 0
vk_r_heads:   db 'heads', 0
vk_r_err:     db 'errors (any row)', 0

vk_win:       dw 0
vk_buf:       dw 0
vk_fname:     dw 0
vk_off:       dw 0, 0
vk_cap:       dw 0
vk_got:       dw 0
vk_err:       dw 0
vk_done:      dw 0
vk_i13old:    dw 0, 0
vk_i13n:      dw 0
vk_i13lo:     dw 0
vk_burnc:     dw 0
vk_burncli:   db 0
              db 0
vk_ceilrow:   dw 0
vk_ceilmb:    dw 0
vk_ct0:       dw 0
vk_cticks:    dw 0
vk_cbytes:    dw 0, 0
vk_spt:       db 0
vk_heads:     db 0
vk_nsec:      db 0
vk_cyl:       db 0
vk_cylhi:     db 0
vk_head:      db 0
vk_cur:       times FSEQ_SIZE db 0
vk_res:       times VK_NRES dd 0

VK_BSS_OWN  equ 512
    OS88_BSS VK_BSS_OWN + BL_BSS_SIZE
    align 512
    OS88_IMAGE_END

    BL_BSS os88_image_end + VK_BSS_OWN
