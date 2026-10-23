; =============================================================================
; os8088 - tests/megaown/megaown.asm
;
; MEGAOWN, a TEST instrument (nothing under tests/ ships): kern_mega's pool
; compaction moving a package's OWN REGION while that very package is the
; caller (docs/plans/KERN-MEGA-PLAN.md 14.15's open item, 14.46). MEGACP
; (tests/megacp) proved the compactor moves claims, but its region sat below
; everything it claimed and never moved. This puts a HOLE under the region
; first: -DMO_HOLE builds MEGAHOLE, an OS88_HIGH | OS88_HIGHMEM package that
; holds three 60KB pool claims and closes its own window ten seconds later,
; and MEGARUN starts it before this one, so both its region and its claims
; lie below MEGAOWN's and are given back while MEGAOWN runs.
;
; MEGAOWN (OS88_HIGH | OS88_HIGHMEM too) waits fifteen seconds and then, in
; a wake handler, does MEGACP's four steps: 60KB claims until the pool takes
; no more (the first ones fill the hole under its region), every other one
; freed, a 64KB claim only a compaction can place, every survivor's stamps
; read back. Around them it walks the pool (OSAPI_XMEM_WALK) for the blocks
; owned 80h | slot - package regions above 1MB, and its own is the only one
; - and checks a stamp in its own bss and a word of its own code read
; through CS, after the call that moved them. Two seconds later a timer
; checks both again from a fresh callback and closes the window.
;
; Lines on port E9:
;   MEGAOWN: region OOOO KKKK      each region block (KB from the pool base)
;   MEGAOWN: ok | no CC            the four steps and the self checks
;   MEGAOWN: after ok | after no   the second callback
;   MEGAHOLE: held N | closing
; CC: 1 too few blocks, 2 the 64KB claim refused, 3 it came back
; conventional, 4 a stamp lost, 5 the bss stamp, 6 the CS word
; =============================================================================

%include "os88api.inc"

%ifdef MO_LOWHOLE
 %define MO_HOLE
%endif

%ifdef MO_LOWHOLE
    OS88_HEADER 'MEGALOWH', mo_entry, 0, OS88_STACK_192
%elifdef MO_LOWBIG
    OS88_HEADER 'MEGALOWB', mo_entry, 0, OS88_STACK_192 | OS88_HIGH
%elifdef MO_HOLE
    OS88_HEADER 'MEGAHOLE', mo_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM
%else
    OS88_HEADER 'MEGAOWN', mo_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM
%endif

MO_MAX  equ 32
MO_KB   equ 60
MO_TMAX equ 24
MO_WN   equ 24

mo_entry:
    push si
    push di
    mov si, mo_tpl
    call OSAPI_WM_CREATE
    jc .out
    OS88_REGION_MOVABLE
    mov [mo_win], bx
    mov ax, mo_tick
    call OSAPI_WM_ONTIMER
    mov ax, mo_wake
    call OSAPI_WM_ONWAKE
%ifdef MO_HOLE
    xor cx, cx
.h:
    mov ax, MO_KB
%ifdef MO_LOWHOLE
    call OSAPI_MEM_CLAIM_HI     ; conventional, TOP-DOWN: the ceiling
    jc .held
    mov si, cx                  ; one is enough: the hole is above the carve
    shl si, 1
    mov [mo_tab + si], dx
    inc cx
    jmp short .held
%else
    call OSAPI_MEM_CLAIM
    jc .held
%endif
    mov si, cx
    shl si, 1
    mov [mo_tab + si], dx
    inc cx
    cmp cx, 3
    jb .h
.held:
    mov si, mo_s_held
    call mo_say
    mov al, cl
    call mo_hex2
    call mo_nl
%ifdef MO_LOWHOLE
    mov ax, 455                 ; twenty-five seconds: CWORD has loaded
%else
    mov ax, 182                 ; ten seconds
%endif
%elifdef MO_LOWBIG
    mov ax, 728                 ; forty seconds
%else
    mov word [mo_stamp], 0xA55A
    mov ax, 273                 ; fifteen seconds
%endif
    call OSAPI_WM_TIMER
    clc
.out:
    pop di
    pop si
    ret

mo_paint:
    ret

mo_tick:                        ; under the gfx lock: post a wake
    push bx
    mov bx, [mo_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

%ifdef MO_LOWBIG
; the claim of everything MEM_AVAIL says a compaction would leave, with every
; claim on the machine printed before and after (OSAPI_CLAIM_SNAPSHOT):
; 'MEGALOWB: c SSSS PPPP OOOO' - segment, paragraphs, owner
mo_wake:
    call mo_snap
    call OSAPI_MEM_AVAIL
    mov si, mo_s_avail
    call mo_say
    call mo_hex4
    call mo_nl
    call OSAPI_MEM_CLAIM
    mov si, mo_s_no
    jc .say
    mov [mo_big], dx
    mov si, mo_s_got
    call mo_say
    mov ax, dx
    call mo_hex4
    call mo_nl
    call mo_snap
    mov dx, [mo_big]
    call OSAPI_MEM_FREE
    mov si, mo_s_done
.say:
    call mo_say
    call mo_nl
    mov bx, [mo_win]
    call OSAPI_WM_CLOSE
    ret

mo_snap:
    push ds
    pop es
    mov di, mo_snbuf
    call OSAPI_CLAIM_SNAPSHOT       ; AX = MEM_MAX
    mov cx, ax
    mov di, mo_snbuf
.l:
    cmp word [di+CLS_SEG], 0
    je .n
    mov si, mo_s_c
    call mo_say
    mov ax, [di+CLS_SEG]
    call mo_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [di+CLS_PARA]
    call mo_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [di+CLS_OWN]
    call mo_hex4
    call mo_nl
.n:
    add di, CLS_RECSZ
    loop .l
    mov si, mo_s_c
    call mo_say
    call mo_nl
    ret
%elifdef MO_HOLE
mo_wake:
    mov si, mo_s_close
    call mo_say
    call mo_nl
    mov bx, [mo_win]
    call OSAPI_WM_CLOSE         ; the claims go with the instance
    ret
%else
mo_wake:
    cmp byte [mo_phase], 0
    je mo_run
    mov si, mo_s_after          ; --- the second callback ---------------
    call mo_say
    call mo_self
    mov si, mo_s_okt
    jnc .s
    mov si, mo_s_not
.s:
    call mo_say
    call mo_nl
    mov bx, [mo_win]
    call OSAPI_WM_CLOSE
    ret

; mo_self - CF = 0 when the bss stamp and the code word read back, CL = the
; failing one (5, 6) otherwise
mo_self:
    mov cl, 5
    cmp word [mo_stamp], 0xA55A
    jne .no
    mov cl, 6
    cmp word [cs:mo_magic], 0x6D4F
    jne .no
    clc
    ret
.no:
    stc
    ret

mo_run:
    inc byte [mo_phase]
    push bp
    call mo_walk
    xor bp, bp                      ; --- 1. fill ---------------------------
.fill:
    cmp bp, MO_MAX
    jae .filled
    mov ax, MO_KB
    call OSAPI_MEM_CLAIM
    jc .filled
    test dl, 4
    jnz .sel
    call OSAPI_MEM_FREE             ; conventional: the pool is full
    jmp short .filled
.sel:
    mov bx, bp
    shl bx, 1
    mov [mo_tab + bx], dx
    mov es, dx
    mov ax, bp
    or ax, 0xC000
    mov [es:0], ax
    mov [es:30000], ax
    mov [es:MO_KB * 1024 - 2], ax
    inc bp
    jmp short .fill
.filled:
    mov [mo_n], bp
    mov cl, 1
    cmp bp, 3
    jb .no
    mov ax, 32                      ; ...and the tail, ever smaller
.tail:
    push ax
    call OSAPI_MEM_CLAIM
    jc .tnext
    test dl, 4
    jnz .tkeep
    call OSAPI_MEM_FREE
.tnext:
    pop ax
    shr ax, 1
    jnz .tail
    jmp short .tdone
.tkeep:
    mov bx, [mo_nt]
    cmp bx, MO_TMAX * 2
    jae .tfree
    mov [mo_ttab + bx], dx
    add word [mo_nt], 2
    pop ax
    jmp short .tail
.tfree:
    call OSAPI_MEM_FREE
    jmp short .tnext
.tdone:
    xor bx, bx                      ; --- 2. holes: free the odd ones -------
.free:
    test bl, 2
    jz .keep
    mov dx, [mo_tab + bx]
    call OSAPI_MEM_FREE
    mov word [mo_tab + bx], 0
.keep:
    add bx, 2
    mov ax, bp
    shl ax, 1
    cmp bx, ax
    jb .free
    mov ax, 64                      ; --- 3. the claim no hole holds -------
    call OSAPI_MEM_CLAIM
    mov cl, 2
    jc .no
    mov [mo_big], dx
    mov cl, 3
    test dl, 4
    jz .nofree
    mov es, dx
    mov word [es:0], 0x1234
    mov word [es:0xFFFE], 0x4321
    xor bx, bx                      ; --- 4. every survivor's stamps -------
.chk:
    mov dx, [mo_tab + bx]
    or dx, dx
    jz .cnext
    mov es, dx
    mov ax, bx
    shr ax, 1
    or ax, 0xC000
    mov cl, 4
    cmp [es:0], ax
    jne .nofree
    cmp [es:30000], ax
    jne .nofree
    cmp [es:MO_KB * 1024 - 2], ax
    jne .nofree
.cnext:
    add bx, 2
    mov ax, bp
    shl ax, 1
    cmp bx, ax
    jb .chk
    mov es, [mo_big]
    mov cl, 4
    cmp word [es:0], 0x1234
    jne .nofree
    cmp word [es:0xFFFE], 0x4321
    jne .nofree
    call mo_walk                    ; the region where the move put it...
    call mo_self                    ; ...and its own bytes, read through it
    jc .nofree
    mov si, mo_s_ok
    call mo_say
    call mo_nl
    mov cl, 0
.nofree:
    push cx
    mov dx, [mo_big]
    or dx, dx
    jz .nb
    call OSAPI_MEM_FREE
.nb:
    pop cx
    or cl, cl
    jz .fa
.no:
    push cx
    call mo_freeall
    pop cx
    mov si, mo_s_no
    call mo_say
    mov al, cl
    call mo_hex2
    call mo_nl
    jmp short .arm
.fa:
    call mo_freeall
.arm:
    pop bp
    mov bx, [mo_win]
    mov ax, 36                      ; two seconds: the second callback
    call OSAPI_WM_TIMER
    ret

mo_freeall:
    xor bx, bx
.t:
    cmp bx, [mo_nt]
    jae .c
    mov dx, [mo_ttab + bx]
    call OSAPI_MEM_FREE
    add bx, 2
    jmp short .t
.c:
    mov word [mo_nt], 0
    xor bx, bx
.l:
    cmp bx, MO_MAX * 2
    jae .o
    mov dx, [mo_tab + bx]
    or dx, dx
    jz .n
    call OSAPI_MEM_FREE
    mov word [mo_tab + bx], 0
.n:
    add bx, 2
    jmp short .l
.o:
    ret

; mo_walk - every pool block owned 80h | slot: 'MEGAOWN: region OOOO KKKK'
mo_walk:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, mo_wbuf
    mov cx, MO_WN
    call OSAPI_XMEM_WALK
    jc .o
    mov cx, ax
    mov di, mo_wbuf
.l:
    jcxz .o
    mov al, [di+XMW_OWN]
    and al, 0xE0
    cmp al, 0x80
    jne .n
    mov si, mo_s_reg
    call mo_say
    mov ax, [di+XMW_OFF]
    call mo_hex4
    mov al, ' '
    out 0xE9, al
    mov ax, [di+XMW_KB]
    call mo_hex4
    call mo_nl
.n:
    add di, XMW_SIZE
    dec cx
    jmp short .l
.o:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

mo_magic    dw 0x6D4F
%endif

mo_say:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mo_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mo_hex4:
    push ax
    mov al, ah
    call mo_hex2
    pop ax
mo_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

mo_tpl:
%ifdef MO_LOWBIG
    dw 400, 300, 200, 40
%elifdef MO_HOLE
    dw 8, 300, 200, 40
%else
    dw 230, 300, 200, 40
%endif
    dw mo_ttl, mo_paint, 0, 0
%ifdef MO_LOWBIG
mo_ttl:     db 'MegaLowB', 0
mo_s_c:     db 'MEGALOWB: c ', 0
mo_s_avail: db 'MEGALOWB: avail ', 0
mo_s_got:   db 'MEGALOWB: got ', 0
mo_s_no:    db 'MEGALOWB: refused', 0
mo_s_done:  db 'MEGALOWB: done', 0
%elifdef MO_LOWHOLE
mo_ttl:     db 'MegaLowH', 0
mo_s_held:  db 'MEGALOWH: held ', 0
mo_s_close: db 'MEGALOWH: closing', 0
%elifdef MO_HOLE
mo_ttl:     db 'MegaHole', 0
mo_s_held:  db 'MEGAHOLE: held ', 0
mo_s_close: db 'MEGAHOLE: closing', 0
%else
mo_ttl:     db 'MegaOwn', 0
mo_s_reg:   db 'MEGAOWN: region ', 0
mo_s_ok:    db 'MEGAOWN: ok', 0
mo_s_no:    db 'MEGAOWN: no ', 0
mo_s_after: db 'MEGAOWN: after', 0
mo_s_okt:   db ' ok', 0
mo_s_not:   db ' no', 0
%endif

    OS88_BSS 2 + 1 + 2 + MO_MAX * 2 + 2 + 2 + 2 + MO_TMAX * 2 + MO_WN * XMW_SIZE + CLAIM_SNAPSHOT_SIZE
    OS88_IMAGE_END

mo_win      equ os88_image_end + 0
mo_phase    equ os88_image_end + 2
mo_stamp    equ os88_image_end + 3
mo_tab      equ os88_image_end + 5
mo_n        equ mo_tab + MO_MAX * 2
mo_big      equ mo_n + 2
mo_nt       equ mo_big + 2
mo_ttab     equ mo_nt + 2
mo_wbuf     equ mo_ttab + MO_TMAX * 2
mo_snbuf    equ mo_wbuf + MO_WN * XMW_SIZE
