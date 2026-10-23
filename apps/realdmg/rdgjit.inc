; =============================================================================
; os8088 - apps/realdmg/rdgjit.inc
;
; RealDMG's RECOMPILER (docs/REALDMG-SPEC.md section 16): Game Boy code from
; ROM translated, a block at a time, into 8086 code in the core's own
; segment. Included by a host after rdgsys.inc, with build/rdgcore.inc (the
; core's offsets) and build/rdgtmpl.bin (the templates) on its include path.
;
; A translated instruction is its interpreter handler's own body - apps/
; realdmg/rdgtmpl.py lifts each one out of the generated core with the dispatch
; taken off and the operands made immediates - so translated code computes
; exactly what the interpreter does, and every gate on the one is a gate on
; the other. What it saves is the interpreter's fixed cost: the three-
; instruction dispatch, which flushes a 286's prefetch queue every time, and
; the operand fetches through SI.
;
; THE CODE goes into a 64KB CACHE SEGMENT of the host's, which also holds
; copies of the core's helper tails at their own offsets (rdgtmpl.py lists
; the slots): a template reaches every helper with a near jump, which is
; what lets one be copied at all, and ~60KB is left for code. A helper that
; leaves for rdgsys records its own CS first, so rdgsys comes back to the copy
; that called it. When the cache is full, everything is thrown away and
; translation starts again (rt_flush).
;
; A BLOCK is straight-line code from one PC to the first control transfer, at
; most RT_MAXI instructions, and never across 4000h or 8000h. It is keyed by
; its PC and, in 4000-7FFF, the ROM bank (tr_enter's table). What leaves it:
;   - a control transfer, translated here: its M-cycles, the event check the
;     interpreter makes at a branch, and on to tr_enter with SI = the target;
;   - an opcode that has no template and is not a jump (HALT, STOP, RETI, an
;     invalid one): SI = its PC and the interpreter's dispatch, which comes
;     back through tr_enter at its next branch;
;   - a store that took an interrupt (rdg_irqnow moves SI: the store is
;     guarded with SI = the next PC, and left by when it moved), or that
;     switched the bank under a block running from 4000-7FFF.
; RAM is never translated but HRAM, behind a guard of its own bytes
; (tr_hram, rt_guard): tr_enter interprets a PC at 8000h-FF7Fh.
; =============================================================================

%include "rdgcore.inc"

RT_MAXI         equ 40              ; instructions a block at most
RT_CTL          equ 96              ; bytes a control transfer's code may take
                                    ;   (JR cc to a spin-wait on (HL) in
                                    ;   4000-7FFF 86 - rt_exit there is 29 -
                                    ;   RET cc 90, CALL cc 73: each rt_room
                                    ;   asks for what it writes, since most
                                    ;   gaps are ~106)
RT_FALL         equ 32              ; ...and a block's fall-through exit (31)
; the translator's scratch, in the view after JIT_BUF's 128 bytes of code
RT_PC0          equ JIT_BUF + 80h   ; word: the block's PC
RT_N            equ JIT_BUF + 82h   ; word: code bytes in JIT_BUF
RT_IX           equ JIT_BUF + 84h   ; word: the instruction's place in it
RT_START        equ JIT_BUF + 86h   ; word: the block's offset in the core
RT_CNT          equ JIT_BUF + 88h   ; word: instructions so far
RT_BANKW        equ JIT_BUF + 8Ah   ; word: S_BANK/S_BANKHI as it was
RT_NEXT         equ JIT_BUF + 8Ch   ; word: the instruction's next PC
RT_IMM          equ JIT_BUF + 8Eh   ; word: its operand bytes
RT_REC          equ JIT_BUF + 90h   ; word: its template record
RT_GUARD        equ JIT_BUF + 92h   ; byte: 1 store guard, 2 bank guard
RT_OP           equ JIT_BUF + 93h   ; byte: the opcode
RT_T            equ JIT_BUF + 94h   ; word: a jump's target
RT_IDLE         equ JIT_BUF + 96h   ; byte: rt_idle's answer for it
RT_V16          equ JIT_BUF + 9Ah   ; word: what rt_tmpl writes at IMM16
RT_ENT          equ JIT_BUF + 9Ch   ; word: the block's entry (its guard's)
RT_GLEN         equ JIT_BUF + 9Eh   ; byte: its control transfer's bytes
RT_HMAX         equ 16              ; an HRAM block's bytes at most (rt_guard)
RT_PEND         equ JIT_BUF + 98h   ; word: M-cycles of PURE templates not yet
                                    ;   taken off BP (rt_flushbp)
RT_LOOP         equ JIT_BUF + 0A0h  ; 16: a spin-wait's body, for rt_idle
RT_P0           equ JIT_BUF + 0B0h  ; word: RT_PEND as rt_ctl found it, each
                                    ;   arm's own (rt_subbp, rt_pend0)

RT_HFREE        equ RDGT_HFREE       ; the table's free ranges: offset, count
RT_CFREE        equ RDGT_CFREE       ; ...the CORE's holes, the second tier
                                    ;   (REALDMG-SPEC 16.2.2)
RT_LOG          equ 0FE00h          ; the link log, in the first segment
                                    ;   (16.2.3; rdgtmpl.py's LOG)
RT_SQHOLES      equ 16              ; RT_SQUEEZE: the core tier's half-slots
RT_HTAIL        equ RDGT_HTAIL       ; ...its tail slots
RT_HJD          equ RDGT_HJD         ; ...the interpreter's JDISPATCH sites
                                    ;   (rdgtmpl.py writes all three)
rt_tab:
    incbin "rdgtmpl.bin"

; rt_init - DS = the view, after rdg_init; AX = the cache's segment, a 64KB
; claim of the host's, BX the block table's, CX a second 64KB cache segment
; or 0 (REALDMG-SPEC 16.2.1) - 64KB too, since a template reaches every
; tail by a near jump - and DL the table's mask (S_HMASK): 31, 64KB, or 7,
; 16KB. The core's three `and bh,mask` take it before the tails are copied. The core's helper tails copied into
; each at their own
; offsets, the cache empty, and the core's two doors pointed at it -
; tr_enter's miss at rdg_jit, and rdgsys's resume (after every event) at
; tr_enter rather than the interpreter's dispatch
rt_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [S_JITSEGA],ax
    mov [S_JITSEGB],cx
    mov [S_JITHSEG],bx
    mov [S_HMASK],dl
    push es
    mov es,[S_DISP+2]               ; the core's masks, which the copies
    mov [es:RDGC_TR_HM0],dl          ;   below take with them
    mov [es:RDGC_TR_HM1],dl
    mov [es:RDGC_TR_HM2],dl
    pop es
    mov [S_JITOUT+2],ax
    mov word [S_JITVEC],rdg_jit
    mov [S_JITVEC+2],cs
    mov word [S_RESVEC],RDGC_TR_ENTER
    push cx
    call .tails
    pop ax
    or ax,ax
    jz .one
    call .tails
.one:
    call rt_flush
    mov byte [S_JITON],1
    mov al,1
    call rt_dispatch                ; the interpreter's sites look again
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.tails:                             ; into AX's segment
    mov es,ax
    mov bx,[cs:rt_tab + RT_HTAIL]       ; the tail slots
    mov dx,[cs:rt_tab + RT_HTAIL + 2]
.slot:
    mov ah,[cs:rt_tab + bx]
    xor al,al
    mov si,ax
    mov di,ax
    mov cx,128
    push ds
    mov ds,[S_DISP+2]               ; the core
    rep movsw
    pop ds
    inc bx
    dec dx
    jnz .slot
    ret

; rt_resume - DS = the view: the recompiler back on over the cache it had
; when it was turned off (the package's rw_jitoff), nothing forgotten - the
; cache holds only ROM's translations, keyed by PC and bank, and neither
; changed meanwhile. For the package's automatic recompiler (REALDMG-SPEC
; 16.7), which turns it off and on to time both.
rt_resume:
    push ax
    mov word [S_RESVEC],RDGC_TR_ENTER
    mov byte [S_JITON],1
    mov al,1
    call rt_dispatch
    pop ax
    ret

; rt_dispatch - AL = 1: the interpreter's control transfers look for
; translated code (JDISPATCH as built); AL = 0: they DISPATCH, as the
; interpreter did before there was a recompiler - each site's first five
; bytes, cmp si,8000h / jae, become mov dh,[si] / inc si / jmp dx, so a
; machine that never translates (an 8088) pays nothing for one. DS = view.
rt_dispatch:
    push ax
    push bx
    push cx
    push si
    push es
    mov es,[S_DISP+2]               ; the core
    mov si,[cs:rt_tab + RT_HJD]
    mov cx,[cs:rt_tab + RT_HJD + 2]
.s:
    mov bx,[cs:rt_tab + si]
    or al,al
    jz .off
    mov word [es:bx],0FE81h         ; cmp si,8000h / jae (+3 stays)
    mov word [es:bx+2],8000h
    mov byte [es:bx+4],73h
    jmp short .n
.off:
    mov word [es:bx],348Ah          ; mov dh,[si] / inc si / jmp dx
    mov word [es:bx+2],0FF46h
    mov byte [es:bx+4],0E2h
.n:
    inc si
    inc si
    loop .s
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; rt_flush - DS = the view: every block forgotten, the first segment's
; first gap current (tier 0)
rt_flush:
    push ax
    push cx
    push di
    push es
    mov es,[S_JITHSEG]
    mov di,JIT_HASH
    mov al,[S_HMASK]                ; (mask + 1) x 256 entries of 8 bytes
    xor ah,ah
    inc ax
    mov cl,10
    shl ax,cl
    mov cx,ax
    xor ax,ax
    rep stosw
    cmp word [S_JITGNX],0FFF0h      ; (the generations near a wrap: from
    jb .g                           ;   nothing, every link forgotten with
    mov word [S_JITGNX],0           ;   the table - 16.2.3)
.g:
    mov ax,[S_JITSEGA]
    mov byte [S_JITTIER],0
    mov byte [S_JITUSED],1
    mov word [S_JITLN],0            ; (the link log: nothing is linked)
    call rt_tier
    inc word [S_JITFL]
    pop es
    pop di
    pop cx
    pop ax
    ret

; rt_tier - AX = the tier's segment, [S_JITTIER] set: it is the one being
; written, its gap list current - the cache segments' free ranges (tiers 0
; and 2), or the core's holes (tier 1, REALDMG-SPEC 16.2.2: a byte a
; half-slot, where its fill begins, FFh for none) - and its first gap.
; Keeps every register but AX
rt_tier:
    push si
    push es
    mov [S_JITSEG],ax
    mov es,ax                       ; its generation: the newest (16.2.3)
    inc word [S_JITGNX]
    mov si,[S_JITGNX]
    mov [es:0FFF0h],si
    pop es
    mov si,RT_HFREE
    cmp byte [S_JITTIER],1
    jne .l
    mov si,RT_CFREE
.l:
    mov ax,[cs:rt_tab + si]
    mov [S_JITGL],ax
    mov ax,[cs:rt_tab + si + 2]
%ifdef RT_SQUEEZE
    cmp word [S_JITLIM],0           ; (the harness's squeezed tiers: a cache
    je .n                           ;   segment's first gap, cut to S_JITLIM,
    mov ax,1                        ;   and the core's first RT_SQHOLES
    cmp byte [S_JITTIER],1          ;   half-slots, so that a test ROM fills
    jne .n                          ;   all three)
    mov ax,RT_SQHOLES
.n:
%endif
    mov [S_JITGN],ax
    pop si
    ; on into rt_gap0

; rt_gap0 - [S_JITSEG]'s first gap current (none: [S_JITEND] 0, so that the
; next rt_room moves on to the next tier)
rt_gap0:
    push ax
    push bx
    push dx
    mov word [S_JITGI],0FFFFh
    call rt_gnext
    jnc .g
    xor ax,ax
    mov [S_JITPTR],ax
    mov word [S_JITEND],0
    jmp short .r
.g:
    mov [S_JITPTR],ax
    mov ax,dx
    call rt_gapend
.r:
    pop dx
    pop bx
    pop ax
    ret

; rt_gnext - [S_JITGI] on to the tier's next gap: AX = its start, DX = its
; end, CF = 0; or CF = 1, none left. Clobbers BX
rt_gnext:
    mov bx,[S_JITGI]
    inc bx
    cmp bx,[S_JITGN]
    jae .none
    mov [S_JITGI],bx
    cmp byte [S_JITTIER],1
    je .core
    shl bx,1                        ; a free range: start, end
    shl bx,1
    add bx,[S_JITGL]
    mov ax,[cs:rt_tab + bx]
    mov dx,[cs:rt_tab + bx + 2]
    clc
    ret
.core:
    push bx
    add bx,[S_JITGL]
    mov al,[cs:rt_tab + bx]         ; where half-slot BX's fill begins
    pop bx
    cmp al,0FFh
    je rt_gnext                     ; (under HMIN: not listed)
    mov dx,bx                       ; DX = the half-slot's start, BX x 128:
    shr dx,1                        ;   (BX / 2) x 256, + 128 if BX is odd
    mov dh,dl
    mov dl,0
    rcr dl,1
    xor ah,ah
    add ax,dx                       ; AX = the fill's start
    add dx,128                      ; DX = its end, the half-slot's - the
    jnz .e                          ;   last one's capped as the cache's
    mov dx,0FFF0h                   ;   list caps it
.e:
    clc
    ret
.none:
    stc
    ret

; rt_tseg - AL = a tier: AX = its segment, or 0 when there is none - the
; core's holes when the host has not turned them off (S_JITNOC), the second
; segment when there is one
rt_tseg:
    or al,al
    jnz .c
    mov ax,[S_JITSEGA]
    ret
.c:
    cmp al,1
    jne .b
    xor ax,ax
    cmp byte [S_JITNOC],0
    jne .r
    mov ax,[S_DISP+2]               ; the core
.r:
    ret
.b:
    mov ax,[S_JITSEGB]
    ret

; rt_unlink - AX = the segment of the tier being recycled (REALDMG-SPEC
; 16.2.3): every link the log holds into it undone, and every logged site
; IN it dropped, its code about to go. The log is RT_LOG in the first cache
; segment, 4 bytes an entry - a site's offset and segment - written by
; tr_lpatch for a chain into an OLDER tier; a chain into a newer one, or in
; one tier, goes with its site. An exit goes back to its unlinked bytes
; (rt_exit's: mov dx,SITE / jmp tr_lsite, and for one into 4000-7FFF seven
; NOPs after), a RET's inline cache entry gets the PC no return has
; (rt_ret's FFFFh). Keeps every register
rt_unlink:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov dx,ax                       ; DX = the tier going
    mov cx,[S_JITLN]
    mov es,[S_JITSEGA]
    mov si,RT_LOG
    mov di,si
    add cx,si                       ; CX = the log's end
.l:
    cmp si,cx
    jae .e
    mov bx,[es:si]                  ; the site,
    mov ax,[es:si+2]                ;   its segment
    add si,4
    cmp ax,dx
    je .l                           ; in the tier: gone with it
    mov ds,ax
    call rt_undo
    jnc .l                          ; undone: dropped
    mov [es:di],bx
    mov [es:di+2],ds                ; (rt_undo took AX)
    add di,4
    jmp short .l
.e:
    pop es
    pop ds
    sub di,RT_LOG
    mov [S_JITLN],di
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rt_undo - DS:BX = a logged site, DX = the tier going: CF = 0 when the
; site's link into it is undone and the entry is done with, CF = 1 to keep
; it. A RET's entry is kept while either of its two cache entries is still
; a far jump, which may be the link another entry logged
rt_undo:
    mov al,[bx]
    cmp al,81h                      ; a RET's: cmp si,PC
    je .ret
    cmp al,36h                      ; an exit into 4000-7FFF: the bank
    je .bank                        ;   check, then the jump at +8
    cmp al,0EAh
    jne .keep
    cmp [bx+3],dx
    jne .keep
    call .six
    clc
    ret
.bank:
    cmp byte [bx+8],0EAh
    jne .keep
    cmp [bx+11],dx
    jne .keep
    call .six
    mov ax,9090h                    ; the seven spare
    mov [bx+6],ax
    mov [bx+8],ax
    mov [bx+10],ax
    mov [bx+12],al
    clc
    ret
.six:                               ; mov dx,SITE / jmp tr_lsite
    mov byte [bx],0BAh
    mov [bx+1],bx
    mov byte [bx+3],0E9h
    mov ax,RDGC_TR_LSITE
    sub ax,bx
    sub ax,6
    mov [bx+4],ax
    ret
.ret:
    xor ah,ah                       ; AH = a far jump stays
    push bx
    call .ent
    add bx,19
    call .ent
    pop bx
    or ah,ah
    jz .drop
.keep:
    stc
    ret
.drop:
    clc
    ret
.ent:                               ; an entry: cmp si,PC at +0, its jump
    cmp byte [bx+14],0EAh           ;   at +14
    jne .er
    cmp word [bx+2],0FFFFh
    je .er
    cmp [bx+17],dx
    jne .far
    mov word [bx+2],0FFFFh
.er:
    ret
.far:
    inc ah
    ret

; rt_forget - AX = a tier's segment: every block in it out of the table
; (REALDMG-SPEC 16.2.3) - its entry cleared, so its PC counts again from
; nothing. Links into it are undone first (rt_unlink). Keeps every register
rt_forget:
    push bx
    push cx
    push di
    push es
    mov es,[S_JITHSEG]
    mov cl,[S_HMASK]                ; (mask + 1) x 256 entries of 8 bytes
    xor ch,ch
    inc cx
    xchg cl,ch
    mov bx,JIT_HASH
.e:
    cmp [es:bx+6],ax
    jne .n
    cmp word [es:bx+4],0
    je .n
    xor di,di
    mov [es:bx],di
    mov [es:bx+2],di
    mov [es:bx+4],di
    mov [es:bx+6],di
.n:
    add bx,8
    loop .e
    pop es
    pop di
    pop cx
    pop bx
    ret

; rt_gapend - AX = the end of a gap: [S_JITEND], 3 bytes short of it for
; its way out
rt_gapend:
%ifdef RT_SQUEEZE
    cmp byte [S_JITTIER],1          ; (not the core's holes: RT_SQHOLES)
    je .e
    cmp word [S_JITLIM],0           ; (the harness's segments, made small
    je .e                           ;   so the second fills, and both)
    cmp ax,[S_JITLIM]
    jbe .e
    mov ax,[S_JITLIM]
.e:
%endif
    sub ax,3
    mov [S_JITEND],ax
    ret

;------------------------------------------------------------------------------
; STATIC (REALDMG-SPEC 16.8): a profile of what the translator made, and the
; preload that makes it again before the game runs.
;------------------------------------------------------------------------------

; rt_prfix - SI = a PC, DL = its bank key: BX = its first profile index
rt_prfix:
    mov bx,si
    xor bl,dl
    mov al,bh
    shr al,1
    shr al,1
    shr al,1
    xor bl,al
    and bx,[S_PRFMASK]
    ret

; rt_prfadd - DS = the view: the block just translated, [RT_PC0] with the
; key [S_JITBK], into the profile: its weight + 1 (at most 255), or a new
; entry in the first empty of the 16 from its hash, or over the lightest of
; them when that weighs 1. Not while preloading, and never HRAM. Every
; register kept.
rt_prfadd:
    cmp word [S_PRFSEG],0
    je .r0
    cmp byte [S_JITPRE],0
    jne .r0
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si,[RT_PC0]
    cmp si,8000h
    jae .r
    mov dl,[S_JITBK]
    call rt_prfix
    mov es,[S_PRFSEG]
    mov cx,16
    mov dh,0FFh                     ; the lightest weight seen, and DI its
.p:                                 ;   entry
    push bx
    shl bx,1
    shl bx,1
    mov al,[es:bx+3]
    or al,al
    jz .new
    cmp [es:bx],si
    jne .o
    cmp [es:bx+2],dl
    jne .o
    inc al
    jz .sat                         ; (255 stays 255)
    mov [es:bx+3],al
    inc word [S_PRFNEW]
.sat:
    pop bx
    jmp short .r
.o:
    cmp al,dh
    jae .nx
    mov dh,al
    mov di,bx
.nx:
    pop bx
    inc bx
    and bx,[S_PRFMASK]
    loop .p
    cmp dh,1                        ; sixteen taken: over the lightest, if
    jne .r                          ;   it weighs 1
    mov bx,di
    push bx
.new:
    mov [es:bx],si
    mov [es:bx+2],dl
    mov byte [es:bx+3],1
    inc word [S_PRFNEW]
    pop bx
.r:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r0:
    ret

%ifndef RT_NOPRELOAD                    ; (a host that never runs Static:
                                        ;   the bare harness's pan modes)
; rt_preload - DS = the view, the cache just flushed (rt_init), the core not
; running: every profile entry translated as tr_miss would, heaviest first,
; its bank mapped in for it, until the cache is full; then the block table
; FIXED (S_JITFIX). Out: AX = the blocks translated. Every other register
; kept.
rt_preload:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor ax,ax
    push ax                         ; [bp+2]: the blocks translated
    push ax                         ; [bp]: the weight this pass
    mov bp,sp
    mov cx,[S_PRFSEG]
    jcxz .fix
    mov es,cx
    mov cx,[S_PRFMASK]              ; the heaviest weight
    inc cx
    xor bx,bx
    xor dl,dl
.mx:
    cmp dl,[es:bx+3]
    jae .mn
    mov dl,[es:bx+3]
.mn:
    add bx,4
    loop .mx
    or dl,dl
    jz .fix
    mov [bp],dl
    mov byte [S_JITPRE],1
.lvl:
    xor bx,bx                       ; BX = the entry, x 4
.e:
    mov es,[S_PRFSEG]
    mov al,[es:bx+3]
    cmp al,[bp]
    jne .ne
    push bx
    push bp                         ; (rt_translate keeps neither)
    call rt_preone
    pop bp
    pop bx
    cmp byte [S_JITPRE],2           ; the cache is full
    je .done
    cmp word [S_JITOUT],0           ; skipped, or given up
    je .ne
    inc word [bp+2]
.ne:
    add bx,4
    mov ax,[S_PRFMASK]
    inc ax
    shl ax,1
    shl ax,1
    cmp bx,ax
    jb .e
    dec byte [bp]
    jnz .lvl
.done:
    mov byte [S_JITPRE],0
.fix:
    mov byte [S_JITFIX],1
    pop ax
    pop ax                          ; AX = the blocks
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; rt_preone - the profile entry at ES:BX translated: [S_JITOUT] = its block,
; or 0 when it was skipped (its bank is not in memory, or its slot and the
; slot's neighbour hold blocks)
rt_preone:
    mov word [S_JITOUT],0
    mov si,[es:bx]
    mov dl,[es:bx+2]
    cmp si,8000h
    jae .no
    mov ax,[S_BANK]                 ; the bank as it is, put back after
    push ax
    push word [S_ROMX]
    cmp dl,0FFh
    je .slot
    mov bl,dl
    xor bh,bh
    shl bx,1
    mov ax,[RDG_BTAB+bx]
    or ax,ax
    jz .nob                         ; not in memory: the bank cache's
    mov [S_ROMX],ax
    mov [S_BANK],dl
.slot:
    mov bx,si                       ; tr_ekey's slot for SI and DL
    mov dh,dl
    xor bl,dl
    mov al,bh
    shr al,1
    shr al,1
    shr al,1
    xor bl,al
    and bh,[S_HMASK]
    shl bx,1
    shl bx,1
    shl bx,1
    mov es,[S_JITHSEG]
    cmp word [es:JIT_HASH+bx+4],0
    je .take
    xor bx,8                        ; ...or its neighbour's
    cmp word [es:JIT_HASH+bx+4],0
    jne .nob
.take:
    mov [S_JITBK],dl
    mov [S_JITSLOT],bx
    call rt_translate
.nob:
    pop word [S_ROMX]
    pop word [S_BANK]
.no:
    ret
%endif

; rdg_jit - tr_enter's miss, SS:SP the game's stack: translate the block at SI
; on the host's stack and go back into the core at tr_back
rdg_jit:
    RDG_TOHOST
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    push es
    cmp byte [S_JITFIX],0           ; a FIXED table (16.8) translates
    je .tr                          ;   HRAM's code, which a preload could
    cmp si,8000h                    ;   not (its bytes are the game's to
    jb .fixed                       ;   put there), and nothing else
.tr:
    call rt_translate
.out:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    jmp rdg_ret
.fixed:                             ; a ROM PC counted to RDG_JITHOT (only
    mov word [S_JITOUT],0           ;   an entry from before the table was
    mov es,[S_JITHSEG]              ;   fixed): interpreted, counting again
    mov bx,[S_JITSLOT]
    mov byte [es:JIT_HASH+bx+3],1
    jmp short .out

; -----------------------------------------------------------------------------
; rt_translate - SI = the PC, [S_JITBK] its bank key, DS = the view. Out:
; [S_JITOUT] = the block's offset in the core. ES:DI writes the code.
; -----------------------------------------------------------------------------
rt_translate:
    mov [RT_PC0],si
.restart:
    cmp byte [S_JITPRE],2           ; a preload's cache is full (16.8):
    jne .go                         ;   this block is given up, not entered
    mov word [S_JITOUT],0
    ret
.go:
    mov si,[RT_PC0]
    mov ax,[S_BANK]
    mov [RT_BANKW],ax
    ; the Game Boy's code, up to the end of its region, into JIT_BUF
    mov cx,4000h
    mov bx,[S_VIEW]
    cmp si,4000h
    jb .reg
    mov cx,0FFFFh                   ; FF80-FFFE, HRAM (16.3)
    cmp si,8000h
    jae .reg
    mov cx,8000h
    mov bx,[S_ROMX]
.reg:
    sub cx,si
    cmp cx,80h
    jbe .n
    mov cx,80h
.n:
    cmp si,8000h                    ; HRAM: 16 bytes at most, for its
    jb .n2                          ;   guard (rt_guard)
    cmp cx,RT_HMAX
    jbe .n2
    mov cx,RT_HMAX
.n2:
    mov [RT_N],cx
    push ds
    pop es
    mov di,JIT_BUF
    push ds
    mov ds,bx
    rep movsb
    pop ds
    mov es,[S_JITSEG]               ; ES = the cache
    mov di,[S_JITPTR]
    mov cx,RT_FALL + 2
    call rt_room
    jc .restart
    mov [RT_START],di
    mov word [RT_IX],0
    mov word [RT_CNT],0
    mov word [RT_PEND],0
    mov byte [RT_GLEN],0
.next:
    mov bx,[RT_IX]
    cmp word [RT_CNT],RT_MAXI
    jae .fall
    lea ax,[bx+3]
    cmp ax,[RT_N]
    ja .fall
    mov al,[JIT_BUF+bx]
    mov [RT_OP],al
    mov ax,[JIT_BUF+bx+1]
    mov [RT_IMM],ax
    call rt_pick                    ; a constant address: one MOV
    jnc .tm
    mov al,[RT_OP]
    xor ah,ah
    cmp al,0CBh
    jne .nf
    mov al,[JIT_BUF+bx+1]
    inc ah
    jmp short .tm
.nf:
    call rt_deadf                   ; INC/DEC r, ADD HL,rr: without F when
.tm:                                ;   the next instruction overwrites it
    shl ax,1
    xchg ax,si
    mov si,[cs:rt_tab + si]
    or si,si
    jz .ctl
    mov [RT_REC],si
    call rt_tmpl
    jc .restart
    inc word [RT_CNT]
    jmp .next
.ctl:
    mov cx,RT_CTL + 4
    call rt_room
    jc .restart
    call rt_glen
    mov ax,[RT_IX]                  ; (the pending M-cycles go into each
    add ax,[RT_PC0]                 ;   arm's own sub bp: rt_subbp)
    call rt_ctl                     ; AX = the instruction's PC
    mov word [RT_PEND],0
    jmp short .done
.fall:
    mov cx,RT_FALL + 4
    call rt_room
    jc .restart
    mov ax,[RT_IX]
    add ax,[RT_PC0]
    cmp word [RT_PEND],0            ; on to the next PC, checking BP: the
    je .fo                          ;   flush's sub bp sets the sign itself
    call rt_flushbp
    call rt_exit
    jmp short .done
.fo:
    call rt_exit_or
.done:
    mov ax,[RT_START]
    mov [RT_ENT],ax
    cmp word [RT_PC0],8000h         ; HRAM: entered through its guard
    jb .dn
    call rt_guard
    jnc .dn
    jmp .restart
.dn:
    mov [S_JITPTR],di
    mov ax,di                       ; (stat: the block's bytes, / 16 - a
    sub ax,[RT_START]               ;   range move inside it reads high)
    mov cl,4
    shr ax,cl
    add [S_JITBY],ax
    ; the table: this PC and bank key -> the block, in the slot it counted
    ; in (tr_miss: tr_enter's own, or its neighbour)
    mov si,[RT_PC0]
    mov dl,[S_JITBK]
    mov bx,[S_JITSLOT]
    mov es,[S_JITHSEG]
    mov [es:JIT_HASH+bx],si
    mov [es:JIT_HASH+bx+2],dl
    mov ax,[RT_ENT]
    mov [es:JIT_HASH+bx+4],ax
    mov [S_JITOUT],ax
    mov ax,[S_JITSEG]               ; ...in the segment it was written in
    mov [es:JIT_HASH+bx+6],ax
    mov [S_JITOUT+2],ax
    inc word [S_JITNB]
    cmp word [RT_PC0],8000h
    jae .hr
    inc word [S_JITNBR]
.hr:
    call rt_prfadd                  ; and into the profile (16.8)
%ifdef RT_DEBUG
    mov bx,[S_JITNB]                ; the last 8 PCs translated, and keys
    and bx,7
    shl bx,1
    shl bx,1
    mov [JIT_BUF+0C0h+bx],si
    mov al,[S_JITBK]
    mov [JIT_BUF+0C2h+bx],al
%endif
    ret

; rt_deadf - AX = the main opcode at JIT_BUF+BX, whose own record it is: its
; record WITHOUT F (rdggen.nof_lines) when it has one and the instructions after
; it in this block overwrite every flag it writes before any of them reads
; one (REALDMG-SPEC 16.1.2); else AX as it was. The look stops, and calls F
; live, at what could hand F on before it is overwritten: a store (whose
; guard may take an interrupt), a template that may leave the block, an
; instruction with no template, the block's end, RT_DLOOK instructions on.
; Keeps BX
RT_DLOOK        equ 6
rt_deadf:
    push ax
    call rt_nofx                    ; AX = its second record, if it has one
    jc .r0
    mov dx,ax
    pop ax
    push ax
    mov si,ax
    mov ch,[cs:rt_tab + RDGT_FLG + si]
    and ch,0Fh
    push dx                         ; [SP] = the second record
    mov dh,ch                       ; DH = the flags still live
    push bx
    mov cx,[RT_CNT]
    shl si,1
    mov si,[cs:rt_tab + si]         ; (its own record's length)
    mov al,[cs:rt_tab + si + 3]
    cbw
    add bx,ax                       ; BX = the next instruction
.k:
    inc cx                          ; CX = the count with this one
    cmp cx,RT_MAXI
    jae .live
    mov ax,[RT_CNT]
    add ax,RT_DLOOK
    cmp cx,ax
    ja .live
    lea si,[bx+3]
    cmp si,[RT_N]
    ja .live
    push cx
    mov al,[JIT_BUF+bx]
    xor ah,ah
    mov si,ax
    mov ch,[cs:rt_tab + RDGT_FLG + si]
    cmp al,0CBh
    jne .m
    mov al,[JIT_BUF+bx+1]           ; a CB op: its record 256 on, and its
    inc ah                          ;   masks the group's rule - RL and RR
    mov si,ax                       ;   read C, the shifts and rotates
    mov ch,0                        ;   write all four, BIT Z, N and H
    cmp al,80h
    jae .m
    mov ch,7
    cmp al,40h
    jae .m
    mov ch,0Fh
    cmp al,10h
    jb .m
    cmp al,20h
    jae .m
    mov ch,8Fh
.m:
    mov cl,ch
    shr cl,1
    shr cl,1
    shr cl,1
    shr cl,1
    and cl,dh                       ; it reads one: live
%ifndef RT_DEADBUG                  ; (tests/gbcpu.py's control: what reads
    jnz .livec                      ;   a flag, ADC or RLA, not seen to)
%endif
    and ch,0Fh
    not ch
    and dh,ch                       ; ...or writes the rest: dead
    jz .dead
    shl si,1
    mov si,[cs:rt_tab + si]         ; its own record: none, a store, or a
    or si,si                        ;   way out before its end - live
    jz .livec
    test byte [cs:rt_tab + si + 4],1+2+4+16+64
    jnz .livec
    mov al,[cs:rt_tab + si + 3]
    cbw
    add bx,ax                       ; BX = the one after it
    pop cx
    jmp .k
.dead:
    pop cx
    pop bx
    pop ax                          ; (the second record)
    pop dx
    ret
.livec:
    pop cx
.live:
    pop bx
    pop dx
.r0:
    pop ax
    ret

; rt_nofx - AX = a main opcode: CF = 0 and AX = the record of it without F
; (rdggen.NOF_OPS' slots), or CF = 1 when it has none
rt_nofx:
    cmp al,40h
    jae .no
    mov ah,al
    and ah,0Fh
    cmp ah,9
    je .hl
    and ah,7
    cmp ah,4
    je .id
    cmp ah,5
    jne .no
.id:
    mov ah,al                       ; INC/DEC r: (op >> 3) x 2 + (op AND 1)
    and ah,1
    shr al,1
    shr al,1
    and al,0FEh
    or al,ah
    jmp short .h
.hl:
    shr al,1                        ; ADD HL,rr: 16 + op >> 4
    shr al,1
    shr al,1
    shr al,1
    add al,16
.h:
    xor ah,ah
    add ax,RDGT_XNOF
    push si
    mov si,ax
    shl si,1
    cmp word [cs:rt_tab + si],1     ; ((HL)'s slots have no record)
    pop si
    ret
.no:
    stc
    ret

; rt_glen - [RT_GLEN] = the bytes of the control transfer at [RT_OP]: JR and
; JR cc 2, JP, JP cc, CALL and CALL cc 3, the rest 1
rt_glen:
    mov al,[RT_OP]
    mov cl,2
    cmp al,40h
    jb .g
    mov cl,1
    cmp al,0C0h
    jb .g
    mov ah,al
    and ah,7
    cmp ah,2
    je .g3
    cmp ah,4
    je .g3
    cmp al,0C3h
    je .g3
    cmp al,0CDh
    jne .g
.g3:
    mov cl,3
.g:
    mov [RT_GLEN],cl
    ret

; rt_guard - an HRAM block's entry (REALDMG-SPEC 16.3), at ES:DI after its
; code: its Game Boy bytes compared with HRAM as it is now, a word at a time
; (a byte for the last of an odd count), then a jump to the code; any that
; differs goes to tr_stale, which forgets the block. [RT_ENT] = the entry.
; CF = 1: flushed, the block starts again.
rt_guard:
    mov cx,[RT_IX]
    add cl,[RT_GLEN]
    adc ch,0
    push cx
    shr cx,1
    inc cx
    mov ax,9
    mul cx                          ; (DX: nothing of ours)
    add ax,12
    mov cx,ax
    call rt_room
    pop cx                          ; CX = the bytes
    jc .r
    mov dx,di                       ; the stale way: mov si,PC / jmp tr_stale
    mov ax,[RT_PC0]
    call rt_movsi
    mov ax,RDGC_TR_STALE
    call rt_jmp
    mov [RT_ENT],di
    xor bx,bx
.w:
    push cx
    sub cx,bx
    jbe .j
    mov ax,8036h                    ; cmp byte/word [ss:PC + n],the bytes
    cmp cx,1
    je .b
    mov ah,81h
.b:
    stosw
    mov al,3Eh
    stosb
    mov ax,[RT_PC0]
    add ax,bx
    stosw
    mov ax,[JIT_BUF+bx]
    cmp cx,1
    je .b1
    stosw
    inc bx
    inc bx
    jmp short .jn
.b1:
    stosb
    inc bx
.jn:
    mov al,75h                      ; jne the stale way
    stosb
    mov ax,dx
    sub ax,di
    dec ax
    stosb
    pop cx
    jmp short .w
.j:
    pop cx
    mov ax,[RT_START]               ; the bytes are the block's: its code
    call rt_jmp
    clc
.r:
    ret

; rt_pick - the instruction at [RT_OP]/[RT_IMM] by its operand: CF = 0 and
; AX = a chosen-by-operand record (rdggen.EXTRA: 512 + k), [RT_IMM] the
; whole address, when it is LDH or LD (nn) on an address the interpreter's
; own fast path takes directly - or [RT_IMM] the instruction's own PC when
; it is a read-modify-write of (HL) (rdggen.rmw_lines); CF = 1 otherwise.
; Keeps BX
rt_pick:
    mov al,[RT_OP]
    mov dx,[RT_IMM]
    cmp al,0F0h                     ; LDH A,(n): n >= 80h
    jne .e0
    cmp dl,80h
    jb .no
    mov ax,512
    jmp short .ff
.e0:
    cmp al,0E0h                     ; LDH (n),A: 80h <= n < FFh, or a
    jne .fa                         ;   sound register io_wr only stores
    cmp dl,80h                      ;   (rt_snd)
    jae .e1
    call rt_snd
    jc .no
    jmp short .e2
.e1:
    cmp dl,0FFh
    je .no
.e2:
    mov ax,513
.ff:
    mov dh,0FFh
    mov [RT_IMM],dx
    clc
    ret
.fa:
    cmp al,0FAh                     ; LD A,(nn): bank 0, 8000-FEFF, FF80+
    jne .ea
    cmp dh,40h
    jb .rd
    cmp dh,80h
    jb .no
    cmp dh,0FFh
    jne .rd
    cmp dl,80h
    jb .no
.rd:
    mov ax,514
    clc
    ret
.ea:
    cmp al,0EAh                     ; LD (nn),A: A000-FEFF, FF80-FFFE
    jne .rmw                        ;   (VRAM's go through vr_wr, which
    cmp dh,0A0h                     ;   marks them for the damage scan)
    jb .bk
    cmp dh,0FFh
    jne .wr
    cmp dl,80h
    jae .e3
    call rt_snd                     ; (or a sound register's, as LDH's)
    jc .no
    jmp short .wr
.e3:
    cmp dl,0FFh
    je .no
.wr:
    mov ax,515
    clc
    ret
.bk:
    cmp dh,20h                      ; ...or 2000-3FFF of an MBC1/3/5: the
    jb .no                          ;   bank select, RDGT_XBANK + 0/1/2
    cmp dh,40h
    jae .no
    mov ah,[S_MBC]
    mov al,0
    cmp ah,RDGM_MBC1
    je .bks
    inc ax
    cmp ah,RDGM_MBC3
    je .bks
    inc ax
    cmp ah,RDGM_MBC5
    jne .no
    cmp dh,30h                      ; (3000-3FFF is its bank's bit 8)
    jae .no
.bks:
    cbw
    add ax,RDGT_XBANK
    clc
    ret
.rmw:
    cmp al,34h                      ; INC (HL), DEC (HL): 516, 517
    je .incdec
    cmp al,35h
    je .incdec
    cmp al,0CBh                     ; CB on (HL) but BIT: 518 + k
    jne .no
    mov al,dl
    mov ah,al
    and ah,7
    cmp ah,6
    jne .no
    cmp al,40h
    jb .cb
    cmp al,80h
    jb .no                          ; (BIT has its own template)
    sub al,40h
.cb:
    shr al,1
    shr al,1
    shr al,1
    xor ah,ah
    add ax,518
    jmp short .pc
.incdec:
    xor ah,ah
    add ax,516 - 34h
.pc:
    mov dx,[RT_IX]
    add dx,[RT_PC0]
    mov [RT_IMM],dx
    clc
    ret
.no:
    stc
    ret

; rt_snd - DL = an I/O register's low byte: CF = 0 when io_wr does nothing
; with a write there but store it - the sound registers FF10-FF3F but the
; five rdgsys keeps for the speaker (REALDMG-SPEC section 17): the
; translated store is then one MOV, where the template's took the slow
; path and io_wr's compares (~140 clocks on a 286), every frame the game's
; sound driver writes them
rt_snd:
    cmp dl,10h
    jb .no
    cmp dl,40h
    jae .no
    cmp dl,12h
    je .no
    cmp dl,14h
    je .no
    cmp dl,17h
    je .no
    cmp dl,19h
    je .no
    cmp dl,26h
    je .no
    cmp dl,1Ah                      ; (the wave and noise channels' DAC
    je .no                          ;   and trigger registers: rdgsys's too,
    cmp dl,1Eh                      ;   REALDMG-SPEC 17.1)
    je .no
    cmp dl,21h
    je .no
    cmp dl,23h
    je .no
    clc
    ret
.no:
    stc
    ret

; rt_room - CX = the bytes about to be written at ES:DI. On to the next gap
; when this one has not got them, with a jump from here to there; with no gap
; left, on into the next TIER (REALDMG-SPEC 16.2.2) - the first cache
; segment, the core's holes, the second cache segment if there is one - with
; CF = 1: the caller starts its block again, there; and with none of that,
; everything is flushed and CF = 1: the caller starts its block again.
rt_room:
    mov ax,di
    add ax,cx
    jc .adv
    cmp ax,[S_JITEND]
    jbe .ok
.adv:
    push bx
    push dx
    call rt_gnext                   ; AX, DX = the next gap's start and end
    jc .last
    inc word [S_JITADV]
    push ax                         ; jmp near there
    mov byte [es:di],0E9h
    sub ax,di
    sub ax,3
    mov [es:di+1],ax
    pop di
    mov ax,dx
    call rt_gapend
    pop dx
    pop bx
    jmp rt_room
.last:
    pop dx
    pop bx
    push cx                         ; THE NEXT TIER (16.2.2, 16.2.3), in
    mov cl,[S_JITTIER]              ;   turn: 0, the core's holes, 2, 0...
.nt:                                ;   over those there are
    inc cl
    cmp cl,3
    jb .t
    xor cl,cl
.t:
    mov al,cl
    call rt_tseg                    ; AX = its segment, 0: none
    or ax,ax
    jz .nt                          ; (tier 0 always has one)
    mov ch,1
    shl ch,cl
    test [S_JITUSED],ch
    jz .fresh
    cmp cl,[S_JITTIER]              ; written before: the OLDEST, so it
    je .flush                       ;   goes - but a preload or a fixed
    mov dl,[S_JITPRE]               ;   table stops at a full cache (16.8),
    or dl,[S_JITFIX]                ;   and one tier alone is a flush
    jnz .full0
    cmp word [S_JITGNX],0FFF0h      ; (the generations near a wrap: a flush)
    jae .flush
    call rt_unlink                  ; its links undone, its blocks out of
    call rt_forget                  ;   the table
    inc word [S_JITRCY]
    inc word [S_JITFL]
.fresh:
    or [S_JITUSED],ch
    mov [S_JITTIER],cl
    pop cx
    inc word [S_JITSW]
    call rt_tier
    stc
    ret
.flush:
    pop cx
    jmp short .fl
.full0:
    pop cx
.full:
    mov al,[S_JITPRE]               ; a preload, or a fixed table's HRAM,
    or al,[S_JITFIX]                ;   stops at a full cache rather than
    jz .fl                          ;   forget what was made (16.8)
    mov byte [S_JITPRE],2
    stc
    ret
.fl:
    call rt_flush
    stc
    ret
.ok:
    clc
    ret

; -----------------------------------------------------------------------------
; rt_tmpl - one templated instruction at [RT_IX], record [RT_REC]: its guards,
; the template, its operands and its fixups. CF = 1: flushed, start again.
; -----------------------------------------------------------------------------
rt_tmpl:
    mov si,[RT_REC]
    xor ah,ah
    mov al,[cs:rt_tab + si + 3]     ; the instruction's length
    add ax,[RT_IX]
    mov [RT_IX],ax
    add ax,[RT_PC0]
    mov [RT_NEXT],ax
    ; the guards (REALDMG-SPEC section 16.5)
    mov byte [RT_GUARD],0
    mov al,[cs:rt_tab + si + 4]
    test al,1
    jnz .sg
    test al,2
    jz .c16
    mov ah,[RT_IMM]                 ; LDH (n),A: IF or IE?
    cmp ah,0Fh
    je .sg
    cmp ah,0FFh
    je .sg
    jmp short .bk
.c16:
    test al,4
    jz .bk
    cmp word [RT_IMM],0FF0Eh        ; LD (nn),A / LD (nn),SP into I/O, HRAM
    jae .sg
    jmp short .bk
.sg:
    or byte [RT_GUARD],1
.bk:
    cmp word [RT_PC0],4000h         ; running from the bank: a store that
    jb .room                        ;   could reach the MBC is checked after
    cmp word [RT_PC0],8000h         ;   (HRAM: no bank to change under it)
    jae .room
    test al,1
    jnz .bg
    test al,4
    jz .room
    cmp word [RT_IMM],8000h
    jae .room
.bg:
    or byte [RT_GUARD],2
.room:
    mov cl,[cs:rt_tab + si + 2]
    xor ch,ch
    add cx,4                        ; (rt_flushbp's, at most)
    test byte [RT_GUARD],1          ; and exactly the guards' bytes
    jz .r2
    add cx,12
.r2:
    test byte [RT_GUARD],2
    jz .r3
    add cx,20
.r3:
    call rt_room
    jnc .go
    ret
.go:
    mov si,[RT_REC]                 ; PURE: its last three bytes, sub bp,n,
    test byte [cs:rt_tab + si + 4],8    ;   become n more pending - the time
    jz .imp                         ;   is read by none of it, and is taken
    mov bl,[cs:rt_tab + si + 2]     ;   off BP before anything that does
    xor bh,bh
    mov al,[cs:rt_tab + si + 9 + bx - 1]
    xor ah,ah
    add [RT_PEND],ax
    mov cx,bx
    sub cx,3
    jmp short .cp
.imp:
    call rt_flushbp
    test byte [RT_GUARD],1
    jz .copy
    mov al,0BEh                     ; mov si,NEXT
    stosb
    mov ax,[RT_NEXT]
    stosw
.copy:
    mov si,[RT_REC]
    mov cl,[cs:rt_tab + si + 2]
    xor ch,ch
    test byte [cs:rt_tab + si + 4],32   ; DEFERRABLE (rdgtmpl): BP exact at
    jz .cp                          ;   its start is all it needs, so its
    cmp byte [RT_GUARD],0           ;   own sub bp,n joins the pending -
    jne .cp                         ;   unless a guard's exit follows it
    push si                         ;   (an exit leaves with BP charged)
    mov bx,cx
    mov al,[cs:rt_tab + si + 9 + bx - 1]
    xor ah,ah
    add [RT_PEND],ax
    pop si
    sub cx,3
.cp:
    push di                         ; the template's place
    push si
    push ds
    push cs
    pop ds
    lea si,[rt_tab + si + 9]
    rep movsb
    pop ds
    pop si
    pop bx                          ; BX = where the template went
    ; the fixups
    mov dx,bx
    sub dx,[cs:rt_tab + si]         ; DX = its place - its origin
    mov cl,[cs:rt_tab + si + 2]
    xor ch,ch
    lea bp,[si + 9]
    add bp,cx                       ; BP = the position lists (in rt_tab)
    mov cl,[cs:rt_tab + si + 5]     ; IMM8
    jcxz .i16
.p8:
    mov al,[cs:rt_tab + bp]
    inc bp
    xor ah,ah
    xchg ax,si
    mov al,[RT_IMM]
    mov [es:bx+si],al
    xchg ax,si
    mov si,[RT_REC]
    loop .p8
.i16:
    mov cl,[cs:rt_tab + si + 6]
    jcxz .ab
    mov ax,[RT_IMM]                 ; IMM16: the operand, or NEXT for a store
    test byte [cs:rt_tab + si + 4],16   ;   that guards itself (rdggen
    jz .v16                         ;   self_guard)
    mov ax,[RT_NEXT]
.v16:
    mov [RT_V16],ax
.p16:
    mov al,[cs:rt_tab + bp]
    inc bp
    xor ah,ah
    xchg ax,si
    mov ax,[RT_V16]
    mov [es:bx+si],ax
    mov si,[RT_REC]
    loop .p16
.ab:
    mov cl,[cs:rt_tab + si + 7]
    jcxz .rl
.pab:
    mov al,[cs:rt_tab + bp]
    inc bp
    xor ah,ah
    xchg ax,si
    add [es:bx+si],dx
    mov si,[RT_REC]
    loop .pab
.rl:
    mov cl,[cs:rt_tab + si + 8]
    jcxz .post
.prl:
    mov al,[cs:rt_tab + bp]
    inc bp
    xor ah,ah
    xchg ax,si
    sub [es:bx+si],dx
    mov si,[RT_REC]
    loop .prl
.post:
    test byte [RT_GUARD],1
    jz .bank
    mov ax,0FE81h                   ; cmp si,NEXT / je +3 / jmp tr_enter:
    stosw                           ;   an interrupt the store took moved SI
    mov ax,[RT_NEXT]
    stosw
    mov ax,0374h
    stosw
    mov ax,RDGC_TR_ENTER
    call rt_jmp
.bank:
    test byte [RT_GUARD],2
    jz .r
    mov ax,8136h                    ; cmp word [ss:S_BANK],BANKW
    stosw
    mov al,3Eh
    stosb
    mov ax,S_BANK
    stosw
    mov ax,[RT_BANKW]
    stosw
    mov ax,0674h                    ; je +6
    stosw
    mov al,0BEh                     ; mov si,NEXT / jmp tr_enter
    stosb
    mov ax,[RT_NEXT]
    stosw
    mov ax,RDGC_TR_ENTER
    call rt_jmp
.r:
    clc
    ret

; rt_flushbp - the pending M-cycles taken off BP at ES:DI: sub bp,n, and
; none pending. Keeps every register.
rt_flushbp:
    push ax
    mov ax,[RT_PEND]
    or ax,ax
    jz .r
    mov word [RT_PEND],0
    cmp ax,127
    ja .w
    push ax
    mov ax,0ED83h                   ; sub bp,imm8
    stosw
    pop ax
    stosb
    jmp short .r
.w:
    push ax
    mov ax,0ED81h                   ; sub bp,imm16
    stosw
    pop ax
    stosw
.r:
    pop ax
    ret

; rt_jmp - jmp near AX (a core offset) at ES:DI
rt_jmp:
    push ax
    mov al,0E9h
    stosb
    pop ax
    sub ax,di
    sub ax,2
    stosw
    ret

; rt_exit_or - on to the Game Boy address AX, testing BP's sign here
rt_exit_or:
    push ax
    mov ax,0ED0Bh                   ; or bp,bp
    stosw
    pop ax
; rt_exit - on to the Game Boy address AX, the flags BP's (a SUB BP just
; before): SI = AX, the event entry when BP went negative, and otherwise the
; block there - directly when it is translated already and its bank cannot
; differ, through tr_link (which patches this exit into that jump) when it
; is not translated yet, and through tr_enter when it is not chainable.
; DS is not loaded: translated code does not keep it (tr_int does).
rt_exit:
    push bx
    push dx
    push ax
    mov bx,ax                       ; bank 0: chained, now if the block
    mov dl,0FFh                     ;   is there already
    cmp ah,40h
    jb .key
    cmp ah,80h
    jae .ram                        ; RAM: never translated
    mov ax,1378h                    ; 4000-7FFF: js +19 / THIRTEEN bytes,
    stosw                           ;   mov dx,SITE / jmp tr_lsite and seven
    mov al,0BAh                     ;   spare, which tr_link makes a bank
    stosb                           ;   check and a jmp near or far (16.4,
    mov ax,di                       ;   16.2.1) / then mov si,T / jmp
    dec ax                          ;   tr_enter, for when the bank is
    stosw                           ;   another
    mov ax,RDGC_TR_LSITE
    call rt_jmp
    mov ax,9090h
    stosw
    stosw
    stosw
    stosb
    pop ax
    push ax
    call rt_movsi
    mov ax,RDGC_TR_ENTER
    call rt_jmp
    jmp short .ev
.key:
    mov dh,dl                       ; DH = its key; the table's slot
    xor bl,dl
    mov dl,bh
    shr dl,1
    shr dl,1
    shr dl,1
    xor bl,dl
    and bh,[S_HMASK]
    shl bx,1
    shl bx,1
    shl bx,1
    push es
    mov es,[S_JITHSEG]
    cmp [es:JIT_HASH+bx],ax
    jne .nob
    cmp [es:JIT_HASH+bx+2],dh
    jne .nob
    mov dx,[es:JIT_HASH+bx+6]
    mov bx,[es:JIT_HASH+bx+4]
    pop es
    or bx,bx
    jz .link
    cmp dx,[S_JITSEG]               ; the other segment's: tr_link makes it
    jne .link                       ;   a far jump (16.2.1)
    mov ax,0378h                    ; js +3 / jmp near the block
    stosw
    mov ax,bx
    call rt_jmp
    jmp short .ev
.nob:
    pop es
.link:
    mov ax,0678h                    ; js +6 / mov dx,SITE / jmp tr_lsite
    stosw                           ;   (which takes T from the mov si,T
    mov al,0BAh                     ;   after it: SITE + 7)
    stosb
    mov ax,di
    dec ax
    stosw
    mov ax,RDGC_TR_LSITE
    call rt_jmp
    jmp short .ev
.ram:
    call rt_movsi                   ; mov si,T / js +3 / jmp tr_enter
    mov ax,0378h
    stosw
    mov ax,RDGC_TR_ENTER
    call rt_jmp
    jmp short .ev2
.ev:                                ; the event's way: SI = T only here -
    pop ax                          ;   a block takes no SI, and the ways
    push ax                         ;   on that need one set it themselves
    call rt_movsi
.ev2:
    mov ax,0FF36h                   ; jmp far [ss:S_EVVEC]
    stosw
    mov al,2Eh
    stosb
    mov ax,S_EVVEC
    stosw
    pop ax
    pop dx
    pop bx
    ret

; rt_movsi - mov si,AX at ES:DI
rt_movsi:
    push ax
    mov al,0BEh
    stosb
    pop ax
    stosw
    ret

; rt_subbp - sub bp,AL
rt_subbp:                           ; ...and the pending M-cycles, taken
    xor ah,ah                       ;   (rt_ctl's arms: one sub each, where
    add ax,[RT_PEND]                ;   a flush before the control transfer
    mov word [RT_PEND],0            ;   made two)
    cmp ax,127
    ja .w
    mov ah,al
    mov al,83h
    stosb
    mov al,0EDh
    stosb
    mov al,ah
    stosb
    ret
.w:
    push ax
    mov ax,0ED81h                   ; sub bp,imm16
    stosw
    pop ax
    stosw
    ret

; rt_pend0 - the second arm of a conditional transfer: its pending M-cycles
; back (the first arm's sub took them). Every register kept.
rt_pend0:
    push ax
    mov ax,[RT_P0]
    mov [RT_PEND],ax
    pop ax
    ret

; rt_skip - Jcc short over what follows: AL = the opcode. Out: BX = the
; displacement byte's place, for rt_land
rt_skip:
    stosb
    mov bx,di
    xor al,al
    stosb
    ret
rt_land:
    mov ax,di
    sub ax,bx
    dec ax
    mov [es:bx],al
    ret

; -----------------------------------------------------------------------------
; rt_ctl - the control transfer, or the opcode with no template, at Game Boy
; address AX: [RT_OP], [RT_IMM]. Ends the block.
; -----------------------------------------------------------------------------
RT_SKIP_NZ      equ 74h             ; after SAHF, the Jcc that SKIPS a taken
RT_SKIP_Z       equ 75h             ;   NZ / Z / NC / C transfer
RT_SKIP_NC      equ 72h
RT_SKIP_C       equ 73h
rt_skips:       db RT_SKIP_NZ, RT_SKIP_Z, RT_SKIP_NC, RT_SKIP_C

rt_ctl:
    push ax
    mov ax,[RT_PEND]
    mov [RT_P0],ax
    pop ax
    mov si,ax                       ; SI = the instruction's PC
    mov bl,[RT_OP]
    cmp bl,18h
    je .jr
    mov bh,bl
    and bh,0E7h
    cmp bh,20h
    je .jrcc
    cmp bl,0C3h
    je .jp
    cmp bh,0C2h
    je .jpcc
    cmp bl,0CDh
    je .call
    cmp bh,0C4h
    je .callcc
    cmp bl,0C9h
    je .ret
    cmp bh,0C0h
    je .retcc
    mov bh,bl
    and bh,0C7h
    cmp bh,0C7h
    je .rst
    cmp bl,0E9h
    je .jphl
    ; no template and no jump: the interpreter takes it from its PC, in
    ; the core (RDG_DISPATCH, through S_DISP)
    call rt_flushbp
    mov al,0BEh                     ; mov si,PC / jmp tr_int
    stosb
    mov ax,si
    stosw
    mov ax,RDGC_TR_INT
    jmp rt_jmp
.cond:                              ; SAHF and the Jcc that skips: BX = its
    mov al,9Eh                      ;   displacement's place
    stosb
    mov bl,[RT_OP]
    mov cl,3
    shr bl,cl
    and bx,3
    mov al,[cs:rt_skips + bx]
    jmp rt_skip
.jr:
    lea ax,[si+2]
    call rt_jrt
    ret
.jrcc:
    call .cond
    push bx
    lea ax,[si+2]
    call rt_jrt
    pop bx
    call rt_land
    call rt_pend0                   ; (the other arm's pending too)
    mov al,2
    call rt_subbp
    lea ax,[si+2]
    jmp rt_exit
.jp:
    mov al,4
    call rt_subbp
    mov ax,[RT_IMM]
    jmp rt_exit
.jpcc:
    call .cond
    push bx
    mov al,4
    call rt_subbp
    mov ax,[RT_IMM]
    call rt_exit
    pop bx
    call rt_land
    call rt_pend0                   ; (the other arm's pending too)
    mov al,3
    call rt_subbp
    lea ax,[si+3]
    jmp rt_exit
.call:
    lea ax,[si+3]
    call rt_push
    mov al,6
    call rt_subbp
    mov ax,[RT_IMM]
    jmp rt_exit
.callcc:
    call .cond
    push bx
    lea ax,[si+3]
    call rt_push
    mov al,6
    call rt_subbp
    mov ax,[RT_IMM]
    call rt_exit
    pop bx
    call rt_land
    call rt_pend0                   ; (the other arm's pending too)
    mov al,3
    call rt_subbp
    lea ax,[si+3]
    jmp rt_exit
.ret:
    mov al,4
    jmp rt_ret
.retcc:
    call .cond
    push bx
    mov al,5
    call rt_ret
    pop bx
    call rt_land
    call rt_pend0                   ; (the other arm's pending too)
    mov al,2
    call rt_subbp
    lea ax,[si+1]
    jmp rt_exit
.rst:
    lea ax,[si+1]
    call rt_push
    mov al,4
    call rt_subbp
    mov al,[RT_OP]
    and ax,38h
    jmp rt_exit
.jphl:
    mov ax,0DE89h                   ; mov si,bx
    stosw
    mov al,1
    jmp rt_retds

; rt_push - push the Game Boy address AX: mov dx,AX / push dx / xor dl,dl
rt_push:
    push ax
    mov al,0BAh
    stosb
    pop ax
    stosw
    mov al,52h
    stosb
    mov ax,0D230h
    stosw
    ret

; rt_ret - RET, its M-cycles in AL: pop si, the check, and an INLINE CACHE
; of the last two places this RET went (tr_link fills it, 16.4): twice cmp
; si,PC / jne / cmp byte [ss:S_BANK],B / jne / jmp near BLOCK and two spare
; bytes, or jmp far BLOCK in the other cache segment (16.2.1), else mov
; dx,SITE / jmp tr_link. The bank check is a jmp short over it when PC is
; in bank 0.
rt_ret:
    push ax
    mov al,5Eh                      ; pop si
    stosb
    pop ax
    call rt_subbp
    mov ax,2C78h                    ; js +44
    stosw
    push bx
    mov bx,di                       ; SITE
    call .entry                     ; the last return, then the one before
    call .entry                     ;   it (tr_rpatch moves it down)
    mov al,0BAh                     ; mov dx,SITE / jmp tr_link
    stosb
    mov ax,bx
    stosw
    pop bx
    mov ax,RDGC_TR_LINK
    call rt_jmp
    mov ax,0FF36h                   ; jmp far [ss:S_EVVEC]
    stosw
    mov al,2Eh
    stosb
    mov ax,S_EVVEC
    stosw
    ret
.entry:                             ; 19 bytes: cmp si,PC / jne +13 / cmp
    mov ax,0FE81h                   ;   byte [ss:S_BANK],B / jne +5 / jmp
    stosw                           ;   near BLOCK and two spare - PC FFFFh,
    mov ax,0FFFFh                   ;   which nothing returns to, and BLOCK
    stosw                           ;   the next instruction until tr_rpatch
    mov ax,0D75h                    ;   fills it
    stosw
    mov ax,8036h
    stosw
    mov al,3Eh
    stosb
    mov ax,S_BANK
    stosw
    xor al,al
    stosb
    mov ax,0575h
    stosw
    mov al,0E9h
    stosb
    xor ax,ax
    stosw
    mov ax,9090h
    stosw
    ret

; rt_retds - JP HL's end: SI is set, AL = its M-cycles; the check, tr_enter
rt_retds:
    call rt_subbp
    mov ax,0378h                    ; js +3 / jmp tr_enter / jmp far ev
    stosw
    mov ax,RDGC_TR_ENTER
    call rt_jmp
    mov ax,0FF36h
    stosw
    mov al,2Eh
    stosb
    mov ax,S_EVVEC
    stosw
    ret

; rt_jrt - a taken JR whose next PC is AX: its target from [RT_IMM]'s low
; byte, and a spin-wait to it skipped as the interpreter skips one
rt_jrt:
    mov dl,[RT_IMM]
    mov dh,dl
    sar dh,1                        ; sign-extend e (8086: no MOVSX)
    sar dh,1
    sar dh,1
    sar dh,1
    sar dh,1
    sar dh,1
    sar dh,1
    add ax,dx
    mov [RT_T],ax
    cmp dx,0FFFDh                   ; -3, a JR NZ, after DEC A/B/C: a
    jne .nocnt                      ;   countdown (cnt_ff, section 8.1) - or
                                    ;   after CP (HL), a wait on LY there
    cmp byte [RT_OP],20h
    jne .nocnt
    mov bx,[RT_IX]
    or bx,bx
    jz .nocnt
    mov dl,[JIT_BUF+bx-1]
    cmp dl,3Dh
    je .cnt
    cmp dl,05h
    je .cnt
    cmp dl,0Dh
    je .cnt
    cmp dl,0BEh                     ; CP (HL): cnt_ff sends DX = 00BEh, and
    jne .nocnt                      ;   only that, to cnt_cph, which looks
    xor dh,dh                       ;   at HL when it runs
.cnt:
    mov bx,RDGC_CNT_FF
.ff:                                ; BX = the tail, DX what it is told
    call rt_flushbp                 ; (it reads BP: charged first)
    mov al,0BEh                     ; mov si,T
    stosb
    mov ax,[RT_T]
    stosw
    mov ax,0C736h                   ; mov word [ss:S_RET],FALLBACK
    stosw
    mov al,06h
    stosb
    mov ax,S_RET
    stosw
    mov ax,di
    add ax,8                        ; past this word, mov dx and the jmp
    stosw
    mov al,0BAh                     ; mov dx,DX
    stosb
    mov ax,dx
    stosw
    xchg ax,bx
    call rt_jmp
    jmp .plain                      ; FALLBACK: the taken JR as usual
.nocnt:
    cmp dx,0FFFCh                   ; -4, a JR NZ after ADD HL,DE / DEC B
    jne .noadd                      ;   or C: a map row stepped through
    cmp byte [RT_OP],20h            ;   (add_ff, REALDMG-SPEC 16.6)
    jne .noadd
    mov bx,[RT_IX]
    cmp bx,2
    jb .noadd
    cmp byte [JIT_BUF+bx-2],19h
    jne .noadd
    mov dl,[JIT_BUF+bx-1]
    cmp dl,05h
    je .add
    cmp dl,0Dh
    je .add
    mov dl,0FCh                     ; (DX the displacement again)
    jmp short .noadd
.add:
    mov bx,RDGC_ADD_FF
    jmp .ff
.noadd:
    cmp dx,0FFFAh                   ; -6, a JR NZ after a copy's four (cpy_ff,
    jne .nocpy                      ;   REALDMG-SPEC 16.6): LD A,(HL+) /
    cmp byte [RT_OP],20h            ;   LD (DE),A or LD A,(DE) / LD (HL+),A,
    jne .nocpy                      ;   then INC DE / DEC B
    mov bx,[RT_IX]
    cmp bx,4
    jb .nocpy
    mov ax,[JIT_BUF+bx-4]           ; (what follows takes T from RT_T)
    cmp word [JIT_BUF+bx-2],0513h
    jne .stat
    cmp ax,122Ah
    je .cpy
    cmp ax,221Ah
    jne .nocpy
.cpy:
    mov dl,al
    mov bx,RDGC_CPY_FF
    jmp .ff
.stat:                              ; ...or a wait on the PPU (stat_ff):
    mov dx,[JIT_BUF+bx-2]           ;   LDH A,(41h) / AND 3 or BIT 1,A, or
    cmp ax,44F0h                    ;   LDH A,(44h) / CP n
    je .ly
    cmp ax,41F0h
    jne .nost
    cmp dx,03E6h
    mov dl,3                        ; DL = 3: mode 0; 2: mode 0 or 1
    je .stf
    cmp dx,4FCBh
    mov dl,2
    je .stf
.nost:
    mov dx,0FFFAh                   ; (DX the displacement again)
    jmp short .nocpy
.ly:
    cmp dl,0FEh
    jne .nost
    mov dl,44h                      ; DL = 44h: line DH
.stf:
    mov bx,RDGC_STAT_FF
    jmp .ff
.nocpy:
    cmp dx,0FFF0h                   ; -16..-4: a spin-wait's shape?
    jb .plain
    cmp dx,0FFFCh
    ja .plain
    call rt_idle
    cmp byte [RT_IDLE],0
    je .plain
    cmp byte [RT_IDLE],1
    je .yes
    ; HL's: only while HL is WRAM or HRAM (rdggen's idle_hl)
    call rt_flushbp                 ; (idle_yes reads BP: charged first,
    mov ax,0FF80h                   ;   for both ways out) - cmp bh,0C0h
    stosw
    mov al,0C0h
    stosb
    mov al,72h                      ; jb .no
    call rt_skip
    push bx
    mov ax,0FF80h                   ; cmp bh,0FFh
    stosw
    mov al,0FFh
    stosb
    mov ax,0575h                    ; jne .yes: past the cmp and jb below
    stosw
    mov ax,0FB80h                   ; cmp bl,80h
    stosw
    mov al,80h
    stosb
    mov al,72h                      ; jb .no
    call rt_skip
    push bx
    call .emit_yes
    pop bx
    call rt_land
    pop bx
    call rt_land
    jmp short .plain
.yes:
    call rt_flushbp
    call .emit_yes
    ret
.emit_yes:
    mov al,0BEh                     ; mov si,T / jmp idle_yes, which
    stosb                           ;   charges the JR's 3 M-cycles itself
    mov ax,[RT_T]
    stosw
    mov ax,RDGC_IDLE_YES
    jmp rt_jmp
.plain:
    mov al,3
    call rt_subbp
    mov ax,[RT_T]
    jmp rt_exit

; -----------------------------------------------------------------------------
; rt_idle - is the loop from [RT_T] to the JR at [RT_T]+len a spin-wait?
; REALDMG-SPEC section 8's rule, decided here once rather than at every pass.
; Out: [RT_IDLE] = 0 no, 1 yes, 2 yes while HL is WRAM or HRAM.
; -----------------------------------------------------------------------------
rt_idle:
    push si
    push di
    push es
    mov byte [RT_IDLE],0
    ; the loop's bytes, from wherever they are
    mov si,[RT_T]
    mov bx,[S_VIEW]
    cmp si,4000h
    jb .src
    cmp si,8000h
    jae .r                          ; RAM: never
    mov bx,[S_ROMX]
.src:
    push ds
    pop es
    mov di,RT_LOOP
    mov cx,16
    push ds
    mov ds,bx
    rep movsb
    pop ds
    ; L = the body's length: the JR is at [RT_T] + L
    mov ax,[RT_IX]
    add ax,[RT_PC0]
    sub ax,[RT_T]                   ; AX = L
    cmp ax,14
    ja .r
    mov dh,al                       ; DH = L
    mov dl,[RT_LOOP]
    cmp dl,0FAh
    je .fa
    cmp dl,0F0h
    je .f0
    cmp dl,7Eh
    je .hl1
    cmp dl,0CBh
    jne .r
    cmp dh,2                        ; BIT b,(HL) alone
    jne .r
    mov dl,[RT_LOOP+1]
    and dl,0C7h
    cmp dl,46h
    jne .r
    mov byte [RT_IDLE],2
    jmp short .r
.hl1:
    mov cl,1
    mov ch,2
    jmp short .walk
.f0:
    cmp byte [RT_LOOP+1],80h
    jb .r
    mov cl,2
    mov ch,1
    jmp short .walk
.fa:
    mov al,[RT_LOOP+2]
    cmp al,0C0h
    jb .r
    cmp al,0FFh
    jne .fa1
    cmp byte [RT_LOOP+1],80h
    jb .r
.fa1:
    mov cl,3
    mov ch,1
.walk:                              ; CL = the place, DH = L, CH = the answer
    cmp cl,dh
    jae .r                          ; a read and no test
.t:
    mov bl,cl
    xor bh,bh
    mov al,[RT_LOOP+bx]
    cmp al,0A7h
    je .one
    cmp al,0B7h
    je .one
    cmp al,0FEh
    je .two
    cmp al,0E6h
    je .two
    cmp al,0CBh
    jne .jrc
    mov al,[RT_LOOP+bx+1]
    and al,0C7h
    cmp al,47h
    je .two
    jmp short .r
.jrc:
    and al,0E7h
    cmp al,20h
    jne .r
    mov al,[RT_LOOP+bx+1]
    add al,cl
    add al,2
    jz .two
    js .r
    mov ah,dh
    add ah,2
    cmp al,ah
    jb .r
.two:
    inc cl
.one:
    inc cl
    cmp cl,dh
    jb .t
    jne .r
    mov [RT_IDLE],ch
.r:
    pop es
    pop di
    pop si
    ret
