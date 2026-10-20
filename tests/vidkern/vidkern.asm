; =============================================================================
; os8088 - tests/vidkern/vidkern.asm
;
; VIDKERN: the gate for Video Player wave 2's three kernel changes
; (docs/plans/VIDEO-PLAN.md 4.1-4.3), run by tests/vidkern.py on a machine
; booted off a fixed disk. Each is a key, and each leaves its answers in
; vk_res for the harness to read:
;
;   R  FSXF_RATE (SPEC.md 53.2.2). Three calls that must be REFUSED - with
;      FSXF_FASTTICK, a divisor under FSX_RATE_MIN, a hook outside the image -
;      then a bracket at 30.0 Hz (divisor 39,773) whose hook counts the
;      periods it is handed for 150 of them. Every sixteenth call the hook
;      stis and burns ~2 periods, so the kernel must SKIP the calls that land
;      inside it and hand the periods to the next one. The periods against
;      [ticks] is 65536/39773 if the accumulator is right, and the BIOS's own
;      count at 40:6C must move with [ticks].
;   F  the progress-box fence (SPEC.md 12.8.5.2). A read that arms the widget,
;      then a SAME-MODE bracket, which must take it down at the door; a read
;      inside the bracket, which must not arm it. The package PARKS at each
;      phase so the harness can read the kernel's [fpg_on].
;   S  OSAPI_FILE_READ_SEQ (SPEC.md 18.4.8) on STREAM.DAT, whose every dword
;      holds its own offset: 8 calls of 32 KB at 0 MB and at 12 MB, timed;
;      two READ_AT calls at 12 MB beside them; the data checked; a write in
;      the middle of a run, which must re-seed silently; the end of the file;
;      and a capacity that is not a cluster multiple, which must refuse.
;
;   A  (or a click) all three for a person rather than a harness, reported on
;      the window and SAVED as VIDKERN.TXT beside the package. F's parks are
;      then two seconds each instead of waits for a harness - LOOK at the
;      screen: the progress box shows in phase 1 and in neither of the two
;      after it. S needs the gate's own STREAM.DAT (every dword its own
;      offset), which a field disk has no room for, so without it S says so
;      and is skipped; tests/viddisk.py's VIDDISK times the same reads on any
;      large file.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VIDKERN', vk_entry

VK_DIV      equ 39773               ; 1,193,182 / 30.0
VK_PER      equ 150                 ; five seconds of periods
VK_CAP      equ 32768               ; one READ_SEQ / READ_AT chunk
VK_CUR      equ VK_CAP              ; ...and the cursor right after it
VK_KB       equ 33
VK_NCALL    equ 8

vk_entry:
    push si
    call bl_blank
    mov si, vk_s_title
    call bl_sline
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

vk_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call vk_all
    call bl_paint
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vk_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bl, al
    or al, 0x20
    cmp al, 'a'
    jne .r
    call vk_all
    call bl_paint
    jmp short .out
.r:
    cmp al, 'r'
    jne .f
    call vk_rate
    jmp short .out
.f:
    cmp al, 'f'
    jne .s
    call vk_fence
    jmp short .out
.s:
    cmp al, 's'
    jne .k
    call vk_seq
    jmp short .out
.k:
    mov al, bl                      ; the report's own keys: scrolling
    call bl_key
    jc .out
    call bl_paint
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- R: FSXF_RATE -------------------------------------------------------------
vk_rate:
    xor bp, bp                      ; BP = the refusal bits
    mov bx, [vk_win]
    mov ax, vk_rate_main
    mov cx, FSXF_RATE + FSXF_FASTTICK
    mov dx, VK_DIV
    mov di, vk_hook
    call OSAPI_FSX_RUN              ; one channel 0: refused
    jnc .r1
    or bp, 1
.r1:
    mov cx, FSXF_RATE
    mov dx, FSX_RATE_MIN - 1
    call OSAPI_FSX_RUN              ; too fast: refused
    jnc .r2
    or bp, 2
.r2:
    mov dx, VK_DIV
    mov di, 0xFFF0
    call OSAPI_FSX_RUN              ; the hook outside the image: refused
    jnc .r3
    or bp, 4
.r3:
    mov [vk_res+0], bp
    mov di, vk_hook
    call OSAPI_FSX_RUN              ; ...and the real one
    sbb ax, ax
    mov [vk_res+2], ax
    mov word [vk_rdone], 1
    ret

; the bracket: SI = the window, DS = CS = ours, ES = KERNEL_SEG
vk_rate_main:
    call OSAPI_GET_TICKS
    mov [vk_t0], ax
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    pop es
    mov [vk_b0], ax
    mov byte [vk_ready], 1
.wait:
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    cmp word [vk_res+6], VK_PER     ; the periods the hook was handed
    jb .wait
    pushf
    cli
    mov byte [vk_ready], 0
    popf
    call OSAPI_GET_TICKS
    sub ax, [vk_t0]
    mov [vk_res+12], ax
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    pop es
    sub ax, [vk_b0]
    mov [vk_res+14], ax
    ret

; the hook: AX = the periods since it last ran. IF=0, and it may sti
vk_hook:
    cmp byte [vk_ready], 0
    je .out
    inc word [vk_res+4]             ; calls
    add [vk_res+6], ax              ; periods (dword)
    adc word [vk_res+8], 0
    cmp ax, [vk_res+10]
    jbe .max
    mov [vk_res+10], ax             ; the most handed to one call
.max:
    test word [vk_res+4], 15
    jnz .out
    sti                             ; ...and every 16th call runs long with
    mov cx, 20000                   ; interrupts ON: ~340K cycles, two
    loop $                          ; periods, so the next IRQ0 lands inside
    cli                             ; it and must be skipped and counted
.out:
    ret

; --- F: the progress-box fence --------------------------------------------------
vk_fence:
    call vk_claim
    jc .out
    call vk_fread                   ; arms the widget: 16 sectors in THIS lock
    mov al, 1                       ; hold (the harness checks that it did -
    call vk_park                    ; the control)
    mov bx, [vk_win]
    mov ax, vk_fence_main
    xor cx, cx                      ; a SAME-MODE bracket: no fsx_mode, ever
    call OSAPI_FSX_RUN
    sbb ax, ax
    mov [vk_res+16], ax
.out:
    mov word [vk_fdone], 1
    ret

vk_fence_main:
    mov al, 2                       ; the door must have taken it down
    call vk_park
    call vk_fread                   ; ...and a read in here must not arm it
    mov al, 3
    call vk_park
    ret

vk_fread:
    push es
    mov es, [vk_seg]
    xor bx, bx
    mov si, vk_f_fence
    mov cx, 16384
    xor dx, dx
    call OSAPI_FILE_READ
    pop es
    ret

; vk_park - AL = the phase: publish it, and spin until the harness says go -
; or, run by a person ([vk_auto]), for two seconds, so the box can be SEEN
vk_park:
    mov [vk_phase], al
    cmp byte [vk_auto], 0
    jne .timed
.spin:
    cmp al, [vk_go]
    jne .spin
    ret
.timed:
    push ax
    push dx
    call OSAPI_GET_TICKS
    mov dx, ax
.t:
    call OSAPI_GET_TICKS
    sub ax, dx
    cmp ax, 36
    jb .t
    pop dx
    pop ax
    ret

; --- S: OSAPI_FILE_READ_SEQ -------------------------------------------------------
vk_seq:
    call vk_claim
    jc .out
    call vk_isgate
    jc .out
    call OSAPI_FILE_DFREE           ; BX = SECTORS per cluster
    mov cl, 9
    shl bx, cl
    mov [vk_res+36], bx             ; ...in bytes
    mov es, [vk_seg]

    call vk_i13on                   ; count the fixed disk's int 13h calls
    mov byte [vk_nochk], 1          ; A and B read only: what is timed is
    xor ax, ax                      ; the call. 8 calls from 0 MB, the first
    xor dx, dx                      ; seeding...
    call vk_seek
    call vk_seq1                    ; (the seeding call, outside the counts)
    call vk_i13zero
    call vk_seqrow
    mov [vk_res+18], ax
    mov ax, [vk_i13n]
    mov [vk_res+42], ax
    mov ax, [vk_i13lo]
    mov [vk_res+44], ax
    xor ax, ax                      ; B: ...and 8 at 12 MB, after a SEEK -
    mov dx, 12 * 16                 ; whose first call walks the chain from
    call vk_seek                    ; the front once, as READ_AT does every
    call OSAPI_GET_TICKS            ; time: timed on its own
    push ax
    call vk_seq1
    call OSAPI_GET_TICKS
    pop dx
    sub ax, dx
    mov [vk_res+50], ax
    call vk_i13zero
    call vk_seqrow
    mov [vk_res+20], ax
    mov ax, [vk_i13n]
    mov [vk_res+46], ax
    mov ax, [vk_i13lo]
    mov [vk_res+48], ax
    call vk_i13off
    mov byte [vk_nochk], 0
    xor ax, ax                      ; ...and the same two runs CHECKED, every
    xor dx, dx                      ; byte the one at its offset
    call vk_seek
    call vk_seqrow
    xor ax, ax
    mov dx, 12 * 16
    call vk_seek
    call vk_seqrow

    xor ax, ax                      ; F: READ_AT at 12 MB, twice, the same
    mov dx, 12 * 16                 ; 32 KB it would read
    mov [vk_coff], ax
    mov [vk_coff+2], dx
    call OSAPI_GET_TICKS
    mov bp, ax
    mov cx, 2
.rat:
    push cx
    mov ax, [vk_coff]
    mov dx, [vk_coff+2]
    xor bx, bx
    mov cx, VK_CAP
    mov si, vk_f_stream
    call OSAPI_FILE_READ_AT
    jnc .ratok
    inc word [vk_res+34]
.ratok:
    pop cx
    loop .rat
    call OSAPI_GET_TICKS
    sub ax, bp
    mov [vk_res+22], ax

    ; C: a write in the middle of a run. 1 MB in, two calls, a temporary
    ; file written and deleted (each bumps the mount generation), then two
    ; more - every byte still the one at its offset
    xor ax, ax
    mov dx, 16
    call vk_seek
    call vk_seq1
    call vk_seq1
    push es
    push ds
    pop es
    xor bx, bx
    xor dx, dx
    mov cx, 512
    mov si, vk_f_tmp
    call OSAPI_FILE_WRITE
    pop es
    jnc .wrote
    inc word [vk_res+34]
.wrote:
    call vk_seq1
    mov si, vk_f_tmp
    call OSAPI_FILE_DELETE
    call vk_seq1
    mov word [vk_res+32], 1         ; C ran; vk_res+24 counts its bad chunks

    ; D: the end. Seek to the last cluster boundary under the size: the
    ; tail arrives whole, and the next call answers 0
    mov ax, VK_SIZE & 0xFFFF
    mov dx, VK_SIZE >> 16
    mov cx, [vk_res+36]
    dec cx
    not cx
    and ax, cx
    call vk_seek
    call vk_seq1
    mov [vk_res+26], ax
    call vk_seq1
    mov [vk_res+28], ax

    ; E: a capacity that is not a cluster multiple must refuse, FERR_NAME
    xor ax, ax
    xor dx, dx
    call vk_seek
    xor bx, bx
    mov cx, 1000
    mov di, VK_CUR
    mov si, vk_f_stream
    mov dx, es                      ; the buffer: DX:BX (the cursor is ES:DI)
    call OSAPI_FILE_READ_SEQ
    jnc .e
    mov [vk_res+30], ax             ; AX = FERR_* when refused (else 0)
.e:
.out:
    mov word [vk_sdone], 1
    ret

; vk_seek - DX:AX = an offset: zero the cursor and set it (the SDK's rule)
vk_seek:
    push di
    push cx
    push ax
    mov di, VK_CUR
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    pop ax
    mov [es:VK_CUR+FSEQ_OFF], ax
    mov [es:VK_CUR+FSEQ_OFF+2], dx
    pop cx
    pop di
    ret

; vk_seqrow - VK_NCALL calls, only the calls timed; out AX = the ticks
vk_seqrow:
    push bp
    push cx
    xor bp, bp
    mov cx, VK_NCALL
.l:
    push cx
    call OSAPI_GET_TICKS
    push ax
    call vk_seq1
    call OSAPI_GET_TICKS
    pop cx
    sub ax, cx
    add bp, ax
    pop cx
    loop .l
    mov ax, bp
    pop cx
    pop bp
    ret

; vk_seq1 - one READ_SEQ of 32 KB, and its bytes checked - OUTSIDE the
; caller's timing, which brackets vk_seq1 whole; so the check is timed too,
; the same ~50 ms on every row. Out AX = the bytes delivered
vk_seq1:
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [es:VK_CUR+FSEQ_OFF]    ; where this chunk starts, for the check
    mov [vk_coff], ax
    mov ax, [es:VK_CUR+FSEQ_OFF+2]
    mov [vk_coff+2], ax
    xor bx, bx
    mov cx, VK_CAP
    mov di, VK_CUR
    mov si, vk_f_stream
    mov dx, es                      ; the buffer: DX:BX (the cursor is ES:DI)
    call OSAPI_FILE_READ_SEQ
    jnc .ok
    inc word [vk_res+34]
    xor ax, ax
    jmp short .out
.ok:
    push ax
    cmp byte [vk_nochk], 0          ; a timing row reads only (the check runs
    jne .chk                        ; on its own rows, at the same offsets)
    mov cx, ax                      ; every dword holds its own offset
    shr cx, 1
    shr cx, 1
    jcxz .chk
    xor bx, bx
    mov si, [vk_coff]
    mov di, [vk_coff+2]
.l:
    cmp [es:bx], si
    jne .bad
    cmp [es:bx+2], di
    jne .bad
    add si, 4
    adc di, 0
    add bx, 4
    loop .l
    jmp short .chk
.bad:
    inc word [vk_res+24]
.chk:
    pop ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- an int 13h counter on unit 80h, for the timing rows ------------------------
vk_i13on:
    push ax
    push es
    xor ax, ax
    mov es, ax
    cli
    mov ax, [es:0x13*4]
    mov [vk_i13old], ax
    mov ax, [es:0x13*4+2]
    mov [vk_i13old+2], ax
    mov word [es:0x13*4], vk_i13
    mov [es:0x13*4+2], cs
    sti
    pop es
    pop ax
    ret
vk_i13off:
    push ax
    push es
    xor ax, ax
    mov es, ax
    cli
    mov ax, [vk_i13old]
    mov [es:0x13*4], ax
    mov ax, [vk_i13old+2]
    mov [es:0x13*4+2], ax
    sti
    pop es
    pop ax
    ret
vk_i13zero:
    mov word [vk_i13n], 0
    mov word [vk_i13lo], 0
    ret
vk_i13:
    cmp dl, 0x80
    jne .chain
    inc word [cs:vk_i13n]
    test cl, 0xC0                   ; cylinder bits 8-9
    jnz .chain
    cmp ch, 16                      ; ...and under 16: the FAT and the root
    jae .chain
    inc word [cs:vk_i13lo]
.chain:
    jmp far [cs:vk_i13old]

; vk_isgate - is STREAM.DAT the gate's own, every dword its offset? A READ_AT
; of the first cluster at 12 MB answers it. CF=1 no ([vk_nogate] = 1)
vk_isgate:
    push es
    mov byte [vk_nogate], 1
    mov es, [vk_seg]
    xor bx, bx
    mov cx, 4096
    xor ax, ax
    mov dx, 12 * 16
    mov si, vk_f_stream
    call OSAPI_FILE_READ_AT
    jc .no
    or ax, ax
    jz .no
    cmp word [es:4], 4
    jne .no
    cmp word [es:6], 12 * 16
    jne .no
    mov byte [vk_nogate], 0
    pop es
    clc
    ret
.no:
    pop es
    stc
    ret

; --- A: all three, reported and saved -------------------------------------------
vk_all:
    push ds
    pop es
    mov di, vk_res
    xor ax, ax
    mov cx, 28
    cld
    rep stosw
    mov word [bl_nrow], 0
    mov si, vk_s_title
    call bl_sline
    mov byte [vk_auto], 1
    call vk_rate
    call vk_fence
    call vk_seq
    mov byte [vk_auto], 0
    push ds
    pop es

    mov si, vk_s_hr
    call bl_sline
    mov ax, [vk_res+0]              ; three refusals, one bit each
    xor cx, cx
.bits:
    shr ax, 1
    adc cx, 0
    or ax, ax
    jnz .bits
    mov ax, cx
    mov si, vk_r_ref
    call vk_kv0
    mov ax, [vk_res+2]
    neg ax
    mov si, vk_r_rcf
    call vk_kv0
    mov ax, [vk_res+4]
    mov si, vk_r_calls
    call vk_kv0
    mov ax, [vk_res+6]
    mov dx, [vk_res+8]
    mov si, vk_r_per
    mov cx, 9
    call bl_kv
    mov ax, [vk_res+10]
    mov si, vk_r_max
    call vk_kv0
    mov ax, [vk_res+12]
    mov si, vk_r_tk
    call vk_kv0
    mov ax, [vk_res+14]
    mov si, vk_r_bios
    call vk_kv0
    mov cx, [vk_res+12]             ; periods a tick x 10,000
    jcxz .noper
    mov ax, [vk_res+6]
    mov bx, 10000
    mul bx
    div cx
    mov si, vk_r_ppt
    call vk_kv0
.noper:
    mov si, vk_s_hf
    call bl_sline
    mov si, vk_s_hf2
    call bl_sline
    mov ax, [vk_res+16]
    neg ax
    mov si, vk_r_fcf
    call vk_kv0

    mov si, vk_s_hs
    call bl_sline
    cmp byte [vk_nogate], 0
    je .s
    mov si, vk_s_nos
    call bl_sline
    jmp .save
.s:
    mov ax, [vk_res+18]
    mov si, vk_r_s0
    call vk_kv0
    mov ax, [vk_res+50]
    mov si, vk_r_sk
    call vk_kv0
    mov ax, [vk_res+20]
    mov si, vk_r_s12
    call vk_kv0
    mov ax, [vk_res+22]
    mov si, vk_r_rat
    call vk_kv0
    mov ax, [vk_res+42]
    mov si, vk_r_i0
    call vk_kv0
    mov ax, [vk_res+46]
    mov si, vk_r_i12
    call vk_kv0
    mov ax, [vk_res+48]
    mov si, vk_r_ilo
    call vk_kv0
    mov ax, [vk_res+24]
    mov si, vk_r_bad
    call vk_kv0
    mov ax, [vk_res+26]
    mov si, vk_r_tail
    call vk_kv0
    mov ax, [vk_res+28]
    mov si, vk_r_end
    call vk_kv0
    mov ax, [vk_res+30]
    mov si, vk_r_eref
    call vk_kv0
    mov ax, [vk_res+34]
    mov si, vk_r_err
    call vk_kv0
.save:
    push ds
    pop es
    mov si, vk_f_txt
    call bl_save
    ret

; vk_kv0 - SI = label, AX = a 16-bit value: a report line
vk_kv0:
    push cx
    push dx
    xor dx, dx
    mov cx, 9
    call bl_kv
    pop dx
    pop cx
    ret

vk_claim:
    cmp word [vk_seg], 0
    jne .have
    mov ax, VK_KB
    call OSAPI_MEM_CLAIM
    jc .out
    mov [vk_seg], dx
.have:
    clc
.out:
    ret

VK_SIZE     equ 13212000            ; tests/vidkern.py's STREAM.DAT

%define BL_ARENA_BYTES 3000
%include "benchlib.inc"

vk_tpl:
    dw 7, 22, 632, 300
    dw vk_ttl, vk_paint, vk_onkey, vk_onclick

vk_ttl:       db 'Video Kernel Gate', 0
vk_f_stream:  db 'STREAM.DAT', 0
vk_f_fence:   db 'FENCE.DAT', 0
vk_f_tmp:     db 'VKTMP.DAT', 0
vk_f_txt:     db 'VIDKERN.TXT', 0
vk_s_title:   db 'VIDKERN - Video Player wave 2: the rate, the fence, READ_SEQ', 0
vk_s_hint:    db 'Click, or press A: ~30 s, then VIDKERN.TXT is saved here.', 0
vk_s_hr:      db '-- R: FSXF_RATE, a 30.0 Hz hook for 150 periods --', 0
vk_s_hf:      db '-- F: the progress-box fence, three 2 s parks --', 0
vk_s_hf2:     db '   LOOK: the box shows in the first park only', 0
vk_s_hs:      db '-- S: READ_SEQ on the gate STREAM.DAT (ticks) --', 0
vk_s_nos:     db 'no gate STREAM.DAT here: S skipped (VIDDISK times it)', 0
vk_r_ref:     db 'bad calls refused (3)', 0
vk_r_rcf:     db 'rate bracket refused', 0
vk_r_calls:   db 'hook calls', 0
vk_r_per:     db 'periods handed', 0
vk_r_max:     db 'most in one call (>1)', 0
vk_r_tk:      db 'ticks elapsed', 0
vk_r_bios:    db 'BIOS 40:6C moved', 0
vk_r_ppt:     db 'periods/tick x10000', 0
vk_r_fcf:     db 'fence bracket refused', 0
vk_r_s0:      db '8 x 32K @0 MB', 0
vk_r_sk:      db 'seek to 12 MB, 1st', 0
vk_r_s12:     db '8 x 32K @12 MB', 0
vk_r_rat:     db '2 x READ_AT @12 MB', 0
vk_r_i0:      db 'int13 calls @0 MB', 0
vk_r_i12:     db 'int13 calls @12 MB', 0
vk_r_ilo:     db '...under cylinder 16', 0
vk_r_bad:     db 'chunks with bad data', 0
vk_r_tail:    db 'tail bytes', 0
vk_r_end:     db 'bytes past the end', 0
vk_r_eref:    db 'odd capacity refusal', 0
vk_r_err:     db 'errors', 0

vk_win:       dw 0
vk_seg:       dw 0
vk_t0:        dw 0
vk_b0:        dw 0
vk_coff:      dw 0, 0
vk_ready:     db 0
vk_phase:     db 0
vk_go:        db 0
vk_nochk:     db 0
vk_auto:      db 0
vk_nogate:    db 0
vk_i13old:    dw 0, 0
vk_i13n:      dw 0
vk_i13lo:     dw 0
vk_rdone:     dw 0
vk_fdone:     dw 0
vk_sdone:     dw 0
vk_res:       times 28 dw 0

    OS88_BSS BL_BSS_SIZE
    align 512
    OS88_IMAGE_END

    BL_BSS os88_image_end
