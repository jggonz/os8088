; =============================================================================
; os8088 - apps/realdmg/rdgaot.inc
;
; A PRECOMPILED cartridge's run-time half (docs/REALDMG-SPEC.md section 20.9):
; what the core's table cannot hold is in a list the host sorted, and a miss
; looks there before it goes to the interpreter. Included by a host that runs
; precompiled code - tools/gbaot/aothost.asm, and the package's RDG_AOT arm -
; after rdgdefs.inc. Its one routine is far-jumped to by the core, so it may
; sit in any segment.
;
; The list: S_AOTLN entries of 8 bytes at S_AOTLSEG:0, the block table's own
; layout (PC, bank key, -, the block's offset and segment), sorted by bank
; key and then PC.
; =============================================================================

; The filter (8KB at S_AOTFSEG:0) and, after it, the cache of list entries
; searched for (RDG_AOTCN of 8 bytes, the slot the low byte of PC ^ PC >> 8
; ^ key): the host sizes the segment to both
RDG_AOTCOFF     equ 2000h
RDG_AOTCN       equ 256
%if RDG_AOTCN != 256
%error rdg_aotmiss takes its cache slot as a whole byte
%endif

; rdg_aotmiss - the core's tr_new2 for a ROM PC in neither its slot nor the
; neighbour, under a FIXED table (REALDMG-SPEC 16.8): SI = the PC, DH = its
; bank key, BX = the slot (the Game Boy's HL in [ss:S_SAVBX]), DL and ES
; free, every other register the Game Boy's. Into the block when the list has
; it, as tr_go goes; else into the interpreter, as tr_kint does. The core runs
; with SP the Game Boy's, so nothing is pushed: the search keeps its bounds in
; the view
rdg_aotmiss:
    mov byte [ss:S_HYLNK],0
.in:
    mov [ss:S_AOTKEY],dh
    mov es,[ss:S_AOTFSEG]           ; THE FILTER FIRST: a PC with no block -
    mov bx,si                       ;   one the walk did not find, a RAM PC,
    xor bh,dh                       ;   an image that was not loaded - is
    mov dl,bl                       ;   told in a dozen instructions, where
    and dl,7                        ;   the search is eleven rounds of the
    shr bx,1                        ;   list (the Victory: 45% of a 286 in
    shr bx,1                        ;   here, before it)
    shr bx,1
    mov [ss:S_AOTMID],bx
    mov bl,dl
    xor bh,bh
    mov dl,[cs:rdg_aotbits+bx]
    mov bx,[ss:S_AOTMID]
    test [es:bx],dl
    jz .nof
    mov bx,si                       ; THE CACHE NEXT: the last search's
    xor bl,bh                       ;   answer at this slot, a block or
%ifndef RDG_AOTCACHEBUG                 ; (tests/gbaot.py's control: the
    xor bl,dh                       ;   none, RDG_AOTCN of them past the
%endif                                  ;   key forgotten)
    xor bh,bh                       ;   filter. A frame's searches are few
    shl bx,1                        ;   PCs over and over (Looney Tunes:
    shl bx,1                        ;   16 of them 82%), and the search is
    shl bx,1                        ;   eleven rounds
    add bx,RDG_AOTCOFF
    mov [ss:S_AOTSLOT],bx
    cmp [es:bx],si
    jne .srch
%ifndef RDG_AOTCACHEBUG
    cmp [es:bx+2],dh
    jne .srch
%endif
    les dx,[es:bx+4]
    or dx,dx                        ; (offset 0: cached as not listed)
    jz .rh
    mov [ss:S_JITOUT],dx
    mov [ss:S_JITOUT+2],es
    jmp .go
.srch:
    mov es,[ss:S_AOTLSEG]
    mov bx,[ss:S_AOTLN]
    cmp byte [ss:S_AOTLX],1         ; (the list in parts, 20.17: which)
    ja .chunk
.bs:
    mov [ss:S_AOTCN],bx
    mov word [ss:S_AOTLO],0
    mov [ss:S_AOTHI],bx
.l:
    mov bx,[ss:S_AOTLO]             ; the first entry not below (key, PC)
    cmp bx,[ss:S_AOTHI]
    jae .end
    add bx,[ss:S_AOTHI]
    shr bx,1
    mov [ss:S_AOTMID],bx
    shl bx,1
    shl bx,1
    shl bx,1
    mov dl,[es:bx+2]
    cmp dl,[ss:S_AOTKEY]
    jb .lt
    ja .ge
    cmp [es:bx],si
    jb .lt
.ge:
    mov bx,[ss:S_AOTMID]
    mov [ss:S_AOTHI],bx
    jmp short .l
.lt:
    mov bx,[ss:S_AOTMID]
    inc bx
    mov [ss:S_AOTLO],bx
    jmp short .l
.end:
    cmp bx,[ss:S_AOTCN]
    jae .neg
    shl bx,1
    shl bx,1
    shl bx,1
    cmp [es:bx],si
    jne .neg
    mov dl,[es:bx+2]
    cmp dl,[ss:S_AOTKEY]
    jne .neg
    mov dx,[es:bx+4]                ; the block, as tr_go enters one -
    or dx,dx                        ;   unless its image was not loaded
    jz .neg                         ;   (REALDMG-SPEC 20.10: offset 0)
    mov [ss:S_JITOUT],dx
    mov dx,[es:bx+6]
    mov [ss:S_JITOUT+2],dx
    mov bx,[ss:S_AOTSLOT]           ; ...and into the cache (DS is free:
    mov ds,[ss:S_AOTFSEG]           ;   translated code keeps none)
    mov [bx],si
    mov dl,[ss:S_AOTKEY]
    mov [bx+2],dl
    mov dx,[ss:S_JITOUT]
    mov [bx+4],dx
    mov dx,[ss:S_JITOUT+2]
    mov [bx+6],dx
    jmp .go
.neg:                               ; not listed, the filter's bit another
    mov bx,[ss:S_AOTSLOT]           ;   PC's: cached too, offset 0, so the
    mov ds,[ss:S_AOTFSEG]           ;   next time costs no search either
    mov [bx],si
    mov dl,[ss:S_AOTKEY]
    mov [bx+2],dl
    mov word [bx+4],0
.rh:                                ; MISSES TRANSLATED (REALDMG-SPEC
    cmp byte [ss:S_HYON],0          ;   20.16): the filter names it and the
    je .no                          ;   host has no block - one made at run
    mov bx,si                       ;   time? The runtime table is open
    xor bh,[ss:S_AOTKEY]            ;   addressing, and an empty entry ends
    and bx,HY_TABN - 1              ;   the probe
    shl bx,1
    shl bx,1
    shl bx,1
    mov es,[ss:S_HYSEG]
.rp:
    cmp word [es:bx+HY_TAB+4],0
    je .cnt
    cmp [es:bx+HY_TAB],si
    jne .rn
    mov dl,[ss:S_AOTKEY]
    cmp [es:bx+HY_TAB+2],dl
    je .rf
.rn:
    add bx,8
    and bx,HY_TABN * 8 - 1
    jmp short .rp
.rf:
    les dx,[es:bx+HY_TAB+4]         ; it is: into the cache, as a listed
    mov [ss:S_JITOUT],dx            ;   block goes, and into it
    mov [ss:S_JITOUT+2],es
    mov bx,[ss:S_AOTSLOT]
    mov ds,[ss:S_AOTFSEG]
    mov [bx],si
    mov dl,[ss:S_AOTKEY]
    mov [bx+2],dl
    mov dx,[ss:S_JITOUT]
    mov [bx+4],dx
    mov dx,[ss:S_JITOUT+2]
    mov [bx+6],dx
.go:                                ; INTO THE BLOCK - or, from tr_link's
    mov bx,[ss:S_SAVBX]             ;   miss, chained first: tr_lpatch in
    cmp byte [ss:S_HYLNK],0         ;   the site's own tier patches the
    jne .lk                         ;   exit at [S_LINK] and goes on to it
    jmp far [ss:S_JITOUT]
.lk:
    mov es,[ss:S_JITOUT+2]
    jmp far [ss:S_HYLP]
.nof:                               ; the filter does not name it: no
    cmp byte [ss:S_HYON],0          ;   block anywhere, so a miss to count
    je .no
.cnt:
    cmp byte [ss:S_HYON],1          ; (2: the runtime tiers are full)
    jne .no
    cmp si,8000h                    ; RAM is never translated here (HRAM's
    jae .no                         ;   blocks are the host's, 20.5)
    inc word [ss:S_HYMISS]
    mov bx,si                       ; the count: a direct-mapped entry, a
    xor bh,[ss:S_AOTKEY]            ;   new key taking it over from 1 - the
    xor bl,bh                       ;   high byte folded in, or PCs 800h
    and bx,HY_CNTN - 1              ;   apart in one bank share it
    shl bx,1
    shl bx,1
    mov es,[ss:S_HYSEG]
    mov dl,[ss:S_AOTKEY]
    cmp [es:bx+HY_CNT],si
    jne .cn
    cmp [es:bx+HY_CNT+2],dl
    jne .cn
    inc byte [es:bx+HY_CNT+3]
    cmp byte [es:bx+HY_CNT+3],HY_HOT
    jb .no
    mov byte [es:bx+HY_CNT+3],0     ; (counting again, if it is not made)
    mov [ss:S_JITBK],dl             ; TRANSLATED, as tr_miss asks the
    mov word [ss:S_JITSLOT],0FFFFh  ;   translator - no slot: the host's
    mov word [ss:S_RET],RDGC_TR_BACK ;  door files it (rdg_hyins) - and
    mov dx,[ss:S_DISP+2]            ;   back at the core's tr_back, into
    mov [ss:S_RETFAR+2],dx          ;   the block or the interpreter
    mov bx,[ss:S_SAVBX]
    jmp far [ss:S_JITVEC]
.cn:
    mov [es:bx+HY_CNT],si
    mov dh,1
    mov [es:bx+HY_CNT+2],dx
.no:
    mov bx,[ss:S_SAVBX]             ; the interpreter's, as tr_kint goes
    jmp far [ss:S_AOTINT]

; (the list in parts, REALDMG-SPEC 20.17: the last part whose first entry
; is not above (key, PC) holds it if any does - parts are the sorted list cut
; in order - and part 0 is S_AOTLSEG's; DX is free, nothing is pushed)
rdg_aotmiss.chunk:
    mov bl,[ss:S_AOTLX]
    dec bl
    xor bh,bh
    shl bx,1
    shl bx,1
    shl bx,1
.ck:
%ifdef RDG_LXBUG                        ; (tests/gbaot.py's control: part
    jmp short .cp                       ;   0 searched whatever the key)
%endif
    mov dl,[ss:S_AOTKEY]
    cmp dl,[ss:bx+S_AOTLXT+4]
    jb .cp
    ja .ct
    cmp si,[ss:bx+S_AOTLXT+2]
    jae .ct
.cp:
    sub bx,8
    jnz .ck
    mov bx,[ss:S_AOTLN]             ; part 0: ES is its already
    jmp rdg_aotmiss.bs
.ct:
    mov es,[ss:bx+S_AOTLXT]
    mov bx,[ss:bx+S_AOTLXT+6]
    jmp rdg_aotmiss.bs

; rdg_aotlink - tr_lno's way here under S_HYON (20.16): tr_link's target,
; SI, DH its key, [S_LINK] the exit, not in the fixed table - looked up as
; any miss, and a block found is chained rather than only entered
rdg_aotlink:
    mov byte [ss:S_HYLNK],1
    jmp rdg_aotmiss.in

; rdg_hyins - the translator's door on a precompiled machine (20.16), after
; rt_translate: DS = the view, SI = the PC, [S_JITBK] its key, [S_JITOUT]
; the block (offset 0: none made). It goes into the runtime table, the
; filter names it, and the miss cache's entry for it is the block - so the
; next look finds it as it finds a listed one. A full tier (S_JITPRE 2) or a
; full table turns translating off: what was made is still found. Takes AX,
; BX, CX, DX and ES
rdg_hyins:
    cmp word [S_JITOUT],0
    je .x
    mov dl,[S_JITBK]
%ifdef RDG_HYBUG                        ; (tests/gbaot.py's control: filed
    xor dl,1                            ;   under the next bank's key, so a
%endif                                  ;   bank runs another's block)
    mov bx,si
    xor bh,dl
    and bx,HY_TABN - 1
    mov cl,3
    shl bx,cl
    mov es,[S_HYSEG]
.p:
    cmp word [es:bx+HY_TAB+4],0
    je .put
    cmp [es:bx+HY_TAB],si           ; (there already: a block made again
    jne .n                          ;   after its tier... never recycled,
    cmp [es:bx+HY_TAB+2],dl         ;   but a second make is harmless)
    je .put
.n:
    add bx,8
    and bx,HY_TABN * 8 - 1
    jmp short .p
.put:
    mov [es:bx+HY_TAB],si
    mov [es:bx+HY_TAB+2],dl
    mov ax,[S_JITOUT]
    mov [es:bx+HY_TAB+4],ax
    mov ax,[S_JITOUT+2]
    mov [es:bx+HY_TAB+6],ax
    inc word [S_HYN]
    cmp word [S_HYN],HY_TABMAX
    jb .f
    mov byte [S_HYON],2
.f:
    mov es,[S_AOTFSEG]              ; the filter's bit, as rdg_aotbitset
    mov bx,si
    xor bh,dl
    mov al,bl
    and al,7
    mov cl,al
    mov al,1
    shl al,cl
    mov cl,3
    shr bx,cl
    or [es:bx],al
    mov bx,si                       ; ...and the cache's entry, as
    xor bl,bh                       ;   rdg_aotmiss chooses it
%ifndef RDG_AOTCACHEBUG
    xor bl,dl
%endif
    xor bh,bh
    shl bx,cl
    add bx,RDG_AOTCOFF
    mov [es:bx],si
    mov [es:bx+2],dl
    mov ax,[S_JITOUT]
    mov [es:bx+4],ax
    mov ax,[S_JITOUT+2]
    mov [es:bx+6],ax
.x:
    cmp byte [S_JITPRE],2           ; the runtime tiers full
    jne .r
    mov byte [S_HYON],2
.r:
    ret

; rdg_aotattach - DS = the view: the list at AX (CX entries) attached with
; its filter at BX (8KB, built here from the entries with a block: one whose
; image was not loaded has offset 0), and the core's misses pointed at
; rdg_aotmiss in THIS segment. A list in PARTS (20.17): the host has put
; S_AOTLX and each part's segment and entries in S_AOTLXT, AX and CX being
; part 0's; each part's first entry is copied into its directory row here.
; S_AOTLX 0 is one part, AX's. Every register kept
rdg_aotattach:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [S_AOTLSEG],ax
    mov [S_AOTLN],cx
    mov [S_AOTFSEG],bx
    cmp byte [S_AOTLX],1
    ja .px
    mov byte [S_AOTLX],1            ; (one part: AX's, its row too)
    mov [S_AOTLXT],ax
    mov [S_AOTLXT+6],cx
.px:
    mov word [S_AOTVEC],rdg_aotmiss
    mov [S_AOTVEC+2],cs
    mov word [S_HYLVEC],rdg_aotlink
    mov [S_HYLVEC+2],cs
    mov word [S_HYLP],RDGC_TR_LPATCH
    mov word [S_AOTINT],RDGC_TR_INT
    mov si,[S_DISP+2]               ; the core
    mov [S_AOTINT+2],si
    mov es,bx                       ; the filter and the cache: none...
    xor di,di
    mov si,cx
    mov cx,(RDG_AOTCOFF + 8 * RDG_AOTCN) / 2
    xor ax,ax
    cld
    rep stosw
    push es                         ; (no RET site counted yet: tr_rmeg's
    push ds                         ;   S_RMEG, as rt_flush clears it)
    pop es
    mov di,S_RMEG
    mov cx,32
    rep stosw
    pop es
    mov dx,si                       ; ...then a bit for each block in the
    mov ax,[S_JITHSEG]              ;   TABLE - the core's tr_ejf asks this
    mov ch,[S_HMASK]                ;   before the table, so it has to name
    mov cl,0                        ;   every block there is - and for each
    inc ch                          ;   listed one; (mask + 1) x 256 entries
%ifndef RDG_NOTABFILT                   ; (tests/gbaot.py's control: the
    push ds                         ;   table left out, so tr_ejf turns
    call rdg_aotbitset              ;   its blocks away)
    pop ds
%endif
    xor si,si                       ; every part of the list: its bits,
.lp:                                ;   and its first entry in its row
    mov ax,[S_AOTLXT+si]
    mov cx,[S_AOTLXT+si+6]
    jcxz .lnx
    push si
    push ds
    mov ds,ax
    mov dx,[0]
    mov bl,[2]
    pop ds
    pop si
    mov [S_AOTLXT+si+2],dx
    mov [S_AOTLXT+si+4],bl
    push si
    push ds
    call rdg_aotbitset
    pop ds
    pop si
.lnx:
    add si,8
    mov al,[S_AOTLX]
    xor ah,ah
    shl ax,1
    shl ax,1
    shl ax,1
    cmp si,ax
    jb .lp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rdg_aotbitset - ES = the filter: a bit for each of CX 8-byte entries at
; AX:0 that has a block (offset not 0). Takes AX, BX, CX, SI, DI and DS
rdg_aotbitset:
    jcxz .x
    mov ds,ax
    xor si,si
.b:
    cmp word [si+4],0
    je .n
    mov bx,[si]                     ; h = PC ^ key << 8, as rdg_aotmiss
    xor bh,[si+2]
    mov di,bx
    and di,7
    shr bx,1
    shr bx,1
    shr bx,1
    mov al,[cs:rdg_aotbits+di]
    or [es:bx],al
.n:
    add si,8
    loop .b
.x:
    ret

rdg_aotbits:    db 1, 2, 4, 8, 16, 32, 64, 128
