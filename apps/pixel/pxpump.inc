; =============================================================================
; os8088 - apps/pixel/pxpump.inc
;
; THE DECODE PIPELINE (SPEC.md 106.9). PiXEL's WORKER decodes; the UI task is
; its FILE PUMP, because a worker may not touch a file, claim memory or fetch
; a part (SPEC.md 20.6 rule 7, 77.2). The handshake is the Audio Player's:
; a slot of the ring the worker has emptied has its `full` byte cleared and
; the request byte set LAST, then OSAPI_WM_WAKE; px_onwake fills every empty
; slot in stream order with OSAPI_FILE_READ_SEQ and clears the request byte
; LAST. A slot of 0 bytes is the end of the file.
;
; The worker never draws. It counts rows ([px_rdone]) and wakes the UI at
; most once a tick; the wake paints the rows that are new (pxview.inc).
; =============================================================================

PX_CHUNK    equ 16384               ; a ring slot, unless a cluster is bigger
PX_CHUNKLO  equ 8192                ; ...or the heap will not give 33 KB
PX_RINGKB   equ 17                  ; the ring the SCALE is decided against:
                                    ; 2 x 8 KB + the KB the 512 alignment may
                                    ; cost - the floor, so a bigger ring never
                                    ; changes which scale a picture gets
PX_HEADMAX  equ 2048                ; the bytes a header may be parsed from
PX_FLATWIN  equ 32768               ; an in-memory file, as the worker sees it
PXS_DEFER   equ px_pdefer           ; byte: px_pumpfill has deferred a fill
                                    ; for the first paint (the plan search's
                                    ; scratch is done with before any row)

JOB_NONE    equ 0
JOB_DECODE  equ 1                   ; tables, rows, histogram
JOB_TABLES  equ 2                   ; tables and histogram (a restored picture,
                                    ; or a display of another depth)

; =============================================================================
; CLAIMS FOR THE DISK (SPEC.md 2.1.1): DMA-safe, and 512-aligned by hand
; =============================================================================

; px_dmaclaim - AX = KB of disk buffer: claims one KB more, page-safe, and
; rounds the base up to 32 paragraphs. out CF = 0, DX = the aligned
; segment, BX = the claim (what to free); CF = 1 refused. Preserves AX, CX
px_dmaclaim:
    push ax
    push cx
    inc ax
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA
    jc .out
    mov bx, dx
    add dx, 31
    and dx, 0xFFE0
    clc
.out:
    pop cx
    pop ax
    ret

; px_clbytes - AX = the volume's cluster in bytes (512 if it will not say).
; Preserves all but AX
px_clbytes:
    cmp byte [px_clok], 0           ; ONE VOL_STAT an open (its FAT walk is
    je .ask                         ; ~105 ms on a 20 MB disk; wave-1 review
    mov ax, [px_clsz]               ; MIN-7) - not one a caller, and not one
    ret                             ; a session: a swapped floppy may differ
.ask:
    push bx
    push cx
    push dx
    call OSAPI_VOL_STAT             ; AX = sectors a cluster, CX = bytes each
    jc .def
    mul cx
    or dx, dx
    jnz .def
    cmp ax, 512
    jae .out
.def:
    mov ax, 512
.out:
    mov [px_clsz], ax
    mov byte [px_clok], 1
    pop dx
    pop cx
    pop bx                          ; (VOL_STAT clobbers it: free clusters)
    ret

; =============================================================================
; THE RING (SPEC.md 106.9)
; =============================================================================

; px_ringclaim - the ring: two 16 KB slots - or two of 8 KB when the heap
; will not give 33, or one cluster-sized one where a cluster is bigger. A
; read is priced per int 13h and not per sector (CLAUDE.md), and a 16 KB one
; crosses a cylinder about half as often a sector as an 8 KB one; with both
; slots empty px_pumpfill reads the two as ONE call (SPEC.md 106.9).
; CF = 1 refused. Preserves all
px_ringclaim:
    push ax
    push bx
    push cx
    push dx
    mov cx, PX_CHUNK
.try:
    call px_clbytes                 ; AX = the cluster
    mov byte [px_nslot], 2
    cmp ax, cx
    jbe .c
    mov cx, ax                      ; a big cluster: one slot of it
    mov byte [px_nslot], 1
.c:
    mov [px_chunk], cx
    mov ax, cx
    cmp byte [px_nslot], 1
    je .one
    shl ax, 1
.one:
    add ax, 1023
    rcr ax, 1                       ; bytes -> KB, up (a carry is bit 16)
    mov cl, 9
    shr ax, cl
    call px_dmaclaim
    jnc .have
    cmp byte [px_nslot], 1          ; a cluster-sized slot has no smaller form
    je .no
    mov cx, PX_CHUNKLO              ; the smaller ring, if the bigger did not
    cmp [px_chunk], cx              ; fit
    ja .try
.no:
    stc
    jmp short .out
.have:
    mov [px_ringraw], bx
    mov [px_sseg], dx
    mov ax, [px_chunk]
    mov cl, 4
    shr ax, cl
    add dx, ax
    mov [px_sseg + 2], dx
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_pumpinit - a fresh stream: every slot empty, both ends at offset 0, the
; READ_SEQ cursor cold. Preserves all
px_pumpinit:
    push ax
    push di
    xor ax, ax
    mov [px_sfull], ax
    mov [px_slen], ax
    mov [px_slen + 2], ax
    mov [px_fslot], al
    mov [px_wslot], al
    mov [px_feof], al
    mov [px_rerr], al
    mov [px_rq], al
    mov [px_rhave], al
    mov [px_rpos], ax
    mov [px_rend], ax
    mov [px_fk], ax
    push bx                         ; THE BASE (106.25): 0, a PNG inside an
    push cx                         ; ICO, a GIF's next frame. A read starts
    push dx                         ; on a cluster (READ_SEQ's rule), so the
    mov dx, [px_sbase + 2]          ; stream starts at the base's cluster and
    mov bx, [px_sbase]              ; the first window drops what is before
    mov cx, bx                      ; the base (px_rnext)
    call px_clbytes                 ; AX = the cluster, a power of two
    dec ax
    and bx, ax
    mov [px_rdrop], bx
    not ax
    and cx, ax
    mov [px_rbase], cx
    mov [px_rnxt], cx
    mov [px_fabs], cx
    mov [px_rbase + 2], dx
    mov [px_rnxt + 2], dx
    mov [px_fabs + 2], dx
    pop dx
    pop cx
    pop bx
    xor ax, ax
    mov [PXS_DEFER], al             ; (px_pumpfill's one deferral, unspent)
    mov di, px_rcur
    call os88_rseq_new
    pop di
    pop ax
    ret

; px_pumpfill - UI TASK, NO LOCK: fill every empty slot, in stream order,
; from where the file was left (SPEC.md 106.9). A slot past the end is
; marked full with 0 bytes, which is the worker's end of file. A read error
; is an end of file too, with [px_rerr] set, so the decoder's "cut short"
; becomes the caller's "disk read failed". Clears the request byte LAST
px_pumpfill:
    cmp byte [px_busy], 1           ; THE FIRST ROWS BEFORE THE NEXT READ:
    jne .fill                       ; px_onwake fills, then paints, and a
    test byte [PXS_DEFER], 2        ; (not for a re-decode that keeps the
    jnz .fill                       ; view: its first rows may be far from
                                    ; it, review-w5 F8)
    push ax                         ; 16 KB read in front of the first paint
    call px_onglass                 ; is a second and more on an XT. So until
    jc .painted                     ; something is painted the ring is filled
    cmp word [px_rdone], 0          ; only when the worker has run DRY (its
                                    ; next slot empty), not ahead of it -
    jne .wait                       ; unless rows are decoded and NONE is
    cmp word [px_srcdone], 0        ; showable yet (an interlaced picture's
    jne .painted                    ; early passes, SPEC.md 106.18): then no
.wait:                              ; paint is coming to wait for...
    push bx
    mov bl, [px_wslot]
    xor bh, bh
    cmp byte [px_sfull + bx], 0
    pop bx
    je .dry
    pop ax
    ret                             ; (it wakes us when it has: px_rnext)
.dry:
    cmp byte [px_tabok], 0          ; ...and the first wake that finds rows
    je .painted                     ; ready and none painted fills nothing
    cmp word [px_rdone], 0          ; and wakes again - once an open
    je .painted                     ; ([px_pumpinit]), so a zoom that paints
    cmp byte [PXS_DEFER], 0         ; no screen row of one master row cannot
    jne .painted                    ; hold the disk off for ever
    mov byte [PXS_DEFER], 1
    pop ax
    jmp px_wake
.painted:
    pop ax
.fill:
    push ax
    xor al, al
    call px_pumpn
    pop ax
    ret

; px_onglass - CF = 1 when a row of the picture being decoded has reached
; the glass: the rows the wake has painted from the top ([px_ypaint]) or
; the bottom ([px_ypbot]) reach the canvas ([px_midy1] .. [px_midy2]).
; Those counters are moved before the span is clipped to the canvas, and at
; Fit the picture's last screen row can be one past [px_midy2] - so a first
; wake that "painted" only that row left [px_ypbot] at 1, and the deferral
; above read 32 KB in front of the first row that did reach the glass: BIG.BMP's
; first rows were 3.6-4.1 s on the 5150 against 1.2-1.5 (SPEC.md 106.22).
; Preserves all
px_onglass:
    push ax
    push bx
    mov ax, [px_ypaint]             ; from the top: rows iy .. iy + ypaint - 1
    or ax, ax
    jz .bot
    add ax, [px_vcan + VW_IY]
    dec ax
    cmp ax, [px_midy1]
    jge .yes
.bot:
    mov ax, [px_ypbot]              ; from the bottom: iy + dh - ypbot ..
    or ax, ax
    jz .no
    mov bx, [px_vcan + VW_IY]
    add bx, [px_vcan + VW_DH]
    sub bx, ax
    cmp bx, [px_midy2]
    jle .yes
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop bx
    pop ax
    ret

; px_pumpone - the same, ONE slot of at most 8 KB: the open's first fill, so
; the first rows wait on what they did before the ring grew, and the rest of
; the file comes 16 KB at a time from the first wake on. (Without
; px_pumpfill's deferral this LOST: the second read landed in front of the
; first paint, and a bottom-up BMP's first rows came 1.8 s later.)
; Preserves all
px_pumpone:
    push ax
    mov al, 1
    call px_pumpn
    pop ax
    ret

; px_pumpn - px_pumpfill's body: AL = 1 for one slot only, of 8 KB at most;
; 2 for one whole slot
px_pumpn:
    cmp byte [px_rflat], 0
    jne .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    mov bp, ax                      ; BP's low byte: one slot, and short
    mov cl, [px_nslot]
    xor ch, ch
    or al, al
    jz .all
    mov cl, 1                       ; (one slot: the loop below, once)
    jmp short .slot
.all:
    cmp cl, 2                       ; BOTH slots empty, in memory order: one
    jne .slot                       ; read fills the two (they are adjacent),
    cmp byte [px_fslot], 0          ; which is one rotational wait the disk
    jne .slot                       ; does not spend
    cmp word [px_sfull], 0
    jne .slot
    cmp byte [px_feof], 0
    jne .slot
    mov es, [px_sseg]
    mov cx, [px_chunk]
    shl cx, 1
    call px_rseq                    ; AX = bytes delivered (0 on an error)
    mov cx, [px_chunk]
    xor bx, bx
    cmp ax, cx                      ; the first slot: up to a chunk of it...
    jbe .two
    mov bx, ax
    sub bx, cx                      ; ...the second, the rest
    mov ax, cx
.two:
    mov [px_slen], ax
    mov [px_slen + 2], bx
    or bx, bx
    jnz .both
    mov byte [px_feof], 1           ; (an empty second slot is the end)
.both:
    mov word [px_sfull], 0x0101     ; ...the slots' LAST field
    jmp short .done
.slot:
    mov bl, [px_fslot]
    xor bh, bh
    cmp byte [px_sfull + bx], 0
    jne .done                       ; the worker still has it: nothing to do
    shl bx, 1
    xor ax, ax
    cmp byte [px_feof], 0
    jne .mark
    push cx
    push bx
    mov es, [px_sseg + bx]
    mov cx, [px_chunk]
    test bp, 1                      ; the open's FIRST read is 8 KB at most:
    jz .full                        ; it is what the first rows wait on, and
    cmp cx, PX_CHUNKLO              ; the worker takes a slot of any length
    jbe .full
    mov cx, PX_CHUNKLO
.full:
    call px_rseq
    pop bx
    pop cx
    or ax, ax
    jnz .mark
    mov byte [px_feof], 1
.mark:
    mov [px_slen + bx], ax
    shr bx, 1
    mov byte [px_sfull + bx], 1     ; ...the slot's LAST field
    inc bl
    cmp bl, [px_nslot]
    jb .nx
    xor bl, bl
.nx:
    mov [px_fslot], bl
    loop .slot
.done:
    mov byte [px_rq], 0             ; LAST
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; px_rseq - UI TASK: CX bytes of the stream at [px_fabs] into ES:0, the
; offset advanced. AX = bytes delivered: 0 at the end, and 0 with [px_rerr]
; set on a read error. Clobbers BX, DX, SI, DI
px_rseq:
    xor bx, bx
    mov ax, [px_fabs]
    mov dx, [px_fabs + 2]
    mov si, px_cur + PXR_NAME
    mov di, px_rcur
    call os88_rseq                  ; DX:AX = bytes delivered
    jnc .got
    mov byte [px_rerr], 1
    xor ax, ax
.got:
    add [px_fabs], ax
    adc word [px_fabs + 2], 0
    ret

; =============================================================================
; READING, ON THE WORKER. px_rb, px_rdn and px_rskip each answer CF = 1 when
; the stream ends (or is cancelled, or failed): the decoder's "cut short",
; which px_wjob turns into what actually happened
; =============================================================================

; (px_rb, px_rdn and px_rskip, the simple formats' readers of the window
; below, moved with them into the SIMPLE part in wave 4: SPEC.md 106.20)

; px_rnext - give the window back and take the next one. CF = 1 at the end,
; on a cancel, or after a read error. Preserves all
px_rnext:
    push ax
    push bx
    push cx
    mov ax, [px_rnxt]               ; the next window starts where this one
    mov [px_rbase], ax              ; ended
    mov ax, [px_rnxt + 2]
    mov [px_rbase + 2], ax
    mov word [px_rpos], 0
    mov word [px_rend], 0
    cmp byte [px_rflat], 0
    jne .flat
    cmp byte [px_rhave], 0          ; --- THE RING ---------------------------
    je .wait
    mov bl, [px_wslot]              ; the slot we held goes back to the pump
    xor bh, bh
    mov byte [px_sfull + bx], 0
    mov byte [px_rq], 1             ; ...the request LAST
    call px_wake
    inc bl
    cmp bl, [px_nslot]
    jb .w1
    xor bl, bl
.w1:
    mov [px_wslot], bl
    mov byte [px_rhave], 0
.wait:
    cmp byte [px_abort], 0
    jne .end
    mov bl, [px_wslot]
    xor bh, bh
    cmp byte [px_sfull + bx], 0
    jne .got
    call px_wake                    ; one is queued at most: asking again is
    mov ax, 1                       ; free, and covers a ring that was full
    call OSAPI_TASK_SLEEP
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE           ; ...and the park point (SPEC.md 66.5)
    jmp short .wait
.got:
    mov byte [px_rhave], 1
    shl bx, 1
    mov ax, [px_slen + bx]
    or ax, ax
    jz .end                         ; a slot of nothing: the end of the file
    mov [px_rend], ax
    mov ax, [px_sseg + bx]
    mov [px_rsegc], ax
    jmp short .adv
.flat:                              ; --- A FILE IN MEMORY -------------------
    cmp byte [px_abort], 0
    jne .end
    mov ax, [px_flatlen]            ; what is left past the window's start
    mov cx, [px_flatlen + 2]
    sub ax, [px_rbase]
    sbb cx, [px_rbase + 2]
    jb .end
    or cx, cx
    jnz .full
    or ax, ax
    jz .end
    cmp ax, PX_FLATWIN
    jbe .part
.full:
    mov ax, PX_FLATWIN
.part:
    mov [px_rend], ax
    mov ax, [px_fk]                 ; window k: 0x800 paragraphs each
    mov cl, 11
    shl ax, cl
    add ax, [px_flatseg]
    mov [px_rsegc], ax
    inc word [px_fk]
.adv:
    mov ax, [px_rend]
    add [px_rnxt], ax
    adc word [px_rnxt + 2], 0
    mov ax, [px_rdrop]              ; the bytes before the stream's base
    cmp ax, [px_rend]               ; (px_pumpinit): the window starts past
    jbe .dr                         ; them
    mov ax, [px_rend]
.dr:
    mov [px_rpos], ax
    sub [px_rdrop], ax
    clc
    jmp short .out
.end:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE SERVICES a decoder PART far-calls on the worker (SPEC.md 106.18). Each
; sets DS to the package itself - a part calls `call far` with its own
; registers - and answers with retf. Every one checks what it is handed and
; does nothing with what is out of range rather than trust a part
; =============================================================================
px_kst:     dw px_ks_next, px_ks_emit, px_ks_scat, px_ks_tick, px_ks_pal
            dw px_ks_cols, px_ks_ring

; K_NEXT - out CF = 0: ES:SI the rest of the worker's window or the next one,
; CX its bytes (> 0); CF = 1 at the end, a cancel or a read error. Keeps all
; else
px_ks_next:
    push ds
    push cs
    pop ds
    push ax
    push si
    mov si, [px_rpos]
    cmp si, [px_rend]
    jb .have
    call px_rnext                   ; CF at the end
    jc .end
    mov si, [px_rpos]
.have:
    mov ax, [px_rend]
    mov cx, ax
    sub cx, si
    mov [px_rpos], ax               ; ...all of it the part's now
    mov es, [px_rsegc]
    add sp, 2                       ; (the SI it came with: not wanted)
    pop ax
    pop ds
    clc
    retf
.end:
    pop si
    pop ax
    pop ds
    stc
    retf

; K_EMIT - AX = a source row in the row buffer: px_emit; then BX (not FFFFh)
; the master rows complete from the top, DX (not FFFFh) the progress.
; out CF = 1 AX = PXD_ABORT or a refusal. Keeps all but AX
px_ks_emit:
    push ds
    push cs
    pop ds
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    call px_kfirst                  ; the tables, if the part did not ask
    jc .out
    mov [px_kbx], bx
    mov [px_kdx], dx
    call px_emit
    jc .out
    call px_kfix
    clc
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ds
    retf

; K_SCAT - at 1/1: the row buffer's pixels at columns CL + k CH into master
; row AX, quantised whole into the cube first on a truecolour picture (the
; dither is a pixel's place's, so a pass's pixels quantise as the whole row
; would); BX and DX as K_EMIT's. CF/AX as K_EMIT's
px_ks_scat:
    push ds
    push cs
    pop ds
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    call px_kfirst
    jc .out
    mov [px_kbx], bx
    mov [px_kdx], dx
    cmp byte [px_cur + PXR_SCL], 0
    jne .prog
    cmp ax, [px_cur + PXR_MH]
    jae .prog
    or ch, ch
    jz .prog
    cmp ch, 8
    ja .prog
    mov bl, cl
    xor bh, bh
    cmp bx, [px_cur + PXR_MW]
    jae .prog
    mov [px_qrow], ax
    push ax
    push cx
    cmp byte [px_tquant], 0
    je .nq
    mov es, [px_wbase]              ; the cube, in place: each index lands
    mov di, QT_SIZE                 ; at or left of the triple it is read
    call px_quant                   ; from
.nq:
    pop cx
    pop ax
    call px_mrow                    ; ES:DI = the master row
    mov bl, ch
    xor bh, bh                      ; BX = the step
    xor ch, ch                      ; CX = the first column
    add di, cx
    mov si, cx
    mov dx, [px_cur + PXR_MW]
    push ds
    mov ds, [px_wseg]
.c:
    mov al, [si]
    mov [es:di], al
    add di, bx
    add si, bx
    cmp si, dx
    jb .c
    pop ds
.prog:
    call px_kfix
    call px_progress                ; the cancel, the liveness, the wake
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ds
    retf

; K_COLS - ES:SI = a block of BX master rows of DX pixels each (3 bytes a
; pixel for truecolour), its top-left at master row AX, column CX; DI = the
; rows complete from the top, or FFFFh (SPEC.md 106.19). Each row is
; quantised into the cube with its own row's and columns' dither, or
; copied, clipped to the master. CF/AX as K_EMIT's
px_ks_cols:
    push ds
    push cs
    pop ds
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    call px_kfirst
    jc .out
    mov [px_kdi], di
    mov [px_kcrow], ax
    mov [px_cn], bx
    mov [px_ccol], cx
    mov [px_cpix], dx
    mov [px_csrc], si
    mov [px_csrc + 2], es
    cmp dx, PX_DIMMAX               ; (nothing a part hands over is trusted)
    ja .prog
.r:
    cmp word [px_cn], 0
    je .prog
    mov ax, [px_kcrow]
    cmp ax, [px_cur + PXR_MH]
    jae .nx
    call px_colrow
.nx:
    inc word [px_kcrow]
    dec word [px_cn]
    mov ax, [px_cpix]               ; the next block row
    cmp byte [px_cur + PXR_RF], RF_RGB
    jne .g
    mov dx, ax
    shl ax, 1
    add ax, dx
.g:
    add [px_csrc], ax
    jmp short .r
.prog:
    mov ax, [px_kdi]
    cmp ax, 0xFFFF
    je .p
    cmp ax, [px_cur + PXR_MH]
    jbe .rd
    mov ax, [px_cur + PXR_MH]
.rd:
    mov [px_rdone], ax
.p:
    call px_progress
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ds
    retf

; px_colrow - K_COLS's block row at [px_csrc] into master row AX, columns
; [px_ccol] on: the run clipped to the master, then copied into the row
; buffer and quantised from there (the dither's phase is the master's
; column), or copied straight in for grey. Clobbers AX, BX, CX, DX, SI, DI
px_colrow:
    mov [px_qrow], ax
    call px_mrow                    ; ES:DI = the master row
    mov bx, [px_ccol]               ; BX = the first column...
    mov cx, [px_cpix]               ; ...CX the pixels
    cmp bx, [px_cur + PXR_MW]
    jae .o
    mov dx, [px_cur + PXR_MW]
    sub dx, bx
    cmp cx, dx
    jbe .n
    mov cx, dx
.n:
    jcxz .o
    add di, bx
    cmp byte [px_cur + PXR_RF], RF_RGB
    je .rgb
    push ds
    lds si, [px_csrc]
    rep movsb
    pop ds
.o:
    ret
.rgb:
    push es
    push di
    push cx
    mov ax, cx                      ; the run's triples into the row buffer
    shl cx, 1
    add cx, ax
    mov es, [px_wseg]
    xor di, di
    push ds
    lds si, [px_csrc]
    rep movsb
    pop ds
    pop cx
    pop di
    pop es
    jmp px_qrun

; px_qrun - the row buffer's CX triples into ES:DI: the cube by the ordered
; dither (SPEC.md 106.8), a pixel at a time, for master row [px_qrow] from
; column BX - px_quant's lookups with the page found per pixel rather than
; patched, because a run starts at any column. Clobbers AX, BX, CX, SI, BP
px_qrun:
    push ds
    mov bp, [px_qrow]
    and bp, 15
    shl bp, 1
    shl bp, 1
    shl bp, 1
    shl bp, 1
    add bp, px_qpage                ; BP = this matrix row's sixteen pages
    mov dx, bx                      ; DX = the column
    mov si, QT_SIZE
    mov ds, [cs:px_wbase]
    cld
.p:
    mov bx, dx
    and bx, 15
    add bx, bp
    mov bh, [cs:bx]                 ; this column's class x 3
    xor bl, bl
    lodsb
    xlatb
    mov ah, al
    inc bh
    lodsb
    xlatb
    add ah, al
    inc bh
    lodsb
    xlatb
    add al, ah
    stosb
    inc dx
    loop .p
    pop ds
    ret

; K_RING - px_rnext for a part LINKED against the package (SPEC.md 106.20),
; whose readers walk the worker's ring window in place: [px_rpos], [px_rend]
; and [px_rsegc] on to the next window. CF = 1 at the end. Keeps all
px_ks_ring:
    push ds
    push cs
    pop ds
    call px_rnext
    pop ds
    retf

; K_TICK - AX = the progress, without a row. CF/AX as K_EMIT's
px_ks_tick:
    push ds
    push cs
    pop ds
    mov [px_srcdone], ax
    call px_progress
    pop ds
    retf

; K_PAL - the palette is known (SPEC.md 106.18). CF/AX as px_tables'
px_ks_pal:
    push ds
    push cs
    pop ds
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    call px_kpal1
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ds
    retf

; px_kpal1 - [px_spal] is the palette of a PAL picture now, and the display
; tables are built for it - what px_wjob does before a resident format's
; first row. out CF = 1 AX = PXD_ABORT on a cancel
px_kpal1:
    mov ax, [px_k + PXK_NPAL]       ; the entries the STREAM gave the part
    or ax, ax                       ; (a PNG's PLTE, a GIF's local table):
    jz .n                           ; px_tables searches that many plans and
    mov [px_cur + PXR_NPAL], ax     ; takes the rest as black
.n:
    cmp byte [px_cur + PXR_PMODE], PM_PAL
    jne .t
    mov byte [px_pvalid], 0         ; a new palette: its plans are not made
    push ds
    pop es
    mov si, px_spal
    mov di, px_pal
    mov cx, 768 / 2
    cld
    rep movsw
.t:
    call px_tables
    jc .out
    mov byte [px_tabok], 1
    call px_wake
    clc
.out:
    ret

; px_kfirst - the tables are built, or K_PAL's work is done now. out CF = 1
; AX = why not; else AX kept
px_kfirst:
    cmp byte [px_tabok], 0
    jne .ok
    push ax
    call px_kpal1
    jc .no
    pop ax
.ok:
    clc
    ret
.no:
    add sp, 2
    ret

; px_kfix - a service's BX and DX, banked: the rows complete (at most the
; master's) and the progress, where they are not FFFFh. Preserves all but AX
px_kfix:
    mov ax, [px_kbx]
    cmp ax, 0xFFFF
    je .d
    cmp ax, [px_cur + PXR_MH]
    jbe .r
    mov ax, [px_cur + PXR_MH]
.r:
    mov [px_rdone], ax
.d:
    mov ax, [px_kdx]
    cmp ax, 0xFFFF
    je .o
    mov [px_srcdone], ax
.o:
    ret

; px_wake - post a wake for our window: worker-safe, at most one queued
; (SPEC.md 74.1). Preserves all
px_wake:
    push bx
    mov bx, [px_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; px_progress - the end of every emitted row, on the worker: liveness every
; eight rows, the cancel, and a wake when a tick has passed. out CF = 1 AX =
; PXD_ABORT on a cancel. Preserves all but AX
px_progress:
    cmp byte [px_abort], 0
    jne .abort
    test byte [px_srcdone], 7
    jnz .w
    push bx
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE
    pop bx
.w:
    call OSAPI_GET_TICKS            ; the progress wake: every 3 ticks at most,
    push ax                         ; because every one lays the window out
    sub ax, [px_wtick]              ; and paints on the UI task - a fraction
    cmp ax, 3                       ; of the CPU the decode is not getting
    pop ax
    jb .ok
    mov [px_wtick], ax
    call px_wake
.ok:
    clc
    ret
.abort:
    mov ax, PXD_ABORT
    stc
    ret

; =============================================================================
; THE WORKER (SPEC.md 20.6, 106.9)
; =============================================================================
px_worker:
.top:
    ; RESTARTABLE ONLY HERE (SPEC.md 66.6.2): at the top of the loop the
    ; worker holds nothing - a job's state is all in the region - so a move
    ; of the region may throw this stack away and re-enter it; inside a job
    ; it may not (the decode's place in the file is the stack's), and the
    ; window is withdrawn before any work
    OS88_WORKER_RESTARTABLE px_worker
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE           ; the close box ends us here
    push ax
    xor ax, ax
    call OSAPI_TASK_RESTARTABLE     ; ...withdrawn
    pop ax
    cmp byte [px_job], JOB_NONE
    jne .run
    mov ax, 2
    call OSAPI_TASK_SLEEP
    jmp short .top
.run:
    call px_wjob                    ; AL = the answer
    mov [px_wres], al
    mov byte [px_job], JOB_NONE     ; LAST
    call px_wake
    jmp short .top

; px_wjob - the job [px_job] names. out AL = PXD_OK, a PXD_* refusal or
; PXD_ABORT
px_wjob:
    mov byte [px_onworker], 1       ; px_plans asks the part the decode holds
    call px_wjob1                   ; (px_wcall), never fetches, and polls the
    mov byte [px_onworker], 0       ; cancel (pl_poll): 106.23's fix - the
    ret                             ; byte was never set, so on the worker it
                                    ; took the UI task's branch
px_wjob1:
    mov byte [px_wstage], 0
    mov al, PXPART_GIF              ; A GIF'S FRAMES (106.25): its part's
    cmp byte [px_job], JOB_ANIM     ; DECODE, which knows the job
    je .part
    mov cl, [px_eop]                ; EDITING'S TWO (SPEC.md 106.24): an
    mov al, PXPART_EDIT             ; operation, or a save, the part the UI
    cmp byte [px_job], JOB_EDIT     ; task fetched for it (px_wcall: no
    je .part                        ; fetch here)
    mov cl, [px_wfmt]
    mov al, PXPART_WRITE
    cmp byte [px_job], JOB_SAVE
    jne .nopart
.part:
    mov bl, PXV_DECODE
    call px_wcall
    jc .fail
    xor al, al
    ret
.nopart:
    cmp byte [px_job], JOB_DECODE   ; a decoder PART knows its palette only
    jne .tab                        ; in the stream: it asks for the tables
    cmp byte [px_dfmt], DF_PART     ; itself (K_PAL, SPEC.md 106.18)
    je .dec
.tab:
    call px_tables                  ; pxview.inc: the palette's display tables
    jc .fail
    mov byte [px_tabok], 1
    call px_wake                    ; ...so the rows can be painted as they come
    cmp byte [px_job], JOB_DECODE   ; (a TABLES job counts nothing: its
    jne .done                       ; master's tail holds its counts - every
                                    ; master is counted when it is made)
.dec:
    mov byte [px_wstage], 1
    xor ax, ax
    mov [px_srcdone], ax
    mov [px_rdone], ax
    mov [px_rcnt], al
    call px_qprep                   ; pxmaster.inc: the quantiser's pages
    call px_decode                  ; pxsimple.inc
    jc .fail
.hist:
    mov byte [px_wstage], 2         ; THE HISTOGRAM's counts: the decoder
    mov al, [px_kheld]              ; part's (pxplan.inc's pl_hcount, CL = 2:
    mov bl, PXV_PLANS               ; SPEC.md 106.25), the part the decode
    mov cl, 2                       ; holds
    mov di, px_k
    call px_wcall
    jc .fail
.done:
    xor al, al                      ; ([px_hok] is the UI task's to set,
    ret                             ; after px_hstats has folded the counts)
.fail:
    cmp byte [px_abort], 0          ; what actually happened, not what the
    jne .ab                         ; decoder saw
    cmp ax, PXD_TRUNC
    jne .code
    cmp byte [px_rerr], 0
    je .code
    mov ax, PXD_READ
.code:
    ret
.ab:
    mov al, PXD_ABORT
    ret

; px_spawn - the worker, once (SPEC.md 20.6): from a callback, the lock held.
; A refusal is transient (the task table is full): CF = 1 and the open says so
px_spawn:
    cmp byte [px_wspawned], 0
    jne .ok
    push ax
    push bx
    mov ax, px_worker
    mov bx, [px_win]
    call OSAPI_TASK_SPAWN
    pop bx
    pop ax
    jc .out
    mov byte [px_wspawned], 1
.ok:
    clc
.out:
    ret

; =============================================================================
; OPENING, FINISHING, CANCELLING - the UI task's half (SPEC.md 106.9)
; =============================================================================

; px_open - open [px_oname] in the folder the instance stands in. THE LOCK IS
; HELD (a dialog's completion, a key, or the wake that took it). Reads the
; head, parses it, claims, hands the worker the job. Any refusal puts the
; picture that was showing back and says why
px_open:
    call px_hstop                   ; (a hidden decode or a GIF's frame: the
    push ax                         ; worker free, the record the shown one's)
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cmp byte [px_busy], 0
    je .free
    mov si, px_s_busy
    call px_toast
    jmp .out
.free:
    call px_eclear                  ; undo, the selection and the EDIT part
    mov si, px_cur                  ; let go (SPEC.md 106.24); bank what is
    mov di, px_prev                 ; showing (SPEC.md 106.8)
    mov cx, PXR_SZ
    cld
    rep movsb
    mov si, px_pal
    mov di, px_ppal
    mov cx, 768
    rep movsb
    mov si, px_plan                 ; ...and its plans, when they are its
    mov di, px_pplan                ; (SPEC.md 106.20): a cancel's restore
    mov cx, 768                     ; then needs no part to make them
    rep movsb
    mov al, [px_pvalid]
    mov [px_ppvalid], al
    mov byte [px_cur + PXR_HAVE], 0
    mov word [px_cur + PXR_MSEG], 0
    mov word [px_cur + PXR_MKB], 0
    mov si, px_oname
    mov di, px_cur + PXR_NAME
    call px_strcpy
    call OSAPI_FILE_HERE            ; where it is: Revert and a cancel go back
    mov [px_cur + PXR_DIR], dx
    mov [px_cur + PXR_VOL], bl
    mov word [px_hseg], 0
    mov word [px_flatraw], 0
    mov byte [px_rflat], 0
    mov byte [px_clok], 0           ; this open asks the volume once
    call px_examine                 ; the folder, the head, the sniff
    jnc .found
    jmp .refuse                     ; (AX = why)
.found:
    mov al, [px_cur + PXR_FMT]
    mov [px_rfmt], al
    call px_hdrparse                ; pxsimple.inc: CF AX = a PXD_*
    jc .refuse
    call px_headfree                ; the head is parsed: given back before
    call px_claims                  ; the claims (SPEC.md 106.18)
    jc .refuse
    call px_setpal
    inc byte [px_cur + PXR_SERIAL]
    cmp byte [px_keepv], 0          ; a re-decode keeps the zoom and the pan
    jne .kv                         ; (SPEC.md 106.19)
    call px_viewnew                 ; pxview.inc: Fit, centred
.kv:
    call px_viewclaim               ; ...and the tables' claim for this depth
    jc .nomem
    mov al, [px_bpp]                ; the worker builds them for THIS depth
    mov [px_tdepth], al
    call px_pumpinit
    mov al, [px_keepv]              ; (a re-decode reads ahead from the
    shl al, 1                       ; start: px_pumpfill)
    mov [PXS_DEFER], al
    call px_pumpone                 ; the first slot now, not a wake later
    call px_spawn
    jc .nowork
    mov byte [px_abort], 0
    mov byte [px_tabok], 0
    mov byte [px_hok], 0
    mov word [px_ypaint], 0
    mov word [px_ypbot], 0
    mov byte [px_busy], 1
    mov byte [px_tabdirty], 1       ; the tables are the NEW palette's now
    mov byte [px_cvmsg], 0          ; (no refusal stands)
    mov byte [px_job], JOB_DECODE   ; LAST: the worker starts on this byte
    call px_bstop                   ; the toolbar's first button: Stop
    call px_compose
    call px_shown
    jmp .out
.nowork:
    mov ax, PXD_MEM
    jmp short .undo
.nomem:
    mov ax, PXD_MEM
.undo:
    call px_relclaims               ; master, ring, work, flat
.refuse:
    push ax
    call px_headfree
    call px_relflat
    pop ax
    call px_restore                 ; the previous picture, as it was
    call px_refusal                 ; "BMP: depth not read"
    call px_compose
    call px_shown
    inc word [px_ndone]             ; an open has ended (a test waits on it)
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_restore - the previous picture back as [px_cur], its palette too. When
; the tables were already rebuilt for the picture that failed
; ([px_tabdirty]), the view goes back to Fit and the worker rebuilds them
; and the histogram; a refusal BEFORE that touched neither, and the window
; is exactly as it was. Preserves all
px_restore:
    push ax                         ; (a refusal's PXD_* rides through here)
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, px_prev
    mov di, px_cur
    mov cx, PXR_SZ
    cld
    rep movsb
    mov si, px_ppal
    mov di, px_pal
    mov cx, 768
    rep movsb
    mov al, [px_ppvalid]            ; its plans, banked (SPEC.md 106.20)
    mov [px_pvalid], al
    or al, al
    jz .np
    mov si, px_pplan
    mov di, px_plan
    mov cx, 768
    rep movsb
.np:
    mov byte [px_prev + PXR_HAVE], 0
    mov word [px_prev + PXR_MSEG], 0
    call px_ncfind                  ; the list's place, the shown one's again
    call px_kkeep                   ; a decoder part of another kind goes
    cmp byte [px_tabdirty], 0
    je .out
    mov byte [px_tabdirty], 0
    mov byte [px_tabok], 0
    mov byte [px_hok], 0
    cmp byte [px_cur + PXR_HAVE], 0
    je .out
    call px_viewnew
    call px_viewclaim
    jc .out
    push ax
    mov al, [px_bpp]
    mov [px_tdepth], al
    pop ax
    call px_spawn
    jc .out
    mov byte [px_abort], 0
    mov byte [px_busy], 2
    mov byte [px_job], JOB_TABLES   ; LAST
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; px_relclaims - give back the new picture's master, ring, work and flat
; buffer. Preserves all
px_relclaims:
    push dx
    mov dx, [px_cur + PXR_MSEG]
    or dx, dx
    jz .r
    call OSAPI_MEM_FREE
    mov word [px_cur + PXR_MSEG], 0
.r:
    mov dx, [px_ringraw]
    or dx, dx
    jz .w
    call OSAPI_MEM_FREE
    mov word [px_ringraw], 0
.w:
    mov dx, [px_wbase]
    or dx, dx
    jz .f
    call OSAPI_MEM_FREE
    mov word [px_wbase], 0
    mov word [px_wseg], 0
.f:
    call px_relflat
    pop dx
    ret

; px_relflat / px_headfree - the in-memory file, the head. Preserve all
px_relflat:
    push dx
    mov dx, [px_flatraw]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [px_flatraw], 0
.out:
    pop dx
    ret
px_headfree:
    push dx
    mov dx, [px_hraw]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [px_hraw], 0
    mov word [px_hseg], 0
.out:
    pop dx
    ret

; px_finish - the worker has answered (UI task, the lock held): the ring and
; the work go back; on success the previous master goes too and this one is
; the picture; otherwise the previous picture comes back and the refusal is
; said. A tables-only job just ends. Then the window says so
px_finish:
    push ax
    push dx
    mov al, [px_busy]
    mov byte [px_busy], 0
    cmp al, 1
    jne .tables
    mov dx, [px_ringraw]            ; the decode's own claims
    or dx, dx
    jz .w
    call OSAPI_MEM_FREE
    mov word [px_ringraw], 0
.w:
    mov dx, [px_wbase]
    or dx, dx
    jz .fl
    call OSAPI_MEM_FREE
    mov word [px_wbase], 0
    mov word [px_wseg], 0
.fl:
    call px_relflat
    call px_bopen                   ; Stop becomes Open again
    mov al, [px_wres]
    or al, al
    jnz .bad
    call px_freeprev                ; THE PICTURE: the old one is let go
    mov byte [px_cur + PXR_HAVE], 1
    mov byte [px_dirty], 0          ; (the file's, as it is on the disk)
    mov byte [px_tabdirty], 0
    call px_anfree                  ; ...and its animation; a GIF's own, if
    cmp byte [px_cur + PXR_FMT], PXF_GIF    ; it moves (106.25)
    jne .na
    push cx
    mov cl, AV_INIT
    call px_ancall
    pop cx
.na:
    call px_hstats                  ; the Histogram's numbers from its counts,
    mov byte [px_hok], 1            ; and only then counted (speed review F2:
    call px_compose                 ; a sync between the two drew byte planes)
    call px_flags
    call px_progpaint               ; the last rows, onto what is there: the
    mov al, PX_R_TB | PX_R_PANELS | PX_R_STATUS
    call px_regdraw                 ; canvas is NOT rendered again
    jmp short .out
.bad:
    push ax
    mov dx, [px_cur + PXR_MSEG]
    or dx, dx
    jz .nm
    call OSAPI_MEM_FREE
    mov word [px_cur + PXR_MSEG], 0
.nm:
    call px_restore
    pop ax
    cmp al, PXD_ABORT
    jne .said
    mov [px_lastref], al
    jmp short .shown
.said:
    xor ah, ah
    call px_refusal
.shown:
    call px_compose
    call px_flags
    mov al, PX_R_ALL
    call px_regdraw
    jmp short .out
.tables:
    call px_hstats                  ; a restore's counts folded too - it never
    mov byte [px_hok], 1            ; was, and the graph drew byte planes
    call px_flags                   ; (what [px_busy] greyed, live again)
    mov al, PX_R_CANVAS | PX_R_PANELS
    call px_regdraw
.out:
    inc word [px_ndone]             ; an open has ended (a test waits on it)
    pop dx
    pop ax
    ret

; px_cancel - Esc, or Stop: the worker stops at its next row or refill and
; px_finish puts the previous picture back. Preserves all
px_cancel:
    cmp byte [px_busy], 2           ; a decode, an operation, a save - not
    je .out                         ; a table's rebuild (nothing to undo)
    cmp byte [px_busy], 0
    je .out
    mov byte [px_abort], 1
.out:
    ret

; px_refusal - AX = a PXD_*: one toast, "FORMAT: reason" (SPEC.md 106.10), at
; most TOAST_MAX glyphs. Preserves all
px_refusal:
    push ax
    push si
    push di
    mov [px_lastref], al            ; (for tests/pxdecode.py)
    call px_refwords
    jc .out
    mov si, px_cline
    call px_toast
    mov di, px_cvmsg                ; ...and it stands in the empty canvas
    call px_strcpy                  ; until something is shown (wave-1
.out:                               ; review MIN-9)
    pop di
    pop si
    pop ax
    ret

; px_refwords - AX = a PXD_*: px_cline = "FORMAT: reason", or the reason
; alone for the two that name no format; the toast's words, and full
; screen's (106.23). CF = 1 it is not a reason. Preserves all
px_refwords:
    cmp ax, PXD_NREASON
    jb .go
    stc
    ret
.go:
    push ax
    push bx
    push si
    push di
    mov di, px_cline
    mov byte [di], 0
    cmp ax, PXD_NOFILE              ; these two say it without a format
    je .r
    cmp ax, PXD_NOTPIC
    je .r
    push ax
    mov al, [px_rfmt]               ; the REFUSED file's format: px_restore has
    call px_fmtname                 ; put the shown one's back by now
    call px_strcpy
    mov si, px_s_colon
    call px_strcat
    pop ax
.r:
    mov bx, ax
    shl bx, 1
    mov si, [px_reasons + bx]
    call px_strcat
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret

; px_bstop / px_bopen - the toolbar's first button is Stop while a picture
; decodes (SPEC.md 106.9). Preserve all
px_bstop:
    push ax
    mov ax, pxi_stop
    mov [px_lab_full + 2 * PX_B_OPEN], ax
    mov ax, px_hicons + PXI_STOP * PXH_SZ
    mov [px_lab_half + 2 * PX_B_OPEN], ax
    mov word [px_tbdef + PX_TBD_CAP], px_c_stop
    pop ax
    ret
px_bopen:
    push ax
    mov ax, pxi_open
    mov [px_lab_full + 2 * PX_B_OPEN], ax
    mov ax, px_hicons
    mov [px_lab_half + 2 * PX_B_OPEN], ax
    mov word [px_tbdef + PX_TBD_CAP], px_c_open
    pop ax
    ret
