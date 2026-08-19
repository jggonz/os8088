; =============================================================================
; os8088 - RAMDISK.DRV / rdstore.inc
;
; The store (SPEC.md 62.9.10): where the bytes live, how an extent is reached,
; and how a file's chain is walked.
;
; ONE ROUTINE IS THE WHOLE ADDRESSING ABSTRACTION AND IT IS `rd_ext_seg`: give
; me a segment through which extent i can be read and written. Conventional,
; that is arithmetic on the claim's base and `rd_ext_flush` is a `ret`;
; extended, it is a bounce the extent is copied down into and back out of.
; Every verb above this file works on "a segment holding extent i" and none of
; them knows which of the two it got.
;
; THREE THINGS SCALE WITH THE STORE and are computed once when it is claimed:
; the extent SIZE (the smallest power of two that keeps the chain inside
; RD_MAXEXT entries), the extent COUNT, and the chain table's own claim. A
; byte-per-extent array in this image is what used to cap the disk at 224KB.
;
; Every OSAPI_XMEM_COPY here moves ONE EXTENT at an extent boundary - even,
; aligned, and at most RD_EXTMAXKB - so not one of SPEC.md 41.5's three
; refusals is reachable and there is no ragged-end case to get wrong.
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1), so every string
; instruction below loads its own segments and puts them back.
; =============================================================================

; -----------------------------------------------------------------------------
; rd_geom - the extent size, count and table size for [rd_kb]
; in:  [rd_kb]
; out: [rd_extkb], [rd_extb], [rd_extpara], [rd_next], [rd_ctabkb]
; clobbers: flags
;
; THE SMALLEST EXTENT THAT FITS, doubling until the count is inside the table -
; SPEC.md 52.3's capacity table with the rule written as a loop rather than as
; rows, because here there is exactly one input.
;
; [rd_extpara] is a word and stays one: the largest extent is 8KB = 512
; paragraphs. [rd_next] is a word and stays one: RD_MAXEXT is 2,048.
; -----------------------------------------------------------------------------
rd_geom:
    push ax
    push bx
    push cx
    push dx
    mov bx, RD_EXTMINKB
.try:
    mov ax, [rd_kb]
    xor dx, dx
    div bx                      ; AX = how many extents that would be
    cmp ax, RD_MAXEXT
    jbe .got
    shl bx, 1
    cmp bx, RD_EXTMAXKB
    jbe .try
    mov bx, RD_EXTMAXKB         ; a size past RD_MAXKB cannot reach here -
    mov ax, [rd_kb]             ; rd_kb_clamp refuses it - and clamping rather
    xor dx, dx                  ; than looping for ever is what makes that a
    div bx                      ; statement about the clamp instead of a
.got:                           ; promise about every caller
    mov [rd_next], ax
    mov [rd_extkb], bx
    mov ax, bx
    mov cl, 10
    shl ax, cl                  ; bytes: 8KB max, so a word holds it
    mov [rd_extb], ax
    mov cl, 4
    shr ax, cl
    mov [rd_extpara], ax
    mov ax, [rd_next]           ; the table: two bytes an entry, rounded to KB
    shl ax, 1
    add ax, 1023
    mov cl, 10
    shr ax, cl
    mov [rd_ctabkb], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_store_get - the store, at the configured size and in the configured place
; in:  nothing ([rd_kb] and [rd_loc] are the settings)
; out: CF = 0 there is a store; CF = 1 and AL = RDERR_MEM
; clobbers: flags
;
; A REFUSED STORE IS A NORMAL PATH (SPEC.md 50.3): the caller is Mount, and
; Mount says so in the pane. It is claimed HERE and not at attach, because
; attach is documented to hook nothing and a refusal there would have to
; un-hook an attach that could not fail.
;
; THE CHAIN TABLE IS CLAIMED FIRST AND FREED LAST, whichever store it belongs
; to: a store with no table is not reachable, and a table with no store is
; 4KB held for nothing.
; -----------------------------------------------------------------------------
rd_store_get:
    cmp byte [rd_have], 0
    je .claim
    clc
    ret
.claim:
    push bx
    push cx
    push dx
    push si
    call rd_geom
    mov ax, [rd_ctabkb]
    call OSAPI_MEM_CLAIM
    jc .no
    mov [rd_ctab], dx
    mov ax, rd_reloc
    call OSAPI_MEM_MOVABLE
    cmp byte [rd_loc], RDL_XMS
    je .xms
    mov ax, [rd_kb]
    call OSAPI_MEM_CLAIM        ; owner = our segment, so drv_release sweeps it
    jc .notab                   ; even if detach somehow does not
    mov [rd_arena], dx
    mov ax, rd_reloc            ; ...and it MOVES (SPEC.md 66.5.10): the largest
    call OSAPI_MEM_MOVABLE      ; claim this driver makes, and live for as long
    jmp short .ok               ; as the volume is mounted
.xms:
    mov ax, [rd_extkb]          ; the bounce is ONE EXTENT, so it follows the
    cmp ax, RDI_META / 1024     ; store's granule rather than being a constant
    jae .bsz                    ; that could stop matching it - but never less
    mov ax, RDI_META / 1024     ; than RDI_META, because rd_wbuf_get LENDS it
.bsz:                           ; as the image block (SPEC.md 62.9.12) and a
    call OSAPI_MEM_CLAIM        ; 1KB extent would take 3KB of the next claim
    jc .notab                   ; with it
    mov [rd_bounce], dx
    mov ax, rd_reloc
    call OSAPI_MEM_MOVABLE
    mov ax, [rd_kb]             ; DX:AX = KB * 1024, in 32 bits: 16MB is far
    mov dx, ax                  ; past a word and the shift below is the whole
    mov cl, 10                  ; of the arithmetic
    shl ax, cl
    mov cl, 6
    shr dx, cl
    call OSAPI_XMEM_ALLOC       ; stamped 0xFF - the kernel - because
    jc .noxms                   ; drv_cp_call clears the dispatch stamp
    mov [rd_xbase], ax          ; (SPEC.md 51.2.3), and nothing owns 0xFF, so
    mov [rd_xbase+2], dx        ; our own detach is what frees it
.ok:
    mov byte [rd_have], 1
    mov word [rd_sext], RD_NOEXT
    call rd_ext_clear
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.noxms:
    mov dx, [rd_bounce]         ; the bounce goes back with it, or a machine
    call OSAPI_MEM_FREE         ; that could not fund the store keeps it for
    mov word [rd_bounce], 0     ; nothing
.notab:
    mov dx, [rd_ctab]
    call OSAPI_MEM_FREE
    mov word [rd_ctab], 0
.no:
    pop si
    pop dx
    pop cx
    pop bx
    mov al, RDERR_MEM
    stc
    ret

; -----------------------------------------------------------------------------
; rd_store_free - give it all back
; in:  nothing
; out: nothing
; clobbers: flags
; -----------------------------------------------------------------------------
rd_store_free:
    push ax
    push cx
    push dx
    cmp byte [rd_have], 0
    je .out
    cmp byte [rd_loc], RDL_XMS
    je .xms
    cmp word [rd_arena], 0
    je .done
    mov dx, [rd_arena]
    call OSAPI_MEM_FREE
    jmp short .done
.xms:
    mov ax, [rd_xbase]
    mov dx, [rd_xbase+2]
    mov cx, ax
    or cx, dx
    jz .bounce
    call OSAPI_XMEM_FREE
.bounce:
    cmp word [rd_bounce], 0
    je .done
    mov dx, [rd_bounce]
    call OSAPI_MEM_FREE
.done:
    cmp word [rd_ctab], 0       ; the table LAST, for rd_store_get's reason in
    je .zero                    ; the other direction
    mov dx, [rd_ctab]
    call OSAPI_MEM_FREE
.zero:
    mov word [rd_arena], 0
    mov word [rd_bounce], 0
    mov word [rd_ctab], 0
    mov word [rd_xbase], 0
    mov word [rd_xbase+2], 0
    mov byte [rd_have], 0
    mov word [rd_sext], RD_NOEXT
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_reloc - the compactor moved one of our claims (SPEC.md 66.5.10)
; in:  BX = the base it WAS at, DX = the base it is at NOW, DS = CS = ours
; out: nothing; preserves every register
;
; THREE words, because there are three claims that can move - the arena, the
; chain table and the extended path's bounce - and the compactor names one of
; them per call. A handle here is an extent NUMBER, never a segment, and
; rd_ext_seg derives the segment on every use from the word below it, so there
; is nothing derived anywhere to fall out of step.
;
; It has no worker, so it needs no OSAPI_TASK_PARK of its own - but SPEC.md
; 66.5.5's rule still binds it, that rule being all-or-nothing across every
; TF_SERVICE task on the machine.
; -----------------------------------------------------------------------------
rd_reloc:
    cmp bx, [rd_arena]
    jne .tab
    mov [rd_arena], dx
    ret
.tab:
    cmp bx, [rd_ctab]
    jne .bounce
    mov [rd_ctab], dx
    ret
.bounce:
    cmp bx, [rd_bounce]
    jne .out
    mov [rd_bounce], dx
.out:
    ret

; =============================================================================
; THE CHAIN TABLE
;
; A WORD PER EXTENT IN A CLAIM, so the whole of what the kernel's own FAT
; window is to a floppy this is to the store - and for the same reason, that a
; table sized for the largest volume cannot live in the image.
;
; rd_cget and rd_cput are the ONLY readers and writers. Every other routine
; here names an extent by NUMBER, which is what makes a moved claim one word
; to fix rather than a sweep.
; =============================================================================

; rd_cget - BX = an extent index -> AX = its chain cell
; clobbers: AX (the output), flags
rd_cget:
    push es
    push bx
    cmp bx, [rd_next]
    jae .bad                    ; out of range reads as the END of a chain, so
                                ; a table that came off a DISK cannot walk this
                                ; driver past its own store (SPEC.md 62.9.12)
    shl bx, 1
    mov es, [rd_ctab]
    mov ax, [es:bx]
    pop bx
    pop es
    ret
.bad:
    mov ax, RD_CEND
    pop bx
    pop es
    ret

; rd_cput - BX = an extent index, AX = the cell to store
; clobbers: flags
rd_cput:
    push es
    push bx
    cmp bx, [rd_next]
    jae .out
    shl bx, 1
    mov es, [rd_ctab]
    mov [es:bx], ax
.out:
    pop bx
    pop es
    ret

; rd_ext_clear - every extent free
rd_ext_clear:
    push ax
    push bx
    push cx
    xor bx, bx
    mov cx, [rd_next]
    jcxz .out
.next:
    mov ax, RD_CFREE
    call rd_cput
    inc bx
    loop .next
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_ext_seg - a segment through which extent AX can be read and written
; in:  AX = an extent index
; out: AX = the segment; offset 0 is the extent's first byte
; clobbers: AX (the output), flags
;
; On the extended path this STAGES: the extent is copied down into the bounce
; and the one already there is written back first. Asking for the extent that
; is already staged costs a compare, which is what makes a sequential walk one
; copy each way per extent rather than per chunk.
; -----------------------------------------------------------------------------
rd_ext_seg:
    cmp byte [rd_loc], RDL_XMS
    je .xms
    push cx
    push dx
    mul word [rd_extpara]       ; DX:AX, and the product is bounded by the
    add ax, [rd_arena]          ; conventional heap - a store big enough to
    pop dx                      ; carry DX is one no 640KB machine can claim
    pop cx
    ret
.xms:
    cmp ax, [rd_sext]
    je .have
    push ax
    call rd_stage_out           ; the one that is there, back where it lives
    pop ax
    mov [rd_sext], ax
    call rd_stage_in
.have:
    mov ax, [rd_bounce]
    ret

; -----------------------------------------------------------------------------
; rd_ext_flush - the caller has written through rd_ext_seg's answer
; in:  nothing (the staged extent is the one that moved)
; out: nothing
; clobbers: flags
;
; A `ret` on the conventional path, where the caller wrote the store itself.
; -----------------------------------------------------------------------------
rd_ext_flush:
    cmp byte [rd_loc], RDL_XMS
    jne .out
    call rd_stage_out
.out:
    ret

; -----------------------------------------------------------------------------
; rd_stage_in / rd_stage_out - one extent between the bounce and the store
; in:  [rd_sext] = which extent
; out: nothing; [rd_xerr] is raised if the copy REFUSED
; clobbers: flags
;
; The linear address is base + extent * [rd_extb], in 32 bits: 2,048 extents
; of 8KB is 16MB, so the product genuinely needs both halves and `mul` is what
; produces them.
;
; **A REFUSED COPY IS RECORDED AND NEVER SWALLOWED** (SPEC.md 62.9.10.2). It
; used to be, and that was the whole of the corruption: OSAPI_XMEM_COPY fenced
; the block against the CALLER, drv_fs_call did not clear the dispatch stamp,
; so every stage a file operation asked for was refused with CF = 1 - and both
; of these threw the answer away. The stage-in left the bounce holding the
; PREVIOUS extent, the caller wrote its piece into that, and the stage-out
; wrote nothing at all. A file copied onto an extended-memory volume came back
; as somebody else's bytes, with the directory perfectly correct about its name
; and its size. The fence is fixed at the kernel end; this is the half that
; makes the next such refusal LOUD instead of silent.
; -----------------------------------------------------------------------------
rd_stage_addr:                  ; out DX:AX = the staged extent's linear end
    mov ax, [rd_sext]
    mul word [rd_extb]          ; DX:AX = extent * bytes
    add ax, [rd_xbase]
    adc dx, [rd_xbase+2]
    ret

rd_stage_in:
    cmp word [rd_sext], RD_NOEXT
    je .out
    push ax
    push cx
    push dx
    push si
    push di
    push es
    call rd_stage_addr
    mov es, [rd_bounce]
    xor si, si
    mov cx, [rd_extb]
    mov di, 1                   ; in from extended
    call OSAPI_XMEM_COPY
    jnc .ok
    mov byte [rd_xerr], 1
.ok:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.out:
    ret

rd_stage_out:
    cmp word [rd_sext], RD_NOEXT
    je .out
    push ax
    push cx
    push dx
    push si
    push di
    push es
    call rd_stage_addr
    mov es, [rd_bounce]
    xor si, si
    mov cx, [rd_extb]
    xor di, di                  ; out to extended
    call OSAPI_XMEM_COPY
    jnc .ok
    mov byte [rd_xerr], 1
.ok:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; rd_ext_alloc - one free extent, marked as the end of a chain
; out: CF = 0 and AX = it; CF = 1 = the store is full
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
rd_ext_alloc:
    push bx
    push cx
    xor bx, bx
    mov cx, [rd_next]
    jcxz .full
.scan:
    call rd_cget
    cmp ax, RD_CFREE
    je .got
    inc bx
    loop .scan
.full:
    pop cx
    pop bx
    stc
    ret
.got:
    mov ax, RD_CEND
    call rd_cput
    mov ax, bx
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; rd_chain_free - release a whole chain
; in:  AX = its first extent (RD_NOEXT = nothing to do)
; out: nothing
; clobbers: flags
;
; BOUNDED BY THE EXTENT COUNT rather than by reaching the end marker: a chain
; is a linked list this driver alone writes, but a LOADED image's table came
; off a disk (SPEC.md 62.9.12) and every byte read off one is treated as
; hostile. A cycle would otherwise be an unbounded loop under the gfx lock.
; -----------------------------------------------------------------------------
rd_chain_free:
    push ax
    push bx
    push cx
    push dx
    mov cx, RD_MAXEXT
    mov dx, ax
.next:
    cmp dx, RD_NOEXT
    je .out
    cmp dx, [rd_next]
    jae .out
    mov bx, dx
    call rd_cget                ; the link, before the cell is released
    mov dx, ax
    push ax
    mov ax, RD_CFREE
    call rd_cput
    pop ax
    cmp dx, RD_CEND
    je .out
    dec dx                      ; a cell is `next + 1`
    loop .next
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_chain_fit - make SI's chain exactly long enough for DX:CX bytes
; in:  SI = the row, DX:CX = the byte count it must hold
; out: CF = 0 done; CF = 1 = the store is full and NOTHING was changed
; clobbers: flags
;
; ALL-OR-NOTHING, which is what lets a refused write leave the file it was
; replacing intact: the extents it needs are counted and taken first, and a
; shortfall gives every one of them straight back before returning.
; -----------------------------------------------------------------------------
rd_chain_fit:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, cx                  ; how many extents DX:CX bytes want, rounded up
    mov bx, dx                  ; - a 32-bit divide by the extent size, which
    add ax, [rd_extb]           ; on this CPU is the two-step `div` below
    adc bx, 0
    sub ax, 1
    sbb bx, 0
    mov dx, bx                  ; DX:AX = bytes + extb - 1
    div word [rd_extb]          ; ...and DX:AX / extb, whose quotient is at
                                ; most RD_MAXEXT and so cannot overflow
    mov [rd_want], ax
    mov ax, [si+RDE_EXT]        ; ...and how many it has
    call rd_chain_len
    mov [rd_hasn], ax
    mov ax, [rd_want]
    cmp ax, [rd_hasn]
    je .done
    jb .shrink
    ; --- grow: take them all before linking any of them ----------------------
    sub ax, [rd_hasn]
    mov cx, ax
    mov word [rd_head], RD_NOEXT
.take:
    call rd_ext_alloc
    jc .undo
    mov bx, ax                  ; push it on the front of a private list
    mov ax, [rd_head]
    cmp ax, RD_NOEXT
    je .first                   ; the FIRST one taken ends that list - and
    inc ax                      ; RD_NOEXT and RD_CEND are the same value, so
.first:                         ; storing it unchanged is exactly that, which
    call rd_cput                ; is why this needs no case of its own
    mov [rd_head], bx
    loop .take
    mov ax, [rd_head]           ; ...and now it cannot fail, so link it on
    cmp word [rd_hasn], 0
    jne .append
    mov [si+RDE_EXT], ax        ; the file had no store at all
    jmp short .done
.append:
    push ax
    mov ax, [si+RDE_EXT]
    call rd_chain_last          ; AX = the last extent of what it already has
    mov bx, ax
    pop ax
    inc ax                      ; `next + 1`
    call rd_cput
    jmp short .done
.undo:
    mov ax, [rd_head]           ; give back whatever this call had taken -
    call rd_chain_free          ; rd_chain_free walks the same links
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.shrink:
    mov ax, [rd_want]
    or ax, ax
    jz .cutall
    mov ax, [si+RDE_EXT]
    mov cx, [rd_want]
    call rd_chain_nth           ; AX = the last extent to KEEP
    mov bx, ax
    call rd_cget                ; ...the rest of the chain goes
    push ax
    mov ax, RD_CEND
    call rd_cput
    pop ax
    cmp ax, RD_CEND
    je .done
    dec ax
    call rd_chain_free
    jmp short .done
.cutall:
    mov ax, [si+RDE_EXT]
    call rd_chain_free
    mov word [si+RDE_EXT], RD_NOEXT
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; rd_chain_len - AX = a first extent (or RD_NOEXT) -> AX = how many links
; clobbers: AX (the output), flags
rd_chain_len:
    push bx
    push cx
    push dx
    xor dx, dx
    mov cx, RD_MAXEXT
.next:
    cmp ax, RD_NOEXT
    je .out
    cmp ax, [rd_next]
    jae .out
    inc dx
    mov bx, ax
    call rd_cget
    cmp ax, RD_CEND
    je .out
    dec ax
    loop .next
.out:
    mov ax, dx
    pop dx
    pop cx
    pop bx
    ret

; rd_chain_last - AX = a first extent -> AX = the last one in that chain
; clobbers: AX (the output), flags
rd_chain_last:
    push bx
    push cx
    push dx
    mov cx, RD_MAXEXT
.next:
    cmp ax, [rd_next]
    jae .out
    mov dx, ax
    mov bx, ax
    call rd_cget
    cmp ax, RD_CEND
    je .last
    dec ax
    loop .next
.last:
    mov ax, dx
.out:
    pop dx
    pop cx
    pop bx
    ret

; rd_chain_nth - AX = a first extent, CX = an ordinal (1-based) -> AX = that
;                link, or the last one if the chain is shorter
; clobbers: AX (the output), flags
rd_chain_nth:
    push bx
    push cx
    push dx
    dec cx
    jcxz .out
.next:
    cmp ax, [rd_next]
    jae .out
    mov dx, ax
    mov bx, ax
    call rd_cget
    cmp ax, RD_CEND
    je .last
    dec ax
    loop .next
    jmp short .out
.last:
    mov ax, dx
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_io - move bytes between a file's chain and the caller's buffer
; in:  [rd_io_dir] = 0 store -> caller, 1 caller -> store
;      [rd_io_ext] = the file's FIRST extent
;      [rd_io_off] = a 32-BIT byte offset within the file
;      [rd_io_len] = a 32-BIT count (the caller has bounds-checked it)
;      [rd_io_seg]:[rd_io_ofs] = the caller's buffer
;      [rd_io_prog] = 1 to report each piece through OSAPI_FS_PROG
; out: nothing
; clobbers: flags
;
; **IT TAKES ITS ARGUMENTS IN MEMORY AND NOT IN REGISTERS**, which is unusual
; here and is the honest shape for this one: two 32-bit quantities, a far
; pointer, a first extent and two flags is nine words of argument, and threading
; them through an 8086's registers costs every caller more than it saves.
;
; THE BUFFER IS NORMALISED AFTER EVERY PIECE, dskw_norm's arithmetic (SPEC.md
; 18.4.1) applied per step rather than once. The fold up front is what makes it
; SAFE - an offset of 0..15 plus RD_FILEMAX cannot reach 65,536, so no transfer
; this store can be asked for wraps its own segment - and the per-piece fold is
; what keeps that true if RD_FILEMAX ever moves, which it can once the offsets
; inside a file stop being words. A wrap here would be a copy that lands at the
; START of the caller's segment and reports success.
; -----------------------------------------------------------------------------
rd_io:
    mov byte [rd_xerr], 0       ; ONE operation, one verdict: every caller
    push ax                     ; tests it afterwards (SPEC.md 62.9.10.2)
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, [rd_io_ofs]         ; fold the paragraphs into the segment once up
    mov cl, 4                   ; front, so the per-piece fold below only ever
    shr ax, cl                  ; has one extent's worth to carry
    add [rd_io_seg], ax
    and word [rd_io_ofs], 15

    mov ax, [rd_io_off]         ; walk to the extent the offset is inside:
    mov dx, [rd_io_off+2]       ; DX:AX / extb, and the remainder is where in
    div word [rd_extb] ; that extent it starts
    mov [rd_io_o], dx
    mov cx, ax
    jcxz .walked
    inc cx                      ; rd_chain_nth is 1-based
    mov ax, [rd_io_ext]
    call rd_chain_nth
    mov [rd_io_ext], ax
.walked:
.chunk:
    mov ax, [rd_io_len]         ; anything left?
    or ax, [rd_io_len+2]
    jz .fin
    cmp word [rd_io_ext], RD_NOEXT
    jne .go                     ; the chain ran out: the caller asked for more
.fin:                           ; than the file has, which its own bounds test
    jmp .done                   ; refuses - this is the belt
.go:
    mov ax, [rd_extb]  ; what is left of THIS extent...
    sub ax, [rd_io_o]
    mov cx, ax
    cmp word [rd_io_len+2], 0   ; ...against what is left of the request, which
    jne .have                   ; only needs comparing when it fits a word
    cmp [rd_io_len], cx
    jae .have
    mov cx, [rd_io_len]
.have:
    mov [rd_io_n], cx
    mov ax, [rd_io_ext]
    call rd_ext_seg             ; AX = a segment holding this extent
    push ds
    push es
    mov si, [rd_io_o]
    mov di, [rd_io_ofs]
    mov cx, [rd_io_n]
    cmp byte [rd_io_dir], 0
    jne .write
    mov es, [rd_io_seg]         ; read: the store is the SOURCE
    mov ds, ax
    cld
    rep movsb
    pop es
    pop ds
    jmp short .moved
.write:
    mov es, ax                  ; ...and write is the same copy the other way
    xchg si, di
    mov ds, [rd_io_seg]
    cld
    rep movsb
    pop es
    pop ds
    call rd_ext_flush
.moved:
    mov ax, [rd_io_n]
    add [rd_io_ofs], ax         ; ...and normalise, because a big file walks
    mov ax, [rd_io_ofs]         ; off the end of this segment and into the next
    mov cl, 4
    shr ax, cl
    add [rd_io_seg], ax
    and word [rd_io_ofs], 15

    mov ax, [rd_io_n]
    sub [rd_io_len], ax
    sbb word [rd_io_len+2], 0
    mov word [rd_io_o], 0       ; every extent after the first starts at 0
    mov ax, [rd_io_ext]         ; ...and follow the link
    mov bx, ax
    call rd_cget
    cmp ax, RD_CEND
    je .last
    dec ax
    mov [rd_io_ext], ax
    jmp short .prog
.last:
    mov word [rd_io_ext], RD_NOEXT
.prog:
    cmp byte [rd_io_prog], 0
    je .chunk
    mov ax, [rd_io_n]
    call OSAPI_FS_PROG          ; AX = bytes SINCE THE LAST REPORT, never a
    jmp .chunk                  ; running total (SPEC.md 12.8.1)
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_free_ext - how many extents are free
; out: AX = the count
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
rd_free_ext:
    push bx
    push cx
    push dx
    xor dx, dx
    xor bx, bx
    mov cx, [rd_next]
    jcxz .out
.scan:
    call rd_cget
    cmp ax, RD_CFREE
    jne .next
    inc dx
.next:
    inc bx
    loop .scan
.out:
    mov ax, dx
    pop dx
    pop cx
    pop bx
    ret
