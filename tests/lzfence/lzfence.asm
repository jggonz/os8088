; =============================================================================
; lzfence - THE GATE ON OSAPI_DECOMP's REFUSALS (SPEC.md 20.13.4)
;
; docs/plans/O88-COMPRESSION-PLAN.md 13 makes this wave 1's gate and says why it must
; exist before any consumer does: the bounds in kernel/lz.inc are measured for
; SIZE and for SPEED, and that they actually REFUSE was an assertion until
; something fed them a bad stream.
;
; Every byte off a disk is hostile (SPEC.md 19) and os88pkg.py is not a gate on
; a foreign .O88, so the decoder is the only thing between a crafted file and a
; write into a neighbour's region - which mem_claim_hi's top-down placement
; (SPEC.md 50.3) makes a resident package's CODE.
;
; **THE POSITIVE CONTROL IS THE POINT.** A decoder that refuses everything
; passes every negative case here, so `good` runs first and its twelve bytes
; are compared one by one. Without it this file would rubber-stamp a stc/ret.
;
; The streams are hand-built rather than generated, so what each one is wrong
; ABOUT is readable in the `db` beside it.
;
; **THE DESTINATION IS A CLAIM AND NOT OUR OWN SEGMENT.** SPEC.md 20.13.3 wants
; DI = 0, and offset 0 of a package's segment is its HEADER - the dispatcher
; the kernel far-calls every callback through. So the entry proc claims a
; kilobyte and decompresses into the base of that, which is what every real
; caller does too.
;
; NEVER SHIPPED. Its own scratch image, the fmtest precedent:
;   make lzfencetest && python3 tests/lzfence.py
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'LZFENCE', lf_entry

LF_WANT     equ 12              ; what `good` must produce

; -----------------------------------------------------------------------------
lf_entry:
    mov ax, 1                   ; a kilobyte to expand into
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [lf_seg], dx

    ; --- the positive control ------------------------------------------------
    mov si, lf_good
    mov cx, lf_good_len
    mov dx, LF_WANT
    call lf_run
    jc .ctlbad                  ; it refused a stream it should have taken
    push ds                     ; compare the claim's bytes with what we wanted
    mov es, [lf_seg]
    mov si, lf_want
    xor di, di
    mov cx, LF_WANT
    cld
    repe cmpsb
    pop ds
    jne .ctlbad
    mov si, lf_yes
    jmp short .ctlsay
.ctlbad:
    mov si, lf_no
.ctlsay:
    mov di, lf_r_ctl
    call lf_say

    ; --- the four refusals ---------------------------------------------------
    mov bx, lf_cases
.next:
    mov si, [bx]
    or si, si
    jz .done
    mov cx, [bx+2]
    mov dx, [bx+4]
    push bx
    call lf_run
    pop bx
    mov si, lf_yes              ; CF=1 - refused, which is the pass
    jc .say
    mov si, lf_no
.say:
    mov di, [bx+6]
    call lf_say
    add bx, 8
    jmp short .next
.done:
    mov si, lf_tpl
    call OSAPI_WM_CREATE
    jc .fail
    clc
    ret
.nomem:
.fail:
    stc
    ret

; -----------------------------------------------------------------------------
; lf_run - one call into the claim. DS:SI = stream, CX = bytes, DX = the output
;          length it claims. out: CF from OSAPI_DECOMP. ES is left ours.
; -----------------------------------------------------------------------------
lf_run:
    push si
    push cx
    push dx
    mov es, [lf_seg]            ; poison the claim first: a decoder that writes
    xor di, di                  ; NOTHING and answers CF=0 must not pass the
    mov cx, 512                 ; control on a previous case's leftovers
    mov ax, 0xCCCC
    cld
    rep stosw
    pop dx
    pop cx
    pop si
    push si
    push cx
    push dx
    mov es, [lf_seg]
    xor di, di                  ; **DI = 0**: the contract (SPEC.md 20.13.3),
    xor bx, bx                  ; and the offset bound depends on it. BX:DX is
                                ; the 32-bit expected output (SPEC.md 20.14.5)
    mov al, OSAPI_LZ_LZ4
    call OSAPI_DECOMP
    pop dx
    pop cx
    pop si
    push ds                     ; ES back to us, for the next lf_say
    pop es
    ret

; lf_say - copy the 3-byte verdict at DS:SI over the one at DS:DI
lf_say:
    push si
    push di
    push cx
    push es
    push ds
    pop es
    mov cx, 3
    cld
    rep movsb
    pop es
    pop cx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
lf_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 6
    mov si, lf_r_ctl
    mov bp, 5
.line:
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    add dx, 10
    push cx                         ; step SI past this NUL-terminated line
    xor cx, cx
.scan:
    lodsb
    or al, al
    jnz .scan
    pop cx
    dec bp
    jnz .line
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

lf_tpl:
    dw 120, 90, 280, 78
    dw lf_ttl, lf_paint, 0, 0

lf_ttl:     db 'LZ fence', 0

; -----------------------------------------------------------------------------
; the streams. Every stream begins with a word T, the length of the RAW TAIL
; it ends in (SPEC.md 20.13.7) - here 0, so the symbols run to the end. LZ4:
; token = (litlen << 4) | (matchlen - 4), then the literals, then a 2-byte
; little-endian offset, then any extended match length.
; -----------------------------------------------------------------------------
; GOOD: four literals, then an eight-byte match at offset 4 -> 'ABCD' three
; times, and the symbols end exactly where the (empty) tail begins.
lf_good:    dw 0
            db 0x44, 'A','B','C','D', 0x04, 0x00
lf_good_len equ $ - lf_good

; TRUNCATED: the same stream with its tail gone, still claiming twelve bytes.
; The offset's high byte is what is missing, so the match reads one byte past
; the symbols - and a symbol running past the tail is a refusal.
lf_trunc:   dw 0
            db 0x44, 'A','B','C','D', 0x04
lf_trunc_len equ $ - lf_trunc

; ZERO OFFSET: a match that would copy from itself for ever.
lf_zero:    dw 0
            db 0x44, 'A','B','C','D', 0x00, 0x00
lf_zero_len equ $ - lf_zero

; OFFSET PAST WHAT HAS BEEN PRODUCED: four bytes written, then a match reaching
; 64 back - below the claim's base, into whatever the heap put under it.
lf_back:    dw 0
            db 0x44, 'A','B','C','D', 0x40, 0x00
lf_back_len equ $ - lf_back

; OVERLONG MATCH: 4 + 15 + 240 = 259 bytes out of a buffer told it holds 12.
lf_long:    dw 0
            db 0x4F, 'A','B','C','D', 0x04, 0x00, 240
lf_long_len equ $ - lf_long

lf_want:    db 'A','B','C','D','A','B','C','D','A','B','C','D'

lf_cases:                       ; stream, length, claimed output, verdict slot
    dw lf_trunc, lf_trunc_len, LF_WANT, lf_r_trunc
    dw lf_zero,  lf_zero_len,  LF_WANT, lf_r_zero
    dw lf_back,  lf_back_len,  LF_WANT, lf_r_back
    dw lf_long,  lf_long_len,  LF_WANT, lf_r_long
    dw 0

lf_yes:     db 'ok '
lf_no:      db 'BAD'

; The report, read out of the image by tests/lzfence.py and drawn by lf_paint.
lf_r_ctl:   db 'xxx control', 0
lf_r_trunc: db 'xxx truncated', 0
lf_r_zero:  db 'xxx zero offset', 0
lf_r_back:  db 'xxx offset past output', 0
lf_r_long:  db 'xxx overlong match', 0

    OS88_BSS 2
    OS88_IMAGE_END
lf_seg      equ os88_image_end + 0
