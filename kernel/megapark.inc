; =============================================================================
; kernel/megapark.inc - THE PARK STUB (docs/plans/KERN-MEGA-DOS-PLAN.md 2.3
; step 9; kernel/megados.inc is the rest). Included by kernel/hiber.inc, in
; HIBER.DRV's `.modh`, under -DKERN_MEGA: mega_dosparkf copies it from there
; into the park's header, and it never runs where it is assembled.
;
; IT RUNS IN PLACE, ABOVE 1MB, IN PROTECTED MODE. kern_dos's kd_unpark opens
; A20, loads the GDT the kernel left in the header (MPK_GDT) and far-jumps to
; 08h:0, a code segment based at the park itself. So nothing of the way back
; executes in conventional memory - which the restore overwrites - or in the
; text framebuffer, which Bochs refuses to fetch instructions from at all.
;
; in:  protected mode, IF = 0, NMI off, CS = 08h (base the park), every PIC
;      line masked; the header's cells (MPK_C_*) and GDT written by the kernel;
;      DX:BX = the sum, CX = KB parked and SI:DI = the park's base, from
;      somewhere the header is NOT - kd_unpark's launch block, below 1MB -
;      so a header a stray write shrank cannot restore too little and still
;      agree with itself (KERN-MEGA-DOS-PLAN 10.4). A resume passes its own
;      cells (mega_hbgof); W7's probe, which after a reset has nothing else,
;      passes the header's
; out: the restore and a far jump to mega_dwake through the kernel's own
;      tables, or - the sum differs - a sentence, a key and a restart
;
; Every datum is `[x - mpk_stub]`, DS = 10h, the same base as CS.
; =============================================================================
cpu 286
; MPKMARK col, char - DOSRMARK=1's trace (kernel/hbmark.inc), one character on
; row 7 of the text page kd_unpark left up, through 28h: the stub's CS is the
; park, so hbmark's CS-relative HBMARK would write above 1MB. Columns 40-44,
; after kd_unpark's own 'U' at 40 and clear of hbm_wake's 'W' at 78.
%ifdef DOSR_MARK
%macro MPKMARK 2
    push es
    push word 0x28
    pop es
    mov byte [es:HM_ROW + %1 * 2], %2
    mov byte [es:HM_ROW + %1 * 2 + 1], 0x07
    pop es
%endmacro
%else
%macro MPKMARK 2
%endmacro
%endif
mpk_stub:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov sp, MPK_STACK
    cld
    MPKMARK 41, 'P'                 ; in protected mode, above 1MB
    cmp bx, [MPK_C_PSUM]            ; --- the cells, against what was passed --
    jne .badc
    cmp dx, [MPK_C_PSUM + 2]
    jne .badc
    cmp cx, [MPK_C_PKB]
    jne .badc
    cmp di, [MPK_C_PBASE]
    jne .badc
    cmp si, [MPK_C_PBASE + 2]
    jne .badc
    mov si, mpk_tabs - mpk_stub     ; ...the kernel's GDTR and IDTR, which are
    mov di, MPK_C_KGDTR             ; constants of its build
    mov cx, 6
    repe cmpsw
    jne .badc
    cmp word [MPK_C_WAKE + 2], MEGA_SEL_CODE ; ...and where it jumps: one of the
    jne .badc                       ; two ways back in, and nothing else
    mov ax, [MPK_C_WAKE]
    cmp ax, mega_dwake
    je .cells
    cmp ax, mega_hbwake
    je .cells0
.badc:
    jmp .bad
.cells0:
    cmp word [MPK_NREG], 0          ; a PARK (a resume sums nothing) keeps the
    je .cells                       ; KB and the base inside the sum as well,
    mov ax, [MPK_C_PKB]             ; so a header shrunk in BOTH places - W7's
    cmp ax, [MPK_SAVE]              ; probe has no other copy to pass - still
    jne .badc                       ; differs from what was parked
    mov ax, [MPK_C_PBASE]
    cmp ax, [MPK_SAVE + 2]
    jne .badc
    mov ax, [MPK_C_PBASE + 2]
    cmp ax, [MPK_SAVE + 4]
    jne .badc
.cells:
    call .pit                       ; the clock starts
    xor ax, ax
    mov [MPK_C_PACC], ax
    mov [MPK_C_PACC + 2], ax
    ; --- the sum, over the list after the header's cells ----------------------
    mov bp, [MPK_NREG]
    mov si, MPK_REG
    xor ax, ax
    xor dx, dx
.reg:
    or bp, bp
    jz .summed
    push word [si+6]                ; lines, then the window, as 32 bits each
    push word [si+4]
    push word [si+2]
    push word [si]
.chunk:
    mov bx, sp
    mov cx, [bx+4]
    or cx, [bx+6]
    jz .rdone
    mov cx, 1024
    cmp word [bx+6], 0
    jne .n
    cmp [bx+4], cx
    jae .n
    mov cx, [bx+4]
.n:
    sub [bx+4], cx
    sbb word [bx+6], 0
    push ax
    push dx
    push cx
    mov ax, [bx]
    mov dx, [bx+2]
    mov di, MPK_GDT + 0x18          ; 18h: the window
    call .desc
    pop cx
    pop dx
    pop ax
    push ds
    push word 0x18
    pop ds
    MPK_SUMLINES
    pop ds
    push ax
    push bx
    call .pit
    pop bx
    pop ax
    mov bx, sp
    inc word [bx+2]
    jmp .chunk
.rdone:
    add sp, 8
    add si, 8
    dec bp
    jmp .reg
.summed:
    cmp ax, [MPK_C_PSUM]
    jne .bad
    cmp dx, [MPK_C_PSUM + 2]
    jne .bad
    MPKMARK 42, 'S'                 ; the sum agrees
    ; --- a hibernate's COPIES, before the restore (W5; 0 on a park) ----------
    ; the tables and every pool block, from where the resume read them into
    ; this block to where the image had them. This stub runs on its own GDT
    ; with IF = 0 and NMI off, so the kernel's GDT, LDT and IDT are only data
    mov bp, [MPK_NCPY]
    mov si, [MPK_NREG]
    shl si, 3
    add si, MPK_REG
.cpy:
    or bp, bp
    jz .cpyd
    call .xcopy
    add si, MPK_CPYSZ
    dec bp
    jmp short .cpy
.cpyd:
    ; --- the restore: the IVT, then 500h .. P - never the BDA ----------------
    xor si, si                      ; SI:BP = the offset
    xor bp, bp
    mov cx, 0x400
    call .move
    mov bp, 0x500
.rst:
    mov ax, bp
    mov dx, si
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    mov cx, [MPK_C_PKB]
    sub cx, ax
    jbe .restored
    cmp cx, 32                      ; whole 32KB chunks, the first ending on
    jb .last                        ; 32KB and the last on P, which is any
    mov cx, 32                      ; whole KB
.last:
    xchg ch, cl                     ; KB -> bytes
    shl cx, 1
    shl cx, 1
    mov ax, bp
    and ax, 0x7FFF
    sub cx, ax                      ; (bp's own place in its 32KB, which is 0
                                    ; but for the first: 500h)
    call .move
    add bp, cx
    adc si, 0
    jmp short .rst
.restored:
    MPKMARK 43, 'R'                 ; conventional memory is back
    call .pit
    mov ax, [MPK_C_PACC]            ; kern_dos left PIT channel 0 in the
    mov dx, [MPK_C_PACC + 2]        ; ROM's mode 3: two counts a clock
    mov bx, 2 * 1193
    mov cx, 0xFFFF
    cmp dx, bx
    jae .tset
    div bx
    mov cx, ax
.tset:
    mov bx, [MPK_C_POUT]
    MPKMARK 44, 'K'                 ; ...into the kernel's tables and out
    lgdt [MPK_C_KGDTR]              ; the kernel's own tables, verified
    lidt [MPK_C_KIDTR]
    jmp far [MPK_C_WAKE]            ; mega_dwake, or a resume's mega_hbwake

.bad:                               ; --- THE SUM DIFFERS (12.1 item 3) ---------
    MPKMARK 42, 'X'
    mov ax, MEGA_GDT_LIN & 0xFFFF   ; W7's anchor goes first: a park judged
    mov dx, MEGA_GDT_LIN >> 16      ; damaged is not one for the next boot to
    mov di, MPK_GDT + 0x18          ; find and take back (it did, in Bochs,
    call .desc                      ; with the header's own shrunk cells)
    push word 0x18
    pop es
    xor ax, ax
    xor di, di
    mov cx, 4
    rep stosw
    push word 0x28                  ; 28h: the text page kd_unpark left up
    pop es
    mov si, mpk_s_bad - mpk_stub
    mov di, 12 * 160
    mov ah, 0x4F
.say:
    lodsb
    or al, al
    jz .key
    stosw
    jmp short .say
.key:
    in al, 0x64                     ; ...a key: anything already waiting is
    test al, 1                      ; not the answer
    jz .k1
    in al, 0x60
    jmp short .key
.k1:
    in al, 0x64
    test al, 1
    jz .k1
    in al, 0x60
    test al, 0x80
    jnz .k1
    mov al, 0x8F                    ; a whole POST: shutdown code 0
    out 0x70, al
    xor al, al
    out 0x71, al
    xor cx, cx
.kbw:
    in al, 0x64
    test al, 2
    loopnz .kbw
    mov al, 0xFE                    ; the 8042's reset line
    out 0x64, al
.h:
    hlt
    jmp short .h

.move:                              ; CX bytes from park + HDR + SI:BP to SI:BP
    pusha
    push ds
    push es
    mov ax, bp
    mov dx, si
    add ax, MPK_HDR
    adc dx, 0
    add ax, [MPK_C_PBASE]
    adc dx, [MPK_C_PBASE + 2]
    mov di, MPK_GDT + 0x18
    call .desc
    mov ax, bp
    mov dx, si
    mov di, MPK_GDT + 0x20
    call .desc
    call .pit
    push word 0x18
    pop ds
    push word 0x20
    pop es
    xor si, si
    xor di, di
    shr cx, 1
    rep movsw
    pop es
    pop ds
    popa
    ret

.xcopy:                             ; [SI] = {dd from, dd to, dd bytes}: the
    push word [si+10]               ; copy, 32KB a window. Clobbers AX BX CX
    push word [si+8]                ; DX DI
    push word [si+6]
    push word [si+4]
    push word [si+2]
    push word [si]
.xc:
    mov bx, sp                      ; [bx] from, [bx+4] to, [bx+8] bytes left
    mov cx, [bx+8]
    or cx, [bx+10]
    jz .xcd
    mov cx, 0x8000
    cmp word [bx+10], 0
    jne .xn
    cmp [bx+8], cx
    jae .xn
    mov cx, [bx+8]
.xn:
    sub [bx+8], cx
    sbb word [bx+10], 0
    mov ax, [bx]
    mov dx, [bx+2]
    mov di, MPK_GDT + 0x18
    call .desc
    mov ax, [bx+4]
    mov dx, [bx+6]
    mov di, MPK_GDT + 0x20
    call .desc
    add [bx], cx
    adc word [bx+2], 0
    add [bx+4], cx
    adc word [bx+6], 0
    push si
    push ds
    push es
    push word 0x18
    pop ds
    push word 0x20
    pop es
    xor si, si
    xor di, di
    shr cx, 1
    rep movsw
    pop es
    pop ds
    pop si
    call .pit
    jmp short .xc
.xcd:
    add sp, 12
    ret

.desc:                              ; [DS:DI] = a 64KB data descriptor, base
    mov word [di], 0xFFFF           ; DX:AX
    mov [di+2], ax
    mov [di+4], dl
    mov byte [di+5], 0x92
    mov word [di+6], 0
    ret

.pit:                               ; PIT ch0's counts since the last call,
    xor al, al                      ; into MPK_C_PACC. Clobbers AX, BX
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    mov bx, [MPK_C_PPREV]
    mov [MPK_C_PPREV], ax
    sub bx, ax
    add [MPK_C_PACC], bx
    adc word [MPK_C_PACC + 2], 0
    ret

mpk_tabs    dw MEGA_GDT_N * 8 - 1   ; mega_gdtr and mega_idtr, as the cells
            dw MEGA_GDT_LIN & 0xFFFF, MEGA_GDT_LIN >> 16 ; must hold them
            dw 256 * 8 - 1
            dw MEGA_IDT_LIN & 0xFFFF, MEGA_IDT_LIN >> 16
mpk_s_bad   db "os8088: "
mpk_s_name  db "            "       ; the program's name (mega_dosgo)
            db " wrote over the parked session. Press a key to restart.", 0
mpk_stub_end:
%if mpk_stub_end - mpk_stub > MPK_GDT
 %error "the park stub runs into its GDT in the park's header"
%endif
cpu 8086
