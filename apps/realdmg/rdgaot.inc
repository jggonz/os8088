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
    jz .no
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
    jz .no
    mov [ss:S_JITOUT],dx
    mov [ss:S_JITOUT+2],es
    mov bx,[ss:S_SAVBX]
    jmp far [ss:S_JITOUT]
.srch:
    mov es,[ss:S_AOTLSEG]
    mov word [ss:S_AOTLO],0
    mov bx,[ss:S_AOTLN]
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
    cmp bx,[ss:S_AOTLN]
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
    mov bx,[ss:S_SAVBX]
    jmp far [ss:S_JITOUT]
.neg:                               ; not listed, the filter's bit another
    mov bx,[ss:S_AOTSLOT]           ;   PC's: cached too, offset 0, so the
    mov ds,[ss:S_AOTFSEG]           ;   next time costs no search either
    mov [bx],si
    mov dl,[ss:S_AOTKEY]
    mov [bx+2],dl
    mov word [bx+4],0
.no:
    mov bx,[ss:S_SAVBX]             ; the interpreter's, as tr_kint goes
    jmp far [ss:S_AOTINT]

; rdg_aotattach - DS = the view: the list at AX (CX entries) attached with
; its filter at BX (8KB, built here from the entries with a block: one whose
; image was not loaded has offset 0), and the core's misses pointed at
; rdg_aotmiss in THIS segment. Every register kept
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
    mov word [S_AOTVEC],rdg_aotmiss
    mov [S_AOTVEC+2],cs
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
    mov cx,dx
    mov ax,[S_AOTLSEG]
    push ds
    call rdg_aotbitset
    pop ds
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
