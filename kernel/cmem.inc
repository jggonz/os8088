; =============================================================================
; os8088 - kernel/cmem.inc
;
; The arena's SECOND claim map: conventional memory a package can ask for
; and give back (SPEC.md 2.6/20.8). Prefix cm_.
;
; Since format v3 every loaded package owns a segment out of the arena at
; linear 0x65800..the int 12h top (SPEC.md 2.5), and ld_alloc carves those
; regions out of it. A region caps at PKG_MAX_PARA - one segment - and it
; holds the package's image and bss, so an app whose DATA is measured in
; hundreds of KB (a bitmap editor's canvas, a captured sample, a render
; target) has nowhere to put it. Before this module the only answers were
; extended memory, which tier 0 does not have and which every byte has to
; be COPIED through (SPEC.md 41.5), and picking an address out of the map
; by hand - which is not an answer at all, because the arena is already
; there and the two allocators would hand out the same paragraph.
;
; So: the same arena, a second map, one first-fit walk that consults both.
;
;   cm_alloc(paragraphs) -> segment      OSAPI_MEM_ALLOC  slot 0x01B8
;   cm_free(segment)                     OSAPI_MEM_FREE   slot 0x01C0
;   cm_caps -> largest free, total free  OSAPI_MEM_AVAIL  slot 0x01C8
;
; THE SHAPE IS xmem.inc's, DELIBERATELY, down to the pushf/cli around the
; publish and the release-at-teardown pair. Blocks are stamped with the
; calling instance (snd_req_inst, the SPEC.md 34.3 rule) and force-freed by
; the three SPEC.md 29.4 teardown sites, so a package that exits without
; freeing - and a task-less package is never TOLD it is closing (SPEC.md 21)
; - cannot strand a claim for the session. Deliberately few entries: this
; is a bulk store for a handful of large claims, not a malloc. A caller
; that wants many pieces takes one block and subdivides it itself.
;
; WHY PARAGRAPHS AND NOT KB. xmem counts KB because its pool is 64MB and a
; word has to span it. The arena is at most 0xA000 - ARENA_SEG paragraphs,
; so a word spans it exactly, and paragraphs are what ld_alloc, I_SPTR and
; I_SIZE already speak (SPEC.md 29.1). One unit across the whole map is the
; reason cm_claim below can enumerate both tables into the same two words.
;
; WHAT THE ARENA IS NOT. Everything here is bounded by [ld_arena_top], the
; one int 12h probe loader_init makes, and by ARENA_SEG below - so the back
; buffer's pinned 0x40000..0x657FF extent (SPEC.md 32) is outside the map by
; construction and bb_set can arm or disarm underneath any grant. That is
; the whole reason ARENA_SEG is a constant and not a function of bb state.
; =============================================================================

; --- one block-table entry ---------------------------------------------------
CB_SEG      equ 0               ; word: the block's base SEGMENT (= paragraph)
CB_PARA     equ 2               ; word: size in PARAGRAPHS. ZERO MEANS THE
                                ; ENTRY IS FREE - there is no separate flag.
CB_OWN      equ 4               ; byte: the stamping instance (0xFF = kernel)
CM_BLKSZ    equ 8               ; padded to 8 so indexing is a shift

CM_MAX_BLKS equ 8               ; grants that can be live at once
CM_OWN_KERN equ 0xFF            ; a claim made outside any instance. 0xFF and
                                ; not 0, because 0 is a real instance slot -
                                ; snd_req_inst already spells the kernel 0xFF.

; The claim iterator's index space: every instance record first, then every
; block-table entry, and last the LOADER'S HOLD. cm_fit and cm_caps both walk
; it, so the two allocators can never disagree about what is occupied (which
; is the entire point of this module living next to loader.inc rather than
; beside a second arena).
CM_HOLD     equ INST_MAX + CM_MAX_BLKS   ; the hold's claim index
CM_NCLAIM   equ CM_HOLD + 1

; -----------------------------------------------------------------------------
; cm_init - hand-zero the block table (boot thread, before any package runs)
; in:  nothing; out: nothing; clobbers: nothing (flags)
;
; `-f bin` zeroes no .bss (SPEC.md 15.1), and a free entry is CB_PARA = 0, so
; the table has to be written rather than assumed - xm_init's reason exactly.
; -----------------------------------------------------------------------------
cm_init:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov di, cm_tab
    mov cx, (CM_MAX_BLKS * CM_BLKSZ) / 2
    xor ax, ax
    cld
    rep stosw
    mov [cm_hold_para], ax      ; and the loader's hold: no load in flight
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cm_owner - who is asking? (the SPEC.md 34.3 grant-stamping rule, reused)
; in:  nothing
; out: AL = the instance slot to stamp: the dispatched callback's stamp if
;      there is one, else the running task's. CM_OWN_KERN = kernel.
; clobbers: nothing else (flags)
;
; xm_owner byte for byte. A package's entry proc is a dispatched site too
; since the loader brackets it (SPEC.md 21 step 9), which is what lets an
; app size and claim its memory before its first repaint instead of after.
; -----------------------------------------------------------------------------
cm_owner:
    push dx
    call snd_req_inst           ; DH = the requesting instance
    mov al, dh
    pop dx
    ret

; -----------------------------------------------------------------------------
; cm_claim - the SIth arena claim, if it is in use (module-internal)
; in:  SI = claim index, 0 .. CM_NCLAIM-1
; out: CF = 0 and CX = base paragraph, DX = end paragraph (exclusive);
;      CF = 1 the slot claims nothing
; clobbers: CX, DX, flags - AX, BX, SI, DI and the segment registers survive
;
; Indices below INST_MAX are instance records - a package record's
; [I_SPTR, I_SPTR+I_SIZE) is occupied iff I_STATE != 0, which is SPEC.md
; 29.2 rule 7 and the reason the store that frees a record frees its region.
; The rest are block-table entries. Built-ins are filtered on the KIND_PKG
; bit, not on I_SIZE, because a built-in keeps a kernel state POINTER in
; I_SPTR and that pointer is not a paragraph.
;
; No arithmetic here can wrap: the largest possible top is 0xA000 (640KB),
; a region caps at PKG_MAX_PARA and a block cannot be granted past the top,
; so every end stays under 0xB000.
; -----------------------------------------------------------------------------
cm_claim:
    push ax
    push bx
    mov bx, si
    cmp bx, CM_HOLD
    je .hold
    cmp bx, INST_MAX
    jae .block
    mov cl, 5                   ; I_RECSZ = 32: record = inst_tab + idx*32
    shl bx, cl
    add bx, inst_tab
    cmp byte [bx+I_STATE], 0
    je .none
    test byte [bx+I_KIND], KIND_PKG
    jz .none                    ; built-in: I_SPTR is a state ptr, not a seg
    mov cx, [bx+I_SPTR]
    mov dx, cx
    add dx, [bx+I_SIZE]
    jmp short .yes
.block:
    sub bx, INST_MAX
    mov cl, 3                   ; CM_BLKSZ = 8
    shl bx, cl
    add bx, cm_tab
    mov cx, [bx+CB_PARA]
    jcxz .none                  ; a zero-length entry is a free entry
    mov dx, cx
    mov cx, [bx+CB_SEG]
    add dx, cx
    jmp short .yes
.hold:
    mov cx, [cm_hold_para]
    jcxz .none                  ; nothing loading
    mov dx, cx
    mov cx, [cm_hold_seg]
    add dx, cx
.yes:
    pop bx
    pop ax
    clc
    ret
.none:
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; cm_fit - first-fit a run of arena paragraphs against BOTH claim maps
; in:  AX = paragraphs needed (> 0)
; out: CF = 0 and BX = the run's base SEGMENT; CF = 1 no hole fits - which
;      includes immediately, on a zero arena (a 256KB machine's int 12h top
;      sits below ARENA_SEG)
; clobbers: nothing else (AX preserved; flags)
;
; ld_alloc's walk, widened by the block table: start the candidate at
; ARENA_SEG, walk every claim, and each time one overlaps push the candidate
; to that claim's end and rescan from the top. Every bump lands on a claim
; end and claims are finite, so it terminates; a freed block merges with its
; neighbours for free because nothing records the gaps.
;
; UI task only - occupancy is derived from the instance table and the block
; table, and only this task grows either. (A DYING task can shrink both;
; that direction is harmless here, it can only turn a refusal into a hole
; the next caller finds.)
; -----------------------------------------------------------------------------
cm_fit:
    push cx
    push dx
    push si
    push di
    mov bx, ARENA_SEG           ; BX = candidate base (paragraph = segment)
.retry:
    mov di, bx
    add di, ax                  ; DI = candidate end (cannot wrap: see above)
    cmp di, [ld_arena_top]
    ja .full                    ; past the int 12h top - or a zero arena,
                                ; where even the first candidate fails
    xor si, si
.scan:
    call cm_claim               ; CX = claim base, DX = claim end
    jc .next
    cmp cx, di
    jae .next                   ; the claim starts at/past our end
    cmp dx, bx
    jbe .next                   ; the claim ends at/before our start
    mov bx, dx                  ; overlap: restart just past this claim
    jmp short .retry
.next:
    inc si
    cmp si, CM_NCLAIM
    jb .scan
    clc
    jmp short .out
.full:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cm_hold / cm_unhold - reserve the region a package is being LOADED into
; in:  cm_hold: BX = base segment, AX = paragraphs. cm_unhold: nothing
; out: nothing; both preserve EVERY register AND THE FLAGS (MOV only) -
;      cm_unhold sits between inst_cb_call and the `jc` that reads the entry
;      proc's CF, so it must not touch them
;
; THE WINDOW THIS CLOSES. ld_alloc reserves the region at SPEC.md 21 step 5,
; but the instance record is not PUBLISHED until step 10 - after the entry
; proc has run and returned - because a half-built instance must not be
; visible to the dock, the Task Manager or a close. So for the whole of the
; entry proc the package's own region reads as FREE to the walk above, whose
; only evidence is I_STATE.
;
; That was invisible until a package called OSAPI_MEM_ALLOC from its entry -
; and then cm_fit handed back ARENA_SEG, the lowest free paragraph, which is
; the segment the caller is EXECUTING IN. The app wrote its canvas over its
; own code and the machine wedged mid-repaint on the first 0xFF opcode.
;
; One word pair, set around the entry call, and the walk treats it as a
; claim like any other. It is not a lock and not a stack: exactly one load
; is ever in flight (loads are UI-task and sequential, SPEC.md 21), so a
; second hold cannot exist to nest.
; -----------------------------------------------------------------------------
cm_hold:
    mov [cm_hold_seg], bx
    mov [cm_hold_para], ax
    ret

cm_unhold:
    mov word [cm_hold_para], 0  ; zero paragraphs IS "no hold" (cm_claim)
    ret

; -----------------------------------------------------------------------------
; cm_alloc - claim a run of conventional memory (osapi slot 0x01B8's body)
;
; in:  AX = paragraphs wanted (0 is rounded up to 1)
; out: CF = 0 and AX = the block's base SEGMENT;
;      CF = 1 and AX = 0 no arena / 1 no free block-table entry / 2 no
;      contiguous run that big
; clobbers: nothing else (flags)
;
; Context: UI task, from a dispatched package site (entry proc, window
; callback, menu handler, file-dialog completion). NEVER from a worker task
; - cm_fit reads the instance table, which the UI task mutates.
; -----------------------------------------------------------------------------
cm_alloc:
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, ax
    jcxz .one                   ; a zero-paragraph request still costs one,
    jmp short .slot             ; so every grant has a distinct base
.one:
    inc cx
.slot:
    xor di, di                  ; DI = a free table entry, 0 = none found
    mov bx, cm_tab
    mov dx, CM_MAX_BLKS
.fscan:
    cmp word [bx+CB_PARA], 0
    jne .fnext
    mov di, bx
    jmp short .fdone
.fnext:
    add bx, CM_BLKSZ
    dec dx
    jnz .fscan
.fdone:
    or di, di
    jz .full
    mov ax, cx
    call cm_fit                 ; in AX = paragraphs; out BX = the segment
    jc .norun
    call cm_owner               ; outside the window: it is a plain read
    pushf
    cli                         ; the publish is ONE store as far as any
    mov [di+CB_SEG], bx         ; other task is concerned (xm_alloc's
    mov [di+CB_PARA], cx        ; reasoning): ungated, a teardown landing
    mov [di+CB_OWN], al         ; between the CB_PARA and CB_OWN stores sees
    popf                        ; a live entry carrying a STALE owner byte,
                                ; frees the block out from under us, and the
                                ; next cm_alloc hands the same range twice
    mov ax, bx                  ; the answer is the base segment
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.full:
    mov ax, 1
    jmp short .fail
.norun:
    xor ax, ax                  ; tell a zero arena apart from a full one:
    cmp word [ld_arena_top], ARENA_SEG  ; the first is a machine that can
    jbe .fail                           ; never fund this app, the second a
    mov ax, 2                           ; moment that cannot
.fail:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; cm_free - give a block back (osapi slot 0x01C0's body)
;
; in:  AX = a base segment this caller owns
; out: CF = 0 freed; CF = 1 not a base, or not the caller's
; clobbers: nothing (AX preserved; flags)
;
; The merge is free: a released entry stops covering its range and cm_fit's
; candidate walk sees one longer gap.
; -----------------------------------------------------------------------------
cm_free:
    push ax
    push bx
    push cx
    push si
    mov bx, ax
    call cm_owner               ; AL = the caller
    mov cx, CM_MAX_BLKS
    mov si, cm_tab
    pushf
    cli                         ; match and release are one operation against
                                ; a concurrent teardown (see cm_alloc)
.scan:
    cmp word [si+CB_PARA], 0
    je .next
    cmp [si+CB_SEG], bx
    jne .next
    cmp [si+CB_OWN], al
    jne .next                   ; someone else's block: not yours to free
    mov word [si+CB_PARA], 0
    mov byte [si+CB_OWN], CM_OWN_KERN   ; never leave a live instance slot
    popf                                ; stamped on a free entry
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.next:
    add si, CM_BLKSZ
    loop .scan
    popf                        ; nothing matched
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; cm_caps - what is free, and what is the biggest single piece?
;           (osapi slot 0x01C8's body)
;
; in:  nothing
; out: AX = the largest contiguous free run, PARAGRAPHS
;      DX = total free, PARAGRAPHS
;      BL = free block-table entries of CM_MAX_BLKS
; clobbers: nothing else (flags)
;
; The query PAINT-NOTES asked for: an app can size itself to what is
; actually there instead of dividing an int 12h figure and hoping nothing
; else wants any. Both answers are honest on the 256KB floor - the arena is
; empty there and this returns zeros, which is the app's cue to put up a
; notice rather than a canvas.
;
; UI task only, like cm_alloc: this reads the instance table too, and
; [cm_ctot] is module scratch with no lock behind it.
;
; The walk is the gap between claims, not the claims themselves: advance a
; cursor from ARENA_SEG, bump it past any claim containing it, and measure
; from there to the LOWEST claim start above it (or the top). Each pass
; either steps past a claim or lands on the next claim's start, so it
; terminates in at most CM_NCLAIM+1 passes.
; -----------------------------------------------------------------------------
; CX and DX are cm_claim's outputs and cannot carry anything across the
; scan, which leaves one value too many for the register file: the running
; TOTAL lives in [cm_ctot] for the duration. It is module scratch on the UI
; task, the only task allowed in here.
cm_caps:
    push cx
    push si
    push di
    push bp
    xor ax, ax                  ; AX = best run so far
    mov word [cm_ctot], 0
    xor bl, bl
    mov si, cm_tab              ; free table entries, counted first (SI is
    mov cx, CM_MAX_BLKS         ; the claim cursor below, not yet)
.tscan:
    cmp word [si+CB_PARA], 0
    jne .tnext
    inc bl
.tnext:
    add si, CM_BLKSZ
    loop .tscan

    mov di, ARENA_SEG           ; DI = the cursor, in paragraphs
.pass:
    cmp di, [ld_arena_top]
    jae .done                   ; also the zero-arena answer: 0 and 0
    mov bp, [ld_arena_top]      ; BP = the lowest claim start above DI
    xor si, si
.scan:
    call cm_claim               ; CX = claim base, DX = claim end...
    jc .next
    cmp cx, di
    ja .above                   ; ...starts above the cursor: a candidate
    cmp dx, di
    jbe .next                   ; ends at/below it: behind us
    mov di, dx                  ; it CONTAINS the cursor: step past it and
    jmp short .pass             ; start the pass again
.above:
    cmp cx, bp
    jae .next
    mov bp, cx                  ; a nearer wall
.next:
    inc si
    cmp si, CM_NCLAIM
    jb .scan
    mov cx, bp                  ; the gap [DI, BP) is free
    sub cx, di
    add [cm_ctot], cx
    cmp cx, ax
    jbe .cursor
    mov ax, cx                  ; a new largest run
.cursor:
    mov di, bp                  ; the cursor lands on the wall and the next
    jmp short .pass             ; pass steps over whatever owns it
.done:
    mov dx, [cm_ctot]
    pop bp
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; cm_inst_mem - what does one instance hold in grants? (SPEC.md 28)
; in:  AL = instance slot
; out: AX = the total, PARAGRAPHS (0 if it holds none)
;      BX = the base SEGMENT of its LARGEST block (0 if it holds none)
; clobbers: nothing else (flags)
;
; The Task Manager's RAM figure sums I_SIZE, which is the package's REGION
; and nothing else. Without this a bitmap editor's quarter megabyte is
; invisible in the one readout that exists to show exactly that.
;
; Two answers rather than one because the memory VIEW draws a band, and a
; band needs a base. The total is exact whatever the instance holds; the
; base is exact for the one-block idiom this API is shaped around (see the
; SPEC.md 20.8 rule "take one block and subdivide it yourself") and names
; the largest piece otherwise, which is the honest approximation - never a
; band over memory the instance does not hold.
; -----------------------------------------------------------------------------
cm_inst_mem:
    push cx
    push dx
    push si
    mov ch, al                  ; CH = the owner we are summing
    xor ax, ax                  ; AX = paragraphs so far
    xor bx, bx                  ; BX = the largest block's base...
    xor dx, dx                  ; ...and DX its size, to compare against
    mov cl, CM_MAX_BLKS
    mov si, cm_tab
.scan:
    cmp word [si+CB_PARA], 0
    je .next
    cmp [si+CB_OWN], ch
    jne .next
    push cx
    mov cx, [si+CB_PARA]
    add ax, cx
    cmp cx, dx
    jbe .nobest
    mov dx, cx
    mov bx, [si+CB_SEG]
.nobest:
    pop cx
.next:
    add si, CM_BLKSZ
    dec cl
    jnz .scan
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; cm_release_inst - force-free every block an instance holds
; in:  AL = instance slot
; out: nothing; clobbers: nothing (flags)
;
; xm_release_inst's shape INCLUDING ITS pushf/cli, which is not decoration:
; this runs on the dying task (inst_task_die, gfx lock NOT held) while the
; UI task may be inside cm_alloc or cm_free, and a scan interleaved with
; either mutator reads a half-published entry. Called from all three
; SPEC.md 29.4 teardown paths, because a task-less package is never told it
; is closing and would otherwise strand its claim for the session.
; -----------------------------------------------------------------------------
cm_release_inst:
    push cx
    push si
    pushf
    cli
    mov cx, CM_MAX_BLKS
    mov si, cm_tab
.scan:
    cmp word [si+CB_PARA], 0
    je .next
    cmp [si+CB_OWN], al
    jne .next
    mov word [si+CB_PARA], 0
    mov byte [si+CB_OWN], CM_OWN_KERN   ; a free entry never carries a live
.next:                                  ; instance slot
    add si, CM_BLKSZ
    loop .scan
    popf
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; cm_release_rec - cm_release_inst addressed by RECORD, not by slot
; in:  DI = instance record ptr
; out: nothing; clobbers: nothing (flags)
;
; xm_release_rec's shape, and for its reason: the SPEC.md 29.4 teardown
; sites hold the record, the release wants the slot, and the conversion
; belongs beside the release rather than at four call sites.
; -----------------------------------------------------------------------------
cm_release_rec:
    push ax
    push cx
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    call cm_release_inst        ; in: AL = instance slot
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

cm_tab:     resb CM_MAX_BLKS * CM_BLKSZ ; the block table, hand-zeroed by
                                        ; cm_init - `-f bin` zeroes no .bss
cm_ctot:    resw 1                      ; cm_caps scratch: the running free
                                        ; total, which has no register left
cm_hold_seg:  resw 1                    ; the region a load is in flight for
cm_hold_para: resw 1                    ; (cm_hold above); 0 paragraphs = none.
                                        ; cm_init zeroes both with the table.

section .text
