; =============================================================================
; os8088 - apps/realdmg/rwbank.inc
;
; RealDMG's BANK CACHE (docs/REALDMG-SPEC.md section 10.3): a cartridge kept
; above 1MB, its banks fetched into conventional memory as the game switches
; to them. Included by realdmg.asm.
;
; A cartridge that fits is one claim, as it always was. One that does not -
; over 256K, or a 256K one that would leave no room beside the recompiler's
; 80KB on a 640KB machine (Kirby's Dream Land) - goes to OSAPI_XMEM_ALLOC
; whole, but for bank 0, which is the view's own 0000-3FFF; banks 1 on live
; in K slots of 16KB, as many as the heap affords after the recompiler, the
; damage memory and the cartridge's RAM. (The plan once kept the
; recompiler's 64KB a SECOND time, the claim having moved in front of it:
; the field Victory's 179K heap gave Kirby 5 slots where 9 fit, and two
; fetches a frame.) rdgsys's bank table names each bank
; that is in a slot; a switch to one that is not (rdg_bmiss) fetches it over
; the slot whose bank the game switched to longest ago, by frame.
;
; A game's working set is small: gbprof's LRU column has Kirby in play on 6
; banks of 16 and Looney Tunes on 8 of 64, a fetch a second or less with
; that many slots. OSAPI_XMEM_COPY is UI-task context, so a fetch the worker
; needs is the UI task's: the worker posts the bank (rw_bankreq), wakes the
; window and yields until it is in. Full screen runs the machine on the UI
; task, which fetches for itself.
; =============================================================================

RW_KEEPKB       equ 24              ; KB the plan keeps for the cartridge's
                                    ;   RAM and some slack: the damage memory
                                    ;   and the recompiler are claimed BEFORE
                                    ;   it (realdmg.asm), so what it sees is
                                    ;   what is left after them
RW_XROMMAX_KB   equ 8192            ; MBC5's largest cartridge

; rw_plan - [rw_romkb] is the size probe's: how the cartridge will live.
; CF = 0: [rw_bcache] 0 one claim, 1 the cache ([rw_nslot] slots); CF = 1:
; SI = the refusal
rw_plan:
    mov byte [rw_bcache], 0
    call OSAPI_MEM_AVAIL            ; AX = the heap's largest run, KB (and
    mov [rw_pavail], ax             ;   BX its total: the log's head says the
    push ax                         ;   one)
    mov bx, RW_KEEPKB               ; BX = what else the heap must hold
    pop ax
    mov cx, [rw_romkb]
    cmp cx, RW_ROMMAX_KB + 2
    ja .cache
    mov dx, cx
    add dx, bx
    cmp ax, dx
    jae .whole                      ; it all fits: one claim
.cache:
    mov si, rw_s_big                ; (the refusal, should it come to one)
    and cx, 0FFF0h                  ; whole banks: the probe answers a KB
    cmp cx, 32                      ;   over an exact size (SPEC.md
    jb .nocache                     ;   20.14.6.3), and the header says the
    cmp cx, RW_XROMMAX_KB           ;   rest (rw_bload)
    ja .nocache
    push ax
    push bx
    push cx
    call OSAPI_XMEM_CAPS            ; AX = KB the store can hand out (and
    mov dx, ax                      ;   DX:CX and BL its own: kept here)
    pop cx
    pop bx
    pop ax
    sub cx, 16                      ; banks 1 on: the store's share
    cmp dx, cx
    jb .nocache
    mov si, rw_s_room
    sub ax, bx                      ; AX = KB for slots
    jb .nocache
    mov cl, 4
    shr ax, cl                      ; slots the heap affords...
    mov dx, [rw_romkb]
    shr dx, cl
    dec dx                          ; ...no more than banks 1 on...
    cmp ax, dx
    jbe .k1
    mov ax, dx
.k1:
    cmp ax, RW_SLOTMAX              ; ...nor than the table holds
    jbe .k2
    mov ax, RW_SLOTMAX
.k2:
    cmp al, [rw_slotmin]            ; RW_SLOTWANT, or four (rw_load), or
    jae .k3                         ;   every bank there is
    cmp ax, dx
    jb .nocache
.k3:
    mov [rw_nslot], ax
    and word [rw_romkb], 0FFF0h
    mov byte [rw_bcache], 1
    clc
    ret
.nocache:
    mov cx, [rw_romkb]              ; one claim without the room the rest
    cmp cx, RW_ROMMAX_KB + 2        ;   wanted, if it fits at all
    ja .no
    mov si, rw_s_room
    call OSAPI_MEM_AVAIL
    cmp ax, cx
    jb .no
.whole:
    clc
    ret
.no:
    stc
    ret

; rw_bload - the cache's load, on the UI task in rw_name's folder: the slots,
; the store, then the file read raw a bank at a time - bank 0 into the view,
; banks 1 to K-1 into slots 0 to K-2 and every later one through slot K-1,
; each copied up as it comes. CF = 1: SI = the refusal, nothing kept.
rw_bload:
    mov ax, [rw_nslot]
    mov cl, 4
    shl ax, cl
    call OSAPI_MEM_CLAIM            ; DX = the slots, pinned: the bank table
    jc .room                        ;   names them
    mov [rw_slotseg], dx
    mov ax, [rw_romkb]
    sub ax, 16
    mov cx, 1024
    mul cx
    call OSAPI_XMEM_ALLOC           ; DX:AX = the store's base
    jc .noxms
    mov [rw_xbase], ax
    mov [rw_xbase + 2], dx
    mov di, rw_bcur                 ; the read's cursor, from offset 0
    mov cx, FSEQ_SIZE / 2
    xor ax, ax
    push ds
    pop es
    rep stosw
    mov al, 2                       ; bank 0: the view's 0000-3FFF
    call op_seg
    mov dx, ax
    push dx
    call rw_bread
    pop dx
    jc .bad
    push es                         ; its header's size: 32KB << n, and it
    mov es, dx                      ;   must be the file's (a compressed
    mov cl, [es:0148h]              ;   file reads raw here, and fails this)
    pop es
    cmp cl, 8
    ja .bad
    mov ax, 32
    shl ax, cl
    cmp ax, [rw_romkb]
    jne .bad
    push es
    mov es, dx
    call rw_hdrchk                  ; (SI = why not)
    pop es
    jc .why
    mov word [rw_bnum], 1
.b:
    mov ax, [rw_bnum]               ; bank n: slot n-1, or the last slot
    dec ax
    mov bx, [rw_nslot]
    dec bx
    cmp ax, bx
    jbe .s
    mov ax, bx
.s:
    shl ax, 1
    mov si, ax
    mov dx, [rw_bnum]
    mov [rw_slotbank + si], dx
    mov dx, si
    mov dh, dl
    xor dl, dl
    shl dx, 1                       ; DX = slot x 400h
    add dx, [rw_slotseg]
    push dx
    call rw_bread
    pop dx
    jc .bad
    push es                         ; up to the store, at (n - 1) x 16KB
    mov es, dx
    mov ax, [rw_bnum]
    dec ax
    mov cx, 16384
    mul cx
    add ax, [rw_xbase]
    adc dx, [rw_xbase + 2]
    xor si, si
    xor di, di                      ; out
    call OSAPI_XMEM_COPY
    pop es
    jc .xbad
    inc word [rw_bnum]
    mov ax, [rw_romkb]
    mov cl, 4
    shr ax, cl
    cmp [rw_bnum], ax
    jb .b
    mov [rw_banks], ax
    mov al, 2                       ; the image rdg_init and rw_ramsetup read
    call op_seg                     ;   bank 0 from: the view itself
    mov [rw_romseg], ax
    mov word [rdg_nres], 1           ; rdgsys: the table is ours
    mov word [rdg_bfill], rw_bfill
    mov word [rdg_bmiss], rw_bmiss
    mov word [rw_nmiss], 0
    clc
    ret
.xbad:
.bad:
    mov si, rw_s_notrom
.why:
    push si
    mov ax, [rw_xbase]
    mov dx, [rw_xbase + 2]
    call OSAPI_XMEM_FREE
    pop si
    jmp short .free
.noxms:
    mov si, rw_s_room
.free:
    mov dx, [rw_slotseg]
    call OSAPI_MEM_FREE
    stc
    ret
.room:
    mov si, rw_s_room
    stc
    ret

; rw_bread - the file's next 16KB into DX:0000. CF = 1 unless it all came
rw_bread:
    push es
    push ds
    pop es
    xor bx, bx
    mov cx, 16384
    mov si, rw_name
    mov di, rw_bcur
    call OSAPI_FILE_READ_SEQ        ; DX:AX = the bytes delivered
    pop es
    jc .r
    or dx, dx
    jnz .no
    cmp ax, 16384
    je .r
.no:
    stc
.r:
    ret

; rw_bfill - rdgsys's rdg_init, its bank table just zeroed (DS = the view):
; every slot's bank
rw_bfill:
    push ax
    push cx
    push si
    push di
    xor si, si
    mov cx, [cs:rw_nslot]
.f:
    mov di, [cs:rw_slotbank + si]
    or di, di
    jz .n                           ; (a fetch that failed left it empty)
    shl di, 1
    mov ax, si
    mov ah, al
    xor al, al
    shl ax, 1                       ; AX = slot x 400h
    add ax, [cs:rw_slotseg]
    sub ax, 400h
    mov [RDG_BTAB + di], ax
.n:
    add si, 2
    loop .f
    pop di
    pop si
    pop cx
    pop ax
    ret

; rw_bmiss - rdgsys's rdg_setbank: BX = a bank x 2 with no segment in the
; table, DS = the view. The UI task fetches it now; the worker asks the UI
; task to and yields until the table has it - or, if the window is closing,
; ends at OSAPI_TASK_ALIVE, which a closed window never returns from. A
; yield and not a tick's sleep: the wake makes the UI task runnable at once,
; so a fetch is a switch there and back and the copy, where a sleep made every
; one a whole tick - 18 a second at most, which a game moving among one bank
; more than the slots hold spends all its time on (the sweep's Final Fantasy
; Adventure, at 1% of real time).
rw_bmiss:
    push ax
    cmp byte [cs:rw_infsx], 0       ; full screen: the machine is on the UI
    jne .ui                         ;   task - and so it is while a state is
    cmp byte [cs:rw_stq], 2         ;   loaded, the worker parked (only then:
    je .ui                          ;   at 1 it is the worker that asks)
    cmp byte [cs:rw_state], 1       ; a load or a restart from the UI task
    jne .ui
    mov [cs:rw_bankreq], bx
    push bx
    mov bx, [cs:rw_win]
    call OSAPI_WM_WAKE
    pop bx
.w:
    call OSAPI_TASK_YIELD
    cmp byte [cs:rw_closing], 0
    jne .gone
    cmp word [cs:rw_bankreq], 0
    jne .w
    pop ax
    ret
.gone:
    mov bx, [cs:rw_win]
    call OSAPI_TASK_ALIVE
    jmp short .w
.ui:
    call rw_bfetch
    pop ax
    ret

; rw_bserve - on the UI task: the bank the worker is waiting for, if any
rw_bserve:
    push bx
    mov bx, [cs:rw_bankreq]
    or bx, bx
    jz .r
    call rw_bfetch
    mov word [cs:rw_bankreq], 0
.r:
    pop bx
    ret

; rw_bfetch - UI task: bank BX/2 from the store into the slot whose bank the
; game switched to longest ago, and the bank table told. RDG_BUSE holds the
; frame of each bank's last switch (the core's mbc_set, the recompiler's bank
; stores and rdg_setbank write it), so this is least-recently-used to the
; frame: replayed over the bank switches of sixteen games (Peanut-GB, the
; profiler's input script, 4,000 frames, 8 slots) it missed 31 times on
; Pokemon Yellow where the old switch count halved at every fetch missed
; 1,467, and 6 times on Final Fantasy Adventure, whose MBC2 switches the
; count never saw (they come only through rdg_setbank) and 231 without.
; The age is the frame now less the stamp, unsigned, so the counter's wrap
; costs nothing; of equal ages the first slot goes.
rw_bfetch:
    push ax
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [cs:rdg_viewseg]
    xor si, si                      ; the victim: the slot switched to
    xor bp, bp                      ;   longest ago
    xor dx, dx                      ; DX = the oldest age so far
    mov cx, [cs:rw_nslot]
.v:
    mov di, [cs:rw_slotbank + si]
    shl di, 1
    mov ax, [es:S_FRAMES]
    sub ax, [es:RDG_BUSE + di]       ; AX = frames since
    cmp ax, dx
    jbe .nv
    mov dx, ax
    mov bp, si
.nv:
    add si, 2
    loop .v
    mov di, [cs:rw_slotbank + bp]   ; its bank leaves the table
    shl di, 1
    mov word [es:RDG_BTAB + di], 0
    mov word [cs:rw_slotbank + bp], 0
    mov ax, bp
    mov ah, al
    xor al, al
    shl ax, 1
    add ax, [cs:rw_slotseg]
    push ax                         ; the slot
    push es
    mov es, ax
    mov ax, bx
    shr ax, 1
    dec ax
    mov cx, 16384
    mul cx                          ; from (bank - 1) x 16KB up there
    add ax, [cs:rw_xbase]
    adc dx, [cs:rw_xbase + 2]
    xor si, si
    mov di, 1                       ; in
    call OSAPI_XMEM_COPY
    pop es
    pop ax
    jc .r                           ; (rdg_setbank maps bank 0 instead)
    sub ax, 400h
    mov [es:RDG_BTAB + bx], ax
    mov ax, bx
    shr ax, 1
    mov [cs:rw_slotbank + bp], ax
    inc word [cs:rw_nmiss]
.r:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret
