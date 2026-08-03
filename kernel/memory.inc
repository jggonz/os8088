; =============================================================================
; os8088 - memory.inc
;
; The claim heap (SPEC.md 42): every paragraph of conventional memory above
; the package pool, handed out in KB-granular, segment-aligned blocks to
; whoever asks - a package through the API slots, or the kernel itself for
; the things it uses speculatively (the menu save-under, a Disk window's
; listing cache, the double-buffer back buffer).
;
; Why this exists. Before it, memory outside the pool was carved up by
; CONSTANTS - SND_SEG, SAVE_SEG, VIEW_SEG, BB_SEG - each claimed whole from
; boot whether or not a byte of it was ever written, and a package that
; wanted more than its 19.5KB region had no way to ask: apps/paint simply
; took linear 0x66000 and hoped, reading a kernel policy constant to guess
; whether the back buffer would ever want it (docs/PAINT-NOTES.md records
; that as the least defensible thing in the file). One map, owned by the
; kernel, retires the whole class: two packages cannot pick the same
; address, an app cannot outlive its claim, and the Task Manager can bill
; every byte to whoever holds it.
;
; The kernel does NOT gate individual accesses. A claim is a segment and a
; length; what the holder does inside it is its own business, exactly as
; with a package's own region. The kernel owns the MAP, not the memory.
;
; Concurrency (binding): the table is read and written under one
; pushf/cli..popf window per operation, the task_spawn precedent (SPEC.md
; 8). Claims are made from the UI task in practice - window callbacks,
; kmain, the loader - but mem_free_owner runs on a dying package's own
; worker task inside inst_task_die, so the two really can meet.
; =============================================================================

MEM_MAX  equ 16                 ; claim records; more than this and a claim
                                ; is refused, which is the same answer as
                                ; "no room" and needs no new failure path
MC_SEG   equ 0                  ; word: base segment, 0 = free record
MC_PARA  equ 2                  ; word: size in paragraphs (KB << 6)
MC_OWN   equ 4                  ; word: owner - 0..INST_MAX-1 is an instance
                                ; slot; 0xFF00 | tag is the kernel's own
MC_SIZE  equ 6

MEM_K_SAVE equ 0xFF01           ; kernel tags (SPEC.md 42.2): menu save-under
MEM_K_BB   equ 0xFF02           ; the double-buffer back buffer

MEM_PARA_KB equ 64              ; paragraphs per KB

; -----------------------------------------------------------------------------
; mem_init - size the heap and clear the map (kmain, before anything claims)
;
; in:       nothing
; out:      nothing (all registers preserved)
;
; int 12h gives conventional memory in KB; the heap is everything from
; HEAP_SEG (the paragraph above the package pool) to the top of it. On a
; machine whose RAM ends below HEAP_SEG the heap is empty and every claim is
; refused - which is a supported outcome, not a boot failure: the kernel's
; own claims all have a "then don't" path.
;
; Runs on task 0 before any other task exists, so it needs no critical
; section (SPEC.md 7's only-the-UI-task-calls-BIOS rule holds here).
; -----------------------------------------------------------------------------
mem_init:
    push ax
    push cx
    push di
    push es

    push ds
    pop es
    mov di, mem_tab             ; every record free
    mov cx, MEM_MAX * MC_SIZE
    xor al, al
    cld
    rep stosb

    mov word [mem_base], HEAP_SEG
    mov word [mem_top], HEAP_SEG        ; an empty heap until int 12h says more

    int 0x12                    ; AX = conventional KB
    mov cl, 6                   ; KB -> paragraphs: AX * 64, and the top
    shl ax, cl                  ; segment IS that paragraph count (640KB ->
    jc .done                    ; 0xA000). A shift out is impossible below
                                ; 1MB, but a hostile BIOS is not our problem
                                ; to crash for
    cmp ax, HEAP_SEG
    jbe .done                   ; RAM ends at or below the heap: none of it
    mov [mem_top], ax
.done:
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_claim - claim a block of the heap (SPEC.md 42.3)
;
; in:       AX = KB wanted (1..640), BX = owner word (instance slot, or a
;           MEM_K_* kernel tag)
; out:      CF = 0, DX = base segment of the block; CF = 1 refused (no free
;           record, no hole big enough, or a silly size)
; clobbers: DX (an output) and the flags - every other register preserved
;
; First fit, lowest address. The scan is the ld_alloc idiom (SPEC.md 21):
; walk the claims; if one overlaps the candidate, restart just past it. Each
; bump lands on a claim's end, so the loop terminates.
; -----------------------------------------------------------------------------
mem_claim:
    push ax
    push cx
    push si
    push di
    pushf
    cli                         ; the whole scan-and-publish, one window

    or ax, ax
    jz .fail
    mov cl, 6
    shl ax, cl                  ; AX = paragraphs wanted
    jc .fail                    ; > 1MB asked for

    mov di, mem_tab             ; a free record first: refusing early costs
.rec:                           ; nothing and keeps the scan simple
    cmp word [di+MC_SEG], 0
    je .haverec
    add di, MC_SIZE
    cmp di, mem_tab + MEM_MAX*MC_SIZE
    jb .rec
    jmp .fail
.haverec:

    mov dx, [mem_base]          ; DX = candidate base
.retry:
    mov cx, dx
    add cx, ax                  ; CX = candidate end
    jc .fail                    ; wrapped past 0xFFFF
    cmp cx, [mem_top]
    ja .fail

    mov si, mem_tab             ; overlap scan: CX is the candidate end and
.scan:                          ; the only register free, so it doubles as
    cmp word [si+MC_SEG], 0     ; the claim-end scratch and is rebuilt after
    je .next
    cmp [si+MC_SEG], cx         ; claim starts at/past our end: no overlap
    jae .next
    mov cx, [si+MC_SEG]
    add cx, [si+MC_PARA]        ; CX = this claim's end
    cmp cx, dx
    jbe .nover                  ; claim ends at/before our start: ditto
    mov dx, cx                  ; overlap: restart just past this claim
    jmp short .retry
.nover:
    mov cx, dx                  ; rebuild the candidate end and carry on
    add cx, ax
.next:
    add si, MC_SIZE
    cmp si, mem_tab + MEM_MAX*MC_SIZE
    jb .scan

    mov [di+MC_SEG], dx         ; publish the claim
    mov [di+MC_PARA], ax
    mov [di+MC_OWN], bx
    popf
    clc
    jmp short .out
.fail:
    popf
    xor dx, dx
    stc
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_free - release a block (SPEC.md 42.3)
;
; in:       DX = the base segment mem_claim returned, BX = the owner word it
;           was claimed with
; out:      CF = 0 released; CF = 1 no such claim, or not this owner's
; clobbers: nothing else (flags)
;
; The owner test is what stops a package freeing the kernel's back buffer,
; or another instance's canvas, with one wrong word.
; -----------------------------------------------------------------------------
mem_free:
    push cx
    push si
    pushf
    cli
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [si+MC_SEG], 0
    je .next                    ; a free record can match a DX of 0
    cmp [si+MC_SEG], dx
    jne .next
    cmp [si+MC_OWN], bx
    jne .next
    mov word [si+MC_SEG], 0     ; the record IS the allocator (SPEC.md 42.3)
    popf
    clc
    jmp short .out
.next:
    add si, MC_SIZE
    loop .scan
    popf
    stc
.out:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; mem_free_owner - release every claim an owner holds (SPEC.md 42.4)
;
; in:       BX = owner word
; out:      nothing (all registers preserved)
;
; The teardown fence. Called from both SPEC.md 29.4 paths beside
; snd_release_rec, so a closed package can no more leak a quarter of a
; megabyte than it can leave a tone droning.
; -----------------------------------------------------------------------------
mem_free_owner:
    push cx
    push si
    pushf
    cli
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [si+MC_SEG], 0
    je .next
    cmp [si+MC_OWN], bx
    jne .next
    mov word [si+MC_SEG], 0
.next:
    add si, MC_SIZE
    loop .scan
    popf
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; mem_free_rec - mem_free_owner for an instance record pointer
; in:  DI = instance record ptr
; out: nothing (all registers preserved) - the snd_release_rec twin
; -----------------------------------------------------------------------------
mem_free_rec:
    push ax
    push bx
    push cx
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    mov bx, ax
    call mem_free_owner
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_avail - how much heap is there, and how much of it in one piece?
;
; in:       nothing
; out:      AX = largest free run in KB, BX = total free KB
; clobbers: nothing else (flags)
;
; The query a package sizes itself from (SPEC.md 42.3). Walking holes
; without sorting: for each candidate base (the heap base, then every
; claim's end), measure the distance to the nearest claim that starts above
; it. O(MEM_MAX^2) with MEM_MAX = 16 - a few hundred cycles, called once at
; app startup.
; -----------------------------------------------------------------------------
mem_avail:
    push cx
    push dx
    push si
    push di
    pushf
    cli
    mov dx, [mem_base]          ; candidate 0: the bottom of the heap
    call mem_run
    mov ax, cx                  ; AX = largest run, paragraphs
    mov di, mem_tab             ; then every claim's end
.cand:
    cmp word [di+MC_SEG], 0
    je .cnext
    mov dx, [di+MC_SEG]
    add dx, [di+MC_PARA]
    cmp dx, [mem_top]
    jae .cnext
    call mem_run                ; CX = free paragraphs from DX up
    cmp cx, ax
    jbe .cnext
    mov ax, cx                  ; a new largest run
.cnext:
    add di, MC_SIZE
    cmp di, mem_tab + MEM_MAX*MC_SIZE
    jb .cand

    mov si, mem_tab             ; total free = heap - sum of claims
    mov bx, [mem_top]
    sub bx, [mem_base]
    mov cx, MEM_MAX
.tot:
    cmp word [si+MC_SEG], 0
    je .tnext
    sub bx, [si+MC_PARA]
.tnext:
    add si, MC_SIZE
    loop .tot

    popf
    mov cl, 6                   ; paragraphs -> KB, rounded DOWN: a claim of
    shr ax, cl                  ; the answer must always fit
    shr bx, cl
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; mem_run - free paragraphs starting at a candidate base (module-internal)
; in:  DX = candidate base segment
; out: CX = free paragraphs from DX up to the next claim (or mem_top)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
mem_run:
    push ax
    push si
    push di
    mov cx, [mem_top]           ; CX = the ceiling, lowered by every claim
    mov si, mem_tab             ; that starts at or above DX
    mov di, MEM_MAX
.scan:
    cmp word [si+MC_SEG], 0
    je .next
    mov ax, [si+MC_SEG]
    cmp ax, dx
    jb .below
    cmp ax, cx
    jae .next
    mov cx, ax                  ; nearer ceiling
    jmp short .next
.below:                         ; a claim that starts below DX still blocks
    add ax, [si+MC_PARA]        ; us if it ENDS above it - only possible when
    cmp ax, dx                  ; DX is another claim's end inside it, which
    jbe .next                   ; cannot happen; the guard is free
    mov cx, dx
.next:
    add si, MC_SIZE
    dec di
    jnz .scan
    sub cx, dx                  ; CX = the run
    jnc .out
    xor cx, cx
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_owned_kb - how many KB does one owner hold? (SPEC.md 42.5)
; in:  BX = owner word
; out: AX = KB claimed by that owner (0 if none)
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
mem_owned_kb:
    push cx
    push si
    xor ax, ax
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [si+MC_SEG], 0
    je .next
    cmp [si+MC_OWN], bx
    jne .next
    add ax, [si+MC_PARA]
.next:
    add si, MC_SIZE
    loop .scan
    mov cl, 6
    shr ax, cl
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; mem_claimed_kb / mem_kernel_kb - the Task Manager's two heap terms
;
; in:  AX = a running KB total
; out: AX = that total plus the claimed KB (all owners / kernel tags only)
; clobbers: nothing else (flags)
;
; ADD-style rather than plain readers because both call sites are building a
; sum in AX, and the .fartext caller reaches them through KCALL (SPEC.md 33)
; where a spare register is not free.
; -----------------------------------------------------------------------------
mem_claimed_kb:
    push bx
    xor bx, bx                  ; BX = 0: every owner
    call mem_sum_kb
    pop bx
    ret

mem_kernel_kb:
    push bx
    mov bx, 1                   ; BX = 1: kernel tags (0xFF00 and up) only
    call mem_sum_kb
    pop bx
    ret

; mem_sum_kb - in: AX = running total, BX = 0 all / 1 kernel-tagged only
mem_sum_kb:
    push cx
    push dx
    push si
    xor dx, dx
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [si+MC_SEG], 0
    je .next
    or bx, bx
    jz .take
    cmp byte [si+MC_OWN+1], 0xFF    ; the kernel tags are 0xFFxx; an instance
    jne .next                       ; slot is 0..INST_MAX-1
.take:
    add dx, [si+MC_PARA]
.next:
    add si, MC_SIZE
    loop .scan
    mov cl, 6
    shr dx, cl                  ; paragraphs -> KB
    add ax, dx
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; mem_total_kb - the heap's size and how much of it is spoken for
; in:  nothing
; out: AX = claimed KB, BX = heap size KB
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
mem_total_kb:
    push cx
    push si
    xor ax, ax
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [si+MC_SEG], 0
    je .next
    add ax, [si+MC_PARA]
.next:
    add si, MC_SIZE
    loop .scan
    mov bx, [mem_top]
    sub bx, [mem_base]
    mov cl, 6
    shr ax, cl
    shr bx, cl
    pop si
    pop cx
    ret

; =============================================================================
; The API slot targets (SPEC.md 20.3/42.3). A package names itself by its own
; window, exactly as OSAPI_TASK_SPAWN does (SPEC.md 20.6): the ownership
; fence is the same one, and for the same reason - a borrowed window pointer
; must not let one instance spend or free another's memory.
; =============================================================================

; -----------------------------------------------------------------------------
; osapi_mem_claim - in: AX = KB, BX = the caller's own window ptr
;                   out: CF = 0 and DX = segment, CF = 1 refused
; -----------------------------------------------------------------------------
osapi_mem_claim:
    push bx
    push di
    call mem_own                ; BX = owner word for this caller
    jc .fail
    call mem_claim
    jmp short .out
.fail:
    xor dx, dx
    stc
.out:
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; osapi_mem_free - in: DX = segment, BX = the caller's own window ptr
;                  out: CF = 0 released, CF = 1 refused
; -----------------------------------------------------------------------------
osapi_mem_free:
    push bx
    push di
    call mem_own
    jc .out                     ; CF = 1 already
    call mem_free
.out:
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; osapi_mem_avail - in: nothing; out: AX = largest free KB, BX = total free KB
; -----------------------------------------------------------------------------
osapi_mem_avail:
    jmp mem_avail

; -----------------------------------------------------------------------------
; mem_own - window pointer to owner word (module-internal)
; in:  BX = a window pointer that came from a package (never trusted)
; out: CF = 0 and BX = the owning instance's slot; CF = 1 if BX is not a
;      plausible window, is unowned, or names a dying instance
; clobbers: BX (the output), DI, flags
; -----------------------------------------------------------------------------
mem_own:
    call inst_wchk              ; bound it before any deref (SPEC.md 20.6)
    jc .bad
    call inst_of_win            ; DI = the record owning BX
    jc .bad
    cmp byte [di+I_STATE], 1
    jne .bad
    mov bx, di                  ; record ptr -> slot index
    sub bx, inst_tab
    push cx
    mov cl, 5
    shr bx, cl
    pop cx
    clc
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
section .bss

mem_tab:  resb MEM_MAX * MC_SIZE  ; the claim map (layout above)
mem_base: resw 1                  ; first heap paragraph = HEAP_SEG
mem_top:  resw 1                  ; one past the last, from int 12h

section .text
