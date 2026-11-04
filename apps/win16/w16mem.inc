; =============================================================================
; os8088 - apps/win16/w16mem.inc
;
; MEMORY: THE GLOBAL AND LOCAL HEAPS (SPEC.md 124.9). Shaped after Wine's
; krnl386 global.c and local.c as winevdm calls them, cut to real mode.
;
; THE GLOBAL HEAP is WIN16's, sub-allocated inside ARENAS: claims of
; W16_ARENAKB, or one the size of a larger request, at most W16_NARENA. Every
; block is whole paragraphs behind a one-paragraph header (below), first fit,
; free neighbours merged, and an arena that is wholly free again is given
; back. A GMEM_FIXED handle IS the block's segment, as real-mode Windows
; answered; a moveable one is an entry of w16_ghseg, W16_GH0 + 2i, a value no
; RAM segment of ours can have. Nothing moves yet, and no caller may count on
; that (WIN16-PLAN.md R3).
;
; THE LOCAL HEAP is in the caller's DS: a descriptor at its start, its offset
; at DS:0006 (pLocalHeap, as Windows kept it), then blocks of a size word and
; their bytes. A moveable local handle is a two-word entry block whose first
; word is the data, so `*(char **)h` reads what LocalLock answers.
; =============================================================================

W16_ARENAKB equ 32              ; the first arena's KB unless a request is
                                ;   more; the second is twice it
W16_LHROOM  equ 4096            ; a library's local heap, past its ne_heap
                                ;   (SPEC.md 124.25.2)
%ifndef PKG_MEGA
W16_ARKEEP  equ 32              ; ...and the third leaves this much of the
                                ;   largest free run (SPEC.md 124.25.1)
%else
W16_ARKEEP  equ 96              ; ...on kern_mega the library part's 62 KB
%endif                          ;   too: its fetch is conventional (124.31.1)
W16_GH0     equ 0x0100          ; the first moveable global handle; + 2i
%ifdef PKG_MEGA
  %if W16_GH0 + 2 * W16_NGH != W16V_LO
    %error "W16V_LO must be the end of the handle range (SPEC.md 124.31.3)"
  %endif
%endif
W16_GHDISC  equ 2               ; [w16_ghf]: GMEM_DISCARDABLE (1 = in use)
W16_GHNOTE  equ 4               ;   ...and GMEM_NOTIFY: discarded only when
                                ;   GlobalNotify's procedure says so

GMEM_MOVEABLE equ 0x0002
GMEM_ZEROINIT equ 0x0040
GMEM_MODIFY   equ 0x0080
GMEM_DISCARDED equ 0x4000       ; GlobalFlags' answer
GMEM_DISCARDABLE_HI equ 0x01    ; GMEM_DISCARDABLE (0x0100), the high byte's
GMEM_NOTIFY_HI      equ 0x40    ; GMEM_NOTIFY (0x4000), the same

; --- a global block's header, the paragraph before its data -----------------
GH_SIZE   equ 0                 ; paragraphs, the header included
GH_FLAGS  equ 2                 ; GHF_*
GH_LOCK   equ 3
GH_HANDLE equ 4                 ; what GlobalAlloc answered for it
GH_OWNER  equ 6                 ; a resource's entry offset (w16res.inc), or 0
GH_MAGIC  equ 8
GH_SIG    equ 0x4857            ; 'WH'
GH_TASK   equ 10                ; the task whose end frees it, + 1; 0 = none's
                                ;   (WIN16's own, or GMEM_DDESHARE: 124.28.3)
GMEM_DDESHARE_HI equ 0x20       ; GMEM_DDESHARE (0x2000), the high byte's
GH_TN     equ 11                ; the kern_mega arm's: a block over 64 KB's
GH_TSEL   equ 12                ;   SEG_TILE count (a byte), its first
GH_TVP    equ 14                ;   selector (0 = none yet), and where it was
                                ;   made (124.31.6)

GHF_USED  equ 1
GHF_MOVE  equ 2
GHF_RES   equ 4                 ; a loaded resource: discardable when unused
GHF_CODE  equ 8                 ; a discardable code segment, GH_OWNER its
                                ;   number (SPEC.md 124.26.3)
GHF_ICON  equ 16                ; an icon CreateCursorIconIndirect made, which
                                ;   DestroyIcon frees (124.27.7)
GHF_HND   equ 32                ; the kern_mega arm's: GH_TSEL is the block's
                                ;   handle descriptor (w16_pblk, 124.31.3)
W16G_HIGH equ 0x01              ; w16_gnew's CL: from the TOP of an arena, the
                                ;   last free run that holds it (124.26.3)
W16G_NOCODE equ 0x04            ; ...and: the program's code is not discarded
                                ;   for it (GlobalAlloc with a GlobalNotify
                                ;   procedure, SPEC.md 124.9.1)

; =============================================================================
; The global heap, inside
; =============================================================================

; -----------------------------------------------------------------------------
; w16_gnew - allocate. in: DX:AX = bytes, CL = GMEM_* (low byte)
; out: AX = the handle (0 = none), DX = the data's segment. Keeps the rest.
; -----------------------------------------------------------------------------
w16_gnew:
    push bx
    push cx
    push si
    push di
    push es
    mov [w16_gflg], cl
    mov bx, ax
    or bx, dx
    jnz .sized
    test cl, GMEM_MOVEABLE          ; 0 bytes: a moveable handle with nothing
    jz .fail                        ;   behind it, which is a discarded one
    call w16_ghnew                  ; -> SI = the entry, AX = the handle
    jc .fail
    mov word [w16_ghseg + si], 0
    xor dx, dx
    jmp .out
.sized:
    add ax, 15                      ; paragraphs = ceil(bytes / 16), + 1
    adc dx, 0
    mov cx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx
    jnz .fail
    cmp ax, 0xFE00                  ; under an arena's 1023KB
    jae .fail
    inc ax
    mov bx, ax                      ; BX = the block
    call w16_gfit                   ; -> AX = its header; CF=1 none fits
    jnc .got
    call w16_garena                 ; a new arena for it
    jnc .fit2
    call w16_gdiscard               ; unused resources, then once more
    jc .old
    call w16_gfit
    jnc .got
.old:
    call w16_gdold                  ; then a discardable block at a time,
    jc .fail                        ;   the least recently used first
    call w16_gfit
    jnc .got
    call w16_garena                 ; (an arena it emptied went back)
    jc .old
.fit2:
    call w16_gfit
    jc .fail
.got:
    W16WALK 0, es, ax               ; ES = the header
    mov byte [es:GH_LOCK], 0
    mov byte [es:GH_TASK], 0        ; nobody's until w16_gmine
    mov word [es:GH_OWNER], 0
%ifdef PKG_MEGA
    mov word [es:GH_TSEL], 0
    mov byte [es:GH_TN], 0
%endif
    inc ax
    mov dx, ax                      ; DX = the data
    test byte [w16_gflg], GMEM_ZEROINIT
    jz .nozero
    call w16_gzero
.nozero:
    test byte [w16_gflg], GMEM_MOVEABLE
    jz .fixed
    call w16_ghnew                  ; SI = the entry, AX = the handle
    jc .nohandle
    mov [w16_ghseg + si], dx
    or byte [es:GH_FLAGS], GHF_MOVE
    jmp short .hset
.nohandle:
    W16VOF ax, es
    call w16_gfreeblk
    jmp short .fail
.fixed:
    mov ax, dx
.hset:
    mov [es:GH_HANDLE], ax
    jmp short .out
.fail:
    xor ax, ax
    xor dx, dx
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; w16_gzero - zero block ES's data (header ES). Keeps all.
w16_gzero:
    push ax
    push bx
    push cx
    push di
    push es
    mov bx, [es:GH_SIZE]
    dec bx                          ; BX = data paragraphs
    W16VOF ax, es
    inc ax
.chunk:
    or bx, bx
    jz .done
    W16WALK 1, es, ax
    mov cx, bx
    cmp cx, 0x0FFF
    jbe .last
    mov cx, 0x0FFF
.last:
    sub bx, cx
    add ax, cx
    push ax
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; words
    xor di, di
    xor ax, ax
    cld
    rep stosw
    pop ax
    jmp short .chunk
.done:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; w16_ghnew - a free moveable entry: SI = its offset in w16_ghseg (2i), AX =
; its handle; CF=1 none. Keeps the rest.
w16_ghnew:
    xor si, si
.t:
    cmp si, 2 * W16_NGH
    jae .full
    cmp byte [w16_ghf + si], 0
    je .hit
    inc si
    inc si
    jmp short .t
.hit:
    mov byte [w16_ghf + si], 1
    mov ax, si
    add ax, W16_GH0
    clc
    ret
.full:
    stc
    ret

; -----------------------------------------------------------------------------
; w16_gfit - BX = paragraphs: the first free block that holds them, split to
; size and marked used - or, with W16G_HIGH in [w16_gflg], the LAST, cut from
; its top (SPEC.md 124.26.3). out: AX = its header; CF=1 none. Keeps the rest.
; -----------------------------------------------------------------------------
w16_gfit:
    push cx
    push dx
    push si
    push es
    xor dx, dx                      ; DX = the last that fits (W16G_HIGH)
    xor si, si
.arena:
    cmp si, 2 * W16_NARENA
    jae .none
    mov ax, [w16_arena + si]
    or ax, ax
    jz .nexta
.blk:
    cmp ax, [w16_arenae + si]
    jae .nexta
    W16WALK 0, es, ax
    mov cx, [es:GH_SIZE]
    test byte [es:GH_FLAGS], GHF_USED
    jz .free
    test byte [es:GH_FLAGS], GHF_CODE ; W16G_NOCODE's data stays below the
    jz .skip                        ;   code, so a hole a segment left is a
    test byte [w16_gflg], W16G_NOCODE ; segment's again (SPEC.md 124.9.1)
    jnz .nexta
    jmp short .skip
.free:
    cmp cx, bx
    jae .fits
.skip:
    add ax, cx
    jmp short .blk
.nexta:
    inc si
    inc si
    jmp short .arena
.fits:
    test byte [w16_gflg], W16G_HIGH
    jz .low
    mov dx, ax
    jmp short .skip
.top:                               ; the last fit: the top of it is the block
    mov ax, dx
    W16WALK 0, es, ax
    mov cx, [es:GH_SIZE]
    sub cx, bx
    jz .exact
    mov [es:GH_SIZE], cx            ; the bottom stays free
    add ax, cx
    W16WALK 0, es, ax
    mov [es:GH_SIZE], bx
    jmp short .exact
.low:
    sub cx, bx                      ; CX = what is left over
    jz .exact
    mov dx, ax                      ; the rest becomes a free block
    add dx, bx
    push es
    W16WALK 1, es, dx
    mov [es:GH_SIZE], cx
    mov byte [es:GH_FLAGS], 0
    mov word [es:GH_MAGIC], GH_SIG
    pop es
    mov [es:GH_SIZE], bx
.exact:
    mov byte [es:GH_FLAGS], GHF_USED
    mov word [es:GH_MAGIC], GH_SIG
    clc
    jmp short .out
.none:
    or dx, dx
    jnz .top
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16_garena - a new arena big enough for BX paragraphs. CF=1 none (no slot,
; or no memory). Keeps all.
; -----------------------------------------------------------------------------
w16_garena:
    push ax
    push cx
    push dx
    push si
    push es
    xor si, si
.slot:
    cmp si, 2 * W16_NARENA
    jae .no
    cmp word [w16_arena + si], 0
    je .free
    inc si
    inc si
    jmp short .slot
.free:
    push si                         ; the floor, by the arenas already in use
    push bx                         ;   (SPEC.md 124.25.1): 32 KB, then 64,
    xor cx, cx                      ;   then what is free but W16_ARKEEP
    xor si, si
.cnt:
    cmp word [w16_arena + si], 0
    je .nc
    inc cx
.nc:
    inc si
    inc si
    cmp si, 2 * W16_NARENA
    jb .cnt
    mov dx, W16_ARENAKB * 64        ; ...all in PARAGRAPHS (SPEC.md 50.3.7)
    jcxz .fl
    mov dx, 2 * W16_ARENAKB * 64
    cmp cx, 1
    je .fl
    mov bl, MEMP_AVAIL
    call OSAPI_MEM_P                ; AX = the largest claim, paragraphs
    sub ax, W16_ARKEEP * 64         ;   (BX and CX are not ours to keep:
    jb .fl                          ;   both come back off the stack or are
    cmp ax, dx                      ;   set again below)
    jbe .fl
    mov dx, ax
.fl:
    pop bx
    pop si
    mov ax, bx                      ; the request's own paragraphs, to fall
    push ax                         ;   back to
    cmp ax, dx
    jae .kb
    mov ax, dx
.kb:
    push ax                         ; what this try asks
    W16VCLAIM                       ; DX = the segment, AX = GRANTED
    mov cx, ax
    pop ax
    jnc .got1
    pop cx                          ; the floor would not go: three
    push cx                         ;   quarters of it, and again, down to
    cmp ax, cx                      ;   the request (the arm's map is 1,012
    jbe .no1                        ;   KB, so a fourth arena is what it
    mov dx, ax                      ;   has left: halving left a third of
    shr dx, 1                       ;   that unused)
    shr dx, 1
    inc dx
    sub ax, dx
    cmp ax, cx
    jae .kb
    mov ax, cx
    jmp short .kb
.no1:
    pop ax
    jmp short .no
.got1:
    pop ax
.got:
    mov [w16_arena + si], dx
    W16SEG es, dx
    mov ax, cx                      ; the arena is all it was granted: the
    mov [es:GH_SIZE], ax            ;   granule's slack is a free block's
    mov byte [es:GH_FLAGS], 0
    mov word [es:GH_MAGIC], GH_SIG
    add ax, dx
    mov [w16_arenae + si], ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_gfreeblk - AX = a block's header: free it, merge its arena's free runs,
; and give the arena back if nothing in it is used. Keeps all.
; -----------------------------------------------------------------------------
w16_gfreeblk:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    W16WALK 1, es, ax
%ifdef PKG_MEGA
    test byte [es:GH_FLAGS], GHF_HND ; its handle descriptor goes with it
    jz .nowin                       ;   (w16_pblk). A huge block's tiles
    xor si, si                      ;   are the arena's
    mov dx, [es:GH_TSEL]
    mov bl, SEG_HND
    call OSAPI_SEG
.nowin:
%endif
    mov byte [es:GH_FLAGS], 0
    mov byte [es:GH_LOCK], 0
    xor si, si                      ; which arena
.arena:
    cmp si, 2 * W16_NARENA
    jae .out
    mov dx, [w16_arena + si]
    or dx, dx
    jz .nexta
    cmp ax, dx
    jb .nexta
    cmp ax, [w16_arenae + si]
    jb .this
.nexta:
    inc si
    inc si
    jmp short .arena
.this:
    xor bx, bx                      ; BX = 1 once a used block is seen
    mov ax, dx
.walk:
    cmp ax, [w16_arenae + si]
    jae .walked
    W16WALK 0, es, ax
    test byte [es:GH_FLAGS], GHF_USED
    jz .free
    mov bx, 1
    add ax, [es:GH_SIZE]
    jmp short .walk
.free:
    mov cx, ax                      ; merge every free block that follows
    add cx, [es:GH_SIZE]
    cmp cx, [w16_arenae + si]
    jae .walked
    push es
    W16WALK 1, es, cx
    test byte [es:GH_FLAGS], GHF_USED
    mov cx, [es:GH_SIZE]
    pop es
    jnz .nomerge
    add [es:GH_SIZE], cx
    jmp short .free
.nomerge:
    add ax, [es:GH_SIZE]
    jmp short .walk
.walked:
    or bx, bx
    jnz .out
%ifdef PKG_MEGA
    call w16_vwdrop                 ; (a walk descriptor may be in it)
%endif
    mov dx, [w16_arena + si]        ; nothing used: give it back
    W16VFREE
    mov word [w16_arena + si], 0
    mov word [w16_arenae + si], 0
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_gfind - AX = a handle -> ES = its block's header and CF=0; SI = its
; moveable entry (2i) or 0FFFFh. A discarded handle: CF=0, ES = 0 (check
; [w16_gdisc]). CF=1 not a handle of ours. Keeps AX, BX, CX, DX, DI.
; -----------------------------------------------------------------------------
w16_gfind:
    push ax
    push bx
    mov byte [w16_gdisc], 0
    mov si, 0xFFFF
    cmp ax, W16_GH0
    jb .seg
    cmp ax, W16_GH0 + 2 * W16_NGH
    jae .seg
    test al, 1
    jnz .bad
    mov si, ax
    sub si, W16_GH0
    cmp byte [w16_ghf + si], 0
    je .bad
    mov ax, [w16_ghseg + si]
    or ax, ax
    jnz .seg
    mov byte [w16_gdisc], 1         ; discarded: no block
    mov es, ax
    jmp short .ok
.seg:
    or ax, ax
    jz .bad
    dec ax                          ; the header is the paragraph before
    call w16_ginheap
    jc .bad
    W16SEG es, ax
    cmp word [es:GH_MAGIC], GH_SIG
    jne .bad
    test byte [es:GH_FLAGS], GHF_USED
    jz .bad
.ok:
    pop bx
    pop ax
    clc
    ret
.bad:
    pop bx
    pop ax
    stc
    ret

; w16_ginheap - CF=0 if paragraph AX is inside one of our arenas. Keeps all.
w16_ginheap:
    push si
    xor si, si
.a:
    cmp si, 2 * W16_NARENA
    jae .no
    cmp word [w16_arena + si], 0
    je .n
    cmp ax, [w16_arena + si]
    jb .n
    cmp ax, [w16_arenae + si]
    jb .yes
.n:
    inc si
    inc si
    jmp short .a
.yes:
    pop si
    clc
    ret
.no:
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; w16_gfreeh - AX = a handle: free its block and its entry. CF=1 not ours.
; Keeps all.
; -----------------------------------------------------------------------------
w16_gfreeh:
    push ax
    push si
    push es
    call w16_gfind
    jc .out
    cmp byte [w16_gdisc], 0
    jne .entry
    W16VOF ax, es
    call w16_gfreeblk
.entry:
    cmp si, 0xFFFF
    je .ok
    mov byte [w16_ghf + si], 0
    mov word [w16_ghseg + si], 0
.ok:
    clc
.out:
    pop es
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_gdold - discard the least recently used GMEM_DISCARDABLE block nobody
; has locked (SPEC.md 124.9): the handle stays, with no block, as Windows left
; it - GlobalLock answers NULL, GlobalFlags GMEM_DISCARDED, and GlobalReAlloc
; gives it a block again. A code segment older than it and not in use goes
; instead (SPEC.md 124.26.5). CF=1: there was none. Keeps all.
; -----------------------------------------------------------------------------
w16_gdold:
    push ax
    push dx
    push si
    push di
    push es
    mov di, 0xFFFF                  ; DI = the oldest so far, DX its use
    xor si, si
.h:
    test byte [w16_ghf + si], W16_GHDISC
    jz .n
    mov ax, [w16_ghseg + si]
    or ax, ax
    jz .n                           ; discarded already
    dec ax                          ; the header
    W16SEG es, ax
    cmp byte [es:GH_LOCK], 0
    jne .n
    mov ax, [w16_ghage + si]
    cmp di, 0xFFFF
    je .take
    cmp ax, dx
    jae .n
.take:
    mov di, si
    mov dx, ax
.n:
    inc si
    inc si
    cmp si, 2 * W16_NGH
    jb .h
    cmp word [w16_ldk], 0           ; the program's code (part 5 kept),
    je .data                        ;   unless the request is data whose
    test byte [w16_gflg], W16G_NOCODE ; owner gives blocks up (124.9.1)
    jnz .data
    cmp di, 0xFFFF
    jne .lim
    mov dx, 0xFFFF                  ; no data block: any age
.lim:
    push bx
    mov bx, w16l_codeold            ; XSEC-OK: part 5's, w16_ldcall calls it there
    call w16_ldcall                 ; CF=0: a segment went
    pop bx
    jnc .out
.data:
    cmp di, 0xFFFF
    je .none
    mov ax, [w16_ghseg + di]
    dec ax
    call w16_gfreeblk
    mov word [w16_ghseg + di], 0
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop ax
    ret

; w16_gtouch - SI = a moveable handle's entry (0xFFFF: a fixed block's, and
; nothing is kept): it is the most recently used now. Keeps all.
w16_gtouch:
    cmp si, 0xFFFF
    je .ret
    push ax
    inc word [w16_gclock]
    mov ax, [w16_gclock]
    mov [w16_ghage + si], ax
    pop ax
.ret:
    ret

; w16_gmine - DX = a block's data: the current task's, freed when it ends
; (SPEC.md 124.28.3). Keeps every register
w16_gmine:
    push ax
    push es
    dec dx
    W16SEG es, dx
    mov al, [w16_cur]
    inc ax
    mov [es:GH_TASK], al
    inc dx
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16_gdiscard - free every loaded resource block nobody is using (SPEC.md
; 124.9's "out of memory" rule). CF=1 there was none. Keeps all.
; -----------------------------------------------------------------------------
w16_gdiscard:
    push ax
    push bx
    push si
    push es
    xor bx, bx                      ; BX = 1 once one went
    xor si, si
.arena:
    cmp si, 2 * W16_NARENA
    jae .done
    mov ax, [w16_arena + si]
    or ax, ax
    jz .nexta
.blk:
    cmp ax, [w16_arenae + si]
    jae .nexta
    W16WALK 0, es, ax
    test byte [es:GH_FLAGS], GHF_USED
    jz .skip
    test byte [es:GH_FLAGS], GHF_RES
    jz .skip
    push ax
    push si
    mov ax, [es:GH_OWNER]
    or ax, ax
    jns .prog
    LCALL w16n_rdisc                ; a library's resource (SPEC.md 124.23.6)
    jmp short .disc
.prog:
    call w16_rdiscard               ; AX = the entry: CF=0 it was unused, and
.disc:
    pop si                          ;   its block is gone; start the arena again
    pop ax
    jc .kept
    mov bx, 1
    jmp short .arena                ; (the arena itself may be gone)
.kept:
    W16WALK 0, es, ax               ; (the call may have walked: aimed again)
.skip:
    add ax, [es:GH_SIZE]
    jmp short .blk
.nexta:
    inc si
    inc si
    jmp short .arena
.done:
    cmp bx, 1                       ; CF = no block went
    pop es
    pop si
    pop bx
    pop ax
    ret

; w16_gfreeall - the program has ended: every arena back, every entry free
w16_gfreeall:
    push dx
    push si
    xor si, si
.a:
    cmp si, 2 * W16_NARENA
    jae .h
    mov dx, [w16_arena + si]
    or dx, dx
    jz .n
%ifdef PKG_MEGA
    call w16_vwdrop
%endif
    W16VFREE
    mov word [w16_arena + si], 0
    mov word [w16_arenae + si], 0
.n:
    inc si
    inc si
    jmp short .a
.h:
    mov [w16_odseg], si             ; the own DCs' block went with them
    mov byte [w16_odcur], 0
.e:
    mov byte [w16_ghf + si], 0
    mov word [w16_ghseg + si], 0
    inc si
    inc si
    cmp si, 2 * W16_NGH
    jb .e
    pop si
    pop dx
    ret

; =============================================================================
; The global heap, as Windows exports it
; =============================================================================

; GlobalAlloc (KERNEL.15) - args: +0 dwBytes (long), +4 wFlags. A moveable
; block asked for GMEM_DISCARDABLE is marked so on its handle: w16_gdold may
; take it when the heap is short, and the handle stays, discarded (SPEC.md
; 124.9). With GMEM_NOTIFY as well it is the program's to give up: when
; the heap cannot hold a request, the GlobalNotify procedure is asked about
; the least recently used of them, as Windows asked it, and the block goes
; only if it answers nonzero - Paintbrush's DISCARDBAND writes a band of
; the picture to its swap file first, and locks a band it has not been
; asked about without looking for NULL (SPEC.md 124.9.1). Then the request
; is tried again.
w16k_galloc:
    call w16_args
    mov ax, [es:bx]
    mov dx, [es:bx + 2]
    mov cl, [es:bx + 4]
    mov ch, [es:bx + 5]
    cmp word [w16_gnproc + 2], 0    ; a GlobalNotify procedure: it is asked
    je .new                         ;   to give a block up before the code
    or cl, W16G_NOCODE              ;   goes (SPEC.md 124.9.1)
.new:
    call w16_gnew
    or ax, ax
    jz .short
    test ch, GMEM_DDESHARE_HI       ; the program's, freed when it ends -
    jnz .own                        ;   unless it is to be shared (124.28.3)
    or dx, dx
    jz .own
    call w16_gmine
.own:
    cmp ax, W16_GH0                 ; a moveable handle's entry: used now,
    jb .out                         ;   and marked if it may be discarded.
    cmp ax, W16_GH0 + 2 * W16_NGH   ;   A fixed one IS its segment, above
    jae .out                        ;   every entry (SPEC.md 124.9)
    push si
    mov si, ax
    sub si, W16_GH0
    call w16_gtouch
    test ch, GMEM_DISCARDABLE_HI
    jz .nd
    mov cl, W16_GHDISC
    test ch, GMEM_NOTIFY_HI
    jz .mark
    mov cl, W16_GHNOTE
.mark:
    or [w16_ghf + si], cl
.nd:
    pop si
.out:
    W16POUT
    ret
.short:
    LJMP w16k_gnask                 ; the procedure, asked: it runs, and the
                                    ;   request is tried again after

; GlobalFree (KERNEL.17) - args: +0 hMem. AX = 0 freed, else the handle
w16k_gfree:
    call w16_args
    mov ax, [es:bx]
    W16PIN
    call w16_gfreeh
    jc .no
    xor ax, ax
.no:
    ret

; GlobalLock (KERNEL.18) - args: +0 hMem. DX:AX = the block, or NULL
w16k_glock:
    call w16_args
    mov ax, [es:bx]
    W16PIN
    call w16_gfind
    jc .null
    cmp byte [w16_gdisc], 0
    jne .null
    inc byte [es:GH_LOCK]
    jnz .l
    dec byte [es:GH_LOCK]           ; it saturates rather than wraps
.l:
    call w16_gtouch                 ; (the order w16_gdold discards in)
    W16VOF dx, es
    inc dx
%ifdef PKG_MEGA
    xchg ax, dx
    call w16_pblk                   ; the program's selector for it
    xchg ax, dx
%endif
    xor ax, ax
    ret
.null:
    mov cl, 2                       ; the DGROUP is a block too (124.9.2)
    jmp w16_dgx

; GlobalUnlock (KERNEL.19) - args: +0 hMem. AX = the lock count left
w16k_gunlock:
    call w16_args
    mov ax, [es:bx]
    W16PIN
    call w16_gfind
    jc .zero
    cmp byte [w16_gdisc], 0
    jne .zero
    cmp byte [es:GH_LOCK], 0
    je .zero
    dec byte [es:GH_LOCK]
    mov al, [es:GH_LOCK]
    xor ah, ah
    xor dx, dx
    ret
.zero:
    xor ax, ax
    xor dx, dx
    ret

; GlobalSize (KERNEL.20) - args: +0 hMem. DX:AX = its bytes
w16k_gsize:
    call w16_args
    mov ax, [es:bx]
    W16PIN
    call w16_gfind
    jc .zero
    cmp byte [w16_gdisc], 0
    jne .zero
    mov ax, [es:GH_SIZE]
    dec ax
    jmp short w16_p2b
.zero:
    mov cl, 4
    jmp w16_dgx

; w16_p2b - AX paragraphs -> DX:AX bytes. Clobbers CX
w16_p2b:
    xor dx, dx
    mov cx, 4
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
    ret

; GlobalHandle (KERNEL.21) - args: +0 wSeg. AX = its handle, DX = the segment
w16k_ghandle:
    call w16_args
    mov ax, [es:bx]
    W16PIN
    call w16_gfind
    jc .zero
    cmp byte [w16_gdisc], 0
    jne .zero
    mov ax, [es:GH_HANDLE]
    W16VOF dx, es
    inc dx
%ifdef PKG_MEGA
    xchg ax, dx
    call w16_pblk
    xchg ax, dx
    W16POUT
%endif
    ret
.zero:
    xor cl, cl
    jmp w16_dgx

; GlobalFlags (KERNEL.22) - args: +0 hMem. AX = the lock count, and
; GMEM_DISCARDED for a discarded handle
w16k_gflags:
    call w16_args
    mov ax, [es:bx]
    W16PIN
    call w16_gfind
    jc .zero
    mov ax, GMEM_DISCARDED
    cmp byte [w16_gdisc], 0
    jne .out
    mov al, [es:GH_LOCK]
    xor ah, ah
.out:
    xor dx, dx
    ret
.zero:
    xor ax, ax
    xor dx, dx
    ret

; LockSegment (KERNEL.23), UnlockSegment (KERNEL.24) - args: +0 wSeg, -1
; meaning the caller's DS. Nothing moves, so the segment is the answer.
w16k_lockseg:
    call w16_args
    mov ax, [es:bx]
    cmp ax, 0xFFFF
    jne .out
    mov es, [w16_pss]
    mov bx, [w16_psp_]
    mov ax, [es:bx + W16F_DS]
.out:
    ret

; GlobalCompact (KERNEL.25) and GetFreeSpace (KERNEL.169) are the system-library
; part's (w16mem.inc's end, SPEC.md 124.25.1, *What GetFreeSpace says*)

; w16_gcan - ES = a block's header. CF=0 when GlobalAlloc can have its room:
; it is free, or a code segment that is not kept (SPEC.md 124.26.5). One in
; use is counted too, as Windows counted all discardable memory: the stack
; is scanned only when a segment is about to go. Keeps all.
w16_gcan:
    test byte [es:GH_FLAGS], GHF_USED
    jz .yes
    test byte [es:GH_FLAGS], GHF_CODE
    jz .no
    push ax
    push si
    push es
    mov ax, [w16_clive]             ; the program's live table, or a
    mov si, [es:GH_OWNER]           ;   library's: its record + 1 in the
    cmp byte [es:GH_OWNER + 1], 0   ;   owner's high byte (124.26.11)
    je .prog
    push si
    mov ax, si
    mov al, ah
    dec ax
    mov ah, W16L_SIZE
    mul ah
    mov si, ax
    W16SEG es, [si + w16_libs + W16L_MREC]
    pop si
    mov ax, [es:W16LM_CD + 4]       ; its w16_clive
.prog:
    and si, 0xFF                    ; its live entry, 6 bytes a segment
    dec si
    push ax
    mov ax, si
    shl si, 1
    add si, ax
    shl si, 1
    pop ax
    W16SEG es, ax
    test byte [es:si + W16CL_FL], W16CLF_KEEP
    pop es
    pop si
    pop ax
    jnz .no
.yes:
    clc
    ret
.no:
    stc
    ret

; GlobalReAlloc (KERNEL.16) - args: +0 wFlags, +2 dwBytes (long), +6 hMem.
; AX = the handle (a fixed block that moved answers its new one), 0 refused
w16k_grealloc:
    push si
    push di
    call w16_args
    mov cl, [es:bx]
    mov [w16_gflg], cl
    mov ax, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov di, [es:bx + 6]             ; DI = the handle
%ifdef PKG_MEGA
    xchg ax, di
    call w16_pin
    xchg ax, di
%endif
    test cl, GMEM_MODIFY
    jz .size
    mov ax, di                      ; the flags alone: whether it may be
    cmp ax, W16_GH0                 ;   discarded is all that changes here
    jb .out
    cmp ax, W16_GH0 + 2 * W16_NGH
%ifdef PKG_MEGA
    jae .mfx
%else
    jae .out                        ; (a fixed one IS its handle)
%endif
    mov si, ax
    sub si, W16_GH0
    and byte [w16_ghf + si], ~(W16_GHDISC | W16_GHNOTE)
    test byte [es:bx + 1], GMEM_DISCARDABLE_HI
    jz .out
    mov cl, W16_GHDISC              ; GlobalAlloc's marks
    test byte [es:bx + 1], GMEM_NOTIFY_HI
    jz .mk
    mov cl, W16_GHNOTE
.mk:
    or [w16_ghf + si], cl
    jmp .out
%ifdef PKG_MEGA
.mfx:                               ; a fixed one: the handle as the program
    mov ax, [es:bx + 6]             ;   passed it. W16POUT would read a block
    jmp .dx                         ;   header before a DGROUP, which has none
%endif                              ;   (SPEC.md 124.9.2)
.size:
    add ax, 15
    adc dx, 0
    mov cx, 4
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx
    jnz .fail
    cmp ax, 0xFE00
    jae .fail
    inc ax
    mov [w16_gnp], ax               ; the block it must become, paragraphs
    mov ax, di
    call w16_gfind                  ; ES = the header, SI = the entry
    jc .fail
    cmp byte [w16_gdisc], 0
    je .live
    ; a discarded moveable handle: give it a block again
    mov ax, [w16_gnp]
    dec ax
    jnz .bytes
    mov ax, di                      ; 0 bytes: it stays discarded
    jmp .out
.bytes:
    call w16_p2b
    mov cl, GMEM_MOVEABLE
    or cl, [w16_gflg]
    push si
    call w16_gnew                   ; a new handle, whose block we take
    pop si
    or ax, ax
    jz .fail
    call w16_gsteal                 ; handle AX's block onto entry SI
    mov ax, di
    jmp .out
.live:
    mov cx, [es:GH_SIZE]
    mov bx, [w16_gnp]
    cmp bx, cx
    ja .grow
    je .same
    ; shrink: the rest becomes a free block
    mov [es:GH_SIZE], bx
    W16VOF ax, es
    add ax, bx
    push es
    W16WALK 1, es, ax
    sub cx, bx
    mov [es:GH_SIZE], cx
    mov byte [es:GH_FLAGS], GHF_USED
    mov word [es:GH_MAGIC], GH_SIG
    pop es
    call w16_gfreeblk               ; (marks it free and merges)
%ifdef PKG_MEGA
    call w16_phfix                  ; its selector, at its new length
%endif
.same:
    mov ax, di
    jmp .out
.grow:
    ; in place, when the next block is free and big enough together
    W16VOF ax, es
    add ax, cx
    call w16_ginheap
    jc .move
    push es
    W16SEG es, ax
    cmp word [es:GH_MAGIC], GH_SIG
    jne .nonext
    test byte [es:GH_FLAGS], GHF_USED
    jnz .nonext
    mov dx, [es:GH_SIZE]
    pop es
    add dx, cx                      ; DX = both
    cmp dx, bx
    jb .move
    push es
    W16SEG es, ax
    mov byte [es:GH_FLAGS], GHF_USED ; take the neighbour whole, then cut
    pop es
    mov [es:GH_SIZE], dx
    push di
    push cx
    W16VOF ax, es
    add ax, cx
    test byte [w16_gflg], GMEM_ZEROINIT
    jz .nz
    call w16_zparas                 ; AX = from, DX - CX paragraphs
.nz:
    pop cx
    pop di
    mov cx, dx
    mov ax, bx                      ; a grow keeps a quarter more than asked
    shr ax, 1                       ;   when the neighbour has it (SPEC.md
    shr ax, 1                       ;   124.9.1's GlobalReAlloc rule): the
    add bx, ax                      ;   next grow by a step is in place, on
    jc .keepall                     ;   no new paragraph
    cmp bx, cx
    jb .shrinkto
.keepall:
%ifdef PKG_MEGA
    call w16_phfix                  ; its selector, at its new length
%endif
    mov ax, di
    jmp .out
.shrinkto:
    mov [es:GH_SIZE], bx            ; the surplus goes back, as a shrink
    W16VOF ax, es
    add ax, bx
    push es
    W16WALK 1, es, ax
    sub cx, bx
    mov [es:GH_SIZE], cx
    mov byte [es:GH_FLAGS], GHF_USED
    mov word [es:GH_MAGIC], GH_SIG
    pop es
    call w16_gfreeblk
    jmp short .keepall
.nonext:
    pop es
.move:
    ; elsewhere: a moveable block always may; a fixed one only when asked
    test byte [es:GH_FLAGS], GHF_MOVE
    jnz .canmove
    test byte [w16_gflg], GMEM_MOVEABLE
    jz .fail
.canmove:
    cmp byte [es:GH_LOCK], 0        ; a locked moveable block stays put, as on
    je .unlocked                    ;   Windows, unless it is a fixed one moving
    test byte [es:GH_FLAGS], GHF_MOVE
%ifndef PKG_MEGA
    jnz .fail
%else
    jz .unlocked
    cmp byte [w16_pmonly], 0        ; ...or a protected-mode-only program's,
    je .fail                        ;   whose selector follows it (w16_pblk,
    test byte [es:GH_FLAGS], GHF_HND ;   w16s_gwin; SPEC.md 124.31.3)
    jz .fail
%endif
.unlocked:
%ifdef PKG_MEGA
    W16VOF ax, es
    mov [w16_gold], ax
%else
    mov [w16_gold], es
%endif
    mov ax, [w16_gnp]
    dec ax
    call w16_p2b
    mov cl, [w16_gflg]
    and cl, GMEM_ZEROINIT           ; a FIXED block for the bytes...
    push si
    call w16_gnew                   ; AX = DX = its segment
    pop si
    or ax, ax
    jz .fail
    push si
    push di
    push ds
    W16SEG es, dx                      ; ...the old data copied in
    mov ax, [w16_gold]
    push es
    W16SEG es, ax
    mov cx, [es:GH_SIZE]
    pop es
    dec cx                          ; paragraphs to copy: the old data
    inc ax
    mov bx, [w16_gnp]
    dec bx
    cmp cx, bx
    jbe .cp
    mov cx, bx
.cp:
    call w16_pcopyseg               ; AX -> ES, CX paragraphs
    pop ds
    pop di
    pop si
    ; the new block takes over the old one's identity
    mov ax, [w16_gold]
    push es
    W16SEG es, ax
    mov bl, [es:GH_FLAGS]
    mov bh, [es:GH_LOCK]
    mov cx, [es:GH_OWNER]
    pop es
    W16VOF ax, es
    dec ax
    push es
    W16SEG es, ax                      ; ES = the new header
    mov [es:GH_FLAGS], bl
    mov [es:GH_LOCK], bh
    mov [es:GH_OWNER], cx
    pop es
%ifdef PKG_MEGA
    SCALL w16s_gwin                 ; ...and its handle descriptor, if any
%endif
    mov ax, [w16_gold]
    call w16_gfreeblk
    W16VOF ax, es                      ; the data
    cmp si, 0xFFFF
    je .newfixed
    mov [w16_ghseg + si], ax        ; moveable: the same handle, a new block
    push es
    dec ax
    W16SEG es, ax
    mov [es:GH_HANDLE], di
    pop es
    mov ax, di
    jmp short .out
.newfixed:
    push es                         ; fixed: the answer is its new segment
    mov dx, ax
    dec ax
    W16SEG es, ax
    mov [es:GH_HANDLE], dx
    pop es
    mov ax, dx
    jmp short .out
.fail:
    mov cl, 6                       ; the DGROUP is a block too (124.9.2)
    call w16_dgx
    jmp short .dx
.out:
    W16POUT
.dx:
    xor dx, dx
    pop di
    pop si
    ret

; w16_dgx - the Global calls' last resort: CL = 0 GlobalHandle, 2 GlobalLock,
; 4 GlobalSize, 6 GlobalReAlloc (w16s_dgtab). A handle that is the running
; program's DGROUP, as the program passed it, is answered by the
; system-library part (SPEC.md 124.9.2), with ES:BX at that argument; any
; other is 0 here, and nothing is fetched for it. A DGROUP whose part cannot
; be read ends the program saying so, as any of the part's imports does
w16_dgx:
    call w16_args
    xor ch, ch
    cmp cl, 6                       ; GlobalReAlloc's handle is at +6
    jne .h
    add bx, cx
.h:
    mov ax, [es:bx]
    cmp ax, [w16_dgroup]
    jne .z
    add cx, w16s_dgtab              ; XSEC-OK: [w16_sfn]'s value, an SCALL's
    mov [w16_sfn], cx
    call w16_scall
    jnc .o
    jmp w16_dispatch.nolib          ; (AX = DX = 0, the program ended)
.z:
    xor ax, ax
    cwd
.o:
    ret

; w16_gsteal - AX = a fresh moveable handle: its block goes to entry SI, and
; the fresh handle's own entry is freed. Keeps all.
w16_gsteal:
    push ax
    push bx
    push es
    mov bx, ax
    sub bx, W16_GH0
    mov ax, [w16_ghseg + bx]
    mov byte [w16_ghf + bx], 0
    mov word [w16_ghseg + bx], 0
    mov [w16_ghseg + si], ax
    dec ax
    W16SEG es, ax
    mov ax, si
    add ax, W16_GH0
    mov [es:GH_HANDLE], ax
    pop es
    pop bx
    pop ax
    ret

; w16_zparas - zero DX - CX paragraphs from paragraph AX. Keeps all.
w16_zparas:
    push ax
    push cx
    push dx
    push di
    push es
    sub dx, cx
.c:
    or dx, dx
    jz .d
    W16WALK 1, es, ax
    mov cx, dx
    cmp cx, 0x0FFF
    jbe .l
    mov cx, 0x0FFF
.l:
    sub dx, cx
    add ax, cx
    push ax
    shl cx, 1
    shl cx, 1
    shl cx, 1
    xor di, di
    xor ax, ax
    cld
    rep stosw
    pop ax
    jmp short .c
.d:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; w16_pcopyseg - CX paragraphs from paragraph AX to ES:0 (ES advanced as it
; goes is not kept: ES is restored). Clobbers AX, CX, DS (the caller saves)
w16_pcopyseg:
    push es
    push si
    push di
.c:
    jcxz .d
    W16SEG ds, ax
    push cx
    cmp cx, 0x0FFF
    jbe .l
    mov cx, 0x0FFF
.l:
    push cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    xor si, si
    xor di, di
    cld
    rep movsw
    pop cx
    W16VOF si, es
    add si, cx
    W16SEG es, si
    add ax, cx
    mov si, cx
    pop cx
    sub cx, si
    jmp short .c
.d:
    pop di
    pop si
    pop es
    ret

; =============================================================================
; The local heap (SPEC.md 124.9): in the CALLER's DS
; =============================================================================

LH_SIG    equ 0x484C            ; 'LH', at the heap's start
LH_FIRST  equ 2                 ; the first block's offset
LH_END    equ 4                 ; the heap's end, exclusive
LH_HDR    equ 6

LB_USED   equ 1                 ; a block's size word: bytes, a multiple of 4,
LB_ENTRY  equ 2                 ;   the header word included; a moveable
                                ;   handle's entry block
LMEM_MOVEABLE equ 0x0002
LMEM_ZEROINIT equ 0x0040
LMEM_MODIFY   equ 0x0080

; w16_lds - ES = the caller's DS, read off the gate's frame. Keeps the rest.
w16_lds:
    push bx
    mov es, [w16_pss]
    mov bx, [w16_psp_]
    mov es, [es:bx + W16F_DS]
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16_linit - AX = the segment, BX = start, DX = end (exclusive): a local heap
; there, its offset at seg:0006. CF=1 too small. Keeps all.
; -----------------------------------------------------------------------------
w16_linit:
    push ax
    push bx
    push cx
    push es
    mov es, ax
    add bx, 3                       ; the descriptor, on a multiple of 4
    and bl, 0xFC
    mov ax, bx
    add ax, LH_HDR + 3
    and al, 0xFC                    ; the first block
    and dl, 0xFC
    mov cx, dx
    sub cx, ax
    jbe .small
    cmp cx, 8
    jb .small
    mov word [es:bx], LH_SIG
    mov [es:bx + LH_FIRST], ax
    mov [es:bx + LH_END], dx
    mov [es:6], bx
    mov bx, ax
    mov [es:bx], cx                 ; one free block, the whole heap
    clc
    jmp short .out
.small:
    stc
.out:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; w16_lheap - ES = a segment: BX = its heap's descriptor; CF=1 it has none.
; Keeps the rest.
w16_lheap:
    mov bx, [es:6]
    or bx, bx
    jz .no
    cmp word [es:bx], LH_SIG
    jne .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; w16_lnew - ES = the heap's segment, CX = bytes, AL = LMEM_* -> AX = the
; data's offset (fixed), 0 = none. Moveable is the caller's (w16k_lalloc).
; Keeps all but AX.
; -----------------------------------------------------------------------------
w16_lnew:
    push bx
    push cx
    push dx
    push si
    push di
    mov [w16_lflg], al
    call w16_lheap
    jc .fail
    mov dx, [es:bx + LH_END]
    mov si, [es:bx + LH_FIRST]
    add cx, 2 + 3                   ; the header, rounded to 4
    jc .fail
    and cl, 0xFC
    cmp cx, 4
    jae .walk
    mov cx, 4
.walk:
    cmp si, dx
    jae .fail
    mov ax, [es:si]
    and ax, 0xFFFC
    jz .fail                        ; a broken heap: stop
    test byte [es:si], LB_USED
    jnz .next
    ; merge the free blocks that follow before judging this one
.merge:
    mov di, si
    add di, ax
    cmp di, dx
    jae .judge
    test byte [es:di], LB_USED
    jnz .judge
    mov di, [es:di]
    and di, 0xFFFC
    jz .judge
    add ax, di
    mov [es:si], ax
    jmp short .merge
.judge:
    cmp ax, cx
    jae .take
.next:
    add si, ax
    jmp short .walk
.take:
    sub ax, cx
    cmp ax, 4
    jb .whole
    mov di, si                      ; split: the tail stays free
    add di, cx
    mov [es:di], ax
    mov [es:si], cx
    jmp short .used
.whole:
    add cx, ax
    mov [es:si], cx
.used:
    or byte [es:si], LB_USED
    lea ax, [si + 2]
    test byte [w16_lflg], LMEM_ZEROINIT
    jz .out
    mov di, ax
    mov cx, [es:si]
    and cx, 0xFFFC
    sub cx, 2
    shr cx, 1
    push ax
    xor ax, ax
    cld
    rep stosw
    pop ax
    jmp short .out
.fail:
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_lfree - ES = the heap's segment, AX = a data offset: free that block.
; Keeps all.
w16_lfree:
    push bx
    mov bx, ax
    and byte [es:bx - 2], 0xFC
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16_lfind - ES = the heap's segment, AX = a local handle or pointer ->
; SI = the data's offset, DI = the entry's offset (0 = a fixed block).
; CF=1 not a block of this heap. Keeps AX, BX, CX, DX.
; -----------------------------------------------------------------------------
w16_lfind:
    push bx
    push dx
    call w16_lheap
    jc .no
    mov dx, [es:bx + LH_END]
    mov si, [es:bx + LH_FIRST]
    add si, 2
    cmp ax, si
    jb .no
    cmp ax, dx
    jae .no
    test al, 1
    jnz .no
    mov si, ax
    test byte [es:si - 2], LB_USED
    jz .no
    xor di, di
    test byte [es:si - 2], LB_ENTRY
    jz .yes
    mov di, si                      ; an entry: its first word is the data
    mov si, [es:di]
.yes:
    pop dx
    pop bx
    clc
    ret
.no:
    pop dx
    pop bx
    stc
    ret

; LocalInit (KERNEL.4) - args: +0 pEnd, +2 pStart, +4 hSeg (0 = the caller's
; DS). A start of 0 puts the heap in the segment's LAST pEnd bytes (Wine's
; LocalInit16). AX = 1, or 0
w16k_linit:
    call w16_args
    mov dx, [es:bx]
    mov cx, [es:bx + 2]
    mov ax, [es:bx + 4]
    or ax, ax
    jnz .seg
    push es
    call w16_lds
    mov ax, es
    pop es
.seg:
    jcxz .tail
    mov bx, cx
    jmp short .init
.tail:
    push ax                         ; the segment's size: a global block's, or
    push dx                         ;   DGROUP's
%ifdef PKG_MEGA
    call w16_pin                    ; (the program's selector: 124.31.3)
%endif
    call w16_gfind
    pop dx
    jc .dg
    mov bx, [es:GH_SIZE]
    dec bx
    mov cl, 4
    shl bx, cl
    pop ax                          ; a library's DGROUP: its heap takes the
    push ax                         ;   room the load gave it too, as a heap
    call w16_islibdg                ;   Windows grew would have (124.25.2)
    jc .sized
    add dx, W16_LHROOM
    jmp short .sized
.dg:
    mov bx, [w16_dgsize]
.sized:
    pop ax
    or bx, bx
    jnz .nz
    mov bx, 0xFFF0                  ; 64KB
.nz:
    mov cx, bx                      ; start = the size - pEnd, end = the size
    sub cx, dx
    jae .st
    xor cx, cx
.st:
    mov dx, bx
    mov bx, cx
.init:
    call w16_linit
    mov ax, 0
    jc .out
    inc ax
.out:
    ret

; w16_islibdg - AX = a segment: CF=0 it is a loaded library's DGROUP
; (SPEC.md 124.23.2). Keeps all.
w16_islibdg:
    push bx
    mov bx, w16_libs
.r:
    cmp byte [bx + W16L_COUNT], 0
    je .n
    cmp ax, [bx + W16L_HINST]
    je .yes
.n:
    add bx, W16L_SIZE
    cmp bx, w16_libs + W16_NLIB * W16L_SIZE
    jb .r
    pop bx
    stc
    ret
.yes:
    pop bx
    clc
    ret

; LocalAlloc (KERNEL.5) - args: +0 wBytes, +2 wFlags
w16k_lalloc:
    push si
    push di
    call w16_args
    mov cx, [es:bx]
    mov al, [es:bx + 2]
    call w16_lds
    call w16_lnew
    or ax, ax
    jz .out
    test byte [w16_lflg], LMEM_MOVEABLE
    jz .out
    mov si, ax                      ; moveable: an entry pointing at it
    mov cx, 2
    xor al, al
    call w16_lnew
    or ax, ax
    jnz .entry
    mov ax, si
    call w16_lfree
    xor ax, ax
    jmp short .out
.entry:
    mov di, ax
    mov [es:di], si
    or byte [es:di - 2], LB_ENTRY
.out:
    pop di
    pop si
    ret

; LocalFree (KERNEL.7) - args: +0 hMem. AX = 0 freed, else the handle
w16k_lfree:
    push si
    push di
    call w16_args
    mov ax, [es:bx]
    call w16_lds
    call w16_lfind
    jc .out
    push ax
    mov ax, si
    call w16_lfree
    or di, di
    jz .done
    mov ax, di
    call w16_lfree
.done:
    pop ax
    xor ax, ax
.out:
    pop di
    pop si
    ret

; LocalLock (KERNEL.8) - args: +0 hMem. AX = the data, DX = the caller's DS
; (Wine's answers a far pointer), 0 if not ours
w16k_llock:
    push si
    push di
    call w16_args
    mov ax, [es:bx]
    call w16_lds
    call w16_lfind
    mov ax, 0
    jc .out
    mov ax, si
.out:
    mov dx, es
    pop di
    pop si
    ret

; LocalUnlock (KERNEL.9) - args: +0 hMem. AX = 0: nothing is ever held
w16k_lunlock:
    xor ax, ax
    ret

; LocalSize (KERNEL.10) - args: +0 hMem. AX = its usable bytes
w16k_lsize:
    push si
    push di
    call w16_args
    mov ax, [es:bx]
    call w16_lds
    call w16_lfind
    mov ax, 0
    jc .out
    mov ax, [es:si - 2]
    and ax, 0xFFFC
    sub ax, 2
.out:
    pop di
    pop si
    ret

; LocalHandle (KERNEL.11) - args: +0 wMem. The pointer is its own handle
; unless an entry names it; AX = that
w16k_lhandle:
    call w16_args
    mov ax, [es:bx]
    ret

; LocalCompact (KERNEL.13) - args: +0 wMinFree. AX = the largest free block
w16k_lcompact:
    push si
    call w16_lds
    xor ax, ax
    call w16_lheap
    jc .out
    mov dx, [es:bx + LH_END]
    mov si, [es:bx + LH_FIRST]
.walk:
    cmp si, dx
    jae .out
    mov cx, [es:si]
    and cx, 0xFFFC
    jz .out
    test byte [es:si], LB_USED
    jnz .next
    cmp cx, ax
    jbe .next
    mov ax, cx
.next:
    add si, cx
    jmp short .walk
.out:
    or ax, ax
    jz .z
    sub ax, 2
.z:
    pop si
    ret

; LocalReAlloc (KERNEL.6) - args: +0 wFlags, +2 wBytes, +4 hMem. AX = the
; handle (a fixed block that moved answers its new pointer), 0 refused
w16k_lrealloc:
    push si
    push di
    call w16_args
    mov dl, [es:bx]
    mov cx, [es:bx + 2]
    mov ax, [es:bx + 4]
    mov [w16_lflg], dl
    mov [w16_lh], ax
    test dl, LMEM_MODIFY
    jnz .same
    call w16_lds
    call w16_lfind                  ; SI = the data, DI = the entry
    jc .fail
    mov dx, [es:si - 2]
    and dx, 0xFFFC
    sub dx, 2                       ; DX = what it holds now
    cmp cx, dx
    jbe .same                       ; shrinking keeps the block as it is
    or di, di
    jnz .move
    test byte [w16_lflg], LMEM_MOVEABLE
    jz .fail
.move:
    push dx
    mov al, [w16_lflg]
    and al, LMEM_ZEROINIT
    call w16_lnew                   ; AX = the new data
    pop dx
    or ax, ax
    jz .fail
    push di
    push si
    mov di, ax                      ; copy what it held
    mov cx, dx
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
    pop si
    pop di
    push ax
    mov ax, si
    call w16_lfree
    pop ax
    or di, di
    jz .out                         ; fixed: the new pointer
    mov [es:di], ax                 ; moveable: the same entry, a new block
.same:
    mov ax, [w16_lh]
    jmp short .out
.fail:
    xor ax, ax
.out:
    pop di
    pop si
    ret

