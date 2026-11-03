; =============================================================================
; os8088 - tests/memp/memp.asm
;
; MEMP, the gate package for the heap's granule (SPEC.md 50.2.1) and its
; paragraph door (SPEC.md 50.3.7). Never ships: `make memp360` puts it on a
; scratch 360KB image of its own, and tests/memp.py launches it and reads its
; verdicts out of its bss.
;
; WHAT IT ASKS, in the entry proc - where a package sizes itself, and where
; mem_own answers from the claim map alone (SPEC.md 50.3 rule 1):
;
;   1  MEMP_AVAIL says the granule (CX = 32) and a free run
;   2  a claim of ONE paragraph is granted a whole granule, on a granule
;   3  33 paragraphs are granted two granules
;   4  MEMP_CLAIM_HI: 300 -> 320, from the top, aligned
;   5  MEMP_REGROW to 100 paragraphs answers 128 granted
;   6  a claim of nothing is refused
;   7  a claim with a 288-paragraph DMA head keeps the head in one 64KB page
;   8  the KB door still answers a claim of 4KB - 256 paragraphs, whose LOW
;      BYTE is zero: the granule mask was `and al` for one commit and refused
;      exactly these (every multiple of 4KB) as a claim of nothing
;   9  ...and of 16KB, the same shape a size up
;  10  MEMP_CPU is accepted, and on a 512-byte granule its base is aligned too
;  11  every claim given back, the largest run is what it was before
;
; Each check writes 1 (passed) or 2 (failed) at its index of mp_res; 0 is a
; check that never ran. Prefix mp_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEMP', mp_entry

MP_N        equ 11

mp_entry:
    push si
    mov si, mp_tpl
    call OSAPI_WM_CREATE            ; BX = the window: it is how the row finds
    jc .out                         ; this instance's segment (W_SEG)
    push bx                         ; ...and the loader SHOWS the window BX
    call mp_suite                   ; names when the entry returns: the suite
    pop bx                          ; spends BX on every verb
    mov word [mp_magic], 'MP'       ; LAST: the row waits on this word
.out:
    pop si
    clc
    ret

; MP_OK n / MP_BAD n - record check n's verdict
%macro MP_OK 1
    mov byte [mp_res + %1], 1
%endmacro
%macro MP_BAD 1
    mov byte [mp_res + %1], 2
%endmacro

mp_suite:
    ; --- 1. the granule, and a run to claim from ---------------------------
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P
    mov [mp_gran], cx
    mov [mp_big0], ax
    cmp cx, 32
    jne .f1
    or ax, ax
    jz .f1
    cmp bx, ax
    jb .f1
    MP_OK 1
    jmp short .t2
.f1:
    MP_BAD 1
    ; --- 2. one paragraph is a whole granule, on a granule ------------------
.t2:
    mov ax, 1
    xor cx, cx
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    jc .f2
    mov [mp_c1], dx
    mov [mp_g2], ax
    cmp ax, 32
    jne .f2
    test dl, 31
    jnz .f2
    MP_OK 2
    jmp short .t3
.f2:
    MP_BAD 2
    ; --- 3. 33 paragraphs are two granules ----------------------------------
.t3:
    mov ax, 33
    xor cx, cx
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    jc .f3
    mov [mp_g3], ax
    call OSAPI_MEM_FREE             ; (DX = the claim)
    cmp word [mp_g3], 64
    jne .f3
    MP_OK 3
    jmp short .t4
.f3:
    MP_BAD 3
    ; --- 4. from the top, aligned ------------------------------------------
.t4:
    mov ax, 300
    xor cx, cx
    mov bl, MEMP_CLAIM_HI
    call OSAPI_MEM_P
    jc .f4
    mov [mp_g4], ax
    mov [mp_c4], dx
    call OSAPI_MEM_FREE
    cmp word [mp_g4], 320
    jne .f4
    test byte [mp_c4], 31
    jnz .f4
    MP_OK 4
    jmp short .t5
.f4:
    MP_BAD 4
    ; --- 5. a regrow answers what it granted --------------------------------
.t5:
    mov dx, [mp_c1]
    or dx, dx
    jz .f5
    mov ax, 100
    mov bl, MEMP_REGROW
    call OSAPI_MEM_P
    jc .f5
    mov [mp_c1], dx                 ; ALWAYS take the answer: it may move
    mov [mp_g5], ax
    cmp ax, 128
    jne .f5
    MP_OK 5
    jmp short .t6
.f5:
    MP_BAD 5
    ; --- 6. nothing is refused ----------------------------------------------
.t6:
    xor ax, ax
    xor cx, cx
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    jnc .f6
    MP_OK 6
    jmp short .t7
.f6:
    call OSAPI_MEM_FREE
    MP_BAD 6
    ; --- 7. a DMA head stays in one 64KB page ------------------------------
.t7:
    mov ax, 288
    mov cx, ax
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    jc .f7
    mov [mp_c7], dx
    call OSAPI_MEM_FREE
    mov ax, [mp_c7]
    and ax, 0x0FFF                  ; paragraphs into its 64KB physical page
    add ax, 288
    cmp ax, 0x1000
    ja .f7
    MP_OK 7
    jmp short .t8
.f7:
    MP_BAD 7
    ; --- 8 and 9. the KB door at 4KB and 16KB --------------------------------
.t8:
    mov ax, 4
    call OSAPI_MEM_CLAIM
    jc .f8
    call OSAPI_MEM_FREE
    MP_OK 8
    jmp short .t9
.f8:
    MP_BAD 8
.t9:
    mov ax, 16
    call OSAPI_MEM_CLAIM
    jc .f9
    call OSAPI_MEM_FREE
    MP_OK 9
    jmp short .t10
.f9:
    MP_BAD 9
    ; --- 10. a CPU-only claim -------------------------------------------------
.t10:
    mov ax, 40
    xor cx, cx
    mov bl, MEMP_CLAIM | MEMP_CPU
    call OSAPI_MEM_P
    jc .f10
    mov [mp_g10], ax
    mov [mp_c10], dx
    call OSAPI_MEM_FREE
    mov ax, [mp_gran]               ; granted to the GRANULE...
    dec ax
    test [mp_g10], ax
    jnz .f10
    cmp word [mp_gran], 32          ; ...and at 512 every base is aligned
    jb .ok10
    test byte [mp_c10], 31
    jnz .f10
.ok10:
    MP_OK 10
    jmp short .t11
.f10:
    MP_BAD 10
    ; --- 11. everything back ------------------------------------------------
.t11:
    mov dx, [mp_c1]
    or dx, dx
    jz .nofree
    call OSAPI_MEM_FREE
.nofree:
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P
    mov [mp_big1], ax
    cmp ax, [mp_big0]
    jb .f11
    MP_OK 11
    ret
.f11:
    MP_BAD 11
    ret

mp_paint:
    ret                             ; W_PAINT arrives white-filled: nothing to
                                    ; draw, the row reads the bss

mp_tpl:
    dw 40, 40, 120, 40
    dw mp_ttl, mp_paint, 0, 0
mp_ttl:     db 'MEMP', 0

    OS88_BSS 64
    OS88_IMAGE_END

; --- loader-zeroed bss: tests/memp.py reads it at W_SEG:image ----------------
mp_magic    equ os88_image_end + 0      ; word: 'MP' once the suite has run
mp_res      equ os88_image_end + 2      ; MP_N+1 bytes: [n] = check n's verdict
mp_gran     equ os88_image_end + 16     ; word: what MEMP_AVAIL said the granule is
mp_big0     equ os88_image_end + 18     ; word: the largest run before
mp_big1     equ os88_image_end + 20     ; word: ...and after
mp_g2       equ os88_image_end + 22     ; words: what each claim was granted
mp_g3       equ os88_image_end + 24
mp_g4       equ os88_image_end + 26
mp_g5       equ os88_image_end + 28
mp_g10      equ os88_image_end + 30
mp_c1       equ os88_image_end + 32     ; words: the claims' bases
mp_c4       equ os88_image_end + 34
mp_c7       equ os88_image_end + 36
mp_c10      equ os88_image_end + 38
