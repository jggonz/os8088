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
; CF = 0: [rw_bcache] 0 one claim, 1 the cache ([rw_nslot] slots), 2 (kern_
; mega's arm) its banks IN PLACE above 1MB; CF = 1: SI = the refusal
rw_plan:
    mov byte [rw_bcache], 0
    call OSAPI_MEM_AVAIL            ; AX = the heap's largest run, KB (and
    mov [rw_pavail], ax             ;   BX its total: the log's head says the
%ifdef PKG_MEGA                     ;   one)
    call rw_xplan
    jnc .r
%endif
    push ax
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
%ifdef PKG_MEGA
    cmp byte [rw_pm], 0             ; NO BANK CACHE UNDER CPU_F_PM: slot
    jne .nocache                    ;   0's segment is 400h BELOW its claim
%endif                              ;   (rw_bfill), which no selector names
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
.r:
    ret
.no:
    stc
    ret

%ifdef PKG_MEGA
; rw_xplan - kern_mega's arm (REALDMG-SPEC 10.3.1): under CPU_F_PM the
; cartridge's banks 1 on go above 1MB WHOLE and are addressed IN PLACE, a
; selector a bank, so nothing is ever copied down and the heap holds none of
; them - [rw_bcache] 2, one 16KB slot for the load to read through. CF = 1:
; not here (real mode, one bank, or the pool short), the plan goes on as
; before. Keeps AX
rw_xplan:
%ifdef RW_NOXPLAN                   ; (tests/rdgmega.py's one-claim leg only,
    stc                             ;   never shipped: the cartridge in one
    ret                             ;   claim under CPU_F_PM, rw_pmfill's)
%endif
    cmp byte [rw_pm], 0
    je .no
    mov cx, [rw_romkb]
    and cx, 0FFF0h                  ; whole banks
    cmp cx, 32
    jb .no
    cmp cx, RW_XROMMAX_KB
    ja .no
    push ax
    push cx
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can hand out
    mov dx, ax
    pop cx
    pop ax
    sub cx, 16                      ; banks 1 on, a 32KB window each: KB x 2
    shl cx, 1
    cmp dx, cx
    jb .no
    and word [rw_romkb], 0FFF0h
    mov word [rw_nslot], 1
    mov byte [rw_bcache], 2
    clc
    ret
.no:
    stc
    ret
%endif

; rw_bload - the cache's load, on the UI task in rw_name's folder: the slots,
; the store, then the file read raw a bank at a time - bank 0 into the view,
; banks 1 to K-1 into slots 0 to K-2 and every later one through slot K-1,
; each copied up as it comes. CF = 1: SI = the refusal, nothing kept.
rw_bload:
    mov ax, [rw_nslot]
    mov cl, 4
    shl ax, cl
%ifdef PKG_MEGA
    xor cx, cx                      ; CONVENTIONAL, whatever OS88_HIGHMEM
    call OSAPI_MEM_CLAIM_DMA        ;   says: the file is read INTO it, and
%else                               ;   a read above 1MB bounces sector by
    call OSAPI_MEM_CLAIM            ;   sector (a 1MB cartridge's load ran
%endif                              ;   minutes on a 286, 16.2.6)
                                    ; DX = the slots (the bank table names
%ifdef RDG_AOT                      ;   them: rw_mreloc, rwmove.inc)
    call rw_atight                  ; (the claim compacted what it had to)
%endif
    jc .room
    mov [rw_slotseg], dx
    mov byte [rw_slotlent], 0       ; (none lent, no buffer in one:
    mov byte [rw_loans], 0          ;   rw_mclaim)
    mov word [rw_lused], 0
    mov ax, [rw_romkb]
    sub ax, 16
%ifdef PKG_MEGA
    cmp byte [rw_bcache], 2         ; in place: a 32KB window a bank, the
    jne .sz                         ;   bank its upper half (10.3.1)
    shl ax, 1
.sz:
%endif
    mov cx, 1024
    mul cx
    call OSAPI_XMEM_ALLOC           ; DX:AX = the store's base
    jc .noxms
    mov [rw_xbase], ax
    mov [rw_xbase + 2], dx
%ifdef RDG_AOT
    mov word [rw_aebank], 0         ; (the ROM in the package: from bank 0)
%endif
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
%ifdef PKG_MEGA
    cmp byte [rw_bcache], 2         ; in place: (n - 1) x 32KB + 16KB, so
    jne .at                         ;   that window n - 1 of the run, at
    shl ax, 1                       ;   (n - 1) x 32KB, has the bank at its
    rcl dx, 1                       ;   4000h - the core's ROMX, whose 4000h
    add ax, 16384                   ;   is the bank's first byte
    adc dx, 0
.at:
%endif
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
%ifdef PKG_MEGA
    cmp byte [rw_bcache], 2
    jne .done
    mov ax, [rw_xbase]              ; the run: a data selector every 32KB,
    mov dx, [rw_xbase + 2]          ;   each a 64KB window (KERN-MEGA-PLAN
    call OSAPI_XMEM_RUN             ;   14.16) - DX the first, CX 8
    jc .xrun
    mov [rw_xsel], dx
    xor dx, dx                      ; the slot was the read's alone
    xchg dx, [rw_slotseg]
    call OSAPI_MEM_FREE
    mov word [rw_nslot], 0
    mov word [rdg_bfill], rw_xfill
    mov word [rdg_bmiss], rw_xmiss
.done:
%endif
    clc
    ret
%ifdef PKG_MEGA
.xrun:                              ; (no run of LDT entries that long)
    mov si, rw_s_room
    jmp short .why
%endif
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
    xor dx, dx
    xchg dx, [rw_slotseg]
    call OSAPI_MEM_FREE
    stc
    ret
.room:
    mov si, rw_s_room
    stc
    ret

; rw_bread - the file's next 16KB into DX:0000. CF = 1 unless it all came
rw_bread:
%ifdef RDG_AOT
    cmp byte [rw_aemb], 0           ; the ROM in the package: bank n at its
    je .f                           ;   offset (rwaot.inc)
    jmp rw_aebread
.f:
%endif
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

%ifdef PKG_MEGA
; rw_xfill - rdgsys's rdg_init in kern_mega's arm (10.3.1), the bank table
; just zeroed (DS = the view): every bank 1 on, window n - 1 of the run
rw_xfill:
    push ax
    push cx
    push di
    mov ax, [cs:rw_xsel]
    mov cx, [cs:rw_banks]
    dec cx
    mov di, RDG_BTAB + 2
.f:
    mov [di], ax
    add ax, 8                       ; (OSAPI_XMEM_RUN's step)
    inc di
    inc di
    loop .f
    mov ax, [cs:rw_b0]              ; ...and bank 0's, the view less 400h
    mov [RDG_BTAB], ax              ;   as a selector (rw_b0set)
    pop di
    pop cx
    pop ax
; rw_xmiss - ...whose table has every bank: rdg_setbank maps bank 0 for a
; bank past the cartridge's end, as it does for a fetch that failed
rw_xmiss:
    ret

; rw_b0set - rw_start, kern_mega's arm under CPU_F_PM (REALDMG-SPEC
; 10.3.2): the bank table's two words rdg_init cannot make, because each is
; a SUM on a segment. Bank 0's ([rw_b0]) is the segment whose 4000h is the
; view's 0000 - the view less 400h, inside the parts carve and below the
; view's own selector, so a SEG_SUB on the carve; and a cartridge in ONE
; claim has its banks 1 on at the claim + (n - 1) x 400h, which rw_pmfill
; asks for at rdg_init's call (rdg_nres, rdg_bfill). The in-place arm's
; rw_xfill takes [rw_b0] too. Every register kept
rw_b0set:
    cmp byte [rw_pm], 0
    je .r
    push ax
    push dx
    mov byte [op_subv], OP_SUBV_RAW ; AX = the view's paragraphs into the
    mov al, 2                       ;   carve, plus one
    call op_seg
    mov byte [op_subv], 0
    sub ax, 1 + 400h
    mov dx, [op_base]
    call rw_sub                     ; (none: 0, and bank 0 reads as garbage
    jnc .b                          ;   - no LDT entry left at all)
    xor dx, dx
.b:
    mov [rw_b0], dx
    cmp byte [rw_bcache], 0
    jne .x
    mov word [rdg_nres], 1          ; one claim: the table is rw_pmfill's
    mov word [rdg_bfill], rw_pmfill ;   (cleared again with the game)
    mov word [rdg_bmiss], 0
.x:
    pop dx
    pop ax
.r:
    ret

; rw_pmfill - rdgsys's rdg_init for a cartridge in one claim under CPU_F_PM
; (rw_b0set), its bank table just zeroed (DS = the view): bank 0's window and
; every bank 1 on, a SEG_SUB at (n - 1) x 400h into the claim - asked with DS
; ours, which the cell reads as the caller. A bank with no LDT entry stays 0:
; rdg_setbank maps bank 0 for it, as for a fetch that failed
rw_pmfill:
    push ax
    push cx
    push dx
    push di
    push ds
    push es
    push ds
    pop es                          ; ES = the view
    mov ds, [cs:rw_dsel]
    mov ax, [rw_b0]
    mov [es:RDG_BTAB], ax
    mov cx, [rw_banks]
    dec cx
    mov di, RDG_BTAB + 2
    xor ax, ax
.f:
    mov dx, [rw_romseg]
    call rw_sub
    jnc .s
    xor dx, dx
.s:
    mov [es:di], dx
    inc di
    inc di
    add ax, 400h
    loop .f
    pop es
    pop ds
    pop di
    pop dx
    pop cx
    pop ax
    ret

rw_b0           dw 0                ; bank 0's table word under CPU_F_PM
%endif

; rw_bmiss - rdgsys's rdg_setbank: BX = a bank x 2 with no segment in the
; table, DS = the view. The UI task fetches it now; the worker asks the UI
; task to and yields until the table has it - or, if the window is closing
; and the bank has not come, asks OSAPI_TASK_ALIVE, which a closed window
; never returns from (while rw_onclose waits for us it serves the bank, so the
; frame finishes and the worker leaves the machine as the close needs). A
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
    cmp word [cs:rw_bankreq], 0     ; served: the frame goes on - and FIRST,
    je .in                          ;   closing or not: rw_onclose serves us
    cmp byte [cs:rw_closing], 0     ;   (rw_bserve) while it waits for
    je .w                           ;   [rw_incore] to fall, which only the
    mov bx, [cs:rw_win]             ;   frame's end can do. Testing closing
    call OSAPI_TASK_ALIVE           ;   first looped here for ever, and the
    jmp short .w                    ;   close with it (REALDMG-SPEC 10.3)
.in:
    pop ax
    ret
.ui:
    call rw_bfetch
    pop ax
    ret

; SIDE FEATURES' ROOM (REALDMG-SPEC 10.3.3). The plan gives the bank cache
; every KB the heap has past RW_KEEPKB, so on a small heap a replay's or the
; log's buffer found none - and a slot is worth less than either while it is
; asked for, and more than either the rest of the time. So a side buffer
; takes the heap's room when OSAPI_MEM_AVAIL says there is some, and
; otherwise LIVES IN BANK SLOTS LENT for it: the cache stops using its top
; slots, one at a time and never below RW_LENDMIN, and the buffers are cut
; from them top down. Nothing is asked of the heap - the slots' claim is
; ours and keeps its size - so a loan cannot fail half way, and when the
; last buffer is let go every lent slot is the cache's again, empty.
RW_LENDMIN      equ 4

; rw_mclaim - AX = KB: DX = a side feature's buffer, CF = 1 none (rw_mrel
; lets it go). AX kept. UI task
rw_mclaim:
    push ax
    push bx
    push cx
    mov cx, ax
%ifndef RW_LENDTEST                 ; (tests/rdglend.py: the heap as if full)
    call OSAPI_MEM_AVAIL            ; (asked first: a refused claim compacts
    cmp ax, cx                      ;   the heap and sheds its caches)
    jb .lend
    mov ax, cx
    call OSAPI_MEM_CLAIM
    jnc .r
%endif
.lend:
    mov ax, cx
%ifdef RW_NOLEND                    ; (...and its control: no slot lent -
    stc                             ;   what a full heap said before 10.3.3)
%else
    call rw_lend
%endif
.r:
    pop cx
    pop bx
    pop ax
    ret

; rw_lend - AX = KB: DX = a buffer in lent slots, more lent as it needs. CF
; = 1: the cache has no slot to lend (none, or RW_LENDMIN left)
rw_lend:
    push ax
    push cx
    mov cl, 6
    shl ax, cl                      ; AX = paragraphs
.room:
    mov cl, [rw_slotlent]           ; room: lent x 400h paragraphs
    xor ch, ch
    xchg cl, ch
    shl cx, 1
    shl cx, 1                       ; CX = lent x 400h
    sub cx, [rw_lused]
    cmp cx, ax
    jae .cut
    call rw_slotlend
    jnc .room
    pop cx
    pop ax
    ret                             ; (CF)
.cut:
    add [rw_lused], ax
    mov ax, [rw_nslot]              ; the top: past every slot, lent or not
    add al, [rw_slotlent]
    adc ah, 0
    mov ah, al
    xor al, al
    shl ax, 1
    shl ax, 1                       ; x 400h
    add ax, [rw_slotseg]
    sub ax, [rw_lused]
    mov dx, ax
    inc byte [rw_loans]
    pop cx
    pop ax
    clc
    ret

; rw_mrel - DX = a buffer rw_mclaim gave: freed, or - one cut from lent
; slots - let go, and with the last of them every lent slot back. UI task
rw_mrel:
    push ax
    call rw_inlent
    jnc .heap
    dec byte [rw_loans]
    jnz .r
    mov word [rw_lused], 0
    call rw_mgive
    jmp short .r
.heap:
    call OSAPI_MEM_FREE
.r:
    pop ax
    ret

; rw_inlent - CF = 1 when DX is in the slots' claim (lent slots' buffers
; are). Keeps every register but AX
rw_inlent:
    cmp byte [rw_loans], 0
    je .no
    cmp dx, [rw_slotseg]
    jb .no
    mov ax, [rw_nslot]
    add al, [rw_slotlent]
    adc ah, 0
    mov ah, al
    xor al, al
    shl ax, 1
    shl ax, 1
    add ax, [rw_slotseg]
    cmp dx, ax
    jae .no
    stc
    ret
.no:
    clc
    ret

; rw_slotlend - the cache's top slot taken out of its use (nothing given
; back to the heap: rw_lend cuts buffers from it). Its bank leaves the table
; - unless it is the bank switched in, which is copied over slot 0's first,
; so S_ROMX and the core's DS never name a lent slot. CF = 1: none to lend.
; The machine is held still for it (rw_stgrab), or already was. UI task
rw_slotlend:
    cmp byte [rw_bcache], 1
    jne .no
    cmp word [rw_nslot], RW_LENDMIN
    ja .go
.no:
    stc
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [rw_lgrab], 0
    cmp byte [rw_stq], 2            ; (still already)
    je .held
    call rw_stgrab                  ; (CF: no game running, nothing to
    jc .held                        ;   hold still)
    mov byte [rw_lgrab], 1
.held:
    mov es, [rdg_viewseg]
    mov si, [rw_nslot]
    dec si
    shl si, 1                       ; SI = the top slot x 2
    mov bx, [rw_slotbank + si]      ; BX = its bank
    or bx, bx
    jz .gone
    shl bx, 1
    mov ax, [es:RDG_BTAB + bx]      ; AX = its table word
    cmp ax, [es:S_ROMX]
    jne .drop
    push ds                         ; the bank switched in: over slot 0,
    push si                         ;   whose bank leaves instead
    mov di, [rw_slotbank]
    shl di, 1
    jz .e0                          ; (slot 0 empty: nobody's to drop)
    mov word [es:RDG_BTAB + di], 0
.e0:
    mov cx, [rw_slotseg]            ; slot 0's segment, and the top's
    mov dx, si
    mov dh, dl                      ; (x 200h: SI is the slot x 2)
    xor dl, dl
    shl dx, 1
    add dx, cx
    push es
    mov es, cx
    mov ds, dx
    xor si, si
    xor di, di
    mov cx, 8192
    cld
    rep movsw
    pop es
    pop si
    pop ds
    mov ax, [rw_slotseg]            ; the bank's table word, S_ROMX and a
    sub ax, 400h                    ;   DS the core saved on it: slot 0's
    mov dx, [es:RDG_BTAB + bx]
    mov [es:RDG_BTAB + bx], ax
    mov [es:S_ROMX], ax
    cmp [es:S_SAVDS], dx
    jne .s0
    mov [es:S_SAVDS], ax
.s0:
    shr bx, 1
    mov [rw_slotbank], bx
    jmp short .gone
.drop:
    mov word [es:RDG_BTAB + bx], 0
.gone:
    mov word [rw_slotbank + si], 0
    dec word [rw_nslot]
    inc byte [rw_slotlent]
    cmp byte [rw_lgrab], 0
    je .x
    call rw_strun
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; rw_mgive - every lent slot the cache's again, EMPTY (rw_bfetch fills an
; empty slot first). Nothing the core names changes, so the machine runs on
rw_mgive:
    push si
.l:
    cmp byte [rw_slotlent], 0
    je .r
    mov si, [rw_nslot]
    shl si, 1
    mov word [rw_slotbank + si], 0
    inc word [rw_nslot]
    dec byte [rw_slotlent]
    jmp short .l
.r:
    pop si
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
    or di, di                       ; an EMPTY slot, given back by a side
    jz .e                           ;   feature (rw_mgive): this one
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
    jmp short .in
.e:
    mov bp, si
.in:
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
