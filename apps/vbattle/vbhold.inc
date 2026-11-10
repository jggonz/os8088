; =============================================================================
; os8088 - apps/vbattle/vbhold.inc - THE BUNDLE HELD IN EXTENDED MEMORY
; (VBATTLE-MEMORY-PLAN 4 and 5, SPEC.md 126.4.1). On a machine with an XMS
; pool (SPEC.md 41) the bundle - all of it when the pool can take it, else
; the ranges the builder's HOLD ORDER names, most worth holding first - is
; copied up a chunk at a time while the reader has nothing better to do, and
; every read lying wholly inside what has arrived is a copy instead of a
; disk read. An 8088 (no pool) is unchanged: one OSAPI_XMEM_CAPS at load.
; Video Player's 98.3.18 is the precedent; its lessons are the rules here:
; the whole pool, none kept back; the UI task alone copies; a cursor a hold
; read passes over is zeroed but for its offset, so a later disk read seeks
; from the name (the SDK's own seek, 18.4.8).
; =============================================================================

VB_HMAX     equ 16              ; ranges in the hold order (header 398..463;
                                ; format 9: VGA's palette is 464..511)
VH_HOLDN    equ 398             ; word: the hold order's ranges
VH_HOLD     equ 400             ; ...each (first 4 KB, 4 KBs), most worth first
VH_TOTAL    equ 52              ; dword: the file's length
HT_START    equ 0               ; a held range: its first 4 KB of the file,
HT_LEN      equ 2               ; ...its 4 KBs, its place in the block in 4
HT_XOFF     equ 4               ; KBs, and how many have arrived
HT_HAVE     equ 6
HT_SIZE     equ 8

; vb_hopen - at the first load: the hold, when there is a store. Below a
; 386 EXPANDED memory first (Video Player's 98.3.18.6: one copy with
; interrupts on, where XMS is int 15h's two with them off), XMS after; from
; a 386 XMS first. Nothing on failure: the disk serves
; THE BOARD IS TIMED FIRST (vb_espeed, Video Player's 98.3.18.8): one as
; fast as RAM keeps that order and is read in place; a slower one gives way
; to the pool, and without one is copied out of, never read in place; one
; slower than VB_ESLOW is not held in at all
vb_hopen:
    mov byte [vb_hon], 0
    mov byte [vb_htype], 0
    call vb_espeed
    call OSAPI_CPU_INFO
    cmp al, CPU_386
    jae .x
    cmp byte [vb_eslow], 0          ; (slower than RAM: the pool first)
    jne .x
    call vb_eopen
    jnc .ok
    call vb_xopen
    jnc .ok
    ret
.x:
    call vb_xopen
    jnc .ok
    cmp byte [vb_eslow], 2          ; (too slow to hold in at all)
    je .no
    call vb_eopen
    jnc .ok
    ret
.no:
    stc
    ret
.ok:
    jmp vb_ctry                     ; THE CACHE, where it may be (vbcache)

VB_EFAST    equ 15              ; the frame's read time in tenths of RAM's:
VB_ESLOW    equ 40              ; to 1.5x as RAM, to 4x copied, then none

; vb_espeed - [vb_eslow]: how the EMS frame READS against this package's
; own RAM (Video Player's vp_espeed) - 1 KB `rep lodsw` passes counted over
; two ticks each from a tick's edge, [vb_er10] the frame's time in tenths
; of RAM's: 0 within VB_EFAST, 1 within VB_ESLOW, 2 slower. No board: 0,
; nothing timed. [vb_eforce] (a test's poke), when not 0, is the answer
; less one after the measurement. A PicoMEM v1 answers with 8 wait states;
; a Lo-tech board on an XT is RAM's speed
vb_espeed:
    mov byte [vb_eslow], 0
    mov word [vb_er10], 0
    mov bl, EMSV_CAPS
    call vb_ems                     ; DX = the frame
    jc .f
    push dx
    mov ax, ds
    call vb_erate
    pop ax
    push bx                         ; RAM's passes
    call vb_erate                   ; BX = the frame's
    pop ax
    mov dx, 10
    mul dx                          ; DX:AX = RAM's x 10
    or bx, bx
    jz .slow
    cmp dx, bx
    jae .slow
    div bx                          ; AX = the frame's time, RAM's tenths
    mov [vb_er10], ax
    cmp ax, VB_EFAST
    jbe .f
    mov byte [vb_eslow], 1
    cmp ax, VB_ESLOW
    jbe .f
.slow:
    mov byte [vb_eslow], 2
.f:
    mov al, [vb_eforce]
    or al, al
    jz .r
    dec al
    mov [vb_eslow], al
.r:
    ret

; vb_erate - AX = a segment: BX = the 1 KB reads of it in two ticks, from
; a tick's edge. AX, CX, DX, SI spent
vb_erate:
    mov [vb_etseg], ax
    call OSAPI_GET_TICKS
    mov dx, ax
.w:
    call OSAPI_GET_TICKS
    cmp ax, dx
    je .w
    mov dx, ax
    xor bx, bx
.l:
    push ds
    mov ds, [vb_etseg]
    xor si, si
    mov cx, 512
    cld
    rep lodsw                       ; (one prefix: an interrupt resumes it)
    pop ds
    inc bx
    call OSAPI_GET_TICKS
    sub ax, dx
    cmp ax, 2
    jb .l
    ret

; vb_hplan - AX = the store's room in 4 KBs: the ranges (vb_htab), the file
; whole when it fits, else the hold order's while the room lasts; [vb_hkb4]
; their 4 KBs. CF=1 none
vb_hplan:
    mov cx, [vb_hcap]               ; (a gate's cap, in 4 KBs: tests/vbthold
    jcxz .cap                       ; holds the store under the bundle with
    cmp ax, cx                      ; it; nothing else writes it)
    jbe .cap
    mov ax, cx
.cap:
    mov [vb_hpool], ax
    mov ax, [vb_meta+VH_TOTAL]      ; the file in 4 KBs
    mov dx, [vb_meta+VH_TOTAL+2]
    add ax, 4095
    adc dx, 0
    mov cx, 12
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    mov byte [vb_hrn], 0
    mov word [vb_hkb4], 0
    cmp ax, [vb_hpool]
    ja .order
    mov si, vb_htab                 ; ALL OF IT: one range
    mov word [si+HT_START], 0
    mov [si+HT_LEN], ax
    mov word [si+HT_XOFF], 0
    mov word [si+HT_HAVE], 0
    mov [vb_hkb4], ax
    mov byte [vb_hrn], 1
    clc
    ret
.order:
    mov cx, [vb_meta+VH_HOLDN]      ; THE HOLD ORDER, while the room lasts
    cmp cx, VB_HMAX
    jbe .o0
    mov cx, VB_HMAX
.o0:
    or cx, cx
    jz .none
    mov bx, VH_HOLD
    mov si, vb_htab
.o:
    mov ax, [vb_meta+bx+2]          ; its 4 KBs, cut to what is left
    mov dx, [vb_hpool]
    sub dx, [vb_hkb4]
    jbe .ofull
    cmp ax, dx
    jbe .o1
    mov ax, dx
.o1:
    or ax, ax
    jz .onext
    mov [si+HT_LEN], ax
    mov dx, [vb_meta+bx]
    mov [si+HT_START], dx
    mov dx, [vb_hkb4]
    mov [si+HT_XOFF], dx
    mov word [si+HT_HAVE], 0
    add [vb_hkb4], ax
    add si, HT_SIZE
    inc byte [vb_hrn]
.onext:
    add bx, 4
    loop .o
.ofull:
    cmp byte [vb_hrn], 0
    je .none
    clc
    ret
.none:
    stc
    ret

; vb_hsz - after vb_hplan (its CF): [vb_hpno] 1 when the order had nothing,
; and [vb_hblk] the block to take - the whole pool, or the file when that
; is less, and never under the plan's: the cache (vbcache.inc) lays itself
; out in it when the hold may be read in place, and the copying hold uses
; the plan's part. CF=1 nothing to take
vb_hsz:
    sbb al, al
    and al, 1
    mov [vb_hpno], al
    mov ax, [vb_meta+VH_TOTAL]
    mov dx, [vb_meta+VH_TOTAL+2]
    call vb_b4k
    cmp ax, [vb_hpool]
    jbe .f
    mov ax, [vb_hpool]
.f:
    cmp ax, [vb_hkb4]
    jae .k
    mov ax, [vb_hkb4]
.k:
    mov [vb_hblk], ax
    or ax, ax
    jz .no
    clc
    ret
.no:
    stc
    ret

; vb_hon1 - the hold taken: its fill from the start, its own cursor
vb_hon1:
    mov byte [vb_hi], 0
    mov byte [vb_hfull], 0
    mov word [vb_hcopies], 0
    push ds
    pop es
    mov di, vb_hcur
    mov cx, FSEQ_SIZE / 2
    xor ax, ax
    cld
    rep stosw
    mov byte [vb_hon], 1
    clc
    ret

; vb_xopen - the hold in the XMS pool: one block, subdivided (41's rule),
; and a 32 KB bounce for the fill. CF=1 not taken
vb_xopen:
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can hand out
%ifdef VB_DIAG
    sub ax, VB_PRXKB + 4            ; (VBATTLED: the profile's, kept - 126.8)
    jbe .no
%endif
    or ax, ax
    jz .no
    mov [vb_cpool], ax              ; (the profile's: what the pool had)
    mov cl, 2                       ; in 4 KBs, one kept for the round-up
    shr ax, cl
    dec ax
    jz .no
    call vb_hplan
    call vb_hsz
    jc .no
    mov ax, [vb_hblk]
    call vb_h4k
    call OSAPI_XMEM_ALLOC
    jc .no
    mov [vb_htok], ax
    mov [vb_htok+2], dx
    mov ax, 32                      ; the fill's bounce - CONVENTIONAL (the
    xor cx, cx                      ; DMA door, no head): the bracket reads
    MEMKB_DMA                       ; into it in real mode
    jc .nob
    mov [vb_hbnc], dx
    mov ax, vb_reloc                ; (MOVABLE: only the fill's foreground
    call OSAPI_MEM_MOVABLE          ; copy reads it, inside one call)
    mov byte [vb_htype], 1
    jmp vb_hon1
.nob:
    mov ax, [vb_htok]
    mov dx, [vb_htok+2]
    call OSAPI_XMEM_FREE
.no:
    stc
    ret

; vb_eopen - the hold in EXPANDED memory (EMS.DRV, SPEC.md 107): a run of
; the board's pages, and the frame's quarters 0 and 1 - a 32 KB window the
; fill reads INTO and a read copies out of. CF=1 not taken
vb_eopen:
    mov bl, EMSV_CAPS
    call vb_ems                     ; AX = pages free, SI = free quarters
    jc .no
    and si, 3                       ; (quarters 0 and 1 both free)
    cmp si, 3
    jne .no
    dec ax                          ; (one over, for a window's second page)
    jle .no
    shl ax, 1
    shl ax, 1                       ; 16 KB pages to 4 KBs
    mov [vb_cpool], ax
    shl word [vb_cpool], 1
    shl word [vb_cpool], 1
    call vb_hplan
    call vb_hsz
    jc .no
    mov ax, [vb_hblk]
    add ax, 3
    shr ax, 1
    shr ax, 1
    inc ax                          ; (one over, for a window's second page,
    inc ax                          ; and one for in place's third)
    mov bl, EMSV_ALLOC
    call vb_ems                     ; AX = the handle
    jc .no
    mov [vb_ehnd], ax
    mov al, 0x0F                    ; ALL FOUR quarters, for the decode in
    mov [vb_eq], al                 ; place (vbinp.inc) - else the two the
    mov bl, EMSV_FRAME              ; copied hold needs
    call vb_ems
    jnc .fr
    mov al, 3
    mov [vb_eq], al
    mov bl, EMSV_FRAME
    call vb_ems                     ; DX = the frame's segment
    jc .nof
.fr:
    mov [vb_efseg], dx
    mov [vb_eport], cx              ; THE RECIPE (SPEC.md 107.4): quarter 0's
    mov [vb_estep], si              ; register, the step to the next, the
    mov [vb_eor], al                ; bits ORed into a page - the hook maps
    mov ax, [vb_ehnd]               ; with it, an ISR that may call nothing
    mov bl, EMSV_BASE
    call vb_ems                     ; AX = the run's first page on the board
    jc .nof
    mov [vb_ebase], ax
    mov byte [vb_htype], 2
    jmp vb_hon1
.nof:
    mov al, [vb_eq]                 ; the quarters given back (none taken:
    mov bl, EMSV_UNFRAME            ; the driver gives back only its
    call vb_ems                     ; caller's)
    mov byte [vb_eq], 0
    mov ax, [vb_ehnd]
    mov bl, EMSV_FREE
    call vb_ems
.no:
    stc
    ret

; vb_ems - OSAPI_DRV_CALL to EMS.DRV, BL the verb (Video Player's vp_ems)
vb_ems:
    mov bh, DRVC_EMS
    call OSAPI_DRV_CALL
    ret

; vb_emap - the hold's 16 KB page AX into quarter 0 and the next into 1:
; a 32 KB window at [vb_efseg]. Out CF=1 refused. Preserves all but flags
vb_emap:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    mov dx, [vb_ehnd]
    xor al, al
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
    jc .out
    inc cx
    mov al, 1
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vb_emap3 - the hold's 16 KB page AX into quarter 3: the UI task's window
; while rings are read in place (the hook maps 0-2). CF=1 refused. Preserves
; all but flags
vb_emap3:
    push ax
    push bx
    push cx
    push dx
    mov cx, ax
    mov dx, [vb_ehnd]
    mov al, 3
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vb_hpiece - CX = the copy's most a call: 32 KB with a card, whose DMA a
; mask does not stop; 4 KB under the PIT's clock, whose periods a mask
; loses (int 15h AH=87h holds interrupts off for the whole copy)
vb_hpiece:
    mov cx, 32768
    cmp byte [vb_snd], 0
    jne .c
    mov cx, 4096
.c:
    ret

; vb_hfind - DX:AX = a file offset, CX = bytes: CF=1 and DX:AX = its
; offset in the hold's block when all of it has arrived there; CF=0 not.
; CX kept; BX, SI spent
vb_hfind:
    push di
    push cx
    mov bx, ax
    and bx, 0x0FFF                  ; BX: the offset inside its 4 KB
    mov di, dx                      ; DI: the 4 KB it starts in
    push cx
    mov cl, 4
    shl di, cl
    mov cl, 12
    shr ax, cl
    or di, ax
    pop cx
    mov ax, bx                      ; SI: the 4 KBs it reaches over,
    xor dx, dx                      ; (offset + bytes + 4095) / 4096
    add ax, cx
    adc dx, 0
    add ax, 4095
    adc dx, 0
    mov cl, 12
    shr ax, cl
    mov cl, 4
    shl dx, cl
    or ax, dx
    mov si, ax
    mov cl, [vb_hrn]
    xor ch, ch
    push bx
    mov bx, vb_htab
.r:
    jcxz .no
    mov ax, di
    sub ax, [bx+HT_START]
    jb .n
    mov dx, ax
    add dx, si
    cmp dx, [bx+HT_HAVE]
    ja .n
    add ax, [bx+HT_XOFF]            ; FOUND: its 4 KB in the block
    pop bx
    call vb_h4k                     ; DX:AX = that x 4096 + the offset in it
    add ax, bx
    adc dx, 0
    pop cx
    pop di
    stc
    ret
.n:
    add bx, HT_SIZE
    dec cx
    jmp .r
.no:
    pop bx
    pop cx
    pop di
    clc
    ret

; vb_hread - vb_read's: DX:AX = file offset, CX bytes to ES:BX, out of the
; hold when it has all of them (CF=0, [vb_rcur]'s cursor zeroed but for
; its offset past the bytes, and [vb_rcur] back to vb_cur); else CF=1 and
; nothing done. SI, DI, BP, ES kept
vb_hread:
    push si
    push di
    push bp
    push bx                         ; [bp+6] the destination
    push ax                         ; [bp+4] the offset
    push dx                         ; [bp+2]
    push cx                         ; [bp]   the bytes
    call vb_hfind
    jnc .disk
    mov bp, sp
    mov si, [ss:bp+6]
    mov bx, [ss:bp]                 ; what is left
    cmp byte [vb_htype], 2
    je .ems
    add ax, [vb_htok]               ; XMS: the block's token on
    adc dx, [vb_htok+2]
.c:
    or bx, bx
    jz .done
    call vb_hpiece                  ; CX: the call's most
    cmp bx, cx
    jae .p
    mov cx, bx
.p:
    push ax
    push dx
    push cx
    push bx
    mov di, 1                       ; in, from extended
    VB_XCOPY
    pop bx
    pop cx
    pop dx
    pop ax
    jc .disk
    jmp short .adv
.ems:                               ; EMS: its page and the next mapped, and
    or bx, bx                       ; a copy out of the window
    jz .done
    push ax
    push dx
    mov cx, ax                      ; the offset in the page...
    and cx, 0x3FFF
    push cx
    mov cl, 14                      ; ...and the page
    shr ax, cl
    mov cl, 2
    shl dx, cl
    or ax, dx
    mov cx, 32768                   ; (the window: two pages...
    cmp byte [vb_hinp], 0
    jne .e3
    call vb_emap
    jmp short .e4
.e3:
    call vb_emap3                   ; ...or quarter 3 alone, in place: the
    mov cx, EMS_PAGE                ; hook's are 0-2)
.e4:
    pop di                          ; DI: the offset in the window
    pop dx
    pop ax
    jc .disk
    sub cx, di                      ; as much as the window holds past it
    cmp bx, cx
    jae .e1
    mov cx, bx
.e1:
    push ds
    push si
    push cx
    xchg si, di                     ; DS:SI the window, ES:DI the caller's
    mov ds, [vb_efseg]
    cmp byte [cs:vb_hinp], 0
    je .e5
    push ax
    mov ax, ds
    add ax, EMS_PAGE * 3 / 16       ; (quarter 3's paragraph)
    mov ds, ax
    pop ax
.e5:
    cld
    rep movsb
    pop cx
    pop si
    pop ds
.adv:
    sub bx, cx
    add si, cx
    add ax, cx
    adc dx, 0
    cmp byte [vb_htype], 2
    je .ems
    jmp .c
.done:
    inc word [vb_hcopies]
    pop cx
    pop dx
    pop ax
    pop bx
    add ax, cx                      ; the cursor: past the bytes, unseeded
    adc dx, 0
    mov di, [vb_rcur]
    xor cx, cx
    mov [di+0], cx
    mov [di+2], cx
    mov [di+4], cx
    mov [di+6], cx
    mov [di+8], cx
    mov [di+10], cx
    mov [di+FSEQ_OFF], ax
    mov [di+FSEQ_OFF+2], dx
    mov word [vb_rcur], vb_cur
    pop bp
    pop di
    pop si
    clc
    ret
.disk:
    pop cx
    pop dx
    pop ax
    pop bx
    pop bp
    pop di
    pop si
    stc
    ret

; vb_hstep - the reader's last turn: one chunk (32 KB, or a range's end) of
; the hold from the disk into the bounce and up into the block. Out AL = 1
; a chunk moved, 0 nothing to do; CF=1 a read failed (the hold is dropped:
; the disk serves). The UI task's
vb_hstep:
    cmp byte [vb_hon], 0
    je .none
    cmp byte [vb_hfull], 0
    jne .none
.find:
    mov al, [vb_hi]
    cmp al, [vb_hrn]
    jb .r
    mov byte [vb_hfull], 1          ; everything has arrived: read in place
    call vb_hipq                    ; from here on, if it may be
.none:
    xor al, al
    clc
    ret
.r:
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, vb_htab
    mov si, ax
    mov ax, [si+HT_LEN]
    sub ax, [si+HT_HAVE]
    jnz .some
    inc byte [vb_hi]
    jmp .find
.some:
    cmp ax, 8                       ; at most 32 KB
    jbe .k
    mov ax, 8
.k:
    cmp byte [vb_htype], 2          ; EMS: no further than its window - the
    jne .k1                         ; page the chunk starts in and the next
    mov bx, [si+HT_XOFF]
    add bx, [si+HT_HAVE]
    and bx, 3                       ; (its 4 KB in that page)
    mov cx, 8
    sub cx, bx
    cmp ax, cx
    jbe .k1
    mov ax, cx
.k1:
    mov [vb_hk], ax
    cmp byte [vb_htype], 2
    jne .x
    jmp vb_hstepe
.x:
    mov ax, [si+HT_START]           ; the file offset: (start + have) x 4 KB
    add ax, [si+HT_HAVE]
    call vb_h4k
    push si
    push ax
    mov ax, [vb_hk]                 ; the bytes (8 x 4 KB = 8000h: 16 bits)
    mov cl, 12
    shl ax, cl
    mov cx, ax
    pop ax
    mov es, [vb_hbnc]
    xor bx, bx
    mov word [vb_rcur], vb_hcur
    mov byte [vb_hbyp], 1
    call vb_read
    mov byte [vb_hbyp], 0
    pop si
    jc .fail
    mov ax, [si+HT_XOFF]            ; up into the block: (xoff + have) x 4 KB
    add ax, [si+HT_HAVE]
    mov bx, [vb_hk]
    push si
    call vb_hup
    jc .ufail
.upd:
    pop si
    mov ax, [vb_hk]
    add [si+HT_HAVE], ax
    mov al, 1
    clc
    ret
.ufail:
    pop si
.fail:
    cmp byte [vb_cmode], 0          ; (the cache keeps its hold: the idle
    jne .f1                         ; loop lives there - a failed read is
    mov byte [vb_hon], 0            ; the reader's, and ends the play) the disk
.f1:                                ; serves from here
    stc
    ret

; vb_hstepe - vb_hstep's for EXPANDED memory, SI = the range, [vb_hk] its
; 4 KBs this time: the page it lands in and the next mapped, and the disk
; read straight INTO the window - no bounce, no copy up (98.3.18.6's fill)
vb_hstepe:
    mov ax, [si+HT_XOFF]
    add ax, [si+HT_HAVE]
    mov bx, ax
    and bx, 3                       ; its 4 KB in the page: the window's
    shr ax, 1                       ; offset
    shr ax, 1
    mov dx, [vb_efseg]
    cmp byte [vb_hinp], 0           ; IN PLACE (the cache): QUARTER 3, a
    je .two                         ; page at most - the hook maps 0-2
    mov cx, 4
    sub cx, bx
    cmp [vb_hk], cx
    jbe .k3
    mov [vb_hk], cx
.k3:
    call vb_emap3
    jc .fail
    add dx, EMS_PAGE * 3 / 16
    jmp short .m
.two:
    call vb_emap
    jc .fail
.m:
    mov [vb_hes], dx
    mov ax, bx
    call vb_h4k
    mov bx, ax                      ; ES:BX = the window at it
    push si
    mov ax, [si+HT_START]           ; the file offset
    add ax, [si+HT_HAVE]
    call vb_h4k
    push ax
    mov ax, [vb_hk]
    mov cl, 12
    shl ax, cl
    mov cx, ax
    pop ax
    mov es, [vb_hes]
    mov word [vb_rcur], vb_hcur
    mov byte [vb_hbyp], 1
    call vb_read
    mov byte [vb_hbyp], 0
    pop si
    jc .fail
    mov ax, [vb_hk]
    add [si+HT_HAVE], ax
    mov al, 1
    clc
    ret
.fail:
    cmp byte [vb_cmode], 0
    jne .f1
    mov byte [vb_hon], 0
.f1:
    stc
    ret

; vb_h4k - DX:AX = AX x 4096. CL spent
vb_h4k:
    mov dx, ax
    mov cl, 4
    shr dx, cl
    mov cl, 12
    shl ax, cl
    ret
