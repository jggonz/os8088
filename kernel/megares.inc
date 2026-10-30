; =============================================================================
; kernel/megares.inc - kern_mega's STREAMING RESUME (docs/plans/
; KERN-MEGA-MEMORY-PLAN.md MC4.1 (b)). Included by kernel/hiber.inc, in
; HIBER.DRV's `.modh`, under -DKERN_MEGA: hbm_xres stages it into the text
; page with SPEC.md 87.5's map (kernel/hbstage.inc), and mega_hbgor leaves
; protected mode by the DOS handover's own route (mega_dosgo_lo), whose
; mega_dropr copies the 16KB stage to the TOP 16KB of conventional memory -
; [mem_top] - 400h, its runs-park placement - and enters it at HS_CODE. It
; never runs where it is assembled.
;
; THE WHOLE IMAGE COMES BACK THROUGH THE BIOS, in real mode, with nothing of
; the fresh kernel left alive - which is what lets it need no room at all:
; every run goes straight to where the image had it.
;
;   1. THE SECTION: the extended runs (the tables first, then every pool
;      block), file sectors [MR_S0, MR_S0 + MR_SN), read a bounce at a time
;      into 1000:0000 and moved with int 15h AH=87h to each run's linear
;      address, summed as they come against the pointer's HBP_XSUM;
;   2. THE CONVENTIONAL PICTURE, file sectors [0, MR_C): straight into place
;      by int 13h, kern_big's hbstub loop - but sectors 0 and 1 (the IVT)
;      land in HS_IVT, sector 2 (the BDA) nowhere, and the LAST 32 - this
;      stage's own 16KB - in the text page as DATA;
;   3. A20 opened (int 15h may shut it, int 13h does not touch it), and the
;      TAIL copied to 0000:0500 - the half of file sector 2 no kernel owns
;      and no resume restores (SPEC.md 87.5) - and entered. The tail puts
;      the IVT back, swaps the stage's cells (HS_CLK and the rest, which
;      hbm_wake reads off the text page) back into the text page while the
;      saved bytes come out of it, copies the stage's own 16KB home, loads
;      the image's GDTR and IDTR, sets PE and far-jumps to mega_hbwake.
;
; BOCHS WILL NOT FETCH AN INSTRUCTION FROM THE TEXT PAGE (megados.inc's
; note), which is why the stub runs in conventional memory and the text page
; only ever holds its DATA.
;
; A SUM THAT DISAGREES, or a read the BIOS refuses three times, is no longer
; a refusal: the fresh session went at the drop. It is a sentence, a key and
; a restart, kern_big's stub's own answer.
; =============================================================================
cpu 8086

MR_C        equ 0x0460          ; dw conventional sectors to put back
MR_XLO      equ 0x0462          ; dw this stage's first file sector (MR_C - 32)
MR_S0       equ 0x0464          ; dw the section's first file sector (its head)
MR_SN       equ 0x0466          ; dw the section's sectors (HBP_XLEN / 512)
MR_NRUN     equ 0x0468          ; dw runs in MR_RUNS
MR_FBSEG    equ 0x046A          ; dw the text page's REAL segment
MR_SUM      equ 0x046C          ; dd what the pointer says the section sums to
MR_ACC      equ 0x0470          ; dd ...and what it does, s1 then s2
MR_RUN      equ 0x0474          ; dw the run being filled
MR_D        equ 0x0476          ; dd where the run's next byte goes, linear
MR_L        equ 0x047A          ; dd ...and its bytes still to come
MR_K        equ 0x047E          ; dw the file sector the walk is at
MR_LEFT     equ 0x0480          ; dw ...and sectors still to read
MR_SKIP     equ 0x0482          ; dw bytes of the bounce that are not a run's
MR_BYTES    equ 0x0484          ; dw bytes in the bounce
MR_LBA      equ 0x0486          ; dd mr_read's scratch: the extent's sector
MR_CNT      equ 0x048A          ; dw ...and the call's count
MR_RK       equ 0x048C          ; dw mr_read's file sector...
MR_RN       equ 0x048E          ; dw ...its sectors still to read
MR_SECT     equ 0x0490          ; dw ...and the CHS sector, 1-based
MR_END      equ 0x0492
MR_RUNS     equ 0x2800          ; {dd linear, dd bytes} x HBX_HMAX: HS_EXT's
                                ; HS_XMAX entries end here
MR_GDT      equ 0x2A00          ; int 15h AH=87h's table: six descriptors
MR_MINI     equ 0x2C00          ; the tail, copied to 0000:0500 at the end
MR_SCR      equ 0x3000          ; one sector, 512-aligned in the stage
MR_BNC      equ 0x1000          ; THE BOUNCE: 1000:0000, 64 sectors, inside
MR_BNCN     equ 64              ; one 64KB DMA page
MR_TAIL     equ 0x0500          ; where the tail runs, linear
%if MR_END > HS_IVT
 %error "megares: the cells run into the IVT's copy"
%endif
%if HS_EXT + HS_XMAX * HS_XSTRIDE > MR_RUNS
 %error "megares: the extents run into the run list"
%endif
%if MR_RUNS + HBX_HMAX * 8 > MR_GDT
 %error "megares: the run list runs into the int 15h table"
%endif

mrs_stub:
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, HS_STACK
    cld
    sti                             ; the ROM wants its interrupts on
    ; --- 1. the section, through the bounce and int 15h --------------------
    xor ax, ax
    mov [MR_ACC], ax
    mov [MR_ACC + 2], ax
    mov [MR_RUN], ax
    call mr_ldrun
    mov ax, [MR_S0]
    mov [MR_K], ax
    mov ax, [MR_SN]
    mov [MR_LEFT], ax
    mov word [MR_SKIP], 512         ; the head: summed, never moved
.sec:
    mov cx, [MR_LEFT]
    or cx, cx
    jnz .sec1
    jmp .secd
.sec1:
    cmp cx, MR_BNCN
    jbe .secn
    mov cx, MR_BNCN
.secn:
    push cx
    mov ax, MR_BNC
    mov es, ax
    xor bx, bx
    mov ax, [MR_K]
    call mr_read
    pop cx
    add [MR_K], cx
    sub [MR_LEFT], cx
    call mr_sum                     ; CX sectors of the bounce
    mov ax, cx
    mov cl, 9
    shl ax, cl
    mov [MR_BYTES], ax
    mov di, [MR_SKIP]
    mov word [MR_SKIP], 0
.disp:
    cmp di, [MR_BYTES]
    jae .sec
    mov ax, [MR_RUN]
    cmp ax, [MR_NRUN]
    jae .sec                        ; past the last run: the chunk's padding
    mov cx, [MR_BYTES]
    sub cx, di                      ; CX = what the bounce still holds...
    cmp word [MR_L + 2], 0
    jne .cap
    cmp cx, [MR_L]
    jbe .cap
    mov cx, [MR_L]                  ; ...or what the run still wants
.cap:
    call mr_move                    ; CX bytes from the bounce at DI
    add di, cx
    add [MR_D], cx
    adc word [MR_D + 2], 0
    sub [MR_L], cx
    sbb word [MR_L + 2], 0
    mov ax, [MR_L]
    or ax, [MR_L + 2]
    jnz .disp
    inc word [MR_RUN]
    call mr_ldrun
    jmp short .disp
.secd:
    mov ax, [MR_ACC]                ; THE SUM, against the pointer's
    cmp ax, [MR_SUM]
    jne .fail
    mov ax, [MR_ACC + 2]
    cmp ax, [MR_SUM + 2]
    jne .fail
    ; --- 2. conventional memory, by the extents ----------------------------
    mov word [MR_K], 0
.cnv:
    mov ax, [MR_K]
    cmp ax, [MR_C]
    jae .cnvd
    cmp ax, 3
    jb .one                         ; the IVT's two, and the BDA's
    cmp ax, [MR_XLO]
    jae .one                        ; this stage's own 32
    mov cx, [MR_XLO]
    sub cx, ax                      ; CX = up to the stage
    mov dx, ax
    and dx, 127
    neg dx
    add dx, 128                     ; DX = to the 64KB page's end
    cmp cx, dx
    jbe .c1
    mov cx, dx
.c1:
    cmp cx, 127
    jbe .c2
    mov cx, 127
.c2:
    push cx
    mov bx, ax
    mov cl, 5
    shl bx, cl                      ; ES = k x 32: linear k x 512
    mov es, bx
    xor bx, bx
    pop cx
    call mr_read
    add [MR_K], cx
    jmp short .cnv
.one:                               ; ONE sector into the stage's scratch,
    push cs                         ; then where it goes
    pop es
    mov bx, MR_SCR
    mov cx, 1
    call mr_read
    mov ax, [MR_K]
    inc word [MR_K]
    mov si, MR_SCR
    cmp ax, 2
    je .cnv                         ; the BDA: the ROM's, never restored
    ja .xsave
    mov di, HS_IVT                  ; the IVT: put back LAST, by the tail
    or ax, ax
    jz .ivt
    add di, 512
.ivt:
    push cs
    pop es
    jmp short .cpy
.xsave:
    sub ax, [MR_XLO]                ; the stage's own bytes: into the text
    mov cl, 9                       ; page, which the tail copies home
    shl ax, cl
    mov di, ax
    mov es, [MR_FBSEG]
.cpy:
    mov cx, 256
    rep movsw
    jmp .cnv
.cnvd:
    ; --- 3. A20, the tail, and out ----------------------------------------
    call mr_a20
    jc .fail
    cli
    xor ax, ax
    mov es, ax
    mov di, MR_TAIL
    mov si, MR_MINI
    mov cx, (mrt_end - mrt) / 2 + 1
    rep movsw
    db 0xEA                         ; jmp far 0000:0500
    dw MR_TAIL, 0
.fail:
    cli
    mov ah, 0x02                    ; the cursor to row 8: rows 0-2 of the
    xor bh, bh                      ; text page are the stage's cells
    mov dx, 0x0800
    sti
    int 0x10
    mov si, mrs_s_fail - mrs_stub
.ch:
    mov al, [si]
    or al, al
    jz .key
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    inc si
    jmp short .ch
.key:
    xor ah, ah
    int 0x16
    int 0x19
    jmp $

; mr_ldrun - [MR_D] and [MR_L] from run [MR_RUN], when there is one
mr_ldrun:
    push si
    push di
    push es
    mov si, [MR_RUN]
    cmp si, [MR_NRUN]
    jae .o
    shl si, 1
    shl si, 1
    shl si, 1
    add si, MR_RUNS
    mov di, MR_D
    push cs
    pop es
    movsw
    movsw
    movsw                           ; (MR_L follows MR_D: four words)
    movsw
.o:
    pop es
    pop di
    pop si
    ret
%if MR_L != MR_D + 4
 %error "megares: mr_ldrun copies MR_D and MR_L as one run"
%endif

; mr_sum - [MR_ACC] += Fletcher's sums over CX sectors at MR_BNC:0, the
; kernel's hbm_xsum word for word. CX kept
mr_sum:
    push cx
    push ds
    mov ax, [MR_ACC]
    mov dx, [MR_ACC + 2]
    mov bx, MR_BNC
    mov ds, bx
    xor si, si
    mov bx, cx
    mov cl, 8
    shl bx, cl                      ; words
.l:
    add ax, [si]
    adc ax, 0
    add dx, ax
    adc dx, 0
    inc si
    inc si
    dec bx
    jnz .l
    pop ds
    mov [MR_ACC], ax
    mov [MR_ACC + 2], dx
    pop cx
    ret

; mr_move - CX bytes (even) from MR_BNC:DI to linear [MR_D], int 15h AH=87h.
; Every register kept but flags; a refusal is .fail's
mr_move:
    push ax
    push cx
    push si
    push di
    push es
    push cs
    pop es
    mov si, MR_GDT
    push di
    xor ax, ax
    mov di, si
    push cx
    mov cx, 24
    rep stosw                       ; the six descriptors, blank
    pop cx
    pop di
    mov ax, di                      ; the source: 10000h + DI
    mov word [MR_GDT + 0x10], 0xFFFF
    mov [MR_GDT + 0x12], ax
    mov byte [MR_GDT + 0x14], MR_BNC >> 12
    mov byte [MR_GDT + 0x15], 0x93
    mov word [MR_GDT + 0x18], 0xFFFF ; the destination
    mov ax, [MR_D]
    mov [MR_GDT + 0x1A], ax
    mov al, [MR_D + 2]
    mov [MR_GDT + 0x1C], al
    mov byte [MR_GDT + 0x1D], 0x93
    shr cx, 1                       ; words
    mov ah, 0x87
    int 0x15
    pop es
    pop di
    pop si
    pop cx
    pop ax
    jc .bad
    ret
.bad:
    add sp, 2                       ; (no way back from here)
    jmp mrs_stub.fail

; mr_read - CX sectors (1..127) from file sector AX into ES:BX, by the
; extents (HS_EXT, [HS_NX] of them), a track or an extent a call. The caller
; keeps the run inside one 64KB DMA page. Every register kept but flags; a
; disk that refuses three times, or a sector past the list, is .fail's
mr_read:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [MR_RK], ax
    mov [MR_RN], cx
.more:
    cmp word [MR_RN], 0
    je .done
    mov ax, [MR_RK]                 ; the extent with file sector AX in it
    mov si, HS_EXT
    mov di, [HS_NX]
    xor dx, dx                      ; DX = the sectors before this extent
.f:
    or di, di
    jz .bad                         ; past the file: a list that lies
    mov cx, ax
    sub cx, dx                      ; CX = how far into this extent
    cmp cx, [si + 4]
    jb .got
    add dx, [si + 4]
    add si, HS_XSTRIDE
    dec di
    jmp short .f
.got:
    mov ax, [si + 4]
    sub ax, cx                      ; the extent's sectors from there...
    cmp ax, [MR_RN]
    jbe .g0
    mov ax, [MR_RN]                 ; ...or what is wanted
.g0:
    mov [MR_CNT], ax
    mov ax, [si]
    mov dx, [si + 2]
    add ax, cx
    adc dx, 0
    mov [MR_LBA], ax
    mov ax, dx                      ; --- CHS (hbstub's arithmetic) ---------
    xor dx, dx
    div word [HS_SPT]
    mov di, ax                      ; DI:AX = the track after the second div
    mov ax, [MR_LBA]
    div word [HS_SPT]               ; ...and DX = the sector - 1
    mov cx, [HS_SPT]
    sub cx, dx                      ; CX = to the track's end
    cmp cx, [MR_CNT]
    jae .g1
    mov [MR_CNT], cx
.g1:
    inc dx
    mov [MR_SECT], dx
    mov dx, di
    div word [HS_HEADS]             ; AX = the cylinder, DX = the head
    mov ch, al
    mov cl, ah
    ror cl, 1
    ror cl, 1
    and cl, 0xC0
    or cl, [MR_SECT]
    mov dh, dl
    mov dl, [HS_UNIT]
    mov si, 3                       ; tries
.try:
    mov al, [MR_CNT]
    mov ah, 0x02
    push cx
    push dx
    int 0x13
    pop dx
    pop cx
    jnc .ok
    push dx
    xor ah, ah
    int 0x13                        ; reset, and again
    pop dx
    dec si
    jnz .try
.bad:
    jmp mrs_stub.fail               ; (SP is the stub's own: no way back)
.ok:
    mov ax, [MR_CNT]
    add [MR_RK], ax
    sub [MR_RN], ax
    mov cl, 9
    shl ax, cl
    add bx, ax                      ; ...and on in the destination
    jmp .more
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mr_a20 - A20 OPEN and proved (kerndos/kdpark.inc's sequence): the tail's
; jump lands above 1MB's reach and int 15h AH=87h may have shut it.
; CF = 1 it would not open
mr_a20:
    call .t
    jnc .r
    in al, 0x92
    test al, 2
    jnz .kbc
    or al, 2
    and al, 0xFE
    out 0x92, al
    call .w
    jnc .r
.kbc:
    cli
    call .kbw
    mov al, 0xD1
    out 0x64, al
    call .kbw
    mov al, 0xDF
    out 0x60, al
    call .kbw
    sti
.w:
    mov cx, 512
.p:
    call .t
    jnc .r
    loop .p
    stc
.r:
    ret
.kbw:
    xor cx, cx
.kb1:
    in al, 0x64
    test al, 2
    loopnz .kb1
    ret
.t:                                 ; CF = 1 the gate is SHUT
    push ds
    push es
    pushf
    cli
    xor ax, ax
    mov es, ax
    dec ax
    mov ds, ax
    mov bx, [es:0xFFE0]             ; 0000:FFE0 against FFFF:FFF0
    not word [0xFFF0]
    mov ax, [es:0xFFE0]
    not word [0xFFF0]
    popf
    pop es
    pop ds
    cmp ax, bx
    je .o
    stc
    ret
.o:
    clc
    ret

mrs_s_fail: db 13, 10, 'Resume failed: the image could not be read back.', 13, 10
            db 'Press any key to restart.', 13, 10, 0
mrs_stub_end:
%if mrs_stub_end - mrs_stub > HS_CODE_MAX
 %error "megares: the stub is bigger than its staging slot"
%endif

; -----------------------------------------------------------------------------
; THE TAIL - runs at 0000:0500 (MR_TAIL), CS = 0, IF = 0, no stack. Every
; datum is [cs:MR_TAIL + (x - mrt)]. Staged at MR_MINI with its four words and
; the two table registers written in by hbm_xres
; -----------------------------------------------------------------------------
%define MRT(x) (MR_TAIL + (x) - mrt)
mrt:
    mov al, 0xFF                    ; every line masked: from here the IVT is
    out 0x21, al                    ; the image's
    out 0xA1, al
    mov al, 0x8D                    ; NMI off
    out 0x70, al
    mov ds, [cs:MRT(mrt_x)]         ; --- the IVT, from the stage -----------
    xor ax, ax
    mov es, ax
    mov si, HS_IVT
    xor di, di
    mov cx, 512
    rep movsw
    mov es, [cs:MRT(mrt_fb)]        ; --- the stage's cells back into the
    mov si, 0x0400                  ; text page (hbm_wake reads HS_CLK and the
    mov di, si                      ; rest there), the image's bytes out
    mov cx, 128
.sw:
    mov ax, [si]
    xchg ax, [es:di]
    mov [si], ax
    inc si
    inc si
    inc di
    inc di
    loop .sw
    push es                         ; --- the stage's own 16KB, home -------
    push ds
    pop es
    pop ds
    xor si, si
    xor di, di
    mov cx, 0x200
    rep movsw                       ; [0, 400h)
    mov si, 0x0500
    mov di, si
    mov cx, (0x4000 - 0x0500) / 2
    rep movsw                       ; [500h, 4000h)
cpu 286
    lgdt [cs:MRT(mrt_gdtr)]         ; --- the image's tables, and in --------
    lidt [cs:MRT(mrt_idtr)]
    smsw ax
    or al, 1
    lmsw ax
cpu 8086
    db 0xEA
mrt_wake:   dw 0, 0                 ; mega_hbwake, MEGA_SEL_CODE
mrt_x:      dw 0                    ; the stage's segment
mrt_fb:     dw 0                    ; the text page's
mrt_gdtr:   dw 0, 0, 0
mrt_idtr:   dw 0, 0, 0
mrt_end:
%if mrt_end - mrt > 0x100
 %error "megares: the tail outgrew 0000:0500-05FF"
%endif
%if MR_MINI + (mrt_end - mrt) > MR_SCR
 %error "megares: the tail runs into the scratch sector"
%endif
