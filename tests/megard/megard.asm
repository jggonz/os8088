; =============================================================================
; os8088 - tests/megard/megard.asm
;
; MEGARD, a TEST instrument (nothing under tests/ ships): one big file read
; through the file layer, on command, into a POOL block above 1MB or into a
; conventional claim, timed - for tests/hdbounce.py (KERN-MEGA-PLAN 14.167),
; which arms its breakpoints first and counts what each read cost the disk.
;
; OS88_HIGHMEM and not OS88_HIGH: under CPU_F_PM its 64KB OSAPI_MEM_CLAIM is
; a pool block - the shape PiXEL's, Audio's and the typeface buffers' claims
; take - and its own region stays low, so the row finds these words through
; the instance table's I_SPTR as a plain segment.
;
; The protocol is four bytes the row reads and writes in this image:
;   mr_state  1 once both claims are held (0xEE: a claim was refused)
;   mr_cmd    the row writes 1 (read into the pool block) or 2 (into the
;             conventional claim); 3 SQUEEZES the conventional heap - 8KB
;             claims until it refuses, then all freed - so every purgeable
;             claim the kernel holds is shed (the read-ahead's landing
;             among them); 0FFh frees both and ends the instance
;   mr_seq    +1 after each read, with mr_cf/mr_err/mr_len/mr_t/mr_sum set
; mr_t is the read's span in 838 ns PIT counts (apps/os88pit.inc); mr_sum the
; 16-bit sum of the bytes delivered, which the row compares with the file's.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARD', mr_entry, 0, OS88_HIGHMEM

mr_entry:
    mov ax, 64
    MEMKB_CLAIM                 ; OS88_HIGHMEM: a pool selector in protected
    jc .no                      ; mode, a segment everywhere else
    mov [mr_hi], dx
    mov ax, 64 * 64             ; 64KB in paragraphs, CONVENTIONAL on every
    mov cx, MEMP_CONV           ; kernel (WaveRack's wo_ioclaim)
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    jc .no
    mov [mr_lo], dx
    mov byte [mr_state], 1
.wait:
    call OSAPI_TASK_YIELD
    mov al, [mr_cmd]
    or al, al
    jz .wait
    cmp al, 0xFF
    je .quit
    mov byte [mr_cmd], 0
    cmp al, 3
    jne .read
    call mr_squeeze
    inc byte [mr_seq]
    jmp short .wait
.read:
    mov dx, [mr_hi]
    cmp al, 1
    je .go
    mov dx, [mr_lo]
.go:
    push es
    mov es, dx
    call pit_now
    mov [mr_t], ax
    mov [mr_t+2], dx
    mov si, mr_name
    xor bx, bx
    mov dx, 1                   ; DX:CX = 64KB of capacity
    xor cx, cx
    call OSAPI_FILE_READ
    mov byte [mr_cf], 0
    jnc .ok
    mov byte [mr_cf], 1
    mov [mr_err], ax
    xor ax, ax
.ok:
    mov [mr_len], ax
    call pit_now
    sub ax, [mr_t]
    sbb dx, [mr_t+2]
    mov [mr_t], ax
    mov [mr_t+2], dx
    mov cx, [mr_len]            ; the sum of what arrived
    xor si, si
    xor dx, dx
    jcxz .summed
.sum:
    mov al, [es:si]
    xor ah, ah
    add dx, ax
    inc si
    loop .sum
.summed:
    mov [mr_sum], dx
    pop es
    inc byte [mr_seq]
    jmp .wait
.quit:
    mov dx, [mr_lo]
    call OSAPI_MEM_FREE
.no:
    mov byte [mr_state], 0xEE
    mov dx, [mr_hi]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
.out:
    stc                         ; no window: the instance ends here
    ret

; mr_squeeze - conventional 8KB claims until the heap refuses (which sheds
; every purgeable claim on the way), then every one freed. Clobbers AX..DX, SI
mr_squeeze:
    mov si, mr_sq
.c:
    cmp si, mr_sq + 2 * MR_SQN
    jae .free
    mov ax, 8 * 64
    mov cx, MEMP_CONV
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    jc .free
    mov [si], dx
    inc si
    inc si
    jmp short .c
.free:
    cmp si, mr_sq
    je .out
    dec si
    dec si
    mov dx, [si]
    call OSAPI_MEM_FREE
    jmp short .free
.out:
    ret

MR_SQN      equ 96              ; 768KB: more than any conventional heap

%include "os88pit.inc"

mr_name:    db 'BIG.DAT', 0
mr_state:   db 0
mr_cmd:     db 0
mr_seq:     db 0
mr_cf:      db 0
mr_err:     dw 0
mr_len:     dw 0
mr_sum:     dw 0
mr_t:       dd 0
mr_hi:      dw 0
mr_lo:      dw 0

    OS88_BSS 2 * MR_SQN
    OS88_IMAGE_END

mr_sq       equ os88_image_end
