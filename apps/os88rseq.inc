; =============================================================================
; os8088 - apps/os88rseq.inc - READ_AT's registers, READ_SEQ's walk
;                              (SPEC.md 18.4.8.1)
;
; OSAPI_FILE_READ_AT re-walks the file's chain from the front on EVERY call,
; so a loop that reads a file a chunk at a time pays a walk that grows with
; the offset - 142 ms a MB on a fixed disk, and CPU even on a floppy whose FAT
; is cached (docs/plans/DISK-CPU-PLAN.md 3). OSAPI_FILE_READ_SEQ walks ONE
; link a call from a 16-byte cursor the caller keeps. This is that slot
; behind READ_AT's own registers, so converting a reader is one line at its
; call site and a cursor in its bss.
;
; THE OFFSET STAYS YOURS. The helper compares DX:AX with the cursor's own
; offset and, when they differ, re-seeds the cursor there - so a seek, a
; restart, a retry after an error and a first call all need nothing from the
; caller. On the SMALL kernel, whose door answers FERR_NAME, the same call is
; made as READ_AT, and on kern_big a READ_AT refusal (FERR_NAME for a bad
; capacity) is READ_SEQ's too, so the retry costs one far call and answers
; the same.
;
; **CALL os88_rseq_new WHEN THE FILE CHANGES.** The cursor names no file: a
; valid cursor is TRUSTED for whatever name the call carries, so a cursor left
; at the offset a second file starts at would read the FIRST file's clusters
; under the second one's name. Zeroing it on every open costs nothing.
;
; Include it once, anywhere in your code. A package's bss is zeroed at load,
; so a cursor there starts cold; one anywhere else wants os88_rseq_new first.
; =============================================================================
%ifndef OS88RSEQ_INC
%define OS88RSEQ_INC

; -----------------------------------------------------------------------------
; os88_rseq - OSAPI_FILE_READ_AT, served from a cursor
; in:  SI -> NUL 8.3 name, ES:BX -> buffer, CX = capacity, DX:AX = offset
;      (READ_AT's registers and rules); DS:DI -> the 16-byte cursor
; out: READ_AT's: CF=0 DX:AX = bytes delivered (0 = the end); CF=1 AX = FERR_*
; preserves: BX, CX, SI, DI, DS, ES
; -----------------------------------------------------------------------------
os88_rseq:
    cmp ax, [di+FSEQ_OFF]
    jne .seed
    cmp dx, [di+FSEQ_OFF+2]
    je .go
.seed:
    call os88_rseq_new
    mov [di+FSEQ_OFF], ax
    mov [di+FSEQ_OFF+2], dx
.go:
    push dx                     ; the offset, for the small kernel's READ_AT
    push ax
    push es
    mov dx, es                  ; DX:BX = the buffer
    push ds
    pop es                      ; ES:DI = the cursor
    call OSAPI_FILE_READ_SEQ
    pop es
    jc .no
    add sp, 4                   ; the offset, unwanted: DX:AX is the answer
    clc
    ret
.no:
    cmp ax, FERR_NAME
    jne .fail
    pop ax                      ; the small kernel, or a refusal READ_AT will
    pop dx                      ; repeat: READ_AT at the same offset
    call OSAPI_FILE_READ_AT
    ret
.fail:
    add sp, 4
    stc
    ret

; os88_rseq_new - zero the cursor at DS:DI (the next os88_rseq re-seeds it)
; preserves: every register
os88_rseq_new:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor ax, ax
    mov cx, FSEQ_SIZE / 2
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret
%endif
