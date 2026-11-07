; =============================================================================
; os8088 - apps/vbattle/vbcache.inc - THE CLIP CACHE
; (VBATTLE-XMS-PLAN, SPEC.md 126.4.6)
;
; Wherever a hold may be read IN PLACE (vbinp.inc's check, made at the
; hold's open: vb_hipok), the hold is a cache of whole clips rather than
; the builder's hold order. The block's first part is PINNED - the idle
; loop, read at the load and decoded where it lies, and the music when the
; pool has room to spare - and the rest is SLOTS, one per clip as it is
; wanted: a run the clip's own length at the cache's pointer, FIFO, every
; slot it overlaps evicted unless its clip is in use. A ring given a clip
; with a slot is in place on what has ARRIVED there (vb_iring), and the
; clip's stream fills the slot (vb_cfill) - so the reader runs as far
; ahead of the picture as the disk lets it, and a clip played once plays
; again with no read at all. When the whole file fits, the slots are the
; file's own places ([vb_cfix]) and nothing is evicted.
; =============================================================================

VB_ILOWH    equ 2               ; a ring in place is LOW under this many 64
                                ; KBs (128 KB) ahead of its leading cursor
VB_CMIN4    equ 64              ; the slots' least room, in 4 KBs (256 KB)

; vb_clen4 - SI = a clip's row: AX = its 4 KBs. CX, DX spent
vb_clen4:
    mov ax, [si+VC_LEN]
    mov dx, [si+VC_LEN+2]
; vb_b4k - DX:AX = bytes: AX = their 4 KBs, up. CX, DX spent
vb_b4k:
    add ax, 4095
    adc dx, 0
    mov cx, 12
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    ret

; vb_hipok - may the hold be read IN PLACE? (126.4.2's check, the whole
; file aside): CF=0 yes, and on XMS the yield sites live; CF=1 no. The
; UI task's
vb_hipok:
    cmp byte [vb_noinp], 0          ; (the A/B gate: the copying hold)
    jne .no
    cmp byte [vb_htype], 2
    jne .x
    cmp byte [vb_eq], 0x0F          ; EMS: the four quarters ours
    jne .no
    cmp byte [vb_eslow], 0          ; ...and the board as fast as RAM: the
    jne .no                         ; decode is the hook's time (98.3.18.8)
    clc
    ret
.x:
    call OSAPI_CPU_INFO             ; AL = the tier, AH = CPU_F_*
    test ah, CPU_F_PM               ; (kern_mega: a token is no address,
    jnz .no                         ; and neither borrow is real mode's)
    mov [vb_tier], al
    cmp al, CPU_286
    jb .no
    ja .chk
    mov byte [la_csacc], 0x93       ; a 286: LOADALL, probed
    call la_capture
    call la_probe
    jc .no
.chk:
    push ds                         ; the hold's first 16 bytes, by the
    pop es                          ; driver...
    mov si, vb_xsig
    mov ax, [vb_htok]
    mov dx, [vb_htok+2]
    mov cx, 16
    mov di, 1
    call OSAPI_XMEM_COPY
    jc .no
    mov ax, [vb_htok]               ; ...and in place
    mov dx, [vb_htok+2]
    mov [vb_xpc], ax
    mov [vb_xpc+2], dx
    mov dx, VB_XH
    xor si, si
    mov di, vb_xsig
    mov cx, 8
    push ds
    VB_DSDX
    cld
    repe cmpsw
    lahf
    VB_DSEND
    pop ds
    test ah, 0x40                   ; ZF: every word the same
    jz .no
    mov al, 1
    call vd_ypatch                  ; the yield sites live
    clc
    ret
.no:
    stc
    ret

; vb_ctry - the hold just opened ([vb_hblk] 4 KBs): a CACHE when it may be
; read in place and has the room, else the copying hold of the builder's
; order - or none, when that order had nothing ([vb_hpno]). CF=1 none
vb_ctry:
    mov byte [vb_cmode], 0
    mov byte [vb_iinp], 0
    cmp byte [vb_pmd], 0            ; (kern_mega's bracket: the copying hold)
    jne .copy
    call vb_hipok
    jc .copy
    call vb_cplan
    jc .copy
    mov byte [vb_hinp], 1           ; IN PLACE from the open on
    clc
    ret
.copy:
    cmp byte [vb_hpno], 0
    jne vb_hfree
    clc
    ret

; vb_hfree - the hold given back: none from here. CF=1 (vb_ctry's answer)
vb_hfree:
    cmp byte [vb_htype], 2
    je .e
    mov ax, [vb_htok]
    mov dx, [vb_htok+2]
    call OSAPI_XMEM_FREE
    xor dx, dx
    xchg dx, [vb_hbnc]
    or dx, dx
    jz .z
    call OSAPI_MEM_FREE
    jmp short .z
.e:
    mov ax, [vb_ehnd]
    mov bl, EMSV_FREE
    call vb_ems
    mov byte [vb_eq], 0
.z:
    mov byte [vb_hon], 0
    mov byte [vb_htype], 0
    stc
    ret

; vb_cplan - the CACHE's plan in a block of [vb_hblk] 4 KBs: the idle loop
; pinned first (range 0), the music after it when it streams and the pool
; has twice the largest clip to spare past it (range 1), the slots the
; rest; or, when the whole file fits, every range and slot at its own
; place in the file. CF=1 not room for the idle loop and VB_CMIN4 of slots
vb_cplan:
    call OSAPI_CPU_INFO
    mov [vb_ctier], al
    mov ax, [vb_meta+VH_TOTAL]      ; the file's 4 KBs
    mov dx, [vb_meta+VH_TOTAL+2]
    call vb_b4k
    mov [vb_cf4], ax
    mov di, VH_IDLE                 ; the idle loop's (paragraphs, 4 KB up)
    call vb_entpara
    mov cl, 8
    shr ax, cl
    mov [vb_ci4], ax
    mov word [vb_cm4], 0            ; the music's, when it streams
    cmp byte [vb_mres], 0
    jne .nm
    mov ax, [vb_meta+VH_MLEN]
    mov dx, [vb_meta+VH_MLEN+2]
    call vb_b4k
    mov [vb_cm4], ax
.nm:
    xor bx, bx                      ; the largest clip's
    xor ax, ax
    mov cx, [vb_meta+VH_NCLIPS]
.mx:
    push ax
    push cx
    call vb_crec
    call vb_clen4
    pop cx
    pop dx
    cmp ax, dx
    jae .mx1
    mov ax, dx
.mx1:
    inc bx
    loop .mx
    mov [vb_cbig4], ax
    mov si, vb_htab                 ; RANGE 0: the idle loop
    mov ax, [vb_meta+VH_IDLE]
    mov dx, [vb_meta+VH_IDLE+2]
    call vb_off4
    mov [si+HT_START], ax
    mov ax, [vb_ci4]
    mov [si+HT_LEN], ax
    mov word [si+HT_HAVE], 0
    mov ax, [vb_hblk]
    cmp ax, [vb_cf4]
    jb .slots
    mov byte [vb_cfix], 1           ; THE WHOLE FILE: every place its own
    mov ax, [si+HT_START]
    mov [si+HT_XOFF], ax
    mov byte [vb_hrn], 1
    cmp word [vb_cm4], 0
    je .fx
    add si, HT_SIZE
    mov ax, [vb_meta+VH_MUSIC]
    mov dx, [vb_meta+VH_MUSIC+2]
    call vb_off4
    mov [si+HT_START], ax
    mov [si+HT_XOFF], ax
    mov ax, [vb_cm4]
    mov [si+HT_LEN], ax
    mov word [si+HT_HAVE], 0
    mov byte [vb_hrn], 2
.fx:
    mov word [vb_cbase], 0
    mov ax, [vb_cf4]
    mov [vb_ctop], ax
    jmp short .done
.slots:
    mov byte [vb_cfix], 0
    mov dx, [vb_cmin]               ; the idle loop and the slots' least
    or dx, dx                       ; (VB_CMIN4, unless a gate's poke says:
    jnz .s0                         ; tests/vbthold.py's part, to evict)
    mov dx, VB_CMIN4
.s0:
    add dx, [vb_ci4]
    cmp ax, dx
    jb .no
    mov word [si+HT_XOFF], 0
    mov byte [vb_hrn], 1
    mov bx, [vb_ci4]                ; BX: the slots' start
    mov dx, ax                      ; the music too, when twice the largest
    sub dx, bx                      ; clip (and the least) is left past it
    sub dx, [vb_cm4]
    jbe .sb
    cmp word [vb_cm4], 0
    je .sb
    mov cx, [vb_cbig4]
    shl cx, 1
    jc .sb
    cmp cx, VB_CMIN4
    jae .s2
    mov cx, VB_CMIN4
.s2:
    cmp dx, cx
    jb .sb
    add si, HT_SIZE
    push ax
    mov ax, [vb_meta+VH_MUSIC]
    mov dx, [vb_meta+VH_MUSIC+2]
    call vb_off4
    mov [si+HT_START], ax
    mov [si+HT_XOFF], bx
    mov ax, [vb_cm4]
    mov [si+HT_LEN], ax
    add bx, ax
    pop ax
    mov word [si+HT_HAVE], 0
    mov byte [vb_hrn], 2
.sb:
    mov [vb_cbase], bx
    mov [vb_ctop], ax
.done:
    mov ax, [vb_cbase]
    mov [vb_cptr], ax
    mov ax, [vb_hblk]
    mov [vb_hkb4], ax
    mov byte [vb_hi], 0
    mov byte [vb_hfull], 0
    push ds
    pop es
    mov di, vb_cx                   ; no clip has a slot
    mov cx, VB_MAXCLIP
    mov ax, 0xFFFF
    cld
    rep stosw
    xor ax, ax
    mov [vb_cfkb], ax
    mov [vb_cev], ax
    mov [vb_cgen], al
    mov ax, [vb_htab+HT_XOFF]       ; the idle loop's place in the block
    call vb_h4k
    mov [vb_ixb], ax
    mov [vb_ixb+2], dx
    mov byte [vb_iinp], 1
    mov byte [vb_cmode], 1
    clc
    ret
.no:
    stc
    ret

; vb_off4 - DX:AX = a region's file offset (4 KB aligned): AX = its 4 KB.
; CL spent
vb_off4:
    mov cl, 12
    shr ax, cl
    mov cl, 4
    shl dx, cl
    or ax, dx
    ret

; vb_cbusy - AL = a clip: CF=1 it is IN USE - playing out of a ring, the
; action pending, the next opening, the end loop, under the cursor or the
; enemy's - and its slot is not to be taken. Every register kept
vb_cbusy:
    cmp al, [vb_pend]
    je .y
    cmp al, [vb_onext]
    je .y
    cmp al, [vb_eloop]
    je .y
    cmp al, [vb_want]
    je .y
    cmp al, [vb_eclip]
    je .y
    cmp al, [vb_rg0+RG_CLIP]
    jne .r1
    cmp byte [vb_rg0+RG_PLAY], 0
    jne .y
.r1:
    cmp al, [vb_rg1+RG_CLIP]
    jne .n
    cmp byte [vb_rg1+RG_PLAY], 0
    jne .y
.n:
    clc
    ret
.y:
    stc
    ret

; vb_cget - AL = a clip: CF=0 it has a slot (one is taken if it had none,
; evicting what it overlaps when [vb_cevict] - never a clip in use); CF=1
; none. From the UI task, or with interrupts off. Every register kept
vb_cget:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor ah, ah
    mov di, ax
    shl di, 1                       ; DI: the clip's word
    cmp word [vb_cx+di], 0xFFFF
    jne .yes
    mov bx, ax
    call vb_crec
    cmp byte [vb_cfix], 0
    je .fifo
    mov ax, [si+VC_OFF]             ; THE WHOLE FILE: its own place
    mov dx, [si+VC_OFF+2]
    call vb_off4
    mov [vb_cx+di], ax
    call vb_clen4
    mov [vb_cl4+di], ax
    mov word [vb_chave+di], 0
    jmp .yes
.fifo:
    call vb_clen4                   ; AX: its 4 KBs
    or ax, ax
    jz .no
    mov cx, [vb_ctop]
    sub cx, [vb_cbase]
    cmp ax, cx
    ja .no                          ; (longer than all the slots' room)
    mov [vb_cl], ax
    mov byte [vb_ctries], 4
    mov bx, [vb_cptr]               ; BX: where it goes
.place:
    mov ax, bx
    add ax, [vb_cl]
    jc .wrap
    cmp ax, [vb_ctop]
    jbe .scan
.wrap:
    mov bx, [vb_cbase]              ; (past the end: from the start)
.scan:
    mov dx, bx
    add dx, [vb_cl]                 ; DX: its end
    xor cx, cx
.s1:                                ; ANY SLOT IT OVERLAPS: in use, the run
    cmp cx, [vb_meta+VH_NCLIPS]     ; goes past it; else evicted below, if
    jae .take                       ; it may be
    mov si, cx
    shl si, 1
    mov ax, [vb_cx+si]
    cmp ax, 0xFFFF
    je .s2
    cmp ax, dx
    jae .s2
    add ax, [vb_cl4+si]
    cmp ax, bx
    jbe .s2
    cmp byte [vb_cevict], 0
    je .no
    mov al, cl
    call vb_cbusy
    jnc .s2
    mov bx, [vb_cx+si]              ; in use: past it
    add bx, [vb_cl4+si]
    dec byte [vb_ctries]
    jz .no
    jmp .place
.s2:
    inc cx
    jmp .s1
.take:
    xor cx, cx                      ; the overlapped EVICTED
.t1:
    cmp cx, [vb_meta+VH_NCLIPS]
    jae .t3
    mov si, cx
    shl si, 1
    mov ax, [vb_cx+si]
    cmp ax, 0xFFFF
    je .t2
    cmp ax, dx
    jae .t2
    add ax, [vb_cl4+si]
    cmp ax, bx
    jbe .t2
    mov word [vb_cx+si], 0xFFFF
    inc word [vb_cev]
    mov al, cl
    call vb_cdrop
.t2:
    inc cx
    jmp .t1
.t3:
    mov [vb_cx+di], bx
    mov ax, [vb_cl]
    mov [vb_cl4+di], ax
    mov word [vb_chave+di], 0
    mov [vb_cptr], dx
.yes:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; vb_cdrop - AL = a clip whose slot was just taken: a ring holding it (not
; playing - vb_cbusy said so) emptied. AX..DX kept
vb_cdrop:
    push ax
    push bx
    push cx
    push dx
    mov bx, vb_rg0
    call .r
    mov bx, vb_rg1
    call .r
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.r:
    cmp al, [bx+RG_CLIP]
    jne .out
    push ax
    mov al, 0xFF
    call vb_rreset
    pop ax
.out:
    ret

; vb_cpub - AL = a clip: every ring holding it IN PLACE told what its slot
; has (RG_FILL, its arrival or its length) - published whole, with
; interrupts off (the hook reads it). Every register kept
vb_cpub:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor ah, ah
    mov di, ax
    shl di, 1
    push ax
    mov bx, ax
    call vb_crec
    mov ax, [vb_chave+di]
    call vb_h4k
    cmp dx, [si+VC_LEN+2]
    jb .c
    ja .l
    cmp ax, [si+VC_LEN]
    jbe .c
.l:
    mov ax, [si+VC_LEN]
    mov dx, [si+VC_LEN+2]
.c:
    pop cx                          ; CL: the clip
    mov bx, vb_rg0
    call .r
    mov bx, vb_rg1
    call .r
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.r:
    cmp cl, [bx+RG_CLIP]
    jne .out
    cmp byte [bx+RG_INP], 0
    je .out
    pushf
    cli
    mov [bx+RG_FILL], ax
    mov [bx+RG_FILL+2], dx
    popf
.out:
    ret

; vb_cpc - CX = the 4 KBs a fill may move now: 8 (32 KB); while a menu
; waits on a key 4 on a 286 and up, 1 on an 8088 - what a key waits
; behind (126.4.6), unless a stream is running low ([vb_pbig])
vb_cpc:
    mov cx, 8
    cmp byte [vb_pbig], 0
    jne .r
    cmp byte [vb_state], GS_MENU
    je .m
    cmp byte [vb_state], GS_TARGET
    jne .r
.m:
    mov cx, 4
    cmp byte [vb_ctier], CPU_286
    jae .r
    mov cx, 1
.r:
    ret

; vb_cfill - AL = a clip with a slot, DI = the file cursor to read with:
; one piece of it off the disk into its slot (XMS: into the bounce and up;
; EMS: straight into quarter 3, a register the hook never writes), and the
; rings holding it told. Out AL = 1 a piece moved, 0 nothing (no slot, or
; all of it there); CF=1 a read or a copy failed. BX, SI, DI, BP kept
vb_cfill:
    push bx
    push si
    push di
    push bp
    xor ah, ah
    mov bp, ax                      ; BP: the clip
    mov bx, ax
    shl bx, 1                       ; BX: its word
    cmp word [vb_cx+bx], 0xFFFF
    je .none
    mov ax, [vb_cl4+bx]
    sub ax, [vb_chave+bx]
    jbe .none
    call vb_cpc
    cmp ax, cx
    jbe .k
    mov ax, cx
.k:
    cmp byte [vb_htype], 2          ; EMS: no further than its page
    jne .k1
    mov dx, [vb_cx+bx]
    add dx, [vb_chave+bx]
    and dx, 3
    mov cx, 4
    sub cx, dx
    cmp ax, cx
    jbe .k1
    mov ax, cx
.k1:
    mov [vb_ck], ax
    push bx
    mov bx, bp
    call vb_crec                    ; SI: its row
    pop bx
    mov ax, [vb_chave+bx]           ; the file offset: the clip's own on by
    call vb_h4k                     ; what has arrived
    add ax, [si+VC_OFF]
    adc dx, [si+VC_OFF+2]
    push ax
    mov ax, [vb_ck]
    mov cl, 12
    shl ax, cl
    mov cx, ax                      ; the bytes (8 x 4 KB = 8000h: 16 bits)
    pop ax
    push bx
    cmp byte [vb_htype], 2
    je .ems
    mov es, [vb_hbnc]               ; XMS: into the bounce...
    xor bx, bx
    jmp short .rd
.ems:
    push ax                         ; (the offset and the bytes, for after)
    push dx
    push cx
    mov ax, [vb_cx+bx]              ; EMS: its page into quarter 3, and the
    add ax, [vb_chave+bx]           ; disk read straight into it
    mov dx, ax
    and dx, 3                       ; (its 4 KB in the page)
    shr ax, 1
    shr ax, 1
    call vb_emap3
    jc .efail
    mov cl, 12
    shl dx, cl
    mov bx, dx                      ; BX: its place in the page
    mov ax, [vb_efseg]
    add ax, EMS_PAGE * 3 / 16
    mov es, ax
    pop cx
    pop dx
    pop ax
.rd:
    mov [vb_rcur], di
    mov byte [vb_hbyp], 1
    call vb_read
    mov byte [vb_hbyp], 0
    pop bx
    jc .fail
    cmp byte [vb_htype], 2
    je .up
    push bx                         ; ...and up into its slot
    mov ax, [vb_cx+bx]
    add ax, [vb_chave+bx]
    mov bx, [vb_ck]
    call vb_hup
    pop bx
    jc .fail
.up:
    mov ax, [vb_ck]
    add [vb_chave+bx], ax
    shl ax, 1
    shl ax, 1
    add [vb_cfkb], ax
    mov ax, bp
    call vb_cpub
    mov al, 1
    clc
    jmp short .out
.efail:
    pop cx
    pop dx
    pop ax
    pop bx
.fail:
    stc
    jmp short .out
.none:
    xor al, al
    clc
.out:
    pop bp
    pop di
    pop si
    pop bx
    ret

; vb_cone - AL = a clip (FFh none): its slot taken (evicting) and a piece
; of it filled, on the cache's own cursor. vb_cfill's out
vb_cone:
    cmp al, 0xFF
    je .none
    mov byte [vb_cevict], 1
    call vb_cget
    jc .none
    push di
    mov di, vb_ccsr
    call vb_cfill
    pop di
    ret
.none:
    xor al, al
    clc
    ret

; vb_cpre1 - the reader's turn after the enemy's head: the clip under the
; cursor WHOLE, then the enemy's (not before the battle, nor after it).
; vb_cfill's out
vb_cpre1:
    mov al, [vb_want]
    call vb_cone
    jc .out
    or al, al
    jnz .out
    cmp byte [vb_state], GS_INTRO
    je .out
    cmp byte [vb_state], GS_END
    je .out
    mov al, [vb_eclip]
    call vb_cone
.out:
    ret

; vb_cpre2 - the reader's last turn: every other clip in the table's order
; (an opening only before the battle), into room the pointer has never
; passed - nothing evicted. vb_cfill's out
vb_cpre2:
    mov byte [vb_cevict], 0
    xor cx, cx
.c:
    cmp cx, [vb_meta+VH_NCLIPS]
    jae .none
    mov bx, cx
    push cx
    call vb_crec                    ; (CL spent)
    pop cx
    test word [si+VC_FLAGS], CF_OPENING
    jz .t
    cmp byte [vb_state], GS_INTRO
    jne .n
.t:
    mov al, cl
    call vb_cget
    jc .n
    push cx
    mov di, vb_ccsr
    call vb_cfill
    pop cx
    jc .out
    or al, al
    jnz .out
.n:
    inc cx
    jmp .c
.none:
    xor al, al
    clc
.out:
    ret

; vb_hup - the bounce's first BX 4 KBs up into the block at its 4 KB AX, in
; copies of vb_hpiece. CF=1 a copy refused. AX..DX spent; SI, DI kept
vb_hup:
    call vb_h4k
    add ax, [vb_htok]
    adc dx, [vb_htok+2]
    push si
    push di
    xor si, si
.up:
    or bx, bx
    jz .upd
    call vb_hpiece                  ; a call's most, in 4 KBs: 8 or 1
    mov cl, ch
    xor ch, ch
    shr cl, 1
    shr cl, 1
    shr cl, 1
    shr cl, 1
    cmp bx, cx
    jae .u1
    mov cx, bx
.u1:
    sub bx, cx
    push bx
    mov bx, cx
    mov cl, 12
    shl bx, cl
    mov cx, bx                      ; its bytes
    push ax
    push dx
    push cx
    mov es, [vb_hbnc]
    xor di, di                      ; out, to extended
    call OSAPI_XMEM_COPY
    pop cx
    pop dx
    pop ax
    pop bx
    jc .ufail
    add si, cx
    add ax, cx
    adc dx, 0
    jmp .up
.upd:
    pop di
    pop si
    clc
    ret
.ufail:
    pop di
    pop si
    stc
    ret
