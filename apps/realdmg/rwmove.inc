; =============================================================================
; os8088 - apps/realdmg/rwmove.inc
;
; RealDMG's claims MOVE (docs/REALDMG-SPEC.md section 14.4, SPEC.md 66.2,
; docs/plans/NO-PINNING-PLAN.md A1). Included by realdmg.asm, after every
; routine the machine runs a frame through, so none of them moved for it.
;
; ALL THE WORK IS DONE AT MOVE TIME. The emulation reads its segments out of
; the state block (S_ROMX, S_RAMSEG, S_JITSEGA...), the bank table and words
; of ours, once a bank switch or once a frame; rw_mreloc puts each of them
; right when the compactor calls it, and the core, the recompiler's part and
; every routine a frame runs gain no instruction. A move happens only with
; the worker PARKED at OSAPI_TASK_ALIVE (the top of its loop, the machine
; between two runs and every register spent) or with the machine the UI
; task's own between two frames (full screen): a package's data claim is
; pinned while its worker runs (SPEC.md 66.3.1, inst_seg_parked).
;
; THE RECOMPILER'S CODE CANNOT BE PUT RIGHT IN PLACE: a chain into the other
; cache segment is a `jmp far` with the segment in its bytes (16.2.1), the
; core's own holes hold such chains too (16.2.2), and only the chains into an
; OLDER tier are logged (16.2.3) - so there is no list of the sites. A move of
; either cache segment FLUSHES the cache instead (rt_init over the new
; segments, which forgets every block) - a re-warm like a recycle's, once per
; move and never per instruction. Static's table is fixed and would stay
; empty: it goes dynamic at once and the worker preloads again at its next
; turn (rw_seg2q, rw_jiton).
;
; THE PARTS CARVE - the core and the view, op_load's 128KB - moves too, with
; the parts standard's own proc and rw_partsmv as its OP_ONMOVE hook (SPEC.md
; 20.12.12): rw_mdecl declares and pins it with the table. The log's text,
; claimed and freed inside one rw_logwrite, is the one claim left pinned
; (bucket C). Every claim in the
; table is pinned again around a call that can claim while its segment is in
; a register (rw_mpin: every file call, through rw_fgo, and the load).
; =============================================================================

; the words that each name one claim of ours, by its base (rw_mdecl's walk;
; rw_romseg is the view itself when the cartridge is the bank cache's, and
; the view is not ours to declare)
rw_mtab:        dw rw_pseg, rdg_dmgseg, rw_jitseg, rw_jit2seg, rw_romseg
                dw rw_slotseg, rw_ramseg, rw_prfseg, rw_logseg, rw_rpseg
                dw 0
%ifdef PKG_MEGA
rw_jitx:        times RT_XMAX dw 0  ; the arm's extra cache tiers' data
rw_jitxk:       db 0                ;   halves, or 0, [rw_jitxk] of them:
                                    ;   SEG_SUBC pairs in one pool block
                                    ;   (rw_jitxget, REALDMG-SPEC 16.2.6)
%endif

; rw_mpin - every claim of ours PINNED (OSAPI_MEM_MOVABLE AX = 0): around a
; call that can claim while one of their segments is in a register - a file
; call (rw_fgo), the load (rw_load). Every register and flag kept.
rw_mpin:
    pushf
    push ax
    xor ax, ax
    jmp short rw_mdecl

; rw_mfree - ...and every one free to move again (rw_mreloc), unless a load
; holds them ([rw_mhold]). Every register and flag kept - rw_fback's callers
; read its CF.
rw_mfree:
    pushf
    push ax
    mov ax, rw_mreloc
    cmp byte [rw_mhold], 0
    je rw_mdecl
    pop ax
    popf
    ret

; rw_mdecl - AX = the proc or 0, for every word of rw_mtab that names a
; claim. Entered with the flags and AX pushed, in that order
rw_mdecl:
    push bx
    push dx
    push si
    mov si, rw_mtab
.l:
    mov bx, [si]
    inc si
    inc si
    or bx, bx
    jz .r
    mov dx, [bx]
    or dx, dx
    jz .l
    cmp bx, rw_romseg               ; (the view, in the bank cache's load)
    jne .d
    cmp byte [rw_bcache], 0
    jne .l
.d:
    call OSAPI_MEM_MOVABLE
    jmp short .l
.r:
%ifdef RDG_AOT
    mov al, OP_CARVE                ; A PRECOMPILED CARTRIDGE'S CARVE STAYS:
    jmp short .pin                  ;   its images jump far into the core's
                                    ;   holes, the segment in their bytes
                                    ;   (REALDMG-SPEC 20.10)
%else
    or ax, ax                       ; ...and the parts carve, the core and
    mov al, OP_CARVE                ;   the view (os88parts.inc's own proc,
    jz .pin                         ;   then rw_partsmv)
    call op_movable
    jmp short .x
%endif
.pin:
    call op_pin
.x:
    pop si
    pop dx
    pop bx
    pop ax
    popf
    ret

; rw_mlent - rw_mreloc's, the slots moving: BX = where they were, AX = the
; move, CX = the slots lent or not. A side buffer cut from a lent slot
; (rwbank.inc's rw_lend, REALDMG-SPEC 10.3.3) moves with them - a replay's
; buttons, the log's records; the log's text is only ever held pinned
; (rw_logwrite). Keeps every register
rw_mlent:
    cmp byte [rw_loans], 0
    je .r
    push cx
    push dx
    mov ch, cl                      ; CX = the claim's paragraphs
    xor cl, cl
    shl cx, 1
    shl cx, 1
    mov dx, [rw_rpseg]
    sub dx, bx
    cmp dx, cx
    jae .lg
    add [rw_rpseg], ax
.lg:
    mov dx, [rw_logseg]
    sub dx, bx
    cmp dx, cx
    jae .x
    add [rw_logseg], ax
.x:
    pop dx
    pop cx
.r:
    ret

; rw_mreloc - A CLAIM OF OURS MOVED (SPEC.md 66.2): BX = where it was, DX =
; where it is, DS = CS = ours, ES = KERNEL_SEG. Every word that names it, and
; every one derived from it, put right; may clobber AX-DI and ES. It runs in
; the allocator: nothing here claims, draws, yields or touches a file.
rw_mreloc:
    mov ax, dx
    sub ax, bx                      ; AX = the move, paragraphs
    mov es, [rdg_viewseg]
    cmp bx, [rw_pseg]               ; the frames: rw_fseg is 11,520 bytes in
    jne .n1
    mov [rw_pseg], dx
    add [rw_fseg], ax
    ret
.n1:
    cmp bx, [rdg_dmgseg]             ; the damage memory: the one word
    jne .n2
    mov [rdg_dmgseg], dx
    ret
.n2:
    mov si, rw_ramseg               ; cartridge RAM: ours and S_RAMSEG
    mov di, S_RAMSEG
    cmp bx, [si]
    je .two
    mov si, rw_prfseg               ; Static's profile: ours and S_PRFSEG
    mov di, S_PRFSEG
    cmp bx, [si]
    je .two
    cmp bx, [rw_rpseg]              ; a replay's buttons: the one word
    jne .n3
    mov [rw_rpseg], dx
    ret
.n3:
    cmp bx, [rw_logseg]             ; the log's records: the one word (the
    jne .n4                         ;   write pins them before it copies
    mov [rw_logseg], dx             ;   the word, rw_logwrite)
    ret
.n4:
    cmp byte [rw_bcache], 0         ; THE ROM: its banks in the bank table
    jne .n5
    cmp bx, [rw_romseg]
    jne .n5
    mov [rw_romseg], dx
    mov cx, [rw_banks]              ; CX = its length, paragraphs
    jmp short .bank
.n5:
    cmp bx, [rw_slotseg]            ; ...or the bank cache's slots
    jne .n6
    mov [rw_slotseg], dx
    mov cx, [rw_nslot]              ; (and the lent ones, whose side
    add cl, [rw_slotlent]           ;   buffers move with them: 10.3.3)
    adc ch, 0
    call rw_mlent
.bank:
    mov ch, cl                      ; x 400h
    xor cl, cl
    shl cx, 1
    shl cx, 1
    mov si, RDG_BTAB                 ; every bank the claim holds: a word a
.b:                                 ;   bank, the segment whose 4000h is the
    mov di, [es:si]                 ;   bank's first byte, so the bank at
    add di, 400h                    ;   400h past it
    sub di, bx
    cmp di, cx
    jae .bn
    add [es:si], ax
.bn:
    inc si
    inc si
    cmp si, RDG_BTAB + 512 * 2
    jb .b
    mov di, [es:S_ROMX]             ; ...the bank switched in now - none
    or di, di                       ;   when it was switched to lazily
    jz .bx                          ;   (REALDMG-SPEC 10.3.2)
    add di, 400h
    sub di, bx
    cmp di, cx
    jae .bx
    add [es:S_ROMX], ax
.bx:
    mov di, S_ROMSEG                ; ...and the whole image's
.one:
    cmp [es:di], bx
    jne .r
    mov [es:di], dx
.r:
    ret
.two:
    mov [si], dx
    jmp short .one
.n6:
    cmp bx, [rw_jitseg]             ; THE RECOMPILER'S CLAIM: its cache, its
    jne .n7                         ;   block table and its own code, whose
    mov [rw_jitseg], dx             ;   four doors are far pointers
    add [rw_jpseg], ax
    call rw_jdset                   ;   (by their code selector, 16.2.4)
    cmp [es:S_JITSEGA], bx          ; the core running over it: flushed
    je rw_mjit
    ret
.n7:
    cmp bx, [rw_jit2seg]            ; ...and its second 64KB (the arm's
    jne .r                          ;   extra tiers are a pool block's now,
    mov [rw_jit2seg], dx            ;   rebased and not ours to put right,
                                    ;   16.2.6)
    cmp [es:S_JITSEGB], bx
    jne .r
    ; on into rw_mjit

; rw_mjit - a cache segment the core was translating into moved: rt_init
; over the claims where they are now (rw_rtinit) - the segments, the doors,
; the tails and a flush, so no block and no chain is left naming the old
; place - and the core left as it was: the interpreter if it was, and
; Static's fixed table dynamic until the worker's preload (rw_seg2q)
rw_mjit:
    push ds
    mov ds, [cs:rdg_viewseg]
    mov al, [S_JITON]
    push ax
    call rw_rtinit
    pop ax
    or al, al
    jnz .on
    mov byte [S_JITON], 0           ; (rw_jitoff's three)
    mov word [S_RESVEC], RDGC_RDG_DISPATCH
    call rw_jdoff
.on:
    cmp byte [S_JITFIX], 0
    je .x
    mov byte [S_JITFIX], 0          ; Static: an empty fixed table would
    mov byte [S_JITPRE], 0          ;   translate nothing; dynamic until the
    mov byte [cs:rw_seg2q], 1       ;   worker preloads it again (rw_jiton)
.x:
    pop ds
    ret

; rw_partsmv - THE PARTS CARVE MOVED (OP_ONMOVE, SPEC.md 20.12.12): the core
; (parts 0 and 1) and the view (part 2) with it. [op_base] is the new one
; already; BX = where the carve was, DX = where it is. Every segment banked
; from an op_seg answer, and every copy of one, is put right: rdg_viewseg and
; rdg_coreseg, and in the view - which moved with the carve, the state block
; with it - S_VIEW, the core's far vectors (S_DISP, S_RESVEC, S_RETFAR), the
; core's DS a run left (S_SAVDS), bank 0's word in the bank table and S_ROMX
; when it is bank 0 (the view less 400h), and S_ROMSEG with rw_romseg when the
; bank cache's load put bank 0 in the view - each by asking whether it lay in
; the carve. Translated code jumps far into the core's holes and the core
; tier IS the core, so a recompiler that has run is flushed (rw_mjit). A
; relocation proc's rules (SPEC.md 66.3): nothing here claims, draws, yields
; or touches a file
rw_partsmv:
    mov cx, [rdg_viewseg]           ; the old view: no game since the entry
    jcxz .r                         ;   proc, nothing banked yet
    mov ax, [rdg_coreseg]           ; the core's old CODE selector, which
    call rw_csel                    ;   the far vectors hold (16.2.4) - on a
    mov [rw_pmcs], ax               ;   real-mode kernel the segment itself,
                                    ;   which the walk below puts right
    add cx, 1000h                   ; CX = the carve's length, paragraphs: up
    sub cx, bx                      ;   to the old view's end (bank 0's word,
    mov ax, dx                      ;   the view less 400h, lies in the core
    sub ax, bx                      ;   below it, so one test serves)
    push ds                         ; AX = the move
    pop es
    mov si, rw_pmtab                ; ours...
    call .tab
    mov es, [rdg_viewseg]           ; ...the state block's, in the view that
    mov si, rw_pmview               ;   moved with the carve...
    call .tab
    mov di, RDG_BTAB                 ; ...and the bank table's
.b:
    call .fix
    inc di
    inc di
    cmp di, RDG_BTAB + 512 * 2
    jb .b
    mov ax, [rdg_coreseg]           ; ...and on kern_mega the far vectors,
    call rw_csel                    ;   which name the core's CODE selector
    mov dx, [rw_pmcs]               ;   - a value no walk can move: what
    cmp ax, dx                      ;   held the old one holds the new
    je .cs
    mov si, rw_pmcode
.c:
    mov di, [si]
    or di, di
    jz .cs
    cmp [es:di], dx
    jne .cn
    mov [es:di], ax
.cn:
    inc si
    inc si
    jmp short .c
.cs:
    cmp word [es:S_JITSEGA], 0      ; a recompiler that has run: flushed
    jne rw_mjit
.r:
    ret
.tab:                               ; every ES: word the list at SI names
    mov di, [si]
    or di, di
    jz .r
    call .fix
    inc si
    inc si
    jmp short .tab
.fix:                               ; ES:DI moves with the carve if it lay
    mov dx, [es:di]                 ;   in it - unless it is a SELECTOR
    test dl, 4                      ;   (bit 2, the LDT's: a heap segment is
    jnz .r                          ;   a multiple of 32), a code alias or
    sub dx, bx                      ;   kern_mega's banks in place (10.3.1)
    cmp dx, cx
    jae .r
    add [es:di], ax
    ret

rw_pmtab:       dw rdg_viewseg, rdg_coreseg, rw_romseg, 0
rw_pmview:      dw S_VIEW, S_DISP + 2, S_RESVEC + 2, S_RETFAR + 2, S_SAVDS
                dw S_ROMX, S_ROMSEG, S_JITOUT + 2, S_COREDS, 0
rw_pmcode:      dw S_DISP + 2, S_RESVEC + 2, S_RETFAR + 2, S_JITOUT + 2, 0
rw_pmcs         dw 0                ; the core's code selector before a move
