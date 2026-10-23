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
;    file, written where R will look for it in 32 KB calls of the HELD
;    streaming write (OSAPI_FILE_WRITE_SEQ, WSEQF_HELD, SPEC.md 18.4.9): no
;    walk, and the FAT and the entry written once at the close. It checks the
;    room first, and it RESUMES - a STREAM.DAT that is a whole number of
;    chunks short of 12.5 MB is carried on from its end, so a write
;    interrupted by a reset costs only what was not written. It reports its
;    own rate (KB/s x 10, the whole write) and WHICH WRITER made it, and
;    saves the report as VDWRITE.TXT beside the bench.
; A  the same with OSAPI_FILE_APPEND, the old writer, which walks the chain
;    to its last cluster every call (SPEC.md 18.4.7.3) and slows as the file
;    grows: the A/B. It was W until 2026-10-07, when the owner read ~30 KB/s
;    off the ST-225 and could not tell from the report that it was APPEND.
;    P is WRITE_SEQ PLAIN (committed every call) and H is W again; U, I, K
;    and F are the held stream's edge cases, for tests/viddisk.py.
; D  deletes STREAM.DAT again, so the disk gets its 12.5 MB back.
; X  EXTENDED MEMORY (docs/plans/VIDEO-XMS-PLAN.md 3.2): what a stream's read-
;    ahead BANK in XMS would cost this machine. OSAPI_XMEM_COPY up and down
;    at 32, 16, 8 and 4 KB a call, PIT-timed with the copy's own interrupts-
;    off window inside the span (on a 286 the copy is int 15h AH=87h, and the
;    whole of it is masked) - after a CHECK that a 32 KB round trip comes back
;    the same bytes, because a copy that times well and moves the wrong ones
;    is not a measurement. Then, if the stream is there, the bank's FILL: 5 s
;    of 32 KB READ_SEQ calls with nothing else, and 5 s of the same with each
;    chunk copied up behind the last, KB/s x 10 - the two rows' ratio is
;    what banking a byte costs over reading it, and the copy rows say how the
;    copy's cost splits into a call's fixed part and its bytes. Saves
;    VDXMS.TXT beside the bench; writes nothing else, and frees the block.
; E  EXPANDED MEMORY (docs/plans/VIDEO-XMS-PLAN.md 10): a LO-TECH-STYLE EMS
;    board - four WRITE-ONLY page registers, each mapping a 16 KB page into
;    one quarter of a 64 KB frame - found where EMS.DRV finds one (frames
;    E000h, D000h, C000h at bases 260h, 264h, 268h, 26Ch and 288h): MartyPC's
;    model, 86Box's "Lo-tech EMS Board", and a PicoMEM's EMS at its own
;    defaults, D000h at 288h. The row after the title says where it answered.
;    THE PROBE READS FIRST:
;    an option ROM anywhere in the frame (55h AAh on a 2 KB boundary) is
;    somebody else's, and E says so and writes NO PORT. Then it maps pages 0
;    and 1, writes each,
;    and maps page 0 into both quarters: paging is proven only if the second
;    quarter then reads page 0's byte. The board is sized by a signature per
;    page, written from page 127 DOWN (a smaller board aliases, and the lowest
;    write to a physical page is its own number). Then: four OUTs mapping the
;    whole frame, 16 KB rep movsw frame -> RAM, RAM -> frame and RAM -> RAM,
;    and - if the stream is there - 5 s of 32 KB READ_SEQ into RAM against 5 s
;    of the same STRAIGHT INTO THE FRAME, a new pair of pages each chunk, the
;    last chunk checked against STREAM.DAT's pattern through the frame. That
;    last pair is the question VIDEO-XMS-PLAN 10 turns on: whether a disk read
;    can fill a bank with no copy at all. Saves VDEMS.TXT beside the bench.
; C  WHAT A READ COSTS THE CPU (docs/plans/DISK-CPU-PLAN.md 7), on a 286
;    with an IDE disk: sectors read through the task file by the bench
;    itself, each PIT-timed with interrupts off - the wait for DRQ and the
;    256 words, with `rep insw` and with the old `in`/`stosw`/`loop` - then
;    4 MB streamed as 64-sector commands against 4 MB of 32 KB READ_SEQ
;    calls on the stream: what the second costs over the first is the
;    kernel's and HDD.DRV's own CPU. Read-only. Saves VDCPU.TXT.
; L  LOADALL ON A 286 (docs/plans/LOADALL-PLAN.md 8): whether this CPU's
;    LOADALL runs, whether A20 is open before and after an OSAPI_XMEM_COPY,
;    32 KB READ and WRITTEN in extended memory in place and checked, a
;    borrow's fixed cost and the copies by LOADALL against OSAPI_XMEM_COPY,
;    a read above 1 MB against below it, and - on an IDE drive at 1F0h -
;    sectors read by the task file STRAIGHT INTO XMS, checked, timed, and
;    counted by how many each interrupts-off borrow took. Disk read-only.
;    M is L with the CS cache's access byte 9Bh where L uses 93h. Saves
;    VDLALL.TXT. vk_larun's header has the rest.
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
;   across a head / cyl   DOES THE ROM CARRY ONE CALL ONTO THE NEXT HEAD? The
;                         kernel ends every fixed-disk run at the track
;                         (SPEC.md 52.1, disk.inc's run loop), so a 32 KB
;                         READ_SEQ is ~5 calls on a 17-sector disk and each
;                         pays the call's set-up and half a turn. A run read
;                         a track a call is the truth; the same run in ONE
;                         call, into a buffer filled with a pattern first,
;                         must checksum the same - a ROM that answers CF=0
;                         for a short or a wrong-head read reads as WRONG
;                         BYTES, which is the failure that would be silent in
;                         the kernel. Across a head, then across a cylinder.
;   N sectors one call    ...and what it buys: the same N sectors (a whole
;   / a call a track      cylinder, or 40 KB) from sector 1 of fresh
;                         cylinders, in one call and a call a track. Only
;                         timed in one call when the crossing read was right.
;
; R IS READ-ONLY: it writes nothing but VIDDISK.TXT, its report, beside
; itself. Only W writes (STREAM.DAT and VDWRITE.TXT) and only D deletes
; (STREAM.DAT). It calls int 13h itself because it is a bench; the player
; never does.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VIDDISK', vk_entry

VK_NRES     equ 28
VK_BUFKB    equ 40                  ; 32 KB chunks, and a whole track
VK_CHUNK    equ 32768
%ifndef VK_WSUB
VK_WSUB     equ VK_CHUNK            ; W hands each 32 KB chunk to the kernel
%endif                              ; in calls of this many bytes: 8192 is
                                    ; FTPD's STOR (SPEC.md 77.49)
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
    mov byte [vk_wmode], 2          ; W: THE STREAMING WRITE, HELD - the
    cmp bl, 'w'                     ; fast path (SPEC.md 18.4.9), and what
    je .write                       ; the field means by "writing a file"
    mov byte [vk_wmode], 0
    cmp bl, 'a'                     ; A: OSAPI_FILE_APPEND, the old writer -
    je .write                       ; the A/B (it was W until 2026-10-07)
    inc byte [vk_wmode]             ; 'p': OSAPI_FILE_WRITE_SEQ, plain
    cmp bl, 'p'
    je .write
    inc byte [vk_wmode]             ; 'h': ...HELD (SPEC.md 18.4.9)
    cmp bl, 'h'
    je .write
    inc byte [vk_wmode]             ; 'u': held and never CLOSED - the unlock
    cmp bl, 'u'                     ; at the end of this callback commits it
    je .write
    inc byte [vk_wmode]             ; 'i': held, with ANOTHER file written
    cmp bl, 'i'                     ; every 64 chunks, whose gate commits it
    je .write
    inc byte [vk_wmode]             ; 'k': held, and at chunk 64 the stream is
    cmp bl, 'k'                     ; DELETED and another file made in its
    je .write                       ; place - the gate must commit it first
    inc byte [vk_wmode]             ; 'f': held, with NO room check - the
    cmp bl, 'f'                     ; volume fills mid-stream and the failed
    je .write                       ; call must abandon the hold, not keep it
    cmp bl, 'd'
    je .del
    cmp bl, 'x'                     ; X: extended memory (the header's X)
    je .xms
    cmp bl, 'e'                     ; E: expanded memory (the header's E)
    je .ems
    cmp bl, 'c'                     ; C: what a read costs the CPU
    je .cpu
    mov byte [vk_lacs], 0x93        ; L: LOADALL, the CS cache's access 93h
    cmp bl, 'l'
    je .lall
    mov byte [vk_lacs], 0x9B        ; M: the same with 9Bh
    cmp bl, 'm'
    je .lall
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
.xms:
    call vk_xmrun
    jmp short .paint
.ems:
    call vk_emrun
    jmp short .paint
.cpu:
    call vk_cprun
    jmp short .paint
.lall:
    call vk_larun
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
    mov bx, [vk_rdoff]              ; (0 but for vk_xrdpl's rows)
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
.kbs:                               ; (vk_xmfill's too)
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
    call vk_mh                      ; one call across a head?

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
    mov al, [vk_wmode]              ; WHICH WRITER: a rate is nothing without
    xor ah, ah                      ; it - the owner read 30 KB/s off W and
    shl ax, 1                       ; could not tell it was APPEND
    mov bx, ax
    mov di, [vk_wpaths + bx]
    mov si, vk_r_wpath
    call bl_kvs
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
    cmp byte [vk_wmode], 6          ; 'f' writes into too little ON PURPOSE
    je .write
    mov si, vk_s_wroom
    call bl_sline
    jmp .home

    ; --- the chunks: a WRITE makes the file, APPENDs grow it ---
.write:
    xor ax, ax                      ; the WRITE_SEQ token, none yet, and
    mov [vk_wtok], ax               ; HELD from mode 2 up
    cmp byte [vk_wmode], 2
    jb .nohold
    mov al, WSEQF_HELD
.nohold:
    mov [vk_wflg], al
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
    mov si, vk_f_names
    xor bx, bx                      ; ES:BX = the next VK_WSUB of the chunk
.sub:
    mov es, [vk_buf]
    mov cx, VK_WSUB
    cmp word [vk_wk], 0
    jne .app
    or bx, bx
    jnz .app
    xor dx, dx                      ; DX:CX = the first call's whole count
    call OSAPI_FILE_WRITE
    jmp short .wrote
.app:
    cmp byte [vk_wmode], 0
    jne .seq
    call OSAPI_FILE_APPEND
    jmp short .wrote
.seq:
    mov di, [vk_wtok]               ; ES:BX = the bytes, DI = the token
    mov al, [vk_wflg]
    call OSAPI_FILE_WRITE_SEQ
    mov [vk_wtok], di
.wrote:
    push ds
    pop es
    jc .werr
    add bx, VK_WSUB
    cmp bx, VK_CHUNK
    jb .sub
    inc word [vk_wk]
    cmp byte [vk_wmode], 4
    jb .chunk
    cmp byte [vk_wmode], 5          ; 'i' and 'k' only
    ja .chunk
    test word [vk_wk], 63
    jnz .chunk
    cmp byte [vk_wmode], 5
    jne .side
    mov si, vk_f_names              ; 'k': the stream itself goes...
    call OSAPI_FILE_DELETE
    jnc .side
    inc word [vk_err]
.side:
    mov si, vk_f_side               ; another file, mid-stream (18.4.9)
    mov bx, vk_s_wtitle
    mov cx, 16
    xor dx, dx
    call OSAPI_FILE_WRITE
    jnc .chunk
    inc word [vk_err]
    jmp .chunk
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
    cmp byte [vk_wmode], 2          ; a HELD stream is committed by its close
    je .close                       ; ('u' leaves it to the unlock)
    cmp byte [vk_wmode], 4
    je .close
    cmp byte [vk_wmode], 6
    jne .closed
.close:
    xor cx, cx
    call OSAPI_FILE_WRITE_SEQ
    jnc .closed
    inc word [vk_err]
.closed:
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
    cmp byte [vk_wmode], 3          ; 'u' touches NO file and mounts NOTHING
    je .out                         ; after W, not even the way home: the
    call vk_back                    ; unlock ending this callback is the only
    mov si, vk_f_wtxt               ; commit there is (SPEC.md 18.4.9)               ; the report, beside the bench
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

; =============================================================================
; vk_xmrun - X: OSAPI_XMEM_COPY on this machine (see the header)
; =============================================================================
VK_XMN      equ 8                   ; iterations of a copy row
VK_XSWKB    equ 1024                ; vk_xsweep's block
VK_XMKB     equ 64                  ; the block: two chunks, so the fill
                                    ; rows alternate halves as a bank would

vk_xmrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [vk_err], 0
    mov word [bl_nrow], 0
    mov word [vk_xmdone], 0
    mov si, vk_s_xmtitle
    call bl_sline
    call vk_claim
    jnc .buf
    mov si, vk_s_fail
    call bl_sline
    jmp .save
.buf:
    call OSAPI_CPU_INFO             ; AL = the tier, AH = its bits
    push ax
    xor ah, ah
    xor dx, dx
    mov si, vk_r_xmtier
    mov cx, 9
    call bl_kv
    pop ax
    mov al, ah
    xor ah, ah
    mov si, vk_r_xmbits
    call bl_kv
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can hand out (and
    xor dx, dx                      ; DX:CX its base: the width again)
    mov cx, 9
    mov si, vk_r_xmfree
    call bl_kv
    cmp ax, VK_XMKB
    jae .alloc
    mov si, vk_s_xmnone
    call bl_sline
    jmp .save
.alloc:
    mov dx, VK_XMKB / 64            ; DX:AX = 64 KB
    xor ax, ax
    call OSAPI_XMEM_ALLOC
    jnc .got
    mov si, vk_s_xmnoalloc
    call bl_sline
    jmp .save
.got:
    mov [vk_xmb], ax
    mov [vk_xmb + 2], dx
    call vk_xmat
    ; --- THE CHECK: chunk 3's pattern up into the block's second half, the
    ; buffer overwritten with chunk 0's, and the half copied back down: it
    ; must be chunk 3's again (vk_fill: high word 1, low words from 32768)
    mov ax, 3
    call vk_fill
    mov word [vk_xmlen], VK_CHUNK
    mov word [vk_xmhalf], VK_CHUNK
    call vk_b_xmup
    xor ax, ax
    call vk_fill
    call vk_b_xmdn
    mov di, vk_s_dok
    cmp word [vk_err], 0
    jne .bad
    push es
    mov es, [vk_buf]
    cmp word [es:0], 0x8000
    jne .badp
    cmp word [es:2], 1
    jne .badp
    cmp word [es:VK_CHUNK - 4], 0xFFFC
    jne .badp
    cmp word [es:VK_CHUNK - 2], 1
    jne .badp
    cmp word [es:VK_CHUNK / 2], 0xC000
    jne .badp
    pop es
    jmp short .say
.badp:
    pop es
.bad:
    mov di, vk_s_xmbad
.say:
    mov si, vk_r_xmchk
    call bl_kvs
    cmp di, vk_s_dok
    je .rows
    jmp .free                       ; wrong bytes: nothing worth timing
    ; --- the copy rows: up and down, at four sizes, into the first half
.rows:
    mov si, vk_s_xmhdr
    call bl_sline
    mov word [vk_xmhalf], 0
    mov word [bl_n], VK_XMN
    mov bx, vk_xmrows
.row:
    mov ax, [bx]
    or ax, ax
    jz .fill
    mov [vk_xmlen], ax
    mov word [bl_body], vk_b_xmup
    mov si, [bx + 2]
    xor al, al                      ; method P: PIT-timed, the copy inside
    call bl_run
    mov word [bl_body], vk_b_xmdn
    mov si, [bx + 4]
    xor al, al
    call bl_run
    add bx, 6
    jmp short .row
.fill:
    call vk_xsweep
    call vk_xconv
    ; --- the bank's fill: READ_SEQ alone, then READ_SEQ and a copy up
    call vk_toc
    jnc .look
    mov si, vk_s_noc
    call bl_sline
    jmp short .free
.look:
    call vk_find
    jnc .stream
    mov si, vk_s_nostr
    call bl_sline
    jmp short .home
.stream:
    mov si, vk_s_using
    mov di, [vk_fname]
    call bl_kvs
    mov si, vk_s_xmhdrf
    call bl_sline
    mov word [vk_ceilmb], 0
    mov byte [vk_xmup], 0
    mov bx, 21                      ; (19 and 20 are vk_mh's)
    call vk_xmfill
    mov si, vk_r_xmseq
    call vk_kv
    mov byte [vk_xmup], 1
    mov bx, 22
    call vk_xmfill
    mov si, vk_r_xmbank
    call vk_kv
    call vk_xrdpl
.home:
    call vk_back
.free:
    mov ax, [vk_xmb]
    mov dx, [vk_xmb + 2]
    call OSAPI_XMEM_FREE
.save:
    mov si, vk_r_err
    mov ax, [vk_err]
    xor dx, dx
    mov cx, 9
    call bl_kv
    call bl_operator
    mov si, vk_f_xtxt               ; the report, beside the bench
    call bl_save
    inc word [vk_xmdone]            ; for a harness: X has finished
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_xrdpl - THE DISK'S END: READ_SEQ alone (vk_xmfill, 5 s a row) into
; the buffer at offsets 0, 1, 2 and 3 KB, then 0 again for the drift. The
; READ_SEQ row moved 634 -> 877 KB/s between two boots that also moved the
; wait states AND the buffer; this holds the boot still and moves the buffer
vk_xrdpl:
    push ax
    push bx
    push si
    push di
    mov si, vk_s_xrphdr
    call bl_sline
    mov word [vk_ceilmb], 0
    mov byte [vk_xmup], 0
    mov bx, 23
    mov si, vk_r_xrp
    mov di, vk_xrpoff
.l:
    mov ax, [di]
    mov [vk_rdoff], ax
    push si
    push di
    call vk_xmfill                  ; (which spends SI and DI)
    pop di
    pop si
    call vk_kv
    inc bx
    add si, VK_XSWL
    inc di
    inc di
    cmp di, vk_xrpoff + 10
    jb .l
    mov word [vk_rdoff], 0
    pop di
    pop si
    pop bx
    pop ax
    ret

; vk_xmat - WHERE THIS BOOT PUT THINGS: the buffer's and the block's
; linear addresses, in KB. The same copy has read 0.214, 0.257 and 0.337 ms
; a KB on the Victory from one boot to the next, at either bus setting, with
; the in-place READ unchanged - so what moves is a copy's two ends, and a
; report that does not say where they were cannot be compared with another.
; Preserves every register
vk_xmat:
    push ax
    push cx
    push dx
    push si
    mov ax, [vk_buf]                ; segment -> KB: seg / 64
    mov cl, 6
    shr ax, cl
    xor dx, dx
    mov cx, 9
    mov si, vk_r_xwbuf
    call bl_kv
    mov ax, [vk_xmb]                ; linear -> KB: DX:AX >> 10
    mov dx, [vk_xmb + 2]
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    xor dx, dx
    mov cx, 9
    mov si, vk_r_xwxms
    call bl_kv
    mov ax, [vk_buf]                ; ...and EXACTLY, in bytes: a KB hides
    call vk_seglin                  ; the low bits a bank interleave reads
    mov cx, 9
    mov si, vk_r_xwbufb
    call bl_kv
    mov ax, [vk_xmb]
    mov dx, [vk_xmb + 2]
    mov si, vk_r_xwxmsb
    call bl_kv
    pop si
    pop dx
    pop cx
    pop ax
    ret

; vk_seglin - AX = a segment: DX:AX = its linear address
vk_seglin:
    push cx
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    pop cx
    ret

; vk_xsweep - the same 8 KB copy DOWN from sixteen places 64 KB apart in a
; 1 MB block, into the same buffer: if a boot's speed is the ADDRESS (DRAM
; banks, a page-mode hit or miss between the copy's two ends) the rows
; differ; if it is the machine's state, they agree with each other and not
; with another boot's. Then 8 KB RAM -> RAM inside the buffer, the control
; with no extended memory in it. Skipped, saying so, without 1 MB free
vk_xsweep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, vk_s_xswhdr
    call bl_sline
    mov dx, VK_XSWKB / 64           ; DX:AX = 1 MB
    xor ax, ax
    call OSAPI_XMEM_ALLOC
    jnc .got
    mov si, vk_s_xswno
    call bl_sline
    jmp short .ram
.got:
    push word [vk_xmb]              ; the 64 KB block, back after
    push word [vk_xmb + 2]
    mov [vk_xsw], ax
    mov [vk_xsw + 2], dx
    mov [vk_xmb], ax
    mov [vk_xmb + 2], dx
    mov word [vk_xmlen], 8192
    mov word [vk_xmhalf], 0
    mov word [bl_n], VK_XMN
    mov word [bl_body], vk_b_xmdn
    mov si, vk_r_xsw
.row:
    xor al, al                      ; method P
    call bl_run
    inc word [vk_xmb + 2]           ; the next 64 KB
    add si, VK_XSWL
    cmp si, vk_r_xsw + 16 * VK_XSWL
    jb .row
    pop word [vk_xmb + 2]
    pop word [vk_xmb]
    mov ax, [vk_xsw]
    mov dx, [vk_xsw + 2]
    call OSAPI_XMEM_FREE
.ram:
    mov word [bl_body], vk_b_xram
    mov si, vk_r_xram
    xor al, al
    call bl_run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; 8 KB RAM -> RAM inside the buffer, rep movsw: the control
vk_b_xram:
    push cx
    push si
    push di
    push ds
    push es
    mov es, [vk_buf]                ; ES first: [vk_buf] is ours, and DS
    push es                         ; stops being ours on the next line
    pop ds
    xor si, si
    mov di, 16384
    mov cx, 4096
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    ret

; vk_xconv - the same 8 KB copy DOWN from the XMS block into CONVENTIONAL
; memory at every place the heap will give: up to VK_XCN 64 KB claims, timed
; at each one's start and middle, each row after a line saying where in KB.
; vk_xsweep's sixteen rows read flat on the Victory, so a boot's speed is not
; where the XMS end is; the other end is what this asks. Claims as much as
; the heap holds and gives it all back before the fill rows
vk_xconv:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, vk_s_xchdr
    call bl_sline
    xor bx, bx
.claim:
    cmp bx, VK_XCN * 2
    jae .run
    mov ax, 64
    call OSAPI_MEM_CLAIM            ; DX = the block
    jc .run
    mov [vk_xcsegs + bx], dx
    inc bx
    inc bx
    jmp short .claim
.run:
    mov [vk_xcn], bx
    or bx, bx
    jnz .go
    mov si, vk_s_xcno
    call bl_sline
    jmp short .out
.go:
    xor bx, bx
.blk:
    mov ax, [vk_xcsegs + bx]
    mov [vk_xcs], ax
    mov word [vk_xco], 0
    call .one
    mov word [vk_xco], 32768
    call .one
    inc bx
    inc bx
    cmp bx, [vk_xcn]
    jb .blk
    ; --- the FINE sweep: the same copy into the first block at offsets 0,
    ; 2, 4 ... 16384 bytes. A board that interleaves two banks overlaps a
    ; read with a write in the other bank and makes one in the same bank
    ; wait, so a copy's speed is fixed by a few LOW bits of its two ends -
    ; which a boot's heap layout moves and a 64 KB step never does
    mov si, vk_s_xfhdr
    call bl_sline
    mov ax, [vk_xcsegs]
    mov [vk_xcs], ax
    xor ax, ax
.fine:
    mov [vk_xco], ax
    push ax
    xor dx, dx
    mov cx, 9
    mov si, vk_r_xfoff
    call bl_kv
    mov word [bl_n], VK_XMN
    mov word [bl_body], vk_b_xcdn
    mov si, vk_r_xcdn
    xor al, al
    call bl_run
    pop ax
    or ax, ax
    jnz .fnext
    inc ax                          ; 0 -> 2 (1 after the shift below)
.fnext:
    shl ax, 1
    cmp ax, 16384
    jbe .fine
    call vk_xgrid
    xor bx, bx
.free:
    mov dx, [vk_xcsegs + bx]
    call OSAPI_MEM_FREE
    inc bx
    inc bx
    cmp bx, [vk_xcn]
    jb .free
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.one:                               ; where, in KB, and then the row
    mov ax, [vk_xcs]
    mov cl, 6
    shr ax, cl
    mov dx, [vk_xco]
    mov cl, 10
    shr dx, cl
    add ax, dx
    xor dx, dx
    mov cx, 9
    mov si, vk_r_xcat
    call bl_kv
    mov word [bl_n], VK_XMN
    mov word [bl_body], vk_b_xcdn
    mov si, vk_r_xcdn
    xor al, al                      ; method P
    call bl_run
    ret

; vk_xgrid - WHICH END: the first claim's offset D and the XMS block's S
; stepped a KB at a time, each against each - sixteen rows down, then four
; up. The fine sweep moved only D, and the XMS rows only S in 64 KB steps,
; so neither can say whether a copy's speed is where its CONVENTIONAL end
; lies or how far its two ends are apart (mod 4 KB, on a board that
; interleaves 2 KB pages): if the gap, the rows of one diagonal agree.
; Then the first claim's linear address, exactly
vk_xgrid:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, vk_s_xghdr
    call bl_sline
    mov ax, [vk_xcsegs]
    mov [vk_xcs], ax
    call vk_seglin
    mov cx, 9
    mov si, vk_r_xgat
    call bl_kv
    mov word [bl_n], VK_XMN
    mov si, vk_r_xg
    xor bx, bx                      ; BX = D, CX = S
    mov word [bl_body], vk_b_xcdn
.d:
    xor cx, cx
.s:
    call .one
    add cx, 1024
    cmp cx, 4096
    jb .s
    add bx, 1024
    cmp bx, 4096
    jb .d
    xor bx, bx                      ; ...and UP, S held at the block's start
    xor cx, cx
    mov word [bl_body], vk_b_xcup
.u:
    call .one
    add bx, 1024
    cmp bx, 4096
    jb .u
    mov word [vk_xco], 0
    mov word [vk_xso], 0
    mov ax, [vk_xmb]                ; ...and what os88xgap.inc's xg_gap
    mov dx, [vk_xmb + 2]            ; answers from its own six copies,
    call xg_gap                     ; which the Video Player places by: G,
    jnc .gok                        ; the gap the grid above calls fastest
    inc word [vk_err]
.gok:
    xor dx, dx
    mov cx, 9
    mov si, vk_r_xgg
    call bl_kv
    or ax, ax
    jz .nog
    call vk_xgrows                  ; ...and the transports AT it
.nog:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.one:
    mov [vk_xco], bx
    mov [vk_xso], cx
    push bx
    push cx
    push si
    xor al, al                      ; method P
    call bl_run
    pop si
    pop cx
    pop bx
    add si, VK_XSWL
    ret

; vk_xgrows - AX = G, the gap xg_gap answered: THE TRANSPORTS, placed. The
; same copies by OSAPI_XMEM_COPY (on a 286, XMEM.DRV's LOADALL in 16 KB
; pieces, SPEC.md 41.13) and by int 15h AH=87h called here, at four sizes
; both ways, with the first claim's end G mod 2G from the block's - where
; the Video Player puts its ring (SPEC.md 98.3.18.12) - and at 32 KB 0 mod
; 2G apart too. It is what the encoder's copy cost should be read from: the
; rows above time a buffer wherever this boot's heap left it, which is what
; made the piece size a cross-boot comparison (LOADALL-PLAN 9).
vk_xgrows:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, vk_s_xgrhd
    call bl_sline
    mov bx, ax                      ; G
    shl ax, 1
    dec ax
    mov cx, ax                      ; 2G - 1
    mov ax, [vk_xcsegs]
    mov [vk_xcs], ax
    mov dx, ax
    push cx
    mov cl, 4
    shl dx, cl                      ; C, the claim's linear low word
    pop cx
    mov ax, [vk_xmb]
    sub ax, dx                      ; X - C
    and ax, cx
    mov [vk_xwo], ax                ; 0 apart
    mov ax, [vk_xmb]
    add ax, bx
    sub ax, dx                      ; X + G - C
    and ax, cx
    mov [vk_xbo], ax                ; G apart
    mov word [vk_xso], 0
    call vk_x87chk                  ; the BIOS's ends right before any timing
    mov word [bl_n], VK_XMN
    mov si, vk_xgtab
.row:
    mov ax, [si]
    or ax, ax
    jz .done
    mov [vk_xcl], ax
    mov ax, [vk_xbo]
    cmp word [si + 2], 0
    je .b
    mov ax, [vk_xwo]
.b:
    mov [vk_xco], ax
    mov ax, [si + 4]
    mov word [bl_body], ax          ; (the table's: one each row)
    push si
    mov si, [si + 6]
    xor al, al                      ; method P
    call bl_run
    pop si
    add si, 8
    jmp short .row
.done:
    mov word [vk_xcl], 8192
    mov word [vk_xco], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; [vk_xcl] bytes by int 15h AH=87h between [vk_xcs]:[vk_xco] and the block
; + [vk_xso], up or down - the BIOS's transport, called from here so it can
; be set beside XMEM.DRV's on one boot
vk_b_x87up:
    push di
    xor di, di
    jmp short vk_x87
vk_b_x87dn:
    push di
    mov di, 1
vk_x87:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, [vk_xcs]
    call vk_seglin
    add ax, [vk_xco]
    adc dx, 0
    mov bx, ax
    mov cx, dx                      ; CX:BX = the conventional end
    mov ax, [vk_xmb]
    mov dx, [vk_xmb + 2]
    add ax, [vk_xso]
    adc dx, 0                       ; DX:AX = the extended end
    or di, di
    jz .up
    xchg ax, bx                     ; down: the extended end is the source
    xchg dx, cx
.up:                                ; CX:BX = the source, DX:AX = the target
    mov si, vk_gdt + 0x18
    call vk_desc
    mov ax, bx
    mov dx, cx
    mov si, vk_gdt + 0x10
    call vk_desc
    push ds
    pop es
    mov si, vk_gdt
    mov cx, [vk_xcl]
    shr cx, 1                       ; (words)
    mov ah, 0x87
    int 0x15
    jnc .ok
    inc word [vk_err]
.ok:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop di
    ret

; vk_x87chk - AH=87h's ends checked: 4 KB of a pattern at [vk_xcs]:[vk_xbo]
; up, the buffer zeroed, back down; the report says ok or the first word
; wrong (a descriptor built wrong writes somewhere else in conventional
; memory, and would time the same). Preserves all
vk_x87chk:
    push ax
    push cx
    push di
    push es
    mov word [vk_xcl], 4096
    mov ax, [vk_xbo]
    mov [vk_xco], ax
    mov es, [vk_xcs]
    mov di, ax
    mov cx, 2048
    mov ax, 0x5A00
    cld
.f:
    stosw
    inc ax
    loop .f
    call vk_b_x87up
    mov di, [vk_xco]
    mov cx, 2048
    xor ax, ax
    rep stosw
    call vk_b_x87dn
    mov di, [vk_xco]
    mov cx, 2048
    mov ax, 0x5A00
.c:
    scasw
    jne .bad
    inc ax
    loop .c
    mov di, vk_s_dok
    jmp short .say
.bad:
    inc word [vk_err]
    mov di, vk_s_xmbad
.say:
    push si
    mov si, vk_r_x87ck
    call bl_kvs
    pop si
    pop es
    pop di
    pop cx
    pop ax
    ret

; vk_desc - SI = an AH=87h descriptor, DX:AX = its base: [vk_xcl] long,
; data, writable. Preserves all
vk_desc:
    push cx
    mov cx, [vk_xcl]
    dec cx
    mov [si], cx
    mov [si + 2], ax
    mov [si + 4], dl
    mov byte [si + 5], 0x93
    mov word [si + 6], 0
    pop cx
    ret

; 8 KB up into the XMS block (+[vk_xso]) from [vk_xcs]:[vk_xco]
vk_b_xcup:
    push di
    xor di, di
    jmp short vk_xccopy
; 8 KB down from the XMS block (+[vk_xso]) into [vk_xcs]:[vk_xco]
vk_b_xcdn:
    push di
    mov di, 1
vk_xccopy:
    push es
    mov es, [vk_xcs]
    mov si, [vk_xco]
    mov ax, [vk_xmb]
    mov dx, [vk_xmb + 2]
    add ax, [vk_xso]
    adc dx, 0
    mov cx, [vk_xcl]
    call OSAPI_XMEM_COPY
    jnc .ok
    inc word [vk_err]
.ok:
    pop es
    pop di
    ret

; the copy bodies: [vk_xmlen] bytes between the buffer and the block at
; [vk_xmhalf], up (DI = 0) or down (DI = 1)
vk_b_xmup:
    xor di, di
    jmp short vk_xmcopy
vk_b_xmdn:
    mov di, 1
vk_xmcopy:
    push es
    mov es, [vk_buf]
    xor si, si
    mov ax, [vk_xmb]
    mov dx, [vk_xmb + 2]
    add ax, [vk_xmhalf]
    adc dx, 0
    mov cx, [vk_xmlen]
    call OSAPI_XMEM_COPY
    jnc .ok
    inc word [vk_err]
.ok:
    pop es
    ret

; vk_xmfill - BX = the result row: 5 s of 32 KB READ_SEQ calls from a fresh
; 2 MB, each copied up into the block's next half when [vk_xmup] - the bank
; filling behind the ring. KB/s x 10 into the row, as vk_ceilmain's
vk_xmfill:
    push ax
    push bx
    push cx
    push dx
    mov [vk_ceilrow], bx
    add word [vk_ceilmb], 1
    mov dx, [vk_ceilmb]
    mov cl, 5
    shl dx, cl                      ; row n starts at 2n MB
    call vk_seek
    mov word [vk_cap], VK_CHUNK
    call vk_b_seq                   ; the seek's walk, outside the timing
    xor ax, ax
    mov [vk_cbytes], ax
    mov [vk_cbytes + 2], ax
    mov word [vk_xmlen], VK_CHUNK
    mov word [vk_xmhalf], 0
    call OSAPI_GET_TICKS
    mov [vk_ct0], ax
.l:
    call vk_b_seq
    add [vk_cbytes], ax
    adc word [vk_cbytes + 2], 0
    or ax, ax
    jz .done                        ; the end of the stream
    cmp byte [vk_xmup], 0
    je .t
    call vk_b_xmup
    xor word [vk_xmhalf], VK_CHUNK  ; the other half next
.t:
    call OSAPI_GET_TICKS
    sub ax, [vk_ct0]
    cmp ax, VK_CEILT
    jb .l
.done:
    call OSAPI_GET_TICKS
    sub ax, [vk_ct0]
    mov [vk_cticks], ax
    pop dx
    pop cx
    pop bx
    pop ax
    jmp vk_ceilmain.kbs             ; ...and the arithmetic is the ceiling's


; =============================================================================
; vk_emrun - E: a Lo-tech-style EMS board (see the header)
; =============================================================================
VK_EPAGES   equ 128                 ; 2 MB, the most its registers name
VK_EMN      equ 8                   ; iterations of a timed row
VK_ECOPY    equ 16384               ; a copy row's bytes: one page, and under
                                    ; a PIT lap (55 ms) on a 4.77 MHz 8088

vk_emrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [vk_err], 0
    mov word [bl_nrow], 0
    mov word [vk_emdone], 0
    mov si, vk_s_emtitle
    call bl_sline
    call vk_claim
    jnc .buf
    mov si, vk_s_fail
    call bl_sline
    jmp .save
.buf:
    call vk_eprobe                  ; CF=1: no board, DI = why
    jnc .found
    mov si, vk_r_emprobe
    call bl_kvs
    jmp .save
.found:
    mov di, vk_s_emwhere            ; '0288h, frame D000h'
    mov ax, [vk_eport]
    call vk_hex4
    add di, 13
    mov ax, [vk_efrm]
    call vk_hex4
    mov si, vk_s_emat
    mov di, vk_s_emwhere
    call bl_kvs
    mov ax, [vk_epages]
    xor dx, dx
    mov cx, 9
    mov si, vk_r_empages
    call bl_kv
    mov si, vk_s_emhdr
    call bl_sline
    mov word [bl_n], VK_EMN
    mov word [bl_body], vk_b_emap
    mov si, vk_r_emmap
    xor al, al
    call bl_run
    xor ax, ax
    call vk_emap4                   ; pages 0..3: the frame is 0 to 64 KB
    mov word [bl_body], vk_b_edn
    mov si, vk_r_emdn
    xor al, al
    call bl_run
    mov word [bl_body], vk_b_eup
    mov si, vk_r_emup
    xor al, al
    call bl_run
    mov word [bl_body], vk_b_eram
    mov si, vk_r_emram
    xor al, al
    call bl_run
    ; --- the disk straight into the frame
    call vk_toc
    jnc .look
    mov si, vk_s_noc
    call bl_sline
    jmp short .save
.look:
    call vk_find
    jnc .stream
    mov si, vk_s_nostr
    call bl_sline
    jmp short .home
.stream:
    mov si, vk_s_using
    mov di, [vk_fname]
    call bl_kvs
    mov si, vk_s_emhdrf
    call bl_sline
    mov word [vk_ceilmb], 0
    mov byte [vk_eframe], 0
    mov bx, 21
    call vk_emfill
    mov si, vk_r_emseq
    call vk_kv
    mov byte [vk_eframe], 1
    mov bx, 22
    call vk_emfill
    mov si, vk_r_emfr
    call vk_kv
    mov si, vk_r_emfchk             ; the last chunk, through the frame
    mov di, vk_s_dok
    cmp byte [vk_efbad], 0
    je .fk
    mov di, vk_s_embad
.fk:
    call bl_kvs
.home:
    call vk_back
.save:
    mov si, vk_r_err
    mov ax, [vk_err]
    xor dx, dx
    mov cx, 9
    call bl_kv
    call bl_operator
    mov si, vk_f_etxt
    call bl_save
    inc word [vk_emdone]            ; for a harness: E has finished
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_hex4 - AX as four hex digits at DI (DS). Every register kept
vk_hex4:
    push ax
    push bx
    push cx
    push di
    mov bx, ax
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol bx, cl
    pop cx
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 'A' - '9' - 1
.p:
    mov [di], al
    inc di
    loop .d
    pop di
    pop cx
    pop bx
    pop ax
    ret

; vk_eout - AL = a page, DX = its register (the board's registers cannot be
; read back, so nothing here asks)
vk_eout:
    out dx, al
    ret

; vk_emap4 - AX = a page: AX..AX+3 into the frame's four quarters
vk_emap4:
    push ax
    push cx
    push dx
    mov dx, [vk_eport]
    mov cx, 4
.l:
    out dx, al
    inc ax
    inc dx
    loop .l
    pop dx
    pop cx
    pop ax
    ret

; vk_etry - DX = a base, ES = a frame: CF=0 a paging board answers there
; (EMS.DRV's em_try): pages 0 and 1 hold two bytes, page 0 into quarter 1
; reads quarter 0's, and a write through one is seen through the other
vk_etry:
    push ax
    push cx
    mov cl, [es:0]
    mov ch, [es:0x4000]
    xor ax, ax                      ; page 0 -> quarter 0, page 1 -> 1
    call vk_eout
    inc dx
    inc ax
    call vk_eout
    dec dx
    mov byte [es:0], 0x5A
    mov byte [es:0x4000], 0xA5
    cmp byte [es:0], 0x5A
    jne .no
    cmp byte [es:0x4000], 0xA5
    jne .no
    inc dx
    dec ax
    call vk_eout                    ; page 0 into quarter 1 as well
    dec dx
    cmp byte [es:0x4000], 0x5A
    jne .no
    mov byte [es:0x4000], 0x3C
    cmp byte [es:0], 0x3C
    jne .no
    pop cx
    pop ax
    clc
    ret
.no:
    mov [es:0], cl
    mov [es:0x4000], ch
    pop cx
    pop ax
    stc
    ret

; vk_eprobe - CF=0 [vk_epages] = the board's 16 KB pages, [vk_eport] and
; [vk_efrm] where it answered; CF=1 DI = why not. EMS.DRV's own search
; (SPEC.md 107.2): frames E000h, D000h, C000h, each at bases 260h, 264h,
; 268h, 26Ch and 288h - the Lo-tech's defaults, and a PicoMEM's (D000h at
; 288h is its own default). A frame holding an option ROM is skipped and
; no port is written for it; the two bytes a base's test writes are put
; back when nothing pages, since then they may be somebody's RAM
vk_eprobe:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov di, vk_s_emnone
    mov si, vk_efrms
.frame:
    mov ax, [si]
    add si, 2
    or ax, ax
    jz .none
    mov es, ax
    xor bx, bx
.rom:
    cmp word [es:bx], 0xAA55
    je .romf
    add bx, 2048
    jnz .rom
    mov bx, vk_ebases
.base:
    mov dx, [bx]
    add bx, 2
    or dx, dx
    jz .frame
    call vk_etry
    jc .base
    mov [vk_eport], dx
    mov [vk_efrm], es
    jmp short .size
.romf:
    mov di, vk_s_emused             ; (a frame was a ROM's: said if no
    jmp short .frame                ; other one answers)
.none:
    jmp .no
    ; --- size it: a signature per page from the top DOWN, then read up
.size:
    mov cx, VK_EPAGES
.wr:
    mov ax, cx
    dec ax
    call vk_eout
    mov [es:0], ax
    not ax
    mov [es:2], ax
    loop .wr
    xor cx, cx
.rd:
    mov ax, cx
    call vk_eout
    cmp [es:0], ax
    jne .sized
    not ax
    cmp [es:2], ax
    jne .sized
    inc cx
    cmp cx, VK_EPAGES
    jb .rd
.sized:
    mov [vk_epages], cx
    mov di, vk_s_emnone
    cmp cx, 4                       ; fewer than a frame's four is no board
    jb .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the timed bodies
vk_b_emap:                          ; four OUTs: the whole frame mapped
    push ax
    mov ax, 4
    call vk_emap4
    pop ax
    ret
vk_b_edn:                           ; frame -> RAM
    push ds
    push es
    mov es, [vk_buf]
    mov ax, [vk_efrm]
    mov ds, ax
    jmp short vk_b_ecp
vk_b_eup:                           ; RAM -> frame
    push ds
    push es
    mov ax, [vk_efrm]
    mov es, ax
    mov ds, [vk_buf]
    jmp short vk_b_ecp
vk_b_eram:                          ; RAM -> RAM, the same buffer
    push ds
    push es
    mov es, [vk_buf]
    mov ds, [vk_buf]
vk_b_ecp:
    xor si, si
    xor di, di
    mov cx, VK_ECOPY / 2
    cld
    rep movsw
    pop es
    pop ds
    ret

; vk_emfill - BX = the result row: 5 s of 32 KB READ_SEQ calls from a fresh
; 2 MB, into the buffer or, with [vk_eframe], STRAIGHT INTO THE FRAME - each
; chunk into the next two pages, mapped into the frame's half it lands in.
; KB/s x 10 into the row, vk_ceilmain's arithmetic; the last chunk read into
; the frame is checked against STREAM.DAT's pattern ([vk_efbad])
vk_emfill:
    push ax
    push bx
    push cx
    push dx
    mov [vk_ceilrow], bx
    add word [vk_ceilmb], 1
    mov dx, [vk_ceilmb]
    mov cl, 5
    shl dx, cl                      ; row n starts at 2n MB
    mov [vk_efmb], dx
    call vk_seek
    mov word [vk_cap], VK_CHUNK
    call vk_b_seq                   ; the seek's walk, outside the timing
    xor ax, ax
    mov [vk_cbytes], ax
    mov [vk_cbytes + 2], ax
    mov [vk_efk], ax                ; chunks read into the frame
    mov byte [vk_efbad], 0
    call OSAPI_GET_TICKS
    mov [vk_ct0], ax
.l:
    cmp byte [vk_eframe], 0
    je .ram
    call vk_b_efr
    jmp short .n
.ram:
    call vk_b_seq
.n:
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
    cmp byte [vk_eframe], 0
    je .kbs
    call vk_efchk
.kbs:
    pop dx
    pop cx
    pop bx
    pop ax
    jmp vk_ceilmain.kbs

; vk_b_efr - the next 32 KB READ_SEQ into the frame: chunk k into pages
; 2k, 2k+1 (mod the board), mapped into the frame's half k & 1
vk_b_efr:
    push bx
    push dx
    push es
    mov ax, [vk_efk]
    shl ax, 1
    xor dx, dx
    div word [vk_epages]
    mov ax, dx                      ; AX = the chunk's first page
    mov dx, [vk_eport]
    test byte [vk_efk], 1
    jz .h
    add dx, 2                       ; the frame's upper half
.h:
    call vk_eout
    inc ax
    inc dx
    call vk_eout
    mov bx, [vk_efk]
    and bx, 1
    mov dl, 15                      ; BX = 0 or 32768
    xchg cx, dx
    shl bx, cl
    xchg cx, dx
    push ds
    pop es
    mov dx, [vk_efrm]               ; DX:BX = the frame
    mov cx, VK_CHUNK
    push di
    push si
    mov di, vk_cur
    mov si, [vk_fname]
    call OSAPI_FILE_READ_SEQ
    pop si
    pop di
    jnc .ok
    inc word [vk_err]
    xor ax, ax
.ok:
    mov [vk_got], ax
    inc word [vk_efk]
    pop es
    pop dx
    pop bx
    ret

; vk_efchk - the last chunk read into the frame is still mapped (its two
; pages, in its half): is it STREAM.DAT's chunk from where the row started?
vk_efchk:
    cmp word [vk_fname], vk_f_names ; only STREAM.DAT has a pattern
    jne .out
    mov ax, [vk_efk]
    or ax, ax
    jz .out
    dec ax                          ; AX = that chunk's place in the frame
    push es
    mov bx, ax
    and bx, 1
    mov cl, 15
    shl bx, cl                      ; BX = its half
    inc ax                          ; ...and its place in the row: the seek's
    mov dx, ax                      ; untimed first read took the row's
    shr dx, 1                       ; first chunk, so it is one on. Its
    add dx, [vk_efmb]               ; offsets' high word is the row's 2n MB
    and ax, 1                       ; plus half that, and AX the first low
    shl ax, cl                      ; word
    mov cx, [vk_efrm]
    mov es, cx
    cmp [es:bx], ax
    jne .bad
    cmp [es:bx + 2], dx
    jne .bad
    add ax, VK_CHUNK - 4
    cmp [es:bx + VK_CHUNK - 4], ax
    jne .bad
    cmp [es:bx + VK_CHUNK - 2], dx
    je .good
.bad:
    mov byte [vk_efbad], 1
.good:
    pop es
.out:
    ret

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

%define BL_ARENA_BYTES 9000          ; (X's report is ~7,000 with its
                                    ; source grid and disk rows; 5,000 cut it)
; --- one int 13h across a head (see the header) ---------------------------------
VK_MHMAX    equ VK_BUFKB * 2        ; sectors the buffer holds: 80
VK_MHPAT    equ 0xE5A6              ; what a sector the ROM did not read reads
VK_MHCYL    equ 2                   ; the head crossing is on this cylinder,
                                    ; the cylinder crossing at its end, and
VK_MHTCYL   equ 6                   ; the timing rows from this one on
VK_MHN      equ 12                  ; ...one fresh cylinder an iteration

; vk_xrd - int 13h AH=2 on unit 80h: AL sectors from [vk_xc] cylinder, DH
; head, CL sector (1-based, bits 0-5) into [vk_buf]:BX. CF and AH are the
; ROM's. Clobbers CX
vk_xrd:
    push es
    push dx
    mov es, [vk_buf]
    mov ch, [vk_xc]
    mov dl, [vk_xc + 1]
    ror dl, 1                       ; cylinder bits 8-9 into CL bits 6-7
    ror dl, 1
    and dl, 0xC0
    or cl, dl
    mov ah, 2
    mov dl, 0x80
    int 0x13
    pop dx
    pop es
    ret

; vk_xsplit - the run [vk_xc]:[vk_xh]:[vk_xs], [vk_xn] sectors, read a track
; a call into [vk_buf]:0 - the truth a crossing read is held to. CF=1 a call
; failed. Every register kept
vk_xsplit:
    push ax
    push bx
    push cx
    push dx
    push si
    push word [vk_xc]
    mov bx, [vk_xbx]
    mov dh, [vk_xh]
    mov cl, [vk_xs]
    mov si, [vk_xn]
.next:
    mov al, [vk_spt]
    sub al, cl
    inc al                          ; AL = the sectors left on this track
    xor ah, ah
    cmp ax, si
    jbe .n
    mov ax, si
.n:
    push ax
    push cx
    call vk_xrd
    pop cx
    pop ax
    jc .out
    sub si, ax
    jz .out                         ; (CF clear: sub gave a result >= 0)
    mov ah, al                      ; BX += AL x 512
    xor al, al
    shl ax, 1
    add bx, ax
    mov cl, 1                       ; the next track from its first sector
    inc dh
    cmp dh, [vk_heads]
    jb .next
    xor dh, dh
    inc word [vk_xc]
    jmp short .next
.out:
    pop word [vk_xc]
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_xone - the same run in ONE call. CF=1 the ROM refused, AH its status
vk_xone:
    push bx
    push cx
    push dx
    mov bx, [vk_xbx]
    mov al, [vk_xn]
    mov dh, [vk_xh]
    mov cl, [vk_xs]
    call vk_xrd
    pop dx
    pop cx
    pop bx
    ret

; vk_xsum - AX = the run's checksum, vb_sum's (s = rol(s + word)), over
; [vk_xn] sectors at [vk_buf]:0
vk_xsum:
    push cx
    push si
    push ds
    mov cx, [vk_xn]
    mov ah, cl                      ; words = sectors x 256
    xor al, al
    mov cx, ax
    mov si, [vk_xbx]
    mov ds, [vk_buf]
    xor ax, ax
.w:
    add ax, [si]
    rol ax, 1
    inc si
    inc si
    loop .w
    pop ds
    pop si
    pop cx
    ret

; vk_xfill - the buffer's first [vk_xn] sectors, VK_MHPAT
vk_xfill:
    push ax
    push cx
    push di
    push es
    mov di, [vk_xbx]
    mov es, [vk_buf]
    mov cx, [vk_xn]
    mov ah, cl
    xor al, al
    mov cx, ax
    mov ax, VK_MHPAT
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; vk_xcase - SI = the label: read the run both ways and say which. CF=0 the
; one call read the truth
vk_xcase:
    push ax
    push bx
    push di
    call vk_xsplit
    mov di, vk_s_xsfail
    jc .say                         ; no truth to hold it to
    call vk_xsum
    mov bx, ax
    call vk_xfill
    call vk_xone
    mov di, vk_s_xref
    jc .err
    call vk_xsum
    mov di, vk_s_xok
    cmp ax, bx
    je .good
    mov di, vk_s_xbad
.say:
    call bl_kvs
    stc
    jmp short .out
.err:
    call bl_kvs                     ; refused, and the status on its own line
    mov al, ah
    xor ah, ah
    xor dx, dx
    push cx
    mov cx, 9
    mov si, vk_r_xst
    call bl_kv
    pop cx
    stc
    jmp short .out
.good:
    call bl_kvs
    clc
.out:
    pop di
    pop bx
    pop ax
    ret

; the timing rows' bodies: [vk_tn] sectors from head 0 sector 1 of cylinder
; [vk_xc], which moves on one an iteration so no track is read twice
vk_b_tone:
    mov ax, [vk_tn]
    mov [vk_xn], ax
    mov byte [vk_xh], 0
    mov byte [vk_xs], 1
    call vk_xone
    jnc .ok
    inc word [vk_err]
.ok:
    inc word [vk_xc]
    ret
vk_b_tsplit:
    mov ax, [vk_tn]
    mov [vk_xn], ax
    mov byte [vk_xh], 0
    mov byte [vk_xs], 1
    call vk_xsplit
    jnc .ok
    inc word [vk_err]
.ok:
    inc word [vk_xc]
    ret

; vk_msweep - THE KERNEL'S OWN SHAPES (SPEC.md 18.91.5): a run from a
; MID-TRACK sector to the cylinder's end, as dsk_xfer issues one under
; the cylinder bound (SPEC.md 18.91.5), into the buffer at 0, 1 and 2 sectors in - VK_SWN of them, the
; start walking the heads and the sectors. The two rows above start every
; run at sector 1 on a 17-sector disk, so a ROM that carries a whole track
; onto the next head and gets a run starting mid-track wrong would pass
; them. (It was written for the owner's ST11M, whose knob install failed to
; boot - which turned out to be the install pairing a stock boot sector with
; the knob kernel, SPEC.md 18.91.5, and the sweep then read 48 of 48 right.)
; Every shape is held to a track a call, as above; the first that is not
; the same bytes is named
VK_SWN     equ 48
VK_SWCYL   equ 10
vk_msweep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor ax, ax
    mov [vk_swn], ax
    mov [vk_swbad], ax
    mov [vk_swref], ax
    mov word [vk_swf], 0xFFFF
    xor si, si                      ; SI = the shape
.l:
    mov ax, si                      ; the buffer at (i mod 3) sectors in
    xor dx, dx
    mov cx, 3
    div cx
    mov di, dx                      ; DI = those sectors
    mov ah, dl
    xor al, al
    shl ax, 1
    mov [vk_xbx], ax
    mov ax, si
    add ax, VK_SWCYL
    mov [vk_xc], ax                 ; a fresh cylinder each
    mov ax, si
    xor dx, dx
    mov cl, [vk_heads]
    xor ch, ch
    div cx
    mov [vk_xh], dl                 ; the head, i mod heads
    mov bl, dl
    mov ax, si
    mov cx, 7
    mul cx
    xor dx, dx
    mov cl, [vk_spt]
    xor ch, ch
    div cx
    inc dx
    mov [vk_xs], dl                 ; the sector, 1 + 7i mod spt
    mov al, [vk_heads]              ; to the cylinder's end, as the kernel
    sub al, bl                      ; reads: (heads - head) x spt - (s - 1)
    mul byte [vk_spt]
    dec dx
    sub ax, dx
    mov cx, VK_MHMAX                ; ...capped by the buffer
    sub cx, di
    cmp ax, cx
    jbe .n
    mov ax, cx
.n:
    mov [vk_xn], ax
    call vk_xsplit
    jc .next                        ; no truth to hold it to
    inc word [vk_swn]
    call vk_xsum
    mov bx, ax
    call vk_xfill
    call vk_xone
    jc .ref
    call vk_xsum
    cmp ax, bx
    je .next
    inc word [vk_swbad]
    cmp word [vk_swf], 0xFFFF
    jne .next
    mov ax, [vk_xc]                 ; the first wrong one, named
    mov [vk_swf], ax
    mov al, [vk_xh]
    xor ah, ah
    mov [vk_swf + 2], ax
    mov al, [vk_xs]
    mov [vk_swf + 4], ax
    mov ax, [vk_xn]
    mov [vk_swf + 6], ax
    jmp short .next
.ref:
    inc word [vk_swref]
.next:
    inc si
    cmp si, VK_SWN
    jb .l
    mov word [vk_xbx], 0
    xor dx, dx
    mov cx, 9
    mov si, vk_r_swn
    mov ax, [vk_swn]
    call bl_kv
    mov si, vk_r_swbad
    mov ax, [vk_swbad]
    call bl_kv
    mov si, vk_r_swref
    mov ax, [vk_swref]
    call bl_kv
    cmp word [vk_swf], 0xFFFF
    je .out
    mov si, vk_r_swfc
    mov ax, [vk_swf]
    call bl_kv
    mov si, vk_r_swfh
    mov ax, [vk_swf + 2]
    call bl_kv
    mov si, vk_r_swfs
    mov ax, [vk_swf + 4]
    call bl_kv
    mov si, vk_r_swfn
    mov ax, [vk_swf + 6]
    call bl_kv
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_mh - the crossing rows and the timing rows (see the header). Reads only
vk_mh:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, vk_s_hdrx
    call bl_sline
    cmp byte [vk_heads], 2
    jb .out                         ; one head: nothing to cross
    ; K = the sectors each side of the seam: a whole track, or what half the
    ; buffer holds
    mov al, [vk_spt]
    cmp al, VK_MHMAX / 2
    jbe .k
    mov al, VK_MHMAX / 2
.k:
    mov cl, al                      ; CL = K
    mov ah, [vk_spt]
    sub ah, al
    inc ah
    mov [vk_xs], ah                 ; from sector spt - K + 1...
    xor ch, ch
    shl cx, 1
    mov [vk_xn], cx                 ; ...2K sectors
    mov word [vk_xc], VK_MHCYL
    mov byte [vk_xh], 0             ; head 0 onto head 1
    mov si, vk_r_xhead
    call vk_xcase
    pushf
    mov al, [vk_heads]
    dec al
    mov [vk_xh], al                 ; the last head onto the next cylinder
    mov si, vk_r_xcyl
    call vk_xcase
    call vk_msweep                  ; ...and the kernel's own shapes
    popf
    mov byte [vk_xgood], 0          ; 1: time the one call too
    jc .time
    inc byte [vk_xgood]
    ; --- the time: N = a cylinder, or the buffer, whichever is less
.time:
    mov al, [vk_spt]
    mul byte [vk_heads]
    cmp ax, VK_MHMAX
    jbe .n
    mov ax, VK_MHMAX
.n:
    mov [vk_tn], ax
    mov si, vk_r_tn
    xor dx, dx
    mov cx, 9
    call bl_kv
    mov word [bl_n], VK_MHN
    mov word [vk_xc], VK_MHTCYL
    mov word [bl_body], vk_b_tsplit
    mov si, vk_r_tsplit
    mov bx, 19
    call vk_row
    cmp byte [vk_xgood], 0
    je .skip
    mov word [vk_xc], VK_MHTCYL     ; the same cylinders again
    mov word [bl_body], vk_b_tone
    mov si, vk_r_tone
    mov bx, 20
    call vk_row
    jmp short .out
.skip:
    mov si, vk_r_tone
    mov di, vk_s_xskip
    call bl_kvs
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; vk_cprun - C: WHAT A READ COSTS THE CPU, on a 286 with an IDE disk
; (docs/plans/DISK-CPU-PLAN.md 7). Three questions, each to a number:
;
;   THE TRANSFER: what moving one sector costs the CPU on this bus, taken by
;     reading sectors through the task file ourselves, PIT-timed with
;     interrupts off around each sector: the drive's wait for DRQ, and the
;     256 words with `rep insw` (what HDD.DRV's rung 1 does, SPEC.md 52.1.2)
;     and again with `in ax, dx` / `stosw` / `loop` (what it did before) -
;     which is the bus's own floor and the old loop's real price, measured.
;     The first sector of each command is its own row: the command's latency.
;   THE STREAM, TWO WAYS: 4 MB of 32 KB OSAPI_FILE_READ_SEQ calls on the
;     stream, then 4 MB of 64-sector commands with `rep insw`, interrupts on
;     and nothing timed inside them. Same bytes, same drive: what the first
;     costs over the second is the kernel's and HDD.DRV's own CPU. The direct
;     row runs LAST so that the buffer ends holding the 64 sectors before
;     [vk_cplba] - tests/viddiskcpu.py checks them against the image.
;
; READ-ONLY, like R: READ SECTORS (20h) to the primary channel's MASTER at
; 1F0h, from cylinder VK_CPCYL up - the fixed disk's own geometry off int 13h
; AH=08h, which is the drive's when HDD.DRV took it (52.1.1 pairs them on it).
; A machine below CPU_286 (no `rep insw`) or with no drive answering at 1F0h
; says so and runs only the READ_SEQ row. Saves VDCPU.TXT beside the bench.
; =============================================================================
VK_IDE      equ 0x1F0
VK_CPSEC    equ 64                  ; sectors a command: one 32 KB chunk
VK_CPCMD    equ 8                   ; commands a timed transfer row
VK_CPSTRM   equ 128                 ; 32 KB units a stream row: 4 MB
VK_CPCYL    equ 20                  ; where the direct reads start

vk_cprun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [vk_err], 0
    mov word [bl_nrow], 0
    mov word [vk_cpdone], 0
    mov byte [vk_cpide], 0
    mov si, vk_s_cptitle
    call bl_sline
    call vk_claim
    jnc .geom
    jmp .fail
.geom:
    mov ah, 8                       ; the fixed disk's geometry, off the ROM
    mov dl, 0x80
    push es
    int 0x13
    pop es
    jnc .gok
    jmp .fail
.gok:
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
    call OSAPI_CPU_INFO
    cmp al, CPU_286
    jae .cpu
    mov si, vk_s_cp86
    call bl_sline
    jmp .stream
.cpu:
    call vk_ide_ready
    jnc .ide
    mov si, vk_s_cpnoide
    call bl_sline
    jmp .stream
.ide:
    mov byte [vk_cpide], 1
    mov al, [vk_heads]              ; the first sector: cylinder VK_CPCYL
    mul byte [vk_spt]
    mov cx, VK_CPCYL
    mul cx
    mov [vk_cplba], ax
    ; --- what reading the PIT costs, which every timed span below carries
    mov si, vk_s_cphdr
    call bl_sline
    xor ax, ax
    mov [vk_cpx], ax
    mov [vk_cpx + 2], ax
    mov cx, 64
.cal:
    pushf
    cli
    call bl_pit
    mov bx, ax
    call bl_pit
    sub bx, ax
    popf
    add [vk_cpx], bx
    adc word [vk_cpx + 2], 0
    loop .cal
    mov si, vk_r_cppit
    mov bx, vk_cpx
    mov cx, 64
    call vk_cprep
    ; --- the transfer, both loops
    mov byte [vk_cpmode], 0
.mode:
    xor ax, ax
    mov [vk_cpx], ax
    mov [vk_cpx + 2], ax
    mov [vk_cpw], ax
    mov [vk_cpw + 2], ax
    mov [vk_cpf], ax
    mov [vk_cpf + 2], ax
    mov word [vk_cpbad], 0
    mov bp, VK_CPCMD
.cmd:
    call vk_cpcmd
    dec bp
    jnz .cmd
    mov bl, [vk_cpmode]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov si, [vk_cprows + bx]
    mov bx, vk_cpx
    mov cx, VK_CPCMD * VK_CPSEC
    call vk_cprep
    mov bl, [vk_cpmode]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov si, [vk_cprows + bx + 2]
    mov bx, vk_cpw
    mov cx, VK_CPCMD * (VK_CPSEC - 1)
    call vk_cprep
    mov si, vk_r_cpfirst
    mov bx, vk_cpf
    mov cx, VK_CPCMD
    call vk_cprep
    mov si, vk_r_cpbad
    mov ax, [vk_cpbad]
    xor dx, dx
    mov cx, 9
    call bl_kv
    inc byte [vk_cpmode]
    cmp byte [vk_cpmode], 2
    jb .mode
    ; --- 4 MB through the kernel, 32 KB READ_SEQ calls; then the same 4 MB
    ; as the bench's own commands, LAST, so the buffer ends holding the
    ; sectors just before [vk_cplba] for a harness to check against the disk
.stream:
    mov si, vk_s_cphdr2
    call bl_sline
    call vk_toc
    jnc .look
    mov si, vk_s_noc
    call bl_sline
    jmp short .direct
.look:
    push word [vk_err]              ; vk_find zeroes it when it finds the
    call vk_find                    ; stream, and the rows above must not
    pop ax                          ; lose theirs to that
    pushf
    add [vk_err], ax
    popf
    jnc .found
    mov si, vk_s_nostr
    call bl_sline
    jmp short .home
.found:
    mov si, vk_s_using
    mov di, [vk_fname]
    call bl_kvs
    mov dx, 0x20                    ; from 2 MB
    call vk_seek
    mov word [vk_cap], VK_CHUNK
    mov word [bl_n], VK_CPSTRM
    mov word [bl_body], vk_b_seq
    mov si, vk_r_cpseq
    mov al, 1                       ; method T: whole ticks, interrupts on
    call bl_run
.home:
    call vk_back
.direct:
    cmp byte [vk_cpide], 0
    je .save
    mov word [bl_n], VK_CPSTRM      ; 4 MB of 64-sector commands, rep insw,
    mov word [bl_body], vk_b_ide    ; interrupts on, nothing timed inside
    mov si, vk_r_cpdir
    mov al, 1
    call bl_run
    jmp short .save
.fail:
    mov si, vk_s_fail
    call bl_sline
.save:
    mov si, vk_r_err
    mov ax, [vk_err]
    xor dx, dx
    mov cx, 9
    call bl_kv
    call bl_operator
    mov si, vk_f_ctxt               ; the report, beside the bench
    call bl_save
    inc word [vk_cpdone]            ; for a harness: C has finished
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_cprep - SI = label, BX -> a dword of PIT counts, CX = what it is over:
; "label  us x 100 each" into the report
vk_cprep:
    push ax
    push bx
    push cx
    push dx
    mov ax, [bx]
    mov dx, [bx + 2]
    call bl_us100
    mov cx, 9
    call bl_kv
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_ide_ready - the primary channel's master, selected and ready. CF=1: none
vk_ide_ready:
    push ax
    push dx
    mov dx, VK_IDE + 6
    mov al, 0xA0
    out dx, al
    call vk_ide_nbsy
    jc .out
    mov dx, VK_IDE + 7
    in al, dx
    cmp al, 0xFF                    ; a floating bus: nothing there
    je .no
    test al, 0x40                   ; DRDY
    jnz .yes
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop dx
    pop ax
    ret

; vk_ide_nbsy - wait for BSY to clear (vk_ide_drq's bound). CF=1 it did not
vk_ide_nbsy:
    push ax
    push bx
    push cx
    push dx
    mov dx, VK_IDE + 7
    mov bx, VK_IDEWT
.o:
    xor cx, cx
.p:
    in al, dx
    test al, 0x80
    jz .ok
    loop .p
    dec bx
    jnz .o
    stc
    jmp short .out
.ok:
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_ide_drq - wait for DRQ, the way hd_ide_drq does. CF=1 ERR or too long:
; VK_IDEWT x 65536 status reads, seconds on a 286 and spent only by a drive
; that has failed - a command's first DRQ is a seek and a turn, and QEMU's
; drive on a loaded host has been seen to take half a million reads
VK_IDEWT    equ 64
vk_ide_drq:
    push bx
    push cx
    push dx
    mov dx, VK_IDE + 7
    mov bx, VK_IDEWT
.o:
    xor cx, cx
.p:
    in al, dx
    test al, 0x80
    jnz .a
    test al, 0x01
    jnz .e
    test al, 0x08
    jnz .ok
.a:
    loop .p
    dec bx
    jnz .o
.e:
    pop dx
    pop cx
    pop bx
    stc
    ret
.ok:
    pop dx
    pop cx
    pop bx
    clc
    ret

; vk_ide_cmd - READ SECTORS, VK_CPSEC of them at [vk_cplba] in the ROM's
; geometry, to the master. CF=1 the drive would not take it
vk_ide_cmd:
    push ax
    push bx
    push cx
    push dx
    call vk_ide_nbsy
    jc .out
    mov ax, [vk_cplba]
    xor dx, dx
    mov cl, [vk_spt]
    xor ch, ch
    div cx                          ; AX = the track, DX = the sector - 1
    mov bl, dl
    inc bl
    xor dx, dx
    mov cl, [vk_heads]
    div cx                          ; AX = the cylinder, DX = the head
    mov bh, dl
    mov cx, ax
    mov dx, VK_IDE + 6
    mov al, bh
    and al, 0x0F
    or al, 0xA0
    out dx, al
    call vk_ide_nbsy
    jc .out
    mov dx, VK_IDE + 2
    mov al, VK_CPSEC
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    inc dx
    mov al, cl
    out dx, al
    inc dx
    mov al, ch
    out dx, al
    mov dx, VK_IDE + 7
    mov al, 0x20
    out dx, al
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_cpcmd - one timed command: VK_CPSEC sectors into the buffer, each sector
; PIT-timed with interrupts off, by [vk_cpmode]'s loop
vk_cpcmd:
    push si
    push di
    push es
    mov es, [vk_buf]
    call vk_ide_cmd
    jc .err
    mov byte [vk_cpfirst], 1
    xor di, di
    mov si, VK_CPSEC
.s:
    call vk_ide_sec
    jc .out
    dec si
    jnz .s
    call vk_ide_nbsy
    jc .err
    mov dx, VK_IDE + 7
    in al, dx
    test al, 0x01
    jz .adv
    inc word [vk_cpbad]
.adv:
    add word [vk_cplba], VK_CPSEC
    jmp short .out
.err:
    inc word [vk_err]
.out:
    pop es
    pop di
    pop si
    ret

; vk_ide_sec - one sector, ES:DI the buffer (advanced past it): the wait for
; DRQ and the 256 words, each between two PIT reads with interrupts off.
; CF=1 the drive said ERR or never asked. Clobbers AX, BX, CX, DX
vk_ide_sec:
    pushf
    cli
    call bl_pit
    mov [vk_cpt0], ax
    call vk_ide_drq
    jc .err
    call bl_pit
    mov bx, [vk_cpt0]
    sub bx, ax                      ; the wait, in counts (a down-counter)
    mov [vk_cpt0], ax
    mov dx, VK_IDE
    mov cx, 256
    cld
    cmp byte [vk_cpmode], 0
    jne .loop
    db 0xF3, 0x6D                   ; rep insw (this row runs on CPU_286 up)
    jmp short .done
.loop:
    in ax, dx                       ; HDD.DRV's rung 1 before SPEC.md 52.1.2
    stosw
    loop .loop
.done:
    call bl_pit
    mov cx, [vk_cpt0]
    sub cx, ax                      ; the transfer
    popf
    add [vk_cpx], cx
    adc word [vk_cpx + 2], 0
    cmp byte [vk_cpfirst], 0
    je .later
    mov byte [vk_cpfirst], 0
    add [vk_cpf], bx
    adc word [vk_cpf + 2], 0
    clc
    ret
.later:
    add [vk_cpw], bx
    adc word [vk_cpw + 2], 0
    clc
    ret
.err:
    popf
    inc word [vk_err]
    stc
    ret

; vk_b_ide - the stream row's body: one command of VK_CPSEC sectors with
; `rep insw` and interrupts ON, nothing timed inside it
vk_b_ide:
    push es
    mov es, [vk_buf]
    call vk_ide_cmd
    jc .err
    xor di, di
    mov si, VK_CPSEC
    mov dx, VK_IDE
.s:
    call vk_ide_drq
    jc .err
    mov cx, 256
    cld
    db 0xF3, 0x6D
    dec si
    jnz .s
    call vk_ide_nbsy
    add word [vk_cplba], VK_CPSEC
    jmp short .out
.err:
    inc word [vk_err]
.out:
    pop es
    ret

; =============================================================================
; vk_larun - L and M: LOADALL on a 286 (docs/plans/LOADALL-PLAN.md 8)
;
; THE 286's LOADALL (`0F 05`) loads EVERY register, the hidden descriptor
; caches included, from 102 bytes at physical 0x800 - which in os8088 is
; KERNEL_SEG:0x0200, the published API cells 0x0204-0x0265. So every use is
; a BORROW inside one IF=0 window: save the 102 bytes, write the table,
; LOADALL, do the work through the cache it set, put the bytes back
; (vk_lborrow / vk_lrestore). Nothing calls a cell while interrupts are off.
;
; L builds the CS cache with access byte 93h, M with 9Bh: the one field of
; the table nobody here has seen work on silicon. A freeze at the probe with
; one is a reason to try the other.
;
; WHAT A FAILED CACHE LOAD DOES IS ARRANGED, not hoped: the table's VISIBLE
; ES selector is the bench's own 40 KB buffer, so a write through an ES whose
; cache did not take lands in memory the bench owns, and DS's is segment 0,
; so a read through a DS whose cache did not take reads the interrupt table
; and FAILS every comparison below rather than passing one.
;
; Rows, in this order: A20 as the machine has it; the probe (INT 6 trapped:
; not a 286's LOADALL); the caches checked below 1 MB, before anything is
; written through one (vk_lself); on an IDE drive at 1F0h, 64 sectors read by the task
; file STRAIGHT INTO XMS - each run of sectors whose DRQ is up inside one
; borrow - checked against the same sectors read plainly, then 2 MB of each
; timed, and how many sectors each borrow took; A20 after an OSAPI_XMEM_COPY;
; a 32 KB read IN PLACE checked against bytes that copy put there (and the
; same read 1 KB off, which must differ); a 32 KB write in place read back
; through OSAPI_XMEM_COPY; a borrow's fixed cost and the copies by both
; transports at 32, 8, 4 and 1 KB; 32 KB read above 1 MB against below it.
; The fill is FIRST because it makes no AH=87h call, and some AT BIOSes shut
; A20 behind every one. And EVERY WRITE through a cache asks the gate first
; (vk_la20) and is skipped, and counted, when it is shut: a write through a
; shut gate lands 1 MB lower, in conventional memory. Read-only on the disk.
; Saves VDLALL.TXT.
; =============================================================================
VK_LN       equ 8                   ; iterations of a timed row
VK_LRUN     equ 8                   ; sectors a borrow takes at most
VK_LDRQ     equ 256                 ; status reads a borrow waits for the next
                                    ; DRQ before it lets the interrupts in
VK_LSTRM    equ 64                  ; 32 KB units a fill row: 2 MB

vk_larun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov word [vk_err], 0
    mov word [bl_nrow], 0
    mov word [vk_ladone], 0
    mov word [vk_la20x], 0
    mov si, vk_s_latitle
    call bl_sline
    mov si, vk_r_lacs
    mov al, [vk_lacs]
    xor ah, ah
    xor dx, dx
    mov cx, 9
    call bl_kv
    call vk_claim
    jnc .cpu
    mov si, vk_s_fail
    call bl_sline
    jmp .save
.cpu:
    call OSAPI_CPU_INFO
    push ax
    xor ah, ah
    xor dx, dx
    mov si, vk_r_xmtier
    mov cx, 9
    call bl_kv
    pop ax
    cmp al, CPU_286                 ; 286 ONLY: a 386 executes 0F 05 as an
    je .xms                         ; invalid opcode, and its LOADALL is a
    mov si, vk_s_lanot              ; different instruction
    call bl_sline
    jmp .save
.xms:
    call OSAPI_XMEM_CAPS
    xor dx, dx
    mov cx, 9
    mov si, vk_r_xmfree
    call bl_kv
    cmp ax, VK_XMKB
    jae .alloc
    mov si, vk_s_xmnone
    call bl_sline
    jmp .save
.alloc:
    mov dx, VK_XMKB / 64
    xor ax, ax
    call OSAPI_XMEM_ALLOC
    jnc .got
    mov si, vk_s_xmnoalloc
    call bl_sline
    jmp .save
.got:
    mov [vk_xmb], ax
    mov [vk_xmb + 2], dx
    call vk_xmat                    ; where this boot put the two ends
    call vk_lprep                   ; the template, from SMSW / SIDT / SGDT
    mov si, vk_r_lamsw
    mov ax, [la_msw]
    xor dx, dx
    mov cx, 9
    call bl_kv
    mov si, vk_r_laidb
    mov ax, [la_idt]
    mov dl, [la_idt + 2]
    xor dh, dh
    call bl_kv
    mov si, vk_r_laidl
    mov ax, [la_idt + 4]
    xor dx, dx
    call bl_kv
    ; --- A20 as the machine has it, before this run copies anything
    mov si, vk_r_la20a
    call vk_la20say
    ; --- THE PROBE: a borrow that changes nothing, INT 6 hooked across it
    mov si, vk_s_laprobe
    call bl_progress
    call vk_lprobe
    mov di, vk_s_layes
    cmp byte [vk_lfault], 0
    je .pr
    mov di, vk_s_lano
.pr:
    mov si, vk_r_laprobe
    call bl_kvs
    cmp byte [vk_lfault], 0
    je .self
    jmp .free
    ; --- THE CACHES TOOK, before anything is WRITTEN through one: both
    ; aimed 1 KB into the bench's own buffer and compared. A DS cache that
    ; did not take reads the interrupt table (its selector is 0), an ES one
    ; reads the buffer from 0 (its selector): either way the bytes differ
.self:
    call vk_lself
    mov di, vk_s_dok
    jnc .sf
    mov di, vk_s_labad
    inc word [vk_err]
.sf:
    mov si, vk_r_laself
    call bl_kvs
    cmp word [vk_err], 0
    je .fill
    jmp .free
    ; --- THE FILL FIRST: sectors by the task file straight into XMS. It is
    ; first because it needs no OSAPI_XMEM_COPY - its pattern goes up by
    ; LOADALL and its reference is the disk itself - so a BIOS that shuts A20
    ; behind every AH=87h (the rows below make them) cannot take it away
.fill:
    mov si, vk_s_lafhdr
    call bl_sline
    call vk_lfill
    ; --- READ IN PLACE: chunk 3's pattern up to the block's second half by
    ; OSAPI_XMEM_COPY, then compared through a borrow, and 1 KB off
    mov si, vk_s_lachk
    call bl_sline
    mov ax, 3
    call vk_fill
    mov word [vk_xmlen], VK_CHUNK
    mov word [vk_xmhalf], VK_CHUNK
    call vk_b_xmup
    mov si, vk_r_la20b
    call vk_la20say
    xor ax, ax
    call vk_lcmp                    ; CF = 1: the bytes differ
    mov di, vk_s_dok
    jnc .rd
    mov di, vk_s_labad
    inc word [vk_err]
.rd:
    mov si, vk_r_lard
    call bl_kvs
    mov ax, 1024
    call vk_lcmp
    mov di, vk_s_ladiff
    jc .rd2
    mov di, vk_s_lasame
    inc word [vk_err]
.rd2:
    mov si, vk_r_laoff
    call bl_kvs
    cmp word [vk_err], 0            ; a read that is not the bytes: nothing is
    je .wr                          ; WRITTEN through a cache that may be wrong
    jmp .free
.wr:
    ; --- WRITE IN PLACE: chunk 0's pattern up to the block's first half
    ; through a borrow, the buffer overwritten, and the half brought back
    ; down by OSAPI_XMEM_COPY: it must be chunk 0's again
    xor ax, ax
    call vk_fill
    mov word [vk_xmlen], VK_CHUNK
    mov word [vk_xmhalf], 0
    call vk_b_lup
    mov ax, 3
    call vk_fill
    call vk_b_xmdn
    mov di, vk_s_dok
    push es
    mov es, [vk_buf]
    cmp word [es:0], 0
    jne .wbad
    cmp word [es:2], 0
    jne .wbad
    cmp word [es:VK_CHUNK / 2], 0x4000
    jne .wbad
    cmp word [es:VK_CHUNK - 4], 0x7FFC
    jne .wbad
    cmp word [es:VK_CHUNK - 2], 0
    je .wsay
.wbad:
    mov di, vk_s_labad
    inc word [vk_err]
.wsay:
    pop es
    mov si, vk_r_lawr
    call bl_kvs
    cmp word [vk_err], 0
    je .rows
    jmp .free                       ; wrong bytes: nothing worth timing
    ; --- the timed rows
.rows:
    mov si, vk_s_lahdr
    call bl_sline
    mov word [bl_n], VK_LN
    mov word [bl_body], vk_b_lnop
    mov si, vk_r_lanop
    xor al, al
    call bl_run
    mov word [vk_xmhalf], 0
    mov bx, vk_larows
.row:
    mov ax, [bx]
    or ax, ax
    jz .reads
    mov [vk_xmlen], ax
    mov word [bl_body], vk_b_xmup
    mov si, [bx + 2]
    xor al, al
    call bl_run
    mov word [bl_body], vk_b_xmdn
    mov si, [bx + 4]
    xor al, al
    call bl_run
    mov word [bl_body], vk_b_lup
    mov si, [bx + 6]
    xor al, al
    call bl_run
    mov word [bl_body], vk_b_ldn
    mov si, [bx + 8]
    xor al, al
    call bl_run
    add bx, 10
    jmp short .row
.reads:
    mov word [bl_body], vk_b_lrdx
    mov si, vk_r_larx
    xor al, al
    call bl_run
    mov word [bl_body], vk_b_lrdc
    mov si, vk_r_larc
    xor al, al
    call bl_run
.free:
    mov ax, [vk_xmb]
    mov dx, [vk_xmb + 2]
    call OSAPI_XMEM_FREE
    mov si, vk_r_la20x              ; LOADALL writes refused on a shut gate
    mov ax, [vk_la20x]
    xor dx, dx
    mov cx, 9
    call bl_kv
.save:
    mov si, vk_r_err
    mov ax, [vk_err]
    xor dx, dx
    mov cx, 9
    call bl_kv
    call bl_operator
    mov si, vk_f_latxt              ; the report, beside the bench
    call bl_save
    inc word [vk_ladone]            ; for a harness: L has finished
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_la20say - SI = label: "open" or "SHUT" for the A20 line right now
vk_la20say:
    push di
    call vk_la20
    mov di, vk_s_laopen
    jnc .s
    mov di, vk_s_lashut
.s:
    call bl_kvs
    pop di
    ret

; vk_la20 - is the A20 line open RIGHT NOW? CF = 0 open, 1 shut. XMEM.DRV's
; wraparound probe (xm_a20_probe): flip the word at FFFF:0510 and see whether
; 0000:0500 moved with it, then flip it back. Preserves all but flags
vk_la20:
    push ax
    push bx
    push ds
    push es
    pushf
    cli
    xor ax, ax
    mov es, ax
    dec ax
    mov ds, ax
    mov bx, [es:0x0500]
    not word [0x0510]
    mov ax, [es:0x0500]
    not word [0x0510]
    popf
    cmp ax, bx                      ; equal: the low word did not follow
    pop es
    pop ds
    pop bx
    pop ax
    je .open
    stc
    ret
.open:
    clc
    ret

; THE BORROW IS apps/os88la.inc's - the include XMEM.DRV's LOADALL transport
; assembles (SPEC.md 41.13), which is this bench's own borrow lifted out of it
; after it ran on the Victory 286. Running it here is what proves the include
; on a 286: these are thin names over it.
vk_lds      equ la_dsc                  ; a borrow's DS cache, 24-bit linear
vk_les      equ la_esc                  ; ...and ES's
vk_lfault   equ la_fault                ; the probe trapped as INT 6
vk_llin     equ la_lin                  ; AX = a segment -> DX:AX linear
vk_lrestore equ la_restore              ; the cells back, DS = ES = ours

; vk_lprep - the include's capture of the MSW, IDTR and GDTR, and the CS
; cache's access byte the key chose (L 93h, M 9Bh). Preserves all but flags
vk_lprep:
    push ax
    call la_capture
    mov al, [vk_lacs]
    mov [la_csacc], al
    pop ax
    ret

; vk_lborrow - IF = 0: the include's borrow with DS's cache at [vk_lds] and
; ES's at [vk_les], SHOWING selector 0 and the buffer (vk_larun's header: a
; cache that did not take then reads the interrupt table and writes our own
; buffer). Every general register preserved; CS: for our data until
; vk_lrestore
vk_lborrow:
    push ax
    mov word [la_dsv], 0
    mov ax, [vk_buf]
    mov [la_esv], ax
    pop ax
    jmp la_borrow

; vk_lprobe - the include's probe: a borrow that changes nothing, INT 6
; hooked; [vk_lfault] = 1 when the instruction trapped
vk_lprobe:
    pushf
    call la_probe
    popf
    ret

%include "os88la.inc"
%include "os88xgap.inc"

; vk_lxa - AX = an offset into the block -> DX:AX = its linear address
vk_lxa:
    xor dx, dx
    add ax, [vk_xmb]
    adc dx, [vk_xmb + 2]
    ret

; vk_lcmp - AX = an offset into the block's SECOND half: its 32 KB compared,
; IN PLACE, with the buffer. CF = 1 they differ. Preserves all but flags
vk_lcmp:
    push ax
    push cx
    push dx
    push si
    push di
    add ax, VK_CHUNK
    call vk_lxa
    mov [vk_lds], ax
    mov [vk_lds + 2], dx
    mov ax, [vk_buf]
    call vk_llin
    mov [vk_les], ax
    mov [vk_les + 2], dx
    pushf
    cli
    call vk_lborrow
    xor si, si
    xor di, di
    mov cx, VK_CHUNK / 2 - 512      ; 1 KB short, so the 1 KB-off row stays
    repe cmpsw                      ; inside the block's 64 KB
    mov [cs:vk_lcx], cx
    lahf
    mov [cs:vk_lzf], ah
    call vk_lrestore
    popf
    test byte [vk_lzf], 0x40        ; ZF: every word equal
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    jz .diff
    clc
    ret
.diff:
    stc
    ret

; vk_lself - the caches, checked below 1 MB (vk_larun's .self). CF = 1 the
; two reads differed. Preserves all but flags
vk_lself:
    push ax
    push cx
    push dx
    push si
    push di
    mov ax, 3
    call vk_fill
    mov ax, [vk_buf]
    call vk_llin
    add ax, 1024
    adc dx, 0
    mov [vk_lds], ax
    mov [vk_lds + 2], dx
    mov [vk_les], ax
    mov [vk_les + 2], dx
    pushf
    cli
    call vk_lborrow
    xor si, si
    xor di, di
    mov cx, (VK_CHUNK - 2048) / 2
    repe cmpsw
    lahf
    mov [cs:vk_lzf], ah
    call vk_lrestore
    popf
    test byte [vk_lzf], 0x40
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    jz .diff
    clc
    ret
.diff:
    stc
    ret

; the timed bodies. A borrow wants IF = 0, which method P's iteration is
vk_b_lnop:                          ; a borrow and nothing else
    mov ax, ds
    call vk_llin
    mov [vk_lds], ax
    mov [vk_lds + 2], dx
    mov ax, [vk_buf]
    call vk_llin
    mov [vk_les], ax
    mov [vk_les + 2], dx
    call vk_lborrow
    call vk_lrestore
    ret

vk_b_lup:                           ; buffer -> block at [vk_xmhalf], by LOADALL
    call vk_la20                    ; A WRITE ONLY THROUGH AN OPEN GATE: a
    jnc .open                       ; shut one would put it 1 MB lower, in
    inc word [vk_la20x]             ; conventional memory, and AH=87h may
    ret                             ; close it after every OSAPI_XMEM_COPY
.open:
    pushf
    cli
    mov ax, [vk_buf]
    call vk_llin
    mov [vk_lds], ax
    mov [vk_lds + 2], dx
    mov ax, [vk_xmhalf]
    call vk_lxa
    mov [vk_les], ax
    mov [vk_les + 2], dx
    mov cx, [vk_xmlen]
    shr cx, 1
    call vk_lborrow
    xor si, si
    xor di, di
    rep movsw
    call vk_lrestore
    popf
    ret

vk_b_ldn:                           ; block at [vk_xmhalf] -> buffer, by LOADALL
    pushf
    cli
    mov ax, [vk_xmhalf]
    call vk_lxa
    mov [vk_lds], ax
    mov [vk_lds + 2], dx
    mov ax, [vk_buf]
    call vk_llin
    mov [vk_les], ax
    mov [vk_les + 2], dx
    mov cx, [vk_xmlen]
    shr cx, 1
    call vk_lborrow
    xor si, si
    xor di, di
    rep movsw
    call vk_lrestore
    popf
    ret

vk_b_lrdx:                          ; 32 KB READ above 1 MB, through a borrow
    xor ax, ax
    call vk_lxa
    mov [vk_lds], ax
    mov [vk_lds + 2], dx
    mov ax, [vk_buf]
    call vk_llin
    mov [vk_les], ax
    mov [vk_les + 2], dx
    mov cx, VK_CHUNK / 2
    call vk_lborrow
    xor si, si
    rep lodsw
    call vk_lrestore
    ret

vk_b_lrdc:                          ; ...and below it, the buffer
    push ds
    mov ds, [vk_buf]
    xor si, si
    mov cx, VK_CHUNK / 2
    cld
    rep lodsw
    pop ds
    ret

; vk_lfill - the fill rows (see vk_larun's header). On the 286 with an IDE
; drive at 1F0h; read-only
vk_lfill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, 8                       ; the geometry, off the ROM, for the task
    mov dl, 0x80                    ; file's CHS (vk_ide_cmd)
    push es
    int 0x13
    pop es
    jnc .gok
    mov si, vk_s_cpnoide
    call bl_sline
    jmp .out
.gok:
    mov al, cl
    and al, 0x3F
    mov [vk_spt], al
    inc dh
    mov [vk_heads], dh
    call vk_ide_ready
    jnc .ide
    mov si, vk_s_cpnoide
    call bl_sline
    jmp .out
.ide:
    mov al, [vk_heads]              ; cylinder VK_CPCYL, as C reads
    mul byte [vk_spt]
    mov cx, VK_CPCYL
    mul cx
    mov [vk_cplba], ax
    mov [vk_lba0], ax
    ; the CHECK: the block's first half set to a pattern no disk holds, 64
    ; sectors read into it by the task file through borrows, then the same
    ; 64 read plainly into the buffer, and the two compared in place
    mov ax, 3
    call vk_fill
    mov word [vk_xmlen], VK_CHUNK
    mov word [vk_xmhalf], 0
    call vk_b_lup                   ; up BY LOADALL: no AH=87h before the fill
    call vk_lzero
    call vk_b_lide
    mov ax, [vk_lba0]
    mov [vk_cplba], ax
    call vk_b_ide
    mov ax, -VK_CHUNK               ; vk_lcmp's offset is into the second
    call vk_lcmp                    ; half: -32 KB is the first
    mov di, vk_s_dok
    jnc .fs
    mov di, vk_s_labad
    inc word [vk_err]
.fs:
    mov si, vk_r_lafck
    call bl_kvs
    cmp di, vk_s_dok
    jne .out
    ; the rows: 2 MB plainly (interrupts on), then 2 MB into XMS
    call vk_lzero
    mov ax, [vk_lba0]
    mov [vk_cplba], ax
    mov word [bl_n], VK_LSTRM
    mov word [bl_body], vk_b_ide
    mov si, vk_r_lafpl
    mov al, 1                       ; method T
    call bl_run
    call vk_lzero
    mov ax, [vk_lba0]
    mov [vk_cplba], ax
    mov word [bl_n], VK_LSTRM
    mov word [bl_body], vk_b_lide
    mov si, vk_r_lafxm
    mov al, 1
    call bl_run
    mov bx, vk_lfrows
.r:
    mov si, [bx]
    or si, si
    jz .max
    mov di, [bx + 2]
    mov ax, [di]
    xor dx, dx
    mov cx, 9
    call bl_kv
    add bx, 4
    jmp short .r
.max:
    mov ax, [vk_lmax]               ; the longest masked borrow, us x 100
    xor dx, dx
    mov cx, 1
    call bl_us100
    mov si, vk_r_lafmx
    mov cx, 9
    call bl_kv
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vk_lzero - the fill's counters to 0
vk_lzero:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, vk_lcnt
    mov cx, VK_LCNTW
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; vk_b_lide - one command of VK_CPSEC sectors into the block's first half,
; the sectors whose DRQ is up taken in borrows of up to VK_LRUN, the
; interrupts let in between borrows and for every wait that is not short
vk_b_lide:
    call vk_la20                    ; vk_b_lup's rule: the gate is checked
    jnc .open                       ; before every write through a cache, and
    inc word [vk_la20x]             ; a shut one reads the command plainly
    jmp vk_b_ide
.open:
    push es
    call vk_ide_cmd
    jc .err
    xor ax, ax
    call vk_lxa
    mov [vk_les], ax
    mov [vk_les + 2], dx
    xor ax, ax                      ; DS's cache: segment 0, never read
    mov [vk_lds], ax
    mov [vk_lds + 2], ax
    xor di, di
    mov si, VK_CPSEC
.borrow:
    call vk_ide_drq                 ; the first of a run, interrupts on
    jc .err
    pushf
    cli
    call bl_pit
    mov [vk_lt0], ax
    call vk_lborrow
    xor bx, bx                      ; BX = sectors this borrow
.sec:
    mov dx, VK_IDE
    mov cx, 256
    db 0xF3, 0x6D                   ; rep insw, into XMS through ES's cache
    inc bx
    dec si
    jz .end
    cmp bx, VK_LRUN
    jae .end
    mov dx, VK_IDE + 7              ; the next DRQ, if it comes soon
    mov cx, VK_LDRQ
.poll:
    in al, dx
    test al, 0x80
    jnz .pn
    test al, 0x01
    jnz .end
    test al, 0x08
    jnz .sec
.pn:
    loop .poll
.end:
    call vk_lrestore                ; DS and ES ours again
    call bl_pit
    mov cx, [vk_lt0]
    sub cx, ax
    popf
    cmp cx, [vk_lmax]
    jbe .tally
    mov [vk_lmax], cx
.tally:
    inc word [vk_lnb]
    add [vk_lns], bx
    mov ax, bx                      ; the histogram: 1, 2-3, 4-7, 8+
    mov cx, vk_lh1
    cmp ax, 1
    je .h
    mov cx, vk_lh2
    cmp ax, 4
    jb .h
    mov cx, vk_lh4
    cmp ax, VK_LRUN
    jb .h
    mov cx, vk_lh8
.h:
    xchg cx, bx
    inc word [bx]
    or si, si
    jnz .borrow
    call vk_ide_nbsy
    add word [vk_cplba], VK_CPSEC
    jmp short .out
.err:
    inc word [vk_err]
.out:
    pop es
    ret

%define BL_NUMBERED                 ; a field run saves NAME1, NAME2... (benchlib)
%define BL_INTRO_HOW 'automatically, when a test ends' ; benchlib's banner
%include "benchlib.inc"

vk_tpl:
    dw 7, 22, 632, 300
    dw vk_ttl, vk_paint, vk_onkey, vk_onclick

vk_ttl:       db 'Video Disk Bench', 0
vk_f_names:   db 'STREAM.DAT', 0, 'BADAPPLE.V88', 0, 'BAPPLE.V88', 0, 0
vk_f_txt:     db 'VIDDISK.TXT', 0
vk_f_wtxt:    db 'VDWRITE.TXT', 0
vk_s_cptitle: db 'VIDDISK C - what a read costs the CPU (DISK-CPU-PLAN 7)', 0
vk_s_cp86:    db 'Below a 286: no rep insw here - the READ_SEQ row only', 0
vk_s_cpnoide: db 'No drive answering at 1F0h master - the READ_SEQ row only', 0
vk_s_cphdr:   db '-- one sector at a time, PIT-timed, ints off (us x 100) --', 0
vk_s_cphdr2:  db '-- 4 MB streamed, ints on: the kernel, then direct --', 0
vk_r_cppit:   db 'a PIT read, itself', 0
vk_r_cpxi:    db 'rep insw, 256 words', 0
vk_r_cpwi:    db '...the wait for DRQ', 0
vk_r_cpxl:    db 'in/stosw/loop, 256 w', 0
vk_r_cpwl:    db '...the wait for DRQ', 0
vk_r_cpfirst: db '...a command, 1st DRQ', 0
vk_r_cpbad:   db '...commands ending ERR', 0
vk_r_cpdir:   db 'direct 64-sector cmd', 0
vk_r_cpseq:   db 'READ_SEQ 32K', 0
vk_cprows:    dw vk_r_cpxi, vk_r_cpwi, vk_r_cpxl, vk_r_cpwl
vk_f_ctxt:    db 'VDCPU.TXT', 0
vk_s_title:   db 'VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)', 0
vk_s_hint:    db 'R (or a click) runs: reads only, saves VIDDISK.TXT. No stream? W.', 0
vk_r_wpath:   db 'the writer', 0
vk_wpaths:    dw vk_s_wp0, vk_s_wp1, vk_s_wp2, vk_s_wp3, vk_s_wp4, vk_s_wp5
              dw vk_s_wp6
vk_s_wp0:     db 'A: APPEND, a walk a call', 0
vk_s_wp1:     db 'P: WRITE_SEQ, plain', 0
vk_s_wp2:     db 'W: WRITE_SEQ, HELD', 0
vk_s_wp3:     db 'U: held, never closed', 0
vk_s_wp4:     db 'I: held, another file', 0
vk_s_wp5:     db 'K: held, deleted', 0
vk_s_wp6:     db 'F: held, no room check', 0
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
vk_s_hdrx:    db '-- int 13h: ONE call across a head (the kernel ends at a track) --', 0
vk_r_xhead:   db 'one call across a head', 0
vk_r_xcyl:    db 'one call across a cyl', 0
vk_r_xst:     db '...the ROM said (AH)', 0
vk_s_xok:     db 'ok - the same bytes', 0
vk_s_xbad:    db 'WRONG BYTES - short or wrong head', 0
vk_s_xref:    db 'refused', 0
vk_s_xsfail:  db 'track reads failed: no answer', 0
vk_s_xskip:   db 'not timed: the crossing read was not right', 0
vk_r_tn:      db 'sectors timed, each way', 0
vk_r_tsplit:  db 'N sectors, track calls', 0
vk_r_tone:    db 'N sectors, one call', 0
vk_r_swn:     db 'kernel shapes read', 0
vk_r_swbad:   db '...WRONG BYTES', 0
vk_r_swref:   db '...refused', 0
vk_r_swfc:    db 'first wrong: cylinder', 0
vk_r_swfh:    db '...head', 0
vk_r_swfs:    db '...from sector', 0
vk_r_swfn:    db '...sectors', 0

vk_s_xmtitle: db 'VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)', 0
vk_s_xmnone:  db 'NO EXTENDED MEMORY (64 KB or more): nothing to time', 0
vk_s_xmnoalloc: db 'OSAPI_XMEM_ALLOC REFUSED 64 KB', 0
vk_s_xmbad:   db 'BAD - not the bytes copied up', 0
vk_s_xmhdr:   db '-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --', 0
vk_s_xmhdrf:  db '-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --', 0
vk_r_xmtier:  db 'CPU tier (1 286, 2 386)', 0
vk_r_xmbits:  db 'CPU feature bits', 0
vk_r_xmfree:  db 'XMS free (KB)', 0
vk_r_xwbuf:   db 'buffer at (KB)', 0
vk_r_xwxms:   db 'XMS block at (KB)', 0
vk_s_xswhdr:  db '-- 8K down from 16 places 64 KB apart (us) --', 0
vk_s_xswno:   db 'No 1 MB of XMS free - the sweep not run', 0
vk_r_xram:    db 'RAM->RAM 8K (control)', 0
vk_s_xchdr:   db '-- 8K down into conventional memory, 64 KB claims (us) --', 0
vk_s_xcno:    db 'No 64 KB of conventional heap free - not run', 0
vk_r_xcat:    db 'conventional at (KB)', 0
vk_r_xcdn:    db 'down 8K into it', 0
vk_s_xfhdr:   db '-- ...into the first claim at offsets (us) --', 0
vk_r_xfoff:   db 'offset in it (bytes)', 0
vk_r_xwbufb:  db 'buffer at (bytes)', 0
vk_r_xwxmsb:  db 'XMS block (bytes)', 0
vk_s_xghdr:   db '-- 8K, conventional D and XMS S stepped (us) --', 0
vk_r_xgat:    db 'first claim (bytes)', 0
vk_r_xgg:     db 'xg_gap answers G', 0
vk_r_x87ck:   db '4 KB up and back, 87h', 0
vk_s_xgrhd:   db '-- the transports, the buffer G apart (and 0 apart) (us) --', 0
vk_xgtab:                           ; vk_xgrows': bytes, 0 G apart / 1 0
                                    ; apart, the body, the label
              dw 32768, 0, vk_b_xcup, vk_r_xg00
              dw 32768, 0, vk_b_xcdn, vk_r_xg01
              dw 32768, 0, vk_b_x87up, vk_r_xg02
              dw 32768, 0, vk_b_x87dn, vk_r_xg03
              dw 16384, 0, vk_b_xcup, vk_r_xg04
              dw 16384, 0, vk_b_xcdn, vk_r_xg05
              dw 16384, 0, vk_b_x87up, vk_r_xg06
              dw 16384, 0, vk_b_x87dn, vk_r_xg07
              dw 8192, 0, vk_b_xcup, vk_r_xg08
              dw 8192, 0, vk_b_xcdn, vk_r_xg09
              dw 8192, 0, vk_b_x87up, vk_r_xg10
              dw 8192, 0, vk_b_x87dn, vk_r_xg11
              dw 4096, 0, vk_b_xcup, vk_r_xg12
              dw 4096, 0, vk_b_xcdn, vk_r_xg13
              dw 4096, 0, vk_b_x87up, vk_r_xg14
              dw 4096, 0, vk_b_x87dn, vk_r_xg15
              dw 32768, 1, vk_b_xcup, vk_r_xg16
              dw 32768, 1, vk_b_xcdn, vk_r_xg17
              dw 32768, 1, vk_b_x87up, vk_r_xg18
              dw 32768, 1, vk_b_x87dn, vk_r_xg19
              dw 0
vk_r_xg00:    db 'G: XMEM up 32K', 0
vk_r_xg01:    db 'G: XMEM down 32K', 0
vk_r_xg02:    db 'G: 87h up 32K', 0
vk_r_xg03:    db 'G: 87h down 32K', 0
vk_r_xg04:    db 'G: XMEM up 16K', 0
vk_r_xg05:    db 'G: XMEM down 16K', 0
vk_r_xg06:    db 'G: 87h up 16K', 0
vk_r_xg07:    db 'G: 87h down 16K', 0
vk_r_xg08:    db 'G: XMEM up 8K', 0
vk_r_xg09:    db 'G: XMEM down 8K', 0
vk_r_xg10:    db 'G: 87h up 8K', 0
vk_r_xg11:    db 'G: 87h down 8K', 0
vk_r_xg12:    db 'G: XMEM up 4K', 0
vk_r_xg13:    db 'G: XMEM down 4K', 0
vk_r_xg14:    db 'G: 87h up 4K', 0
vk_r_xg15:    db 'G: 87h down 4K', 0
vk_r_xg16:    db '0: XMEM up 32K', 0
vk_r_xg17:    db '0: XMEM down 32K', 0
vk_r_xg18:    db '0: 87h up 32K', 0
vk_r_xg19:    db '0: 87h down 32K', 0
vk_s_xrphdr:  db '-- READ_SEQ alone into the buffer at +n KB (KB/s x 10) --', 0
vk_xrpoff:    dw 0, 1024, 2048, 3072, 0
VK_XSWL     equ 16
vk_r_xsw:
%assign vk_i 0
%rep 16
%defstr vk_is vk_i
              db 'down 8K +', vk_is, '*64K'
              times VK_XSWL - 1 - 13 - %strlen(vk_is) db ' '
              db 0                  ; (VK_XSWL bytes, the NUL among them)
%assign vk_i vk_i + 1
%endrep
vk_r_xg:                            ; vk_xgrid's twenty rows, VK_XSWL each
%assign vk_d 0
%rep 4
%assign vk_s 0
%rep 4
              db 'down D+', '0' + vk_d, 'K S+', '0' + vk_s, 'K'
              times VK_XSWL - 1 - 14 db ' '
              db 0
%assign vk_s vk_s + 1
%endrep
%assign vk_d vk_d + 1
%endrep
%assign vk_d 0
%rep 4
              db 'up   D+', '0' + vk_d, 'K S+0K'
              times VK_XSWL - 1 - 14 db ' '
              db 0
%assign vk_d vk_d + 1
%endrep
vk_r_xrp:
%assign vk_d 0
%rep 5
              db 'READ_SEQ +', '0' + (vk_d % 4), 'K'
              times VK_XSWL - 1 - 12 db ' '
              db 0
%assign vk_d vk_d + 1
%endrep
vk_r_xmchk:   db '32 KB up and back', 0
vk_r_xmseq:   db 'READ_SEQ alone', 0
vk_r_xmbank:  db 'READ_SEQ + copy up', 0
vk_r_xu32:    db 'XMS up 32K', 0
vk_r_xd32:    db 'XMS down 32K', 0
vk_r_xu16:    db 'XMS up 16K', 0
vk_r_xd16:    db 'XMS down 16K', 0
vk_r_xu8:     db 'XMS up 8K', 0
vk_r_xd8:     db 'XMS down 8K', 0
vk_r_xu4:     db 'XMS up 4K', 0
vk_r_xd4:     db 'XMS down 4K', 0
vk_f_xtxt:    db 'VDXMS.TXT', 0
vk_f_latxt:   db 'VDLALL.TXT', 0
vk_s_latitle: db 'VIDDISK L - LOADALL: extended memory in place (LOADALL-PLAN 8)', 0
vk_s_lanot:   db 'NOT A 286: LOADALL (0F 05) is the 286 alone - nothing to run', 0
vk_s_laprobe: db 'L: probing LOADALL - a freeze here: reset, then try M', 0
vk_s_layes:   db 'ran', 0
vk_s_lano:    db 'TRAPPED as an invalid opcode', 0
vk_s_laopen:  db 'open', 0
vk_s_lashut:  db 'SHUT', 0
vk_s_labad:   db 'BAD - not the bytes', 0
vk_s_ladiff:  db 'differs, as it must', 0
vk_s_lasame:  db 'SAME - the check is blind', 0
vk_s_lahdr:   db '-- a borrow and the copies, PIT-timed, ints off --', 0
vk_s_lafhdr:  db '-- the fill: 64 sectors by the task file straight into XMS --', 0
vk_s_lachk:   db '-- in place against OSAPI_XMEM_COPY, the other transport --', 0
vk_r_lacs:    db 'CS cache access byte', 0
vk_r_lamsw:   db 'MSW (SMSW)', 0
vk_r_laidb:   db 'IDTR base', 0
vk_r_laidl:   db 'IDTR limit', 0
vk_r_la20a:   db 'A20 before any copy', 0
vk_r_la20b:   db 'A20 after XMEM_COPY', 0
vk_r_laprobe: db 'LOADALL', 0
vk_r_lard:    db '32K read in place', 0
vk_r_laself:  db 'caches, below 1 MB', 0
vk_r_laoff:   db '...and 1 KB off', 0
vk_r_lawr:    db '32K written in place', 0
vk_r_lanop:   db 'a borrow, no work', 0
vk_r_lxu32:   db 'XMEM_COPY up 32K', 0
vk_r_lxd32:   db 'XMEM_COPY down 32K', 0
vk_r_llu32:   db 'LOADALL up 32K', 0
vk_r_lld32:   db 'LOADALL down 32K', 0
vk_r_lxu8:    db 'XMEM_COPY up 8K', 0
vk_r_lxd8:    db 'XMEM_COPY down 8K', 0
vk_r_llu8:    db 'LOADALL up 8K', 0
vk_r_lld8:    db 'LOADALL down 8K', 0
vk_r_lxu4:    db 'XMEM_COPY up 4K', 0
vk_r_lxd4:    db 'XMEM_COPY down 4K', 0
vk_r_llu4:    db 'LOADALL up 4K', 0
vk_r_lld4:    db 'LOADALL down 4K', 0
vk_r_lxu1:    db 'XMEM_COPY up 1K', 0
vk_r_lxd1:    db 'XMEM_COPY down 1K', 0
vk_r_llu1:    db 'LOADALL up 1K', 0
vk_r_lld1:    db 'LOADALL down 1K', 0
vk_r_larx:    db 'read 32K above 1 MB', 0
vk_r_larc:    db 'read 32K below 1 MB', 0
vk_r_lafck:   db '64 sectors into XMS', 0
vk_r_lafpl:   db '64-sector cmd, plain', 0
vk_r_lafxm:   db '64-sector cmd, into XMS', 0
vk_r_lafnb:   db 'borrows taken', 0
vk_r_lafns:   db 'sectors in borrows', 0
vk_r_lafh1:   db 'borrows of 1 sector', 0
vk_r_lafh2:   db 'borrows of 2-3', 0
vk_r_lafh4:   db 'borrows of 4-7', 0
vk_r_lafh8:   db 'borrows of 8', 0
vk_r_lafmx:   db 'longest borrow us x100', 0
vk_r_la20x:   db 'writes skipped, A20 SHUT', 0
vk_larows:    dw 32768, vk_r_lxu32, vk_r_lxd32, vk_r_llu32, vk_r_lld32
              dw 8192, vk_r_lxu8, vk_r_lxd8, vk_r_llu8, vk_r_lld8
              dw 4096, vk_r_lxu4, vk_r_lxd4, vk_r_llu4, vk_r_lld4
              dw 1024, vk_r_lxu1, vk_r_lxd1, vk_r_llu1, vk_r_lld1, 0
vk_lfrows:    dw vk_r_lafnb, vk_lnb, vk_r_lafns, vk_lns, vk_r_lafh1, vk_lh1
              dw vk_r_lafh2, vk_lh2, vk_r_lafh4, vk_lh4, vk_r_lafh8, vk_lh8, 0
vk_xmrows:    dw 32768, vk_r_xu32, vk_r_xd32, 16384, vk_r_xu16, vk_r_xd16
              dw 8192, vk_r_xu8, vk_r_xd8, 4096, vk_r_xu4, vk_r_xd4, 0
vk_xmb:       dw 0, 0           ; the block
vk_xsw:       dw 0, 0           ; vk_xsweep's 1 MB
VK_XCN      equ 8                   ; vk_xconv's claims, 64 KB each
vk_xcsegs:    times VK_XCN dw 0
vk_xcn:       dw 0              ; ...how many it got, x 2
vk_xcs:       dw 0              ; ...the row's block
vk_xco:       dw 0              ; ...and its offset
vk_xso:       dw 0              ; vk_xgrid's offset into the XMS block
vk_rdoff:     dw 0              ; vk_b_seq's offset into the buffer
vk_xcl:       dw 8192           ; vk_b_xcup/xcdn's bytes
vk_xbo:       dw 0              ; vk_xgrows' offsets in the first claim: G
vk_xwo:       dw 0              ; ...and 0 apart
vk_gdt:       times 48 db 0     ; vk_x87's descriptor block
              XG_DATA
vk_xmlen:     dw 0              ; ...a copy's bytes
vk_xmhalf:    dw 0              ; ...and where in the block
vk_xmup:      db 0              ; vk_xmfill: copy each chunk up
              db 0
vk_xmdone:    dw 0              ; for a harness: X has finished
vk_s_emtitle: db 'VIDDISK E - expanded memory: a Lo-tech-style EMS board', 0
vk_s_emused:  db 'none: a frame held an option ROM', 0
vk_s_emnone:  db 'no board: E/D/C000h at 260h-26Ch, 288h', 0
vk_s_emat:    db 'EMS board at', 0
vk_s_emwhere: db '0000h, frame 0000h', 0
vk_efrms:     dw 0xE000, 0xD000, 0xC000, 0    ; EMS.DRV's frames, in order
vk_ebases:    dw 0x260, 0x264, 0x268, 0x26C, 0x288, 0     ; ...and bases
vk_eport:     dw 0                  ; where the board answered
vk_efrm:      dw 0
vk_s_embad:   db 'BAD - not STREAM.DAT', 0
vk_s_emhdr:   db '-- the board (us a row; 16 KB copies, rep movsw) --', 0
vk_s_emhdrf:  db '-- 32 KB READ_SEQ for 5 s (KB/s x 10) --', 0
vk_r_emprobe: db 'EMS probe', 0
vk_r_empages: db 'EMS pages (16 KB)', 0
vk_r_emmap:   db 'map 4 pages (4 OUTs)', 0
vk_r_emdn:    db 'frame -> RAM 16K', 0
vk_r_emup:    db 'RAM -> frame 16K', 0
vk_r_emram:   db 'RAM -> RAM 16K', 0
vk_r_emseq:   db 'READ_SEQ into RAM', 0
vk_r_emfr:    db 'READ_SEQ into frame', 0
vk_r_emfchk:  db 'frame holds the file', 0
vk_f_etxt:    db 'VDEMS.TXT', 0
vk_epages:    dw 0              ; the board's pages
vk_eframe:    db 0              ; vk_emfill: into the frame
vk_efbad:     db 0              ; ...its last chunk was not the file's
vk_efk:       dw 0              ; ...chunks read into it
vk_efmb:      dw 0              ; ...the row's first offset's high word
vk_emdone:    dw 0              ; for a harness: E has finished

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
vk_xc:        dw 0              ; the crossing rows' cylinder,
vk_xn:        dw 0              ; ...run length in sectors,
vk_tn:        dw 0              ; ...the timing rows' length,
vk_xh:        db 0              ; ...head,
vk_xs:        db 0              ; ...first sector,
vk_xgood:     db 0              ; ...and whether one call read the truth
vk_xbx:       dw 0              ; ...the buffer offset a run reads to
vk_swn:       dw 0              ; vk_msweep: shapes read,
vk_swbad:     dw 0              ; ...the wrong ones,
vk_swref:     dw 0              ; ...the refused ones,
vk_swf:       dw 0, 0, 0, 0     ; ...and the first wrong: C, H, S, n
vk_dchk:      db 0
              db 0
vk_hdir:      dw 0
vk_wk:        dw 0
vk_wmode:     db 0              ; 0 APPEND, 1 WRITE_SEQ, 2 WRITE_SEQ HELD,
                                ; 3 held unclosed, 4 held interleaved,
                                ; 5 held, the stream deleted mid-way
vk_f_side:    db 'VKSIDE.TXT', 0
vk_wtok:      dw 0              ; ...its token
vk_wflg:      db 0              ; ...and its flags
vk_wdone:     dw 0
vk_ddone:     dw 0
vk_wfrom:     dw 0
vk_fnd:       times OSAPI_FIND_SZ db 0
vk_cur:       times FSEQ_SIZE db 0
vk_cplba:     dw 0              ; C: the next sector a direct read takes
vk_cpmode:    db 0              ; ...0 rep insw, 1 in/stosw/loop
vk_cpfirst:   db 0              ; ...this sector is its command's first
vk_cpide:     db 0              ; ...the direct rows ran
vk_cpt0:      dw 0              ; ...a PIT read
vk_cpbad:     dw 0              ; ...commands that ended in ERR
vk_cpdone:    dw 0              ; for a harness: C has finished
              align 2
vk_cpx:       dw 0, 0           ; ...PIT counts: the transfers,
vk_cpw:       dw 0, 0           ; ...the waits after a command's first,
vk_cpf:       dw 0, 0           ; ...and its first
vk_lacs:      db 0x93           ; L: the CS cache's access byte (M: 9Bh)
vk_lzf:       db 0              ; ...vk_lcmp's flags
              db 0
vk_lcx:       dw 0              ; ...and its count left
vk_ladone:    dw 0              ; for a harness: L has finished
vk_la20x:     dw 0              ; ...LOADALL writes refused, A20 shut
vk_lba0:      dw 0              ; the fill's first sector
vk_lt0:       dw 0              ; ...a borrow's first PIT read
vk_lcnt:                        ; the fill's counters (vk_lzero):
vk_lnb:       dw 0              ; ...borrows,
vk_lns:       dw 0              ; ...sectors read in them,
vk_lh1:       dw 0              ; ...borrows of 1 sector,
vk_lh2:       dw 0              ; ...2-3,
vk_lh4:       dw 0              ; ...4-7,
vk_lh8:       dw 0              ; ...VK_LRUN,
vk_lmax:      dw 0              ; ...and the longest, PIT counts
VK_LCNTW    equ ($ - vk_lcnt) / 2
              LA_DATA           ; apps/os88la.inc's state
vk_res:       times VK_NRES dd 0

VK_BSS_OWN  equ 512
    OS88_BSS VK_BSS_OWN + BL_BSS_SIZE
    align 512
    OS88_IMAGE_END

    BL_BSS os88_image_end + VK_BSS_OWN
