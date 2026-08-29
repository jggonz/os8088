; =============================================================================
; os8088 - memory.inc
;
; The claim heap (SPEC.md 50): every paragraph of conventional memory above
; the package pool, handed out in KB-granular, segment-aligned blocks to
; whoever asks - a package through the API slots, or the kernel itself for
; the things it uses speculatively (the menu save-under, a Disk window's
; listing cache, the clipboard).
;
; Why this exists. Before it, memory outside the pool was carved up by
; CONSTANTS - SND_SEG, SAVE_SEG, VIEW_SEG, BB_SEG - each claimed whole from
; boot whether or not a byte of it was ever written, and a package that
; wanted more than its 19.5KB region had no way to ask: apps/paint simply
; took linear 0x66000 and hoped, reading a kernel policy constant to guess
; whether SPEC.md 32's then-live back buffer would ever want it
; (docs/PAINT-NOTES.md records
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
MC_RLOC  equ 8                  ; word: the RELOCATION HANDLE (SPEC.md 66).
                                ; 0 = PINNED, which is the default and what
                                ; every claim was before compaction existed;
                                ; otherwise the near offset of the holder's
                                ; relocation proc, called through the owner's
                                ; dispatcher when the compactor moves the
                                ; block. It is a PROC and not the address of
                                ; the word naming the block, which was the
                                ; first design and is not enough for either of
                                ; the two consumers that matter: the Disk
                                ; window's view cache has TWO naming words
                                ; (FS_VSEG and the [fm_vseg] mirror every
                                ; reader looks at) and Tracker normalises 32
                                ; sample segments off its module base, none of
                                ; which a poke at [trk_modseg] reaches
MC_SIZE  equ 10

MEM_K_SAVE equ 0xFF01           ; kernel tags (SPEC.md 50.2): menu save-under
MEM_K_DRV  equ 0xFF03           ; a loaded driver's image (SPEC.md 51.3)
MEM_K_COPY equ 0xFF04           ; the file manager's copy buffer (22.3): held
                                ; for exactly one Cut/Copy/Paste operation,
                                ; including the part of it that is suspended
                                ; on an overwrite question
MEM_K_ASC  equ 0xFF06           ; one volume's ASSOC.DAT cache (SPEC.md
                                ; 54.7) - shared, reloaded on a volume switch
MEM_K_FATW equ 0xFF05           ; one volume's private FAT window (18.8.1),
                                ; held for as long as that volume is mounted
MEM_K_MOD  equ 0xFF08           ; an on-demand kernel module's image (SPEC.md
                                ; 2.8) - claimed top-down like a driver's,
                                ; because its base IS its CS and so it can
                                ; never move, and freed by the feature that
                                ; asked for it rather than by a teardown
MEM_K_CLONE equ 0xFF09          ; the disk cloner's buffer (SPEC.md 18.99) -
                                ; the job block AND the sectors in flight, so
                                ; freeing it is the whole of the operation's
                                ; teardown; held for exactly one clone
; 0xFF0B WAS MEM_K_OVL, the relocated boot overlay - a CS like MEM_K_DRV's,
; claimed high at MARK 12 and freed after drv_boot. The overlay rides stage 2's
; blob now (SPEC.md 2.9.6), which is the same lifetime at a fixed address and
; no claim at all, so the tag is retired. It is LEFT UNUSED rather than reused:
; a value that has meant one thing in a shipped kernel and something else in
; the next is exactly the kind of collision a debug read cannot see.
MEM_K_BAND equ 0xFF0A           ; the 1bpp band composer's buffer (SPEC.md
                                ; 5.9.2) - taken ONCE at boot and never freed,
                                ; because a composer with nowhere to compose
                                ; is a composer that silently stops composing.
                                ; Claimed rather than declared in .bss for the
                                ; obvious reason: 2KB of KERN_BUDGET buys a
                                ; 128px band and 2KB of the arena buys a band
                                ; as wide as the screen, and SPEC.md 5.9.1's
                                ; cost is per CHUNK
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
; zero in that word must ALREADY mean "no buffer, do it the slow way" - "one
; word" being per BLOCK, not per tag: a tag may name an ARRAY of them and add
; an ordinal to itself, which is how there is one raise cache per window
; rather than one for the machine (SPEC.md 11.96.3). That is
; the whole contract, and it is what lets the shed skip a notification
; protocol entirely: mem_shed_one zeroes the word through mem_pg_own before it
; frees the block, so the consumer's existing refusal path is the notice.
; menu_drop's `or ax, ax / jz .nosave` is the shape - every purgeable consumer
; has to already have one.
; THE HIGH BYTE IS THE PRIORITY, and higher means harder to take (SPEC.md
; 50.6.4). It was one value meaning "purgeable", which made every cache worth
; the same as every other - so a WINDOW RAISE CACHE, whose loss is one
; redraw, could evict the DIRECTORY READ-AHEAD WINDOW, whose loss triples an
; install's metadata I/O. Both were "a cache"; they are not the same thing to
; the person waiting.
;
; The scale runs up to the ordinary claim, which is what makes the test one
; compare: a record is takeable by a claimant of level L when its high byte
; is >= MEM_PG_MIN (so it is a cache at all) and < L. An ordinary claim ranks
; MEM_LVL_TOP and so takes any cache; a cache ranks itself and so takes only
; cheaper ones; and NOTHING takes a claim that is not a cache, because 0xFF
; and every instance slot fall outside the range by construction.
MEM_PG_TRIV equ 0xFB            ; losing it costs a redraw or a recompute
MEM_PG_LOW  equ 0xFC            ; ...a little I/O, or a visible pause
MEM_PG_MED  equ 0xFD            ; ...seconds of work the user waits through
MEM_PG_HIGH equ 0xFE            ; ...a long operation getting much longer
MEM_PG_MIN  equ MEM_PG_TRIV     ; the range, for the two-compare test
MEM_PG_MAX  equ MEM_PG_HIGH
MEM_LVL_TOP equ 0xFF            ; an ordinary claim: UNPURGEABLE, the default,
                                ; and the only rank that outranks every cache

MEM_P_WSAVE equ MEM_PG_TRIV<<8 | 0x10   ; a window's raise cache (SPEC.md
                                ; 11.96): TRIVIAL, because what it costs to
                                ; lose is one window redrawing itself, which
                                ; is what every window did before it existed.
                                ; IT IS A RANGE, not a tag - the window's slot
                                ; is added to it, so there is one cache per
                                ; window and 0x10..0x1F is reserved for them
                                ; (SPEC.md 11.96.3). Low bytes are per-rank,
                                ; so a future TRIVIAL tag must avoid that
                                ; block; the ranks below are free of it
MEM_P_DIRW  equ MEM_PG_HIGH<<8 | 0x02   ; the directory read-ahead window:
                                ; HIGH, because it is the difference between
                                ; 316 int 13h calls and 114 on an install
                                ; (SPEC.md 18.95), which on the target machine
                                ; is measured in seconds the user watches
                                ; (SPEC.md 19.2.3) - its zero already means
                                ; "read one sector at a time", which is what
                                ; every directory walk did before it existed

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
SK_CLAIM   equ 16               ; word: claimed out of it, every owner, and
                                ; never a purgeable claim (SPEC.md 50.6.3)
SK_KCLAIM  equ 18               ; word: ...of which the kernel's own tags
SK_XMS     equ 20               ; word: the pool above 1MB, 0 on tier 0 (41)
SYSKB_SIZE equ 22
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


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
mem_init_x:
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

    call mem_floor_ax           ; THE FLOOR (SPEC.md 39.22), and it is a proc
                                ; rather than eight instructions here because
                                ; mem_unblob below has to arrive at the same
                                ; answer - two copies of a conditional whose
                                ; disagreement would be a mono machine handed
                                ; a VGABUF rung the decoder is still using

    ; --- AND STAGE 2 IS DOWN HERE (SPEC.md 2.9.5) ---------------------------
    ; The loading screen lives in the loader, and the loader copied itself to
    ; HEAP_SEG before it read a sector - so the blob sits on the heap's FLOOR
    ; and is LIVE until spl_finish, which is after drv_boot. The arena
    ; therefore starts above it and the whole top of RAM stays claimable.
    ;
    ; It is ASKED OF [spl_fseg] rather than assumed to be HEAP_SEG, because
    ; that word is what stage 2 actually published and a kernel reached some
    ; other way - a diagnostic payload, a test harness - never wrote it.
    ; COLD_SEG is its seed and is below the heap, so the compare refuses it
    ; and that kernel keeps the whole floor.
    ;
    ; A blob ABOVE the floor is not tested for: stage 2 puts itself exactly on
    ; HEAP_SEG and the floor is HEAP_SEG or one VGABUF rung below it, so the
    ; raise is never a lowering. What IS tested is the seed.
    cmp word [spl_fseg], COLD_SEG
    jbe .nospl
    mov ax, [spl_fseg]
    add ax, BOOT2_PAD / 16      ; ...the paragraph past the blob's last byte
.nospl:
    mov [mem_base], ax
    mov [mem_top], ax           ; an empty heap until int 12h says more

    int 0x12                    ; AX = conventional KB
    mov cl, 6                   ; KB -> paragraphs: AX * 64, and the top
    shl ax, cl                  ; segment IS that paragraph count (640KB ->
    jc .done                    ; 0xA000). A shift out is impossible below
                                ; 1MB, but a hostile BIOS is not our problem
                                ; to crash for
    cmp ax, [mem_base]          ; ...against the FLOOR and not HEAP_SEG, which
    jbe .done                   ; is only its seed now: RAM ending at or below
                                ; the heap means none of it
    mov [mem_top], ax
.done:
    pop es
    pop di
    pop cx
    pop ax
    retf

; -----------------------------------------------------------------------------
; mem_unblob - stage 2's 2KB back, and the hole it leaves closed (SPEC.md
;              50.6.3)
;
; in:       nothing
; out:      nothing (every register preserved; CF is mem_compact's, and no
;           caller has anything to do with it)
;
; Called ONCE, from kmain the line after spl_finish - the moment nothing wants
; the loading screen again. Two halves, and the second is the half that makes
; the first worth anything:
;
;   1. [mem_base] goes back to the floor mem_init worked out, which hands the
;      arena BOOT2_PAD bytes it has never been allowed to see.
;   2. ...and mem_compact packs the movable claims down onto it. Without this
;      the released bytes are a hole UNDER kern:FATW and kern:ASC rather than
;      part of the long run, which is the fragmentation this whole move exists
;      to avoid - the blob would simply have strung the boot out at the other
;      end of the heap.
;
; AX = 0 is mem_compact's "compact regardless": there is no pending claim to
; size the decision against, and the whole point is to close a hole nobody has
; asked for memory across yet.
;
; A kernel whose [spl_fseg] was never written never raised the base in the
; first place, so [mem_floor] equals [mem_base], the store is a no-op and
; mem_compact finds every claim already at its fill point.
; -----------------------------------------------------------------------------
mem_unblob_x:
    push ax
    call mem_floor_ax
    mov [mem_base], ax
    xor ax, ax
    call mem_compact
    pop ax
    retf

; -----------------------------------------------------------------------------
; mem_floor_ax - the heap's lowest paragraph (module internal)
; out: AX = HEAP_SEG, or one VGABUF rung below it on a machine with no VGA
; clobbers: AX and the flags
;
; `.vgabuf` is the planar decoder's two buffers and nothing else, sitting in a
; rung of their own at the TOP of the kernel - so on a machine with no VGA
; they are never reserved and the heap simply starts under them. Nothing moves
; and nothing is claimed: [mem_base] was always a runtime word and HEAP_SEG is
; only what it is seeded with.
;
; It asks [vid_avail] AND NEVER [vid_mono] (SPEC.md 39.22): a machine that
; boots mono can never acquire the VGA bit, there being no probe at 0xA000 -
; but a VGA machine always has VID_A_CGA too and can be SWITCHED to mono, so
; it may never give these back. That is also what makes the answer STABLE
; across a boot, which is what lets mem_unblob ask it a second time.
; vid_probe_avail runs before mem_init in kmain, which is what makes the bit
; readable at all.
; -----------------------------------------------------------------------------
mem_floor_ax:
    mov ax, HEAP_SEG
%if VGABUF_PARA
    test byte [vid_avail], VID_A_VGA
    jnz .out
    sub ax, VGABUF_PARA
.out:
%endif
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
;   claim owner word. SPEC.md 66 compacts the DATA arena and deliberately
;   leaves regions out for exactly this reason.
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
; --- ...AND THE TOP-DOWN DOORS (SPEC.md 50.3.2) ------------------------------
; A claim the HARDWARE addresses can never move: the 8237 holds a sound ring's
; page and offset in its own registers and a NIC's descriptors point at its
; buffers. That is the property mem_claim_hi exists for - "a package region can
; NEVER move, because its base IS its CS" - and 50.3's own warning is what a
; driver's ring was doing in the data arena: "one long-lived data claim landing
; mid-heap permanently splits the space a package can be loaded into".
;
; They were down there because OSAPI_MEM_CLAIM was the only door a driver had.
; These two are the other one. Nothing else changes: the scan, the page bump
; and the record are the same code, reached with mem_dir set.
mem_claim_dma_hi_x:
    push si
    push di
    mov di, 1                   ; ...and this one MEANS top-down, where the
    jmp short mem_claim_dma_x.head  ; door below asks the tag
mem_claim_dma_x:
    push si
    push di
    xor di, di
.head:
    push cx
    mov si, cx
    mov cl, 6
    shl si, cl                  ; head KB -> paragraphs
    pop cx
    jnc .set
    mov si, 0xFFFF              ; a head past 1MB: let the page test refuse it
.set:                           ; rather than wrap into a plausible small one
    jmp short mem_claim_x.go    ; DI is already this door's own answer, and
                                ; there is no longer a TAG that can disagree
                                ; with it: SPEC.md 50.6.1.1's defect was a
                                ; purgeable claim reaching the scan without
                                ; passing the direction test, and 50.6.1 has
                                ; since deleted the test. A door that asks
                                ; nothing cannot skip the asking
mem_claim_hi_x:
    push si
    push di
    xor si, si
    mov di, 1                   ; ...and this one MEANS it: an explicit hi
    jmp short mem_claim_x.go    ; claim is a caller's request, not a tag's
mem_claim_x:
    push si
    push di
    xor si, si
    xor di, di                  ; A PURGEABLE TAG IS AN ORDINARY DATA CLAIM
                                ; (SPEC.md 50.6.1). It used to place itself
                                ; top-down under a ceiling of its own, and
                                ; what that bought was a cache the shed could
                                ; not give back: the read-ahead is claimed at
                                ; the A: mount, BEFORE any driver image
                                ; exists, so the ceiling was [mem_top] and it
                                ; took the roof - after which every region
                                ; packed underneath it and shedding it opened
                                ; an island above them that only another
                                ; top-down claim could ever reach
.go:
    cmp byte [mem_cp_busy], 0   ; INSIDE A COMPACTION - a relocation proc that
    jne .busy                   ; claimed (SPEC.md 66.3 rule 3 forbids it) -
                                ; the map is mid-walk and first fit would hand
                                ; it the very run the walk is about to copy
                                ; the next block onto. Refuse, and refuse the
                                ; shed too, which frees records the walk holds
    call mem_claim_1            ; COMPACT, THEN SHED AND RETRY (SPEC.md 66.4 /
    jnc .out                    ; 50.6.2). A cache loses to anything that is
    call mem_compact            ; not one, and a package load loses to nothing
    jnc .go                     ; - so both are on mem_claim_hi's path too.
    call mem_shed_one           ; Compaction goes FIRST because a shed can
    jnc .go                     ; destroy MEM_P_DIRW, priced at "a long
                                ; operation getting much longer" - seconds of
                                ; int 13h on the field machine - where a
                                ; compaction is a memcpy in tenths of a
                                ; second. Cheapest first is what the priority
                                ; ladder exists to say.
                                ;
                                ; It terminates because each shed removes a
                                ; record and mem_shed_one refuses when there
                                ; are none, and because a compaction packs
                                ; every movable claim against the one below
                                ; it - so the next call finds no movers and
                                ; answers CF = 1 (SPEC.md 66.4)
    jmp short .out
.busy:
    stc
.out:
    pop di
    pop si
    ret

; the scan-and-publish itself; mem_claim above is the retry loop around it
;
; SI AND DI ARE PARAMETERS AND ARE PRESERVED, which they were not: both are
; staged into the globals below and then reused as scan scratch, and the
; epilogue restored only AX and CX. That is invisible on the first call and
; wrong on the SECOND - mem_claim's shed-and-retry re-enters with whatever the
; failed scan left, so the retry stages a record pointer as [mem_dir] (it
; allocates top-down instead of bottom-up) and another as [mem_dma] (it
; invents a 64KB-page constraint nobody asked for). The one path that needs
; the shed most is the one that could not use it: measured on a 256KB
; machine, a 75KB claim failed, shed the 63KB read-ahead window, and failed
; again against 95.5KB of contiguous free heap (SPEC.md 50.6.2).
mem_claim_1:
    push ax
    push cx
    push si
    push di
    pushf
    cli                         ; the whole scan-and-publish, one window -
    mov [mem_dma], si           ; the operation's PARAMETERS included. The
    mov [mem_dir], di           ; entries stage them in registers because a
                                ; global stored BEFORE the cli is another
                                ; task's to overwrite in the gap: a worker's
                                ; DMA head silently dropped, or a region
                                ; claim allocating from the wrong end

    or ax, ax                   ; (the ceiling the scan may not pass is
                                ; [mem_top] for every claim there is now -
                                ; SPEC.md 50.6.1 - so it is read where it is
                                ; used instead of being staged here)
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
    mov dx, [mem_top]           ; the highest base that still fits
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
    cmp cx, [mem_top]
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
    mov word [ss:di+MC_RLOC], 0    ; PINNED until the holder says otherwise
                                ; (SPEC.md 66.2). A record is REUSED - freeing
                                ; is one store to MC_SEG - so a stale handle
                                ; here would let the compactor move a new
                                ; tenant's block and then call the PREVIOUS
                                ; tenant's proc to tell it about it
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
    pop di                      ; the parameters, so a RETRY re-enters with
    pop si                      ; the same ones (pop touches no flag, and CF
    pop cx                      ; is this routine's answer)
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_pg_own - owner -> the ONE kernel word that names that purgeable block
;
; The whole notification protocol (SPEC.md 50.6). Zeroing this word IS the
; notice, because a purgeable consumer's contract is that zero there already
; means "no buffer, do it the slow way" - a path it must already have and
; already test. An owner matching no row here simply keeps its block until it
; is freed properly, which is safe and is why the list ends in a 0 rather than
; an assertion.
;
; A ROW IS A RANGE, and that is what makes one cache per WINDOW possible
; (SPEC.md 11.96.3): the tag is a base, the consumer adds an ordinal to it,
; and the words it names are an array of that many. The lookup is therefore
; `d = owner - tag; if d < count then word = base + d*2` rather than an
; equality - unsigned, so a row whose tag is ABOVE the owner wraps high and
; fails the compare rather than matching backwards into somebody else's array.
;
; The stride is not a column because there is nothing for it to be: every
; purgeable owner word is a WORD, by the contract above.
; -----------------------------------------------------------------------------
PGO_TAG   equ 0                 ; word: the first owner this row answers for
PGO_BASE  equ 2                 ; word: -> its array of naming words
PGO_CNT   equ 4                 ; word: how many (1 = a plain tag)
PGO_SIZE  equ 6
section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)


mem_pg_own:
    dw MEM_P_WSAVE, wm_su_segs,  MAX_WIN
    dw MEM_P_DIRW,  dsk_rah_seg, 1
    dw 0
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. DS is still KERNEL_SEG,
                                ; so the DATA stays in .text at every toggle


; -----------------------------------------------------------------------------
; mem_shed_one - give up the CHEAPEST purgeable block this claimant outranks
; in:  BX = the owner being claimed FOR (mem_claim holds it across the retry)
; out: CF = 0 one was shed, CF = 1 there was nothing it may take
; clobbers: flags
;
; SPEC.md 50.6.4. It used to shed the first purgeable record in table order,
; which is two wrong answers at once: it could take a block MORE valuable than
; the thing being claimed - a window raise cache evicting the directory
; read-ahead window - and, given the choice, it took whichever happened to sit
; lower in mem_tab rather than whichever costs least to lose.
;
; So: one pass, remembering the lowest-priority record strictly below the
; claimant's own rank, and shed that. The rank comes out of BX because the tag
; IS the request (see the constants) - an ordinary claim's high byte is not in
; the purgeable range, so it ranks MEM_LVL_TOP and outranks every cache.
;
; A cache taking a cheaper cache cannot thrash in any way that matters, and
; that is a property of the LEVELS being honest rather than of this routine:
; whatever it takes is by definition cheaper to rebuild than what it is
; taken for.
; -----------------------------------------------------------------------------
mem_shed_one:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    pushf
    cli
    mov ah, MEM_LVL_TOP         ; AH = the claimant's rank: its own high byte
    cmp bh, MEM_PG_MIN          ; when it is a cache, MEM_LVL_TOP when it is
    jb .rank                    ; an ordinary claim (which is every high byte
    cmp bh, MEM_PG_MAX          ; outside the range, 0xFF and instance slots
    ja .rank                    ; alike)
    mov ah, bh
.rank:
    xor di, di                  ; DI = the best candidate so far, 0 = none
    mov al, ah                  ; AL = its level, seeded at the claimant's own
    mov si, mem_tab             ; so that only STRICTLY cheaper records win
    mov cx, MEM_MAX
.scan:
    cmp word [ss:si+MC_SEG], 0
    je .next
    mov dl, [ss:si+MC_OWN+1]
    cmp dl, MEM_PG_MIN          ; not a cache at all: never takeable
    jb .next
    cmp dl, al                  ; at or above the best so far (which starts at
    jae .next                   ; the claimant's rank): leave it
    mov al, dl                  ; a new cheapest
    mov di, si
.next:
    add si, MC_SIZE
    loop .scan
    or di, di
    jz .none
    mov bx, [ss:di+MC_OWN]
    call mem_pg_forget          ; the owner is told BEFORE the block goes, so
                                ; there is no window in which the word names
                                ; memory somebody else already holds
    mov word [ss:di+MC_SEG], 0
    popf
    clc
    jmp short .out
.none:
    popf
    stc
.out:
    pop di
    pop si                      ; SI BEFORE DX - the pair was crossed here, and
    pop dx                      ; SI is mem_claim_x's PARAMETER (SPEC.md
                                ; 50.6.4.1): the shed-and-retry restaged the
                                ; caller's DX as [mem_dma], which is half of
                                ; the very defect mem_claim_1's header
                                ; describes, through a different door
    pop cx
    pop bx
    pop ax
    ret

; mem_pg_forget - zero the word mem_pg_own names for owner BX
; preserves every register and the flags
mem_pg_forget:
    pushf
    push ax
    push bx
    push cx
    push si
    mov si, mem_pg_own
.lp:
    mov ax, [si+PGO_TAG]
    or ax, ax
    jz .out
    mov cx, bx
    sub cx, ax                  ; d = owner - tag, UNSIGNED: a row above the
    cmp cx, [si+PGO_CNT]        ; owner wraps high and fails this compare
    jb .hit
    add si, PGO_SIZE
    jmp short .lp
.hit:
    shl cx, 1                   ; a word per entry (see the header: there is
    add cx, [si+PGO_BASE]       ; no stride column because there is nothing
    mov si, cx                  ; for it to be)
    mov word [si], 0
.out:
    pop si
    pop cx
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

; =============================================================================
; HEAP COMPACTION (SPEC.md 66)
;
; The data arena is first fit from the bottom (SPEC.md 50.2), so over a long
; session it fills with holes: a claim freed between two live ones leaves a
; run nothing that big will fit in, and `mem_avail` reports a healthy TOTAL
; over a largest RUN that refuses a 116KB module (docs/FIELD-NOTES.md 2).
; Compaction slides the movable claims down against each other and gives the
; middle of the heap back in one piece.
;
; WHY IT IS NOT A WORD POKE. SPEC.md 50.6's purgeable contract - one kernel
; word names the block, and a zero in it already means "do it the slow way" -
; is the obvious mechanism to borrow, and it is not enough for either consumer
; that matters. The Disk window's view cache has TWO naming words, FS_VSEG in
; the state block and the [fm_vseg] global "every reader looks at"; Tracker
; normalises a base segment per sample off its module base and never reads
; [trk_modseg] again after load. So a move NOTIFIES, through the holder's own
; dispatcher, and the holder puts its own derived state right.
;
; WHAT MAY MOVE is one predicate, mem_can_move, and the default is PINNED -
; a claim moves only if its holder has declared a relocation proc. That is
; what makes this safe to adopt one claimant at a time rather than all at
; once, and it is why nothing in the tree changed behaviour on the commit
; that introduced it.
;
; THE THREE CONTEXTS THAT CAN HOLD A POINTER INTO A BLOCK, and what bounds
; each - the whole safety argument, and the reason a wrong one here is silent
; memory corruption rather than a crash:
;
;   OTHER TASKS      [sch_lock], raised across the moves, which pauses task
;                    switching while the tick keeps running - dsk_xfer's own
;                    bargain for exactly this (SPEC.md 18.9). It does NOT
;                    reach a task already suspended mid-access, which is what
;                    pins a claim whose owner has a live worker until SPEC.md
;                    66.5's park handshake exists.
;   INTERRUPTS       not bounded by sch_lock at all, so any claim with
;                    MC_DMA != 0 is pinned outright. That is right twice over:
;                    a bus master may be mid-transfer, and the 64KB page rule
;                    is a property of the ADDRESS, so such a block cannot be
;                    relocated even in principle.
;   THE CALLER       mem_claim is reached from the UI task or a window
;                    callback (a worker may not claim - SPEC.md 20.6 rule 7),
;                    so the caller may itself be a holder deeper in its own
;                    frame - menu_drop holds the save-under across a whole
;                    menu, and a menu command can claim. Bounded by the
;                    holder's own discipline, which is SPEC.md 66.3 rule 2:
;                    re-read your base at each use, and never carry a derived
;                    pointer across a claim. menu_drop already obeys it.
; =============================================================================

; -----------------------------------------------------------------------------
; mem_can_move - may the compactor relocate this claim? (module internal)
; in:  SI = record pointer
; out: CF = 0 movable, CF = 1 pinned
; clobbers: nothing else (flags)
;
; ONE routine decides, and every rule that pins a block lives in it - the
; `dsk_xfer` reads-the-class-while-BX-is-still-the-row discipline. Three of
; the tests below can never fire on a correctly-behaved holder, because
; nothing declares a region or a DMA buffer movable; they are here because
; "no caller does that" is not the same statement as "this cannot happen",
; and the failure it would buy is the worst one this tree can ship.
; -----------------------------------------------------------------------------
mem_can_move:
%ifdef NOCOMPACT
    stc                         ; make HEAPCOMPACT=0: nothing can move on this
    ret                         ; build, so mem_avail's plan must model the
                                ; heap it really has (SPEC.md 66.8)
%endif
    push ax
    push bx
    push di
    cmp word [ss:si+MC_RLOC], 0
    je .pin                     ; the default: nobody declared it movable
    cmp word [ss:si+MC_DMA], 0
    jne .pin                    ; a bus master's, and unrelocatable in
                                ; principle (see the header)
    call mem_in_xfer            ; ...and the block a DISK TRANSFER is reading
    jc .pin                     ; into, which the kernel is holding a pointer
                                ; into on its own stack (SPEC.md 66.3 rule 5)
    mov bx, [ss:si+MC_OWN]
    cmp bh, MEM_PG_MIN
    jb .notpg
    cmp bh, MEM_PG_MAX
    jbe .pin                    ; A PURGEABLE CACHE IS NEVER MOVED, AND NEVER
                                ; NEEDS TO BE (SPEC.md 50.6.1). It is not that
                                ; it could not be: it is that mem_claim's loop
                                ; already reaches the same room by SHEDDING it
                                ; one step later, and the shed is the cheaper
                                ; primitive by every measure - a 63KB block
                                ; with a 64KB page head cannot be packed
                                ; tight, and its contents are disposable by
                                ; definition (a re-claim calls dsk_rah_flush,
                                ; so a cache never comes back warm from any
                                ; path). A relocation here would copy 63KB to
                                ; preserve nothing.
                                ;
                                ; This USED to cite the top-down placement as
                                ; the reason, and the compactor's pin was then
                                ; resting on an invariant the allocator was
                                ; not delivering (50.6.1.1). The pin outlived
                                ; its justification and was right anyway,
                                ; which is worth one comment: a correct answer
                                ; held up by a false premise is a defect that
                                ; has not fired yet.
.notpg:
    cmp bx, MEM_K_DRV
    je .pin                     ; a driver's image and an on-demand module's
    cmp bx, MEM_K_MOD           ; are each a CS (SPEC.md 2.8/51.3), and a base
    je .pin                     ; that is a CS can never move. The boot overlay
                                ; was a third of these until SPEC.md 2.9.6 put
                                ; it in stage 2's blob, where it is not a claim
                                ; at all and there is nothing to pin
    cmp bx, INST_MAX
    jae .notslot
    call mem_is_region          ; an instance slot owns a package's REGION and
    jc .pin                     ; a built-in's data claims alike, and only the
    jmp short .ok               ; first of those is a CS
.notslot:
    cmp bh, 0xFF
    je .ok                      ; a kernel tag: its proc is the kernel's own
    call mem_busy_seg           ; ...otherwise a package's or a driver's own
    jc .pin                     ; segment, and somebody may be RUNNING in it
.ok:
    pop di
    pop bx
    pop ax
    clc
    ret
.pin:
    pop di
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; mem_is_region - is the claim at SI a package's REGION? (module internal)
; in:  SI = record, BX = its owner (an instance slot, already tested)
; out: CF = 1 it is a region (so: pinned), CF = 0 it is a data claim
; clobbers: nothing else (flags)
;
; A region is the one claim whose base IS the instance's I_SPTR, and only a
; KIND_PKG record has one - a built-in stamps its DATA claims with its slot too
; (the Disk window's listing cache), which is the distinction that made SPEC.md
; 50.6.1's purgeable ceiling dead on arrival before that ceiling was deleted
; outright. This test OUTLIVED it: mem_can_move is the caller it still has, and
; "a region can never move, because its base IS its CS" is the older rule.
; -----------------------------------------------------------------------------
mem_is_region:
    push ax
    push di
    mov di, bx
    mov ax, di
    shl ax, 1                   ; ax = slot * I_RECSZ (32); no shl reg,imm on
    shl ax, 1                   ; an 8086 and CL is not free here
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, inst_tab
    mov di, ax
    test byte [di+I_KIND], KIND_PKG
    jz .no
    mov ax, [ss:si+MC_SEG]
    cmp ax, [di+I_SPTR]
    jne .no
    pop di
    pop ax
    stc
    ret
.no:
    pop di
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; mem_busy_seg - is anybody RUNNING in the image at BX? (module internal)
; in:  BX = a segment that owns claims (a package region or a driver image)
; out: CF = 1 pin it, CF = 0 nothing is running in there
; clobbers: nothing else (flags)
;
; A package's data claims are owned by the segment it runs in (SPEC.md 50.2),
; and its WORKER reads them with no lock of any kind - Tracker's mixer walks
; sample data through a segment it derived at load time and holds in a
; register across the mix. [sch_lock] stops a switch from happening NOW; it
; does nothing about a task already suspended inside such a loop, so an
; instance that owns a live worker keeps its claims where they are.
;
; A DRIVER's segment is pinned unconditionally, and the reason is the same one
; stated the other way round: a driver has no instance record to read I_TASK
; out of, and it may well own a service task (SPEC.md 34.5's transient refill
; task is exactly one). "I cannot tell" and "it is busy" have to be the same
; answer here.
;
; Both of these lift when SPEC.md 66.5's park handshake lands - the point of
; putting them behind one predicate is that they lift in one place.
; -----------------------------------------------------------------------------
mem_busy_seg:
    call KERNEL_SEG:inst_seg_parked     ; CF = 0: no worker, or one that has
    jnc .free                           ; PARKED at OSAPI_TASK_ALIVE (66.5).
                                        ; A segment matching no instance -
                                        ; a driver's image - answers CF = 1,
                                        ; which is the header's rule
    mov byte [mem_wpin], 1      ; ...and RECORD that this is why we refused, so
                                ; mem_compact knows a park would be worth
                                ; asking for. Without it the compactor either
                                ; pays for a park on every failed claim or
                                ; never asks for one at all
    stc
    ret
.free:
    clc
    ret

; -----------------------------------------------------------------------------
; mem_in_xfer - is a disk transfer reading INTO the claim at SI? (internal)
; in:  SI = record pointer
; out: CF = 1 the destination is inside it, CF = 0 it is not
; clobbers: nothing else (flags)
;
; SPEC.md 66.3 rule 5. [mem_pinseg] is dsk_xfer's, set and cleared inside its
; own [sch_lock] hold, and 0 is the value it spends every path back to.
;
; Factored because it now has TWO callers that must not drift: mem_can_move,
; which refuses to relocate such a block, and mem_cp_drop, which refuses to
; dissolve one. The second is the newer and the sharper - a relocation would
; at least leave the bytes somewhere, where a free hands them to whoever asks
; next while the controller is still writing.
; -----------------------------------------------------------------------------
mem_in_xfer:
    push ax
    push bx
    mov ax, [mem_pinseg]
    or ax, ax
    jz .no
    cmp ax, [ss:si+MC_SEG]
    jb .no
    mov bx, [ss:si+MC_SEG]
    add bx, [ss:si+MC_PARA]
    cmp ax, bx
    jae .no
    pop bx
    pop ax
    stc
    ret
.no:
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; mem_cp_drop - may this pass DISSOLVE the claim at SI? (module internal)
; in:  SI = a record mem_can_move has just PINNED
; out: CF = 0 it is a cache and this pass may drop it, CF = 1 it is a barrier
; clobbers: nothing else (flags)
;
; SPEC.md 66.10. The compactor's third outcome, and the one that costs nothing
; to execute: a purgeable block is never relocated - 63KB with a 64KB page
; head cannot be packed tight, and a re-claim calls dsk_rah_flush, so copying
; it would preserve nothing - but it can be made to STOP BEING A BARRIER,
; which is the whole of what it was doing wrong.
;
; [mem_pg_rank] IS THE "ONLY UNDER PRESSURE" RULE, and it is a parameter that
; already existed: mem_compact takes the size of the pending claim, and 0
; means "compact regardless, nobody is waiting". mem_unblob is the one caller
; that passes 0 - the boot giving the loader's floor back - and it must not
; drop, because the A: mount claimed this cache four routines ago and the
; desktop would then run with no read-ahead until the next mount.
; -----------------------------------------------------------------------------
mem_cp_drop:
    cmp byte [mem_pg_rank], 0
    je .no
    call mem_in_xfer            ; A DROP IS NOT A WEAKER PIN THAN A MOVE HERE.
    jc .no                      ; The walk reaches this AFTER mem_can_move has
                                ; already refused, so every other reason that
                                ; routine pins is being deliberately overruled
                                ; - which is right for all of them but one.
                                ; MC_DMA is an address property and we are not
                                ; changing the address; a region or a driver
                                ; segment is never purgeable. An IN-FLIGHT
                                ; TRANSFER is different in kind: dsk_rah_fill
                                ; reads INTO this very cache, so freeing it
                                ; there would hand a live int 13h destination
                                ; to the next claimant. No path reaches here
                                ; from inside dsk_xfer today - mem_claim is
                                ; the only way in and a transfer does not
                                ; claim - and "no caller does that" is not the
                                ; same statement as "this cannot happen",
                                ; which is mem_can_move's own rule
    push ax
    mov al, [ss:si+MC_OWN+1]    ; mem_shed_one's two compares, and they have to
    cmp al, MEM_PG_MIN          ; be the same two: is it a cache at all, and is
    jb .nopop                   ; it strictly CHEAPER than the claimant. The
    cmp al, [mem_pg_rank]       ; second subsumes the range's top - 0xFF is
    jae .nopop                  ; never below a level, and neither is an
    pop ax                      ; instance slot
    clc
    ret
.nopop:
    pop ax
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; mem_cp_next - the live claim with the lowest base at or above BX (internal)
; in:  BX = a base segment to search from
; out: CF = 0 and SI = its record; CF = 1 there is none
; clobbers: SI (the output), flags
;
; The compactor walks the arena in ascending base order and this is the walk.
; A sort would be the obvious alternative and is the wrong shape: the RUN pass
; moves blocks while it walks, so any order captured up front is stale by the
; second step - where "the lowest base at or above the last one processed"
; stays true through every move, because a claim only ever slides DOWN and
; only ever to a paragraph the walk has already passed.
; -----------------------------------------------------------------------------
mem_cp_next:
    push ax
    push cx
    push dx
    push di
    xor si, si                  ; SI = best record so far, 0 = none
    mov dx, 0xFFFF              ; DX = its base
    mov di, mem_tab
    mov cx, MEM_MAX
.scan:
    mov ax, [ss:di+MC_SEG]
    or ax, ax
    je .next                    ; a free record
    cmp ax, bx
    jb .next                    ; already behind the walk
    cmp ax, dx
    jae .next                   ; not the lowest
    mov dx, ax
    mov si, di
.next:
    add di, MC_SIZE
    loop .scan
    or si, si
    jz .none
    pop di
    pop dx
    pop cx
    pop ax
    clc
    ret
.none:
    pop di
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; mem_cp_plan - what WOULD compaction achieve? (module internal)
; in:  nothing
; out: CX = the largest free run, in paragraphs, that compaction would leave;
;      DX = how many claims would actually move
; clobbers: CX/DX (the outputs) and the flags
;
; **The dry run is the performance argument** (SPEC.md 66.4). A compaction
; that moves 100KB costs about a quarter of a second on the target machine,
; and a compaction that moves 100KB and then still refuses the claim costs the
; same quarter second for nothing. So the walk is done twice: once in
; arithmetic, deciding whether the answer is worth having, and only then in
; memory. A refusal therefore costs one table walk - MEM_MAX is 32 - rather
; than a heap copy, which is what lets this sit on the claim path at all.
;
; It is the SAME walk as mem_cp_run below, deliberately step for step: the two
; disagreeing is the one way this feature can promise room it does not deliver.
; -----------------------------------------------------------------------------
mem_cp_plan:
    push ax
    push bx
    push si
    xor dx, dx                  ; movers
    xor cx, cx                  ; largest run
    mov byte [mem_wpin], 0      ; ...and "a worker is what stopped me", which
                                ; mem_busy_seg raises (SPEC.md 66.5)
    mov bx, [mem_base]          ; BX = the fill point: the lowest paragraph
                                ; nothing has been packed into yet
    mov word [mem_cp_key], 0    ; ...and the search key, the ORIGINAL base + 1
                                ; of the last claim processed. It is the one
                                ; quantity that reads the same in both passes,
                                ; because a moved claim's NEW base is behind
                                ; the walk and its old one is not - which is
                                ; what lets the run move blocks while it walks
.loop:
    push bx
    mov bx, [mem_cp_key]
    call mem_cp_next            ; SI = the next claim up (pop writes no flags)
    pop bx
    jc .tail
    mov ax, [ss:si+MC_SEG]
    inc ax
    mov [mem_cp_key], ax
    call mem_can_move
    jc .pinned
    cmp [ss:si+MC_SEG], bx
    je .stay                    ; already packed: it would not move
    inc dx
.stay:
    add bx, [ss:si+MC_PARA]     ; the fill point advances by its size
    jmp short .loop
.pinned:
    call mem_cp_drop            ; ...unless it is a cache this pass may
    jc .barrier                 ; dissolve (SPEC.md 66.10): the fill point does
                                ; NOT advance and no barrier is recorded, so
                                ; the plan counts exactly the room the run
                                ; below is about to produce. The KEY has
                                ; already stepped past it, so it is not
                                ; revisited
    inc dx
    jmp short .loop
.barrier:
    mov ax, [ss:si+MC_SEG]      ; the gap under a pinned claim is a hole that
    sub ax, bx                  ; compaction cannot close - it is a barrier,
    jbe .nogap                  ; and claims above it still pack against it
    cmp ax, cx
    jbe .nogap
    mov cx, ax
.nogap:
    mov bx, [ss:si+MC_SEG]      ; the fill point resumes above it
    add bx, [ss:si+MC_PARA]
    jmp short .loop
.tail:
    mov ax, [mem_top]           ; ...and the run above everything, which is the
    sub ax, bx                  ; one compaction is usually enlarging
    jbe .done
    cmp ax, cx
    jbe .done
    mov cx, ax
.done:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_cp_run - do it (module internal)
; in:  nothing; [sch_lock] already raised by mem_compact
; out: nothing (all registers preserved)
;
; mem_cp_plan's walk exactly, with the move in it. Order inside one block is
; binding and is the purgeable shed's rule turned round: the bytes go first,
; then the record, then the holder is told - so there is no instant in which
; the map names a block whose contents have not arrived, and none in which the
; holder has been sent to an address the map does not agree with.
; -----------------------------------------------------------------------------
mem_cp_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [mem_base]
    mov word [mem_cp_key], 0    ; the search key, as in the plan
.loop:
    push bx
    mov bx, [mem_cp_key]
    call mem_cp_next
    pop bx
    jc .done
    mov ax, [ss:si+MC_SEG]      ; step the key past the ORIGINAL base, banked
    inc ax                      ; here because the move is about to overwrite it
    mov [mem_cp_key], ax
    call mem_can_move
    jc .pinned
    mov di, [ss:si+MC_SEG]
    cmp di, bx
    je .packed                  ; already where it belongs
    push si
    push bx
    mov cx, [ss:si+MC_PARA]     ; only what is claimed travels
    mov si, di
    mov di, bx
    call mem_bcopy              ; source SI, destination DI, CX paragraphs -
                                ; segment-stepped, so a block over 64KB moves
                                ; without a 16-bit offset ever binding
    pop bx
    pop si
    mov ax, [ss:si+MC_SEG]      ; AX = where it WAS, for the notification
    mov [ss:si+MC_SEG], bx      ; the map follows the bytes...
    push bx
    mov dx, bx
    mov bx, ax
    call mem_reloc_call         ; ...and the holder follows the map (BX = old
    pop bx                      ; base, DX = new)
.packed:
    add bx, [ss:si+MC_PARA]
    jmp short .loop
.pinned:
    call mem_cp_drop            ; the plan's step, for real (SPEC.md 66.10).
    jc .barrier                 ; The owner is told BEFORE the record goes -
    push bx                     ; mem_shed_one's order, for its reason: there
    mov bx, [ss:si+MC_OWN]      ; is no instant in which a kernel word names
    call mem_pg_forget          ; memory somebody else already holds. BX is
    pop bx                      ; the FILL POINT and does not move: that is
    mov word [ss:si+MC_SEG], 0  ; what makes this walk the plan's, step for
    jmp short .loop             ; step
.barrier:
    mov bx, [ss:si+MC_SEG]
    add bx, [ss:si+MC_PARA]
    jmp short .loop
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_reloc_call - tell a holder its block moved (module internal)
; in:  SI = the record, BX = the old base segment, DX = the new one
; out: nothing (all registers preserved)
;
; wm_pkgcall's shape, with the far pointer taken out of the claim record
; instead of a window record - which is drv_call's relationship to it as well
; (SPEC.md 51). So a holder writes an ordinary near proc with a near `ret`
; and never a `retf`, exactly as it does for W_PAINT: the dispatcher at
; PKG_DISP owns the far boundary, and a missing `retf` cannot exist because
; nobody writes one.
;
; The kernel's own holders go through cw_mem_disp, which is that same three
; bytes living in .text - so a kernel relocation proc is a near proc in .text
; too, whatever section the module around it is in. memory.inc is .cold, so
; this could not be a near call in any case (tools/os88ovlchk.py refuses one),
; and making both cases far makes them one code path rather than two.
;
; DS is the holder's, because the proc's whole job is to write the holder's
; own data. ES is KERNEL_SEG, wm_pkgcall's promise, so a package proc may read
; the kernel structures it is used to reading.
; -----------------------------------------------------------------------------
; The far pointer is BUILT rather than written as an immediate, because the
; 8086 has no `call far <reg>:<imm>` and this module is `.cold`, where a data
; directive is refused outright (tools/os88ovlchk.py, SPEC.md 2.6 rule 1). It
; is staged in `.lowbss` and reached with an `ss:` override for the reason
; mem_tab is: DS belongs to the HOLDER by the time the call goes out, so a
; scratch word in the kernel's own segment would be unreachable at exactly the
; instruction that needs it. One word of shared state, and it is safe because
; the compactor is not re-entrant ([mem_cp_busy]) and runs with [sch_lock]
; raised.
mem_reloc_call:
    push ax
    push bx                     ; ALL of them, because the proc is a window
    push cx                     ; callback in every other respect and a window
    push dx                     ; callback may clobber AX/BX/CX/DX/SI/DI/ES
    push si                     ; (SPEC.md 13). mem_cp_run reads SI - the
    push di                     ; record - straight after this returns, and a
    push bp                     ; proc that used SI would have moved the fill
    push ds                     ; point onto a live block
    push es
    mov ax, KERNEL_SEG
    mov es, ax                  ; wm_pkgcall's ES contract: the callback may
                                ; read the kernel structures it always reads.
                                ; NAMED and not `push cs`, which in .cold is
                                ; COLD_SEG and would be quietly wrong
    mov bp, [ss:si+MC_RLOC]     ; BP = the proc, PKG_DISP's own contract
    mov ax, [ss:si+MC_OWN]
    cmp ah, MEM_PG_MIN
    jb .notk
    cmp ah, MEM_LVL_TOP
    jbe .kernel                 ; 0xFExx and 0xFFxx alike: a kernel tag. A
                                ; SEGMENT can never collide with this range -
                                ; conventional memory ends at 0xA000
.notk:
    cmp ax, INST_MAX
    jb .kernel                  ; an instance slot: a BUILT-IN's data claim.
                                ; A region is pinned and never arrives here
    mov word [ss:mem_fptr], PKG_DISP
    mov [ss:mem_fptr+2], ax
    mov ds, ax                  ; DS = the holder's own segment: the proc's
    jmp short .go               ; whole job is to write the holder's own data
.kernel:
    mov word [ss:mem_fptr], cw_mem_disp
    mov word [ss:mem_fptr+2], KERNEL_SEG
    mov ax, KERNEL_SEG          ; a kernel relocation proc is a NEAR proc in
    mov ds, ax                  ; .text, reached through the one shim, so DS
                                ; is the kernel's own
.go:
    call far [ss:mem_fptr]
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mem_compact - close the data arena's holes (SPEC.md 66.4)
; in:  AX = the KB the pending claim needs (0 = compact whatever the size, and
;           DISSOLVE NOTHING - see mem_cp_drop and SPEC.md 66.10.1)
;      BX = the owner it is being compacted FOR, read only when AX is non-zero:
;           its high byte is the rank a cache has to be cheaper than before
;           this pass may drop it (SPEC.md 50.6.4)
; out: CF = 0 something moved, so the map has changed and the claim is worth
;      retrying; CF = 1 nothing moved and nothing was copied
; clobbers: nothing else (flags)
;
; Called from mem_claim's retry loop, AHEAD of the shed. That ordering is
; deliberate and is an improvement on what the shed did alone: mem_shed_one
; can destroy MEM_P_DIRW, whose own priority is set at "a long operation
; getting much longer" - seconds of int 13h on the field machine - where
; compaction costs a memcpy in tenths of a second. Cheapest first is what the
; priority ladder was built to express, and a memcpy is cheaper than that
; cache.
;
; TERMINATION, which the retry loop rests on: the run packs every movable
; claim against the one below it, so a second call immediately after finds
; every one of them already at its fill point, counts no movers, and answers
; CF = 1. The loop can therefore compact at most once per shed, and sheds are
; bounded by MEM_MAX.
; -----------------------------------------------------------------------------
mem_compact:
%ifdef NOCOMPACT
    stc                         ; make HEAPCOMPACT=0: the A/B, with the body
    ret                         ; gone and not merely the call (SPEC.md 66.8)
%else
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [mem_cp_busy], 0
    jne .none                   ; a relocation proc that claimed would arrive
                                ; here inside the walk it is being driven by,
                                ; and the walk's own map would move under it
                                ; (SPEC.md 66.3 rule 3 forbids it; this is what
                                ; makes the forbidden thing bounded)
    mov cl, 6
    shl ax, cl                  ; KB -> paragraphs; 0 stays 0
    jc .none                    ; more than a megabyte: nothing to do here
    mov di, ax                  ; DI = what the caller needs
    xor al, al                  ; ...and the rank a drop is judged against
    or di, di                   ; (SPEC.md 66.10.1): ZERO when nobody is
    jz .rank                    ; waiting, so mem_unblob's "compact regardless"
    mov al, MEM_LVL_TOP         ; dissolves nothing. Otherwise the CLAIMANT's
    cmp bh, MEM_PG_MIN          ; own level, mem_shed_one's derivation exactly -
    jb .rank                    ; a cache may only take a cheaper cache, and
    cmp bh, MEM_PG_MAX          ; putting that rule in one door and not the
    ja .rank                    ; other is how SPEC.md 50.6.4 gets undone
    mov al, bh
.rank:
    mov [mem_pg_rank], al

    inc byte [mem_cp_busy]
    inc byte [sch_lock]         ; pause task switching across the PLAN as well
                                ; as the moves - the dsk_xfer bargain (SPEC.md
                                ; 18.9), where the tick, the mouse and any
                                ; sound IRQ keep running. The plan is pure
                                ; arithmetic over 32 records and costs
                                ; microseconds, and taking it inside the same
                                ; window is what stops the map changing
                                ; between deciding a move is worth making and
                                ; making it
    call mem_cp_plan            ; CX = the run it would leave, DX = movers
    call mem_cp_worth           ; CF = 0: run it
    jnc .doit

    ; --- it is not enough. Would standing the workers still make it enough? --
    ;
    ; SPEC.md 66.5. Asking costs up to INST_PARKW ticks, so it is asked ONLY
    ; when the last plan actually refused a claim because somebody might be
    ; running in it - otherwise every failed claim on a machine with a worker
    ; would pay for a park that could not have helped.
    cmp byte [mem_wpin], 0
    je .undo
    dec byte [sch_lock]         ; a park needs the machine RUNNING: the workers
    call KERNEL_SEG:cw_inst_park_req    ; have to reach OSAPI_TASK_ALIVE
    inc byte [sch_lock]
    inc byte [mem_parked]       ; ...and it is withdrawn on EVERY path out
    call mem_cp_plan            ; ...then ask again, with whoever parked now
    call mem_cp_worth           ; movable
    jc .undo
.doit:
    call mem_cp_run
    call mem_cp_unpark
    dec byte [sch_lock]
    dec byte [mem_cp_busy]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.undo:
    call mem_cp_unpark
    dec byte [sch_lock]
    dec byte [mem_cp_busy]
.none:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
%endif

; mem_cp_worth - is the plan in CX/DX worth executing? (module internal)
; in:  CX = the run it would leave, DX = movers, DI = paragraphs wanted
; out: CF = 0 run it, CF = 1 do not
mem_cp_worth:
    or dx, dx
    jz .no                      ; nothing can move: not a byte is touched
    or di, di
    jz .yes                     ; "compact regardless"
    cmp cx, di
    jb .no                      ; IT STILL WOULD NOT FIT. Move nothing: the
                                ; caller is about to be refused either way and
                                ; a quarter second of memcpy would not change
                                ; that (SPEC.md 66.4)
.yes:
    clc
    ret
.no:
    stc
    ret

; mem_cp_unpark - release the workers, if this pass ever asked them to stand
; preserves every register and the flags
mem_cp_unpark:
    pushf
    cmp byte [mem_parked], 0
    je .out
    mov byte [mem_parked], 0
    call KERNEL_SEG:inst_park_end
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; mem_movable - declare (or withdraw) a claim's relocation proc (SPEC.md 66.2)
; in:  DX = the claim's base segment, AX = the near offset of the relocation
;      proc in the OWNER's segment (0 = pin it again), BX = the owner word
; out: CF = 0 recorded; CF = 1 no such claim, or not this owner's
; clobbers: nothing else (flags)
;
; Separate from the claim rather than an argument to it, for three reasons
; that all turned out to be the same one. There are three claim entries
; (mem_claim, mem_claim_hi, mem_claim_dma) and a variant of each would be six.
; A claim already held can be declared movable, so adopting this is one line
; at a time rather than a rewrite of a package's startup. And AX = 0 gives a
; holder the other half - a way to PIN itself for a stretch - which is the
; Macintosh Memory Manager's HLock beside its purgeable handle, and this
; module is already that machine's homage.
;
; It does NOT test whether the claim is one the compactor would actually move:
; mem_can_move decides that, alone, every time it is asked. A holder is
; allowed to declare a claim movable that the kernel then pins for reasons of
; its own - having a live worker, being a driver's - and to have that decision
; change under it later without being told.
; -----------------------------------------------------------------------------
mem_movable_x:
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
    jne .next                   ; the mem_free fence: yours, or not at all
    mov [ss:si+MC_RLOC], ax
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
; WHAT IT IS NOT. It does not compact the heap by sliding OTHER claims down -
; SPEC.md 66 does that, and this routine deliberately stays out of it. The two
; answer different questions: mem_regrow resizes ONE claim whose holder is
; standing right there and takes the new base out of DX, where the compactor
; moves claims whose holders are not asking for anything and have to be TOLD.
; So a regrow does not call the relocation proc even when the claim has one -
; that would tell a holder twice, in two different ways, about one move
; (SPEC.md 66.7) - and MC_RLOC rides through all three paths untouched.
;
; Path 2 is still worth what it always was: a claim that grows in place never
; leaves a hole behind it, and fragmentation not created needs no repairing.
; -----------------------------------------------------------------------------
mem_regrow:
    push ax
    push bx
    push cx
    push si
    push di
    pushf
    cli
    cmp byte [mem_cp_busy], 0   ; not from inside a relocation proc either
    jne .fail                   ; (SPEC.md 66.3 rule 3): path 3 would claim
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
; The owner test is what stops a package freeing the kernel's own claims,
; or another instance's canvas, with one wrong word.
; -----------------------------------------------------------------------------
mem_free_x:
    push cx
    push si
    pushf
    cli
    cmp byte [mem_cp_busy], 0   ; a relocation proc may not free (SPEC.md 66.3
    jne .busy                   ; rule 3): the walk is standing on the record
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
.busy:
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
mem_free_owner_x:
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
mem_free_rec_x:
    push ax
    push bx
    push cx
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    mov bx, ax                  ; a built-in's claims are stamped with its
    call mem_free_owner_x         ; instance slot (the Disk window's cache)
    test byte [di+I_KIND], KIND_PKG
    jz .done
    mov bx, [di+I_SPTR]         ; ...and a package's with its segment, which
    call mem_free_owner_x         ; is what it could name itself by
.done:
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; mem_avail - how much heap is there, and how much of it in one piece?
;
; in:       nothing
; out:      AX = largest free run in KB, BX = total free KB
; clobbers: nothing else (flags)
;
; The query a package sizes itself from (SPEC.md 50.3), and the whole of its
; contract is one sentence: IT ANSWERS THE QUESTION mem_claim ANSWERS. Every
; consumer sizes itself DOWN from this number - Paint's canvas, Tracker's
; module, ArtfulType's document, the recorder's tiers, bb_canfit's greying -
; so a caller that CHECKS FIRST AND BAILS never discovers a claim it would
; have been granted. Under-reporting here is not a safe error; it is the only
; direction in which the error is invisible.
;
; **PURGEABLE claims are counted as FREE** (SPEC.md 50.6.3). They were counted
; as occupied, on the reasoning that a conservative direction is a safe one -
; and that is the mistake above, in its first half. Measured on a 256KB
; machine, this said 21.5KB while a caller went on to fund an 18KB claim AND a
; 16KB one out of the same heap.
;
; **AND THE LARGEST RUN IS mem_cp_plan's, BECAUSE mem_claim COMPACTS** (SPEC.md
; 66.10.3). That is the second half, and it went unfixed for a release: the
; refusal path is COMPACT, then shed, then retry - so the run a claimant can
; have is the one compaction would leave, not the one lying about now. This
; routine used to walk the holes itself, measuring each candidate base to the
; nearest claim above it, and a movable 3KB claim sitting above a cache was
; enough to make it answer 373.0K where the allocator would hand out 442.5K.
;
; So it does not have a scan of its own any more. mem_cp_plan IS the model of
; what mem_claim will do, it is already written, and it is already the routine
; the compactor decides with - which makes the two the same arithmetic rather
; than two walks with matching comments. It costs no more: the walk it
; replaced was O(MEM_MAX^2) too.
;
; The TOTAL is still a walk of its own, and needs no compaction: packing
; claims together does not change how many bytes they occupy.
; -----------------------------------------------------------------------------
mem_avail_x:
    mov al, MEM_LVL_TOP         ; an ORDINARY claim's rank: every cache counts
mem_avail_lvl_x:                  ; as free, because mem_claim sheds them all.
                                ; AL = the caller's own level otherwise, and
                                ; only caches BELOW it count (SPEC.md 50.6.4)
    push cx
    push dx
    push si
    push di
    pushf
    cli
    cmp byte [mem_cp_busy], 0   ; INSIDE A COMPACTION, which means a relocation
    jne .nested                 ; proc is asking (SPEC.md 66.3 rule 3 forbids
                                ; it claiming, and this is the same rule): the
                                ; walk below would reset [mem_cp_key] and
                                ; [mem_wpin] under the walk driving it. Zero
                                ; is the honest answer to a holder that has no
                                ; business sizing anything mid-move
    mov [mem_pg_rank], al       ; inside the window, with the other three
    call mem_cp_plan            ; CX = the largest run compaction would leave,
    mov ax, cx                  ; caches at or below AL dissolved with it

    mov si, mem_tab             ; total free = heap - sum of claims, and a
    mov bx, [mem_top]           ; PURGEABLE claim is only one of them when it
    sub bx, [mem_base]          ; OUTRANKS the caller - a total that disagreed
    mov cx, MEM_MAX             ; with the run scan above would be the same lie
.tot:                           ; in the other register. DL, not AL: AX is
    cmp word [ss:si+MC_SEG], 0  ; carrying the largest run by now
    je .tnext
    mov dl, [ss:si+MC_OWN+1]
    cmp dl, MEM_PG_MIN
    jb .tsolid
    cmp dl, [mem_pg_rank]
    jb .tnext
.tsolid:
    sub bx, [ss:si+MC_PARA]
.tnext:
    add si, MC_SIZE
    loop .tot

    popf
    mov cl, 6                   ; paragraphs -> KB, rounded DOWN: a claim of
    shr ax, cl                  ; the answer must always fit
    shr bx, cl
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret
.nested:
    popf
    xor ax, ax
    xor bx, bx
    jmp short .out

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
mem_owned_kb_x:
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
    retf

; -----------------------------------------------------------------------------
; mem_claimed_kb / mem_kernel_kb - the Task Manager's two heap terms
;
; in:  AX = a running KB total
; out: AX = that total plus the claimed KB (all owners / kernel tags only)
; clobbers: nothing else (flags)
;
; ADD-style rather than plain readers because both call sites are building a
; sum in AX and have no spare register to hold a partial total in.
;
; Neither counts a PURGEABLE claim, because mem_sum_kb does not (SPEC.md
; 50.6.3): SK_CLAIM and SK_KCLAIM are the same question asked of two owner
; sets, so "of which the kernel's own" is only honest if both draw the line
; in the same place.
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
    cmp byte [ss:si+MC_OWN+1], MEM_PG_MIN  ; PURGEABLE is in nobody's total,
    jb .notpg                       ; not even System's (SPEC.md 50.6.3) - and
    cmp byte [ss:si+MC_OWN+1], MEM_PG_MAX  ; it is a RANGE now, because the
    jbe .next                       ; high byte carries the priority (50.6.4).
.notpg:                             ; It is not memory anyone can run out of -
                                    ; the next claim that needs it takes it -
                                    ; so counting it as used would be the
                                    ; misleading answer, not the honest one
    or bx, bx
    jz .take
    cmp byte [ss:si+MC_OWN+1], 0xFF    ; the kernel tags are 0xFFxx; an instance
    je .take                        ; slot is 0..INST_MAX-1
    push bx                         ; ...and a LOADED DRIVER's own buffers,
    mov bx, [ss:si+MC_OWN]             ; which name themselves by the segment the
    call drv_owns_seg_x               ; driver runs in and so match neither test
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
;
; **A PURGEABLE claim is not spoken for** (SPEC.md 50.6.3/28.4.1). This had a
; record walk of its own that counted every live record, which made SK_CLAIM
; the one figure in the module answering a different question from the rest:
; mem_avail counts a cache as free, mem_sum_kb leaves it out of every total,
; every per-row column omits it - and this put it back into the one number
; the Task Manager builds its RAM figure from. Measured on the field machine,
; that was `RAM 195/384K` over rows accounting for 132: the missing 63 being
; SPEC.md 18.95's DirRead window, which no row shows because it belongs to no
; instance. 28.4.1 fixed the HEAP half of that line by subtracting the
; purgeables in the VIEW and left the RAM half reading this.
;
; So the walk is gone and mem_sum_kb answers instead: ONE routine decides
; what is in a total, which is what stops the two drifting again. That is
; also mem_claimed_kb's first caller - it was written for this and never
; wired up, which is why the duplicate walk survived being noticed.
; -----------------------------------------------------------------------------
mem_total_kb:
    push cx
    xor ax, ax
    call mem_claimed_kb         ; AX = claimed KB, every owner, no cache
    mov bx, [mem_top]
    sub bx, [mem_base]
    mov cl, 6
    shr bx, cl
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
osapi_mem_claim_x:
    push cx
    xor cx, cx                  ; no head: no 64KB page constraint
    call osapi_mem_claim_dma_x
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
; ---- osapi_mem_claim_hi / _dma_hi - the same, from the TOP (SPEC.md 50.3.2)
; What a driver's long-lived, hardware-addressed buffer asks for. A ring the
; 8237 or a NIC holds the address of is exactly as unmovable as a package's
; region, so it belongs at the same end - and out of the arena the compactor
; and every data claim share.
osapi_mem_claim_hi_x:
    push cx
    xor cx, cx                  ; no head: no 64KB page constraint
    call osapi_mem_claim_dma_hi_x
    pop cx                      ; pop leaves CF alone, and so does ret
    ret

osapi_mem_claim_dma_hi_x:
    push bx
    call mem_own
    jc osapi_mem_claim_dma_x.fail
    call mem_claim_dma_hi_x
    jmp short osapi_mem_claim_dma_x.out

osapi_mem_claim_dma_x:
    push bx
    call mem_own                ; BX = owner word = the calling segment
    jc .fail
    call mem_claim_dma_x
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
osapi_mem_free_x:
    push bx
    call mem_own
    jc .out                     ; CF = 1 already
    call mem_free_x
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
osapi_mem_regrow_x:
    push bx
    call mem_own                ; BX = the owner ES names (CF=1 = not a
    jc .out                     ; region or driver image we know)
    call mem_regrow
.out:
    pop bx                      ; pop leaves CF and DX alone
    retf

; -----------------------------------------------------------------------------
; osapi_mem_movable - in: DX = the claim, AX = your relocation proc (0 pins it
;                     again), ES = the caller's segment
;                     out: CF = 0 recorded, CF = 1 refused (not yours)
;
; SPEC.md 66.2. The fence is mem_own's, exactly as osapi_mem_free's is: a
; package names itself by the segment it runs in and the X stub puts it in ES,
; so there is nothing to pass and nothing to forge. A package cannot make
; somebody else's claim movable, and it cannot point the kernel at a proc
; outside its own image, because the proc is called through ITS dispatcher in
; ITS segment.
; -----------------------------------------------------------------------------
osapi_mem_movable_x:
    push bx
    call mem_own
    jc .out                     ; CF = 1 already
    call mem_movable_x
.out:
    pop bx
    retf

; -----------------------------------------------------------------------------
; osapi_mem_avail - in: nothing; out: AX = largest free KB, BX = total free KB
; -----------------------------------------------------------------------------
osapi_mem_avail_x:
    jmp mem_avail_x

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
osapi_cm_alloc_x:
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
    call osapi_mem_claim_x        ; CF/DX by the heap's rules, ES the identity
    jc .refuse
    mov ax, dx                  ; main's contract: AX = the base segment
    pop dx
    pop cx
    retf
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
    retf

osapi_cm_free_x:
    push dx                     ; in: AX = the base segment; out: CF, with
    mov dx, ax                  ; AX preserved - main's contract, so the
    call osapi_mem_free_x         ; heap's DX-in shape is hidden here
    pop dx
    retf

osapi_cm_caps_x:
    push cx
    push si
    mov ch, bh                  ; BH survives in CH (CX itself is banked)
    call mem_avail_x              ; AX = largest free KB, BX = total free KB
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
    retf

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
    push dx                          ; ...or by a SEGMENT that is itself a
    mov dx, [ss:si+MC_OWN]           ; driver image: SPEC.md 52.11.7's second
    call mem_own_drv                 ; image, one level and no further. DX is
    pop dx                           ; banked because osapi_mem_free passes the
    jnc .yes                         ; segment to free in it, and `pop` leaves
                                     ; the flags alone
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

; -----------------------------------------------------------------------------
; mem_owner_of - who owns the claim based at BX?
; in:  BX = a base segment
; out: CF = 0 and BX = that claim's owner word; CF = 1 = no claim starts there
; clobbers: BX (the output), flags
;
; SPEC.md 52.11.6. The kernel has two fences that ask "is this segment one I
; handed out" - mem_own for a claim and drv_owns_seg for a driver image - and
; a driver's SECOND image (SPEC.md 52.11) answers no to both, because it is
; neither an instance, nor MEM_K_DRV, nor a row in drv_tab. Both need the same
; one step: take the owner of the claim the caller is running in and ask about
; THAT. This is the step; each fence decides for itself what a valid answer is.
; -----------------------------------------------------------------------------
mem_owner_of_x:
    push cx
    push si
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp [ss:si+MC_SEG], bx
    je .hit
    add si, MC_SIZE
    loop .scan
    pop si
    pop cx
    stc
    ret
.hit:
    mov bx, [ss:si+MC_OWN]
    pop si
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; mem_own_drv - is DX the segment of a DRIVER's image? (module internal)
; in:  DX = a candidate segment
; out: CF = 0 yes, CF = 1 no
; clobbers: flags
;
; SPEC.md 52.11.7. A driver may load a SECOND IMAGE of its own - HDD.DRV reads
; HDDTOOL.DRV when the user clicks Format or Install - and it claims that
; image through OSAPI_MEM_CLAIM like any other buffer, so the claim's owner is
; the RESIDENT's segment. The tool then runs in a segment mem_own had never
; heard of: not an instance, not MEM_K_DRV, so every claim it made was refused
; whatever its size. This is the one level of indirection that says otherwise.
;
; ONE LEVEL, deliberately: an image loaded by an image loaded by a driver is
; not a shape anything here has, and a walk that followed the chain could be
; made to loop by a table it does not own.
; -----------------------------------------------------------------------------
mem_own_drv:
    push cx
    push si
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp [ss:si+MC_SEG], dx
    jne .next
    cmp word [ss:si+MC_OWN], MEM_K_DRV
    je .yes
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
osapi_claim_snapshot_x:
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
;
; That is also where SPEC.md 39.22's `.vgabuf` lands, and it lands there by
; construction rather than by a decision anybody has to remember: those 848
; bytes are inside SK_KERN's span and outside KBUF_KB, so the residual takes
; them. It moves a kilobyte from SK_DSK to SK_IMG against the build before the
; rung existed - the buffers used to be .lowbss and so used to be counted as
; "the rest of .lowbss" - and neither row was ever the honest name for a plane
; buffer. Adding a row of its own would change a published block that a
; package reads (SPEC.md 28), for a figure nobody can act on.
; =============================================================================
SK_STK_KB  equ ((MAX_TASKS-1) * SCH_STACK + 1023) / 1024
SK_STK0_KB equ (STK0_SIZE + 1023) / 1024
SK_FAT_KB  equ (FAT_PARA * 16 + 1023) / 1024

osapi_sys_kb_x:
    push ax
    push bx
    push di
    mov ax, KERN_KB             ; ...and on a machine with NO VGA the kernel is
%if VGABUF_PARA                 ; a KB smaller, because `.vgabuf` was never
                                ; reserved (SPEC.md 39.22). Reported and not
                                ; hidden: the free figure comes out right by
                                ; construction - mem_avail is
                                ; [mem_top] - [mem_base] - claims and the
                                ; floor already moved - but the SYSTEM row is
                                ; a build constant, and left alone it would
                                ; bill a mono machine for a buffer it does not
                                ; have. That is exactly the misreporting a
                                ; heap claim was rejected for.
    test byte [vid_avail], VID_A_VGA
    jnz .kern
    sub ax, (VGABUF_PARA * 16 + 1023) / 1024
.kern:
%endif
    mov [es:di+SK_KERN], ax
    mov word [es:di+SK_BUF],  KBUF_KB
    sub ax, KBUF_KB
    mov [es:di+SK_IMG],  ax     ; ...and the IMAGE row follows it, or the three
                                ; would stop summing
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
%ifdef KERN_BIG
    mov ax, [xm_kb]             ; the pool above 1MB, 0 on tier 0 (SPEC.md 41)
%else
    xor ax, ax                  ; kern_small has no store above 1MB at all
                                ; (SPEC.md 41.11), so SK_XMS is the 0 that
                                ; tier 0 reports and the Task Manager's line
                                ; reads 'XMS 0/0K' - the same words on the
                                ; same build for the same reason
%endif
    mov [es:di+SK_XMS], ax
    pop di
    pop bx
    pop ax
    retf

; =============================================================================
; Far entries (SPEC.md 2.6): the resident thunks in kernel.asm far-call these,
; and these near-call the bodies above, so every routine here keeps its
; ordinary near `ret`.
; =============================================================================
mmf_mem_avail:            call mem_avail_x
                      retf
mmf_mem_avail_lvl:        call mem_avail_lvl_x
                      retf
mmf_mem_claim:            call mem_claim_x
                      retf
mmf_mem_claim_dma:        call mem_claim_dma_x
                      retf
mmf_osapi_mem_claim_hi:   call osapi_mem_claim_hi_x
                          retf
mmf_osapi_mem_claim_dma_hi: call osapi_mem_claim_dma_hi_x
                          retf
mmf_mem_claim_hi:         call mem_claim_hi_x
                      retf
mmf_mem_free:             call mem_free_x
                      retf
mmf_mem_free_owner:       call mem_free_owner_x
                      retf
mmf_mem_movable:          call mem_movable_x
                      retf
mmf_mem_owner_of:         call mem_owner_of_x
                      retf
mmf_osapi_claim_snapshot: call osapi_claim_snapshot_x
                      retf
mmf_osapi_mem_avail:      call osapi_mem_avail_x
                      retf
mmf_osapi_mem_claim:      call osapi_mem_claim_x
                      retf
mmf_osapi_mem_claim_dma:  call osapi_mem_claim_dma_x
                      retf
mmf_osapi_mem_free:       call osapi_mem_free_x
                      retf


; -----------------------------------------------------------------------------
section .lowbss                 ; LOW_SEG (SPEC.md 2.1). The claim map is
                                ; scanned through a pointer and never
                                ; addressed by name outside this module, so it
                                ; costs one ss: prefix per field and nothing in
                                ; the kernel's own 64KB segment - which since
                                ; hard-disk support is the binding guard
mem_tab:  resb MEM_MAX * MC_SIZE  ; the claim map (layout above)
mem_fptr: resw 2                  ; mem_reloc_call's staged far pointer. HERE
                                  ; and not in .bss because DS is the HOLDER's
                                  ; at the instruction that reads it, and SS
                                  ; is the one segment register the compactor
                                  ; can count on (SPEC.md 66.3)

section .bss


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
mem_av_pad: resb 2              ; ...and two bytes to keep the words aligned
                                ; (one of them was [mem_av_pg], which is
                                ; [mem_pg_rank] below now - see its header)
mem_cp_busy: resb 1             ; the compactor is running (SPEC.md 66.4): a
                                ; relocation proc that claims - which 66.3
                                ; rule 3 forbids - would otherwise re-enter
                                ; the walk that is driving it and move the map
                                ; under itself. Bounded rather than trusted
mem_wpin:    resb 1             ; the last plan pinned something ONLY because a
                                ; worker might be running in it, so a park is
                                ; worth asking for (SPEC.md 66.5)
mem_parked:  resb 1             ; ...and this pass has asked, so it owes the
                                ; withdrawal on every path out
mem_pg_rank: resb 1             ; THE RANK AT WHICH A PURGEABLE CLAIM COUNTS AS
                                ; TAKEABLE (SPEC.md 50.6.4): a cache is free
                                ; to this asker when its own level is >=
                                ; MEM_PG_MIN and strictly BELOW this byte.
                                ; 0 = nothing is takeable, which is what
                                ; mem_compact stores when no claim is waiting
                                ; (66.10.1).
                                ;
                                ; ONE byte for what were two: mem_avail staged
                                ; this as [mem_av_pg] to answer "how much may
                                ; I have", and the compactor needs the same
                                ; number to answer "may I dissolve this" -
                                ; asking it twice under two names is how the
                                ; two answers drift apart. Staged inside the
                                ; asker's own cli window, never a parameter
                                ; channel another task can overwrite in the
                                ; gap, and mem_avail refuses outright while
                                ; [mem_cp_busy] so a walk cannot be re-entered
                                ; and have this changed underneath it.
                                ; It spends the byte that used to sit here as
                                ; alignment padding, so the words below are
                                ; where they were
mem_pinseg:  resw 1             ; a segment a disk transfer is reading into, so
                                ; the claim holding it may not move under the
                                ; kernel's own pointer (SPEC.md 66.3 rule 5).
                                ; dsk_xfer's, set and cleared inside its
                                ; [sch_lock] hold - and 0 is the safe value it
                                ; spends every path back to
mem_cp_key:  resw 1             ; the compaction walk's cursor: the ORIGINAL
                                ; base + 1 of the last claim processed. A word
                                ; rather than a stack slot because BOTH passes
                                ; use it and the run rewrites the bases it is
                                ; walking - see mem_cp_plan
mem_top:  resw 1                  ; one past the last, from int 12h

section .text
