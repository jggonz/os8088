; Far numeric and string-array storage for the Speedy Basic runtime.
; Every public entry is an ordinary SmallerC near-call function.  The core
; sees only integer cell indices, never far pointers.

section .bss
sbm_lo_seg:     resw 1
sbm_hi_seg:     resw 1
sbm_str_seg:    resw 1
sbm_kind_seg:   resw 1
sbm_used:       resw 1
sbm_str_used:   resw 1
sbm_plane_kb:   resw 1
sbm_lo_kb:      resw 1
sbm_new_kb:     resw 1

section .text

; int sbm_init(void) -- claims are lazy so graphics-only programs pay none.
_sbm_init:
    mov word [sbm_lo_seg], 0
    mov word [sbm_hi_seg], 0
    mov word [sbm_str_seg], 0
    mov word [sbm_kind_seg], 0
    mov word [sbm_used], 0
    mov word [sbm_str_used], 0
    mov word [sbm_plane_kb], 0
    mov word [sbm_lo_kb], 0
    xor ax, ax
    ret

_sbm_reset:
    mov ax, [sbm_lo_seg]
    test ax, ax
    jz .reset_hi
    push ax
    call _os88_mem_free
    add sp, 2
.reset_hi:
    mov ax, [sbm_hi_seg]
    test ax, ax
    jz .reset_str
    push ax
    call _os88_mem_free
    add sp, 2
.reset_str:
    mov ax, [sbm_str_seg]
    test ax, ax
    jz .reset_kind
    push ax
    call _os88_mem_free
    add sp, 2
.reset_kind:
    mov ax, [sbm_kind_seg]
    test ax, ax
    jz .reset_done
    push ax
    call _os88_mem_free
    add sp, 2
.reset_done:
    mov word [sbm_lo_seg], 0
    mov word [sbm_hi_seg], 0
    mov word [sbm_str_seg], 0
    mov word [sbm_kind_seg], 0
    mov word [sbm_used], 0
    mov word [sbm_str_used], 0
    mov word [sbm_plane_kb], 0
    mov word [sbm_lo_kb], 0
    ret

_sbm_lo_segment:
    mov ax, [sbm_lo_seg]
    ret

; Claim the optional high-word store at the low plane's current size and seed
; every live word.  AX returns its segment, or zero on allocation failure.
sbm_claim_hi:
    cmp word [sbm_hi_seg], 0
    jne .hi_ok
    push ds
    push es
    push si
    push di
    mov ax, [sbm_plane_kb]
    push ax
    call _os88_mem_claim
    add sp, 2
    mov [sbm_hi_seg], ax
    test ax, ax
    jz .hi_restore
    mov es, ax
    mov ds, [sbm_lo_seg]
    xor si, si
    xor di, di
    mov cx, [cs:sbm_used]
.hi_seed:
    jcxz .hi_restore
    lodsw
    cwd
    mov ax, dx
    stosw
    loop .hi_seed
.hi_restore:
    pop di
    pop si
    pop es
    pop ds
.hi_ok:
    mov ax, [sbm_hi_seg]
    ret

; Ensure that both numeric planes have room for AX cells.  Claims grow in 4K
; chunks (2,048 cells), keeping the low plane contiguous for graphics GET/PUT.
; MEM_REGROW extends in place using only the delta when possible, or moves and
; copies inside the kernel.  Its returned segment must replace the old base.
;
; If live-high growth fails after low succeeds, sbm_used and sbm_plane_kb stay
; unchanged.  sbm_lo_kb records the harmless physical head start so a retry
; grows only high.  Existing cells stay valid throughout.
; Returns AX=0 on success or -1 on failure.
sbm_ensure:
    push bx
    push dx
    mov bx, [sbm_plane_kb]
    mov cl, 9
    shl bx, cl                       ; current cells = KB * 512
    cmp ax, bx
    jbe .ensure_ok
    add ax, 2047
    and ax, 0xf800                   ; round cells to a 2,048-cell chunk
    mov cl, 9
    shr ax, cl                       ; claim size in KB: 4, 8, ... 32
    mov [sbm_new_kb], ax
    cmp ax, [sbm_lo_kb]
    jbe .ensure_high
    mov dx, [sbm_lo_seg]
    test dx, dx
    jnz .ensure_regrow_low
    push ax
    call _os88_mem_claim
    add sp, 2
    jmp short .ensure_low_answer
.ensure_regrow_low:
    push ax                            ; new KB, then old segment
    push dx
    call _os88_mem_regrow
    add sp, 4
.ensure_low_answer:
    test ax, ax
    jz .ensure_bad
    mov [sbm_lo_seg], ax
    mov ax, [sbm_new_kb]
    mov [sbm_lo_kb], ax
.ensure_high:
    mov dx, [sbm_hi_seg]
    test dx, dx
    jz .ensure_publish
    mov ax, [sbm_new_kb]
    push ax                            ; new KB, then old segment
    push dx
    call _os88_mem_regrow
    add sp, 4
    test ax, ax
    jz .ensure_bad
    mov [sbm_hi_seg], ax
.ensure_publish:
    mov ax, [sbm_new_kb]
    mov [sbm_plane_kb], ax
.ensure_ok:
    xor ax, ax
    jmp short .ensure_done
.ensure_bad:
    mov ax, -1
.ensure_done:
    pop dx
    pop bx
    ret

; int sbm_make(unsigned count) -- zeroed contiguous cells, or -1.
_sbm_make:
    push bp
    mov bp, sp
    push es
    push di
    push bx
    cmp word [bp+4], 0
    jne .make_nonzero
    mov ax, [sbm_used]
    jmp .make_done
.make_nonzero:
    mov dx, [sbm_used]
    mov ax, dx
    add ax, [bp+4]
    jc .make_bad
    cmp ax, 16384
    ja .make_bad
    push ax
    call sbm_ensure
    pop bx                              ; requested new used count
    test ax, ax
    jnz .make_bad
    cmp word [sbm_kind_seg], 0
    jne .make_claimed
    mov ax, 2
    push ax
    call _os88_mem_claim
    add sp, 2
    mov [sbm_kind_seg], ax
    test ax, ax
    jz .make_bad
    push di
    mov es, ax
    xor ax, ax
    xor di, di
    mov cx, 1024
    cld
    rep stosw
    pop di
.make_claimed:
    mov dx, [sbm_used]
    mov [sbm_used], bx
    mov di, dx
    shl di, 1
    mov cx, [bp+4]
    mov es, [sbm_lo_seg]
    xor ax, ax
    cld
    rep stosw
    cmp word [sbm_hi_seg], 0
    je .make_result
    mov di, dx
    shl di, 1
    mov cx, [bp+4]
    mov es, [sbm_hi_seg]
    xor ax, ax
    rep stosw
.make_result:
    mov ax, dx
    jmp short .make_done
.make_bad:
    mov ax, -1
.make_done:
    pop bx
    pop di
    pop es
    pop bp
    ret

_sbm_get:
_sbm_get_lo:
    push bp
    mov bp, sp
    push es
    push bx
    mov bx, [bp+4]
    shl bx, 1
    mov es, [sbm_lo_seg]
    mov ax, [es:bx]
    pop bx
    pop es
    pop bp
    ret

_sbm_get_hi:
    push bp
    mov bp, sp
    push es
    push bx
    mov bx, [bp+4]
    shl bx, 1
    mov ax, [sbm_hi_seg]
    test ax, ax
    jz .get_hi_sign
    mov es, ax
    mov ax, [es:bx]
    jmp short .get_hi_done
.get_hi_sign:
    mov es, [sbm_lo_seg]
    mov ax, [es:bx]
    cwd
    mov ax, dx
.get_hi_done:
    pop bx
    pop es
    pop bp
    ret

; void sbm_set(unsigned index, int value) -- sign-extends into a live high.
_sbm_set:
    push bp
    mov bp, sp
    push es
    push bx
    mov bx, [bp+4]
    shl bx, 1
    mov ax, [bp+6]
    mov es, [sbm_lo_seg]
    mov [es:bx], ax
    cwd
    cmp word [sbm_hi_seg], 0
    je .set_done
    mov es, [sbm_hi_seg]
    mov [es:bx], dx
.set_done:
    pop bx
    pop es
    pop bp
    ret

; int sbm_set_pair(index, lo, hi) -- 0 success, -1 if late high allocation
; fails.  On failure the old cell remains intact.
_sbm_set_pair:
    push bp
    mov bp, sp
    push es
    push bx
    mov bx, [bp+4]
    shl bx, 1
    mov ax, [bp+6]
    cwd
    cmp word [sbm_hi_seg], 0
    jne .pair_store
    cmp dx, [bp+8]
    je .pair_low
    call sbm_claim_hi
    test ax, ax
    jz .pair_bad
.pair_store:
    mov ax, [bp+6]
    mov es, [sbm_lo_seg]
    mov [es:bx], ax
    mov ax, [bp+8]
    mov es, [sbm_hi_seg]
    mov [es:bx], ax
    xor ax, ax
    jmp short .pair_done
.pair_low:
    mov ax, [bp+6]
    mov es, [sbm_lo_seg]
    mov [es:bx], ax
    xor ax, ax
    jmp short .pair_done
.pair_bad:
    mov ax, -1
.pair_done:
    pop bx
    pop es
    pop bp
    ret

; One type bit per numeric cell: 0 = LONG, 1 = SINGLE.
_sbm_kind_get:
    push bp
    mov bp, sp
    push es
    push bx
    push cx
    mov bx, [bp+4]
    mov cl, bl
    and cl, 7
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov es, [sbm_kind_seg]
    mov al, [es:bx]
    xor ah, ah
    shr ax, cl
    and ax, 1
    pop cx
    pop bx
    pop es
    pop bp
    ret

_sbm_kind_set:
    push bp
    mov bp, sp
    push es
    push bx
    push cx
    push dx
    mov bx, [bp+4]
    mov cl, bl
    and cl, 7
    mov dl, 1
    shl dl, cl
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov es, [sbm_kind_seg]
    test word [bp+6], -1
    jz .kind_clear
    or [es:bx], dl
    jmp short .kind_done
.kind_clear:
    not dl
    and [es:bx], dl
.kind_done:
    pop dx
    pop cx
    pop bx
    pop es
    pop bp
    ret

; int sbm_str_set(index, near source, length) -- 0 or -1 (pool full).
_sbm_str_set:
    push bp
    mov bp, sp
    push es
    push si
    push di
    mov di, [bp+4]
    cmp di, [sbm_used]
    jae .ss_bad
    call sbm_claim_hi
    test ax, ax
    jz .ss_bad
    cmp word [sbm_str_seg], 0
    jne .ss_have_pool
    mov ax, 32
    push ax
    call _os88_mem_claim
    add sp, 2
    mov [sbm_str_seg], ax
    test ax, ax
    jz .ss_bad
.ss_have_pool:
    shl di, 1
    mov es, [sbm_hi_seg]
    mov cx, [es:di]
    test cx, cx
    jz .ss_alloc
    cmp [bp+8], cx
    ja .ss_alloc
    mov es, [sbm_lo_seg]
    mov dx, [es:di]                 ; replacement fits its existing extent
    jmp short .ss_copy
.ss_alloc:
    mov ax, [sbm_str_used]
    mov dx, ax
    add ax, [bp+8]
    jc .ss_bad
    cmp ax, 32767
    ja .ss_bad
    mov [sbm_str_used], ax
.ss_copy:
    mov es, [sbm_str_seg]
    mov di, dx
    mov si, [bp+6]
    mov cx, [bp+8]
    cld
    rep movsb
    mov di, [bp+4]
    shl di, 1
    mov es, [sbm_lo_seg]
    mov [es:di], dx
    mov ax, [bp+8]
    mov es, [sbm_hi_seg]
    mov [es:di], ax
    xor ax, ax
    jmp short .ss_done
.ss_bad:
    mov ax, -1
.ss_done:
    pop di
    pop si
    pop es
    pop bp
    ret

_sbm_str_len:
    jmp _sbm_get_hi

; int sbm_str_copy(index, near dest, capacity) -- copied length, NUL appended.
_sbm_str_copy:
    push bp
    mov bp, sp
    push ds
    push es
    push si
    push di
    push bx
    mov bx, [bp+4]
    shl bx, 1
    mov es, [sbm_hi_seg]
    mov cx, [es:bx]
    mov ax, [bp+8]
    test ax, ax
    jz .sc_zero
    dec ax
    cmp cx, ax
    jbe .sc_len
    mov cx, ax
.sc_len:
    mov dx, cx
    mov es, [sbm_lo_seg]
    mov si, [es:bx]
    push ds
    pop es
    mov di, [bp+6]
    mov ds, [sbm_str_seg]
    cld
    rep movsb
    mov byte [es:di], 0
    mov ax, dx
    jmp short .sc_done
.sc_zero:
    xor ax, ax
.sc_done:
    pop bx
    pop di
    pop si
    pop es
    pop ds
    pop bp
    ret

; int sbm_str_char(index, position) -- byte or -1 outside the stored string.
_sbm_str_char:
    push bp
    mov bp, sp
    push es
    push bx
    push di
    mov bx, [bp+4]
    shl bx, 1
    mov es, [sbm_hi_seg]
    mov ax, [bp+6]
    cmp ax, [es:bx]
    jae .char_bad
    mov es, [sbm_lo_seg]
    mov di, [es:bx]
    add di, ax
    mov es, [sbm_str_seg]
    xor ax, ax
    mov al, [es:di]
    jmp short .char_done
.char_bad:
    mov ax, -1
.char_done:
    pop di
    pop bx
    pop es
    pop bp
    ret
