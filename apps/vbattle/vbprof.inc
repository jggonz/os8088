; =============================================================================
; os8088 - apps/vbattle/vbprof.inc
;
; THE PLAY'S PROFILE (SPEC.md 126.8): VBATTLED.O88, the engine built
; VB_DIAG, times every frame it draws and logs every read, and writes them
; beside itself as VBPROFn.DAT when the play ends - Video Player's VIDEOD
; (98.3.22) for the battle. `tools/vbtprof.py VBPROFn.DAT` reads it.
;
; PIT channel 2 runs free for the play (mode 2, 65,536 counts of 0.838 us;
; the speaker's data line off - the battle never sounds the speaker), and a
; frame's times are differences of its count, so a frame of 55 ms or more
; wraps. On kern_mega's real-mode bracket the claim would be a selector the
; hook cannot write through, so there the play is not profiled.
;
; The claim: 48 KB if the rings can still be sized beside it, else 24, 12
; or 6 (vb_prtry), or none - a big bundle's rings want the room, and a
; profile that took it refused the play ("Not enough memory", the owner).
; A VB_PRH-byte header, [vb_prna] frames of VB_PRE bytes, then [vb_prrl]
; reads of 8 at [vb_prrb] - an eighth of the claim.
;   frame: +0 its time in vb_frame (the decode), +2 the time since the
;          last frame's start, +4 flags (bit 0: STALLED - its record not
;          in its ring yet; bits 4..7 the mode), +5 the clip (FF: none),
;          +6 KB of the clip read ahead in its ring (255 at most), +7 the
;          card's lead in 256 bytes (255 at most), +8 the sound feed's
;          time since the last frame
;   read:  +0 the hook's period count as it began (low word), +2 the
;          periods it took, +4 its bytes, +6 its file offset in 4 KBs
;   header: 'VBPR', +4 version 1, +6 frames, +8 reads, +10 VB_DIV, +12 fps,
;          +14 the rate, +16 samples a frame, +18 the adapter (0 CGA4,
;          1 VGA4, 2 Hercules), +19 VGA's K, +20 frames late (forgiven),
;          +22 frames stalled, +24 periods (dword), +28 the reader's
;          periods (dword), +32 its bytes (dword), +36 its calls, +38 the
;          bundle's name (13), +51 the card (1: a card plays), +52 the
;          ring's KB, +54 VB_PRE, +56 the frames' room, +58 the claim's KB,
;          +60 1: kept in XMS, +62 frames dropped; version 2: +64 the
;          HOLD (vb_htype: 0 none, 1 XMS, 2 EMS), +65 it filled, +66 its
;          KB, +68 the reads it served (vb_hcopies), +70 its ranges, +71 a
;          card played (+51 is vb_snd at the end, when the card is closed),
;          +72 sound frames LATE (vb_alate: a record the sound reached before
;          it was read); a frame's flags bits 1-3 those since the last;
;          version 4: +74 keys timed, +76 the longest key-to-band (hook
;          periods), +78 their sum (dword), +82 the pool's KB, +84 the
;          cache on, +85 the whole file, +86 KB it filled, +88 rings in
;          place, +90 slots evicted, +92 clips whole at the end, +94 the
;          guard's KB, +95 the idle loop in the block (126.4.6)
; =============================================================================

VB_PRH      equ 96                  ; (version 2: 64 before the hold's line)
VB_PRE      equ 10
VB_PRKB     equ 48                  ; the claim tried first, halved to 6
; IN XMS (the owner: "a system with plenty of XMS ... so its more accurate
; as well - not cache starving anything"): the hook stages VB_PRSN frames
; in 4 KB of conventional memory, the play's foreground loop moves them up
; (vb_prdrain), a read goes up as it is logged, and the play's end brings
; it all down into a claim of its own, the rings given back by then
VB_PRSN     equ 400                 ; frames staged
VB_PRXF     equ 30000               ; frames in XMS (~33 min at 15 a second)
VB_PRXR     equ 4096                ; reads in XMS
VB_PRXRB    equ VB_PRH + VB_PRE * VB_PRXF   ; (the reads' first)
VB_PRXSZ    equ VB_PRXRB + 8 * VB_PRXR
VB_PRXKB    equ (VB_PRXSZ + 1023) / 1024

; vb_prtry - the rings, with a profile beside them if there is room for
; both: vb_rclaim with a profile of 48 KB, then 24, 12, 6, then none.
; vb_rclaim's own CF and [vb_err]
vb_prtry:
    mov byte [vb_prx], 0
    xor ax, ax
    mov [vb_prtot], ax
    mov [vb_prhi], ax
    mov [vb_prti], ax
    mov [vb_prdrop], ax
    mov [vb_prsnd], al
    mov [vb_prlat], ax
    mov [vb_prkn], ax               ; (the keys: none timed yet)
    mov [vb_prkmax], ax
    mov [vb_prksum], ax
    mov [vb_prksum+2], ax
    mov [vb_prkon], al
    cmp byte [vb_pmd], 0            ; (kern_mega: none)
    jne .conv
    call OSAPI_XMEM_CAPS            ; THE PROFILE IN XMS, when it is there:
    cmp ax, VB_PRXKB + 4            ; 4 KB of conventional memory beside the
    jb .conv                        ; rings, not 48
    mov ax, VB_PRXSZ & 0xFFFF
    mov dx, VB_PRXSZ >> 16
    call OSAPI_XMEM_ALLOC
    jc .conv
    mov [vb_prxt], ax
    mov [vb_prxt+2], dx
    mov ax, 4
    call OSAPI_MEM_CLAIM
    jc .xno
    mov [vb_prseg], dx
    mov byte [vb_prx], 1
    xor ax, ax
    mov [vb_prn], ax
    mov [vb_prnl], ax
    mov [vb_prfs], ax
    mov word [vb_prna], VB_PRXF
    mov word [vb_prrl], VB_PRXR
    mov word [vb_prkb], VB_PRXKB
    call vb_rclaim
    jnc .ok
    call vb_prxfree                 ; (no rings: none of it, the play's own
    stc                             ; refusal)
    ret
.xno:
    mov ax, [vb_prxt]
    mov dx, [vb_prxt+2]
    call OSAPI_XMEM_FREE
.conv:
    mov word [vb_prkb], VB_PRKB
.t:
    call vb_prbeg
    call vb_rclaim
    jnc .ok
    call vb_prfree
    shr word [vb_prkb], 1
    cmp word [vb_prkb], 6
    jae .t
    mov word [vb_prkb], 0           ; (none: the play, unprofiled)
    jmp vb_rclaim
.ok:
    ret

; vb_prxfree - the XMS profile's block and staging given back. Every
; register kept
vb_prxfree:
    push ax
    push dx
    cmp byte [vb_prx], 0
    je .c
    mov byte [vb_prx], 0
    mov ax, [vb_prxt]
    mov dx, [vb_prxt+2]
    call OSAPI_XMEM_FREE
.c:
    pop dx
    pop ax
    jmp vb_prfree

; vb_prdrain - the play's foreground: the frames the hook staged moved up
; into XMS. Every register kept
vb_prdrain:
    cmp byte [vb_prx], 0
    jne .go
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
.again:
    pushf
    cli
    mov bx, [vb_prn]
    popf
    sub bx, [vb_prtot]              ; staged, not yet up
    jz .done
    mov cx, VB_PRSN
    sub cx, [vb_prti]               ; as far as the staging's end
    cmp bx, cx
    jae .c
    mov cx, bx
.c:
    mov bx, cx                      ; BX: the frames this time
    mov ax, VB_PRE
    mul bx
    push ax                         ; (their bytes)
    mov ax, VB_PRE
    mul word [vb_prti]
    mov si, ax                      ; ES:SI the staging's
    mov ax, VB_PRE
    mul word [vb_prtot]             ; DX:AX the block's: past the header
    add ax, VB_PRH
    adc dx, 0
    add ax, [vb_prxt]
    adc dx, [vb_prxt+2]
    pop cx
    push bx
    mov es, [vb_prseg]
    xor di, di                      ; out, to extended
    call OSAPI_XMEM_COPY
    pop bx
    add [vb_prtot], bx
    mov ax, [vb_prti]
    add ax, bx
    cmp ax, VB_PRSN
    jb .w
    sub ax, VB_PRSN
.w:
    mov [vb_prti], ax
    jmp .again
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vb_prfree - the profile's claim given back. Every register kept
vb_prfree:
    push dx
    xor dx, dx
    xchg dx, [vb_prseg]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
.r:
    pop dx
    ret

; vb_prbeg - the profile's claim of [vb_prkb] KB, zeroed, and its room:
; frames, and an eighth of it for reads (none on kern_mega, or when there
; is no room). Every register kept
vb_prbeg:
    push ax
    push cx
    push dx
    push di
    push es
    mov word [vb_prseg], 0
    cmp byte [vb_pmd], 0            ; (kern_mega: a selector, not profiled)
    jne .out
    mov ax, [vb_prkb]
    or ax, ax
    jz .out
    call OSAPI_MEM_CLAIM
    jc .out
    mov [vb_prseg], dx
    mov ax, [vb_prkb]               ; reads: 16 a KB (an eighth of it)
    mov cl, 4
    shl ax, cl
    mov [vb_prrl], ax
    mov ax, [vb_prkb]               ; frames: the rest, less the header
    mov cl, 10
    shl ax, cl
    mov cx, ax
    shr cx, 1
    shr cx, 1
    shr cx, 1
    sub ax, cx
    sub ax, VB_PRH
    xor dx, dx
    mov cx, VB_PRE
    div cx
    mov [vb_prna], ax
    mul cx
    add ax, VB_PRH
    mov [vb_prrb], ax               ; (the reads' first: after the frames)
    mov es, [vb_prseg]
    xor di, di
    mov ax, [vb_prkb]
    mov cl, 9
    shl ax, cl
    mov cx, ax                      ; (words)
    xor ax, ax
    cld
    rep stosw
    mov [vb_prn], ax
    mov [vb_prnl], ax
    mov [vb_prfs], ax
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; vb_prgo - the clock: channel 2 run free. AX spent
vb_prgo:
    cmp word [vb_prseg], 0
    je .r
    in al, 0x61
    mov [vb_pr61], al
    and al, 0xFC                    ; the speaker's data off, the gate on
    or al, 1
    out 0x61, al
    mov al, 0xB4                    ; channel 2, both bytes, mode 2
    out 0x43, al
    xor al, al
    out 0x42, al
    out 0x42, al                    ; 65,536: free running
    call vb_prpit
    mov [vb_prta], ax
.r:
    ret

; vb_prpit - AX = channel 2's count, latched. Every other register kept
vb_prpit:
    pushf
    cli
    mov al, 0x80
    out 0x43, al
    in al, 0x42
    mov ah, al
    in al, 0x42
    popf
    xchg al, ah
    ret

; vb_prf0 - the hook: a frame begins. AX spent
vb_prf0:
    cmp word [vb_prseg], 0
    je .r
    call vb_prpit
    mov [vb_prtf], ax
.r:
    ret

; vb_prmark - the hook: a frame drawn (AL 1: it STALLED). Every register
; but AX kept
vb_prmark:
    cmp word [vb_prseg], 0
    je .r
    push bx
    push cx
    push dx
    push di
    push es
    mov cl, al
    mov bx, [vb_prn]
    cmp bx, [vb_prna]
    jae .o
    cmp byte [vb_prx], 0
    je .cv
    mov ax, bx                      ; XMS: the staging's next, unless the
    sub ax, [vb_prtot]              ; foreground has not moved it up
    cmp ax, VB_PRSN
    jb .xs
    inc word [vb_prdrop]
    jmp .o
.xs:
    mov ax, VB_PRE
    mul word [vb_prhi]
    mov di, ax
    inc word [vb_prhi]
    cmp word [vb_prhi], VB_PRSN
    jb .xw
    mov word [vb_prhi], 0
.xw:
    inc word [vb_prn]
    jmp short .wr
.cv:
    inc word [vb_prn]
    mov ax, VB_PRE
    mul bx
    add ax, VB_PRH
    mov di, ax
.wr:
    mov es, [vb_prseg]
    call vb_prpit
    mov dx, [vb_prtf]
    sub dx, ax                      ; a down-counter: before - after
    mov [es:di], dx
    mov dx, [vb_prta]
    sub dx, [vb_prtf]
    mov [es:di+2], dx
    mov ax, [vb_prtf]
    mov [vb_prta], ax
    mov ax, [vb_alate]              ; THE SOUND LATE since the last frame
    sub ax, [vb_prlat]              ; (bits 1-3, 7 at most): a record its
    add [vb_prlat], ax              ; sound reached before it was read
    cmp ax, 7
    jbe .l7
    mov ax, 7
.l7:
    shl al, 1
    or cl, al
    mov al, [vb_mode]
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or al, cl
    mov [es:di+4], al
    mov al, 0xFF
    cmp byte [vb_mode], VM_CLIP
    jne .c
    mov al, [vb_cc]
.c:
    mov [es:di+5], al
    xor al, al
    cmp byte [vb_mode], VM_CLIP
    jne .k
    push di
    mov bx, [vb_cring]
    mov di, RG_VOFF
    call vb_held                    ; DX: what is read past the cursor
    pop di
    mov al, dh
    shr al, 1
    shr al, 1
    cmp dx, 0xFC00
    jb .k
    mov al, 255
.k:
    mov [es:di+6], al
    xor al, al
    cmp byte [vb_snd], 0
    je .s
    mov byte [vb_prsnd], 1          ; (a card played: the header's +71)
    mov ax, [vb_wpos]               ; the card's lead
    sub ax, [vb_cons32]
    mov al, ah
.s:
    mov [es:di+7], al
    mov ax, [vb_prfs]
    mov [es:di+8], ax
    mov word [vb_prfs], 0
.o:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
.r:
    ret

; vb_prfa / vb_prfb - the hook's sound feed timed, summed to the next frame.
; AX spent
vb_prfa:
    cmp word [vb_prseg], 0
    je .r
    call vb_prpit
    mov [vb_prtg], ax
.r:
    ret
vb_prfb:
    cmp word [vb_prseg], 0
    je .r
    call vb_prpit
    neg ax
    add ax, [vb_prtg]
    add [vb_prfs], ax
.r:
    ret

; vb_prread - a read done (vb_dread): CX bytes asked at file offset DX:AX
; (4 KBs), [vb_rp0] the periods as it began. Every register kept
vb_prread:
    cmp word [vb_prseg], 0
    jne .go
    ret
.go:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, [vb_prnl]
    cmp bx, [vb_prrl]
    jae .o
    inc word [vb_prnl]
    mov [vb_prrbuf+4], cx
    mov al, ah                      ; the offset in 4 KBs: (DX:AX) >> 12
    mov ah, dl
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [vb_prrbuf+6], ax
    pushf
    cli
    mov ax, [vb_rp0]
    mov [vb_prrbuf], ax
    mov ax, [vb_hper]
    sub ax, [vb_rp0]
    mov [vb_prrbuf+2], ax
    popf
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; (its place: 8 bytes a read)
    cmp byte [vb_prx], 0
    jne .x
    add bx, [vb_prrb]
    mov di, bx
    mov es, [vb_prseg]
    mov si, vb_prrbuf
    mov cx, 4
    cld
    rep movsw
    jmp short .o
.x:
    mov ax, bx                      ; XMS: up, now (the foreground's)
    xor dx, dx
    add ax, VB_PRXRB & 0xFFFF
    adc dx, VB_PRXRB >> 16
    add ax, [vb_prxt]
    adc dx, [vb_prxt+2]
    push ds
    pop es
    mov si, vb_prrbuf
    mov cx, 8
    xor di, di
    call OSAPI_XMEM_COPY
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; vb_prend - the play over: the gate as it was, the header, VBPROFn.DAT
; (the first n from 1 not on the disk) and the claim given back. Every
; register kept
vb_prend:
    cmp word [vb_prseg], 0
    jne .go
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [vb_pr61]
    out 0x61, al
    call vb_prtgt                   ; ES: the file's bytes, [vb_prsz] its size
    jc .drop
    mov word [es:0], 'VB'
    mov word [es:2], 'PR'
    mov word [es:4], 4              ; (4: the cache and the keys, +74..95)
    mov ax, [vb_prn]
    mov [es:6], ax
    mov ax, [vb_prnl]
    mov [es:8], ax
    mov word [es:10], VB_DIV
    mov ax, [vb_meta+VH_FPS]
    mov [es:12], ax
    mov ax, [vb_meta+VH_RATE]
    mov [es:14], ax
    mov ax, [vb_meta+VH_SPF]
    mov [es:16], ax
    mov al, [vb_kind]
    mov ah, [vb_meta+349]
    mov [es:18], ax
    mov ax, [vb_late]
    mov [es:20], ax
    mov ax, [vb_stall]
    mov [es:22], ax
    mov ax, [vb_hper]
    mov [es:24], ax
    mov ax, [vb_hper+2]
    mov [es:26], ax
    mov ax, [vb_rper]
    mov [es:28], ax
    mov ax, [vb_rper+2]
    mov [es:30], ax
    mov ax, [vb_rbytes]
    mov [es:32], ax
    mov ax, [vb_rbytes+2]
    mov [es:34], ax
    mov ax, [vb_rcalls]
    mov [es:36], ax
    mov si, [vb_namep]
    mov di, 38
    mov cx, 12
    cld
.nm:
    lodsb
    or al, al
    jz .nz
    stosb
    loop .nm
.nz:
    mov al, [vb_snd]
    mov [es:51], al
    mov ax, [vb_rkb]
    mov [es:52], ax
    mov word [es:54], VB_PRE
    mov ax, [vb_prroom]
    mov [es:56], ax
    mov ax, [vb_prkb]
    mov [es:58], ax
    mov al, [vb_prx]
    mov [es:60], al                 ; +60 1: it was kept in XMS
    mov ax, [vb_prdrop]
    mov [es:62], ax                 ; +62 frames the staging had no room for
    xor al, al                      ; +64.. THE HOLD: was the bundle in XMS
    cmp byte [vb_hon], 0            ; (or EMS), how much, and how many reads
    je .nh                          ; it served
    mov al, [vb_htype]
.nh:
    mov [es:64], al
    mov al, [vb_hfull]
    mov [es:65], al
    mov ax, [vb_hkb4]
    shl ax, 1
    shl ax, 1
    mov [es:66], ax
    mov ax, [vb_hcopies]
    mov [es:68], ax
    mov al, [vb_hrn]
    mov [es:70], al
    mov al, [vb_prsnd]
    mov [es:71], al
    mov ax, [vb_alate]
    mov [es:72], ax                 ; +72 sound frames late, all the play
    mov ax, [vb_prkn]               ; VERSION 4: +74 keys timed to their
    mov [es:74], ax                 ; band's redraw, +76 the longest, +78
    mov ax, [vb_prkmax]             ; their sum (dword) - in the hook's
    mov [es:76], ax                 ; periods
    mov ax, [vb_prksum]
    mov [es:78], ax
    mov ax, [vb_prksum+2]
    mov [es:80], ax
    mov ax, [vb_cpool]              ; +82 the pool's KB at the open
    mov [es:82], ax
    mov al, [vb_cmode]              ; +84 the CACHE (126.4.6), +85 the whole
    mov ah, [vb_cfix]               ; file in it
    mov [es:84], ax
    mov ax, [vb_cfkb]               ; +86 KB it filled
    mov [es:86], ax
    mov ax, [vb_irings]             ; +88 rings given a clip in place
    mov [es:88], ax
    mov ax, [vb_cev]                ; +90 slots evicted
    mov [es:90], ax
    xor ax, ax                      ; +92 clips held whole at the end
    xor bx, bx
    mov cx, [vb_meta+VH_NCLIPS]
.cw:
    cmp word [vb_cx+bx], 0xFFFF
    je .cw1
    mov dx, [vb_chave+bx]
    cmp dx, [vb_cl4+bx]
    jb .cw1
    inc ax
.cw1:
    add bx, 2
    loop .cw
    mov [es:92], ax
    mov al, [vb_cgkb]               ; +94 the rings' guard, KB
    mov ah, [vb_iinp]               ; +95 the idle loop in the block
    mov [es:94], ax
    mov al, '1'
.try:
    mov [vb_prfn + 6], al
    push ax
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, vb_prfn
    call OSAPI_FILE_READ            ; zero bytes: is it there?
    xchg bx, ax
    pop ax
    jnc .next
    cmp bx, FERR_NOENT              ; only no such file is free
    je .free
.next:
    inc al
    cmp al, '9'
    jbe .try
    jmp short .drop                 ; nine already: none written
.free:
    mov cx, [vb_prsz]
    mov dx, [vb_prsz+2]
    mov es, [vb_prfseg]
    xor bx, bx
    mov si, vb_prfn
    call OSAPI_FILE_WRITE
.drop:
    cmp byte [vb_prx], 0            ; (XMS: the file's own claim, and the
    je .d1                          ; block)
    mov dx, [vb_prfseg]
    or dx, dx
    jz .d0
    call OSAPI_MEM_FREE
.d0:
    call vb_prxfree
.d1:
    call vb_prfree
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vb_prtgt - the file's bytes: the conventional claim as it stands, or - in
; XMS - the last frames moved up, and the header, the frames and the reads
; brought down into a claim of their own (the rings are given back by now).
; Out ES = [vb_prfseg] its segment, [vb_prsz] its size, [vb_prroom] the
; frames' room the reader reads by. CF=1 no room to bring it down
vb_prtgt:
    cmp byte [vb_prx], 0
    jne .x
    mov ax, [vb_prnl]
    mov cl, 3
    shl ax, cl
    add ax, [vb_prrb]
    mov [vb_prsz], ax
    mov word [vb_prsz+2], 0
    mov ax, [vb_prna]
    mov [vb_prroom], ax
    mov ax, [vb_prseg]
    mov [vb_prfseg], ax
    mov es, ax
    clc
    ret
.x:
    call vb_prdrain
    mov word [vb_prfseg], 0
.try:
    mov ax, [vb_prn]
    mov [vb_prroom], ax
    mov cx, VB_PRE                  ; the size: the header, the frames, the
    mul cx                          ; reads
    add ax, VB_PRH
    adc dx, 0
    mov bx, [vb_prnl]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add ax, bx
    adc dx, 0
    mov [vb_prsz], ax
    mov [vb_prsz+2], dx
    add ax, 1023                    ; in KB
    adc dx, 0
    mov cx, 10
.kb:
    shr dx, 1
    rcr ax, 1
    loop .kb
    call OSAPI_MEM_CLAIM
    jnc .got
    cmp word [vb_prn], 3000         ; (no room for all: the first 3,000)
    jbe .no
    mov word [vb_prn], 3000
    jmp .try
.no:
    stc
    ret
.got:
    mov [vb_prfseg], dx
    xor ax, ax                      ; THE FRAMES, down, 30,000 bytes a call
    xor dx, dx                      ; DX:AX: their offset
.fr:
    mov cx, [vb_prroom]
    push ax
    push dx
    mov ax, VB_PRE
    mul cx
    mov bx, ax
    mov cx, dx                      ; CX:BX all of them
    pop dx
    pop ax
    sub bx, ax
    sbb cx, dx                      ; ...left
    jnz .big
    cmp bx, 30000
    jbe .last
.big:
    mov bx, 30000
.last:
    or bx, bx
    jz .reads
    push ax
    push dx
    push bx
    call .dst                       ; ES:SI the file's, past the header
    add ax, VB_PRH                  ; DX:AX the block's, past the header
    adc dx, 0
    add ax, [vb_prxt]
    adc dx, [vb_prxt+2]
    mov cx, bx
    mov di, 1                       ; in, from extended
    call OSAPI_XMEM_COPY
    pop bx
    pop dx
    pop ax
    add ax, bx
    adc dx, 0
    jmp .fr
.reads:
    mov cx, [vb_prnl]               ; THE READS, after them: one call
    shl cx, 1
    shl cx, 1
    shl cx, 1
    jcxz .done
    call .dst                       ; (DX:AX = the frames' bytes)
    mov ax, VB_PRXRB & 0xFFFF
    mov dx, VB_PRXRB >> 16
    add ax, [vb_prxt]
    adc dx, [vb_prxt+2]
    mov di, 1
    call OSAPI_XMEM_COPY
.done:
    mov es, [vb_prfseg]             ; (the header: zeroed past what is set)
    xor di, di
    mov cx, VB_PRH / 2
    xor ax, ax
    cld
    rep stosw
    clc
    ret
.dst:                               ; ES:SI = the file's byte VB_PRH + DX:AX
    push ax                         ; (on kern_mega's 8-paragraph grid, as
    push dx                         ; an OS88_HIGH package's segments are -
    push cx                         ; tests/megagrid)
    add ax, VB_PRH
    adc dx, 0
    mov si, ax
    and si, 127
    mov cx, 7                       ; (DX:AX >> 7, then 8 paragraphs each)
.d:
    shr dx, 1
    rcr ax, 1
    loop .d
    mov cl, 3
    shl ax, cl
    add ax, [vb_prfseg]
    mov es, ax
    pop cx
    pop dx
    pop ax
    ret

vb_prfn:    db 'VBPROF1.DAT', 0

; vb_prkey - a key taken (vb_keys) AT A MENU: its time noted, for its
; band's redraw (version 4; the title's Enter is the openings' wait, not
; a menu's). Every register kept
vb_prkey:
    cmp byte [vb_state], GS_MENU
    je .k
    cmp byte [vb_state], GS_TARGET
    je .k
    ret
.k:
    push ax
    pushf
    cli
    mov ax, [vb_hper]
    popf
    mov [vb_prk0], ax
    mov byte [vb_prkon], 1
    pop ax
    ret

; vb_prkend - the band redrawn (vb_band): the key before it, timed. Every
; register kept
vb_prkend:
    cmp byte [vb_prkon], 0
    jne .go
    ret
.go:
    push ax
    mov byte [vb_prkon], 0
    pushf
    cli
    mov ax, [vb_hper]
    popf
    sub ax, [vb_prk0]
    inc word [vb_prkn]
    add [vb_prksum], ax
    adc word [vb_prksum+2], 0
    cmp ax, [vb_prkmax]
    jbe .m
    mov [vb_prkmax], ax
.m:
    pop ax
    ret
