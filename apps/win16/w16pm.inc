; =============================================================================
; os8088 - apps/win16/w16pm.inc
;
; THE PROTECTED-MODE ARM'S MEMORY (SPEC.md 124.31.3): VIRTUAL PARAGRAPHS
; INSIDE, SELECTORS AT THE EDGE. WIN16 computes in paragraphs - a block's
; header is the paragraph below its data, an arena is walked by adding sizes,
; a program's segments are a claim's base plus offsets - and on kern_mega no
; such value can be loaded into a segment register. The arm keeps every sum
; and changes what the numbers mean: each claim WIN16 carves is entered in
; the MAP with a virtual base of its own, first fit in W16V_LO-FFFFh, and a
; virtual paragraph becomes a selector only where it is loaded:
;
;   W16SEG es, ax           ; real mode: mov es, ax
;                           ; the arm:   ES = SEG_SUB(claim, ax - base)
;
; A value in no claim of the map is loaded as it is - it is a selector
; already (a part, the program's, the kernel's) - so W16SEG is safe on a
; variable either kind may be in. The kernel answers the same selector for
; the same base, so a header asked for twice costs one LDT entry, and a cache
; of W16V_NC rows in front of it answers a repeat without a kernel call.
;
; What the program is handed is converted at the edge instead: w16_psel (a
; virtual paragraph -> a selector reaching the block's own length, a code
; segment's code half) and w16_pvirt (a selector of ours -> its virtual
; paragraph). WIN16's own claims go through w16_vclaim and w16_vfree, which
; are OSAPI_MEM_CLAIM and OSAPI_MEM_FREE in real mode.
;
; In real mode every macro here is the instruction it replaces, byte for
; byte, so the shipped build is unchanged (SPEC.md 124.31: no 8088's disk
; carries a byte of the arm).
; =============================================================================

; W16SEG dst, src - load ES or DS with a virtual paragraph. src is a 16-bit
; register or a word in memory. Keeps every register but dst, and the flags.
; In the arm a part's W16SEG needs DS = WIN16's (it reads the map there).
%macro W16V_SECT 0              ; w16v_sx = this section's thunk suffix
  %defstr w16v_ss __?SECT?__      ; "[section .name attributes...]": the
  %substr w16v_n7 w16v_ss 10, 8   ;   name, and the character after it
  %substr w16v_n6 w16v_ss 10, 7
  %substr w16v_n5 w16v_ss 10, 6
  %ifidn w16v_n7, '.w16dlg '
    %define w16v_sx d
  %elifidn w16v_n7, '.w16dlg]'
    %define w16v_sx d
  %elifidn w16v_n7, '.w16sys '
    %define w16v_sx s
  %elifidn w16v_n7, '.w16sys]'
    %define w16v_sx s
  %elifidn w16v_n6, '.w16fp '
    %define w16v_sx f
  %elifidn w16v_n6, '.w16fp]'
    %define w16v_sx f
  %elifidn w16v_n6, '.w16ld '
    %define w16v_sx l
  %elifidn w16v_n6, '.w16ld]'
    %define w16v_sx l
  %elifidn w16v_n5, '.text '
    %define w16v_sx t
  %elifidn w16v_n5, '.text]'
    %define w16v_sx t
  %else
    %error "W16SEG/W16VOF in a section with no thunks:" __?SECT?__
  %endif
%endmacro

%macro W16SEG 2
%ifndef PKG_MEGA
    mov %1, %2
%else
  W16V_SECT
  %assign w16v_r 0
  %ifidni %2, ax
    %assign w16v_r 1
  %elifidni %2, bx
    %assign w16v_r 1
  %elifidni %2, cx
    %assign w16v_r 1
  %elifidni %2, dx
    %assign w16v_r 1
  %elifidni %2, si
    %assign w16v_r 1
  %elifidni %2, di
    %assign w16v_r 1
  %elifidni %2, bp
    %assign w16v_r 1
  %endif
  %define w16vany_%[w16v_sx] 1
  %if w16v_r
    %define w16vu_%[w16v_sx]_%1%2 1
    call w16v%[w16v_sx]_%1%2
  %else
    %define w16vu_%[w16v_sx]_%1m 1
    push word %2
    call w16v%[w16v_sx]_%1m
  %endif
%endif
%endmacro

; W16VOF reg, sreg - reg = the virtual paragraph segment register sreg was
; loaded with by W16SEG (real mode: mov reg, sreg). A selector of nobody's
; map comes back as it is. Keeps every other register, and the flags.
%macro W16VOF 2
    mov %1, %2
%ifdef PKG_MEGA
  W16V_SECT
  %define w16vany_%[w16v_sx] 1
  %define w16vo_%[w16v_sx]_%1 1
    call w16v%[w16v_sx]_o%1
%endif
%endmacro

; W16PIN / W16POUT - AX at the edge (SPEC.md 124.31.3): w16_pin (what the
; program passed -> WIN16's) and w16_pout (WIN16's -> the program's). Nothing
; in real mode, where the two are the same value.
%macro W16PIN 0
%ifdef PKG_MEGA
    W16IMG w16_pin
%endif
%endmacro
%macro W16POUT 0
%ifdef PKG_MEGA
    W16IMG w16_pout
%endif
%endmacro

; W16ICS reg - reg = the IMAGE's code segment, from DS = ours: a program's
; far pointer into WIN16 (a stub, DOS3Call, a thunk's call) names it. Real
; mode `mov reg, ds`; the arm the pair's code half, DS - 8 (SPEC.md 124.31.4).
%macro W16ICS 1
    mov %1, ds
%ifdef PKG_MEGA
    sub %1, 8
%endif
%endmacro

; W16PSP - AX = the PSP as the program sees it (its ES at the start, InitTask's
; ES, a LibMain's): [w16_psp] in real mode, its selector in the arm
%macro W16PSP 0
%ifdef PKG_MEGA
    W16IMG w16_pspsel
%else
    mov ax, [w16_psp]
%endif
%endmacro

; W16TOSEL reg - the arm only: reg, a virtual paragraph, becomes the selector
; W16SEG would have loaded (for a variable that holds a selector). Nothing in
; real mode. Keeps every other register, and the flags.
%macro W16TOSEL 1
%ifdef PKG_MEGA
  W16V_SECT
  %define w16vany_%[w16v_sx] 1
  %define w16vs_%[w16v_sx]_%1 1
    call w16v%[w16v_sx]_s%1
%endif
%endmacro

; W16TOVIRT reg - the arm only: reg, a selector W16TOSEL made, becomes its
; virtual paragraph again (W16VOF's half after the mov). Nothing in real
; mode. Keeps every other register, and the flags.
%macro W16TOVIRT 1
%ifdef PKG_MEGA
  W16V_SECT
  %define w16vany_%[w16v_sx] 1
  %define w16vo_%[w16v_sx]_%1 1
    call w16v%[w16v_sx]_o%1
%endif
%endmacro

; W16CSDS reg - reg = this section's DATA segment, from CS: the same value in
; real mode, and in the arm the pair's data half, CS + 8 (a package image, a
; part claim and a SEG_SUBC copy are each a pair). Flags not kept in the arm.
%macro W16CSDS 1
    mov %1, cs
%ifdef PKG_MEGA
    add %1, 8
%endif
%endmacro

; W16CSEG sreg - sreg = this section's data, as `push cs` / `pop sreg` is in
; real mode: in the arm the pair's data half, so what is written through it is
; a store and not a trap (SPEC.md 20.18). Flags not kept in the arm.
%macro W16CSEG 1
%ifdef PKG_MEGA
    push ax
    W16CSDS ax
    mov %1, ax
    pop ax
%else
    push cs
    pop %1
%endif
%endmacro

; W16IMG fn - the arm's image routine fn, from any section: a near call in
; the image, ICALL in a part (whose DS is ours)
%macro W16IMG 1
  W16V_SECT
  %ifidn w16v_sx, t
    call %1
  %else
    ICALL %1
  %endif
%endmacro

; W16VCLAIM / W16VFREE - a claim of WIN16's own, entered in the map: as
; OSAPI_MEM_CLAIM (AX = KB -> CF=0 DX = its virtual base) and OSAPI_MEM_FREE
; (DX = it). Real mode: those calls.
%macro W16VCLAIM 0
%ifdef PKG_MEGA
    W16IMG w16_vclaim
%else
    call OSAPI_MEM_CLAIM
%endif
%endmacro
%macro W16VFREE 0
%ifdef PKG_MEGA
    W16IMG w16_vfree
%else
    call OSAPI_MEM_FREE
%endif
%endmacro

%ifdef PKG_MEGA

W16V_LO   equ 0x1000            ; the first virtual paragraph: below it are
                                ;   the moveable handles (W16_GH0)
W16V_N    equ 10                ; map rows: an owner holds at most eight
                                ;   claims (SPEC.md 50), and two to spare
W16V_SEL  equ 0                 ; a row: the claim's selector (0 = free)
W16V_PARA equ 2                 ;   ...its paragraphs (0 = a free row)
W16V_VB   equ 4                 ;   ...its virtual base
W16V_LIN  equ 6                 ;   ...its linear base, 24 bits in a dword
W16V_SZ   equ 10
W16V_NC   equ 8                 ; cache rows, a power of two: a virtual
                                ;   paragraph and its selector each

    W16BSS w16_vmap, W16V_N * W16V_SZ
    W16BSS w16_vc, W16V_NC * 4

; W16PM_SEL sfx, dsload - a section's core: AX = a virtual paragraph -> AX =
; a data selector at it, to its claim's end (64 KB at most); AX in no claim
; comes back as it went. Keeps every other register; the flags are not kept.
; dsload is how DS = WIN16's is had for the map: `cs` in the image (its pair's
; data half is CS + 8), `none` in a part, whose callers have DS = ours.
%macro W16PM_SEL 2
w16v%1_sel:
    push bx
    push ds
  %ifidni %2, cs
    push ax
    mov ax, cs
    add ax, 8
    mov ds, ax
    pop ax
  %else
    mov ds, [cs:w16v%1_myds]        ; WIN16's, written when the part came
  %endif
    mov bx, ax
    and bx, W16V_NC - 1
    shl bx, 1
    shl bx, 1
    cmp ax, [w16_vc + bx]
    jne .miss
    cmp word [w16_vc + bx + 2], 0
    je .miss
    mov ax, [w16_vc + bx + 2]
    pop ds
    pop bx
    ret
.miss:
    push cx
    push dx
    push si
    mov si, w16_vmap
    mov cx, W16V_N
.row:
    mov dx, ax
    sub dx, [si + W16V_VB]
    jb .next
    cmp dx, [si + W16V_PARA]
    jb .hit
.next:
    add si, W16V_SZ
    loop .row
    jmp short .out                  ; a selector already
.hit:
    push bx
    push ax
    xchg ax, dx                     ; AX = paragraphs in
    mov dx, [si + W16V_SEL]
    xor cx, cx                      ; to the claim's end
    mov bl, SEG_SUB
    call OSAPI_SEG                  ; DX = the selector
    pop ax
    pop bx
    jc .out
    mov [w16_vc + bx], ax
    mov [w16_vc + bx + 2], dx
    mov ax, dx
.out:
    pop si
    pop dx
    pop cx
    pop ds
    pop bx
    ret
%endmacro

; W16PM_VOF sfx, dsload - AX = a selector -> AX = its virtual paragraph: the
; cache first (what W16SEG loaded is in it), then the map by its linear base
; (w16_pvirt); a selector of nobody's comes back as it went. Keeps every
; other register; the flags are not kept.
%macro W16PM_VOF 2
w16v%1_vof:
    push bx
    push cx
    push ds
  %ifidni %2, cs
    push ax
    mov ax, cs
    add ax, 8
    mov ds, ax
    pop ax
  %else
    mov ds, [cs:w16v%1_myds]
  %endif
    or ax, ax
    jz .out
    mov bx, w16_vc
    mov cx, W16V_NC
.c:
    cmp ax, [bx + 2]
    je .hit
    add bx, 4
    loop .c
  %ifidni %2, cs
    call w16_pvirt
  %else
    ICALL w16_pvirt
  %endif
    jmp short .out
.hit:
    mov ax, [bx]
.out:
    pop ds
    pop cx
    pop bx
    ret
%endmacro
%macro W16PM_STHK 2             ; sfx, reg: W16TOSEL's
  %ifdef w16vs_%1_%2
w16v%1_s%2:
    pushf
    %ifnidni %2, ax
    push ax
    mov ax, %2
    %endif
    call w16v%1_sel
    %ifnidni %2, ax
    mov %2, ax
    pop ax
    %endif
    popf
    ret
  %endif
%endmacro
%macro W16PM_VTHK 2             ; sfx, reg
  W16PM_STHK %1, %2
  %ifdef w16vo_%1_%2
w16v%1_o%2:
    pushf
    %ifnidni %2, ax
    push ax
    mov ax, %2
    %endif
    call w16v%1_vof
    %ifnidni %2, ax
    mov %2, ax
    pop ax
    %endif
    popf
    ret
  %endif
%endmacro

; W16PM_THUNKS sfx - the W16SEG entries that section used (W16SEG defines
; w16vu_<sfx>_<dst><src>), emitted after every use, as the export rows are
%macro W16PM_THK 3              ; sfx, dst, src
  %ifdef w16vu_%1_%2%3
w16v%1_%2%3:
    pushf
    push ax
    %ifnidni %3, ax
    mov ax, %3
    %endif
    call w16v%1_sel
    mov %2, ax
    pop ax
    popf
    ret
  %endif
%endmacro
%macro W16PM_THKM 2             ; sfx, dst: the word above the return address
  %ifdef w16vu_%1_%2m
w16v%1_%2m:
    pushf
    push ax
    push bp
    mov bp, sp
    mov ax, [ss:bp + 8]             ; bp, ax, flags, the return, the word
    call w16v%1_sel
    mov %2, ax
    pop bp
    pop ax
    popf
    ret 2
  %endif
%endmacro
%macro W16PM_THUNKS 1
  W16PM_THK %1, es, ax
  W16PM_THK %1, es, bx
  W16PM_THK %1, es, cx
  W16PM_THK %1, es, dx
  W16PM_THK %1, es, si
  W16PM_THK %1, es, di
  W16PM_THK %1, es, bp
  W16PM_THK %1, ds, ax
  W16PM_THK %1, ds, bx
  W16PM_THK %1, ds, cx
  W16PM_THK %1, ds, dx
  W16PM_THK %1, ds, si
  W16PM_THK %1, ds, di
  W16PM_THK %1, ds, bp
  W16PM_THKM %1, es
  W16PM_THKM %1, ds
  W16PM_VTHK %1, ax
  W16PM_VTHK %1, bx
  W16PM_VTHK %1, cx
  W16PM_VTHK %1, dx
  W16PM_VTHK %1, si
  W16PM_VTHK %1, di
  W16PM_VTHK %1, bp
%endmacro

; W16PM_PART sfx, dsload - a section's core and thunks, if it used W16SEG:
; emitted in that section after its last use (win16.asm's tail)
%macro W16PM_PART 2
  %ifnidni %2, cs
w16v%1_myds: dw 0                   ; WIN16's data selector (w16_pmds)
  %endif
  %ifdef w16vany_%1
    W16PM_SEL %1, %2
    W16PM_VOF %1, %2
    W16PM_THUNKS %1
  %endif
%endmacro

; W16PMDS sfx, sreg - the arm: part sfx, its data reachable through sreg,
; told WIN16's data selector (DS = ours). Nothing in real mode.
%macro W16PMDS 2
%ifdef PKG_MEGA
    mov [%2:w16v%1_myds], ds        ; XSEC-OK: the part's own word, written
%endif                              ;   through its data selector
%endmacro

; -----------------------------------------------------------------------------
; The image's own: the map's rows, and the edge. DS = ours for every one.
; -----------------------------------------------------------------------------
%macro W16PM_IMAGE 0

; w16_vclaim - AX = KB: OSAPI_MEM_CLAIM, the claim entered in the map. out:
; CF=0 DX = its virtual base; CF=1 none (no memory, or no room in the map).
; Keeps every other register.
w16_vclaim:
    call OSAPI_MEM_CLAIM
    jc .out
    push cx
    mov cx, ax
    call w16_vnew                   ; DX = the virtual base
    pop cx
    jnc .out
    call OSAPI_MEM_FREE             ; (DX = the claim still)
    stc
.out:
    ret

; w16_vfree - DX = a virtual base from w16_vclaim (or a selector, as it is):
; its row cleared, the claim given back. Keeps all.
w16_vfree:
    push dx
    call w16_vdel                   ; DX = the claim
    call OSAPI_MEM_FREE
    pop dx
    ret

; w16_vnew - DX = a selector, CX = KB: a row for it at the first virtual base
; that holds it, with the low five bits of its linear paragraph, so what
; WIN16 aligns to 512 bytes in virtual paragraphs is aligned in memory too
; (w16_kclaim: every disk-visible base). out: CF=0 DX = the base; CF=1 none,
; DX kept. Keeps the rest.
w16_vnew:
    push ax
    push bx
    push cx
    push si
    push di
    mov ax, cx
    mov cl, 6
    shl ax, cl                      ; paragraphs (a KB count is under 1024)
    mov cx, ax
    xor di, di                      ; DI = a free row
    mov si, w16_vmap
    mov bx, W16V_N
.free:
    cmp word [si + W16V_PARA], 0
    jne .used
    or di, di
    jnz .used
    mov di, si
.used:
    add si, W16V_SZ
    dec bx
    jnz .free
    or di, di
    jz .no
    push dx
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = its linear base
    mov [di + W16V_LIN], ax
    mov [di + W16V_LIN + 2], dx
    pop dx
    jc .no
    mov bx, W16V_LO                 ; BX = the candidate base
.try:
    push cx
    mov ax, [di + W16V_LIN]
    mov cl, 4
    shr ax, cl
    sub ax, bx
    and ax, 31
    pop cx
    add bx, ax                      ; ...on the linear base's phase
    jc .no
    mov ax, bx
    add ax, cx
    jc .no                          ; past FFFFh
    mov si, w16_vmap
    push cx
    mov cx, W16V_N
.ov:
    cmp word [si + W16V_PARA], 0
    je .ovn
    cmp ax, [si + W16V_VB]          ; ends at or below it: clear
    jbe .ovn
    push ax
    mov ax, [si + W16V_VB]
    add ax, [si + W16V_PARA]
    cmp bx, ax                      ; starts at or above its end: clear
    jae .ovp
    mov bx, ax                      ; overlaps: try past it
    pop ax
    pop cx
    jmp short .try
.ovp:
    pop ax
.ovn:
    add si, W16V_SZ
    loop .ov
    pop cx
    mov [di + W16V_SEL], dx
    mov [di + W16V_PARA], cx
    mov [di + W16V_VB], bx
    mov dx, bx
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; w16_vrow - DX = a virtual base -> SI = its row, CF=0; CF=1 none. Keeps all
w16_vrow:
    push cx
    mov si, w16_vmap
    mov cx, W16V_N
.r:
    cmp word [si + W16V_PARA], 0
    je .n
    cmp dx, [si + W16V_VB]
    je .y
.n:
    add si, W16V_SZ
    loop .r
    stc
.y:
    pop cx
    ret

; w16_vdel - DX = a virtual base -> DX = its claim's selector, the row
; cleared and the cache with it; a value in no row comes back as it went.
; Keeps the rest.
w16_vdel:
    push si
    call w16_vrow
    jc .out
    mov word [si + W16V_PARA], 0
    mov dx, [si + W16V_SEL]
    call w16_vcflush
.out:
    pop si
    ret

; w16_vcflush - every cache row emptied (a claim went). Keeps all.
w16_vcflush:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, w16_vc
    mov cx, W16V_NC * 2
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; w16_vfind - AX = a virtual paragraph -> SI = its row and DX = paragraphs
; into it, CF=0; CF=1 in none. Keeps the rest.
w16_vfind:
    push cx
    mov si, w16_vmap
    mov cx, W16V_N
.r:
    mov dx, ax
    sub dx, [si + W16V_VB]
    jb .n
    cmp dx, [si + W16V_PARA]
    jb .y
.n:
    add si, W16V_SZ
    loop .r
    stc
    pop cx
    ret
.y:
    clc
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16_psel - the EDGE, out (SPEC.md 124.31.3): AX = a virtual paragraph, CX =
; paragraphs it reaches (0 = to its claim's end, 64 KB at most), BL = 0 a
; data selector, 1 a code segment's -> AX = what the program is given: a data
; selector, or SEG_SUBC's code half. A value in no claim comes back as it
; went. CF=1 no LDT entry. Keeps every other register.
; -----------------------------------------------------------------------------
w16_psel:
    push bx
    push dx
    push si
    call w16_vfind                  ; SI = the row, DX = paragraphs in
    jc .as
    xchg ax, dx
    mov dx, [si + W16V_SEL]
    mov bh, bl
    mov bl, SEG_SUB
    or bh, bh
    jz .k
    mov bl, SEG_SUBC
.k:
    call OSAPI_SEG                  ; DX = the selector (a pair's data half)
    jc .out
    mov ax, dx
    or bh, bh
    jz .out
    sub ax, 8                       ; the code half
.as:
    clc
.out:
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16_pvirt - the EDGE, in: AX = a selector the program holds -> AX = the
; virtual paragraph it is based at, CF=0; CF=1 not in a claim of ours (or not
; on a paragraph), AX kept. A code half is its data half's paragraph too.
; Keeps every other register.
; -----------------------------------------------------------------------------
w16_pvirt:
    push bx
    push cx
    push dx
    push si
    push ax
    mov dx, ax
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = its linear base
    jc .no
    test al, 0x0F
    jnz .no
    mov si, w16_vmap
    mov cx, W16V_N
.r:
    cmp word [si + W16V_PARA], 0
    je .n
    push ax
    push dx
    sub ax, [si + W16V_LIN]
    sbb dx, [si + W16V_LIN + 2]     ; DX:AX = bytes in
    jb .n2
    mov bx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    dec bx
    jnz .sh
    or dx, dx
    jnz .n2
    cmp ax, [si + W16V_PARA]
    jae .n2
    add ax, [si + W16V_VB]          ; the virtual paragraph
    pop dx
    pop dx
    pop dx                          ; (the caller's AX)
    clc
    jmp short .out
.n2:
    pop dx
    pop ax
.n:
    add si, W16V_SZ
    loop .r
.no:
    pop ax
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_pspsel - AX = the PSP's selector (16 paragraphs). Keeps the rest.
w16_pspsel:
    push bx
    push cx
    mov ax, [w16_psp]
    mov cx, 16
    xor bl, bl
    call w16_psel
    pop cx
    pop bx
    ret

; w16_ssel - AX = one of the program's segments' paragraph, CL = 1 a code
; segment or 0 a data one, BX = a data one's index * 2 -> AX = the selector
; the program names it by (SPEC.md 124.31.4). A code segment's code half
; reaches its claim's end, so the loader, a thunk and the stack scan ask for
; the same one without the segment's length, which [w16_slen] has only while
; that segment is being laid; a data one reaches [w16_slen + BX]. Keeps the rest.
w16_ssel:
    push bx
    push cx
    push dx
    mov dl, cl
    xor cx, cx
    or dl, dl
    jnz .go
    mov cx, [w16_slen + bx]
    add cx, 15
    jc .all
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    jmp short .go
.all:
    xor cx, cx                      ; 64KB
.go:
    mov bl, dl
    call w16_psel
    pop dx
    pop cx
    pop bx
    ret

; w16_pin - AX = a global handle or segment the PROGRAM passed -> AX = WIN16's:
; a moveable handle (below W16V_LO) as it is, a selector its virtual
; paragraph (kept when it is nobody's). Keeps every other register.
w16_pin:
    cmp ax, W16V_LO
    jb .out
    call w16_pvirt
.out:
    ret

; w16_pout - AX = WIN16's global handle -> AX = the program's: a moveable one
; (below W16V_LO) or 0 as it is, a fixed one its block's selector (w16_pblk).
; Keeps every other register.
w16_pout:
    cmp ax, W16V_LO
    jb .out
    call w16_pblk
.out:
    ret

; w16_pblk - AX = a global block's data paragraph -> AX = a data selector
; reaching the block's own length (its header's GH_SIZE), as standard mode's
; GlobalLock answered. Keeps every other register.
w16_pblk:
    push bx
    push cx
    push es
    dec ax
    W16SEG es, ax
    mov cx, [es:GH_SIZE]
    inc ax
    dec cx
    xor bl, bl
    call w16_psel
    pop es
    pop cx
    pop bx
    ret

%endmacro

%endif ; PKG_MEGA
