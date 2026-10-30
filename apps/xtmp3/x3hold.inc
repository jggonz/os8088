; =============================================================================
; os8088 - apps/xtmp3/x3hold.inc - THE HOLD (SPEC.md 121.6.1)
;
; %included by x3eng.inc. A live play reads NO DISK: the Video Player's rule
; for its desktop Live (SPEC.md 98.3.18.1 - "a read holds the picture"), here
; because a read holds sch_lock and the card plays its ring alone through
; it, and a floppy whose motor stopped pays a spin-up on the next refill
; (86Box's 386, round 4). Before a track's ladder or job, the WHOLE MP3 is
; read into a hold - one XMS block where OSAPI_XMEM_CAPS has its size, else a
; conventional claim where the heap holds it beside X3H_MARGKB and it is no
; larger than X3H_CAPKB, never on an 8086 - a time-boxed run of 32 KB
; READ_SEQ chunks each timer pass, under X3ST_LOAD ('Loading 42%'). From then
; on aplook.inc's gulps are COPIES out of it (x3_rseq, its AP_RSEQ): the
; refill path, the probe's priming, a seek's and a step's are all the same
; call. Anything else - no pool, too big, an 8086 - streams as before.
;
; THE KEY is the file: its folder, volume, name and size. A track that opens
; with the same key keeps the hold (a replay, a seek, a step, a Decode First
; after Stop); any other frees it first, and so do Stop and a load that
; fails. The kernel frees an XMS block and a claim with the instance.
;
; The conventional hold is only ever the first thing given up: x3_plan,
; asked for an XT plan the 400,000-byte ceiling cannot hold beside it, frees
; it and plans again (the ceiling stands; the hold is a convenience).
; =============================================================================

X3H_CAPKB   equ 192                 ; the conventional hold's most
X3H_MARGKB  equ 96                  ; ...and the heap it must leave free
X3H_PASSHI  equ 2                   ; a pass's load: chunks until 2 x 65,536
                                    ; pit counts (110 ms) have gone - the UI
                                    ; answers between passes on a slow disk

X3HK_NONE   equ 0
X3HK_XMS    equ 1
X3HK_CONV   equ 2

; -----------------------------------------------------------------------------
; x3h_begin - the current track, open, about to calibrate or convert. out:
; CF = 0 go on now (held already, or nothing to hold: it streams); CF = 1 a
; load has started (MD_LOAD), and x3h_tick goes on when it is in
; -----------------------------------------------------------------------------
x3h_begin:
    call x3h_same                   ; THIS file, held whole: nothing to do
    jnc .now
    call x3h_free
    mov si, x3_fname
    call x3_fsize                   ; DX:AX = its bytes
    jc .now
    mov [x3h_size], ax
    mov [x3h_size+2], dx
    mov bx, ax                      ; BX = its KB, rounded up (a 16-bit KB
    or bx, dx                       ; count: 64 MB, far past any pool)
    jz .now
    add ax, 1023
    adc dx, 0
    mov cx, 10
.kb:
    shr dx, 1
    rcr ax, 1
    loop .kb
    or dx, dx
    jnz .now
    mov bx, ax
    push bx
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can hand out (and BL,
    pop bx                          ; CX, DX its other answers: BX banked)
    cmp ax, bx
    jb .conv
    mov ax, [x3h_size]              ; one block, the file's size
    mov dx, [x3h_size+2]
    call OSAPI_XMEM_ALLOC
    jc .conv
    mov [x3h_base], ax
    mov [x3h_base+2], dx
    mov byte [x3h_kind], X3HK_XMS
    jmp short .go
.conv:
    push bx
    call OSAPI_CPU_INFO             ; never on an 8086: its rungs are the XT
    pop bx                          ; path, whose ceiling the hold would eat
    cmp al, CPU_8086
    je .now
    cmp bx, X3H_CAPKB
    ja .now
    push bx
    call OSAPI_MEM_AVAIL            ; AX = the largest run, BX = all, KB
    pop cx                          ; CX = ours
    cmp ax, cx
    jb .now
    sub bx, cx
    jb .now
    cmp bx, X3H_MARGKB
    jb .now
    mov ax, cx
    mov [x3h_ckb], cx               ; (its KB: a read's capacity stays in it;
    call OSAPI_MEM_CLAIM            ; banked first - the claim spends CX)
    jc .now
    mov [x3h_base], dx
    mov byte [x3h_kind], X3HK_CONV
    call x3_ldecl                   ; MOVABLE (x3_rmove), pinned per read
.go:
    call x3h_keyset
    xor ax, ax
    mov [x3h_have], ax
    mov [x3h_have+2], ax
    mov byte [x3h_on], al
    push di
    mov di, x3h_cur
    call os88_rseq_new
    pop di
    mov byte [x3_md], MD_LOAD
    mov byte [x3_st+X3S_STATE], X3ST_LOAD
    mov byte [x3_st+X3S_PCT], 0
    call x3_memck
    stc
    ret
.now:
    clc
    ret

; -----------------------------------------------------------------------------
; x3h_tick - MD_LOAD's pass: chunks until X3H_PASSHI has gone or the file is
; in. The file in: the ladder (or the job) the track was opened for. A read
; that fails: the hold given up, and the track streams as it always did
; -----------------------------------------------------------------------------
x3h_tick:
    call pit_now
    mov [x3h_t0], ax
    mov [x3h_t0+2], dx
.chunk:
    call x3h_read1                  ; CF = 1 failed; ZF = 1 the file is in
    jc .fail
    jz .done
    mov bx, x3h_t0
    call x3_pitd
    cmp dx, X3H_PASSHI
    jb .chunk
.pct:
    mov ax, [x3h_have]              ; 'Loading 42%': have x 100 / size, in
    mov dx, [x3h_have+2]            ; KB so it fits a word
    mov cx, 10
.s1:
    shr dx, 1
    rcr ax, 1
    loop .s1
    mov cx, 100
    mul cx
    mov bx, [x3h_size]
    mov cx, [x3h_size+2]
    push ax
    push dx
    mov ax, bx
    mov dx, cx
    mov cx, 10
.s2:
    shr dx, 1
    rcr ax, 1
    loop .s2
    mov bx, ax
    pop dx
    pop ax
    or bx, bx
    jz .p0
    cmp dx, bx
    jae .p0
    div bx
    mov [x3_st+X3S_PCT], al
.p0:
    clc
    ret
.fail:
    call x3h_free
.done:
    mov byte [x3_st+X3S_PCT], 100
    jmp x3l_start                   ; the track's ladder (or job), as x3_go

; x3h_read1 - one chunk of the file into the hold. out: CF = 1 a read failed;
; CF = 0 and ZF = 1 the whole file is in (x3h_on), ZF = 0 more to come
x3h_read1:
    mov ax, [x3h_have]
    mov dx, [x3h_have+2]
    cmp dx, [x3h_size+2]
    jne .more
    cmp ax, [x3h_size]
.more:
    jae .in
    mov es, [ap_la_seg]             ; THE BOUNCE: the look-ahead's claim (32
    xor bx, bx                      ; KB at a track's open), copied up after
    cmp byte [x3h_kind], X3HK_CONV
    jne .rd
    push ax                         ; ...or straight into a conventional hold:
    push dx                         ; its paragraph, the KB-multiple's
    mov cx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    call x3h_at                     ; (a window there: x3h_at, MC7)
    mov es, ax
    pop dx
    pop ax
.rd:
    mov cx, 32768
    cmp byte [x3h_kind], X3HK_CONV  ; a conventional hold's last chunk: no
    jne .cx                         ; more capacity than the claim has left
    push ax                         ; (a KB multiple: whole sectors fit it)
    push dx
    mov si, [x3h_ckb]               ; its bytes = KB x 1024, less the have
    mov ax, si
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr si, cl
    mov dx, si
    sub ax, [x3h_have]
    sbb dx, [x3h_have+2]
    mov cx, 32768
    or dx, dx
    jnz .cxo
    cmp ax, cx
    jae .cxo
    mov cx, ax
.cxo:
    pop dx
    pop ax
.cx:
    mov si, x3_fname
    mov di, x3h_cur
    call x3_lpin                    ; ES:BX: the bounce or the hold itself
    call os88_rseq                  ; DX:AX = bytes delivered
    call x3_ldecl
    push ds
    pop es
    jc .bad
    mov cx, ax
    or dx, dx
    jz .got
    mov cx, 32768
.got:
    jcxz .bad                       ; nothing where the size says there is
    cmp byte [x3h_kind], X3HK_XMS
    jne .adv
    push cx
    inc cx                          ; (an odd tail: one byte more of the
    and cl, 0xFE                    ; bounce, inside the KB-rounded block)
    push es
    mov es, [ap_la_seg]
    xor si, si
    mov ax, [x3h_base]
    mov dx, [x3h_base+2]
    add ax, [x3h_have]
    adc dx, [x3h_have+2]
    xor di, di                      ; out to extended
    call OSAPI_XMEM_COPY
    pop es
    pop cx
    jc .bad
.adv:
    add [x3h_have], cx
    adc word [x3h_have+2], 0
    or cx, cx                       ; ZF = 0: more, unless that was all
    clc
    ret
.in:
    mov byte [x3h_on], 1
    xor ax, ax                      ; ZF = 1, CF = 0
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; x3_rseq - aplook.inc's read (AP_RSEQ), os88_rseq's registers: SI -> the
; name, ES:BX the buffer, CX its capacity, DX:AX the offset, DI the cursor.
; out CF = 0, DX:AX = bytes delivered (0 at the end). THE HELD FILE is a copy
; out of the hold; any other read is the disk's (counted: x3_nrseq)
; -----------------------------------------------------------------------------
x3_rseq:
    cmp byte [x3h_on], 0
    je .disk
    cmp si, x3_fname
    jne .disk
    call x3h_same
    jc .disk
    push bx
    push cx
    push si
    push di
    push bp
    mov bp, cx                      ; BP = the capacity
    mov si, [x3h_size]              ; what is left from the offset
    mov di, [x3h_size+2]
    sub si, ax
    sbb di, dx
    jb .eof
    or di, di
    jnz .cap
    cmp si, bp
    jae .cap
    mov bp, si                      ; BP = the bytes delivered
.cap:
    or bp, bp
    jz .eof
    push ax
    push dx
    cmp byte [x3h_kind], X3HK_XMS
    jne .conv
    mov si, bx                      ; ES:SI = the buffer
    add ax, [x3h_base]
    adc dx, [x3h_base+2]
    mov cx, bp
    and cl, 0xFE                    ; the even part...
    jcxz .tail
    push ax
    push dx
    mov di, 1                       ; in from extended
    call OSAPI_XMEM_COPY
    pop dx
    pop ax
    jc .xbad
.tail:
    test bp, 1                      ; ...and an odd last byte, through a word
    jz .ok
    mov cx, bp
    and cl, 0xFE
    add ax, cx
    adc dx, 0
    push es
    push bx
    push ds
    pop es
    mov si, x3h_tw
    mov cx, 2
    mov di, 1
    call OSAPI_XMEM_COPY
    pop bx
    pop es
    jc .xbad
    mov al, [x3h_tw]
    mov di, bx
    add di, bp
    dec di
    mov [es:di], al
    jmp short .ok
.conv:
    mov si, ax                      ; DS:SI = the hold at the offset: its
    and si, 15                      ; paragraph through x3h_at's window, the
    mov cx, 4                       ; offset under 16
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    call x3h_at
    mov di, bx
    mov cx, bp
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
.ok:
    inc word [x3h_ncopy]
    pop dx
    pop ax
    mov ax, bp
    xor dx, dx
    clc
    jmp short .out
.xbad:
    pop dx                          ; a copy refused: the disk, after all
    pop ax
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    jmp short .disk
.eof:
    xor ax, ax
    xor dx, dx
    clc
.out:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret
.disk:
    inc word [x3_nrseq]
    call x3_lpin                    ; ES:BX the ring's, across READ_SEQ's
    call os88_rseq                  ; walk (a copy above claims nothing)
    jmp x3_ldecl                    ; (keeps the flags: CF the read's)

; x3h_same - CF = 0 the hold is THIS track's file, whole: its folder, volume
; and name (ap_trk_dir/ap_trk_vol, x3_fname). keeps everything but flags
x3h_same:
    cmp byte [x3h_on], 0
    je .no
    push ax
    push si
    push di
    mov ax, [ap_trk_dir]
    cmp ax, [x3h_dir]
    jne .ne
    mov al, [ap_trk_vol]
    cmp al, [x3h_vol]
    jne .ne
    mov si, x3_fname
    mov di, x3h_name
.c:
    mov al, [si]
    cmp al, [di]
    jne .ne
    inc si
    inc di
    or al, al
    jnz .c
    pop di
    pop si
    pop ax
    clc
    ret
.ne:
    pop di
    pop si
    pop ax
.no:
    stc
    ret

; x3h_at - AX = paragraphs into the CONVENTIONAL hold -> AX = a segment for
; NOW. The sum [x3h_base] + AX, asked of the kernel first (OSAPI_SEG's
; SEG_AT, apps/os88partsbody.inc's op_at): under kern_mega the hold is a
; selector and a sum on it names some other descriptor, so one window is
; re-aimed there (KERN-MEGA-MEMORY-PLAN MC7); every real-mode kernel refuses
; the verb with every register kept and the sum is the answer. No 8086 has
; a conventional hold (x3h_open), so the XT never pays the call. Every
; other register kept
x3h_at:
    push bx
    push cx
    push dx
    mov dx, [x3h_base]
    mov cx, [x3h_win]               ; the window to re-aim, 0 = a new one
    mov bl, SEG_AT
    call OSAPI_SEG
    jnc .w
    add dx, ax                      ; refused: a real-mode kernel's sum
.w:
    mov [x3h_win], dx
    xchg ax, dx
    pop dx
    pop cx
    pop bx
    ret

; x3h_keyset - the key: this track's folder, volume and name
x3h_keyset:
    push ax
    push cx
    push si
    push di
    mov ax, [ap_trk_dir]
    mov [x3h_dir], ax
    mov al, [ap_trk_vol]
    mov [x3h_vol], al
    mov si, x3_fname
    mov di, x3h_name
    mov cx, 12
    call x3_putn
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; x3h_free - the hold given back, whatever it was. keeps everything
x3h_free:
    push ax
    push dx
    mov al, [x3h_kind]
    mov byte [x3h_kind], X3HK_NONE
    mov byte [x3h_on], 0
    cmp al, X3HK_XMS
    jne .c
    mov ax, [x3h_base]
    mov dx, [x3h_base+2]
    call OSAPI_XMEM_FREE
    jmp short .o
.c:
    cmp al, X3HK_CONV
    jne .o
    mov dx, [x3h_base]
    call OSAPI_MEM_FREE
.o:
    pop dx
    pop ax
    ret
