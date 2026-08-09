; =============================================================================
; os8088 - memory.inc
;
; The claim heap (SPEC.md 50): every paragraph of conventional memory above
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

MEM_MAX  equ 32                 ; claim records; more than this and a claim
                                ; is refused, which is the same answer as
                                ; "no room" and needs no new failure path.
                                ; Mirrored in apps/os88api.inc (SPEC.md 20.9
                                ; buffer sizing) - change together, rebuild
                                ; every .o88. It
                                ; was 16 while a package's REGION came out of
                                ; a pool of its own and only data competed for
                                ; records; a region is a claim now (SPEC.md
                                ; 20.1), so twelve instances can hold twelve
                                ; of these before anything asks for data
MEM_OWNER_MAX equ 8             ; ...and no one owner may hold more than this
                                ; many. It does NOT shrink the table - the
                                ; table is sized by what the machine can hold,
                                ; not by the cap - and that is not what it is
                                ; for. Since a region is a claim, running the
                                ; table out now means NO PACKAGE CAN LOAD, so
                                ; the failure has to be charged to whoever
                                ; caused it: one app is refused its ninth
                                ; claim instead of every other app being
                                ; refused its first. Eight is Paint's
                                ; measured peak (scratch, canvas, undo,
                                ; clipboard, LZW, plus the transient sixth it
                                ; holds while trading a bigger clipboard for
                                ; a smaller) with its region and one spare
MC_SEG   equ 0                  ; word: base segment, 0 = free record
MC_PARA  equ 2                  ; word: size in paragraphs (KB << 6)
MC_OWN   equ 4                  ; word: owner - 0..INST_MAX-1 is an instance
                                ; slot; 0xFF00 | tag is the kernel's own
MC_DMA   equ 6                  ; word: the 64KB-page-safe HEAD in paragraphs,
                                ; 0 = no constraint. Recorded so that a claim
                                ; which MOVES keeps the property it was
                                ; granted for: mem_regrow's path 3 is a fresh
                                ; search, and without this it searched as if
                                ; the block were ordinary and could land the
                                ; head straddling - which the 8237 answers by
                                ; wrapping to the start of its page and moving
                                ; the wrong memory, silently
MC_SIZE  equ 8

MEM_K_SAVE equ 0xFF01           ; kernel tags (SPEC.md 50.2): menu save-under
MEM_K_BB   equ 0xFF02           ; the double-buffer back buffer
MEM_K_DRV  equ 0xFF03           ; a loaded driver's image (SPEC.md 51.3)
MEM_K_COPY equ 0xFF04           ; the file manager's copy buffer (22.3): held
                                ; for exactly one Cut/Copy/Paste operation,
                                ; including the part of it that is suspended
                                ; on an overwrite question
MEM_K_ASC  equ 0xFF06           ; one volume's ASSOC.DAT cache (SPEC.md
                                ; 54.7) - shared, reloaded on a volume switch
MEM_K_FATW equ 0xFF05           ; one volume's private FAT window (18.8.1),
                                ; held for as long as that volume is mounted
MEM_K_CLIP equ 0xFF07           ; the system clipboard's text (SPEC.md 55) -
                                ; sized to what is in it, freed when it is
                                ; emptied, and deliberately NOT an instance's,
                                ; so closing the app that copied does not take
                                ; the clipboard down with it

; --- PURGEABLE claims (SPEC.md 50.6) -----------------------------------------
; A cache the kernel can take back the instant anything else needs the room.
; The Macintosh Memory Manager's purgeable handle, which is where the name
; comes from and this OS is that machine's homage.
;
; IT IS ENCODED IN THE OWNER, and that is what makes it cost nothing. The
; owner word already carries a namespace - 0..INST_MAX-1 an instance slot,
; 0xFFxx a kernel tag - so 0xFExx is "a kernel tag, and purgeable". Three
; things fall out of that with no new field anywhere:
;
;   the RECORD does not grow, so mem_tab is the size it was;
;   osapi_claim_snapshot (20.9) is unchanged, so no .o88 is invalidated and
;     the Task Manager can pick the class out of what it already reads;
;   mem_claim cannot be asked for a purgeable claim and place it as an
;     ordinary one, because the tag IS the request.
;
; TO BE PURGEABLE A CLAIM MUST HAVE EXACTLY ONE KERNEL WORD NAMING IT, and a
; zero in that word must ALREADY mean "no buffer, do it the slow way". That is
; the whole contract, and it is what lets the shed skip a notification
; protocol entirely: mem_shed_one zeroes the word through mem_pg_own before it
; frees the block, so the consumer's existing refusal path is the notice.
; menu_drop's `or ax, ax / jz .nosave` is the shape - every purgeable consumer
; has to already have one.
MEM_PG_HI  equ 0xFE             ; the owner's high byte: purgeable kernel tag
MEM_P_WSAVE equ 0xFE01          ; a window's raise cache (SPEC.md 11.96)

MEM_PARA_KB equ 64              ; paragraphs per KB

; --- osapi_claim_snapshot's buffer, API slot 0x02A0 (SPEC.md 20.9) ----------------
; The three fields of a claim record that mean anything outside this module.
; MC_DMA is not one of them: it is how a claim that MOVES keeps the page
; property it was granted for, and nothing outside can move a claim.
CLS_SEG    equ 0                ; word: base segment, 0 = a free record
CLS_PARA   equ 2                ; word: size in paragraphs (KB << 6)
CLS_OWN    equ 4                ; word: owner (SPEC.md 50.2)
CLS_RECSZ  equ 6
CLAIM_SNAPSHOT_SIZE equ MEM_MAX * CLS_RECSZ

; --- osapi_sys_kb's buffer, API slot 0x02A8 (SPEC.md 20.9) --------------------
; The kernel's own footprint broken into its parts, then the heap's totals.
; The parts sum to SK_KERN exactly - see the routine at the foot of this file
; for which two of them absorb the rounding, and why.
SK_KERN    equ 0                ; word: the whole span (SPEC.md 2)
SK_IMG     equ 2                ; word: image + .bss + the cold segment
SK_FAT     equ 4                ; word: the mount-time FAT window (18.8)
SK_STK     equ 6                ; word: the MAX_TASKS-1 background stacks
SK_STK0    equ 8                ; word: task 0's - the UI task's
SK_DSK     equ 10               ; word: the disk buffers, the rest of .lowbss
SK_BUF     equ 12               ; word: FAT + stacks + disk buffers together,
                                ; the part of the kernel that is scratch
                                ; rather than program
SK_HEAP    equ 14               ; word: the claim heap's size
SK_CLAIM   equ 16               ; word: claimed out of it, every owner
SK_KCLAIM  equ 18               ; word: ...of which the kernel's own tags
SK_XMS     equ 20               ; word: the pool above 1MB, 0 on tier 0 (41)
SYSKB_SIZE equ 22

; -----------------------------------------------------------------------------
; mem_init - size the heap and clear the map (kmain, before anything claims)
;
; in:       nothing
; out:      nothing (all registers preserved)
;
; int 12h gives conventional memory in KB; the heap is everything from
; HEAP_SEG (the paragraph the kernel ends on) to the top of it. On a
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

    push ss                     ; mem_tab is in LOW_SEG now (SPEC.md 2.1), so
    pop es                      ; the clear writes through SS, not DS
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
; mem_claim / mem_claim_hi / mem_claim_dma - claim a block of the heap
; (SPEC.md 50.3)
;
; in:       AX = KB wanted (1..640), BX = owner word (instance slot, or a
;           MEM_K_* kernel tag); mem_claim_dma also CX = KB of the block's
;           HEAD that must not cross a 64KB physical boundary (0 = none)
; out:      CF = 0, DX = base segment of the block; CF = 1 refused (no free
;           record, no hole big enough, this owner is at MEM_OWNER_MAX, a
;           silly size, or nothing page-safe when CX asked for it)
; clobbers: DX (an output) and the flags - every other register preserved
;
; **Two ends, one heap.** mem_claim is first fit from the BOTTOM upward, and
; is what data asks for. mem_claim_hi is first fit from the TOP downward, and
; is what a package's REGION asks for (SPEC.md 20.1/21), because the two have
; different lifetimes and only one of them can ever move:
;
;   a data claim can move within its lifetime by being freed and re-claimed;
;   a package region can NEVER move, because its base IS its CS - relocating
;   it would invalidate W_SEG, I_SPTR, every MB_SEG in the menu bar and every
;   claim owner word, and there is no compaction and cannot be.
;
; Allocated from one end they interleave, and one long-lived data claim
; landing mid-heap permanently splits the space a package can be loaded into:
; a package then fails to load not because 8KB is not free but because 8KB is
; not CONTIGUOUS. From opposite ends they only meet when the heap is genuinely
; full, and either side may still use all of it when the other is not there.
;
; The scan is the ld_alloc idiom it replaced (SPEC.md 21): walk the claims; if
; one overlaps the candidate, restart just past it (or just below it, going
; down). Each bump lands on a claim edge and moves monotonically, so both
; loops terminate.
;
; **The 64KB page constraint is a THIRD kind of bump, not a caller's problem.**
; The 8237 has no register for address bits 16..19 - port 8xh holds them and
; the chip never carries into them - so an ISA DMA buffer that straddles a
; 64KB PHYSICAL boundary wraps to the start of its page and transfers the
; wrong memory. The sound driver used to discover that by claiming, testing,
; and claiming AGAIN while HOLDING the block that failed, up to four times:
; correct, but it can hold 128KB to find 32KB and refuse on a machine that
; had the room all along. mem_claim_dma asks the scan instead. A candidate
; whose head would straddle bumps to the next page floor (going up) or to the
; highest base in its own page that still fits (going down) - both strictly
; monotonic, like the overlap bump, so termination is unchanged, and the very
; next test passes by construction because the new base's head starts at
; offset 0 or ends exactly at the page's end.
;
; CX is the HEAD, not the whole block, because only part of a buffer is
; usually the chip's: the sound driver's staging pool sits above its double
; buffer and ring and is copied with rep movsb, so it may straddle freely.
; Constraining the whole 32KB would halve the legal bases for nothing.
;
; A head bigger than a page can never be satisfied and is refused up front -
; which is also what keeps the bump loop finite, since the bump only helps
; when a page can hold the head at all.
;
; PRESERVED by mem_regrow: the record carries the constraint (MC_DMA), so a
; claim that MOVES (regrow path 3) is re-placed under the same page rule -
; mem_dmaok is the re-test, and mem_hifit refuses a straddling candidate
; rather than bumping, which can refuse where a legal base exists (rare,
; conservative, never wrong memory).
; -----------------------------------------------------------------------------
mem_claim_dma:
    push si
    push di
    push cx
    mov si, cx
    mov cl, 6
    shl si, cl                  ; head KB -> paragraphs
    pop cx
    jnc .set
    mov si, 0xFFFF              ; a head past 1MB: let the page test refuse it
.set:                           ; rather than wrap into a plausible small one
    xor di, di
    jmp short mem_claim.go
mem_claim_hi:
    push si
    push di
    xor si, si
    mov di, 1
    jmp short mem_claim.go
mem_claim:
    push si
    push di
    xor si, si
    xor di, di
    cmp bh, MEM_PG_HI           ; a purgeable tag places ITSELF (SPEC.md
    jne .go                     ; 50.5.1): top down like a region, but under
    inc di                      ; a ceiling that keeps it out of the region
                                ; arena. The tag is the request, so there is
                                ; no third entry point to call by mistake
.go:
    call mem_claim_1            ; SHED AND RETRY (SPEC.md 50.6.2). A cache
    jnc .out                    ; loses to anything that is not one, and a
    call mem_shed_one           ; package load loses to nothing - so this is
    jnc .go                     ; on mem_claim_hi's path too. It terminates
                                ; because each shed removes a record and
                                ; mem_shed_one refuses when there are none
.out:
    pop di
    pop si
    ret

; the scan-and-publish itself; mem_claim above is the retry loop around it
mem_claim_1:
    push ax
    push cx
    push si                     ; THE STAGED PARAMETERS, and they are staged in
    push di                     ; REGISTERS on purpose (see the cli below), so
                                ; the retry above re-reads them from here. The
                                ; scan owns both - DI walks mem_tab, SI counts
                                ; this owner then walks it again - so without
                                ; this a shed-and-retry restages a mem_tab
                                ; OFFSET as [mem_dma] and a non-zero [mem_dir]:
                                ; a phantom DMA head measured in KB, and a
                                ; bottom-up data claim placed top down, in the
                                ; region arena mem_pg_ceil exists to keep whole
    pushf
    cli                         ; the whole scan-and-publish, one window -
    mov [mem_dma], si           ; the operation's PARAMETERS included. The
    mov [mem_dir], di           ; entries stage them in registers because a
                                ; global stored BEFORE the cli is another
                                ; task's to overwrite in the gap: a worker's
                                ; DMA head silently dropped, or a region
                                ; claim allocating from the wrong end

    push ax                     ; THE CEILING the scan may not pass. It is
    mov ax, [mem_top]           ; [mem_top] for everything except a purgeable
    cmp bh, MEM_PG_HI           ; claim, which stops below the lowest REGION
    jne .ceilok                 ; so that it can never split the arena a
    call mem_pg_ceil            ; package has to load into (SPEC.md 50.6.1)
.ceilok:
    mov [mem_ceil], ax
    pop ax

    or ax, ax
    jz .fail
    mov cl, 6
    shl ax, cl                  ; AX = paragraphs wanted
    jc .fail                    ; > 1MB asked for
    cmp word [mem_dma], 0x1000  ; ...and a head bigger than a 64KB page can
    ja .fail                    ; never sit inside one

    mov di, mem_tab             ; this owner's share first (MEM_OWNER_MAX):
    mov cx, MEM_MAX             ; the table is a shared resource and running
    xor si, si                  ; it out stops packages LOADING, so the app
.own:                           ; that wants a ninth claim pays, not the next
    cmp word [ss:di+MC_SEG], 0     ; app to ask for its first
    je .ownext
    cmp [ss:di+MC_OWN], bx
    jne .ownext
    inc si
.ownext:
    add di, MC_SIZE
    loop .own
    cmp si, MEM_OWNER_MAX
    jae .fail

    mov di, mem_tab             ; a free record next: refusing early costs
.rec:                           ; nothing and keeps the scan simple
    cmp word [ss:di+MC_SEG], 0
    je .haverec
    add di, MC_SIZE
    cmp di, mem_tab + MEM_MAX*MC_SIZE
    jb .rec
    jmp .fail
.haverec:

    cmp word [mem_dir], 0
    jne .hi
    mov dx, [mem_base]          ; DX = candidate base
    jmp short .retry
.hi:
    mov dx, [mem_ceil]          ; the highest base that still fits
    sub dx, ax
    jc .fail
    cmp dx, [mem_base]
    jb .fail
.retry:
    cmp word [mem_dma], 0       ; the 64KB DMA-page test, when one was asked
    je .fit                     ; for (CX is dead here - .fit rebuilds it)
    mov cx, dx
    and cx, 0x0FFF              ; paragraphs into this 64KB physical page
    add cx, [mem_dma]
    cmp cx, 0x1000              ; the head has to END inside the same page
    jbe .fit
    mov cx, dx                  ; it straddles. Going UP, the next base that
    and cx, 0xF000              ; can work is the next page's floor...
    add cx, 0x1000
    jc .fail                    ; ...past 1MB, so there is no next page
    cmp word [mem_dir], 0
    je .dmago
    sub cx, [mem_dma]           ; ...and going DOWN, the highest base in THIS
    cmp cx, [mem_base]          ; page whose head still fits. Below the heap
    jb .fail                    ; floor means every lower page is too
.dmago:
    mov dx, cx
    jmp short .retry
.fit:
    mov cx, dx
    add cx, ax                  ; CX = candidate end
    jc .fail                    ; wrapped past 0xFFFF
    cmp cx, [mem_ceil]
    ja .fail

    mov si, mem_tab             ; overlap scan: CX is the candidate end and
.scan:                          ; the only register free, so it doubles as
    cmp word [ss:si+MC_SEG], 0     ; the claim-end scratch and is rebuilt after
    je .next
    cmp [ss:si+MC_SEG], cx         ; claim starts at/past our end: no overlap
    jae .next
    mov cx, [ss:si+MC_SEG]
    add cx, [ss:si+MC_PARA]        ; CX = this claim's end
    cmp cx, dx
    jbe .nover                  ; claim ends at/before our start: ditto
    cmp word [mem_dir], 0
    jne .down
    mov dx, cx                  ; overlap: restart just past this claim...
    jmp .retry
.down:                          ; ...or just BELOW it, coming down from the
    mov dx, [ss:si+MC_SEG]         ; top. Monotonic either way, so it terminates
    sub dx, ax
    jc .fail
    cmp dx, [mem_base]
    jb .fail
    jmp .retry
.nover:
    mov cx, dx                  ; rebuild the candidate end and carry on
    add cx, ax
.next:
    add si, MC_SIZE
    cmp si, mem_tab + MEM_MAX*MC_SIZE
    jb .scan

    mov [ss:di+MC_SEG], dx         ; publish the claim
    mov [ss:di+MC_PARA], ax
    mov [ss:di+MC_OWN], bx
    push ax
    mov ax, [mem_dma]           ; ...and the page constraint it was granted
    mov [ss:di+MC_DMA], ax         ; under, so a later move can honour it
    pop ax
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
; mem_pg_ceil - the lowest REGION base, or [mem_top] when there is none
; out: AX = the ceiling a purgeable claim may not pass; preserves everything
;      else
;
; SPEC.md 50.6.1. A purgeable block placed among the regions is
; indistinguishable from a region to the scan while it is live, so it splits
; exactly the run a package needs - and shed-and-retry does not save it,
; because that only fires on a REFUSAL and a region that merely fits WORSE has
; been fragmented with nothing noticing. So purgeable is highest-fit inside
; the DATA arena: the region arena keeps 50.3's property that everything in it
; is a region, and the block still sits against the free middle, which is the
; one run both arenas grow toward and the only one worth enlarging.
;
; AN INSTANCE SLOT IN MC_OWN IS NOT A REGION, and reading it as one made this
; whole feature dead on the commonest desktop there is. 50.3's rule is that a
; PACKAGE's region is owned by the instance slot and a PACKAGE's data claims by
; its segment - it says nothing about a BUILT-IN, whose data claims are stamped
; with its slot as well, and mem_free_rec forty lines below is where that is
; written down ("a built-in's claims are stamped with its instance slot (the
; Disk window's cache)"). So "below INST_MAX" also matches a Disk window's
; 3KB listing cache (SPEC.md 22.1), which is a data claim placed BOTTOM UP and
; therefore sits at the very floor of the heap. Measured, with README.TXT open
; from a Disk window: the ceiling came back as that cache's base, 0x17C0
; against a heap floor of 0x1680 - and the FAT window already filled every
; paragraph between them, so every raise-cache claim was refused on a machine
; with 505KB free and SPEC.md 11.96 never once fired.
;
; A region is what a package's image IS: I_SPTR of a KIND_PKG instance. That is
; the test, and it is two compares on the few records that get that far.
; -----------------------------------------------------------------------------
mem_pg_ceil:
    push bx
    push cx
    push si
    push di
    mov ax, [mem_top]
    mov bx, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [ss:bx+MC_SEG], 0
    je .next
    cmp [ss:bx+MC_SEG], ax
    jae .next                   ; at or above the lowest region found so far:
                                ; it cannot lower the ceiling whatever it is
    mov di, [ss:bx+MC_OWN]
    cmp di, INST_MAX
    jae .next                   ; a kernel tag, or a package's own segment -
                                ; either way a data claim
    mov si, di                  ; si = di*I_RECSZ, without CL: the loop counter
    shl si, 1                   ; is in CX and the 8086 has no shl reg, imm
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1
    add si, inst_tab
    test byte [si+I_KIND], KIND_PKG
    jz .next                    ; a built-in stamps its DATA claims with its
                                ; slot too - see the header
    mov di, [ss:bx+MC_SEG]
    cmp di, [si+I_SPTR]
    jne .next                   ; ...and a package's region is the one claim
    mov ax, di                  ; whose base is its I_SPTR
.next:
    add bx, MC_SIZE
    loop .scan
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mem_pg_own - tag -> the ONE kernel word that names that purgeable block
;
; The whole notification protocol (SPEC.md 50.6). Zeroing this word IS the
; notice, because a purgeable consumer's contract is that zero there already
; means "no buffer, do it the slow way" - a path it must already have and
; already test. A tag missing from this table simply keeps its block until it
; is freed properly, which is safe and is why the list ends in a 0 rather than
; an assertion.
; -----------------------------------------------------------------------------
mem_pg_own:
    dw MEM_P_WSAVE, wm_su_seg
    dw 0

; -----------------------------------------------------------------------------
; mem_shed_one - give up one purgeable block
; out: CF = 0 one was shed, CF = 1 there was nothing to shed
; clobbers: flags
;
; ONE CALLER, AND THAT IS LOAD-BEARING - it is mem_claim's retry, above, and
; nothing else in the tree sheds. "Zeroing the word IS the notice" holds only
; while that word is the ONLY reference to the block, and there are two spans
; where it is not: wm_su_take publishes [wm_su_seg] LAST, so the word is still
; 0 for the whole of gfx_save, and wm_su_try holds the segment in ES across
; gfx_restore. A shed inside either would be written into, or read out of,
; memory another owner already holds - tens of milliseconds each, with IF = 1.
;
; What makes that unreachable is not the gfx lock (a task_yield spin mutex
; excludes other DRAWING, not preemption). It is that EVERY mem_claim in the
; tree is either a UI-task action or made under the gfx lock, and a raise is a
; UI-task action under that same lock - so no second task is inside mem_claim
; while the first is inside gfx_save. A package worker that claims memory
; directly would break it. That is the rule to keep, and it is why a worker
; grows through OSAPI_MEM_REGROW (which never sheds) rather than claim-copy-
; free: SPEC.md 50.6's contract rests on it.
; -----------------------------------------------------------------------------
mem_shed_one:
    push ax
    push bx
    push cx
    push si
    pushf
    cli
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [ss:si+MC_SEG], 0
    je .next
    cmp byte [ss:si+MC_OWN+1], MEM_PG_HI
    jne .next
    mov bx, [ss:si+MC_OWN]
    call mem_pg_forget          ; the owner is told BEFORE the block goes, so
                                ; there is no window in which the word names
                                ; memory somebody else already holds
    mov word [ss:si+MC_SEG], 0
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
    pop bx
    pop ax
    ret

; mem_pg_forget - zero the word mem_pg_own names for tag BX
; preserves every register and the flags
mem_pg_forget:
    pushf
    push ax
    push bx
    push si
    mov si, mem_pg_own
.lp:
    mov ax, [si]
    or ax, ax
    jz .out
    cmp ax, bx
    je .hit
    add si, 4
    jmp short .lp
.hit:
    mov bx, [si+2]
    mov word [bx], 0
.out:
    pop si
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; mem_runfree - is the paragraph run [CX, DI) clear of every live claim?
; in:  CX = first paragraph, DI = one past the last, [mem_skip] = a record to
;      ignore (0 = ignore none)
; out: CF = 0 the run is free, CF = 1 something is in it
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
mem_runfree:
    push ax
    push bx
    push dx
    mov bx, mem_tab
    mov dx, MEM_MAX
.next:
    cmp bx, [mem_skip]
    je .skip
    cmp word [ss:bx+MC_SEG], 0
    je .skip
    mov ax, [ss:bx+MC_SEG]
    cmp ax, di
    jae .skip                   ; it starts at or past our end
    add ax, [ss:bx+MC_PARA]
    cmp ax, cx
    ja .busy                    ; ...and it ends past our start: overlap
.skip:
    add bx, MC_SIZE
    dec dx
    jnz .next
    clc
    jmp short .out
.busy:
    stc
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_hifit - the HIGHEST free run of AX paragraphs
; in:  AX = paragraphs
; out: CF = 0 and CX = its base; CF = 1 nothing that big is free
; clobbers: CX (the output), CF
;
; Highest and not lowest on purpose: the one caller is a claim being GROWN,
; and a block at the top has the rest of the heap's free space above it, so
; the next grow extends in place instead of moving again. The low holes stay
; where they are for claims that are not growing.
;
; The only candidate bases worth trying are the heap's floor and the end of
; each live claim - a gap starts at one of those or nowhere - so this is
; MEM_MAX candidates against MEM_MAX claims, not a walk of the heap.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; mem_dmaok - would a block based at CX keep [mem_dma] paragraphs inside one
;             64KB physical page? (module internal)
; in:  CX = candidate base, [mem_dma] = the head in paragraphs (0 = no rule)
; out: CF = 0 fine; CF = 1 it would straddle
; clobbers: nothing else (flags)
;
; mem_claim's scan does this inline because it also has to BUMP to the next
; page floor; mem_hifit only has to reject, so it asks here. One predicate,
; two callers, and the second one is the whole reason a moved claim keeps the
; property it was granted under.
; -----------------------------------------------------------------------------
mem_dmaok:
    push ax
    cmp word [mem_dma], 0
    je .ok
    mov ax, cx
    and ax, 0x0FFF              ; paragraphs into this 64KB physical page
    add ax, [mem_dma]
    cmp ax, 0x1000              ; the head has to END inside the same page
    ja .no
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop ax
    ret

mem_hifit:
    push ax
    push bx
    push dx
    push di
    push si
    mov word [mem_skip], 0      ; a destination may overlap nothing at all
    xor si, si                  ; SI = best base so far, 0 = none
    mov bx, mem_tab
    mov cx, [mem_base]          ; the first candidate is the heap's floor
.cand:
    mov di, cx
    add di, ax
    jc .step                    ; wrapped
    cmp di, [mem_top]
    ja .step
    call mem_runfree
    jc .step
    call mem_dmaok              ; a constrained block may not straddle a 64KB
    jc .step                    ; page here any more than at its first claim
    cmp cx, si
    jbe .step
    mov si, cx                  ; a higher one that fits
.step:                          ; ...then the end of each live claim, and the
    cmp bx, mem_tab + MEM_MAX*MC_SIZE   ; table bound is the loop bound: a
    jae .done                           ; count could walk one record past it
    cmp word [ss:bx+MC_SEG], 0
    je .skiprec                 ; a free record has no end to try
    mov cx, [ss:bx+MC_SEG]
    add cx, [ss:bx+MC_PARA]
    add bx, MC_SIZE
    jmp short .cand
.skiprec:
    add bx, MC_SIZE
    jmp short .step
.done:
    mov cx, si
    or si, si
    jz .none
    clc
    jmp short .out
.none:
    stc
.out:
    pop si
    pop di
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_bcopy - move CX paragraphs from segment SI to segment DI
; in:  SI = source base segment, DI = destination base segment, CX = paragraphs
; out: nothing; preserves every register
;
; Segment-stepped, 1KB at a time, so a block bigger than 64KB moves without a
; 16-bit offset ever binding - the dskw_rdata idiom (SPEC.md 18.4.1). The
; two blocks never overlap: mem_hifit treats the source as occupied, so the
; destination it returns cannot cover it, and a plain forward copy is safe.
; -----------------------------------------------------------------------------
mem_bcopy:
    push ax
    push cx
    push si
    push di
    push ds
    push es
.chunk:
    jcxz .done
    mov ax, cx
    cmp ax, 64                  ; 64 paragraphs = 1KB per pass
    jbe .last
    mov ax, 64
.last:
    sub cx, ax
    push cx
    push si
    push di
    mov ds, si
    mov es, di
    xor si, si
    xor di, di
    mov cx, ax
    shl cx, 1                   ; paragraphs -> words (x16 bytes / 2)
    shl cx, 1
    shl cx, 1
    cld
    rep movsw
    pop di
    pop si
    pop cx
    add si, ax                  ; ...and both bases step by what moved
    add di, ax
    jmp short .chunk
.done:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_regrow - resize a claim you already hold (SPEC.md 50.3)
; in:  DX = the claim's current base segment, AX = the new size in KB,
;      BX = the owner word (the API stub supplies it)
; out: CF=0 and DX = the claim's base NOW - unchanged when it grew where it
;      stood, different when it had to move, and the contents came with it
;      either way. CF=1 and the old claim is untouched at its old base.
; clobbers: DX (the output), CF
;
; WHY THIS EXISTS. Growing a claim used to mean claim-new, copy, free-old,
; which needs old + new free AT ONCE and contiguously - so a heap with plenty
; of total room refused a resize it could afford, and the app reported "not
; enough memory" over hundreds of free KB. Three paths, and the first two
; move nothing:
;
;   1. shrink or level -> the record's length changes and that is all.
;   2. grow with free paragraphs directly above -> extend in place. This is
;      the case that matters: it needs only the DIFFERENCE free, not old +
;      new, and it is what a canvas dragged bigger hits nearly every time.
;   3. grow with something above -> a new block, HIGHEST-fit so the grower
;      lands at the top with the free space above it, the kernel copies, and
;      the old block is freed. Only this path pays for a move, and it is the
;      same copy the caller used to make itself.
;
; WHAT IT IS NOT. It does not compact the heap by sliding OTHER claims down,
; and it cannot: a claim's base lives in its holder's own bss - a package's
; [pt_base], the kernel's [bb_seg] - and nothing here can reach in and
; rewrite those. Moving somebody else's block hands them a pointer into
; memory that is no longer theirs. Real compaction needs a relocation
; callback every holder implements; until there is one, the fix is to stop
; CREATING the fragmentation, which is what path 2 does.
; -----------------------------------------------------------------------------
mem_regrow:
    push ax
    push bx
    push cx
    push si
    push di
    pushf
    cli
    or ax, ax
    jz .fail
    mov cl, 6
    shl ax, cl                  ; AX = paragraphs wanted
    jc .fail

    mov si, mem_tab             ; it must be a base, and yours
    mov cx, MEM_MAX
.find:
    cmp word [ss:si+MC_SEG], 0
    je .fnext
    cmp [ss:si+MC_SEG], dx
    jne .fnext
    cmp [ss:si+MC_OWN], bx
    je .found
.fnext:
    add si, MC_SIZE
    loop .find
    jmp short .fail
.found:
    cmp ax, [ss:si+MC_PARA]
    ja .grow
    mov [ss:si+MC_PARA], ax        ; 1. shrink or level: the tail is free now
    jmp short .ok

.grow:
    mov di, dx                  ; 2. does it fit where it stands?
    add di, ax
    jc .move
    cmp di, [mem_top]
    ja .move
    mov [mem_skip], si          ; our own paragraphs are not in the way
    mov cx, dx
    add cx, [ss:si+MC_PARA]        ; CX = our end = the extension's start
    call mem_runfree
    mov word [mem_skip], 0
    jc .move
    mov [ss:si+MC_PARA], ax        ; it does: extend, copy nothing
    jmp short .ok

.move:                          ; 3. the highest block that will take it
    push ax                     ; the constraint this block was GRANTED under
    mov ax, [ss:si+MC_DMA]         ; travels with it: mem_hifit rejects a base
    mov [mem_dma], ax           ; whose head would straddle, exactly as the
    pop ax                      ; first claim did. Paths 1 and 2 need no such
                                ; care - the rule is on the HEAD, so a shrink
                                ; and an extend upward both keep it
    call mem_hifit              ; CX = its base
    pushf
    mov word [mem_dma], 0       ; a transient, and this window is the only
    popf                        ; place it may be set (the whole op is IF=0)
    jc .fail
    push ax
    push cx
    mov di, cx
    mov cx, [ss:si+MC_PARA]        ; only what is in use travels
    mov si, dx
    call mem_bcopy
    pop cx
    pop ax
    mov si, mem_tab             ; re-find: SI carried the source segment
.rfind:                         ; bounded, though it cannot miss - the record
    cmp si, mem_tab + MEM_MAX*MC_SIZE   ; was found once already and nothing
    jae .fail                           ; since could clear it
    cmp [ss:si+MC_SEG], dx
    jne .rnext
    cmp [ss:si+MC_OWN], bx
    je .rgot
.rnext:
    add si, MC_SIZE
    jmp short .rfind
.rgot:
    mov [ss:si+MC_SEG], cx         ; the record follows the bytes; MC_DMA is
    mov [ss:si+MC_PARA], ax        ; already right - it never changed
    mov dx, cx                  ; ...and the caller is told where it went
.ok:
    popf
    clc
    jmp short .out
.fail:
    popf
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_free - release a block (SPEC.md 50.3)
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
    cmp word [ss:si+MC_SEG], 0
    je .next                    ; a free record can match a DX of 0
    cmp [ss:si+MC_SEG], dx
    jne .next
    cmp [ss:si+MC_OWN], bx
    jne .next
    mov word [ss:si+MC_SEG], 0     ; the record IS the allocator (SPEC.md 50.3)
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
; mem_free_owner - release every claim an owner holds (SPEC.md 50.4)
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
    cmp word [ss:si+MC_SEG], 0
    je .next
    cmp [ss:si+MC_OWN], bx
    jne .next
    mov word [ss:si+MC_SEG], 0
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
    mov bx, ax                  ; a built-in's claims are stamped with its
    call mem_free_owner         ; instance slot (the Disk window's cache)
    test byte [di+I_KIND], KIND_PKG
    jz .done
    mov bx, [di+I_SPTR]         ; ...and a package's with its segment, which
    call mem_free_owner         ; is what it could name itself by
.done:
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
; The query a package sizes itself from (SPEC.md 50.3). Walking holes
; without sorting: for each candidate base (the heap base, then every
; claim's end), measure the distance to the nearest claim that starts above
; it. O(MEM_MAX^2) with MEM_MAX = 32 - around a thousand cycles, called at
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
    cmp word [ss:di+MC_SEG], 0
    je .cnext
    mov dx, [ss:di+MC_SEG]
    add dx, [ss:di+MC_PARA]
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
    cmp word [ss:si+MC_SEG], 0
    je .tnext
    sub bx, [ss:si+MC_PARA]
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
    cmp word [ss:si+MC_SEG], 0
    je .next
    mov ax, [ss:si+MC_SEG]
    cmp ax, dx
    jb .below
    cmp ax, cx
    jae .next
    mov cx, ax                  ; nearer ceiling
    jmp short .next
.below:                         ; a claim that starts below DX still blocks
    add ax, [ss:si+MC_PARA]        ; us if it ENDS above it - only possible when
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
; mem_claim_get - read one claim record by index (SPEC.md 50.6)
; in:  AX = record index (0..MEM_MAX-1)
; out: CF = 0 and DX = base segment, CX = paragraphs, BX = owner word;
;      CF = 1 if that record is free
; clobbers: BX/CX/DX (outputs) and the flags
;
; The Task Manager's memory map walks these to draw every live claim at its
; real address - the one thing the old map could not do, because the blocks
; it drew were constants and there was nothing to walk.
;
; This is the ONLY place in the module that turns a record INDEX into an
; address; every other loop steps by MC_SIZE and cannot drift. It used to
; open-code the stride as an add-chain commented "*6 = MC_SIZE", and when
; MC_DMA widened the record to 8 the comment stayed true and the code did
; not: every record after the first was read straddling two, so the map drew
; claims at invented addresses and invented sizes - one of them across nearly
; the whole of conventional memory - while every figure in the window, which
; comes from the stepping loops, stayed correct. It multiplies by the
; constant now.
; -----------------------------------------------------------------------------
mem_claim_get:
    push si
    cmp ax, MEM_MAX
    jae .none
    push ax
    mov si, MC_SIZE
    mul si                      ; DX:AX = index * MC_SIZE, both small
    mov si, ax
    pop ax
    add si, mem_tab
    cmp word [ss:si+MC_SEG], 0
    je .none
    mov dx, [ss:si+MC_SEG]
    mov cx, [ss:si+MC_PARA]
    mov bx, [ss:si+MC_OWN]
    clc
    pop si
    ret
.none:
    stc
    pop si
    ret

; -----------------------------------------------------------------------------
; mem_owned_kb - how many KB does one owner hold? (SPEC.md 50.6)
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
    cmp word [ss:si+MC_SEG], 0
    je .next
    cmp [ss:si+MC_OWN], bx
    jne .next
    add ax, [ss:si+MC_PARA]
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
; sum in AX and have no spare register to hold a partial total in.
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
    cmp word [ss:si+MC_SEG], 0
    je .next
    cmp byte [ss:si+MC_OWN+1], MEM_PG_HI  ; PURGEABLE is in nobody's total, not
    je .next                        ; even System's (SPEC.md 50.6.3). It is not
                                    ; memory anyone can run out of - the next
                                    ; claim that needs it takes it - so
                                    ; counting it as used would be the
                                    ; misleading answer, not the honest one
    or bx, bx
    jz .take
    cmp byte [ss:si+MC_OWN+1], 0xFF    ; the kernel tags are 0xFFxx; an instance
    je .take                        ; slot is 0..INST_MAX-1
    push bx                         ; ...and a LOADED DRIVER's own buffers,
    mov bx, [ss:si+MC_OWN]             ; which name themselves by the segment the
    call drv_owns_seg               ; driver runs in and so match neither test
    pop bx                          ; above. They belong under System, where
    jc .next                        ; the driver's image already is (51.3) -
                                    ; without this they were in the totals and
                                    ; in the map, and in no row at all
.take:
    add dx, [ss:si+MC_PARA]
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
    cmp word [ss:si+MC_SEG], 0
    je .next
    add ax, [ss:si+MC_PARA]
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
; The API slot targets (SPEC.md 20.3/50.3).
;
; A package does not name itself: its SEGMENT is its identity, and the API
; stub hands that over in ES (SPEC.md 20.3). Nothing can be spoofed - a
; package cannot forge the CS/DS it runs in - and, unlike a window-pointer
; fence, it works from the ENTRY PROC, where there is no window and no
; published instance yet. That matters: sizing yourself to the memory you can
; actually get is the first thing an app does, before it creates its window.
; =============================================================================

; -----------------------------------------------------------------------------
; osapi_mem_claim - in: AX = KB, ES = the caller's segment (the stub's doing)
;                   out: CF = 0 and DX = segment, CF = 1 refused
;
; The plain claim IS the DMA claim with no head to constrain, so it borrows the
; body rather than repeating it - and it has to bank CX, which is the head
; parameter there and one of the registers this slot promises to preserve.
; -----------------------------------------------------------------------------
osapi_mem_claim:
    push cx
    xor cx, cx                  ; no head: no 64KB page constraint
    call osapi_mem_claim_dma
    pop cx                      ; pop leaves CF alone, and so does ret
    ret

; -----------------------------------------------------------------------------
; osapi_mem_claim_dma - in: AX = KB, CX = KB of the head that must not cross a
;                       64KB physical boundary, ES = the caller's segment
;                       out: CF = 0 and DX = segment, CF = 1 refused
;
; What an ISA DMA buffer asks for (SPEC.md 50.3/51.6). A driver reaches it
; through the same slot table as everything else: mem_own accepts a driver's
; image segment as an owner exactly as it accepts a package's region.
; -----------------------------------------------------------------------------
osapi_mem_claim_dma:
    push bx
    call mem_own                ; BX = owner word = the calling segment
    jc .fail
    call mem_claim_dma
    jmp short .out
.fail:
    xor dx, dx
    stc
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; osapi_mem_free - in: DX = segment, ES = the caller's segment
;                  out: CF = 0 released, CF = 1 refused (not yours)
; -----------------------------------------------------------------------------
osapi_mem_free:
    push bx
    call mem_own
    jc .out                     ; CF = 1 already
    call mem_free
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; osapi_mem_regrow - in: DX = the claim, AX = new size in KB, ES = the
;                    caller's segment; out: mem_regrow's, CF=1 if not yours
;
; The slot needs this wrapper for the same reason osapi_mem_free does, and
; went without it: mem_regrow's fence is `cmp [ss:si+MC_OWN], bx`, and nothing
; put an owner in BX. OSAPI_XSTUB stamps ES from the caller's DS and stops
; there, so what the fence actually compared was whatever the package left in
; BX - a size, a loop counter - against an owner segment. It never matched,
; so the slot ALWAYS answered CF=1 and no package could resize a claim.
;
; apps/paint is the one shipped caller and passes BX = the KB it wants, so
; its fragmentation fallback - the entire reason mem_regrow was written - has
; never once run. That is the failure mode of a fence with an implicit input:
; the refusal is indistinguishable from an honest "no room", which is exactly
; what the caller reports and the user believes.
; -----------------------------------------------------------------------------
osapi_mem_regrow:
    push bx
    call mem_own                ; BX = the owner ES names (CF=1 = not a
    jc .out                     ; region or driver image we know)
    call mem_regrow
.out:
    pop bx                      ; pop leaves CF and DX alone
    ret

; -----------------------------------------------------------------------------
; osapi_mem_avail - in: nothing; out: AX = largest free KB, BX = total free KB
; -----------------------------------------------------------------------------
osapi_mem_avail:
    jmp mem_avail

; -----------------------------------------------------------------------------
; osapi_cm_alloc / osapi_cm_free / osapi_cm_caps - main's v3 arena slots
; (SPEC.md 20.8), answered by the claim heap
;
; main numbers these 0x01B8/0x01C0/0x01C8 and counts in PARAGRAPHS; the heap
; counts KB. The wrappers convert - a paragraph count rounds UP to whole KB,
; so a grant is never smaller than asked - and keep main's register contracts
; exactly: cm_alloc answers in AX (not DX) and fails with main's code in AX,
; cm_free takes AX and preserves it, cm_caps answers AX/DX/BL preserving BH.
; A package built against main's SDK runs on this kernel unchanged; one built
; against this SDK uses the KB slots directly (SPEC.md 50.3).
; -----------------------------------------------------------------------------
osapi_cm_alloc:
    push cx
    push dx
    add ax, 63                  ; paragraphs -> KB, rounded up; 0 asks for
    jc .refuse                  ; 1KB (main's "0 is rounded up to 1"), and a
    mov cl, 6                   ; carry is >0xFFC0 paragraphs - nothing that
    shr ax, cl                  ; big exists below 1MB
    or ax, ax
    jnz .ask
    inc ax
.ask:
    call osapi_mem_claim        ; CF/DX by the heap's rules, ES the identity
    jc .refuse
    mov ax, dx                  ; main's contract: AX = the base segment
    pop dx
    pop cx
    ret
.refuse:
    xor ax, ax                  ; main's failure codes: 0 = no arena at all,
    mov dx, [mem_top]           ; 1 = no record, 2 = no run. The heap does
    cmp dx, [mem_base]          ; not tell 1 from 2 apart (and no shipped
    je .code                    ; caller ever did): 2, or 0 on a machine
    mov ax, 2                   ; whose heap is empty
.code:
    pop dx
    pop cx
    stc
    ret

osapi_cm_free:
    push dx                     ; in: AX = the base segment; out: CF, with
    mov dx, ax                  ; AX preserved - main's contract, so the
    call osapi_mem_free         ; heap's DX-in shape is hidden here
    pop dx
    ret

osapi_cm_caps:
    push cx
    push si
    mov ch, bh                  ; BH survives in CH (CX itself is banked)
    call mem_avail              ; AX = largest free KB, BX = total free KB
    mov dx, bx
    mov cl, 6
    shl ax, cl                  ; KB -> paragraphs: a 640KB heap is 0xA000
    shl dx, cl                  ; paragraphs, so the shifts never carry
    xor bx, bx                  ; BL = free claim records (main: free
    mov si, mem_tab             ; block-table entries)
.rec:
    cmp word [ss:si+MC_SEG], 0
    jne .used
    inc bx
.used:
    add si, MC_SIZE
    cmp si, mem_tab + MEM_MAX*MC_SIZE
    jb .rec
    mov bh, ch                  ; the caller's BH back (BL <= MEM_MAX)
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; mem_own - the calling package's owner word (module-internal)
; in:  ES = the caller's segment, stamped by the API stub
; out: CF = 0 and BX = the owner word (that same segment); CF = 1 if the
;      caller is not a loaded package
; clobbers: BX (the output), flags
;
; It used to be a range test - a segment inside the package pool was a loaded
; package and nothing else could be. There is no pool now, so the question is
; asked of the claim map instead: **ES is a package if a live claim STARTS at
; it and that claim is owned by an instance slot**, which is exactly how a
; region is allocated (loader_run step 5) and what nothing else looks like.
; Data claims carry a segment or a 0xFFxx tag as their owner, so the second
; half of the test is what separates a region from anything else that might
; happen to begin at the same paragraph.
;
; **It has to answer during the ENTRY PROC**, which is where an app sizes
; itself - and the entry runs at loader_run step 8, BEFORE I_STATE and I_SPTR
; are published at step 9. So this cannot be an inst_tab scan, however much it
; reads like one. The region claim exists from step 5, which is why the claim
; map is the right table to ask.
;
; No weaker than the range test it replaces: ES is stamped by the API stub
; from the caller's own DS, so a package can only name a segment it is
; actually executing at, and two instances of one package live at different
; bases and so remain distinct owners.
; -----------------------------------------------------------------------------
mem_own:
    push cx
    push si
    mov bx, es
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp [ss:si+MC_SEG], bx         ; a claim that starts exactly here...
    jne .next
    cmp word [ss:si+MC_OWN], INST_MAX   ; ...owned by an instance slot: a
    jb .yes                          ; PACKAGE's region...
    cmp word [ss:si+MC_OWN], MEM_K_DRV  ; ...or by the driver tag: a DRIVER's
    je .yes                          ; image, which is the same kind of thing
                                     ; and the same kind of caller. Without
                                     ; this a driver cannot claim the bulk
                                     ; buffers its own SDK tells it to claim -
                                     ; it has no instance, so the test above
                                     ; can never say yes about it
.next:
    add si, MC_SIZE
    loop .scan
    pop si
    pop cx
    stc
    ret
.yes:
    pop si
    pop cx
    clc
    ret

; =============================================================================
; osapi_claim_snapshot - API slot 0x02A0: the whole claim table into the caller's
;                    buffer (SPEC.md 20.9/50.5)
;
; in:  ES:DI = a CLAIM_SNAPSHOT_SIZE buffer the caller owns
; out: AX = MEM_MAX; the buffer filled; DI unchanged
; clobbers: AX (output) and the flags
;
; Whole rather than per record because both things a memory map does with
; this table want all of it: it walks every record to draw a band, and hashes
; every record to decide whether any band moved. Per record that is MEM_MAX
; far calls twice over; here it is one call and a near walk of the copy.
;
; No cli. A claim is published by a single word store (MC_SEG last), so a
; record is either there or not, and a torn table would at worst draw one
; band wrong for half a second - which is not what osapi_sys_snapshot's window is
; guarding against and does not deserve the same price.
; =============================================================================
osapi_claim_snapshot:
    push cx
    push dx
    push si
    push di
    mov si, mem_tab
    mov cx, MEM_MAX
.rec:
    mov ax, [ss:si+MC_SEG]
    mov [es:di+CLS_SEG], ax
    mov dx, [ss:si+MC_PARA]
    mov [es:di+CLS_PARA], dx
    mov ax, [ss:si+MC_OWN]
    mov [es:di+CLS_OWN], ax
    add si, MC_SIZE
    add di, CLS_RECSZ
    loop .rec
    mov ax, MEM_MAX
    pop di
    pop si
    pop dx
    pop cx
    ret

; =============================================================================
; osapi_sys_kb - API slot 0x02A8: what the KERNEL occupies and what the heap
;                holds, in KB (SPEC.md 20.9/2/50)
;
; in:  ES:DI = a SYSKB_SIZE buffer the caller owns
; out: the buffer filled; every register preserved
;
; The first seven figures were assembly-time constants of the kernel's own
; until a task monitor stopped being part of it, and that is precisely what a
; program outside cannot have: KERN_KB moves with every build, and a package
; carrying last month's number would report a footprint the machine does not
; have. They still ARE constants - this call just puts them where something
; else can read them.
;
; SK_IMG deliberately covers the COLD SEGMENT as well as the image and .bss.
; Cold code is code; it is resident for the whole session and it is inside
; the span SK_KERN measures, so leaving it out made the parts sum three
; kilobytes short of the whole and no row on any screen said where they went.
;
; SK_IMG and SK_DSK are RESIDUALS, and that is what makes the parts add up.
; Every rung of the ladder is a whole number of 512-byte sectors (SPEC.md 2),
; so half of them are an odd half-kilobyte and four independently rounded
; parts can lose two kilobytes against a total that rounds once. So the two
; least interesting figures absorb it: SK_IMG is the whole span less the
; buffers, and SK_DSK is the buffers less the FAT window and the stacks -
; which is exactly what "whatever else is in .lowbss" already meant.
; =============================================================================
SK_STK_KB  equ ((MAX_TASKS-1) * SCH_STACK + 1023) / 1024
SK_STK0_KB equ (STK0_SIZE + 1023) / 1024
SK_FAT_KB  equ (FAT_PARA * 16 + 1023) / 1024

osapi_sys_kb:
    push ax
    push bx
    push di
    mov word [es:di+SK_KERN], KERN_KB
    mov word [es:di+SK_BUF],  KBUF_KB
    mov word [es:di+SK_IMG],  KERN_KB - KBUF_KB
    mov word [es:di+SK_FAT],  SK_FAT_KB
    mov word [es:di+SK_STK],  SK_STK_KB
    mov word [es:di+SK_STK0], SK_STK0_KB
    mov word [es:di+SK_DSK],  KBUF_KB - SK_FAT_KB - SK_STK_KB - SK_STK0_KB
    push di
    call mem_total_kb           ; AX = claimed KB, BX = heap size KB
    pop di
    mov [es:di+SK_CLAIM], ax
    mov [es:di+SK_HEAP], bx
    xor ax, ax
    push di
    call mem_kernel_kb          ; AX += the kernel's own tagged claims
    pop di
    mov [es:di+SK_KCLAIM], ax
    mov ax, [xm_kb]             ; the pool above 1MB, 0 on tier 0 (SPEC.md 41)
    mov [es:di+SK_XMS], ax
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .lowbss                 ; LOW_SEG (SPEC.md 2.1). The claim map is
                                ; scanned through a pointer and never
                                ; addressed by name outside this module, so it
                                ; costs one ss: prefix per field and nothing in
                                ; the kernel's own 64KB segment - which since
                                ; hard-disk support is the binding guard
mem_tab:  resb MEM_MAX * MC_SIZE  ; the claim map (layout above)

section .bss


mem_ceil: resw 1                  ; the highest paragraph the scan may reach:
                                  ; [mem_top], or the lowest region base for a
                                  ; purgeable claim (SPEC.md 50.6.1). Staged
                                  ; inside the cli window with the other two
mem_dir:  resw 1                  ; 0 = mem_claim (bottom up), 1 = mem_claim_hi
                                  ; (top down, a package's region). A word so
                                  ; the entries can stage it in a register and
                                  ; store it INSIDE the cli window - it is a
                                  ; global only because the scan reads it per
                                  ; candidate, never a parameter channel
mem_dma:  resw 1                  ; ...and mem_claim_dma's head, in paragraphs,
                                  ; for the same reason: 0 = no page constraint
mem_base: resw 1                  ; first heap paragraph = HEAP_SEG
mem_skip:   resw 1              ; mem_runfree: a record to ignore, 0 = none
mem_top:  resw 1                  ; one past the last, from int 12h

section .text
