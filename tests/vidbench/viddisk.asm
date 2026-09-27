; =============================================================================
; os8088 - tests/vidbench/viddisk.asm
;
; VIDDISK: what STREAMING a large file off the fixed disk costs, on the
; machine it runs on (docs/plans/VIDEO-PLAN.md wave 0 (b), and what waves 2
; and 3 added to the question).
;
;   python3 tests/viddisk.py [--machine os8088_5150_herc_hdd_sb_gla]
;   python3 tests/viddisk.py --floppy     (the 360 KB field floppy's path)
;
; THE STREAM is the first of STREAM.DAT, BADAPPLE.V88 or BAPPLE.V88 that is at
; least 12 MB + 32 KB long - the emulator row makes a STREAM.DAT, the owner's
; field image carries BADAPPLE.V88, and the encoder's VGA disk BAPPLE.V88.
; WHERE it is looked for depends on where the bench is: beside itself on a
; fixed disk (drive C: or after), as the field VHDs have it, and in C:'s ROOT
; when the bench runs off a floppy - which is `make viddisk360`, the 360 KB
; disk for a machine nobody can copy 12 MB onto (docs/plans/VIDEO-PLAN.md
; 15.8). With none, the file rows say so and skip, and the int 13h rows still
; run. Every row is tick-timed (benchlib's method T: a disk call is tens of
; milliseconds and more).
;
; W  MAKES the stream: STREAM.DAT, 12.5 MB, every dword its own offset in the
;    file, written where R will look for it in 32 KB OSAPI_FILE_APPENDs. It
;    checks the room first, and it RESUMES - a STREAM.DAT that is a whole
;    number of chunks short of 12.5 MB is carried on from its end, so a write
;    interrupted by a reset costs only what was not written. Minutes: every
;    append walks the chain to its last cluster (SPEC.md 18.4.7.3), so it
;    slows as the file grows. It reports its own rate (KB/s x 10, the whole
;    write) and saves the report as VDWRITE.TXT beside the bench.
; D  deletes STREAM.DAT again, so the disk gets its 12.5 MB back.
;
; With a STREAM.DAT, R also CHECKS the data at 12 MB, read by READ_AT and by
; READ_SEQ: a stream that times well and reads the wrong bytes is not a
; measurement.
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
; R IS READ-ONLY: it writes nothing but VIDDISK.TXT, its report, beside
; itself. Only W writes (STREAM.DAT and VDWRITE.TXT) and only D deletes
; (STREAM.DAT). It calls int 13h itself because it is a bench; the player
; never does.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VIDDISK', vk_entry

VK_NRES     equ 20
VK_BUFKB    equ 40                  ; 32 KB chunks, and a whole track
VK_CHUNK    equ 32768
VK_DIV      equ 39773               ; 30.0 Hz
VK_CEILT    equ 91                  ; ticks a ceiling row streams: 5 s
VK_MB12     equ 12 * 16             ; 12 MB, as a high word
VK_NCHUNK   equ 400                 ; STREAM.DAT: 400 x 32 KB = 12.5 MB
VK_DRV_C    equ 2                   ; OSAPI_FILE_HERE's drives: A: is 0

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
    cmp bl, 'w'
    je .write
    cmp bl, 'd'
    je .del
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.write:
    call vk_wrun
    jmp short .paint
.del:
    call vk_drun
    jmp short .paint
.run:
    call vk_run
.paint:
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
    mov byte [vk_dchk], 0
    mov si, vk_s_title
    call bl_sline
    call vk_claim
    jc .fail
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

    call vk_toc                     ; where the stream lives: C: off a floppy
    jnc .look
    mov si, vk_s_noc
    call bl_sline
    jmp .ctl
.look:
    call vk_where
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
    mov dx, VK_MB12
    call vk_chk                     ; READ_AT's bytes at 12 MB

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
    mov dx, VK_MB12
    call vk_chk                     ; ...and READ_SEQ's
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
    call vk_dkv                     ; the data check's verdict

    ; --- the controller: whole tracks, then single sectors --------------------
.ctl:
    call vk_back                    ; the report goes beside the bench
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
    call vk_back
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

; --- where the stream lives, and the buffer -----------------------------------

; vk_claim - the DMA-safe buffer, once: CF=1 there is none. DMA-safe, all of
; it: a whole track is read into it and STREAM.DAT written out of it, and on
; an XT controller a transfer crossing a 64 KB page is error 09h every call
; (the 286's first run)
vk_claim:
    cmp word [vk_buf], 0
    jne .have
    push ax
    push cx
    push dx
    mov ax, VK_BUFKB
    mov cx, VK_BUFKB
    call OSAPI_MEM_CLAIM_DMA
    jc .no
    mov [vk_buf], dx
.no:
    pop dx                          ; CF is the claim's
    pop cx
    pop ax
    ret
.have:
    clc
    ret

; vk_toc - stand where the stream lives: beside the bench on a fixed disk,
; C:'s root when the bench is on a floppy - a 360 KB disk cannot hold 12.5 MB,
; and on the 5150 there is no other way to put a file that size on its hard
; disk than to write it there. CF=1: no C: answered (the hard disk is not
; mounted), and the instance is back where it was. [vk_moved] tells vk_back
; whether there is a way back to take.
vk_toc:
    push ax
    push bx
    push dx
    mov byte [vk_moved], 0
    call OSAPI_FILE_HERE            ; BL = our drive, DX = our folder
    mov [vk_hdrv], bl
    mov [vk_hdir], dx
    cmp bl, VK_DRV_C
    jae .ok                         ; a fixed disk: the stream is beside us
    mov bl, VK_DRV_C
    xor dx, dx                      ; C:'s root
    call OSAPI_FILE_GOTO
    mov byte [vk_moved], 1
    jnc .ok
    call vk_back                    ; it left us at a root: go home
    stc
    jmp short .out
.ok:
    clc
.out:
    pop dx
    pop bx
    pop ax
    ret

; vk_back - home again, if vk_toc moved us. A remount: floppy I/O. It is safe
; to call twice
vk_back:
    cmp byte [vk_moved], 0
    je .out
    push ax
    push bx
    push dx
    mov bl, [vk_hdrv]
    mov dx, [vk_hdir]
    call OSAPI_FILE_GOTO            ; CF=1 the floppy went: nothing to do
    mov byte [vk_moved], 0
    pop dx
    pop bx
    pop ax
.out:
    ret

; vk_where - the report's line saying which drive the stream is on
vk_where:
    push ax
    push si
    push di
    call bl_drive                   ; AL = 'A'..
    mov [vk_drvs], al
    mov si, vk_r_where
    mov di, vk_drvs
    call bl_kvs
    pop di
    pop si
    pop ax
    ret

; vk_size - STREAM.DAT where we stand: CF=0 DX:AX = its size, CF=1 there is
; none. OSAPI_FILE_FIND by ordinal, so it reads the folder: it is asked once
vk_size:
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    xor cx, cx
.next:
    mov di, vk_fnd
    call OSAPI_FILE_FIND            ; CX = the next ordinal
    jc .out                         ; the end: none
    cmp word [vk_fnd + 14], OSAPI_FT_DIR
    jae .next                       ; a folder, or '..'
    mov si, vk_f_names              ; 'STREAM.DAT', 0 - the first name
    mov di, vk_fnd
    push cx
    mov cx, 11
    repe cmpsb
    pop cx
    jne .next
    mov ax, [vk_fnd + 18]
    mov dx, [vk_fnd + 20]
    clc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; vk_fill - AX = k: the buffer as STREAM.DAT's chunk k, every dword its own
; offset in the file. A chunk is 32 KB on a 32 KB boundary, so its high word
; is k / 2 throughout and its low word runs from (k & 1) x 32768 by fours
vk_fill:
    push ax
    push cx
    push dx
    push di
    push es
    mov dx, ax
    shr dx, 1                       ; the offsets' high word
    and ax, 1
    mov cl, 15
    shl ax, cl                      ; ...and the first low word
    mov es, [vk_buf]
    xor di, di
    mov cx, VK_CHUNK / 4
    cld
.l:
    stosw
    xchg ax, dx
    stosw
    xchg ax, dx
    add ax, 4
    loop .l
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; vk_chk - DX = the high word of the offset the buffer was read from (its low
; word 0): does the buffer hold STREAM.DAT's pattern from there? Its first and
; last dwords are asked, which a read of the wrong clusters, a short read and
; a transfer that never happened all fail. Only STREAM.DAT has a pattern.
; [vk_dchk]: 0 not asked, 1 every check held, 2 one did not
vk_chk:
    cmp word [vk_fname], vk_f_names
    jne .out
    push es
    mov es, [vk_buf]
    cmp word [es:0], 0
    jne .bad
    cmp [es:2], dx
    jne .bad
    cmp word [es:VK_CHUNK - 4], VK_CHUNK - 4
    jne .bad
    cmp [es:VK_CHUNK - 2], dx
    jne .bad
    cmp byte [vk_dchk], 2
    je .done
    mov byte [vk_dchk], 1
    jmp short .done
.bad:
    mov byte [vk_dchk], 2
.done:
    pop es
.out:
    ret

; vk_dkv - the data check's line
vk_dkv:
    push si
    push di
    mov di, vk_s_dnone
    cmp byte [vk_dchk], 1
    jb .say
    mov di, vk_s_dok
    je .say
    mov di, vk_s_dbad
.say:
    mov si, vk_r_dchk
    call bl_kvs
    pop di
    pop si
    ret

; vk_wprog - the status row while W writes: [vk_wk] chunks of VK_NCHUNK
vk_wprog:
    push ax
    push cx
    push dx
    push si
    push di
    call bl_lclr
    mov si, vk_s_wprog
    xor di, di
    call bl_lput
    mov ax, [vk_wk]
    mov cx, 32
    mul cx                          ; KB so far
    mov di, 24
    mov cx, 6
    call bl_dec
    mov si, vk_s_wof
    mov di, 31
    call bl_lput
    mov si, bl_lscr
    call bl_progress
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; vk_wrun - W: make STREAM.DAT where R will look for it, 12.5 MB of it
vk_wrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [vk_wdone], 0
    mov word [bl_nrow], 0
    mov word [vk_err], 0
    mov si, vk_s_wtitle
    call bl_sline
    call vk_claim
    jc .fail
    call vk_toc
    jnc .there
    mov si, vk_s_noc
    call bl_sline
    jmp .out
.there:
    call vk_where

    ; --- how much is there already: a whole number of chunks carries on ---
    xor bx, bx                      ; BX = the first chunk to write
    call vk_size
    jc .room                        ; none: from the start
    cmp dx, VK_NCHUNK / 2
    jae .have                       ; 12.5 MB or more: nothing to do
    test ax, VK_CHUNK - 1
    jnz .again                      ; torn: start again
    mov bx, dx
    shl bx, 1
    rol ax, 1                       ; bit 15, the odd chunk, to bit 0
    and ax, 1
    add bx, ax
    or bx, bx
    jz .again                       ; an empty file: WRITE makes it anew
    mov ax, bx
    mov cx, 32
    mul cx
    xor dx, dx
    mov si, vk_r_wresume
    mov cx, 9
    call bl_kv
    jmp short .room
.again:
    mov si, vk_f_names
    call OSAPI_FILE_DELETE
    xor bx, bx
.room:
    mov [vk_wk], bx
    mov [vk_wfrom], bx

    ; --- room for the rest: KB free against KB to write ---
    call OSAPI_FILE_DFREE           ; DX:AX = free bytes - and it WRITES BX
    jc .dfree
    mov cx, 1024
    call vk_div32                   ; DX:AX = free KB
    mov si, vk_r_wfree
    mov cx, 9
    call bl_kv
    push dx
    push ax
    mov ax, VK_NCHUNK
    sub ax, [vk_wk]
    mov cx, 32
    mul cx                          ; AX = KB to write (DX = 0)
    mov si, vk_r_wneed
    mov cx, 9
    call bl_kv
    mov cx, ax
    pop ax
    pop dx
    or dx, dx
    jnz .write                      ; 64 MB free or more
    cmp ax, cx
    jae .write
    mov si, vk_s_wroom
    call bl_sline
    jmp .home

    ; --- the chunks: a WRITE makes the file, APPENDs grow it ---
.write:
    call OSAPI_GET_TICKS
    mov [vk_ct0], ax
.chunk:
    mov bx, [vk_wk]
    cmp bx, VK_NCHUNK
    jae .done
    test bl, 3
    jnz .fill
    call vk_wprog                   ; every 128 KB
.fill:
    mov ax, bx
    call vk_fill
    mov es, [vk_buf]
    mov si, vk_f_names
    mov cx, VK_CHUNK
    or bx, bx
    mov bx, 0                       ; ES:BX = the chunk (flags kept)
    jnz .app
    xor dx, dx                      ; DX:CX = the whole of it
    call OSAPI_FILE_WRITE
    jmp short .wrote
.app:
    call OSAPI_FILE_APPEND
.wrote:
    push ds
    pop es
    jc .werr
    inc word [vk_wk]
    jmp short .chunk
.werr:
    xor dx, dx                      ; AX = FERR_*
    mov si, vk_r_werr
    mov cx, 9
    call bl_kv
    mov ax, [vk_wk]
    mov cx, 32
    mul cx
    mov si, vk_r_wat
    mov cx, 9
    call bl_kv
    inc word [vk_err]
.done:
    call OSAPI_GET_TICKS
    sub ax, [vk_ct0]
    mov [vk_cticks], ax
    mov ax, [vk_wk]
    sub ax, [vk_wfrom]              ; chunks written by this run
    jz .said
    mov cx, 32
    mul cx
    xor dx, dx
    mov si, vk_r_wkb
    mov cx, 9
    call bl_kv
    ; tenths of KB/s: KB x 182 / ticks - KB x 182 fits 32 bits
    mov cx, 182
    mul cx                          ; DX:AX = KB x 182 (KB is under 12,801)
    mov cx, [vk_cticks]
    or cx, cx
    jz .said
    call vk_div32
    mov si, vk_r_wrate
    mov cx, 9
    call bl_kv
    mov ax, [vk_cticks]             ; seconds: ticks x 10 / 182
    mov cx, 10
    mul cx
    mov cx, 182
    div cx
    xor dx, dx
    mov si, vk_r_wsecs
    mov cx, 9
    call bl_kv
.said:
    cmp word [vk_err], 0
    jne .home
    mov si, vk_s_wdone
    call bl_sline
    jmp short .home
.dfree:
    mov si, vk_s_wdfree
    call bl_sline
    jmp short .home
.have:
    mov si, vk_s_whave
    call bl_sline
.home:
    call vk_back
    mov si, vk_f_wtxt               ; the report, beside the bench
    call bl_save
    jmp short .out
.fail:
    mov si, vk_s_fail
    call bl_sline
.out:
    inc word [vk_wdone]             ; for a harness: W has finished
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_drun - D: STREAM.DAT deleted, from where W wrote it
vk_drun:
    push ax
    push cx
    push dx
    push si
    mov word [vk_ddone], 0
    mov word [bl_nrow], 0
    mov si, vk_s_dtitle
    call bl_sline
    call vk_toc
    jnc .there
    mov si, vk_s_noc
    call bl_sline
    jmp short .out
.there:
    call vk_where
    mov si, vk_f_names
    call OSAPI_FILE_DELETE
    jnc .gone
    xor dx, dx                      ; AX = FERR_*: 4 is "there was none"
    mov si, vk_r_derr
    mov cx, 9
    call bl_kv
    jmp short .home
.gone:
    mov si, vk_s_dgone
    call bl_sline
.home:
    call vk_back
.out:
    inc word [vk_ddone]             ; for a harness: D has finished
    pop si
    pop dx
    pop cx
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
vk_f_wtxt:    db 'VDWRITE.TXT', 0
vk_s_title:   db 'VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)', 0
vk_s_hint:    db 'R (or a click) runs: reads only, saves VIDDISK.TXT. No stream? W.', 0
vk_s_nostr:   db 'No STREAM.DAT or (BAD)APPLE.V88 of 12 MB: press W to write one', 0
vk_s_noc:     db 'NO C: - mount the hard disk (Control Panel), then run again', 0
vk_s_wtitle:  db 'VIDDISK W - writing STREAM.DAT, 12.5 MB, for R to read back', 0
vk_s_wprog:   db 'W: writing STREAM.DAT', 0
vk_s_wof:     db 'of 12800 KB - minutes', 0
vk_s_wroom:   db 'NOT ENOUGH ROOM for STREAM.DAT: nothing written', 0
vk_s_wdfree:  db 'THE DISK DID NOT SAY HOW MUCH IS FREE: nothing written', 0
vk_s_whave:   db 'STREAM.DAT is already whole: press R to run the bench', 0
vk_s_wdone:   db 'STREAM.DAT is whole: press R to run the bench, D to delete it', 0
vk_s_dtitle:  db 'VIDDISK D - deleting STREAM.DAT', 0
vk_s_dgone:   db 'STREAM.DAT deleted: its 12.5 MB are free again', 0
vk_s_dok:     db 'ok', 0
vk_s_dbad:    db 'BAD - not the bytes W wrote', 0
vk_s_dnone:   db 'not STREAM.DAT: none', 0
vk_drvs:      db '?:', 0
vk_r_where:   db 'the stream is on', 0
vk_r_dchk:    db 'data at 12 MB', 0
vk_r_wresume: db 'already written (KB)', 0
vk_r_wfree:   db 'free on the disk (KB)', 0
vk_r_wneed:   db 'to write (KB)', 0
vk_r_werr:    db 'WRITE FAILED, FERR_', 0
vk_r_wat:     db '...after (KB)', 0
vk_r_wkb:     db 'written this run (KB)', 0
vk_r_wrate:   db 'write KB/s x 10', 0
vk_r_wsecs:   db 'write took (s)', 0
vk_r_derr:    db 'DELETE FAILED, FERR_', 0
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
vk_moved:     db 0
vk_hdrv:      db 0
vk_dchk:      db 0
              db 0
vk_hdir:      dw 0
vk_wk:        dw 0
vk_wdone:     dw 0
vk_ddone:     dw 0
vk_wfrom:     dw 0
vk_fnd:       times OSAPI_FIND_SZ db 0
vk_cur:       times FSEQ_SIZE db 0
vk_res:       times VK_NRES dd 0

VK_BSS_OWN  equ 512
    OS88_BSS VK_BSS_OWN + BL_BSS_SIZE
    align 512
    OS88_IMAGE_END

    BL_BSS os88_image_end + VK_BSS_OWN
